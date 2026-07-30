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

Lowerer::ExpressionAttempt Lowerer::lower_container_query(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Call) {
    return {};
  }
  const bool bound_query =
      expression.text == "$left"
      || expression.text == "$right"
      || expression.text == "$low"
      || expression.text == "$high"
      || expression.text == "$increment";
  const bool size_query = expression.text == "$size";
  const bool bits_query = expression.text == "$bits";
  const bool dimensions_query =
      expression.text == "$dimensions";
  const bool unpacked_dimensions_query =
      expression.text == "$unpacked_dimensions";
  if (!bound_query && !size_query && !bits_query
      && !dimensions_query
      && !unpacked_dimensions_query) {
    return {};
  }
  if (expression.operands.empty()) {
    return {};
  }
  if (!is_container_expression(
          expression.operands.front())) {
    const auto& operand = expression.operands.front();
    if (operand.kind == ExpressionKind::Identifier
        && visible_type_mark(operand.text) != nullptr) {
      report(
          "FSIM-ELAB-SVQUERY-004",
          expression.text
              + " type-only forms are outside the bounded "
                "container-query subset",
          operand.span);
      return std::nullopt;
    }
    return {};
  }
  const bool accepts_dimension = bound_query || size_query;
  const auto maximum_arguments =
      accepts_dimension ? std::size_t{2} : std::size_t{1};
  if (language_ != frontend::Language::SystemVerilog2017
      || expression.operands.size() > maximum_arguments) {
    report(
        "FSIM-ELAB-SVQUERY-001",
        expression.text
            + " requires a direct one-dimensional SystemVerilog "
              "container object",
        expression.span);
    return std::nullopt;
  }
  if (expression.operands.size() == 2) {
    auto dimension =
        constant_index(expression.operands[1]);
    if (!dimension) {
      std::string error;
      if (const auto value =
              evaluate_systemverilog_constant_expression(
                  expression.operands[1], {}, {}, error)) {
        dimension = value->integer_value();
      }
    }
    if (!dimension || *dimension != 1) {
      report(
          "FSIM-ELAB-SVQUERY-002",
          expression.text
              + " supports only the constant unpacked dimension 1",
          expression.operands[1].span);
      return std::nullopt;
    }
  }
  const auto& operand = expression.operands.front();
  const auto* source_type =
      operand.kind == ExpressionKind::Identifier
          ? object_type(operand.text)
          : nullptr;
  const auto runtime_type =
      source_type
          ? container_type(*source_type, operand.span)
          : std::nullopt;
  if (!source_type || !runtime_type) {
    report(
        "FSIM-ELAB-SVQUERY-001",
        expression.text
            + " requires a direct typed container object",
        operand.span);
    return std::nullopt;
  }
  const auto constant_result =
      [&](const std::int64_t value) {
        const auto destination =
            allocate_register(
                32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant{
            destination,
            unsigned_value(
                static_cast<std::uint32_t>(value), 32)});
        return ExpressionAttempt{destination};
      };
  if (dimensions_query) {
    return constant_result(2);
  }
  if (unpacked_dimensions_query) {
    return constant_result(1);
  }
  if (runtime_type->associative && bound_query) {
    report(
        "FSIM-ELAB-SVQUERY-003",
        expression.text
            + " has no finite bound for an associative array",
        operand.span);
    return std::nullopt;
  }
  if (runtime_type->fixed) {
    const auto left =
        static_cast<std::int64_t>(
            runtime_type->index_left);
    const auto right =
        static_cast<std::int64_t>(
            runtime_type->index_right);
    const auto count =
        left >= right
            ? left - right + 1
            : right - left + 1;
    if (bits_query) {
      return constant_result(
          count * runtime_type->element_width);
    }
    if (size_query) {
      return constant_result(count);
    }
    if (expression.text == "$left") {
      return constant_result(left);
    }
    if (expression.text == "$right") {
      return constant_result(right);
    }
    if (expression.text == "$low") {
      return constant_result(std::min(left, right));
    }
    if (expression.text == "$high") {
      return constant_result(std::max(left, right));
    }
    return constant_result(left >= right ? 1 : -1);
  }
  if (bound_query
      && (expression.text == "$left"
          || expression.text == "$low")) {
    return constant_result(0);
  }
  if (bound_query
      && expression.text == "$increment") {
    return constant_result(-1);
  }
  const auto source = lower_container_expression(operand);
  if (!source) {
    return std::nullopt;
  }
  const auto size =
      allocate_register(32, frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(
      ContainerSize{size, *source});
  if (size_query) {
    return size;
  }
  const auto factor =
      allocate_register(32, frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(LoadConstant{
      factor,
      unsigned_value(
          bits_query ? runtime_type->element_width : 1,
          32)});
  const auto destination =
      allocate_register(32, frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(Binary{
      bits_query
          ? BinaryOperator::multiply_unsigned
          : BinaryOperator::subtract_signed,
      destination,
      size,
      factor});
  return destination;
}

std::optional<ContainerRegisterId>
Lowerer::lower_container_pattern(
    const Expression& expression,
    const frontend::Type& source_type,
    const ContainerType& runtime_type) {
  if (language_ != frontend::Language::SystemVerilog2017
      || expression.kind != ExpressionKind::Aggregate
      || expression.text != "sv-pattern"
      || expression.aggregate_choices.size()
          != expression.operands.size()
      || expression.aggregate_choice_expressions.size()
          != expression.operands.size()) {
    report(
        "FSIM-ELAB-SVPATTERN-001",
        "a container assignment pattern requires consistent "
        "SystemVerilog aggregate metadata",
        expression.span);
    return std::nullopt;
  }
  bool has_positional = false;
  bool has_keyed = false;
  bool has_default = false;
  for (const auto& choice : expression.aggregate_choices) {
    has_positional |= choice.empty();
    has_keyed |= choice == "@key";
    has_default |= choice == "default";
    if (!choice.empty() && choice != "@key"
        && choice != "default") {
      report(
          "FSIM-ELAB-SVPATTERN-001",
          "unknown container assignment-pattern association",
          expression.span);
      return std::nullopt;
    }
  }
  if (has_default || (has_positional && has_keyed)) {
    report(
        "FSIM-ELAB-SVPATTERN-004",
        has_default
            ? "default container assignment-pattern members are "
              "outside the bounded subset"
            : "container assignment patterns cannot mix positional "
              "and keyed members",
        expression.span);
    return std::nullopt;
  }
  if (runtime_type.associative != has_keyed
      && !expression.operands.empty()) {
    report(
        "FSIM-ELAB-SVPATTERN-001",
        runtime_type.associative
            ? "associative-array assignment patterns require keyed "
              "members"
            : "non-associative container patterns require positional "
              "members",
        expression.span);
    return std::nullopt;
  }
  const auto count = expression.operands.size();
  const auto fixed_count =
      static_cast<std::uint64_t>(
          runtime_type.index_left >= runtime_type.index_right
              ? static_cast<std::int64_t>(runtime_type.index_left)
                    - runtime_type.index_right
              : static_cast<std::int64_t>(runtime_type.index_right)
                    - runtime_type.index_left)
      + 1U;
  if ((runtime_type.fixed
       && count != fixed_count)
      || count > maximum_container_elements
      || (runtime_type.maximum_elements
          && count > *runtime_type.maximum_elements)) {
    report(
        "FSIM-ELAB-SVPATTERN-002",
        runtime_type.fixed
            ? "a static-array assignment pattern must match the "
              "specialized element count"
            : "an assignment pattern exceeds the bounded container "
              "capacity",
        expression.span);
    return std::nullopt;
  }
  const auto destination =
      allocate_container_register(runtime_type);
  if (!runtime_type.fixed && !runtime_type.associative
      && !runtime_type.queue) {
    const auto size =
        allocate_register(32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        size, unsigned_value(count, 32)});
    process_.operations.emplace_back(
        ResizeContainer{destination, size});
  }
  std::set<std::uint64_t> keys;
  for (std::size_t element = 0; element < count; ++element) {
    if (runtime_type.queue) {
      const auto value = lower_expression(
          expression.operands[element],
          runtime_type.element_width,
          &source_type);
      if (!value) {
        return std::nullopt;
      }
      process_.operations.emplace_back(
          PushContainer{destination, *value, false});
      continue;
    }
    RegisterId index{};
    bool signed_index = false;
    if (runtime_type.associative) {
      const auto& choices =
          expression.aggregate_choice_expressions[element];
      if (choices.size() != 1) {
        report(
            "FSIM-ELAB-SVPATTERN-003",
            "an associative assignment-pattern member requires one "
            "locally constant key",
            expression.span);
        return std::nullopt;
      }
      auto key = constant_index(choices.front());
      if (!key) {
        std::string error;
        if (const auto value =
                evaluate_systemverilog_constant_expression(
                    choices.front(), {}, {}, error)) {
          key = value->integer_value();
        }
      }
      if (!key) {
        report(
            "FSIM-ELAB-SVPATTERN-003",
            "associative assignment-pattern keys must be locally "
            "constant known integral values",
            choices.front().span);
        return std::nullopt;
      }
      const auto mask =
          runtime_type.index_width == 64
              ? std::numeric_limits<std::uint64_t>::max()
              : (UINT64_C(1) << runtime_type.index_width) - 1U;
      if (!keys.insert(
              static_cast<std::uint64_t>(*key) & mask).second) {
        report(
            "FSIM-ELAB-SVPATTERN-003",
            "associative assignment-pattern keys must be unique "
            "after index-type conversion",
            choices.front().span);
        return std::nullopt;
      }
      const auto* index_type =
          source_type.systemverilog_container
              ->associative_index_type.get();
      const auto lowered = lower_expression(
          choices.front(), runtime_type.index_width, index_type);
      if (!lowered) {
        return std::nullopt;
      }
      index = register_width(*lowered)
                      == runtime_type.index_width
                  ? *lowered
                  : resize_register(
                        *lowered,
                        runtime_type.index_width,
                        runtime_type.signed_indices);
      signed_index = runtime_type.signed_indices;
    } else {
      std::int64_t declared_index =
          static_cast<std::int64_t>(element);
      if (runtime_type.fixed) {
        const auto step =
            runtime_type.index_left >= runtime_type.index_right
                ? -static_cast<std::int64_t>(element)
                : static_cast<std::int64_t>(element);
        declared_index =
            static_cast<std::int64_t>(runtime_type.index_left)
            + step;
        signed_index = true;
      }
      index =
          allocate_register(32, frontend::ValueDomain::Bit2);
      process_.operations.emplace_back(LoadConstant{
          index,
          unsigned_value(
              static_cast<std::uint32_t>(declared_index), 32)});
    }
    const auto value = lower_expression(
        expression.operands[element],
        runtime_type.element_width,
        &source_type);
    if (!value) {
      return std::nullopt;
    }
    process_.operations.emplace_back(ContainerWrite{
        destination, index, *value, signed_index});
  }
  return destination;
}

bool Lowerer::lower_container_locator(
    const Expression& expression,
    const ContainerRegisterId destination,
    const ContainerType& destination_type) {
  if (language_ != frontend::Language::SystemVerilog2017
      || expression.operands.empty()
      || expression.operands.front().kind
          != ExpressionKind::Identifier
      || !is_container_expression(expression.operands.front())) {
    report(
        "FSIM-ELAB-SVLOCATOR-001",
        "container locators require a direct supported "
        "SystemVerilog unpacked-container receiver",
        expression.span);
    return false;
  }
  if (expression.operands.size() != 1) {
    report(
        "FSIM-ELAB-SVLOCATOR-002",
        "bounded container locator methods take no arguments",
        expression.span);
    return false;
  }
  const auto& receiver = expression.operands.front();
  const auto source = lower_container_expression(receiver);
  const auto* source_frontend_type = object_type(receiver.text);
  const auto source_type =
      source_frontend_type
          ? container_type(*source_frontend_type, receiver.span)
          : std::nullopt;
  if (!source || !source_type || source_type->associative) {
    report(
        "FSIM-ELAB-SVLOCATOR-001",
        "bounded container locators do not support associative "
        "or unresolved receivers",
        expression.span);
    return false;
  }
  const bool index_result =
      expression.text == ".unique_index";
  const bool compatible =
      destination_type.queue
      && !destination_type.associative
      && !destination_type.fixed
      && (index_result
              ? destination_type.element_width == 32
                    && destination_type.two_state
                    && destination_type.signed_elements
              : destination_type.element_width
                        == source_type->element_width
                    && destination_type.two_state
                        == source_type->two_state
                    && destination_type.signed_elements
                        == source_type->signed_elements);
  if (!compatible) {
    report(
        "FSIM-ELAB-SVLOCATOR-003",
        "container locator result requires a compatible queue target",
        expression.span);
    return false;
  }
  auto operation = ContainerLocatorOperator::minimum;
  if (expression.text == ".max") {
    operation = ContainerLocatorOperator::maximum;
  } else if (expression.text == ".unique") {
    operation = ContainerLocatorOperator::unique;
  } else if (index_result) {
    operation = ContainerLocatorOperator::unique_index;
  }
  process_.operations.emplace_back(
      LocateContainer{operation, destination, *source});
  return true;
}

void Lowerer::lower_container_method(
    const Statement& statement) {
  const auto& call = statement.value;
  const bool ordering_method =
      call.kind == ExpressionKind::Call
      && (call.text == ".reverse"
          || call.text == ".sort"
          || call.text == ".rsort");
  const bool unsupported_shuffle =
      call.kind == ExpressionKind::Call
      && call.text == ".shuffle";
  const bool locator_method =
      call.kind == ExpressionKind::Call
      && (call.text == ".min"
          || call.text == ".max"
          || call.text == ".unique"
          || call.text == ".unique_index");
  if (locator_method) {
    report(
        "FSIM-ELAB-SVLOCATOR-005",
        "container locator results cannot be discarded",
        call.span);
    return;
  }
  if (ordering_method || unsupported_shuffle) {
    if (language_
            != frontend::Language::SystemVerilog2017
        || call.operands.empty()
        || call.operands.front().kind
            != ExpressionKind::Identifier
        || !is_container_expression(
            call.operands.front())) {
      report(
          "FSIM-ELAB-SVORDER-001",
          "container ordering requires a direct writable "
          "SystemVerilog unpacked-container receiver",
          call.span);
      return;
    }
    if (call.operands.size() != 1) {
      report(
          "FSIM-ELAB-SVORDER-002",
          "container ordering methods take no arguments",
          call.span);
      return;
    }
    if (unsupported_shuffle) {
      report(
          "FSIM-ELAB-SVORDER-005",
          "shuffle() is outside the deterministic "
          "container-ordering subset",
          call.span);
      return;
    }
  }
  if (call.kind == ExpressionKind::Call
      && (call.text == ".sum"
          || call.text == ".product"
          || call.text == ".and"
          || call.text == ".or"
          || call.text == ".xor")) {
    report(
        "FSIM-ELAB-SVREDUCE-003",
        "a container reduction result must be used in an expression",
        call.span);
    return;
  }
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
      || call.text == ".pop_back"
      || ordering_method
      || unsupported_shuffle;
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
  } else if (ordering_method) {
    if (runtime_type->associative) {
      report(
          "FSIM-ELAB-SVORDER-003",
          "container ordering does not support associative arrays",
          call.span);
      return;
    }
    auto operation =
        ContainerOrderingOperator::reverse;
    if (call.text == ".sort") {
      operation =
          ContainerOrderingOperator::ascending;
    } else if (call.text == ".rsort") {
      operation =
          ContainerOrderingOperator::descending;
    }
    process_.operations.emplace_back(
        OrderContainer{operation, *target});
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
