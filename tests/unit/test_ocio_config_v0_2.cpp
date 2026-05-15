// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenColorIO/OpenColorIO.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <filesystem>
#include <set>
#include <string>

namespace OCIO = OCIO_NAMESPACE;

namespace {

std::filesystem::path config_path() {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / "share" / "cpipe" / "ocio" / "v0.2" /
           "config.ocio";
}

std::set<std::string> display_names(const OCIO::ConstConfigRcPtr& config) {
    std::set<std::string> out;
    for (int i = 0; i < config->getNumDisplays(); ++i) {
        out.emplace(config->getDisplay(i));
    }
    return out;
}

std::set<std::string> view_names(const OCIO::ConstConfigRcPtr& config, const char* display) {
    std::set<std::string> out;
    for (int i = 0; i < config->getNumViews(display); ++i) {
        out.emplace(config->getView(display, i));
    }
    return out;
}

}  // namespace

TEST_CASE("OCIO config v0.2 exposes scene and display spaces") {
    const auto config = OCIO::Config::CreateFromFile(config_path().string().c_str());
    REQUIRE(config);

    REQUIRE(config->getColorSpace("raw_camera") != nullptr);
    REQUIRE(config->getColorSpace("scene_linear_rec2020") != nullptr);
    REQUIRE(config->getColorSpace("output_srgb") != nullptr);
    REQUIRE(config->getColorSpace("output_pq_rec2020") != nullptr);

    const auto displays = display_names(config);
    REQUIRE(displays == std::set<std::string>{"Display P3", "Rec.2020 HLG", "Rec.2020 PQ", "sRGB"});

    REQUIRE(view_names(config, "sRGB") == std::set<std::string>{"Standard SDR", "Untouched"});
    REQUIRE(view_names(config, "Display P3") == std::set<std::string>{"Standard SDR", "Untouched"});
    REQUIRE(view_names(config, "Rec.2020 PQ") ==
            std::set<std::string>{"Standard HDR", "Untouched"});
    REQUIRE(view_names(config, "Rec.2020 HLG") ==
            std::set<std::string>{"Standard HDR", "Untouched"});

    REQUIRE(config->getLook("Standard SDR") != nullptr);
    REQUIRE(config->getLook("Standard HDR") != nullptr);
}

TEST_CASE("Host OCIO accessor loads v0.2 output processors") {
    cpipe::runtime::HostContext host_context;
    auto* host = host_context.host();

    auto* srgb = host->get_ocio_processor(host, config_path().string().c_str(),
                                          "scene_linear_rec2020", "output_srgb");
    REQUIRE(srgb != nullptr);

    auto* pq = host->get_ocio_processor(host, config_path().string().c_str(),
                                        "scene_linear_rec2020", "output_pq_rec2020");
    REQUIRE(pq != nullptr);
}
