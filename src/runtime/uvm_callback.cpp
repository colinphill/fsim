// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_callback.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <ranges>
#include <sstream>
#include <type_traits>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidCallback{"FSIM-UVM-CALLBACK-001"};
constexpr std::string_view kCallbackResource{"FSIM-UVM-CALLBACK-002"};
constexpr std::string_view kInvalidTransaction{"FSIM-UVM-TR-001"};
constexpr std::string_view kTransactionResource{"FSIM-UVM-TR-002"};

[[noreturn]] void callback_fail(const std::string_view code,
                                const std::string_view message) {
  throw SystemVerilogUvmCallbackError{std::string{code}, std::string{message}};
}

[[noreturn]] void transaction_fail(const std::string_view code,
                                   const std::string_view message) {
  throw SystemVerilogUvmTransactionError{std::string{code},
                                         std::string{message}};
}

[[nodiscard]] std::size_t
attribute_bytes(const SystemVerilogUvmCallbackAttributes &attributes) {
  std::size_t result{};
  for (const auto &[name, value] : attributes) {
    if (name.size() > std::numeric_limits<std::size_t>::max() - result ||
        value.size() >
            std::numeric_limits<std::size_t>::max() - result - name.size()) {
      return std::numeric_limits<std::size_t>::max();
    }
    result += name.size() + value.size();
  }
  return result;
}

[[nodiscard]] std::string
attribute_value_text(const SystemVerilogUvmTransactionAttributeValue &value) {
  return std::visit(
      [](const auto &selected) -> std::string {
        using Value = std::remove_cvref_t<decltype(selected)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return selected;
        } else if constexpr (std::is_same_v<Value, bool>) {
          return selected ? "true" : "false";
        } else if constexpr (std::is_same_v<Value, double>) {
          if (!std::isfinite(selected)) {
            transaction_fail(
                kInvalidTransaction,
                "UVM transaction attributes require finite real values");
          }
          std::array<char, 128> buffer{};
          const auto converted =
              std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                            selected, std::chars_format::general,
                            std::numeric_limits<double>::max_digits10);
          if (converted.ec != std::errc{}) {
            transaction_fail(
                kInvalidTransaction,
                "UVM transaction real attribute formatting failed");
          }
          return {buffer.data(), converted.ptr};
        } else {
          return std::to_string(selected);
        }
      },
      value);
}

[[nodiscard]] std::size_t transaction_attribute_bytes(
    const std::span<const SystemVerilogUvmTransactionAttribute> attributes) {
  std::size_t result{};
  for (const auto &attribute : attributes) {
    const auto value = attribute_value_text(attribute.value);
    if (attribute.name.size() >
            std::numeric_limits<std::size_t>::max() - result ||
        value.size() > std::numeric_limits<std::size_t>::max() - result -
                           attribute.name.size()) {
      return std::numeric_limits<std::size_t>::max();
    }
    result += attribute.name.size() + value.size();
  }
  return result;
}

[[nodiscard]] SystemVerilogUvmActivityAction
activity_action(const SystemVerilogUvmTransactionState state) {
  switch (state) {
  case SystemVerilogUvmTransactionState::Completed:
    return SystemVerilogUvmActivityAction::Completed;
  case SystemVerilogUvmTransactionState::Cancelled:
    return SystemVerilogUvmActivityAction::Cancelled;
  case SystemVerilogUvmTransactionState::Failed:
    return SystemVerilogUvmActivityAction::Failed;
  case SystemVerilogUvmTransactionState::Active:
    break;
  }
  transaction_fail(kInvalidTransaction,
                   "UVM transaction end state must be terminal");
}

class DepthGuard final {
public:
  explicit DepthGuard(std::size_t &depth) : depth_(&depth) { ++*depth_; }
  ~DepthGuard() { --*depth_; }
  DepthGuard(const DepthGuard &) = delete;
  DepthGuard &operator=(const DepthGuard &) = delete;

private:
  std::size_t *depth_{};
};

} // namespace

SystemVerilogUvmCallbackError::SystemVerilogUvmCallbackError(
    std::string code, std::string message)
    : std::runtime_error{std::move(message)}, code_(std::move(code)) {}

SystemVerilogUvmCallbackService::SystemVerilogUvmCallbackService(
    SystemVerilogUvmObjectService &objects,
    SystemVerilogUvmComponentService &components,
    SystemVerilogUvmCallbackLimits limits)
    : objects_(&objects), components_(&components), limits_(limits) {
  if (limits_.maximum_callbacks == 0 ||
      limits_.maximum_callbacks_per_dispatch == 0 ||
      limits_.maximum_reentry_depth == 0 || limits_.maximum_failures == 0 ||
      limits_.maximum_type_bytes == 0 || limits_.maximum_name_bytes == 0 ||
      limits_.maximum_operation_bytes == 0 ||
      limits_.maximum_attribute_count == 0 ||
      limits_.maximum_attribute_bytes == 0 || limits_.maximum_mutations == 0) {
    callback_fail(kCallbackResource, "UVM callback limits must be nonzero");
  }
}

SystemVerilogUvmCallbackHandle SystemVerilogUvmCallbackService::add(
    SystemVerilogUvmCallbackDescriptor descriptor) {
  if (descriptor.target_type.empty() || descriptor.callback_type.empty() ||
      descriptor.name.empty() || !descriptor.callback) {
    callback_fail(
        kInvalidCallback,
        "UVM callback registration requires types, name, and callback");
  }
  if (descriptor.target_type.size() > limits_.maximum_type_bytes ||
      descriptor.callback_type.size() > limits_.maximum_type_bytes ||
      descriptor.name.size() > limits_.maximum_name_bytes) {
    callback_fail(kCallbackResource,
                  "UVM callback registration text exceeds limit");
  }
  if (descriptor.ordering != SystemVerilogUvmCallbackOrdering::Append &&
      descriptor.ordering != SystemVerilogUvmCallbackOrdering::Prepend) {
    callback_fail(kInvalidCallback, "invalid UVM callback ordering");
  }
  if (descriptor.instance != 0) {
    if (!objects_->contains(descriptor.instance) ||
        objects_->type_name(descriptor.instance) != descriptor.target_type) {
      callback_fail(
          kInvalidCallback,
          "instance UVM callback target does not have the registered type");
    }
  }
  const auto duplicate =
      std::ranges::find_if(callbacks_, [&](const auto &candidate) {
        const auto &snapshot = candidate.second.snapshot;
        return snapshot.target_type == descriptor.target_type &&
               snapshot.callback_type == descriptor.callback_type &&
               snapshot.name == descriptor.name &&
               snapshot.instance == descriptor.instance;
      });
  if (duplicate != callbacks_.end()) {
    callback_fail(kInvalidCallback, "duplicate UVM callback registration");
  }
  if (callbacks_.size() >= limits_.maximum_callbacks ||
      mutations_ >= limits_.maximum_mutations ||
      next_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_order_ == std::numeric_limits<std::uint64_t>::max()) {
    callback_fail(kCallbackResource,
                  "UVM callback registration ceiling exceeded");
  }

  const auto slot = next_slot_;
  const auto generation = generations_.try_emplace(slot, 1).first->second;
  const SystemVerilogUvmCallbackHandle handle{owner_, slot, generation};
  SystemVerilogUvmCallbackSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.scope = descriptor.instance == 0
                       ? SystemVerilogUvmCallbackScope::TypeWide
                       : SystemVerilogUvmCallbackScope::Instance;
  snapshot.target_type = std::move(descriptor.target_type);
  snapshot.callback_type = std::move(descriptor.callback_type);
  snapshot.name = std::move(descriptor.name);
  snapshot.instance = descriptor.instance;
  snapshot.mask = descriptor.mask;
  snapshot.order = next_order_;
  auto [position, inserted] = callbacks_.emplace(
      slot, Entry{std::move(snapshot), std::move(descriptor.callback)});
  if (!inserted) {
    callback_fail(kCallbackResource, "UVM callback identity collision");
  }
  try {
    if (descriptor.ordering == SystemVerilogUvmCallbackOrdering::Prepend) {
      order_.insert(order_.begin(), handle);
    } else {
      order_.push_back(handle);
    }
  } catch (...) {
    callbacks_.erase(position);
    throw;
  }
  ++next_slot_;
  ++next_order_;
  ++mutations_;
  publish_activity(SystemVerilogUvmActivityAction::Created,
                   position->second.snapshot, "registered");
  return handle;
}

void SystemVerilogUvmCallbackService::remove(
    const SystemVerilogUvmCallbackHandle handle) {
  auto &selected = entry(handle);
  if (mutations_ >= limits_.maximum_mutations ||
      selected.snapshot.handle.generation_ ==
          std::numeric_limits<std::uint64_t>::max()) {
    callback_fail(kCallbackResource, "UVM callback mutation ceiling exceeded");
  }
  const auto retained = selected.snapshot;
  std::erase(order_, handle);
  callbacks_.erase(handle.slot_);
  ++generations_.at(handle.slot_);
  ++mutations_;
  publish_activity(SystemVerilogUvmActivityAction::Cancelled, retained,
                   "removed");
}

void SystemVerilogUvmCallbackService::set_mask(
    const SystemVerilogUvmCallbackHandle handle, const std::uint64_t mask) {
  auto &selected = entry(handle);
  if (mutations_ >= limits_.maximum_mutations) {
    callback_fail(kCallbackResource, "UVM callback mutation ceiling exceeded");
  }
  selected.snapshot.mask = mask;
  ++mutations_;
  publish_activity(SystemVerilogUvmActivityAction::Updated, selected.snapshot,
                   "mask=" + std::to_string(mask));
}

SystemVerilogUvmCallbackDispatchResult
SystemVerilogUvmCallbackService::dispatch(
    const SystemVerilogClassHandle target, std::string callback_type,
    const std::uint64_t mask, std::string operation,
    SystemVerilogUvmCallbackAttributes attributes) {
  if (target == 0 || !objects_->contains(target) || callback_type.empty() ||
      mask == 0) {
    callback_fail(kInvalidCallback,
                  "invalid UVM callback dispatch target or mask");
  }
  SystemVerilogUvmCallbackInvocation invocation;
  invocation.target = target;
  invocation.root = root_of(target);
  invocation.target_type = objects_->type_name(target);
  invocation.callback_type = std::move(callback_type);
  invocation.operation = std::move(operation);
  invocation.mask = mask;
  invocation.depth = dispatch_depth_ + 1U;
  invocation.attributes = std::move(attributes);
  validate_invocation(invocation);
  if (dispatch_depth_ >= limits_.maximum_reentry_depth) {
    callback_fail(kCallbackResource, "UVM callback re-entry ceiling exceeded");
  }

  std::vector<SystemVerilogUvmCallbackHandle> frozen;
  frozen.reserve(
      std::min(order_.size(), limits_.maximum_callbacks_per_dispatch));
  for (const auto &handle : order_) {
    const auto found = callbacks_.find(handle.slot_);
    if (found == callbacks_.end() || found->second.snapshot.handle != handle ||
        found->second.snapshot.target_type != invocation.target_type ||
        found->second.snapshot.callback_type != invocation.callback_type ||
        (found->second.snapshot.instance != 0 &&
         found->second.snapshot.instance != target) ||
        (found->second.snapshot.mask & mask) == 0) {
      continue;
    }
    if (frozen.size() >= limits_.maximum_callbacks_per_dispatch) {
      callback_fail(kCallbackResource,
                    "UVM callback dispatch ceiling exceeded");
    }
    frozen.push_back(handle);
  }
  if (frozen.size() > limits_.maximum_mutations - mutations_) {
    callback_fail(kCallbackResource, "UVM callback mutation ceiling exceeded");
  }

  DepthGuard depth{dispatch_depth_};
  SystemVerilogUvmCallbackDispatchResult result;
  result.invocation = std::move(invocation);
  for (const auto &handle : frozen) {
    auto found = callbacks_.find(handle.slot_);
    if (found == callbacks_.end() || found->second.snapshot.handle != handle ||
        (found->second.snapshot.mask & mask) == 0) {
      continue;
    }
    if (mutations_ >= limits_.maximum_mutations) {
      callback_fail(kCallbackResource,
                    "UVM callback mutation ceiling exceeded");
    }
    const auto callback_snapshot = found->second.snapshot;
    const auto callback = found->second.callback;
    ++found->second.snapshot.invocations;
    ++mutations_;
    result.invoked.push_back(handle);
    const auto saved = result.invocation;
    publish_activity(SystemVerilogUvmActivityAction::Started, callback_snapshot,
                     result.invocation.operation);
    try {
      callback(result.invocation);
      validate_invocation(result.invocation);
      if (result.invocation.target != saved.target ||
          result.invocation.root != saved.root ||
          result.invocation.target_type != saved.target_type ||
          result.invocation.callback_type != saved.callback_type ||
          result.invocation.mask != saved.mask ||
          result.invocation.depth != saved.depth) {
        callback_fail(kInvalidCallback,
                      "UVM callbacks may not mutate dispatch routing identity");
      }
      publish_activity(SystemVerilogUvmActivityAction::Completed,
                       callback_snapshot, result.invocation.operation);
    } catch (const std::exception &error) {
      result.invocation = saved;
      ++result.contained_failures;
      if (failures_.size() < limits_.maximum_failures) {
        failures_.push_back({std::string{kInvalidCallback}, callback_snapshot,
                             saved, error.what()});
      }
      publish_activity(SystemVerilogUvmActivityAction::Failed,
                       callback_snapshot, error.what());
    } catch (...) {
      result.invocation = saved;
      ++result.contained_failures;
      if (failures_.size() < limits_.maximum_failures) {
        failures_.push_back({std::string{kInvalidCallback}, callback_snapshot,
                             saved, "unknown UVM callback failure"});
      }
      publish_activity(SystemVerilogUvmActivityAction::Failed,
                       callback_snapshot, "unknown UVM callback failure");
    }
  }
  return result;
}

SystemVerilogUvmCallbackSnapshot SystemVerilogUvmCallbackService::snapshot(
    const SystemVerilogUvmCallbackHandle handle) const {
  return entry(handle).snapshot;
}

std::vector<SystemVerilogUvmCallbackSnapshot>
SystemVerilogUvmCallbackService::snapshots() const {
  std::vector<SystemVerilogUvmCallbackSnapshot> result;
  result.reserve(order_.size());
  for (const auto &handle : order_) {
    const auto found = callbacks_.find(handle.slot_);
    if (found != callbacks_.end() && found->second.snapshot.handle == handle) {
      result.push_back(found->second.snapshot);
    }
  }
  return result;
}

SystemVerilogUvmCallbackService::Entry &SystemVerilogUvmCallbackService::entry(
    const SystemVerilogUvmCallbackHandle handle) {
  return const_cast<Entry &>(std::as_const(*this).entry(handle));
}

const SystemVerilogUvmCallbackService::Entry &
SystemVerilogUvmCallbackService::entry(
    const SystemVerilogUvmCallbackHandle handle) const {
  if (!handle || handle.owner_.get() != owner_.get()) {
    callback_fail(kInvalidCallback, "foreign or invalid UVM callback handle");
  }
  const auto found = callbacks_.find(handle.slot_);
  if (found == callbacks_.end() || found->second.snapshot.handle != handle) {
    callback_fail(kInvalidCallback, "stale UVM callback handle");
  }
  return found->second;
}

SystemVerilogUvmRootHandle SystemVerilogUvmCallbackService::root_of(
    const SystemVerilogClassHandle target) const {
  return components_->contains(target) ? components_->root_of(target) : 0;
}

void SystemVerilogUvmCallbackService::validate_invocation(
    const SystemVerilogUvmCallbackInvocation &invocation) const {
  if (invocation.target_type.size() > limits_.maximum_type_bytes ||
      invocation.callback_type.size() > limits_.maximum_type_bytes ||
      invocation.operation.size() > limits_.maximum_operation_bytes ||
      invocation.attributes.size() > limits_.maximum_attribute_count ||
      attribute_bytes(invocation.attributes) >
          limits_.maximum_attribute_bytes) {
    callback_fail(kCallbackResource, "UVM callback invocation exceeds limit");
  }
  for (const auto &[name, value] : invocation.attributes) {
    if (name.empty() || name.size() > limits_.maximum_name_bytes ||
        value.size() > limits_.maximum_attribute_bytes) {
      callback_fail(kInvalidCallback, "invalid UVM callback attribute");
    }
  }
}

void SystemVerilogUvmCallbackService::publish_activity(
    const SystemVerilogUvmActivityAction action,
    const SystemVerilogUvmCallbackSnapshot &callback,
    const std::string_view detail) noexcept {
  if (!activity_)
    return;
  try {
    activity_->publish({SystemVerilogUvmActivityKind::Callback, action,
                        callback.target_type + ":" + callback.callback_type +
                            ":" + callback.name,
                        std::string{detail},
                        callback.instance == 0 ? 0 : root_of(callback.instance),
                        callback.mask});
  } catch (...) {
    // Callback exception containment must not be defeated by an exhausted
    // optional observation surface.
  }
}

SystemVerilogUvmTransactionError::SystemVerilogUvmTransactionError(
    std::string code, std::string message)
    : std::runtime_error{std::move(message)}, code_(std::move(code)) {}

SystemVerilogUvmTransactionRecorderService::
    SystemVerilogUvmTransactionRecorderService(
        SystemVerilogUvmObjectService &objects,
        SystemVerilogUvmComponentService &components,
        SystemVerilogUvmCallbackService &callbacks,
        SystemVerilogUvmTransactionLimits limits)
    : objects_(&objects), components_(&components), callbacks_(&callbacks),
      limits_(limits) {
  if (limits_.maximum_transactions == 0 ||
      limits_.maximum_active_transactions == 0 ||
      limits_.maximum_attributes_per_transaction == 0 ||
      limits_.maximum_links_per_transaction == 0 ||
      limits_.maximum_trace_records == 0 || limits_.maximum_text_bytes == 0 ||
      limits_.maximum_attribute_bytes == 0 || limits_.maximum_mutations == 0) {
    transaction_fail(kTransactionResource,
                     "UVM transaction limits must be nonzero");
  }
}

SystemVerilogUvmTransactionHandle
SystemVerilogUvmTransactionRecorderService::begin(
    SystemVerilogUvmTransactionDescriptor descriptor) {
  validate_text(descriptor.name);
  validate_text(descriptor.stream);
  validate_text(descriptor.kind);
  validate_text(descriptor.callback_type);
  if (descriptor.name.empty() || descriptor.stream.empty() ||
      descriptor.kind.empty() || descriptor.callback_type.empty()) {
    transaction_fail(
        kInvalidTransaction,
        "UVM transaction begin requires name, stream, kind, and callback type");
  }
  if (descriptor.root != 0 && !components_->contains_root(descriptor.root)) {
    transaction_fail(kInvalidTransaction, "invalid UVM transaction root");
  }
  if (descriptor.object != 0 && !objects_->contains(descriptor.object)) {
    transaction_fail(kInvalidTransaction, "invalid UVM transaction object");
  }
  if (descriptor.object != 0 && components_->contains(descriptor.object) &&
      components_->root_of(descriptor.object) != descriptor.root) {
    transaction_fail(kInvalidTransaction,
                     "UVM transaction object and recorder root do not match");
  }
  std::optional<std::uint64_t> parent_identity;
  if (descriptor.parent) {
    const auto &parent = entry(*descriptor.parent).snapshot;
    if (parent.root != descriptor.root) {
      transaction_fail(kInvalidTransaction,
                       "UVM transaction parent belongs to a different root");
    }
    parent_identity = parent.identity;
  }
  if (descriptor.attributes.size() >
      limits_.maximum_attributes_per_transaction) {
    transaction_fail(kTransactionResource,
                     "UVM transaction attribute ceiling exceeded");
  }
  std::map<std::string, SystemVerilogUvmTransactionAttribute, std::less<>>
      unique_attributes;
  std::size_t bytes{};
  for (auto &attribute : descriptor.attributes) {
    validate_attribute(attribute);
    const auto text = attribute_value_text(attribute.value);
    if (attribute.name.size() > limits_.maximum_attribute_bytes - bytes ||
        text.size() >
            limits_.maximum_attribute_bytes - bytes - attribute.name.size()) {
      transaction_fail(kTransactionResource,
                       "UVM transaction attributes exceed byte limit");
    }
    bytes += attribute.name.size() + text.size();
    if (!unique_attributes.emplace(attribute.name, attribute).second) {
      transaction_fail(kInvalidTransaction,
                       "duplicate UVM transaction attribute");
    }
  }

  if (descriptor.object != 0) {
    SystemVerilogUvmCallbackAttributes callback_attributes;
    for (const auto &[name, attribute] : unique_attributes) {
      callback_attributes.emplace(name, attribute_value_text(attribute.value));
    }
    auto dispatched = callbacks_->dispatch(
        descriptor.object, descriptor.callback_type, UINT64_C(1), "begin",
        std::move(callback_attributes));
    auto original_attributes = std::move(unique_attributes);
    for (auto &[name, value] : dispatched.invocation.attributes) {
      const auto original = original_attributes.find(name);
      if (original != original_attributes.end() &&
          attribute_value_text(original->second.value) == value) {
        unique_attributes.emplace(name, std::move(original->second));
      } else {
        unique_attributes.emplace(
            name, SystemVerilogUvmTransactionAttribute{name, std::move(value)});
      }
    }
  }

  if (unique_attributes.size() > limits_.maximum_attributes_per_transaction) {
    transaction_fail(kTransactionResource,
                     "UVM transaction attribute ceiling exceeded");
  }
  bytes = 0;
  for (const auto &[name, attribute] : unique_attributes) {
    validate_attribute(attribute);
    const auto value = attribute_value_text(attribute.value);
    if (name.size() > limits_.maximum_attribute_bytes - bytes ||
        value.size() > limits_.maximum_attribute_bytes - bytes - name.size()) {
      transaction_fail(kTransactionResource,
                       "UVM transaction attributes exceed byte limit");
    }
    bytes += name.size() + value.size();
  }

  const auto active =
      std::ranges::count_if(transactions_, [](const auto &candidate) {
        return candidate.second.snapshot.state ==
               SystemVerilogUvmTransactionState::Active;
      });
  const auto required_traces = 1U + unique_attributes.size();
  if (transactions_.size() >= limits_.maximum_transactions ||
      static_cast<std::size_t>(active) >= limits_.maximum_active_transactions ||
      required_traces > limits_.maximum_trace_records - trace_records_.size() ||
      required_traces > limits_.maximum_mutations - mutations_ ||
      next_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_identity_ == std::numeric_limits<std::uint64_t>::max() ||
      required_traces >
          std::numeric_limits<std::uint64_t>::max() - next_trace_sequence_) {
    transaction_fail(kTransactionResource,
                     "UVM transaction begin ceiling exceeded");
  }

  const auto slot = next_slot_;
  const auto generation = generations_.try_emplace(slot, 1).first->second;
  const SystemVerilogUvmTransactionHandle handle{owner_, slot, generation};
  const auto [time, delta] = timestamp();
  SystemVerilogUvmTransactionSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.identity = next_identity_;
  snapshot.name = std::move(descriptor.name);
  snapshot.stream = std::move(descriptor.stream);
  snapshot.kind = std::move(descriptor.kind);
  snapshot.root = descriptor.root;
  snapshot.object = descriptor.object;
  snapshot.parent_identity = parent_identity;
  snapshot.begin_time = time;
  snapshot.begin_delta = delta;
  for (auto &[name, attribute] : unique_attributes) {
    snapshot.attributes.push_back(std::move(attribute));
  }
  auto [position, inserted] = transactions_.emplace(
      slot, Entry{std::move(snapshot), std::move(descriptor.callback_type)});
  if (!inserted) {
    transaction_fail(kTransactionResource,
                     "UVM transaction identity collision");
  }
  append_trace({0, SystemVerilogUvmTransactionTraceKind::Begin,
                position->second.snapshot.identity, parent_identity,
                position->second.snapshot.root, time, delta,
                position->second.snapshot.name,
                position->second.snapshot.stream + ":" +
                    position->second.snapshot.kind});
  for (const auto &attribute : position->second.snapshot.attributes) {
    append_trace({0, SystemVerilogUvmTransactionTraceKind::Attribute,
                  position->second.snapshot.identity, std::nullopt,
                  position->second.snapshot.root, time, delta, attribute.name,
                  attribute_value_text(attribute.value)});
  }
  mutations_ += required_traces;
  ++next_slot_;
  ++next_identity_;
  try {
    publish_activity(SystemVerilogUvmActivityAction::Started,
                     position->second.snapshot, "begin");
  } catch (...) {
    transactions_.erase(position);
    trace_records_.resize(trace_records_.size() - required_traces);
    next_trace_sequence_ -= required_traces;
    mutations_ -= required_traces;
    --next_slot_;
    --next_identity_;
    generations_.erase(slot);
    throw;
  }
  return handle;
}

void SystemVerilogUvmTransactionRecorderService::record_attribute(
    const SystemVerilogUvmTransactionHandle transaction,
    SystemVerilogUvmTransactionAttribute attribute) {
  validate_attribute(attribute);
  auto &selected = entry(transaction);
  if (selected.snapshot.state != SystemVerilogUvmTransactionState::Active) {
    transaction_fail(
        kInvalidTransaction,
        "attributes may only be recorded on an active UVM transaction");
  }
  auto found = std::ranges::find(selected.snapshot.attributes, attribute.name,
                                 &SystemVerilogUvmTransactionAttribute::name);
  if (found == selected.snapshot.attributes.end() &&
      selected.snapshot.attributes.size() >=
          limits_.maximum_attributes_per_transaction) {
    transaction_fail(kTransactionResource,
                     "UVM transaction attribute ceiling exceeded");
  }
  const auto value = attribute_value_text(attribute.value);
  if (trace_records_.size() >= limits_.maximum_trace_records ||
      mutations_ >= limits_.maximum_mutations) {
    transaction_fail(kTransactionResource,
                     "UVM transaction trace ceiling exceeded");
  }
  if (selected.snapshot.object != 0) {
    auto dispatched = callbacks_->dispatch(
        selected.snapshot.object, selected.callback_type, UINT64_C(4),
        "attribute", {{attribute.name, value}});
    const auto changed = dispatched.invocation.attributes.find(attribute.name);
    if (changed != dispatched.invocation.attributes.end() &&
        changed->second != value) {
      attribute.value = changed->second;
    }
  }
  auto &current = entry(transaction);
  if (current.snapshot.state != SystemVerilogUvmTransactionState::Active) {
    transaction_fail(kInvalidTransaction,
                     "UVM transaction changed state during attribute callback");
  }
  found = std::ranges::find(current.snapshot.attributes, attribute.name,
                            &SystemVerilogUvmTransactionAttribute::name);
  if ((found == current.snapshot.attributes.end() &&
       current.snapshot.attributes.size() >=
           limits_.maximum_attributes_per_transaction) ||
      trace_records_.size() >= limits_.maximum_trace_records ||
      mutations_ >= limits_.maximum_mutations) {
    transaction_fail(kTransactionResource,
                     "UVM transaction attribute or trace ceiling exceeded");
  }
  auto prospective_attributes = current.snapshot.attributes;
  auto prospective = std::ranges::find(
      prospective_attributes, attribute.name,
      &SystemVerilogUvmTransactionAttribute::name);
  if (prospective == prospective_attributes.end()) {
    prospective_attributes.push_back(attribute);
  } else {
    *prospective = attribute;
  }
  if (transaction_attribute_bytes(prospective_attributes) >
      limits_.maximum_attribute_bytes) {
    transaction_fail(
        kTransactionResource,
        "UVM transaction attributes exceed aggregate byte limit");
  }
  const auto previous_attributes = current.snapshot.attributes;
  current.snapshot.attributes = std::move(prospective_attributes);
  const auto [time, delta] = timestamp();
  append_trace({0, SystemVerilogUvmTransactionTraceKind::Attribute,
                current.snapshot.identity, std::nullopt, current.snapshot.root,
                time, delta, attribute.name,
                attribute_value_text(attribute.value)});
  ++mutations_;
  try {
    publish_activity(SystemVerilogUvmActivityAction::Updated, current.snapshot,
                     "attribute:" + attribute.name);
  } catch (...) {
    current.snapshot.attributes = previous_attributes;
    trace_records_.pop_back();
    --next_trace_sequence_;
    --mutations_;
    throw;
  }
}

void SystemVerilogUvmTransactionRecorderService::add_link(
    const SystemVerilogUvmTransactionHandle transaction,
    const SystemVerilogUvmTransactionHandle related, std::string relation) {
  validate_text(relation);
  if (relation.empty() || transaction == related) {
    transaction_fail(kInvalidTransaction, "invalid UVM transaction link");
  }
  auto &selected = entry(transaction);
  const auto &target = entry(related);
  if (selected.snapshot.state != SystemVerilogUvmTransactionState::Active ||
      selected.snapshot.root != target.snapshot.root ||
      std::ranges::any_of(selected.snapshot.links, [&](const auto &link) {
        return link.target_identity == target.snapshot.identity &&
               link.relation == relation;
      })) {
    transaction_fail(kInvalidTransaction,
                     "invalid, duplicate, or cross-root UVM transaction link");
  }
  if (selected.snapshot.links.size() >= limits_.maximum_links_per_transaction ||
      trace_records_.size() >= limits_.maximum_trace_records ||
      mutations_ >= limits_.maximum_mutations) {
    transaction_fail(kTransactionResource,
                     "UVM transaction link ceiling exceeded");
  }
  if (selected.snapshot.object != 0) {
    (void)callbacks_->dispatch(
        selected.snapshot.object, selected.callback_type, UINT64_C(8), "link",
        {{"relation", relation},
         {"target", std::to_string(target.snapshot.identity)}});
  }
  auto &current = entry(transaction);
  const auto &current_target = entry(related);
  if (current.snapshot.state != SystemVerilogUvmTransactionState::Active ||
      current.snapshot.root != current_target.snapshot.root ||
      std::ranges::any_of(current.snapshot.links, [&](const auto &link) {
        return link.target_identity == current_target.snapshot.identity &&
               link.relation == relation;
      })) {
    transaction_fail(kInvalidTransaction,
                     "UVM transaction changed during link callback");
  }
  if (current.snapshot.links.size() >=
          limits_.maximum_links_per_transaction ||
      trace_records_.size() >= limits_.maximum_trace_records ||
      mutations_ >= limits_.maximum_mutations) {
    transaction_fail(kTransactionResource,
                     "UVM transaction link ceiling exceeded");
  }
  current.snapshot.links.push_back(
      {current_target.snapshot.identity, relation});
  const auto [time, delta] = timestamp();
  append_trace({0, SystemVerilogUvmTransactionTraceKind::Link,
                current.snapshot.identity, current_target.snapshot.identity,
                current.snapshot.root, time, delta, std::move(relation),
                current_target.snapshot.name});
  ++mutations_;
  try {
    publish_activity(SystemVerilogUvmActivityAction::Bound, current.snapshot,
                     "link:" +
                         std::to_string(current_target.snapshot.identity));
  } catch (...) {
    current.snapshot.links.pop_back();
    trace_records_.pop_back();
    --next_trace_sequence_;
    --mutations_;
    throw;
  }
}

void SystemVerilogUvmTransactionRecorderService::end(
    const SystemVerilogUvmTransactionHandle transaction,
    const SystemVerilogUvmTransactionState state) {
  const auto action = activity_action(state);
  auto &selected = entry(transaction);
  if (selected.snapshot.state != SystemVerilogUvmTransactionState::Active) {
    transaction_fail(kInvalidTransaction, "UVM transaction is not active");
  }
  if (trace_records_.size() >= limits_.maximum_trace_records ||
      mutations_ >= limits_.maximum_mutations) {
    transaction_fail(kTransactionResource,
                     "UVM transaction end ceiling exceeded");
  }
  if (selected.snapshot.object != 0) {
    SystemVerilogUvmCallbackAttributes attributes;
    for (const auto &attribute : selected.snapshot.attributes) {
      attributes.emplace(attribute.name, attribute_value_text(attribute.value));
    }
    (void)callbacks_->dispatch(selected.snapshot.object, selected.callback_type,
                               UINT64_C(2), "end", std::move(attributes));
  }
  auto &current = entry(transaction);
  if (current.snapshot.state != SystemVerilogUvmTransactionState::Active) {
    transaction_fail(kInvalidTransaction,
                     "UVM transaction changed state during end callback");
  }
  if (trace_records_.size() >= limits_.maximum_trace_records ||
      mutations_ >= limits_.maximum_mutations) {
    transaction_fail(kTransactionResource,
                     "UVM transaction end ceiling exceeded");
  }
  const auto [time, delta] = timestamp();
  const auto previous_snapshot = current.snapshot;
  current.snapshot.state = state;
  current.snapshot.end_time = time;
  current.snapshot.end_delta = delta;
  append_trace({0, SystemVerilogUvmTransactionTraceKind::End,
                current.snapshot.identity, std::nullopt, current.snapshot.root,
                time, delta, current.snapshot.name,
                std::to_string(static_cast<std::uint8_t>(state))});
  ++mutations_;
  try {
    publish_activity(action, current.snapshot, "end");
  } catch (...) {
    current.snapshot = previous_snapshot;
    trace_records_.pop_back();
    --next_trace_sequence_;
    --mutations_;
    throw;
  }
}

void SystemVerilogUvmTransactionRecorderService::release(
    const SystemVerilogUvmTransactionHandle transaction) {
  const auto &selected = entry(transaction);
  if (selected.snapshot.state == SystemVerilogUvmTransactionState::Active) {
    transaction_fail(kInvalidTransaction,
                     "cannot release an active UVM transaction");
  }
  const auto referenced =
      std::ranges::any_of(transactions_, [&](const auto &candidate) {
        return candidate.first != transaction.slot_ &&
               (candidate.second.snapshot.parent_identity ==
                    std::optional{selected.snapshot.identity} ||
                std::ranges::any_of(
                    candidate.second.snapshot.links, [&](const auto &link) {
                      return link.target_identity == selected.snapshot.identity;
                    }));
      });
  if (referenced) {
    transaction_fail(kInvalidTransaction,
                     "cannot release a referenced UVM transaction record");
  }
  if (mutations_ >= limits_.maximum_mutations ||
      transaction.generation_ == std::numeric_limits<std::uint64_t>::max()) {
    transaction_fail(kTransactionResource,
                     "UVM transaction mutation ceiling exceeded");
  }
  transactions_.erase(transaction.slot_);
  ++generations_.at(transaction.slot_);
  ++mutations_;
}

SystemVerilogUvmTransactionSnapshot
SystemVerilogUvmTransactionRecorderService::snapshot(
    const SystemVerilogUvmTransactionHandle transaction) const {
  return entry(transaction).snapshot;
}

std::vector<SystemVerilogUvmTransactionSnapshot>
SystemVerilogUvmTransactionRecorderService::snapshots() const {
  std::vector<SystemVerilogUvmTransactionSnapshot> result;
  result.reserve(transactions_.size());
  for (const auto &[slot, selected] : transactions_) {
    (void)slot;
    result.push_back(selected.snapshot);
  }
  return result;
}

SystemVerilogUvmTransactionRecorderService::Entry &
SystemVerilogUvmTransactionRecorderService::entry(
    const SystemVerilogUvmTransactionHandle transaction) {
  return const_cast<Entry &>(std::as_const(*this).entry(transaction));
}

const SystemVerilogUvmTransactionRecorderService::Entry &
SystemVerilogUvmTransactionRecorderService::entry(
    const SystemVerilogUvmTransactionHandle transaction) const {
  if (!transaction || transaction.owner_.get() != owner_.get()) {
    transaction_fail(kInvalidTransaction,
                     "foreign or invalid UVM transaction handle");
  }
  const auto found = transactions_.find(transaction.slot_);
  if (found == transactions_.end() ||
      found->second.snapshot.handle != transaction) {
    transaction_fail(kInvalidTransaction, "stale UVM transaction handle");
  }
  return found->second;
}

std::pair<SimulationTick, std::uint64_t>
SystemVerilogUvmTransactionRecorderService::timestamp() const noexcept {
  return scheduler_ ? std::pair{scheduler_->now(), scheduler_->delta()}
                    : std::pair<SimulationTick, std::uint64_t>{};
}

void SystemVerilogUvmTransactionRecorderService::validate_text(
    const std::string_view text) const {
  if (text.size() > limits_.maximum_text_bytes) {
    transaction_fail(kTransactionResource,
                     "UVM transaction text exceeds limit");
  }
}

void SystemVerilogUvmTransactionRecorderService::validate_attribute(
    const SystemVerilogUvmTransactionAttribute &attribute) const {
  validate_text(attribute.name);
  if (attribute.name.empty()) {
    transaction_fail(kInvalidTransaction,
                     "UVM transaction attribute name is empty");
  }
  const auto value = attribute_value_text(attribute.value);
  if (value.size() > limits_.maximum_attribute_bytes) {
    transaction_fail(kTransactionResource,
                     "UVM transaction attribute exceeds limit");
  }
}

void SystemVerilogUvmTransactionRecorderService::append_trace(
    SystemVerilogUvmTransactionTraceRecord record) {
  validate_text(record.name);
  validate_text(record.value);
  if (trace_records_.size() >= limits_.maximum_trace_records ||
      next_trace_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    transaction_fail(kTransactionResource,
                     "UVM transaction trace ceiling exceeded");
  }
  record.sequence = next_trace_sequence_++;
  trace_records_.push_back(std::move(record));
}

void SystemVerilogUvmTransactionRecorderService::publish_activity(
    const SystemVerilogUvmActivityAction action,
    const SystemVerilogUvmTransactionSnapshot &transaction,
    std::string detail) {
  if (!activity_)
    return;
  activity_->publish({SystemVerilogUvmActivityKind::Transaction, action,
                      transaction.stream + ":" + transaction.name + "#" +
                          std::to_string(transaction.identity),
                      std::move(detail), transaction.root,
                      transaction.identity});
}

} // namespace fsim::runtime
