// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_internal.hpp"
#include "simir_static_kernel.hpp"
#include "simir_static_kernel_state.hpp"
#include "simir_execution_context.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <ranges>
#include <sstream>

namespace fsim::runtime::simir {

void Interpreter::set_file_root(std::filesystem::path root)
{
    impl_->set_file_root(std::move(root));
}

void Interpreter::set_operation_budget(const std::uint64_t operations)
{
    impl_->operation_budget = operations;
}

void Interpreter::set_plusargs(const std::span<const std::string> plusargs)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot set SimIR plusargs after start"
        };
    }
    impl_->plusargs.assign(plusargs.begin(), plusargs.end());
}

void Interpreter::set_time_resolution_femtoseconds(
    const std::uint64_t femtoseconds)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot set SimIR time resolution after start"
        };
    }
    if (femtoseconds == 0) {
        throw std::invalid_argument {
            "SimIR time resolution must be positive"
        };
    }
    impl_->time_format.resolution_femtoseconds = femtoseconds;
    auto magnitude = femtoseconds;
    auto exponent = std::int32_t { -15 };
    while (magnitude >= 10 && exponent < 0) {
        magnitude /= 10;
        ++exponent;
    }
    impl_->time_format.units = exponent;
}

std::optional<std::uint64_t> Interpreter::take_class_allocation_seed()
{
    return std::exchange(impl_->pending_class_allocation_seed, std::nullopt);
}

void Interpreter::set_class_allocate_context_hook(
    ClassAllocateContextHook hook)
{
    impl_->class_allocate_context_hook = std::move(hook);
}

void Interpreter::set_coverage_sample_hook(CoverageSampleHook hook)
{
    impl_->coverage_sample_hook = std::move(hook);
}

void Interpreter::set_coverage_query_hook(CoverageQueryHook hook)
{
    impl_->coverage_query_hook = std::move(hook);
}

void Interpreter::set_vhdl_psl_api_hook(VhdlPslApiHook hook)
{
    impl_->vhdl_psl_api_hook = std::move(hook);
}

void Interpreter::set_coverage_control_hook(CoverageControlHook hook)
{
    impl_->coverage_control_hook = std::move(hook);
}

void Interpreter::set_system_command_hook(SystemCommandHook hook)
{
    impl_->system_command_hook = std::move(hook);
}

void Interpreter::set_vcd_control_hook(VcdControlHook hook)
{
    impl_->vcd_control_hook = std::move(hook);
}

void Interpreter::set_coverage_database_control_hook(
    CoverageDatabaseControlHook hook)
{
    impl_->coverage_database_control_hook = std::move(hook);
}

void Interpreter::set_class_property_read_hook(ClassPropertyReadHook hook)
{
    impl_->class_property_read_hook = std::move(hook);
}

void Interpreter::set_class_property_write_hook(ClassPropertyWriteHook hook)
{
    impl_->class_property_write_hook = std::move(hook);
}

void Interpreter::set_class_property_container_hook(
    ClassPropertyContainerHook hook)
{
    impl_->class_property_container_hook = std::move(hook);
}

void Interpreter::set_class_method_call_context_hook(
    ClassMethodCallContextHook hook)
{
    impl_->class_method_call_context_hook = std::move(hook);
}

void Interpreter::set_class_static_property_read_hook(
    ClassStaticPropertyReadHook hook)
{
    impl_->class_static_property_read_hook = std::move(hook);
}

void Interpreter::set_class_static_property_write_hook(
    ClassStaticPropertyWriteHook hook)
{
    impl_->class_static_property_write_hook = std::move(hook);
}

void Interpreter::set_class_static_method_call_context_hook(
    ClassStaticMethodCallContextHook hook)
{
    impl_->class_static_method_call_context_hook = std::move(hook);
}

void Interpreter::set_dpi_function_call_hook(DpiFunctionCallHook hook)
{
    impl_->dpi_function_call_hook = std::move(hook);
}

SignalId Interpreter::add_signal(Signal signal)
{
    if (impl_->started || impl_->start_in_progress) {
        throw std::logic_error("cannot add a SimIR signal after start");
    }
    const auto id = static_cast<SignalId>(impl_->signals.size());
    if (static_cast<std::size_t>(id) != impl_->signals.size()) {
        throw std::length_error("too many SimIR signals");
    }
    const auto valid_strength = [](const StrengthRank rank) {
        return static_cast<std::underlying_type_t<StrengthRank>>(rank)
            <= static_cast<std::underlying_type_t<StrengthRank>>(
                StrengthRank::supply);
    };
    if (!valid_strength(signal.implicit_drive_strength.zero)
        || !valid_strength(signal.implicit_drive_strength.one)
        || (signal.charge_strength
            && !valid_strength(*signal.charge_strength))) {
        throw std::invalid_argument { "invalid SimIR signal strength metadata" };
    }
    if (signal.systemverilog_scalar != SystemVerilogScalarKind::None) {
        const auto scalar = decode_systemverilog_scalar_payload(
            signal.initial_value, signal.systemverilog_scalar);
        const auto classification = scalar
            ? classify_systemverilog_scalar(scalar.value)
            : SystemVerilogScalarClassification {
                  .error = SystemVerilogScalarError::InvalidKind
              };
        const bool valid_value = signal.systemverilog_scalar
                == SystemVerilogScalarKind::Time
            ? signal.initial_value.width() == 64U
                && !signal.initial_value.is_logic9()
            : scalar
                && (signal.systemverilog_scalar
                        == SystemVerilogScalarKind::Chandle
                    || (classification && classification.finite));
        if (!valid_value
            || signal.resolution != ResolutionKind::none
            || signal.implicit_driver || signal.charge_strength
            || signal.charge_decay) {
            throw std::invalid_argument {
                "invalid SimIR SystemVerilog scalar signal metadata"
            };
        }
    }
    signal.initial_value = Interpreter::Impl::coerce_value_kind(
        std::move(signal.initial_value), signal.value_kind);
    // A net's X bits come from delayed continuous assignments, whose
    // drivers read X until their first value matures.
    if ((signal.resolution == ResolutionKind::sv_wire
            || signal.resolution == ResolutionKind::sv_wand
            || signal.resolution == ResolutionKind::sv_wor)
        && !signal.initial_value.is_logic9()) {
        for (std::size_t bit = 0; bit < signal.initial_value.width(); ++bit) {
            if (signal.initial_value.get(bit) == Logic4::x) {
                impl_->delayed_net_initial_values.emplace(
                    id, signal.initial_value);
                break;
            }
        }
    }
    impl_->driven_values.push_back(signal.initial_value);
    impl_->driver_values.emplace_back();
    impl_->direct_single_driver_routes.emplace_back();
    impl_->direct_single_driver_word_scratch.emplace_back();
    impl_->direct_single_driver_logic9_word_scratch.emplace_back();
    impl_->native_word_update_signals.emplace_back();
    impl_->native_logic9_word_update_signals.emplace_back();
    impl_->direct_single_driver_processes.push_back(
        std::numeric_limits<ProcessId>::max());
    impl_->stable_single_writer_processes.push_back(
        std::numeric_limits<ProcessId>::max());
    impl_->signal_transaction_observed.push_back(false);
    impl_->signal_writer_counts.push_back(0U);
    impl_->forced_driver_values.emplace_back();
    impl_->forced_driver_masks.emplace_back();
    impl_->external_driver_values.emplace_back();
    impl_->charge_decay_handles.emplace_back();
    impl_->charge_values.push_back(
        signal.charge_strength
            ? std::make_unique<PackedLogic4>(signal.initial_value)
            : nullptr);
    impl_->signal_last_values.push_back(signal.initial_value);
    impl_->signal_selection_histories.emplace_back();
    impl_->signal_selection_transaction_region_pending.push_back(0U);
    impl_->signal_selection_transaction_pending_listed.push_back(0U);
    impl_->signal_container_aggregate_aliases.emplace_back(std::nullopt);
    impl_->aggregate_signal_current_projection.emplace_back(std::nullopt);
    impl_->aggregate_signal_stored_projection.emplace_back(std::nullopt);
    impl_->aggregate_signal_batches.emplace_back();
    impl_->aggregate_signal_current_projection_revisions.push_back(0U);
    impl_->aggregate_signal_stored_projection_revisions.push_back(0U);
    impl_->aggregate_signal_last_changes.emplace_back(std::nullopt);
    impl_->aggregate_signal_last_projection_revisions.push_back(0U);
    impl_->forced_values.emplace_back();
    impl_->forced_masks.emplace_back();
    if (!signal.initial_value.is_logic9()
        && signal.initial_value.width() <= 64U) {
        const auto word = signal.initial_value.unchecked_low_word();
        impl_->direct_signal_aval.push_back(word.aval);
        impl_->direct_signal_bval.push_back(word.bval);
        impl_->direct_signal_last_aval.push_back(word.aval);
        impl_->direct_signal_last_bval.push_back(word.bval);
    } else {
        impl_->direct_signal_aval.push_back(0U);
        impl_->direct_signal_bval.push_back(0U);
        impl_->direct_signal_last_aval.push_back(0U);
        impl_->direct_signal_last_bval.push_back(0U);
    }
    if (signal.initial_value.is_logic9()
        && signal.initial_value.width() != 0U
        && signal.initial_value.width() <= 64U) {
        impl_->direct_signal_logic9_plane0.resize(id, 0U);
        impl_->direct_signal_logic9_plane1.resize(id, 0U);
        impl_->direct_signal_logic9_plane2.resize(id, 0U);
        impl_->direct_signal_logic9_plane3.resize(id, 0U);
        impl_->direct_signal_last_logic9_plane0.resize(id, 0U);
        impl_->direct_signal_last_logic9_plane1.resize(id, 0U);
        impl_->direct_signal_last_logic9_plane2.resize(id, 0U);
        impl_->direct_signal_last_logic9_plane3.resize(id, 0U);
        const auto word = signal.initial_value.logic9_low_word();
        impl_->direct_signal_logic9_plane0.push_back(word.planes[0]);
        impl_->direct_signal_logic9_plane1.push_back(word.planes[1]);
        impl_->direct_signal_logic9_plane2.push_back(word.planes[2]);
        impl_->direct_signal_logic9_plane3.push_back(word.planes[3]);
        impl_->direct_signal_last_logic9_plane0.push_back(word.planes[0]);
        impl_->direct_signal_last_logic9_plane1.push_back(word.planes[1]);
        impl_->direct_signal_last_logic9_plane2.push_back(word.planes[2]);
        impl_->direct_signal_last_logic9_plane3.push_back(word.planes[3]);
    }
    impl_->direct_signal_materialization_pending.push_back(0U);
    if (impl_->direct_wide_signal_aval.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error {
            "SimIR direct signal plane exceeds its native offset range"
        };
    }
    const auto wide_offset = static_cast<std::uint32_t>(
        impl_->direct_wide_signal_aval.size());
    impl_->direct_wide_signal_offsets.push_back(wide_offset);
    const auto words = signal.initial_value.aval_words().size();
    if (words > std::numeric_limits<std::uint32_t>::max()
            - impl_->direct_wide_signal_aval.size()) {
        throw std::length_error {
            "SimIR direct signal plane exceeds its native word range"
        };
    }
    if (!signal.initial_value.is_logic9()) {
        const auto aval = signal.initial_value.aval_words();
        const auto bval = signal.initial_value.bval_words();
        impl_->direct_wide_signal_aval.insert(
            impl_->direct_wide_signal_aval.end(),
            aval.begin(), aval.end());
        impl_->direct_wide_signal_bval.insert(
            impl_->direct_wide_signal_bval.end(),
            bval.begin(), bval.end());
    } else {
        const auto plane0 = signal.initial_value.logic9_plane_words(0U);
        const auto plane1 = signal.initial_value.logic9_plane_words(1U);
        const auto plane2 = signal.initial_value.logic9_plane_words(2U);
        const auto plane3 = signal.initial_value.logic9_plane_words(3U);
        impl_->direct_wide_signal_logic9_plane2.resize(wide_offset, 0U);
        impl_->direct_wide_signal_logic9_plane3.resize(wide_offset, 0U);
        impl_->direct_wide_signal_aval.insert(
            impl_->direct_wide_signal_aval.end(),
            plane0.begin(), plane0.end());
        impl_->direct_wide_signal_bval.insert(
            impl_->direct_wide_signal_bval.end(),
            plane1.begin(), plane1.end());
        impl_->direct_wide_signal_logic9_plane2.insert(
            impl_->direct_wide_signal_logic9_plane2.end(),
            plane2.begin(), plane2.end());
        impl_->direct_wide_signal_logic9_plane3.insert(
            impl_->direct_wide_signal_logic9_plane3.end(),
            plane3.begin(), plane3.end());
    }
    if (signal.vhdl_scalar_width != 0U) {
        impl_->vhdl_scalar_widths.emplace(id, signal.vhdl_scalar_width);
    }
    if (!signal.vhdl_conversion_drivers.empty()) {
        for (const auto& driver : signal.vhdl_conversion_drivers) {
            for (const auto source : driver.sources) {
                auto& dependents = impl_->vhdl_conversion_dependents[source];
                // Signal ids only grow, so an earlier push of this id can
                // only be the last entry.
                if (dependents.empty() || dependents.back() != id) {
                    dependents.push_back(id);
                }
            }
        }
        impl_->vhdl_conversion_drivers.emplace(
            id, std::move(signal.vhdl_conversion_drivers));
    }
    if (signal.vhdl_resolver) {
        // Each driver of a resolved VHDL signal starts at the signal's
        // initial value (IEEE 1076-2008 14.7.2).
        for (const auto source : signal.vhdl_resolver->sources) {
            impl_->vhdl_conversion_dependents[source].push_back(id);
        }
        impl_->vhdl_resolvers.emplace(id, std::move(*signal.vhdl_resolver));
        impl_->vhdl_driver_initial_values.emplace(id, signal.initial_value);
    }
    SignalHot hot {
        .initial_value = std::move(signal.initial_value),
        .resolution = signal.resolution,
        .value_kind = signal.value_kind,
        .systemverilog_scalar = signal.systemverilog_scalar,
        .event_variable = signal.event_variable,
        .has_implicit_driver = signal.implicit_driver.has_value(),
        .has_charge_strength = signal.charge_strength.has_value(),
    };
    SignalCold cold {
        .name = std::move(signal.name),
        .implicit_driver = std::move(signal.implicit_driver),
        .implicit_drive_strength = signal.implicit_drive_strength,
        .charge_strength = std::move(signal.charge_strength),
        .charge_decay = std::move(signal.charge_decay),
    };
    impl_->signals.push_back(std::move(hot));
    try {
        impl_->signal_cold.push_back(std::move(cold));
    } catch (...) {
        impl_->signals.pop_back();
        throw;
    }
    impl_->switch_endpoint_adjacency.emplace_back();
    impl_->switch_control_adjacency.emplace_back();
    impl_->invalidate_switch_components();
    impl_->event_identities.push_back(id);
    impl_->event_identity_members.emplace_back();
    if (impl_->signals.back().event_variable) {
        impl_->event_identity_members.back().push_back(id);
    }
    impl_->dynamic_fanout.emplace_back();
    impl_->event_states.emplace_back();
    impl_->signal_events.emplace_back();
    impl_->signal_event_scheduling_stamps.emplace_back();
    impl_->signal_transactions.emplace_back();
    impl_->signal_container_aliases.emplace_back();
    impl_->signal_container_element_aliases.emplace_back(std::nullopt);
    impl_->sampled_value_dependency_mask.push_back(0U);
    impl_->monitor_signal_watch_mask.push_back(0U);
    impl_->signal_value_revisions.push_back(1U);
    impl_->static_fanout_dirty = true;
    return id;
}

StringObjectId Interpreter::add_string_object(StringObject object)
{
    if (impl_->started || impl_->start_in_progress) {
        throw std::logic_error {
            "cannot add a SimIR string object after start"
        };
    }
    if (object.initial_value.size() > maximum_string_bytes) {
        throw std::length_error { "SimIR string object exceeds byte limit" };
    }
    (void)systemverilog_string_length(object.initial_value);
    const auto id = static_cast<StringObjectId>(impl_->string_objects.size());
    if (static_cast<std::size_t>(id)
        != impl_->string_objects.size()) {
        throw std::length_error { "too many SimIR string objects" };
    }
    impl_->string_objects.push_back(std::move(object));
    return id;
}

ContainerObjectId Interpreter::add_container_object(
    ContainerObject object)
{
    if (impl_->started || impl_->start_in_progress) {
        throw std::logic_error {
            "cannot add a SimIR container object after start"
        };
    }
    validate_container_value(object.initial_value);
    const auto id = static_cast<ContainerObjectId>(
        impl_->container_objects.size());
    if (static_cast<std::size_t>(id)
        != impl_->container_objects.size()) {
        throw std::length_error { "too many SimIR container objects" };
    }
    if (object.slice_alias) {
        const auto& alias = *object.slice_alias;
        if (alias.object >= id) {
            throw std::invalid_argument {
                "a SimIR container slice alias must reference an earlier object"
            };
        }
        const auto& source = impl_->container_objects[alias.object].initial_value.type;
        const auto& target = object.initial_value.type;
        const auto element_count =
            [](const std::int32_t left,
                const std::int32_t right) {
                return static_cast<std::uint64_t>(
                           left >= right
                               ? static_cast<std::int64_t>(left) - right
                               : static_cast<std::int64_t>(right) - left)
                    + 1U;
            };
        const auto source_low = std::min(source.index_left, source.index_right);
        const auto source_high = std::max(source.index_left, source.index_right);
        const bool direction_matches = alias.selected_left == alias.selected_right
            || (alias.selected_left >= alias.selected_right)
                == (source.index_left >= source.index_right);
        if (!source.fixed || !target.fixed
            || alias.selected_left < source_low
            || alias.selected_left > source_high
            || alias.selected_right < source_low
            || alias.selected_right > source_high
            || !direction_matches
            || element_count(
                   alias.selected_left,
                   alias.selected_right)
                != element_count(
                    target.index_left, target.index_right)
            || source.element_width != target.element_width
            || source.two_state != target.two_state
            || source.signed_elements != target.signed_elements) {
            throw std::invalid_argument {
                "invalid SimIR static-array slice alias"
            };
        }
    }
    const auto element_count = object.initial_value.elements.size();
    impl_->container_objects.push_back(std::move(object));
    impl_->container_value_reference_exposed.push_back(0U);
    impl_->container_value_reference_exposure_in_progress.push_back(0U);
    impl_->container_signal_aliases.push_back(std::nullopt);
    impl_->container_element_signal_aliases.emplace_back(element_count);
    impl_->container_alias_write_batches.emplace_back();
    impl_->container_aggregate_signal_aliases.push_back(std::nullopt);
    impl_->container_aggregate_current_revisions.push_back(1U);
    impl_->container_aggregate_stored_revisions.push_back(1U);
    impl_->container_materialized_revisions.push_back(std::nullopt);
    impl_->container_dynamic_fanout.emplace_back();
    return id;
}

void Interpreter::add_container_signal_alias(
    const ContainerSignalAlias alias)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR container signal alias after start"
        };
    }
    if (alias.object >= impl_->container_objects.size()
        || alias.signal >= impl_->signals.size()) {
        throw std::out_of_range {
            "SimIR container signal alias identifier is out of range"
        };
    }
    if (!alias.readable && !alias.writable) {
        throw std::invalid_argument {
            "a SimIR container signal alias must be readable or writable"
        };
    }
    if (impl_->container_signal_aliases[alias.object]
        || std::ranges::any_of(
            impl_->container_element_signal_aliases[alias.object],
            [](const auto& item) { return item.has_value(); })) {
        throw std::invalid_argument {
            "a SimIR container object has more than one signal alias"
        };
    }
    const auto& object = impl_->container_objects[alias.object];
    if (object.slice_alias) {
        throw std::invalid_argument {
            "a SimIR container slice cannot alias a packed signal"
        };
    }
    const auto width = container_signal_bridge_width(object.initial_value.type);
    if (!width
        || *width
            != impl_->signals[alias.signal].initial_value.width()) {
        throw std::invalid_argument {
            "a SimIR container signal alias has incompatible width or shape"
        };
    }
    auto& reverse_aliases = impl_->signal_container_aliases[alias.signal];
    reverse_aliases.reserve(reverse_aliases.size() + 1U);
    if (impl_->container_value_reference_exposed[alias.object]) {
        impl_->expose_signal_value_reference(alias.signal);
    }
    impl_->revoke_stable_writer_shadow_for_container_alias(alias.signal);
    impl_->container_signal_aliases[alias.object] = alias;
    reverse_aliases.push_back(alias.object);
    impl_->refresh_direct_single_driver_route(alias.signal);
}

void Interpreter::add_container_element_signal_alias(
    const ContainerElementSignalAlias alias)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR container element alias after start"
        };
    }
    if (alias.object >= impl_->container_objects.size()
        || alias.signal >= impl_->signals.size()) {
        throw std::out_of_range {
            "SimIR container element alias identifier is out of range"
        };
    }
    if (!alias.readable && !alias.writable) {
        throw std::invalid_argument {
            "a SimIR container element alias must be readable or writable"
        };
    }
    const auto& object = impl_->container_objects[alias.object];
    const auto& type = object.initial_value.type;
    if (object.slice_alias || !type.fixed || type.dimensions.empty()
        || (type.element_kind != ContainerElementKind::Packed
            && type.element_kind != ContainerElementKind::Scalar)
        || alias.ordinal
            >= impl_->container_element_signal_aliases[alias.object].size()
        || impl_->container_signal_aliases[alias.object]
        || impl_->container_aggregate_signal_aliases[alias.object]
        || impl_->container_element_signal_aliases[alias.object][alias.ordinal]
        || impl_->signal_container_element_aliases[alias.signal]
        || impl_->signal_container_aggregate_aliases[alias.signal]
        || !impl_->signal_container_aliases[alias.signal].empty()
        || impl_->signals[alias.signal].initial_value.width()
            != type.element_width
        || impl_->signals[alias.signal].value_kind != ValueKind::logic4
        || impl_->signals[alias.signal].initial_value.is_logic9()) {
        throw std::invalid_argument {
            "a SimIR container element alias has incompatible shape or duplicates"
        };
    }
    if (impl_->container_value_reference_exposed[alias.object]) {
        impl_->expose_signal_value_reference(alias.signal);
    }
    impl_->revoke_stable_writer_shadow_for_container_alias(alias.signal);
    impl_->container_element_signal_aliases[alias.object][alias.ordinal] = alias;
    impl_->signal_container_element_aliases[alias.signal]
        = std::pair { alias.object, static_cast<std::size_t>(alias.ordinal) };
    impl_->refresh_direct_single_driver_route(alias.signal);
}

void Interpreter::add_container_aggregate_signal_alias(
    const ContainerAggregateSignalAlias alias)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR aggregate signal alias after start"
        };
    }
    if (alias.object >= impl_->container_objects.size()
        || alias.signal >= impl_->signals.size()) {
        throw std::out_of_range {
            "SimIR aggregate signal alias identifier is out of range"
        };
    }
    if (!alias.readable) {
        throw std::invalid_argument {
            "a SimIR aggregate signal alias must be readable"
        };
    }
    const auto& object = impl_->container_objects[alias.object];
    const auto& type = object.initial_value.type;
    const auto& elements = impl_->container_element_signal_aliases[alias.object];
    const auto width = container_signal_bridge_width(type);
    if (object.slice_alias || !type.fixed || type.dimensions.empty()
        || type.element_kind != ContainerElementKind::Packed
        || type.element_width == 0U || type.two_state
        || !width || *width != impl_->signals[alias.signal].initial_value.width()
        || impl_->signals[alias.signal].initial_value.is_logic9()
        || impl_->signals[alias.signal].value_kind != ValueKind::logic4
        || impl_->signals[alias.signal].resolution == ResolutionKind::none
        || impl_->container_signal_aliases[alias.object]
        || impl_->container_aggregate_signal_aliases[alias.object]
        || elements.size() != object.initial_value.elements.size()
        || std::ranges::any_of(elements, [](const auto& element) {
            return !element || !element->readable || !element->writable;
        })
        || impl_->signal_container_aggregate_aliases[alias.signal]
        || impl_->signal_container_element_aliases[alias.signal]
        || !impl_->signal_container_aliases[alias.signal].empty()) {
        throw std::invalid_argument {
            "a SimIR aggregate alias has incompatible shape or backing elements"
        };
    }
    for (const auto& element : elements) {
        const auto& signal = impl_->signals[element->signal];
        if (signal.initial_value.width() != type.element_width
            || signal.initial_value.is_logic9()
            || signal.value_kind != ValueKind::logic4
            || signal.resolution != impl_->signals[alias.signal].resolution) {
            throw std::invalid_argument {
                "a SimIR aggregate alias has incompatible element signals"
            };
        }
    }
    // Allocate both wide projections before publishing either alias direction.
    // A failed registration must leave the original signal route usable.
    PackedLogic4 current_projection { *width, Logic4::zero };
    PackedLogic4 stored_projection { *width, Logic4::zero };
    const auto element_width = object.initial_value.type.element_width;
    const auto count = elements.size();
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto signal = elements[ordinal]->signal;
        const auto offset = (count - ordinal - 1U) * element_width;
        current_projection.insert_bits(
            impl_->signals[signal].initial_value, offset);
        stored_projection.insert_bits(impl_->driven_values[signal], offset);
    }
    if (impl_->container_value_reference_exposed[alias.object]) {
        impl_->expose_signal_value_reference(alias.signal);
    }
    impl_->revoke_stable_writer_shadow_for_container_alias(alias.signal);
    impl_->container_aggregate_signal_aliases[alias.object] = alias;
    impl_->signal_container_aggregate_aliases[alias.signal] = alias.object;
    impl_->has_container_aggregate_aliases = true;
    impl_->aggregate_signal_current_projection[alias.signal].emplace(
        std::move(current_projection));
    impl_->aggregate_signal_stored_projection[alias.signal].emplace(
        std::move(stored_projection));
    impl_->aggregate_signal_current_projection_revisions[alias.signal]
        = impl_->container_aggregate_current_revisions[alias.object];
    impl_->aggregate_signal_stored_projection_revisions[alias.signal]
        = impl_->container_aggregate_stored_revisions[alias.object];
    impl_->refresh_direct_single_driver_route(alias.signal);
}

void Interpreter::Impl::require_writable_aggregate_signal(
    const SignalId signal_id) const
{
    if (signal_id >= signal_container_aggregate_aliases.size()
        || !signal_container_aggregate_aliases[signal_id]) {
        return;
    }
    const auto object = *signal_container_aggregate_aliases[signal_id];
    const auto& alias = container_aggregate_signal_aliases.at(object);
    if (alias && !alias->writable) {
        throw std::logic_error {
            "the aggregate signal alias is observation-only"
        };
    }
}

void Interpreter::Impl::promote_container_alias_authority(
    const SignalId signal_id)
{
    std::optional<ContainerObjectId> object;
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        object = *signal_container_aggregate_aliases[signal_id];
    } else if (signal_id < signal_container_element_aliases.size()
        && signal_container_element_aliases[signal_id]) {
        object = signal_container_element_aliases[signal_id]->first;
    }
    if (!object || *object >= container_aggregate_signal_aliases.size()
        || !container_aggregate_signal_aliases[*object]
        || retained_aggregate_authorities.contains(*object)) {
        return;
    }

    const auto proxy = container_aggregate_signal_aliases[*object]->signal;
    materialize_direct_signal(proxy);
    for (const auto& alias : container_element_signal_aliases.at(*object)) {
        materialize_direct_signal(alias->signal);
    }
    auto& current = *aggregate_signal_current_projection.at(proxy);
    const auto& container = get_container_object(*object).initial_value;
    const auto& aliases = container_element_signal_aliases.at(*object);
    const auto width = container.type.element_width;
    const auto count = aliases.size();
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        current.insert_bits(
            signals.at(aliases[ordinal]->signal).initial_value,
            (count - ordinal - 1U) * width);
    }
    aggregate_signal_current_projection_revisions[proxy]
        = container_aggregate_current_revisions[*object];
    auto& stored = *aggregate_signal_stored_projection.at(proxy);
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        stored.insert_bits(
            driven_values.at(aliases[ordinal]->signal),
            (count - ordinal - 1U) * width);
    }
    aggregate_signal_stored_projection_revisions[proxy]
        = container_aggregate_stored_revisions[*object];
    retained_aggregate_authorities.insert(*object);
}

void Interpreter::Impl::promote_all_container_alias_authorities()
{
    for (const auto& alias : container_aggregate_signal_aliases) {
        if (alias) {
            promote_container_alias_authority(alias->signal);
        }
    }
}

bool Interpreter::Impl::container_alias_authority_active(
    const ContainerObjectId object) const noexcept
{
    return retained_aggregate_authorities.contains(object);
}

void Interpreter::Impl::begin_container_alias_write_batch(
    const ContainerObjectId object,
    const bool has_driver,
    const bool phasewise_storage)
{
    auto& batch = container_alias_write_batches.at(object);
    if (has_bidirectional_switches || !module_paths.empty()
        || !module_timing_checks.empty()) {
        throw std::logic_error {
            "whole-container alias writes require a design without switch, "
            "module-path, or timing-check cascades"
        };
    }
    if (stored_signal_change_hook && !phasewise_storage) {
        throw std::logic_error {
            "whole-container writes with stored observers require an "
            "atomic family storage transition"
        };
    }
    if (has_driver && driver_change_hook) {
        throw std::logic_error {
            "whole-container driver writes cannot defer driver observers "
            "without a phasewise driver update"
        };
    }

    const auto& aliases = container_element_signal_aliases.at(object);
    std::vector<ContainerAliasLeafObserver> prepared;
    prepared.reserve(aliases.size());
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        const auto& alias = aliases[ordinal];
        if (!alias || !alias->readable || !alias->writable
            || signals.at(alias->signal).event_variable) {
            throw std::logic_error {
                "whole-container alias writes require a complete, "
                "non-cascading element map"
            };
        }
        prepared.push_back({
            alias->signal, ordinal,
            signals.at(alias->signal).initial_value });
    }

    batch.frames.push_back({ false, false, std::move(prepared) });
}

ContainerAliasLeafObserver*
Interpreter::Impl::container_alias_leaf_observer(
    const SignalId signal_id) noexcept
{
    if (signal_id >= signal_container_element_aliases.size()
        || !signal_container_element_aliases[signal_id]) {
        return nullptr;
    }
    const auto [object, ordinal]
        = *signal_container_element_aliases[signal_id];
    if (object >= container_alias_write_batches.size()) {
        return nullptr;
    }
    auto& batch = container_alias_write_batches[object];
    if (batch.frames.empty()
        || ordinal >= batch.frames.back().leaves.size()) {
        return nullptr;
    }
    auto& leaf = batch.frames.back().leaves[ordinal];
    return leaf.signal == signal_id ? &leaf : nullptr;
}

std::vector<ContainerAliasLeafObserver>
Interpreter::Impl::take_container_alias_write_batch(
    const ContainerObjectId object,
    bool& changed)
{
    if (object >= container_alias_write_batches.size()) {
        changed = false;
        return { };
    }
    auto& batch = container_alias_write_batches[object];
    if (batch.frames.empty()) {
        changed = false;
        return { };
    }
    auto& frame = batch.frames.back();
    if (!frame.captured) {
        capture_container_alias_write_batch(object);
    }
    changed = frame.changed;
    auto leaves = std::move(frame.leaves);
    batch.frames.pop_back();
    return leaves;
}

void Interpreter::Impl::finish_container_alias_write_batch(
    const ContainerObjectId object,
    std::vector<ContainerAliasLeafObserver> leaves,
    const bool changed)
{
    std::exception_ptr callback_failure;
    const auto invoke_observer = [&callback_failure](auto&& observer) {
        try {
            observer();
        } catch (...) {
            if (!callback_failure) {
                callback_failure = std::current_exception();
            }
        }
    };
    for (const auto& leaf : leaves) {
        if (leaf.current_changed && signal_change_hook) {
            invoke_observer([&] {
                signal_change_hook(
                    leaf.signal, leaf.current, scheduler.now());
            });
        }
        if (leaf.scalar_changed && scalar_signal_change_hook) {
            invoke_observer([&] {
                const auto scalar = decode_systemverilog_scalar_payload(
                    leaf.current,
                    signals.at(leaf.signal).systemverilog_scalar);
                if (!scalar) {
                    throw std::logic_error {
                        "SimIR scalar alias published an invalid payload"
                    };
                }
                scalar_signal_change_hook(
                    leaf.signal, scalar.value, scheduler.now());
            });
        }
        if (leaf.element_changed && container_element_change_hook) {
            invoke_observer([&] {
                container_element_change_hook(
                    object, leaf.ordinal, leaf.current, scheduler.now());
            });
        }
    }
    if (changed && container_object_change_hook) {
        invoke_observer([&] {
            container_object_change_hook(object, scheduler.now());
        });
    }
    if (callback_failure) {
        std::rethrow_exception(callback_failure);
    }
}

void Interpreter::Impl::capture_container_alias_write_batch(
    const ContainerObjectId object)
{
    if (object >= container_alias_write_batches.size()) {
        return;
    }
    auto& batch = container_alias_write_batches[object];
    if (batch.frames.empty() || batch.frames.back().captured) {
        return;
    }
    auto& frame = batch.frames.back();
    for (auto& leaf : frame.leaves) {
        leaf.current = signals.at(leaf.signal).initial_value;
    }
    frame.captured = true;
}

void Interpreter::Impl::begin_aggregate_signal_batch(
    const SignalId signal_id,
    const SignalChangeOrigin origin)
{
    if (signal_id >= signal_container_aggregate_aliases.size()
        || !signal_container_aggregate_aliases[signal_id]) {
        return;
    }
    auto& batch = aggregate_signal_batches.at(signal_id);
    if (batch.depth == std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error {
            "aggregate signal update nesting exceeds its limit"
        };
    }
    if (batch.depth == 0U) {
        batch.previous.emplace(aggregate_signal_current_value(signal_id));
        batch.changed = false;
        batch.event_prepared = false;
        batch.stored_changed = false;
        batch.stored_observer_notified = false;
        batch.transaction_changed = false;
        batch.notify_fanout = false;
        batch.driver_changed = false;
        batch.driver.reset();
        batch.origin = origin;
    }
    ++batch.depth;
}

void Interpreter::Impl::finish_aggregate_signal_batch(
    const SignalId signal_id,
    const bool defer_object_hook)
{
    if (signal_id >= aggregate_signal_batches.size()) {
        return;
    }
    auto& batch = aggregate_signal_batches[signal_id];
    if (batch.depth == 0U) {
        return;
    }
    --batch.depth;
    if (batch.depth != 0U) {
        return;
    }

    const auto previous = std::move(batch.previous);
    const bool changed = batch.changed;
    const bool event_prepared = batch.event_prepared;
    const bool stored_changed = batch.stored_changed;
    const bool stored_observer_notified
        = batch.stored_observer_notified;
    const bool transaction_changed = batch.transaction_changed;
    const bool notify_fanout = batch.notify_fanout;
    const bool driver_changed = batch.driver_changed;
    const auto driver = batch.driver;
    const auto origin = batch.origin;
    batch = { };

    if (!previous) {
        return;
    }
    const auto object
        = *signal_container_aggregate_aliases.at(signal_id);
    const auto revision = container_aggregate_current_revisions.at(object);
    if (changed) {
        signal_last_values.at(signal_id) = *previous;
        aggregate_signal_last_changes.at(signal_id).reset();
        aggregate_signal_last_projection_revisions.at(signal_id) = revision;
        auto& value_revision = signal_value_revisions.at(signal_id);
        if (value_revision == std::numeric_limits<std::uint64_t>::max()) {
            value_revision = 1U;
            std::ranges::fill(
                container_materialized_revisions, std::nullopt);
        } else {
            ++value_revision;
        }
        note_signal_revision(signal_id);
    }
    if (transaction_changed) {
        note_signal_transaction(signal_id, notify_fanout, origin);
    }
    if (!defer_object_hook) {
        if (stored_changed && stored_signal_change_hook
            && !stored_observer_notified) {
            stored_signal_change_hook(signal_id, scheduler.now());
        }
        if (driver_changed && driver_change_hook && driver) {
            driver_change_hook(*driver, signal_id, scheduler.now());
        }
        if (changed) {
            publish_aggregate_signal_value_change(
                signal_id, notify_fanout, origin, nullptr,
                event_prepared);
        }
        if (stored_changed) {
            for (const auto process : container_dynamic_fanout.at(object)) {
                queue_next_delta(process, origin);
            }
            if (container_object_change_hook) {
                container_object_change_hook(object, scheduler.now());
            }
        }
        return;
    }
    std::exception_ptr callback_failure;
    const auto invoke_observer = [&callback_failure](auto&& observer) {
        try {
            observer();
        } catch (...) {
            if (!callback_failure) {
                callback_failure = std::current_exception();
            }
        }
    };
    if (stored_changed && stored_signal_change_hook
        && !stored_observer_notified) {
        invoke_observer([&] {
            stored_signal_change_hook(signal_id, scheduler.now());
        });
    }
    if (driver_changed && driver_change_hook && driver) {
        invoke_observer([&] {
            driver_change_hook(*driver, signal_id, scheduler.now());
        });
    }
    if (changed) {
        publish_aggregate_signal_value_change(
            signal_id, notify_fanout, origin, &callback_failure,
            event_prepared);
    }
    if (stored_changed) {
        for (const auto process : container_dynamic_fanout.at(object)) {
            queue_next_delta(process, origin);
        }
        const bool alias_write_batch_active
            = object < container_alias_write_batches.size()
            && !container_alias_write_batches[object].frames.empty();
        if (!defer_object_hook && !alias_write_batch_active
            && container_object_change_hook) {
            invoke_observer([&] {
                container_object_change_hook(object, scheduler.now());
            });
        }
    }
    if (callback_failure) {
        std::rethrow_exception(callback_failure);
    }
}

void Interpreter::Impl::note_aggregate_leaf_current_change(
    const SignalId signal_id)
{
    if (signal_id >= signal_container_element_aliases.size()
        || !signal_container_element_aliases[signal_id]) {
        return;
    }
    const auto object = signal_container_element_aliases[signal_id]->first;
    if (object >= container_aggregate_current_revisions.size()) {
        return;
    }
    if (object < container_aggregate_signal_aliases.size()
        && container_aggregate_signal_aliases[object]) {
        const auto proxy = container_aggregate_signal_aliases[object]->signal;
        auto& batch = aggregate_signal_batches[proxy];
        if (batch.depth != 0U) {
            batch.changed = true;
            auto& revision = container_aggregate_current_revisions[object];
            if (revision == std::numeric_limits<std::uint64_t>::max()) {
                revision = 1U;
                aggregate_signal_current_projection_revisions[proxy] = 0U;
            } else {
                ++revision;
            }
            aggregate_signal_current_projection_revisions[proxy]
                = container_alias_authority_active(object) ? revision : 0U;
            aggregate_signal_last_projection_revisions[proxy] = revision;
            if (container_alias_authority_active(object)) {
                const auto ordinal
                    = signal_container_element_aliases[signal_id]->second;
                const auto& container = get_container_object(object).initial_value;
                const auto count
                    = container_element_signal_aliases[object].size();
                aggregate_signal_current_projection[proxy]->insert_bits(
                    signals[signal_id].initial_value,
                    (count - ordinal - 1U)
                        * container.type.element_width);
            }
            return;
        }
        const auto ordinal
            = signal_container_element_aliases[signal_id]->second;
        if (proxy < aggregate_signal_last_changes.size()) {
            aggregate_signal_last_changes[proxy] = std::pair {
                ordinal, signal_last_values[signal_id]
            };
            aggregate_signal_last_projection_revisions[proxy] = 0U;
            if (proxy < signal_value_revisions.size()) {
                ++signal_value_revisions[proxy];
                note_signal_revision(proxy);
                if (signal_value_revisions[proxy] == 0U) {
                    signal_value_revisions[proxy] = 1U;
                }
            }
        }
    }
    auto& revision = container_aggregate_current_revisions[object];
    if (revision == std::numeric_limits<std::uint64_t>::max()) {
        revision = 1U;
        if (container_aggregate_signal_aliases[object]) {
            const auto proxy
                = container_aggregate_signal_aliases[object]->signal;
            aggregate_signal_current_projection_revisions[proxy] = 0U;
        }
    } else {
        ++revision;
    }
    if (container_aggregate_signal_aliases[object]
        && container_alias_authority_active(object)) {
        const auto proxy = container_aggregate_signal_aliases[object]->signal;
        const auto ordinal
            = signal_container_element_aliases[signal_id]->second;
        const auto& container = get_container_object(object).initial_value;
        const auto count = container_element_signal_aliases[object].size();
        aggregate_signal_current_projection[proxy]->insert_bits(
            signals[signal_id].initial_value,
            (count - ordinal - 1U) * container.type.element_width);
        aggregate_signal_current_projection_revisions[proxy] = revision;
    }
}

void Interpreter::Impl::note_aggregate_leaf_stored_change(
    const SignalId signal_id)
{
    if (signal_id >= signal_container_element_aliases.size()
        || !signal_container_element_aliases[signal_id]) {
        return;
    }
    const auto object = signal_container_element_aliases[signal_id]->first;
    if (object >= container_aggregate_stored_revisions.size()) {
        return;
    }
    auto& revision = container_aggregate_stored_revisions[object];
    if (revision == std::numeric_limits<std::uint64_t>::max()) {
        revision = 1U;
        if (container_aggregate_signal_aliases[object]) {
            const auto proxy
                = container_aggregate_signal_aliases[object]->signal;
            aggregate_signal_stored_projection_revisions[proxy] = 0U;
        }
    } else {
        ++revision;
    }
    if (container_aggregate_signal_aliases[object]
        && container_alias_authority_active(object)) {
        const auto proxy = container_aggregate_signal_aliases[object]->signal;
        const auto ordinal
            = signal_container_element_aliases[signal_id]->second;
        const auto& container = get_container_object(object).initial_value;
        const auto count = container_element_signal_aliases[object].size();
        aggregate_signal_stored_projection[proxy]->insert_bits(
            driven_values[signal_id],
            (count - ordinal - 1U) * container.type.element_width);
        aggregate_signal_stored_projection_revisions[proxy] = revision;
    }
}

const PackedLogic4& Interpreter::Impl::aggregate_signal_current_value(
    const SignalId signal_id)
{
    (void)get_signal(signal_id);
    const auto object
        = *signal_container_aggregate_aliases.at(signal_id);
    auto& projection = *aggregate_signal_current_projection.at(signal_id);
    const auto revision = container_aggregate_current_revisions.at(object);
    if (aggregate_signal_current_projection_revisions[signal_id]
        != revision) {
        const auto& value = get_container_object(object).initial_value;
        const auto& aliases = container_element_signal_aliases.at(object);
        const auto width = value.type.element_width;
        const auto count = value.elements.size();
        for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
            const auto leaf = aliases[ordinal]->signal;
            projection.insert_bits(
                get_signal(leaf).initial_value,
                (count - ordinal - 1U) * width);
        }
        aggregate_signal_current_projection_revisions[signal_id]
            = revision;
    }
    return projection;
}

const PackedLogic4& Interpreter::Impl::aggregate_signal_last_value(
    const SignalId signal_id)
{
    const auto& batch = aggregate_signal_batches.at(signal_id);
    if (batch.depth != 0U && batch.previous) {
        return *batch.previous;
    }
    const auto object
        = *signal_container_aggregate_aliases.at(signal_id);
    const auto revision = container_aggregate_current_revisions.at(object);
    auto& projection = signal_last_values.at(signal_id);
    if (aggregate_signal_last_projection_revisions.at(signal_id)
        != revision) {
        projection = aggregate_signal_current_value(signal_id);
        const auto& change = aggregate_signal_last_changes.at(signal_id);
        if (change) {
            const auto& type = get_container_object(object).initial_value.type;
            const auto count = container_element_signal_aliases.at(object).size();
            projection.insert_bits(
                change->second,
                (count - change->first - 1U) * type.element_width);
        }
        aggregate_signal_last_projection_revisions[signal_id] = revision;
    }
    return projection;
}

const PackedLogic4& Interpreter::Impl::logical_signal_value(
    const SignalId signal_id)
{
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        return aggregate_signal_current_value(signal_id);
    }
    return get_signal(signal_id).initial_value;
}

const PackedLogic4& Interpreter::Impl::logical_signal_last_value(
    const SignalId signal_id)
{
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        return aggregate_signal_last_value(signal_id);
    }
    (void)get_signal(signal_id);
    return signal_last_values.at(signal_id);
}

void Interpreter::reserve_process_capacity(const std::size_t capacity)
{
    if (impl_->started || impl_->start_in_progress
        || !impl_->processes.empty()) {
        throw std::logic_error {
            "process capacity must be reserved before registration and start"
        };
    }
    impl_->processes.reserve_initial(capacity);
}

ProcessId Interpreter::add_process(Process process)
{
    if (impl_->start_in_progress) {
        throw std::logic_error {
            "cannot add a SimIR process during startup actions"
        };
    }
    if (impl_->validation_only) {
        throw std::logic_error(
            "cannot add an executable SimIR process to a validation-only interpreter");
    }
    return add_process_impl(process, &process);
}

void Interpreter::register_package_static_startup_callback(
    const std::uint64_t startup_order,
    const std::span<const SignalId> writable_signal_ids,
    const std::span<const StringObjectId> writable_string_object_ids,
    const std::span<const ContainerObjectId> writable_container_object_ids,
    std::function<void()> callback)
{
    if (impl_->started || impl_->package_static_startup_registration_closed) {
        throw std::logic_error {
            "package static startup callbacks must be registered before start"
        };
    }
    if (impl_->validation_only || !callback) {
        throw std::invalid_argument {
            "invalid package static startup callback"
        };
    }
    if (std::ranges::any_of(
            impl_->package_static_startup_callbacks,
            [&](const auto& existing) {
                return existing.startup_order == startup_order;
            })) {
        throw std::invalid_argument {
            "package static startup callback order is not unique"
        };
    }
    const auto copy_ordered_ids = []<typename Id>(
        const std::span<const Id> source,
        const std::size_t limit,
        const char* message) {
        std::vector<Id> result(source.begin(), source.end());
        std::ranges::sort(result);
        if (std::ranges::adjacent_find(result) != result.end()
            || std::ranges::any_of(result, [&](const auto id) {
                   return static_cast<std::size_t>(id) >= limit;
               })) {
            throw std::invalid_argument { message };
        }
        return result;
    };
    auto registration = Impl::PackageStaticStartupCallback {
        .startup_order = startup_order,
        .writable_signal_ids = copy_ordered_ids(
            writable_signal_ids, impl_->signals.size(),
            "package static startup callback has an invalid signal allowlist"),
        .writable_string_object_ids = copy_ordered_ids(
            writable_string_object_ids, impl_->string_objects.size(),
            "package static startup callback has an invalid string allowlist"),
        .writable_container_object_ids = copy_ordered_ids(
            writable_container_object_ids, impl_->container_objects.size(),
            "package static startup callback has an invalid container allowlist"),
        .callback = std::move(callback),
    };
    impl_->package_static_startup_callbacks.push_back(
        std::move(registration));
}

ProcessId Interpreter::validate_process(const Process& process)
{
    if (!impl_->validation_only && !impl_->processes.empty()) {
        throw std::logic_error(
            "cannot add a validation-only SimIR process after executable processes");
    }
    impl_->validation_only = true;
    return add_process_impl(process, nullptr);
}

ProcessId Interpreter::add_process_impl(
    const Process& process,
    Process* const owned_process)
{
    return impl_->add_process_program_impl(
        ProcessProgramView { process }, { }, owned_process, nullptr,
        owned_process != nullptr);
}

const Interpreter::Impl::OperationBodyKinds* Interpreter::Impl::body_kinds(
    const OperationList& operations)
{
    if (operations.empty() || operations.has_operation_overrides()) {
        return nullptr;
    }
    const auto [entry, inserted]
        = operation_body_kinds.try_emplace(operations.body_identity());
    auto& summary = entry->second;
    if (inserted) {
        summary.body = operations;
        const auto* const body = operations.data();
        for (std::size_t index = 0U; index < operations.size(); ++index) {
            const auto& operation = body[index];
            summary.kinds.set(
                static_kernel_detail::operation_kind_ordinal(operation));
            if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
                if (read->kind != SignalReadKind::current) {
                    summary.sampled_read = true;
                    summary.sampled_reads.push_back({
                        read->signal, read->clock, read->gate });
                }
            } else if (const auto* wait
                       = operation_get_if<WaitOn>(&operation)) {
                summary.transaction_wait = summary.transaction_wait
                    || std::ranges::find(wait->edges, EdgeKind::transaction)
                        != wait->edges.end();
            }
        }
    }
    return &summary;
}

void Interpreter::Impl::observe_signal_transaction(const SignalId signal)
{
    if (signal >= signals.size()) {
        throw std::invalid_argument(
            "process transaction observation references invalid signal");
    }
    if (signal_transaction_observed[signal]) {
        return;
    }
    signal_transaction_observed[signal] = true;
    stable_single_writer_processes[signal]
        = std::numeric_limits<ProcessId>::max();
    ++signal_writer_revision;
    if (signal_writer_revision == 0U) {
        throw std::overflow_error {
            "SimIR signal-writer topology revision overflow"
        };
    }
}

void Interpreter::Impl::record_sampled_dependencies(
    const ProcessProgramView& process)
{
    const auto record_sampled_dependency = [&](const SignalId signal) {
        if (signal >= signals.size()
            || signal >= sampled_value_dependency_mask.size()) {
            sampled_value_dependencies_unknown = true;
            return;
        }
        sampled_value_dependency_mask[signal] = 1U;
    };
    const auto& operations = process.operations();
    // A shared body lists its sampled reads once; only the per-instance
    // signal remap is applied here.
    if (const auto* const kinds = body_kinds(operations)) {
        for (const auto& read : kinds->sampled_reads) {
            requires_sampled_values = true;
            record_sampled_dependency(operations.signal(read.signal));
            if (read.clock) {
                record_sampled_dependency(operations.signal(*read.clock));
            }
            if (read.gate) {
                record_sampled_dependency(operations.signal(*read.gate));
            }
        }
        return;
    }
    // Like transaction observation in add_process_program_impl, map just
    // the IDs that contribute to the sampled-value mask without copying
    // expanded operation payloads.
    for (std::size_t instruction = 0U;
         instruction < operations.size(); ++instruction) {
        const auto& operation = operations[instruction];
        const auto* read = operation_get_if<ReadSignal>(&operation);
        if (read == nullptr || read->kind == SignalReadKind::current) {
            continue;
        }
        requires_sampled_values = true;
        record_sampled_dependency(operations.signal(read->signal));
        if (read->clock) {
            record_sampled_dependency(operations.signal(*read->clock));
        }
        if (read->gate) {
            record_sampled_dependency(operations.signal(*read->gate));
        }
    }
}

ProcessId Interpreter::Impl::add_process_program_impl(
    ProcessProgramView process,
    std::shared_ptr<const ProcessProgramTemplate> common_program,
    Process* const owned_process,
    ProcessInstanceProgram* const owned_instance,
    const bool executable,
    const ProcessProgramView* const registration)
{
    if (owned_process != nullptr && owned_instance != nullptr) {
        throw std::logic_error {
            "a SimIR process cannot have two registration owners"
        };
    }
    const auto register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            process.register_value_kinds());
    const auto container_register_types
        = process_layout_detail::ProcessLayoutAccess::view(
            process.container_register_types());
    if (started) {
        throw std::logic_error("cannot add a SimIR process after start");
    }
    const auto id = static_cast<ProcessId>(processes.size());
    if (static_cast<std::size_t>(id) != processes.size()) {
        throw std::length_error("too many SimIR processes");
    }
    if (process.id() != id) {
        throw std::invalid_argument("SimIR process IDs must be dense and ordered");
    }
    const auto& signal_remap = process.operations().signal_remap();
    for (std::size_t index = 0U; index < signal_remap.size(); ++index) {
        const auto [canonical, actual] = signal_remap[index];
        if (actual >= signals.size()
            || (index != 0U
                && signal_remap[index - 1U].first >= canonical)) {
            throw std::invalid_argument {
                "SimIR process signal remap is invalid"
            };
        }
    }
    const auto valid_strength = [](const StrengthRank rank) {
        return static_cast<std::underlying_type_t<StrengthRank>>(rank)
            <= static_cast<std::underlying_type_t<StrengthRank>>(
                StrengthRank::supply);
    };
    if (!valid_strength(process.drive_strength().zero)
        || !valid_strength(process.drive_strength().one)) {
        throw std::invalid_argument { "invalid SimIR process drive strength" };
    }
    // Resolve aggregate-proxy driver regions before transaction observation,
    // writer counts, or driver tables can be mutated. The source Process keeps
    // its original proxy regions for artifacts and analysis.
    const auto outputs = expanded_driver_regions(process);
    // Allocate immutable scalar-source coverage before any observation or
    // writer-topology state changes. A failed preparation leaves registration
    // untouched and can retry the same dense process ID.
    using ScalarDriverRegions
        = std::shared_ptr<const std::vector<Process::DriverRegion>>;
    // Keyed by driver-group index; entries are appended in group order.
    std::vector<std::pair<std::size_t, ScalarDriverRegions>>
        scalar_driver_regions;
    const auto driver_group_end = [&](const std::size_t begin) {
        auto end = begin + 1U;
        while (end < outputs.size()
            && outputs[end].signal == outputs[begin].signal) {
            ++end;
        }
        return end;
    };
    std::size_t group_index = 0U;
    for (std::size_t begin = 0U, end = 0U; begin < outputs.size();
         begin = end, ++group_index) {
        end = driver_group_end(begin);
        const std::span<const Process::DriverRegion> regions {
            outputs.data() + begin, end - begin };
        const auto signal_id = outputs[begin].signal;
        const auto& signal = signals.at(signal_id);
        if (signal.resolution == ResolutionKind::std_logic
            && signal.value_kind == ValueKind::logic9 && !regions.empty()
            && std::ranges::none_of(regions, &Process::DriverRegion::whole)
            && std::ranges::none_of(regions,
                [&](const Process::DriverRegion& region) {
                    return region.offset == 0U
                        && region.width == signal.initial_value.width();
                })) {
            scalar_driver_regions.emplace_back(group_index,
                std::make_shared<std::vector<Process::DriverRegion>>(
                    regions.begin(), regions.end()));
        }
    }
    const bool has_switch_metadata = process.switch_source().has_value()
        || process.switch_target().has_value()
        || process.switch_control().has_value();
    const bool has_switch_region = process.switch_source_offset() != 0
        || process.switch_target_offset() != 0 || process.switch_width() != 0;
    const bool switch_connection = process.switch_bidirectional();
    if ((switch_connection
            && (!process.switch_source() || !process.switch_target()))
        || (has_switch_region && !has_switch_metadata)
        || (has_switch_metadata
            && (!process.switch_source() || !process.switch_target()
                || *process.switch_source() >= signals.size()
                || *process.switch_target() >= signals.size()
                || (process.switch_control()
                    && *process.switch_control() >= signals.size())))) {
        throw std::invalid_argument {
            "SimIR transmission connection has invalid endpoint metadata"
        };
    }
    if (has_switch_metadata) {
        const auto source_width = signals[*process.switch_source()]
                                      .initial_value.width();
        const auto target_width = signals[*process.switch_target()]
                                      .initial_value.width();
        const auto selected_width = process.switch_width();
        const bool invalid_selected_region = selected_width != 0
            && (process.switch_source_offset() > source_width
                || selected_width
                    > source_width - process.switch_source_offset()
                || process.switch_target_offset() > target_width
                || selected_width
                    > target_width - process.switch_target_offset());
        if (invalid_selected_region
            || (selected_width == 0
                && (process.switch_source_offset() != 0
                    || process.switch_target_offset() != 0
                    || (source_width != target_width
                        && source_width != 1 && target_width != 1)))
            || (process.switch_control()
                && signals[*process.switch_control()]
                        .initial_value.width()
                    != 1
                && signals[*process.switch_control()]
                        .initial_value.width()
                    != (selected_width == 0
                            ? std::max(source_width, target_width)
                            : selected_width))) {
            throw std::invalid_argument {
                "SimIR transmission connection has incompatible endpoint widths"
            };
        }
    }
    if (process.final() && process.initialize()) {
        throw std::invalid_argument(
            "a SimIR final process cannot initialize at time zero");
    }
    const bool package_static_initializer
        = process.startup_phase()
            == ProcessStartupPhase::package_static_initializer;
    if (process.startup_phase() != ProcessStartupPhase::ordinary
        && process.startup_phase() != ProcessStartupPhase::after_procedures
        && !package_static_initializer) {
        throw std::invalid_argument {
            "invalid SimIR process startup phase"
        };
    }
    if ((!package_static_initializer && process.startup_order() != 0U)
        || (package_static_initializer
            && (!process.initialize() || process.final()
                || process.scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog
                || !process.static_sensitivity().empty()
                || process.operations().empty()
                || process.driver_regions().empty()
                || process.observed() || process.reactive()
                || process.postponed() || process.program_owner()
                || process.switch_source() || process.switch_target()
                || process.switch_control()
                || process.switch_bidirectional()))) {
        throw std::invalid_argument {
            "invalid SimIR package static initializer process metadata"
        };
    }
    const auto& string_write_closure
        = process.startup_writable_string_object_ids();
    const auto& container_write_closure
        = process.startup_writable_container_object_ids();
    const auto package_owner_id = process.startup_package_owner_id();
    if (!package_static_initializer) {
        if (package_owner_id != 0U || !string_write_closure.empty()
            || !container_write_closure.empty()) {
            throw std::invalid_argument {
                "ordinary SimIR process carries package startup object permissions"
            };
        }
    } else {
        const auto canonical_ids = [](const auto& values,
                                      const std::size_t limit) {
            std::optional<std::uint32_t> previous;
            for (const auto id : values) {
                if (id >= limit || (previous && id <= *previous)) {
                    return false;
                }
                previous = id;
            }
            return true;
        };
        if (package_owner_id == 0U
            || !canonical_ids(string_write_closure, string_objects.size())
            || !canonical_ids(container_write_closure,
                container_objects.size())) {
            throw std::invalid_argument {
                "SimIR package startup object permissions are not canonical"
            };
        }
        std::vector<StringObjectId> operation_string_writes;
        std::vector<ContainerObjectId> operation_container_writes;
        for (const auto& operation : process.operations()) {
            if (const auto* string_write
                = operation_get_if<WriteStringObject>(&operation)) {
                operation_string_writes.push_back(string_write->object);
            } else if (const auto* container_write
                = operation_get_if<WriteContainerObject>(&operation)) {
                if (container_write->nonblocking
                    || container_write->transaction_signal) {
                    throw std::invalid_argument {
                        "SimIR package startup cannot defer a whole-container write"
                    };
                }
                operation_container_writes.push_back(container_write->object);
            } else if (operation_holds<WriteContainerObjectElement>(
                           operation)) {
                throw std::invalid_argument {
                    "SimIR package startup cannot use direct container-element writes"
                };
            }
        }
        const auto canonicalize_ids = [](auto& values) {
            std::ranges::sort(values);
            values.erase(std::unique(values.begin(), values.end()),
                values.end());
        };
        canonicalize_ids(operation_string_writes);
        canonicalize_ids(operation_container_writes);
        if (operation_string_writes != string_write_closure
            || operation_container_writes != container_write_closure) {
            throw std::invalid_argument {
                "SimIR package startup object permissions do not match process writes"
            };
        }
        for (const auto object : string_write_closure) {
            if (string_objects[object].package_owner_id != package_owner_id) {
                throw std::invalid_argument {
                    "SimIR package startup string permission has a foreign owner"
                };
            }
        }
        for (const auto object : container_write_closure) {
            const auto& target = container_objects[object];
            if (target.package_owner_id != package_owner_id
                || target.slice_alias
                || container_has_signal_alias(object)) {
                throw std::invalid_argument {
                    "SimIR package startup container permission is not direct owned storage"
                };
            }
        }
    }
    const auto scheduling_regions = static_cast<unsigned>(process.observed())
        + static_cast<unsigned>(process.reactive())
        + static_cast<unsigned>(process.postponed());
    if (scheduling_regions > 1) {
        throw std::invalid_argument(
            "a SimIR process cannot occupy multiple scheduling regions");
    }
    if (!register_value_kinds.empty()
        && register_value_kinds.size()
            != process.register_count()) {
        throw std::invalid_argument(
            "SimIR register value-kind count does not match register_count");
    }
    const auto register_signal_selection = [&](
        const SignalId signal,
        const std::optional<SignalSelection>& selection) {
        if (selection) {
            this->register_signal_selection(signal, *selection);
        }
    };
    for (std::size_t sensitivity_index = 0;
         sensitivity_index < process.static_sensitivity().size();
         ++sensitivity_index) {
        const auto signal = process.static_sensitivity()[sensitivity_index];
        if (signal.signal >= signals.size()) {
            throw std::invalid_argument("process sensitivity references invalid signal");
        }
        const auto signal_width = signals[signal.signal].initial_value.width();
        if ((signal.width == 0U && signal.offset != 0U)
            || (signal.width != 0U
                && (signal.edge != EdgeKind::any
                    || signals[signal.signal].event_variable
                    || signal.offset >= signal_width
                    || signal.width > signal_width - signal.offset))) {
            throw std::invalid_argument("process sensitivity has an invalid range");
        }
        if (signal.edge != EdgeKind::any
            && signal.edge != EdgeKind::transaction
            && signal_width == 0U) {
            throw std::invalid_argument(
                "edge sensitivity requires a non-empty signal");
        }
        if (signal.edge == EdgeKind::transaction) {
            observe_signal_transaction(signal.signal);
        }
    }
    // Instances of one body share a kinds summary; a body without the
    // operations below skips each per-instruction scan.
    const auto* const kinds = body_kinds(process.operations());
    const auto has_kind = [&]<typename... Kinds>() {
        return kinds == nullptr
            || (kinds->kinds.test(static_kernel_detail::operation_kind_of<Kinds>)
                || ...);
    };
    // The operations whose registrations the process carries (see
    // add_process_program_impl's `registration`).
    const auto& registered = registration != nullptr ? *registration : process;
    const auto* const registered_kinds = registration != nullptr
        ? body_kinds(registered.operations()) : kinds;
    const auto has_registered_kind = [&]<typename... Kinds>() {
        return registered_kinds == nullptr
            || (registered_kinds->kinds.test(
                    static_kernel_detail::operation_kind_of<Kinds>)
                || ...);
    };
    const bool scan_registration = registered_kinds == nullptr
        || registered_kinds->transaction_wait
        || has_registered_kind.template operator()<MailboxCreate, MailboxPut, MailboxGet,
            MailboxNum, DynamicEvent, SignalActive, SignalLastActive,
            SignalEvent, SignalLastValue, SignalLastEvent, SignalDriving,
            SignalDrivingValue>();
    // Resolve only the signal operands here. Expanding a WaitOn copies its
    // edge vectors during registration, while OperationList::signal is
    // allocation-free and preserves per-instance signal identities.
    for (std::size_t instruction = 0U;
         scan_registration && instruction < registered.operations().size();
         ++instruction) {
        const auto& operation = registered.operations()[instruction];
        const auto valid_mailbox_kind = [](const MailboxElementKind kind) {
            return kind == MailboxElementKind::packed
                || kind == MailboxElementKind::string;
        };
        const auto valid_mailbox_shape = [&](
            const MailboxElementKind kind,
            const std::uint32_t width) {
            return valid_mailbox_kind(kind)
                && (kind == MailboxElementKind::packed
                    ? width != 0U
                    : width == 0U);
        };
        if (const auto* create = operation_get_if<MailboxCreate>(&operation)) {
            if (!valid_mailbox_shape(
                    create->element_kind, create->element_width)
                || create->destination >= registered.register_count()
                || create->capacity >= registered.register_count()) {
                throw std::invalid_argument {
                    "SimIR MailboxCreate has invalid typed registers or shape"
                };
            }
        } else if (const auto* put = operation_get_if<MailboxPut>(&operation)) {
            if (!valid_mailbox_shape(put->element_kind, put->element_width)
                || put->receiver >= registered.register_count()
                || (put->result
                    && *put->result >= registered.register_count())
                || (put->element_kind == MailboxElementKind::packed
                    ? put->source >= registered.register_count()
                    : put->string_source >= registered.string_register_count())) {
                throw std::invalid_argument {
                    "SimIR MailboxPut has invalid typed registers or shape"
                };
            }
        } else if (const auto* get = operation_get_if<MailboxGet>(&operation)) {
            if (!valid_mailbox_shape(get->element_kind, get->element_width)
                || get->receiver >= registered.register_count()
                || (get->result
                    && *get->result >= registered.register_count())
                || (get->element_kind == MailboxElementKind::packed
                    ? get->destination >= registered.register_count()
                    : get->string_destination
                        >= registered.string_register_count())) {
                throw std::invalid_argument {
                    "SimIR MailboxGet has invalid typed registers or shape"
                };
            }
        } else if (const auto* num = operation_get_if<MailboxNum>(&operation)) {
            if (num->receiver >= registered.register_count()
                || num->destination >= registered.register_count()) {
                throw std::invalid_argument {
                    "SimIR MailboxNum has an invalid register ID"
                };
            }
        } else if (const auto* event = operation_get_if<DynamicEvent>(&operation)) {
            if (event->receiver >= registered.register_count()
                || event->destination >= registered.register_count()) {
                throw std::invalid_argument {
                    "SimIR DynamicEvent has an invalid register ID"
                };
            }
        }
        if (const auto* active = operation_get_if<SignalActive>(&operation)) {
            const auto signal = registered.operations().signal(active->signal);
            observe_signal_transaction(signal);
            register_signal_selection(signal, active->selection);
        } else if (const auto* last_active
                   = operation_get_if<SignalLastActive>(&operation)) {
            const auto signal
                = registered.operations().signal(last_active->signal);
            observe_signal_transaction(signal);
            register_signal_selection(signal, last_active->selection);
        } else if (const auto* event
                   = operation_get_if<SignalEvent>(&operation)) {
            register_signal_selection(
                registered.operations().signal(event->signal),
                event->selection);
        } else if (const auto* last_value
                   = operation_get_if<SignalLastValue>(&operation)) {
            register_signal_selection(
                registered.operations().signal(last_value->signal),
                last_value->selection);
        } else if (const auto* last_event
                   = operation_get_if<SignalLastEvent>(&operation)) {
            register_signal_selection(
                registered.operations().signal(last_event->signal),
                last_event->selection);
        } else if (const auto* driving
                   = operation_get_if<SignalDriving>(&operation)) {
            register_signal_selection(
                registered.operations().signal(driving->signal),
                driving->selection);
        } else if (const auto* driving_value
                   = operation_get_if<SignalDrivingValue>(&operation)) {
            register_signal_selection(
                registered.operations().signal(driving_value->signal),
                driving_value->selection);
        }
        const auto* wait = operation_get_if<WaitOn>(&operation);
        if (wait == nullptr) {
            continue;
        }
        for (std::size_t index = 0; index < wait->edges.size(); ++index) {
            if (wait->edges[index] == EdgeKind::transaction) {
                observe_signal_transaction(registered.operations().signal(
                    wait->signals.at(index)));
            }
        }
    }
    // Debug-local and container-register metadata come from the shared
    // template, so one validation covers every instance of it.
    const bool template_validated = common_program
        && validated_program_templates.contains(common_program);
    if (!template_validated) {
        std::set<std::string> local_names;
        for (const auto& local : process.debug_locals()) {
            if (local.name.empty()
                || local.register_id >= process.register_count()) {
                throw std::invalid_argument { "invalid SimIR debug-local metadata" };
            }
            if (local.systemverilog_scalar != SystemVerilogScalarKind::None) {
                const auto expected_width = local.systemverilog_scalar
                        == SystemVerilogScalarKind::ShortReal
                    ? 32U
                    : local.systemverilog_scalar == SystemVerilogScalarKind::Real
                        || local.systemverilog_scalar
                            == SystemVerilogScalarKind::Realtime
                        || local.systemverilog_scalar
                            == SystemVerilogScalarKind::Time
                        || local.systemverilog_scalar
                            == SystemVerilogScalarKind::Chandle
                    ? 64U
                    : 0U;
                if (local.width != expected_width
                    || local.value_kind != ValueKind::logic4) {
                    throw std::invalid_argument {
                        "invalid SimIR scalar debug-local metadata"
                    };
                }
            }
            if (!local_names.insert(local.name).second) {
                throw std::invalid_argument { "duplicate SimIR debug-local name" };
            }
        }
        std::set<std::string> string_local_names;
        for (const auto& local : process.debug_string_locals()) {
            if (local.name.empty()
                || local.register_id >= process.string_register_count()) {
                throw std::invalid_argument {
                    "invalid SimIR string debug-local metadata"
                };
            }
            if (!string_local_names.insert(local.name).second
                || local_names.contains(local.name)) {
                throw std::invalid_argument {
                    "duplicate SimIR debug-local name"
                };
            }
        }
        if (container_register_types.size()
            != process.container_register_count()) {
            throw std::invalid_argument {
                "SimIR container register type count does not match register count"
            };
        }
        std::set<std::string> container_local_names;
        for (const auto& local : process.debug_container_locals()) {
            if (local.name.empty()
                || local.register_id >= process.container_register_count()
                || local.type != container_register_types.at(local.register_id)) {
                throw std::invalid_argument {
                    "invalid SimIR container debug-local metadata"
                };
            }
            if (!container_local_names.insert(local.name).second
                || local_names.contains(local.name)
                || string_local_names.contains(local.name)) {
                throw std::invalid_argument {
                    "duplicate SimIR debug-local name"
                };
            }
        }
        if (common_program) {
            validated_program_templates.insert(common_program);
        }
    }
    if (!common_program) {
        common_program = process_program_templates.intern(process);
    }
    // A delayed continuous assignment or gate drives X until its first
    // delayed value matures (see register_driver).
    bool delayed_writer = false;
    if (registered_kinds != nullptr) {
        delayed_writer = has_registered_kind.template
            operator()<WriteInertial, WriteInertialSlice>();
    } else {
        for (std::size_t instruction = 0;
             instruction < registered.operations().size() && !delayed_writer;
             ++instruction) {
            const auto& operation = registered.operations()[instruction];
            delayed_writer = operation_holds<WriteInertial>(operation)
                || operation_holds<WriteInertialSlice>(operation);
        }
    }
    std::size_t scalar_cursor = 0U;
    group_index = 0U;
    for (std::size_t begin = 0U, end = 0U; begin < outputs.size();
         begin = end, ++group_index) {
        end = driver_group_end(begin);
        const auto signal = outputs[begin].signal;
        const std::span<const Process::DriverRegion> regions {
            outputs.data() + begin, end - begin };
        if (!switch_connection && !package_static_initializer) {
            auto& writer_count = signal_writer_counts[signal];
            if (writer_count != std::numeric_limits<std::uint32_t>::max()) {
                ++writer_count;
            }
            stable_single_writer_processes[signal]
                = writer_count == 1U
                    && !signal_transaction_observed[signal]
                    && !has_container_signal_alias(signal)
                ? id
                : std::numeric_limits<ProcessId>::max();
            ++signal_writer_revision;
            if (signal_writer_revision == 0U) {
                throw std::overflow_error {
                    "SimIR signal-writer topology revision overflow"
                };
            }
            const bool has_scalar_regions
                = scalar_cursor < scalar_driver_regions.size()
                && scalar_driver_regions[scalar_cursor].first == group_index;
            register_driver(id, signal, regions, process.drive_strength(),
                has_scalar_regions
                    ? scalar_driver_regions[scalar_cursor++].second
                    : ScalarDriverRegions { },
                delayed_writer);
        }
    }

    if (next_process_generation == 0) {
        throw std::overflow_error { "SimIR process generation overflow" };
    }
    const auto process_generation = next_process_generation++;
    if (process.program_owner()) {
        program_owners.insert(*process.program_owner());
    }
    if (kinds == nullptr || kinds->sampled_read) {
        record_sampled_dependencies(process);
    }
    if (has_switch_metadata && !switch_connection) {
        if (switch_source_readers.size() < signals.size()) {
            switch_source_readers.resize(signals.size());
        }
        switch_source_readers[*process.switch_source()].push_back(id);
    }
    if (switch_connection) {
        switch_endpoint_adjacency.resize(signals.size());
        switch_control_adjacency.resize(signals.size());
        const auto connection_index = switch_connections.size();
        switch_connections.push_back(Impl::SwitchConnection {
            .source = *process.switch_source(),
            .target = *process.switch_target(),
            .control = process.switch_control(),
            .source_offset = process.switch_source_offset(),
            .target_offset = process.switch_target_offset(),
            .width = process.switch_width(),
            .active_high = process.switch_active_high(),
            .resistive = process.switch_resistive()
        });
        switch_endpoint_adjacency[*process.switch_source()]
            .push_back(connection_index);
        if (*process.switch_target() != *process.switch_source()) {
            switch_endpoint_adjacency[*process.switch_target()]
                .push_back(connection_index);
        }
        if (process.switch_control()) {
            switch_control_adjacency[*process.switch_control()]
                .push_back(connection_index);
        }
        invalidate_switch_components();
        has_bidirectional_switches = true;
    }
    const bool initialize = process.initialize();
    const bool observed = process.observed();
    const bool reactive = process.reactive();
    const bool postponed = process.postponed();
    // The program is installed straight into a bare storage record; a full
    // ProcessColdState would only be constructed to be moved from.
    ProcessProgramStorage program_storage;
    program_storage.program_template = std::move(common_program);
    if (owned_instance != nullptr) {
        program_storage.instance_program
            = std::move(*owned_instance);
    } else {
        program_storage.instance_program = ProcessInstanceProgram {
            owned_process != nullptr
                ? std::move(*owned_process)
                : process.materialize()
        };
    }
    const ProcessProgramView program {
        *program_storage.program_template,
        program_storage.instance_program
    };
    std::optional<Impl::ConstantDriverStartupEntry> compact_shape;
    if (executable
        && program.startup_phase() != ProcessStartupPhase::package_static_initializer) {
        compact_shape
            = recognize_constant_driver_startup(id, program);
    }
    if (compact_shape
        && program.scheduling_domain()
            == ProcessSchedulingDomain::systemverilog
        && compact_shape->update_domain
            == SignalUpdateDomain::systemverilog_active
        && !compact_shape->projected) {
        const auto& operations = program.operations();
        const bool copied_constant = program.register_count() == 2U;
        const auto has_statement
            = operations.size() == (copied_constant ? 6U : 5U);
        const auto entry_operation = operations.expanded(0U);
        const auto entry_point = operation_get_if<DebugPoint>(
            &entry_operation);
        const auto statement_index = has_statement ? 1U : 0U;
        const auto statement_operation = has_statement
            ? std::optional<Operation> {
                  operations.expanded(statement_index)
              }
            : std::nullopt;
        const auto load_operation = operations.expanded(
            1U + statement_index);
        const auto* const load
            = operation_get_if<LoadConstant>(&load_operation);
        if (entry_point == nullptr || load == nullptr) {
            throw std::logic_error {
                "recognized startup write lost its entry or constant"
            };
        }

        auto bank = std::make_unique<ProcessStartupWriteBank>();
        bank->value = load->value;
        bank->entry_point = *entry_point;
        if (statement_operation) {
            const auto* const point
                = operation_get_if<DebugPoint>(&*statement_operation);
            if (point == nullptr) {
                throw std::logic_error {
                    "recognized startup write lost its statement point"
                };
            }
            bank->statement_point = *point;
        }
        if (copied_constant) {
            const auto copy_operation
                = operations.expanded(2U + statement_index);
            const auto* const copy
                = operation_get_if<CopyRegister>(&copy_operation);
            if (copy == nullptr) {
                throw std::logic_error {
                    "recognized startup write lost its constant copy"
                };
            }
            bank->constant_copy = *copy;
        }
        bank->signal = compact_shape->signal;
        bank->offset = compact_shape->offset;
        bank->slice = compact_shape->slice;
        bank->update_domain = compact_shape->update_domain;
        bank->operation_count = operations.size();
        const auto body_identity = operations.body_identity();
        auto body_cache = startup_write_body_caches.find(body_identity);
        if (body_cache == startup_write_body_caches.end()) {
            auto cache = std::make_shared<ProcessStartupWriteBodyCache>(
                operations);
            body_cache = startup_write_body_caches.emplace(
                body_identity, std::move(cache)).first;
        }
        bank->body_cache = body_cache->second;
        compact_shape->startup_write_bank = std::move(bank);
        program_storage.instance_program.operations
            = OperationList { };
    }
    const auto execution_phase = process_execution_phase(
        observed, reactive, postponed);
    if (compact_shape) {
        auto compact = std::move(*compact_shape);
        compact.generation = process_generation;
        compact.execution_phase = execution_phase;
        compact.status = initialize
            ? ProcessStatus::running
            : ProcessStatus::waiting;
        compact.static_trigger_mask = initialize
            ? Process::full_static_trigger_mask
            : 0U;
        compact.waiting_on_static = !initialize;
        compact.random_state = Impl::initial_random_state(
            root_seed, id);
        compact.program_storage = std::make_unique<ProcessProgramStorage>(
            std::move(program_storage));
        processes.push_compact(std::move(compact));
    } else {
        Impl::ProcessState state;
        state.generation = process_generation;
        state.id = id;
        state.has_callable_frame_push = kinds != nullptr
            ? has_kind.template operator()<CallableFramePush>()
            : std::ranges::any_of(std::as_const(program.operations()),
                  [](const Operation& operation) {
                      return operation_holds<CallableFramePush>(operation);
                  });
        state.cold().random_state = Impl::initial_random_state(
            root_seed, id);
        state.cold().design_process = id;
        state.execution_phase = execution_phase;
        state.static_trigger_mask = initialize
            ? Process::full_static_trigger_mask
            : 0U;
        state.waiting_on_static = !initialize;
        state.status = initialize
            ? ProcessStatus::running
            : ProcessStatus::waiting;
        state.cold().local_program_storage
            = std::move(program_storage);
        processes.push_back(std::move(state));
    }
    static_fanout_dirty = true;
    if (executable
        && !processes.program_view(id).static_sensitivity().empty()) {
        register_static_sensitivity_cohort(id);
    }
    return id;
}

std::uint32_t Interpreter::add_module_path(ModulePath path)
{
    if (impl_->started) {
        throw std::logic_error("cannot add a SimIR module path after start");
    }
    const auto id = static_cast<std::uint32_t>(impl_->module_paths.size());
    if (static_cast<std::size_t>(id) != impl_->module_paths.size()) {
        throw std::length_error("too many SimIR module paths");
    }
    if (path.id != id) {
        throw std::invalid_argument(
            "SimIR module-path IDs must be dense and ordered");
    }
    if (path.identity.empty()
        || std::ranges::any_of(
            impl_->module_paths,
            [&](const ModulePath& candidate) {
                return candidate.identity == path.identity;
            })
        || std::ranges::any_of(
            impl_->module_timing_checks,
            [&](const ModuleTimingCheck& candidate) {
                return candidate.identity == path.identity;
            })) {
        throw std::invalid_argument(
            "SimIR module-path identities must be nonempty and unique");
    }
    const auto valid_terminal = [&](const ModulePathTerminal& terminal) {
        if (terminal.signal >= impl_->signals.size() || terminal.width == 0) {
            return false;
        }
        const auto width = impl_->signals[terminal.signal].initial_value.width();
        return terminal.offset <= width
            && terminal.width <= width - terminal.offset;
    };
    if (path.sources.empty() || path.destinations.empty()
        || !std::ranges::all_of(path.sources, valid_terminal)
        || !std::ranges::all_of(path.destinations, valid_terminal)) {
        throw std::invalid_argument("invalid SimIR module-path terminals");
    }
    const bool valid_delay_count = path.delays.size() == 1
        || path.delays.size() == 2 || path.delays.size() == 3
        || path.delays.size() == 6 || path.delays.size() == 12;
    if (!valid_delay_count) {
        throw std::invalid_argument("invalid SimIR module-path delay count");
    }
    const auto valid_optional_delay_table = [](const auto& values) {
        return values.empty() || values.size() == 1U || values.size() == 2U
            || values.size() == 3U || values.size() == 6U
            || values.size() == 12U;
    };
    if (!valid_optional_delay_table(path.pulse_reject_delays)
        || !valid_optional_delay_table(path.pulse_error_delays)
        || !valid_optional_delay_table(path.retain_delays)
        || path.pulse_reject_delays.empty()
            != path.pulse_error_delays.empty()
        || path.pulse_reject_delays.size()
            != path.pulse_error_delays.size()
        || (!path.pulse_reject_delays.empty()
            && !std::ranges::equal(path.pulse_reject_delays,
                path.pulse_error_delays,
                [](const auto reject, const auto error) {
                    return reject <= error;
                }))) {
        throw std::invalid_argument("invalid SimIR module-path pulse tables");
    }
    if (!path.full
        && (path.sources.size() != path.destinations.size()
            || !std::ranges::equal(
                path.sources, path.destinations,
                [](const auto& source, const auto& destination) {
                    return source.width == destination.width;
                }))) {
        throw std::invalid_argument(
            "parallel SimIR module-path terminal widths do not match");
    }
    if (!std::ranges::all_of(
            path.drivers,
            [&](const ProcessId driver) {
                return driver < impl_->processes.size();
            })
        || !std::ranges::is_sorted(path.drivers)
        || std::ranges::adjacent_find(path.drivers)
            != path.drivers.end()) {
        throw std::invalid_argument("invalid SimIR module-path driver set");
    }
    if (path.source_edge > ModulePathEdge::edge
        || path.polarity > ModulePathPolarity::negative
        || path.pulse_style > ModulePathPulseStyle::ondetect
        || path.selection_group > path.id || (path.conditional && path.ifnone)
        || path.conditional == path.condition.empty()
        || path.pulse_reject_limit.has_value()
            != path.pulse_error_limit.has_value()
        || (path.pulse_reject_limit
            && *path.pulse_reject_limit > *path.pulse_error_limit)) {
        throw std::invalid_argument("invalid SimIR module-path enumeration");
    }
    validate_module_path_expression(path.condition, impl_->signals);
    validate_module_path_expression(path.data_source, impl_->signals);
    // The drivers of a path-delayed net start X on its unknown bits (see
    // add_signal).
    for (const auto driver : path.drivers) {
        for (const auto& destination : path.destinations) {
            const auto delayed
                = impl_->delayed_net_initial_values.find(destination.signal);
            if (delayed == impl_->delayed_net_initial_values.end()) {
                continue;
            }
            auto* record
                = impl_->driver_values.at(destination.signal).find(driver);
            if (record == nullptr) {
                continue;
            }
            for (auto bit = static_cast<std::size_t>(destination.offset);
                bit < static_cast<std::size_t>(destination.offset)
                        + destination.width
                && bit < record->value.width();
                ++bit) {
                if (delayed->second.get(bit) == Logic4::x) {
                    record->value.set(bit, Logic4::x);
                }
            }
            const auto resolved
                = impl_->resolved_driver_value(destination.signal);
            impl_->driven_values[destination.signal] = resolved;
            impl_->signals[destination.signal].initial_value = resolved;
        }
    }
    impl_->module_paths.push_back(std::move(path));
    return id;
}

std::uint32_t Interpreter::add_module_timing_check(
    ModuleTimingCheck check)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR module timing check after start"
        };
    }
    const auto id = static_cast<std::uint32_t>(
        impl_->module_timing_checks.size());
    if (static_cast<std::size_t>(id)
        != impl_->module_timing_checks.size()) {
        throw std::length_error { "too many SimIR module timing checks" };
    }
    const auto valid_event = [&](const ModuleTimingEvent& event) {
        return event.terminal.signal < impl_->signals.size()
            && event.terminal.width == 1
            && event.terminal.offset
            < impl_->signals[event.terminal.signal].initial_value.width()
            && event.edge <= ModulePathEdge::edge;
    };
    const bool identity_valid = !check.identity.empty()
        && std::ranges::none_of(
            impl_->module_timing_checks,
            [&](const ModuleTimingCheck& candidate) {
                return candidate.identity == check.identity;
            })
        && std::ranges::none_of(
            impl_->module_paths,
            [&](const ModulePath& candidate) {
                return candidate.identity == check.identity;
            });
    const auto valid_delayed_terminal = [&](const ModulePathTerminal& terminal) {
        return terminal.signal < impl_->signals.size()
            && terminal.width == 1
            && terminal.offset
            < impl_->signals[terminal.signal].initial_value.width();
    };
    const bool compound = check.kind == ModuleTimingCheckKind::setuphold
        || check.kind == ModuleTimingCheckKind::recrem
        || check.kind == ModuleTimingCheckKind::fullskew
        || check.kind == ModuleTimingCheckKind::nochange;
    const auto expected_limits = compound ? 2U : 1U;
    bool valid_compound_sum = !compound;
    if (check.limits.size() == 2) {
        const bool overflow = (check.limits[1] > 0
                                  && check.limits[0]
                                      > std::numeric_limits<std::int64_t>::max()
                                          - check.limits[1])
            || (check.limits[1] < 0
                && check.limits[0]
                    < std::numeric_limits<std::int64_t>::min()
                        - check.limits[1]);
        valid_compound_sum = !overflow
            && check.limits[0] + check.limits[1] > 0;
    }
    const bool controlled_reference = check.reference.edge != ModulePathEdge::none;
    if (check.id != id || !identity_valid
        || check.kind > ModuleTimingCheckKind::nochange
        || !valid_event(check.reference)
        || ((check.kind == ModuleTimingCheckKind::period
                || check.kind == ModuleTimingCheckKind::width)
            && !controlled_reference)
        || (check.kind != ModuleTimingCheckKind::period
            && check.kind != ModuleTimingCheckKind::width
            && (!check.data || !valid_event(*check.data)))
        || check.limits.size() != expected_limits
        || ((check.kind != ModuleTimingCheckKind::setuphold
                && check.kind != ModuleTimingCheckKind::recrem
                && check.kind != ModuleTimingCheckKind::nochange)
            && std::ranges::any_of(
                check.limits,
                [](const std::int64_t limit) { return limit < 0; }))
        || ((check.kind == ModuleTimingCheckKind::setuphold
                || check.kind == ModuleTimingCheckKind::recrem)
            && !valid_compound_sum)
        || (check.kind == ModuleTimingCheckKind::nochange
            && check.limits.size() == 2
            && check.limits[0] > check.limits[1])
        || (check.threshold.has_value()
            && check.kind != ModuleTimingCheckKind::width)
        || (check.notifier
            && (*check.notifier >= impl_->signals.size()
                || impl_->signals[*check.notifier].initial_value.width() != 1))
        || (check.delayed_reference
            && !valid_delayed_terminal(*check.delayed_reference))
        || (check.delayed_data
            && !valid_delayed_terminal(*check.delayed_data))) {
        throw std::invalid_argument { "invalid SimIR module timing check" };
    }
    validate_module_path_expression(check.reference.condition, impl_->signals);
    if (check.data) {
        validate_module_path_expression(check.data->condition, impl_->signals);
    }
    validate_module_path_expression(check.timestamp_condition, impl_->signals);
    validate_module_path_expression(check.timecheck_condition, impl_->signals);
    impl_->module_timing_checks.push_back(std::move(check));
    impl_->module_timing_check_states.emplace_back();
    return id;
}

void Interpreter::reannotate_module_timing(
    const std::span<const ModulePath> paths,
    const std::span<const ModuleTimingCheck> checks)
{
    if (impl_->started && !impl_->scheduler.at_safe_point()) {
        throw std::logic_error {
            "SimIR timing reannotation requires a scheduler safe point"
        };
    }
    if (paths.size() != impl_->module_paths.size()
        || checks.size() != impl_->module_timing_checks.size()) {
        throw std::invalid_argument {
            "SimIR timing reannotation changed timing topology"
        };
    }
    const auto valid_delay_table = [](const auto& values) {
        return values.size() == 1U || values.size() == 2U
            || values.size() == 3U || values.size() == 6U
            || values.size() == 12U;
    };
    const auto valid_optional_delay_table = [&](const auto& values) {
        return values.empty() || valid_delay_table(values);
    };
    const auto same_expression = [](const ModulePathExpression& left,
                                     const ModulePathExpression& right) {
        return left.root == right.root && left.nodes.size() == right.nodes.size()
            && std::ranges::equal(
                left.nodes,
                right.nodes,
                [](const ModulePathExpressionNode& left_node,
                    const ModulePathExpressionNode& right_node) {
                    return left_node.operation == right_node.operation
                        && left_node.operands == right_node.operands
                        && left_node.constant == right_node.constant
                        && left_node.terminal == right_node.terminal
                        && left_node.binary == right_node.binary
                        && left_node.logical == right_node.logical
                        && left_node.shift == right_node.shift
                        && left_node.reduction == right_node.reduction
                        && left_node.width == right_node.width
                        && left_node.is_signed == right_node.is_signed;
                });
    };
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& before = impl_->module_paths[index];
        const auto& after = paths[index];
        if (after.id != before.id || after.identity != before.identity
            || after.sources != before.sources
            || after.destinations != before.destinations
            || after.drivers != before.drivers || after.full != before.full
            || after.conditional != before.conditional
            || after.ifnone != before.ifnone
            || after.selection_group != before.selection_group
            || after.source_edge != before.source_edge
            || after.polarity != before.polarity
            || after.pulse_style != before.pulse_style
            || after.show_cancelled != before.show_cancelled
            || !same_expression(after.condition, before.condition)
            || !same_expression(after.data_source, before.data_source)
            || !valid_delay_table(after.delays)
            || !valid_optional_delay_table(after.pulse_reject_delays)
            || !valid_optional_delay_table(after.pulse_error_delays)
            || !valid_optional_delay_table(after.retain_delays)
            || after.pulse_reject_delays.empty()
                != after.pulse_error_delays.empty()
            || after.pulse_reject_delays.size()
                != after.pulse_error_delays.size()
            || (!after.pulse_reject_delays.empty()
                && !std::ranges::equal(
                    after.pulse_reject_delays,
                    after.pulse_error_delays,
                    [](const auto reject, const auto error) {
                        return reject <= error;
                    }))
            || after.pulse_reject_limit.has_value()
                != after.pulse_error_limit.has_value()
            || (after.pulse_reject_limit
                && *after.pulse_reject_limit > *after.pulse_error_limit)) {
            throw std::invalid_argument {
                "SimIR timing reannotation changed path topology"
            };
        }
    }
    for (std::size_t index = 0; index < checks.size(); ++index) {
        const auto& before = impl_->module_timing_checks[index];
        const auto& after = checks[index];
        if (after.id != before.id || after.identity != before.identity
            || after.kind != before.kind
            || after.reference.terminal != before.reference.terminal
            || after.reference.edge != before.reference.edge
            || after.reference.edge_descriptors
                != before.reference.edge_descriptors
            || !same_expression(
                after.reference.condition, before.reference.condition)
            || after.data.has_value() != before.data.has_value()
            || (after.data
                && (after.data->terminal != before.data->terminal
                    || after.data->edge != before.data->edge
                    || after.data->edge_descriptors
                        != before.data->edge_descriptors
                    || !same_expression(
                        after.data->condition, before.data->condition)))
            || after.notifier != before.notifier
            || after.delayed_reference != before.delayed_reference
            || after.delayed_data != before.delayed_data
            || after.event_based != before.event_based
            || after.remain_active != before.remain_active
            || !same_expression(
                after.timestamp_condition, before.timestamp_condition)
            || !same_expression(
                after.timecheck_condition, before.timecheck_condition)
            || after.limits.size() != before.limits.size()) {
            throw std::invalid_argument {
                "SimIR timing reannotation changed timing-check topology"
            };
        }
        const bool compound = after.kind == ModuleTimingCheckKind::setuphold
            || after.kind == ModuleTimingCheckKind::recrem
            || after.kind == ModuleTimingCheckKind::fullskew
            || after.kind == ModuleTimingCheckKind::nochange;
        bool valid_compound_sum = !compound;
        if (after.limits.size() == 2U) {
            const bool overflow = (after.limits[1] > 0
                                      && after.limits[0]
                                          > std::numeric_limits<
                                                std::int64_t>::max()
                                              - after.limits[1])
                || (after.limits[1] < 0
                    && after.limits[0]
                        < std::numeric_limits<std::int64_t>::min()
                            - after.limits[1]);
            valid_compound_sum = !overflow
                && after.limits[0] + after.limits[1] > 0;
        }
        if (((after.kind != ModuleTimingCheckKind::setuphold
                 && after.kind != ModuleTimingCheckKind::recrem
                 && after.kind != ModuleTimingCheckKind::nochange)
                && std::ranges::any_of(
                    after.limits,
                    [](const std::int64_t limit) { return limit < 0; }))
            || ((after.kind == ModuleTimingCheckKind::setuphold
                    || after.kind == ModuleTimingCheckKind::recrem)
                && !valid_compound_sum)
            || (after.kind == ModuleTimingCheckKind::nochange
                && after.limits[0] > after.limits[1])
            || (after.threshold.has_value()
                && after.kind != ModuleTimingCheckKind::width)) {
            throw std::invalid_argument {
                "SimIR timing reannotation has invalid timing-check values"
            };
        }
    }
    std::vector<ModulePath> replacement_paths(paths.begin(), paths.end());
    std::vector<ModuleTimingCheck> replacement_checks(
        checks.begin(), checks.end());
    impl_->module_paths.swap(replacement_paths);
    impl_->module_timing_checks.swap(replacement_checks);
}

std::vector<SimulationTick> Interpreter::module_path_delays(
    const std::uint32_t path) const
{
    if (path >= impl_->module_paths.size()) {
        throw std::out_of_range("SimIR module path does not exist");
    }
    return impl_->module_paths[path].delays;
}

bool Interpreter::set_module_path_delays(
    const std::uint32_t path, const std::span<const SimulationTick> delays)
{
    if (path >= impl_->module_paths.size()) {
        throw std::out_of_range("SimIR module path does not exist");
    }
    if (impl_->started && !impl_->scheduler.at_safe_point()) {
        return false;
    }
    // The replacement keeps the topology, so the reannotation validation
    // checks the new table's length and the pulse limits against it.
    std::vector<ModulePath> replacement = impl_->module_paths;
    replacement[path].delays.assign(delays.begin(), delays.end());
    reannotate_module_timing(replacement, impl_->module_timing_checks);
    return true;
}

void Interpreter::reannotate_vital_timing(
    const std::span<const VitalTimingReannotation> annotations,
    const bool reset_timing_state)
{
    if (impl_->started && !impl_->scheduler.at_safe_point()) {
        throw std::logic_error {
            "SimIR VITAL reannotation requires a scheduler safe point"
        };
    }
    struct Replacement {
        struct Definition {
            InstructionIndex instruction { };
            RegisterId target { };
            SimulationTick value { };
        };

        Impl::ProcessState* process { };
        InstructionIndex instruction { };
        bool timing_check { };
        std::array<SimulationTick, 6> values { };
        std::size_t value_count { };
        std::vector<Definition> definitions;
    };
    std::vector<Replacement> replacements;
    replacements.reserve(annotations.size());
    std::set<std::pair<ProcessId, InstructionIndex>> calls;
    std::map<std::pair<ProcessId, InstructionIndex>,
        std::pair<RegisterId, SimulationTick>>
        definitions;
    for (const auto& annotation : annotations) {
        if (annotation.process >= impl_->processes.size()
            || impl_->processes.is_compact_constant(annotation.process)) {
            throw std::invalid_argument {
                "SimIR VITAL reannotation references a stale call"
            };
        }
        const auto program
            = impl_->processes.program_view(annotation.process);
        if (program.id() != annotation.process
            || annotation.instruction >= program.operations().size()) {
            throw std::invalid_argument {
                "SimIR VITAL reannotation references a stale call"
            };
        }
        if (!calls.emplace(annotation.process, annotation.instruction).second) {
            throw std::invalid_argument {
                "SimIR VITAL reannotation references a call more than once"
            };
        }
        auto& process = impl_->processes[annotation.process];
        const auto& process_operations
            = std::as_const(process.program().operations());
        const auto& operation
            = process_operations[annotation.instruction];
        if (annotation.timing_check) {
            if (annotation.value_count != 4U
                || operation_get_if<VitalTimingCheck>(&operation) == nullptr) {
                throw std::invalid_argument {
                    "SimIR VITAL reannotation changed timing-check topology"
                };
            }
        } else {
            const auto* delay = operation_get_if<VitalDelay>(&operation);
            const auto expected = delay == nullptr
                ? 0U
                : delay->shape == VitalDelayShape::single
                ? 1U
                : delay->shape == VitalDelayShape::delay01 ? 2U
                                                           : 6U;
            if (delay == nullptr || annotation.value_count != expected) {
                throw std::invalid_argument {
                    "SimIR VITAL reannotation changed delay topology"
                };
            }
        }
        Replacement replacement { &process, annotation.instruction,
            annotation.timing_check, annotation.values,
            annotation.value_count, { } };
        if (!annotation.timing_check) {
            const auto* delay = operation_get_if<VitalDelay>(&operation);
            std::unordered_map<RegisterId, InstructionIndex> retained;
            for (InstructionIndex index = 0; index < annotation.instruction;
                ++index) {
                if (const auto* load = operation_get_if<LoadConstant>(
                        &process_operations[index])) {
                    retained[load->destination] = index;
                } else if (const auto* extract = operation_get_if<Extract>(
                               &process_operations[index])) {
                    retained[extract->destination] = index;
                }
            }
            const auto plan = [&](const RegisterId target,
                                  const SimulationTick value) {
                const auto found = retained.find(target);
                if (found == retained.end()) {
                    throw std::invalid_argument {
                        "SimIR VITAL reannotation lost a static delay definition"
                    };
                }
                const auto key
                    = std::pair { annotation.process, found->second };
                const auto [planned, inserted]
                    = definitions.emplace(key, std::pair { target, value });
                if (!inserted
                    && (planned->second.first != target
                        || planned->second.second != value)) {
                    throw std::invalid_argument {
                        "SimIR VITAL reannotation has conflicting static delay values"
                    };
                }
                if (inserted) {
                    replacement.definitions.push_back(
                        { found->second, target, value });
                }
            };
            for (std::size_t index = 0; index < annotation.value_count;
                ++index) {
                plan(delay->default_delays[index], annotation.values[index]);
                for (const auto& path : delay->paths)
                    plan(path.delays[index], annotation.values[index]);
            }
        }
        replacements.push_back(std::move(replacement));
    }
    for (const auto& replacement : replacements) {
        auto& process_operations
            = replacement.process->cold().program_storage()
                  .instance_program.operations;
        auto& operation
            = process_operations[replacement.instruction];
        if (replacement.timing_check) {
            auto* check = operation_get_if<VitalTimingCheck>(&operation);
            std::copy_n(replacement.values.begin(), 4U,
                check->limits.begin());
            auto* const vital_state
                = replacement.process->cold().vital_state.get();
            if (reset_timing_state && vital_state != nullptr) {
                vital_state->timing.erase(replacement.instruction);
            }
            replacement.process->cold()
                .synchronize_public_program_operations();
            continue;
        }
        for (const auto& definition : replacement.definitions) {
            process_operations[definition.instruction]
                = LoadConstant { definition.target,
                      PackedLogic4::from_aval_bval(
                          64U, definition.value, 0U) };
        }
        replacement.process->cold()
            .synchronize_public_program_operations();
    }
}

void Interpreter::set_process_executor(
    const ProcessId process,
    std::unique_ptr<ProcessExecutor> executor)
{
    if (impl_->started || impl_->start_in_progress) {
        throw std::logic_error(
            "cannot install a SimIR process executor after start");
    }
    if (!executor) {
        throw std::invalid_argument("SimIR process executor cannot be null");
    }
    auto& state = impl_->get_process(process);
    if (state.executor) {
        throw std::logic_error(
            "a SimIR process executor is already installed");
    }
    impl_->advance_process_executor_generation(state);
    state.executor = std::move(executor);
}

void Interpreter::set_deferred_process_executor(
    const ProcessId process,
    std::function<bool()> ready,
    std::function<std::unique_ptr<ProcessExecutor>()> take)
{
    set_deferred_process_executor(
        process, std::move(ready), std::move(take), { });
}

void Interpreter::set_deferred_process_executor(
    const ProcessId process,
    std::function<bool()> ready,
    std::function<std::unique_ptr<ProcessExecutor>()> take,
    DeferredProcessExecutorContract contract)
{
    if (impl_->started) {
        throw std::logic_error(
            "cannot defer a SimIR process executor after start");
    }
    if (!ready || !take) {
        throw std::invalid_argument(
            "deferred SimIR process executor requires both callbacks");
    }
    auto& state = impl_->get_process(process);
    if (state.executor || state.cold().deferred_executor) {
        throw std::logic_error(
            "a SimIR process executor is already installed or deferred");
    }
    impl_->advance_process_executor_generation(state);
    state.cold().deferred_executor.emplace(
        Impl::ProcessState::DeferredExecutor {
            std::move(ready), std::move(take),
            std::move(contract), { } });
}

void Interpreter::materialize_ready_process_executors()
{
    if (impl_->started) {
        return;
    }
    // Compact constants have no deferred executor by construction; the table
    // range visits full execution-state records only.
    for (auto& process : impl_->processes) {
        if (!process.executor && process.cold().deferred_executor
            && Impl::can_install_deferred_executor(process)
            && impl_->deferred_executor_ready(process)) {
            impl_->install_deferred_executor(process);
        }
    }
}

std::optional<Interpreter::Impl::ConstantDriverStartupEntry>
Interpreter::Impl::recognize_constant_driver_startup(
    const ProcessId id, const ProcessProgramView& program) const
{
    // Registration classifies the candidate before publishing its ProcessId
    // into ProcessTable, so this is a pre-insertion identity check.
    if (id != processes.size()) {
        return std::nullopt;
    }
    const auto register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            program.register_value_kinds());
    const auto& operations = program.operations();
    const bool copied_known_logic4_constant
        = program.register_count() == 2U
        && register_value_kinds.size() == 2U
        && register_value_kinds[0] == ValueKind::logic4
        && register_value_kinds[1] == ValueKind::logic4;
    const bool single_logic_constant
        = program.register_count() == 1U
        && register_value_kinds.size() == 1U
        && (register_value_kinds.front() == ValueKind::logic4
            || register_value_kinds.front() == ValueKind::logic9);
    const auto expected_without_statement
        = copied_known_logic4_constant ? 5U : 4U;
    const auto expected_with_statement
        = expected_without_statement + 1U;
    if (program.id() != id || !program.initialize() || program.final()
        || program.observed() || program.reactive() || program.postponed()
        || program.program_owner()
        || (program.scheduling_domain()
                != ProcessSchedulingDomain::systemverilog
            && program.scheduling_domain()
                != ProcessSchedulingDomain::generic)
        || !program.static_sensitivity().empty()
        || !program.static_trigger_regions().empty()
        || (!single_logic_constant
            && !copied_known_logic4_constant)
        || (copied_known_logic4_constant
            && program.scheduling_domain()
                != ProcessSchedulingDomain::systemverilog)
        || program.string_register_count() != 0U
        || program.container_register_count() != 0U
        || !program.debug_locals().empty()
        || !program.debug_string_locals().empty()
        || !program.debug_container_locals().empty()
        || !program.container_register_types().empty()
        || program.switch_source() || program.switch_target()
        || program.switch_control()
        || program.switch_source_offset() != 0U
        || program.switch_target_offset() != 0U
        || program.switch_width() != 0U
        || program.switch_bidirectional()
        || program.switch_resistive()
        || (operations.size() != expected_without_statement
            && operations.size() != expected_with_statement)
        || program.driver_regions().size() != 1U) {
        return std::nullopt;
    }

    const auto statement_offset
        = operations.size() == expected_with_statement ? 1U : 0U;
    const auto entry_point_operation = operations.expanded(0U);
    const auto statement_operation = operations.expanded(statement_offset);
    const auto load_operation = operations.expanded(1U + statement_offset);
    const auto copy_operation = copied_known_logic4_constant
        ? std::optional<Operation> {
              operations.expanded(2U + statement_offset)
          }
        : std::nullopt;
    const auto write_index
        = 2U + statement_offset
        + static_cast<std::size_t>(copied_known_logic4_constant);
    const auto write_operation = operations.expanded(write_index);
    const auto halt_operation = operations.expanded(write_index + 1U);
    const auto* entry_point
        = operation_get_if<DebugPoint>(&entry_point_operation);
    const auto* statement_point = statement_offset != 0U
        ? operation_get_if<DebugPoint>(&statement_operation) : nullptr;
    const auto* load = operation_get_if<LoadConstant>(&load_operation);
    const auto* copy = copy_operation
        ? operation_get_if<CopyRegister>(&*copy_operation) : nullptr;
    const auto* halt = operation_get_if<Halt>(&halt_operation);
    if (entry_point == nullptr || load == nullptr || halt == nullptr
        || entry_point->kind != DebugPointKind::process_entry
        || (statement_offset != 0U
            && (statement_point == nullptr
                || statement_point->kind != DebugPointKind::statement))
        || load->destination != 0U || load->value.width() == 0U
        || (copied_known_logic4_constant
            && (copy == nullptr || copy->destination != 1U
                || copy->source != load->destination
                || load->value.is_logic9()
                || !std::ranges::all_of(
                    load->value.bval_words(),
                    [](const std::uint64_t word) { return word == 0U; })))
        || halt->program_exit) {
        return std::nullopt;
    }

    const auto* whole = operation_get_if<WriteUpdate>(&write_operation);
    const auto* slice = operation_get_if<WriteUpdateSlice>(&write_operation);
    const auto* projected
        = operation_get_if<WriteProjected>(&write_operation);
    const auto* projected_slice
        = operation_get_if<WriteProjectedSlice>(&write_operation);
    const bool systemverilog_process
        = program.scheduling_domain()
            == ProcessSchedulingDomain::systemverilog;
    if (systemverilog_process) {
        if ((whole == nullptr) == (slice == nullptr)
            || projected != nullptr || projected_slice != nullptr) {
            return std::nullopt;
        }
    } else if (whole != nullptr || slice != nullptr
        || (projected == nullptr && projected_slice == nullptr)) {
        return std::nullopt;
    }
    const bool projected_write
        = projected != nullptr || projected_slice != nullptr;
    const auto signal_id = systemverilog_process
        ? (whole != nullptr ? whole->signal : slice->signal)
        : (projected != nullptr
                ? projected->signal : projected_slice->signal);
    const auto source = systemverilog_process
        ? (whole != nullptr ? whole->source : slice->source)
        : (projected != nullptr
                ? projected->source : projected_slice->source);
    const auto domain = whole != nullptr ? whole->domain
        : slice != nullptr ? slice->domain : SignalUpdateDomain::generic;
    const auto written_register = copy != nullptr
        ? copy->destination : load->destination;
    if (source != written_register
        || written_register >= register_value_kinds.size()
        || (systemverilog_process
            && domain != SignalUpdateDomain::systemverilog_active)
        || signal_id >= signals.size()
        || signals[signal_id].value_kind
            != register_value_kinds[written_register]
        || signals[signal_id].systemverilog_scalar
            != SystemVerilogScalarKind::None) {
        return std::nullopt;
    }
    const auto projected_mode = projected != nullptr
        ? projected->mode
        : projected_slice != nullptr
        ? projected_slice->mode : ProjectedDelayMode::inertial;
    const auto projected_delay = projected != nullptr
        ? projected->delay
        : projected_slice != nullptr ? projected_slice->delay : 0U;
    const auto projected_rejection = projected != nullptr
        ? projected->rejection
        : projected_slice != nullptr ? projected_slice->rejection : 0U;
    if (projected_write
        && ((projected_mode != ProjectedDelayMode::transport
                && projected_mode != ProjectedDelayMode::inertial)
            || (projected_mode == ProjectedDelayMode::inertial
                && projected_rejection > projected_delay))) {
        return std::nullopt;
    }

    const auto target_width = signals[signal_id].initial_value.width();
    const auto& region = program.driver_regions().front();
    if (whole != nullptr || projected != nullptr) {
        if (load->value.width() != target_width
            || region.signal != signal_id || !region.whole
            || region.offset != 0U || region.width != 0U) {
            return std::nullopt;
        }
    } else {
        if (slice != nullptr
            && register_value_kinds.front() != ValueKind::logic4) {
            return std::nullopt;
        }
        const auto offset = slice != nullptr
            ? slice->offset : projected_slice->offset;
        if (offset > target_width
            || load->value.width() > target_width - offset
            || region.signal != signal_id || region.whole
            || region.offset != offset
            || region.width != load->value.width()) {
            return std::nullopt;
        }
    }

    ConstantDriverStartupEntry result;
    result.id = id;
    result.signal = signal_id;
    result.offset = slice != nullptr
        ? slice->offset
        : projected_slice != nullptr ? projected_slice->offset : 0U;
    result.slice = slice != nullptr || projected_slice != nullptr;
    result.projected = projected_write;
    result.update_domain = projected_write
        ? SignalUpdateDomain::generic : domain;
    if (projected_write) {
        result.projected_mode = projected_mode;
        result.projected_delay = projected_delay;
        result.projected_rejection = projected_rejection;
    }
    return result;
}

void Interpreter::start()
{
    if (impl_->validation_only) {
        throw std::logic_error(
            "cannot start a validation-only SimIR interpreter");
    }
    if (impl_->started) {
        return;
    }
    if (impl_->start_in_progress) {
        throw std::logic_error {
            "SimIR interpreter startup cannot be reentered"
        };
    }
    if (impl_->package_static_startup_actions_entered) {
        throw std::logic_error {
            "SimIR startup cannot be retried after package startup actions ran"
        };
    }
    struct StartAttemptGuard {
        bool& active;
        ~StartAttemptGuard() { active = false; }
    };
    impl_->start_in_progress = true;
    const StartAttemptGuard start_attempt_guard { impl_->start_in_progress };
    impl_->package_static_startup_registration_closed = true;
    // Aggregate registration validates complete proxy families; sparse
    // element-only alias maps remain supported. Promote retained aggregate
    // authority before any process can observe the simulation state.
    impl_->promote_all_container_alias_authorities();
    const auto selected_signal_count = static_cast<std::size_t>(
        std::ranges::count_if(
            impl_->signal_selection_histories,
            [](const auto& histories) { return !histories.empty(); }));
    if (selected_signal_count != 0U) {
        impl_->signal_selection_pending_transaction_signals.reserve(
            selected_signal_count);
    }
    impl_->rebuild_static_fanout();
    impl_->build_native_signal_dependency_masks();
    impl_->build_native_signal_publication_shape_certificate();
    impl_->build_direct_signal_read_capabilities();
    if (impl_->direct_signal_read_owner_token == 0U) {
        impl_->direct_signal_read_owner_token
            = allocate_direct_signal_read_owner_token();
    }
    // Complete sampled-history preallocation before start becomes committed,
    // so an allocation failure can be retried without a partially started run.
    impl_->prepare_systemverilog_update_pool();
    // The cold process sidecars now own every template reference. Drop the
    // startup interning buckets before entering the execution lifecycle.
    impl_->process_program_templates.clear();
    // Cohort and scheduler setup may retain references to individual state
    // fields. Publish initial records through their address-stable owner
    // before creating any such bindings.
    impl_->processes.freeze_initial_storage();
    struct PackageStartupAction {
        std::uint64_t order { };
        ProcessId process { std::numeric_limits<ProcessId>::max() };
        std::size_t callback { std::numeric_limits<std::size_t>::max() };
    };
    std::vector<PackageStartupAction> package_startup_actions;
    package_startup_actions.reserve(
        impl_->processes.size()
        + impl_->package_static_startup_callbacks.size());
    // A static kernel member's startup action is the kernel's: the kernel
    // lists its package static initializers (the host's own program is the
    // kernel's stub).
    const auto kernel_member = [&](const ProcessId id) {
        return impl_->static_kernel && id < impl_->fusion_dormant_process.size()
            && impl_->fusion_dormant_process[id] != 0U;
    };
    if (impl_->static_kernel) {
        for (const auto& member : impl_->static_kernel->startup_members()) {
            package_startup_actions.push_back({
                member.order, member.process,
                std::numeric_limits<std::size_t>::max() });
        }
    }
    for (ProcessId id = 0U; id < impl_->processes.size(); ++id) {
        if (kernel_member(id)) {
            continue;
        }
        const auto program = impl_->processes.program_view(id);
        if (program.startup_phase()
            == ProcessStartupPhase::package_static_initializer) {
            if (impl_->processes.is_compact_constant(id)) {
                throw std::logic_error {
                    "package static initializer was compacted as a scheduled write"
                };
            }
            package_startup_actions.push_back({
                program.startup_order(), id,
                std::numeric_limits<std::size_t>::max() });
        }
    }
    for (std::size_t index = 0U;
         index < impl_->package_static_startup_callbacks.size();
         ++index) {
        package_startup_actions.push_back({
            impl_->package_static_startup_callbacks[index].startup_order,
            std::numeric_limits<ProcessId>::max(), index });
    }
    std::ranges::sort(package_startup_actions, [](const auto& left,
                                                  const auto& right) {
        return left.order < right.order;
    });
    for (std::size_t index = 1U;
         index < package_startup_actions.size();
         ++index) {
        if (package_startup_actions[index - 1U].order
            == package_startup_actions[index].order) {
            throw std::logic_error {
                "package static startup action order is not unique"
            };
        }
    }
    if (!package_startup_actions.empty()) {
        impl_->package_static_startup_signal_processes.assign(
            impl_->signals.size(), std::numeric_limits<ProcessId>::max());
    }
    if (!package_startup_actions.empty() && impl_->scheduler.has_pending()) {
        throw std::logic_error {
            "package static initialization started with pending scheduler work"
        };
    }
    for (const auto& action : package_startup_actions) {
        if (impl_->scheduler.has_pending() || impl_->stopped_by_design) {
            throw std::logic_error {
                "package static initialization encountered deferred startup work"
            };
        }
        impl_->package_static_startup_actions_entered = true;
        impl_->package_static_initialization_active = true;
        if (action.callback
            != std::numeric_limits<std::size_t>::max()) {
            impl_->package_static_initialization_callback_active = true;
            impl_->package_static_initialization_callback_index
                = action.callback;
            try {
                const auto& callback
                    = impl_->package_static_startup_callbacks.at(action.callback);
                callback.callback();
                // The application's class services ran the initializer on
                // the host's signals: kernel-owned ones take its values.
                if (impl_->static_kernel) {
                    for (const auto signal : callback.writable_signal_ids) {
                        impl_->static_kernel->import_startup_write(signal);
                    }
                }
            } catch (...) {
                impl_->package_static_initialization_callback_active = false;
                impl_->package_static_initialization_callback_index
                    = std::numeric_limits<std::size_t>::max();
                impl_->package_static_initialization_active = false;
                throw;
            }
            impl_->package_static_initialization_callback_active = false;
            impl_->package_static_initialization_callback_index
                = std::numeric_limits<std::size_t>::max();
        } else {
            const auto id = action.process;
            const auto program = impl_->processes.program_view(id);
            for (const auto& region : program.driver_regions()) {
                if (region.signal >= impl_->signals.size()) {
                    impl_->package_static_initialization_active = false;
                    throw std::logic_error {
                        "package static initializer has an invalid write signal"
                    };
                }
                impl_->package_static_startup_signal_processes[
                    region.signal] = id;
            }
            impl_->package_static_initialization_process = id;
            try {
                if (kernel_member(id)) {
                    impl_->static_kernel->run_startup_member(id);
                } else if (impl_->static_kernel_hosts(id)) {
                    // Hosted tier: the kernel runs the initializer (S25).
                    impl_->static_kernel->execute_hosted_startup(id);
                } else {
                    impl_->execute(id);
                }
            } catch (...) {
                impl_->package_static_initialization_active = false;
                impl_->package_static_initialization_process
                    = std::numeric_limits<ProcessId>::max();
                throw;
            }
            impl_->package_static_initialization_process
                = std::numeric_limits<ProcessId>::max();
        }
        impl_->package_static_initialization_active = false;
        if (action.process != std::numeric_limits<ProcessId>::max()
            && !kernel_member(action.process)) {
            const auto& process = impl_->get_process(action.process);
            if (!process.halted || process.suspended
                || process.status != ProcessStatus::finished) {
                throw std::logic_error {
                    "package static initializer did not finish synchronously"
                };
            }
        }
        if (impl_->scheduler.has_pending() || impl_->stopped_by_design) {
            throw std::logic_error {
                "package static startup action created deferred work"
            };
        }
    }
    impl_->package_static_startup_callbacks.clear();
    // The startup assignments above establish the simulation baseline. Build
    // active runtime inventories and sampled defaults only after that baseline
    // is final, before ordinary time-zero processes are prearmed or queued.
    // Reserve the static-cohort scratch before simulation can run a cohort.
    impl_->cohort_overflow_contexts.reserve(impl_->processes.size());
    impl_->cohort_overflow_entries.reserve(impl_->processes.size());
    if (impl_->requires_sampled_values) {
        if (impl_->sampled_value_dependency_mask.size()
            != impl_->signals.size()) {
            impl_->sampled_value_dependencies_unknown = true;
        }
        impl_->sampled_defaults.resize(impl_->signals.size());
        impl_->sampled_values.resize(impl_->signals.size());
        for (std::size_t signal = 0U;
             signal < impl_->signals.size();
             ++signal) {
            if (!impl_->sampled_value_dependencies_unknown
                && impl_->sampled_value_dependency_mask[signal] == 0U) {
                continue;
            }
            // Aggregate aliases keep authoritative current state in their
            // retained projection rather than the proxy Signal object.
            const auto& initial_value = impl_->logical_signal_value(
                static_cast<SignalId>(signal));
            impl_->sampled_defaults[signal]
                = initial_value;
            impl_->sampled_values[signal]
                = initial_value;
        }
        impl_->build_sampled_history_clock_index();
    }
    impl_->started = true;
    impl_->initialize_vhdl_resolutions();
    // Switch networks are solved when a member's drivers change; implicit
    // drivers such as supply nets never change, so solve every network once.
    impl_->refresh_switch_network();
    for (const auto& entry : impl_->startup_write_body_caches) {
        entry.second->release_registration_identity();
    }
    impl_->startup_write_body_caches.clear();
    impl_->operation_body_kinds.clear();
    if (impl_->native_process_count_profile_enabled) {
        impl_->native_process_resume_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_single_resume_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_single_static_wait_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_cohort_resume_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_cohort_static_wait_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_word_fanout_ready_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_static_trigger_counts.assign(
            impl_->processes.size(), { });
        impl_->native_process_single_wave_processes.clear();
        impl_->native_process_single_wave_offsets.clear();
        impl_->native_process_single_wave_identity.reset();
    }
    if (impl_->profile_static_cohorts_enabled) {
        std::vector<std::size_t> cohorts;
        for (std::size_t index = 0;
            index < impl_->static_sensitivity_cohorts.size(); ++index) {
            if (impl_->static_sensitivity_cohorts[index].members.size() > 1U) {
                cohorts.push_back(index);
            }
        }
        std::ranges::sort(cohorts, [&](const auto left, const auto right) {
            return impl_->static_sensitivity_cohorts[left].members.size()
                > impl_->static_sensitivity_cohorts[right].members.size();
        });
        std::size_t grouped_processes { };
        for (const auto cohort : cohorts) {
            grouped_processes += impl_->static_sensitivity_cohorts[cohort].members.size();
        }
        std::cerr << "fsim-profile: static-cohorts cohorts=" << cohorts.size()
                  << " processes=" << grouped_processes << '\n';
        for (const auto cohort : cohorts | std::views::take(20U)) {
            const auto& members
                = impl_->static_sensitivity_cohorts[cohort].members;
            const auto first = impl_->processes.program_view(members.front());
            std::cerr << "  size=" << members.size()
                      << " sensitivity=" << first.static_sensitivity().size()
                      << " first=" << first.name() << '\n';
        }
    }
    std::vector<bool> prearmed_static_waits(
        impl_->processes.size(), false);
    // Inert static kernel members are never initialized on the host.
    const auto inert = [&](const ProcessId id) {
        return impl_->static_kernel
            && id < impl_->fusion_dormant_process.size()
            && impl_->fusion_dormant_process[id] == 1U;
    };
    // A VHDL or mixed kernel with hosted VHDL processes: the initial run of
    // a hosted process belongs to the kernel's first round, which is the
    // initialization delta (S25a). Under the debugger the first activation
    // is likewise queued, so it runs where the debugger can stop it.
    const bool kernel_runs_after_hosted_start = [&] {
        if (!impl_->static_kernel) {
            return false;
        }
        if (impl_->static_kernel->debug_points()
            || impl_->static_kernel->queues_first_activation()) {
            return true;
        }
        for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
            if (impl_->static_kernel_hosts(id)
                && impl_->processes.scheduling_domain(id)
                    != ProcessSchedulingDomain::systemverilog) {
                return true;
            }
        }
        return false;
    }();
    // The kernel's host process arms last: its first run is the kernel's
    // first activation, which runs the kernel's time-zero processes, and
    // every static wait is armed before any of those runs.
    std::optional<ProcessId> kernel_host;
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (inert(id)) {
            continue;
        }
        const auto program = impl_->processes.program_view(id);
        if (program.startup_phase()
            != ProcessStartupPhase::ordinary) {
            continue;
        }
        if (!program.initialize() || program.final()
            || program.static_sensitivity().empty()
            || program.operations().empty()
            || !operation_holds<WaitSensitivity>(
                std::as_const(program.operations()).front())) {
            continue;
        }
        if (impl_->processes.is_compact_constant(id)) {
            continue;
        }
        if (!kernel_runs_after_hosted_start && impl_->static_kernel
            && id < impl_->fusion_dormant_process.size()
            && impl_->fusion_dormant_process[id] == 2U) {
            kernel_host = id;
            continue;
        }
        if (kernel_runs_after_hosted_start
            && id < impl_->fusion_dormant_process.size()
            && impl_->fusion_dormant_process[id] == 2U) {
            // The kernel's first activation is the initialization of the
            // design (IEEE 1076-2019 14.7.5.2), with the hosted VHDL
            // processes' initial runs: it is queued for the first delta
            // below like them, not run here. Run here, its rounds would
            // count start() as a delta and its threads would run one delta
            // ahead of the host's updates.
            continue;
        } else if (impl_->static_kernel_hosts(id)) {
            impl_->static_kernel->execute_hosted_startup(id);
        } else {
            impl_->execute(id);
        }
        prearmed_static_waits[id] = true;
    }
    if (kernel_host) {
        impl_->execute(*kernel_host);
        prearmed_static_waits[*kernel_host] = true;
    }
    // A SystemVerilog process that waits only on time and reads a settling
    // signal starts one delta after the other processes, so time-zero
    // continuous assignments and event controls settle before it reads them
    // (the order within the active region is unspecified, IEEE 1800-2017
    // 4.7). The static kernel's threads follow the same rule.
    const auto late_start = systemverilog_late_starters(
        impl_->processes.size(), impl_->signals.size(),
        [&](const std::size_t id) {
            // The kernel's host runs the kernel stub; the rule reads the
            // program it was shaped from, as the planner does.
            if (impl_->static_kernel) {
                if (const auto* original = impl_->static_kernel->startup_program(
                        static_cast<ProcessId>(id))) {
                    return ProcessProgramView { *original };
                }
            }
            return impl_->processes.program_view(static_cast<ProcessId>(id));
        },
        [&](const std::size_t id) {
            return impl_->processes.is_compact_constant(
                static_cast<ProcessId>(id));
        },
        [&](const std::size_t id) { return inert(static_cast<ProcessId>(id)); },
        [&](const std::size_t signal) {
            return impl_->signals[signal].resolution != ResolutionKind::none;
        },
        [&](const std::size_t id) {
            return id < impl_->static_kernel_event_driven_process.size()
                && impl_->static_kernel_event_driven_process[id] != 0U;
        });
    const auto starts_late = [&](const ProcessId id) {
        return static_cast<bool>(late_start[id]);
    };
    const auto kernel_late = impl_->static_kernel
        ? impl_->static_kernel->first_late_process()
        : std::nullopt;
    for (const bool late : { false, true }) {
        // Processes of the after_procedures phase start once every other
        // ordinary process of time zero has been queued.
        for (const auto phase : { ProcessStartupPhase::ordinary,
                 ProcessStartupPhase::after_procedures }) {
            for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
                if (late && phase == ProcessStartupPhase::ordinary
                    && kernel_late == std::optional { id }) {
                    // The kernel's late members start in its late activation
                    // (start_behavioral).
                    impl_->static_kernel->queue_late_start(id);
                }
                if (inert(id)) {
                    continue;
                }
                const auto program = impl_->processes.program_view(id);
                if (program.startup_phase() != phase) {
                    continue;
                }
                if (!program.initialize() || program.final()
                    || prearmed_static_waits[id]
                    || starts_late(id) != late) {
                    continue;
                }
                if (late) {
                    impl_->queue_next_delta(id);
                } else {
                    impl_->queue_at(id, impl_->scheduler.now(), true);
                }
            }
        }
    }
}

bool Interpreter::package_static_initialization_active() const noexcept
{
    return impl_->package_static_initialization_active;
}

RunResult Interpreter::run(std::optional<SimulationTick> until)
{
    impl_->simulator_status.reset();
    start();
    auto ordinary = impl_->scheduler.run(until);
    ordinary.simulator_status = impl_->simulator_status;
    const bool design_stop = ordinary.status == RunStatus::stopped
        && impl_->stopped_by_design;
    if (impl_->finals_ran
        || (ordinary.status != RunStatus::completed
            && !design_stop)) {
        return ordinary;
    }

    impl_->finals_ran = true;
    if (design_stop) {
        impl_->scheduler.discard_pending();
        impl_->scheduler.clear_stop();
    }
    if (impl_->static_kernel) {
        // Hosted final blocks are scheduled by the kernel (S25).
        impl_->static_kernel->run_finals(design_stop);
    }
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        // Static kernel members are never final blocks.
        if (impl_->static_kernel && id < impl_->fusion_dormant_process.size()
            && impl_->fusion_dormant_process[id] == 1U) {
            continue;
        }
        if (impl_->static_kernel_hosts(id)) {
            continue;
        }
        if (impl_->processes.program_view(id).final()) {
            impl_->queue_at(id, impl_->scheduler.now());
        }
    }
    if (!impl_->scheduler.has_pending()) {
        if (design_stop) {
            impl_->scheduler.request_stop();
        }
        return ordinary;
    }

    const auto final_result = impl_->scheduler.run();
    ordinary.time = final_result.time;
    ordinary.delta = final_result.delta;
    ordinary.callbacks_executed += final_result.callbacks_executed;
    if (design_stop) {
        ordinary.status = RunStatus::stopped;
        impl_->scheduler.request_stop();
    } else {
        ordinary.status = final_result.status;
    }
    return ordinary;
}

RunResult Interpreter::finish()
{
    start();
    RunResult result {
        RunStatus::stopped,
        impl_->scheduler.now(),
        impl_->scheduler.delta(),
        0,
        std::nullopt,
    };
    if (impl_->finals_ran) {
        impl_->scheduler.request_stop();
        return result;
    }

    impl_->finals_ran = true;
    impl_->scheduler.discard_pending();
    impl_->scheduler.clear_stop();
    if (impl_->static_kernel) {
        impl_->static_kernel->run_finals(true);
    }
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (impl_->static_kernel_hosts(id)) {
            continue;
        }
        if (impl_->processes.program_view(id).final()) {
            impl_->queue_at(id, impl_->scheduler.now());
        }
    }
    if (impl_->scheduler.has_pending()) {
        const auto final_result = impl_->scheduler.run();
        result.time = final_result.time;
        result.delta = final_result.delta;
        result.callbacks_executed = final_result.callbacks_executed;
    }
    result.status = RunStatus::stopped;
    impl_->scheduler.request_stop();
    return result;
}

void Interpreter::deposit_signal(SignalId signal, PackedLogic4 value,
    const ProcessSchedulingDomain origin_domain)
{
    if (!impl_->started) {
        impl_->vhdl_deposited_before_start.insert(signal);
    }
    // A net joined by bidirectional switches takes its value from the whole
    // network, so a deposit there persists as an external driver that the
    // network resolves through to every endpoint (IEEE 1800-2017 38.34).
    if (impl_->switch_network_member(signal)
        && impl_->driven_values[signal].width() == value.width()) {
        impl_->external_driver_slot(signal)
            = impl_->normalize_signal_value(signal, std::move(value));
        impl_->mark_switch_network_dirty(signal);
        impl_->refresh_switch_network();
        return;
    }
    impl_->commit(signal, std::move(value),
        SignalChangeOrigin { origin_domain, SchedulerPhase::active });
    if (impl_->static_kernel && impl_->started
        && origin_domain == ProcessSchedulingDomain::generic) {
        impl_->static_kernel->host_deposit(signal);
    }
}

void Interpreter::deposit_scalar_signal(
    const SignalId signal,
    const SystemVerilogScalarValue value)
{
    const auto& stored = impl_->get_signal(signal);
    if (stored.systemverilog_scalar != value.kind) {
        throw std::invalid_argument { "SimIR scalar signal kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded) {
        throw std::invalid_argument { "invalid SimIR scalar signal value" };
    }
    deposit_signal(signal, encoded.value);
}

void Interpreter::force_signal(SignalId signal, PackedLogic4 value,
    const ProcessSchedulingDomain origin_domain)
{
    if (impl_->package_static_initialization_active) {
        throw std::logic_error {
            "package static startup cannot force a signal"
        };
    }
    const auto width = impl_->get_signal(signal).initial_value.width();
    if (width != value.width()) {
        throw std::invalid_argument("SimIR signal force width mismatch");
    }
    impl_->force_slice(signal, std::move(value), 0,
        SignalChangeOrigin { origin_domain, SchedulerPhase::active });
}

void Interpreter::force_scalar_signal(
    const SignalId signal,
    const SystemVerilogScalarValue value)
{
    const auto& stored = impl_->get_signal(signal);
    if (stored.systemverilog_scalar != value.kind) {
        throw std::invalid_argument { "SimIR scalar signal kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded) {
        throw std::invalid_argument { "invalid SimIR scalar signal value" };
    }
    force_signal(signal, encoded.value);
}

void Interpreter::release_signal(SignalId signal,
    const ProcessSchedulingDomain origin_domain)
{
    if (impl_->package_static_initialization_active) {
        throw std::logic_error {
            "package static startup cannot release a signal force"
        };
    }
    const auto width = impl_->get_signal(signal).initial_value.width();
    impl_->release_slice(signal, 0, width,
        SignalChangeOrigin { origin_domain, SchedulerPhase::active });
}

void Interpreter::force_signal_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset)
{
    if (impl_->package_static_initialization_active) {
        throw std::logic_error {
            "package static startup cannot force a signal slice"
        };
    }
    impl_->force_slice(signal, std::move(value), offset);
}

void Interpreter::release_signal_slice(
    const SignalId signal,
    const std::size_t offset,
    const std::size_t width)
{
    if (impl_->package_static_initialization_active) {
        throw std::logic_error {
            "package static startup cannot release a signal slice force"
        };
    }
    impl_->release_slice(signal, offset, width);
}

bool Interpreter::signal_is_forced(const SignalId signal) const
{
    (void)impl_->get_signal(signal);
    if (signal < impl_->signal_container_aggregate_aliases.size()
        && impl_->signal_container_aggregate_aliases[signal]) {
        const auto object = *impl_->signal_container_aggregate_aliases[signal];
        const auto& aliases = impl_->container_element_signal_aliases.at(object);
        return std::ranges::any_of(aliases, [&](const auto& alias) {
            return alias && static_cast<bool>(impl_->forced_values[alias->signal]);
        });
    }
    return static_cast<bool>(impl_->forced_values[signal]);
}

void Interpreter::schedule_signal_at(SignalId signal, PackedLogic4 value,
    SimulationTick time, StableOrder order)
{
    if (impl_->package_static_initialization_active) {
        throw std::logic_error {
            "package static startup cannot schedule a signal write"
        };
    }
    impl_->require_writable_aggregate_signal(signal);
    // Validate eagerly so a malformed drive does not fail much later.
    if (impl_->get_signal(signal).initial_value.width() != value.width()) {
        throw std::invalid_argument(
            "SimIR signal assignment width mismatch (signal "
            + std::to_string(signal) + " has width "
            + std::to_string(impl_->get_signal(signal).initial_value.width())
            + ", value has width " + std::to_string(value.width()) + ")");
    }
    impl_->scheduler.schedule_at(
        time, SchedulerPhase::update, order,
        [state = impl_.get(), signal, value = std::move(value)](
            Scheduler&) mutable {
            state->stage_update(signal, std::move(value));
        });
}

void Interpreter::schedule_scalar_signal_at(
    const SignalId signal,
    const SystemVerilogScalarValue value,
    const SimulationTick time,
    const StableOrder order)
{
    const auto& stored = impl_->get_signal(signal);
    if (stored.systemverilog_scalar != value.kind) {
        throw std::invalid_argument { "SimIR scalar signal kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded) {
        throw std::invalid_argument { "invalid SimIR scalar signal value" };
    }
    schedule_signal_at(signal, encoded.value, time, order);
}

void Interpreter::schedule_signal_after(SignalId signal, PackedLogic4 value,
    SimulationTick delay,
    StableOrder order)
{
    if (impl_->package_static_initialization_active) {
        throw std::logic_error {
            "package static startup cannot schedule a signal write"
        };
    }
    if (delay > std::numeric_limits<SimulationTick>::max() - impl_->scheduler.now()) {
        throw std::overflow_error("simulation time overflow scheduling signal");
    }
    schedule_signal_at(signal, std::move(value), impl_->scheduler.now() + delay,
        order);
}

void Interpreter::schedule_scalar_signal_after(
    const SignalId signal,
    const SystemVerilogScalarValue value,
    const SimulationTick delay,
    const StableOrder order)
{
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - impl_->scheduler.now()) {
        throw std::overflow_error { "simulation time overflow scheduling signal" };
    }
    schedule_scalar_signal_at(
        signal, value, impl_->scheduler.now() + delay, order);
}

const PackedLogic4& Interpreter::signal_value(SignalId signal) const
{
    impl_->expose_signal_value_reference(signal);
    (void)impl_->get_signal(signal);
    if (signal < impl_->signal_container_aggregate_aliases.size()
        && impl_->signal_container_aggregate_aliases[signal]) {
        return impl_->aggregate_signal_current_value(signal);
    }
    return impl_->get_signal(signal).initial_value;
}

PackedLogic4 Interpreter::signal_value_snapshot(const SignalId signal) const
{
    impl_->prepare_signal_observation(signal);
    if (signal < impl_->signal_container_aggregate_aliases.size()
        && impl_->signal_container_aggregate_aliases[signal]) {
        return impl_->aggregate_signal_current_value(signal);
    }
    return impl_->get_signal(signal).initial_value;
}

const PackedLogic4& Interpreter::stored_signal_value(
    const SignalId signal) const
{
    impl_->expose_signal_value_reference(signal);
    (void)impl_->get_signal(signal);
    if (signal < impl_->signal_container_aggregate_aliases.size()
        && impl_->signal_container_aggregate_aliases[signal]) {
        const auto object
            = *impl_->signal_container_aggregate_aliases[signal];
        auto& projection
            = *impl_->aggregate_signal_stored_projection.at(signal);
        const auto revision
            = impl_->container_aggregate_stored_revisions.at(object);
        if (impl_->aggregate_signal_stored_projection_revisions[signal]
            != revision) {
            const auto& value
                = impl_->get_container_object(object).initial_value;
            const auto& aliases
                = impl_->container_element_signal_aliases.at(object);
            const auto width = value.type.element_width;
            const auto count = value.elements.size();
            for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
                const auto leaf = aliases[ordinal]->signal;
                projection.insert_bits(
                    impl_->driven_values.at(leaf),
                    (count - ordinal - 1U) * width);
            }
            impl_->aggregate_signal_stored_projection_revisions[signal]
                = revision;
        }
        return projection;
    }
    return impl_->driven_values.at(signal);
}

PackedLogic4 Interpreter::stored_signal_value_snapshot(
    const SignalId signal) const
{
    impl_->prepare_signal_observation(signal);
    if (signal < impl_->signal_container_aggregate_aliases.size()
        && impl_->signal_container_aggregate_aliases[signal]) {
        const auto object
            = *impl_->signal_container_aggregate_aliases[signal];
        auto& projection
            = *impl_->aggregate_signal_stored_projection.at(signal);
        const auto revision
            = impl_->container_aggregate_stored_revisions.at(object);
        if (impl_->aggregate_signal_stored_projection_revisions[signal]
            != revision) {
            const auto& value
                = impl_->get_container_object(object).initial_value;
            const auto& aliases
                = impl_->container_element_signal_aliases.at(object);
            const auto width = value.type.element_width;
            const auto count = value.elements.size();
            for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
                const auto leaf = aliases[ordinal]->signal;
                projection.insert_bits(
                    impl_->driven_values.at(leaf),
                    (count - ordinal - 1U) * width);
            }
            impl_->aggregate_signal_stored_projection_revisions[signal]
                = revision;
        }
        return projection;
    }
    return impl_->driven_values.at(signal);
}

SystemVerilogScalarValue Interpreter::scalar_signal_value(
    const SignalId signal) const
{
    impl_->prepare_signal_observation(signal);
    const auto& stored = impl_->get_signal(signal);
    const auto decoded = decode_systemverilog_scalar_payload(
        stored.initial_value, stored.systemverilog_scalar);
    if (!decoded) {
        throw std::logic_error { "SimIR signal is not a valid scalar value" };
    }
    return decoded.value;
}

std::vector<SystemVerilogScalarSignalSnapshot>
Interpreter::scalar_signal_snapshots() const
{
    std::vector<SystemVerilogScalarSignalSnapshot> result;
    for (std::size_t index = 0; index < impl_->signals.size(); ++index) {
        const auto signal = static_cast<SignalId>(index);
        const auto& stored = impl_->signals[index];
        if (stored.systemverilog_scalar == SystemVerilogScalarKind::None) {
            continue;
        }
        result.push_back({ signal,
            impl_->get_signal_cold(signal).name,
            scalar_signal_value(signal) });
    }
    return result;
}

const std::string& Interpreter::string_object_value(
    const StringObjectId object) const
{
    return impl_->string_object_storage(object);
}

void Interpreter::Impl::initialize_package_static_string_object(
    const StringObjectId object_id,
    const std::string_view value,
    const std::optional<ProcessId> process)
{
    const bool in_range = object_id < string_objects.size();
    require_package_static_write("string", in_range,
        in_range ? string_objects[object_id].package_owner_id : 0U,
        object_id, process, false);
    if (value.size() > maximum_string_bytes) {
        throw std::length_error {
            "SimIR string object exceeds byte limit"
        };
    }
    (void)systemverilog_string_length(value);
    string_objects[object_id].initial_value = value;
}

void Interpreter::deposit_string_object(
    const StringObjectId object,
    const std::string_view value)
{
    if (impl_->package_static_initialization_active) {
        impl_->initialize_package_static_string_object(object, value);
        return;
    }
    if (value.size() > maximum_string_bytes) {
        throw std::length_error {
            "SimIR string object exceeds byte limit"
        };
    }
    (void)systemverilog_string_length(value);
    impl_->string_object_storage(object) = value;
}

const ContainerValue& Interpreter::container_object_value(
    const ContainerObjectId object) const
{
    impl_->expose_container_value_reference(object);
    return impl_->read_container_object_value(object);
}

ContainerValue Interpreter::container_object_value_snapshot(
    const ContainerObjectId object) const
{
    impl_->prepare_container_value_observation(object);
    return impl_->read_container_object_value(object);
}

void Interpreter::deposit_container_object(
    const ContainerObjectId object,
    ContainerValue value)
{
    if (impl_->package_static_initialization_active) {
        impl_->initialize_package_static_container_object(object, value);
        return;
    }
    impl_->write_container_object_value(object, value);
}

void Interpreter::deposit_container_object_element(
    const ContainerObjectId object,
    const std::size_t ordinal,
    PackedLogic4 value)
{
    if (impl_->package_static_initialization_active) {
        const auto& target = impl_->get_container_object(object);
        if (ordinal >= target.initial_value.elements.size()
            || value.width() != target.initial_value.type.element_width
            || value.is_logic9()
            || (target.initial_value.type.two_state && has_unknown(value))) {
            throw std::invalid_argument {
                "SimIR container element deposit type or ordinal mismatch"
            };
        }
        auto replacement = target.initial_value;
        replacement.elements[ordinal] = std::move(value);
        impl_->initialize_package_static_container_object(
            object, replacement);
        return;
    }
    auto& target = impl_->get_container_object(object);
    if (ordinal >= target.initial_value.elements.size()
        || value.width() != target.initial_value.type.element_width
        || value.is_logic9()
        || (target.initial_value.type.two_state && has_unknown(value))) {
        throw std::invalid_argument {
            "SimIR container element deposit type or ordinal mismatch"
        };
    }
    const auto& aliases = impl_->container_element_signal_aliases.at(object);
    if (ordinal < aliases.size() && aliases[ordinal]) {
        const auto& alias = *aliases[ordinal];
        if (!alias.writable) {
            throw std::invalid_argument {
                "SimIR container element is not writable through its signal alias"
            };
        }
        impl_->commit(alias.signal, std::move(value));
        return;
    }
    if (std::ranges::any_of(
            aliases,
            [](const auto& alias) { return alias.has_value(); })) {
        const bool changed = target.initial_value.elements[ordinal] != value;
        impl_->invalidate_container_aggregate_extract(object);
        target.initial_value.elements[ordinal] = std::move(value);
        if (changed) {
            if (impl_->container_value_reference_exposed[object] != 0U) {
                impl_->synchronize_container_value_references_from_object(
                    object);
            }
            const auto waiters = impl_->container_dynamic_fanout.at(object);
            for (const auto process : waiters) {
                impl_->queue_next_delta(process);
            }
            if (impl_->container_object_change_hook) {
                impl_->container_object_change_hook(object, impl_->scheduler.now());
            }
        }
        return;
    }
    auto replacement = impl_->read_container_object_value(object);
    replacement.elements[ordinal] = std::move(value);
    impl_->write_container_object_value(object, replacement);
}

PackedLogic4 Interpreter::driver_value(
    const ProcessId process,
    const SignalId signal) const
{
    if (process >= impl_->processes.size()) {
        throw std::out_of_range("invalid SimIR process ID");
    }
    if (signal < impl_->signal_container_aggregate_aliases.size()
        && impl_->signal_container_aggregate_aliases[signal]) {
        const auto object = *impl_->signal_container_aggregate_aliases[signal];
        const auto& type = impl_->get_container_object(object).initial_value.type;
        const auto& aliases = impl_->container_element_signal_aliases.at(object);
        const auto count = aliases.size();
        const auto width = type.element_width;
        auto projection = PackedLogic4(
            impl_->get_signal(signal).initial_value.width(), Logic4::z);
        auto found_driver = false;
        for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
            const auto leaf = aliases[ordinal]->signal;
            const DriverRecord* record { };
            record = impl_->driver_values.at(leaf).find(process);
            if (record == nullptr) {
                continue;
            }
            projection.insert_bits(
                record->value, (count - ordinal - 1U) * width);
            found_driver = true;
        }
        if (!found_driver) {
            throw std::out_of_range(
                "process has no driver slot for SimIR aggregate signal");
        }
        return projection;
    }
    return impl_->underlying_driver_value(process, signal);
}

ProcessId Interpreter::design_process(const ProcessId process) const
{
    // A fork child of a kernel thread (its output names it).
    if (impl_->static_kernel) {
        if (const auto design
            = impl_->static_kernel->child_design_process(process)) {
            return *design;
        }
    }
    if (impl_->processes.is_compact_constant(process)) {
        return process;
    }
    return impl_->get_process(process).cold().design_process;
}

const Process& Interpreter::process_program(const ProcessId process) const
{
    // A host member runs the kernel stub on the scheduler; the debugger
    // and the VPI's compiletf scan (IEEE 1800-2023 38.36.2) see the program
    // the member really runs.
    if (impl_->static_kernel) {
        if (const auto* original
            = impl_->static_kernel->startup_program(process)) {
            return *original;
        }
    }
    return impl_->processes.public_program(process);
}

InstructionIndex Interpreter::process_instruction(
    const ProcessId process) const
{
    if (const auto* compact = impl_->processes.compact_constant(process)) {
        return compact->pc;
    }
    return impl_->get_process(process).pc;
}

void Interpreter::track_process_interpreter_operations(
    const ProcessId process)
{
    if (auto* compact = impl_->processes.compact_constant(process)) {
        compact->track_interpreter_operations = true;
        return;
    }
    impl_->get_process(process).cold().track_interpreter_operations = true;
}

std::uint64_t Interpreter::process_interpreter_operations(
    const ProcessId process) const
{
    if (const auto* compact = impl_->processes.compact_constant(process)) {
        return compact->interpreter_operations;
    }
    return impl_->get_process(process).cold().interpreter_operations;
}

ProcessId Interpreter::dynamic_process_root(const ProcessId process) const
{
    if (impl_->static_kernel) {
        if (const auto root
            = impl_->static_kernel->child_dynamic_root(process)) {
            return *root;
        }
    }
    auto root = process;
    while (true) {
        if (impl_->processes.is_compact_constant(root)) {
            return root;
        }
        const auto parent = impl_->get_process(root).cold().fork_parent;
        if (!parent) {
            return root;
        }
        if (!impl_->get_process(*parent).cold().fork_parent) {
            return root;
        }
        root = *parent;
    }
    return root;
}

void Interpreter::kill_dynamic_processes(
    const std::span<const ProcessId> design_processes)
{
    impl_->kill_dynamic_processes(design_processes);
    if (impl_->static_kernel) {
        impl_->static_kernel->kill_member_children(design_processes);
    }
}

std::vector<Interpreter::SvaObligation> Interpreter::finish_sva_monitors()
{
    std::vector<SvaObligation> result;
    for (auto& [key, monitor] : impl_->sva_monitors) {
        const auto finished = monitor->finish();
        if (std::getenv("FSIM_SVA_TRACE") != nullptr) {
            std::cerr << "fsim sva: finish process " << key.first
                      << " site " << key.second << " failures "
                      << finished.failures << " holds " << finished.holds
                      << '\n';
        }
        if (finished.failures != 0U || finished.holds != 0U) {
            result.push_back(
                { key.first, finished.failures, finished.holds });
        }
    }
    return result;
}

void Interpreter::kill_sva_monitors(
    const std::span<const ProcessId> design_processes)
{
    for (auto& [key, monitor] : impl_->sva_monitors) {
        if (std::ranges::find(design_processes, key.first)
            != design_processes.end()) {
            monitor->kill();
        }
    }
}

PackedLogic4 Interpreter::read_debug_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    // A kernel thread member, or a fork child of one, owns its registers.
    const auto kernel_owner = impl_->static_kernel
        ? impl_->static_kernel->debug_owner(process) : std::nullopt;
    const bool kernel_member = kernel_owner.has_value();
    const auto* const kernel_original = kernel_member
        ? impl_->static_kernel->debug_program(*kernel_owner) : nullptr;
    const auto program = kernel_original != nullptr
        ? ProcessProgramView { *kernel_original }
        : impl_->processes.program_view(kernel_owner.value_or(process));
    if (local_index >= program.debug_locals().size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = program.debug_locals()[local_index];
    if (kernel_member) {
        return impl_->static_kernel->debug_register(
            *kernel_owner, local.register_id, local.width);
    }
    auto& state = impl_->get_process(process);
    if (state.executor) {
        return state.executor->read_register(
            local.register_id, local.width);
    }
    const auto& value
        = impl_->ensure_process_frame(state).registers.at(local.register_id);
    if (value.width() != local.width) {
        throw std::logic_error { "SimIR debug local has not been initialized" };
    }
    return value;
}

SystemVerilogScalarValue Interpreter::read_debug_scalar_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    const auto kernel_owner = impl_->static_kernel
        ? impl_->static_kernel->debug_owner(process) : std::nullopt;
    const auto* const kernel_original = kernel_owner
        ? impl_->static_kernel->debug_program(*kernel_owner) : nullptr;
    const auto program = kernel_original != nullptr
        ? ProcessProgramView { *kernel_original }
        : impl_->processes.program_view(kernel_owner.value_or(process));
    if (local_index >= program.debug_locals().size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = program.debug_locals()[local_index];
    if (local.systemverilog_scalar == SystemVerilogScalarKind::None) {
        throw std::logic_error { "SimIR debug local is not a scalar value" };
    }
    const auto packed = read_debug_local(process, local_index);
    const auto decoded = decode_systemverilog_scalar_payload(
        packed, local.systemverilog_scalar);
    if (!decoded) {
        throw std::logic_error { "SimIR scalar debug local is not initialized" };
    }
    return decoded.value;
}

void Interpreter::write_debug_scalar_local(
    const ProcessId process,
    const std::size_t local_index,
    const SystemVerilogScalarValue value)
{
    const auto kernel_owner = impl_->static_kernel
        ? impl_->static_kernel->debug_owner(process) : std::nullopt;
    const auto* const kernel_original = kernel_owner
        ? impl_->static_kernel->debug_program(*kernel_owner) : nullptr;
    const auto program = kernel_original != nullptr
        ? ProcessProgramView { *kernel_original }
        : impl_->processes.program_view(kernel_owner.value_or(process));
    if (local_index >= program.debug_locals().size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = program.debug_locals()[local_index];
    if (value.kind != local.systemverilog_scalar) {
        throw std::invalid_argument { "SimIR scalar debug-local kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded || encoded.value.width() != local.width) {
        throw std::invalid_argument { "invalid SimIR scalar debug-local payload" };
    }
    if (kernel_owner) {
        impl_->static_kernel->debug_write_register(
            *kernel_owner, local.register_id, encoded.value);
        return;
    }
    auto& state = impl_->get_process(process);
    impl_->write_process_register(state, local.register_id, encoded.value);
}

std::string Interpreter::read_debug_string_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    // A kernel thread member, or a fork child of one, owns its registers.
    const auto kernel_owner = impl_->static_kernel
        ? impl_->static_kernel->debug_owner(process) : std::nullopt;
    const bool kernel_member = kernel_owner.has_value();
    const auto* const kernel_original = kernel_member
        ? impl_->static_kernel->debug_program(*kernel_owner) : nullptr;
    const auto program = kernel_original != nullptr
        ? ProcessProgramView { *kernel_original }
        : impl_->processes.program_view(kernel_owner.value_or(process));
    if (local_index >= program.debug_string_locals().size()) {
        throw std::out_of_range { "invalid SimIR string debug-local index" };
    }
    const auto& local = program.debug_string_locals()[local_index];
    if (kernel_member) {
        return impl_->static_kernel->debug_string_register(
            *kernel_owner, local.register_id);
    }
    auto& state = impl_->get_process(process);
    if (state.executor) {
        return state.executor->read_string_register(local.register_id);
    }
    return impl_->ensure_process_frame(state).string_registers.at(
        local.register_id);
}

ContainerValue Interpreter::read_debug_container_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    // A kernel thread member, or a fork child of one, owns its registers.
    const auto kernel_owner = impl_->static_kernel
        ? impl_->static_kernel->debug_owner(process) : std::nullopt;
    const bool kernel_member = kernel_owner.has_value();
    const auto* const kernel_original = kernel_member
        ? impl_->static_kernel->debug_program(*kernel_owner) : nullptr;
    const auto program = kernel_original != nullptr
        ? ProcessProgramView { *kernel_original }
        : impl_->processes.program_view(kernel_owner.value_or(process));
    if (local_index >= program.debug_container_locals().size()) {
        throw std::out_of_range {
            "invalid SimIR container debug-local index"
        };
    }
    const auto& local = program.debug_container_locals()[local_index];
    if (kernel_member) {
        return impl_->static_kernel->debug_container_register(
            *kernel_owner, local.register_id);
    }
    auto& state = impl_->get_process(process);
    if (state.executor) {
        return state.executor->read_container_register(local.register_id);
    }
    (void)impl_->ensure_process_frame(state);
    return impl_->read_container_register(state, local.register_id);
}

bool Interpreter::stopped_by_design() const noexcept
{
    return impl_->stopped_by_design;
}

bool Interpreter::paused_in_final() const noexcept
{
    return impl_->paused_in_final;
}

Scheduler& Interpreter::scheduler() noexcept { return impl_->scheduler; }
const Scheduler& Interpreter::scheduler() const noexcept
{
    return impl_->scheduler;
}

void Interpreter::set_signal_change_hook(SignalChangeHook hook)
{
    impl_->signal_change_hook = std::move(hook);
}

void Interpreter::set_native_signal_observation_required_hook(
    NativeSignalObservationRequiredHook hook)
{
    impl_->native_signal_observation_required_hook = std::move(hook);
}

void Interpreter::set_native_signal_observation_any_hook(
    NativeSignalObservationAnyHook hook)
{
    impl_->native_signal_observation_any_hook = std::move(hook);
}

void Interpreter::set_stored_signal_change_hook(StoredSignalChangeHook hook)
{
    impl_->stored_signal_change_hook = std::move(hook);
}

void Interpreter::set_driver_change_hook(DriverChangeHook hook)
{
    impl_->driver_change_hook = std::move(hook);
}

void Interpreter::set_event_trigger_hook(EventTriggerHook hook)
{
    impl_->event_trigger_hook = std::move(hook);
}

void Interpreter::set_container_object_change_hook(
    ContainerObjectChangeHook hook)
{
    impl_->container_object_change_hook = std::move(hook);
}

void Interpreter::set_container_element_change_hook(
    ContainerElementChangeHook hook)
{
    impl_->container_element_change_hook = std::move(hook);
}

void Interpreter::set_scalar_signal_change_hook(
    ScalarSignalChangeHook hook)
{
    impl_->scalar_signal_change_hook = std::move(hook);
}

void Interpreter::set_execution_point_hook(ExecutionPointHook hook)
{
    impl_->execution_point_hook = std::move(hook);
}

void Interpreter::set_output_hook(OutputHook hook)
{
    impl_->output_hook = std::move(hook);
}

void Interpreter::set_trusted_text_output_hook(OutputHook hook)
{
    impl_->output_hook.set_trusted_text_hook(std::move(hook));
}

void Interpreter::prepare_output_callback_observation()
{
    impl_->prepare_callback_observation();
}

void Interpreter::set_report_hook(ReportHook hook)
{
    impl_->report_hook = std::move(hook);
}

void Interpreter::set_fork_spawn_filter(ForkSpawnFilter filter)
{
    std::shared_ptr<const ForkSpawnFilter> prepared;
    if (filter) {
        prepared = std::make_shared<ForkSpawnFilter>(std::move(filter));
    }
    impl_->fork_spawn_filter = std::move(prepared);
}

} // namespace fsim::runtime::simir
