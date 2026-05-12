# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 cpipe contributors

function(cpipe_fetch_halide)
    set(CPIPE_HALIDE_VERSION "21.0.0")
    set(CPIPE_HALIDE_RELEASE_HASH "b629c80de18f1534ec71fddd8b567aa7027a0876")
    set(CPIPE_HALIDE_ARCHIVE
        "Halide-${CPIPE_HALIDE_VERSION}-x86-64-linux-${CPIPE_HALIDE_RELEASE_HASH}.tar.gz")
    set(CPIPE_HALIDE_URL
        "https://github.com/halide/Halide/releases/download/v${CPIPE_HALIDE_VERSION}/${CPIPE_HALIDE_ARCHIVE}"
    )

    FetchContent_Declare(cpipe_halide URL "${CPIPE_HALIDE_URL}")
    FetchContent_MakeAvailable(cpipe_halide)

    list(APPEND CMAKE_PREFIX_PATH "${cpipe_halide_SOURCE_DIR}")
    find_package(Halide ${CPIPE_HALIDE_VERSION} REQUIRED CONFIG)
endfunction()
