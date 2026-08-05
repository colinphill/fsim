// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_methods.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

SystemVerilogClassMethodFrame::SystemVerilogClassMethodFrame(
    SystemVerilogClassMethodRuntime& runtime,
    const SystemVerilogClassHandle this_handle,
    std::string owner_type,
    std::vector<SystemVerilogClassMethodValue> arguments,
    const std::size_t automatic_value_count)
    : runtime_(&runtime),
      this_handle_(this_handle),
      owner_type_(std::move(owner_type)),
      arguments_(std::move(arguments)),
      locals_(automatic_value_count) {}

SystemVerilogClassPropertyValue&
SystemVerilogClassMethodFrame::property(const std::string_view name) {
  if (runtime_ == nullptr) {
    throw std::logic_error{"class method frame has no runtime"};
  }
  try {
    return runtime_->heap_->property(
        this_handle_, owner_type_ + "::" + std::string{name});
  } catch (const std::out_of_range&) {
    return runtime_->heap_->property(this_handle_, name);
  }
}

const SystemVerilogClassPropertyValue&
SystemVerilogClassMethodFrame::property(const std::string_view name) const {
  if (runtime_ == nullptr) {
    throw std::logic_error{"class method frame has no runtime"};
  }
  try {
    return std::as_const(*runtime_->heap_).property(
        this_handle_, owner_type_ + "::" + std::string{name});
  } catch (const std::out_of_range&) {
    return std::as_const(*runtime_->heap_).property(this_handle_, name);
  }
}

SystemVerilogClassPropertyValue&
SystemVerilogClassMethodFrame::static_property(const std::string_view name) {
  return static_property(owner_type_, name);
}

const SystemVerilogClassPropertyValue&
SystemVerilogClassMethodFrame::static_property(
    const std::string_view name) const {
  if (runtime_ == nullptr || runtime_->static_store_ == nullptr) {
    throw std::logic_error{"class method frame has no static-state store"};
  }
  return std::as_const(*runtime_->static_store_).property(owner_type_, name);
}

SystemVerilogClassPropertyValue&
SystemVerilogClassMethodFrame::static_property(
    const std::string_view specialization_or_alias,
    const std::string_view name) {
  if (runtime_ == nullptr || runtime_->static_store_ == nullptr) {
    throw std::logic_error{"class method frame has no static-state store"};
  }
  return runtime_->static_store_->property(specialization_or_alias, name);
}

SystemVerilogClassMethodValue& SystemVerilogClassMethodFrame::argument(
    const std::size_t index) {
  if (index >= arguments_.size()) {
    throw std::out_of_range{"class method argument index is out of range"};
  }
  return arguments_[index];
}

const SystemVerilogClassMethodValue& SystemVerilogClassMethodFrame::argument(
    const std::size_t index) const {
  if (index >= arguments_.size()) {
    throw std::out_of_range{"class method argument index is out of range"};
  }
  return arguments_[index];
}

SystemVerilogClassMethodValue& SystemVerilogClassMethodFrame::local(
    const std::size_t index) {
  if (index >= locals_.size()) {
    throw std::out_of_range{"class method local index is out of range"};
  }
  return locals_[index];
}

const SystemVerilogClassMethodValue& SystemVerilogClassMethodFrame::local(
    const std::size_t index) const {
  if (index >= locals_.size()) {
    throw std::out_of_range{"class method local index is out of range"};
  }
  return locals_[index];
}

SystemVerilogClassInvocationResult
SystemVerilogClassMethodFrame::call_nonvirtual(
    const std::string_view canonical_method,
    std::vector<SystemVerilogClassMethodValue>& actuals) {
  if (runtime_ == nullptr) {
    throw std::logic_error{"class method frame has no runtime"};
  }
  return runtime_->invoke(canonical_method, this_handle_, actuals);
}

SystemVerilogClassMethodRuntime::SystemVerilogClassMethodRuntime(
    SystemVerilogClassHeap& heap,
    const SystemVerilogClassMethodLimits limits,
    SystemVerilogClassStaticStore* static_store)
    : heap_(&heap), static_store_(static_store), limits_(limits) {}

void SystemVerilogClassMethodRuntime::register_method(
    SystemVerilogClassMethodDescriptor descriptor) {
  if (descriptor.canonical_identity.empty()
      || descriptor.owner_type.empty()
      || (!descriptor.entry && !descriptor.is_pure)) {
    throw std::invalid_argument{
        "class method registration requires identities and an entry"};
  }
  const auto identity = descriptor.canonical_identity;
  if (!methods_.emplace(identity, std::move(descriptor)).second) {
    throw std::invalid_argument{
        "duplicate class method registration '" + identity + "'"};
  }
  const auto& retained = methods_.at(identity);
  if (retained.virtual_slot) {
    register_virtual_override(
        retained.owner_type, *retained.virtual_slot, identity);
  }
}

bool SystemVerilogClassMethodRuntime::has_method(
    const std::string_view canonical_identity) const {
  return methods_.contains(std::string{canonical_identity});
}

SystemVerilogClassInvocationResult SystemVerilogClassMethodRuntime::invoke(
    const std::string_view canonical_identity,
    const SystemVerilogClassHandle this_handle,
    std::vector<SystemVerilogClassMethodValue>& actuals) {
  const auto found = methods_.find(std::string{canonical_identity});
  if (found == methods_.end()) {
    throw std::out_of_range{
        "class method '" + std::string{canonical_identity}
        + "' is not registered"};
  }
  const auto& descriptor = found->second;
  if (descriptor.is_pure || !descriptor.entry) {
    throw std::logic_error{
        "pure class method '" + descriptor.canonical_identity
        + "' cannot be called"};
  }
  if (descriptor.arguments.size() != actuals.size()) {
    throw std::invalid_argument{"class method actual count does not match"};
  }
  if (!descriptor.is_static) {
    (void)heap_->checked_cast(this_handle, descriptor.owner_type);
    if (this_handle == 0) {
      throw std::out_of_range{"instance class method called through null"};
    }
  } else if (this_handle != 0) {
    throw std::invalid_argument{"static class method received a this handle"};
  }
  if (descriptor.automatic_value_count
      > limits_.maximum_automatic_values) {
    throw std::length_error{"class method automatic-storage budget exceeded"};
  }
  std::vector<SystemVerilogClassMethodValue> arguments;
  arguments.reserve(actuals.size());
  for (std::size_t index = 0; index < actuals.size(); ++index) {
    if (descriptor.arguments[index]
        == SystemVerilogClassArgumentMode::Output) {
      arguments.emplace_back();
    } else {
      arguments.push_back(actuals[index]);
    }
  }
  return execute(
      descriptor,
      SystemVerilogClassMethodFrame{
          *this, this_handle, descriptor.owner_type, std::move(arguments),
          descriptor.automatic_value_count},
      actuals);
}

void SystemVerilogClassMethodRuntime::register_virtual_override(
    std::string dynamic_type,
    const std::uint32_t slot,
    std::string canonical_method) {
  const auto method = methods_.find(canonical_method);
  if (method == methods_.end()) {
    throw std::out_of_range{
        "virtual dispatch target '" + canonical_method
        + "' is not registered"};
  }
  if (!method->second.virtual_slot
      || *method->second.virtual_slot != slot) {
    throw std::invalid_argument{
        "virtual dispatch target has an incompatible slot"};
  }
  const auto key = std::pair{std::move(dynamic_type), slot};
  if (!virtual_dispatch_.emplace(key, std::move(canonical_method)).second) {
    throw std::invalid_argument{
        "duplicate virtual dispatch binding for dynamic type and slot"};
  }
}

SystemVerilogClassInvocationResult
SystemVerilogClassMethodRuntime::invoke_virtual(
    const std::uint32_t slot,
    const SystemVerilogClassHandle this_handle,
    std::vector<SystemVerilogClassMethodValue>& actuals) {
  if (this_handle == 0) {
    throw std::out_of_range{"virtual class method called through null"};
  }
  const auto& instance = heap_->object(this_handle);
  const auto binding = virtual_dispatch_.find(
      std::pair{instance.dynamic_type, slot});
  if (binding == virtual_dispatch_.end()) {
    throw std::out_of_range{
        "dynamic type '" + instance.dynamic_type
        + "' has no method in virtual slot " + std::to_string(slot)};
  }
  return invoke(binding->second, this_handle, actuals);
}

SystemVerilogClassInvocationResult SystemVerilogClassMethodRuntime::resume(
    const SystemVerilogClassInvocationHandle continuation,
    std::vector<SystemVerilogClassMethodValue>& actuals) {
  const auto pending = pending_.find(continuation);
  if (pending == pending_.end()) {
    throw std::out_of_range{"null or stale class method continuation"};
  }
  const auto method = methods_.find(pending->second.method);
  if (method == methods_.end()) {
    throw std::logic_error{"pending class method is no longer registered"};
  }
  if (method->second.arguments.size() != actuals.size()) {
    throw std::invalid_argument{"resumed class method actual count does not match"};
  }
  auto frame = std::move(pending->second.frame);
  pending_.erase(pending);
  return execute(
      method->second, std::move(frame), actuals, continuation);
}

std::vector<SystemVerilogClassInvocationSnapshot>
SystemVerilogClassMethodRuntime::pending_invocations() const {
  std::vector<SystemVerilogClassInvocationSnapshot> result;
  result.reserve(pending_.size());
  for (const auto& [continuation, invocation] : pending_) {
    result.push_back({
        continuation,
        invocation.method,
        invocation.frame.this_handle_,
        invocation.frame.continuation_point_,
        invocation.frame.arguments_,
        invocation.frame.locals_});
  }
  return result;
}

SystemVerilogClassInvocationResult SystemVerilogClassMethodRuntime::execute(
    const SystemVerilogClassMethodDescriptor& descriptor,
    SystemVerilogClassMethodFrame frame,
    std::vector<SystemVerilogClassMethodValue>& actuals,
    const SystemVerilogClassInvocationHandle existing_continuation) {
  if (call_depth_ >= limits_.maximum_call_depth) {
    throw std::length_error{"class method recursion depth exceeded"};
  }
  struct DepthGuard {
    std::size_t& depth;
    explicit DepthGuard(std::size_t& value) : depth(value) { ++depth; }
    ~DepthGuard() { --depth; }
  } guard{call_depth_};
  const auto status = descriptor.entry(frame);
  copy_out(descriptor, frame, actuals);
  if (status == SystemVerilogClassMethodStatus::Completed) {
    return {status, 0};
  }
  if (!descriptor.is_task) {
    throw std::logic_error{"a class function attempted to suspend"};
  }
  if (pending_.size() >= limits_.maximum_suspended_invocations) {
    throw std::length_error{"suspended class-method budget exceeded"};
  }
  auto continuation = existing_continuation;
  if (continuation == 0) {
    if (next_continuation_ == 0) {
      throw std::overflow_error{"class method continuation identity overflow"};
    }
    continuation = next_continuation_++;
  }
  const auto inserted = pending_.emplace(
      continuation,
      PendingInvocation{descriptor.canonical_identity, std::move(frame)});
  if (!inserted.second) {
    throw std::logic_error{"duplicate class method continuation identity"};
  }
  return {status, continuation};
}

void SystemVerilogClassMethodRuntime::copy_out(
    const SystemVerilogClassMethodDescriptor& descriptor,
    const SystemVerilogClassMethodFrame& frame,
    std::vector<SystemVerilogClassMethodValue>& actuals) {
  for (std::size_t index = 0; index < actuals.size(); ++index) {
    if (descriptor.arguments[index]
            != SystemVerilogClassArgumentMode::Input) {
      actuals[index] = frame.arguments_[index];
    }
  }
}

}  // namespace fsim::runtime
