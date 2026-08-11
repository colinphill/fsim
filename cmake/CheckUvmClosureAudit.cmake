# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/uvm_release_closure.tsv")
set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/uvm_conformance_inventory.tsv")
set(FSIM_DIAGNOSTICS "${FSIM_SOURCE_DIR}/docs/diagnostics.md")
set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/uvm-closure-audit.md")
set(FSIM_APPLICATION
  "${FSIM_SOURCE_DIR}/tests/app/uvm_phase_tlm_application_test.cpp")
set(FSIM_INVENTORY_SOURCE
  "${FSIM_SOURCE_DIR}/tests/app/uvm_conformance_inventory.cpp")
set(FSIM_PHASE_DESIGN
  "${FSIM_SOURCE_DIR}/src/app/application_phase_design.cpp")
set(FSIM_RUNNER "${FSIM_SOURCE_DIR}/cmake/RunUvmPhaseTlmExample.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MATRIX}" "${FSIM_INVENTORY}" "${FSIM_DIAGNOSTICS}"
    "${FSIM_AUDIT}" "${FSIM_APPLICATION}" "${FSIM_INVENTORY_SOURCE}"
    "${FSIM_PHASE_DESIGN}" "${FSIM_RUNNER}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "UVM closure-audit input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_MATRIX}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 23)
  message(FATAL_ERROR
    "UVM release closure must contain SPDX, one header, and 21 rows; got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL "contract\tuvm_1_2\tuvm_2020_3_1\tevidence")
  message(FATAL_ERROR "UVM release closure schema changed")
endif()
set(FSIM_CONTRACTS)
foreach(FSIM_INDEX RANGE 2 22)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 4)
    message(FATAL_ERROR "UVM closure row ${FSIM_INDEX} does not have four fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_CONTRACT)
  list(GET FSIM_FIELDS 3 FSIM_EVIDENCE)
  list(FIND FSIM_CONTRACTS "${FSIM_CONTRACT}" FSIM_CONTRACT_INDEX)
  if(NOT FSIM_CONTRACT_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate UVM closure contract: ${FSIM_CONTRACT}")
  endif()
  list(APPEND FSIM_CONTRACTS "${FSIM_CONTRACT}")
  if(NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
    message(FATAL_ERROR
      "UVM closure contract ${FSIM_CONTRACT} lost evidence ${FSIM_EVIDENCE}")
  endif()
endforeach()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "canonical_version\t1.2\t2020.3"
    "legacy_global_controls\tretained\tremoved"
    "legacy_registration_macros\tretained\tremoved"
    "legacy_component_stop_methods\tretained\tremoved"
    "legacy_sequence_library_methods\tretained\tremoved"
    "component_config_compatibility\tretained\tretained"
    "test_done_objection_compatibility\tretained\tretained"
    "ieee_policy_classes\tabsent\tpresent"
    "ieee_object_policy_dispatch\tabsent\tpresent"
    "ieee_report_summary_methods\tabsent\tpresent"
    "governed_class_inventory\t53\t56"
    "project_class_inventory\t27\t27"
    "active_supported_families\t17\t17"
    "exact_stage_count\t10\t10"
    "maximum_rss_kib\t4299292\t4861336"
    "trace_count\t7\t7"
    "trace_bytes\t28345\t28345"
    "trace_sha256\t62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057\t62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057"
    "artifact_provenance\trelease+source+artifact\trelease+source+artifact"
    "cache_provenance\trelease+source+artifact\trelease+source+artifact"
    "unresolved_supported_gaps\t0\t0")
  string(FIND "${FSIM_MATRIX_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM closure matrix lost exact row: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_DIAGNOSTICS}" FSIM_DIAGNOSTIC_CONTENTS)
string(REGEX MATCHALL "FSIM-UVM-[A-Z0-9-]+" FSIM_UVM_CODES
  "${FSIM_DIAGNOSTIC_CONTENTS}")
list(REMOVE_DUPLICATES FSIM_UVM_CODES)
list(LENGTH FSIM_UVM_CODES FSIM_UVM_CODE_COUNT)
if(NOT FSIM_UVM_CODE_COUNT EQUAL 82)
  message(FATAL_ERROR
    "UVM diagnostic inventory changed: expected 82, got ${FSIM_UVM_CODE_COUNT}")
endif()
foreach(FSIM_CODE IN ITEMS
    FSIM-UVM-PHASE-008 FSIM-UVM-OBJ-004 FSIM-UVM-PHASE-007
    FSIM-UVM-SEQ-010 FSIM-UVM-SEQ-011 FSIM-UVM-REG-007
    FSIM-UVM-DEBUG-001 FSIM-UVM-FOREIGN-001 FSIM-UVM-COPY-002
    FSIM-UVM-TLM1-001 FSIM-UVM-TLM2-001 FSIM-UVM-SEQ-002
    FSIM-UVM-PHASE-004 FSIM-UVM-STATE-002 FSIM-UVM-REG-014
    FSIM-UVM-COPY-004 FSIM-UVM-REG-003 FSIM-UVM-REG-005
    FSIM-UVM-REG-009)
  list(FIND FSIM_UVM_CODES "${FSIM_CODE}" FSIM_CODE_INDEX)
  if(FSIM_CODE_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM closure lost diagnostic ${FSIM_CODE}")
  endif()
endforeach()

set(FSIM_COMPOSED_OUTPUT)
foreach(FSIM_GATE IN ITEMS
    CheckDiagnosticCatalog.cmake
    CheckSourceLineBudget.cmake
    CheckV1ConformanceAudit.cmake
    CheckV1InventoryRelease.cmake)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR)
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "UVM closure composed gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
  string(APPEND FSIM_COMPOSED_OUTPUT
    "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}\n")
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "diagnostic catalog covers 2152 production codes"
    "Checked 842 authored sources against the 2500-line hard limit with a 2000-line refactor target"
    "final inventory audit: 2152 diagnostics, 842 bounded sources, 972 SPDX-owned files"
    "v1 conformance audit: 320 authored test/control files")
  string(FIND "${FSIM_COMPOSED_OUTPUT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM closure lost composed evidence: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_APPLICATION}" FSIM_APPLICATION_CONTENTS)
file(READ "${FSIM_INVENTORY_SOURCE}" FSIM_INVENTORY_SOURCE_CONTENTS)
file(READ "${FSIM_PHASE_DESIGN}" FSIM_PHASE_DESIGN_CONTENTS)
file(READ "${FSIM_RUNNER}" FSIM_RUNNER_CONTENTS)
file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "require_uvm_release_closure(release)"
    "closure_audit=compatibility/diagnostics/source/complexity/memory/trace/"
    "artifact/cache gaps=0")
  string(FIND "${FSIM_APPLICATION_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM application lost closure contract: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "uvm_provenance.artifact_identity = project.artifact_identity"
    "uvm_provenance.uvm_release = metadata.uvm_release"
    "uvm_provenance.source_identity = metadata.uvm_source_identity"
    "metadata->cache_key + \":\" + metadata->optimization")
  string(FIND "${FSIM_PHASE_DESIGN_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM artifact/cache provenance lost token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "highest cognitive complexity is 64"
    "maximum loop depth 2"
    "no recursion-in-loop or unguarded recursion"
    "zero unresolved supported gaps")
  string(FIND "${FSIM_AUDIT_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM closure audit lost reviewed evidence: ${FSIM_TOKEN}")
  endif()
endforeach()
string(FIND "${FSIM_RUNNER_CONTENTS}"
  "closure_audit=compatibility/diagnostics/source/complexity/memory/trace/artifact/cache gaps=0"
  FSIM_RUNNER_INDEX)
if(FSIM_RUNNER_INDEX EQUAL -1)
  message(FATAL_ERROR "UVM runner lost closure-audit transcript")
endif()

message(STATUS
  "UVM closure audit: 21 release/behavior rows, 82 UVM diagnostics, 2152 "
  "catalog codes, 842 bounded sources, 972 SPDX files, retained memory/trace/"
  "artifact/cache provenance, and zero unresolved supported gaps")
