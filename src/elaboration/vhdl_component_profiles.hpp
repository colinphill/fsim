// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fsim::elaboration::vhdl_component_detail {

inline std::string canonical_generic_name(
    const std::string_view name,
    const std::unordered_map<std::string, std::string>& names) {
  if (const auto direct = names.find(std::string{name});
      direct != names.end()) {
    return direct->second;
  }
  const auto separator = name.find('.');
  if (separator == std::string_view::npos) {
    return std::string{name};
  }
  if (const auto prefix =
          names.find(std::string{name.substr(0, separator)});
      prefix != names.end()) {
    return prefix->second + std::string{name.substr(separator)};
  }
  return std::string{name};
}

inline std::string expression_profile(
    const frontend::Expression& expression,
    const std::unordered_map<std::string, std::string>& names) {
  std::ostringstream output;
  output << static_cast<int>(expression.kind) << ':';
  if (expression.kind == frontend::ExpressionKind::Identifier) {
    output << canonical_generic_name(expression.text, names);
  } else {
    output << expression.text;
  }
  for (const auto& operand : expression.operands) {
    output << '(' << expression_profile(operand, names) << ')';
  }
  return output.str();
}

inline std::string type_profile(
    const frontend::Type& type,
    const std::unordered_map<std::string, std::string>& names) {
  std::ostringstream output;
  output << static_cast<int>(type.domain)
         << ";spelling="
         << canonical_generic_name(type.spelling, names)
         << ";named="
         << canonical_generic_name(type.named_type, names)
         << ";nominal=" << type.nominal_type
         << ";signed=" << type.is_signed;
  if (type.packed_range) {
    output << ";packed=" << type.packed_range->left << ':'
           << type.packed_range->right << ':'
           << type.packed_range->descending;
  } else if (type.packed_range_expression) {
    output << ";packed-expr="
           << expression_profile(
                  type.packed_range_expression->left, names)
           << ':'
           << expression_profile(
                  type.packed_range_expression->right, names)
           << ':'
           << type.packed_range_expression->descending.value_or(true);
  }
  if (type.integer_range) {
    output << ";integer=" << type.integer_range->left << ':'
           << type.integer_range->right << ':'
           << type.integer_range->descending;
  } else if (type.integer_range_expression) {
    output << ";integer-expr="
           << expression_profile(
                  type.integer_range_expression->left, names)
           << ':'
           << expression_profile(
                  type.integer_range_expression->right, names)
           << ':' << type.integer_range_expression->descending;
  }
  if (type.integer_base_range) {
    output << ";integer-base="
           << type.integer_base_range->left << ':'
           << type.integer_base_range->right << ':'
           << type.integer_base_range->descending;
  } else if (type.integer_base_range_expression) {
    output << ";integer-base-expr="
           << expression_profile(
                  type.integer_base_range_expression->left, names)
           << ':'
           << expression_profile(
                  type.integer_base_range_expression->right, names)
           << ':' << type.integer_base_range_expression->descending;
  }
  for (const auto& literal : type.enumeration_literals) {
    output << ";literal=" << literal;
  }
  if (type.enumeration_range) {
    output << ";enum=" << type.enumeration_range->left << ':'
           << type.enumeration_range->right << ':'
           << type.enumeration_range->descending;
  } else if (type.enumeration_range_expression) {
    output << ";enum-expr="
           << expression_profile(
                  type.enumeration_range_expression->left, names)
           << ':'
           << expression_profile(
                  type.enumeration_range_expression->right, names)
           << ':' << type.enumeration_range_expression->descending;
  }
  if (type.enumeration_base_range) {
    output << ";enum-base="
           << type.enumeration_base_range->left << ':'
           << type.enumeration_base_range->right << ':'
           << type.enumeration_base_range->descending;
  } else if (type.enumeration_base_range_expression) {
    output << ";enum-base-expr="
           << expression_profile(
                  type.enumeration_base_range_expression->left, names)
           << ':'
           << expression_profile(
                  type.enumeration_base_range_expression->right, names)
           << ':' << type.enumeration_base_range_expression->descending;
  }
  output << ";aggregate=" << static_cast<int>(type.packed_aggregate);
  for (const auto& member : type.packed_members) {
    output << ";member=" << member.name << ':'
           << static_cast<int>(member.domain) << ':'
           << canonical_generic_name(member.spelling, names)
           << ':' << member.is_signed
           << ':' << member.lsb_offset;
    if (member.packed_range) {
      output << ':' << member.packed_range->left << ':'
             << member.packed_range->right << ':'
             << member.packed_range->descending;
    } else if (member.packed_range_expression) {
      output << ":expr:"
             << expression_profile(
                    member.packed_range_expression->left, names)
             << ':'
             << expression_profile(
                    member.packed_range_expression->right, names)
             << ':'
             << member.packed_range_expression
                    ->descending.value_or(true);
    }
    for (const auto& nested : member.nested_types) {
      output << ":nested={" << type_profile(nested, names) << '}';
    }
  }
  if (type.vhdl_array) {
    const auto& array = *type.vhdl_array;
    output << ";array=" << array.index_subtype << ':'
           << canonical_generic_name(array.element_spelling, names)
           << ':'
           << canonical_generic_name(array.element_named_type, names)
           << ':' << static_cast<int>(array.element_domain)
           << ':' << array.unconstrained;
    if (array.index_base_range) {
      output << ':' << array.index_base_range->left << ':'
             << array.index_base_range->right << ':'
             << array.index_base_range->descending;
    }
    for (const auto& dimension : array.dimensions) {
      output << ";dimension="
             << canonical_generic_name(
                    dimension.index_subtype, names)
             << ':' << dimension.unconstrained;
      if (dimension.index_base_range) {
        output << ":base:" << dimension.index_base_range->left
               << ':' << dimension.index_base_range->right
               << ':' << dimension.index_base_range->descending;
      }
      if (dimension.constraint) {
        output << ":constraint:"
               << expression_profile(
                      dimension.constraint->left, names)
               << ':'
               << expression_profile(
                      dimension.constraint->right, names)
               << ':' << dimension.constraint->descending;
      }
      if (dimension.range) {
        output << ":range:" << dimension.range->left
               << ':' << dimension.range->right
               << ':' << dimension.range->descending
               << ":null:" << dimension.null
               << ":stride:" << dimension.stride;
      }
    }
    if (array.flat_width) {
      output << ";flat-width=" << *array.flat_width;
    }
    for (const auto& element : array.element_types) {
      output << ";element={" << type_profile(element, names) << '}';
    }
  }
  for (const auto& constraint : type.vhdl_array_constraints) {
    output << ";array-constraint="
           << expression_profile(constraint.left, names) << ':'
           << expression_profile(constraint.right, names) << ':'
           << constraint.descending;
  }
  return output.str();
}

}  // namespace fsim::elaboration::vhdl_component_detail
