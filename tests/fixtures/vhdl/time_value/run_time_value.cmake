# SPDX-License-Identifier: Apache-2.0
#
# TIME'VALUE and DELAY_LENGTH'VALUE (IEEE 1076-2019 16.2.2, 5.2.4.2) on the
# v4 kernel and the interpreter: values in ticks of the run's resolution,
# truncated toward zero below it; a string that is no TIME literal fails the
# run.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the TIME'VALUE fixture directory")
endif()

foreach(variable IN ITEMS FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL
    FSIM_STATIC_KERNEL_NATIVE FSIM_KERNEL_VERIFY FSIM_KERNEL_THREADS)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-time-value-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/time_value.vhd" "${case_dir}/time_value.vhd")
# The 1 fs literals of time_value_fs.vhd cannot be represented at 1 ps, so
# that file has a workspace of its own.
set(fs_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-time-value-fs-${workspace_id}")
file(MAKE_DIRECTORY "${fs_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/time_value_fs.vhd"
  "${fs_dir}/time_value_fs.vhd")
set(ps_dir "${case_dir}")

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

run_fsim_step(compile 0 compile -q --standard 2019 time_value.vhd)

# Ticks of 1 ps: 10 ns, 1.5 us, 2 ps, DELAY_LENGTH 3 ns, "1 fs" -> 0,
# "1500 fs" -> 1, "-1500 fs" -> -1, "999 fs" -> 0, "2 US" (case-insensitive).
set(expected "a1 10000" "a2 1500000" "a3 2" "a4 3000" "a5 0" "a6 1" "a7 -1"
  "a8 0" "a9 2000000")
run_fsim_step(elaborate_ps 0 elaborate -q --top time_value_ps --time-resolution 1ps)
run_fsim_step(simulate_ps_v4 0 simulate)
check_reports(ps_v4)
run_fsim_step(simulate_ps_interpreter 0 simulate --engine interpreter)
check_reports(ps_interpreter)

# Ticks of 1 fs: every unit exact.
set(case_dir "${fs_dir}")
run_fsim_step(compile_fs 0 compile -q --standard 2019 time_value_fs.vhd)
set(expected "b1 1" "b2 1500" "b3 10000000")
run_fsim_step(elaborate_fs 0 elaborate -q --top time_value_fs --time-resolution 1fs)
run_fsim_step(simulate_fs_v4 0 simulate)
check_reports(fs_v4)
run_fsim_step(simulate_fs_interpreter 0 simulate --engine interpreter)
check_reports(fs_interpreter)

set(case_dir "${ps_dir}")
# Not a TIME literal: the run fails with the 'VALUE error, on both engines.
foreach(top IN ITEMS bad syntax)
  run_fsim_step(elaborate_${top} 0 elaborate -q --top time_value_${top}
    --time-resolution 1ps)
  foreach(engine IN ITEMS v4 interpreter)
    run_fsim_step(simulate_${top}_${engine} 1 simulate --engine ${engine})
    if(output MATCHES "not reached")
      message(FATAL_ERROR "${top} ${engine}: 'VALUE of a bad string did not fail\n"
        "${output}\nworkspace: ${case_dir}")
    endif()
  endforeach()
endforeach()
file(REMOVE_RECURSE "${ps_dir}" "${fs_dir}")
