// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_object.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <limits>
#include <ranges>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::uint64_t slot_mask = (1ULL << 24U) - 1U;
constexpr std::uint64_t generation_mask = (1ULL << 16U) - 1U;
constexpr std::uint64_t registry_mask = (1ULL << 22U) - 1U;
constexpr std::uint64_t vhpi_tag = 1ULL << 62U;
constexpr std::uint64_t iterator_tag = 1ULL << 63U;
constexpr std::uint32_t maximum_registry_identity = (1U << 22U) - 1U;
constexpr std::size_t maximum_iterator_objects = 1U << 20U;
constexpr std::size_t maximum_identifier_size = 4096;
constexpr std::size_t maximum_full_name_size = 1U << 20U;
constexpr std::size_t maximum_source_size = 1U << 20U;
constexpr std::size_t maximum_index_dimensions = 32;
constexpr std::size_t maximum_package_dependencies = 256;
std::atomic<std::uint32_t> next_registry_identity{1};

VhdlVhpiIteratorError iterator_error(
    const VhdlVhpiObjectError error) noexcept {
  switch (error) {
    case VhdlVhpiObjectError::InvalidSimulation:
      return VhdlVhpiIteratorError::InvalidSimulation;
    case VhdlVhpiObjectError::CrossSimulation:
      return VhdlVhpiIteratorError::CrossSimulation;
    case VhdlVhpiObjectError::StaleHandle:
      return VhdlVhpiIteratorError::StaleHandle;
    case VhdlVhpiObjectError::ReleasedHandle:
      return VhdlVhpiIteratorError::ReleasedHandle;
    default:
      return VhdlVhpiIteratorError::InvalidObject;
  }
}

}  // namespace

VhdlVhpiObjectRegistry::VhdlVhpiObjectRegistry(
    const std::uint64_t simulation_identity) noexcept
    : simulation_identity_(simulation_identity) {
  const auto identity =
      next_registry_identity.fetch_add(1, std::memory_order_relaxed);
  if (identity <= maximum_registry_identity) {
    registry_identity_ = identity;
  }
}

std::uint64_t VhdlVhpiObjectRegistry::simulation_identity() const noexcept {
  return simulation_identity_;
}

bool VhdlVhpiObjectRegistry::valid() const noexcept {
  return simulation_identity_ != 0U && registry_identity_ != 0U;
}

fsim_vhpi_handle_v1 VhdlVhpiObjectRegistry::encode_object(
    const std::uint32_t slot,
    const std::uint16_t generation) const noexcept {
  return vhpi_tag
      | (static_cast<std::uint64_t>(registry_identity_) << 40U)
      | (static_cast<std::uint64_t>(generation) << 24U)
      | (static_cast<std::uint64_t>(slot) + 1U);
}

fsim_vhpi_handle_v1 VhdlVhpiObjectRegistry::encode_iterator(
    const std::uint32_t slot,
    const std::uint16_t generation) const noexcept {
  return iterator_tag | encode_object(slot, generation);
}

VhdlVhpiObjectError VhdlVhpiObjectRegistry::resolve_object(
    const fsim_vhpi_handle_v1 handle,
    std::uint32_t& slot) const noexcept {
  if (handle == 0U || (handle & vhpi_tag) == 0U
      || (handle & iterator_tag) != 0U || (handle & slot_mask) == 0U) {
    return VhdlVhpiObjectError::InvalidHandle;
  }
  if (static_cast<std::uint32_t>((handle >> 40U) & registry_mask)
      != registry_identity_) {
    return VhdlVhpiObjectError::CrossSimulation;
  }
  const auto decoded_slot =
      static_cast<std::uint32_t>((handle & slot_mask) - 1U);
  if (decoded_slot >= objects_.size()) {
    return VhdlVhpiObjectError::InvalidHandle;
  }
  const auto& record = objects_[decoded_slot];
  const auto generation =
      static_cast<std::uint16_t>((handle >> 24U) & generation_mask);
  if (generation != record.generation) {
    return VhdlVhpiObjectError::StaleHandle;
  }
  if (!record.live) {
    return VhdlVhpiObjectError::ReleasedHandle;
  }
  slot = decoded_slot;
  return VhdlVhpiObjectError::None;
}

VhdlVhpiIteratorError VhdlVhpiObjectRegistry::resolve_iterator(
    const fsim_vhpi_handle_v1 handle,
    std::uint32_t& slot) const noexcept {
  if (handle == 0U || (handle & vhpi_tag) == 0U
      || (handle & iterator_tag) == 0U || (handle & slot_mask) == 0U) {
    return VhdlVhpiIteratorError::InvalidHandle;
  }
  if (static_cast<std::uint32_t>((handle >> 40U) & registry_mask)
      != registry_identity_) {
    return VhdlVhpiIteratorError::CrossSimulation;
  }
  const auto decoded_slot =
      static_cast<std::uint32_t>((handle & slot_mask) - 1U);
  if (decoded_slot >= iterators_.size()) {
    return VhdlVhpiIteratorError::InvalidHandle;
  }
  const auto& record = iterators_[decoded_slot];
  const auto generation =
      static_cast<std::uint16_t>((handle >> 24U) & generation_mask);
  if (generation != record.generation) {
    return VhdlVhpiIteratorError::StaleHandle;
  }
  if (!record.live) {
    return VhdlVhpiIteratorError::ReleasedHandle;
  }
  slot = decoded_slot;
  return VhdlVhpiIteratorError::None;
}

bool VhdlVhpiObjectRegistry::normalize_identifier(
    const std::string_view input,
    std::string& output) {
  output.clear();
  if (input.empty() || input.size() > maximum_identifier_size
      || input.find('\0') != std::string_view::npos) {
    return false;
  }
  if (input.front() == '\\') {
    if (input.size() < 3U || input.back() != '\\') {
      return false;
    }
    for (std::size_t index = 1; index + 1U < input.size(); ++index) {
      if (input[index] == '\\') {
        if (index + 2U >= input.size() || input[index + 1U] != '\\') {
          return false;
        }
        ++index;
      }
    }
    output.assign(input);
    return true;
  }
  const auto ascii_alpha = [](const char value) {
    return std::isalpha(static_cast<unsigned char>(value)) != 0;
  };
  const auto ascii_alnum = [](const char value) {
    return std::isalnum(static_cast<unsigned char>(value)) != 0;
  };
  if (!ascii_alpha(input.front()) || input.back() == '_') {
    return false;
  }
  bool underscore{};
  output.reserve(input.size());
  for (const char value : input) {
    if (value == '_') {
      if (underscore) {
        return false;
      }
      underscore = true;
      output.push_back(value);
      continue;
    }
    if (!ascii_alnum(value)) {
      return false;
    }
    underscore = false;
    output.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(value))));
  }
  return true;
}

std::string VhdlVhpiObjectRegistry::format_segment(
    const std::string_view name,
    const std::span<const std::int64_t> indices) {
  std::string result{name};
  if (indices.empty()) {
    return result;
  }
  result.push_back('(');
  for (std::size_t index = 0; index < indices.size(); ++index) {
    if (index != 0U) {
      result.push_back(',');
    }
    result.append(std::to_string(indices[index]));
  }
  result.push_back(')');
  return result;
}

bool VhdlVhpiObjectRegistry::normalize_segment(
    const std::string_view input,
    std::string& output) {
  if (input.empty() || input.size() > maximum_identifier_size) {
    return false;
  }
  std::size_t name_size{};
  if (input.front() == '\\') {
    bool closed{};
    for (std::size_t index = 1; index < input.size(); ++index) {
      if (input[index] != '\\') {
        continue;
      }
      if (index + 1U < input.size() && input[index + 1U] == '\\') {
        ++index;
        continue;
      }
      name_size = index + 1U;
      closed = true;
      break;
    }
    if (!closed) {
      return false;
    }
  } else {
    const auto suffix = input.find('(');
    name_size = suffix == std::string_view::npos ? input.size() : suffix;
  }

  std::string name;
  if (!normalize_identifier(input.substr(0, name_size), name)) {
    return false;
  }
  if (name_size == input.size()) {
    output = std::move(name);
    return true;
  }
  if (input[name_size] != '(' || input.back() != ')') {
    return false;
  }

  std::vector<std::int64_t> indices;
  auto remaining = input.substr(name_size + 1U, input.size() - name_size - 2U);
  while (!remaining.empty()) {
    if (indices.size() >= maximum_index_dimensions) {
      return false;
    }
    const auto comma = remaining.find(',');
    auto token = remaining.substr(0, comma);
    while (!token.empty()
        && std::isspace(static_cast<unsigned char>(token.front())) != 0) {
      token.remove_prefix(1);
    }
    while (!token.empty()
        && std::isspace(static_cast<unsigned char>(token.back())) != 0) {
      token.remove_suffix(1);
    }
    if (token.empty()) {
      return false;
    }
    std::int64_t value{};
    const auto parsed =
        std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
      return false;
    }
    indices.push_back(value);
    if (comma == std::string_view::npos) {
      remaining = {};
    } else {
      remaining.remove_prefix(comma + 1U);
    }
  }
  if (indices.empty()) {
    return false;
  }
  output = format_segment(name, indices);
  return output.size() <= maximum_identifier_size;
}

bool VhdlVhpiObjectRegistry::normalize_full_name(
    const std::string_view input,
    std::string& output) {
  output.clear();
  if (input.empty() || input.size() > maximum_full_name_size
      || input.find('\0') != std::string_view::npos) {
    return false;
  }
  std::size_t segment_start{};
  bool extended{};
  std::uint32_t parentheses{};
  for (std::size_t index = 0; index <= input.size(); ++index) {
    const bool at_end = index == input.size();
    if (!at_end && input[index] == '\\') {
      if (extended && index + 1U < input.size()
          && input[index + 1U] == '\\') {
        ++index;
        continue;
      }
      extended = !extended;
    } else if (!at_end && !extended && input[index] == '(') {
      ++parentheses;
    } else if (!at_end && !extended && input[index] == ')') {
      if (parentheses == 0U) {
        return false;
      }
      --parentheses;
    }
    if (!at_end
        && (input[index] != '.' || extended || parentheses != 0U)) {
      continue;
    }
    if (extended || parentheses != 0U || index == segment_start) {
      return false;
    }
    std::string segment;
    if (!normalize_segment(
            input.substr(segment_start, index - segment_start), segment)) {
      return false;
    }
    if (!output.empty()) {
      output.push_back('.');
    }
    output.append(segment);
    segment_start = index + 1U;
  }
  return output.size() <= maximum_full_name_size;
}

std::string VhdlVhpiObjectRegistry::sibling_key(
    const fsim_vhpi_handle_v1 parent,
    const std::string_view selected_name) {
  std::string result = std::to_string(parent);
  result.push_back('\0');
  result.append(selected_name);
  return result;
}

bool VhdlVhpiObjectRegistry::is_region(
    const VhdlVhpiObjectKind kind) noexcept {
  switch (kind) {
    case VhdlVhpiObjectKind::Root:
    case VhdlVhpiObjectKind::Library:
    case VhdlVhpiObjectKind::DesignUnit:
    case VhdlVhpiObjectKind::Region:
    case VhdlVhpiObjectKind::Entity:
    case VhdlVhpiObjectKind::Architecture:
    case VhdlVhpiObjectKind::Configuration:
    case VhdlVhpiObjectKind::Package:
    case VhdlVhpiObjectKind::PackageBody:
    case VhdlVhpiObjectKind::Component:
    case VhdlVhpiObjectKind::Block:
    case VhdlVhpiObjectKind::Generate:
    case VhdlVhpiObjectKind::Process:
    case VhdlVhpiObjectKind::Subprogram:
      return true;
    default:
      return false;
  }
}

VhdlVhpiObjectResult VhdlVhpiObjectRegistry::create_object(
    const VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, VhdlVhpiObjectError::InvalidSimulation};
  }
  if (static_cast<std::uint32_t>(kind)
      > static_cast<std::uint32_t>(VhdlVhpiObjectKind::Subtype)) {
    return {{}, VhdlVhpiObjectError::InvalidKind};
  }

  std::uint32_t parent_slot{};
  if (kind == VhdlVhpiObjectKind::Root) {
    if (parent != 0U) {
      return {{}, VhdlVhpiObjectError::InvalidParent};
    }
  } else {
    const auto error = resolve_object(parent, parent_slot);
    if (error != VhdlVhpiObjectError::None) {
      if (error == VhdlVhpiObjectError::CrossSimulation
          || error == VhdlVhpiObjectError::StaleHandle
          || error == VhdlVhpiObjectError::ReleasedHandle) {
        return {{}, error};
      }
      return {{}, VhdlVhpiObjectError::InvalidParent};
    }
  }

  std::uint32_t slot{};
  bool reused{};
  while (!free_objects_.empty()) {
    slot = free_objects_.back();
    free_objects_.pop_back();
    if (objects_[slot].generation
        != std::numeric_limits<std::uint16_t>::max()) {
      reused = true;
      break;
    }
  }
  const auto ordinal = next_ordinal_++;
  if (reused) {
    auto& record = objects_[slot];
    ++record.generation;
    record.live = true;
    record.live_children = 0;
    record.ordinal = ordinal;
    record.parent = parent;
    record.kind = kind;
    record.name.clear();
    record.selected_name.clear();
    record.full_name.clear();
    record.indices.clear();
    record.source.reset();
  } else {
    if (objects_.size() >= slot_mask) {
      return {{}, VhdlVhpiObjectError::ResourceLimit};
    }
    slot = static_cast<std::uint32_t>(objects_.size());
    try {
      objects_.push_back(
          ObjectRecord{
              0,
              true,
              0,
              ordinal,
              parent,
              kind,
              {},
              {},
              {},
              {},
              std::nullopt,
              {},
              {},
              {},
              {},
          });
    } catch (...) {
      return {{}, VhdlVhpiObjectError::ResourceLimit};
    }
  }
  if (kind != VhdlVhpiObjectKind::Root) {
    ++objects_[parent_slot].live_children;
  }
  return {encode_object(slot, objects_[slot].generation), {}};
}

VhdlVhpiObjectResult VhdlVhpiObjectRegistry::create_object(
    const VhdlVhpiObjectDescriptor& descriptor) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, VhdlVhpiObjectError::InvalidSimulation};
  }
  if (static_cast<std::uint32_t>(descriptor.kind)
      > static_cast<std::uint32_t>(VhdlVhpiObjectKind::Subtype)) {
    return {{}, VhdlVhpiObjectError::InvalidKind};
  }
  if (descriptor.indices.size() > maximum_index_dimensions) {
    return {{}, VhdlVhpiObjectError::InvalidName};
  }
  std::string name;
  if (!normalize_identifier(descriptor.name, name)) {
    return {{}, VhdlVhpiObjectError::InvalidName};
  }
  std::string selected_name;
  try {
    selected_name = format_segment(name, descriptor.indices);
  } catch (...) {
    return {{}, VhdlVhpiObjectError::ResourceLimit};
  }
  if (selected_name.size() > maximum_identifier_size) {
    return {{}, VhdlVhpiObjectError::InvalidName};
  }
  if (descriptor.source
      && (descriptor.source->file.empty()
          || descriptor.source->file.size() > maximum_source_size
          || descriptor.source->file.find('\0') != std::string::npos
          || descriptor.source->line == 0U
          || descriptor.source->column == 0U)) {
    return {{}, VhdlVhpiObjectError::InvalidSource};
  }
  const auto valid_provenance_text = [](const std::string_view text) {
    return !text.empty() && text.size() <= maximum_source_size
        && text.find('\0') == std::string_view::npos;
  };
  const bool has_provenance = !descriptor.language_standard.empty()
      || !descriptor.predefined_environment.empty()
      || !descriptor.compatibility_profile.empty()
      || !descriptor.package_dependencies.empty();
  if (has_provenance
      && (!valid_provenance_text(descriptor.language_standard)
          || !valid_provenance_text(descriptor.predefined_environment)
          || !valid_provenance_text(descriptor.compatibility_profile))) {
    return {{}, VhdlVhpiObjectError::InvalidProvenance};
  }
  if (descriptor.package_dependencies.size()
      > maximum_package_dependencies) {
    return {{}, VhdlVhpiObjectError::InvalidProvenance};
  }
  for (std::size_t index = 0;
      index < descriptor.package_dependencies.size(); ++index) {
    const auto& package = descriptor.package_dependencies[index];
    if (!valid_provenance_text(package.standard)
        || !valid_provenance_text(package.predefined_environment)
        || !valid_provenance_text(package.package)
        || !valid_provenance_text(package.revision)
        || !valid_provenance_text(package.source_digest)) {
      return {{}, VhdlVhpiObjectError::InvalidProvenance};
    }
    for (std::size_t prior = 0; prior < index; ++prior) {
      if (descriptor.package_dependencies[prior].package
          == package.package) {
        return {{}, VhdlVhpiObjectError::InvalidProvenance};
      }
    }
  }

  std::uint32_t parent_slot{};
  if (descriptor.kind == VhdlVhpiObjectKind::Root) {
    if (descriptor.parent != 0U) {
      return {{}, VhdlVhpiObjectError::InvalidParent};
    }
  } else {
    const auto error = resolve_object(descriptor.parent, parent_slot);
    if (error != VhdlVhpiObjectError::None) {
      if (error == VhdlVhpiObjectError::CrossSimulation
          || error == VhdlVhpiObjectError::StaleHandle
          || error == VhdlVhpiObjectError::ReleasedHandle) {
        return {{}, error};
      }
      return {{}, VhdlVhpiObjectError::InvalidParent};
    }
    if (objects_[parent_slot].full_name.empty()) {
      return {{}, VhdlVhpiObjectError::InvalidParent};
    }
  }

  std::string full_name;
  try {
    if (descriptor.kind == VhdlVhpiObjectKind::Root) {
      full_name = selected_name;
    } else {
      full_name = objects_[parent_slot].full_name;
      full_name.push_back('.');
      full_name.append(selected_name);
    }
  } catch (...) {
    return {{}, VhdlVhpiObjectError::ResourceLimit};
  }
  if (full_name.size() > maximum_full_name_size) {
    return {{}, VhdlVhpiObjectError::InvalidName};
  }
  const auto key = sibling_key(descriptor.parent, selected_name);
  if (siblings_.contains(key) || full_names_.contains(full_name)) {
    return {{}, VhdlVhpiObjectError::DuplicateName};
  }

  ObjectRecord candidate;
  try {
    candidate.live = true;
    candidate.ordinal = next_ordinal_++;
    candidate.parent = descriptor.parent;
    candidate.kind = descriptor.kind;
    candidate.name = name;
    candidate.selected_name = selected_name;
    candidate.full_name = full_name;
    candidate.indices.assign(
        descriptor.indices.begin(), descriptor.indices.end());
    candidate.source = descriptor.source;
    candidate.language_standard = descriptor.language_standard;
    candidate.predefined_environment = descriptor.predefined_environment;
    candidate.compatibility_profile = descriptor.compatibility_profile;
    candidate.package_dependencies.assign(
        descriptor.package_dependencies.begin(),
        descriptor.package_dependencies.end());
  } catch (...) {
    return {{}, VhdlVhpiObjectError::ResourceLimit};
  }

  std::uint32_t slot{};
  bool reused{};
  while (!free_objects_.empty()) {
    slot = free_objects_.back();
    free_objects_.pop_back();
    if (objects_[slot].generation
        != std::numeric_limits<std::uint16_t>::max()) {
      candidate.generation =
          static_cast<std::uint16_t>(objects_[slot].generation + 1U);
      reused = true;
      break;
    }
  }
  if (!reused) {
    if (objects_.size() >= slot_mask) {
      return {{}, VhdlVhpiObjectError::ResourceLimit};
    }
    slot = static_cast<std::uint32_t>(objects_.size());
    try {
      objects_.push_back(std::move(candidate));
    } catch (...) {
      return {{}, VhdlVhpiObjectError::ResourceLimit};
    }
  } else {
    objects_[slot] = std::move(candidate);
  }

  const auto handle = encode_object(slot, objects_[slot].generation);
  bool sibling_inserted{};
  try {
    sibling_inserted = siblings_.emplace(key, handle).second;
    if (!sibling_inserted
        || !full_names_.emplace(full_name, handle).second) {
      if (sibling_inserted) {
        siblings_.erase(key);
      }
      objects_[slot].live = false;
      free_objects_.push_back(slot);
      return {{}, VhdlVhpiObjectError::DuplicateName};
    }
  } catch (...) {
    if (sibling_inserted) {
      siblings_.erase(key);
    }
    full_names_.erase(full_name);
    objects_[slot].live = false;
    free_objects_.push_back(slot);
    return {{}, VhdlVhpiObjectError::ResourceLimit};
  }
  if (descriptor.kind != VhdlVhpiObjectKind::Root) {
    ++objects_[parent_slot].live_children;
  }
  return {handle, {}};
}

VhdlVhpiObjectLookupResult VhdlVhpiObjectRegistry::lookup_locked(
    const fsim_vhpi_handle_v1 handle) const {
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != VhdlVhpiObjectError::None) {
    return {{}, error};
  }
  const auto& record = objects_[slot];
  return {{
      handle,
      record.parent,
      record.kind,
      record.live_children,
      record.ordinal,
      record.name,
      record.selected_name,
      record.full_name,
      record.indices,
      record.source,
      record.language_standard,
      record.predefined_environment,
      record.compatibility_profile,
      record.package_dependencies,
  }, {}};
}

VhdlVhpiObjectLookupResult VhdlVhpiObjectRegistry::lookup_object(
    const fsim_vhpi_handle_v1 handle) const {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, VhdlVhpiObjectError::InvalidSimulation};
  }
  return lookup_locked(handle);
}

VhdlVhpiObjectLookupResult VhdlVhpiObjectRegistry::find(
    const std::string_view full_name) const {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, VhdlVhpiObjectError::InvalidSimulation};
  }
  std::string normalized;
  if (!normalize_full_name(full_name, normalized)) {
    return {{}, VhdlVhpiObjectError::InvalidName};
  }
  const auto found = full_names_.find(normalized);
  if (found == full_names_.end()) {
    return {{}, VhdlVhpiObjectError::NotFound};
  }
  return lookup_locked(found->second);
}

VhdlVhpiObjectLookupResult VhdlVhpiObjectRegistry::find_child(
    const fsim_vhpi_handle_v1 parent,
    const std::string_view name,
    const std::span<const std::int64_t> indices) const {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, VhdlVhpiObjectError::InvalidSimulation};
  }
  std::uint32_t parent_slot{};
  const auto parent_error = resolve_object(parent, parent_slot);
  if (parent_error != VhdlVhpiObjectError::None) {
    return {{}, parent_error};
  }
  (void)parent_slot;
  if (indices.size() > maximum_index_dimensions) {
    return {{}, VhdlVhpiObjectError::InvalidName};
  }
  std::string normalized_name;
  if (!normalize_identifier(name, normalized_name)) {
    return {{}, VhdlVhpiObjectError::InvalidName};
  }
  std::string selected_name;
  try {
    selected_name = format_segment(normalized_name, indices);
  } catch (...) {
    return {{}, VhdlVhpiObjectError::ResourceLimit};
  }
  const auto found = siblings_.find(sibling_key(parent, selected_name));
  if (found == siblings_.end()) {
    return {{}, VhdlVhpiObjectError::NotFound};
  }
  return lookup_locked(found->second);
}

VhdlVhpiObjectError VhdlVhpiObjectRegistry::release_object(
    const fsim_vhpi_handle_v1 handle) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return VhdlVhpiObjectError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != VhdlVhpiObjectError::None) {
    return error;
  }
  auto& record = objects_[slot];
  if (record.live_children != 0U) {
    return VhdlVhpiObjectError::HasChildren;
  }
  try {
    free_objects_.push_back(slot);
  } catch (...) {
    return VhdlVhpiObjectError::ResourceLimit;
  }
  if (!record.full_name.empty()) {
    siblings_.erase(sibling_key(record.parent, record.selected_name));
    full_names_.erase(record.full_name);
  }
  record.live = false;
  if (record.kind != VhdlVhpiObjectKind::Root) {
    std::uint32_t parent_slot{};
    if (resolve_object(record.parent, parent_slot)
        == VhdlVhpiObjectError::None) {
      --objects_[parent_slot].live_children;
    }
  }
  return VhdlVhpiObjectError::None;
}

VhdlVhpiIteratorResult VhdlVhpiObjectRegistry::create_iterator(
    const std::span<const fsim_vhpi_handle_v1> objects) {
  std::scoped_lock lock{mutex_};
  return create_iterator_locked(objects);
}

VhdlVhpiIteratorResult VhdlVhpiObjectRegistry::create_iterator_locked(
    const std::span<const fsim_vhpi_handle_v1> objects) {
  if (!valid()) {
    return {{}, VhdlVhpiIteratorError::InvalidSimulation};
  }
  if (objects.size() > maximum_iterator_objects) {
    return {{}, VhdlVhpiIteratorError::ResourceLimit};
  }
  for (const auto object : objects) {
    std::uint32_t slot{};
    const auto error = resolve_object(object, slot);
    if (error != VhdlVhpiObjectError::None) {
      return {{}, iterator_error(error)};
    }
  }

  std::vector<fsim_vhpi_handle_v1> snapshot;
  try {
    snapshot.assign(objects.begin(), objects.end());
  } catch (...) {
    return {{}, VhdlVhpiIteratorError::ResourceLimit};
  }

  std::uint32_t slot{};
  bool reused{};
  while (!free_iterators_.empty()) {
    slot = free_iterators_.back();
    free_iterators_.pop_back();
    if (iterators_[slot].generation
        != std::numeric_limits<std::uint16_t>::max()) {
      reused = true;
      break;
    }
  }
  if (reused) {
    auto& iterator = iterators_[slot];
    ++iterator.generation;
    iterator.live = true;
    iterator.cursor = 0;
    iterator.objects = std::move(snapshot);
  } else {
    if (iterators_.size() >= slot_mask) {
      return {{}, VhdlVhpiIteratorError::ResourceLimit};
    }
    slot = static_cast<std::uint32_t>(iterators_.size());
    try {
      iterators_.push_back(
          IteratorRecord{0, true, 0, std::move(snapshot)});
    } catch (...) {
      return {{}, VhdlVhpiIteratorError::ResourceLimit};
    }
  }
  return {encode_iterator(slot, iterators_[slot].generation), {}};
}

VhdlVhpiIteratorScanResult VhdlVhpiObjectRegistry::scan(
    const fsim_vhpi_handle_v1 iterator) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, VhdlVhpiIteratorError::InvalidSimulation};
  }
  std::uint32_t iterator_slot{};
  const auto error = resolve_iterator(iterator, iterator_slot);
  if (error != VhdlVhpiIteratorError::None) {
    return {{}, error};
  }
  auto& record = iterators_[iterator_slot];
  if (record.cursor >= record.objects.size()) {
    return {{}, VhdlVhpiIteratorError::Exhausted};
  }
  const auto object = record.objects[record.cursor];
  std::uint32_t object_slot{};
  const auto object_error = resolve_object(object, object_slot);
  if (object_error != VhdlVhpiObjectError::None) {
    return {{}, iterator_error(object_error)};
  }
  ++record.cursor;
  return {object, {}};
}

VhdlVhpiIteratorError VhdlVhpiObjectRegistry::release_iterator(
    const fsim_vhpi_handle_v1 iterator) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return VhdlVhpiIteratorError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_iterator(iterator, slot);
  if (error != VhdlVhpiIteratorError::None) {
    return error;
  }
  try {
    free_iterators_.push_back(slot);
  } catch (...) {
    return VhdlVhpiIteratorError::ResourceLimit;
  }
  auto& record = iterators_[slot];
  record.live = false;
  record.objects.clear();
  record.cursor = 0;
  return VhdlVhpiIteratorError::None;
}

VhdlVhpiIteratorResult VhdlVhpiObjectRegistry::iterate_relationship(
    const fsim_vhpi_handle_v1 parent,
    const VhdlVhpiRelationshipKind relationship) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, VhdlVhpiIteratorError::InvalidSimulation};
  }
  if (static_cast<std::uint32_t>(relationship)
      > static_cast<std::uint32_t>(
          VhdlVhpiRelationshipKind::Declarations)) {
    return {{}, VhdlVhpiIteratorError::InvalidRelationship};
  }
  std::uint32_t parent_slot{};
  const auto parent_error = resolve_object(parent, parent_slot);
  if (parent_error != VhdlVhpiObjectError::None) {
    return {{}, iterator_error(parent_error)};
  }
  (void)parent_slot;

  std::vector<std::pair<std::uint64_t, fsim_vhpi_handle_v1>> ordered;
  try {
    for (std::uint32_t slot = 0; slot < objects_.size(); ++slot) {
      const auto& record = objects_[slot];
      if (!record.live || record.parent != parent) {
        continue;
      }
      const bool region = is_region(record.kind);
      if ((relationship == VhdlVhpiRelationshipKind::Regions && !region)
          || (relationship == VhdlVhpiRelationshipKind::Declarations
              && region)) {
        continue;
      }
      ordered.emplace_back(
          record.ordinal, encode_object(slot, record.generation));
    }
  } catch (...) {
    return {{}, VhdlVhpiIteratorError::ResourceLimit};
  }
  if (ordered.size() > maximum_iterator_objects) {
    return {{}, VhdlVhpiIteratorError::ResourceLimit};
  }
  std::ranges::sort(ordered);
  std::vector<fsim_vhpi_handle_v1> snapshot;
  try {
    snapshot.reserve(ordered.size());
    for (const auto& [ordinal, handle] : ordered) {
      (void)ordinal;
      snapshot.push_back(handle);
    }
  } catch (...) {
    return {{}, VhdlVhpiIteratorError::ResourceLimit};
  }
  return create_iterator_locked(snapshot);
}

}  // namespace fsim::runtime
