// SPDX-License-Identifier: Apache-2.0
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/vpi_plugin.hpp"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

extern "C" std::size_t fsim_vpi_abi_c_host_size();
extern "C" void fsim_vpi_abi_c_report(const fsim_vpi_host_v1* host);

namespace fsim::tests::runtime {

namespace {

struct ReportCapture {
  std::string code;
  std::string message;
  fsim_vpi_error_severity_v1 severity{};
};

void FSIM_VPI_CALL capture_report(
    void* const context, const fsim_vpi_error_view_v1* const error) {
  auto& capture = *static_cast<ReportCapture*>(context);
  capture.code.assign(error->code, error->code_size);
  capture.message.assign(error->message, error->message_size);
  capture.severity = error->severity;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void test_systemverilog_vpi_host_abi() {
  using fsim::runtime::SystemVerilogVpiAbiError;
  using fsim::runtime::SystemVerilogVpiPluginError;
  using fsim::runtime::load_systemverilog_vpi_plugin;
  using fsim::runtime::make_systemverilog_vpi_host;
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
  fsim_vpi_abi_c_report(&host);
  require(
      capture.code == "FSIM-VPI-ABI-TEST"
          && capture.message == "C translation unit callback"
          && capture.severity == FSIM_VPI_ERROR_NOTICE,
      "VPI host report callback preserves bounded C strings and severity");

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
      "VPI loader rejects an image without the exact bind symbol");
  auto invalid_host = host;
  invalid_host.simulation_identity = 0;
  const auto host_failure = load_systemverilog_vpi_plugin(
      FSIM_VPI_TEST_PLUGIN_PATH, invalid_host);
  require(
      !host_failure
          && host_failure.error == SystemVerilogVpiPluginError::HostAbi,
      "VPI loader validates host ownership before opening an image");
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
