// SPDX-License-Identifier: Apache-2.0
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/vhpi_plugin.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

extern "C" std::size_t fsim_vhpi_abi_c_host_size();
extern "C" const char* fsim_vhpi_abi_c_bind_symbol();
extern "C" void fsim_vhpi_abi_c_report(const fsim_vhpi_host_v1* host);

namespace fsim::tests::runtime {

namespace {

struct VhpiReportCapture {
  std::string code;
  std::string message;
  fsim_vhpi_error_severity_v1 severity{};
};

void FSIM_VHPI_CALL capture_vhpi_report(
    void* const context, const fsim_vhpi_error_view_v1* const error) {
  auto& capture = *static_cast<VhpiReportCapture*>(context);
  capture.code.assign(error->code, error->code_size);
  capture.message.assign(error->message, error->message_size);
  capture.severity = error->severity;
}

fsim_vhpi_status_v1 FSIM_VHPI_CALL vhpi_lifecycle(void*) {
  return FSIM_VHPI_STATUS_OK;
}

void require_vhpi(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void test_vhdl_vhpi_host_abi() {
  using fsim::runtime::VhdlVhpiAbiError;
  using fsim::runtime::VhdlVhpiPluginError;
  using fsim::runtime::make_vhdl_vhpi_host;
  using fsim::runtime::validate_vhdl_vhpi_host;
  using fsim::runtime::validate_vhdl_vhpi_plugin_descriptor;

  static_assert(sizeof(fsim_vhpi_handle_v1) == sizeof(std::uint64_t));
  static_assert(sizeof(fsim_vhpi_host_v1) == 40);
  static_assert(sizeof(fsim_vhpi_plugin_v1) == 48);

  VhpiReportCapture capture;
  const auto host = make_vhdl_vhpi_host(158, &capture, capture_vhpi_report);
  require_vhpi(
      validate_vhdl_vhpi_host(host) == VhdlVhpiAbiError::None,
      "VHPI host accepts the complete native ABI");
  require_vhpi(
      host.struct_size == fsim_vhpi_abi_c_host_size(),
      "VHPI host has identical C and C++ layout");
  require_vhpi(
      std::strcmp(
          fsim_vhpi_abi_c_bind_symbol(), "fsim_vhpi_plugin_bind_v1") == 0,
      "VHPI C ABI publishes exactly one versioned bind symbol");
  fsim_vhpi_abi_c_report(&host);
  require_vhpi(
      capture.code == "FSIM-VHPI-ABI-TEST"
          && capture.message == "C translation unit callback"
          && capture.severity == FSIM_VHPI_ERROR_NOTE,
      "VHPI report callback preserves bounded C strings and severity");

  const auto expect_host = [&](const auto mutate,
                               const VhdlVhpiAbiError expected,
                               const char* const message) {
    auto invalid = host;
    mutate(invalid);
    require_vhpi(validate_vhdl_vhpi_host(invalid) == expected, message);
  };
  expect_host(
      [](auto& value) {
        value.abi_version = FSIM_VHPI_HOST_ABI_VERSION_V2 + 1U;
      },
      VhdlVhpiAbiError::AbiVersion,
      "VHPI host rejects an unknown ABI version");
  expect_host(
      [](auto& value) { --value.struct_size; },
      VhdlVhpiAbiError::AbiSize,
      "VHPI host rejects a truncated table");
  expect_host(
      [](auto& value) { value.pointer_bits = 32; },
      VhdlVhpiAbiError::AbiPointerWidth,
      "VHPI host rejects a foreign pointer width");
  expect_host(
      [](auto& value) { value.flags = 1; },
      VhdlVhpiAbiError::AbiFlags,
      "VHPI host rejects reserved flags");
  expect_host(
      [](auto& value) { value.simulation_identity = 0; },
      VhdlVhpiAbiError::SimulationIdentity,
      "VHPI host rejects an unowned simulation identity");
  expect_host(
      [](auto& value) { value.context = nullptr; },
      VhdlVhpiAbiError::HostContext,
      "VHPI host rejects a missing report context");
  expect_host(
      [](auto& value) { value.report = nullptr; },
      VhdlVhpiAbiError::ReportCallback,
      "VHPI host rejects a missing report callback");

  const char plugin_name[] = "fsim-vhpi-test";
  const auto plugin = fsim_vhpi_plugin_v1{
      FSIM_VHPI_PLUGIN_ABI_VERSION,
      static_cast<std::uint32_t>(sizeof(fsim_vhpi_plugin_v1)),
      0,
      static_cast<std::uint32_t>(sizeof(plugin_name) - 1),
      plugin_name,
      &capture,
      vhpi_lifecycle,
      vhpi_lifecycle,
  };
  require_vhpi(
      validate_vhdl_vhpi_plugin_descriptor(plugin)
          == VhdlVhpiPluginError::None,
      "VHPI plug-in descriptor accepts the complete native ABI");

  const auto expect_plugin = [&](const auto mutate,
                                 const VhdlVhpiPluginError expected,
                                 const char* const message) {
    auto invalid = plugin;
    mutate(invalid);
    require_vhpi(
        validate_vhdl_vhpi_plugin_descriptor(invalid) == expected,
        message);
  };
  expect_plugin(
      [](auto& value) { ++value.abi_version; },
      VhdlVhpiPluginError::PluginAbiVersion,
      "VHPI plug-in rejects an unknown ABI version");
  expect_plugin(
      [](auto& value) { --value.struct_size; },
      VhdlVhpiPluginError::PluginAbiSize,
      "VHPI plug-in rejects a truncated descriptor");
  expect_plugin(
      [](auto& value) { value.flags = 1; },
      VhdlVhpiPluginError::PluginAbiFlags,
      "VHPI plug-in rejects reserved flags");
  expect_plugin(
      [](auto& value) { value.name = nullptr; },
      VhdlVhpiPluginError::PluginName,
      "VHPI plug-in rejects a missing bounded name");
  expect_plugin(
      [](auto& value) { value.name_size = 0; },
      VhdlVhpiPluginError::PluginName,
      "VHPI plug-in rejects an empty bounded name");
  expect_plugin(
      [](auto& value) { value.name_size = 4097; },
      VhdlVhpiPluginError::PluginName,
      "VHPI plug-in rejects an excessive bounded name");
  const char embedded_nul_name[] = {'v', 'h', '\0', 'p', 'i'};
  auto invalid_name = plugin;
  invalid_name.name = embedded_nul_name;
  invalid_name.name_size = sizeof(embedded_nul_name);
  require_vhpi(
      validate_vhdl_vhpi_plugin_descriptor(invalid_name)
          == VhdlVhpiPluginError::PluginName,
      "VHPI plug-in rejects an embedded NUL in its bounded name");
  expect_plugin(
      [](auto& value) { value.startup = nullptr; },
      VhdlVhpiPluginError::MissingLifecycle,
      "VHPI plug-in rejects a missing startup callback");
  expect_plugin(
      [](auto& value) { value.shutdown = nullptr; },
      VhdlVhpiPluginError::MissingLifecycle,
      "VHPI plug-in rejects a missing shutdown callback");

  using fsim::runtime::load_vhdl_vhpi_plugin;
  std::string error;
  auto probe = fsim::platform::DynamicLibrary::open(
      FSIM_VHPI_TEST_PLUGIN_PATH, error);
  require_vhpi(
      probe != nullptr && error.empty(), "VHPI reference image opens");
  using Reset = void(FSIM_VHPI_CALL*)(int);
  using Count = int(FSIM_VHPI_CALL*)();
  const auto reset = reinterpret_cast<Reset>(
      probe->symbol("fsim_vhpi_test_reset", error));
  const auto startup_count = reinterpret_cast<Count>(
      probe->symbol("fsim_vhpi_test_startup_count", error));
  const auto shutdown_count = reinterpret_cast<Count>(
      probe->symbol("fsim_vhpi_test_shutdown_count", error));
  require_vhpi(
      reset != nullptr && startup_count != nullptr && shutdown_count != nullptr
          && error.empty(),
      "VHPI reference image exposes its lifecycle probes");

  reset(0);
  {
    const auto loaded =
        load_vhdl_vhpi_plugin(FSIM_VHPI_TEST_PLUGIN_PATH, host);
    require_vhpi(
        static_cast<bool>(loaded) && loaded.value->name() == "fsim-vhpi-test"
            && loaded.value->path()
                == std::filesystem::path{FSIM_VHPI_TEST_PLUGIN_PATH}
                       .lexically_normal(),
        "VHPI loader publishes a complete validated plug-in");
    require_vhpi(
        startup_count() == 1 && shutdown_count() == 0,
        "VHPI loader starts once before publication");
    const auto shutdown_result = loaded.value->shutdown();
    require_vhpi(
        static_cast<bool>(shutdown_result) && shutdown_count() == 1,
        "VHPI explicit shutdown succeeds exactly once");
    const auto repeated = loaded.value->shutdown();
    require_vhpi(
        static_cast<bool>(repeated) && shutdown_count() == 1,
        "VHPI repeated shutdown retains success without re-entry");
  }
  require_vhpi(
      shutdown_count() == 1,
      "VHPI destruction does not repeat explicit shutdown");

  reset(0);
  {
    const auto loaded =
        load_vhdl_vhpi_plugin(FSIM_VHPI_TEST_PLUGIN_PATH, host);
    require_vhpi(
        static_cast<bool>(loaded) && startup_count() == 1,
        "VHPI automatic teardown setup succeeds");
  }
  require_vhpi(
      shutdown_count() == 1,
      "VHPI destruction shuts down before image unload");

  const auto expect_load_error =
      [&](const int selected_mode,
          const VhdlVhpiPluginError expected,
          const int expected_startups,
          const char* const message) {
        reset(selected_mode);
        const auto result =
            load_vhdl_vhpi_plugin(FSIM_VHPI_TEST_PLUGIN_PATH, host);
        require_vhpi(
            !static_cast<bool>(result) && result.error == expected
                && startup_count() == expected_startups
                && shutdown_count() == 0,
            message);
      };
  expect_load_error(
      1, VhdlVhpiPluginError::BindFailure, 0,
      "VHPI loader contains bind status failure transactionally");
  expect_load_error(
      2, VhdlVhpiPluginError::BindException, 0,
      "VHPI loader contains bind exceptions transactionally");
  expect_load_error(
      3, VhdlVhpiPluginError::PluginAbiVersion, 0,
      "VHPI loader rejects a foreign plug-in ABI version");
  expect_load_error(
      4, VhdlVhpiPluginError::PluginAbiSize, 0,
      "VHPI loader rejects a truncated plug-in descriptor");
  expect_load_error(
      5, VhdlVhpiPluginError::PluginAbiFlags, 0,
      "VHPI loader rejects reserved plug-in flags");
  expect_load_error(
      6, VhdlVhpiPluginError::PluginName, 0,
      "VHPI loader rejects a missing bounded plug-in name");
  expect_load_error(
      7, VhdlVhpiPluginError::StartupFailure, 1,
      "VHPI loader contains startup status failure without shutdown");
  expect_load_error(
      8, VhdlVhpiPluginError::StartupException, 1,
      "VHPI loader contains startup exceptions without shutdown");
  expect_load_error(
      11, VhdlVhpiPluginError::MissingLifecycle, 0,
      "VHPI loader requires the complete lifecycle profile");

  reset(9);
  {
    const auto loaded =
        load_vhdl_vhpi_plugin(FSIM_VHPI_TEST_PLUGIN_PATH, host);
    require_vhpi(static_cast<bool>(loaded), "VHPI shutdown fixture loads");
    const auto shutdown_result = loaded.value->shutdown();
    const auto repeated = loaded.value->shutdown();
    require_vhpi(
        !static_cast<bool>(shutdown_result)
            && shutdown_result.error
                == VhdlVhpiPluginError::ShutdownFailure
            && repeated.error == shutdown_result.error
            && shutdown_count() == 1,
        "VHPI shutdown status failure is retained and invoked once");
  }
  reset(10);
  {
    const auto loaded =
        load_vhdl_vhpi_plugin(FSIM_VHPI_TEST_PLUGIN_PATH, host);
    require_vhpi(static_cast<bool>(loaded), "VHPI exception fixture loads");
    const auto shutdown_result = loaded.value->shutdown();
    require_vhpi(
        !static_cast<bool>(shutdown_result)
            && shutdown_result.error
                == VhdlVhpiPluginError::ShutdownException
            && shutdown_count() == 1,
        "VHPI shutdown exceptions are contained exactly once");
  }

  const auto missing_bind =
      load_vhdl_vhpi_plugin(FSIM_DPI_TEST_PLUGIN_PATH, host);
  require_vhpi(
      !static_cast<bool>(missing_bind)
          && missing_bind.error == VhdlVhpiPluginError::MissingBindSymbol,
      "VHPI loader rejects an image without the exact bind symbol");
  auto invalid_host = host;
  invalid_host.simulation_identity = 0;
  const auto host_failure =
      load_vhdl_vhpi_plugin(FSIM_VHPI_TEST_PLUGIN_PATH, invalid_host);
  require_vhpi(
      !static_cast<bool>(host_failure)
          && host_failure.error == VhdlVhpiPluginError::HostAbi,
      "VHPI loader validates host ownership before opening an image");
  const auto missing_artifact = load_vhdl_vhpi_plugin(
      std::filesystem::path{FSIM_VHPI_TEST_PLUGIN_PATH}.concat(".missing"),
      host);
  require_vhpi(
      !static_cast<bool>(missing_artifact)
          && missing_artifact.error == VhdlVhpiPluginError::ArtifactOpen,
      "VHPI loader reports artifact-open failure without partial state");
}

}  // namespace fsim::tests::runtime
