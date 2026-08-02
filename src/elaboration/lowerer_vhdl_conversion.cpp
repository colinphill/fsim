// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "vhdl_array_boundary.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

[[nodiscard]] bool same_nominal_base(
    const frontend::Type& left,
    const frontend::Type& right) {
  if (left.nominal_type.empty() && right.nominal_type.empty()) {
    return true;
  }
  return !left.nominal_type.empty()
      && left.nominal_type == right.nominal_type;
}

[[nodiscard]] bool exact_bounded_type_match(
    const frontend::Type& target,
    const frontend::Type& source) {
  if (target.domain != source.domain
      || target.is_signed != source.is_signed
      || target.width() != source.width()) {
    return false;
  }
  const bool target_array = target.vhdl_array.has_value();
  const bool source_array = source.vhdl_array.has_value();
  if (target_array || source_array) {
    return target_array && source_array
        && same_nominal_base(target, source)
        && vhdl_array_shape_matches(
            *target.vhdl_array, *source.vhdl_array, false);
  }
  const bool target_record = !target.packed_members.empty();
  const bool source_record = !source.packed_members.empty();
  if (target_record || source_record) {
    return target_record && source_record
        && same_nominal_base(target, source);
  }
  const bool target_enumeration =
      !target.enumeration_literals.empty();
  const bool source_enumeration =
      !source.enumeration_literals.empty();
  if (target_enumeration || source_enumeration) {
    return target_enumeration && source_enumeration
        && same_nominal_base(target, source);
  }
  if (target.domain == frontend::ValueDomain::Integer) {
    return true;
  }
  return same_nominal_base(target, source);
}

[[nodiscard]] bool supported_conversion_match(
    const frontend::Type& target,
    const frontend::Type& source) {
  const auto numeric_vector = [](const frontend::Type& type) {
    const auto separator = type.spelling.find_last_of('.');
    const auto name = std::string_view{type.spelling}.substr(
        separator == std::string::npos ? 0 : separator + 1);
    return name == "signed" || name == "unsigned";
  };
  if (numeric_vector(target) && numeric_vector(source)
      && target.domain == source.domain
      && target.width() == source.width()) {
    return true;
  }
  const bool composite = target.vhdl_array || source.vhdl_array
      || !target.packed_members.empty() || !source.packed_members.empty();
  if (!composite
      && (target.domain == frontend::ValueDomain::Integer
          || source.domain == frontend::ValueDomain::Integer)) {
    return target.domain == frontend::ValueDomain::Integer
        && source.domain == frontend::ValueDomain::Integer
        && target.width() == source.width();
  }
  return exact_bounded_type_match(target, source);
}

}  // namespace

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_conversion_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || expression.kind != ExpressionKind::Call
      || expression.operands.size() != 1) {
    return ExpressionAttempt{};
  }

  constexpr std::string_view qualification_prefix{
      "@vhdl-qualified:"};
  const bool qualified =
      expression.text.starts_with(qualification_prefix);
  const auto conversion_name = qualified
      ? std::string_view{expression.text}.substr(
            qualification_prefix.size())
      : std::string_view{expression.text};
  const auto builtin_type = [](const std::string_view name) {
    frontend::Type result;
    result.spelling = name;
    if (name == "integer" || name == "natural"
        || name == "positive") {
      result.domain = frontend::ValueDomain::Integer;
      result.is_signed = true;
      if (name == "natural") {
        result.integer_range = frontend::IntegerRange{
            0, std::numeric_limits<std::int32_t>::max(), false};
      } else if (name == "positive") {
        result.integer_range = frontend::IntegerRange{
            1, std::numeric_limits<std::int32_t>::max(), false};
      }
    } else if (name == "boolean") {
      result.domain = frontend::ValueDomain::Boolean;
    } else if (name == "bit") {
      result.domain = frontend::ValueDomain::Bit2;
    }
    return result;
  };

  const auto* conversion_type = visible_type_mark(conversion_name);
  frontend::Type builtin = builtin_type(conversion_name);
  if (conversion_type == nullptr
      && builtin.domain != frontend::ValueDomain::Unknown) {
    conversion_type = &builtin;
  }
  if (expected_type != nullptr) {
    const auto simple_type_name = [](const std::string_view name) {
      const auto separator = name.find_last_of('.');
      return name.substr(
          separator == std::string_view::npos ? 0 : separator + 1);
    };
    const auto expected_name = expected_type->named_type.empty()
        ? std::string_view{expected_type->spelling}
        : std::string_view{expected_type->named_type};
    const auto conversion_simple = simple_type_name(conversion_name);
    if (simple_type_name(conversion_name)
            == simple_type_name(expected_name)
        && (conversion_type == nullptr
            || conversion_simple == "signed"
            || conversion_simple == "unsigned")) {
      conversion_type = expected_type;
    }
  }
  if (conversion_type == nullptr) {
    if (qualified) {
      report(
          "FSIM-ELAB-VHQUAL-001",
          "VHDL qualified expression type mark '"
              + std::string{conversion_name}
              + "' is not visible",
          expression.span);
      return std::nullopt;
    }
    return ExpressionAttempt{};
  }

  const auto width = conversion_type->width();
  if (!width || *width == 0 || *width > 64) {
    report(
        qualified ? "FSIM-ELAB-VHQUAL-002"
                  : "FSIM-ELAB-VHCONV-002",
        std::string{qualified ? "VHDL qualification type '"
                              : "VHDL conversion type '"}
            + std::string{conversion_name}
            + "' has no executable width in 1..64",
        expression.span);
    return std::nullopt;
  }

  if (expected_type != nullptr
      && (expected_width != *width
          || !vhdl_callable_type_matches(
              *expected_type, *conversion_type))) {
    report(
        qualified ? "FSIM-ELAB-VHQUAL-004"
                  : "FSIM-ELAB-VHCONV-004",
        std::string{qualified ? "qualified expression type '"
                              : "conversion result type '"}
            + conversion_type->spelling
            + "' is incompatible with contextual type '"
            + expected_type->spelling + "'",
        expression.span);
    return std::nullopt;
  }

  const auto& operand = expression.operands.front();
  std::optional<frontend::Type> source_storage =
      vhdl_expression_type(operand);
  const frontend::Type* source_type = source_storage
      ? &*source_storage
      : enumeration_expression_type(operand);
  frontend::Type source_builtin;
  if (source_type == nullptr
      && is_integer_expression(operand)) {
    source_builtin = builtin_type("integer");
    source_type = &source_builtin;
  } else if (source_type == nullptr
             && operand.kind == ExpressionKind::BooleanLiteral) {
    source_builtin = builtin_type("boolean");
    source_type = &source_builtin;
  }
  frontend::Type nested_storage;
  if (source_type == nullptr
      && operand.kind == ExpressionKind::Call
      && operand.operands.size() == 1) {
    auto nested_name = std::string_view{operand.text};
    if (nested_name.starts_with(qualification_prefix)) {
      nested_name.remove_prefix(qualification_prefix.size());
    }
    if (const auto* nested = visible_type_mark(nested_name)) {
      source_type = nested;
    } else {
      nested_storage = builtin_type(nested_name);
      if (nested_storage.domain
          != frontend::ValueDomain::Unknown) {
        source_type = &nested_storage;
      }
    }
  }

  const auto compatible = source_type == nullptr
      ? vhdl_expression_matches_type(operand, *conversion_type)
      : qualified
          ? exact_bounded_type_match(
              *conversion_type, *source_type)
          : supported_conversion_match(
              *conversion_type, *source_type);
  if (!compatible) {
    report(
        qualified ? "FSIM-ELAB-VHQUAL-003"
                  : "FSIM-ELAB-VHCONV-003",
        std::string{qualified ? "qualified expression operand is not of "
                              : "conversion operand is not closely related "
                                  "to "}
            + "type '" + conversion_type->spelling + "'",
        operand.span);
    return std::nullopt;
  }

  const auto source_width = source_type != nullptr
      ? source_type->width()
      : infer_width(operand);
  const auto lowering_width = source_width
      ? static_cast<std::size_t>(*source_width)
      : static_cast<std::size_t>(*width);
  auto source = lower_expression(
      operand,
      qualified ? static_cast<std::size_t>(*width)
                : lowering_width,
      qualified || source_type == nullptr
          ? conversion_type : source_type);
  if (!source) {
    return std::nullopt;
  }
  if (register_width(*source) != *width
      || register_domain(*source) != conversion_type->domain) {
    report(
        qualified ? "FSIM-ELAB-VHQUAL-003"
                  : "FSIM-ELAB-VHCONV-003",
        std::string{qualified ? "qualified expression does not exactly "
                                  "match "
                              : "conversion does not support changing the "
                                  "width or state domain of "}
            + "type '" + conversion_type->spelling + "'",
        operand.span);
    return std::nullopt;
  }
  if (!conversion_type->enumeration_literals.empty()) {
    emit_enumeration_check(*source, *conversion_type);
  } else if (conversion_type->domain
             == frontend::ValueDomain::Integer) {
    emit_integer_check(*source, conversion_type->integer_range);
  }
  return source;
}

}  // namespace fsim::elaboration
