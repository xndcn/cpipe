#include "plugin_loader.h"
#include "plugin_registry.h"

#include <cpipe/node_plugin.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using cpipe::plugin::PluginLoader;
using cpipe::plugin::PluginRegistry;

void host_log(void* ctx, int level, const char* message) {
    (void)ctx;
    (void)level;
    (void)message;
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

cpipe_host_api_t make_host_api() {
    return cpipe_host_api_t{
        .api_version = CPIPE_NODE_PLUGIN_API_VERSION,
        .log = host_log,
        .log_ctx = nullptr,
        .buffer_allocate = host_buffer_allocate,
        .buffer_release = host_buffer_release,
        .buffer_ctx = nullptr,
    };
}

} // namespace

TEST(PluginRegistry, RegisterFindAndIterate_Succeeds) {
    PluginLoader loader{make_host_api()};
    auto plugin = loader.load(MOCK_PLUGIN_PATH);
    ASSERT_TRUE(plugin.has_value()) << plugin.error().message;

    PluginRegistry registry;
    auto register_result = registry.register_plugin(std::move(plugin.value()));
    ASSERT_TRUE(register_result.has_value()) << register_result.error().message;
    EXPECT_EQ(registry.size(), 1u);

    auto found = registry.find("cpipe.test.mock");
    ASSERT_TRUE(found.has_value()) << found.error().message;
    EXPECT_EQ(found->get().plugin_id(), "cpipe.test.mock");

    auto all_plugins = registry.all();
    ASSERT_EQ(all_plugins.size(), 1u);
    EXPECT_EQ(all_plugins.front().get().plugin_id(), "cpipe.test.mock");
}

TEST(PluginRegistry, Register_DuplicatePluginId_KeepsFirstRegistration) {
    PluginLoader loader{make_host_api()};
    auto first_plugin = loader.load(MOCK_PLUGIN_PATH);
    auto second_plugin = loader.load(MOCK_PLUGIN_PATH);
    ASSERT_TRUE(first_plugin.has_value()) << first_plugin.error().message;
    ASSERT_TRUE(second_plugin.has_value()) << second_plugin.error().message;

    PluginRegistry registry;
    ASSERT_TRUE(registry.register_plugin(std::move(first_plugin.value())).has_value());

    auto duplicate = registry.register_plugin(std::move(second_plugin.value()));
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);

    auto found = registry.find("cpipe.test.mock");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->get().path(), std::filesystem::path{MOCK_PLUGIN_PATH});
    EXPECT_EQ(registry.size(), 1u);
}
