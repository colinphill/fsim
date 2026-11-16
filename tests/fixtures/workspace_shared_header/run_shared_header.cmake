# SPDX-License-Identifier: Apache-2.0
#
# A package in a header that two separately compiled sources include is one
# package (IEEE 1800-2023 26.2: one declaration, imported by both): both
# objects own a copy with the same content digest, and the copies link as
# one unit, with its class. The header is reached by different path
# spellings (inc/pkg.svh, ../inc/pkg.svh, an absolute source path), which
# name one file. Copies that differ are refused at compile with a message
# that asks to recompile the including sources together: the header changed
# after one source was compiled, or two files declare the package.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the shared header fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

function(prepare_case CASE)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-shared-header/${CASE}")
  file(REMOVE_RECURSE "${case_dir}")
  file(MAKE_DIRECTORY "${case_dir}/inc" "${case_dir}/sub")
  configure_file("${FSIM_FIXTURE_DIR}/inc/pkg.svh" "${case_dir}/inc/pkg.svh" COPYONLY)
  configure_file("${FSIM_FIXTURE_DIR}/a.sv" "${case_dir}/a.sv" COPYONLY)
  configure_file("${FSIM_FIXTURE_DIR}/sub/b.sv" "${case_dir}/sub/b.sv" COPYONLY)
  set(case_dir "${case_dir}" PARENT_SCOPE)
endfunction()

# Runs one step; EXPECT is 0 (must pass) or 1 (must fail). The output is
# returned in step_output.
function(run_step CASE_DIR EXPECT)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${CASE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(EXPECT EQUAL 0 AND NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "fsim ${ARGN} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${CASE_DIR}")
  endif()
  if(EXPECT EQUAL 1 AND "${result}" STREQUAL "0")
    message(FATAL_ERROR "fsim ${ARGN} passed but must fail\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${CASE_DIR}")
  endif()
  set(step_output "${stdout}\n${stderr}" PARENT_SCOPE)
endfunction()

function(expect_simulation CASE_DIR A B)
  run_step("${CASE_DIR}" 0 elaborate -q work.top --no-aot)
  foreach(engine IN ITEMS interpreter v4)
    run_step("${CASE_DIR}" 0 simulate --engine ${engine})
    foreach(line IN ITEMS "shared A=${A} 7" "shared B=${B}")
      string(FIND "${step_output}" "${line}" found)
      if(found EQUAL -1)
        message(FATAL_ERROR "${engine} run of ${CASE_DIR} lacks '${line}':\n${step_output}")
      endif()
    endforeach()
  endforeach()
endfunction()

function(expect_differing_copies OUTPUT)
  string(FIND "${OUTPUT}" "copies differ" found)
  string(FIND "${OUTPUT}" "different copy of this unit" found_catalog)
  if(found EQUAL -1 AND found_catalog EQUAL -1)
    message(FATAL_ERROR "differing copies not reported as such:\n${OUTPUT}")
  endif()
endfunction()

# One compile of both sources.
prepare_case(together)
run_step("${case_dir}" 0 compile -q a.sv sub/b.sv)
expect_simulation("${case_dir}" 10 5)

# Separate compiles, relative spellings.
prepare_case(separate)
run_step("${case_dir}" 0 compile -q a.sv)
run_step("${case_dir}" 0 compile -q sub/b.sv)
expect_simulation("${case_dir}" 10 5)
# Recompiling one source keeps the other's copy.
run_step("${case_dir}" 0 compile -q a.sv)
expect_simulation("${case_dir}" 10 5)

# Separate compiles, one absolute source path.
prepare_case(absolute)
run_step("${case_dir}" 0 compile -q a.sv)
run_step("${case_dir}" 0 compile -q "${case_dir}/sub/b.sv")
expect_simulation("${case_dir}" 10 5)

# The header changes after both were compiled: one source alone is refused,
# both together are compiled.
prepare_case(changed)
run_step("${case_dir}" 0 compile -q a.sv)
run_step("${case_dir}" 0 compile -q sub/b.sv)
file(READ "${case_dir}/inc/pkg.svh" header)
string(REPLACE "VALUE = 5" "VALUE = 6" header "${header}")
file(WRITE "${case_dir}/inc/pkg.svh" "${header}")
run_step("${case_dir}" 1 compile -q sub/b.sv)
expect_differing_copies("${step_output}")
run_step("${case_dir}" 0 compile -q a.sv sub/b.sv)
expect_simulation("${case_dir}" 12 6)

# The same package text in two files is two declarations of one name.
prepare_case(two_files)
configure_file("${FSIM_FIXTURE_DIR}/inc/pkg.svh" "${case_dir}/inc/copy.svh" COPYONLY)
file(READ "${case_dir}/sub/b.sv" source)
string(REPLACE "../inc/pkg.svh" "../inc/copy.svh" source "${source}")
file(WRITE "${case_dir}/sub/b.sv" "${source}")
run_step("${case_dir}" 0 compile -q a.sv)
run_step("${case_dir}" 1 compile -q sub/b.sv)
expect_differing_copies("${step_output}")
