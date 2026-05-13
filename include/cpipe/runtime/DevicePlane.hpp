// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <string_view>

namespace cpipe::runtime {

class IDevicePlane {
public:
    virtual ~IDevicePlane() = default;

    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t device_memory_budget_bytes() const noexcept = 0;
};

}  // namespace cpipe::runtime
