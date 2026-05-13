add_library(cpipe-builtin-nodes STATIC stub.cpp)
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

cpipe_target_warning_flags(cpipe-builtin-nodes)

target_link_libraries(
    cpipe-builtin-nodes
    PUBLIC
        cpipe::sdk
        cpipe::runtime
)
