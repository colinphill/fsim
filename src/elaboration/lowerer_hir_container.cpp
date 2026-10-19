// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
namespace {

    bool is_container_reduction(const std::string_view method) noexcept
    {
        return method == ".sum" || method == ".product"
            || method == ".and" || method == ".or" || method == ".xor";
    }

    bool is_container_locator(const std::string_view method) noexcept
    {
        return method == ".min" || method == ".max"
            || method == ".unique" || method == ".unique_index"
            || method == ".find" || method == ".find_index"
            || method == ".find_first" || method == ".find_first_index"
            || method == ".find_last" || method == ".find_last_index";
    }

    bool is_predicate_locator(const std::string_view method) noexcept
    {
        return method.starts_with(".find");
    }

} // namespace

std::optional<std::vector<ContainerPredicateNode>>
Lowerer::lower_hir_container_expression_graph(
    const semantic::ExpressionId expression_id,
    const std::string_view iterator_name,
    const ContainerType& source_type,
    const HirContainerExpressionPurpose purpose)
{
    using ValueKind = ContainerPredicateValueKind;
    const bool predicate
        = purpose == HirContainerExpressionPurpose::locator_predicate;
    const bool locator_transformation
        = purpose == HirContainerExpressionPurpose::locator_transformation;
    const auto unsupported_code = locator_transformation
        ? "FSIM-ELAB-SVLOCATOR-006"
        : predicate ? "FSIM-ELAB-SVFIND-004"
                    : "FSIM-ELAB-SVREDUCE-004";
    const auto reference_code = locator_transformation
        ? "FSIM-ELAB-SVLOCATOR-007"
        : predicate ? "FSIM-ELAB-SVFIND-008"
                    : "FSIM-ELAB-SVREDUCE-005";
    const auto expression_name = locator_transformation
        ? std::string_view { "container locator transformation" }
        : predicate ? std::string_view { "container locator predicate" }
                    : std::string_view { "reduction transformation" };
    const auto root = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!root || root->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto root_span = hir_source_span(root->systemverilog->source);
    std::vector<ContainerPredicateNode> nodes;
    std::size_t conditional_count { };
    const auto append = [&](ContainerPredicateNode node)
        -> std::optional<std::uint32_t> {
        if (nodes.size() >= maximum_container_predicate_nodes) {
            report(
                unsupported_code,
                std::string { expression_name } + " is limited to 64 nodes",
                root_span);
            return std::nullopt;
        }
        nodes.push_back(std::move(node));
        return static_cast<std::uint32_t>(nodes.size() - 1U);
    };
    const auto direct_kind = [&](const semantic::sv::Expression& expression)
        -> std::optional<ValueKind> {
        if (expression.kind != semantic::sv::ExpressionKind::name) {
            return std::nullopt;
        }
        if (expression.text == iterator_name) {
            return ValueKind::element;
        }
        if (expression.text == std::string { iterator_name } + ".index") {
            return ValueKind::index;
        }
        return std::nullopt;
    };
    // Whether an expression in the operand DAG rooted at `candidate`
    // satisfies `predicate`.
    const auto any_expression = [&](const semantic::ExpressionId candidate,
                                    const auto& predicate) {
        std::vector<semantic::ExpressionId> pending { candidate };
        std::unordered_set<std::uint32_t> visited;
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!visited.insert(current.value()).second) {
                continue;
            }
            const auto expression
                = specialized_hir_unit_->find_expression(current);
            if (!expression || expression->systemverilog == nullptr) {
                continue;
            }
            const auto& source = *expression->systemverilog;
            if (predicate(source)) {
                return true;
            }
            pending.insert(
                pending.end(), source.operands.begin(), source.operands.end());
        }
        return false;
    };
    const auto index_name = std::string { iterator_name } + ".index";
    const auto contains_iterator = [&](const semantic::ExpressionId candidate) {
        return any_expression(
            candidate, [&](const semantic::sv::Expression& source) {
                return source.kind == semantic::sv::ExpressionKind::name
                    && (source.text == iterator_name
                        || source.text.starts_with(
                            std::string { iterator_name } + "."));
            });
    };
    const auto contains_iterator_index
        = [&](const semantic::ExpressionId candidate) {
        return any_expression(
            candidate, [&](const semantic::sv::Expression& source) {
                return (source.kind == semantic::sv::ExpressionKind::name
                           && (source.text == index_name
                               || source.text.starts_with(index_name + ".")))
                    || (source.kind == semantic::sv::ExpressionKind::call
                        && source.text == ".index");
            });
    };
    const auto contains_unknown_index
        = [&](const semantic::ExpressionId candidate) {
        return any_expression(
            candidate, [&](const semantic::sv::Expression& source) {
                return source.kind == semantic::sv::ExpressionKind::name
                    && source.text.find(".index") != std::string::npos
                    && source.text != index_name;
            });
    };
    const auto report_reference = [&](const semantic::sv::Expression& source,
                                      const std::string_view reason) {
        report(
            reference_code,
            "container iterator '" + std::string { iterator_name }
                + "' " + std::string { reason },
            hir_source_span(source.source));
    };
    const auto lower_constant = [&](const semantic::ExpressionId candidate,
                                    const ValueKind kind)
        -> std::optional<std::uint32_t> {
        const auto expression
            = specialized_hir_unit_->find_expression(candidate);
        const auto value = hir_constant_integer(candidate);
        if (!expression || expression->systemverilog == nullptr || !value) {
            if (expression && expression->systemverilog != nullptr
                && contains_unknown_index(candidate)) {
                report_reference(
                    *expression->systemverilog,
                    "predicate contains an unknown iterator reference");
            } else {
                report(
                    unsupported_code,
                    std::string { expression_name }
                        + " constants must be locally constant and convertible "
                          "to the selected element type",
                    expression && expression->systemverilog != nullptr
                        ? hir_source_span(expression->systemverilog->source)
                        : root_span);
            }
            return std::nullopt;
        }
        ContainerPredicateNode node;
        node.operation = ContainerPredicateOperator::constant;
        node.value_kind = kind;
        node.constant = integer_value(
            *value,
            kind == ValueKind::index ? 32U : source_type.element_width);
        return append(std::move(node));
    };
    const auto lower_value = [&](const semantic::ExpressionId candidate,
                                 const ValueKind expected)
        -> std::optional<std::uint32_t> {
        const auto expression
            = specialized_hir_unit_->find_expression(candidate);
        if (!expression || expression->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& source = *expression->systemverilog;
        if (const auto kind = direct_kind(source)) {
            if (*kind != expected) {
                report_reference(
                    source, "cannot mix element and index comparison operands");
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = *kind == ValueKind::index
                ? ContainerPredicateOperator::index
                : ContainerPredicateOperator::item;
            node.value_kind = *kind;
            return append(std::move(node));
        }
        if (contains_iterator(candidate)) {
            if (contains_iterator_index(candidate)) {
                report_reference(
                    source,
                    "supports only its direct value or direct .index leaf");
            } else {
                report(
                    unsupported_code,
                    std::string { expression_name }
                        + " accepts only the scoped '"
                        + std::string { iterator_name }
                        + "' iterator or locally constant operands",
                    hir_source_span(source.source));
            }
            return std::nullopt;
        }
        return lower_constant(candidate, expected);
    };
    std::function<std::optional<std::uint32_t>(semantic::ExpressionId)> lower;
    lower = [&](const semantic::ExpressionId candidate)
        -> std::optional<std::uint32_t> {
        const auto expression
            = specialized_hir_unit_->find_expression(candidate);
        if (!expression || expression->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& source = *expression->systemverilog;
        if (const auto kind = direct_kind(source)) {
            ContainerPredicateNode node;
            node.operation = *kind == ValueKind::index
                ? ContainerPredicateOperator::index
                : ContainerPredicateOperator::item;
            node.value_kind = *kind;
            return append(std::move(node));
        }
        if (!contains_iterator(candidate)) {
            if (contains_unknown_index(candidate)) {
                report_reference(
                    source, "predicate contains an unknown iterator reference");
                return std::nullopt;
            }
            return lower_constant(candidate, ValueKind::element);
        }
        if (source.kind == semantic::sv::ExpressionKind::unary
            && source.text == "!" && source.operands.size() == 1U) {
            const auto operand = lower(source.operands.front());
            if (!operand) {
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = ContainerPredicateOperator::logical_not;
            node.left = *operand;
            node.value_kind = ValueKind::logical;
            return append(std::move(node));
        }
        if (!predicate && source.text == "?:"
            && source.operands.size() == 3U) {
            if (++conditional_count > 1U) {
                report(
                    unsupported_code,
                    locator_transformation
                        ? "container locator transformations permit one "
                          "conditional key selection"
                        : "container reduction transformations permit one "
                          "conditional element selection",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            const auto condition = lower(source.operands[0]);
            const auto when_true = lower_value(
                source.operands[1], ValueKind::element);
            const auto when_false = lower_value(
                source.operands[2], ValueKind::element);
            if (!condition || !when_true || !when_false) {
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = ContainerPredicateOperator::conditional;
            node.left = *condition;
            node.right = *when_true;
            node.third = *when_false;
            node.value_kind = ValueKind::element;
            return append(std::move(node));
        }
        if (source.kind == semantic::sv::ExpressionKind::binary
            && source.operands.size() == 2U) {
            const bool logical = source.text == "&&" || source.text == "||";
            const bool comparison = source.text == "==" || source.text == "!="
                || source.text == "<" || source.text == "<="
                || source.text == ">" || source.text == ">=";
            if (logical || comparison) {
                std::optional<std::uint32_t> left;
                std::optional<std::uint32_t> right;
                if (comparison) {
                    const auto left_expression
                        = specialized_hir_unit_->find_expression(
                            source.operands[0]);
                    const auto right_expression
                        = specialized_hir_unit_->find_expression(
                            source.operands[1]);
                    const auto left_kind = left_expression
                            && left_expression->systemverilog != nullptr
                        ? direct_kind(*left_expression->systemverilog)
                        : std::nullopt;
                    const auto right_kind = right_expression
                            && right_expression->systemverilog != nullptr
                        ? direct_kind(*right_expression->systemverilog)
                        : std::nullopt;
                    if (left_kind && right_kind && *left_kind != *right_kind) {
                        report_reference(
                            source,
                            "cannot mix element and index comparison operands");
                        return std::nullopt;
                    }
                    const auto kind = left_kind.value_or(
                        right_kind.value_or(ValueKind::element));
                    left = lower_value(source.operands[0], kind);
                    right = lower_value(source.operands[1], kind);
                } else {
                    left = lower(source.operands[0]);
                    right = lower(source.operands[1]);
                }
                if (!left || !right) {
                    return std::nullopt;
                }
                ContainerPredicateNode node;
                node.left = *left;
                node.right = *right;
                node.value_kind = ValueKind::logical;
                if (source.text == "==") {
                    node.operation = ContainerPredicateOperator::equal;
                } else if (source.text == "!=") {
                    node.operation = ContainerPredicateOperator::not_equal;
                } else if (source.text == "<") {
                    node.operation = ContainerPredicateOperator::less;
                } else if (source.text == "<=") {
                    node.operation = ContainerPredicateOperator::less_equal;
                } else if (source.text == ">") {
                    node.operation = ContainerPredicateOperator::greater;
                } else if (source.text == ">=") {
                    node.operation = ContainerPredicateOperator::greater_equal;
                } else if (source.text == "&&") {
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
                + " supports the scoped iterator, its direct .index leaf, "
                  "locally constant operands, comparisons, logical &&, ||, "
                  "and !"
                + (!predicate ? ", and one conditional element selection"
                              : ""),
            hir_source_span(source.source));
        return std::nullopt;
    };
    const auto result = lower(expression_id);
    if (!result) {
        return std::nullopt;
    }
    if (!predicate && nodes[*result].value_kind != ValueKind::element) {
        report(
            locator_transformation
                ? "FSIM-ELAB-SVLOCATOR-008"
                : "FSIM-ELAB-SVREDUCE-006",
            locator_transformation
                ? "container locator transformation root must have the "
                  "receiver element type"
                : "container reduction transformation root must have the "
                  "receiver element type",
            root_span);
        return std::nullopt;
    }
    return nodes;
}

std::optional<RegisterId>
Lowerer::lower_hir_systemverilog_container_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    const auto span = hir_source_span(call.source);
    const auto invalid_result = [&]() {
        const auto width = expected_width == 0U ? 32U : expected_width;
        const auto destination = load_constant(
            unsigned_value(0U, width), frontend::ValueDomain::Bit2);
        return std::optional<RegisterId> { destination };
    };
    const bool reduction = is_container_reduction(call.text);
    const bool locator = is_container_locator(call.text);
    const bool ordering = call.text == ".reverse" || call.text == ".sort"
        || call.text == ".rsort" || call.text == ".shuffle";
    if (ordering) {
        report(
            "FSIM-ELAB-SVORDER-004",
            "container ordering methods do not produce an expression result",
            span);
        return invalid_result();
    }
    if (locator) {
        report(
            is_predicate_locator(call.text)
                ? "FSIM-ELAB-SVFIND-006"
                : "FSIM-ELAB-SVLOCATOR-004",
            "container locator results require a compatible whole-queue "
            "assignment target",
            span);
        return invalid_result();
    }
    // num() is size() for an associative array (IEEE 1800-2017 7.9.1).
    const bool size_query = call.text == ".size" || call.text == ".num";
    const bool query = size_query || call.text == ".exists"
        || call.text == ".first" || call.text == ".last"
        || call.text == ".next" || call.text == ".prev";
    const bool pop = call.text == ".pop_front" || call.text == ".pop_back";
    if (!reduction && !query && !pop) {
        return std::nullopt;
    }
    if (call.operands.empty()) {
        if (reduction) {
            report(
                "FSIM-ELAB-SVREDUCE-002",
                "container reduction methods require a receiver",
                span);
        }
        return invalid_result();
    }
    // A queue selected from another value (`q[i].pop_front()`) is popped
    // through a copy that is written back (IEEE 1800-2017 7.10.2).
    const auto pop_path = pop
            && !hir_container_object_binding(call.operands.front())
        ? hir_sv_value_path(call.operands.front())
        : std::nullopt;
    const bool pop_element = pop_path && !pop_path->leaf
        && !pop_path->steps.empty() && !pop_path->root.read_only
        && pop_path->type.queue;
    if (pop && !pop_element
        && hir_static_container_expression_type(call.operands.front())
        && !hir_container_object_binding(call.operands.front())) {
        report(
            "FSIM-ELAB-SVCONTAINER-022",
            "mutating container methods require a direct writable object "
            "receiver",
            span);
        return invalid_result();
    }
    const auto source = lower_hir_static_container_value(
        call.operands.front());
    if (!source) {
        if (reduction) {
            report(
                "FSIM-ELAB-SVREDUCE-001",
                "container reduction methods require a direct SystemVerilog "
                "unpacked-container receiver",
                span);
        }
        return invalid_result();
    }
    if (size_query) {
        if (call.operands.size() != 1U) {
            return invalid_result();
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        if (source->type.fixed && source->type.dimensions.size() > 1U) {
            const auto& outer = source->type.dimensions.front();
            const auto count = static_cast<std::uint32_t>(
                std::abs(
                    static_cast<std::int64_t>(outer.first) - outer.second)
                + 1);
            process_.operations.emplace_back(LoadConstant {
                destination, unsigned_value(count, 32U) });
        } else {
            process_.operations.emplace_back(ContainerSize {
                destination, source->value });
        }
        return expected_width != 0U && expected_width != 32U
            ? std::optional {
                  resize_register(destination, expected_width, false)
              }
            : std::optional { destination };
    }
    if (reduction) {
        if (call.operands.size() > 3U) {
            report(
                "FSIM-ELAB-SVREDUCE-002",
                "container reduction methods take no arguments and retain at "
                "most one iterator plus one with-clause transformation",
                span);
            return invalid_result();
        }
        if (source->type.element_kind != ContainerElementKind::Packed
            && source->type.element_kind != ContainerElementKind::Scalar) {
            report(
                "FSIM-ELAB-SVREDUCE-006",
                "container reductions require packed integral elements",
                span);
            return invalid_result();
        }
        const bool explicit_iterator = call.operands.size() == 3U;
        const bool transformed = call.operands.size() >= 2U;
        std::string iterator_name { "item" };
        if (explicit_iterator) {
            const auto iterator = specialized_hir_unit_->find_expression(
                call.operands[1]);
            if (!iterator || iterator->systemverilog == nullptr
                || iterator->systemverilog->kind
                    != semantic::sv::ExpressionKind::name) {
                report(
                    "FSIM-ELAB-SVREDUCE-007",
                    "a named container reduction iterator must be one "
                    "identifier",
                    span);
                return invalid_result();
            }
            iterator_name = iterator->systemverilog->text;
            if (hir_referenced_declaration(call.operands[1])) {
                report(
                    "FSIM-ELAB-SVREDUCE-007",
                    "named container reduction iterator '" + iterator_name
                        + "' collides with a visible object",
                    hir_source_span(iterator->systemverilog->source));
                return invalid_result();
            }
        }
        if (transformed && source->type.associative) {
            report(
                "FSIM-ELAB-SVREDUCE-006",
                "container reduction with-clauses do not support "
                "associative-array receivers",
                span);
            return invalid_result();
        }
        std::vector<ContainerPredicateNode> transformation;
        if (transformed) {
            const auto lowered = lower_hir_container_expression_graph(
                call.operands[explicit_iterator ? 2U : 1U],
                iterator_name,
                source->type,
                HirContainerExpressionPurpose::reduction_transformation);
            if (!lowered) {
                return invalid_result();
            }
            transformation = std::move(*lowered);
        }
        auto operation = ContainerReductionOperator::sum;
        if (call.text == ".product") {
            operation = ContainerReductionOperator::product;
        } else if (call.text == ".and") {
            operation = ContainerReductionOperator::bit_and;
        } else if (call.text == ".or") {
            operation = ContainerReductionOperator::bit_or;
        } else if (call.text == ".xor") {
            operation = ContainerReductionOperator::bit_xor;
        }
        const auto destination = allocate_register(
            source->type.element_width,
            source->type.two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ContainerReduction {
            operation,
            destination,
            source->value,
            std::move(transformation),
        });
        return expected_width != 0U
                && expected_width != source->type.element_width
            ? std::optional { resize_register(
                  destination,
                  expected_width,
                  source->type.signed_elements) }
            : std::optional { destination };
    }
    if (pop && pop_element) {
        if (call.operands.size() != 1U) {
            return invalid_result();
        }
        const auto destination = allocate_register(
            pop_path->type.element_width,
            pop_path->type.two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(PopContainer {
            destination, source->value, call.text == ".pop_front" });
        if (!lower_hir_sv_value_path_write(
                *pop_path, std::nullopt, source->value)) {
            return invalid_result();
        }
        return expected_width != 0U
                && expected_width != pop_path->type.element_width
            ? std::optional { resize_register(
                  destination,
                  expected_width,
                  pop_path->type.signed_elements) }
            : std::optional { destination };
    }
    if (pop) {
        const auto binding = hir_container_object_binding(
            call.operands.front());
        if (!binding || binding->type == nullptr || !binding->type->queue
            || call.operands.size() != 1U) {
            report(
                "FSIM-ELAB-SVCONTAINER-019",
                "pop_front/pop_back require a direct queue receiver",
                span);
            return invalid_result();
        }
        if (binding->read_only) {
            report(
                "FSIM-ELAB-SVPORT-009",
                "an input container port is read-only within its module",
                span);
            return invalid_result();
        }
        const auto destination = allocate_register(
            binding->type->element_width,
            binding->type->two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(PopContainer {
            destination, source->value, call.text == ".pop_front" });
        if (!binding->local) {
            process_.operations.emplace_back(WriteContainerObject {
                binding->object, source->value, std::nullopt });
        }
        return expected_width != 0U
                && expected_width != binding->type->element_width
            ? std::optional { resize_register(
                  destination,
                  expected_width,
                  binding->type->signed_elements) }
            : std::optional { destination };
    }
    if (call.operands.size() != (size_query ? 1U : 2U)) {
        return invalid_result();
    }
    if (!source->type.associative) {
        report(
            "FSIM-ELAB-SVCONTAINER-015",
            call.text + " requires an associative-array receiver",
            span);
        return invalid_result();
    }
    const bool string_index = source->type.string_indices;
    const auto index_id = call.operands[1];
    const auto index = string_index
        ? lower_hir_string_expression(index_id)
        : lower_hir_expression(index_id, source->type.index_width);
    if (!index) {
        return invalid_result();
    }
    const auto destination = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    if (call.text == ".exists") {
        auto converted = *index;
        if (!string_index
            && register_width(converted) != source->type.index_width) {
            converted = resize_register(
                converted,
                source->type.index_width,
                source->type.signed_indices);
        }
        process_.operations.emplace_back(ContainerExists {
            destination, source->value, converted, string_index });
        return expected_width != 0U && expected_width != 32U
            ? std::optional {
                  resize_register(destination, expected_width, false)
              }
            : std::optional { destination };
    }
    const auto declaration = hir_referenced_declaration(index_id);
    if (string_index) {
        const auto binding = declaration
            ? hir_string_binding(*declaration, hir_process_scope_, false)
            : std::nullopt;
        if (!binding) {
            report(
                "FSIM-ELAB-SVCONTAINER-016",
                call.text
                    + " requires a direct mutable string variable argument",
                span);
            return invalid_result();
        }
        auto traversal = ContainerTraversal::first;
        if (call.text == ".last") {
            traversal = ContainerTraversal::last;
        } else if (call.text == ".next") {
            traversal = ContainerTraversal::next;
        } else if (call.text == ".prev") {
            traversal = ContainerTraversal::previous;
        }
        process_.operations.emplace_back(TraverseContainer {
            destination, source->value, *index, traversal, true });
        if (binding->kind == HirStringBindingKind::object
            && binding->object) {
            process_.operations.emplace_back(WriteStringObject {
                *binding->object, *index });
        }
    } else {
        const auto binding = declaration
            ? hir_runtime_binding(*declaration, hir_process_scope_, false)
            : std::nullopt;
        if (!binding || register_width(*index) != source->type.index_width) {
            report(
                binding ? "FSIM-ELAB-SVCONTAINER-017"
                        : "FSIM-ELAB-SVCONTAINER-016",
                binding
                    ? call.text
                        + " argument must match the associative-array index "
                          "width"
                    : call.text
                        + " requires a direct mutable integral variable "
                          "argument",
                span);
            return invalid_result();
        }
        auto traversal = ContainerTraversal::first;
        if (call.text == ".last") {
            traversal = ContainerTraversal::last;
        } else if (call.text == ".next") {
            traversal = ContainerTraversal::next;
        } else if (call.text == ".prev") {
            traversal = ContainerTraversal::previous;
        }
        process_.operations.emplace_back(TraverseContainer {
            destination, source->value, *index, traversal, false });
        if (binding->kind == HirRuntimeBindingKind::signal
            && binding->signal) {
            process_.operations.emplace_back(WriteBlocking {
                *binding->signal, *index });
        }
    }
    return expected_width != 0U && expected_width != 32U
        ? std::optional { resize_register(destination, expected_width, false) }
        : std::optional { destination };
}

bool Lowerer::lower_hir_container_locator_assignment(
    const semantic::ExpressionId target_id,
    const semantic::ExpressionId value_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(value_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call
        || !is_container_locator(expression->systemverilog->text)) {
        return false;
    }
    const auto& call = *expression->systemverilog;
    const bool predicate = is_predicate_locator(call.text);
    const auto span = hir_source_span(call.source);
    const auto target = hir_container_object_binding(target_id);
    if (!target || target->type == nullptr) {
        report(
            predicate ? "FSIM-ELAB-SVFIND-006"
                      : "FSIM-ELAB-SVLOCATOR-004",
            "container locator results require a compatible whole-queue "
            "assignment target",
            span);
        return true;
    }
    if (target->read_only) {
        report(
            "FSIM-ELAB-SVPORT-009",
            "an input container port is read-only within its module",
            span);
        return true;
    }
    if ((!predicate && (call.operands.empty() || call.operands.size() > 3U))
        || (predicate && call.operands.size() != 2U
            && call.operands.size() != 3U)) {
        report(
            predicate ? "FSIM-ELAB-SVFIND-002"
                      : "FSIM-ELAB-SVLOCATOR-002",
            predicate
                ? "predicate container locator methods require exactly one "
                  "with-clause predicate"
                : "bounded container locator methods take no value arguments "
                  "and retain at most one iterator plus one with-clause "
                  "transformation",
            span);
        return true;
    }
    const auto source = lower_hir_static_container_value(
        call.operands.front());
    if (!source || source->type.associative) {
        report(
            predicate ? "FSIM-ELAB-SVFIND-001"
                      : "FSIM-ELAB-SVLOCATOR-001",
            "bounded container locators do not support associative or "
            "unresolved receivers",
            span);
        return true;
    }
    if (source->type.element_kind != ContainerElementKind::Packed
        && source->type.element_kind != ContainerElementKind::Scalar) {
        report(
            predicate ? "FSIM-ELAB-SVFIND-008"
                      : "FSIM-ELAB-SVLOCATOR-008",
            "container locators require packed integral elements",
            span);
        return true;
    }
    const bool index_result = call.text == ".unique_index"
        || call.text == ".find_index"
        || call.text == ".find_first_index"
        || call.text == ".find_last_index";
    const bool compatible = target->type->queue
        && !target->type->associative && !target->type->fixed
        && (index_result
                ? target->type->element_width == 32U
                    && target->type->two_state
                    && target->type->signed_elements
                : target->type->element_width == source->type.element_width
                    && target->type->two_state == source->type.two_state
                    && target->type->signed_elements
                        == source->type.signed_elements);
    if (!compatible) {
        report(
            predicate ? "FSIM-ELAB-SVFIND-003"
                      : "FSIM-ELAB-SVLOCATOR-003",
            "container locator result requires a compatible queue target",
            span);
        return true;
    }
    const bool explicit_iterator = call.operands.size() == 3U;
    std::string iterator_name { "item" };
    if (explicit_iterator) {
        const auto iterator = specialized_hir_unit_->find_expression(
            call.operands[1]);
        if (!iterator || iterator->systemverilog == nullptr
            || iterator->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            report(
                predicate ? "FSIM-ELAB-SVFIND-007"
                          : "FSIM-ELAB-SVLOCATOR-008",
                "a named container locator iterator must be one identifier",
                span);
            return true;
        }
        iterator_name = iterator->systemverilog->text;
        if (hir_referenced_declaration(call.operands[1])) {
            report(
                predicate ? "FSIM-ELAB-SVFIND-007"
                          : "FSIM-ELAB-SVLOCATOR-008",
                "named container locator iterator '" + iterator_name
                    + "' collides with a visible object",
                hir_source_span(iterator->systemverilog->source));
            return true;
        }
    }
    std::vector<ContainerPredicateNode> predicate_graph;
    std::vector<ContainerPredicateNode> transformation;
    if (predicate) {
        const auto lowered = lower_hir_container_expression_graph(
            call.operands[explicit_iterator ? 2U : 1U],
            iterator_name,
            source->type,
            HirContainerExpressionPurpose::locator_predicate);
        if (!lowered) {
            return true;
        }
        predicate_graph = std::move(*lowered);
    } else if (call.operands.size() >= 2U) {
        const auto lowered = lower_hir_container_expression_graph(
            call.operands[explicit_iterator ? 2U : 1U],
            iterator_name,
            source->type,
            HirContainerExpressionPurpose::locator_transformation);
        if (!lowered) {
            return true;
        }
        transformation = std::move(*lowered);
    }
    auto operation = ContainerLocatorOperator::minimum;
    if (call.text == ".max") {
        operation = ContainerLocatorOperator::maximum;
    } else if (call.text == ".unique") {
        operation = ContainerLocatorOperator::unique;
    } else if (call.text == ".unique_index") {
        operation = ContainerLocatorOperator::unique_index;
    } else if (call.text == ".find") {
        operation = ContainerLocatorOperator::find;
    } else if (call.text == ".find_index") {
        operation = ContainerLocatorOperator::find_index;
    } else if (call.text == ".find_first") {
        operation = ContainerLocatorOperator::find_first;
    } else if (call.text == ".find_first_index") {
        operation = ContainerLocatorOperator::find_first_index;
    } else if (call.text == ".find_last") {
        operation = ContainerLocatorOperator::find_last;
    } else if (call.text == ".find_last_index") {
        operation = ContainerLocatorOperator::find_last_index;
    }
    const auto destination = target->local
        ? *target->local
        : allocate_container_register(*target->type);
    process_.operations.emplace_back(LocateContainer {
        operation,
        destination,
        source->value,
        std::move(predicate_graph),
        std::move(transformation),
    });
    if (!target->local) {
        process_.operations.emplace_back(WriteContainerObject {
            target->object, destination, std::nullopt });
    }
    return true;
}

std::optional<semantic::sv::TypeReference>
Lowerer::hir_systemverilog_value_type(
    const semantic::ExpressionId expression_id) const
{
    // The declared type of a variable, of a member of a structure variable
    // (`s.a.b`), or of an element of an unpacked array (`a[i]`).
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const auto resolver = hir_resolver();
    if (source.kind == semantic::sv::ExpressionKind::index
        && source.operands.size() == 2U && source.text == "index") {
        auto element = hir_systemverilog_value_type(source.operands.front());
        if (!element || element->unpacked_dimensions.empty()) {
            return std::nullopt;
        }
        element->unpacked_dimensions.erase(
            element->unpacked_dimensions.begin());
        if (element->unpacked_dimensions.empty()) {
            element->container_form.reset();
            element->container_element_types.clear();
        }
        return element;
    }
    if (source.kind != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    const auto name_separator = source.text.find('.');
    auto declaration_id = hir_referenced_declaration(expression_id);
    if (!declaration_id && name_separator != std::string::npos) {
        declaration_id = resolver.resolve_systemverilog(
            std::string_view { source.text }.substr(0U, name_separator),
            source.scope,
            [](const semantic::CompiledDeclarationView& candidate) {
                return candidate.systemverilog != nullptr
                    && candidate.systemverilog->type.has_value();
            },
            false).unique();
    }
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->type) {
        return std::nullopt;
    }
    auto current = *declaration->systemverilog->type;
    if (name_separator == std::string::npos) {
        return current;
    }
    if (source.text.substr(0U, name_separator)
        != declaration->systemverilog->name) {
        return std::nullopt;
    }
    auto remaining = std::string_view { source.text }.substr(
        name_separator + 1U);
    while (!remaining.empty()) {
        if (!current.target.target.valid()) {
            current = resolver.effective_systemverilog_type(
                current, declaration->systemverilog->scope)
                          .value_or(current);
        }
        auto definition = current.target.target.valid()
            ? specialized_hir_unit_->find_type(current.target.target)
            : std::nullopt;
        std::unordered_set<std::uint32_t> aliases;
        while (definition && definition->systemverilog != nullptr
            && definition->systemverilog->form
                == semantic::sv::TypeForm::alias
            && definition->systemverilog->base.target.target.valid()
            && aliases.insert(definition->systemverilog->base.target
                                  .target.value()).second) {
            definition = specialized_hir_unit_->find_type(
                definition->systemverilog->base.target.target);
        }
        if (!definition || definition->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto separator = remaining.find('.');
        const auto segment = remaining.substr(0U, separator);
        const auto member = std::ranges::find(
            definition->systemverilog->members, segment,
            &semantic::sv::PackedMember::name);
        if (member == definition->systemverilog->members.end()) {
            return std::nullopt;
        }
        current = member->type;
        if (separator == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(separator + 1U);
    }
    return current;
}

std::optional<Lowerer::HirEnumerationMethod>
Lowerer::hir_systemverilog_enumeration_method(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const auto method_name = [](const std::string_view text) {
        return text == "first" || text == "last" || text == "next"
            || text == "prev" || text == "num" || text == "name";
    };
    HirEnumerationMethod result;
    std::optional<semantic::DeclarationId> declaration_id;
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text.size() > 1U && source.text.front() == '.'
        && method_name(std::string_view { source.text }.substr(1U))
        && !source.operands.empty() && source.operands.size() <= 2U) {
        result.method = source.text.substr(1U);
        result.receiver = source.operands.front();
        if (source.operands.size() == 2U) {
            if (result.method != "next" && result.method != "prev") {
                return std::nullopt;
            }
            result.count = source.operands[1];
        }
        const auto receiver = specialized_hir_unit_->find_expression(
            source.operands.front());
        // An element of an unpacked array of enumerations (`a[i].name()`).
        if (receiver && receiver->systemverilog != nullptr
            && receiver->systemverilog->kind
                == semantic::sv::ExpressionKind::index) {
            if (const auto type = hir_systemverilog_value_type(
                    source.operands.front())) {
                return hir_systemverilog_enumeration_type_profile(
                    std::move(result), *type, receiver->systemverilog->scope);
            }
        }
        // A chained call (`e.next(1).next(1)`) has the enumeration type of
        // the inner method's receiver.
        if (receiver && receiver->systemverilog != nullptr
            && receiver->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            const auto inner = hir_systemverilog_enumeration_method(
                source.operands.front());
            if (!inner || inner->method == "name" || inner->method == "num") {
                return std::nullopt;
            }
            return hir_systemverilog_enumeration_profile(
                std::move(result), inner->receiver_declaration);
        }
        if (!receiver || receiver->systemverilog == nullptr) {
            return std::nullopt;
        }
        if (receiver->systemverilog->text.find('.') != std::string::npos) {
            // An enumeration member of a structure (`s.m.name()`, IEEE
            // 1800-2017 6.19.5, 7.2).
            if (const auto type = hir_systemverilog_value_type(
                    source.operands.front())) {
                const auto root = hir_referenced_declaration(
                    source.operands.front());
                if (root) {
                    result.receiver_declaration = *root;
                }
                return hir_systemverilog_enumeration_type_profile(
                    std::move(result), *type, receiver->systemverilog->scope);
            }
            return std::nullopt;
        }
        declaration_id = hir_referenced_declaration(source.operands.front());
    } else if (source.kind == semantic::sv::ExpressionKind::name) {
        const auto separator = source.text.rfind('.');
        if (separator == std::string::npos
            || !method_name(
                std::string_view { source.text }.substr(separator + 1U))) {
            return std::nullopt;
        }
        result.method = source.text.substr(separator + 1U);
        declaration_id = hir_referenced_declaration(expression_id);
        if (!declaration_id) {
            const semantic::CompiledDeclarationPredicate object
                = [](const semantic::CompiledDeclarationView& candidate) {
                      return candidate.systemverilog != nullptr
                          && candidate.systemverilog->type.has_value()
                          && (candidate.systemverilog->form
                                  == semantic::sv::DeclarationForm::variable
                              || candidate.systemverilog->form
                                  == semantic::sv::DeclarationForm::net
                              || candidate.systemverilog->form
                                  == semantic::sv::DeclarationForm::port);
                  };
            declaration_id = hir_resolver().resolve_systemverilog(
                 std::string_view { source.text }.substr(0U, separator),
                 source.scope, object, false)
                                 .unique();
        }
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        // Only a direct `variable.method`, or `P::variable.method` for a
        // package variable; a member path is not a method.
        auto root = std::string_view { source.text }.substr(0U, separator);
        if (const auto scope = root.rfind("::");
            scope != std::string_view::npos) {
            root.remove_prefix(scope + 2U);
        }
        if (!declaration || declaration->systemverilog == nullptr
            || declaration->systemverilog->name != root) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    if (!declaration_id) {
        return std::nullopt;
    }
    return hir_systemverilog_enumeration_profile(
        std::move(result), *declaration_id);
}

std::optional<Lowerer::HirEnumerationMethod>
Lowerer::hir_systemverilog_enumeration_profile(
    HirEnumerationMethod result,
    const semantic::DeclarationId declaration_id) const
{
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->type
        || declaration->systemverilog->type->container_form) {
        return std::nullopt;
    }
    result.receiver_declaration = declaration_id;
    return hir_systemverilog_enumeration_type_profile(std::move(result),
        *declaration->systemverilog->type, declaration->systemverilog->scope);
}

std::optional<Lowerer::HirEnumerationMethod>
Lowerer::hir_systemverilog_enumeration_type_profile(
    HirEnumerationMethod result,
    semantic::sv::TypeReference type,
    const semantic::ScopeId scope) const
{
    if (!type.target.target.valid()) {
        type = hir_resolver().effective_systemverilog_type(type, scope)
                   .value_or(type);
    }
    if (!type.target.target.valid()) {
        return std::nullopt;
    }
    auto definition = specialized_hir_unit_->find_type(type.target.target);
    std::unordered_set<std::uint32_t> aliases;
    while (definition && definition->systemverilog != nullptr
        && definition->systemverilog->form == semantic::sv::TypeForm::alias
        && definition->systemverilog->base.target.target.valid()
        && aliases.insert(
                      definition->systemverilog->base.target.target.value())
               .second) {
        definition = specialized_hir_unit_->find_type(
            definition->systemverilog->base.target.target);
    }
    if (!definition || definition->systemverilog == nullptr
        || definition->systemverilog->form
            != semantic::sv::TypeForm::enumeration
        || definition->systemverilog->enumeration_literals.empty()) {
        return std::nullopt;
    }
    const auto width = hir_systemverilog_type_width(type);
    if (!width || *width == 0U || *width > 64U) {
        return std::nullopt;
    }
    result.width = *width;
    result.four_state = hir_systemverilog_type_four_state(type);
    result.signed_value = type.signed_value;
    std::optional<std::int64_t> previous;
    for (const auto& literal :
        definition->systemverilog->enumeration_literals) {
        std::optional<std::int64_t> value;
        if (literal.value) {
            value = specialized_hir_unit_->evaluate_integral_expression(
                *literal.value);
        } else {
            value = previous ? std::optional { *previous + 1 }
                             : std::optional<std::int64_t> { 0 };
        }
        if (!value) {
            return std::nullopt;
        }
        previous = value;
        const auto mask = result.width == 64U
            ? ~std::uint64_t { 0 }
            : (std::uint64_t { 1 } << result.width) - 1U;
        result.values.push_back(static_cast<std::uint64_t>(*value) & mask);
        result.names.push_back(literal.name);
    }
    return result;
}

std::optional<RegisterId>
Lowerer::lower_hir_systemverilog_enumeration_receiver(
    const HirEnumerationMethod& method)
{
    std::optional<RegisterId> value;
    if (method.receiver) {
        value = lower_hir_expression(*method.receiver, method.width);
    } else {
        const auto binding = hir_runtime_binding(
            method.receiver_declaration, hir_process_scope_, false);
        if (!binding || (!binding->local && !binding->signal)) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            binding->width, binding->domain);
        if (binding->local) {
            process_.operations.emplace_back(
                CopyRegister { destination, *binding->local });
        } else {
            process_.operations.emplace_back(ReadSignal {
                destination,
                *binding->signal,
                sample_concurrent_assertion_reads_
                    ? SignalReadKind::sampled
                    : SignalReadKind::current,
            });
            record_implicit_signal_dependency(*binding->signal);
        }
        value = destination;
    }
    if (value && register_width(*value) != method.width) {
        value = resize_register(*value, method.width, false);
    }
    return value;
}

std::optional<RegisterId> Lowerer::lower_hir_systemverilog_enumeration_method(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto method = hir_systemverilog_enumeration_method(expression_id);
    if (!method || method->method == "name") {
        return std::nullopt;
    }
    const auto finish = [&](RegisterId value) {
        if (expected_width != 0U && register_width(value) != expected_width) {
            value = resize_register(value, expected_width,
                method->method == "num" || method->signed_value);
        }
        return std::optional { value };
    };
    const auto constant = [&](const std::uint64_t value,
                              const std::size_t width) {
        const auto destination = allocate_register(width,
            method->four_state ? frontend::ValueDomain::Logic4
                               : frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { destination, unsigned_value(value, width) });
        return destination;
    };
    // num() is an int; first() and last() are the declared extremes
    // (IEEE 1800-2017 6.19.5.1, 6.19.5.2, 6.19.5.5).
    if (method->method == "num") {
        const auto destination
            = load_constant(unsigned_value(method->values.size(), 32U),
                frontend::ValueDomain::Bit2);
        return finish(destination);
    }
    if (method->method == "first" || method->method == "last") {
        return finish(constant(method->method == "first"
                ? method->values.front()
                : method->values.back(),
            method->width));
    }
    std::int64_t steps = 1;
    if (method->count) {
        const auto count = hir_constant_integer(*method->count);
        if (!count) {
            const auto expression = specialized_hir_unit_->find_expression(
                *method->count);
            report("FSIM-ELAB-SVENUM-003",
                "an enumeration next() or prev() count must be an "
                "elaboration-time constant",
                hir_source_span(expression && expression->systemverilog
                        ? expression->systemverilog->source
                        : semantic::SourceSpanId { }));
            return std::nullopt;
        }
        steps = *count;
    }
    const auto receiver = lower_hir_systemverilog_enumeration_receiver(
        *method);
    if (!receiver) {
        return std::nullopt;
    }
    // next(N) and prev(N) wrap around the declaration order; a value that
    // is not a member yields the unknown value of the base type.
    const auto count = static_cast<std::int64_t>(method->values.size());
    const auto offset = method->method == "next" ? steps : -steps;
    const auto domain = method->four_state ? frontend::ValueDomain::Logic4
                                           : frontend::ValueDomain::Bit2;
    auto result = allocate_register(method->width, domain);
    process_.operations.emplace_back(LoadConstant { result,
        method->four_state ? PackedLogic4(method->width, Logic4::x)
                           : unsigned_value(0U, method->width) });
    for (std::int64_t index { }; index < count; ++index) {
        const auto literal = load_constant(
            unsigned_value(
                method->values[static_cast<std::size_t>(index)], method->width),
            register_domain(*receiver));
        const auto matches = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, matches, *receiver, literal });
        const auto target = ((index + offset) % count + count) % count;
        const auto selected = constant(
            method->values[static_cast<std::size_t>(target)], method->width);
        const auto next = allocate_register(method->width, domain);
        process_.operations.emplace_back(
            ConditionalSelect { next, matches, selected, result });
        result = next;
    }
    return finish(result);
}

std::optional<StringRegisterId>
Lowerer::lower_hir_systemverilog_enumeration_name(
    const semantic::ExpressionId expression_id)
{
    const auto method = hir_systemverilog_enumeration_method(expression_id);
    if (!method || method->method != "name") {
        return std::nullopt;
    }
    return lower_hir_systemverilog_enumeration_name(*method);
}

std::optional<StringRegisterId>
Lowerer::lower_hir_systemverilog_enumeration_value_name(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    const auto declaration = expression && expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name
        ? hir_referenced_declaration(expression_id)
        : std::nullopt;
    if (!declaration) {
        return std::nullopt;
    }
    HirEnumerationMethod name;
    name.method = "name";
    name.receiver = expression_id;
    const auto profile = hir_systemverilog_enumeration_profile(
        std::move(name), *declaration);
    return profile ? lower_hir_systemverilog_enumeration_name(*profile)
                   : std::nullopt;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_systemverilog_enumeration_name(
    const HirEnumerationMethod& method_profile)
{
    const auto* const method = &method_profile;
    const auto receiver = lower_hir_systemverilog_enumeration_receiver(
        *method);
    if (!receiver) {
        return std::nullopt;
    }
    // name() is the literal's name, or "" for a value that is not a member
    // (IEEE 1800-2017 6.19.5.6).
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(
        LoadStringConstant { destination, std::string { } });
    std::vector<std::size_t> exits;
    for (std::size_t index { }; index < method->values.size(); ++index) {
        const auto literal = load_constant(
            unsigned_value(method->values[index], method->width),
            register_domain(*receiver));
        const auto matches = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, matches, *receiver, literal });
        const auto branch = process_.operations.size();
        process_.operations.emplace_back(Branch {
            matches, 0U, 0U, UnknownBranchPolicy::error });
        const auto when_true = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(
            LoadStringConstant { destination, method->names[index] });
        exits.push_back(process_.operations.size());
        process_.operations.emplace_back(Jump { 0U });
        process_.operations[branch] = Branch { matches, when_true,
            static_cast<InstructionIndex>(process_.operations.size()),
            UnknownBranchPolicy::error };
    }
    const auto end = static_cast<InstructionIndex>(process_.operations.size());
    for (const auto exit : exits) {
        process_.operations[exit] = Jump { end };
    }
    return destination;
}

} // namespace fsim::elaboration
