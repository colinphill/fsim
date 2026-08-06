// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace elaboration_detail;
using namespace runtime::simir;

Lowerer::ExpressionAttempt Lowerer::lower_file_binary_read(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Call
      || expression.text != "$fread") {
    return {};
  }
  const bool named = std::ranges::any_of(
      expression.call_argument_names,
      [](const std::string& name) { return !name.empty(); });
  if (language_ != frontend::Language::SystemVerilog2017 || named
      || expression.operands.size() < 2U
      || expression.operands.size() > 4U) {
    report(
        "FSIM-ELAB-SVFILE-013",
        "$fread requires a target, handle, and optional start/count",
        expression.span);
    return std::nullopt;
  }
  const auto& target = expression.operands[0];
  if (target.kind != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVFILE-014",
        "$fread target must be a direct writable packed value or memory",
        target.span);
    return std::nullopt;
  }
  FileBinaryRead operation;
  if (const auto local = container_locals_.find(target.text);
      local != container_locals_.end()) {
    const auto& type = process_.container_register_types.at(local->second);
    const auto width =
        runtime::simir::container_packed_element_width(type);
    if (!type.fixed || !width
        || *width > std::numeric_limits<std::uint32_t>::max()) {
      report(
          "FSIM-ELAB-SVFILE-014",
          "$fread memory target must be fixed with recursively packed "
          "elements",
          target.span);
      return std::nullopt;
    }
    operation.target_kind = FileBinaryTargetKind::container_register;
    operation.target = local->second;
    operation.width = static_cast<std::uint32_t>(*width);
    operation.two_state = type.two_state;
    operation.scalar_kind = type.scalar_kind;
  } else if (const auto object = container_objects_.find(target.text);
             object != container_objects_.end()
             && !read_only_container_objects_.contains(target.text)) {
    const auto* frontend_type = object_type(target.text);
    const auto type = frontend_type != nullptr
        ? container_type(*frontend_type, target.span) : std::nullopt;
    const auto width = type
        ? runtime::simir::container_packed_element_width(*type)
        : std::nullopt;
    if (!type || !type->fixed || !width
        || *width > std::numeric_limits<std::uint32_t>::max()) {
      report(
          "FSIM-ELAB-SVFILE-014",
          "$fread memory target must be fixed with recursively packed "
          "elements",
          target.span);
      return std::nullopt;
    }
    operation.target_kind = FileBinaryTargetKind::container_object;
    operation.target = object->second;
    operation.width = static_cast<std::uint32_t>(*width);
    operation.two_state = type->two_state;
    operation.scalar_kind = type->scalar_kind;
  } else if (const auto packed_local = locals_.find(target.text);
             packed_local != locals_.end()) {
    const auto width = register_width(packed_local->second);
    if (width == 0
        || width > std::numeric_limits<std::uint32_t>::max()
        || expression.operands.size() != 2U) {
      report(
          "FSIM-ELAB-SVFILE-014",
          "packed $fread targets require a positive executable width and no "
          "start/count",
          target.span);
      return std::nullopt;
    }
    operation.target_kind = FileBinaryTargetKind::packed_register;
    operation.target = packed_local->second;
    operation.width = static_cast<std::uint32_t>(width);
    operation.two_state =
        is_two_state_domain(register_domain(packed_local->second));
    if (const auto* type = object_type(target.text); type != nullptr) {
      operation.scalar_kind = type->systemverilog_scalar;
      operation.two_state = operation.scalar_kind
              != frontend::SystemVerilogScalarKind::None
          || operation.two_state;
    }
  } else if (const auto signal = signals_.find(target.text);
             signal != signals_.end()
             && !read_only_signals_.contains(signal->second)) {
    const auto width = design_.signal_info_[signal->second].width;
    const auto* type = object_type(target.text);
    if (width == 0
        || width > std::numeric_limits<std::uint32_t>::max()
        || type == nullptr
        || expression.operands.size() != 2U) {
      report(
          "FSIM-ELAB-SVFILE-014",
          "packed $fread targets require a positive executable width and no "
          "start/count",
          target.span);
      return std::nullopt;
    }
    operation.target_kind = FileBinaryTargetKind::packed_signal;
    operation.target = signal->second;
    operation.width = static_cast<std::uint32_t>(width);
    operation.two_state = is_two_state_domain(type->domain);
    operation.scalar_kind = type->systemverilog_scalar;
    operation.two_state = operation.scalar_kind
            != frontend::SystemVerilogScalarKind::None
        || operation.two_state;
  } else {
    report(
        "FSIM-ELAB-SVFILE-014",
        "$fread target is unknown, read-only, or unsupported",
        target.span);
    return std::nullopt;
  }

  const auto& handle_expression = expression.operands[1];
  const auto* handle_type = handle_expression.kind == ExpressionKind::Identifier
      ? object_type(handle_expression.text) : nullptr;
  if (!(handle_expression.kind == ExpressionKind::IntegerLiteral
        || (handle_type != nullptr
            && handle_type->domain == frontend::ValueDomain::Integer)
        || is_integer_expression(handle_expression))) {
    report(
        "FSIM-ELAB-SVFILE-013",
        "$fread handle must be a 32-bit integer expression",
        handle_expression.span);
    return std::nullopt;
  }
  auto handle = lower_expression(handle_expression, 32);
  if (!handle) return std::nullopt;
  if (register_width(*handle) != 32) {
    *handle = resize_register(
        *handle, 32, is_signed_expression(handle_expression));
  }
  operation.handle = *handle;
  const auto lower_bound = [&](const std::size_t operand)
      -> std::optional<RegisterId> {
    auto value = lower_expression(expression.operands[operand], 32);
    if (value && register_width(*value) != 32) {
      *value = resize_register(
          *value, 32, is_signed_expression(expression.operands[operand]));
    }
    return value;
  };
  if (expression.operands.size() >= 3U) {
    const auto start = lower_bound(2U);
    if (!start) return std::nullopt;
    operation.start = *start;
    operation.has_start = true;
  }
  if (expression.operands.size() == 4U) {
    const auto count = lower_bound(3U);
    if (!count) return std::nullopt;
    operation.count = *count;
    operation.has_count = true;
  }
  const auto destination =
      allocate_register(32, frontend::ValueDomain::Bit2);
  operation.destination = destination;
  process_.operations.emplace_back(std::move(operation));
  return destination;
}

}  // namespace fsim::elaboration
