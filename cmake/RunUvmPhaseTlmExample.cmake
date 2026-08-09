# SPDX-License-Identifier: Apache-2.0

foreach(required IN ITEMS FSIM_EXECUTABLE FSIM_UVM_ROOT FSIM_WORK_DIR FSIM_RELEASE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

set(
  expected
  "FSIM-UVM-PHASE-TLM-PASS phases=build/connect/eoe/sos/run/extract/check/report/final roots=left,right objection=1/0 drain=3 payload=37 result=42 source=37/42/1 race=5 deadlock=FSIM-UVM-PHASE-008 sequence=arb/lock/response/virtual roles=agent/driver/monitor/scoreboard callback=6 transaction=5 cap=records register=frontdoor/backdoor/predictor maps=little/big byte_enable=1010 callback_coverage=1 sequence=access replay=relocated cap=records"
)

function(run_stage)
  execute_process(
    COMMAND
      "${FSIM_EXECUTABLE}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
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
