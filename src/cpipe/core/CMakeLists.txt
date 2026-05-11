add_library(cpipe-core STATIC core_stub.cpp)
add_library(cpipe::core ALIAS cpipe-core)

target_include_directories(cpipe-core PUBLIC "${PROJECT_SOURCE_DIR}/include")
cpipe_target_warning_flags(cpipe-core)
