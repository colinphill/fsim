// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scope_randomize.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_execution_context.hpp"
#include "simir_internal.hpp"

#include "simir_signal_attributes.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numeric>

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
    const auto required = [&](const auto& value,
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
            ? std::make_shared<ContainerValue>(
                process.executor->read_container_register(register_id))
            : container_register_storage(process, register_id);
        frame.storage_bytes += sizeof(ContainerValue)
            + container_value_storage_bytes(*value);
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
    std::vector<SharedContainerValue> preserved_containers;
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
            ? std::make_shared<ContainerValue>(
                process.executor->read_container_register(register_id))
            : container_register_storage(process, register_id);
        account_preserved_storage(
            sizeof(ContainerValue) + container_value_storage_bytes(*value));
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
                frame.container_ids[index], *frame.containers[index]);
        } else {
            set_container_register_storage(
                process, frame.container_ids[index],
                std::move(frame.containers[index]));
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
                *preserved_containers[index]);
        } else {
            set_container_register_storage(
                process, operation.preserve_containers[index],
                std::move(preserved_containers[index]));
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
            ? std::make_shared<ContainerValue>(
                process.executor->read_container_register(register_id))
            : container_register_storage(process, register_id);
        account_context_storage(
            sizeof(ContainerValue) + container_value_storage_bytes(*value));
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
                context.container_ids[index], *context.containers[index]);
        } else {
            set_container_register_storage(
                process, context.container_ids[index],
                std::move(context.containers[index]));
        }
    }
}

#include "simir_execution_boundaries.tpp"

bool Interpreter::clear_process_executor(const ProcessId process)
{
    if (impl_->started) {
        return false;
    }
    auto& state = impl_->get_process(process);
    state.executor.reset();
    state.deferred_executor.reset();
    return true;
}

void Interpreter::Impl::install_deferred_executor(ProcessState& process)
{
    auto executor = process.deferred_executor->take();
    if (!executor) {
        throw std::logic_error {
            "deferred SimIR process executor produced a null executor"
        };
    }
    if (process.frame) {
        for (std::size_t register_index = 0;
             register_index < process.frame->registers.size();
             ++register_index) {
            const auto& value = process.frame->registers[register_index];
            if (value.width() != 0) {
                executor->write_register(
                    static_cast<RegisterId>(register_index), value);
            }
        }
        for (std::size_t register_index = 0;
             register_index < process.frame->string_registers.size();
             ++register_index) {
            executor->write_string_register(
                static_cast<StringRegisterId>(register_index),
                process.frame->string_registers[register_index]);
        }
        for (std::size_t register_index = 0;
             register_index < process.frame->container_registers.size();
             ++register_index) {
            executor->write_container_register_storage(
                static_cast<ContainerRegisterId>(register_index),
                process.frame->container_registers[register_index]);
        }
    } else {
        for (std::size_t register_index = 0;
             register_index < process.program.container_register_types.size();
             ++register_index) {
            executor->write_container_register_storage(
                static_cast<ContainerRegisterId>(register_index),
                default_container_register(
                    process.program.container_register_types[register_index]));
        }
    }
    executor->redirect(process.pc);
    process.executor = std::move(executor);
    process.deferred_executor.reset();
    process.frame.reset();
}

[[nodiscard]] bool Interpreter::Impl::handle_executor_resume(
    ProcessState& process, const ProcessResumeResult& boundary)
{
    const auto* operation
        = boundary.instruction < process.program.operations.size()
        ? &std::as_const(process.program.operations)[boundary.instruction]
        : nullptr;
    if (boundary.external.kind != ExternalSuspendKind::simir_boundary) {
        handle_external_boundary(
            process,
            boundary.instruction,
            boundary.next_instruction,
            boundary.external);
        snapshot_callable_context(process);
        return false;
    }
    if (native_process_count_profile_enabled && operation) {
        ++native_process_simir_boundary_groups[operation->storage.index()];
        if (const auto* scheduling = std::get_if<SchedulingOperationGroup>(
                &operation->storage)) {
            ++native_process_scheduling_boundaries[
                scheduling->storage.index()];
        }
    }
    const bool debug_boundary
        = operation && operation_holds<DebugPoint>(*operation);
    const bool class_boundary
        = operation && is_class_execution_boundary(*operation);
    const bool immediate_process_boundary
        = operation && is_immediate_process_boundary(*operation);
    const bool synchronization_boundary
        = operation && is_synchronization_boundary(*operation);
    const bool callable_boundary
        = operation && is_dynamic_callable_boundary(*operation);
    handle_boundary(
        process, boundary.instruction, boundary.next_instruction);
    if ((class_boundary || callable_boundary || immediate_process_boundary
            || (synchronization_boundary
                && process.status == ProcessStatus::running
                && !process.queued))
        && !scheduler.stop_requested()) {
        return true;
    }
    if (!debug_boundary || scheduler.stop_requested()) {
        snapshot_callable_context(process);
        return false;
    }
    return true;
}

#include "simir_execution_interpreter.tpp"
void Interpreter::Impl::execute_static_cohort(
    const std::span<const ProcessId> process_ids)
{
    if (process_ids.size() < 2U || process_profile_enabled) {
        for (const auto id : process_ids) {
            if (scheduler.stop_requested()) {
                break;
            }
            auto& state = get_process(id);
            state.queued = false;
            state.waiting_on_static = false;
            remove_dynamic_wait(state);
            execute(id);
        }
        return;
    }

    if (scheduler.stop_requested()) {
        return;
    }
    for (const auto id : process_ids) {
        auto& state = get_process(id);
        restore_callable_context(state);
        if (!state.executor && state.deferred_executor
            && can_install_deferred_executor(state)
            && state.deferred_executor->ready()) {
            install_deferred_executor(state);
        }
    }

    constexpr std::size_t inline_cohort_capacity = 512U;
    std::array<std::optional<ExecutionContext>, inline_cohort_capacity>
        inline_contexts;
    std::array<ProcessCohortResumeEntry, inline_cohort_capacity>
        inline_entries;
    std::vector<ExecutionContext> overflow_contexts;
    std::vector<ProcessCohortResumeEntry> overflow_entries;
    static const bool inline_cohort_buffers_enabled
        = std::getenv("FSIM_DISABLE_INLINE_COHORT_BUFFERS") == nullptr;
    std::size_t begin = 0U;
    while (begin < process_ids.size() && !scheduler.stop_requested()) {
        auto* const state = &get_process(process_ids[begin]);
        if (state->suspended || state->halted || !state->executor) {
            state->queued = false;
            state->waiting_on_static = false;
            remove_dynamic_wait(*state);
            execute(state->program.id);
            ++begin;
            continue;
        }

        const auto* const cohort_domain
            = state->executor->cohort_domain();
        const bool state_aware_cohort
            = state->executor->cohort_manages_process_state();
        if (cohort_domain == nullptr) {
            state->queued = false;
            state->waiting_on_static = false;
            remove_dynamic_wait(*state);
            execute(state->program.id);
            ++begin;
            continue;
        }
        auto end = begin + 1U;
        while (end < process_ids.size()) {
            const auto& candidate = get_process(process_ids[end]);
            if (candidate.suspended || candidate.halted
                || !candidate.executor
                || candidate.executor->cohort_domain() != cohort_domain
                || candidate.executor->cohort_manages_process_state()
                    != state_aware_cohort) {
                break;
            }
            ++end;
        }
        if (end - begin < 2U) {
            state->queued = false;
            state->waiting_on_static = false;
            remove_dynamic_wait(*state);
            execute(state->program.id);
            begin = end;
            continue;
        }

        const auto count = end - begin;
        if (!state_aware_cohort) {
            for (auto index = begin; index < end; ++index) {
                auto& member = get_process(process_ids[index]);
                member.queued = false;
                member.waiting_on_static = false;
                remove_dynamic_wait(member);
            }
        }
        std::span<ProcessCohortResumeEntry> entries;
        if (inline_cohort_buffers_enabled
            && count <= inline_cohort_capacity) {
            for (std::size_t offset = 0U; offset < count; ++offset) {
                auto& member = get_process(process_ids[begin + offset]);
                if (!state_aware_cohort) {
                    member.status = ProcessStatus::running;
                }
                inline_contexts[offset].emplace(*this, member.program.id);
                inline_entries[offset] = {
                    member.executor.get(),
                    &*inline_contexts[offset],
                    member.pc,
                    { },
                    { },
                    &member.queued,
                    &member.waiting_on_static,
                    &member.status
                };
            }
            entries = std::span<ProcessCohortResumeEntry> {
                inline_entries.data(), count
            };
        } else {
            overflow_contexts.clear();
            overflow_entries.clear();
            overflow_contexts.reserve(count);
            overflow_entries.reserve(count);
            for (auto index = begin; index < end; ++index) {
                auto& member = get_process(process_ids[index]);
                if (!state_aware_cohort) {
                    member.status = ProcessStatus::running;
                }
                overflow_contexts.emplace_back(*this, member.program.id);
                overflow_entries.push_back({
                    member.executor.get(),
                    &overflow_contexts.back(),
                    member.pc,
                    { },
                    { },
                    &member.queued,
                    &member.waiting_on_static,
                    &member.status
                });
            }
            entries = overflow_entries;
        }

        const auto executed
            = entries.front().executor->resume_cohort(entries);
        if (native_phase_profile_enabled) {
            ++native_phase_profile_cohort_resumes;
            native_phase_profile_cohort_members += executed;
        }
        if (executed > entries.size()) {
            throw std::logic_error {
                "process cohort executor returned an invalid activation count"
            };
        }
        if (native_process_count_profile_enabled) {
            for (std::size_t offset = 0; offset < executed; ++offset) {
                const auto id = process_ids[begin + offset];
                if (id >= native_process_resume_counts.size()) {
                    const auto size = static_cast<std::size_t>(id) + 1U;
                    native_process_resume_counts.resize(size);
                    native_process_single_resume_counts.resize(size);
                    native_process_single_static_wait_counts.resize(size);
                    native_process_cohort_resume_counts.resize(size);
                    native_process_cohort_static_wait_counts.resize(size);
                    native_process_word_fanout_ready_counts.resize(size);
                }
                ++native_process_resume_counts[id];
                ++native_process_cohort_resume_counts[id];
                ++native_process_cohort_boundary_counts[
                    static_cast<std::size_t>(
                        entries[offset].result.external.kind)];
                if (entries[offset].result.external.kind
                    == ExternalSuspendKind::wait_sensitivity) {
                    ++native_process_cohort_static_wait_counts[id];
                }
            }
        }
        if (executed == 0U) {
            state->queued = false;
            state->waiting_on_static = false;
            remove_dynamic_wait(*state);
            execute(state->program.id);
            ++begin;
            continue;
        }

        std::size_t local = 0U;
        for (; local < executed; ++local) {
            auto* const member
                = &get_process(process_ids[begin + local]);
            if (entries[local].failure) {
                std::rethrow_exception(entries[local].failure);
            }
            if (handle_executor_resume(*member, entries[local].result)) {
                execute(member->program.id);
            }
            if (scheduler.stop_requested()) {
                return;
            }
        }
        begin += local;
    }
}

void Interpreter::Impl::build_native_static_regions()
{
    if (native_static_regions_built) {
        return;
    }
    native_static_regions_built = true;
    const auto no_region = std::numeric_limits<std::size_t>::max();
    native_static_region_by_process.assign(processes.size(), no_region);
    native_static_region_offset_by_process.assign(processes.size(), no_region);
    if (std::getenv("FSIM_ENABLE_NATIVE_STATIC_REGIONS") == nullptr
        || process_profile_enabled || execution_point_hook) {
        return;
    }

    std::vector<bool> candidate(processes.size(), false);
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    for (std::size_t id = 0; id < processes.size(); ++id) {
        auto& state = processes[id];
        const auto& process = state.program;
        const auto cohort = id < static_sensitivity_cohort_by_process.size()
            ? static_sensitivity_cohort_by_process[id]
            : no_cohort;
        const auto wait_count = std::ranges::count_if(
            process.operations,
            [](const auto& operation) {
                return operation_holds<WaitSensitivity>(operation);
            });
        const bool static_region_shape
            = !process.static_sensitivity.empty()
            && wait_count == 1
            && !process.postponed && !process.reactive && !process.observed
            && (cohort == no_cohort
                || static_sensitivity_cohorts[cohort].members.size() < 2U);
        if (!static_region_shape) {
            continue;
        }
        if (!state.executor && state.deferred_executor
            && can_install_deferred_executor(state)
            && state.deferred_executor->ready()) {
            install_deferred_executor(state);
        }
        candidate[id] = state.executor
            && state.executor->cohort_domain() != nullptr
            && state.executor->cohort_manages_process_state();
    }
    std::vector<std::size_t> parent(processes.size());
    std::iota(parent.begin(), parent.end(), std::size_t { });
    const auto root = [&](std::size_t id) {
        while (parent[id] != id) {
            parent[id] = parent[parent[id]];
            id = parent[id];
        }
        return id;
    };
    const auto join = [&](const std::size_t left,
                          const std::size_t right) {
        const auto left_root = root(left);
        const auto right_root = root(right);
        if (left_root != right_root) {
            parent[right_root] = left_root;
        }
    };
    for (std::size_t id = 0; id < processes.size(); ++id) {
        if (!candidate[id]) {
            continue;
        }
        std::set<SignalId> outputs;
        for (const auto& driver : processes[id].program.driver_regions) {
            outputs.insert(driver.signal);
        }
        if (outputs.empty()) {
            for (const auto& operation : processes[id].program.operations) {
                if (const auto signal = output_signal(operation)) {
                    outputs.insert(*signal);
                }
            }
        }
        for (const auto signal : outputs) {
            if (!can_publish_native_word(
                    signal, static_cast<ProcessId>(id))) {
                continue;
            }
            for (const auto& fanout : static_fanout[signal]) {
                const auto target
                    = static_cast<std::size_t>(fanout.process);
                if (fanout.edge == EdgeKind::any
                    && target < candidate.size() && candidate[target]) {
                    join(id, target);
                }
            }
        }
    }

    std::map<std::size_t, std::vector<ProcessId>> components;
    for (std::size_t id = 0; id < candidate.size(); ++id) {
        if (candidate[id]) {
            components[root(id)].push_back(static_cast<ProcessId>(id));
        }
    }
    for (auto& [component, members] : components) {
        (void)component;
        if (members.size() < 2U) {
            continue;
        }
        const auto region_id = native_static_regions.size();
        NativeStaticRegion region;
        region.members = std::move(members);
        region.active.assign(region.members.size(), 0U);
        region.ready_offsets.reserve(region.members.size());
        region.contexts.reserve(region.members.size());
        region.entries.resize(region.members.size());
        for (std::size_t offset = 0; offset < region.members.size();
             ++offset) {
            const auto id = region.members[offset];
            region.contexts.push_back(
                std::make_unique<ExecutionContext>(*this, id));
            auto& state = get_process(id);
            region.entries[offset] = ProcessCohortResumeEntry {
                state.executor.get(), region.contexts[offset].get(), state.pc,
                { }, { }, &state.queued, &state.waiting_on_static,
                &state.status, &region.active[offset]
            };
            native_static_region_by_process[id] = region_id;
            native_static_region_offset_by_process[id] = offset;
        }
        native_static_regions.push_back(std::move(region));
    }
}

std::size_t Interpreter::Impl::execute_native_static_region(
    const std::size_t region_id,
    const std::span<const ProcessId> ready)
{
    ++native_static_region_attempts;
    native_static_region_ready += ready.size();
    if (region_id >= native_static_regions.size() || ready.size() < 2U
        || scheduler.stop_requested()) {
        ++native_static_region_declines[0];
        return 0U;
    }
    auto& region = native_static_regions[region_id];
    std::ranges::fill(region.active, UINT8_C(0));
    region.ready_offsets.clear();
    for (const auto id : ready) {
        if (id >= native_static_region_by_process.size()
            || native_static_region_by_process[id] != region_id) {
            return 0U;
        }
        const auto offset = native_static_region_offset_by_process[id];
        region.active[offset] = 1U;
        region.ready_offsets.push_back(offset);
    }
    if (!region.prepared) {
        for (std::size_t offset = 0; offset < region.members.size();
             ++offset) {
            auto& state = get_process(region.members[offset]);
            restore_callable_context(state);
            if (!state.executor && state.deferred_executor
                && can_install_deferred_executor(state)
                && state.deferred_executor->ready()) {
                install_deferred_executor(state);
            }
            if (!state.executor
                || state.executor->cohort_domain() == nullptr
                || !state.executor->cohort_manages_process_state()) {
                ++native_static_region_declines[1];
                return 0U;
            }
            auto& entry = region.entries[offset];
            entry.executor = state.executor.get();
            entry.start_instruction = state.pc;
        }
        region.prepared = true;
    }
    for (const auto id : ready) {
        auto& state = get_process(id);
        const auto offset = native_static_region_offset_by_process[id];
        auto& entry = region.entries[offset];
        if (state.executor.get() != entry.executor || state.suspended
            || state.halted || !state.waiting_on_static) {
            ++native_static_region_declines[2];
            return 0U;
        }
        restore_callable_context(state);
        entry.start_instruction = state.pc;
        entry.result = { };
        entry.failure = { };
    }

    const auto scanned = region.entries.front().executor->resume_region(
        region.entries, region.ready_offsets);
    if (scanned == 0U || scanned > region.entries.size()) {
        ++native_static_region_declines[3];
        return 0U;
    }
    ++native_static_region_calls;
    std::size_t consumed { };
    for (const auto id : ready) {
        const auto offset = native_static_region_offset_by_process[id];
        if (offset >= scanned) {
            break;
        }
        auto& state = get_process(id);
        auto& entry = region.entries[offset];
        if (entry.failure) {
            std::rethrow_exception(entry.failure);
        }
        if (native_process_count_profile_enabled) {
            ++native_process_resume_counts[id];
            ++native_process_single_resume_counts[id];
            ++native_process_single_boundary_counts[
                static_cast<std::size_t>(entry.result.external.kind)];
            if (entry.result.external.kind
                == ExternalSuspendKind::wait_sensitivity) {
                ++native_process_single_static_wait_counts[id];
            }
        }
        if (handle_executor_resume(state, entry.result)) {
            execute(id);
        }
        ++consumed;
        if (scheduler.stop_requested()) {
            break;
        }
    }
    native_static_region_consumed += consumed;
    if (consumed != ready.size()) {
        ++native_static_region_declines[4];
    }
    return consumed;
}

SchedulerBatchResult Interpreter::Impl::execute(
    Scheduler& runtime,
    const std::span<const std::uint64_t> cohort_ids)
{
    SchedulerBatchResult result;
    const auto phase_revision = runtime.current_phase_revision();
    std::size_t batch_index { };
    while (batch_index < cohort_ids.size()) {
        const auto raw_cohort = cohort_ids[batch_index];
        if ((raw_cohort & native_static_region_payload) != 0U) {
            const auto process = static_cast<ProcessId>(
                raw_cohort & ~native_static_region_payload);
            const auto no_region = std::numeric_limits<std::size_t>::max();
            const auto region
                = process < native_static_region_by_process.size()
                ? native_static_region_by_process[process]
                : no_region;
            if (region == no_region) {
                result.failure = std::make_exception_ptr(
                    std::logic_error(
                        "scheduler batch references an invalid native "
                        "static-region process"));
                ++result.executed;
                break;
            }
            auto end = batch_index + 1U;
            while (end < cohort_ids.size()
                && (cohort_ids[end] & native_static_region_payload) != 0U) {
                const auto candidate = static_cast<ProcessId>(
                    cohort_ids[end] & ~native_static_region_payload);
                if (candidate >= native_static_region_by_process.size()
                    || native_static_region_by_process[candidate] != region) {
                    break;
                }
                ++end;
            }
            std::vector<ProcessId> ready;
            ready.reserve(end - batch_index);
            for (auto index = batch_index; index < end; ++index) {
                ready.push_back(static_cast<ProcessId>(
                    cohort_ids[index] & ~native_static_region_payload));
            }
            std::size_t consumed { };
            try {
                consumed = execute_native_static_region(region, ready);
            } catch (...) {
                result.failure = std::current_exception();
            }
            if (result.failure) {
                break;
            }
            if (consumed == 0U) {
                if (result.executed == 0U) {
                    return result;
                }
                break;
            }
            if (consumed > ready.size()) {
                result.failure = std::make_exception_ptr(
                    std::logic_error(
                        "native static region consumed an invalid process "
                        "count"));
                break;
            }
            result.executed += consumed;
            batch_index += consumed;
            if (consumed != ready.size()) {
                break;
            }
            if (runtime.stop_requested()
                || runtime.current_phase_revision() != phase_revision) {
                break;
            }
            continue;
        }
        if (raw_cohort >= static_sensitivity_cohorts.size()) {
            result.failure = std::make_exception_ptr(
                std::logic_error(
                    "scheduler batch references an invalid static cohort"));
            ++result.executed;
            break;
        }
        auto& cohort = static_sensitivity_cohorts[
            static_cast<std::size_t>(raw_cohort)];
        if (cohort.ready.empty()) {
            result.failure = std::make_exception_ptr(
                std::logic_error(
                    "scheduler batch references an empty static cohort"));
            ++result.executed;
            break;
        }
        auto ready = std::move(cohort.ready);
        cohort.ready.clear();
        try {
            execute_static_cohort(ready);
        } catch (...) {
            result.failure = std::current_exception();
        }
        ++result.executed;
        ++batch_index;
        if (result.failure || runtime.stop_requested()
            || runtime.current_phase_revision() != phase_revision) {
            break;
        }
    }
    return result;
}
} // namespace fsim::runtime::simir
