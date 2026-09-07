# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/legacy_tf_inventory.tsv")
set(FSIM_PLAN "${FSIM_SOURCE_DIR}/docs/implementation_plan_v3.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_FEATURE_README
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/README.md")
set(FSIM_NATIVE_PLUGIN_ABI
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/native_plugin_abi.h")
set(FSIM_NATIVE_PLUGIN_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/native_plugin.hpp")
set(FSIM_NATIVE_PLUGIN_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/native_plugin.cpp")
set(FSIM_NATIVE_PLUGIN_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/native_plugin_abi_test.cpp")
set(FSIM_NATIVE_PLUGIN_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/native_plugin_abi_c_test.c")
set(FSIM_VERIUSER_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/veriuser.h")
set(FSIM_VERIUSER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/veriuser_abi_test.cpp")
set(FSIM_VERIUSER_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/veriuser_abi_c_test.c")
set(FSIM_TF_LINK_CMAKE
  "${FSIM_SOURCE_DIR}/cmake/FsimNativePlugin.cmake")
set(FSIM_TF_LINK_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_link.cpp")
set(FSIM_TF_LINK_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_plugin_link_test.cpp")
set(FSIM_TF_LINK_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_link_probe_plugin.c")
set(FSIM_TF_PLUGIN_ABI
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_plugin_abi.h")
set(FSIM_TF_PLUGIN_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_plugin.hpp")
set(FSIM_TF_PLUGIN_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_plugin.cpp")
set(FSIM_TF_PLUGIN_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_plugin_test.cpp")
set(FSIM_TF_REGISTRATION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_registration.hpp")
set(FSIM_TF_REGISTRATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_registration.cpp")
set(FSIM_TF_REGISTRATION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_registration_test.cpp")
set(FSIM_TF_CALL_BRIDGE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_call_bridge.h")
set(FSIM_TF_CALL_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_call.hpp")
set(FSIM_TF_CALL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_call.cpp")
set(FSIM_TF_CALL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_call_test.cpp")
set(FSIM_TF_MISC_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_misc.hpp")
set(FSIM_TF_MISC_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_misc.cpp")
set(FSIM_TF_MISC_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_misc_test.cpp")
set(FSIM_TF_ARGUMENT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_argument.hpp")
set(FSIM_TF_ARGUMENT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_argument.cpp")
set(FSIM_TF_ARGUMENT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_argument_test.cpp")
set(FSIM_TF_VALUE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_value.hpp")
set(FSIM_TF_VALUE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_value.cpp")
set(FSIM_TF_VALUE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_value_test.cpp")
set(FSIM_TF_INSTANCE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_instance.hpp")
set(FSIM_TF_INSTANCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_instance.cpp")
set(FSIM_TF_INSTANCE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_instance_test.cpp")
set(FSIM_TF_TIME_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_time.hpp")
set(FSIM_TF_TIME_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_time.cpp")
set(FSIM_TF_TIME_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_time_test.cpp")
set(FSIM_TF_CONTEXT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_context.hpp")
set(FSIM_TF_CONTEXT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_context.cpp")
set(FSIM_TF_CONTEXT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_context_test.cpp")
set(FSIM_TF_CONTROL_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_control.hpp")
set(FSIM_TF_CONTROL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_control.cpp")
set(FSIM_TF_CONTROL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_control_test.cpp")
set(FSIM_TF_SYNCHRONIZATION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_synchronization.hpp")
set(FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_synchronization.cpp")
set(FSIM_TF_SYNCHRONIZATION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_synchronization_test.cpp")
set(FSIM_TF_APPLICATION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/app/application_tf.hpp")
set(FSIM_TF_APPLICATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_tf.cpp")
set(FSIM_TF_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/tf_plugin_application_test.cpp")
set(FSIM_TF_SCHEDULER_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/app/application_tf_scheduler.hpp")
set(FSIM_TF_SCHEDULER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_tf_scheduler.cpp")
set(FSIM_TF_SCHEDULER_TEST
  "${FSIM_SOURCE_DIR}/tests/app/tf_scheduler_application_test.cpp")
set(FSIM_TF_CONTAINMENT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_containment.hpp")
set(FSIM_TF_CONTAINMENT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_containment.cpp")
set(FSIM_TF_CONTAINMENT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_containment_test.cpp")
set(FSIM_TF_CPP_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_cpp_probe_plugin.cpp")
set(FSIM_TF_CROSS_PLATFORM_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_cross_platform_plugins_test.cpp")
set(FSIM_HOSTED_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_RUNTIME_TEST_CMAKE
  "${FSIM_SOURCE_DIR}/tests/runtime/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_INVENTORY}" "${FSIM_PLAN}" "${FSIM_TEST_CMAKE}"
    "${FSIM_FEATURE_README}" "${FSIM_NATIVE_PLUGIN_ABI}"
    "${FSIM_NATIVE_PLUGIN_MODEL}" "${FSIM_NATIVE_PLUGIN_IMPLEMENTATION}"
    "${FSIM_NATIVE_PLUGIN_TEST}" "${FSIM_NATIVE_PLUGIN_C_TEST}"
    "${FSIM_VERIUSER_HEADER}" "${FSIM_VERIUSER_TEST}"
    "${FSIM_VERIUSER_C_TEST}"
    "${FSIM_TF_LINK_CMAKE}" "${FSIM_TF_LINK_IMPLEMENTATION}"
    "${FSIM_TF_LINK_TEST}" "${FSIM_TF_LINK_PLUGIN}"
    "${FSIM_TF_PLUGIN_ABI}" "${FSIM_TF_PLUGIN_MODEL}"
    "${FSIM_TF_PLUGIN_IMPLEMENTATION}" "${FSIM_TF_PLUGIN_TEST}"
    "${FSIM_TF_REGISTRATION_MODEL}"
    "${FSIM_TF_REGISTRATION_IMPLEMENTATION}"
    "${FSIM_TF_REGISTRATION_TEST}"
    "${FSIM_TF_CALL_BRIDGE}" "${FSIM_TF_CALL_MODEL}"
    "${FSIM_TF_CALL_IMPLEMENTATION}" "${FSIM_TF_CALL_TEST}"
    "${FSIM_TF_MISC_MODEL}" "${FSIM_TF_MISC_IMPLEMENTATION}"
    "${FSIM_TF_MISC_TEST}"
    "${FSIM_TF_ARGUMENT_MODEL}" "${FSIM_TF_ARGUMENT_IMPLEMENTATION}"
    "${FSIM_TF_ARGUMENT_TEST}"
    "${FSIM_TF_VALUE_MODEL}" "${FSIM_TF_VALUE_IMPLEMENTATION}"
    "${FSIM_TF_VALUE_TEST}"
    "${FSIM_TF_INSTANCE_MODEL}" "${FSIM_TF_INSTANCE_IMPLEMENTATION}"
    "${FSIM_TF_INSTANCE_TEST}"
    "${FSIM_TF_TIME_MODEL}" "${FSIM_TF_TIME_IMPLEMENTATION}"
    "${FSIM_TF_TIME_TEST}"
    "${FSIM_TF_CONTEXT_MODEL}" "${FSIM_TF_CONTEXT_IMPLEMENTATION}"
    "${FSIM_TF_CONTEXT_TEST}"
    "${FSIM_TF_CONTROL_MODEL}" "${FSIM_TF_CONTROL_IMPLEMENTATION}"
    "${FSIM_TF_CONTROL_TEST}"
    "${FSIM_TF_SYNCHRONIZATION_MODEL}"
    "${FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION}"
    "${FSIM_TF_SYNCHRONIZATION_TEST}"
    "${FSIM_TF_APPLICATION_MODEL}"
    "${FSIM_TF_APPLICATION_IMPLEMENTATION}"
    "${FSIM_TF_APPLICATION_TEST}"
    "${FSIM_TF_SCHEDULER_MODEL}"
    "${FSIM_TF_SCHEDULER_IMPLEMENTATION}"
    "${FSIM_TF_SCHEDULER_TEST}"
    "${FSIM_TF_CONTAINMENT_MODEL}"
    "${FSIM_TF_CONTAINMENT_IMPLEMENTATION}"
    "${FSIM_TF_CONTAINMENT_TEST}"
    "${FSIM_TF_CPP_PLUGIN}"
    "${FSIM_TF_CROSS_PLATFORM_TEST}"
    "${FSIM_HOSTED_WORKFLOW}"
    "${FSIM_RUNTIME_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "legacy TF inventory input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_TEXT)
string(REPLACE "\r\n" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(REPLACE "\r" "\n" FSIM_INVENTORY_TEXT "${FSIM_INVENTORY_TEXT}")
string(SHA256 FSIM_ACTUAL_DIGEST "${FSIM_INVENTORY_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "a3bff1ef32fbf3fd412556c63c39e69a45f4efdd5cb599d07b3ac47136b531e0")
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "legacy TF inventory digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

string(TOLOWER "${FSIM_INVENTORY_TEXT}" FSIM_INVENTORY_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "/home/" "standards/" "file://" "modelsim" "questa" "vcs"
    "xcelium" "ncsim" "iverilog" "verilator" "riviera" "aldec")
  string(FIND "${FSIM_INVENTORY_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "legacy TF inventory records private, absolute, or vendor-specific text")
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
  math(EXPR FSIM_REMAINDER_START "${FSIM_BATCH_OFFSET} + 1")
  string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_REMAINDER_START} -1
    FSIM_PLAN_REMAINDER)
  string(FIND "${FSIM_PLAN_REMAINDER}" "${FSIM_BATCH_TOKEN}"
    FSIM_DUPLICATE_OFFSET)
  if(NOT FSIM_DUPLICATE_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan duplicates Batch ${FSIM_BATCH}")
  endif()
endforeach()

string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 181 - IEEE legacy TF PLI" FSIM_BATCH_START)
string(FIND "${FSIM_PLAN_TEXT}"
  "### Batch 182 - IEEE ACC and complete legacy PLI closure" FSIM_BATCH_END)
if(FSIM_BATCH_START EQUAL -1 OR FSIM_BATCH_END EQUAL -1 OR
   FSIM_BATCH_END LESS_EQUAL FSIM_BATCH_START)
  message(FATAL_ERROR "authoritative Batch 181 plan boundary is missing")
endif()
math(EXPR FSIM_BATCH_LENGTH "${FSIM_BATCH_END} - ${FSIM_BATCH_START}")
string(SUBSTRING "${FSIM_PLAN_TEXT}" ${FSIM_BATCH_START}
  ${FSIM_BATCH_LENGTH} FSIM_BATCH_TEXT)
foreach(FSIM_CHANGE RANGE 1 20)
  set(FSIM_CHANGE_TOKEN "\n${FSIM_CHANGE}. ")
  string(FIND "${FSIM_BATCH_TEXT}" "${FSIM_CHANGE_TOKEN}"
    FSIM_CHANGE_OFFSET)
  if(FSIM_CHANGE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 181 plan lost exact Change ${FSIM_CHANGE}")
  endif()
  math(EXPR FSIM_REMAINDER_START "${FSIM_CHANGE_OFFSET} + 1")
  string(SUBSTRING "${FSIM_BATCH_TEXT}" ${FSIM_REMAINDER_START} -1
    FSIM_BATCH_REMAINDER)
  string(FIND "${FSIM_BATCH_REMAINDER}" "${FSIM_CHANGE_TOKEN}"
    FSIM_DUPLICATE_OFFSET)
  if(NOT FSIM_DUPLICATE_OFFSET EQUAL -1)
    message(FATAL_ERROR "Batch 181 plan duplicates Change ${FSIM_CHANGE}")
  endif()
endforeach()
foreach(FSIM_TOKEN IN ITEMS
    "Register IEEE TF requirements and explicitly exclude vendor extensions."
    "Contain plugin exceptions, invalid pointers, unload, and re-entry."
    "Prove independently authored C/C++ plugins on Linux and Windows."
    "Run standard batch closure and freeze the TF surface."
    "Readers reject v2 manifests")
  string(FIND "${FSIM_PLAN_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "v3 plan lost required TF token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "NAME fsim.legacy-tf-inventory"
    "CheckLegacyTfInventory.cmake")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "legacy TF inventory registration lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_FEATURE_README}" FSIM_FEATURE_README_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "Batch 181 starts with the IEEE-only 18-row"
    "legacy_tf_inventory.tsv"
    "all eight retained Verilog and SystemVerilog"
    "Vendor extensions are excluded"
    "a3bff1ef32fbf3fd412556c63c39e69a45f4efdd5cb599d07b3ac47136b531e0")
  string(FIND "${FSIM_FEATURE_README_TEXT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "legacy TF feature contract lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

set(FSIM_EXPECTED_HEADER
  "id\tstandard\thdl_profiles\tdomain\tstate\tchange\tobligation\timplementation_owner\tpositive_evidence\tnegative_evidence\tlinux_evidence\twindows_evidence\tscheduler_evidence\tabi_evidence\tdiagnostic_owner\tresource_owner\textension_policy")
set(FSIM_EXPECTED_STANDARD "IEEE1364-2005")
set(FSIM_EXPECTED_PROFILES
  "V1995,V2001,V2001NoConfig,V2005,SV2005,SV2009,SV2012,SV2017")
set(FSIM_EXPECTED_EXTENSION_POLICY "ieee-only-no-vendor-extensions")
set(FSIM_EXPECTED_DOMAINS
  native-plugin-abi
  veriuser-header
  platform-link-surfaces
  registration-table-discovery
  descriptor-validation
  task-function-callbacks
  misctf-lifecycle
  argument-inspection
  value-access
  parameter-instance-access
  time-delay-timescale
  scope-workarea-userdata
  output-control
  synchronization-callbacks
  hdl-system-registration
  scheduler-coordination
  failure-containment
  cross-platform-plugins)
set(FSIM_COMPLETED_CHANGE 19)

function(fsim_require_legacy_tf_owner
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
    message(FATAL_ERROR "${FSIM_ID} has unsafe ${FSIM_FIELD} owner: ${FSIM_PATH}")
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
  message(FATAL_ERROR "legacy TF inventory SPDX policy or header changed")
endif()

set(FSIM_IDS)
set(FSIM_CHANGES)
set(FSIM_DOMAINS)
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
    message(FATAL_ERROR "legacy TF inventory row has ${FSIM_FIELD_COUNT} fields")
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
  list(GET FSIM_FIELDS 16 FSIM_EXTENSION_POLICY)
  if(NOT FSIM_ID STREQUAL "LEGTF-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_STANDARD STREQUAL FSIM_EXPECTED_STANDARD OR
     NOT FSIM_PROFILES STREQUAL FSIM_EXPECTED_PROFILES OR
     NOT FSIM_DOMAIN STREQUAL FSIM_EXPECTED_DOMAIN OR
     NOT FSIM_STATE STREQUAL FSIM_EXPECTED_STATE OR
     NOT FSIM_CHANGE_ID STREQUAL "B181-C${FSIM_CHANGE_TEXT}" OR
     NOT FSIM_EXTENSION_POLICY STREQUAL FSIM_EXPECTED_EXTENSION_POLICY)
    message(FATAL_ERROR
      "${FSIM_ID} has drifted standard, profiles, domain, state, change, or extension policy")
  endif()
  string(LENGTH "${FSIM_OBLIGATION}" FSIM_OBLIGATION_LENGTH)
  if(FSIM_OBLIGATION_LENGTH LESS 32 OR FSIM_OBLIGATION_LENGTH GREATER 240)
    message(FATAL_ERROR "${FSIM_ID} lost its bounded independent obligation")
  endif()

  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE_ID)
  list(FIND FSIM_CHANGES "${FSIM_CHANGE_ID}" FSIM_DUPLICATE_CHANGE)
  list(FIND FSIM_DOMAINS "${FSIM_DOMAIN}" FSIM_DUPLICATE_DOMAIN)
  if(NOT FSIM_DUPLICATE_ID EQUAL -1 OR
     NOT FSIM_DUPLICATE_CHANGE EQUAL -1 OR
     NOT FSIM_DUPLICATE_DOMAIN EQUAL -1)
    message(FATAL_ERROR "duplicate legacy TF inventory identity: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_CHANGES "${FSIM_CHANGE_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")

  list(GET FSIM_FIELDS 7 FSIM_IMPLEMENTATION_OWNER)
  fsim_require_legacy_tf_owner("${FSIM_ID}" implementation
    "${FSIM_IMPLEMENTATION_OWNER}" "(include|src|cmake)/"
    "${FSIM_REQUIRE_EXISTS}")
  foreach(FSIM_EVIDENCE_INDEX RANGE 8 13)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE_OWNER)
    fsim_require_legacy_tf_owner("${FSIM_ID}"
      "evidence-${FSIM_EVIDENCE_INDEX}" "${FSIM_EVIDENCE_OWNER}" "tests/"
      "${FSIM_REQUIRE_EXISTS}")
  endforeach()
  list(GET FSIM_FIELDS 14 FSIM_DIAGNOSTIC_OWNER)
  list(GET FSIM_FIELDS 15 FSIM_RESOURCE_OWNER)
  if(NOT FSIM_DIAGNOSTIC_OWNER STREQUAL "docs/diagnostics.md" OR
     NOT FSIM_RESOURCE_OWNER STREQUAL
       "cmake/CheckResourcePortabilityContract.cmake" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_DIAGNOSTIC_OWNER}" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_RESOURCE_OWNER}")
    message(FATAL_ERROR "${FSIM_ID} drifted diagnostic or resource ownership")
  endif()

  if(FSIM_STATE STREQUAL "active")
    math(EXPR FSIM_ACTIVE_COUNT "${FSIM_ACTIVE_COUNT} + 1")
  elseif(FSIM_STATE STREQUAL "preserved")
    math(EXPR FSIM_PRESERVED_COUNT "${FSIM_PRESERVED_COUNT} + 1")
  else()
    message(FATAL_ERROR "${FSIM_ID} has unsupported state ${FSIM_STATE}")
  endif()
  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
endforeach()

math(EXPR FSIM_EXPECTED_PRESERVED "${FSIM_COMPLETED_CHANGE} - 1")
math(EXPR FSIM_EXPECTED_ACTIVE "18 - ${FSIM_EXPECTED_PRESERVED}")
if(NOT FSIM_ROW_COUNT EQUAL 18 OR
   NOT FSIM_ACTIVE_COUNT EQUAL FSIM_EXPECTED_ACTIVE OR
   NOT FSIM_PRESERVED_COUNT EQUAL FSIM_EXPECTED_PRESERVED)
  message(FATAL_ERROR
    "legacy TF inventory expected ${FSIM_EXPECTED_ACTIVE} active and ${FSIM_EXPECTED_PRESERVED} preserved rows; got rows=${FSIM_ROW_COUNT}, active=${FSIM_ACTIVE_COUNT}, preserved=${FSIM_PRESERVED_COUNT}")
endif()

file(READ "${FSIM_NATIVE_PLUGIN_ABI}" FSIM_NATIVE_PLUGIN_ABI_TEXT)
file(READ "${FSIM_NATIVE_PLUGIN_MODEL}" FSIM_NATIVE_PLUGIN_MODEL_TEXT)
file(READ "${FSIM_NATIVE_PLUGIN_IMPLEMENTATION}"
  FSIM_NATIVE_PLUGIN_IMPLEMENTATION_TEXT)
file(READ "${FSIM_NATIVE_PLUGIN_TEST}" FSIM_NATIVE_PLUGIN_TEST_TEXT)
file(READ "${FSIM_NATIVE_PLUGIN_C_TEST}" FSIM_NATIVE_PLUGIN_C_TEST_TEXT)
file(READ "${FSIM_RUNTIME_TEST_CMAKE}" FSIM_RUNTIME_TEST_CMAKE_TEXT)
set(FSIM_NATIVE_PLUGIN_CONTRACT
  "${FSIM_NATIVE_PLUGIN_ABI_TEXT}${FSIM_NATIVE_PLUGIN_MODEL_TEXT}${FSIM_NATIVE_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_NATIVE_PLUGIN_TEST_TEXT}${FSIM_NATIVE_PLUGIN_C_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_NATIVE_PLUGIN_ABI_VERSION 3u"
    "fsim_native_plugin_descriptor_v3_get"
    "fsim_native_plugin_descriptor_v3"
    "FSIM_NATIVE_PLUGIN_CAPABILITY_TF"
    "FSIM_NATIVE_PLUGIN_CAPABILITY_ACC"
    "FSIM_NATIVE_PLUGIN_KNOWN_CAPABILITIES"
    "validate_native_plugin_descriptor"
    "copy_native_plugin_metadata"
    "value.abi_version = 2"
    "fsim.runtime.native_plugin_abi")
  string(FIND "${FSIM_NATIVE_PLUGIN_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "v3 native plug-in ABI lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()
string(FIND "${FSIM_NATIVE_PLUGIN_ABI_TEXT}" "descriptor_v2"
  FSIM_V2_DESCRIPTOR_OFFSET)
if(NOT FSIM_V2_DESCRIPTOR_OFFSET EQUAL -1)
  message(FATAL_ERROR "v3 native plug-in ABI exposed a v2 descriptor")
endif()

file(READ "${FSIM_VERIUSER_HEADER}" FSIM_VERIUSER_HEADER_TEXT)
file(READ "${FSIM_VERIUSER_TEST}" FSIM_VERIUSER_TEST_TEXT)
file(READ "${FSIM_VERIUSER_C_TEST}" FSIM_VERIUSER_C_TEST_TEXT)
set(FSIM_VERIUSER_CONTRACT
  "${FSIM_VERIUSER_HEADER_TEXT}${FSIM_VERIUSER_TEST_TEXT}${FSIM_VERIUSER_C_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "typedef int PLI_INT32"
    "reason_checktf 1"
    "reason_rosynch 11"
    "reason_startofrestart 28"
    "tf_readwritereal 16"
    "tf_real_node 107"
    "typedef struct t_vecval"
    "typedef struct t_tfexprinfo"
    "typedef struct t_tfnodeinfo"
    "tf_getinstance"
    "tf_igetp"
    "tf_rosynchronize"
    "tf_synchronize"
    "veriuser_version_str"
    "endofcompile_routines"
    "must not hide C++ keywords"
    "fsim.runtime.veriuser_abi")
  string(FIND "${FSIM_VERIUSER_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "standard veriuser header lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()
string(TOLOWER "${FSIM_VERIUSER_HEADER_TEXT}" FSIM_VERIUSER_HEADER_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "s_tfcell" "veriusertfs" "usertask" "userfunction")
  string(FIND "${FSIM_VERIUSER_HEADER_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "Change 3 veriuser header pulled in registration or vendor surface: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_LINK_CMAKE}" FSIM_TF_LINK_CMAKE_TEXT)
file(READ "${FSIM_TF_LINK_IMPLEMENTATION}" FSIM_TF_LINK_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_LINK_TEST}" FSIM_TF_LINK_TEST_TEXT)
file(READ "${FSIM_TF_LINK_PLUGIN}" FSIM_TF_LINK_PLUGIN_TEXT)
set(FSIM_TF_LINK_CONTRACT
  "${FSIM_TF_LINK_CMAKE_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_TEST_TEXT}${FSIM_TF_LINK_PLUGIN_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "fsim_add_tf_link_surface"
    "add_library(fsim::tf ALIAS"
    "FSIM_TF_LINK_SURFACE_BUILD=1"
    "OUTPUT_NAME fsim_tf"
    "SOVERSION 3"
    "FSIM_NATIVE_PLUGIN_CAPABILITY_TF"
    "standard_tf_symbols"
    "standard_tf_symbols[0])"
    "err_intercept"
    "veriuser_version_str"
    "vpi_printf"
    "fsim.runtime.tf_plugin_link")
  string(FIND "${FSIM_TF_LINK_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "TF platform link surface lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_PLUGIN_ABI}" FSIM_TF_PLUGIN_ABI_TEXT)
file(READ "${FSIM_TF_PLUGIN_MODEL}" FSIM_TF_PLUGIN_MODEL_TEXT)
file(READ "${FSIM_TF_PLUGIN_IMPLEMENTATION}"
  FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_PLUGIN_TEST}" FSIM_TF_PLUGIN_TEST_TEXT)
set(FSIM_TF_PLUGIN_CONTRACT
  "${FSIM_NATIVE_PLUGIN_ABI_TEXT}${FSIM_TF_PLUGIN_ABI_TEXT}${FSIM_TF_PLUGIN_MODEL_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_TF_PLUGIN_TEST_TEXT}${FSIM_TF_LINK_PLUGIN_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_NATIVE_PLUGIN_DESCRIPTOR_SYMBOL"
    "FSIM_NATIVE_PLUGIN_MAX_INTERFACES 8u"
    "fsim_native_plugin_interface_v3"
    "FSIM_TF_INTERFACE_ABI_VERSION 3u"
    "FSIM_TF_REGISTRATION_TABLE_ABI_VERSION 3u"
    "FSIM_TF_MAX_REGISTRATIONS 4096u"
    "fsim_tf_registration_v3"
    "fsim_tf_registration_table_v3"
    "validate_tf_registration_table"
    "validate_tf_plugin_descriptor"
    "InterfaceDuplicate"
    "RegistrationTable"
    "fsim.runtime.tf_plugin")
  string(FIND "${FSIM_TF_PLUGIN_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF registration discovery lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()
string(TOLOWER "${FSIM_TF_PLUGIN_CONTRACT}" FSIM_TF_PLUGIN_CONTRACT_LOWER)
foreach(FSIM_FORBIDDEN IN ITEMS
    "s_tfcell" "veriusertfs" "vlog_startup_routines" "usertask"
    "userfunction")
  string(FIND "${FSIM_TF_PLUGIN_CONTRACT_LOWER}" "${FSIM_FORBIDDEN}"
    FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF discovery admitted an obsolete or vendor bootstrap surface: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_REGISTRATION_MODEL}"
  FSIM_TF_REGISTRATION_MODEL_TEXT)
file(READ "${FSIM_TF_REGISTRATION_IMPLEMENTATION}"
  FSIM_TF_REGISTRATION_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_REGISTRATION_TEST}"
  FSIM_TF_REGISTRATION_TEST_TEXT)
set(FSIM_TF_REGISTRATION_CONTRACT
  "${FSIM_TF_PLUGIN_ABI_TEXT}${FSIM_TF_PLUGIN_MODEL_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_TF_REGISTRATION_MODEL_TEXT}${FSIM_TF_REGISTRATION_IMPLEMENTATION_TEXT}${FSIM_TF_REGISTRATION_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "FSIM_TF_MAX_REGISTRATION_NAME_SIZE 255u"
    "TfRegistrationKind"
    "TfRegistrationError"
    "DuplicateName"
    "validate_and_copy_tf_registrations"
    "registration.struct_size > table.entry_stride"
    "registration.calltf == nullptr"
    "registration.sizetf == nullptr"
    "registration.sizetf != nullptr"
    "valid_registration_name"
    "result.value.empty()"
    "callback_calls == 0"
    "fsim.runtime.tf_registration")
  string(FIND "${FSIM_TF_REGISTRATION_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF descriptor validation lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_CALL_BRIDGE}" FSIM_TF_CALL_BRIDGE_TEXT)
file(READ "${FSIM_TF_CALL_MODEL}" FSIM_TF_CALL_MODEL_TEXT)
file(READ "${FSIM_TF_CALL_IMPLEMENTATION}" FSIM_TF_CALL_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_CALL_TEST}" FSIM_TF_CALL_TEST_TEXT)
set(FSIM_TF_CALL_CONTRACT
  "${FSIM_TF_PLUGIN_ABI_TEXT}${FSIM_TF_PLUGIN_MODEL_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_CALL_TEST_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_PLUGIN_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "fsim_tf_routine_v3"
    "fsim_tf_misc_routine_v3"
    "kMaxTfFunctionWidth"
    "TfFunctionResult"
    "TfBoundCall"
    "bind_tf_call_with_owner"
    "reason_checktf"
    "reason_sizetf"
    "reason_calltf"
    "TfCallError::CallbackException"
    "TfCallError::ContextBusy"
    "TfCallError::UnassignedResult"
    "fsim_tf_call_context_enter_v3"
    "fsim_tf_call_context_leave_v3"
    "assign_integral_result"
    "tf_putlongp"
    "tf_putrealp"
    "loaded.value.reset()"
    "fsim.runtime.tf_call")
  string(FIND "${FSIM_TF_CALL_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF task/function callback contract lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_MISC_MODEL}" FSIM_TF_MISC_MODEL_TEXT)
file(READ "${FSIM_TF_MISC_IMPLEMENTATION}" FSIM_TF_MISC_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_MISC_TEST}" FSIM_TF_MISC_TEST_TEXT)
set(FSIM_TF_MISC_CONTRACT
  "${FSIM_TF_PLUGIN_MODEL_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_TF_MISC_MODEL_TEXT}${FSIM_TF_MISC_IMPLEMENTATION_TEXT}${FSIM_TF_MISC_TEST_TEXT}${FSIM_TF_LINK_PLUGIN_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "TfMiscReason"
    "ParameterValueChange = reason_paramvc"
    "Synchronize = reason_synch"
    "ReadOnlySynchronize = reason_rosynch"
    "StartOfSave = reason_startofsave"
    "StartOfRestart = reason_startofrestart"
    "TfMiscDispatcher"
    "valid_parameter"
    "parameter > 0"
    "parameter == 0"
    "TfMiscError::CallbackException"
    "TfMiscError::Reentrant"
    "last_callback_value == 31"
    "loaded.value.reset()"
    "fsim.runtime.tf_misc")
  string(FIND "${FSIM_TF_MISC_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "TF misctf lifecycle lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_ARGUMENT_MODEL}" FSIM_TF_ARGUMENT_MODEL_TEXT)
file(READ "${FSIM_TF_ARGUMENT_IMPLEMENTATION}"
  FSIM_TF_ARGUMENT_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_ARGUMENT_TEST}" FSIM_TF_ARGUMENT_TEST_TEXT)
set(FSIM_TF_ARGUMENT_CONTRACT
  "${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_ARGUMENT_MODEL_TEXT}${FSIM_TF_ARGUMENT_IMPLEMENTATION_TEXT}${FSIM_TF_ARGUMENT_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfArguments"
    "kMaxTfArgumentWidth"
    "kMaxTfExpressionTextSize"
    "TfArgumentKind"
    "TfArgumentDirection"
    "validate_and_copy_tf_arguments"
    "FSIM_TF_CALL_PHASE_CHECK"
    "FSIM_TF_CALL_PHASE_SIZE"
    "FSIM_TF_CALL_PHASE_CALL"
    "fsim_tf_argument_bridge_v3"
    "tf_nump(void)"
    "tf_typep"
    "tf_sizep"
    "tf_exprinfo"
    "TfCallError::InvalidArguments"
    "phase_calls[reason_checktf] == 1"
    "phase_calls[reason_sizetf] == 1"
    "phase_calls[reason_calltf] == 1"
    "fsim.runtime.tf_argument")
  string(FIND "${FSIM_TF_ARGUMENT_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "TF argument inspection lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_VALUE_MODEL}" FSIM_TF_VALUE_MODEL_TEXT)
file(READ "${FSIM_TF_VALUE_IMPLEMENTATION}"
  FSIM_TF_VALUE_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_VALUE_TEST}" FSIM_TF_VALUE_TEST_TEXT)
set(FSIM_TF_VALUE_CONTRACT
  "${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_VALUE_MODEL_TEXT}${FSIM_TF_VALUE_IMPLEMENTATION_TEXT}${FSIM_TF_VALUE_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfCallValueBytes"
    "TfValueKind"
    "TfValueError"
    "validate_and_copy_tf_values"
    "make_default_tf_values"
    "FSIM_TF_VALUE_INTEGRAL"
    "TfArgumentUpdate"
    "TfCallError::InvalidValues"
    "tf_getcstringp"
    "tf_getlongp"
    "tf_getrealp"
    "tf_strgetp"
    "tf_evaluatep"
    "tf_propagatep"
    "value.vector_words.back().bvalbits"
    "invoked.argument_updates.size() == 4"
    "fsim.runtime.tf_value")
  string(FIND "${FSIM_TF_VALUE_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR "TF value access lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_INSTANCE_MODEL}" FSIM_TF_INSTANCE_MODEL_TEXT)
file(READ "${FSIM_TF_INSTANCE_IMPLEMENTATION}"
  FSIM_TF_INSTANCE_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_INSTANCE_TEST}" FSIM_TF_INSTANCE_TEST_TEXT)
set(FSIM_TF_INSTANCE_CONTRACT
  "${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_PLUGIN_MODEL_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_TF_INSTANCE_MODEL_TEXT}${FSIM_TF_INSTANCE_IMPLEMENTATION_TEXT}${FSIM_TF_INSTANCE_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "TfInstanceIdentity"
    "TfInstanceError"
    "validate_tf_instance_identity"
    "FSIM_TF_INSTANCE_ABI_VERSION 3u"
    "fsim_tf_instance_bridge_v3"
    "TfCallError::InvalidInstance"
    "instance_identity"
    "tf_getinstance(void)"
    "current_instance_matches"
    "tf_iexprinfo"
    "tf_inodeinfo"
    "tf_igetp"
    "tf_iputp"
    "tf_istrgetp"
    "instance_tokens[1] != instance_tokens[2]"
    "fsim.runtime.tf_instance")
  string(FIND "${FSIM_TF_INSTANCE_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF parameter/instance access lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_TIME_MODEL}" FSIM_TF_TIME_MODEL_TEXT)
file(READ "${FSIM_TF_TIME_IMPLEMENTATION}" FSIM_TF_TIME_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_TIME_TEST}" FSIM_TF_TIME_TEST_TEXT)
set(FSIM_TF_TIME_CONTRACT
  "${FSIM_VERIUSER_HEADER_TEXT}${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_TIME_MODEL_TEXT}${FSIM_TF_TIME_IMPLEMENTATION_TEXT}${FSIM_TF_TIME_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfDelayRequests"
    "TfTimeProfile"
    "TfTimeState"
    "TfTimeError"
    "validate_tf_time_profile"
    "tf_time_to_local_integer"
    "tf_local_integer_to_ticks"
    "tf_local_real_to_ticks"
    "fsim_tf_time_bridge_v3"
    "TfCallError::InvalidTime"
    "time_profile"
    "delay_requests"
    "tf_getlongtime"
    "tf_getnextlongtime"
    "tf_getrealtime"
    "tf_gettimeprecision"
    "tf_gettimeunit"
    "tf_setdelay"
    "tf_setlongdelay"
    "tf_setrealdelay"
    "tf_scale_longdelay"
    "tf_unscale_longdelay"
    "thrown.delay_requests.empty()"
    "fsim.runtime.tf_time")
  string(FIND "${FSIM_TF_TIME_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF time, delay, or timescale access lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_CONTEXT_MODEL}" FSIM_TF_CONTEXT_MODEL_TEXT)
file(READ "${FSIM_TF_CONTEXT_IMPLEMENTATION}"
  FSIM_TF_CONTEXT_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_CONTEXT_TEST}" FSIM_TF_CONTEXT_TEST_TEXT)
set(FSIM_TF_CONTEXT_CONTRACT
  "${FSIM_VERIUSER_HEADER_TEXT}${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_PLUGIN_MODEL_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_TF_CONTEXT_MODEL_TEXT}${FSIM_TF_CONTEXT_IMPLEMENTATION_TEXT}${FSIM_TF_CONTEXT_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfContextNameSize"
    "TfContextProfile"
    "TfContextError"
    "validate_and_copy_tf_context"
    "scope_belongs_to_module"
    "TfCallError::InvalidContext"
    "context_profile"
    "std::recursive_mutex"
    "module_instance_name"
    "scope_name"
    "routine_name"
    "work_area"
    "registration.user_data"
    "tf_mipname"
    "tf_spname"
    "tf_getroutine"
    "tf_getworkarea"
    "tf_setworkarea"
    "tf_imipname"
    "tf_ispname"
    "tf_igetroutine"
    "tf_igetworkarea"
    "tf_isetworkarea"
    "instance_calls[0] == 2"
    "TfCallError::CallbackException"
    "fsim.runtime.tf_context")
  string(FIND "${FSIM_TF_CONTEXT_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF context or lifetime access lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_CONTROL_MODEL}" FSIM_TF_CONTROL_MODEL_TEXT)
file(READ "${FSIM_TF_CONTROL_IMPLEMENTATION}"
  FSIM_TF_CONTROL_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_CONTROL_TEST}" FSIM_TF_CONTROL_TEST_TEXT)
set(FSIM_TF_CONTROL_CONTRACT
  "${FSIM_VERIUSER_HEADER_TEXT}${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_CONTROL_MODEL_TEXT}${FSIM_TF_CONTROL_IMPLEMENTATION_TEXT}${FSIM_TF_CONTROL_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfControlEffects"
    "kMaxTfControlTextSize"
    "kMaxTfControlBytes"
    "TfControlEffectKind"
    "TfControlEffect"
    "TfControlCapture"
    "capture_tf_control_effect_v3"
    "FSIM_TF_CONTROL_OUTPUT"
    "TfCallError::ControlLimit"
    "control_effects"
    "control_emit"
    "control_failed"
    "format_control"
    "io_printf"
    "io_mcdprintf"
    "tf_text"
    "tf_warning"
    "tf_error"
    "tf_message"
    "tf_dofinish"
    "tf_dostop"
    "missing_result.control_effects.empty()"
    "fsim.runtime.tf_control")
  string(FIND "${FSIM_TF_CONTROL_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF output or control access lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_SYNCHRONIZATION_MODEL}"
  FSIM_TF_SYNCHRONIZATION_MODEL_TEXT)
file(READ "${FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION}"
  FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_SYNCHRONIZATION_TEST}"
  FSIM_TF_SYNCHRONIZATION_TEST_TEXT)
set(FSIM_TF_SYNCHRONIZATION_CONTRACT
  "${FSIM_VERIUSER_HEADER_TEXT}${FSIM_TF_CALL_BRIDGE_TEXT}${FSIM_TF_CALL_MODEL_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_TF_SYNCHRONIZATION_MODEL_TEXT}${FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION_TEXT}${FSIM_TF_SYNCHRONIZATION_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfSynchronizationRequests"
    "TfSynchronizationKind"
    "TfSynchronizationRequest"
    "valid_tf_synchronization_kind"
    "tf_synchronization_reason"
    "FSIM_TF_CALL_PHASE_SYNCHRONIZE"
    "FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE"
    "FSIM_TF_SYNCHRONIZATION_READ_WRITE"
    "FSIM_TF_SYNCHRONIZATION_READ_ONLY"
    "synchronization_requests"
    "TfCallError::InvalidSynchronization"
    "TfCallError::MissingMiscCallback"
    "synchronization_count"
    "append_synchronization"
    "tf_synchronize"
    "tf_rosynchronize"
    "tf_isynchronize"
    "tf_irosynchronize"
    "reason_synch"
    "reason_rosynch"
    "read_write.argument_updates.size() == 1"
    "read_only.argument_updates.empty()"
    "TfCallError::ContextBusy"
    "fsim.runtime.tf_synchronization")
  string(FIND "${FSIM_TF_SYNCHRONIZATION_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF synchronization access lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_APPLICATION_MODEL}" FSIM_TF_APPLICATION_MODEL_TEXT)
file(READ "${FSIM_TF_APPLICATION_IMPLEMENTATION}"
  FSIM_TF_APPLICATION_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_APPLICATION_TEST}" FSIM_TF_APPLICATION_TEST_TEXT)
set(FSIM_TF_APPLICATION_CONTRACT
  "${FSIM_TF_APPLICATION_MODEL_TEXT}${FSIM_TF_APPLICATION_IMPLEMENTATION_TEXT}${FSIM_TF_APPLICATION_TEST_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}${FSIM_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfApplicationPlugins"
    "kMaxTfApplicationRegistrations"
    "TfApplicationRegistry"
    "TfApplicationRegistration"
    "tf_application_profile_supported"
    "StandardRevision::Verilog1995"
    "StandardRevision::Verilog2001"
    "StandardRevision::Verilog2001NoConfig"
    "StandardRevision::Verilog2005"
    "StandardRevision::SystemVerilog2005"
    "StandardRevision::SystemVerilog2009"
    "StandardRevision::SystemVerilog2012"
    "StandardRevision::SystemVerilog2017"
    "StandardRevision::Vhdl2008"
    "TfApplicationError::DuplicateRegistration"
    "TfApplicationError::UnsupportedProfile"
    "TfApplicationError::MissingRegistration"
    "TfApplicationError::KindMismatch"
    "runtime::load_tf_plugin"
    "registry.resolve"
    "registry.bind"
    "result_width() == 17"
    "registry.plugin_count() == 1"
    "fsim.application.tf-plugin")
  string(FIND "${FSIM_TF_APPLICATION_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF HDL system registration lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_SCHEDULER_MODEL}" FSIM_TF_SCHEDULER_MODEL_TEXT)
file(READ "${FSIM_TF_SCHEDULER_IMPLEMENTATION}"
  FSIM_TF_SCHEDULER_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_SCHEDULER_TEST}" FSIM_TF_SCHEDULER_TEST_TEXT)
set(FSIM_TF_SCHEDULER_CONTRACT
  "${FSIM_TF_SCHEDULER_MODEL_TEXT}${FSIM_TF_SCHEDULER_IMPLEMENTATION_TEXT}${FSIM_TF_SCHEDULER_TEST_TEXT}${FSIM_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "kMaxTfSchedulerCalls"
    "kMaxTfSchedulerCallbacks"
    "TfSchedulerCoordinator"
    "TfSchedulerPublication"
    "TfSchedulerCallbackKind::Reactivate"
    "TfSchedulerCallbackKind::ReadWriteSynchronize"
    "TfSchedulerCallbackKind::ReadOnlySynchronize"
    "SchedulerPhase::reactive"
    "SchedulerPhase::postponed"
    "schedule_after_cancelable"
    "stable_order_base"
    "TfSchedulerError::InactiveScheduler"
    "TfSchedulerError::Publication"
    "request_stop"
    "reason_reactivate"
    "plugin_coordinator.bind"
    "FSIM_TF_LINK_PROBE_PLUGIN_PATH"
    "fsim.application.tf-scheduler")
  string(FIND "${FSIM_TF_SCHEDULER_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF scheduler coordinator lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_CONTAINMENT_MODEL}" FSIM_TF_CONTAINMENT_MODEL_TEXT)
file(READ "${FSIM_TF_CONTAINMENT_IMPLEMENTATION}"
  FSIM_TF_CONTAINMENT_IMPLEMENTATION_TEXT)
file(READ "${FSIM_TF_CONTAINMENT_TEST}" FSIM_TF_CONTAINMENT_TEST_TEXT)
set(FSIM_TF_CONTAINMENT_CONTRACT
  "${FSIM_TF_CONTAINMENT_MODEL_TEXT}${FSIM_TF_CONTAINMENT_IMPLEMENTATION_TEXT}${FSIM_TF_CONTAINMENT_TEST_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_TF_REGISTRATION_IMPLEMENTATION_TEXT}${FSIM_TF_CALL_IMPLEMENTATION_TEXT}${FSIM_TF_LINK_IMPLEMENTATION_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "TfNativePointerAccess"
    "TfContainmentError"
    "validate_tf_native_pointer"
    "validate_tf_callback_pointer"
    "VirtualQuery"
    "/proc/self/maps"
    "TfPluginError::InvalidPointer"
    "TfRegistrationError::Pointer"
    "TfCallError::InvalidPointer"
    "TfCallError::CallbackException"
    "TfCallError::ContextBusy"
    "fsim_tf_call_context_enter_v3"
    "FSIM_TF_LINK_PROBE_PLUGIN_PATH"
    "loaded.value.reset()"
    "fsim.runtime.tf_containment")
  string(FIND "${FSIM_TF_CONTAINMENT_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF failure containment lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TF_CPP_PLUGIN}" FSIM_TF_CPP_PLUGIN_TEXT)
file(READ "${FSIM_TF_CROSS_PLATFORM_TEST}"
  FSIM_TF_CROSS_PLATFORM_TEST_TEXT)
file(READ "${FSIM_HOSTED_WORKFLOW}" FSIM_HOSTED_WORKFLOW_TEXT)
set(FSIM_TF_CROSS_PLATFORM_CONTRACT
  "${FSIM_TF_LINK_CMAKE_TEXT}${FSIM_TF_LINK_PLUGIN_TEXT}${FSIM_TF_CPP_PLUGIN_TEXT}${FSIM_TF_CROSS_PLATFORM_TEST_TEXT}${FSIM_TF_PLUGIN_MODEL_TEXT}${FSIM_TF_PLUGIN_IMPLEMENTATION_TEXT}${FSIM_RUNTIME_TEST_CMAKE_TEXT}${FSIM_HOSTED_WORKFLOW_TEXT}")
foreach(FSIM_TOKEN IN ITEMS
    "LINKER_LANGUAGE CXX"
    "c_std_11"
    "cxx_std_20"
    "fsim_tf_link_probe_plugin"
    "fsim_tf_cpp_probe_plugin"
    "fsim_native_plugin_descriptor_v3_get"
    "FSIM_TF_C_PLUGIN_PATH"
    "FSIM_TF_CPP_PLUGIN_PATH"
    "tf_plugin_artifact_loaded"
    "bound calls retain both native images"
    "both native images unload"
    "windows-llvm-mingw"
    "fsim.runtime.tf_cross_platform_plugins")
  string(FIND "${FSIM_TF_CROSS_PLATFORM_CONTRACT}" "${FSIM_TOKEN}"
    FSIM_TOKEN_OFFSET)
  if(FSIM_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "TF cross-platform plug-in proof lost contract token: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "legacy TF inventory passed: rows=${FSIM_ROW_COUNT} active=${FSIM_ACTIVE_COUNT} preserved=${FSIM_PRESERVED_COUNT} extension-policy=${FSIM_EXPECTED_EXTENSION_POLICY} digest=${FSIM_ACTUAL_DIGEST}")
