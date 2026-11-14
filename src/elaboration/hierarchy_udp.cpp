// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_driver_regions.hpp"
#include "lowerer_internal.hpp"
#include "fsim/support/sha256.hpp"

#include <iostream>
#include <array>

namespace fsim::elaboration {
namespace {

frontend::SourceSpan udp_source(
    const semantic::sv::UdpTableRow& row,
    const semantic::CompiledDesign* compiled)
{
    return compiled != nullptr
        ? compiled_source_span(*compiled, row.source)
        : frontend::SourceSpan { };
}

frontend::SourceSpan udp_source(
    const semantic::sv::UdpInputPattern& pattern,
    const semantic::CompiledDesign* compiled)
{
    return compiled != nullptr
        ? compiled_source_span(*compiled, pattern.source)
        : frontend::SourceSpan { };
}
frontend::VerilogUdpLevelSymbol udp_level(
    const semantic::sv::UdpLevel value)
{
    using Input = semantic::sv::UdpLevel;
    using Output = frontend::VerilogUdpLevelSymbol;
    switch (value) {
    case Input::zero:
        return Output::Zero;
    case Input::one:
        return Output::One;
    case Input::unknown:
        return Output::Unknown;
    case Input::dont_care:
        return Output::DontCare;
    case Input::binary:
        return Output::Binary;
    }
    return Output::Unknown;
}

frontend::VerilogUdpEdgeSymbol udp_edge(
    const semantic::sv::UdpEdge value)
{
    using Input = semantic::sv::UdpEdge;
    using Output = frontend::VerilogUdpEdgeSymbol;
    switch (value) {
    case Input::none:
        return Output::None;
    case Input::rising:
        return Output::Rising;
    case Input::falling:
        return Output::Falling;
    case Input::positive:
        return Output::Positive;
    case Input::negative:
        return Output::Negative;
    case Input::any:
        return Output::Any;
    case Input::explicit_edge:
        return Output::Explicit;
    }
    return Output::None;
}

frontend::VerilogUdpOutputSymbol udp_output(
    const semantic::sv::UdpOutput value)
{
    using Input = semantic::sv::UdpOutput;
    using Output = frontend::VerilogUdpOutputSymbol;
    switch (value) {
    case Input::zero:
        return Output::Zero;
    case Input::one:
        return Output::One;
    case Input::unknown:
        return Output::Unknown;
    case Input::no_change:
        return Output::NoChange;
    }
    return Output::Unknown;
}
frontend::Language udp_language(
    const semantic::sv::UdpDeclaration& declaration)
{
    return declaration.language == semantic::Language::verilog
        ? frontend::Language::Verilog2005
        : frontend::Language::SystemVerilog2017;
}

void digest_field(
    support::Sha256& digest, const std::string_view value) {
    digest.update(std::to_string(value.size()));
    digest.update(":");
    digest.update(value);
    digest.update(";");
}

template <typename Value>
void digest_number(support::Sha256& digest, const Value value) {
    digest_field(
        digest,
        std::to_string(static_cast<std::uint64_t>(value)));
}

template <typename InputPattern>
frontend::VerilogUdpInputPattern compatible_udp_pattern(
    const InputPattern& pattern,
    const semantic::CompiledDesign* compiled)
{
    return {
        udp_level(pattern.level),
        udp_edge(pattern.edge),
        udp_level(pattern.previous),
        udp_level(pattern.current),
        udp_source(pattern, compiled)
    };
}

template <typename TableRow>
frontend::VerilogUdpTableRow compatible_udp_row(
    const TableRow& row, const semantic::CompiledDesign* compiled)
{
    frontend::VerilogUdpTableRow result;
    result.inputs.reserve(row.inputs.size());
    for (const auto& input : row.inputs) {
        result.inputs.push_back(compatible_udp_pattern(input, compiled));
    }
    if (row.current_state) {
        result.current_state = udp_level(*row.current_state);
    }
    result.output = udp_output(row.output);
    result.span = udp_source(row, compiled);
    return result;
}

char udp_level_character(const frontend::VerilogUdpLevelSymbol level)
{
    using Level = frontend::VerilogUdpLevelSymbol;
    switch (level) {
    case Level::Zero:
        return '0';
    case Level::One:
        return '1';
    case Level::Unknown:
        return 'x';
    case Level::DontCare:
        return '?';
    case Level::Binary:
        return 'b';
    }
    return 'x';
}

// A table entry as written: `inputs : output` for a combinational UDP and
// `inputs : state : output` for a sequential one (IEEE 1800-2023 29.3).
std::string udp_row_text(const frontend::VerilogUdpTableRow& row)
{
    using Edge = frontend::VerilogUdpEdgeSymbol;
    std::string text;
    for (const auto& input : row.inputs) {
        if (!text.empty()) {
            text += ' ';
        }
        switch (input.edge) {
        case Edge::None:
            text += udp_level_character(input.level);
            break;
        case Edge::Rising:
            text += 'r';
            break;
        case Edge::Falling:
            text += 'f';
            break;
        case Edge::Positive:
            text += 'p';
            break;
        case Edge::Negative:
            text += 'n';
            break;
        case Edge::Any:
            text += '*';
            break;
        case Edge::Explicit:
            text += '(';
            text += udp_level_character(input.previous);
            text += udp_level_character(input.current);
            text += ')';
            break;
        }
    }
    text += " : ";
    if (row.current_state) {
        text += udp_level_character(*row.current_state);
        text += " : ";
    }
    switch (row.output) {
    case frontend::VerilogUdpOutputSymbol::Zero:
        text += '0';
        break;
    case frontend::VerilogUdpOutputSymbol::One:
        text += '1';
        break;
    case frontend::VerilogUdpOutputSymbol::Unknown:
        text += 'x';
        break;
    case frontend::VerilogUdpOutputSymbol::NoChange:
        text += '-';
        break;
    }
    return text;
}

template <typename Declaration>
UdpTableInfo make_udp_table(
    const Declaration& declaration,
    const semantic::CompiledDesign* compiled)
{
    const auto library = declaration.library.empty()
        ? std::string { "work" }
        : declaration.library;
    UdpTableInfo table;
    table.identity = "udp:" + library + "." + declaration.name;
    table.sequential = declaration.sequential;
    if (declaration.initial_output) {
        table.initial_output = udp_output(*declaration.initial_output);
    }
    table.terminals.reserve(declaration.inputs.size() + 1);
    table.terminals.push_back(declaration.output);
    table.terminals.insert(table.terminals.end(),
        declaration.inputs.begin(), declaration.inputs.end());
    table.rows.reserve(declaration.rows.size());
    for (const auto& row : declaration.rows) {
        table.rows.push_back(compatible_udp_row(row, compiled));
        table.row_text.push_back(udp_row_text(table.rows.back()));
    }

    support::Sha256 digest;
    digest_field(digest, "fsim-udp-table-v1");
    digest_field(digest, table.identity);
    digest_number(digest, table.sequential);
    digest_field(digest, declaration.output);
    digest_number(digest,
        table.initial_output
            ? static_cast<unsigned>(*table.initial_output) + 1U
            : 0U);
    for (const auto& input : declaration.inputs) {
        digest_field(digest, input);
    }
    for (const auto& row : table.rows) {
        digest_number(digest,
            row.current_state
                ? static_cast<unsigned>(*row.current_state) + 1U
                : 0U);
        for (const auto& input : row.inputs) {
            digest_number(digest, input.level);
            digest_number(digest, input.edge);
            digest_number(digest, input.previous);
            digest_number(digest, input.current);
        }
        digest_number(digest, row.output);
    }
    table.digest = support::Sha256::hex(digest.finish());
    return table;
}

runtime::simir::StrengthRank udp_strength_rank(
    const std::uint8_t encoded)
{
    using Frontend = frontend::VerilogStrength;
    using Runtime = runtime::simir::StrengthRank;
    switch (static_cast<Frontend>(encoded)) {
    case Frontend::HighZ:
        return Runtime::highz;
    case Frontend::Small:
        return Runtime::small;
    case Frontend::Medium:
        return Runtime::medium;
    case Frontend::Weak:
        return Runtime::weak;
    case Frontend::Large:
        return Runtime::large;
    case Frontend::Pull:
        return Runtime::pull;
    case Frontend::Strong:
        return Runtime::strong;
    case Frontend::Supply:
        return Runtime::supply;
    }
    return Runtime::strong;
}

std::optional<runtime::SimulationTick> udp_delay_value(
    const semantic::sv::DelayValue& delay,
    const semantic::SpecializedHirUnit& specialization)
{
    if (delay.divisor != 1U || !delay.unit.empty()) {
        return std::nullopt;
    }
    auto result = delay.magnitude;
    if (!delay.expression) {
        return result;
    }
    const auto value = specialization.evaluate_integral_expression(
        *delay.expression);
    if (!value || *value < 0) {
        return std::nullopt;
    }
    const auto multiplier = static_cast<std::uint64_t>(*value);
    if (multiplier != 0U
        && result > std::numeric_limits<std::uint64_t>::max()
                / multiplier) {
        return std::nullopt;
    }
    return result * multiplier;
}

std::optional<runtime::simir::TransitionDelays> udp_transition_delays(
    const semantic::sv::Delay& delay,
    const semantic::SpecializedHirUnit& specialization)
{
    const auto rise = udp_delay_value(delay.primary, specialization);
    if (!rise) {
        return std::nullopt;
    }
    auto fall = *rise;
    auto turnoff = *rise;
    if (!delay.additional.empty()) {
        const auto value = udp_delay_value(
            delay.additional.front().primary, specialization);
        if (!value) {
            return std::nullopt;
        }
        fall = *value;
        turnoff = std::min(*rise, fall);
    }
    if (delay.additional.size() > 1U) {
        const auto value = udp_delay_value(
            delay.additional[1].primary, specialization);
        if (!value) {
            return std::nullopt;
        }
        turnoff = *value;
    }
    return runtime::simir::TransitionDelays { *rise, fall, turnoff };
}

// The rise, fall and turn-off delays of a primitive with each of their
// minimum, typical and maximum values (IEEE 1800-2023 28.3.3, 37.35). Empty
// when a value is not a static nonnegative simulation time.
std::vector<VerilogPrimitiveDelayInfo> primitive_delay_info(
    const semantic::sv::Delay& delay,
    const semantic::SpecializedHirUnit& specialization)
{
    std::vector<VerilogPrimitiveDelayInfo> result;
    const auto add = [&](const semantic::sv::Delay& value) {
        const auto typical = udp_delay_value(
            value.typical ? *value.typical : value.primary, specialization);
        const auto minimum = value.minimum
            ? udp_delay_value(*value.minimum, specialization) : typical;
        const auto maximum = value.maximum
            ? udp_delay_value(*value.maximum, specialization) : typical;
        if (!minimum || !typical || !maximum) {
            return false;
        }
        result.push_back({ *minimum, *typical, *maximum });
        return true;
    };
    if (!add(delay)) {
        return { };
    }
    for (const auto& additional : delay.additional) {
        if (!add(additional)) {
            return { };
        }
    }
    return result;
}

void apply_udp_output_profile(
    runtime::simir::Process& process,
    const runtime::simir::DriveStrength strength,
    const std::optional<runtime::simir::TransitionDelays> delays)
{
    using namespace runtime::simir;
    process.drive_strength = strength;
    if (!delays) {
        return;
    }
    for (auto& operation : process.operations) {
        if (const auto* write_update
            = operation_get_if<WriteUpdate>(&operation)) {
            operation = WriteInertial {
                write_update->signal, write_update->source, *delays,
                write_update->domain,
            };
        } else if (const auto* write_slice
            = operation_get_if<WriteUpdateSlice>(&operation)) {
            operation = WriteInertialSlice {
                write_slice->signal, write_slice->source,
                write_slice->offset, *delays, write_slice->domain
            };
        } else if (const auto* write_dynamic_slice
            = operation_get_if<WriteUpdateDynamicSlice>(&operation)) {
            operation = WriteInertialDynamicSlice {
                write_dynamic_slice->signal, write_dynamic_slice->source,
                write_dynamic_slice->selection, *delays,
                write_dynamic_slice->domain
            };
        } else if (const auto* write_dynamic_part_slice
            = operation_get_if<WriteUpdateDynamicPartSlice>(&operation)) {
            operation = WriteInertialDynamicPartSlice {
                write_dynamic_part_slice->signal,
                write_dynamic_part_slice->source,
                write_dynamic_part_slice->selection, *delays,
                write_dynamic_part_slice->domain
            };
        }
    }
}

class CompiledUdpProcessBuilder final {
public:
    CompiledUdpProcessBuilder(
        ElaboratedDesign& design,
        std::string path,
        const semantic::sv::UdpDeclaration& declaration,
        std::span<const runtime::simir::SignalId> inputs,
        const runtime::simir::SignalId value,
        std::span<const runtime::simir::SignalId> previous,
        const std::optional<runtime::simir::SignalId> initialized)
        : declaration_(declaration)
        , inputs_(inputs)
        , value_(value)
        , previous_(previous)
        , initialized_(initialized)
    {
        process_.id = static_cast<runtime::simir::ProcessId>(
            design.process_count());
        process_.name = std::move(path) + (declaration.sequential
                ? ".$udp_state"
                : ".$udp");
        process_.language_standard = declaration.standard;
        process_.compatibility_profile = declaration.compatibility_profile;
        process_.scheduling_domain
            = runtime::simir::ProcessSchedulingDomain::systemverilog;
        for (const auto signal : inputs_) {
            process_.static_sensitivity.push_back({
                signal, runtime::simir::EdgeKind::any
            });
        }
    }

    [[nodiscard]] runtime::simir::Process build()
    {
        // The activation that applies the initial output is not an input
        // transition of the table.
        std::optional<runtime::simir::RegisterId> was_initialized;
        if (declaration_.sequential && declaration_.initial_output
            && initialized_) {
            was_initialized = equals(*initialized_, runtime::Logic4::one);
        }
        if (declaration_.sequential) {
            append_initialization();
        }
        std::vector<runtime::simir::InstructionIndex> exits;
        for (const auto& row : declaration_.rows) {
            auto condition = constant(runtime::Logic4::one);
            if (row.current_state) {
                condition = conjunction(
                    condition,
                    level_condition(value_, *row.current_state));
            }
            for (std::size_t index = 0U;
                index < row.inputs.size(); ++index) {
                const auto term = declaration_.sequential
                    ? edge_condition(index, row.inputs[index])
                    : level_condition(
                        inputs_[index], row.inputs[index].level);
                condition = conjunction(condition, term);
            }
            const auto branch = instruction();
            process_.operations.emplace_back(runtime::simir::Branch {
                condition, 0U, 0U,
                runtime::simir::UnknownBranchPolicy::when_false
            });
            const auto when_true = instruction();
            if (row.output != semantic::sv::UdpOutput::no_change) {
                process_.operations.emplace_back(
                    runtime::simir::WriteBlocking {
                        value_, constant(output_value(row.output))
                    });
            }
            exits.push_back(instruction());
            process_.operations.emplace_back(runtime::simir::Jump { 0U });
            const auto when_false = instruction();
            process_.operations[branch] = runtime::simir::Branch {
                condition, when_true, when_false,
                runtime::simir::UnknownBranchPolicy::when_false
            };
        }
        if (!declaration_.sequential) {
            process_.operations.emplace_back(runtime::simir::WriteBlocking {
                value_, constant(runtime::Logic4::x)
            });
        } else if (!inputs_.empty()) {
            // An input change that no row covers makes the output x
            // (IEEE 1800-2017 29.6).
            auto any_changed = changed(0U);
            for (std::size_t index = 1U; index < inputs_.size(); ++index) {
                any_changed = disjunction(any_changed, changed(index));
            }
            if (was_initialized) {
                any_changed = conjunction(any_changed, *was_initialized);
            }
            const auto branch = instruction();
            process_.operations.emplace_back(runtime::simir::Branch {
                any_changed, 0U, 0U,
                runtime::simir::UnknownBranchPolicy::when_false
            });
            const auto when_true = instruction();
            process_.operations.emplace_back(runtime::simir::WriteBlocking {
                value_, constant(runtime::Logic4::x)
            });
            const auto when_false = instruction();
            process_.operations[branch] = runtime::simir::Branch {
                any_changed, when_true, when_false,
                runtime::simir::UnknownBranchPolicy::when_false
            };
        }
        const auto finish = instruction();
        for (const auto exit : exits) {
            process_.operations[exit] = runtime::simir::Jump { finish };
        }
        if (declaration_.sequential) {
            for (std::size_t index = 0U;
                index < inputs_.size(); ++index) {
                const auto current = read(inputs_[index]);
                process_.operations.emplace_back(
                    runtime::simir::WriteBlocking {
                        previous_[index], current
                    });
            }
        }
        process_.operations.emplace_back(runtime::simir::WaitSensitivity { });
        process_.operations.emplace_back(runtime::simir::Jump { 0U });
        process_.register_count = widths_.size();
        process_.register_value_kinds.assign(
            widths_.size(), runtime::simir::ValueKind::logic4);
        process_.driver_regions
            = elaboration_detail::collect_driver_regions(process_, widths_);
        return std::move(process_);
    }

private:
    [[nodiscard]] runtime::simir::InstructionIndex instruction() const
    {
        return static_cast<runtime::simir::InstructionIndex>(
            process_.operations.size());
    }

    [[nodiscard]] runtime::simir::RegisterId allocate()
    {
        const auto result = static_cast<runtime::simir::RegisterId>(
            widths_.size());
        widths_.push_back(1U);
        return result;
    }

    [[nodiscard]] runtime::simir::RegisterId constant(
        const runtime::Logic4 value)
    {
        const auto result = allocate();
        process_.operations.emplace_back(runtime::simir::LoadConstant {
            result, runtime::PackedLogic4 { 1U, value }
        });
        return result;
    }

    [[nodiscard]] runtime::simir::RegisterId read(
        const runtime::simir::SignalId signal)
    {
        const auto result = allocate();
        process_.operations.emplace_back(runtime::simir::ReadSignal {
            result, signal
        });
        return result;
    }

    [[nodiscard]] runtime::simir::RegisterId binary(
        const runtime::simir::BinaryOperator operation,
        const runtime::simir::RegisterId left,
        const runtime::simir::RegisterId right)
    {
        const auto result = allocate();
        process_.operations.emplace_back(runtime::simir::Binary {
            operation, result, left, right
        });
        return result;
    }

    [[nodiscard]] runtime::simir::RegisterId conjunction(
        const runtime::simir::RegisterId left,
        const runtime::simir::RegisterId right)
    {
        return binary(runtime::simir::BinaryOperator::bit_and, left, right);
    }

    [[nodiscard]] runtime::simir::RegisterId disjunction(
        const runtime::simir::RegisterId left,
        const runtime::simir::RegisterId right)
    {
        return binary(runtime::simir::BinaryOperator::bit_or, left, right);
    }

    [[nodiscard]] runtime::simir::RegisterId invert(
        const runtime::simir::RegisterId source)
    {
        const auto result = allocate();
        process_.operations.emplace_back(runtime::simir::LogicalNot {
            result, source
        });
        return result;
    }

    [[nodiscard]] runtime::simir::RegisterId equals(
        const runtime::simir::SignalId signal,
        const runtime::Logic4 value)
    {
        return binary(runtime::simir::BinaryOperator::case_equal,
            read(signal), constant(value));
    }

    [[nodiscard]] runtime::simir::RegisterId level_condition(
        const runtime::simir::SignalId signal,
        const semantic::sv::UdpLevel level)
    {
        using Level = semantic::sv::UdpLevel;
        switch (level) {
        case Level::zero:
            return equals(signal, runtime::Logic4::zero);
        case Level::one:
            return equals(signal, runtime::Logic4::one);
        case Level::unknown:
            return disjunction(
                equals(signal, runtime::Logic4::x),
                equals(signal, runtime::Logic4::z));
        case Level::dont_care:
            return constant(runtime::Logic4::one);
        case Level::binary:
            return disjunction(
                equals(signal, runtime::Logic4::zero),
                equals(signal, runtime::Logic4::one));
        }
        return constant(runtime::Logic4::zero);
    }

    [[nodiscard]] runtime::simir::RegisterId transition(
        const std::size_t input,
        const semantic::sv::UdpLevel before,
        const semantic::sv::UdpLevel after)
    {
        return conjunction(
            level_condition(previous_[input], before),
            level_condition(inputs_[input], after));
    }

    [[nodiscard]] runtime::simir::RegisterId changed(
        const std::size_t input)
    {
        return invert(binary(runtime::simir::BinaryOperator::case_equal,
            read(previous_[input]), read(inputs_[input])));
    }

    [[nodiscard]] runtime::simir::RegisterId edge_condition(
        const std::size_t input,
        const semantic::sv::UdpInputPattern& pattern)
    {
        using Edge = semantic::sv::UdpEdge;
        using Level = semantic::sv::UdpLevel;
        const auto unknown = Level::unknown;
        const auto zero = Level::zero;
        const auto one = Level::one;
        switch (pattern.edge) {
        case Edge::rising:
            return transition(input, zero, one);
        case Edge::falling:
            return transition(input, one, zero);
        case Edge::positive:
            return disjunction(
                transition(input, zero, one),
                disjunction(
                    transition(input, zero, unknown),
                    transition(input, unknown, one)));
        case Edge::negative:
            return disjunction(
                transition(input, one, zero),
                disjunction(
                    transition(input, one, unknown),
                    transition(input, unknown, zero)));
        case Edge::any:
            return changed(input);
        case Edge::explicit_edge:
            return conjunction(
                transition(input, pattern.previous, pattern.current),
                changed(input));
        case Edge::none:
            return level_condition(inputs_[input], pattern.level);
        }
        return constant(runtime::Logic4::zero);
    }

    [[nodiscard]] static runtime::Logic4 output_value(
        const semantic::sv::UdpOutput value)
    {
        switch (value) {
        case semantic::sv::UdpOutput::zero:
            return runtime::Logic4::zero;
        case semantic::sv::UdpOutput::one:
            return runtime::Logic4::one;
        case semantic::sv::UdpOutput::unknown:
        case semantic::sv::UdpOutput::no_change:
            return runtime::Logic4::x;
        }
        return runtime::Logic4::x;
    }

    void append_initialization()
    {
        if (!declaration_.initial_output || !initialized_) {
            return;
        }
        const auto initialized = equals(
            *initialized_, runtime::Logic4::one);
        const auto condition = invert(initialized);
        const auto branch = instruction();
        process_.operations.emplace_back(runtime::simir::Branch {
            condition, 0U, 0U,
            runtime::simir::UnknownBranchPolicy::when_false
        });
        const auto when_true = instruction();
        process_.operations.emplace_back(runtime::simir::WriteBlocking {
            value_, constant(output_value(*declaration_.initial_output))
        });
        process_.operations.emplace_back(runtime::simir::WriteBlocking {
            *initialized_, constant(runtime::Logic4::one)
        });
        const auto exit = instruction();
        process_.operations.emplace_back(runtime::simir::Jump { 0U });
        const auto when_false = instruction();
        const auto finish = instruction();
        process_.operations[branch] = runtime::simir::Branch {
            condition, when_true, when_false,
            runtime::simir::UnknownBranchPolicy::when_false
        };
        process_.operations[exit] = runtime::simir::Jump { finish };
    }

    const semantic::sv::UdpDeclaration& declaration_;
    std::span<const runtime::simir::SignalId> inputs_;
    runtime::simir::SignalId value_ { };
    std::span<const runtime::simir::SignalId> previous_;
    std::optional<runtime::simir::SignalId> initialized_;
    runtime::simir::Process process_;
    std::vector<std::size_t> widths_;
};

} // namespace

UdpTableId HierarchyBuilder::register_udp_table(UdpTableInfo table)
{
    if (const auto found = udp_table_by_identity_.find(table.identity);
        found != udp_table_by_identity_.end()) {
        return found->second;
    }
    table.id = static_cast<UdpTableId>(design_.udp_tables_.size());
    const auto id = table.id;
    const auto identity = table.identity;
    design_.udp_tables_.push_back(std::move(table));
    udp_table_by_identity_.emplace(identity, id);
    return id;
}

UdpTableId HierarchyBuilder::normalized_udp_table(
    const semantic::sv::UdpDeclaration& declaration)
{
    return register_udp_table(make_udp_table(declaration, compiled_));
}

bool HierarchyBuilder::instantiate_compiled_udp(
    const semantic::sv::UdpDeclaration& declaration,
    const semantic::sv::Instance& instance,
    const semantic::SpecializedHirUnit& specialization,
    const std::string& path,
    const std::optional<std::int64_t> array_index,
    const SignalMap& parent_signals,
    const ReadOnlySignalSet& read_only_signals,
    const StringMap& parent_strings,
    const ReadOnlyStringSet& read_only_strings,
    const ContainerMap& parent_containers,
    const ReadOnlyContainerSet& read_only_containers,
    const Binding*)
{
    const auto source = compiled_source_span(*compiled_, instance.source);
    if (!instance.parameters.empty()) {
        report(
            "FSIM-ELAB-BIND-060",
            "UDP instance '" + path
                + "' cannot have parameter overrides",
            source);
        return false;
    }
    if (std::ranges::any_of(instance.ports,
            [](const semantic::sv::ActualAssociation& association) {
                return association.formal.has_value();
            })) {
        report(
            "FSIM-ELAB-BIND-061",
            "UDP instance '" + path
                + "' requires positional terminal connections",
            source);
        return false;
    }
    const auto terminal_count = declaration.inputs.size() + 1U;
    if (instance.ports.size() != terminal_count) {
        report(
            "FSIM-ELAB-BIND-004",
            "UDP instance '" + path + "' has "
                + std::to_string(instance.ports.size())
                + " terminal associations; expected "
                + std::to_string(terminal_count),
            source);
        return false;
    }
    for (const auto& association : instance.ports) {
        if (association.kind == semantic::sv::ActualKind::type
            || (association.kind == semantic::sv::ActualKind::expression
                && !association.expression)) {
            report(
                "FSIM-ELAB-BIND-004",
                "UDP instance '" + path
                    + "' has a non-value terminal association",
                compiled_source_span(*compiled_, association.source));
            return false;
        }
    }

    std::optional<std::size_t> array_ordinal;
    if (array_index) {
        const auto selected = std::ranges::find(
            instance.array_indices, *array_index);
        if (selected == instance.array_indices.end()) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled UDP instance-array occurrence '" + path
                    + "' has no matching source index",
                source);
            return false;
        }
        array_ordinal = static_cast<std::size_t>(std::distance(
            instance.array_indices.begin(), selected));
    } else if (!instance.array_indices.empty()) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled UDP instance array '" + path
                + "' requires a selected occurrence index",
            source);
        return false;
    }

    const auto table_id = normalized_udp_table(declaration);
    const auto& table = design_.udp_tables_[table_id];
    const auto delays = instance.udp_delay
        ? udp_transition_delays(*instance.udp_delay, specialization)
        : std::optional<runtime::simir::TransitionDelays> { };
    if (instance.udp_delay && !delays) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled UDP propagation delay at '" + path
                + "' is not a static nonnegative simulation time",
            source);
        return false;
    }
    const auto strength = runtime::simir::DriveStrength {
        instance.drive_zero
            ? udp_strength_rank(*instance.drive_zero)
            : runtime::simir::StrengthRank::strong,
        instance.drive_one
            ? udp_strength_rank(*instance.drive_one)
            : runtime::simir::StrengthRank::strong,
    };

    const auto add_signal = [&](const std::string_view local_name,
                                const frontend::PortDirection direction,
                                const bool is_port,
                                const runtime::Logic4 initial
                                    = runtime::Logic4::x)
        -> std::optional<SignalId> {
        const auto index = design_.signals_.size();
        const auto id = static_cast<SignalId>(index);
        if (static_cast<std::size_t>(id) != index) {
            report(
                "FSIM-ELAB-011",
                "the design has too many signals for dense 32-bit IDs",
                source);
            return std::nullopt;
        }
        const auto name = path + "." + std::string { local_name };
        SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = is_port ? "wire" : "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        info.is_port = is_port;
        info.direction = direction;
        info.declaration_span = compiled_source_span(
            *compiled_, declaration.source);
        design_.signal_info_.push_back(std::move(info));
        design_.signals_.push_back(runtime::simir::Signal {
            name,
            runtime::PackedLogic4 { 1U, initial },
            runtime::simir::ResolutionKind::none,
            runtime::simir::ValueKind::logic4,
        });
        design_.signal_by_name_.insert_or_assign(name, id);
        return id;
    };

    const auto output = add_signal(
        "$udp_value", frontend::PortDirection::Unknown, false);
    if (!output) {
        return false;
    }
    const auto direct_scalar_signal = [&](const semantic::ExpressionId id)
        -> std::optional<SignalId> {
        const auto expression = specialization.find_expression(id);
        if (!expression || expression->systemverilog == nullptr
            || expression->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            return std::nullopt;
        }
        const auto& record = *expression->systemverilog;
        auto name = record.text;
        if (record.referenced_name && record.referenced_name->selected) {
            const auto selected = specialization.find_declaration(
                *record.referenced_name->selected);
            if (selected && selected->systemverilog != nullptr) {
                name = selected->systemverilog->name;
            }
        }
        const auto found = parent_signals.find(name);
        if (found == parent_signals.end()
            || found->second >= design_.signal_info_.size()
            || design_.signal_info_[found->second].width != 1U) {
            return std::nullopt;
        }
        return found->second;
    };
    std::vector<SignalId> inputs;
    std::vector<bool> direct_inputs;
    inputs.reserve(declaration.inputs.size());
    direct_inputs.reserve(declaration.inputs.size());
    for (std::size_t index = 0U;
        index < declaration.inputs.size(); ++index) {
        const auto& input = declaration.inputs[index];
        const auto& association = instance.ports[index + 1U];
        if (!array_ordinal && association.expression) {
            if (const auto actual = direct_scalar_signal(
                    *association.expression)) {
                inputs.push_back(*actual);
                direct_inputs.push_back(true);
                design_.signal_by_name_.insert_or_assign(
                    path + "." + input, *actual);
                continue;
            }
        }
        auto initial = runtime::Logic4::x;
        if (association.kind == semantic::sv::ActualKind::open
            || association.kind
                == semantic::sv::ActualKind::default_value) {
            initial = instance.unconnected_drive
                    == semantic::sv::UnconnectedDrive::pull_zero
                ? runtime::Logic4::zero
                : instance.unconnected_drive
                    == semantic::sv::UnconnectedDrive::pull_one
                ? runtime::Logic4::one
                : runtime::Logic4::z;
        }
        const auto signal = add_signal(
            input, frontend::PortDirection::Input, true, initial);
        if (!signal) {
            return false;
        }
        inputs.push_back(*signal);
        direct_inputs.push_back(false);
    }
    std::vector<SignalId> previous;
    std::optional<SignalId> initialized;
    if (declaration.sequential) {
        previous.reserve(declaration.inputs.size());
        for (std::size_t index = 0U;
            index < declaration.inputs.size(); ++index) {
            const auto signal = add_signal(
                "$udp_previous$" + std::to_string(index),
                frontend::PortDirection::Unknown,
                false);
            if (!signal) {
                return false;
            }
            previous.push_back(*signal);
        }
        if (declaration.initial_output) {
            initialized = add_signal(
                "$udp_initialized",
                frontend::PortDirection::Unknown,
                false);
            if (!initialized) {
                return false;
            }
        }
    }

    struct SelectedArrayTerminal {
        SignalId signal { };
        std::uint32_t offset { };
        bool selected { };
    };
    const auto selected_array_terminal = [&](
                                             const semantic::ExpressionId id,
                                             const semantic::SourceSpanId
                                                 association_source)
        -> std::optional<SelectedArrayTerminal> {
        const auto expression = specialization.find_expression(id);
        if (!expression || expression->systemverilog == nullptr
            || expression->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            report(
                "FSIM-ELAB-BIND-064",
                "UDP instance-array terminal at '" + path
                    + "' must name a scalar or packed vector signal",
                compiled_source_span(*compiled_, association_source));
            return std::nullopt;
        }
        const auto& record = *expression->systemverilog;
        auto name = record.text;
        if (record.referenced_name && record.referenced_name->selected) {
            const auto declaration_record = specialization.find_declaration(
                *record.referenced_name->selected);
            if (declaration_record
                && declaration_record->systemverilog != nullptr) {
                name = declaration_record->systemverilog->name;
            }
        }
        const auto found = parent_signals.find(name);
        if (found == parent_signals.end()
            || found->second >= design_.signal_info_.size()) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled UDP terminal '" + name
                    + "' is not a signal in its parent scope",
                compiled_source_span(*compiled_, association_source));
            return std::nullopt;
        }
        const auto& info = design_.signal_info_[found->second];
        if (info.width == 1U) {
            return SelectedArrayTerminal { found->second, 0U, false };
        }
        if (info.width != instance.array_indices.size()
            || !info.packed_range) {
            report(
                "FSIM-ELAB-BIND-064",
                "UDP instance-array terminal '" + name
                    + "' must be scalar or match the instance count",
                compiled_source_span(*compiled_, association_source));
            return std::nullopt;
        }
        const auto& range = *info.packed_range;
        const auto selected = range.left
            + (range.descending
                    ? -static_cast<std::int64_t>(*array_ordinal)
                    : static_cast<std::int64_t>(*array_ordinal));
        const auto offset = range.descending
            ? selected - range.right
            : range.right - selected;
        if (offset < 0
            || static_cast<std::uint64_t>(offset)
                >= info.width) {
            report(
                "FSIM-ELAB-BIND-064",
                "UDP instance-array terminal '" + name
                    + "' has an incompatible packed range",
                compiled_source_span(*compiled_, association_source));
            return std::nullopt;
        }
        return SelectedArrayTerminal {
            found->second, static_cast<std::uint32_t>(offset), true
        };
    };

    auto make_lowerer = [&] {
        Lowerer result {
            design_, parent_signals, read_only_signals,
            parent_strings, read_only_strings,
            parent_containers, read_only_containers,
            diagnostics_
        };
        result.set_vhdl_lowering_index(
            &vhdl_lowering_index_);
        result.set_specialized_hir_unit(&specialization);
        return result;
    };

    SpecializationInfo profile;
    const auto profile_index = design_.specializations_.size();
    profile.id = static_cast<SpecializationId>(profile_index);
    if (static_cast<std::size_t>(profile.id) != profile_index) {
        throw std::length_error(
            "too many elaborated design-unit specializations");
    }
    const auto library = declaration.library.empty()
        ? std::string { "work" }
        : declaration.library;
    profile.unit = "sv:" + library + "." + declaration.name;
    profile.instance = path;
    profile.source_instance = instance.id;
    const auto owner = compiled_->find_unit(
        semantic::UnitKind::systemverilog_compilation_unit,
        library, "$unit$udp$" + declaration.name);
    if (owner && owner->identity->source == declaration.source) {
        profile.source_unit = owner->identity->id;
    }
    profile.source_span = declaration.source;
    profile.origin = declaration.origin;
    const auto declaration_source = compiled_source_span(
        *compiled_, declaration.source);
    profile.source = std::string {
        frontend::physical_source(declaration_source)
    };
    profile.language = udp_language(declaration);
    profile.library = library;
    profile.parameter_identity_values = {
        { "__udp", table.identity },
        { "__udp_table", table.digest },
    };

    // The process whose delayed write carries the output delay (retimed by
    // vpi_put_delays).
    std::vector<runtime::simir::ProcessId> output_drivers;
    const auto append_process = [&](runtime::simir::Process process) {
        canonicalize_process_operations(process);
        profile.processes.push_back(process.id);
        design_.append_process_record(std::move(process));
    };
    const auto append_manual_input = [&](
        const SelectedArrayTerminal actual,
        const SignalId destination,
        const std::size_t order) {
        runtime::simir::Process process;
        process.id = static_cast<runtime::simir::ProcessId>(
            design_.process_count());
        process.name = path + ".$udp_input_" + std::to_string(order);
        process.language_standard = declaration.standard;
        process.compatibility_profile = declaration.compatibility_profile;
        process.scheduling_domain
            = runtime::simir::ProcessSchedulingDomain::systemverilog;
        process.static_sensitivity.push_back({
            actual.signal, runtime::simir::EdgeKind::any
        });
        process.operations.emplace_back(runtime::simir::ReadSignal {
            0U, actual.signal
        });
        auto value = runtime::simir::RegisterId { 0U };
        std::vector<std::size_t> widths {
            design_.signal_info_[actual.signal].width
        };
        process.register_value_kinds.push_back(
            design_.signals_[actual.signal].value_kind);
        if (actual.selected) {
            value = 1U;
            widths.push_back(1U);
            process.register_value_kinds.push_back(
                runtime::simir::ValueKind::logic4);
            process.operations.emplace_back(runtime::simir::Extract {
                value, 0U, actual.offset, 1U
            });
        }
        process.operations.emplace_back(runtime::simir::WriteUpdate {
            destination,
            value,
            runtime::simir::SignalUpdateDomain::systemverilog_active,
        });
        process.operations.emplace_back(
            runtime::simir::WaitSensitivity { });
        process.operations.emplace_back(runtime::simir::Jump { 0U });
        process.register_count = widths.size();
        process.driver_regions = elaboration_detail::collect_driver_regions(
            process, widths);
        append_process(std::move(process));
    };
    const auto append_manual_output = [&](
        const SignalId value,
        const SelectedArrayTerminal actual) {
        runtime::simir::Process process;
        process.id = static_cast<runtime::simir::ProcessId>(
            design_.process_count());
        process.name = path + ".$udp_output";
        process.language_standard = declaration.standard;
        process.compatibility_profile = declaration.compatibility_profile;
        process.scheduling_domain
            = runtime::simir::ProcessSchedulingDomain::systemverilog;
        process.static_sensitivity.push_back({
            value, runtime::simir::EdgeKind::any
        });
        process.operations.emplace_back(runtime::simir::ReadSignal {
            0U, value
        });
        if (actual.selected) {
            process.operations.emplace_back(runtime::simir::WriteUpdateSlice {
                actual.signal, 0U, actual.offset,
                runtime::simir::SignalUpdateDomain::systemverilog_active,
            });
        } else {
            process.operations.emplace_back(runtime::simir::WriteUpdate {
                actual.signal, 0U,
                runtime::simir::SignalUpdateDomain::systemverilog_active,
            });
        }
        process.operations.emplace_back(
            runtime::simir::WaitSensitivity { });
        process.operations.emplace_back(runtime::simir::Jump { 0U });
        process.register_count = 1U;
        process.register_value_kinds.push_back(
            runtime::simir::ValueKind::logic4);
        apply_udp_output_profile(process, strength, delays);
        if (delays) {
            output_drivers.push_back(process.id);
        }
        const std::array<std::size_t, 1U> widths { 1U };
        process.driver_regions = elaboration_detail::collect_driver_regions(
            process, widths);
        append_process(std::move(process));
    };

    for (std::size_t index = 0U; index < declaration.inputs.size(); ++index) {
        const auto& association = instance.ports[index + 1U];
        if (association.kind == semantic::sv::ActualKind::open
            || association.kind
                == semantic::sv::ActualKind::default_value) {
            continue;
        }
        if (direct_inputs[index]) {
            continue;
        }
        if (array_ordinal) {
            const auto actual = selected_array_terminal(
                *association.expression, association.source);
            if (!actual) {
                return false;
            }
            append_manual_input(*actual, inputs[index], index);
            continue;
        }
        auto lowerer = make_lowerer();
        const auto diagnostics_before = diagnostics_.size();
        auto adapter = lowerer.lower_hir_input_actual(
            *association.expression, inputs[index],
            udp_language(declaration), path, index);
        if (!adapter) {
            if (diagnostics_.size() == diagnostics_before) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled UDP input terminal '"
                        + declaration.inputs[index]
                        + "' requires an unavailable HIR expression adapter",
                    compiled_source_span(*compiled_, association.source));
            }
            return false;
        }
        adapter->language_standard = declaration.standard;
        adapter->compatibility_profile = declaration.compatibility_profile;
        append_process(std::move(*adapter));
    }

    const auto& output_association = instance.ports.front();
    if (output_association.kind == semantic::sv::ActualKind::open
        || output_association.kind
            == semantic::sv::ActualKind::default_value) {
        // The table process still owns its internal state and debugger-visible
        // occurrence even when the output terminal is intentionally open.
    } else if (array_ordinal) {
        const auto actual = selected_array_terminal(
            *output_association.expression, output_association.source);
        if (!actual) {
            return false;
        }
        append_manual_output(*output, *actual);
    } else {
        if (const auto actual = direct_scalar_signal(
                *output_association.expression)) {
            design_.signal_by_name_.insert_or_assign(
                path + "." + declaration.output, *actual);
        }
        auto lowerer = make_lowerer();
        const auto diagnostics_before = diagnostics_.size();
        auto adapter = lowerer.lower_hir_output_actual(
            *output, *output_association.expression,
            udp_language(declaration), path, 0U);
        if (!adapter) {
            if (diagnostics_.size() == diagnostics_before) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled UDP output terminal '" + declaration.output
                        + "' requires an unavailable HIR lvalue adapter",
                    compiled_source_span(
                        *compiled_, output_association.source));
            }
            return false;
        }
        adapter->language_standard = declaration.standard;
        adapter->compatibility_profile = declaration.compatibility_profile;
        apply_udp_output_profile(*adapter, strength, delays);
        if (delays) {
            output_drivers.push_back(adapter->id);
        }
        append_process(std::move(*adapter));
    }
    append_process(CompiledUdpProcessBuilder {
        design_, path, declaration, inputs, *output, previous, initialized
    }.build());

    // The instance record for the VPI primitive objects (IEEE 1800-2023
    // 29, 37.35, 37.36). Each terminal is the one-bit signal the table reads
    // or drives: the connected net when it is a plain scalar, else the
    // instance's own copy.
    {
        VerilogPrimitiveInfo info;
        const auto separator = path.rfind('.');
        info.instance = separator == std::string::npos
            ? std::string { } : path.substr(0U, separator);
        if (!instance.anonymous) {
            info.name = separator == std::string::npos
                ? path : path.substr(separator + 1U);
            info.full_name = path;
        }
        info.kind = VerilogPrimitiveKind::udp;
        info.keyword = declaration.name;
        info.udp_table = table_id;
        info.array_name = instance.anonymous ? std::string { }
                                             : instance.name;
        if (array_index && !instance.array_indices.empty()) {
            info.array_member = true;
            info.array_index = *array_index;
            info.array_left = instance.array_indices.front();
            info.array_right = instance.array_indices.back();
        }
        info.terminals.push_back({
            frontend::VerilogPrimitiveTerminalDirection::Output,
            *output, 0U, 1U, { }, std::nullopt });
        for (const auto input : inputs) {
            info.terminals.push_back({
                frontend::VerilogPrimitiveTerminalDirection::Input,
                input, 0U, 1U, { }, std::nullopt });
        }
        if (instance.udp_delay) {
            info.delays = primitive_delay_info(
                *instance.udp_delay, specialization);
        }
        info.drive_zero = instance.drive_zero;
        info.drive_one = instance.drive_one;
        info.drivers = std::move(output_drivers);
        info.source = source;
        design_.verilog_primitives_.push_back(std::move(info));
    }

    // A module path of the enclosing module may end at this primitive's
    // output; its delay then applies to the primitive's driver (IEEE
    // 1364-2005 14.2). The enclosing paths were attached before the
    // primitive existed.
    attach_verilog_specify_drivers(0U, profile.processes);
    design_.specializations_.push_back(std::move(profile));
    return true;
}

void HierarchyBuilder::record_verilog_primitives(
    const std::vector<semantic::sv::PrimitiveInstance>& primitives,
    const semantic::SpecializedHirUnit& specialization,
    const std::string& scope_path,
    const SignalMap& signals,
    const std::span<const runtime::simir::ProcessId> processes,
    const GateArrayElement* const array_element)
{
    using Kind = semantic::sv::PrimitiveKind;
    using Direction = semantic::sv::PrimitiveTerminalDirection;
    using Form = semantic::sv::ExpressionKind;
    const auto terminal_for = [&](const semantic::ExpressionId expression)
        -> VerilogPrimitiveTerminalInfo {
        VerilogPrimitiveTerminalInfo result;
        const auto view = specialization.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return result;
        }
        result.expression = view->systemverilog->text;
        result.hir_expression = expression.value();
        // A bit or part select of a net names the net first.
        auto base = view;
        while (base && base->systemverilog != nullptr
            && (base->systemverilog->kind == Form::index
                || base->systemverilog->kind == Form::slice)
            && !base->systemverilog->operands.empty()) {
            base = specialization.find_expression(
                base->systemverilog->operands.front());
        }
        if (!base || base->systemverilog == nullptr
            || base->systemverilog->kind != Form::name) {
            return result;
        }
        const auto& record = *base->systemverilog;
        auto name = record.text;
        if (record.referenced_name && record.referenced_name->selected) {
            const auto selected = specialization.find_declaration(
                *record.referenced_name->selected);
            if (selected && selected->systemverilog != nullptr) {
                name = selected->systemverilog->name;
            }
        }
        const auto found = signals.find(name);
        if (found == signals.end()
            || found->second >= design_.signal_info_.size()) {
            return result;
        }
        const auto selection = resolve_verilog_specify_selection(
            expression, found->second, design_.signal_info_[found->second],
            specialization);
        if (selection) {
            result.signal = selection->signal;
            result.offset = selection->offset;
            result.width = selection->width;
            result.expression.clear();
            result.hir_expression.reset();
        }
        return result;
    };
    // A named gate is lowered to processes named after its instance (the
    // statement label: `g`, `g[1]`, `g$output1`); the ones that hold a
    // delayed write carry its delay. Indexed on the first delayed gate.
    std::unordered_map<std::string, std::vector<runtime::simir::ProcessId>>
        delayed_drivers;
    bool indexed { };
    const auto index_drivers = [&] {
        indexed = true;
        for (const auto id : processes) {
            const auto view = design_.process_view(id);
            bool delayed { };
            for (const auto& operation : view.operations()) {
                delayed = delayed
                    || runtime::simir::operation_get_if<
                           runtime::simir::WriteInertial>(&operation)
                        != nullptr
                    || runtime::simir::operation_get_if<
                           runtime::simir::WriteInertialSlice>(&operation)
                        != nullptr;
            }
            if (!delayed) {
                continue;
            }
            delayed_drivers[view.name()].push_back(id);
        }
    };
    for (const auto& primitive : primitives) {
        // A UDP instance is recorded as it is instantiated.
        if (primitive.kind == Kind::udp) {
            continue;
        }
        VerilogPrimitiveInfo info;
        info.instance = scope_path;
        switch (primitive.kind) {
        case Kind::gate:
            info.kind = VerilogPrimitiveKind::gate;
            break;
        case Kind::switch_device:
            info.kind = VerilogPrimitiveKind::switch_device;
            break;
        case Kind::pull:
            info.kind = VerilogPrimitiveKind::pull;
            break;
        case Kind::udp:
            break;
        }
        info.keyword = primitive.keyword;
        info.array_name = primitive.name;
        info.array_member = primitive.array_member;
        info.array_index = primitive.array_index;
        info.array_left = primitive.array_left;
        info.array_right = primitive.array_right;
        if (array_element != nullptr) {
            info.array_name = array_element->name;
            info.array_member = true;
            info.array_index = array_element->index;
            info.array_left = array_element->left;
            info.array_right = array_element->right;
        }
        if (!info.array_name.empty()) {
            info.name = info.array_member
                ? info.array_name + "[" + std::to_string(info.array_index)
                    + "]"
                : info.array_name;
            info.full_name = scope_path.empty()
                ? info.name : scope_path + "." + info.name;
        }
        for (const auto& terminal : primitive.terminals) {
            auto resolved = terminal.expression
                ? terminal_for(*terminal.expression)
                : VerilogPrimitiveTerminalInfo { };
            resolved.direction = terminal.direction == Direction::output
                ? frontend::VerilogPrimitiveTerminalDirection::Output
                : terminal.direction == Direction::inout
                ? frontend::VerilogPrimitiveTerminalDirection::Inout
                : frontend::VerilogPrimitiveTerminalDirection::Input;
            info.terminals.push_back(std::move(resolved));
        }
        if (primitive.delay) {
            info.delays = primitive_delay_info(
                *primitive.delay, specialization);
        }
        info.drive_zero = primitive.drive_zero;
        info.drive_one = primitive.drive_one;
        if (!info.delays.empty() && !info.full_name.empty()) {
            if (!indexed) {
                index_drivers();
            }
            // The processes of a gate array element are named after the
            // generate scope of its iteration.
            const auto& driver_name = array_element != nullptr
                ? array_element->driver_name : info.full_name;
            // One process per output terminal (`$outputN`), and a bufif or
            // notif without a delay splits into `$one`.
            // A tranif's turn-on and turn-off delays are on its control's
            // driver (`$control`).
            for (const auto& suffix : { std::string {}, std::string { "$one" },
                     std::string { "$control" } }) {
                const auto found = delayed_drivers.find(driver_name + suffix);
                if (found != delayed_drivers.end()) {
                    info.drivers.insert(info.drivers.end(),
                        found->second.begin(), found->second.end());
                }
            }
            for (std::size_t output = 0U;; ++output) {
                const auto found = delayed_drivers.find(
                    driver_name + "$output" + std::to_string(output));
                if (found == delayed_drivers.end()) {
                    break;
                }
                info.drivers.insert(info.drivers.end(), found->second.begin(),
                    found->second.end());
            }
        }
        info.source = compiled_source_span(*compiled_, primitive.source);
        design_.verilog_primitives_.push_back(std::move(info));
    }
}

} // namespace fsim::elaboration
