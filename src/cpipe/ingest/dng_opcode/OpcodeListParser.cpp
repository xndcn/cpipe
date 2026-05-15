// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/ingest/dng_opcode/OpcodeList.hpp>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cpipe::ingest::dng_opcode {
namespace {

constexpr std::uint16_t kTiffMagic = 42;
constexpr std::uint16_t kTypeByte = 1;
constexpr std::uint16_t kTypeAscii = 2;
constexpr std::uint16_t kTypeShort = 3;
constexpr std::uint16_t kTypeLong = 4;
constexpr std::uint16_t kTypeRational = 5;
constexpr std::uint16_t kTypeUndefined = 7;
constexpr std::uint16_t kTypeSRational = 10;

constexpr std::uint16_t kTagXmp = 700;
constexpr std::uint16_t kTagImageWidth = 256;
constexpr std::uint16_t kTagImageLength = 257;
constexpr std::uint16_t kTagBitsPerSample = 258;
constexpr std::uint16_t kTagCompression = 259;
constexpr std::uint16_t kTagPhotometricInterpretation = 262;
constexpr std::uint16_t kTagMake = 271;
constexpr std::uint16_t kTagModel = 272;
constexpr std::uint16_t kTagStripOffsets = 273;
constexpr std::uint16_t kTagOrientation = 274;
constexpr std::uint16_t kTagSamplesPerPixel = 277;
constexpr std::uint16_t kTagStripByteCounts = 279;
constexpr std::uint16_t kTagExifIFD = 34665;
constexpr std::uint16_t kTagIsoSpeedRatings = 34855;
constexpr std::uint16_t kTagIccProfile = 34675;
constexpr std::uint16_t kTagExposureTime = 33434;
constexpr std::uint16_t kTagFNumber = 33437;
constexpr std::uint16_t kTagFocalLength = 37386;
constexpr std::uint16_t kTagCfaRepeatPatternDim = 33421;
constexpr std::uint16_t kTagCfaPattern = 33422;
constexpr std::uint16_t kTagLinearizationTable = 50712;
constexpr std::uint16_t kTagBlackLevel = 50714;
constexpr std::uint16_t kTagWhiteLevel = 50717;
constexpr std::uint16_t kTagColorMatrix1 = 50721;
constexpr std::uint16_t kTagColorMatrix2 = 50722;
constexpr std::uint16_t kTagAsShotNeutral = 50728;
constexpr std::uint16_t kTagCalibrationIlluminant1 = 50778;
constexpr std::uint16_t kTagCalibrationIlluminant2 = 50779;
constexpr std::uint16_t kTagActiveArea = 50829;
constexpr std::uint16_t kTagForwardMatrix1 = 50964;
constexpr std::uint16_t kTagForwardMatrix2 = 50965;
constexpr std::uint16_t kTagOpcodeList1 = 51008;
constexpr std::uint16_t kTagOpcodeList2 = 51009;
constexpr std::uint16_t kTagOpcodeList3 = 51022;

constexpr std::uint32_t kCompressionNone = 1;
constexpr std::uint32_t kPhotometricCfa = 32803;

struct Entry {
    std::uint16_t tag{0};
    std::uint16_t type{0};
    std::uint32_t count{0};
    std::uint32_t value_or_offset{0};
};

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[offset]) |
                                      (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U));
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(read_u16(bytes, offset)) |
           (static_cast<std::uint32_t>(read_u16(bytes, offset + 2U)) << 16U);
}

std::int32_t read_i32(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::int32_t>(read_u32(bytes, offset));
}

std::uint32_t type_size(std::uint16_t type) {
    switch (type) {
        case kTypeByte:
        case kTypeAscii:
        case kTypeUndefined:
            return 1;
        case kTypeShort:
            return 2;
        case kTypeLong:
            return 4;
        case kTypeRational:
        case kTypeSRational:
            return 8;
        default:
            return 0;
    }
}

std::vector<std::byte> entry_data(std::span<const std::byte> bytes, const Entry& entry) {
    const auto item_size = type_size(entry.type);
    const auto byte_count = static_cast<std::uint64_t>(entry.count) * item_size;
    if (item_size == 0 || byte_count == 0 || byte_count > bytes.size()) {
        return {};
    }

    std::vector<std::byte> out(static_cast<std::size_t>(byte_count));
    if (byte_count <= 4) {
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::byte>((entry.value_or_offset >> (8U * i)) & 0xffU);
        }
        return out;
    }

    if (entry.value_or_offset > bytes.size() || byte_count > bytes.size() - entry.value_or_offset) {
        return {};
    }
    std::copy_n(bytes.begin() + entry.value_or_offset, out.size(), out.begin());
    return out;
}

std::optional<Entry> find_entry(std::span<const Entry> entries, std::uint16_t tag) {
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [tag](const Entry& entry) { return entry.tag == tag; });
    if (found == entries.end()) {
        return std::nullopt;
    }
    return *found;
}

std::optional<std::vector<Entry>> read_entries(std::span<const std::byte> bytes,
                                               std::uint32_t ifd_offset) {
    if (ifd_offset + 2U > bytes.size()) {
        return std::nullopt;
    }
    const auto entry_count = read_u16(bytes, ifd_offset);
    const auto entries_offset = ifd_offset + 2U;
    if (entries_offset + static_cast<std::uint64_t>(entry_count) * 12ULL > bytes.size()) {
        return std::nullopt;
    }

    std::vector<Entry> entries;
    entries.reserve(entry_count);
    for (std::uint16_t i = 0; i < entry_count; ++i) {
        const auto offset = entries_offset + i * 12U;
        entries.push_back(Entry{.tag = read_u16(bytes, offset),
                                .type = read_u16(bytes, offset + 2U),
                                .count = read_u32(bytes, offset + 4U),
                                .value_or_offset = read_u32(bytes, offset + 8U)});
    }
    return entries;
}

std::vector<std::byte> copy_ifd_bytes(std::span<const std::byte> bytes, std::uint32_t ifd_offset) {
    if (ifd_offset + 2U > bytes.size()) {
        return {};
    }
    const auto entry_count = read_u16(bytes, ifd_offset);
    const auto byte_count = 2ULL + static_cast<std::uint64_t>(entry_count) * 12ULL + 4ULL;
    if (byte_count > bytes.size() || ifd_offset > bytes.size() - byte_count) {
        return {};
    }
    const auto begin = bytes.begin() + ifd_offset;
    return {begin, begin + static_cast<std::ptrdiff_t>(byte_count)};
}

std::uint32_t first_u32(std::span<const std::byte> bytes, const Entry& entry) {
    const auto data = entry_data(bytes, entry);
    if (data.empty()) {
        return 0;
    }
    if (entry.type == kTypeShort && data.size() >= 2) {
        return read_u16(data, 0);
    }
    if (entry.type == kTypeLong && data.size() >= 4) {
        return read_u32(data, 0);
    }
    if (entry.type == kTypeByte) {
        return std::to_integer<std::uint32_t>(data[0]);
    }
    return 0;
}

std::vector<std::uint32_t> u32_values(std::span<const std::byte> bytes, const Entry& entry) {
    const auto data = entry_data(bytes, entry);
    std::vector<std::uint32_t> out;
    if (data.empty()) {
        return out;
    }

    if (entry.type == kTypeByte && data.size() >= entry.count) {
        out.reserve(entry.count);
        for (std::uint32_t i = 0; i < entry.count; ++i) {
            out.push_back(std::to_integer<std::uint32_t>(data[i]));
        }
        return out;
    }
    if (entry.type == kTypeShort && data.size() >= entry.count * 2ULL) {
        out.reserve(entry.count);
        for (std::uint32_t i = 0; i < entry.count; ++i) {
            out.push_back(read_u16(data, static_cast<std::size_t>(i) * 2U));
        }
        return out;
    }
    if (entry.type == kTypeLong && data.size() >= entry.count * 4ULL) {
        out.reserve(entry.count);
        for (std::uint32_t i = 0; i < entry.count; ++i) {
            out.push_back(read_u32(data, static_cast<std::size_t>(i) * 4U));
        }
    }
    return out;
}

std::vector<std::uint16_t> shorts(std::span<const std::byte> bytes, const Entry& entry) {
    const auto values = u32_values(bytes, entry);
    std::vector<std::uint16_t> out;
    if (entry.type != kTypeShort) {
        return out;
    }
    out.reserve(values.size());
    for (const auto value : values) {
        out.push_back(static_cast<std::uint16_t>(value));
    }
    return out;
}

std::vector<float> numeric_values(std::span<const std::byte> bytes, const Entry& entry) {
    const auto data = entry_data(bytes, entry);
    std::vector<float> out;
    if (data.empty()) {
        return out;
    }

    if (entry.type == kTypeByte || entry.type == kTypeShort || entry.type == kTypeLong) {
        const auto values = u32_values(bytes, entry);
        out.reserve(values.size());
        for (const auto value : values) {
            out.push_back(static_cast<float>(value));
        }
        return out;
    }

    if ((entry.type != kTypeRational && entry.type != kTypeSRational) ||
        data.size() < entry.count * 8ULL) {
        return out;
    }
    out.reserve(entry.count);
    for (std::uint32_t i = 0; i < entry.count; ++i) {
        const auto offset = i * 8U;
        const auto denominator = entry.type == kTypeRational
                                     ? static_cast<float>(read_u32(data, offset + 4U))
                                     : static_cast<float>(read_i32(data, offset + 4U));
        if (denominator == 0.0F) {
            out.push_back(0.0F);
            continue;
        }
        const auto numerator = entry.type == kTypeRational
                                   ? static_cast<float>(read_u32(data, offset))
                                   : static_cast<float>(read_i32(data, offset));
        out.push_back(numerator / denominator);
    }
    return out;
}

std::string ascii(std::span<const std::byte> bytes, const Entry& entry) {
    const auto data = entry_data(bytes, entry);
    if (entry.type != kTypeAscii || data.empty()) {
        return {};
    }
    const auto* chars = reinterpret_cast<const char*>(data.data());
    const auto* end = std::find(chars, chars + data.size(), '\0');
    return {chars, static_cast<std::size_t>(end - chars)};
}

std::vector<std::byte> blob(std::span<const std::byte> bytes, const Entry& entry) {
    return entry_data(bytes, entry);
}

compute::Matrix3 matrix3(std::span<const std::byte> bytes, const Entry& entry) {
    compute::Matrix3 matrix{};
    const auto values = numeric_values(bytes, entry);
    for (std::size_t i = 0; i < std::min<std::size_t>(matrix.values.size(), values.size()); ++i) {
        matrix.values[i] = values[i];
    }
    return matrix;
}

void apply_capture_entries(std::span<const std::byte> bytes, std::span<const Entry> entries,
                           compute::CaptureBlock* capture) {
    if (capture == nullptr) {
        return;
    }
    if (const auto entry = find_entry(entries, kTagExposureTime)) {
        const auto values = numeric_values(bytes, *entry);
        if (!values.empty()) {
            capture->exposure_time_ns = static_cast<std::int64_t>(values[0] * 1'000'000'000.0F);
        }
    }
    if (const auto entry = find_entry(entries, kTagIsoSpeedRatings)) {
        capture->iso = static_cast<std::int32_t>(first_u32(bytes, *entry));
    }
    if (const auto entry = find_entry(entries, kTagFNumber)) {
        const auto values = numeric_values(bytes, *entry);
        if (!values.empty()) {
            capture->lens_aperture = values[0];
        }
    }
    if (const auto entry = find_entry(entries, kTagFocalLength)) {
        const auto values = numeric_values(bytes, *entry);
        if (!values.empty()) {
            capture->lens_focal_length_mm = values[0];
        }
    }
}

ParseResult failed(std::string message) {
    return ParseResult{.status = CPIPE_FAILED, .metadata = {}, .message = std::move(message)};
}

std::vector<std::byte> read_all(std::ifstream& input) {
    const std::vector<char> chars{std::istreambuf_iterator<char>{input},
                                  std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(chars.size());
    std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
ParseResult OpcodeListParser::parse(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return failed("failed to open DNG");
    }
    const auto bytes = read_all(input);
    if (bytes.size() < 8 || bytes[0] != std::byte{'I'} || bytes[1] != std::byte{'I'} ||
        read_u16(bytes, 2) != kTiffMagic) {
        return failed("unsupported DNG/TIFF header");
    }

    const auto ifd_offset = read_u32(bytes, 4);
    const auto entries = read_entries(bytes, ifd_offset);
    if (!entries) {
        return failed("invalid IFD table");
    }

    ParsedDngMetadata out;
    auto apply_u32 = [&](std::uint16_t tag, auto setter) {
        if (const auto entry = find_entry(*entries, tag)) {
            setter(first_u32(bytes, *entry));
        }
    };

    auto first_or = [&](std::uint16_t tag, std::uint32_t fallback) {
        if (const auto entry = find_entry(*entries, tag)) {
            return first_u32(bytes, *entry);
        }
        return fallback;
    };
    const auto compression = first_or(kTagCompression, kCompressionNone);
    const auto photometric = first_or(kTagPhotometricInterpretation, kPhotometricCfa);
    const auto samples_per_pixel = first_or(kTagSamplesPerPixel, 1U);
    if (photometric != kPhotometricCfa || samples_per_pixel != 1U) {
        return ParseResult{.status = CPIPE_UNSUPPORTED,
                           .metadata = std::move(out),
                           .message = "only single-plane Bayer CFA DNG is supported in P1"};
    }

    apply_u32(kTagImageWidth, [&](std::uint32_t value) { out.width = value; });
    apply_u32(kTagImageLength, [&](std::uint32_t value) { out.height = value; });
    apply_u32(kTagBitsPerSample, [&](std::uint32_t value) {
        out.bits_per_sample = static_cast<std::uint16_t>(value);
    });
    apply_u32(kTagStripOffsets, [&](std::uint32_t value) { out.strip_offset = value; });
    apply_u32(kTagStripByteCounts, [&](std::uint32_t value) { out.strip_byte_count = value; });
    apply_u32(kTagOrientation, [&](std::uint32_t value) {
        out.capture.orientation = static_cast<std::uint8_t>(value);
    });
    apply_u32(kTagWhiteLevel, [&](std::uint32_t value) { out.calibration.white_level = value; });
    apply_u32(kTagCalibrationIlluminant1, [&](std::uint32_t value) {
        out.calibration.calibration_illuminant1 = static_cast<std::uint16_t>(value);
    });
    apply_u32(kTagCalibrationIlluminant2, [&](std::uint32_t value) {
        out.calibration.calibration_illuminant2 = static_cast<std::uint16_t>(value);
    });

    apply_capture_entries(bytes, *entries, &out.capture);

    if (const auto entry = find_entry(*entries, kTagMake)) {
        out.make = ascii(bytes, *entry);
        out.capture.camera_id = out.make;
    }
    if (const auto entry = find_entry(*entries, kTagModel)) {
        out.model = ascii(bytes, *entry);
        out.capture.physical_camera_id = out.model;
    }

    if (const auto entry = find_entry(*entries, kTagCfaRepeatPatternDim)) {
        const auto repeat_values = shorts(bytes, *entry);
        if (repeat_values.size() != 2) {
            return ParseResult{.status = CPIPE_UNSUPPORTED,
                               .metadata = std::move(out),
                               .message = "only 2x2 Bayer or 4x4 Quad Bayer DNG is supported"};
        }
        const auto supported_repeat = repeat_values[0] == repeat_values[1] &&
                                      (repeat_values[0] == 2 || repeat_values[0] == 4);
        if (!supported_repeat) {
            return ParseResult{.status = CPIPE_UNSUPPORTED,
                               .metadata = std::move(out),
                               .message = "only 2x2 Bayer or 4x4 Quad Bayer DNG is supported"};
        }
        out.calibration.cfa = compute::CFADescriptor{};
        out.calibration.cfa->repeat = {static_cast<std::uint8_t>(repeat_values[0]),
                                       static_cast<std::uint8_t>(repeat_values[1])};
    } else {
        return ParseResult{.status = CPIPE_UNSUPPORTED,
                           .metadata = std::move(out),
                           .message = "DNG is missing CFARepeatPatternDim"};
    }

    if (const auto entry = find_entry(*entries, kTagCfaPattern)) {
        const auto data = entry_data(bytes, *entry);
        if (out.calibration.cfa) {
            const auto expected_count = static_cast<std::size_t>(out.calibration.cfa->repeat[0]) *
                                        out.calibration.cfa->repeat[1];
            if (data.size() == expected_count &&
                expected_count <= out.calibration.cfa->pattern.size()) {
                for (std::size_t i = 0; i < expected_count; ++i) {
                    out.calibration.cfa->pattern[i] = std::to_integer<std::uint8_t>(data[i]);
                }
            } else {
                out.calibration.cfa.reset();
            }
        }
    } else {
        out.calibration.cfa.reset();
    }
    if (!out.calibration.cfa) {
        return ParseResult{.status = CPIPE_UNSUPPORTED,
                           .metadata = std::move(out),
                           .message = "DNG is missing a supported CFA pattern"};
    }

    if (const auto entry = find_entry(*entries, kTagBlackLevel)) {
        const auto black = numeric_values(bytes, *entry);
        for (std::size_t i = 0; i < std::min<std::size_t>(black.size(), 4); ++i) {
            out.calibration.black_level[i] = black[i];
        }
    }
    if (const auto entry = find_entry(*entries, kTagLinearizationTable)) {
        out.linearization_table = shorts(bytes, *entry);
        out.calibration.linearization_table = compute::LinearizationTable{out.linearization_table};
    }
    if (const auto entry = find_entry(*entries, kTagColorMatrix1)) {
        out.calibration.color_matrix1 = matrix3(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagColorMatrix2)) {
        out.calibration.color_matrix2 = matrix3(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagForwardMatrix1)) {
        out.calibration.forward_matrix1 = matrix3(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagForwardMatrix2)) {
        out.calibration.forward_matrix2 = matrix3(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagAsShotNeutral)) {
        const auto neutral = numeric_values(bytes, *entry);
        for (std::size_t i = 0; i < std::min<std::size_t>(neutral.size(), 3); ++i) {
            out.capture.as_shot_neutral[i] = neutral[i];
        }
    }
    if (const auto entry = find_entry(*entries, kTagActiveArea)) {
        const auto values = u32_values(bytes, *entry);
        if (values.size() == 4 && values[2] >= values[0] && values[3] >= values[1]) {
            out.active_area = compute::Rect2u{.x = values[1],
                                              .y = values[0],
                                              .width = values[3] - values[1],
                                              .height = values[2] - values[0]};
        }
    }
    if (const auto entry = find_entry(*entries, kTagExifIFD)) {
        const auto exif_offset = first_u32(bytes, *entry);
        out.exif_blob = copy_ifd_bytes(bytes, exif_offset);
        if (const auto exif_entries = read_entries(bytes, exif_offset)) {
            apply_capture_entries(bytes, *exif_entries, &out.capture);
        }
    }
    if (const auto entry = find_entry(*entries, kTagXmp)) {
        out.xmp_blob = blob(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagIccProfile)) {
        out.icc_blob = blob(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagOpcodeList1)) {
        out.opcode_list_1 = blob(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagOpcodeList2)) {
        out.opcode_list_2 = blob(bytes, *entry);
    }
    if (const auto entry = find_entry(*entries, kTagOpcodeList3)) {
        out.opcode_list_3 = blob(bytes, *entry);
    }

    if (out.width == 0 || out.height == 0 || out.bits_per_sample == 0 || out.bits_per_sample > 16 ||
        out.strip_byte_count == 0) {
        return failed("DNG is missing required RAW fields");
    }
    if (compression != kCompressionNone && out.strip_offset == 0) {
        return failed("compressed DNG is missing a raw strip offset");
    }
    return ParseResult{.status = CPIPE_OK, .metadata = std::move(out), .message = {}};
}

}  // namespace cpipe::ingest::dng_opcode
