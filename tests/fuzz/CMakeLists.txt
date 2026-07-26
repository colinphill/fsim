# SPDX-License-Identifier: Apache-2.0

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "FSIM_BUILD_FUZZERS requires Clang and libFuzzer")
endif()

# Recompile the complete frontend with coverage instrumentation. Keeping this
# separate from fsim_frontend prevents fuzzer-only objects and runtime
# requirements from leaking into the normal CLI and unit-test executables.
add_library(fsim_frontend_fuzz_subject STATIC ${FSIM_FRONTEND_SOURCES})
fsim_configure_library(fsim_frontend_fuzz_subject)
target_compile_options(
  fsim_frontend_fuzz_subject
  PRIVATE
    -fno-omit-frame-pointer
    -fno-sanitize-recover=undefined
    -fsanitize=fuzzer-no-link,address,undefined
)

add_executable(fsim_frontend_fuzz frontend_fuzz.cpp)
target_compile_features(fsim_frontend_fuzz PRIVATE cxx_std_20)
target_include_directories(
  fsim_frontend_fuzz
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(fsim_frontend_fuzz PRIVATE fsim_frontend_fuzz_subject)
fsim_enable_warnings(fsim_frontend_fuzz)
target_compile_options(
  fsim_frontend_fuzz
  PRIVATE
    -fno-omit-frame-pointer
    -fno-sanitize-recover=undefined
    -fsanitize=fuzzer,address,undefined
)
target_link_options(
  fsim_frontend_fuzz
  PRIVATE
    -fsanitize=fuzzer,address,undefined
)
