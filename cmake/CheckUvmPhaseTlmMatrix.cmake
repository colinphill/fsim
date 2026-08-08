# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_EVIDENCE_FILES
  tests/runtime/runtime_uvm_phase_tests.cpp
  tests/runtime/runtime_uvm_objection_tests.cpp
  tests/runtime/runtime_uvm_quiescence_tests.cpp
  tests/runtime/runtime_uvm_tlm1_tests.cpp
  tests/runtime/runtime_uvm_tlm1_execution_tests.cpp
  tests/runtime/runtime_uvm_analysis_tests.cpp
  tests/runtime/runtime_uvm_tlm2_tests.cpp
  tests/runtime/runtime_uvm_activity_tests.cpp
  tests/runtime/runtime_uvm_checkpoint_tests.cpp
  tests/app/application_test_artifact_phases.cpp
  tests/app/application_test_classes.cpp
  tests/app/uvm_phase_tlm_application_test.cpp
)

set(FSIM_EXECUTABLE_EVIDENCE "")
foreach(FSIM_RELATIVE IN LISTS FSIM_EVIDENCE_FILES)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "UVM phase/TLM matrix input is missing: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  string(APPEND FSIM_EXECUTABLE_EVIDENCE "\n${FSIM_CONTENTS}")
endforeach()

set(FSIM_REQUIRED_DIAGNOSTICS
  FSIM-UVM-PHASE-001 FSIM-UVM-PHASE-002 FSIM-UVM-PHASE-003
  FSIM-UVM-PHASE-004 FSIM-UVM-PHASE-005 FSIM-UVM-PHASE-006
  FSIM-UVM-PHASE-007 FSIM-UVM-PHASE-008
  FSIM-UVM-OBJ-001 FSIM-UVM-OBJ-002 FSIM-UVM-OBJ-003 FSIM-UVM-OBJ-004
  FSIM-UVM-TLM1-001 FSIM-UVM-TLM1-002 FSIM-UVM-TLM1-003
  FSIM-UVM-TLM1-004 FSIM-UVM-TLM1-005 FSIM-UVM-TLM1-006
  FSIM-UVM-TLM1-007 FSIM-UVM-TLM1-008
  FSIM-UVM-TLM2-001 FSIM-UVM-TLM2-002 FSIM-UVM-TLM2-003
  FSIM-UVM-TLM2-004 FSIM-UVM-TLM2-005
  FSIM-UVM-DEBUG-001 FSIM-UVM-DEBUG-002
  FSIM-UVM-ACTIVITY-001 FSIM-UVM-ACTIVITY-002
  FSIM-UVM-FOREIGN-001 FSIM-UVM-FOREIGN-002
  FSIM-UVM-STATE-001 FSIM-UVM-STATE-002
)
foreach(FSIM_CODE IN LISTS FSIM_REQUIRED_DIAGNOSTICS)
  string(FIND "${FSIM_EXECUTABLE_EVIDENCE}" "${FSIM_CODE}" FSIM_FOUND)
  if(FSIM_FOUND EQUAL -1)
    message(FATAL_ERROR
      "UVM phase/TLM negative matrix has no executable assertion for ${FSIM_CODE}")
  endif()
endforeach()

function(fsim_require_token FSIM_RELATIVE FSIM_TOKEN FSIM_DESCRIPTION)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}" FSIM_CONTENTS)
  string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
  if(FSIM_FOUND EQUAL -1)
    message(FATAL_ERROR
      "UVM phase/TLM matrix lost ${FSIM_DESCRIPTION}: ${FSIM_TOKEN}")
  endif()
endfunction()

fsim_require_token(
  tests/runtime/runtime_uvm_phase_tests.cpp
  "synchronize_domains" "domain synchronization evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_phase_tests.cpp
  "execute_synchronized_task_phases" "synchronized execution evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_phase_tests.cpp
  ".jump(" "forward/backward phase-jump evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_objection_tests.cpp
  "maximum_aggregate_drain_time" "aggregate drain ceiling evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_quiescence_tests.cpp
  "ready race" "ready-to-end objection race evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_quiescence_tests.cpp
  "no-work quiescence deadlock" "deadlock cleanup evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_tlm1_execution_tests.cpp
  "blocked_put" "TLM1 FIFO reservation evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_tlm1_execution_tests.cpp
  "complete_transport" "TLM1 timed transport evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_analysis_tests.cpp
  "maximum_analysis_recursion_depth" "analysis recursion ceiling evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_tlm2_tests.cpp
  "nb_transport_fw" "TLM2 nonblocking transport evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_tlm2_tests.cpp
  "get_direct_mem_ptr" "TLM2 direct-memory evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_checkpoint_tests.cpp
  "maximum_external_callbacks" "checkpoint callback-summary ceiling evidence")
fsim_require_token(
  tests/app/application_test_artifact_phases.cpp
  "replay_checkpoints[0] == replay_checkpoints[1]" "restart parity evidence")
fsim_require_token(
  tests/app/application_test_artifact_phases.cpp
  "relocated == interpreted" "artifact relocation evidence")
fsim_require_token(
  tests/app/uvm_phase_tlm_application_test.cpp
  "race=5 deadlock=FSIM-UVM-PHASE-008" "cross-engine race/deadlock transcript")

set(FSIM_RUNNER "${FSIM_SOURCE_DIR}/cmake/RunUvmPhaseTlmExample.cmake")
foreach(FSIM_TOKEN IN ITEMS
    "foreach(optimization IN ITEMS o0 o2)"
    "compiled cold.fst"
    "compiled warm.fst"
    "debug debug.vcd"
    "file(REMOVE_RECURSE")
  fsim_require_token(
    cmake/RunUvmPhaseTlmExample.cmake "${FSIM_TOKEN}"
    "direct/artifact/engine/cache runner coverage")
endforeach()

fsim_require_token(
  tests/CMakeLists.txt "fsim.application.uvm_phase_tlm.1_2"
  "UVM 1.2 matrix registration")
fsim_require_token(
  tests/CMakeLists.txt "fsim.application.uvm_phase_tlm.2020_3_1"
  "UVM 2020.3.1 matrix registration")

message(STATUS
  "UVM phase/TLM matrix: 33 diagnostics and aggregate execution contracts verified")
