// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_callback.hpp"
#include "fsim/runtime/vhpi_value.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {

namespace {

using namespace fsim::runtime;

void require_negative(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

VhdlVhpiObjectResult named_object(
    VhdlVhpiObjectRegistry& objects,
    const VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(
      VhdlVhpiObjectDescriptor{kind, parent, name, {}, std::nullopt});
}

}  // namespace

void test_vhdl_vhpi_negative_matrix() {
  VhdlVhpiObjectRegistry objects{1'800};
  VhdlVhpiObjectRegistry foreign_objects{1'801};
  const auto root =
      named_object(objects, VhdlVhpiObjectKind::Root, 0, "work");
  const auto foreign_root = named_object(
      foreign_objects, VhdlVhpiObjectKind::Root, 0, "foreign");
  const auto enumeration_type = named_object(
      objects, VhdlVhpiObjectKind::Type, root.value, "state_t");
  const auto enumeration_value = named_object(
      objects, VhdlVhpiObjectKind::Variable, root.value, "state");
  const auto signal = named_object(
      objects, VhdlVhpiObjectKind::Signal, root.value, "data");
  const auto process = named_object(
      objects, VhdlVhpiObjectKind::Process, root.value, "producer");
  require_negative(
      root && foreign_root && enumeration_type && enumeration_value
          && signal && process,
      "VHPI negative matrix fixture construction failed");

  std::uint32_t cases{};
  const auto expect = [&](const bool condition, const char* message) {
    ++cases;
    require_negative(condition, message);
  };
  expect(
      objects.lookup_object(1).error
          == VhdlVhpiObjectError::InvalidHandle,
      "VHPI negative matrix accepted an untagged handle");
  expect(
      objects.lookup_object(foreign_root.value).error
          == VhdlVhpiObjectError::CrossSimulation,
      "VHPI negative matrix accepted a cross-owner handle");

  const auto released = named_object(
      objects, VhdlVhpiObjectKind::Variable, root.value, "temporary");
  const std::array released_span{released.value};
  const auto iterator = objects.create_iterator(released_span);
  expect(
      released && iterator
          && objects.release_object(released.value)
              == VhdlVhpiObjectError::None
          && objects.lookup_object(released.value).error
              == VhdlVhpiObjectError::ReleasedHandle
          && objects.scan(iterator.value).error
              == VhdlVhpiIteratorError::ReleasedHandle,
      "VHPI negative matrix lost released object/iterator state");
  const auto replacement = named_object(
      objects, VhdlVhpiObjectKind::Variable, root.value, "temporary");
  expect(
      replacement && replacement.value != released.value
          && objects.lookup_object(released.value).error
              == VhdlVhpiObjectError::StaleHandle
          && objects.scan(iterator.value).error
              == VhdlVhpiIteratorError::StaleHandle
          && objects.release_iterator(iterator.value)
              == VhdlVhpiIteratorError::None,
      "VHPI negative matrix lost stale generation diagnostics");

  VhdlVhpiTypeSystem types{objects};
  expect(
      types.publish(
          enumeration_type.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Enumeration, 0, {}, false, 0})
          == VhdlVhpiTypeError::None
          && types.bind_declaration(
                 enumeration_value.value, enumeration_type.value)
              == VhdlVhpiTypeError::None,
      "VHPI negative matrix enum fixture publication failed");
  VhdlVhpiValueSystem values{objects, types};
  expect(
      values.bind(
          enumeration_value.value,
          {enumeration_type.value, {"idle", "run"}, {}, 0},
          VhdlVhpiEnumerationValue{1})
          == VhdlVhpiValueError::None,
      "VHPI negative matrix enum value binding failed");
  std::array<char, 2> short_buffer{'x', 'x'};
  const auto short_read = values.enumeration_literal(
      enumeration_value.value, short_buffer);
  expect(
      short_read.error == VhdlVhpiValueError::BufferTooSmall
          && short_read.required_size == 3
          && short_buffer == std::array<char, 2>{'x', 'x'},
      "VHPI negative matrix short buffer changed caller storage");
  expect(
      values.write(
          enumeration_value.value, VhdlVhpiEnumerationValue{2})
              == VhdlVhpiValueError::InvalidPosition
          && std::get<VhdlVhpiEnumerationValue>(
                 values.read(enumeration_value.value).value)
                 .position
              == 1,
      "VHPI negative matrix rejected write changed retained state");

  const auto bad_profile_value = named_object(
      objects, VhdlVhpiObjectKind::Variable, root.value, "bad_state");
  expect(
      types.bind_declaration(
          bad_profile_value.value, enumeration_type.value)
              == VhdlVhpiTypeError::None
          && values.bind(
                 bad_profile_value.value,
                 {enumeration_type.value, {"same", "same"}, {}, 0},
                 VhdlVhpiEnumerationValue{0})
              == VhdlVhpiValueError::InvalidProfile
          && values.read(bad_profile_value.value).error
              == VhdlVhpiValueError::NotBound,
      "VHPI negative matrix malformed profile was partially published");

  const auto deep_type = named_object(
      objects, VhdlVhpiObjectKind::Type, root.value, "deep_t");
  VhdlVhpiConstraint deep;
  for (std::size_t depth = 0; depth < 65; ++depth) {
    deep = VhdlVhpiConstraint{
        VhdlVhpiConstraintKind::Array,
        std::nullopt,
        {std::move(deep)}};
  }
  expect(
      types.publish(
          deep_type.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Integer, 0, deep, false, 0})
              == VhdlVhpiTypeError::DepthLimit
          && types.query(deep_type.value).error
              == VhdlVhpiTypeError::NotFound,
      "VHPI negative matrix recursive resource rejection published state");

  Scheduler scheduler;
  VhdlVhpiCallbackSystem callbacks{objects, scheduler};
  std::uint64_t remover_identity{};
  std::uint64_t peer_identity{};
  std::uint32_t peer_calls{};
  const auto remover = callbacks.register_callback({
      VhdlVhpiCallbackKind::StartOfSimulation,
      0,
      true,
      0,
      [&](const auto&) {
        require_negative(
            callbacks.remove_callback(remover_identity)
                    == VhdlVhpiCallbackError::None
                && callbacks.remove_callback(peer_identity)
                    == VhdlVhpiCallbackError::None,
            "VHPI negative matrix callback removal failed");
      },
  });
  const auto peer = callbacks.register_callback({
      VhdlVhpiCallbackKind::StartOfSimulation,
      0,
      true,
      0,
      [&](const auto&) { ++peer_calls; },
  });
  remover_identity = remover.value.identity;
  peer_identity = peer.value.identity;
  expect(
      remover && peer
          && callbacks.publish(VhdlVhpiCallbackKind::StartOfSimulation)
              == VhdlVhpiCallbackError::None
          && peer_calls == 0
          && callbacks.remove_callback(peer_identity)
              == VhdlVhpiCallbackError::NotActive,
      "VHPI negative matrix peer removal was not immediate");

  bool nested{};
  std::uint32_t recursive_calls{};
  const auto recursive = callbacks.register_callback({
      VhdlVhpiCallbackKind::Event,
      root.value,
      true,
      0,
      [&](const auto&) {
        ++recursive_calls;
        if (!nested) {
          nested = true;
          require_negative(
              callbacks.publish(
                  VhdlVhpiCallbackKind::Event,
                  VhdlVhpiEventData{
                      root.value, 2, std::nullopt, {}, std::nullopt, 0})
                  == VhdlVhpiCallbackError::None,
              "VHPI negative matrix recursive callback publish failed");
        } else {
          throw std::runtime_error("contained negative-matrix callback");
        }
      },
  });
  expect(
      recursive
          && callbacks.publish(
                 VhdlVhpiCallbackKind::Event,
                 VhdlVhpiEventData{
                     root.value, 1, std::nullopt, {}, std::nullopt, 0})
              == VhdlVhpiCallbackError::None
          && recursive_calls == 2
          && callbacks.status(recursive.value.identity).value.status
              == VhdlVhpiCallbackStatus::CallbackFailed,
      "VHPI negative matrix did not contain recursive callback failure");
  expect(
      callbacks
              .register_callback({
                  VhdlVhpiCallbackKind::Signal,
                  foreign_root.value,
                  true,
                  0,
                  [](const auto&) {},
              })
              .error == VhdlVhpiCallbackError::CrossSimulation
          && callbacks
                 .register_callback({
                     VhdlVhpiCallbackKind::Signal,
                     process.value,
                     true,
                     0,
                     [](const auto&) {},
                 })
                 .error == VhdlVhpiCallbackError::InvalidObject,
      "VHPI negative matrix callback ownership/profile checks diverged");

  require_negative(cases == 13, "VHPI negative matrix case count changed");
}

}  // namespace fsim::tests::runtime
