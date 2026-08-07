// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_abi.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fsim::runtime {

enum class SystemVerilogVpiAbiError {
  None,
  AbiVersion,
  AbiSize,
  AbiPointerWidth,
  AbiFlags,
  SimulationIdentity,
  HostContext,
  ReportCallback,
  ServiceContext,
  ServiceCallback,
};

enum class SystemVerilogVpiPluginError {
  None,
  HostAbi,
  ArtifactOpen,
  MissingBindSymbol,
  BindFailure,
  BindException,
  PluginAbiVersion,
  PluginAbiSize,
  PluginAbiFlags,
  PluginName,
  MissingLifecycle,
  StartupFailure,
  StartupException,
  ShutdownFailure,
  ShutdownException,
};

struct SystemVerilogVpiShutdownResult {
  SystemVerilogVpiPluginError error{SystemVerilogVpiPluginError::None};
  std::string message;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiPluginError::None;
  }
};

class SystemVerilogVpiLoadedPlugin final {
 public:
  struct Impl;

  ~SystemVerilogVpiLoadedPlugin();
  SystemVerilogVpiLoadedPlugin(SystemVerilogVpiLoadedPlugin&&) noexcept;
  SystemVerilogVpiLoadedPlugin& operator=(
      SystemVerilogVpiLoadedPlugin&&) noexcept;
  SystemVerilogVpiLoadedPlugin(const SystemVerilogVpiLoadedPlugin&) = delete;
  SystemVerilogVpiLoadedPlugin& operator=(
      const SystemVerilogVpiLoadedPlugin&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] SystemVerilogVpiShutdownResult shutdown() noexcept;

  explicit SystemVerilogVpiLoadedPlugin(std::unique_ptr<Impl> impl) noexcept;

 private:
  std::unique_ptr<Impl> impl_;
};

struct SystemVerilogVpiPluginLoadResult {
  std::unique_ptr<SystemVerilogVpiLoadedPlugin> value;
  SystemVerilogVpiPluginError error{SystemVerilogVpiPluginError::None};
  std::string message;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiPluginError::None && value != nullptr;
  }
};

[[nodiscard]] fsim_vpi_host_v1 make_systemverilog_vpi_host(
    std::uint64_t simulation_identity,
    void* context,
    fsim_vpi_report_v1 report) noexcept;

[[nodiscard]] fsim_vpi_host_v2 make_systemverilog_vpi_host_v2(
    std::uint64_t simulation_identity,
    void* report_context,
    fsim_vpi_report_v1 report,
    void* service_context,
    fsim_vpi_invoke_service_v1 invoke_service) noexcept;

[[nodiscard]] SystemVerilogVpiAbiError validate_systemverilog_vpi_host(
    const fsim_vpi_host_v1& host) noexcept;

[[nodiscard]] SystemVerilogVpiPluginError
validate_systemverilog_vpi_plugin_descriptor(
    const fsim_vpi_plugin_v1& plugin) noexcept;

[[nodiscard]] SystemVerilogVpiPluginLoadResult
load_systemverilog_vpi_plugin(
    const std::filesystem::path& artifact,
    const fsim_vpi_host_v1& host);

}  // namespace fsim::runtime
