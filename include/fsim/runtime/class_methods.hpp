// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/class_static.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

using SystemVerilogClassMethodValue = SystemVerilogClassPropertyValue;
using SystemVerilogClassInvocationHandle = std::uint64_t;

enum class SystemVerilogClassArgumentMode : std::uint8_t {
  Input,
  Output,
  Inout,
  Ref,
};

enum class SystemVerilogClassMethodStatus : std::uint8_t {
  Completed,
  Suspended,
};

struct SystemVerilogClassInvocationResult {
  SystemVerilogClassMethodStatus status{
      SystemVerilogClassMethodStatus::Completed};
  SystemVerilogClassInvocationHandle continuation{};
};

struct SystemVerilogClassInvocationSnapshot {
  SystemVerilogClassInvocationHandle continuation{};
  std::string canonical_method;
  SystemVerilogClassHandle this_handle{};
  std::size_t continuation_point{};
  std::vector<SystemVerilogClassMethodValue> arguments;
  std::vector<SystemVerilogClassMethodValue> locals;
};

class SystemVerilogClassMethodRuntime;

class SystemVerilogClassMethodFrame final {
 public:
  [[nodiscard]] SystemVerilogClassHandle this_handle() const noexcept {
    return this_handle_;
  }
  [[nodiscard]] SystemVerilogClassPropertyValue& property(
      std::string_view name);
  [[nodiscard]] const SystemVerilogClassPropertyValue& property(
      std::string_view name) const;
  [[nodiscard]] SystemVerilogClassPropertyValue& static_property(
      std::string_view name);
  [[nodiscard]] const SystemVerilogClassPropertyValue& static_property(
      std::string_view name) const;
  [[nodiscard]] SystemVerilogClassPropertyValue& static_property(
      std::string_view specialization_or_alias,
      std::string_view name);
  [[nodiscard]] SystemVerilogClassMethodValue& argument(std::size_t index);
  [[nodiscard]] const SystemVerilogClassMethodValue& argument(
      std::size_t index) const;
  [[nodiscard]] SystemVerilogClassMethodValue& local(std::size_t index);
  [[nodiscard]] const SystemVerilogClassMethodValue& local(
      std::size_t index) const;
  [[nodiscard]] std::size_t continuation_point() const noexcept {
    return continuation_point_;
  }
  void suspend_at(std::size_t continuation_point) noexcept {
    continuation_point_ = continuation_point;
  }
  [[nodiscard]] SystemVerilogClassInvocationResult call_nonvirtual(
      std::string_view canonical_method,
      std::vector<SystemVerilogClassMethodValue>& actuals);

 private:
  friend class SystemVerilogClassMethodRuntime;
  SystemVerilogClassMethodFrame(
      SystemVerilogClassMethodRuntime& runtime,
      SystemVerilogClassHandle this_handle,
      std::string owner_type,
      std::vector<SystemVerilogClassMethodValue> arguments,
      std::size_t automatic_value_count);

  SystemVerilogClassMethodRuntime* runtime_{};
  SystemVerilogClassHandle this_handle_{};
  std::string owner_type_;
  std::vector<SystemVerilogClassMethodValue> arguments_;
  std::vector<SystemVerilogClassMethodValue> locals_;
  std::size_t continuation_point_{};
};

struct SystemVerilogClassMethodDescriptor {
  std::string canonical_identity;
  std::string owner_type;
  std::vector<SystemVerilogClassArgumentMode> arguments;
  std::size_t automatic_value_count{};
  bool is_static{};
  bool is_task{};
  bool is_pure{};
  std::optional<std::uint32_t> virtual_slot;
  std::function<SystemVerilogClassMethodStatus(
      SystemVerilogClassMethodFrame&)> entry;
};

struct SystemVerilogClassMethodLimits {
  std::size_t maximum_call_depth{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_automatic_values{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_suspended_invocations{
      std::numeric_limits<std::size_t>::max()};
};

class SystemVerilogClassMethodRuntime final {
 public:
  explicit SystemVerilogClassMethodRuntime(
      SystemVerilogClassHeap& heap,
      SystemVerilogClassMethodLimits limits = {},
      SystemVerilogClassStaticStore* static_store = nullptr);

  void register_method(SystemVerilogClassMethodDescriptor descriptor);
  [[nodiscard]] bool has_method(std::string_view canonical_identity) const;
  [[nodiscard]] SystemVerilogClassInvocationResult invoke(
      std::string_view canonical_identity,
      SystemVerilogClassHandle this_handle,
      std::vector<SystemVerilogClassMethodValue>& actuals);
  void register_virtual_override(
      std::string dynamic_type,
      std::uint32_t slot,
      std::string canonical_method);
  [[nodiscard]] SystemVerilogClassInvocationResult invoke_virtual(
      std::uint32_t slot,
      SystemVerilogClassHandle this_handle,
      std::vector<SystemVerilogClassMethodValue>& actuals);
  [[nodiscard]] SystemVerilogClassInvocationResult resume(
      SystemVerilogClassInvocationHandle continuation,
      std::vector<SystemVerilogClassMethodValue>& actuals);
  [[nodiscard]] std::size_t suspended_invocations() const noexcept {
    return pending_.size();
  }
  /// Continuation-sorted value snapshots for debugger inspection. The
  /// continuation and class handles remain opaque generation-safe integers.
  [[nodiscard]] std::vector<SystemVerilogClassInvocationSnapshot>
  pending_invocations() const;

 private:
  friend class SystemVerilogClassMethodFrame;
  struct PendingInvocation {
    std::string method;
    SystemVerilogClassMethodFrame frame;
  };

  [[nodiscard]] SystemVerilogClassInvocationResult execute(
      const SystemVerilogClassMethodDescriptor& descriptor,
      SystemVerilogClassMethodFrame frame,
      std::vector<SystemVerilogClassMethodValue>& actuals,
      SystemVerilogClassInvocationHandle existing_continuation = 0);
  static void copy_out(
      const SystemVerilogClassMethodDescriptor& descriptor,
      const SystemVerilogClassMethodFrame& frame,
      std::vector<SystemVerilogClassMethodValue>& actuals);

  SystemVerilogClassHeap* heap_{};
  SystemVerilogClassStaticStore* static_store_{};
  SystemVerilogClassMethodLimits limits_;
  std::map<std::string, SystemVerilogClassMethodDescriptor> methods_;
  std::map<std::pair<std::string, std::uint32_t>, std::string>
      virtual_dispatch_;
  std::map<SystemVerilogClassInvocationHandle, PendingInvocation> pending_;
  SystemVerilogClassInvocationHandle next_continuation_{1};
  std::size_t call_depth_{};
};

}  // namespace fsim::runtime
