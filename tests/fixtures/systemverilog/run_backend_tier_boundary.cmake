# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the tier fixture directory")
endif()
if(NOT DEFINED FSIM_TIER_INSTANCE_COUNT
    OR NOT FSIM_TIER_INSTANCE_COUNT MATCHES "^(63|64)$")
  message(FATAL_ERROR "FSIM_TIER_INSTANCE_COUNT must be 63 or 64")
endif()
if(NOT DEFINED FSIM_TIER_OPTIMIZATION
    OR NOT FSIM_TIER_OPTIMIZATION MATCHES "^O[02]$")
  message(FATAL_ERROR "FSIM_TIER_OPTIMIZATION must be O0 or O2")
endif()

if(FSIM_TIER_INSTANCE_COUNT EQUAL 63)
  set(expected_backend_tier none)
else()
  set(expected_backend_tier less)
endif()
set(top_name "backend_tier_boundary_${FSIM_TIER_INSTANCE_COUNT}")

# Each CTest invocation has its own fresh project/cache workspace. Keep the
# profile streams as artifacts so the admission decision can be reviewed.
unset(ENV{FSIM_JIT_PROCESS_IDS})
unset(ENV{FSIM_JIT_PROMOTE_TINY_RECURRING})
unset(ENV{FSIM_DUMP_LLVM_PROCESS})
unset(ENV{FSIM_LLVM_ARGS})
set(ENV{FSIM_PROFILE_JIT} 1)
set(ENV{FSIM_PROFILE_JIT_MODULES} 1)
set(ENV{FSIM_PROFILE_LLVM_MODULES} 1)

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
string(CONCAT case_dir
  "${CMAKE_CURRENT_BINARY_DIR}/fsim-backend-tier-${workspace_id}-"
  "${FSIM_TIER_INSTANCE_COUNT}-${FSIM_TIER_OPTIMIZATION}")
file(MAKE_DIRECTORY "${case_dir}")
file(COPY_FILE
  "${FSIM_FIXTURE_DIR}/backend_tier_boundary.sv"
  "${case_dir}/backend_tier_boundary.sv")
set(native_cache_root "${case_dir}/.fsim/cache/llvm-native/llvm/objects")
if(EXISTS "${native_cache_root}")
  message(FATAL_ERROR
    "new tier-boundary workspace unexpectedly has a populated native cache: "
    "${native_cache_root}")
endif()
file(WRITE "${case_dir}/environment.txt"
  "FSIM_PROFILE_JIT=1\n"
  "FSIM_PROFILE_JIT_MODULES=1\n"
  "FSIM_PROFILE_LLVM_MODULES=1\n"
  "FSIM_JIT_PROCESS_IDS=<unset>\n"
  "FSIM_JIT_PROMOTE_TINY_RECURRING=<unset>\n"
  "FSIM_DUMP_LLVM_PROCESS=<unset>\n"
  "FSIM_LLVM_ARGS=<unset>\n"
  "fsim_executable=${FSIM_EXECUTABLE}\n"
  "workspace=${case_dir}\n"
  "native_cache_root=${native_cache_root}\n"
  "native_cache_root_absent_before_simulate=1\n"
  "template_instances=${FSIM_TIER_INSTANCE_COUNT}\n"
  "expected_backend_tier=${expected_backend_tier}\n"
  "optimization=${FSIM_TIER_OPTIMIZATION}\n")

function(run_fsim_step STEP_NAME)
  string(JOIN " " logged_arguments ${ARGN})
  file(APPEND "${case_dir}/environment.txt"
    "command_${STEP_NAME}=${FSIM_EXECUTABLE} ${logged_arguments}\n")
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
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\n"
      "workspace: ${case_dir}")
  endif()
endfunction()

run_fsim_step(compile compile -q backend_tier_boundary.sv)
run_fsim_step(elaborate elaborate -q --top "${top_name}" --no-aot
  --optimization "${FSIM_TIER_OPTIMIZATION}")
if(EXISTS "${native_cache_root}")
  message(FATAL_ERROR
    "--no-aot elaboration populated the native cache before the cold JIT run: "
    "${native_cache_root}")
endif()
run_fsim_step(simulate simulate --engine compiled)
if(NOT IS_DIRECTORY "${native_cache_root}")
  message(FATAL_ERROR
    "compiled simulation did not use its isolated native-cache root: "
    "${native_cache_root}")
endif()
file(GLOB_RECURSE native_cache_entries LIST_DIRECTORIES false
  "${native_cache_root}/*")
if(NOT native_cache_entries)
  message(FATAL_ERROR
    "cold compiled simulation created no native cache objects under: "
    "${native_cache_root}")
endif()

file(READ "${case_dir}/simulate.stdout" actual)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REGEX REPLACE
  "(^|\n)simulation (completed|stopped) at tick [0-9]+, delta [0-9]+(\n|$)"
  "\\1" actual "${actual}")
file(WRITE "${case_dir}/actual.out" "${actual}")
set(expected "BACKEND_TIER_BOUNDARY_${FSIM_TIER_INSTANCE_COUNT} PASS\n")
file(WRITE "${case_dir}/expected.out" "${expected}")
if(NOT actual STREQUAL expected)
  file(READ "${case_dir}/simulate.stderr" stderr)
  message(FATAL_ERROR
    "compiled transcript mismatch for ${top_name} at "
    "${FSIM_TIER_OPTIMIZATION}\nExpected:\n${expected}"
    "Actual:\n${actual}\nstderr:\n${stderr}\n"
    "workspace: ${case_dir}")
endif()

file(READ "${case_dir}/simulate.stderr" profile)
string(REGEX MATCH
  "fsim-profile: jit setup_ms=[^\r\n]* selective_large_design=1 compile_all=0"
  large_design_summary "${profile}")
if(NOT large_design_summary)
  message(FATAL_ERROR
    "fixture did not exercise selective compilation of a 128+ process "
    "design\n${profile}\nworkspace: ${case_dir}")
endif()

string(REGEX MATCHALL
  "fsim jit module profile: identity=[^\r\n]*"
  app_module_records "${profile}")
set(boundary_records)
foreach(record IN LISTS app_module_records)
  if(record MATCHES
      "bound_instance_counts=([0-9,]+) process_ids=([0-9,]+)")
    set(instance_counts "${CMAKE_MATCH_1}")
    set(process_ids "${CMAKE_MATCH_2}")
    string(REPLACE "," ";" instance_counts "${instance_counts}")
    string(REPLACE "," ";" process_ids "${process_ids}")
    list(LENGTH instance_counts instance_count_length)
    list(LENGTH process_ids process_id_length)
    if(NOT instance_count_length EQUAL process_id_length)
      message(FATAL_ERROR
        "profile instance counts do not align with process IDs\n${record}\n"
        "workspace: ${case_dir}")
    endif()
    math(EXPR last_instance_index "${instance_count_length} - 1")
    foreach(index RANGE 0 ${last_instance_index})
      list(GET instance_counts ${index} instance_count)
      if("${instance_count}" STREQUAL "${FSIM_TIER_INSTANCE_COUNT}")
        list(GET process_ids ${index} representative_process_id)
        list(APPEND boundary_records "${record}")
      endif()
    endforeach()
  endif()
endforeach()
list(LENGTH boundary_records boundary_record_count)
if(NOT boundary_record_count EQUAL 1)
  message(FATAL_ERROR
    "expected one module with ${FSIM_TIER_INSTANCE_COUNT} bound instances, "
    "found ${boundary_record_count}\n${profile}\nworkspace: ${case_dir}")
endif()

string(REGEX MATCHALL
  "fsim-profile: llvm-module identity='[^\r\n]* backend_tier=less[^\r\n]*"
  less_records "${profile}")
list(LENGTH less_records less_record_count)
if(FSIM_TIER_INSTANCE_COUNT EQUAL 63)
  if(NOT less_record_count EQUAL 0)
    message(FATAL_ERROR
      "63-instance cold module unexpectedly selected Less\n${profile}\n"
      "workspace: ${case_dir}")
  endif()
  string(REGEX MATCHALL
    "fsim-profile: llvm-module identity='[^\r\n]* backend_tier=none[^\r\n]*"
    none_records "${profile}")
  set(expected_record_tier none)
else()
  if(NOT less_record_count EQUAL 1)
    message(FATAL_ERROR
      "64-instance cold module must select exactly one Less module, found "
      "${less_record_count}\n${profile}\nworkspace: ${case_dir}")
  endif()
  set(none_records "${less_records}")
  set(expected_record_tier less)
endif()

set(matching_tier_records)
foreach(record IN LISTS none_records)
  if(record MATCHES "process_ids=([0-9,]+)")
    set(module_process_ids "${CMAKE_MATCH_1}")
    string(REPLACE "," ";" module_process_ids "${module_process_ids}")
    list(FIND module_process_ids "${representative_process_id}" process_index)
    if(NOT process_index EQUAL -1)
      list(APPEND matching_tier_records "${record}")
    endif()
  endif()
endforeach()
list(LENGTH matching_tier_records matching_tier_record_count)
if(NOT matching_tier_record_count EQUAL 1)
  message(FATAL_ERROR
    "bound-template representative ${representative_process_id} has no "
    "unique ${expected_record_tier} LLVM module record\n${profile}\n"
    "workspace: ${case_dir}")
endif()

if(expected_record_tier STREQUAL "less")
  list(GET matching_tier_records 0 less_record)
  if(NOT less_record MATCHES "optimized_ir_instructions=([0-9]+)")
    message(FATAL_ERROR
      "Less module profile omits its optimized IR count\n${less_record}\n"
      "workspace: ${case_dir}")
  endif()
  if(CMAKE_MATCH_1 GREATER 16384)
    message(FATAL_ERROR
      "Less module exceeded the 16384 optimized IR cap\n${less_record}\n"
      "workspace: ${case_dir}")
  endif()
endif()

message(STATUS
  "Cold tier admission passed: ${FSIM_TIER_INSTANCE_COUNT} bound instances "
  "selected ${expected_record_tier} at ${FSIM_TIER_OPTIMIZATION}; "
  "workspace: ${case_dir}")
