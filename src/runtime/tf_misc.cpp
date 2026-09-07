// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_misc.hpp"

#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

struct MiscEntry {
  std::uint32_t registration_index{};
  PLI_INT32 user_data{};
  fsim_tf_misc_routine_v3 callback{};
};

[[nodiscard]] bool valid_reason(const TfMiscReason reason) noexcept {
  switch (reason) {
    case TfMiscReason::Save:
    case TfMiscReason::Restart:
    case TfMiscReason::Disable:
    case TfMiscReason::ParameterValueChange:
    case TfMiscReason::Synchronize:
    case TfMiscReason::Finish:
    case TfMiscReason::Reactivate:
    case TfMiscReason::ReadOnlySynchronize:
    case TfMiscReason::ParameterDelayChange:
    case TfMiscReason::EndOfCompile:
    case TfMiscReason::Scope:
    case TfMiscReason::Interactive:
    case TfMiscReason::Reset:
    case TfMiscReason::EndOfReset:
    case TfMiscReason::Force:
    case TfMiscReason::Release:
    case TfMiscReason::StartOfSave:
    case TfMiscReason::StartOfRestart:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_parameter(const TfMiscReason reason,
                                   const PLI_INT32 parameter) noexcept {
  switch (reason) {
    case TfMiscReason::ParameterValueChange:
    case TfMiscReason::ParameterDelayChange:
      return parameter > 0;
    default:
      return parameter == 0;
  }
}

}  // namespace

struct TfMiscDispatcher::Impl {
  std::vector<MiscEntry> entries;
  std::shared_ptr<const void> owner;
  bool dispatching{};
};

TfMiscDispatcher::TfMiscDispatcher(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

TfMiscDispatcher::TfMiscDispatcher(TfMiscDispatcher&&) noexcept = default;
TfMiscDispatcher& TfMiscDispatcher::operator=(TfMiscDispatcher&&) noexcept =
    default;
TfMiscDispatcher::~TfMiscDispatcher() = default;

std::uint32_t TfMiscDispatcher::callback_count() const noexcept {
  return static_cast<std::uint32_t>(impl_->entries.size());
}

TfMiscDispatchResult TfMiscDispatcher::dispatch(
    const TfMiscReason reason, const PLI_INT32 parameter) noexcept {
  if (!valid_reason(reason)) {
    return {.error = TfMiscError::InvalidReason,
            .last_callback_value = 0,
            .registration_index = 0,
            .callbacks_invoked = 0};
  }
  if (!valid_parameter(reason, parameter)) {
    return {.error = TfMiscError::InvalidParameter,
            .last_callback_value = 0,
            .registration_index = 0,
            .callbacks_invoked = 0};
  }
  if (impl_->dispatching) {
    return {.error = TfMiscError::Reentrant,
            .last_callback_value = 0,
            .registration_index = 0,
            .callbacks_invoked = 0};
  }

  struct DispatchGuard {
    bool& dispatching;
    ~DispatchGuard() { dispatching = false; }
  };
  impl_->dispatching = true;
  DispatchGuard guard{impl_->dispatching};

  std::uint32_t invoked{};
  PLI_INT32 last_value{};
  for (const auto& entry : impl_->entries) {
    try {
      last_value = entry.callback(entry.user_data,
                                  static_cast<PLI_INT32>(reason), parameter);
    } catch (...) {
      return {.error = TfMiscError::CallbackException,
              .last_callback_value = 0,
              .registration_index = entry.registration_index,
              .callbacks_invoked = invoked};
    }
    ++invoked;
  }
  return {.error = TfMiscError::None,
          .last_callback_value = last_value,
          .registration_index = 0,
          .callbacks_invoked = invoked};
}

TfMiscCreateResult make_tf_misc_dispatcher(
    const std::span<const TfRegistration> registrations,
    std::shared_ptr<const void> owner) noexcept {
  if (registrations.size() > FSIM_TF_MAX_REGISTRATIONS) {
    return {.value = nullptr, .error = TfMiscError::InvalidRegistration};
  }
  try {
    auto impl = std::make_unique<TfMiscDispatcher::Impl>();
    impl->entries.reserve(registrations.size());
    for (std::size_t index = 0; index < registrations.size(); ++index) {
      const auto& registration = registrations[index];
      if (registration.misctf != nullptr) {
        impl->entries.push_back(MiscEntry{
            .registration_index = static_cast<std::uint32_t>(index),
            .user_data = registration.user_data,
            .callback = registration.misctf,
        });
      }
    }
    impl->owner = std::move(owner);
    return {.value = std::unique_ptr<TfMiscDispatcher>{
                new TfMiscDispatcher{std::move(impl)}},
            .error = TfMiscError::None};
  } catch (const std::bad_alloc&) {
    return {.value = nullptr, .error = TfMiscError::Allocation};
  } catch (...) {
    return {.value = nullptr, .error = TfMiscError::Allocation};
  }
}

}  // namespace fsim::runtime
