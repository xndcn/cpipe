include(FetchContent)

set(
    CPIPE_HALIDE_URL
    "https://github.com/halide/Halide/releases/download/v21.0.0/Halide-21.0.0-x86-64-linux-b629c80de18f1534ec71fddd8b567aa7027a0876.tar.gz"
    CACHE STRING "Halide v21 Linux x86_64 release archive")

function(cpipe_find_halide)
    if(TARGET Halide::Halide)
        return()
    endif()

    FetchContent_Declare(cpipe_halide URL "${CPIPE_HALIDE_URL}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(cpipe_halide)
    list(PREPEND CMAKE_PREFIX_PATH "${cpipe_halide_SOURCE_DIR}")
    find_package(Halide 21 REQUIRED CONFIG)
endfunction()
