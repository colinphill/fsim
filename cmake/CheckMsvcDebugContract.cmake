# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ROOT_CMAKE "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_APPLICATION_ANALYSIS
  "${FSIM_SOURCE_DIR}/src/app/application_analysis.cpp")
set(FSIM_APPLICATION_SEMANTIC
  "${FSIM_SOURCE_DIR}/src/app/application_semantic.cpp")
set(FSIM_SYSTEMC_CORE "${FSIM_SOURCE_DIR}/include/fsim/systemc/core.hpp")
set(FSIM_SYSTEMC_COMPILER_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/plugin_compiler_test.cpp")
set(FSIM_APPLICATION_SYSTEMC_SOURCE
  "${FSIM_SOURCE_DIR}/tests/app/application_test_sources_systemc.cpp")
set(FSIM_APPLICATION_SPECIALIZATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_specialization.cpp")
set(FSIM_APPLICATION_CLI_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_cli.cpp")
set(FSIM_TYPED_BOUNDARY_TEST
  "${FSIM_SOURCE_DIR}/tests/app/typed_boundary_application_test.cpp")
set(FSIM_RELEASE_AUDIT
  "${FSIM_SOURCE_DIR}/cmake/CheckV1ReleaseAudit.cmake")
set(FSIM_RELEASE_CANDIDATE
  "${FSIM_SOURCE_DIR}/cmake/CheckV1ReleaseCandidate.cmake")
set(FSIM_FRONTEND "${FSIM_SOURCE_DIR}/tests/frontend/frontend_sv_conformance_tests.cpp")
set(FSIM_ELABORATION "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_sv_conformance_test.cpp")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_ROOT_CMAKE}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_WORKFLOW}"
    "${FSIM_APPLICATION_ANALYSIS}"
    "${FSIM_APPLICATION_SEMANTIC}"
    "${FSIM_SYSTEMC_CORE}"
    "${FSIM_SYSTEMC_COMPILER_TEST}"
    "${FSIM_APPLICATION_SYSTEMC_SOURCE}"
    "${FSIM_APPLICATION_SPECIALIZATION_TEST}"
    "${FSIM_APPLICATION_CLI_TEST}"
    "${FSIM_TYPED_BOUNDARY_TEST}"
    "${FSIM_RELEASE_AUDIT}"
    "${FSIM_RELEASE_CANDIDATE}"
    "${FSIM_FRONTEND}"
    "${FSIM_ELABORATION}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "MSVC Debug contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ROOT_CMAKE}" FSIM_ROOT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_APPLICATION_ANALYSIS}" FSIM_APPLICATION_ANALYSIS_CONTENTS)
file(READ "${FSIM_APPLICATION_SEMANTIC}" FSIM_APPLICATION_SEMANTIC_CONTENTS)
file(READ "${FSIM_SYSTEMC_CORE}" FSIM_SYSTEMC_CORE_CONTENTS)
file(READ "${FSIM_SYSTEMC_COMPILER_TEST}" FSIM_SYSTEMC_COMPILER_TEST_CONTENTS)
file(READ
  "${FSIM_APPLICATION_SYSTEMC_SOURCE}"
  FSIM_APPLICATION_SYSTEMC_SOURCE_CONTENTS)
file(READ
  "${FSIM_APPLICATION_SPECIALIZATION_TEST}"
  FSIM_APPLICATION_SPECIALIZATION_TEST_CONTENTS)
file(READ "${FSIM_APPLICATION_CLI_TEST}" FSIM_APPLICATION_CLI_TEST_CONTENTS)
file(READ "${FSIM_TYPED_BOUNDARY_TEST}" FSIM_TYPED_BOUNDARY_TEST_CONTENTS)
file(READ "${FSIM_RELEASE_AUDIT}" FSIM_RELEASE_AUDIT_CONTENTS)
file(READ "${FSIM_RELEASE_CANDIDATE}" FSIM_RELEASE_CANDIDATE_CONTENTS)
file(READ "${FSIM_FRONTEND}" FSIM_FRONTEND_CONTENTS)
file(READ "${FSIM_ELABORATION}" FSIM_ELABORATION_CONTENTS)

foreach(FSIM_ROOT_POLICY IN ITEMS
    "function(fsim_configure_test_platform target)"
    "CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\""
    "target_link_options(\${target} PRIVATE /STACK:8388608)"
    "target_compile_options(fsim_application PRIVATE /bigobj)"
    "fsim_configure_test_platform(\${target})")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_ROOT_POLICY}" FSIM_POLICY_INDEX)
  if(FSIM_POLICY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "root build lost MSVC Debug test policy: ${FSIM_ROOT_POLICY}")
  endif()
endforeach()

string(FIND
  "${FSIM_APPLICATION_ANALYSIS_CONTENTS}"
  "static_cast<std::uint32_t>(sensitivity.edge)"
  FSIM_EDGE_CAST_INDEX)
if(FSIM_EDGE_CAST_INDEX EQUAL -1)
  message(FATAL_ERROR
    "SystemC sensitivity ABI metadata lost its explicit MSVC-safe width cast")
endif()

string(FIND
  "${FSIM_SYSTEMC_CORE_CONTENTS}"
  "} else {\n        return \"sc_interface\";"
  FSIM_INTERFACE_KIND_ELSE_INDEX)
if(FSIM_INTERFACE_KIND_ELSE_INDEX EQUAL -1)
  message(FATAL_ERROR
    "SystemC interface-kind fallback lost its MSVC-safe constexpr else")
endif()

string(FIND
  "${FSIM_SYSTEMC_COMPILER_TEST_CONTENTS}"
  "fsim::support::environment_variable(name_)"
  FSIM_COMPILER_ENVIRONMENT_INDEX)
string(FIND
  "${FSIM_SYSTEMC_COMPILER_TEST_CONTENTS}"
  "std::getenv("
  FSIM_COMPILER_GETENV_INDEX)
if(FSIM_COMPILER_ENVIRONMENT_INDEX EQUAL -1
    OR NOT FSIM_COMPILER_GETENV_INDEX EQUAL -1)
  message(FATAL_ERROR
    "SystemC compiler test bypasses the MSVC-safe environment helper")
endif()

foreach(FSIM_SEMANTIC_PATH_POLICY IN ITEMS
    "std::filesystem::weakly_canonical(path, error)"
    "normalized_source_name(fsim::support::path_to_utf8(path))")
  string(FIND
    "${FSIM_APPLICATION_SEMANTIC_CONTENTS}"
    "${FSIM_SEMANTIC_PATH_POLICY}"
    FSIM_SEMANTIC_PATH_INDEX)
  if(FSIM_SEMANTIC_PATH_INDEX EQUAL -1)
    message(FATAL_ERROR
      "semantic source identity lost Windows path canonicalization: "
      "${FSIM_SEMANTIC_PATH_POLICY}")
  endif()
endforeach()
string(FIND
  "${FSIM_APPLICATION_CLI_TEST_CONTENTS}"
  "shared_checked->semantics.source_files().size() == 2"
  FSIM_SHARED_SOURCE_COUNT_INDEX)
if(FSIM_SHARED_SOURCE_COUNT_INDEX EQUAL -1)
  message(FATAL_ERROR
    "shared compilation-unit test lost semantic source de-duplication proof")
endif()
foreach(FSIM_TYPED_BOUNDARY_PATH_POLICY IN ITEMS
    "#include \"path_test_support.hpp\""
    "fsim::test::same_source_path("
    "fsim::support::path_to_utf8(expected)")
  string(FIND
    "${FSIM_TYPED_BOUNDARY_TEST_CONTENTS}"
    "${FSIM_TYPED_BOUNDARY_PATH_POLICY}"
    FSIM_TYPED_BOUNDARY_PATH_INDEX)
  if(FSIM_TYPED_BOUNDARY_PATH_INDEX EQUAL -1)
    message(FATAL_ERROR
      "typed-boundary provenance lost portable path identity: "
      "${FSIM_TYPED_BOUNDARY_PATH_POLICY}")
  endif()
endforeach()

string(REGEX MATCHALL
  "output << R\"\\("
  FSIM_APPLICATION_SYSTEMC_SOURCE_CHUNKS
  "${FSIM_APPLICATION_SYSTEMC_SOURCE_CONTENTS}")
list(LENGTH
  FSIM_APPLICATION_SYSTEMC_SOURCE_CHUNKS
  FSIM_APPLICATION_SYSTEMC_SOURCE_CHUNK_COUNT)
if(FSIM_APPLICATION_SYSTEMC_SOURCE_CHUNK_COUNT LESS 6)
  message(FATAL_ERROR
    "generated SystemC application source lost its MSVC-safe literal chunks")
endif()

foreach(FSIM_SPECIALIZATION_PATH_POLICY IN ITEMS
    "#include \"path_test_support.hpp\""
    "same_source_path("
    "has_source_dependency(")
  string(FIND
    "${FSIM_APPLICATION_SPECIALIZATION_TEST_CONTENTS}"
    "${FSIM_SPECIALIZATION_PATH_POLICY}"
    FSIM_SPECIALIZATION_PATH_INDEX)
  if(FSIM_SPECIALIZATION_PATH_INDEX EQUAL -1)
    message(FATAL_ERROR
      "specialization test lost portable path comparison: "
      "${FSIM_SPECIALIZATION_PATH_POLICY}")
  endif()
endforeach()
string(FIND
  "${FSIM_APPLICATION_SPECIALIZATION_TEST_CONTENTS}"
  "vhdl_generic_entity_source.string()"
  FSIM_SPECIALIZATION_NATIVE_PATH_INDEX)
if(NOT FSIM_SPECIALIZATION_NATIVE_PATH_INDEX EQUAL -1)
  message(FATAL_ERROR
    "specialization test compares UTF-8 dependencies with a native spelling")
endif()

foreach(FSIM_RELEASE_CONTENTS IN ITEMS
    FSIM_RELEASE_AUDIT_CONTENTS
    FSIM_RELEASE_CANDIDATE_CONTENTS)
  string(FIND
    "${${FSIM_RELEASE_CONTENTS}}"
    "string(REPLACE \"\\r\\n\" \"\\n\" FSIM_MATRIX_CONTENTS"
    FSIM_MATRIX_NORMALIZATION_INDEX)
  string(FIND
    "${${FSIM_RELEASE_CONTENTS}}"
    "string(SHA256 FSIM_MATRIX_DIGEST \"\${FSIM_MATRIX_CONTENTS}\")"
    FSIM_MATRIX_HASH_INDEX)
  if(FSIM_MATRIX_NORMALIZATION_INDEX EQUAL -1
      OR FSIM_MATRIX_HASH_INDEX EQUAL -1)
    message(FATAL_ERROR
      "release matrix hashing lost CRLF-independent MSVC policy")
  endif()
endforeach()

foreach(FSIM_C_HOST IN ITEMS
    fsim_jit_runtime_c_tests
    fsim_systemc_abi_c_tests
    fsim_api_header_c_test)
  string(FIND
    "${FSIM_TEST_CONTENTS}"
    "fsim_configure_test_platform(${FSIM_C_HOST})"
    FSIM_C_HOST_INDEX)
  if(FSIM_C_HOST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "C test host lacks common MSVC stack policy: ${FSIM_C_HOST}")
  endif()
endforeach()

foreach(FSIM_TIMEOUT_POLICY IN ITEMS
    "fsim.application.scoped_locals"
    "PROPERTIES TIMEOUT 60"
    "fsim.application.systemc_matrix"
    "PROPERTIES TIMEOUT 1200"
    "fsim.application.sv_containers"
    "PROPERTIES TIMEOUT 1200"
    "PROPERTIES TIMEOUT 600")
  string(FIND
    "${FSIM_TEST_CONTENTS}" "${FSIM_TIMEOUT_POLICY}" FSIM_TIMEOUT_INDEX)
  if(FSIM_TIMEOUT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "test inventory lost MSVC timeout policy: ${FSIM_TIMEOUT_POLICY}")
  endif()
endforeach()

foreach(FSIM_JOB_POLICY IN ITEMS
    "windows-msvc:"
    "preset: ci-windows"
    "preset: ci-windows-release"
    "--parallel 4")
  string(FIND
    "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_JOB_POLICY}" FSIM_JOB_INDEX)
  if(FSIM_JOB_INDEX EQUAL -1)
    message(FATAL_ERROR
      "workflow lost MSVC Debug contract: ${FSIM_JOB_POLICY}")
  endif()
endforeach()

foreach(FSIM_SOURCE_POLICY IN ITEMS
    "UTF-8 BOM is transparent"
    "BOM and CRLF SystemVerilog input parses identically"
    "CRLF diagnostics retain exact Windows logical/physical path and line"
    "BOM and CRLF VHDL input parses identically")
  string(FIND
    "${FSIM_FRONTEND_CONTENTS}" "${FSIM_SOURCE_POLICY}" FSIM_SOURCE_INDEX)
  if(FSIM_SOURCE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "frontend lost MSVC source policy: ${FSIM_SOURCE_POLICY}")
  endif()
endforeach()
string(FIND
  "${FSIM_ELABORATION_CONTENTS}"
  "MSVC Debug source portability"
  FSIM_ELABORATION_INDEX)
if(FSIM_ELABORATION_INDEX EQUAL -1)
  message(FATAL_ERROR "elaboration lost MSVC Debug portability fixture")
endif()

message(STATUS
  "MSVC Debug contract: common 8 MiB stack policy covers C/C++ test hosts; "
  "SystemC enum metadata crosses the integer validation seam explicitly and "
  "interface-kind selection uses an explicit constexpr fallback; "
  "compiler tests use the shared MSVC-safe environment helper; "
  "generated SystemC source uses bounded literal chunks; "
  "scoped/container/application timeouts and BOM/CRLF span/elaboration "
  "fixtures are present")
