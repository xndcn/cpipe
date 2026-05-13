include(FetchContent)

function(cpipe_ensure_halide)
    if(TARGET Halide::Runtime)
        return()
    endif()

    unset(Halide_DIR CACHE)
    unset(HalideHelpers_DIR CACHE)

    if(CPIPE_HALIDE_ROOT)
        find_package(Halide 21 CONFIG REQUIRED PATHS "${CPIPE_HALIDE_ROOT}" NO_DEFAULT_PATH)
    else()
        FetchContent_Declare(
            cpipe_halide
            URL
                https://github.com/halide/Halide/releases/download/v21.0.0/Halide-21.0.0-x86-64-linux-b629c80de18f1534ec71fddd8b567aa7027a0876.tar.gz
        )
        FetchContent_GetProperties(cpipe_halide)
        if(NOT cpipe_halide_POPULATED)
            cmake_policy(PUSH)
            if(POLICY CMP0169)
                cmake_policy(SET CMP0169 OLD)
            endif()
            FetchContent_Populate(cpipe_halide)
            cmake_policy(POP)
        endif()
        find_package(Halide 21 CONFIG REQUIRED PATHS "${cpipe_halide_SOURCE_DIR}" NO_DEFAULT_PATH)
    endif()
endfunction()

function(cpipe_add_halide_library target)
    cpipe_ensure_halide()
    add_halide_library(${target} ${ARGN})
endfunction()
