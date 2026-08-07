// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_type.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {

namespace {

void require_vhpi_type(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult named_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const char* const name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

fsim::runtime::VhdlVhpiConstraint range_constraint(
    const std::int64_t left,
    const std::int64_t right,
    const fsim::runtime::VhdlVhpiDirection direction,
    const bool is_null = false) {
  return {
      fsim::runtime::VhdlVhpiConstraintKind::Range,
      fsim::runtime::VhdlVhpiRange{left, right, direction, is_null},
      {},
  };
}

}  // namespace

void test_vhdl_vhpi_types_and_constraints() {
  using fsim::runtime::VhdlVhpiConstraint;
  using fsim::runtime::VhdlVhpiConstraintKind;
  using fsim::runtime::VhdlVhpiDirection;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiScalarKind;
  using fsim::runtime::VhdlVhpiTypeDescriptor;
  using fsim::runtime::VhdlVhpiTypeError;
  using fsim::runtime::VhdlVhpiTypeSystem;

  VhdlVhpiObjectRegistry objects{501};
  VhdlVhpiObjectRegistry others{502};
  const auto root = named_object(objects, VhdlVhpiObjectKind::Root, 0, "work");
  const auto other_root =
      named_object(others, VhdlVhpiObjectKind::Root, 0, "other");
  const auto resolver = named_object(
      objects, VhdlVhpiObjectKind::Subprogram, root.value, "resolve");
  const auto integer_type =
      named_object(objects, VhdlVhpiObjectKind::Type, root.value, "integer_t");
  const auto positive = named_object(
      objects, VhdlVhpiObjectKind::Subtype, root.value, "positive_t");
  const auto small_positive = named_object(
      objects, VhdlVhpiObjectKind::Subtype, root.value, "small_positive_t");
  const auto resolved_bit =
      named_object(objects, VhdlVhpiObjectKind::Type, root.value, "resolved_t");
  const auto matrix =
      named_object(objects, VhdlVhpiObjectKind::Type, root.value, "matrix_t");
  const auto signal =
      named_object(objects, VhdlVhpiObjectKind::Signal, root.value, "data");
  const auto variable =
      named_object(objects, VhdlVhpiObjectKind::Variable, root.value, "count");
  require_vhpi_type(
      root && other_root && resolver && integer_type && positive
          && small_positive && resolved_bit && matrix && signal && variable,
      "VHPI type fixture hierarchy creation failed");

  VhdlVhpiTypeSystem types{objects};
  VhdlVhpiTypeSystem other_types{others};
  const auto integer_descriptor = VhdlVhpiTypeDescriptor{
      VhdlVhpiScalarKind::Integer,
      0,
      range_constraint(
          -2147483648LL, 2147483647LL, VhdlVhpiDirection::To),
      false,
      0,
  };
  require_vhpi_type(
      types.publish(integer_type.value, integer_descriptor)
          == VhdlVhpiTypeError::None,
      "VHPI base scalar type publication failed");
  const auto positive_descriptor = VhdlVhpiTypeDescriptor{
      VhdlVhpiScalarKind::Integer,
      integer_type.value,
      range_constraint(1, 100, VhdlVhpiDirection::To),
      false,
      0,
  };
  require_vhpi_type(
      types.publish(positive.value, positive_descriptor)
              == VhdlVhpiTypeError::None
          && types.publish(
                 small_positive.value,
                 VhdlVhpiTypeDescriptor{
                     VhdlVhpiScalarKind::Integer,
                     positive.value,
                     range_constraint(10, 1, VhdlVhpiDirection::Downto),
                     false,
                     0})
              == VhdlVhpiTypeError::None
          && types.base_type(small_positive.value).value
              == integer_type.value,
      "VHPI subtype chain and canonical base identity failed");
  require_vhpi_type(
      types.publish(
          resolved_bit.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Bit,
              0,
              {},
              true,
              resolver.value})
          == VhdlVhpiTypeError::None,
      "VHPI resolved scalar type publication failed");

  VhdlVhpiConstraint matrix_constraint{
      VhdlVhpiConstraintKind::Array,
      std::nullopt,
      {
          range_constraint(7, 0, VhdlVhpiDirection::Downto),
          VhdlVhpiConstraint{
              VhdlVhpiConstraintKind::Record,
              std::nullopt,
              {
                  range_constraint(0, 3, VhdlVhpiDirection::To),
                  range_constraint(4, 1, VhdlVhpiDirection::Downto),
              }},
      }};
  const auto matrix_descriptor = VhdlVhpiTypeDescriptor{
      VhdlVhpiScalarKind::Integer, 0, matrix_constraint, false, 0};
  require_vhpi_type(
      types.publish(matrix.value, matrix_descriptor)
          == VhdlVhpiTypeError::None,
      "VHPI recursive constraint publication failed");
  matrix_constraint.children.clear();
  const auto queried_matrix = types.query(matrix.value);
  require_vhpi_type(
      queried_matrix
          && queried_matrix.descriptor.constraint.children.size() == 2
          && queried_matrix.descriptor.constraint.children[1].children.size()
              == 2,
      "VHPI type system owns an immutable recursive descriptor snapshot");

  require_vhpi_type(
      types.bind_declaration(signal.value, positive.value)
              == VhdlVhpiTypeError::None
          && types.bind_declaration(variable.value, resolved_bit.value)
              == VhdlVhpiTypeError::None,
      "VHPI declaration type binding failed");
  const auto signal_type = types.declaration_type(signal.value);
  require_vhpi_type(
      signal_type && signal_type.type == positive.value
          && signal_type.descriptor.base_type == integer_type.value
          && signal_type.descriptor.constraint.range
          && signal_type.descriptor.constraint.range->left == 1
          && signal_type.descriptor.constraint.range->right == 100,
      "VHPI declaration query lost subtype or range identity");
  require_vhpi_type(
      types.bind_declaration(signal.value, integer_type.value)
              == VhdlVhpiTypeError::AlreadyPublished
          && types.bind_declaration(root.value, integer_type.value)
              == VhdlVhpiTypeError::InvalidDeclaration
          && types.declaration_type(resolver.value).error
              == VhdlVhpiTypeError::NotFound,
      "VHPI declaration binding negatives were not deterministic");

  const auto bad_type =
      named_object(objects, VhdlVhpiObjectKind::Type, root.value, "bad_t");
  const auto bad_subtype =
      named_object(objects, VhdlVhpiObjectKind::Subtype, root.value, "bad_s");
  require_vhpi_type(
      types.publish(
          bad_type.value,
          VhdlVhpiTypeDescriptor{
              static_cast<VhdlVhpiScalarKind>(99), 0, {}, false, 0})
          == VhdlVhpiTypeError::InvalidScalarKind,
      "VHPI unknown scalar category was accepted");
  require_vhpi_type(
      types.publish(
          bad_subtype.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Integer, 0, {}, false, 0})
          == VhdlVhpiTypeError::InvalidBaseType,
      "VHPI subtype without a base type was accepted");
  require_vhpi_type(
      types.publish(
          bad_type.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Integer,
              0,
              range_constraint(9, 1, VhdlVhpiDirection::To),
              false,
              0})
          == VhdlVhpiTypeError::InvalidRange,
      "VHPI inconsistent range direction was accepted");
  require_vhpi_type(
      types.publish(
          bad_type.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Integer,
              0,
              range_constraint(9, 1, VhdlVhpiDirection::To, true),
              false,
              0})
          == VhdlVhpiTypeError::None,
      "VHPI explicit null range was rejected");

  const auto unresolved =
      named_object(objects, VhdlVhpiObjectKind::Type, root.value, "unresolved");
  require_vhpi_type(
      types.publish(
          unresolved.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Bit, 0, {}, true, signal.value})
          == VhdlVhpiTypeError::InvalidResolution,
      "VHPI non-subprogram resolution object was accepted");
  require_vhpi_type(
      other_types.publish(
          other_root.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Bit, 0, {}, false, 0})
          == VhdlVhpiTypeError::InvalidTypeObject
          && types.query(other_root.value).error
              == VhdlVhpiTypeError::CrossSimulation,
      "VHPI type ownership boundaries were not enforced");

  const auto deep_type =
      named_object(objects, VhdlVhpiObjectKind::Type, root.value, "deep_t");
  VhdlVhpiConstraint deep;
  for (std::size_t depth = 0; depth < 65; ++depth) {
    deep = VhdlVhpiConstraint{
        VhdlVhpiConstraintKind::Array,
        std::nullopt,
        {std::move(deep)}};
  }
  require_vhpi_type(
      types.publish(
          deep_type.value,
          VhdlVhpiTypeDescriptor{
              VhdlVhpiScalarKind::Integer, 0, deep, false, 0})
          == VhdlVhpiTypeError::DepthLimit,
      "VHPI recursive constraint depth limit was not enforced");

  require_vhpi_type(
      types.publish(integer_type.value, integer_descriptor)
          == VhdlVhpiTypeError::AlreadyPublished,
      "VHPI canonical type identity accepted duplicate publication");
}

}  // namespace fsim::tests::runtime
