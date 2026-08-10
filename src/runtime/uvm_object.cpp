// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_object.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] std::size_t property_size(
    const SystemVerilogClassPropertyValue& value) {
  if (value.packed.width() != 0) return value.packed.width();
  if (value.kind == SystemVerilogClassPropertyKind::String) {
    if (value.string.size() > std::numeric_limits<std::size_t>::max() / 8U) {
      throw std::length_error{"UVM string print size overflow"};
    }
    return value.string.size() * 8U;
  }
  if (value.kind == SystemVerilogClassPropertyKind::ClassHandle) return 64;
  return value.handle_container ? value.handle_container->size()
                                : value.handles.size();
}

[[nodiscard]] std::string property_text(
    const SystemVerilogClassPropertyValue& value) {
  if (value.packed.width() != 0) return value.packed.to_msb_string();
  if (value.kind == SystemVerilogClassPropertyKind::String) {
    return value.string;
  }
  if (value.kind == SystemVerilogClassPropertyKind::ClassHandle) {
    return value.handle == 0
        ? std::string{"null"}
        : "@" + std::to_string(value.handle);
  }
  if (value.handle_container) {
    return "size=" + std::to_string(value.handle_container->size());
  }
  return "size=" + std::to_string(value.handles.size());
}

[[nodiscard]] SystemVerilogUvmObjectEntryKind property_entry_kind(
    const SystemVerilogClassPropertyValue& value) {
  if (value.packed.width() != 0) {
    return SystemVerilogUvmObjectEntryKind::Packed;
  }
  if (value.kind == SystemVerilogClassPropertyKind::String) {
    return SystemVerilogUvmObjectEntryKind::String;
  }
  if (value.kind == SystemVerilogClassPropertyKind::ClassHandle
      && value.handle == 0) {
    return SystemVerilogUvmObjectEntryKind::NullHandle;
  }
  return SystemVerilogUvmObjectEntryKind::Container;
}

}  // namespace

SystemVerilogUvmObjectService::SystemVerilogUvmObjectService(
    SystemVerilogClassHeap& heap,
    CreateHook create_hook,
    const SystemVerilogUvmObjectLimits limits)
    : heap_(&heap),
      create_hook_(std::move(create_hook)),
      limits_(limits),
      owner_(std::make_shared<unsigned char>()) {
  if (!create_hook_) {
    throw std::invalid_argument{"UVM object creation hook must not be empty"};
  }
  if (limits_.maximum_depth == 0 || limits_.maximum_objects == 0
      || limits_.maximum_fields == 0
      || limits_.maximum_output_bytes == 0) {
    throw std::invalid_argument{"UVM object resource limits must be positive"};
  }
}

void SystemVerilogUvmObjectService::register_type(
    SystemVerilogUvmObjectDescriptor descriptor_value) {
  if (descriptor_value.specialization_identity.empty()
      || descriptor_value.type_name.empty()) {
    throw std::invalid_argument{
        "UVM object type requires specialization and type-name identities"};
  }
  std::set<std::string> fields;
  for (const auto& field : descriptor_value.fields) {
    if (field.property.empty() || !fields.insert(field.property).second) {
      throw std::invalid_argument{
          "UVM field automation requires unique nonempty properties"};
    }
    const auto recursion_flags =
        static_cast<unsigned>(
            has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference))
        + static_cast<unsigned>(
            has_flag(field.flags, SystemVerilogUvmFieldFlag::Shallow))
        + static_cast<unsigned>(
            has_flag(field.flags, SystemVerilogUvmFieldFlag::Deep));
    if (recursion_flags > 1
        || (has_flag(field.flags, SystemVerilogUvmFieldFlag::Physical)
            && has_flag(field.flags, SystemVerilogUvmFieldFlag::Abstract))) {
      throw SystemVerilogUvmObjectPolicyError{
          "FSIM-UVM-POLICY-001",
          "UVM field automation has conflicting recursion or abstraction flags"};
    }
  }
  const auto identity = descriptor_value.specialization_identity;
  if (!descriptors_.emplace(identity, std::move(descriptor_value)).second) {
    throw std::invalid_argument{
        "duplicate UVM object specialization '" + identity + "'"};
  }
}

bool SystemVerilogUvmObjectService::contains_type(
    const std::string_view specialization_identity) const noexcept {
  return descriptors_.contains(specialization_identity);
}

void SystemVerilogUvmObjectService::initialize(
    const SystemVerilogClassHandle object,
    std::string object_name) {
  (void)descriptor(object);
  if (metadata_.contains(object)) {
    throw std::invalid_argument{"UVM object is already initialized"};
  }
  if (next_instance_id_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"UVM object instance identifier exhausted"};
  }
  metadata_.emplace(
      object, Metadata{std::move(object_name), {}, next_instance_id_++});
}

bool SystemVerilogUvmObjectService::contains(
    const SystemVerilogClassHandle object) const noexcept {
  if (!heap_->contains(object) || !metadata_.contains(object)) return false;
  const auto& value = heap_->object(object);
  return descriptors_.contains(value.specialization_identity);
}

SystemVerilogUvmObjectHandle SystemVerilogUvmObjectService::object_handle(
    const SystemVerilogClassHandle object) const {
  if (!contains(object)) {
    throw SystemVerilogUvmCopyError{
        "FSIM-UVM-COPY-001", "UVM object handle is empty or stale"};
  }
  return SystemVerilogUvmObjectHandle{owner_, object};
}

bool SystemVerilogUvmObjectService::contains(
    const SystemVerilogUvmObjectHandle& object) const noexcept {
  return object.valid() && object.owner_ == owner_ && contains(object.object_);
}

void SystemVerilogUvmObjectService::set_name(
    const SystemVerilogClassHandle object,
    std::string object_name) {
  metadata(object).name = std::move(object_name);
}

void SystemVerilogUvmObjectService::set_full_name(
    const SystemVerilogClassHandle object,
    std::string object_full_name) {
  metadata(object).full_name = std::move(object_full_name);
}

std::string_view SystemVerilogUvmObjectService::name(
    const SystemVerilogClassHandle object) const {
  return metadata(object).name;
}

std::string SystemVerilogUvmObjectService::full_name(
    const SystemVerilogClassHandle object) const {
  const auto& object_metadata = metadata(object);
  if (!object_metadata.full_name.empty()) {
    return object_metadata.full_name;
  }
  const auto& type = descriptor(object);
  if (type.get_full_name) return type.get_full_name(object);
  return object_metadata.name;
}

std::string SystemVerilogUvmObjectService::type_name(
    const SystemVerilogClassHandle object) const {
  return descriptor(object).type_name;
}

std::uint64_t SystemVerilogUvmObjectService::instance_id(
    const SystemVerilogClassHandle object) const {
  return metadata(object).instance_id;
}

void SystemVerilogUvmObjectService::erase(
    const SystemVerilogClassHandle object) noexcept {
  metadata_.erase(object);
}

SystemVerilogClassHandle SystemVerilogUvmObjectService::create(
    const SystemVerilogClassHandle prototype,
    std::string object_name) {
  const auto& source = heap_->object(prototype);
  (void)descriptor(prototype);
  const auto specialization_identity = source.specialization_identity;
  const auto declared_type = source.declared_type;
  const auto created = create_hook_(
      specialization_identity, declared_type, object_name);
  if (created == 0 || !heap_->contains(created)) {
    throw std::runtime_error{"UVM create hook did not return a live object"};
  }
  const auto& result = heap_->object(created);
  if (result.specialization_identity != specialization_identity) {
    throw std::invalid_argument{
        "UVM create hook returned a different specialization"};
  }
  if (!metadata_.contains(created)) {
    initialize(created, std::move(object_name));
  } else {
    set_name(created, std::move(object_name));
  }
  return created;
}

bool SystemVerilogUvmObjectService::compare(
    const SystemVerilogClassHandle left,
    const SystemVerilogClassHandle right) {
  return compare_detailed(left, right).equal();
}

std::vector<SystemVerilogUvmObjectEntry>
SystemVerilogUvmObjectService::print(
    const SystemVerilogClassHandle object) {
  std::map<SystemVerilogClassHandle, std::string> first_paths;
  std::set<SystemVerilogClassHandle> active;
  std::vector<SystemVerilogUvmObjectEntry> entries;
  std::size_t objects{};
  std::size_t fields{};
  std::size_t bytes{};
  const auto account_field = [&] {
    if (++fields > limits_.maximum_fields) {
      throw std::length_error{"UVM print field budget exceeded"};
    }
  };
  const auto append = [&](SystemVerilogUvmObjectEntry entry) {
    const auto added = entry.path.size() + entry.type_name.size()
        + entry.value.size();
    if (added > limits_.maximum_output_bytes - bytes) {
      throw std::length_error{"UVM print output budget exceeded"};
    }
    bytes += added;
    entries.push_back(std::move(entry));
  };
  std::function<void(SystemVerilogClassHandle, std::string, std::size_t)> walk;
  walk = [&](const SystemVerilogClassHandle current,
             std::string path,
             const std::size_t depth) {
    if (current == 0) {
      append({std::move(path), {}, "null", 0,
              SystemVerilogUvmObjectEntryKind::NullHandle, depth, 64});
      return;
    }
    if (depth >= limits_.maximum_depth) {
      throw std::length_error{"UVM print recursion depth exceeded"};
    }
    if (const auto found = first_paths.find(current);
        found != first_paths.end()) {
      append({std::move(path), type_name(current), found->second, current,
              active.contains(current)
                  ? SystemVerilogUvmObjectEntryKind::Cycle
                  : SystemVerilogUvmObjectEntryKind::Reference,
              depth, 64});
      return;
    }
    if (++objects > limits_.maximum_objects) {
      throw std::length_error{"UVM print object budget exceeded"};
    }
    first_paths.emplace(current, path);
    active.insert(current);
    const auto& type = descriptor(current);
    append({path, type_name(current), std::string{name(current)}, current,
            SystemVerilogUvmObjectEntryKind::Object, depth,
            type.fields.size()});
    for (const auto& field : type.fields) {
      if (has_flag(field.flags, SystemVerilogUvmFieldFlag::NoPrint)) continue;
      account_field();
      const auto field_path = path + "." + field.property;
      const auto& value = heap_->property(current, field.property);
      if (value.kind == SystemVerilogClassPropertyKind::ClassHandle
          && !has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)) {
        walk(value.handle, field_path, depth + 1U);
      } else if (value.kind == SystemVerilogClassPropertyKind::Container) {
        const auto container_size = value.handle_container
            ? value.handle_container->size()
            : value.handles.size();
        append({field_path, {}, "size=" + std::to_string(container_size),
                current, SystemVerilogUvmObjectEntryKind::Container,
                depth + 1U, container_size});
        const auto emit_handle = [&](const SystemVerilogClassHandle handle,
                                     std::string element_path) {
          account_field();
          if (has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)) {
            append({std::move(element_path), {},
                    handle == 0 ? std::string{"null"}
                                : "@" + std::to_string(handle),
                    handle, handle == 0
                        ? SystemVerilogUvmObjectEntryKind::NullHandle
                        : SystemVerilogUvmObjectEntryKind::Reference,
                    depth + 2U, 64});
          } else {
            walk(handle, std::move(element_path), depth + 2U);
          }
        };
        for (std::size_t index = 0; index < value.handles.size(); ++index) {
          emit_handle(value.handles[index],
                      field_path + "[" + std::to_string(index) + "]");
        }
        if (value.handle_container) {
          const auto sequential =
              value.handle_container->sequential_values();
          for (std::size_t index = 0; index < sequential.size(); ++index) {
            emit_handle(sequential[index],
                        field_path + "[" + std::to_string(index) + "]");
          }
          for (const auto& [key, handle] :
               value.handle_container->keyed_values()) {
            std::string escaped_key;
            escaped_key.reserve(key.size());
            for (const auto character : key) {
              if (character == '"' || character == '\\') {
                escaped_key.push_back('\\');
              }
              escaped_key.push_back(character);
            }
            emit_handle(
                handle, field_path + "[\"" + escaped_key + "\"]");
          }
        }
      } else {
        const auto referenced =
            has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference);
        append({field_path, {}, property_text(value),
                referenced ? value.handle : current,
                referenced
                    ? SystemVerilogUvmObjectEntryKind::Reference
                    : property_entry_kind(value),
                depth + 1U, property_size(value)});
      }
    }
    if (type.do_print) type.do_print(current, entries);
    active.erase(current);
  };
  auto root = full_name(object);
  if (root.empty()) root = "<object>";
  walk(object, std::move(root), 0);
  if (entries.size() > limits_.maximum_fields + limits_.maximum_objects) {
    throw std::length_error{"UVM print hook entry budget exceeded"};
  }
  bytes = 0;
  for (const auto& entry : entries) {
    const auto added = entry.path.size() + entry.type_name.size()
        + entry.value.size();
    if (added > limits_.maximum_output_bytes - bytes) {
      throw std::length_error{"UVM print hook output budget exceeded"};
    }
    bytes += added;
  }
  if (print_hook_) print_hook_(entries);
  return entries;
}

std::vector<SystemVerilogUvmObjectEntry>
SystemVerilogUvmObjectService::record(
    const SystemVerilogClassHandle object) {
  std::map<SystemVerilogClassHandle, std::string> first_paths;
  std::set<SystemVerilogClassHandle> active;
  std::vector<SystemVerilogUvmObjectEntry> entries;
  std::size_t objects{};
  std::size_t fields{};
  std::size_t bytes{};
  const auto append = [&](SystemVerilogUvmObjectEntry entry) {
    const auto added = entry.path.size() + entry.type_name.size()
        + entry.value.size();
    if (added > limits_.maximum_output_bytes - bytes) {
      throw std::length_error{"UVM record output budget exceeded"};
    }
    bytes += added;
    entries.push_back(std::move(entry));
  };
  std::function<void(SystemVerilogClassHandle, std::string, std::size_t)> walk;
  walk = [&](const SystemVerilogClassHandle current,
             std::string path,
             const std::size_t depth) {
    if (current == 0) {
      append({std::move(path), {}, "null", 0,
              SystemVerilogUvmObjectEntryKind::NullHandle, depth});
      return;
    }
    if (depth >= limits_.maximum_depth) {
      throw std::length_error{"UVM record recursion depth exceeded"};
    }
    if (const auto found = first_paths.find(current);
        found != first_paths.end()) {
      append({std::move(path), type_name(current), found->second, current,
              active.contains(current)
                  ? SystemVerilogUvmObjectEntryKind::Cycle
                  : SystemVerilogUvmObjectEntryKind::Reference,
              depth});
      return;
    }
    if (++objects > limits_.maximum_objects) {
      throw std::length_error{"UVM record object budget exceeded"};
    }
    first_paths.emplace(current, path);
    active.insert(current);
    append({path, type_name(current), std::string{name(current)}, current,
            SystemVerilogUvmObjectEntryKind::Object, depth});
    const auto& type = descriptor(current);
    for (const auto& field : type.fields) {
      if (++fields > limits_.maximum_fields) {
        throw std::length_error{"UVM record field budget exceeded"};
      }
      if (has_flag(field.flags, SystemVerilogUvmFieldFlag::NoRecord)) continue;
      const auto field_path = path + "." + field.property;
      const auto& value = heap_->property(current, field.property);
      if (value.kind == SystemVerilogClassPropertyKind::ClassHandle
          && !has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)) {
        walk(value.handle, field_path, depth + 1U);
      } else {
        append({field_path, {}, property_text(value), current,
                has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)
                    ? SystemVerilogUvmObjectEntryKind::Reference
                    : property_entry_kind(value),
                depth + 1U});
      }
    }
    if (type.do_record) type.do_record(current, entries);
    active.erase(current);
  };
  auto root = full_name(object);
  if (root.empty()) root = "<object>";
  walk(object, std::move(root), 0);
  if (entries.size() > limits_.maximum_fields + limits_.maximum_objects) {
    throw std::length_error{"UVM record hook entry budget exceeded"};
  }
  bytes = 0;
  for (const auto& entry : entries) {
    const auto added = entry.path.size() + entry.type_name.size()
        + entry.value.size();
    if (added > limits_.maximum_output_bytes - bytes) {
      throw std::length_error{"UVM record hook output budget exceeded"};
    }
    bytes += added;
  }
  if (record_hook_) record_hook_(entries);
  return entries;
}

const SystemVerilogUvmObjectDescriptor&
SystemVerilogUvmObjectService::descriptor(
    const SystemVerilogClassHandle object) const {
  const auto& class_object = heap_->object(object);
  const auto found = descriptors_.find(class_object.specialization_identity);
  if (found == descriptors_.end()) {
    throw std::out_of_range{
        "class specialization is not registered as a UVM object: " +
        class_object.specialization_identity};
  }
  return found->second;
}

SystemVerilogUvmObjectService::Metadata&
SystemVerilogUvmObjectService::metadata(
    const SystemVerilogClassHandle object) {
  (void)heap_->object(object);
  const auto found = metadata_.find(object);
  if (found == metadata_.end()) {
    throw std::out_of_range{"class handle is not an initialized UVM object"};
  }
  return found->second;
}

const SystemVerilogUvmObjectService::Metadata&
SystemVerilogUvmObjectService::metadata(
    const SystemVerilogClassHandle object) const {
  (void)heap_->object(object);
  const auto found = metadata_.find(object);
  if (found == metadata_.end()) {
    throw std::out_of_range{"class handle is not an initialized UVM object"};
  }
  return found->second;
}

}  // namespace fsim::runtime
