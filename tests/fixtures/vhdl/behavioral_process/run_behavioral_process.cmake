# SPDX-License-Identifier: Apache-2.0
#
# VHDL behavioral processes (IEEE 1076-2019 11.3) in the v4 kernel. For each
# fixture (waits.vhd: wait statements and zero-delay assignments; delays.vhd:
# delayed, transport, reject ... inertial and multi-element assignments), the
# report lines of the interpreter, of the compiled v4 kernel and of the kernel
# under FSIM_KERNEL_VERIFY must equal <fixture>.expected, which is derived from
# the LRM by hand (10.2, 10.5.2.2, 14.7.2). The order of reports within a
# delta cycle is unspecified (14.7.5.2), so both sides are sorted.
# kernel_paths.vhd covers kernel paths once taken wrongly: a delayed write
# through sliced formal outputs, a 65-bit element store at a variable index,
# real operands of math_real calls and a postponed process's delayed write.
#
# Profile check on waits.vhd, delays.vhd, images.vhd, members.vhd and
# kernel_paths.vhd (string operations
# through the generic bridge): every process is a compiled
# kernel member (FSIM_PROFILE_KERNEL's compiled_members=N/N) and none runs on
# the operation tier (no tier:not_compiled row under FSIM_PROFILE_PHASES).
# delays.vhd's delayed, transport, reject ... inertial and multi-element
# assignments edit the kernel's projected output waveforms (KOp::net_delay,
# sub 1).
#
# FSIM_KERNEL_VERIFY also replays every compiled thread activation (resume
# entry to next suspension) on the reference evaluator with its effects
# shadowed, and compares writes, host and delayed writes, the slots' words,
# the operands and results of operations run through the generic bridge
# (output, files, strings), registers, the call stack and the suspension. Each
# fixture's verified run must report verified_thread_runs > 0 and skip no
# member, and FSIM_KERNEL_VERIFY_INJECT=thread, which makes the check report
# its first verified activation as a mismatch, must fail the run with an
# fsim-kernel-verify message. members.vhd (delayed assignments in processes with
# a sensitivity list) also needs verified_member_runs > 0 and the member
# injection (FSIM_KERNEL_VERIFY_INJECT=member).
#
if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the VHDL behavioral_process fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-vhdl-behavioral-process-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")

function(run_fsim_step STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
  endif()
  string(REGEX MATCHALL "note\\[FSIM-HDL-REPORT\\]: [^\n]*"
    reports "${stdout}\n${stderr}")
  list(TRANSFORM reports REPLACE "^note\\[FSIM-HDL-REPORT\\]: " "")
  list(SORT reports)
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
  set(${STEP_NAME}_stderr "${stderr}" PARENT_SCOPE)
endfunction()

function(check_verified_threads NAME)
  if(NOT verified_stderr MATCHES "verified_thread_runs=([0-9]+)"
      OR "${CMAKE_MATCH_1}" EQUAL 0)
    message(FATAL_ERROR
      "${NAME}: FSIM_KERNEL_VERIFY replayed no thread activation\n"
      "stderr:\n${verified_stderr}\nworkspace: ${case_dir}")
  endif()
  if(verified_stderr MATCHES "verify_skipped reason=([a-z_]+)")
    message(FATAL_ERROR
      "${NAME}: FSIM_KERNEL_VERIFY skipped members (${CMAKE_MATCH_1})\n"
      "stderr:\n${verified_stderr}\nworkspace: ${case_dir}")
  endif()
endfunction()

function(check_verified_members NAME)
  if(NOT verified_stderr MATCHES "verified_member_runs=([0-9]+)"
      OR "${CMAKE_MATCH_1}" EQUAL 0)
    message(FATAL_ERROR
      "${NAME}: FSIM_KERNEL_VERIFY replayed no member activation\n"
      "stderr:\n${verified_stderr}\nworkspace: ${case_dir}")
  endif()
endfunction()

function(check_injected_mismatch NAME)
  set(mode thread)
  if(ARGC GREATER 1)
    set(mode "${ARGV1}")
  endif()
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  set(ENV{FSIM_KERNEL_VERIFY_INJECT} ${mode})
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" simulate --engine v4
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  unset(ENV{FSIM_KERNEL_VERIFY_INJECT})
  unset(ENV{FSIM_KERNEL_VERIFY})
  if("${result}" STREQUAL "0"
      OR NOT "${stdout}\n${stderr}" MATCHES "fsim-kernel-verify: t=[0-9]+ [^\n]* injected mismatch")
    message(FATAL_ERROR
      "${NAME}: the injected ${mode} mismatch was not reported (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
  endif()
endfunction()

function(check_fixture NAME)
  file(COPY_FILE "${FSIM_FIXTURE_DIR}/${NAME}.vhd" "${case_dir}/${NAME}.vhd")
  run_fsim_step(compile compile -q --standard 2008 ${NAME}.vhd)
  run_fsim_step(elaborate elaborate -q work.${NAME} --no-aot)
  run_fsim_step(interpreter simulate --engine interpreter)
  set(ENV{FSIM_PROFILE_PHASES} 1)
  set(ENV{FSIM_PROFILE_KERNEL} 1)
  run_fsim_step(kernel simulate --engine v4)
  unset(ENV{FSIM_PROFILE_PHASES})
  unset(ENV{FSIM_PROFILE_KERNEL})
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  set(ENV{FSIM_PROFILE_KERNEL} 1)
  run_fsim_step(verified simulate --engine v4)
  unset(ENV{FSIM_PROFILE_KERNEL})
  unset(ENV{FSIM_KERNEL_VERIFY})

  file(STRINGS "${FSIM_FIXTURE_DIR}/${NAME}.expected" expected_lines)
  list(SORT expected_lines)
  list(JOIN expected_lines "\n" expected)
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "${NAME} (${run}): the reports differ from ${NAME}.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()

  if("${NAME}" STREQUAL "waits" OR "${NAME}" STREQUAL "delays"
      OR "${NAME}" STREQUAL "images" OR "${NAME}" STREQUAL "members"
      OR "${NAME}" STREQUAL "kernel_paths")
    check_verified_threads(${NAME})
    check_injected_mismatch(${NAME})
    if("${NAME}" STREQUAL "members")
      check_verified_members(members)
      check_injected_mismatch(members member)
    endif()
    if(NOT kernel_stderr MATCHES "compiled_members=([0-9]+)/([0-9]+)")
      message(FATAL_ERROR
        "${NAME}: no compiled_members line in the kernel profile\n"
        "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
    endif()
    set(compiled "${CMAKE_MATCH_1}")
    set(members "${CMAKE_MATCH_2}")
    if("${members}" EQUAL 0 OR NOT "${compiled}" EQUAL "${members}")
      message(FATAL_ERROR
        "${NAME}: ${compiled} of ${members} behavioral members are compiled\n"
        "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
    endif()
    if("${kernel_stderr}" MATCHES "tier:not_compiled")
      message(FATAL_ERROR
        "${NAME}: a member runs on the operation tier\n"
        "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
    endif()
  endif()
endfunction()

check_fixture(waits)
check_fixture(delays)
check_fixture(images)
check_fixture(members)
check_fixture(kernel_paths)

# SystemVerilog threads: threads.sv (delays, an event control, nonblocking
# writes) and classes.sv (one thread per class of effect the reference replay
# shadows: blocking write, $display, task call, fork-join_none, string
# operations, file output and input). Their output lines must equal the
# .expected files on every engine, and the verified run must replay thread
# activations without skipping a member.
function(check_sv_fixture NAME)
  # One workspace per fixture: both define a module `top`, and a library does
  # not hold two units of one name.
  set(case_dir "${case_dir}/${NAME}")
  file(MAKE_DIRECTORY "${case_dir}")
  file(COPY_FILE "${FSIM_FIXTURE_DIR}/${NAME}.sv" "${case_dir}/${NAME}.sv")
  run_fsim_step(sv_compile compile -q ${NAME}.sv)
  run_fsim_step(sv_elaborate elaborate -q top --no-aot)
  file(STRINGS "${FSIM_FIXTURE_DIR}/${NAME}.expected" sv_expected_lines)
  list(SORT sv_expected_lines)
  list(JOIN sv_expected_lines "\n" sv_expected)
  foreach(run IN ITEMS interpreter kernel verified)
    set(engine v4)
    if("${run}" STREQUAL "interpreter")
      set(engine interpreter)
    endif()
    if("${run}" STREQUAL "verified")
      set(ENV{FSIM_KERNEL_VERIFY} 1)
      set(ENV{FSIM_PROFILE_KERNEL} 1)
    endif()
    execute_process(
      COMMAND "${FSIM_EXECUTABLE}" simulate --engine ${engine}
      WORKING_DIRECTORY "${case_dir}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE stdout
      ERROR_VARIABLE stderr)
    unset(ENV{FSIM_KERNEL_VERIFY})
    unset(ENV{FSIM_PROFILE_KERNEL})
    if(NOT "${result}" STREQUAL "0")
      message(FATAL_ERROR
        "${NAME} (${run}) failed (${result})\n"
        "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
    endif()
    string(REGEX MATCHALL "[^\n]+" lines "${stdout}")
    list(FILTER lines EXCLUDE REGEX "^(simulation (completed|stopped)|fsim)")
    list(SORT lines)
    list(JOIN lines "\n" lines)
    if(NOT "${lines}" STREQUAL "${sv_expected}")
      message(FATAL_ERROR
        "${NAME} (${run}): output differs from ${NAME}.expected\n"
        "Expected:\n${sv_expected}\n${run}:\n${lines}\nworkspace: ${case_dir}")
    endif()
    set(${run}_stderr "${stderr}")
  endforeach()
  check_verified_threads(${NAME})
  check_injected_mismatch(${NAME})
endfunction()

check_sv_fixture(threads)
check_sv_fixture(classes)
