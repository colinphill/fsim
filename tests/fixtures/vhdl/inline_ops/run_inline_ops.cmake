# SPDX-License-Identifier: Apache-2.0
#
# One source of this directory run on the interpreter, the compiled v4
# kernel, the compiled kernel under FSIM_KERNEL_VERIFY (every run replayed
# on the reference evaluator and compared) and the reference scheduler; the
# reports of each must equal the .expected file, whose values are derived by
# hand in the source. Arguments: FSIM_CASE (the file's base name),
# FSIM_LANGUAGE (vhdl, sv or mixed: <case>.sv and <case>_vhdl.vhd, with
# FSIM_TOP) and FSIM_STANDARD (VHDL only); optionally
# FSIM_SOURCE (the base name of the sources when several cases run one
# source with different elaboration generics; default FSIM_CASE) and
# FSIM_GENERICS (a list of NAME=VALUE top-level generics or parameters), and
# FSIM_FORBID_GENERIC, a list of operation kinds (SimIR type names such as
# DynamicPartSelect) that must not run through the operation helper in the
# kernel run: the kernel reports them with FSIM_PROFILE_PHASES as
# `reason=generic:<kind>`. FSIM_FORBID_REASON lists other profile reasons
# that must not appear, such as `tier:not_compiled:assertion` (a member that
# runs on the operation tier). FSIM_FORBID_EFFECT lists kernel instruction
# kinds (KOp names such as store_slot_dynamic or wide_alu) that generated code
# must not run through the runtime's evaluate, effect or wide helper: the
# profile reports each such site as `reason=effect:<kind>` (a full deferred
# queue's growth is `reason=capacity:<kind>`, a check's error report
# `reason=failure:<kind>` and a host signal's access `reason=host:<kind>`;
# those three are the helpers the design keeps). FSIM_LIBRARY_ONLY, when
# set, fails the case on any `reason=effect:` and on any `reason=generic:`
# other than the library operations (display, report, coverage, string,
# random, real-number math). FSIM_FORBID_NOT_COMPILED likewise lists reasons
# (compile failures such as register_width) that must not keep a member off
# the compiled kernel: `reason=tier:not_compiled:<reason>`. FSIM_FORBID_DEOPT,
# when set, fails the case when the kernel run reports any deoptimization
# (a compiled member continuing on the reference evaluator;
# FSIM_PROFILE_KERNEL's `deopts=` count). FSIM_KERNEL_ENV is a
# list of NAME=VALUE environment settings for the kernel runs (plan options of
# the kernel, such as FSIM_KERNEL_NO_MIRRORS, which leaves every host signal
# unmirrored). FSIM_REPORTS=warnings compares the simulation's warning
# reports (`warning[FSIM-HDL-REPORT]: ...`, without source locations)
# instead of the `inl` lines of a SystemVerilog case. FSIM_COMPARE_TRACE,
# when set, also runs the interpreter and the kernel with a VCD trace (which
# makes the kernel publish every change of the traced signals to the host)
# and requires the two traces to be equal. FSIM_EXPECT_ERROR, a diagnostic
# code, makes the case a run that must fail: each of the interpreter, the
# kernel and the verified kernel must exit nonzero
# with that code reported, after the reports of the .expected file (the
# reports printed before the error).
# FSIM_MIN_OWNED, a count, fails the case when the kernel run owns fewer
# signals (the static-kernel profile's owned_signals=): the signals of the
# case then live in the compiled kernel, not on the host.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the inline operations fixture directory")
endif()
foreach(required IN ITEMS FSIM_CASE FSIM_LANGUAGE)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} must be set")
  endif()
endforeach()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()
# A list argument reaches the script with its separators escaped (add_test
# writes `A\;B` into the test's command line, and -D keeps the backslashes),
# which a foreach over the variable takes for one element: unescape them so
# that every entry of FSIM_KERNEL_ENV and the FSIM_FORBID_* lists applies.
foreach(variable IN ITEMS FSIM_KERNEL_ENV FSIM_FORBID_GENERIC
    FSIM_FORBID_REASON FSIM_FORBID_NOT_COMPILED FSIM_FORBID_EFFECT)
  if(DEFINED ${variable})
    string(REPLACE "\\;" ";" ${variable} "${${variable}}")
  endif()
endforeach()
if(NOT DEFINED FSIM_SOURCE)
  set(FSIM_SOURCE "${FSIM_CASE}")
endif()
set(generic_arguments "")
foreach(generic IN LISTS FSIM_GENERICS)
  list(APPEND generic_arguments --generic "${generic}")
endforeach()

if("${FSIM_LANGUAGE}" STREQUAL "vhdl")
  set(source "${FSIM_SOURCE}.vhd")
  set(compile_arguments compile -q --standard "${FSIM_STANDARD}" "${source}")
  set(elaborate_arguments elaborate -q work.top ${generic_arguments} --no-aot)
  # A report ends in ';', which is also a list separator here, so it is not
  # part of the match.
  if("${FSIM_CASE}" STREQUAL "shared_variable" OR "${FSIM_CASE}" STREQUAL "shared_part")
    set(report_regex "shared [^;\n]*")
  else()
    set(report_regex "wide [^;\n]*")
  endif()
elseif("${FSIM_LANGUAGE}" STREQUAL "mixed")
  # A SystemVerilog file (<case>.sv) and a VHDL-2008 file (<case>_vhdl.vhd);
  # FSIM_TOP names the top level (a VHDL entity).
  if(NOT DEFINED FSIM_TOP)
    message(FATAL_ERROR "FSIM_TOP must name the top level of a mixed case")
  endif()
  set(source "${FSIM_SOURCE}.sv")
  set(second_source "${FSIM_SOURCE}_vhdl.vhd")
  set(compile_arguments compile -q "${source}")
  set(second_compile_arguments compile -q --standard 2008 "${second_source}")
  set(elaborate_arguments elaborate -q --top "${FSIM_TOP}" ${generic_arguments} --no-aot)
  set(report_regex "inl [^\n]*")
else()
  set(source "${FSIM_SOURCE}.sv")
  set(compile_arguments compile -q "${source}")
  set(elaborate_arguments elaborate -q --top top ${generic_arguments} --no-aot)
  if("${FSIM_REPORTS}" STREQUAL "warnings")
    set(report_regex "warning\\[FSIM-HDL-REPORT\\]: [^\n]*")
  else()
    set(report_regex "inl [^\n]*")
  endif()
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-inline-ops-${FSIM_CASE}-${workspace_id}")
fsim_test_workspace("${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/${source}" "${case_dir}/${source}")
if(DEFINED second_source)
  file(COPY_FILE "${FSIM_FIXTURE_DIR}/${second_source}" "${case_dir}/${second_source}")
endif()

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
  # The report texts, without source locations.
  # The kernel's own profile lines (fsim-kernel:, fsim-profile:) are not
  # reports, though one of them begins with "wide ".
  string(REGEX REPLACE "(^|\n)fsim-(kernel|profile):[^\n]*" "\\1" scanned "${stdout}\n${stderr}")
  string(REGEX MATCHALL "${report_regex}" reports "${scanned}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
  set(${STEP_NAME}_stderr "${stderr}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile ${compile_arguments})
if(DEFINED second_compile_arguments)
  run_fsim_step(compile_second ${second_compile_arguments})
endif()
run_fsim_step(elaborate ${elaborate_arguments})
if(DEFINED FSIM_EXPECT_ERROR)
  file(READ "${FSIM_FIXTURE_DIR}/${FSIM_CASE}.expected" expected)
  string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
  string(STRIP "${expected}" expected)
  foreach(run IN ITEMS interpreter kernel verified)
    set(run_environment "")
    if(run STREQUAL "verified")
      set(run_environment FSIM_KERNEL_VERIFY=1)
    endif()
    if(run STREQUAL "interpreter")
      set(run_engine interpreter)
    else()
      set(run_engine v4)
    endif()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env ${run_environment}
        "${FSIM_EXECUTABLE}" simulate --engine ${run_engine}
      WORKING_DIRECTORY "${case_dir}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE stdout
      ERROR_VARIABLE stderr)
    if("${result}" STREQUAL "0" OR NOT "${stdout}\n${stderr}" MATCHES "${FSIM_EXPECT_ERROR}")
      message(FATAL_ERROR
        "${FSIM_CASE} (${run}): the run must fail with ${FSIM_EXPECT_ERROR} "
        "(exit ${result})\nstdout:\n${stdout}\nstderr:\n${stderr}\n"
        "workspace: ${case_dir}")
    endif()
    string(REGEX REPLACE "(^|\n)fsim-(kernel|profile):[^\n]*" "\\1" scanned "${stdout}\n${stderr}")
    string(REGEX MATCHALL "${report_regex}" reports "${scanned}")
    list(JOIN reports "\n" reports)
    if(NOT "${reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "${FSIM_CASE} (${run}): the reports before the error differ from "
        "${FSIM_CASE}.expected\nExpected:\n${expected}\n${run}:\n${reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
  return()
endif()
run_fsim_step(interpreter simulate --engine interpreter)
if(DEFINED FSIM_FORBID_GENERIC OR DEFINED FSIM_FORBID_REASON
    OR DEFINED FSIM_FORBID_NOT_COMPILED OR DEFINED FSIM_FORBID_EFFECT
    OR DEFINED FSIM_MIN_OWNED OR FSIM_LIBRARY_ONLY)
  set(ENV{FSIM_PROFILE_PHASES} 1)
endif()
if(FSIM_FORBID_DEOPT)
  set(ENV{FSIM_PROFILE_KERNEL} 1)
endif()
set(kernel_env_names "")
foreach(entry IN LISTS FSIM_KERNEL_ENV)
  if(NOT entry MATCHES "^([A-Z0-9_]+)=(.*)$")
    message(FATAL_ERROR "FSIM_KERNEL_ENV entries are NAME=VALUE, not '${entry}'")
  endif()
  set(ENV{${CMAKE_MATCH_1}} "${CMAKE_MATCH_2}")
  list(APPEND kernel_env_names "${CMAKE_MATCH_1}")
endforeach()
run_fsim_step(kernel simulate --engine v4)
unset(ENV{FSIM_PROFILE_PHASES})
unset(ENV{FSIM_PROFILE_KERNEL})
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})
foreach(name IN LISTS kernel_env_names)
  unset(ENV{${name}})
endforeach()
if(FSIM_COMPARE_TRACE)
  run_fsim_step(traced_interpreter simulate --engine interpreter
    --trace trace-interpreter.vcd)
  run_fsim_step(traced_kernel simulate --engine v4 --trace trace-kernel.vcd)
  file(READ "${case_dir}/trace-interpreter.vcd" interpreter_trace)
  file(READ "${case_dir}/trace-kernel.vcd" kernel_trace)
  if(NOT "${interpreter_trace}" STREQUAL "${kernel_trace}")
    message(FATAL_ERROR
      "${FSIM_CASE}: the kernel's trace differs from the interpreter's\n"
      "workspace: ${case_dir}")
  endif()
endif()
set(ENV{FSIM_STATIC_KERNEL} 0)
run_fsim_step(elaborate_scheduler ${elaborate_arguments})
run_fsim_step(scheduler simulate --engine v4)
unset(ENV{FSIM_STATIC_KERNEL})

file(READ "${FSIM_FIXTURE_DIR}/${FSIM_CASE}.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)
foreach(run IN ITEMS interpreter kernel verified scheduler)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "${FSIM_CASE} (${run}): the reports differ from ${FSIM_CASE}.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

if(DEFINED FSIM_MIN_OWNED)
  set(owned_signals -1)
  if("${kernel_stderr}" MATCHES "static-kernel disabled=0 [^\n]*owned_signals=([0-9]+)")
    set(owned_signals "${CMAKE_MATCH_1}")
  endif()
  if(owned_signals LESS FSIM_MIN_OWNED)
    message(FATAL_ERROR
      "${FSIM_CASE}: the kernel owns ${owned_signals} signals, fewer than ${FSIM_MIN_OWNED}\n"
      "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
endif()

foreach(kind IN LISTS FSIM_FORBID_GENERIC)
  if("${kernel_stderr}" MATCHES "reason=generic:${kind}[ \n]")
    message(FATAL_ERROR
      "${FSIM_CASE}: a ${kind} operation runs through the operation helper\n"
      "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
endforeach()

foreach(reason IN LISTS FSIM_FORBID_REASON)
  if("${kernel_stderr}" MATCHES "reason=${reason}[ \n]")
    message(FATAL_ERROR
      "${FSIM_CASE}: the kernel run reports reason=${reason}\n"
      "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
endforeach()

foreach(reason IN LISTS FSIM_FORBID_NOT_COMPILED)
  if("${kernel_stderr}" MATCHES "reason=tier:not_compiled:${reason}[ \n]")
    message(FATAL_ERROR
      "${FSIM_CASE}: a member stays off the compiled kernel (${reason})\n"
      "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
endforeach()

if(FSIM_FORBID_DEOPT)
  if(NOT "${kernel_stderr}" MATCHES "fsim-kernel: deopts=0 ")
    message(FATAL_ERROR
      "${FSIM_CASE}: the kernel run reports a deoptimization (or no profile)\n"
      "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
endif()

foreach(kind IN LISTS FSIM_FORBID_EFFECT)
  if("${kernel_stderr}" MATCHES "reason=effect:${kind}[ \n]")
    message(FATAL_ERROR
      "${FSIM_CASE}: a ${kind} instruction runs through the runtime helper\n"
      "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
endforeach()

if(FSIM_LIBRARY_ONLY)
  if("${kernel_stderr}" MATCHES "reason=effect:([a-z_0-9]+)")
    message(FATAL_ERROR
      "${FSIM_CASE}: a ${CMAKE_MATCH_1} instruction runs through the runtime helper\n"
      "stderr:\n${kernel_stderr}\nworkspace: ${case_dir}")
  endif()
  string(REGEX MATCHALL "reason=generic:[A-Za-z0-9_]+" generic_reasons "${kernel_stderr}")
  foreach(reason IN LISTS generic_reasons)
    string(REPLACE "reason=generic:" "" kind "${reason}")
    if(NOT kind MATCHES "^(Display|FormatDisplay|StringDisplay|TimeDisplay|Report|RandomValue|CountOnes|CountBits|ConcatenateStrings|LoadStringConstant|CopyStringRegister|PlusArgSelect|CodeCoverageHit|CodeCoverageToggle|SystemVerilogMath|SystemVerilogScalarBinary)$")
      message(FATAL_ERROR
        "${FSIM_CASE}: a ${kind} operation runs through the operation helper "
        "(only library operations may)\nstderr:\n${kernel_stderr}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endif()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()
