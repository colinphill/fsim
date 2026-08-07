// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/vhpi_type.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiDelayMode : std::uint32_t {
  Inertial,
  Transport,
};

enum class VhdlVhpiDriverError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  InvalidSignal,
  InvalidSource,
  InvalidDriver,
  InvalidTransaction,
  InvalidOperation,
  InvalidOwner,
  AlreadyCompleted,
  InvalidType,
  InvalidValue,
  InvalidWaveform,
  InvalidRejection,
  MultipleDrivers,
  BufferTooSmall,
  ResourceLimit,
};

enum class VhdlVhpiWriteKind : std::uint32_t {
  Deposit,
  Force,
  Release,
};

struct VhdlVhpiWaveformElement {
  PackedLogic9 value{0};
  SimulationTick delay{};
};

struct VhdlVhpiDriverDescriptor {
  std::uint64_t identity{};
  std::uint64_t source_identity{};
  fsim_vhpi_handle_v1 signal{};
  fsim_vhpi_handle_v1 source{};
  std::uint64_t ordinal{};
  PackedLogic9 value{0};
};

struct VhdlVhpiSourceDescriptor {
  std::uint64_t identity{};
  std::uint64_t driver{};
  fsim_vhpi_handle_v1 signal{};
  fsim_vhpi_handle_v1 object{};
  std::uint64_t ordinal{};
};

struct VhdlVhpiTransactionDescriptor {
  std::uint64_t identity{};
  std::uint64_t driver{};
  fsim_vhpi_handle_v1 signal{};
  PackedLogic9 value{0};
  SimulationTick time{};
  SimulationTick rejection{};
  VhdlVhpiDelayMode mode{VhdlVhpiDelayMode::Inertial};
  std::uint32_t waveform_index{};
};

struct VhdlVhpiWriteDescriptor {
  std::uint64_t identity{};
  fsim_vhpi_handle_v1 signal{};
  fsim_vhpi_handle_v1 owner{};
  VhdlVhpiWriteKind kind{VhdlVhpiWriteKind::Deposit};
  std::uint64_t target_force{};
  PackedLogic9 value{0};
  SimulationTick time{};
  bool pending{};
  bool active{};
  bool canceled{};
  bool completed{};
};

template <typename T>
struct VhdlVhpiDriverResult {
  T value;
  VhdlVhpiDriverError error{VhdlVhpiDriverError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiDriverError::None;
  }
};

using VhdlVhpiDriverDescriptorResult =
    VhdlVhpiDriverResult<VhdlVhpiDriverDescriptor>;
using VhdlVhpiSourceListResult =
    VhdlVhpiDriverResult<std::vector<VhdlVhpiSourceDescriptor>>;
using VhdlVhpiDriverListResult =
    VhdlVhpiDriverResult<std::vector<VhdlVhpiDriverDescriptor>>;
using VhdlVhpiTransactionListResult =
    VhdlVhpiDriverResult<std::vector<VhdlVhpiTransactionDescriptor>>;
using VhdlVhpiDriverLogicResult =
    VhdlVhpiDriverResult<PackedLogic9>;
using VhdlVhpiWriteResult =
    VhdlVhpiDriverResult<VhdlVhpiWriteDescriptor>;

class VhdlVhpiDriverSystem final {
 public:
  VhdlVhpiDriverSystem(
      VhdlVhpiObjectRegistry& objects,
      VhdlVhpiTypeSystem& types,
      Scheduler& scheduler) noexcept;
  ~VhdlVhpiDriverSystem();

  VhdlVhpiDriverSystem(const VhdlVhpiDriverSystem&) = delete;
  VhdlVhpiDriverSystem& operator=(const VhdlVhpiDriverSystem&) = delete;

  [[nodiscard]] VhdlVhpiDriverDescriptorResult create_driver(
      fsim_vhpi_handle_v1 signal,
      fsim_vhpi_handle_v1 source,
      const PackedLogic9& initial);
  [[nodiscard]] VhdlVhpiDriverDescriptorResult driver(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiDriverLogicResult signal_value(
      fsim_vhpi_handle_v1 signal) const;
  [[nodiscard]] VhdlVhpiDriverListResult drivers(
      fsim_vhpi_handle_v1 signal) const;
  [[nodiscard]] VhdlVhpiSourceListResult sources(
      fsim_vhpi_handle_v1 signal) const;
  [[nodiscard]] VhdlVhpiTransactionListResult transactions(
      std::uint64_t driver) const;
  [[nodiscard]] VhdlVhpiTransactionListResult schedule_waveform(
      std::uint64_t driver,
      std::span<const VhdlVhpiWaveformElement> elements,
      SimulationTick rejection,
      VhdlVhpiDelayMode mode);
  [[nodiscard]] VhdlVhpiDriverError cancel_transaction(
      std::uint64_t transaction,
      fsim_vhpi_handle_v1 owner);
  [[nodiscard]] VhdlVhpiWriteResult deposit(
      fsim_vhpi_handle_v1 signal,
      fsim_vhpi_handle_v1 owner,
      const PackedLogic9& value,
      SimulationTick delay = 0);
  [[nodiscard]] VhdlVhpiWriteResult force(
      fsim_vhpi_handle_v1 signal,
      fsim_vhpi_handle_v1 owner,
      const PackedLogic9& value,
      SimulationTick delay = 0);
  [[nodiscard]] VhdlVhpiWriteResult release(
      std::uint64_t force,
      fsim_vhpi_handle_v1 owner,
      SimulationTick delay = 0);
  [[nodiscard]] VhdlVhpiWriteResult operation(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiDriverError cancel_operation(
      std::uint64_t identity,
      fsim_vhpi_handle_v1 owner);

 private:
  struct TransactionEntry {
    VhdlVhpiTransactionDescriptor descriptor;
    ScheduledTaskHandle task;
  };
  struct DriverEntry {
    VhdlVhpiDriverDescriptor descriptor;
    std::vector<TransactionEntry> transactions;
  };
  struct SignalEntry {
    fsim_vhpi_handle_v1 type{};
    bool resolved{};
    std::size_t width{};
    PackedLogic9 resolved_value{0};
    PackedLogic9 value{0};
    std::vector<std::uint64_t> drivers;
    std::vector<std::uint64_t> forces;
  };
  struct WriteEntry {
    VhdlVhpiWriteDescriptor descriptor;
    ScheduledTaskHandle task;
  };

  [[nodiscard]] static VhdlVhpiDriverError object_error(
      VhdlVhpiObjectError error) noexcept;
  [[nodiscard]] std::uint64_t make_identity(
      std::uint8_t kind, std::uint32_t local) const noexcept;
  [[nodiscard]] VhdlVhpiDriverError validate_identity(
      std::uint64_t identity, std::uint8_t kind) const noexcept;
  void resolve_signal_locked(fsim_vhpi_handle_v1 signal);
  void commit_transaction(
      std::uint64_t driver, std::uint64_t transaction);
  [[nodiscard]] VhdlVhpiWriteResult prepare_write(
      fsim_vhpi_handle_v1 signal,
      fsim_vhpi_handle_v1 owner,
      VhdlVhpiWriteKind kind,
      std::uint64_t target_force,
      const PackedLogic9& value,
      SimulationTick delay);
  void commit_write(std::uint64_t identity);
  void refresh_visible_locked(fsim_vhpi_handle_v1 signal);

  VhdlVhpiObjectRegistry* objects_{};
  VhdlVhpiTypeSystem* types_{};
  Scheduler* scheduler_{};
  std::uint32_t system_identity_{};
  std::uint32_t next_driver_{1};
  std::uint32_t next_source_{1};
  std::uint32_t next_transaction_{1};
  std::uint32_t next_operation_{1};
  std::uint64_t next_ordinal_{};
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, DriverEntry> drivers_;
  std::unordered_map<fsim_vhpi_handle_v1, SignalEntry> signals_;
  std::unordered_map<std::uint64_t, WriteEntry> operations_;
};

}  // namespace fsim::runtime
