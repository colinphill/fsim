# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the scheduling fixture directory")
endif()
if(NOT DEFINED FSIM_SCHEDULING_ENGINE)
  set(FSIM_SCHEDULING_ENGINE interpreter)
endif()
if(FSIM_SCHEDULING_ENGINE STREQUAL "interpreter")
  set(fsim_engine interpreter)
elseif(FSIM_SCHEDULING_ENGINE STREQUAL "v4")
  set(fsim_engine v4)
else()
  message(FATAL_ERROR
    "FSIM_SCHEDULING_ENGINE must be interpreter or v4")
endif()

if(NOT DEFINED FSIM_SCHEDULING_OPTIMIZATION)
  set(FSIM_SCHEDULING_OPTIMIZATION O2)
endif()
if(NOT FSIM_SCHEDULING_OPTIMIZATION MATCHES "^O[02]$")
  message(FATAL_ERROR "FSIM_SCHEDULING_OPTIMIZATION must be O0 or O2")
endif()
unset(ENV{FSIM_ENABLE_SV_REGION_KERNEL})
unset(ENV{FSIM_ENABLE_SV_LOCAL_WAVE})

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-scheduling-${workspace_id}")
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

if(DEFINED FSIM_SCHEDULING_CASES)
  set(cases ${FSIM_SCHEDULING_CASES})
else()
  set(cases
    a2_native_chain
    a2_chain_fork_merge
    a1_alias_leaf_native_read
    multidimensional_fixed_read
    fixed_array_write
    a2_active_nba_boundary
    a2_derived_clock_reader
    continuous_active
    continuous_active_verilog
    ordered_wave_native
    implicit_port_active
    implicit_port_active_verilog
    inactive_nested_delta
    active_inactive_nba
    active_inactive_nba_verilog
    nba_batch_before_active
    derived_clock_nba
    packed_vector_edge_sensitivity
    strobe_end_of_slot
    strobe_end_of_slot_verilog
    monitor_once_per_slot
    reactive_reinactive_re_nba
    mixed_domain_boundary
    inertial_maturation_order
    named_event_triggered_nba)
endif()
foreach(case_name IN LISTS cases)
  # Compare checked publication with default local state independently of
  # environment inherited by CTest. The default run clears this override.
  set(ENV{FSIM_ENABLE_SV_LOCAL_WAVE} 0)
  set(case_dir "${workspace}/${case_name}")
  file(MAKE_DIRECTORY "${case_dir}")
  set(ENV{FSIM_ENABLE_SV_REGION_KERNEL} 0)
  if(case_name STREQUAL "mixed_domain_boundary")
    file(COPY_FILE
      "${FSIM_FIXTURE_DIR}/mixed_language_leaf.vhd"
      "${case_dir}/mixed_language_leaf.vhd")
    file(COPY_FILE
      "${FSIM_FIXTURE_DIR}/${case_name}.sv"
      "${case_dir}/${case_name}.sv")
    run_fsim_step("${case_name}" compile_vhdl
      compile -q --standard 2008 mixed_language_leaf.vhd)
    run_fsim_step("${case_name}" compile_sv
      compile -q mixed_domain_boundary.sv)
  elseif(case_name STREQUAL "continuous_active_verilog"
      OR case_name STREQUAL "implicit_port_active_verilog"
      OR case_name STREQUAL "active_inactive_nba_verilog"
      OR case_name STREQUAL "strobe_end_of_slot_verilog")
    file(COPY_FILE
      "${FSIM_FIXTURE_DIR}/${case_name}.v"
      "${case_dir}/${case_name}.v")
    run_fsim_step("${case_name}" compile
      compile -q "${case_name}.v")
  else()
    file(COPY_FILE
      "${FSIM_FIXTURE_DIR}/${case_name}.sv"
      "${case_dir}/${case_name}.sv")
    run_fsim_step("${case_name}" compile
      compile -q "${case_name}.sv")
  endif()

  run_fsim_step("${case_name}" elaborate
    elaborate -q --top "${case_name}" --no-aot
      --optimization "${FSIM_SCHEDULING_OPTIMIZATION}")
  run_fsim_step("${case_name}" simulate
    simulate --engine "${fsim_engine}")

  file(READ "${case_dir}/simulate.stdout" actual)
  string(REPLACE "\r\n" "\n" actual "${actual}")
  string(REGEX REPLACE
    "(^|\n)simulation (completed|stopped) at tick [0-9]+, delta [0-9]+(\n|$)"
    "\\1" actual "${actual}")
  file(WRITE "${case_dir}/actual.out" "${actual}")
  file(READ "${FSIM_FIXTURE_DIR}/expected/${case_name}.out" expected)
  string(REPLACE "\r\n" "\n" expected "${expected}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
      "${case_name} transcript mismatch for ${FSIM_SCHEDULING_ENGINE}\n"
      "Expected:\n${expected}Actual:\n${actual}\n"
      "workspace: ${case_dir}")
  endif()
  # Profiling and an explicit enable flag must not be prerequisites for
  # correctness. Reuse the same design with the ordinary policy.
  unset(ENV{FSIM_ENABLE_SV_REGION_KERNEL})
  unset(ENV{FSIM_ENABLE_SV_LOCAL_WAVE})
  run_fsim_step("${case_name}" simulate_default
    simulate --engine "${fsim_engine}")
  file(READ "${case_dir}/simulate_default.stdout" default_actual)
  string(REPLACE "\r\n" "\n" default_actual "${default_actual}")
  string(REGEX REPLACE
    "(^|\n)simulation (completed|stopped) at tick [0-9]+, delta [0-9]+(\n|$)"
    "\\1" default_actual "${default_actual}")
  file(WRITE "${case_dir}/default-actual.out" "${default_actual}")
  if(NOT default_actual STREQUAL expected)
    message(FATAL_ERROR
      "${case_name} default unprofiled transcript mismatch for ${FSIM_SCHEDULING_ENGINE}\n"
      "Expected:\n${expected}Actual:\n${default_actual}\n"
      "workspace: ${case_dir}")
  endif()
  message(STATUS "Scheduling witness passed: ${case_name} (controlled and default)")
endforeach()
unset(ENV{FSIM_ENABLE_SV_REGION_KERNEL})
unset(ENV{FSIM_ENABLE_SV_LOCAL_WAVE})

if(NOT "$ENV{KEEP_WORKSPACE}" STREQUAL "1")
  file(REMOVE_RECURSE "${workspace}")
else()
  message(STATUS "Scheduling witness workspace: ${workspace}")
endif()
