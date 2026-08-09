// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <limits>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidBackdoor{"FSIM-UVM-REG-009"};
constexpr std::string_view kBackdoorLimit{"FSIM-UVM-REG-010"};

[[noreturn]] void fail_backdoor(const std::string_view code,
                                std::string message) {
  throw SystemVerilogUvmRegisterModelError{std::string{code},
                                           std::move(message)};
}

void insert_slice(PackedLogic4 &destination, const PackedLogic4 &source,
                  const std::size_t destination_lsb,
                  const std::size_t source_lsb, const std::size_t width) {
  for (std::size_t bit = 0; bit < width; ++bit)
    destination.set(destination_lsb + bit, source.get(source_lsb + bit));
}

} // namespace

void SystemVerilogUvmRegisterModelService::set_backdoor_transport(
    SystemVerilogUvmRegisterBackdoorTransport transport) noexcept {
  backdoor_transport_ = std::move(transport);
}

std::pair<SystemVerilogUvmRootHandle, std::size_t>
SystemVerilogUvmRegisterModelService::target_shape(
    const SystemVerilogUvmRegisterTarget &target) const {
  const auto count = static_cast<unsigned>(target.register_handle.has_value()) +
                     static_cast<unsigned>(target.field.has_value()) +
                     static_cast<unsigned>(target.memory.has_value());
  if (count != 1)
    fail_backdoor(kInvalidBackdoor,
                  "UVM register access target must select exactly one object");
  if (target.register_handle) {
    const auto selected = reg(*target.register_handle).snapshot;
    return {selected.root, selected.width_bits};
  }
  if (target.field) {
    const auto selected = field(*target.field).snapshot;
    return {selected.root, selected.width_bits};
  }
  const auto selected = memory(*target.memory).snapshot;
  if (target.memory_word_index >= selected.word_count)
    fail_backdoor(kInvalidBackdoor,
                  "UVM register-memory access word is out of range");
  return {selected.root, selected.word_width_bits};
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::predict_target(
    const SystemVerilogUvmRegisterTarget &target, PackedLogic4 value,
    const SystemVerilogUvmRegisterPredictKind kind) {
  if (target.register_handle)
    return predict(*target.register_handle, std::move(value), kind);
  if (target.field)
    return predict(*target.field, std::move(value), kind);
  const auto selected = memory(*target.memory).snapshot;
  const auto indices =
      unflatten_memory_index(selected, target.memory_word_index);
  return predict(*target.memory, indices, std::move(value), kind);
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterUserFrontdoorHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_ != owner_)
    return false;
  const auto found = user_frontdoors_.find(handle.slot_);
  return found != user_frontdoors_.end() &&
         found->second.generation == handle.generation_;
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterHdlPathHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_ != owner_)
    return false;
  const auto found = hdl_paths_.find(handle.slot_);
  return found != hdl_paths_.end() &&
         found->second.generation == handle.generation_;
}

SystemVerilogUvmRegisterModelService::UserFrontdoorEntry &
SystemVerilogUvmRegisterModelService::user_frontdoor(
    const SystemVerilogUvmRegisterUserFrontdoorHandle handle) {
  return const_cast<UserFrontdoorEntry &>(
      std::as_const(*this).user_frontdoor(handle));
}

const SystemVerilogUvmRegisterModelService::UserFrontdoorEntry &
SystemVerilogUvmRegisterModelService::user_frontdoor(
    const SystemVerilogUvmRegisterUserFrontdoorHandle handle) const {
  if (!contains(handle))
    fail_backdoor(kInvalidBackdoor,
                  "invalid or foreign UVM user-frontdoor handle");
  return user_frontdoors_.at(handle.slot_);
}

SystemVerilogUvmRegisterModelService::HdlPathEntry &
SystemVerilogUvmRegisterModelService::hdl_path(
    const SystemVerilogUvmRegisterHdlPathHandle handle) {
  return const_cast<HdlPathEntry &>(std::as_const(*this).hdl_path(handle));
}

const SystemVerilogUvmRegisterModelService::HdlPathEntry &
SystemVerilogUvmRegisterModelService::hdl_path(
    const SystemVerilogUvmRegisterHdlPathHandle handle) const {
  if (!contains(handle))
    fail_backdoor(kInvalidBackdoor,
                  "invalid or foreign UVM register HDL-path handle");
  return hdl_paths_.at(handle.slot_);
}

SystemVerilogUvmRegisterUserFrontdoorHandle
SystemVerilogUvmRegisterModelService::register_user_frontdoor(
    SystemVerilogUvmRegisterUserFrontdoorDescriptor descriptor) {
  validate_name(descriptor.name);
  const auto [root, width] = target_shape(descriptor.target);
  (void)width;
  if (descriptor.root != root || (!descriptor.read && !descriptor.write))
    fail_backdoor(kInvalidBackdoor,
                  "UVM user frontdoor ownership or callbacks are invalid");
  if (user_frontdoors_.size() >= limits_.maximum_user_frontdoors ||
      next_user_frontdoor_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_frontdoor_order_ == std::numeric_limits<std::uint64_t>::max())
    fail_backdoor(kBackdoorLimit,
                  "UVM user-frontdoor resource ceiling exceeded");
  record_mutation();
  const auto slot = next_user_frontdoor_slot_++;
  const auto handle =
      SystemVerilogUvmRegisterUserFrontdoorHandle{owner_, slot, 1};
  SystemVerilogUvmRegisterUserFrontdoorSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.root = descriptor.root;
  snapshot.name = std::move(descriptor.name);
  snapshot.target = descriptor.target;
  snapshot.registration_order = next_frontdoor_order_++;
  user_frontdoors_.emplace(
      slot, UserFrontdoorEntry{std::move(snapshot), std::move(descriptor.read),
                               std::move(descriptor.write), 1});
  return handle;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::user_frontdoor_read(
    const SystemVerilogUvmRegisterUserFrontdoorHandle handle,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  auto &entry = user_frontdoor(handle);
  if (!entry.read)
    fail_backdoor(kInvalidBackdoor,
                  "UVM user frontdoor does not support reads");
  if (backdoor_operations_ >= limits_.maximum_backdoor_operations)
    fail_backdoor(kBackdoorLimit,
                  "UVM user-frontdoor operation ceiling exceeded");
  ++backdoor_operations_;
  ++entry.snapshot.reads;
  try {
    auto result = entry.read(options);
    const auto [root, width] = target_shape(entry.snapshot.target);
    (void)root;
    if (!result.success() || result.value.width() != width) {
      ++entry.snapshot.failures;
      return result.success()
                 ? SystemVerilogUvmRegisterOperationResult{
                       SystemVerilogUvmRegisterOperationStatus::NotOk,
                       std::move(result.value), false,
                       "UVM user-frontdoor read width is invalid"}
                 : result;
    }
    return predict_target(entry.snapshot.target, std::move(result.value),
                          SystemVerilogUvmRegisterPredictKind::Read);
  } catch (const std::exception &error) {
    ++entry.snapshot.failures;
    return {SystemVerilogUvmRegisterOperationStatus::NotOk, PackedLogic4{0}, false,
            error.what()};
  } catch (...) {
    ++entry.snapshot.failures;
    return {SystemVerilogUvmRegisterOperationStatus::NotOk, PackedLogic4{0}, false,
            "UVM user-frontdoor read callback threw"};
  }
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::user_frontdoor_write(
    const SystemVerilogUvmRegisterUserFrontdoorHandle handle,
    PackedLogic4 value,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  auto &entry = user_frontdoor(handle);
  const auto [root, width] = target_shape(entry.snapshot.target);
  (void)root;
  if (!entry.write || value.width() != width)
    fail_backdoor(kInvalidBackdoor,
                  "UVM user-frontdoor write is unsupported or wrong width");
  if (backdoor_operations_ >= limits_.maximum_backdoor_operations)
    fail_backdoor(kBackdoorLimit,
                  "UVM user-frontdoor operation ceiling exceeded");
  ++backdoor_operations_;
  ++entry.snapshot.writes;
  try {
    auto result = entry.write(value, options);
    if (!result.success()) {
      ++entry.snapshot.failures;
      return result;
    }
    return predict_target(entry.snapshot.target, std::move(value),
                          SystemVerilogUvmRegisterPredictKind::Write);
  } catch (const std::exception &error) {
    ++entry.snapshot.failures;
    return {SystemVerilogUvmRegisterOperationStatus::NotOk, std::move(value),
            false, error.what()};
  } catch (...) {
    ++entry.snapshot.failures;
    return {SystemVerilogUvmRegisterOperationStatus::NotOk, std::move(value),
            false, "UVM user-frontdoor write callback threw"};
  }
}

SystemVerilogUvmRegisterHdlPathHandle
SystemVerilogUvmRegisterModelService::register_hdl_path(
    SystemVerilogUvmRegisterHdlPathDescriptor descriptor) {
  validate_name(descriptor.abstraction);
  const auto [root, width] = target_shape(descriptor.target);
  if (descriptor.root != root || descriptor.slices.empty() ||
      !backdoor_transport_.resolve || !backdoor_transport_.read ||
      !backdoor_transport_.write)
    fail_backdoor(kInvalidBackdoor,
                  "UVM register HDL path ownership or transport is invalid");
  const auto bytes = validate_hdl_slices(descriptor, width);
  if (hdl_paths_.size() >= limits_.maximum_hdl_paths ||
      descriptor.slices.size() > limits_.maximum_hdl_slices - hdl_slices_ ||
      bytes > limits_.maximum_hdl_path_bytes - hdl_path_bytes_ ||
      next_hdl_path_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_frontdoor_order_ == std::numeric_limits<std::uint64_t>::max())
    fail_backdoor(kBackdoorLimit,
                  "UVM register HDL-path resource ceiling exceeded");
  record_mutation();
  const auto slot = next_hdl_path_slot_++;
  const auto handle = SystemVerilogUvmRegisterHdlPathHandle{owner_, slot, 1};
  SystemVerilogUvmRegisterHdlPathSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.root = descriptor.root;
  snapshot.abstraction = std::move(descriptor.abstraction);
  snapshot.target = descriptor.target;
  snapshot.slices = std::move(descriptor.slices);
  snapshot.registration_order = next_frontdoor_order_++;
  hdl_slices_ += snapshot.slices.size();
  hdl_path_bytes_ += bytes;
  hdl_paths_.emplace(slot, HdlPathEntry{std::move(snapshot), 1});
  return handle;
}

std::size_t SystemVerilogUvmRegisterModelService::validate_hdl_slices(
    const SystemVerilogUvmRegisterHdlPathDescriptor &descriptor,
    const std::size_t width) const {
  std::size_t bytes{};
  std::vector<bool> covered(width);
  for (const auto &slice : descriptor.slices) {
    if (slice.path.empty() || slice.width == 0 ||
        slice.value_lsb > width || slice.width > width - slice.value_lsb)
      fail_backdoor(kInvalidBackdoor,
                    "UVM register HDL slice range is invalid");
    std::optional<std::size_t> resolved;
    try {
      resolved = backdoor_transport_.resolve(slice.kind, slice.path);
    } catch (const std::exception &error) {
      fail_backdoor(kInvalidBackdoor, error.what());
    }
    if (!resolved || slice.hdl_lsb > *resolved ||
        slice.width > *resolved - slice.hdl_lsb)
      fail_backdoor(kInvalidBackdoor,
                    "UVM register HDL slice does not resolve exactly");
    for (std::size_t bit = 0; bit < slice.width; ++bit) {
      const auto index = slice.value_lsb + bit;
      if (covered[index])
        fail_backdoor(kInvalidBackdoor,
                      "UVM register HDL slices overlap");
      covered[index] = true;
    }
    if (slice.path.size() > limits_.maximum_hdl_path_bytes - bytes)
      fail_backdoor(kBackdoorLimit,
                    "UVM register HDL path byte ceiling exceeded");
    bytes += slice.path.size();
  }
  if (std::ranges::find(covered, false) != covered.end())
    fail_backdoor(kInvalidBackdoor,
                  "UVM register HDL slices do not cover the target");
  return bytes;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::backdoor_read(
    const SystemVerilogUvmRegisterHdlPathHandle handle) {
  auto &entry = hdl_path(handle);
  const auto [root, width] = target_shape(entry.snapshot.target);
  (void)root;
  if (width > limits_.maximum_backdoor_value_bits ||
      backdoor_operations_ >= limits_.maximum_backdoor_operations)
    fail_backdoor(kBackdoorLimit,
                  "UVM register backdoor read ceiling exceeded");
  auto value = read_hdl_value(entry, width);
  ++backdoor_operations_;
  ++entry.snapshot.accesses;
  return predict_target(entry.snapshot.target, std::move(value),
                        SystemVerilogUvmRegisterPredictKind::Read);
}

PackedLogic4 SystemVerilogUvmRegisterModelService::read_hdl_value(
    const HdlPathEntry &entry, const std::size_t width) const {
  PackedLogic4 value(width, Logic4::zero);
  try {
    for (const auto &slice : entry.snapshot.slices) {
      const auto resolved = backdoor_transport_.resolve(slice.kind, slice.path);
      if (!resolved || slice.hdl_lsb > *resolved ||
          slice.width > *resolved - slice.hdl_lsb)
        fail_backdoor(kInvalidBackdoor,
                      "UVM register HDL path no longer resolves");
      const auto hdl_value = backdoor_transport_.read(slice.kind, slice.path);
      if (hdl_value.width() != *resolved)
        fail_backdoor(kInvalidBackdoor,
                      "UVM register HDL read width changed");
      insert_slice(value, hdl_value, slice.value_lsb, slice.hdl_lsb,
                   slice.width);
    }
  } catch (const SystemVerilogUvmRegisterModelError &) {
    throw;
  } catch (const std::exception &error) {
    fail_backdoor(kInvalidBackdoor, error.what());
  }
  return value;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::backdoor_write(
    const SystemVerilogUvmRegisterHdlPathHandle handle, PackedLogic4 value,
    const SystemVerilogUvmRegisterBackdoorKind kind) {
  auto &entry = hdl_path(handle);
  const auto [root, width] = target_shape(entry.snapshot.target);
  (void)root;
  if (kind == SystemVerilogUvmRegisterBackdoorKind::Read ||
      (kind != SystemVerilogUvmRegisterBackdoorKind::Release &&
       value.width() != width))
    fail_backdoor(kInvalidBackdoor,
                  "UVM register backdoor operation or value width is invalid");
  if (width > limits_.maximum_backdoor_value_bits ||
      backdoor_operations_ >= limits_.maximum_backdoor_operations)
    fail_backdoor(kBackdoorLimit,
                  "UVM register backdoor write ceiling exceeded");
  const auto resolved = resolve_hdl_write(entry, value, kind);
  apply_hdl_write(resolved, kind);
  ++backdoor_operations_;
  ++entry.snapshot.accesses;
  if (kind == SystemVerilogUvmRegisterBackdoorKind::Release) {
    auto released = read_hdl_value(entry, width);
    return predict_target(entry.snapshot.target, std::move(released),
                          SystemVerilogUvmRegisterPredictKind::Read);
  }
  return predict_target(entry.snapshot.target, std::move(value),
                        SystemVerilogUvmRegisterPredictKind::Direct);
}

std::vector<SystemVerilogUvmRegisterModelService::ResolvedHdlSlice>
SystemVerilogUvmRegisterModelService::resolve_hdl_write(
    const HdlPathEntry &entry, const PackedLogic4 &value,
    const SystemVerilogUvmRegisterBackdoorKind kind) const {
  std::vector<ResolvedHdlSlice> resolved;
  resolved.reserve(entry.snapshot.slices.size());
  try {
    for (const auto &slice : entry.snapshot.slices) {
      const auto resolved_width =
          backdoor_transport_.resolve(slice.kind, slice.path);
      if (!resolved_width || slice.hdl_lsb > *resolved_width ||
          slice.width > *resolved_width - slice.hdl_lsb)
        fail_backdoor(kInvalidBackdoor,
                      "UVM register HDL path no longer resolves");
      auto original = backdoor_transport_.read(slice.kind, slice.path);
      if (original.width() != *resolved_width)
        fail_backdoor(kInvalidBackdoor,
                      "UVM register HDL write width changed");
      auto updated = original;
      if (kind != SystemVerilogUvmRegisterBackdoorKind::Release)
        insert_slice(updated, value, slice.hdl_lsb, slice.value_lsb,
                     slice.width);
      resolved.push_back({&slice, std::move(original), std::move(updated)});
    }
  } catch (const SystemVerilogUvmRegisterModelError &) {
    throw;
  } catch (const std::exception &error) {
    fail_backdoor(kInvalidBackdoor, error.what());
  }
  return resolved;
}

void SystemVerilogUvmRegisterModelService::apply_hdl_write(
    const std::span<const ResolvedHdlSlice> slices,
    const SystemVerilogUvmRegisterBackdoorKind kind) {
  std::size_t applied{};
  try {
    for (const auto &slice : slices) {
      backdoor_transport_.write(slice.slice->kind, slice.slice->path, kind,
                                slice.updated);
      ++applied;
    }
  } catch (...) {
    rollback_hdl_write(slices, applied, kind);
    fail_backdoor(kInvalidBackdoor,
                  "UVM register HDL backdoor transport failed");
  }
}

void SystemVerilogUvmRegisterModelService::rollback_hdl_write(
    const std::span<const ResolvedHdlSlice> slices, std::size_t applied,
    const SystemVerilogUvmRegisterBackdoorKind kind) noexcept {
  while (applied != 0) {
    --applied;
    try {
      if (kind == SystemVerilogUvmRegisterBackdoorKind::Force)
        backdoor_transport_.write(
            slices[applied].slice->kind, slices[applied].slice->path,
            SystemVerilogUvmRegisterBackdoorKind::Release, PackedLogic4{0});
      backdoor_transport_.write(
          slices[applied].slice->kind, slices[applied].slice->path,
          SystemVerilogUvmRegisterBackdoorKind::Deposit,
          slices[applied].original);
    } catch (...) {
    }
  }
}

SystemVerilogUvmRegisterUserFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::user_frontdoor_snapshot(
    const SystemVerilogUvmRegisterUserFrontdoorHandle handle) const {
  return user_frontdoor(handle).snapshot;
}

SystemVerilogUvmRegisterHdlPathSnapshot
SystemVerilogUvmRegisterModelService::hdl_path_snapshot(
    const SystemVerilogUvmRegisterHdlPathHandle handle) const {
  return hdl_path(handle).snapshot;
}

std::vector<SystemVerilogUvmRegisterUserFrontdoorSnapshot>
SystemVerilogUvmRegisterModelService::user_frontdoors() const {
  std::vector<SystemVerilogUvmRegisterUserFrontdoorSnapshot> result;
  result.reserve(user_frontdoors_.size());
  for (const auto &[slot, entry] : user_frontdoors_) {
    (void)slot;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterHdlPathSnapshot>
SystemVerilogUvmRegisterModelService::hdl_paths() const {
  std::vector<SystemVerilogUvmRegisterHdlPathSnapshot> result;
  result.reserve(hdl_paths_.size());
  for (const auto &[slot, entry] : hdl_paths_) {
    (void)slot;
    result.push_back(entry.snapshot);
  }
  return result;
}

} // namespace fsim::runtime
