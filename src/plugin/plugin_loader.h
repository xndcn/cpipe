#pragma once

#include <cpipe/error.h>
#include <cpipe/node_plugin.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cpipe::plugin {

struct PluginHandle {
    using plugin_init_fn = cpipe_status_t (*)(const cpipe_host_api_t* host);
    using plugin_shutdown_fn = void (*)(void);
    using node_create_fn = cpipe_node_t* (*)(const char* config_json);
    using node_destroy_fn = void (*)(cpipe_node_t* node);
    using node_get_info_fn = const cpipe_node_info_t* (*)(const cpipe_node_t* node);
    using node_get_parameter_schema_fn = const char* (*)(const cpipe_node_t* node);
    using node_process_fn = cpipe_status_t (*)(
        cpipe_node_t* node,
        const cpipe_buffer_t* const* inputs, uint32_t input_count,
        cpipe_buffer_t* const* outputs, uint32_t output_count,
        const char* params_json);

    PluginHandle() = default;
    ~PluginHandle();

    PluginHandle(PluginHandle&& other) noexcept;
    PluginHandle& operator=(PluginHandle&& other) noexcept;

    PluginHandle(const PluginHandle&) = delete;
    PluginHandle& operator=(const PluginHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string_view plugin_id() const noexcept;
    [[nodiscard]] const cpipe_node_info_t& info() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    plugin_init_fn plugin_init{nullptr};
    plugin_shutdown_fn plugin_shutdown{nullptr};
    node_create_fn node_create{nullptr};
    node_destroy_fn node_destroy{nullptr};
    node_get_info_fn node_get_info{nullptr};
    node_get_parameter_schema_fn node_get_parameter_schema{nullptr};
    node_process_fn node_process{nullptr};

private:
    friend class PluginLoader;

    void release() noexcept;

    std::filesystem::path path_;
    std::string plugin_id_;
    std::string display_name_;
    std::string version_;
    std::string category_;
    cpipe_node_info_t info_{};
    void* native_handle_{nullptr};
    bool initialized_{false};
};

class PluginLoader {
public:
    explicit PluginLoader(cpipe_host_api_t host_api = {});

    [[nodiscard]] expected<PluginHandle, Error> load(const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<PluginHandle> load_directory(const std::filesystem::path& directory) const;

private:
    cpipe_host_api_t host_api_{};
};

} // namespace cpipe::plugin
