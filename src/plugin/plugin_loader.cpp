#include "plugin_loader.h"

#include "error.h"
#include "log.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cpipe::plugin {
namespace {

std::string to_owned_string(const char* value) {
    return value != nullptr ? value : "";
}

const char* shared_library_extension() noexcept {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

expected<void*, Error> open_library(const std::filesystem::path& path) {
#if defined(_WIN32)
    auto* handle = LoadLibraryA(path.string().c_str());
    if (handle == nullptr) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "failed to load plugin '" + path.string() + "'"));
    }
    return reinterpret_cast<void*>(handle);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "failed to load plugin '" + path.string() + "': " + (error != nullptr ? error : "unknown error")));
    }
    return handle;
#endif
}

void close_library(void* handle) noexcept {
    if (handle == nullptr) {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

template <typename Symbol>
expected<Symbol, Error> load_symbol(void* handle, const char* symbol_name, const std::filesystem::path& path) {
#if defined(_WIN32)
    auto* symbol = reinterpret_cast<Symbol>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name));
    if (symbol == nullptr) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "plugin '" + path.string() + "' is missing required symbol '" + symbol_name + "'"));
    }
    return symbol;
#else
    dlerror();
    auto symbol = reinterpret_cast<Symbol>(dlsym(handle, symbol_name));
    const char* error = dlerror();
    if (error != nullptr || symbol == nullptr) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "plugin '" + path.string() + "' is missing required symbol '" + symbol_name + "'"));
    }
    return symbol;
#endif
}

} // namespace

PluginHandle::~PluginHandle() {
    release();
}

PluginHandle::PluginHandle(PluginHandle&& other) noexcept {
    *this = std::move(other);
}

PluginHandle& PluginHandle::operator=(PluginHandle&& other) noexcept {
    if (this != &other) {
        release();

        path_ = std::move(other.path_);
        plugin_id_ = std::move(other.plugin_id_);
        display_name_ = std::move(other.display_name_);
        version_ = std::move(other.version_);
        category_ = std::move(other.category_);
        info_ = other.info_;
        native_handle_ = other.native_handle_;
        initialized_ = other.initialized_;
        plugin_init = other.plugin_init;
        plugin_shutdown = other.plugin_shutdown;
        node_create = other.node_create;
        node_destroy = other.node_destroy;
        node_get_info = other.node_get_info;
        node_get_parameter_schema = other.node_get_parameter_schema;
        node_process = other.node_process;

        if (!plugin_id_.empty()) {
            info_.plugin_id = plugin_id_.c_str();
        }
        info_.display_name = display_name_.c_str();
        info_.version = version_.c_str();
        info_.category = category_.c_str();

        other.info_ = {};
        other.native_handle_ = nullptr;
        other.initialized_ = false;
        other.plugin_init = nullptr;
        other.plugin_shutdown = nullptr;
        other.node_create = nullptr;
        other.node_destroy = nullptr;
        other.node_get_info = nullptr;
        other.node_get_parameter_schema = nullptr;
        other.node_process = nullptr;
    }
    return *this;
}

bool PluginHandle::valid() const noexcept {
    return native_handle_ != nullptr;
}

std::string_view PluginHandle::plugin_id() const noexcept {
    return plugin_id_;
}

const cpipe_node_info_t& PluginHandle::info() const noexcept {
    return info_;
}

const std::filesystem::path& PluginHandle::path() const noexcept {
    return path_;
}

void PluginHandle::release() noexcept {
    if (native_handle_ == nullptr) {
        return;
    }

    if (initialized_ && plugin_shutdown != nullptr) {
        plugin_shutdown();
    }

    close_library(native_handle_);
    native_handle_ = nullptr;
    initialized_ = false;
    plugin_init = nullptr;
    plugin_shutdown = nullptr;
    node_create = nullptr;
    node_destroy = nullptr;
    node_get_info = nullptr;
    node_get_parameter_schema = nullptr;
    node_process = nullptr;
    info_ = {};
    path_.clear();
    plugin_id_.clear();
    display_name_.clear();
    version_.clear();
    category_.clear();
}

PluginLoader::PluginLoader(cpipe_host_api_t host_api) : host_api_(host_api) {}

expected<PluginHandle, Error> PluginLoader::load(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "plugin path does not exist: " + path.string()));
    }

    auto library = open_library(path);
    if (!library.has_value()) {
        CPIPE_LOG_WARN("{}", library.error().message);
        return unexpected<Error>(std::move(library.error()));
    }

    PluginHandle handle;
    handle.path_ = path;
    handle.native_handle_ = library.value();

    auto init = load_symbol<PluginHandle::plugin_init_fn>(handle.native_handle_, "cpipe_plugin_init", path);
    if (!init.has_value()) {
        CPIPE_LOG_WARN("{}", init.error().message);
        handle.release();
        return unexpected<Error>(std::move(init.error()));
    }
    handle.plugin_init = init.value();

    const auto init_status = handle.plugin_init(&host_api_);
    if (init_status != CPIPE_STATUS_OK) {
        auto error = make_error(
            init_status,
            "plugin '" + path.string() + "' init failed with status " + std::string(status_to_string(init_status)));
        CPIPE_LOG_WARN("{}", error.message);
        handle.release();
        return unexpected<Error>(std::move(error));
    }
    handle.initialized_ = true;

    auto shutdown = load_symbol<PluginHandle::plugin_shutdown_fn>(handle.native_handle_, "cpipe_plugin_shutdown", path);
    auto create = load_symbol<PluginHandle::node_create_fn>(handle.native_handle_, "cpipe_node_create", path);
    auto destroy = load_symbol<PluginHandle::node_destroy_fn>(handle.native_handle_, "cpipe_node_destroy", path);
    auto get_info = load_symbol<PluginHandle::node_get_info_fn>(handle.native_handle_, "cpipe_node_get_info", path);
    auto get_parameter_schema = load_symbol<PluginHandle::node_get_parameter_schema_fn>(handle.native_handle_, "cpipe_node_get_parameter_schema", path);
    auto process = load_symbol<PluginHandle::node_process_fn>(handle.native_handle_, "cpipe_node_process", path);

    const Error* first_error = nullptr;
    for (const auto* candidate : {shutdown.has_value() ? nullptr : &shutdown.error(),
                                  create.has_value() ? nullptr : &create.error(),
                                  destroy.has_value() ? nullptr : &destroy.error(),
                                  get_info.has_value() ? nullptr : &get_info.error(),
                                  get_parameter_schema.has_value() ? nullptr : &get_parameter_schema.error(),
                                  process.has_value() ? nullptr : &process.error()}) {
        if (candidate != nullptr) {
            first_error = candidate;
            break;
        }
    }

    if (first_error != nullptr) {
        CPIPE_LOG_WARN("{}", first_error->message);
        auto error = *first_error;
        handle.release();
        return unexpected<Error>(std::move(error));
    }

    handle.plugin_shutdown = shutdown.value();
    handle.node_create = create.value();
    handle.node_destroy = destroy.value();
    handle.node_get_info = get_info.value();
    handle.node_get_parameter_schema = get_parameter_schema.value();
    handle.node_process = process.value();

    cpipe_node_t* probe_node = handle.node_create("{}");
    if (probe_node == nullptr) {
        auto error = make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "plugin '" + path.string() + "' failed to create a probe node");
        CPIPE_LOG_WARN("{}", error.message);
        handle.release();
        return unexpected<Error>(std::move(error));
    }

    const cpipe_node_info_t* info = handle.node_get_info(probe_node);
    if (info == nullptr || info->plugin_id == nullptr) {
        handle.node_destroy(probe_node);
        auto error = make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "plugin '" + path.string() + "' returned invalid node metadata");
        CPIPE_LOG_WARN("{}", error.message);
        handle.release();
        return unexpected<Error>(std::move(error));
    }

    if (info->abi_version != CPIPE_NODE_PLUGIN_API_VERSION) {
        handle.node_destroy(probe_node);
        auto error = make_error(
            CPIPE_STATUS_ERROR_ABI_MISMATCH,
            "plugin '" + path.string() + "' ABI mismatch: expected " +
                std::to_string(CPIPE_NODE_PLUGIN_API_VERSION) + ", got " + std::to_string(info->abi_version));
        CPIPE_LOG_WARN("{}", error.message);
        handle.release();
        return unexpected<Error>(std::move(error));
    }

    handle.plugin_id_ = to_owned_string(info->plugin_id);
    handle.display_name_ = to_owned_string(info->display_name);
    handle.version_ = to_owned_string(info->version);
    handle.category_ = to_owned_string(info->category);
    handle.info_ = *info;
    handle.info_.plugin_id = handle.plugin_id_.c_str();
    handle.info_.display_name = handle.display_name_.c_str();
    handle.info_.version = handle.version_.c_str();
    handle.info_.category = handle.category_.c_str();

    handle.node_destroy(probe_node);
    return handle;
}

std::vector<PluginHandle> PluginLoader::load_directory(const std::filesystem::path& directory) const {
    std::vector<PluginHandle> plugins;
    std::vector<std::filesystem::path> candidates;
    std::error_code error;

    if (!std::filesystem::is_directory(directory, error)) {
        CPIPE_LOG_WARN("plugin directory is not readable: {}", directory.string());
        return plugins;
    }

    for (std::filesystem::directory_iterator it(directory, error), end; it != end && !error; it.increment(error)) {
        if (!it->is_regular_file()) {
            continue;
        }
        if (it->path().extension() == shared_library_extension()) {
            candidates.push_back(it->path());
        }
    }

    std::sort(candidates.begin(), candidates.end());
    for (const auto& candidate : candidates) {
        auto plugin = load(candidate);
        if (plugin.has_value()) {
            plugins.push_back(std::move(plugin.value()));
        }
    }

    return plugins;
}

} // namespace cpipe::plugin
