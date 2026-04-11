#include "plugin_loader.h"

#include <cpipe/buffer.h>
#include <cpipe/node_plugin.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint8_t kInputPatternModulus = 251u;

using cpipe::plugin::PluginLoader;
using cpipe::platform::BufferPool;
using cpipe::platform::BufferDescriptor;

struct HostContext {
    std::vector<std::string> messages;
};

void host_log(void* ctx, int level, const char* message) {
    (void)level;

    auto* host = static_cast<HostContext*>(ctx);
    if (host != nullptr) {
        host->messages.emplace_back(message != nullptr ? message : "");
    }
}

cpipe_status_t host_buffer_allocate(
    void* ctx,
    cpipe_buffer_t* buf,
    uint32_t width,
    uint32_t height,
    cpipe_pixel_format_t format) {
    (void)ctx;
    (void)width;
    (void)height;
    (void)format;

    if (buf != nullptr) {
        *buf = {};
    }
    return CPIPE_STATUS_ERROR_UNSUPPORTED;
}

void host_buffer_release(void* ctx, cpipe_buffer_t* buf) {
    (void)ctx;
    (void)buf;
}

cpipe_host_api_t make_host_api(HostContext& context) {
    return cpipe_host_api_t{
        .api_version = CPIPE_NODE_PLUGIN_API_VERSION,
        .log = host_log,
        .log_ctx = &context,
        .buffer_allocate = host_buffer_allocate,
        .buffer_release = host_buffer_release,
        .buffer_ctx = &context,
    };
}

} // namespace

TEST(PluginLoader, Load_ValidPlugin_ResolvesSymbolsAndMetadata) {
    HostContext context;
    PluginLoader loader{make_host_api(context)};

    auto plugin = loader.load(MOCK_PLUGIN_PATH);
    ASSERT_TRUE(plugin.has_value()) << plugin.error().message;

    EXPECT_TRUE(plugin->valid());
    EXPECT_EQ(plugin->plugin_id(), "cpipe.test.mock");
    EXPECT_EQ(plugin->info().abi_version, CPIPE_NODE_PLUGIN_API_VERSION);
    EXPECT_EQ(plugin->info().input_count, 1u);
    EXPECT_EQ(plugin->info().output_count, 1u);
    EXPECT_NE(plugin->plugin_shutdown, nullptr);
    EXPECT_NE(plugin->node_create, nullptr);
    EXPECT_NE(plugin->node_destroy, nullptr);
    EXPECT_NE(plugin->node_get_info, nullptr);
    EXPECT_NE(plugin->node_get_parameter_schema, nullptr);
    EXPECT_NE(plugin->node_process, nullptr);
    ASSERT_FALSE(context.messages.empty());
    EXPECT_EQ(context.messages.front(), "mock_plugin initialized");
}

TEST(PluginLoader, Load_AbiMismatchPlugin_ReturnsError) {
    HostContext context;
    PluginLoader loader{make_host_api(context)};

    auto plugin = loader.load(MOCK_PLUGIN_ABI_MISMATCH_PATH);
    ASSERT_FALSE(plugin.has_value());
    EXPECT_EQ(plugin.error().code, CPIPE_STATUS_ERROR_ABI_MISMATCH);
}

TEST(PluginLoader, Load_MissingSymbolsPlugin_ReturnsError) {
    HostContext context;
    PluginLoader loader{make_host_api(context)};

    auto plugin = loader.load(MOCK_PLUGIN_MISSING_SYMBOLS_PATH);
    ASSERT_FALSE(plugin.has_value());
    EXPECT_EQ(plugin.error().code, CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED);
}

TEST(PluginLoader, Load_InvalidSharedLibrary_ReturnsError) {
    HostContext context;
    PluginLoader loader{make_host_api(context)};

    const auto invalid_path = std::filesystem::temp_directory_path() / "cpipe-invalid-plugin.so";
    {
        std::ofstream file{invalid_path};
        ASSERT_TRUE(file.is_open());
        file << "not a shared library";
    }

    auto plugin = loader.load(invalid_path);
    std::filesystem::remove(invalid_path);

    ASSERT_FALSE(plugin.has_value());
    EXPECT_EQ(plugin.error().code, CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED);
}

TEST(PluginLoader, LoadDirectory_LoadsOnlyValidPlugins) {
    HostContext context;
    PluginLoader loader{make_host_api(context)};

    auto plugins = loader.load_directory(MOCK_PLUGIN_DIRECTORY);
    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins.front().plugin_id(), "cpipe.test.mock");
}

TEST(PluginLoader, FullLifecycle_CreateProcessDestroy_Succeeds) {
    HostContext context;
    PluginLoader loader{make_host_api(context)};

    auto plugin_result = loader.load(MOCK_PLUGIN_PATH);
    ASSERT_TRUE(plugin_result.has_value()) << plugin_result.error().message;
    auto plugin = std::move(plugin_result.value());

    cpipe_node_t* node = plugin.node_create("{}");
    ASSERT_NE(node, nullptr);
    EXPECT_STREQ(plugin.node_get_parameter_schema(node), "{}");
    EXPECT_STREQ(plugin.node_get_info(node)->plugin_id, "cpipe.test.mock");

    BufferPool pool;
    auto desc = BufferDescriptor::create(4, 2, CPIPE_PIXEL_FORMAT_RGBA_8).value();
    auto input = pool.allocate(desc).value();
    auto output = pool.allocate(desc).value();

    auto* input_bytes = static_cast<std::uint8_t*>(input.data());
    auto* output_bytes = static_cast<std::uint8_t*>(output.data());
    for (std::size_t i = 0; i < desc.size(); ++i) {
        input_bytes[i] = static_cast<std::uint8_t>(i % kInputPatternModulus);
        output_bytes[i] = 0u;
    }

    const cpipe_buffer_t input_view = input.to_c();
    cpipe_buffer_t output_view = output.to_c();
    const cpipe_buffer_t* inputs[] = {&input_view};
    cpipe_buffer_t* outputs[] = {&output_view};

    const auto status = plugin.node_process(node, inputs, 1, outputs, 1, "{}");
    EXPECT_EQ(status, CPIPE_STATUS_OK);
    EXPECT_TRUE(std::equal(input_bytes, input_bytes + desc.size(), output_bytes));

    plugin.node_destroy(node);
}
