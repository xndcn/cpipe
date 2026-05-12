function(cpipe_enable_ccache)
    find_program(CPIPE_CCACHE_PROGRAM ccache)
    if(CPIPE_CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER
            "${CPIPE_CCACHE_PROGRAM}"
            CACHE STRING "C compiler launcher" FORCE
        )
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${CPIPE_CCACHE_PROGRAM}"
            CACHE STRING "CXX compiler launcher" FORCE
        )
    endif()
endfunction()

function(cpipe_target_warning_flags target)
    target_compile_options(
        ${target}
        PRIVATE
            $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wextra>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wpedantic>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wshadow>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wconversion>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Werror>
            $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wall>
            $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wextra>
            $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wpedantic>
            $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wshadow>
            $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wconversion>
            $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Werror>
    )
endfunction()

function(cpipe_target_sanitizers target)
    if(CPIPE_ENABLE_ASAN_UBSAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

function(cpipe_configure_target target)
    cpipe_target_warning_flags(${target})
    cpipe_target_sanitizers(${target})
endfunction()
