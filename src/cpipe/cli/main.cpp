// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <CLI/CLI.hpp>

int main(int argc, char** argv) {
    CLI::App app{"cpipe"};
    app.allow_extras(false);
    CLI11_PARSE(app, argc, argv);
    return 0;
}
