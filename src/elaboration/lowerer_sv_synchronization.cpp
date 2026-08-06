// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
namespace {

[[nodiscard]] bool is_mailbox(const frontend::Type* type) {
  return type != nullptr && type->spelling == "mailbox";
}

[[nodiscard]] bool is_semaphore(const frontend::Type* type) {
  return type != nullptr && type->spelling == "semaphore";
}

[[nodiscard]] const frontend::Type* mailbox_element_type(
    const frontend::Type* type) {
  if (!is_mailbox(type)
      || type->systemverilog_class_parameter_actuals.size() != 1U
      || !type->systemverilog_class_parameter_actuals.front().type_actual) {
    return nullptr;
  }
  return type->systemverilog_class_parameter_actuals.front()
      .type_actual.get();
}

}  // namespace

Lowerer::ExpressionAttempt Lowerer::lower_synchronization_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  using namespace runtime::simir;
  if (language_ != frontend::Language::SystemVerilog2017
      || expression.kind != ExpressionKind::Call) {
    return {};
  }

  if (expression.text.starts_with("@sv-sync-new:")) {
    const auto kind = std::string_view{expression.text}.substr(13U);
    const bool mailbox = kind == "mailbox" && is_mailbox(expected_type);
    const bool semaphore = kind == "semaphore" && is_semaphore(expected_type);
    if ((!mailbox && !semaphore) || expected_width != 64U
        || expression.operands.size() > 1U) {
      report(
          "FSIM-ELAB-SVSYNC-001",
          "mailbox/semaphore construction requires a compatible 64-bit "
          "destination and zero or one count argument",
          expression.span);
      return std::nullopt;
    }
    const auto count = expression.operands.empty()
        ? allocate_register(32, frontend::ValueDomain::Integer)
        : lower_expression(expression.operands.front(), 32);
    if (!count) return std::nullopt;
    if (expression.operands.empty()) {
      process_.operations.emplace_back(LoadConstant{
          *count, PackedLogic4::from_aval_bval(32, 0, 0)});
    }
    const auto destination = allocate_register(
        64, frontend::ValueDomain::Bit2);
    if (mailbox) {
      const auto* element = mailbox_element_type(expected_type);
      const auto width = element ? element->width() : std::nullopt;
      if (!width || *width == 0
          || *width > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVSYNC-002",
            "typed mailbox construction requires exactly one finite "
            "positive packed element type",
            expression.span);
        return std::nullopt;
      }
      process_.operations.emplace_back(MailboxCreate{
          destination, *count, static_cast<std::uint32_t>(*width)});
    } else {
      process_.operations.emplace_back(
          SemaphoreCreate{destination, *count});
    }
    return destination;
  }

  if (!expression.text.starts_with('.') || expression.operands.empty()
      || expression.operands.front().kind != ExpressionKind::Identifier) {
    return {};
  }
  const auto& receiver_expression = expression.operands.front();
  const auto* receiver_type = object_type(receiver_expression.text);
  if (!is_mailbox(receiver_type) && !is_semaphore(receiver_type)) {
    return {};
  }
  const auto receiver = lower_expression(
      receiver_expression, 64, receiver_type);
  if (!receiver) return std::nullopt;
  const auto method = std::string_view{expression.text}.substr(1U);
  const auto arguments = expression.operands.size() - 1U;
  const auto result = [&]() {
    return allocate_register(32, frontend::ValueDomain::Integer);
  };
  const auto resize_result = [&](const RegisterId value) {
    return expected_width != 0U && expected_width != 32U
        ? resize_register(value, expected_width, false)
        : value;
  };

  if (is_mailbox(receiver_type)) {
    const auto* element = mailbox_element_type(receiver_type);
    const auto width = element ? element->width() : std::nullopt;
    if (!width || *width == 0
        || *width > std::numeric_limits<std::uint32_t>::max()) {
      report(
          "FSIM-ELAB-SVSYNC-002",
          "mailbox operations require exactly one finite positive packed "
          "element type",
          expression.span);
      return std::nullopt;
    }
    const auto element_width = static_cast<std::uint32_t>(*width);
    if (method == "num" && arguments == 0U) {
      const auto destination = result();
      process_.operations.emplace_back(
          MailboxNum{destination, *receiver});
      return resize_result(destination);
    }
    if ((method == "put" || method == "try_put")
        && arguments == 1U) {
      const auto source = lower_expression(
          expression.operands[1], *width, element);
      if (!source) return std::nullopt;
      if (method == "put") {
        process_.operations.emplace_back(MailboxPut{
            *receiver, *source, element_width, std::nullopt});
        return std::optional<RegisterId>{};
      }
      const auto destination = result();
      process_.operations.emplace_back(MailboxPut{
          *receiver, *source, element_width, destination});
      return resize_result(destination);
    }
    const bool get = method == "get" || method == "try_get";
    const bool peek = method == "peek" || method == "try_peek";
    if ((get || peek) && arguments == 1U) {
      const auto value = allocate_register(*width, element->domain);
      std::optional<RegisterId> success;
      if (method.starts_with("try_")) success = result();
      process_.operations.emplace_back(MailboxGet{
          *receiver, value, element_width, success, peek});
      lower_callable_copy_out(
          expression.operands[1], *element, value, {}, {}, false, false,
          "@mailbox_copyout_" + std::to_string(value));
      if (!success) return std::optional<RegisterId>{};
      return resize_result(*success);
    }
    report(
        "FSIM-ELAB-SVSYNC-003",
        "mailbox method requires the standard num, put/try_put, "
        "get/try_get, or peek/try_peek arity",
        expression.span);
    return std::nullopt;
  }

  if ((method == "get" || method == "try_get" || method == "put")
      && arguments <= 1U) {
    const auto keys = arguments == 0U
        ? allocate_register(32, frontend::ValueDomain::Integer)
        : lower_expression(expression.operands[1], 32);
    if (!keys) return std::nullopt;
    if (arguments == 0U) {
      process_.operations.emplace_back(LoadConstant{
          *keys, PackedLogic4::from_aval_bval(32, 1, 0)});
    }
    if (method == "put") {
      process_.operations.emplace_back(SemaphorePut{*receiver, *keys});
      return std::optional<RegisterId>{};
    }
    if (method == "get") {
      process_.operations.emplace_back(
          SemaphoreGet{*receiver, *keys, std::nullopt});
      return std::optional<RegisterId>{};
    }
    const auto destination = result();
    process_.operations.emplace_back(
        SemaphoreGet{*receiver, *keys, destination});
    return resize_result(destination);
  }
  report(
      "FSIM-ELAB-SVSYNC-004",
      "semaphore method requires get/try_get/put with zero or one key-count "
      "argument",
      expression.span);
  return std::nullopt;
}

bool Lowerer::lower_synchronization_method_statement(
    const Statement& statement) {
  Expression call;
  if (statement.kind == StatementKind::TaskCall) {
    const auto separator = statement.task_name.rfind('.');
    if (separator == std::string::npos) return false;
    const auto receiver_name = statement.task_name.substr(0, separator);
    const auto* type = object_type(receiver_name);
    if (!is_mailbox(type) && !is_semaphore(type)) return false;
    call = Expression{
        ExpressionKind::Call,
        statement.task_name.substr(separator),
        {Expression{
            ExpressionKind::Identifier, receiver_name, {}, statement.span}},
        statement.span};
    call.operands.insert(
        call.operands.end(),
        statement.task_arguments.begin(),
        statement.task_arguments.end());
  } else if (statement.kind == StatementKind::ContainerMethod) {
    call = statement.value;
  } else {
    return false;
  }
  const auto lowered = lower_synchronization_expression(call, 0, nullptr);
  return lowered.handled;
}

}  // namespace fsim::elaboration
