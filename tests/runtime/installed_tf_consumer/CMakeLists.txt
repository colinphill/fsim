# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)
project(fsim_installed_tf_consumer LANGUAGES C)

find_package(fsim CONFIG REQUIRED)

add_executable(fsim_installed_tf_consumer consumer.c)
target_compile_features(fsim_installed_tf_consumer PRIVATE c_std_11)
target_link_libraries(fsim_installed_tf_consumer PRIVATE fsim::tf)

if(WIN32)
  add_custom_command(
    TARGET fsim_installed_tf_consumer
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fsim::tf>"
      "$<TARGET_FILE_DIR:fsim_installed_tf_consumer>"
    VERBATIM
  )
endif()

file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer-path-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:fsim_installed_tf_consumer>"
)
