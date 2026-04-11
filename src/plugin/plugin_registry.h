#pragma once

#include "plugin_loader.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cpipe::plugin {

class PluginRegistry {
public:
    [[nodiscard]] expected<void, Error> register_plugin(PluginHandle plugin);
    [[nodiscard]] expected<std::reference_wrapper<const PluginHandle>, Error> find(std::string_view plugin_id) const;
    [[nodiscard]] std::vector<std::reference_wrapper<const PluginHandle>> all() const;
    [[nodiscard]] size_t size() const noexcept;

private:
    std::unordered_map<std::string, PluginHandle> plugins_;
};

} // namespace cpipe::plugin
