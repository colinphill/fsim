// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application_tf.hpp"

#include <algorithm>
#include <iterator>
#include <new>
#include <utility>

namespace fsim::app {

bool tf_application_profile_supported(
    const frontend::StandardRevision profile) noexcept {
  switch (profile) {
    case frontend::StandardRevision::Verilog1995:
    case frontend::StandardRevision::Verilog2001:
    case frontend::StandardRevision::Verilog2001NoConfig:
    case frontend::StandardRevision::Verilog2005:
    case frontend::StandardRevision::SystemVerilog2005:
    case frontend::StandardRevision::SystemVerilog2009:
    case frontend::StandardRevision::SystemVerilog2012:
    case frontend::StandardRevision::SystemVerilog2017:
      return true;
    case frontend::StandardRevision::Vhdl1987:
    case frontend::StandardRevision::Vhdl1993:
    case frontend::StandardRevision::Vhdl2000:
    case frontend::StandardRevision::Vhdl2002:
    case frontend::StandardRevision::Vhdl2008:
      return false;
  }
  return false;
}

struct TfApplicationRegistry::Impl {
  std::vector<std::unique_ptr<runtime::TfLoadedPlugin>> plugins;
  std::vector<TfApplicationRegistration> registrations;
};

TfApplicationRegistry::TfApplicationRegistry()
    : impl_(std::make_unique<Impl>()) {}
TfApplicationRegistry::TfApplicationRegistry(TfApplicationRegistry&&) noexcept =
    default;
TfApplicationRegistry& TfApplicationRegistry::operator=(
    TfApplicationRegistry&&) noexcept = default;
TfApplicationRegistry::~TfApplicationRegistry() = default;

TfApplicationLoadResult TfApplicationRegistry::load(
    const std::filesystem::path& artifact) noexcept {
  if (impl_->plugins.size() >= kMaxTfApplicationPlugins) {
    return {.error = TfApplicationError::ResourceLimit,
            .plugin_error = runtime::TfPluginError::None,
            .plugin_index = 0,
            .registrations = {},
            .detail = {}};
  }
  auto loaded = runtime::load_tf_plugin(artifact);
  if (!loaded) {
    return {.error = TfApplicationError::Plugin,
            .plugin_error = loaded.error,
            .plugin_index = 0,
            .registrations = {},
            .detail = std::move(loaded.detail)};
  }
  const auto& registrations = loaded.value->registrations();
  if (registrations.size() >
      kMaxTfApplicationRegistrations - impl_->registrations.size()) {
    return {.error = TfApplicationError::ResourceLimit,
            .plugin_error = runtime::TfPluginError::None,
            .plugin_index = 0,
            .registrations = {},
            .detail = {}};
  }
  try {
    const auto plugin_index =
        static_cast<std::uint32_t>(impl_->plugins.size());
    std::vector<TfApplicationRegistration> additions;
    additions.reserve(registrations.size());
    for (std::size_t index = 0; index < registrations.size(); ++index) {
      const auto& candidate = registrations[index];
      if (std::ranges::any_of(
              impl_->registrations, [&](const auto& existing) {
                return existing.name == candidate.name;
              }) ||
          std::ranges::any_of(additions, [&](const auto& existing) {
            return existing.name == candidate.name;
          })) {
        return {.error = TfApplicationError::DuplicateRegistration,
                .plugin_error = runtime::TfPluginError::None,
                .plugin_index = 0,
                .registrations = {},
                .detail = {}};
      }
      additions.push_back({.name = candidate.name,
                           .kind = candidate.kind,
                           .plugin_index = plugin_index,
                           .registration_index =
                               static_cast<std::uint32_t>(index)});
    }
    auto published = additions;
    impl_->plugins.reserve(impl_->plugins.size() + 1U);
    impl_->registrations.reserve(impl_->registrations.size() +
                                 additions.size());
    impl_->plugins.push_back(std::move(loaded.value));
    impl_->registrations.insert(
        impl_->registrations.end(),
        std::make_move_iterator(additions.begin()),
        std::make_move_iterator(additions.end()));
    return {.error = TfApplicationError::None,
            .plugin_error = runtime::TfPluginError::None,
            .plugin_index = plugin_index,
            .registrations = std::move(published),
            .detail = {}};
  } catch (const std::bad_alloc&) {
    return {.error = TfApplicationError::Allocation,
            .plugin_error = runtime::TfPluginError::None,
            .plugin_index = 0,
            .registrations = {},
            .detail = {}};
  } catch (...) {
    return {.error = TfApplicationError::Allocation,
            .plugin_error = runtime::TfPluginError::None,
            .plugin_index = 0,
            .registrations = {},
            .detail = {}};
  }
}

TfApplicationResolveResult TfApplicationRegistry::resolve(
    const frontend::StandardRevision profile, const std::string_view name,
    const std::optional<runtime::TfRegistrationKind> expected_kind) const
    noexcept {
  if (!tf_application_profile_supported(profile)) {
    return {.error = TfApplicationError::UnsupportedProfile,
            .registration = {}};
  }
  const auto found = std::ranges::find(
      impl_->registrations, name, &TfApplicationRegistration::name);
  if (found == impl_->registrations.end()) {
    return {.error = TfApplicationError::MissingRegistration,
            .registration = {}};
  }
  if (expected_kind.has_value() && found->kind != *expected_kind) {
    return {.error = TfApplicationError::KindMismatch, .registration = {}};
  }
  try {
    return {.error = TfApplicationError::None, .registration = *found};
  } catch (...) {
    return {.error = TfApplicationError::Allocation, .registration = {}};
  }
}

TfApplicationBindResult TfApplicationRegistry::bind(
    const frontend::StandardRevision profile, const std::string_view name,
    const std::span<const runtime::TfArgument> arguments,
    const runtime::TfInstanceIdentity instance,
    const runtime::TfTimeProfile time_profile,
    const runtime::TfContextProfile& context_profile) const noexcept {
  const auto resolved = resolve(profile, name);
  if (!resolved) {
    return {.error = resolved.error, .binding = {}};
  }
  auto binding = impl_->plugins[resolved.registration.plugin_index]->bind(
      resolved.registration.registration_index, arguments, instance,
      time_profile, context_profile);
  if (!binding) {
    return {.error = TfApplicationError::Binding,
            .binding = std::move(binding)};
  }
  return {.error = TfApplicationError::None,
          .binding = std::move(binding)};
}

std::uint32_t TfApplicationRegistry::plugin_count() const noexcept {
  return static_cast<std::uint32_t>(impl_->plugins.size());
}

std::uint32_t TfApplicationRegistry::registration_count() const noexcept {
  return static_cast<std::uint32_t>(impl_->registrations.size());
}

}  // namespace fsim::app
