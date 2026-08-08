// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_value.hpp"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {

namespace {

void require_vhpi_value(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult value_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

}  // namespace

void test_vhdl_vhpi_scalar_values() {
  using fsim::runtime::VhdlVhpiAccessValue;
  using fsim::runtime::VhdlVhpiConstraint;
  using fsim::runtime::VhdlVhpiConstraintKind;
  using fsim::runtime::VhdlVhpiDirection;
  using fsim::runtime::VhdlVhpiEnumerationValue;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiPhysicalUnit;
  using fsim::runtime::VhdlVhpiPhysicalValue;
  using fsim::runtime::VhdlVhpiRange;
  using fsim::runtime::VhdlVhpiScalarKind;
  using fsim::runtime::VhdlVhpiTypeDescriptor;
  using fsim::runtime::VhdlVhpiTypeError;
  using fsim::runtime::VhdlVhpiTypeSystem;
  using fsim::runtime::VhdlVhpiValueError;
  using fsim::runtime::VhdlVhpiValueProfile;
  using fsim::runtime::VhdlVhpiValueSystem;

  VhdlVhpiObjectRegistry objects{601};
  const auto root = value_object(objects, VhdlVhpiObjectKind::Root, 0, "work");
  require_vhpi_value(
      static_cast<bool>(root), "VHPI value root creation failed");
  VhdlVhpiTypeSystem types{objects};

  const auto publish_type =
      [&](const std::string& name,
          const VhdlVhpiScalarKind kind,
          const VhdlVhpiConstraint& constraint = {}) {
        const auto type =
            value_object(objects, VhdlVhpiObjectKind::Type, root.value, name);
        require_vhpi_value(
            static_cast<bool>(type),
            "VHPI value type object creation failed");
        require_vhpi_value(
            types.publish(
                type.value,
                VhdlVhpiTypeDescriptor{kind, 0, constraint, false, 0})
                == VhdlVhpiTypeError::None,
            "VHPI value type publication failed");
        return type.value;
      };
  const auto boolean_type = publish_type("boolean_t", VhdlVhpiScalarKind::Boolean);
  const auto character_type =
      publish_type("character_t", VhdlVhpiScalarKind::Character);
  const auto integer_type = publish_type(
      "integer_t",
      VhdlVhpiScalarKind::Integer,
      VhdlVhpiConstraint{
          VhdlVhpiConstraintKind::Range,
          VhdlVhpiRange{-10, 10, VhdlVhpiDirection::To, false},
          {}});
  const auto real_type = publish_type("real_t", VhdlVhpiScalarKind::Real);
  const auto time_type = publish_type("time_t", VhdlVhpiScalarKind::Time);
  const auto enumeration_type =
      publish_type("state_t", VhdlVhpiScalarKind::Enumeration);
  const auto physical_type =
      publish_type("delay_t", VhdlVhpiScalarKind::Physical);
  const auto access_type =
      publish_type("integer_access_t", VhdlVhpiScalarKind::Access);

  const auto declaration =
      [&](const std::string& name, const fsim_vhpi_handle_v1 type) {
        const auto object = value_object(
            objects, VhdlVhpiObjectKind::Variable, root.value, name);
        require_vhpi_value(
            static_cast<bool>(object),
            "VHPI value declaration creation failed");
        require_vhpi_value(
            types.bind_declaration(object.value, type)
                == VhdlVhpiTypeError::None,
            "VHPI value declaration type binding failed");
        return object.value;
      };
  const auto boolean_value = declaration("boolean_value", boolean_type);
  const auto character_value = declaration("character_value", character_type);
  const auto integer_value = declaration("integer_value", integer_type);
  const auto real_value = declaration("real_value", real_type);
  const auto time_value = declaration("time_value", time_type);
  const auto enumeration_value =
      declaration("enumeration_value", enumeration_type);
  const auto physical_value = declaration("physical_value", physical_type);
  const auto access_value = declaration("access_value", access_type);
  const auto target = declaration("target", integer_type);
  const auto wrong_target = declaration("wrong_target", boolean_type);

  VhdlVhpiValueSystem values{objects, types};
  require_vhpi_value(
      values.bind(boolean_value, {boolean_type, {}, {}, 0}, true)
              == VhdlVhpiValueError::None
          && values.bind(
                 character_value, {character_type, {}, {}, 0}, U'\u03bb')
              == VhdlVhpiValueError::None
          && values.bind(integer_value, {integer_type, {}, {}, 0},
                 std::int64_t{-7})
              == VhdlVhpiValueError::None
          && values.bind(real_value, {real_type, {}, {}, 0}, 3.25)
              == VhdlVhpiValueError::None
          && values.bind(time_value, {time_type, {}, {}, 0},
                 std::uint64_t{UINT64_MAX})
              == VhdlVhpiValueError::None
          && values.bind(target, {integer_type, {}, {}, 0}, std::int64_t{4})
              == VhdlVhpiValueError::None
          && values.bind(wrong_target, {boolean_type, {}, {}, 0}, false)
              == VhdlVhpiValueError::None,
      "VHPI scalar value binding failed");

  VhdlVhpiValueProfile enumeration_profile{
      enumeration_type, {"idle", "run", "done"}, {}, 0};
  require_vhpi_value(
      values.bind(
          enumeration_value,
          enumeration_profile,
          VhdlVhpiEnumerationValue{1})
          == VhdlVhpiValueError::None,
      "VHPI enumeration value binding failed");
  enumeration_profile.enumeration_literals[1] = "changed";
  std::array<char, 2> short_buffer{'x', 'x'};
  const auto short_text =
      values.enumeration_literal(enumeration_value, short_buffer);
  std::array<char, 3> exact_buffer{};
  const auto exact_text =
      values.enumeration_literal(enumeration_value, exact_buffer);
  require_vhpi_value(
      short_text.error == VhdlVhpiValueError::BufferTooSmall
          && short_text.required_size == 3
          && short_buffer == std::array<char, 2>{'x', 'x'}
          && exact_text && exact_text.required_size == 3
          && std::string{exact_buffer.data(), exact_buffer.size()} == "run",
      "VHPI enumeration literal buffer contract lost ownership or wrote partial data");

  VhdlVhpiValueProfile physical_profile{
      physical_type,
      {},
      {
          VhdlVhpiPhysicalUnit{"fs", 1},
          VhdlVhpiPhysicalUnit{"ps", 1000},
          VhdlVhpiPhysicalUnit{"ns", 1000000},
      },
      0};
  require_vhpi_value(
      values.bind(
          physical_value, physical_profile, VhdlVhpiPhysicalValue{-4, 1})
              == VhdlVhpiValueError::None,
      "VHPI physical value binding failed");
  physical_profile.physical_units[1].name = "changed";
  const auto retained_profile = values.profile(physical_value);
  require_vhpi_value(
      retained_profile
          && retained_profile.value.physical_units[1].name == "ps"
          && retained_profile.value.physical_units[1].primary_multiplier
              == 1000,
      "VHPI physical unit metadata was not owned exactly");

  require_vhpi_value(
      values.bind(
          access_value,
          {access_type, {}, {}, integer_type},
          VhdlVhpiAccessValue{0})
              == VhdlVhpiValueError::None
          && std::get<VhdlVhpiAccessValue>(values.read(access_value).value)
                     .object
              == 0
          && values.write(access_value, VhdlVhpiAccessValue{target})
              == VhdlVhpiValueError::None
          && std::get<VhdlVhpiAccessValue>(values.read(access_value).value)
                     .object
              == target,
      "VHPI access null and designated-object transfer failed");

  require_vhpi_value(
      std::get<bool>(values.read(boolean_value).value)
          && std::get<char32_t>(values.read(character_value).value)
                 == U'\u03bb'
          && std::get<std::int64_t>(values.read(integer_value).value) == -7
          && std::get<double>(values.read(real_value).value) == 3.25
          && std::get<std::uint64_t>(values.read(time_value).value)
              == UINT64_MAX
          && std::get<VhdlVhpiPhysicalValue>(
                 values.read(physical_value).value)
                 .magnitude
              == -4,
      "VHPI scalar and physical reads were not lossless");

  require_vhpi_value(
      values.write(integer_value, std::int64_t{11})
              == VhdlVhpiValueError::RangeViolation
          && values.write(character_value, char32_t{0xd800})
              == VhdlVhpiValueError::RangeViolation
          && values.write(real_value, std::numeric_limits<double>::infinity())
              == VhdlVhpiValueError::RangeViolation
          && values.write(
                 enumeration_value, VhdlVhpiEnumerationValue{3})
              == VhdlVhpiValueError::InvalidPosition
          && values.write(physical_value, VhdlVhpiPhysicalValue{1, 3})
              == VhdlVhpiValueError::InvalidUnit
          && values.write(
                 access_value, VhdlVhpiAccessValue{wrong_target})
              == VhdlVhpiValueError::InvalidAccess
          && values.write(boolean_value, std::int64_t{1})
              == VhdlVhpiValueError::TypeMismatch,
      "VHPI malformed value writes were accepted");
  require_vhpi_value(
      std::get<std::int64_t>(values.read(integer_value).value) == -7
          && std::get<VhdlVhpiEnumerationValue>(
                 values.read(enumeration_value).value)
                 .position
              == 1
          && std::get<VhdlVhpiPhysicalValue>(
                 values.read(physical_value).value)
                 .unit_position
              == 1,
      "VHPI rejected writes changed retained values");

  const auto duplicate_enum =
      declaration("duplicate_enum", enumeration_type);
  const auto bad_physical = declaration("bad_physical", physical_type);
  require_vhpi_value(
      values.bind(
          duplicate_enum,
          {enumeration_type, {"same", "same"}, {}, 0},
          VhdlVhpiEnumerationValue{0})
              == VhdlVhpiValueError::InvalidProfile
          && values.bind(
                 bad_physical,
                 {physical_type, {}, {VhdlVhpiPhysicalUnit{"fs", 2}}, 0},
                 VhdlVhpiPhysicalValue{0, 0})
              == VhdlVhpiValueError::InvalidProfile
          && values.bind(
                 boolean_value, {boolean_type, {}, {}, 0}, false)
              == VhdlVhpiValueError::AlreadyBound,
      "VHPI malformed or duplicate value publication was accepted");
}

}  // namespace fsim::tests::runtime
