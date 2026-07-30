// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<ContainerType> Lowerer::container_type(
    const frontend::Type& type,
    const frontend::SourceSpan& span) {
  if (!type.systemverilog_container) {
    return std::nullopt;
  }
  const auto width = type.width();
  if (!width || *width == 0 || *width > 64) {
    report(
        "FSIM-ELAB-SVCONTAINER-003",
        "container elements must have an executable width in 1..64",
        span);
    return std::nullopt;
  }
  ContainerType result;
  result.element_width = static_cast<std::uint32_t>(*width);
  result.two_state = is_two_state_domain(type.domain);
  result.signed_elements = type.is_signed;
  result.queue =
      type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::Queue;
  if (type.systemverilog_container->queue_maximum) {
    const auto maximum_index = constant_index(
        *type.systemverilog_container->queue_maximum);
    if (!maximum_index || *maximum_index < 0
        || *maximum_index >= static_cast<std::int64_t>(
            maximum_container_elements)) {
      report(
          "FSIM-ELAB-SVCONTAINER-004",
          "bounded queue maximum index must be a known value in 0..4095",
          type.systemverilog_container->queue_maximum->span);
      return std::nullopt;
    }
    result.maximum_elements =
        static_cast<std::uint32_t>(*maximum_index + 1);
  }
  return result;
}

ContainerRegisterId Lowerer::allocate_container_register(
    const ContainerType& type) {
  const auto id = next_container_register_++;
  process_.container_register_types.push_back(type);
  return id;
}

bool Lowerer::is_container_expression(
    const Expression& expression) const {
  if (expression.kind == ExpressionKind::Identifier) {
    return container_locals_.contains(expression.text)
        || container_objects_.contains(expression.text);
  }
  return false;
}

std::optional<ContainerRegisterId>
Lowerer::lower_container_expression(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVCONTAINER-005",
        "container values currently require a direct object reference",
        expression.span);
    return std::nullopt;
  }
  if (const auto local = container_locals_.find(expression.text);
      local != container_locals_.end()) {
    return local->second;
  }
  const auto object = container_objects_.find(expression.text);
  const auto* type = object_type(expression.text);
  if (object == container_objects_.end() || type == nullptr) {
    report(
        "FSIM-ELAB-SVCONTAINER-006",
        "unknown container object '" + expression.text + "'",
        expression.span);
    return std::nullopt;
  }
  const auto runtime_type = container_type(*type, expression.span);
  if (!runtime_type) {
    return std::nullopt;
  }
  const auto destination =
      allocate_container_register(*runtime_type);
  process_.operations.emplace_back(
      ReadContainerObject{destination, object->second});
  return destination;
}

void Lowerer::lower_container_method(
    const Statement& statement) {
  const auto& call = statement.value;
  if (call.kind != ExpressionKind::Call
      || call.operands.empty()
      || call.operands.front().kind
          != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVCONTAINER-007",
        "container method requires a direct object receiver",
        statement.span);
    return;
  }
  const auto& receiver = call.operands.front();
  const auto target = lower_container_expression(receiver);
  const auto* type = object_type(receiver.text);
  if (!target || type == nullptr) {
    return;
  }
  const auto width = type->width();
  if (!width) {
    return;
  }
  if (call.text == ".delete") {
    process_.operations.emplace_back(DeleteContainer{*target});
  } else if (
      call.text == ".push_front"
      || call.text == ".push_back") {
    if (call.operands.size() != 2) {
      return;
    }
    const auto value =
        lower_expression(call.operands[1], *width, type);
    if (!value) {
      return;
    }
    process_.operations.emplace_back(
        PushContainer{
            *target, *value,
            call.text == ".push_front"});
  } else if (
      call.text == ".pop_front"
      || call.text == ".pop_back") {
    const auto discarded =
        allocate_register(*width, type->domain);
    process_.operations.emplace_back(
        PopContainer{
            discarded, *target,
            call.text == ".pop_front"});
  } else {
    report(
        "FSIM-ELAB-SVCONTAINER-008",
        "unsupported container method '" + call.text + "'",
        call.span);
    return;
  }
  if (const auto object =
          container_objects_.find(receiver.text);
      object != container_objects_.end()) {
    process_.operations.emplace_back(
        WriteContainerObject{object->second, *target});
  }
}

}  // namespace fsim::elaboration
