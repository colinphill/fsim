// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_uvm_registry.hpp"

#include <ranges>

namespace fsim::app::application_detail {
namespace {

[[nodiscard]] std::string_view class_leaf_name(
    const std::string_view identity) {
  const auto separator = identity.rfind("::");
  return separator == std::string_view::npos
      ? identity : identity.substr(separator + 2U);
}

[[nodiscard]] const frontend::SystemVerilogClassSpecialization&
find_specialization(
    const std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    const std::string_view identity) {
  const auto found = std::ranges::find(
      specializations, identity,
      &frontend::SystemVerilogClassSpecialization::specialization_identity);
  if (found == specializations.end()) {
    throw std::out_of_range{
        "SystemVerilog class specialization '" + std::string{identity}
        + "' is not available for UVM registration"};
  }
  return *found;
}

[[nodiscard]] bool has_registry_utility_methods(
    const frontend::SystemVerilogClassSpecialization& specialization) {
  const auto get_type = std::ranges::find(
      specialization.methods, std::string_view{"get_type"},
      &frontend::SystemVerilogClassMethodProfile::name);
  const auto get_object_type = std::ranges::find(
      specialization.methods, std::string_view{"get_object_type"},
      &frontend::SystemVerilogClassMethodProfile::name);
  return get_type != specialization.methods.end() && get_type->is_static
      && get_object_type != specialization.methods.end()
      && !get_object_type->is_static;
}

[[nodiscard]] std::string registry_type_name(
    const frontend::SystemVerilogClassSpecialization& specialization) {
  std::string result{class_leaf_name(specialization.declaration_identity)};
  if (specialization.parameter_values.empty()) return result;
  result += "#(";
  for (std::size_t index = 0;
       index < specialization.parameter_values.size(); ++index) {
    if (index != 0) result += ",";
    result += specialization.parameter_values[index].first;
    result += "=";
    result += specialization.parameter_values[index].second;
  }
  result += ")";
  return result;
}

}

bool is_systemverilog_uvm_type(
    const std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::string_view base_name) {
  const auto* current = &specialization;
  while (current != nullptr) {
    if (class_leaf_name(current->declaration_identity) == base_name) return true;
    current = current->base_specialization_identity.empty()
        ? nullptr
        : &find_specialization(
              specializations, current->base_specialization_identity);
  }
  return false;
}

void register_systemverilog_uvm_object_types(
    const std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    runtime::SystemVerilogUvmObjectService& objects) {
  for (const auto& specialization : specializations) {
    if (!is_systemverilog_uvm_type(
            specializations, specialization, "uvm_object")) {
      continue;
    }
    runtime::SystemVerilogUvmObjectDescriptor descriptor;
    descriptor.specialization_identity = specialization.specialization_identity;
    descriptor.type_name =
        std::string{class_leaf_name(specialization.declaration_identity)};
    for (const auto& property : specialization.properties) {
      if (property.is_static
          || class_leaf_name(property.owner_identity) == "uvm_object") {
        continue;
      }
      descriptor.fields.push_back({
          property.owner_identity + "::" + property.name,
          runtime::SystemVerilogUvmFieldFlag::None});
    }
    objects.register_type(std::move(descriptor));
  }
}

void register_systemverilog_uvm_registry_types(
    const std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    runtime::SystemVerilogUvmRegistryService& registry) {
  for (const auto& specialization : specializations) {
    if (!is_systemverilog_uvm_type(
            specializations, specialization, "uvm_object")
        || !has_registry_utility_methods(specialization)) {
      continue;
    }
    (void)registry.register_type({
        0,
        specialization.specialization_identity,
        specialization.declaration_identity,
        registry_type_name(specialization),
        is_systemverilog_uvm_type(
            specializations, specialization, "uvm_component")
            ? runtime::SystemVerilogUvmRegisteredKind::Component
            : runtime::SystemVerilogUvmRegisteredKind::Object,
        !specialization.parameter_values.empty(),
        0});
  }
}

std::optional<runtime::PackedLogic4>
invoke_systemverilog_uvm_factory_method(
    const std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    const runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogUvmFactoryService& factory,
    const std::uint64_t receiver,
    const std::string_view canonical_identity,
    const std::span<const runtime::PackedLogic4> actuals) {
  const auto& specialization = find_specialization(
      specializations, heap.object(receiver).specialization_identity);
  if (!is_systemverilog_uvm_type(
          specializations, specialization, "uvm_factory")) {
    return std::nullopt;
  }
  const auto separator = canonical_identity.rfind("::");
  const auto method = separator == std::string_view::npos
      ? canonical_identity : canonical_identity.substr(separator + 2U);
  if (method == "set_type_override_by_type" && actuals.size() >= 2U) {
    const auto replace = actuals.size() < 3U
        || actuals[2].low_word().aval != 0;
    (void)factory.set_type_override_by_type(
        actuals[0].low_word().aval,
        actuals[1].low_word().aval,
        replace);
    return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
  }
  if (method == "find_override_by_type" && !actuals.empty()) {
    const auto resolved = factory.resolve_by_type(actuals[0].low_word().aval);
    return runtime::PackedLogic4::from_aval_bval(64, resolved.resolved, 0);
  }
  if (method == "create_object_by_type" && !actuals.empty()) {
    return runtime::PackedLogic4::from_aval_bval(
        64, factory.create_object_by_type(actuals[0].low_word().aval), 0);
  }
  if (method == "print") {
    (void)factory.report(actuals.empty() || actuals[0].low_word().aval != 0);
    return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
  }
  return std::nullopt;
}

}
