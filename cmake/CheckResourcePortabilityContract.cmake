# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ROOT "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_FOOTPRINT "${FSIM_SOURCE_DIR}/cmake/FsimDebugFootprint.cmake")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_FUZZ_CMAKE "${FSIM_SOURCE_DIR}/tests/fuzz/CMakeLists.txt")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_SCOPED "${FSIM_SOURCE_DIR}/tests/app/scoped_local_application_test.cpp")
set(FSIM_SYSTEMC "${FSIM_SOURCE_DIR}/tests/app/application_systemc_matrix_test.cpp")
set(FSIM_COVERAGE "${FSIM_SOURCE_DIR}/src/frontend/coverage_sampling.cpp")
set(FSIM_FST_WRITER "${FSIM_SOURCE_DIR}/include/fsim/runtime/fst_writer.hpp")
set(FSIM_FST_WRITER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/fst_writer.cpp")
set(FSIM_FST_WRITER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/fst_writer_tests.cpp")
set(FSIM_FST_FORMAT
  "${FSIM_SOURCE_DIR}/src/app/application_trace_format.cpp")
set(FSIM_FST_FORMAT_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_format_application_test.cpp")
set(FSIM_FST_ENCODER "${FSIM_SOURCE_DIR}/src/runtime/fst_value_encoder.cpp")
set(FSIM_FST_ENCODER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/fst_value_encoder_tests.cpp")
set(FSIM_FST_CHANGE_ENCODER
  "${FSIM_SOURCE_DIR}/src/runtime/fst_change_encoder.cpp")
set(FSIM_FST_CHANGE_ENCODER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/fst_change_encoder_tests.cpp")
set(FSIM_FST_HIERARCHY
  "${FSIM_SOURCE_DIR}/src/app/application_trace_hierarchy.cpp")
set(FSIM_FST_HIERARCHY_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_hierarchy_application_test.cpp")
set(FSIM_FST_OBSERVATION
  "${FSIM_SOURCE_DIR}/src/app/application_trace_observation.cpp")
set(FSIM_FST_OBSERVATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_observation_application_test.cpp")
set(FSIM_FST_CONTROL
  "${FSIM_SOURCE_DIR}/src/app/application_trace_control.cpp")
set(FSIM_FST_CONTROL_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_control_application_test.cpp")
set(FSIM_FST_APPLICATION "${FSIM_SOURCE_DIR}/src/app/application_run.cpp")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_ROOT}"
    "${FSIM_FOOTPRINT}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_FUZZ_CMAKE}"
    "${FSIM_WORKFLOW}"
    "${FSIM_SCOPED}"
    "${FSIM_SYSTEMC}"
    "${FSIM_COVERAGE}"
    "${FSIM_FST_WRITER}"
    "${FSIM_FST_WRITER_IMPLEMENTATION}"
    "${FSIM_FST_WRITER_TEST}"
    "${FSIM_FST_FORMAT}"
    "${FSIM_FST_FORMAT_TEST}"
    "${FSIM_FST_ENCODER}"
    "${FSIM_FST_ENCODER_TEST}"
    "${FSIM_FST_CHANGE_ENCODER}"
    "${FSIM_FST_CHANGE_ENCODER_TEST}"
    "${FSIM_FST_HIERARCHY}"
    "${FSIM_FST_HIERARCHY_TEST}"
    "${FSIM_FST_OBSERVATION}"
    "${FSIM_FST_OBSERVATION_TEST}"
    "${FSIM_FST_CONTROL}"
    "${FSIM_FST_CONTROL_TEST}"
    "${FSIM_FST_APPLICATION}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "resource contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ROOT}" FSIM_ROOT_CONTENTS)
file(READ "${FSIM_FOOTPRINT}" FSIM_FOOTPRINT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_FUZZ_CMAKE}" FSIM_FUZZ_CMAKE_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_SCOPED}" FSIM_SCOPED_CONTENTS)
file(READ "${FSIM_SYSTEMC}" FSIM_SYSTEMC_CONTENTS)
file(READ "${FSIM_COVERAGE}" FSIM_COVERAGE_CONTENTS)
file(READ "${FSIM_FST_WRITER}" FSIM_FST_WRITER_CONTENTS)
file(READ "${FSIM_FST_WRITER_IMPLEMENTATION}"
  FSIM_FST_WRITER_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_FST_WRITER_TEST}" FSIM_FST_WRITER_TEST_CONTENTS)
file(READ "${FSIM_FST_FORMAT}" FSIM_FST_FORMAT_CONTENTS)
file(READ "${FSIM_FST_FORMAT_TEST}" FSIM_FST_FORMAT_TEST_CONTENTS)
file(READ "${FSIM_FST_ENCODER}" FSIM_FST_ENCODER_CONTENTS)
file(READ "${FSIM_FST_ENCODER_TEST}" FSIM_FST_ENCODER_TEST_CONTENTS)
file(READ "${FSIM_FST_CHANGE_ENCODER}" FSIM_FST_CHANGE_ENCODER_CONTENTS)
file(READ "${FSIM_FST_CHANGE_ENCODER_TEST}"
  FSIM_FST_CHANGE_ENCODER_TEST_CONTENTS)
file(READ "${FSIM_FST_HIERARCHY}" FSIM_FST_HIERARCHY_CONTENTS)
file(READ "${FSIM_FST_HIERARCHY_TEST}" FSIM_FST_HIERARCHY_TEST_CONTENTS)
file(READ "${FSIM_FST_OBSERVATION}" FSIM_FST_OBSERVATION_CONTENTS)
file(READ "${FSIM_FST_OBSERVATION_TEST}"
  FSIM_FST_OBSERVATION_TEST_CONTENTS)
file(READ "${FSIM_FST_CONTROL}" FSIM_FST_CONTROL_CONTENTS)
file(READ "${FSIM_FST_CONTROL_TEST}" FSIM_FST_CONTROL_TEST_CONTENTS)
file(READ "${FSIM_FST_APPLICATION}" FSIM_FST_APPLICATION_CONTENTS)

string(REGEX MATCHALL "--parallel 4" FSIM_PARALLEL_STEPS "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_PARALLEL_STEPS FSIM_PARALLEL_COUNT)
if(NOT FSIM_PARALLEL_COUNT EQUAL 5)
  message(FATAL_ERROR
    "expected five four-worker hosted build steps, found ${FSIM_PARALLEL_COUNT}")
endif()
string(REGEX MATCH "--parallel ([^4]|4[^[:space:]\r\n])" FSIM_OTHER_PARALLEL "${FSIM_WORKFLOW_CONTENTS}")
if(FSIM_OTHER_PARALLEL)
  message(FATAL_ERROR "workflow contains a non-four-worker build step")
endif()

string(REGEX MATCHALL
  "timeout-minutes:[ ]*120([ \t\r\n]|$)"
  FSIM_120_MINUTE_TIMEOUTS
  "${FSIM_WORKFLOW_CONTENTS}")
string(REGEX MATCHALL
  "timeout-minutes:"
  FSIM_HOSTED_TIMEOUTS
  "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_120_MINUTE_TIMEOUTS FSIM_120_MINUTE_TIMEOUT_COUNT)
list(LENGTH FSIM_HOSTED_TIMEOUTS FSIM_HOSTED_TIMEOUT_COUNT)
if(NOT FSIM_HOSTED_TIMEOUT_COUNT EQUAL 5
    OR NOT FSIM_120_MINUTE_TIMEOUT_COUNT EQUAL FSIM_HOSTED_TIMEOUT_COUNT)
  message(FATAL_ERROR
    "expected all five hosted job timeouts to be 120 minutes")
endif()

foreach(FSIM_FOOTPRINT_POLICY IN ITEMS
    "FSIM_LINK_POOL_SIZE"
    "\"8\""
    "fsim_link_pool=\${FSIM_LINK_POOL_SIZE}"
    "set(CMAKE_JOB_POOL_LINK fsim_link_pool)"
    "PROPERTY JOB_POOL_LINK fsim_link_pool"
    "FSIM_COMPACT_DEBUG_BUILD"
    "<CONFIG:Debug>:-Og"
    "<CONFIG:Debug>:-gz=zstd")
  string(FIND "${FSIM_FOOTPRINT_CONTENTS}" "${FSIM_FOOTPRINT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "debug/link footprint lost policy: ${FSIM_FOOTPRINT_POLICY}")
  endif()
endforeach()

foreach(FSIM_FST_VALUE_POLICY IN ITEMS
    "maximum_container_bytes"
    "maximum_hierarchy_bytes"
    "maximum_buffer_bytes"
    "maximum_name_bytes"
    "maximum_events"
    "maximum_timestamps"
    "maximum_type_metadata_bytes"
    "maximum_value_bytes"
    "fst_maximum_type_metadata_bytes"
    "fst_maximum_string_bytes"
    "fst_maximum_type_members"
    "fst_maximum_shape_dimensions"
    "FST change count exceeds its limit"
    "FST timestamp count exceeds its limit"
    "FST change payload exceeds its byte limit"
    "FST initial-value buffer exceeds its limit"
    "FST change buffer exceeds its limit"
    "FST writer is terminal after failure"
    "failed to publish FST output"
    "trace lifecycle ended without a clean close"
    "terminal_count == 1"
    "FST trace instance ancestry is invalid"
    "trace hierarchy maps distinct signals"
    "fsim-trace-provenance-v1"
    "trace observation fanout cannot be reentrant"
    "trace observation callback failed after accepting a record"
    "late trace snapshot would reorder an accepted record"
    "trace selection declaration owner is invalid"
    "FST values must have a nonzero width"
    "4'097U"
    "4'103U")
  string(FIND
    "${FSIM_FST_WRITER_CONTENTS}${FSIM_FST_WRITER_IMPLEMENTATION_CONTENTS}${FSIM_FST_WRITER_TEST_CONTENTS}${FSIM_FST_FORMAT_CONTENTS}${FSIM_FST_FORMAT_TEST_CONTENTS}${FSIM_FST_ENCODER_CONTENTS}${FSIM_FST_ENCODER_TEST_CONTENTS}${FSIM_FST_CHANGE_ENCODER_CONTENTS}${FSIM_FST_CHANGE_ENCODER_TEST_CONTENTS}${FSIM_FST_HIERARCHY_CONTENTS}${FSIM_FST_HIERARCHY_TEST_CONTENTS}${FSIM_FST_OBSERVATION_CONTENTS}${FSIM_FST_OBSERVATION_TEST_CONTENTS}${FSIM_FST_CONTROL_CONTENTS}${FSIM_FST_CONTROL_TEST_CONTENTS}${FSIM_FST_APPLICATION_CONTENTS}"
    "${FSIM_FST_VALUE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "FST arbitrary-width value resource policy lost token: ${FSIM_FST_VALUE_POLICY}")
  endif()
endforeach()

string(FIND
  "${FSIM_COVERAGE_CONTENTS}"
  "boost/multiprecision/cpp_int.hpp"
  FSIM_BOOST_CONSUMER_INDEX)
string(REGEX MATCHALL
  "PRIVATE[ \t\r\n]+fsim_boost_pfr_headers"
  FSIM_BOOST_INCLUDE_BINDINGS
  "${FSIM_ROOT_CONTENTS}")
string(REGEX MATCHALL
  "PRIVATE[ \t\r\n]+fsim_boost_pfr_headers"
  FSIM_FUZZ_BOOST_INCLUDE_BINDINGS
  "${FSIM_FUZZ_CMAKE_CONTENTS}")
list(LENGTH FSIM_BOOST_INCLUDE_BINDINGS FSIM_BOOST_INCLUDE_COUNT)
list(LENGTH FSIM_FUZZ_BOOST_INCLUDE_BINDINGS FSIM_FUZZ_BOOST_INCLUDE_COUNT)
if(FSIM_BOOST_CONSUMER_INDEX EQUAL -1
    OR NOT FSIM_BOOST_INCLUDE_COUNT EQUAL 5
    OR NOT FSIM_FUZZ_BOOST_INCLUDE_COUNT EQUAL 1)
  message(FATAL_ERROR
    "Boost consumers lost a pinned header target binding")
endif()
foreach(FSIM_STACK_POLICY IN ITEMS
    "function(fsim_configure_test_platform target)"
    "CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\""
    "target_link_options(\${target} PRIVATE /STACK:8388608)")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_STACK_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "Windows test stack lost policy: ${FSIM_STACK_POLICY}")
  endif()
endforeach()

foreach(FSIM_TIMEOUT_POLICY IN ITEMS
    "fsim.application.scoped_locals"
    "PROPERTIES TIMEOUT 60"
    "fsim.application.systemc_matrix"
    "PROPERTIES TIMEOUT 1200"
    "fsim.application.sv_containers"
    "PROPERTIES TIMEOUT 1200"
    "set_tests_properties(fsim.api PROPERTIES TIMEOUT 120)")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TIMEOUT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "test inventory lost timeout policy: ${FSIM_TIMEOUT_POLICY}")
  endif()
endforeach()

foreach(FSIM_PHASE IN ITEMS
    "building reference project"
    "building compiled project"
    "running interpreter"
    "running compiled engine"
    "building warm project"
    "running warm compiled engine"
    "complete")
  string(FIND "${FSIM_SCOPED_CONTENTS}" "${FSIM_PHASE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "scoped-local trace lost phase: ${FSIM_PHASE}")
  endif()
endforeach()
foreach(FSIM_PHASE IN ITEMS
    "systemc matrix: integration"
    "systemc matrix: scheduling"
    "systemc matrix: complete")
  string(FIND "${FSIM_SYSTEMC_CONTENTS}" "${FSIM_PHASE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC matrix trace lost phase: ${FSIM_PHASE}")
  endif()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "fsim.resource-portability-contract"
  FSIM_REGISTRATION_INDEX)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "resource portability contract CTest is not registered")
endif()

message(STATUS
  "resource portability contract: five four-worker, 120-minute hosted jobs, "
  "eight-link pool, compact Debug objects, 8 MiB Windows stacks, bounded "
  "large-test and FST value/change/hierarchy storage, pinned Boost headers, "
  "and scoped/SystemC phase traces are present")
