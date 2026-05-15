// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/color/HeifReader.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_output_heif_hdr_pq();

namespace {

cpipe::tests::BufferLayout rgba16_unorm_layout(std::uint32_t width, std::uint32_t height) {
    cpipe::tests::BufferLayout layout{};
    layout.kind = cpipe::tests::BufferKind::Image2D;
    layout.format = cpipe::tests::PixelFormat::R16G16B16A16_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

void process_output_node(const cpipe_plugin_desc_t& desc,
                         const std::shared_ptr<cpipe::tests::CpuBuffer>& input,
                         const std::filesystem::path& path) {
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle(nlohmann::json{{"path", path.string()}});
    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                            nullptr, &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_process_ctx process{
        .compute = nullptr,
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = nullptr,
        .n_out = 0,
        .out_metadata = nullptr,
    };

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        params.get(), &process, nullptr));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    REQUIRE(status == CPIPE_OK);
}

bool icc_has_tag(std::span<const std::uint8_t> profile, const char* tag) {
    if (profile.size() < 132U) {
        return false;
    }
    const auto read_u32 = [&](std::size_t offset) {
        return (static_cast<std::uint32_t>(profile[offset + 0U]) << 24U) |
               (static_cast<std::uint32_t>(profile[offset + 1U]) << 16U) |
               (static_cast<std::uint32_t>(profile[offset + 2U]) << 8U) |
               static_cast<std::uint32_t>(profile[offset + 3U]);
    };
    const auto sig =
        (static_cast<std::uint32_t>(tag[0]) << 24U) | (static_cast<std::uint32_t>(tag[1]) << 16U) |
        (static_cast<std::uint32_t>(tag[2]) << 8U) | static_cast<std::uint32_t>(tag[3]);
    const auto count = read_u32(128U);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto record = 132U + (static_cast<std::size_t>(i) * 12U);
        if (record + 12U <= profile.size() && read_u32(record) == sig) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("output.heif_hdr_pq writes a 10-bit BT.2020 PQ HEIF with ICC, CICP, mdcv, and clli") {
    cpipe_link_builtin_output_heif_hdr_pq();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.output.heif_hdr_pq");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("caps").at("precision") ==
            nlohmann::json::array({"u16"}));
    REQUIRE(manifest.at("color").at("input_role") == "output_pq_rec2020");

    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 64;
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "output_pq_rec2020";
    metadata->applied_steps = {"color.scene_linear_to_display"};

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(rgba16_unorm_layout(width, height),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);

    auto* pixels =
        static_cast<std::uint16_t*>(input->lock_cpu(cpipe::tests::IBuffer::CpuAccess::Write));
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto base = (static_cast<std::size_t>(y) * width + x) * 4U;
            const auto code = static_cast<std::uint16_t>(
                128U + (((x + y) * (900U - 128U)) / ((width - 1U) + (height - 1U))));
            pixels[base + 0U] = static_cast<std::uint16_t>(code << 6U);
            pixels[base + 1U] = static_cast<std::uint16_t>((code / 2U + 128U) << 6U);
            pixels[base + 2U] = static_cast<std::uint16_t>((1023U - code / 3U) << 6U);
            pixels[base + 3U] = 1023U << 6U;
        }
    }
    input->unlock_cpu();
    input->flush_cpu_writes();

    const auto path = std::filesystem::temp_directory_path() / "out-hdr.heif";
    std::filesystem::remove(path);
    process_output_node(*desc, input, path);
    REQUIRE(std::filesystem::file_size(path) > 0);

    cpipe::color::HeifInfo info;
    std::string error;
    const auto read_status = cpipe::color::read_heif_sdr(path, &info, &error);
    INFO(error);
    REQUIRE(read_status == CPIPE_OK);
    REQUIRE(info.width == width);
    REQUIRE(info.height == height);
    REQUIRE(info.luma_bits_per_pixel == 10);
    REQUIRE(info.nclx_color_primaries == 9);
    REQUIRE(info.nclx_transfer_characteristics == 16);
    REQUIRE(info.nclx_matrix_coefficients == 9);
    REQUIRE(info.nclx_full_range);
    REQUIRE(icc_has_tag(info.icc_profile, "cicp"));
    REQUIRE(icc_has_tag(info.icc_profile, "A2B0"));
    REQUIRE(info.has_mastering_display);
    REQUIRE(info.has_content_light_level);
    REQUIRE(info.max_content_light_level > 0);
    REQUIRE(info.max_pic_average_light_level > 0);
}
