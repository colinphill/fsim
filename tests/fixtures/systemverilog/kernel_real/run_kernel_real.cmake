# SPDX-License-Identifier: Apache-2.0
#
# real, shortreal, realtime and time variables held by the v4 kernel. The
# design is simulated on the reference interpreter and on the kernel: built
# ahead of time and without, in bytecode only (FSIM_STATIC_KERNEL_NATIVE=0),
# under FSIM_KERNEL_VERIFY and on four threads. Every kernel run prints what
# the interpreter prints, and no operation runs through the generic bridge
# except displays, which are host output (FSIM_PROFILE_KERNEL lists bridged
# operations as "fsim-kernel: generic N <operation>"). The design dumps
# itself with $dumpvars; every kernel run writes the interpreter's VCD file.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the kernel real fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_STATIC_KERNEL_NATIVE
    FSIM_STATIC_KERNEL_PLAN_CACHE FSIM_KERNEL_VERIFY FSIM_KERNEL_THREADS
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_KERNEL_IMAGE_REQUIRED
    FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-kernel-real-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/kernel_real.sv" "${case_dir}/kernel_real.sv")

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
# The dump's lines with each time step's changes sorted: processes of one
# region run in no defined order (IEEE 1800-2023 4.7), so neither do the
# changes they make in one time step (21.7.2.3). The header's $date line
# names the run's wall-clock time and is left out.
function(sorted_dump RUN)
  file(STRINGS "${case_dir}/kernel_real.vcd" lines)
  set(sorted "")
  set(step "")
  set(changes OFF)
  foreach(line IN LISTS lines)
    if(line MATCHES "^#[0-9]+$")
      list(SORT step)
      list(APPEND sorted ${step} "${line}")
      set(step "")
      set(changes ON)
    elseif(NOT changes)
      if(NOT line MATCHES "^\\$date ")
        list(APPEND sorted "${line}")
      endif()
    else()
      list(APPEND step "${line}")
    endif()
  endforeach()
  list(SORT step)
  list(APPEND sorted ${step})
  list(JOIN sorted "\n" text)
  file(WRITE "${case_dir}/${RUN}.vcd" "${text}\n")
endfunction()

function(real_run RUN ENGINE)
  run_fsim_step("${RUN}" simulate --engine "${ENGINE}")
  sorted_dump("${RUN}")
  file(READ "${case_dir}/${RUN}.vcd" dump)
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
  file(READ "${case_dir}/reference.vcd" reference_dump)
  if(NOT dump STREQUAL reference_dump)
    message(FATAL_ERROR "${RUN}: the kernel run's VCD file differs from the interpreter's\n"
      "workspace: ${case_dir}")
  endif()
endfunction()

run_fsim_step(compile compile -q kernel_real.sv)
run_fsim_step(elaborate_aot elaborate -q --top kernel_real)
real_run(reference interpreter)
file(READ "${case_dir}/reference.stdout" reference)
if(NOT reference MATCHES "inf -231.418093 -inf 1.750000 30")
  message(FATAL_ERROR "the reference run printed no final values\n"
    "${reference}\nworkspace: ${case_dir}")
endif()
file(READ "${case_dir}/reference.vcd" reference_dump)
# The initial values hold the cell's time output, x until the cell's first
# event, and a real value. (Identifier codes include # and $.)
string(FIND "${reference_dump}" "\$dumpvars\n" initial_begin)
string(FIND "${reference_dump}" "\n#1\n" initial_end)
math(EXPR initial_length "${initial_end} - ${initial_begin}")
string(SUBSTRING "${reference_dump}" ${initial_begin} ${initial_length} initial_values)
if(initial_begin LESS 0 OR initial_end LESS initial_begin
    OR NOT initial_values MATCHES "\nbx+ "
    OR NOT initial_values MATCHES "\nr1\\.5 "
    OR NOT reference_dump MATCHES "\n#30\n")
  message(FATAL_ERROR "the reference run's VCD file lacks the expected changes\n"
    "${reference_dump}\nworkspace: ${case_dir}")
endif()
set(ENV{FSIM_PROFILE_KERNEL} 1)
real_run(aot v4)
set(ENV{FSIM_STATIC_KERNEL_NATIVE} 0)
real_run(bytecode v4)
unset(ENV{FSIM_STATIC_KERNEL_NATIVE})
set(ENV{FSIM_KERNEL_VERIFY} 1)
real_run(verify v4)
unset(ENV{FSIM_KERNEL_VERIFY})
set(ENV{FSIM_KERNEL_THREADS} 4)
real_run(threads v4)
unset(ENV{FSIM_KERNEL_THREADS})
unset(ENV{FSIM_PROFILE_KERNEL})
run_fsim_step(elaborate_jit elaborate -q --top kernel_real --no-aot)
set(ENV{FSIM_PROFILE_KERNEL} 1)
real_run(jit v4)
unset(ENV{FSIM_PROFILE_KERNEL})
