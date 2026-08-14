// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

Interpreter::Interpreter(
    SchedulerOptions options,
    const std::uint64_t seed)
    : impl_(std::make_unique<Impl>(options, seed))
{
}
Interpreter::~Interpreter() = default;
Interpreter::Interpreter(Interpreter&&) noexcept = default;
Interpreter& Interpreter::operator=(Interpreter&&) noexcept = default;

void Interpreter::set_file_root(std::filesystem::path root)
{
    impl_->set_file_root(std::move(root));
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

void Interpreter::set_class_allocate_hook(ClassAllocateHook hook)
{
    impl_->class_allocate_hook = std::move(hook);
}

void Interpreter::set_coverage_sample_hook(CoverageSampleHook hook)
{
    impl_->coverage_sample_hook = std::move(hook);
}

void Interpreter::set_coverage_query_hook(CoverageQueryHook hook)
{
    impl_->coverage_query_hook = std::move(hook);
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

void Interpreter::set_class_method_call_hook(ClassMethodCallHook hook)
{
    impl_->class_method_call_hook = std::move(hook);
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

void Interpreter::set_class_static_method_call_hook(
    ClassStaticMethodCallHook hook)
{
    impl_->class_static_method_call_hook = std::move(hook);
}

SignalId Interpreter::add_signal(Signal signal)
{
    if (impl_->started) {
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
    impl_->driven_values.push_back(signal.initial_value);
    impl_->driver_values.emplace_back();
    impl_->driver_strengths.emplace_back();
    impl_->forced_driver_values.emplace_back();
    impl_->forced_driver_masks.emplace_back();
    impl_->external_driver_values.emplace_back();
    impl_->charge_decay_handles.emplace_back();
    impl_->charge_values.push_back(
        signal.charge_strength
            ? std::optional<PackedLogic4> { signal.initial_value }
            : std::nullopt);
    impl_->signal_last_values.push_back(signal.initial_value);
    impl_->sampled_values.push_back(signal.initial_value);
    impl_->sampled_defaults.push_back(signal.initial_value);
    impl_->forced_values.emplace_back();
    impl_->forced_masks.emplace_back(
        signal.initial_value.width(), Logic4::zero);
    impl_->signals.push_back(std::move(signal));
    impl_->event_identities.push_back(id);
    impl_->static_fanout.emplace_back();
    impl_->dynamic_fanout.emplace_back();
    impl_->event_states.emplace_back();
    impl_->signal_events.emplace_back();
    impl_->signal_transactions.emplace_back();
    return id;
}

StringObjectId Interpreter::add_string_object(StringObject object)
{
    if (impl_->started) {
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
    if (impl_->started) {
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
    impl_->container_objects.push_back(std::move(object));
    impl_->container_signal_aliases.push_back(std::nullopt);
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
    if (impl_->container_signal_aliases[alias.object]) {
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
    impl_->container_signal_aliases[alias.object] = alias;
}

ProcessId Interpreter::add_process(Process process)
{
    if (impl_->started) {
        throw std::logic_error("cannot add a SimIR process after start");
    }
    const auto id = static_cast<ProcessId>(impl_->processes.size());
    if (static_cast<std::size_t>(id) != impl_->processes.size()) {
        throw std::length_error("too many SimIR processes");
    }
    if (process.id != id) {
        throw std::invalid_argument("SimIR process IDs must be dense and ordered");
    }
    const auto valid_strength = [](const StrengthRank rank) {
        return static_cast<std::underlying_type_t<StrengthRank>>(rank)
            <= static_cast<std::underlying_type_t<StrengthRank>>(
                StrengthRank::supply);
    };
    if (!valid_strength(process.drive_strength.zero)
        || !valid_strength(process.drive_strength.one)) {
        throw std::invalid_argument { "invalid SimIR process drive strength" };
    }
    const bool has_switch_metadata = process.switch_source.has_value()
        || process.switch_target.has_value()
        || process.switch_control.has_value();
    const bool has_switch_region = process.switch_source_offset != 0
        || process.switch_target_offset != 0 || process.switch_width != 0;
    const bool switch_connection = process.switch_bidirectional;
    if ((switch_connection
            && (!process.switch_source || !process.switch_target))
        || (has_switch_region && !has_switch_metadata)
        || (has_switch_metadata
            && (!process.switch_source || !process.switch_target
                || *process.switch_source >= impl_->signals.size()
                || *process.switch_target >= impl_->signals.size()
                || (process.switch_control
                    && *process.switch_control >= impl_->signals.size())))) {
        throw std::invalid_argument {
            "SimIR transmission connection has invalid endpoint metadata"
        };
    }
    if (has_switch_metadata) {
        const auto source_width = impl_->signals[*process.switch_source]
                                      .initial_value.width();
        const auto target_width = impl_->signals[*process.switch_target]
                                      .initial_value.width();
        const auto selected_width = process.switch_width;
        const bool invalid_selected_region = selected_width != 0
            && (process.switch_source_offset > source_width
                || selected_width
                    > source_width - process.switch_source_offset
                || process.switch_target_offset > target_width
                || selected_width
                    > target_width - process.switch_target_offset);
        if (invalid_selected_region
            || (selected_width == 0
                && (process.switch_source_offset != 0
                    || process.switch_target_offset != 0
                    || (source_width != target_width
                        && source_width != 1 && target_width != 1)))
            || (process.switch_control
                && impl_->signals[*process.switch_control]
                        .initial_value.width()
                    != 1
                && impl_->signals[*process.switch_control]
                        .initial_value.width()
                    != (selected_width == 0
                            ? std::max(source_width, target_width)
                            : selected_width))) {
            throw std::invalid_argument {
                "SimIR transmission connection has incompatible endpoint widths"
            };
        }
    }
    if (process.final && process.initialize) {
        throw std::invalid_argument(
            "a SimIR final process cannot initialize at time zero");
    }
    const auto scheduling_regions = static_cast<unsigned>(process.observed)
        + static_cast<unsigned>(process.reactive)
        + static_cast<unsigned>(process.postponed);
    if (scheduling_regions > 1) {
        throw std::invalid_argument(
            "a SimIR process cannot occupy multiple scheduling regions");
    }
    if (!process.register_value_kinds.empty()
        && process.register_value_kinds.size()
            != process.register_count) {
        throw std::invalid_argument(
            "SimIR register value-kind count does not match register_count");
    }
    for (const auto signal : process.static_sensitivity) {
        if (signal.signal >= impl_->signals.size()) {
            throw std::invalid_argument("process sensitivity references invalid signal");
        }
        if (signal.edge != EdgeKind::any
            && signal.edge != EdgeKind::transaction && impl_->signals[signal.signal].initial_value.width() != 1) {
            throw std::invalid_argument(
                "edge sensitivity currently requires a scalar signal");
        }
        impl_->static_fanout[signal.signal].push_back({ id, signal.edge });
    }
    std::set<std::string> local_names;
    for (const auto& local : process.debug_locals) {
        if (local.name.empty()
            || local.register_id >= process.register_count) {
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
    for (const auto& local : process.debug_string_locals) {
        if (local.name.empty()
            || local.register_id >= process.string_register_count) {
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
    if (process.container_register_types.size()
        != process.container_register_count) {
        throw std::invalid_argument {
            "SimIR container register type count does not match register count"
        };
    }
    std::set<std::string> container_local_names;
    for (const auto& local : process.debug_container_locals) {
        if (local.name.empty()
            || local.register_id >= process.container_register_count
            || local.type != process.container_register_types.at(local.register_id)) {
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
    std::map<SignalId, std::vector<Process::DriverRegion>> outputs;
    if (process.driver_regions.empty()) {
        for (const auto& operation : process.operations) {
            const auto signal = output_signal(operation);
            if (signal) {
                outputs[*signal].push_back(
                    Process::DriverRegion { *signal, 0, 0, true });
            }
        }
    } else {
        for (const auto& region : process.driver_regions) {
            outputs[region.signal].push_back(region);
        }
    }
    for (const auto& [signal, regions] : outputs) {
        if (signal >= impl_->signals.size()) {
            throw std::invalid_argument(
                "process output references invalid signal");
        }
        if (!switch_connection) {
            impl_->register_driver(
                id, signal, regions, process.drive_strength);
        }
    }

    Impl::ProcessState state;
    if (impl_->next_process_generation == 0) {
        throw std::overflow_error { "SimIR process generation overflow" };
    }
    state.generation = impl_->next_process_generation++;
    state.frame = std::make_shared<Impl::ProcessFrame>();
    state.frame->registers.assign(
        process.register_count, PackedLogic4 { });
    state.frame->string_registers.assign(
        process.string_register_count, { });
    state.frame->container_registers.reserve(
        process.container_register_count);
    for (const auto& type : process.container_register_types) {
        state.frame->container_registers.push_back(
            default_container_value(type));
    }
    state.random_state = Impl::initial_random_state(
        impl_->root_seed, id);
    state.design_process = id;
    state.waiting_on_static = !process.initialize;
    state.status = process.initialize
        ? ProcessStatus::running
        : ProcessStatus::waiting;
    if (process.program_owner) {
        impl_->program_owners.insert(*process.program_owner);
    }
    state.program = std::move(process);
    impl_->processes.push_back(std::move(state));
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
            || impl_->processes[annotation.process].program.id
                != annotation.process
            || annotation.instruction
                >= impl_->processes[annotation.process]
                    .program.operations.size()) {
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
        const auto& operation
            = process.program.operations[annotation.instruction];
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
                        &process.program.operations[index])) {
                    retained[load->destination] = index;
                } else if (const auto* extract = operation_get_if<Extract>(
                               &process.program.operations[index])) {
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
        auto& operation
            = replacement.process->program.operations[replacement.instruction];
        if (replacement.timing_check) {
            auto* check = operation_get_if<VitalTimingCheck>(&operation);
            std::copy_n(replacement.values.begin(), 4U,
                check->limits.begin());
            if (reset_timing_state) {
                replacement.process->vital_timing_states.erase(
                    replacement.instruction);
            }
            continue;
        }
        for (const auto& definition : replacement.definitions) {
            replacement.process->program.operations[definition.instruction]
                = LoadConstant { definition.target,
                      PackedLogic4::from_aval_bval(
                          64U, definition.value, 0U) };
        }
    }
}

void Interpreter::set_process_executor(
    const ProcessId process,
    std::unique_ptr<ProcessExecutor> executor)
{
    if (impl_->started) {
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
    state.executor = std::move(executor);
}

void Interpreter::start()
{
    if (impl_->started) {
        return;
    }
    impl_->started = true;
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (impl_->processes[id].program.initialize
            && !impl_->processes[id].program.final) {
            impl_->queue_at(id, impl_->scheduler.now());
        }
    }
}

RunResult Interpreter::run(std::optional<SimulationTick> until)
{
    start();
    auto ordinary = impl_->scheduler.run(until);
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
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (impl_->processes[id].program.final) {
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
    };
    if (impl_->finals_ran) {
        impl_->scheduler.request_stop();
        return result;
    }

    impl_->finals_ran = true;
    impl_->scheduler.discard_pending();
    impl_->scheduler.clear_stop();
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (impl_->processes[id].program.final) {
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

void Interpreter::deposit_signal(SignalId signal, PackedLogic4 value)
{
    impl_->commit(signal, std::move(value));
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

void Interpreter::force_signal(SignalId signal, PackedLogic4 value)
{
    const auto width = impl_->get_signal(signal).initial_value.width();
    if (width != value.width()) {
        throw std::invalid_argument("SimIR signal force width mismatch");
    }
    impl_->force_slice(signal, std::move(value), 0);
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

void Interpreter::release_signal(SignalId signal)
{
    const auto width = impl_->get_signal(signal).initial_value.width();
    impl_->release_slice(signal, 0, width);
}

void Interpreter::force_signal_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset)
{
    impl_->force_slice(signal, std::move(value), offset);
}

void Interpreter::release_signal_slice(
    const SignalId signal,
    const std::size_t offset,
    const std::size_t width)
{
    impl_->release_slice(signal, offset, width);
}

bool Interpreter::signal_is_forced(const SignalId signal) const
{
    (void)impl_->get_signal(signal);
    return impl_->forced_values[signal].has_value();
}

void Interpreter::schedule_signal_at(SignalId signal, PackedLogic4 value,
    SimulationTick time, StableOrder order)
{
    // Validate eagerly so a malformed drive does not fail much later.
    if (impl_->get_signal(signal).initial_value.width() != value.width()) {
        throw std::invalid_argument("SimIR signal assignment width mismatch");
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
    return impl_->get_signal(signal).initial_value;
}

const PackedLogic4& Interpreter::stored_signal_value(
    const SignalId signal) const
{
    (void)impl_->get_signal(signal);
    return impl_->driven_values.at(signal);
}

SystemVerilogScalarValue Interpreter::scalar_signal_value(
    const SignalId signal) const
{
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
        result.push_back({ signal, stored.name, scalar_signal_value(signal) });
    }
    return result;
}

const std::string& Interpreter::string_object_value(
    const StringObjectId object) const
{
    return impl_->get_string_object(object).initial_value;
}

void Interpreter::deposit_string_object(
    const StringObjectId object,
    const std::string_view value)
{
    if (value.size() > maximum_string_bytes) {
        throw std::length_error {
            "SimIR string object exceeds byte limit"
        };
    }
    (void)systemverilog_string_length(value);
    impl_->get_string_object(object).initial_value = value;
}

const ContainerValue& Interpreter::container_object_value(
    const ContainerObjectId object) const
{
    return impl_->read_container_object_value(object);
}

void Interpreter::deposit_container_object(
    const ContainerObjectId object,
    ContainerValue value)
{
    impl_->write_container_object_value(object, value);
}

PackedLogic4 Interpreter::driver_value(
    const ProcessId process,
    const SignalId signal) const
{
    (void)impl_->get_process(process);
    return impl_->underlying_driver_value(process, signal);
}

ProcessId Interpreter::design_process(const ProcessId process) const
{
    return impl_->get_process(process).design_process;
}

ProcessId Interpreter::dynamic_process_root(const ProcessId process) const
{
    auto root = process;
    while (const auto parent = impl_->get_process(root).fork_parent) {
        if (!impl_->get_process(*parent).fork_parent) {
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
}

PackedLogic4 Interpreter::read_debug_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_locals.size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = state.program.debug_locals[local_index];
    if (state.executor) {
        return state.executor->read_register(
            local.register_id, local.width);
    }
    const auto& value = state.frame->registers.at(local.register_id);
    if (value.width() != local.width) {
        throw std::logic_error { "SimIR debug local has not been initialized" };
    }
    return value;
}

SystemVerilogScalarValue Interpreter::read_debug_scalar_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_locals.size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = state.program.debug_locals[local_index];
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
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_locals.size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = state.program.debug_locals[local_index];
    if (value.kind != local.systemverilog_scalar) {
        throw std::invalid_argument { "SimIR scalar debug-local kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded || encoded.value.width() != local.width) {
        throw std::invalid_argument { "invalid SimIR scalar debug-local payload" };
    }
    impl_->write_process_register(state, local.register_id, encoded.value);
}

std::string Interpreter::read_debug_string_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_string_locals.size()) {
        throw std::out_of_range { "invalid SimIR string debug-local index" };
    }
    const auto& local = state.program.debug_string_locals[local_index];
    if (state.executor) {
        return state.executor->read_string_register(local.register_id);
    }
    return state.frame->string_registers.at(local.register_id);
}

ContainerValue Interpreter::read_debug_container_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_container_locals.size()) {
        throw std::out_of_range {
            "invalid SimIR container debug-local index"
        };
    }
    const auto& local = state.program.debug_container_locals[local_index];
    if (state.executor) {
        return state.executor->read_container_register(local.register_id);
    }
    return state.frame->container_registers.at(local.register_id);
}

bool Interpreter::stopped_by_design() const noexcept
{
    return impl_->stopped_by_design;
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

void Interpreter::set_report_hook(ReportHook hook)
{
    impl_->report_hook = std::move(hook);
}

void Interpreter::set_fork_spawn_filter(ForkSpawnFilter filter)
{
    impl_->fork_spawn_filter = std::move(filter);
}

} // namespace fsim::runtime::simir
