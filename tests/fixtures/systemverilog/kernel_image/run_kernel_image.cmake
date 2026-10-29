# SPDX-License-Identifier: Apache-2.0
#
# Persisted static-kernel images. Each case is simulated on the v4 engine
# three ways from one workspace: elaborated without ahead-of-time state (the
# reference transcript), then with `--aot`, which records the kernel image,
# and simulated with FSIM_KERNEL_IMAGE_REQUIRED=1, so the kernel must
# restore that image; and under FSIM_VERIFY_ARTIFACT=1, where the kernel is
# built afresh and its image must equal the stored one (FSIM-ART-0014
# otherwise). Every transcript must equal the reference.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the kernel image fixture directory")
endif()
if(NOT DEFINED FSIM_SCHEDULING_FIXTURE_DIR
    OR NOT IS_DIRECTORY "${FSIM_SCHEDULING_FIXTURE_DIR}")
  message(FATAL_ERROR
    "FSIM_SCHEDULING_FIXTURE_DIR must name the scheduling fixture directory")
endif()
foreach(variable IN ITEMS FSIM_KERNEL_IMAGE_REQUIRED FSIM_VERIFY_ARTIFACT
    FSIM_STATIC_KERNEL_PLAN_CACHE)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-kernel-image-${workspace_id}")
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

function(read_transcript STEP_NAME OUTPUT)
  file(READ "${case_dir}/${STEP_NAME}.stdout" text)
  string(REPLACE "\r\n" "\n" text "${text}")
  set(${OUTPUT} "${text}" PARENT_SCOPE)
endfunction()

set(cases
  kernel_image_lanes
  a2_native_chain
  a2_chain_fork_merge
  fixed_array_write
  ordered_wave_native
  nba_batch_before_active)

foreach(case_name IN LISTS cases)
  set(case_dir "${workspace}/${case_name}")
  file(MAKE_DIRECTORY "${case_dir}")
  if(EXISTS "${FSIM_FIXTURE_DIR}/${case_name}.sv")
    file(COPY_FILE "${FSIM_FIXTURE_DIR}/${case_name}.sv"
      "${case_dir}/${case_name}.sv")
  else()
    file(COPY_FILE "${FSIM_SCHEDULING_FIXTURE_DIR}/${case_name}.sv"
      "${case_dir}/${case_name}.sv")
  endif()
  run_fsim_step("${case_name}" compile compile -q "${case_name}.sv")

  run_fsim_step("${case_name}" elaborate_reference
    elaborate -q --top "${case_name}" --no-aot --optimization O2)
  run_fsim_step("${case_name}" simulate_reference simulate --engine v4)
  read_transcript(simulate_reference expected)

  run_fsim_step("${case_name}" elaborate_aot
    elaborate -q --top "${case_name}" --aot --aot-scope kernel --optimization O2)
  set(ENV{FSIM_KERNEL_IMAGE_REQUIRED} 1)
  run_fsim_step("${case_name}" simulate_restored simulate --engine v4)
  unset(ENV{FSIM_KERNEL_IMAGE_REQUIRED})
  set(ENV{FSIM_VERIFY_ARTIFACT} 1)
  run_fsim_step("${case_name}" simulate_verified simulate --engine v4)
  unset(ENV{FSIM_VERIFY_ARTIFACT})

  foreach(step IN ITEMS simulate_restored simulate_verified)
    read_transcript(${step} actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR
        "${case_name} ${step} transcript differs from the reference\n"
        "Expected:\n${expected}Actual:\n${actual}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
  message(STATUS "Kernel image witness passed: ${case_name}")
endforeach()

if(NOT "$ENV{KEEP_WORKSPACE}" STREQUAL "1")
  file(REMOVE_RECURSE "${workspace}")
endif()
