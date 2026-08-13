// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::tests::runtime {

namespace {

    void require(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

} // namespace

void test_simir_alternate_executor_event_replacement_and_cancel()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    class Producer final : public ProcessExecutor {
    public:
        explicit Producer(const SignalId event)
            : event_(event)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            InstructionIndex) override
        {
            ProcessResumeResult result { 0, 1 };
            switch (state_++) {
            case 0:
                context.notify_event(
                    event_, 5, EventNotificationKind::timed);
                context.notify_event(
                    event_, 7, EventNotificationKind::timed);
                context.notify_event(
                    event_, 3, EventNotificationKind::timed);
                result.external.kind = ExternalSuspendKind::wait_for;
                result.external.delay = 4;
                break;
            case 1:
                context.notify_event(
                    event_, 2, EventNotificationKind::timed);
                result.external.kind = ExternalSuspendKind::wait_for;
                result.external.delay = 1;
                break;
            case 2:
                context.cancel_event(event_);
                result.external.kind = ExternalSuspendKind::wait_for;
                result.external.delay = 1;
                break;
            default:
                context.notify_event(
                    event_, 0, EventNotificationKind::immediate);
                result.external.kind = ExternalSuspendKind::halt;
                break;
            }
            return result;
        }

    private:
        SignalId event_ { };
        std::size_t state_ { };
    };

    class Consumer final : public ProcessExecutor {
    public:
        Consumer(
            const SignalId event,
            std::size_t& notifications)
            : event_(event)
            , notifications_(notifications)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            InstructionIndex) override
        {
            if (initialized_) {
                ++notifications_;
            }
            initialized_ = true;
            ProcessResumeResult result { 0, 1 };
            result.external.kind = ExternalSuspendKind::wait_on;
            result.external.sensitivity.push_back(
                { event_, EdgeKind::any });
            return result;
        }

    private:
        SignalId event_ { };
        std::size_t& notifications_;
        bool initialized_ { };
    };

    Interpreter interpreter;
    const auto event = interpreter.add_signal(
        { "event", PackedLogic4::from_msb_string("0") });

    Process producer;
    producer.id = 0;
    producer.name = "event_producer";
    producer.operations = { Halt { } };
    const auto producer_id = interpreter.add_process(std::move(producer));

    Process consumer;
    consumer.id = 1;
    consumer.name = "event_consumer";
    consumer.operations = { Halt { } };
    const auto consumer_id = interpreter.add_process(std::move(consumer));

    std::size_t notifications = 0;
    interpreter.set_process_executor(
        producer_id, std::make_unique<Producer>(event));
    interpreter.set_process_executor(
        consumer_id,
        std::make_unique<Consumer>(event, notifications));

    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed && result.time == 6,
        "replaced and canceled event notifications must not run");
    require(
        notifications == 2,
        "only the earliest timed and final immediate event must trigger");
}

void test_simir_alternate_executor_notify_delayed()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    class Producer final : public ProcessExecutor {
    public:
        Producer(
            const SignalId event,
            bool& duplicate_rejected)
            : event_(event)
            , duplicate_rejected_(duplicate_rejected)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            InstructionIndex) override
        {
            ProcessResumeResult result { 0, 1 };
            if (state_++ == 0) {
                context.notify_event(
                    event_, 2, EventNotificationKind::delayed);
                try {
                    context.notify_event(
                        event_, 1, EventNotificationKind::delayed);
                } catch (const std::logic_error&) {
                    duplicate_rejected_ = true;
                }
                result.external.kind = ExternalSuspendKind::wait_for;
                result.external.delay = 1;
            } else {
                context.cancel_event(event_);
                context.notify_event(
                    event_, 0, EventNotificationKind::delayed);
                result.external.kind = ExternalSuspendKind::halt;
            }
            return result;
        }

    private:
        SignalId event_ { };
        bool& duplicate_rejected_;
        std::size_t state_ { };
    };

    class Consumer final : public ProcessExecutor {
    public:
        Consumer(
            const SignalId event,
            std::size_t& notifications)
            : event_(event)
            , notifications_(notifications)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            InstructionIndex) override
        {
            ProcessResumeResult result { 0, 1 };
            if (initialized_) {
                ++notifications_;
                result.external.kind = ExternalSuspendKind::halt;
            } else {
                initialized_ = true;
                result.external.kind = ExternalSuspendKind::wait_on;
                result.external.sensitivity.push_back(
                    { event_, EdgeKind::any });
            }
            return result;
        }

    private:
        SignalId event_ { };
        std::size_t& notifications_;
        bool initialized_ { };
    };

    Interpreter interpreter;
    const auto event = interpreter.add_signal(
        { "delayed_event", PackedLogic4::from_msb_string("0") });

    Process producer;
    producer.id = 0;
    producer.name = "notify_delayed_producer";
    producer.operations = { Halt { } };
    const auto producer_id = interpreter.add_process(std::move(producer));

    Process consumer;
    consumer.id = 1;
    consumer.name = "notify_delayed_consumer";
    consumer.operations = { Halt { } };
    const auto consumer_id = interpreter.add_process(std::move(consumer));

    bool duplicate_rejected = false;
    std::size_t notifications = 0;
    interpreter.set_process_executor(
        producer_id,
        std::make_unique<Producer>(
            event, duplicate_rejected));
    interpreter.set_process_executor(
        consumer_id,
        std::make_unique<Consumer>(event, notifications));

    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed && result.time == 2,
        "canceled notify_delayed entries must drain harmlessly");
    require(
        duplicate_rejected,
        "notify_delayed must reject an event with a pending notification");
    require(
        notifications == 1,
        "the replacement delta notify_delayed must trigger exactly once");
}

void test_simir_alternate_executor_primitive_channel_updates()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    class ChannelExecutor final : public ProcessExecutor {
    public:
        ChannelExecutor(
            const SignalId output,
            std::vector<std::uint64_t>& updates)
            : output_(output)
            , updates_(updates)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            InstructionIndex) override
        {
            context.request_channel_update(10);
            context.request_channel_update(10);
            context.request_channel_update(20);
            ProcessResumeResult result { 0, 1 };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

        void update_channel(
            const std::uint64_t channel,
            ProcessExecutionContext& context) override
        {
            updates_.push_back(channel);
            context.write_update(
                output_,
                PackedLogic4::from_msb_string(
                    channel == 10       ? "01"
                        : channel == 20 ? "10"
                                        : "11"));
            if (channel == 10) {
                context.request_channel_update(10);
                context.request_channel_update(30);
            }
        }

    private:
        SignalId output_ { };
        std::vector<std::uint64_t>& updates_;
    };

    Interpreter interpreter;
    const auto output = interpreter.add_signal(
        { "channel_output", PackedLogic4::from_msb_string("00") });
    Process process;
    process.id = 0;
    process.name = "primitive_channel_requester";
    process.operations = { Halt { } };
    const auto process_id = interpreter.add_process(std::move(process));

    std::vector<std::uint64_t> updates;
    std::size_t commits = 0;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4&,
            const SimulationTick) {
            if (signal == output) {
                ++commits;
            }
        });
    interpreter.set_process_executor(
        process_id,
        std::make_unique<ChannelExecutor>(output, updates));

    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && result.time == 0,
        "updates requested during the update phase must enter the next delta");
    require(
        updates == std::vector<std::uint64_t> { 10, 20, 30 },
        "primitive-channel updates must be deduplicated and stably ordered");
    require(
        commits == 2,
        "an update-phase request must commit in a later delta");
    require(
        interpreter.signal_value(output).to_msb_string() == "11",
        "primitive-channel writes must commit through the common update phase");
}

void test_simir_alternate_executor_signal_event_window()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    class Producer final : public ProcessExecutor {
    public:
        Producer(
            const SignalId signal,
            bool& expired)
            : signal_(signal)
            , expired_(expired)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            InstructionIndex) override
        {
            ProcessResumeResult result { 0, 1 };
            if (state_++ == 0) {
                context.write_update(
                    signal_, PackedLogic4::from_msb_string("1"));
                result.external.kind = ExternalSuspendKind::wait_for;
                result.external.delay = 2;
            } else {
                expired_ = !context.signal_event(signal_);
                result.external.kind = ExternalSuspendKind::halt;
            }
            return result;
        }

    private:
        SignalId signal_ { };
        bool& expired_;
        std::size_t state_ { };
    };

    class Consumer final : public ProcessExecutor {
    public:
        Consumer(
            const SignalId signal,
            bool& observed)
            : signal_(signal)
            , observed_(observed)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            InstructionIndex) override
        {
            observed_ = context.signal_event(signal_);
            ProcessResumeResult result { 0, 1 };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        SignalId signal_ { };
        bool& observed_;
    };

    Interpreter interpreter;
    const auto signal = interpreter.add_signal(
        { "event_window", PackedLogic4::from_msb_string("0") });

    Process producer;
    producer.id = 0;
    producer.name = "signal_event_producer";
    producer.operations = { Halt { } };
    const auto producer_id = interpreter.add_process(std::move(producer));

    Process consumer;
    consumer.id = 1;
    consumer.name = "signal_event_consumer";
    consumer.initialize = false;
    consumer.static_sensitivity.push_back(
        { signal, EdgeKind::any });
    consumer.operations = { WaitSensitivity { } };
    const auto consumer_id = interpreter.add_process(std::move(consumer));

    bool observed = false;
    bool expired = false;
    interpreter.set_process_executor(
        producer_id,
        std::make_unique<Producer>(signal, expired));
    interpreter.set_process_executor(
        consumer_id,
        std::make_unique<Consumer>(signal, observed));

    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed && result.time == 2,
        "signal event-window test must reach its later observation");
    require(
        observed,
        "a signal event must be visible in the awakened evaluation delta");
    require(
        expired,
        "a signal event must expire after its awakened evaluation delta");
}

void test_simir_alternate_executor_event_lists()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    class Producer final : public ProcessExecutor {
    public:
        explicit Producer(const std::array<SignalId, 3> events)
            : events_(events)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            InstructionIndex) override
        {
            for (std::size_t index = 0; index < events_.size(); ++index) {
                context.notify_event(
                    events_[index],
                    static_cast<SimulationTick>(index + 1),
                    EventNotificationKind::timed);
            }
            ProcessResumeResult result { 0, 1 };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        std::array<SignalId, 3> events_;
    };

    class ListConsumer final : public ProcessExecutor {
    public:
        ListConsumer(
            std::vector<SignalId> events,
            const bool wait_all,
            std::size_t& wakeups)
            : events_(std::move(events))
            , wait_all_(wait_all)
            , wakeups_(wakeups)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            InstructionIndex) override
        {
            ProcessResumeResult result { 0, 1 };
            if (initialized_) {
                ++wakeups_;
                result.external.kind = ExternalSuspendKind::halt;
                return result;
            }
            initialized_ = true;
            result.external.kind = ExternalSuspendKind::wait_on;
            result.external.wait_all = wait_all_;
            for (const auto event : events_) {
                result.external.sensitivity.push_back(
                    { event, EdgeKind::any });
            }
            return result;
        }

    private:
        std::vector<SignalId> events_;
        bool wait_all_ { };
        std::size_t& wakeups_;
        bool initialized_ { };
    };

    Interpreter interpreter;
    const std::array events {
        interpreter.add_signal(
            { "event_a", PackedLogic4::from_msb_string("0") }),
        interpreter.add_signal(
            { "event_b", PackedLogic4::from_msb_string("0") }),
        interpreter.add_signal(
            { "event_c", PackedLogic4::from_msb_string("0") }),
    };

    Process producer;
    producer.id = 0;
    producer.name = "event_list_producer";
    producer.operations = { Halt { } };
    const auto producer_id = interpreter.add_process(std::move(producer));

    Process or_consumer;
    or_consumer.id = 1;
    or_consumer.name = "event_or_consumer";
    or_consumer.operations = { Halt { } };
    const auto or_id = interpreter.add_process(std::move(or_consumer));

    Process and_consumer;
    and_consumer.id = 2;
    and_consumer.name = "event_and_consumer";
    and_consumer.operations = { Halt { } };
    const auto and_id = interpreter.add_process(std::move(and_consumer));

    std::size_t or_wakeups = 0;
    std::size_t and_wakeups = 0;
    interpreter.set_process_executor(
        producer_id, std::make_unique<Producer>(events));
    interpreter.set_process_executor(
        or_id,
        std::make_unique<ListConsumer>(
            std::vector<SignalId> { events[0], events[1] },
            false,
            or_wakeups));
    interpreter.set_process_executor(
        and_id,
        std::make_unique<ListConsumer>(
            std::vector<SignalId> {
                events[0], events[1], events[2] },
            true,
            and_wakeups));

    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed && result.time == 3,
        "event-list waits must retain deterministic timed progress");
    require(
        or_wakeups == 1,
        "an OR-list wait must wake on its first event");
    require(
        and_wakeups == 1,
        "an AND-list wait must wake only after every event");
}

void test_simir_assertion_metadata()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter nonfatal_interpreter;
    std::vector<std::string> nonfatal_reports;
    nonfatal_interpreter.set_report_hook(
        [&nonfatal_reports](
            const ProcessId,
            const std::string_view message,
            const AssertionSeverity severity,
            const SourceLocation& source,
            const SimulationTick,
            const std::uint64_t) {
            require(
                severity == AssertionSeverity::error
                    && source.path == "nonfatal.sv",
                "nonfatal assertion severity and source");
            nonfatal_reports.emplace_back(message);
        });
    Process nonfatal_process;
    nonfatal_process.id = 0;
    nonfatal_process.name = "nonfatal-assertion";
    nonfatal_process.register_count = 1;
    nonfatal_process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("0") },
        Assert {
            0,
            "continued",
            AssertionSeverity::error,
            SourceLocation { "nonfatal.sv", 4, 3 } },
        Halt { },
    };
    (void)nonfatal_interpreter.add_process(
        std::move(nonfatal_process));
    const auto nonfatal_result = nonfatal_interpreter.run();
    require(
        nonfatal_result.status == RunStatus::completed
            && nonfatal_reports
                == std::vector<std::string> { "continued" },
        "a nonfatal SimIR assertion must report once and continue");

    Interpreter interpreter;
    Process process;
    process.id = 0;
    process.name = "assertion";
    process.register_count = 1;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("0") },
        Assert {
            0,
            "metadata survived",
            AssertionSeverity::failure,
            SourceLocation { "assertions.sv", 17, 9 } },
        Halt { },
    };
    (void)interpreter.add_process(std::move(process));
    try {
        (void)interpreter.run();
        throw std::runtime_error("a false SimIR assertion was accepted");
    } catch (const AssertionError& error) {
        require(error.process() == 0 && error.instruction() == 1,
            "assertion process and instruction");
        require(error.severity() == AssertionSeverity::failure,
            "assertion severity");
        require(
            !error.reported(),
            "a raw fatal assertion is reported by its boundary handler");
        require(
            error.source().path == "assertions.sv"
                && error.source().line == 17
                && error.source().column == 9,
            "assertion source location");
        require(
            std::string_view { error.what() }.find("metadata survived")
                != std::string_view::npos,
            "assertion message");
    }
}

void test_simir_execution_point_ordering()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    const auto signal = interpreter.add_signal(
        { "value", PackedLogic4::from_msb_string("X") });
    for (ProcessId id = 0; id < 2; ++id) {
        Process process;
        process.id = id;
        process.name = "process_" + std::to_string(id);
        process.register_count = 1;
        process.operations = {
            DebugPoint {
                DebugPointKind::statement,
                SourceLocation {
                    "execution_points.sv",
                    static_cast<std::uint32_t>(10 + id),
                    3 } },
            DebugPoint {
                DebugPointKind::call,
                SourceLocation {
                    "execution_points.sv",
                    static_cast<std::uint32_t>(20 + id),
                    7 } },
            LoadConstant {
                0,
                PackedLogic4::from_msb_string(id == 0 ? "0" : "1") },
            WriteBlocking { signal, 0 },
            Halt { },
        };
        (void)interpreter.add_process(std::move(process));
    }
    std::vector<ProcessId> points;
    std::vector<ExecutionPoint> all_points;
    interpreter.set_execution_point_hook(
        [&](Scheduler& scheduler, const ExecutionPoint& point) {
            all_points.push_back(point);
            if (point.kind == ExecutionPointKind::statement) {
                points.push_back(point.process);
                scheduler.request_stop();
            }
        });
    interpreter.start();
    const auto first = interpreter.run();
    require(first.status == RunStatus::stopped,
        "first statement safe point must stop");
    require(interpreter.signal_value(signal).to_msb_string() == "X",
        "a source stop occurs before its statement");

    interpreter.scheduler().clear_stop();
    const auto second = interpreter.run();
    require(second.status == RunStatus::stopped,
        "second statement safe point must stop");
    require(interpreter.signal_value(signal).to_msb_string() == "0",
        "the lower-ID process must finish before the next process stops");

    interpreter.scheduler().clear_stop();
    const auto third = interpreter.run();
    require(third.status == RunStatus::completed,
        "execution-point continuation must complete");
    require(interpreter.signal_value(signal).to_msb_string() == "1",
        "the later process must resume after the earlier process");
    require(points == std::vector<ProcessId> { 0, 1 },
        "execution-point process ordering");
    std::vector<ExecutionPoint> source_points;
    for (const auto& point : all_points) {
        if (point.kind == ExecutionPointKind::statement
            || point.kind == ExecutionPointKind::call) {
            source_points.push_back(point);
        }
    }
    require(
        source_points.size() == 4
            && source_points[0].kind == ExecutionPointKind::statement
            && source_points[1].kind == ExecutionPointKind::call
            && source_points[1].source.line == 20
            && source_points[1].source.column == 7
            && source_points[2].kind == ExecutionPointKind::statement
            && source_points[3].kind == ExecutionPointKind::call
            && source_points[3].source.line == 21,
        "call execution-point kind and source ordering");
}

void test_simir_display_output()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    Process process;
    process.name = "display";
    process.operations = {
        Display { "first", true },
        Display { "postponed", true, true },
        WaitFor { 2 },
        Display { "tail", false },
        Halt { },
    };
    const auto process_id = interpreter.add_process(std::move(process));
    Process second_process;
    second_process.id = 1;
    second_process.name = "second-strobe";
    second_process.operations = {
        Display { "second-postponed", true, true },
        Halt { },
    };
    const auto second_process_id = interpreter.add_process(std::move(second_process));
    struct Event {
        ProcessId process { };
        std::string text;
        bool newline { };
        SimulationTick time { };
        std::uint64_t delta { };
        SchedulerPhase phase { SchedulerPhase::active };
    };
    std::vector<Event> events;
    interpreter.set_output_hook(
        [&events, &interpreter](
            const ProcessId process_value,
            const std::string_view text,
            const bool newline,
            const SimulationTick time,
            const std::uint64_t delta) {
            events.push_back(
                { process_value,
                    std::string { text },
                    newline,
                    time,
                    delta,
                    interpreter.scheduler()
                        .current_phase()
                        .value_or(SchedulerPhase::active) });
        });
    interpreter.start();
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed,
        "display process must complete");
    require(
        events.size() == 4
            && events[0].process == process_id
            && events[0].text == "first"
            && events[0].newline
            && events[0].time == 0
            && events[0].delta == 0
            && events[0].phase == SchedulerPhase::active
            && events[1].process == process_id
            && events[1].text == "postponed"
            && events[1].newline
            && events[1].time == 0
            && events[1].delta == 0
            && events[1].phase == SchedulerPhase::postponed
            && events[2].process == second_process_id
            && events[2].text == "second-postponed"
            && events[2].newline
            && events[2].time == 0
            && events[2].delta == 0
            && events[2].phase == SchedulerPhase::postponed
            && events[3].process == process_id
            && events[3].text == "tail"
            && !events[3].newline
            && events[3].time == 2
            && events[3].delta == 0
            && events[3].phase == SchedulerPhase::active,
        "immediate/postponed display hook ordering and metadata");

    Interpreter report_interpreter;
    Process report_process;
    report_process.name = "reports";
    report_process.register_count = 6;
    report_process.operations = {
        Report {
            "warning",
            AssertionSeverity::warning,
            SourceLocation { "report.vhd", 9, 5 } },
        Report {
            "error",
            AssertionSeverity::error,
            SourceLocation { "report.vhd", 10, 5 } },
        LoadConstant { 0, PackedLogic4::from_msb_string("10xz") },
        FormatDisplay {
            0,
            OutputFormat::binary,
            "v=",
            "!",
            true,
            false },
        LoadConstant {
            2, PackedLogic4::from_msb_string("11111111") },
        FormatDisplay {
            2,
            OutputFormat::decimal,
            "d=",
            "",
            true,
            false,
            true },
        LoadConstant { 3, PackedLogic4::from_msb_string("10xz") },
        FormatDisplay {
            3,
            OutputFormat::decimal,
            "u=",
            "",
            true,
            false,
            false },
        LoadConstant {
            1, PackedLogic4::from_msb_string("10100101") },
        FormatDisplay {
            1,
            OutputFormat::hexadecimal,
            "h=",
            "",
            true,
            false },
        FormatDisplay {
            1,
            OutputFormat::character,
            "c=",
            "",
            true,
            false },
        LoadConstant {
            4, PackedLogic4::from_msb_string("01110100011001010111001101110100") },
        FormatDisplay {
            4,
            OutputFormat::string,
            "s=",
            "",
            true,
            false },
        LoadConstant {
            5, PackedLogic4::from_msb_string("0000000010100101") },
        FormatDisplay {
            5,
            OutputFormat::hexadecimal,
            "z=",
            "",
            true,
            false,
            false,
            true },
        FormatDisplay {
            1,
            OutputFormat::hexadecimal,
            "width=",
            "",
            true,
            false,
            false,
            false,
            6 },
        FormatDisplay {
            1,
            OutputFormat::hexadecimal,
            "left=",
            "!",
            true,
            false,
            false,
            false,
            6,
            true },
        FormatDisplay {
            2,
            OutputFormat::decimal,
            "zero=",
            "",
            true,
            false,
            true,
            false,
            6,
            false,
            true },
        WaitFor { 7 },
        TimeDisplay { "time=", "", true, false, 4, false, true },
        Halt { },
    };
    const auto report_process_id = report_interpreter.add_process(std::move(report_process));
    struct ObservedReport {
        ProcessId process { };
        std::string message;
        AssertionSeverity severity { AssertionSeverity::note };
        SourceLocation source;
        SimulationTick time { };
        std::uint64_t delta { };
    };
    std::vector<ObservedReport> reports;
    std::vector<std::string> formatted_output;
    report_interpreter.set_report_hook(
        [&reports](
            const ProcessId process_value,
            const std::string_view message,
            const AssertionSeverity severity,
            const SourceLocation& source,
            const SimulationTick time,
            const std::uint64_t delta) {
            reports.push_back(
                { process_value,
                    std::string { message },
                    severity,
                    source,
                    time,
                    delta });
        });
    report_interpreter.set_output_hook(
        [&formatted_output](
            const ProcessId,
            const std::string_view text,
            const bool,
            const SimulationTick,
            const std::uint64_t) {
            formatted_output.emplace_back(text);
        });
    report_interpreter.start();
    const auto report_result = report_interpreter.run();
    require(
        report_result.status == RunStatus::completed
            && reports.size() == 2
            && reports[0].process == report_process_id
            && reports[0].message == "warning"
            && reports[0].severity == AssertionSeverity::warning
            && reports[0].source.line == 9
            && reports[0].time == 0
            && reports[0].delta == 0
            && reports[1].severity == AssertionSeverity::error
            && formatted_output
                == std::vector<std::string> {
                    "v=10xz!", "d=-1", "u=x", "h=a5", "c=\xA5",
                    "s=test", "z=a5", "width=    a5",
                    "left=a5    !", "zero=-00001", "time=0007" },
        "nonfatal report hook severity, source, and ordering");

    Interpreter exact_time_interpreter;
    exact_time_interpreter.set_time_resolution_femtoseconds(2);
    Process exact_time_process;
    exact_time_process.name = "exact-time-format";
    exact_time_process.register_count = 3;
    exact_time_process.string_register_count = 1;
    exact_time_process.operations = {
        LoadConstant {
            0,
            PackedLogic4::from_aval_bval(
                32, static_cast<std::uint32_t>(-12), 0) },
        LoadConstant { 1, PackedLogic4::from_aval_bval(32, 3, 0) },
        LoadConstant { 2, PackedLogic4::from_aval_bval(32, 12, 0) },
        LoadStringConstant { 0, " ps" },
        TimeFormatControl { 0, 1, 0, 2 },
        WaitFor { 1250 },
        TimeDisplay { "exact=", "", true, false, 0, false, false, true },
        TimeDisplay { "zero=", "", true, false, 0, false, false, false },
        TimeDisplay { "wide=", "", true, false, 16, false, false, false },
        Halt { },
    };
    const auto exact_time_process_id = exact_time_interpreter.add_process(
        std::move(exact_time_process));
    (void)exact_time_process_id;
    std::vector<std::string> exact_time_output;
    exact_time_interpreter.set_output_hook(
        [&exact_time_output](
            const ProcessId,
            const std::string_view text,
            const bool,
            const SimulationTick,
            const std::uint64_t) {
            exact_time_output.emplace_back(text);
        });
    exact_time_interpreter.start();
    const auto exact_time_result = exact_time_interpreter.run();
    require(
        exact_time_result.status == RunStatus::completed
            && exact_time_result.time == 1250
            && exact_time_output
                == std::vector<std::string> {
                    "exact=    2.500 ps",
                    "zero=2.500 ps",
                    "wide=        2.500 ps" },
        "$timeformat exact non-power-of-ten scaling and width policy");

    Interpreter monitor_replacement_interpreter;
    const auto monitored_signal = monitor_replacement_interpreter.add_signal(
        { "watched", PackedLogic4::from_msb_string("0") });
    Process monitor_replacement_process;
    monitor_replacement_process.name = "monitor-replacement";
    monitor_replacement_process.register_count = 1;
    monitor_replacement_process.operations = {
        MonitorInstall {
            {
                MonitorValue {
                    MonitorValueKind::signal,
                    monitored_signal,
                    OutputFormat::binary,
                    "value=" },
            },
            "",
            true,
            false,
            std::nullopt },
        MonitorInstall {
            { }, "literal replacement", true, false, std::nullopt },
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        WriteBlocking { monitored_signal, 0 },
        Halt { },
    };
    static_cast<void>(
        monitor_replacement_interpreter.add_process(
            std::move(monitor_replacement_process)));
    std::vector<std::string> replacement_output;
    monitor_replacement_interpreter.set_output_hook(
        [&replacement_output](
            const ProcessId,
            const std::string_view text,
            const bool,
            const SimulationTick,
            const std::uint64_t) {
            replacement_output.emplace_back(text);
        });
    monitor_replacement_interpreter.start();
    const auto monitor_replacement_result = monitor_replacement_interpreter.run();
    require(
        monitor_replacement_result.status == RunStatus::completed
            && replacement_output
                == std::vector<std::string> { "literal replacement" },
        "literal monitor replacement cancels the previous watched list");

    Interpreter failure_interpreter;
    Process failure_process;
    failure_process.name = "failure-report";
    failure_process.operations = {
        Report {
            "terminal",
            AssertionSeverity::failure,
            SourceLocation { "failure.vhd", 12, 7 } },
        Halt { }
    };
    const auto failure_process_id = failure_interpreter.add_process(std::move(failure_process));
    std::size_t failure_reports { };
    failure_interpreter.set_report_hook(
        [&failure_reports](
            const ProcessId,
            const std::string_view,
            const AssertionSeverity,
            const SourceLocation&,
            const SimulationTick,
            const std::uint64_t) {
            ++failure_reports;
        });
    failure_interpreter.start();
    bool caught_failure = false;
    try {
        static_cast<void>(failure_interpreter.run());
    } catch (const AssertionError& error) {
        caught_failure = error.process() == failure_process_id
            && error.instruction() == 0
            && error.severity() == AssertionSeverity::failure
            && error.source().path == "failure.vhd"
            && std::string_view { error.what() }.find("terminal")
                != std::string_view::npos;
    }
    require(
        caught_failure && failure_reports == 1,
        "failure report callback-before-termination semantics");
}

void test_deterministic_random_values()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto run_sequence =
        [](const std::uint64_t seed) {
            Interpreter interpreter { { 1000, 32 }, seed };
            std::array<SignalId, 4> outputs { };
            for (std::size_t index = 0; index < outputs.size(); ++index) {
                outputs[index] = interpreter.add_signal(
                    { "random_" + std::to_string(index),
                        PackedLogic4(32, Logic4::zero) });
            }
            Process process;
            process.name = "random-sequence";
            process.register_count = 6;
            process.operations = {
                RandomValue {
                    0,
                    RandomKind::urandom,
                    std::nullopt,
                    std::nullopt },
                WriteBlocking { outputs[0], 0 },
                RandomValue {
                    1,
                    RandomKind::random,
                    std::nullopt,
                    std::nullopt },
                WriteBlocking { outputs[1], 1 },
                LoadConstant {
                    2, PackedLogic4::from_aval_bval(32, 9, 0) },
                RandomValue {
                    3,
                    RandomKind::urandom_range,
                    2,
                    std::nullopt },
                WriteBlocking { outputs[2], 3 },
                LoadConstant {
                    4, PackedLogic4::from_aval_bval(32, 3, 0) },
                LoadConstant {
                    5, PackedLogic4::from_aval_bval(32, 9, 0) },
                RandomValue {
                    3,
                    RandomKind::urandom_range,
                    4,
                    5 },
                WriteBlocking { outputs[3], 3 },
                Halt { },
            };
            static_cast<void>(
                interpreter.add_process(std::move(process)));
            interpreter.start();
            const auto result = interpreter.run();
            require(
                result.status == RunStatus::completed,
                "random sequence must complete");
            std::array<Logic4Word, 4> values { };
            for (std::size_t index = 0; index < outputs.size(); ++index) {
                values[index] = interpreter.signal_value(outputs[index]).low_word();
            }
            return values;
        };

    const auto first = run_sequence(42);
    const auto repeated = run_sequence(42);
    const auto changed_seed = run_sequence(43);
    require(
        first == repeated,
        "the same project seed must reproduce the random sequence");
    require(
        first[0] != changed_seed[0],
        "a changed project seed must change the process stream");
    require(
        first[2].bval == 0 && first[2].aval <= 9,
        "one-bound urandom_range inclusive bounds");
    require(
        first[3].bval == 0
            && first[3].aval >= 3 && first[3].aval <= 9,
        "two-bound urandom_range and reversed-bound normalization");

    const auto run_after_optional_unknown =
        [](const bool include_unknown) {
            Interpreter interpreter { { 1000, 32 }, 77 };
            const auto random_output = interpreter.add_signal(
                { "random", PackedLogic4(32, Logic4::zero) });
            const auto unknown_output = interpreter.add_signal(
                { "unknown", PackedLogic4(32, Logic4::zero) });
            Process process;
            process.name = "unknown-bound";
            process.register_count = 3;
            if (include_unknown) {
                process.operations.emplace_back(
                    LoadConstant { 0, PackedLogic4(32, Logic4::x) });
                process.operations.emplace_back(
                    RandomValue {
                        1,
                        RandomKind::urandom_range,
                        0,
                        std::nullopt });
                process.operations.emplace_back(
                    WriteBlocking { unknown_output, 1 });
            }
            process.operations.emplace_back(
                RandomValue {
                    2,
                    RandomKind::urandom,
                    std::nullopt,
                    std::nullopt });
            process.operations.emplace_back(
                WriteBlocking { random_output, 2 });
            process.operations.emplace_back(Halt { });
            static_cast<void>(
                interpreter.add_process(std::move(process)));
            interpreter.start();
            static_cast<void>(interpreter.run());
            return std::pair {
                interpreter.signal_value(random_output),
                interpreter.signal_value(unknown_output)
            };
        };
    const auto [after_unknown, unknown_result] = run_after_optional_unknown(true);
    const auto [without_unknown, unused] = run_after_optional_unknown(false);
    static_cast<void>(unused);
    require(
        unknown_result
                == PackedLogic4(32, Logic4::x)
            && after_unknown == without_unknown,
        "unknown range bounds return X without consuming the stream");

    Interpreter per_process { { 1000, 32 }, 99 };
    const auto first_process_output = per_process.add_signal(
        { "first", PackedLogic4(32, Logic4::zero) });
    const auto second_process_output = per_process.add_signal(
        { "second", PackedLogic4(32, Logic4::zero) });
    for (ProcessId id = 0; id < 2; ++id) {
        Process process;
        process.id = id;
        process.name = "random-process-" + std::to_string(id);
        process.register_count = 1;
        process.operations = {
            RandomValue {
                0,
                RandomKind::urandom,
                std::nullopt,
                std::nullopt },
            WriteBlocking {
                id == 0 ? first_process_output : second_process_output,
                0 },
            Halt { },
        };
        static_cast<void>(
            per_process.add_process(std::move(process)));
    }
    per_process.start();
    static_cast<void>(per_process.run());
    require(
        per_process.signal_value(first_process_output)
            != per_process.signal_value(second_process_output),
        "stable process IDs derive independent random streams");
}

} // namespace fsim::tests::runtime
