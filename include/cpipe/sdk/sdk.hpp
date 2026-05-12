// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::sdk {

class Buffer {
public:
    Buffer(cpipe_buffer_t* handle, const cpipe_buffer_suite_v1* suite) noexcept
        : handle_(handle), suite_(suite) {}

    [[nodiscard]] cpipe_buffer_t* handle() const noexcept {
        return handle_;
    }

    [[nodiscard]] int format() const noexcept {
        int value = 0;
        return suite_->get_format(handle_, &value) == CPIPE_OK ? value : 0;
    }

    [[nodiscard]] int kind() const noexcept {
        int value = 0;
        return suite_->get_kind(handle_, &value) == CPIPE_OK ? value : 0;
    }

    [[nodiscard]] uint32_t width() const noexcept {
        uint8_t ndim = 0;
        uint32_t dims[8] = {};
        if (suite_->get_dims(handle_, &ndim, dims) != CPIPE_OK || ndim < 1) {
            return 0;
        }
        return dims[0];
    }

    [[nodiscard]] uint32_t height() const noexcept {
        uint8_t ndim = 0;
        uint32_t dims[8] = {};
        if (suite_->get_dims(handle_, &ndim, dims) != CPIPE_OK || ndim < 2) {
            return 0;
        }
        return dims[1];
    }

    [[nodiscard]] void* lock_cpu(int access) const noexcept {
        void* ptr = nullptr;
        return suite_->lock_cpu(handle_, access, &ptr) == CPIPE_OK ? ptr : nullptr;
    }

    void unlock_cpu() const noexcept {
        static_cast<void>(suite_->unlock_cpu(handle_));
    }

private:
    cpipe_buffer_t* handle_ = nullptr;
    const cpipe_buffer_suite_v1* suite_ = nullptr;
};

class ComputeContext {
public:
    ComputeContext(cpipe_compute_t* handle, const cpipe_compute_suite_v1* suite) noexcept
        : handle_(handle), suite_(suite) {}

    [[nodiscard]] int submit_halide(std::string_view aot_id, const cpipe_buffer_t* const* inputs,
                                    std::size_t n_in, cpipe_buffer_t* const* outputs,
                                    std::size_t n_out) const noexcept {
        return suite_->submit_halide(handle_, aot_id.data(), inputs, n_in, outputs, n_out);
    }

private:
    cpipe_compute_t* handle_ = nullptr;
    const cpipe_compute_suite_v1* suite_ = nullptr;
};

inline void* host_alloc(cpipe_host_t* host, std::size_t bytes) noexcept {
    return host != nullptr && host->alloc != nullptr ? host->alloc(host, bytes)
                                                     : std::malloc(bytes);
}

inline void host_free(cpipe_host_t* host, void* ptr) noexcept {
    if (host != nullptr && host->free != nullptr) {
        host->free(host, ptr);
        return;
    }
    std::free(ptr);
}

}  // namespace cpipe::sdk
