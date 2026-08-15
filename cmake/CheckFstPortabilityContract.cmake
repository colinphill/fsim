# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_READER_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/fst_reader.hpp")
set(FSIM_READER_SOURCE
  "${FSIM_SOURCE_DIR}/src/runtime/fst_reader.cpp")
set(FSIM_READER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/fst_reader_tests.cpp")
set(FSIM_VCD_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vcd_control_application_test.cpp")
set(FSIM_PHASE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_non_project_cli.cpp")
set(FSIM_ARTIFACT_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/design_artifact_test.cpp")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")

foreach(FSIM_INPUT IN ITEMS
    "${FSIM_READER_HEADER}"
    "${FSIM_READER_SOURCE}"
    "${FSIM_READER_TEST}"
    "${FSIM_VCD_TEST}"
    "${FSIM_PHASE_TEST}"
    "${FSIM_ARTIFACT_TEST}"
    "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "FST portability input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_READER_HEADER}" FSIM_HEADER_CONTENTS)
file(READ "${FSIM_READER_SOURCE}" FSIM_SOURCE_CONTENTS)
file(READ "${FSIM_READER_TEST}" FSIM_TEST_CONTENTS)
file(READ "${FSIM_VCD_TEST}" FSIM_VCD_CONTENTS)
file(READ "${FSIM_PHASE_TEST}" FSIM_PHASE_CONTENTS)
file(READ "${FSIM_ARTIFACT_TEST}" FSIM_ARTIFACT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)

foreach(FSIM_POLICY IN ITEMS
    "std::span<const std::uint8_t>"
    "const std::filesystem::path& path"
    "maximum_container_bytes"
    "maximum_block_bytes"
    "maximum_hierarchy_bytes"
    "maximum_scopes"
    "maximum_declarations"
    "maximum_timestamps"
    "maximum_values"
    "maximum_decoded_value_bytes"
    "maximum_text_bytes"
    "maximum_metadata_bytes")
  string(FIND "${FSIM_HEADER_CONTENTS}" "${FSIM_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "FST reader header lost policy: ${FSIM_POLICY}")
  endif()
endforeach()

foreach(FSIM_POLICY IN ITEMS
    "std::ifstream input { path, std::ios::binary }"
    "constexpr std::size_t chunk_size"
    "std::bad_alloc"
    "read_fst_file("
    "kFstDeterministicContainerProfile"
    "kFstStoredContainerProfile"
    "maximum_decoded_value_bytes"
    "FSIM-FST-READ-001"
    "FSIM-FST-READ-002"
    "FSIM-FST-READ-003"
    "FSIM-FST-READ-004"
    "FSIM-FST-READ-005")
  string(FIND "${FSIM_SOURCE_CONTENTS}" "${FSIM_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "FST reader lost portability policy: ${FSIM_POLICY}")
  endif()
endforeach()

foreach(FSIM_FORBIDDEN IN ITEMS
    "#include <zlib.h>"
    "#include <fstapi.h>"
    "#include \"fstapi.h\""
    "mmap("
    "CreateFile"
    "fopen("
    "::open("
    "_open("
    "sizeof(long)"
    "sizeof(std::size_t)"
    "__BYTE_ORDER")
  string(FIND "${FSIM_SOURCE_CONTENTS}" "${FSIM_FORBIDDEN}" FSIM_INDEX)
  if(NOT FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "FST reader gained a platform or external dependency: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

foreach(FSIM_EVIDENCE IN ITEMS
    "test_container_header_and_truncation_rejections"
    "test_hierarchy_identity_and_compression_rejections"
    "test_value_time_and_compression_rejections"
    "test_resource_limits_and_file_input"
    "test_compression_semantic_equivalence"
    "read_fst_file(valid_path)"
    "missing.fst"
    "maximum_decoded_value_bytes")
  string(FIND "${FSIM_TEST_CONTENTS}" "${FSIM_EVIDENCE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "FST reader test lost evidence: ${FSIM_EVIDENCE}")
  endif()
endforeach()

string(FIND "${FSIM_VCD_CONTENTS}" "read_fst(" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "VCD/FST equivalence lost decoded FST evidence")
endif()
string(FIND "${FSIM_PHASE_CONTENTS}" "semantic_digest" FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "phase equivalence lost semantic digest evidence")
endif()
string(FIND "${FSIM_ARTIFACT_CONTENTS}" "decoded_fst.trace->semantic_digest"
  FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "artifact retention lost decoded FST evidence")
endif()
string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "fsim.fst-portability-contract"
  FSIM_INDEX)
if(FSIM_INDEX EQUAL -1)
  message(FATAL_ERROR "FST portability contract CTest is not registered")
endif()

message(STATUS
  "FST portability contract: bounded fixed-width decoding, binary filesystem "
  "I/O, transactional diagnostics, corruption/resource negatives, semantic "
  "differentials, and Linux/Windows dependency independence are present")
