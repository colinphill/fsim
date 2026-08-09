// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <limits>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidModel{"FSIM-UVM-REG-001"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-REG-002"};

[[noreturn]] void fail(const std::string_view code, std::string message) {
  throw SystemVerilogUvmRegisterModelError{std::string{code},
                                           std::move(message)};
}

[[nodiscard]] std::uint64_t byte_width(const std::uint32_t width_bits) {
  return (static_cast<std::uint64_t>(width_bits) + 7U) / 8U;
}

[[nodiscard]] bool
valid_endianness(const SystemVerilogUvmRegisterMapEndianness value) {
  return value == SystemVerilogUvmRegisterMapEndianness::Little ||
         value == SystemVerilogUvmRegisterMapEndianness::Big ||
         value == SystemVerilogUvmRegisterMapEndianness::LittleFifo ||
         value == SystemVerilogUvmRegisterMapEndianness::BigFifo;
}

void validate_span(const std::uint64_t offset, const std::uint64_t bytes,
                   const std::string_view subject) {
  if (bytes == 0 || offset > std::numeric_limits<std::uint64_t>::max() -
                                 (bytes - 1U)) {
    fail(kInvalidModel,
         std::string{subject} + " address span overflows 64-bit storage");
  }
}

} // namespace

SystemVerilogUvmRegisterModelError::SystemVerilogUvmRegisterModelError(
    std::string code, std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

SystemVerilogUvmRegisterModelService::SystemVerilogUvmRegisterModelService(
    SystemVerilogUvmComponentService &components,
    SystemVerilogUvmRegisterModelLimits limits)
    : components_(&components), limits_(limits) {
  if (limits_.maximum_blocks == 0 || limits_.maximum_maps == 0 ||
      limits_.maximum_registers == 0 || limits_.maximum_fields == 0 ||
      limits_.maximum_memories == 0 ||
      limits_.maximum_declarations == 0 ||
      limits_.maximum_declarations_per_block == 0 ||
      limits_.maximum_block_depth == 0 || limits_.maximum_name_bytes == 0 ||
      limits_.maximum_full_name_bytes == 0 ||
      limits_.maximum_width_bits == 0 || limits_.maximum_dimensions == 0 ||
      limits_.maximum_register_value_bits == 0 ||
      limits_.maximum_dimension_extent == 0 ||
      limits_.maximum_memory_words == 0 ||
      limits_.maximum_reset_kinds_per_field == 0 ||
      limits_.maximum_reset_name_bytes == 0 ||
      limits_.maximum_reset_value_bits == 0 ||
      limits_.maximum_materialized_memory_words == 0 ||
      limits_.maximum_materialized_memory_bits == 0 ||
      limits_.maximum_map_registrations == 0 ||
      limits_.maximum_map_registrations_per_map == 0 ||
      limits_.maximum_submap_depth == 0 ||
      limits_.maximum_bus_width_bytes == 0 ||
      limits_.maximum_physical_beats == 0 ||
      limits_.maximum_physical_byte_enables == 0 ||
      limits_.maximum_burst_words == 0 ||
      limits_.maximum_burst_value_bits == 0 ||
      limits_.maximum_address_lookup_work == 0 ||
      limits_.maximum_adapters == 0 || limits_.maximum_predictors == 0 ||
      limits_.maximum_frontdoor_operations == 0 ||
      limits_.maximum_pending_frontdoor_operations == 0 ||
      limits_.maximum_frontdoor_bus_items == 0 ||
      limits_.maximum_frontdoor_value_bits == 0 ||
      limits_.maximum_frontdoor_byte_enables == 0 ||
      limits_.maximum_predictor_observations == 0 ||
      limits_.maximum_user_frontdoors == 0 ||
      limits_.maximum_hdl_paths == 0 || limits_.maximum_hdl_slices == 0 ||
      limits_.maximum_hdl_path_bytes == 0 ||
      limits_.maximum_backdoor_operations == 0 ||
      limits_.maximum_backdoor_value_bits == 0 ||
      limits_.maximum_standard_sequences == 0 ||
      limits_.maximum_standard_sequence_operations == 0 ||
      limits_.maximum_standard_sequence_failures == 0 ||
      limits_.maximum_standard_sequence_failure_bytes == 0 ||
      limits_.maximum_standard_sequence_exclusions == 0 ||
      limits_.maximum_standard_sequence_exclusion_bytes == 0 ||
      limits_.maximum_register_callbacks == 0 ||
      limits_.maximum_register_callbacks_per_access == 0 ||
      limits_.maximum_register_callback_invocations == 0 ||
      limits_.maximum_register_callback_failures == 0 ||
      limits_.maximum_register_coverage_models == 0 ||
      limits_.maximum_register_coverage_samples == 0 ||
      limits_.maximum_register_coverage_bins == 0 ||
      limits_.maximum_register_coverage_bin_bytes == 0 ||
      limits_.maximum_operations == 0 || limits_.maximum_mutations == 0) {
    throw std::invalid_argument{
        "UVM register-model resource limits must all be positive"};
  }
}

void SystemVerilogUvmRegisterModelService::publish_activity(
    const SystemVerilogUvmActivityAction action, std::string identity,
    const SystemVerilogUvmRootHandle root, const std::uint64_t value,
    std::string detail) noexcept {
  if (!activity_)
    return;
  try {
    activity_->publish({SystemVerilogUvmActivityKind::RegisterModel, action,
                        std::move(identity), std::move(detail), root, value});
  } catch (...) {
    // Optional observation failure must not roll back a committed model edit.
  }
}

void SystemVerilogUvmRegisterModelService::validate_name(
    const std::string_view name) const {
  if (name.size() > limits_.maximum_name_bytes) {
    fail(kResourceLimit, "UVM register-model name storage limit exceeded");
  }
  if (name.empty() || name.find('\0') != std::string_view::npos ||
      name.find('.') != std::string_view::npos ||
      name.find(':') != std::string_view::npos) {
    fail(kInvalidModel,
         "UVM register-model name is empty, excessive, or contains a "
         "hierarchy separator");
  }
}

void SystemVerilogUvmRegisterModelService::validate_declaration_capacity(
    const std::size_t current, const std::size_t maximum) const {
  if (current >= maximum || declarations_.size() >=
                                limits_.maximum_declarations) {
    fail(kResourceLimit, "UVM register-model declaration limit exceeded");
  }
}

void SystemVerilogUvmRegisterModelService::record_mutation() {
  if (mutations_ >= limits_.maximum_mutations) {
    fail(kResourceLimit, "UVM register-model mutation limit exceeded");
  }
  ++mutations_;
}

std::uint64_t SystemVerilogUvmRegisterModelService::next_identity() {
  if (next_identity_ == 0 ||
      next_identity_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM register-model identity space is exhausted");
  }
  return next_identity_++;
}

std::uint64_t SystemVerilogUvmRegisterModelService::next_order() {
  if (next_order_ == 0 ||
      next_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit,
         "UVM register-model declaration order is exhausted");
  }
  return next_order_++;
}

void SystemVerilogUvmRegisterModelService::record_declaration(
    const SystemVerilogUvmRegisterDeclarationKind kind,
    const std::uint64_t identity, const std::uint64_t owner_identity,
    const SystemVerilogUvmRootHandle root, std::string name,
    std::string full_name, const std::uint64_t order) {
  declarations_.push_back({kind, identity, owner_identity, root,
                           std::move(name), std::move(full_name), order});
}

std::string SystemVerilogUvmRegisterModelService::child_full_name(
    const SystemVerilogUvmRegisterBlockSnapshot &owner,
    const std::string_view name) const {
  std::string result = owner.full_name;
  if (result.size() >= limits_.maximum_full_name_bytes ||
      name.size() > limits_.maximum_full_name_bytes - result.size() - 1U) {
    fail(kResourceLimit,
         "UVM register-model full-name storage limit exceeded");
  }
  result.push_back('.');
  result.append(name);
  return result;
}

void SystemVerilogUvmRegisterModelService::reserve_child_name(
    const SystemVerilogUvmRegisterBlockHandle owner,
    const std::string_view name) {
  auto &names = child_names_[block(owner).snapshot.identity];
  if (names.size() >= limits_.maximum_declarations_per_block) {
    fail(kResourceLimit,
         "UVM register-model per-block declaration limit exceeded");
  }
  if (names.contains(name)) {
    fail(kInvalidModel,
         "UVM register-model block already owns this child name");
  }
}

void SystemVerilogUvmRegisterModelService::require_building(
    const SystemVerilogUvmRegisterBlockHandle handle) const {
  const auto &snapshot = block(handle).snapshot;
  if (!components_->contains_root(snapshot.root)) {
    fail(kInvalidModel, "UVM register-model root is stale");
  }
  if (snapshot.state !=
      SystemVerilogUvmRegisterModelState::Building) {
    fail(kInvalidModel, "locked UVM register-model hierarchy is immutable");
  }
}

SystemVerilogUvmRegisterBlockHandle
SystemVerilogUvmRegisterModelService::create_block(
    SystemVerilogUvmRegisterBlockDescriptor descriptor) {
  validate_name(descriptor.name);
  validate_declaration_capacity(blocks_.size(), limits_.maximum_blocks);

  SystemVerilogUvmRootHandle root = descriptor.root;
  std::size_t depth{1};
  std::string full_name;
  std::uint64_t parent_identity{};
  if (descriptor.parent) {
    require_building(*descriptor.parent);
    const auto &parent = block(*descriptor.parent).snapshot;
    if (root != 0 && root != parent.root) {
      fail(kInvalidModel,
           "UVM register-model child block belongs to another root");
    }
    root = parent.root;
    depth = parent.depth + 1U;
    if (depth > limits_.maximum_block_depth) {
      fail(kResourceLimit, "UVM register-model block depth limit exceeded");
    }
    full_name = child_full_name(parent, descriptor.name);
    parent_identity = parent.identity;
    auto &names = child_names_[parent.identity];
    if (names.size() >= limits_.maximum_declarations_per_block) {
      fail(kResourceLimit,
           "UVM register-model per-block declaration limit exceeded");
    }
    if (names.contains(descriptor.name)) {
      fail(kInvalidModel,
           "UVM register-model block already owns this child name");
    }
  } else {
    if (root == 0 || !components_->contains_root(root)) {
      fail(kInvalidModel,
           "top UVM register-model block has an empty or stale root");
    }
    auto &names = top_names_[root];
    if (names.contains(descriptor.name)) {
      fail(kInvalidModel,
           "UVM root already owns this top register-model block name");
    }
    full_name = std::string{components_->root_identity(root)} + ":" +
                descriptor.name;
    if (full_name.size() > limits_.maximum_full_name_bytes) {
      fail(kResourceLimit,
           "UVM register-model full-name storage limit exceeded");
    }
  }

  record_mutation();
  const auto identity = next_identity();
  const auto order = next_order();
  const auto handle =
      SystemVerilogUvmRegisterBlockHandle{owner_, identity, 1};
  SystemVerilogUvmRegisterBlockSnapshot snapshot{
      handle, identity, root, descriptor.parent, std::move(descriptor.name),
      std::move(full_name), depth, SystemVerilogUvmRegisterModelState::Building,
      order};
  const auto declaration_name = snapshot.name;
  const auto declaration_full_name = snapshot.full_name;
  blocks_.emplace(identity, BlockEntry{snapshot, 1});
  if (descriptor.parent) {
    const auto parent_id = block(*descriptor.parent).snapshot.identity;
    child_names_[parent_id].emplace(snapshot.name);
    child_blocks_[parent_id].push_back(handle);
  } else {
    top_names_[root].emplace(snapshot.name);
  }
  record_declaration(SystemVerilogUvmRegisterDeclarationKind::Block, identity,
                     parent_identity, root, declaration_name,
                     declaration_full_name, order);
  return handle;
}

SystemVerilogUvmRegisterMapHandle
SystemVerilogUvmRegisterModelService::create_map(
    SystemVerilogUvmRegisterMapDescriptor descriptor) {
  validate_name(descriptor.name);
  if (descriptor.bus_width_bytes == 0 ||
      !valid_endianness(descriptor.endianness))
    fail("FSIM-UVM-REG-005", "invalid UVM register-map configuration");
  if (descriptor.bus_width_bytes > limits_.maximum_bus_width_bytes)
    fail("FSIM-UVM-REG-006", "UVM register-map bus-width limit exceeded");
  validate_declaration_capacity(maps_.size(), limits_.maximum_maps);
  require_building(descriptor.block);
  const auto owner_snapshot = block(descriptor.block).snapshot;
  const auto full_name = child_full_name(owner_snapshot, descriptor.name);
  reserve_child_name(descriptor.block, descriptor.name);
  record_mutation();
  const auto identity = next_identity();
  const auto order = next_order();
  const auto handle = SystemVerilogUvmRegisterMapHandle{owner_, identity, 1};
  SystemVerilogUvmRegisterMapSnapshot snapshot{
      handle, identity, descriptor.block, owner_snapshot.root,
      std::move(descriptor.name), full_name, descriptor.base_offset,
      descriptor.bus_width_bytes, descriptor.endianness,
      descriptor.byte_addressing, order};
  const auto declaration_name = snapshot.name;
  maps_.emplace(identity, MapEntry{snapshot, 1});
  map_states_.emplace(identity, MapState{});
  child_names_[owner_snapshot.identity].emplace(snapshot.name);
  record_declaration(SystemVerilogUvmRegisterDeclarationKind::Map, identity,
                     owner_snapshot.identity, owner_snapshot.root,
                     declaration_name, full_name, order);
  return handle;
}

SystemVerilogUvmRegisterHandle
SystemVerilogUvmRegisterModelService::create_register(
    SystemVerilogUvmRegisterDescriptor descriptor) {
  validate_name(descriptor.name);
  validate_declaration_capacity(registers_.size(), limits_.maximum_registers);
  require_building(descriptor.block);
  if (descriptor.width_bits == 0 ||
      descriptor.width_bits > limits_.maximum_width_bits) {
    fail(descriptor.width_bits == 0 ? kInvalidModel : kResourceLimit,
         "UVM register width is zero or exceeds its configured limit");
  }
  if (descriptor.width_bits >
      limits_.maximum_register_value_bits - register_value_bits_) {
    fail(kResourceLimit,
         "UVM aggregate register-value bit limit exceeded");
  }
  validate_span(descriptor.offset, byte_width(descriptor.width_bits),
                "UVM register");
  const auto owner_snapshot = block(descriptor.block).snapshot;
  const auto full_name = child_full_name(owner_snapshot, descriptor.name);
  reserve_child_name(descriptor.block, descriptor.name);
  record_mutation();
  const auto identity = next_identity();
  const auto order = next_order();
  const auto handle = SystemVerilogUvmRegisterHandle{owner_, identity, 1};
  SystemVerilogUvmRegisterSnapshot snapshot{
      handle, identity, descriptor.block, owner_snapshot.root,
      std::move(descriptor.name), full_name, descriptor.width_bits,
      descriptor.offset, order};
  const auto declaration_name = snapshot.name;
  registers_.emplace(identity, RegisterEntry{snapshot, 1});
  register_values_.emplace(
      identity,
      RegisterValueState{PackedLogic4{descriptor.width_bits, Logic4::zero},
                         PackedLogic4{descriptor.width_bits, Logic4::zero}, {}});
  register_value_bits_ += descriptor.width_bits;
  register_fields_.try_emplace(identity);
  child_names_[owner_snapshot.identity].emplace(snapshot.name);
  record_declaration(SystemVerilogUvmRegisterDeclarationKind::Register,
                     identity, owner_snapshot.identity, owner_snapshot.root,
                     declaration_name, full_name, order);
  return handle;
}

SystemVerilogUvmRegisterFieldHandle
SystemVerilogUvmRegisterModelService::create_field(
    SystemVerilogUvmRegisterFieldDescriptor descriptor) {
  validate_name(descriptor.name);
  validate_declaration_capacity(fields_.size(), limits_.maximum_fields);
  const auto register_snapshot = reg(descriptor.register_handle).snapshot;
  require_building(register_snapshot.block);
  if (descriptor.access > SystemVerilogUvmRegisterAccessPolicy::NoAccess ||
      descriptor.compare > SystemVerilogUvmRegisterComparePolicy::NoCheck) {
    fail(kInvalidModel, "UVM register field policy is invalid");
  }
  if (descriptor.width_bits == 0 ||
      descriptor.width_bits > limits_.maximum_width_bits) {
    fail(descriptor.width_bits == 0 ? kInvalidModel : kResourceLimit,
         "UVM register field width is zero or exceeds its configured limit");
  }
  const auto upper = static_cast<std::uint64_t>(
                         descriptor.least_significant_bit) +
                     descriptor.width_bits;
  if (upper > register_snapshot.width_bits) {
    fail(kInvalidModel, "UVM register field does not fit its owning register");
  }
  auto &names = field_names_[register_snapshot.identity];
  if (names.size() >= limits_.maximum_declarations_per_block) {
    fail(kResourceLimit,
         "UVM register field declaration limit exceeded");
  }
  if (names.contains(descriptor.name)) {
    fail(kInvalidModel, "UVM register already owns this field name");
  }
  for (const auto &existing_handle :
       register_fields_[register_snapshot.identity]) {
    const auto &existing = field(existing_handle).snapshot;
    const auto existing_upper =
        static_cast<std::uint64_t>(existing.least_significant_bit) +
        existing.width_bits;
    if (descriptor.least_significant_bit < existing_upper &&
        existing.least_significant_bit < upper) {
      fail(kInvalidModel, "UVM register fields overlap");
    }
  }
  std::string full_name = register_snapshot.full_name;
  if (full_name.size() >= limits_.maximum_full_name_bytes ||
      descriptor.name.size() >
          limits_.maximum_full_name_bytes - full_name.size() - 1U) {
    fail(kResourceLimit,
         "UVM register-model full-name storage limit exceeded");
  }
  full_name.push_back('.');
  full_name.append(descriptor.name);

  record_mutation();
  const auto identity = next_identity();
  const auto order = next_order();
  const auto handle =
      SystemVerilogUvmRegisterFieldHandle{owner_, identity, 1};
  SystemVerilogUvmRegisterFieldSnapshot snapshot{
      handle,
      identity,
      descriptor.register_handle,
      register_snapshot.block,
      register_snapshot.root,
      std::move(descriptor.name),
      full_name,
      descriptor.width_bits,
      descriptor.least_significant_bit,
      order};
  snapshot.access = descriptor.access;
  snapshot.is_volatile = descriptor.is_volatile;
  snapshot.compare = descriptor.compare;
  const auto declaration_name = snapshot.name;
  fields_.emplace(identity, FieldEntry{snapshot, 1});
  field_resets_.try_emplace(identity);
  names.emplace(snapshot.name);
  register_fields_[register_snapshot.identity].push_back(handle);
  record_declaration(SystemVerilogUvmRegisterDeclarationKind::Field, identity,
                     register_snapshot.identity, register_snapshot.root,
                     declaration_name, full_name, order);
  return handle;
}

SystemVerilogUvmRegisterMemoryHandle
SystemVerilogUvmRegisterModelService::create_memory(
    SystemVerilogUvmRegisterMemoryDescriptor descriptor) {
  validate_name(descriptor.name);
  validate_declaration_capacity(memories_.size(), limits_.maximum_memories);
  require_building(descriptor.block);
  if (descriptor.access > SystemVerilogUvmRegisterAccessPolicy::NoAccess)
    fail(kInvalidModel, "UVM memory access policy is invalid");
  if (descriptor.word_width_bits == 0 ||
      descriptor.word_width_bits > limits_.maximum_width_bits) {
    fail(descriptor.word_width_bits == 0 ? kInvalidModel : kResourceLimit,
         "UVM memory word width is zero or exceeds its configured limit");
  }
  if (descriptor.dimensions.empty()) {
    fail(kInvalidModel, "UVM memory must have at least one dimension");
  }
  if (descriptor.dimensions.size() > limits_.maximum_dimensions) {
    fail(kResourceLimit, "UVM memory dimension count limit exceeded");
  }
  std::size_t words{1};
  for (const auto extent : descriptor.dimensions) {
    if (extent == 0) {
      fail(kInvalidModel, "UVM memory dimensions must be nonzero");
    }
    if (extent > limits_.maximum_dimension_extent ||
        words > limits_.maximum_memory_words / extent) {
      fail(kResourceLimit, "UVM memory extent or word-count limit exceeded");
    }
    words *= extent;
  }
  const auto bytes_per_word = byte_width(descriptor.word_width_bits);
  if (static_cast<std::uint64_t>(words) >
      std::numeric_limits<std::uint64_t>::max() / bytes_per_word) {
    fail(kInvalidModel, "UVM memory byte extent overflows 64-bit storage");
  }
  validate_span(descriptor.offset,
                static_cast<std::uint64_t>(words) * bytes_per_word,
                "UVM memory");
  const auto owner_snapshot = block(descriptor.block).snapshot;
  const auto full_name = child_full_name(owner_snapshot, descriptor.name);
  reserve_child_name(descriptor.block, descriptor.name);
  record_mutation();
  const auto identity = next_identity();
  const auto order = next_order();
  const auto handle =
      SystemVerilogUvmRegisterMemoryHandle{owner_, identity, 1};
  SystemVerilogUvmRegisterMemorySnapshot snapshot{
      handle, identity, descriptor.block, owner_snapshot.root,
      std::move(descriptor.name), full_name, descriptor.word_width_bits,
      descriptor.offset, std::move(descriptor.dimensions), words, order};
  snapshot.access = descriptor.access;
  const auto declaration_name = snapshot.name;
  memories_.emplace(identity, MemoryEntry{snapshot, 1});
  memory_values_.try_emplace(identity);
  child_names_[owner_snapshot.identity].emplace(snapshot.name);
  record_declaration(SystemVerilogUvmRegisterDeclarationKind::Memory, identity,
                     owner_snapshot.identity, owner_snapshot.root,
                     declaration_name, full_name, order);
  return handle;
}

void SystemVerilogUvmRegisterModelService::freeze_subtree(
    const SystemVerilogUvmRegisterBlockHandle handle) {
  auto &snapshot = block(handle).snapshot;
  if (const auto children = child_blocks_.find(snapshot.identity);
      children != child_blocks_.end()) {
    for (const auto &child : children->second) {
      freeze_subtree(child);
    }
  }
  snapshot.state = SystemVerilogUvmRegisterModelState::Locked;
}

void SystemVerilogUvmRegisterModelService::lock_model(
    const SystemVerilogUvmRegisterBlockHandle top) {
  auto &snapshot = block(top).snapshot;
  if (snapshot.parent) {
    fail(kInvalidModel, "only a top UVM register block can lock a model");
  }
  require_building(top);
  validate_map_layouts(top);
  record_mutation();
  freeze_subtree(top);
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterBlockHandle handle) const noexcept {
  const auto found = blocks_.find(handle.slot_);
  return handle.owner_ == owner_ && found != blocks_.end() &&
         found->second.generation == handle.generation_;
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterMapHandle handle) const noexcept {
  const auto found = maps_.find(handle.slot_);
  return handle.owner_ == owner_ && found != maps_.end() &&
         found->second.generation == handle.generation_;
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterHandle handle) const noexcept {
  const auto found = registers_.find(handle.slot_);
  return handle.owner_ == owner_ && found != registers_.end() &&
         found->second.generation == handle.generation_;
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterFieldHandle handle) const noexcept {
  const auto found = fields_.find(handle.slot_);
  return handle.owner_ == owner_ && found != fields_.end() &&
         found->second.generation == handle.generation_;
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterMemoryHandle handle) const noexcept {
  const auto found = memories_.find(handle.slot_);
  return handle.owner_ == owner_ && found != memories_.end() &&
         found->second.generation == handle.generation_;
}

SystemVerilogUvmRegisterModelService::BlockEntry &
SystemVerilogUvmRegisterModelService::block(
    const SystemVerilogUvmRegisterBlockHandle handle) {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register block handle is empty, stale, or foreign");
  return blocks_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::BlockEntry &
SystemVerilogUvmRegisterModelService::block(
    const SystemVerilogUvmRegisterBlockHandle handle) const {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register block handle is empty, stale, or foreign");
  return blocks_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::MapEntry &
SystemVerilogUvmRegisterModelService::map(
    const SystemVerilogUvmRegisterMapHandle handle) const {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register map handle is empty, stale, or foreign");
  return maps_.at(handle.slot_);
}

SystemVerilogUvmRegisterModelService::MapEntry &
SystemVerilogUvmRegisterModelService::map(
    const SystemVerilogUvmRegisterMapHandle handle) {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register map handle is empty, stale, or foreign");
  return maps_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::RegisterEntry &
SystemVerilogUvmRegisterModelService::reg(
    const SystemVerilogUvmRegisterHandle handle) const {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register handle is empty, stale, or foreign");
  return registers_.at(handle.slot_);
}

SystemVerilogUvmRegisterModelService::RegisterEntry &
SystemVerilogUvmRegisterModelService::reg(
    const SystemVerilogUvmRegisterHandle handle) {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register handle is empty, stale, or foreign");
  return registers_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::FieldEntry &
SystemVerilogUvmRegisterModelService::field(
    const SystemVerilogUvmRegisterFieldHandle handle) const {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register field handle is empty, stale, or foreign");
  return fields_.at(handle.slot_);
}

SystemVerilogUvmRegisterModelService::FieldEntry &
SystemVerilogUvmRegisterModelService::field(
    const SystemVerilogUvmRegisterFieldHandle handle) {
  if (!contains(handle))
    fail(kInvalidModel, "UVM register field handle is empty, stale, or foreign");
  return fields_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::MemoryEntry &
SystemVerilogUvmRegisterModelService::memory(
    const SystemVerilogUvmRegisterMemoryHandle handle) const {
  if (!contains(handle))
    fail(kInvalidModel, "UVM memory handle is empty, stale, or foreign");
  return memories_.at(handle.slot_);
}

SystemVerilogUvmRegisterModelService::MemoryEntry &
SystemVerilogUvmRegisterModelService::memory(
    const SystemVerilogUvmRegisterMemoryHandle handle) {
  if (!contains(handle))
    fail(kInvalidModel, "UVM memory handle is empty, stale, or foreign");
  return memories_.at(handle.slot_);
}

SystemVerilogUvmRegisterBlockSnapshot
SystemVerilogUvmRegisterModelService::snapshot(
    const SystemVerilogUvmRegisterBlockHandle handle) const {
  return block(handle).snapshot;
}

SystemVerilogUvmRegisterMapSnapshot
SystemVerilogUvmRegisterModelService::snapshot(
    const SystemVerilogUvmRegisterMapHandle handle) const {
  return map(handle).snapshot;
}

SystemVerilogUvmRegisterSnapshot
SystemVerilogUvmRegisterModelService::snapshot(
    const SystemVerilogUvmRegisterHandle handle) const {
  return reg(handle).snapshot;
}

SystemVerilogUvmRegisterFieldSnapshot
SystemVerilogUvmRegisterModelService::snapshot(
    const SystemVerilogUvmRegisterFieldHandle handle) const {
  return field(handle).snapshot;
}

SystemVerilogUvmRegisterMemorySnapshot
SystemVerilogUvmRegisterModelService::snapshot(
    const SystemVerilogUvmRegisterMemoryHandle handle) const {
  return memory(handle).snapshot;
}

std::vector<SystemVerilogUvmRegisterBlockSnapshot>
SystemVerilogUvmRegisterModelService::blocks() const {
  std::vector<SystemVerilogUvmRegisterBlockSnapshot> result;
  result.reserve(blocks_.size());
  for (const auto &[identity, entry] : blocks_) {
    (void)identity;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterMapSnapshot>
SystemVerilogUvmRegisterModelService::maps() const {
  std::vector<SystemVerilogUvmRegisterMapSnapshot> result;
  result.reserve(maps_.size());
  for (const auto &[identity, entry] : maps_) {
    (void)identity;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterSnapshot>
SystemVerilogUvmRegisterModelService::registers() const {
  std::vector<SystemVerilogUvmRegisterSnapshot> result;
  result.reserve(registers_.size());
  for (const auto &[identity, entry] : registers_) {
    (void)identity;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterFieldSnapshot>
SystemVerilogUvmRegisterModelService::fields() const {
  std::vector<SystemVerilogUvmRegisterFieldSnapshot> result;
  result.reserve(fields_.size());
  for (const auto &[identity, entry] : fields_) {
    (void)identity;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterMemorySnapshot>
SystemVerilogUvmRegisterModelService::memories() const {
  std::vector<SystemVerilogUvmRegisterMemorySnapshot> result;
  result.reserve(memories_.size());
  for (const auto &[identity, entry] : memories_) {
    (void)identity;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterDeclarationSnapshot>
SystemVerilogUvmRegisterModelService::declarations() const {
  return declarations_;
}

} // namespace fsim::runtime
