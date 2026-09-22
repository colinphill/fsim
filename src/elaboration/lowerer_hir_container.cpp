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
    const auto contains_iterator = [&](const semantic::ExpressionId candidate) {
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
            if (source.kind == semantic::sv::ExpressionKind::name
                && (source.text == iterator_name
                    || source.text.starts_with(
                        std::string { iterator_name } + "."))) {
                return true;
            }
            pending.insert(
                pending.end(), source.operands.begin(), source.operands.end());
        }
        return false;
    };
    const auto contains_iterator_index
        = [&](const semantic::ExpressionId candidate) {
        std::vector<semantic::ExpressionId> pending { candidate };
        std::unordered_set<std::uint32_t> visited;
        const auto index_name
            = std::string { iterator_name } + ".index";
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
            if ((source.kind == semantic::sv::ExpressionKind::name
                    && (source.text == index_name
                        || source.text.starts_with(index_name + ".")))
                || (source.kind == semantic::sv::ExpressionKind::call
                    && source.text == ".index")) {
                return true;
            }
            pending.insert(
                pending.end(), source.operands.begin(), source.operands.end());
        }
        return false;
    };
    const auto contains_unknown_index = [&](const semantic::ExpressionId candidate) {
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
            if (source.kind == semantic::sv::ExpressionKind::name
                && source.text.find(".index") != std::string::npos
                && source.text != std::string { iterator_name } + ".index") {
                return true;
            }
            pending.insert(
                pending.end(), source.operands.begin(), source.operands.end());
        }
        return false;
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
        const auto destination = allocate_register(
            width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(0U, width) });
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
    const bool query = call.text == ".size" || call.text == ".exists"
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
    if (pop
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
    if (call.text == ".size") {
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
    if (call.operands.size() != (call.text == ".size" ? 1U : 2U)) {
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

} // namespace fsim::elaboration
