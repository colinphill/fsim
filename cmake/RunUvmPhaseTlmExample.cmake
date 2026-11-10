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

# run_stage_with_error(<stderr variable> <driver arguments>...): one stage of
# the driver, with FSIM_STAGE_ENVIRONMENT set; its standard error is returned
# in the named variable.
function(run_stage_with_error error_variable)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env ${FSIM_STAGE_ENVIRONMENT}
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
  set(${error_variable} "${error}" PARENT_SCOPE)
endfunction()

function(run_stage)
  set(FSIM_STAGE_ENVIRONMENT "")
  run_stage_with_error(ignored ${ARGN})
endfunction()

set(common "${FSIM_UVM_ROOT}" "${FSIM_WORK_DIR}" "${FSIM_RELEASE}")
run_stage(direct ${common})
run_stage(compile ${common})
foreach(optimization IN ITEMS o0 o2)
  set(kernel_cache "${FSIM_WORK_DIR}/${optimization}/cache/static-kernel.sqlite3")
  # Only an ahead-of-time elaboration stores v4 kernel code in the
  # optimization's cache (make_config: <work>/<optimization>/cache;
  # `elaborate --aot`, docs/engine-v4-semantics.md). An ordinary
  # elaboration and a simulation compile the kernel in memory and publish
  # nothing, so the cold run starts, and ends, without a cache file.
  file(REMOVE "${FSIM_WORK_DIR}/${optimization}/cache/static-kernel.sqlite3")
  run_stage(elaborate ${common} ${optimization})
  if(EXISTS "${kernel_cache}")
    message(FATAL_ERROR
      "UVM phase/TLM elaboration without --aot published ${kernel_cache}")
  endif()
  run_stage(simulate ${common} ${optimization} compiled cold.fst)
  if(EXISTS "${kernel_cache}")
    message(FATAL_ERROR
      "UVM phase/TLM compiled simulation published ${kernel_cache}")
  endif()
  # The ahead-of-time elaboration stores the kernel code; the warm run
  # starts from it. Its profile (FSIM_PROFILE_PHASES) reports every kernel
  # template restored from the cache (`static-kernel-codegen restored=N
  # of=N`), and no template the run compiles later has instructions of its
  # own to compile (`instructions=0`); the cold run above compiled them.
  run_stage(elaborate ${common} ${optimization} --aot)
  if(NOT EXISTS "${kernel_cache}")
    message(FATAL_ERROR
      "UVM phase/TLM elaborate --aot did not store ${kernel_cache}")
  endif()
  set(FSIM_STAGE_ENVIRONMENT FSIM_PROFILE_PHASES=1)
  run_stage_with_error(profile
    simulate ${common} ${optimization} compiled warm.fst)
  set(FSIM_STAGE_ENVIRONMENT "")
  string(REGEX MATCH
    "fsim-profile: static-kernel-codegen restored=([0-9]+) of=([0-9]+)"
    restored "${profile}")
  if(NOT restored OR CMAKE_MATCH_1 EQUAL 0
      OR NOT CMAKE_MATCH_1 EQUAL CMAKE_MATCH_2)
    message(FATAL_ERROR
      "UVM phase/TLM compiled simulation did not start from ${kernel_cache}\n${profile}")
  endif()
  string(REGEX MATCH
    "fsim-profile: static-kernel-codegen templates=[0-9]+ hot=[0-9]+ warm=[0-9]+ instructions=[1-9]"
    compiled_in_run "${profile}")
  if(compiled_in_run)
    message(FATAL_ERROR
      "UVM phase/TLM compiled simulation compiled kernel code despite ${kernel_cache}\n${profile}")
  endif()
  run_stage(simulate ${common} ${optimization} debug debug.vcd)
endforeach()
