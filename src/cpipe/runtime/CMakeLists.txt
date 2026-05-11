add_library(cpipe-runtime STATIC runtime_stub.cpp)
add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(cpipe-runtime PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-runtime PUBLIC cpipe-core)
cpipe_target_warning_flags(cpipe-runtime)
