// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scope_randomize.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_execution_context.hpp"
#include "simir_execution_shared.hpp"
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

    [[maybe_unused, nodiscard]] bool valid_vhdl_assert_format(
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

    [[maybe_unused, nodiscard]] std::string format_vhdl_assert_message(
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

} // namespace fsim::runtime
