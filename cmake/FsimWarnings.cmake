# SPDX-License-Identifier: Apache-2.0

function(fsim_enable_warnings target)
  if(MSVC)
    target_compile_options(
      ${target}
      PRIVATE
        /W4
        "$<$<COMPILE_LANGUAGE:CXX>:/permissive-;/Zc:__cplusplus>"
    )
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(FSIM_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(
      ${target}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        "$<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor;-Wold-style-cast>"
    )
    if(FSIM_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
