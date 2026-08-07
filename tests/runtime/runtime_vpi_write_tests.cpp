// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_value_control.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::tests::runtime {

namespace {

using fsim::runtime::Logic4;
using fsim::runtime::Logic9;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunStatus;
using fsim::runtime::Scheduler;
using fsim::runtime::SystemVerilogVpiDelayPolicy;
using fsim::runtime::SystemVerilogVpiDirection;
using fsim::runtime::SystemVerilogVpiDriveStrength;
using fsim::runtime::SystemVerilogVpiNetKind;
using fsim::runtime::SystemVerilogVpiObjectDescriptor;
using fsim::runtime::SystemVerilogVpiObjectError;
using fsim::runtime::SystemVerilogVpiObjectKind;
using fsim::runtime::SystemVerilogVpiObjectRegistry;
using fsim::runtime::SystemVerilogVpiScheduledWriteStatus;
using fsim::runtime::SystemVerilogVpiStoredValue;
using fsim::runtime::SystemVerilogVpiStrengthRank;
using fsim::runtime::SystemVerilogVpiTypeInfo;
using fsim::runtime::SystemVerilogVpiValueCategory;
using fsim::runtime::SystemVerilogVpiValueControl;
using fsim::runtime::SystemVerilogVpiValueError;
using fsim::runtime::SystemVerilogVpiValueFormat;
using fsim::runtime::SystemVerilogVpiValueWriteData;
using fsim::runtime::SystemVerilogVpiWriteControlError;
using fsim::runtime::SystemVerilogVpiWriteKind;
using fsim::runtime::make_systemverilog_vpi_stored_value;

void require_vpi_write(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

SystemVerilogVpiTypeInfo write_type(
    const SystemVerilogVpiValueCategory category,
    const std::uint32_t width,
    const bool is_signed = false) {
  SystemVerilogVpiTypeInfo type;
  type.category = category;
  type.width = width;
  type.is_signed = is_signed;
  return type;
}

template <typename Value>
SystemVerilogVpiStoredValue stored_write(Value value) {
  SystemVerilogVpiStoredValue result;
  result.payload = std::move(value);
  return result;
}

SystemVerilogVpiStoredValue scalar_write(
    const SystemVerilogVpiTypeInfo& type,
    const Logic9 state) {
  SystemVerilogVpiValueWriteData input;
  input.scalar = state;
  const auto result = make_systemverilog_vpi_stored_value(
      type, SystemVerilogVpiValueFormat::Scalar, input);
  require_vpi_write(
      static_cast<bool>(result),
      "VPI scalar write fixture conversion failed");
  return *result.value;
}

}  // namespace

void test_systemverilog_vpi_checked_writes() {
  const auto logic_type =
      write_type(SystemVerilogVpiValueCategory::Logic4, 1);
  const auto bit65_type =
      write_type(SystemVerilogVpiValueCategory::Bit2, 65);
  const auto logic65_type =
      write_type(SystemVerilogVpiValueCategory::Logic4, 65);
  const auto logic9_type =
      write_type(SystemVerilogVpiValueCategory::Logic9, 1);

  SystemVerilogVpiValueWriteData integer_input;
  integer_input.integer = 0xfbU;
  const auto integer_conversion = make_systemverilog_vpi_stored_value(
      write_type(SystemVerilogVpiValueCategory::Integer4, 8, true),
      SystemVerilogVpiValueFormat::Integer,
      integer_input);
  std::array<std::uint64_t, 2> bit_words{1, 1};
  SystemVerilogVpiValueWriteData bit_input;
  bit_input.words = bit_words;
  const auto bit_conversion = make_systemverilog_vpi_stored_value(
      bit65_type, SystemVerilogVpiValueFormat::BitVector, bit_input);
  std::array<std::uint64_t, 1> short_words{1};
  bit_input.words = short_words;
  const auto short_conversion = make_systemverilog_vpi_stored_value(
      bit65_type, SystemVerilogVpiValueFormat::BitVector, bit_input);
  std::array<std::uint64_t, 2> bad_padding{1, 2};
  bit_input.words = bad_padding;
  const auto padding_conversion = make_systemverilog_vpi_stored_value(
      bit65_type, SystemVerilogVpiValueFormat::BitVector, bit_input);
  require_vpi_write(
      integer_conversion && bit_conversion
          && short_conversion.error
              == SystemVerilogVpiValueError::BufferTooSmall
          && short_conversion.required_words == 2
          && padding_conversion.error
              == SystemVerilogVpiValueError::InvalidEncoding,
      "VPI writes convert raw integers and reject short or nonzero-padded two-state buffers");

  std::array<std::uint64_t, 4> logic4_words{
      (std::uint64_t{1} << 63U) | 1U,
      0,
      std::uint64_t{1} << 63U,
      1};
  SystemVerilogVpiValueWriteData logic4_input;
  logic4_input.words = logic4_words;
  const auto logic4_conversion = make_systemverilog_vpi_stored_value(
      logic65_type,
      SystemVerilogVpiValueFormat::Logic4Vector,
      logic4_input);
  std::array<std::uint64_t, 4> invalid_logic9_words{1, 1, 1, 1};
  SystemVerilogVpiValueWriteData logic9_input;
  logic9_input.words = invalid_logic9_words;
  const auto invalid_logic9 = make_systemverilog_vpi_stored_value(
      logic9_type,
      SystemVerilogVpiValueFormat::Logic9Vector,
      logic9_input);
  require_vpi_write(
      logic4_conversion
          && invalid_logic9.error
              == SystemVerilogVpiValueError::InvalidEncoding,
      "VPI writes decode exact aval/bval planes and reject foreign nine-state encodings");

  SystemVerilogVpiValueWriteData real_input;
  real_input.real = std::numeric_limits<double>::max();
  const auto shortreal_overflow = make_systemverilog_vpi_stored_value(
      write_type(SystemVerilogVpiValueCategory::ShortReal, 32),
      SystemVerilogVpiValueFormat::Real,
      real_input);
  const std::array<char, 3> string_bytes{'a', '\0', 'b'};
  SystemVerilogVpiValueWriteData string_input;
  string_input.characters = string_bytes;
  const auto string_conversion = make_systemverilog_vpi_stored_value(
      write_type(SystemVerilogVpiValueCategory::String, 0),
      SystemVerilogVpiValueFormat::String,
      string_input);
  SystemVerilogVpiValueWriteData strength_input;
  strength_input.strength.state = Logic4::z;
  strength_input.strength.drive = SystemVerilogVpiDriveStrength{
      SystemVerilogVpiStrengthRank::Pull,
      SystemVerilogVpiStrengthRank::Supply};
  const auto strength_conversion = make_systemverilog_vpi_stored_value(
      logic_type,
      SystemVerilogVpiValueFormat::Strength,
      strength_input);
  require_vpi_write(
      shortreal_overflow.error == SystemVerilogVpiValueError::ResourceLimit
          && string_conversion && strength_conversion,
      "VPI writes retain embedded string bytes and strength while rejecting shortreal overflow");

  SystemVerilogVpiObjectRegistry registry{801};
  Scheduler scheduler;
  SystemVerilogVpiValueControl control{registry, scheduler, 1000};
  const auto root =
      registry.create(SystemVerilogVpiObjectKind::Root, 0, "top");
  const auto create = [&](const SystemVerilogVpiObjectKind kind,
                          const char* const name,
                          const SystemVerilogVpiTypeInfo& type) {
    return registry.create(SystemVerilogVpiObjectDescriptor{
        kind, root.value, name, std::nullopt, type});
  };
  const auto variable =
      create(SystemVerilogVpiObjectKind::Variable, "value", logic_type);
  auto parameter_type =
      write_type(SystemVerilogVpiValueCategory::Integer4, 8, true);
  parameter_type.is_constant = true;
  const auto parameter = create(
      SystemVerilogVpiObjectKind::Parameter, "constant", parameter_type);
  auto input_type = logic_type;
  input_type.direction = SystemVerilogVpiDirection::Input;
  const auto input_port =
      create(SystemVerilogVpiObjectKind::Port, "input_port", input_type);
  auto output_type = logic_type;
  output_type.direction = SystemVerilogVpiDirection::Output;
  const auto output_port =
      create(SystemVerilogVpiObjectKind::Port, "output_port", output_type);
  require_vpi_write(
      root && variable && parameter && input_port && output_port
          && registry.bind_value(
                 variable.value,
                 stored_write(PackedLogic4{1, Logic4::zero}))
              == SystemVerilogVpiValueError::None,
      "VPI write fixtures publish writable, constant, input, and output objects");

  auto one = scalar_write(logic_type, Logic9::one);
  auto zero = scalar_write(logic_type, Logic9::zero);
  auto unknown = scalar_write(logic_type, Logic9::x);
  require_vpi_write(
      control.apply(
          variable.value, SystemVerilogVpiWriteKind::Deposit, one)
              == SystemVerilogVpiValueError::None
          && registry.read_value(
                 variable.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::one
          && control.apply(
                 variable.value,
                 SystemVerilogVpiWriteKind::Force,
                 unknown)
              == SystemVerilogVpiValueError::None
          && control.apply(
                 variable.value,
                 SystemVerilogVpiWriteKind::Deposit,
                 zero)
              == SystemVerilogVpiValueError::None
          && registry.read_value(
                 variable.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::x
          && control.apply(
                 variable.value,
                 SystemVerilogVpiWriteKind::Release)
              == SystemVerilogVpiValueError::None
          && registry.read_value(
                 variable.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::zero
          && control.apply(
                 variable.value,
                 SystemVerilogVpiWriteKind::Release)
              == SystemVerilogVpiValueError::NotForced,
      "VPI force masks deposits and release reveals the latest underlying value");

  require_vpi_write(
      control.apply(
          parameter.value,
          SystemVerilogVpiWriteKind::Deposit,
          *integer_conversion.value)
              == SystemVerilogVpiValueError::ReadOnly
          && control.apply(
                 input_port.value,
                 SystemVerilogVpiWriteKind::Deposit,
                 one)
              == SystemVerilogVpiValueError::InputOnly
          && control.apply(
                 output_port.value,
                 SystemVerilogVpiWriteKind::Deposit,
                 one)
              == SystemVerilogVpiValueError::None
          && control.apply(
                 variable.value,
                 SystemVerilogVpiWriteKind::Deposit,
                 stored_write(PackedLogic4{2}))
              == SystemVerilogVpiValueError::TypeMismatch,
      "VPI writes enforce constants, port direction, and exact width transactionally");
  require_vpi_write(
      control.apply(
          output_port.value,
          SystemVerilogVpiWriteKind::Deposit,
          *strength_conversion.value)
              == SystemVerilogVpiValueError::None
          && registry.read_value(
                 output_port.value,
                 SystemVerilogVpiValueFormat::Strength).strength.drive
              == SystemVerilogVpiDriveStrength{
                  SystemVerilogVpiStrengthRank::Pull,
                  SystemVerilogVpiStrengthRank::Supply},
      "VPI strength deposits preserve distinct zero/one ranks");


  const auto delayed_deposit = control.schedule(
      variable.value,
      SystemVerilogVpiWriteKind::Deposit,
      one,
      5,
      SystemVerilogVpiDelayPolicy::Transport);
  const auto delayed_force = control.schedule(
      variable.value,
      SystemVerilogVpiWriteKind::Force,
      unknown,
      7,
      SystemVerilogVpiDelayPolicy::Transport);
  require_vpi_write(
      delayed_deposit && delayed_force && control.pending() == 2,
      "VPI transport scheduling retains independent ordered writes");
  const auto first_run = scheduler.run(5);
  require_vpi_write(
      first_run.status == RunStatus::time_limit
          && control.status(delayed_deposit.value).status
              == SystemVerilogVpiScheduledWriteStatus::Applied
          && control.status(delayed_force.value).status
              == SystemVerilogVpiScheduledWriteStatus::Pending
          && registry.read_value(
                 variable.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::one,
      "VPI delayed deposits publish exactly at the common scheduler tick");
  (void)scheduler.run(7);
  require_vpi_write(
      control.status(delayed_force.value).status
              == SystemVerilogVpiScheduledWriteStatus::Applied
          && registry.read_value(
                 variable.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::x,
      "VPI delayed force publication preserves scheduler order");

  const auto delayed_release = control.schedule(
      variable.value,
      SystemVerilogVpiWriteKind::Release,
      std::nullopt,
      2,
      SystemVerilogVpiDelayPolicy::Transport);
  (void)scheduler.run();
  require_vpi_write(
      delayed_release
          && control.status(delayed_release.value).status
              == SystemVerilogVpiScheduledWriteStatus::Applied
          && registry.read_value(
                 variable.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::one,
      "VPI delayed release reveals the deposited underlying value");

  const auto superseded = control.schedule(
      variable.value,
      SystemVerilogVpiWriteKind::Deposit,
      one,
      10,
      SystemVerilogVpiDelayPolicy::Inertial);
  const auto inertial = control.schedule(
      variable.value,
      SystemVerilogVpiWriteKind::Deposit,
      zero,
      5,
      SystemVerilogVpiDelayPolicy::Inertial);
  require_vpi_write(
      superseded && inertial
          && control.status(superseded.value).status
              == SystemVerilogVpiScheduledWriteStatus::Cancelled,
      "VPI inertial scheduling cancels older pending writes on the same object");
  (void)scheduler.run();
  require_vpi_write(
      control.status(inertial.value).status
              == SystemVerilogVpiScheduledWriteStatus::Applied
          && registry.read_value(
                 variable.value,
                 SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::zero,
      "VPI inertial scheduling publishes only the latest accepted write");

  const auto cancelled = control.schedule(
      variable.value,
      SystemVerilogVpiWriteKind::Deposit,
      one,
      3,
      SystemVerilogVpiDelayPolicy::Transport);
  require_vpi_write(
      cancelled
          && control.cancel(cancelled.value)
              == SystemVerilogVpiWriteControlError::None
          && control.status(cancelled.value).status
              == SystemVerilogVpiScheduledWriteStatus::Cancelled
          && control.cancel(cancelled.value)
              == SystemVerilogVpiWriteControlError::NotPending,
      "VPI delayed writes support explicit idempotence-aware cancellation");

  const auto denied = control.schedule(
      parameter.value,
      SystemVerilogVpiWriteKind::Deposit,
      *integer_conversion.value,
      1,
      SystemVerilogVpiDelayPolicy::Transport);
  require_vpi_write(
      denied.error == SystemVerilogVpiWriteControlError::None
          && denied.value_error == SystemVerilogVpiValueError::ReadOnly
          && control.pending() == 0,
      "VPI delayed writes preflight read-only failure without queue publication");

  const auto overflow = control.schedule(
      variable.value,
      SystemVerilogVpiWriteKind::Deposit,
      one,
      std::numeric_limits<std::uint64_t>::max(),
      SystemVerilogVpiDelayPolicy::Transport);
  require_vpi_write(
      overflow.error == SystemVerilogVpiWriteControlError::ResourceLimit
          && control.pending() == 0,
      "VPI delayed writes reject absolute-time overflow before queue publication");

  const auto transient =
      create(SystemVerilogVpiObjectKind::Variable, "transient", logic_type);
  require_vpi_write(
      transient
          && registry.bind_value(
                 transient.value,
                 stored_write(PackedLogic4{1, Logic4::zero}))
              == SystemVerilogVpiValueError::None,
      "VPI delayed stale-handle fixture binds an initial value");
  const auto stale_write = control.schedule(
      transient.value,
      SystemVerilogVpiWriteKind::Deposit,
      one,
      2,
      SystemVerilogVpiDelayPolicy::Transport);
  require_vpi_write(
      stale_write
          && registry.release(transient.value)
              == SystemVerilogVpiObjectError::None,
      "VPI delayed stale-handle fixture releases before execution");
  const auto replacement =
      create(SystemVerilogVpiObjectKind::Variable, "replacement", logic_type);
  (void)scheduler.run();
  require_vpi_write(
      replacement
          && control.status(stale_write.value).status
              == SystemVerilogVpiScheduledWriteStatus::Failed
          && control.status(stale_write.value).value_error
              == SystemVerilogVpiValueError::StaleHandle,
      "VPI delayed writes reject generation reuse without partial publication");

  SystemVerilogVpiValueControl other_control{registry, scheduler, 2000};
  require_vpi_write(
      other_control.cancel(cancelled.value)
              == SystemVerilogVpiWriteControlError::CrossControl
          && control.release(cancelled.value)
              == SystemVerilogVpiWriteControlError::None
          && control.status(cancelled.value).error
              == SystemVerilogVpiWriteControlError::NotFound,
      "VPI scheduled-write handles preserve controller ownership and independent release");
}

}  // namespace fsim::tests::runtime
