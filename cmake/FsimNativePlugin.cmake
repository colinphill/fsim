# SPDX-License-Identifier: Apache-2.0

function(fsim_add_tf_link_surface target)
  add_library(
    ${target} SHARED
    "${PROJECT_SOURCE_DIR}/src/runtime/tf_link.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/tf_containment.cpp"
  )
  add_library(fsim::tf ALIAS ${target})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_definitions(${target} PRIVATE FSIM_TF_LINK_SURFACE_BUILD=1)
  target_include_directories(
    ${target}
    PUBLIC
      "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )
  set_target_properties(
    ${target}
    PROPERTIES
      LINKER_LANGUAGE CXX
      OUTPUT_NAME fsim_tf
      SOVERSION 3
  )
  if(NOT WIN32)
    set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()
  fsim_enable_warnings(${target})
  fsim_configure_debug_footprint(${target})
endfunction()
