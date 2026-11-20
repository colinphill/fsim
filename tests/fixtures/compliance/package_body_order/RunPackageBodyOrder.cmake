# SPDX-License-Identifier: Apache-2.0
#
# Compile orders around a VHDL package body (IEEE 1076-2019 4.7, 4.8, 13.5):
# a package user depends on the package declaration only, so analyzing a new
# body before or after the user gives the same library; the body's
# completeness is decided when the body is analyzed or at elaboration.
#
#   FSIM_SCENARIO=replace_body   P + old body, user, new body: the user stays
#                                and elaborates with the new body's values.
#   FSIM_SCENARIO=deferred_*     a new declaration adds a deferred constant
#                                that only the new body supplies; every order
#                                compiles (orders A-D), elaboration before the
#                                new body reports FSIM-FE-VHDECL-001 (E).

foreach(required FSIM_EXECUTABLE FSIM_FIXTURE_DIR FSIM_SCENARIO)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} must be set")
  endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(workspace
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-pbo-${FSIM_SCENARIO}-${workspace_id}")
file(MAKE_DIRECTORY "${workspace}")
file(GLOB sources "${FSIM_FIXTURE_DIR}/*.vhd")
file(COPY ${sources} DESTINATION "${workspace}")

function(fsim_step expect_success)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${workspace}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  set(step_output "${output}" PARENT_SCOPE)
  if(expect_success AND NOT result STREQUAL "0")
    message(FATAL_ERROR "${FSIM_SCENARIO}: fsim ${ARGN} failed (${result}):\n"
      "${output}\nworkspace: ${workspace}")
  endif()
  if(NOT expect_success AND result STREQUAL "0")
    message(FATAL_ERROR "${FSIM_SCENARIO}: fsim ${ARGN} was accepted\n"
      "workspace: ${workspace}")
  endif()
endfunction()

function(fsim_compile)
  fsim_step(TRUE compile -q --lang vhdl --standard 2008 ${ARGN})
endfunction()

function(fsim_expect_report top text)
  fsim_step(TRUE elaborate -q "${top}")
  fsim_step(TRUE simulate)
  string(FIND "${step_output}" "${text}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "${FSIM_SCENARIO}: simulation of ${top} did not "
      "report '${text}':\n${step_output}\nworkspace: ${workspace}")
  endif()
endfunction()

if(FSIM_SCENARIO STREQUAL "replace_body")
  fsim_compile(p.vhd body_old.vhd)
  fsim_compile(user.vhd)
  fsim_compile(body_new.vhd)
  fsim_expect_report(pbo_user "width=8 sel=1")
elseif(FSIM_SCENARIO STREQUAL "deferred_a")
  fsim_compile(deferred_p_old.vhd)
  fsim_compile(deferred_p_new.vhd)
  fsim_compile(deferred_user.vhd)
  fsim_compile(deferred_body_new.vhd)
  fsim_expect_report(pbo_deferred_user "d=8")
elseif(FSIM_SCENARIO STREQUAL "deferred_b")
  fsim_compile(deferred_p_old.vhd)
  fsim_compile(deferred_p_new.vhd deferred_user.vhd)
  fsim_compile(deferred_body_new.vhd)
  fsim_expect_report(pbo_deferred_user "d=8")
elseif(FSIM_SCENARIO STREQUAL "deferred_c")
  fsim_compile(deferred_p_old.vhd)
  fsim_compile(deferred_p_new.vhd deferred_user.vhd deferred_body_new.vhd)
  fsim_expect_report(pbo_deferred_user "d=8")
elseif(FSIM_SCENARIO STREQUAL "deferred_d")
  fsim_compile(deferred_p_old.vhd)
  fsim_compile(deferred_p_new.vhd deferred_body_new.vhd)
  fsim_compile(deferred_user.vhd)
  fsim_expect_report(pbo_deferred_user "d=8")
elseif(FSIM_SCENARIO STREQUAL "deferred_e")
  fsim_compile(deferred_p_old.vhd)
  fsim_compile(deferred_p_new.vhd)
  fsim_compile(deferred_user.vhd)
  fsim_step(FALSE elaborate -q pbo_deferred_user)
  if(NOT step_output MATCHES "FSIM-FE-VHDECL-001")
    message(FATAL_ERROR "deferred_e: elaboration without the new body did "
      "not report FSIM-FE-VHDECL-001:\n${step_output}")
  endif()
else()
  message(FATAL_ERROR "unknown FSIM_SCENARIO ${FSIM_SCENARIO}")
endif()
