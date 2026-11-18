# SPDX-License-Identifier: Apache-2.0
#
# The VCD var_type of each net and variable (IEEE 1800-2023 21.7.2.1), in
# the $dumpvars file and in the --trace file alike.

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the VCD var type fixture directory")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-vcd-var-types-${workspace_id}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/vcd_var_types.sv" "${case_dir}/vcd_var_types.sv")

function(run_fsim_step STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${case_dir}/${STEP_NAME}.stdout"
    ERROR_FILE "${case_dir}/${STEP_NAME}.stderr")
  if(NOT "${result}" STREQUAL "0")
    file(READ "${case_dir}/${STEP_NAME}.stderr" stderr)
    message(FATAL_ERROR "${STEP_NAME} failed with ${result}\n${stderr}\nworkspace: ${case_dir}")
  endif()
endfunction()

run_fsim_step(compile compile -q vcd_var_types.sv)
run_fsim_step(elaborate elaborate -q --top vcd_var_types)
run_fsim_step(simulate simulate --trace trace.vcd)

set(expected
  "reg 1 l" "reg 1 b" "reg 1 r" "reg 4 v" "wire 1 w" "tri 1 tw"
  "supply0 1 s0" "wand 1 wa" "integer 32 i" "integer 32 n" "integer 8 by"
  "integer 64 li" "time 64 tm" "real 1 re" "realtime 1 rt" "real 1 sr"
  "reg 1 cy" "reg 1 cz" "wire 1 a" "reg 1 y" "wire 1 z")
foreach(file IN ITEMS dump.vcd trace.vcd)
  file(READ "${case_dir}/${file}" text)
  foreach(entry IN LISTS expected)
    string(REPLACE " " ";" fields "${entry}")
    list(GET fields 0 keyword)
    list(GET fields 1 width)
    list(GET fields 2 name)
    if(NOT text MATCHES "\n\\$var ${keyword} ${width} [^ \n]+ ${name} \\$end\n")
      message(FATAL_ERROR "${file}: ${name} is not declared `$var ${keyword} ${width}`\n"
        "${text}\nworkspace: ${case_dir}")
    endif()
  endforeach()
endforeach()
file(REMOVE_RECURSE "${case_dir}")
