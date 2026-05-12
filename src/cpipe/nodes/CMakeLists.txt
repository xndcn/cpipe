add_library(cpipe-builtin-nodes STATIC empty.cpp)
add_library(cpipe::builtin-nodes ALIAS cpipe-builtin-nodes)

target_include_directories(cpipe-builtin-nodes PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-builtin-nodes PUBLIC cpipe-sdk cpipe-runtime)
cpipe_target_defaults(cpipe-builtin-nodes)
