// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_registry.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

SystemVerilogUvmRegistryService::SystemVerilogUvmRegistryService(
    SystemVerilogClassHeap& heap,
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmComponentService& components,
    ObjectCreateHook object_create_hook,
    ComponentCreateHook component_create_hook,
    SystemVerilogUvmRegistryLimits limits)
    : heap_(&heap),
      objects_(&objects),
      components_(&components),
      object_create_hook_(std::move(object_create_hook)),
      component_create_hook_(std::move(component_create_hook)),
      limits_(limits) {
  if (!object_create_hook_ || !component_create_hook_) {
    throw std::invalid_argument{"UVM registry creation hooks must be present"};
  }
  if (limits_.maximum_types == 0 || limits_.maximum_identity_bytes == 0
      || limits_.maximum_type_name_bytes == 0) {
    throw std::invalid_argument{"UVM registry limits must all be positive"};
  }
}

SystemVerilogUvmTypeHandle SystemVerilogUvmRegistryService::register_type(
    SystemVerilogUvmRegisteredType descriptor) {
  if (descriptor.specialization_identity.empty()
      || descriptor.declaration_identity.empty()
      || descriptor.type_name.empty()) {
    throw std::invalid_argument{
        "UVM registered type requires specialization, declaration, and name"};
  }
  if (descriptor.specialization_identity.size()
          > limits_.maximum_identity_bytes
      || descriptor.declaration_identity.size()
          > limits_.maximum_identity_bytes) {
    throw std::length_error{"UVM registered type identity exceeds the limit"};
  }
  if (descriptor.type_name.size() > limits_.maximum_type_name_bytes) {
    throw std::length_error{"UVM registered type name exceeds the limit"};
  }
  if (types_.size() >= limits_.maximum_types) {
    throw std::length_error{"UVM registered type budget exceeded"};
  }
  if (wrappers_by_specialization_.contains(
          descriptor.specialization_identity)) {
    throw std::invalid_argument{"duplicate UVM registered specialization"};
  }
  if (wrappers_by_name_.contains(descriptor.type_name)) {
    throw std::invalid_argument{"duplicate UVM registered type name"};
  }
  if (next_wrapper_ == 0
      || next_wrapper_ == std::numeric_limits<std::uint64_t>::max()
      || next_registration_order_
          == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"UVM registered type identity exhausted"};
  }

  const auto wrapper = next_wrapper_++;
  descriptor.wrapper = wrapper;
  descriptor.registration_order = next_registration_order_++;
  const auto specialization = descriptor.specialization_identity;
  const auto declaration = descriptor.declaration_identity;
  const auto type_name = descriptor.type_name;
  types_.emplace(wrapper, std::move(descriptor));
  try {
    wrappers_by_specialization_.emplace(specialization, wrapper);
    wrappers_by_name_.emplace(type_name, wrapper);
    wrappers_by_declaration_.emplace(declaration, wrapper);
  } catch (...) {
    wrappers_by_specialization_.erase(specialization);
    wrappers_by_name_.erase(type_name);
    const auto [begin, end] = wrappers_by_declaration_.equal_range(declaration);
    for (auto found = begin; found != end; ++found) {
      if (found->second == wrapper) {
        wrappers_by_declaration_.erase(found);
        break;
      }
    }
    types_.erase(wrapper);
    throw;
  }
  return wrapper;
}

bool SystemVerilogUvmRegistryService::contains(
    const SystemVerilogUvmTypeHandle wrapper) const noexcept {
  return wrapper != 0 && types_.contains(wrapper);
}

const SystemVerilogUvmRegisteredType&
SystemVerilogUvmRegistryService::type(
    const SystemVerilogUvmTypeHandle wrapper) const {
  const auto found = types_.find(wrapper);
  if (wrapper == 0 || found == types_.end()) {
    throw std::out_of_range{"null or stale UVM type wrapper"};
  }
  return found->second;
}

SystemVerilogUvmRegisteredType SystemVerilogUvmRegistryService::snapshot(
    const SystemVerilogUvmTypeHandle wrapper) const {
  return type(wrapper);
}

std::vector<SystemVerilogUvmRegisteredType>
SystemVerilogUvmRegistryService::types() const {
  std::vector<SystemVerilogUvmRegisteredType> result;
  result.reserve(types_.size());
  for (const auto& [wrapper, descriptor] : types_) {
    (void)wrapper;
    result.push_back(descriptor);
  }
  return result;
}

SystemVerilogUvmTypeHandle
SystemVerilogUvmRegistryService::wrapper_by_specialization(
    const std::string_view specialization_identity) const noexcept {
  const auto found = wrappers_by_specialization_.find(
      specialization_identity);
  return found == wrappers_by_specialization_.end() ? 0 : found->second;
}

SystemVerilogUvmTypeHandle SystemVerilogUvmRegistryService::wrapper_by_name(
    const std::string_view type_name) const noexcept {
  const auto found = wrappers_by_name_.find(type_name);
  return found == wrappers_by_name_.end() ? 0 : found->second;
}

SystemVerilogUvmTypeHandle
SystemVerilogUvmRegistryService::unique_wrapper_by_declaration(
    const std::string_view declaration_identity) const {
  const auto [begin, end] = wrappers_by_declaration_.equal_range(
      declaration_identity);
  if (begin == end) return 0;
  const auto result = begin->second;
  auto next = begin;
  ++next;
  if (next != end) {
    throw std::invalid_argument{
        "UVM declaration has multiple registered specializations"};
  }
  return result;
}

void SystemVerilogUvmRegistryService::validate_name(
    const std::string_view name) const {
  if (name.size() > components_->limits().maximum_name_bytes) {
    throw std::length_error{"UVM created object name exceeds the limit"};
  }
}

void SystemVerilogUvmRegistryService::rollback_created(
    const SystemVerilogClassHandle object,
    const SystemVerilogUvmRegisteredKind) noexcept {
  if (components_->contains(object)) {
    try {
      components_->release(object);
    } catch (...) {
    }
    return;
  }
  objects_->erase(object);
  if (heap_->contains(object)) (void)heap_->release(object);
}

SystemVerilogClassHandle
SystemVerilogUvmRegistryService::create_object_by_type(
    const SystemVerilogUvmTypeHandle wrapper,
    const std::string_view name) {
  validate_name(name);
  const auto& registered = type(wrapper);
  if (registered.kind != SystemVerilogUvmRegisteredKind::Object) {
    throw std::invalid_argument{"UVM component wrapper used to create object"};
  }
  const auto result = object_create_hook_(
      registered.specialization_identity, name);
  if (result == 0 || !objects_->contains(result)
      || heap_->object(result).specialization_identity
          != registered.specialization_identity
      || components_->contains(result)) {
    rollback_created(result, registered.kind);
    throw std::invalid_argument{
        "UVM object creation returned a mismatched specialization"};
  }
  return result;
}

SystemVerilogClassHandle
SystemVerilogUvmRegistryService::create_object_by_name(
    const std::string_view type_name,
    const std::string_view name) {
  const auto wrapper = wrapper_by_name(type_name);
  if (wrapper == 0) {
    throw std::out_of_range{"unknown UVM registered type name"};
  }
  return create_object_by_type(wrapper, name);
}

SystemVerilogClassHandle
SystemVerilogUvmRegistryService::create_component_by_type(
    const SystemVerilogUvmTypeHandle wrapper,
    const std::string_view name,
    const SystemVerilogClassHandle parent,
    const SystemVerilogUvmRootHandle root) {
  validate_name(name);
  const auto& registered = type(wrapper);
  if (registered.kind != SystemVerilogUvmRegisteredKind::Component) {
    throw std::invalid_argument{"UVM object wrapper used to create component"};
  }
  const auto result = component_create_hook_(
      registered.specialization_identity, name, parent, root);
  if (result == 0 || !components_->contains(result)
      || heap_->object(result).specialization_identity
          != registered.specialization_identity
      || (parent != 0 && components_->parent(result) != parent)
      || (root != 0 && components_->root_of(result) != root)) {
    rollback_created(result, registered.kind);
    throw std::invalid_argument{
        "UVM component creation returned a mismatched specialization"};
  }
  return result;
}

SystemVerilogClassHandle
SystemVerilogUvmRegistryService::create_component_by_name(
    const std::string_view type_name,
    const std::string_view name,
    const SystemVerilogClassHandle parent,
    const SystemVerilogUvmRootHandle root) {
  const auto wrapper = wrapper_by_name(type_name);
  if (wrapper == 0) {
    throw std::out_of_range{"unknown UVM registered type name"};
  }
  return create_component_by_type(wrapper, name, parent, root);
}

}  // namespace fsim::runtime
