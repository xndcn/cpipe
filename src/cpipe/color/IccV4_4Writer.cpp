// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <lcms2.h>

#include <algorithm>
#include <cpipe/color/IccV4_4Writer.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cpipe::color {
namespace {

struct TagRecord {
    std::uint32_t signature{0};
    std::uint32_t offset{0};
    std::uint32_t size{0};
};

std::uint32_t fourcc(std::string_view value) {
    return (static_cast<std::uint32_t>(value[0]) << 24U) |
           (static_cast<std::uint32_t>(value[1]) << 16U) |
           (static_cast<std::uint32_t>(value[2]) << 8U) | static_cast<std::uint32_t>(value[3]);
}

std::uint32_t read_u32_be(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset + 0U]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

void write_u32_be(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

void append_u32_be(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void align4(std::vector<std::uint8_t>& bytes) {
    while ((bytes.size() % 4U) != 0U) {
        bytes.push_back(0);
    }
}

std::vector<std::uint8_t> make_base_profile() {
    cmsCIExyY white_point{0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries{
        {0.708, 0.292, 1.0},
        {0.170, 0.797, 1.0},
        {0.131, 0.046, 1.0},
    };

    cmsToneCurve* raw_curve = cmsBuildGamma(nullptr, 1.0);
    if (raw_curve == nullptr) {
        throw std::runtime_error{"failed to create ICC tone curve"};
    }
    std::unique_ptr<cmsToneCurve, decltype(&cmsFreeToneCurve)> curve{raw_curve, &cmsFreeToneCurve};
    cmsToneCurve* curves[3] = {curve.get(), curve.get(), curve.get()};
    cmsHPROFILE raw_profile = cmsCreateRGBProfile(&white_point, &primaries, curves);
    if (raw_profile == nullptr) {
        throw std::runtime_error{"failed to create base ICC profile"};
    }
    std::unique_ptr<void, decltype(&cmsCloseProfile)> profile{raw_profile, &cmsCloseProfile};
    cmsSetProfileVersion(profile.get(), 4.4);

    cmsUInt32Number size = 0;
    if (cmsSaveProfileToMem(profile.get(), nullptr, &size) == 0 || size == 0) {
        throw std::runtime_error{"failed to size base ICC profile"};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (cmsSaveProfileToMem(profile.get(), bytes.data(), &size) == 0) {
        throw std::runtime_error{"failed to serialize base ICC profile"};
    }
    bytes.resize(static_cast<std::size_t>(size));
    return bytes;
}

std::vector<TagRecord> read_tag_records(const std::vector<std::uint8_t>& profile) {
    if (profile.size() < 132U || read_u32_be(profile, 36U) != fourcc("acsp")) {
        throw std::runtime_error{"invalid ICC base profile"};
    }
    const auto count = read_u32_be(profile, 128U);
    if (132U + (static_cast<std::size_t>(count) * 12U) > profile.size()) {
        throw std::runtime_error{"invalid ICC tag table"};
    }

    std::vector<TagRecord> records;
    records.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto offset = 132U + (static_cast<std::size_t>(i) * 12U);
        TagRecord record{
            .signature = read_u32_be(profile, offset),
            .offset = read_u32_be(profile, offset + 4U),
            .size = read_u32_be(profile, offset + 8U),
        };
        if (static_cast<std::size_t>(record.offset) + record.size > profile.size()) {
            throw std::runtime_error{"invalid ICC tag payload"};
        }
        records.push_back(record);
    }
    return records;
}

std::vector<std::uint8_t> cicp_tag() {
    std::vector<std::uint8_t> bytes;
    append_u32_be(bytes, fourcc("cicp"));
    append_u32_be(bytes, 0);
    bytes.push_back(9);
    bytes.push_back(16);
    bytes.push_back(9);
    bytes.push_back(1);
    return bytes;
}

std::vector<std::uint8_t> a2b0_float_tag() {
    std::vector<std::uint8_t> bytes;
    append_u32_be(bytes, fourcc("mABF"));
    append_u32_be(bytes, 0);
    bytes.push_back(3);
    bytes.push_back(3);
    bytes.push_back(0);
    bytes.push_back(0);
    append_u32_be(bytes, 16);
    append_u32_be(bytes, 0);
    return bytes;
}

}  // namespace

std::vector<std::uint8_t> IccV4_4Writer::bt2020_pq() {
    const auto base = make_base_profile();
    const auto base_records = read_tag_records(base);

    struct Payload {
        std::uint32_t signature{0};
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Payload> payloads;
    payloads.reserve(base_records.size() + 2U);
    for (const auto& record : base_records) {
        if (record.signature == fourcc("cicp") || record.signature == fourcc("A2B0")) {
            continue;
        }
        const auto begin = base.begin() + static_cast<std::ptrdiff_t>(record.offset);
        payloads.push_back(Payload{.signature = record.signature,
                                   .bytes = std::vector<std::uint8_t>{begin, begin + record.size}});
    }
    payloads.push_back(Payload{.signature = fourcc("cicp"), .bytes = cicp_tag()});
    payloads.push_back(Payload{.signature = fourcc("A2B0"), .bytes = a2b0_float_tag()});
    std::sort(payloads.begin(), payloads.end(),
              [](const Payload& lhs, const Payload& rhs) { return lhs.signature < rhs.signature; });

    std::vector<TagRecord> new_records;
    new_records.reserve(payloads.size());
    const auto tag_table_end = 132U + (payloads.size() * 12U);
    std::vector<std::uint8_t> out(base.begin(), base.begin() + 128);
    append_u32_be(out, static_cast<std::uint32_t>(payloads.size()));
    out.resize(tag_table_end, 0);

    for (const auto& payload : payloads) {
        align4(out);
        const auto offset = static_cast<std::uint32_t>(out.size());
        out.insert(out.end(), payload.bytes.begin(), payload.bytes.end());
        new_records.push_back(TagRecord{.signature = payload.signature,
                                        .offset = offset,
                                        .size = static_cast<std::uint32_t>(payload.bytes.size())});
    }

    write_u32_be(out, 0U, static_cast<std::uint32_t>(out.size()));
    out[8] = 0x04U;
    out[9] = 0x40U;
    out[10] = 0x00U;
    out[11] = 0x00U;

    for (std::size_t i = 0; i < new_records.size(); ++i) {
        const auto record_offset = 132U + (i * 12U);
        write_u32_be(out, record_offset, new_records[i].signature);
        write_u32_be(out, record_offset + 4U, new_records[i].offset);
        write_u32_be(out, record_offset + 8U, new_records[i].size);
    }
    return out;
}

}  // namespace cpipe::color
