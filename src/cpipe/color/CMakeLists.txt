find_package(OpenColorIO CONFIG REQUIRED)
find_package(lcms2 CONFIG REQUIRED)
find_package(libde265 CONFIG REQUIRED)
find_package(libheif CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

add_library(
    cpipe-color
    STATIC
        HeifReader.cpp
        HeifWriter.cpp
)
add_library(cpipe::color ALIAS cpipe-color)

cpipe_target_warning_flags(cpipe-color)

target_link_libraries(
    cpipe-color
    PUBLIC
        cpipe::core
        cpipe::sdk
    PRIVATE
        de265
        heif
        lcms2::lcms2
        OpenColorIO::OpenColorIO
        yaml-cpp::yaml-cpp
)
