add_library(cpipe-sdk INTERFACE)
add_library(cpipe::sdk ALIAS cpipe-sdk)

target_include_directories(cpipe-sdk INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_compile_features(cpipe-sdk INTERFACE cxx_std_20)
target_link_libraries(cpipe-sdk INTERFACE cpipe-core)
