// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/tf_registration.hpp"
#include "fsim/runtime/tf_argument.hpp"
#include "fsim/runtime/tf_context.hpp"
#include "fsim/runtime/tf_control.hpp"
#include "fsim/runtime/tf_instance.hpp"
#include "fsim/runtime/tf_misc.hpp"
#include "fsim/runtime/tf_time.hpp"
#include "fsim/runtime/tf_synchronization.hpp"
#include "fsim/runtime/tf_value.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t kMaxTfFunctionWidth = 1U << 20U;

enum class TfFunctionResultKind {
  Integral,
  Real,
};

enum class TfCallError {
  None,
  InvalidRegistration,
  InvalidPointer,
  InvalidArguments,
  InvalidValues,
  InvalidInstance,
  InvalidTime,
  InvalidContext,
  ControlLimit,
  InvalidSynchronization,
  MissingMiscCallback,
  InvalidSize,
  CallbackException,
  ContextBusy,
  UnassignedResult,
  Allocation,
};

struct TfFunctionResult {
  TfFunctionResultKind kind{TfFunctionResultKind::Integral};
  std::uint32_t width{};
  std::vector<PLI_UINT32> aval_words;
  std::vector<PLI_UINT32> bval_words;
  double real{};
};

struct TfArgumentUpdate {
  PLI_INT32 parameter{};
  TfArgumentValue value;
};

struct TfInvokeResult {
  TfCallError error{TfCallError::None};
  PLI_INT32 callback_value{};
  std::optional<TfFunctionResult> function_result;
  std::vector<TfArgumentUpdate> argument_updates;
  std::vector<TfDelayRequest> delay_requests;
  std::vector<TfControlEffect> control_effects;
  std::vector<TfSynchronizationRequest> synchronization_requests;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfCallError::None;
  }
};

struct TfBindResult;
class TfLoadedPlugin;

class TfBoundCall final {
public:
  TfBoundCall(TfBoundCall&&) noexcept;
  TfBoundCall& operator=(TfBoundCall&&) noexcept;
  TfBoundCall(const TfBoundCall&) = delete;
  TfBoundCall& operator=(const TfBoundCall&) = delete;
  ~TfBoundCall();

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] TfRegistrationKind kind() const noexcept;
  [[nodiscard]] std::uint32_t result_width() const noexcept;
  [[nodiscard]] const TfInstanceIdentity& instance_identity() const noexcept;
  [[nodiscard]] const TfTimeProfile& time_profile() const noexcept;
  [[nodiscard]] const TfContextProfile& context_profile() const noexcept;
  [[nodiscard]] TfInvokeResult invoke() const noexcept;
  [[nodiscard]] TfInvokeResult invoke(TfTimeState time) const noexcept;
  [[nodiscard]] TfInvokeResult invoke(
      std::span<const TfArgumentValue> values) const noexcept;
  [[nodiscard]] TfInvokeResult invoke(
      std::span<const TfArgumentValue> values, TfTimeState time) const noexcept;
  [[nodiscard]] TfInvokeResult synchronize(
      TfSynchronizationKind kind) const noexcept;
  [[nodiscard]] TfInvokeResult synchronize(
      TfSynchronizationKind kind, TfTimeState time) const noexcept;
  [[nodiscard]] TfInvokeResult synchronize(
      TfSynchronizationKind kind,
      std::span<const TfArgumentValue> values) const noexcept;
  [[nodiscard]] TfInvokeResult synchronize(
      TfSynchronizationKind kind, std::span<const TfArgumentValue> values,
      TfTimeState time) const noexcept;
  [[nodiscard]] TfInvokeResult reactivate() const noexcept;
  [[nodiscard]] TfInvokeResult reactivate(TfTimeState time) const noexcept;
  [[nodiscard]] TfInvokeResult reactivate(
      std::span<const TfArgumentValue> values) const noexcept;
  [[nodiscard]] TfInvokeResult reactivate(
      std::span<const TfArgumentValue> values,
      TfTimeState time) const noexcept;

private:
  struct Impl;
  explicit TfBoundCall(std::unique_ptr<Impl> impl) noexcept;
  [[nodiscard]] TfInvokeResult invoke_owned(
      std::vector<TfArgumentValue> values, TfTimeState time,
      std::optional<TfMiscReason> misc_reason) const noexcept;
  std::unique_ptr<Impl> impl_;

  friend struct TfBindResult;
  friend class TfLoadedPlugin;
  friend TfBindResult bind_tf_call(
      const TfRegistration&, std::span<const TfArgument>,
      TfInstanceIdentity, TfTimeProfile, const TfContextProfile&) noexcept;
  friend TfBindResult bind_tf_call_with_owner(
      const TfRegistration&, std::span<const TfArgument>,
      std::shared_ptr<const void>, TfInstanceIdentity, TfTimeProfile,
      const TfContextProfile&) noexcept;
};

struct TfBindResult {
  std::unique_ptr<TfBoundCall> value;
  TfCallError error{TfCallError::None};
  PLI_INT32 callback_value{};
  std::vector<TfControlEffect> control_effects;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfCallError::None && value != nullptr;
  }
};

[[nodiscard]] TfBindResult bind_tf_call(
    const TfRegistration& registration,
    std::span<const TfArgument> arguments = {},
    TfInstanceIdentity instance = {},
    TfTimeProfile time_profile = {},
    const TfContextProfile& context_profile = {}) noexcept;

[[nodiscard]] TfBindResult bind_tf_call_with_owner(
    const TfRegistration& registration,
    std::span<const TfArgument> arguments,
    std::shared_ptr<const void> owner,
    TfInstanceIdentity instance = {},
    TfTimeProfile time_profile = {},
    const TfContextProfile& context_profile = {}) noexcept;

}  // namespace fsim::runtime
