# SPDX-License-Identifier: Apache-2.0

foreach(variable IN ITEMS
    FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_TARGET_MANIFEST
    FSIM_BRIDGE_LIBRARY FSIM_UPSTREAM_LIBRARY)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "${variable} is required")
  endif()
endforeach()
foreach(input IN ITEMS
    "${FSIM_TARGET_MANIFEST}" "${FSIM_BRIDGE_LIBRARY}"
    "${FSIM_UPSTREAM_LIBRARY}")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "SystemC shared-runtime input is missing: ${input}")
  endif()
endforeach()

include("${FSIM_SOURCE_DIR}/cmake/FsimSystemCAccellera.cmake")
file(READ "${FSIM_TARGET_MANIFEST}" target_manifest)
foreach(token IN ITEMS
    "schema=fsim-systemc-accellera-targets-v1"
    "runtime_identity=systemc-3.0.2-${FSIM_SYSTEMC_ARCHIVE_SHA256}"
    "bridge=fsim_systemc_accellera_runtime"
    "upstream_target=")
  string(FIND "${target_manifest}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "SystemC target manifest omits ${token}")
  endif()
endforeach()
string(REGEX MATCH "target_count=([0-9]+)" target_count_match "${target_manifest}")
if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 LESS 20)
  message(FATAL_ERROR "SystemC target manifest has no bounded target count")
endif()
set(target_count "${CMAKE_MATCH_1}")
file(STRINGS "${FSIM_TARGET_MANIFEST}" target_records REGEX "^target=")
list(LENGTH target_records target_record_count)
if(NOT target_record_count EQUAL target_count)
  message(FATAL_ERROR
    "SystemC target manifest count mismatch: ${target_record_count}/${target_count}")
endif()

set(metadata_root "${FSIM_BINARY_DIR}/_deps/fsim_systemc_3_0_2-build")
foreach(metadata IN ITEMS
    "${FSIM_BINARY_DIR}/SystemCLanguageTargets.cmake"
    "${metadata_root}/SystemCLanguageConfig.cmake"
    "${metadata_root}/SystemCLanguageConfigVersion.cmake"
    "${metadata_root}/SystemCTLMConfig.cmake"
    "${metadata_root}/SystemCTLMConfigVersion.cmake"
    "${metadata_root}/systemc.pc"
    "${metadata_root}/tlm.pc")
  if(NOT EXISTS "${metadata}")
    message(FATAL_ERROR "official SystemC consumer metadata is missing: ${metadata}")
  endif()
endforeach()
file(READ "${metadata_root}/systemc.pc" systemc_pc)
file(READ "${metadata_root}/tlm.pc" tlm_pc)
foreach(token IN ITEMS
    "Name: SystemC" "Version: 3.0.2" "Libs:" "-lsystemc"
    "Cflags:" "-I\${includedir}")
  string(FIND "${systemc_pc}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "systemc.pc omits ${token}")
  endif()
endforeach()
foreach(token IN ITEMS
    "Name: TLM-2.0" "Version: 2.0.6" "Requires: systemc")
  string(FIND "${tlm_pc}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "tlm.pc omits ${token}")
  endif()
endforeach()

file(REAL_PATH "${FSIM_UPSTREAM_LIBRARY}" upstream_real)
get_filename_component(upstream_name "${upstream_real}" NAME)
if(NOT upstream_name MATCHES "systemc" OR upstream_name MATCHES "fsim_systemc")
  message(FATAL_ERROR "unexpected official SystemC runtime path: ${upstream_real}")
endif()
if(NOT WIN32)
  file(GLOB static_candidates
    "${FSIM_BINARY_DIR}/_deps/fsim_systemc_3_0_2-build/src/libsystemc*.a")
  if(static_candidates)
    message(FATAL_ERROR "private/static SystemC runtime copy was built")
  endif()
endif()

file(GET_RUNTIME_DEPENDENCIES
  LIBRARIES "${FSIM_BRIDGE_LIBRARY}"
  RESOLVED_DEPENDENCIES_VAR resolved
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  CONFLICTING_DEPENDENCIES_PREFIX conflicts)
set(systemc_runtime_paths)
foreach(dependency IN LISTS resolved)
  get_filename_component(name "${dependency}" NAME)
  if(name MATCHES "systemc" AND NOT name MATCHES "fsim_systemc")
    file(REAL_PATH "${dependency}" dependency_real)
    list(APPEND systemc_runtime_paths "${dependency_real}")
  endif()
endforeach()
list(REMOVE_DUPLICATES systemc_runtime_paths)
list(LENGTH systemc_runtime_paths runtime_count)
if(NOT runtime_count EQUAL 1)
  message(FATAL_ERROR
    "bridge resolves ${runtime_count} official SystemC runtimes: ${systemc_runtime_paths}")
endif()
list(GET systemc_runtime_paths 0 resolved_runtime)
if(NOT resolved_runtime STREQUAL upstream_real)
  message(FATAL_ERROR
    "bridge runtime differs from governed target: ${resolved_runtime}")
endif()

message(STATUS
  "SystemC shared-runtime contract passed: targets=${target_count} "
  "runtime=${resolved_runtime} CMake=2 pkg-config=2")
