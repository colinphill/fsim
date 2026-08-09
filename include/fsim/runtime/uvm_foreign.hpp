// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_foreign_abi.h"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_callback.hpp"
#include "fsim/runtime/uvm_register_model.hpp"
#include "fsim/runtime/uvm_sequence.hpp"
#include "fsim/runtime/uvm_tlm1.hpp"
#include "fsim/runtime/uvm_tlm2.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime {

struct SystemVerilogUvmForeignLimits {
  std::size_t maximum_snapshots{16};
  std::size_t maximum_records{1U << 20U};
  std::size_t maximum_text_bytes{1U << 24U};
  std::size_t maximum_payload_bytes{1U << 26U};
  std::size_t maximum_callbacks{512};
};

struct SystemVerilogUvmForeignRecord {
  fsim_uvm_foreign_record_v1 value{};
  std::string identity;
  std::string detail;
  std::vector<std::uint8_t> payload;
};

class SystemVerilogUvmForeignService final {
 public:
  SystemVerilogUvmForeignService(
      SystemVerilogUvmPhaseService& phases,
      SystemVerilogUvmObjectionService& objections,
      SystemVerilogUvmTlm1Service& tlm1,
      SystemVerilogUvmTlm2Service& tlm2,
      SystemVerilogUvmActivityService& activity,
      SystemVerilogUvmForeignLimits limits = {});
  ~SystemVerilogUvmForeignService();
  SystemVerilogUvmForeignService(
      const SystemVerilogUvmForeignService&) = delete;
  SystemVerilogUvmForeignService& operator=(
      const SystemVerilogUvmForeignService&) = delete;

  void set_scheduler(Scheduler& scheduler) noexcept { scheduler_ = &scheduler; }
  void set_integrated_services(
      SystemVerilogUvmSequenceService& sequences,
      SystemVerilogUvmCallbackService& callbacks,
      SystemVerilogUvmTransactionRecorderService& transactions,
      SystemVerilogUvmRegisterModelService& register_model) noexcept;
  [[nodiscard]] fsim_uvm_foreign_status_v1 capture(
      fsim_uvm_foreign_snapshot_v1& result) noexcept;
  [[nodiscard]] fsim_uvm_foreign_status_v1 copy_record(
      const fsim_uvm_foreign_snapshot_v1& snapshot,
      std::size_t index,
      fsim_uvm_foreign_record_v1& result,
      std::span<char> identity,
      std::span<char> detail,
      std::span<std::uint8_t> payload) const noexcept;
  [[nodiscard]] fsim_uvm_foreign_status_v1 release(
      const fsim_uvm_foreign_snapshot_v1& snapshot) noexcept;
  [[nodiscard]] fsim_uvm_foreign_status_v1 add_callback(
      fsim_uvm_foreign_activity_callback_v1 callback,
      void* context,
      std::uint64_t& token) noexcept;
  [[nodiscard]] fsim_uvm_foreign_status_v1 remove_callback(
      std::uint64_t token) noexcept;

  [[nodiscard]] std::uint64_t simulation_identity() const noexcept {
    return simulation_identity_;
  }
  [[nodiscard]] std::size_t callback_count() const noexcept {
    return callbacks_.size();
  }
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return diagnostic_code_;
  }
  [[nodiscard]] std::string_view diagnostic_message() const noexcept {
    return diagnostic_message_;
  }

 private:
  struct Snapshot {
    fsim_uvm_foreign_snapshot_v1 value{};
    std::vector<SystemVerilogUvmForeignRecord> records;
  };
  void set_error(std::string code, std::string message) const;
  [[nodiscard]] std::vector<SystemVerilogUvmForeignRecord>
  integrated_records(std::size_t maximum_records,
                     std::size_t maximum_text_bytes) const;

  SystemVerilogUvmPhaseService* phases_{};
  SystemVerilogUvmObjectionService* objections_{};
  SystemVerilogUvmTlm1Service* tlm1_{};
  SystemVerilogUvmTlm2Service* tlm2_{};
  SystemVerilogUvmActivityService* activity_{};
  SystemVerilogUvmSequenceService* sequences_{};
  SystemVerilogUvmCallbackService* uvm_callbacks_{};
  SystemVerilogUvmTransactionRecorderService* transactions_{};
  SystemVerilogUvmRegisterModelService* register_model_{};
  Scheduler* scheduler_{};
  SystemVerilogUvmForeignLimits limits_;
  std::uint64_t simulation_identity_{};
  std::uint64_t next_generation_{1};
  std::uint64_t next_callback_{1};
  std::map<std::uint64_t, Snapshot> snapshots_;
  std::map<std::uint64_t, std::uint64_t> callbacks_;
  mutable std::string diagnostic_code_;
  mutable std::string diagnostic_message_;
};

[[nodiscard]] fsim_uvm_foreign_host_v1 make_systemverilog_uvm_foreign_host(
    SystemVerilogUvmForeignService& service) noexcept;

}  // namespace fsim::runtime
