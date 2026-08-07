// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_abi.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fsim::runtime {

enum class VhdlVhpiAbiError {
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

enum class VhdlVhpiPluginError {
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

struct VhdlVhpiShutdownResult {
  VhdlVhpiPluginError error{VhdlVhpiPluginError::None};
  std::string message;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiPluginError::None;
  }
};

class VhdlVhpiLoadedPlugin final {
 public:
  struct Impl;

  ~VhdlVhpiLoadedPlugin();
  VhdlVhpiLoadedPlugin(VhdlVhpiLoadedPlugin&&) noexcept;
  VhdlVhpiLoadedPlugin& operator=(VhdlVhpiLoadedPlugin&&) noexcept;
  VhdlVhpiLoadedPlugin(const VhdlVhpiLoadedPlugin&) = delete;
  VhdlVhpiLoadedPlugin& operator=(const VhdlVhpiLoadedPlugin&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] VhdlVhpiShutdownResult shutdown() noexcept;

  explicit VhdlVhpiLoadedPlugin(std::unique_ptr<Impl> impl) noexcept;

 private:
  std::unique_ptr<Impl> impl_;
};

struct VhdlVhpiPluginLoadResult {
  std::unique_ptr<VhdlVhpiLoadedPlugin> value;
  VhdlVhpiPluginError error{VhdlVhpiPluginError::None};
  std::string message;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiPluginError::None && value != nullptr;
  }
};

[[nodiscard]] fsim_vhpi_host_v1 make_vhdl_vhpi_host(
    std::uint64_t simulation_identity,
    void* context,
    fsim_vhpi_report_v1 report) noexcept;

[[nodiscard]] fsim_vhpi_host_v2 make_vhdl_vhpi_host_v2(
    std::uint64_t simulation_identity,
    void* report_context,
    fsim_vhpi_report_v1 report,
    void* service_context,
    fsim_vhpi_invoke_service_v1 invoke_service) noexcept;

[[nodiscard]] VhdlVhpiAbiError validate_vhdl_vhpi_host(
    const fsim_vhpi_host_v1& host) noexcept;

[[nodiscard]] VhdlVhpiPluginError validate_vhdl_vhpi_plugin_descriptor(
    const fsim_vhpi_plugin_v1& plugin) noexcept;

[[nodiscard]] VhdlVhpiPluginLoadResult load_vhdl_vhpi_plugin(
    const std::filesystem::path& artifact,
    const fsim_vhpi_host_v1& host);

}  // namespace fsim::runtime
