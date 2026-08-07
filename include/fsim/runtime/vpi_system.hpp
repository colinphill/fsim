// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_object.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogVpiSystemCallableKind {
  Task,
  Function,
};

enum class SystemVerilogVpiSystemPhase {
  None,
  Compile,
  Size,
  Call,
};

enum class SystemVerilogVpiSystemError {
  None,
  InvalidRegistry,
  InvalidKind,
  InvalidName,
  InvalidProfile,
  DuplicateName,
  LateRegistration,
  NotFound,
  InvalidHandle,
  CrossRegistry,
  StaleHandle,
  InvalidScope,
  CrossSimulation,
  InvalidArguments,
  CompileRejected,
  SizeRejected,
  SizeMismatch,
  CallRejected,
  CallbackException,
  InvalidResult,
  ResultTypeMismatch,
  ResultAlreadyPublished,
  ResultNotPublished,
  ActiveCall,
  Closed,
  ResourceLimit,
};

struct SystemVerilogVpiSystemRegistrationHandle {
  std::uint64_t owner{};
  std::uint64_t id{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return owner != 0U && id != 0U;
  }

  friend bool operator==(
      const SystemVerilogVpiSystemRegistrationHandle&,
      const SystemVerilogVpiSystemRegistrationHandle&) = default;
};

struct SystemVerilogVpiSystemCallHandle {
  std::uint64_t owner{};
  std::uint64_t id{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return owner != 0U && id != 0U;
  }

  friend bool operator==(
      const SystemVerilogVpiSystemCallHandle&,
      const SystemVerilogVpiSystemCallHandle&) = default;
};

struct SystemVerilogVpiSystemArgumentHandle {
  std::uint64_t owner{};
  std::uint64_t call{};
  std::uint64_t ordinal{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return owner != 0U && call != 0U && ordinal != 0U;
  }

  friend bool operator==(
      const SystemVerilogVpiSystemArgumentHandle&,
      const SystemVerilogVpiSystemArgumentHandle&) = default;
};

enum class SystemVerilogVpiSystemCallState {
  Active,
  Completed,
  Failed,
};

struct SystemVerilogVpiSystemInvocation {
  SystemVerilogVpiSystemCallableKind kind{
      SystemVerilogVpiSystemCallableKind::Task};
  std::string_view name;
  std::optional<SystemVerilogVpiTypeInfo> return_type;
  SystemVerilogVpiObjectInfo scope;
  std::span<const SystemVerilogVpiStoredValue> arguments;
  std::span<const SystemVerilogVpiSystemArgumentHandle> argument_handles;
  SystemVerilogVpiSystemRegistrationHandle registration;
  SystemVerilogVpiSystemCallHandle call;
  std::uintptr_t registration_user_data{};
  std::uintptr_t call_user_data{};
};

struct SystemVerilogVpiSystemCallbackResult {
  bool accepted{true};
  std::string diagnostic;
};

struct SystemVerilogVpiSystemSizeResult {
  bool accepted{true};
  std::uint32_t width{};
  std::string diagnostic;
};

using SystemVerilogVpiSystemCompileTf = std::function<
    SystemVerilogVpiSystemCallbackResult(
        const SystemVerilogVpiSystemInvocation&)>;
using SystemVerilogVpiSystemSizeTf = std::function<
    SystemVerilogVpiSystemSizeResult(
        const SystemVerilogVpiSystemInvocation&)>;
using SystemVerilogVpiSystemCallTf = std::function<
    SystemVerilogVpiSystemCallbackResult(
        const SystemVerilogVpiSystemInvocation&)>;

struct SystemVerilogVpiSystemRegistration {
  SystemVerilogVpiSystemCallableKind kind{
      SystemVerilogVpiSystemCallableKind::Task};
  std::string name;
  std::optional<SystemVerilogVpiTypeInfo> return_type;
  SystemVerilogVpiSystemCompileTf compiletf;
  SystemVerilogVpiSystemSizeTf sizetf;
  SystemVerilogVpiSystemCallTf calltf;
  std::uintptr_t user_data{};
};

struct SystemVerilogVpiSystemRegisterResult {
  SystemVerilogVpiSystemRegistrationHandle value;
  SystemVerilogVpiSystemError error{
      SystemVerilogVpiSystemError::None};
  std::string diagnostic;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiSystemError::None
        && static_cast<bool>(value);
  }
};

struct SystemVerilogVpiSystemExecuteResult {
  SystemVerilogVpiSystemCallHandle call;
  SystemVerilogVpiSystemError error{
      SystemVerilogVpiSystemError::None};
  SystemVerilogVpiSystemPhase phase{
      SystemVerilogVpiSystemPhase::None};
  SystemVerilogVpiSystemCallableKind kind{
      SystemVerilogVpiSystemCallableKind::Task};
  std::optional<SystemVerilogVpiTypeInfo> return_type;
  std::optional<SystemVerilogVpiStoredValue> value;
  std::string diagnostic;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiSystemError::None;
  }
};

struct SystemVerilogVpiSystemArgumentResult {
  SystemVerilogVpiSystemArgumentHandle handle;
  SystemVerilogVpiSystemCallHandle call;
  std::size_t ordinal{};
  std::optional<SystemVerilogVpiStoredValue> value;
  SystemVerilogVpiSystemError error{
      SystemVerilogVpiSystemError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiSystemError::None
        && static_cast<bool>(handle) && value.has_value();
  }
};

struct SystemVerilogVpiSystemCallResult {
  SystemVerilogVpiSystemCallHandle handle;
  SystemVerilogVpiSystemRegistrationHandle registration;
  SystemVerilogVpiSystemCallState state{
      SystemVerilogVpiSystemCallState::Active};
  SystemVerilogVpiSystemCallableKind kind{
      SystemVerilogVpiSystemCallableKind::Task};
  fsim_vpi_handle_v1 scope{};
  std::size_t argument_count{};
  std::uintptr_t registration_user_data{};
  std::uintptr_t call_user_data{};
  std::optional<SystemVerilogVpiStoredValue> value;
  SystemVerilogVpiSystemError execution_error{
      SystemVerilogVpiSystemError::None};
  SystemVerilogVpiSystemError error{
      SystemVerilogVpiSystemError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiSystemError::None
        && static_cast<bool>(handle);
  }
};

class SystemVerilogVpiSystemRegistry final {
 public:
  explicit SystemVerilogVpiSystemRegistry(
      const SystemVerilogVpiObjectRegistry& objects) noexcept;
  ~SystemVerilogVpiSystemRegistry();

  SystemVerilogVpiSystemRegistry(
      const SystemVerilogVpiSystemRegistry&) = delete;
  SystemVerilogVpiSystemRegistry& operator=(
      const SystemVerilogVpiSystemRegistry&) = delete;
  SystemVerilogVpiSystemRegistry(
      SystemVerilogVpiSystemRegistry&&) = delete;
  SystemVerilogVpiSystemRegistry& operator=(
      SystemVerilogVpiSystemRegistry&&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
  [[nodiscard]] SystemVerilogVpiSystemRegisterResult register_callable(
      SystemVerilogVpiSystemRegistration registration);
  [[nodiscard]] SystemVerilogVpiSystemExecuteResult execute(
      std::string_view name,
      fsim_vpi_handle_v1 scope,
      std::vector<SystemVerilogVpiStoredValue> arguments = {},
      std::uintptr_t call_user_data = 0);
  [[nodiscard]] SystemVerilogVpiSystemError publish_result(
      SystemVerilogVpiSystemCallHandle call,
      SystemVerilogVpiStoredValue value);
  [[nodiscard]] SystemVerilogVpiSystemArgumentResult argument(
      SystemVerilogVpiSystemCallHandle call, std::size_t ordinal) const;
  [[nodiscard]] SystemVerilogVpiSystemArgumentResult argument(
      SystemVerilogVpiSystemArgumentHandle argument) const;
  [[nodiscard]] SystemVerilogVpiSystemCallResult call(
      SystemVerilogVpiSystemCallHandle call) const;
  [[nodiscard]] SystemVerilogVpiSystemError release_call(
      SystemVerilogVpiSystemCallHandle call);
  [[nodiscard]] SystemVerilogVpiSystemError unregister_callable(
      SystemVerilogVpiSystemRegistrationHandle registration);
  [[nodiscard]] SystemVerilogVpiSystemError seal_registrations();
  [[nodiscard]] bool registrations_sealed() const;
  void teardown() noexcept;
  [[nodiscard]] std::size_t registrations() const;
  [[nodiscard]] std::size_t calls() const;

  struct Entry;
  struct CallRecord;

 private:
  const SystemVerilogVpiObjectRegistry* objects_{};
  std::uint64_t owner_{};
  std::uint64_t next_registration_{1};
  std::uint64_t next_call_{1};
  bool sealed_{};
  bool closed_{};
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<Entry>>
      registrations_;
  std::map<std::uint64_t, std::shared_ptr<Entry>> registration_handles_;
  std::map<std::uint64_t, std::shared_ptr<CallRecord>> calls_;
};

}  // namespace fsim::runtime
