// SPDX-License-Identifier: Apache-2.0
namespace {

[[nodiscard]] bool is_class_execution_boundary(const Operation& operation)
{
    return operation_holds<ClassAllocate>(operation)
        || operation_holds<ClassPropertyRead>(operation)
        || operation_holds<ClassPropertyWrite>(operation)
        || operation_holds<ClassMethodCall>(operation)
        || operation_holds<ClassStaticPropertyRead>(operation)
        || operation_holds<ClassStaticPropertyWrite>(operation)
        || operation_holds<ClassStaticMethodCall>(operation)
        || operation_holds<CoverageSample>(operation)
        || operation_holds<CoverageQuery>(operation)
        || operation_holds<PlusArgSelect>(operation);
}

[[nodiscard]] bool is_immediate_process_boundary(const Operation& operation)
{
    const auto* read = operation_get_if<ReadSignal>(&operation);
    return (read && read->kind != SignalReadKind::current)
        || operation_holds<ProcessSelf>(operation)
        || operation_holds<ProcessStatusQuery>(operation)
        || operation_holds<ProcessCompleted>(operation)
        || operation_holds<ProcessResume>(operation)
        || operation_holds<ProcessGetRandState>(operation)
        || operation_holds<ProcessSetRandState>(operation)
        || operation_holds<ProcessSrandom>(operation)
        || operation_holds<SystemCommand>(operation)
        || operation_holds<VcdControl>(operation)
        || operation_holds<CoverageDatabaseControl>(operation)
        || operation_holds<StochasticQueueOperation>(operation)
        || operation_holds<PlaEvaluate>(operation)
        || operation_holds<TimeFormatControl>(operation)
        || operation_holds<EventTriggered>(operation)
        || operation_holds<EventAlias>(operation)
        || operation_holds<DisableBlock>(operation);
}

[[nodiscard]] bool is_synchronization_boundary(const Operation& operation)
{
    return operation_holds<MailboxCreate>(operation)
        || operation_holds<MailboxPut>(operation)
        || operation_holds<MailboxGet>(operation)
        || operation_holds<MailboxNum>(operation)
        || operation_holds<SemaphoreCreate>(operation)
        || operation_holds<SemaphoreGet>(operation)
        || operation_holds<SemaphorePut>(operation);
}

[[nodiscard]] bool is_dynamic_callable_boundary(const Operation& operation)
{
    const auto* call = operation_get_if<Call>(&operation);
    const auto* return_operation = operation_get_if<Return>(&operation);
    return (call && call->stack.capacity == 0)
        || (return_operation && return_operation->stack.capacity == 0)
        || operation_holds<CallableFramePush>(operation)
        || operation_holds<CallableFramePop>(operation);
}

} // namespace

void Interpreter::Impl::handle_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex next_instruction)
{
    if (instruction >= process.program.operations.size()) {
        process.pc = instruction;
        fail(process, "executor returned an invalid boundary instruction");
    }

    const auto& operation = process.program.operations[instruction];
    const auto* dynamic_call = fsim::runtime::simir::operation_get_if<Call>(&operation);
    const auto* dynamic_return = fsim::runtime::simir::operation_get_if<Return>(&operation);
    const auto* frame_push = fsim::runtime::simir::operation_get_if<CallableFramePush>(&operation);
    const auto* frame_pop = fsim::runtime::simir::operation_get_if<CallableFramePop>(&operation);
    const bool callable_boundary = (dynamic_call && dynamic_call->stack.capacity == 0)
        || (dynamic_return && dynamic_return->stack.capacity == 0)
        || frame_push || frame_pop;
    const auto expected_next = callable_boundary
        ? instruction
        : instruction + 1;
    if (instruction == std::numeric_limits<InstructionIndex>::max()
        || next_instruction != expected_next) {
        process.pc = instruction;
        fail(
            process,
            "executor returned a non-sequential boundary resume instruction");
    }

    process.pc = callable_boundary ? instruction : next_instruction;
    if (dynamic_call && dynamic_call->stack.capacity == 0) {
        execute_dynamic_call(process, *dynamic_call);
        return;
    }
    if (dynamic_return && dynamic_return->stack.capacity == 0) {
        execute_dynamic_return(process, *dynamic_return);
        return;
    }
    if (frame_push) {
        push_callable_frame(process, *frame_push);
        return;
    }
    if (frame_pop) {
        pop_callable_frame(process, *frame_pop);
        return;
    }
    if (const auto* read = operation_get_if<ReadSignal>(&operation);
        read && read->kind != SignalReadKind::current) {
        execute_sampled_read(process, *read);
        return;
    }
    if (const auto* sample
        = fsim::runtime::simir::operation_get_if<CoverageSample>(&operation)) {
        if (!coverage_sample_hook) {
            process.pc = instruction;
            fail(process, "coverage sampling service is unavailable");
        }
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "coverage sampling boundary requires an executor");
        }
        std::vector<PackedLogic4> actuals;
        actuals.reserve(sample->actuals.size());
        for (std::size_t index = 0; index < sample->actuals.size(); ++index) {
            actuals.push_back(process.executor->read_register(
                sample->actuals[index], sample->actual_widths[index]));
        }
        coverage_sample_hook(
            sample->instance_identity, actuals, sample->signed_actuals,
            sample->trigger);
        return;
    }
    if (const auto* query
        = fsim::runtime::simir::operation_get_if<CoverageQuery>(&operation)) {
        if (query->kind != CoverageQueryKind::overall_type
            && query->kind != CoverageQueryKind::overall_instance) {
            process.pc = instruction;
            fail(process, "coverage query kind is invalid");
        }
        if (!coverage_query_hook) {
            process.pc = instruction;
            fail(process, "coverage query service is unavailable");
        }
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "coverage query boundary requires an executor");
        }
        auto value = coverage_query_hook(query->kind);
        if (value.width() != 64U || value.is_logic9()) {
            process.pc = instruction;
            fail(process, "coverage query service returned an invalid real payload");
        }
        process.executor->write_register(query->destination, value);
        return;
    }
    if (const auto* query
        = fsim::runtime::simir::operation_get_if<PlusArgSelect>(&operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "plusarg boundary requires an executor");
        }
        const auto prefix = process.executor->read_string_register(
            query->query);
        const std::string* selected = nullptr;
        for (const auto& argument : plusargs) {
            auto candidate = std::string_view { argument };
            if (candidate.starts_with('+')) {
                candidate.remove_prefix(1);
            }
            if (candidate.starts_with(prefix)) {
                selected = &argument;
                break;
            }
        }
        if (query->selected) {
            auto selected_text = std::string_view { };
            if (selected != nullptr) {
                selected_text = *selected;
                if (selected_text.starts_with('+')) {
                    selected_text.remove_prefix(1);
                }
            }
            process.executor->write_string_register(
                *query->selected, selected_text);
        }
        write_process_register(
            process,
            query->destination,
            PackedLogic4::from_aval_bval(
                32, selected != nullptr ? 1U : 0U, 0));
        return;
    }
    if (const auto* command
        = fsim::runtime::simir::operation_get_if<SystemCommand>(&operation)) {
        if (!system_command_hook) {
            process.pc = instruction;
            fail(process, "$system service is unavailable");
        }
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "$system boundary requires an executor");
        }
        std::optional<std::string> owned_text;
        if (command->command) {
            owned_text = process.executor->read_string_register(
                *command->command);
        }
        const auto status = system_command_hook(
            owned_text
                ? std::optional<std::string_view> { *owned_text }
                : std::nullopt);
        if (command->destination) {
            process.executor->write_register(
                *command->destination,
                PackedLogic4::from_aval_bval(
                    32, static_cast<std::uint32_t>(status), 0));
        }
        return;
    }
    if (const auto* control
        = fsim::runtime::simir::operation_get_if<VcdControl>(&operation)) {
        if (!vcd_control_hook || !process.executor) {
            process.pc = instruction;
            fail(process, "VCD control service is unavailable");
        }
        VcdControlEvent event;
        event.kind = control->kind;
        if (control->filename) {
            event.filename = process.executor->read_string_register(
                *control->filename);
        }
        if (control->value) {
            const auto converted = process.executor
                                       ->read_register(*control->value, 64U)
                                       .known_unsigned_value();
            if (!converted) {
                process.pc = instruction;
                fail(process,
                    "VCD control value must be a known unsigned 64-bit integer");
            }
            event.value = *converted;
        }
        event.selections = control->selections;
        event.scope = control->scope;
        event.time = scheduler.now();
        event.delta = scheduler.delta();
        vcd_control_hook(event);
        if (control->kind == VcdControlKind::variables
            || control->kind == VcdControlKind::ports) {
            const auto begin_kind
                = control->kind == VcdControlKind::variables
                ? VcdControlKind::begin_variables
                : VcdControlKind::begin_ports;
            scheduler.schedule(
                SchedulerPhase::postponed,
                process.program.id,
                [this, begin_kind](Scheduler& runtime) {
                    if (!vcd_control_hook) {
                        return;
                    }
                    VcdControlEvent begin;
                    begin.kind = begin_kind;
                    begin.time = runtime.now();
                    begin.delta = runtime.delta();
                    vcd_control_hook(begin);
                });
        }
        return;
    }
    if (const auto* control
        = fsim::runtime::simir::operation_get_if<CoverageDatabaseControl>(
            &operation)) {
        if (!coverage_database_control_hook || !process.executor) {
            process.pc = instruction;
            fail(process, "coverage database service is unavailable");
        }
        coverage_database_control_hook({ control->kind,
            process.executor->read_string_register(control->filename) });
        return;
    }
    if (const auto* queue
        = fsim::runtime::simir::operation_get_if<StochasticQueueOperation>(
            &operation)) {
        execute_stochastic_queue(process, *queue);
        return;
    }
    if (const auto* pla
        = fsim::runtime::simir::operation_get_if<PlaEvaluate>(&operation)) {
        execute_pla(process, *pla);
        return;
    }
    if (const auto* format
        = fsim::runtime::simir::operation_get_if<TimeFormatControl>(
            &operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "$timeformat boundary requires an executor");
        }
        const auto decode = [&](const RegisterId id, const char* name) {
            const auto value = process.executor->read_register(id, 32);
            const auto word = value.low_word();
            if (word.bval != 0) {
                process.pc = instruction;
                fail(
                    process,
                    std::string { "$timeformat " } + name
                        + " must be a known 32-bit value");
            }
            return static_cast<std::uint32_t>(word.aval);
        };
        const auto units = static_cast<std::int32_t>(
            decode(format->units, "units"));
        const auto precision = decode(format->precision, "precision");
        const auto minimum_width = decode(
            format->minimum_width, "minimum width");
        const auto suffix = process.executor->read_string_register(
            format->suffix);
        if (units < -15 || units > 0
            || precision > maximum_string_bytes
            || minimum_width > maximum_string_bytes
            || suffix.size() > maximum_string_bytes) {
            process.pc = instruction;
            fail(
                process,
                "$timeformat arguments exceed their supported IEEE profile");
        }
        time_format.units = units;
        time_format.precision = precision;
        time_format.suffix = suffix;
        time_format.minimum_width = minimum_width;
        return;
    }
    if (const auto* point = fsim::runtime::simir::operation_get_if<DebugPoint>(&operation)) {
        clear_wait_timeout(process);
        process.current_source = point->source;
        process.current_scope = point->scope;
        auto kind = ExecutionPointKind::statement;
        switch (point->kind) {
        case DebugPointKind::statement:
            kind = ExecutionPointKind::statement;
            break;
        case DebugPointKind::call:
            kind = ExecutionPointKind::call;
            break;
        case DebugPointKind::wait:
            kind = ExecutionPointKind::wait;
            break;
        case DebugPointKind::assertion:
            kind = ExecutionPointKind::assertion;
            break;
        case DebugPointKind::process_entry:
            kind = ExecutionPointKind::process_entry;
            break;
        }
        notify_execution_point(
            process, instruction, kind, process.current_source,
            process.current_scope);
        if (scheduler.stop_requested()) {
            queue_current(process.program.id);
        }
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitRegion>(&operation)) {
        clear_wait_timeout(process);
        const auto current = scheduler.current_phase();
        if (!current || wait->phase <= *current) {
            process.pc = instruction;
            fail(process, "WaitRegion must target a later scheduler region");
        }
        process.status = ProcessStatus::waiting;
        process.queued = true;
        scheduler.schedule(
            wait->phase,
            process.program.id,
            [this, id = process.program.id](Scheduler&) {
                auto& state = get_process(id);
                state.queued = false;
                execute(id);
            });
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitFor>(&operation)) {
        clear_wait_timeout(process);
        auto delay = wait->delay;
        if (wait->source) {
            try {
                const auto payload = process.executor
                    ? process.executor->read_register(
                          *wait->source, wait->source_width)
                    : get_register(process, *wait->source);
                delay = normalized_dynamic_wait_delay(*wait, payload);
            } catch (const std::exception& error) {
                process.pc = instruction;
                fail(process, error.what());
            }
        }
        if (delay == 0) {
            process.status = ProcessStatus::waiting;
            if (process.program.postponed) {
                queue_next_delta(process.program.id);
            } else {
                process.queued = true;
                scheduler.schedule(
                    SchedulerPhase::inactive,
                    process.program.id,
                    [this, id = process.program.id](Scheduler&) {
                        auto& state = get_process(id);
                        state.queued = false;
                        execute(id);
                    });
            }
            notify_execution_point(
                process, instruction, ExecutionPointKind::process_suspend,
                process.current_source);
            return;
        }
        if (delay
            > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
            process.pc = instruction;
            fail(process, "simulation time overflow in WaitFor");
        }
        queue_at(process.program.id, scheduler.now() + delay);
        process.status = ProcessStatus::waiting;
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitOn>(&operation)) {
        if (wait->signals.empty() && !wait->timeout) {
            process.pc = instruction;
            fail(
                process,
                "WaitOn requires at least one signal or a timeout");
        }
        if (!wait->edges.empty()
            && wait->edges.size() != wait->signals.size()) {
            process.pc = instruction;
            fail(process, "WaitOn edge count must match its signal count");
        }
        if (!wait->timeout
            && (wait->timeout_result
                || wait->timeout_origin)) {
            process.pc = instruction;
            fail(
                process,
                "WaitOn timeout metadata requires a timeout");
        }
        if (wait->timeout_origin
            && !wait->timeout_result) {
            process.pc = instruction;
            fail(
                process,
                "WaitOn timeout rearm requires a result register");
        }
        if (wait->timeout_origin) {
            if (*wait->timeout_origin >= instruction) {
                process.pc = instruction;
                fail(
                    process,
                    "WaitOn timeout origin must precede its rearm");
            }
            const auto* origin = fsim::runtime::simir::operation_get_if<WaitOn>(
                &process.program.operations[*wait->timeout_origin]);
            if (origin == nullptr
                || !origin->timeout
                || origin->timeout_origin
                || origin->timeout != wait->timeout
                || origin->timeout_result
                    != wait->timeout_result
                || origin->signals != wait->signals
                || origin->edges != wait->edges) {
                process.pc = instruction;
                fail(
                    process,
                    "WaitOn timeout rearm does not match its origin");
            }
        }
        process.waiting_on_signal = true;
        process.status = ProcessStatus::waiting;
        process.dynamic_sensitivity.clear();
        process.dynamic_sensitivity.reserve(wait->signals.size());
        for (std::size_t index = 0; index < wait->signals.size(); ++index) {
            const auto source_signal = wait->signals[index];
            const auto& source = get_signal(source_signal);
            const auto identity = source.event_variable
                ? event_identities.at(source_signal)
                : std::optional<SignalId> { source_signal };
            const auto edge = wait->edges.empty() ? EdgeKind::any : wait->edges[index];
            switch (edge) {
            case EdgeKind::any:
                break;
            case EdgeKind::posedge:
            case EdgeKind::negedge:
                if (source.initial_value.width() != 1) {
                    process.pc = instruction;
                    fail(process, "WaitOn edge requires a scalar signal");
                }
                break;
            default:
                process.pc = instruction;
                fail(process, "WaitOn has an invalid edge kind");
            }
            if (identity) {
                process.dynamic_sensitivity.push_back({ *identity, edge });
            }
        }
        std::sort(
            process.dynamic_sensitivity.begin(),
            process.dynamic_sensitivity.end(),
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal
                    || (lhs.signal == rhs.signal
                        && lhs.edge < rhs.edge);
            });
        process.dynamic_sensitivity.erase(
            std::unique(
                process.dynamic_sensitivity.begin(),
                process.dynamic_sensitivity.end(),
                [](const Sensitivity& lhs, const Sensitivity& rhs) {
                    return lhs.signal == rhs.signal
                        && lhs.edge == rhs.edge;
                }),
            process.dynamic_sensitivity.end());
        for (const auto sensitivity : process.dynamic_sensitivity) {
            dynamic_fanout[sensitivity.signal].push_back(
                { process.program.id, sensitivity.edge });
        }
        if (wait->timeout) {
            if (wait->timeout_origin) {
                rearm_wait_timeout(
                    process,
                    instruction,
                    *wait->timeout_origin,
                    wait->timeout_result);
            } else {
                begin_wait_timeout(
                    process,
                    instruction,
                    *wait->timeout,
                    wait->timeout_result);
            }
        } else {
            clear_wait_timeout(process);
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitPla>(
            &operation)) {
        clear_wait_timeout(process);
        (void)get_container_object(wait->memory);
        process.waiting_on_signal = true;
        process.waiting_on_container = wait->memory;
        process.status = ProcessStatus::waiting;
        process.dynamic_sensitivity.clear();
        process.dynamic_sensitivity.reserve(wait->signals.size());
        for (const auto signal : wait->signals) {
            (void)get_signal(signal);
            process.dynamic_sensitivity.push_back(
                { signal, EdgeKind::any });
        }
        std::ranges::sort(
            process.dynamic_sensitivity,
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal;
            });
        process.dynamic_sensitivity.erase(
            std::ranges::unique(process.dynamic_sensitivity).begin(),
            process.dynamic_sensitivity.end());
        for (const auto sensitivity : process.dynamic_sensitivity) {
            dynamic_fanout[sensitivity.signal].push_back(
                { process.program.id, sensitivity.edge });
        }
        auto& memory_waiters = container_dynamic_fanout.at(wait->memory);
        if (std::ranges::find(memory_waiters, process.program.id)
            == memory_waiters.end()) {
            memory_waiters.push_back(process.program.id);
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitOrder>(
            &operation)) {
        if (wait->events.empty()) {
            process.pc = instruction;
            fail(process, "WaitOrder requires at least one event");
        }
        clear_wait_timeout(process);
        process.waiting_on_signal = true;
        process.status = ProcessStatus::waiting;
        process.wait_order_events.clear();
        process.wait_order_events.reserve(wait->events.size());
        process.wait_order_index = 0;
        process.wait_order_result = wait->result;
        process.dynamic_sensitivity.clear();
        process.dynamic_sensitivity.reserve(wait->events.size());
        for (const auto event : wait->events) {
            const auto& signal = get_signal(event);
            if (!signal.event_variable) {
                process.pc = instruction;
                fail(process, "WaitOrder requires named-event variables");
            }
            const auto identity = event_identities.at(event);
            process.wait_order_events.push_back(identity);
            if (identity) {
                process.dynamic_sensitivity.push_back(
                    { *identity, EdgeKind::any });
            }
        }
        std::ranges::sort(
            process.dynamic_sensitivity,
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal;
            });
        process.dynamic_sensitivity.erase(
            std::ranges::unique(process.dynamic_sensitivity).begin(),
            process.dynamic_sensitivity.end());
        for (const auto sensitivity : process.dynamic_sensitivity) {
            dynamic_fanout[sensitivity.signal].push_back(
                { process.program.id, sensitivity.edge });
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<WaitSensitivity>(operation)) {
        clear_wait_timeout(process);
        if (process.program.static_sensitivity.empty()) {
            process.pc = instruction;
            fail(process, "WaitSensitivity requires a static sensitivity list");
        }
        process.waiting_on_static = true;
        process.status = ProcessStatus::waiting;
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<WaitForever>(operation)) {
        clear_wait_timeout(process);
        process.status = ProcessStatus::waiting;
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<Yield>(operation)) {
        clear_wait_timeout(process);
        process.status = ProcessStatus::waiting;
        queue_next_delta(process.program.id);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    const auto read_boundary_value = [&](const RegisterId id) {
        return process.executor->read_register(
            id, ProcessExecutor::native_register_width);
    };
    const auto read_boundary_handle = [&](const RegisterId id) {
        return process.executor->read_register(id, 64U);
    };
    if (const auto* class_allocate = fsim::runtime::simir::operation_get_if<ClassAllocate>(&operation)) {
        if (!class_allocate_hook) {
            fail(process, "class allocation service is unavailable");
        }
        std::vector<PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        actuals.reserve(class_allocate->constructor_actuals.size());
        string_actuals.reserve(class_allocate->constructor_actuals.size());
        for (std::size_t index = 0;
            index < class_allocate->constructor_actuals.size(); ++index) {
            const auto actual = class_allocate->constructor_actuals[index];
            const auto string_actual = !class_allocate->constructor_actual_kinds.empty()
                && class_allocate->constructor_actual_kinds[index] == 1U;
            actuals.push_back(
                string_actual ? PackedLogic4(64) : read_boundary_value(actual));
            string_actuals.push_back(
                string_actual
                    ? process.executor->read_string_register(actual)
                    : std::string { });
        }
        const auto handle = class_allocate_hook(
            process.program.name,
            class_allocate->specialization_identity,
            class_allocate->declared_type,
            actuals,
            string_actuals,
            class_allocate->constructor_actual_names);
        write_process_register(
            process,
            class_allocate->destination,
            PackedLogic4::from_aval_bval(64, handle, 0));
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassPropertyRead>(
            &operation)) {
        if (!class_property_read_hook) {
            fail(process, "class property service is unavailable");
        }
        const auto handle = read_boundary_handle(
            property->receiver)
                                .low_word()
                                .aval;
        write_process_register(
            process,
            property->destination,
            resize_class_value(
                class_property_read_hook(handle, property->property_identity),
                property->width));
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassPropertyWrite>(
            &operation)) {
        if (!class_property_write_hook) {
            fail(process, "class property service is unavailable");
        }
        class_property_write_hook(
            read_boundary_handle(property->receiver).low_word().aval,
            property->property_identity,
            read_boundary_value(property->source));
        return;
    }
    if (const auto* method = fsim::runtime::simir::operation_get_if<ClassMethodCall>(
            &operation)) {
        if (!class_method_call_hook) {
            fail(process, "class method service is unavailable");
        }
        std::vector<PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        actuals.reserve(method->actuals.size());
        string_actuals.reserve(method->actuals.size());
        for (std::size_t index = 0; index < method->actuals.size(); ++index) {
            const auto actual = method->actuals[index];
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            actuals.push_back(
                string_actual ? PackedLogic4(64) : read_boundary_value(actual));
            string_actuals.push_back(
                string_actual
                    ? process.executor->read_string_register(actual)
                    : std::string { });
        }
        const auto handle = read_boundary_handle(
            method->receiver)
                                .low_word()
                                .aval;
        write_process_register(
            process,
            method->destination,
            resize_class_value(
                class_method_call_hook(
                    handle,
                    method->method_identity,
                    actuals,
                    string_actuals,
                    method->actual_names,
                    method->actual_directions,
                    method->inline_constraints,
                    method->virtual_dispatch),
                method->result_width));
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            if (string_actual) {
                process.executor->write_string_register(
                    method->actuals[index], string_actuals[index]);
            } else {
                write_process_register(process, method->actuals[index], actuals[index]);
            }
        }
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassStaticPropertyRead>(
            &operation)) {
        if (!class_static_property_read_hook) {
            fail(process, "class static property service is unavailable");
        }
        write_process_register(
            process,
            property->destination,
            resize_class_value(
                class_static_property_read_hook(property->property_identity),
                property->width));
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassStaticPropertyWrite>(
            &operation)) {
        if (!class_static_property_write_hook) {
            fail(process, "class static property service is unavailable");
        }
        class_static_property_write_hook(
            property->property_identity,
            read_boundary_value(property->source));
        return;
    }
    if (const auto* method = fsim::runtime::simir::operation_get_if<ClassStaticMethodCall>(
            &operation)) {
        if (!class_static_method_call_hook) {
            fail(process, "class static method service is unavailable");
        }
        std::vector<PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        actuals.reserve(method->actuals.size());
        string_actuals.reserve(method->actuals.size());
        for (std::size_t index = 0; index < method->actuals.size(); ++index) {
            const auto actual = method->actuals[index];
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            actuals.push_back(
                string_actual ? PackedLogic4(64) : read_boundary_value(actual));
            string_actuals.push_back(
                string_actual
                    ? process.executor->read_string_register(actual)
                    : std::string { });
        }
        write_process_register(
            process,
            method->destination,
            resize_class_value(
                class_static_method_call_hook(
                    method->method_identity,
                    actuals,
                    string_actuals,
                    method->actual_names,
                    method->actual_directions),
                method->result_width));
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            if (string_actual) {
                process.executor->write_string_register(
                    method->actuals[index], string_actuals[index]);
            } else {
                write_process_register(process, method->actuals[index], actuals[index]);
            }
        }
        return;
    }
    if (handle_synchronization_boundary(process, instruction, operation)) {
        return;
    }
    if (handle_process_boundary(process, instruction, operation)) {
        return;
    }
    if (handle_fork_boundary(process, instruction, operation)) {
        return;
    }
    if (fsim::runtime::simir::operation_holds<Pause>(operation)) {
        clear_wait_timeout(process);
        scheduler.request_stop();
        queue_current(process.program.id);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<Stop>(operation)) {
        clear_wait_timeout(process);
        process.halted = true;
        stopped_by_design = true;
        scheduler.request_stop();
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<Halt>(operation)) {
        const auto halt = *fsim::runtime::simir::operation_get_if<Halt>(
            &operation);
        clear_wait_timeout(process);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        if (halt.program_exit) {
            exit_program(process);
        } else if (process.fork_parent) {
            complete_fork_child(process);
        } else {
            complete_process(process, ProcessStatus::finished);
            complete_program_process(process);
        }
        return;
    }

    process.pc = instruction;
    fail(
        process,
        "executor returned at an operation that is not a kernel boundary");
}
