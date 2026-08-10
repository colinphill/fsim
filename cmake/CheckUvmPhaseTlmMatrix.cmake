# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_EVIDENCE_FILES
  tests/runtime/runtime_class_heap_tests.cpp
  tests/runtime/runtime_uvm_phase_tests.cpp
  tests/runtime/runtime_uvm_objection_tests.cpp
  tests/runtime/runtime_uvm_quiescence_tests.cpp
  tests/runtime/runtime_uvm_tlm1_tests.cpp
  tests/runtime/runtime_uvm_tlm1_execution_tests.cpp
  tests/runtime/runtime_uvm_analysis_tests.cpp
  tests/runtime/runtime_uvm_tlm2_tests.cpp
  tests/runtime/runtime_uvm_activity_tests.cpp
  tests/runtime/runtime_uvm_checkpoint_tests.cpp
  tests/runtime/runtime_uvm_object_policy_tests.cpp
  tests/runtime/runtime_uvm_packer_tests.cpp
  tests/runtime/runtime_uvm_synchronization_policy_tests.cpp
  tests/runtime/runtime_uvm_sequence_tests.cpp
  tests/runtime/runtime_uvm_sequence_access_tests.cpp
  tests/runtime/runtime_uvm_sequence_handshake_tests.cpp
  tests/runtime/runtime_uvm_sequence_macro_tests.cpp
  tests/runtime/runtime_uvm_sequence_role_tests.cpp
  tests/runtime/runtime_uvm_sequence_virtual_tests.cpp
  tests/runtime/runtime_uvm_callback_tests.cpp
  tests/runtime/runtime_uvm_register_model_tests.cpp
  tests/runtime/runtime_uvm_register_value_tests.cpp
  tests/runtime/runtime_uvm_register_map_tests.cpp
  tests/runtime/runtime_uvm_register_frontdoor_tests.cpp
  tests/runtime/runtime_uvm_register_backdoor_tests.cpp
  tests/runtime/runtime_uvm_register_sequence_tests.cpp
  tests/runtime/runtime_uvm_register_coverage_tests.cpp
  tests/runtime/runtime_uvm_command_line_tests.cpp
  tests/runtime/runtime_uvm_test_runner_tests.cpp
  tests/app/application_test_artifact_phases.cpp
  tests/app/application_test_classes.cpp
  tests/app/uvm_phase_tlm_application_test.cpp
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  tests/app/uvm_phase_tlm_register_probe.cpp
  tests/fixtures/systemverilog/uvm_core_smoke_probe.sv
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
  FSIM-UVM-COPY-001 FSIM-UVM-COPY-002 FSIM-UVM-COPY-003
  FSIM-UVM-COPY-004 FSIM-UVM-COPY-005
  FSIM-UVM-PACK-001 FSIM-UVM-PACK-002
  FSIM-UVM-SYNC-001 FSIM-UVM-SYNC-002
  FSIM-UVM-CMD-001 FSIM-UVM-CMD-002
  FSIM-UVM-RUN-001 FSIM-UVM-RUN-002 FSIM-UVM-RUN-003
  FSIM-UVM-POLICY-001
  FSIM-UVM-OBJ-001 FSIM-UVM-OBJ-002 FSIM-UVM-OBJ-003 FSIM-UVM-OBJ-004
  FSIM-UVM-TLM1-001 FSIM-UVM-TLM1-002 FSIM-UVM-TLM1-003
  FSIM-UVM-TLM1-004 FSIM-UVM-TLM1-005 FSIM-UVM-TLM1-006
  FSIM-UVM-TLM1-007 FSIM-UVM-TLM1-008
  FSIM-UVM-TLM2-001 FSIM-UVM-TLM2-002 FSIM-UVM-TLM2-003
  FSIM-UVM-TLM2-004 FSIM-UVM-TLM2-005
  FSIM-UVM-SEQ-001 FSIM-UVM-SEQ-002 FSIM-UVM-SEQ-003
  FSIM-UVM-SEQ-004 FSIM-UVM-SEQ-005 FSIM-UVM-SEQ-006
  FSIM-UVM-SEQ-007 FSIM-UVM-SEQ-008 FSIM-UVM-SEQ-009
  FSIM-UVM-SEQ-010 FSIM-UVM-SEQ-011 FSIM-UVM-SEQ-012
  FSIM-UVM-SEQ-013
  FSIM-UVM-DEBUG-001 FSIM-UVM-DEBUG-002
  FSIM-UVM-ACTIVITY-001 FSIM-UVM-ACTIVITY-002
  FSIM-UVM-CALLBACK-001 FSIM-UVM-CALLBACK-002
  FSIM-UVM-TR-001 FSIM-UVM-TR-002
  FSIM-UVM-REG-001 FSIM-UVM-REG-002 FSIM-UVM-REG-003
  FSIM-UVM-REG-004 FSIM-UVM-REG-005 FSIM-UVM-REG-006
  FSIM-UVM-REG-007 FSIM-UVM-REG-008 FSIM-UVM-REG-009
  FSIM-UVM-REG-010 FSIM-UVM-REG-011 FSIM-UVM-REG-012
  FSIM-UVM-REG-013 FSIM-UVM-REG-014
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
  tests/runtime/runtime_uvm_object_policy_tests.cpp
  "SystemVerilogUvmPrinterKind::Table" "printer/comparer policy evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_packer_tests.cpp
  "SystemVerilogUvmPackerEndian::Little" "packer policy evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_synchronization_policy_tests.cpp
  "SystemVerilogUvmHeartbeatMode::All" "synchronization policy evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_command_line_tests.cpp
  "get_arg_values" "command-line processor query evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_test_runner_tests.cpp
  "SystemVerilogUvmRunStatus::TimedOut" "run_test lifecycle evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_sequence_tests.cpp
  "StrictRandom" "complete sequence arbitration evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_sequence_access_tests.cpp
  "request_grab" "sequence lock/grab evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_sequence_handshake_tests.cpp
  "get_next_item" "driver handshake evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_sequence_role_tests.cpp
  "Scoreboard" "agent/monitor/scoreboard evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_sequence_virtual_tests.cpp
  "coordinate_virtual" "virtual sequence evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_callback_tests.cpp
  "uvm_transaction_callback" "callback/transaction evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_register_map_tests.cpp
  "BigFifo" "complete register-map endian evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_register_frontdoor_tests.cpp
  "register_predictor" "adapter/predictor evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_register_backdoor_tests.cpp
  "SystemVerilogUvmRegisterHdlKind::Vhpi" "VPI/VHPI backdoor evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_register_sequence_tests.cpp
  "Kind::Access"
  "standard register-sequence evidence")
fsim_require_token(
  tests/runtime/runtime_uvm_register_coverage_tests.cpp
  "register_coverage_model" "register callback/coverage evidence")
fsim_require_token(
  tests/app/application_test_artifact_phases.cpp
  "replay_checkpoints[0] == replay_checkpoints[1]" "restart parity evidence")
fsim_require_token(
  tests/app/application_test_artifact_phases.cpp
  "relocated == interpreted" "artifact relocation evidence")
fsim_require_token(
  tests/app/uvm_phase_tlm_application_test.cpp
  "race=5 deadlock=FSIM-UVM-PHASE-008" "cross-engine race/deadlock transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "sequence=arb/lock/response/virtual" "exact sequence environment transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "copier=deep/shallow/reference" "exact object copier policy transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "packer=big/little/metadata/unpack recorder=object/replay"
  "exact packer and transaction recorder policy transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "sync=event/pool/barrier/queue/heartbeat/spell"
  "exact synchronization policy transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "cmdline=args/plus/uvm/exact/prefix/value/tool/isolation"
  "exact command-line processor transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "run_test=select/topology/timeout/seed/repeat/finish/fatal"
  "exact run_test lifecycle transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "report=verbosity/severity/action/file/catcher/phase/time"
  "exact report-control transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "objection_trace=on/bounded"
  "exact objection-trace transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "tracing=factory/config/resource/debug/activity"
  "exact configuration-tracing transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "legacy_macros=field/object/component/sequence/registry/callback/report"
  "exact UVM 1.2 legacy-macro transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "legacy_api=phase/objection/tlm/sequence/callback/register/policy/"
  "exact UVM 1.2 legacy-API transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "uvm2020_api=policy/field_op/copier/object/printer/comparer/packer/"
  "exact UVM 2020-3.1 API transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "uvm_release=selected/provenance/object/design/cache/checkpoint/"
  "exact dual-release provenance transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "core_smoke=governed/project/object/factory/resource/config/cmdline/"
  "exact core smoke-suite transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_sequence_probe.cpp
  "flow_smoke=phase/objection/sequence/sequencer/roles/virtual/tlm1/"
  "exact phase/sequence/TLM flow-smoke transcript")
foreach(FSIM_SUITE IN ITEMS
    object_policy factory resource configuration command_line reporting
    callback test_selection topology timeout seed)
  fsim_require_token(
    tests/fixtures/systemverilog/uvm_core_smoke_probe.sv
    "${FSIM_SUITE}_smoke"
    "project-owned ${FSIM_SUITE} smoke suite")
endforeach()
fsim_require_token(
  tests/app/uvm_phase_tlm_register_probe.cpp
  "invalid_width.status == SystemVerilogUvmRegisterOperationStatus::NotOk"
  "exact register negative-operation evidence")
fsim_require_token(
  tests/app/uvm_phase_tlm_application_test.cpp
  "register_smoke=block/map/field/memory/adapter/predictor/frontdoor/"
  "exact register/foreign smoke-suite transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_application_test.cpp
  "platform_contract=source/abi/linux/windows/cdecl/filesystem"
  "exact source/ABI portability transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_application_test.cpp
  "conformance_inventory=standard/project families=17"
  "exact governed/project conformance transcript")
fsim_require_token(
  tests/app/uvm_phase_tlm_application_test.cpp
  "closure_audit=compatibility/diagnostics/source/complexity/memory/trace/"
  "exact UVM closure-audit transcript")

set(FSIM_RUNNER "${FSIM_SOURCE_DIR}/cmake/RunUvmPhaseTlmExample.cmake")
foreach(FSIM_TOKEN IN ITEMS
    "foreach(optimization IN ITEMS o0 o2)"
    "compiled cold.fst"
    "compiled warm.fst"
    "debug debug.vcd"
    "TIMEOUT \"\${FSIM_STAGE_TIMEOUT_SECONDS}\""
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
  "UVM matrix: 79 diagnostics and aggregate execution contracts verified")
