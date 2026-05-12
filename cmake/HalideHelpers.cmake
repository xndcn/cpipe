include(FetchContent)

function(cpipe_provide_halide)
    if(TARGET Halide::Runtime)
        return()
    endif()

    find_package(Halide 21.0.0 CONFIG QUIET)
    if(Halide_FOUND)
        return()
    endif()
    unset(Halide_DIR CACHE)

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        message(FATAL_ERROR "P0 Halide FetchContent package is pinned for Linux x86_64 only")
    endif()

    set(CPIPE_HALIDE_ARCHIVE
        "Halide-21.0.0-x86-64-linux-b629c80de18f1534ec71fddd8b567aa7027a0876"
    )
    FetchContent_Declare(
        cpipe_halide
        URL "https://github.com/halide/Halide/releases/download/v21.0.0/${CPIPE_HALIDE_ARCHIVE}.tar.gz"
        URL_HASH SHA256=b56139ddc5d863486b9b339e1c9b7cc3f6aadd4dd8a2eff2202e79ca68706091
    )
    FetchContent_GetProperties(cpipe_halide)
    if(NOT cpipe_halide_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
            FetchContent_Populate(cpipe_halide)
            cmake_policy(POP)
        else()
            FetchContent_Populate(cpipe_halide)
        endif()
    endif()

    set(Halide_DIR "${cpipe_halide_SOURCE_DIR}/lib/cmake/Halide" CACHE PATH "" FORCE)
    find_package(
        Halide 21.0.0 CONFIG REQUIRED
        PATHS "${cpipe_halide_SOURCE_DIR}"
        NO_DEFAULT_PATH
    )
endfunction()

function(cpipe_add_halide_library target)
    message(FATAL_ERROR "cpipe_add_halide_library is wired in T5 when the passthrough generator lands")
endfunction()
