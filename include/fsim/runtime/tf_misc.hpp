// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/tf_registration.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace fsim::runtime {

enum class TfMiscReason : PLI_INT32 {
  Save = reason_save,
  Restart = reason_restart,
  Disable = reason_disable,
  ParameterValueChange = reason_paramvc,
  Synchronize = reason_synch,
  Finish = reason_finish,
  Reactivate = reason_reactivate,
  ReadOnlySynchronize = reason_rosynch,
  ParameterDelayChange = reason_paramdrc,
  EndOfCompile = reason_endofcompile,
  Scope = reason_scope,
  Interactive = reason_interactive,
  Reset = reason_reset,
  EndOfReset = reason_endofreset,
  Force = reason_force,
  Release = reason_release,
  StartOfSave = reason_startofsave,
  StartOfRestart = reason_startofrestart,
};

enum class TfMiscError {
  None,
  InvalidRegistration,
  InvalidReason,
  InvalidParameter,
  CallbackException,
  Reentrant,
  Allocation,
};

struct TfMiscDispatchResult {
  TfMiscError error{TfMiscError::None};
  PLI_INT32 last_callback_value{};
  std::uint32_t registration_index{};
  std::uint32_t callbacks_invoked{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfMiscError::None;
  }
};

struct TfMiscCreateResult;

class TfMiscDispatcher final {
public:
  TfMiscDispatcher(TfMiscDispatcher&&) noexcept;
  TfMiscDispatcher& operator=(TfMiscDispatcher&&) noexcept;
  TfMiscDispatcher(const TfMiscDispatcher&) = delete;
  TfMiscDispatcher& operator=(const TfMiscDispatcher&) = delete;
  ~TfMiscDispatcher();

  [[nodiscard]] std::uint32_t callback_count() const noexcept;
  [[nodiscard]] TfMiscDispatchResult dispatch(
      TfMiscReason reason, PLI_INT32 parameter = 0) noexcept;

private:
  struct Impl;
  explicit TfMiscDispatcher(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;

  friend struct TfMiscCreateResult;
  friend TfMiscCreateResult make_tf_misc_dispatcher(
      std::span<const TfRegistration>, std::shared_ptr<const void>) noexcept;
};

struct TfMiscCreateResult {
  std::unique_ptr<TfMiscDispatcher> value;
  TfMiscError error{TfMiscError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfMiscError::None && value != nullptr;
  }
};

[[nodiscard]] TfMiscCreateResult make_tf_misc_dispatcher(
    std::span<const TfRegistration> registrations,
    std::shared_ptr<const void> owner = {}) noexcept;

}  // namespace fsim::runtime
