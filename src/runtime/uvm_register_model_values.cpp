// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <limits>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidOperation{"FSIM-UVM-REG-003"};
constexpr std::string_view kOperationLimit{"FSIM-UVM-REG-004"};

[[noreturn]] void fail_value(const std::string_view code,
                             std::string message) {
  throw SystemVerilogUvmRegisterModelError{std::string{code},
                                           std::move(message)};
}

[[nodiscard]] bool has_unknown(const PackedLogic4 &value) {
  if (value.is_logic9())
    return true;
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    const auto scalar = value.get(bit);
    if (scalar != Logic4::zero && scalar != Logic4::one)
      return true;
  }
  return false;
}

[[nodiscard]] PackedLogic4 extract(const PackedLogic4 &value,
                                   const std::size_t offset,
                                   const std::size_t width) {
  PackedLogic4 result{width, Logic4::zero};
  for (std::size_t bit = 0; bit < width; ++bit)
    result.set(bit, value.get(offset + bit));
  return result;
}

[[nodiscard]] PackedLogic4 insert(PackedLogic4 target,
                                  const PackedLogic4 &value,
                                  const std::size_t offset) {
  for (std::size_t bit = 0; bit < value.width(); ++bit)
    target.set(offset + bit, value.get(bit));
  return target;
}

[[nodiscard]] PackedLogic4 filled(const std::size_t width, const bool value) {
  return PackedLogic4{width, value ? Logic4::one : Logic4::zero};
}

[[nodiscard]] PackedLogic4 bit_not(const PackedLogic4 &value) {
  PackedLogic4 result{value.width(), Logic4::zero};
  for (std::size_t bit = 0; bit < value.width(); ++bit)
    result.set(bit, value.get(bit) == Logic4::one ? Logic4::zero : Logic4::one);
  return result;
}

enum class BitOperation : std::uint8_t { And, Or, Xor };

[[nodiscard]] PackedLogic4 bit_combine(const PackedLogic4 &left,
                                       const PackedLogic4 &right,
                                       const BitOperation operation) {
  PackedLogic4 result{left.width(), Logic4::zero};
  for (std::size_t bit = 0; bit < left.width(); ++bit) {
    const bool lhs = left.get(bit) == Logic4::one;
    const bool rhs = right.get(bit) == Logic4::one;
    bool output{};
    switch (operation) {
    case BitOperation::And:
      output = lhs && rhs;
      break;
    case BitOperation::Or:
      output = lhs || rhs;
      break;
    case BitOperation::Xor:
      output = lhs != rhs;
      break;
    }
    result.set(bit, output ? Logic4::one : Logic4::zero);
  }
  return result;
}

[[nodiscard]] bool valid_policy(const SystemVerilogUvmRegisterAccessPolicy policy) {
  return policy <= SystemVerilogUvmRegisterAccessPolicy::NoAccess;
}

[[nodiscard]] bool readable(const SystemVerilogUvmRegisterAccessPolicy policy) {
  using Policy = SystemVerilogUvmRegisterAccessPolicy;
  switch (policy) {
  case Policy::WriteOnly:
  case Policy::WriteOnce:
  case Policy::WriteOnlyClear:
  case Policy::WriteOnlySet:
  case Policy::NoAccess:
    return false;
  default:
    return valid_policy(policy);
  }
}

[[nodiscard]] bool writable(const SystemVerilogUvmRegisterAccessPolicy policy) {
  using Policy = SystemVerilogUvmRegisterAccessPolicy;
  switch (policy) {
  case Policy::ReadOnly:
  case Policy::ReadClear:
  case Policy::ReadSet:
  case Policy::NoAccess:
    return false;
  default:
    return valid_policy(policy);
  }
}

[[nodiscard]] bool clears_on_read(
    const SystemVerilogUvmRegisterAccessPolicy policy) {
  using Policy = SystemVerilogUvmRegisterAccessPolicy;
  return policy == Policy::ReadClear || policy == Policy::WriteReadClear ||
         policy == Policy::WriteSetReadClear ||
         policy == Policy::WriteOneSetReadClear ||
         policy == Policy::WriteZeroSetReadClear;
}

[[nodiscard]] bool sets_on_read(
    const SystemVerilogUvmRegisterAccessPolicy policy) {
  using Policy = SystemVerilogUvmRegisterAccessPolicy;
  return policy == Policy::ReadSet || policy == Policy::WriteReadSet ||
         policy == Policy::WriteClearReadSet ||
         policy == Policy::WriteOneClearReadSet ||
         policy == Policy::WriteZeroClearReadSet;
}

[[nodiscard]] bool write_once(
    const SystemVerilogUvmRegisterAccessPolicy policy) {
  using Policy = SystemVerilogUvmRegisterAccessPolicy;
  return policy == Policy::WriteOnce || policy == Policy::WriteOnceReadWrite;
}

[[nodiscard]] PackedLogic4 apply_read(
    const SystemVerilogUvmRegisterAccessPolicy policy,
    const PackedLogic4 &value) {
  if (clears_on_read(policy))
    return filled(value.width(), false);
  if (sets_on_read(policy))
    return filled(value.width(), true);
  return value;
}

[[nodiscard]] PackedLogic4 apply_write(
    const SystemVerilogUvmRegisterAccessPolicy policy,
    const PackedLogic4 &old_value, const PackedLogic4 &value,
    const bool was_written) {
  using Policy = SystemVerilogUvmRegisterAccessPolicy;
  if (write_once(policy) && was_written)
    return old_value;
  switch (policy) {
  case Policy::ReadWrite:
  case Policy::WriteOnly:
  case Policy::WriteReadClear:
  case Policy::WriteReadSet:
  case Policy::WriteOnce:
  case Policy::WriteOnceReadWrite:
    return value;
  case Policy::WriteClear:
  case Policy::WriteClearReadSet:
  case Policy::WriteOnlyClear:
    return filled(value.width(), false);
  case Policy::WriteSet:
  case Policy::WriteSetReadClear:
  case Policy::WriteOnlySet:
    return filled(value.width(), true);
  case Policy::WriteOneClear:
  case Policy::WriteOneClearReadSet:
    return bit_combine(old_value, bit_not(value), BitOperation::And);
  case Policy::WriteOneSet:
  case Policy::WriteOneSetReadClear:
    return bit_combine(old_value, value, BitOperation::Or);
  case Policy::WriteOneToggle:
    return bit_combine(old_value, value, BitOperation::Xor);
  case Policy::WriteZeroClear:
  case Policy::WriteZeroClearReadSet:
    return bit_combine(old_value, value, BitOperation::And);
  case Policy::WriteZeroSet:
  case Policy::WriteZeroSetReadClear:
    return bit_combine(old_value, bit_not(value), BitOperation::Or);
  case Policy::WriteZeroToggle:
    return bit_combine(old_value, bit_not(value), BitOperation::Xor);
  case Policy::ReadOnly:
  case Policy::ReadClear:
  case Policy::ReadSet:
  case Policy::NoAccess:
    return old_value;
  }
  return old_value;
}

[[nodiscard]] SystemVerilogUvmRegisterOperationResult rejected(
    const SystemVerilogUvmRegisterOperationStatus status,
    std::string message, const std::size_t width) {
  return {status, PackedLogic4{width, Logic4::zero}, false,
          std::move(message)};
}

} // namespace

void SystemVerilogUvmRegisterModelService::require_locked(
    const SystemVerilogUvmRegisterBlockHandle handle) const {
  const auto &snapshot = block(handle).snapshot;
  if (!components_->contains_root(snapshot.root) ||
      snapshot.state != SystemVerilogUvmRegisterModelState::Locked) {
    fail_value(kInvalidOperation,
               "UVM register value operation requires a live locked model");
  }
}

void SystemVerilogUvmRegisterModelService::record_operation(
    const bool mutation) {
  if (operations_ >= limits_.maximum_operations ||
      (mutation && mutations_ >= limits_.maximum_mutations)) {
    fail_value(kOperationLimit,
               "UVM register operation or mutation limit exceeded");
  }
  ++operations_;
  if (mutation)
    ++mutations_;
}

void SystemVerilogUvmRegisterModelService::configure_field(
    const SystemVerilogUvmRegisterFieldHandle handle,
    const SystemVerilogUvmRegisterAccessPolicy access,
    const bool is_volatile,
    const SystemVerilogUvmRegisterComparePolicy compare_policy) {
  auto &snapshot = field(handle).snapshot;
  require_building(snapshot.block);
  if (!valid_policy(access) ||
      compare_policy > SystemVerilogUvmRegisterComparePolicy::NoCheck) {
    fail_value(kInvalidOperation, "invalid UVM register field policy");
  }
  record_mutation();
  snapshot.access = access;
  snapshot.is_volatile = is_volatile;
  snapshot.compare = compare_policy;
}

void SystemVerilogUvmRegisterModelService::set_reset(
    const SystemVerilogUvmRegisterFieldHandle handle, std::string kind,
    PackedLogic4 value) {
  const auto &snapshot = field(handle).snapshot;
  require_building(snapshot.block);
  if (kind.size() > limits_.maximum_reset_name_bytes)
    fail_value(kOperationLimit, "UVM register reset-name limit exceeded");
  if (kind.empty() || kind.find('\0') != std::string::npos ||
      value.width() != snapshot.width_bits || has_unknown(value)) {
    fail_value(kInvalidOperation,
               "invalid UVM register reset kind, width, or value");
  }
  auto &resets = field_resets_[snapshot.identity].values;
  const bool new_kind = !resets.contains(kind);
  if (!resets.contains(kind) &&
      resets.size() >= limits_.maximum_reset_kinds_per_field) {
    fail_value(kOperationLimit, "UVM register reset-kind limit exceeded");
  }
  if (new_kind && snapshot.width_bits >
                      limits_.maximum_reset_value_bits - reset_value_bits_) {
    fail_value(kOperationLimit,
               "UVM aggregate reset-value bit limit exceeded");
  }
  record_mutation();
  resets.insert_or_assign(std::move(kind), std::move(value));
  if (new_kind)
    reset_value_bits_ += snapshot.width_bits;
}

void SystemVerilogUvmRegisterModelService::set_memory_access(
    const SystemVerilogUvmRegisterMemoryHandle handle,
    const SystemVerilogUvmRegisterAccessPolicy access) {
  auto &snapshot = memory(handle).snapshot;
  require_building(snapshot.block);
  if (!valid_policy(access))
    fail_value(kInvalidOperation, "invalid UVM memory access policy");
  record_mutation();
  snapshot.access = access;
}

void SystemVerilogUvmRegisterModelService::set(
    const SystemVerilogUvmRegisterFieldHandle handle, PackedLogic4 value) {
  const auto &snapshot = field(handle).snapshot;
  require_locked(snapshot.block);
  if (value.width() != snapshot.width_bits || has_unknown(value))
    fail_value(kInvalidOperation, "invalid UVM register field set value");
  auto candidate = register_values_.at(reg(snapshot.register_handle).snapshot.identity);
  const auto old_field = extract(candidate.desired,
                                 snapshot.least_significant_bit,
                                 snapshot.width_bits);
  const auto desired = apply_write(
      snapshot.access, old_field, value,
      candidate.written_fields[snapshot.identity]);
  candidate.desired = insert(std::move(candidate.desired), desired,
                             snapshot.least_significant_bit);
  const bool changed = candidate.desired !=
                       register_values_.at(
                           reg(snapshot.register_handle).snapshot.identity)
                           .desired;
  record_operation(changed);
  register_values_.at(reg(snapshot.register_handle).snapshot.identity) =
      std::move(candidate);
}

void SystemVerilogUvmRegisterModelService::set(
    const SystemVerilogUvmRegisterHandle handle, PackedLogic4 value) {
  const auto &snapshot = reg(handle).snapshot;
  require_locked(snapshot.block);
  if (value.width() != snapshot.width_bits || has_unknown(value))
    fail_value(kInvalidOperation, "invalid UVM register set value");
  auto candidate = register_values_.at(snapshot.identity);
  PackedLogic4 desired = value;
  for (const auto &field_handle : register_fields_.at(snapshot.identity)) {
    const auto &field_snapshot = field(field_handle).snapshot;
    const auto old_field = extract(
        candidate.desired, field_snapshot.least_significant_bit,
        field_snapshot.width_bits);
    const auto incoming = extract(value, field_snapshot.least_significant_bit,
                                  field_snapshot.width_bits);
    desired = insert(
        std::move(desired),
        apply_write(field_snapshot.access, old_field, incoming,
                    candidate.written_fields[field_snapshot.identity]),
        field_snapshot.least_significant_bit);
  }
  const bool changed = candidate.desired != desired;
  record_operation(changed);
  register_values_.at(snapshot.identity).desired = std::move(desired);
}

PackedLogic4 SystemVerilogUvmRegisterModelService::get(
    const SystemVerilogUvmRegisterFieldHandle handle) const {
  const auto &snapshot = field(handle).snapshot;
  require_locked(snapshot.block);
  const auto &state =
      register_values_.at(reg(snapshot.register_handle).snapshot.identity);
  return extract(state.desired, snapshot.least_significant_bit,
                 snapshot.width_bits);
}

PackedLogic4 SystemVerilogUvmRegisterModelService::get(
    const SystemVerilogUvmRegisterHandle handle) const {
  const auto &snapshot = reg(handle).snapshot;
  require_locked(snapshot.block);
  return register_values_.at(snapshot.identity).desired;
}

PackedLogic4 SystemVerilogUvmRegisterModelService::get_mirrored(
    const SystemVerilogUvmRegisterFieldHandle handle) const {
  const auto &snapshot = field(handle).snapshot;
  require_locked(snapshot.block);
  const auto &state =
      register_values_.at(reg(snapshot.register_handle).snapshot.identity);
  return extract(state.mirrored, snapshot.least_significant_bit,
                 snapshot.width_bits);
}

PackedLogic4 SystemVerilogUvmRegisterModelService::get_mirrored(
    const SystemVerilogUvmRegisterHandle handle) const {
  const auto &snapshot = reg(handle).snapshot;
  require_locked(snapshot.block);
  return register_values_.at(snapshot.identity).mirrored;
}

SystemVerilogUvmRegisterFieldValueSnapshot
SystemVerilogUvmRegisterModelService::value_snapshot(
    const SystemVerilogUvmRegisterFieldHandle handle) const {
  const auto desired = get(handle);
  const auto mirrored = get_mirrored(handle);
  const auto &snapshot = field(handle).snapshot;
  const auto resets = field_resets_.find(snapshot.identity);
  return {handle, desired, mirrored,
          writable(snapshot.access) &&
              (snapshot.is_volatile || desired != mirrored),
          resets == field_resets_.end()
              ? std::map<std::string, PackedLogic4, std::less<>>{}
              : resets->second.values};
}

SystemVerilogUvmRegisterValueSnapshot
SystemVerilogUvmRegisterModelService::value_snapshot(
    const SystemVerilogUvmRegisterHandle handle) const {
  return {handle, get(handle), get_mirrored(handle), needs_update(handle)};
}

bool SystemVerilogUvmRegisterModelService::needs_update(
    const SystemVerilogUvmRegisterHandle handle) const {
  const auto &snapshot = reg(handle).snapshot;
  require_locked(snapshot.block);
  const auto &state = register_values_.at(snapshot.identity);
  for (const auto &field_handle : register_fields_.at(snapshot.identity)) {
    const auto &field_snapshot = field(field_handle).snapshot;
    if (writable(field_snapshot.access) &&
        (field_snapshot.is_volatile ||
        extract(state.desired, field_snapshot.least_significant_bit,
                field_snapshot.width_bits) !=
            extract(state.mirrored, field_snapshot.least_significant_bit,
                    field_snapshot.width_bits)))
      return true;
  }
  return state.desired != state.mirrored;
}

SystemVerilogUvmRegisterCompareResult
SystemVerilogUvmRegisterModelService::compare(
    const SystemVerilogUvmRegisterHandle handle,
    const PackedLogic4 &expected) const {
  const auto &snapshot = reg(handle).snapshot;
  require_locked(snapshot.block);
  if (expected.width() != snapshot.width_bits)
    fail_value(kInvalidOperation, "invalid UVM register compare width");
  if (has_unknown(expected))
    return {SystemVerilogUvmRegisterOperationStatus::HasUnknown, false, {}};
  const auto &mirrored = register_values_.at(snapshot.identity).mirrored;
  SystemVerilogUvmRegisterCompareResult result;
  for (const auto &field_handle : register_fields_.at(snapshot.identity)) {
    const auto &field_snapshot = field(field_handle).snapshot;
    if (field_snapshot.compare == SystemVerilogUvmRegisterComparePolicy::NoCheck)
      continue;
    if (extract(mirrored, field_snapshot.least_significant_bit,
                field_snapshot.width_bits) !=
        extract(expected, field_snapshot.least_significant_bit,
                field_snapshot.width_bits)) {
      result.matches = false;
      result.mismatches.push_back(field_handle);
    }
  }
  if (register_fields_.at(snapshot.identity).empty())
    result.matches = mirrored == expected;
  return result;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::predict(
    const SystemVerilogUvmRegisterFieldHandle handle, PackedLogic4 value,
    const SystemVerilogUvmRegisterPredictKind kind) {
  const auto &snapshot = field(handle).snapshot;
  require_locked(snapshot.block);
  if (kind > SystemVerilogUvmRegisterPredictKind::Write)
    fail_value(kInvalidOperation, "invalid UVM register prediction kind");
  if (value.width() != snapshot.width_bits) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                    "UVM register field prediction width mismatch",
                    snapshot.width_bits);
  }
  if (has_unknown(value)) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::HasUnknown,
                    "UVM register field prediction contains X/Z",
                    snapshot.width_bits);
  }
  const auto register_identity = reg(snapshot.register_handle).snapshot.identity;
  auto candidate = register_values_.at(register_identity);
  const auto old_value = extract(candidate.mirrored,
                                 snapshot.least_significant_bit,
                                 snapshot.width_bits);
  PackedLogic4 predicted = value;
  if (kind == SystemVerilogUvmRegisterPredictKind::Read) {
    if (!readable(snapshot.access)) {
      record_operation(false);
      return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                      "UVM register field is not readable",
                      snapshot.width_bits);
    }
    predicted = apply_read(snapshot.access, value);
  } else if (kind == SystemVerilogUvmRegisterPredictKind::Write) {
    if (!writable(snapshot.access)) {
      record_operation(false);
      return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                      "UVM register field is not writable",
                      snapshot.width_bits);
    }
    predicted = apply_write(snapshot.access, old_value, value,
                            candidate.written_fields[snapshot.identity]);
    if (write_once(snapshot.access))
      candidate.written_fields[snapshot.identity] = true;
  }
  candidate.mirrored = insert(std::move(candidate.mirrored), predicted,
                              snapshot.least_significant_bit);
  candidate.desired = insert(std::move(candidate.desired), predicted,
                             snapshot.least_significant_bit);
  const bool changed = candidate != register_values_.at(register_identity);
  record_operation(changed);
  register_values_.at(register_identity) = std::move(candidate);
  return {SystemVerilogUvmRegisterOperationStatus::IsOk, predicted, changed, {}};
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::predict(
    const SystemVerilogUvmRegisterHandle handle, PackedLogic4 value,
    const SystemVerilogUvmRegisterPredictKind kind) {
  const auto &snapshot = reg(handle).snapshot;
  require_locked(snapshot.block);
  if (kind > SystemVerilogUvmRegisterPredictKind::Write)
    fail_value(kInvalidOperation, "invalid UVM register prediction kind");
  if (value.width() != snapshot.width_bits) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                    "UVM register prediction width mismatch",
                    snapshot.width_bits);
  }
  if (has_unknown(value)) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::HasUnknown,
                    "UVM register prediction contains X/Z",
                    snapshot.width_bits);
  }
  auto candidate = register_values_.at(snapshot.identity);
  if (kind == SystemVerilogUvmRegisterPredictKind::Direct) {
    candidate.desired = value;
    candidate.mirrored = value;
  } else {
    PackedLogic4 predicted = value;
    for (const auto &field_handle : register_fields_.at(snapshot.identity)) {
      const auto &field_snapshot = field(field_handle).snapshot;
      const auto incoming = extract(value, field_snapshot.least_significant_bit,
                                    field_snapshot.width_bits);
      const auto old_field =
          extract(candidate.mirrored, field_snapshot.least_significant_bit,
                  field_snapshot.width_bits);
      PackedLogic4 field_value = old_field;
      if (kind == SystemVerilogUvmRegisterPredictKind::Read &&
          readable(field_snapshot.access)) {
        field_value = apply_read(field_snapshot.access, incoming);
      } else if (kind == SystemVerilogUvmRegisterPredictKind::Write &&
                 writable(field_snapshot.access)) {
        field_value = apply_write(
            field_snapshot.access, old_field, incoming,
            candidate.written_fields[field_snapshot.identity]);
        if (write_once(field_snapshot.access))
          candidate.written_fields[field_snapshot.identity] = true;
      }
      predicted = insert(std::move(predicted), field_value,
                         field_snapshot.least_significant_bit);
    }
    candidate.desired = predicted;
    candidate.mirrored = predicted;
  }
  const bool changed = candidate != register_values_.at(snapshot.identity);
  const auto result_value = candidate.mirrored;
  record_operation(changed);
  register_values_.at(snapshot.identity) = std::move(candidate);
  return {SystemVerilogUvmRegisterOperationStatus::IsOk, result_value, changed,
          {}};
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::read(
    const SystemVerilogUvmRegisterFieldHandle handle) {
  const auto observed = get_mirrored(handle);
  auto result = predict(handle, observed,
                        SystemVerilogUvmRegisterPredictKind::Read);
  if (result.success())
    result.value = observed;
  return result;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::read(
    const SystemVerilogUvmRegisterHandle handle) {
  const auto observed = get_mirrored(handle);
  auto result = predict(handle, observed,
                        SystemVerilogUvmRegisterPredictKind::Read);
  if (result.success())
    result.value = observed;
  return result;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::write(
    const SystemVerilogUvmRegisterFieldHandle handle, PackedLogic4 value) {
  return predict(handle, std::move(value),
                 SystemVerilogUvmRegisterPredictKind::Write);
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::write(
    const SystemVerilogUvmRegisterHandle handle, PackedLogic4 value) {
  return predict(handle, std::move(value),
                 SystemVerilogUvmRegisterPredictKind::Write);
}

std::size_t SystemVerilogUvmRegisterModelService::flatten_memory_index(
    const SystemVerilogUvmRegisterMemorySnapshot &snapshot,
    const std::span<const std::size_t> indices) const {
  if (indices.size() != snapshot.dimensions.size())
    fail_value(kInvalidOperation, "UVM memory index rank mismatch");
  std::size_t result{};
  for (std::size_t dimension = 0; dimension < indices.size(); ++dimension) {
    if (indices[dimension] >= snapshot.dimensions[dimension])
      fail_value(kInvalidOperation, "UVM memory index is out of range");
    result = result * snapshot.dimensions[dimension] + indices[dimension];
  }
  return result;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::read(
    const SystemVerilogUvmRegisterMemoryHandle handle,
    const std::span<const std::size_t> indices) {
  const auto &snapshot = memory(handle).snapshot;
  require_locked(snapshot.block);
  const auto index = flatten_memory_index(snapshot, indices);
  if (!readable(snapshot.access)) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                    "UVM memory is not readable", snapshot.word_width_bits);
  }
  auto &state = memory_values_[snapshot.identity];
  const auto found = state.mirrored.find(index);
  const auto observed = found == state.mirrored.end()
                            ? filled(snapshot.word_width_bits, false)
                            : found->second;
  const auto predicted = apply_read(snapshot.access, observed);
  const bool materializes = predicted != observed && found == state.mirrored.end();
  if (materializes &&
      state.mirrored.size() >= limits_.maximum_materialized_memory_words) {
    fail_value(kOperationLimit,
               "UVM materialized memory-word limit exceeded");
  }
  if (materializes && snapshot.word_width_bits >
                          limits_.maximum_materialized_memory_bits -
                              materialized_memory_bits_) {
    fail_value(kOperationLimit,
               "UVM materialized memory-bit limit exceeded");
  }
  record_operation(predicted != observed);
  if (predicted != observed) {
    state.mirrored.insert_or_assign(index, predicted);
    state.desired.insert_or_assign(index, predicted);
  }
  if (materializes)
    materialized_memory_bits_ += snapshot.word_width_bits;
  return {SystemVerilogUvmRegisterOperationStatus::IsOk, observed,
          predicted != observed, {}};
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::write(
    const SystemVerilogUvmRegisterMemoryHandle handle,
    const std::span<const std::size_t> indices, PackedLogic4 value) {
  return write(handle, indices, std::move(value), {});
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::write(
    const SystemVerilogUvmRegisterMemoryHandle handle,
    const std::span<const std::size_t> indices, PackedLogic4 value,
    const std::span<const std::uint8_t> byte_enables) {
  const auto &snapshot = memory(handle).snapshot;
  require_locked(snapshot.block);
  const auto index = flatten_memory_index(snapshot, indices);
  if (value.width() != snapshot.word_width_bits) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                    "UVM memory write width mismatch",
                    snapshot.word_width_bits);
  }
  const auto word_bytes =
      static_cast<std::size_t>(snapshot.word_width_bits / 8U) +
      static_cast<std::size_t>((snapshot.word_width_bits % 8U) != 0U);
  if (!byte_enables.empty() &&
      (byte_enables.size() != word_bytes ||
       std::ranges::any_of(byte_enables,
                           [](const auto enable) { return enable > 1U; }))) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                    "UVM memory byte enables are invalid",
                    snapshot.word_width_bits);
  }
  bool enabled_unknown{};
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    if (!byte_enables.empty() && byte_enables[bit / 8U] == 0)
      continue;
    const auto logic = value.get(bit);
    enabled_unknown = enabled_unknown || logic == Logic4::x || logic == Logic4::z;
  }
  if (enabled_unknown) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::HasUnknown,
                    "UVM memory write contains X/Z",
                    snapshot.word_width_bits);
  }
  if (!writable(snapshot.access)) {
    record_operation(false);
    return rejected(SystemVerilogUvmRegisterOperationStatus::NotOk,
                    "UVM memory is not writable", snapshot.word_width_bits);
  }
  auto &state = memory_values_[snapshot.identity];
  const auto found = state.mirrored.find(index);
  const auto old_value = found == state.mirrored.end()
                             ? filled(snapshot.word_width_bits, false)
                             : found->second;
  if (!byte_enables.empty() &&
      std::ranges::none_of(byte_enables,
                           [](const auto enable) { return enable != 0; })) {
    record_operation(false);
    return {SystemVerilogUvmRegisterOperationStatus::IsOk, old_value, false,
            {}};
  }
  auto predicted =
      apply_write(snapshot.access, old_value, value, state.written.contains(index));
  if (!byte_enables.empty()) {
    for (std::size_t bit = 0; bit < predicted.width(); ++bit) {
      if (byte_enables[bit / 8U] == 0)
        predicted.set(bit, old_value.get(bit));
    }
  }
  const bool materializes = found == state.mirrored.end();
  if (materializes &&
      state.mirrored.size() >= limits_.maximum_materialized_memory_words) {
    fail_value(kOperationLimit,
               "UVM materialized memory-word limit exceeded");
  }
  if (materializes && snapshot.word_width_bits >
                          limits_.maximum_materialized_memory_bits -
                              materialized_memory_bits_) {
    fail_value(kOperationLimit,
               "UVM materialized memory-bit limit exceeded");
  }
  const bool state_changed = found == state.mirrored.end() ||
                             predicted != old_value ||
                             (write_once(snapshot.access) &&
                              !state.written.contains(index));
  record_operation(state_changed);
  state.mirrored.insert_or_assign(index, predicted);
  state.desired.insert_or_assign(index, predicted);
  if (write_once(snapshot.access))
    state.written.insert(index);
  if (materializes)
    materialized_memory_bits_ += snapshot.word_width_bits;
  return {SystemVerilogUvmRegisterOperationStatus::IsOk, predicted,
          predicted != old_value, {}};
}

void SystemVerilogUvmRegisterModelService::reset(
    const SystemVerilogUvmRegisterBlockHandle top,
    const std::string_view kind) {
  require_locked(top);
  if (kind.size() > limits_.maximum_reset_name_bytes)
    fail_value(kOperationLimit, "UVM register reset-name limit exceeded");
  if (kind.empty() || kind.find('\0') != std::string_view::npos)
    fail_value(kInvalidOperation, "invalid UVM register reset kind");
  const auto top_identity = block(top).snapshot.identity;
  std::set<std::uint64_t> subtree{top_identity};
  std::vector<SystemVerilogUvmRegisterBlockHandle> pending{top};
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    const auto current_identity = block(current).snapshot.identity;
    if (const auto children = child_blocks_.find(current_identity);
        children != child_blocks_.end()) {
      for (const auto &child : children->second) {
        subtree.insert(block(child).snapshot.identity);
        pending.push_back(child);
      }
    }
  }
  std::map<std::uint64_t, RegisterValueState> candidates;
  for (const auto &[identity, entry] : fields_) {
    const auto &field_snapshot = entry.snapshot;
    if (!subtree.contains(block(field_snapshot.block).snapshot.identity))
      continue;
    const auto reset_entry = field_resets_.find(identity);
    if (reset_entry == field_resets_.end())
      continue;
    const auto reset_value = reset_entry->second.values.find(kind);
    if (reset_value == reset_entry->second.values.end())
      continue;
    const auto register_identity =
        reg(field_snapshot.register_handle).snapshot.identity;
    auto [candidate_entry, inserted] = candidates.try_emplace(
        register_identity, register_values_.at(register_identity));
    (void)inserted;
    auto &candidate = candidate_entry->second;
    candidate.desired =
        insert(std::move(candidate.desired), reset_value->second,
               field_snapshot.least_significant_bit);
    candidate.mirrored =
        insert(std::move(candidate.mirrored), reset_value->second,
               field_snapshot.least_significant_bit);
    if (kind == "HARD")
      candidate.written_fields.erase(identity);
  }
  bool changed{};
  for (const auto &[identity, candidate] : candidates) {
    if (register_values_.at(identity) != candidate) {
      changed = true;
      break;
    }
  }
  record_operation(changed);
  for (auto &[identity, candidate] : candidates)
    register_values_.at(identity) = std::move(candidate);
}

} // namespace fsim::runtime
