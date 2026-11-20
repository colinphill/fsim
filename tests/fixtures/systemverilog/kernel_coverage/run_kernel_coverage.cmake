# SPDX-License-Identifier: Apache-2.0
#
# Code coverage in the v4 kernel (KOp::coverage). Each design is simulated
# with code coverage on the reference interpreter and on the kernel: with
# the kernel built ahead of time and without, in bytecode only
# (FSIM_STATIC_KERNEL_NATIVE=0), under FSIM_KERNEL_VERIFY and on four
# threads. Every kernel run's coverage report must equal the interpreter's,
# and no coverage operation may run through the generic bridge
# (FSIM_PROFILE_KERNEL lists those as "fsim-kernel: generic N <operation>").

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the kernel coverage fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_STATIC_KERNEL_NATIVE
    FSIM_STATIC_KERNEL_PLAN_CACHE FSIM_KERNEL_VERIFY FSIM_KERNEL_THREADS
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_KERNEL_IMAGE_REQUIRED
    FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-kernel-coverage-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")

function(run_fsim_step CASE_NAME STEP_NAME)
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
      "${CASE_NAME} ${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\n"
      "workspace: ${case_dir}")
  endif()
endfunction()

# Simulates on ENGINE into <RUN>.fsimcov and renders <RUN>.report; the
# kernel runs must not bridge a coverage operation.
function(coverage_run CASE_NAME RUN ENGINE)
  run_fsim_step("${CASE_NAME}" "${RUN}_simulate" simulate --engine "${ENGINE}"
    --code-coverage --coverage-output "${RUN}.fsimcov")
  run_fsim_step("${CASE_NAME}" "${RUN}_report" coverage report "${RUN}.fsimcov"
    -o "${RUN}.report")
  if(NOT ENGINE STREQUAL "interpreter")
    file(READ "${case_dir}/${RUN}_simulate.stderr" profile)
    if(profile MATCHES "fsim-kernel: generic [0-9]+ CodeCoverage")
      message(FATAL_ERROR
        "${CASE_NAME} ${RUN}: a coverage operation ran through the generic bridge\n"
        "${profile}\nworkspace: ${case_dir}")
    endif()
    if(NOT profile MATCHES "fsim-kernel: ")
      message(FATAL_ERROR
        "${CASE_NAME} ${RUN}: the run printed no kernel profile\n"
        "${profile}\nworkspace: ${case_dir}")
    endif()
  endif()
endfunction()

set(cases sv vhdl)
set(sources_sv kernel_coverage.sv)
set(top_sv kernel_coverage)
set(sources_vhdl kernel_coverage_resized.vhd)
set(top_vhdl kernel_coverage_vhdl)

# kernel_coverage.vhd assigns the 16-bit product UNSIGNED(d) * 13 + 7 to the
# 8-bit d (IEEE 1076-2019 16.8.5): it is rejected with the length error
# (owner 11-19); kernel_coverage_resized.vhd is the positive case.
set(case_dir "${workspace}/vhdl_rejected")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/kernel_coverage.vhd"
  "${case_dir}/kernel_coverage.vhd")
execute_process(
  COMMAND "${FSIM_EXECUTABLE}" compile -q --code-coverage --standard 2008
    kernel_coverage.vhd
  WORKING_DIRECTORY "${case_dir}"
  RESULT_VARIABLE rejected_result
  OUTPUT_VARIABLE rejected_output
  ERROR_VARIABLE rejected_output)
if("${rejected_result}" STREQUAL "0")
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" elaborate -q --top kernel_coverage_vhdl
      --code-coverage
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE rejected_result
    OUTPUT_VARIABLE rejected_output
    ERROR_VARIABLE rejected_output)
endif()
if("${rejected_result}" STREQUAL "0"
    OR NOT rejected_output MATCHES "error\\[FSIM-ELAB-VHARRAY-006\\]")
  message(FATAL_ERROR
    "kernel_coverage.vhd was not rejected with FSIM-ELAB-VHARRAY-006\n"
    "${rejected_output}\nworkspace: ${case_dir}")
endif()

foreach(case_name IN LISTS cases)
  set(case_dir "${workspace}/${case_name}")
  file(MAKE_DIRECTORY "${case_dir}")
  foreach(file IN LISTS sources_${case_name})
    file(COPY_FILE "${FSIM_FIXTURE_DIR}/${file}" "${case_dir}/${file}")
  endforeach()
  if(case_name STREQUAL "vhdl")
    run_fsim_step("${case_name}" compile
      compile -q --code-coverage --standard 2008 ${sources_${case_name}})
  else()
    run_fsim_step("${case_name}" compile
      compile -q --code-coverage ${sources_${case_name}})
  endif()

  run_fsim_step("${case_name}" elaborate_aot
    elaborate -q --top "${top_${case_name}}" --code-coverage)
  coverage_run("${case_name}" reference interpreter)
  set(ENV{FSIM_PROFILE_KERNEL} 1)
  coverage_run("${case_name}" aot v4)
  set(ENV{FSIM_STATIC_KERNEL_NATIVE} 0)
  coverage_run("${case_name}" bytecode v4)
  unset(ENV{FSIM_STATIC_KERNEL_NATIVE})
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  coverage_run("${case_name}" verify v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  set(ENV{FSIM_KERNEL_THREADS} 4)
  coverage_run("${case_name}" threads v4)
  unset(ENV{FSIM_KERNEL_THREADS})
  unset(ENV{FSIM_PROFILE_KERNEL})

  run_fsim_step("${case_name}" elaborate_jit
    elaborate -q --top "${top_${case_name}}" --code-coverage --no-aot)
  set(ENV{FSIM_PROFILE_KERNEL} 1)
  coverage_run("${case_name}" jit v4)
  unset(ENV{FSIM_PROFILE_KERNEL})

  file(READ "${case_dir}/reference.report" reference)
  if(NOT reference MATCHES "[Tt]oggle")
    message(FATAL_ERROR "${case_name}: the reference report has no toggle coverage\n"
      "${reference}\nworkspace: ${case_dir}")
  endif()
  foreach(run IN ITEMS aot bytecode verify threads jit)
    file(READ "${case_dir}/${run}.report" report)
    if(NOT report STREQUAL reference)
      message(FATAL_ERROR
        "${case_name}: the ${run} kernel run's coverage differs from the interpreter's\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endforeach()
