# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

set(FSIM_MIMALLOC_VERSION "3.5.3")
set(FSIM_MIMALLOC_ARCHIVE_NAME "mimalloc-3.5.3.tar.gz")
set(FSIM_MIMALLOC_ARCHIVE_SIZE 1455942)
set(FSIM_MIMALLOC_ARCHIVE_SHA256
    "3b4a15153a59905995f7070296ed604bb5ccc00cabb8b93446931aff77224d47")
set(FSIM_MIMALLOC_SOURCE_ROOT "mimalloc-3.5.3")
set(FSIM_MIMALLOC_TREE_FILES 382)
set(FSIM_MIMALLOC_TREE_SHA256
    "6137e8d51396a3492833c7b8e62f7766af3d9ff0f4b33811b06f34e9ea6d35d2")

function(fsim_mimalloc_validate_archive archive)
  if(NOT EXISTS "${archive}" OR IS_DIRECTORY "${archive}")
    message(FATAL_ERROR
      "mimalloc ${FSIM_MIMALLOC_VERSION} release archive is missing: ${archive}")
  endif()
  file(SIZE "${archive}" actual_size)
  if(NOT actual_size EQUAL FSIM_MIMALLOC_ARCHIVE_SIZE)
    message(FATAL_ERROR
      "mimalloc archive size mismatch: expected ${FSIM_MIMALLOC_ARCHIVE_SIZE}, found ${actual_size}")
  endif()
  file(SHA256 "${archive}" actual_digest)
  if(NOT actual_digest STREQUAL FSIM_MIMALLOC_ARCHIVE_SHA256)
    message(FATAL_ERROR "mimalloc archive SHA-256 mismatch: ${actual_digest}")
  endif()
endfunction()

function(fsim_mimalloc_validate_source_tree root)
  if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "mimalloc source root is missing: ${root}")
  endif()
  file(GLOB_RECURSE files LIST_DIRECTORIES FALSE RELATIVE "${root}" "${root}/*")
  list(SORT files)
  list(LENGTH files file_count)
  if(NOT file_count EQUAL FSIM_MIMALLOC_TREE_FILES)
    message(FATAL_ERROR
      "mimalloc extracted source has ${file_count} files, expected ${FSIM_MIMALLOC_TREE_FILES}")
  endif()
  set(canonical "")
  foreach(relative_path IN LISTS files)
    if(IS_SYMLINK "${root}/${relative_path}")
      message(FATAL_ERROR "mimalloc extracted source is a symlink: ${relative_path}")
    endif()
    file(SHA256 "${root}/${relative_path}" digest)
    string(APPEND canonical "${digest}  ${relative_path}\n")
  endforeach()
  string(SHA256 tree_digest "${canonical}")
  if(NOT tree_digest STREQUAL FSIM_MIMALLOC_TREE_SHA256)
    message(FATAL_ERROR "mimalloc extracted source SHA-256 mismatch: ${tree_digest}")
  endif()
endfunction()

function(fsim_mimalloc_materialize_source archive work_root output_root)
  fsim_mimalloc_validate_archive("${archive}")
  set(root "${work_root}/${FSIM_MIMALLOC_SOURCE_ROOT}")
  if(NOT EXISTS "${root}")
    set(staging "${work_root}/extract-${FSIM_MIMALLOC_ARCHIVE_SHA256}")
    file(REMOVE_RECURSE "${staging}")
    file(MAKE_DIRECTORY "${staging}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${staging}")
    fsim_mimalloc_validate_source_tree("${staging}/${FSIM_MIMALLOC_SOURCE_ROOT}")
    file(RENAME "${staging}/${FSIM_MIMALLOC_SOURCE_ROOT}" "${root}")
    file(REMOVE_RECURSE "${staging}")
  endif()
  # Check every configure, including an already materialized source directory.
  fsim_mimalloc_validate_source_tree("${root}")
  set(${output_root} "${root}" PARENT_SCOPE)
endfunction()

# Whether this build replaces the allocator: sanitizers and the allocation
# profiler interpose their own allocation functions.
function(fsim_mimalloc_enabled output)
  set(enabled ${FSIM_MIMALLOC})
  string(CONCAT flags "${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS} "
    "${CMAKE_EXE_LINKER_FLAGS}")
  if(flags MATCHES "-fsanitize=" OR FSIM_ENABLE_ALLOCATION_PROFILING)
    set(enabled OFF)
  endif()
  set(${output} ${enabled} PARENT_SCOPE)
endfunction()

# Makes `executable` allocate with mimalloc (see third_party/mimalloc-3.5.3).
function(fsim_use_mimalloc executable)
  fsim_mimalloc_enabled(enabled)
  if(NOT enabled)
    return()
  endif()
  set(vendor_root
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/mimalloc-${FSIM_MIMALLOC_VERSION}")
  if(NOT TARGET fsim_mimalloc)
    fsim_mimalloc_materialize_source(
      "${vendor_root}/${FSIM_MIMALLOC_ARCHIVE_NAME}"
      "${CMAKE_BINARY_DIR}/_deps/fsim_mimalloc_3_5_3-src"
      source_root)
    find_package(Threads REQUIRED)
    set(source "${source_root}/src/static.c")
    if(WIN32)
      # Windows overrides through a DLL loaded before the C runtime's heap is
      # used, with the upstream redirection module; MSVC builds it as C++.
      add_library(fsim_mimalloc SHARED "${source}")
      set_target_properties(fsim_mimalloc PROPERTIES
        OUTPUT_NAME "fsim-mimalloc"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
      target_compile_definitions(fsim_mimalloc PRIVATE
        MI_SHARED_LIB MI_SHARED_LIB_EXPORT MI_MALLOC_OVERRIDE)
      if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set_source_files_properties("${source}" PROPERTIES LANGUAGE CXX)
      endif()
      target_link_libraries(fsim_mimalloc PRIVATE
        "${source_root}/bin/mimalloc-redirect.lib")
      add_custom_command(TARGET fsim_mimalloc POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${source_root}/bin/mimalloc-redirect.dll"
          "$<TARGET_FILE_DIR:fsim_mimalloc>")
      install(TARGETS fsim_mimalloc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
      install(FILES "${source_root}/bin/mimalloc-redirect.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
    else()
      # ELF executables interpose malloc, free and the C++ operators for
      # every shared library they load.
      add_library(fsim_mimalloc OBJECT "${source}")
      set_target_properties(fsim_mimalloc PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)
      target_compile_definitions(fsim_mimalloc PRIVATE MI_MALLOC_OVERRIDE)
      if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options(fsim_mimalloc PRIVATE
          -Wno-unknown-pragmas -ftls-model=initial-exec -fno-builtin-malloc)
      endif()
    endif()
    set_target_properties(fsim_mimalloc PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED ON
      COMPILE_WARNING_AS_ERROR OFF)
    target_include_directories(fsim_mimalloc SYSTEM PRIVATE
      "${source_root}/include")
    target_link_libraries(fsim_mimalloc PUBLIC Threads::Threads)
    # Upstream source does not follow fsim's authored-source warning policy.
    if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      target_compile_options(fsim_mimalloc PRIVATE /WX-)
    elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
      target_compile_options(fsim_mimalloc PRIVATE -Wno-error -w)
    endif()
    install(FILES
      "${vendor_root}/LICENSE"
      "${vendor_root}/NOTICE"
      "${vendor_root}/SOURCE_MANIFEST.txt"
      "${vendor_root}/mimalloc-${FSIM_MIMALLOC_VERSION}.spdx.json"
      DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party/mimalloc-${FSIM_MIMALLOC_VERSION}")
  endif()
  target_link_libraries(${executable} PRIVATE fsim_mimalloc)
  if(WIN32)
    # Reference the DLL so the loader maps it before the program allocates.
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      target_link_options(${executable} PRIVATE "/INCLUDE:mi_version")
    else()
      target_link_options(${executable} PRIVATE "-Wl,--undefined=mi_version")
    endif()
  endif()
  target_compile_definitions(${executable} PRIVATE FSIM_USES_MIMALLOC=1)
endfunction()
