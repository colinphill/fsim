# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_require_tokens path)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "execution-mode qualification owner is missing: ${path}")
  endif()
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "execution-mode qualification owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_tokens(
  "${FSIM_SOURCE_DIR}/tests/app/random_application_test.cpp"
  "config_for(directory, source, optimization, 123)"
  "changed_config.project.seed = 124;"
  "reference.values == compiled.values"
  "reference.output == compiled.output"
  "reference.values == repeated.values"
  "fsim::project::Optimization::o0"
  "fsim::project::Optimization::o2")

fsim_require_tokens(
  "${FSIM_SOURCE_DIR}/tests/app/coverage_application_test.cpp"
  "fsim::app::SimulationEngine::interpreter"
  "const auto cold = run_once("
  "const auto warm = run_once("
  "cold.native_cache.hits == 0"
  "cold.native_cache.misses == 1"
  "warm.native_cache.hits == 1"
  "reference.percentages == expected"
  "fsim::project::Optimization::o0, \"cache-o0\""
  "fsim::project::Optimization::o2, \"cache-o2\"")

fsim_require_tokens(
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp"
  "app::DebuggerControl debugger"
  "const auto interpreted = run_engine(app::SimulationEngine::interpreter);"
  "const auto compiled = run_engine(app::SimulationEngine::compiled);"
  "const auto compiled_warm = run_engine(app::SimulationEngine::compiled);"
  "assert(interpreted == compiled);"
  "assert(compiled == compiled_warm);"
  "replay_checkpoints[1] == replay_checkpoints[2]"
  "\"--seed\", \"23\"")

fsim_require_tokens(
  "${FSIM_SOURCE_DIR}/tests/app/application_test_cli.cpp"
  "\"fsim\", \"debug\""
  "fsim debugger: tb"
  "(O0 hybrid, 2 compiled process(es) in "
  "time 0, delta 0, scope tb")

fsim_require_tokens(
  "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt"
  "fsim.application.artifact_phases"
  "fsim.application.random"
  "fsim.application.coverage"
  "NAME fsim.execution-mode-qualification"
  "CheckExecutionModeQualification.cmake")

message(STATUS
  "execution-mode qualification passed: fixed-seed interpreter/O0/O2/debugger cold/warm equivalence owners are registered")
