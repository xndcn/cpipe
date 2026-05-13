// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <libraw/libraw.h>

#include <algorithm>
#include <array>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/ingest/dng/DngReader.hpp>
#include <cpipe/ingest/dng_opcode/OpcodeList.hpp>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cpipe::ingest::dng {
namespace {

struct DecodedRaw {
    std::vector<std::byte> bytes;
    dng_opcode::ParsedDngMetadata metadata;
};

std::shared_ptr<const compute::ByteBlob> make_blob(std::vector<std::byte> bytes) {
    auto blob = std::make_shared<compute::ByteBlob>();
    blob->bytes = std::move(bytes);
    return blob;
}

std::vector<std::byte> read_all(std::ifstream& input) {
    const std::vector<char> chars{std::istreambuf_iterator<char>{input},
                                  std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(chars.size());
    std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

DngReadResult failed(cpipe_status_t status, std::string message) {
    return DngReadResult{.status = status, .buffer = nullptr, .message = std::move(message)};
}

std::vector<std::byte> copy_opcode_blob(const libraw_dng_rawopcode_t& opcode) {
    if (opcode.data == nullptr || opcode.len == 0) {
        return {};
    }
    const auto* first = static_cast<const std::byte*>(opcode.data);
    return {first, first + opcode.len};
}

void apply_libraw_metadata(const LibRaw& processor, dng_opcode::ParsedDngMetadata* metadata) {
    if (metadata == nullptr) {
        return;
    }

    const auto& image = processor.imgdata;
    if (metadata->make.empty()) {
        metadata->make = image.idata.make;
        metadata->capture.camera_id = metadata->make;
    }
    if (metadata->model.empty()) {
        metadata->model = image.idata.model;
        metadata->capture.physical_camera_id = metadata->model;
    }
    if (metadata->capture.iso == 0 && image.other.iso_speed > 0.0F) {
        metadata->capture.iso = static_cast<std::int32_t>(image.other.iso_speed);
    }
    if (metadata->capture.exposure_time_ns == 0 && image.other.shutter > 0.0F) {
        metadata->capture.exposure_time_ns =
            static_cast<std::int64_t>(image.other.shutter * 1'000'000'000.0F);
    }
    if (metadata->capture.sensor_timestamp_ns == 0 && image.other.timestamp > 0) {
        metadata->capture.sensor_timestamp_ns =
            static_cast<std::int64_t>(image.other.timestamp) * 1'000'000'000LL;
    }
    if (metadata->capture.lens_aperture == 0.0F && image.other.aperture > 0.0F) {
        metadata->capture.lens_aperture = image.other.aperture;
    }
    if (metadata->capture.lens_focal_length_mm == 0.0F && image.other.focal_len > 0.0F) {
        metadata->capture.lens_focal_length_mm = image.other.focal_len;
    }
    if (metadata->calibration.white_level == 0 && image.color.maximum > 0) {
        metadata->calibration.white_level = image.color.maximum;
    }
    if (metadata->calibration.black_level == std::array<float, 4>{} && image.color.black > 0) {
        metadata->calibration.black_level.fill(static_cast<float>(image.color.black));
    }
    if (metadata->xmp_blob.empty() && image.idata.xmpdata != nullptr && image.idata.xmplen > 0) {
        const auto* first = reinterpret_cast<const std::byte*>(image.idata.xmpdata);
        metadata->xmp_blob.assign(first, first + image.idata.xmplen);
    }
    if (metadata->icc_blob.empty() && image.color.profile != nullptr &&
        image.color.profile_length > 0) {
        const auto* first = static_cast<const std::byte*>(image.color.profile);
        metadata->icc_blob.assign(first, first + image.color.profile_length);
    }
    if (metadata->opcode_list_1.empty()) {
        metadata->opcode_list_1 = copy_opcode_blob(image.color.dng_levels.rawopcodes[0]);
    }
    if (metadata->opcode_list_2.empty()) {
        metadata->opcode_list_2 = copy_opcode_blob(image.color.dng_levels.rawopcodes[1]);
    }
    if (metadata->opcode_list_3.empty()) {
        metadata->opcode_list_3 = copy_opcode_blob(image.color.dng_levels.rawopcodes[2]);
    }
}

std::optional<DecodedRaw> decode_with_libraw(const std::filesystem::path& path,
                                             dng_opcode::ParsedDngMetadata metadata) {
    LibRaw processor;
    const auto path_string = path.string();
    if (processor.open_file(path_string.c_str()) != LIBRAW_SUCCESS) {
        return std::nullopt;
    }
    if (processor.unpack() != LIBRAW_SUCCESS || processor.imgdata.rawdata.raw_image == nullptr) {
        return std::nullopt;
    }

    apply_libraw_metadata(processor, &metadata);

    const auto width = metadata.width;
    const auto height = metadata.height;
    const auto raw_width = processor.imgdata.sizes.raw_width;
    const auto raw_height = processor.imgdata.sizes.raw_height;
    if (width == 0 || height == 0 || raw_width < width || raw_height < height) {
        return std::nullopt;
    }

    std::vector<std::byte> raw(static_cast<std::size_t>(width) * height * sizeof(std::uint16_t));
    auto* output = reinterpret_cast<std::uint16_t*>(raw.data());
    const auto* input = processor.imgdata.rawdata.raw_image;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::copy_n(input + static_cast<std::size_t>(y) * raw_width, width,
                    output + static_cast<std::size_t>(y) * width);
    }

    return DecodedRaw{.bytes = std::move(raw), .metadata = std::move(metadata)};
}

std::optional<DecodedRaw> decode_single_strip(const std::filesystem::path& path,
                                              dng_opcode::ParsedDngMetadata metadata) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return std::nullopt;
    }
    const auto bytes = read_all(input);
    if (metadata.strip_offset > bytes.size() ||
        metadata.strip_byte_count > bytes.size() - metadata.strip_offset) {
        return std::nullopt;
    }

    const auto expected =
        static_cast<std::uint64_t>(metadata.width) * metadata.height * sizeof(std::uint16_t);
    std::vector<std::byte> raw(static_cast<std::size_t>(expected));
    const auto copy_bytes = std::min<std::uint64_t>(expected, metadata.strip_byte_count);
    std::memcpy(raw.data(), bytes.data() + metadata.strip_offset,
                static_cast<std::size_t>(copy_bytes));
    return DecodedRaw{.bytes = std::move(raw), .metadata = std::move(metadata)};
}

std::shared_ptr<const compute::BufferMetadata> make_metadata(
    const dng_opcode::ParsedDngMetadata& info) {
    auto metadata = std::make_shared<compute::BufferMetadata>();
    metadata->calibration = std::make_shared<compute::CalibrationBlock>(info.calibration);
    metadata->capture = info.capture;
    metadata->active_area = info.active_area;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps.clear();
    if (!info.exif_blob.empty()) {
        metadata->exif_blob = make_blob(info.exif_blob);
    }
    if (!info.xmp_blob.empty()) {
        metadata->xmp_blob = make_blob(info.xmp_blob);
    }
    if (!info.icc_blob.empty()) {
        metadata->icc_blob = make_blob(info.icc_blob);
    }
    if (!info.opcode_list_1.empty()) {
        metadata->ext_blobs["com.cpipe.dng.opcode_list_1_bytes"] = make_blob(info.opcode_list_1);
    }
    if (!info.opcode_list_2.empty()) {
        metadata->ext_blobs["com.cpipe.dng.opcode_list_2_bytes"] = make_blob(info.opcode_list_2);
    }
    if (!info.opcode_list_3.empty()) {
        metadata->ext_blobs["com.cpipe.dng.opcode_list_3_bytes"] = make_blob(info.opcode_list_3);
    }
    return metadata;
}

}  // namespace

DngReadResult DngReader::read(const std::filesystem::path& path) {
    const auto parsed = dng_opcode::OpcodeListParser::parse(path);
    if (parsed.status != CPIPE_OK) {
        const auto status = parsed.status == CPIPE_UNSUPPORTED ? CPIPE_FAILED : parsed.status;
        return failed(status, parsed.message);
    }

    auto decoded = decode_with_libraw(path, parsed.metadata);
    if (!decoded) {
        decoded = decode_single_strip(path, parsed.metadata);
    }
    if (!decoded) {
        return failed(CPIPE_FAILED, "failed to decode DNG RAW16 payload");
    }

    compute::BufferLayout layout{};
    layout.kind = compute::BufferKind::Image2D;
    layout.format = compute::PixelFormat::R16_UINT;
    layout.ndim = 2;
    layout.dims[0] = decoded->metadata.width;
    layout.dims[1] = decoded->metadata.height;

    auto buffer = std::make_shared<compute::CpuBuffer>(layout,
                                                       compute::BufferUsage::Input |
                                                           compute::BufferUsage::CpuRead |
                                                           compute::BufferUsage::CpuWrite,
                                                       "raw_camera");
    auto* output = static_cast<std::byte*>(buffer->lock_cpu(compute::IBuffer::CpuAccess::Write));
    std::memcpy(output, decoded->bytes.data(),
                std::min<std::size_t>(decoded->bytes.size(),
                                      static_cast<std::size_t>(buffer->size_bytes())));
    buffer->unlock_cpu();
    buffer->flush_cpu_writes();
    buffer->set_metadata(make_metadata(decoded->metadata));

    return DngReadResult{.status = CPIPE_OK, .buffer = std::move(buffer), .message = {}};
}

}  // namespace cpipe::ingest::dng
