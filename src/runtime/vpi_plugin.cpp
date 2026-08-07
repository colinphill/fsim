// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_plugin.hpp"

#include "fsim/platform/dynamic_library.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::uint32_t required_host_size = static_cast<std::uint32_t>(
    offsetof(fsim_vpi_host_v1, report) + sizeof(fsim_vpi_host_v1::report));
constexpr std::uint32_t required_host_v2_size =
    static_cast<std::uint32_t>(sizeof(fsim_vpi_host_v2));
constexpr std::uint32_t required_plugin_size = static_cast<std::uint32_t>(
    offsetof(fsim_vpi_plugin_v1, shutdown)
    + sizeof(fsim_vpi_plugin_v1::shutdown));
constexpr std::uint32_t maximum_plugin_name_size = 4096;

}  // namespace

struct SystemVerilogVpiLoadedPlugin::Impl {
  std::filesystem::path path;
  std::string name;
  void* context{};
  fsim_vpi_plugin_lifecycle_v1 shutdown_callback{};
  bool shutdown_attempted{};
  SystemVerilogVpiShutdownResult shutdown_result;
  std::unique_ptr<platform::DynamicLibrary> library;

  [[nodiscard]] SystemVerilogVpiShutdownResult shutdown() noexcept {
    if (shutdown_attempted) {
      return shutdown_result;
    }
    shutdown_attempted = true;
    if (shutdown_callback == nullptr) {
      return shutdown_result;
    }
    try {
      if (shutdown_callback(context) != FSIM_VPI_STATUS_OK) {
        shutdown_result = {
            SystemVerilogVpiPluginError::ShutdownFailure,
            "VPI plug-in shutdown returned failure",
        };
      }
    } catch (...) {
      shutdown_result = {
          SystemVerilogVpiPluginError::ShutdownException,
          "VPI plug-in shutdown threw an exception",
      };
    }
    return shutdown_result;
  }

  ~Impl() { (void)shutdown(); }
};

SystemVerilogVpiLoadedPlugin::~SystemVerilogVpiLoadedPlugin() = default;
SystemVerilogVpiLoadedPlugin::SystemVerilogVpiLoadedPlugin(
    SystemVerilogVpiLoadedPlugin&&) noexcept = default;
SystemVerilogVpiLoadedPlugin& SystemVerilogVpiLoadedPlugin::operator=(
    SystemVerilogVpiLoadedPlugin&&) noexcept = default;

SystemVerilogVpiLoadedPlugin::SystemVerilogVpiLoadedPlugin(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

const std::filesystem::path& SystemVerilogVpiLoadedPlugin::path()
    const noexcept {
  return impl_->path;
}

const std::string& SystemVerilogVpiLoadedPlugin::name() const noexcept {
  return impl_->name;
}

SystemVerilogVpiShutdownResult SystemVerilogVpiLoadedPlugin::shutdown()
    noexcept {
  return impl_->shutdown();
}

fsim_vpi_host_v1 make_systemverilog_vpi_host(
    const std::uint64_t simulation_identity,
    void* const context,
    const fsim_vpi_report_v1 report) noexcept {
  return {
      FSIM_VPI_HOST_ABI_VERSION,
      required_host_size,
      static_cast<std::uint32_t>(sizeof(void*) * 8U),
      0,
      simulation_identity,
      context,
      report,
  };
}

fsim_vpi_host_v2 make_systemverilog_vpi_host_v2(
    const std::uint64_t simulation_identity,
    void* const report_context,
    const fsim_vpi_report_v1 report,
    void* const service_context,
    const fsim_vpi_invoke_service_v1 invoke_service) noexcept {
  auto result = fsim_vpi_host_v2{
      make_systemverilog_vpi_host(
          simulation_identity, report_context, report),
      service_context,
      invoke_service,
  };
  result.v1.abi_version = FSIM_VPI_HOST_ABI_VERSION_V2;
  result.v1.struct_size = required_host_v2_size;
  return result;
}

SystemVerilogVpiAbiError validate_systemverilog_vpi_host(
    const fsim_vpi_host_v1& host) noexcept {
  if (host.abi_version != FSIM_VPI_HOST_ABI_VERSION
      && host.abi_version != FSIM_VPI_HOST_ABI_VERSION_V2) {
    return SystemVerilogVpiAbiError::AbiVersion;
  }
  if (host.struct_size < required_host_size) {
    return SystemVerilogVpiAbiError::AbiSize;
  }
  if (host.pointer_bits != sizeof(void*) * 8U) {
    return SystemVerilogVpiAbiError::AbiPointerWidth;
  }
  if (host.flags != 0U) {
    return SystemVerilogVpiAbiError::AbiFlags;
  }
  if (host.simulation_identity == 0U) {
    return SystemVerilogVpiAbiError::SimulationIdentity;
  }
  if (host.context == nullptr) {
    return SystemVerilogVpiAbiError::HostContext;
  }
  if (host.report == nullptr) {
    return SystemVerilogVpiAbiError::ReportCallback;
  }
  if (host.abi_version == FSIM_VPI_HOST_ABI_VERSION_V2) {
    if (host.struct_size < required_host_v2_size) {
      return SystemVerilogVpiAbiError::AbiSize;
    }
    const auto& host_v2 =
        reinterpret_cast<const fsim_vpi_host_v2&>(host);
    if (host_v2.service_context == nullptr) {
      return SystemVerilogVpiAbiError::ServiceContext;
    }
    if (host_v2.invoke_service == nullptr) {
      return SystemVerilogVpiAbiError::ServiceCallback;
    }
  }
  return SystemVerilogVpiAbiError::None;
}

SystemVerilogVpiPluginError validate_systemverilog_vpi_plugin_descriptor(
    const fsim_vpi_plugin_v1& plugin) noexcept {
  if (plugin.abi_version != FSIM_VPI_PLUGIN_ABI_VERSION) {
    return SystemVerilogVpiPluginError::PluginAbiVersion;
  }
  if (plugin.struct_size < required_plugin_size) {
    return SystemVerilogVpiPluginError::PluginAbiSize;
  }
  if (plugin.flags != 0U) {
    return SystemVerilogVpiPluginError::PluginAbiFlags;
  }
  if (plugin.name == nullptr || plugin.name_size == 0U
      || plugin.name_size > maximum_plugin_name_size
      || std::string_view{plugin.name, plugin.name_size}.find('\0')
          != std::string_view::npos) {
    return SystemVerilogVpiPluginError::PluginName;
  }
  if (plugin.startup == nullptr || plugin.shutdown == nullptr) {
    return SystemVerilogVpiPluginError::MissingLifecycle;
  }
  return SystemVerilogVpiPluginError::None;
}

SystemVerilogVpiPluginLoadResult load_systemverilog_vpi_plugin(
    const std::filesystem::path& artifact,
    const fsim_vpi_host_v1& host) {
  if (validate_systemverilog_vpi_host(host) != SystemVerilogVpiAbiError::None) {
    return {{}, SystemVerilogVpiPluginError::HostAbi,
        "VPI host ABI validation failed"};
  }

  std::string error;
  auto library = platform::DynamicLibrary::open(artifact, error);
  if (!library) {
    return {{}, SystemVerilogVpiPluginError::ArtifactOpen, std::move(error)};
  }
  auto* raw_bind = library->symbol(FSIM_VPI_PLUGIN_BIND_SYMBOL, error);
  if (raw_bind == nullptr) {
    return {{}, SystemVerilogVpiPluginError::MissingBindSymbol,
        "VPI plug-in is missing its bind symbol: " + error};
  }

  const auto bind =
      reinterpret_cast<fsim_vpi_plugin_bind_v1_fn>(raw_bind);
  fsim_vpi_plugin_v1 plugin{};
  try {
    if (bind(&host, &plugin) != FSIM_VPI_STATUS_OK) {
      return {{}, SystemVerilogVpiPluginError::BindFailure,
          "VPI plug-in bind returned failure"};
    }
  } catch (...) {
    return {{}, SystemVerilogVpiPluginError::BindException,
        "VPI plug-in bind threw an exception"};
  }

  const auto descriptor_error =
      validate_systemverilog_vpi_plugin_descriptor(plugin);
  if (descriptor_error != SystemVerilogVpiPluginError::None) {
    return {{}, descriptor_error, "VPI plug-in descriptor mismatch"};
  }

  try {
    if (plugin.startup(plugin.context) != FSIM_VPI_STATUS_OK) {
      return {{}, SystemVerilogVpiPluginError::StartupFailure,
          "VPI plug-in startup returned failure"};
    }
  } catch (...) {
    return {{}, SystemVerilogVpiPluginError::StartupException,
        "VPI plug-in startup threw an exception"};
  }

  auto impl = std::make_unique<SystemVerilogVpiLoadedPlugin::Impl>();
  impl->path = artifact.lexically_normal();
  impl->name.assign(plugin.name, plugin.name_size);
  impl->context = plugin.context;
  impl->shutdown_callback = plugin.shutdown;
  impl->library = std::move(library);
  return {std::make_unique<SystemVerilogVpiLoadedPlugin>(std::move(impl)),
      {}, {}};
}

}  // namespace fsim::runtime
