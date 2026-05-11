add_library(cpipe-builtin-nodes STATIC nodes_stub.cpp)
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

target_include_directories(cpipe-builtin-nodes PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-builtin-nodes PUBLIC cpipe-sdk cpipe-runtime)
cpipe_target_warning_flags(cpipe-builtin-nodes)
