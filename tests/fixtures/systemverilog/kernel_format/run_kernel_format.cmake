# SPDX-License-Identifier: Apache-2.0
#
# String formatting in the v4 kernel (KOp::str format and radix), VHDL
# 'VALUE parsing (KOp::str method), $realtime and VHDL's NOW. Each design (SystemVerilog kernel_format, VHDL
# kernel_format_vhdl) is simulated on the reference interpreter and on the
# kernel: built ahead of time and without, in bytecode only
# (FSIM_STATIC_KERNEL_NATIVE=0), under FSIM_KERNEL_VERIFY and on four
# threads. Every kernel run prints what the interpreter prints, and no
# formatting or time query runs through the generic bridge (FSIM_PROFILE_KERNEL
# lists those as "fsim-kernel: generic N <operation>"; displays and TextIO
# file writes are host output and stay there). kernel_format_vhdl_limit's
# field width beyond the string length limit fails every run after the
# same output.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the kernel format fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_STATIC_KERNEL_NATIVE
    FSIM_STATIC_KERNEL_PLAN_CACHE FSIM_KERNEL_VERIFY FSIM_KERNEL_THREADS
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_KERNEL_IMAGE_REQUIRED
    FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-kernel-format-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/kernel_format.sv" "${case_dir}/kernel_format.sv")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/kernel_format.vhd" "${case_dir}/kernel_format.vhd")

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
# bridges no formatting or time query.
function(format_run RUN ENGINE)
  run_fsim_step("${RUN}" simulate --engine "${ENGINE}")
  if(ENGINE STREQUAL "interpreter")
    return()
  endif()
  file(READ "${case_dir}/${RUN}.stderr" profile)
  if(NOT profile MATCHES "fsim-kernel: ")
    message(FATAL_ERROR "${RUN}: the run printed no kernel profile\n"
      "${profile}\nworkspace: ${case_dir}")
  endif()
  if(profile MATCHES "fsim-kernel: generic [0-9]+ (StringMethod|SystemVerilogMath)")
    message(FATAL_ERROR
      "${RUN}: a formatting operation or time query ran through the generic bridge\n"
      "${profile}\nworkspace: ${case_dir}")
  endif()
  file(READ "${case_dir}/reference.stdout" reference)
  file(READ "${case_dir}/${RUN}.stdout" output)
  if(NOT output STREQUAL reference)
    message(FATAL_ERROR "${RUN}: the kernel run's output differs from the interpreter's\n"
      "reference:\n${reference}\n${RUN}:\n${output}\nworkspace: ${case_dir}")
  endif()
endfunction()

# Every kernel tier on the elaborated TOP; its interpreter run prints
# EXPECTED.
function(format_tiers TOP EXPECTED)
  run_fsim_step(${TOP}_elaborate_aot elaborate -q --top ${TOP})
  format_run(reference interpreter)
  file(READ "${case_dir}/reference.stdout" reference)
  if(NOT reference MATCHES "${EXPECTED}")
    message(FATAL_ERROR "${TOP}: the reference run printed no formatted lines\n"
      "${reference}\nworkspace: ${case_dir}")
  endif()
  set(ENV{FSIM_PROFILE_KERNEL} 1)
  format_run(${TOP}_aot v4)
  set(ENV{FSIM_STATIC_KERNEL_NATIVE} 0)
  format_run(${TOP}_bytecode v4)
  unset(ENV{FSIM_STATIC_KERNEL_NATIVE})
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  format_run(${TOP}_verify v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  set(ENV{FSIM_KERNEL_THREADS} 4)
  format_run(${TOP}_threads v4)
  unset(ENV{FSIM_KERNEL_THREADS})
  unset(ENV{FSIM_PROFILE_KERNEL})
  run_fsim_step(${TOP}_elaborate_jit elaborate -q --top ${TOP} --no-aot)
  set(ENV{FSIM_PROFILE_KERNEL} 1)
  format_run(${TOP}_jit v4)
  unset(ENV{FSIM_PROFILE_KERNEL})
endfunction()

run_fsim_step(compile compile -q kernel_format.sv)
format_tiers(kernel_format "kernel_format.third 7:")
run_fsim_step(compile_vhdl compile -q --standard 2019 kernel_format.vhd)
format_tiers(kernel_format_vhdl ":kernel_format_vhdl:second: 12:  12\\|ab  \\|115 ns.* 84 blue 12000 um 'z' 60-2 0110 0k")

# The field width limit (IEEE 1076-2019 16.4 WIDTH; the string length limit)
# fails the run on every engine after the lines written before it.
function(limit_run RUN ENGINE)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" simulate --engine "${ENGINE}"
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${case_dir}/${RUN}.stdout"
    ERROR_FILE "${case_dir}/${RUN}.stderr")
  file(READ "${case_dir}/${RUN}.stdout" output)
  file(READ "${case_dir}/${RUN}.stderr" errors)
  if("${result}" STREQUAL "0"
      OR NOT errors MATCHES "VHDL TextIO field exceeds the string length limit"
      OR NOT output STREQUAL "   1\n   2\n")
    message(FATAL_ERROR "${RUN}: the field width limit did not fail the run as expected "
      "(${result})\nstdout:\n${output}\nstderr:\n${errors}\nworkspace: ${case_dir}")
  endif()
endfunction()

run_fsim_step(limit_elaborate elaborate -q --top kernel_format_vhdl_limit)
limit_run(limit_reference interpreter)
limit_run(limit_aot v4)
set(ENV{FSIM_STATIC_KERNEL_NATIVE} 0)
limit_run(limit_bytecode v4)
unset(ENV{FSIM_STATIC_KERNEL_NATIVE})
set(ENV{FSIM_KERNEL_VERIFY} 1)
limit_run(limit_verify v4)
unset(ENV{FSIM_KERNEL_VERIFY})
