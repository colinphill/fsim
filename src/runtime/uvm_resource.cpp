// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_resource.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fsim::runtime {
namespace {

bool glob_matches(
    const std::string_view pattern,
    const std::string_view value) noexcept {
  std::size_t pattern_index{};
  std::size_t value_index{};
  std::size_t star{std::string_view::npos};
  std::size_t star_value{};
  while (value_index < value.size()) {
    if (pattern_index < pattern.size()
        && (pattern[pattern_index] == '?'
            || pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size()
               && pattern[pattern_index] == '*') {
      star = pattern_index++;
      star_value = value_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1;
      value_index = ++star_value;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size()
         && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

std::size_t edit_distance(
    const std::string_view left,
    const std::string_view right) {
  std::vector<std::size_t> previous(right.size() + 1);
  std::vector<std::size_t> current(right.size() + 1);
  for (std::size_t index{}; index <= right.size(); ++index) {
    previous[index] = index;
  }
  for (std::size_t left_index{}; left_index < left.size(); ++left_index) {
    current[0] = left_index + 1;
    for (std::size_t right_index{};
         right_index < right.size(); ++right_index) {
      const auto substitution = previous[right_index]
          + static_cast<std::size_t>(
              left[left_index] != right[right_index]);
      current[right_index + 1] = std::min({
          previous[right_index + 1] + 1,
          current[right_index] + 1,
          substitution});
    }
    previous.swap(current);
  }
  return previous.back();
}

const char* value_kind_name(
    const SystemVerilogUvmResourceValueKind kind) noexcept {
  switch (kind) {
    case SystemVerilogUvmResourceValueKind::Packed: return "packed";
    case SystemVerilogUvmResourceValueKind::Real: return "real";
    case SystemVerilogUvmResourceValueKind::String: return "string";
    case SystemVerilogUvmResourceValueKind::Object: return "object";
  }
  return "unknown";
}

}  // namespace

SystemVerilogUvmResourcePoolService::SystemVerilogUvmResourcePoolService(
    SystemVerilogClassHeap* heap,
    SystemVerilogUvmResourceLimits limits)
    : heap_(heap), limits_(limits) {}

SystemVerilogUvmResourceHandle
SystemVerilogUvmResourcePoolService::insert(
    SystemVerilogUvmResourceDescriptor descriptor) {
  validate_descriptor(descriptor);
  if (resources_.size() >= limits_.max_resources) {
    throw std::length_error{"UVM resource pool capacity exceeded"};
  }
  if (next_handle_ == 0
      || next_handle_ == std::numeric_limits<
          SystemVerilogUvmResourceHandle>::max()) {
    throw std::overflow_error{"UVM resource handles exhausted"};
  }
  SystemVerilogUvmResource resource;
  resource.handle = next_handle_++;
  resource.name = std::move(descriptor.name);
  resource.scope_pattern = std::move(descriptor.scope_pattern);
  resource.type = std::move(descriptor.type);
  resource.value = std::move(descriptor.value);
  resource.precedence = descriptor.precedence;
  resource.read_only = descriptor.read_only;
  resource.auditing = descriptor.auditing;
  resource.registration_order = next_registration_order_++;
  const auto handle = resource.handle;
  resources_.emplace(handle, StoredResource{std::move(resource), {}});
  return handle;
}

bool SystemVerilogUvmResourcePoolService::erase(
    const SystemVerilogUvmResourceHandle handle) noexcept {
  return resources_.erase(handle) != 0;
}

bool SystemVerilogUvmResourcePoolService::contains(
    const SystemVerilogUvmResourceHandle handle) const noexcept {
  return resources_.contains(handle);
}

SystemVerilogUvmResource SystemVerilogUvmResourcePoolService::snapshot(
    const SystemVerilogUvmResourceHandle handle) const {
  return stored(handle).resource;
}

void SystemVerilogUvmResourcePoolService::set_scope(
    const SystemVerilogUvmResourceHandle handle,
    std::string scope_pattern) {
  if (scope_pattern.size() > limits_.max_scope_bytes) {
    throw std::length_error{"UVM resource scope exceeds its byte budget"};
  }
  stored(handle).resource.scope_pattern = std::move(scope_pattern);
}

void SystemVerilogUvmResourcePoolService::set_precedence(
    const SystemVerilogUvmResourceHandle handle,
    const std::int64_t precedence) {
  stored(handle).resource.precedence = precedence;
}

void SystemVerilogUvmResourcePoolService::set_priority(
    const SystemVerilogUvmResourceHandle handle,
    const SystemVerilogUvmResourcePriority priority) {
  auto& resource = stored(handle).resource;
  if (priority == SystemVerilogUvmResourcePriority::High) {
    if (next_high_priority_ == std::numeric_limits<std::int64_t>::max()) {
      throw std::overflow_error{"UVM resource high-priority order exhausted"};
    }
    resource.priority_order = ++next_high_priority_;
  } else {
    if (next_low_priority_ == std::numeric_limits<std::int64_t>::min()) {
      throw std::overflow_error{"UVM resource low-priority order exhausted"};
    }
    resource.priority_order = --next_low_priority_;
  }
}

void SystemVerilogUvmResourcePoolService::set_read_only(
    const SystemVerilogUvmResourceHandle handle,
    const bool read_only) {
  stored(handle).resource.read_only = read_only;
}

void SystemVerilogUvmResourcePoolService::set_auditing(
    const SystemVerilogUvmResourceHandle handle,
    const bool auditing) {
  stored(handle).resource.auditing = auditing;
}

std::vector<SystemVerilogUvmResourceHandle>
SystemVerilogUvmResourcePoolService::lookup_name(
    const std::string_view scope,
    const std::string_view name,
    const std::optional<std::string_view> type_identity) const {
  validate_lookup(scope, name);
  if (type_identity
      && type_identity->size() > limits_.max_type_identity_bytes) {
    throw std::length_error{"UVM resource type identity exceeds its budget"};
  }
  std::vector<SystemVerilogUvmResourceHandle> result;
  for (const auto& [handle, stored_resource] : resources_) {
    const auto& resource = stored_resource.resource;
    if (resource.name != name
        || !glob_matches(resource.scope_pattern, scope)
        || (type_identity && resource.type.identity != *type_identity)) {
      continue;
    }
    if (result.size() >= limits_.max_lookup_results) {
      throw std::length_error{"UVM resource lookup result budget exceeded"};
    }
    result.push_back(handle);
  }
  std::sort(
      result.begin(), result.end(),
      [this](const auto left, const auto right) {
        const auto& lhs = resources_.at(left).resource;
        const auto& rhs = resources_.at(right).resource;
        if (lhs.precedence != rhs.precedence) {
          return lhs.precedence > rhs.precedence;
        }
        if (lhs.priority_order != rhs.priority_order) {
          return lhs.priority_order > rhs.priority_order;
        }
        return lhs.registration_order < rhs.registration_order;
      });
  return result;
}

SystemVerilogUvmResourceHandle
SystemVerilogUvmResourcePoolService::get_by_name(
    const std::string_view scope,
    const std::string_view name,
    const std::optional<std::string_view> type_identity) const {
  const auto matches = lookup_name(scope, name, type_identity);
  return matches.empty() ? 0 : matches.front();
}

std::vector<SystemVerilogUvmResourceHandle>
SystemVerilogUvmResourcePoolService::lookup_type(
    const std::string_view scope,
    const std::string_view type_identity) const {
  validate_lookup(scope, {});
  if (type_identity.size() > limits_.max_type_identity_bytes) {
    throw std::length_error{"UVM resource type identity exceeds its budget"};
  }
  std::vector<SystemVerilogUvmResourceHandle> result;
  for (const auto& [handle, stored_resource] : resources_) {
    const auto& resource = stored_resource.resource;
    if (resource.type.identity != type_identity
        || !glob_matches(resource.scope_pattern, scope)) {
      continue;
    }
    if (result.size() >= limits_.max_lookup_results) {
      throw std::length_error{"UVM resource lookup result budget exceeded"};
    }
    result.push_back(handle);
  }
  std::sort(
      result.begin(), result.end(),
      [this](const auto left, const auto right) {
        const auto& lhs = resources_.at(left).resource;
        const auto& rhs = resources_.at(right).resource;
        if (lhs.precedence != rhs.precedence) {
          return lhs.precedence > rhs.precedence;
        }
        if (lhs.priority_order != rhs.priority_order) {
          return lhs.priority_order > rhs.priority_order;
        }
        return lhs.registration_order < rhs.registration_order;
      });
  return result;
}

SystemVerilogUvmResourceHandle
SystemVerilogUvmResourcePoolService::get_by_type(
    const std::string_view scope,
    const std::string_view type_identity) const {
  const auto matches = lookup_type(scope, type_identity);
  return matches.empty() ? 0 : matches.front();
}

SystemVerilogUvmResourceValue SystemVerilogUvmResourcePoolService::read(
    const SystemVerilogUvmResourceHandle handle,
    const std::string_view accessor) {
  if (accessor.size() > limits_.max_accessor_bytes) {
    throw std::length_error{"UVM resource accessor exceeds its byte budget"};
  }
  validate_value(stored(handle).resource.type, stored(handle).resource.value);
  notify(handle, SystemVerilogUvmResourceCallbackEvent::PreRead, accessor);
  auto& resource = stored(handle).resource;
  const auto value = resource.value;
  ++resource.read_count;
  if (resource.auditing) {
    append_audit(
        resource, SystemVerilogUvmResourceAuditAction::Read,
        accessor, true);
  }
  notify(handle, SystemVerilogUvmResourceCallbackEvent::PostRead, accessor);
  return value;
}

bool SystemVerilogUvmResourcePoolService::write(
    const SystemVerilogUvmResourceHandle handle,
    SystemVerilogUvmResourceValue value,
    const std::string_view accessor) {
  if (accessor.size() > limits_.max_accessor_bytes) {
    throw std::length_error{"UVM resource accessor exceeds its byte budget"};
  }
  auto& initial = stored(handle).resource;
  validate_value(initial.type, value);
  if (initial.read_only) {
    if (initial.auditing) {
      append_audit(
          initial, SystemVerilogUvmResourceAuditAction::RejectedWrite,
          accessor, false);
    }
    return false;
  }
  notify(handle, SystemVerilogUvmResourceCallbackEvent::PreWrite, accessor);
  auto& resource = stored(handle).resource;
  if (resource.read_only) {
    if (resource.auditing) {
      append_audit(
          resource, SystemVerilogUvmResourceAuditAction::RejectedWrite,
          accessor, false);
    }
    return false;
  }
  validate_value(resource.type, value);
  resource.value = std::move(value);
  ++resource.revision;
  ++resource.write_count;
  if (resource.auditing) {
    append_audit(
        resource, SystemVerilogUvmResourceAuditAction::Write,
        accessor, true);
  }
  notify(handle, SystemVerilogUvmResourceCallbackEvent::PostWrite, accessor);
  return true;
}

SystemVerilogUvmResourceCallbackToken
SystemVerilogUvmResourcePoolService::add_callback(
    const SystemVerilogUvmResourceHandle handle,
    Callback callback) {
  if (!callback) {
    throw std::invalid_argument{"UVM resource callback must be callable"};
  }
  auto& callbacks = stored(handle).callbacks;
  if (callbacks.size() >= limits_.max_callbacks_per_resource) {
    throw std::length_error{"UVM resource callback budget exceeded"};
  }
  if (next_callback_token_ == 0
      || next_callback_token_ == std::numeric_limits<
          SystemVerilogUvmResourceCallbackToken>::max()) {
    throw std::overflow_error{"UVM resource callback tokens exhausted"};
  }
  const auto token = next_callback_token_++;
  callbacks.emplace_back(token, std::move(callback));
  return token;
}

bool SystemVerilogUvmResourcePoolService::remove_callback(
    const SystemVerilogUvmResourceHandle handle,
    const SystemVerilogUvmResourceCallbackToken token) noexcept {
  const auto found = resources_.find(handle);
  if (found == resources_.end()) return false;
  auto& callbacks = found->second.callbacks;
  const auto callback = std::find_if(
      callbacks.begin(), callbacks.end(),
      [token](const auto& entry) { return entry.first == token; });
  if (callback == callbacks.end()) return false;
  callbacks.erase(callback);
  return true;
}

std::vector<std::string> SystemVerilogUvmResourcePoolService::spell_check(
    const std::string_view name,
    const std::size_t maximum_distance) const {
  if (name.size() > limits_.max_name_bytes) {
    throw std::length_error{"UVM resource spelling input exceeds its budget"};
  }
  if (maximum_distance > limits_.max_spell_distance) {
    throw std::length_error{"UVM resource spelling distance exceeds its budget"};
  }
  std::map<std::string, std::size_t, std::less<>> candidates;
  for (const auto& [handle, stored_resource] : resources_) {
    (void)handle;
    const auto& candidate = stored_resource.resource.name;
    if (candidates.contains(candidate)) continue;
    const auto distance = edit_distance(name, candidate);
    if (distance <= maximum_distance) {
      candidates.emplace(candidate, distance);
    }
  }
  std::vector<std::pair<std::size_t, std::string>> ordered;
  ordered.reserve(candidates.size());
  for (const auto& [candidate, distance] : candidates) {
    ordered.emplace_back(distance, candidate);
  }
  std::sort(ordered.begin(), ordered.end());
  if (ordered.size() > limits_.max_spell_candidates) {
    ordered.resize(limits_.max_spell_candidates);
  }
  std::vector<std::string> result;
  result.reserve(ordered.size());
  for (auto& [distance, candidate] : ordered) {
    (void)distance;
    result.push_back(std::move(candidate));
  }
  return result;
}

std::string SystemVerilogUvmResourcePoolService::report() const {
  std::string result{"UVM Resource Pool\n"};
  const auto append = [this, &result](const std::string& text) {
    if (text.size() > limits_.max_report_bytes -
            std::min(result.size(), limits_.max_report_bytes)) {
      throw std::length_error{"UVM resource report byte budget exceeded"};
    }
    result += text;
  };
  for (const auto& [handle, stored_resource] : resources_) {
    const auto& resource = stored_resource.resource;
    std::ostringstream line;
    line << handle << ' ' << resource.type.identity << ' '
         << value_kind_name(resource.type.kind) << ' '
         << resource.name << " @ " << resource.scope_pattern
         << " precedence=" << resource.precedence
         << " priority=" << resource.priority_order
         << " revision=" << resource.revision
         << " reads=" << resource.read_count
         << " writes=" << resource.write_count << '\n';
    append(line.str());
  }
  return result;
}

SystemVerilogUvmResourcePoolService::StoredResource&
SystemVerilogUvmResourcePoolService::stored(
    const SystemVerilogUvmResourceHandle handle) {
  const auto found = resources_.find(handle);
  if (found == resources_.end()) {
    throw std::invalid_argument{"unknown UVM resource handle"};
  }
  return found->second;
}

const SystemVerilogUvmResourcePoolService::StoredResource&
SystemVerilogUvmResourcePoolService::stored(
    const SystemVerilogUvmResourceHandle handle) const {
  const auto found = resources_.find(handle);
  if (found == resources_.end()) {
    throw std::invalid_argument{"unknown UVM resource handle"};
  }
  return found->second;
}

void SystemVerilogUvmResourcePoolService::validate_descriptor(
    const SystemVerilogUvmResourceDescriptor& descriptor) const {
  if (descriptor.name.size() > limits_.max_name_bytes) {
    throw std::length_error{"UVM resource name exceeds its byte budget"};
  }
  if (descriptor.scope_pattern.size() > limits_.max_scope_bytes) {
    throw std::length_error{"UVM resource scope exceeds its byte budget"};
  }
  if (descriptor.type.identity.empty()) {
    throw std::invalid_argument{"UVM resource type identity must not be empty"};
  }
  if (descriptor.type.identity.size() > limits_.max_type_identity_bytes) {
    throw std::length_error{"UVM resource type identity exceeds its budget"};
  }
  if (descriptor.type.packed_width > limits_.max_packed_width) {
    throw std::length_error{"UVM resource packed width exceeds its budget"};
  }
  validate_value(descriptor.type, descriptor.value);
}

void SystemVerilogUvmResourcePoolService::validate_value(
    const SystemVerilogUvmResourceType& type,
    const SystemVerilogUvmResourceValue& value) const {
  switch (type.kind) {
    case SystemVerilogUvmResourceValueKind::Packed: {
      const auto* packed = std::get_if<PackedLogic4>(&value);
      if (!packed || packed->width() != type.packed_width) {
        throw std::invalid_argument{
            "UVM packed resource value does not match its declared width"};
      }
      break;
    }
    case SystemVerilogUvmResourceValueKind::Real:
      if (!std::holds_alternative<double>(value)) {
        throw std::invalid_argument{"UVM real resource requires a real value"};
      }
      break;
    case SystemVerilogUvmResourceValueKind::String: {
      const auto* string = std::get_if<std::string>(&value);
      if (!string) {
        throw std::invalid_argument{
            "UVM string resource requires a string value"};
      }
      if (string->size() > limits_.max_string_value_bytes) {
        throw std::length_error{"UVM resource string exceeds its byte budget"};
      }
      break;
    }
    case SystemVerilogUvmResourceValueKind::Object: {
      if (!heap_) {
        throw std::invalid_argument{
            "UVM object resource requires a class heap"};
      }
      const auto* object = std::get_if<SystemVerilogClassHandle>(&value);
      if (!object || (*object != 0 && !heap_->contains(*object))) {
        throw std::invalid_argument{
            "UVM object resource requires a null or live class handle"};
      }
      break;
    }
  }
}

void SystemVerilogUvmResourcePoolService::validate_lookup(
    const std::string_view scope,
    const std::string_view name) const {
  if (scope.size() > limits_.max_scope_bytes) {
    throw std::length_error{"UVM resource lookup scope exceeds its budget"};
  }
  if (name.size() > limits_.max_name_bytes) {
    throw std::length_error{"UVM resource lookup name exceeds its budget"};
  }
}

void SystemVerilogUvmResourcePoolService::append_audit(
    const SystemVerilogUvmResource& resource,
    const SystemVerilogUvmResourceAuditAction action,
    const std::string_view accessor,
    const bool success) {
  if (limits_.max_audit_records == 0) {
    ++dropped_audit_records_;
    return;
  }
  if (audit_records_.size() == limits_.max_audit_records) {
    audit_records_.pop_front();
    ++dropped_audit_records_;
  }
  audit_records_.push_back({
      next_audit_sequence_++, resource.handle, action,
      std::string{accessor}, success, resource.revision});
}

void SystemVerilogUvmResourcePoolService::notify(
    const SystemVerilogUvmResourceHandle handle,
    const SystemVerilogUvmResourceCallbackEvent event,
    const std::string_view accessor) {
  const auto resource_snapshot = stored(handle).resource;
  const auto callbacks = stored(handle).callbacks;
  for (const auto& [token, callback] : callbacks) {
    (void)token;
    try {
      callback(event, resource_snapshot);
    } catch (...) {
      append_audit(
          resource_snapshot,
          SystemVerilogUvmResourceAuditAction::CallbackFailure,
          accessor, false);
    }
  }
}

}  // namespace fsim::runtime
