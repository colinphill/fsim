// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogUvmOverrideKind : std::uint8_t {
  Instance,
  Type,
};

struct SystemVerilogUvmFactoryLimits {
  std::size_t max_type_overrides{65'536};
  std::size_t max_instance_overrides{65'536};
  std::size_t max_type_name_bytes{4'096};
  std::size_t max_instance_path_bytes{16'384};
  std::size_t max_resolution_depth{1'024};
  std::size_t max_report_bytes{16U * 1'024U * 1'024U};
};

struct SystemVerilogUvmFactoryOverride {
  SystemVerilogUvmOverrideKind kind{SystemVerilogUvmOverrideKind::Type};
  SystemVerilogUvmTypeHandle original{};
  SystemVerilogUvmTypeHandle override{};
  std::string original_name;
  std::string instance_pattern;
  std::uint64_t registration_order{};
  std::uint64_t uses{};
};

struct SystemVerilogUvmFactoryTraceStep {
  SystemVerilogUvmOverrideKind kind{SystemVerilogUvmOverrideKind::Type};
  SystemVerilogUvmTypeHandle original{};
  SystemVerilogUvmTypeHandle override{};
  std::string original_name;
  std::string instance_pattern;
  std::uint64_t registration_order{};
};

struct SystemVerilogUvmFactoryResolution {
  SystemVerilogUvmTypeHandle requested{};
  SystemVerilogUvmTypeHandle resolved{};
  std::string requested_name;
  std::string full_instance_path;
  std::vector<SystemVerilogUvmFactoryTraceStep> steps;
};

class SystemVerilogUvmFactoryService final {
 public:
  explicit SystemVerilogUvmFactoryService(
      SystemVerilogUvmRegistryService& registry,
      SystemVerilogUvmFactoryLimits limits = {});

  [[nodiscard]] bool set_type_override_by_type(
      SystemVerilogUvmTypeHandle original,
      SystemVerilogUvmTypeHandle override,
      bool replace = true);
  [[nodiscard]] bool set_type_override_by_name(
      std::string_view original_name,
      std::string_view override_name,
      bool replace = true);
  [[nodiscard]] bool set_instance_override_by_type(
      SystemVerilogUvmTypeHandle original,
      SystemVerilogUvmTypeHandle override,
      std::string_view full_instance_pattern);
  [[nodiscard]] bool set_instance_override_by_name(
      std::string_view original_name,
      std::string_view override_name,
      std::string_view full_instance_pattern);

  [[nodiscard]] SystemVerilogUvmFactoryResolution resolve_by_type(
      SystemVerilogUvmTypeHandle requested,
      std::string_view full_instance_path = {});
  [[nodiscard]] SystemVerilogUvmFactoryResolution resolve_by_name(
      std::string_view requested_name,
      std::string_view full_instance_path = {});
  [[nodiscard]] SystemVerilogUvmFactoryResolution debug_resolve_by_type(
      SystemVerilogUvmTypeHandle requested,
      std::string_view full_instance_path = {}) const;
  [[nodiscard]] SystemVerilogUvmFactoryResolution debug_resolve_by_name(
      std::string_view requested_name,
      std::string_view full_instance_path = {}) const;

  [[nodiscard]] SystemVerilogClassHandle create_object_by_type(
      SystemVerilogUvmTypeHandle requested,
      std::string_view parent_instance_path = {},
      std::string_view name = {});
  [[nodiscard]] SystemVerilogClassHandle create_object_by_name(
      std::string_view requested_name,
      std::string_view parent_instance_path = {},
      std::string_view name = {});
  [[nodiscard]] SystemVerilogClassHandle create_component_by_type(
      SystemVerilogUvmTypeHandle requested,
      std::string_view parent_instance_path,
      std::string_view name,
      SystemVerilogClassHandle parent = 0,
      SystemVerilogUvmRootHandle root = 0);
  [[nodiscard]] SystemVerilogClassHandle create_component_by_name(
      std::string_view requested_name,
      std::string_view parent_instance_path,
      std::string_view name,
      SystemVerilogClassHandle parent = 0,
      SystemVerilogUvmRootHandle root = 0);

  [[nodiscard]] const std::vector<SystemVerilogUvmFactoryOverride>&
  type_overrides() const noexcept { return type_overrides_; }
  [[nodiscard]] const std::vector<SystemVerilogUvmFactoryOverride>&
  instance_overrides() const noexcept { return instance_overrides_; }
  [[nodiscard]] std::string report(bool all_types = true) const;
  [[nodiscard]] const SystemVerilogUvmFactoryLimits& limits() const noexcept {
    return limits_;
  }

 private:
  [[nodiscard]] SystemVerilogUvmFactoryResolution resolve(
      SystemVerilogUvmTypeHandle requested,
      std::string requested_name,
      std::string_view full_instance_path,
      bool count_uses) const;
  [[nodiscard]] std::string checked_type_name(
      SystemVerilogUvmTypeHandle wrapper) const;
  void validate_type_name(std::string_view name) const;
  void validate_instance_path(std::string_view path) const;
  [[nodiscard]] static std::string full_instance_path(
      std::string_view parent, std::string_view name);

  SystemVerilogUvmRegistryService* registry_{};
  SystemVerilogUvmFactoryLimits limits_;
  mutable std::vector<SystemVerilogUvmFactoryOverride> type_overrides_;
  mutable std::vector<SystemVerilogUvmFactoryOverride> instance_overrides_;
  std::uint64_t next_registration_order_{};
};

}
