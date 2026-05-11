add_library(cpipe-sdk STATIC sdk_stub.cpp)
add_library(cpipe::sdk ALIAS cpipe-sdk)

target_include_directories(cpipe-sdk PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-sdk PUBLIC cpipe-core)
cpipe_target_warning_flags(cpipe-sdk)
