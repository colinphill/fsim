// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::elaboration {
namespace {

    const frontend::Expression* specify_terminal_base(
        const frontend::Expression& expression)
    {
        if ((expression.kind == frontend::ExpressionKind::Index
                || expression.kind == frontend::ExpressionKind::Slice)
            && !expression.operands.empty()) {
            return specify_terminal_base(expression.operands.front());
        }
        return &expression;
    }

    std::optional<std::int64_t> static_integer(
        const frontend::Expression& expression,
        const elaboration_detail::ConstantEnvironment& environment)
    {
        std::string error;
        return elaboration_detail::evaluate_constant_expression(
            expression, environment, error);
    }

    std::optional<std::uint64_t> packed_index_offset(
        const std::int64_t index,
        const SignalInfo& signal)
    {
        if (!signal.packed_range) {
            return index == 0 && signal.width == 1
                ? std::optional<std::uint64_t> { 0 }
                : std::nullopt;
        }
        const auto& range = *signal.packed_range;
        const auto low = std::min(range.left, range.right);
        const auto high = std::max(range.left, range.right);
        if (index < low || index > high)
            return std::nullopt;
        return range.descending
            ? static_cast<std::uint64_t>(index)
                - static_cast<std::uint64_t>(range.right)
            : static_cast<std::uint64_t>(range.right)
                - static_cast<std::uint64_t>(index);
    }

    std::optional<VerilogSpecifyTerminalInfo> specify_terminal_selection(
        const frontend::Expression& expression,
        const runtime::simir::SignalId signal_id,
        const SignalInfo& signal,
        const elaboration_detail::ConstantEnvironment& environment)
    {
        if (signal.width == 0
            || signal.width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        if (expression.kind == frontend::ExpressionKind::Identifier) {
            return VerilogSpecifyTerminalInfo {
                signal_id, 0, static_cast<std::uint32_t>(signal.width)
            };
        }
        if (expression.kind == frontend::ExpressionKind::Index
            && expression.operands.size() == 2) {
            const auto index = static_integer(expression.operands[1], environment);
            const auto offset = index
                ? packed_index_offset(*index, signal)
                : std::nullopt;
            if (!offset
                || *offset > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            return VerilogSpecifyTerminalInfo {
                signal_id, static_cast<std::uint32_t>(*offset), 1
            };
        }
        if (expression.kind != frontend::ExpressionKind::Slice
            || expression.operands.size() != 3) {
            return std::nullopt;
        }
        const auto first = static_integer(expression.operands[1], environment);
        const auto second = static_integer(expression.operands[2], environment);
        if (!first || !second)
            return std::nullopt;
        std::int64_t left = *first;
        std::int64_t right = *second;
        if (expression.text == "+:" || expression.text == "-:") {
            if (*second <= 0)
                return std::nullopt;
            const auto width = static_cast<std::uint64_t>(*second);
            if (width - 1U
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            const auto delta = static_cast<std::int64_t>(width - 1U);
            if ((expression.text == "+:"
                    && left > std::numeric_limits<std::int64_t>::max() - delta)
                || (expression.text == "-:"
                    && left < std::numeric_limits<std::int64_t>::min() + delta)) {
                return std::nullopt;
            }
            right = expression.text == "+:" ? left + delta : left - delta;
        } else if (expression.text != ":") {
            return std::nullopt;
        }
        const auto left_offset = packed_index_offset(left, signal);
        const auto right_offset = packed_index_offset(right, signal);
        if (!left_offset || !right_offset)
            return std::nullopt;
        const auto low_offset = std::min(*left_offset, *right_offset);
        const auto high_offset = std::max(*left_offset, *right_offset);
        const auto width = high_offset - low_offset + 1U;
        if (low_offset > std::numeric_limits<std::uint32_t>::max()
            || width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return VerilogSpecifyTerminalInfo {
            signal_id,
            static_cast<std::uint32_t>(low_offset),
            static_cast<std::uint32_t>(width)
        };
    }

} // namespace

std::optional<VerilogSpecifyTerminalInfo>
elaboration_detail::resolve_verilog_specify_selection(
    const frontend::Expression& expression,
    const runtime::simir::SignalId signal,
    const SignalInfo& info,
    const elaboration_detail::ConstantEnvironment& environment)
{
    return specify_terminal_selection(expression, signal, info, environment);
}

void HierarchyBuilder::validate_verilog_specify(
    const DesignUnit& unit,
    const std::string& path,
    const SignalMap& signals,
    const ConstantEnvironment& parameter_environment)
{
    if (unit.verilog_specify_blocks.empty())
        return;

    const auto declaration = [&](const std::string_view name)
        -> const frontend::SignalDeclaration* {
        const auto port = std::ranges::find(
            unit.ports, name, &frontend::SignalDeclaration::name);
        if (port != unit.ports.end())
            return &*port;
        const auto signal = std::ranges::find(
            unit.signals, name, &frontend::SignalDeclaration::name);
        return signal == unit.signals.end() ? nullptr : &*signal;
    };
    const auto resolve = [&](const frontend::Expression& expression,
                             const std::string_view role,
                             const bool require_input,
                             const bool require_output,
                             const bool require_port = false)
        -> std::optional<VerilogSpecifyTerminalInfo> {
        const auto* base = specify_terminal_base(expression);
        if (base->kind != frontend::ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVSPEC-002",
                "specify " + std::string { role }
                    + " at '" + path
                    + "' is not a static module terminal",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals.find(base->text);
        const auto* declared = declaration(base->text);
        if (signal == signals.end() || declared == nullptr) {
            report(
                "FSIM-ELAB-SVSPEC-002",
                "specify " + std::string { role } + " '" + base->text
                    + "' was not found in module instance '" + path + "'",
                expression.span);
            return std::nullopt;
        }
        const bool input = declared->direction == frontend::PortDirection::Input
            || declared->direction == frontend::PortDirection::Inout;
        const bool output = declared->direction == frontend::PortDirection::Output
            || declared->direction == frontend::PortDirection::Inout;
        const bool port = input || output;
        if ((require_input && !input) || (require_output && !output)
            || (require_port && !port)) {
            report(
                "FSIM-ELAB-SVSPEC-003",
                "specify " + std::string { role } + " '" + base->text
                    + "' has an incompatible module-port direction at '"
                    + path + "'",
                expression.span);
            return std::nullopt;
        }
        const auto selection = specify_terminal_selection(
            expression,
            signal->second,
            design_.signal_info_.at(signal->second),
            parameter_environment);
        if (!selection) {
            report(
                "FSIM-ELAB-SVSPEC-004",
                "specify " + std::string { role } + " '" + base->text
                    + "' does not have a static nonzero packed width",
                expression.span);
        }
        return selection;
    };
    const auto simulation_time = [&](
                                     const frontend::Delay& delay,
                                     const frontend::SourceSpan& source,
                                     const std::string_view code,
                                     const std::string_view role)
        -> std::optional<runtime::SimulationTick> {
        std::uint64_t ticks = delay.magnitude;
        if (delay.expression) {
            std::string error;
            const auto value = evaluate_systemverilog_constant_expression(
                *delay.expression, { }, parameter_environment, error);
            const auto integer = value ? value->integer_value() : std::nullopt;
            if (!integer || *integer < 0 || delay.divisor == 0
                || static_cast<std::uint64_t>(*integer)
                    > std::numeric_limits<std::uint64_t>::max()
                        / delay.magnitude) {
                report(
                    std::string { code },
                    std::string { role } + " at '" + path
                        + "' is not a static nonnegative simulation time",
                    source);
                return std::nullopt;
            }
            ticks = static_cast<std::uint64_t>(*integer) * delay.magnitude;
        }
        if (delay.divisor != 1 || !delay.unit.empty()) {
            report(
                std::string { code },
                std::string { role } + " at '" + path
                    + "' was not normalized to project ticks",
                source);
            return std::nullopt;
        }
        return ticks;
    };
    const auto pulse_limit = [&](
                                 const frontend::Delay& delay,
                                 const frontend::SourceSpan& source) {
        return simulation_time(
            delay, source, "FSIM-ELAB-SVSPEC-011", "PATHPULSE limit");
    };
    const auto timing_limit = [&](
                                  const frontend::Delay& delay,
                                  const frontend::SourceSpan& source)
        -> std::optional<std::int64_t> {
        if (delay.divisor != 1 || !delay.unit.empty()) {
            report(
                "FSIM-ELAB-SVSPEC-016",
                "timing-check limit at '" + path
                    + "' was not normalized to project ticks",
                source);
            return std::nullopt;
        }
        if (!delay.expression) {
            if (delay.magnitude
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                report(
                    "FSIM-ELAB-SVSPEC-016",
                    "timing-check limit at '" + path
                        + "' overflows signed simulation time",
                    source);
                return std::nullopt;
            }
            return static_cast<std::int64_t>(delay.magnitude);
        }
        std::string error;
        const auto value = evaluate_systemverilog_constant_expression(
            *delay.expression, { }, parameter_environment, error);
        const auto integer = value ? value->integer_value() : std::nullopt;
        const auto magnitude = static_cast<std::int64_t>(
            std::min<std::uint64_t>(
                delay.magnitude,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())));
        const bool overflow = !integer
            || (magnitude != 0 && *integer > 0
                && *integer
                    > std::numeric_limits<std::int64_t>::max() / magnitude)
            || (magnitude != 0 && *integer < 0
                && *integer
                    < std::numeric_limits<std::int64_t>::min() / magnitude);
        if (overflow
            || delay.magnitude
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            report(
                "FSIM-ELAB-SVSPEC-016",
                "timing-check limit at '" + path
                    + "' is not a static representable simulation time",
                source);
            return std::nullopt;
        }
        return *integer * magnitude;
    };

    std::size_t path_ordinal { };
    std::size_t timing_check_ordinal { };
    for (const auto& block : unit.verilog_specify_blocks) {
        const auto block_path_begin = design_.verilog_specify_paths_.size();
        for (const auto& module_path : block.module_paths) {
            VerilogSpecifyPathInfo normalized;
            normalized.id = static_cast<VerilogSpecifyPathId>(
                design_.verilog_specify_paths_.size());
            if (static_cast<std::size_t>(normalized.id)
                != design_.verilog_specify_paths_.size()) {
                throw std::length_error { "too many Verilog specify paths" };
            }
            normalized.identity = "sdf:iopath:" + path + ":"
                + std::to_string(path_ordinal++);
            normalized.instance = path;
            normalized.kind = module_path.kind;
            normalized.source_edge = module_path.source_edge;
            normalized.polarity = module_path.polarity;
            normalized.condition = module_path.condition;
            normalized.destination_data_source = module_path.destination_data_source;
            normalized.conditional = module_path.conditional;
            normalized.ifnone = module_path.ifnone;
            normalized.source = module_path.span;
            bool valid_programs = true;
            if (module_path.conditional) {
                const auto program = compile_verilog_specify_expression(
                    module_path.condition,
                    signals,
                    parameter_environment,
                    "path condition");
                if (program) {
                    normalized.condition_program = *program;
                } else {
                    valid_programs = false;
                }
            }
            if (module_path.destination_data_source.valid()) {
                const auto program = compile_verilog_specify_expression(
                    module_path.destination_data_source,
                    signals,
                    parameter_environment,
                    "destination data source");
                if (program) {
                    normalized.data_source_program = *program;
                } else {
                    valid_programs = false;
                }
            }
            for (const auto& source : module_path.sources) {
                if (const auto selection = resolve(
                        source, "path source", true, false)) {
                    normalized.sources.push_back(*selection);
                }
            }
            for (const auto& destination : module_path.destinations) {
                if (const auto selection = resolve(
                        destination, "path destination", false, true)) {
                    normalized.destinations.push_back(*selection);
                }
            }
            if (!normalized.data_source_program.empty()) {
                const auto width = normalized.data_source_program.nodes.at(
                                                                           normalized.data_source_program.root)
                                       .width;
                if (std::ranges::any_of(
                        normalized.destinations,
                        [&](const VerilogSpecifyTerminalInfo& destination) {
                            return width != 1 && width != destination.width;
                        })) {
                    report(
                        "FSIM-ELAB-SVSPEC-009",
                        "specify destination data source at '" + path
                            + "' must be scalar or match every destination width",
                        module_path.destination_data_source.span);
                    valid_programs = false;
                }
            }
            if (module_path.source_edge != frontend::VerilogSpecifyEdge::None
                && std::ranges::any_of(
                    normalized.sources,
                    [](const VerilogSpecifyTerminalInfo& source) {
                        return source.width != 1;
                    })) {
                report(
                    "FSIM-ELAB-SVSPEC-010",
                    "edge-sensitive specify path at '" + path
                        + "' requires scalar source terminals",
                    module_path.span);
                valid_programs = false;
            }
            if (module_path.kind
                    == frontend::VerilogModulePathKind::Parallel
                && (normalized.sources.size() != module_path.sources.size()
                    || normalized.destinations.size()
                        != module_path.destinations.size()
                    || !std::ranges::equal(
                        normalized.sources,
                        normalized.destinations,
                        [](const auto& left, const auto& right) {
                            return left.width == right.width;
                        }))) {
                report(
                    "FSIM-ELAB-SVSPEC-005",
                    "parallel specify path at '" + path
                        + "' requires pairwise equal source and destination "
                          "terminal widths",
                    module_path.span);
            }
            const bool valid_delay_count = module_path.delays.size() == 1
                || module_path.delays.size() == 2
                || module_path.delays.size() == 3
                || module_path.delays.size() == 6
                || module_path.delays.size() == 12;
            if (!valid_delay_count) {
                report(
                    "FSIM-ELAB-SVSPEC-006",
                    "specify path at '" + path
                        + "' requires 1, 2, 3, 6, or 12 transition delays",
                    module_path.span);
            }
            for (const auto& delay : module_path.delays) {
                std::uint64_t ticks = delay.magnitude;
                if (delay.expression) {
                    std::string error;
                    const auto value = evaluate_systemverilog_constant_expression(
                        *delay.expression,
                        { },
                        parameter_environment,
                        error);
                    const auto integer = value ? value->integer_value() : std::nullopt;
                    if (!integer || *integer < 0
                        || delay.divisor == 0
                        || static_cast<std::uint64_t>(*integer)
                            > std::numeric_limits<std::uint64_t>::max()
                                / delay.magnitude) {
                        report(
                            "FSIM-ELAB-SVSPEC-007",
                            "specify path delay at '" + path
                                + "' is not a static nonnegative simulation time",
                            delay.span);
                        continue;
                    }
                    ticks = static_cast<std::uint64_t>(*integer)
                        * delay.magnitude;
                }
                if (delay.divisor != 1) {
                    report(
                        "FSIM-ELAB-SVSPEC-007",
                        "specify path delay at '" + path
                            + "' was not normalized to project ticks",
                        delay.span);
                    continue;
                }
                normalized.delays.push_back(ticks);
            }
            if (normalized.sources.size() == module_path.sources.size()
                && normalized.destinations.size()
                    == module_path.destinations.size()
                && valid_delay_count
                && valid_programs
                && normalized.delays.size() == module_path.delays.size()
                && (module_path.kind != frontend::VerilogModulePathKind::Parallel
                    || std::ranges::equal(
                        normalized.sources,
                        normalized.destinations,
                        [](const auto& left, const auto& right) {
                            return left.width == right.width;
                        }))) {
                normalized.selection_group = normalized.id;
                if (module_path.conditional || module_path.ifnone) {
                    const auto prior = std::ranges::find_if(
                        std::span {
                            design_.verilog_specify_paths_ }
                            .subspan(
                                block_path_begin),
                        [&](const VerilogSpecifyPathInfo& candidate) {
                            return (candidate.conditional || candidate.ifnone)
                                && candidate.kind == normalized.kind
                                && candidate.source_edge == normalized.source_edge
                                && candidate.sources == normalized.sources
                                && candidate.destinations == normalized.destinations;
                        });
                    if (prior != std::span { design_.verilog_specify_paths_ }.subspan(block_path_begin).end()) {
                        normalized.selection_group = prior->selection_group;
                    }
                }
                design_.verilog_specify_paths_.push_back(std::move(normalized));
            }
        }
        for (const auto& pulse : block.pulse_declarations) {
            std::vector<VerilogSpecifyTerminalInfo> terminals;
            for (const auto& terminal : pulse.terminals) {
                if (const auto selected = resolve(terminal, "pulse terminal", false, true)) {
                    terminals.push_back(*selected);
                }
            }
            const auto overlaps = [](
                                      const VerilogSpecifyTerminalInfo& left,
                                      const VerilogSpecifyTerminalInfo& right) {
                if (left.signal != right.signal)
                    return false;
                const auto left_end = static_cast<std::uint64_t>(left.offset) + left.width;
                const auto right_end = static_cast<std::uint64_t>(right.offset) + right.width;
                return left.offset < right_end && right.offset < left_end;
            };
            for (auto& path_info : std::span {
                     design_.verilog_specify_paths_ }
                     .subspan(block_path_begin)) {
                if (!std::ranges::any_of(
                        path_info.destinations,
                        [&](const VerilogSpecifyTerminalInfo& destination) {
                            return std::ranges::any_of(
                                terminals,
                                [&](const VerilogSpecifyTerminalInfo& terminal) {
                                    return overlaps(destination, terminal);
                                });
                        })) {
                    continue;
                }
                if (pulse.controls_style) {
                    path_info.pulse_style = pulse.style;
                } else {
                    path_info.show_cancelled = pulse.show_cancelled;
                }
            }
        }
        for (std::size_t specificity = 0; specificity <= 2; ++specificity) {
            for (const auto& specparam : block.specparams) {
                if (!specparam.path_pulse
                    || static_cast<std::size_t>(
                           !specparam.path_pulse_input.empty())
                            + static_cast<std::size_t>(
                                !specparam.path_pulse_output.empty())
                        != specificity
                    || !specparam.path_pulse_reject_delay) {
                    continue;
                }
                const auto reject = pulse_limit(
                    *specparam.path_pulse_reject_delay, specparam.span);
                const auto error = specparam.path_pulse_error_delay
                    ? pulse_limit(*specparam.path_pulse_error_delay, specparam.span)
                    : reject;
                if (!reject || !error)
                    continue;
                if (*reject > *error) {
                    report(
                        "FSIM-ELAB-SVSPEC-012",
                        "PATHPULSE rejection limit at '" + path
                            + "' cannot exceed its error limit",
                        specparam.span);
                    continue;
                }
                const auto input = specparam.path_pulse_input.empty()
                    ? signals.end()
                    : signals.find(specparam.path_pulse_input);
                const auto output = specparam.path_pulse_output.empty()
                    ? signals.end()
                    : signals.find(specparam.path_pulse_output);
                if ((!specparam.path_pulse_input.empty() && input == signals.end())
                    || (!specparam.path_pulse_output.empty()
                        && output == signals.end())) {
                    report(
                        "FSIM-ELAB-SVSPEC-013",
                        "PATHPULSE terminal selector at '" + path
                            + "' does not name module signals",
                        specparam.span);
                    continue;
                }
                for (auto& path_info : std::span {
                         design_.verilog_specify_paths_ }
                         .subspan(block_path_begin)) {
                    if (input != signals.end()
                        && std::ranges::none_of(
                            path_info.sources,
                            [&](const VerilogSpecifyTerminalInfo& terminal) {
                                return terminal.signal == input->second;
                            })) {
                        continue;
                    }
                    if (output != signals.end()
                        && std::ranges::none_of(
                            path_info.destinations,
                            [&](const VerilogSpecifyTerminalInfo& terminal) {
                                return terminal.signal == output->second;
                            })) {
                        continue;
                    }
                    path_info.pulse_reject_limit = *reject;
                    path_info.pulse_error_limit = *error;
                }
            }
        }
        for (const auto& check : block.timing_checks) {
            runtime::simir::ModuleTimingCheck normalized;
            normalized.id = static_cast<std::uint32_t>(
                design_.verilog_timing_checks_.size());
            if (static_cast<std::size_t>(normalized.id)
                != design_.verilog_timing_checks_.size()) {
                throw std::length_error { "too many Verilog timing checks" };
            }
            normalized.identity = "sdf:timingcheck:" + path + ":"
                + std::to_string(timing_check_ordinal++);
            const auto runtime_edge = [](const frontend::VerilogSpecifyEdge edge) {
                switch (edge) {
                case frontend::VerilogSpecifyEdge::None:
                    return runtime::simir::ModulePathEdge::none;
                case frontend::VerilogSpecifyEdge::Posedge:
                    return runtime::simir::ModulePathEdge::posedge;
                case frontend::VerilogSpecifyEdge::Negedge:
                    return runtime::simir::ModulePathEdge::negedge;
                case frontend::VerilogSpecifyEdge::Edge:
                    return runtime::simir::ModulePathEdge::edge;
                }
                return runtime::simir::ModulePathEdge::none;
            };
            const auto normalize_event = [&](
                                             const frontend::VerilogTimingCheckEvent& event,
                                             const std::string_view role)
                -> std::optional<runtime::simir::ModuleTimingEvent> {
                const auto terminal = resolve(
                    event.expression, role, false, false, true);
                if (!terminal || terminal->width != 1) {
                    if (terminal) {
                        report(
                            "FSIM-ELAB-SVSPEC-014",
                            "specify " + std::string { role } + " at '" + path
                                + "' must be scalar",
                            event.span);
                    }
                    return std::nullopt;
                }
                runtime::simir::ModuleTimingEvent result;
                result.terminal = {
                    terminal->signal, terminal->offset, terminal->width
                };
                result.edge = runtime_edge(event.edge);
                result.edge_descriptors = event.edge_descriptors;
                if (event.edge == frontend::VerilogSpecifyEdge::Edge) {
                    static constexpr std::array valid_descriptors {
                        std::string_view { "01" }, std::string_view { "10" },
                        std::string_view { "0x" }, std::string_view { "x1" },
                        std::string_view { "1x" }, std::string_view { "x0" },
                        std::string_view { "0z" }, std::string_view { "z1" },
                        std::string_view { "1z" }, std::string_view { "z0" },
                        std::string_view { "xz" }, std::string_view { "zx" }
                    };
                    const bool descriptors_valid = !result.edge_descriptors.empty()
                        && std::ranges::all_of(
                            result.edge_descriptors,
                            [&](std::string descriptor) {
                                std::ranges::transform(
                                    descriptor,
                                    descriptor.begin(),
                                    [](const unsigned char character) {
                                        return static_cast<char>(
                                            std::tolower(character));
                                    });
                                return std::ranges::find(
                                           valid_descriptors, descriptor)
                                    != valid_descriptors.end();
                            });
                    if (!descriptors_valid) {
                        report(
                            "FSIM-ELAB-SVSPEC-019",
                            "timing-check edge descriptor at '" + path
                                + "' is not an IEEE 1364 scalar transition",
                            event.span);
                        return std::nullopt;
                    }
                }
                if (event.condition.valid()) {
                    const auto condition = compile_verilog_specify_expression(
                        event.condition, signals, parameter_environment,
                        "timing-event condition");
                    if (!condition)
                        return std::nullopt;
                    result.condition = *condition;
                }
                return result;
            };
            const auto reference = normalize_event(
                check.reference_event, "timing-check reference event");
            const auto data = check.data_event.expression.valid()
                ? normalize_event(
                      check.data_event, "timing-check data event")
                : std::optional<runtime::simir::ModuleTimingEvent> { };
            if (!reference
                || (check.data_event.expression.valid() && !data)) {
                continue;
            }
            normalized.reference = *reference;
            normalized.data = data;
            const auto kind = [](const frontend::VerilogTimingCheckKind value) {
                using Frontend = frontend::VerilogTimingCheckKind;
                using Runtime = runtime::simir::ModuleTimingCheckKind;
                switch (value) {
                case Frontend::Setup:
                    return Runtime::setup;
                case Frontend::Hold:
                    return Runtime::hold;
                case Frontend::SetupHold:
                    return Runtime::setuphold;
                case Frontend::Recovery:
                    return Runtime::recovery;
                case Frontend::Removal:
                    return Runtime::removal;
                case Frontend::RecRem:
                    return Runtime::recrem;
                case Frontend::Skew:
                    return Runtime::skew;
                case Frontend::TimeSkew:
                    return Runtime::timeskew;
                case Frontend::FullSkew:
                    return Runtime::fullskew;
                case Frontend::Period:
                    return Runtime::period;
                case Frontend::Width:
                    return Runtime::width;
                case Frontend::NoChange:
                    return Runtime::nochange;
                }
                return Runtime::setup;
            };
            normalized.kind = kind(check.kind);
            bool valid = check.normalized_limits.size() == check.limits.size();
            const bool controlled_reference = check.reference_event.edge
                != frontend::VerilogSpecifyEdge::None;
            if ((normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::period
                    || normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::width)
                && !controlled_reference) {
                report(
                    "FSIM-ELAB-SVSPEC-019",
                    "controlled timing check at '" + path
                        + "' requires an edge-qualified reference event",
                    check.reference_event.span);
                valid = false;
            }
            for (const auto& limit : check.normalized_limits) {
                const auto value = timing_limit(limit, check.span);
                if (!value) {
                    valid = false;
                } else {
                    normalized.limits.push_back(*value);
                }
            }
            const bool signed_limits = normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::setuphold
                || normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::recrem
                || normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::nochange;
            if (!signed_limits
                && std::ranges::any_of(
                    normalized.limits,
                    [](const std::int64_t limit) { return limit < 0; })) {
                report(
                    "FSIM-ELAB-SVSPEC-016",
                    "timing-check limit at '" + path
                        + "' must be nonnegative for this check",
                    check.span);
                valid = false;
            }
            if ((normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::setuphold
                    || normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::recrem)
                && normalized.limits.size() == 2) {
                const auto first = normalized.limits[0];
                const auto second = normalized.limits[1];
                const bool overflow = (second > 0
                                          && first > std::numeric_limits<std::int64_t>::max() - second)
                    || (second < 0
                        && first
                            < std::numeric_limits<std::int64_t>::min() - second);
                if (overflow || first + second <= 0) {
                    report(
                        "FSIM-ELAB-SVSPEC-016",
                        "combined timing-check limits at '" + path
                            + "' must have a positive representable sum",
                        check.span);
                    valid = false;
                }
            }
            if (normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::nochange
                && normalized.limits.size() == 2
                && normalized.limits[0] > normalized.limits[1]) {
                report(
                    "FSIM-ELAB-SVSPEC-016",
                    "$nochange offsets at '" + path
                        + "' are not in ascending order",
                    check.span);
                valid = false;
            }
            if (check.normalized_threshold) {
                const auto threshold = timing_limit(
                    *check.normalized_threshold, check.threshold.span);
                if (!threshold || *threshold < 0) {
                    valid = false;
                } else {
                    normalized.threshold = static_cast<runtime::SimulationTick>(*threshold);
                }
            }
            const auto optional_signal = [&](const frontend::Expression& expression)
                -> std::optional<runtime::simir::SignalId> {
                if (!expression.valid()
                    || expression.kind != frontend::ExpressionKind::Identifier) {
                    return std::nullopt;
                }
                const auto found = signals.find(expression.text);
                return found == signals.end()
                        || design_.signal_info_.at(found->second).width != 1
                    ? std::nullopt
                    : std::optional<runtime::simir::SignalId> { found->second };
            };
            if (check.notifier.valid()) {
                normalized.notifier = optional_signal(check.notifier);
                const auto* notifier_declaration =
                    check.notifier.kind == frontend::ExpressionKind::Identifier
                    ? declaration(check.notifier.text)
                    : nullptr;
                if (!normalized.notifier || notifier_declaration == nullptr
                    || !notifier_declaration->type.systemverilog_net_type.empty()) {
                    report(
                        "FSIM-ELAB-SVSPEC-015",
                        "timing-check notifier at '" + path
                            + "' is not a scalar variable",
                        check.notifier.span);
                    valid = false;
                }
            }
            const auto optional_condition = [&](
                                                const frontend::Expression& expression,
                                                const std::string_view role)
                -> std::optional<runtime::simir::ModulePathExpression> {
                if (!expression.valid()) {
                    return runtime::simir::ModulePathExpression { };
                }
                return compile_verilog_specify_expression(
                    expression, signals, parameter_environment, role);
            };
            if (check.timestamp_condition.valid()) {
                const auto condition = optional_condition(
                    check.timestamp_condition, "timestamp condition");
                if (!condition) {
                    valid = false;
                } else {
                    normalized.timestamp_condition = *condition;
                }
            }
            if (check.timecheck_condition.valid()) {
                const auto condition = optional_condition(
                    check.timecheck_condition, "timecheck condition");
                if (!condition) {
                    valid = false;
                } else {
                    normalized.timecheck_condition = *condition;
                }
            }
            const auto delayed_terminal = [&](
                                              const frontend::Expression& expression,
                                              const std::string_view role)
                -> std::optional<runtime::simir::ModulePathTerminal> {
                const auto terminal = resolve(expression, role, false, false);
                if (!terminal || terminal->width != 1) {
                    if (terminal) {
                        report(
                            "FSIM-ELAB-SVSPEC-017",
                            "specify " + std::string { role } + " at '" + path
                                + "' must be scalar",
                            expression.span);
                    }
                    return std::nullopt;
                }
                return runtime::simir::ModulePathTerminal {
                    terminal->signal, terminal->offset, terminal->width
                };
            };
            if (check.delayed_reference.valid()) {
                normalized.delayed_reference = delayed_terminal(
                    check.delayed_reference, "delayed-reference signal");
                valid = valid && normalized.delayed_reference.has_value();
            }
            if (check.delayed_data.valid()) {
                normalized.delayed_data = delayed_terminal(
                    check.delayed_data, "delayed-data signal");
                valid = valid && normalized.delayed_data.has_value();
            }
            const auto static_flag = [&](
                                         const frontend::Expression& expression,
                                         const std::string_view role,
                                         bool& destination) {
                if (!expression.valid())
                    return;
                std::string error;
                const auto value = evaluate_systemverilog_constant_expression(
                    expression, { }, parameter_environment, error);
                const auto integer = value ? value->integer_value() : std::nullopt;
                if (!integer) {
                    report(
                        "FSIM-ELAB-SVSPEC-018",
                        "timing-check " + std::string { role } + " at '" + path
                            + "' is not a static two-state value",
                        expression.span);
                    valid = false;
                    return;
                }
                destination = *integer != 0;
            };
            static_flag(
                check.event_based_flag,
                "event-based flag",
                normalized.event_based);
            static_flag(
                check.remain_active_flag,
                "remain-active flag",
                normalized.remain_active);
            normalized.source = runtime::simir::SourceLocation {
                check.span.source_name.str(),
                static_cast<std::uint32_t>(check.span.begin.line),
                static_cast<std::uint32_t>(check.span.begin.column)
            };
            if (valid) {
                design_.verilog_timing_checks_.push_back(std::move(normalized));
            }
        }
    }
}

} // namespace fsim::elaboration
