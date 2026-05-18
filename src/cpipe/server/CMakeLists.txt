find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)
if(CPIPE_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()

add_library(cpipe-server STATIC EditorServer.cpp stub.cpp)
add_library(cpipe::server ALIAS cpipe-server)

cpipe_target_warning_flags(cpipe-server)

target_compile_definitions(
    cpipe-server
    PRIVATE
        CPIPE_ENABLE_TRACY=$<BOOL:${CPIPE_ENABLE_TRACY}>
)

target_link_libraries(
    cpipe-server
    PUBLIC
        cpipe::runtime
    PRIVATE
        unofficial::uwebsockets::uwebsockets
        WebP::webp
)

if(CPIPE_ENABLE_TRACY)
    target_link_libraries(cpipe-server PRIVATE Tracy::TracyClient)
endif()
