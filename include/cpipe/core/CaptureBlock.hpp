// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace cpipe::compute {

struct CaptureBlock {
    std::int64_t sensor_timestamp_ns{0};
    std::int64_t exposure_time_ns{0};
    std::int32_t iso{0};
    float lens_focal_length_mm{0.0F};
    float lens_aperture{0.0F};
    float lens_focus_distance_d{0.0F};
    std::array<float, 3> as_shot_neutral{1.0F, 1.0F, 1.0F};
    std::uint8_t orientation{1};

    std::string camera_id;
    std::string physical_camera_id;
    std::uint32_t burst_index{0};
    std::uint32_t burst_size{1};

    ~CaptureBlock();
};

}  // namespace cpipe::compute
