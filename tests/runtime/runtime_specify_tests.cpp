// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

    void require(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

} // namespace

void test_transition_delay_selection()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const TransitionDelays delays { 7, 11, 13 };
    require(
        transition_delay(
            PackedLogic4::from_msb_string("0000"),
            PackedLogic4::from_msb_string("1000"),
            delays)
            == 7,
        "a rising transition selects the rise delay");
    require(
        transition_delay(
            PackedLogic4::from_msb_string("1111"),
            PackedLogic4::from_msb_string("1011"),
            delays)
            == 11,
        "a falling transition selects the fall delay");
    require(
        transition_delay(
            PackedLogic4::from_msb_string("1111"),
            PackedLogic4::from_msb_string("11Z1"),
            delays)
            == 13,
        "a high-impedance transition selects the turnoff delay");
    require(
        transition_delay(
            PackedLogic4::from_msb_string("0000"),
            PackedLogic4::from_msb_string("00X0"),
            delays)
            == 7,
        "a transition to unknown selects the shortest delay");
    require(
        transition_delay(
            PackedLogic4::from_msb_string("0000"),
            PackedLogic4::from_msb_string("1Z00"),
            delays)
            == 7,
        "a packed mixed transition selects the shortest applicable delay");
    require(
        !transition_delay(
            PackedLogic4::from_msb_string("10XZ"),
            PackedLogic4::from_msb_string("10XZ"),
            delays),
        "an unchanged packed value has no transition delay");

    bool rejected = false;
    try {
        static_cast<void>(
            transition_delay(
                PackedLogic4::from_msb_string("0"),
                PackedLogic4::from_msb_string("00"),
                delays));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "transition-delay width mismatch must be rejected");

    const std::array<SimulationTick, 1> one { 9 };
    require(
        module_path_transition_delay(Logic4::z, Logic4::x, one) == 9,
        "one-value module path table applies to every transition");
    const std::array<SimulationTick, 2> two { 7, 11 };
    require(
        module_path_transition_delay(Logic4::zero, Logic4::z, two) == 7
            && module_path_transition_delay(
                   Logic4::one, Logic4::zero, two)
                == 11,
        "two-value module path table derives turnoff from rise and fall");
    const std::array<SimulationTick, 3> three { 7, 11, 13 };
    require(
        module_path_transition_delay(
            Logic4::zero, Logic4::x, three)
                == 7
            && module_path_transition_delay(
                   Logic4::one, Logic4::x, three)
                == 11
            && module_path_transition_delay(
                   Logic4::x, Logic4::z, three)
                == 13,
        "three-value module path table derives unknown transitions");
    const std::array<SimulationTick, 6> six { 1, 2, 3, 4, 5, 6 };
    require(
        module_path_transition_delay(Logic4::zero, Logic4::x, six) == 1
            && module_path_transition_delay(
                   Logic4::x, Logic4::one, six)
                == 4
            && module_path_transition_delay(
                   Logic4::one, Logic4::x, six)
                == 2
            && module_path_transition_delay(
                   Logic4::x, Logic4::zero, six)
                == 6
            && module_path_transition_delay(
                   Logic4::x, Logic4::z, six)
                == 5
            && module_path_transition_delay(
                   Logic4::z, Logic4::x, six)
                == 4,
        "six-value module path table uses exact unknown min/max derivation");
    const std::array<SimulationTick, 12> twelve {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
    };
    require(
        module_path_transition_delay(
            Logic4::zero, Logic4::x, twelve)
                == 7
            && module_path_transition_delay(
                   Logic4::x, Logic4::one, twelve)
                == 8
            && module_path_transition_delay(
                   Logic4::one, Logic4::x, twelve)
                == 9
            && module_path_transition_delay(
                   Logic4::x, Logic4::zero, twelve)
                == 10
            && module_path_transition_delay(
                   Logic4::x, Logic4::z, twelve)
                == 11
            && module_path_transition_delay(
                   Logic4::z, Logic4::x, twelve)
                == 12,
        "twelve-value module path table selects explicit unknown entries");
    require(
        !module_path_transition_delay(
            Logic4::one, Logic4::one, twelve),
        "unchanged module path values have no transition");
}

void test_simir_module_timing_checks()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto run_check = [](
                               const ModuleTimingCheckKind kind,
                               const ModulePathEdge reference_edge,
                               const std::initializer_list<std::pair<SimulationTick, bool>>
                                   reference_changes,
                               const std::initializer_list<std::pair<SimulationTick, bool>>
                                   data_changes,
                               const bool condition_enabled = true,
                               const std::optional<SimulationTick> threshold = std::nullopt) {
        Interpreter interpreter;
        const auto reference = interpreter.add_signal(
            { "reference", PackedLogic4::from_msb_string("0") });
        const auto data = interpreter.add_signal(
            { "data", PackedLogic4::from_msb_string("0") });
        const auto condition = interpreter.add_signal(
            { "condition", PackedLogic4::from_msb_string(condition_enabled ? "1" : "0") });
        const auto notifier = interpreter.add_signal(
            { "notifier", PackedLogic4::from_msb_string("0") });

        ModuleTimingCheck check;
        check.id = 0;
        check.identity = "runtime:timing-check:0";
        check.kind = kind;
        check.reference.terminal = { reference, 0, 1 };
        check.reference.edge = reference_edge;
        ModulePathExpressionNode condition_node;
        condition_node.operation = ModulePathExpressionOperator::terminal;
        condition_node.terminal = { condition, 0, 1 };
        condition_node.width = 1;
        check.reference.condition.nodes.push_back(std::move(condition_node));
        check.reference.condition.root = 0;
        if (kind != ModuleTimingCheckKind::period
            && kind != ModuleTimingCheckKind::width) {
            ModuleTimingEvent data_event;
            data_event.terminal = { data, 0, 1 };
            data_event.edge = ModulePathEdge::posedge;
            check.data = std::move(data_event);
        }
        check.limits = { 5 };
        check.threshold = threshold;
        check.notifier = notifier;
        static_cast<void>(
            interpreter.add_module_timing_check(std::move(check)));

        std::size_t reports = 0;
        interpreter.set_report_hook(
            [&](const ProcessId,
                const std::string_view message,
                const AssertionSeverity severity,
                const SourceLocation&,
                const SimulationTick,
                const std::uint64_t) {
                require(
                    message == "Verilog specify timing-check violation"
                        && severity == AssertionSeverity::error,
                    "a module timing check reports exact violation metadata");
                ++reports;
            });
        for (const auto& [time, value] : reference_changes) {
            interpreter.schedule_signal_at(
                reference,
                PackedLogic4::from_msb_string(value ? "1" : "0"),
                time,
                0);
        }
        for (const auto& [time, value] : data_changes) {
            interpreter.schedule_signal_at(
                data,
                PackedLogic4::from_msb_string(value ? "1" : "0"),
                time,
                1);
        }
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed,
            "a module timing-check run completes");
        return std::pair {
            reports,
            interpreter.signal_value(notifier).to_msb_string()
        };
    };

    const auto setup = run_check(
        ModuleTimingCheckKind::setup,
        ModulePathEdge::posedge,
        { { 8, true } },
        { { 5, true } });
    require(
        setup == std::pair<std::size_t, std::string> { 1, "1" },
        "$setup checks the data-to-reference interval and toggles its notifier");
    const auto hold = run_check(
        ModuleTimingCheckKind::hold,
        ModulePathEdge::posedge,
        { { 5, true } },
        { { 8, true } });
    require(
        hold == std::pair<std::size_t, std::string> { 1, "1" },
        "$hold checks the reference-to-data interval");
    const auto recovery = run_check(
        ModuleTimingCheckKind::recovery,
        ModulePathEdge::posedge,
        { { 5, true } },
        { { 8, true } });
    require(
        recovery == std::pair<std::size_t, std::string> { 1, "1" },
        "$recovery checks control release before the clock event");
    const auto removal = run_check(
        ModuleTimingCheckKind::removal,
        ModulePathEdge::posedge,
        { { 8, true } },
        { { 5, true } });
    require(
        removal == std::pair<std::size_t, std::string> { 1, "1" },
        "$removal checks control release after the clock event");
    const auto skew = run_check(
        ModuleTimingCheckKind::skew,
        ModulePathEdge::posedge,
        { { 5, true } },
        { { 12, true } });
    require(
        skew == std::pair<std::size_t, std::string> { 1, "1" },
        "$skew checks the maximum reference-to-data interval");
    const auto period = run_check(
        ModuleTimingCheckKind::period,
        ModulePathEdge::posedge,
        { { 5, true }, { 6, false }, { 8, true } },
        { });
    require(
        period == std::pair<std::size_t, std::string> { 1, "1" },
        "$period checks consecutive selected reference events");
    const auto width = run_check(
        ModuleTimingCheckKind::width,
        ModulePathEdge::negedge,
        { { 5, true }, { 8, false } },
        { });
    require(
        width == std::pair<std::size_t, std::string> { 1, "1" },
        "$width checks the preceding opposite-edge interval");
    const auto below_threshold = run_check(
        ModuleTimingCheckKind::width,
        ModulePathEdge::negedge,
        { { 5, true }, { 8, false } },
        { },
        true,
        3);
    require(
        below_threshold == std::pair<std::size_t, std::string> { 0, "0" },
        "$width suppresses pulse intervals at or below its threshold");
    const auto disabled = run_check(
        ModuleTimingCheckKind::setup,
        ModulePathEdge::posedge,
        { { 8, true } },
        { { 5, true } },
        false);
    require(
        disabled == std::pair<std::size_t, std::string> { 0, "0" },
        "a false timing-event condition suppresses history and violations");

    struct CompoundResult {
        std::size_t reports { };
        std::string notifier;
        std::vector<SimulationTick> report_times;
        std::vector<SimulationTick> delayed_reference_times;
        std::vector<SimulationTick> delayed_data_times;
    };
    const auto run_compound = [](
                                  const ModuleTimingCheckKind kind,
                                  std::vector<std::int64_t> limits,
                                  const std::initializer_list<std::pair<SimulationTick, bool>>
                                      reference_changes,
                                  const std::initializer_list<std::pair<SimulationTick, bool>>
                                      data_changes,
                                  const bool event_based = false,
                                  const bool remain_active = false,
                                  const bool timestamp_enabled = true,
                                  const bool timecheck_enabled = true) {
        Interpreter interpreter;
        const auto reference = interpreter.add_signal(
            { "compound-reference", PackedLogic4::from_msb_string("0") });
        const auto data = interpreter.add_signal(
            { "compound-data", PackedLogic4::from_msb_string("0") });
        const auto notifier = interpreter.add_signal(
            { "compound-notifier", PackedLogic4::from_msb_string("0") });
        const auto delayed_reference = interpreter.add_signal(
            { "delayed-reference", PackedLogic4::from_msb_string("0") });
        const auto delayed_data = interpreter.add_signal(
            { "delayed-data", PackedLogic4::from_msb_string("0") });
        const auto timestamp_condition = interpreter.add_signal({ "timestamp-condition",
            PackedLogic4::from_msb_string(
                timestamp_enabled ? "1" : "0") });
        const auto timecheck_condition = interpreter.add_signal({ "timecheck-condition",
            PackedLogic4::from_msb_string(
                timecheck_enabled ? "1" : "0") });

        const auto terminal_expression = [](
                                             const SignalId signal) {
            ModulePathExpression expression;
            ModulePathExpressionNode node;
            node.operation = ModulePathExpressionOperator::terminal;
            node.terminal = { signal, 0, 1 };
            node.width = 1;
            expression.nodes.push_back(std::move(node));
            return expression;
        };
        ModuleTimingCheck check;
        check.id = 0;
        check.identity = "runtime:compound-timing-check:0";
        check.kind = kind;
        check.reference.terminal = { reference, 0, 1 };
        check.reference.edge = ModulePathEdge::posedge;
        ModuleTimingEvent data_event;
        data_event.terminal = { data, 0, 1 };
        data_event.edge = ModulePathEdge::posedge;
        check.data = std::move(data_event);
        check.limits = std::move(limits);
        check.notifier = notifier;
        check.timestamp_condition = terminal_expression(timestamp_condition);
        check.timecheck_condition = terminal_expression(timecheck_condition);
        check.delayed_reference = ModulePathTerminal {
            delayed_reference, 0, 1
        };
        check.delayed_data = ModulePathTerminal { delayed_data, 0, 1 };
        check.event_based = event_based;
        check.remain_active = remain_active;
        static_cast<void>(
            interpreter.add_module_timing_check(std::move(check)));

        CompoundResult result;
        interpreter.set_report_hook(
            [&](const ProcessId,
                const std::string_view,
                const AssertionSeverity,
                const SourceLocation&,
                const SimulationTick time,
                const std::uint64_t) {
                ++result.reports;
                result.report_times.push_back(time);
            });
        interpreter.set_signal_change_hook(
            [&](const SignalId signal,
                const PackedLogic4&,
                const SimulationTick time) {
                if (signal == delayed_reference) {
                    result.delayed_reference_times.push_back(time);
                }
                if (signal == delayed_data) {
                    result.delayed_data_times.push_back(time);
                }
            });
        for (const auto& [time, value] : reference_changes) {
            interpreter.schedule_signal_at(
                reference,
                PackedLogic4::from_msb_string(value ? "1" : "0"),
                time,
                0);
        }
        for (const auto& [time, value] : data_changes) {
            interpreter.schedule_signal_at(
                data,
                PackedLogic4::from_msb_string(value ? "1" : "0"),
                time,
                1);
        }
        const auto run = interpreter.run();
        require(
            run.status == RunStatus::completed,
            "a compound module timing-check run completes");
        result.notifier = interpreter.signal_value(notifier).to_msb_string();
        return result;
    };

    const auto setuphold = run_compound(
        ModuleTimingCheckKind::setuphold,
        { -2, 5 },
        { { 5, true } },
        { { 8, true } });
    require(
        setuphold.reports == 1 && setuphold.notifier == "1"
            && setuphold.report_times == std::vector<SimulationTick> { 8 }
            && setuphold.delayed_reference_times
                == std::vector<SimulationTick> { 7 }
            && setuphold.delayed_data_times
                == std::vector<SimulationTick> { 8 },
        "$setuphold supports a negative setup limit and delayed signals");
    const auto setup_boundary = run_compound(
        ModuleTimingCheckKind::setuphold,
        { 5, 5 },
        { { 10, true } },
        { { 5, true } });
    const auto hold_boundary = run_compound(
        ModuleTimingCheckKind::setuphold,
        { 5, 5 },
        { { 5, true } },
        { { 10, true } });
    require(
        setup_boundary.reports == 0 && hold_boundary.reports == 0,
        "$setuphold excludes events exactly on either timing-window boundary");
    const auto simultaneous = run_compound(
        ModuleTimingCheckKind::setuphold,
        { 5, 5 },
        { { 5, true } },
        { { 5, true } });
    require(
        simultaneous.reports == 1
            && simultaneous.report_times
                == std::vector<SimulationTick> { 5 },
        "$setuphold handles simultaneous reference and data events in stable "
        "scheduler order");
    const auto negative_hold = run_compound(
        ModuleTimingCheckKind::setuphold,
        { 5, -2 },
        { { 5, true } },
        { { 2, true } });
    require(
        negative_hold.reports == 1
            && negative_hold.delayed_data_times
                == std::vector<SimulationTick> { 4 },
        "$setuphold shifts delayed data for a negative hold limit");
    const auto timestamp_disabled = run_compound(
        ModuleTimingCheckKind::setuphold,
        { 5, 5 },
        { { 8, true } },
        { { 5, true } },
        false,
        false,
        false,
        true);
    require(
        timestamp_disabled.reports == 0,
        "a false timestamp condition suppresses compound-check history");
    const auto timecheck_disabled = run_compound(
        ModuleTimingCheckKind::setuphold,
        { 5, 5 },
        { { 8, true } },
        { { 5, true } },
        false,
        false,
        true,
        false);
    require(
        timecheck_disabled.reports == 0,
        "a false timecheck condition suppresses compound-check violations");
    const auto recrem = run_compound(
        ModuleTimingCheckKind::recrem,
        { 5, 5 },
        { { 5, true } },
        { { 8, true } });
    require(
        recrem.reports == 1 && recrem.report_times.front() == 8,
        "$recrem combines recovery and removal windows");
    const auto event_timeskew = run_compound(
        ModuleTimingCheckKind::timeskew,
        { 5 },
        { { 5, true } },
        { { 12, true } },
        true,
        true);
    require(
        event_timeskew.reports == 1
            && event_timeskew.report_times.front() == 12,
        "event-based $timeskew reports on a late data event");
    const auto inactive_timeskew = run_compound(
        ModuleTimingCheckKind::timeskew,
        { 5 },
        { { 5, true } },
        { { 12, true }, { 13, false }, { 14, true } },
        true,
        false);
    require(
        inactive_timeskew.reports == 1,
        "$timeskew becomes inactive after its first event-based violation");
    const auto active_timeskew = run_compound(
        ModuleTimingCheckKind::timeskew,
        { 5 },
        { { 5, true } },
        { { 12, true }, { 13, false }, { 14, true } },
        true,
        true);
    require(
        active_timeskew.reports == 2,
        "$timeskew remain-active mode reports repeated late data events");
    const auto timer_timeskew = run_compound(
        ModuleTimingCheckKind::timeskew,
        { 5 },
        { { 5, true } },
        { });
    require(
        timer_timeskew.reports == 1
            && timer_timeskew.report_times.front() == 10,
        "timer-based $timeskew reports when its window expires");
    const auto fullskew = run_compound(
        ModuleTimingCheckKind::fullskew,
        { 5, 3 },
        { { 10, true } },
        { { 5, true } },
        true,
        true);
    require(
        fullskew.reports == 1,
        "$fullskew applies its distinct data and reference limits");
    const auto timer_fullskew = run_compound(
        ModuleTimingCheckKind::fullskew,
        { 5, 3 },
        { { 5, true } },
        { });
    require(
        timer_fullskew.reports == 1
            && timer_fullskew.report_times.front() == 10,
        "timer-based $fullskew reports a missing data event");
    const auto nochange = run_compound(
        ModuleTimingCheckKind::nochange,
        { -2, 4 },
        { { 6, true } },
        { { 5, true } });
    require(
        nochange.reports == 1,
        "$nochange applies signed start and end offsets around reference");

    Interpreter invalid;
    const auto invalid_signal = invalid.add_signal(
        { "invalid-timing", PackedLogic4::from_msb_string("0") });
    ModuleTimingCheck invalid_check;
    invalid_check.id = 1;
    invalid_check.identity = "runtime:invalid-timing-check:0";
    invalid_check.kind = ModuleTimingCheckKind::period;
    invalid_check.reference.terminal = { invalid_signal, 0, 1 };
    invalid_check.reference.edge = ModulePathEdge::posedge;
    invalid_check.limits = { 1 };
    bool invalid_id_rejected = false;
    try {
        static_cast<void>(
            invalid.add_module_timing_check(invalid_check));
    } catch (const std::invalid_argument&) {
        invalid_id_rejected = true;
    }
    require(
        invalid_id_rejected,
        "a nondense timing-check id rejects before mutating runtime state");
    invalid_check.id = 0;
    invalid_check.kind = ModuleTimingCheckKind::setup;
    invalid_check.data = invalid_check.reference;
    invalid_check.limits = { -1 };
    bool invalid_limit_rejected = false;
    try {
        static_cast<void>(
            invalid.add_module_timing_check(std::move(invalid_check)));
    } catch (const std::invalid_argument&) {
        invalid_limit_rejected = true;
    }
    require(
        invalid_limit_rejected,
        "a negative simple timing limit rejects transactionally");

    Interpreter overflow;
    const auto overflow_source = overflow.add_signal(
        { "overflow-source", PackedLogic4::from_msb_string("0") });
    const auto overflow_output = overflow.add_signal(
        { "overflow-output", PackedLogic4::from_msb_string("0") });
    Process overflow_driver;
    overflow_driver.id = 0;
    overflow_driver.name = "module-path-overflow";
    overflow_driver.register_count = 1;
    overflow_driver.initialize = false;
    overflow_driver.static_sensitivity.push_back(
        { overflow_source, EdgeKind::any });
    overflow_driver.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        WriteBlocking { overflow_output, 0 },
        Halt { },
    };
    static_cast<void>(overflow.add_process(std::move(overflow_driver)));
    ModulePath overflow_path;
    overflow_path.id = 0;
    overflow_path.identity = "runtime:overflow-path:0";
    overflow_path.sources.push_back({ overflow_source, 0, 1 });
    overflow_path.destinations.push_back({ overflow_output, 0, 1 });
    overflow_path.drivers.push_back(0);
    overflow_path.delays.push_back(5);
    static_cast<void>(overflow.add_module_path(std::move(overflow_path)));
    overflow.schedule_signal_at(
        overflow_source,
        PackedLogic4::from_msb_string("1"),
        std::numeric_limits<SimulationTick>::max() - 2,
        0);
    bool overflow_rejected = false;
    try {
        static_cast<void>(overflow.run());
    } catch (const std::overflow_error&) {
        overflow_rejected = true;
    }
    require(
        overflow_rejected,
        "module-path scheduling rejects simulation-time overflow");
}

void test_simir_module_paths()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter { { 1000, 64 } };
    const auto input = interpreter.add_signal(
        { "path.input", PackedLogic4::from_msb_string("0") });
    const auto output = interpreter.add_signal(
        { "path.output", PackedLogic4::from_msb_string("0") });
    const auto enable = interpreter.add_signal(
        { "path.enable", PackedLogic4::from_msb_string("X") });

    Process stimulus;
    stimulus.id = 0;
    stimulus.name = "path-stimulus";
    stimulus.register_count = 3;
    stimulus.driver_regions = {
        { input, 0, 1, true }, { enable, 0, 1, true }
    };
    stimulus.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        WriteUpdate { input, 0 },
        WaitFor { 10 },
        LoadConstant { 1, PackedLogic4::from_msb_string("1") },
        WriteUpdate { enable, 1 },
        WaitFor { 10 },
        LoadConstant { 2, PackedLogic4::from_msb_string("0") },
        WriteUpdate { input, 2 },
        WaitFor { 20 },
        Halt { }
    };
    (void)interpreter.add_process(std::move(stimulus));

    Process driver;
    driver.id = 1;
    driver.name = "path-driver";
    driver.register_count = 1;
    driver.static_sensitivity = { { input, EdgeKind::any } };
    driver.driver_regions = { { output, 0, 1, true } };
    driver.operations = {
        ReadSignal { 0, input },
        WriteInertial { output, 0, { 2, 4, 6 } },
        WaitSensitivity { },
        Jump { 0 }
    };
    (void)interpreter.add_process(std::move(driver));
    ModulePath conditional;
    conditional.id = 0;
    conditional.identity = "runtime:conditional-path:0";
    conditional.sources = { { input, 0, 1 } };
    conditional.destinations = { { output, 0, 1 } };
    conditional.drivers = { 1 };
    conditional.delays = { 5, 7 };
    conditional.condition.nodes.push_back(ModulePathExpressionNode {
        ModulePathExpressionOperator::terminal,
        { },
        PackedLogic4 { },
        { enable, 0, 1 },
        BinaryOperator::bit_and,
        LogicalBinaryOperator::logical_and,
        ShiftOperator::logical_left,
        ReductionOperator::bit_and,
        1,
        false });
    conditional.data_source.nodes.push_back(ModulePathExpressionNode {
        ModulePathExpressionOperator::terminal,
        { },
        PackedLogic4 { },
        { input, 0, 1 },
        BinaryOperator::bit_and,
        LogicalBinaryOperator::logical_and,
        ShiftOperator::logical_left,
        ReductionOperator::bit_and,
        1,
        false });
    conditional.conditional = true;
    conditional.source = { "module-path.v", 4, 3 };
    auto competing = conditional;
    competing.id = 1;
    competing.identity = "runtime:conditional-path:1";
    competing.delays = { 50 };
    competing.source = { "module-path.v", 5, 3 };
    (void)interpreter.add_module_path(std::move(conditional));
    (void)interpreter.add_module_path(std::move(competing));

    ModulePath fallback;
    fallback.id = 2;
    fallback.identity = "runtime:conditional-path:2";
    fallback.sources = { { input, 0, 1 } };
    fallback.destinations = { { output, 0, 1 } };
    fallback.drivers = { 1 };
    fallback.delays = { 3 };
    fallback.ifnone = true;
    fallback.source = { "module-path.v", 6, 3 };
    (void)interpreter.add_module_path(std::move(fallback));

    std::vector<std::pair<SimulationTick, std::string>> changes;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == output) {
                changes.emplace_back(time, value.to_msb_string());
            }
        });
    interpreter.start();
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && changes == std::vector<std::pair<SimulationTick, std::string>> { { 5, "1" }, { 31, "0" } },
        "module paths select conditions and accumulate intrinsic delays");

    const auto run_packed_path = [](const bool full) {
        Interpreter packed { { 1000, 64 } };
        const auto source = packed.add_signal(
            { "packed.source", PackedLogic4::from_msb_string("00") });
        const auto destination = packed.add_signal(
            { "packed.destination", PackedLogic4::from_msb_string("00") });
        Process packed_stimulus;
        packed_stimulus.id = 0;
        packed_stimulus.name = "packed-stimulus";
        packed_stimulus.register_count = 1;
        packed_stimulus.driver_regions = { { source, 0, 2, true } };
        packed_stimulus.operations = {
            LoadConstant { 0, PackedLogic4::from_msb_string("01") },
            WriteUpdate { source, 0 },
            WaitFor { 10 },
            Halt { }
        };
        (void)packed.add_process(std::move(packed_stimulus));
        Process swap;
        swap.id = 1;
        swap.name = "packed-swap";
        swap.register_count = 4;
        swap.static_sensitivity = { { source, EdgeKind::any } };
        swap.driver_regions = { { destination, 0, 2, true } };
        swap.operations = {
            ReadSignal { 0, source },
            Extract { 1, 0, 0, 1 },
            Extract { 2, 0, 1, 1 },
            Concatenate { 3, { 1, 2 }, 2 },
            WriteUpdate { destination, 3 },
            WaitSensitivity { },
            Jump { 0 }
        };
        (void)packed.add_process(std::move(swap));
        ModulePath path;
        path.identity = "runtime:packed-path:0";
        path.sources = { { source, 0, 2 } };
        path.destinations = { { destination, 0, 2 } };
        path.drivers = { 1 };
        path.delays = { 5 };
        path.full = full;
        (void)packed.add_module_path(std::move(path));
        std::vector<SimulationTick> packed_changes;
        packed.set_signal_change_hook(
            [&](const SignalId signal,
                const PackedLogic4&,
                const SimulationTick time) {
                if (signal == destination)
                    packed_changes.push_back(time);
            });
        packed.start();
        (void)packed.run();
        return packed_changes;
    };
    require(
        run_packed_path(false) == std::vector<SimulationTick> { 0 }
            && run_packed_path(true) == std::vector<SimulationTick> { 5 },
        "parallel paths pair lanes while full paths react to any source lane");

    Interpreter rejected_pulse { { 1000, 64 } };
    const auto pulse_source = rejected_pulse.add_signal(
        { "pulse.source", PackedLogic4::from_msb_string("0") });
    const auto pulse_output = rejected_pulse.add_signal(
        { "pulse.output", PackedLogic4::from_msb_string("0") });
    Process pulse_stimulus;
    pulse_stimulus.id = 0;
    pulse_stimulus.name = "module-path-pulse";
    pulse_stimulus.register_count = 2;
    pulse_stimulus.driver_regions = { { pulse_source, 0, 1, true } };
    pulse_stimulus.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        WriteUpdate { pulse_source, 0 },
        WaitFor { 2 },
        LoadConstant { 1, PackedLogic4::from_msb_string("0") },
        WriteUpdate { pulse_source, 1 },
        WaitFor { 10 },
        Halt { }
    };
    (void)rejected_pulse.add_process(std::move(pulse_stimulus));
    Process pulse_driver;
    pulse_driver.id = 1;
    pulse_driver.name = "module-path-pulse-driver";
    pulse_driver.register_count = 1;
    pulse_driver.static_sensitivity = { { pulse_source, EdgeKind::any } };
    pulse_driver.driver_regions = { { pulse_output, 0, 1, true } };
    pulse_driver.operations = {
        ReadSignal { 0, pulse_source },
        WriteUpdate { pulse_output, 0 },
        WaitSensitivity { },
        Jump { 0 }
    };
    (void)rejected_pulse.add_process(std::move(pulse_driver));
    ModulePath pulse_path;
    pulse_path.identity = "runtime:rejected-pulse-path:0";
    pulse_path.sources = { { pulse_source, 0, 1 } };
    pulse_path.destinations = { { pulse_output, 0, 1 } };
    pulse_path.drivers = { 1 };
    pulse_path.delays = { 5 };
    (void)rejected_pulse.add_module_path(std::move(pulse_path));
    std::vector<SimulationTick> rejected_changes;
    rejected_pulse.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4&,
            const SimulationTick time) {
            if (signal == pulse_output)
                rejected_changes.push_back(time);
        });
    rejected_pulse.start();
    (void)rejected_pulse.run();
    require(
        rejected_changes.empty()
            && rejected_pulse.signal_value(pulse_output)
                == PackedLogic4::from_msb_string("0"),
        "module path inertial replacement rejects a superseded pulse");

    const auto run_corrupt_pulse = [](
                                       const ModulePathPulseStyle style,
                                       std::vector<SimulationTick> delays,
                                       const SimulationTick reject,
                                       const SimulationTick error,
                                       const bool show_cancelled,
                                       const bool overlap) {
        Interpreter pulse { { 1000, 64 } };
        const auto source = pulse.add_signal(
            { "corrupt.source", PackedLogic4::from_msb_string("0") });
        const auto destination = pulse.add_signal(
            { "corrupt.destination", PackedLogic4::from_msb_string("0") });
        Process corrupt_stimulus;
        corrupt_stimulus.id = 0;
        corrupt_stimulus.name = "corrupt-pulse";
        corrupt_stimulus.register_count = overlap ? 3U : 2U;
        corrupt_stimulus.driver_regions = { { source, 0, 1, true } };
        corrupt_stimulus.operations = {
            LoadConstant { 0, PackedLogic4::from_msb_string("1") },
            WriteUpdate { source, 0 },
            WaitFor { 4 },
            LoadConstant { 1, PackedLogic4::from_msb_string("0") },
            WriteUpdate { source, 1 }
        };
        if (overlap) {
            corrupt_stimulus.operations.emplace_back(WaitFor { 2 });
            corrupt_stimulus.operations.emplace_back(LoadConstant {
                2, PackedLogic4::from_msb_string("1") });
            corrupt_stimulus.operations.emplace_back(WriteUpdate { source, 2 });
        }
        corrupt_stimulus.operations.emplace_back(WaitFor { 20 });
        corrupt_stimulus.operations.emplace_back(Halt { });
        (void)pulse.add_process(std::move(corrupt_stimulus));
        Process corrupt_driver;
        corrupt_driver.id = 1;
        corrupt_driver.name = "corrupt-pulse-driver";
        corrupt_driver.register_count = 1;
        corrupt_driver.static_sensitivity = { { source, EdgeKind::any } };
        corrupt_driver.driver_regions = { { destination, 0, 1, true } };
        corrupt_driver.operations = {
            ReadSignal { 0, source },
            WriteUpdate { destination, 0 },
            WaitSensitivity { },
            Jump { 0 }
        };
        (void)pulse.add_process(std::move(corrupt_driver));
        ModulePath path;
        path.identity = "runtime:corrupt-pulse-path:0";
        path.sources = { { source, 0, 1 } };
        path.destinations = { { destination, 0, 1 } };
        path.drivers = { 1 };
        path.delays = std::move(delays);
        path.pulse_style = style;
        path.show_cancelled = show_cancelled;
        path.pulse_reject_limit = reject;
        path.pulse_error_limit = error;
        (void)pulse.add_module_path(std::move(path));
        std::vector<std::pair<SimulationTick, std::string>> corrupt_changes;
        std::ostringstream trace_stream;
        VcdWriter trace { trace_stream, "1ns", 64 };
        const auto trace_signal = trace.declare_signal(
            "corrupt.destination", 1);
        trace.begin();
        trace.change(trace_signal, Logic4::zero);
        pulse.set_signal_change_hook(
            [&](const SignalId signal,
                const PackedLogic4& changed,
                const SimulationTick time) {
                if (signal == destination) {
                    corrupt_changes.emplace_back(time, changed.to_msb_string());
                    trace.set_time(time);
                    trace.change(trace_signal, changed.get(0));
                }
            });
        pulse.start();
        (void)pulse.run();
        trace.flush();
        return std::pair { corrupt_changes, trace_stream.str() };
    };
    const auto onevent_changes = run_corrupt_pulse(
        ModulePathPulseStyle::onevent, { 10 }, 2, 8, false, false);
    require(
        onevent_changes.first
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 10, "X" }, { 14, "0" } },
        "onevent pulse corruption publishes X at the pending event time");
    require(
        onevent_changes.second.find("#10") != std::string::npos
            && onevent_changes.second.find('x') != std::string::npos,
        "module path pulse corruption is visible in VCD output");
    require(
        run_corrupt_pulse(
            ModulePathPulseStyle::ondetect, { 10 }, 2, 8, false, false)
                .first
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 4, "X" }, { 14, "0" } },
        "ondetect pulse corruption publishes X when the pulse is detected");
    require(
        run_corrupt_pulse(
            ModulePathPulseStyle::onevent, { 10, 2 }, 0, 0, true, false)
                .first
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 6, "X" }, { 10, "0" } },
        "showcancelled exposes a negative pulse until its original event time");
    require(
        run_corrupt_pulse(
            ModulePathPulseStyle::ondetect, { 10 }, 2, 8, false, true)
                .first
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 4, "X" }, { 16, "1" } },
        "overlapping corrupt pulses replace recovery without stale events");

    const auto run_edge_polarity = [](
                                       const ModulePathEdge edge,
                                       const bool negative) {
        Interpreter filtered { { 1000, 64 } };
        const auto source = filtered.add_signal(
            { "edge.source", PackedLogic4::from_msb_string("0") });
        const auto destination = filtered.add_signal(
            { "edge.destination", PackedLogic4::from_msb_string("0") });
        Process edge_stimulus;
        edge_stimulus.id = 0;
        edge_stimulus.name = "edge-stimulus";
        edge_stimulus.register_count = 2;
        edge_stimulus.driver_regions = { { source, 0, 1, true } };
        edge_stimulus.operations = {
            LoadConstant { 0, PackedLogic4::from_msb_string("1") },
            WriteUpdate { source, 0 },
            WaitFor { 10 },
            LoadConstant { 1, PackedLogic4::from_msb_string("0") },
            WriteUpdate { source, 1 },
            WaitFor { 10 },
            Halt { }
        };
        (void)filtered.add_process(std::move(edge_stimulus));
        Process edge_driver;
        edge_driver.id = 1;
        edge_driver.name = "edge-driver";
        edge_driver.register_count = 1;
        edge_driver.static_sensitivity = { { source, EdgeKind::any } };
        edge_driver.driver_regions = { { destination, 0, 1, true } };
        edge_driver.operations = {
            ReadSignal { 0, source },
            WriteUpdate { destination, 0 },
            WaitSensitivity { },
            Jump { 0 }
        };
        (void)filtered.add_process(std::move(edge_driver));
        ModulePath path;
        path.identity = "runtime:edge-polarity-path:0";
        path.sources = { { source, 0, 1 } };
        path.destinations = { { destination, 0, 1 } };
        path.drivers = { 1 };
        path.delays = { 5 };
        path.source_edge = edge;
        if (negative) {
            path.polarity = ModulePathPolarity::negative;
            path.data_source.nodes.push_back(ModulePathExpressionNode {
                ModulePathExpressionOperator::terminal,
                { },
                PackedLogic4 { },
                { source, 0, 1 },
                BinaryOperator::bit_and,
                LogicalBinaryOperator::logical_and,
                ShiftOperator::logical_left,
                ReductionOperator::bit_and,
                1,
                false });
        }
        (void)filtered.add_module_path(std::move(path));
        std::vector<std::pair<SimulationTick, std::string>> filtered_changes;
        filtered.set_signal_change_hook(
            [&](const SignalId signal,
                const PackedLogic4& changed,
                const SimulationTick time) {
                if (signal == destination) {
                    filtered_changes.emplace_back(time, changed.to_msb_string());
                }
            });
        filtered.start();
        (void)filtered.run();
        return filtered_changes;
    };
    require(
        run_edge_polarity(ModulePathEdge::posedge, false)
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 5, "1" }, { 10, "0" } },
        "posedge module paths delay only matching source transitions");
    require(
        run_edge_polarity(ModulePathEdge::none, true)
            == std::vector<std::pair<SimulationTick, std::string>> { { 15, "1" } },
        "negative polarity transforms destination data before scheduling");
}

} // namespace fsim::tests::runtime
