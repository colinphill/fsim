// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <ranges>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {
    [[maybe_unused]] constexpr std::string_view retired_string_copyout_diagnostic {
        "FSIM-ELAB-SVCLASS-018"
    };
    [[maybe_unused]] constexpr std::string_view
        retired_static_string_copyout_diagnostic { "FSIM-ELAB-SVCLASS-019" };
} // namespace

bool Lowerer::lower_class_assignment(const Statement& statement) {
  constexpr std::string_view property_prefix{"@sv-property:"};
  constexpr std::string_view static_property_prefix{"@sv-static-property:"};
  constexpr std::string_view container_index_prefix{
      "@sv-container-index:"};
  constexpr std::string_view container_property_prefix{
      "@sv-container-property:"};
  if (statement.target.kind == ExpressionKind::Call
      && statement.target.text.starts_with(container_index_prefix)) {
    if (statement.assignment_kind != AssignmentKind::Blocking
        || statement.target.operands.size() != 2U) {
      report(
          "FSIM-ELAB-SVCLASS-012",
          "class handle container elements require a blocking indexed "
          "assignment",
          statement.span);
      return true;
    }
    const auto receiver = lower_expression(
        statement.target.operands[0], 64);
    const auto index_width = infer_width(
        statement.target.operands[1]).value_or(std::size_t{32});
    const auto index = lower_expression(
        statement.target.operands[1], index_width);
    const auto source = lower_expression(statement.value, 64);
    if (!receiver || !index || !source) return true;
    const auto destination = allocate_register(
        64, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ClassMethodCall{
        destination,
        *receiver,
        "@container-write:"
            + statement.target.text.substr(container_index_prefix.size()),
        {*index, *source},
        {"", ""},
        {static_cast<std::uint8_t>(frontend::PortDirection::Input),
         static_cast<std::uint8_t>(frontend::PortDirection::Input)},
        64,
        false});
    return true;
  }
  if (statement.target.kind == ExpressionKind::Call
      && statement.target.text.starts_with(container_property_prefix)) {
    const auto plain_new =
        statement.value.kind == ExpressionKind::Index
        && statement.value.operands.size() == 2U
        && statement.value.operands[0].kind == ExpressionKind::Identifier
        && statement.value.operands[0].text == "new";
    if (statement.assignment_kind != AssignmentKind::Blocking
        || statement.target.operands.size() != 1U || !plain_new) {
      report(
          "FSIM-ELAB-SVCLASS-013",
          "dynamic class handle container assignment requires new[size]",
          statement.span);
      return true;
    }
    const auto receiver = lower_expression(
        statement.target.operands.front(), 64);
    const auto size_width = infer_width(
        statement.value.operands[1]).value_or(std::size_t{32});
    const auto size = lower_expression(
        statement.value.operands[1], size_width);
    if (!receiver || !size) return true;
    const auto destination = allocate_register(
        64, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ClassMethodCall{
        destination,
        *receiver,
        "@container-resize:"
            + statement.target.text.substr(container_property_prefix.size()),
        {*size},
        {""},
        {static_cast<std::uint8_t>(frontend::PortDirection::Input)},
        64,
        false});
    return true;
  }
  if (statement.target.kind == ExpressionKind::Call
      && statement.target.text.starts_with(static_property_prefix)) {
    if (statement.assignment_kind != AssignmentKind::Blocking
        || statement.procedural_assignment_control
            != frontend::ProceduralAssignmentControl::None
        || statement.target.call_result_width == 0
        || statement.target.call_result_width
            > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-SVCLASS-008",
          "class static properties require a time-free blocking assignment "
          "and an executable packed type",
          statement.span);
      return true;
    }
    const auto width = static_cast<std::size_t>(
        statement.target.call_result_width);
    const auto source = lower_expression(statement.value, width);
    if (source) {
      process_.operations.emplace_back(ClassStaticPropertyWrite{
          *source,
          statement.target.text.substr(static_property_prefix.size())});
    }
    return true;
  }
  if (statement.target.kind != ExpressionKind::Call
      || !statement.target.text.starts_with(property_prefix)) {
    return false;
  }
  if (statement.assignment_kind != AssignmentKind::Blocking
      || statement.procedural_assignment_control
          != frontend::ProceduralAssignmentControl::None
      || statement.target.operands.size() != 1U
      || statement.target.call_result_width == 0
      || statement.target.call_result_width
          > std::numeric_limits<std::size_t>::max()) {
    report(
        "FSIM-ELAB-SVCLASS-004",
        "instance class properties require a time-free blocking assignment "
        "and an executable packed type",
        statement.span);
    return true;
  }
  const auto receiver = lower_expression(
      statement.target.operands.front(), 64);
  const auto width = static_cast<std::size_t>(
      statement.target.call_result_width);
  const auto source = lower_expression(statement.value, width);
  if (!receiver || !source) return true;
  process_.operations.emplace_back(ClassPropertyWrite{
      *receiver,
      *source,
      statement.target.text.substr(property_prefix.size())});
  return true;
}

Lowerer::ExpressionAttempt Lowerer::lower_class_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  if (expression.kind != ExpressionKind::Call
      || !expression.text.starts_with("@sv-")) {
    return {};
  }
  constexpr std::string_view container_method_prefix{
      "@sv-container-method:"};
  if (expression.text.starts_with(container_method_prefix)) {
    constexpr std::string_view pop_front{"pop_front:"};
    constexpr std::string_view size{"size:"};
    const auto operation = std::string_view{expression.text}.substr(
        container_method_prefix.size());
    const auto pop = operation.starts_with(pop_front);
    const auto query_size = operation.starts_with(size);
    if ((!pop && !query_size) || expression.operands.size() != 1U) {
      report(
          "FSIM-ELAB-SVCLASS-016",
          "class handle container expression requires pop_front() or size()",
          expression.span);
      return std::nullopt;
    }
    const auto receiver = lower_expression(expression.operands.front(), 64);
    if (!receiver) return std::nullopt;
    const auto result_width = pop ? std::size_t{64} : std::size_t{32};
    const auto destination = allocate_register(
        result_width,
        pop ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Integer);
    const auto property = operation.substr(
        pop ? pop_front.size() : size.size());
    process_.operations.emplace_back(ClassMethodCall{
        destination,
        *receiver,
        (pop ? "@container-pop-front:" : "@container-size:")
            + std::string{property},
        {},
        {},
        {},
        static_cast<std::uint32_t>(result_width),
        false});
    if (expected_width != 0 && expected_width != result_width) {
      return resize_register(destination, expected_width, false);
    }
    return destination;
  }
  constexpr std::string_view container_index_prefix{
      "@sv-container-index:"};
  if (expression.text.starts_with(container_index_prefix)) {
    if (expression.operands.size() != 2U) {
      report(
          "FSIM-ELAB-SVCLASS-014",
          "class handle container read requires a receiver and index",
          expression.span);
      return std::nullopt;
    }
    const auto receiver = lower_expression(expression.operands[0], 64);
    const auto index_width = infer_width(
        expression.operands[1]).value_or(std::size_t{32});
    const auto index = lower_expression(
        expression.operands[1], index_width);
    if (!receiver || !index) return std::nullopt;
    const auto destination = allocate_register(
        64, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ClassMethodCall{
        destination,
        *receiver,
        "@container-read:"
            + expression.text.substr(container_index_prefix.size()),
        {*index},
        {""},
        {static_cast<std::uint8_t>(frontend::PortDirection::Input)},
        64,
        false});
    if (expected_width != 0 && expected_width != 64) {
      return resize_register(destination, expected_width, false);
    }
    return destination;
  }
  constexpr std::string_view static_property_prefix{"@sv-static-property:"};
  if (expression.text.starts_with(static_property_prefix)) {
    if (expression.call_result_width == 0
        || expression.call_result_width
            > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-SVCLASS-009",
          "class static property read requires an executable packed type",
          expression.span);
      return std::nullopt;
    }
    const auto result_width = static_cast<std::size_t>(
        expression.call_result_width);
    const auto destination = allocate_register(
        result_width, expression.call_result_domain);
    process_.operations.emplace_back(ClassStaticPropertyRead{
        destination,
        expression.text.substr(static_property_prefix.size()),
        static_cast<std::uint32_t>(result_width)});
    if (expected_width != 0 && expected_width != result_width) {
      return resize_register(
          destination, expected_width, expression.call_result_signed);
    }
    return destination;
  }
  constexpr std::string_view property_prefix{"@sv-property:"};
  if (expression.text.starts_with(property_prefix)) {
    if (expression.operands.size() != 1U
        || expression.call_result_width == 0
        || expression.call_result_width
            > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-SVCLASS-005",
          "instance class property read requires one receiver and an "
          "executable packed type",
          expression.span);
      return std::nullopt;
    }
    const auto receiver = lower_expression(expression.operands.front(), 64);
    if (!receiver) return std::nullopt;
    const auto result_width = static_cast<std::size_t>(
        expression.call_result_width);
    const auto destination = allocate_register(
        result_width, expression.call_result_domain);
    process_.operations.emplace_back(ClassPropertyRead{
        destination,
        *receiver,
        expression.text.substr(property_prefix.size()),
        static_cast<std::uint32_t>(result_width)});
    if (expected_width != 0 && expected_width != result_width) {
      return resize_register(
          destination, expected_width, expression.call_result_signed);
    }
    return destination;
  }
  constexpr std::string_view method_prefix{"@sv-method:"};
  constexpr std::string_view base_method_prefix{"@sv-base-method:"};
  if (expression.text.starts_with(method_prefix)
      || expression.text.starts_with(base_method_prefix)) {
    const auto selected_prefix = expression.text.starts_with(base_method_prefix)
        ? base_method_prefix : method_prefix;
    if (expression.operands.empty()
        || expression.call_argument_directions.size()
            != expression.operands.size()
        || (!expression.call_argument_names.empty()
            && expression.call_argument_names.size()
                != expression.operands.size())
        || expression.call_result_width == 0
        || expression.call_result_width
            > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-SVCLASS-006",
          "instance class method call has inconsistent executable profile "
          "metadata",
          expression.span);
      return std::nullopt;
    }
    const auto receiver = lower_expression(expression.operands.front(), 64);
    if (!receiver) return std::nullopt;
    std::vector<RegisterId> actuals;
    std::vector<std::uint8_t> actual_kinds;
    std::vector<std::size_t> actual_widths;
    std::vector<frontend::ValueDomain> actual_domains;
    for (const auto& operand : expression.operands | std::views::drop(1)) {
      if (is_string_expression(operand)) {
        const auto actual = lower_string_expression(operand);
        if (!actual) return std::nullopt;
        actuals.push_back(*actual);
        actual_kinds.push_back(1U);
        actual_widths.push_back(0U);
        actual_domains.push_back(frontend::ValueDomain::String);
        continue;
      }
      const auto width = infer_width(operand);
      if (!width || *width == 0) {
        report(
            "FSIM-ELAB-SVCLASS-007",
            "class method actual requires a positive packed width",
            operand.span);
        return std::nullopt;
      }
      const auto actual = lower_expression(operand, *width);
      if (!actual) return std::nullopt;
      actuals.push_back(*actual);
      actual_kinds.push_back(0U);
      actual_widths.push_back(*width);
      actual_domains.push_back(register_domain(*actual));
    }
    const auto result_width = static_cast<std::size_t>(
        expression.call_result_width);
    const auto destination = allocate_register(
        result_width, expression.call_result_domain);
    std::vector<std::string> names;
    if (!expression.call_argument_names.empty()) {
      names.assign(
          expression.call_argument_names.begin() + 1,
          expression.call_argument_names.end());
    }
    if (names.empty()) names.resize(actuals.size());
    std::vector<std::uint8_t> directions;
    directions.reserve(actuals.size());
    for (const auto direction :
         expression.call_argument_directions | std::views::drop(1)) {
      directions.push_back(static_cast<std::uint8_t>(direction));
    }
    auto inline_constraints = lower_inline_constraints(expression);
    if (!inline_constraints)
        return std::nullopt;
    ClassMethodCall operation {
        destination,
        *receiver,
        expression.text.substr(selected_prefix.size()),
        actuals,
        std::move(names),
        std::move(directions),
        static_cast<std::uint32_t>(result_width),
        selected_prefix == method_prefix,
        actual_kinds
    };
    operation.inline_constraints = std::move(*inline_constraints);
    process_.operations.emplace_back(std::move(operation));
    for (std::size_t index = 0; index < actuals.size(); ++index) {
      if (expression.call_argument_directions[index + 1U]
          == frontend::PortDirection::Input) {
        continue;
      }
      if (actual_kinds[index] == 1U) {
          frontend::Type actual_type;
          actual_type.domain = frontend::ValueDomain::String;
          lower_callable_copy_out(
              expression.operands[index + 1U],
              actual_type,
              { },
              actuals[index],
              { },
              true,
              false,
              "@class_function_copyout_" + std::to_string(index));
          continue;
      }
      frontend::Type actual_type;
      actual_type.domain = actual_domains[index];
      actual_type.packed_range = frontend::PackedRange{
          static_cast<std::int64_t>(actual_widths[index] - 1U), 0, true};
      lower_callable_copy_out(
          expression.operands[index + 1U],
          actual_type,
          actuals[index],
          {},
          {},
          false,
          false,
          "@class_function_copyout_" + std::to_string(index));
    }
    if (expected_width != 0 && expected_width != result_width) {
      return resize_register(
          destination, expected_width, expression.call_result_signed);
    }
    return destination;
  }
  constexpr std::string_view static_method_prefix{"@sv-static-method:"};
  if (expression.text.starts_with(static_method_prefix)) {
    if (expression.call_argument_directions.size()
            != expression.operands.size()
        || (!expression.call_argument_names.empty()
            && expression.call_argument_names.size()
                != expression.operands.size())
        || expression.call_result_width == 0
        || expression.call_result_width
            > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-SVCLASS-010",
          "class static method call has inconsistent executable profile "
          "metadata",
          expression.span);
      return std::nullopt;
    }
    std::vector<RegisterId> actuals;
    std::vector<std::uint8_t> actual_kinds;
    std::vector<std::size_t> actual_widths;
    std::vector<frontend::ValueDomain> actual_domains;
    for (const auto& operand : expression.operands) {
      if (is_string_expression(operand)) {
        const auto actual = lower_string_expression(operand);
        if (!actual) return std::nullopt;
        actuals.push_back(*actual);
        actual_kinds.push_back(1U);
        actual_widths.push_back(0U);
        actual_domains.push_back(frontend::ValueDomain::String);
        continue;
      }
      const auto width = infer_width(operand);
      if (!width || *width == 0) {
        report(
            "FSIM-ELAB-SVCLASS-011",
            "class static method actual requires a positive packed width",
            operand.span);
        return std::nullopt;
      }
      const auto actual = lower_expression(operand, *width);
      if (!actual) return std::nullopt;
      actuals.push_back(*actual);
      actual_kinds.push_back(0U);
      actual_widths.push_back(*width);
      actual_domains.push_back(register_domain(*actual));
    }
    const auto result_width = static_cast<std::size_t>(
        expression.call_result_width);
    const auto destination = allocate_register(
        result_width, expression.call_result_domain);
    auto names = expression.call_argument_names;
    if (names.empty()) names.resize(actuals.size());
    std::vector<std::uint8_t> directions;
    directions.reserve(actuals.size());
    for (const auto direction : expression.call_argument_directions) {
      directions.push_back(static_cast<std::uint8_t>(direction));
    }
    process_.operations.emplace_back(ClassStaticMethodCall{
        destination,
        expression.text.substr(static_method_prefix.size()),
        actuals,
        std::move(names),
        std::move(directions),
        static_cast<std::uint32_t>(result_width),
        actual_kinds});
    for (std::size_t index = 0; index < actuals.size(); ++index) {
      if (expression.call_argument_directions[index]
          == frontend::PortDirection::Input) {
        continue;
      }
      if (actual_kinds[index] == 1U) {
          frontend::Type actual_type;
          actual_type.domain = frontend::ValueDomain::String;
          lower_callable_copy_out(
              expression.operands[index], actual_type, { }, actuals[index], { },
              true, false,
              "@class_static_function_copyout_" + std::to_string(index));
          continue;
      }
      frontend::Type actual_type;
      actual_type.domain = actual_domains[index];
      actual_type.packed_range = frontend::PackedRange{
          static_cast<std::int64_t>(actual_widths[index] - 1U), 0, true};
      lower_callable_copy_out(
          expression.operands[index], actual_type, actuals[index], {}, {},
          false, false,
          "@class_static_function_copyout_" + std::to_string(index));
    }
    if (expected_width != 0 && expected_width != result_width) {
      return resize_register(
          destination, expected_width, expression.call_result_signed);
    }
    return destination;
  }
  if (expression.text == "@sv-null") {
    if (expected_width == 64 && expected_type != nullptr
        && expected_type->systemverilog_scalar
            == frontend::SystemVerilogScalarKind::Chandle) {
      const auto destination = allocate_register(
          64, frontend::ValueDomain::Bit2);
      process_.operations.emplace_back(LoadConstant{
          destination, PackedLogic4::from_aval_bval(64, 0, 0)});
      return destination;
    }
    if (expected_width != 64
        || ((expected_type == nullptr
             || expected_type->systemverilog_class_declaration.empty())
            && expression.nominal_type.empty())) {
      report(
          "FSIM-ELAB-SVCLASS-001",
          "null requires a contextual class-handle type",
          expression.span);
      return std::nullopt;
    }
    const auto destination = allocate_register(
        64, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        destination, PackedLogic4::from_aval_bval(64, 0, 0)});
    return destination;
  }
  constexpr std::string_view checked_cast_prefix{"@sv-dollar-cast:"};
  if (expression.text.starts_with(checked_cast_prefix)) {
    const auto declared_type = expression.text.substr(
        checked_cast_prefix.size());
    if (expression.operands.size() != 2U || declared_type.empty()) {
      report(
          "FSIM-ELAB-SVCLASS-017",
          "$cast requires a resolved destination class handle and source",
          expression.span);
      return std::nullopt;
    }
    frontend::Type handle_type;
    handle_type.systemverilog_class_name = declared_type;
    handle_type.systemverilog_class_declaration = declared_type;
    const auto prior = lower_expression(
        expression.operands[0], 64, &handle_type);
    const auto source = lower_expression(expression.operands[1], 64);
    if (!prior || !source) return std::nullopt;
    const auto success = allocate_register(
        1, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(ClassMethodCall{
        success,
        *source,
        "@checked-cast:" + std::string{declared_type},
        {},
        {},
        {},
        1,
        false});
    const auto selected = allocate_register(
        64, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ConditionalSelect{
        selected, success, *source, *prior});
    lower_callable_copy_out(
        expression.operands[0], handle_type, selected, {}, {}, false, false,
        "@class_checked_cast");
    return success;
  }
  constexpr std::string_view allocation_prefix{"@sv-new:"};
  if (!expression.text.starts_with(allocation_prefix)) {
    return {};
  }
  const auto class_identity =
      expression.text.substr(allocation_prefix.size());
  if (expected_width != 64 || class_identity.empty()
      || expected_type == nullptr
      || expected_type->systemverilog_class_declaration != class_identity) {
    report(
        "FSIM-ELAB-SVCLASS-002",
        "class allocation result is incompatible with its destination "
        "handle type",
        expression.span);
    return std::nullopt;
  }
  std::vector<RegisterId> actuals;
  std::vector<std::uint8_t> actual_kinds;
  actuals.reserve(expression.operands.size());
  actual_kinds.reserve(expression.operands.size());
  for (const auto& operand : expression.operands) {
    if (operand.text == "@sv-null") {
      const auto actual = allocate_register(
          64, frontend::ValueDomain::Bit2);
      process_.operations.emplace_back(LoadConstant{
          actual, PackedLogic4::from_aval_bval(64, 0, 0)});
      actuals.push_back(actual);
      actual_kinds.push_back(0U);
      continue;
    }
    if (is_string_expression(operand)) {
      const auto actual = lower_string_expression(operand);
      if (!actual) return std::nullopt;
      actuals.push_back(*actual);
      actual_kinds.push_back(1U);
      continue;
    }
    const auto width = infer_width(operand);
    if (!width || *width == 0) {
      report(
          "FSIM-ELAB-SVCLASS-003",
          "constructor actual has no positive executable packed width",
          operand.span);
      return std::nullopt;
    }
    const auto actual = lower_expression(operand, *width);
    if (!actual) return std::nullopt;
    actuals.push_back(*actual);
    actual_kinds.push_back(0U);
  }
  const auto destination = allocate_register(
      64, frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(ClassAllocate{
      destination,
      class_identity,
      expected_type->systemverilog_class_declaration,
      std::move(actuals),
      std::move(actual_kinds),
      expression.call_argument_names});
  return destination;
}

}  // namespace fsim::elaboration
