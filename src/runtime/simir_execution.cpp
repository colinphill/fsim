// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scope_randomize.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_execution_context.hpp"
#include "simir_internal.hpp"

#include "simir_signal_attributes.hpp"
#include <bit>
#include <cmath>

namespace fsim::runtime::simir {

namespace {

    [[nodiscard]] PackedLogic4 resize_class_value(
        const PackedLogic4& value,
        const std::size_t width)
    {
        PackedLogic4 result(width, Logic4::zero);
        for (std::size_t bit = 0; bit < std::min(width, value.width()); ++bit) {
            result.set(bit, value.get(bit));
        }
        return value.is_logic9() ? result.promoted_to_logic9() : result;
    }

    [[nodiscard]] SimulationTick normalized_dynamic_wait_delay(
        const WaitFor& wait,
        const PackedLogic4& payload)
    {
        if (!wait.source || wait.source_width == 0
            || wait.source_width > 64
            || payload.width() != wait.source_width
            || wait.rounding_quantum == 0) {
            throw std::invalid_argument {
                "runtime WaitFor has invalid dynamic-delay metadata"
            };
        }
        if (wait.source_kind == SystemVerilogScalarKind::ShortReal
            || wait.source_kind == SystemVerilogScalarKind::Real
            || wait.source_kind == SystemVerilogScalarKind::Realtime) {
            const auto decoded = decode_systemverilog_scalar_payload(
                payload, wait.source_kind);
            const auto number = decoded ? decoded.value.as_real() : std::nullopt;
            if (!number || !std::isfinite(*number)) {
                throw std::invalid_argument {
                    "runtime WaitFor requires a finite real delay"
                };
            }
            if (*number < 0.0) {
                throw std::invalid_argument {
                    "runtime WaitFor delay cannot be negative"
                };
            }
            const auto scaled_quanta = static_cast<long double>(*number)
                * static_cast<long double>(wait.delay)
                / static_cast<long double>(wait.rounding_quantum);
            if (!std::isfinite(scaled_quanta)) {
                throw std::overflow_error {
                    "runtime WaitFor delay overflows simulation ticks"
                };
            }
            const auto rounded_quanta = std::round(scaled_quanta);
            const auto maximum_quanta = static_cast<long double>(
                                            std::numeric_limits<SimulationTick>::max())
                / static_cast<long double>(wait.rounding_quantum);
            if (rounded_quanta > maximum_quanta) {
                throw std::overflow_error {
                    "runtime WaitFor delay overflows simulation ticks"
                };
            }
            return static_cast<SimulationTick>(rounded_quanta)
                * wait.rounding_quantum;
        }

        std::uint64_t magnitude = 0;
        if (wait.source_kind == SystemVerilogScalarKind::Time) {
            const auto decoded = decode_systemverilog_scalar_payload(
                payload, wait.source_kind);
            const auto ticks = decoded ? decoded.value.as_time() : std::nullopt;
            if (!ticks) {
                throw std::invalid_argument {
                    "runtime WaitFor requires a known time delay"
                };
            }
            magnitude = *ticks;
        } else if (wait.source_kind == SystemVerilogScalarKind::None) {
            const auto word = payload.low_word();
            if (word.bval != 0) {
                throw std::invalid_argument {
                    "runtime WaitFor requires a known integral delay"
                };
            }
            if (wait.source_signed
                && payload.width() != 0
                && ((word.aval >> (payload.width() - 1U)) & 1U) != 0) {
                throw std::invalid_argument {
                    "runtime WaitFor delay cannot be negative"
                };
            }
            magnitude = word.aval;
        } else {
            throw std::invalid_argument {
                "runtime WaitFor has an invalid delay value kind"
            };
        }
        if (magnitude != 0
            && wait.delay
                > std::numeric_limits<SimulationTick>::max() / magnitude) {
            throw std::overflow_error {
                "runtime WaitFor delay overflows simulation ticks"
            };
        }
        return magnitude * wait.delay;
    }

} // namespace

void Interpreter::Impl::execute_stochastic_queue(
    ProcessState& process,
    const StochasticQueueOperation& operation)
{
    const auto read_i32 = [&](const RegisterId id,
                              const std::string_view role) {
        const auto value = process.executor
            ? process.executor->read_register(id, 32U)
            : get_register(process, id);
        const auto word = value.low_word();
        if (value.width() != 32U || word.bval != 0) {
            fail(
                process,
                std::string { "stochastic queue " } + std::string { role }
                    + " must be a known 32-bit integer");
        }
        return std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(word.aval));
    };
    const auto write_i32 = [&](const RegisterId id, const std::int32_t value) {
        write_process_register(
            process,
            id,
            PackedLogic4::from_aval_bval(
                32U, std::bit_cast<std::uint32_t>(value), 0));
    };
    const auto write_status = [&](const std::int32_t value) {
        write_i32(operation.status, value);
    };
    const auto required = [&](const std::optional<RegisterId> value,
                              const std::string_view role) {
        if (!value) {
            fail(
                process,
                std::string { "stochastic queue operation is missing " }
                    + std::string { role });
        }
        return *value;
    };
    const auto saturating_add = [](const SimulationTick lhs,
                                    const SimulationTick rhs) {
        return rhs > std::numeric_limits<SimulationTick>::max() - lhs
            ? std::numeric_limits<SimulationTick>::max()
            : lhs + rhs;
    };
    const auto queue_id = read_i32(operation.queue_id, "queue ID");

    if (operation.kind == StochasticQueueKind::initialize) {
        const auto queue_type = read_i32(
            required(operation.queue_type, "queue type"), "queue type");
        const auto maximum_length = read_i32(
            required(operation.maximum_length, "maximum length"),
            "maximum length");
        if (queue_type != 1 && queue_type != 2) {
            write_status(4);
            return;
        }
        if (maximum_length <= 0) {
            write_status(5);
            return;
        }
        if (stochastic_queues.contains(queue_id)) {
            write_status(6);
            return;
        }
        if (stochastic_queues.size()
            >= maximum_container_storage_bytes
                / sizeof(StochasticQueueState)) {
            write_status(7);
            return;
        }
        try {
            StochasticQueueState state;
            state.lifo = queue_type == 2;
            state.maximum_length
                = static_cast<std::uint32_t>(maximum_length);
            stochastic_queues.emplace(queue_id, std::move(state));
        } catch (const std::bad_alloc&) {
            write_status(7);
            return;
        }
        write_status(0);
        return;
    }

    const auto found = stochastic_queues.find(queue_id);
    if (found == stochastic_queues.end()) {
        if (operation.job_id)
            write_i32(*operation.job_id, 0);
        if (operation.information_id)
            write_i32(*operation.information_id, 0);
        if (operation.statistic_value)
            write_i32(*operation.statistic_value, 0);
        if (operation.result)
            write_i32(*operation.result, 0);
        write_status(2);
        return;
    }
    auto& queue = found->second;

    if (operation.kind == StochasticQueueKind::add) {
        if (queue.entries.size() >= queue.maximum_length) {
            write_status(1);
            return;
        }
        const auto job = read_i32(
            required(operation.job_id, "job ID"), "job ID");
        const auto information = read_i32(
            required(operation.information_id, "information ID"),
            "information ID");
        const auto maximum_entries = maximum_container_storage_bytes
            / sizeof(StochasticQueueEntry);
        std::size_t stored_entries { };
        for (const auto& [id, state] : stochastic_queues) {
            (void)id;
            if (state.entries.size() > maximum_entries - stored_entries) {
                write_status(7);
                return;
            }
            stored_entries += state.entries.size();
        }
        if (stored_entries >= maximum_entries) {
            write_status(7);
            return;
        }
        try {
            queue.entries.push_back(
                { job, information, scheduler.now() });
        } catch (const std::bad_alloc&) {
            write_status(7);
            return;
        }
        if (queue.last_arrival) {
            queue.total_interarrival = saturating_add(
                queue.total_interarrival,
                scheduler.now() - *queue.last_arrival);
        }
        queue.last_arrival = scheduler.now();
        ++queue.arrivals;
        queue.maximum_occupancy = std::max<std::uint64_t>(
            queue.maximum_occupancy, queue.entries.size());
        write_status(0);
        return;
    }

    if (operation.kind == StochasticQueueKind::remove) {
        const auto job = required(operation.job_id, "job ID output");
        const auto information = required(
            operation.information_id, "information ID output");
        if (queue.entries.empty()) {
            write_i32(job, 0);
            write_i32(information, 0);
            write_status(3);
            return;
        }
        const auto entry = queue.lifo
            ? queue.entries.back()
            : queue.entries.front();
        if (queue.lifo)
            queue.entries.pop_back();
        else
            queue.entries.pop_front();
        const auto wait = scheduler.now() - entry.arrival;
        queue.shortest_wait = queue.shortest_wait
            ? std::min(*queue.shortest_wait, wait)
            : wait;
        queue.total_removed_wait = saturating_add(
            queue.total_removed_wait, wait);
        ++queue.removals;
        write_i32(job, entry.job_id);
        write_i32(information, entry.information_id);
        write_status(0);
        return;
    }

    if (operation.kind == StochasticQueueKind::full) {
        write_i32(
            required(operation.result, "$q_full result"),
            queue.entries.size() >= queue.maximum_length ? 1 : 0);
        write_status(0);
        return;
    }

    if (operation.kind != StochasticQueueKind::examine) {
        fail(process, "stochastic queue operation kind is invalid");
    }
    const auto code = read_i32(
        required(operation.statistic_code, "statistic code"),
        "statistic code");
    std::uint64_t statistic { };
    switch (code) {
    case 1:
        statistic = queue.entries.size();
        break;
    case 2:
        statistic = queue.arrivals > 1
            ? queue.total_interarrival / (queue.arrivals - 1U)
            : 0;
        break;
    case 3:
        statistic = queue.maximum_occupancy;
        break;
    case 4:
        statistic = queue.shortest_wait.value_or(0);
        break;
    case 5:
        for (const auto& entry : queue.entries) {
            statistic = std::max<std::uint64_t>(
                statistic, scheduler.now() - entry.arrival);
        }
        break;
    case 6:
        statistic = queue.removals != 0
            ? queue.total_removed_wait / queue.removals
            : 0;
        break;
    default:
        statistic = 0;
        break;
    }
    write_i32(
        required(operation.statistic_value, "statistic output"),
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(statistic)));
    write_status(0);
}

void Interpreter::Impl::execute_pla(
    ProcessState& process,
    const PlaEvaluate& operation)
{
    const auto& personality = read_container_object_value(operation.memory);
    const auto packed_memory
        = personality.type.element_kind == ContainerElementKind::Packed
        || personality.type.element_kind == ContainerElementKind::Scalar;
    if (!personality.type.fixed || !packed_memory
        || personality.type.dimensions.size() != 1U
        || personality.type.element_width != operation.input_width
        || personality.elements.size() != operation.output_width
        || operation.input_width == 0U || operation.output_width == 0U) {
        fail(
            process,
            "PLA personality memory must be a one-dimensional fixed packed array with one input-width word per output bit");
    }
    const auto input = process.executor
        ? process.executor->read_register(operation.input, operation.input_width)
        : get_register(process, operation.input);
    if (input.width() != operation.input_width) {
        fail(process, "PLA input register width does not match its personality");
    }
    const bool and_plane = operation.logic == PlaLogicKind::and_logic
        || operation.logic == PlaLogicKind::nand_logic;
    const bool invert = operation.logic == PlaLogicKind::nand_logic
        || operation.logic == PlaLogicKind::nor_logic;
    auto output = PackedLogic4(operation.output_width, Logic4::x);
    for (std::size_t row = 0; row < personality.elements.size(); ++row) {
        const auto& word = personality.elements[row];
        if (word.width() != operation.input_width) {
            fail(process, "PLA personality word has an invalid packed width");
        }
        auto result = and_plane ? Logic4::one : Logic4::zero;
        for (std::size_t bit = 0; bit < operation.input_width; ++bit) {
            const auto personality_bit = word.get(bit);
            std::optional<Logic4> selected;
            if (!operation.plane) {
                if (personality_bit == Logic4::one) {
                    selected = input.get(bit);
                } else if (personality_bit != Logic4::zero) {
                    selected = Logic4::x;
                }
            } else {
                switch (personality_bit) {
                case Logic4::zero:
                    selected = logic_not(input.get(bit));
                    break;
                case Logic4::one:
                    selected = input.get(bit);
                    break;
                case Logic4::x:
                    selected = Logic4::x;
                    break;
                case Logic4::z:
                    break;
                }
            }
            if (selected) {
                result = and_plane
                    ? logic_and(result, *selected)
                    : logic_or(result, *selected);
            }
        }
        if (invert) {
            result = logic_not(result);
        }
        // Fixed-array storage is declared-left first, while packed bit zero is
        // the declared-right term for the required ascending PLA profiles.
        output.set(operation.output_width - 1U - row, result);
    }
    write_process_register(process, operation.output, output);
}

void Interpreter::Impl::request_channel_update(
    const ProcessId process_id,
    const std::uint64_t channel)
{
    auto& process = get_process(process_id);
    if (!process.executor) {
        throw std::logic_error {
            "primitive-channel update requires an alternate executor"
        };
    }
    if (!pending_channel_updates.insert(channel).second) {
        return;
    }
    auto callback =
        [this, process_id, channel](Scheduler&) {
            auto& state = get_process(process_id);
            ExecutionContext context { *this, process_id };
            try {
                state.executor->update_channel(channel, context);
            } catch (...) {
                pending_channel_updates.erase(channel);
                throw;
            }
            pending_channel_updates.erase(channel);
        };
    const auto phase = scheduler.current_phase();
    if (phase
        && (*phase == SchedulerPhase::update
            || *phase == SchedulerPhase::observed
            || *phase >= SchedulerPhase::re_update)) {
        scheduler.schedule_next_delta(
            SchedulerPhase::update, channel, std::move(callback));
    } else {
        scheduler.schedule(
            SchedulerPhase::update, channel, std::move(callback));
    }
}

void Interpreter::Impl::execute_dynamic_call(
    ProcessState& process,
    const Call& operation)
{
    if (operation.stack.capacity != 0
        || operation.stack.pointer != 0
        || operation.stack.entries != 0) {
        fail(process, "dynamic call stack has fixed-register metadata");
    }
    if (process.dynamic_call_stack.size()
        >= maximum_container_storage_bytes / sizeof(InstructionIndex)) {
        fail(process, "dynamic call stack exceeds its owning-storage budget");
    }
    if (operation.target >= process.program.operations.size()
        || operation.return_target >= process.program.operations.size()) {
        fail(process, "call target is outside the operation stream");
    }
    process.dynamic_call_stack.push_back(operation.return_target);
    process.pc = operation.target;
}

void Interpreter::Impl::execute_dynamic_return(
    ProcessState& process,
    const Return& operation)
{
    if (operation.stack.capacity != 0
        || operation.stack.pointer != 0
        || operation.stack.entries != 0) {
        fail(process, "dynamic call stack has fixed-register metadata");
    }
    if (process.dynamic_call_stack.empty()) {
        fail(process, "call-stack underflow");
    }
    const auto target = process.dynamic_call_stack.back();
    process.dynamic_call_stack.pop_back();
    if (target >= process.program.operations.size()) {
        fail(process, "call-stack return target is invalid");
    }
    process.pc = target;
}

void Interpreter::Impl::push_callable_frame(
    ProcessState& process,
    const CallableFramePush& operation)
{
    if (operation.identity == 0) {
        fail(process, "automatic callable frame identity is zero");
    }
    ProcessState::CallableFrameState frame;
    frame.identity = operation.identity;
    frame.packed_ids = operation.packed;
    frame.string_ids = operation.strings;
    frame.container_ids = operation.containers;
    frame.packed.reserve(operation.packed.size());
    frame.strings.reserve(operation.strings.size());
    frame.containers.reserve(operation.containers.size());
    for (const auto register_id : operation.packed) {
        const auto value = process.executor
            ? process.executor->snapshot_register(register_id)
            : get_register(process, register_id);
        frame.storage_bytes += sizeof(PackedLogic4)
            + value.aval_words().size_bytes()
            + value.bval_words().size_bytes()
                * (value.is_logic9() ? 3U : 1U);
        frame.packed.push_back(value);
    }
    for (const auto register_id : operation.strings) {
        auto value = process.executor
            ? process.executor->read_string_register(register_id)
            : get_string_register(process, register_id);
        frame.storage_bytes += sizeof(std::string) + value.size();
        frame.strings.push_back(std::move(value));
    }
    for (const auto register_id : operation.containers) {
        auto value = process.executor
            ? process.executor->read_container_register(register_id)
            : get_container_register(process, register_id);
        frame.storage_bytes += sizeof(ContainerValue)
            + container_value_storage_bytes(value);
        frame.containers.push_back(std::move(value));
    }
    if (frame.storage_bytes
        > maximum_container_storage_bytes
            - std::min(
                process.callable_frame_storage_bytes,
                maximum_container_storage_bytes)) {
        fail(process, "automatic callable frames exceed their owning-storage budget");
    }
    process.callable_frame_storage_bytes += frame.storage_bytes;
    process.callable_frames.push_back(std::move(frame));
    ++process.pc;
}

void Interpreter::Impl::pop_callable_frame(
    ProcessState& process,
    const CallableFramePop& operation)
{
    if (process.callable_frames.empty()
        || process.callable_frames.back().identity != operation.identity) {
        fail(process, "automatic callable frame stack mismatch");
    }
    std::vector<PackedLogic4> preserved_packed;
    std::vector<std::string> preserved_strings;
    std::vector<ContainerValue> preserved_containers;
    preserved_packed.reserve(operation.preserve_packed.size());
    preserved_strings.reserve(operation.preserve_strings.size());
    preserved_containers.reserve(operation.preserve_containers.size());
    std::size_t preserved_storage_bytes { };
    const auto account_preserved_storage = [&](const std::size_t bytes) {
        const auto unavailable = std::min(
            process.callable_frame_storage_bytes,
            maximum_container_storage_bytes);
        if (bytes > maximum_container_storage_bytes - unavailable
            || preserved_storage_bytes
                > maximum_container_storage_bytes - unavailable - bytes) {
            fail(process,
                "automatic callable results exceed their owning-storage budget");
        }
        preserved_storage_bytes += bytes;
    };
    for (const auto register_id : operation.preserve_packed) {
        auto value = process.executor
            ? process.executor->snapshot_register(register_id)
            : get_register(process, register_id);
        account_preserved_storage(
            sizeof(PackedLogic4)
            + value.aval_words().size_bytes()
            + value.bval_words().size_bytes()
                * (value.is_logic9() ? 3U : 1U));
        preserved_packed.push_back(std::move(value));
    }
    for (const auto register_id : operation.preserve_strings) {
        auto value = process.executor
            ? process.executor->read_string_register(register_id)
            : get_string_register(process, register_id);
        account_preserved_storage(sizeof(std::string) + value.size());
        preserved_strings.push_back(std::move(value));
    }
    for (const auto register_id : operation.preserve_containers) {
        auto value = process.executor
            ? process.executor->read_container_register(register_id)
            : get_container_register(process, register_id);
        account_preserved_storage(
            sizeof(ContainerValue) + container_value_storage_bytes(value));
        preserved_containers.push_back(std::move(value));
    }
    auto frame = std::move(process.callable_frames.back());
    process.callable_frames.pop_back();
    for (std::size_t index = 0; index < frame.packed.size(); ++index) {
        if (process.executor) {
            process.executor->write_register(
                frame.packed_ids[index], frame.packed[index]);
        } else {
            get_register(process, frame.packed_ids[index])
                = std::move(frame.packed[index]);
        }
    }
    for (std::size_t index = 0; index < frame.strings.size(); ++index) {
        if (process.executor) {
            process.executor->write_string_register(
                frame.string_ids[index], frame.strings[index]);
        } else {
            get_string_register(process, frame.string_ids[index])
                = std::move(frame.strings[index]);
        }
    }
    for (std::size_t index = 0; index < frame.containers.size(); ++index) {
        if (process.executor) {
            process.executor->write_container_register(
                frame.container_ids[index], frame.containers[index]);
        } else {
            get_container_register(process, frame.container_ids[index])
                = std::move(frame.containers[index]);
        }
    }
    process.callable_frame_storage_bytes -= frame.storage_bytes;
    for (std::size_t index = 0;
        index < preserved_packed.size(); ++index) {
        if (process.executor) {
            process.executor->write_register(
                operation.preserve_packed[index], preserved_packed[index]);
        } else {
            get_register(process, operation.preserve_packed[index])
                = std::move(preserved_packed[index]);
        }
    }
    for (std::size_t index = 0;
        index < preserved_strings.size(); ++index) {
        if (process.executor) {
            process.executor->write_string_register(
                operation.preserve_strings[index], preserved_strings[index]);
        } else {
            get_string_register(process, operation.preserve_strings[index])
                = std::move(preserved_strings[index]);
        }
    }
    for (std::size_t index = 0;
        index < preserved_containers.size(); ++index) {
        if (process.executor) {
            process.executor->write_container_register(
                operation.preserve_containers[index],
                preserved_containers[index]);
        } else {
            get_container_register(
                process, operation.preserve_containers[index])
                = std::move(preserved_containers[index]);
        }
    }
    ++process.pc;
}

void Interpreter::Impl::snapshot_callable_context(ProcessState& process)
{
    process.suspended_callable_context.reset();
    process.callable_context_storage_bytes = 0;
    if (process.halted || process.callable_frames.empty()) {
        return;
    }

    std::set<RegisterId> packed_ids;
    std::set<StringRegisterId> string_ids;
    std::set<ContainerRegisterId> container_ids;
    for (const auto& frame : process.callable_frames) {
        packed_ids.insert(frame.packed_ids.begin(), frame.packed_ids.end());
        string_ids.insert(frame.string_ids.begin(), frame.string_ids.end());
        container_ids.insert(
            frame.container_ids.begin(), frame.container_ids.end());
    }

    ProcessState::CallableFrameState context;
    context.packed_ids.assign(packed_ids.begin(), packed_ids.end());
    context.string_ids.assign(string_ids.begin(), string_ids.end());
    context.container_ids.assign(container_ids.begin(), container_ids.end());
    context.packed.reserve(context.packed_ids.size());
    context.strings.reserve(context.string_ids.size());
    context.containers.reserve(context.container_ids.size());
    const auto account_context_storage = [&](const std::size_t bytes) {
        const auto unavailable = std::min(
            process.callable_frame_storage_bytes,
            maximum_container_storage_bytes);
        if (bytes > maximum_container_storage_bytes - unavailable
            || context.storage_bytes
                > maximum_container_storage_bytes - unavailable - bytes) {
            fail(process,
                "suspended automatic callable context exceeds its owning-storage budget");
        }
        context.storage_bytes += bytes;
    };
    for (const auto register_id : context.packed_ids) {
        auto value = process.executor
            ? process.executor->snapshot_register(register_id)
            : get_register(process, register_id);
        account_context_storage(
            sizeof(PackedLogic4)
            + value.aval_words().size_bytes()
            + value.bval_words().size_bytes()
                * (value.is_logic9() ? 3U : 1U));
        context.packed.push_back(std::move(value));
    }
    for (const auto register_id : context.string_ids) {
        auto value = process.executor
            ? process.executor->read_string_register(register_id)
            : get_string_register(process, register_id);
        account_context_storage(sizeof(std::string) + value.size());
        context.strings.push_back(std::move(value));
    }
    for (const auto register_id : context.container_ids) {
        auto value = process.executor
            ? process.executor->read_container_register(register_id)
            : get_container_register(process, register_id);
        account_context_storage(
            sizeof(ContainerValue) + container_value_storage_bytes(value));
        context.containers.push_back(std::move(value));
    }
    process.callable_context_storage_bytes = context.storage_bytes;
    process.suspended_callable_context = std::move(context);
}

void Interpreter::Impl::restore_callable_context(ProcessState& process)
{
    if (!process.suspended_callable_context) {
        return;
    }
    auto context = std::move(*process.suspended_callable_context);
    process.suspended_callable_context.reset();
    process.callable_context_storage_bytes = 0;
    for (std::size_t index = 0; index < context.packed.size(); ++index) {
        if (process.executor) {
            process.executor->write_register(
                context.packed_ids[index], context.packed[index]);
        } else {
            get_register(process, context.packed_ids[index])
                = std::move(context.packed[index]);
        }
    }
    for (std::size_t index = 0; index < context.strings.size(); ++index) {
        if (process.executor) {
            process.executor->write_string_register(
                context.string_ids[index], context.strings[index]);
        } else {
            get_string_register(process, context.string_ids[index])
                = std::move(context.strings[index]);
        }
    }
    for (std::size_t index = 0; index < context.containers.size(); ++index) {
        if (process.executor) {
            process.executor->write_container_register(
                context.container_ids[index], context.containers[index]);
        } else {
            get_container_register(process, context.container_ids[index])
                = std::move(context.containers[index]);
        }
    }
}

#include "simir_execution_boundaries.tpp"
void Interpreter::Impl::execute(ProcessId id)
{
    auto& process = get_process(id);
    if (process.suspended) {
        process.suspended_wake = true;
        return;
    }
    restore_callable_context(process);
    if (!process.halted) {
        process.status = ProcessStatus::running;
    }
    if (process.executor) {
        ExecutionContext context { *this, id };
        while (!process.halted) {
            const auto boundary = process.executor->resume(context, process.pc);
            const auto* operation = boundary.instruction < process.program.operations.size()
                ? &process.program.operations[boundary.instruction]
                : nullptr;
            const bool debug_boundary = operation && operation_holds<DebugPoint>(*operation);
            const bool class_boundary = operation && is_class_execution_boundary(*operation);
            const bool immediate_process_boundary = operation && is_immediate_process_boundary(*operation);
            const bool synchronization_boundary = operation && is_synchronization_boundary(*operation);
            const bool callable_boundary = operation && is_dynamic_callable_boundary(*operation);
            if (boundary.external.kind
                == ExternalSuspendKind::simir_boundary) {
                handle_boundary(
                    process, boundary.instruction, boundary.next_instruction);
            } else {
                handle_external_boundary(
                    process,
                    boundary.instruction,
                    boundary.next_instruction,
                    boundary.external);
            }
            if ((class_boundary || callable_boundary
                    || immediate_process_boundary
                    || (synchronization_boundary
                        && process.status == ProcessStatus::running
                        && !process.queued))
                && !scheduler.stop_requested()) {
                continue;
            }
            if (!debug_boundary || scheduler.stop_requested()) {
                snapshot_callable_context(process);
                return;
            }
        }
        snapshot_callable_context(process);
        return;
    }

    while (!process.halted) {
        if (process.pc >= process.program.operations.size()) {
            fail(process, "program counter is outside the operation stream");
        }

        const auto instruction = process.pc;
        const auto& operation = process.program.operations[instruction];
        bool boundary = false;
        const auto selected_offset =
            [&](const DynamicIndex& selection) -> std::uint32_t {
            try {
                return dynamic_index_offset(
                    get_register(process, selection.index),
                    selection);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
        };
        const auto known_string_index =
            [&](const RegisterId index_register,
                const bool signed_index,
                const std::size_t size) -> std::size_t {
            const auto& value = get_register(process, index_register);
            if (value.width() == 0 || value.width() > 64) {
                fail(
                    process,
                    "string index must be a nonempty value of at most 64 bits");
            }
            const auto word = value.low_word();
            if (word.bval != 0) {
                fail(process, "string index contains X or Z");
            }
            if (signed_index && word.width != 0
                && word.width < 64
                && ((word.aval >> (word.width - 1U)) & 1U) != 0) {
                fail(process, "string index is negative");
            }
            if (signed_index && word.width == 64
                && (word.aval >> 63U) != 0) {
                fail(process, "string index is negative");
            }
            if (word.aval >= size) {
                fail(process, "string index is outside the code-point range");
            }
            return static_cast<std::size_t>(word.aval);
        };
        fsim::runtime::simir::visit_operation(
            [&](const auto& op) {
                using OperationType = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<OperationType, LoadConstant>) {
                    get_register(process, op.destination) = coerce_value_kind(
                        op.value,
                        register_value_kind(
                            process, op.destination));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
                    const auto& signal = get_signal(op.signal);
                    if (op.kind == SignalReadKind::current) {
                        get_register(process, op.destination) = coerce_value_kind(
                            signal.initial_value,
                            register_value_kind(process, op.destination));
                        ++process.pc;
                        return;
                    }
                    execute_sampled_read(process, op);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
                    (void)get_signal(op.signal);
                    const auto& event = signal_events[op.signal];
                    const auto active = event
                        && event->first == scheduler.now()
                        && event->second == scheduler.delta();
                    get_register(process, op.destination) = PackedLogic4(
                        1, active ? Logic4::one : Logic4::zero);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
                    (void)get_signal(op.signal);
                    get_register(process, op.destination) = coerce_value_kind(
                        signal_last_values[op.signal],
                        register_value_kind(
                            process, op.destination));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
                    (void)get_signal(op.signal);
                    const auto& event = signal_events[op.signal];
                    const auto elapsed = event
                        ? scheduler.now() - event->first
                        : std::numeric_limits<SimulationTick>::max();
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(64, elapsed, 0);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ReadSimulationTime>) {
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(64, scheduler.now(), 0);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, VitalTimingCheck>) {
                    ExecutionContext context { *this, id };
                    PackedLogic4 result(1);
                    result.fill(context.evaluate_vital_timing_check(
                        instruction, op));
                    get_register(process, op.destination) = std::move(result);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
                    execute_vital_delay_operation(id, process, instruction, op);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
                    (void)get_signal(op.signal);
                    const auto& transaction = signal_transactions[op.signal];
                    const auto active = transaction
                        && transaction->first == scheduler.now()
                        && transaction->second == scheduler.delta();
                    get_register(process, op.destination) = PackedLogic4(
                        1, active ? Logic4::one : Logic4::zero);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, SignalLastActive>) {
                    const auto elapsed = signal_attribute_detail::last_active(
                        *this, op.signal);
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(64, elapsed, 0);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDriving>) {
                    const auto driving = signal_attribute_detail::driving(
                        *this, process.program.id, op.signal);
                    get_register(process, op.destination) = PackedLogic4(
                        1, driving ? Logic4::one : Logic4::zero);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDrivingValue>) {
                    if (!signal_attribute_detail::driving(
                            *this, process.program.id, op.signal)) {
                        fail(process,
                            "VHDL 'driving_value queried a signal without a driver");
                    }
                    get_register(process, op.destination) = coerce_value_kind(
                        signal_attribute_detail::driving_value(
                            *this, process.program.id, op.signal),
                        register_value_kind(process, op.destination));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
                    get_register(process, op.destination) = coerce_value_kind(
                        get_register(process, op.source),
                        register_value_kind(
                            process, op.destination));
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ConvertToTwoState>) {
                    const auto& source = get_register(process, op.source);
                    auto converted = PackedLogic4(
                        source.width(), Logic4::zero);
                    for (std::size_t bit = 0; bit < source.width(); ++bit) {
                        if (to_logic4(source.get_logic9(bit))
                            == Logic4::one) {
                            converted.set(bit, Logic4::one);
                        }
                    }
                    get_register(process, op.destination) = std::move(converted);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, ClassAllocate>) {
                    if (!class_allocate_hook) {
                        fail(process, "class allocation service is unavailable");
                    }
                    std::vector<PackedLogic4> actuals;
                    std::vector<std::string> string_actuals;
                    actuals.reserve(op.constructor_actuals.size());
                    string_actuals.reserve(op.constructor_actuals.size());
                    for (std::size_t index = 0;
                        index < op.constructor_actuals.size(); ++index) {
                        const auto actual = op.constructor_actuals[index];
                        const auto string_actual = !op.constructor_actual_kinds.empty()
                            && op.constructor_actual_kinds[index] == 1U;
                        actuals.push_back(
                            string_actual
                                ? PackedLogic4(64)
                                : get_register(process, actual));
                        string_actuals.push_back(
                            string_actual
                                ? process.frame->string_registers.at(actual)
                                : std::string { });
                    }
                    const auto handle = class_allocate_hook(
                        process.program.name,
                        op.specialization_identity,
                        op.declared_type,
                        actuals,
                        string_actuals,
                        op.constructor_actual_names);
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(64, handle, 0);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ClassPropertyRead>) {
                    if (!class_property_read_hook) {
                        fail(process, "class property service is unavailable");
                    }
                    const auto handle = get_register(process, op.receiver).low_word().aval;
                    get_register(process, op.destination) = resize_class_value(
                        class_property_read_hook(handle, op.property_identity),
                        op.width);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ClassPropertyWrite>) {
                    if (!class_property_write_hook) {
                        fail(process, "class property service is unavailable");
                    }
                    const auto handle = get_register(process, op.receiver).low_word().aval;
                    class_property_write_hook(
                        handle, op.property_identity,
                        get_register(process, op.source));
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ClassMethodCall>) {
                    if (!class_method_call_hook) {
                        fail(process, "class method service is unavailable");
                    }
                    std::vector<PackedLogic4> actuals;
                    std::vector<std::string> string_actuals;
                    actuals.reserve(op.actuals.size());
                    string_actuals.reserve(op.actuals.size());
                    for (std::size_t index = 0; index < op.actuals.size(); ++index) {
                        const auto string_actual = !op.actual_kinds.empty()
                            && op.actual_kinds[index] == 1U;
                        actuals.push_back(
                            string_actual
                                ? PackedLogic4(64)
                                : get_register(process, op.actuals[index]));
                        string_actuals.push_back(
                            string_actual
                                ? get_string_register(process, op.actuals[index])
                                : std::string { });
                    }
                    const auto handle = get_register(process, op.receiver).low_word().aval;
                    get_register(process, op.destination) = class_method_call_hook(
                        handle,
                        op.method_identity,
                        actuals,
                        string_actuals,
                        op.actual_names,
                        op.actual_directions,
                        op.inline_constraints,
                        op.virtual_dispatch);
                    for (std::size_t index = 0; index < actuals.size(); ++index) {
                        const auto string_actual = !op.actual_kinds.empty()
                            && op.actual_kinds[index] == 1U;
                        if (string_actual) {
                            get_string_register(process, op.actuals[index]) = string_actuals[index];
                        } else {
                            get_register(process, op.actuals[index]) = actuals[index];
                        }
                    }
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ClassStaticPropertyRead>) {
                    if (!class_static_property_read_hook) {
                        fail(process, "class static property service is unavailable");
                    }
                    get_register(process, op.destination) = resize_class_value(
                        class_static_property_read_hook(op.property_identity),
                        op.width);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ClassStaticPropertyWrite>) {
                    if (!class_static_property_write_hook) {
                        fail(process, "class static property service is unavailable");
                    }
                    class_static_property_write_hook(
                        op.property_identity, get_register(process, op.source));
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ClassStaticMethodCall>) {
                    if (!class_static_method_call_hook) {
                        fail(process, "class static method service is unavailable");
                    }
                    std::vector<PackedLogic4> actuals;
                    std::vector<std::string> string_actuals;
                    actuals.reserve(op.actuals.size());
                    string_actuals.reserve(op.actuals.size());
                    for (std::size_t index = 0; index < op.actuals.size(); ++index) {
                        const auto string_actual = !op.actual_kinds.empty()
                            && op.actual_kinds[index] == 1U;
                        actuals.push_back(
                            string_actual
                                ? PackedLogic4(64)
                                : get_register(process, op.actuals[index]));
                        string_actuals.push_back(
                            string_actual
                                ? get_string_register(process, op.actuals[index])
                                : std::string { });
                    }
                    get_register(process, op.destination) = class_static_method_call_hook(
                        op.method_identity,
                        actuals,
                        string_actuals,
                        op.actual_names,
                        op.actual_directions);
                    for (std::size_t index = 0; index < actuals.size(); ++index) {
                        const auto string_actual = !op.actual_kinds.empty()
                            && op.actual_kinds[index] == 1U;
                        if (string_actual) {
                            get_string_register(process, op.actuals[index]) = string_actuals[index];
                        } else {
                            get_register(process, op.actuals[index]) = actuals[index];
                        }
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
                    if (op.value.size() > maximum_string_bytes) {
                        fail(process, "string literal exceeds 4096-byte limit");
                    }
                    try {
                        (void)systemverilog_string_length(op.value);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    get_string_register(process, op.destination) = op.value;
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, CopyStringRegister>) {
                    get_string_register(process, op.destination) = get_string_register(process, op.source);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, ReadStringObject>) {
                    get_string_register(process, op.destination) = get_string_object(op.object).initial_value;
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, WriteStringObject>) {
                    get_string_object(op.object).initial_value = get_string_register(process, op.source);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, ConcatenateStrings>) {
                    std::string result;
                    for (const auto operand : op.operands) {
                        const auto& value = get_string_register(process, operand);
                        if (value.size()
                            > maximum_string_bytes - result.size()) {
                            fail(
                                process,
                                "string concatenation exceeds 4096-byte limit");
                        }
                        result += value;
                    }
                    get_string_register(process, op.destination) = std::move(result);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, CompareStrings>) {
                    bool equal { };
                    try {
                        equal = systemverilog_string_compare(
                                    get_string_register(process, op.lhs),
                                    get_string_register(process, op.rhs))
                            == 0;
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    get_register(process, op.destination) = PackedLogic4 {
                        1,
                        equal != op.not_equal
                            ? Logic4::one
                            : Logic4::zero
                    };
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, StringLength>) {
                    std::size_t size { };
                    try {
                        size = systemverilog_string_length(
                            get_string_register(process, op.source));
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(
                        32, static_cast<std::uint32_t>(size), 0);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
                    auto& source = get_string_register(process, op.source);
                    std::size_t size { };
                    try {
                        size = systemverilog_string_length(source);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    const auto index = known_string_index(
                        op.index, op.signed_index, size);
                    std::uint32_t code_point { };
                    try {
                        code_point = systemverilog_string_at(source, index);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(
                        32, code_point, 0);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, StringReplaceCodePoint>) {
                    auto& target = get_string_register(process, op.target);
                    std::size_t size { };
                    try {
                        size = systemverilog_string_length(target);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    const auto index = known_string_index(
                        op.index, op.signed_index, size);
                    const auto code_point = get_register(process, op.source).low_word();
                    if (code_point.bval != 0) {
                        fail(process, "string replacement code point contains X or Z");
                    }
                    try {
                        systemverilog_string_replace(
                            target, index,
                            static_cast<std::uint32_t>(code_point.aval),
                            maximum_string_bytes);
                    } catch (const std::exception& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, StringMethod>) {
                    execute_string(process, op);
                } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
                    const auto& query = get_string_register(process, op.query);
                    const std::string* selected = nullptr;
                    for (const auto& argument : plusargs) {
                        auto candidate = std::string_view { argument };
                        if (candidate.starts_with('+')) {
                            candidate.remove_prefix(1);
                        }
                        if (candidate.starts_with(query)) {
                            selected = &argument;
                            break;
                        }
                    }
                    if (op.selected) {
                        auto& destination = get_string_register(process, *op.selected);
                        destination.clear();
                        if (selected != nullptr) {
                            auto candidate = std::string_view { *selected };
                            if (candidate.starts_with('+')) {
                                candidate.remove_prefix(1);
                            }
                            destination.assign(candidate);
                        }
                    }
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(
                        32, selected != nullptr ? 1U : 0U, 0);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
                    if (!system_command_hook) {
                        fail(process, "$system service is unavailable");
                    }
                    const auto command = op.command
                        ? std::optional<std::string_view> {
                              get_string_register(process, *op.command)
                          }
                        : std::nullopt;
                    const auto status = system_command_hook(command);
                    if (op.destination) {
                        get_register(process, *op.destination)
                            = PackedLogic4::from_aval_bval(
                                32,
                                static_cast<std::uint32_t>(status),
                                0);
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
                    if (!vcd_control_hook) {
                        fail(process, "VCD control service is unavailable");
                    }
                    VcdControlEvent event;
                    event.kind = op.kind;
                    if (op.filename) {
                        event.filename = get_string_register(process, *op.filename);
                    }
                    if (op.value) {
                        const auto converted
                            = get_register(process, *op.value).known_unsigned_value();
                        if (!converted) {
                            fail(process, "VCD control value must be a known unsigned 64-bit integer");
                        }
                        event.value = *converted;
                    }
                    event.selections = op.selections;
                    event.scope = op.scope;
                    event.time = scheduler.now();
                    event.delta = scheduler.delta();
                    vcd_control_hook(event);
                    if (op.kind == VcdControlKind::variables
                        || op.kind == VcdControlKind::ports) {
                        const auto begin_kind
                            = op.kind == VcdControlKind::variables
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
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType,
                        CoverageDatabaseControl>) {
                    if (!coverage_database_control_hook) {
                        fail(process,
                            "coverage database service is unavailable");
                    }
                    coverage_database_control_hook({ op.kind,
                        get_string_register(process, op.filename) });
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, StochasticQueueOperation>) {
                    execute_stochastic_queue(process, op);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
                    execute_pla(process, op);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, TimeFormatControl>) {
                    set_time_format(process, op);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
                    get_register(process, op.destination) = unary_not(get_register(process, op.source));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
                    get_register(process, op.destination) = logical_not(get_register(process, op.source));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
                    get_register(process, op.destination) = logical_binary(
                        op.operation,
                        get_register(process, op.lhs),
                        get_register(process, op.rhs));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Reduction>) {
                    get_register(process, op.destination) = reduce_value(
                        op.operation,
                        get_register(process, op.source));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
                    get_register(process, op.destination) = count_ones_value(
                        get_register(process, op.source));
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, CountBits>) {
                    get_register(process, op.destination) = count_bits_value(
                        get_register(process, op.source),
                        op.state_mask);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Shift>) {
                    get_register(process, op.destination) = shift_value(
                        op.operation,
                        get_register(process, op.value),
                        get_register(process, op.amount),
                        op.signed_amount);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Extract>) {
                    try {
                        get_register(process, op.destination) = extract_value(
                            get_register(process, op.source),
                            op.offset,
                            op.width);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
                    try {
                        get_register(process, op.destination) = extract_value(
                            get_register(process, op.source),
                            dynamic_index_offset(
                                get_register(process, op.selection.index),
                                op.selection),
                            1);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
                    try {
                        get_register(process, op.destination) = dynamic_part_select_value(
                            get_register(process, op.source),
                            get_register(process, op.base),
                            op.left,
                            op.right,
                            op.base_offset,
                            op.width,
                            op.increasing,
                            op.source_descending,
                            op.two_state);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Insert>) {
                    try {
                        get_register(process, op.destination) = insert_value(
                            get_register(process, op.target),
                            get_register(process, op.source),
                            op.offset);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
                    try {
                        get_register(process, op.destination) = insert_value(
                            get_register(process, op.target),
                            get_register(process, op.source),
                            dynamic_index_offset(
                                get_register(process, op.selection.index),
                                op.selection));
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
                    try {
                        get_register(process, op.destination) = dynamic_part_insert_value(
                            get_register(process, op.target),
                            get_register(process, op.source),
                            get_register(
                                process, op.selection.base),
                            op.selection);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
                    std::vector<PackedLogic4> operands;
                    operands.reserve(op.operands.size());
                    for (const auto operand : op.operands) {
                        operands.push_back(get_register(process, operand));
                    }
                    try {
                        get_register(process, op.destination) = concatenate_values(operands, op.width);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, SystemVerilogScalarBinary>) {
                    const auto result = systemverilog_scalar_binary_payload(
                        op.operation,
                        get_register(process, op.lhs), op.lhs_kind,
                        get_register(process, op.rhs), op.rhs_kind,
                        op.result_kind);
                    if (!result) {
                        fail(
                            process,
                            "SystemVerilog scalar binary operation failed (error "
                                + std::to_string(static_cast<unsigned>(result.error))
                                + ", lhs kind "
                                + std::to_string(static_cast<unsigned>(op.lhs_kind))
                                + ", rhs kind "
                                + std::to_string(static_cast<unsigned>(op.rhs_kind))
                                + ")");
                    }
                    get_register(process, op.destination) = result.value;
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, SystemVerilogMath>) {
                    if (op.function >= SystemVerilogMathFunction::Time) {
                        const auto function
                            = op.function == SystemVerilogMathFunction::Time
                            ? SystemVerilogTimeFunction::Time
                            : op.function
                                == SystemVerilogMathFunction::Stime
                            ? SystemVerilogTimeFunction::Stime
                            : SystemVerilogTimeFunction::Realtime;
                        const auto value = systemverilog_time_function(
                            function,
                            scheduler.now(),
                            { op.time_unit_femtoseconds,
                                op.time_precision_femtoseconds,
                                time_format.resolution_femtoseconds });
                        if (!value) {
                            fail(
                                process,
                                "SystemVerilog time query failed (error "
                                    + std::to_string(static_cast<unsigned>(
                                        value.error))
                                    + ")");
                        }
                        const auto encoded = op.function
                                == SystemVerilogMathFunction::Realtime
                            ? encode_systemverilog_scalar_payload(value.value)
                            : systemverilog_scalar_to_packed(
                                  value.value,
                                  op.function
                                          == SystemVerilogMathFunction::Stime
                                      ? 32U
                                      : 64U,
                                  false);
                        if (!encoded) {
                            fail(
                                process,
                                "SystemVerilog time query failed (error "
                                    + std::to_string(static_cast<unsigned>(
                                        encoded.error))
                                    + ")");
                        }
                        get_register(process, op.destination) = encoded.value;
                        ++process.pc;
                        return;
                    }
                    const auto* second = op.second_width == 0
                        ? nullptr
                        : &get_register(process, op.second);
                    const auto result = systemverilog_math_payload(
                        op.function,
                        get_register(process, op.first),
                        op.first_kind,
                        op.first_signed,
                        second,
                        op.second_kind,
                        op.second_signed);
                    if (!result) {
                        fail(
                            process,
                            "SystemVerilog math function failed (error "
                                + std::to_string(
                                    static_cast<unsigned>(result.error))
                                + ")");
                    }
                    get_register(process, op.destination) = result.value;
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Binary>) {
                    try {
                        get_register(process, op.destination) = binary_value(op.operation, get_register(process, op.lhs),
                            get_register(process, op.rhs));
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
                    try {
                        get_register(process, op.destination) = integer_unary_value(
                            op.operation,
                            get_register(process, op.source));
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
                    try {
                        get_register(process, op.destination) = integer_binary_value(
                            op.operation,
                            get_register(process, op.lhs),
                            get_register(process, op.rhs));
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
                    try {
                        check_integer_range(
                            get_register(process, op.source),
                            op.lower,
                            op.upper);
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
                    try {
                        get_register(process, op.destination) = conditional_value(
                            get_register(process, op.condition),
                            get_register(process, op.when_true),
                            get_register(process, op.when_false));
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else {
                    // Restart template dispatch depth before MSVC reaches its
                    // nested-block compiler limit on the full operation set.
                    [&] {
                if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    commit_driver(
                        process.program.id,
                        op.signal,
                        std::move(value));
                } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    stage_update(
                        process.program.id,
                        op.signal,
                        std::move(value));
                } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    scheduler.schedule_after(
                        op.delay, SchedulerPhase::update, process.program.id,
                        [this,
                            driver = process.program.id,
                            signal = op.signal,
                            value = std::move(value)](Scheduler&) mutable {
                            stage_update(
                                driver, signal, std::move(value));
                        });
                } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    schedule_inertial(
                        process.program.id,
                        op.signal,
                        std::move(value),
                        std::nullopt,
                        op.delays);
                } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    schedule_projected(
                        process.program.id,
                        op.signal,
                        value,
                        std::nullopt,
                        op.delay,
                        op.rejection,
                        op.mode);
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
                    std::vector<ProjectedWaveformValue> elements;
                    elements.reserve(op.elements.size());
                    for (const auto& element : op.elements) {
                        elements.push_back(
                            { get_register(process, element.source),
                                element.delay });
                    }
                    ++process.pc;
                    schedule_projected_waveform(
                        process.program.id,
                        op.signal,
                        elements,
                        std::nullopt,
                        op.rejection,
                        op.mode);
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    commit_driver_slice(
                        process.program.id,
                        op.signal,
                        std::move(value),
                        op.offset);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    stage_update_slice(
                        process.program.id,
                        op.signal,
                        std::move(value),
                        op.offset);
                } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    scheduler.schedule_after(
                        op.delay,
                        SchedulerPhase::update,
                        process.program.id,
                        [this,
                            driver = process.program.id,
                            signal = op.signal,
                            offset = op.offset,
                            value = std::move(value)](
                            Scheduler&) mutable {
                            stage_update_slice(
                                driver,
                                signal,
                                std::move(value),
                                offset);
                        });
                } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    schedule_inertial(
                        process.program.id,
                        op.signal,
                        std::move(value),
                        op.offset,
                        op.delays);
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
                    auto value = get_register(process, op.source);
                    ++process.pc;
                    schedule_projected(
                        process.program.id,
                        op.signal,
                        value,
                        op.offset,
                        op.delay,
                        op.rejection,
                        op.mode);
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
                    std::vector<ProjectedWaveformValue> elements;
                    elements.reserve(op.elements.size());
                    for (const auto& element : op.elements) {
                        elements.push_back(
                            { get_register(process, element.source),
                                element.delay });
                    }
                    ++process.pc;
                    schedule_projected_waveform(
                        process.program.id,
                        op.signal,
                        elements,
                        op.offset,
                        op.rejection,
                        op.mode);
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
                    auto value = get_register(process, op.source);
                    const auto offset = selected_offset(op.selection);
                    ++process.pc;
                    commit_driver_slice(
                        process.program.id,
                        op.signal,
                        std::move(value),
                        offset);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
                    auto value = get_register(process, op.source);
                    const auto offset = selected_offset(op.selection);
                    ++process.pc;
                    stage_update_slice(
                        process.program.id,
                        op.signal,
                        std::move(value),
                        offset);
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
                    auto value = get_register(process, op.source);
                    const auto offset = selected_offset(op.selection);
                    ++process.pc;
                    scheduler.schedule_after(
                        op.delay,
                        SchedulerPhase::update,
                        process.program.id,
                        [this,
                            driver = process.program.id,
                            signal = op.signal,
                            offset,
                            value = std::move(value)](
                            Scheduler&) mutable {
                            stage_update_slice(
                                driver,
                                signal,
                                std::move(value),
                                offset);
                        });
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
                    try {
                        const auto write = dynamic_part_write_value(
                            get_register(process, op.source),
                            get_register(process, op.selection.base),
                            op.selection);
                        ++process.pc;
                        if (write) {
                            commit_driver_slice(
                                process.program.id,
                                op.signal,
                                write->value,
                                write->offset);
                        }
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
                    try {
                        const auto write = dynamic_part_write_value(
                            get_register(process, op.source),
                            get_register(process, op.selection.base),
                            op.selection);
                        ++process.pc;
                        if (write) {
                            stage_update_slice(
                                process.program.id,
                                op.signal,
                                write->value,
                                write->offset);
                        }
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
                    try {
                        auto write = dynamic_part_write_value(
                            get_register(process, op.source),
                            get_register(process, op.selection.base),
                            op.selection);
                        ++process.pc;
                        if (write) {
                            scheduler.schedule_after(
                                op.delay,
                                SchedulerPhase::update,
                                process.program.id,
                                [this,
                                    driver = process.program.id,
                                    signal = op.signal,
                                    write = std::move(*write)](
                                    Scheduler&) mutable {
                                    stage_update_slice(
                                        driver,
                                        signal,
                                        std::move(write.value),
                                        write.offset);
                                });
                        }
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
                    try {
                        const auto offset = op.selection
                            ? selected_offset(*op.selection)
                            : op.offset;
                        if (op.driving_value) {
                            force_driver_slice(
                                process.program.id,
                                op.signal,
                                get_register(process, op.source),
                                offset);
                        } else {
                            force_slice(
                                op.signal,
                                get_register(process, op.source),
                                offset);
                        }
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
                    try {
                        const auto offset = op.selection
                            ? selected_offset(*op.selection)
                            : op.offset;
                        if (op.driving_value) {
                            release_driver_slice(
                                process.program.id, op.signal, offset, op.width);
                        } else {
                            release_slice(op.signal, offset, op.width);
                        }
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
                    auto value = get_register(process, op.source);
                    const auto offset = selected_offset(op.selection);
                    ++process.pc;
                    schedule_inertial(
                        process.program.id,
                        op.signal,
                        std::move(value),
                        offset,
                        op.delays);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         WriteInertialDynamicPartSlice>) {
                    try {
                        auto write = dynamic_part_write_value(
                            get_register(process, op.source),
                            get_register(process, op.selection.base),
                            op.selection);
                        ++process.pc;
                        if (write) {
                            schedule_inertial(
                                process.program.id,
                                op.signal,
                                std::move(write->value),
                                write->offset,
                                op.delays);
                        }
                    } catch (const std::invalid_argument& error) {
                        fail(process, error.what());
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
                    auto value = get_register(process, op.source);
                    const auto offset = selected_offset(op.selection);
                    ++process.pc;
                    schedule_projected(
                        process.program.id,
                        op.signal,
                        value,
                        offset,
                        op.delay,
                        op.rejection,
                        op.mode);
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
                    std::vector<ProjectedWaveformValue> elements;
                    elements.reserve(op.elements.size());
                    for (const auto& element : op.elements) {
                        elements.push_back(
                            { get_register(process, element.source),
                                element.delay });
                    }
                    const auto offset = selected_offset(op.selection);
                    ++process.pc;
                    schedule_projected_waveform(
                        process.program.id,
                        op.signal,
                        elements,
                        offset,
                        op.rejection,
                        op.mode);
                } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
                    (void)op;
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
                    (void)op;
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
                    (void)op;
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
                    (void)op;
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
                    (void)op;
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
                    (void)op;
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
                    (void)op;
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, Yield>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, Fork>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, ProcessSelf>) {
                    boundary = true;
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessStatusQuery>) {
                    boundary = true;
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessCompleted>) {
                    boundary = true;
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessAwait>) {
                    boundary = true;
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessKill>
                    || std::is_same_v<OperationType, ProcessSuspend>
                    || std::is_same_v<OperationType, ProcessResume>
                    || std::is_same_v<OperationType, ProcessGetRandState>
                    || std::is_same_v<OperationType, ProcessSetRandState>
                    || std::is_same_v<OperationType, ProcessSrandom>) {
                    boundary = true;
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxCreate>
                    || std::is_same_v<OperationType, MailboxPut>
                    || std::is_same_v<OperationType, MailboxGet>
                    || std::is_same_v<OperationType, MailboxNum>
                    || std::is_same_v<OperationType, SemaphoreCreate>
                    || std::is_same_v<OperationType, SemaphoreGet>
                    || std::is_same_v<OperationType, SemaphorePut>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, Jump>) {
                    if (op.target >= process.program.operations.size()) {
                        fail(process, "jump target is outside the operation stream");
                    }
                    process.pc = op.target;
                } else if constexpr (std::is_same_v<OperationType, Call>) {
                    if (op.stack.capacity == 0) {
                        execute_dynamic_call(process, op);
                        return;
                    }
                    const auto pointer = get_register(process, op.stack.pointer).low_word();
                    if (pointer.bval != 0) {
                        fail(process, "call-stack pointer is unknown");
                    }
                    if (pointer.aval >= op.stack.capacity) {
                        fail(process, "call-stack capacity is exhausted");
                    }
                    if (op.target >= process.program.operations.size()
                        || op.return_target
                            >= process.program.operations.size()) {
                        fail(process, "call target is outside the operation stream");
                    }
                    get_register(
                        process,
                        static_cast<RegisterId>(
                            op.stack.entries + pointer.aval)) = PackedLogic4::from_aval_bval(32, op.return_target, 0);
                    get_register(process, op.stack.pointer) = PackedLogic4::from_aval_bval(
                        32, pointer.aval + 1U, 0);
                    process.pc = op.target;
                } else if constexpr (std::is_same_v<OperationType, Return>) {
                    if (op.stack.capacity == 0) {
                        execute_dynamic_return(process, op);
                        return;
                    }
                    const auto pointer = get_register(process, op.stack.pointer).low_word();
                    if (pointer.bval != 0) {
                        fail(process, "call-stack pointer is unknown");
                    }
                    if (pointer.aval == 0
                        || pointer.aval > op.stack.capacity) {
                        fail(process, "call-stack underflow");
                    }
                    const auto next_pointer = pointer.aval - 1U;
                    const auto target = get_register(
                        process,
                        static_cast<RegisterId>(
                            op.stack.entries + next_pointer))
                                            .low_word();
                    if (target.bval != 0
                        || target.aval
                            >= process.program.operations.size()) {
                        fail(process, "call-stack return target is invalid");
                    }
                    get_register(process, op.stack.pointer) = PackedLogic4::from_aval_bval(
                        32, next_pointer, 0);
                    process.pc = static_cast<InstructionIndex>(target.aval);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePush>) {
                    push_callable_frame(process, op);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePop>) {
                    pop_callable_frame(process, op);
                } else if constexpr (std::is_same_v<OperationType, Branch>) {
                    const auto& condition = get_register(process, op.condition);
                    if (condition.width() != 1) {
                        fail(process, "branch condition must be scalar");
                    }
                    const auto value = condition.get(0);
                    InstructionIndex target { };
                    if (value != Logic4::zero && value != Logic4::one) {
                        if (op.unknown_policy
                            == UnknownBranchPolicy::when_false) {
                            target = op.when_false;
                        } else {
                            fail(
                                process,
                                "branch condition is unknown or high impedance");
                        }
                    } else {
                        target = value == Logic4::one ? op.when_true : op.when_false;
                    }
                    if (target >= process.program.operations.size()) {
                        fail(process, "branch target is outside the operation stream");
                    }
                    process.pc = target;
                } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, Assert>) {
                    const auto& condition = get_register(process, op.condition);
                    if (condition.width() != 1 || condition.get(0) != Logic4::one) {
                        const auto message = op.message.empty()
                            ? std::string_view { "assertion failed" }
                            : std::string_view { op.message };
                        if (op.severity != AssertionSeverity::failure
                            && report_hook) {
                            report_hook(
                                process.program.id,
                                message,
                                op.severity,
                                op.source,
                                scheduler.now(),
                                scheduler.delta());
                        }
                        if (op.severity == AssertionSeverity::failure) {
                            throw AssertionError(
                                process.program.id,
                                process.pc,
                                std::string { message },
                                op.severity,
                                op.source);
                        }
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Display>) {
                    if (op.postponed) {
                        scheduler.schedule(
                            SchedulerPhase::postponed,
                            process.program.id,
                            [this,
                                process_id = process.program.id,
                                text = op.text,
                                newline = op.newline](Scheduler& runtime) {
                                if (output_hook) {
                                    output_hook(
                                        process_id,
                                        text,
                                        newline,
                                        runtime.now(),
                                        runtime.delta());
                                }
                            });
                    } else if (output_hook) {
                        output_hook(
                            process.program.id,
                            op.text,
                            op.newline,
                            scheduler.now(),
                            scheduler.delta());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
                    auto text = make_formatted_output(
                        op.prefix,
                        op.suffix,
                        op.format,
                        get_register(process, op.source),
                        op.signed_decimal,
                        op.suppress_leading_zero,
                        op.minimum_width,
                        op.left_justify,
                        op.zero_pad,
                        op.scalar_kind);
                    if (op.postponed) {
                        scheduler.schedule(
                            SchedulerPhase::postponed,
                            process.program.id,
                            [this,
                                process_id = process.program.id,
                                text = std::move(text),
                                newline = op.newline](Scheduler& runtime) {
                                if (output_hook) {
                                    output_hook(
                                        process_id,
                                        text,
                                        newline,
                                        runtime.now(),
                                        runtime.delta());
                                }
                            });
                    } else if (output_hook) {
                        output_hook(
                            process.program.id,
                            text,
                            op.newline,
                            scheduler.now(),
                            scheduler.delta());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, StringDisplay>) {
                    auto text = op.prefix
                        + get_string_register(process, op.source)
                        + op.suffix;
                    if (op.postponed) {
                        scheduler.schedule(
                            SchedulerPhase::postponed,
                            process.program.id,
                            [this,
                                process_id = process.program.id,
                                text = std::move(text),
                                newline = op.newline](Scheduler& runtime) {
                                if (output_hook) {
                                    output_hook(
                                        process_id,
                                        text,
                                        newline,
                                        runtime.now(),
                                        runtime.delta());
                                }
                            });
                    } else if (output_hook) {
                        output_hook(
                            process.program.id,
                            text,
                            op.newline,
                            scheduler.now(),
                            scheduler.delta());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, StringReport>) {
                    const auto& encoded = get_register(process, op.severity);
                    if (encoded.width() != 2
                        || encoded.get(0) == Logic4::x
                        || encoded.get(0) == Logic4::z
                        || encoded.get(1) == Logic4::x
                        || encoded.get(1) == Logic4::z) {
                        fail(
                            process,
                            "VHDL severity expression produced an invalid value");
                    }
                    const auto ordinal = (encoded.get(0) == Logic4::one ? 1U : 0U)
                        | (encoded.get(1) == Logic4::one ? 2U : 0U);
                    const auto severity = static_cast<AssertionSeverity>(ordinal);
                    const auto& message = get_string_register(process, op.message);
                    if (severity == AssertionSeverity::failure) {
                        if (op.standalone && report_hook) {
                            report_hook(
                                process.program.id,
                                message,
                                severity,
                                op.source,
                                scheduler.now(),
                                scheduler.delta());
                        }
                        throw AssertionError(
                            process.program.id,
                            process.pc,
                            message.empty()
                                ? (op.standalone
                                          ? "report failure"
                                          : "assertion failed")
                                : message,
                            severity,
                            op.source,
                            op.standalone);
                    }
                    if (report_hook) {
                        report_hook(
                            process.program.id,
                            message,
                            severity,
                            op.source,
                            scheduler.now(),
                            scheduler.delta());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
                    auto text = make_time_output(
                        op.prefix,
                        op.suffix,
                        scheduler.now(),
                        time_format,
                        op.use_timeformat_width,
                        op.minimum_width,
                        op.left_justify,
                        op.zero_pad);
                    if (op.postponed) {
                        scheduler.schedule(
                            SchedulerPhase::postponed,
                            process.program.id,
                            [this,
                                process_id = process.program.id,
                                text = std::move(text),
                                newline = op.newline](Scheduler& runtime) {
                                if (output_hook) {
                                    output_hook(
                                        process_id,
                                        text,
                                        newline,
                                        runtime.now(),
                                        runtime.delta());
                                }
                            });
                    } else if (output_hook) {
                        output_hook(
                            process.program.id,
                            text,
                            op.newline,
                            scheduler.now(),
                            scheduler.delta());
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
                    install_monitor(process.program.id, op);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
                    set_monitor_enabled(op.enabled);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, CoverageSample>) {
                    if (!coverage_sample_hook) {
                        fail(process, "coverage sampling service is unavailable");
                    }
                    std::vector<PackedLogic4> actuals;
                    actuals.reserve(op.actuals.size());
                    for (const auto actual : op.actuals) {
                        actuals.push_back(get_register(process, actual));
                    }
                    coverage_sample_hook(
                        op.instance_identity, actuals, op.signed_actuals,
                        op.trigger);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, CoverageQuery>) {
                    if (op.kind != CoverageQueryKind::overall_type
                        && op.kind
                            != CoverageQueryKind::overall_instance) {
                        fail(process, "coverage query kind is invalid");
                    }
                    if (!coverage_query_hook) {
                        fail(process, "coverage query service is unavailable");
                    }
                    auto value = coverage_query_hook(op.kind);
                    if (value.width() != 64U || value.is_logic9()) {
                        fail(process, "coverage query service returned an invalid real payload");
                    }
                    get_register(process, op.destination) = std::move(value);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
                    const auto maximum = op.maximum
                        ? std::optional<PackedLogic4> {
                              get_register(process, *op.maximum)
                          }
                        : std::nullopt;
                    const auto minimum = op.minimum
                        ? std::optional<PackedLogic4> {
                              get_register(process, *op.minimum)
                          }
                        : std::nullopt;
                    get_register(process, op.destination) = random_value(
                        process.program.id,
                        op.kind,
                        maximum,
                        minimum);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, RandomDistribution>) {
                    const auto integer_operand = [&](const RegisterId register_id) {
                        const auto& operand = get_register(process, register_id);
                        if (operand.width() != 32U || operand.is_logic9()
                            || operand.low_word().bval != 0U) {
                            fail(process,
                                "random distribution operand must be a known 32-bit integer");
                        }
                        return std::bit_cast<std::int32_t>(
                            static_cast<std::uint32_t>(operand.low_word().aval));
                    };
                    const auto seed = integer_operand(op.seed);
                    const auto first = integer_operand(op.first);
                    const auto second = op.second
                        ? std::optional<std::int32_t> {
                              integer_operand(*op.second)
                          }
                        : std::nullopt;
                    const auto evaluated = evaluate_random_distribution(
                        op.kind, seed, first, second);
                    get_register(process, op.destination)
                        = PackedLogic4::from_aval_bval(
                            32U,
                            std::bit_cast<std::uint32_t>(evaluated.value),
                            0U);
                    get_register(process, op.seed)
                        = PackedLogic4::from_aval_bval(
                            32U,
                            std::bit_cast<std::uint32_t>(evaluated.seed),
                            0U);
                    ++process.pc;
                } else if constexpr (
                    std::is_same_v<OperationType, ScopeRandomize>) {
                    SystemVerilogScopeRandomizeRequest request;
                    request.limits.maximum_domain_values = op.maximum_domain_values;
                    request.selection = (static_cast<std::uint64_t>(next_random(process)) << 32U)
                        | next_random(process);
                    request.variables.reserve(op.targets.size());
                    for (const auto& target : op.targets) {
                        request.variables.push_back({ target.canonical_identity,
                            { target.domain_kind == ScopeRandomizeDomainKind::enumeration
                                    ? SystemVerilogConstraintDomainKind::Enumeration
                                    : target.domain_kind
                                        == ScopeRandomizeDomainKind::integer
                                    ? SystemVerilogConstraintDomainKind::Integer
                                    : SystemVerilogConstraintDomainKind::BitVector,
                                target.width,
                                target.signed_value,
                                target.nominal_type },
                            target.domain,
                            &get_register(process, target.target) });
                    }
                    if (!op.inline_constraints.empty()) {
                        request.inline_constraints = [&](auto& solver, const auto& variables) {
                            configure_systemverilog_inline_constraints(
                                solver,
                                variables,
                                op.inline_constraints,
                                process.program.name + "::std::randomize@"
                                    + std::to_string(process.pc));
                        };
                    }
                    const auto result = randomize_systemverilog_scope(request);
                    get_register(process, op.destination) = PackedLogic4::from_aval_bval(
                        32, result.language_result(), 0);
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Report>) {
                    if (report_hook) {
                        report_hook(
                            process.program.id,
                            op.message,
                            op.severity,
                            op.source,
                            scheduler.now(),
                            scheduler.delta());
                    }
                    if (op.severity == AssertionSeverity::failure) {
                        throw AssertionError(
                            process.program.id,
                            process.pc,
                            op.message.empty() ? "report failure" : op.message,
                            op.severity,
                            op.source,
                            true);
                    }
                    ++process.pc;
                } else if constexpr (std::is_same_v<OperationType, Pause>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, Stop>) {
                    boundary = true;
                } else if constexpr (std::is_same_v<OperationType, Halt>) {
                    boundary = true;
                } else {
                    if constexpr (requires {
                                      execute_file(process, op);
                                  }) {
                        execute_file(process, op);
                    } else if constexpr (requires {
                                             execute_container(process, op);
                                         }) {
                        execute_container(process, op);
                    } else {
                        static_assert(
                            sizeof(op) == 0,
                            "unhandled SimIR operation");
                    }
                }
                    }();
                }
            },
            operation);

        if (boundary) {
            const bool debug_boundary = fsim::runtime::simir::operation_holds<DebugPoint>(operation);
            const bool immediate_process_boundary = is_immediate_process_boundary(operation);
            const bool synchronization_boundary = is_synchronization_boundary(operation);
            handle_boundary(process, instruction, instruction + 1);
            if ((immediate_process_boundary
                    || (synchronization_boundary
                        && process.status == ProcessStatus::running
                        && !process.queued))
                && !scheduler.stop_requested()) {
                continue;
            }
            if (!debug_boundary || scheduler.stop_requested()) {
                snapshot_callable_context(process);
                return;
            }
        }
    }
}
} // namespace fsim::runtime::simir
