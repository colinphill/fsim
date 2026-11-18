// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_constant_evaluator.hpp"
#include "fsim/support/stack_growth.hpp"
#include "fsim/frontend/systemverilog_standard_package.hpp"
#include "lowerer_internal.hpp"
#include "fsim/semantic/systemverilog_structure_image.hpp"

namespace fsim::elaboration {
namespace {

    using support::BuiltinMethod;

    bool is_container_reduction(const BuiltinMethod method) noexcept
    {
        return method == BuiltinMethod::sum || method == BuiltinMethod::product
            || method == BuiltinMethod::reduce_and
            || method == BuiltinMethod::reduce_or
            || method == BuiltinMethod::reduce_xor;
    }

    bool is_container_locator(const BuiltinMethod method) noexcept
    {
        return method == BuiltinMethod::min || method == BuiltinMethod::max
            || method == BuiltinMethod::unique
            || method == BuiltinMethod::unique_index
            || method == BuiltinMethod::find
            || method == BuiltinMethod::find_index
            || method == BuiltinMethod::find_first
            || method == BuiltinMethod::find_first_index
            || method == BuiltinMethod::find_last
            || method == BuiltinMethod::find_last_index;
    }

    bool is_predicate_locator(const BuiltinMethod method) noexcept
    {
        return method == BuiltinMethod::find
            || method == BuiltinMethod::find_index
            || method == BuiltinMethod::find_first
            || method == BuiltinMethod::find_first_index
            || method == BuiltinMethod::find_last
            || method == BuiltinMethod::find_last_index;
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
        if (semantic::sv::is_iterator_index_name(
                expression, iterator_name, true)) {
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
                return semantic::sv::is_iterator_index_name(
                           source, iterator_name, false)
                    || (source.kind == semantic::sv::ExpressionKind::call
                        && source.builtin_method
                            == support::BuiltinMethod::index);
            });
    };
    const auto contains_unknown_index
        = [&](const semantic::ExpressionId candidate) {
        return any_expression(
            candidate, [&](const semantic::sv::Expression& source) {
                return source.kind == semantic::sv::ExpressionKind::name
                    && source.builtin_method == support::BuiltinMethod::index
                    && !semantic::sv::is_iterator_index_name(
                        source, iterator_name, true);
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
    // `h.p.exists(k)` of a container property held by an object
    // (a container-method marker on the handle) queries a copy of the
    // property's container (IEEE 1800-2017 7.9, 7.10, 8.4).
    support::BuiltinMethod method = call.builtin_method;
    std::optional<HirStaticContainerValue> class_source;
    if ((call.marker == support::ExpressionMarker::sv_container_size
            || call.marker == support::ExpressionMarker::sv_container_method
            || call.marker == support::ExpressionMarker::sv_container_push_back)
        && !call.operands.empty()) {
        // The size query is marked by itself; any other method is marked as
        // a container method that carries its built-in method.
        if (call.marker == support::ExpressionMarker::sv_container_size) {
            method = support::BuiltinMethod::size;
        }
        if (method != support::BuiltinMethod::size
            && method != support::BuiltinMethod::num
            && method != support::BuiltinMethod::exists
            && method != support::BuiltinMethod::first
            && method != support::BuiltinMethod::last
            && method != support::BuiltinMethod::next
            && method != support::BuiltinMethod::prev) {
            return std::nullopt;
        }
        const std::string_view identity { call.marker_argument };
        const auto held = hir_class_value_container_type(identity)
            ? lower_hir_class_value_container(call.operands.front(), identity)
            : lower_hir_class_handle_container(
                  call.operands.front(), identity);
        if (!held) {
            return std::nullopt;
        }
        class_source = held->container;
    }
    const auto invalid_result = [&]() {
        const auto width = expected_width == 0U ? 32U : expected_width;
        const auto destination = load_constant(
            unsigned_value(0U, width), frontend::ValueDomain::Bit2);
        return std::optional<RegisterId> { destination };
    };
    // The method's name with its dot, for diagnostics only.
    const std::string text
        = "." + std::string { support::builtin_method_name(method) };
    const bool reduction = is_container_reduction(method);
    const bool locator = is_container_locator(method);
    const bool ordering = method == support::BuiltinMethod::reverse
        || method == support::BuiltinMethod::sort
        || method == support::BuiltinMethod::rsort
        || method == support::BuiltinMethod::shuffle;
    if (ordering) {
        report(
            "FSIM-ELAB-SVORDER-004",
            "container ordering methods do not produce an expression result",
            span);
        return invalid_result();
    }
    if (locator) {
        report(
            is_predicate_locator(method)
                ? "FSIM-ELAB-SVFIND-006"
                : "FSIM-ELAB-SVLOCATOR-004",
            "container locator results require a compatible whole-queue "
            "assignment target",
            span);
        return invalid_result();
    }
    // num() is size() for an associative array (IEEE 1800-2017 7.9.1).
    const bool size_query = method == support::BuiltinMethod::size
        || method == support::BuiltinMethod::num;
    const bool query = size_query || method == support::BuiltinMethod::exists
        || method == support::BuiltinMethod::first
        || method == support::BuiltinMethod::last
        || method == support::BuiltinMethod::next
        || method == support::BuiltinMethod::prev;
    const bool pop = method == support::BuiltinMethod::pop_front
        || method == support::BuiltinMethod::pop_back;
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
    const auto source = class_source
        ? class_source
        : lower_hir_static_container_value(call.operands.front());
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
            // The iterator argument names the iterator inside the with
            // clause, hiding a visible object of that name (7.12.4).
            iterator_name = iterator->systemverilog->text;
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
        // Over a multidimensional array the iterator is a row (7.12.3).
        if (transformed && source->type.fixed
            && source->type.dimensions.size() > 1U) {
            const auto transform_id = call.operands[explicit_iterator ? 2U : 1U];
            const auto reduced = lower_hir_container_reduction_loop(
                method, source->value, source->type, iterator_name,
                transform_id);
            if (!reduced) {
                report(
                    "FSIM-ELAB-SVREDUCE-004",
                    "a reduction transformation over array rows must be an "
                    "integral expression",
                    span);
                return invalid_result();
            }
            return expected_width != 0U
                    && expected_width != register_width(*reduced)
                ? std::optional { resize_register(*reduced, expected_width,
                      hir_expression_signed(transform_id)) }
                : reduced;
        }
        if (transformed) {
            const auto transform_id = call.operands[explicit_iterator ? 2U : 1U];
            const auto diagnostic_count = diagnostics_.size();
            const auto lowered = lower_hir_container_expression_graph(
                transform_id,
                iterator_name,
                source->type,
                HirContainerExpressionPurpose::reduction_transformation);
            if (!lowered) {
                // A transformation beyond the bounded graph runs as an
                // element loop; its result has the with expression's type
                // (7.12.3).
                const auto graph_diagnostics = std::vector<Diagnostic>(
                    diagnostics_.begin()
                        + static_cast<std::ptrdiff_t>(diagnostic_count),
                    diagnostics_.end());
                diagnostics_.resize(diagnostic_count);
                if (const auto reduced = lower_hir_container_reduction_loop(
                        method, source->value, source->type,
                        iterator_name, transform_id)) {
                    return expected_width != 0U
                            && expected_width != register_width(*reduced)
                        ? std::optional { resize_register(*reduced,
                              expected_width,
                              hir_expression_signed(transform_id)) }
                        : reduced;
                }
                diagnostics_.resize(diagnostic_count);
                diagnostics_.insert(diagnostics_.end(),
                    graph_diagnostics.begin(), graph_diagnostics.end());
                return invalid_result();
            }
            transformation = std::move(*lowered);
        }
        auto operation = ContainerReductionOperator::sum;
        if (method == support::BuiltinMethod::product) {
            operation = ContainerReductionOperator::product;
        } else if (method == support::BuiltinMethod::reduce_and) {
            operation = ContainerReductionOperator::bit_and;
        } else if (method == support::BuiltinMethod::reduce_or) {
            operation = ContainerReductionOperator::bit_or;
        } else if (method == support::BuiltinMethod::reduce_xor) {
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
            destination, source->value,
            method == support::BuiltinMethod::pop_front });
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
            destination, source->value,
            method == support::BuiltinMethod::pop_front });
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
            text + " requires an associative-array receiver",
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
    if (method == support::BuiltinMethod::exists) {
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
                text
                    + " requires a direct mutable string variable argument",
                span);
            return invalid_result();
        }
        auto traversal = ContainerTraversal::first;
        if (method == support::BuiltinMethod::last) {
            traversal = ContainerTraversal::last;
        } else if (method == support::BuiltinMethod::next) {
            traversal = ContainerTraversal::next;
        } else if (method == support::BuiltinMethod::prev) {
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
                    ? text
                        + " argument must match the associative-array index "
                          "width"
                    : text
                        + " requires a direct mutable integral variable "
                          "argument",
                span);
            return invalid_result();
        }
        auto traversal = ContainerTraversal::first;
        if (method == support::BuiltinMethod::last) {
            traversal = ContainerTraversal::last;
        } else if (method == support::BuiltinMethod::next) {
            traversal = ContainerTraversal::next;
        } else if (method == support::BuiltinMethod::prev) {
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
    const semantic::ExpressionId value_id,
    const HirContainerObjectBinding* const declared_target)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(value_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call
        || !is_container_locator(semantic::sv::member_builtin_method(*expression->systemverilog))) {
        return false;
    }
    const auto& call = *expression->systemverilog;
    const bool predicate = is_predicate_locator(semantic::sv::member_builtin_method(call));
    const auto span = hir_source_span(call.source);
    const auto target = declared_target != nullptr
        ? std::optional { *declared_target }
        : hir_container_object_binding(target_id);
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
    // Every locator selects string elements as well (7.12.1).
    const bool string_elements
        = source->type.element_kind == ContainerElementKind::String;
    if (source->type.element_kind != ContainerElementKind::Packed
        && source->type.element_kind != ContainerElementKind::Scalar
        && !string_elements) {
        report(
            predicate ? "FSIM-ELAB-SVFIND-008"
                      : "FSIM-ELAB-SVLOCATOR-008",
            "container locators require packed integral elements",
            span);
        return true;
    }
    const bool index_result
        = semantic::sv::member_builtin_method(call) == support::BuiltinMethod::unique_index
        || semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_index
        || semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_first_index
        || semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_last_index;
    const bool compatible = target->type->queue
        && !target->type->associative && !target->type->fixed
        && (index_result
                ? target->type->element_width == 32U
                    && target->type->two_state
                    && target->type->signed_elements
                : string_elements
                ? target->type->element_kind == ContainerElementKind::String
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
        // The iterator argument names the iterator inside the with
        // clause, hiding a visible object of that name (7.12.4).
        iterator_name = iterator->systemverilog->text;
    }
    std::vector<ContainerPredicateNode> predicate_graph;
    std::vector<ContainerPredicateNode> transformation;
    if (predicate) {
        const auto predicate_id = call.operands[explicit_iterator ? 2U : 1U];
        const auto diagnostic_count = diagnostics_.size();
        const auto lowered = string_elements
            ? std::nullopt
            : lower_hir_container_expression_graph(
                predicate_id,
                iterator_name,
                source->type,
                HirContainerExpressionPurpose::locator_predicate);
        if (!lowered) {
            // A predicate beyond the bounded graph (a method call, a
            // select, a variable operand) runs as an element loop.
            const auto graph_diagnostics = std::vector<Diagnostic>(
                diagnostics_.begin()
                    + static_cast<std::ptrdiff_t>(diagnostic_count),
                diagnostics_.end());
            diagnostics_.resize(diagnostic_count);
            const auto destination = target->local
                ? *target->local
                : allocate_container_register(*target->type);
            if (lower_hir_container_locator_loop(semantic::sv::member_builtin_method(call), source->value,
                    source->type, destination, iterator_name,
                    predicate_id)) {
                if (!target->local) {
                    process_.operations.emplace_back(WriteContainerObject {
                        target->object, destination, std::nullopt });
                }
                return true;
            }
            diagnostics_.resize(diagnostic_count);
            diagnostics_.insert(diagnostics_.end(),
                graph_diagnostics.begin(), graph_diagnostics.end());
            return true;
        }
        predicate_graph = std::move(*lowered);
    } else {
        // min, max and unique compare the elements, or the values of the
        // with expression, with the relational operators of their type
        // (7.12.1). A string element or a with expression beyond the
        // bounded graph runs as an element loop.
        const bool transformed = call.operands.size() >= 2U;
        const auto key_id = transformed
            ? std::optional { call.operands[explicit_iterator ? 2U : 1U] }
            : std::nullopt;
        const auto diagnostic_count = diagnostics_.size();
        bool graph = !string_elements;
        if (graph && transformed) {
            auto lowered = lower_hir_container_expression_graph(
                *key_id,
                iterator_name,
                source->type,
                HirContainerExpressionPurpose::locator_transformation);
            if (lowered) {
                transformation = std::move(*lowered);
            } else {
                graph = false;
            }
        }
        if (!graph) {
            const auto graph_diagnostics = std::vector<Diagnostic>(
                diagnostics_.begin()
                    + static_cast<std::ptrdiff_t>(diagnostic_count),
                diagnostics_.end());
            diagnostics_.resize(diagnostic_count);
            const auto destination = target->local
                ? *target->local
                : allocate_container_register(*target->type);
            if (lower_hir_container_ordered_locator_loop(
                    semantic::sv::member_builtin_method(call), source->value,
                    source->type, destination, iterator_name, key_id)) {
                if (!target->local) {
                    process_.operations.emplace_back(WriteContainerObject {
                        target->object, destination, std::nullopt });
                }
                return true;
            }
            diagnostics_.resize(diagnostic_count);
            diagnostics_.insert(diagnostics_.end(),
                graph_diagnostics.begin(), graph_diagnostics.end());
            if (graph_diagnostics.empty()) {
                report(
                    "FSIM-ELAB-SVLOCATOR-006",
                    "container locator transformation cannot be lowered over "
                    "these elements",
                    span);
            }
            return true;
        }
    }
    auto operation = ContainerLocatorOperator::minimum;
    if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::max) {
        operation = ContainerLocatorOperator::maximum;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::unique) {
        operation = ContainerLocatorOperator::unique;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::unique_index) {
        operation = ContainerLocatorOperator::unique_index;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find) {
        operation = ContainerLocatorOperator::find;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_index) {
        operation = ContainerLocatorOperator::find_index;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_first) {
        operation = ContainerLocatorOperator::find_first;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_first_index) {
        operation = ContainerLocatorOperator::find_first_index;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_last) {
        operation = ContainerLocatorOperator::find_last;
    } else if (semantic::sv::member_builtin_method(call) == support::BuiltinMethod::find_last_index) {
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

bool Lowerer::lower_hir_container_locator_loop(
    const support::BuiltinMethod method,
    const runtime::simir::ContainerRegisterId source,
    const runtime::simir::ContainerType& source_type,
    const runtime::simir::ContainerRegisterId destination,
    const std::string_view iterator_name,
    const semantic::ExpressionId predicate)
{
    using runtime::simir::BinaryOperator;
    const bool index_result = method == support::BuiltinMethod::unique_index
        || method == support::BuiltinMethod::find_index
        || method == support::BuiltinMethod::find_first_index
        || method == support::BuiltinMethod::find_last_index;
    const bool first = method == support::BuiltinMethod::find_first
        || method == support::BuiltinMethod::find_first_index;
    const bool last = method == support::BuiltinMethod::find_last
        || method == support::BuiltinMethod::find_last_index;
    // The predicate's references to the iterator and to `iterator.index`.
    const auto references
        = hir_container_iterator_references(predicate, iterator_name);
    const auto element_domain = source_type.two_state
        ? frontend::ValueDomain::Bit2
        : frontend::ValueDomain::Logic4;
    process_.operations.emplace_back(DeleteContainer { destination, { } });
    const auto size = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerSize { size, source });
    const auto count = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant {
        count, PackedLogic4::from_aval_bval(32U, 0U, 0U) });
    const auto one = load_constant(
        PackedLogic4::from_aval_bval(32U, 1U, 0U),
        frontend::ValueDomain::Bit2);
    const auto loop = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto more = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::less_unsigned, more, count, size });
    const auto enter = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch { more, 0U, 0U });
    const auto body = static_cast<InstructionIndex>(
        process_.operations.size());
    auto index = count;
    if (last) {
        // The last match first: visit from the end (7.12.1).
        const auto from_end = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_unsigned, from_end, size, count });
        index = allocate_register(32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_unsigned, index, from_end, one });
    }
    const bool string_elements
        = source_type.element_kind == ContainerElementKind::String;
    std::optional<RegisterId> element;
    std::optional<StringRegisterId> text;
    if (string_elements) {
        text = allocate_string_register();
        process_.operations.emplace_back(ContainerStringRead {
            *text, source, index, true, false, false, { } });
    } else {
        element = allocate_register(
            source_type.element_width, element_domain);
        process_.operations.emplace_back(ContainerRead {
            *element, source, index, true, false, false });
    }
    for (const auto& [id, is_index] : references) {
        if (is_index) {
            hir_captured_update_indices_.insert_or_assign(id, index);
        } else if (text) {
            hir_captured_strings_.insert_or_assign(id, *text);
        } else {
            hir_captured_update_indices_.insert_or_assign(id, *element);
            if (source_type.signed_elements) {
                hir_captured_signed_.insert(id);
            }
        }
    }
    const auto width = hir_expression_width(predicate, hir_process_scope_)
                           .value_or(1U);
    auto truth = lower_hir_expression(predicate, width);
    for (const auto& reference : references) {
        hir_captured_update_indices_.erase(reference.first);
        hir_captured_signed_.erase(reference.first);
        hir_captured_strings_.erase(reference.first);
    }
    if (!truth) {
        return false;
    }
    if (register_width(*truth) != 1U) {
        const auto zero = allocate_register(1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalNot { zero, *truth });
        const auto nonzero
            = allocate_register(1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalNot { nonzero, zero });
        truth = nonzero;
    }
    const auto test = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch { *truth, 0U, 0U });
    const auto take = static_cast<InstructionIndex>(
        process_.operations.size());
    if (text && !index_result) {
        process_.operations.emplace_back(AppendContainer {
            destination, false, std::nullopt, std::nullopt, *text });
    } else {
        process_.operations.emplace_back(PushContainer {
            destination, index_result ? index : *element, false,
            std::nullopt });
    }
    std::optional<InstructionIndex> leave;
    if (first || last) {
        leave = static_cast<InstructionIndex>(process_.operations.size());
        process_.operations.emplace_back(Jump { 0U });
    }
    const auto next = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto advanced = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::add_unsigned, advanced, count, one });
    process_.operations.emplace_back(CopyRegister { count, advanced });
    process_.operations.emplace_back(Jump { loop });
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[enter] = Branch {
        more, body, end, UnknownBranchPolicy::when_false };
    process_.operations[test] = Branch {
        *truth, take, next, UnknownBranchPolicy::when_false };
    if (leave) {
        process_.operations[*leave] = Jump { end };
    }
    return true;
}

void Lowerer::hir_bind_container_iterator(
    const std::vector<std::pair<std::uint32_t, bool>>& references,
    const runtime::simir::ContainerType& type,
    const std::optional<RegisterId> element,
    const std::optional<StringRegisterId> text,
    const bool bind)
{
    for (const auto& reference : references) {
        const auto id = reference.first;
        if (!bind) {
            hir_captured_update_indices_.erase(id);
            hir_captured_signed_.erase(id);
            hir_captured_strings_.erase(id);
            hir_captured_scalar_kinds_.erase(id);
        } else if (text) {
            hir_captured_strings_.insert_or_assign(id, *text);
        } else if (element) {
            hir_captured_update_indices_.insert_or_assign(id, *element);
            if (type.signed_elements) {
                hir_captured_signed_.insert(id);
            }
            if (!reference.second
                && frontend::is_real_scalar_kind(type.scalar_kind)) {
                hir_captured_scalar_kinds_.insert_or_assign(
                    id, type.scalar_kind);
            }
        }
    }
}

std::optional<Lowerer::HirContainerKey> Lowerer::hir_container_key(
    const runtime::simir::ContainerType& type,
    const std::string_view iterator_name,
    const std::optional<semantic::ExpressionId> expression)
{
    HirContainerKey key;
    key.expression = expression;
    const bool text_elements
        = type.element_kind == ContainerElementKind::String;
    if (expression) {
        key.references = hir_container_iterator_references(
            *expression, iterator_name);
    }
    // Without a with clause, or with the iterator alone, the elements are
    // compared themselves (IEEE 1800-2023 7.12.1, 7.12.2).
    const bool iterator_alone = !expression
        || (key.references.size() == 1U
            && key.references.front().first == expression->value()
            && !key.references.front().second);
    if (iterator_alone) {
        key.references.clear();
        key.expression.reset();
        if (text_elements) {
            key.kind = HirContainerKey::Kind::text;
        } else if (frontend::is_real_scalar_kind(type.scalar_kind)) {
            key.kind = HirContainerKey::Kind::real;
            key.scalar_kind = type.scalar_kind;
        } else {
            key.width = type.element_width;
            key.is_signed = type.signed_elements;
        }
        return key;
    }
    // `item.index` names the element's original position, which a
    // comparison of two elements does not carry.
    if (std::ranges::any_of(key.references,
            [](const auto& reference) { return reference.second; })) {
        return std::nullopt;
    }
    std::optional<RegisterId> probe;
    std::optional<StringRegisterId> probe_text;
    if (text_elements) {
        probe_text = allocate_string_register();
    } else {
        probe = allocate_register(type.element_width,
            type.two_state ? frontend::ValueDomain::Bit2
                           : frontend::ValueDomain::Logic4);
    }
    hir_bind_container_iterator(key.references, type, probe, probe_text, true);
    bool valid = true;
    const auto scalar_kind = hir_systemverilog_scalar_kind(*expression);
    if (hir_expression_is_string(*expression, hir_process_scope_)) {
        key.kind = HirContainerKey::Kind::text;
    } else if (frontend::is_real_scalar_kind(scalar_kind)) {
        key.kind = HirContainerKey::Kind::real;
        key.scalar_kind = scalar_kind;
    } else {
        const auto width = hir_expression_width(
            *expression, hir_process_scope_);
        valid = width && *width != 0U;
        key.width = width.value_or(0U);
        key.is_signed = hir_expression_signed(*expression);
    }
    hir_bind_container_iterator(
        key.references, type, probe, probe_text, false);
    if (!valid) {
        return std::nullopt;
    }
    return key;
}

std::optional<Lowerer::HirContainerKeyValue>
Lowerer::lower_hir_container_key_value(
    const HirContainerKey& key,
    const runtime::simir::ContainerType& type,
    const std::optional<RegisterId> element,
    const std::optional<StringRegisterId> text)
{
    if (!key.expression) {
        return HirContainerKeyValue { element, text };
    }
    hir_bind_container_iterator(key.references, type, element, text, true);
    HirContainerKeyValue result;
    bool valid = true;
    if (key.kind == HirContainerKey::Kind::text) {
        result.text = lower_hir_string_expression(*key.expression);
        valid = result.text.has_value();
    } else if (key.kind == HirContainerKey::Kind::real) {
        result.value = lower_hir_expression(*key.expression, 64U);
        valid = result.value.has_value();
    } else {
        result.value = lower_hir_expression(*key.expression, key.width);
        valid = result.value.has_value();
        if (valid && register_width(*result.value) != key.width) {
            result.value = resize_register(
                *result.value, key.width, key.is_signed);
        }
    }
    hir_bind_container_iterator(key.references, type, element, text, false);
    if (!valid) {
        return std::nullopt;
    }
    return result;
}

RegisterId Lowerer::lower_hir_container_key_compare(
    const HirContainerKey& key,
    const bool equal,
    const HirContainerKeyValue& lhs,
    const HirContainerKeyValue& rhs)
{
    using runtime::simir::BinaryOperator;
    const auto result = allocate_register(1U, frontend::ValueDomain::Logic4);
    if (key.kind == HirContainerKey::Kind::text) {
        // Strings compare lexicographically: the sign of lhs.compare(rhs)
        // (IEEE 1800-2023 6.16, 6.16.8).
        StringMethod method;
        method.operation = StringMethodOperator::compare;
        method.destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        method.source = *lhs.text;
        method.argument = *rhs.text;
        process_.operations.emplace_back(method);
        process_.operations.emplace_back(Binary {
            equal ? BinaryOperator::equal : BinaryOperator::less_signed,
            result, method.destination,
            load_constant(unsigned_value(0U, 32U),
                frontend::ValueDomain::Integer) });
    } else if (key.kind == HirContainerKey::Kind::real) {
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            equal ? runtime::SystemVerilogScalarBinaryOperator::Equal
                  : runtime::SystemVerilogScalarBinaryOperator::Less,
            result, *lhs.value, *rhs.value, key.scalar_kind, key.scalar_kind,
            frontend::SystemVerilogScalarKind::None });
    } else {
        process_.operations.emplace_back(Binary {
            equal ? BinaryOperator::equal
                  : key.is_signed ? BinaryOperator::less_signed
                                  : BinaryOperator::less_unsigned,
            result, *lhs.value, *rhs.value });
    }
    return result;
}

std::optional<Lowerer::HirContainerKeyedElement>
Lowerer::lower_hir_container_keyed_element(
    const HirContainerKey& key,
    const runtime::simir::ContainerType& type,
    const runtime::simir::ContainerRegisterId source,
    const RegisterId ordinal)
{
    HirContainerKeyedElement element;
    if (type.element_kind == ContainerElementKind::String) {
        element.text = allocate_string_register();
        process_.operations.emplace_back(ContainerStringRead {
            *element.text, source, ordinal, true, type.fixed, false, { } });
    } else {
        element.value = allocate_register(type.element_width,
            type.two_state ? frontend::ValueDomain::Bit2
                           : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ContainerRead {
            *element.value, source, ordinal, true, type.fixed, false });
    }
    const auto value = lower_hir_container_key_value(
        key, type, element.value, element.text);
    if (!value) {
        return std::nullopt;
    }
    element.key = *value;
    return element;
}

bool Lowerer::lower_hir_container_ordered_locator_loop(
    const support::BuiltinMethod method,
    const runtime::simir::ContainerRegisterId source,
    const runtime::simir::ContainerType& source_type,
    const runtime::simir::ContainerRegisterId destination,
    const std::string_view iterator_name,
    const std::optional<semantic::ExpressionId> expression)
{
    using runtime::simir::BinaryOperator;
    const auto key = hir_container_key(
        source_type, iterator_name, expression);
    if (!key) {
        return false;
    }
    const bool maximum = method == support::BuiltinMethod::max;
    const bool selecting = maximum || method == support::BuiltinMethod::min;
    const bool index_result = method == support::BuiltinMethod::unique_index;
    process_.operations.emplace_back(DeleteContainer { destination, { } });
    const auto size = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerSize { size, source });
    const auto zero = load_constant(
        PackedLogic4::from_aval_bval(32U, 0U, 0U),
        frontend::ValueDomain::Bit2);
    const auto one = load_constant(
        PackedLogic4::from_aval_bval(32U, 1U, 0U),
        frontend::ValueDomain::Bit2);
    const auto append = [&](const HirContainerKeyedElement& element,
                            const RegisterId ordinal) {
        if (index_result) {
            // The declared index of the element (IEEE 1800-2023 7.12.1).
            auto declared = ordinal;
            if (source_type.fixed) {
                const auto left = load_constant(
                    PackedLogic4::from_aval_bval(32U,
                        static_cast<std::uint32_t>(source_type.index_left),
                        0U),
                    frontend::ValueDomain::Bit2);
                declared = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    source_type.index_left >= source_type.index_right
                        ? BinaryOperator::subtract_unsigned
                        : BinaryOperator::add_unsigned,
                    declared, left, ordinal });
            }
            process_.operations.emplace_back(PushContainer {
                destination, declared, false, std::nullopt });
        } else if (element.text) {
            process_.operations.emplace_back(AppendContainer {
                destination, false, std::nullopt, std::nullopt,
                *element.text });
        } else {
            process_.operations.emplace_back(PushContainer {
                destination, *element.value, false, std::nullopt });
        }
    };
    const auto step = [&](const RegisterId counter) {
        const auto advanced = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, advanced, counter, one });
        process_.operations.emplace_back(CopyRegister { counter, advanced });
    };
    // The extreme element so far (min and max), zero on entry.
    const auto selected = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(CopyRegister { selected, zero });
    const auto index = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(CopyRegister { index, zero });
    const auto loop = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto more = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::less_unsigned, more, index, size });
    const auto loop_test = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch { more, 0U, 0U });
    const auto body = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto current = lower_hir_container_keyed_element(
        *key, source_type, source, index);
    if (!current) {
        return false;
    }
    if (selecting) {
        // min and max keep the first of equal elements; the extreme so far
        // is read again at each element.
        const auto best = lower_hir_container_keyed_element(
            *key, source_type, source, selected);
        if (!best) {
            return false;
        }
        const auto better = lower_hir_container_key_compare(*key, false,
            maximum ? best->key : current->key,
            maximum ? current->key : best->key);
        const auto better_test = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch { better, 0U, 0U });
        const auto replace = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CopyRegister { selected, index });
        const auto next = static_cast<InstructionIndex>(
            process_.operations.size());
        step(index);
        process_.operations.emplace_back(Jump { loop });
        const auto finish = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[loop_test] = Branch {
            more, body, finish, UnknownBranchPolicy::when_false };
        process_.operations[better_test] = Branch {
            better, replace, next, UnknownBranchPolicy::when_false };
        // An empty source selects nothing.
        const auto nonempty = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::not_equal, nonempty, size, zero });
        const auto push_test = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch { nonempty, 0U, 0U });
        const auto push = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto chosen = lower_hir_container_keyed_element(
            *key, source_type, source, selected);
        if (!chosen) {
            return false;
        }
        append(*chosen, selected);
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[push_test] = Branch {
            nonempty, push, end, UnknownBranchPolicy::when_false };
        return true;
    }
    // unique and unique_index: an element whose key equals the key of an
    // earlier element is dropped (7.12.1).
    const auto found = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant {
        found, PackedLogic4::from_aval_bval(1U, 0U, 0U) });
    const auto earlier = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(CopyRegister { earlier, zero });
    const auto inner = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto before = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::less_unsigned, before, earlier, index });
    const auto inner_test = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch { before, 0U, 0U });
    const auto inner_body = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto other = lower_hir_container_keyed_element(
        *key, source_type, source, earlier);
    if (!other) {
        return false;
    }
    const auto same = lower_hir_container_key_compare(
        *key, true, other->key, current->key);
    const auto same_test = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch { same, 0U, 0U });
    const auto mark = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(LoadConstant {
        found, PackedLogic4::from_aval_bval(1U, 1U, 0U) });
    const auto leave = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump { 0U });
    const auto advance_inner = static_cast<InstructionIndex>(
        process_.operations.size());
    step(earlier);
    process_.operations.emplace_back(Jump { inner });
    const auto inner_end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[inner_test] = Branch {
        before, inner_body, inner_end, UnknownBranchPolicy::when_false };
    process_.operations[same_test] = Branch {
        same, mark, advance_inner, UnknownBranchPolicy::when_false };
    process_.operations[leave] = Jump { inner_end };
    const auto duplicate_test = inner_end;
    process_.operations.emplace_back(Branch { found, 0U, 0U });
    const auto keep = static_cast<InstructionIndex>(
        process_.operations.size());
    append(*current, index);
    const auto skip = static_cast<InstructionIndex>(
        process_.operations.size());
    step(index);
    process_.operations.emplace_back(Jump { loop });
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[duplicate_test] = Branch {
        found, skip, keep, UnknownBranchPolicy::when_false };
    process_.operations[loop_test] = Branch {
        more, body, end, UnknownBranchPolicy::when_false };
    return true;
}

bool Lowerer::lower_hir_container_keyed_order_loop(
    const runtime::simir::ContainerRegisterId target,
    const runtime::simir::ContainerType& type,
    const bool descending,
    const std::string_view iterator_name,
    const std::optional<semantic::ExpressionId> expression)
{
    using runtime::simir::BinaryOperator;
    const auto key = hir_container_key(type, iterator_name, expression);
    if (!key) {
        return false;
    }
    const bool text_elements
        = type.element_kind == ContainerElementKind::String;
    const bool linear = type.fixed;
    // A with expression whose value a queue holds (text, real, or at most
    // 64 bits) is evaluated once per element and its values move with the
    // elements; a wider one is evaluated again at each comparison. Without
    // a with expression the elements compare themselves (7.12.2).
    const bool stored_keys = key->expression.has_value()
        && (key->kind != HirContainerKey::Kind::integral
            || key->width <= 64U);
    // The element alone, without evaluating the with expression.
    auto element_key = *key;
    element_key.expression.reset();
    element_key.references.clear();
    const auto position = [&]() {
        return static_cast<InstructionIndex>(process_.operations.size());
    };
    const auto size = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerSize { size, target });
    const auto one = load_constant(
        PackedLogic4::from_aval_bval(32U, 1U, 0U),
        frontend::ValueDomain::Bit2);
    const auto zero = load_constant(
        PackedLogic4::from_aval_bval(32U, 0U, 0U),
        frontend::ValueDomain::Bit2);
    const auto step = [&](const RegisterId counter) {
        const auto advanced = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, advanced, counter, one });
        process_.operations.emplace_back(CopyRegister { counter, advanced });
    };
    const auto write_element = [&](const ContainerRegisterId destination,
                                   const RegisterId ordinal,
                                   const HirContainerKeyedElement& element) {
        if (text_elements) {
            process_.operations.emplace_back(ContainerStringWrite {
                destination, ordinal, *element.text, true, linear, false,
                { }, false });
        } else {
            process_.operations.emplace_back(ContainerWrite {
                destination, ordinal, *element.value, true, linear, false });
        }
    };
    // The keys, one per element in element order, and the merge buffers:
    // queues of the key type, and a container of the receiver's type (a
    // fixed-size one already has every element).
    const auto buffer = allocate_container_register(type);
    if (!type.fixed) {
        process_.operations.emplace_back(DeleteContainer { buffer, { } });
    }
    std::optional<ContainerRegisterId> keys;
    std::optional<ContainerRegisterId> merged_keys;
    ContainerType key_type;
    key_type.queue = true;
    auto key_domain = frontend::ValueDomain::Logic4;
    // The key queues are cleared here; the first key value gives their
    // type, and the clears then name them.
    const auto key_clear = position();
    if (stored_keys) {
        process_.operations.emplace_back(DeleteContainer { 0U, { } });
        process_.operations.emplace_back(DeleteContainer { 0U, { } });
    }
    const auto read_key = [&](const ContainerRegisterId source,
                              const RegisterId ordinal) {
        HirContainerKeyValue result;
        if (key_type.element_kind == ContainerElementKind::String) {
            result.text = allocate_string_register();
            process_.operations.emplace_back(ContainerStringRead {
                *result.text, source, ordinal, true, false, false, { } });
        } else {
            result.value = allocate_register(
                key_type.element_width, key_domain);
            process_.operations.emplace_back(ContainerRead {
                *result.value, source, ordinal, true, false, false });
        }
        return result;
    };
    const auto write_key = [&](const ContainerRegisterId destination,
                               const RegisterId ordinal,
                               const HirContainerKeyValue& value) {
        if (value.text) {
            process_.operations.emplace_back(ContainerStringWrite {
                destination, ordinal, *value.text, true, false, false,
                { }, false });
        } else {
            process_.operations.emplace_back(ContainerWrite {
                destination, ordinal, *value.value, true, false, false });
        }
    };
    // for (i = 0; i < size; ++i) fill the buffer and the key queues
    // from a[i]
    if (!type.fixed || stored_keys) {
        const auto fill_index = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(CopyRegister { fill_index, zero });
        const auto fill = position();
        const auto filling = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, filling, fill_index, size });
        const auto fill_test = position();
        process_.operations.emplace_back(Branch { filling, 0U, 0U });
        const auto fill_body = position();
        const auto element = lower_hir_container_keyed_element(
            element_key, type, target, fill_index);
        if (!element) {
            return false;
        }
        if (!type.fixed) {
            process_.operations.emplace_back(AppendContainer {
                buffer, false, std::nullopt, element->value,
                element->text });
        }
        if (stored_keys) {
            const auto value = lower_hir_container_key_value(
                *key, type, element->value, element->text);
            if (!value || (!value->value && !value->text)) {
                return false;
            }
            if (value->text) {
                key_type.element_kind = ContainerElementKind::String;
                key_type.element_width = 0U;
            } else {
                key_domain = register_domain(*value->value);
                key_type.element_kind = ContainerElementKind::Packed;
                key_type.element_width = static_cast<std::uint32_t>(
                    register_width(*value->value));
                key_type.two_state
                    = key_domain != frontend::ValueDomain::Logic4
                    && key_domain != frontend::ValueDomain::Logic9
                    && key_domain != frontend::ValueDomain::Unknown;
                key_type.signed_elements = key->is_signed;
            }
            keys = allocate_container_register(key_type);
            merged_keys = allocate_container_register(key_type);
            process_.operations[key_clear]
                = DeleteContainer { *keys, { } };
            process_.operations[key_clear + 1U]
                = DeleteContainer { *merged_keys, { } };
            for (const auto key_queue : { *keys, *merged_keys }) {
                process_.operations.emplace_back(AppendContainer {
                    key_queue, false, std::nullopt, value->value,
                    value->text });
            }
        }
        step(fill_index);
        process_.operations.emplace_back(Jump { fill });
        const auto fill_end = position();
        process_.operations[fill_test] = Branch {
            filling, fill_body, fill_end, UnknownBranchPolicy::when_false };
    }
    // A bottom-up merge sort (7.12.2 does not require a stable order; the
    // merge keeps equal keys in their order). Each pass merges the sorted
    // runs [low, middle) and [middle, high) of `run` elements into one run
    // of the other buffer.
    const auto run = allocate_register(32U, frontend::ValueDomain::Bit2);
    const auto low = allocate_register(32U, frontend::ValueDomain::Bit2);
    const auto middle = allocate_register(32U, frontend::ValueDomain::Bit2);
    const auto high = allocate_register(32U, frontend::ValueDomain::Bit2);
    const auto left_index = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    const auto right_index = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    const auto out_index = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    const auto emit_pass = [&](const ContainerRegisterId from,
                               const std::optional<ContainerRegisterId>
                                   from_keys,
                               const ContainerRegisterId to,
                               const std::optional<ContainerRegisterId>
                                   to_keys) {
        process_.operations.emplace_back(CopyRegister { low, zero });
        const auto pass = position();
        const auto passing = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, passing, low, size });
        const auto pass_test = position();
        process_.operations.emplace_back(Branch { passing, 0U, 0U });
        const auto pass_body = position();
        // middle = min(low + run, size), high = min(middle + run, size),
        // without overflowing the 32-bit ordinals.
        process_.operations.emplace_back(CopyRegister { middle, size });
        process_.operations.emplace_back(CopyRegister { high, size });
        const auto remaining = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_unsigned, remaining, size, low });
        const auto split = allocate_register(1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, split, run, remaining });
        const auto split_test = position();
        process_.operations.emplace_back(Branch { split, 0U, 0U });
        const auto split_body = position();
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, middle, low, run });
        const auto rest = allocate_register(32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_unsigned, rest, size, middle });
        const auto full = allocate_register(1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, full, run, rest });
        const auto full_test = position();
        process_.operations.emplace_back(Branch { full, 0U, 0U });
        const auto full_body = position();
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, high, middle, run });
        const auto bounded = position();
        process_.operations[split_test] = Branch {
            split, split_body, bounded, UnknownBranchPolicy::when_false };
        process_.operations[full_test] = Branch {
            full, full_body, bounded, UnknownBranchPolicy::when_false };
        process_.operations.emplace_back(CopyRegister { left_index, low });
        process_.operations.emplace_back(
            CopyRegister { right_index, middle });
        process_.operations.emplace_back(CopyRegister { out_index, low });
        const auto merge = position();
        const auto merging = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, merging, out_index, high });
        const auto merge_test = position();
        process_.operations.emplace_back(Branch { merging, 0U, 0U });
        const auto merge_body = position();
        const auto left_more = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, left_more, left_index, middle });
        const auto left_test = position();
        process_.operations.emplace_back(Branch { left_more, 0U, 0U });
        const auto left_body = position();
        const auto right_more = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, right_more, right_index, high });
        const auto right_test = position();
        process_.operations.emplace_back(Branch { right_more, 0U, 0U });
        const auto right_body = position();
        // The right element goes first only when its key strictly precedes
        // the left one's: less for sort, greater for rsort.
        std::optional<HirContainerKeyValue> left_key;
        std::optional<HirContainerKeyValue> right_key;
        if (stored_keys) {
            left_key = read_key(*from_keys, left_index);
            right_key = read_key(*from_keys, right_index);
        } else {
            const auto left_element = lower_hir_container_keyed_element(
                *key, type, from, left_index);
            const auto right_element = lower_hir_container_keyed_element(
                *key, type, from, right_index);
            if (!left_element || !right_element) {
                return false;
            }
            left_key = left_element->key;
            right_key = right_element->key;
        }
        const auto precedes = lower_hir_container_key_compare(*key, false,
            descending ? *left_key : *right_key,
            descending ? *right_key : *left_key);
        const auto precedes_test = position();
        process_.operations.emplace_back(Branch { precedes, 0U, 0U });
        const auto take = [&](const RegisterId source_index) {
            const auto element = lower_hir_container_keyed_element(
                element_key, type, from, source_index);
            if (!element) {
                return false;
            }
            write_element(to, out_index, *element);
            if (stored_keys) {
                write_key(*to_keys, out_index,
                    read_key(*from_keys, source_index));
            }
            step(source_index);
            step(out_index);
            process_.operations.emplace_back(Jump { merge });
            return true;
        };
        const auto take_left = position();
        if (!take(left_index)) {
            return false;
        }
        const auto take_right = position();
        if (!take(right_index)) {
            return false;
        }
        const auto merge_end = position();
        process_.operations.emplace_back(CopyRegister { low, high });
        process_.operations.emplace_back(Jump { pass });
        const auto pass_end = position();
        process_.operations[pass_test] = Branch {
            passing, pass_body, pass_end, UnknownBranchPolicy::when_false };
        process_.operations[merge_test] = Branch {
            merging, merge_body, merge_end, UnknownBranchPolicy::when_false };
        process_.operations[left_test] = Branch {
            left_more, left_body, take_right,
            UnknownBranchPolicy::when_false };
        process_.operations[right_test] = Branch {
            right_more, right_body, take_left,
            UnknownBranchPolicy::when_false };
        process_.operations[precedes_test] = Branch {
            precedes, take_right, take_left,
            UnknownBranchPolicy::when_false };
        return true;
    };
    // Each continuing pass doubles `run` only while 2 * run < size, so it
    // does not overflow:
    //   run = 1; if (run < size) for (;;) {
    //     pass a -> buffer; if (!(run < size - run)) { a = buffer; break; }
    //     run += run;
    //     pass buffer -> a; if (!(run < size - run)) break;
    //     run += run; }
    const auto double_run = [&]() {
        const auto doubled = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, doubled, run, run });
        process_.operations.emplace_back(CopyRegister { run, doubled });
    };
    const auto continuing = [&]() {
        const auto remaining = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_unsigned, remaining, size, run });
        const auto again = allocate_register(1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned, again, run, remaining });
        return again;
    };
    process_.operations.emplace_back(CopyRegister { run, one });
    const auto sorting = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::less_unsigned, sorting, run, size });
    const auto sorting_test = position();
    process_.operations.emplace_back(Branch { sorting, 0U, 0U });
    const auto passes = position();
    if (!emit_pass(target, keys, buffer, merged_keys)) {
        return false;
    }
    const auto forward_again = continuing();
    const auto forward_test = position();
    process_.operations.emplace_back(Branch { forward_again, 0U, 0U });
    const auto backward = position();
    double_run();
    if (!emit_pass(buffer, merged_keys, target, keys)) {
        return false;
    }
    const auto backward_again = continuing();
    const auto backward_test = position();
    process_.operations.emplace_back(Branch { backward_again, 0U, 0U });
    const auto repeat = position();
    double_run();
    process_.operations.emplace_back(Jump { passes });
    // The sorted elements are in the buffer: a[i] = buffer[i].
    const auto copy_back = position();
    const auto copy_index = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(CopyRegister { copy_index, zero });
    const auto copy = position();
    const auto copying = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::less_unsigned, copying, copy_index, size });
    const auto copy_test = position();
    process_.operations.emplace_back(Branch { copying, 0U, 0U });
    const auto copy_body = position();
    const auto sorted = lower_hir_container_keyed_element(
        element_key, type, buffer, copy_index);
    if (!sorted) {
        return false;
    }
    write_element(target, copy_index, *sorted);
    step(copy_index);
    process_.operations.emplace_back(Jump { copy });
    const auto end = position();
    process_.operations[sorting_test] = Branch {
        sorting, passes, end, UnknownBranchPolicy::when_false };
    process_.operations[forward_test] = Branch {
        forward_again, backward, copy_back,
        UnknownBranchPolicy::when_false };
    process_.operations[backward_test] = Branch {
        backward_again, repeat, end, UnknownBranchPolicy::when_false };
    process_.operations[copy_test] = Branch {
        copying, copy_body, end, UnknownBranchPolicy::when_false };
    return true;
}

std::optional<Lowerer::HirStaticContainerValue>
Lowerer::lower_hir_container_locator_value(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call
        || !is_container_locator(semantic::sv::member_builtin_method(
            *expression->systemverilog))
        || expression->systemverilog->operands.empty()) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    const auto method = semantic::sv::member_builtin_method(call);
    const auto source_type = hir_static_container_expression_type(
        call.operands.front());
    if (!source_type || source_type->associative) {
        return std::nullopt;
    }
    // The result is a queue of the receiver's elements, or of int for an
    // index locator (IEEE 1800-2023 7.12.1).
    auto type = *source_type;
    type.fixed = false;
    type.queue = true;
    type.sparse = false;
    type.dimensions.clear();
    type.maximum_elements.reset();
    if (method == support::BuiltinMethod::unique_index
        || method == support::BuiltinMethod::find_index
        || method == support::BuiltinMethod::find_first_index
        || method == support::BuiltinMethod::find_last_index) {
        type = ContainerType { };
        type.element_width = 32U;
        type.two_state = true;
        type.signed_elements = true;
        type.queue = true;
    }
    HirContainerObjectBinding result;
    result.local = allocate_container_register(type);
    result.type = &type;
    if (!lower_hir_container_locator_assignment(
            expression_id, expression_id, &result)) {
        return std::nullopt;
    }
    return HirStaticContainerValue { *result.local, type };
}

std::optional<RegisterId> Lowerer::lower_hir_container_reduction_loop(
    const support::BuiltinMethod method,
    const runtime::simir::ContainerRegisterId source,
    const runtime::simir::ContainerType& source_type,
    const std::string_view iterator_name,
    const semantic::ExpressionId transformation)
{
    using runtime::simir::BinaryOperator;
    const auto references = hir_container_iterator_references(
        transformation, iterator_name);
    const auto element_domain = source_type.two_state
        ? frontend::ValueDomain::Bit2
        : frontend::ValueDomain::Logic4;
    // Over a multidimensional array each iterator value is a row: the
    // array without its first dimension (IEEE 1800-2023 7.12.3).
    std::optional<ContainerType> row_type;
    std::uint64_t row_count { 1U };
    std::uint64_t row_elements { 1U };
    if (source_type.fixed && source_type.dimensions.size() > 1U) {
        row_type = source_type;
        row_type->dimensions.erase(row_type->dimensions.begin());
        row_type->index_left = row_type->dimensions.front().first;
        row_type->index_right = row_type->dimensions.front().second;
        const auto& outer = source_type.dimensions.front();
        row_count = index_distance(outer.first, outer.second) + 1U;
        for (const auto& dimension : row_type->dimensions) {
            row_elements *= index_distance(dimension.first, dimension.second)
                + 1U;
        }
    }
    const auto bind_iterator = [&](const RegisterId index,
                                   const std::optional<RegisterId> element,
                                   const std::optional<ContainerRegisterId>
                                       row) {
        for (const auto& [id, is_index] : references) {
            if (is_index) {
                hir_captured_update_indices_.insert_or_assign(id, index);
            } else if (row) {
                hir_captured_containers_.insert_or_assign(
                    id, HirStaticContainerValue { *row, *row_type });
            } else {
                hir_captured_update_indices_.insert_or_assign(id, *element);
                if (source_type.signed_elements) {
                    hir_captured_signed_.insert(id);
                }
            }
        }
    };
    const auto unbind_iterator = [&]() {
        for (const auto& reference : references) {
            hir_captured_update_indices_.erase(reference.first);
            hir_captured_signed_.erase(reference.first);
            hir_captured_containers_.erase(reference.first);
        }
    };
    // The with expression's width, with the iterator at the element's.
    const auto probe = allocate_register(
        source_type.element_width, element_domain);
    bind_iterator(probe, probe,
        row_type ? std::optional { allocate_container_register(*row_type) }
                 : std::nullopt);
    const auto width = hir_expression_width(
        transformation, hir_process_scope_);
    const bool signed_transformation = hir_expression_signed(transformation);
    unbind_iterator();
    if (!width || *width == 0U) {
        return std::nullopt;
    }
    const auto accumulator_domain = frontend::ValueDomain::Logic4;
    const auto accumulator = allocate_register(*width, accumulator_domain);
    const bool product = method == support::BuiltinMethod::product;
    const bool all_ones = method == support::BuiltinMethod::reduce_and;
    auto initial = PackedLogic4 { *width,
        all_ones ? runtime::Logic4::one : runtime::Logic4::zero };
    if (product) {
        initial.set(0U, runtime::Logic4::one);
    }
    process_.operations.emplace_back(LoadConstant { accumulator, initial });
    const auto size = allocate_register(32U, frontend::ValueDomain::Bit2);
    if (row_type) {
        process_.operations.emplace_back(LoadConstant {
            size, unsigned_value(row_count, 32U) });
    } else {
        process_.operations.emplace_back(ContainerSize { size, source });
    }
    const auto count = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant {
        count, PackedLogic4::from_aval_bval(32U, 0U, 0U) });
    const auto one = load_constant(
        PackedLogic4::from_aval_bval(32U, 1U, 0U),
        frontend::ValueDomain::Bit2);
    const auto loop = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto more = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::less_unsigned, more, count, size });
    const auto enter = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch { more, 0U, 0U });
    const auto body = static_cast<InstructionIndex>(
        process_.operations.size());
    if (row_type) {
        const auto row = allocate_container_register(*row_type);
        const auto offset = allocate_register(32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::multiply_unsigned, offset, count,
            load_constant(unsigned_value(row_elements, 32U),
                frontend::ValueDomain::Bit2) });
        copy_hir_static_container_ordinals(
            row, *row_type, source, *row_type, { }, offset);
        bind_iterator(count, std::nullopt, row);
    } else {
        const auto element = allocate_register(
            source_type.element_width, element_domain);
        process_.operations.emplace_back(ContainerRead {
            element, source, count, true, false, false });
        bind_iterator(count, element, std::nullopt);
    }
    auto value = lower_hir_expression(transformation, *width);
    unbind_iterator();
    if (!value) {
        return std::nullopt;
    }
    if (register_width(*value) != *width) {
        value = resize_register(*value, *width, signed_transformation);
    }
    const auto combined = allocate_register(*width, accumulator_domain);
    process_.operations.emplace_back(Binary {
        method == support::BuiltinMethod::sum ? BinaryOperator::add_unsigned
            : product    ? BinaryOperator::multiply_unsigned
            : all_ones   ? BinaryOperator::bit_and
            : method == support::BuiltinMethod::reduce_or
                ? BinaryOperator::bit_or
                : BinaryOperator::bit_xor,
        combined, accumulator, *value });
    process_.operations.emplace_back(CopyRegister { accumulator, combined });
    const auto advanced = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::add_unsigned, advanced, count, one });
    process_.operations.emplace_back(CopyRegister { count, advanced });
    process_.operations.emplace_back(Jump { loop });
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[enter] = Branch {
        more, body, end, UnknownBranchPolicy::when_false };
    return accumulator;
}

std::vector<std::pair<std::uint32_t, bool>>
Lowerer::hir_container_iterator_references(
    const semantic::ExpressionId expression,
    const std::string_view iterator_name) const
{
    std::vector<std::pair<std::uint32_t, bool>> references;
    std::vector<semantic::ExpressionId> pending { expression };
    std::unordered_set<std::uint32_t> visited;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current.value()).second) {
            continue;
        }
        const auto record = specialized_hir_unit_->find_expression(current);
        if (!record || record->systemverilog == nullptr) {
            continue;
        }
        const auto& node = *record->systemverilog;
        if (node.kind == semantic::sv::ExpressionKind::name
            && (node.text == iterator_name
                || semantic::sv::is_iterator_index_name(
                    node, iterator_name, true))) {
            references.emplace_back(current.value(),
                node.text != iterator_name);
            continue;
        }
        // A nested array method whose with clause names the same iterator
        // hides this one there (IEEE 1800-2023 7.12.4).
        if (node.kind == semantic::sv::ExpressionKind::call
            && (is_container_reduction(semantic::sv::member_builtin_method(node))
                || is_container_locator(semantic::sv::member_builtin_method(node)))
            && node.operands.size() >= 2U) {
            const auto inner = node.operands.size() == 3U
                ? specialized_hir_unit_->find_expression(node.operands[1])
                : std::nullopt;
            const auto inner_name = inner && inner->systemverilog != nullptr
                ? std::string_view { inner->systemverilog->text }
                : std::string_view { "item" };
            if (inner_name == iterator_name) {
                pending.push_back(node.operands.front());
                continue;
            }
        }
        pending.insert(
            pending.end(), node.operands.begin(), node.operands.end());
    }
    return references;
}

std::optional<semantic::sv::TypeReference>
Lowerer::hir_systemverilog_value_type(
    const semantic::ExpressionId expression_id,
    const std::size_t trailing_segments) const
{
    // A deep expression (a long ?: chain) continues on a fresh stack
    // segment: each operand is a nested call.
    if (!support::stack_growth_detail::has_room()) {
        return support::with_stack(
            [&] { return hir_systemverilog_value_type(expression_id, trailing_segments); });
    }
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
    // A member of a structure value (`p.m` over a class property).
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.marker == support::ExpressionMarker::sv_select
        && source.operands.size() == 1U && trailing_segments == 0U) {
        // A boxed structure property has no packed profile; its declared
        // type names the structure.
        auto base = hir_systemverilog_value_type(source.operands.front());
        if (!base) {
            const auto operand = specialized_hir_unit_->find_expression(
                source.operands.front());
            const auto& input = operand && operand->systemverilog != nullptr
                ? *operand->systemverilog : source;
            const bool static_property = input.kind
                == semantic::sv::ExpressionKind::class_static_property;
            if (&input != &source && !input.class_member_identity.empty()
                && (static_property
                    || input.kind
                        == semantic::sv::ExpressionKind::class_property)) {
                if (const auto* property
                    = resolver.resolve_systemverilog_class_property(
                        input.class_member_identity, static_property)
                          .unique()) {
                    base = property->type;
                }
            }
        }
        const auto* structure = base
            ? semantic::sv::systemverilog_structure_definition(
                  specialized_hir_unit_->design().systemverilog_hir, *base)
            : nullptr;
        if (structure == nullptr) {
            return std::nullopt;
        }
        const std::string_view name { source.marker_argument };
        const auto member = std::ranges::find(
            structure->members, name, &semantic::sv::PackedMember::name);
        if (member == structure->members.end()) {
            return std::nullopt;
        }
        return member->type;
    }
    // A class property, typed as the accessed specialization declares it
    // (IEEE 1800-2017 8.25).
    if ((source.kind == semantic::sv::ExpressionKind::class_property
            || source.kind
                == semantic::sv::ExpressionKind::class_static_property)
        && trailing_segments == 0U) {
        const auto profile = hir_class_property_profile(expression_id);
        if (!profile || profile->event) {
            return std::nullopt;
        }
        return profile->type;
    }
    if (source.kind != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    // A caller may drop trailing segments, such as a method name.
    auto text = std::string_view { source.text };
    for (std::size_t dropped { }; dropped < trailing_segments; ++dropped) {
        const auto last = text.rfind('.');
        if (last == std::string_view::npos) {
            return std::nullopt;
        }
        text = text.substr(0U, last);
    }
    const auto name_separator = text.find('.');
    auto declaration_id = hir_referenced_declaration(expression_id);
    if (!declaration_id && name_separator != std::string::npos) {
        declaration_id = resolver.resolve_systemverilog(
            text.substr(0U, name_separator),
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
    if (text.substr(0U, name_separator)
        != declaration->systemverilog->name) {
        return std::nullopt;
    }
    auto remaining = text.substr(name_separator + 1U);
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
    const auto enumeration_method = [](const support::BuiltinMethod method) {
        using support::BuiltinMethod;
        return method == BuiltinMethod::first || method == BuiltinMethod::last
            || method == BuiltinMethod::next || method == BuiltinMethod::prev
            || method == BuiltinMethod::num || method == BuiltinMethod::name;
    };
    // A `variable.method` name written without parentheses (13.4.1)
    // carries no built-in method of its own: its last part is looked up in
    // the LRM's method table.
    HirEnumerationMethod result;
    std::optional<semantic::DeclarationId> declaration_id;
    if (source.kind == semantic::sv::ExpressionKind::call
        && enumeration_method(semantic::sv::member_builtin_method(source))
        && !source.operands.empty() && source.operands.size() <= 2U) {
        result.method = semantic::sv::member_builtin_method(source);
        result.receiver = source.operands.front();
        if (source.operands.size() == 2U) {
            if (result.method != support::BuiltinMethod::next && result.method != support::BuiltinMethod::prev) {
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
        // An enumeration class property (`h.e.name()`, IEEE 1800-2017
        // 6.19.5, 8.5).
        if (receiver && receiver->systemverilog != nullptr
            && (receiver->systemverilog->kind
                    == semantic::sv::ExpressionKind::class_property
                || receiver->systemverilog->kind
                    == semantic::sv::ExpressionKind::class_static_property)) {
            if (const auto type = hir_systemverilog_value_type(
                    source.operands.front())) {
                return hir_systemverilog_enumeration_type_profile(
                    std::move(result), *type, receiver->systemverilog->scope);
            }
            return std::nullopt;
        }
        // The `state` a process's status() returns (IEEE 1800-2023 9.7): an
        // int-based enumeration whose constants are numbered in order.
        if (hir_process_status_query(source.operands.front())) {
            const auto states = frontend::systemverilog_process_states();
            for (std::size_t index = 0U; index < states.size(); ++index) {
                result.names.emplace_back(states[index]);
                result.values.push_back(index);
            }
            result.width = 32U;
            result.signed_value = true;
            return result;
        }
        // A chained call (`e.next(1).next(1)`) has the enumeration type of
        // the inner method's receiver.
        if (receiver && receiver->systemverilog != nullptr
            && receiver->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            const auto inner = hir_systemverilog_enumeration_method(
                source.operands.front());
            if (!inner || inner->method == support::BuiltinMethod::name
                || inner->method == support::BuiltinMethod::num) {
                return std::nullopt;
            }
            // The inner call's value is of the inner receiver's
            // enumeration type, whatever named that receiver (a variable,
            // an array element, a class property or a structure member).
            auto chained = std::move(*inner);
            chained.method = result.method;
            chained.receiver = result.receiver;
            chained.count = result.count;
            chained.member.reset();
            return chained;
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
        const auto named = separator == std::string::npos
            ? support::BuiltinMethod::none
            : support::builtin_method_named(
                  std::string_view { source.text }.substr(separator + 1U));
        if (!enumeration_method(named)) {
            return std::nullopt;
        }
        result.method = named;
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
            auto prefix = std::string_view { source.text }.substr(
                0U, separator);
            if (const auto scope = prefix.rfind("::");
                prefix.find('.') != std::string_view::npos
                && (scope == std::string_view::npos
                    || scope < prefix.find('.'))) {
                prefix = prefix.substr(0U, prefix.find('.'));
            }
            declaration_id = hir_resolver().resolve_systemverilog(
                 prefix, source.scope, object, false)
                                 .unique();
        }
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        // Only a direct `variable.method`, or `P::variable.method` for a
        // package variable, or an enumeration member of a packed structure
        // variable (`s.m.name`, IEEE 1800-2017 6.19.5, 7.2).
        auto root = std::string_view { source.text }.substr(0U, separator);
        if (const auto scope = root.rfind("::");
            scope != std::string_view::npos) {
            root.remove_prefix(scope + 2U);
        }
        if (const auto member_separator = root.find('.');
            member_separator != std::string_view::npos) {
            if (!declaration || declaration->systemverilog == nullptr
                || declaration->systemverilog->name
                    != root.substr(0U, member_separator)) {
                return std::nullopt;
            }
            auto member = hir_systemverilog_member_path(
                *declaration_id, root.substr(member_separator + 1U));
            const auto type = member
                ? hir_systemverilog_value_type(expression_id, 1U)
                : std::nullopt;
            if (!type) {
                return std::nullopt;
            }
            result.receiver_declaration = *declaration_id;
            result.member = std::move(member);
            return hir_systemverilog_enumeration_type_profile(
                std::move(result), *type,
                declaration->systemverilog->scope);
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
    const semantic::DeclarationId declaration_id, const bool wide) const
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
        *declaration->systemverilog->type, declaration->systemverilog->scope,
        wide);
}

std::optional<Lowerer::HirEnumerationMethod>
Lowerer::hir_systemverilog_enumeration_type_profile(
    HirEnumerationMethod result,
    semantic::sv::TypeReference type,
    const semantic::ScopeId scope, const bool wide) const
{
    // A type parameter names the type its specialization binds (8.25).
    type = hir_resolver().effective_systemverilog_type(type, scope)
               .value_or(type);
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
    if (!width || *width == 0U || (*width > 64U && !wide)) {
        return std::nullopt;
    }
    result.width = *width;
    result.four_state = hir_systemverilog_type_four_state(type);
    result.signed_value = type.signed_value;
    if (*width > 64U) {
        // A literal without a value is the previous literal's value plus
        // one (IEEE 1800-2023 6.19); values wider than 64 bits are kept
        // as packed words.
        const auto words = (*width + 63U) / 64U;
        const auto top_mask = *width % 64U == 0U
            ? ~std::uint64_t { 0 }
            : (std::uint64_t { 1 } << (*width % 64U)) - 1U;
        std::optional<std::vector<std::uint64_t>> previous_words;
        for (const auto& literal :
            definition->systemverilog->enumeration_literals) {
            std::vector<std::uint64_t> aval(words);
            std::vector<std::uint64_t> bval(words);
            if (literal.value) {
                std::string error;
                const auto constant = evaluate_hir_systemverilog_constant(
                    *specialized_hir_unit_, *literal.value, error);
                if (!constant) {
                    return std::nullopt;
                }
                const auto source_aval = constant->packed.aval_words();
                const auto source_bval = constant->packed.bval_words();
                const auto width_bits = constant->packed.width();
                const bool negative = constant->signed_value
                    && width_bits != 0U
                    && ((source_aval[(width_bits - 1U) / 64U]
                            >> ((width_bits - 1U) % 64U))
                        & 1U) != 0U;
                for (std::size_t word { }; word < words; ++word) {
                    if (word < source_aval.size()) {
                        aval[word] = source_aval[word];
                        bval[word] = source_bval[word];
                        const auto low_bits = width_bits - word * 64U;
                        if (low_bits < 64U) {
                            const auto kept
                                = (std::uint64_t { 1 } << low_bits) - 1U;
                            aval[word] = (aval[word] & kept)
                                | (negative ? ~kept : std::uint64_t { });
                            bval[word] &= kept;
                        }
                    } else {
                        aval[word] = negative ? ~std::uint64_t { 0 }
                                              : std::uint64_t { };
                    }
                }
            } else if (previous_words) {
                aval = *previous_words;
                for (auto& word : aval) {
                    if (++word != 0U) {
                        break;
                    }
                }
            }
            aval.back() &= top_mask;
            bval.back() &= top_mask;
            if (std::ranges::any_of(bval,
                    [](const std::uint64_t word) { return word != 0U; })) {
                // An x or z literal value is not a known value, as the
                // 64-bit profile also refuses.
                return std::nullopt;
            }
            previous_words = aval;
            result.wide_values.push_back(
                PackedLogic4::from_word_planes(*width, aval, bval));
            result.names.push_back(literal.name);
        }
        return result;
    }
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
        if (wide) {
            result.wide_values.push_back(PackedLogic4::from_aval_bval(
                result.width, result.values.back(), 0U));
        }
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
        if (const auto& member = method.member) {
            if (member->offset + member->width > binding->width) {
                return std::nullopt;
            }
            const auto part = allocate_register(
                member->width, binding->domain);
            process_.operations.emplace_back(Extract {
                part, destination,
                static_cast<std::uint32_t>(member->offset),
                static_cast<std::uint32_t>(member->width) });
            value = part;
        }
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
    if (!method || method->method == support::BuiltinMethod::name) {
        return std::nullopt;
    }
    const auto finish = [&](RegisterId value) {
        if (expected_width != 0U && register_width(value) != expected_width) {
            value = resize_register(value, expected_width,
                method->method == support::BuiltinMethod::num || method->signed_value);
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
    if (method->method == support::BuiltinMethod::num) {
        const auto destination
            = load_constant(unsigned_value(method->values.size(), 32U),
                frontend::ValueDomain::Bit2);
        return finish(destination);
    }
    if (method->method == support::BuiltinMethod::first || method->method == support::BuiltinMethod::last) {
        return finish(constant(method->method == support::BuiltinMethod::first
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
    const auto offset = method->method == support::BuiltinMethod::next ? steps : -steps;
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
    if (!method || method->method != support::BuiltinMethod::name) {
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
    name.method = support::BuiltinMethod::name;
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
