include(FetchContent)

function(cpipe_ensure_halide)
    if(TARGET Halide::Halide AND TARGET Halide::Runtime)
        return()
    endif()

    set(CPIPE_HALIDE_URL
        "https://github.com/halide/Halide/releases/download/v21.0.0/Halide-21.0.0-x86-64-linux-b629c80de18f1534ec71fddd8b567aa7027a0876.tar.gz")
    set(CPIPE_HALIDE_SHA256 "b56139ddc5d863486b9b339e1c9b7cc3f6aadd4dd8a2eff2202e79ca68706091")

    FetchContent_Declare(cpipe_halide URL "${CPIPE_HALIDE_URL}" URL_HASH SHA256=${CPIPE_HALIDE_SHA256})
    FetchContent_GetProperties(cpipe_halide)
    if(NOT cpipe_halide_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(cpipe_halide)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()

    find_package(
        Halide 21.0.0 EXACT CONFIG REQUIRED
        PATHS "${cpipe_halide_SOURCE_DIR}"
        NO_DEFAULT_PATH)
    find_package(
        HalideHelpers 21.0.0 EXACT CONFIG REQUIRED
        PATHS "${cpipe_halide_SOURCE_DIR}"
        NO_DEFAULT_PATH)
endfunction()

function(cpipe_add_halide_runtime target)
    cpipe_ensure_halide()
    if(TARGET "${target}")
        return()
    endif()
    add_halide_runtime("${target}" TARGETS host)
endfunction()

function(cpipe_add_halide_library target)
    set(oneValueArgs GENERATOR FUNCTION_NAME USE_RUNTIME)
    set(multiValueArgs SOURCES TARGETS)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "cpipe_add_halide_library(${target}) requires SOURCES")
    endif()
    if(NOT ARG_GENERATOR)
        message(FATAL_ERROR "cpipe_add_halide_library(${target}) requires GENERATOR")
    endif()
    if(NOT ARG_FUNCTION_NAME)
        set(ARG_FUNCTION_NAME "${target}")
    endif()
    if(NOT ARG_TARGETS)
        set(ARG_TARGETS host)
    endif()
    if(NOT ARG_USE_RUNTIME)
        set(ARG_USE_RUNTIME cpipe-halide-runtime)
    endif()

    cpipe_add_halide_runtime("${ARG_USE_RUNTIME}")
    add_halide_generator("${target}_generator" SOURCES ${ARG_SOURCES})
    add_halide_library(
        "${target}"
        FROM
        "${target}_generator"
        GENERATOR
        "${ARG_GENERATOR}"
        FUNCTION_NAME
        "${ARG_FUNCTION_NAME}"
        TARGETS
        ${ARG_TARGETS}
        USE_RUNTIME
        "${ARG_USE_RUNTIME}")
endfunction()
