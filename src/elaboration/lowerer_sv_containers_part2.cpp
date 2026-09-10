// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    [[nodiscard]] bool aggregate_box(
        const ContainerType& type) noexcept
    {
        return type.element_kind == ContainerElementKind::Aggregate
            && type.aggregate_value;
    }

    [[nodiscard]] std::optional<std::uint64_t> container_value_bits(
        const ContainerType& type);

    [[maybe_unused, nodiscard]] std::optional<std::uint64_t> aggregate_type_bits(
        const frontend::Type& type)
    {
        const bool unpacked_union = type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedUnion;
        const bool unpacked_structure = type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedStruct;
        if (!unpacked_union && !unpacked_structure) {
            return type.width();
        }
        if (type.packed_members.empty()) {
            return std::nullopt;
        }
        std::uint64_t result { };
        for (const auto& member : type.packed_members) {
            if (member.nested_types.size() != 1U) {
                return std::nullopt;
            }
            const auto bits = aggregate_type_bits(member.nested_types.front());
            if (!bits) {
                return std::nullopt;
            }
            if (unpacked_union) {
                result = std::max(result, *bits);
            } else {
                if (*bits > std::numeric_limits<std::uint64_t>::max() - result) {
                    return std::nullopt;
                }
                result += *bits;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::uint64_t> container_element_bits(
        const ContainerType& type)
    {
        switch (type.element_kind) {
        case ContainerElementKind::Packed:
        case ContainerElementKind::Scalar:
            return type.element_width;
        case ContainerElementKind::String:
            return std::nullopt;
        case ContainerElementKind::Container:
            return type.element_types.size() == 1U
                ? container_value_bits(type.element_types.front())
                : std::nullopt;
        case ContainerElementKind::Aggregate:
            break;
        }
        if (type.element_types.empty()) {
            return std::nullopt;
        }
        std::uint64_t result { };
        for (const auto& member : type.element_types) {
            const auto bits = container_value_bits(member);
            if (!bits) {
                return std::nullopt;
            }
            if (type.union_aggregate) {
                result = std::max(result, *bits);
            } else {
                if (*bits > std::numeric_limits<std::uint64_t>::max() - result) {
                    return std::nullopt;
                }
                result += *bits;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::uint64_t> container_value_bits(
        const ContainerType& type)
    {
        const auto element = container_element_bits(type);
        if (!element) {
            return std::nullopt;
        }
        if (aggregate_box(type)) {
            return element;
        }
        if (!type.fixed) {
            return std::nullopt;
        }
        std::uint64_t count { 1 };
        if (type.dimensions.empty()) {
            count = static_cast<std::uint64_t>(
                std::abs(
                    static_cast<std::int64_t>(type.index_left)
                    - type.index_right)
                + 1);
        } else {
            for (const auto& dimension : type.dimensions) {
                const auto extent = static_cast<std::uint64_t>(
                    std::abs(
                        static_cast<std::int64_t>(dimension.first)
                        - dimension.second)
                    + 1);
                if (extent != 0
                    && count
                        > std::numeric_limits<std::uint64_t>::max() / extent) {
                    return std::nullopt;
                }
                count *= extent;
            }
        }
        if (*element != 0
            && count
                > std::numeric_limits<std::uint64_t>::max() / *element) {
            return std::nullopt;
        }
        return count * *element;
    }

} // namespace

std::optional<std::vector<ContainerPredicateNode>>
Lowerer::lower_container_expression_graph(
    const Expression& expression,
    const std::string_view iterator_name,
    const frontend::Type& source_type,
    const ContainerType& runtime_type,
    const ContainerExpressionPurpose purpose)
{
    using PredicateKind = ContainerPredicateValueKind;
    const bool transformation = purpose != ContainerExpressionPurpose::predicate;
    const bool ordering_key = purpose == ContainerExpressionPurpose::ordering_key;
    const bool locator_transformation = purpose == ContainerExpressionPurpose::locator_transformation;
    std::vector<ContainerPredicateNode> nodes;
    std::size_t conditional_count { };
    const auto unsupported_code = ordering_key
        ? "FSIM-ELAB-SVORDER-006"
        : locator_transformation
        ? "FSIM-ELAB-SVLOCATOR-006"
        : transformation
        ? "FSIM-ELAB-SVREDUCE-004"
        : "FSIM-ELAB-SVFIND-004";
    const auto reference_code = ordering_key
        ? "FSIM-ELAB-SVORDER-007"
        : locator_transformation
        ? "FSIM-ELAB-SVLOCATOR-007"
        : transformation
        ? "FSIM-ELAB-SVREDUCE-005"
        : "FSIM-ELAB-SVFIND-008";
    const auto expression_name = ordering_key
        ? std::string_view { "container ordering key expression" }
        : locator_transformation
        ? std::string_view { "container locator transformation" }
        : transformation
        ? std::string_view { "reduction transformation" }
        : std::string_view { "container locator predicate" };
    const auto append =
        [&](ContainerPredicateNode node)
        -> std::optional<std::uint32_t> {
        if (nodes.size() >= maximum_container_predicate_nodes) {
            report(
                unsupported_code,
                std::string { expression_name }
                    + " is limited to 64 nodes",
                expression.span);
            return std::nullopt;
        }
        nodes.push_back(std::move(node));
        return static_cast<std::uint32_t>(nodes.size() - 1U);
    };
    const auto direct_kind =
        [&](const Expression& candidate)
        -> std::optional<PredicateKind> {
        if (candidate.kind != ExpressionKind::Identifier) {
            return std::nullopt;
        }
        if (candidate.text == iterator_name) {
            return PredicateKind::element;
        }
        if (candidate.text
            == std::string { iterator_name } + ".index") {
            return PredicateKind::index;
        }
        return std::nullopt;
    };
    const auto contains_iterator =
        [&](const auto& self, const Expression& candidate) -> bool {
        return (candidate.kind == ExpressionKind::Identifier
                   && (candidate.text == iterator_name
                       || candidate.text.starts_with(
                           std::string { iterator_name } + ".")))
            || std::ranges::any_of(
                candidate.operands,
                [&](const auto& operand) {
                    return self(self, operand);
                });
    };
    const auto contains_identifier =
        [&](const auto& self, const Expression& candidate) -> bool {
        return candidate.kind == ExpressionKind::Identifier
            || std::ranges::any_of(
                candidate.operands,
                [&](const auto& operand) {
                    return self(self, operand);
                });
    };
    const auto contains_index_reference =
        [&](const auto& self, const Expression& candidate) -> bool {
        return (candidate.kind == ExpressionKind::Identifier
                   && (candidate.text
                           == std::string { iterator_name } + ".index"
                       || candidate.text.starts_with(
                           std::string { iterator_name }
                           + ".index.")))
            || (candidate.kind == ExpressionKind::Call
                && candidate.text == ".index"
                && contains_iterator(
                    contains_iterator, candidate))
            || std::ranges::any_of(
                candidate.operands,
                [&](const auto& operand) {
                    return self(self, operand);
                });
    };
    const auto report_invalid_reference =
        [&](const Expression& candidate,
            const std::string_view reason) {
            report(
                reference_code,
                "container iterator '" + std::string { iterator_name }
                    + "' " + std::string { reason },
                candidate.span);
        };
    const auto lower_constant =
        [&](const Expression& candidate,
            const PredicateKind value_kind)
        -> std::optional<std::uint32_t> {
        const frontend::Type index_type {
            frontend::ValueDomain::Integer, "int",
            std::nullopt, true
        };
        const auto& conversion_type = value_kind == PredicateKind::index
            ? index_type
            : source_type;
        const auto width = value_kind == PredicateKind::index
            ? 32U
            : runtime_type.element_width;
        std::string error;
        const auto value = evaluate_systemverilog_constant_expression(
            candidate, { }, { }, error);
        const auto converted = value
            ? convert_systemverilog_parameter_value(
                  *value, conversion_type, error)
            : std::nullopt;
        const auto literal = converted
            ? literal_value(
                  converted->expression(candidate.span),
                  width,
                  frontend::Language::SystemVerilog2017)
            : std::nullopt;
        if (!literal
            || literal->value.width() != width
            || ((value_kind == PredicateKind::index
                    || runtime_type.two_state)
                && std::ranges::any_of(
                    literal->value.bval_words(),
                    [](const auto word) { return word != 0; }))) {
            if (!value
                && contains_identifier(
                    contains_identifier, candidate)) {
                report_invalid_reference(
                    candidate,
                    "predicate contains an unknown iterator reference");
                return std::nullopt;
            }
            report(
                unsupported_code,
                std::string { expression_name }
                    + " constants must be locally "
                      "constant and convertible to the selected "
                    + std::string {
                        value_kind == PredicateKind::index
                            ? "signed 32-bit index"
                            : "element type" }
                    + (error.empty() ? std::string { } : ": " + error),
                candidate.span);
            return std::nullopt;
        }
        ContainerPredicateNode node;
        node.operation = ContainerPredicateOperator::constant;
        node.constant = literal->value;
        node.value_kind = value_kind;
        return append(std::move(node));
    };
    const auto lower_value =
        [&](const Expression& candidate,
            const PredicateKind expected_kind)
        -> std::optional<std::uint32_t> {
        if (const auto kind = direct_kind(candidate)) {
            if (*kind != expected_kind) {
                report_invalid_reference(
                    candidate,
                    "cannot mix element and index comparison operands");
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = *kind == PredicateKind::index
                ? ContainerPredicateOperator::index
                : ContainerPredicateOperator::item;
            node.value_kind = *kind;
            return append(std::move(node));
        }
        if (contains_iterator(contains_iterator, candidate)) {
            if (contains_index_reference(
                    contains_index_reference, candidate)) {
                report_invalid_reference(
                    candidate,
                    "supports only its direct value or direct .index leaf");
            } else {
                report(
                    unsupported_code,
                    std::string { expression_name }
                        + " accepts only the scoped '"
                        + std::string { iterator_name }
                        + "' iterator or locally constant operands",
                    candidate.span);
            }
            return std::nullopt;
        }
        if (candidate.kind == ExpressionKind::Identifier
            && candidate.text.find(".index")
                != std::string::npos) {
            report_invalid_reference(
                candidate,
                "predicate contains an unknown iterator reference");
            return std::nullopt;
        }
        return lower_constant(candidate, expected_kind);
    };
    std::function<std::optional<std::uint32_t>(
        const Expression&)>
        lower;
    lower =
        [&](const Expression& candidate)
        -> std::optional<std::uint32_t> {
        if (const auto kind = direct_kind(candidate)) {
            ContainerPredicateNode node;
            node.operation = *kind == PredicateKind::index
                ? ContainerPredicateOperator::index
                : ContainerPredicateOperator::item;
            node.value_kind = *kind;
            return append(std::move(node));
        }
        if (!contains_iterator(
                contains_iterator, candidate)) {
            if (candidate.kind == ExpressionKind::Identifier
                && candidate.text.find(".index")
                    != std::string::npos) {
                report_invalid_reference(
                    candidate,
                    "predicate contains an unknown iterator reference");
                return std::nullopt;
            }
            return lower_constant(
                candidate, PredicateKind::element);
        }
        if (candidate.kind == ExpressionKind::Unary
            && candidate.text == "!"
            && candidate.operands.size() == 1) {
            const auto operand = lower(candidate.operands.front());
            if (!operand) {
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = ContainerPredicateOperator::logical_not;
            node.left = *operand;
            node.value_kind = PredicateKind::logical;
            return append(std::move(node));
        }
        if (transformation
            && candidate.kind == ExpressionKind::Call
            && candidate.text == "?:"
            && candidate.operands.size() == 3) {
            if (++conditional_count > 1U) {
                report(
                    unsupported_code,
                    ordering_key
                        ? "container ordering key expressions permit one "
                          "conditional key selection"
                        : locator_transformation
                        ? "container locator transformations permit one "
                          "conditional key selection"
                        : "container reduction transformations permit one "
                          "conditional element selection",
                    candidate.span);
                return std::nullopt;
            }
            const auto condition = lower(candidate.operands[0]);
            const auto when_true = lower_value(
                candidate.operands[1],
                PredicateKind::element);
            const auto when_false = lower_value(
                candidate.operands[2],
                PredicateKind::element);
            if (!condition || !when_true || !when_false) {
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = ContainerPredicateOperator::conditional;
            node.left = *condition;
            node.right = *when_true;
            node.third = *when_false;
            node.value_kind = PredicateKind::element;
            return append(std::move(node));
        }
        if (candidate.kind == ExpressionKind::Binary
            && candidate.operands.size() == 2) {
            const bool logical = candidate.text == "&&"
                || candidate.text == "||";
            const bool comparison = candidate.text == "=="
                || candidate.text == "!="
                || candidate.text == "<"
                || candidate.text == "<="
                || candidate.text == ">"
                || candidate.text == ">=";
            if (logical || comparison) {
                std::optional<std::uint32_t> left;
                std::optional<std::uint32_t> right;
                if (comparison) {
                    const auto left_kind = direct_kind(candidate.operands[0]);
                    const auto right_kind = direct_kind(candidate.operands[1]);
                    if (left_kind && right_kind
                        && *left_kind != *right_kind) {
                        report_invalid_reference(
                            candidate,
                            "cannot mix element and index comparison operands");
                        return std::nullopt;
                    }
                    const auto comparison_kind = left_kind.value_or(
                        right_kind.value_or(
                            PredicateKind::element));
                    left = lower_value(
                        candidate.operands[0], comparison_kind);
                    right = lower_value(
                        candidate.operands[1], comparison_kind);
                } else {
                    left = lower(candidate.operands[0]);
                    right = lower(candidate.operands[1]);
                }
                if (!left || !right) {
                    return std::nullopt;
                }
                ContainerPredicateNode node;
                node.left = *left;
                node.right = *right;
                node.value_kind = PredicateKind::logical;
                if (candidate.text == "==") {
                    node.operation = ContainerPredicateOperator::equal;
                } else if (candidate.text == "!=") {
                    node.operation = ContainerPredicateOperator::not_equal;
                } else if (candidate.text == "<") {
                    node.operation = ContainerPredicateOperator::less;
                } else if (candidate.text == "<=") {
                    node.operation = ContainerPredicateOperator::less_equal;
                } else if (candidate.text == ">") {
                    node.operation = ContainerPredicateOperator::greater;
                } else if (candidate.text == ">=") {
                    node.operation = ContainerPredicateOperator::greater_equal;
                } else if (candidate.text == "&&") {
                    node.operation = ContainerPredicateOperator::logical_and;
                } else {
                    node.operation = ContainerPredicateOperator::logical_or;
                }
                return append(std::move(node));
            }
        }
        report(
            unsupported_code,
            std::string { expression_name }
                + " supports the scoped '"
                + std::string { iterator_name }
                + "' iterator, its direct .index leaf, locally constant "
                  "operands, comparisons, logical &&, ||, and !"
                + (transformation
                        ? ", and one conditional element selection"
                        : ""),
            candidate.span);
        return std::nullopt;
    };
    const auto root = lower(expression);
    if (!root) {
        return std::nullopt;
    }
    if (transformation
        && nodes[*root].value_kind != PredicateKind::element) {
        report(
            ordering_key
                ? "FSIM-ELAB-SVORDER-008"
                : locator_transformation
                ? "FSIM-ELAB-SVLOCATOR-008"
                : "FSIM-ELAB-SVREDUCE-006",
            ordering_key
                ? "container ordering key root must have the receiver "
                  "element type"
                : locator_transformation
                ? "container locator transformation root must have the "
                  "receiver element type"
                : "container reduction transformation root must have the "
                  "receiver element type",
            expression.span);
        return std::nullopt;
    }
    return nodes;
}

bool Lowerer::lower_container_locator(
    const Expression& expression,
    const ContainerRegisterId destination,
    const ContainerType& destination_type)
{
    const bool predicate_locator = expression.text == ".find"
        || expression.text == ".find_index"
        || expression.text == ".find_first"
        || expression.text == ".find_first_index"
        || expression.text == ".find_last"
        || expression.text == ".find_last_index";
    const bool has_transformation = !predicate_locator
        && expression.operands.size() >= 2U;
    if (language_ != frontend::Language::SystemVerilog2017
        || expression.operands.empty()
        || (!is_container_expression(
                expression.operands.front())
            && !is_static_container_slice_candidate(
                expression.operands.front()))) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-001"
                : "FSIM-ELAB-SVLOCATOR-001",
            "container locators require a direct supported "
            "SystemVerilog unpacked-container receiver",
            expression.span);
        return false;
    }
    if ((!predicate_locator
            && (expression.operands.size() < 1U
                || expression.operands.size() > 3U))
        || (predicate_locator
            && expression.operands.size() != 2U
            && expression.operands.size() != 3U)) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-002"
                : "FSIM-ELAB-SVLOCATOR-002",
            predicate_locator
                ? "predicate container locator methods require exactly "
                  "one with-clause predicate"
                : "bounded container locator methods take no value "
                  "arguments and retain at most one iterator plus one "
                  "with-clause transformation",
            expression.span);
        return false;
    }
    const bool explicit_iterator = expression.operands.size() == 3U;
    if (explicit_iterator
        && expression.operands[1].kind
            != ExpressionKind::Identifier) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-007"
                : "FSIM-ELAB-SVLOCATOR-008",
            "a named container locator iterator must be one identifier",
            expression.operands[1].span);
        return false;
    }
    const std::string_view iterator_name = explicit_iterator
        ? std::string_view { expression.operands[1].text }
        : std::string_view { "item" };
    const auto& receiver = expression.operands.front();
    const auto source = lower_container_expression(receiver);
    const auto* source_frontend_type = container_expression_type(receiver);
    const auto source_type = container_expression_runtime_type(receiver);
    if (!source || !source_type) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-001"
                : "FSIM-ELAB-SVLOCATOR-001",
            "bounded container locators do not support associative "
            "or unresolved receivers",
            expression.span);
        return false;
    }
    if (source_type->associative) {
        report(
            has_transformation
                ? "FSIM-ELAB-SVLOCATOR-008"
                : predicate_locator
                ? "FSIM-ELAB-SVFIND-001"
                : "FSIM-ELAB-SVLOCATOR-001",
            "bounded container locators do not support associative "
            "receivers",
            expression.span);
        return false;
    }
    if (source_type->element_kind != ContainerElementKind::Packed) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-008"
                : "FSIM-ELAB-SVLOCATOR-008",
            "container locators require packed integral elements",
            expression.span);
        return false;
    }
    const auto iterator_key = std::string { iterator_name };
    const bool iterator_collision = explicit_iterator
        && (object_type(iterator_name) != nullptr
            || locals_.contains(iterator_key)
            || string_locals_.contains(iterator_key)
            || container_locals_.contains(iterator_key)
            || signals_.contains(iterator_key)
            || string_objects_.contains(iterator_key)
            || container_objects_.contains(iterator_key));
    if (iterator_collision) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-007"
                : "FSIM-ELAB-SVLOCATOR-008",
            "named container locator iterator '" + std::string { iterator_name }
                + "' collides with a visible object",
            expression.operands[1].span);
        return false;
    }
    const bool index_result = expression.text == ".unique_index"
        || expression.text == ".find_index"
        || expression.text == ".find_first_index"
        || expression.text == ".find_last_index";
    const bool compatible = destination_type.queue
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
            predicate_locator
                ? "FSIM-ELAB-SVFIND-003"
                : "FSIM-ELAB-SVLOCATOR-003",
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
    if (expression.text == ".find") {
        operation = ContainerLocatorOperator::find;
    } else if (expression.text == ".find_index") {
        operation = ContainerLocatorOperator::find_index;
    } else if (expression.text == ".find_first") {
        operation = ContainerLocatorOperator::find_first;
    } else if (expression.text == ".find_first_index") {
        operation = ContainerLocatorOperator::find_first_index;
    } else if (expression.text == ".find_last") {
        operation = ContainerLocatorOperator::find_last;
    } else if (expression.text == ".find_last_index") {
        operation = ContainerLocatorOperator::find_last_index;
    }
    std::vector<ContainerPredicateNode> predicate;
    std::vector<ContainerPredicateNode> transformation;
    if (predicate_locator) {
        const auto& predicate_expression = expression.operands[explicit_iterator ? 2U : 1U];
        const auto lowered = lower_container_expression_graph(
            predicate_expression, iterator_name,
            *source_frontend_type,
            *source_type,
            ContainerExpressionPurpose::predicate);
        if (!lowered) {
            return false;
        }
        predicate = std::move(*lowered);
    } else if (has_transformation) {
        const auto& transformation_expression = expression.operands[explicit_iterator ? 2U : 1U];
        const auto lowered = lower_container_expression_graph(
            transformation_expression, iterator_name,
            *source_frontend_type, *source_type,
            ContainerExpressionPurpose::locator_transformation);
        if (!lowered) {
            return false;
        }
        transformation = std::move(*lowered);
    }
    process_.operations.emplace_back(
        LocateContainer {
            operation, destination, *source,
            std::move(predicate),
            std::move(transformation) });
    return true;
}

void Lowerer::lower_container_method(
    const Statement& statement)
{
    if (lower_synchronization_method_statement(statement)) {
        return;
    }
    if (lower_process_method_statement(statement)) {
        return;
    }
    if (lower_string_method_statement(statement)) {
        return;
    }
    const auto& call = statement.value;
    constexpr std::string_view class_container_method_prefix {
        "@sv-container-method:"
    };
    if (call.kind == ExpressionKind::Call
        && call.text.starts_with(class_container_method_prefix)) {
        constexpr std::string_view push_back { "push_back:" };
        const auto operation = std::string_view { call.text }.substr(
            class_container_method_prefix.size());
        if (!operation.starts_with(push_back)
            || call.operands.size() != 2U) {
            report(
                "FSIM-ELAB-SVCLASS-015",
                "class handle container statement requires push_back(value)",
                statement.span);
            return;
        }
        const auto receiver = lower_expression(call.operands[0], 64);
        const auto value = lower_expression(call.operands[1], 64);
        if (!receiver || !value)
            return;
        const auto destination = allocate_register(
            64, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ClassMethodCall {
            destination,
            *receiver,
            "@container-push-back:"
                + std::string { operation.substr(push_back.size()) },
            { *value },
            { "" },
            { static_cast<std::uint8_t>(frontend::PortDirection::Input) },
            64,
            false });
        return;
    }
    const bool ordering_method = call.kind == ExpressionKind::Call
        && (call.text == ".reverse"
            || call.text == ".sort"
            || call.text == ".rsort"
            || call.text == ".shuffle");
    const bool ordering_slice_receiver = ordering_method
        && !call.operands.empty()
        && is_static_container_slice_candidate(
            call.operands.front());
    const bool locator_method = call.kind == ExpressionKind::Call
        && (call.text == ".min"
            || call.text == ".max"
            || call.text == ".unique"
            || call.text == ".unique_index"
            || call.text == ".find"
            || call.text == ".find_index"
            || call.text == ".find_first"
            || call.text == ".find_first_index"
            || call.text == ".find_last"
            || call.text == ".find_last_index");
    if (locator_method) {
        const bool predicate_locator = call.text.starts_with(".find");
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-005"
                : "FSIM-ELAB-SVLOCATOR-005",
            "container locator results cannot be discarded",
            call.span);
        return;
    }
    if (ordering_method) {
        if (language_
                != frontend::Language::SystemVerilog2017
            || call.operands.empty()
            || (!ordering_slice_receiver
                && (call.operands.front().kind
                        != ExpressionKind::Identifier
                    || !is_container_expression(
                        call.operands.front())))) {
            report(
                "FSIM-ELAB-SVORDER-001",
                "container ordering requires a direct writable "
                "SystemVerilog unpacked-container receiver",
                call.span);
            return;
        }
        const bool ordering_key_method = call.text == ".sort"
            || call.text == ".rsort";
        const bool valid_arity = ordering_key_method
            ? call.operands.size() >= 1
                && call.operands.size() <= 3
            : call.operands.size() == 1;
        if (!valid_arity) {
            report(
                "FSIM-ELAB-SVORDER-002",
                ordering_key_method
                    ? "sort and rsort take no value arguments and retain "
                      "at most one iterator plus one with-clause key"
                    : "reverse and shuffle take no arguments",
                call.span);
            return;
        }
        if (call.operands.size() == 3
            && call.operands[1].kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVORDER-008",
                "a named container ordering iterator must be one identifier",
                call.operands[1].span);
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
        || (call.operands.front().kind
                != ExpressionKind::Identifier
            && !ordering_slice_receiver)) {
        report(
            "FSIM-ELAB-SVCONTAINER-007",
            "container method requires a direct object receiver",
            statement.span);
        return;
    }
    const auto& receiver = call.operands.front();
    const auto& receiver_base = ordering_slice_receiver
        ? receiver.operands.front()
        : receiver;
    const auto mutates_receiver = call.text == ".delete"
        || call.text == ".insert"
        || call.text == ".push_front"
        || call.text == ".push_back"
        || call.text == ".pop_front"
        || call.text == ".pop_back"
        || ordering_method;
    if (mutates_receiver
        && read_only_container_objects_.contains(
            receiver_base.text)) {
        report(
            "FSIM-ELAB-SVPORT-009",
            "an input container port is read-only within its module",
            receiver_base.span);
        return;
    }
    const auto target = lower_container_expression(receiver_base);
    const auto* type = object_type(receiver_base.text);
    if (!target || type == nullptr) {
        return;
    }
    const auto runtime_type = container_type(*type, call.span);
    if (!runtime_type) {
        return;
    }
    if (ordering_method
        && runtime_type->element_kind
            != ContainerElementKind::Packed) {
        report(
            "FSIM-ELAB-SVORDER-008",
            "container ordering requires packed integral elements",
            call.span);
        return;
    }
    const auto width = type->width();
    if (!width) {
        return;
    }
    if (call.text == ".delete") {
        if (call.operands.size() == 2) {
            if (!runtime_type->associative && !runtime_type->queue) {
                report(
                    "FSIM-ELAB-SVCONTAINER-018",
                    "delete(index) requires a queue or associative-array receiver",
                    call.span);
                return;
            }
            const bool string_index = runtime_type->associative
                && runtime_type->string_indices;
            const auto index = string_index
                ? lower_string_expression(call.operands[1])
                : lower_expression(
                      call.operands[1],
                      runtime_type->associative
                          ? runtime_type->index_width
                          : 32U,
                      runtime_type->associative
                          ? type->systemverilog_container
                                ->associative_index_type.get()
                          : nullptr);
            if (!index) {
                return;
            }
            process_.operations.emplace_back(
                DeleteContainer { *target, *index, string_index });
        } else {
            if (runtime_type->fixed) {
                report(
                    "FSIM-ELAB-SVCONTAINER-021",
                    "delete() cannot clear a static unpacked array",
                    call.span);
                return;
            }
            process_.operations.emplace_back(
                DeleteContainer { *target, std::nullopt });
        }
    } else if (call.text == ".insert") {
        if (!runtime_type->queue) {
            report(
                "FSIM-ELAB-SVCONTAINER-019",
                "insert requires a queue receiver", call.span);
            return;
        }
        if (call.operands.size() != 3U) {
            return;
        }
        const auto index = lower_expression(call.operands[1], 32);
        const auto value = lower_expression(call.operands[2], *width, type);
        if (!index || !value) {
            return;
        }
        process_.operations.emplace_back(
            PushContainer { *target, *value, false, *index });
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
        const auto value = lower_expression(call.operands[1], *width, type);
        if (!value) {
            return;
        }
        process_.operations.emplace_back(
            PushContainer {
                *target, *value,
                call.text == ".push_front" });
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
        const auto discarded = allocate_register(*width, type->domain);
        process_.operations.emplace_back(
            PopContainer {
                discarded, *target,
                call.text == ".pop_front" });
    } else if (ordering_method) {
        auto ordering_target = *target;
        auto ordering_type = *runtime_type;
        if (ordering_slice_receiver) {
            const auto selection = static_container_slice(receiver);
            if (!selection) {
                return;
            }
            ordering_type = selection->selected_type;
            ordering_target = allocate_container_register(ordering_type);
            copy_static_container_ordinals(
                ordering_target,
                ordering_type,
                *target,
                ordering_type);
        }
        if (ordering_type.associative) {
            report(
                "FSIM-ELAB-SVORDER-003",
                "container ordering does not support associative arrays",
                call.span);
            return;
        }
        const bool explicit_iterator = call.operands.size() == 3;
        const bool has_key = call.operands.size() >= 2;
        const std::string_view iterator_name = explicit_iterator
            ? std::string_view { call.operands[1].text }
            : std::string_view { "item" };
        const auto iterator_key = std::string { iterator_name };
        const bool iterator_collision = explicit_iterator
            && (object_type(iterator_name) != nullptr
                || locals_.contains(iterator_key)
                || string_locals_.contains(iterator_key)
                || container_locals_.contains(iterator_key)
                || signals_.contains(iterator_key)
                || string_objects_.contains(iterator_key)
                || container_objects_.contains(iterator_key));
        if (iterator_collision) {
            report(
                "FSIM-ELAB-SVORDER-008",
                "named container ordering iterator '"
                    + std::string { iterator_name }
                    + "' collides with a visible object",
                call.operands[1].span);
            return;
        }
        std::vector<ContainerPredicateNode> key;
        if (has_key) {
            const auto& key_expression = call.operands[explicit_iterator ? 2U : 1U];
            const auto lowered = lower_container_expression_graph(
                key_expression, iterator_name,
                *type, ordering_type,
                ContainerExpressionPurpose::ordering_key);
            if (!lowered) {
                return;
            }
            key = std::move(*lowered);
        }
        auto operation = ContainerOrderingOperator::reverse;
        if (call.text == ".sort") {
            operation = ContainerOrderingOperator::ascending;
        } else if (call.text == ".rsort") {
            operation = ContainerOrderingOperator::descending;
        } else if (call.text == ".shuffle") {
            operation = ContainerOrderingOperator::shuffle;
        }
        process_.operations.emplace_back(
            OrderContainer {
                operation, ordering_target, std::move(key) });
        if (ordering_slice_receiver) {
            const auto replacement = allocate_container_register(*runtime_type);
            process_.operations.emplace_back(
                CopyContainerRegister { replacement, *target });
            copy_static_container_ordinals(
                replacement,
                ordering_type,
                ordering_target,
                ordering_type);
            process_.operations.emplace_back(
                CopyContainerRegister { *target, replacement });
        }
    } else {
        report(
            "FSIM-ELAB-SVCONTAINER-008",
            "unsupported container method '" + call.text + "'",
            call.span);
        return;
    }
    if (const auto object = container_objects_.find(receiver_base.text);
        object != container_objects_.end()) {
        process_.operations.emplace_back(
            WriteContainerObject { object->second, *target, std::nullopt });
    }
}

} // namespace fsim::elaboration
