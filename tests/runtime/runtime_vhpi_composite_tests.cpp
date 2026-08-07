// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_composite.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {

namespace {

void require_vhpi_composite(
    const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult composite_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

fsim::runtime::VhdlVhpiCompositeValue integer_value(
    const std::int64_t value) {
  return {fsim::runtime::VhdlVhpiValuePayload{value}};
}

fsim::runtime::VhdlVhpiCompositeValue boolean_value(const bool value) {
  return {fsim::runtime::VhdlVhpiValuePayload{value}};
}

std::int64_t integer_of(
    const fsim::runtime::VhdlVhpiCompositeValue& value) {
  return std::get<std::int64_t>(
      std::get<fsim::runtime::VhdlVhpiValuePayload>(value.value));
}

}  // namespace

void test_vhdl_vhpi_composite_values() {
  using fsim::runtime::VhdlVhpiArrayValue;
  using fsim::runtime::VhdlVhpiCompositeError;
  using fsim::runtime::VhdlVhpiCompositeKind;
  using fsim::runtime::VhdlVhpiCompositeSystem;
  using fsim::runtime::VhdlVhpiCompositeTypeDescriptor;
  using fsim::runtime::VhdlVhpiCompositeValue;
  using fsim::runtime::VhdlVhpiConstraint;
  using fsim::runtime::VhdlVhpiConstraintKind;
  using fsim::runtime::VhdlVhpiDirection;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiRange;
  using fsim::runtime::VhdlVhpiRecordField;
  using fsim::runtime::VhdlVhpiRecordValue;
  using fsim::runtime::VhdlVhpiScalarKind;
  using fsim::runtime::VhdlVhpiTypeDescriptor;
  using fsim::runtime::VhdlVhpiTypeError;
  using fsim::runtime::VhdlVhpiTypeSystem;
  using fsim::runtime::VhdlVhpiValueSystem;

  VhdlVhpiObjectRegistry objects{701};
  const auto root =
      composite_object(objects, VhdlVhpiObjectKind::Root, 0, "work");
  require_vhpi_composite(
      static_cast<bool>(root), "VHPI composite root creation failed");
  VhdlVhpiTypeSystem types{objects};
  VhdlVhpiValueSystem scalars{objects, types};
  VhdlVhpiCompositeSystem composites{types, scalars};

  const auto publish_semantic =
      [&](const std::string& name,
          const VhdlVhpiScalarKind kind,
          const VhdlVhpiConstraint& constraint = {}) {
        const auto type = composite_object(
            objects, VhdlVhpiObjectKind::Type, root.value, name);
        require_vhpi_composite(
            static_cast<bool>(type),
            "VHPI composite type object creation failed");
        require_vhpi_composite(
            types.publish(
                type.value,
                VhdlVhpiTypeDescriptor{kind, 0, constraint, false, 0})
                == VhdlVhpiTypeError::None,
            "VHPI composite semantic type publication failed");
        return type.value;
      };
  const auto integer_type = publish_semantic(
      "integer_t",
      VhdlVhpiScalarKind::Integer,
      VhdlVhpiConstraint{
          VhdlVhpiConstraintKind::Range,
          VhdlVhpiRange{-100, 100, VhdlVhpiDirection::To, false},
          {}});
  const auto boolean_type =
      publish_semantic("boolean_t", VhdlVhpiScalarKind::Boolean);
  const auto matrix_type =
      publish_semantic("matrix_t", VhdlVhpiScalarKind::Array);
  const auto packet_type =
      publish_semantic("packet_t", VhdlVhpiScalarKind::Record);
  const auto packet_vector_type =
      publish_semantic("packet_vector_t", VhdlVhpiScalarKind::Array);
  const auto null_array_type =
      publish_semantic("null_array_t", VhdlVhpiScalarKind::Array);

  const std::vector<VhdlVhpiRange> matrix_dimensions{
      {-1, 0, VhdlVhpiDirection::To, false},
      {3, 2, VhdlVhpiDirection::Downto, false},
  };
  require_vhpi_composite(
      composites.publish_type(VhdlVhpiCompositeTypeDescriptor{
          matrix_type,
          VhdlVhpiCompositeKind::Array,
          false,
          matrix_dimensions,
          integer_type,
          {integer_type, {}, {}, 0},
          {}})
          == VhdlVhpiCompositeError::None,
      "VHPI constrained multidimensional array publication failed");
  require_vhpi_composite(
      composites.publish_type(VhdlVhpiCompositeTypeDescriptor{
          packet_type,
          VhdlVhpiCompositeKind::Record,
          false,
          {},
          0,
          {},
          {
              VhdlVhpiRecordField{"payload", matrix_type, {}},
              VhdlVhpiRecordField{
                  "valid", boolean_type, {boolean_type, {}, {}, 0}},
          }})
          == VhdlVhpiCompositeError::None,
      "VHPI recursive record publication failed");
  require_vhpi_composite(
      composites.publish_type(VhdlVhpiCompositeTypeDescriptor{
          packet_vector_type,
          VhdlVhpiCompositeKind::Array,
          true,
          {},
          packet_type,
          {},
          {}})
          == VhdlVhpiCompositeError::None,
      "VHPI unconstrained array publication failed");
  const std::vector<VhdlVhpiRange> null_dimensions{
      {3, 2, VhdlVhpiDirection::To, true}};
  require_vhpi_composite(
      composites.publish_type(VhdlVhpiCompositeTypeDescriptor{
          null_array_type,
          VhdlVhpiCompositeKind::Array,
          false,
          null_dimensions,
          integer_type,
          {integer_type, {}, {}, 0},
          {}})
          == VhdlVhpiCompositeError::None,
      "VHPI constrained null array publication failed");

  const auto declaration =
      [&](const std::string& name, const fsim_vhpi_handle_v1 type) {
        const auto object = composite_object(
            objects, VhdlVhpiObjectKind::Variable, root.value, name);
        require_vhpi_composite(
            static_cast<bool>(object),
            "VHPI composite declaration creation failed");
        require_vhpi_composite(
            types.bind_declaration(object.value, type)
                == VhdlVhpiTypeError::None,
            "VHPI composite declaration type binding failed");
        return object.value;
      };
  const auto matrix_object = declaration("matrix_value", matrix_type);
  const auto packet_object = declaration("packet_value", packet_type);
  const auto vector_object =
      declaration("packet_vector_value", packet_vector_type);
  const auto null_object = declaration("null_value", null_array_type);

  const VhdlVhpiCompositeValue matrix_value{VhdlVhpiArrayValue{
      matrix_dimensions,
      {
          integer_value(10),
          integer_value(11),
          integer_value(12),
          integer_value(13),
      }}};
  require_vhpi_composite(
      composites.bind(matrix_object, matrix_value)
              == VhdlVhpiCompositeError::None
          && composites.bind(
                 null_object,
                 VhdlVhpiCompositeValue{
                     VhdlVhpiArrayValue{null_dimensions, {}}})
              == VhdlVhpiCompositeError::None,
      "VHPI constrained or null array binding failed");

  const std::array<std::int64_t, 2> first_index{-1, 3};
  const std::array<std::int64_t, 2> second_index{-1, 2};
  const std::array<std::int64_t, 2> third_index{0, 3};
  const std::array<std::int64_t, 2> fourth_index{0, 2};
  require_vhpi_composite(
      composites.array_offset(matrix_object, first_index).value == 0
          && composites.array_offset(matrix_object, second_index).value == 1
          && composites.array_offset(matrix_object, third_index).value == 2
          && composites.array_offset(matrix_object, fourth_index).value == 3,
      "VHPI multidimensional declared-index mapping is incorrect");

  std::array<VhdlVhpiCompositeValue, 3> short_buffer{
      integer_value(90), integer_value(91), integer_value(92)};
  const auto short_read =
      composites.read_members(matrix_object, short_buffer);
  std::array<VhdlVhpiCompositeValue, 4> exact_buffer{};
  const auto exact_read =
      composites.read_members(matrix_object, exact_buffer);
  require_vhpi_composite(
      short_read.error == VhdlVhpiCompositeError::BufferTooSmall
          && short_read.required_size == 4
          && integer_of(short_buffer[0]) == 90
          && integer_of(short_buffer[2]) == 92
          && exact_read && exact_read.required_size == 4
          && integer_of(exact_buffer[0]) == 10
          && integer_of(exact_buffer[3]) == 13,
      "VHPI composite partial-buffer contract failed");

  require_vhpi_composite(
      composites.write_array_element(
          matrix_object, fourth_index, integer_value(44))
              == VhdlVhpiCompositeError::None
          && composites.array_offset(matrix_object, fourth_index).value == 3,
      "VHPI transactional array element update failed");
  const auto updated_matrix = composites.read(matrix_object);
  require_vhpi_composite(
      updated_matrix
          && integer_of(
                 std::get<VhdlVhpiArrayValue>(updated_matrix.value.value)
                     .elements[3])
              == 44,
      "VHPI array element update did not publish");

  const VhdlVhpiCompositeValue packet_value{VhdlVhpiRecordValue{
      {matrix_value, boolean_value(true)}}};
  require_vhpi_composite(
      composites.bind(packet_object, packet_value)
          == VhdlVhpiCompositeError::None,
      "VHPI recursive record value binding failed");
  const std::vector<VhdlVhpiRange> vector_dimensions{
      {5, 4, VhdlVhpiDirection::Downto, false}};
  const VhdlVhpiCompositeValue vector_value{VhdlVhpiArrayValue{
      vector_dimensions,
      {
          packet_value,
          VhdlVhpiCompositeValue{VhdlVhpiRecordValue{
              {matrix_value, boolean_value(false)}}},
      }}};
  require_vhpi_composite(
      composites.bind(vector_object, vector_value)
          == VhdlVhpiCompositeError::None,
      "VHPI runtime-constrained array of records binding failed");
  const std::array<std::int64_t, 1> vector_last{4};
  require_vhpi_composite(
      composites.array_offset(vector_object, vector_last).value == 1,
      "VHPI unconstrained descending index mapping failed");

  const std::array<std::int64_t, 2> outside{1, 2};
  require_vhpi_composite(
      composites.write_array_element(
          matrix_object, outside, integer_value(99))
              == VhdlVhpiCompositeError::InvalidIndex
          && composites.write_array_element(
                 matrix_object, first_index, boolean_value(true))
              == VhdlVhpiCompositeError::TypeMismatch
          && composites.write(
                 matrix_object,
                 VhdlVhpiCompositeValue{VhdlVhpiArrayValue{
                     matrix_dimensions,
                     {integer_value(1)}}})
              == VhdlVhpiCompositeError::InvalidShape,
      "VHPI invalid composite updates were accepted");
  const auto retained = composites.read(matrix_object);
  require_vhpi_composite(
      retained
          && integer_of(
                 std::get<VhdlVhpiArrayValue>(retained.value.value)
                     .elements[0])
              == 10
          && integer_of(
                 std::get<VhdlVhpiArrayValue>(retained.value.value)
                     .elements[3])
              == 44,
      "VHPI rejected composite update changed retained state");

  const auto bad_record_type =
      publish_semantic("bad_record_t", VhdlVhpiScalarKind::Record);
  require_vhpi_composite(
      composites.publish_type(VhdlVhpiCompositeTypeDescriptor{
          bad_record_type,
          VhdlVhpiCompositeKind::Record,
          false,
          {},
          0,
          {},
          {
              VhdlVhpiRecordField{
                  "Field", integer_type, {integer_type, {}, {}, 0}},
              VhdlVhpiRecordField{
                  "field", integer_type, {integer_type, {}, {}, 0}},
          }})
          == VhdlVhpiCompositeError::InvalidDescriptor,
      "VHPI case-insensitive duplicate record fields were accepted");
  require_vhpi_composite(
      composites.publish_type(VhdlVhpiCompositeTypeDescriptor{
          matrix_type,
          VhdlVhpiCompositeKind::Array,
          false,
          matrix_dimensions,
          integer_type,
          {integer_type, {}, {}, 0},
          {}})
          == VhdlVhpiCompositeError::AlreadyPublished,
      "VHPI canonical composite type accepted duplicate publication");
}

}  // namespace fsim::tests::runtime
