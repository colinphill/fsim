# SPDX-License-Identifier: Apache-2.0
#
# Clock domain analysis (FSIM_PROFILE_KERNEL_DOMAINS). Each witness is
# simulated on the v4 kernel with the domain report and on the reference
# scheduler (FSIM_STATIC_KERNEL=0); the exit status, transcript and
# diagnostics must be equal, and the report must classify the clocks:
#   cd_divider: div2 is derived from clk, div4 from div2, and the counter
#     with an asynchronous reset (`process (div4, rst)`) is a flop on div4;
#   cd_icg: the gated clock's enable comes through a latch transparent
#     while clk is low, so the gate is a candidate;
#   cd_glitch: the enable is a flop on clk's rising edge, so the gate can
#     pulse after the edge and is refused;
#   cd_edge: RISING_EDGE rejects 'U' to '1', '0' to 'X' and 'X' to '1' in a
#     process whose sensitivity elaboration narrows to the clock's edges
#     (the counter ends at 2, not 5).

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the clock domain fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL_DELTA_COLLAPSE FSIM_STATIC_KERNEL
    FSIM_STATIC_KERNEL_PLAN_CACHE FSIM_KERNEL_VERIFY FSIM_PROFILE_PHASES
    FSIM_PROFILE_KERNEL FSIM_PROFILE_KERNEL_DOMAINS
    FSIM_KERNEL_IMAGE_REQUIRED FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/fsim-clock-domains-${workspace_id}")
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

set(cases cd_divider cd_icg cd_glitch cd_edge cd_buffer)
set(expect_cd_divider
  "fsim-kernel: domains count=[0-9]+ members=[0-9]+ async_flops=1"
  "domain-derived clock=cd_divider_tb\\.dut\\.div2 edge=pos parent=cd_divider_tb\\.clk/pos"
  "domain-derived clock=cd_divider_tb\\.dut\\.div4 edge=pos parent=cd_divider_tb\\.dut\\.div2/pos")
set(expect_cd_icg
  "domain-gate clock=cd_icg_tb\\.dut\\.gclk edge=pos source=cd_icg_tb\\.clk [^\n]* gate=candidate"
  "domain-admission pending_gate_certificate members=[1-9]")
set(expect_cd_glitch
  "domain-gate clock=cd_glitch_tb\\.dut\\.gclk edge=pos source=cd_glitch_tb\\.clk [^\n]* gate=refuse_same_edge_enable"
  "domain-admission refused_gate_same_edge_enable members=[1-9]")

set(expect_cd_buffer
  "domain-gate clock=cd_buffer_tb\\.dut\\.clk_b edge=pos source=[^ ]* source_depths=1 enables=0 gate=buffer"
  "domain-admission pending_vhdl_flop_proof members=1 ")

set(expect_stdout_cd_edge "count=2")

foreach(case_name IN LISTS cases)
  set(case_dir "${workspace}/${case_name}")
  file(MAKE_DIRECTORY "${case_dir}")
  foreach(file IN ITEMS clock_domains_dut.vhd ${case_name}_tb.sv)
    file(COPY_FILE "${FSIM_FIXTURE_DIR}/${file}" "${case_dir}/${file}")
  endforeach()
  run_fsim_step("${case_name}" compile_vhdl
    compile -q --standard 2008 clock_domains_dut.vhd)
  run_fsim_step("${case_name}" compile_sv compile -q "${case_name}_tb.sv")

  run_fsim_step("${case_name}" elaborate_kernel
    elaborate -q --top "${case_name}_tb" --no-aot --optimization O2)
  set(ENV{FSIM_PROFILE_KERNEL_DOMAINS} 1)
  simulate_step(kernel)
  unset(ENV{FSIM_PROFILE_KERNEL_DOMAINS})

  set(ENV{FSIM_STATIC_KERNEL} 0)
  run_fsim_step("${case_name}" elaborate_scheduler
    elaborate -q --top "${case_name}_tb" --no-aot --optimization O2)
  simulate_step(scheduler)
  unset(ENV{FSIM_STATIC_KERNEL})

  foreach(part IN ITEMS result stdout stderr)
    if(NOT "${kernel_${part}}" STREQUAL "${scheduler_${part}}")
      message(FATAL_ERROR
        "${case_name}: the kernel run's ${part} differs from the reference scheduler\n"
        "Scheduler:\n${scheduler_${part}}\nKernel:\n${kernel_${part}}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
  if(NOT "${kernel_result}" STREQUAL "0")
    message(FATAL_ERROR "${case_name}: the simulation failed (${kernel_result})\n"
      "${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
  foreach(pattern IN LISTS expect_stdout_${case_name})
    if(NOT kernel_stdout MATCHES "${pattern}")
      message(FATAL_ERROR
        "${case_name}: the transcript must match '${pattern}'\n"
        "${kernel_stdout}\nworkspace: ${case_dir}")
    endif()
  endforeach()
  foreach(pattern IN LISTS expect_${case_name})
    if(NOT kernel_profile MATCHES "${pattern}")
      message(FATAL_ERROR
        "${case_name}: the domain report must match '${pattern}'\n"
        "${kernel_profile}\nworkspace: ${case_dir}")
    endif()
  endforeach()
endforeach()

file(REMOVE_RECURSE "${workspace}")
