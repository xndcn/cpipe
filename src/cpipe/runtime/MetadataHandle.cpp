// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/MetadataHandle.hpp>
#include <utility>

namespace cpipe::runtime {

std::unique_ptr<cpipe_metadata_t> make_metadata_handle(
    std::shared_ptr<const compute::BufferMetadata> metadata) {
    auto handle = std::make_unique<cpipe_metadata_t>();
    handle->metadata = std::move(metadata);
    return handle;
}

const compute::BufferMetadata* metadata_from_handle(const cpipe_metadata_t* handle) {
    if (handle == nullptr || !handle->metadata) {
        return nullptr;
    }
    return handle->metadata.get();
}

std::unique_ptr<cpipe_metadata_builder_t> make_metadata_builder_handle(
    std::shared_ptr<const compute::BufferMetadata> base,
    std::vector<std::shared_ptr<const compute::BufferMetadata>> input_metadata) {
    auto handle = std::make_unique<cpipe_metadata_builder_t>();
    handle->builder = compute::MetadataBuilder{std::move(base)};
    handle->input_metadata = std::move(input_metadata);
    return handle;
}

compute::MetadataBuilder* builder_from_handle(cpipe_metadata_builder_t* handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    return &handle->builder;
}

std::shared_ptr<const compute::BufferMetadata> freeze_metadata_builder(
    cpipe_metadata_builder_t* handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    return handle->builder.freeze();
}

}  // namespace cpipe::runtime
