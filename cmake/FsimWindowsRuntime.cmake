# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

set(FSIM_WINDOWS_TOOLCHAIN "fsim-toolchain-22.1.8-1")

# Return the value of KEY=value from the toolchain provenance record.
function(fsim_windows_toolchain_record source_dir key output)
  file(STRINGS
    "${source_dir}/third_party/${FSIM_WINDOWS_TOOLCHAIN}/SOURCE_MANIFEST.txt"
    records REGEX "^${key}=")
  if(NOT records MATCHES "^${key}=([^;]+)$")
    message(FATAL_ERROR
      "${FSIM_WINDOWS_TOOLCHAIN} provenance does not record ${key}")
  endif()
  set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

# With FSIM_WINDOWS_TOOLCHAIN_REDIST naming the toolchain's redistributable
# archive, check it and the compiler against the provenance record, unpack it
# into the build tree, and set FSIM_WINDOWS_TOOLCHAIN_BUNDLE to its root so
# installation places the whole toolchain beside fsim. Otherwise installation
# carries only the runtime DLLs fsim's binaries import.
function(fsim_configure_windows_toolchain)
  set(FSIM_WINDOWS_TOOLCHAIN_REDIST "" CACHE FILEPATH
    "Redistributable fsim toolchain archive to install beside fsim on Windows")
  set(FSIM_WINDOWS_TOOLCHAIN_BUNDLE "" PARENT_SCOPE)
  if(NOT (WIN32 AND MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      OR FSIM_WINDOWS_TOOLCHAIN_REDIST STREQUAL "")
    return()
  endif()
  # The toolchain finds its headers and libraries from its bin directory, and
  # fsim shares that directory for the runtime DLLs and the plug-in compiler.
  if(NOT CMAKE_INSTALL_BINDIR STREQUAL "bin")
    message(FATAL_ERROR
      "bundling ${FSIM_WINDOWS_TOOLCHAIN} requires CMAKE_INSTALL_BINDIR=bin")
  endif()
  fsim_windows_toolchain_record(
    "${PROJECT_SOURCE_DIR}" redist_archive_sha256 expected_sha256)
  fsim_windows_toolchain_record(
    "${PROJECT_SOURCE_DIR}" archive_root archive_root)
  fsim_windows_toolchain_record(
    "${PROJECT_SOURCE_DIR}" fork_commit fork_commit)

  if(NOT EXISTS "${FSIM_WINDOWS_TOOLCHAIN_REDIST}")
    message(FATAL_ERROR
      "FSIM_WINDOWS_TOOLCHAIN_REDIST does not exist: "
      "${FSIM_WINDOWS_TOOLCHAIN_REDIST}")
  endif()
  file(SHA256 "${FSIM_WINDOWS_TOOLCHAIN_REDIST}" actual_sha256)
  if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR
      "${FSIM_WINDOWS_TOOLCHAIN_REDIST} is not the ${FSIM_WINDOWS_TOOLCHAIN} "
      "redistributable archive recorded in third_party/"
      "${FSIM_WINDOWS_TOOLCHAIN}/SOURCE_MANIFEST.txt")
  endif()

  # Bundled plug-in builds must use fsim's own compiler and C++ runtime, so
  # the compiler has to come from the same toolchain build.
  get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(compiler_root "${compiler_bin}" DIRECTORY)
  set(compiler_manifest "${compiler_root}/FSIM-TOOLCHAIN.txt")
  set(compiler_records)
  if(EXISTS "${compiler_manifest}")
    file(STRINGS "${compiler_manifest}" compiler_records)
  endif()
  if(NOT "fork_commit=${fork_commit}" IN_LIST compiler_records)
    message(FATAL_ERROR
      "bundling ${FSIM_WINDOWS_TOOLCHAIN} requires the compiler from the same "
      "toolchain build; ${CMAKE_CXX_COMPILER} is not")
  endif()

  set(unpacked "${CMAKE_BINARY_DIR}/_deps/fsim_windows_toolchain")
  set(stamp "${unpacked}/archive.sha256")
  set(unpacked_sha256 "")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" unpacked_sha256)
  endif()
  if(NOT unpacked_sha256 STREQUAL actual_sha256)
    message(STATUS "Unpacking ${FSIM_WINDOWS_TOOLCHAIN} redistributable")
    file(REMOVE_RECURSE "${unpacked}")
    file(ARCHIVE_EXTRACT
      INPUT "${FSIM_WINDOWS_TOOLCHAIN_REDIST}"
      DESTINATION "${unpacked}")
    file(WRITE "${stamp}" "${actual_sha256}")
  endif()
  set(bundle "${unpacked}/${archive_root}")
  set(bundle_records)
  if(EXISTS "${bundle}/FSIM-TOOLCHAIN.txt")
    file(STRINGS "${bundle}/FSIM-TOOLCHAIN.txt" bundle_records)
  endif()
  if(NOT "variant=redist" IN_LIST bundle_records
      OR NOT "fork_commit=${fork_commit}" IN_LIST bundle_records
      OR NOT EXISTS "${bundle}/bin/clang++.exe")
    message(FATAL_ERROR
      "${FSIM_WINDOWS_TOOLCHAIN_REDIST} does not unpack to the "
      "${FSIM_WINDOWS_TOOLCHAIN} redistributable toolchain")
  endif()
  message(STATUS "fsim Windows toolchain bundle: ${FSIM_WINDOWS_TOOLCHAIN}")
  set(FSIM_WINDOWS_TOOLCHAIN_BUNDLE "${bundle}" PARENT_SCOPE)
endfunction()

# Install the toolchain runtime with its provenance, so Windows archives run
# without the toolchain on PATH. A bundle installs the whole redistributable
# toolchain into the prefix, sharing bin with fsim. Without one, the runtime
# DLLs come from the compiler's directory, where the tests also load them,
# and installation stops if one differs from its recorded digest.
function(fsim_install_windows_runtime)
  if(NOT (WIN32 AND MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    return()
  endif()
  set(root "${PROJECT_SOURCE_DIR}/third_party/${FSIM_WINDOWS_TOOLCHAIN}")
  set(docs "${CMAKE_INSTALL_DOCDIR}/third-party/${FSIM_WINDOWS_TOOLCHAIN}")
  install(
    FILES
      "${root}/LICENSE"
      "${root}/NOTICE"
      "${root}/SOURCE_MANIFEST.txt"
      "${root}/${FSIM_WINDOWS_TOOLCHAIN}.spdx.json"
    DESTINATION "${docs}"
  )
  if(FSIM_WINDOWS_TOOLCHAIN_BUNDLE)
    # The toolchain locates its headers and libraries relative to bin, so its
    # directories install at the prefix root. Its C and C++ headers move to
    # the target directory, which clang also searches, keeping them out of the
    # include directory that fsim's consumers add to their search path. Its
    # license is the LICENSE above, and its manifest joins the docs.
    set(bundle "${FSIM_WINDOWS_TOOLCHAIN_BUNDLE}")
    if(EXISTS "${bundle}/x86_64-w64-mingw32/include")
      message(FATAL_ERROR
        "${FSIM_WINDOWS_TOOLCHAIN} unexpectedly has target headers")
    endif()
    file(GLOB entries RELATIVE "${bundle}" "${bundle}/*")
    list(SORT entries)
    foreach(entry IN LISTS entries)
      if(entry STREQUAL "FSIM-TOOLCHAIN.txt")
        install(FILES "${bundle}/${entry}" DESTINATION "${docs}")
      elseif(entry STREQUAL "include")
        install(
          DIRECTORY "${bundle}/include/"
          DESTINATION "x86_64-w64-mingw32/include"
          USE_SOURCE_PERMISSIONS
        )
      elseif(entry MATCHES "^(bin|lib|share|x86_64-w64-mingw32)$")
        install(
          DIRECTORY "${bundle}/${entry}/"
          DESTINATION "${entry}"
          USE_SOURCE_PERMISSIONS
        )
      elseif(NOT entry STREQUAL "LICENSE.TXT")
        message(FATAL_ERROR
          "${FSIM_WINDOWS_TOOLCHAIN} has an unexpected top-level entry: ${entry}")
      endif()
    endforeach()
    return()
  endif()

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
      message(FATAL_ERROR
        "${FSIM_WINDOWS_TOOLCHAIN} provenance does not record ${name}")
    endif()
    set(path "${toolchain_bin}/${name}")
    install(CODE "
      if(NOT EXISTS \"${path}\")
        message(FATAL_ERROR \"${FSIM_WINDOWS_TOOLCHAIN} runtime file is missing: ${path}\")
      endif()
      file(SHA256 \"${path}\" fsim_runtime_sha256)
      if(NOT fsim_runtime_sha256 STREQUAL \"${expected}\")
        message(FATAL_ERROR
          \"${path} differs from the ${FSIM_WINDOWS_TOOLCHAIN} runtime recorded in \"
          \"third_party/${FSIM_WINDOWS_TOOLCHAIN}/SOURCE_MANIFEST.txt\")
      endif()
    ")
    list(APPEND files "${path}")
  endforeach()
  install(FILES ${files} DESTINATION "${CMAKE_INSTALL_BINDIR}")
endfunction()
