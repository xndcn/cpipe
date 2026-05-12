# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 cpipe contributors

function(cpipe_target_warning_flags target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_options(
        ${target}
        PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
            $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
            $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
            $<$<COMPILE_LANGUAGE:CXX>:-Werror>)

    if(CPIPE_ENABLE_SANITIZERS)
        target_compile_options(
            ${target}
            PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address,undefined>
                $<$<COMPILE_LANGUAGE:C>:-fsanitize=address,undefined>)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

function(cpipe_target_c_warning_flags target)
    target_compile_options(
        ${target}
        PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-std=c99>
            $<$<COMPILE_LANGUAGE:C>:-Wall>
            $<$<COMPILE_LANGUAGE:C>:-Wextra>
            $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
            $<$<COMPILE_LANGUAGE:C>:-Werror>)
endfunction()
