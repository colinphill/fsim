# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

set(FSIM_LLVM_MINGW_RUNTIME_RELEASE "20260616")

# Install the LLVM-MinGW runtime DLLs that fsim's binaries import, with their
# provenance, so Windows archives run without the toolchain on PATH. The DLLs
# come from the compiler's directory, where the tests also load them.
# Installation stops if one differs from its SOURCE_MANIFEST.txt digest.
function(fsim_install_windows_runtime)
  if(NOT (WIN32 AND MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    return()
  endif()
  set(component "llvm-mingw-${FSIM_LLVM_MINGW_RUNTIME_RELEASE}")
  set(root "${PROJECT_SOURCE_DIR}/third_party/${component}")
  set(names libc++.dll libunwind.dll)
  if(FSIM_WITH_LLVM AND TARGET LLVM)
    get_target_property(llvm_type LLVM TYPE)
    get_target_property(llvm_location LLVM LOCATION)
    if(llvm_type STREQUAL "SHARED_LIBRARY")
      if(NOT llvm_location MATCHES "[.]dll$")
        message(FATAL_ERROR
          "shared LLVM target has no DLL location: ${llvm_location}")
      endif()
      get_filename_component(llvm_name "${llvm_location}" NAME)
      list(APPEND names "${llvm_name}")
    endif()
  endif()

  get_filename_component(toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  file(STRINGS "${root}/SOURCE_MANIFEST.txt" records REGEX "^file_sha256=")
  set(files)
  foreach(name IN LISTS names)
    set(expected "")
    foreach(record IN LISTS records)
      if(record MATCHES "^file_sha256=bin/([^|]+)[|]([0-9a-f]+)$"
          AND CMAKE_MATCH_1 STREQUAL name)
        set(expected "${CMAKE_MATCH_2}")
      endif()
    endforeach()
    if(expected STREQUAL "")
      message(FATAL_ERROR "${component} provenance does not record ${name}")
    endif()
    set(path "${toolchain_bin}/${name}")
    install(CODE "
      if(NOT EXISTS \"${path}\")
        message(FATAL_ERROR \"${component} runtime file is missing: ${path}\")
      endif()
      file(SHA256 \"${path}\" fsim_runtime_sha256)
      if(NOT fsim_runtime_sha256 STREQUAL \"${expected}\")
        message(FATAL_ERROR
          \"${path} differs from the ${component} runtime recorded in \"
          \"third_party/${component}/SOURCE_MANIFEST.txt\")
      endif()
    ")
    list(APPEND files "${path}")
  endforeach()
  install(FILES ${files} DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(
    FILES
      "${root}/LICENSE"
      "${root}/NOTICE"
      "${root}/SOURCE_MANIFEST.txt"
      "${root}/${component}.spdx.json"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party/${component}"
  )
endfunction()
