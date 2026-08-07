// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/vhpi_type.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiForeignKind : std::uint32_t {
  Subprogram,
  Model,
};

enum class VhdlVhpiParameterMode : std::uint32_t {
  In,
  Out,
  InOut,
};

enum class VhdlVhpiForeignState : std::uint32_t {
  Registered,
  Started,
  Stopped,
  Unregistered,
};

enum class VhdlVhpiCallState : std::uint32_t {
  Active,
  Completed,
  Failed,
};

enum class VhdlVhpiForeignError {
  None,
  InvalidSimulation,
  InvalidProfile,
  InvalidType,
  InvalidScope,
  InvalidArguments,
  InvalidHandle,
  CrossSimulation,
  Duplicate,
  NotFound,
  InvalidState,
  ActiveCall,
  ResultNotAllowed,
  ResultTypeMismatch,
  ResultAlreadyPublished,
  ResultMissing,
  CallbackFailed,
  Unregistered,
  Closed,
  ResourceLimit,
};

struct VhdlVhpiForeignValue {
  fsim_vhpi_handle_v1 type{};
  PackedLogic9 value{0};

  friend bool operator==(
      const VhdlVhpiForeignValue&,
      const VhdlVhpiForeignValue&) = default;
};

struct VhdlVhpiForeignParameter {
  std::string name;
  fsim_vhpi_handle_v1 type{};
  VhdlVhpiParameterMode mode{VhdlVhpiParameterMode::In};
};

struct VhdlVhpiForeignDescriptor {
  std::uint64_t identity{};
  VhdlVhpiForeignKind kind{VhdlVhpiForeignKind::Subprogram};
  std::string name;
  std::vector<VhdlVhpiForeignParameter> parameters;
  fsim_vhpi_handle_v1 result_type{};
  std::uint64_t ordinal{};
  std::uint64_t user_data{};
  VhdlVhpiForeignState state{VhdlVhpiForeignState::Registered};
  std::uint64_t invocations{};
  std::uint32_t active_calls{};
};

struct VhdlVhpiForeignInvocation {
  std::uint64_t registration{};
  std::uint64_t call{};
  fsim_vhpi_handle_v1 scope{};
  std::vector<std::uint64_t> arguments;
  std::uint64_t registration_user_data{};
  std::uint64_t call_user_data{};
};

using VhdlVhpiForeignLifecycle =
    std::function<void(const VhdlVhpiForeignDescriptor&)>;
using VhdlVhpiForeignCallback =
    std::function<void(const VhdlVhpiForeignInvocation&)>;

struct VhdlVhpiForeignRegistration {
  VhdlVhpiForeignKind kind{VhdlVhpiForeignKind::Subprogram};
  std::string name;
  std::vector<VhdlVhpiForeignParameter> parameters;
  fsim_vhpi_handle_v1 result_type{};
  std::uint64_t user_data{};
  VhdlVhpiForeignLifecycle start;
  VhdlVhpiForeignCallback invoke;
  VhdlVhpiForeignLifecycle stop;
};

struct VhdlVhpiCallDescriptor {
  std::uint64_t identity{};
  std::uint64_t registration{};
  fsim_vhpi_handle_v1 scope{};
  VhdlVhpiCallState state{VhdlVhpiCallState::Active};
  std::vector<std::uint64_t> arguments;
  std::uint64_t result{};
  std::uint64_t user_data{};
  VhdlVhpiForeignError execution_error{VhdlVhpiForeignError::None};
};

template <typename T>
struct VhdlVhpiForeignResult {
  T value;
  VhdlVhpiForeignError error{VhdlVhpiForeignError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiForeignError::None;
  }
};

using VhdlVhpiForeignRegistrationResult =
    VhdlVhpiForeignResult<VhdlVhpiForeignDescriptor>;
using VhdlVhpiForeignDescriptorResult =
    VhdlVhpiForeignResult<VhdlVhpiForeignDescriptor>;
using VhdlVhpiForeignCallResult =
    VhdlVhpiForeignResult<VhdlVhpiCallDescriptor>;
using VhdlVhpiForeignValueResult =
    VhdlVhpiForeignResult<VhdlVhpiForeignValue>;

class VhdlVhpiForeignSystem final {
 public:
  VhdlVhpiForeignSystem(
      VhdlVhpiObjectRegistry& objects,
      VhdlVhpiTypeSystem& types) noexcept;
  ~VhdlVhpiForeignSystem();

  VhdlVhpiForeignSystem(const VhdlVhpiForeignSystem&) = delete;
  VhdlVhpiForeignSystem& operator=(
      const VhdlVhpiForeignSystem&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] VhdlVhpiForeignRegistrationResult register_foreign(
      VhdlVhpiForeignRegistration registration);
  [[nodiscard]] VhdlVhpiForeignDescriptorResult registration(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiForeignError start(std::uint64_t identity);
  [[nodiscard]] VhdlVhpiForeignError stop(std::uint64_t identity);
  [[nodiscard]] VhdlVhpiForeignCallResult invoke(
      std::uint64_t registration,
      fsim_vhpi_handle_v1 scope,
      std::vector<VhdlVhpiForeignValue> arguments,
      std::uint64_t user_data = 0);
  [[nodiscard]] VhdlVhpiForeignError publish_result(
      std::uint64_t call, VhdlVhpiForeignValue result);
  [[nodiscard]] VhdlVhpiForeignCallResult call(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiForeignValueResult argument(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiForeignValueResult result(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiForeignError release_call(
      std::uint64_t identity);
  [[nodiscard]] VhdlVhpiForeignError unregister_foreign(
      std::uint64_t identity);
  void teardown() noexcept;

 private:
  struct RegistrationEntry {
    VhdlVhpiForeignDescriptor descriptor;
    VhdlVhpiForeignLifecycle start;
    VhdlVhpiForeignCallback invoke;
    VhdlVhpiForeignLifecycle stop;
  };
  struct CallEntry {
    VhdlVhpiCallDescriptor descriptor;
    std::vector<VhdlVhpiForeignValue> values;
    std::optional<VhdlVhpiForeignValue> result;
  };
  struct ValueEntry {
    std::uint64_t call{};
    VhdlVhpiForeignValue value;
  };

  [[nodiscard]] static bool valid_name(
      std::string_view name) noexcept;
  [[nodiscard]] static std::string canonical_name(std::string_view name);
  [[nodiscard]] std::uint64_t make_identity(
      std::uint8_t kind, std::uint32_t local) const noexcept;
  [[nodiscard]] VhdlVhpiForeignError validate_identity(
      std::uint64_t identity, std::uint8_t kind) const noexcept;
  [[nodiscard]] VhdlVhpiForeignError validate_profile(
      const VhdlVhpiForeignRegistration& registration) const;

  VhdlVhpiObjectRegistry* objects_{};
  VhdlVhpiTypeSystem* types_{};
  std::uint32_t system_identity_{};
  std::uint32_t next_registration_{1};
  std::uint32_t next_call_{1};
  std::uint32_t next_argument_{1};
  std::uint32_t next_result_{1};
  std::uint64_t next_ordinal_{};
  bool closed_{};
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, RegistrationEntry> registrations_;
  std::unordered_map<std::string, std::uint64_t> names_;
  std::unordered_map<std::uint64_t, CallEntry> calls_;
  std::unordered_map<std::uint64_t, ValueEntry> arguments_;
  std::unordered_map<std::uint64_t, ValueEntry> results_;
};

}  // namespace fsim::runtime
