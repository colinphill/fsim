// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/token.hpp"
#include "fsim/runtime/tf_plugin.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

inline constexpr std::uint32_t kMaxTfApplicationPlugins = 256;
inline constexpr std::uint32_t kMaxTfApplicationRegistrations = 1U << 16U;

enum class TfApplicationError {
  None,
  Plugin,
  ResourceLimit,
  DuplicateRegistration,
  UnsupportedProfile,
  MissingRegistration,
  KindMismatch,
  Binding,
  Allocation,
};

struct TfApplicationRegistration {
  std::string name;
  runtime::TfRegistrationKind kind{runtime::TfRegistrationKind::Task};
  std::uint32_t plugin_index{};
  std::uint32_t registration_index{};
};

struct TfApplicationLoadResult {
  TfApplicationError error{TfApplicationError::None};
  runtime::TfPluginError plugin_error{runtime::TfPluginError::None};
  std::uint32_t plugin_index{};
  std::vector<TfApplicationRegistration> registrations;
  std::string detail;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfApplicationError::None;
  }
};

struct TfApplicationResolveResult {
  TfApplicationError error{TfApplicationError::None};
  TfApplicationRegistration registration;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfApplicationError::None;
  }
};

struct TfApplicationBindResult {
  TfApplicationError error{TfApplicationError::None};
  runtime::TfBindResult binding;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfApplicationError::None &&
           static_cast<bool>(binding);
  }
};

class TfApplicationRegistry final {
public:
  TfApplicationRegistry();
  TfApplicationRegistry(TfApplicationRegistry&&) noexcept;
  TfApplicationRegistry& operator=(TfApplicationRegistry&&) noexcept;
  TfApplicationRegistry(const TfApplicationRegistry&) = delete;
  TfApplicationRegistry& operator=(const TfApplicationRegistry&) = delete;
  ~TfApplicationRegistry();

  [[nodiscard]] TfApplicationLoadResult load(
      const std::filesystem::path& artifact) noexcept;
  [[nodiscard]] TfApplicationResolveResult resolve(
      frontend::StandardRevision profile, std::string_view name,
      std::optional<runtime::TfRegistrationKind> expected_kind =
          std::nullopt) const noexcept;
  [[nodiscard]] TfApplicationBindResult bind(
      frontend::StandardRevision profile, std::string_view name,
      std::span<const runtime::TfArgument> arguments = {},
      runtime::TfInstanceIdentity instance = {},
      runtime::TfTimeProfile time_profile = {},
      const runtime::TfContextProfile& context_profile = {}) const noexcept;

  [[nodiscard]] std::uint32_t plugin_count() const noexcept;
  [[nodiscard]] std::uint32_t registration_count() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool tf_application_profile_supported(
    frontend::StandardRevision profile) noexcept;

}  // namespace fsim::app
