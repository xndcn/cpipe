add_library(cpipe-core STATIC empty.cpp)
add_library(cpipe::core ALIAS cpipe-core)

target_include_directories(cpipe-core PUBLIC "${PROJECT_SOURCE_DIR}/include")
cpipe_target_defaults(cpipe-core)
