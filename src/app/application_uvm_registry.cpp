// SPDX-License-Identifier: Apache-2.0
#include "application_uvm_registry.hpp"

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/uvm_factory.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_registry.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::app::application_detail {
namespace {

[[nodiscard]] std::string_view class_leaf_name(
    const std::string_view identity) {
  const auto separator = identity.rfind("::");
  return separator == std::string_view::npos
      ? identity : identity.substr(separator + 2U);
}

[[nodiscard]] const semantic::sv::ClassSpecialization&
find_specialization(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const std::string_view identity) {
  const auto found = std::ranges::find(
      specializations, identity,
      &semantic::sv::ClassSpecialization::specialization_identity);
  if (found == specializations.end()) {
    throw std::out_of_range {
        "SystemVerilog class specialization '" + std::string { identity }
        + "' is not available for UVM registration"
    };
  }
  return *found;
}

[[nodiscard]] bool has_registry_utility_methods(
    const semantic::sv::ClassSpecialization& specialization) {
  const auto get_type = std::ranges::find(
      specialization.methods, std::string_view { "get_type" },
      &semantic::sv::SpecializedClassMethod::name);
  const auto get_object_type = std::ranges::find(
      specialization.methods, std::string_view { "get_object_type" },
      &semantic::sv::SpecializedClassMethod::name);
  return get_type != specialization.methods.end() && get_type->static_method
      && get_object_type != specialization.methods.end()
      && !get_object_type->static_method;
}

[[nodiscard]] std::string registry_type_name(
    const semantic::sv::ClassSpecialization& specialization) {
  std::string result { class_leaf_name(specialization.declaration_identity) };
  if (specialization.parameters.empty())
    return result;
  result += "#(";
  for (std::size_t index = 0; index < specialization.parameters.size();
       ++index) {
    if (index != 0)
      result += ",";
    result += specialization.parameters[index].name;
    result += "=";
    result += specialization.parameters[index].display_identity;
  }
  result += ")";
  return result;
}

}

bool is_systemverilog_uvm_type(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const semantic::sv::ClassSpecialization& specialization,
    const std::string_view base_name) {
  const auto* current = &specialization;
  while (current != nullptr) {
    if (class_leaf_name(current->declaration_identity) == base_name)
      return true;
    current = current->base
        ? &find_specialization(
              specializations, current->base->specialization_identity)
        : nullptr;
  }
  const auto inherited_property = std::ranges::any_of(
      specialization.properties, [&](const auto& property) {
        return class_leaf_name(property.owner_identity) == base_name;
      });
  if (inherited_property)
    return true;
  const auto owner_fragment = "::" + std::string { base_name } + "::";
  return std::ranges::any_of(
      specialization.methods, [&](const auto& method) {
        return method.canonical_identity.find(owner_fragment)
            != std::string::npos;
      });
}

void ensure_systemverilog_uvm_object_type(
    const semantic::sv::ClassSpecialization& specialization,
    runtime::SystemVerilogUvmObjectService& objects) {
  if (objects.contains_type(specialization.specialization_identity))
    return;
  runtime::SystemVerilogUvmObjectDescriptor descriptor;
  descriptor.specialization_identity = specialization.specialization_identity;
  descriptor.type_name
      = std::string { class_leaf_name(specialization.declaration_identity) };
  for (const auto& property : specialization.properties) {
    if (property.static_storage
        || class_leaf_name(property.owner_identity) == "uvm_object") {
      continue;
    }
    descriptor.fields.push_back({
        property.owner_identity + "::" + property.name,
        runtime::SystemVerilogUvmFieldFlag::None
    });
  }
  objects.register_type(std::move(descriptor));
}

void register_systemverilog_uvm_object_types(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    runtime::SystemVerilogUvmObjectService& objects) {
  for (const auto& specialization : specializations) {
    if (is_systemverilog_uvm_type(
            specializations, specialization, "uvm_object")) {
      ensure_systemverilog_uvm_object_type(specialization, objects);
    }
  }
}

void register_systemverilog_uvm_registry_types(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
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
        !specialization.parameters.empty(),
        0
    });
  }
}

std::optional<runtime::PackedLogic4>
invoke_systemverilog_uvm_factory_method(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
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
      ? canonical_identity
      : canonical_identity.substr(separator + 2U);
  if (method == "set_type_override_by_type" && actuals.size() >= 2U) {
    const auto replace = actuals.size() < 3U
        || actuals[2].low_word().aval != 0;
    (void)factory.set_type_override_by_type(
        actuals[0].low_word().aval, actuals[1].low_word().aval, replace);
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
