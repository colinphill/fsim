// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_object.hpp"

#include "fsim/runtime/vpi_type_descriptor.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::uint64_t slot_mask = (1ULL << 24U) - 1U;
constexpr std::uint64_t epoch_mask = (1ULL << 16U) - 1U;
constexpr std::uint64_t iterator_bit = 1ULL << 63U;
constexpr std::uint64_t registry_mask = (1ULL << 23U) - 1U;
constexpr std::uint32_t maximum_registry_identity = (1U << 23U) - 1U;
constexpr std::size_t maximum_name_size = 4096;
constexpr std::size_t maximum_source_size = 1U << 20U;
constexpr std::size_t maximum_iterator_objects = 1U << 20U;
std::atomic<std::uint32_t> next_registry_identity{1};
constexpr std::uint32_t maximum_value_width = 1U << 20U;

SystemVerilogVpiTypeInfo default_type(
    const SystemVerilogVpiObjectKind kind) {
  SystemVerilogVpiTypeInfo type;
  switch (kind) {
    case SystemVerilogVpiObjectKind::Port:
      type.category = SystemVerilogVpiValueCategory::Logic4;
      type.direction = SystemVerilogVpiDirection::Inout;
      type.width = 1;
      break;
    case SystemVerilogVpiObjectKind::Net:
      type.category = SystemVerilogVpiValueCategory::Logic4;
      type.net_kind = SystemVerilogVpiNetKind::Wire;
      type.width = 1;
      break;
    case SystemVerilogVpiObjectKind::Variable:
    case SystemVerilogVpiObjectKind::Memory:
    case SystemVerilogVpiObjectKind::Array:
    case SystemVerilogVpiObjectKind::ClassProperty:
      type.category = SystemVerilogVpiValueCategory::Logic4;
      type.width = 1;
      break;
    case SystemVerilogVpiObjectKind::Parameter:
      type.category = SystemVerilogVpiValueCategory::Integer4;
      type.width = 32;
      type.is_signed = true;
      type.is_constant = true;
      break;
    case SystemVerilogVpiObjectKind::NamedEvent:
      type.category = SystemVerilogVpiValueCategory::Event;
      break;
    default:
      break;
  }
  return type;
}

SystemVerilogVpiValueError value_error(
    const SystemVerilogVpiObjectError error) {
  switch (error) {
    case SystemVerilogVpiObjectError::InvalidSimulation:
      return SystemVerilogVpiValueError::InvalidSimulation;
    case SystemVerilogVpiObjectError::InvalidHandle:
      return SystemVerilogVpiValueError::InvalidHandle;
    case SystemVerilogVpiObjectError::CrossSimulation:
      return SystemVerilogVpiValueError::CrossSimulation;
    case SystemVerilogVpiObjectError::StaleHandle:
      return SystemVerilogVpiValueError::StaleHandle;
    case SystemVerilogVpiObjectError::ReleasedHandle:
      return SystemVerilogVpiValueError::ReleasedHandle;
    default:
      return SystemVerilogVpiValueError::InvalidHandle;
  }
}

SystemVerilogVpiValueReadResult value_failure(
    const SystemVerilogVpiValueError error) {
  SystemVerilogVpiValueReadResult result;
  result.error = error;
  return result;
}

bool writable_value(
    const SystemVerilogVpiObjectKind kind,
    const SystemVerilogVpiTypeInfo& type) {
  if (type.is_constant) {
    return false;
  }
  switch (kind) {
    case SystemVerilogVpiObjectKind::Port:
      return type.direction == SystemVerilogVpiDirection::Output
          || type.direction == SystemVerilogVpiDirection::Inout;
    case SystemVerilogVpiObjectKind::Net:
    case SystemVerilogVpiObjectKind::Variable:
    case SystemVerilogVpiObjectKind::Memory:
    case SystemVerilogVpiObjectKind::Array:
    case SystemVerilogVpiObjectKind::ClassProperty:
      return true;
    default:
      return false;
  }
}

SystemVerilogVpiValueError write_access_error(
    const SystemVerilogVpiObjectKind kind,
    const SystemVerilogVpiTypeInfo& type) {
  if (kind == SystemVerilogVpiObjectKind::Port
      && type.direction == SystemVerilogVpiDirection::Input) {
    return SystemVerilogVpiValueError::InputOnly;
  }
  return SystemVerilogVpiValueError::ReadOnly;
}

void notify_value_observers(
    const std::vector<SystemVerilogVpiValueObserver>& observers,
    const fsim_vpi_handle_v1 handle,
    const SystemVerilogVpiStoredValue& value) noexcept {
  for (const auto& observer : observers) {
    try {
      observer(handle, value);
    } catch (...) {
    }
  }
}

bool equivalent_type(
    const SystemVerilogVpiTypeInfo& lhs,
    const SystemVerilogVpiTypeInfo& rhs) {
  const bool descriptors_equal =
      (!lhs.descriptor && !rhs.descriptor)
      || (lhs.descriptor && rhs.descriptor
          && *lhs.descriptor == *rhs.descriptor);
  return lhs.language == rhs.language
      && lhs.category == rhs.category
      && lhs.net_kind == rhs.net_kind
      && lhs.direction == rhs.direction
      && lhs.lifetime == rhs.lifetime
      && lhs.width == rhs.width
      && lhs.is_signed == rhs.is_signed
      && lhs.is_constant == rhs.is_constant
      && descriptors_equal;
}

bool valid_type(
    const SystemVerilogVpiObjectKind kind,
    const std::optional<SystemVerilogVpiTypeInfo>& candidate) {
  if (!candidate) {
    return false;
  }
  const auto& type = *candidate;
  if (static_cast<unsigned>(type.language)
          > static_cast<unsigned>(
              SystemVerilogVpiLanguage::SystemVerilog2017)
      || static_cast<unsigned>(type.category)
          > static_cast<unsigned>(SystemVerilogVpiValueCategory::Event)
      || static_cast<unsigned>(type.net_kind)
          > static_cast<unsigned>(SystemVerilogVpiNetKind::Uwire)
      || static_cast<unsigned>(type.direction)
          > static_cast<unsigned>(SystemVerilogVpiDirection::Inout)
      || static_cast<unsigned>(type.lifetime)
          > static_cast<unsigned>(SystemVerilogVpiLifetime::Automatic)) {
    return false;
  }
  const bool systemverilog_only =
      kind == SystemVerilogVpiObjectKind::Interface
      || kind == SystemVerilogVpiObjectKind::Program
      || kind == SystemVerilogVpiObjectKind::Package
      || kind == SystemVerilogVpiObjectKind::Class
      || kind == SystemVerilogVpiObjectKind::ClassProperty;
  if (systemverilog_only
      && type.language != SystemVerilogVpiLanguage::SystemVerilog2017) {
    return false;
  }

  switch (type.category) {
    case SystemVerilogVpiValueCategory::None:
    case SystemVerilogVpiValueCategory::String:
    case SystemVerilogVpiValueCategory::Event:
      if (type.width != 0U) {
        return false;
      }
      break;
    case SystemVerilogVpiValueCategory::Real:
      if (type.width != 64U) {
        return false;
      }
      break;
    case SystemVerilogVpiValueCategory::ShortReal:
      if (type.width != 32U) {
        return false;
      }
      break;
    case SystemVerilogVpiValueCategory::Time:
      if (type.width != 64U) {
        return false;
      }
      break;
    default:
      if (type.width == 0U || type.width > maximum_value_width) {
        return false;
      }
      break;
  }
  if ((type.category == SystemVerilogVpiValueCategory::None
          || type.category == SystemVerilogVpiValueCategory::String
          || type.category == SystemVerilogVpiValueCategory::Event
          || type.category == SystemVerilogVpiValueCategory::Real
          || type.category == SystemVerilogVpiValueCategory::ShortReal)
      && type.is_signed) {
    return false;
  }

  const bool scope = kind == SystemVerilogVpiObjectKind::Root
      || kind == SystemVerilogVpiObjectKind::Module
      || kind == SystemVerilogVpiObjectKind::Interface
      || kind == SystemVerilogVpiObjectKind::Program
      || kind == SystemVerilogVpiObjectKind::Package
      || kind == SystemVerilogVpiObjectKind::GenerateScope
      || kind == SystemVerilogVpiObjectKind::Class;
  if (scope && type.category != SystemVerilogVpiValueCategory::None) {
    return false;
  }
  if (kind == SystemVerilogVpiObjectKind::NamedEvent
      && (type.category != SystemVerilogVpiValueCategory::Event
          || type.descriptor)) {
    return false;
  }
  if (!scope && kind != SystemVerilogVpiObjectKind::NamedEvent
      && type.category == SystemVerilogVpiValueCategory::None
      && !type.descriptor) {
    return false;
  }

  if (kind == SystemVerilogVpiObjectKind::Net) {
    if (type.net_kind == SystemVerilogVpiNetKind::None
        || type.lifetime != SystemVerilogVpiLifetime::Static) {
      return false;
    }
  } else if (kind != SystemVerilogVpiObjectKind::Port
      && type.net_kind != SystemVerilogVpiNetKind::None) {
    return false;
  }
  if (kind == SystemVerilogVpiObjectKind::Port) {
    if (type.direction == SystemVerilogVpiDirection::None
        || type.lifetime != SystemVerilogVpiLifetime::Static) {
      return false;
    }
  } else if (type.direction != SystemVerilogVpiDirection::None) {
    return false;
  }
  if (type.lifetime == SystemVerilogVpiLifetime::Automatic
      && kind != SystemVerilogVpiObjectKind::Variable
      && kind != SystemVerilogVpiObjectKind::ClassProperty) {
    return false;
  }
  if (type.is_constant
      != (kind == SystemVerilogVpiObjectKind::Parameter)) {
    return false;
  }
  if (!type.descriptor) {
    return true;
  }

  const auto descriptor_result =
      validate_systemverilog_vpi_descriptor(*type.descriptor);
  if (!descriptor_result) {
    return false;
  }
  const auto descriptor_kind = type.descriptor->kind;
  const bool descriptor_requires_systemverilog =
      descriptor_kind == SystemVerilogVpiDescriptorKind::DynamicArray
      || descriptor_kind == SystemVerilogVpiDescriptorKind::Queue
      || descriptor_kind == SystemVerilogVpiDescriptorKind::AssociativeArray
      || descriptor_kind == SystemVerilogVpiDescriptorKind::Struct
      || descriptor_kind == SystemVerilogVpiDescriptorKind::Union
      || descriptor_kind == SystemVerilogVpiDescriptorKind::Enum
      || descriptor_kind == SystemVerilogVpiDescriptorKind::String
      || descriptor_kind == SystemVerilogVpiDescriptorKind::Class
      || descriptor_kind == SystemVerilogVpiDescriptorKind::ClassHandle;
  if (descriptor_requires_systemverilog
      && type.language != SystemVerilogVpiLanguage::SystemVerilog2017) {
    return false;
  }

  if (descriptor_kind == SystemVerilogVpiDescriptorKind::String) {
    if (type.category != SystemVerilogVpiValueCategory::String
        || type.width != 0U || type.is_signed) {
      return false;
    }
  } else if (descriptor_kind == SystemVerilogVpiDescriptorKind::Enum
      || descriptor_kind == SystemVerilogVpiDescriptorKind::Scalar) {
    if (type.category != type.descriptor->category
        || type.width != type.descriptor->width
        || type.is_signed != type.descriptor->is_signed) {
      return false;
    }
  } else if (type.category != SystemVerilogVpiValueCategory::None
      || type.width != 0U || type.is_signed) {
    return false;
  }

  if (scope) {
    return kind == SystemVerilogVpiObjectKind::Class
        && descriptor_kind == SystemVerilogVpiDescriptorKind::Class;
  }
  if (kind == SystemVerilogVpiObjectKind::Memory) {
    return descriptor_kind
        == SystemVerilogVpiDescriptorKind::UnpackedArray;
  }
  if (kind == SystemVerilogVpiObjectKind::Array) {
    return descriptor_kind == SystemVerilogVpiDescriptorKind::PackedArray
        || descriptor_kind
            == SystemVerilogVpiDescriptorKind::UnpackedArray
        || descriptor_kind
            == SystemVerilogVpiDescriptorKind::DynamicArray
        || descriptor_kind == SystemVerilogVpiDescriptorKind::Queue
        || descriptor_kind
            == SystemVerilogVpiDescriptorKind::AssociativeArray;
  }
  if (kind == SystemVerilogVpiObjectKind::Net
      || kind == SystemVerilogVpiObjectKind::Port) {
    return descriptor_kind == SystemVerilogVpiDescriptorKind::Scalar
        || descriptor_kind == SystemVerilogVpiDescriptorKind::PackedArray
        || descriptor_kind == SystemVerilogVpiDescriptorKind::Enum;
  }
  return descriptor_kind != SystemVerilogVpiDescriptorKind::Class;
}

}  // namespace

SystemVerilogVpiObjectRegistry::SystemVerilogVpiObjectRegistry(
    const std::uint64_t simulation_identity) noexcept
    : simulation_identity_(simulation_identity) {
  const auto identity =
      next_registry_identity.fetch_add(1, std::memory_order_relaxed);
  if (simulation_identity != 0U && identity <= maximum_registry_identity) {
    registry_identity_ = identity;
  }
}

std::uint64_t SystemVerilogVpiObjectRegistry::simulation_identity()
    const noexcept {
  return simulation_identity_;
}

bool SystemVerilogVpiObjectRegistry::valid() const noexcept {
  return simulation_identity_ != 0U && registry_identity_ != 0U;
}

fsim_vpi_handle_v1 SystemVerilogVpiObjectRegistry::encode_object(
    const std::uint32_t slot, const std::uint16_t epoch) const noexcept {
  return (static_cast<std::uint64_t>(registry_identity_) << 40U)
      | (static_cast<std::uint64_t>(epoch) << 24U)
      | (static_cast<std::uint64_t>(slot) + 1U);
}

fsim_vpi_handle_v1 SystemVerilogVpiObjectRegistry::encode_iterator(
    const std::uint32_t slot, const std::uint16_t epoch) const noexcept {
  return iterator_bit | encode_object(slot, epoch);
}

SystemVerilogVpiObjectError SystemVerilogVpiObjectRegistry::resolve_object(
    const fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept {
  if (handle == 0U || (handle & iterator_bit) != 0U
      || (handle & slot_mask) == 0U) {
    return SystemVerilogVpiObjectError::InvalidHandle;
  }
  if (static_cast<std::uint32_t>((handle >> 40U) & registry_mask)
      != registry_identity_) {
    return SystemVerilogVpiObjectError::CrossSimulation;
  }
  const auto decoded_slot =
      static_cast<std::uint32_t>((handle & slot_mask) - 1U);
  if (decoded_slot >= records_.size()) {
    return SystemVerilogVpiObjectError::InvalidHandle;
  }
  const auto& record = records_[decoded_slot];
  const auto epoch =
      static_cast<std::uint16_t>((handle >> 24U) & epoch_mask);
  if (epoch != record.epoch) {
    return SystemVerilogVpiObjectError::StaleHandle;
  }
  if (!record.live) {
    return SystemVerilogVpiObjectError::ReleasedHandle;
  }
  slot = decoded_slot;
  return SystemVerilogVpiObjectError::None;
}

SystemVerilogVpiIteratorError SystemVerilogVpiObjectRegistry::resolve_iterator(
    const fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept {
  if (handle == 0U || (handle & iterator_bit) == 0U
      || (handle & slot_mask) == 0U) {
    return SystemVerilogVpiIteratorError::InvalidHandle;
  }
  if (static_cast<std::uint32_t>((handle >> 40U) & registry_mask)
      != registry_identity_) {
    return SystemVerilogVpiIteratorError::CrossSimulation;
  }
  const auto decoded_slot =
      static_cast<std::uint32_t>((handle & slot_mask) - 1U);
  if (decoded_slot >= iterators_.size()) {
    return SystemVerilogVpiIteratorError::InvalidHandle;
  }
  const auto& record = iterators_[decoded_slot];
  const auto epoch =
      static_cast<std::uint16_t>((handle >> 24U) & epoch_mask);
  if (epoch != record.epoch) {
    return SystemVerilogVpiIteratorError::StaleHandle;
  }
  if (!record.live) {
    return SystemVerilogVpiIteratorError::ReleasedHandle;
  }
  slot = decoded_slot;
  return SystemVerilogVpiIteratorError::None;
}

bool SystemVerilogVpiObjectRegistry::normalize_name(
    const std::string_view input,
    std::string& normalized,
    std::string& hierarchy_segment) {
  if (input.empty() || input.size() > maximum_name_size
      || input.find('\0') != std::string_view::npos) {
    return false;
  }
  if (input.front() != '\\') {
    if (input.find('.') != std::string_view::npos
        || std::ranges::any_of(input, [](const char character) {
             return std::isspace(
                 static_cast<unsigned char>(character)) != 0;
           })) {
      return false;
    }
    normalized.assign(input);
    hierarchy_segment = normalized;
    return true;
  }

  auto end = input.size();
  while (end > 1U
         && std::isspace(static_cast<unsigned char>(input[end - 1U])) != 0) {
    --end;
  }
  if (end == 1U
      || std::ranges::any_of(input.substr(1U, end - 1U),
          [](const char character) {
            return std::isspace(
                static_cast<unsigned char>(character)) != 0;
          })) {
    return false;
  }
  normalized.assign(input.substr(0U, end));
  hierarchy_segment = normalized;
  hierarchy_segment.push_back(' ');
  return true;
}

std::string SystemVerilogVpiObjectRegistry::sibling_key(
    const fsim_vpi_handle_v1 parent, const std::string_view name) {
  auto key = std::to_string(parent);
  key.push_back('\0');
  key.append(name);
  return key;
}

SystemVerilogVpiObjectResult SystemVerilogVpiObjectRegistry::create(
    const SystemVerilogVpiObjectKind kind,
    const fsim_vpi_handle_v1 parent,
    const std::string_view name) {
  return create(SystemVerilogVpiObjectDescriptor{
      kind,
      parent,
      std::string{name},
      std::nullopt,
      default_type(kind),
  });
}

SystemVerilogVpiObjectResult SystemVerilogVpiObjectRegistry::create(
    const SystemVerilogVpiObjectDescriptor& descriptor) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, SystemVerilogVpiObjectError::InvalidSimulation};
  }
  if (static_cast<unsigned>(descriptor.kind)
      > static_cast<unsigned>(
          SystemVerilogVpiObjectKind::NamedEvent)) {
    return {{}, SystemVerilogVpiObjectError::InvalidKind};
  }

  std::string normalized_name;
  std::string hierarchy_segment;
  if (!normalize_name(
          descriptor.name, normalized_name, hierarchy_segment)) {
    return {{}, SystemVerilogVpiObjectError::InvalidName};
  }
  if (descriptor.source
      && (descriptor.source->file.empty()
          || descriptor.source->file.size() > maximum_source_size
          || descriptor.source->file.find('\0') != std::string::npos
          || descriptor.source->line == 0U
          || descriptor.source->column == 0U)) {
    return {{}, SystemVerilogVpiObjectError::InvalidSource};
  }
  if (!valid_type(descriptor.kind, descriptor.type)) {
    return {{}, SystemVerilogVpiObjectError::InvalidType};
  }
  auto owned_type = descriptor.type;
  if (owned_type && owned_type->descriptor) {
    owned_type->descriptor =
        std::make_shared<const SystemVerilogVpiTypeDescriptor>(
            *owned_type->descriptor);
  }

  std::uint32_t parent_slot{};
  if (descriptor.kind == SystemVerilogVpiObjectKind::Root) {
    if (descriptor.parent != 0U) {
      return {{}, SystemVerilogVpiObjectError::InvalidParent};
    }
  } else {
    const auto parent_error =
        resolve_object(descriptor.parent, parent_slot);
    if (parent_error != SystemVerilogVpiObjectError::None) {
      return {{}, SystemVerilogVpiObjectError::InvalidParent};
    }
  }

  auto key = sibling_key(descriptor.parent, normalized_name);
  if (siblings_.contains(key)) {
    return {{}, SystemVerilogVpiObjectError::DuplicateName};
  }
  std::string full_name;
  if (descriptor.kind == SystemVerilogVpiObjectKind::Root) {
    full_name = hierarchy_segment;
  } else {
    full_name = records_[parent_slot].full_name;
    full_name.push_back('.');
    full_name.append(hierarchy_segment);
  }
  if (full_names_.contains(full_name)) {
    return {{}, SystemVerilogVpiObjectError::DuplicateName};
  }

  std::uint32_t slot{};
  bool reused{};
  while (!free_slots_.empty()) {
    slot = free_slots_.back();
    free_slots_.pop_back();
    if (records_[slot].epoch != std::numeric_limits<std::uint16_t>::max()) {
      reused = true;
      break;
    }
  }
  const auto ordinal = next_ordinal_++;
  if (reused) {
    auto& record = records_[slot];
    ++record.epoch;
    record.live = true;
    record.live_children = 0;
    record.ordinal = ordinal;
    record.parent = descriptor.parent;
    record.kind = descriptor.kind;
    record.name = std::move(normalized_name);
    record.full_name = std::move(full_name);
    record.source = descriptor.source;
    record.type = owned_type;
    record.forced_value.reset();
    record.value.reset();
    record.initial_value.reset();
  } else {
    if (records_.size() >= slot_mask) {
      return {{}, SystemVerilogVpiObjectError::ResourceLimit};
    }
    slot = static_cast<std::uint32_t>(records_.size());
    records_.push_back(Record{
        0,
        true,
        0,
        ordinal,
        descriptor.parent,
        descriptor.kind,
        std::move(normalized_name),
        std::move(full_name),
        descriptor.source,
        owned_type,
        std::nullopt,
        std::nullopt,
        std::nullopt,
    });
  }

  const auto handle = encode_object(slot, records_[slot].epoch);
  siblings_.emplace(std::move(key), handle);
  full_names_.emplace(records_[slot].full_name, handle);
  if (descriptor.kind != SystemVerilogVpiObjectKind::Root) {
    ++records_[parent_slot].live_children;
  }
  return {handle, {}};
}

SystemVerilogVpiObjectLookupResult
SystemVerilogVpiObjectRegistry::lookup_locked(
    const fsim_vpi_handle_v1 handle) const {
  if (!valid()) {
    return {{}, SystemVerilogVpiObjectError::InvalidSimulation};
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return {{}, error};
  }
  const auto& record = records_[slot];
  return {SystemVerilogVpiObjectInfo{
              handle,
              record.parent,
              record.kind,
              record.name,
              record.full_name,
              record.source,
              record.type,
          },
      {}};
}

SystemVerilogVpiObjectLookupResult SystemVerilogVpiObjectRegistry::lookup(
    const fsim_vpi_handle_v1 handle) const {
  std::scoped_lock lock{mutex_};
  return lookup_locked(handle);
}

SystemVerilogVpiObjectLookupResult SystemVerilogVpiObjectRegistry::find(
    const std::string_view full_name) const {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, SystemVerilogVpiObjectError::InvalidSimulation};
  }
  const auto found = full_names_.find(std::string{full_name});
  if (found == full_names_.end()) {
    return {{}, SystemVerilogVpiObjectError::NotFound};
  }
  return lookup_locked(found->second);
}

SystemVerilogVpiObjectLookupResult
SystemVerilogVpiObjectRegistry::find_child(
    const fsim_vpi_handle_v1 parent, const std::string_view name) const {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, SystemVerilogVpiObjectError::InvalidSimulation};
  }
  std::uint32_t parent_slot{};
  if (resolve_object(parent, parent_slot)
      != SystemVerilogVpiObjectError::None) {
    return {{}, SystemVerilogVpiObjectError::InvalidParent};
  }
  std::string normalized;
  std::string segment;
  if (!normalize_name(name, normalized, segment)) {
    return {{}, SystemVerilogVpiObjectError::InvalidName};
  }
  const auto found = siblings_.find(sibling_key(parent, normalized));
  if (found == siblings_.end()) {
    return {{}, SystemVerilogVpiObjectError::NotFound};
  }
  return lookup_locked(found->second);
}

SystemVerilogVpiTypeLookupResult
SystemVerilogVpiObjectRegistry::type_info(
    const fsim_vpi_handle_v1 handle) const {
  std::scoped_lock lock{mutex_};
  const auto object = lookup_locked(handle);
  if (!object) {
    return {{}, object.error};
  }
  if (!object.value->type) {
    return {{}, SystemVerilogVpiObjectError::InvalidType};
  }
  return {object.value->type, {}};
}
std::optional<std::uint64_t>
SystemVerilogVpiObjectRegistry::add_value_observer(
    SystemVerilogVpiValueObserver observer) {
  std::scoped_lock lock{mutex_};
  constexpr std::size_t maximum_observers = 65'536;
  if (!valid() || !observer
      || value_observers_.size() >= maximum_observers
      || next_value_observer_ == 0U) {
    return std::nullopt;
  }
  const auto id = next_value_observer_++;
  value_observers_.emplace(id, std::move(observer));
  return id;
}

bool SystemVerilogVpiObjectRegistry::remove_value_observer(
    const std::uint64_t observer) {
  std::scoped_lock lock{mutex_};
  return observer != 0U && value_observers_.erase(observer) == 1U;
}


SystemVerilogVpiValueError SystemVerilogVpiObjectRegistry::bind_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiValueError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return value_error(error);
  }
  auto& record = records_[slot];
  if (!record.type) {
    return SystemVerilogVpiValueError::NotReadable;
  }
  if (record.value) {
    return SystemVerilogVpiValueError::AlreadyBound;
  }
  if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
    return SystemVerilogVpiValueError::TypeMismatch;
  }
  record.initial_value = value;
  record.value = std::move(value);
  return SystemVerilogVpiValueError::None;
}
SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::validate_value_write(
    const fsim_vpi_handle_v1 handle,
    const SystemVerilogVpiStoredValue* const value) const {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiValueError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return value_error(error);
  }
  const auto& record = records_[slot];
  if (!record.type) {
    return SystemVerilogVpiValueError::NotReadable;
  }
  if (!writable_value(record.kind, *record.type)) {
    return write_access_error(record.kind, *record.type);
  }
  if (value
      && !validate_systemverilog_vpi_stored_value(*record.type, *value)) {
    return SystemVerilogVpiValueError::TypeMismatch;
  }
  return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError SystemVerilogVpiObjectRegistry::deposit_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value) {
  std::vector<SystemVerilogVpiValueObserver> observers;
  std::optional<SystemVerilogVpiStoredValue> published;
  std::unique_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiValueError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return value_error(error);
  }
  auto& record = records_[slot];
  if (!record.type) {
    return SystemVerilogVpiValueError::NotReadable;
  }
  if (!writable_value(record.kind, *record.type)) {
    return write_access_error(record.kind, *record.type);
  }
  if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
    return SystemVerilogVpiValueError::TypeMismatch;
  }
  const bool changed = !record.forced_value
      && (!record.value || *record.value != value);
  if (changed && !value_observers_.empty()) {
    published = value;
    observers.reserve(value_observers_.size());
    for (const auto& [id, observer] : value_observers_) {
      (void)id;
      observers.push_back(observer);
    }
  }
  record.value = std::move(value);
  lock.unlock();
  if (published) {
    notify_value_observers(observers, handle, *published);
  }
  return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError SystemVerilogVpiObjectRegistry::force_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value) {
  std::vector<SystemVerilogVpiValueObserver> observers;
  std::optional<SystemVerilogVpiStoredValue> published;
  std::unique_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiValueError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return value_error(error);
  }
  auto& record = records_[slot];
  if (!record.type) {
    return SystemVerilogVpiValueError::NotReadable;
  }
  if (!writable_value(record.kind, *record.type)) {
    return write_access_error(record.kind, *record.type);
  }
  if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
    return SystemVerilogVpiValueError::TypeMismatch;
  }
  const bool changed =
      !record.forced_value || *record.forced_value != value;
  if (changed && !value_observers_.empty()) {
    published = value;
    observers.reserve(value_observers_.size());
    for (const auto& [id, observer] : value_observers_) {
      (void)id;
      observers.push_back(observer);
    }
  }
  record.forced_value = std::move(value);
  lock.unlock();
  if (published) {
    notify_value_observers(observers, handle, *published);
  }
  return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::release_forced_value(
    const fsim_vpi_handle_v1 handle) {
  std::vector<SystemVerilogVpiValueObserver> observers;
  std::optional<SystemVerilogVpiStoredValue> published;
  std::unique_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiValueError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return value_error(error);
  }
  auto& record = records_[slot];
  if (!record.type) {
    return SystemVerilogVpiValueError::NotReadable;
  }
  if (!writable_value(record.kind, *record.type)) {
    return write_access_error(record.kind, *record.type);
  }
  if (!record.forced_value) {
    return SystemVerilogVpiValueError::NotForced;
  }
  const bool changed =
      record.value && *record.forced_value != *record.value;
  if (changed && !value_observers_.empty()) {
    published = record.value;
    observers.reserve(value_observers_.size());
    for (const auto& [id, observer] : value_observers_) {
      (void)id;
      observers.push_back(observer);
    }
  }
  record.forced_value.reset();
  lock.unlock();
  if (published) {
    notify_value_observers(observers, handle, *published);
  }
  return SystemVerilogVpiValueError::None;
}
SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::reset_values() {
  struct ResetChange {
    std::size_t slot{};
    fsim_vpi_handle_v1 handle{};
    SystemVerilogVpiStoredValue reset_value;
    std::optional<SystemVerilogVpiStoredValue> published;
  };

  std::vector<ResetChange> changes;
  std::vector<SystemVerilogVpiValueObserver> observers;
  std::unique_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiValueError::InvalidSimulation;
  }
  try {
    changes.reserve(records_.size());
    for (std::size_t slot = 0; slot < records_.size(); ++slot) {
      const auto& record = records_[slot];
      if (!record.live || !record.value || !record.initial_value) {
        continue;
      }
      const auto& visible =
          record.forced_value ? *record.forced_value : *record.value;
      ResetChange change;
      change.slot = slot;
      change.handle = encode_object(
          static_cast<std::uint32_t>(slot), record.epoch);
      change.reset_value = *record.initial_value;
      if (visible != *record.initial_value
          && !value_observers_.empty()) {
        change.published = *record.initial_value;
      }
      changes.push_back(std::move(change));
    }
    if (std::ranges::any_of(
            changes,
            [](const ResetChange& change) {
              return change.published.has_value();
            })) {
      observers.reserve(value_observers_.size());
      for (const auto& [id, observer] : value_observers_) {
        (void)id;
        observers.push_back(observer);
      }
    }
  } catch (...) {
    return SystemVerilogVpiValueError::ResourceLimit;
  }

  for (auto& change : changes) {
    auto& record = records_[change.slot];
    record.value = std::move(change.reset_value);
    record.forced_value.reset();
  }
  lock.unlock();

  for (const auto& change : changes) {
    if (change.published) {
      notify_value_observers(
          observers, change.handle, *change.published);
    }
  }
  return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiObjectStateSnapshot
SystemVerilogVpiObjectRegistry::snapshot_values() const {
  SystemVerilogVpiObjectStateSnapshot snapshot;
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    snapshot.error =
        SystemVerilogVpiObjectStateError::InvalidSimulation;
    return snapshot;
  }
  snapshot.simulation_identity = simulation_identity_;
  try {
    snapshot.objects.reserve(records_.size());
    for (std::size_t slot = 0; slot < records_.size(); ++slot) {
      const auto& record = records_[slot];
      if (!record.live || !record.type) {
        continue;
      }
      snapshot.objects.push_back(SystemVerilogVpiObjectState{
          encode_object(
              static_cast<std::uint32_t>(slot), record.epoch),
          record.full_name,
          *record.type,
          record.value,
          record.forced_value,
      });
    }
  } catch (...) {
    snapshot.objects.clear();
    snapshot.error = SystemVerilogVpiObjectStateError::ResourceLimit;
  }
  return snapshot;
}

SystemVerilogVpiObjectStateRestoreResult
SystemVerilogVpiObjectRegistry::restore_values(
    const SystemVerilogVpiObjectStateSnapshot& snapshot) {
  struct PendingRestore {
    std::uint32_t slot{};
    std::optional<SystemVerilogVpiStoredValue> value;
    std::optional<SystemVerilogVpiStoredValue> forced_value;
  };

  SystemVerilogVpiObjectStateRestoreResult result;
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    result.error =
        SystemVerilogVpiObjectStateError::InvalidSimulation;
    return result;
  }
  if (snapshot.error != SystemVerilogVpiObjectStateError::None
      || snapshot.simulation_identity == 0) {
    result.error = SystemVerilogVpiObjectStateError::InvalidState;
    return result;
  }

  std::vector<PendingRestore> pending;
  std::unordered_set<std::string> names;
  std::unordered_set<fsim_vpi_handle_v1> handles;
  try {
    const auto restorable_objects = std::ranges::count_if(
        records_, [](const Record& record) {
          return record.live && record.type.has_value();
        });
    if (snapshot.objects.size()
        != static_cast<std::size_t>(restorable_objects)) {
      result.error = SystemVerilogVpiObjectStateError::InvalidState;
      return result;
    }
    pending.reserve(snapshot.objects.size());
    result.handles.reserve(snapshot.objects.size());
    names.reserve(snapshot.objects.size());
    handles.reserve(snapshot.objects.size());
    for (const auto& state : snapshot.objects) {
      if (state.source_handle == 0U || state.full_name.empty()
          || !names.insert(state.full_name).second
          || !handles.insert(state.source_handle).second) {
        result.error = SystemVerilogVpiObjectStateError::InvalidState;
        return result;
      }
      const auto found = full_names_.find(state.full_name);
      if (found == full_names_.end()) {
        result.error = SystemVerilogVpiObjectStateError::MissingObject;
        return result;
      }
      std::uint32_t slot{};
      if (resolve_object(found->second, slot)
              != SystemVerilogVpiObjectError::None) {
        result.error = SystemVerilogVpiObjectStateError::MissingObject;
        return result;
      }
      const auto& record = records_[slot];
      if (!record.type || !equivalent_type(*record.type, state.type)
          || (state.value
              && !validate_systemverilog_vpi_stored_value(
                  *record.type, *state.value))
          || (state.forced_value
              && !validate_systemverilog_vpi_stored_value(
                  *record.type, *state.forced_value))) {
        result.error = SystemVerilogVpiObjectStateError::TypeMismatch;
        return result;
      }
      pending.push_back(
          {slot, state.value, state.forced_value});
      result.handles.push_back(
          {state.source_handle, found->second});
    }
  } catch (...) {
    result.handles.clear();
    result.error = SystemVerilogVpiObjectStateError::ResourceLimit;
    return result;
  }

  for (auto& restore : pending) {
    auto& record = records_[restore.slot];
    record.value = std::move(restore.value);
    record.forced_value = std::move(restore.forced_value);
  }
  return result;
}


SystemVerilogVpiValueReadResult SystemVerilogVpiObjectRegistry::read_value(
    const fsim_vpi_handle_v1 handle,
    const SystemVerilogVpiValueFormat format,
    const SystemVerilogVpiValueReadBuffers buffers) const {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return value_failure(SystemVerilogVpiValueError::InvalidSimulation);
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return value_failure(value_error(error));
  }
  const auto& record = records_[slot];
  if (!record.type) {
    return value_failure(SystemVerilogVpiValueError::NotReadable);
  }
  const auto& visible_value =
      record.forced_value ? record.forced_value : record.value;
  if (!visible_value) {
    return value_failure(SystemVerilogVpiValueError::NotBound);
  }
  return read_systemverilog_vpi_value(
      *record.type, *visible_value, format, buffers);
}

SystemVerilogVpiObjectError SystemVerilogVpiObjectRegistry::release(
    const fsim_vpi_handle_v1 handle) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiObjectError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_object(handle, slot);
  if (error != SystemVerilogVpiObjectError::None) {
    return error;
  }
  auto& record = records_[slot];
  if (record.live_children != 0U) {
    return SystemVerilogVpiObjectError::HasChildren;
  }

  siblings_.erase(sibling_key(record.parent, record.name));
  record.value.reset();
  record.forced_value.reset();
  record.initial_value.reset();
  full_names_.erase(record.full_name);
  record.live = false;
  if (record.kind != SystemVerilogVpiObjectKind::Root) {
    std::uint32_t parent_slot{};
    if (resolve_object(record.parent, parent_slot)
        == SystemVerilogVpiObjectError::None) {
      --records_[parent_slot].live_children;
    }
  }
  free_slots_.push_back(slot);
  return SystemVerilogVpiObjectError::None;
}

SystemVerilogVpiIteratorResult
SystemVerilogVpiObjectRegistry::iterate_children(
    const fsim_vpi_handle_v1 parent) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, SystemVerilogVpiIteratorError::InvalidSimulation};
  }
  std::uint32_t parent_slot{};
  if (resolve_object(parent, parent_slot)
      != SystemVerilogVpiObjectError::None) {
    return {{}, SystemVerilogVpiIteratorError::InvalidObject};
  }

  std::vector<std::pair<std::uint64_t, fsim_vpi_handle_v1>> ordered;
  ordered.reserve(records_[parent_slot].live_children);
  for (std::uint32_t slot = 0; slot < records_.size(); ++slot) {
    const auto& record = records_[slot];
    if (record.live && record.parent == parent) {
      ordered.emplace_back(
          record.ordinal, encode_object(slot, record.epoch));
    }
  }
  if (ordered.size() > maximum_iterator_objects) {
    return {{}, SystemVerilogVpiIteratorError::ResourceLimit};
  }
  std::ranges::sort(ordered);

  std::vector<fsim_vpi_handle_v1> objects;
  objects.reserve(ordered.size());
  for (const auto& [ordinal, handle] : ordered) {
    (void)ordinal;
    objects.push_back(handle);
  }

  std::uint32_t slot{};
  bool reused{};
  while (!free_iterators_.empty()) {
    slot = free_iterators_.back();
    free_iterators_.pop_back();
    if (iterators_[slot].epoch
        != std::numeric_limits<std::uint16_t>::max()) {
      reused = true;
      break;
    }
  }
  if (reused) {
    auto& iterator = iterators_[slot];
    ++iterator.epoch;
    iterator.live = true;
    iterator.cursor = 0;
    iterator.objects = std::move(objects);
  } else {
    if (iterators_.size() >= slot_mask) {
      return {{}, SystemVerilogVpiIteratorError::ResourceLimit};
    }
    slot = static_cast<std::uint32_t>(iterators_.size());
    iterators_.push_back(
        IteratorRecord{0, true, 0, std::move(objects)});
  }
  return {encode_iterator(slot, iterators_[slot].epoch), {}};
}

SystemVerilogVpiIteratorScanResult SystemVerilogVpiObjectRegistry::scan(
    const fsim_vpi_handle_v1 iterator) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return {{}, SystemVerilogVpiIteratorError::InvalidSimulation};
  }
  std::uint32_t slot{};
  const auto error = resolve_iterator(iterator, slot);
  if (error != SystemVerilogVpiIteratorError::None) {
    return {{}, error};
  }
  auto& record = iterators_[slot];
  if (record.cursor == record.objects.size()) {
    return {{}, SystemVerilogVpiIteratorError::End};
  }
  return {record.objects[record.cursor++], {}};
}

SystemVerilogVpiIteratorError
SystemVerilogVpiObjectRegistry::release_iterator(
    const fsim_vpi_handle_v1 iterator) {
  std::scoped_lock lock{mutex_};
  if (!valid()) {
    return SystemVerilogVpiIteratorError::InvalidSimulation;
  }
  std::uint32_t slot{};
  const auto error = resolve_iterator(iterator, slot);
  if (error != SystemVerilogVpiIteratorError::None) {
    return error;
  }
  iterators_[slot].live = false;
  iterators_[slot].objects.clear();
  free_iterators_.push_back(slot);
  return SystemVerilogVpiIteratorError::None;
}

}  // namespace fsim::runtime
