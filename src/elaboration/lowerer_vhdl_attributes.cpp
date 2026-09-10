// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

const frontend::Type* Lowerer::vhdl_array_attribute_prefix_type(
    const Expression& expression) const {
  if (language_ != frontend::Language::Vhdl2008
      || expression.kind != ExpressionKind::Call
      || expression.operands.empty()
      || expression.operands.front().kind != ExpressionKind::Identifier) {
    return nullptr;
  }
  const auto& prefix = expression.operands.front().text;
  if (const auto* object = object_type(prefix); object != nullptr) {
    return object;
  }
  return visible_type_mark(prefix);
}

bool Lowerer::is_vhdl_array_like(const frontend::Type& type) {
  if (type.vhdl_array) {
    return true;
  }
  const auto separator = type.spelling.find_last_of('.');
  const auto name = type.spelling.substr(
      separator == std::string::npos ? 0 : separator + 1);
  return name == "bit_vector" || name == "std_logic_vector"
      || name == "std_ulogic_vector" || name == "signed"
      || name == "unsigned" || name == "ufixed" || name == "sfixed"
      || name == "unresolved_ufixed" || name == "unresolved_sfixed"
      || name == "float" || name == "unresolved_float"
      || name == "u_float";
}

std::optional<frontend::PackedRange> Lowerer::vhdl_array_attribute_range(
    const Expression& expression,
    const bool report_errors) {
  const auto* type = vhdl_array_attribute_prefix_type(expression);
  if (type == nullptr || !is_vhdl_array_like(*type)) {
    if (report_errors) {
      report(
          "FSIM-ELAB-VHARRAYATTR-001",
          expression.text
              + " requires a visible bounded array object, type, or subtype mark",
          expression.span);
    }
    return std::nullopt;
  }
  if (expression.operands.empty() || expression.operands.size() > 2) {
    if (report_errors) {
      report(
          "FSIM-ELAB-VHARRAYATTR-001",
          expression.text + " accepts at most one dimension argument",
          expression.span);
    }
    return std::nullopt;
  }

  std::int64_t dimension = 1;
  if (expression.operands.size() == 2) {
    const auto selected = static_integer_value(expression.operands[1]);
    if (!selected) {
      if (report_errors) {
        report(
            "FSIM-ELAB-VHARRAYATTR-002",
            expression.text + " requires a locally static dimension",
            expression.operands[1].span);
      }
      return std::nullopt;
    }
    dimension = *selected;
  }
  const auto rank = type->vhdl_array
      ? type->vhdl_array->dimensions.size()
      : std::size_t{1};
  if (dimension < 1
      || static_cast<std::uint64_t>(dimension) > rank) {
    if (report_errors) {
      report(
          "FSIM-ELAB-VHARRAYATTR-002",
          expression.text + " dimension " + std::to_string(dimension)
              + " is outside array rank " + std::to_string(rank),
          expression.operands.size() == 2
              ? expression.operands[1].span
              : expression.operands.front().span);
    }
    return std::nullopt;
  }
  if (type->vhdl_array) {
    const auto& selected = type->vhdl_array->dimensions[
        static_cast<std::size_t>(dimension - 1)];
    if (selected.range) {
      return frontend::PackedRange{
          selected.range->left,
          selected.range->right,
          selected.range->descending};
    }
  } else if (type->packed_range) {
    return type->packed_range;
  }
  if (report_errors) {
    report(
        "FSIM-ELAB-VHARRAYATTR-001",
        expression.text + " requires a concrete array constraint",
        expression.operands.front().span);
  }
  return std::nullopt;
}

RegisterId Lowerer::narrow_enumeration_ordinal(
    const RegisterId source,
    const frontend::Type& type) {
  const auto width =
      static_cast<std::size_t>(type.width().value_or(1));
  const auto destination = allocate_register(
      width,
      type.domain == frontend::ValueDomain::Boolean
          ? frontend::ValueDomain::Boolean
          : frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(Extract{
      destination,
      source,
      0,
      static_cast<std::uint32_t>(width)});
  return destination;
}

std::optional<RegisterId> Lowerer::lower_vhdl_scalar_attribute(
    const Expression& expression,
    const frontend::Type* expected_type) {
  const auto& prefix = expression.operands.front().text;
  const auto* type_mark = visible_type_mark(prefix);
  const auto* object = object_type(prefix);
  const auto scalar_type = [&](const frontend::Type* candidate) {
    return candidate != nullptr && !is_vhdl_array_like(*candidate)
        && candidate->packed_members.empty()
        && (candidate->domain == frontend::ValueDomain::Integer
            || candidate->domain == frontend::ValueDomain::Boolean
            || candidate->domain == frontend::ValueDomain::Bit2
            || !candidate->enumeration_literals.empty());
  };
  const bool scalar_value_attribute = expression.text == "'pos"
      || expression.text == "'val" || expression.text == "'succ"
      || expression.text == "'pred" || expression.text == "'leftof"
      || expression.text == "'rightof";
  const bool object_shorthand =
      vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019
      && expression.text != "'val" && scalar_value_attribute
      && expression.operands.size() == 1U && scalar_type(object);
  const auto* attribute_type = object_shorthand ? object : type_mark;
  if (!scalar_type(attribute_type)) {
    if (type_mark != nullptr && is_vhdl_array_like(*type_mark)
        && scalar_value_attribute) {
      report(
          "FSIM-ELAB-VHARRAYATTR-001",
          expression.text + " is not defined for array type '"
              + type_mark->spelling + "'",
          expression.span);
    } else if (scalar_type(object)) {
      const bool enumeration = !object->enumeration_literals.empty();
      report(
          enumeration ? "FSIM-ELAB-VHENUMATTR-001"
                      : "FSIM-ELAB-VHSCALARATTR-001",
          std::string{enumeration ? "enumeration" : "scalar"}
              + " attribute '" + expression.text
              + " requires a visible type or subtype mark",
          expression.operands.front().span);
    } else {
      report(
          "FSIM-ELAB-VHENUMATTR-001",
          "enumeration attribute '" + expression.text
              + "' requires a visible enumeration type mark",
          expression.operands.front().span);
    }
    return std::nullopt;
  }
  const auto& type = *attribute_type;
  if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
      && (expression.text == "'length" || expression.text == "'range"
          || expression.text == "'reverse_range")) {
    report(
        "FSIM-ELAB-VHATTR-011",
        expression.text
            + " on a scalar prefix requires the VHDL-2019 profile",
        expression.span);
    return std::nullopt;
  }
  const bool enumeration = !type.enumeration_literals.empty();
  const bool integer = type.domain == frontend::ValueDomain::Integer;
  const bool boolean = type.domain == frontend::ValueDomain::Boolean;
  const bool bit = type.domain == frontend::ValueDomain::Bit2
      && !is_vhdl_array_like(type) && type.packed_members.empty();
  if (!enumeration && !integer && !boolean && !bit) {
    report(
        "FSIM-ELAB-VHSCALARATTR-001",
        "scalar attribute prefix '" + type.spelling
            + "' has no supported discrete range",
        expression.span);
    return std::nullopt;
  }
  const bool enumeration_result = expression.text == "'left"
      || expression.text == "'right" || expression.text == "'low"
      || expression.text == "'high" || expression.text == "'val"
      || expression.text == "'succ" || expression.text == "'pred"
      || expression.text == "'leftof" || expression.text == "'rightof";
  if (enumeration && enumeration_result && expected_type != nullptr) {
    if (expected_type->vhdl_array) {
      report(
          "FSIM-ELAB-VHARRAY-006",
          "VHDL array values require the same nominal type; expected '"
              + expected_type->spelling + "' but found '" + type.spelling
              + "'",
          expression.span);
      return std::nullopt;
    }
    if (!expected_type->enumeration_literals.empty()
        && expected_type->nominal_type != type.nominal_type) {
      report(
          "FSIM-ELAB-VHENUM-002",
          "VHDL enumeration values require the same nominal type; expected '"
              + expected_type->spelling + "' but found '" + type.spelling
              + "'",
          expression.span);
      return std::nullopt;
    }
  }

  const auto width_value = type.width();
  if (!width_value || *width_value == 0
      || *width_value > (integer ? 64U : 32U)
      || (enumeration
          && type.enumeration_literals.size()
              > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max()))) {
    report(
        enumeration ? "FSIM-ELAB-VHENUMATTR-001"
                    : "FSIM-ELAB-VHSCALARATTR-001",
        "scalar attribute prefix '" + type.spelling
            + "' has no executable ordinal range",
        expression.span);
    return std::nullopt;
  }
  const auto width = static_cast<std::size_t>(*width_value);
  const auto range = [&]() {
    if (enumeration) {
      return type.enumeration_range.value_or(frontend::EnumerationRange{
          0,
          static_cast<std::int64_t>(type.enumeration_literals.size() - 1U),
          false});
    }
    if (integer) {
      const auto value = type.integer_range.value_or(
          frontend::vhdl_predefined_integer_range(
              vhdl_standard_, "integer"));
      return frontend::EnumerationRange{
          value.left, value.right, value.descending};
    }
    return frontend::EnumerationRange{0, 1, false};
  }();
  const auto left = range.left;
  const auto right = range.right;
  const auto low = std::min(left, right);
  const auto high = std::max(left, right);
  const std::string diagnostic = enumeration
      ? "FSIM-ELAB-VHENUMATTR-001"
      : "FSIM-ELAB-VHSCALARATTR-001";
  const std::string range_diagnostic = enumeration
      ? "FSIM-ELAB-VHENUMATTR-002"
      : "FSIM-ELAB-VHSCALARATTR-002";
  const auto require_arity = [&](const std::size_t expected) {
    if (expression.operands.size() == expected + 1U) {
      return true;
    }
    report(
        diagnostic,
        expression.text + " on "
            + (enumeration ? std::string{"enumeration"}
                           : std::string{"scalar"})
            + " type '" + type.spelling
            + "' requires " + std::to_string(expected)
            + (expected == 1 ? " argument" : " arguments"),
        expression.span);
    return false;
  };
  const auto load_scalar = [&](const std::int64_t value) {
    const auto destination = allocate_register(width, type.domain);
    process_.operations.emplace_back(LoadConstant{
        destination,
        integer ? integer_value(value, width)
                : unsigned_value(static_cast<std::uint64_t>(value), width)});
    return destination;
  };

  if (expression.text == "'range"
      || expression.text == "'reverse_range") {
    report(
        "FSIM-ELAB-VHSCALARATTR-003",
        expression.text
            + " is a discrete range and cannot be used as a scalar expression",
        expression.span);
    return std::nullopt;
  }

  if (expression.text == "'left" || expression.text == "'right"
      || expression.text == "'low" || expression.text == "'high") {
    if (!require_arity(0)) {
      return std::nullopt;
    }
    return load_scalar(
        expression.text == "'left" ? left
        : expression.text == "'right" ? right
        : expression.text == "'low" ? low
                                      : high);
  }
  if (expression.text == "'length") {
    if (!require_arity(0)) {
      return std::nullopt;
    }
    const auto integer_width = static_cast<std::size_t>(
        frontend::vhdl_predefined_integer_storage_width(vhdl_standard_));
    const auto integer_range = frontend::vhdl_predefined_integer_range(
        vhdl_standard_, "integer");
    const auto distance = index_distance(low, high);
    if (distance == std::numeric_limits<std::uint64_t>::max()
        || distance + 1U
            > static_cast<std::uint64_t>(integer_range.right)) {
      report(
          range_diagnostic,
          "'length result is outside the predefined integer range",
          expression.span);
      return std::nullopt;
    }
    const auto destination = allocate_register(
        integer_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(
        LoadConstant{destination, integer_value(
            static_cast<std::int64_t>(distance + 1U), integer_width)});
    return destination;
  }
  if (expression.text == "'ascending") {
    if (!require_arity(0)) {
      return std::nullopt;
    }
    const auto destination =
        allocate_register(1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(LoadConstant{
        destination,
        PackedLogic4(1, range.descending ? Logic4::zero : Logic4::one)});
    return destination;
  }

  const bool position = expression.text == "'pos";
  const bool value = expression.text == "'val";
  const bool successor = expression.text == "'succ"
      || (expression.text == "'leftof" && range.descending)
      || (expression.text == "'rightof" && !range.descending);
  const bool predecessor = expression.text == "'pred"
      || (expression.text == "'leftof" && !range.descending)
      || (expression.text == "'rightof" && range.descending);
  if (!position && !value && !successor && !predecessor) {
    report(
        diagnostic,
        "attribute '" + expression.text + "' is not defined for scalar type '"
            + type.spelling + "'",
        expression.span);
    return std::nullopt;
  }
  if (!require_arity(object_shorthand ? 0U : 1U)) {
    return std::nullopt;
  }

  if (value) {
    const auto static_value = constant_index(expression.operands[1]);
    if (static_value && (*static_value < low || *static_value > high)) {
      report(
          range_diagnostic,
          "'val argument " + std::to_string(*static_value)
              + " is outside scalar type '" + type.spelling + "'",
          expression.operands[1].span);
      return std::nullopt;
    }
    if (!is_integer_expression(expression.operands[1])) {
      report(
          diagnostic,
          "'val requires an integer-family argument",
          expression.operands[1].span);
      return std::nullopt;
    }
    const auto integer_width = static_cast<std::size_t>(
        frontend::vhdl_predefined_integer_storage_width(vhdl_standard_));
    const auto argument = lower_expression(
        expression.operands[1], integer_width);
    if (!argument || register_width(*argument) != integer_width) {
      report(
          diagnostic,
          "'val argument does not have the profile-selected integer representation",
          expression.operands[1].span);
      return std::nullopt;
    }
    process_.operations.emplace_back(IntegerCheck{*argument, low, high});
    if (integer) {
      return argument;
    }
    return narrow_enumeration_ordinal(*argument, type);
  }

  const auto argument = [&]() -> std::optional<RegisterId> {
    const auto& candidate = object_shorthand
        ? expression.operands.front()
        : expression.operands[1];
    if (enumeration) {
      if (candidate.kind == ExpressionKind::IntegerLiteral
          || candidate.kind == ExpressionKind::BooleanLiteral
          || candidate.kind == ExpressionKind::StringLiteral) {
        report(
            diagnostic,
            expression.text + " requires a value of enumeration type '"
                + type.spelling + "'",
            candidate.span);
        return std::nullopt;
      }
      if (candidate.kind == ExpressionKind::Identifier
          && !vhdl_enumeration_ordinal(candidate, type)) {
        const auto* argument_type = object_type(candidate.text);
        if (argument_type != nullptr
            && argument_type->enumeration_literals.empty()) {
          report(
              diagnostic,
              expression.text + " requires a value of enumeration type '"
                  + type.spelling + "'",
              candidate.span);
          return std::nullopt;
        }
      }
    } else if (integer && !is_integer_expression(candidate)) {
      report(
          diagnostic,
          expression.text + " requires an integer-family value",
          candidate.span);
      return std::nullopt;
    } else if (!integer) {
      const auto* candidate_type = candidate.kind == ExpressionKind::Identifier
          ? object_type(candidate.text)
          : nullptr;
      const bool literal_matches = boolean
          ? candidate.kind == ExpressionKind::BooleanLiteral
          : candidate.kind == ExpressionKind::LogicLiteral;
      if (!literal_matches
          && (candidate_type == nullptr
              || candidate_type->domain != type.domain
              || is_vhdl_array_like(*candidate_type))) {
        report(
            diagnostic,
            expression.text + " requires a value of scalar type '"
                + type.spelling + "'",
            candidate.span);
        return std::nullopt;
      }
    }
    return lower_expression(candidate, width, &type);
  }();
  if (!argument) {
    return std::nullopt;
  }
  const auto ordinal = integer ? *argument : widen_enumeration_ordinal(*argument);
  if (position) {
    process_.operations.emplace_back(IntegerCheck{ordinal, low, high});
    return ordinal;
  }

  const auto static_ordinal = constant_index(expression.operands[1]);
  const auto lower = successor ? low : low + 1;
  const auto upper = successor ? high - 1 : high;
  if (lower > upper
      || (static_ordinal
          && (*static_ordinal < lower || *static_ordinal > upper))) {
    report(
        range_diagnostic,
        expression.text + " argument has no result inside scalar type '"
            + type.spelling + "'",
        expression.operands[1].span);
    return std::nullopt;
  }
  process_.operations.emplace_back(IntegerCheck{ordinal, lower, upper});
  const auto ordinal_width = register_width(ordinal);
  const auto one = allocate_register(
      ordinal_width, frontend::ValueDomain::Integer);
  process_.operations.emplace_back(
      LoadConstant{one, integer_value(1, ordinal_width)});
  const auto adjusted = allocate_register(
      ordinal_width, frontend::ValueDomain::Integer);
  process_.operations.emplace_back(IntegerBinary{
      successor ? IntegerBinaryOperator::add
                : IntegerBinaryOperator::subtract,
      adjusted,
      ordinal,
      one});
  if (integer) {
    return adjusted;
  }
  return narrow_enumeration_ordinal(adjusted, type);
}

}  // namespace fsim::elaboration
