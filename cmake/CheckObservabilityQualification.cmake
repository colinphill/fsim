# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_require_tokens relative_path)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${relative_path}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "observability qualification owner is missing: ${relative_path}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "observability qualification owner ${relative_path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_tokens(
  tests/app/application_test_cli.cpp
  "fsim::app::run_debug_repl("
  "compiled_debug_output.str() == transcript"
  "set_signal_change_hook("
  "const auto debug_trace = directory / \"debug-select.vcd\""
  "assert(!debug_vcd.empty());")

fsim_require_tokens(
  tests/app/trace_observation_application_test.cpp
  "void test_atomic_correlated_fanout()"
  "FstWriter fst(fst_bytes);"
  "VcdWriter vcd(vcd_text);"
  "TraceRegion::Callback, \"uvm:phase\", uvm_values"
  "std::vector<std::uint64_t>({ 1U, 2U, 3U, 4U, 5U, 6U, 7U })"
  "assert(recorder.callback_failures() == 1U);"
  "void test_reentrant_callback_containment()"
  "void test_systemc_repeated_dirty_phase()")

fsim_require_tokens(
  tests/runtime/fst_writer_tests.cpp
  "void test_ordered_changes_and_validation()"
  "assert(write_change_container() == bytes);"
  "FST change count exceeds its limit"
  "FstWriterCompression::Deterministic"
  "assert(deterministic != stored);"
  "assert(deterministic_stored < deterministic_bits);")

fsim_require_tokens(
  tests/runtime/transaction_record_test.cpp
  "repeated == bytes"
  "decoded == value"
  "transaction_record_precedes(value, later)"
  "TransactionObjectDomain::systemc"
  "TransactionObjectDomain::tlm1"
  "TransactionObjectDomain::tlm2"
  "FSIM-SCV-T003")

fsim_require_tokens(
  tests/app/scv_recording_application_test.cpp
  "std::ranges::sort(records, transaction_record_precedes);"
  "direct_receipt.bytes == loopback_receipt.bytes"
  "direct.drain() == loopback.drain()"
  "resource_metrics->backpressure_events > 0U")

fsim_require_tokens(
  tests/scv/scv_recording_test.cpp
  "registry.native_callback_count() >= 9U"
  "first_record.attributes.front().name == \"accepted\""
  "second_record.relations[0].name == \"follows\""
  "registry.set_recording(false, diagnostics)"
  "registry.records().size() == completed_count")

fsim_require_tokens(
  tests/app/scv_trace_application_test.cpp
  "assert(envelope.sequence > callback_sequence);"
  "service.callback_failures() == 1U"
  "inspected->waveforms[0].format == ScvWaveformFormat::vcd"
  "ScvWaveformFormat::fst"
  "ScvTraceSubmitStatus::backpressure"
  "closed.size() == 1U && bounded.closed()")

fsim_require_tokens(
  tests/app/mixed_conversion_application_test.cpp
  "assert(reference.debug_local == compiled.debug_local);"
  "assert(reference.vcd == compiled.vcd);"
  "assert(edited.keys != cold.keys);"
  "assert(edited.native_cache.misses > 0);"
  "assert(edited.native_cache.stores == edited.native_cache.misses);")

fsim_require_tokens(
  tests/CMakeLists.txt
  "fsim.application.trace_observation"
  "fsim.application.scv_trace"
  "fsim.runtime.transaction_record"
  "fsim.application.scv_recording"
  "fsim.scv.recording"
  "fsim.application.mixed_conversions"
  "NAME fsim.observability-qualification"
  "CheckObservabilityQualification.cmake")

fsim_require_tokens(
  tests/runtime/CMakeLists.txt
  "NAME fsim.runtime.fst_writer"
  "COMMAND fsim_fst_writer_tests")

message(STATUS
  "observability qualification passed: debugger callback VCD FST and SCV transaction order/correlation/edit owners are registered")
