// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cpipe/runtime/VulkanImage.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_color_scene_linear_to_display();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferMetadata;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout rgba_layout(PixelFormat format, std::uint32_t width, std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = format;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

std::shared_ptr<BufferMetadata> scene_metadata() {
    auto metadata = std::make_shared<BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"tone.filmic_rgb"};
    return metadata;
}

cpipe_status_t process_display(const cpipe_plugin_desc_t& desc,
                               const std::shared_ptr<IBuffer>& input,
                               const std::shared_ptr<IBuffer>& output, std::string_view target) {
    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle(nlohmann::json{{"target", target}});

    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                            nullptr, &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder =
        cpipe::runtime::make_metadata_builder_handle(input->metadata(), {input->metadata()});
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        params.get(), &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    return status;
}

nlohmann::json pipeline_doc(nlohmann::json params) {
    return {
        {"$schema", "https://schemas.cpipe.dev/pipeline/v0.4.json"},
        {"version", "0.4"},
        {"id", "scene-linear-to-display-schema"},
        {"inputs", nlohmann::json::array({{{"port", "rgb"},
                                           {"kind", "Image2D"},
                                           {"format", "R16G16B16A16_SFLOAT"},
                                           {"width", 1},
                                           {"height", 1}}})},
        {"nodes", nlohmann::json::array({{{"id", "display"},
                                          {"type", "com.cpipe.color.scene_linear_to_display"},
                                          {"params", std::move(params)}}})},
        {"edges", nlohmann::json::array()},
    };
}

}  // namespace

TEST_CASE("color.scene_linear_to_display manifest and schema declare target") {
    cpipe_link_builtin_color_scene_linear_to_display();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.color.scene_linear_to_display");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "OCIO");
    REQUIRE(manifest.at("params").at(0).at("name") == "target");
    REQUIRE(manifest.at("params").at(0).at("enum_values") ==
            nlohmann::json::array({"sRGB", "BT2020-PQ", "DisplayP3", "BT2020-HLG"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"color.scene_linear_to_display"}));

    const auto schema_path =
        std::filesystem::path{CPIPE_SOURCE_DIR} / "schemas" / "pipeline-v0.4.json";
    std::ifstream schema_file{schema_path};
    REQUIRE(schema_file.good());
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schema_file));

    REQUIRE_NOTHROW(validator.validate(pipeline_doc({{"target", "sRGB"}})));
    REQUIRE_NOTHROW(validator.validate(pipeline_doc({{"target", "BT2020-PQ"}})));
    REQUIRE_THROWS(validator.validate(pipeline_doc(nlohmann::json::object())));
    REQUIRE_THROWS(validator.validate(pipeline_doc({{"target", "AdobeRGB"}})));
}

TEST_CASE("color.scene_linear_to_display converts scene-linear Rec2020 to sRGB RGBA8") {
    cpipe_link_builtin_color_scene_linear_to_display();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.color.scene_linear_to_display");
    REQUIRE(desc != nullptr);

    auto input = std::make_shared<CpuBuffer>(
        rgba_layout(PixelFormat::R16G16B16A16_SFLOAT, 2, 1),
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata());
    cpipe::tests::write_rgba16(*input,
                               {{{0.18F, 0.20F, 0.22F, 1.0F}, {0.45F, 0.30F, 0.12F, 0.5F}}});

    auto output = std::make_shared<CpuBuffer>(
        rgba_layout(PixelFormat::R8G8B8A8_UNORM, 2, 1),
        BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    REQUIRE(process_display(*desc, input, output, "sRGB") == CPIPE_OK);

    const auto* out = static_cast<const std::uint8_t*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::any_of(out, out + 6, [](std::uint8_t value) { return value > 0; }));
    REQUIRE(out[3] == 255);
    REQUIRE(out[7] >= 127);
    REQUIRE(out[7] <= 128);
    output->unlock_cpu();

    REQUIRE(output->metadata() != nullptr);
    REQUIRE(output->metadata()->cs_role == "output_srgb");
    REQUIRE(output->metadata()->applied_steps.back() == "color.scene_linear_to_display");
}

TEST_CASE("color.scene_linear_to_display uses OCIO Vulkan when Vulkan images are supplied") {
    const auto* enabled = std::getenv("CPIPE_VULKAN_AVAILABLE");
    if (enabled == nullptr || std::string_view{enabled} != "ON") {
        SUCCEED("CPIPE_VULKAN_AVAILABLE is not ON; skipping scene-linear Vulkan display check");
        return;
    }

    cpipe_link_builtin_color_scene_linear_to_display();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.color.scene_linear_to_display");
    REQUIRE(desc != nullptr);

    const auto created = cpipe::runtime::VulkanDevicePlane::create();
    REQUIRE(created.status == cpipe::compute::StatusCode::Ok);
    REQUIRE(created.plane != nullptr);

    auto input = std::make_shared<cpipe::runtime::VulkanImage>(
        created.plane, rgba_layout(PixelFormat::R16G16B16A16_SFLOAT, 2, 1),
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite | BufferUsage::GpuStorage,
        "scene_linear_rec2020");
    input->set_metadata(scene_metadata());

    auto* in = static_cast<std::uint16_t*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    const std::uint16_t values[] = {0x31c7, 0x3266, 0x330a, 0x3c00, 0x3733, 0x34cd, 0x2fb8, 0x3800};
    std::copy(std::begin(values), std::end(values), in);
    input->unlock_cpu();
    input->flush_cpu_writes();

    auto output = std::make_shared<cpipe::runtime::VulkanImage>(
        created.plane, rgba_layout(PixelFormat::R8G8B8A8_UNORM, 2, 1),
        BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite |
            BufferUsage::GpuStorage,
        "output_srgb");

    REQUIRE(process_display(*desc, input, output, "sRGB") == CPIPE_OK);

    const auto* out = static_cast<const std::uint8_t*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::any_of(out, out + 6, [](std::uint8_t value) { return value > 0; }));
    REQUIRE(out[3] == 255);
    REQUIRE(out[7] >= 127);
    REQUIRE(out[7] <= 128);
    output->unlock_cpu();

    REQUIRE(output->metadata() != nullptr);
    REQUIRE(output->metadata()->cs_role == "output_srgb");
    REQUIRE(output->metadata()->applied_steps.back() == "color.scene_linear_to_display");
}

TEST_CASE("color.scene_linear_to_display converts scene-linear Rec2020 to top-aligned PQ RGBA16") {
    cpipe_link_builtin_color_scene_linear_to_display();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.color.scene_linear_to_display");
    REQUIRE(desc != nullptr);

    auto input = std::make_shared<CpuBuffer>(
        rgba_layout(PixelFormat::R16G16B16A16_SFLOAT, 2, 1),
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata());
    cpipe::tests::write_rgba16(*input,
                               {{{0.05F, 0.18F, 0.50F, 1.0F}, {0.90F, 0.70F, 0.30F, 0.5F}}});

    auto output = std::make_shared<CpuBuffer>(
        rgba_layout(PixelFormat::R16G16B16A16_UNORM, 2, 1),
        BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    REQUIRE(process_display(*desc, input, output, "BT2020-PQ") == CPIPE_OK);

    const auto* out = static_cast<const std::uint16_t*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    for (std::size_t i = 0; i < 8; ++i) {
        REQUIRE((out[i] & 0x003fU) == 0);
    }
    REQUIRE(out[3] == (1023U << 6U));
    REQUIRE(out[7] == (512U << 6U));
    output->unlock_cpu();

    REQUIRE(output->metadata() != nullptr);
    REQUIRE(output->metadata()->cs_role == "output_pq_rec2020");
    REQUIRE(output->metadata()->applied_steps.back() == "color.scene_linear_to_display");
}

TEST_CASE("color.scene_linear_to_display rejects reserved P2 display targets") {
    cpipe_link_builtin_color_scene_linear_to_display();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.color.scene_linear_to_display");
    REQUIRE(desc != nullptr);

    auto input = std::make_shared<CpuBuffer>(
        rgba_layout(PixelFormat::R16G16B16A16_SFLOAT, 1, 1),
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata());
    cpipe::tests::write_rgba16(*input, {{{0.18F, 0.18F, 0.18F, 1.0F}}});

    auto output = std::make_shared<CpuBuffer>(
        rgba_layout(PixelFormat::R8G8B8A8_UNORM, 1, 1),
        BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    REQUIRE(process_display(*desc, input, output, "DisplayP3") == CPIPE_UNSUPPORTED);
    REQUIRE(process_display(*desc, input, output, "BT2020-HLG") == CPIPE_UNSUPPORTED);
}
