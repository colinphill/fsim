# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_LLVM_TEST_EXECUTABLE)
  message(FATAL_ERROR "FSIM_LLVM_TEST_EXECUTABLE is required")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    FSIM_PROFILE_LLVM_MODULES=1
    "${FSIM_LLVM_TEST_EXECUTABLE}" --wide-bitwise-profile
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "wide bitwise profile test failed (${result})\n${stdout}\n${stderr}")
endif()

set(profile "${stdout}\n${stderr}")
string(REPLACE "\n" ";" profile_lines "${profile}")
set(shape_256 "")
set(shape_1024 "")
foreach(line IN LISTS profile_lines)
  if(line MATCHES "process_ids=876([^0-9]|$)")
    set(shape_256 "${line}")
  elseif(line MATCHES "process_ids=1644([^0-9]|$)")
    set(shape_1024 "${line}")
  endif()
endforeach()

foreach(shape IN ITEMS shape_256 shape_1024)
  if("${${shape}}" STREQUAL "")
    message(FATAL_ERROR
      "wide bitwise census omitted ${shape}\n${profile}")
  endif()
  foreach(opcode IN ITEMS and or xor not)
    if(NOT "${${shape}}" MATCHES "raw_vector_${opcode}=[1-9][0-9]*")
      message(FATAL_ERROR
        "${shape} did not lower ${opcode} through 4xi64 vector IR\n${${shape}}")
    endif()
  endforeach()
  if("${${shape}}" MATCHES "raw_wide_(and|or|xor)=[1-9][0-9]*")
    message(FATAL_ERROR
      "${shape} retained whole-width integer bitwise operations\n${${shape}}")
  endif()
endforeach()
