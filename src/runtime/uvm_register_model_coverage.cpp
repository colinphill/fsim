// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidCallback{"FSIM-UVM-REG-013"};
constexpr std::string_view kCallbackLimit{"FSIM-UVM-REG-014"};

[[noreturn]] void fail_callback(const std::string_view code,
                                const std::string_view message) {
  throw SystemVerilogUvmRegisterModelError{std::string{code},
                                           std::string{message}};
}

[[nodiscard]] std::uint32_t
phase_mask(const SystemVerilogUvmRegisterCallbackPhase phase) {
  return UINT32_C(1) << static_cast<std::uint8_t>(phase);
}

[[nodiscard]] bool in_block(const std::string_view owner,
                            const std::string_view selected) {
  return selected == owner ||
         (selected.size() > owner.size() && selected.starts_with(owner) &&
          selected[owner.size()] == '.');
}

[[nodiscard]] bool read_allowed(const SystemVerilogUvmRegisterMapRights rights) {
  return rights != SystemVerilogUvmRegisterMapRights::WriteOnly;
}

[[nodiscard]] bool
write_allowed(const SystemVerilogUvmRegisterMapRights rights) {
  return rights != SystemVerilogUvmRegisterMapRights::ReadOnly;
}

} // namespace

SystemVerilogUvmRegisterCallbackHandle
SystemVerilogUvmRegisterModelService::register_callback(
    SystemVerilogUvmRegisterCallbackDescriptor descriptor) {
  const auto selected = static_cast<unsigned>(descriptor.block.has_value()) +
                        static_cast<unsigned>(descriptor.map.has_value()) +
                        static_cast<unsigned>(descriptor.reg.has_value()) +
                        static_cast<unsigned>(descriptor.field.has_value()) +
                        static_cast<unsigned>(descriptor.memory.has_value());
  const auto scope_matches =
      (descriptor.scope == SystemVerilogUvmRegisterCallbackScope::Block &&
       descriptor.block) ||
      (descriptor.scope == SystemVerilogUvmRegisterCallbackScope::Map &&
       descriptor.map) ||
      (descriptor.scope == SystemVerilogUvmRegisterCallbackScope::Register &&
       descriptor.reg) ||
      (descriptor.scope == SystemVerilogUvmRegisterCallbackScope::Field &&
       descriptor.field) ||
      (descriptor.scope == SystemVerilogUvmRegisterCallbackScope::Memory &&
       descriptor.memory);
  if (selected != 1 || !scope_matches || !descriptor.callback ||
      descriptor.name.empty() ||
      descriptor.name.find('\0') != std::string::npos ||
      descriptor.phases == 0 || (descriptor.phases & ~UINT32_C(0x0f)) != 0) {
    fail_callback(kInvalidCallback,
                  "invalid UVM register callback descriptor");
  }
  if (descriptor.name.size() > limits_.maximum_name_bytes ||
      register_callbacks_.size() == limits_.maximum_register_callbacks ||
      next_register_callback_slot_ ==
          std::numeric_limits<std::uint64_t>::max() ||
      next_register_callback_order_ ==
          std::numeric_limits<std::uint64_t>::max()) {
    fail_callback(kCallbackLimit,
                  "UVM register callback registration limit exceeded");
  }
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmRegisterBlockHandle owner;
  if (descriptor.block) {
    const auto selected_block = snapshot(*descriptor.block);
    root = selected_block.root;
    owner = selected_block.handle;
  } else if (descriptor.map) {
    const auto selected_map = snapshot(*descriptor.map);
    root = selected_map.root;
    owner = selected_map.block;
  } else if (descriptor.reg) {
    const auto selected_register = snapshot(*descriptor.reg);
    root = selected_register.root;
    owner = selected_register.block;
  } else if (descriptor.field) {
    const auto selected_field = snapshot(*descriptor.field);
    root = selected_field.root;
    owner = selected_field.block;
  } else {
    const auto selected_memory = snapshot(*descriptor.memory);
    root = selected_memory.root;
    owner = selected_memory.block;
  }
  (void)root;
  require_locked(owner);
  const auto slot = next_register_callback_slot_++;
  const SystemVerilogUvmRegisterCallbackHandle handle{owner_, slot, 1};
  SystemVerilogUvmRegisterCallbackSnapshot retained{
      handle,
      descriptor.scope,
      descriptor.block,
      descriptor.map,
      descriptor.reg,
      descriptor.field,
      descriptor.memory,
      std::move(descriptor.name),
      descriptor.phases,
      descriptor.priority,
      next_register_callback_order_++,
      0,
      0};
  register_callbacks_.emplace(
      slot, RegisterCallbackEntry{std::move(retained),
                                  std::move(descriptor.callback), 1});
  record_mutation();
  const auto &registered = register_callbacks_.at(slot).snapshot;
  publish_activity(SystemVerilogUvmActivityAction::Created, registered.name,
                   root, registered.registration_order, "callback");
  return handle;
}

SystemVerilogUvmRegisterCoverageHandle
SystemVerilogUvmRegisterModelService::register_coverage_model(
    SystemVerilogUvmRegisterCoverageDescriptor descriptor) {
  const auto selected = snapshot(descriptor.block);
  require_locked(descriptor.block);
  if (descriptor.name.empty() || descriptor.name.find('\0') != std::string::npos ||
      (!descriptor.per_map && !descriptor.per_field &&
       !descriptor.reset_desired_mirror_cross)) {
    fail_callback(kInvalidCallback,
                  "invalid UVM register coverage descriptor");
  }
  if (descriptor.name.size() > limits_.maximum_name_bytes ||
      coverage_models_.size() == limits_.maximum_register_coverage_models ||
      next_register_coverage_slot_ ==
          std::numeric_limits<std::uint64_t>::max() ||
      next_register_callback_order_ ==
          std::numeric_limits<std::uint64_t>::max()) {
    fail_callback(kCallbackLimit,
                  "UVM register coverage registration limit exceeded");
  }
  (void)selected;
  const auto slot = next_register_coverage_slot_++;
  const SystemVerilogUvmRegisterCoverageHandle handle{owner_, slot, 1};
  coverage_models_.emplace(
      slot,
      RegisterCoverageEntry{
          {handle, descriptor.block, std::move(descriptor.name),
           descriptor.per_map, descriptor.per_field,
           descriptor.reset_desired_mirror_cross,
           next_register_callback_order_++, 0, {}},
          1});
  record_mutation();
  const auto &registered = coverage_models_.at(slot).snapshot;
  publish_activity(SystemVerilogUvmActivityAction::Created,
                   selected.full_name + ":" + registered.name,
                   selected.root, registered.registration_order, "coverage");
  return handle;
}

SystemVerilogUvmRegisterModelService::RegisterCallbackEntry &
SystemVerilogUvmRegisterModelService::register_callback_entry(
    const SystemVerilogUvmRegisterCallbackHandle handle) {
  return const_cast<RegisterCallbackEntry &>(
      std::as_const(*this).register_callback_entry(handle));
}

const SystemVerilogUvmRegisterModelService::RegisterCallbackEntry &
SystemVerilogUvmRegisterModelService::register_callback_entry(
    const SystemVerilogUvmRegisterCallbackHandle handle) const {
  if (!handle.valid() || handle.owner_.get() != owner_.get()) {
    fail_callback(kInvalidCallback,
                  "invalid or foreign UVM register callback handle");
  }
  const auto found = register_callbacks_.find(handle.slot_);
  if (found == register_callbacks_.end() ||
      found->second.generation != handle.generation_) {
    fail_callback(kInvalidCallback, "stale UVM register callback handle");
  }
  return found->second;
}

SystemVerilogUvmRegisterModelService::RegisterCoverageEntry &
SystemVerilogUvmRegisterModelService::coverage_entry(
    const SystemVerilogUvmRegisterCoverageHandle handle) {
  return const_cast<RegisterCoverageEntry &>(
      std::as_const(*this).coverage_entry(handle));
}

const SystemVerilogUvmRegisterModelService::RegisterCoverageEntry &
SystemVerilogUvmRegisterModelService::coverage_entry(
    const SystemVerilogUvmRegisterCoverageHandle handle) const {
  if (!handle.valid() || handle.owner_.get() != owner_.get()) {
    fail_callback(kInvalidCallback,
                  "invalid or foreign UVM register coverage handle");
  }
  const auto found = coverage_models_.find(handle.slot_);
  if (found == coverage_models_.end() ||
      found->second.generation != handle.generation_) {
    fail_callback(kInvalidCallback, "stale UVM register coverage handle");
  }
  return found->second;
}

SystemVerilogUvmRegisterBlockHandle
SystemVerilogUvmRegisterModelService::target_block(
    const SystemVerilogUvmRegisterTarget &target) const {
  if (target.register_handle) {
    return snapshot(*target.register_handle).block;
  }
  if (target.field) {
    return snapshot(*target.field).block;
  }
  return snapshot(*target.memory).block;
}

bool SystemVerilogUvmRegisterModelService::callback_matches(
    const SystemVerilogUvmRegisterCallbackSnapshot &callback,
    const SystemVerilogUvmRegisterCallbackContext &context) const {
  if ((callback.phases & phase_mask(context.phase)) == 0) {
    return false;
  }
  switch (callback.scope) {
  case SystemVerilogUvmRegisterCallbackScope::Block: {
    const auto owner = snapshot(*callback.block);
    const auto selected = snapshot(target_block(context.target));
    return owner.root == selected.root &&
           in_block(owner.full_name, selected.full_name);
  }
  case SystemVerilogUvmRegisterCallbackScope::Map:
    return context.map && *context.map == *callback.map;
  case SystemVerilogUvmRegisterCallbackScope::Register:
    if (context.target.register_handle) {
      return *context.target.register_handle == *callback.reg;
    }
    return context.target.field &&
           snapshot(*context.target.field).register_handle == *callback.reg;
  case SystemVerilogUvmRegisterCallbackScope::Memory:
    return context.target.memory && *context.target.memory == *callback.memory;
  case SystemVerilogUvmRegisterCallbackScope::Field:
    if (context.target.field) {
      return *context.target.field == *callback.field;
    }
    return context.target.register_handle &&
           snapshot(*callback.field).register_handle ==
               *context.target.register_handle;
  }
  return false;
}

bool SystemVerilogUvmRegisterModelService::dispatch_register_callbacks(
    SystemVerilogUvmRegisterCallbackContext &context, const bool reverse) {
  auto selected = matching_register_callbacks(context);
  if (reverse) {
    std::ranges::reverse(selected);
  }
  for (auto *entry : selected) {
    if (!invoke_register_callback(*entry, context)) {
      return false;
    }
  }
  return true;
}

std::vector<SystemVerilogUvmRegisterModelService::RegisterCallbackEntry *>
SystemVerilogUvmRegisterModelService::matching_register_callbacks(
    const SystemVerilogUvmRegisterCallbackContext &context) {
  std::vector<RegisterCallbackEntry *> selected;
  for (auto &[slot, entry] : register_callbacks_) {
    (void)slot;
    if (callback_matches(entry.snapshot, context)) {
      if (selected.size() == limits_.maximum_register_callbacks_per_access) {
        fail_callback(kCallbackLimit,
                      "UVM register callback per-access limit exceeded");
      }
      selected.push_back(&entry);
    }
  }
  std::ranges::sort(selected, [](const auto *left, const auto *right) {
    if (left->snapshot.scope != right->snapshot.scope) {
      return left->snapshot.scope < right->snapshot.scope;
    }
    if (left->snapshot.priority != right->snapshot.priority) {
      return left->snapshot.priority > right->snapshot.priority;
    }
    return left->snapshot.registration_order <
           right->snapshot.registration_order;
  });
  return selected;
}

bool SystemVerilogUvmRegisterModelService::invoke_register_callback(
    RegisterCallbackEntry &entry,
    SystemVerilogUvmRegisterCallbackContext &context) {
  if (register_callback_invocations_ ==
      limits_.maximum_register_callback_invocations) {
    fail_callback(kCallbackLimit,
                  "UVM register callback invocation limit exceeded");
  }
  ++register_callback_invocations_;
  ++entry.snapshot.invocations;
  context.scope = entry.snapshot.scope;
  context.block = entry.snapshot.block;
  context.reg = entry.snapshot.reg;
  context.field = entry.snapshot.field;
  context.memory = entry.snapshot.memory;
  const auto original_target = context.target;
  const auto original_map = context.map;
  const auto original_phase = context.phase;
  try {
    entry.callback(context);
    const auto shape = target_shape(context.target);
    if (context.target != original_target || context.map != original_map ||
        context.phase != original_phase || context.value.width() != shape.second) {
      throw std::runtime_error{"callback produced an invalid access mutation"};
    }
  } catch (const std::exception &error) {
    record_register_callback_failure(entry, context, error.what());
    return false;
  } catch (...) {
    record_register_callback_failure(
        entry, context, "register callback threw nonstandard exception");
    return false;
  }
  if (!context.proceed) {
    context.status = SystemVerilogUvmRegisterOperationStatus::NotOk;
    if (context.message.empty()) {
      context.message = "register callback stopped the access";
    }
    return false;
  }
  return context.status != SystemVerilogUvmRegisterOperationStatus::NotOk;
}

void SystemVerilogUvmRegisterModelService::record_register_callback_failure(
    RegisterCallbackEntry &entry,
    SystemVerilogUvmRegisterCallbackContext &context, std::string message) {
  if (register_callback_failures_ ==
      limits_.maximum_register_callback_failures) {
    fail_callback(kCallbackLimit,
                  "UVM register callback failure limit exceeded");
  }
  ++register_callback_failures_;
  ++entry.snapshot.failures;
  context.status = SystemVerilogUvmRegisterOperationStatus::NotOk;
  context.message = std::move(message);
  context.proceed = false;
}

void SystemVerilogUvmRegisterModelService::validate_callback_map(
    const SystemVerilogUvmRegisterTarget &target,
    const SystemVerilogUvmRegisterMapHandle selected_map,
    const bool read) const {
  const auto selected = snapshot(selected_map);
  const auto shape = target_shape(target);
  if (selected.root != shape.first) {
    fail_callback(kInvalidCallback,
                  "register callback map and target roots differ");
  }
  if (target.memory) {
    const auto entries = mapped_memories(selected_map, true);
    const auto found = std::ranges::find(entries, *target.memory,
                                         &SystemVerilogUvmRegisterMapMemorySnapshot::memory);
    if (found == entries.end() || found->unmapped ||
        (read ? !read_allowed(found->rights)
              : !write_allowed(found->rights))) {
      fail_callback(kInvalidCallback,
                    "register callback memory map rights reject access");
    }
    return;
  }
  const auto selected_register = target.register_handle
                                     ? *target.register_handle
                                     : snapshot(*target.field).register_handle;
  const auto entries = mapped_registers(selected_map, true);
  const auto found = std::ranges::find(
      entries, selected_register,
      &SystemVerilogUvmRegisterMapRegisterSnapshot::register_handle);
  if (found == entries.end() || found->unmapped ||
      (read ? !read_allowed(found->rights) : !write_allowed(found->rights))) {
    fail_callback(kInvalidCallback,
                  "register callback register map rights reject access");
  }
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::direct_target_read(
    const SystemVerilogUvmRegisterTarget &target) {
  if (target.register_handle) {
    return read(*target.register_handle);
  }
  if (target.field) {
    return read(*target.field);
  }
  const auto selected = snapshot(*target.memory);
  const auto indices = unflatten_memory_index(selected, target.memory_word_index);
  return read(*target.memory, indices);
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::direct_target_write(
    const SystemVerilogUvmRegisterTarget &target, PackedLogic4 value) {
  if (target.register_handle) {
    return write(*target.register_handle, std::move(value));
  }
  if (target.field) {
    return write(*target.field, std::move(value));
  }
  const auto selected = snapshot(*target.memory);
  const auto indices = unflatten_memory_index(selected, target.memory_word_index);
  return write(*target.memory, indices, std::move(value));
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::callback_read(
    SystemVerilogUvmRegisterTarget target,
    const std::optional<SystemVerilogUvmRegisterMapHandle> selected_map) {
  std::pair<SystemVerilogUvmRootHandle, std::size_t> shape;
  try {
    shape = target_shape(target);
  } catch (const SystemVerilogUvmRegisterModelError &) {
    fail_callback(kInvalidCallback, "invalid UVM register callback target");
  }
  require_locked(target_block(target));
  const auto selected_block = snapshot(target_block(target));
  if (selected_map) {
    validate_callback_map(target, *selected_map, true);
  }
  SystemVerilogUvmRegisterCallbackContext context{
      SystemVerilogUvmRegisterCallbackPhase::PreRead,
      target,
      selected_map,
      SystemVerilogUvmRegisterCallbackScope::Block,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      PackedLogic4{shape.second, Logic4::zero},
      SystemVerilogUvmRegisterOperationStatus::IsOk,
      {},
      true};
  if (!dispatch_register_callbacks(context, false)) {
    record_mutation();
    publish_activity(SystemVerilogUvmActivityAction::Failed,
                     selected_block.full_name, selected_block.root, 0,
                     "callback-read");
    return {context.status, std::move(context.value), false,
            std::move(context.message)};
  }
  auto result = direct_target_read(target);
  context.phase = SystemVerilogUvmRegisterCallbackPhase::PostRead;
  context.value = result.value;
  context.status = result.status;
  context.message = result.message;
  context.proceed = result.success();
  (void)dispatch_register_callbacks(context, true);
  if (context.status == SystemVerilogUvmRegisterOperationStatus::IsOk) {
    if (context.value != result.value) {
      (void)predict_target(target, context.value,
                           SystemVerilogUvmRegisterPredictKind::Direct);
    }
    sample_register_coverage(target, selected_map, true);
  }
  record_mutation();
  publish_activity(context.status == SystemVerilogUvmRegisterOperationStatus::IsOk
                       ? SystemVerilogUvmActivityAction::Completed
                       : SystemVerilogUvmActivityAction::Failed,
                   selected_block.full_name, selected_block.root, 0,
                   "callback-read");
  return {context.status, std::move(context.value), result.changed,
          std::move(context.message)};
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::callback_write(
    SystemVerilogUvmRegisterTarget target, PackedLogic4 value,
    const std::optional<SystemVerilogUvmRegisterMapHandle> selected_map) {
  std::pair<SystemVerilogUvmRootHandle, std::size_t> shape;
  try {
    shape = target_shape(target);
  } catch (const SystemVerilogUvmRegisterModelError &) {
    fail_callback(kInvalidCallback, "invalid UVM register callback target");
  }
  if (value.width() != shape.second) {
    fail_callback(kInvalidCallback,
                  "invalid UVM register callback write width");
  }
  require_locked(target_block(target));
  const auto selected_block = snapshot(target_block(target));
  if (selected_map) {
    validate_callback_map(target, *selected_map, false);
  }
  SystemVerilogUvmRegisterCallbackContext context{
      SystemVerilogUvmRegisterCallbackPhase::PreWrite,
      target,
      selected_map,
      SystemVerilogUvmRegisterCallbackScope::Block,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::move(value),
      SystemVerilogUvmRegisterOperationStatus::IsOk,
      {},
      true};
  if (!dispatch_register_callbacks(context, false)) {
    record_mutation();
    publish_activity(SystemVerilogUvmActivityAction::Failed,
                     selected_block.full_name, selected_block.root, 0,
                     "callback-write");
    return {context.status, std::move(context.value), false,
            std::move(context.message)};
  }
  auto result = direct_target_write(target, context.value);
  context.phase = SystemVerilogUvmRegisterCallbackPhase::PostWrite;
  context.value = result.value.width() == shape.second ? result.value : context.value;
  context.status = result.status;
  context.message = result.message;
  context.proceed = result.success();
  (void)dispatch_register_callbacks(context, true);
  if (context.status == SystemVerilogUvmRegisterOperationStatus::IsOk) {
    sample_register_coverage(target, selected_map, false);
  }
  record_mutation();
  publish_activity(context.status == SystemVerilogUvmRegisterOperationStatus::IsOk
                       ? SystemVerilogUvmActivityAction::Completed
                       : SystemVerilogUvmActivityAction::Failed,
                   selected_block.full_name, selected_block.root, 0,
                   "callback-write");
  return {context.status, std::move(context.value), result.changed,
          std::move(context.message)};
}

void SystemVerilogUvmRegisterModelService::sample_coverage_bin(
    RegisterCoverageEntry &coverage, std::string key) {
  const auto found = coverage.snapshot.bins.find(key);
  if (found != coverage.snapshot.bins.end()) {
    if (found->second == std::numeric_limits<std::uint64_t>::max()) {
      fail_callback(kCallbackLimit, "UVM register coverage bin overflow");
    }
    ++found->second;
    return;
  }
  if (register_coverage_bins_ == limits_.maximum_register_coverage_bins ||
      key.size() > limits_.maximum_register_coverage_bin_bytes -
                       register_coverage_bin_bytes_) {
    fail_callback(kCallbackLimit,
                  "UVM register coverage bin resource limit exceeded");
  }
  register_coverage_bin_bytes_ += key.size();
  ++register_coverage_bins_;
  coverage.snapshot.bins.emplace(std::move(key), 1);
}

void SystemVerilogUvmRegisterModelService::sample_register_coverage(
    const SystemVerilogUvmRegisterTarget &target,
    const std::optional<SystemVerilogUvmRegisterMapHandle> selected_map,
    const bool read_access) {
  const auto selected_block = snapshot(target_block(target));
  for (auto &[slot, coverage] : coverage_models_) {
    (void)slot;
    const auto owner = snapshot(coverage.snapshot.block);
    if (owner.root != selected_block.root ||
        !in_block(owner.full_name, selected_block.full_name)) {
      continue;
    }
    sample_coverage_model(coverage, target, selected_map, read_access);
  }
}

void SystemVerilogUvmRegisterModelService::sample_coverage_model(
    RegisterCoverageEntry &coverage,
    const SystemVerilogUvmRegisterTarget &target,
    const std::optional<SystemVerilogUvmRegisterMapHandle> selected_map,
    const bool read_access) {
  if (register_coverage_samples_ ==
      limits_.maximum_register_coverage_samples) {
    fail_callback(kCallbackLimit,
                  "UVM register coverage sample limit exceeded");
  }
  ++register_coverage_samples_;
  ++coverage.snapshot.samples;
  if (coverage.snapshot.per_map && selected_map) {
    const auto selected = snapshot(*selected_map);
    sample_coverage_bin(coverage,
                        "map:" + std::to_string(selected.identity) +
                            (read_access ? ":read" : ":write"));
  }
  for (const auto &field : coverage_fields(target)) {
    sample_field_coverage(coverage, field);
  }
}

std::vector<SystemVerilogUvmRegisterFieldSnapshot>
SystemVerilogUvmRegisterModelService::coverage_fields(
    const SystemVerilogUvmRegisterTarget &target) const {
  std::vector<SystemVerilogUvmRegisterFieldSnapshot> result;
  if (target.field) {
    result.push_back(snapshot(*target.field));
    return result;
  }
  if (!target.register_handle) {
    return result;
  }
  for (const auto &candidate : fields()) {
    if (candidate.register_handle == *target.register_handle) {
      result.push_back(candidate);
    }
  }
  return result;
}

void SystemVerilogUvmRegisterModelService::sample_field_coverage(
    RegisterCoverageEntry &coverage,
    const SystemVerilogUvmRegisterFieldSnapshot &field) {
  const auto state = value_snapshot(field.handle);
  if (coverage.snapshot.per_field) {
    sample_coverage_bin(
        coverage, "field:" + std::to_string(field.identity) + ":" +
                      state.mirrored.to_msb_string());
  }
  if (!coverage.snapshot.reset_desired_mirror_cross) {
    return;
  }
  for (const auto &[kind, reset_value] : state.reset_values) {
    sample_coverage_bin(
        coverage,
        "cross:" + std::to_string(field.identity) + ":" + kind + ":" +
            reset_value.to_msb_string() + ":" + state.desired.to_msb_string() +
            ":" + state.mirrored.to_msb_string());
  }
}

SystemVerilogUvmRegisterCallbackSnapshot
SystemVerilogUvmRegisterModelService::callback_snapshot(
    const SystemVerilogUvmRegisterCallbackHandle handle) const {
  return register_callback_entry(handle).snapshot;
}

SystemVerilogUvmRegisterCoverageSnapshot
SystemVerilogUvmRegisterModelService::coverage_snapshot(
    const SystemVerilogUvmRegisterCoverageHandle handle) const {
  return coverage_entry(handle).snapshot;
}

std::vector<SystemVerilogUvmRegisterCallbackSnapshot>
SystemVerilogUvmRegisterModelService::register_callbacks() const {
  std::vector<SystemVerilogUvmRegisterCallbackSnapshot> result;
  result.reserve(register_callbacks_.size());
  for (const auto &[slot, entry] : register_callbacks_) {
    (void)slot;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterCoverageSnapshot>
SystemVerilogUvmRegisterModelService::coverage_models() const {
  std::vector<SystemVerilogUvmRegisterCoverageSnapshot> result;
  result.reserve(coverage_models_.size());
  for (const auto &[slot, entry] : coverage_models_) {
    (void)slot;
    result.push_back(entry.snapshot);
  }
  return result;
}

} // namespace fsim::runtime
