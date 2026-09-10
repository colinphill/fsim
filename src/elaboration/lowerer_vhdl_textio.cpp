// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

bool Lowerer::lower_vhdl_textio_procedure_call(
    const Statement& statement) {
  const bool read_line = statement.procedure_name == "readline";
  const bool write_line = statement.procedure_name == "writeline";
  const bool read = statement.procedure_name == "read";
  const bool write = statement.procedure_name == "write";
  if (!read_line && !write_line && !read && !write) return false;

  const auto positional = [&] {
    std::vector<const Expression*> result;
    for (const auto& argument : statement.procedure_arguments) {
      if (!argument.formal) result.push_back(&argument.value);
    }
    return result;
  }();
  const auto actual = [&](const std::string_view formal,
                          const std::size_t index) -> const Expression* {
    const auto named = std::ranges::find_if(
        statement.procedure_arguments,
        [&](const auto& argument) {
          return argument.formal && *argument.formal == formal;
        });
    if (named != statement.procedure_arguments.end()) return &named->value;
    return index < positional.size() ? positional[index] : nullptr;
  };
  const auto line_type = [](const frontend::Type* type) {
    if (type == nullptr || type->domain != frontend::ValueDomain::String) {
      return false;
    }
    const auto dot = type->spelling.find_last_of('.');
    return type->spelling.substr(
               dot == std::string::npos ? 0U : dot + 1U)
        == "line";
  };
  const auto* first = actual("l", 0);
  if ((read || write)
      && (first == nullptr || first->kind != ExpressionKind::Identifier
          || !line_type(object_type(first->text)))) {
    return false;
  }

  if (read_line || write_line) {
    const auto* file = actual("f", 0);
    const auto* line = actual("l", 1);
    const auto file_local = file != nullptr
            && file->kind == ExpressionKind::Identifier
        ? locals_.find(file->text) : locals_.end();
    const auto line_local = line != nullptr
            && line->kind == ExpressionKind::Identifier
        ? string_locals_.find(line->text) : string_locals_.end();
    const auto* file_type = file != nullptr
            && file->kind == ExpressionKind::Identifier
        ? object_type(file->text) : nullptr;
    const bool text_file = file_type != nullptr && file_type->vhdl_file
        && !file_type->vhdl_file->element_types.empty()
        && file_type->vhdl_file->element_types.front().domain
            == frontend::ValueDomain::String;
    if (statement.procedure_arguments.size() != 2
        || file_local == locals_.end() || !text_file
        || line_local == string_locals_.end()
        || !line_type(line == nullptr ? nullptr : object_type(line->text))) {
      report(
          "FSIM-ELAB-VHTEXTIO-001",
          std::string{read_line ? "readline" : "writeline"}
              + " requires a text file and writable line object",
          statement.span);
      return true;
    }
    if (read_line) {
      const auto count = allocate_register(
          32, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(FileReadLine{
          count, file_local->second, line_local->second, 0,
          FileReadKind::line, true});
    } else {
      process_.operations.emplace_back(FileWriteString{
          file_local->second, line_local->second, {}, {}, true, true});
    }
    return true;
  }

  const auto line_local = string_locals_.find(first->text);
  if (line_local == string_locals_.end()) {
    report(
        "FSIM-ELAB-VHTEXTIO-002",
        "TextIO read/write requires a writable line object",
        first->span);
    return true;
  }
  const auto* value = actual("value", 1);
  if (value == nullptr) {
    report(
        "FSIM-ELAB-VHTEXTIO-003",
        "TextIO read/write requires a value actual",
        statement.span);
    return true;
  }

  if (read) {
    const auto* good = actual("good", 2);
    if (statement.procedure_arguments.size() < 2
        || statement.procedure_arguments.size() > 3) {
      report(
          "FSIM-ELAB-VHTEXTIO-003",
          "TextIO read accepts line, value, and optional good actuals",
          statement.span);
      return true;
    }
    const auto target = value->kind == ExpressionKind::Identifier
        ? locals_.find(value->text) : locals_.end();
    const auto* target_type = value->kind == ExpressionKind::Identifier
        ? object_type(value->text) : nullptr;
    if (target == locals_.end() || target_type == nullptr) {
      report(
          "FSIM-ELAB-VHTEXTIO-004",
          "TextIO read value must be a writable bounded scalar",
          value->span);
      return true;
    }
    InputScanConversion conversion;
    conversion.target.id = target->second;
    conversion.target.kind = InputScanTargetKind::packed_register;
    conversion.target.two_state = true;
    if (target_type->domain == frontend::ValueDomain::Integer) {
      conversion.format = InputScanFormat::decimal;
      conversion.target.width = static_cast<std::uint32_t>(
          target_type->width().value_or(32U));
    } else if (target_type->domain == frontend::ValueDomain::Boolean) {
      conversion.format = InputScanFormat::boolean_value;
      conversion.target.width = 1;
    } else if (target_type->domain == frontend::ValueDomain::Bit2
               && target_type->enumeration_literals.empty()
               && target_type->width() == 1) {
      conversion.format = InputScanFormat::binary;
      conversion.target.width = 1;
    } else {
      report(
          "FSIM-ELAB-VHTEXTIO-005",
          "bounded TextIO read supports integer, boolean, and bit values",
          value->span);
      return true;
    }
    std::optional<RegisterId> success;
    if (good != nullptr) {
      const auto good_local = good->kind == ExpressionKind::Identifier
          ? locals_.find(good->text) : locals_.end();
      const auto* good_type = good->kind == ExpressionKind::Identifier
          ? object_type(good->text) : nullptr;
      if (good_local == locals_.end() || good_type == nullptr
          || good_type->domain != frontend::ValueDomain::Boolean) {
        report(
            "FSIM-ELAB-VHTEXTIO-006",
            "TextIO read good actual must be a writable boolean",
            good->span);
        return true;
      }
      success = good_local->second;
    } else if (vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
      success = allocate_register(1U, frontend::ValueDomain::Boolean);
    }
    const auto count = allocate_register(
        32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(FileScan{
        count,
        0,
        line_local->second,
        true,
        {std::move(conversion)},
        {},
        !success,
        success,
        true});
    if (good == nullptr
        && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
      const auto branch = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Branch {
          *success, branch + 2U, branch + 1U,
          UnknownBranchPolicy::when_false });
      process_.operations.emplace_back(VhdlAssertApi {
          VhdlAssertApiKind::record_read_failure,
          std::nullopt, std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, std::nullopt,
          SourceLocation {
              statement.span.source_name.str(),
              static_cast<std::uint32_t>(statement.span.begin.line),
              static_cast<std::uint32_t>(statement.span.begin.column) } });
    }
    return true;
  }

  if (statement.procedure_arguments.size() < 2
      || statement.procedure_arguments.size() > 4) {
    report(
        "FSIM-ELAB-VHTEXTIO-003",
        "TextIO write accepts line, value, justified, and field actuals",
        statement.span);
    return true;
  }
  const auto* justified = actual("justified", 2);
  const auto* field = actual("field", 3);
  bool left_justify{};
  if (justified != nullptr) {
    if (justified->kind != ExpressionKind::Identifier
        || (justified->text != "left" && justified->text != "right")) {
      report(
          "FSIM-ELAB-VHTEXTIO-007",
          "bounded TextIO justification must be static left or right",
          justified->span);
      return true;
    }
    left_justify = justified->text == "left";
  }
  std::uint32_t minimum_width{};
  if (field != nullptr) {
    std::string error;
    const auto width = evaluate_constant_expression(*field, {}, error);
    if (!width || *width < 0
        || *width > static_cast<std::int64_t>(maximum_string_bytes)) {
      report(
          "FSIM-ELAB-VHTEXTIO-008",
          "bounded TextIO field must be a static value in 0..4096",
          field->span);
      return true;
    }
    minimum_width = static_cast<std::uint32_t>(*width);
  }
  const auto append_string = [&](const StringRegisterId source) {
    StringMethod operation;
    operation.operation = StringMethodOperator::format_string;
    operation.source = line_local->second;
    operation.argument = source;
    operation.minimum_width = minimum_width;
    operation.left_justify = left_justify;
    process_.operations.emplace_back(operation);
  };
  const auto resolved_value_type = vhdl_expression_type(*value);
  const auto* value_type = resolved_value_type
      ? &*resolved_value_type : nullptr;
  if (is_string_expression(*value)) {
    const auto source = lower_string_expression(*value);
    if (source) append_string(*source);
    return true;
  }
  const auto domain = value_type != nullptr
      ? value_type->domain
      : value->kind == ExpressionKind::IntegerLiteral
          ? frontend::ValueDomain::Integer
          : value->kind == ExpressionKind::BooleanLiteral
              ? frontend::ValueDomain::Boolean
              : value->kind == ExpressionKind::LogicLiteral
                  ? frontend::ValueDomain::Bit2
                  : frontend::ValueDomain::Unknown;
  if (domain == frontend::ValueDomain::Integer
      || (domain == frontend::ValueDomain::Bit2
          && (value_type == nullptr
              || (value_type->enumeration_literals.empty()
                  && value_type->width() == 1)))) {
    const auto width = domain == frontend::ValueDomain::Integer
        ? static_cast<std::size_t>(
              value_type != nullptr
                  ? value_type->width().value_or(32U)
                  : frontend::vhdl_predefined_integer_storage_width(
                        vhdl_standard_))
        : std::size_t { 1 };
    const auto source = lower_expression(*value, width, value_type);
    if (!source) return true;
    StringMethod operation;
    operation.operation = StringMethodOperator::format_packed;
    operation.source = line_local->second;
    operation.first = *source;
    operation.second = allocate_register(
        32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        operation.second, unsigned_value(width, 32)});
    operation.format = domain == frontend::ValueDomain::Integer
        ? OutputFormat::decimal : OutputFormat::binary;
    operation.signed_decimal = domain == frontend::ValueDomain::Integer;
    operation.minimum_width = minimum_width;
    operation.left_justify = left_justify;
    process_.operations.emplace_back(operation);
    return true;
  }
  if (domain == frontend::ValueDomain::Boolean) {
    const auto condition = lower_expression(*value, 1, value_type);
    if (!condition) return true;
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        *condition, 0, 0, UnknownBranchPolicy::error});
    const auto true_start = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto true_text = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant{true_text, "TRUE"});
    append_string(true_text);
    const auto skip_false = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump{});
    const auto false_start = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto false_text = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant{false_text, "FALSE"});
    append_string(false_text);
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[branch] = Branch{
        *condition, true_start, false_start, UnknownBranchPolicy::error};
    process_.operations[skip_false] = Jump{end};
    return true;
  }
  report(
      "FSIM-ELAB-VHTEXTIO-009",
      "bounded TextIO write supports integer, boolean, bit, and string values",
      value->span);
  return true;
}

} // namespace fsim::elaboration
