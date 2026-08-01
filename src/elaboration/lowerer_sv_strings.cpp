// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

bool Lowerer::is_string_expression(
    const Expression& expression) const {
  if (expression.kind == ExpressionKind::StringLiteral) {
    return true;
  }
  if (expression.kind == ExpressionKind::Identifier) {
    return string_locals_.contains(expression.text)
        || string_objects_.contains(expression.text)
        || (object_type(expression.text) != nullptr
            && object_type(expression.text)->domain
                == frontend::ValueDomain::String);
  }
  if (expression.kind == ExpressionKind::Concatenation) {
    return !expression.operands.empty()
        && std::ranges::all_of(
            expression.operands,
            [&](const Expression& operand) {
              return is_string_expression(operand);
            });
  }
  if (expression.kind == ExpressionKind::Call) {
    if (expression.text == "$sformatf") {
      return true;
    }
    if (!expression.operands.empty()
        && (expression.text == ".toupper"
            || expression.text == ".tolower"
            || expression.text == ".substr")) {
      return is_string_expression(expression.operands.front());
    }
    const auto* function = visible_function(expression.text);
    return function != nullptr
        && function->return_type.domain
            == frontend::ValueDomain::String;
  }
  return false;
}

std::optional<StringRegisterId>
Lowerer::lower_string_expression(
    const Expression& expression) {
  if (expression.kind == ExpressionKind::StringLiteral) {
    if (!expression.decoded_string) {
      report(
          "FSIM-ELAB-SVSTRING-006",
          "string literal has no decoded byte value",
          expression.span);
      return std::nullopt;
    }
    if (expression.decoded_string->size()
        > maximum_string_bytes) {
      report(
          "FSIM-ELAB-SVSTRING-007",
          "string literal exceeds the 4096-byte limit",
          expression.span);
      return std::nullopt;
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(
        LoadStringConstant{
            destination, *expression.decoded_string});
    return destination;
  }
  if (expression.kind == ExpressionKind::Identifier) {
    if (const auto local =
            string_locals_.find(expression.text);
        local != string_locals_.end()) {
      return local->second;
    }
    if (const auto object =
            string_objects_.find(expression.text);
        object != string_objects_.end()) {
      const auto destination = allocate_string_register();
      process_.operations.emplace_back(
          ReadStringObject{destination, object->second});
      return destination;
    }
    report(
        "FSIM-ELAB-SVSTRING-008",
        "unknown string object '" + expression.text + "'",
        expression.span);
    return std::nullopt;
  }
  if (expression.kind == ExpressionKind::Concatenation) {
    if (expression.operands.empty()) {
      report(
          "FSIM-ELAB-SVSTRING-009",
          "string concatenation requires at least one operand",
          expression.span);
      return std::nullopt;
    }
    std::vector<StringRegisterId> operands;
    operands.reserve(expression.operands.size());
    for (const auto& operand : expression.operands) {
      const auto value = lower_string_expression(operand);
      if (!value) {
        report(
            "FSIM-ELAB-SVSTRING-010",
            "string concatenation requires string operands",
            operand.span);
        return std::nullopt;
      }
      operands.push_back(*value);
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(
        ConcatenateStrings{
            destination, std::move(operands)});
    return destination;
  }
  if (expression.kind == ExpressionKind::Call) {
    if (expression.text == "$sformatf") {
      return lower_string_format(
          expression.operands, 0U, expression.text, expression.span);
    }
    if ((expression.text == ".toupper"
         || expression.text == ".tolower"
         || expression.text == ".substr")
        && !expression.operands.empty()
        && is_string_expression(expression.operands.front())) {
      const auto expected = expression.text == ".substr" ? 3U : 1U;
      if (expression.operands.size() != expected) {
        report(
            "FSIM-ELAB-SVSTRING-018",
            "runtime string method '" + expression.text
                + "' has an incompatible argument count",
            expression.span);
        return std::nullopt;
      }
      const auto source =
          lower_string_expression(expression.operands.front());
      if (!source) {
        return std::nullopt;
      }
      StringMethod method;
      method.operation =
          expression.text == ".toupper"
              ? StringMethodOperator::toupper
              : expression.text == ".tolower"
                    ? StringMethodOperator::tolower
                    : StringMethodOperator::substr;
      method.source = *source;
      method.string_destination = allocate_string_register();
      if (expression.text == ".substr") {
        auto first = lower_expression(expression.operands[1], 32);
        auto second = lower_expression(expression.operands[2], 32);
        if (!first || !second) {
          report(
              "FSIM-ELAB-SVSTRING-018",
              "substr indices must be signed 32-bit integral values",
              expression.span);
          return std::nullopt;
        }
        if (register_width(*first) != 32) {
          *first = resize_register(
              *first, 32,
              is_signed_expression(expression.operands[1]));
        }
        if (register_width(*second) != 32) {
          *second = resize_register(
              *second, 32,
              is_signed_expression(expression.operands[2]));
        }
        method.first = *first;
        method.second = *second;
      }
      process_.operations.emplace_back(method);
      return method.string_destination;
    }
    const auto found =
        function_indices_.find(expression.text);
    if (!function_support_initialized_
        || found == function_indices_.end()) {
      report(
          "FSIM-ELAB-SVSTRING-011",
          "unknown runtime string function '"
              + expression.text + "'",
          expression.span);
      return std::nullopt;
    }
    const auto function_index = found->second;
    auto& frame = function_frames_[function_index];
    const auto& function = *frame.source;
    if (function.return_type.domain
            != frontend::ValueDomain::String) {
      report(
          "FSIM-ELAB-SVSTRING-011",
          "string function call has an incompatible result or argument "
          "count",
          expression.span);
      return std::nullopt;
    }
    const auto actuals = bind_function_actuals(expression, function);
    if (!actuals
        || !validate_function_reference_actuals(function, *actuals)) {
      return std::nullopt;
    }
    if (!frame.allocated) {
      frame.result_is_string = true;
      frame.string_result = allocate_string_register();
      frame.arguments.reserve(function.arguments.size());
      frame.string_arguments.reserve(function.arguments.size());
      frame.container_arguments.reserve(function.arguments.size());
      frame.argument_is_string.reserve(function.arguments.size());
      frame.argument_is_container.reserve(function.arguments.size());
      for (const auto& argument : function.arguments) {
        if (argument.type.systemverilog_container) {
          const auto type =
              container_type(argument.type, argument.span);
          if (!type) {
            return std::nullopt;
          }
          frame.arguments.push_back({});
          frame.string_arguments.push_back({});
          frame.container_arguments.push_back(
              allocate_container_register(*type));
          frame.argument_is_string.push_back(false);
          frame.argument_is_container.push_back(true);
          continue;
        }
        if (argument.type.domain
            == frontend::ValueDomain::String) {
          frame.arguments.push_back({});
          frame.string_arguments.push_back(
              allocate_string_register());
          frame.container_arguments.push_back({});
          frame.argument_is_string.push_back(true);
          frame.argument_is_container.push_back(false);
          continue;
        }
        const auto width = argument.type.width();
        if (!width || *width == 0 || *width > 64) {
          report(
              "FSIM-ELAB-SVFUNC-004",
              "function argument '" + argument.name
                  + "' must be a string or have an executable packed "
                    "width in [1, 64]",
              argument.span);
          return std::nullopt;
        }
        frame.arguments.push_back(
            allocate_register(*width, argument.type.domain));
        frame.string_arguments.push_back({});
        frame.container_arguments.push_back({});
        frame.argument_is_string.push_back(false);
        frame.argument_is_container.push_back(false);
      }
      frame.allocated = true;
    }
    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
      const auto& formal = function.arguments[index];
      if (formal.direction == frontend::PortDirection::Output) {
        if (frame.argument_is_container[index]
            || frame.argument_is_string[index]) {
          report(
              "FSIM-ELAB-SVFUNC-011",
              "function output/inout/ref formals require a bounded packed "
              "integral type",
              formal.span);
          return std::nullopt;
        }
        const auto width = static_cast<std::size_t>(
            *formal.type.width());
        process_.operations.emplace_back(LoadConstant{
            frame.arguments[index],
            default_packed_value(formal.type, width)});
        continue;
      }
      if (frame.argument_is_container[index]) {
        const auto actual =
            lower_container_expression(
                *(*actuals)[index]);
        if (!actual) {
          return std::nullopt;
        }
        process_.operations.emplace_back(
            CopyContainerRegister{
                frame.container_arguments[index], *actual});
        continue;
      }
      if (frame.argument_is_string[index]) {
        const auto actual =
            lower_string_expression(*(*actuals)[index]);
        if (!actual) {
          return std::nullopt;
        }
        process_.operations.emplace_back(
            CopyStringRegister{
                frame.string_arguments[index], *actual});
        continue;
      }
      const auto width =
          static_cast<std::size_t>(*formal.type.width());
      auto actual = lower_expression(
          *(*actuals)[index], width, &formal.type);
      if (!actual) {
        return std::nullopt;
      }
      if (register_width(*actual) != width) {
        *actual = resize_register(
            *actual,
            width,
            is_signed_expression(*(*actuals)[index]));
      }
      process_.operations.emplace_back(
          CopyRegister{frame.arguments[index], *actual});
    }
    process_.operations.emplace_back(
        LoadStringConstant{frame.string_result, {}});
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(
        Call{
            frame.target.value_or(0),
            static_cast<InstructionIndex>(call_site + 1U),
            function_call_stack_});
    if (frame.target) {
      std::get<Call>(
          process_.operations[call_site]).target = *frame.target;
    } else {
      frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
      frame.queued = true;
      pending_functions_.push_back(function_index);
    }
    if (active_function_) {
      function_dependencies_[*active_function_].insert(
          function_index);
    }
    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
      const auto& formal = function.arguments[index];
      if (formal.direction == frontend::PortDirection::Input) {
        continue;
      }
      lower_callable_copy_out(
          *(*actuals)[index],
          formal.type,
          frame.arguments[index],
          frame.string_arguments[index],
          frame.container_arguments[index],
          frame.argument_is_string[index],
          frame.argument_is_container[index],
          "@function_copyout_" + std::to_string(function_index)
              + "_" + std::to_string(index));
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(
        CopyStringRegister{
            destination, frame.string_result});
    return destination;
  }
  report(
      "FSIM-ELAB-SVSTRING-010",
      "expression does not produce a runtime string value",
      expression.span);
  return std::nullopt;
}

bool Lowerer::lower_string_method_statement(
    const Statement& statement) {
  const auto& call = statement.value;
  if (call.kind != ExpressionKind::Call
      || (call.text != ".putc" && call.text != ".itoa"
          && call.text != ".hextoa" && call.text != ".octtoa"
          && call.text != ".bintoa")) {
    return false;
  }
  if (language_ != frontend::Language::SystemVerilog2017
      || call.operands.empty()
      || call.operands.front().kind != ExpressionKind::Identifier
      || !is_string_expression(call.operands.front())) {
    report(
        "FSIM-ELAB-SVSTRING-018",
        "mutating string methods require a direct writable string object",
        call.span);
    return true;
  }
  if (const auto object =
          string_objects_.find(call.operands.front().text);
      object != string_objects_.end()
      && read_only_string_objects_.contains(object->second)) {
    report(
        "FSIM-ELAB-SVPORT-011",
        "an input mutable string port is read-only",
        call.operands.front().span);
    return true;
  }
  const auto expected = call.text == ".putc" ? 3U : 2U;
  if (call.operands.size() != expected) {
    report(
        "FSIM-ELAB-SVSTRING-018",
        "runtime string method '" + call.text
            + "' has an incompatible argument count",
        call.span);
    return true;
  }
  const auto target = lower_string_expression(call.operands.front());
  auto first = lower_expression(call.operands[1], 32);
  if (!target || !first) {
    report(
        "FSIM-ELAB-SVSTRING-018",
        "mutating string method argument must be a 32-bit integral value",
        call.operands[1].span);
    return true;
  }
  if (register_width(*first) != 32) {
    *first = resize_register(
        *first, 32, is_signed_expression(call.operands[1]));
  }
  StringMethod method;
  method.source = *target;
  method.first = *first;
  if (call.text == ".putc") {
    auto character = lower_expression(call.operands[2], 8);
    if (!character) {
      report(
          "FSIM-ELAB-SVSTRING-018",
          "putc character must be an 8-bit integral value",
          call.operands[2].span);
      return true;
    }
    if (register_width(*character) != 8) {
      *character = resize_register(
          *character, 8,
          is_signed_expression(call.operands[2]));
    }
    method.operation = StringMethodOperator::putc;
    method.second = *character;
  } else {
    method.operation =
        call.text == ".itoa"
            ? StringMethodOperator::itoa
            : call.text == ".hextoa"
                  ? StringMethodOperator::hextoa
                  : call.text == ".octtoa"
                        ? StringMethodOperator::octtoa
                        : StringMethodOperator::bintoa;
  }
  process_.operations.emplace_back(method);
  if (const auto object =
          string_objects_.find(call.operands.front().text);
      object != string_objects_.end()) {
    process_.operations.emplace_back(
        WriteStringObject{object->second, *target});
  }
  return true;
}

} // namespace fsim::elaboration
