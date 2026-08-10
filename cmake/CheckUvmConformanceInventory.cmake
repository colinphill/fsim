# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/uvm_conformance_inventory.tsv")
set(FSIM_HEADER
  "${FSIM_SOURCE_DIR}/tests/app/uvm_conformance_inventory.hpp")
set(FSIM_SOURCE
  "${FSIM_SOURCE_DIR}/tests/app/uvm_conformance_inventory.cpp")
set(FSIM_APPLICATION
  "${FSIM_SOURCE_DIR}/tests/app/uvm_phase_tlm_application_test.cpp")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_RUNNER "${FSIM_SOURCE_DIR}/cmake/RunUvmPhaseTlmExample.cmake")
foreach(FSIM_INPUT IN ITEMS "${FSIM_INVENTORY}" "${FSIM_HEADER}"
    "${FSIM_SOURCE}" "${FSIM_APPLICATION}" "${FSIM_TEST_CMAKE}"
    "${FSIM_RUNNER}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "UVM conformance-inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 20)
  message(FATAL_ERROR
    "UVM conformance inventory must contain SPDX, one header, and 18 rows; got ${FSIM_ROW_COUNT}")
endif()
list(GET FSIM_ROWS 0 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "UVM conformance inventory lost its SPDX policy")
endif()
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_HEADER_ROW STREQUAL
    "family\trelease\tboundary\tgoverned_classes\tproject_classes\tpositive_evidence\tnegative_evidence\texecution_evidence")
  message(FATAL_ERROR "UVM conformance inventory header changed")
endif()

set(FSIM_BOTH_ROWS 0)
set(FSIM_UVM_1_2_ROWS 0)
set(FSIM_UVM_2020_ROWS 0)
set(FSIM_FAMILIES)
foreach(FSIM_INDEX RANGE 2 19)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 8)
    message(FATAL_ERROR "UVM conformance row ${FSIM_INDEX} does not have eight fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_FAMILY)
  list(GET FSIM_FIELDS 1 FSIM_RELEASE)
  list(GET FSIM_FIELDS 2 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 5 FSIM_POSITIVE)
  list(GET FSIM_FIELDS 6 FSIM_NEGATIVE)
  list(GET FSIM_FIELDS 7 FSIM_EXECUTION)
  list(FIND FSIM_FAMILIES "${FSIM_FAMILY}" FSIM_FAMILY_INDEX)
  if(NOT FSIM_FAMILY_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate UVM conformance family: ${FSIM_FAMILY}")
  endif()
  list(APPEND FSIM_FAMILIES "${FSIM_FAMILY}")
  if(NOT FSIM_BOUNDARY STREQUAL "supported")
    message(FATAL_ERROR "UVM conformance family is not supported: ${FSIM_FAMILY}")
  endif()
  if(FSIM_RELEASE STREQUAL "both")
    math(EXPR FSIM_BOTH_ROWS "${FSIM_BOTH_ROWS} + 1")
  elseif(FSIM_RELEASE STREQUAL "uvm-1.2")
    math(EXPR FSIM_UVM_1_2_ROWS "${FSIM_UVM_1_2_ROWS} + 1")
  elseif(FSIM_RELEASE STREQUAL "uvm-2020.3.1")
    math(EXPR FSIM_UVM_2020_ROWS "${FSIM_UVM_2020_ROWS} + 1")
  else()
    message(FATAL_ERROR "unknown UVM conformance release: ${FSIM_RELEASE}")
  endif()
  foreach(FSIM_EVIDENCE IN ITEMS
      "${FSIM_POSITIVE}" "${FSIM_NEGATIVE}" "${FSIM_EXECUTION}")
    if(NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
      message(FATAL_ERROR
        "UVM conformance family ${FSIM_FAMILY} lost evidence: ${FSIM_EVIDENCE}")
    endif()
  endforeach()
endforeach()
if(NOT FSIM_BOTH_ROWS EQUAL 16 OR NOT FSIM_UVM_1_2_ROWS EQUAL 1 OR
   NOT FSIM_UVM_2020_ROWS EQUAL 1)
  message(FATAL_ERROR
    "UVM conformance inventory must select 17 exact families per release")
endif()

file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_CONTENTS)
string(TOLOWER "${FSIM_INVENTORY_CONTENTS}" FSIM_INVENTORY_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS "xfail" "expected-fail" "waiver" "allowlist" "suppress")
  string(FIND "${FSIM_INVENTORY_LOWER}" "${FSIM_FORBIDDEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "UVM supported inventory contains forbidden failure escape: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE}" FSIM_SOURCE_CONTENTS)
file(READ "${FSIM_APPLICATION}" FSIM_APPLICATION_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_RUNNER}" FSIM_RUNNER_CONTENTS)
foreach(FSIM_TOKEN IN ITEMS
    "summary.families != 17"
    "summary.project_classes != 27"
    "release == \"uvm-1.2\" ? 53u : 56u"
    "require_class(project, family, class_name)")
  string(FIND "${FSIM_SOURCE_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM runtime inventory lost contract: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "require_uvm_conformance_inventory(project, release)"
    "conformance_inventory=standard/project families=17"
    "supported_gaps=0 suppression=none")
  string(FIND "${FSIM_APPLICATION_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM application lost inventory contract: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "app/uvm_conformance_inventory.cpp"
    "fsim.uvm-conformance-inventory")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM CTest lost inventory contract: ${FSIM_TOKEN}")
  endif()
endforeach()
string(FIND "${FSIM_RUNNER_CONTENTS}"
  "conformance_inventory=standard/project families=17" FSIM_RUNNER_INDEX)
if(FSIM_RUNNER_INDEX EQUAL -1)
  message(FATAL_ERROR "UVM runner lost conformance-inventory transcript")
endif()

message(STATUS
  "UVM conformance inventory: 18 rows, 17 exact families per release, 53/56 "
  "governed classes, 27 project classes, complete evidence ownership, and no "
  "supported-failure escape are present")
