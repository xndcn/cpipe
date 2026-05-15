// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <lcms2.h>
#include <libheif/heif.h>

#include <algorithm>
#include <cmath>
#include <cpipe/color/HeifWriter.hpp>
#include <cpipe/color/IccV4_4Writer.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cpipe::color {
namespace {

struct Ycbcr4208 {
    std::vector<std::uint8_t> y;
    std::vector<std::uint8_t> cb;
    std::vector<std::uint8_t> cr;
    std::uint32_t chroma_width{0};
    std::uint32_t chroma_height{0};
};

struct Ycbcr42010 {
    std::vector<std::uint16_t> y;
    std::vector<std::uint16_t> cb;
    std::vector<std::uint16_t> cr;
    std::uint32_t chroma_width{0};
    std::uint32_t chroma_height{0};
};

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

std::uint8_t quantize_unorm8(float value) {
    const auto clamped = std::clamp(value, 0.0F, 1.0F);
    const auto rounded = static_cast<int>(std::lround(clamped * 255.0F));
    return static_cast<std::uint8_t>(std::clamp(rounded, 0, 255));
}

std::uint16_t quantize_unorm10(float value) {
    const auto clamped = std::clamp(value, 0.0F, 1.0F);
    const auto rounded = static_cast<int>(std::lround(clamped * 1023.0F));
    return static_cast<std::uint16_t>(std::clamp(rounded, 0, 1023));
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

Ycbcr4208 rgb_to_ycbcr420_sdr(const Rgba8ImageView& image) {
    Ycbcr4208 out{};
    out.chroma_width = (image.width + 1U) / 2U;
    out.chroma_height = (image.height + 1U) / 2U;
    out.y.resize(static_cast<std::size_t>(image.width) * image.height);
    out.cb.resize(static_cast<std::size_t>(out.chroma_width) * out.chroma_height);
    out.cr.resize(out.cb.size());

    std::vector<float> cb_accum(out.cb.size(), 0.0F);
    std::vector<float> cr_accum(out.cr.size(), 0.0F);
    std::vector<std::uint8_t> counts(out.cb.size(), 0);

    for (std::uint32_t y = 0; y < image.height; ++y) {
        const auto* row = image.pixels + (static_cast<std::size_t>(y) * image.stride_pixels * 4U);
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto in_base = static_cast<std::size_t>(x) * 4U;
            const auto r = static_cast<float>(row[in_base + 0U]) / 255.0F;
            const auto g = static_cast<float>(row[in_base + 1U]) / 255.0F;
            const auto b = static_cast<float>(row[in_base + 2U]) / 255.0F;
            const auto luma = (0.2126F * r) + (0.7152F * g) + (0.0722F * b);
            const auto index = static_cast<std::size_t>(y) * image.width + x;
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

Ycbcr42010 rgb_to_ycbcr420_hdr(const Rgba16ImageView& image) {
    Ycbcr42010 out{};
    out.chroma_width = (image.width + 1U) / 2U;
    out.chroma_height = (image.height + 1U) / 2U;
    out.y.resize(static_cast<std::size_t>(image.width) * image.height);
    out.cb.resize(static_cast<std::size_t>(out.chroma_width) * out.chroma_height);
    out.cr.resize(out.cb.size());

    std::vector<float> cb_accum(out.cb.size(), 0.0F);
    std::vector<float> cr_accum(out.cr.size(), 0.0F);
    std::vector<std::uint8_t> counts(out.cb.size(), 0);

    for (std::uint32_t y = 0; y < image.height; ++y) {
        const auto* row = image.pixels + (static_cast<std::size_t>(y) * image.stride_pixels * 4U);
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto in_base = static_cast<std::size_t>(x) * 4U;
            const auto r = static_cast<float>(row[in_base + 0U] >> 6U) / 1023.0F;
            const auto g = static_cast<float>(row[in_base + 1U] >> 6U) / 1023.0F;
            const auto b = static_cast<float>(row[in_base + 2U] >> 6U) / 1023.0F;
            const auto luma = (0.2627F * r) + (0.6780F * g) + (0.0593F * b);
            const auto index = static_cast<std::size_t>(y) * image.width + x;
            out.y[index] = quantize_unorm10(luma);

            const auto chroma_index =
                static_cast<std::size_t>(y / 2U) * out.chroma_width + (x / 2U);
            cb_accum[chroma_index] += ((b - luma) / 1.8814F) + 0.5F;
            cr_accum[chroma_index] += ((r - luma) / 1.4746F) + 0.5F;
            ++counts[chroma_index];
        }
    }

    for (std::size_t i = 0; i < out.cb.size(); ++i) {
        const auto divisor = static_cast<float>(counts[i]);
        out.cb[i] = quantize_unorm10(cb_accum[i] / divisor);
        out.cr[i] = quantize_unorm10(cr_accum[i] / divisor);
    }
    return out;
}

bool copy_plane8(heif_image* image, heif_channel channel, const std::vector<std::uint8_t>& src,
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

bool copy_plane10(heif_image* image, heif_channel channel, const std::vector<std::uint16_t>& src,
                  std::uint32_t width, std::uint32_t height, std::string* error) {
    std::size_t stride = 0;
    auto* dst = heif_image_get_plane2(image, channel, &stride);
    if (dst == nullptr) {
        set_error(error, "failed to access HEIF image plane");
        return false;
    }
    const auto row_bytes = static_cast<std::size_t>(width) * sizeof(std::uint16_t);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(dst + (static_cast<std::size_t>(y) * stride),
                    src.data() + (static_cast<std::size_t>(y) * width), row_bytes);
    }
    return true;
}

bool add_ycbcr_planes(heif_image* image, const Ycbcr4208& ycbcr, std::uint32_t width,
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
    return copy_plane8(image, heif_channel_Y, ycbcr.y, width, height, error) &&
           copy_plane8(image, heif_channel_Cb, ycbcr.cb, ycbcr.chroma_width, ycbcr.chroma_height,
                       error) &&
           copy_plane8(image, heif_channel_Cr, ycbcr.cr, ycbcr.chroma_width, ycbcr.chroma_height,
                       error);
}

bool add_ycbcr_planes(heif_image* image, const Ycbcr42010& ycbcr, std::uint32_t width,
                      std::uint32_t height, std::string* error) {
    if (!check_heif(heif_image_add_plane(image, heif_channel_Y, static_cast<int>(width),
                                         static_cast<int>(height), 10),
                    "heif_image_add_plane(Y)", error)) {
        return false;
    }
    if (!check_heif(
            heif_image_add_plane(image, heif_channel_Cb, static_cast<int>(ycbcr.chroma_width),
                                 static_cast<int>(ycbcr.chroma_height), 10),
            "heif_image_add_plane(Cb)", error)) {
        return false;
    }
    if (!check_heif(
            heif_image_add_plane(image, heif_channel_Cr, static_cast<int>(ycbcr.chroma_width),
                                 static_cast<int>(ycbcr.chroma_height), 10),
            "heif_image_add_plane(Cr)", error)) {
        return false;
    }
    return copy_plane10(image, heif_channel_Y, ycbcr.y, width, height, error) &&
           copy_plane10(image, heif_channel_Cb, ycbcr.cb, ycbcr.chroma_width, ycbcr.chroma_height,
                        error) &&
           copy_plane10(image, heif_channel_Cr, ycbcr.cr, ycbcr.chroma_width, ycbcr.chroma_height,
                        error);
}

bool attach_icc_profile(heif_image* image, const std::vector<std::uint8_t>& icc,
                        std::string* error) {
    if (icc.empty()) {
        set_error(error, "empty ICC profile");
        return false;
    }
    return check_heif(heif_image_set_raw_color_profile(image, "prof", icc.data(), icc.size()),
                      "heif_image_set_raw_color_profile", error);
}

std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> create_nclx(
    heif_color_primaries primaries, heif_transfer_characteristics transfer,
    heif_matrix_coefficients matrix, std::string* error) {
    std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> nclx{
        heif_nclx_color_profile_alloc(), &heif_nclx_color_profile_free};
    if (!nclx) {
        set_error(error, "failed to allocate NCLX profile");
        return nclx;
    }
    nclx->color_primaries = primaries;
    nclx->transfer_characteristics = transfer;
    nclx->matrix_coefficients = matrix;
    nclx->full_range_flag = 1;
    return nclx;
}

std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)> make_encoder(
    heif_context* context, heif_compression_format format, const char* name, std::string* error) {
    const heif_encoder_descriptor* descriptors[4]{};
    const auto descriptor_count = heif_get_encoder_descriptors(format, name, descriptors, 4);
    if (descriptor_count <= 0 || descriptors[0] == nullptr) {
        set_error(error, "requested HEIF encoder is unavailable");
        return {nullptr, &heif_encoder_release};
    }

    heif_encoder* raw_encoder = nullptr;
    if (!check_heif(heif_context_get_encoder(context, descriptors[0], &raw_encoder),
                    "heif_context_get_encoder", error)) {
        return {nullptr, &heif_encoder_release};
    }
    return {raw_encoder, &heif_encoder_release};
}

std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)> kvazaar_encoder(
    heif_context* context, int quality, std::string* error) {
    auto encoder = make_encoder(context, heif_compression_HEVC, "kvazaar", error);
    if (!encoder) {
        return encoder;
    }
    if (!check_heif(heif_encoder_set_lossy_quality(encoder.get(), quality),
                    "heif_encoder_set_lossy_quality", error)) {
        return {nullptr, &heif_encoder_release};
    }
    return encoder;
}

std::unique_ptr<heif_encoding_options, decltype(&heif_encoding_options_free)> encode_options(
    heif_color_profile_nclx* nclx, std::string* error) {
    std::unique_ptr<heif_encoding_options, decltype(&heif_encoding_options_free)> options{
        heif_encoding_options_alloc(), &heif_encoding_options_free};
    if (!options) {
        set_error(error, "failed to allocate HEIF encoding options");
        return options;
    }
    options->save_two_colr_boxes_when_ICC_and_nclx_available = 1;
    options->output_nclx_profile = nclx;
    return options;
}

bool valid_image(const Rgba8ImageView& image) {
    return image.pixels != nullptr && image.width > 0 && image.height > 0 &&
           image.stride_pixels >= image.width &&
           image.width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
           image.height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());
}

bool valid_image(const Rgba16ImageView& image) {
    return image.pixels != nullptr && image.width > 0 && image.height > 0 &&
           image.stride_pixels >= image.width &&
           image.width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
           image.height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());
}

float pq_eotf_nits(float code) {
    constexpr float m1 = 2610.0F / 16384.0F;
    constexpr float m2 = 2523.0F / 32.0F;
    constexpr float c1 = 3424.0F / 4096.0F;
    constexpr float c2 = 2413.0F / 128.0F;
    constexpr float c3 = 2392.0F / 128.0F;
    const auto normalized = std::clamp(code, 0.0F, 1.0F);
    const auto p = std::pow(normalized, 1.0F / m2);
    const auto numerator = std::max(p - c1, 0.0F);
    const auto denominator = c2 - (c3 * p);
    if (denominator <= 0.0F) {
        return 10000.0F;
    }
    return 10000.0F * std::pow(numerator / denominator, 1.0F / m1);
}

heif_content_light_level compute_clli(const Rgba16ImageView& image) {
    double fall_sum = 0.0;
    float max_cll = 0.0F;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const auto* row = image.pixels + (static_cast<std::size_t>(y) * image.stride_pixels * 4U);
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto base = static_cast<std::size_t>(x) * 4U;
            const auto r = pq_eotf_nits(static_cast<float>(row[base + 0U] >> 6U) / 1023.0F);
            const auto g = pq_eotf_nits(static_cast<float>(row[base + 1U] >> 6U) / 1023.0F);
            const auto b = pq_eotf_nits(static_cast<float>(row[base + 2U] >> 6U) / 1023.0F);
            max_cll = std::max(max_cll, std::max({r, g, b}));
            fall_sum += (0.2627F * r) + (0.6780F * g) + (0.0593F * b);
        }
    }

    const auto pixel_count = static_cast<double>(image.width) * static_cast<double>(image.height);
    const auto max_fall = pixel_count > 0.0 ? fall_sum / pixel_count : 0.0;
    return heif_content_light_level{
        .max_content_light_level = static_cast<std::uint16_t>(
            std::clamp<int>(static_cast<int>(std::lround(max_cll)), 1, 65535)),
        .max_pic_average_light_level = static_cast<std::uint16_t>(
            std::clamp<int>(static_cast<int>(std::lround(max_fall)), 1, 65535)),
    };
}

heif_mastering_display_colour_volume default_mdcv() {
    return heif_mastering_display_colour_volume{
        .display_primaries_x = {35400, 8500, 6550},
        .display_primaries_y = {14600, 39850, 2300},
        .white_point_x = 15635,
        .white_point_y = 16450,
        .max_display_mastering_luminance = 1000,
        .min_display_mastering_luminance = 50,
    };
}

}  // namespace

cpipe_status_t write_heif_sdr(const std::filesystem::path& path, const Rgba8ImageView& image,
                              const HeifWriteOptions& options, std::string* error) {
    if (!valid_image(image)) {
        set_error(error, "invalid HEIF input image");
        return CPIPE_BAD_INDEX;
    }

    const auto ycbcr = rgb_to_ycbcr420_sdr(image);
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

    auto nclx =
        create_nclx(heif_color_primaries_ITU_R_BT_709_5, heif_transfer_characteristic_IEC_61966_2_1,
                    heif_matrix_coefficients_ITU_R_BT_709_5, error);
    const auto icc = create_srgb_v4_icc(error);
    if (!add_ycbcr_planes(heif_image.get(), ycbcr, image.width, image.height, error) ||
        !attach_icc_profile(heif_image.get(), icc, error) || !nclx ||
        !check_heif(heif_image_set_nclx_color_profile(heif_image.get(), nclx.get()),
                    "heif_image_set_nclx_color_profile", error)) {
        return CPIPE_FAILED;
    }

    (void)options;
    auto encoder = make_encoder(context.get(), heif_compression_uncompressed, nullptr, error);
    auto encoding_options = encode_options(nclx.get(), error);
    if (!encoder || !encoding_options) {
        return CPIPE_FAILED;
    }
    if (!check_heif(heif_context_encode_image(context.get(), heif_image.get(), encoder.get(),
                                              encoding_options.get(), nullptr),
                    "heif_context_encode_image", error)) {
        return CPIPE_FAILED;
    }
    if (!check_heif(heif_context_write_to_file(context.get(), path.string().c_str()),
                    "heif_context_write_to_file", error)) {
        return CPIPE_FAILED;
    }
    return CPIPE_OK;
}

cpipe_status_t write_heif_hdr_pq(const std::filesystem::path& path, const Rgba16ImageView& image,
                                 const HeifWriteOptions& options, std::string* error) {
    if (!valid_image(image)) {
        set_error(error, "invalid HEIF input image");
        return CPIPE_BAD_INDEX;
    }

    const auto ycbcr = rgb_to_ycbcr420_hdr(image);
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

    auto nclx = create_nclx(heif_color_primaries_ITU_R_BT_2020_2_and_2100_0,
                            heif_transfer_characteristic_ITU_R_BT_2100_0_PQ,
                            heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance, error);
    const auto icc = IccV4_4Writer::bt2020_pq();
    auto mdcv = default_mdcv();
    auto clli = compute_clli(image);
    heif_image_set_mastering_display_colour_volume(heif_image.get(), &mdcv);
    heif_image_set_content_light_level(heif_image.get(), &clli);

    if (!add_ycbcr_planes(heif_image.get(), ycbcr, image.width, image.height, error) ||
        !attach_icc_profile(heif_image.get(), icc, error) || !nclx ||
        !check_heif(heif_image_set_nclx_color_profile(heif_image.get(), nclx.get()),
                    "heif_image_set_nclx_color_profile", error)) {
        return CPIPE_FAILED;
    }

    auto encoder = kvazaar_encoder(context.get(), options.quality, error);
    auto encoding_options = encode_options(nclx.get(), error);
    if (!encoder || !encoding_options) {
        return CPIPE_FAILED;
    }
    (void)heif_encoder_set_parameter_string(encoder.get(), "preset", "medium");
    (void)heif_encoder_set_parameter_integer(encoder.get(), "bit-depth", 10);

    heif_image_handle* raw_handle = nullptr;
    if (!check_heif(heif_context_encode_image(context.get(), heif_image.get(), encoder.get(),
                                              encoding_options.get(), &raw_handle),
                    "heif_context_encode_image", error)) {
        return CPIPE_FAILED;
    }
    std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> handle{
        raw_handle, &heif_image_handle_release};
    heif_image_handle_set_mastering_display_colour_volume(handle.get(), &mdcv);
    heif_image_handle_set_content_light_level(handle.get(), &clli);

    if (!check_heif(heif_context_write_to_file(context.get(), path.string().c_str()),
                    "heif_context_write_to_file", error)) {
        return CPIPE_FAILED;
    }
    return CPIPE_OK;
}

}  // namespace cpipe::color
