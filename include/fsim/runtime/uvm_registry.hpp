// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

using SystemVerilogUvmTypeHandle = std::uint64_t;

enum class SystemVerilogUvmRegisteredKind : std::uint8_t {
  Object,
  Component,
};

struct SystemVerilogUvmRegistryLimits {
  std::size_t maximum_types{65'536};
  std::size_t maximum_identity_bytes{16'384};
  std::size_t maximum_type_name_bytes{4'096};
};

struct SystemVerilogUvmRegisteredType {
  SystemVerilogUvmTypeHandle wrapper{};
  std::string specialization_identity;
  std::string declaration_identity;
  std::string type_name;
  SystemVerilogUvmRegisteredKind kind{
      SystemVerilogUvmRegisteredKind::Object};
  bool parameterized{};
  std::uint64_t registration_order{};
};

/// Simulation-owned UVM type-wrapper registry. Registration is exact and
/// append-only, wrapper and name lookup are deterministic, and creation is
/// checked against the registered kind and specialization before publication.
class SystemVerilogUvmRegistryService final {
 public:
  using ObjectCreateHook = std::function<SystemVerilogClassHandle(
      std::string_view specialization_identity,
      std::string_view name)>;
  using ComponentCreateHook = std::function<SystemVerilogClassHandle(
      std::string_view specialization_identity,
      std::string_view name,
      SystemVerilogClassHandle parent,
      SystemVerilogUvmRootHandle root)>;

  SystemVerilogUvmRegistryService(
      SystemVerilogClassHeap& heap,
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmComponentService& components,
      ObjectCreateHook object_create_hook,
      ComponentCreateHook component_create_hook,
      SystemVerilogUvmRegistryLimits limits = {});

  [[nodiscard]] SystemVerilogUvmTypeHandle register_type(
      SystemVerilogUvmRegisteredType descriptor);
  [[nodiscard]] bool contains(
      SystemVerilogUvmTypeHandle wrapper) const noexcept;
  [[nodiscard]] SystemVerilogUvmRegisteredType snapshot(
      SystemVerilogUvmTypeHandle wrapper) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisteredType> types() const;
  [[nodiscard]] SystemVerilogUvmTypeHandle wrapper_by_specialization(
      std::string_view specialization_identity) const noexcept;
  [[nodiscard]] SystemVerilogUvmTypeHandle wrapper_by_name(
      std::string_view type_name) const noexcept;
  [[nodiscard]] SystemVerilogUvmTypeHandle unique_wrapper_by_declaration(
      std::string_view declaration_identity) const;

  [[nodiscard]] SystemVerilogClassHandle create_object_by_type(
      SystemVerilogUvmTypeHandle wrapper,
      std::string_view name = {});
  [[nodiscard]] SystemVerilogClassHandle create_object_by_name(
      std::string_view type_name,
      std::string_view name = {});
  [[nodiscard]] SystemVerilogClassHandle create_component_by_type(
      SystemVerilogUvmTypeHandle wrapper,
      std::string_view name,
      SystemVerilogClassHandle parent = 0,
      SystemVerilogUvmRootHandle root = 0);
  [[nodiscard]] SystemVerilogClassHandle create_component_by_name(
      std::string_view type_name,
      std::string_view name,
      SystemVerilogClassHandle parent = 0,
      SystemVerilogUvmRootHandle root = 0);

  [[nodiscard]] std::size_t size() const noexcept { return types_.size(); }
  [[nodiscard]] const SystemVerilogUvmRegistryLimits& limits() const noexcept {
    return limits_;
  }

 private:
  [[nodiscard]] const SystemVerilogUvmRegisteredType& type(
      SystemVerilogUvmTypeHandle wrapper) const;
  void validate_name(std::string_view name) const;
  void rollback_created(
      SystemVerilogClassHandle object,
      SystemVerilogUvmRegisteredKind kind) noexcept;

  static constexpr SystemVerilogUvmTypeHandle first_wrapper_{
      0xffff'0000'0000'0001ULL};
  SystemVerilogClassHeap* heap_{};
  SystemVerilogUvmObjectService* objects_{};
  SystemVerilogUvmComponentService* components_{};
  ObjectCreateHook object_create_hook_;
  ComponentCreateHook component_create_hook_;
  SystemVerilogUvmRegistryLimits limits_;
  std::map<SystemVerilogUvmTypeHandle, SystemVerilogUvmRegisteredType> types_;
  std::map<std::string, SystemVerilogUvmTypeHandle, std::less<>>
      wrappers_by_specialization_;
  std::map<std::string, SystemVerilogUvmTypeHandle, std::less<>>
      wrappers_by_name_;
  std::multimap<std::string, SystemVerilogUvmTypeHandle, std::less<>>
      wrappers_by_declaration_;
  SystemVerilogUvmTypeHandle next_wrapper_{first_wrapper_};
  std::uint64_t next_registration_order_{};
};

}  // namespace fsim::runtime
