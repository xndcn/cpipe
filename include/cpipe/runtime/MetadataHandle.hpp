// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/IBuffer.hpp>
#include <cpipe/core/MetadataBuilder.hpp>
#include <memory>
#include <vector>

struct cpipe_metadata_s {
    std::shared_ptr<const cpipe::compute::BufferMetadata> metadata;
};

struct cpipe_metadata_builder_s {
    cpipe::compute::MetadataBuilder builder;
    std::vector<std::shared_ptr<const cpipe::compute::BufferMetadata>> input_metadata;
};

namespace cpipe::runtime {

[[nodiscard]] std::unique_ptr<cpipe_metadata_t> make_metadata_handle(
    std::shared_ptr<const compute::BufferMetadata> metadata);

[[nodiscard]] const compute::BufferMetadata* metadata_from_handle(const cpipe_metadata_t* handle);

[[nodiscard]] std::unique_ptr<cpipe_metadata_builder_t> make_metadata_builder_handle(
    std::shared_ptr<const compute::BufferMetadata> base,
    std::vector<std::shared_ptr<const compute::BufferMetadata>> input_metadata = {});

[[nodiscard]] compute::MetadataBuilder* builder_from_handle(cpipe_metadata_builder_t* handle);

[[nodiscard]] std::shared_ptr<const compute::BufferMetadata> freeze_metadata_builder(
    cpipe_metadata_builder_t* handle);

}  // namespace cpipe::runtime
