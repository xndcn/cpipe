// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cpipe::tests {

struct SyntheticDngOptions {
    std::uint32_t width{4};
    std::uint32_t height{3};
    std::array<std::uint16_t, 2> cfa_repeat{2, 2};
    std::vector<std::uint8_t> cfa_pattern{0, 1, 1, 2};
    bool include_cfa{true};
    bool include_opcode_lists{true};
    bool include_sidecars{true};
    std::vector<std::uint16_t> pixels;
};

inline void push_u16(std::vector<std::byte>* out, std::uint16_t value) {
    out->push_back(static_cast<std::byte>(value & 0xffU));
    out->push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

inline void push_u32(std::vector<std::byte>* out, std::uint32_t value) {
    push_u16(out, static_cast<std::uint16_t>(value & 0xffffU));
    push_u16(out, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

inline std::vector<std::byte> bytes_u16(std::span<const std::uint16_t> values) {
    std::vector<std::byte> out;
    out.reserve(values.size() * sizeof(std::uint16_t));
    for (const auto value : values) {
        push_u16(&out, value);
    }
    return out;
}

inline std::vector<std::byte> bytes_u32(std::span<const std::uint32_t> values) {
    std::vector<std::byte> out;
    out.reserve(values.size() * sizeof(std::uint32_t));
    for (const auto value : values) {
        push_u32(&out, value);
    }
    return out;
}

inline std::vector<std::byte> bytes_ascii(std::string value) {
    value.push_back('\0');
    std::vector<std::byte> out(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(value[i]));
    }
    return out;
}

inline std::vector<std::byte> bytes_rational(
    std::span<const std::pair<std::uint32_t, std::uint32_t>> values) {
    std::vector<std::byte> out;
    out.reserve(values.size() * 8U);
    for (const auto& [numerator, denominator] : values) {
        push_u32(&out, numerator);
        push_u32(&out, denominator);
    }
    return out;
}

inline std::vector<std::byte> bytes_srational(
    std::span<const std::pair<std::int32_t, std::int32_t>> values) {
    std::vector<std::byte> out;
    out.reserve(values.size() * 8U);
    for (const auto& [numerator, denominator] : values) {
        push_u32(&out, static_cast<std::uint32_t>(numerator));
        push_u32(&out, static_cast<std::uint32_t>(denominator));
    }
    return out;
}

inline std::vector<std::uint16_t> default_pixels(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint16_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * height);
    for (std::uint32_t i = 0; i < width * height; ++i) {
        pixels.push_back(static_cast<std::uint16_t>(64U + i * 9U));
    }
    return pixels;
}

class TiffWriter {
public:
    std::size_t add_section(std::vector<std::byte> data) {
        sections_.push_back(std::move(data));
        return sections_.size() - 1U;
    }

    void add_value(std::uint16_t tag, std::uint16_t type, std::uint32_t count,
                   std::vector<std::byte> data) {
        entries_.push_back(
            Entry{.tag = tag, .type = type, .count = count, .data = std::move(data)});
    }

    void add_offset(std::uint16_t tag, std::size_t section_index) {
        entries_.push_back(Entry{
            .tag = tag, .type = kTypeLong, .count = 1, .data = {}, .section_index = section_index});
    }

    std::vector<std::byte> finish() {
        const auto entry_count = static_cast<std::uint16_t>(entries_.size());
        const auto ifd_size = 2U + static_cast<std::uint32_t>(entry_count) * 12U + 4U;
        auto next_data_offset = 8U + ifd_size;

        for (auto& entry : entries_) {
            if (entry.section_index != kNoSection && entry.section_index >= sections_.size()) {
                throw std::runtime_error("invalid TIFF section index");
            }
            if (entry.section_index == kNoSection && entry.data.size() > 4U) {
                entry.value_or_offset = next_data_offset;
                next_data_offset += static_cast<std::uint32_t>(entry.data.size());
            }
        }

        std::vector<std::uint32_t> section_offsets(sections_.size(), 0);
        for (std::size_t i = 0; i < sections_.size(); ++i) {
            section_offsets[i] = next_data_offset;
            next_data_offset += static_cast<std::uint32_t>(sections_[i].size());
        }
        for (auto& entry : entries_) {
            if (entry.section_index != kNoSection) {
                entry.value_or_offset = section_offsets[entry.section_index];
            }
        }

        std::vector<std::byte> out;
        out.reserve(next_data_offset);
        out.push_back(std::byte{'I'});
        out.push_back(std::byte{'I'});
        push_u16(&out, 42);
        push_u32(&out, 8);
        push_u16(&out, entry_count);
        for (const auto& entry : entries_) {
            push_u16(&out, entry.tag);
            push_u16(&out, entry.type);
            push_u32(&out, entry.count);
            if (entry.section_index == kNoSection && entry.data.size() <= 4U) {
                for (std::size_t i = 0; i < 4U; ++i) {
                    out.push_back(i < entry.data.size() ? entry.data[i] : std::byte{0});
                }
            } else {
                push_u32(&out, entry.value_or_offset);
            }
        }
        push_u32(&out, 0);

        for (const auto& entry : entries_) {
            if (entry.section_index == kNoSection && entry.data.size() > 4U) {
                out.insert(out.end(), entry.data.begin(), entry.data.end());
            }
        }
        for (const auto& section : sections_) {
            out.insert(out.end(), section.begin(), section.end());
        }
        return out;
    }

private:
    static constexpr std::uint16_t kTypeLong = 4;
    static constexpr std::size_t kNoSection = static_cast<std::size_t>(-1);

    struct Entry {
        std::uint16_t tag{0};
        std::uint16_t type{0};
        std::uint32_t count{0};
        std::vector<std::byte> data;
        std::size_t section_index{kNoSection};
        std::uint32_t value_or_offset{0};
    };

    std::vector<Entry> entries_;
    std::vector<std::vector<std::byte>> sections_;
};

inline std::filesystem::path write_synthetic_dng(std::string name, SyntheticDngOptions options) {
    constexpr std::uint16_t kTypeByte = 1;
    constexpr std::uint16_t kTypeAscii = 2;
    constexpr std::uint16_t kTypeShort = 3;
    constexpr std::uint16_t kTypeLong = 4;
    constexpr std::uint16_t kTypeRational = 5;
    constexpr std::uint16_t kTypeUndefined = 7;
    constexpr std::uint16_t kTypeSRational = 10;

    if (options.pixels.empty()) {
        options.pixels = default_pixels(options.width, options.height);
    }

    TiffWriter writer;
    const auto raw_section = writer.add_section(bytes_u16(options.pixels));
    const auto exif_section = writer.add_section(std::vector<std::byte>{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});

    writer.add_value(256, kTypeLong, 1, bytes_u32(std::array<std::uint32_t, 1>{options.width}));
    writer.add_value(257, kTypeLong, 1, bytes_u32(std::array<std::uint32_t, 1>{options.height}));
    writer.add_value(258, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{16}));
    writer.add_value(259, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{1}));
    writer.add_value(262, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{32803}));
    writer.add_value(271, kTypeAscii, 11, bytes_ascii("GoogleTest"));
    writer.add_value(272, kTypeAscii, 9, bytes_ascii("Pixel8UT"));
    writer.add_offset(273, raw_section);
    writer.add_value(274, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{1}));
    writer.add_value(277, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{1}));
    writer.add_value(278, kTypeLong, 1, bytes_u32(std::array<std::uint32_t, 1>{options.height}));
    writer.add_value(279, kTypeLong, 1,
                     bytes_u32(std::array<std::uint32_t, 1>{
                         static_cast<std::uint32_t>(options.pixels.size() * 2U)}));
    writer.add_value(33434, kTypeRational, 1,
                     bytes_rational(std::array<std::pair<std::uint32_t, std::uint32_t>, 1>{
                         std::pair<std::uint32_t, std::uint32_t>{1, 125}}));
    writer.add_value(33437, kTypeRational, 1,
                     bytes_rational(std::array<std::pair<std::uint32_t, std::uint32_t>, 1>{
                         std::pair<std::uint32_t, std::uint32_t>{17, 10}}));
    writer.add_offset(34665, exif_section);
    writer.add_value(34855, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{400}));
    writer.add_value(37386, kTypeRational, 1,
                     bytes_rational(std::array<std::pair<std::uint32_t, std::uint32_t>, 1>{
                         std::pair<std::uint32_t, std::uint32_t>{43, 10}}));

    if (options.include_sidecars) {
        const auto xmp = bytes_ascii("<x:xmpmeta>unit</x:xmpmeta>");
        writer.add_value(700, kTypeByte, static_cast<std::uint32_t>(xmp.size()), xmp);
        writer.add_value(
            34675, kTypeUndefined, 4,
            std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    }

    if (options.include_cfa) {
        writer.add_value(
            33421, kTypeShort, 2,
            bytes_u16(std::array<std::uint16_t, 2>{options.cfa_repeat[0], options.cfa_repeat[1]}));
        std::vector<std::byte> cfa_pattern(options.cfa_pattern.size());
        for (std::size_t i = 0; i < options.cfa_pattern.size(); ++i) {
            cfa_pattern[i] = static_cast<std::byte>(options.cfa_pattern[i]);
        }
        const auto cfa_count = static_cast<std::uint32_t>(cfa_pattern.size());
        writer.add_value(33422, kTypeByte, cfa_count, std::move(cfa_pattern));
    }

    writer.add_value(
        50706, kTypeByte, 4,
        std::vector<std::byte>{std::byte{1}, std::byte{4}, std::byte{0}, std::byte{0}});
    writer.add_value(
        50707, kTypeByte, 4,
        std::vector<std::byte>{std::byte{1}, std::byte{4}, std::byte{0}, std::byte{0}});
    const auto unique_model = bytes_ascii("cpipe unit camera");
    writer.add_value(50708, kTypeAscii, static_cast<std::uint32_t>(unique_model.size()),
                     unique_model);
    writer.add_value(50712, kTypeShort, 4,
                     bytes_u16(std::array<std::uint16_t, 4>{0, 128, 1024, 4095}));
    writer.add_value(50714, kTypeRational, 4,
                     bytes_rational(std::array<std::pair<std::uint32_t, std::uint32_t>, 4>{
                         std::pair<std::uint32_t, std::uint32_t>{64, 1},
                         std::pair<std::uint32_t, std::uint32_t>{65, 1},
                         std::pair<std::uint32_t, std::uint32_t>{66, 1},
                         std::pair<std::uint32_t, std::uint32_t>{67, 1}}));
    writer.add_value(50717, kTypeLong, 1, bytes_u32(std::array<std::uint32_t, 1>{4095}));
    const std::array<std::pair<std::int32_t, std::int32_t>, 9> identity_matrix{
        std::pair<std::int32_t, std::int32_t>{1, 1}, std::pair<std::int32_t, std::int32_t>{0, 1},
        std::pair<std::int32_t, std::int32_t>{0, 1}, std::pair<std::int32_t, std::int32_t>{0, 1},
        std::pair<std::int32_t, std::int32_t>{1, 1}, std::pair<std::int32_t, std::int32_t>{0, 1},
        std::pair<std::int32_t, std::int32_t>{0, 1}, std::pair<std::int32_t, std::int32_t>{0, 1},
        std::pair<std::int32_t, std::int32_t>{1, 1},
    };
    writer.add_value(50721, kTypeSRational, 9, bytes_srational(identity_matrix));
    writer.add_value(50722, kTypeSRational, 9, bytes_srational(identity_matrix));
    writer.add_value(50728, kTypeRational, 3,
                     bytes_rational(std::array<std::pair<std::uint32_t, std::uint32_t>, 3>{
                         std::pair<std::uint32_t, std::uint32_t>{1, 1},
                         std::pair<std::uint32_t, std::uint32_t>{2, 1},
                         std::pair<std::uint32_t, std::uint32_t>{3, 1}}));
    writer.add_value(50778, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{21}));
    writer.add_value(50779, kTypeShort, 1, bytes_u16(std::array<std::uint16_t, 1>{17}));
    writer.add_value(50829, kTypeLong, 4,
                     bytes_u32(std::array<std::uint32_t, 4>{0, 0, options.height, options.width}));
    writer.add_value(50964, kTypeSRational, 9, bytes_srational(identity_matrix));
    writer.add_value(50965, kTypeSRational, 9, bytes_srational(identity_matrix));
    if (options.include_opcode_lists) {
        writer.add_value(
            51008, kTypeUndefined, 4,
            std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
        writer.add_value(
            51009, kTypeUndefined, 4,
            std::vector<std::byte>{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}});
        writer.add_value(
            51022, kTypeUndefined, 8,
            std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1},
                                   std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12}});
    }

    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("cpipe_" + name + "_" + std::to_string(counter++) + ".dng");
    const auto bytes = writer.finish();
    std::ofstream out{path, std::ios::binary};
    if (!out) {
        throw std::runtime_error("failed to open synthetic DNG");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out.good()) {
        throw std::runtime_error("failed to write synthetic DNG");
    }
    return path;
}

}  // namespace cpipe::tests
