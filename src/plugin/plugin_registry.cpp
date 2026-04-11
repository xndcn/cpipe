#include "plugin_registry.h"

#include "error.h"
#include "log.h"

namespace cpipe::plugin {

expected<void, Error> PluginRegistry::register_plugin(PluginHandle plugin) {
    if (!plugin.valid() || plugin.plugin_id().empty()) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM,
            "cannot register an invalid plugin handle"));
    }

    const std::string plugin_id{plugin.plugin_id()};
    if (plugins_.contains(plugin_id)) {
        auto error = make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM,
            "plugin '" + plugin_id + "' is already registered");
        CPIPE_LOG_WARN("{}", error.message);
        return unexpected<Error>(std::move(error));
    }

    plugins_.emplace(plugin_id, std::move(plugin));
    return {};
}

expected<std::reference_wrapper<const PluginHandle>, Error> PluginRegistry::find(std::string_view plugin_id) const {
    auto it = plugins_.find(std::string(plugin_id));
    if (it == plugins_.end()) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED,
            "plugin '" + std::string(plugin_id) + "' is not registered"));
    }

    return std::cref(it->second);
}

std::vector<std::reference_wrapper<const PluginHandle>> PluginRegistry::all() const {
    std::vector<std::reference_wrapper<const PluginHandle>> plugins;
    plugins.reserve(plugins_.size());
    for (const auto& [plugin_id, plugin] : plugins_) {
        (void)plugin_id;
        plugins.push_back(std::cref(plugin));
    }
    return plugins;
}

size_t PluginRegistry::size() const noexcept {
    return plugins_.size();
}

} // namespace cpipe::plugin
