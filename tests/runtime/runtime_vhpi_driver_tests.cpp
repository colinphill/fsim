// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_driver.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_driver(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult driver_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

}  // namespace

void test_vhdl_vhpi_drivers() {
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

  VhdlVhpiObjectRegistry objects{901};
  const auto root =
      driver_object(objects, VhdlVhpiObjectKind::Root, 0, "work");
  require_vhpi_driver(
      static_cast<bool>(root), "VHPI driver root creation failed");
  const auto create =
      [&](const VhdlVhpiObjectKind kind, const std::string& name) {
        const auto object = driver_object(objects, kind, root.value, name);
        require_vhpi_driver(
            static_cast<bool>(object), "VHPI driver object creation failed");
        return object.value;
      };
  const auto resolver =
      create(VhdlVhpiObjectKind::Subprogram, "resolve_logic");
  const auto resolved_type =
      create(VhdlVhpiObjectKind::Type, "resolved_logic_t");
  const auto unresolved_type =
      create(VhdlVhpiObjectKind::Type, "unresolved_logic_t");
  const auto resolved_signal =
      create(VhdlVhpiObjectKind::Signal, "resolved_signal");
  const auto unresolved_signal =
      create(VhdlVhpiObjectKind::Signal, "unresolved_signal");
  const auto first_source =
      create(VhdlVhpiObjectKind::Process, "first_source");
  const auto second_source =
      create(VhdlVhpiObjectKind::Process, "second_source");

  VhdlVhpiTypeSystem types{objects};
  require_vhpi_driver(
      types.publish(
          resolved_type,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Logic9, 0, {}, true, resolver})
              == VhdlVhpiTypeError::None
          && types.publish(
                 unresolved_type,
                 VhdlVhpiTypeDescriptor{
                     VhdlVhpiScalarKind::Logic9, 0, {}, false, 0})
              == VhdlVhpiTypeError::None
          && types.bind_declaration(resolved_signal, resolved_type)
              == VhdlVhpiTypeError::None
          && types.bind_declaration(unresolved_signal, unresolved_type)
              == VhdlVhpiTypeError::None,
      "VHPI driver type publication failed");

  Scheduler scheduler;
  VhdlVhpiDriverSystem drivers{objects, types, scheduler};
  const auto first = drivers.create_driver(
      resolved_signal,
      first_source,
      PackedLogic9::from_msb_string("00"));
  const auto second = drivers.create_driver(
      resolved_signal,
      second_source,
      PackedLogic9::from_msb_string("ZZ"));
  require_vhpi_driver(
      first && second && first.value.identity != second.value.identity
          && first.value.source_identity != second.value.source_identity
          && first.value.ordinal < second.value.ordinal
          && drivers.signal_value(resolved_signal)
                 .value.to_msb_string()
              == "00",
      "VHPI resolved driver publication failed");

  const auto driver_relationship = drivers.drivers(resolved_signal);
  const auto source_relationship = drivers.sources(resolved_signal);
  require_vhpi_driver(
      driver_relationship && source_relationship
          && driver_relationship.value.size() == 2
          && driver_relationship.value[0].identity == first.value.identity
          && driver_relationship.value[1].identity == second.value.identity
          && source_relationship.value.size() == 2
          && source_relationship.value[0].object == first_source
          && source_relationship.value[1].object == second_source,
      "VHPI driver/source relationships are not creation ordered");
  require_vhpi_driver(
      drivers
              .create_driver(
                  resolved_signal,
                  first_source,
                  PackedLogic9::from_msb_string("11"))
              .error
              == VhdlVhpiDriverError::InvalidSource
          && drivers
                 .create_driver(
                     resolved_signal,
                     create(VhdlVhpiObjectKind::Process, "wide_source"),
                     PackedLogic9::from_msb_string("111"))
                 .error
              == VhdlVhpiDriverError::InvalidValue,
      "VHPI duplicate source or width mismatch was accepted");

  const auto unresolved_first = drivers.create_driver(
      unresolved_signal,
      first_source,
      PackedLogic9::from_msb_string("0"));
  require_vhpi_driver(
      unresolved_first
          && drivers
                 .create_driver(
                     unresolved_signal,
                     second_source,
                     PackedLogic9::from_msb_string("1"))
                 .error
              == VhdlVhpiDriverError::MultipleDrivers,
      "VHPI unresolved signal accepted multiple drivers");

  VhdlVhpiObjectRegistry foreign_objects{902};
  const auto foreign_root = driver_object(
      foreign_objects, VhdlVhpiObjectKind::Root, 0, "foreign");
  require_vhpi_driver(
      static_cast<bool>(foreign_root), "VHPI foreign driver root failed");
  VhdlVhpiTypeSystem foreign_types{foreign_objects};
  Scheduler foreign_scheduler;
  VhdlVhpiDriverSystem foreign{
      foreign_objects, foreign_types, foreign_scheduler};
  require_vhpi_driver(
      foreign.driver(first.value.identity).error
          == VhdlVhpiDriverError::CrossSimulation,
      "VHPI foreign driver identity was accepted");

  const std::array<VhdlVhpiWaveformElement, 2> first_waveform{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("11"), 3},
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("00"), 6}};
  const auto projected = drivers.schedule_waveform(
      first.value.identity,
      first_waveform,
      0,
      VhdlVhpiDelayMode::Transport);
  require_vhpi_driver(
      projected && projected.value.size() == 2
          && projected.value[0].time == 3
          && projected.value[0].waveform_index == 0
          && projected.value[1].time == 6
          && projected.value[1].waveform_index == 1,
      "VHPI projected waveform publication failed");

  const std::array<VhdlVhpiWaveformElement, 1> replacement{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("11"), 4}};
  require_vhpi_driver(
      static_cast<bool>(
          drivers.schedule_waveform(
              first.value.identity,
              replacement,
              0,
              VhdlVhpiDelayMode::Transport)),
      "VHPI projected waveform replacement failed");
  const auto after_replacement = drivers.transactions(first.value.identity);
  require_vhpi_driver(
      after_replacement && after_replacement.value.size() == 2
          && after_replacement.value[0].time == 3
          && after_replacement.value[1].time == 4,
      "VHPI replacement did not truncate later waveform elements");

  static_cast<void>(scheduler.run(3));
  require_vhpi_driver(
      drivers.driver(first.value.identity)
              .value.value.to_msb_string()
              == "11"
          && drivers.signal_value(resolved_signal)
                 .value.to_msb_string()
              == "11"
          && drivers.transactions(first.value.identity).value.size() == 1,
      "VHPI first projected transaction did not commit at time 3");
  static_cast<void>(scheduler.run(4));
  require_vhpi_driver(
      drivers.transactions(first.value.identity).value.empty()
          && drivers.signal_value(resolved_signal)
                 .value.to_msb_string()
              == "11",
      "VHPI replacement transaction did not commit at time 4");

  const std::array<VhdlVhpiWaveformElement, 2> transport_tail{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("00"), 2},
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("ZZ"), 7}};
  require_vhpi_driver(
      static_cast<bool>(
          drivers.schedule_waveform(
              first.value.identity,
              transport_tail,
              0,
              VhdlVhpiDelayMode::Transport)),
      "VHPI transport tail scheduling failed");
  const std::array<VhdlVhpiWaveformElement, 1> inertial_replacement{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("11"), 3}};
  const auto inertial = drivers.schedule_waveform(
      first.value.identity,
      inertial_replacement,
      2,
      VhdlVhpiDelayMode::Inertial);
  const auto inertial_pending = drivers.transactions(first.value.identity);
  require_vhpi_driver(
      inertial && inertial_pending && inertial_pending.value.size() == 1
          && inertial_pending.value[0].time == 7
          && inertial_pending.value[0].rejection == 2
          && inertial_pending.value[0].mode
              == VhdlVhpiDelayMode::Inertial,
      "VHPI inertial rejection did not remove the preceding pulse");

  const std::array<VhdlVhpiWaveformElement, 2> descending{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("00"), 5},
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("11"), 4}};
  const std::array<VhdlVhpiWaveformElement, 1> wrong_width{
      VhdlVhpiWaveformElement{
          PackedLogic9::from_msb_string("1"), 1}};
  require_vhpi_driver(
      drivers
              .schedule_waveform(
                  first.value.identity,
                  {},
                  0,
                  VhdlVhpiDelayMode::Transport)
              .error
              == VhdlVhpiDriverError::InvalidWaveform
          && drivers
                 .schedule_waveform(
                     first.value.identity,
                     descending,
                     0,
                     VhdlVhpiDelayMode::Transport)
                 .error
              == VhdlVhpiDriverError::InvalidWaveform
          && drivers
                 .schedule_waveform(
                     first.value.identity,
                     wrong_width,
                     0,
                     VhdlVhpiDelayMode::Transport)
                 .error
              == VhdlVhpiDriverError::InvalidWaveform
          && drivers
                 .schedule_waveform(
                     first.value.identity,
                     inertial_replacement,
                     4,
                     VhdlVhpiDelayMode::Inertial)
                 .error
              == VhdlVhpiDriverError::InvalidRejection
          && drivers.transactions(first.value.identity).value.size() == 1,
      "VHPI invalid waveform changed the projected queue");

  static_cast<void>(scheduler.run());
  require_vhpi_driver(
      scheduler.now() == 7
          && drivers.transactions(first.value.identity).value.empty()
          && drivers.driver(first.value.identity)
                 .value.value.to_msb_string()
              == "11"
          && drivers.signal_value(resolved_signal)
                 .value.to_msb_string()
              == "11",
      "VHPI inertial transaction did not complete deterministically");
}

}  // namespace fsim::tests::runtime
