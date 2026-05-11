function(cpipe_find_lavapipe_icd out_var)
    set(candidates
        "/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
        "/usr/share/vulkan/icd.d/lvp_icd.json"
        "/usr/share/vulkan/icd.d/lavapipe_icd.x86_64.json"
        "/usr/share/vulkan/icd.d/lavapipe_icd.json"
        "/usr/share/vulkan/icd.d/lvp_icd.i686.json")

    foreach(candidate IN LISTS candidates)
        if(EXISTS "${candidate}")
            set("${out_var}" "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set("${out_var}" "" PARENT_SCOPE)
endfunction()
