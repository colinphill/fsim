// SPDX-License-Identifier: Apache-2.0
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/vpi_plugin.hpp"
#include "fsim/runtime/vpi_bridge.h"
#include "fsim/runtime/vpi_system.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>

extern "C" std::size_t fsim_vpi_abi_c_host_size();
extern "C" const char* fsim_vpi_abi_c_bind_symbol();
extern "C" const char* fsim_vpi_abi_c_startup_symbol();
extern "C" void fsim_vpi_abi_c_report(const fsim_vpi_host_v1* host);

namespace fsim::tests::runtime {

namespace {

struct ReportCapture {
  std::string code;
  std::string message;
  fsim_vpi_error_severity_v1 severity{};
};

struct RoutineCapture {
  std::uint32_t calls{};
  std::uint32_t operation{};
  std::uint32_t flags{};
  std::uint32_t text_size{};
  fsim_vpi_handle_v1 handle{};
  std::uint64_t argument{};
  std::uint64_t user_data{};
  std::string text;
  fsim_vpi_status_v1 status{FSIM_VPI_STATUS_OK};
  bool malformed{};
  bool throw_exception{};
};

void FSIM_VPI_CALL capture_report(
    void* const context, const fsim_vpi_error_view_v1* const error) {
  auto& capture = *static_cast<ReportCapture*>(context);
  capture.code.assign(error->code, error->code_size);
  capture.message.assign(error->message, error->message_size);
  capture.severity = error->severity;
}

fsim_vpi_status_v1 FSIM_VPI_CALL capture_routine(
    void* const context,
    const fsim_vpi_service_request_v1* const request,
    fsim_vpi_service_result_v1* const result) {
  auto& capture = *static_cast<RoutineCapture*>(context);
  ++capture.calls;
  capture.operation = request->operation;
  capture.flags = request->flags;
  capture.text_size = request->text_size;
  capture.handle = request->handle;
  capture.argument = request->argument;
  capture.user_data = request->user_data;
  capture.text.assign(
      request->text == nullptr ? "" : request->text, request->text_size);
  if (capture.throw_exception) {
    throw std::runtime_error("routine callback exception");
  }
  result->struct_size = static_cast<std::uint32_t>(sizeof(*result));
  result->status = capture.status;
  result->reserved = capture.malformed ? 1U : 0U;
  result->handle = UINT64_C(0x1234);
  result->value = request->argument + 1U;
  return capture.status;
}

fsim_vpi_status_v1 FSIM_VPI_BRIDGE_CALL capture_standard_routine(
    void* const context,
    const std::uint32_t routine,
    const fsim_vpi_service_request_v1* const request,
    fsim_vpi_service_result_v1* const result) {
  if (context == nullptr || request == nullptr || result == nullptr) {
    return FSIM_VPI_STATUS_INVALID_ARGUMENT;
  }
  const auto& host = *static_cast<const fsim_vpi_host_v2*>(context);
  const auto invoked = fsim::runtime::invoke_systemverilog_vpi_2023_routine(
      host, static_cast<fsim::runtime::SystemVerilogVpiRoutineKind>(routine),
      *request);
  *result = invoked.value;
  return invoked ? FSIM_VPI_STATUS_OK : FSIM_VPI_STATUS_INTERNAL_ERROR;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void test_systemverilog_vpi_host_abi() {
  using fsim::runtime::SystemVerilogVpiAbiError;
  using fsim::runtime::SystemVerilogVpiPluginEntryKind;
  using fsim::runtime::SystemVerilogVpiPluginError;
  using fsim::runtime::SystemVerilogVpiRoutineError;
  using fsim::runtime::SystemVerilogVpiRoutineKind;
  using fsim::runtime::find_systemverilog_vpi_2023_routine;
  using fsim::runtime::invoke_systemverilog_vpi_2023_routine;
  using fsim::runtime::load_systemverilog_vpi_plugin;
  using fsim::runtime::make_systemverilog_vpi_host;
  using fsim::runtime::make_systemverilog_vpi_host_v2;
  using fsim::runtime::systemverilog_vpi_2023_routines;
  using fsim::runtime::validate_systemverilog_vpi_host;

  ReportCapture capture;
  const auto host = make_systemverilog_vpi_host(91, &capture, capture_report);
  require(
      validate_systemverilog_vpi_host(host)
          == SystemVerilogVpiAbiError::None,
      "VPI host v1 accepts the complete native ABI");
  require(
      host.struct_size == fsim_vpi_abi_c_host_size(),
      "VPI host v1 has identical C and C++ layout");
  require(
      std::strcmp(
          fsim_vpi_abi_c_bind_symbol(), "fsim_vpi_plugin_bind_v1") == 0,
      "VPI C ABI publishes exactly one versioned bind symbol");
  require(
      std::strcmp(
          fsim_vpi_abi_c_startup_symbol(), "vlog_startup_routines") == 0,
      "VPI C ABI retains the standardized startup-table symbol");
  fsim_vpi_abi_c_report(&host);
  require(
      capture.code == "FSIM-VPI-ABI-TEST"
          && capture.message == "C translation unit callback"
          && capture.severity == FSIM_VPI_ERROR_NOTICE,
      "VPI host report callback preserves bounded C strings and severity");

  const auto routines = systemverilog_vpi_2023_routines();
  std::unordered_set<std::string_view> routine_names;
  for (std::size_t index = 0; index < routines.size(); ++index) {
    require(
        static_cast<std::size_t>(routines[index].kind) == index
            && !routines[index].name.empty()
            && routine_names.emplace(routines[index].name).second
            && find_systemverilog_vpi_2023_routine(routines[index].kind)
                == &routines[index]
            && find_systemverilog_vpi_2023_routine(routines[index].name)
                == &routines[index],
        "VPI 2023 routine inventory is complete unique and index-stable");
  }
  require(
      routines.size() == 42U
          && find_systemverilog_vpi_2023_routine(
                 static_cast<SystemVerilogVpiRoutineKind>(999U))
              == nullptr
          && find_systemverilog_vpi_2023_routine("vpi_vendor_extension")
              == nullptr,
      "VPI routine inventory rejects unknown standard and vendor names");

  require(
      fsim_vpi_current_call_context_v1() == nullptr
          && vpi_handle_by_name(
                 const_cast<PLI_BYTE8*>("top.outside"), nullptr)
              == nullptr,
      "standard VPI wrappers reject calls outside an entered context");

  RoutineCapture routine_capture;
  auto routine_host = make_systemverilog_vpi_host_v2(
      92U, &capture, capture_report, &routine_capture, capture_routine);
  const std::string hierarchy_name{"top.u"};
  fsim_vpi_service_request_v1 routine_request{
      static_cast<std::uint32_t>(sizeof(fsim_vpi_service_request_v1)),
      FSIM_VPI_SERVICE_HIERARCHY,
      0U,
      static_cast<std::uint32_t>(hierarchy_name.size()),
      0U,
      41U,
      0U,
      hierarchy_name.data(),
  };
  const auto routine_result = invoke_systemverilog_vpi_2023_routine(
      routine_host, SystemVerilogVpiRoutineKind::HandleByName,
      routine_request);
  require(
      routine_result && routine_capture.calls == 1U
          && routine_capture.operation == FSIM_VPI_SERVICE_HIERARCHY
          && routine_result.value.handle == UINT64_C(0x1234)
          && routine_result.value.value == 42U,
      "VPI routine bridge validates and dispatches a complete sized request");

  fsim_vpi_call_context_v1 standard_context{
      FSIM_VPI_CONTEXT_ABI_VERSION,
      static_cast<std::uint32_t>(sizeof(fsim_vpi_call_context_v1)),
      &routine_host,
      capture_standard_routine,
  };
  auto invalid_standard_context = standard_context;
  invalid_standard_context.invoke = nullptr;
  require(
      fsim_vpi_call_context_enter_v1(&invalid_standard_context) == -1
          && fsim_vpi_call_context_enter_v1(&standard_context) == 0
          && fsim_vpi_current_call_context_v1() == &standard_context,
      "standard VPI call context validates and publishes one complete frame");

  auto* const standard_handle = vpi_handle_by_name(
      const_cast<PLI_BYTE8*>("top.standard"), nullptr);
  require(
      standard_handle == reinterpret_cast<vpiHandle>(UINT64_C(0x1234))
          && routine_capture.text == "top.standard"
          && routine_capture.operation == FSIM_VPI_SERVICE_HIERARCHY,
      "standard hierarchy wrapper marshals text through the checked dispatcher");
  require(
      vpi_get(vpiSize, standard_handle) == vpiSize + 1
          && routine_capture.handle == UINT64_C(0x1234)
          && routine_capture.argument == vpiSize,
      "standard property wrapper preserves handle and property identities");

  s_vpi_arrayvalue array_value{};
  PLI_INT32 array_index{};
  vpi_get_value_array(standard_handle, &array_value, &array_index, 1U);
  require(
      routine_capture.flags == 1U
          && routine_capture.operation == FSIM_VPI_SERVICE_VALUE
          && routine_capture.user_data != 0U,
      "standard array-value wrapper uses the value dispatcher extension lane");
  auto assertion_callback = +[](PLI_INT32, p_vpi_time, vpiHandle,
                                p_vpi_attempt_info, PLI_BYTE8*) -> PLI_INT32 {
    return 0;
  };
  require(
      vpi_register_assertion_cb(
          standard_handle, cbAssertionStart, assertion_callback, nullptr)
              == reinterpret_cast<vpiHandle>(UINT64_C(0x1234))
          && routine_capture.flags == 2U
          && routine_capture.operation == FSIM_VPI_SERVICE_CALLBACK,
      "assertion wrapper shares the checked callback registration seam");
  require(
      vpi_load_init(standard_handle, nullptr, 2) == 3
          && routine_capture.flags == 5U
          && routine_capture.handle == UINT64_C(0x1234),
      "data-reader wrapper preserves extension operation and hierarchy owner");
  require(
      fsim_vpi_call_context_leave_v1(&invalid_standard_context) == -1
          && fsim_vpi_current_call_context_v1() == &standard_context
          && fsim_vpi_call_context_leave_v1(&standard_context) == 0
          && fsim_vpi_current_call_context_v1() == nullptr,
      "standard VPI call context rejects mismatched leave and clears exactly once");
  const auto calls_after_leave = routine_capture.calls;
  require(
      vpi_get(vpiSize, standard_handle) == vpiUndefined
          && routine_capture.calls == calls_after_leave,
      "standard VPI wrappers never cross the host boundary after context leave");

  const auto calls_before_rejection = routine_capture.calls;
  auto wrong_family = routine_request;
  wrong_family.operation = FSIM_VPI_SERVICE_VALUE;
  require(
      invoke_systemverilog_vpi_2023_routine(
          routine_host, SystemVerilogVpiRoutineKind::HandleByName,
          wrong_family).error == SystemVerilogVpiRoutineError::InvalidRequest
          && routine_capture.calls == calls_before_rejection
          && capture.code == "FSIM-VPI-ROUTINE-003",
      "VPI routine bridge rejects a mismatched service before callback entry");
  require(
      invoke_systemverilog_vpi_2023_routine(
          routine_host, SystemVerilogVpiRoutineKind::Scan,
          routine_request).error == SystemVerilogVpiRoutineError::InvalidHandle
          && routine_capture.calls == calls_before_rejection
          && capture.code == "FSIM-VPI-ROUTINE-004",
      "VPI routine bridge enforces required handle policy before dispatch");

  routine_capture.status = FSIM_VPI_STATUS_STALE_HANDLE;
  require(
      invoke_systemverilog_vpi_2023_routine(
          routine_host, SystemVerilogVpiRoutineKind::HandleByName,
          routine_request).error == SystemVerilogVpiRoutineError::CallbackFailure
          && capture.code == "FSIM-VPI-ROUTINE-006",
      "VPI routine bridge retains callback status failure as a checked error");
  routine_capture.status = FSIM_VPI_STATUS_OK;
  routine_capture.malformed = true;
  require(
      invoke_systemverilog_vpi_2023_routine(
          routine_host, SystemVerilogVpiRoutineKind::HandleByName,
          routine_request).error == SystemVerilogVpiRoutineError::MalformedResult
          && capture.code == "FSIM-VPI-ROUTINE-007",
      "VPI routine bridge rejects malformed callback results");
  routine_capture.malformed = false;
  routine_capture.throw_exception = true;
  require(
      invoke_systemverilog_vpi_2023_routine(
          routine_host, SystemVerilogVpiRoutineKind::HandleByName,
          routine_request).error == SystemVerilogVpiRoutineError::CallbackException
          && capture.code == "FSIM-VPI-ROUTINE-005",
      "VPI routine bridge contains foreign callback exceptions");
  routine_capture.throw_exception = false;

  const auto invalid_routine = invoke_systemverilog_vpi_2023_routine(
      routine_host, static_cast<SystemVerilogVpiRoutineKind>(999U),
      routine_request);
  require(
      invalid_routine.error == SystemVerilogVpiRoutineError::InvalidRoutine
          && capture.code == "FSIM-VPI-ROUTINE-001",
      "VPI routine bridge reports an unknown selector through the host channel");
  auto invalid_routine_host = routine_host;
  invalid_routine_host.invoke_service = nullptr;
  require(
      invoke_systemverilog_vpi_2023_routine(
          invalid_routine_host, SystemVerilogVpiRoutineKind::HandleByName,
          routine_request).error == SystemVerilogVpiRoutineError::InvalidHost
          && capture.code == "FSIM-VPI-ROUTINE-002",
      "VPI routine bridge rejects an incomplete host before callback entry");

  const auto expect = [&](const auto mutate,
                          const SystemVerilogVpiAbiError expected,
                          const char* const message) {
    auto invalid = host;
    mutate(invalid);
    require(validate_systemverilog_vpi_host(invalid) == expected, message);
  };
  expect(
      [](auto& value) {
        value.abi_version = FSIM_VPI_HOST_ABI_VERSION_V2 + 1U;
      },
      SystemVerilogVpiAbiError::AbiVersion,
      "VPI host rejects an unknown ABI version");
  expect(
      [](auto& value) { --value.struct_size; },
      SystemVerilogVpiAbiError::AbiSize,
      "VPI host rejects a truncated table");
  expect(
      [](auto& value) { value.pointer_bits = 32; },
      SystemVerilogVpiAbiError::AbiPointerWidth,
      "VPI host rejects a foreign pointer width");
  expect(
      [](auto& value) { value.flags = 1; },
      SystemVerilogVpiAbiError::AbiFlags,
      "VPI host rejects reserved flags");
  expect(
      [](auto& value) { value.simulation_identity = 0; },
      SystemVerilogVpiAbiError::SimulationIdentity,
      "VPI host rejects the unowned simulation identity");
  expect(
      [](auto& value) { value.context = nullptr; },
      SystemVerilogVpiAbiError::HostContext,
      "VPI host rejects a missing context");
  expect(
      [](auto& value) { value.report = nullptr; },
      SystemVerilogVpiAbiError::ReportCallback,
      "VPI host rejects a missing report callback");

  std::string error;
  auto probe = fsim::platform::DynamicLibrary::open(
      FSIM_VPI_TEST_PLUGIN_PATH, error);
  require(probe != nullptr && error.empty(), "VPI reference image opens");
  using Reset = void(FSIM_VPI_CALL*)(int);
  using Count = int(FSIM_VPI_CALL*)();
  const auto reset = reinterpret_cast<Reset>(
      probe->symbol("fsim_vpi_test_reset", error));
  const auto startup_count = reinterpret_cast<Count>(
      probe->symbol("fsim_vpi_test_startup_count", error));
  const auto shutdown_count = reinterpret_cast<Count>(
      probe->symbol("fsim_vpi_test_shutdown_count", error));
  require(
      reset != nullptr && startup_count != nullptr && shutdown_count != nullptr
          && error.empty(),
      "VPI reference image exposes its lifecycle probes");

  reset(0);
  {
    const auto loaded = load_systemverilog_vpi_plugin(
        FSIM_VPI_TEST_PLUGIN_PATH, host);
    require(
        loaded && loaded.value->name() == "fsim-vpi-test"
            && loaded.value->entry_kind()
                == SystemVerilogVpiPluginEntryKind::DirectV3
            && loaded.value->startup_routine_count() == 1U
            && loaded.value->path()
                == std::filesystem::path{FSIM_VPI_TEST_PLUGIN_PATH}
                       .lexically_normal(),
        "VPI loader publishes a complete validated plug-in");
    require(
        startup_count() == 1 && shutdown_count() == 0,
        "VPI loader runs startup once before publication");
    const auto shutdown = loaded.value->shutdown();
    require(
        shutdown && shutdown_count() == 1,
        "VPI explicit shutdown succeeds exactly once");
    const auto repeated = loaded.value->shutdown();
    require(
        repeated && shutdown_count() == 1,
        "VPI repeated shutdown returns the retained result without re-entry");
  }
  require(shutdown_count() == 1, "VPI destruction does not repeat shutdown");

  reset(0);
  {
    const auto loaded = load_systemverilog_vpi_plugin(
        FSIM_VPI_TEST_PLUGIN_PATH, host);
    require(loaded && startup_count() == 1, "VPI automatic teardown setup");
  }
  require(
      shutdown_count() == 1,
      "VPI destruction performs shutdown before unloading the image");

  {
    const auto loaded = load_systemverilog_vpi_plugin(
        FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH, host);
    require(
        loaded
            && loaded.value->entry_kind()
                == SystemVerilogVpiPluginEntryKind::StandardStartupTable
            && loaded.value->startup_routine_count() == 2U
            && loaded.value->name()
                == std::filesystem::path{
                    FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH}.stem().string(),
        "VPI loader accepts the standard null-terminated startup table");
    std::string standard_error;
    auto standard_probe = fsim::platform::DynamicLibrary::open(
        FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH, standard_error);
    using StandardCount = unsigned(FSIM_VPI_CALL*)();
    const auto standard_count = reinterpret_cast<StandardCount>(
        standard_probe->symbol(
            "fsim_vpi_standard_startup_count", standard_error));
    require(
        standard_probe && standard_count != nullptr
            && standard_error.empty() && standard_count() == 3U
            && loaded.value->shutdown(),
      "standard VPI startup routines execute in table order without a custom shutdown ABI");
  }

  routine_capture.calls = 0U;
  {
    const auto loaded = load_systemverilog_vpi_plugin(
        FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH, routine_host.v1);
    require(
        loaded && routine_capture.calls == 1U
            && routine_capture.operation == FSIM_VPI_SERVICE_LIFECYCLE,
        "standard startup routines call normative wrappers through the entered v2 host context");
    std::string standard_error;
    auto standard_probe = fsim::platform::DynamicLibrary::open(
        FSIM_VPI_STANDARD_STARTUP_PLUGIN_PATH, standard_error);
    using StandardContextResult = PLI_INT32(FSIM_VPI_CALL*)();
    const auto standard_context_result =
        reinterpret_cast<StandardContextResult>(standard_probe->symbol(
            "fsim_vpi_standard_context_result", standard_error));
    require(
        standard_probe && standard_context_result != nullptr
            && standard_error.empty() && standard_context_result() == 1,
        "standard startup context returns the checked host service result");
  }

  const auto expect_load_error = [&](const int mode,
                                     const SystemVerilogVpiPluginError expected,
                                     const int expected_startups,
                                     const char* const message) {
    reset(mode);
    const auto result = load_systemverilog_vpi_plugin(
        FSIM_VPI_TEST_PLUGIN_PATH, host);
    require(
        !result && result.error == expected
            && startup_count() == expected_startups
            && shutdown_count() == 0,
        message);
  };
  expect_load_error(
      1, SystemVerilogVpiPluginError::BindFailure, 0,
      "VPI loader contains bind status failure transactionally");
  expect_load_error(
      2, SystemVerilogVpiPluginError::BindException, 0,
      "VPI loader contains bind exceptions transactionally");
  expect_load_error(
      3, SystemVerilogVpiPluginError::PluginAbiVersion, 0,
      "VPI loader rejects a foreign plug-in ABI version");
  expect_load_error(
      4, SystemVerilogVpiPluginError::PluginAbiSize, 0,
      "VPI loader rejects a truncated plug-in descriptor");
  expect_load_error(
      5, SystemVerilogVpiPluginError::PluginAbiFlags, 0,
      "VPI loader rejects reserved plug-in flags");
  expect_load_error(
      6, SystemVerilogVpiPluginError::PluginName, 0,
      "VPI loader rejects a missing bounded plug-in name");
  expect_load_error(
      7, SystemVerilogVpiPluginError::StartupFailure, 1,
      "VPI loader contains startup status failure without shutdown");
  expect_load_error(
      8, SystemVerilogVpiPluginError::StartupException, 1,
      "VPI loader contains startup exceptions without shutdown");
  expect_load_error(
      11, SystemVerilogVpiPluginError::MissingLifecycle, 0,
      "VPI loader requires the complete lifecycle profile");

  reset(9);
  {
    const auto loaded = load_systemverilog_vpi_plugin(
        FSIM_VPI_TEST_PLUGIN_PATH, host);
    require(static_cast<bool>(loaded), "VPI shutdown status fixture loads");
    const auto shutdown = loaded.value->shutdown();
    const auto repeated = loaded.value->shutdown();
    require(
        !shutdown
            && shutdown.error == SystemVerilogVpiPluginError::ShutdownFailure
            && repeated.error == shutdown.error && shutdown_count() == 1,
        "VPI shutdown status failure is retained and invoked once");
  }
  reset(10);
  {
    const auto loaded = load_systemverilog_vpi_plugin(
        FSIM_VPI_TEST_PLUGIN_PATH, host);
    require(static_cast<bool>(loaded), "VPI shutdown exception fixture loads");
    const auto shutdown = loaded.value->shutdown();
    require(
        !shutdown
            && shutdown.error == SystemVerilogVpiPluginError::ShutdownException
            && shutdown_count() == 1,
        "VPI shutdown exceptions are contained exactly once");
  }

  const auto missing_bind = load_systemverilog_vpi_plugin(
      FSIM_DPI_TEST_PLUGIN_PATH, host);
  require(
      !missing_bind
          && missing_bind.error
              == SystemVerilogVpiPluginError::MissingBindSymbol,
      "VPI loader rejects an image without a direct bind or startup table");
  auto invalid_host = host;
  invalid_host.simulation_identity = 0;
  const auto host_failure = load_systemverilog_vpi_plugin(
      FSIM_VPI_TEST_PLUGIN_PATH, invalid_host);
  require(
      !host_failure
          && host_failure.error == SystemVerilogVpiPluginError::HostAbi,
      "VPI loader validates host ownership before opening an image");
  auto truncated_host = host;
  --truncated_host.struct_size;
  const auto truncated_host_failure = load_systemverilog_vpi_plugin(
      std::filesystem::path{FSIM_VPI_TEST_PLUGIN_PATH}.concat(
          ".unopened-truncated-host"),
      truncated_host);
  require(
      !truncated_host_failure
          && truncated_host_failure.error
              == SystemVerilogVpiPluginError::HostAbi,
      "VPI loader rejects a one-byte host truncation before opening an image");
  const auto missing_artifact = load_systemverilog_vpi_plugin(
      std::filesystem::path{FSIM_VPI_TEST_PLUGIN_PATH}.concat(".missing"),
      host);
  require(
      !missing_artifact
          && missing_artifact.error
              == SystemVerilogVpiPluginError::ArtifactOpen,
      "VPI loader reports artifact-open failure without partial state");
}

}  // namespace fsim::tests::runtime
