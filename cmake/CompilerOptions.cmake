option(CPIPE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(cpipe_target_warning_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
        return()
    endif()

    target_compile_options(
        ${target}
        PRIVATE -Wall
                -Wextra
                -Wpedantic
                -Wshadow
                -Wconversion
                -Werror
                -fexceptions)
endfunction()

function(cpipe_target_sanitizers target)
    if(NOT CPIPE_ENABLE_SANITIZERS)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

function(cpipe_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    cpipe_target_warning_flags(${target})
    cpipe_target_sanitizers(${target})
endfunction()
