// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {

using namespace runtime::simir;

std::optional<DynamicIndex> Lowerer::lower_dynamic_index(
    const Expression& source,
    const Expression& index,
    const std::size_t source_width,
    const std::uint32_t base_offset,
    const frontend::SourceSpan& span) {
  const auto range = expression_range(source, source_width);
  if (!range
      || range->left < std::numeric_limits<std::int32_t>::min()
      || range->left > std::numeric_limits<std::int32_t>::max()
      || range->right < std::numeric_limits<std::int32_t>::min()
      || range->right > std::numeric_limits<std::int32_t>::max()
      || range->width()
          > static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max() - base_offset)
              + 1U) {
    report(
        "FSIM-ELAB-DYNINDEX-001",
        "dynamic packed selection requires a concrete one-dimensional range "
        "representable by signed 32-bit indices and normalized offsets",
        span);
    return std::nullopt;
  }
  if (language_ == frontend::Language::Vhdl2008
      && !is_integer_expression(index)) {
    report(
        "FSIM-ELAB-DYNINDEX-002",
        "a dynamic VHDL array index requires an integer-family expression",
        index.span);
    return std::nullopt;
  }
  const auto lowered = lower_expression(index, 32);
  if (!lowered || register_width(*lowered) != 32) {
    report(
        "FSIM-ELAB-DYNINDEX-002",
        "a dynamic packed index must lower to the signed 32-bit runtime "
        "representation",
        index.span);
    return std::nullopt;
  }
  return DynamicIndex{
      *lowered, range->left, range->right, base_offset};
}

std::optional<Lowerer::ConstantSliceSelection>
Lowerer::constant_slice_selection(
    const Expression& expression,
    const std::size_t source_width) {
  if (expression.kind != ExpressionKind::Slice
      || expression.operands.size() != 3) {
    return std::nullopt;
  }
  const auto& source = expression.operands[0];
  const auto range = expression_range(source, source_width);
  if (!range) {
    return std::nullopt;
  }

  std::int64_t left = 0;
  std::int64_t right = 0;
  std::uint64_t width = 0;
  if (expression.text == "+:" || expression.text == "-:") {
    const auto base = static_integer_value(expression.operands[1]);
    const auto selected_width =
        static_integer_value(expression.operands[2]);
    if (!base || !selected_width || *selected_width <= 0) {
      return std::nullopt;
    }
    const auto distance = *selected_width - 1;
    std::int64_t lower = 0;
    std::int64_t upper = 0;
    if (expression.text == "+:") {
      if (*base
          > std::numeric_limits<std::int64_t>::max() - distance) {
        return std::nullopt;
      }
      lower = *base;
      upper = *base + distance;
    } else {
      if (*base
          < std::numeric_limits<std::int64_t>::min() + distance) {
        return std::nullopt;
      }
      lower = *base - distance;
      upper = *base;
    }
    if (range->left >= range->right) {
      left = upper;
      right = lower;
    } else {
      left = lower;
      right = upper;
    }
    width = static_cast<std::uint64_t>(*selected_width);
  } else {
    const auto parsed_left = static_integer_value(expression.operands[1]);
    const auto parsed_right = static_integer_value(expression.operands[2]);
    if (!parsed_left || !parsed_right) {
      return std::nullopt;
    }
    left = *parsed_left;
    right = *parsed_right;
    const bool selected_descending = left >= right;
    if (left != right
        && selected_descending != (range->left >= range->right)) {
      return std::nullopt;
    }
    width = index_distance(left, right) + 1;
  }
  const auto offset = select_offset(source, right, source_width);
  const auto left_offset = select_offset(source, left, source_width);
  if (!offset || !left_offset || width == 0
      || width > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }
  return ConstantSliceSelection{
      *offset, static_cast<std::size_t>(width)};
}

std::optional<RegisterId> Lowerer::lower_procedural_update_value(
    const Statement& statement,
    const RegisterId captured,
    const std::size_t target_width,
    const frontend::Type* contextual_target_type) {
  if (statement.procedural_update_kind
          == frontend::ProceduralUpdateKind::None
      || statement.procedural_update_operator.empty()
      || statement.value.kind != ExpressionKind::Binary
      || statement.value.operands.size() != 2
      || statement.value.text
          != statement.procedural_update_operator) {
    report(
        "FSIM-ELAB-106",
        "procedural update metadata does not match its normalized binary "
        "expression",
        statement.span);
    return std::nullopt;
  }
  const auto temporary_name =
      "@procedural-lvalue-" + std::to_string(captured) + "-"
      + std::to_string(process_.operations.size());
  const auto [local, inserted] =
      locals_.emplace(temporary_name, captured);
  if (!inserted) {
    report(
        "FSIM-ELAB-106",
        "procedural update could not allocate its captured lvalue",
        statement.span);
    return std::nullopt;
  }
  local_signed_.emplace(
      temporary_name, is_signed_expression(statement.target));
  if (target_width > 0
      && target_width - 1
          <= static_cast<std::size_t>(
              std::numeric_limits<std::int64_t>::max())) {
    local_ranges_.emplace(
        temporary_name,
        frontend::PackedRange{
            static_cast<std::int64_t>(target_width - 1), 0, true});
  }
  auto normalized = statement.value;
  normalized.operands[0] = Expression{
      ExpressionKind::Identifier,
      temporary_name,
      {},
      statement.target.span};
  const auto value = lower_expression(
      normalized, target_width, contextual_target_type);
  local_ranges_.erase(temporary_name);
  local_signed_.erase(temporary_name);
  locals_.erase(local);
  if (value) {
    procedural_update_result_ =
        statement.procedural_update_kind
                == frontend::ProceduralUpdateKind::Postfix
            ? captured
            : *value;
  }
  return value;
}

std::optional<RegisterId>
Lowerer::lower_procedural_update_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  if (language_ != frontend::Language::SystemVerilog2017
      || expression.operands.size() != 1
      || (expression.text != "pre++"
          && expression.text != "pre--"
          && expression.text != "post++"
          && expression.text != "post--")) {
    report(
        "FSIM-ELAB-SVEXPR-007",
        "an update expression requires a SystemVerilog prefix or postfix "
        "increment/decrement with one writable operand",
        expression.span);
    return std::nullopt;
  }
  const bool increment = expression.text.ends_with("++");
  const bool prefix = expression.text.starts_with("pre");
  const auto one = Expression{
      ExpressionKind::IntegerLiteral, "1", {}, expression.span};
  Statement update;
  update.kind = StatementKind::Assignment;
  update.assignment_kind = AssignmentKind::Blocking;
  update.target = expression.operands.front();
  update.value = Expression{
      ExpressionKind::Binary,
      increment ? "+" : "-",
      {update.target, one},
      expression.span};
  update.procedural_update_kind =
      prefix ? frontend::ProceduralUpdateKind::Prefix
             : frontend::ProceduralUpdateKind::Postfix;
  update.procedural_update_operator = increment ? "+" : "-";
  update.span = expression.span;

  procedural_update_result_.reset();
  lower_assignment(update);
  if (!procedural_update_result_) {
    return std::nullopt;
  }
  auto result = *procedural_update_result_;
  procedural_update_result_.reset();
  if (expected_width != 0
      && register_width(result) != expected_width) {
    result = resize_register(
        result,
        expected_width,
        is_signed_expression(expression));
  }
  (void)expected_type;
  return result;
}

void Lowerer::lower_force_release(const Statement& statement) {
  const bool force = statement.kind == StatementKind::Force;
  const Expression* base = &statement.target;
  if ((statement.target.kind == ExpressionKind::Index
       && statement.target.operands.size() == 2)
      || (statement.target.kind == ExpressionKind::Slice
          && statement.target.operands.size() == 3)) {
    base = &statement.target.operands.front();
  } else if (statement.target.kind != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVFORCE-001",
        "procedural force/release supports a signal, static bit-select, or "
        "static part-select",
        statement.target.span);
    return;
  }
  if (base->kind != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVFORCE-001",
        "nested or runtime-selected procedural force/release targets are "
        "not supported",
        statement.target.span);
    return;
  }

  auto target_name = base->text;
  std::uint32_t offset = 0;
  std::optional<std::size_t> selected_width;
  std::optional<frontend::ValueDomain> selected_domain;
  if (!signals_.contains(target_name)) {
    if (const auto selected = packed_member_reference(target_name)) {
      const auto width = selected->member->width();
      if (!width || *width == 0
          || *width > std::numeric_limits<std::uint32_t>::max()
          || selected->member->lsb_offset
              > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVFORCE-002",
            "packed force/release member has no executable layout",
            statement.target.span);
        return;
      }
      target_name = selected->base;
      offset = static_cast<std::uint32_t>(selected->member->lsb_offset);
      selected_width = static_cast<std::size_t>(*width);
      selected_domain = selected->member->domain;
    }
  }
  const auto signal = signals_.find(target_name);
  if (signal == signals_.end()) {
    report(
        "FSIM-ELAB-SVFORCE-002",
        locals_.contains(target_name)
            ? "procedural force/release of automatic local variables is not "
              "supported"
            : "procedural force/release requires a visible packed signal",
        statement.target.span);
    return;
  }
  const auto whole_width = design_.signal_info_[signal->second].width;
  const auto selection_source_width =
      selected_width.value_or(whole_width);
  if (statement.target.kind == ExpressionKind::Index) {
    const auto index = static_integer_value(statement.target.operands[1]);
    const auto selected = index
        ? select_offset(*base, *index, selection_source_width)
        : std::nullopt;
    if (!selected
        || static_cast<std::uint64_t>(offset) + *selected
            > std::numeric_limits<std::uint32_t>::max()) {
      report(
          "FSIM-ELAB-SVFORCE-001",
          "procedural force/release bit-select requires a static in-range "
          "index",
          statement.target.span);
      return;
    }
    offset += static_cast<std::uint32_t>(*selected);
    selected_width = 1;
  } else if (statement.target.kind == ExpressionKind::Slice) {
    const auto selected = constant_slice_selection(
        statement.target, selection_source_width);
    if (!selected
        || selected->offset
            > std::numeric_limits<std::uint32_t>::max() - offset
        || selected->width
            > std::numeric_limits<std::uint32_t>::max()) {
      report(
          "FSIM-ELAB-SVFORCE-001",
          "procedural force/release part-select requires static in-range "
          "bounds and width",
          statement.target.span);
      return;
    }
    offset += static_cast<std::uint32_t>(selected->offset);
    selected_width = selected->width;
  }
  const auto width = selected_width.value_or(whole_width);
  if (!force) {
    process_.operations.emplace_back(ReleaseSignalSlice{
        signal->second,
        offset,
        static_cast<std::uint32_t>(width)});
    return;
  }
  const auto* target_type =
      offset == 0 && width == whole_width ? object_type(target_name) : nullptr;
  auto value = lower_expression(statement.value, width, target_type);
  if (!value) {
    return;
  }
  if (register_width(*value) != width) {
    *value = resize_register(
        *value, width, is_signed_expression(statement.value));
  }
  const auto domain = selected_domain.value_or(
      target_type != nullptr
          ? target_type->domain
          : design_.signal_info_[signal->second].source_domain);
  if (is_two_state_domain(domain)
      && !is_two_state_domain(register_domain(*value))) {
    report(
        "FSIM-ELAB-SVFORCE-003",
        "force of a two-state target requires an explicit conversion",
        statement.value.span);
    return;
  }
  process_.operations.emplace_back(
      ForceSignalSlice{signal->second, *value, offset});
}

}  // namespace fsim::elaboration
