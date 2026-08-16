# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_scv_require_tokens path)
  if(NOT EXISTS "${FSIM_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "SCV portability owner is missing: ${path}")
  endif()
  file(READ "${FSIM_SOURCE_DIR}/${path}" contents)
  foreach(token IN LISTS ARGN)
    string(FIND "${contents}" "${token}" offset)
    if(offset EQUAL -1)
      message(FATAL_ERROR "SCV portability owner ${path} lost token: ${token}")
    endif()
  endforeach()
endfunction()

fsim_scv_require_tokens(
  "include/fsim/systemc/scv_artifact.hpp"
  "scv_artifact_identity_limit = 4096"
  "validate_scv_artifact_compatibility")
fsim_scv_require_tokens(
  "src/systemc/scv_artifact.cpp"
  "FSIM-SCV-A001"
  "fsim_scv_accepts_compatibility_identity"
  "exceeds the 4096-byte limit")
fsim_scv_require_tokens(
  "include/fsim/library/artifact.hpp"
  "kFormatVersion = 5"
  "std::string scv_compatibility")
fsim_scv_require_tokens(
  "include/fsim/artifact/design.hpp"
  "kDesignFormatVersion = 11"
  "struct DesignSystemCPlugin"
  "std::string scv_compatibility")
fsim_scv_require_tokens(
  "src/app/application_library_import.cpp"
  "native.scv_compatibility != fsim_scv_compatibility_identity()"
  "mapped SystemC native plug-in SCV producer")
fsim_scv_require_tokens(
  "src/app/application_phase_design.cpp"
  "validate_scv_artifact_compatibility"
  "embedded .fsimdesign SystemC plug-in"
  "plugin_metadata->scv_compatibility")
fsim_scv_require_tokens(
  "tests/scv/scv_artifact_test.cpp"
  "relocated-"
  "future-scv-library"
  "FSIM-SCV-A001")
fsim_scv_require_tokens(
  "tests/app/application_test_non_project_cli.cpp"
  "relocated-"
  "stale_scv_design"
  "FSIM-SCV-A001")
fsim_scv_require_tokens(
  "tests/app/application_test_cli.cpp"
  "models-systemc-stale-scv.fsimlib"
  "scv_compatibility"
  "FSIM-LIB-0008"
  "stale_cache_has_file")
fsim_scv_require_tokens(
  "docs/diagnostics.md"
  "FSIM-SCV-C001"
  "FSIM-SCV-A001")

fsim_scv_require_tokens(
  "include/fsim/systemc/scv_backend_protocol.hpp"
  "ScvIslandId"
  "ScvHierarchyId"
  "ScvObjectId"
  "ScvStreamId"
  "ScvGeneratorId"
  "ScvTransactionId"
  "scv_backend_message_header_bytes = 160")
fsim_scv_require_tokens(
  "src/systemc/scv_backend_protocol.cpp"
  "fsim-scv-backend-identity-v1"
  "FSIM-SCV-B001"
  "FSIM-SCV-B002"
  "FSIM-SCV-B003"
  "scv_backend_message_precedes")
foreach(path IN ITEMS
    "include/fsim/systemc/scv_backend_protocol.hpp"
    "src/systemc/scv_backend_protocol.cpp"
    "include/fsim/systemc/scv_random.hpp"
    "src/systemc/scv_random.cpp"
    "include/fsim/systemc/scv_recording.hpp"
    "include/fsim/systemc/scv_backend_transport.hpp"
    "include/fsim/systemc/scv_resources.hpp")
  file(READ "${FSIM_SOURCE_DIR}/${path}" protocol_text)
  foreach(forbidden IN ITEMS
      "#include <scv"
      "scv_smart_ptr"
      "scv_tr_handle"
      "sc_core::"
      "void*"
      "uintptr_t")
    string(FIND "${protocol_text}" "${forbidden}" forbidden_offset)
    if(NOT forbidden_offset EQUAL -1)
      message(FATAL_ERROR
        "SCV pointer-free protocol ${path} contains forbidden token: ${forbidden}")
    endif()
  endforeach()
endforeach()
fsim_scv_require_tokens(
  "include/fsim/systemc/scv_constraints.hpp"
  "ScvConstraintVariable"
  "ScvConstraintClause"
  "ScvConstraintDistribution"
  "solve_scv_constraints")
fsim_scv_require_tokens(
  "src/systemc/scv_constraints.cpp"
  "SystemVerilogConstraintSolver"
  "FSIM-SCV-Q001"
  "FSIM-SCV-Q002"
  "FSIM-SCV-Q003"
  "rolled back")
fsim_scv_require_tokens(
  "include/fsim/systemc/scv_extensions.hpp"
  "ScvExtensionNode"
  "range_left"
  "aval_words"
  "bval_words"
  "capture_scv_extensions")
fsim_scv_require_tokens(
  "src/systemc/scv_extensions.cpp"
  "FSIM-SCV-X001"
  "FSIM-SCV-X002"
  "FSIM-SCV-X003"
  "max_value_bits"
  "max_total_words"
  "max_total_string_bytes")
fsim_scv_require_tokens(
  "include/fsim/runtime/transaction_record.hpp"
  "transaction_record_schema_version = 1U"
  "TransactionStableId"
  "TransactionTypedValue"
  "TransactionCorrelatedObject"
  "serialize_transaction_record")
fsim_scv_require_tokens(
  "src/runtime/transaction_record.cpp"
  "FSIM-SCV-T001"
  "FSIM-SCV-T002"
  "FSIM-SCV-T003"
  "transaction_record_precedes")
fsim_scv_require_tokens(
  "include/fsim/systemc/scv_recording.hpp"
  "ScvNativeRecordingRegistry"
  "ScvNativeRecordingLimits"
  "take_records")
fsim_scv_require_tokens(
  "src/systemc/scv_recording.cpp"
  "scv_tr_db"
  "scv_tr_stream"
  "scv_tr_generator"
  "register_class_cb"
  "register_record_attribute_cb"
  "register_relation_cb"
  "FSIM-SCV-N001"
  "FSIM-SCV-N002"
  "FSIM-SCV-N003")
fsim_scv_require_tokens(
  "src/app/application_scv_trace.hpp"
  "ScvTraceCorrelationService"
  "ScvWaveformFormat"
  "ScvTraceSubmitStatus"
  "max_pending_records")
fsim_scv_require_tokens(
  "src/app/application_scv_trace.cpp"
  "TransactionObjectDomain::systemc"
  "TransactionObjectDomain::tlm1"
  "TransactionObjectDomain::tlm2"
  "FSIM-SCV-L001"
  "FSIM-SCV-L002"
  "FSIM-SCV-L003"
  "bounded backpressure")
fsim_scv_require_tokens(
  "include/fsim/systemc/scv_backend_transport.hpp"
  "scv_transport_schema_version = 1U"
  "ScvBackendRecordTransport"
  "merge_scv_transport_envelopes")
fsim_scv_require_tokens(
  "src/systemc/scv_backend_transport.cpp"
  "serialize_transaction_record"
  "deserialize_transaction_record"
  "ScvBackendTransportKind::direct"
  "ScvBackendTransportKind::worker_loopback"
  "FSIM-SCV-W001"
  "FSIM-SCV-W002"
  "FSIM-SCV-W003")
fsim_scv_require_tokens(
  "include/fsim/systemc/scv_resources.hpp"
  "ScvResourceLimits"
  "ScvResourceWorkload"
  "ScvResourceMetrics"
  "run_scv_resource_probe")
fsim_scv_require_tokens(
  "src/systemc/scv_resources.cpp"
  "recording_enabled"
  "ScvBackendTransportStatus::backpressure"
  "ScvConstraintStatus::resource_exhausted"
  "FSIM-SCV-E001"
  "FSIM-SCV-E002"
  "FSIM-SCV-E003"
  "output_hash")
fsim_scv_require_tokens(
  "tests/scv/scv_resource_test.cpp"
  "producer_failure_at"
  "consumer_failure_at"
  "backpressure_events"
  "output_digest")
fsim_scv_require_tokens(
  "tests/app/scv_recording_application_test.cpp"
  "run_scv_resource_probe"
  "resource_metrics")
fsim_scv_require_tokens(
  "cmake/RunScvClosure.cmake"
  "FSIM-SCV-CLOSURE-START"
  "SCV_RESOURCE_BASELINE"
  "release-sanitizer-ci=deferred-batch-177")
fsim_scv_require_tokens(
  "tests/scv/scv_closure_test.cmake"
  "FSIM_PRESERVED_COUNT EQUAL 18"
  "SCV-012"
  "Batch 175 planned restart checkpoint")
fsim_scv_require_tokens(
  "docs/v2-scv-release-audit.md"
  "official Accellera SCV 2.0.1"
  "pointer-free identities"
  "Release, sanitizer, and hosted CI"
  "Batch 177")
fsim_scv_require_tokens(
  "tests/CMakeLists.txt"
  "FSIM_SCV_OFFICIAL_EXAMPLES"
  "hello|general/hello/main.cpp"
  "introspection|extensions/introspection1/test.cpp"
  "randomization|randomization/ex_04_simplerand/test.cc"
  "transactions|transactions/overview/main.cpp"
  "fsim.scv.official_"
  "SC_ALLOW_DEPRECATED_IEEE_API"
  "/wd4100"
  "-Wno-unused-parameter")
fsim_scv_require_tokens(
  "tests/scv/scv_corpus_test.cpp"
  "derive_scv_random_seeds"
  "solve_scv_constraints"
  "capture_scv_extensions"
  "ScvNativeRecordingRegistry"
  "ScvBackendRecordTransport"
  "live_native_payloads")

fsim_scv_require_tokens(
  "include/fsim/systemc/scv_random.hpp"
  "derive_scv_random_seeds"
  "class ScvRandomStream"
  "class ScvRandomDistribution"
  "class ScvRandomBag"
  "ScvRandomBagSnapshot")
fsim_scv_require_tokens(
  "src/systemc/scv_random.cpp"
  "fsim-scv-random-v1"
  "FSIM-SCV-R001"
  "FSIM-SCV-R002"
  "FSIM-SCV-R003")
fsim_scv_require_tokens(
  "include/fsim/systemc/scv_smart_ptr.hpp"
  "class ScvNativeSmartPtrRegistry"
  "ScvNativeSmartPtrHandle"
  "ScvNativeExtensionInfo"
  "live_native_payloads")
fsim_scv_require_tokens(
  "src/systemc/scv_smart_ptr.cpp"
  "scv_smart_ptr<Value>"
  "FSIM-SCV-P001"
  "FSIM-SCV-P002"
  "FSIM-SCV-P003")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/scv_smart_ptr.hpp"
  smart_ptr_boundary)
foreach(forbidden IN ITEMS
    "#include <scv"
    "scv_smart_ptr"
    "scv_extensions"
    "sc_core::"
    "void*"
    "uintptr_t")
  string(FIND "${smart_ptr_boundary}" "${forbidden}" forbidden_offset)
  if(NOT forbidden_offset EQUAL -1)
    message(FATAL_ERROR
      "SCV native ownership boundary contains forbidden token: ${forbidden}")
  endif()
endforeach()

message(STATUS "SCV portability contract passed")
