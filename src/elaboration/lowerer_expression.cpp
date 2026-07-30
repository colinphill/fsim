// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt::ExpressionAttempt() = default;

Lowerer::ExpressionAttempt::ExpressionAttempt(
        const RegisterId result)
    : handled(true), value(result) {}

Lowerer::ExpressionAttempt::ExpressionAttempt(
        std::optional<RegisterId> result)
    : handled(true), value(std::move(result)) {}

Lowerer::ExpressionAttempt::ExpressionAttempt(
        const std::nullopt_t)
    : handled(true) {}

std::optional<RegisterId> Lowerer::lower_expression(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type* expected_type) {
    auto attempt = lower_primary_expression(
        expression, expected_width, expected_type);
    if (attempt.handled) {
        return attempt.value;
    }
    attempt = lower_unary_attribute_expression(
        expression, expected_width, expected_type);
    if (attempt.handled) {
        return attempt.value;
    }
    attempt = lower_system_function_expression(
        expression, expected_width, expected_type);
    if (attempt.handled) {
        return attempt.value;
    }
    return lower_binary_expression(
        expression, expected_width, expected_type).value;
}

Lowerer::ExpressionAttempt Lowerer::lower_primary_expression(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type* expected_type) {

        const bool container_reduction =
            expression.kind == ExpressionKind::Call
            && (expression.text == ".sum"
                || expression.text == ".product"
                || expression.text == ".and"
                || expression.text == ".or"
                || expression.text == ".xor");
        const bool container_ordering =
            expression.kind == ExpressionKind::Call
            && (expression.text == ".reverse"
                || expression.text == ".sort"
                || expression.text == ".rsort");
        if (container_ordering
            || (expression.kind == ExpressionKind::Call
                && expression.text == ".shuffle")) {
            report(
                expression.text == ".shuffle"
                    ? "FSIM-ELAB-SVORDER-005"
                    : "FSIM-ELAB-SVORDER-004",
                expression.text == ".shuffle"
                    ? "shuffle() is outside the deterministic "
                      "container-ordering subset"
                    : "container ordering methods do not produce an "
                      "expression result",
                expression.span);
            return std::nullopt;
        }
        if (container_reduction
            && (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1
                || !is_container_expression(
                    expression.operands.front()))) {
            report(
                expression.operands.size() != 1
                    ? "FSIM-ELAB-SVREDUCE-002"
                    : "FSIM-ELAB-SVREDUCE-001",
                expression.operands.size() != 1
                    ? "container reduction methods take no arguments"
                    : "container reduction methods require a direct "
                      "SystemVerilog unpacked-container receiver",
                expression.span);
            return std::nullopt;
        }

        if ((expression.kind == ExpressionKind::Call
             && (expression.text == ".size"
                 || expression.text == ".exists"
                 || expression.text == ".first"
                 || expression.text == ".last"
                 || expression.text == ".next"
                 || expression.text == ".prev"
                 || expression.text == ".pop_front"
                 || expression.text == ".pop_back"
                 || container_reduction)
             && !expression.operands.empty()
             && is_container_expression(
                 expression.operands.front()))
            || (expression.kind == ExpressionKind::Index
                && expression.operands.size() == 2
                && is_container_expression(
                    expression.operands.front()))) {
            const auto source_expression =
                expression.operands.front();
            if (expression.kind == ExpressionKind::Call
                && (expression.text == ".pop_front"
                    || expression.text == ".pop_back")
                && source_expression.kind
                    == ExpressionKind::Identifier
                && read_only_container_objects_.contains(
                    source_expression.text)) {
                report(
                    "FSIM-ELAB-SVPORT-009",
                    "an input container port is read-only within its "
                    "module",
                    source_expression.span);
                return std::nullopt;
            }
            const auto source =
                lower_container_expression(source_expression);
            if (!source) {
                return std::nullopt;
            }
            if (expression.kind == ExpressionKind::Call
                && expression.text == ".size") {
                const auto destination =
                    allocate_register(
                        32, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(
                    ContainerSize{destination, *source});
                return destination;
            }
            const auto* type =
                source_expression.kind
                        == ExpressionKind::Identifier
                    ? object_type(source_expression.text)
                    : nullptr;
            if (type == nullptr) {
                report(
                    "FSIM-ELAB-SVCONTAINER-002",
                    "container element type cannot be resolved",
                    expression.span);
                return std::nullopt;
            }
            const auto width = type->width();
            if (!width) {
                return std::nullopt;
            }
            const auto runtime_type =
                container_type(*type, expression.span);
            if (!runtime_type) {
                return std::nullopt;
            }
            if (container_reduction) {
                auto operation =
                    ContainerReductionOperator::sum;
                if (expression.text == ".product") {
                    operation =
                        ContainerReductionOperator::product;
                } else if (expression.text == ".and") {
                    operation =
                        ContainerReductionOperator::bit_and;
                } else if (expression.text == ".or") {
                    operation =
                        ContainerReductionOperator::bit_or;
                } else if (expression.text == ".xor") {
                    operation =
                        ContainerReductionOperator::bit_xor;
                }
                const auto destination =
                    allocate_register(*width, type->domain);
                process_.operations.emplace_back(
                    ContainerReduction{
                        operation, destination, *source});
                return destination;
            }
            if (expression.kind == ExpressionKind::Call
                && (expression.text == ".exists"
                    || expression.text == ".first"
                    || expression.text == ".last"
                    || expression.text == ".next"
                    || expression.text == ".prev")) {
                if (!runtime_type->associative) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-015",
                        expression.text
                            + " requires an associative-array receiver",
                        expression.span);
                    return std::nullopt;
                }
                if (expression.operands.size() != 2) {
                    return std::nullopt;
                }
                const auto* index_type =
                    type->systemverilog_container
                        ->associative_index_type.get();
                auto index = lower_expression(
                    expression.operands[1],
                    runtime_type->index_width,
                    index_type);
                if (!index) {
                    return std::nullopt;
                }
                if (expression.text == ".exists"
                    && register_width(*index)
                        != runtime_type->index_width) {
                    *index = resize_register(
                        *index,
                        runtime_type->index_width,
                        index_type->is_signed);
                }
                const auto destination =
                    allocate_register(
                        32, frontend::ValueDomain::Bit2);
                if (expression.text == ".exists") {
                    process_.operations.emplace_back(
                        ContainerExists{
                            destination, *source, *index});
                    return destination;
                }
                if (expression.operands[1].kind
                        != ExpressionKind::Identifier
                    || (!locals_.contains(
                            expression.operands[1].text)
                        && !signals_.contains(
                            expression.operands[1].text))) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-016",
                        expression.text
                            + " requires a direct mutable integral "
                              "variable argument",
                        expression.operands[1].span);
                    return std::nullopt;
                }
                if (register_width(*index)
                    != runtime_type->index_width) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-017",
                        expression.text
                            + " argument must match the associative-array "
                              "index width",
                        expression.operands[1].span);
                    return std::nullopt;
                }
                auto traversal = ContainerTraversal::first;
                if (expression.text == ".last") {
                    traversal = ContainerTraversal::last;
                } else if (expression.text == ".next") {
                    traversal = ContainerTraversal::next;
                } else if (expression.text == ".prev") {
                    traversal = ContainerTraversal::previous;
                }
                process_.operations.emplace_back(
                    TraverseContainer{
                        destination, *source, *index, traversal});
                if (const auto signal = signals_.find(
                        expression.operands[1].text);
                    signal != signals_.end()
                    && !locals_.contains(
                        expression.operands[1].text)) {
                    process_.operations.emplace_back(
                        WriteBlocking{signal->second, *index});
                }
                return destination;
            }
            const auto destination =
                allocate_register(*width, type->domain);
            if (expression.kind == ExpressionKind::Call) {
                process_.operations.emplace_back(
                    PopContainer{
                        destination,
                        *source,
                        expression.text == ".pop_front"});
                if (const auto object =
                        container_objects_.find(
                            source_expression.text);
                    object != container_objects_.end()) {
                    process_.operations.emplace_back(
                        WriteContainerObject{
                            object->second, *source});
                }
                return destination;
            }
            const auto index_width =
                runtime_type->associative
                    ? static_cast<std::size_t>(
                          runtime_type->index_width)
                    : runtime_type->fixed
                          ? std::size_t{32}
                          : infer_width(expression.operands[1])
                                .value_or(std::size_t{32});
            auto index = lower_expression(
                expression.operands[1],
                index_width,
                runtime_type->associative
                    ? type->systemverilog_container
                          ->associative_index_type.get()
                    : nullptr);
            if (!index) {
                return std::nullopt;
            }
            if (runtime_type->associative
                && register_width(*index)
                    != runtime_type->index_width) {
                *index = resize_register(
                    *index,
                    runtime_type->index_width,
                    runtime_type->signed_indices);
            }
            process_.operations.emplace_back(
                ContainerRead{
                    destination,
                    *source,
                    *index,
                    runtime_type->associative
                        ? runtime_type->signed_indices
                        : runtime_type->fixed
                              || is_signed_expression(
                                  expression.operands[1])});
            return destination;
        }

        if (expression.kind == ExpressionKind::Call
            && expression.text.starts_with('.')
            && !expression.operands.empty()
            && is_container_expression(
                expression.operands.front())) {
            report(
                "FSIM-ELAB-SVCONTAINER-008",
                "unsupported container method '"
                    + expression.text + "'",
                expression.span);
            return std::nullopt;
        }

        if (expression.kind == ExpressionKind::Call
            && expression.text == ".len"
            && expression.operands.size() == 1
            && is_string_expression(
                expression.operands.front())) {
            const auto source =
                lower_string_expression(
                    expression.operands.front());
            if (!source) {
                return std::nullopt;
            }
            const auto destination =
                allocate_register(
                    32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                StringLength{destination, *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2
            && is_string_expression(expression.operands.front())) {
            const auto source =
                lower_string_expression(
                    expression.operands.front());
            const auto index_width =
                infer_width(expression.operands[1])
                    .value_or(std::size_t{32});
            const auto index =
                lower_expression(
                    expression.operands[1], index_width);
            if (!source || !index) {
                return std::nullopt;
            }
            const auto destination =
                allocate_register(
                    8, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                StringIndex{
                    destination,
                    *source,
                    *index,
                    is_signed_expression(
                        expression.operands[1])});
            return destination;
        }

        const auto enumeration_context_compatible =
            [&](const frontend::Type* source_type) {
              if (language_
                      != frontend::Language::Vhdl2008
                  || expected_type == nullptr
                  || source_type == nullptr) {
                  return true;
              }
              const bool expected_array =
                  expected_type->vhdl_array.has_value();
              const bool source_array =
                  source_type->vhdl_array.has_value();
              if (expected_array || source_array) {
                  if (expected_array && source_array
                      && expected_type->nominal_type
                          == source_type->nominal_type) {
                      return true;
                  }
                  report(
                      "FSIM-ELAB-VHARRAY-006",
                      "VHDL array values require the same nominal type; "
                      "expected '" + expected_type->spelling
                          + "' but found '" + source_type->spelling + "'",
                      expression.span);
                  return false;
              }
              const bool expected_enumeration =
                  !expected_type->enumeration_literals.empty();
              const bool source_enumeration =
                  !source_type->enumeration_literals.empty();
              if (!expected_enumeration && !source_enumeration) {
                  return true;
              }
              if (expected_enumeration
                  && source_enumeration
                  && expected_type->nominal_type
                      == source_type->nominal_type) {
                  return true;
              }
              report(
                  "FSIM-ELAB-VHENUM-002",
                  "VHDL enumeration values require the same nominal "
                  "type; expected '" + expected_type->spelling
                      + "' but found '" + source_type->spelling + "'",
                  expression.span);
              return false;
            };
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Call
            && !expression.operands.empty()
            && expression.operands.front().kind
                == ExpressionKind::Identifier) {
            const auto& prefix =
                expression.operands.front().text;
            if (const auto* type =
                    visible_type_mark(prefix);
                type != nullptr
                && !type->enumeration_literals.empty()) {
                const bool enumeration_result =
                    expression.text == "'left"
                    || expression.text == "'right"
                    || expression.text == "'low"
                    || expression.text == "'high"
                    || expression.text == "'val"
                    || expression.text == "'succ"
                    || expression.text == "'pred"
                    || expression.text == "'leftof"
                    || expression.text == "'rightof";
                if (enumeration_result
                    && !enumeration_context_compatible(type)) {
                    return std::nullopt;
                }
                return lower_enumeration_attribute(
                    expression, *type);
            }
            const bool enumeration_attribute =
                expression.text == "'pos"
                || expression.text == "'val"
                || expression.text == "'succ"
                || expression.text == "'pred"
                || expression.text == "'leftof"
                || expression.text == "'rightof";
            const auto* prefix_object = object_type(prefix);
            if (enumeration_attribute
                || (prefix_object != nullptr
                    && !prefix_object
                            ->enumeration_literals.empty())) {
                report(
                    "FSIM-ELAB-VHENUMATTR-001",
                    "enumeration attribute '" + expression.text
                        + "' requires a visible enumeration type mark",
                    expression.operands.front().span);
                return std::nullopt;
            }
        }
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local = locals_.find(expression.text);
                local != locals_.end()) {
                if (!enumeration_context_compatible(
                        object_type(expression.text))) {
                    return std::nullopt;
                }
                return local->second;
            }
            const auto found = signals_.find(expression.text);
            if (found != signals_.end()) {
                const auto& signal =
                    design_.signal_info_[found->second];
                const auto* type =
                    visible_type(expression.text);
                if (!enumeration_context_compatible(type)) {
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    signal.width,
                    type != nullptr
                        ? type->domain
                        : signal.source_domain);
                process_.operations.emplace_back(
                    ReadSignal{destination, found->second});
                return destination;
            }
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                const auto base_width =
                    infer_width(Expression{
                        ExpressionKind::Identifier,
                        selected->base,
                        {},
                        expression.span});
                const auto member_width =
                    selected->member->width();
                if (!base_width || !member_width
                    || *member_width == 0
                    || selected->member->lsb_offset
                        > std::numeric_limits<std::uint32_t>::max()
                    || *member_width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-SVSTRUCT-002",
                        "packed aggregate member '" + expression.text
                            + "' has no executable layout",
                        expression.span);
                    return std::nullopt;
                }
                const auto source = lower_expression(
                    Expression{
                        ExpressionKind::Identifier,
                        selected->base,
                        {},
                        expression.span},
                    *base_width);
                if (!source) {
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    *member_width,
                    selected->member->domain);
                process_.operations.emplace_back(Extract{
                    destination,
                    *source,
                    static_cast<std::uint32_t>(
                        selected->member->lsb_offset),
                    static_cast<std::uint32_t>(*member_width)});
                return destination;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expected_type != nullptr
                && !expected_type->enumeration_literals.empty()) {
                const auto ordinal =
                    vhdl_enumeration_ordinal(
                        expression, *expected_type);
                if (!ordinal) {
                    report(
                        "FSIM-ELAB-VHENUM-001",
                        "enumeration type '" + expected_type->spelling
                            + "' has no literal '" + expression.text + "'",
                        expression.span);
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    expected_width, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant{
                    destination,
                    unsigned_value(
                        static_cast<std::uint64_t>(*ordinal),
                        expected_width)});
                return destination;
            }
            {
                report(
                    "FSIM-ELAB-040",
                    "unknown identifier '" + expression.text + "'",
                    expression.span);
                return std::nullopt;
            }
        }
        if (expression.kind == ExpressionKind::Aggregate) {
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.text == "sv-pattern") {
                report(
                    "FSIM-ELAB-SVPATTERN-001",
                    "a SystemVerilog assignment pattern requires a "
                    "supported contextual whole-container target",
                    expression.span);
                return std::nullopt;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expected_type != nullptr
                && expected_type->vhdl_array) {
                return lower_vhdl_array_aggregate(
                    expression, expected_width, *expected_type);
            }
            if (language_ != frontend::Language::Vhdl2008
                || expected_type == nullptr
                || expected_type->packed_members.empty()) {
                report(
                    "FSIM-ELAB-VHAGG-001",
                    "a VHDL record aggregate requires a contextual "
                    "record target type",
                    expression.span);
                return std::nullopt;
            }
            const auto aggregate_width = expected_type->width();
            if (!aggregate_width
                || *aggregate_width != expected_width
                || *aggregate_width == 0
                || *aggregate_width
                    > std::numeric_limits<std::size_t>::max()
                || expression.aggregate_choices.size()
                    != expression.operands.size()
                || expression.aggregate_choice_expressions.size()
                    != expression.operands.size()) {
                report(
                    "FSIM-ELAB-VHAGG-002",
                    "record aggregate association metadata or contextual "
                    "layout is inconsistent",
                    expression.span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                expected_width, expected_type->domain);
            process_.operations.emplace_back(LoadConstant{
                destination,
                default_packed_value(
                    *expected_type, expected_width)});
            std::vector<bool> assigned(
                expected_type->packed_members.size(), false);
            std::optional<std::size_t> others_index;
            std::size_t positional_index = 0;
            bool valid = true;
            const auto insert_member =
                [&](const std::size_t member_index,
                    const Expression& value,
                    const frontend::SourceSpan& span) {
                  if (member_index
                          >= expected_type->packed_members.size()
                      || assigned[member_index]) {
                    report(
                        "FSIM-ELAB-VHAGG-004",
                        member_index
                                < expected_type->packed_members.size()
                            ? "record aggregate element '"
                                  + expected_type
                                        ->packed_members[member_index]
                                        .name
                                  + "' is assigned more than once"
                            : "record aggregate has too many positional "
                              "associations",
                        span);
                    valid = false;
                    return;
                  }
                  const auto& member =
                      expected_type->packed_members[member_index];
                  const auto member_width = member.width();
                  if (!member_width || *member_width == 0
                      || *member_width
                          > std::numeric_limits<std::size_t>::max()
                      || member.lsb_offset
                          > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-VHAGG-002",
                        "record aggregate element '" + member.name
                            + "' has no executable flattened layout",
                        span);
                    valid = false;
                    return;
                  }
                  const auto lowered = lower_expression(
                      value,
                      static_cast<std::size_t>(*member_width));
                  if (!lowered) {
                    valid = false;
                    return;
                  }
                  if (register_width(*lowered) != *member_width) {
                    report(
                        "FSIM-ELAB-VHAGG-006",
                        "record aggregate element '" + member.name
                            + "' expects "
                            + std::to_string(*member_width)
                            + " bits but its value has "
                            + std::to_string(
                                register_width(*lowered))
                            + " bits",
                        span);
                    valid = false;
                    return;
                  }
                  if (is_two_state_domain(member.domain)
                      && !is_two_state_domain(
                          register_domain(*lowered))) {
                    report(
                        "FSIM-ELAB-VHAGG-007",
                        "two-state record aggregate element '"
                            + member.name
                            + "' requires an explicit conversion",
                        span);
                    valid = false;
                    return;
                  }
                  process_.operations.emplace_back(Insert{
                      destination,
                      destination,
                      *lowered,
                      static_cast<std::uint32_t>(
                          member.lsb_offset)});
                  assigned[member_index] = true;
                };
            for (std::size_t index = 0;
                 index < expression.operands.size();
                 ++index) {
                const auto& choice =
                    expression.aggregate_choices[index];
                const auto& choice_expressions =
                    expression.aggregate_choice_expressions[index];
                if (choice.empty()) {
                    if (!choice_expressions.empty()) {
                        report(
                            "FSIM-ELAB-VHAGG-002",
                            "positional record aggregate association has "
                            "unexpected choice metadata",
                            expression.operands[index].span);
                        valid = false;
                    }
                    insert_member(
                        positional_index++,
                        expression.operands[index],
                        expression.operands[index].span);
                    continue;
                }
                if (choice == "others") {
                    if (choice_expressions.size() != 1
                        || choice_expressions.front().kind
                            != ExpressionKind::Identifier
                        || choice_expressions.front().text
                            != "others") {
                        report(
                            "FSIM-ELAB-VHAGG-002",
                            "record aggregate others association has "
                            "inconsistent choice metadata",
                            expression.operands[index].span);
                        valid = false;
                    }
                    if (others_index) {
                        report(
                            "FSIM-ELAB-VHAGG-004",
                            "record aggregate has more than one others "
                            "association",
                            expression.operands[index].span);
                        valid = false;
                    } else {
                        others_index = index;
                    }
                    continue;
                }
                if (choice_expressions.size() != 1
                    || choice_expressions.front().kind
                        != ExpressionKind::Identifier
                    || choice_expressions.front().text != choice) {
                    report(
                        "FSIM-ELAB-VHAGG-008",
                        "record aggregates do not accept discrete, range, "
                        "or choice-list associations",
                        expression.operands[index].span);
                    valid = false;
                    continue;
                }
                const auto member = std::find_if(
                    expected_type->packed_members.begin(),
                    expected_type->packed_members.end(),
                    [&](const frontend::PackedMember& candidate) {
                        return candidate.name == choice;
                    });
                if (member
                    == expected_type->packed_members.end()) {
                    report(
                        "FSIM-ELAB-VHAGG-003",
                        "record aggregate type '"
                            + expected_type->spelling
                            + "' has no element '" + choice + "'",
                        expression.operands[index].span);
                    valid = false;
                    continue;
                }
                insert_member(
                    static_cast<std::size_t>(std::distance(
                        expected_type->packed_members.begin(),
                        member)),
                    expression.operands[index],
                    expression.operands[index].span);
            }
            if (others_index) {
                for (std::size_t member = 0;
                     member < assigned.size();
                     ++member) {
                    if (!assigned[member]) {
                        insert_member(
                            member,
                            expression.operands[*others_index],
                            expression.operands[*others_index].span);
                    }
                }
            }
            for (std::size_t member = 0;
                 member < assigned.size();
                 ++member) {
                if (assigned[member]) {
                    continue;
                }
                report(
                    "FSIM-ELAB-VHAGG-005",
                    "record aggregate is missing element '"
                        + expected_type->packed_members[member].name
                        + "'",
                    expression.span);
                valid = false;
            }
            return valid
                ? std::optional<RegisterId>{destination}
                : std::nullopt;
        }
        if (expression.kind == ExpressionKind::IntegerLiteral
            || expression.kind == ExpressionKind::BooleanLiteral
            || expression.kind == ExpressionKind::LogicLiteral
            || expression.kind == ExpressionKind::StringLiteral) {
            if (language_ == frontend::Language::Vhdl2008
                && expected_type != nullptr
                && !expected_type->enumeration_literals.empty()) {
                const auto ordinal =
                    vhdl_enumeration_ordinal(
                        expression, *expected_type);
                if (!ordinal) {
                    report(
                        "FSIM-ELAB-VHENUM-001",
                        "enumeration type '" + expected_type->spelling
                            + "' has no literal '" + expression.text + "'",
                        expression.span);
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    expected_width, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant{
                    destination,
                    unsigned_value(
                        static_cast<std::uint64_t>(*ordinal),
                        expected_width)});
                return destination;
            }
            const auto literal =
                literal_value(expression, expected_width, language_);
            if (!literal) {
                report(
                    "FSIM-ELAB-041",
                    "unsupported or malformed literal '" + expression.text + "'",
                    expression.span);
                return std::nullopt;
            }
            const auto destination =
                allocate_register(literal->value.width(), literal->domain);
            process_.operations.emplace_back(
                LoadConstant{destination, std::move(literal->value)});
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Call
            && expression.operands.size() == 1
            && (locals_.contains(expression.text)
                || signals_.contains(expression.text)
                || packed_member_reference(expression.text))) {
            return lower_expression(
                Expression{
                    ExpressionKind::Index,
                    "index",
                    {
                        Expression{
                            ExpressionKind::Identifier,
                            expression.text,
                            {},
                            expression.span},
                        expression.operands[0]},
                    expression.span},
                expected_width);
        }
        if (expression.kind == ExpressionKind::Call) {
            emit_debug_point(
                DebugPointKind::call, expression.span);
            auto function = lower_user_function_expression(
                expression, expected_width, expected_type);
            if (function.handled) {
                return function;
            }
        }
        if (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2) {
            const auto source_width =
                infer_width(expression.operands[0]);
            const auto index =
                static_integer_value(
                    expression.operands[1]);
            if (!source_width) {
                report(
                    "FSIM-ELAB-068",
                    "a bit-select requires an inferable packed source",
                    expression.span);
                return std::nullopt;
            }
            const auto source =
                lower_expression(
                    expression.operands[0], *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination =
                allocate_register(1, register_domain(*source));
            if (index) {
                const auto offset = select_offset(
                    expression.operands[0],
                    *index,
                    *source_width);
                if (!offset
                    || *offset
                        > std::numeric_limits<
                            std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-068",
                        "bit-select index "
                            + std::to_string(*index)
                            + " is outside the source's declared packed "
                              "range",
                        expression.span);
                    return std::nullopt;
                }
                process_.operations.emplace_back(Extract{
                    destination,
                    *source,
                    static_cast<std::uint32_t>(*offset),
                    1});
            } else {
                const auto selection = lower_dynamic_index(
                    expression.operands[0],
                    expression.operands[1],
                    *source_width,
                    0,
                    expression.span);
                if (!selection) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(
                    DynamicExtract{
                        destination,
                        *source,
                        *selection});
            }
            return destination;
        }
        if (expression.kind == ExpressionKind::Slice
            && expression.operands.size() == 3) {
            if (language_ == frontend::Language::Vhdl2008
                && expected_type != nullptr
                && expected_type->vhdl_array
                && expression.operands[0].kind
                    == ExpressionKind::Identifier
                && !enumeration_context_compatible(
                    object_type(expression.operands[0].text))) {
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands[0]);
            const auto selection =
                source_width
                    ? constant_slice_selection(
                          expression, *source_width)
                    : std::nullopt;
            if (!source_width || !selection
                || selection->offset
                    > std::numeric_limits<std::uint32_t>::max()
                || selection->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "a part-select requires an inferable packed source, "
                    "constant in-range bounds, a positive indexed width, "
                    "and compatible direction",
                    expression.span);
                return std::nullopt;
            }
            const auto source =
                lower_expression(
                    expression.operands[0], *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                selection->width,
                register_domain(*source));
            process_.operations.emplace_back(Extract{
                destination,
                *source,
                static_cast<std::uint32_t>(selection->offset),
                static_cast<std::uint32_t>(selection->width)});
            return destination;
        }
        if (language_ != frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Replication) {
            std::string count_error;
            const auto count =
                expression.operands.empty()
                    ? std::nullopt
                    : evaluate_constant_expression(
                          expression.operands[0],
                          {},
                          count_error);
            if (!count || *count <= 0
                || expression.operands.size() < 2) {
                report(
                    "FSIM-ELAB-SVREPL-001",
                    "a replication concatenation requires a positive "
                    "constant count and at least one packed operand",
                    expression.span);
                return std::nullopt;
            }
            std::vector<RegisterId> group_operands;
            group_operands.reserve(expression.operands.size() - 1);
            std::size_t group_width = 0;
            auto result_domain = frontend::ValueDomain::Bit2;
            for (std::size_t index = 1;
                 index < expression.operands.size();
                 ++index) {
                const auto& operand_expression =
                    expression.operands[index];
                const auto operand_width =
                    infer_width(operand_expression);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::uint32_t>::max()
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - group_width) {
                    report(
                        "FSIM-ELAB-SVREPL-001",
                        "a replication operand width is not statically "
                        "inferable or the group width overflows",
                        operand_expression.span);
                    return std::nullopt;
                }
                const auto operand = lower_expression(
                    operand_expression, *operand_width);
                if (!operand) {
                    return std::nullopt;
                }
                group_operands.push_back(*operand);
                group_width += register_width(*operand);
                const auto domain = register_domain(*operand);
                if (domain == frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic9;
                } else if (
                    domain != frontend::ValueDomain::Bit2
                    && domain != frontend::ValueDomain::Boolean
                    && result_domain
                        != frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic4;
                }
            }
            const auto repetition_count =
                static_cast<std::uint64_t>(*count);
            if (group_width == 0
                || repetition_count
                    > std::numeric_limits<std::uint32_t>::max()
                          / group_width) {
                report(
                    "FSIM-ELAB-SVREPL-001",
                    "replication result width is outside the supported "
                    "range",
                    expression.span);
                return std::nullopt;
            }
            RegisterId group = group_operands.front();
            if (group_operands.size() > 1) {
                group = allocate_register(
                    group_width, result_domain);
                process_.operations.emplace_back(Concatenate{
                    group,
                    std::move(group_operands),
                    static_cast<std::uint32_t>(group_width)});
            }

            std::optional<RegisterId> result;
            std::size_t result_width = 0;
            auto block = group;
            auto block_width = group_width;
            auto remaining = repetition_count;
            while (remaining != 0) {
                if ((remaining & 1U) != 0) {
                    if (!result) {
                        result = block;
                        result_width = block_width;
                    } else {
                        const auto combined_width =
                            result_width + block_width;
                        const auto combined = allocate_register(
                            combined_width, result_domain);
                        process_.operations.emplace_back(
                            Concatenate{
                                combined,
                                {*result, block},
                                static_cast<std::uint32_t>(
                                    combined_width)});
                        result = combined;
                        result_width = combined_width;
                    }
                }
                remaining >>= 1U;
                if (remaining != 0) {
                    const auto doubled_width =
                        block_width * 2U;
                    const auto doubled = allocate_register(
                        doubled_width, result_domain);
                    process_.operations.emplace_back(
                        Concatenate{
                            doubled,
                            {block, block},
                            static_cast<std::uint32_t>(
                                doubled_width)});
                    block = doubled;
                    block_width = doubled_width;
                }
            }
            return result;
        }
        if (language_ != frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Concatenation) {
            if (expression.operands.empty()) {
                report(
                    "FSIM-ELAB-069",
                    "a concatenation requires at least one packed operand",
                    expression.span);
                return std::nullopt;
            }
            std::vector<RegisterId> operands;
            operands.reserve(expression.operands.size());
            std::size_t width = 0;
            auto result_domain = frontend::ValueDomain::Bit2;
            for (const auto& operand_expression : expression.operands) {
                const auto operand_width =
                    infer_width(operand_expression);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::uint32_t>::max()
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - width) {
                    report(
                        "FSIM-ELAB-069",
                        "concatenation operand width is not statically "
                        "inferable or the total width overflows",
                        operand_expression.span);
                    return std::nullopt;
                }
                const auto operand =
                    lower_expression(
                        operand_expression, *operand_width);
                if (!operand) {
                    return std::nullopt;
                }
                operands.push_back(*operand);
                width += register_width(*operand);
                const auto domain = register_domain(*operand);
                if (domain == frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic9;
                } else if (
                    domain != frontend::ValueDomain::Bit2
                    && domain != frontend::ValueDomain::Boolean
                    && result_domain
                        != frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic4;
                }
            }
            if (width == 0
                || width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-069",
                    "concatenation result width is outside the supported "
                    "range",
                    expression.span);
                return std::nullopt;
            }
            const auto destination =
                allocate_register(width, result_domain);
            process_.operations.emplace_back(Concatenate{
                destination,
                std::move(operands),
                static_cast<std::uint32_t>(width)});
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Binary
            && expression.text == "&"
            && expression.operands.size() == 2) {
            std::vector<RegisterId> operands;
            operands.reserve(2);
            std::size_t width = 0;
            auto result_domain = frontend::ValueDomain::Bit2;
            for (const auto& operand_expression : expression.operands) {
                const auto operand_width =
                    infer_width(operand_expression);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::uint32_t>::max()
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - width) {
                    report(
                        "FSIM-ELAB-069",
                        "VHDL concatenation operand width is not "
                        "statically inferable or the total width "
                        "overflows",
                        operand_expression.span);
                    return std::nullopt;
                }
                const auto operand = lower_expression(
                    operand_expression, *operand_width);
                if (!operand) {
                    return std::nullopt;
                }
                operands.push_back(*operand);
                width += register_width(*operand);
                const auto domain = register_domain(*operand);
                if (domain == frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic9;
                } else if (
                    domain != frontend::ValueDomain::Bit2
                    && domain != frontend::ValueDomain::Boolean
                    && result_domain
                        != frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic4;
                }
            }
            if (width == 0
                || width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-069",
                    "VHDL concatenation result width is outside the "
                    "supported range",
                    expression.span);
                return std::nullopt;
            }
            const auto destination =
                allocate_register(width, result_domain);
            process_.operations.emplace_back(Concatenate{
                destination,
                std::move(operands),
                static_cast<std::uint32_t>(width)});
            return destination;
        }

        return ExpressionAttempt{};
    }

} // namespace fsim::elaboration
