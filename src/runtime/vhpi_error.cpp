// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_error.hpp"

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_code_size = 256;
constexpr std::size_t maximum_message_size = 1U << 16U;

}  // namespace

VhdlVhpiErrorState::VhdlVhpiErrorState(
    const std::uint64_t simulation_identity) noexcept
    : simulation_identity_(simulation_identity) {}

std::uint64_t VhdlVhpiErrorState::simulation_identity() const noexcept {
  return simulation_identity_;
}

VhdlVhpiErrorStateError VhdlVhpiErrorState::publish(
    const std::uint32_t severity,
    const std::string_view code,
    const std::string_view message) {
  if (simulation_identity_ == 0U) {
    return VhdlVhpiErrorStateError::InvalidSimulation;
  }
  if (severity > static_cast<std::uint32_t>(FSIM_VHPI_ERROR_INTERNAL)) {
    return VhdlVhpiErrorStateError::InvalidSeverity;
  }
  if (code.empty() || code.size() > maximum_code_size
      || code.find('\0') != std::string_view::npos) {
    return VhdlVhpiErrorStateError::InvalidCode;
  }
  if (message.empty() || message.size() > maximum_message_size
      || message.find('\0') != std::string_view::npos) {
    return VhdlVhpiErrorStateError::InvalidMessage;
  }
  std::scoped_lock lock{mutex_};
  last_ = VhdlVhpiErrorRecord{
      static_cast<fsim_vhpi_error_severity_v1>(severity),
      std::string{code},
      std::string{message},
  };
  return VhdlVhpiErrorStateError::None;
}

std::optional<VhdlVhpiErrorRecord> VhdlVhpiErrorState::check() const {
  std::scoped_lock lock{mutex_};
  return last_;
}

void VhdlVhpiErrorState::begin_call() noexcept { clear(); }

void VhdlVhpiErrorState::clear() noexcept {
  std::scoped_lock lock{mutex_};
  last_.reset();
}

}  // namespace fsim::runtime
