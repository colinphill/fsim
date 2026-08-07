// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_plugin.hpp"

#include "fsim/platform/dynamic_library.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::uint32_t required_host_size = static_cast<std::uint32_t>(
    offsetof(fsim_vhpi_host_v1, report) + sizeof(fsim_vhpi_host_v1::report));
constexpr std::uint32_t required_host_v2_size =
    static_cast<std::uint32_t>(
        offsetof(fsim_vhpi_host_v2, invoke_service)
        + sizeof(fsim_vhpi_host_v2::invoke_service));
constexpr std::uint32_t required_plugin_size = static_cast<std::uint32_t>(
    offsetof(fsim_vhpi_plugin_v1, shutdown)
    + sizeof(fsim_vhpi_plugin_v1::shutdown));
constexpr std::uint32_t maximum_plugin_name_size = 4096;

}  // namespace

struct VhdlVhpiLoadedPlugin::Impl {
  std::filesystem::path path;
  std::string name;
  void* context{};
  fsim_vhpi_plugin_lifecycle_v1 shutdown_callback{};
  bool startup_succeeded{};
  bool shutdown_attempted{};
  VhdlVhpiShutdownResult shutdown_result;
  std::unique_ptr<platform::DynamicLibrary> library;

  [[nodiscard]] VhdlVhpiShutdownResult shutdown() noexcept {
    if (shutdown_attempted || !startup_succeeded) {
      return shutdown_result;
    }
    shutdown_attempted = true;
    try {
      if (shutdown_callback(context) != FSIM_VHPI_STATUS_OK) {
        shutdown_result = {
            VhdlVhpiPluginError::ShutdownFailure,
            "VHPI plug-in shutdown returned failure",
        };
      }
    } catch (...) {
      shutdown_result = {
          VhdlVhpiPluginError::ShutdownException,
          "VHPI plug-in shutdown threw an exception",
      };
    }
    return shutdown_result;
  }

  ~Impl() { (void)shutdown(); }
};

VhdlVhpiLoadedPlugin::~VhdlVhpiLoadedPlugin() = default;
VhdlVhpiLoadedPlugin::VhdlVhpiLoadedPlugin(
    VhdlVhpiLoadedPlugin&&) noexcept = default;
VhdlVhpiLoadedPlugin& VhdlVhpiLoadedPlugin::operator=(
    VhdlVhpiLoadedPlugin&&) noexcept = default;

VhdlVhpiLoadedPlugin::VhdlVhpiLoadedPlugin(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

const std::filesystem::path& VhdlVhpiLoadedPlugin::path() const noexcept {
  return impl_->path;
}

const std::string& VhdlVhpiLoadedPlugin::name() const noexcept {
  return impl_->name;
}

VhdlVhpiShutdownResult VhdlVhpiLoadedPlugin::shutdown() noexcept {
  return impl_->shutdown();
}

fsim_vhpi_host_v1 make_vhdl_vhpi_host(
    const std::uint64_t simulation_identity,
    void* const context,
    const fsim_vhpi_report_v1 report) noexcept {
  return {
      FSIM_VHPI_HOST_ABI_VERSION,
      required_host_size,
      static_cast<std::uint32_t>(sizeof(void*) * 8U),
      0,
      simulation_identity,
      context,
      report,
  };
}

fsim_vhpi_host_v2 make_vhdl_vhpi_host_v2(
    const std::uint64_t simulation_identity,
    void* const report_context,
    const fsim_vhpi_report_v1 report,
    void* const service_context,
    const fsim_vhpi_invoke_service_v1 invoke_service) noexcept {
  auto result = fsim_vhpi_host_v2{
      make_vhdl_vhpi_host(
          simulation_identity, report_context, report),
      service_context,
      invoke_service,
  };
  result.v1.abi_version = FSIM_VHPI_HOST_ABI_VERSION_V2;
  result.v1.struct_size = required_host_v2_size;
  return result;
}

VhdlVhpiAbiError validate_vhdl_vhpi_host(
    const fsim_vhpi_host_v1& host) noexcept {
  if (host.abi_version != FSIM_VHPI_HOST_ABI_VERSION
      && host.abi_version != FSIM_VHPI_HOST_ABI_VERSION_V2) {
    return VhdlVhpiAbiError::AbiVersion;
  }
  if (host.struct_size < required_host_size) {
    return VhdlVhpiAbiError::AbiSize;
  }
  if (host.pointer_bits != sizeof(void*) * 8U) {
    return VhdlVhpiAbiError::AbiPointerWidth;
  }
  if (host.flags != 0U) {
    return VhdlVhpiAbiError::AbiFlags;
  }
  if (host.simulation_identity == 0U) {
    return VhdlVhpiAbiError::SimulationIdentity;
  }
  if (host.context == nullptr) {
    return VhdlVhpiAbiError::HostContext;
  }
  if (host.report == nullptr) {
    return VhdlVhpiAbiError::ReportCallback;
  }
  if (host.abi_version == FSIM_VHPI_HOST_ABI_VERSION_V2) {
    if (host.struct_size < required_host_v2_size) {
      return VhdlVhpiAbiError::AbiSize;
    }
    const auto& host_v2 =
        reinterpret_cast<const fsim_vhpi_host_v2&>(host);
    if (host_v2.service_context == nullptr) {
      return VhdlVhpiAbiError::ServiceContext;
    }
    if (host_v2.invoke_service == nullptr) {
      return VhdlVhpiAbiError::ServiceCallback;
    }
  }
  return VhdlVhpiAbiError::None;
}

VhdlVhpiPluginError validate_vhdl_vhpi_plugin_descriptor(
    const fsim_vhpi_plugin_v1& plugin) noexcept {
  if (plugin.abi_version != FSIM_VHPI_PLUGIN_ABI_VERSION) {
    return VhdlVhpiPluginError::PluginAbiVersion;
  }
  if (plugin.struct_size < required_plugin_size) {
    return VhdlVhpiPluginError::PluginAbiSize;
  }
  if (plugin.flags != 0U) {
    return VhdlVhpiPluginError::PluginAbiFlags;
  }
  if (plugin.name == nullptr || plugin.name_size == 0U
      || plugin.name_size > maximum_plugin_name_size
      || std::string_view{plugin.name, plugin.name_size}.find('\0')
          != std::string_view::npos) {
    return VhdlVhpiPluginError::PluginName;
  }
  if (plugin.startup == nullptr || plugin.shutdown == nullptr) {
    return VhdlVhpiPluginError::MissingLifecycle;
  }
  return VhdlVhpiPluginError::None;
}

VhdlVhpiPluginLoadResult load_vhdl_vhpi_plugin(
    const std::filesystem::path& artifact,
    const fsim_vhpi_host_v1& host) {
  if (validate_vhdl_vhpi_host(host) != VhdlVhpiAbiError::None) {
    return {{}, VhdlVhpiPluginError::HostAbi,
        "VHPI host ABI validation failed"};
  }

  std::string error;
  auto library = platform::DynamicLibrary::open(artifact, error);
  if (!library) {
    return {{}, VhdlVhpiPluginError::ArtifactOpen, std::move(error)};
  }
  auto* const raw_bind = library->symbol(FSIM_VHPI_PLUGIN_BIND_SYMBOL, error);
  if (raw_bind == nullptr) {
    return {{}, VhdlVhpiPluginError::MissingBindSymbol,
        "VHPI plug-in is missing its bind symbol: " + error};
  }

  const auto bind =
      reinterpret_cast<fsim_vhpi_plugin_bind_v1_fn>(raw_bind);
  fsim_vhpi_plugin_v1 plugin{};
  try {
    if (bind(&host, &plugin) != FSIM_VHPI_STATUS_OK) {
      return {{}, VhdlVhpiPluginError::BindFailure,
          "VHPI plug-in bind returned failure"};
    }
  } catch (...) {
    return {{}, VhdlVhpiPluginError::BindException,
        "VHPI plug-in bind threw an exception"};
  }

  const auto descriptor_error = validate_vhdl_vhpi_plugin_descriptor(plugin);
  if (descriptor_error != VhdlVhpiPluginError::None) {
    return {{}, descriptor_error, "VHPI plug-in descriptor mismatch"};
  }

  auto impl = std::make_unique<VhdlVhpiLoadedPlugin::Impl>();
  impl->path = artifact.lexically_normal();
  impl->name.assign(plugin.name, plugin.name_size);
  impl->context = plugin.context;
  impl->shutdown_callback = plugin.shutdown;
  impl->library = std::move(library);
  try {
    if (plugin.startup(plugin.context) != FSIM_VHPI_STATUS_OK) {
      return {{}, VhdlVhpiPluginError::StartupFailure,
          "VHPI plug-in startup returned failure"};
    }
  } catch (...) {
    return {{}, VhdlVhpiPluginError::StartupException,
        "VHPI plug-in startup threw an exception"};
  }
  impl->startup_succeeded = true;
  return {std::make_unique<VhdlVhpiLoadedPlugin>(std::move(impl)), {}, {}};
}

}  // namespace fsim::runtime
