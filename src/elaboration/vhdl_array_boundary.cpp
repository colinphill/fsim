// SPDX-License-Identifier: Apache-2.0
#include "vhdl_array_boundary.hpp"

#include <sstream>

namespace fsim::elaboration::elaboration_detail {

void append_vhdl_type_shape_identity(
    std::ostringstream& output,
    const frontend::Type& type) {
  output << "{domain=" << static_cast<unsigned>(type.domain)
         << ";signed=" << (type.is_signed ? 1 : 0)
         << ";spelling=" << type.spelling
         << ";named=" << type.named_type
         << ";nominal=" << type.nominal_type << ";width=";
  if (const auto width = type.width()) {
    output << *width;
  } else {
    output << '?';
  }
  if (type.packed_range) {
    output << ";packed=" << type.packed_range->left
           << ':' << type.packed_range->right
           << ':' << (type.packed_range->descending ? 1 : 0);
  }
  if (type.integer_range) {
    output << ";integer=" << type.integer_range->left
           << ':' << type.integer_range->right
           << ':' << (type.integer_range->descending ? 1 : 0);
  }
  if (type.enumeration_range) {
    output << ";enum=" << type.enumeration_range->left
           << ':' << type.enumeration_range->right
           << ':' << (type.enumeration_range->descending ? 1 : 0);
  }
  for (const auto& literal : type.enumeration_literals) {
    output << ";literal=" << literal;
  }
  for (const auto& member : type.packed_members) {
    output << ";member=" << member.name
           << ':' << static_cast<unsigned>(member.domain)
           << ':' << (member.is_signed ? 1 : 0)
           << ':' << member.spelling
           << ':' << member.lsb_offset << ':';
    if (const auto width = member.width()) {
      output << *width;
    } else {
      output << '?';
    }
    for (const auto& nested : member.nested_types) {
      output << ":nested=";
      append_vhdl_type_shape_identity(output, nested);
    }
  }
  if (type.vhdl_array) {
    const auto& array = *type.vhdl_array;
    output << ";array=" << array.index_subtype
           << ':' << array.element_spelling
           << ':' << array.element_named_type
           << ':' << static_cast<unsigned>(array.element_domain)
           << ':' << (array.unconstrained ? 1 : 0)
           << ":flat=";
    if (array.flat_width) {
      output << *array.flat_width;
    } else {
      output << '?';
    }
    for (const auto& dimension : array.dimensions) {
      output << ";dimension=" << dimension.index_subtype
             << ':' << (dimension.unconstrained ? 1 : 0)
             << ':' << (dimension.null ? 1 : 0)
             << ':' << dimension.stride;
      if (dimension.range) {
        output << ':' << dimension.range->left
               << ':' << dimension.range->right
               << ':' << (dimension.range->descending ? 1 : 0);
      } else {
        output << ":?";
      }
    }
    for (const auto& element : array.element_types) {
      output << ";element=";
      append_vhdl_type_shape_identity(output, element);
    }
  }
  output << '}';
}

bool vhdl_array_element_profile_matches(
    const frontend::Type& left,
    const frontend::Type& right) {
  if (left.domain != right.domain
      || left.is_signed != right.is_signed
      || left.width() != right.width()
      || left.nominal_type != right.nominal_type
      || left.packed_members.size() != right.packed_members.size()) {
    return false;
  }
  for (std::size_t index = 0;
       index < left.packed_members.size(); ++index) {
    const auto& left_member = left.packed_members[index];
    const auto& right_member = right.packed_members[index];
    if (left_member.name != right_member.name
        || left_member.domain != right_member.domain
        || left_member.is_signed != right_member.is_signed
        || left_member.width() != right_member.width()
        || left_member.lsb_offset != right_member.lsb_offset
        || left_member.nested_types.size()
            != right_member.nested_types.size()) {
      return false;
    }
    for (std::size_t nested_index = 0;
         nested_index < left_member.nested_types.size();
         ++nested_index) {
      if (!vhdl_array_element_profile_matches(
              left_member.nested_types[nested_index],
              right_member.nested_types[nested_index])) {
        return false;
      }
    }
  }
  return true;
}

bool vhdl_array_shape_matches(
    const frontend::VhdlArrayInfo& formal,
    const frontend::VhdlArrayInfo& actual,
    const bool allow_unconstrained) {
  if (formal.dimensions.size() != actual.dimensions.size()
      || formal.element_types.size() != actual.element_types.size()) {
    return false;
  }
  for (std::size_t index = 0;
       index < formal.dimensions.size(); ++index) {
    const auto& left = formal.dimensions[index];
    const auto& right = actual.dimensions[index];
    if (allow_unconstrained && left.unconstrained) {
      continue;
    }
    if (left.unconstrained != right.unconstrained
        || left.range.has_value() != right.range.has_value()
        || left.null != right.null
        || left.stride != right.stride) {
      return false;
    }
    if (left.range
        && (left.range->left != right.range->left
            || left.range->right != right.range->right
            || left.range->descending
                != right.range->descending)) {
      return false;
    }
  }
  for (std::size_t index = 0;
       index < formal.element_types.size(); ++index) {
    if (!vhdl_array_element_profile_matches(
            formal.element_types[index],
            actual.element_types[index])) {
      return false;
    }
  }
  return true;
}

std::string vhdl_array_shape_identity(
    const frontend::Type& type) {
  std::ostringstream output;
  output << "vhdl-array-shape-v2;type=";
  append_vhdl_type_shape_identity(output, type);
  return output.str();
}
} // namespace fsim::elaboration::elaboration_detail
