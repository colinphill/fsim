// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidSequence{"FSIM-UVM-REG-011"};
constexpr std::string_view kSequenceLimit{"FSIM-UVM-REG-012"};

[[noreturn]] void fail_sequence(const std::string_view code,
                                const std::string_view message) {
  throw SystemVerilogUvmRegisterModelError{std::string{code},
                                           std::string{message}};
}

[[nodiscard]] std::uint32_t
kind_mask(const SystemVerilogUvmRegisterStandardSequenceKind kind) {
  return UINT32_C(1) << static_cast<std::uint8_t>(kind);
}

[[nodiscard]] bool rights_readable(const SystemVerilogUvmRegisterMapRights r) {
  return r != SystemVerilogUvmRegisterMapRights::WriteOnly;
}

[[nodiscard]] bool rights_writable(const SystemVerilogUvmRegisterMapRights r) {
  return r != SystemVerilogUvmRegisterMapRights::ReadOnly;
}

[[nodiscard]] bool policy_readable(
    const SystemVerilogUvmRegisterAccessPolicy policy) {
  switch (policy) {
  case SystemVerilogUvmRegisterAccessPolicy::WriteOnly:
  case SystemVerilogUvmRegisterAccessPolicy::WriteOnlyClear:
  case SystemVerilogUvmRegisterAccessPolicy::WriteOnlySet:
  case SystemVerilogUvmRegisterAccessPolicy::NoAccess:
    return false;
  default:
    return true;
  }
}

[[nodiscard]] bool policy_writable(
    const SystemVerilogUvmRegisterAccessPolicy policy) {
  return policy != SystemVerilogUvmRegisterAccessPolicy::ReadOnly &&
         policy != SystemVerilogUvmRegisterAccessPolicy::NoAccess;
}

[[nodiscard]] bool in_subtree(const std::string_view top,
                              const std::string_view name) {
  return name == top ||
         (name.size() > top.size() && name.starts_with(top) &&
          name[top.size()] == '.');
}

void validate_exclusion(
    const SystemVerilogUvmRegisterStandardSequenceExclusion &entry) {
  if (entry.full_name.empty() || entry.full_name.find('\0') != std::string::npos ||
      entry.kinds == 0 || (entry.kinds & ~UINT32_C(0xff)) != 0) {
    fail_sequence(kInvalidSequence,
                  "invalid UVM standard register-sequence exclusion");
  }
}

class StandardSequenceRunner final {
public:
  StandardSequenceRunner(
      SystemVerilogUvmRegisterModelService &model,
      const SystemVerilogUvmRegisterStandardSequenceOptions &options,
      const SystemVerilogUvmRegisterBlockSnapshot &top,
      const std::size_t operation_budget, const std::size_t failure_budget,
      const std::size_t failure_byte_budget)
      : model_(model), options_(options), top_(top),
        operation_budget_(operation_budget), failure_budget_(failure_budget),
        failure_byte_budget_(failure_byte_budget), random_state_(options.seed) {
    result_.kind = options.kind;
    result_.top = options.top;
    result_.map = options.map;
    result_.reset_kind = options.reset_kind;
    result_.seed = options.seed;
  }

  [[nodiscard]] SystemVerilogUvmRegisterStandardSequenceSnapshot run() {
    inventory();
    if (static_cast<std::uint8_t>(options_.kind) <=
        static_cast<std::uint8_t>(
            SystemVerilogUvmRegisterStandardSequenceKind::BitBash)) {
      dispatch_reset_sequence();
    } else {
      dispatch_access_sequence();
    }
    result_.final_random_state = random_state_;
    return std::move(result_);
  }

private:
  void dispatch_reset_sequence() {
    switch (options_.kind) {
    case SystemVerilogUvmRegisterStandardSequenceKind::Reset:
      run_reset(false);
      break;
    case SystemVerilogUvmRegisterStandardSequenceKind::HardwareReset:
      run_reset(true);
      if (!stopped()) {
        run_register_reset_checks();
      }
      break;
    case SystemVerilogUvmRegisterStandardSequenceKind::BitBash:
      run_bit_bash();
      break;
    default:
      fail_sequence(kInvalidSequence,
                    "invalid reset-oriented standard sequence kind");
    }
  }

  void dispatch_access_sequence() {
    switch (options_.kind) {
    case SystemVerilogUvmRegisterStandardSequenceKind::Access:
      run_register_access(false);
      break;
    case SystemVerilogUvmRegisterStandardSequenceKind::SharedAccess:
      run_register_access(true);
      break;
    case SystemVerilogUvmRegisterStandardSequenceKind::MemoryAccess:
      run_memory_access(false);
      break;
    case SystemVerilogUvmRegisterStandardSequenceKind::MemoryWalk:
      run_memory_access(true);
      break;
    case SystemVerilogUvmRegisterStandardSequenceKind::Traverse:
      break;
    default:
      fail_sequence(kInvalidSequence,
                    "invalid access-oriented standard sequence kind");
    }
  }
  [[nodiscard]] bool excluded(const std::string_view full_name) {
    for (const auto &entry : options_.exclusions) {
      if ((entry.kinds & kind_mask(options_.kind)) != 0 &&
          in_subtree(entry.full_name, full_name)) {
        if (excluded_names_.insert(std::string{full_name}).second) {
          ++result_.excluded;
        }
        return true;
      }
    }
    return false;
  }

  void inventory() {
    inventory_blocks();
    if (options_.map) {
      inventory_mapped();
    } else {
      inventory_declarations();
    }
    inventory_fields();
  }

  void inventory_blocks() {
    for (const auto &entry : model_.blocks()) {
      if (entry.root == top_.root && in_subtree(top_.full_name, entry.full_name) &&
          !excluded(entry.full_name)) {
        ++result_.visited_blocks;
      }
    }
  }

  void inventory_mapped() {
    result_.visited_maps = 1;
    for (const auto &entry : model_.mapped_registers(*options_.map, true)) {
      const auto declaration = model_.snapshot(entry.register_handle);
      if (!entry.unmapped && !excluded(declaration.full_name)) {
        registers_.push_back(entry);
        ++result_.visited_registers;
      }
    }
    for (const auto &entry : model_.mapped_memories(*options_.map, true)) {
      const auto declaration = model_.snapshot(entry.memory);
      if (!entry.unmapped && !excluded(declaration.full_name)) {
        memories_.push_back(entry);
        ++result_.visited_memories;
      }
    }
  }

  void inventory_declarations() {
    for (const auto &entry : model_.maps()) {
      const auto block = model_.snapshot(entry.block);
      if (entry.root == top_.root && in_subtree(top_.full_name, block.full_name) &&
          !excluded(entry.full_name)) {
        ++result_.visited_maps;
      }
    }
    for (const auto &entry : model_.registers()) {
      const auto block = model_.snapshot(entry.block);
      if (entry.root == top_.root && in_subtree(top_.full_name, block.full_name) &&
          !excluded(entry.full_name)) {
        ++result_.visited_registers;
      }
    }
    for (const auto &entry : model_.memories()) {
      const auto block = model_.snapshot(entry.block);
      if (entry.root == top_.root && in_subtree(top_.full_name, block.full_name) &&
          !excluded(entry.full_name)) {
        ++result_.visited_memories;
      }
    }
  }

  void inventory_fields() {
    for (const auto &entry : model_.fields()) {
      const auto block = model_.snapshot(entry.block);
      if (entry.root == top_.root && in_subtree(top_.full_name, block.full_name) &&
          !excluded(entry.full_name)) {
        ++result_.visited_fields;
      }
    }
  }

  [[nodiscard]] bool stopped() const {
    return result_.failures.size() >= options_.maximum_failures;
  }

  void note_operation() {
    if (result_.operations == operation_budget_) {
      fail_sequence(kSequenceLimit,
                    "UVM standard register-sequence operation limit exceeded");
    }
    ++result_.operations;
  }

  void add_failure(std::string target, std::string message) {
    if (stopped()) {
      return;
    }
    const auto bytes = target.size() + message.size();
    if (result_.failures.size() == failure_budget_ ||
        bytes > failure_byte_budget_ - failure_bytes_) {
      fail_sequence(kSequenceLimit,
                    "UVM standard register-sequence failure limit exceeded");
    }
    failure_bytes_ += bytes;
    result_.failures.push_back({std::move(target), std::move(message)});
  }

  [[nodiscard]] std::uint64_t next_random() {
    random_state_ += UINT64_C(0x9e3779b97f4a7c15);
    auto value = random_state_;
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
  }

  [[nodiscard]] PackedLogic4 random_value(const std::size_t width) {
    std::string text(width, '0');
    std::uint64_t word{};
    for (std::size_t i = 0; i < width; ++i) {
      if ((i & 63U) == 0) {
        word = next_random();
      }
      text[width - i - 1] = ((word >> (i & 63U)) & 1U) != 0 ? '1' : '0';
    }
    return PackedLogic4::from_msb_string(text);
  }

  [[nodiscard]] std::optional<SystemVerilogUvmRegisterOperationResult>
  invoke_read(const SystemVerilogUvmRegisterTarget &target,
              const std::string_view name) {
    note_operation();
    ++result_.reads;
    try {
      auto value = options_.read ? options_.read(target) : direct_read(target);
      if (!value.success()) {
        add_failure(std::string{name}, value.message.empty()
                                           ? "read returned non-OK status"
                                           : value.message);
        return std::nullopt;
      }
      if (value.value.width() != target_width(target)) {
        add_failure(std::string{name}, "read returned an invalid value width");
        return std::nullopt;
      }
      return value;
    } catch (const std::exception &error) {
      add_failure(std::string{name}, error.what());
    } catch (...) {
      add_failure(std::string{name}, "read callback threw nonstandard exception");
    }
    return std::nullopt;
  }

  [[nodiscard]] bool invoke_write(const SystemVerilogUvmRegisterTarget &target,
                                  const PackedLogic4 &value,
                                  const std::string_view name) {
    note_operation();
    ++result_.writes;
    try {
      auto status =
          options_.write ? options_.write(target, value) : direct_write(target, value);
      if (!status.success()) {
        add_failure(std::string{name}, status.message.empty()
                                           ? "write returned non-OK status"
                                           : status.message);
        return false;
      }
      return true;
    } catch (const std::exception &error) {
      add_failure(std::string{name}, error.what());
    } catch (...) {
      add_failure(std::string{name}, "write callback threw nonstandard exception");
    }
    return false;
  }

  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  direct_read(const SystemVerilogUvmRegisterTarget &target) {
    return model_.callback_read(target, options_.map);
  }

  [[nodiscard]] std::size_t
  target_width(const SystemVerilogUvmRegisterTarget &target) const {
    if (target.register_handle) {
      return model_.snapshot(*target.register_handle).width_bits;
    }
    if (target.field) {
      return model_.snapshot(*target.field).width_bits;
    }
    return model_.snapshot(*target.memory).word_width_bits;
  }

  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  direct_write(const SystemVerilogUvmRegisterTarget &target,
               const PackedLogic4 &value) {
    return model_.callback_write(target, value, options_.map);
  }

  void compare_value(const std::string_view name, const PackedLogic4 &actual,
                     const PackedLogic4 &expected) {
    ++result_.comparisons;
    if (actual != expected) {
      add_failure(std::string{name}, "read value did not match expected value");
    }
  }

  void run_reset(const bool hardware) {
    note_operation();
    if (hardware) {
      if (!options_.hardware_reset) {
        add_failure(top_.full_name, "hardware-reset callback is not configured");
        return;
      }
      try {
        options_.hardware_reset();
      } catch (const std::exception &error) {
        add_failure(top_.full_name, error.what());
        return;
      } catch (...) {
        add_failure(top_.full_name,
                    "hardware-reset callback threw nonstandard exception");
        return;
      }
    }
    model_.reset(options_.top, options_.reset_kind);
  }

  void run_register_reset_checks() {
    for (const auto &mapping : registers_) {
      if (stopped()) {
        break;
      }
      const auto declaration = model_.snapshot(mapping.register_handle);
      if (!rights_readable(mapping.rights)) {
        continue;
      }
      const auto expected = model_.get_mirrored(mapping.register_handle);
      const auto target = SystemVerilogUvmRegisterTarget{
          mapping.register_handle, std::nullopt, std::nullopt, 0};
      if (const auto actual = invoke_read(target, declaration.full_name)) {
        compare_value(declaration.full_name, actual->value, expected);
      }
    }
  }

  void run_bit_bash() {
    for (const auto &field : model_.fields()) {
      if (stopped()) {
        break;
      }
      const auto block = model_.snapshot(field.block);
      if (!in_subtree(top_.full_name, block.full_name) ||
          excluded(field.full_name) || field.is_volatile ||
          field.compare == SystemVerilogUvmRegisterComparePolicy::NoCheck ||
          field.access != SystemVerilogUvmRegisterAccessPolicy::ReadWrite ||
          !mapped_field_accessible(field)) {
        continue;
      }
      const auto original = model_.get_mirrored(field.handle);
      for (std::size_t bit = 0; bit < field.width_bits && !stopped(); ++bit) {
        auto text = original.to_msb_string();
        text[field.width_bits - bit - 1] =
            text[field.width_bits - bit - 1] == '0' ? '1' : '0';
        const auto expected = PackedLogic4::from_msb_string(text);
        const auto target = SystemVerilogUvmRegisterTarget{
            std::nullopt, field.handle, std::nullopt, 0};
        if (invoke_write(target, expected, field.full_name)) {
          if (const auto actual = invoke_read(target, field.full_name)) {
            compare_value(field.full_name, actual->value, expected);
          }
        }
      }
      const auto target = SystemVerilogUvmRegisterTarget{
          std::nullopt, field.handle, std::nullopt, 0};
      (void)invoke_write(target, original, field.full_name);
    }
  }

  [[nodiscard]] bool mapped_field_accessible(
      const SystemVerilogUvmRegisterFieldSnapshot &field) const {
    return std::ranges::any_of(registers_, [&](const auto &mapping) {
      return mapping.register_handle == field.register_handle &&
             rights_readable(mapping.rights) && rights_writable(mapping.rights);
    });
  }

  [[nodiscard]] bool shared(
      const SystemVerilogUvmRegisterHandle handle) const {
    std::size_t mappings{};
    for (const auto &map : model_.maps()) {
      if (map.root != top_.root) {
        continue;
      }
      for (const auto &entry : model_.mapped_registers(map.handle, false)) {
        if (!entry.unmapped && entry.register_handle == handle && ++mappings > 1) {
          return true;
        }
      }
    }
    return false;
  }

  void run_register_access(const bool shared_only) {
    for (const auto &mapping : registers_) {
      if (stopped() || (shared_only && !shared(mapping.register_handle))) {
        continue;
      }
      const auto declaration = model_.snapshot(mapping.register_handle);
      if (!rights_readable(mapping.rights) || !rights_writable(mapping.rights)) {
        continue;
      }
      const auto original = model_.get_mirrored(mapping.register_handle);
      const auto value = random_value(declaration.width_bits);
      const auto target = SystemVerilogUvmRegisterTarget{
          mapping.register_handle, std::nullopt, std::nullopt, 0};
      if (invoke_write(target, value, declaration.full_name)) {
        if (const auto actual = invoke_read(target, declaration.full_name)) {
          compare_value(declaration.full_name, actual->value,
                        model_.get_mirrored(mapping.register_handle));
        }
      }
      (void)invoke_write(target, original, declaration.full_name);
    }
  }

  void run_memory_access(const bool walking) {
    for (const auto &mapping : memories_) {
      if (stopped()) {
        break;
      }
      const auto memory = model_.snapshot(mapping.memory);
      if (!rights_readable(mapping.rights) || !rights_writable(mapping.rights) ||
          !policy_readable(memory.access) || !policy_writable(memory.access)) {
        continue;
      }
      for (std::size_t word = 0; word < memory.word_count && !stopped(); ++word) {
        PackedLogic4 value;
        if (walking) {
          std::string text(memory.word_width_bits, '0');
          text[memory.word_width_bits - (word % memory.word_width_bits) - 1] = '1';
          value = PackedLogic4::from_msb_string(text);
        } else {
          value = random_value(memory.word_width_bits);
        }
        const auto target = SystemVerilogUvmRegisterTarget{
            std::nullopt, std::nullopt, mapping.memory, word};
        std::ostringstream name;
        name << memory.full_name << '[' << word << ']';
        if (invoke_write(target, value, name.str())) {
          if (const auto actual = invoke_read(target, name.str())) {
            compare_value(name.str(), actual->value, value);
          }
        }
      }
    }
  }

  SystemVerilogUvmRegisterModelService &model_;
  const SystemVerilogUvmRegisterStandardSequenceOptions &options_;
  const SystemVerilogUvmRegisterBlockSnapshot &top_;
  std::size_t operation_budget_{};
  std::size_t failure_budget_{};
  std::size_t failure_byte_budget_{};
  std::size_t failure_bytes_{};
  std::uint64_t random_state_{};
  SystemVerilogUvmRegisterStandardSequenceSnapshot result_;
  std::vector<SystemVerilogUvmRegisterMapRegisterSnapshot> registers_;
  std::vector<SystemVerilogUvmRegisterMapMemorySnapshot> memories_;
  std::set<std::string, std::less<>> excluded_names_;
};

} // namespace

SystemVerilogUvmRegisterStandardSequenceSnapshot
SystemVerilogUvmRegisterModelService::run_standard_sequence(
    const SystemVerilogUvmRegisterStandardSequenceOptions &options) {
  const auto top = snapshot(options.top);
  require_locked(options.top);
  if (options.reset_kind.empty() ||
      options.reset_kind.find('\0') != std::string::npos ||
      options.maximum_failures == 0) {
    fail_sequence(kInvalidSequence,
                  "invalid UVM standard register-sequence options");
  }
  if (options.reset_kind.size() > limits_.maximum_reset_name_bytes ||
      options.maximum_failures > limits_.maximum_standard_sequence_failures ||
      options.exclusions.size() > limits_.maximum_standard_sequence_exclusions) {
    fail_sequence(kSequenceLimit,
                  "UVM standard register-sequence option limit exceeded");
  }
  std::size_t exclusion_bytes{};
  for (const auto &entry : options.exclusions) {
    validate_exclusion(entry);
    if (entry.full_name.size() >
        limits_.maximum_standard_sequence_exclusion_bytes - exclusion_bytes) {
      fail_sequence(kSequenceLimit,
                    "UVM standard register-sequence exclusion-byte limit exceeded");
    }
    exclusion_bytes += entry.full_name.size();
  }
  if (standard_sequences_.size() == limits_.maximum_standard_sequences ||
      next_standard_sequence_order_ ==
          std::numeric_limits<std::uint64_t>::max()) {
    fail_sequence(kSequenceLimit,
                  "UVM standard register-sequence inventory limit exceeded");
  }
  if (options.map) {
    const auto selected = snapshot(*options.map);
    const auto owner = snapshot(selected.block);
    if (selected.root != top.root ||
        !in_subtree(top.full_name, owner.full_name)) {
      fail_sequence(kInvalidSequence,
                    "standard register-sequence map is outside the root");
    }
  } else if (options.kind != SystemVerilogUvmRegisterStandardSequenceKind::Reset &&
             options.kind !=
                 SystemVerilogUvmRegisterStandardSequenceKind::Traverse) {
    fail_sequence(kInvalidSequence,
                  "standard register-sequence kind requires a selected map");
  }
  StandardSequenceRunner runner{
      *this, options, top,
      limits_.maximum_standard_sequence_operations -
          standard_sequence_operations_,
      limits_.maximum_standard_sequence_failures - standard_sequence_failures_,
      limits_.maximum_standard_sequence_failure_bytes -
          standard_sequence_failure_bytes_};
  auto result = runner.run();
  result.execution_order = next_standard_sequence_order_++;
  std::size_t failure_bytes{};
  for (const auto &failure : result.failures) {
    failure_bytes += failure.target.size() + failure.message.size();
  }
  standard_sequence_operations_ += result.operations;
  standard_sequence_failures_ += result.failures.size();
  standard_sequence_failure_bytes_ += failure_bytes;
  standard_sequences_.push_back(result);
  record_mutation();
  publish_activity(result.success() ? SystemVerilogUvmActivityAction::Completed
                                    : SystemVerilogUvmActivityAction::Failed,
                   top.full_name, top.root, result.execution_order,
                   "standard-sequence:" +
                       std::to_string(static_cast<unsigned>(result.kind)));
  return result;
}

} // namespace fsim::runtime
