# SPDX-License-Identifier: Apache-2.0

function(fsim_add_tf_link_surface target)
  add_library(
    ${target} SHARED
    "${PROJECT_SOURCE_DIR}/src/runtime/svdpi.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/vpi_standard.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/tf_link.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/tf_containment.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_lifecycle.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_handle.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_iterator.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_lookup.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_traversal.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_object.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_read.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_write.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_timing.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_vcl.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_callback.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_handle_lifetime.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_tf_coherence.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_vpi_coherence.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/acc_vendor_rejection.cpp"
  )
  add_library(fsim::tf ALIAS ${target})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_definitions(
    ${target}
    PRIVATE
      FSIM_TF_LINK_SURFACE_BUILD=1
      FSIM_ACC_LINK_SURFACE_BUILD=1
      FSIM_SVDPI_LINK_SURFACE_BUILD=1
      FSIM_VPI_LINK_SURFACE_BUILD=1
      FSIM_PROJECT_VERSION="${PROJECT_VERSION}"
  )
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
