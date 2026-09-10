# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_retained_profile_qualification.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 15)
  message(FATAL_ERROR "v3 retained-profile ledger must contain 13 rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tlanguage\tcanonical\taliases\tselection_evidence\tbehavioral_evidence\tartifact_evidence\towner")
  message(FATAL_ERROR "v3 retained-profile ledger header changed")
endif()

set(FSIM_EXPECTED_IDS
  V3PROFILE-VHDL87 V3PROFILE-VHDL93 V3PROFILE-VHDL2000
  V3PROFILE-VHDL2002 V3PROFILE-VHDL2008 V3PROFILE-V1995
  V3PROFILE-V2001 V3PROFILE-V2001-NOCONFIG V3PROFILE-V2005
  V3PROFILE-SV2005 V3PROFILE-SV2009 V3PROFILE-SV2012 V3PROFILE-SV2017)
set(FSIM_IDS)
set(FSIM_VHDL_COUNT 0)
set(FSIM_VERILOG_COUNT 0)
set(FSIM_SYSTEMVERILOG_COUNT 0)
foreach(FSIM_INDEX RANGE 2 14)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 8)
    message(FATAL_ERROR "v3 retained-profile row must contain eight fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_LANGUAGE)
  list(GET FSIM_FIELDS 2 FSIM_CANONICAL)
  list(GET FSIM_FIELDS 3 FSIM_ALIASES)
  list(GET FSIM_FIELDS 7 FSIM_OWNER)
  if(FSIM_ID IN_LIST FSIM_IDS OR NOT FSIM_ID IN_LIST FSIM_EXPECTED_IDS OR
     NOT FSIM_LANGUAGE MATCHES "^(vhdl|verilog|systemverilog)$" OR
     FSIM_CANONICAL STREQUAL "" OR FSIM_ALIASES STREQUAL "" OR
     NOT FSIM_OWNER STREQUAL "B188-C04")
    message(FATAL_ERROR "v3 retained-profile row is malformed: ${FSIM_ID}")
  endif()
  foreach(FSIM_FIELD_INDEX RANGE 4 6)
    list(GET FSIM_FIELDS ${FSIM_FIELD_INDEX} FSIM_EVIDENCE_RELATIVE)
    if(IS_ABSOLUTE "${FSIM_EVIDENCE_RELATIVE}" OR
       FSIM_EVIDENCE_RELATIVE MATCHES "(^|/)\.\.(/|$)" OR
       FSIM_EVIDENCE_RELATIVE MATCHES "[\\:]" OR
       NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE_RELATIVE}")
      message(FATAL_ERROR
        "v3 retained-profile evidence is unsafe or missing: ${FSIM_ID}")
    endif()
  endforeach()
  string(TOUPPER "${FSIM_LANGUAGE}" FSIM_LANGUAGE_KEY)
  math(EXPR FSIM_${FSIM_LANGUAGE_KEY}_COUNT
    "${FSIM_${FSIM_LANGUAGE_KEY}_COUNT} + 1")
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()
foreach(FSIM_ID IN LISTS FSIM_EXPECTED_IDS)
  if(NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "v3 retained profile is missing: ${FSIM_ID}")
  endif()
endforeach()
if(NOT FSIM_VHDL_COUNT EQUAL 5 OR NOT FSIM_VERILOG_COUNT EQUAL 4 OR
   NOT FSIM_SYSTEMVERILOG_COUNT EQUAL 4)
  message(FATAL_ERROR
    "v3 retained-profile language counts changed: ${FSIM_VHDL_COUNT}/${FSIM_VERILOG_COUNT}/${FSIM_SYSTEMVERILOG_COUNT}")
endif()

function(fsim_require_profile_tokens FSIM_RELATIVE)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 retained-profile evidence changed in ${FSIM_RELATIVE}: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_profile_tokens(tests/project/project_config_test.cpp
  "std::tuple { \"87\", \"1987\", VhdlStandard::vhdl_1987 }"
  "std::tuple { \"08\", \"2008\", VhdlStandard::vhdl_2008 }"
  "std::tuple { \"95\", \"1995\", VerilogStandard::verilog_1995 }"
  "std::tuple { \"05\", \"2005\", VerilogStandard::verilog_2005 }"
  "SystemVerilogStandard::systemverilog_2005"
  "SystemVerilogStandard::systemverilog_2017")
fsim_require_profile_tokens(tests/app/typed_boundary_application_test.cpp
  "FSIM-VERILOG-2005-PASS"
  "FSIM-SYSTEMVERILOG-2017-PASS"
  "modes=v1995/v2001/v2001-noconfig/sv2005/sv2009/sv2012")
fsim_require_profile_tokens(tests/app/vhdl_logic9_application_test.cpp
  "FSIM-VHDL-OLDER-MODES-PASS"
  "revisions=1987/1993/2000/2002")
fsim_require_profile_tokens(tests/app/vhdl_ieee_integration_application_test.cpp
  "Expected { \"2008\", 19, 45, 32, 17 }"
  "FSIM-VHDL-OLDER-ENVIRONMENT-PASS")
fsim_require_profile_tokens(tests/app/application_test_artifact_verilog.cpp
  "FSIM-OLDER-STANDARD-ARTIFACT-MATRIX-PASS modes=6"
  "FSIM-VERILOG-2005-ARTIFACT-PASS"
  "FSIM-SYSTEMVERILOG-2017-ARTIFACT-PASS"
  "FSIM-VHDL-OLDER-MODES-ARTIFACT-PASS")

foreach(FSIM_CHECKER IN ITEMS
    CheckVhdlStandardModeInventory.cmake
    CheckVerilogSystemVerilogStandardModeInventory.cmake)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_CHECKER}"
    RESULT_VARIABLE FSIM_CHECK_RESULT
    OUTPUT_VARIABLE FSIM_CHECK_OUTPUT
    ERROR_VARIABLE FSIM_CHECK_ERROR
    TIMEOUT 120)
  if(NOT FSIM_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR
      "v3 retained-profile owner failed in ${FSIM_CHECKER}: ${FSIM_CHECK_ERROR}")
  endif()
endforeach()

fsim_require_profile_tokens(tests/CMakeLists.txt
  "NAME fsim.v3-retained-profile-qualification"
  "CheckV3RetainedProfiles.cmake"
  "NAME fsim.vhdl-standard-mode-closure-matrix"
  "NAME fsim.verilog-systemverilog-standard-mode-closure-matrix")
file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
set(FSIM_EXPECTED_DIGEST
  "7ec4c1c842a711bae73a8d979f817c2943a90daf0251a90dd7d80ed10911943b")
if(NOT FSIM_LEDGER_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "v3 retained-profile digest changed: expected=${FSIM_EXPECTED_DIGEST} actual=${FSIM_LEDGER_DIGEST}")
endif()
message(STATUS
  "v3 retained profiles passed: profiles=13 vhdl=5 verilog=4 systemverilog=4 digest=${FSIM_LEDGER_DIGEST}")
