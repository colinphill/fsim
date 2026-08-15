# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

set(FSIM_SYSTEMC_SOURCE_SCHEMA "fsim-systemc-accellera-source-v1")
set(FSIM_SYSTEMC_VERSION "3.0.2")
set(FSIM_SYSTEMC_TLM_VERSION "2.0.6")
set(FSIM_SYSTEMC_RELEASE_DATE "2025-10-31")
set(FSIM_SYSTEMC_RELEASE_TAG "3.0.2")
set(FSIM_SYSTEMC_UPSTREAM_COMMIT
    "70b0fc8e4a74acc677b0fc73cea08f940c2115d5")
set(FSIM_SYSTEMC_UPSTREAM_URL
    "https://github.com/accellera-official/systemc/archive/refs/tags/3.0.2.tar.gz")
set(FSIM_SYSTEMC_ARCHIVE_NAME "systemc-3.0.2.tar.gz")
set(FSIM_SYSTEMC_ARCHIVE_SIZE 4573759)
set(FSIM_SYSTEMC_ARCHIVE_SHA256
    "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4")
set(FSIM_SYSTEMC_SOURCE_ROOT "systemc-3.0.2")
set(FSIM_SYSTEMC_TREE_FILE_COUNT 4456)
set(FSIM_SYSTEMC_TREE_SHA256
    "b1fbb8b7bcb76e3f803155585f2c3e1073e4c990de72302040cfab283c9f9fd0")
set(FSIM_SYSTEMC_LICENSE_EXPRESSION "Apache-2.0")
set(FSIM_SYSTEMC_SBOM_PURL
    "pkg:github/accellera-official/systemc@3.0.2")
set(
  FSIM_SYSTEMC_REQUIRED_FILES
  "LICENSE|cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30"
  "NOTICE|65f4baf4b72004c7984afdb798e4b506d2c9f82d0c6cb68e07feb0464a111ab1"
  "CMakeLists.txt|9a1c308e162c2e3bf28f7efd658816b49c59500b81ed81b66dbc18d78924049d"
  "src/systemc.h|9240003b0f96e6380cc017c49f37b9ef8c266d9455ca7dcb3831be6ced1ae866"
  "src/tlm.h|4ffcc8d678620f282ee66ea89c0d23a0803f6403577f5f03de19ad9c24565412"
  "src/sysc/kernel/sc_ver.h|c38d5cff6f03a46ac51b7bed3596e40335f593d8229ba9fcc06bea8775877c4f"
)

function(fsim_systemc_validate_source_metadata)
  foreach(variable IN ITEMS
      FSIM_SYSTEMC_SOURCE_SCHEMA FSIM_SYSTEMC_VERSION FSIM_SYSTEMC_TLM_VERSION
      FSIM_SYSTEMC_RELEASE_DATE FSIM_SYSTEMC_RELEASE_TAG
      FSIM_SYSTEMC_UPSTREAM_COMMIT FSIM_SYSTEMC_UPSTREAM_URL
      FSIM_SYSTEMC_ARCHIVE_NAME FSIM_SYSTEMC_ARCHIVE_SIZE
      FSIM_SYSTEMC_ARCHIVE_SHA256 FSIM_SYSTEMC_SOURCE_ROOT
      FSIM_SYSTEMC_TREE_FILE_COUNT FSIM_SYSTEMC_TREE_SHA256
      FSIM_SYSTEMC_LICENSE_EXPRESSION FSIM_SYSTEMC_SBOM_PURL
      FSIM_SYSTEMC_REQUIRED_FILES)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
      message(FATAL_ERROR "missing governed SystemC metadata: ${variable}")
    endif()
  endforeach()
  if(NOT FSIM_SYSTEMC_VERSION STREQUAL "3.0.2" OR
     NOT FSIM_SYSTEMC_RELEASE_TAG STREQUAL FSIM_SYSTEMC_VERSION OR
     NOT FSIM_SYSTEMC_SOURCE_ROOT STREQUAL "systemc-3.0.2" OR
     NOT FSIM_SYSTEMC_ARCHIVE_NAME STREQUAL "systemc-3.0.2.tar.gz")
    message(FATAL_ERROR "governed SystemC release identity is not 3.0.2")
  endif()
  if(NOT FSIM_SYSTEMC_UPSTREAM_URL MATCHES
       "^https://github\\.com/accellera-official/systemc/")
    message(FATAL_ERROR "SystemC source URL is not the official Accellera repository")
  endif()
  foreach(digest IN ITEMS
      "${FSIM_SYSTEMC_UPSTREAM_COMMIT}"
      "${FSIM_SYSTEMC_ARCHIVE_SHA256}"
      "${FSIM_SYSTEMC_TREE_SHA256}")
    if(NOT digest MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "invalid SystemC provenance digest: ${digest}")
    endif()
  endforeach()
  string(LENGTH "${FSIM_SYSTEMC_UPSTREAM_COMMIT}" commit_length)
  string(LENGTH "${FSIM_SYSTEMC_ARCHIVE_SHA256}" archive_digest_length)
  string(LENGTH "${FSIM_SYSTEMC_TREE_SHA256}" tree_digest_length)
  if(NOT commit_length EQUAL 40 OR NOT archive_digest_length EQUAL 64 OR
     NOT tree_digest_length EQUAL 64)
    message(FATAL_ERROR "invalid SystemC provenance digest length")
  endif()
  if(NOT FSIM_SYSTEMC_ARCHIVE_SIZE MATCHES "^[1-9][0-9]*$" OR
     NOT FSIM_SYSTEMC_TREE_FILE_COUNT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "invalid SystemC archive or tree size")
  endif()
  list(LENGTH FSIM_SYSTEMC_REQUIRED_FILES required_count)
  if(NOT required_count EQUAL 6)
    message(FATAL_ERROR "expected six governed SystemC source entry points")
  endif()
  foreach(entry IN LISTS FSIM_SYSTEMC_REQUIRED_FILES)
    string(REPLACE "|" ";" fields "${entry}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 2)
      message(FATAL_ERROR "malformed SystemC source identity: ${entry}")
    endif()
    list(GET fields 0 relative_path)
    list(GET fields 1 digest)
    if(relative_path MATCHES "(^/|(^|/)\\.\\.(/|$))" OR
       NOT digest MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "unsafe SystemC source identity: ${entry}")
    endif()
    string(LENGTH "${digest}" digest_length)
    if(NOT digest_length EQUAL 64)
      message(FATAL_ERROR "invalid SystemC source digest length: ${entry}")
    endif()
  endforeach()
endfunction()

function(fsim_systemc_archive_error archive output_error)
  set(error "")
  if(NOT EXISTS "${archive}")
    set(error "official SystemC 3.0.2 archive is missing: ${archive}")
  else()
    file(SIZE "${archive}" actual_size)
    if(NOT actual_size EQUAL FSIM_SYSTEMC_ARCHIVE_SIZE)
      set(error
          "SystemC archive size mismatch: expected ${FSIM_SYSTEMC_ARCHIVE_SIZE}, found ${actual_size}")
    else()
      file(SHA256 "${archive}" actual_digest)
      if(NOT actual_digest STREQUAL FSIM_SYSTEMC_ARCHIVE_SHA256)
        set(error "SystemC archive SHA-256 mismatch: ${actual_digest}")
      endif()
    endif()
  endif()
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_systemc_validate_archive archive)
  fsim_systemc_validate_source_metadata()
  fsim_systemc_archive_error("${archive}" error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}")
  endif()
endfunction()

function(fsim_systemc_compute_tree_identity root output_count output_digest)
  if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "SystemC source root is not a directory: ${root}")
  endif()
  file(GLOB_RECURSE files LIST_DIRECTORIES FALSE RELATIVE "${root}" "${root}/*")
  list(SORT files)
  set(canonical "")
  foreach(relative_path IN LISTS files)
    if(relative_path MATCHES "(^|/)\\.\\.(/|$)")
      message(FATAL_ERROR "unsafe extracted SystemC path: ${relative_path}")
    endif()
    file(SHA256 "${root}/${relative_path}" digest)
    string(APPEND canonical "${digest}  ${relative_path}\n")
  endforeach()
  list(LENGTH files file_count)
  string(SHA256 tree_digest "${canonical}")
  set(${output_count} "${file_count}" PARENT_SCOPE)
  set(${output_digest} "${tree_digest}" PARENT_SCOPE)
endfunction()

function(fsim_systemc_source_tree_error root output_error)
  set(error "")
  get_filename_component(root_name "${root}" NAME)
  if(NOT root_name STREQUAL FSIM_SYSTEMC_SOURCE_ROOT)
    set(error
        "wrong SystemC source version/root: expected ${FSIM_SYSTEMC_SOURCE_ROOT}, found ${root_name}")
  elseif(NOT IS_DIRECTORY "${root}")
    set(error "SystemC source root is missing: ${root}")
  else()
    foreach(entry IN LISTS FSIM_SYSTEMC_REQUIRED_FILES)
      string(REPLACE "|" ";" fields "${entry}")
      list(GET fields 0 relative_path)
      list(GET fields 1 expected_digest)
      if(NOT EXISTS "${root}/${relative_path}")
        set(error "incomplete SystemC source tree omits ${relative_path}")
        break()
      endif()
      file(SHA256 "${root}/${relative_path}" actual_digest)
      if(NOT actual_digest STREQUAL expected_digest)
        set(error "SystemC source entry changed: ${relative_path}")
        break()
      endif()
    endforeach()
    if(error STREQUAL "")
      fsim_systemc_compute_tree_identity("${root}" actual_count actual_digest)
      if(NOT actual_count EQUAL FSIM_SYSTEMC_TREE_FILE_COUNT OR
         NOT actual_digest STREQUAL FSIM_SYSTEMC_TREE_SHA256)
        set(error
            "SystemC source tree mismatch: files=${actual_count}, SHA256=${actual_digest}")
      endif()
    endif()
  endif()
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_systemc_validate_source_tree root)
  fsim_systemc_validate_source_metadata()
  fsim_systemc_source_tree_error("${root}" error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}")
  endif()
endfunction()

function(fsim_systemc_work_root_error work_root output_root output_error)
  if(work_root STREQUAL "")
    set(${output_root} "" PARENT_SCOPE)
    set(${output_error} "the SystemC work root is empty" PARENT_SCOPE)
    return()
  endif()
  if(DEFINED FSIM_BINARY_DIR AND NOT FSIM_BINARY_DIR STREQUAL "")
    set(binary_root "${FSIM_BINARY_DIR}")
  else()
    set(binary_root "${CMAKE_BINARY_DIR}")
  endif()
  set(candidate "${work_root}")
  cmake_path(ABSOLUTE_PATH candidate BASE_DIRECTORY "${binary_root}"
             NORMALIZE OUTPUT_VARIABLE normalized)
  set(error "")
  if(DEFINED FSIM_SOURCE_DIR AND NOT FSIM_SOURCE_DIR STREQUAL "")
    set(source_root "${FSIM_SOURCE_DIR}")
    cmake_path(NORMAL_PATH source_root OUTPUT_VARIABLE source_root)
    cmake_path(IS_PREFIX source_root "${normalized}" NORMALIZE inside_source)
    cmake_path(IS_PREFIX binary_root "${normalized}" NORMALIZE inside_binary)
    if(inside_source AND NOT inside_binary)
      set(error "the SystemC work root must remain outside the source tree")
    endif()
  endif()
  set(${output_root} "${normalized}" PARENT_SCOPE)
  set(${output_error} "${error}" PARENT_SCOPE)
endfunction()

function(fsim_systemc_manifest_contents root archive output_contents)
  fsim_systemc_compute_tree_identity("${root}" tree_count tree_digest)
  set(contents "schema=${FSIM_SYSTEMC_SOURCE_SCHEMA}\n")
  string(APPEND contents "name=SystemC\nversion=${FSIM_SYSTEMC_VERSION}\n")
  string(APPEND contents "release_tag=${FSIM_SYSTEMC_RELEASE_TAG}\n")
  string(APPEND contents "upstream_commit=${FSIM_SYSTEMC_UPSTREAM_COMMIT}\n")
  string(APPEND contents "upstream_url=${FSIM_SYSTEMC_UPSTREAM_URL}\n")
  string(APPEND contents "archive=${archive}\n")
  string(APPEND contents "archive_size=${FSIM_SYSTEMC_ARCHIVE_SIZE}\n")
  string(APPEND contents "archive_sha256=${FSIM_SYSTEMC_ARCHIVE_SHA256}\n")
  string(APPEND contents "source_root=${root}\n")
  string(APPEND contents "tree_files=${tree_count}\ntree_sha256=${tree_digest}\n")
  string(APPEND contents "license=${FSIM_SYSTEMC_LICENSE_EXPRESSION}\n")
  string(APPEND contents "purl=${FSIM_SYSTEMC_SBOM_PURL}\n")
  foreach(entry IN LISTS FSIM_SYSTEMC_REQUIRED_FILES)
    string(APPEND contents "entry=${entry}\n")
  endforeach()
  set(${output_contents} "${contents}" PARENT_SCOPE)
endfunction()

function(fsim_systemc_configure_pkgconfig source_root binary_root)
  set(TARGET_ARCH "${SystemC_TARGET_ARCH}")
  # Keep the installed metadata relocatable even when `cmake --install
  # --prefix` overrides the configure-time prefix.
  set(prefix "\${pcfiledir}/../..")
  set(exec_prefix "\${prefix}")
  set(libdir "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
  set(LIB_ARCH_SUFFIX "")
  set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
  set(PACKAGE_NAME "SystemC")
  set(PACKAGE_VERSION "${FSIM_SYSTEMC_VERSION}")
  set(PACKAGE_URL "https://www.accellera.org/downloads/standards/systemc")
  set(PACKAGE "systemc")
  set(PKGCONFIG_LDPRIV "")
  set(PKGCONFIG_CFLAGS "")
  set(PKGCONFIG_DEFINES "")
  set(TLM_PACKAGE_VERSION "${FSIM_SYSTEMC_TLM_VERSION}")
  configure_file(
    "${source_root}/src/systemc.pc.in" "${binary_root}/systemc.pc" @ONLY)
  configure_file(
    "${source_root}/src/tlm.pc.in" "${binary_root}/tlm.pc" @ONLY)
  install(
    FILES "${binary_root}/systemc.pc" "${binary_root}/tlm.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    COMPONENT dev)
endfunction()

function(fsim_systemc_apply_runtime_fixes target source_root)
  set(upstream_source "${source_root}/src/sysc/kernel/sc_simcontext.cpp")
  file(READ "${upstream_source}" patched_contents)
  set(old_order [=[    delete m_cor_pkg;
    delete m_time_params;
    delete m_collectable;
    delete m_runnable;
    delete m_null_event_p;
    delete m_timed_events;
    delete m_process_table;]=])
  set(new_order [=[    // A suspended thread owns a coroutine allocated by m_cor_pkg. Destroy
    // every remaining process and coroutine before releasing that package.
    delete m_process_table;
    delete m_cor_pkg;
    delete m_time_params;
    delete m_collectable;
    delete m_runnable;
    delete m_null_event_p;
    delete m_timed_events;]=])
  set(original_contents "${patched_contents}")
  string(REPLACE "${old_order}" "${new_order}"
    patched_contents "${patched_contents}")
  if(patched_contents STREQUAL original_contents)
    message(FATAL_ERROR "cannot apply the governed SystemC process teardown fix")
  endif()
  set(patched_root "${CMAKE_BINARY_DIR}/generated/systemc-runtime-fixes")
  file(MAKE_DIRECTORY "${patched_root}")
  set(patched_source "${patched_root}/sc_simcontext.cpp")
  set(write_patched_source TRUE)
  if(EXISTS "${patched_source}")
    file(READ "${patched_source}" current_patched_contents)
    if(current_patched_contents STREQUAL patched_contents)
      set(write_patched_source FALSE)
    endif()
  endif()
  if(write_patched_source)
    file(WRITE "${patched_source}" "${patched_contents}")
  endif()

  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
     CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(upstream_common_header
        "${source_root}/src/sysc/kernel/sc_cmnhdr.h")
    file(READ "${upstream_common_header}" patched_header_contents)
    set(old_template_condition
        "#if defined(SC_BUILD) && defined(_MSC_VER)")
    set(new_template_condition
        "#if defined(SC_BUILD) && defined(_MSC_VER) && !defined(__clang__)")
    string(FIND "${patched_header_contents}" "${old_template_condition}"
      template_condition_offset)
    if(template_condition_offset EQUAL -1)
      message(FATAL_ERROR
        "cannot apply the governed SystemC clang-cl template fix")
    endif()
    string(REPLACE "${old_template_condition}" "${new_template_condition}"
      patched_header_contents "${patched_header_contents}")
    set(patched_common_header "${patched_root}/sc_cmnhdr.h")
    file(WRITE "${patched_common_header}" "${patched_header_contents}")
    # Upstream spells its normal warning level as `-Wall`. clang-cl interprets
    # that spelling as MSVC's `/Wall`, enabling every off-by-default Clang
    # diagnostic and producing tens of thousands of warnings in pristine
    # Accellera sources. Silence only this governed third-party runtime; fsim
    # targets and consumers retain their own `/W4 /WX` policy.
    target_compile_options(
      "${target}" PRIVATE "/FI${patched_common_header}" /W0)
  endif()

  get_target_property(runtime_sources "${target}" SOURCES)
  set(found_source FALSE)
  set(filtered_sources "")
  foreach(source IN LISTS runtime_sources)
    if(source MATCHES "(^|/)sc_simcontext\\.cpp$")
      set(found_source TRUE)
    else()
      list(APPEND filtered_sources "${source}")
    endif()
  endforeach()
  if(NOT found_source)
    message(FATAL_ERROR "official SystemC target omits sc_simcontext.cpp")
  endif()
  set_property(TARGET "${target}" PROPERTY SOURCES "${filtered_sources}")
  target_sources("${target}" PRIVATE "${patched_source}")
endfunction()

function(fsim_systemc_materialize_source archive work_root output_root output_manifest)
  fsim_systemc_validate_archive("${archive}")
  fsim_systemc_work_root_error("${work_root}" work_root error)
  if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}: ${work_root}")
  endif()
  set(source_parent "${work_root}/sources")
  set(root "${source_parent}/${FSIM_SYSTEMC_SOURCE_ROOT}")
  if(NOT EXISTS "${root}")
    set(staging "${work_root}/extract-${FSIM_SYSTEMC_ARCHIVE_SHA256}")
    file(REMOVE_RECURSE "${staging}")
    file(MAKE_DIRECTORY "${staging}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${staging}")
    fsim_systemc_validate_source_tree("${staging}/${FSIM_SYSTEMC_SOURCE_ROOT}")
    file(MAKE_DIRECTORY "${source_parent}")
    file(RENAME "${staging}/${FSIM_SYSTEMC_SOURCE_ROOT}" "${root}")
    file(REMOVE_RECURSE "${staging}")
  endif()
  fsim_systemc_validate_source_tree("${root}")
  file(MAKE_DIRECTORY "${work_root}/manifests")
  set(manifest "${work_root}/manifests/systemc-3.0.2.txt")
  fsim_systemc_manifest_contents("${root}" "${archive}" contents)
  file(WRITE "${manifest}" "${contents}")
  set(${output_root} "${root}" PARENT_SCOPE)
  set(${output_manifest} "${manifest}" PARENT_SCOPE)
endfunction()

macro(fsim_systemc_add_official_runtime archive work_root)
  if(TARGET SystemC::systemc OR TARGET systemc)
    message(FATAL_ERROR "a SystemC runtime target already exists before the governed runtime")
  endif()
  fsim_systemc_materialize_source(
    "${archive}" "${work_root}"
    FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR FSIM_SYSTEMC_SOURCE_MANIFEST)

  # These are upstream's own supported build controls. They are fixed before
  # add_subdirectory so the one in-tree target is always shared, offline, and
  # free of examples/regressions during normal fsim builds.
  set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(ENABLE_EXAMPLES OFF CACHE BOOL "Build SystemC examples" FORCE)
  set(ENABLE_REGRESSION OFF CACHE BOOL "Build SystemC regressions" FORCE)
  set(BUILD_SOURCE_DOCUMENTATION OFF CACHE BOOL
      "Build SystemC source documentation" FORCE)
  set(INSTALL_TO_LIB_BUILD_TYPE_DIR OFF CACHE BOOL
      "Install SystemC without build-type library directories" FORCE)
  set(INSTALL_TO_LIB_TARGET_ARCH_DIR OFF CACHE BOOL
      "Install SystemC without target-architecture library directories" FORCE)
  set(INSTALL_LIB_TARGET_ARCH_SYMLINK OFF CACHE BOOL
      "Do not install a SystemC target-architecture symlink" FORCE)
  set(SYSTEMC_UNITY_BUILD OFF CACHE BOOL "Disable SystemC unity build" FORCE)
  set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
  add_subdirectory(
    "${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/fsim_systemc_3_0_2-build")

  if(NOT TARGET SystemC::systemc OR NOT TARGET systemc)
    message(FATAL_ERROR "official SystemC source did not define SystemC::systemc")
  endif()
  get_target_property(
    FSIM_SYSTEMC_UPSTREAM_INCLUDE_DIRECTORIES
    systemc INTERFACE_INCLUDE_DIRECTORIES)
  set_property(
    TARGET systemc APPEND PROPERTY
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        "${FSIM_SYSTEMC_UPSTREAM_INCLUDE_DIRECTORIES}")
  if(WIN32)
    set(FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET "systemc-3.0.2")
    if(NOT TARGET "${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}")
      message(FATAL_ERROR "official Windows SystemC shared runtime target is missing")
    endif()
  else()
    set(FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET systemc)
  endif()
  fsim_systemc_apply_runtime_fixes(
    "${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}"
    "${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}")
  # SystemC intentionally converts a user-module member pointer to its
  # unambiguous sc_process_host base for SC_METHOD/SC_THREAD dispatch. GCC's
  # vptr sanitizer diagnoses that standard conversion at the base call site,
  # even though the dynamic object is the original module. Keep every other
  # undefined-behavior check enabled while excluding this upstream dispatch.
  if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$" AND
     CMAKE_CXX_FLAGS MATCHES "(^|[ ])-fsanitize=[^ ]*undefined")
    target_compile_options(
      "${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}"
      PRIVATE -fno-sanitize=vptr)
  endif()
  get_target_property(
    FSIM_SYSTEMC_UPSTREAM_RUNTIME_TYPE
    "${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}" TYPE)
  if(NOT FSIM_SYSTEMC_UPSTREAM_RUNTIME_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR
      "official SystemC runtime is not shared: ${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TYPE}")
  endif()
  if(NOT WIN32)
    set_target_properties(
      "${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}"
      PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()
  get_target_property(FSIM_SYSTEMC_ALIAS_TARGET SystemC::systemc ALIASED_TARGET)
  if(NOT FSIM_SYSTEMC_ALIAS_TARGET STREQUAL "systemc")
    message(FATAL_ERROR "SystemC::systemc does not name the one official target")
  endif()

  fsim_systemc_configure_pkgconfig(
    "${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/fsim_systemc_3_0_2-build")

  set(FSIM_SYSTEMC_RUNTIME_IDENTITY
      "systemc-${FSIM_SYSTEMC_VERSION}-${FSIM_SYSTEMC_ARCHIVE_SHA256}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-cxx20")
endmacro()

function(fsim_systemc_collect_build_targets directory output_targets)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(subdirectory IN LISTS subdirectories)
    fsim_systemc_collect_build_targets("${subdirectory}" child_targets)
    list(APPEND targets ${child_targets})
  endforeach()
  set(${output_targets} "${targets}" PARENT_SCOPE)
endfunction()

function(fsim_systemc_write_runtime_target_manifest output_path)
  fsim_systemc_collect_build_targets("${CMAKE_SOURCE_DIR}" targets)
  list(REMOVE_DUPLICATES targets)
  list(SORT targets)
  set(records "schema=fsim-systemc-accellera-targets-v1\n")
  string(APPEND records
    "runtime_identity=${FSIM_SYSTEMC_RUNTIME_IDENTITY}\n"
    "upstream_target=${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}\n")
  set(fsim_target_count 0)
  foreach(target IN LISTS targets)
    if(NOT target MATCHES "^fsim($|[-_])" OR
       target STREQUAL "fsim_systemc_accellera_runtime")
      continue()
    endif()
    get_target_property(imported "${target}" IMPORTED)
    if(imported)
      continue()
    endif()
    get_target_property(type "${target}" TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY" OR type STREQUAL "OBJECT_LIBRARY" OR
       type STREQUAL "UTILITY")
      continue()
    endif()
    get_target_property(links "${target}" LINK_LIBRARIES)
    if(links STREQUAL "links-NOTFOUND")
      set(links "")
    endif()
    get_target_property(interface_links "${target}" INTERFACE_LINK_LIBRARIES)
    if(interface_links STREQUAL "interface_links-NOTFOUND")
      set(interface_links "")
    endif()
    set(all_links ${links} ${interface_links})
    list(FIND all_links fsim_systemc_accellera_runtime runtime_index)
    if(runtime_index EQUAL -1)
      message(FATAL_ERROR
        "fsim target does not link the one shared Accellera runtime: ${target}")
    endif()
    math(EXPR fsim_target_count "${fsim_target_count} + 1")
    string(APPEND records
      "target=${target}|type=${type}|bridge=fsim_systemc_accellera_runtime\n")
  endforeach()
  if(fsim_target_count LESS 20)
    message(FATAL_ERROR
      "unexpectedly small fsim target inventory: ${fsim_target_count}")
  endif()
  string(APPEND records "target_count=${fsim_target_count}\n")
  file(WRITE "${output_path}" "${records}")
  set(FSIM_SYSTEMC_RUNTIME_TARGET_COUNT "${fsim_target_count}" PARENT_SCOPE)
endfunction()
