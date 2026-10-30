# SPDX-License-Identifier: Apache-2.0
#
# VHDL delta collapsing must be invisible. Each case is elaborated and
# simulated on the v4 engine three times from one workspace: with
# collapsing, with FSIM_STATIC_KERNEL_DELTA_COLLAPSE=0 (the reference delta
# schedule in the kernel) and with FSIM_STATIC_KERNEL=0 (the reference
# scheduler). The exit status, the transcript and the diagnostics must be
# equal. Each case must also exercise its rule rather than pass by
# avoiding it:
#   dc_glitch_assert, dc_glitch_count: the glitch shows in the reference
#     (a report, a fatal range check);
#   dc_nested_loop: refused for its data-dependent inner loop;
#   dc_observe_glitch: the testbench sees the glitch on the output port;
#   dc_attributes: 'EVENT counts every pulse of the internal signal;
#   dc_regions: a report for each value of the inputs' exclusive or;
#   dc_static, dc_chain: collapsed members are kept (dc_chain eight
#     levels deep);
#   dc_case, dc_numeric, dc_record, dc_diamond: selected and case
#     assignments, numeric_std arithmetic, record field drivers, and fan-in
#     across levels, all collapsed;
#   dc_sv_reader: VHDL cells sampled by a SystemVerilog flop (refused while
#     SystemVerilog readers are not first-delta readers);
#   dc_popcount, dc_slice: integer arithmetic and range checks under a data
#     condition, and dynamic part inserts (narrow and wide targets) whose
#     bases are loop indices, all collapsed by the range certificate;
#   dc_guard: to_integer's metavalue warnings in cones fed by a register
#     and by the testbench input, collapsed under the known-value guard
#     while their sources are known and run in the delta schedule while one
#     holds a metavalue (the guards must both open and close).
# Where a case collapses, the collapsed run must keep at least the members
# listed in min_kept_<case> (the refusal cases' unobserved neighbours
# included) and run some of them collapsed, so that it exercises collapsed
# evaluation as well as the refusal rules.
# The static case also runs from an ahead-of-time snapshot, whose kernel
# image holds the collapse plan: the restored run must keep as many
# members and match the reference.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the delta collapse fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL_DELTA_COLLAPSE FSIM_STATIC_KERNEL
    FSIM_STATIC_KERNEL_PLAN_CACHE FSIM_KERNEL_VERIFY FSIM_PROFILE_PHASES
    FSIM_PROFILE_KERNEL FSIM_STATIC_KERNEL_COLLAPSE_GUARDS
    FSIM_KERNEL_IMAGE_REQUIRED FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-delta-collapse-${workspace_id}")
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

# Simulates; the exit status, transcript and diagnostics (without profile
# lines) land in <STEP>_result, <STEP>_stdout and <STEP>_stderr.
function(simulate_step STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" simulate --engine v4
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${case_dir}/${STEP_NAME}.stdout"
    ERROR_FILE "${case_dir}/${STEP_NAME}.stderr")
  foreach(stream IN ITEMS stdout stderr)
    file(READ "${case_dir}/${STEP_NAME}.${stream}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    if(stream STREQUAL "stderr")
      set(${STEP_NAME}_profile "${text}" PARENT_SCOPE)
    endif()
    string(REGEX REPLACE "(^|\n)(FSIM-PROFILE|FSIM-PHASE|fsim-profile|fsim-kernel)[^\n]*" ""
      text "${text}")
    string(STRIP "${text}" text)
    set(${STEP_NAME}_${stream} "${text}" PARENT_SCOPE)
  endforeach()
  set(${STEP_NAME}_result "${result}" PARENT_SCOPE)
endfunction()

set(cases dc_glitch_assert dc_glitch_count dc_static dc_nested_loop
  dc_observe_glitch dc_attributes dc_chain dc_regions dc_case dc_numeric
  dc_record dc_diamond dc_sv_reader dc_popcount dc_slice dc_guard)
set(min_kept_dc_observe_glitch 2)
set(min_kept_dc_regions 2)
set(min_kept_dc_case 2)
set(min_kept_dc_numeric 3)
set(min_kept_dc_record 3)
set(min_kept_dc_diamond 5)
set(min_kept_dc_popcount 2)
set(min_kept_dc_slice 3)
set(min_kept_dc_guard 3)

foreach(case_name IN LISTS cases)
  set(case_dir "${workspace}/${case_name}")
  file(MAKE_DIRECTORY "${case_dir}")
  foreach(file IN ITEMS delta_collapse_dut.vhd ${case_name}_tb.sv)
    file(COPY_FILE "${FSIM_FIXTURE_DIR}/${file}" "${case_dir}/${file}")
  endforeach()
  run_fsim_step("${case_name}" compile_vhdl
    compile -q --standard 2008 delta_collapse_dut.vhd)
  run_fsim_step("${case_name}" compile_sv compile -q "${case_name}_tb.sv")

  run_fsim_step("${case_name}" elaborate_collapsed
    elaborate -q --top "${case_name}_tb" --no-aot --optimization O2)
  set(ENV{FSIM_PROFILE_PHASES} 1)
  set(ENV{FSIM_PROFILE_KERNEL} 1)
  simulate_step(collapsed)
  unset(ENV{FSIM_PROFILE_PHASES})
  unset(ENV{FSIM_PROFILE_KERNEL})

  set(ENV{FSIM_STATIC_KERNEL_DELTA_COLLAPSE} 0)
  run_fsim_step("${case_name}" elaborate_reference
    elaborate -q --top "${case_name}_tb" --no-aot --optimization O2)
  simulate_step(reference)
  unset(ENV{FSIM_STATIC_KERNEL_DELTA_COLLAPSE})

  set(ENV{FSIM_STATIC_KERNEL} 0)
  run_fsim_step("${case_name}" elaborate_scheduler
    elaborate -q --top "${case_name}_tb" --no-aot --optimization O2)
  simulate_step(scheduler)
  unset(ENV{FSIM_STATIC_KERNEL})

  foreach(part IN ITEMS result stdout stderr)
    if(NOT "${collapsed_${part}}" STREQUAL "${reference_${part}}")
      message(FATAL_ERROR
        "${case_name}: the collapsed run's ${part} differs from the reference\n"
        "Reference:\n${reference_${part}}\nCollapsed:\n${collapsed_${part}}\n"
        "workspace: ${case_dir}")
    endif()
    if(NOT "${scheduler_${part}}" STREQUAL "${reference_${part}}")
      message(FATAL_ERROR
        "${case_name}: the reference scheduler's ${part} differs from the "
        "kernel's reference schedule\n"
        "Kernel:\n${reference_${part}}\nScheduler:\n${scheduler_${part}}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()

  set(kept 0)
  if(collapsed_profile MATCHES "static-kernel-collapse planned=[0-9]+ kept=([0-9]+)")
    set(kept "${CMAKE_MATCH_1}")
  endif()
  if(DEFINED min_kept_${case_name} AND kept LESS min_kept_${case_name})
    message(FATAL_ERROR
      "${case_name}: at least ${min_kept_${case_name}} members must collapse "
      "(kept ${kept})\n${collapsed_profile}\nworkspace: ${case_dir}")
  endif()
  # Kept members must also run collapsed (FSIM_PROFILE_KERNEL's count).
  if(DEFINED min_kept_${case_name}
      AND NOT collapsed_profile MATCHES " collapsed=[1-9]")
    message(FATAL_ERROR
      "${case_name}: the kept members never ran collapsed\n"
      "${collapsed_profile}\nworkspace: ${case_dir}")
  endif()
  if(case_name STREQUAL "dc_glitch_assert")
    if(NOT reference_stdout MATCHES "glitch" AND NOT reference_stderr MATCHES "glitch")
      message(FATAL_ERROR "${case_name}: the reference must report the glitch\n"
        "workspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_glitch_count")
    if("${reference_result}" STREQUAL "0")
      message(FATAL_ERROR "${case_name}: the reference must fail its range check\n"
        "workspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_nested_loop")
    if(NOT kept EQUAL 0 OR NOT collapsed_profile MATCHES " loop=1")
      message(FATAL_ERROR
        "${case_name}: the data-dependent inner loop must keep its process "
        "uncollapsed (kept ${kept})\n${collapsed_profile}\nworkspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_observe_glitch")
    # Six input changes, each a 0-1-0 glitch on q, after q's first value.
    if(NOT reference_stdout MATCHES "events=([0-9]+)" OR CMAKE_MATCH_1 LESS 13)
      message(FATAL_ERROR "${case_name}: the testbench must see every glitch\n"
        "${reference_stdout}\nworkspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_attributes")
    if(NOT reference_stdout MATCHES "q=9 ")
      message(FATAL_ERROR "${case_name}: each of the nine pulses must count\n"
        "${reference_stdout}\nworkspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_regions")
    string(REGEX MATCHALL "x='[01]'" reports "${reference_stdout}\n${reference_stderr}")
    list(LENGTH reports report_count)
    if(report_count LESS 4)
      message(FATAL_ERROR "${case_name}: each value of x must be reported "
        "(${report_count} reports)\nworkspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_guard")
    string(REGEX MATCHALL "detected a metavalue" warnings
      "${reference_stdout}\n${reference_stderr}")
    list(LENGTH warnings warning_count)
    if(warning_count LESS 4 OR NOT collapsed_profile MATCHES " guarded=2 "
        OR NOT collapsed_profile MATCHES " guards_open=[1-9]"
        OR NOT collapsed_profile MATCHES " guards_closed=[1-9]")
      message(FATAL_ERROR
        "${case_name}: the guarded conversion must collapse, and its guard "
        "must open and close (${warning_count} warnings)\n${collapsed_profile}\n"
        "workspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_chain")
    if(kept LESS 9 OR NOT collapsed_profile MATCHES "levels=9")
      message(FATAL_ERROR
        "${case_name}: the rotation and the eight cells must collapse in "
        "nine levels (kept ${kept})\n${collapsed_profile}\nworkspace: ${case_dir}")
    endif()
  elseif(case_name STREQUAL "dc_static")
    if(kept LESS 9)
      message(FATAL_ERROR
        "${case_name}: the cells, the rotation and the packers must collapse "
        "(kept ${kept})\n${collapsed_profile}\nworkspace: ${case_dir}")
    endif()
    run_fsim_step("${case_name}" elaborate_aot
      elaborate -q --top "${case_name}_tb" --aot --aot-scope kernel --optimization O2)
    set(ENV{FSIM_KERNEL_IMAGE_REQUIRED} 1)
    set(ENV{FSIM_PROFILE_PHASES} 1)
    simulate_step(restored)
    unset(ENV{FSIM_PROFILE_PHASES})
    unset(ENV{FSIM_KERNEL_IMAGE_REQUIRED})
    foreach(part IN ITEMS result stdout stderr)
      if(NOT "${restored_${part}}" STREQUAL "${reference_${part}}")
        message(FATAL_ERROR
          "${case_name}: the restored run's ${part} differs from the reference\n"
          "Reference:\n${reference_${part}}\nRestored:\n${restored_${part}}\n"
          "workspace: ${case_dir}")
      endif()
    endforeach()
    if(NOT restored_profile MATCHES "static-kernel-image restore=restored"
        OR NOT restored_profile MATCHES "static-kernel-collapse planned=[0-9]+ kept=${kept} ")
      message(FATAL_ERROR
        "${case_name}: the restored kernel image must hold the collapse plan "
        "(kept ${kept})\n${restored_profile}\nworkspace: ${case_dir}")
    endif()
  endif()
  message(STATUS "Delta collapse witness passed: ${case_name} (kept ${kept})")
endforeach()

if(NOT "$ENV{KEEP_WORKSPACE}" STREQUAL "1")
  file(REMOVE_RECURSE "${workspace}")
endif()
