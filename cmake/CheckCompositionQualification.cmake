# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_require_tokens relative_path)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${relative_path}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "composition qualification owner is missing: ${relative_path}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "composition qualification owner ${relative_path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_tokens(
  tests/app/application_test_multiple_roots.cpp
  "multiple-root-application"
  "std::vector<std::string>{\"source\", \"sink\"}"
  "multiple-root-mixed-language"
  "{\"vhdl:work.vhdl_root(rtl)\", \"vhdl_side\"}"
  "{\"sv:work.sv_root\", \"sv_side\"}"
  "fsim::app::SimulationEngine::interpreter"
  "fsim::app::SimulationEngine::compiled"
  "reordered_project->cache_key != reference_key")

fsim_require_tokens(
  tests/app/application_test_mixed.cpp
  "fsim::project::Language::vhdl"
  "fsim::project::Language::system_verilog"
  "sv-to-vhdl-construction-actual-test"
  "vhdl-to-sv-construction-actual-test"
  "fsim::app::SimulationEngine::interpreter"
  "fsim::app::SimulationEngine::compiled")

fsim_require_tokens(
  tests/app/typed_boundary_application_test.cpp
  "fsim::project::Language::vhdl"
  "fsim::project::Language::system_verilog"
  "systemc_sources.language = fsim::project::Language::systemc"
  "systemc:models.boundary_native"
  "fsim::semantic::design::BoundaryKind::systemc_instance"
  "fsim::app::SimulationEngine::interpreter"
  "fsim::app::SimulationEngine::compiled"
  "assert(cold.project_cache_hit);"
  "assert(warm.project_cache_hit);")

fsim_require_tokens(
  tests/app/application_test_non_project_cli.cpp
  "\"compile\""
  "\"elaborate\""
  "\"simulate\""
  "serialize_runtime_state"
  "producer-hidden"
  "relocated artifacts"
  "read only design.fsimdesign"
  "relocated outputs"
  "relocated-consumer-cache"
  "relocated-consumer-files"
  "bytes.find(producer_prefix) == std::string::npos")

fsim_require_tokens(
  tests/app/application_test_artifact_verilog.cpp
  "checkpoint"
  "relocated_interpreted"
  "relocated_compiled"
  "provenance"
  "cache.hits")

fsim_require_tokens(
  tests/library/library_artifact_test.cpp
  "source-hidden.fsimlib"
  "producer-absolute"
  "identity_relocation_diagnostics"
  "missing_relocation_diagnostics"
  "assert(fsim::library::relocate_unit_sources("
  "assert(!fsim::library::relocate_unit_sources(")

fsim_require_tokens(
  tests/CMakeLists.txt
  "fsim.application.core_mixed"
  "fsim.application.core_multiple_roots"
  "fsim.application.core_non_project_cli"
  "fsim.application.artifact_phases"
  "fsim.application.mixed_conversions"
  "fsim.application.typed_boundaries"
  "fsim.library.artifact"
  "fsim.source-hidden-relocation-contract"
  "fsim.non-project-restartability-contract"
  "NAME fsim.composition-qualification"
  "CheckCompositionQualification.cmake")

message(STATUS
  "composition qualification passed: multiple-root mixed-language project/non-project artifact and identity-preserving relocation owners are registered")
