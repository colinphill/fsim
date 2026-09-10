// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <bit>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt Lowerer::lower_primary_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    constexpr std::string_view triggered_suffix { ".triggered" };
    if (language_ == frontend::Language::SystemVerilog2017
        && expression.kind == ExpressionKind::Identifier
        && expression.text.ends_with(triggered_suffix)) {
        const auto event_name = expression.text.substr(
            0, expression.text.size() - triggered_suffix.size());
        const auto found = signals_.find(event_name);
        if (found == signals_.end()
            || design_.signal_info_[found->second].type_name
                != "event") {
            report(
                "FSIM-ELAB-SVEVENT-008",
                "triggered property receiver '" + event_name
                    + "' is not a named event",
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            EventTriggered { destination, found->second });
        return destination;
    }

    auto synchronization = lower_synchronization_expression(
        expression, expected_width, expected_type);
    if (synchronization.handled) {
        return synchronization;
    }

    auto process = lower_process_expression(expression);
    if (process.handled) {
        return process;
    }

    auto vital = lower_vhdl_vital_expression(
        expression, expected_width, expected_type);
    if (vital.handled) {
        return vital;
    }

    auto vhdl_physical = lower_vhdl_physical_expression(
        expression, expected_width, expected_type);
    if (vhdl_physical.handled) {
        return vhdl_physical;
    }

    auto vhdl_access = lower_vhdl_access_expression(
        expression, expected_width, expected_type);
    if (vhdl_access.handled) {
        return vhdl_access;
    }

    auto unpacked_aggregate = lower_unpacked_aggregate_member_read(expression);
    if (unpacked_aggregate.handled) {
        return unpacked_aggregate;
    }

    auto multidimensional = lower_multidimensional_container_read(expression);
    if (multidimensional.handled) {
        return multidimensional;
    }
    auto vhdl_array_selection = lower_vhdl_array_selection_expression(
        expression, expected_width, expected_type);
    if (vhdl_array_selection.handled) {
        return vhdl_array_selection;
    }

    auto cast = lower_primary_cast_expression(
        expression, expected_width, expected_type);
    if (cast.handled) {
        return cast;
    }


    const bool container_reduction = expression.kind == ExpressionKind::Call
        && (expression.text == ".sum"
            || expression.text == ".product"
            || expression.text == ".and"
            || expression.text == ".or"
            || expression.text == ".xor");
    const bool container_ordering = expression.kind == ExpressionKind::Call
        && (expression.text == ".reverse"
            || expression.text == ".sort"
            || expression.text == ".rsort"
            || expression.text == ".shuffle");
    if (container_ordering) {
        report(
            "FSIM-ELAB-SVORDER-004",
            "container ordering methods do not produce an "
            "expression result",
            expression.span);
        return std::nullopt;
    }
    const bool container_locator = expression.kind == ExpressionKind::Call
        && (expression.text == ".min"
            || expression.text == ".max"
            || expression.text == ".unique"
            || expression.text == ".unique_index"
            || expression.text == ".find"
            || expression.text == ".find_index"
            || expression.text == ".find_first"
            || expression.text == ".find_first_index"
            || expression.text == ".find_last"
            || expression.text == ".find_last_index");
    if (container_locator) {
        const bool predicate_locator = expression.text.starts_with(".find");
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-006"
                : "FSIM-ELAB-SVLOCATOR-004",
            "container locator results require a compatible "
            "whole-queue assignment target",
            expression.span);
        return std::nullopt;
    }
    if (container_reduction
        && (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.empty()
            || expression.operands.size() > 3
            || (!is_container_expression(
                    expression.operands.front())
                && !is_static_container_slice_candidate(
                    expression.operands.front())))) {
        report(
            expression.operands.empty()
                    || expression.operands.size() > 3
                ? "FSIM-ELAB-SVREDUCE-002"
                : "FSIM-ELAB-SVREDUCE-001",
            expression.operands.empty()
                    || expression.operands.size() > 3
                ? "container reduction methods take no arguments "
                  "and retain at most one iterator plus one "
                  "with-clause transformation"
                : "container reduction methods require a direct "
                  "SystemVerilog unpacked-container receiver",
            expression.span);
        return std::nullopt;
    }

    const bool static_slice_receiver = !expression.operands.empty()
        && is_static_container_slice_candidate(
            expression.operands.front());
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
            && (is_container_expression(
                    expression.operands.front())
                || (static_slice_receiver
                    && (expression.text == ".size"
                        || container_reduction))))
        || (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2
            && is_container_expression(
                expression.operands.front()))) {
        const auto& source_expression = expression.operands.front();
        if (expression.kind == ExpressionKind::Call
            && (expression.text == ".pop_front"
                || expression.text == ".pop_back")
            && source_expression.kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVCONTAINER-022",
                "mutating container methods require a direct "
                "writable object receiver",
                source_expression.span);
            return std::nullopt;
        }
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
        if (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2U
            && source_expression.kind == ExpressionKind::Identifier) {
            const auto object = container_objects_.find(
                source_expression.text);
            const auto* source_type = container_expression_type(
                source_expression);
            const auto runtime_type = container_expression_runtime_type(
                source_expression);
            if (object != container_objects_.end()
                && source_type != nullptr
                && runtime_type
                && runtime_type->fixed
                && runtime_type->dimensions.size() == 1U
                && !runtime_type->two_state
                && (runtime_type->element_kind
                        == ContainerElementKind::Packed
                    || runtime_type->element_kind
                        == ContainerElementKind::Scalar)
                && runtime_type->element_width != 0U) {
                const auto alias = std::find_if(
                    design_.container_signal_aliases_.rbegin(),
                    design_.container_signal_aliases_.rend(),
                    [&](const ContainerSignalAlias& candidate) {
                        return candidate.object == object->second
                            && candidate.readable;
                    });
                const auto selected = static_integer_value(
                    expression.operands[1]);
                const auto [left, right]
                    = runtime_type->dimensions.front();
                const auto low = std::min(left, right);
                const auto high = std::max(left, right);
                if (alias != design_.container_signal_aliases_.rend()
                    && design_.signal_info_.at(alias->signal).source_domain
                        == frontend::ValueDomain::Logic4
                    && selected
                    && *selected >= low && *selected <= high) {
                    const auto count = static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(high) - low + 1);
                    const auto total_width = count
                        * runtime_type->element_width;
                    if (total_width
                        <= std::numeric_limits<std::uint32_t>::max()) {
                        const auto ordinal = left >= right
                            ? static_cast<std::uint64_t>(left - *selected)
                            : static_cast<std::uint64_t>(*selected - left);
                        const auto offset = static_cast<std::uint32_t>(
                            (count - 1U - ordinal)
                            * runtime_type->element_width);
                        const auto packed = allocate_register(
                            static_cast<std::size_t>(total_width),
                            design_.signal_info_.at(alias->signal)
                                        .source_domain);
                        frontend::ValueDomain element_domain
                            = design_.signal_info_.at(alias->signal)
                                  .source_domain;
                        if (source_type->systemverilog_container
                            && source_type->systemverilog_container
                                   ->element_types.size() == 1U) {
                            element_domain
                                = source_type->systemverilog_container
                                      ->element_types.front()
                                      .domain;
                        }
                        const auto destination = allocate_register(
                            runtime_type->element_width,
                            element_domain);
                        process_.operations.emplace_back(
                            ReadSignal {
                                packed,
                                alias->signal,
                                sample_concurrent_assertion_reads_
                                    ? SignalReadKind::sampled
                                    : SignalReadKind::current });
                        process_.operations.emplace_back(Extract {
                            destination,
                            packed,
                            offset,
                            runtime_type->element_width });
                        implicit_signal_dependencies_.push_back(
                            alias->signal);
                        return destination;
                    }
                }
            }
        }
        const auto source = lower_container_expression(source_expression);
        if (!source) {
            return std::nullopt;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == ".size") {
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            const auto runtime_type = container_expression_runtime_type(
                source_expression);
            if (runtime_type && runtime_type->fixed
                && runtime_type->dimensions.size() > 1) {
                const auto& outer = runtime_type->dimensions.front();
                const auto count = static_cast<std::uint32_t>(
                    std::abs(
                        static_cast<std::int64_t>(outer.first)
                        - outer.second)
                    + 1);
                process_.operations.emplace_back(LoadConstant {
                    destination, unsigned_value(count, 32) });
                return destination;
            }
            process_.operations.emplace_back(
                ContainerSize { destination, *source });
            return destination;
        }
        const auto* type = container_expression_type(source_expression);
        if (type == nullptr) {
            report(
                "FSIM-ELAB-SVCONTAINER-002",
                "container element type cannot be resolved",
                expression.span);
            return std::nullopt;
        }
        const auto runtime_type = container_expression_runtime_type(
            source_expression);
        if (!runtime_type) {
            return std::nullopt;
        }
        if (container_reduction) {
            if (runtime_type->element_kind
                != ContainerElementKind::Packed) {
                report(
                    "FSIM-ELAB-SVREDUCE-006",
                    "container reductions require packed integral elements",
                    expression.span);
                return std::nullopt;
            }
            const auto width = type->width();
            if (!width)
                return std::nullopt;
            const bool explicit_iterator = expression.operands.size() == 3;
            const bool has_transformation = expression.operands.size() >= 2;
            if (explicit_iterator
                && expression.operands[1].kind
                    != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-SVREDUCE-007",
                    "a named container reduction iterator must be "
                    "one identifier",
                    expression.operands[1].span);
                return std::nullopt;
            }
            const std::string_view iterator_name = explicit_iterator
                ? std::string_view {
                      expression.operands[1].text
                  }
                : std::string_view { "item" };
            if (has_transformation
                && runtime_type->associative) {
                report(
                    "FSIM-ELAB-SVREDUCE-006",
                    "container reduction with-clauses do not "
                    "support associative-array receivers",
                    expression.span);
                return std::nullopt;
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
                    "FSIM-ELAB-SVREDUCE-007",
                    "named container reduction iterator '"
                        + std::string { iterator_name }
                        + "' collides with a visible object",
                    expression.operands[1].span);
                return std::nullopt;
            }
            std::vector<ContainerPredicateNode> transformation;
            if (has_transformation) {
                const auto& transformation_expression = expression.operands[explicit_iterator ? 2U : 1U];
                const auto lowered = lower_container_expression_graph(
                    transformation_expression,
                    iterator_name,
                    *type, *runtime_type,
                    ContainerExpressionPurpose::
                        reduction_transformation);
                if (!lowered) {
                    return std::nullopt;
                }
                transformation = std::move(*lowered);
            }
            auto operation = ContainerReductionOperator::sum;
            if (expression.text == ".product") {
                operation = ContainerReductionOperator::product;
            } else if (expression.text == ".and") {
                operation = ContainerReductionOperator::bit_and;
            } else if (expression.text == ".or") {
                operation = ContainerReductionOperator::bit_or;
            } else if (expression.text == ".xor") {
                operation = ContainerReductionOperator::bit_xor;
            }
            const auto destination = allocate_register(*width, type->domain);
            process_.operations.emplace_back(
                ContainerReduction {
                    operation, destination, *source,
                    std::move(transformation) });
            return destination;
        }
        const auto width = type->width();
        if (!width)
            return std::nullopt;
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
            const auto* index_type = type->systemverilog_container
                                         ->associative_index_type.get();
            const bool string_index = runtime_type->string_indices;
            auto index = string_index
                ? lower_string_expression(expression.operands[1])
                : lower_expression(
                      expression.operands[1],
                      runtime_type->index_width,
                      index_type);
            if (!index) {
                return std::nullopt;
            }
            if (expression.text == ".exists"
                && !string_index
                && register_width(*index)
                    != runtime_type->index_width) {
                *index = resize_register(
                    *index,
                    runtime_type->index_width,
                    index_type->is_signed);
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            if (expression.text == ".exists") {
                process_.operations.emplace_back(
                    ContainerExists {
                        destination, *source, *index,
                        string_index });
                return destination;
            }
            if (string_index) {
                if (expression.operands[1].kind
                        != ExpressionKind::Identifier
                    || (!string_locals_.contains(
                            expression.operands[1].text)
                        && !string_objects_.contains(
                            expression.operands[1].text))) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-016",
                        expression.text
                            + " requires a direct mutable string "
                              "variable argument",
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
                    TraverseContainer {
                        destination, *source, *index,
                        traversal, true });
                if (const auto object = string_objects_.find(
                        expression.operands[1].text);
                    object != string_objects_.end()
                    && !string_locals_.contains(
                        expression.operands[1].text)) {
                    process_.operations.emplace_back(
                        WriteStringObject { object->second, *index });
                }
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
                TraverseContainer {
                    destination, *source, *index, traversal });
            if (const auto signal = signals_.find(
                    expression.operands[1].text);
                signal != signals_.end()
                && !locals_.contains(
                    expression.operands[1].text)) {
                process_.operations.emplace_back(
                    WriteBlocking { signal->second, *index });
            }
            return destination;
        }
        const auto destination = allocate_register(*width, type->domain);
        if (expression.kind == ExpressionKind::Call) {
            process_.operations.emplace_back(
                PopContainer {
                    destination,
                    *source,
                    expression.text == ".pop_front" });
            if (const auto object = container_objects_.find(
                    source_expression.text);
                object != container_objects_.end()) {
                process_.operations.emplace_back(
                    WriteContainerObject {
                        object->second, *source, std::nullopt });
            }
            return destination;
        }
        const auto index_width = runtime_type->associative
            ? static_cast<std::size_t>(
                  runtime_type->index_width)
            : runtime_type->fixed
            ? std::size_t { 32 }
            : infer_width(expression.operands[1])
                  .value_or(std::size_t { 32 });
        const bool string_index = runtime_type->associative
            && runtime_type->string_indices;
        auto index = string_index
            ? lower_string_expression(expression.operands[1])
            : lower_expression(
                  expression.operands[1],
                  index_width,
                  runtime_type->associative
                      ? type->systemverilog_container
                            ->associative_index_type.get()
                      : nullptr);
        if (!index) {
            return std::nullopt;
        }
        if (runtime_type->fixed
            && !string_index
            && register_width(*index) != 32U) {
            *index = resize_register(
                *index,
                32U,
                is_signed_expression(expression.operands[1]));
        } else if (runtime_type->associative
            && !string_index
            && register_width(*index)
                != runtime_type->index_width) {
            *index = resize_register(
                *index,
                runtime_type->index_width,
                runtime_type->signed_indices);
        }
        process_.operations.emplace_back(
            ContainerRead {
                destination,
                *source,
                *index,
                runtime_type->associative
                    ? runtime_type->signed_indices
                    : runtime_type->fixed
                        || is_signed_expression(
                            expression.operands[1]),
                false,
                string_index });
        return destination;
    }

    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with('.')
        && !expression.operands.empty()
        && (is_container_expression(
                expression.operands.front())
            || is_static_container_slice_candidate(
                expression.operands.front()))) {
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
        const auto source = lower_string_expression(
            expression.operands.front());
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            StringLength { destination, *source });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == ".getc"
            || expression.text == ".compare"
            || expression.text == ".icompare"
            || expression.text == ".atoi"
            || expression.text == ".atohex"
            || expression.text == ".atooct"
            || expression.text == ".atobin"
            || expression.text == ".atoreal")
        && !expression.operands.empty()
        && is_string_expression(expression.operands.front())) {
        const bool conversion = expression.text == ".atoi"
            || expression.text == ".atohex"
            || expression.text == ".atooct"
            || expression.text == ".atobin"
            || expression.text == ".atoreal";
        if (expression.operands.size() != (conversion ? 1U : 2U)) {
            report(
                "FSIM-ELAB-SVSTRING-018",
                "runtime string method '" + expression.text
                    + (conversion
                            ? "' takes no arguments"
                            : "' requires one argument"),
                expression.span);
            return std::nullopt;
        }
        const auto source = lower_string_expression(
            expression.operands.front());
        if (!source) {
            return std::nullopt;
        }
        StringMethod method;
        method.source = *source;
        method.destination = allocate_register(
            expression.text == ".atoreal" ? 64U : 32U,
            expression.text == ".getc"
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Integer);
        if (conversion) {
            method.operation = expression.text == ".atoi"
                ? StringMethodOperator::atoi
                : expression.text == ".atohex"
                ? StringMethodOperator::atohex
                : expression.text == ".atooct"
                ? StringMethodOperator::atooct
                : expression.text == ".atobin"
                ? StringMethodOperator::atobin
                : StringMethodOperator::atoreal;
        } else if (expression.text == ".getc") {
            auto index = lower_expression(
                expression.operands[1], 32);
            if (!index) {
                report(
                    "FSIM-ELAB-SVSTRING-018",
                    "getc index must be a signed 32-bit integral value",
                    expression.operands[1].span);
                return std::nullopt;
            }
            if (register_width(*index) != 32) {
                *index = resize_register(
                    *index, 32,
                    is_signed_expression(expression.operands[1]));
            }
            method.operation = StringMethodOperator::getc;
            method.first = *index;
        } else {
            if (!is_string_expression(expression.operands[1])) {
                report(
                    "FSIM-ELAB-SVSTRING-018",
                    "compare argument must be a runtime string value",
                    expression.operands[1].span);
                return std::nullopt;
            }
            const auto argument = lower_string_expression(
                expression.operands[1]);
            if (!argument) {
                return std::nullopt;
            }
            method.operation = expression.text == ".compare"
                ? StringMethodOperator::compare
                : StringMethodOperator::icompare;
            method.argument = *argument;
        }
        process_.operations.emplace_back(method);
        return method.destination;
    }
    if (expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2
        && is_string_expression(expression.operands.front())) {
        const auto source = lower_string_expression(
            expression.operands.front());
        const auto index_width = infer_width(expression.operands[1])
                                     .value_or(std::size_t { 32 });
        const auto index = lower_expression(
            expression.operands[1], index_width);
        if (!source || !index) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            StringIndex {
                destination,
                *source,
                *index,
                is_signed_expression(
                    expression.operands[1]) });
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
            const auto builtin_array = [](const frontend::Type& type) {
                return type.spelling == "bit_vector"
                    || type.spelling == "std_logic_vector"
                    || type.spelling == "std_ulogic_vector"
                    || type.spelling == "signed"
                    || type.spelling == "unsigned";
            };
            const bool expected_array = expected_type->vhdl_array.has_value()
                || builtin_array(*expected_type);
            const bool source_array = source_type->vhdl_array.has_value()
                || builtin_array(*source_type);
            if (expected_array && source_array) {
                const bool same_builtin = builtin_array(*expected_type)
                    && builtin_array(*source_type)
                    && (expected_type->spelling == source_type->spelling
                        || ((expected_type->spelling == "std_logic_vector"
                                || expected_type->spelling
                                    == "std_ulogic_vector")
                            && (source_type->spelling == "std_logic_vector"
                                || source_type->spelling
                                    == "std_ulogic_vector")));
                const bool same_nominal = !expected_type->nominal_type.empty()
                    && expected_type->nominal_type
                        == source_type->nominal_type;
                if (same_builtin || same_nominal) {
                    return true;
                }
                report(
                    "FSIM-ELAB-VHARRAY-006",
                    "VHDL array values require the same nominal type; "
                    "expected '"
                        + expected_type->spelling
                        + "' but found '" + source_type->spelling + "'",
                    expression.span);
                return false;
            }
            if (expected_array != source_array) {
                return true;
            }
            const bool expected_enumeration = !expected_type->enumeration_literals.empty();
            const bool source_enumeration = !source_type->enumeration_literals.empty();
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
                "type; expected '"
                    + expected_type->spelling
                    + "' but found '" + source_type->spelling + "'",
                expression.span);
            return false;
        };
    if (language_ == frontend::Language::Vhdl2008) {
        auto protected_method = lower_vhdl_protected_expression(
            expression, expected_width, expected_type);
        if (protected_method.handled) {
            return protected_method.value;
        }
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && expression.text.starts_with("'")
        && !expression.operands.empty()
        && expression.operands.front().kind
            == ExpressionKind::Identifier) {
        const auto& prefix = expression.operands.front().text;
        const auto* prefix_type_mark = visible_type_mark(prefix);
        const bool scalar_value_attribute = expression.text == "'pos" || expression.text == "'val"
            || expression.text == "'succ" || expression.text == "'pred"
            || expression.text == "'leftof"
            || expression.text == "'rightof";
        const auto* prefix_object = object_type(prefix);
        const auto scalar = [&](const frontend::Type* type) {
            return type != nullptr && !is_vhdl_array_like(*type)
                && type->packed_members.empty();
        };
        const bool scalar_attribute = scalar_value_attribute
            || expression.text == "'left"
            || expression.text == "'right"
            || expression.text == "'low"
            || expression.text == "'high"
            || expression.text == "'length"
            || expression.text == "'ascending"
            || expression.text == "'range"
            || expression.text == "'reverse_range";
        if (((scalar(prefix_type_mark) || scalar(prefix_object))
                && scalar_attribute)
            || scalar_value_attribute) {
            return lower_vhdl_scalar_attribute(
                expression, expected_type);
        }
    }
    if (expected_type != nullptr
        && expected_type->systemverilog_virtual_interface) {
        if (const auto literal_name
            = systemverilog_interface_literal_name(expression)) {
            const auto literal = systemverilog_interface_literals_.find(
                *literal_name);
            if (literal != systemverilog_interface_literals_.end()) {
                const auto destination = allocate_register(
                    64, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    PackedLogic4::from_aval_bval(
                        64, literal->second.handle, 0) });
                return destination;
            }
        }
    }
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto local = locals_.find(expression.text);
            local != locals_.end()) {
            if (!enumeration_context_compatible(object_type(
                    expression.text))) {
                return std::nullopt;
            }
            return local->second;
        }
        const auto found = signals_.find(expression.text);
        if (found != signals_.end()) {
            const auto& signal = design_.signal_info_[found->second];
            const auto* type = visible_type(expression.text);
            if (!enumeration_context_compatible(type)) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                signal.width,
                type != nullptr
                    ? type->domain
                    : signal.source_domain);
            if (signal.width != 0) {
                process_.operations.emplace_back(
                    ReadSignal {
                        destination,
                        found->second,
                        sample_concurrent_assertion_reads_
                            ? SignalReadKind::sampled
                            : SignalReadKind::current });
            }
            return destination;
        }
        if (const auto selected = packed_member_reference(expression.text)) {
            if (!enumeration_context_compatible(object_type(
                    expression.text))) {
                return std::nullopt;
            }
            const auto base_width = infer_width(Expression {
                ExpressionKind::Identifier,
                selected->base,
                { },
                expression.span });
            const auto member_width = selected->member->width();
            if (!base_width || !member_width
                || *member_width == 0
                || selected->lsb_offset
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
                Expression {
                    ExpressionKind::Identifier,
                    selected->base,
                    { },
                    expression.span },
                *base_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                *member_width,
                selected->member->domain);
            process_.operations.emplace_back(Extract {
                destination,
                *source,
                static_cast<std::uint32_t>(
                    selected->lsb_offset),
                static_cast<std::uint32_t>(*member_width) });
            auto result = destination;
            for (const auto& tagged : selected->unions) {
                if (tagged.tag_width == 0) {
                    continue;
                }
                if (tagged.tag_offset
                        > std::numeric_limits<std::uint32_t>::max()
                    || tagged.tag_width == 0
                    || tagged.tag_width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-SVUNION-001",
                        "tagged-union member has no executable tag "
                        "layout",
                        expression.span);
                    return std::nullopt;
                }
                const auto tag = allocate_register(
                    tagged.tag_width,
                    register_domain(*source));
                process_.operations.emplace_back(Extract {
                    tag,
                    *source,
                    static_cast<std::uint32_t>(tagged.tag_offset),
                    static_cast<std::uint32_t>(tagged.tag_width) });
                const auto expected_tag = allocate_register(
                    tagged.tag_width,
                    register_domain(*source));
                process_.operations.emplace_back(LoadConstant {
                    expected_tag,
                    unsigned_value(tagged.tag, tagged.tag_width) });
                const auto active = allocate_register(
                    1, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::case_equal,
                    active,
                    tag,
                    expected_tag });
                const auto inactive = allocate_register(
                    *member_width,
                    selected->member->domain);
                auto inactive_value = PackedLogic4(
                    *member_width,
                    is_two_state_domain(selected->member->domain)
                        ? Logic4::zero
                        : Logic4::x);
                if (selected->member->domain
                    == frontend::ValueDomain::Logic9) {
                    inactive_value.fill(runtime::Logic9::x);
                }
                process_.operations.emplace_back(LoadConstant {
                    inactive, std::move(inactive_value) });
                const auto checked = allocate_register(
                    *member_width,
                    selected->member->domain);
                process_.operations.emplace_back(ConditionalSelect {
                    checked, active, result, inactive });
                result = checked;
            }
            return result;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expected_type != nullptr
            && !expected_type->enumeration_literals.empty()) {
            const auto ordinal = vhdl_enumeration_ordinal(
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
            process_.operations.emplace_back(LoadConstant {
                destination,
                unsigned_value(
                    static_cast<std::uint64_t>(*ordinal),
                    expected_width) });
            return destination;
        }
        {
            if (!report_unsupported_cross_root_reference(
                    expression.text, expression.span)) {
                report(
                    "FSIM-ELAB-040",
                    "unknown identifier '" + expression.text + "'",
                    expression.span);
            }
            return std::nullopt;
        }
    }
    constexpr std::string_view tagged_prefix { "@sv-tagged:" };
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with(tagged_prefix)) {
        if (expected_type == nullptr
            || expected_type->packed_aggregate
                != frontend::PackedAggregateKind::TaggedUnion
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-SVUNION-001",
                "tagged-union construction requires one member value "
                "and a contextual tagged-union type",
                expression.span);
            return std::nullopt;
        }
        const auto member_name = std::string {
            std::string_view { expression.text }.substr(
                tagged_prefix.size())
        };
        Expression pattern {
            ExpressionKind::Aggregate,
            "sv-pattern",
            expression.operands,
            expression.span
        };
        pattern.aggregate_choices = { "@key" };
        pattern.aggregate_choice_expressions = { { Expression {
            ExpressionKind::Identifier,
            member_name,
            { },
            expression.span } } };
        return lower_sv_packed_pattern(
            pattern, expected_width, *expected_type);
    }
    if (expression.kind == ExpressionKind::Aggregate) {
        if (language_
                == frontend::Language::SystemVerilog2017
            && expression.text == "sv-pattern") {
            if (expected_type != nullptr
                && !expected_type->packed_members.empty()) {
                return lower_sv_packed_pattern(
                    expression, expected_width, *expected_type);
            }
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "a SystemVerilog assignment pattern requires a "
                "supported contextual whole-container or packed "
                "aggregate target",
                expression.span);
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expected_type != nullptr
            && (expected_type->vhdl_array
                || (expected_type->packed_range
                    && expected_type->packed_members.empty()))) {
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
        process_.operations.emplace_back(LoadConstant {
            destination,
            default_packed_value(
                *expected_type, expected_width) });
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
                const auto& member = expected_type->packed_members[member_index];
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
                frontend::Type scalar_type;
                const frontend::Type* member_type = &scalar_type;
                if (!member.nested_types.empty()) {
                    member_type = &member.nested_types.front();
                } else {
                    scalar_type.domain = member.domain;
                    scalar_type.spelling = member.spelling;
                    scalar_type.packed_range = member.packed_range;
                    scalar_type.packed_range_expression = member.packed_range_expression;
                    scalar_type.is_signed = member.is_signed;
                }
                const auto actual_type = vhdl_expression_type(value);
                if (actual_type
                    && !vhdl_callable_type_matches(
                        *member_type, *actual_type)) {
                    report(
                        is_two_state_domain(member_type->domain)
                                && !is_two_state_domain(actual_type->domain)
                            ? "FSIM-ELAB-VHAGG-007"
                            : "FSIM-ELAB-VHAGG-009",
                        is_two_state_domain(member_type->domain)
                                && !is_two_state_domain(actual_type->domain)
                            ? "two-state record aggregate element '"
                                + member.name
                                + "' requires an explicit conversion"
                            : "record aggregate element '" + member.name
                                + "' requires its exact contextual subtype",
                        span);
                    valid = false;
                    return;
                }
                if (!actual_type
                    && (member_type->vhdl_array
                        || !member_type->packed_members.empty())
                    && !vhdl_expression_matches_type(
                        value, *member_type)) {
                    report(
                        "FSIM-ELAB-VHAGG-009",
                        "record aggregate element '" + member.name
                            + "' requires its exact contextual subtype",
                        span);
                    valid = false;
                    return;
                }
                const auto lowered = lower_expression(
                    value,
                    static_cast<std::size_t>(*member_width),
                    member_type);
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
                if (register_domain(*lowered) != member_type->domain) {
                    report(
                        "FSIM-ELAB-VHAGG-009",
                        "record aggregate element '" + member.name
                            + "' has an incompatible state domain",
                        span);
                    valid = false;
                    return;
                }
                if (!member_type->enumeration_literals.empty()) {
                    emit_enumeration_check(*lowered, *member_type);
                } else if (
                    member_type->domain
                        == frontend::ValueDomain::Integer
                    && !member_type->vhdl_physical
                    && member_type->nominal_type != "@builtin:time") {
                    emit_integer_check(
                        *lowered, member_type->integer_range);
                }
                process_.operations.emplace_back(Insert {
                    destination,
                    destination,
                    *lowered,
                    static_cast<std::uint32_t>(
                        member.lsb_offset) });
                assigned[member_index] = true;
            };
        for (std::size_t index = 0;
            index < expression.operands.size();
            ++index) {
            const auto& choice = expression.aggregate_choices[index];
            const auto& choice_expressions = expression.aggregate_choice_expressions[index];
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
            if (choice_expressions.empty()
                || std::ranges::any_of(
                    choice_expressions,
                    [](const Expression& member_choice) {
                        return member_choice.kind
                            != ExpressionKind::Identifier
                            || member_choice.text == "others";
                    })) {
                report(
                    "FSIM-ELAB-VHAGG-008",
                    "record aggregate choices must be one or more "
                    "element names",
                    expression.operands[index].span);
                valid = false;
                continue;
            }
            for (const auto& member_choice : choice_expressions) {
                const auto member = std::ranges::find(
                    expected_type->packed_members,
                    member_choice.text,
                    &frontend::PackedMember::name);
                if (member
                    == expected_type->packed_members.end()) {
                    report(
                        "FSIM-ELAB-VHAGG-003",
                        "record aggregate type '"
                            + expected_type->spelling
                            + "' has no element '"
                            + member_choice.text + "'",
                        member_choice.span);
                    valid = false;
                    continue;
                }
                insert_member(
                    static_cast<std::size_t>(std::distance(
                        expected_type->packed_members.begin(),
                        member)),
                    expression.operands[index],
                    member_choice.span);
            }
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
            ? std::optional<RegisterId> { destination }
            : std::nullopt;
    }
    if (expression.kind == ExpressionKind::IntegerLiteral
        || expression.kind == ExpressionKind::BooleanLiteral
        || expression.kind == ExpressionKind::LogicLiteral
        || expression.kind == ExpressionKind::StringLiteral) {
        const auto scalar_kind = expected_type != nullptr
                && expected_type->systemverilog_scalar
                    != frontend::SystemVerilogScalarKind::None
            ? expected_type->systemverilog_scalar
            : expression.systemverilog_scalar_kind;
        if (scalar_kind != frontend::SystemVerilogScalarKind::None
            && expression.kind == ExpressionKind::IntegerLiteral) {
            std::string error;
            const auto evaluated = frontend::evaluate_systemverilog_scalar_constant(
                expression, { }, { }, error);
            const auto converted = evaluated
                ? frontend::convert_systemverilog_scalar_constant(
                      *evaluated,
                      scalar_kind,
                      error)
                : std::nullopt;
            if (!converted) {
                report(
                    "FSIM-ELAB-SVSCALAR-001",
                    "cannot convert scalar literal '"
                        + expression.text + "': " + error,
                    expression.span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                expected_width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                destination,
                PackedLogic4::from_aval_bval(
                    expected_width, converted->bits, 0) });
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expected_type != nullptr
            && !expected_type->enumeration_literals.empty()) {
            const auto ordinal = vhdl_enumeration_ordinal(
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
            process_.operations.emplace_back(LoadConstant {
                destination,
                unsigned_value(
                    static_cast<std::uint64_t>(*ordinal),
                    expected_width) });
            return destination;
        }
        const auto literal = literal_value(expression, expected_width, language_);
        if (!literal) {
            report(
                "FSIM-ELAB-041",
                "unsupported or malformed literal '" + expression.text + "'",
                expression.span);
            return std::nullopt;
        }
        auto literal_domain = literal->domain;
        if (language_ == frontend::Language::Vhdl2008
            && expected_type != nullptr) {
            if (expression.kind == ExpressionKind::IntegerLiteral
                && expected_type->domain
                    == frontend::ValueDomain::Integer) {
                literal_domain = frontend::ValueDomain::Integer;
            } else if (
                (expression.kind == ExpressionKind::LogicLiteral
                    || expression.kind == ExpressionKind::StringLiteral)
                && literal_domain == frontend::ValueDomain::Bit2
                && expected_type->domain
                    == frontend::ValueDomain::Logic9) {
                literal_domain = frontend::ValueDomain::Logic9;
            }
        }
        const auto destination = allocate_register(
            literal->value.width(), literal_domain);
        process_.operations.emplace_back(
            LoadConstant { destination, std::move(literal->value) });
        return destination;
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && expression.operands.size() == 1
        && (locals_.contains(expression.text)
            || signals_.contains(expression.text)
            || packed_member_reference(expression.text))) {
        return lower_expression(
            Expression { ExpressionKind::Index, "index",
                { Expression { ExpressionKind::Identifier,
                      expression.text, { }, expression.span },
                    expression.operands[0] },
                expression.span },
            expected_width);
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && expression.operands.size() == 1) {
        auto conversion = lower_vhdl_conversion_expression(
            expression, expected_width, expected_type);
        if (conversion.handled) {
            return conversion.value;
        }
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
        const auto source_width = infer_width(expression.operands[0]);
        const auto index = static_integer_value(
            expression.operands[1]);
        if (!source_width) {
            report(
                "FSIM-ELAB-068",
                "a bit-select requires an inferable packed source",
                expression.span);
            return std::nullopt;
        }
        const auto source = lower_expression(
            expression.operands[0], *source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(1, register_domain(*source));
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
            process_.operations.emplace_back(Extract {
                destination,
                *source,
                static_cast<std::uint32_t>(*offset),
                1 });
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
                DynamicExtract {
                    destination,
                    *source,
                    *selection });
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
        const auto source_width = infer_width(expression.operands[0]);
        const auto selection = source_width
            ? constant_slice_selection(
                  expression, *source_width)
            : std::nullopt;
        if (source_width && !selection
            && language_
                == frontend::Language::SystemVerilog2017
            && (expression.text == "+:"
                || expression.text == "-:")
            && !static_integer_value(
                expression.operands[1])) {
            const auto selected_width = static_integer_value(
                expression.operands[2]);
            if (!selected_width || *selected_width <= 0
                || static_cast<std::uint64_t>(*selected_width)
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVEXPR-004",
                    "a runtime-base packed part-select requires a "
                    "positive locally constant host-representable "
                    "result width",
                    expression.operands[2].span);
                return std::nullopt;
            }
            {
                const auto dynamic_selection = lower_dynamic_index(
                    expression.operands[0],
                    expression.operands[1],
                    *source_width,
                    0,
                    expression.span);
                if (!dynamic_selection) {
                    return std::nullopt;
                }
                const auto source = lower_expression(
                    expression.operands[0], *source_width);
                if (!source) {
                    return std::nullopt;
                }
                const auto width = static_cast<std::uint32_t>(
                    *selected_width);
                const auto destination = allocate_register(
                    width, register_domain(*source));
                process_.operations.emplace_back(
                    DynamicPartSelect {
                        destination,
                        *source,
                        dynamic_selection->index,
                        dynamic_selection->left,
                        dynamic_selection->right,
                        width,
                        expression.text == "+:",
                        dynamic_selection->left
                            >= dynamic_selection->right,
                        is_two_state_domain(
                            register_domain(*source)) });
                return destination;
            }
        }
        if (source_width && !selection
            && language_ == frontend::Language::Vhdl2008
            && (!static_integer_value(expression.operands[1])
                || !static_integer_value(expression.operands[2]))) {
            return lower_vhdl_dynamic_slice_expression(
                expression, *source_width, expected_width);
        }
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
        const auto source = lower_expression(
            expression.operands[0], *source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            selection->width,
            register_domain(*source));
        process_.operations.emplace_back(Extract {
            destination,
            *source,
            static_cast<std::uint32_t>(selection->offset),
            static_cast<std::uint32_t>(selection->width) });
        return destination;
    }
    if (language_ != frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Replication) {
        std::string count_error;
        const auto count = expression.operands.empty()
            ? std::nullopt
            : evaluate_constant_expression(
                  expression.operands[0],
                  { },
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
            const auto& operand_expression = expression.operands[index];
            const auto operand_width = infer_width(operand_expression);
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
        const auto repetition_count = static_cast<std::uint64_t>(*count);
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
            process_.operations.emplace_back(Concatenate {
                group,
                std::move(group_operands),
                static_cast<std::uint32_t>(group_width) });
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
                    const auto combined_width = result_width + block_width;
                    const auto combined = allocate_register(
                        combined_width, result_domain);
                    process_.operations.emplace_back(
                        Concatenate {
                            combined,
                            { *result, block },
                            static_cast<std::uint32_t>(
                                combined_width) });
                    result = combined;
                    result_width = combined_width;
                }
            }
            remaining >>= 1U;
            if (remaining != 0) {
                const auto doubled_width = block_width * 2U;
                const auto doubled = allocate_register(
                    doubled_width, result_domain);
                process_.operations.emplace_back(
                    Concatenate {
                        doubled,
                        { block, block },
                        static_cast<std::uint32_t>(
                            doubled_width) });
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
            if (operand_expression.kind == ExpressionKind::Replication
                && !operand_expression.operands.empty()) {
                std::string count_error;
                const auto count = evaluate_constant_expression(
                    operand_expression.operands.front(), { }, count_error);
                if (count && *count == 0) {
                    continue;
                }
            }
            const auto operand_width = infer_width(operand_expression);
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
                "concatenation result width is outside the supported "
                "range",
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(width, result_domain);
        process_.operations.emplace_back(Concatenate {
            destination,
            std::move(operands),
            static_cast<std::uint32_t>(width) });
        return destination;
    }
    return ExpressionAttempt { };
}

} // namespace fsim::elaboration
