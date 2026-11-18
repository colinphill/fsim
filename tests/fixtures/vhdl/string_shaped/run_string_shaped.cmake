# SPDX-License-Identifier: Apache-2.0
#
# STRING values whose index ranges are not static keep their own bounds
# (IEEE 1076-2019 5.3.2.2, 9.3.3.3, 9.3.7, 4.2.1, 16.2), while
# STD.TEXTIO.LINE values stay 1 to their length (16.4). The v4 kernel and
# the interpreter report the same values; on v4 a process that is not
# kernel code names only the existing run-time-shaped reason
# (dynamic_containers:vhdl), and the LINE process stays kernel code. A
# STRING aggregate range with a bound below 1 fails the run.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the shaped STRING fixture directory")
endif()

foreach(variable IN ITEMS FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL
    FSIM_STATIC_KERNEL_NATIVE FSIM_KERNEL_VERIFY FSIM_KERNEL_THREADS)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-string-shaped-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/string_shaped.vhd"
  "${case_dir}/string_shaped.vhd")

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

function(check_reports RUN)
  string(REGEX MATCHALL "FSIM-HDL-REPORT\\]: [^\n]*" rows "${output}")
  list(TRANSFORM rows REPLACE "FSIM-HDL-REPORT\\]: " "")
  if(NOT "${rows}" STREQUAL "${expected}")
    message(FATAL_ERROR "${RUN}: the reports are\n  ${rows}\nnot\n  ${expected}\n"
      "${output}\nworkspace: ${case_dir}")
  endif()
endfunction()

# Each group: its top, whether its process must be kernel code, and the
# reports worked out from the LRM rules named in string_shaped.vhd.
set(heap_expected
  "h1 7 9 true" "h2 0 'm'" "h3 5 2 false 4" "h4 wxyz 'w' 'z'" "h5 2 3 bc"
  "h6 7 bc" "h7 equal" "h8 equal" "h9 7 xyz" "h10 1 4 xyzw" "h11 7 8 ee"
  "h12 6 7 true ff" "h13 equal")
set(heap_native OFF)
set(result_expected "r1 3 5 cde" "r2 3 true" "r3 2 4 bcd")
set(result_native OFF)
set(formal_expected "p1 3 4 cd" "p2 3" "p1 7 8 xy")
set(formal_native OFF)
set(line_expected "l1 1 3 abc" "l2 1 5 abc42" "l3 1 5 abc42" "l4 1 2 xy")
set(line_native ON)

run_fsim_step(compile 0 compile -q string_shaped.vhd)
foreach(group IN ITEMS heap result formal line)
  set(expected ${${group}_expected})
  run_fsim_step(elaborate_${group} 0 elaborate -q --top string_shaped_${group})
  set(ENV{FSIM_PROFILE_PHASES} 1)
  run_fsim_step(simulate_${group}_v4 0 simulate)
  unset(ENV{FSIM_PROFILE_PHASES})
  check_reports(${group}_v4)
  string(REGEX MATCHALL "reason=tier:not_compiled:[^ \n]*" reasons "${output}")
  foreach(reason IN LISTS reasons)
    if(${group}_native OR NOT reason STREQUAL "reason=tier:not_compiled:dynamic_containers:vhdl")
      message(FATAL_ERROR "${group} v4: the process is not kernel code: ${reason}\n"
        "${output}\nworkspace: ${case_dir}")
    endif()
  endforeach()
  run_fsim_step(simulate_${group}_interpreter 0 simulate --engine interpreter)
  check_reports(${group}_interpreter)
endforeach()

run_fsim_step(elaborate_bounds 0 elaborate -q --top string_shaped_bounds)
foreach(engine IN ITEMS v4 interpreter)
  run_fsim_step(simulate_bounds_${engine} 1 simulate --engine ${engine})
  if(NOT output MATCHES "VHDL STRING index range is outside its POSITIVE index subtype"
      OR output MATCHES "not reached")
    message(FATAL_ERROR "${engine}: a range bound below 1 did not fail the run\n"
      "${output}\nworkspace: ${case_dir}")
  endif()
endforeach()
file(REMOVE_RECURSE "${case_dir}")
