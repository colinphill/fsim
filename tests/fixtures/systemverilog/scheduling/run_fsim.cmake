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
elseif(FSIM_SCHEDULING_ENGINE STREQUAL "llvm")
  set(fsim_engine compiled)
else()
  message(FATAL_ERROR
    "FSIM_SCHEDULING_ENGINE must be interpreter or llvm")
endif()

if(NOT DEFINED FSIM_SCHEDULING_OPTIMIZATION)
  set(FSIM_SCHEDULING_OPTIMIZATION O2)
endif()
if(NOT FSIM_SCHEDULING_OPTIMIZATION MATCHES "^O[02]$")
  message(FATAL_ERROR "FSIM_SCHEDULING_OPTIMIZATION must be O0 or O2")
endif()
if(FSIM_SCHEDULING_ENGINE STREQUAL "llvm")
  # Admission evidence is stderr-only and is kept outside canonical output.
  set(ENV{FSIM_PROFILE_JIT} 1)
  set(ENV{FSIM_PROFILE_SV_WAVES} 1)
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
  if(FSIM_SCHEDULING_ENGINE STREQUAL "llvm"
      AND (case_name STREQUAL "a2_native_chain"
        OR case_name STREQUAL "a2_chain_fork_merge"))
    # Retain explicit route controls for the admission/fallback witnesses.
    # Every case also runs with the default policy and profiling disabled below.
    set(ENV{FSIM_ENABLE_SV_REGION_KERNEL} 1)
  else()
    set(ENV{FSIM_ENABLE_SV_REGION_KERNEL} 0)
  endif()
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

  if(FSIM_SCHEDULING_ENGINE STREQUAL "llvm"
      AND case_name STREQUAL "a1_alias_leaf_native_read")
    set(ENV{FSIM_PROFILE_JIT_PROCESSES} 1)
    set(ENV{FSIM_PROFILE_JIT_PROCESSES_ALL} 1)
  endif()
  run_fsim_step("${case_name}" elaborate
    elaborate -q --top "${case_name}" --no-aot
      --optimization "${FSIM_SCHEDULING_OPTIMIZATION}")
  run_fsim_step("${case_name}" simulate
    simulate --engine "${fsim_engine}")

  if(FSIM_SCHEDULING_ENGINE STREQUAL "llvm")
    file(READ "${case_dir}/simulate.stderr" native_evidence)
    string(REGEX MATCH "fsim-profile: jit setup_ms=[^\r\n]* processes=([0-9]+)"
      native_summary "${native_evidence}")
    if(NOT native_summary OR CMAKE_MATCH_1 LESS 1)
      message(FATAL_ERROR
        "${case_name} has no proven native process admission at "
        "${FSIM_SCHEDULING_OPTIMIZATION}\nworkspace: ${case_dir}")
    endif()
    set(native_processes "${CMAKE_MATCH_1}")
    file(WRITE "${case_dir}/native-admission.txt"
      "${FSIM_SCHEDULING_OPTIMIZATION} processes=${native_processes}\n")
    message(STATUS "Native admission: ${case_name} ${FSIM_SCHEDULING_OPTIMIZATION} "
      "processes=${native_processes}")
    string(REGEX MATCH
      "fsim-profile: sv-ordered-wave-summary [^\r\n]*"
      region_backend_summary "${native_evidence}")
    if(NOT region_backend_summary)
      message(FATAL_ERROR
        "${case_name} lacks the region backend profile summary\n"
        "${native_evidence}\nworkspace: ${case_dir}")
    endif()
    if(case_name STREQUAL "a2_native_chain")
      string(REGEX MATCH "region_backend_runs=([1-9][0-9]*)"
        native_backend_run "${region_backend_summary}")
      string(REGEX MATCH "region_backend_completions=([1-9][0-9]*)"
        native_backend_completion "${region_backend_summary}")
      if(NOT native_backend_run OR NOT native_backend_completion)
        message(FATAL_ERROR
          "${case_name} must execute and commit the real LLVM region backend at "
          "${FSIM_SCHEDULING_OPTIMIZATION}\n${native_evidence}\n"
          "workspace: ${case_dir}")
      endif()
      file(WRITE "${case_dir}/native-region-backend-admission.txt"
        "${FSIM_SCHEDULING_OPTIMIZATION} ${region_backend_summary}\n")
      message(STATUS "Native region backend: ${case_name} "
        "${FSIM_SCHEDULING_OPTIMIZATION} ${native_backend_run}")
    else()
      if(NOT region_backend_summary MATCHES "region_backend_runs=0"
          OR NOT region_backend_summary MATCHES "region_backend_completions=0")
        message(FATAL_ERROR
          "${case_name} unexpectedly executed an excluded region backend\n"
          "${native_evidence}\nworkspace: ${case_dir}")
      endif()
    endif()
    if(case_name STREQUAL "ordered_wave_native")
      string(REGEX MATCH
        "fsim-profile: sv-ordered-wave [^\r\n]*offered=([0-9]+) executed=([2-9]|[1-9][0-9]+) failure=0"
        native_wave_summary "${native_evidence}")
      if(NOT native_wave_summary)
        message(FATAL_ERROR
          "ordered_wave_native lacks an executed ordered SV Active cohort at "
          "${FSIM_SCHEDULING_OPTIMIZATION}\n${native_evidence}\n"
          "workspace: ${case_dir}")
      endif()
      set(native_wave_offered "${CMAKE_MATCH_1}")
      set(native_wave_executed "${CMAKE_MATCH_2}")
      if(native_wave_offered LESS 2)
        message(FATAL_ERROR
          "ordered_wave_native lacks a two-member ordered SV Active cohort at "
          "${FSIM_SCHEDULING_OPTIMIZATION}\n${native_evidence}\n"
          "workspace: ${case_dir}")
      endif()
      file(WRITE "${case_dir}/native-wave-admission.txt"
        "${FSIM_SCHEDULING_OPTIMIZATION} offered=${native_wave_offered} "
        "executed=${native_wave_executed}\n")
      string(REGEX MATCH
        "fsim-profile: sv-ordered-wave-summary [^\r\n]*"
        native_wave_totals "${native_evidence}")
      if(NOT native_wave_totals)
        message(FATAL_ERROR
          "ordered_wave_native must execute all three 20-member waves at "
          "${FSIM_SCHEDULING_OPTIMIZATION}\n${native_evidence}\n"
          "workspace: ${case_dir}")
      endif()
      foreach(required_metric IN ITEMS
          offered_batches=3 offered_members=60 accepted_batches=3
          accepted_members=60 zero_accepted_batches=0 failed_batches=0
          region_kernel_attempts=0 region_kernel_runs=0
          region_kernel_members=0 region_kernel_publications=0
          region_kernel_failures=0)
        if(NOT native_wave_totals MATCHES
            "(^|[ \t])${required_metric}([ \t]|$)")
          message(FATAL_ERROR
            "ordered_wave_native has incorrect ${required_metric} at "
            "${FSIM_SCHEDULING_OPTIMIZATION}\n${native_evidence}\n"
            "workspace: ${case_dir}")
        endif()
      endforeach()
      file(APPEND "${case_dir}/native-wave-admission.txt"
        "${native_wave_totals}\n")
      message(STATUS "Native ordered wave: ${case_name} "
        "${FSIM_SCHEDULING_OPTIMIZATION} offered=${native_wave_offered} "
        "executed=${native_wave_executed}")
    endif()
    if(case_name STREQUAL "a1_alias_leaf_native_read"
        AND FSIM_REQUIRE_A1_NATIVE_DIRECT_READ_SLOTS)
      string(REGEX MATCH
        "fsim-profile: jit-process [^\r\n]*direct_read_slots=([2-9]|[1-9][0-9]+)[^\r\n]*"
        native_direct_read_line "${native_evidence}")
      if(NOT native_direct_read_line)
        message(FATAL_ERROR
          "a1_alias_leaf_native_read must admit a native process with at "
          "least two compiled current-value read slots at "
          "${FSIM_SCHEDULING_OPTIMIZATION}\n${native_evidence}\n"
          "workspace: ${case_dir}")
      endif()
      set(native_direct_read_slots "${CMAKE_MATCH_1}")
      file(WRITE "${case_dir}/native-direct-read-admission.txt"
        "${FSIM_SCHEDULING_OPTIMIZATION} direct_read_slots="
        "${native_direct_read_slots}\n")
    endif()
    if(case_name STREQUAL "a1_alias_leaf_native_read")
      unset(ENV{FSIM_PROFILE_JIT_PROCESSES})
      unset(ENV{FSIM_PROFILE_JIT_PROCESSES_ALL})
    endif()
  endif()

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
  # correctness. Reuse the same compiled design with the ordinary policy.
  unset(ENV{FSIM_ENABLE_SV_REGION_KERNEL})
  unset(ENV{FSIM_ENABLE_SV_LOCAL_WAVE})
  unset(ENV{FSIM_PROFILE_JIT})
  unset(ENV{FSIM_PROFILE_SV_WAVES})
  unset(ENV{FSIM_PROFILE_JIT_PROCESSES})
  unset(ENV{FSIM_PROFILE_JIT_PROCESSES_ALL})
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
  if(FSIM_SCHEDULING_ENGINE STREQUAL "llvm")
    set(ENV{FSIM_PROFILE_JIT} 1)
    set(ENV{FSIM_PROFILE_SV_WAVES} 1)
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
