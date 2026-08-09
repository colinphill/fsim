// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_object.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] bool same_property_shape(
    const SystemVerilogClassPropertyValue& left,
    const SystemVerilogClassPropertyValue& right) {
  return left.kind == right.kind
      && left.packed.width() == right.packed.width()
      && left.handle_container.has_value()
          == right.handle_container.has_value();
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
      limits_(limits) {
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

SystemVerilogClassHandle SystemVerilogUvmObjectService::clone(
    const SystemVerilogClassHandle source) {
  const auto result = create(source, std::string{name(source)});
  try {
    copy(result, source);
  } catch (...) {
    metadata_.erase(result);
    (void)heap_->release(result);
    throw;
  }
  return result;
}

void SystemVerilogUvmObjectService::copy(
    const SystemVerilogClassHandle destination,
    const SystemVerilogClassHandle source) {
  if (destination == 0 || source == 0) {
    throw std::invalid_argument{"UVM copy requires two non-null objects"};
  }
  if (type_name(destination) != type_name(source)) {
    throw std::invalid_argument{"UVM copy requires matching object types"};
  }

  struct Context {
    std::map<SystemVerilogClassHandle, SystemVerilogClassHandle> mapped;
    std::map<SystemVerilogClassHandle, SystemVerilogClassObject> originals;
    std::vector<SystemVerilogClassHandle> created;
    std::size_t objects{};
    std::size_t fields{};
  } context;
  context.mapped.emplace(source, destination);

  const auto account_object = [&](const std::size_t depth) {
    if (depth >= limits_.maximum_depth) {
      throw std::length_error{"UVM copy recursion depth exceeded"};
    }
    if (++context.objects > limits_.maximum_objects) {
      throw std::length_error{"UVM copy object budget exceeded"};
    }
  };
  const auto account_field = [&] {
    if (++context.fields > limits_.maximum_fields) {
      throw std::length_error{"UVM copy field budget exceeded"};
    }
  };

  std::function<void(
      SystemVerilogClassHandle,
      SystemVerilogClassHandle,
      std::size_t)> copy_object;
  std::function<SystemVerilogClassHandle(
      SystemVerilogClassHandle,
      std::string_view,
      std::size_t)> copy_handle;

  copy_handle = [&](const SystemVerilogClassHandle source_handle,
                    const std::string_view child_name,
                    const std::size_t depth) {
    if (source_handle == 0) return SystemVerilogClassHandle{};
    if (const auto found = context.mapped.find(source_handle);
        found != context.mapped.end()) {
      return found->second;
    }
    const auto created = create(source_handle, std::string{child_name});
    context.created.push_back(created);
    context.mapped.emplace(source_handle, created);
    copy_object(created, source_handle, depth);
    return created;
  };

  copy_object = [&](const SystemVerilogClassHandle target_handle,
                    const SystemVerilogClassHandle source_handle,
                    const std::size_t depth) {
    account_object(depth);
    if (type_name(target_handle) != type_name(source_handle)) {
      throw std::invalid_argument{
          "UVM recursive copy encountered mismatched object types"};
    }
    context.originals.try_emplace(
        target_handle, heap_->object(target_handle));
    const auto& type = descriptor(source_handle);
    for (const auto& field : type.fields) {
      account_field();
      if (has_flag(field.flags, SystemVerilogUvmFieldFlag::NoCopy)) continue;
      const auto source_value = heap_->property(source_handle, field.property);
      if (!same_property_shape(
              heap_->property(target_handle, field.property), source_value)) {
        throw std::invalid_argument{
            "UVM field copy encountered incompatible property shapes"};
      }
      if (source_value.packed.width() != 0) {
        heap_->property(target_handle, field.property).packed =
            source_value.packed;
      } else if (source_value.kind == SystemVerilogClassPropertyKind::String) {
        heap_->property(target_handle, field.property).string =
            source_value.string;
      } else if (
          source_value.kind == SystemVerilogClassPropertyKind::ClassHandle) {
        const auto copied_handle =
            has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)
            ? source_value.handle
            : copy_handle(source_value.handle, field.property, depth + 1U);
        heap_->property(target_handle, field.property).handle = copied_handle;
      } else if (!source_value.handles.empty()) {
        std::vector<SystemVerilogClassHandle> copied_handles;
        copied_handles.reserve(source_value.handles.size());
        for (std::size_t index = 0;
             index < source_value.handles.size(); ++index) {
          copied_handles.push_back(
              has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)
              ? source_value.handles[index]
              : copy_handle(
                    source_value.handles[index],
                    field.property + "[" + std::to_string(index) + "]",
                    depth + 1U));
        }
        heap_->property(target_handle, field.property).handles =
            std::move(copied_handles);
      } else {
        heap_->property(target_handle, field.property).handle_container =
            source_value.handle_container;
      }
    }
    if (type.do_copy) type.do_copy(target_handle, source_handle);
  };

  try {
    copy_object(destination, source, 0);
  } catch (...) {
    for (auto& [handle, original] : context.originals) {
      if (heap_->contains(handle)) heap_->object(handle) = std::move(original);
    }
    for (auto handle = context.created.rbegin();
         handle != context.created.rend(); ++handle) {
      metadata_.erase(*handle);
      (void)heap_->release(*handle);
    }
    throw;
  }
}

bool SystemVerilogUvmObjectService::compare(
    const SystemVerilogClassHandle left,
    const SystemVerilogClassHandle right) {
  if (left == 0 || right == 0) return left == right;

  std::map<SystemVerilogClassHandle, SystemVerilogClassHandle> left_to_right;
  std::map<SystemVerilogClassHandle, SystemVerilogClassHandle> right_to_left;
  std::size_t objects{};
  std::size_t fields{};
  std::function<bool(
      SystemVerilogClassHandle,
      SystemVerilogClassHandle,
      std::size_t)> compare_object;
  compare_object = [&](const SystemVerilogClassHandle lhs,
                       const SystemVerilogClassHandle rhs,
                       const std::size_t depth) {
    if (lhs == 0 || rhs == 0) return lhs == rhs;
    if (depth >= limits_.maximum_depth) {
      throw std::length_error{"UVM compare recursion depth exceeded"};
    }
    if (const auto mapped = left_to_right.find(lhs);
        mapped != left_to_right.end()) {
      return mapped->second == rhs;
    }
    if (right_to_left.contains(rhs)) return false;
    if (++objects > limits_.maximum_objects) {
      throw std::length_error{"UVM compare object budget exceeded"};
    }
    if (type_name(lhs) != type_name(rhs)) return false;
    left_to_right.emplace(lhs, rhs);
    right_to_left.emplace(rhs, lhs);
    const auto& type = descriptor(lhs);
    bool equal = true;
    for (const auto& field : type.fields) {
      if (++fields > limits_.maximum_fields) {
        throw std::length_error{"UVM compare field budget exceeded"};
      }
      if (has_flag(field.flags, SystemVerilogUvmFieldFlag::NoCompare)) {
        continue;
      }
      const auto& left_value = heap_->property(lhs, field.property);
      const auto& right_value = heap_->property(rhs, field.property);
      if (!same_property_shape(left_value, right_value)) return false;
      if (left_value.packed.width() != 0) {
        equal = equal && left_value.packed == right_value.packed;
      } else if (
          left_value.kind == SystemVerilogClassPropertyKind::String) {
        equal = equal && left_value.string == right_value.string;
      } else if (
          left_value.kind == SystemVerilogClassPropertyKind::ClassHandle) {
        equal = equal
            && (has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)
                    ? left_value.handle == right_value.handle
                    : compare_object(
                          left_value.handle, right_value.handle, depth + 1U));
      } else if (!left_value.handles.empty()
                 || !right_value.handles.empty()) {
        if (left_value.handles.size() != right_value.handles.size()) {
          equal = false;
        } else {
          for (std::size_t index = 0;
               equal && index < left_value.handles.size(); ++index) {
            equal = has_flag(
                        field.flags, SystemVerilogUvmFieldFlag::Reference)
                ? left_value.handles[index] == right_value.handles[index]
                : compare_object(
                      left_value.handles[index],
                      right_value.handles[index], depth + 1U);
          }
        }
      } else {
        equal = equal
            && left_value.handle_container.has_value()
                == right_value.handle_container.has_value()
            && (!left_value.handle_container
                || left_value.handle_container->size()
                    == right_value.handle_container->size());
      }
    }
    if (type.do_compare) equal = type.do_compare(lhs, rhs) && equal;
    return equal;
  };
  return compare_object(left, right, 0);
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
              SystemVerilogUvmObjectEntryKind::NullHandle, depth});
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
              depth});
      return;
    }
    if (++objects > limits_.maximum_objects) {
      throw std::length_error{"UVM print object budget exceeded"};
    }
    first_paths.emplace(current, path);
    active.insert(current);
    append({path, type_name(current), std::string{name(current)}, current,
            SystemVerilogUvmObjectEntryKind::Object, depth});
    const auto& type = descriptor(current);
    for (const auto& field : type.fields) {
      if (++fields > limits_.maximum_fields) {
        throw std::length_error{"UVM print field budget exceeded"};
      }
      if (has_flag(field.flags, SystemVerilogUvmFieldFlag::NoPrint)) continue;
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
