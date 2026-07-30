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
  if (!width || *width == 0 || *width > 64
      || !type.packed_members.empty()
      || type.domain == frontend::ValueDomain::String) {
    report(
        "FSIM-ELAB-SVCONTAINER-003",
        "container elements must be non-aggregate integral values with "
        "an executable width in 1..64",
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
  result.associative =
      type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::AssociativeArray;
  result.fixed =
      type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::StaticArray;
  if (result.associative) {
    const auto& index_type =
        type.systemverilog_container->associative_index_type;
    const auto index_width =
        index_type ? index_type->width() : std::nullopt;
    if (!index_type || !index_width || *index_width == 0
        || *index_width > 64
        || index_type->domain == frontend::ValueDomain::String
        || index_type->domain == frontend::ValueDomain::Unknown
        || !index_type->packed_members.empty()
        || index_type->vhdl_array) {
      report(
          "FSIM-ELAB-SVCONTAINER-013",
          "associative-array indices require a resolved integral scalar "
          "type with width in 1..64",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    result.index_width =
        static_cast<std::uint32_t>(*index_width);
    result.two_state_indices =
        is_two_state_domain(index_type->domain);
    result.signed_indices = index_type->is_signed;
  }
  if (type.systemverilog_container->queue_maximum) {
    const auto& maximum_expression =
        *type.systemverilog_container->queue_maximum;
    auto maximum_index = constant_index(maximum_expression);
    if (!maximum_index) {
      std::string error;
      const auto value =
          evaluate_systemverilog_constant_expression(
              maximum_expression, {}, {}, error);
      maximum_index =
          value ? value->integer_value() : std::nullopt;
    }
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
  if (result.fixed) {
    std::optional<std::int64_t> left;
    std::optional<std::int64_t> right;
    const auto bound_value =
        [](const Expression& expression) {
          if (const auto simple = constant_index(expression)) {
            return simple;
          }
          std::string error;
          const auto value =
              evaluate_systemverilog_constant_expression(
                  expression, {}, {}, error);
          return value
              ? value->integer_value()
              : std::optional<std::int64_t>{};
        };
    if (const auto& range =
            type.systemverilog_container
                ->static_range_expression) {
      left = bound_value(range->left);
      right = bound_value(range->right);
    } else if (
        const auto& concrete_range =
            type.systemverilog_container->static_range) {
      left = concrete_range->left;
      right = concrete_range->right;
    }
    const auto in_int32 =
        [](const std::int64_t value) {
          return value
                  >= std::numeric_limits<std::int32_t>::min()
              && value
                  <= std::numeric_limits<std::int32_t>::max();
        };
    if (!left || !right || !in_int32(*left)
        || !in_int32(*right)) {
      report(
          "FSIM-ELAB-SVCONTAINER-020",
          "static unpacked-array bounds must be locally constant "
          "32-bit integral values",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    const auto count =
        *left >= *right
            ? static_cast<std::uint64_t>(*left - *right) + 1U
            : static_cast<std::uint64_t>(*right - *left) + 1U;
    if (count > maximum_container_elements) {
      report(
          "FSIM-ELAB-SVCONTAINER-020",
          "static unpacked arrays are limited to 4096 elements",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    result.index_left = static_cast<std::int32_t>(*left);
    result.index_right = static_cast<std::int32_t>(*right);
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
  if (call.kind == ExpressionKind::Call
      && (call.text == ".exists"
          || call.text == ".first"
          || call.text == ".last"
          || call.text == ".next"
          || call.text == ".prev")) {
    (void)lower_expression(call, 32);
    return;
  }
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
  const auto mutates_receiver =
      call.text == ".delete"
      || call.text == ".push_front"
      || call.text == ".push_back"
      || call.text == ".pop_front"
      || call.text == ".pop_back";
  if (mutates_receiver
      && read_only_container_objects_.contains(
          receiver.text)) {
    report(
        "FSIM-ELAB-SVPORT-009",
        "an input container port is read-only within its module",
        receiver.span);
    return;
  }
  const auto target = lower_container_expression(receiver);
  const auto* type = object_type(receiver.text);
  if (!target || type == nullptr) {
    return;
  }
  const auto width = type->width();
  const auto runtime_type =
      container_type(*type, call.span);
  if (!width || !runtime_type) {
    return;
  }
  if (call.text == ".delete") {
    if (call.operands.size() == 2) {
      if (!runtime_type->associative) {
        report(
            "FSIM-ELAB-SVCONTAINER-018",
            "delete(index) requires an associative-array receiver",
            call.span);
        return;
      }
      const auto index = lower_expression(
          call.operands[1],
          runtime_type->index_width,
          type->systemverilog_container
              ->associative_index_type.get());
      if (!index) {
        return;
      }
      process_.operations.emplace_back(
          DeleteContainer{*target, *index});
    } else {
      if (runtime_type->fixed) {
        report(
            "FSIM-ELAB-SVCONTAINER-021",
            "delete() cannot clear a static unpacked array",
            call.span);
        return;
      }
      process_.operations.emplace_back(
          DeleteContainer{*target, std::nullopt});
    }
  } else if (
      call.text == ".push_front"
      || call.text == ".push_back") {
    if (!runtime_type->queue) {
      report(
          "FSIM-ELAB-SVCONTAINER-019",
          "push_front/push_back require a queue receiver",
          call.span);
      return;
    }
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
    if (!runtime_type->queue) {
      report(
          "FSIM-ELAB-SVCONTAINER-019",
          "pop_front/pop_back require a queue receiver",
          call.span);
      return;
    }
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
