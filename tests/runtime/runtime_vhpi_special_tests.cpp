// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_special.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_special(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult special_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

}  // namespace

void test_vhdl_vhpi_special_values() {
  using fsim::runtime::PackedLogic9;
  using fsim::runtime::VhdlVhpiFileAccess;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiProtectedAccess;
  using fsim::runtime::VhdlVhpiResolverDescriptor;
  using fsim::runtime::VhdlVhpiScalarKind;
  using fsim::runtime::VhdlVhpiSpecialError;
  using fsim::runtime::VhdlVhpiSpecialSystem;
  using fsim::runtime::VhdlVhpiTypeDescriptor;
  using fsim::runtime::VhdlVhpiTypeError;
  using fsim::runtime::VhdlVhpiTypeSystem;

  VhdlVhpiObjectRegistry objects{801};
  const auto root =
      special_object(objects, VhdlVhpiObjectKind::Root, 0, "work");
  require_vhpi_special(
      static_cast<bool>(root), "VHPI special root creation failed");
  VhdlVhpiTypeSystem types{objects};

  const auto create =
      [&](const VhdlVhpiObjectKind kind, const std::string& name) {
        const auto object = special_object(objects, kind, root.value, name);
        require_vhpi_special(
            static_cast<bool>(object), "VHPI special object creation failed");
        return object.value;
      };
  const auto file_type = create(VhdlVhpiObjectKind::Type, "text_file_t");
  const auto protected_type =
      create(VhdlVhpiObjectKind::Type, "scoreboard_t");
  const auto logic_type = create(VhdlVhpiObjectKind::Type, "ulogic9_t");
  const auto resolver =
      create(VhdlVhpiObjectKind::Subprogram, "resolve_logic9");
  const auto resolved_logic_type =
      create(VhdlVhpiObjectKind::Type, "logic9_t");
  require_vhpi_special(
      types.publish(
          file_type,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::File, 0, {}, false, 0})
              == VhdlVhpiTypeError::None
          && types.publish(
                 protected_type,
                 VhdlVhpiTypeDescriptor{
                     VhdlVhpiScalarKind::Protected, 0, {}, false, 0})
              == VhdlVhpiTypeError::None
          && types.publish(
                 logic_type,
                 VhdlVhpiTypeDescriptor{
                     VhdlVhpiScalarKind::Logic9, 0, {}, false, 0})
              == VhdlVhpiTypeError::None
          && types.publish(
                 resolved_logic_type,
                 VhdlVhpiTypeDescriptor{
                     VhdlVhpiScalarKind::Logic9,
                     0,
                     {},
                     true,
                     resolver})
              == VhdlVhpiTypeError::None,
      "VHPI special semantic type publication failed");

  const auto file_object =
      create(VhdlVhpiObjectKind::File, "stimulus_file");
  const auto protected_object =
      create(VhdlVhpiObjectKind::Variable, "scoreboard");
  const auto logic_object =
      create(VhdlVhpiObjectKind::Signal, "logic_vector");
  const auto scalar_logic_object =
      create(VhdlVhpiObjectKind::Signal, "logic_scalar");
  const auto first_caller =
      create(VhdlVhpiObjectKind::Process, "first_caller");
  const auto second_caller =
      create(VhdlVhpiObjectKind::Process, "second_caller");
  require_vhpi_special(
      types.bind_declaration(file_object, file_type)
              == VhdlVhpiTypeError::None
          && types.bind_declaration(protected_object, protected_type)
              == VhdlVhpiTypeError::None
          && types.bind_declaration(logic_object, resolved_logic_type)
              == VhdlVhpiTypeError::None
          && types.bind_declaration(scalar_logic_object, logic_type)
              == VhdlVhpiTypeError::None,
      "VHPI special declaration binding failed");

  VhdlVhpiSpecialSystem special{objects, types};
  VhdlVhpiObjectRegistry foreign_objects{802};
  const auto foreign_root = special_object(
      foreign_objects, VhdlVhpiObjectKind::Root, 0, "foreign");
  require_vhpi_special(
      static_cast<bool>(foreign_root), "VHPI foreign root creation failed");
  VhdlVhpiTypeSystem foreign_types{foreign_objects};
  VhdlVhpiSpecialSystem foreign{foreign_objects, foreign_types};

  const auto opened = special.open_file(
      file_object, VhdlVhpiFileAccess::ReadWrite, "stimulus.vec");
  require_vhpi_special(
      opened && opened.value.identity != 0U
          && opened.value.declaration == file_object
          && opened.value.type == file_type
          && opened.value.logical_name == "stimulus.vec"
          && special
                 .query_file(
                     opened.value.identity, VhdlVhpiFileAccess::ReadOnly)
          && special
                 .query_file(
                     opened.value.identity, VhdlVhpiFileAccess::WriteOnly),
      "VHPI opaque read-write file publication failed");
  require_vhpi_special(
      special
              .query_file(
                  opened.value.identity, VhdlVhpiFileAccess::Append)
              .error
              == VhdlVhpiSpecialError::InvalidAccess
          && special
                 .open_file(
                     file_object,
                     VhdlVhpiFileAccess::ReadOnly,
                     "duplicate.vec")
                 .error
              == VhdlVhpiSpecialError::AlreadyOpen
          && foreign
                 .query_file(
                     opened.value.identity, VhdlVhpiFileAccess::ReadOnly)
                 .error
              == VhdlVhpiSpecialError::CrossSimulation,
      "VHPI file access or ownership checks failed");
  require_vhpi_special(
      special.close_file(opened.value.identity)
              == VhdlVhpiSpecialError::None
          && special
                 .query_file(
                     opened.value.identity, VhdlVhpiFileAccess::ReadOnly)
                 .error
              == VhdlVhpiSpecialError::NotOpen
          && special.close_file(opened.value.identity)
              == VhdlVhpiSpecialError::NotOpen,
      "VHPI file close state is not deterministic");
  const auto reopened = special.open_file(
      file_object, VhdlVhpiFileAccess::ReadOnly, "second.vec");
  require_vhpi_special(
      reopened && reopened.value.identity != opened.value.identity,
      "VHPI file identities were reused");

  require_vhpi_special(
      special.register_protected(protected_object)
              == VhdlVhpiSpecialError::None
          && special.register_protected(protected_object)
              == VhdlVhpiSpecialError::AlreadyBound,
      "VHPI protected object registration failed");
  const auto shared_first = special.acquire_protected(
      protected_object, first_caller, VhdlVhpiProtectedAccess::Shared);
  const auto shared_second = special.acquire_protected(
      protected_object, second_caller, VhdlVhpiProtectedAccess::Shared);
  require_vhpi_special(
      shared_first && shared_second
          && special
                 .acquire_protected(
                     protected_object,
                     first_caller,
                     VhdlVhpiProtectedAccess::Exclusive)
                 .error
              == VhdlVhpiSpecialError::Busy
          && special.release_protected(shared_first.value, second_caller)
              == VhdlVhpiSpecialError::InvalidAccess,
      "VHPI protected shared/exclusive access control failed");
  require_vhpi_special(
      special.release_protected(shared_first.value, first_caller)
              == VhdlVhpiSpecialError::None
          && special.release_protected(shared_second.value, second_caller)
              == VhdlVhpiSpecialError::None,
      "VHPI protected shared lease release failed");
  const auto exclusive = special.acquire_protected(
      protected_object, first_caller, VhdlVhpiProtectedAccess::Exclusive);
  require_vhpi_special(
      exclusive
          && special
                 .acquire_protected(
                     protected_object,
                     second_caller,
                     VhdlVhpiProtectedAccess::Shared)
                 .error
              == VhdlVhpiSpecialError::Busy
          && foreign
                 .release_protected(exclusive.value, first_caller)
              == VhdlVhpiSpecialError::CrossSimulation
          && special.release_protected(exclusive.value, first_caller)
              == VhdlVhpiSpecialError::None
          && special.release_protected(exclusive.value, first_caller)
              == VhdlVhpiSpecialError::InvalidResource,
      "VHPI protected exclusive identity handling failed");

  std::string provenance{"ieee.std_logic_1164.resolved/v1"};
  require_vhpi_special(
      special.publish_resolver(VhdlVhpiResolverDescriptor{
          resolved_logic_type, resolver, provenance})
              == VhdlVhpiSpecialError::None,
      "VHPI resolver provenance publication failed");
  provenance.assign("mutated");
  const auto resolver_query = special.resolver(resolved_logic_type);
  require_vhpi_special(
      resolver_query && resolver_query.value.subprogram == resolver
          && resolver_query.value.provenance
              == "ieee.std_logic_1164.resolved/v1"
          && special.publish_resolver(VhdlVhpiResolverDescriptor{
                 resolved_logic_type, resolver, "duplicate"})
              == VhdlVhpiSpecialError::AlreadyBound
          && special.publish_resolver(
                 VhdlVhpiResolverDescriptor{logic_type, resolver, "wrong"})
              == VhdlVhpiSpecialError::InvalidResolver,
      "VHPI resolver provenance or identity is incorrect");

  const auto all_states = PackedLogic9::from_msb_string("UX01ZWLH-");
  require_vhpi_special(
      special.bind_logic(logic_object, all_states)
              == VhdlVhpiSpecialError::None
          && special.bind_logic(logic_object, all_states)
              == VhdlVhpiSpecialError::AlreadyBound
          && special.bind_logic(
                 scalar_logic_object,
                 PackedLogic9::from_msb_string("U"))
              == VhdlVhpiSpecialError::None,
      "VHPI nine-state scalar/vector binding failed");
  std::array<std::uint8_t, 8> short_encoding{};
  short_encoding.fill(0xffU);
  const auto short_result =
      special.encode_logic(logic_object, short_encoding);
  std::array<std::uint8_t, 9> exact_encoding{};
  const auto exact_result =
      special.encode_logic(logic_object, exact_encoding);
  require_vhpi_special(
      short_result.error == VhdlVhpiSpecialError::BufferTooSmall
          && short_result.required_size == 9
          && short_encoding.front() == 0xffU
          && short_encoding.back() == 0xffU && exact_result
          && exact_result.required_size == 9
          && exact_encoding
              == std::array<std::uint8_t, 9>{0, 1, 2, 3, 4, 5, 6, 7, 8},
      "VHPI exact nine-state buffer encoding failed");
  require_vhpi_special(
      special.read_logic(logic_object)
              .value.to_msb_string()
              == "UX01ZWLH-"
          && special.write_logic(
                 logic_object,
                 PackedLogic9::from_msb_string("-HLWZ10XU"))
              == VhdlVhpiSpecialError::None
          && special.read_logic(logic_object)
                 .value.to_msb_string()
              == "-HLWZ10XU",
      "VHPI lossless nine-state transfer failed");
  require_vhpi_special(
      special.write_logic(
          logic_object, PackedLogic9::from_msb_string("UX"))
              == VhdlVhpiSpecialError::InvalidValue
          && special.read_logic(logic_object)
                 .value.to_msb_string()
              == "-HLWZ10XU"
          && special.bind_logic(
                 foreign_root.value,
                 PackedLogic9::from_msb_string("U"))
              == VhdlVhpiSpecialError::CrossSimulation,
      "VHPI rejected nine-state update changed retained state");
  require_vhpi_special(
      objects.release_object(logic_object)
              == fsim::runtime::VhdlVhpiObjectError::None
          && special.read_logic(logic_object).error
              == VhdlVhpiSpecialError::ReleasedHandle,
      "VHPI released logic identity remained readable");
}

}  // namespace fsim::tests::runtime
