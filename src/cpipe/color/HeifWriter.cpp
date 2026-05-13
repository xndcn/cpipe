// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenColorIO/OpenColorIO.h>
#include <lcms2.h>
#include <libheif/heif.h>

#include <algorithm>
#include <cpipe/color/HeifWriter.hpp>
#include <cstddef>
#include <cstring>
#include <limits>
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

float half_to_float(std::uint16_t bits) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    _Float16 half = 0.0F;
    std::memcpy(&half, &bits, sizeof(bits));
    return static_cast<float>(half);
}

std::uint8_t quantize_unorm8(float value) {
    const auto clamped = std::clamp(value, 0.0F, 1.0F);
    const auto rounded = static_cast<int>((clamped * 255.0F) + 0.5F);
    return static_cast<std::uint8_t>(std::clamp(rounded, 0, 255));
}

std::vector<float> ocio_to_srgb(const Rgba16ImageView& image,
                                const std::filesystem::path& config_path, std::string* error) {
    const auto pixel_count = static_cast<std::size_t>(image.width) * image.height;
    std::vector<float> rgba(pixel_count * 4U);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const auto* row = image.pixels + (static_cast<std::size_t>(y) * image.stride_pixels * 4U);
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto in_base = static_cast<std::size_t>(x) * 4U;
            const auto out_base =
                ((static_cast<std::size_t>(y) * image.width) + static_cast<std::size_t>(x)) * 4U;
            rgba[out_base + 0U] = half_to_float(row[in_base + 0U]);
            rgba[out_base + 1U] = half_to_float(row[in_base + 1U]);
            rgba[out_base + 2U] = half_to_float(row[in_base + 2U]);
            rgba[out_base + 3U] = 1.0F;
        }
    }

    try {
        const auto config = OCIO::Config::CreateFromFile(config_path.string().c_str());
        const auto processor =
            config->getProcessor("scene_linear_rec2020", "output_srgb")->getDefaultCPUProcessor();
        OCIO::PackedImageDesc desc{rgba.data(), static_cast<long>(image.width),
                                   static_cast<long>(image.height), 4L};
        processor->apply(desc);
    } catch (const std::exception& e) {
        set_error(error, std::string{"OCIO transform failed: "} + e.what());
        return {};
    }

    return rgba;
}

std::vector<std::uint8_t> create_srgb_v4_icc(std::string* error) {
    cmsHPROFILE raw_profile = cmsCreate_sRGBProfile();
    if (raw_profile == nullptr) {
        set_error(error, "failed to create sRGB ICC profile");
        return {};
    }
    std::unique_ptr<void, decltype(&cmsCloseProfile)> profile{raw_profile, &cmsCloseProfile};
    cmsSetProfileVersion(profile.get(), 4.4);

    cmsUInt32Number size = 0;
    if (cmsSaveProfileToMem(profile.get(), nullptr, &size) == 0 || size == 0) {
        set_error(error, "failed to size sRGB ICC profile");
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (cmsSaveProfileToMem(profile.get(), bytes.data(), &size) == 0) {
        set_error(error, "failed to serialize sRGB ICC profile");
        return {};
    }
    bytes.resize(static_cast<std::size_t>(size));
    return bytes;
}

struct Ycbcr420 {
    std::vector<std::uint8_t> y;
    std::vector<std::uint8_t> cb;
    std::vector<std::uint8_t> cr;
    std::uint32_t chroma_width{0};
    std::uint32_t chroma_height{0};
};

Ycbcr420 rgb_to_ycbcr420(const std::vector<float>& rgba, std::uint32_t width,
                         std::uint32_t height) {
    Ycbcr420 out{};
    out.chroma_width = (width + 1U) / 2U;
    out.chroma_height = (height + 1U) / 2U;
    out.y.resize(static_cast<std::size_t>(width) * height);
    out.cb.resize(static_cast<std::size_t>(out.chroma_width) * out.chroma_height);
    out.cr.resize(out.cb.size());

    std::vector<float> cb_accum(out.cb.size(), 0.0F);
    std::vector<float> cr_accum(out.cr.size(), 0.0F);
    std::vector<std::uint8_t> counts(out.cb.size(), 0);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y) * width + x;
            const auto base = index * 4U;
            const auto r = std::clamp(rgba[base + 0U], 0.0F, 1.0F);
            const auto g = std::clamp(rgba[base + 1U], 0.0F, 1.0F);
            const auto b = std::clamp(rgba[base + 2U], 0.0F, 1.0F);
            const auto luma = (0.2126F * r) + (0.7152F * g) + (0.0722F * b);
            out.y[index] = quantize_unorm8(luma);

            const auto chroma_index =
                static_cast<std::size_t>(y / 2U) * out.chroma_width + (x / 2U);
            cb_accum[chroma_index] += ((b - luma) / 1.8556F) + 0.5F;
            cr_accum[chroma_index] += ((r - luma) / 1.5748F) + 0.5F;
            ++counts[chroma_index];
        }
    }

    for (std::size_t i = 0; i < out.cb.size(); ++i) {
        const auto divisor = static_cast<float>(counts[i]);
        out.cb[i] = quantize_unorm8(cb_accum[i] / divisor);
        out.cr[i] = quantize_unorm8(cr_accum[i] / divisor);
    }
    return out;
}

bool copy_plane(heif_image* image, heif_channel channel, const std::vector<std::uint8_t>& src,
                std::uint32_t width, std::uint32_t height, std::string* error) {
    std::size_t stride = 0;
    auto* dst = heif_image_get_plane2(image, channel, &stride);
    if (dst == nullptr) {
        set_error(error, "failed to access HEIF image plane");
        return false;
    }
    const auto row_bytes = static_cast<std::size_t>(width);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(dst + (static_cast<std::size_t>(y) * stride),
                    src.data() + (static_cast<std::size_t>(y) * row_bytes), row_bytes);
    }
    return true;
}

bool add_ycbcr_planes(heif_image* image, const Ycbcr420& ycbcr, std::uint32_t width,
                      std::uint32_t height, std::string* error) {
    if (!check_heif(heif_image_add_plane(image, heif_channel_Y, static_cast<int>(width),
                                         static_cast<int>(height), 8),
                    "heif_image_add_plane(Y)", error)) {
        return false;
    }
    if (!check_heif(
            heif_image_add_plane(image, heif_channel_Cb, static_cast<int>(ycbcr.chroma_width),
                                 static_cast<int>(ycbcr.chroma_height), 8),
            "heif_image_add_plane(Cb)", error)) {
        return false;
    }
    if (!check_heif(
            heif_image_add_plane(image, heif_channel_Cr, static_cast<int>(ycbcr.chroma_width),
                                 static_cast<int>(ycbcr.chroma_height), 8),
            "heif_image_add_plane(Cr)", error)) {
        return false;
    }
    return copy_plane(image, heif_channel_Y, ycbcr.y, width, height, error) &&
           copy_plane(image, heif_channel_Cb, ycbcr.cb, ycbcr.chroma_width, ycbcr.chroma_height,
                      error) &&
           copy_plane(image, heif_channel_Cr, ycbcr.cr, ycbcr.chroma_width, ycbcr.chroma_height,
                      error);
}

bool attach_icc_profile(heif_image* image, std::string* error) {
    const auto icc = create_srgb_v4_icc(error);
    if (icc.empty()) {
        return false;
    }
    return check_heif(heif_image_set_raw_color_profile(image, "prof", icc.data(), icc.size()),
                      "heif_image_set_raw_color_profile", error);
}

std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> create_srgb_nclx(
    std::string* error) {
    std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> nclx{
        heif_nclx_color_profile_alloc(), &heif_nclx_color_profile_free};
    if (!nclx) {
        set_error(error, "failed to allocate NCLX profile");
        return nclx;
    }
    nclx->color_primaries = heif_color_primaries_ITU_R_BT_709_5;
    nclx->transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;
    nclx->matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_709_5;
    nclx->full_range_flag = 1;
    return nclx;
}

}  // namespace

cpipe_status_t write_heif_sdr(const std::filesystem::path& path, const Rgba16ImageView& image,
                              const HeifWriteOptions& options, std::string* error) {
    if (image.pixels == nullptr || image.width == 0 || image.height == 0 ||
        image.stride_pixels < image.width ||
        image.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        image.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        set_error(error, "invalid HEIF input image");
        return CPIPE_BAD_INDEX;
    }
    if (options.ocio_config_path.empty()) {
        set_error(error, "missing OCIO config path");
        return CPIPE_NEED_PARAM;
    }

    const auto srgb = ocio_to_srgb(image, options.ocio_config_path, error);
    if (srgb.empty()) {
        return CPIPE_FAILED;
    }
    const auto ycbcr = rgb_to_ycbcr420(srgb, image.width, image.height);

    std::unique_ptr<heif_context, decltype(&heif_context_free)> context{heif_context_alloc(),
                                                                        &heif_context_free};
    if (!context) {
        set_error(error, "failed to allocate HEIF context");
        return CPIPE_FAILED;
    }

    heif_image* raw_image = nullptr;
    if (!check_heif(heif_image_create(static_cast<int>(image.width), static_cast<int>(image.height),
                                      heif_colorspace_YCbCr, heif_chroma_420, &raw_image),
                    "heif_image_create", error)) {
        return CPIPE_FAILED;
    }
    std::unique_ptr<heif_image, decltype(&heif_image_release)> heif_image{raw_image,
                                                                          &heif_image_release};

    if (!add_ycbcr_planes(heif_image.get(), ycbcr, image.width, image.height, error) ||
        !attach_icc_profile(heif_image.get(), error)) {
        return CPIPE_FAILED;
    }
    auto nclx = create_srgb_nclx(error);
    if (!nclx || !check_heif(heif_image_set_nclx_color_profile(heif_image.get(), nclx.get()),
                             "heif_image_set_nclx_color_profile", error)) {
        return CPIPE_FAILED;
    }

    const heif_encoder_descriptor* descriptors[4]{};
    const auto descriptor_count =
        heif_get_encoder_descriptors(heif_compression_HEVC, "kvazaar", descriptors, 4);
    if (descriptor_count <= 0 || descriptors[0] == nullptr) {
        set_error(error, "kvazaar HEVC encoder is unavailable");
        return CPIPE_UNSUPPORTED;
    }

    heif_encoder* raw_encoder = nullptr;
    if (!check_heif(heif_context_get_encoder(context.get(), descriptors[0], &raw_encoder),
                    "heif_context_get_encoder(kvazaar)", error)) {
        return CPIPE_FAILED;
    }
    std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)> encoder{raw_encoder,
                                                                           &heif_encoder_release};
    if (!check_heif(heif_encoder_set_lossy_quality(encoder.get(), options.quality),
                    "heif_encoder_set_lossy_quality", error)) {
        return CPIPE_FAILED;
    }
    std::unique_ptr<heif_encoding_options, decltype(&heif_encoding_options_free)> encode_options{
        heif_encoding_options_alloc(), &heif_encoding_options_free};
    if (!encode_options) {
        set_error(error, "failed to allocate HEIF encoding options");
        return CPIPE_FAILED;
    }
    encode_options->save_two_colr_boxes_when_ICC_and_nclx_available = 1;
    encode_options->output_nclx_profile = nclx.get();

    if (!check_heif(heif_context_encode_image(context.get(), heif_image.get(), encoder.get(),
                                              encode_options.get(), nullptr),
                    "heif_context_encode_image", error)) {
        return CPIPE_FAILED;
    }
    if (!check_heif(heif_context_write_to_file(context.get(), path.string().c_str()),
                    "heif_context_write_to_file", error)) {
        return CPIPE_FAILED;
    }
    return CPIPE_OK;
}

}  // namespace cpipe::color
