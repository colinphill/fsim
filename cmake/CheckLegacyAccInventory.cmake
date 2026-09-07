# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/legacy_acc_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_FEATURE_README
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/README.md")
set(FSIM_ACC_HEADER "${FSIM_SOURCE_DIR}/include/fsim/runtime/acc_user.h")
set(FSIM_ACC_CPP_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_user_abi_test.cpp")
set(FSIM_ACC_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_user_abi_c_test.c")
set(FSIM_ACC_LIFECYCLE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_lifecycle.cpp")
set(FSIM_ACC_LIFECYCLE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_lifecycle_test.cpp")
set(FSIM_ACC_HANDLE_BRIDGE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/acc_handle_bridge.h")
set(FSIM_ACC_HANDLE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_handle.cpp")
set(FSIM_ACC_HANDLE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_handle_test.cpp")
set(FSIM_ACC_INTERNAL
  "${FSIM_SOURCE_DIR}/src/runtime/acc_internal.hpp")
set(FSIM_ACC_LOOKUP_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_lookup.cpp")
set(FSIM_ACC_LOOKUP_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_lookup_test.cpp")
set(FSIM_ACC_TRAVERSAL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_traversal.cpp")
set(FSIM_ACC_TRAVERSAL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_traversal_test.cpp")
set(FSIM_ACC_OBJECT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_object.cpp")
set(FSIM_ACC_OBJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_object_test.cpp")
set(FSIM_ACC_READ_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_read.cpp")
set(FSIM_ACC_READ_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_read_test.cpp")
set(FSIM_ACC_WRITE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_write.cpp")
set(FSIM_ACC_WRITE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_write_test.cpp")
set(FSIM_ACC_ITERATOR_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_iterator.cpp")
set(FSIM_ACC_ITERATOR_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_iterator_test.cpp")
set(FSIM_ACC_TIMING_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_timing.cpp")
set(FSIM_ACC_TIMING_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_timing_test.cpp")
set(FSIM_ACC_VCL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_vcl.cpp")
set(FSIM_ACC_VCL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_vcl_test.cpp")
set(FSIM_ACC_CALLBACK_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_callback.cpp")
set(FSIM_ACC_CALLBACK_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_callback_test.cpp")
set(FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_handle_lifetime.cpp")
set(FSIM_ACC_HANDLE_LIFETIME_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_handle_lifetime_test.cpp")
set(FSIM_ACC_TF_COHERENCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_tf_coherence.cpp")
set(FSIM_ACC_TF_COHERENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_tf_coherence_test.cpp")
set(FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_vpi_coherence.cpp")
set(FSIM_ACC_VPI_COHERENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_vpi_coherence_test.cpp")
set(FSIM_VPI_OBJECT_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vpi_object.hpp")
set(FSIM_VPI_OBJECT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/vpi_object.cpp")
set(FSIM_ACC_SCHEDULER_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/app/application_acc_scheduler.hpp")
set(FSIM_ACC_SCHEDULER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_acc_scheduler.cpp")
set(FSIM_ACC_SCHEDULER_TEST
  "${FSIM_SOURCE_DIR}/tests/app/acc_scheduler_application_test.cpp")
set(FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_vendor_rejection.cpp")
set(FSIM_ACC_VENDOR_REJECTION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_vendor_rejection_test.cpp")
set(FSIM_ACC_C_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_link_probe_plugin.c")
set(FSIM_ACC_CPP_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_cpp_probe_plugin.cpp")
set(FSIM_ACC_CROSS_PLATFORM_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_cross_platform_plugins_test.cpp")
set(FSIM_RUNTIME_CMAKE
  "${FSIM_SOURCE_DIR}/tests/runtime/CMakeLists.txt")
set(FSIM_ROOT_CMAKE "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_INSTALLED_CONTRACT
  "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake")
set(FSIM_INSTALL_OWNERSHIP
  "${FSIM_SOURCE_DIR}/cmake/CheckBinaryInstallOwnership.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}" "${FSIM_PLAN}" "${FSIM_TEST_CMAKE}"
    "${FSIM_FEATURE_README}" "${FSIM_ACC_HEADER}" "${FSIM_ACC_CPP_TEST}"
    "${FSIM_ACC_C_TEST}" "${FSIM_ACC_LIFECYCLE_IMPLEMENTATION}"
    "${FSIM_ACC_LIFECYCLE_TEST}" "${FSIM_ACC_HANDLE_BRIDGE}"
    "${FSIM_ACC_HANDLE_IMPLEMENTATION}" "${FSIM_ACC_HANDLE_TEST}"
    "${FSIM_ACC_INTERNAL}" "${FSIM_ACC_LOOKUP_IMPLEMENTATION}"
    "${FSIM_ACC_LOOKUP_TEST}" "${FSIM_ACC_TRAVERSAL_IMPLEMENTATION}"
    "${FSIM_ACC_TRAVERSAL_TEST}" "${FSIM_ACC_OBJECT_IMPLEMENTATION}"
    "${FSIM_ACC_OBJECT_TEST}" "${FSIM_ACC_READ_IMPLEMENTATION}"
    "${FSIM_ACC_READ_TEST}" "${FSIM_ACC_WRITE_IMPLEMENTATION}"
    "${FSIM_ACC_WRITE_TEST}" "${FSIM_ACC_ITERATOR_IMPLEMENTATION}"
    "${FSIM_ACC_ITERATOR_TEST}" "${FSIM_ACC_TIMING_IMPLEMENTATION}"
    "${FSIM_ACC_TIMING_TEST}" "${FSIM_ACC_VCL_IMPLEMENTATION}"
    "${FSIM_ACC_VCL_TEST}" "${FSIM_ACC_CALLBACK_IMPLEMENTATION}"
    "${FSIM_ACC_CALLBACK_TEST}"
    "${FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION}"
    "${FSIM_ACC_HANDLE_LIFETIME_TEST}"
    "${FSIM_ACC_TF_COHERENCE_IMPLEMENTATION}"
    "${FSIM_ACC_TF_COHERENCE_TEST}"
    "${FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION}"
    "${FSIM_ACC_VPI_COHERENCE_TEST}"
    "${FSIM_VPI_OBJECT_HEADER}" "${FSIM_VPI_OBJECT_IMPLEMENTATION}"
    "${FSIM_ACC_SCHEDULER_MODEL}"
    "${FSIM_ACC_SCHEDULER_IMPLEMENTATION}"
    "${FSIM_ACC_SCHEDULER_TEST}"
    "${FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION}"
    "${FSIM_ACC_VENDOR_REJECTION_TEST}"
    "${FSIM_ACC_C_PLUGIN}" "${FSIM_ACC_CPP_PLUGIN}"
    "${FSIM_ACC_CROSS_PLATFORM_TEST}"
    "${FSIM_RUNTIME_CMAKE}" "${FSIM_ROOT_CMAKE}"
    "${FSIM_INSTALLED_CONTRACT}" "${FSIM_INSTALL_OWNERSHIP}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "legacy ACC inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_TEXT)
string(REPLACE "\r\n" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(REPLACE "\r" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(SHA256 FSIM_ACTUAL_DIGEST "${FSIM_INVENTORY_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "1fa0db768c78515e6596639dc3da43b323c1e7f8c2b209d668c02f35071bb1a7")
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "legacy ACC inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

string(TOLOWER "${FSIM_INVENTORY_TEXT}" FSIM_INVENTORY_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "/home/" "standards/" "file://" "modelsim" "questa" "vcs"
    "xcelium" "ncsim" "iverilog" "verilator" "riviera" "aldec"
    "compatibility-reader" "migration-reader")
  string(FIND "${FSIM_INVENTORY_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "legacy ACC inventory records private, absolute, vendor, or compatibility text")
  endif()
endforeach()

file(READ "${FSIM_PLAN}" FSIM_PLAN_TEXT)
string(REGEX MATCHALL "### Batch [0-9][0-9][0-9] -"
  FSIM_BATCH_HEADINGS "${FSIM_PLAN_TEXT}")
list(LENGTH FSIM_BATCH_HEADINGS FSIM_BATCH_COUNT)
if(NOT FSIM_BATCH_COUNT EQUAL 20)
  message(FATAL_ERROR
    "v3 plan must contain exactly twenty batch headings; got ${FSIM_BATCH_COUNT}")
endif()
foreach(FSIM_BATCH RANGE 178 197)
  set(FSIM_BATCH_TOKEN "### Batch ${FSIM_BATCH} -")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_BATCH_TOKEN}" FSIM_BATCH_OFFSET)
  if(FSIM_BATCH_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost Batch ${FSIM_BATCH}")
  endif()
endforeach()

string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 182 - IEEE ACC and complete legacy PLI closure"
  FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 183 - VHDL-2019 syntax, types, interfaces, and expressions"
  FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR FSIM_BATCH_END EQUAL -1 OR
   FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 182 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "\n${FSIM_CHANGE}. ")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}"
    FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 182 plan lost exact Change ${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1
    FSIM_BATCH_REMAINDER)
  string(FIND "${FSIM_BATCH_REMAINDER}" "${FSIM_CHANGE_TOKEN}"
    FSIM_DUPLICATE_OFFSET)
  if(NOT FSIM_DUPLICATE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 182 duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "Register the complete IEEE ACC routine and object inventory."
    "Provide standard-compatible"
    "acc_user.h"
    "Reject unsupported vendor names with stable diagnostics."
    "Run the full TF/ACC engine, artifact, cache, and platform corpus."
    "Run standard batch closure and declare legacy IEEE PLI complete."
    "Batch 180 and Batch 190 remain tenth-batch sanitizer and hosted-CI")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost required ACC token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.legacy-acc-inventory"
    "CheckLegacyAccInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "legacy ACC inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_FEATURE_README}" FSIM_FEATURE_README_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "Batch 182 starts with the IEEE-only 18-row"
    "legacy_acc_inventory.tsv"
    "102 standardized ACC routines"
    "115 canonical ACC object kinds"
    "ieee-only-no-vendor-extensions")
  string(FIND "${FSIM_FEATURE_README_TEXT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "feature-matrix documentation lost ACC token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_EXPECTED_HEADER
  "id\tstandard\thdl_profiles\tdomain\tstate\tchange\tobligation\troutine_inventory\tobject_inventory\timplementation_owner\tpositive_evidence\tnegative_evidence\tcoherence_evidence\tscheduler_evidence\tdiagnostic_owner\tresource_owner\textension_policy")
set(FSIM_EXPECTED_PROFILES
  "V1995,V2001,V2001NoConfig,V2005,SV2005,SV2009,SV2012,SV2017")
set(FSIM_EXPECTED_DOMAINS
  public-acc-header
  lifecycle-configuration-errors
  generation-qualified-handles
  name-lookup
  hierarchy-traversal
  complete-object-model
  value-and-property-reads
  value-updates
  indexed-iterators
  path-delay-timing-checks
  value-change-links
  callback-cancellation-order
  safe-point-validity
  tf-acc-coherence
  acc-vpi-object-equivalence
  parallel-coordination
  vendor-name-rejection
  legacy-pli-closure-corpus)
set(FSIM_COMPLETED_CHANGE 19)

function(fsim_require_acc_owner
    FSIM_ID FSIM_FIELD FSIM_PATH FSIM_PREFIX FSIM_REQUIRE_EXISTS)
  if(FSIM_PATH STREQUAL "" OR IS_ABSOLUTE "${FSIM_PATH}" OR
     NOT FSIM_PATH MATCHES "^${FSIM_PREFIX}")
    message(FATAL_ERROR
      "${FSIM_ID} misplaced ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
  string(FIND "${FSIM_PATH}" ".." FSIM_PARENT_OFFSET)
  string(FIND "${FSIM_PATH}" "\\" FSIM_BACKSLASH_OFFSET)
  if(NOT FSIM_PARENT_OFFSET EQUAL -1 OR
     NOT FSIM_BACKSLASH_OFFSET EQUAL -1)
    message(FATAL_ERROR "${FSIM_ID} has unsafe ${FSIM_FIELD} owner")
  endif()
  if(FSIM_REQUIRE_EXISTS AND NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
    message(FATAL_ERROR "${FSIM_ID} lost ${FSIM_FIELD} owner: ${FSIM_PATH}")
  endif()
endfunction()

string(REPLACE "\n" ";" FSIM_LINES "${FSIM_INVENTORY_TEXT}")
list(GET FSIM_LINES 0 FSIM_SPDX)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL FSIM_EXPECTED_HEADER)
  message(FATAL_ERROR "legacy ACC inventory SPDX policy or header changed")
endif()

set(FSIM_IDS)
set(FSIM_DOMAINS)
set(FSIM_CHANGES)
set(FSIM_ROUTINES)
set(FSIM_OBJECTS)
set(FSIM_ROW_COUNT 0)
set(FSIM_ACTIVE_COUNT 0)
set(FSIM_PRESERVED_COUNT 0)
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#" OR
     FSIM_LINE STREQUAL FSIM_EXPECTED_HEADER)
    continue()
  endif()

  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 17)
    message(FATAL_ERROR
      "legacy ACC row has ${FSIM_FIELD_COUNT} fields, expected 17")
  endif()

  math(EXPR FSIM_CHANGE "${FSIM_ROW_COUNT} + 2")
  if(FSIM_CHANGE LESS 10)
    set(FSIM_CHANGE_TEXT "0${FSIM_CHANGE}")
  else()
    set(FSIM_CHANGE_TEXT "${FSIM_CHANGE}")
  endif()
  list(GET FSIM_EXPECTED_DOMAINS ${FSIM_ROW_COUNT} FSIM_EXPECTED_DOMAIN)
  if(FSIM_CHANGE LESS_EQUAL FSIM_COMPLETED_CHANGE)
    set(FSIM_EXPECTED_STATE preserved)
    set(FSIM_REQUIRE_EXISTS TRUE)
  else()
    set(FSIM_EXPECTED_STATE active)
    set(FSIM_REQUIRE_EXISTS FALSE)
  endif()

  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_STANDARD)
  list(GET FSIM_FIELDS 2 FSIM_PROFILES)
  list(GET FSIM_FIELDS 3 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 4 FSIM_STATE)
  list(GET FSIM_FIELDS 5 FSIM_CHANGE_ID)
  list(GET FSIM_FIELDS 6 FSIM_OBLIGATION)
  list(GET FSIM_FIELDS 7 FSIM_ROUTINE_INVENTORY)
  list(GET FSIM_FIELDS 8 FSIM_OBJECT_INVENTORY)
  list(GET FSIM_FIELDS 9 FSIM_IMPLEMENTATION)
  list(GET FSIM_FIELDS 10 FSIM_POSITIVE)
  list(GET FSIM_FIELDS 11 FSIM_NEGATIVE)
  list(GET FSIM_FIELDS 12 FSIM_COHERENCE)
  list(GET FSIM_FIELDS 13 FSIM_SCHEDULER)
  list(GET FSIM_FIELDS 14 FSIM_DIAGNOSTIC)
  list(GET FSIM_FIELDS 15 FSIM_RESOURCE)
  list(GET FSIM_FIELDS 16 FSIM_EXTENSION_POLICY)

  if(NOT FSIM_ID STREQUAL "LEGACC-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_STANDARD STREQUAL "IEEE1364-2005" OR
     NOT FSIM_PROFILES STREQUAL FSIM_EXPECTED_PROFILES OR
     NOT FSIM_DOMAIN STREQUAL FSIM_EXPECTED_DOMAIN OR
     NOT FSIM_STATE STREQUAL FSIM_EXPECTED_STATE OR
     NOT FSIM_CHANGE_ID STREQUAL "B182-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_EXTENSION_POLICY STREQUAL
       "ieee-only-no-vendor-extensions")
    message(FATAL_ERROR
      "${FSIM_ID} has a drifted standard, profile, domain, state, change, or policy")
  endif()
  string(LENGTH "${FSIM_OBLIGATION}" FSIM_OBLIGATION_LENGTH)
  if(FSIM_OBLIGATION_LENGTH LESS 72 OR
     FSIM_OBLIGATION MATCHES "[\"'`]" OR
     FSIM_OBLIGATION MATCHES "[.;:]")
    message(FATAL_ERROR
      "${FSIM_ID} obligation is not independently bounded prose")
  endif()

  foreach(FSIM_OWNER IN ITEMS
      FSIM_IMPLEMENTATION FSIM_POSITIVE FSIM_NEGATIVE FSIM_COHERENCE
      FSIM_SCHEDULER)
    fsim_require_acc_owner(
      "${FSIM_ID}" "${FSIM_OWNER}" "${${FSIM_OWNER}}"
      "(include|src|tests)/" "${FSIM_REQUIRE_EXISTS}")
  endforeach()
  fsim_require_acc_owner(
    "${FSIM_ID}" diagnostic_owner "${FSIM_DIAGNOSTIC}" "docs/" TRUE)
  fsim_require_acc_owner(
    "${FSIM_ID}" resource_owner "${FSIM_RESOURCE}" "cmake/" TRUE)

  if(NOT FSIM_ROUTINE_INVENTORY STREQUAL "-")
    string(REPLACE "," ";" FSIM_ROW_ROUTINES "${FSIM_ROUTINE_INVENTORY}")
    foreach(FSIM_ROUTINE IN LISTS FSIM_ROW_ROUTINES)
      if(NOT FSIM_ROUTINE MATCHES "^acc_[a-z0-9_]+$")
        message(FATAL_ERROR
          "${FSIM_ID} has malformed ACC routine token: ${FSIM_ROUTINE}")
      endif()
      list(FIND FSIM_ROUTINES "${FSIM_ROUTINE}" FSIM_ROUTINE_INDEX)
      if(NOT FSIM_ROUTINE_INDEX EQUAL -1)
        message(FATAL_ERROR
          "${FSIM_ID} duplicates ACC routine token: ${FSIM_ROUTINE}")
      endif()
      list(APPEND FSIM_ROUTINES "${FSIM_ROUTINE}")
    endforeach()
  endif()
  if(NOT FSIM_OBJECT_INVENTORY STREQUAL "-")
    string(REPLACE "," ";" FSIM_ROW_OBJECTS "${FSIM_OBJECT_INVENTORY}")
    foreach(FSIM_OBJECT IN LISTS FSIM_ROW_OBJECTS)
      if(NOT FSIM_OBJECT MATCHES "^acc[A-Z][A-Za-z0-9]+$")
        message(FATAL_ERROR
          "${FSIM_ID} has malformed ACC object token: ${FSIM_OBJECT}")
      endif()
      list(FIND FSIM_OBJECTS "${FSIM_OBJECT}" FSIM_OBJECT_INDEX)
      if(NOT FSIM_OBJECT_INDEX EQUAL -1)
        message(FATAL_ERROR
          "${FSIM_ID} duplicates ACC object token: ${FSIM_OBJECT}")
      endif()
      list(APPEND FSIM_OBJECTS "${FSIM_OBJECT}")
    endforeach()
  endif()

  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_ID_INDEX)
  list(FIND FSIM_DOMAINS "${FSIM_DOMAIN}" FSIM_DOMAIN_INDEX)
  list(FIND FSIM_CHANGES "${FSIM_CHANGE_ID}" FSIM_CHANGE_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1 OR NOT FSIM_DOMAIN_INDEX EQUAL -1 OR
     NOT FSIM_CHANGE_INDEX EQUAL -1)
    message(FATAL_ERROR "legacy ACC row identity is duplicated: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")
  list(APPEND FSIM_CHANGES "${FSIM_CHANGE_ID}")
  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
  if(FSIM_STATE STREQUAL "active")
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  elseif(FSIM_STATE STREQUAL "preserved")
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  else()
    message(FATAL_ERROR "${FSIM_ID} has unsupported state ${FSIM_STATE}")
  endif()
endforeach()

list(LENGTH FSIM_ROUTINES FSIM_ROUTINE_COUNT)
list(LENGTH FSIM_OBJECTS FSIM_OBJECT_COUNT)
math(EXPR FSIM_EXPECTED_PRESERVED "${FSIM_COMPLETED_CHANGE} - 1")
math(EXPR FSIM_EXPECTED_ACTIVE "18 - ${FSIM_EXPECTED_PRESERVED}")
if(NOT FSIM_ROW_COUNT EQUAL 18 OR
   NOT FSIM_ACTIVE_COUNT EQUAL FSIM_EXPECTED_ACTIVE OR
   NOT FSIM_PRESERVED_COUNT EQUAL FSIM_EXPECTED_PRESERVED OR
   NOT FSIM_ROUTINE_COUNT EQUAL 102 OR NOT FSIM_OBJECT_COUNT EQUAL 115)
  message(FATAL_ERROR
    "legacy ACC inventory expected ${FSIM_EXPECTED_ACTIVE} active rows, ${FSIM_EXPECTED_PRESERVED} preserved rows, 102 routines, and 115 objects; got ${FSIM_ACTIVE_COUNT}, ${FSIM_PRESERVED_COUNT}, ${FSIM_ROUTINE_COUNT}, and ${FSIM_OBJECT_COUNT}")
endif()

foreach(FSIM_ROUTINE IN ITEMS
    acc_initialize acc_configure acc_handle_by_name acc_next_topmod
    acc_fetch_value acc_set_value acc_append_delays acc_vcl_add
    acc_vcl_delete acc_handle_tfinst acc_product_version)
  list(FIND FSIM_ROUTINES "${FSIM_ROUTINE}" FSIM_ROUTINE_INDEX)
  if(FSIM_ROUTINE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "legacy ACC inventory lost routine family representative: ${FSIM_ROUTINE}")
  endif()
endforeach()
foreach(FSIM_OBJECT IN ITEMS
    accModule accScope accNet accReg accPort accPrimitive accModPath
    accTchk accNamedEvent accIntegerVar accTask accFunction accConstant)
  list(FIND FSIM_OBJECTS "${FSIM_OBJECT}" FSIM_OBJECT_INDEX)
  if(FSIM_OBJECT_INDEX EQUAL -1)
    message(FATAL_ERROR
      "legacy ACC inventory lost object family representative: ${FSIM_OBJECT}")
  endif()
endforeach()

file(READ "${FSIM_ACC_HEADER}" FSIM_ACC_HEADER_TEXT)
file(READ "${FSIM_ACC_CPP_TEST}" FSIM_ACC_CPP_TEST_TEXT)
file(READ "${FSIM_ACC_C_TEST}" FSIM_ACC_C_TEST_TEXT)
file(READ "${FSIM_ACC_LIFECYCLE_IMPLEMENTATION}"
  FSIM_ACC_LIFECYCLE_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_LIFECYCLE_TEST}" FSIM_ACC_LIFECYCLE_TEST_TEXT)
file(READ "${FSIM_ACC_HANDLE_BRIDGE}" FSIM_ACC_HANDLE_BRIDGE_TEXT)
file(READ "${FSIM_ACC_HANDLE_IMPLEMENTATION}"
  FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_HANDLE_TEST}" FSIM_ACC_HANDLE_TEST_TEXT)
file(READ "${FSIM_ACC_INTERNAL}" FSIM_ACC_INTERNAL_TEXT)
file(READ "${FSIM_ACC_LOOKUP_IMPLEMENTATION}"
  FSIM_ACC_LOOKUP_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_LOOKUP_TEST}" FSIM_ACC_LOOKUP_TEST_TEXT)
file(READ "${FSIM_ACC_TRAVERSAL_IMPLEMENTATION}"
  FSIM_ACC_TRAVERSAL_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_TRAVERSAL_TEST}" FSIM_ACC_TRAVERSAL_TEST_TEXT)
file(READ "${FSIM_ACC_OBJECT_IMPLEMENTATION}"
  FSIM_ACC_OBJECT_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_OBJECT_TEST}" FSIM_ACC_OBJECT_TEST_TEXT)
file(READ "${FSIM_ACC_READ_IMPLEMENTATION}"
  FSIM_ACC_READ_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_READ_TEST}" FSIM_ACC_READ_TEST_TEXT)
file(READ "${FSIM_ACC_WRITE_IMPLEMENTATION}"
  FSIM_ACC_WRITE_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_WRITE_TEST}" FSIM_ACC_WRITE_TEST_TEXT)
file(READ "${FSIM_ACC_ITERATOR_IMPLEMENTATION}"
  FSIM_ACC_ITERATOR_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_ITERATOR_TEST}" FSIM_ACC_ITERATOR_TEST_TEXT)
file(READ "${FSIM_ACC_TIMING_IMPLEMENTATION}"
  FSIM_ACC_TIMING_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_TIMING_TEST}" FSIM_ACC_TIMING_TEST_TEXT)
file(READ "${FSIM_ACC_VCL_IMPLEMENTATION}"
  FSIM_ACC_VCL_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_VCL_TEST}" FSIM_ACC_VCL_TEST_TEXT)
file(READ "${FSIM_ACC_CALLBACK_IMPLEMENTATION}"
  FSIM_ACC_CALLBACK_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_CALLBACK_TEST}" FSIM_ACC_CALLBACK_TEST_TEXT)
file(READ "${FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION}"
  FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_HANDLE_LIFETIME_TEST}"
  FSIM_ACC_HANDLE_LIFETIME_TEST_TEXT)
file(READ "${FSIM_ACC_TF_COHERENCE_IMPLEMENTATION}"
  FSIM_ACC_TF_COHERENCE_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_TF_COHERENCE_TEST}"
  FSIM_ACC_TF_COHERENCE_TEST_TEXT)
file(READ "${FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION}"
  FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_VPI_COHERENCE_TEST}"
  FSIM_ACC_VPI_COHERENCE_TEST_TEXT)
file(READ "${FSIM_VPI_OBJECT_HEADER}" FSIM_VPI_OBJECT_HEADER_TEXT)
file(READ "${FSIM_VPI_OBJECT_IMPLEMENTATION}"
  FSIM_VPI_OBJECT_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_SCHEDULER_MODEL}" FSIM_ACC_SCHEDULER_MODEL_TEXT)
file(READ "${FSIM_ACC_SCHEDULER_IMPLEMENTATION}"
  FSIM_ACC_SCHEDULER_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_SCHEDULER_TEST}" FSIM_ACC_SCHEDULER_TEST_TEXT)
file(READ "${FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION}"
  FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION_TEXT)
file(READ "${FSIM_ACC_VENDOR_REJECTION_TEST}"
  FSIM_ACC_VENDOR_REJECTION_TEST_TEXT)
file(READ "${FSIM_ACC_C_PLUGIN}" FSIM_ACC_C_PLUGIN_TEXT)
file(READ "${FSIM_ACC_CPP_PLUGIN}" FSIM_ACC_CPP_PLUGIN_TEXT)
file(READ "${FSIM_ACC_CROSS_PLATFORM_TEST}"
  FSIM_ACC_CROSS_PLATFORM_TEST_TEXT)
file(READ "${FSIM_RUNTIME_CMAKE}" FSIM_RUNTIME_CMAKE_TEXT)
file(READ "${FSIM_ROOT_CMAKE}" FSIM_ROOT_CMAKE_TEXT)
file(READ "${FSIM_INSTALLED_CONTRACT}" FSIM_INSTALLED_CONTRACT_TEXT)
file(READ "${FSIM_INSTALL_OWNERSHIP}" FSIM_INSTALL_OWNERSHIP_TEXT)
set(FSIM_ACC_HEADER_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_CPP_TEST_TEXT}${FSIM_ACC_C_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}${FSIM_ROOT_CMAKE_TEXT}${FSIM_INSTALLED_CONTRACT_TEXT}${FSIM_INSTALL_OWNERSHIP_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "#ifndef ACC_USER_H"
    "typedef PLI_INT32* HANDLE"
    "typedef PLI_INT32* handle"
    "typedef struct t_acc_time"
    "typedef struct t_setval_delay"
    "typedef struct t_acc_vecval"
    "typedef struct t_setval_value"
    "typedef struct t_strengths"
    "typedef struct t_vc_record"
    "typedef struct t_location"
    "typedef struct t_timescale_info"
    "PLI_INT32 acc_error_flag"
    "acc_handle_calling_mod_m"
    "must not hide C++ keywords"
    "fsim.runtime.acc_user_abi"
    "include/fsim/runtime/acc_user.h"
    "runtime/acc_user.h")
  string(FIND "${FSIM_ACC_HEADER_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "public ACC header lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_ROUTINE IN LISTS FSIM_ROUTINES)
  string(FIND "${FSIM_ACC_HEADER_TEXT}" " ${FSIM_ROUTINE}"
    FSIM_ROUTINE_OFFSET)
  if(FSIM_ROUTINE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "public ACC header lost routine declaration: ${FSIM_ROUTINE}")
  endif()
endforeach()
foreach(FSIM_OBJECT IN LISTS FSIM_OBJECTS)
  string(FIND "${FSIM_ACC_HEADER_TEXT}" "#define ${FSIM_OBJECT} "
    FSIM_OBJECT_OFFSET)
  if(FSIM_OBJECT_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "public ACC header lost object identity: ${FSIM_OBJECT}")
  endif()
endforeach()
foreach(FSIM_OBJECT IN LISTS FSIM_OBJECTS)
  string(FIND "${FSIM_ACC_READ_IMPLEMENTATION_TEXT}"
    "FSIM_ACC_TYPE_NAME(${FSIM_OBJECT})" FSIM_OBJECT_OFFSET)
  if(FSIM_OBJECT_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC type-string table lost object identity: ${FSIM_OBJECT}")
  endif()
endforeach()
set(FSIM_ACC_LIFECYCLE_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_LIFECYCLE_IMPLEMENTATION_TEXT}${FSIM_ACC_LIFECYCLE_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaximumConfigurationValueSize = 4096"
    "kConfigurationItems"
    "supported_configuration_item"
    "bounded_configuration_value"
    "std::scoped_lock"
    "std::numeric_limits<std::uint64_t>::max()"
    "acc_error_flag = 0"
    "PLI_INT32 acc_initialize(void)"
    "void acc_close(void)"
    "PLI_INT32 acc_configure"
    "PLI_INT32 acc_product_type(void)"
    "PLI_BYTE8* acc_product_version(void)"
    "void acc_reset_buffer(void)"
    "PLI_BYTE8* acc_version(void)"
    "configuration requires an initialized ACC lifecycle"
    "configuration storage is bounded before publication"
    "concurrent lifecycle calls are serialized"
    "fsim.runtime.acc_lifecycle")
  string(FIND "${FSIM_ACC_LIFECYCLE_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC lifecycle lost transaction, error, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_HANDLE_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_HANDLE_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION 3u"
    "FSIM_ACC_HANDLE_MAX_OBJECTS"
    "simulation_identity"
    "hierarchy_generation"
    "fsim_acc_handle_context_enter_v3"
    "fsim_acc_handle_context_leave_v3"
    "fsim_acc_handle_from_vpi_v3"
    "fsim_acc_handle_to_vpi_v3"
    "std::unordered_map<handle, AccHandleRecord*>"
    "std::map<AccIdentity, handle>"
    "validate_tf_callback_pointer"
    "PLI_INT32 acc_compare_handles"
    "PLI_INT32 acc_object_of_type"
    "PLI_INT32 acc_object_in_typelist"
    "PLI_INT32 acc_release_object"
    "ACC mapping is stable for one live VPI object generation"
    "ACC rejects a live handle in a different simulation"
    "ACC observes stale or released underlying VPI generations"
    "mapping rejects a second valid VPI object at the context limit"
    "fsim.runtime.acc_handle")
  string(FIND "${FSIM_ACC_HANDLE_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC handle bridge lost generation, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_LOOKUP_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_INTERNAL_TEXT}${FSIM_ACC_LOOKUP_IMPLEMENTATION_TEXT}${FSIM_ACC_LOOKUP_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_LOOKUP_ABSOLUTE"
    "FSIM_ACC_LOOKUP_RELATIVE"
    "FSIM_ACC_LOOKUP_PLI_SCOPE"
    "FSIM_ACC_RELATION_PARENT"
    "FSIM_ACC_RELATION_SCOPE"
    "FSIM_ACC_RELATION_SIMULATED_NET"
    "FSIM_ACC_NAME_MAXIMUM_BYTES 4096u"
    "configuration_enabled"
    "acc_handle_by_name"
    "acc_handle_object"
    "acc_handle_parent"
    "acc_handle_scope"
    "acc_handle_simulated_net"
    "acc_handle_interactive_scope"
    "acc_set_interactive_scope"
    "acc_set_scope"
    "absolute and relative lookup retain exact branch ownership"
    "enabled optional set-scope names select an absolute module"
    "unreadable lookup names fail before callback dispatch"
    "fsim.runtime.acc_lookup")
  string(FIND "${FSIM_ACC_LOOKUP_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC lookup lost name, scope, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_TRAVERSAL_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_TRAVERSAL_IMPLEMENTATION_TEXT}${FSIM_ACC_TRAVERSAL_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_TRAVERSE_BEGIN"
    "FSIM_ACC_TRAVERSE_NEXT"
    "FSIM_ACC_TRAVERSE_END"
    "FSIM_ACC_COLLECTION_MAXIMUM_OBJECTS"
    "acc_next_topmod"
    "acc_next_scope"
    "acc_next_child"
    "acc_collect"
    "acc_count"
    "acc_free"
    "filtered next traversal preserves canonical child creation order"
    "next traversal can recover position from a valid prior object"
    "completed and failed traversals release every native cursor"
    "fsim.runtime.acc_traversal")
  string(FIND "${FSIM_ACC_TRAVERSAL_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC traversal lost order, ownership, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_OBJECT_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_OBJECT_IMPLEMENTATION_TEXT}${FSIM_ACC_OBJECT_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_OBJECT_QUERY_ABI_VERSION 3u"
    "fsim_acc_object_query_v3"
    "fsim_acc_vpi_object_query_v3_fn"
    "FSIM_ACC_OBJECT_CONDITION"
    "FSIM_ACC_OBJECT_TERMINAL_INDEX"
    "type_matches"
    "acc_handle_condition"
    "acc_handle_modpath"
    "acc_handle_path"
    "acc_handle_port"
    "acc_handle_tchk"
    "acc_handle_terminal"
    "every complete object-model subtype preserves exact identity"
    "full types preserve standardized generic object membership"
    "invalid owners indices names and edges fail before callback dispatch"
    "fsim.runtime.acc_object")
  string(FIND "${FSIM_ACC_OBJECT_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC object model lost subtype, connectivity, timing, or containment token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_READ_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_READ_IMPLEMENTATION_TEXT}${FSIM_ACC_READ_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_READ_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_READ_MAXIMUM_BITS"
    "FSIM_ACC_READ_MAXIMUM_STRING_BYTES"
    "fsim_acc_logic_word_v3"
    "fsim_acc_read_query_v3"
    "fsim_acc_read_result_v3"
    "fsim_acc_vpi_read_v3_fn"
    "thread_local std::string string_buffer"
    "acc_fetch_attribute"
    "acc_fetch_location"
    "acc_fetch_range"
    "acc_fetch_timescale_info"
    "acc_fetch_type_str"
    "acc_fetch_value"
    "wide four-state values preserve every aval and bval word"
    "structured string-copy failures retain the ACC error state"
    "resolver exceptions are contained at the ACC boundary"
    "fsim.runtime.acc_read")
  string(FIND "${FSIM_ACC_READ_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC read bridge lost value, property, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_WRITE_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_WRITE_IMPLEMENTATION_TEXT}${FSIM_ACC_WRITE_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_WRITE_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_WRITE_CAP_DEPOSIT"
    "FSIM_ACC_WRITE_CAP_DEASSIGN"
    "fsim_acc_write_value_v3"
    "fsim_acc_write_query_v3"
    "fsim_acc_write_result_v3"
    "fsim_acc_vpi_write_v3_fn"
    "PLI_INT32 acc_set_value"
    "no-delay vector deposit copies every four-state word atomically"
    "every standardized string input format is copied exactly"
    "release returns the post-release vector in the same transaction"
    "procedural assign rejects net targets before dispatch"
    "write callback exceptions are contained at the ACC boundary"
    "fsim.runtime.acc_write")
  string(FIND "${FSIM_ACC_WRITE_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC write bridge lost transaction, target, delay, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_ITERATOR_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_INTERNAL_TEXT}${FSIM_ACC_ITERATOR_IMPLEMENTATION_TEXT}${FSIM_ACC_ITERATOR_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_ITERATOR_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_ITERATOR_MAXIMUM_TYPES 256u"
    "fsim_acc_iterator_query_v3"
    "fsim_acc_iterator_result_v3"
    "fsim_acc_vpi_iterate_v3_fn"
    "std::map<SessionKey, IteratorSession>"
    "acc_next_bit"
    "acc_next_cell_load"
    "acc_next_driver"
    "acc_next_hiconn"
    "acc_next_input"
    "acc_next_load"
    "acc_next_loconn"
    "acc_next_modpath"
    "acc_next_output"
    "acc_next_portout"
    "acc_next_tchk"
    "acc_next_terminal"
    "same result identity can own independent relation-family cursors"
    "iterator recovers position from a valid prior relation object"
    "completed and failed indexed iterators release every native cursor"
    "fsim.runtime.acc_iterator")
  string(FIND "${FSIM_ACC_ITERATOR_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC indexed iterator lost relation, order, ownership, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_TIMING_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_INTERNAL_TEXT}${FSIM_ACC_LIFECYCLE_IMPLEMENTATION_TEXT}${FSIM_ACC_TIMING_IMPLEMENTATION_TEXT}${FSIM_ACC_TIMING_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_TIMING_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_TIMING_MAXIMUM_DELAYS 12u"
    "FSIM_ACC_TIMING_CAP_DELAYS"
    "fsim_acc_timing_query_v3"
    "fsim_acc_timing_result_v3"
    "fsim_acc_vpi_timing_v3_fn"
    "configuration_value"
    "acc_append_delays"
    "acc_append_pulsere"
    "acc_fetch_delay_mode"
    "acc_fetch_delays"
    "acc_fetch_polarity"
    "acc_fetch_pulsere"
    "acc_replace_delays"
    "acc_replace_pulsere"
    "acc_set_pulsere"
    "single delay values publish after validation"
    "minimum typical maximum delays use one bounded array"
    "pulse reject and error pairs publish atomically"
    "malformed results cannot partially publish"
    "callback exceptions are contained"
    "fsim.runtime.acc_timing")
  string(FIND "${FSIM_ACC_TIMING_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC timing bridge lost delay, pulse, configuration, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_VCL_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_VCL_IMPLEMENTATION_TEXT}${FSIM_ACC_VCL_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_VCL_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_VCL_MAXIMUM_LINKS"
    "fsim_acc_vcl_query_v3"
    "fsim_acc_vcl_event_v3"
    "fsim_acc_vpi_vcl_v3_fn"
    "fsim_acc_vcl_dispatch_v3"
    "void acc_vcl_add"
    "logic callback retains time value and user data"
    "strength callback retains logic and both strengths"
    "vector callback retains the exact generation-qualified handle"
    "real callback retains its value"
    "unsupported objects fail before registration dispatch"
    "consumer exceptions are contained at the ACC boundary"
    "fsim.runtime.acc_vcl")
  string(FIND "${FSIM_ACC_VCL_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC value-change registration lost value, time, callback, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_CALLBACK_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_INTERNAL_TEXT}${FSIM_ACC_VCL_IMPLEMENTATION_TEXT}${FSIM_ACC_CALLBACK_IMPLEMENTATION_TEXT}${FSIM_ACC_CALLBACK_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_VCL_UNREGISTER"
    "sequence_identity"
    "std::condition_variable link_condition"
    "thread_local std::uint64_t dispatching_link"
    "cancel_vcl_link"
    "void acc_vcl_delete"
    "duplicate callback tuples reject before simulator dispatch"
    "out-of-order callback sequence is rejected"
    "cancellation blocks simulator re-entry before unregister returns"
    "removed callbacks cannot publish late observations"
    "same-link consumer re-entry is contained"
    "self-cancellation returns without deadlock"
    "fsim.runtime.acc_callback")
  string(FIND "${FSIM_ACC_CALLBACK_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC callback cancellation lost order, re-entry, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_HANDLE_LIFETIME_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_INTERNAL_TEXT}${FSIM_ACC_ITERATOR_IMPLEMENTATION_TEXT}${FSIM_ACC_READ_IMPLEMENTATION_TEXT}${FSIM_ACC_WRITE_IMPLEMENTATION_TEXT}${FSIM_ACC_VCL_IMPLEMENTATION_TEXT}${FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION_TEXT}${FSIM_ACC_HANDLE_LIFETIME_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_SAFE_POINT_ABI_VERSION 3u"
    "fsim_acc_safe_point_v3"
    "fsim_acc_safe_point_advance_v3"
    "invalidate_iterator_safe_point"
    "invalidated_iterator_sessions"
    "reset_read_borrowed_storage"
    "reset_write_borrowed_storage"
    "safe-point advance closes retained iterator cursors"
    "generation-qualified handles survive safe-point advance"
    "callback links survive safe-point advance"
    "an iterator cannot resume across its safe-point boundary"
    "borrowed storage can be reacquired after a safe point"
    "old handles reject a new hierarchy generation"
    "old callback links reject a new hierarchy generation"
    "fsim.runtime.acc_handle_lifetime")
  string(FIND "${FSIM_ACC_HANDLE_LIFETIME_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC safe-point lifetime lost handle, iterator, callback, storage, or generation token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_TF_COHERENCE_CONTRACT
  "${FSIM_ACC_HEADER_TEXT}${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_HANDLE_IMPLEMENTATION_TEXT}${FSIM_ACC_INTERNAL_TEXT}${FSIM_ACC_TF_COHERENCE_IMPLEMENTATION_TEXT}${FSIM_ACC_TF_COHERENCE_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_TF_CONTEXT_ABI_VERSION 3u"
    "fsim_acc_tf_context_v3"
    "fsim_tf_current_call_context_v3"
    "valid_tf_context_binding"
    "PLI_INT32 acc_fetch_argc"
    "double acc_fetch_tfarg"
    "double acc_fetch_itfarg"
    "handle acc_handle_tfarg"
    "handle acc_handle_tfinst"
    "ACC and TF share argument values, instance scope, and work area"
    "shared call state cannot escape the callback lifetime"
    "fsim.runtime.acc_tf_coherence")
  string(FIND "${FSIM_ACC_TF_COHERENCE_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC TF coherence lost shared value, scope, work-area, or lifetime token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_VPI_COHERENCE_CONTRACT
  "${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION_TEXT}${FSIM_ACC_VPI_COHERENCE_TEST_TEXT}${FSIM_VPI_OBJECT_HEADER_TEXT}${FSIM_VPI_OBJECT_IMPLEMENTATION_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "fsim_acc_vpi_same_object_v3"
    "SystemVerilogVpiObjectKind::Constant"
    "SystemVerilogVpiObjectKind::Concatenation"
    "SystemVerilogVpiObjectKind::Operation"
    "SystemVerilogVpiObjectKind::MinTypMax"
    "ACC value reads the same storage as the VPI registry"
    "ACC hierarchy resolves the same VPI parent and full name"
    "ACC connectivity returns the exact VPI expression identity"
    "ACC timing reads the same VPI-keyed path record"
    "ACC and VPI reject the same released generation"
    "fsim.runtime.acc_vpi_coherence")
  string(FIND "${FSIM_ACC_VPI_COHERENCE_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC/VPI coherence lost shared identity, value, hierarchy, connectivity, timing, or generation token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_SCHEDULER_CONTRACT
  "${FSIM_ACC_SCHEDULER_MODEL_TEXT}${FSIM_ACC_SCHEDULER_IMPLEMENTATION_TEXT}${FSIM_ACC_SCHEDULER_TEST_TEXT}${FSIM_TEST_CMAKE_TEXT}${FSIM_ROOT_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaximumAccSchedulerRequests"
    "AccSchedulerOperationKind"
    "std::recursive_mutex"
    "std::map<AccSchedulerSequence"
    "AccSchedulerError::OutOfOrderEpoch"
    "impl_->scheduler->running()"
    "impl_->scheduler->current_phase()"
    "parallel workers stage every ACC operation family"
    "publication retains the exact scheduler boundary"
    "worker completion order cannot change ACC execution order"
    "a later epoch cannot strand earlier staged ACC operations"
    "foreign exceptions and callback re-entry are contained deterministically"
    "fsim.application.acc-scheduler")
  string(FIND "${FSIM_ACC_SCHEDULER_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC parallel coordinator lost ordering, scheduler, containment, or resource token: ${FSIM_TOKEN}")
  endif()
endforeach()
set(FSIM_ACC_VENDOR_REJECTION_CONTRACT
  "${FSIM_ACC_HANDLE_BRIDGE_TEXT}${FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION_TEXT}${FSIM_ACC_VENDOR_REJECTION_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_ACC_STANDARD_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_STANDARD_NAME_MAXIMUM_BYTES 128u"
    "kStandardRoutines.size() == 102U"
    "std::ranges::is_sorted(kStandardRoutines)"
    "FSIM-ACC-NAME-001"
    "FSIM-ACC-NAME-002"
    "FSIM-ACC-NAME-003"
    "FSIM-ACC-NAME-004"
    "FSIM-ACC-NAME-005"
    "FSIM-ACC-NAME-006"
    "validate_tf_callback_pointer(dispatch)"
    "an unsupported vendor routine cannot enter fallback dispatch"
    "every canonical ACC object constant is accepted"
    "an unsupported behavior selector cannot enter fallback dispatch"
    "a v2 query is rejected before name inspection or dispatch"
    "fsim.runtime.acc_vendor_rejection")
  string(FIND "${FSIM_ACC_VENDOR_REJECTION_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC vendor rejection lost inventory, diagnostic, dispatch, or v2-rejection token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_ROUTINE IN LISTS FSIM_ROUTINES)
  string(FIND "${FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION_TEXT}"
    "\"${FSIM_ROUTINE}\"" FSIM_ROUTINE_OFFSET)
  if(FSIM_ROUTINE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC vendor rejection lost standardized routine: ${FSIM_ROUTINE}")
  endif()
endforeach()
foreach(FSIM_OBJECT IN LISTS FSIM_OBJECTS)
  string(FIND "${FSIM_ACC_VENDOR_REJECTION_TEST_TEXT}"
    "${FSIM_OBJECT}" FSIM_OBJECT_OFFSET)
  if(FSIM_OBJECT_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC vendor rejection lost canonical object witness: ${FSIM_OBJECT}")
  endif()
endforeach()
set(FSIM_ACC_CROSS_PLATFORM_CONTRACT
  "${FSIM_ACC_C_PLUGIN_TEXT}${FSIM_ACC_CPP_PLUGIN_TEXT}${FSIM_ACC_CROSS_PLATFORM_TEST_TEXT}${FSIM_RUNTIME_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "fsim_acc_link_probe"
    "fsim_acc_cpp_probe"
    "standard_acc_symbols.size() == 102U"
    "acc_error_flag"
    "acc_vendor_fast_handle"
    "DynamicLibrary::is_loaded"
    "cached ACC plug-in artifacts did not load"
    "FSIM_ACC_LINK_LIBRARY_PATH"
    "FSIM_ACC_C_PLUGIN_PATH"
    "FSIM_ACC_CPP_PLUGIN_PATH"
    "fsim.runtime.acc_cross_platform_plugins"
    "linux;windows;engine;artifact;cache")
  string(FIND "${FSIM_ACC_CROSS_PLATFORM_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC closure corpus lost link, platform, artifact, cache, or execution token: ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_ROUTINE IN LISTS FSIM_ROUTINES)
  string(FIND "${FSIM_ACC_CROSS_PLATFORM_TEST_TEXT}"
    "\"${FSIM_ROUTINE}\"" FSIM_ROUTINE_OFFSET)
  if(FSIM_ROUTINE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ACC closure corpus lost exported routine witness: ${FSIM_ROUTINE}")
  endif()
endforeach()
string(TOLOWER "${FSIM_ACC_HEADER_TEXT}" FSIM_ACC_HEADER_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "modelsim" "questa" "vcs" "xcelium" "ncsim" "iverilog"
    "verilator" "riviera" "aldec" "descriptor_v2")
  string(FIND "${FSIM_ACC_HEADER_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "public ACC header admitted vendor or v2 surface: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

message(STATUS
  "legacy ACC inventory passed: rows=${FSIM_ROW_COUNT} active=${FSIM_ACTIVE_COUNT} preserved=${FSIM_PRESERVED_COUNT} routines=${FSIM_ROUTINE_COUNT} objects=${FSIM_OBJECT_COUNT} digest=${FSIM_ACTUAL_DIGEST}")
