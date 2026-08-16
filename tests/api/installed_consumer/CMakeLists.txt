# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)
project(fsim_core_api_consumer LANGUAGES C CXX)

find_package(fsim CONFIG REQUIRED)

add_executable(
  fsim_core_api_consumer
  ../api_abi_c_test.c
  ../api_abi_cpp_test.cpp
)
target_compile_features(
  fsim_core_api_consumer
  PRIVATE c_std_11 cxx_std_20
)
target_link_libraries(fsim_core_api_consumer PRIVATE fsim::api)

if(WIN32)
  add_custom_command(
    TARGET fsim_core_api_consumer
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:fsim::api>"
      "$<TARGET_FILE_DIR:fsim_core_api_consumer>"
    VERBATIM
  )
endif()

file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer-path-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:fsim_core_api_consumer>"
)
