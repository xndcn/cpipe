// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenColorIO/OpenColorIO.h>
#include <libheif/heif.h>

#include <cpipe/color/HeifReader.hpp>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cpipe::color {
namespace {

namespace OCIO = OCIO_NAMESPACE;

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::string heif_message(const heif_error& error) {
    if (error.message == nullptr) {
        return "libheif error";
    }
    return error.message;
}

bool check_heif(const heif_error& status, std::string_view action, std::string* error) {
    if (status.code == heif_error_Ok) {
        return true;
    }
    set_error(error, std::string{action} + ": " + heif_message(status));
    return false;
}

bool fill_scene_linear_rec2020(const std::vector<std::uint8_t>& decoded_rgba, std::uint32_t width,
                               std::uint32_t height, const std::filesystem::path& config_path,
                               std::vector<float>* out, std::string* error) {
    if (out == nullptr) {
        set_error(error, "output scene-linear vector is null");
        return false;
    }

    const auto pixel_count = static_cast<std::size_t>(width) * height;
    out->resize(pixel_count * 4U);
    for (std::size_t i = 0; i < pixel_count * 4U; ++i) {
        (*out)[i] = static_cast<float>(decoded_rgba[i]) / 255.0F;
    }

    try {
        const auto config = OCIO::Config::CreateFromFile(config_path.string().c_str());
        const auto processor =
            config->getProcessor("output_srgb", "scene_linear_rec2020")->getDefaultCPUProcessor();
        OCIO::PackedImageDesc desc{out->data(), static_cast<long>(width), static_cast<long>(height),
                                   4L};
        processor->apply(desc);
    } catch (const std::exception& e) {
        set_error(error, std::string{"OCIO inverse transform failed: "} + e.what());
        return false;
    }

    return true;
}

}  // namespace

cpipe_status_t read_heif_sdr(const std::filesystem::path& path, HeifInfo* out, std::string* error) {
    return read_heif_sdr(path, HeifReadOptions{}, out, error);
}

cpipe_status_t read_heif_sdr(const std::filesystem::path& path, const HeifReadOptions& options,
                             HeifInfo* out, std::string* error) {
    if (out == nullptr) {
        set_error(error, "output HeifInfo pointer is null");
        return CPIPE_BAD_INDEX;
    }
    *out = HeifInfo{};

    std::unique_ptr<heif_context, decltype(&heif_context_free)> context{heif_context_alloc(),
                                                                        &heif_context_free};
    if (!context) {
        set_error(error, "failed to allocate HEIF context");
        return CPIPE_FAILED;
    }
    if (!check_heif(heif_context_read_from_file(context.get(), path.string().c_str(), nullptr),
                    "heif_context_read_from_file", error)) {
        return CPIPE_FAILED;
    }

    heif_image_handle* raw_handle = nullptr;
    if (!check_heif(heif_context_get_primary_image_handle(context.get(), &raw_handle),
                    "heif_context_get_primary_image_handle", error)) {
        return CPIPE_FAILED;
    }
    std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> handle{
        raw_handle, &heif_image_handle_release};

    const auto width = heif_image_handle_get_width(handle.get());
    const auto height = heif_image_handle_get_height(handle.get());
    if (width <= 0 || height <= 0) {
        set_error(error, "invalid HEIF dimensions");
        return CPIPE_FAILED;
    }
    out->width = static_cast<std::uint32_t>(width);
    out->height = static_cast<std::uint32_t>(height);
    out->luma_bits_per_pixel = heif_image_handle_get_luma_bits_per_pixel(handle.get());

    out->icc_profile_bytes = heif_image_handle_get_raw_color_profile_size(handle.get());
    if (out->icc_profile_bytes > 0) {
        std::vector<std::uint8_t> icc(out->icc_profile_bytes);
        if (!check_heif(heif_image_handle_get_raw_color_profile(handle.get(), icc.data()),
                        "heif_image_handle_get_raw_color_profile", error)) {
            return CPIPE_FAILED;
        }
    }

    heif_color_profile_nclx* raw_nclx = nullptr;
    if (!check_heif(heif_image_handle_get_nclx_color_profile(handle.get(), &raw_nclx),
                    "heif_image_handle_get_nclx_color_profile", error)) {
        return CPIPE_FAILED;
    }
    std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> nclx{
        raw_nclx, &heif_nclx_color_profile_free};
    out->nclx_color_primaries = static_cast<int>(nclx->color_primaries);
    out->nclx_transfer_characteristics = static_cast<int>(nclx->transfer_characteristics);
    out->nclx_matrix_coefficients = static_cast<int>(nclx->matrix_coefficients);

    heif_image* raw_image = nullptr;
    if (!check_heif(heif_decode_image(handle.get(), &raw_image, heif_colorspace_RGB,
                                      heif_chroma_interleaved_RGBA, nullptr),
                    "heif_decode_image", error)) {
        return CPIPE_FAILED;
    }
    std::unique_ptr<heif_image, decltype(&heif_image_release)> image{raw_image,
                                                                     &heif_image_release};

    std::size_t stride = 0;
    const auto* plane =
        heif_image_get_plane_readonly2(image.get(), heif_channel_interleaved, &stride);
    if (plane == nullptr) {
        set_error(error, "failed to access decoded RGBA plane");
        return CPIPE_FAILED;
    }
    const auto row_bytes = static_cast<std::size_t>(width) * 4U;
    out->decoded_rgba.resize(row_bytes * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        std::memcpy(out->decoded_rgba.data() + (static_cast<std::size_t>(y) * row_bytes),
                    plane + (static_cast<std::size_t>(y) * stride), row_bytes);
    }
    if (!options.ocio_config_path.empty() &&
        !fill_scene_linear_rec2020(out->decoded_rgba, out->width, out->height,
                                   options.ocio_config_path, &out->scene_linear_rec2020_rgba,
                                   error)) {
        return CPIPE_FAILED;
    }
    return CPIPE_OK;
}

}  // namespace cpipe::color
