// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_abi.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::runtime {

enum class SystemVerilogVpiErrorStateError {
  None,
  InvalidSimulation,
  InvalidSeverity,
  InvalidCode,
  InvalidMessage,
};

struct SystemVerilogVpiErrorRecord {
  fsim_vpi_error_severity_v1 severity{FSIM_VPI_ERROR_NOTICE};
  std::string code;
  std::string message;
};

class SystemVerilogVpiErrorState final {
 public:
  explicit SystemVerilogVpiErrorState(
      std::uint64_t simulation_identity) noexcept;

  [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
  [[nodiscard]] SystemVerilogVpiErrorStateError publish(
      std::uint32_t severity,
      std::string_view code,
      std::string_view message);
  [[nodiscard]] std::optional<SystemVerilogVpiErrorRecord> check() const;
  void begin_call() noexcept;
  void clear() noexcept;

 private:
  std::uint64_t simulation_identity_{};
  mutable std::mutex mutex_;
  std::optional<SystemVerilogVpiErrorRecord> last_;
};

}  // namespace fsim::runtime
