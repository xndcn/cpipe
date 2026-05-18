if(NOT DEFINED CPIPE_WEB_DIST_DIR)
    message(FATAL_ERROR "CPIPE_WEB_DIST_DIR is required")
endif()

set(cpipe_editor_static_stamp "${CPIPE_WEB_DIST_DIR}/.stamp")
set(cpipe_editor_static_destination "${CMAKE_INSTALL_PREFIX}/share/cpipe/editor")

if(NOT EXISTS "${cpipe_editor_static_stamp}")
    message(STATUS "Skipping cpipe editor static install; ${cpipe_editor_static_stamp} not found")
    return()
endif()

file(
    INSTALL
    DESTINATION "${cpipe_editor_static_destination}"
    TYPE DIRECTORY
    FILES "${CPIPE_WEB_DIST_DIR}/"
    PATTERN ".stamp" EXCLUDE
)
