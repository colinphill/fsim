// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_abi.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::runtime {

enum class VhdlVhpiErrorStateError {
  None,
  InvalidSimulation,
  InvalidSeverity,
  InvalidCode,
  InvalidMessage,
};

struct VhdlVhpiErrorRecord {
  fsim_vhpi_error_severity_v1 severity{FSIM_VHPI_ERROR_NOTE};
  std::string code;
  std::string message;
};

class VhdlVhpiErrorState final {
 public:
  explicit VhdlVhpiErrorState(std::uint64_t simulation_identity) noexcept;

  [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
  [[nodiscard]] VhdlVhpiErrorStateError publish(
      std::uint32_t severity,
      std::string_view code,
      std::string_view message);
  [[nodiscard]] std::optional<VhdlVhpiErrorRecord> check() const;
  void begin_call() noexcept;
  void clear() noexcept;

 private:
  std::uint64_t simulation_identity_{};
  mutable std::mutex mutex_;
  std::optional<VhdlVhpiErrorRecord> last_;
};

}  // namespace fsim::runtime
