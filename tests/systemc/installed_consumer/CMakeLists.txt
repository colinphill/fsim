# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)
project(fsim_systemc_installed_consumer LANGUAGES CXX)

find_package(SystemCLanguage 3.0.2.20251031 EXACT CONFIG REQUIRED)
find_package(SystemCTLM 2.0.6.20191203 EXACT CONFIG REQUIRED)

add_executable(fsim_systemc_installed_consumer main.cpp)
target_compile_features(fsim_systemc_installed_consumer PRIVATE cxx_std_20)
target_link_libraries(
  fsim_systemc_installed_consumer
  PRIVATE SystemC::systemc
)
file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/consumer-path-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:fsim_systemc_installed_consumer>")
