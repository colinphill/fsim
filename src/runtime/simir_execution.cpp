// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scope_randomize.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_execution_context.hpp"
#include "simir_internal.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/path.hpp"

#include "simir_signal_attributes.hpp"
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numeric>

#include <boost/multiprecision/cpp_int.hpp>

namespace fsim::runtime::simir {

namespace {

    struct VhdlAssertFormatField {
        char variable { };
        char alignment { };
        char fill { ' ' };
        std::size_t width { };
        std::string_view precision;
    };

    [[nodiscard]] bool parse_vhdl_assert_format_field(
        const std::string_view text, VhdlAssertFormatField& field)
    {
        if (text.empty()
            || (text.front() != 's' && text.front() != 'S'
                && text.front() != 'r' && text.front() != 't'
                && text.front() != 'i')) {
            return false;
        }
        field.variable = text.front();
        auto rest = text.substr(1U);
        if (!rest.empty() && rest.front() == '.') {
            if (field.variable != 't') {
                return false;
            }
            field.precision = rest.substr(1U);
            rest = { };
        } else if (!rest.empty()) {
            if (rest.front() != ':') {
                return false;
            }
            rest.remove_prefix(1U);
            if (rest.size() >= 2U
                && (rest[1] == '<' || rest[1] == '>' || rest[1] == '^')) {
                field.fill = rest.front();
                field.alignment = rest[1];
                rest.remove_prefix(2U);
            } else if (!rest.empty()
                && (rest.front() == '<' || rest.front() == '>'
                    || rest.front() == '^')) {
                field.alignment = rest.front();
                rest.remove_prefix(1U);
            }
            const auto dot = rest.find('.');
            const auto width = rest.substr(0U, dot);
            if (!width.empty()) {
                std::size_t parsed { };
                const auto converted = std::from_chars(
                    width.data(), width.data() + width.size(), parsed);
                if (converted.ec != std::errc { }
                    || converted.ptr != width.data() + width.size()
                    || parsed > maximum_string_bytes) {
                    return false;
                }
                field.width = parsed;
            }
            if (dot != std::string_view::npos) {
                if (field.variable != 't') {
                    return false;
                }
                field.precision = rest.substr(dot + 1U);
            }
            rest = { };
        }
        if (!rest.empty()) {
            return false;
        }
        if (!field.precision.empty()
            && field.precision != "fs" && field.precision != "ps"
            && field.precision != "ns" && field.precision != "us"
            && field.precision != "ms" && field.precision != "sec"
            && field.precision != "min" && field.precision != "hr") {
            return false;
        }
        if (field.alignment == 0) {
            field.alignment = field.variable == 't' ? '>' : '<';
        }
        return true;
    }

    [[nodiscard]] bool valid_vhdl_assert_format(
        const std::string_view format)
    {
        if (format.size() > maximum_string_bytes) {
            return false;
        }
        for (std::size_t index = 0U; index < format.size();) {
            if (format[index] == '}') {
                if (index + 1U < format.size()
                    && format[index + 1U] == '}') {
                    index += 2U;
                    continue;
                }
                return false;
            }
            if (format[index] != '{') {
                ++index;
                continue;
            }
            if (index + 1U < format.size() && format[index + 1U] == '{') {
                index += 2U;
                continue;
            }
            const auto end = format.find('}', index + 1U);
            if (end == std::string_view::npos) {
                return false;
            }
            VhdlAssertFormatField field;
            if (!parse_vhdl_assert_format_field(
                    format.substr(index + 1U, end - index - 1U), field)) {
                return false;
            }
            index = end + 1U;
        }
        return true;
    }

    [[nodiscard]] std::string vhdl_assert_time(
        const SimulationTick tick,
        const std::uint64_t resolution_femtoseconds,
        const std::string_view precision)
    {
        using boost::multiprecision::cpp_int;
        std::uint64_t unit = 1U;
        std::string_view suffix = precision;
        if (precision == "ps") unit = 1'000U;
        else if (precision == "ns") unit = 1'000'000U;
        else if (precision == "us") unit = 1'000'000'000U;
        else if (precision == "ms") unit = 1'000'000'000'000U;
        else if (precision == "sec") unit = 1'000'000'000'000'000U;
        else if (precision == "min") unit = 60'000'000'000'000'000U;
        else if (precision == "hr") unit = 3'600'000'000'000'000'000U;
        else if (precision.empty()) suffix = "fs";
        const cpp_int femtoseconds = cpp_int { tick }
            * resolution_femtoseconds;
        cpp_int rounded = femtoseconds / unit;
        if ((femtoseconds % unit) * 2 >= unit) {
            ++rounded;
        }
        return rounded.convert_to<std::string>() + " "
            + std::string { suffix };
    }

    [[nodiscard]] std::string aligned_vhdl_assert_field(
        std::string value, const VhdlAssertFormatField& field)
    {
        if (value.size() >= field.width) {
            return value;
        }
        const auto padding = field.width - value.size();
        if (field.alignment == '<') {
            value.append(padding, field.fill);
        } else if (field.alignment == '>') {
            value.insert(0U, padding, field.fill);
        } else {
            const auto left = padding / 2U;
            value.insert(0U, left, field.fill);
            value.append(padding - left, field.fill);
        }
        return value;
    }

    [[nodiscard]] std::string format_vhdl_assert_message(
        const std::string_view format,
        const std::string_view message,
        const AssertionSeverity severity,
        const std::string_view instance,
        const SimulationTick tick,
        const std::uint64_t resolution_femtoseconds)
    {
        std::string result;
        result.reserve(std::min<std::size_t>(
            maximum_string_bytes, format.size() + message.size()));
        for (std::size_t index = 0U; index < format.size();) {
            if (format[index] == '}' && index + 1U < format.size()
                && format[index + 1U] == '}') {
                result.push_back('}');
                index += 2U;
                continue;
            }
            if (format[index] != '{') {
                result.push_back(format[index++]);
                continue;
            }
            if (index + 1U < format.size() && format[index + 1U] == '{') {
                result.push_back('{');
                index += 2U;
                continue;
            }
            const auto end = format.find('}', index + 1U);
            VhdlAssertFormatField field;
            if (end == std::string_view::npos
                || !parse_vhdl_assert_format_field(
                    format.substr(index + 1U, end - index - 1U), field)) {
                throw std::logic_error { "invalid retained VHDL assert format" };
            }
            std::string value;
            if (field.variable == 'r') {
                value = message;
            } else if (field.variable == 'i') {
                value = instance;
            } else if (field.variable == 't') {
                value = vhdl_assert_time(
                    tick, resolution_femtoseconds, field.precision);
            } else {
                static constexpr std::array names {
                    std::string_view { "note" },
                    std::string_view { "warning" },
                    std::string_view { "error" },
                    std::string_view { "failure" } };
                value = names[static_cast<std::size_t>(severity)];
                if (field.variable == 'S') {
                    std::ranges::transform(value, value.begin(), [](char ch) {
                        return static_cast<char>(
                            ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
                    });
                }
            }
            value = aligned_vhdl_assert_field(std::move(value), field);
            if (value.size() > maximum_string_bytes
                || result.size() > maximum_string_bytes - value.size()) {
                throw std::length_error {
                    "formatted VHDL report exceeds the bounded string limit" };
            }
            result += value;
            index = end + 1U;
        }
        return result;
    }

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
    if (process.callable_frames.back().escaping_context) {
        capture_callable_values(
            process, *process.callable_frames.back().escaping_context);
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
    std::set<RegisterId> shadowed_packed;
    std::set<StringRegisterId> shadowed_strings;
    std::set<ContainerRegisterId> shadowed_containers;
    for (const auto& frame : process.callable_frames) {
        shadowed_packed.insert(
            frame.packed_ids.begin(), frame.packed_ids.end());
        shadowed_strings.insert(
            frame.string_ids.begin(), frame.string_ids.end());
        shadowed_containers.insert(
            frame.container_ids.begin(), frame.container_ids.end());
    }
    for (auto context = process.escaping_callable_contexts.rbegin();
        context != process.escaping_callable_contexts.rend(); ++context) {
        if (!*context) {
            fail(process, "escaping automatic callable context is null");
        }
        capture_callable_values(
            process, **context,
            shadowed_packed, shadowed_strings, shadowed_containers);
        shadowed_packed.insert(
            (*context)->packed_ids.begin(), (*context)->packed_ids.end());
        shadowed_strings.insert(
            (*context)->string_ids.begin(), (*context)->string_ids.end());
        shadowed_containers.insert(
            (*context)->container_ids.begin(),
            (*context)->container_ids.end());
    }

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
    for (const auto& context : process.escaping_callable_contexts) {
        if (!context) {
            fail(process, "escaping automatic callable context is null");
        }
        restore_callable_values(process, *context);
    }
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

void Interpreter::Impl::execute_vhdl_report(
    ProcessState& process,
    const InstructionIndex instruction,
    const std::string_view message,
    const AssertionSeverity severity,
    const SourceLocation& source,
    const bool standalone)
{
    const auto level = static_cast<std::size_t>(severity);
    if (level >= vhdl_assert_enabled.size()) {
        process.pc = instruction;
        fail(process, "VHDL report severity is invalid");
    }
    if (!vhdl_assert_enabled[level]) {
        return;
    }
    if (vhdl_assert_counts[level] != std::numeric_limits<std::uint64_t>::max()) {
        ++vhdl_assert_counts[level];
    }
    const auto formatted = format_vhdl_assert_message(
        vhdl_assert_formats[level], message, severity,
        process.program.name, scheduler.now(),
        time_format.resolution_femtoseconds);
    if (report_hook) {
        report_hook(
            process.program.id, formatted, severity, source,
            scheduler.now(), scheduler.delta());
    }
    if (severity == AssertionSeverity::failure) {
        throw AssertionError(
            process.program.id, instruction,
            formatted.empty()
                ? (standalone ? "report failure" : "assertion failed")
                : formatted,
            severity, source, true);
    }
}

void Interpreter::Impl::execute_vhdl_assert_api(
    ProcessState& process, const VhdlAssertApi& operation)
{
    using Kind = VhdlAssertApiKind;
    const auto packed = [&](const RegisterId id, const std::size_t width)
        -> PackedLogic4 {
        return process.executor
            ? process.executor->read_register(id, width)
            : get_register(process, id);
    };
    const auto write_packed = [&](const RegisterId id, PackedLogic4 value) {
        if (process.executor) {
            process.executor->write_register(id, value);
        } else {
            get_register(process, id) = std::move(value);
        }
    };
    const auto string_value = [&](const StringRegisterId id) {
        return process.executor
            ? process.executor->read_string_register(id)
            : get_string_register(process, id);
    };
    const auto write_string = [&](const StringRegisterId id,
                                  const std::string_view value) {
        if (process.executor) {
            process.executor->write_string_register(id, value);
        } else {
            get_string_register(process, id) = value;
        }
    };
    const auto decode_level = [&](const RegisterId id) {
        const auto value = packed(id, 2U);
        if (value.width() != 2U || value.is_logic9()
            || value.low_word().bval != 0U
            || value.low_word().aval >= 4U) {
            fail(process, "VHDL assert API severity is not SEVERITY_LEVEL");
        }
        return static_cast<std::size_t>(value.low_word().aval);
    };
    const auto decode_boolean = [&](const RegisterId id) {
        const auto value = packed(id, 1U);
        if (value.width() != 1U || value.is_logic9()
            || value.low_word().bval != 0U) {
            fail(process, "VHDL assert API enable is not BOOLEAN");
        }
        return (value.low_word().aval & 1U) != 0U;
    };
    const auto no_other_operands = [&] {
        return !operation.string_destination && !operation.enable
            && !operation.format && !operation.valid;
    };

    if ((operation.kind == Kind::is_failed
            || operation.kind == Kind::get_count)
        && operation.destination && no_other_operands()) {
        std::uint64_t result { };
        if (operation.level) {
            result = vhdl_assert_counts[decode_level(*operation.level)];
        } else {
            for (std::size_t level = 1U;
                level < vhdl_assert_counts.size(); ++level) {
                const auto maximum = operation.kind == Kind::get_count
                    ? static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())
                    : std::uint64_t { 1U };
                result = vhdl_assert_counts[level] > maximum - result
                    ? maximum : result + vhdl_assert_counts[level];
            }
        }
        if (operation.kind == Kind::is_failed) {
            result = result != 0U ? 1U : 0U;
        } else {
            result = std::min<std::uint64_t>(result,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()));
        }
        write_packed(*operation.destination,
            PackedLogic4::from_aval_bval(
                operation.kind == Kind::is_failed ? 1U : 64U,
                result, 0U));
        return;
    }
    if (operation.kind == Kind::clear && !operation.destination
        && !operation.string_destination && !operation.level
        && !operation.enable && !operation.format && !operation.valid) {
        vhdl_assert_counts.fill(0U);
        return;
    }
    if (operation.kind == Kind::set_enable && !operation.destination
        && !operation.string_destination && operation.enable
        && !operation.format && !operation.valid) {
        const auto enabled = decode_boolean(*operation.enable);
        if (operation.level) {
            vhdl_assert_enabled[decode_level(*operation.level)] = enabled;
        } else {
            vhdl_assert_enabled.fill(enabled);
        }
        return;
    }
    if (operation.kind == Kind::get_enable && operation.destination
        && operation.level && no_other_operands()) {
        const auto enabled = vhdl_assert_enabled[decode_level(*operation.level)];
        write_packed(*operation.destination,
            PackedLogic4::from_aval_bval(1U, enabled ? 1U : 0U, 0U));
        return;
    }
    if (operation.kind == Kind::set_format && !operation.destination
        && !operation.string_destination && operation.level
        && !operation.enable && operation.format) {
        const auto level = decode_level(*operation.level);
        const auto format = string_value(*operation.format);
        const auto valid = valid_vhdl_assert_format(format);
        if (operation.valid) {
            write_packed(*operation.valid,
                PackedLogic4::from_aval_bval(1U, valid ? 1U : 0U, 0U));
        }
        if (valid) {
            vhdl_assert_formats[level] = format;
        } else if (!operation.valid) {
            execute_vhdl_report(
                process, process.pc,
                "invalid VHDL assert format", AssertionSeverity::failure,
                operation.source, true);
        }
        return;
    }
    if (operation.kind == Kind::get_format && !operation.destination
        && operation.string_destination && operation.level
        && !operation.enable && !operation.format && !operation.valid) {
        write_string(*operation.string_destination,
            vhdl_assert_formats[decode_level(*operation.level)]);
        return;
    }
    if (operation.kind == Kind::set_read_severity && !operation.destination
        && !operation.string_destination && operation.level
        && !operation.enable && !operation.format && !operation.valid) {
        vhdl_read_severity = static_cast<AssertionSeverity>(
            decode_level(*operation.level));
        return;
    }
    if (operation.kind == Kind::get_read_severity && operation.destination
        && !operation.string_destination && !operation.level
        && no_other_operands()) {
        write_packed(*operation.destination,
            PackedLogic4::from_aval_bval(2U,
                static_cast<std::uint64_t>(vhdl_read_severity), 0U));
        return;
    }
    if (operation.kind == Kind::record_read_failure
        && !operation.destination && !operation.string_destination
        && !operation.level && !operation.enable && !operation.format
        && !operation.valid) {
        execute_vhdl_report(
            process, process.pc,
            "VHDL TextIO read did not convert the requested element",
            vhdl_read_severity, operation.source, false);
        return;
    }
    fail(process, "VHDL assert API operation is invalid");
}

void Interpreter::Impl::execute_vhdl_reflection_api(
    ProcessState& process, const VhdlReflectionApi& operation)
{
    using Kind = VhdlReflectionApiKind;
    constexpr std::size_t maximum_mirrors = 65'536U;
    const auto packed = [&](const RegisterId id, const std::size_t width) {
        return process.executor
            ? process.executor->read_register(id, width)
            : get_register(process, id);
    };
    const auto write_packed = [&](PackedLogic4 value) {
        if (!operation.destination || value.width() != operation.result_width) {
            fail(process, "VHDL reflection packed result is invalid");
        }
        if (process.executor) {
            process.executor->write_register(*operation.destination, value);
        } else {
            get_register(process, *operation.destination) = std::move(value);
        }
    };
    const auto write_unsigned = [&](const std::uint64_t value) {
        write_packed(PackedLogic4::from_aval_bval(
            operation.result_width, value, 0U));
    };
    const auto write_string = [&](const std::string_view value) {
        if (!operation.string_destination
            || value.size() > maximum_string_bytes) {
            fail(process, "VHDL reflection string result is invalid");
        }
        if (process.executor) {
            process.executor->write_string_register(
                *operation.string_destination, value);
        } else {
            get_string_register(process, *operation.string_destination) = value;
        }
    };
    const auto string_argument = [&]() -> std::string {
        if (!operation.string_argument) {
            fail(process, "VHDL reflection string argument is missing");
        }
        return process.executor
            ? process.executor->read_string_register(*operation.string_argument)
            : get_string_register(process, *operation.string_argument);
    };
    const auto integer_argument = [&](const std::size_t index) -> std::int64_t {
        if (index >= operation.arguments.size()) {
            fail(process, "VHDL reflection integer argument is missing");
        }
        const auto value = packed(operation.arguments[index], 64U);
        const auto decoded = value.known_signed_value();
        if (!decoded) {
            fail(process, "VHDL reflection integer argument is not known");
        }
        return *decoded;
    };
    const auto create = [&](VhdlReflectionType type,
                            PackedLogic4 value,
                            const bool has_value,
                            std::optional<PackedLogic4> designated_value = { })
        -> std::uint32_t {
        if (vhdl_reflection_mirrors.size() >= maximum_mirrors) {
            fail(process, "VHDL reflection mirror limit is exhausted");
        }
        vhdl_reflection_mirrors.push_back(
            VhdlReflectionMirror { std::move(type), std::move(value), has_value,
                std::move(designated_value) });
        return static_cast<std::uint32_t>(vhdl_reflection_mirrors.size() - 1U);
    };
    const auto mirror = [&]() -> const VhdlReflectionMirror& {
        if (!operation.receiver) {
            fail(process, "VHDL reflection receiver is missing");
        }
        const auto value = packed(*operation.receiver, 32U);
        const auto handle = value.known_unsigned_value();
        if (!handle || *handle == 0U
            || *handle >= vhdl_reflection_mirrors.size()) {
            fail(process, "VHDL reflection receiver is null or stale");
        }
        return vhdl_reflection_mirrors[static_cast<std::size_t>(*handle)];
    };
    const auto range = [&](const VhdlReflectionMirror& value,
                           const std::size_t dimension = 0U)
        -> const VhdlReflectionRange& {
        if (dimension >= value.type.ranges.size()) {
            fail(process, "VHDL reflection dimension is outside the subtype");
        }
        return value.type.ranges[dimension];
    };
    const auto range_length = [](const VhdlReflectionRange& value) {
        const auto distance = value.left >= value.right
            ? static_cast<std::uint64_t>(value.left - value.right)
            : static_cast<std::uint64_t>(value.right - value.left);
        return distance + 1U;
    };
    const auto bound = [&](const VhdlReflectionRange& value) {
        switch (operation.kind) {
        case Kind::left: return value.left;
        case Kind::right: return value.right;
        case Kind::low: return std::min(value.left, value.right);
        case Kind::high: return std::max(value.left, value.right);
        default: fail(process, "VHDL reflection bound query is invalid");
        }
    };
    const auto require_value = [&](const VhdlReflectionMirror& value) {
        if (!value.has_value) {
            fail(process, "VHDL reflection value method used a subtype mirror");
        }
    };
    const auto child_handle = [&](const VhdlReflectionMirror& owner,
                                  const std::size_t index,
                                  const bool with_value) {
        if (index >= owner.type.children.size()) {
            fail(process, "VHDL reflection element is outside the subtype");
        }
        auto child = owner.type.children[index];
        auto value = PackedLogic4(child.packed_width, Logic4::zero);
        if (with_value) {
            require_value(owner);
            if (child.packed_width == 0U
                || child.lsb_offset > owner.value.width()
                || child.packed_width > owner.value.width() - child.lsb_offset) {
                fail(process, "VHDL reflection element has no executable snapshot");
            }
            value = owner.value.extract_bits(
                static_cast<std::size_t>(child.lsb_offset), child.packed_width);
        }
        return create(std::move(child), std::move(value), with_value);
    };

    if (operation.kind == Kind::create_subtype
        || operation.kind == Kind::create_value) {
        if (operation.receiver || operation.string_destination
            || !operation.destination || operation.result_width != 32U
            || (operation.kind == Kind::create_subtype
                && (operation.source || operation.access_heap))
            || (operation.kind == Kind::create_value
                && operation.type.packed_width != 0U && !operation.source)
            || (operation.access_heap
                && operation.type.type_class != VhdlReflectionClass::access)) {
            fail(process, "VHDL reflection mirror creation is invalid");
        }
        auto value = operation.source
            ? packed(*operation.source, operation.type.packed_width)
            : PackedLogic4(operation.type.packed_width, Logic4::zero);
        std::optional<PackedLogic4> designated_value;
        if (operation.access_heap) {
            const auto handle = value.known_unsigned_value();
            if (!handle) {
                fail(process, "reflected access value is not known");
            }
            if (*handle != 0U) {
                const auto& heap = process.executor
                    ? process.executor->read_container_register(
                        *operation.access_heap)
                    : read_container_register(process, *operation.access_heap);
                if (heap.keys.size() != heap.elements.size()) {
                    fail(process, "VHDL access heap snapshot is inconsistent");
                }
                const auto key = PackedLogic4::from_aval_bval(
                    value.width(), *handle, 0U);
                const auto found = std::ranges::find(heap.keys, key);
                if (found == heap.keys.end()) {
                    fail(process, "reflected access value is stale");
                }
                designated_value = heap.elements[static_cast<std::size_t>(
                    found - heap.keys.begin())];
            }
        }
        write_unsigned(create(operation.type, std::move(value),
            operation.kind == Kind::create_value,
            std::move(designated_value)));
        return;
    }

    const auto& owner = mirror();
    if (operation.kind == Kind::get_type_class) {
        write_unsigned(static_cast<std::uint64_t>(owner.type.type_class));
        return;
    }
    if (operation.kind == Kind::get_subtype_mirror) {
        write_unsigned(create(owner.type,
            PackedLogic4(owner.type.packed_width, Logic4::zero), false));
        return;
    }
    if (operation.kind == Kind::convert) {
        if (owner.type.type_class != operation.type.type_class) {
            fail(process, "VHDL reflection class conversion does not match the mirror");
        }
        const auto receiver_value = packed(*operation.receiver, 32U);
        write_packed(receiver_value);
        return;
    }
    if (operation.kind == Kind::convert_generic) {
        const auto receiver_value = packed(*operation.receiver, 32U);
        write_packed(receiver_value);
        return;
    }
    if (operation.kind == Kind::simple_name) {
        write_string(owner.type.simple_name);
        return;
    }
    if (operation.kind == Kind::enumeration_literal) {
        if (owner.type.type_class != VhdlReflectionClass::enumeration) {
            fail(process, "enumeration_literal requires an enumeration subtype mirror");
        }
        std::size_t index { };
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) {
                fail(process, "enumeration literal name is outside the subtype");
            }
            index = static_cast<std::size_t>(found - owner.type.names.begin());
        } else {
            const auto requested = integer_argument(0U);
            if (requested < 0
                || static_cast<std::uint64_t>(requested) >= owner.type.names.size()) {
                fail(process, "enumeration literal index is outside the subtype");
            }
            index = static_cast<std::size_t>(requested);
        }
        auto type = owner.type;
        write_unsigned(create(std::move(type),
            PackedLogic4::from_aval_bval(owner.type.packed_width, index, 0U), true));
        return;
    }
    if (operation.kind == Kind::pos) {
        require_value(owner);
        const auto value = owner.value.known_unsigned_value();
        if (!value) fail(process, "reflected enumeration value is not known");
        write_unsigned(*value);
        return;
    }
    if (operation.kind == Kind::image) {
        require_value(owner);
        if (owner.type.type_class == VhdlReflectionClass::enumeration) {
            const auto ordinal = owner.value.known_unsigned_value();
            if (!ordinal || *ordinal >= owner.type.names.size()) {
                fail(process, "reflected enumeration value is outside the subtype");
            }
            write_string(owner.type.names[static_cast<std::size_t>(*ordinal)]);
        } else if (owner.type.type_class == VhdlReflectionClass::floating) {
            const auto bits = owner.value.known_unsigned_value();
            if (!bits || owner.value.width() != 64U) {
                fail(process, "reflected floating value is invalid");
            }
            write_string(std::to_string(std::bit_cast<double>(*bits)));
        } else {
            std::optional<std::int64_t> value;
            if (owner.type.signed_value) {
                value = owner.value.known_signed_value();
            } else if (const auto unsigned_value
                    = owner.value.known_unsigned_value();
                unsigned_value
                && *unsigned_value
                    <= static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                value = static_cast<std::int64_t>(*unsigned_value);
            }
            if (!value) fail(process, "reflected scalar value is not known");
            auto text = std::to_string(*value);
            if (owner.type.type_class == VhdlReflectionClass::physical
                && !owner.type.names.empty()) {
                text += " " + owner.type.names.front();
            }
            write_string(text);
        }
        return;
    }
    if (operation.kind == Kind::left || operation.kind == Kind::right
        || operation.kind == Kind::low || operation.kind == Kind::high) {
        const auto dimension = owner.type.type_class == VhdlReflectionClass::array
            && !operation.arguments.empty()
            ? static_cast<std::size_t>(integer_argument(0U) - 1)
            : 0U;
        const auto result = bound(range(owner, dimension));
        if (owner.type.type_class == VhdlReflectionClass::array) {
            write_unsigned(static_cast<std::uint64_t>(result));
        } else {
            auto type = owner.type;
            write_unsigned(create(std::move(type),
                PackedLogic4::from_aval_bval(owner.type.packed_width,
                    static_cast<std::uint64_t>(result), 0U), true));
        }
        return;
    }
    if (operation.kind == Kind::length) {
        if (owner.type.type_class == VhdlReflectionClass::record) {
            if (owner.type.names.size() != owner.type.children.size()) {
                fail(process, "record reflection descriptor is inconsistent");
            }
            write_unsigned(owner.type.children.size());
            return;
        }
        const auto dimension = owner.type.type_class == VhdlReflectionClass::array
            && !operation.arguments.empty()
            ? static_cast<std::size_t>(integer_argument(0U) - 1)
            : 0U;
        write_unsigned(range_length(range(owner, dimension)));
        return;
    }
    if (operation.kind == Kind::ascending) {
        const auto dimension = owner.type.type_class == VhdlReflectionClass::array
            && !operation.arguments.empty()
            ? static_cast<std::size_t>(integer_argument(0U) - 1)
            : 0U;
        write_unsigned(range(owner, dimension).ascending ? 1U : 0U);
        return;
    }
    if (operation.kind == Kind::value) {
        require_value(owner);
        auto value = resize_class_value(owner.value, operation.result_width);
        if (owner.type.signed_value && operation.result_width > owner.value.width()
            && owner.value.width() != 0U) {
            const auto extension = owner.value.get(owner.value.width() - 1U);
            for (std::size_t bit = owner.value.width();
                 bit < operation.result_width; ++bit) {
                value.set(bit, extension);
            }
        }
        write_packed(std::move(value));
        return;
    }
    if (operation.kind == Kind::units_length) {
        write_unsigned(owner.type.names.size());
        return;
    }
    if (operation.kind == Kind::unit_name) {
        const auto index = integer_argument(0U);
        if (index < 0 || static_cast<std::uint64_t>(index) >= owner.type.names.size())
            fail(process, "physical unit index is outside the subtype");
        write_string(owner.type.names[static_cast<std::size_t>(index)]);
        return;
    }
    if (operation.kind == Kind::unit_index) {
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) fail(process, "physical unit name is unknown");
            write_unsigned(static_cast<std::uint64_t>(found - owner.type.names.begin()));
        } else {
            require_value(owner);
            const auto value = owner.value.known_signed_value();
            if (!value || owner.type.scales.empty()) {
                fail(process, "reflected physical value has no unit");
            }
            std::size_t selected = 0U;
            for (std::size_t index = 0U; index < owner.type.scales.size(); ++index) {
                if (owner.type.scales[index] != 0U
                    && *value % static_cast<std::int64_t>(
                        owner.type.scales[index]) == 0
                    && owner.type.scales[index]
                        >= owner.type.scales[selected]) {
                    selected = index;
                }
            }
            write_unsigned(selected);
        }
        return;
    }
    if (operation.kind == Kind::scale) {
        std::size_t index { };
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) fail(process, "physical unit name is unknown");
            index = static_cast<std::size_t>(found - owner.type.names.begin());
        } else {
            const auto requested = integer_argument(0U);
            if (requested < 0) fail(process, "physical unit index is negative");
            index = static_cast<std::size_t>(requested);
        }
        if (index >= owner.type.scales.size()) fail(process, "physical unit index is outside the subtype");
        write_unsigned(owner.type.scales[index]);
        return;
    }
    if (operation.kind == Kind::record_element_name) {
        const auto index = integer_argument(0U);
        if (index < 0 || static_cast<std::uint64_t>(index) >= owner.type.names.size())
            fail(process, "record element index is outside the subtype");
        write_string(owner.type.names[static_cast<std::size_t>(index)]);
        return;
    }
    if (operation.kind == Kind::record_element_index) {
        const auto name = string_argument();
        const auto found = std::ranges::find(owner.type.names, name);
        if (found == owner.type.names.end()) fail(process, "record element name is unknown");
        write_unsigned(static_cast<std::uint64_t>(found - owner.type.names.begin()));
        return;
    }
    if (operation.kind == Kind::record_element_subtype
        || (operation.kind == Kind::aggregate_get
            && owner.type.type_class == VhdlReflectionClass::record)) {
        std::size_t index { };
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) fail(process, "record element name is unknown");
            index = static_cast<std::size_t>(found - owner.type.names.begin());
        } else {
            const auto requested = integer_argument(0U);
            if (requested < 0) fail(process, "aggregate element index is negative");
            index = static_cast<std::size_t>(requested);
        }
        write_unsigned(child_handle(owner, index,
            operation.kind == Kind::aggregate_get));
        return;
    }
    if (operation.kind == Kind::aggregate_get
        && owner.type.type_class == VhdlReflectionClass::array) {
        require_value(owner);
        if (owner.type.children.size() != 1U
            || operation.arguments.size() != owner.type.ranges.size()) {
            fail(process, "array reflection index arity does not match its dimensions");
        }
        const auto& element = owner.type.children.front();
        std::uint64_t bit_offset = 0U;
        std::uint64_t stride = element.packed_width;
        for (std::size_t dimension = owner.type.ranges.size();
             dimension-- > 0U;) {
            const auto& bounds = owner.type.ranges[dimension];
            const auto index = integer_argument(dimension);
            const auto low = std::min(bounds.left, bounds.right);
            const auto high = std::max(bounds.left, bounds.right);
            if (index < low || index > high) {
                fail(process, "array reflection index is outside the subtype");
            }
            const auto ordinal = index >= bounds.right
                ? static_cast<std::uint64_t>(index - bounds.right)
                : static_cast<std::uint64_t>(bounds.right - index);
            if (ordinal != 0U
                && stride > std::numeric_limits<std::uint64_t>::max()
                    / ordinal) {
                fail(process, "array reflection offset overflows");
            }
            const auto contribution = ordinal * stride;
            if (contribution
                > std::numeric_limits<std::uint64_t>::max() - bit_offset) {
                fail(process, "array reflection offset overflows");
            }
            bit_offset += contribution;
            const auto count = range_length(bounds);
            if (count != 0U && dimension != 0U
                && stride > std::numeric_limits<std::uint64_t>::max() / count) {
                fail(process, "array reflection stride overflows");
            }
            stride *= count;
        }
        if (element.packed_width == 0U
            || bit_offset > owner.value.width()
            || element.packed_width > owner.value.width() - bit_offset) {
            fail(process, "array reflection element has no executable snapshot");
        }
        auto element_type = element;
        element_type.lsb_offset = 0U;
        write_unsigned(create(std::move(element_type),
            owner.value.extract_bits(static_cast<std::size_t>(bit_offset),
                element.packed_width), true));
        return;
    }
    if (operation.kind == Kind::dimensions) {
        write_unsigned(owner.type.ranges.size());
        return;
    }
    if (operation.kind == Kind::array_index_subtype) {
        const auto dimension = operation.arguments.empty()
            ? 0U : static_cast<std::size_t>(integer_argument(0U) - 1);
        if (dimension >= owner.type.ranges.size()) fail(process, "array dimension is outside the subtype");
        VhdlReflectionType index_type;
        index_type.type_class = VhdlReflectionClass::integer;
        index_type.simple_name = "index";
        index_type.packed_width = 64U;
        index_type.signed_value = true;
        index_type.ranges.push_back(owner.type.ranges[dimension]);
        write_unsigned(create(std::move(index_type), PackedLogic4(64U, Logic4::zero), false));
        return;
    }
    if (operation.kind == Kind::array_element_subtype) {
        write_unsigned(child_handle(owner, 0U, false));
        return;
    }
    if (operation.kind == Kind::is_null) {
        require_value(owner);
        const auto value = owner.value.known_unsigned_value();
        if (!value) fail(process, "reflected access value is not known");
        write_unsigned(*value == 0U ? 1U : 0U);
        return;
    }
    if (operation.kind == Kind::designated_subtype) {
        write_unsigned(child_handle(owner, 0U, false));
        return;
    }
    if (operation.kind == Kind::file_logical_name
        || operation.kind == Kind::file_open_kind) {
        require_value(owner);
        const auto handle = owner.value.known_unsigned_value();
        if (!handle) fail(process, "reflected file handle is not known");
        const auto found = files.find(static_cast<FileHandle>(*handle));
        if (found == files.end() || found->second.closed) {
            fail(process, "reflected file is not open");
        }
        if (operation.kind == Kind::file_logical_name) {
            write_string(found->second.logical_name);
        } else {
            write_unsigned(found->second.mode.starts_with("r") ? 0U
                : found->second.mode.starts_with("w") ? 1U : 2U);
        }
        return;
    }
    if (operation.kind == Kind::access_get) {
        require_value(owner);
        if (owner.type.type_class != VhdlReflectionClass::access
            || owner.type.children.size() != 1U) {
            fail(process, "access reflection requires one designated subtype");
        }
        const auto handle = owner.value.known_unsigned_value();
        if (!handle || *handle == 0U || !owner.designated_value) {
            fail(process, "cannot reflect the designated value of null access");
        }
        auto designated_type = owner.type.children.front();
        write_unsigned(create(std::move(designated_type),
            *owner.designated_value, true));
        return;
    }
    fail(process, "VHDL reflection API operation is invalid");
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
