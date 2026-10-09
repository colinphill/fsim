// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <limits>
#include <iostream>
#include <iterator>
#include <map>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

frontend::SourceSpan compiled_resolution_source_span(
    const semantic::CompiledDesign& compiled,
    const semantic::SourceSpanId source)
{
    frontend::SourceSpan result;
    const auto& spans = compiled.semantics.source_spans();
    if (!source.valid() || source.value() >= spans.size()) {
        return result;
    }
    const auto& span = spans[source.value()];
    result.source_name = span.logical_name;
    result.begin = {
        static_cast<std::size_t>(span.begin.offset),
        span.begin.line,
        span.begin.column,
    };
    result.end = {
        static_cast<std::size_t>(span.end.offset),
        span.end.line,
        span.end.column,
    };
    const auto& files = compiled.semantics.source_files();
    if (span.file.valid() && span.file.value() < files.size()) {
        result.physical_source_name = files[span.file.value()].physical_name;
    }
    return result;
}

const semantic::sv::Declaration* compiled_systemverilog_declaration(
    const semantic::CompiledDesign& compiled,
    const semantic::DeclarationId id)
{
    const auto found = compiled.find_declaration(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

const semantic::sv::TypeDefinition* compiled_systemverilog_type(
    const semantic::CompiledDesign& compiled,
    const semantic::TypeId id)
{
    const auto found = compiled.find_type(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

const semantic::sv::Statement* compiled_systemverilog_statement(
    const semantic::CompiledDesign& compiled,
    const semantic::StatementId id)
{
    const auto found = compiled.find_statement(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

const semantic::sv::Expression* compiled_systemverilog_expression(
    const semantic::CompiledDesign& compiled,
    const semantic::ExpressionId id)
{
    const auto found = compiled.find_expression(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

bool compiled_resolver_returns_first(
    const semantic::CompiledDesign& compiled,
    const semantic::sv::Declaration& function)
{
    if (!function.callable || function.callable->formals.size() != 1U
        || function.statements.size() != 1U) {
        return false;
    }
    const auto* formal = compiled_systemverilog_declaration(
        compiled, function.callable->formals.front());
    const auto* statement = compiled_systemverilog_statement(
        compiled, function.statements.front());
    if (formal == nullptr || statement == nullptr
        || statement->kind
            != semantic::sv::StatementKind::return_statement
        || !statement->value) {
        return false;
    }
    const auto* value = compiled_systemverilog_expression(
        compiled, *statement->value);
    if (value == nullptr
        || value->kind != semantic::sv::ExpressionKind::index
        || value->operands.size() != 2U) {
        return false;
    }
    const auto* drivers = compiled_systemverilog_expression(
        compiled, value->operands.front());
    const auto* index = compiled_systemverilog_expression(
        compiled, value->operands.back());
    return drivers != nullptr && index != nullptr
        && drivers->kind == semantic::sv::ExpressionKind::name
        && drivers->text == formal->name
        && index->kind == semantic::sv::ExpressionKind::integer_literal
        && index->text == "0";
}

} // namespace

HierarchyBuilder::ElementNetRewriteCensus
HierarchyBuilder::rewrite_eligible_element_net_families()
{
    ElementNetRewriteCensus census;

    struct ElementNetFamily {
        ContainerObjectId object { };
        SignalId proxy { };
        std::uint32_t element_width { };
        std::uint32_t element_count { };
        std::int32_t declared_left { };
        std::int32_t declared_right { };
        std::size_t dimension_count { };
        bool readable { };
        bool writable { };
        std::vector<std::string> leaf_names;
        std::vector<std::string> leaf_suffixes;
        std::vector<PackedLogic4> initial_elements;
        std::vector<SignalId> leaves;
    };

    // Reuse declaration indexes once. Besides avoiding repeated full scans,
    // this lets every candidate fail before any signal or alias is appended.
    std::unordered_map<ContainerObjectId, const ContainerSignalAlias*>
        alias_by_object;
    std::unordered_set<ContainerObjectId> duplicate_alias_objects;
    std::unordered_map<SignalId, ContainerObjectId> first_object_by_signal;
    std::unordered_set<SignalId> duplicate_alias_signals;
    for (const auto& alias : design_.container_signal_aliases_) {
        if (!alias_by_object.emplace(alias.object, &alias).second) {
            duplicate_alias_objects.insert(alias.object);
        }
        const auto [signal_position, signal_inserted]
            = first_object_by_signal.emplace(alias.signal, alias.object);
        if (!signal_inserted && signal_position->second != alias.object) {
            duplicate_alias_signals.insert(alias.signal);
        }
    }
    std::unordered_set<ContainerObjectId> already_aggregated_objects;
    for (const auto& alias : design_.container_aggregate_signal_aliases_) {
        already_aggregated_objects.insert(alias.object);
    }
    std::unordered_set<ContainerObjectId> existing_element_objects;
    for (const auto& alias : design_.container_element_signal_aliases_) {
        existing_element_objects.insert(alias.object);
    }
    std::unordered_map<SignalId, std::vector<std::string>> names_by_signal;
    names_by_signal.reserve(design_.signal_by_name_.size());
    for (const auto& [name, signal] : design_.signal_by_name_) {
        names_by_signal[signal].push_back(name);
    }
    for (auto& [signal, names] : names_by_signal) {
        (void)signal;
        std::ranges::sort(names);
    }

    // Terminal-level switch and timing objects need their original signal
    // identity. Exclude only the families they actually mention.
    std::unordered_set<SignalId> unsupported_terminal_signals;
    for (std::size_t process_index = 0U;
        process_index < design_.process_count(); ++process_index) {
        const auto process = design_.process_view(process_index);
        if (process.switch_source()) {
            unsupported_terminal_signals.insert(*process.switch_source());
        }
        if (process.switch_target()) {
            unsupported_terminal_signals.insert(*process.switch_target());
        }
        if (process.switch_control()) {
            unsupported_terminal_signals.insert(*process.switch_control());
        }
    }
    const auto record_path_terminals = [&](
        const ModulePathExpression& expression) {
        for (const auto& node : expression.nodes) {
            if (node.operation == ModulePathExpressionOperator::terminal) {
                unsupported_terminal_signals.insert(node.terminal.signal);
            }
        }
    };
    for (const auto& path : design_.verilog_specify_paths_) {
        for (const auto& terminal : path.sources) {
            unsupported_terminal_signals.insert(terminal.signal);
        }
        for (const auto& terminal : path.destinations) {
            unsupported_terminal_signals.insert(terminal.signal);
        }
        record_path_terminals(path.condition_program);
        record_path_terminals(path.data_source_program);
    }
    for (const auto& check : design_.verilog_timing_checks_) {
        unsupported_terminal_signals.insert(check.reference.terminal.signal);
        if (check.data) {
            unsupported_terminal_signals.insert(check.data->terminal.signal);
        }
        if (check.notifier) {
            unsupported_terminal_signals.insert(*check.notifier);
        }
        if (check.delayed_reference) {
            unsupported_terminal_signals.insert(
                check.delayed_reference->signal);
        }
        if (check.delayed_data) {
            unsupported_terminal_signals.insert(check.delayed_data->signal);
        }
        for (const auto& node : check.reference.condition.nodes) {
            if (node.operation == ModulePathExpressionOperator::terminal) {
                unsupported_terminal_signals.insert(node.terminal.signal);
            }
        }
        if (check.data) {
            for (const auto& node : check.data->condition.nodes) {
                if (node.operation
                    == ModulePathExpressionOperator::terminal) {
                    unsupported_terminal_signals.insert(node.terminal.signal);
                }
            }
        }
    }

    std::vector<ElementNetFamily> families;
    families.reserve(design_.container_object_info_.size());
    std::unordered_set<std::string> planned_leaf_names;
    for (const auto& object_info : design_.container_object_info_) {
        const auto object_id = object_info.id;
        if (object_id >= design_.container_objects_.size()
            || object_info.is_port || object_info.slice_alias
            || duplicate_alias_objects.contains(object_id)
            || already_aggregated_objects.contains(object_id)
            || existing_element_objects.contains(object_id)) {
            continue;
        }

        const auto& type = object_info.type;
        if (!type.fixed || type.queue || type.associative
            || type.string_indices || type.two_state
            || type.element_kind != ContainerElementKind::Packed
            || type.element_width == 0U
            || type.scalar_kind != frontend::SystemVerilogScalarKind::None
            || type.dimensions.empty()
            || !type.element_types.empty()
            || !type.element_nominal_type.empty()) {
            continue;
        }

        const auto alias_position = alias_by_object.find(object_id);
        if (alias_position == alias_by_object.end()) {
            continue;
        }
        const auto& alias = *alias_position->second;
        if (!alias.readable || !alias.writable) {
            continue;
        }

        std::uint64_t count64 = 1U;
        bool count_overflow { };
        for (const auto& [left, right] : type.dimensions) {
            const auto extent = static_cast<std::uint64_t>(
                left >= right
                    ? static_cast<std::int64_t>(left) - right
                    : static_cast<std::int64_t>(right) - left) + 1U;
            if (extent > std::numeric_limits<std::uint32_t>::max()
                || count64 > std::numeric_limits<std::uint32_t>::max()
                    / extent) {
                count_overflow = true;
                break;
            }
            count64 *= extent;
        }
        if (count_overflow) {
            continue;
        }
        const auto total_width64 = count64 * type.element_width;
        if (total_width64 > std::numeric_limits<std::uint32_t>::max()
            || total_width64
                > maximum_container_storage_bytes * 8U) {
            continue;
        }

        const auto& object = design_.container_objects_[object_id];
        if (object.slice_alias || object.initial_value.type != type
            || object.initial_value.elements.size() != count64
            || !object.initial_value.string_elements.empty()
            || !object.initial_value.nested_elements.empty()) {
            continue;
        }
        const auto proxy = alias.signal;
        if (proxy >= design_.signal_info_.size()
            || proxy >= design_.signals_.size()
            || duplicate_alias_signals.contains(proxy)) {
            continue;
        }
        const auto& proxy_info = design_.signal_info_[proxy];
        const auto& proxy_signal = design_.signals_[proxy];
        if ((proxy_info.systemverilog_net_type != "wire"
                && proxy_info.systemverilog_net_type != "tri")
            || proxy_info.source_domain != frontend::ValueDomain::Logic4
            || proxy_info.is_port
            || proxy_info.vhdl_array != nullptr
            || proxy_info.vhdl_access != nullptr
            || proxy_info.vhdl_physical != nullptr
            || !proxy_info.vhdl_mode_view_bindings.empty()
            || proxy_info.width != total_width64
            || proxy_signal.initial_value.width() != total_width64
            || proxy_signal.initial_value.is_logic9()
            || proxy_signal.value_kind != ValueKind::logic4
            || proxy_signal.charge_strength
            || proxy_signal.charge_decay
            || proxy_signal.implicit_driver) {
            continue;
        }
        record_lowering_census(census.shape_qualified_families);
        record_lowering_census(
            census.shape_qualified_leaves,
            static_cast<std::size_t>(count64));
        if (unsupported_terminal_signals.contains(proxy)) {
            record_lowering_census(
                census.terminal_fallback_families);
            record_lowering_census(
                census.terminal_fallback_leaves,
                static_cast<std::size_t>(count64));
            continue;
        }
        if (resolver_by_signal_.contains(proxy)) {
            record_lowering_census(
                census.resolver_fallback_families);
            continue;
        }

        ElementNetFamily family;
        family.object = object_id;
        family.proxy = proxy;
        family.element_width = type.element_width;
        family.element_count = static_cast<std::uint32_t>(count64);
        family.declared_left = type.dimensions.front().first;
        family.declared_right = type.dimensions.front().second;
        family.dimension_count = type.dimensions.size();
        family.readable = alias.readable;
        family.writable = alias.writable;
        family.leaf_names.reserve(family.element_count);
        family.leaf_suffixes.reserve(family.element_count);
        family.initial_elements.reserve(family.element_count);

        bool valid_initial_cache_shape = true;
        bool invalid_initial_cache_shape = false;
        bool ordinal_out_of_range = false;
        bool name_collision = false;
        std::unordered_set<std::string> local_leaf_names;
        std::vector<std::string> newly_planned_leaf_names;
        const auto add_leaf_name = [&](std::string leaf_name) {
            if (!local_leaf_names.insert(leaf_name).second) {
                return;
            }
            if (const auto existing
                = design_.signal_by_name_.find(leaf_name);
                existing != design_.signal_by_name_.end()
                && existing->second != proxy) {
                name_collision = true;
            }
            if (planned_leaf_names.insert(leaf_name).second) {
                newly_planned_leaf_names.push_back(std::move(leaf_name));
            } else {
                name_collision = true;
            }
        };
        for (std::uint32_t ordinal = 0U;
            ordinal < family.element_count;
            ++ordinal) {
            // ContainerValue uses row-major declared order: the last
            // unpacked dimension varies fastest, regardless of direction.
            auto remaining = static_cast<std::uint64_t>(ordinal);
            auto stride = count64;
            std::string suffix;
            for (const auto& [left, right] : type.dimensions) {
                const auto extent = static_cast<std::uint64_t>(
                    left >= right
                        ? static_cast<std::int64_t>(left) - right
                        : static_cast<std::int64_t>(right) - left) + 1U;
                stride /= extent;
                const auto position = remaining / stride;
                remaining %= stride;
                const auto source_index = static_cast<std::int64_t>(left)
                    + (left >= right
                            ? -static_cast<std::int64_t>(position)
                            : static_cast<std::int64_t>(position));
                if (source_index < std::numeric_limits<std::int32_t>::min()
                    || source_index > std::numeric_limits<std::int32_t>::max()) {
                    valid_initial_cache_shape = false;
                    ordinal_out_of_range = true;
                    break;
                }
                suffix += "[" + std::to_string(source_index) + "]";
            }
            if (!valid_initial_cache_shape) {
                break;
            }

            const auto offset
                = (static_cast<std::size_t>(family.element_count)
                    - static_cast<std::size_t>(ordinal) - 1U)
                * static_cast<std::size_t>(family.element_width);
            const auto& cached_element
                = object.initial_value.elements[ordinal];
            if (cached_element.width() != family.element_width
                || cached_element.is_logic9()) {
                valid_initial_cache_shape = false;
                invalid_initial_cache_shape = true;
                break;
            }
            // The readable aggregate proxy is authoritative for current leaf
            // values. Its container cache can still hold default X values
            // while an undriven wire proxy begins at Z.
            auto initial = proxy_signal.initial_value.extract_bits(
                offset, family.element_width);

            const auto canonical_name = object_info.name + suffix;
            const auto names = names_by_signal.find(proxy);
            add_leaf_name(canonical_name);
            if (names != names_by_signal.end()) {
                for (const auto& public_name : names->second) {
                    add_leaf_name(public_name + suffix);
                }
            }
            family.leaf_names.push_back(canonical_name);
            family.leaf_suffixes.push_back(std::move(suffix));
            family.initial_elements.push_back(std::move(initial));
        }
        if (!valid_initial_cache_shape || name_collision) {
            if (invalid_initial_cache_shape) {
                record_lowering_census(
                    census.invalid_initial_cache_shape_fallback_families);
            }
            if (ordinal_out_of_range) {
                record_lowering_census(
                    census.ordinal_range_fallback_families);
            }
            if (name_collision) {
                record_lowering_census(
                    census.name_collision_fallback_families);
            }
            for (const auto& name : newly_planned_leaf_names) {
                planned_leaf_names.erase(name);
            }
            continue;
        }
        families.push_back(std::move(family));
        record_lowering_census(census.candidate_families);
        record_lowering_census(
            census.candidate_leaves, families.back().element_count);
    }

    if (families.empty()) {
        return census;
    }

    // VHDL projected writes carry transaction semantics that still target the
    // original aggregate signal. Until aggregate projection publication is
    // certified against physical element aliases, keep only the referenced
    // proxy family on its retained representation. Inspect expanded operation
    // bindings so shared process templates are checked against their actual
    // signal IDs rather than their canonical body IDs. Do this only after a
    // candidate family exists so ordinary designs pay no process-scan cost.
    std::unordered_set<SignalId> candidate_proxies;
    candidate_proxies.reserve(families.size());
    for (const auto& family : families) {
        candidate_proxies.insert(family.proxy);
    }
    std::unordered_set<SignalId> projected_write_signals;
    for (std::size_t process_index = 0U;
        process_index < design_.process_count(); ++process_index) {
        const auto process = design_.process_view(process_index);
        const auto& process_operations = process.operations();
        for (std::size_t index = 0U;
            index < process_operations.size();
            ++index) {
            const auto& shared = process_operations[index];
            const bool is_projected_write
                = operation_holds<WriteProjected>(shared)
                || operation_holds<WriteProjectedWaveform>(shared)
                || operation_holds<WriteProjectedSlice>(shared)
                || operation_holds<WriteProjectedWaveformSlice>(shared)
                || operation_holds<WriteProjectedDynamicSlice>(shared)
                || operation_holds<
                    WriteProjectedWaveformDynamicSlice>(shared);
            if (!is_projected_write) {
                continue;
            }
            const auto operation = process_operations.expanded(index);
            visit_operation(
                [&](const auto& value) {
                    using Type = std::decay_t<decltype(value)>;
                    if constexpr (
                        std::is_same_v<Type, WriteProjected>
                        || std::is_same_v<Type, WriteProjectedWaveform>
                        || std::is_same_v<Type, WriteProjectedSlice>
                        || std::is_same_v<
                            Type, WriteProjectedWaveformSlice>
                        || std::is_same_v<
                            Type, WriteProjectedDynamicSlice>
                        || std::is_same_v<
                            Type, WriteProjectedWaveformDynamicSlice>) {
                        if (candidate_proxies.contains(value.signal)) {
                            projected_write_signals.insert(value.signal);
                            record_lowering_census(
                                census.projected_write_operations);
                        }
                    }
                },
                operation);
        }
    }
    std::erase_if(
        families,
        [&](const ElementNetFamily& family) {
            if (!projected_write_signals.contains(family.proxy)) {
                return false;
            }
            record_lowering_census(
                census.projected_fallback_families);
            record_lowering_census(
                census.projected_fallback_leaves,
                family.element_count);
            return true;
        });
    if (lowering_census_enabled_) {
        census.eligible_families = families.size();
        for (const auto& family : families) {
            census.eligible_leaves += family.element_count;
        }
    }
    if (families.empty()) {
        return census;
    }

    const auto signal_capacity = static_cast<std::uint64_t>(
        std::numeric_limits<SignalId>::max());
    std::uint64_t required_signal_count { };
    for (const auto& family : families) {
        required_signal_count += family.element_count;
    }
    if (static_cast<std::uint64_t>(design_.signals_.size())
            + required_signal_count
        > signal_capacity) {
        if (lowering_census_enabled_) {
            census.capacity_fallback_families = families.size();
            for (const auto& family : families) {
                census.capacity_fallback_leaves += family.element_count;
            }
        }
        return census;
    }

    for (auto& family : families) {
        family.leaves.reserve(family.element_count);
        const auto proxy_info = design_.signal_info_[family.proxy];
        const auto proxy_signal = design_.signals_[family.proxy];
        const auto names = names_by_signal.find(family.proxy);
        for (std::uint32_t ordinal = 0U;
            ordinal < family.element_count;
            ++ordinal) {
            const auto leaf_id = static_cast<SignalId>(
                design_.signals_.size());
            const auto& suffix = family.leaf_suffixes[ordinal];
            const auto& leaf_name = family.leaf_names[ordinal];
            auto leaf_info = proxy_info;
            leaf_info.id = leaf_id;
            leaf_info.name = leaf_name;
            leaf_info.width = family.element_width;
            leaf_info.is_port = false;
            leaf_info.direction = frontend::PortDirection::Unknown;
            auto leaf_signal = proxy_signal;
            leaf_signal.name = leaf_name;
            leaf_signal.initial_value = family.initial_elements[ordinal];
            // Hierarchy validation replaces this staging value with the
            // declared net type's real resolution kind before design freeze.
            leaf_signal.resolution = ResolutionKind::none;
            design_.signal_info_.push_back(std::move(leaf_info));
            design_.signals_.push_back(std::move(leaf_signal));
            family.leaves.push_back(leaf_id);
            design_.container_element_signal_aliases_.push_back(
                ContainerElementSignalAlias {
                    family.object,
                    ordinal,
                    leaf_id,
                    family.readable,
                    family.writable,
                });

            design_.signal_by_name_.insert_or_assign(leaf_name, leaf_id);
            if (names != names_by_signal.end()) {
                for (const auto& public_name : names->second) {
                    design_.signal_by_name_.insert_or_assign(
                        public_name + suffix, leaf_id);
                }
            }
        }
    }

    std::unordered_map<SignalId, std::size_t> family_by_proxy;
    family_by_proxy.reserve(families.size());
    std::unordered_map<ContainerObjectId, std::size_t> family_by_object;
    family_by_object.reserve(families.size());
    std::unordered_set<ContainerObjectId> family_objects;
    family_objects.reserve(families.size());
    for (std::size_t index = 0U; index < families.size(); ++index) {
        const auto& family = families[index];
        family_by_proxy.emplace(family.proxy, index);
        family_by_object.emplace(family.object, index);
        family_objects.insert(family.object);
        design_.container_aggregate_signal_aliases_.push_back(
            ContainerAggregateSignalAlias {
                family.object,
                family.proxy,
                family.readable,
                family.writable,
            });
    }
    std::erase_if(
        design_.container_signal_aliases_,
        [&](const ContainerSignalAlias& alias) {
            return family_objects.contains(alias.object);
        });
    if (lowering_census_enabled_) {
        census.physical_families = families.size();
        census.physical_leaves = static_cast<std::size_t>(
            required_signal_count);
    }

    const auto map_leaf_interval = [&](const SignalId proxy,
                                       const std::uint32_t offset,
                                       const std::uint32_t width)
        -> std::optional<std::pair<SignalId, std::uint32_t>> {
        const auto family_index = family_by_proxy.find(proxy);
        if (family_index == family_by_proxy.end() || width == 0U) {
            return std::nullopt;
        }
        const auto& family = families[family_index->second];
        const auto end = static_cast<std::uint64_t>(offset) + width;
        const auto total_width
            = static_cast<std::uint64_t>(family.element_width)
            * family.element_count;
        if (end > total_width) {
            return std::nullopt;
        }
        const auto first_lane = offset / family.element_width;
        const auto last_lane = static_cast<std::uint32_t>(
            (end - 1U) / family.element_width);
        if (first_lane != last_lane
            || first_lane >= family.element_count) {
            return std::nullopt;
        }
        const auto ordinal = family.element_count - first_lane - 1U;
        return std::pair {
            family.leaves[ordinal],
            offset % family.element_width,
        };
    };

    if (lowering_census_enabled_) {
        for (std::size_t process_index = 0U;
            process_index < design_.process_count(); ++process_index) {
            const auto process = design_.process_view(process_index);
            for (const auto& sensitivity : process.static_sensitivity()) {
                if (!family_by_proxy.contains(sensitivity.signal)) {
                    continue;
                }
                ++census.proxy_sensitivity_entries;
                if (!map_leaf_interval(
                        sensitivity.signal,
                        sensitivity.offset,
                        sensitivity.width)) {
                    ++census.sensitivity_range_fallbacks;
                }
            }
        }
    }

    const auto register_use_is_known = [](
        const Operation& operation,
        const RegisterId register_id) {
        bool known = false;
        bool used = false;
        visit_operation(
            [&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, LoadConstant>
                    || std::is_same_v<Type, ReadSignal>
                    || std::is_same_v<Type, ReadSimulationTime>
                    || std::is_same_v<Type, SignalEvent>
                    || std::is_same_v<Type, SignalLastValue>
                    || std::is_same_v<Type, SignalLastEvent>
                    || std::is_same_v<Type, SignalActive>
                    || std::is_same_v<Type, SignalLastActive>
                    || std::is_same_v<Type, SignalDriving>
                    || std::is_same_v<Type, SignalDrivingValue>
                    || std::is_same_v<Type, EventTriggered>) {
                    known = true;
                    used = value.destination == register_id;
                } else if constexpr (std::is_same_v<Type, Extract>) {
                    known = true;
                    used = value.source == register_id
                        || value.destination == register_id;
                } else if constexpr (
                    std::is_same_v<Type, CopyRegister>
                    || std::is_same_v<Type, ConvertToTwoState>
                    || std::is_same_v<Type, UnaryNot>
                    || std::is_same_v<Type, LogicalNot>
                    || std::is_same_v<Type, Reduction>
                    || std::is_same_v<Type, CountOnes>
                    || std::is_same_v<Type, CountBits>
                    || std::is_same_v<Type, DynamicExtract>
                    || std::is_same_v<Type, DynamicPartSelect>
                    || std::is_same_v<Type, IntegerUnary>) {
                    known = true;
                    used = value.source == register_id
                        || value.destination == register_id;
                    if constexpr (
                        std::is_same_v<Type, DynamicExtract>) {
                        used = used
                            || value.selection.index == register_id;
                    } else if constexpr (
                        std::is_same_v<Type, DynamicPartSelect>) {
                        used = used || value.base == register_id;
                    }
                } else if constexpr (
                    std::is_same_v<Type, Shift>) {
                    known = true;
                    used = value.value == register_id
                        || value.amount == register_id
                        || value.destination == register_id;
                } else if constexpr (
                    std::is_same_v<Type, LogicalBinary>
                    || std::is_same_v<Type, Binary>
                    || std::is_same_v<Type, IntegerBinary>) {
                    known = true;
                    used = value.lhs == register_id
                        || value.rhs == register_id
                        || value.destination == register_id;
                } else if constexpr (
                    std::is_same_v<Type, ConditionalSelect>) {
                    known = true;
                    used = value.condition == register_id
                        || value.when_true == register_id
                        || value.when_false == register_id
                        || value.destination == register_id;
                } else if constexpr (
                    std::is_same_v<Type, Concatenate>) {
                    known = true;
                    used = value.destination == register_id
                        || std::ranges::find(
                            value.operands, register_id)
                            != value.operands.end();
                } else if constexpr (std::is_same_v<Type, Insert>) {
                    known = true;
                    used = value.target == register_id
                        || value.source == register_id
                        || value.destination == register_id;
                } else if constexpr (
                    std::is_same_v<Type, DynamicInsert>
                    || std::is_same_v<Type, DynamicPartInsert>) {
                    known = true;
                    used = value.target == register_id
                        || value.source == register_id
                        || value.destination == register_id;
                    if constexpr (std::is_same_v<Type, DynamicInsert>) {
                        used = used
                            || value.selection.index == register_id;
                    } else {
                        used = used || value.selection.base == register_id;
                    }
                } else if constexpr (
                    std::is_same_v<Type, WriteBlocking>
                    || std::is_same_v<Type, WriteUpdate>
                    || std::is_same_v<Type, WriteAfter>
                    || std::is_same_v<Type, WriteInertial>
                    || std::is_same_v<Type, WriteDelayed>
                    || std::is_same_v<Type, WriteProjected>
                    || std::is_same_v<Type, WriteBlockingSlice>
                    || std::is_same_v<Type, WriteUpdateSlice>
                    || std::is_same_v<Type, WriteAfterSlice>
                    || std::is_same_v<Type, WriteInertialSlice>
                    || std::is_same_v<Type, WriteProjectedSlice>
                    || std::is_same_v<Type, WriteBlockingDynamicSlice>
                    || std::is_same_v<Type, WriteUpdateDynamicSlice>
                    || std::is_same_v<Type, WriteAfterDynamicSlice>
                    || std::is_same_v<Type,
                        WriteBlockingDynamicPartSlice>
                    || std::is_same_v<Type, WriteUpdateDynamicPartSlice>
                    || std::is_same_v<Type, WriteAfterDynamicPartSlice>
                    || std::is_same_v<Type, WriteInertialDynamicSlice>
                    || std::is_same_v<Type,
                        WriteInertialDynamicPartSlice>
                    || std::is_same_v<Type, WriteProjectedDynamicSlice>) {
                    known = true;
                    used = value.source == register_id;
                    if constexpr (std::is_same_v<Type, WriteDelayed>) {
                        used = used || value.delay.source == register_id;
                    }
                    if constexpr (
                        std::is_same_v<Type, WriteBlockingDynamicSlice>
                        || std::is_same_v<Type, WriteUpdateDynamicSlice>
                        || std::is_same_v<Type, WriteAfterDynamicSlice>
                        || std::is_same_v<Type, WriteInertialDynamicSlice>
                        || std::is_same_v<Type, WriteProjectedDynamicSlice>) {
                        used = used || value.selection.index == register_id;
                    } else if constexpr (
                        std::is_same_v<Type,
                            WriteBlockingDynamicPartSlice>
                        || std::is_same_v<Type,
                            WriteUpdateDynamicPartSlice>
                        || std::is_same_v<Type,
                            WriteAfterDynamicPartSlice>
                        || std::is_same_v<Type,
                            WriteInertialDynamicPartSlice>) {
                        used = used || value.selection.base == register_id;
                    }
                } else if constexpr (
                    std::is_same_v<Type, WriteContainerObjectElement>) {
                    known = true;
                    used = value.index == register_id
                        || value.source == register_id;
                    if (value.dynamic_part) {
                        used = used
                            || value.dynamic_part->base == register_id;
                    }
                } else if constexpr (std::is_same_v<Type, WaitFor>) {
                    known = true;
                    used = value.source && *value.source == register_id;
                } else if constexpr (std::is_same_v<Type, Assert>) {
                    known = true;
                    used = value.condition == register_id;
                } else if constexpr (
                    std::is_same_v<Type, Branch>) {
                    known = true;
                    used = value.condition == register_id;
                } else if constexpr (
                    std::is_same_v<Type, WaitOn>) {
                    known = true;
                    used = value.timeout_result
                        && *value.timeout_result == register_id;
                } else if constexpr (
                    std::is_same_v<Type, WaitOrder>) {
                    known = true;
                    used = value.result == register_id;
                } else if constexpr (
                    std::is_same_v<Type, Pause>
                    || std::is_same_v<Type, Stop>) {
                    known = true;
                    used = value.status
                        && *value.status == register_id;
                } else if constexpr (
                    std::is_same_v<Type, Jump>
                    || std::is_same_v<Type, Halt>
                    || std::is_same_v<Type, DebugPoint>
                    || std::is_same_v<Type, WaitSensitivity>
                    || std::is_same_v<Type, WaitForever>
                    || std::is_same_v<Type, WaitRegion>
                    || std::is_same_v<Type, WaitPla>) {
                    known = true;
                }
            },
            operation);
        return known && !used;
    };

    const auto width_effects_are_known = [&](const Operation& operation) {
        if (register_use_is_known(
                operation, std::numeric_limits<RegisterId>::max())) {
            return true;
        }
        bool known = false;
        visit_operation(
            [&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (
                    std::is_same_v<Type, StochasticQueueOperation>
                    || std::is_same_v<Type, PlaEvaluate>
                    || std::is_same_v<Type, VhdlAssertApi>
                    || std::is_same_v<Type, RandomDistribution>
                    || std::is_same_v<Type, ScopeRandomize>
                    || std::is_same_v<Type, Call>
                    || std::is_same_v<Type, Return>
                    || std::is_same_v<Type, TraverseContainer>
                    || std::is_same_v<Type, FileOpen>
                    || std::is_same_v<Type, FileClose>
                    || std::is_same_v<Type, FileReadLine>
                    || std::is_same_v<Type, FileErrorStatus>
                    || std::is_same_v<Type, FileScan>
                    || std::is_same_v<Type, FileBinaryRead>
                    || std::is_same_v<Type, ClassMethodCall>
                    || std::is_same_v<Type, ClassStaticMethodCall>) {
                    known = true;
                }
            },
            operation);
        return known;
    };

    const auto exact_register_widths = [&](
        const Process& process,
        const std::vector<Operation>& operations) {
        struct WidthState {
            std::optional<std::uint32_t> width;
            bool invalid { };
        };
        struct CopyRule {
            RegisterId destination { };
            RegisterId source { };
        };
        std::vector<WidthState> states(process.register_count);
        std::vector<CopyRule> copy_rules;
        bool has_out_of_range_register_definition = false;
        bool has_unsupported_width_operation = false;
        const auto invalidate = [&](const RegisterId id) {
            if (id < states.size()) {
                states[id].invalid = true;
            } else {
                has_out_of_range_register_definition = true;
            }
        };
        const auto offer = [&](const RegisterId id,
                               const std::uint32_t width) {
            if (id >= states.size()) {
                has_out_of_range_register_definition = true;
                return;
            }
            auto& state = states[id];
            if (width == 0U) {
                state.invalid = true;
                return;
            }
            if (state.width && *state.width != width) {
                state.invalid = true;
            } else {
                state.width = width;
            }
        };
        const auto account_call_stack = [&](const CallStack& stack) {
            if (stack.capacity == 0U) {
                return;
            }
            offer(stack.pointer, 32U);
            const auto begin = static_cast<std::uint64_t>(stack.entries);
            const auto end = begin + stack.capacity;
            if (begin >= states.size() || end > states.size()) {
                has_out_of_range_register_definition = true;
                return;
            }
            for (auto entry = begin; entry < end; ++entry) {
                offer(static_cast<RegisterId>(entry), 32U);
            }
        };
        const auto invalidate_class_packed_actuals = [&](
            const auto& actuals, const auto& kinds) {
            for (std::size_t index = 0U;
                index < actuals.size();
                ++index) {
                if (kinds.empty() || index >= kinds.size()
                    || kinds[index] != 1U) {
                    invalidate(actuals[index]);
                }
            }
        };

        for (const auto& operation : operations) {
            if (!width_effects_are_known(operation)) {
                has_unsupported_width_operation = true;
            }
            visit_operation(
                [&](const auto& value) {
                    using Type = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Type, LoadConstant>) {
                        if (value.value.width()
                            <= std::numeric_limits<std::uint32_t>::max()) {
                            offer(value.destination,
                                static_cast<std::uint32_t>(
                                    value.value.width()));
                        } else {
                            invalidate(value.destination);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, ReadSignal>) {
                        if (value.signal < design_.signal_info_.size()
                            && design_.signal_info_[value.signal].width
                                <= std::numeric_limits<std::uint32_t>::max()) {
                            offer(value.destination,
                                static_cast<std::uint32_t>(
                                    design_.signal_info_[value.signal].width));
                        } else {
                            invalidate(value.destination);
                        }
                    } else if constexpr (std::is_same_v<Type, Extract>) {
                        offer(value.destination, value.width);
                    } else if constexpr (
                        std::is_same_v<Type, CopyRegister>
                        || std::is_same_v<Type, ConvertToTwoState>
                        || std::is_same_v<Type, UnaryNot>) {
                        copy_rules.push_back(
                            { value.destination, value.source });
                    } else if constexpr (
                        std::is_same_v<Type, DynamicExtract>) {
                        offer(value.destination, 1U);
                    } else if constexpr (
                        std::is_same_v<Type, DynamicPartSelect>) {
                        offer(value.destination, value.width);
                    } else if constexpr (
                        std::is_same_v<Type, ReadSimulationTime>) {
                        offer(value.destination, 64U);
                    } else if constexpr (std::is_same_v<Type, SignalEvent>
                        || std::is_same_v<Type, SignalActive>
                        || std::is_same_v<Type, SignalDriving>
                        || std::is_same_v<Type, EventTriggered>) {
                        offer(value.destination, 1U);
                    } else if constexpr (
                        std::is_same_v<Type, SignalLastEvent>
                        || std::is_same_v<Type, SignalLastActive>) {
                        offer(value.destination, 64U);
                    } else if constexpr (
                        std::is_same_v<Type, SignalLastValue>
                        || std::is_same_v<Type, SignalDrivingValue>) {
                        if (value.signal < design_.signal_info_.size()
                            && design_.signal_info_[value.signal].width
                                <= std::numeric_limits<std::uint32_t>::max()) {
                            offer(value.destination,
                                static_cast<std::uint32_t>(
                                    design_.signal_info_[value.signal].width));
                        } else {
                            invalidate(value.destination);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, LogicalNot>
                        || std::is_same_v<Type, LogicalBinary>
                        || std::is_same_v<Type, Reduction>) {
                        offer(value.destination, 1U);
                    } else if constexpr (std::is_same_v<Type, Binary>) {
                        // Mirrors binary_value: comparisons produce one bit;
                        // bitwise and add/subtract results take the common
                        // operand width. Other arithmetic stays unknown.
                        switch (value.operation) {
                        case BinaryOperator::equal:
                        case BinaryOperator::case_equal:
                        case BinaryOperator::casez_equal:
                        case BinaryOperator::casex_equal:
                        case BinaryOperator::wildcard_equal:
                        case BinaryOperator::not_equal:
                        case BinaryOperator::less_unsigned:
                        case BinaryOperator::less_equal_unsigned:
                        case BinaryOperator::greater_unsigned:
                        case BinaryOperator::greater_equal_unsigned:
                        case BinaryOperator::less_signed:
                        case BinaryOperator::less_equal_signed:
                        case BinaryOperator::greater_signed:
                        case BinaryOperator::greater_equal_signed:
                        case BinaryOperator::vhdl_match_equal:
                            offer(value.destination, 1U);
                            break;
                        case BinaryOperator::bit_and:
                        case BinaryOperator::bit_or:
                        case BinaryOperator::bit_xor:
                        case BinaryOperator::add_unsigned:
                        case BinaryOperator::subtract_unsigned:
                        case BinaryOperator::add_signed:
                        case BinaryOperator::subtract_signed:
                            copy_rules.push_back(
                                { value.destination, value.lhs });
                            copy_rules.push_back(
                                { value.destination, value.rhs });
                            break;
                        default:
                            invalidate(value.destination);
                            break;
                        }
                    } else if constexpr (
                        std::is_same_v<Type, ConditionalSelect>) {
                        // conditional_value requires equal arm widths and
                        // returns that width.
                        copy_rules.push_back(
                            { value.destination, value.when_true });
                        copy_rules.push_back(
                            { value.destination, value.when_false });
                    } else if constexpr (std::is_same_v<Type, Shift>) {
                        // shift_value returns the shifted value's width.
                        copy_rules.push_back(
                            { value.destination, value.value });
                    } else if constexpr (
                        std::is_same_v<Type, CountOnes>
                        || std::is_same_v<Type, CountBits>) {
                        offer(value.destination, 32U);
                    } else if constexpr (
                        std::is_same_v<Type, Concatenate>) {
                        offer(value.destination, value.width);
                    } else if constexpr (std::is_same_v<Type, WaitOrder>) {
                        offer(value.result, 1U);
                    } else if constexpr (std::is_same_v<Type, WaitOn>) {
                        if (value.timeout_result) {
                            offer(*value.timeout_result, 1U);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, StochasticQueueOperation>) {
                        offer(value.status, 32U);
                        if (value.kind != StochasticQueueKind::initialize) {
                            // A missing queue writes every present optional
                            // output before dispatching the kind-specific
                            // path. For add, job/information are read on
                            // success but become 32-bit defs on a miss.
                            if (value.job_id) {
                                offer(*value.job_id, 32U);
                            }
                            if (value.information_id) {
                                offer(*value.information_id, 32U);
                            }
                            if (value.statistic_value) {
                                offer(*value.statistic_value, 32U);
                            }
                            if (value.result) {
                                offer(*value.result, 32U);
                            }
                        }
                    } else if constexpr (std::is_same_v<Type, PlaEvaluate>) {
                        offer(value.output, value.output_width);
                    } else if constexpr (
                        std::is_same_v<Type, VhdlAssertApi>) {
                        if (value.destination) {
                            if (value.kind == VhdlAssertApiKind::get_count) {
                                offer(*value.destination, 64U);
                            } else if (
                                value.kind
                                == VhdlAssertApiKind::get_read_severity) {
                                offer(*value.destination, 2U);
                            } else if (
                                value.kind == VhdlAssertApiKind::is_failed
                                || value.kind
                                    == VhdlAssertApiKind::get_enable) {
                                offer(*value.destination, 1U);
                            } else {
                                invalidate(*value.destination);
                            }
                        }
                        if (value.valid) {
                            if (value.kind == VhdlAssertApiKind::set_format) {
                                offer(*value.valid, 1U);
                            } else {
                                invalidate(*value.valid);
                            }
                        }
                    } else if constexpr (
                        std::is_same_v<Type, RandomDistribution>) {
                        offer(value.destination, 32U);
                        offer(value.seed, 32U);
                    } else if constexpr (
                        std::is_same_v<Type, ScopeRandomize>) {
                        offer(value.destination, 32U);
                        for (const auto& target : value.targets) {
                            offer(target.target, target.width);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, Call>
                        || std::is_same_v<Type, Return>) {
                        account_call_stack(value.stack);
                    } else if constexpr (
                        std::is_same_v<Type, TraverseContainer>) {
                        offer(value.destination, 32U);
                        if (!value.string_index) {
                            invalidate(value.index);
                        }
                    } else if constexpr (std::is_same_v<Type, FileOpen>) {
                        offer(value.destination, 32U);
                        if (value.status) {
                            offer(*value.status, 2U);
                        }
                    } else if constexpr (std::is_same_v<Type, FileClose>) {
                        if (value.clear_handle) {
                            offer(value.handle, 32U);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, FileReadLine>) {
                        offer(value.destination, 32U);
                        if (value.target_kind
                            == FileTextTargetKind::packed_register) {
                            offer(value.target, value.target_width);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, FileErrorStatus>) {
                        offer(value.destination, 32U);
                        if (value.target_kind
                            == FileTextTargetKind::packed_register) {
                            offer(value.target, value.target_width);
                        }
                    } else if constexpr (std::is_same_v<Type, FileScan>) {
                        offer(value.destination, 32U);
                        if (value.success) {
                            offer(*value.success, 1U);
                        }
                        for (const auto& conversion : value.conversions) {
                            if (conversion.target.kind
                                == InputScanTargetKind::packed_register) {
                                offer(
                                    conversion.target.id,
                                    conversion.target.width);
                            }
                        }
                    } else if constexpr (
                        std::is_same_v<Type, FileBinaryRead>) {
                        offer(value.destination, 32U);
                        if (value.target_kind
                            == FileBinaryTargetKind::packed_register) {
                            offer(value.target, value.width);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, ClassMethodCall>) {
                        invalidate(value.destination);
                        invalidate_class_packed_actuals(
                            value.actuals, value.actual_kinds);
                    } else if constexpr (
                        std::is_same_v<Type, ClassStaticMethodCall>) {
                        invalidate(value.destination);
                        invalidate_class_packed_actuals(
                            value.actuals, value.actual_kinds);
                    } else {
                        if constexpr (requires { value.destination; }) {
                            using Destination
                                = std::remove_cvref_t<
                                    decltype(value.destination)>;
                            if constexpr (std::is_same_v<
                                              Destination, RegisterId>) {
                                invalidate(value.destination);
                            } else if constexpr (std::is_same_v<
                                                     Destination,
                                                     std::optional<RegisterId>>) {
                                if (value.destination) {
                                    invalidate(*value.destination);
                                }
                            }
                        }
                        if constexpr (requires { value.result; }) {
                            using Result
                                = std::remove_cvref_t<decltype(value.result)>;
                            if constexpr (std::is_same_v<
                                              Result, RegisterId>) {
                                invalidate(value.result);
                            } else if constexpr (std::is_same_v<
                                                     Result,
                                                     std::optional<RegisterId>>) {
                                if (value.result) {
                                    invalidate(*value.result);
                                }
                            }
                        }
                        if constexpr (requires { value.timeout_result; }) {
                            if (value.timeout_result) {
                                invalidate(*value.timeout_result);
                            }
                        }
                    }
                },
                operation);
        }

        for (std::size_t pass = 0U; pass < states.size(); ++pass) {
            bool changed = false;
            for (const auto& rule : copy_rules) {
                if (rule.destination >= states.size()) {
                    has_out_of_range_register_definition = true;
                    continue;
                }
                auto& destination = states[rule.destination];
                if (rule.source >= states.size()) {
                    if (!destination.invalid) {
                        destination.invalid = true;
                        changed = true;
                    }
                    continue;
                }
                const auto& source = states[rule.source];
                if (source.invalid) {
                    if (!destination.invalid) {
                        destination.invalid = true;
                        changed = true;
                    }
                    continue;
                }
                if (!source.width) {
                    continue;
                }
                if (!destination.width) {
                    if (destination.invalid) {
                        continue;
                    }
                    destination.width = source.width;
                    changed = true;
                } else if (*destination.width != *source.width
                    && !destination.invalid) {
                    destination.invalid = true;
                    changed = true;
                }
            }
            if (!changed) {
                break;
            }
        }
        for (std::size_t pass = 0U; pass < states.size(); ++pass) {
            bool changed = false;
            for (const auto& rule : copy_rules) {
                if (rule.destination >= states.size()) {
                    has_out_of_range_register_definition = true;
                    continue;
                }
                const bool source_invalid
                    = rule.source >= states.size()
                    || states[rule.source].invalid
                    || !states[rule.source].width;
                auto& destination = states[rule.destination];
                if (source_invalid && !destination.invalid) {
                    destination.invalid = true;
                    changed = true;
                }
            }
            if (!changed) {
                break;
            }
        }
        std::vector<std::optional<std::uint32_t>> result(states.size());
        if (has_unsupported_width_operation) {
            return result;
        }
        for (std::size_t index = 0U; index < states.size(); ++index) {
            if (!has_out_of_range_register_definition
                && !states[index].invalid) {
                result[index] = states[index].width;
            }
        }
        return result;
    };

    std::vector<bool> family_has_leaf_write;
    if (lowering_census_enabled_) {
        family_has_leaf_write.resize(families.size(), false);
    }

    for (std::size_t process_index = 0U;
        process_index < design_.process_count(); ++process_index) {
        const auto process_view = design_.process_view(process_index);
        const auto& process_operations = process_view.operations();
        bool has_candidate_operation = false;
        for (std::size_t index = 0U;
            index < process_operations.size();
            ++index) {
            const auto& operation = process_operations[index];
            visit_operation(
                [&](const auto& value) {
                    using Type = std::decay_t<decltype(value)>;
                    if constexpr (requires { value.signal; }) {
                        using Signal = std::remove_cvref_t<
                            decltype(value.signal)>;
                        if constexpr (std::is_same_v<Signal, SignalId>) {
                            const auto signal
                                = process_operations.signal(value.signal);
                            has_candidate_operation
                                = has_candidate_operation
                                || family_by_proxy.contains(signal);
                        }
                    }
                    if constexpr (std::is_same_v<
                                      Type,
                                      WriteContainerObjectElement>) {
                        has_candidate_operation
                            = has_candidate_operation
                            || family_by_object.contains(value.object);
                    }
                },
                operation);
            if (has_candidate_operation) {
                break;
            }
        }
        if (!has_candidate_operation) {
            has_candidate_operation = std::ranges::any_of(
                process_view.static_sensitivity(),
                [&](const Sensitivity& sensitivity) {
                    return family_by_proxy.contains(sensitivity.signal)
                        && sensitivity.width != 0U;
                });
        }
        if (!has_candidate_operation) {
            continue;
        }

        auto process = process_view.materialize();

        // Keep each sensitivity slot in place: static-trigger region masks
        // address this vector by index. Whole-array and cross-element ranges
        // remain on the proxy; an exact interval wholly inside one element
        // can follow that element's physical signal.
        for (auto& sensitivity : process.static_sensitivity) {
            const auto mapped = map_leaf_interval(
                sensitivity.signal, sensitivity.offset, sensitivity.width);
            if (mapped) {
                sensitivity.signal = mapped->first;
                sensitivity.offset = mapped->second;
                record_lowering_census(
                    census.leaf_sensitivity_rewrites);
            }
        }

        std::vector<Operation> operations;
        operations.reserve(process.operations.size());
        for (std::size_t index = 0U;
            index < process.operations.size();
            ++index) {
            operations.push_back(process.operations.expanded(index));
        }
        const auto register_widths
            = exact_register_widths(process, operations);
        const auto register_value_kinds
            = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                process.register_value_kinds);
        const auto register_is_logic4 = [&](const RegisterId id) {
            return id < register_value_kinds.size()
                && register_value_kinds[id] == ValueKind::logic4;
        };
        std::vector<bool> operation_changed(operations.size(), false);
        bool may_write_from_observed_or_postponed = process.observed
            || process.postponed || process.final;

        // A constant immediately before an element write is sufficient only
        // when every control-flow entry to that write passes through it.
        // SimIR calls, fork entries, and block exits can otherwise jump over
        // the apparent definition while preserving the operation positions.
        std::vector<bool> has_direct_entry(operations.size(), false);
        const auto mark_direct_entry = [&](const std::size_t target) {
            if (target < has_direct_entry.size()) {
                has_direct_entry[target] = true;
            }
        };
        for (const auto& operation : operations) {
            visit_operation(
                [&](const auto& value) {
                    using Type = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Type, Jump>) {
                        mark_direct_entry(value.target);
                    } else if constexpr (std::is_same_v<Type, Call>) {
                        mark_direct_entry(value.target);
                        mark_direct_entry(value.return_target);
                    } else if constexpr (std::is_same_v<Type, Branch>) {
                        mark_direct_entry(value.when_true);
                        mark_direct_entry(value.when_false);
                    } else if constexpr (std::is_same_v<Type, Fork>) {
                        for (const auto branch : value.branches) {
                            mark_direct_entry(branch);
                        }
                    } else if constexpr (
                        std::is_same_v<Type, DisableBlock>) {
                        mark_direct_entry(value.end);
                    } else if constexpr (
                        std::is_same_v<Type, WaitRegion>) {
                        may_write_from_observed_or_postponed
                            = may_write_from_observed_or_postponed
                            || value.phase == runtime::SchedulerPhase::observed
                            || value.phase == runtime::SchedulerPhase::postponed;
                    }
                },
                operation);
        }

        for (std::size_t index = 0U;
            index < operations.size();
            ++index) {
            auto* read = operation_get_if<ReadSignal>(&operations[index]);
            if (read == nullptr
                || !family_by_proxy.contains(read->signal)) {
                continue;
            }
            record_lowering_census(census.proxy_read_operations);

            if (index + 1U >= operations.size()) {
                record_lowering_census(
                    census.read_no_extract_fallbacks);
                continue;
            }
            auto* extract
                = operation_get_if<Extract>(&operations[index + 1U]);
            if (extract == nullptr || extract->source != read->destination) {
                record_lowering_census(
                    census.read_no_extract_fallbacks);
                continue;
            }
            if (read->kind != SignalReadKind::current
                || read->clock || read->gate
                || extract->destination == read->destination) {
                record_lowering_census(
                    census.read_context_or_use_fallbacks);
                continue;
            }
            if (read->signal >= design_.signal_info_.size()
                || read->destination >= register_widths.size()
                || !register_is_logic4(read->destination)
                || !register_widths[read->destination]
                || *register_widths[read->destination]
                    != design_.signal_info_[read->signal].width) {
                record_lowering_census(census.read_width_fallbacks);
                continue;
            }
            if (has_direct_entry[index + 1U]
                || std::ranges::any_of(
                    process.debug_locals,
                    [&](const DebugLocal& local) {
                        return local.register_id == read->destination;
                    })) {
                record_lowering_census(
                    census.read_context_or_use_fallbacks);
                continue;
            }

            bool has_only_known_uses = true;
            for (std::size_t other = 0U;
                other < operations.size();
                ++other) {
                if (other == index || other == index + 1U) {
                    continue;
                }
                if (!register_use_is_known(
                        operations[other], read->destination)) {
                    has_only_known_uses = false;
                    break;
                }
            }
            if (!has_only_known_uses) {
                record_lowering_census(
                    census.read_context_or_use_fallbacks);
                continue;
            }

            if (const auto mapped = map_leaf_interval(
                    read->signal, extract->offset, extract->width)) {
                read->signal = mapped->first;
                extract->offset = mapped->second;
                operation_changed[index] = true;
                operation_changed[index + 1U] = true;
                record_lowering_census(census.leaf_read_rewrites);
            } else {
                record_lowering_census(census.read_range_fallbacks);
            }
        }

        auto remapped_regions = process.driver_regions;
        bool region_changed = false;
        for (std::size_t index = 0U;
            index < operations.size();
            ++index) {
            auto* write
                = operation_get_if<WriteContainerObjectElement>(
                    &operations[index]);
            if (write == nullptr) {
                continue;
            }
            const auto family_position
                = family_by_object.find(write->object);
            if (family_position == family_by_object.end()) {
                continue;
            }
            record_lowering_census(
                census.proxy_element_write_operations);
            // Retain checked container execution for multidimensional
            // element writes. Their linear indices may be computed from
            // several registers even when the source indices are constants.
            if (write->linear_index || write->dynamic_part
                || families[family_position->second].dimension_count != 1U) {
                record_lowering_census(
                    census.element_write_dynamic_fallbacks);
                continue;
            }
            if (write->transaction_signal) {
                record_lowering_census(
                    census.element_write_transaction_fallbacks);
                continue;
            }
            const auto* index_literal = index == 0U
                ? nullptr
                : operation_get_if<LoadConstant>(
                    &operations[index - 1U]);
            if (index_literal == nullptr
                || index_literal->destination != write->index) {
                record_lowering_census(
                    census.element_write_index_fallbacks);
                continue;
            }
            if (has_direct_entry[index]
                || (!write->nonblocking
                    && may_write_from_observed_or_postponed)) {
                record_lowering_census(
                    census.element_write_context_fallbacks);
                continue;
            }
            if (write->source >= register_widths.size()
                || !register_is_logic4(write->source)
                || !register_widths[write->source]) {
                record_lowering_census(
                    census.element_write_width_fallbacks);
                continue;
            }
            const auto& family = families[family_position->second];
            if (*register_widths[write->source] != family.element_width) {
                record_lowering_census(
                    census.element_write_width_fallbacks);
                continue;
            }

            std::optional<std::int64_t> logical_index;
            if (write->signed_index) {
                if (const auto value
                    = index_literal->value.known_signed_value()) {
                    logical_index = *value;
                }
            } else {
                if (const auto value
                    = index_literal->value.known_unsigned_value();
                    value
                    && *value
                        <= static_cast<std::uint64_t>(
                            std::numeric_limits<std::int32_t>::max())) {
                    logical_index = static_cast<std::int64_t>(*value);
                }
            }
            if (!logical_index) {
                record_lowering_census(
                    census.element_write_index_fallbacks);
                continue;
            }
            const auto low = std::min(
                static_cast<std::int64_t>(family.declared_left),
                static_cast<std::int64_t>(family.declared_right));
            const auto high = std::max(
                static_cast<std::int64_t>(family.declared_left),
                static_cast<std::int64_t>(family.declared_right));
            if (*logical_index < low || *logical_index > high) {
                record_lowering_census(
                    census.element_write_index_fallbacks);
                continue;
            }
            const auto ordinal = family.declared_left
                    >= family.declared_right
                ? static_cast<std::int64_t>(family.declared_left)
                    - *logical_index
                : *logical_index
                    - static_cast<std::int64_t>(family.declared_left);
            if (ordinal < 0
                || static_cast<std::uint64_t>(ordinal)
                    >= family.element_count) {
                record_lowering_census(
                    census.element_write_index_fallbacks);
                continue;
            }
            const auto leaf = family.leaves[
                static_cast<std::size_t>(ordinal)];
            if (write->nonblocking) {
                const auto domain
                    = process.scheduling_domain
                        == ProcessSchedulingDomain::systemverilog
                    ? SignalUpdateDomain::systemverilog_nba
                    : SignalUpdateDomain::generic;
                operations[index] = WriteUpdate {
                    leaf, write->source, domain
                };
            } else {
                operations[index] = WriteBlocking {
                    leaf, write->source
                };
            }
            operation_changed[index] = true;
            record_lowering_census(
                census.leaf_element_write_rewrites);
            if (lowering_census_enabled_) {
                family_has_leaf_write[family_position->second] = true;
            }
            if (std::ranges::none_of(
                    remapped_regions,
                    [&](const Process::DriverRegion& region) {
                        return region.signal == leaf
                            && region.offset == 0U
                            && region.width == 0U
                            && region.whole;
                    })) {
                remapped_regions.push_back(Process::DriverRegion {
                    leaf, 0U, 0U, true
                });
                region_changed = true;
            }
        }

        std::size_t region_cursor { };
        const auto consume_region = [&](const SignalId signal,
                                        const std::uint32_t offset)
            -> std::optional<std::size_t> {
            while (region_cursor < remapped_regions.size()) {
                const auto& region = remapped_regions[region_cursor];
                if (!region.whole
                    && region.signal == signal
                    && region.offset == offset) {
                    return region_cursor++;
                }
                ++region_cursor;
            }
            return std::nullopt;
        };
        for (std::size_t index = 0U;
            index < operations.size();
            ++index) {
            auto remap_static_slice = [&](auto& value,
                                          const bool eligible) {
                const auto family_position
                    = family_by_proxy.find(value.signal);
                const auto region_index
                    = consume_region(value.signal, value.offset);
                if (!eligible
                    || family_position == family_by_proxy.end()) {
                    return;
                }
                record_lowering_census(
                    census.proxy_slice_write_operations);
                if (!region_index) {
                    record_lowering_census(
                        census.slice_write_region_fallbacks);
                    return;
                }
                const auto& region = remapped_regions[*region_index];
                if (region.width == 0U
                    || value.source >= register_widths.size()
                    || !register_widths[value.source]
                    || *register_widths[value.source] != region.width) {
                    record_lowering_census(
                        census.slice_write_width_fallbacks);
                    return;
                }
                const auto mapped = map_leaf_interval(
                    value.signal, value.offset, region.width);
                if (!mapped) {
                    record_lowering_census(
                        census.slice_write_range_fallbacks);
                    return;
                }
                value.signal = mapped->first;
                value.offset = mapped->second;
                remapped_regions[*region_index].signal = mapped->first;
                remapped_regions[*region_index].offset = mapped->second;
                operation_changed[index] = true;
                region_changed = true;
                record_lowering_census(
                    census.leaf_slice_write_rewrites);
                if (lowering_census_enabled_) {
                    family_has_leaf_write[family_position->second] = true;
                }
            };

            visit_operation(
                [&](auto& value) {
                    using Type = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Type, WriteBlockingSlice>
                        || std::is_same_v<Type, WriteUpdateSlice>
                        || std::is_same_v<Type, WriteAfterSlice>
                        || std::is_same_v<Type, WriteInertialSlice>) {
                        remap_static_slice(value, true);
                    } else if constexpr (
                        std::is_same_v<Type, WriteProjectedSlice>) {
                        remap_static_slice(value, false);
                    } else if constexpr (std::is_same_v<
                                             Type,
                                             WriteProjectedWaveformSlice>) {
                        // Projected VHDL writes keep their aggregate route,
                        // but consume the corresponding region so later
                        // ordinary slices still match the right owner.
                        (void)consume_region(value.signal, value.offset);
                    }
                },
                operations[index]);
        }

        for (std::size_t index = 0U;
            index < operation_changed.size();
            ++index) {
            if (operation_changed[index]) {
                process.operations.replace(
                    index, std::move(operations[index]));
            }
        }
        if (region_changed) {
            process.driver_regions = std::move(remapped_regions);
        }
        design_.replace_process_record(process_index, std::move(process));
    }
    if (lowering_census_enabled_) {
        for (const bool has_leaf_write : family_has_leaf_write) {
            if (has_leaf_write) {
                ++census.physical_families_with_leaf_writes;
            } else {
                ++census.physical_alias_only_families;
            }
        }
    }
    return census;
}
void HierarchyBuilder::finish()
{
    if (lowering_census_enabled_) {
        std::cerr << "fsim-profile: concurrent-process-template"
                  << " occurrences="
                  << vhdl_concurrent_template_occurrences_
                  << " misses=" << concurrent_template_misses_
                  << " hits=" << concurrent_template_hits_
                  << " rejected=" << concurrent_template_rejections_
                  << " lowerer_calls="
                  << vhdl_concurrent_template_occurrences_
                      - concurrent_template_hits_
                  << " lowered_cpu_ns="
                  << concurrent_template_lower_cpu_ns_
                  << '\n';
        std::cerr << "fsim-profile: vhdl-process-template"
                  << " occurrences=" << vhdl_process_template_occurrences_
                  << " lowerer_calls=" << vhdl_process_lower_requests_
                  << " hits=" << vhdl_process_template_hits_
                  << " misses=" << vhdl_process_template_misses_
                  << " rejected=" << vhdl_process_template_rejections_
                  << " lowered_cpu_ns=" << vhdl_process_template_lower_cpu_ns_
                  << '\n';
        std::cerr << "fsim-profile: systemverilog-concurrent-process-template"
                  << " occurrences="
                  << systemverilog_concurrent_occurrences_
                  << " top_occurrences="
                  << systemverilog_concurrent_occurrences_
                      - systemverilog_generated_concurrent_occurrences_
                  << " generated_occurrences="
                  << systemverilog_generated_concurrent_occurrences_
                  << " misses="
                  << systemverilog_concurrent_template_misses_
                  << " hits=" << systemverilog_concurrent_template_hits_
                  << " rejected="
                  << systemverilog_concurrent_template_rejections_
                  << " lowered="
                  << systemverilog_concurrent_templates_lowered_
                  << " replayed="
                  << systemverilog_concurrent_templates_replayed_
                  << " top_replayed="
                  << systemverilog_concurrent_templates_replayed_
                      - systemverilog_generated_concurrent_replays_
                  << " generated_replayed="
                  << systemverilog_generated_concurrent_replays_
                  << " lowerer_calls="
                  << systemverilog_concurrent_occurrences_
                      - systemverilog_concurrent_templates_replayed_
                  << " top_lowerer_calls="
                  << systemverilog_concurrent_occurrences_
                      - systemverilog_generated_concurrent_occurrences_
                      - (systemverilog_concurrent_templates_replayed_
                          - systemverilog_generated_concurrent_replays_)
                  << " generated_lowerer_calls="
                  << systemverilog_generated_concurrent_occurrences_
                      - systemverilog_generated_concurrent_replays_
                  << '\n';
        std::cerr << "fsim-profile: systemverilog-generated-process-template"
                  << " occurrences="
                  << systemverilog_generated_process_occurrences_
                  << " lowerer_calls="
                  << systemverilog_generated_process_lower_requests_
                  << " cached_programs="
                  << systemverilog_generated_process_templates_lowered_
                  << " replays="
                  << systemverilog_generated_process_template_replays_
                  << " rejected="
                  << systemverilog_generated_process_template_rejections_
                  << '\n';
        std::cerr << "fsim-profile: systemverilog-ordinary-process-template"
                  << " occurrences="
                  << systemverilog_ordinary_process_template_occurrences_
                  << " lowerer_calls="
                  << systemverilog_ordinary_process_template_lower_requests_
                  << " cached_programs="
                  << systemverilog_ordinary_process_templates_lowered_
                  << " replays="
                  << systemverilog_ordinary_process_template_replays_
                  << " rejected="
                  << systemverilog_ordinary_process_template_rejections_
                  << '\n';
        std::cerr << "fsim-profile: hir-process-lowering-census"
                  << " uncached_vhdl_process_occurrences="
                  << vhdl_process_lower_requests_
                  << " uncached_vhdl_generated_process_occurrences="
                  << vhdl_generated_process_lower_requests_
                  << " vhdl_lowerer_generated_processes="
                  << vhdl_lowerer_generated_processes_
                  << " uncached_systemverilog_process_occurrences="
                  << systemverilog_process_lower_requests_
                  << " uncached_systemverilog_generated_process_occurrences="
                  << systemverilog_generated_process_lower_requests_
                  << " systemverilog_clocking_process_occurrences="
                  << systemverilog_clocking_process_occurrences_
                  << " systemverilog_lowerer_generated_processes="
                  << systemverilog_lowerer_generated_processes_
                  << " uncached_vhdl_generated_input_actual_occurrences="
                  << vhdl_generated_input_actual_occurrences_
                  << " uncached_vhdl_port_input_actual_occurrences="
                  << vhdl_port_input_actual_occurrences_
                  << " uncached_vhdl_port_output_actual_occurrences="
                  << vhdl_port_output_actual_occurrences_
                  << " uncached_vhdl_generated_concurrent_occurrences="
                  << vhdl_generated_concurrent_occurrences_
                  << '\n';
    }
    const auto element_net_census
        = rewrite_eligible_element_net_families();
    if (lowering_census_enabled_) {
        std::cerr << "fsim-profile: element-net-family-census"
                  << " shape_families="
                  << element_net_census.shape_qualified_families
                  << " shape_leaves="
                  << element_net_census.shape_qualified_leaves
                  << " invalid_initial_cache_shape_fallback_families="
                  << element_net_census
                         .invalid_initial_cache_shape_fallback_families
                  << " resolver_fallback_families="
                  << element_net_census.resolver_fallback_families
                  << " ordinal_range_fallback_families="
                  << element_net_census.ordinal_range_fallback_families
                  << " name_collision_fallback_families="
                  << element_net_census.name_collision_fallback_families
                  << " candidates="
                  << element_net_census.candidate_families
                  << " candidate_leaves="
                  << element_net_census.candidate_leaves
                  << " terminal_fallback_families="
                  << element_net_census.terminal_fallback_families
                  << " terminal_fallback_leaves="
                  << element_net_census.terminal_fallback_leaves
                  << " projected_write_operations="
                  << element_net_census.projected_write_operations
                  << " projected_fallback_families="
                  << element_net_census.projected_fallback_families
                  << " projected_fallback_leaves="
                  << element_net_census.projected_fallback_leaves
                  << " eligible_families="
                  << element_net_census.eligible_families
                  << " eligible_leaves="
                  << element_net_census.eligible_leaves
                  << " capacity_fallback_families="
                  << element_net_census.capacity_fallback_families
                  << " capacity_fallback_leaves="
                  << element_net_census.capacity_fallback_leaves
                  << " physical_families="
                  << element_net_census.physical_families
                  << " physical_leaves="
                  << element_net_census.physical_leaves
                  << " families_with_leaf_writes="
                  << element_net_census.physical_families_with_leaf_writes
                  << " alias_only_families="
                  << element_net_census.physical_alias_only_families
                  << '\n';
        std::cerr << "fsim-profile: element-net-read-census"
                  << " proxy_reads="
                  << element_net_census.proxy_read_operations
                  << " leaf_read_rewrites="
                  << element_net_census.leaf_read_rewrites
                  << " no_extract_fallbacks="
                  << element_net_census.read_no_extract_fallbacks
                  << " width_fallbacks="
                  << element_net_census.read_width_fallbacks
                  << " context_or_use_fallbacks="
                  << element_net_census.read_context_or_use_fallbacks
                  << " range_fallbacks="
                  << element_net_census.read_range_fallbacks
                  << " proxy_sensitivities="
                  << element_net_census.proxy_sensitivity_entries
                  << " sensitivity_rewrites="
                  << element_net_census.leaf_sensitivity_rewrites
                  << " sensitivity_range_fallbacks="
                  << element_net_census.sensitivity_range_fallbacks
                  << '\n';
        std::cerr << "fsim-profile: element-net-write-census"
                  << " proxy_element_writes="
                  << element_net_census.proxy_element_write_operations
                  << " leaf_element_write_rewrites="
                  << element_net_census.leaf_element_write_rewrites
                  << " dynamic_fallbacks="
                  << element_net_census.element_write_dynamic_fallbacks
                  << " transaction_fallbacks="
                  << element_net_census.element_write_transaction_fallbacks
                  << " index_fallbacks="
                  << element_net_census.element_write_index_fallbacks
                  << " width_fallbacks="
                  << element_net_census.element_write_width_fallbacks
                  << " context_fallbacks="
                  << element_net_census.element_write_context_fallbacks
                  << " proxy_slice_writes="
                  << element_net_census.proxy_slice_write_operations
                  << " leaf_slice_write_rewrites="
                  << element_net_census.leaf_slice_write_rewrites
                  << " slice_region_fallbacks="
                  << element_net_census.slice_write_region_fallbacks
                  << " slice_width_fallbacks="
                  << element_net_census.slice_write_width_fallbacks
                  << " slice_range_fallbacks="
                  << element_net_census.slice_write_range_fallbacks
                  << '\n';
    }
    validate_process_drivers();
    design_.finalize_process_rows();
    std::stable_sort(
        design_.systemc_objects_.begin(),
        design_.systemc_objects_.end(),
        [](const SystemCNamedObjectInfo& left,
            const SystemCNamedObjectInfo& right) {
            return left.native_handle < right.native_handle;
        });
    for (const auto& [path, binding] : bindings_) {
        (void)binding;
        if (!used_bindings_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-011",
                "binding instance path '" + path
                    + "' was not found in the elaborated hierarchy",
                { });
        }
    }
    for (const auto& [path, instance] : systemc_instances_) {
        (void)instance;
        if (!used_systemc_instances_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-033",
                "constructed SystemC instance path '" + path
                    + "' was not reached from the elaborated hierarchy",
                { });
        }
    }
    // Root-global predeclaration and authoritative root instantiation both
    // specialize the same unit now that no prepared AST root is retained.
    // Preserve the first diagnostic (including constant-function $error
    // failures) while coalescing the identical second report.
    std::vector<Diagnostic> unique_diagnostics;
    unique_diagnostics.reserve(diagnostics_.size());
    for (auto& diagnostic : diagnostics_) {
        const auto duplicate = std::ranges::any_of(
            unique_diagnostics, [&](const Diagnostic& retained) {
                return retained.code == diagnostic.code
                    && retained.message == diagnostic.message
                    && retained.span == diagnostic.span;
            });
        if (!duplicate) {
            unique_diagnostics.push_back(std::move(diagnostic));
        }
    }
    diagnostics_ = std::move(unique_diagnostics);
    design_.freeze_hierarchy_paths();
    if (diagnostics_.empty()) {
        try {
            design_.finalize_signal_driver_inventory();
        } catch (const std::exception& error) {
            report("FSIM-ELAB-DRIVER-001",
                "could not construct immutable signal-driver inventory: "
                    + std::string { error.what() },
                { });
        }
    }
}

ResolutionKind HierarchyBuilder::native_resolution(
    const SignalInfo& signal)
{
    const auto& net_type = signal.systemverilog_net_type.empty()
        ? signal.type_name
        : signal.systemverilog_net_type;
    if (signal.type_name == "std_logic"
        || signal.type_name == "std_logic_vector") {
        return ResolutionKind::std_logic;
    }
    // IEEE 1800-2017 6.7.2: a real net is unresolved; only a user nettype
    // can resolve several drivers of a real value.
    if (signal.systemverilog_scalar
        != frontend::SystemVerilogScalarKind::None) {
        return ResolutionKind::none;
    }
    if (net_type == "wire"
        || net_type == "tri"
        || net_type == "tri0"
        || net_type == "tri1"
        || net_type == "trireg"
        || net_type == "supply0"
        || net_type == "supply1") {
        return ResolutionKind::sv_wire;
    }
    if (net_type == "wand"
        || net_type == "triand") {
        return ResolutionKind::sv_wand;
    }
    if (net_type == "wor"
        || net_type == "trior") {
        return ResolutionKind::sv_wor;
    }
    // A nonempty name can also be an unresolved user nettype or an
    // accidentally retained variable spelling. Only the explicit built-in
    // net kinds above have native wire resolution; user nettypes are handled
    // by explicit_resolution() after their resolver has been linked.
    return ResolutionKind::none;
}

std::optional<ResolutionKind>
HierarchyBuilder::explicit_resolution(
    const SignalId signal)
{
    const auto found = resolver_by_signal_.find(signal);
    if (found == resolver_by_signal_.end()) {
        const auto& info = design_.signal_info_.at(signal);
        const auto nettype = info.systemverilog_net_type.empty()
            ? info.type_name
            : info.systemverilog_net_type;
        const auto user_nettype = systemverilog_resolution_kinds_.find(
            nettype);
        if (user_nettype != systemverilog_resolution_kinds_.end()) {
            return user_nettype->second;
        }
        return std::nullopt;
    }
    if (found->second == "std_logic") {
        return ResolutionKind::std_logic;
    }
    if (found->second == "sv_wire") {
        return ResolutionKind::sv_wire;
    }
    if (const auto user = vhdl_resolution_kinds_.find(found->second);
        user != vhdl_resolution_kinds_.end()) {
        return user->second;
    }
    if (const auto user = systemverilog_resolution_kinds_.find(found->second);
        user != systemverilog_resolution_kinds_.end()) {
        return user->second;
    }
    report(
        "FSIM-ELAB-BIND-050",
        "unknown resolver '" + found->second
            + "'; expected \"std_logic\" or \"sv_wire\"",
        { });
    return ResolutionKind::none;
}

void HierarchyBuilder::register_systemverilog_resolution_functions(
    const semantic::sv::Unit& unit)
{
    if (compiled_ == nullptr) {
        return;
    }
    if (std::ranges::find(
            systemverilog_resolution_unit_registrations_, unit.id)
        != systemverilog_resolution_unit_registrations_.end()) {
        return;
    }
    const auto diagnostics_before = diagnostics_.size();
    for (const auto declaration_id : unit.declarations) {
        const auto* declaration = compiled_systemverilog_declaration(
            *compiled_, declaration_id);
        if (declaration == nullptr
            || declaration->form
                != semantic::sv::DeclarationForm::nettype_declaration
            || !declaration->declared_type) {
            continue;
        }
        const auto* type = compiled_systemverilog_type(
            *compiled_, *declaration->declared_type);
        if (type == nullptr || type->resolution_function.empty()) {
            continue;
        }
        const auto& resolver = type->resolution_function;
        std::vector<const semantic::sv::Declaration*> matches;
        const semantic::CompiledDeclarationPredicate executable_function
            = [](const semantic::CompiledDeclarationView& candidate) {
                  return candidate.systemverilog != nullptr
                      && candidate.systemverilog->form
                          == semantic::sv::DeclarationForm::function
                      && !candidate.systemverilog->statements.empty();
              };
        const auto resolution = semantic::CompiledDesignResolver {
            *compiled_, unit.id }
                                    .resolve_systemverilog(resolver,
                                        unit.scope, executable_function,
                                        false);
        for (const auto candidate_id : resolution.candidates) {
            if (const auto* candidate = compiled_systemverilog_declaration(
                    *compiled_, candidate_id)) {
                matches.push_back(candidate);
            }
        }
        const auto source = compiled_resolution_source_span(
            *compiled_, type->source);
        if (matches.size() != 1U) {
            report(
                matches.empty()
                    ? "FSIM-ELAB-SVNETTYPE-001"
                    : "FSIM-ELAB-SVNETTYPE-002",
                matches.empty()
                    ? "SystemVerilog nettype resolution function '"
                        + resolver
                        + "' is not visible with an executable body"
                    : "SystemVerilog nettype resolution function '"
                        + resolver + "' is ambiguous",
                source);
            continue;
        }
        const auto& function = *matches.front();
        const auto* formal = function.callable
                && function.callable->formals.size() == 1U
            ? compiled_systemverilog_declaration(
                  *compiled_, function.callable->formals.front())
            : nullptr;
        const bool profile_matches = function.callable
            && function.callable->function
            && formal != nullptr && formal->type
            && formal->type->container_form
                == semantic::sv::TypeForm::dynamic_array
            && type->base.executable_width
            && function.callable->return_type.executable_width
            && *type->base.executable_width
                == *function.callable->return_type.executable_width
            && type->base.four_state
                == function.callable->return_type.four_state;
        if (!profile_matches) {
            report(
                "FSIM-ELAB-SVNETTYPE-003",
                "SystemVerilog nettype resolution function '" + resolver
                    + "' must take one dynamic array of the net base type "
                      "and return that base type",
                compiled_resolution_source_span(
                    *compiled_, function.source));
            continue;
        }
        if (!compiled_resolver_returns_first(*compiled_, function)) {
            report(
                "FSIM-ELAB-SVNETTYPE-004",
                "the executable SystemVerilog nettype resolver '" + resolver
                    + "' is outside the retained deterministic resolution "
                      "forms",
                compiled_resolution_source_span(
                    *compiled_, function.source));
            continue;
        }
        const auto [entry, inserted] = systemverilog_resolution_kinds_.emplace(
            resolver, ResolutionKind::sv_user_first);
        if (!inserted && entry->second != ResolutionKind::sv_user_first) {
            report(
                "FSIM-ELAB-SVNETTYPE-002",
                "SystemVerilog nettype resolution function '" + resolver
                    + "' has conflicting visible bodies",
                source);
        }
        const auto [nettype, nettype_inserted]
            = systemverilog_resolution_kinds_.emplace(
                declaration->name, ResolutionKind::sv_user_first);
        if (!nettype_inserted
            && nettype->second != ResolutionKind::sv_user_first) {
            report(
                "FSIM-ELAB-SVNETTYPE-002",
                "SystemVerilog nettype '" + declaration->name
                    + "' has conflicting visible resolution functions",
                source);
        }
    }
    if (diagnostics_.size() == diagnostics_before) {
        systemverilog_resolution_unit_registrations_.push_back(unit.id);
    }
}

void HierarchyBuilder::set_resolution(
    const SignalId signal,
    const ResolutionKind resolution)
{
    design_.signal_info_.at(signal).resolution = resolution;
    design_.signals_.at(signal).resolution = resolution;
}

void HierarchyBuilder::validate_process_drivers()
{
    using DriverRegion = Process::DriverRegion;
    struct ProcessDriver {
        std::vector<DriverRegion> regions;
        bool continuous { };
        bool event_controlled { };
        bool vhdl { };
    };
    // Processes record their unit's standard; the VHDL revisions do not
    // overlap the Verilog and SystemVerilog ones.
    const auto vhdl_language_standard = [](const std::string_view standard) {
        return standard == "1987" || standard == "1993"
            || standard == "2000" || standard == "2002"
            || standard == "2008" || standard == "2019";
    };
    std::unordered_map<ContainerObjectId, std::vector<SignalId>>
        writable_container_signals;
    std::unordered_map<SignalId, bool> variable_container_signals;
    for (const auto& alias : design_.container_signal_aliases_) {
        if (alias.writable) {
            writable_container_signals[alias.object].push_back(alias.signal);
            if (alias.signal < design_.signal_info_.size()
                && design_.signal_info_[alias.signal]
                       .systemverilog_net_type.empty()) {
                variable_container_signals.insert_or_assign(
                    alias.signal, true);
            }
        }
    }
    for (const auto& alias : design_.container_element_signal_aliases_) {
        if (alias.writable) {
            writable_container_signals[alias.object].push_back(alias.signal);
            if (alias.signal < design_.signal_info_.size()
                && design_.signal_info_[alias.signal]
                       .systemverilog_net_type.empty()) {
                variable_container_signals.insert_or_assign(
                    alias.signal, true);
            }
        }
    }
    for (const auto& alias : design_.container_aggregate_signal_aliases_) {
        if (alias.writable) {
            writable_container_signals[alias.object].push_back(alias.signal);
            if (alias.signal < design_.signal_info_.size()
                && design_.signal_info_[alias.signal]
                       .systemverilog_net_type.empty()) {
                variable_container_signals.insert_or_assign(
                    alias.signal, true);
            }
        }
    }
    const auto regions_overlap = [](
                                     const DriverRegion& left,
                                     const DriverRegion& right) {
        if (left.whole || right.whole) {
            return true;
        }
        const auto left_end = static_cast<std::uint64_t>(left.offset)
            + left.width;
        const auto right_end = static_cast<std::uint64_t>(right.offset)
            + right.width;
        return left.offset < right_end && right.offset < left_end;
    };
    const auto process_leaf = [](const ProcessProgramView& process) {
        const auto& name = process.name();
        return std::string_view { name }.substr(
            name.find_last_of('.') + 1U);
    };
    const auto is_continuous_process = [&](const ProcessProgramView& process) {
        const auto leaf = process_leaf(process);
        return leaf.starts_with("concurrent_");
    };
    std::unordered_set<SignalId> continuous_signals;
    for (std::size_t process_index = 0U;
        process_index < design_.process_count(); ++process_index) {
        const auto process = design_.process_view(process_index);
        if (process.name().find("$declaration_initializer_")
                != std::string::npos
            || !is_continuous_process(process)) {
            continue;
        }
        for (const auto& region : process.driver_regions()) {
            continuous_signals.insert(region.signal);
        }
        for (const auto& operation : process.operations()) {
            visit_operation(
                [&](const auto& value) {
                    using OperationType
                        = std::decay_t<decltype(value)>;
                    if constexpr (
                        std::is_same_v<
                            OperationType, WriteContainerObject>
                        || std::is_same_v<
                            OperationType, WriteContainerObjectElement>) {
                        const auto aliases
                            = writable_container_signals.find(value.object);
                        if (aliases != writable_container_signals.end()) {
                            continuous_signals.insert(
                                aliases->second.begin(),
                                aliases->second.end());
                        }
                    }
                },
                operation);
        }
    }
    std::unordered_map<SignalId, std::vector<ProcessDriver>> drivers;
    for (std::size_t process_index = 0U;
        process_index < design_.process_count(); ++process_index) {
        const auto process = design_.process_view(process_index);
        if (process.name().find("$declaration_initializer_")
            != std::string::npos) {
            continue;
        }
        std::map<SignalId, std::vector<DriverRegion>> process_outputs;
        for (const auto& region : process.driver_regions()) {
            process_outputs[region.signal].push_back(region);
        }
        const auto record_container_object_write =
            [&](const ContainerObjectId object) {
                const auto aliases = writable_container_signals.find(object);
                if (aliases == writable_container_signals.end()) {
                    return;
                }
                for (const auto signal : aliases->second) {
                    if (variable_container_signals.contains(signal)
                        && !continuous_signals.contains(signal)) {
                        // Procedural-only variable arrays retain their legacy
                        // multiple-writer semantics. Audit the conservative
                        // whole-array claim only when a continuous process
                        // also drives the corresponding signal.
                        continue;
                    }
                    auto& regions = process_outputs[signal];
                    if (std::ranges::none_of(
                            regions,
                            [](const DriverRegion& region) {
                                return region.whole;
                            })) {
                        regions.push_back(DriverRegion {
                            signal, 0U, 0U, true });
                    }
                }
            };
        for (const auto& operation : process.operations()) {
            visit_operation(
                [&](const auto& value) {
                    using OperationType
                        = std::decay_t<decltype(value)>;
                    if constexpr (
                        std::is_same_v<
                            OperationType, WriteContainerObject>
                        || std::is_same_v<
                            OperationType, WriteContainerObjectElement>) {
                        record_container_object_write(value.object);
                    }
                },
                operation);
        }
        for (auto& [signal, regions] : process_outputs) {
            const bool continuous = is_continuous_process(process);
            const bool event_controlled
                = !process.static_sensitivity().empty();
            drivers[signal].push_back(ProcessDriver {
                std::move(regions), continuous, event_controlled,
                vhdl_language_standard(process.language_standard()) });
        }
    }
    for (SignalId signal = 0;
        signal < design_.signal_info_.size();
        ++signal) {
        if (vhdl_unprotected_shared_signals_.contains(signal)) {
            // Shared variables have one immediately updated storage value,
            // even when their VHDL element type is ordinarily resolved.
            set_resolution(signal, ResolutionKind::none);
            continue;
        }
        const auto selected = explicit_resolution(signal);
        set_resolution(
            signal,
            selected.value_or(
                native_resolution(
                    design_.signal_info_.at(signal))));
    }
    for (const auto& [signal, process_drivers] : drivers) {
        const auto& info = design_.signal_info_.at(signal);
        const auto boundary_drivers = boundary_driver_paths_.find(signal);
        if (process_drivers.size() <= 1
            || (boundary_drivers != boundary_driver_paths_.end()
                && boundary_drivers->second.size() > 1U)
            || vhdl_unprotected_shared_signals_.contains(signal)
            || info.resolution != ResolutionKind::none
            || (info.type_name == "reg"
                && !variable_container_signals.contains(signal))
            || (info.type_name == "integer"
                && !variable_container_signals.contains(signal))
            || info.name.ends_with(".$container_storage")) {
            continue;
        }
        bool overlap = false;
        for (std::size_t left = 0;
            left < process_drivers.size() && !overlap; ++left) {
            for (std::size_t right = left + 1;
                right < process_drivers.size() && !overlap; ++right) {
                overlap = std::ranges::any_of(
                    process_drivers[left].regions,
                    [&](const auto& left_region) {
                        return std::ranges::any_of(
                            process_drivers[right].regions,
                            [&](const auto& right_region) {
                                return regions_overlap(
                                    left_region, right_region);
                            });
                    });
            }
        }
        if (!overlap
            && (!info.vhdl_mode_view_bindings.empty()
                || info.vhdl_array
                || std::ranges::all_of(
                    process_drivers,
                    [](const auto& driver) {
                        return driver.continuous;
                    }))) {
            continue;
        }
        if (process_drivers.size() == 2
            && process_drivers.front().event_controlled
                != process_drivers.back().event_controlled
            && !process_drivers.front().continuous
            && !process_drivers.back().continuous) {
            continue;
        }
        // A SystemVerilog variable may be written by any number of
        // procedural statements in different processes (IEEE 1800-2017
        // 6.5); only continuous assignments conflict with other drivers.
        if (std::ranges::none_of(process_drivers, [](const auto& driver) {
                return driver.continuous || driver.vhdl;
            })) {
            continue;
        }
        report(
            "FSIM-ELAB-DRV-001",
            "unresolved variable '" + info.name
                + "' has multiple process drivers",
            { });
    }
}

const Binding* HierarchyBuilder::binding_for(const std::string& path)
{
    const auto found = bindings_.find(path);
    if (found == bindings_.end()) {
        return nullptr;
    }
    used_bindings_.insert(path);
    return found->second;
}

std::optional<UnitResolutionCandidate>
HierarchyBuilder::compiled_instance_target(
    const std::string_view parent_library,
    const std::string_view name,
    const std::string& path,
    const frontend::SourceSpan source,
    const Binding* const binding,
    std::optional<semantic::CompiledUnitView> linked_target,
    const bool linked_target_authoritative)
{
    if (compiled_ == nullptr) {
        return std::nullopt;
    }
    if (binding != nullptr && binding->target) {
        const auto target = parse_target(*binding->target);
        if (!target) {
            report(
                "FSIM-ELAB-BIND-013",
                "malformed binding target '" + *binding->target + "'",
                source);
            return std::nullopt;
        }
        if (target->language == "systemc") {
            return UnitResolutionCandidate {
                binding->target, *binding->target, std::nullopt, nullptr };
        }
        if (target->language == "vhdl" && !target->architecture) {
            report(
                "FSIM-ELAB-BIND-016",
                "an explicit VHDL binding target must name an "
                "architecture, for example vhdl:work.entity(rtl)",
                source);
            return std::nullopt;
        }
        auto selected = choose_bound_unit(*compiled_, *target);
        if (!selected) {
            report(
                "FSIM-ELAB-BIND-015",
                "binding target '" + *binding->target
                    + "' was not found",
                source);
            return std::nullopt;
        }
        UnitResolutionCandidate result;
        result.identity = unit_identity(*selected);
        result.compiled_unit = std::move(selected);
        return result;
    }
    const bool linked_systemverilog_unit = linked_target
        && linked_target->systemverilog != nullptr
        && linked_target->vhdl == nullptr
        && (linked_target->systemverilog->kind
                == semantic::sv::UnitKind::module
            || linked_target->systemverilog->kind
                == semantic::sv::UnitKind::interface
            || linked_target->systemverilog->kind
                == semantic::sv::UnitKind::program)
        && !linked_target->systemverilog->external;
    const bool linked_vhdl_unit = linked_target
        && linked_target->vhdl != nullptr
        && linked_target->systemverilog == nullptr
        && linked_target->vhdl->kind
            == semantic::vhdl::UnitKind::architecture;
    if (linked_target_authoritative
        && (linked_systemverilog_unit || linked_vhdl_unit)) {
        UnitResolutionCandidate result;
        result.identity = unit_identity(*linked_target);
        result.compiled_unit = std::move(linked_target);
        return result;
    }

    const auto scope = effective_search_scope(
        parent_library, search_libraries_);
    std::vector<UnitResolutionCandidate> candidates;
    std::vector<std::string> unavailable_libraries;
    for (std::size_t index = 0U; index < scope.size(); ++index) {
        const auto& library = scope[index];
        if (!has_logical_library(
                *compiled_, systemc_candidates_, systemc_libraries_,
                library)) {
            if (index != 0U) {
                unavailable_libraries.push_back(library);
            }
            continue;
        }
        auto resolved = resolve_unit_candidates(
            *compiled_, library, name);
        candidates.insert(
            candidates.end(),
            std::make_move_iterator(resolved.begin()),
            std::make_move_iterator(resolved.end()));
        for (const auto& factory : systemc_candidates_) {
            if (factory.library == library && factory.name == name) {
                candidates.push_back({
                    factory.target,
                    "systemc:" + factory.library + "." + factory.name,
                    std::nullopt,
                    nullptr,
                });
            }
        }
    }
    std::stable_sort(
        candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) {
            return left.identity < right.identity;
        });
    std::string formatted_scope;
    for (const auto& entry : scope) {
        if (!formatted_scope.empty()) {
            formatted_scope += ", ";
        }
        formatted_scope += entry;
    }
    if (!unavailable_libraries.empty()) {
        std::string unavailable;
        for (const auto& entry : unavailable_libraries) {
            if (!unavailable.empty()) {
                unavailable += ", ";
            }
            unavailable += entry;
        }
        report(
            "FSIM-ELAB-BIND-059",
            "instance '" + path
                + "' queried unavailable logical library/libraries ["
                + unavailable + "] while resolving unit '"
                + std::string { name } + "' in search scope ["
                + formatted_scope + "]",
            source);
        return std::nullopt;
    }
    if (candidates.empty()) {
        report(
            "FSIM-ELAB-BIND-012",
            "instance '" + path + "' names unit '"
                + std::string { name }
                + "', which was not found across VHDL, Verilog, "
                  "SystemVerilog, or SystemC in search scope ["
                + formatted_scope + "]; candidates: <none>",
            source);
        return std::nullopt;
    }
    if (candidates.size() != 1U) {
        report(
            "FSIM-ELAB-BIND-017",
            "instance '" + path + "' names ambiguous unit '"
                + std::string { name } + "' in search scope ["
                + formatted_scope + "]; candidates: "
                + format_resolution_candidates(candidates),
            source);
        return std::nullopt;
    }
    return candidates.front();
}

} // namespace fsim::elaboration
