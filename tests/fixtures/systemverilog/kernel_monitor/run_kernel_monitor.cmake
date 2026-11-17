# SPDX-License-Identifier: Apache-2.0
#
# $monitor and $strobe of signals held by the v4 kernel. The design is
# simulated on the reference interpreter and on the kernel: built ahead of
# time and without, in bytecode only (FSIM_STATIC_KERNEL_NATIVE=0), under
# FSIM_KERNEL_VERIFY and on four threads. Every kernel run prints what the
# interpreter prints, and no operation runs through the generic bridge
# except displays, which are host output (FSIM_PROFILE_KERNEL lists bridged
# operations as "fsim-kernel: generic N <operation>"): the monitored
# variables stay kernel slots.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the kernel monitor fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_STATIC_KERNEL_NATIVE
    FSIM_STATIC_KERNEL_PLAN_CACHE FSIM_KERNEL_VERIFY FSIM_KERNEL_THREADS
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_KERNEL_IMAGE_REQUIRED
    FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-kernel-monitor-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/kernel_monitor.sv" "${case_dir}/kernel_monitor.sv")

function(run_fsim_step STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${case_dir}/${STEP_NAME}.stdout"
    ERROR_FILE "${case_dir}/${STEP_NAME}.stderr")
  if(NOT "${result}" STREQUAL "0")
    file(READ "${case_dir}/${STEP_NAME}.stdout" stdout)
    file(READ "${case_dir}/${STEP_NAME}.stderr" stderr)
    message(FATAL_ERROR
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
  endif()
endfunction()

# Simulates on ENGINE; a kernel run prints the interpreter's output and
# bridges nothing but displays.
function(monitor_run RUN ENGINE)
  run_fsim_step("${RUN}" simulate --engine "${ENGINE}")
  if(ENGINE STREQUAL "interpreter")
    return()
  endif()
  file(READ "${case_dir}/${RUN}.stderr" profile)
  if(NOT profile MATCHES "fsim-kernel: ")
    message(FATAL_ERROR "${RUN}: the run printed no kernel profile\n"
      "${profile}\nworkspace: ${case_dir}")
  endif()
  string(REGEX MATCHALL "fsim-kernel: generic [0-9]+ [^\n]*" bridged "${profile}")
  foreach(row IN LISTS bridged)
    if(NOT row MATCHES "^fsim-kernel: generic [0-9]+ [A-Za-z]*Display ")
      message(FATAL_ERROR "${RUN}: an operation ran through the generic bridge: ${row}\n"
        "${profile}\nworkspace: ${case_dir}")
    endif()
  endforeach()
  file(READ "${case_dir}/reference.stdout" reference)
  file(READ "${case_dir}/${RUN}.stdout" output)
  if(NOT output STREQUAL reference)
    message(FATAL_ERROR "${RUN}: the kernel run's output differs from the interpreter's\n"
      "reference:\n${reference}\n${RUN}:\n${output}\nworkspace: ${case_dir}")
  endif()
endfunction()

run_fsim_step(compile compile -q kernel_monitor.sv)
run_fsim_step(elaborate_aot elaborate -q --top kernel_monitor)
monitor_run(reference interpreter)
file(READ "${case_dir}/reference.stdout" reference)
# The monitor's last line before $monitoroff, none while it is off, the
# line $monitoron prints, a strobe, and the replacing monitor's lines.
if(NOT reference MATCHES "\n6 a=18 y=19 doubled=38 r=32.000000\nstrobe 6 y=19 doubled=38\n8 a=100 y=101 doubled=202 r=32.000000\nagain 9 y=8\nagain 10 y=10\n")
  message(FATAL_ERROR "the reference run printed other monitor lines\n"
    "${reference}\nworkspace: ${case_dir}")
endif()
set(ENV{FSIM_PROFILE_KERNEL} 1)
monitor_run(aot v4)
set(ENV{FSIM_STATIC_KERNEL_NATIVE} 0)
monitor_run(bytecode v4)
unset(ENV{FSIM_STATIC_KERNEL_NATIVE})
set(ENV{FSIM_KERNEL_VERIFY} 1)
monitor_run(verify v4)
unset(ENV{FSIM_KERNEL_VERIFY})
set(ENV{FSIM_KERNEL_THREADS} 4)
monitor_run(threads v4)
unset(ENV{FSIM_KERNEL_THREADS})
unset(ENV{FSIM_PROFILE_KERNEL})
run_fsim_step(elaborate_jit elaborate -q --top kernel_monitor --no-aot)
set(ENV{FSIM_PROFILE_KERNEL} 1)
monitor_run(jit v4)
unset(ENV{FSIM_PROFILE_KERNEL})
