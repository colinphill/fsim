// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_plugin.hpp"

#include "fsim/platform/dynamic_library.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {

namespace {

struct DpiLibraryOwner {
  std::unique_ptr<platform::DynamicLibrary> library;
};

}  // namespace

struct SystemVerilogDpiLoadedPlugin::Impl {
  std::filesystem::path path;
  std::shared_ptr<DpiLibraryOwner> owner;
  std::unordered_map<std::string, void*> imports;
  std::unordered_map<std::string, void*> exports;
  bool quarantined{};
};

namespace {

[[nodiscard]] bool identifier(const std::string& value) noexcept {
  if (value.empty()) return false;
  const auto first = static_cast<unsigned char>(value.front());
  if (std::isalpha(first) == 0 && value.front() != '_') return false;
  return std::ranges::all_of(value.substr(1), [](const char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '_';
  });
}

[[nodiscard]] bool relative_portable_path(
    const std::filesystem::path& path) noexcept {
  if (path.empty() || path.is_absolute() || path.has_root_name()
      || path.has_root_directory()) return false;
  return std::ranges::none_of(path, [](const auto& component) {
    return component == ".." || component.empty();
  });
}

template <class Range, class Key>
[[nodiscard]] bool unique_entries(const Range& values, Key key) {
  std::unordered_set<std::string> seen;
  for (const auto& value : values) {
    if (!seen.emplace(key(value)).second) return false;
  }
  return true;
}

[[nodiscard]] bool source_extension(const std::filesystem::path& path) {
  const auto extension = path.extension().string();
  return extension == ".c" || extension == ".cc" || extension == ".cpp"
      || extension == ".cxx";
}

[[nodiscard]] std::string library_filename(
    const SystemVerilogDpiPluginPlatform platform) {
  return platform == SystemVerilogDpiPluginPlatform::Msvc
      ? "plugin.dll" : "plugin.so";
}

}  // namespace

SystemVerilogDpiPluginPlanResult plan_systemverilog_dpi_plugin(
    const SystemVerilogDpiPluginManifest& manifest,
    const std::filesystem::path& source_root,
    const std::filesystem::path& build_root,
    const std::vector<std::filesystem::path>& discovery_roots,
    std::string compiler,
    const SystemVerilogDpiPluginPlatform platform) {
  if (manifest.version != systemverilog_dpi_plugin_manifest_version) {
    return {{}, SystemVerilogDpiPluginError::ManifestVersion};
  }
  if (!identifier(manifest.name)) {
    return {{}, SystemVerilogDpiPluginError::InvalidName};
  }
  if (compiler.empty() || compiler.find('\0') != std::string::npos) {
    return {{}, SystemVerilogDpiPluginError::MissingCompiler};
  }
  if (manifest.sources.empty()
      || !std::ranges::all_of(manifest.sources, [](const auto& path) {
        return relative_portable_path(path) && source_extension(path);
      })) {
    return {{}, SystemVerilogDpiPluginError::InvalidSource};
  }
  if (!std::ranges::all_of(manifest.include_directories,
          relative_portable_path)) {
    return {{}, SystemVerilogDpiPluginError::InvalidPath};
  }
  const auto path_key = [](const auto& path) {
    return path.lexically_normal().generic_string();
  };
  if (!unique_entries(manifest.sources, path_key)
      || !unique_entries(manifest.include_directories, path_key)
      || !unique_entries(manifest.libraries,
          [](const auto& value) { return value; })) {
    return {{}, SystemVerilogDpiPluginError::DuplicateEntry};
  }
  std::vector<std::string> symbols = manifest.imported_symbols;
  symbols.insert(symbols.end(), manifest.exported_symbols.begin(),
      manifest.exported_symbols.end());
  if (!std::ranges::all_of(symbols, identifier)) {
    return {{}, SystemVerilogDpiPluginError::InvalidSymbol};
  }
  if (!unique_entries(symbols, [](const auto& value) { return value; })) {
    return {{}, SystemVerilogDpiPluginError::DuplicateEntry};
  }

  SystemVerilogDpiPluginBuildPlan plan;
  const auto filename = library_filename(platform);
  const auto command_path = [platform](const auto& path) {
    return platform == SystemVerilogDpiPluginPlatform::Msvc
        ? path.string()
        : path.generic_string();
  };
  plan.artifact = (build_root / manifest.name / filename).lexically_normal();
  for (const auto& root : discovery_roots) {
    plan.discovery_candidates.push_back(
        (root / manifest.name / filename).lexically_normal());
  }
  std::vector<std::filesystem::path> objects;
  for (std::size_t index{}; index < manifest.sources.size(); ++index) {
    const auto source =
        (source_root / manifest.sources[index]).lexically_normal();
    auto object = build_root / manifest.name
        / (std::to_string(index) + "-"
            + manifest.sources[index].filename().string()
            + (platform == SystemVerilogDpiPluginPlatform::Msvc
                ? ".obj" : ".o"));
    object = object.lexically_normal();
    objects.push_back(object);
    SystemVerilogDpiPluginCommand command;
    command.arguments.push_back(compiler);
    if (platform == SystemVerilogDpiPluginPlatform::Msvc) {
      command.arguments.insert(command.arguments.end(),
          {"/nologo", "/std:c++20", "/EHsc", "/Gd", "/c"});
      for (const auto& include : manifest.include_directories) {
        command.arguments.push_back(
            "/I" + command_path((source_root / include).lexically_normal()));
      }
      command.arguments.push_back(command_path(source));
      command.arguments.push_back("/Fo" + command_path(object));
    } else {
      command.arguments.insert(command.arguments.end(),
          {"-std=c++20", "-fPIC", "-fvisibility=hidden"});
      for (const auto& include : manifest.include_directories) {
        command.arguments.push_back("-I");
        command.arguments.push_back(
            command_path((source_root / include).lexically_normal()));
      }
      command.arguments.insert(command.arguments.end(),
          {"-c", command_path(source), "-o", command_path(object)});
    }
    plan.compile_commands.push_back(std::move(command));
  }
  plan.link_command.arguments.push_back(std::move(compiler));
  plan.link_command.arguments.push_back(
      platform == SystemVerilogDpiPluginPlatform::Msvc ? "/LD" : "-shared");
  for (const auto& object : objects) {
    plan.link_command.arguments.push_back(command_path(object));
  }
  for (const auto& library : manifest.libraries) {
    plan.link_command.arguments.push_back(
        platform == SystemVerilogDpiPluginPlatform::Msvc
            ? library + ".lib" : "-l" + library);
  }
  plan.link_command.arguments.push_back(
      (platform == SystemVerilogDpiPluginPlatform::Msvc ? "/Fe:" : "-o"));
  plan.link_command.arguments.push_back(command_path(plan.artifact));
  return {std::move(plan), {}};
}

SystemVerilogDpiLoadedPlugin::SystemVerilogDpiLoadedPlugin(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

SystemVerilogDpiLoadedPlugin::~SystemVerilogDpiLoadedPlugin() = default;
SystemVerilogDpiLoadedPlugin::SystemVerilogDpiLoadedPlugin(
    SystemVerilogDpiLoadedPlugin&&) noexcept = default;
SystemVerilogDpiLoadedPlugin& SystemVerilogDpiLoadedPlugin::operator=(
    SystemVerilogDpiLoadedPlugin&&) noexcept = default;

SystemVerilogDpiPluginSymbol::SystemVerilogDpiPluginSymbol(
    void* address, std::shared_ptr<const void> lifetime) noexcept
    : address_(address), lifetime_(std::move(lifetime)) {}

void* SystemVerilogDpiPluginSymbol::address() const noexcept {
  return address_;
}

SystemVerilogDpiPluginSymbol::operator bool() const noexcept {
  return address_ != nullptr && lifetime_ != nullptr;
}

const std::filesystem::path& SystemVerilogDpiLoadedPlugin::path()
    const noexcept {
  return impl_->path;
}

std::optional<SystemVerilogDpiPluginSymbol>
SystemVerilogDpiLoadedPlugin::imported_symbol(
    const std::string_view name) const noexcept {
  const auto found = impl_->imports.find(std::string{name});
  return found == impl_->imports.end()
      ? std::nullopt
      : std::optional<SystemVerilogDpiPluginSymbol>{
          std::in_place, found->second, impl_->owner};
}

std::optional<SystemVerilogDpiPluginSymbol>
SystemVerilogDpiLoadedPlugin::exported_symbol(
    const std::string_view name) const noexcept {
  const auto found = impl_->exports.find(std::string{name});
  return found == impl_->exports.end()
      ? std::nullopt
      : std::optional<SystemVerilogDpiPluginSymbol>{
          std::in_place, found->second, impl_->owner};
}

bool SystemVerilogDpiLoadedPlugin::quarantine() noexcept {
  try {
    static auto* mutex = new std::mutex;
    static auto* owners =
        new std::vector<std::shared_ptr<const void>>;
    const std::lock_guard lock{*mutex};
    owners->push_back(impl_->owner);
    impl_->quarantined = true;
    return true;
  } catch (...) {
    return false;
  }
}

SystemVerilogDpiPluginLoadResult load_systemverilog_dpi_plugin(
    const std::filesystem::path& artifact,
    const SystemVerilogDpiPluginManifest& manifest) {
  std::string error;
  auto library = platform::DynamicLibrary::open(artifact, error);
  if (!library) {
    return {{}, SystemVerilogDpiPluginError::ArtifactOpen, std::move(error)};
  }
  auto* raw_descriptor = library->symbol(
      FSIM_DPI_PLUGIN_DESCRIPTOR_SYMBOL, error);
  if (raw_descriptor == nullptr) {
    return {{}, SystemVerilogDpiPluginError::MissingDescriptor,
        "DPI plug-in is missing its ABI descriptor: " + error};
  }
  const auto descriptor_function =
      reinterpret_cast<fsim_dpi_plugin_descriptor_v1_get_fn>(raw_descriptor);
  const fsim_dpi_plugin_descriptor_v1* descriptor{};
  try {
    descriptor = descriptor_function();
  } catch (...) {
    return {{}, SystemVerilogDpiPluginError::MissingDescriptor,
        "DPI plug-in ABI descriptor threw an exception"};
  }
  if (descriptor == nullptr) {
    return {{}, SystemVerilogDpiPluginError::MissingDescriptor,
        "DPI plug-in returned a null ABI descriptor"};
  }
  const auto descriptor_error =
      validate_systemverilog_dpi_plugin_descriptor(*descriptor, manifest);
  if (descriptor_error != SystemVerilogDpiPluginError::None) {
    return {{}, descriptor_error, "DPI plug-in ABI descriptor mismatch"};
  }
  auto impl = std::make_unique<SystemVerilogDpiLoadedPlugin::Impl>();
  impl->path = artifact.lexically_normal();
  impl->owner = std::make_shared<DpiLibraryOwner>();
  impl->owner->library = std::move(library);
  const auto resolve = [&](const std::vector<std::string>& names,
                           auto& destination) {
    for (const auto& name : names) {
      auto* address = impl->owner->library->symbol(name, error);
      if (address == nullptr) {
        error = "DPI plug-in is missing symbol " + name + ": " + error;
        return false;
      }
      destination.emplace(name, address);
    }
    return true;
  };
  if (!resolve(manifest.imported_symbols, impl->imports)
      || !resolve(manifest.exported_symbols, impl->exports)) {
    return {{}, SystemVerilogDpiPluginError::MissingSymbol,
        std::move(error)};
  }
  return {std::make_unique<SystemVerilogDpiLoadedPlugin>(std::move(impl)),
      {}, {}};
}

SystemVerilogDpiPluginError validate_systemverilog_dpi_plugin_descriptor(
    const fsim_dpi_plugin_descriptor_v1& descriptor,
    const SystemVerilogDpiPluginManifest& manifest) noexcept {
  if (descriptor.abi_version != FSIM_DPI_PLUGIN_ABI_VERSION) {
    return SystemVerilogDpiPluginError::AbiVersion;
  }
  if (descriptor.struct_size < sizeof(fsim_dpi_plugin_descriptor_v1)) {
    return SystemVerilogDpiPluginError::AbiSize;
  }
  if (descriptor.pointer_bits != sizeof(void*) * 8U) {
    return SystemVerilogDpiPluginError::AbiPointerWidth;
  }
  if (descriptor.flags != 0) {
    return SystemVerilogDpiPluginError::AbiFlags;
  }
  if (descriptor.name == nullptr
      || descriptor.name_size != manifest.name.size()
      || std::memcmp(descriptor.name, manifest.name.data(),
          manifest.name.size()) != 0) {
    return SystemVerilogDpiPluginError::AbiName;
  }
  return {};
}

SystemVerilogDpiPluginProvenanceResult
provenance_systemverilog_dpi_plugin(
    const SystemVerilogDpiPluginManifest& manifest,
    const std::filesystem::path& artifact,
    const std::string_view toolchain_identity,
    const SystemVerilogDpiPluginPlatform platform) {
  const auto add_field = [](support::Sha256& hash,
                            const std::string_view value) {
    hash.update(std::to_string(value.size()));
    hash.update(":");
    hash.update(value);
  };
  support::Sha256 manifest_hash;
  add_field(manifest_hash, std::to_string(manifest.version));
  add_field(manifest_hash, manifest.name);
  const auto add_paths = [&](const auto& paths) {
    add_field(manifest_hash, std::to_string(paths.size()));
    for (const auto& path : paths) {
      add_field(manifest_hash, path.lexically_normal().generic_string());
    }
  };
  const auto add_strings = [&](const auto& values) {
    add_field(manifest_hash, std::to_string(values.size()));
    for (const auto& value : values) add_field(manifest_hash, value);
  };
  add_paths(manifest.sources);
  add_paths(manifest.include_directories);
  add_strings(manifest.libraries);
  add_strings(manifest.imported_symbols);
  add_strings(manifest.exported_symbols);
  const auto manifest_digest = support::Sha256::hex(manifest_hash.finish());

  std::ifstream stream{artifact, std::ios::binary};
  if (!stream) {
    return {{}, SystemVerilogDpiPluginError::ArtifactRead,
        "cannot read DPI plug-in artifact"};
  }
  support::Sha256 artifact_hash;
  std::array<char, 64U * 1024U> buffer{};
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = stream.gcount();
    if (count > 0) {
      artifact_hash.update(std::as_bytes(std::span{
          buffer.data(), static_cast<std::size_t>(count)}));
    }
  }
  if (!stream.eof()) {
    return {{}, SystemVerilogDpiPluginError::ArtifactRead,
        "failed while reading DPI plug-in artifact"};
  }
  const auto artifact_digest = support::Sha256::hex(artifact_hash.finish());
  support::Sha256 cache_hash;
  add_field(cache_hash, manifest_digest);
  add_field(cache_hash, artifact_digest);
  add_field(cache_hash, toolchain_identity);
  add_field(cache_hash, platform == SystemVerilogDpiPluginPlatform::Msvc
      ? "msvc" : "posix");
  add_field(cache_hash, std::to_string(FSIM_DPI_PLUGIN_ABI_VERSION));
  add_field(cache_hash, std::to_string(sizeof(void*) * 8U));
  return {{manifest_digest, artifact_digest,
      support::Sha256::hex(cache_hash.finish())}, {}, {}};
}

bool systemverilog_dpi_plugin_artifact_loaded(
    const std::filesystem::path& artifact) noexcept {
  return platform::DynamicLibrary::is_loaded(artifact);
}

}  // namespace fsim::runtime
