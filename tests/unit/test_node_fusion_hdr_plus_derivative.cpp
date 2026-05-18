// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

void cpipe_link_builtin_fusion_hdr_plus_derivative();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout raw16_layout(const std::uint32_t width, const std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R16_UINT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

std::shared_ptr<CpuBuffer> make_raw16(const std::uint32_t width, const std::uint32_t height,
                                      BufferUsage usage) {
    return std::make_shared<CpuBuffer>(raw16_layout(width, height), usage);
}

void write_raw16(CpuBuffer& buffer, const std::vector<std::uint16_t>& pixels) {
    auto* out = static_cast<std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    std::copy(pixels.begin(), pixels.end(), out);
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

std::vector<std::uint16_t> read_raw16(CpuBuffer& buffer, const std::size_t count) {
    std::vector<std::uint16_t> pixels(count);
    const auto* in = static_cast<const std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    std::copy(in, in + count, pixels.begin());
    buffer.unlock_cpu();
    return pixels;
}

std::filesystem::path write_fusion_pipeline() {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_hdr_plus_stub_pipeline.json";
    std::ofstream out{path};
    out << R"({
  "$schema":"https://schemas.cpipe.dev/pipeline/v0.4.json",
  "version":"0.4",
  "id":"hdr-plus-stub-pipeline",
  "inputs":[{"port":"raw","kind":"Image2D","format":"R16_UINT","width":4,"height":2}],
  "nodes":[
    {"id":"fuse","type":"com.cpipe.fusion.hdr_plus_derivative","params":{}}
  ],
  "edges":[]
})";
    return path;
}

std::filesystem::path write_raw16_file(const std::vector<std::uint16_t>& pixels) {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_hdr_plus_stub_input.raw16";
    std::ofstream out{path, std::ios::binary};
    for (const auto value : pixels) {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
    return path;
}

std::vector<std::uint16_t> read_raw16_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::vector<std::uint16_t> pixels;
    std::uint16_t value = 0;
    while (in.read(reinterpret_cast<char*>(&value), sizeof(value))) {
        pixels.push_back(value);
    }
    return pixels;
}

}  // namespace

TEST_CASE("fusion.hdr_plus_derivative is registered as a two-input passthrough stub") {
    cpipe_link_builtin_fusion_hdr_plus_derivative();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    const auto& descriptors = registry.descriptors();
    REQUIRE(std::ranges::any_of(descriptors, [](const cpipe_plugin_desc_t* desc) {
        return desc != nullptr && desc->node_id != nullptr &&
               std::string_view{desc->node_id} == "com.cpipe.fusion.hdr_plus_derivative";
    }));

    const auto* desc = registry.find("com.cpipe.fusion.hdr_plus_derivative");
    REQUIRE(desc != nullptr);
    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("name") == "ref");
    REQUIRE(manifest.at("ports").at(1).at("name") == "frame");
    REQUIRE(manifest.at("ports").at(2).at("name") == "fused");
    REQUIRE(manifest.at("ports").at(2).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"burst_fusion_stub"}));

    const std::vector<std::uint16_t> ref_pixels{10, 20, 30, 40, 50, 60, 70, 80};
    const std::vector<std::uint16_t> frame_pixels{90, 91, 92, 93, 94, 95, 96, 97};

    auto metadata = std::make_shared<cpipe::compute::BufferMetadata>();
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization"};

    auto ref = make_raw16(4, 2, BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    ref->set_metadata(metadata);
    write_raw16(*ref, ref_pixels);
    auto frame =
        make_raw16(4, 2, BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    frame->set_metadata(metadata);
    write_raw16(*frame, frame_pixels);
    auto output =
        make_raw16(4, 2, BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    cpipe::runtime::HostContext host_context;
    cpipe::runtime::ComputeContext compute;
    void* instance = nullptr;
    REQUIRE(desc->main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                             &instance) == CPIPE_OK);

    auto ref_handle = cpipe::runtime::make_buffer_handle(ref);
    auto frame_handle = cpipe::runtime::make_buffer_handle(frame);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder = cpipe::runtime::make_metadata_builder_handle(
        ref->metadata(), {ref->metadata(), frame->metadata()});
    const cpipe_buffer_t* inputs[] = {ref_handle.get(), frame_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 2,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    REQUIRE(desc->main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                             reinterpret_cast<cpipe_node_t*>(instance), nullptr, &process,
                             nullptr) == CPIPE_OK);
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc->main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                             nullptr) == CPIPE_OK);

    REQUIRE(read_raw16(*output, ref_pixels.size()) == ref_pixels);
    REQUIRE(output->metadata() != nullptr);
    REQUIRE(output->metadata()->applied_steps ==
            std::vector<std::string>{"linearization", "burst_fusion_stub"});
}

TEST_CASE("Pipeline run feeds the HDR+ placeholder input ports from the same source buffer") {
    cpipe_link_builtin_fusion_hdr_plus_derivative();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(write_fusion_pipeline(), registry, &pipeline, &error) ==
            CPIPE_OK);

    const std::vector<std::uint16_t> pixels{1, 3, 5, 7, 9, 11, 13, 15};
    const auto input_path = write_raw16_file(pixels);
    const auto output_path =
        std::filesystem::temp_directory_path() / "cpipe_hdr_plus_stub_output.raw16";
    std::filesystem::remove(output_path);

    REQUIRE(pipeline.run_file(input_path, output_path, &error) == CPIPE_OK);
    REQUIRE(read_raw16_file(output_path) == pixels);
}
