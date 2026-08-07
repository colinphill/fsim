// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_error.hpp"

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_code_size = 256;
constexpr std::size_t maximum_message_size = 1U << 20U;

}  // namespace

SystemVerilogVpiErrorState::SystemVerilogVpiErrorState(
    const std::uint64_t simulation_identity) noexcept
    : simulation_identity_(simulation_identity) {}

std::uint64_t SystemVerilogVpiErrorState::simulation_identity()
    const noexcept {
  return simulation_identity_;
}

SystemVerilogVpiErrorStateError SystemVerilogVpiErrorState::publish(
    const std::uint32_t severity,
    const std::string_view code,
    const std::string_view message) {
  if (simulation_identity_ == 0U) {
    return SystemVerilogVpiErrorStateError::InvalidSimulation;
  }
  if (severity > static_cast<std::uint32_t>(FSIM_VPI_ERROR_INTERNAL)) {
    return SystemVerilogVpiErrorStateError::InvalidSeverity;
  }
  if (code.empty() || code.size() > maximum_code_size
      || code.find('\0') != std::string_view::npos) {
    return SystemVerilogVpiErrorStateError::InvalidCode;
  }
  if (message.empty() || message.size() > maximum_message_size
      || message.find('\0') != std::string_view::npos) {
    return SystemVerilogVpiErrorStateError::InvalidMessage;
  }
  std::scoped_lock lock{mutex_};
  last_ = SystemVerilogVpiErrorRecord{
      static_cast<fsim_vpi_error_severity_v1>(severity),
      std::string{code},
      std::string{message}};
  return SystemVerilogVpiErrorStateError::None;
}

std::optional<SystemVerilogVpiErrorRecord>
SystemVerilogVpiErrorState::check() const {
  std::scoped_lock lock{mutex_};
  return last_;
}

void SystemVerilogVpiErrorState::begin_call() noexcept { clear(); }

void SystemVerilogVpiErrorState::clear() noexcept {
  std::scoped_lock lock{mutex_};
  last_.reset();
}

}  // namespace fsim::runtime
