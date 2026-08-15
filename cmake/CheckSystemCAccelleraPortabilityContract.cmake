# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_require_tokens relative_path)
  set(path "${FSIM_SOURCE_DIR}/${relative_path}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "SystemC compatibility input is missing: ${relative_path}")
  endif()
  file(READ "${path}" contents)
  foreach(token IN LISTS ARGN)
    string(FIND "${contents}" "${token}" token_index)
    if(token_index EQUAL -1)
      message(FATAL_ERROR
        "SystemC compatibility contract lost '${token}' in ${relative_path}")
    endif()
  endforeach()
endfunction()

function(fsim_forbid_tokens relative_path)
  set(path "${FSIM_SOURCE_DIR}/${relative_path}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "SystemC compatibility input is missing: ${relative_path}")
  endif()
  file(READ "${path}" contents)
  foreach(token IN LISTS ARGN)
    string(FIND "${contents}" "${token}" token_index)
    if(NOT token_index EQUAL -1)
      message(FATAL_ERROR
        "SystemC Accellera-only contract found '${token}' in ${relative_path}")
    endif()
  endforeach()
endfunction()

fsim_require_tokens(include/fsim/systemc.hpp
  "fsim/systemc/accellera.hpp"
  "fsim/systemc/incremental.hpp")
fsim_require_tokens(include/systemc "fsim/systemc.hpp")
foreach(path IN ITEMS
    include/fsim/systemc/core.hpp
    include/fsim/systemc/channels.hpp
    include/fsim/systemc/datatypes.hpp
    include/fsim/systemc/marshalling.hpp
    include/fsim/systemc/plugin.hpp
    src/systemc/systemc_api.cpp
    src/systemc/systemc_core.cpp
    src/systemc/systemc_foreign.cpp
    tests/systemc/systemc_header_test.cpp)
  if(EXISTS "${FSIM_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "legacy SystemC interface remains: ${path}")
  endif()
endforeach()
fsim_require_tokens(include/fsim/systemc/accellera.hpp
  "#include <systemc.h>"
  "FSIM_SYSTEMC_ACCELERA_VERSION \"3.0.2\""
  "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4"
  "FSIM_SYSTEMC_BRIDGE_REVISION 2u"
  "fsim_systemc_accellera_compatibility_identity"
  "backend_bindable_endpoint"
  "backend_port"
  "SC_FSIM_EXPORT_AS")
fsim_require_tokens(include/fsim/systemc_abi.h
  "FSIM_SYSTEMC_ABI_VERSION 3u")
fsim_forbid_tokens(include/fsim/systemc_abi.h
  "fsim_sc_module_factory_v1"
  "(*register_factory)("
  "mark_hdl_module"
  "set_hdl_module_actual"
  "set_hdl_module_implementation")
fsim_forbid_tokens(include/fsim/systemc/accellera.hpp
  "SC_FSIM_HDL_MODULE"
  "hdl_module_type"
  "class hdl_module")
fsim_forbid_tokens(include/fsim/systemc/hierarchy.hpp
  "ForeignChildDescription"
  "foreign_children")
fsim_forbid_tokens(include/fsim/elaboration/elaborator.hpp
  "ForeignChild"
  "ForeignPort"
  "foreign_children")
fsim_forbid_tokens(src/elaboration/hierarchy_systemc.cpp
  "systemc_foreign_target"
  "connect_foreign_child"
  "hdl_module")
fsim_require_tokens(cmake/FsimSystemCAccellera.cmake
  "fsim_systemc_apply_runtime_fixes"
  "delete m_process_table;"
  "delete m_cor_pkg;"
  "cannot apply the governed SystemC process teardown fix"
  "CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\""
  "set(MSVC TRUE)"
  "set(MSVC \"\${FSIM_SYSTEMC_PARENT_MSVC}\")"
  "!defined(__clang__)"
  "cannot apply the governed SystemC clang-cl template fix"
  "\"\${target}\" PRIVATE \"/FI\${patched_common_header}\" /W0"
  "target_compile_options(systemc PRIVATE /W0)")
fsim_require_tokens(.gitattributes
  "third_party/systemc-3.0.2/** -text")
fsim_require_tokens(cmake/CheckInstalledPublicContract.cmake
  "CMAKE_MSVC_RUNTIME_LIBRARY=\${FSIM_MSVC_RUNTIME_LIBRARY}"
  "FSIM_STAGED_CONSUMER_EXECUTABLE")
fsim_require_tokens(cmake/CheckSystemCSharedRuntime.cmake
  "DIRECTORIES \"\${bridge_directory}\" \"\${upstream_directory}\"")
fsim_require_tokens(cmake/CheckFetchedTclRelocation.cmake
  "FSIM_SYSTEMC_BRIDGE_LIBRARY"
  "FSIM_SYSTEMC_UPSTREAM_LIBRARY"
  "DESTINATION \"\${FSIM_STAGE_DIR}/bin\"")
fsim_require_tokens(tests/CMakeLists.txt
  "-DFSIM_MSVC_RUNTIME_LIBRARY=\${CMAKE_MSVC_RUNTIME_LIBRARY}"
  "fsim_configure_windows_test_runtime"
  "ENVIRONMENT_MODIFICATION"
  "PATH=path_list_prepend:$<TARGET_FILE_DIR:fsim_systemc_accellera_runtime>"
  "PATH=path_list_prepend:$<TARGET_FILE_DIR:\${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>"
  "-DFSIM_SYSTEMC_BRIDGE_LIBRARY=$<TARGET_FILE:fsim_systemc_accellera_runtime>"
  "-DFSIM_SYSTEMC_UPSTREAM_LIBRARY=$<TARGET_FILE:\${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>")
fsim_require_tokens(src/systemc/accellera_compatibility.cpp
  "_LIBCPP_VERSION"
  "_MSVC_STL_VERSION"
  "_MSVC_STL_UPDATE"
  "__GLIBCXX__"
  "_GLIBCXX_RELEASE"
  "|bridge="
  "|stdlib=")
fsim_require_tokens(src/systemc/accellera_runtime.cpp
  "#if !defined(_WIN32)"
  "sc_main(int, char*[])")
fsim_require_tokens(src/systemc/plugin_compiler.cpp
  "accellera-runtime"
  "accellera-compatibility"
  "plugin_export_library"
  "FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_PATH")
fsim_require_tokens(src/systemc/incremental_compiler.cpp
  "plugin_export_library"
  "FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_PATH"
  "cannot locate the fsim SystemC plug-in export library"
  "expected_compiler_fingerprint"
  "metadata->compiler_fingerprint != current_fingerprint"
  "producer identity is stale or incompatible")
fsim_require_tokens(src/app/application_library_import.cpp
  "native.compiler_fingerprint != *fingerprint"
  "native.systemc_abi != FSIM_SYSTEMC_ABI_VERSION")
fsim_require_tokens(src/app/application_phase_design.cpp
  "record.compiler_fingerprint != *current_fingerprint"
  "embedded SystemC plug-in producer identity is stale")
fsim_require_tokens(CMakeLists.txt
  "fsim_systemc_plugin_exports"
  "fsim::systemc_plugin_exports"
  "fsim_systemc_headers"
  "FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_PATH"
  "fsim_systemc_accellera_runtime"
  "FSIM_SYSTEMC_INTERNAL_RUNTIME_TARGET"
  "FSIM_SYSTEMC_DEFAULT_DEFINES"
  "TARGET_LINKER_FILE:\${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}"
  "/STACK:33554432"
  "WINDOWS_EXPORT_ALL_SYMBOLS ON")
foreach(path IN ITEMS
    include/fsim/systemc.hpp
    include/systemc
    src/systemc/plugin_compiler.cpp
    src/systemc/incremental_compiler.cpp
    CMakeLists.txt
    tests/CMakeLists.txt)
  fsim_forbid_tokens("${path}"
    "FSIM_SYSTEMC_LEGACY_COMPATIBILITY"
    "fsim_systemc_support"
    "FSIM_SYSTEMC_SUPPORT_LIBRARY_PATH"
    "FSIM_SYSTEMC_ACCELERA_SUPPORT_LIBRARY_PATH")
endforeach()
foreach(path IN ITEMS
    tests/systemc/accellera_systemc_corpus_test.cpp
    tests/systemc/accellera_upstream_sc_main.cpp
    tests/systemc/systemc_shared_runtime_test.cpp)
  fsim_forbid_tokens("${path}" "__declspec(dllexport)")
endforeach()
fsim_require_tokens(tests/systemc/systemc_compatibility_test.cpp
  "SC_VERSION_MAJOR == 3"
  "SC_VERSION_MINOR == 0"
  "SC_VERSION_PATCH == 2"
  "FSIM_SYSTEMC_ABI_VERSION == 3u"
  "unknown-stdlib"
  "wrong_bridge"
  "wrong_stdlib")
fsim_require_tokens(tests/systemc/incremental_compiler_test.cpp
  "stale.fsimscplugin"
  "compute_incremental_plugin_input_digest"
  "stale_plugin_diagnostics")
fsim_require_tokens(tests/app/application_test_cli.cpp
  "compiler_fingerprint ="
  "mapped-systemc-fallback")
fsim_require_tokens(tests/app/application_test_non_project_cli.cpp
  "stale-incremental.fsimdesign"
  "stale_producer_rejected")
fsim_require_tokens(include/fsim/systemc/kernel_backend_protocol.hpp
  "kSystemCKernelProtocolVersion = 1U"
  "kSystemCKernelMessageHeaderBytes = 128U"
  "SystemCIslandId"
  "SystemCHierarchyId"
  "SystemCObjectId"
  "SystemCEndpointId"
  "SystemCTransactionId"
  "SystemCSequenceId"
  "class SystemCKernelBackend"
  "SystemCKernelTransportResult exchange"
  "SystemCKernelProtocolLimits"
  "serialize_systemc_kernel_message"
  "deserialize_systemc_kernel_message")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_protocol.hpp"
  FSIM_BACKEND_PROTOCOL_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_PROTOCOL_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend protocol exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/kernel_backend_protocol.cpp
  "fsim-systemc-kernel-identity-v1"
  "FSIM-SC-B001"
  "FSIM-SC-B002"
  "FSIM-SC-B003"
  "append_u64"
  "reserved0 != 0U"
  "payload_size > limits.max_payload_bytes")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "0xcb1bf7d185be2025ULL"
  "3e36d03af2c6b98c86ce50e64b82e0831ed7e485e6375c51c009e572a61a72b9"
  "SystemCKernelOperation::observe_transaction"
  "oversized_diagnostics")
fsim_require_tokens(include/fsim/systemc/kernel_backend_session.hpp
  "kSystemCKernelSessionPayloadVersion = 1U"
  "SystemCKernelSessionLimits"
  "SystemCKernelSessionState"
  "SystemCKernelLifecycleCode"
  "SystemCKernelCreateSessionPayload"
  "SystemCKernelCreateObjectPayload"
  "SystemCKernelBindEndpointPayload"
  "SystemCKernelLifecycleReceipt"
  "make_systemc_kernel_session_backend"
  "systemc_kernel_backend_live_contexts")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_session.hpp"
  FSIM_BACKEND_SESSION_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_SESSION_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend session exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/kernel_backend_session.cpp
  "ContextActivation"
  "std::make_unique<sc_core::sc_simcontext>"
  "HierarchyRegistry::load"
  "bind_backend_interface"
  "context_->elaborate()"
  "context_->initialize(false)"
  "context_->end()"
  "FSIM-SC-S001"
  "FSIM-SC-S002"
  "FSIM-SC-S003"
  "FSIM-SC-S004")
fsim_require_tokens(src/systemc/context_activation.hpp
  "sc_curr_simcontext"
  "std::recursive_mutex")
fsim_require_tokens(tests/systemc/kernel_backend_session_test.cpp
  "systemc_kernel_lifecycle_diagnostic_code"
  "systemc_kernel_backend_live_contexts() == 2U"
  "SystemCKernelSessionState::quiescent"
  "SystemCKernelSessionState::failed"
  "bad-binding-session"
  "unbound-session"
  "repeat-create-session")
fsim_require_tokens(tests/systemc/kernel_backend_session_plugin.cpp
  "backend_input<std::uint32_t>"
  "before_end_of_elaboration"
  "start_of_simulation"
  "requested session factory failure")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_test_accellera_session_plugin"
  "fsim.systemc.kernel_backend_session")
fsim_require_tokens(include/fsim/systemc/kernel_backend_execution.hpp
  "kSystemCKernelExecutionPayloadVersion = 2U"
  "SystemCKernelExecutionLimits"
  "SystemCAccelleraRegion"
  "SystemCKernelAdvanceKind"
  "SystemCKernelExecutionStatus"
  "SystemCKernelExecutionOrder"
  "SystemCKernelScalarValue"
  "SystemCKernelExecutionReceipt"
  "serialize_systemc_execution_receipt"
  "deserialize_systemc_execution_receipt")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_execution.hpp"
  FSIM_BACKEND_EXECUTION_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_EXECUTION_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend execution protocol exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/kernel_backend_execution.cpp
  "systemc_kernel_apply_scalar"
  "systemc_kernel_advance_native"
  "sc_core::sc_start"
  "SC_RUN_TO_TIME"
  "pending_activity_at_current_time"
  "FSIM-SC-E001"
  "FSIM-SC-E002"
  "FSIM-SC-E003"
  "FSIM-SC-E004")
fsim_require_tokens(src/systemc/kernel_backend_session.cpp
  "SystemCKernelOperation::apply_inputs"
  "SystemCKernelOperation::advance"
  "SystemCKernelOperation::next_activity"
  "SystemCKernelOperation::drain_outputs"
  "SystemCKernelOperation::report"
  "SystemCKernelOperation::inspect"
  "SystemCKernelOperation::snapshot"
  "dirty_outputs_")
fsim_require_tokens(tests/systemc/kernel_backend_execution_test.cpp
  "SystemCKernelExecutionStatus::paused"
  "SystemCKernelExecutionStatus::stopped"
  "SystemCKernelExecutionStatus::error"
  "next_activity_time_fs == 10'000'000U"
  "systemc_kernel_backend_live_contexts() == 0U"
  "SystemCKernelExecutionCode::resource")
fsim_require_tokens(tests/systemc/kernel_backend_execution_plugin.cpp
  "backend_input<std::uint32_t>"
  "backend_output<std::uint32_t>"
  "sc_core::sc_pause()"
  "sc_core::sc_stop()"
  "requested execution failure")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_test_accellera_execution_plugin"
  "fsim.systemc.kernel_backend_execution")
fsim_require_tokens(include/fsim/systemc/kernel_backend_loopback.hpp
  "kSystemCKernelLoopbackRevision = 1U"
  "SystemCKernelLoopbackLimits"
  "SystemCKernelLoopbackStats"
  "SystemCKernelLoopbackBackend"
  "make_systemc_kernel_loopback_backend"
  "make_systemc_kernel_loopback_session_backend"
  "systemc_kernel_loopback_live_transports")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_loopback.hpp"
  FSIM_BACKEND_LOOPBACK_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_LOOPBACK_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend loopback exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/kernel_backend_loopback.cpp
  "deserialize_systemc_kernel_message"
  "response.header.correlation == request.header.sequence"
  "SystemCKernelMessageFlag::replayable"
  "max_forwarded_exchanges"
  "contain_peer_failure"
  "FSIM-SC-L001"
  "FSIM-SC-L002"
  "FSIM-SC-L003"
  "FSIM-SC-L004")
fsim_require_tokens(tests/systemc/kernel_backend_loopback_test.cpp
  "alternate_response_sequence"
  "wrong_correlation"
  "stats().replayed == 1U"
  "stats().evicted == 1U"
  "stats().disconnected == 1U"
  "systemc_kernel_backend_live_contexts() == 2U"
  "systemc_kernel_backend_live_contexts() == 0U"
  "audit_real_session_equivalence")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "response.header.correlation = request.header.sequence")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_kernel_backend_loopback_tests"
  "fsim.systemc.kernel_backend_loopback")
fsim_require_tokens(include/fsim/systemc/kernel_backend_synchronization.hpp
  "kSystemCKernelSynchronizationRevision = 1U"
  "SystemCKernelHostLanguage"
  "SystemCKernelCrossingStage"
  "SystemCKernelSynchronizationPoint"
  "SystemCKernelSynchronizationInput"
  "SystemCKernelSynchronizationReceipt"
  "SystemCKernelSynchronizer"
  "make_systemc_kernel_synchronizer")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_synchronization.hpp"
  FSIM_BACKEND_SYNCHRONIZATION_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_SYNCHRONIZATION_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend synchronization exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/kernel_backend_synchronization.cpp
  "SystemCKernelAdvanceKind::time"
  "SystemCKernelAdvanceKind::delta"
  "SystemCKernelCrossingStage::kernel_arrival"
  "SystemCKernelCrossingStage::kernel_quiescent"
  "std::ranges::sort(result.dirty_outputs"
  "fail_locked"
  "FSIM-SC-N001"
  "FSIM-SC-N002"
  "FSIM-SC-N003"
  "FSIM-SC-N004")
file(READ
  "${FSIM_SOURCE_DIR}/src/systemc/kernel_backend_synchronization.cpp"
  FSIM_BACKEND_SYNCHRONIZATION_SOURCE)
string(FIND "${FSIM_BACKEND_SYNCHRONIZATION_SOURCE}"
  "Scheduler" FSIM_SECOND_SCHEDULER_INDEX)
if(NOT FSIM_SECOND_SCHEDULER_INDEX EQUAL -1)
  message(FATAL_ERROR
    "SystemC backend synchronization must not implement or depend on a second scheduler")
endif()
fsim_require_tokens(tests/systemc/kernel_backend_synchronization_test.cpp
  "SystemCKernelHostLanguage::system_verilog"
  "SystemCKernelHostLanguage::vhdl"
  "SystemCKernelCrossingStage::kernel_arrival"
  "SystemCAccelleraRegion::update"
  "systemc_kernel_backend_live_contexts() == 2U"
  "systemc_kernel_backend_live_contexts() == 0U"
  "next_activity_time_fs == 10'000'000U"
  "FSIM-SC-N004")
fsim_require_tokens(tests/app/application_test_systemc_scheduling.cpp
  "sv_reference"
  "vhdl_reference"
  "test_systemc_scheduling_matrix")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_kernel_backend_synchronization_tests"
  "fsim.systemc.kernel_backend_synchronization")
fsim_require_tokens(include/fsim/systemc/kernel_backend_value_codec.hpp
  "kSystemCKernelValueCodecVersion = 1U"
  "SystemCKernelValueKind"
  "SystemCKernelValueRange"
  "SystemCKernelValueLimits"
  "serialize_systemc_kernel_value"
  "deserialize_systemc_kernel_value"
  "project_systemc_kernel_scalar_value")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_value_codec.hpp"
  FSIM_BACKEND_VALUE_CODEC_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_VALUE_CODEC_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend value codec exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(include/fsim/systemc/kernel_backend_value_endpoint.hpp
  "backend_value_endpoint"
  "backend_value_input"
  "backend_value_output"
  "sc_dt::sc_bv"
  "sc_dt::sc_lv")
fsim_require_tokens(src/systemc/kernel_backend_value_codec.cpp
  "kMagic"
  "valid_logic9_planes"
  "valid_enum_metadata"
  "max_encoded_bytes"
  "FSIM-SC-V001"
  "FSIM-SC-V002"
  "FSIM-SC-V003"
  "FSIM-SC-V004")
fsim_require_tokens(src/systemc/kernel_backend_execution.cpp
  "serialize_systemc_kernel_value"
  "deserialize_systemc_kernel_value"
  "systemc_kernel_apply_value"
  "systemc_kernel_sample_value")
fsim_require_tokens(tests/systemc/kernel_backend_value_codec_test.cpp
  "make_bit2"
  "make_logic4"
  "make_logic9"
  "make_enumeration"
  "make_time"
  "audit_payload_rejections")
fsim_require_tokens(tests/systemc/kernel_backend_execution_test.cpp
  "run_typed_loopback_execution"
  "wide_logic4_value"
  "wide_bit2_value"
  "value.typed")
fsim_require_tokens(tests/systemc/kernel_backend_value_codec_plugin.cpp
  "backend_value_input<sc_dt::sc_lv<257>>"
  "backend_value_output<sc_dt::sc_lv<257>>"
  "backend_value_input<sc_dt::sc_bv<129>>"
  "backend_value_output<sc_dt::sc_bv<129>>")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "SystemCKernelValueKind::logic9"
  "serialize_systemc_apply_inputs_payload"
  "decoded_crossing")
fsim_require_tokens(tests/app/typed_boundary_application_test.cpp
  "verify_crossing_codec"
  "serialize_systemc_kernel_value"
  "deserialize_systemc_kernel_value")
fsim_require_tokens(tests/artifact/design_artifact_test.cpp
  "systemc-backend-values-v1"
  "state/systemc-values.bin"
  "deserialize_systemc_kernel_value")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_kernel_backend_value_codec_tests"
  "fsim.systemc.kernel_backend_value_codec"
  "fsim_test_accellera_value_codec_plugin")
fsim_require_tokens(include/fsim/systemc/kernel_backend_tlm1.hpp
  "kSystemCKernelTlm1Version = 1U"
  "SystemCKernelTlm1InterfaceKind"
  "SystemCKernelTlm1Operation"
  "SystemCKernelTlm1State"
  "SystemCKernelTlm1Endpoint"
  "SystemCKernelTlm1Transaction"
  "SystemCKernelTlm1Registry"
  "serialize_systemc_kernel_tlm1_transaction"
  "deserialize_systemc_kernel_tlm1_transaction")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_tlm1.hpp"
  FSIM_BACKEND_TLM1_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_TLM1_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend TLM1 protocol exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/tlm1_backend.cpp
  "supports_operation"
  "Native in-island SystemC TLM1 traffic must not be serialized"
  "connected endpoints disagree on bridge ownership"
  "FSIM-SC-T001"
  "FSIM-SC-T002"
  "FSIM-SC-T003"
  "FSIM-SC-T004")
fsim_require_tokens(tests/systemc/tlm1_backend_test.cpp
  "tlm::tlm_fifo<std::uint32_t>"
  "tlm::tlm_transport_if"
  "tlm::tlm_analysis_port"
  "put->nb_put(11U)"
  "get_peek->nb_peek"
  "SystemCKernelTlm1State::blocked"
  "mismatched_bridge"
  "one_way")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "serialize_systemc_kernel_tlm1_transaction"
  "deserialize_systemc_kernel_tlm1_transaction")
fsim_require_tokens(tests/app/systemc_tlm_application_test.cpp
  "fsim/systemc/accellera.hpp"
  "tlm::tlm_fifo<std::uint32_t>"
  "result.status == fsim::runtime::RunStatus::completed"
  "result.time == 5U")
fsim_require_tokens(tests/artifact/design_artifact_test.cpp
  "systemc-native-tlm1-v1"
  "state/systemc-tlm1.bin"
  "deserialize_systemc_kernel_tlm1_transaction")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_tlm1_backend_tests"
  "fsim.systemc.tlm1_backend"
  "fsim.application.systemc_tlm1")
fsim_require_tokens(include/fsim/systemc/kernel_backend_tlm2.hpp
  "kSystemCKernelTlm2Version = 1U"
  "SystemCKernelTlm2SocketKind"
  "SystemCKernelTlm2Operation"
  "SystemCKernelTlm2Phase"
  "SystemCKernelTlm2Dmi"
  "SystemCKernelTlm2Extension"
  "SystemCKernelTlm2Registry"
  "serialize_systemc_kernel_tlm2_transaction"
  "deserialize_systemc_kernel_tlm2_transaction")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_tlm2.hpp"
  FSIM_BACKEND_TLM2_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_TLM2_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC backend TLM2 protocol exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/tlm2_backend.cpp
  "compatible_direction"
  "Native in-island SystemC TLM2 traffic must not be serialized"
  "socket direction, width, or bridge ownership differs"
  "FSIM-SC-U001"
  "FSIM-SC-U002"
  "FSIM-SC-U003"
  "FSIM-SC-U004")
fsim_require_tokens(tests/systemc/tlm2_backend_test.cpp
  "simple_initiator_socket"
  "simple_target_socket"
  "nb_transport_fw"
  "get_direct_mem_ptr"
  "transport_dbg"
  "tlm_quantumkeeper"
  "NativeExtension"
  "serialize_systemc_kernel_tlm2_transaction")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "serialize_systemc_kernel_tlm2_transaction"
  "deserialize_systemc_kernel_tlm2_transaction")
fsim_require_tokens(tests/app/systemc_tlm_application_test.cpp
  "tlm_utils::simple_initiator_socket"
  "tlm_utils::simple_target_socket"
  "tlm_utils::tlm_quantumkeeper"
  "get_direct_mem_ptr"
  "transport_dbg")
fsim_require_tokens(tests/artifact/design_artifact_test.cpp
  "systemc-native-tlm2-v1"
  "state/systemc-tlm2.bin"
  "deserialize_systemc_kernel_tlm2_transaction")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_tlm2_backend_tests"
  "fsim.systemc.tlm2_backend"
  "tlm2")
fsim_require_tokens(include/fsim/systemc/kernel_backend_inventory.hpp
  "kSystemCKernelInventoryVersion = 1U"
  "SystemCKernelChannelKind"
  "SystemCKernelWriterPolicy"
  "SystemCKernelUpdateOwner"
  "SystemCKernelObservationMode"
  "SystemCKernelChannelInventory"
  "serialize_systemc_kernel_channel_inventory"
  "deserialize_systemc_kernel_channel_inventory")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_inventory.hpp"
  FSIM_BACKEND_INVENTORY_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_INVENTORY_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC channel inventory protocol exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(include/fsim/systemc/kernel_backend_inventory_accellera.hpp
  "sc_core::sc_signal"
  "sc_core::sc_buffer"
  "sc_core::sc_clock"
  "sc_core::sc_signal_resolved"
  "sc_core::sc_signal_rv"
  "sc_core::sc_mutex"
  "sc_core::sc_semaphore"
  "sc_core::sc_event_queue"
  "describe_accellera_channel")
fsim_require_tokens(src/systemc/kernel_backend_inventory.cpp
  "A frozen SystemC channel disappeared or changed after binding"
  "Unsupported SystemC channel metadata must remain explicitly visible"
  "FSIM-SC-W001"
  "FSIM-SC-W002"
  "FSIM-SC-W003"
  "FSIM-SC-W004")
fsim_require_tokens(tests/systemc/kernel_backend_inventory_test.cpp
  "sc_core::sc_signal<std::int32_t>"
  "sc_core::sc_buffer<sc_dt::sc_logic>"
  "sc_core::sc_clock"
  "sc_core::sc_signal_resolved"
  "sc_core::sc_signal_rv<8>"
  "CustomChannel"
  "validate_live_snapshot")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "serialize_systemc_kernel_channel_inventory"
  "deserialize_systemc_kernel_channel_inventory")
fsim_require_tokens(tests/app/trace_hierarchy_application_test.cpp
  "test_systemc_channel_inventory_path"
  "SystemCKernelChannelInventory")
fsim_require_tokens(tests/artifact/design_artifact_test.cpp
  "systemc-channel-inventory-v1"
  "state/systemc-channels.bin"
  "deserialize_systemc_kernel_channel_inventory")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_kernel_backend_inventory_tests"
  "fsim.systemc.kernel_backend_inventory")
fsim_require_tokens(include/fsim/systemc/kernel_backend_binding_inventory.hpp
  "kSystemCKernelBindingInventoryVersion = 1U"
  "SystemCKernelBindingKind"
  "SystemCKernelBindingDirection"
  "SystemCKernelBindingTarget"
  "SystemCKernelBindingInventory"
  "serialize_systemc_kernel_binding_inventory"
  "deserialize_systemc_kernel_binding_inventory")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_binding_inventory.hpp"
  FSIM_BACKEND_BINDING_INVENTORY_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_BINDING_INVENTORY_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC binding inventory protocol exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(include/fsim/systemc/kernel_backend_binding_inventory_accellera.hpp
  "sc_core::sc_in"
  "sc_core::sc_out"
  "sc_core::sc_inout"
  "sc_core::sc_port"
  "sc_core::sc_export"
  "describe_accellera_binding")
fsim_require_tokens(src/systemc/kernel_backend_binding_inventory.cpp
  "complete chain is invalid"
  "binding does not resolve to an inventoried channel"
  "FSIM-SC-X001"
  "FSIM-SC-X002"
  "FSIM-SC-X003"
  "FSIM-SC-X004")
fsim_require_tokens(tests/systemc/kernel_backend_binding_inventory_test.cpp
  "sc_core::sc_in<int>"
  "sc_core::sc_out<int>"
  "sc_core::sc_export"
  "SC_ZERO_OR_MORE_BOUND"
  "SystemCKernelHostLanguage::vhdl"
  "child.input(input)")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "serialize_systemc_kernel_binding_inventory"
  "deserialize_systemc_kernel_binding_inventory")
fsim_require_tokens(tests/app/trace_hierarchy_application_test.cpp
  "SystemCKernelBindingInventory"
  "mixed_signal_alias")
fsim_require_tokens(tests/artifact/design_artifact_test.cpp
  "systemc-binding-inventory-v1"
  "state/systemc-bindings.bin"
  "deserialize_systemc_kernel_binding_inventory")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_kernel_backend_binding_inventory_tests"
  "fsim.systemc.kernel_backend_binding_inventory")
fsim_require_tokens(src/app/application_systemc_trace.hpp
  "SystemCTracePipeline"
  "AccelleraSystemCTraceHook"
  "value_changed_event"
  "capture_attempts")
fsim_require_tokens(src/app/application_systemc_trace.cpp
  "systemc:late-enable-snapshot"
  "systemc:post-update-dirty"
  "maximum_pending_batches"
  "vcd.begin_checkpoint(\"dumpall\")"
  "FSIM-SC-Z001"
  "FSIM-SC-Z002"
  "FSIM-SC-Z003"
  "FSIM-SC-Z004")
fsim_require_tokens(tests/app/systemc_trace_application_test.cpp
  "sc_core::sc_buffer<sc_dt::sc_lv<257>>"
  "capture_attempts() == 2U"
  "backpressure_events == 1U"
  "wide_values[1] == wide_values[2]")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "lossless repeated SystemC trace dirty batch"
  "repeated_trace == trace_payload")
fsim_require_tokens(tests/app/trace_observation_application_test.cpp
  "test_systemc_repeated_dirty_phase"
  "systemc:post-update-dirty")
fsim_require_tokens(tests/artifact/design_artifact_test.cpp
  "systemc-trace-dirty-v1"
  "state/systemc-trace.fst"
  "trace.trace->values[1].payload")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_trace_application_tests"
  "fsim.application.systemc_trace")
fsim_require_tokens(include/fsim/systemc/kernel_backend_observation.hpp
  "kSystemCKernelObservationVersion = 1U"
  "SystemCKernelObservationKind"
  "SystemCKernelChannelObservationAdapter"
  "SystemCKernelSafePointObserver"
  "serialize_systemc_kernel_observation_batch"
  "deserialize_systemc_kernel_observation_batch")
file(READ
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_observation.hpp"
  FSIM_BACKEND_OBSERVATION_HEADER)
foreach(FSIM_FORBIDDEN_TOKEN IN ITEMS
    "sc_core::" "sc_simcontext" "tlm::" "void*" "uintptr_t"
    "coroutine_handle" "std::thread")
  string(FIND "${FSIM_BACKEND_OBSERVATION_HEADER}"
    "${FSIM_FORBIDDEN_TOKEN}" FSIM_FORBIDDEN_INDEX)
  if(NOT FSIM_FORBIDDEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemC observation protocol exposes forbidden runtime token: ${FSIM_FORBIDDEN_TOKEN}")
  endif()
endforeach()
fsim_require_tokens(src/systemc/kernel_backend_observation.cpp
  "permitted only at an island safe point"
  "visible but has no debugger read adapter"
  "native in-island TLM1 transaction"
  "native in-island TLM2 transaction"
  "FSIM-SC-Y001"
  "FSIM-SC-Y002"
  "FSIM-SC-Y003"
  "FSIM-SC-Y004")
fsim_require_tokens(tests/systemc/kernel_backend_observation_test.cpp
  "test_safe_point_and_custom_adapter"
  "SystemCAccelleraRegion::evaluate"
  "SystemCKernelObservationKind::tlm1_begin"
  "SystemCKernelObservationKind::tlm2_phase"
  "SystemCKernelObservationKind::tlm2_dmi"
  "SystemCKernelObservationKind::tlm2_debug"
  "deserialize_systemc_kernel_observation_batch")
fsim_require_tokens(tests/systemc/kernel_backend_protocol_test.cpp
  "serialize_systemc_kernel_observation_batch"
  "decoded_observations->records.front().transaction")
fsim_require_tokens(tests/app/systemc_tlm_application_test.cpp
  "verify_observation"
  "SystemCKernelObservationKind::tlm1_end"
  "SystemCKernelObservationKind::tlm2_dmi"
  "SystemCKernelObservationKind::tlm2_debug")
fsim_require_tokens(tests/artifact/design_artifact_test.cpp
  "systemc-observation-v1"
  "state/systemc-observations.bin"
  "deserialize_systemc_kernel_observation_batch")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim_systemc_kernel_backend_observation_tests"
  "fsim.systemc.kernel_backend_observation")
fsim_require_tokens(docs/diagnostics.md
  "producer fingerprint over the exact"
  "bridge revision"
  "FSIM-SC-B001"
  "FSIM-SC-B002"
  "FSIM-SC-B003"
  "FSIM-SC-S001"
  "FSIM-SC-S002"
  "FSIM-SC-S003"
  "FSIM-SC-S004"
  "FSIM-SC-E001"
  "FSIM-SC-E002"
  "FSIM-SC-E003"
  "FSIM-SC-E004"
  "FSIM-SC-L001"
  "FSIM-SC-L002"
  "FSIM-SC-L003"
  "FSIM-SC-L004"
  "FSIM-SC-U001"
  "FSIM-SC-U002"
  "FSIM-SC-U003"
  "FSIM-SC-U004"
  "FSIM-SC-W001"
  "FSIM-SC-W002"
  "FSIM-SC-W003"
  "FSIM-SC-W004"
  "FSIM-SC-X001"
  "FSIM-SC-X002"
  "FSIM-SC-X003"
  "FSIM-SC-X004"
  "FSIM-SC-Y001"
  "FSIM-SC-Y002"
  "FSIM-SC-Y003"
  "FSIM-SC-Y004"
  "FSIM-SC-Z001"
  "FSIM-SC-Z002"
  "FSIM-SC-Z003"
  "FSIM-SC-Z004"
  "FSIM-SC-N001"
  "FSIM-SC-N002"
  "FSIM-SC-N003"
  "FSIM-SC-N004"
  "FSIM-SC-V001"
  "FSIM-SC-V002"
  "FSIM-SC-V003"
  "FSIM-SC-V004"
  "FSIM-SC-T001"
  "FSIM-SC-T002"
  "FSIM-SC-T003"
  "FSIM-SC-T004")
fsim_require_tokens(tests/CMakeLists.txt
  "fsim.systemc-accellera-portability-contract"
  "CheckSystemCAccelleraPortabilityContract.cmake")

message(STATUS
  "SystemC Accellera portability contract passed: official public adapter, "
  "ABI 2, upstream/compiler/stdlib/bridge identities, stale object/plugin/"
  "mapped-library/design rejection, one-context session lifecycle/rollback, "
  "bounded ordered execution protocol, serialized loopback replay/containment, "
  "multi-island safe-point synchronization without a second scheduler, "
  "arbitrary-width typed value codecs and native vector adapters, native "
  "co-located TLM1/TLM2, bounded safe-point observation and lossless post-update trace dirty hooks, install "
  "target, and focused evidence are present")
