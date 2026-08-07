// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_driver.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_write(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult write_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

}  // namespace

void test_vhdl_vhpi_writes() {
  using fsim::runtime::PackedLogic9;
  using fsim::runtime::Scheduler;
  using fsim::runtime::VhdlVhpiDelayMode;
  using fsim::runtime::VhdlVhpiDriverError;
  using fsim::runtime::VhdlVhpiDriverSystem;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiScalarKind;
  using fsim::runtime::VhdlVhpiTypeDescriptor;
  using fsim::runtime::VhdlVhpiTypeError;
  using fsim::runtime::VhdlVhpiTypeSystem;
  using fsim::runtime::VhdlVhpiWaveformElement;
  using fsim::runtime::VhdlVhpiWriteKind;

  VhdlVhpiObjectRegistry objects{1001};
  const auto root =
      write_object(objects, VhdlVhpiObjectKind::Root, 0, "work");
  require_vhpi_write(
      static_cast<bool>(root), "VHPI write root creation failed");
  const auto create =
      [&](const VhdlVhpiObjectKind kind, const std::string& name) {
        const auto object = write_object(objects, kind, root.value, name);
        require_vhpi_write(
            static_cast<bool>(object), "VHPI write object creation failed");
        return object.value;
      };
  const auto resolver =
      create(VhdlVhpiObjectKind::Subprogram, "resolve_logic");
  const auto logic_type =
      create(VhdlVhpiObjectKind::Type, "logic_t");
  const auto signal =
      create(VhdlVhpiObjectKind::Signal, "target");
  const auto first_owner =
      create(VhdlVhpiObjectKind::Process, "first_owner");
  const auto second_owner =
      create(VhdlVhpiObjectKind::Process, "second_owner");
  VhdlVhpiTypeSystem types{objects};
  require_vhpi_write(
      types.publish(
          logic_type,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Logic9, 0, {}, true, resolver})
              == VhdlVhpiTypeError::None
          && types.bind_declaration(signal, logic_type)
              == VhdlVhpiTypeError::None,
      "VHPI write type publication failed");

  Scheduler scheduler;
  VhdlVhpiDriverSystem writes{objects, types, scheduler};
  const auto driver = writes.create_driver(
      signal, first_owner, PackedLogic9::from_msb_string("00"));
  require_vhpi_write(
      static_cast<bool>(driver), "VHPI write driver creation failed");

  const auto deposit = writes.deposit(
      signal, first_owner, PackedLogic9::from_msb_string("11"));
  require_vhpi_write(
      deposit && deposit.value.kind == VhdlVhpiWriteKind::Deposit
          && deposit.value.completed && !deposit.value.pending
          && writes.operation(deposit.value.identity).value.completed
          && writes.signal_value(signal).value.to_msb_string() == "11"
          && writes.cancel_operation(deposit.value.identity, first_owner)
              == VhdlVhpiDriverError::AlreadyCompleted,
      "VHPI immediate deposit or retained identity failed");

  const std::array<VhdlVhpiWaveformElement, 1> driver_zero{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("00"), 1}};
  require_vhpi_write(
      static_cast<bool>(writes.schedule_waveform(
          driver.value.identity,
          driver_zero,
          0,
          VhdlVhpiDelayMode::Transport)),
      "VHPI post-deposit driver transaction failed");
  static_cast<void>(scheduler.run(1));
  require_vhpi_write(
      writes.signal_value(signal).value.to_msb_string() == "00",
      "VHPI driver activity did not supersede a deposit");

  const auto force = writes.force(
      signal, first_owner, PackedLogic9::from_msb_string("11"));
  const std::array<VhdlVhpiWaveformElement, 1> driver_release{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("ZZ"), 1}};
  require_vhpi_write(
      force && force.value.active
          && writes.signal_value(signal).value.to_msb_string() == "11"
          && static_cast<bool>(writes.schedule_waveform(
                 driver.value.identity,
                 driver_release,
                 0,
                 VhdlVhpiDelayMode::Transport)),
      "VHPI immediate force publication failed");
  static_cast<void>(scheduler.run(2));
  require_vhpi_write(
      writes.signal_value(signal).value.to_msb_string() == "11"
          && writes.driver(driver.value.identity)
                 .value.value.to_msb_string()
              == "ZZ",
      "VHPI force did not mask underlying driver activity");
  const auto release = writes.release(force.value.identity, first_owner);
  require_vhpi_write(
      release && release.value.kind == VhdlVhpiWriteKind::Release
          && release.value.active && release.value.completed
          && !writes.operation(force.value.identity).value.active
          && writes.signal_value(signal).value.to_msb_string() == "ZZ",
      "VHPI release did not expose the latest underlying value");

  const auto lower_force = writes.force(
      signal, first_owner, PackedLogic9::from_msb_string("00"));
  const auto upper_force = writes.force(
      signal, second_owner, PackedLogic9::from_msb_string("11"));
  require_vhpi_write(
      lower_force && upper_force
          && writes.signal_value(signal).value.to_msb_string() == "11"
          && writes.release(lower_force.value.identity, first_owner)
          && writes.signal_value(signal).value.to_msb_string() == "11"
          && writes.release(upper_force.value.identity, second_owner)
          && writes.signal_value(signal).value.to_msb_string() == "ZZ",
      "VHPI force layering or non-top release failed");

  const auto canceled_force = writes.force(
      signal,
      first_owner,
      PackedLogic9::from_msb_string("00"),
      2);
  require_vhpi_write(
      canceled_force && canceled_force.value.pending
          && writes.cancel_operation(
                 canceled_force.value.identity, second_owner)
              == VhdlVhpiDriverError::InvalidOwner
          && writes.cancel_operation(
                 canceled_force.value.identity, first_owner)
              == VhdlVhpiDriverError::None
          && writes.operation(canceled_force.value.identity).value.canceled,
      "VHPI delayed force cancellation failed");
  static_cast<void>(scheduler.run());
  require_vhpi_write(
      scheduler.now() == 2
          && writes.signal_value(signal).value.to_msb_string() == "ZZ",
      "VHPI canceled force changed the signal");

  const auto delayed_deposit = writes.deposit(
      signal,
      first_owner,
      PackedLogic9::from_msb_string("11"),
      2);
  require_vhpi_write(
      delayed_deposit && delayed_deposit.value.pending,
      "VHPI delayed deposit was not retained");
  static_cast<void>(scheduler.run());
  require_vhpi_write(
      scheduler.now() == 4
          && writes.operation(delayed_deposit.value.identity)
                 .value.completed
          && writes.signal_value(signal).value.to_msb_string() == "11",
      "VHPI delayed deposit did not commit");

  const auto delayed_release_force = writes.force(
      signal, first_owner, PackedLogic9::from_msb_string("00"));
  const auto delayed_release = writes.release(
      delayed_release_force.value.identity, first_owner, 2);
  const std::array<VhdlVhpiWaveformElement, 1> underlying_one{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("11"), 1}};
  require_vhpi_write(
      delayed_release_force && delayed_release
          && delayed_release.value.pending
          && static_cast<bool>(writes.schedule_waveform(
                 driver.value.identity,
                 underlying_one,
                 0,
                 VhdlVhpiDelayMode::Transport)),
      "VHPI delayed release preparation failed");
  static_cast<void>(scheduler.run(5));
  require_vhpi_write(
      writes.signal_value(signal).value.to_msb_string() == "00",
      "VHPI driver update escaped a pending force release");
  static_cast<void>(scheduler.run(6));
  require_vhpi_write(
      writes.signal_value(signal).value.to_msb_string() == "11"
          && writes.operation(delayed_release.value.identity)
                 .value.completed,
      "VHPI delayed release did not expose the latest driver");

  const std::array<VhdlVhpiWaveformElement, 2> cancellable{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("00"), 2},
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("11"), 4}};
  const auto transactions = writes.schedule_waveform(
      driver.value.identity,
      cancellable,
      0,
      VhdlVhpiDelayMode::Transport);
  require_vhpi_write(
      transactions && transactions.value.size() == 2
          && writes.cancel_transaction(
                 transactions.value[1].identity, second_owner)
              == VhdlVhpiDriverError::InvalidOwner
          && writes.cancel_transaction(
                 transactions.value[1].identity, first_owner)
              == VhdlVhpiDriverError::None
          && writes.cancel_transaction(
                 transactions.value[1].identity, first_owner)
              == VhdlVhpiDriverError::InvalidTransaction
          && writes.transactions(driver.value.identity).value.size() == 1,
      "VHPI projected transaction cancellation failed");
  static_cast<void>(scheduler.run());
  require_vhpi_write(
      scheduler.now() == 8
          && writes.signal_value(signal).value.to_msb_string() == "00",
      "VHPI retained projected transaction did not commit");

  const auto before_invalid =
      writes.signal_value(signal).value.to_msb_string();
  require_vhpi_write(
      writes
              .deposit(
                  signal,
                  first_owner,
                  PackedLogic9::from_msb_string("1"))
              .error
              == VhdlVhpiDriverError::InvalidValue
          && writes.release(force.value.identity, second_owner).error
              == VhdlVhpiDriverError::InvalidOperation
          && writes
                 .force(
                     signal,
                     signal,
                     PackedLogic9::from_msb_string("11"))
                 .error
              == VhdlVhpiDriverError::InvalidOwner
          && writes.signal_value(signal).value.to_msb_string()
              == before_invalid,
      "VHPI invalid write partially changed signal state");

  VhdlVhpiObjectRegistry foreign_objects{1002};
  const auto foreign_root = write_object(
      foreign_objects, VhdlVhpiObjectKind::Root, 0, "foreign");
  require_vhpi_write(
      static_cast<bool>(foreign_root), "VHPI foreign write root failed");
  VhdlVhpiTypeSystem foreign_types{foreign_objects};
  Scheduler foreign_scheduler;
  VhdlVhpiDriverSystem foreign{
      foreign_objects, foreign_types, foreign_scheduler};
  require_vhpi_write(
      foreign.operation(deposit.value.identity).error
          == VhdlVhpiDriverError::CrossSimulation,
      "VHPI foreign write-operation identity was accepted");
}

}  // namespace fsim::tests::runtime
