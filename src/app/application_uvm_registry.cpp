// SPDX-License-Identifier: Apache-2.0
#include "application_uvm_registry.hpp"

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/uvm_factory.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_registry.hpp"
#include "fsim/support/uvm_role.hpp"

#include <algorithm>
#include <set>
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
  auto found = std::ranges::find(
      specializations, identity,
      &semantic::sv::ClassSpecialization::specialization_identity);
  // The class of a generated scope or design element instance has the
  // classes of the specialization it is made from as its bases (IEEE
  // 1800-2023 6.22, 27.4).
  if (found == specializations.end()) {
    found = std::ranges::find(
        specializations,
        semantic::sv::generated_specialization_source(identity),
        &semantic::sv::ClassSpecialization::specialization_identity);
  }
  if (found == specializations.end()) {
    throw std::out_of_range {
        "SystemVerilog class specialization '" + std::string { identity }
        + "' is not available for UVM registration"
    };
  }
  return *found;
}

// Whether the class declares the members the registration macros make
// (`uvm_object_utils`, IEEE 1800.2-2020 B.2.1): a static method hiding the
// library's uvm_object::get_type, and a method overriding its virtual
// get_object_type (IEEE 1800-2023 8.20).
[[nodiscard]] bool has_registry_utility_methods(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const semantic::sv::ClassSpecialization& specialization) {
  // The library's uvm_object of the class's base chain: a method of the
  // class that overrides or hides one of its methods replaces it in the
  // class's own method list.
  const semantic::sv::ClassSpecialization* library_object = nullptr;
  for (const auto* current = &specialization; current != nullptr;
       current = current->base
           ? &find_specialization(
                 specializations, current->base->specialization_identity)
           : nullptr) {
    if (current->uvm_role == support::UvmRole::object) {
      library_object = current;
      break;
    }
  }
  if (library_object == nullptr || library_object == &specialization)
    return false;
  const auto library_method = [&](const support::UvmRole role) {
    const auto found = std::ranges::find(library_object->methods, role,
        &semantic::sv::SpecializedClassMethod::uvm_role);
    return found == library_object->methods.end() ? nullptr : &*found;
  };
  const auto* library_get_type
      = library_method(support::UvmRole::object_get_type);
  const auto* library_get_object_type
      = library_method(support::UvmRole::object_get_object_type);
  if (library_get_type == nullptr || library_get_object_type == nullptr)
    return false;
  // Members of a class below uvm_object, the library's own classes that
  // use the macros included.
  const auto& library_identity = library_object->declaration_identity;
  const auto get_type = std::ranges::any_of(specialization.methods,
      [&](const semantic::sv::SpecializedClassMethod& method) {
        return method.owner_identity != library_identity
            && method.static_method
            && method.name == library_get_type->name;
      });
  const auto get_object_type = std::ranges::any_of(specialization.methods,
      [&](const semantic::sv::SpecializedClassMethod& method) {
        return method.owner_identity != library_identity
            && !method.static_method && method.virtual_slot
            && method.virtual_slot == library_get_object_type->virtual_slot;
      });
  return get_type && get_object_type;
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
    const support::UvmRole role) {
  for (const auto* current = &specialization; current != nullptr;
       current = current->base
           ? &find_specialization(
                 specializations, current->base->specialization_identity)
           : nullptr) {
    if (current->uvm_role == role)
      return true;
  }
  return false;
}

void ensure_systemverilog_uvm_object_type(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const semantic::sv::ClassSpecialization& specialization,
    runtime::SystemVerilogUvmObjectService& objects) {
  if (objects.contains_type(specialization.specialization_identity))
    return;
  runtime::SystemVerilogUvmObjectDescriptor descriptor;
  descriptor.specialization_identity = specialization.specialization_identity;
  descriptor.type_name
      = std::string { class_leaf_name(specialization.declaration_identity) };
  // The fields of the library's uvm_object are the object service's own.
  std::set<std::string_view> library_object;
  for (const auto* current = &specialization; current != nullptr;
       current = current->base
           ? &find_specialization(
                 specializations, current->base->specialization_identity)
           : nullptr) {
    if (current->uvm_role == support::UvmRole::object)
      library_object.insert(current->declaration_identity);
  }
  for (const auto& property : specialization.properties) {
    if (property.static_storage
        || library_object.contains(property.owner_identity)) {
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
            specializations, specialization, support::UvmRole::object)) {
      ensure_systemverilog_uvm_object_type(
          specializations, specialization, objects);
    }
  }
}

void register_systemverilog_uvm_registry_types(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    runtime::SystemVerilogUvmRegistryService& registry) {
  for (const auto& specialization : specializations) {
    if (!is_systemverilog_uvm_type(
            specializations, specialization, support::UvmRole::object)
        || !has_registry_utility_methods(specializations, specialization)) {
      continue;
    }
    (void)registry.register_type({
        0,
        specialization.specialization_identity,
        specialization.declaration_identity,
        registry_type_name(specialization),
        is_systemverilog_uvm_type(
            specializations, specialization, support::UvmRole::component)
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
          specializations, specialization, support::UvmRole::factory)) {
    return std::nullopt;
  }
  // The factory method the call names, by its recorded role: the method of
  // the class that declares it, which an override replaces in the
  // receiver's own list.
  auto method = support::UvmRole::none;
  for (const auto& candidate : specializations) {
    const auto found = std::ranges::find(candidate.methods,
        canonical_identity,
        &semantic::sv::SpecializedClassMethod::canonical_identity);
    if (found != candidate.methods.end()) {
      method = found->uvm_role;
      break;
    }
  }
  using support::UvmRole;
  if (method == UvmRole::factory_set_type_override_by_type
      && actuals.size() >= 2U) {
    const auto replace = actuals.size() < 3U
        || actuals[2].low_word().aval != 0;
    (void)factory.set_type_override_by_type(
        actuals[0].low_word().aval, actuals[1].low_word().aval, replace);
    return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
  }
  if (method == UvmRole::factory_find_override_by_type && !actuals.empty()) {
    const auto resolved = factory.resolve_by_type(actuals[0].low_word().aval);
    return runtime::PackedLogic4::from_aval_bval(64, resolved.resolved, 0);
  }
  if (method == UvmRole::factory_create_object_by_type && !actuals.empty()) {
    return runtime::PackedLogic4::from_aval_bval(
        64, factory.create_object_by_type(actuals[0].low_word().aval), 0);
  }
  if (method == UvmRole::factory_print) {
    (void)factory.report(actuals.empty() || actuals[0].low_word().aval != 0);
    return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
  }
  return std::nullopt;
}

}
