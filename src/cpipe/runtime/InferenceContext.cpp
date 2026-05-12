// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/InferenceContext.hpp>
#include <cstddef>

namespace cpipe::runtime {
namespace {

// NOLINTNEXTLINE(readability-named-parameter,bugprone-easily-swappable-parameters)
auto unsupported_submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                  std::size_t, cpipe_buffer_t* const*, std::size_t) -> int {
    return CPIPE_UNSUPPORTED;
}

const cpipe_inference_suite_v1 kInferenceSuite{&unsupported_submit_inference};

}  // namespace

auto InferenceContext::handle() noexcept -> cpipe_inference_t* {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<cpipe_inference_t*>(this);
}

auto inference_suite_v1() noexcept -> const cpipe_inference_suite_v1& {
    return kInferenceSuite;
}

}  // namespace cpipe::runtime
