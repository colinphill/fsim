# SPDX-License-Identifier: Apache-2.0
#
# STRING allocators with run-time index ranges and STRING aggregates with
# range choices (IEEE 1076-2019 9.3.3.3, 9.3.5, 9.3.7): the v4 kernel and
# the interpreter report the same values, the aggregates run as kernel
# code with no operation on the generic bridge but the reports (host
# output; FSIM_PROFILE_KERNEL lists bridged operations as "fsim-kernel:
# generic N <operation>"), and a range bound outside POSITIVE fails the
# run.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the STRING allocator fixture directory")
endif()

foreach(variable IN ITEMS FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL
    FSIM_STATIC_KERNEL_NATIVE FSIM_KERNEL_VERIFY FSIM_KERNEL_THREADS)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-string-allocators-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/string_allocators.vhd"
  "${case_dir}/string_allocators.vhd")

function(run_fsim_step STEP_NAME EXPECTED_RESULT)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${case_dir}/${STEP_NAME}.stdout"
    ERROR_FILE "${case_dir}/${STEP_NAME}.stderr")
  file(READ "${case_dir}/${STEP_NAME}.stdout" stdout)
  file(READ "${case_dir}/${STEP_NAME}.stderr" stderr)
  if(NOT "${result}" STREQUAL "${EXPECTED_RESULT}")
    message(FATAL_ERROR "${STEP_NAME} returned ${result}, not ${EXPECTED_RESULT}\n"
      "${stdout}${stderr}\nworkspace: ${case_dir}")
  endif()
  set(output "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

set(expected
  "a1 4 0" "a2 3" "a3 0" "a4 0" "a5 qqqq" "a6 kkk" "a7 bc" "l1 5" "l2 wwww"
  "l3 vv" "g1 zzz" "g2 abk" "g3 kkk" "g4 bck" "g5 xky" "ab")
function(check_reports RUN)
  string(REGEX MATCHALL "FSIM-HDL-REPORT\\]: [^\n]*" rows "${output}")
  list(TRANSFORM rows REPLACE "FSIM-HDL-REPORT\\]: " "")
  if(NOT "${rows}" STREQUAL "${expected}")
    message(FATAL_ERROR "${RUN}: the reports are\n  ${rows}\nnot\n  ${expected}\n"
      "${output}\nworkspace: ${case_dir}")
  endif()
endfunction()

run_fsim_step(compile 0 compile -q string_allocators.vhd)
run_fsim_step(elaborate 0 elaborate -q --top string_allocators)
set(ENV{FSIM_PROFILE_KERNEL} 1)
set(ENV{FSIM_PROFILE_PHASES} 1)
run_fsim_step(simulate_v4 0 simulate)
unset(ENV{FSIM_PROFILE_KERNEL})
unset(ENV{FSIM_PROFILE_PHASES})
check_reports(v4)
if(output MATCHES "not_compiled[^\n]*example=string_allocators\\.aggregates")
  message(FATAL_ERROR "v4: the aggregates process is not kernel code\n"
    "${output}\nworkspace: ${case_dir}")
endif()
string(REGEX MATCHALL "fsim-kernel: generic [0-9]+ [^\n]*aggregates" bridged
  "${output}")
foreach(row IN LISTS bridged)
  if(NOT row MATCHES "^fsim-kernel: generic [0-9]+ StringReport ")
    message(FATAL_ERROR "v4: an operation ran through the generic bridge: ${row}\n"
      "${output}\nworkspace: ${case_dir}")
  endif()
endforeach()
run_fsim_step(simulate_interpreter 0 simulate --engine interpreter)
check_reports(interpreter)

run_fsim_step(elaborate_bounds 0 elaborate -q --top string_allocator_bounds)
foreach(engine IN ITEMS v4 interpreter)
  run_fsim_step(simulate_bounds_${engine} 1 simulate --engine ${engine})
  if(NOT output MATCHES "VHDL STRING index range is outside its POSITIVE index subtype"
      OR output MATCHES "not reached")
    message(FATAL_ERROR "${engine}: a range bound below 1 did not fail the run\n"
      "${output}\nworkspace: ${case_dir}")
  endif()
endforeach()
file(REMOVE_RECURSE "${case_dir}")
