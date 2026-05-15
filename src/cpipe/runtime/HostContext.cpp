// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenColorIO/OpenColorIO.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

struct cpipe_ocio_processor_s {
    OCIO::ConstCPUProcessorRcPtr cpu_processor;
};

namespace {

std::uint32_t half_to_bits(std::uint16_t bits) {
    const auto sign = static_cast<std::uint32_t>(bits & 0x8000U) << 16U;
    auto exponent = static_cast<std::uint32_t>((bits >> 10U) & 0x1fU);
    auto mantissa = static_cast<std::uint32_t>(bits & 0x03ffU);
    if (exponent == 0) {
        if (mantissa == 0) {
            return sign;
        }
        exponent = 1;
        while ((mantissa & 0x0400U) == 0U) {
            mantissa <<= 1U;
            --exponent;
        }
        mantissa &= 0x03ffU;
        return sign | ((exponent + 112U) << 23U) | (mantissa << 13U);
    }
    if (exponent == 31U) {
        return sign | 0x7f800000U | (mantissa << 13U);
    }
    return sign | ((exponent + 112U) << 23U) | (mantissa << 13U);
}

float half_to_float(std::uint16_t bits) {
    const auto out = half_to_bits(bits);
    float value = 0.0F;
    std::memcpy(&value, &out, sizeof(value));
    return value;
}

std::uint16_t float_to_half(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const auto sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    auto exponent = static_cast<int>((bits >> 23U) & 0xffU) - 127 + 15;
    auto mantissa = bits & 0x7fffffU;
    if (exponent <= 0) {
        if (exponent < -10) {
            return sign;
        }
        mantissa |= 0x800000U;
        const auto shifted = mantissa >> static_cast<unsigned>(1 - exponent + 13);
        return static_cast<std::uint16_t>(sign | shifted);
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00U);
    }
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(sign) |
                                      (static_cast<std::uint32_t>(exponent) << 10U) |
                                      (mantissa >> 13U));
}

}  // namespace

namespace cpipe::runtime {

HostContext::HostContext() {
    buffer_suite_.get_dims = &HostContext::get_dims;
    buffer_suite_.get_format = &HostContext::get_format;
    buffer_suite_.get_kind = &HostContext::get_kind;
    buffer_suite_.get_stride = &HostContext::get_stride;
    buffer_suite_.get_color_role = &HostContext::get_color_role;
    buffer_suite_.get_metadata = &HostContext::get_metadata;
    buffer_suite_.lock_cpu = &HostContext::lock_cpu;
    buffer_suite_.unlock_cpu = &HostContext::unlock_cpu;
    buffer_suite_.flush_cpu_writes = &HostContext::flush_cpu_writes;

    metadata_suite_.get_calibration = &HostContext::get_calibration;
    metadata_suite_.get_capture = &HostContext::get_capture;
    metadata_suite_.get_tensor_quant = &HostContext::get_tensor_quant;
    metadata_suite_.get_cs_role = &HostContext::get_cs_role;
    metadata_suite_.get_active_area = &HostContext::get_active_area;
    metadata_suite_.has_applied_step = &HostContext::has_applied_step;
    metadata_suite_.list_applied_steps = &HostContext::list_applied_steps;
    metadata_suite_.get_blob = &HostContext::get_blob;
    metadata_suite_.list_blob_keys = &HostContext::list_blob_keys;

    metadata_builder_suite_.share_calibration_from = &HostContext::share_calibration_from;
    metadata_builder_suite_.clear_calibration = &HostContext::clear_calibration;
    metadata_builder_suite_.clear_cfa = &HostContext::clear_cfa;
    metadata_builder_suite_.set_as_shot_neutral = &HostContext::set_as_shot_neutral;
    metadata_builder_suite_.set_orientation = &HostContext::set_orientation;
    metadata_builder_suite_.set_cs_role = &HostContext::set_cs_role;
    metadata_builder_suite_.add_applied_step = &HostContext::add_applied_step;
    metadata_builder_suite_.remove_applied_step = &HostContext::remove_applied_step;
    metadata_builder_suite_.set_active_area = &HostContext::set_active_area;
    metadata_builder_suite_.set_tensor_quant = &HostContext::set_tensor_quant;
    metadata_builder_suite_.set_blob = &HostContext::set_blob;
    metadata_builder_suite_.merge_from = &HostContext::merge_from;

    compute_suite_.submit_halide = &HostContext::submit_halide;
    compute_suite_.submit_slang = &HostContext::submit_slang;
    compute_suite_.request_scratch = &HostContext::request_scratch;
    compute_suite_.record_marker = &HostContext::record_marker;
    compute_suite_.submit_halide_with_params = &HostContext::submit_halide_with_params;
    compute_suite_.submit_ocio_processor = &HostContext::submit_ocio_processor;

    param_suite_.get_double = &HostContext::get_param_double;
    param_suite_.get_int = &HostContext::get_param_int;
    param_suite_.get_bool = &HostContext::get_param_bool;
    param_suite_.get_enum = &HostContext::get_param_enum;
    param_suite_.get_curve = &HostContext::get_param_curve;
    param_suite_.get_color = &HostContext::get_param_color;

    inference_suite_.submit_inference = &HostContext::submit_inference;

    host_.abi_major = CPIPE_ABI_MAJOR;
    host_.abi_minor = CPIPE_ABI_MINOR;
    host_.get_suite = &HostContext::get_suite;
    host_.log = &HostContext::log;
    host_.alloc = &HostContext::alloc;
    host_.free = &HostContext::free;
    host_.get_ocio_processor = &HostContext::get_ocio_processor;
}

HostContext::~HostContext() = default;

cpipe_host_t* HostContext::host() noexcept {
    return &host_;
}

const void* HostContext::get_suite(cpipe_host_t* self, const char* suite_name, int version) {
    if (self == nullptr || version != 1) {
        return nullptr;
    }

    auto* context = reinterpret_cast<HostContext*>(reinterpret_cast<char*>(self) -
                                                   offsetof(HostContext, host_));
    if (std::strcmp(suite_name, "buffer") == 0) {
        return &context->buffer_suite_;
    }
    if (std::strcmp(suite_name, "compute") == 0) {
        return &context->compute_suite_;
    }
    if (std::strcmp(suite_name, "param") == 0) {
        return &context->param_suite_;
    }
    if (std::strcmp(suite_name, "inference") == 0) {
        return &context->inference_suite_;
    }
    if (std::strcmp(suite_name, "metadata") == 0) {
        return &context->metadata_suite_;
    }
    if (std::strcmp(suite_name, "metadata_builder") == 0) {
        return &context->metadata_builder_suite_;
    }
    return nullptr;
}

void HostContext::log(cpipe_host_t*, int level, const char* msg) {
    std::clog << "cpipe plugin log[" << level << "]: " << msg << '\n';
}

void* HostContext::alloc(cpipe_host_t*, std::size_t bytes) {
    return std::malloc(bytes);
}

void HostContext::free(cpipe_host_t*, void* ptr) {
    std::free(ptr);
}

int HostContext::get_dims(const cpipe_buffer_t* buffer, std::uint8_t* ndim,
                          std::uint32_t out_dims[8]) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || ndim == nullptr || out_dims == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = impl->layout();
    *ndim = layout.ndim;
    for (std::uint8_t i = 0; i < layout.ndim; ++i) {
        out_dims[i] = layout.dims[i];
    }
    return CPIPE_OK;
}

int HostContext::get_format(const cpipe_buffer_t* buffer, int* out_format) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_format == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_format = static_cast<int>(impl->layout().format);
    return CPIPE_OK;
}

int HostContext::get_kind(const cpipe_buffer_t* buffer, int* out_kind) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_kind == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_kind = static_cast<int>(impl->layout().kind);
    return CPIPE_OK;
}

int HostContext::get_stride(const cpipe_buffer_t* buffer, std::uint64_t out_stride[8]) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_stride == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = impl->layout();
    for (std::uint8_t i = 0; i < layout.ndim; ++i) {
        out_stride[i] = layout.stride[i];
    }
    return CPIPE_OK;
}

int HostContext::get_color_role(const cpipe_buffer_t* buffer, const char** out_role) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || out_role == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_role = impl->color_role().data();
    return CPIPE_OK;
}

int HostContext::get_metadata(const cpipe_buffer_t* buffer, const cpipe_metadata_t** out) {
    auto* handle = const_cast<cpipe_buffer_t*>(buffer);
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || handle == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    handle->metadata_view.metadata = impl->metadata();
    *out = handle->metadata_view.metadata ? &handle->metadata_view : nullptr;
    return CPIPE_OK;
}

int HostContext::lock_cpu(cpipe_buffer_t* buffer, int access, void** ptr) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr || ptr == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *ptr = impl->lock_cpu(static_cast<compute::IBuffer::CpuAccess>(access));
    return CPIPE_OK;
}

int HostContext::unlock_cpu(cpipe_buffer_t* buffer) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->unlock_cpu();
    return CPIPE_OK;
}

int HostContext::flush_cpu_writes(cpipe_buffer_t* buffer) {
    const auto impl = buffer_from_handle(buffer);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->flush_cpu_writes();
    return CPIPE_OK;
}

int HostContext::get_calibration(const cpipe_metadata_t* metadata, cpipe_calibration_view* out) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    *out = {};
    out->get_noise_profile = &HostContext::get_noise_profile;
    out->get_linearization_table = &HostContext::get_linearization_table;
    if (!impl->calibration) {
        return CPIPE_OK;
    }

    const auto& calibration = *impl->calibration;
    if (calibration.cfa) {
        out->has_cfa = 1;
        out->cfa_repeat[0] = 2;
        out->cfa_repeat[1] = 2;
        std::copy(calibration.cfa->pattern.begin(), calibration.cfa->pattern.end(),
                  out->cfa_pattern);
    }
    std::copy(calibration.black_level.begin(), calibration.black_level.end(), out->black_level);
    out->white_level = calibration.white_level;
    if (calibration.linearization_table) {
        out->has_linearization_table = 1;
    }
    if (calibration.color_matrix1) {
        out->has_color_matrix1 = 1;
        std::copy(calibration.color_matrix1->values.begin(),
                  calibration.color_matrix1->values.end(), out->color_matrix1);
    }
    if (calibration.color_matrix2) {
        out->has_color_matrix2 = 1;
        std::copy(calibration.color_matrix2->values.begin(),
                  calibration.color_matrix2->values.end(), out->color_matrix2);
    }
    if (calibration.forward_matrix1) {
        out->has_forward_matrix1 = 1;
        std::copy(calibration.forward_matrix1->values.begin(),
                  calibration.forward_matrix1->values.end(), out->forward_matrix1);
    }
    if (calibration.forward_matrix2) {
        out->has_forward_matrix2 = 1;
        std::copy(calibration.forward_matrix2->values.begin(),
                  calibration.forward_matrix2->values.end(), out->forward_matrix2);
    }
    out->calibration_illuminant1 = calibration.calibration_illuminant1;
    out->calibration_illuminant2 = calibration.calibration_illuminant2;
    return CPIPE_OK;
}

int HostContext::get_linearization_table(const cpipe_metadata_t* metadata, std::size_t max_values,
                                         std::size_t* out_n, std::uint16_t* out_values) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out_n == nullptr || (max_values > 0 && out_values == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    if (!impl->calibration) {
        *out_n = 0;
        return CPIPE_OK;
    }
    const auto& calibration = *impl->calibration;
    if (!calibration.linearization_table) {
        *out_n = 0;
        return CPIPE_OK;
    }
    const auto& values = calibration.linearization_table->values;
    *out_n = values.size();
    const auto count = std::min(max_values, values.size());
    for (std::size_t i = 0; i < count; ++i) {
        out_values[i] = values[i];
    }
    return CPIPE_OK;
}

int HostContext::get_capture(const cpipe_metadata_t* metadata, cpipe_capture_view* out) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    *out = {};
    const auto& capture = impl->capture;
    out->sensor_timestamp_ns = capture.sensor_timestamp_ns;
    out->exposure_time_ns = capture.exposure_time_ns;
    out->iso = capture.iso;
    out->lens_focal_length_mm = capture.lens_focal_length_mm;
    out->lens_aperture = capture.lens_aperture;
    out->lens_focus_distance_d = capture.lens_focus_distance_d;
    std::copy(capture.as_shot_neutral.begin(), capture.as_shot_neutral.end(), out->as_shot_neutral);
    out->orientation = capture.orientation;
    out->burst_index = capture.burst_index;
    out->burst_size = capture.burst_size;
    out->get_camera_id = &HostContext::get_camera_id;
    out->get_physical_camera_id = &HostContext::get_physical_camera_id;
    return CPIPE_OK;
}

int HostContext::get_tensor_quant(const cpipe_metadata_t* metadata, cpipe_tensor_quant_view* out) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    *out = {};
    out->scheme = static_cast<int>(impl->tensor_quant.scheme);
    if (impl->tensor_quant.axis) {
        out->has_axis = 1;
        out->axis = *impl->tensor_quant.axis;
    }
    out->get_scales = &HostContext::get_scales;
    out->get_zero_points = &HostContext::get_zero_points;
    return CPIPE_OK;
}

int HostContext::get_cs_role(const cpipe_metadata_t* metadata, const char** out) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out = impl->cs_role.c_str();
    return CPIPE_OK;
}

int HostContext::get_active_area(const cpipe_metadata_t* metadata, std::uint32_t* x,
                                 std::uint32_t* y, std::uint32_t* w, std::uint32_t* h) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || x == nullptr || y == nullptr || w == nullptr || h == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    if (!impl->active_area) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return CPIPE_OK;
    }
    *x = impl->active_area->x;
    *y = impl->active_area->y;
    *w = impl->active_area->width;
    *h = impl->active_area->height;
    return CPIPE_OK;
}

int HostContext::has_applied_step(const cpipe_metadata_t* metadata, const char* step,
                                  int* out_bool) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || step == nullptr || out_bool == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    *out_bool = std::find(impl->applied_steps.begin(), impl->applied_steps.end(), step) !=
                        impl->applied_steps.end()
                    ? 1
                    : 0;
    return CPIPE_OK;
}

int HostContext::list_applied_steps(const cpipe_metadata_t* metadata, std::size_t max,
                                    std::size_t* out_n, const char** out_steps) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out_n == nullptr || (max > 0 && out_steps == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    *out_n = impl->applied_steps.size();
    const auto count = std::min(max, impl->applied_steps.size());
    for (std::size_t i = 0; i < count; ++i) {
        out_steps[i] = impl->applied_steps[i].c_str();
    }
    return CPIPE_OK;
}

int HostContext::get_blob(const cpipe_metadata_t* metadata, const char* key, const void** out_ptr,
                          std::size_t* out_size) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || key == nullptr || out_ptr == nullptr || out_size == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    const cpipe::compute::ByteBlob* blob = nullptr;
    if (std::strcmp(key, "exif_raw") == 0 && impl->exif_blob) {
        blob = impl->exif_blob.get();
    } else if (std::strcmp(key, "xmp_raw") == 0 && impl->xmp_blob) {
        blob = impl->xmp_blob.get();
    } else if (std::strcmp(key, "icc_raw") == 0 && impl->icc_blob) {
        blob = impl->icc_blob.get();
    } else if (const auto found = impl->ext_blobs.find(key); found != impl->ext_blobs.end()) {
        blob = found->second.get();
    }

    if (blob == nullptr) {
        *out_ptr = nullptr;
        *out_size = 0;
        return CPIPE_FAILED;
    }
    *out_ptr = blob->bytes.data();
    *out_size = blob->bytes.size();
    return CPIPE_OK;
}

int HostContext::list_blob_keys(const cpipe_metadata_t* metadata, std::size_t max,
                                std::size_t* out_total, const char** out_keys) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out_total == nullptr || (max > 0 && out_keys == nullptr)) {
        return CPIPE_BAD_INDEX;
    }

    std::size_t total = impl->ext_blobs.size();
    total += impl->exif_blob ? 1U : 0U;
    total += impl->xmp_blob ? 1U : 0U;
    total += impl->icc_blob ? 1U : 0U;
    *out_total = total;

    std::size_t written = 0;
    auto write_key = [&](const char* key) {
        if (written < max) {
            out_keys[written] = key;
        }
        ++written;
    };
    if (impl->exif_blob) {
        write_key("exif_raw");
    }
    if (impl->xmp_blob) {
        write_key("xmp_raw");
    }
    if (impl->icc_blob) {
        write_key("icc_raw");
    }
    for (const auto& [key, blob] : impl->ext_blobs) {
        (void)blob;
        if (written < max) {
            out_keys[written] = key.c_str();
        }
        ++written;
    }
    return CPIPE_OK;
}

int HostContext::get_noise_profile(const cpipe_metadata_t* metadata, std::size_t max_pairs,
                                   std::size_t* out_n, float* out_a, float* out_b) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out_n == nullptr ||
        (max_pairs > 0 && (out_a == nullptr || out_b == nullptr))) {
        return CPIPE_BAD_INDEX;
    }
    if (!impl->calibration) {
        *out_n = 0;
        return CPIPE_OK;
    }
    const auto& noise_profile = impl->calibration->noise_profile;
    *out_n = noise_profile.size();
    const auto count = std::min(max_pairs, noise_profile.size());
    for (std::size_t i = 0; i < count; ++i) {
        out_a[i] = noise_profile[i].first;
        out_b[i] = noise_profile[i].second;
    }
    return CPIPE_OK;
}

int HostContext::get_camera_id(const cpipe_metadata_t* metadata, char* out, std::size_t cap) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || (cap > 0 && out == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    const auto count = cap == 0 ? 0 : std::min(cap - 1, impl->capture.camera_id.size());
    if (count > 0) {
        std::memcpy(out, impl->capture.camera_id.data(), count);
    }
    if (cap > 0) {
        out[count] = '\0';
    }
    return CPIPE_OK;
}

int HostContext::get_physical_camera_id(const cpipe_metadata_t* metadata, char* out,
                                        std::size_t cap) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || (cap > 0 && out == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    const auto count = cap == 0 ? 0 : std::min(cap - 1, impl->capture.physical_camera_id.size());
    if (count > 0) {
        std::memcpy(out, impl->capture.physical_camera_id.data(), count);
    }
    if (cap > 0) {
        out[count] = '\0';
    }
    return CPIPE_OK;
}

int HostContext::get_scales(const cpipe_metadata_t* metadata, std::size_t max, std::size_t* out_n,
                            float* out) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out_n == nullptr || (max > 0 && out == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    *out_n = impl->tensor_quant.scales.size();
    const auto count = std::min(max, impl->tensor_quant.scales.size());
    std::copy_n(impl->tensor_quant.scales.begin(), count, out);
    return CPIPE_OK;
}

int HostContext::get_zero_points(const cpipe_metadata_t* metadata, std::size_t max,
                                 std::size_t* out_n, std::int32_t* out) {
    const auto* impl = metadata_from_handle(metadata);
    if (impl == nullptr || out_n == nullptr || (max > 0 && out == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    *out_n = impl->tensor_quant.zero_points.size();
    const auto count = std::min(max, impl->tensor_quant.zero_points.size());
    std::copy_n(impl->tensor_quant.zero_points.begin(), count, out);
    return CPIPE_OK;
}

int HostContext::share_calibration_from(cpipe_metadata_builder_t* builder, std::size_t input_idx) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr || builder == nullptr || input_idx >= builder->input_metadata.size()) {
        return CPIPE_BAD_INDEX;
    }
    const auto& input = builder->input_metadata[input_idx];
    impl->set_calibration(input ? input->calibration : nullptr);
    return CPIPE_OK;
}

int HostContext::clear_calibration(cpipe_metadata_builder_t* builder) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->clear_calibration();
    return CPIPE_OK;
}

int HostContext::clear_cfa(cpipe_metadata_builder_t* builder) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->clear_cfa();
    return CPIPE_OK;
}

int HostContext::set_as_shot_neutral(cpipe_metadata_builder_t* builder, const float rgb[3]) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr || rgb == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->set_as_shot_neutral({rgb[0], rgb[1], rgb[2]});
    return CPIPE_OK;
}

int HostContext::set_orientation(cpipe_metadata_builder_t* builder, std::uint8_t orient) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->set_orientation(orient);
    return CPIPE_OK;
}

int HostContext::set_cs_role(cpipe_metadata_builder_t* builder, const char* cs_role) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr || cs_role == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->set_cs_role(cs_role);
    return CPIPE_OK;
}

int HostContext::add_applied_step(cpipe_metadata_builder_t* builder, const char* step) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr || step == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->add_applied_step(step);
    return CPIPE_OK;
}

int HostContext::remove_applied_step(cpipe_metadata_builder_t* builder, const char* step) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr || step == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->remove_applied_step(step);
    return CPIPE_OK;
}

int HostContext::set_active_area(cpipe_metadata_builder_t* builder, std::uint32_t x,
                                 std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    impl->set_active_area(cpipe::compute::Rect2u{.x = x, .y = y, .width = w, .height = h});
    return CPIPE_OK;
}

int HostContext::set_tensor_quant(cpipe_metadata_builder_t* builder, int scheme, int has_axis,
                                  std::int8_t axis, const float* scales, std::size_t n_scales,
                                  const std::int32_t* zero_points, std::size_t n_zp) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr || scheme < 0 || scheme > 2 || (n_scales > 0 && scales == nullptr) ||
        (n_zp > 0 && zero_points == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    cpipe::compute::TensorQuant quant;
    quant.scheme = static_cast<cpipe::compute::TensorQuant::Scheme>(scheme);
    if (has_axis != 0) {
        quant.axis = axis;
    }
    if (n_scales > 0) {
        quant.scales.assign(scales, scales + n_scales);
    }
    if (n_zp > 0) {
        quant.zero_points.assign(zero_points, zero_points + n_zp);
    }
    impl->set_tensor_quant(std::move(quant));
    return CPIPE_OK;
}

int HostContext::set_blob(cpipe_metadata_builder_t* builder, const char* key, const void* ptr,
                          std::size_t size) {
    auto* impl = builder_from_handle(builder);
    if (impl == nullptr || key == nullptr || (size > 0 && ptr == nullptr)) {
        return CPIPE_BAD_INDEX;
    }
    if (size == 0) {
        impl->set_blob(key, nullptr);
        return CPIPE_OK;
    }

    auto blob = std::make_shared<cpipe::compute::ByteBlob>();
    const auto* bytes = static_cast<const std::byte*>(ptr);
    blob->bytes.assign(bytes, bytes + size);
    impl->set_blob(key, std::move(blob));
    return CPIPE_OK;
}

int HostContext::merge_from(cpipe_metadata_builder_t*, std::size_t, int policy) {
    return policy == 0 ? CPIPE_OK : CPIPE_UNSUPPORTED;
}

int HostContext::get_param_double(const cpipe_props_t* props, const char* key, double* out) {
    const auto* params = params_from_handle(props);
    if (params == nullptr || key == nullptr || out == nullptr || !params->contains(key) ||
        !params->at(key).is_number()) {
        return CPIPE_NEED_PARAM;
    }
    *out = params->at(key).get<double>();
    return CPIPE_OK;
}

int HostContext::get_param_int(const cpipe_props_t* props, const char* key, std::int64_t* out) {
    const auto* params = params_from_handle(props);
    if (params == nullptr || key == nullptr || out == nullptr || !params->contains(key) ||
        !params->at(key).is_number_integer()) {
        return CPIPE_NEED_PARAM;
    }
    *out = params->at(key).get<std::int64_t>();
    return CPIPE_OK;
}

int HostContext::get_param_bool(const cpipe_props_t* props, const char* key, int* out) {
    const auto* params = params_from_handle(props);
    if (params == nullptr || key == nullptr || out == nullptr || !params->contains(key) ||
        !params->at(key).is_boolean()) {
        return CPIPE_NEED_PARAM;
    }
    *out = params->at(key).get<bool>() ? 1 : 0;
    return CPIPE_OK;
}

int HostContext::get_param_enum(const cpipe_props_t* props, const char* key, const char** out) {
    const auto* params = params_from_handle(props);
    if (params == nullptr || key == nullptr || out == nullptr || !params->contains(key) ||
        !params->at(key).is_string()) {
        return CPIPE_NEED_PARAM;
    }
    *out = params->at(key).get_ref<const std::string&>().c_str();
    return CPIPE_OK;
}

int HostContext::get_param_curve(const cpipe_props_t*, const char*, const float**, const float**,
                                 std::size_t*) {
    return CPIPE_UNSUPPORTED;
}

int HostContext::get_param_color(const cpipe_props_t* props, const char* key, float rgba[4]) {
    const auto* params = params_from_handle(props);
    if (params == nullptr || key == nullptr || rgba == nullptr || !params->contains(key) ||
        !params->at(key).is_array() || params->at(key).size() != 4) {
        return CPIPE_NEED_PARAM;
    }
    const auto& value = params->at(key);
    for (std::size_t i = 0; i < 4; ++i) {
        if (!value[i].is_number()) {
            return CPIPE_NEED_PARAM;
        }
        rgba[i] = value[i].get<float>();
    }
    return CPIPE_OK;
}

int HostContext::submit_halide(cpipe_compute_t* compute_handle, const char* aot_id,
                               const cpipe_buffer_t* const* inputs, std::size_t n_in,
                               cpipe_buffer_t* const* outputs, std::size_t n_out) {
    if (compute_handle == nullptr || aot_id == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    std::vector<std::shared_ptr<compute::IBuffer>> input_buffers;
    input_buffers.reserve(n_in);
    for (std::size_t i = 0; i < n_in; ++i) {
        input_buffers.push_back(buffer_from_handle(inputs[i]));
    }

    std::vector<std::shared_ptr<compute::IBuffer>> output_buffers;
    output_buffers.reserve(n_out);
    for (std::size_t i = 0; i < n_out; ++i) {
        output_buffers.push_back(buffer_from_handle(outputs[i]));
    }

    auto* context = reinterpret_cast<ComputeContext*>(compute_handle);
    return context->submit_halide(std::string_view{aot_id}, input_buffers, output_buffers);
}

int HostContext::submit_halide_with_params(cpipe_compute_t* compute_handle, const char* aot_id,
                                           const cpipe_buffer_t* const* inputs, std::size_t n_in,
                                           cpipe_buffer_t* const* outputs, std::size_t n_out,
                                           const void* param_blob, std::size_t param_blob_size) {
    if (compute_handle == nullptr || aot_id == nullptr ||
        (param_blob == nullptr && param_blob_size != 0)) {
        return CPIPE_BAD_INDEX;
    }

    std::vector<std::shared_ptr<compute::IBuffer>> input_buffers;
    input_buffers.reserve(n_in);
    for (std::size_t i = 0; i < n_in; ++i) {
        input_buffers.push_back(buffer_from_handle(inputs[i]));
    }

    std::vector<std::shared_ptr<compute::IBuffer>> output_buffers;
    output_buffers.reserve(n_out);
    for (std::size_t i = 0; i < n_out; ++i) {
        output_buffers.push_back(buffer_from_handle(outputs[i]));
    }

    const auto* bytes = static_cast<const std::byte*>(param_blob);
    const auto params = bytes == nullptr ? std::span<const std::byte>{}
                                         : std::span<const std::byte>{bytes, param_blob_size};
    auto* context = reinterpret_cast<ComputeContext*>(compute_handle);
    return context->submit_halide_with_params(std::string_view{aot_id}, input_buffers,
                                              output_buffers, params);
}

int HostContext::submit_ocio_processor(cpipe_compute_t*, cpipe_ocio_processor_t* processor,
                                       const cpipe_buffer_t* input_handle,
                                       cpipe_buffer_t* output_handle) {
    if (processor == nullptr || processor->cpu_processor == nullptr || input_handle == nullptr ||
        output_handle == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    auto input = buffer_from_handle(input_handle);
    auto output = buffer_from_handle(output_handle);
    if (input == nullptr || output == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    const auto& layout = input->layout();
    if (layout.kind != compute::BufferKind::Image2D || layout.ndim != 2 ||
        output->layout().dims[0] != layout.dims[0] || output->layout().dims[1] != layout.dims[1] ||
        output->layout().format != layout.format) {
        return CPIPE_BAD_INDEX;
    }

    const auto pixel_count =
        static_cast<std::size_t>(layout.dims[0]) * static_cast<std::size_t>(layout.dims[1]);
    if (layout.format == compute::PixelFormat::R32G32B32A32_SFLOAT) {
        const auto* in =
            static_cast<const float*>(input->lock_cpu(compute::IBuffer::CpuAccess::Read));
        auto* out = static_cast<float*>(output->lock_cpu(compute::IBuffer::CpuAccess::Write));
        std::copy_n(in, pixel_count * 4U, out);
        OCIO::PackedImageDesc desc{out, static_cast<long>(layout.dims[0]),
                                   static_cast<long>(layout.dims[1]), 4};
        processor->cpu_processor->apply(desc);
        output->unlock_cpu();
        output->flush_cpu_writes();
        input->unlock_cpu();
        return CPIPE_OK;
    }
    if (layout.format == compute::PixelFormat::R16G16B16A16_SFLOAT) {
        const auto* in =
            static_cast<const std::uint16_t*>(input->lock_cpu(compute::IBuffer::CpuAccess::Read));
        auto* out =
            static_cast<std::uint16_t*>(output->lock_cpu(compute::IBuffer::CpuAccess::Write));
        std::vector<float> rgba(pixel_count * 4U);
        for (std::size_t i = 0; i < rgba.size(); ++i) {
            rgba[i] = half_to_float(in[i]);
        }
        OCIO::PackedImageDesc desc{rgba.data(), static_cast<long>(layout.dims[0]),
                                   static_cast<long>(layout.dims[1]), 4};
        processor->cpu_processor->apply(desc);
        for (std::size_t i = 0; i < rgba.size(); ++i) {
            out[i] = float_to_half(rgba[i]);
        }
        output->unlock_cpu();
        output->flush_cpu_writes();
        input->unlock_cpu();
        return CPIPE_OK;
    }
    return CPIPE_BAD_PRECISION;
}

int HostContext::submit_slang(cpipe_compute_t*, const char*, const char*,
                              const cpipe_buffer_t* const*, std::size_t, cpipe_buffer_t* const*,
                              std::size_t, const void*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

int HostContext::request_scratch(cpipe_compute_t*, std::uint64_t, int, cpipe_buffer_t**) {
    return CPIPE_UNSUPPORTED;
}

void HostContext::record_marker(cpipe_compute_t*, const char*) {}

int HostContext::submit_inference(cpipe_inference_t*, const char*, const cpipe_buffer_t* const*,
                                  std::size_t, cpipe_buffer_t* const*, std::size_t) {
    return CPIPE_UNSUPPORTED;
}

cpipe_ocio_processor_t* HostContext::get_ocio_processor(cpipe_host_t* self, const char* config_path,
                                                        const char* src_cs, const char* dst_cs) {
    if (self == nullptr || config_path == nullptr || src_cs == nullptr || dst_cs == nullptr) {
        return nullptr;
    }
    try {
        auto* context = reinterpret_cast<HostContext*>(reinterpret_cast<char*>(self) -
                                                       offsetof(HostContext, host_));
        const auto config = OCIO::Config::CreateFromFile(config_path);
        const auto processor = config->getProcessor(src_cs, dst_cs)->getDefaultCPUProcessor();
        auto handle = std::make_unique<cpipe_ocio_processor_t>();
        handle->cpu_processor = processor;
        auto* out = handle.get();
        context->ocio_processors_.push_back(std::move(handle));
        return out;
    } catch (const std::exception& e) {
        log(self, 3, e.what());
        return nullptr;
    }
}

}  // namespace cpipe::runtime
