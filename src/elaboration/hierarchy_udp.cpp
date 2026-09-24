// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_driver_regions.hpp"
#include "lowerer_internal.hpp"
#include "fsim/support/sha256.hpp"

#include <array>

namespace fsim::elaboration {
namespace {

frontend::SourceSpan compiled_source_span(
    const semantic::CompiledDesign& compiled,
    const semantic::SourceSpanId source)
{
    frontend::SourceSpan result;
    const auto& spans = compiled.semantics.source_spans();
    if (!source.valid() || source.value() >= spans.size()) {
        return result;
    }
    const auto& span = spans[source.value()];
    result.source_name = span.logical_name;
    result.begin = { static_cast<std::size_t>(span.begin.offset),
        span.begin.line, span.begin.column };
    result.end = { static_cast<std::size_t>(span.end.offset),
        span.end.line, span.end.column };
    const auto& files = compiled.semantics.source_files();
    if (span.file.valid() && span.file.value() < files.size()) {
        result.physical_source_name = files[span.file.value()].physical_name;
    }
    const auto& expansions = compiled.semantics.expansions();
    auto expansion = span.expansion;
    while (expansion && expansion->valid()
        && expansion->value() < expansions.size()) {
        const auto& record = expansions[expansion->value()];
        result.expansion_stack.push_back(record.description);
        expansion = record.parent;
    }
    std::ranges::reverse(result.expansion_stack);
    return result;
}

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
                write_update->signal, write_update->source, *delays
            };
        } else if (const auto* write_slice
            = operation_get_if<WriteUpdateSlice>(&operation)) {
            operation = WriteInertialSlice {
                write_slice->signal, write_slice->source,
                write_slice->offset, *delays
            };
        } else if (const auto* write_dynamic_slice
            = operation_get_if<WriteUpdateDynamicSlice>(&operation)) {
            operation = WriteInertialDynamicSlice {
                write_dynamic_slice->signal, write_dynamic_slice->source,
                write_dynamic_slice->selection, *delays
            };
        } else if (const auto* write_dynamic_part_slice
            = operation_get_if<WriteUpdateDynamicPartSlice>(&operation)) {
            operation = WriteInertialDynamicPartSlice {
                write_dynamic_part_slice->signal,
                write_dynamic_part_slice->source,
                write_dynamic_part_slice->selection, *delays
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
            design.processes().size());
        process_.name = std::move(path) + (declaration.sequential
                ? ".$udp_state"
                : ".$udp");
        process_.language_standard = declaration.standard;
        process_.compatibility_profile = declaration.compatibility_profile;
        for (const auto signal : inputs_) {
            process_.static_sensitivity.push_back({
                signal, runtime::simir::EdgeKind::any
            });
        }
    }

    [[nodiscard]] runtime::simir::Process build()
    {
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

    const auto append_process = [&](runtime::simir::Process process) {
        canonicalize_process_operations(process);
        profile.processes.push_back(process.id);
        design_.processes_.push_back(std::move(process));
    };
    const auto append_manual_input = [&](
        const SelectedArrayTerminal actual,
        const SignalId destination,
        const std::size_t order) {
        runtime::simir::Process process;
        process.id = static_cast<runtime::simir::ProcessId>(
            design_.processes_.size());
        process.name = path + ".$udp_input_" + std::to_string(order);
        process.language_standard = declaration.standard;
        process.compatibility_profile = declaration.compatibility_profile;
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
            destination, value
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
            design_.processes_.size());
        process.name = path + ".$udp_output";
        process.language_standard = declaration.standard;
        process.compatibility_profile = declaration.compatibility_profile;
        process.static_sensitivity.push_back({
            value, runtime::simir::EdgeKind::any
        });
        process.operations.emplace_back(runtime::simir::ReadSignal {
            0U, value
        });
        if (actual.selected) {
            process.operations.emplace_back(runtime::simir::WriteUpdateSlice {
                actual.signal, 0U, actual.offset
            });
        } else {
            process.operations.emplace_back(runtime::simir::WriteUpdate {
                actual.signal, 0U
            });
        }
        process.operations.emplace_back(
            runtime::simir::WaitSensitivity { });
        process.operations.emplace_back(runtime::simir::Jump { 0U });
        process.register_count = 1U;
        process.register_value_kinds.push_back(
            runtime::simir::ValueKind::logic4);
        apply_udp_output_profile(process, strength, delays);
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
        append_process(std::move(*adapter));
    }
    append_process(CompiledUdpProcessBuilder {
        design_, path, declaration, inputs, *output, previous, initialized
    }.build());

    design_.specializations_.push_back(std::move(profile));
    return true;
}

} // namespace fsim::elaboration
