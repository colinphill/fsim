# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ABI_HEADER "${FSIM_SOURCE_DIR}/include/fsim/runtime/uvm_foreign_abi.h")
set(FSIM_ABI_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_uvm_foreign_abi_c_test.c")
set(FSIM_LIMIT_HEADER "${FSIM_SOURCE_DIR}/tests/app/uvm_process_limits.hpp")
set(FSIM_LIMIT_SOURCE "${FSIM_SOURCE_DIR}/tests/app/uvm_process_limits.cpp")
set(FSIM_APPLICATION
  "${FSIM_SOURCE_DIR}/tests/app/uvm_phase_tlm_application_test.cpp")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_RUNNER "${FSIM_SOURCE_DIR}/cmake/RunUvmPhaseTlmExample.cmake")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_ABI_HEADER}" "${FSIM_ABI_C_TEST}" "${FSIM_LIMIT_HEADER}"
    "${FSIM_LIMIT_SOURCE}" "${FSIM_APPLICATION}" "${FSIM_TEST_CMAKE}"
    "${FSIM_RUNNER}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "UVM platform-contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ABI_HEADER}" FSIM_ABI_HEADER_CONTENTS)
file(READ "${FSIM_ABI_C_TEST}" FSIM_ABI_C_TEST_CONTENTS)
file(READ "${FSIM_LIMIT_HEADER}" FSIM_LIMIT_HEADER_CONTENTS)
file(READ "${FSIM_LIMIT_SOURCE}" FSIM_LIMIT_SOURCE_CONTENTS)
file(READ "${FSIM_APPLICATION}" FSIM_APPLICATION_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_RUNNER}" FSIM_RUNNER_CONTENTS)

foreach(FSIM_TOKEN IN ITEMS
    "FSIM_UVM_FOREIGN_ABI_VERSION 1u"
    "defined(_WIN32)"
    "FSIM_UVM_FOREIGN_CALL __cdecl"
    "extern \"C\""
    "fsim_uvm_foreign_host_v1")
  string(FIND "${FSIM_ABI_HEADER_CONTENTS}" "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM foreign ABI lost contract: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "FSIM_UVM_LAYOUT(fsim_uvm_foreign_snapshot_v1, 48u, 8u)"
    "FSIM_UVM_LAYOUT(fsim_uvm_foreign_record_v1, 72u, 8u)"
    "FSIM_UVM_LAYOUT(fsim_uvm_foreign_activity_v1, 88u, 8u)"
    "FSIM_UVM_LAYOUT(fsim_uvm_foreign_host_v1, 64u, 8u)"
    "FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, remove_callback, 56u)"
    "fsim_uvm_foreign_activity_callback_v1 callback")
  string(FIND "${FSIM_ABI_C_TEST_CONTENTS}" "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM C ABI probe lost contract: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "6ULL * 1024ULL * 1024ULL * 1024ULL"
    "RLIMIT_AS"
    "JOB_OBJECT_LIMIT_PROCESS_MEMORY"
    "AssignProcessToJobObject")
  string(FIND
    "${FSIM_LIMIT_HEADER_CONTENTS}\n${FSIM_LIMIT_SOURCE_CONTENTS}"
    "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM process limiter lost contract: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "std::filesystem::absolute(uvm_root)"
    "install_uvm_process_address_space_ceiling()"
    "platform_contract=source/abi/linux/windows/cdecl/filesystem")
  string(FIND "${FSIM_APPLICATION_CONTENTS}" "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM application lost portability contract: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "fsim.application.uvm_phase_tlm.1_2"
    "fsim.application.uvm_phase_tlm.2020_3_1"
    "app/uvm_process_limits.cpp"
    "RUN_SERIAL TRUE"
    "TIMEOUT 7200")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM CTest lost platform contract: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_TOKEN IN ITEMS
    "set(FSIM_STAGE_TIMEOUT_SECONDS 1200)"
    "TIMEOUT \"\${FSIM_STAGE_TIMEOUT_SECONDS}\""
    "limits=as6g/stage1200/matrix7200")
  string(FIND "${FSIM_RUNNER_CONTENTS}" "${FSIM_TOKEN}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "UVM runner lost resource contract: ${FSIM_TOKEN}")
  endif()
endforeach()

message(STATUS
  "UVM platform contract: two governed releases, filesystem-neutral source "
  "paths, x64 C ABI/cdecl, POSIX and Windows 6-GiB process limits, 1200-second "
  "stage limits, and 7200-second serial matrices are present")
