# Strict warnings for our own code (not for JUCE or other dependencies).
function(so_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                                 -Wno-sign-conversion -Wnon-virtual-dtor)
    endif()
endfunction()
