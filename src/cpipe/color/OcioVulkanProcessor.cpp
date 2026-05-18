// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenColorIO/OpenColorIO.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cpipe/color/OcioVulkanProcessor.hpp>
#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/Sync.hpp>
#include <cpipe/runtime/Trace.hpp>
#include <cpipe/runtime/VulkanCommandBuffer.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cpipe/runtime/VulkanImage.hpp>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

namespace cpipe::color {
namespace {

using cpipe::compute::PixelFormat;

constexpr const char* kOcioFunctionName = "ocio_transform";
constexpr std::uint32_t kWorkgroupSize = 8;

void check_vk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error{operation};
    }
}

const char* storage_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8G8B8A8_UNORM:
            return "rgba8";
        case PixelFormat::R16G16B16A16_UNORM:
            return "rgba16";
        case PixelFormat::R16G16B16A16_SFLOAT:
            return "rgba16f";
        case PixelFormat::R32G32B32A32_SFLOAT:
            return "rgba32f";
        default:
            throw std::runtime_error{"unsupported OCIO Vulkan output format"};
    }
}

std::string strip_version_lines(std::string_view shader_text) {
    std::string out;
    std::size_t line_begin = 0;
    while (line_begin < shader_text.size()) {
        const auto line_end = shader_text.find('\n', line_begin);
        const auto count = line_end == std::string_view::npos ? shader_text.size() - line_begin
                                                              : line_end - line_begin + 1;
        const auto line = shader_text.substr(line_begin, count);
        if (!line.starts_with("#version")) {
            out.append(line);
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        line_begin = line_end + 1;
    }
    return out;
}

std::string make_compute_shader(std::string_view ocio_shader, PixelFormat output_format) {
    std::string source;
    source.reserve(ocio_shader.size() + 1024U);
    source += "#version 460\n";
    source += "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n";
    source += "layout(set = 0, binding = 0, rgba16f) uniform readonly image2D cpipe_input;\n";
    source += "layout(set = 0, binding = 1, ";
    source += storage_format(output_format);
    source += ") uniform writeonly image2D cpipe_output;\n";
    source += strip_version_lines(ocio_shader);
    source += "\nvoid main() {\n";
    source += "  ivec2 xy = ivec2(gl_GlobalInvocationID.xy);\n";
    source += "  ivec2 size = imageSize(cpipe_input);\n";
    source += "  if (xy.x >= size.x || xy.y >= size.y) { return; }\n";
    source += "  vec4 pixel = imageLoad(cpipe_input, xy);\n";
    source += "  pixel = ";
    source += kOcioFunctionName;
    source += "(pixel);\n";
    source += "  imageStore(cpipe_output, xy, pixel);\n";
    source += "}\n";
    return source;
}

std::vector<std::uint32_t> compile_compute_shader(const std::string& source) {
    static const bool initialized = [] {
        glslang::InitializeProcess();
        return true;
    }();
    (void)initialized;

    const char* strings[] = {source.c_str()};
    glslang::TShader shader{EShLangCompute};
    shader.setStrings(strings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 460);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 460, false, messages)) {
        throw std::runtime_error{std::string{"glslang parse failed: "} + shader.getInfoLog()};
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        throw std::runtime_error{std::string{"glslang link failed: "} + program.getInfoLog()};
    }

    std::vector<std::uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spirv);
    return spirv;
}

struct ImageView {
    ImageView(const runtime::VulkanDevicePlane& plane, runtime::VulkanImage& image)
        : device(plane.device()) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image.vk_image();
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = image.vk_format();
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        check_vk(vkCreateImageView(device, &info, nullptr, &view), "vkCreateImageView");
    }

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    ~ImageView() {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }

    VkDevice device{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};

struct DescriptorPool {
    explicit DescriptorPool(const runtime::VulkanDevicePlane& plane) : device(plane.device()) {
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_size.descriptorCount = 2;

        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = 1;
        info.poolSizeCount = 1;
        info.pPoolSizes = &pool_size;
        check_vk(vkCreateDescriptorPool(device, &info, nullptr, &pool), "vkCreateDescriptorPool");
    }

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    ~DescriptorPool() {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, pool, nullptr);
        }
    }

    VkDevice device{VK_NULL_HANDLE};
    VkDescriptorPool pool{VK_NULL_HANDLE};
};

}  // namespace

class OcioVulkanProcessor::Impl final {
public:
    Impl(std::filesystem::path config_path, std::string src_cs, std::string dst_cs)
        : config_path_(std::move(config_path)),
          src_cs_(std::move(src_cs)),
          dst_cs_(std::move(dst_cs)) {
        const auto config = OCIO::Config::CreateFromFile(config_path_.string().c_str());
        const auto processor =
            config->getProcessor(src_cs_.c_str(), dst_cs_.c_str())->getDefaultGPUProcessor();
        auto shader_desc = OCIO::GpuShaderDesc::CreateShaderDesc();
        shader_desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_VK_4_6);
        shader_desc->setFunctionName(kOcioFunctionName);
        shader_desc->setPixelName("pixel");
        shader_desc->setDescriptorSetIndex(0, 2);
        processor->extractGpuShaderInfo(shader_desc);
        if (shader_desc->getNumUniforms() != 0 || shader_desc->getNumTextures() != 0 ||
            shader_desc->getNum3DTextures() != 0) {
            throw std::runtime_error{"OCIO Vulkan LUT/uniform upload is not needed by v0.2 config"};
        }
        ocio_shader_text_ = shader_desc->getShaderText();
    }

    ~Impl() {
        for (auto& entry : pipelines_) {
            entry.second.destroy();
        }
    }

    [[nodiscard]] cpipe_status_t compute_pass(
        const std::shared_ptr<runtime::VulkanDevicePlane>& plane, runtime::VulkanImage& input,
        runtime::VulkanImage& output) {
        CPIPE_TRACE_SCOPE("OcioVulkanProcessor::compute_pass");
        if (plane == nullptr || input.plane() != plane || output.plane() != plane) {
            return CPIPE_BAD_INDEX;
        }
        const auto& input_layout = input.layout();
        const auto& output_layout = output.layout();
        if (input_layout.format != PixelFormat::R16G16B16A16_SFLOAT ||
            input_layout.dims[0] != output_layout.dims[0] ||
            input_layout.dims[1] != output_layout.dims[1]) {
            return CPIPE_BAD_PRECISION;
        }

        try {
            auto& pipeline = pipeline_for(*plane, output_layout.format);
            input.transition_to_general();
            output.transition_to_general();

            ImageView input_view{*plane, input};
            ImageView output_view{*plane, output};
            DescriptorPool descriptor_pool{*plane};
            const VkDescriptorSet descriptor_set =
                allocate_descriptor_set(*plane, descriptor_pool.pool, pipeline.set_layout);
            update_descriptor_set(*plane, descriptor_set, input_view.view, output_view.view);

            runtime::VulkanTimelineSemaphore timeline{*plane, 0};
            runtime::VulkanCommandBuffer command_buffer{plane};
            command_buffer.begin();
            command_buffer.bind_compute_pipeline(pipeline.pipeline);
            command_buffer.bind_descriptor_set(pipeline.pipeline_layout, descriptor_set);
            command_buffer.dispatch((output_layout.dims[0] + kWorkgroupSize - 1U) / kWorkgroupSize,
                                    (output_layout.dims[1] + kWorkgroupSize - 1U) / kWorkgroupSize,
                                    1);
            command_buffer.end();
            command_buffer.submit(timeline, 1);
            if (!plane->wait_timeline(timeline, 1, std::chrono::seconds{2})) {
                return CPIPE_FAILED;
            }
            return CPIPE_OK;
        } catch (const std::exception&) {
            return CPIPE_FAILED;
        }
    }

private:
    struct PipelineResources {
        void destroy() noexcept {
            if (device != VK_NULL_HANDLE) {
                if (pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, pipeline, nullptr);
                }
                if (shader != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, shader, nullptr);
                }
                if (pipeline_layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                }
                if (set_layout != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
                }
            }
            pipeline = VK_NULL_HANDLE;
            shader = VK_NULL_HANDLE;
            pipeline_layout = VK_NULL_HANDLE;
            set_layout = VK_NULL_HANDLE;
            device = VK_NULL_HANDLE;
        }

        VkDevice device{VK_NULL_HANDLE};
        VkDescriptorSetLayout set_layout{VK_NULL_HANDLE};
        VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
        VkShaderModule shader{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
    };

    PipelineResources& pipeline_for(const runtime::VulkanDevicePlane& plane,
                                    PixelFormat output_format) {
        std::lock_guard lock{mutex_};
        auto& pipeline = pipelines_[output_format];
        if (pipeline.pipeline != VK_NULL_HANDLE) {
            if (pipeline.device == plane.device()) {
                return pipeline;
            }
            pipeline.destroy();
        }

        pipeline.device = plane.device();
        create_descriptor_set_layout(plane, pipeline);
        create_pipeline_layout(plane, pipeline);
        create_shader_and_pipeline(plane, output_format, pipeline);
        return pipeline;
    }

    static void create_descriptor_set_layout(const runtime::VulkanDevicePlane& plane,
                                             PipelineResources& pipeline) {
        VkDescriptorSetLayoutBinding bindings[2]{};
        for (std::uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 2;
        info.pBindings = bindings;
        check_vk(vkCreateDescriptorSetLayout(plane.device(), &info, nullptr, &pipeline.set_layout),
                 "vkCreateDescriptorSetLayout");
    }

    static void create_pipeline_layout(const runtime::VulkanDevicePlane& plane,
                                       PipelineResources& pipeline) {
        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = 1;
        info.pSetLayouts = &pipeline.set_layout;
        check_vk(vkCreatePipelineLayout(plane.device(), &info, nullptr, &pipeline.pipeline_layout),
                 "vkCreatePipelineLayout");
    }

    void create_shader_and_pipeline(const runtime::VulkanDevicePlane& plane,
                                    PixelFormat output_format, PipelineResources& pipeline) const {
        const auto source = make_compute_shader(ocio_shader_text_, output_format);
        const auto spirv = compile_compute_shader(source);

        VkShaderModuleCreateInfo shader_info{};
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = spirv.size() * sizeof(std::uint32_t);
        shader_info.pCode = spirv.data();
        check_vk(vkCreateShaderModule(plane.device(), &shader_info, nullptr, &pipeline.shader),
                 "vkCreateShaderModule");

        VkPipelineShaderStageCreateInfo stage_info{};
        stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage_info.module = pipeline.shader;
        stage_info.pName = "main";

        VkComputePipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = stage_info;
        pipeline_info.layout = pipeline.pipeline_layout;
        check_vk(vkCreateComputePipelines(plane.device(), VK_NULL_HANDLE, 1, &pipeline_info,
                                          nullptr, &pipeline.pipeline),
                 "vkCreateComputePipelines");
    }

    static VkDescriptorSet allocate_descriptor_set(const runtime::VulkanDevicePlane& plane,
                                                   VkDescriptorPool descriptor_pool,
                                                   VkDescriptorSetLayout set_layout) {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = descriptor_pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &set_layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        check_vk(vkAllocateDescriptorSets(plane.device(), &info, &set), "vkAllocateDescriptorSets");
        return set;
    }

    static void update_descriptor_set(const runtime::VulkanDevicePlane& plane, VkDescriptorSet set,
                                      VkImageView input_view, VkImageView output_view) {
        VkDescriptorImageInfo image_infos[2]{};
        image_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_infos[0].imageView = input_view;
        image_infos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_infos[1].imageView = output_view;

        VkWriteDescriptorSet writes[2]{};
        for (std::uint32_t i = 0; i < 2; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo = &image_infos[i];
        }
        vkUpdateDescriptorSets(plane.device(), 2, writes, 0, nullptr);
    }

    std::filesystem::path config_path_;
    std::string src_cs_;
    std::string dst_cs_;
    std::string ocio_shader_text_;
    std::mutex mutex_;
    std::unordered_map<PixelFormat, PipelineResources> pipelines_;
};

OcioVulkanProcessor::OcioVulkanProcessor(std::filesystem::path config_path, std::string src_cs,
                                         std::string dst_cs)
    : impl_(std::make_unique<Impl>(std::move(config_path), std::move(src_cs), std::move(dst_cs))) {}

OcioVulkanProcessor::~OcioVulkanProcessor() = default;

cpipe_status_t OcioVulkanProcessor::compute_pass(
    const std::shared_ptr<runtime::VulkanDevicePlane>& plane, runtime::VulkanImage& input,
    runtime::VulkanImage& output) {
    return impl_->compute_pass(plane, input, output);
}

}  // namespace cpipe::color
