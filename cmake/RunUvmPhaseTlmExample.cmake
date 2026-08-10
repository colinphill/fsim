# SPDX-License-Identifier: Apache-2.0

foreach(required IN ITEMS FSIM_EXECUTABLE FSIM_UVM_ROOT FSIM_WORK_DIR FSIM_RELEASE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

if(NOT DEFINED FSIM_STAGE_TIMEOUT_SECONDS)
  set(FSIM_STAGE_TIMEOUT_SECONDS 1200)
endif()
if(NOT FSIM_STAGE_TIMEOUT_SECONDS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "FSIM_STAGE_TIMEOUT_SECONDS must be a positive integer")
endif()

set(
  expected
    "FSIM-UVM-PHASE-TLM-PASS phases=build/connect/eoe/sos/run/extract/check/report/final roots=left,right objection=1/0 drain=3 payload=37 result=42 source=37/42/1 race=5 deadlock=FSIM-UVM-PHASE-008 sequence=arb/lock/response/virtual roles=agent/driver/monitor/scoreboard callback=42 transaction=23 cap=records policy=line/tree/table compare=deep/mismatch/limit copier=deep/shallow/reference packer=big/little/metadata/unpack recorder=object/replay sync=event/pool/barrier/queue/heartbeat/spell cmdline=args/plus/uvm/exact/prefix/value/tool/isolation run_test=select/topology/timeout/seed/repeat/finish/fatal report=verbosity/severity/action/file/catcher/phase/time objection_trace=on/bounded tracing=factory/config/resource/debug/activity legacy_macros=field/object/component/sequence/registry/callback/report legacy_api=phase/objection/tlm/sequence/callback/register/policy/cmdline/aliases/negative uvm2020_api=policy/field_op/copier/object/printer/comparer/packer/recorder/report/version/removed uvm_release=selected/provenance/object/design/cache/checkpoint/replay/mismatch core_smoke=governed/project/object/factory/resource/config/cmdline/report/callback/run_test/topology/timeout/seed flow_smoke=phase/objection/sequence/sequencer/roles/virtual/tlm1/tlm2/callback/transaction/cancellation register=frontdoor/backdoor/predictor maps=little/big byte_enable=1010 callback_coverage=1 sequence=access replay=relocated cap=records register_smoke=block/map/field/memory/adapter/predictor/frontdoor/backdoor/sequence/callback/coverage/dpi/vpi/vhpi/relocation/endian/byte_enable/negative/checkpoint/cap platform_contract=source/abi/linux/windows/cdecl/filesystem limits=as6g/stage1200/matrix7200 conformance_inventory=standard/project families=17 governed_classes=release-exact project_classes=27 supported_gaps=0 suppression=none"
)
string(APPEND expected
  " closure_audit=compatibility/diagnostics/source/complexity/memory/trace/artifact/cache gaps=0")

function(run_stage)
  execute_process(
    COMMAND
      "${FSIM_EXECUTABLE}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT "${FSIM_STAGE_TIMEOUT_SECONDS}"
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "UVM phase/TLM stage failed (${ARGN})\nstdout:\n${output}\nstderr:\n${error}")
  endif()
  string(FIND "${output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "UVM phase/TLM stage lost exact transcript (${ARGN})\n${output}")
  endif()
  message(STATUS "${output}")
endfunction()

set(common "${FSIM_UVM_ROOT}" "${FSIM_WORK_DIR}" "${FSIM_RELEASE}")
run_stage(direct ${common})
run_stage(compile ${common})
foreach(optimization IN ITEMS o0 o2)
  run_stage(elaborate ${common} ${optimization})
  file(REMOVE_RECURSE "${FSIM_WORK_DIR}/.fsim-sim-cache")
  run_stage(simulate ${common} ${optimization} compiled cold.fst)
  if(NOT EXISTS "${FSIM_WORK_DIR}/.fsim-sim-cache")
    message(FATAL_ERROR
      "UVM phase/TLM cold run did not publish its native cache (${optimization})")
  endif()
  run_stage(simulate ${common} ${optimization} compiled warm.fst)
  run_stage(simulate ${common} ${optimization} debug debug.vcd)
endforeach()
