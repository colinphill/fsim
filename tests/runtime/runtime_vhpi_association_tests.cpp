// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_association.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_association(
    const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult association_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

fsim::runtime::VhdlVhpiAssociationProfile association_profile(
    const fsim::runtime::VhdlVhpiAssociationKind kind,
    const fsim_vhpi_handle_v1 formal,
    const fsim_vhpi_handle_v1 actual,
    const fsim::runtime::VhdlVhpiActualKind actual_kind,
    const fsim::runtime::VhdlVhpiAssociationMode mode,
    const fsim::runtime::VhdlVhpiAssociationClass object_class,
    const std::uint64_t object_user_data = 0,
    const std::uint64_t call_user_data = 0) {
  return {kind,
          formal,
          actual,
          actual_kind,
          mode,
          object_class,
          object_user_data,
          call_user_data};
}

}  // namespace

void test_vhdl_vhpi_associations() {
  using fsim::runtime::VhdlVhpiActualKind;
  using fsim::runtime::VhdlVhpiAssociationClass;
  using fsim::runtime::VhdlVhpiAssociationError;
  using fsim::runtime::VhdlVhpiAssociationKind;
  using fsim::runtime::VhdlVhpiAssociationMode;
  using fsim::runtime::VhdlVhpiAssociationProfile;
  using fsim::runtime::VhdlVhpiAssociationSystem;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;

  VhdlVhpiObjectRegistry objects{1401};
  const auto root = association_object(
      objects, VhdlVhpiObjectKind::Root, 0, "work");
  const auto second_root = association_object(
      objects, VhdlVhpiObjectKind::Root, 0, "other");
  require_vhpi_association(
      root && second_root, "VHPI association root creation failed");
  const auto create = [&](const VhdlVhpiObjectKind kind,
                          const fsim_vhpi_handle_v1 parent,
                          const std::string& name) {
    const auto object = association_object(objects, kind, parent, name);
    require_vhpi_association(
        static_cast<bool>(object),
        "VHPI association object creation failed");
    return object.value;
  };
  const auto owner = create(VhdlVhpiObjectKind::Component, root.value, "u1");
  const auto generic_formal = create(
      VhdlVhpiObjectKind::Constant, owner, "width");
  const auto port_formal = create(
      VhdlVhpiObjectKind::Signal, owner, "data");
  const auto open_formal = create(
      VhdlVhpiObjectKind::Signal, owner, "ready");
  const auto disconnected_formal = create(
      VhdlVhpiObjectKind::Signal, owner, "unused");
  const auto generic_actual = create(
      VhdlVhpiObjectKind::Constant, root.value, "actual_width");
  const auto port_actual = create(
      VhdlVhpiObjectKind::Signal, root.value, "actual_data");

  const std::array profiles{
      association_profile(
          VhdlVhpiAssociationKind::Generic,
          generic_formal,
          generic_actual,
          VhdlVhpiActualKind::Object,
          VhdlVhpiAssociationMode::In,
          VhdlVhpiAssociationClass::Constant,
          0x11,
          0x21),
      association_profile(
          VhdlVhpiAssociationKind::Port,
          port_formal,
          port_actual,
          VhdlVhpiActualKind::Object,
          VhdlVhpiAssociationMode::InOut,
          VhdlVhpiAssociationClass::Signal,
          0x12,
          0x22),
      association_profile(
          VhdlVhpiAssociationKind::Port,
          open_formal,
          0,
          VhdlVhpiActualKind::Open,
          VhdlVhpiAssociationMode::Out,
          VhdlVhpiAssociationClass::Signal),
      association_profile(
          VhdlVhpiAssociationKind::Port,
          disconnected_formal,
          0,
          VhdlVhpiActualKind::Disconnected,
          VhdlVhpiAssociationMode::In,
          VhdlVhpiAssociationClass::Signal)};
  VhdlVhpiAssociationSystem associations{objects};
  require_vhpi_association(
      associations.publish(owner, profiles)
              == VhdlVhpiAssociationError::None
          && associations.publish(owner, profiles)
              == VhdlVhpiAssociationError::AlreadyPublished,
      "VHPI association publication/republication failed");
  const auto generics = associations.associations(
      owner, VhdlVhpiAssociationKind::Generic);
  const auto ports = associations.associations(
      owner, VhdlVhpiAssociationKind::Port);
  require_vhpi_association(
      generics && ports && generics.value.size() == 1
          && ports.value.size() == 3
          && generics.value[0].owner == owner
          && generics.value[0].root == root.value
          && generics.value[0].formal == generic_formal
          && generics.value[0].actual == generic_actual
          && generics.value[0].mode == VhdlVhpiAssociationMode::In
          && generics.value[0].object_class
              == VhdlVhpiAssociationClass::Constant
          && ports.value[0].ordinal < ports.value[1].ordinal
          && ports.value[1].actual_kind == VhdlVhpiActualKind::Open
          && ports.value[2].actual_kind
              == VhdlVhpiActualKind::Disconnected,
      "VHPI association query lost exact metadata or stable order");
  const auto generic_identity = generics.value[0].identity;
  require_vhpi_association(
      associations.set_user_data(generic_identity, 0x31, 0x41)
              == VhdlVhpiAssociationError::None
          && associations.association(generic_identity)
                 .value.object_user_data
              == 0x31
          && associations.association(generic_identity)
                 .value.call_user_data
              == 0x41,
      "VHPI association object/call user data did not round-trip");

  const auto cross_owner = create(
      VhdlVhpiObjectKind::Component, root.value, "cross_owner");
  const auto cross_formal = create(
      VhdlVhpiObjectKind::Signal, cross_owner, "port");
  const auto cross_actual = create(
      VhdlVhpiObjectKind::Signal, second_root.value, "actual");
  const std::array cross_profile{association_profile(
      VhdlVhpiAssociationKind::Port,
      cross_formal,
      cross_actual,
      VhdlVhpiActualKind::Object,
      VhdlVhpiAssociationMode::In,
      VhdlVhpiAssociationClass::Signal)};
  require_vhpi_association(
      associations.publish(cross_owner, cross_profile)
              == VhdlVhpiAssociationError::CrossRoot
          && associations.associations(
                 cross_owner, VhdlVhpiAssociationKind::Port)
                 .error
              == VhdlVhpiAssociationError::NotFound,
      "VHPI cross-root rejection was not transactional");

  const auto invalid_owner = create(
      VhdlVhpiObjectKind::Signal, root.value, "invalid_owner");
  const auto duplicate_owner = create(
      VhdlVhpiObjectKind::Component, root.value, "duplicate_owner");
  const auto duplicate_formal = create(
      VhdlVhpiObjectKind::Signal, duplicate_owner, "port");
  const std::array duplicate_profiles{
      association_profile(
          VhdlVhpiAssociationKind::Port,
          duplicate_formal,
          port_actual,
          VhdlVhpiActualKind::Object,
          VhdlVhpiAssociationMode::In,
          VhdlVhpiAssociationClass::Signal),
      association_profile(
          VhdlVhpiAssociationKind::Port,
          duplicate_formal,
          0,
          VhdlVhpiActualKind::Open,
          VhdlVhpiAssociationMode::Out,
          VhdlVhpiAssociationClass::Signal)};
  require_vhpi_association(
      associations.publish(invalid_owner, profiles)
              == VhdlVhpiAssociationError::InvalidOwner
          && associations.publish(duplicate_owner, duplicate_profiles)
              == VhdlVhpiAssociationError::DuplicateFormal,
      "VHPI invalid owner or duplicate formal was accepted");

  VhdlVhpiObjectRegistry foreign_objects{1402};
  const auto foreign_root = association_object(
      foreign_objects, VhdlVhpiObjectKind::Root, 0, "foreign");
  VhdlVhpiAssociationSystem foreign{foreign_objects};
  require_vhpi_association(
      foreign.association(generic_identity).error
              == VhdlVhpiAssociationError::CrossSimulation
          && foreign.set_user_data(generic_identity, 0, 0)
              == VhdlVhpiAssociationError::CrossSimulation
          && associations.associations(
                 foreign_root.value, VhdlVhpiAssociationKind::Port)
                 .error
              == VhdlVhpiAssociationError::CrossSimulation,
      "VHPI association identities or owners crossed simulations");
}

}  // namespace fsim::tests::runtime
