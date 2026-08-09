// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <tuple>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidMap{"FSIM-UVM-REG-005"};
constexpr std::string_view kMapLimit{"FSIM-UVM-REG-006"};

[[noreturn]] void fail_map(const std::string_view code,
                           const std::string_view message) {
  throw SystemVerilogUvmRegisterModelError{std::string{code},
                                           std::string{message}};
}

[[nodiscard]] bool valid_rights(const SystemVerilogUvmRegisterMapRights value) {
  return value == SystemVerilogUvmRegisterMapRights::ReadWrite ||
         value == SystemVerilogUvmRegisterMapRights::ReadOnly ||
         value == SystemVerilogUvmRegisterMapRights::WriteOnly;
}

[[nodiscard]] bool map_readable(const SystemVerilogUvmRegisterMapRights value) {
  return value != SystemVerilogUvmRegisterMapRights::WriteOnly;
}

[[nodiscard]] bool map_writable(const SystemVerilogUvmRegisterMapRights value) {
  return value != SystemVerilogUvmRegisterMapRights::ReadOnly;
}

[[nodiscard]] bool
policy_readable(const SystemVerilogUvmRegisterAccessPolicy policy) {
  return policy != SystemVerilogUvmRegisterAccessPolicy::WriteOnly &&
         policy != SystemVerilogUvmRegisterAccessPolicy::WriteOnce &&
         policy != SystemVerilogUvmRegisterAccessPolicy::WriteOnlyClear &&
         policy != SystemVerilogUvmRegisterAccessPolicy::WriteOnlySet &&
         policy != SystemVerilogUvmRegisterAccessPolicy::NoAccess;
}

[[nodiscard]] bool
policy_writable(const SystemVerilogUvmRegisterAccessPolicy policy) {
  return policy != SystemVerilogUvmRegisterAccessPolicy::ReadOnly &&
         policy != SystemVerilogUvmRegisterAccessPolicy::ReadClear &&
         policy != SystemVerilogUvmRegisterAccessPolicy::ReadSet &&
         policy != SystemVerilogUvmRegisterAccessPolicy::NoAccess;
}

[[nodiscard]] std::size_t bytes_for_bits(const std::size_t bits) {
  return bits / 8U + static_cast<std::size_t>((bits % 8U) != 0U);
}

void validate_burst_values(
    const std::span<const PackedLogic4> values,
    const std::span<const std::vector<std::uint8_t>> byte_enables,
    const std::size_t width_bits) {
  const auto word_bytes = bytes_for_bits(width_bits);
  for (std::size_t word = 0; word < values.size(); ++word) {
    if (values[word].width() != width_bits)
      fail_map(kInvalidMap, "invalid UVM register-memory burst value");
    if (!byte_enables.empty() &&
        (byte_enables[word].size() != word_bytes ||
         std::ranges::any_of(byte_enables[word],
                             [](const auto value) { return value > 1U; })))
      fail_map(kInvalidMap, "invalid UVM register-memory byte enables");
    for (std::size_t bit = 0; bit < values[word].width(); ++bit) {
      if (!byte_enables.empty() && byte_enables[word][bit / 8U] == 0)
        continue;
      const auto logic = values[word].get(bit);
      if (logic == Logic4::x || logic == Logic4::z)
        fail_map(kInvalidMap, "invalid UVM register-memory burst value");
    }
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const std::string_view message) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    fail_map(kInvalidMap, message);
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const std::string_view message) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
    fail_map(kInvalidMap, message);
  return left * right;
}

struct AddressRange {
  std::uint64_t first{};
  std::uint64_t last{};
};

[[nodiscard]] bool overlaps(const AddressRange left, const AddressRange right) {
  return left.first <= right.last && right.first <= left.last;
}

[[nodiscard]] std::uint64_t
checked_layout_size(std::vector<AddressRange> ranges) {
  std::ranges::sort(ranges, [](const auto left, const auto right) {
    return std::tie(left.first, left.last) < std::tie(right.first, right.last);
  });
  for (std::size_t index = 1; index < ranges.size(); ++index) {
    if (overlaps(ranges[index - 1U], ranges[index]))
      fail_map(kInvalidMap, "overlapping UVM register-map address ranges");
  }
  std::uint64_t size{1};
  for (const auto range : ranges)
    size = std::max(size, checked_add(range.last, 1,
                                     "UVM register-map size overflow"));
  return size;
}

} // namespace

void SystemVerilogUvmRegisterModelService::add_register(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterHandle register_handle,
    const std::uint64_t offset,
    const SystemVerilogUvmRegisterMapRights rights, const bool unmapped) {
  if (!valid_rights(rights))
    fail_map(kInvalidMap, "invalid UVM register-map rights");
  const auto map_snapshot = map(map_handle).snapshot;
  const auto register_snapshot = reg(register_handle).snapshot;
  require_building(map_snapshot.block);
  if (map_snapshot.block != register_snapshot.block)
    fail_map(kInvalidMap, "register and map do not belong to the same block");
  auto &state = map_states_.at(map_snapshot.identity);
  if (std::ranges::any_of(state.registers, [&](const auto &entry) {
        return entry.register_handle == register_handle;
      }))
    fail_map(kInvalidMap, "register is already present in UVM register map");
  if (map_registrations_ >= limits_.maximum_map_registrations ||
      state.registers.size() + state.memories.size() + state.children.size() >=
          limits_.maximum_map_registrations_per_map)
    fail_map(kMapLimit, "UVM register-map registration limit exceeded");
  if (!unmapped) {
    const auto bytes = bytes_for_bits(register_snapshot.width_bits);
    (void)physical_accesses_impl(map_handle, offset, bytes, 0, 0);
  }
  record_mutation();
  state.registers.push_back({map_handle, register_handle, offset, rights,
                             unmapped, next_mapping_order_++});
  ++map_registrations_;
}

void SystemVerilogUvmRegisterModelService::add_memory(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterMemoryHandle memory_handle,
    const std::uint64_t offset,
    const SystemVerilogUvmRegisterMapRights rights, const bool unmapped) {
  if (!valid_rights(rights))
    fail_map(kInvalidMap, "invalid UVM register-map rights");
  const auto map_snapshot = map(map_handle).snapshot;
  const auto memory_snapshot = memory(memory_handle).snapshot;
  require_building(map_snapshot.block);
  if (map_snapshot.block != memory_snapshot.block)
    fail_map(kInvalidMap, "memory and map do not belong to the same block");
  auto &state = map_states_.at(map_snapshot.identity);
  if (std::ranges::any_of(state.memories, [&](const auto &entry) {
        return entry.memory == memory_handle;
      }))
    fail_map(kInvalidMap, "memory is already present in UVM register map");
  if (map_registrations_ >= limits_.maximum_map_registrations ||
      state.registers.size() + state.memories.size() + state.children.size() >=
          limits_.maximum_map_registrations_per_map)
    fail_map(kMapLimit, "UVM register-map registration limit exceeded");
  if (!unmapped) {
    const auto bytes = bytes_for_bits(memory_snapshot.word_width_bits);
    (void)physical_accesses_impl(map_handle, offset, bytes,
                                 memory_snapshot.word_count - 1U, bytes);
  }
  record_mutation();
  state.memories.push_back({map_handle, memory_handle, offset, rights, unmapped,
                            next_mapping_order_++});
  ++map_registrations_;
}

void SystemVerilogUvmRegisterModelService::add_submap(
    const SystemVerilogUvmRegisterMapHandle parent_handle,
    const SystemVerilogUvmRegisterMapHandle child_handle,
    const std::uint64_t offset) {
  if (parent_handle == child_handle)
    fail_map(kInvalidMap, "UVM register map cannot contain itself");
  const auto parent_snapshot = map(parent_handle).snapshot;
  const auto child_snapshot = map(child_handle).snapshot;
  require_building(parent_snapshot.block);
  require_building(child_snapshot.block);
  if (parent_snapshot.root != child_snapshot.root)
    fail_map(kInvalidMap, "cross-root UVM register submap is invalid");
  auto ancestor = std::optional<SystemVerilogUvmRegisterBlockHandle>{
      child_snapshot.block};
  bool owns_child{};
  while (ancestor) {
    if (*ancestor == parent_snapshot.block) {
      owns_child = true;
      break;
    }
    ancestor = block(*ancestor).snapshot.parent;
  }
  if (!owns_child)
    fail_map(kInvalidMap, "submap block is not below its parent-map block");
  if (address_unit_bytes(parent_handle) != address_unit_bytes(child_handle))
    fail_map(kInvalidMap, "submap address-unit width is not aligned");
  auto &parent_state = map_states_.at(parent_snapshot.identity);
  auto &child_state = map_states_.at(child_snapshot.identity);
  if (child_state.parent)
    fail_map(kInvalidMap, "UVM register submap already has a parent map");
  if (map_registrations_ >= limits_.maximum_map_registrations ||
      parent_state.registers.size() + parent_state.memories.size() +
              parent_state.children.size() >=
          limits_.maximum_map_registrations_per_map)
    fail_map(kMapLimit, "UVM register-map registration limit exceeded");
  auto parent = std::optional<SystemVerilogUvmRegisterMapHandle>{parent_handle};
  std::size_t depth{1};
  while (parent) {
    if (*parent == child_handle)
      fail_map(kInvalidMap, "cyclic UVM register submap hierarchy");
    const auto &state = map_states_.at(map(*parent).snapshot.identity);
    parent = state.parent;
    ++depth;
  }
  std::size_t descendant_depth{1};
  std::vector<std::pair<SystemVerilogUvmRegisterMapHandle, std::size_t>>
      descendants{{child_handle, 1}};
  std::size_t hierarchy_work{};
  while (!descendants.empty()) {
    const auto [current, local_depth] = descendants.back();
    descendants.pop_back();
    descendant_depth = std::max(descendant_depth, local_depth);
    const auto &state = map_states_.at(map(current).snapshot.identity);
    if (state.children.size() > limits_.maximum_address_lookup_work ||
        hierarchy_work > limits_.maximum_address_lookup_work -
                             state.children.size())
      fail_map(kMapLimit, "UVM register submap hierarchy-work limit exceeded");
    hierarchy_work += state.children.size();
    for (const auto &relation : state.children)
      descendants.emplace_back(relation.child, local_depth + 1U);
  }
  if (descendant_depth > limits_.maximum_submap_depth ||
      depth > limits_.maximum_submap_depth - descendant_depth + 1U)
    fail_map(kMapLimit, "UVM register submap-depth limit exceeded");
  record_mutation();
  const SystemVerilogUvmRegisterSubmapSnapshot relation{
      parent_handle, child_handle, offset, next_mapping_order_++};
  parent_state.children.push_back(relation);
  child_state.parent = parent_handle;
  ++map_registrations_;
}

std::size_t SystemVerilogUvmRegisterModelService::map_depth(
    const SystemVerilogUvmRegisterMapHandle handle) const {
  std::size_t depth{1};
  auto current = handle;
  while (const auto parent = map_states_.at(map(current).snapshot.identity).parent) {
    current = *parent;
    if (++depth > limits_.maximum_submap_depth)
      fail_map(kMapLimit, "UVM register submap-depth limit exceeded");
  }
  return depth;
}

SystemVerilogUvmRegisterMapHandle
SystemVerilogUvmRegisterModelService::root_map(
    SystemVerilogUvmRegisterMapHandle handle) const {
  while (const auto parent = map_states_.at(map(handle).snapshot.identity).parent)
    handle = *parent;
  return handle;
}

std::uint32_t SystemVerilogUvmRegisterModelService::address_unit_bytes(
    const SystemVerilogUvmRegisterMapHandle handle) const {
  const auto snapshot = map(handle).snapshot;
  return snapshot.byte_addressing ? 1U : snapshot.bus_width_bytes;
}

std::uint64_t SystemVerilogUvmRegisterModelService::hierarchical_base(
    SystemVerilogUvmRegisterMapHandle handle) const {
  std::uint64_t base{};
  while (true) {
    const auto snapshot = map(handle).snapshot;
    const auto &state = map_states_.at(snapshot.identity);
    if (!state.parent)
      return checked_add(base, snapshot.base_offset,
                         "UVM register-map base address overflow");
    const auto &parent_state =
        map_states_.at(map(*state.parent).snapshot.identity);
    const auto relation = std::ranges::find_if(
        parent_state.children,
        [&](const auto &entry) { return entry.child == handle; });
    if (relation == parent_state.children.end())
      fail_map(kInvalidMap, "broken UVM register submap hierarchy");
    base = checked_add(base, relation->offset,
                       "UVM register submap address overflow");
    handle = *state.parent;
  }
}

std::vector<SystemVerilogUvmRegisterBusBeat>
SystemVerilogUvmRegisterModelService::physical_accesses(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const std::uint64_t offset, const std::size_t byte_count,
    const std::size_t memory_word_offset,
    const std::size_t memory_word_bytes) const {
  require_locked(map(map_handle).snapshot.block);
  return physical_accesses_impl(map_handle, offset, byte_count,
                                memory_word_offset, memory_word_bytes);
}

std::vector<SystemVerilogUvmRegisterBusBeat>
SystemVerilogUvmRegisterModelService::physical_accesses_impl(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const std::uint64_t offset, const std::size_t byte_count,
    const std::size_t memory_word_offset,
    const std::size_t memory_word_bytes) const {
  return materialize_physical_accesses(make_physical_access_plan(
      map_handle, offset, byte_count, memory_word_offset, memory_word_bytes));
}

SystemVerilogUvmRegisterModelService::PhysicalAccessPlan
SystemVerilogUvmRegisterModelService::make_physical_access_plan(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const std::uint64_t offset, const std::size_t byte_count,
    const std::size_t memory_word_offset,
    const std::size_t memory_word_bytes) const {
  const auto leaf = map(map_handle).snapshot;
  if (byte_count == 0)
    fail_map(kInvalidMap, "zero-byte UVM register-map access");
  const auto aub = address_unit_bytes(map_handle);
  std::uint64_t word_delta{};
  if (memory_word_offset != 0) {
    if (memory_word_bytes == 0)
      fail_map(kInvalidMap, "memory word geometry is missing");
    if (memory_word_bytes >= aub) {
      if (memory_word_bytes % aub != 0)
        fail_map(kInvalidMap, "memory word is not address-unit aligned");
      word_delta = checked_multiply(
          memory_word_offset, memory_word_bytes / aub,
          "UVM register-memory word address overflow");
    } else {
      word_delta = memory_word_offset;
    }
  }
  auto local_address = checked_add(offset, word_delta,
                                   "UVM register-map offset overflow");
  local_address = checked_add(hierarchical_base(map_handle), local_address,
                              "UVM register-map address overflow");

  auto effective_bus = leaf.bus_width_bytes;
  auto current = map_handle;
  while (const auto parent = map_states_.at(map(current).snapshot.identity).parent) {
    current = *parent;
    effective_bus = std::min(effective_bus, map(current).snapshot.bus_width_bytes);
  }
  if (effective_bus % aub != 0)
    fail_map(kInvalidMap, "UVM register-map bus is not address-unit aligned");
  const auto beat_count =
      byte_count / effective_bus +
      static_cast<std::size_t>((byte_count % effective_bus) != 0U);
  if (beat_count > limits_.maximum_physical_beats ||
      beat_count > limits_.maximum_physical_byte_enables / effective_bus)
    fail_map(kMapLimit, "UVM register-map physical-beat limit exceeded");
  const auto root = map(root_map(map_handle)).snapshot;
  return {local_address, byte_count, effective_bus, aub, beat_count,
          root.endianness};
}

std::vector<SystemVerilogUvmRegisterBusBeat>
SystemVerilogUvmRegisterModelService::materialize_physical_accesses(
    const PhysicalAccessPlan &plan) const {
  const auto step = plan.bus_width_bytes / plan.address_unit_bytes;
  std::vector<SystemVerilogUvmRegisterBusBeat> result;
  result.reserve(plan.beat_count);
  for (std::size_t logical = 0; logical < plan.beat_count; ++logical) {
    std::size_t address_index = logical;
    std::size_t data_index = logical;
    switch (plan.endianness) {
    case SystemVerilogUvmRegisterMapEndianness::Little:
      break;
    case SystemVerilogUvmRegisterMapEndianness::Big:
      address_index = plan.beat_count - 1U - logical;
      break;
    case SystemVerilogUvmRegisterMapEndianness::LittleFifo:
      address_index = 0;
      break;
    case SystemVerilogUvmRegisterMapEndianness::BigFifo:
      address_index = 0;
      data_index = plan.beat_count - 1U - logical;
      break;
    }
    const auto byte_offset = data_index * plan.bus_width_bytes;
    const auto active =
        std::min(static_cast<std::size_t>(plan.bus_width_bytes),
                 plan.byte_count - byte_offset);
    const auto address_delta = checked_multiply(
        address_index, step, "UVM register-map beat address overflow");
    result.push_back({checked_add(plan.address, address_delta,
                                  "UVM register-map beat address overflow"),
                      byte_offset,
                      std::vector<std::uint8_t>(plan.bus_width_bytes, 0)});
    std::fill_n(result.back().byte_enables.begin(), active,
                static_cast<std::uint8_t>(1));
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterBusBeat>
SystemVerilogUvmRegisterModelService::register_accesses(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterHandle register_handle) const {
  const auto map_snapshot = map(map_handle).snapshot;
  require_locked(map_snapshot.block);
  const auto register_snapshot = reg(register_handle).snapshot;
  const auto &entries = map_states_.at(map_snapshot.identity).registers;
  const auto entry = std::ranges::find_if(entries, [&](const auto &candidate) {
    return candidate.register_handle == register_handle;
  });
  if (entry == entries.end() || entry->unmapped)
    fail_map(kInvalidMap, "register is not physically mapped in this map");
  return physical_accesses(map_handle, entry->offset,
                           bytes_for_bits(register_snapshot.width_bits));
}

std::vector<SystemVerilogUvmRegisterBusBeat>
SystemVerilogUvmRegisterModelService::memory_accesses(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterMemoryHandle memory_handle,
    const std::size_t word_index) const {
  const auto map_snapshot = map(map_handle).snapshot;
  require_locked(map_snapshot.block);
  const auto memory_snapshot = memory(memory_handle).snapshot;
  if (word_index >= memory_snapshot.word_count)
    fail_map(kInvalidMap, "UVM register-memory word index is out of range");
  const auto &entries = map_states_.at(map_snapshot.identity).memories;
  const auto entry = std::ranges::find_if(entries, [&](const auto &candidate) {
    return candidate.memory == memory_handle;
  });
  if (entry == entries.end() || entry->unmapped)
    fail_map(kInvalidMap, "memory is not physically mapped in this map");
  const auto bytes = bytes_for_bits(memory_snapshot.word_width_bits);
  return physical_accesses(map_handle, entry->offset, bytes, word_index, bytes);
}

std::vector<SystemVerilogUvmRegisterMapRegisterSnapshot>
SystemVerilogUvmRegisterModelService::mapped_registers(
    const SystemVerilogUvmRegisterMapHandle handle,
    const bool hierarchical) const {
  (void)map(handle);
  std::vector<SystemVerilogUvmRegisterMapRegisterSnapshot> result;
  std::vector<SystemVerilogUvmRegisterMapHandle> pending{handle};
  std::size_t work{};
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    const auto &state = map_states_.at(map(current).snapshot.identity);
    if (state.registers.size() > limits_.maximum_address_lookup_work ||
        work > limits_.maximum_address_lookup_work - state.registers.size())
      fail_map(kMapLimit, "UVM register-map lookup-work limit exceeded");
    work += state.registers.size();
    result.insert(result.end(), state.registers.begin(), state.registers.end());
    if (hierarchical) {
      for (const auto &child : state.children)
        pending.push_back(child.child);
    }
  }
  std::ranges::sort(result, {}, &SystemVerilogUvmRegisterMapRegisterSnapshot::registration_order);
  return result;
}

std::vector<SystemVerilogUvmRegisterMapMemorySnapshot>
SystemVerilogUvmRegisterModelService::mapped_memories(
    const SystemVerilogUvmRegisterMapHandle handle,
    const bool hierarchical) const {
  (void)map(handle);
  std::vector<SystemVerilogUvmRegisterMapMemorySnapshot> result;
  std::vector<SystemVerilogUvmRegisterMapHandle> pending{handle};
  std::size_t work{};
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    const auto &state = map_states_.at(map(current).snapshot.identity);
    if (state.memories.size() > limits_.maximum_address_lookup_work ||
        work > limits_.maximum_address_lookup_work - state.memories.size())
      fail_map(kMapLimit, "UVM register-map lookup-work limit exceeded");
    work += state.memories.size();
    result.insert(result.end(), state.memories.begin(), state.memories.end());
    if (hierarchical) {
      for (const auto &child : state.children)
        pending.push_back(child.child);
    }
  }
  std::ranges::sort(result, {}, &SystemVerilogUvmRegisterMapMemorySnapshot::registration_order);
  return result;
}

std::vector<SystemVerilogUvmRegisterSubmapSnapshot>
SystemVerilogUvmRegisterModelService::submaps(
    const SystemVerilogUvmRegisterMapHandle handle,
    const bool hierarchical) const {
  (void)map(handle);
  std::vector<SystemVerilogUvmRegisterSubmapSnapshot> result;
  std::vector<SystemVerilogUvmRegisterMapHandle> pending{handle};
  std::size_t work{};
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    const auto &state = map_states_.at(map(current).snapshot.identity);
    if (state.children.size() > limits_.maximum_address_lookup_work ||
        work > limits_.maximum_address_lookup_work - state.children.size())
      fail_map(kMapLimit, "UVM register-map lookup-work limit exceeded");
    work += state.children.size();
    result.insert(result.end(), state.children.begin(), state.children.end());
    if (hierarchical) {
      for (const auto &child : state.children)
        pending.push_back(child.child);
    }
  }
  std::ranges::sort(result, {}, &SystemVerilogUvmRegisterSubmapSnapshot::registration_order);
  return result;
}

std::optional<SystemVerilogUvmRegisterAddressLookup>
SystemVerilogUvmRegisterModelService::lookup_register_address(
    const SystemVerilogUvmRegisterMapHandle root,
    const std::uint64_t address, const bool read_access,
    std::size_t &work) const {
  std::optional<SystemVerilogUvmRegisterAddressLookup> found;
  for (const auto &entry : mapped_registers(root, true)) {
    if (entry.unmapped ||
        (read_access ? !map_readable(entry.rights)
                     : !map_writable(entry.rights)))
      continue;
    for (const auto &beat : register_accesses(entry.map, entry.register_handle)) {
      if (++work > limits_.maximum_address_lookup_work)
        fail_map(kMapLimit, "UVM register-map lookup-work limit exceeded");
      if (beat.address == address) {
        if (found)
          fail_map(kInvalidMap, "ambiguous UVM register-map address lookup");
        found = SystemVerilogUvmRegisterAddressLookup{
            entry.register_handle, std::nullopt, 0, entry.rights,
            entry.registration_order};
      }
    }
  }
  return found;
}

std::optional<SystemVerilogUvmRegisterAddressLookup>
SystemVerilogUvmRegisterModelService::lookup_memory_address(
    const SystemVerilogUvmRegisterMapHandle root,
    const std::uint64_t address, const bool read_access,
    std::size_t &work) const {
  std::optional<SystemVerilogUvmRegisterAddressLookup> found;
  for (const auto &entry : mapped_memories(root, true)) {
    if (entry.unmapped ||
        (read_access ? !map_readable(entry.rights)
                     : !map_writable(entry.rights)))
      continue;
    const auto memory_snapshot = memory(entry.memory).snapshot;
    const auto bytes = bytes_for_bits(memory_snapshot.word_width_bits);
    const auto aub = address_unit_bytes(entry.map);
    const auto stride = bytes >= aub ? bytes / aub : 1U;
    const auto first = checked_add(hierarchical_base(entry.map), entry.offset,
                                   "UVM register-memory address overflow");
    if (address < first)
      continue;
    const auto candidate = static_cast<std::size_t>((address - first) / stride);
    if (candidate >= memory_snapshot.word_count)
      continue;
    for (const auto &beat : memory_accesses(entry.map, entry.memory, candidate)) {
      if (++work > limits_.maximum_address_lookup_work)
        fail_map(kMapLimit, "UVM register-map lookup-work limit exceeded");
      if (beat.address == address) {
        if (found)
          fail_map(kInvalidMap, "ambiguous UVM register-map address lookup");
        found = SystemVerilogUvmRegisterAddressLookup{
            std::nullopt, entry.memory, candidate, entry.rights,
            entry.registration_order};
      }
    }
  }
  return found;
}

std::optional<SystemVerilogUvmRegisterAddressLookup>
SystemVerilogUvmRegisterModelService::lookup(
    const SystemVerilogUvmRegisterMapHandle handle,
    const std::uint64_t address, const bool read_access) const {
  const auto root = root_map(handle);
  require_locked(map(root).snapshot.block);
  std::size_t work{};
  auto result = lookup_register_address(root, address, read_access, work);
  const auto memory_result =
      lookup_memory_address(root, address, read_access, work);
  if (result && memory_result)
    fail_map(kInvalidMap, "ambiguous UVM register-map address lookup");
  if (memory_result)
    result = memory_result;
  return result;
}

std::vector<std::size_t>
SystemVerilogUvmRegisterModelService::unflatten_memory_index(
    const SystemVerilogUvmRegisterMemorySnapshot &memory_snapshot,
    std::size_t index) const {
  if (index >= memory_snapshot.word_count)
    fail_map(kInvalidMap, "UVM register-memory word index is out of range");
  std::vector<std::size_t> result(memory_snapshot.dimensions.size());
  for (std::size_t dimension = result.size(); dimension-- != 0;) {
    result[dimension] = index % memory_snapshot.dimensions[dimension];
    index /= memory_snapshot.dimensions[dimension];
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterModelService::MemoryWordBackup>
SystemVerilogUvmRegisterModelService::backup_memory_words(
    const SystemVerilogUvmRegisterMemorySnapshot &memory_snapshot,
    const std::size_t first_word, const std::size_t word_count) const {
  const auto &state = memory_values_.at(memory_snapshot.identity);
  std::vector<MemoryWordBackup> result;
  result.reserve(word_count);
  for (std::size_t word = 0; word < word_count; ++word) {
    const auto index = first_word + word;
    const auto desired = state.desired.find(index);
    const auto mirrored = state.mirrored.find(index);
    result.push_back({index,
                      desired == state.desired.end()
                          ? std::optional<PackedLogic4>{}
                          : desired->second,
                      mirrored == state.mirrored.end()
                          ? std::optional<PackedLogic4>{}
                          : mirrored->second,
                      state.written.contains(index)});
  }
  return result;
}

void SystemVerilogUvmRegisterModelService::restore_memory_words(
    const SystemVerilogUvmRegisterMemorySnapshot &memory_snapshot,
    const std::span<const MemoryWordBackup> backup,
    const std::uint64_t operations, const std::uint64_t mutations,
    const std::size_t materialized_bits) {
  auto &state = memory_values_.at(memory_snapshot.identity);
  for (const auto &word : backup) {
    if (word.desired)
      state.desired.insert_or_assign(word.index, *word.desired);
    else
      state.desired.erase(word.index);
    if (word.mirrored)
      state.mirrored.insert_or_assign(word.index, *word.mirrored);
    else
      state.mirrored.erase(word.index);
    if (word.written)
      state.written.insert(word.index);
    else
      state.written.erase(word.index);
  }
  operations_ = operations;
  mutations_ = mutations;
  materialized_memory_bits_ = materialized_bits;
}

SystemVerilogUvmRegisterMemoryBurstResult
SystemVerilogUvmRegisterModelService::read_burst(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterMemoryHandle memory_handle,
    const std::size_t first_word, const std::size_t word_count) {
  const auto map_snapshot = map(map_handle).snapshot;
  require_locked(map_snapshot.block);
  const auto memory_snapshot = memory(memory_handle).snapshot;
  if (word_count == 0 || word_count > limits_.maximum_burst_words ||
      first_word > memory_snapshot.word_count ||
      word_count > memory_snapshot.word_count - first_word)
    fail_map(word_count > limits_.maximum_burst_words ? kMapLimit : kInvalidMap,
             "invalid UVM register-memory burst range");
  if (word_count >
      limits_.maximum_burst_value_bits / memory_snapshot.word_width_bits)
    fail_map(kMapLimit, "UVM register-memory burst-value limit exceeded");
  const auto entries = mapped_memories(map_handle, true);
  const auto entry = std::ranges::find_if(entries, [&](const auto &candidate) {
    return candidate.memory == memory_handle;
  });
  if (entry == entries.end() || entry->unmapped ||
      !map_readable(entry->rights) || !policy_readable(memory_snapshot.access)) {
    record_operation(false);
    return {SystemVerilogUvmRegisterOperationStatus::NotOk, {}, 0, false,
            "memory is not readable through this map"};
  }
  if (word_count > limits_.maximum_operations ||
      operations_ > limits_.maximum_operations - word_count)
    fail_map(kMapLimit, "UVM register-memory burst operation limit exceeded");
  const auto &state = memory_values_.at(memory_snapshot.identity);
  std::size_t new_words{};
  for (std::size_t word = 0; word < word_count; ++word)
    new_words += !state.mirrored.contains(first_word + word);
  const auto new_bits =
      checked_multiply(new_words, memory_snapshot.word_width_bits,
                       "UVM register-memory burst storage overflow");
  if (new_words > limits_.maximum_materialized_memory_words -
                      state.mirrored.size() ||
      new_bits > limits_.maximum_materialized_memory_bits -
                     materialized_memory_bits_)
    fail_map(kMapLimit, "UVM register-memory burst storage limit exceeded");
  SystemVerilogUvmRegisterMemoryBurstResult result;
  result.values.reserve(word_count);
  const auto backup =
      backup_memory_words(memory_snapshot, first_word, word_count);
  const auto operations_before = operations_;
  const auto mutations_before = mutations_;
  const auto materialized_bits_before = materialized_memory_bits_;
  try {
    for (std::size_t word = 0; word < word_count; ++word) {
      auto operation = read(
          memory_handle,
          unflatten_memory_index(memory_snapshot, first_word + word));
      result.values.push_back(operation.value);
      result.changed = result.changed || operation.changed;
      ++result.completed_words;
    }
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    restore_memory_words(memory_snapshot, backup, operations_before,
                         mutations_before, materialized_bits_before);
    if (error.diagnostic_code() == "FSIM-UVM-REG-004")
      fail_map(kMapLimit, "UVM register-memory burst operation limit exceeded");
    throw;
  } catch (...) {
    restore_memory_words(memory_snapshot, backup, operations_before,
                         mutations_before, materialized_bits_before);
    throw;
  }
  return result;
}

SystemVerilogUvmRegisterMemoryBurstResult
SystemVerilogUvmRegisterModelService::write_burst(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterMemoryHandle memory_handle,
    const std::size_t first_word, const std::span<const PackedLogic4> values,
    const std::span<const std::vector<std::uint8_t>> byte_enables) {
  const auto map_snapshot = map(map_handle).snapshot;
  require_locked(map_snapshot.block);
  const auto memory_snapshot = memory(memory_handle).snapshot;
  if (values.empty() || values.size() > limits_.maximum_burst_words ||
      first_word > memory_snapshot.word_count ||
      values.size() > memory_snapshot.word_count - first_word ||
      (!byte_enables.empty() && byte_enables.size() != values.size()))
    fail_map(values.size() > limits_.maximum_burst_words ? kMapLimit
                                                         : kInvalidMap,
             "invalid UVM register-memory burst write");
  if (values.size() >
      limits_.maximum_burst_value_bits / memory_snapshot.word_width_bits)
    fail_map(kMapLimit, "UVM register-memory burst-value limit exceeded");
  const auto entries = mapped_memories(map_handle, true);
  const auto entry = std::ranges::find_if(entries, [&](const auto &candidate) {
    return candidate.memory == memory_handle;
  });
  if (entry == entries.end() || entry->unmapped ||
      !map_writable(entry->rights) || !policy_writable(memory_snapshot.access)) {
    record_operation(false);
    return {SystemVerilogUvmRegisterOperationStatus::NotOk, {}, 0, false,
            "memory is not writable through this map"};
  }
  validate_burst_values(values, byte_enables, memory_snapshot.word_width_bits);
  if (values.size() > limits_.maximum_operations ||
      operations_ > limits_.maximum_operations - values.size())
    fail_map(kMapLimit, "UVM register-memory burst operation limit exceeded");
  const auto &state = memory_values_.at(memory_snapshot.identity);
  std::size_t new_words{};
  for (std::size_t word = 0; word < values.size(); ++word) {
    const bool enabled =
        byte_enables.empty() ||
        std::ranges::any_of(byte_enables[word],
                            [](const auto value) { return value != 0; });
    new_words += enabled && !state.mirrored.contains(first_word + word);
  }
  const auto new_bits =
      checked_multiply(new_words, memory_snapshot.word_width_bits,
                       "UVM register-memory burst storage overflow");
  if (new_words > limits_.maximum_materialized_memory_words -
                      state.mirrored.size() ||
      new_bits > limits_.maximum_materialized_memory_bits -
                     materialized_memory_bits_)
    fail_map(kMapLimit, "UVM register-memory burst storage limit exceeded");

  SystemVerilogUvmRegisterMemoryBurstResult result;
  result.values.reserve(values.size());
  const auto backup =
      backup_memory_words(memory_snapshot, first_word, values.size());
  const auto operations_before = operations_;
  const auto mutations_before = mutations_;
  const auto materialized_bits_before = materialized_memory_bits_;
  try {
    for (std::size_t word = 0; word < values.size(); ++word) {
      const auto indices =
          unflatten_memory_index(memory_snapshot, first_word + word);
      auto operation = byte_enables.empty()
                           ? write(memory_handle, indices, values[word])
                           : write(memory_handle, indices, values[word],
                                   byte_enables[word]);
      result.values.push_back(operation.value);
      result.changed = result.changed || operation.changed;
      ++result.completed_words;
    }
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    restore_memory_words(memory_snapshot, backup, operations_before,
                         mutations_before, materialized_bits_before);
    if (error.diagnostic_code() == "FSIM-UVM-REG-004")
      fail_map(kMapLimit, "UVM register-memory burst operation limit exceeded");
    throw;
  } catch (...) {
    restore_memory_words(memory_snapshot, backup, operations_before,
                         mutations_before, materialized_bits_before);
    throw;
  }
  return result;
}

void SystemVerilogUvmRegisterModelService::validate_map_layouts(
    const SystemVerilogUvmRegisterBlockHandle top) const {
  std::set<std::uint64_t> subtree;
  std::vector<SystemVerilogUvmRegisterBlockHandle> blocks{top};
  while (!blocks.empty()) {
    const auto current = blocks.back();
    blocks.pop_back();
    const auto identity = block(current).snapshot.identity;
    subtree.insert(identity);
    if (const auto children = child_blocks_.find(identity);
        children != child_blocks_.end())
      blocks.insert(blocks.end(), children->second.begin(),
                    children->second.end());
  }

  std::map<std::uint64_t, std::uint64_t> sizes;
  std::function<std::uint64_t(SystemVerilogUvmRegisterMapHandle)> map_size;
  map_size = [&](const SystemVerilogUvmRegisterMapHandle handle) {
    const auto snapshot = map(handle).snapshot;
    if (const auto found = sizes.find(snapshot.identity); found != sizes.end())
      return found->second;
    const auto aub = address_unit_bytes(handle);
    if (snapshot.bus_width_bytes % aub != 0)
      fail_map(kInvalidMap, "UVM register-map bus is not address-unit aligned");
    const auto step = snapshot.bus_width_bytes / aub;
    std::vector<AddressRange> ranges;
    const auto add_range =
        [&](const AddressRange range) { ranges.push_back(range); };
    const auto &state = map_states_.at(snapshot.identity);
    for (const auto &entry : state.registers) {
      if (entry.unmapped)
        continue;
      const auto bytes = bytes_for_bits(reg(entry.register_handle).snapshot.width_bits);
      const auto beats = bytes / snapshot.bus_width_bytes +
                         static_cast<std::uint64_t>(
                             (bytes % snapshot.bus_width_bytes) != 0U);
      const auto span = snapshot.endianness ==
                                    SystemVerilogUvmRegisterMapEndianness::LittleFifo ||
                                snapshot.endianness ==
                                    SystemVerilogUvmRegisterMapEndianness::BigFifo
                            ? 0
                            : checked_multiply(beats - 1U, step,
                                               "UVM register address overflow");
      add_range({entry.offset,
                 checked_add(entry.offset, span,
                             "UVM register address overflow")});
    }
    for (const auto &entry : state.memories) {
      if (entry.unmapped)
        continue;
      const auto memory_snapshot = memory(entry.memory).snapshot;
      const auto bytes = bytes_for_bits(memory_snapshot.word_width_bits);
      if (bytes >= aub && bytes % aub != 0)
        fail_map(kInvalidMap, "UVM register-memory word is not aligned");
      const auto word_stride = bytes >= aub ? bytes / aub : 1U;
      const auto beats = bytes / snapshot.bus_width_bytes +
                         static_cast<std::uint64_t>(
                             (bytes % snapshot.bus_width_bytes) != 0U);
      const auto beat_span = snapshot.endianness ==
                                         SystemVerilogUvmRegisterMapEndianness::LittleFifo ||
                                     snapshot.endianness ==
                                         SystemVerilogUvmRegisterMapEndianness::BigFifo
                                 ? 0
                                 : checked_multiply(beats - 1U, step,
                                                    "UVM memory address overflow");
      const auto words_span = checked_multiply(memory_snapshot.word_count - 1U,
                                               word_stride,
                                               "UVM memory address overflow");
      add_range({entry.offset,
                 checked_add(entry.offset,
                             checked_add(words_span, beat_span,
                                         "UVM memory address overflow"),
                             "UVM memory address overflow")});
    }
    for (const auto &relation : state.children) {
      const auto child_size = map_size(relation.child);
      add_range({relation.offset,
                 checked_add(relation.offset, child_size - 1U,
                             "UVM register submap address overflow")});
    }
    const auto size = checked_layout_size(std::move(ranges));
    sizes.emplace(snapshot.identity, size);
    return size;
  };

  for (const auto &[identity, entry] : maps_) {
    if (!subtree.contains(block(entry.snapshot.block).snapshot.identity))
      continue;
    (void)map_depth(entry.snapshot.handle);
    (void)map_size(entry.snapshot.handle);
    (void)identity;
  }
}

} // namespace fsim::runtime
