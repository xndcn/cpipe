// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <string_view>

#include "cpipe/core/BufferLayout.hpp"
#include "cpipe/core/BufferUsage.hpp"
#include "cpipe/core/IBuffer.hpp"
#include "cpipe/core/PixelFormat.hpp"
#include "cpipe/runtime/BufferHandle.hpp"
#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/InferenceContext.hpp"
#include "cpipe/runtime/Registry.hpp"
#include "cpipe/runtime/Scheduler.hpp"
#include "cpipe/runtime/TaskExecutor.hpp"

extern "C" int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output);

namespace {

auto read_json(const std::filesystem::path& path) -> nlohmann::json {
    std::ifstream stream{path};
    REQUIRE(stream.is_open());
    return nlohmann::json::parse(stream);
}

auto make_rgba_layout() -> cpipe::compute::BufferLayout {
    cpipe::compute::BufferLayout layout{};
    layout.kind = cpipe::compute::BufferKind::Image2D;
    layout.format = cpipe::compute::PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 16;
    layout.dims[1] = 16;
    return layout;
}

void fill_gradient(cpipe::compute::CpuBuffer& buffer) {
    auto* ptr =
        static_cast<std::uint8_t*>(buffer.lock_cpu(cpipe::compute::IBuffer::CpuAccess::ReadWrite));
    REQUIRE(ptr != nullptr);

    for (std::uint64_t index = 0; index < buffer.size_bytes(); ++index) {
        ptr[index] = static_cast<std::uint8_t>((index * 13U) & 0xFFU);
    }

    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

class NodeInstance final {
public:
    NodeInstance(const cpipe_plugin_desc_t& desc, cpipe_host_t& host) : desc_(&desc), host_(&host) {
        // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
        auto* out_ctx = static_cast<void*>(&instance_);
        REQUIRE(desc_->main_entry(CPIPE_ACTION_CREATE, host_, nullptr, nullptr, nullptr, out_ctx) ==
                CPIPE_OK);
        REQUIRE(instance_ != nullptr);
    }

    ~NodeInstance() {
        if (instance_ != nullptr) {
            (void)desc_->main_entry(CPIPE_ACTION_DESTROY, host_, nullptr, nullptr, instance_,
                                    nullptr);
        }
    }

    NodeInstance(const NodeInstance&) = delete;
    auto operator=(const NodeInstance&) -> NodeInstance& = delete;

    [[nodiscard]] auto native() const noexcept -> cpipe_node_t* {
        return reinterpret_cast<cpipe_node_t*>(instance_);
    }

private:
    const cpipe_plugin_desc_t* desc_ = nullptr;
    cpipe_host_t* host_ = nullptr;
    void* instance_ = nullptr;
};

}  // namespace

TEST_CASE("Passthrough node manifest is registered and schema-valid") {
    cpipe::runtime::Registry registry;
    REQUIRE(registry.load_builtin_nodes() >= 1U);

    const auto* desc = registry.find("com.cpipe.builtin.passthrough");
    REQUIRE(desc != nullptr);
    CHECK(desc->node_version == std::string_view{"1.0.0"});
    REQUIRE(desc->manifest_json != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    const auto source_manifest = read_json(CPIPE_PASSTHROUGH_MANIFEST_SOURCE);
    CHECK(manifest == source_manifest);

    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(read_json(CPIPE_NODE_SCHEMA_SOURCE));
    CHECK_NOTHROW(validator.validate(manifest));
}

TEST_CASE("Passthrough node copies RGBA8 Image2D bytes through the scheduler") {
    using cpipe::compute::BufferUsage;
    using cpipe::compute::CpuBuffer;
    using cpipe::compute::IBuffer;

    cpipe::runtime::Registry registry;
    REQUIRE(registry.load_builtin_nodes() >= 1U);
    const auto* desc = registry.find("com.cpipe.builtin.passthrough");
    REQUIRE(desc != nullptr);

    auto host = cpipe::runtime::make_host();
    NodeInstance node{*desc, host};

    CpuBuffer input(make_rgba_layout(),
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    CpuBuffer output(make_rgba_layout(),
                     BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    fill_gradient(input);

    cpipe::runtime::BufferHandle input_handle{input};
    cpipe::runtime::BufferHandle output_handle{output};
    std::array<const cpipe_buffer_t*, 1> inputs{input_handle.native()};
    std::array<cpipe_buffer_t*, 1> outputs{output_handle.native()};

    const std::array<cpipe::runtime::HalideFilter, 1> filters{
        {{"passthrough_copy", &passthrough_copy}}};
    cpipe::runtime::TaskExecutor executor{2};
    cpipe::runtime::ComputeContext compute{executor, filters};
    cpipe::runtime::InferenceContext inference;
    cpipe::runtime::Scheduler scheduler{executor};

    const std::array<cpipe::runtime::ScheduledNode, 1> nodes{
        {{desc->main_entry, node.native(), nullptr, inputs, outputs, nullptr}}};

    REQUIRE(scheduler.run(nodes, compute, inference) == CPIPE_OK);

    const auto* input_ptr =
        static_cast<const std::uint8_t*>(input.lock_cpu(IBuffer::CpuAccess::Read));
    const auto* output_ptr =
        static_cast<const std::uint8_t*>(output.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(input_ptr != nullptr);
    REQUIRE(output_ptr != nullptr);
    CHECK(std::memcmp(input_ptr, output_ptr, static_cast<std::size_t>(input.size_bytes())) == 0);
    output.unlock_cpu();
    input.unlock_cpu();
}
