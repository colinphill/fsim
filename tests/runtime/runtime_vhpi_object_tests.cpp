// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_error.hpp"
#include "fsim/runtime/vhpi_object.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {

namespace {

void require_vhpi_object(
    const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void test_vhdl_vhpi_error_and_handles() {
  using fsim::runtime::VhdlVhpiErrorState;
  using fsim::runtime::VhdlVhpiErrorStateError;
  using fsim::runtime::VhdlVhpiIteratorError;
  using fsim::runtime::VhdlVhpiObjectError;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;

  VhdlVhpiErrorState errors{301};
  VhdlVhpiErrorState other_errors{302};
  require_vhpi_object(
      errors.simulation_identity() == 301
          && other_errors.simulation_identity() == 302
          && !errors.check() && !other_errors.check(),
      "VHPI error state starts empty and simulation-owned");
  std::string code = "FSIM-VHPI-OBJECT-001";
  std::string message = "owned diagnostic storage";
  require_vhpi_object(
      errors.publish(FSIM_VHPI_ERROR_ERROR, code, message)
          == VhdlVhpiErrorStateError::None,
      "VHPI error state accepts a bounded diagnostic");
  code.assign("changed");
  message.assign("changed");
  const auto retained = errors.check();
  require_vhpi_object(
      retained && retained->severity == FSIM_VHPI_ERROR_ERROR
          && retained->code == "FSIM-VHPI-OBJECT-001"
          && retained->message == "owned diagnostic storage"
          && !other_errors.check(),
      "VHPI diagnostics own strings and remain isolated by simulation");
  require_vhpi_object(
      errors.publish(
          static_cast<std::uint32_t>(FSIM_VHPI_ERROR_INTERNAL) + 1U,
          "FSIM-VHPI-BAD-SEVERITY", "invalid")
          == VhdlVhpiErrorStateError::InvalidSeverity
          && errors.check()->code == "FSIM-VHPI-OBJECT-001",
      "VHPI invalid severity does not replace retained state");
  require_vhpi_object(
      errors.publish(FSIM_VHPI_ERROR_ERROR, "", "message")
              == VhdlVhpiErrorStateError::InvalidCode
          && errors.publish(
                 FSIM_VHPI_ERROR_ERROR,
                 std::string_view{"bad\0code", 8},
                 "message")
              == VhdlVhpiErrorStateError::InvalidCode
          && errors.publish(
                 FSIM_VHPI_ERROR_ERROR, "CODE",
                 std::string_view{"bad\0message", 11})
              == VhdlVhpiErrorStateError::InvalidMessage,
      "VHPI error state rejects empty and embedded-NUL fields");
  require_vhpi_object(
      errors.publish(
          FSIM_VHPI_ERROR_ERROR, std::string(257, 'c'), "message")
              == VhdlVhpiErrorStateError::InvalidCode
          && errors.publish(
                 FSIM_VHPI_ERROR_ERROR, "CODE",
                 std::string((1U << 16U) + 1U, 'm'))
              == VhdlVhpiErrorStateError::InvalidMessage,
      "VHPI error state enforces diagnostic resource bounds");
  errors.begin_call();
  require_vhpi_object(
      !errors.check(), "VHPI call boundary clears retained error state");
  VhdlVhpiErrorState invalid_errors{0};
  require_vhpi_object(
      invalid_errors.publish(FSIM_VHPI_ERROR_NOTE, "CODE", "message")
          == VhdlVhpiErrorStateError::InvalidSimulation,
      "VHPI error state rejects an unowned simulation");

  VhdlVhpiObjectRegistry invalid_registry{0};
  require_vhpi_object(
      !invalid_registry.valid()
          && invalid_registry
                     .create_object(VhdlVhpiObjectKind::Root)
                     .error
              == VhdlVhpiObjectError::InvalidSimulation,
      "VHPI object registry rejects an unowned simulation");

  VhdlVhpiObjectRegistry registry{301};
  VhdlVhpiObjectRegistry other_registry{302};
  const auto root = registry.create_object(VhdlVhpiObjectKind::Root);
  const auto other_root =
      other_registry.create_object(VhdlVhpiObjectKind::Root);
  require_vhpi_object(
      registry.valid() && other_registry.valid() && root && other_root
          && root.value != other_root.value
          && (root.value & (UINT64_C(1) << 62U)) != 0U,
      "VHPI registries create distinct tagged nonpointer handles");
  require_vhpi_object(
      registry.lookup_object(1).error
              == VhdlVhpiObjectError::InvalidHandle
          && registry.lookup_object(other_root.value).error
              == VhdlVhpiObjectError::CrossSimulation,
      "VHPI lookup distinguishes malformed and cross-simulation handles");
  require_vhpi_object(
      registry
              .create_object(
                  static_cast<VhdlVhpiObjectKind>(999), root.value)
              .error
          == VhdlVhpiObjectError::InvalidKind,
      "VHPI registry rejects unknown object kinds");

  const auto entity =
      registry.create_object(VhdlVhpiObjectKind::Entity, root.value);
  const auto signal =
      registry.create_object(VhdlVhpiObjectKind::Signal, entity.value);
  require_vhpi_object(
      entity && signal,
      "VHPI registry creates simulation-owned parented objects");
  const auto root_metadata = registry.lookup_object(root.value);
  const auto entity_metadata = registry.lookup_object(entity.value);
  const auto signal_metadata = registry.lookup_object(signal.value);
  require_vhpi_object(
      root_metadata && entity_metadata && signal_metadata
          && root_metadata.value.live_children == 1
          && entity_metadata.value.parent == root.value
          && entity_metadata.value.kind == VhdlVhpiObjectKind::Entity
          && entity_metadata.value.live_children == 1
          && signal_metadata.value.parent == entity.value
          && signal_metadata.value.kind == VhdlVhpiObjectKind::Signal
          && root_metadata.value.ordinal < entity_metadata.value.ordinal
          && entity_metadata.value.ordinal < signal_metadata.value.ordinal,
      "VHPI object metadata preserves kind, parent, children, and order");
  require_vhpi_object(
      registry.release_object(root.value)
          == VhdlVhpiObjectError::HasChildren,
      "VHPI registry requires leaf-first object release");

  const std::array initial_objects{entity.value, signal.value};
  const auto iterator = registry.create_iterator(initial_objects);
  require_vhpi_object(
      iterator
          && registry.lookup_object(iterator.value).error
              == VhdlVhpiObjectError::InvalidHandle
          && registry.scan(signal.value).error
              == VhdlVhpiIteratorError::InvalidHandle
          && other_registry.scan(iterator.value).error
              == VhdlVhpiIteratorError::CrossSimulation,
      "VHPI object and iterator identities remain disjoint and owned");
  require_vhpi_object(
      other_registry.create_iterator(initial_objects).error
          == VhdlVhpiIteratorError::CrossSimulation,
      "VHPI iterator creation rejects cross-simulation objects");
  require_vhpi_object(
      registry.scan(iterator.value).value == entity.value
          && registry.scan(iterator.value).value == signal.value
          && registry.scan(iterator.value).error
              == VhdlVhpiIteratorError::Exhausted,
      "VHPI iterator scans its stable snapshot then reports exhaustion");
  require_vhpi_object(
      registry.release_iterator(iterator.value)
              == VhdlVhpiIteratorError::None
          && registry.scan(iterator.value).error
              == VhdlVhpiIteratorError::ReleasedHandle,
      "VHPI released iterator is diagnosed before slot reuse");
  const std::array one_object{signal.value};
  const auto replacement_iterator = registry.create_iterator(one_object);
  require_vhpi_object(
      replacement_iterator && replacement_iterator.value != iterator.value
          && registry.scan(iterator.value).error
              == VhdlVhpiIteratorError::StaleHandle,
      "VHPI reused iterator slot advances its generation");

  require_vhpi_object(
      registry.release_object(signal.value) == VhdlVhpiObjectError::None
          && registry.lookup_object(signal.value).error
              == VhdlVhpiObjectError::ReleasedHandle,
      "VHPI released object is diagnosed before slot reuse");
  require_vhpi_object(
      registry.scan(replacement_iterator.value).error
          == VhdlVhpiIteratorError::ReleasedHandle,
      "VHPI iterator reports a released snapshotted object");
  const auto variable =
      registry.create_object(VhdlVhpiObjectKind::Variable, entity.value);
  require_vhpi_object(
      variable && variable.value != signal.value
          && registry.lookup_object(signal.value).error
              == VhdlVhpiObjectError::StaleHandle
          && registry.scan(replacement_iterator.value).error
              == VhdlVhpiIteratorError::StaleHandle,
      "VHPI reused object slot advances its generation");
  require_vhpi_object(
      registry.release_iterator(replacement_iterator.value)
              == VhdlVhpiIteratorError::None
          && registry.release_object(variable.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(entity.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(root.value)
              == VhdlVhpiObjectError::None,
      "VHPI object and iterator registries tear down independently");
}

}  // namespace fsim::tests::runtime
