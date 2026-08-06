// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "fsim/support/sha256.hpp"

namespace fsim::elaboration {
namespace {

frontend::Expression udp_literal(const char value,
                                 const frontend::SourceSpan& span) {
    return frontend::Expression{
        frontend::ExpressionKind::LogicLiteral,
        std::string{"1'b"} + value,
        {},
        span};
}

frontend::Expression udp_identifier(
    const std::string& name, const frontend::SourceSpan& span) {
    return frontend::Expression{
        frontend::ExpressionKind::Identifier, name, {}, span};
}

frontend::Expression udp_binary(
    std::string operation,
    frontend::Expression left,
    frontend::Expression right,
    const frontend::SourceSpan& span) {
    return frontend::Expression{
        frontend::ExpressionKind::Binary,
        std::move(operation),
        {std::move(left), std::move(right)},
        span};
}

frontend::Expression udp_level_condition(
    frontend::Expression actual,
    const frontend::VerilogUdpLevelSymbol symbol,
    const frontend::SourceSpan& span) {
    const auto equals = [&](const char value) {
        return udp_binary(
            "===", actual, udp_literal(value, span),
            span);
    };
    switch (symbol) {
    case frontend::VerilogUdpLevelSymbol::Zero:
        return equals('0');
    case frontend::VerilogUdpLevelSymbol::One:
        return equals('1');
    case frontend::VerilogUdpLevelSymbol::Unknown:
        return udp_binary("||", equals('x'), equals('z'), span);
    case frontend::VerilogUdpLevelSymbol::DontCare:
        return udp_literal('1', span);
    case frontend::VerilogUdpLevelSymbol::Binary:
        return udp_binary("||", equals('0'), equals('1'), span);
    }
    return udp_literal('0', span);
}

frontend::Expression udp_level_condition(
    const std::string& input,
    const frontend::VerilogUdpLevelSymbol symbol,
    const frontend::SourceSpan& span) {
    return udp_level_condition(
        udp_identifier(input, span), symbol, span);
}

frontend::Expression udp_or(
    frontend::Expression left,
    frontend::Expression right,
    const frontend::SourceSpan& span) {
    return udp_binary("||", std::move(left), std::move(right), span);
}

frontend::Expression udp_edge_condition(
    const std::string& previous,
    const std::string& current,
    const frontend::VerilogUdpInputPattern& pattern) {
    const auto before = [&](const frontend::VerilogUdpLevelSymbol symbol) {
        return udp_level_condition(
            previous, symbol, pattern.span);
    };
    const auto after = [&](const frontend::VerilogUdpLevelSymbol symbol) {
        return udp_level_condition(current, symbol, pattern.span);
    };
    const auto transition = [&](const frontend::VerilogUdpLevelSymbol from,
                                const frontend::VerilogUdpLevelSymbol to) {
        return udp_binary(
            "&&", before(from), after(to), pattern.span);
    };
    const auto unknown = frontend::VerilogUdpLevelSymbol::Unknown;
    const auto zero = frontend::VerilogUdpLevelSymbol::Zero;
    const auto one = frontend::VerilogUdpLevelSymbol::One;
    switch (pattern.edge) {
    case frontend::VerilogUdpEdgeSymbol::Rising:
        return transition(zero, one);
    case frontend::VerilogUdpEdgeSymbol::Falling:
        return transition(one, zero);
    case frontend::VerilogUdpEdgeSymbol::Positive:
        return udp_or(
            transition(zero, one),
            udp_or(
                transition(zero, unknown),
                transition(unknown, one),
                pattern.span),
            pattern.span);
    case frontend::VerilogUdpEdgeSymbol::Negative:
        return udp_or(
            transition(one, zero),
            udp_or(
                transition(one, unknown),
                transition(unknown, zero),
                pattern.span),
            pattern.span);
    case frontend::VerilogUdpEdgeSymbol::Any:
        return udp_binary(
            "!==",
            udp_identifier(previous, pattern.span),
            udp_identifier(current, pattern.span),
            pattern.span);
    case frontend::VerilogUdpEdgeSymbol::Explicit:
        return udp_binary(
            "&&",
            transition(pattern.previous, pattern.current),
            udp_binary(
                "!==",
                udp_identifier(previous, pattern.span),
                udp_identifier(current, pattern.span),
                pattern.span),
            pattern.span);
    case frontend::VerilogUdpEdgeSymbol::None:
        return udp_level_condition(current, pattern.level, pattern.span);
    }
    return udp_literal('0', pattern.span);
}

frontend::Statement udp_assignment(
    const std::string& output,
    const frontend::VerilogUdpOutputSymbol value,
    const frontend::SourceSpan& span) {
    char literal = 'x';
    if (value == frontend::VerilogUdpOutputSymbol::Zero) {
        literal = '0';
    } else if (value == frontend::VerilogUdpOutputSymbol::One) {
        literal = '1';
    }
    frontend::Statement result;
    result.kind = frontend::StatementKind::Assignment;
    result.assignment_kind = frontend::AssignmentKind::Blocking;
    result.target = udp_identifier(output, span);
    result.value = udp_literal(literal, span);
    result.span = span;
    return result;
}

frontend::Process combinational_udp_process(
    const frontend::VerilogUdpDeclaration& declaration) {
    std::vector<frontend::Statement> tail{
        udp_assignment(
            declaration.output,
            frontend::VerilogUdpOutputSymbol::Unknown,
            declaration.span)};
    for (auto row = declaration.rows.rbegin();
         row != declaration.rows.rend(); ++row) {
        auto condition = udp_literal('1', row->span);
        for (std::size_t index = 0; index < row->inputs.size(); ++index) {
            condition = udp_binary(
                "&&",
                std::move(condition),
                udp_level_condition(
                    declaration.inputs[index],
                    row->inputs[index].level,
                    row->inputs[index].span),
                row->span);
        }
        frontend::Statement branch;
        branch.kind = frontend::StatementKind::If;
        branch.condition = std::move(condition);
        branch.statements.push_back(
            udp_assignment(declaration.output, row->output, row->span));
        branch.else_statements = std::move(tail);
        branch.span = row->span;
        tail.clear();
        tail.push_back(std::move(branch));
    }
    frontend::Process process;
    process.kind = frontend::ProcessKind::SystemVerilogAlwaysComb;
    process.name = "$udp";
    process.sensitivities.push_back(
        {frontend::EdgeKind::Any, "*", declaration.span, {}});
    process.statements = std::move(tail);
    process.span = declaration.span;
    return process;
}

frontend::Process sequential_udp_process(
    const frontend::VerilogUdpDeclaration& declaration) {
    frontend::Process process;
    process.kind = frontend::ProcessKind::SystemVerilogAlwaysComb;
    process.name = "$udp_state";
    for (std::size_t index = 0; index < declaration.inputs.size(); ++index) {
        process.sensitivities.push_back({
            frontend::EdgeKind::Any,
            declaration.inputs[index],
            declaration.span,
            {}});
    }

    std::vector<frontend::Statement> tail;
    for (auto row = declaration.rows.rbegin();
         row != declaration.rows.rend(); ++row) {
        auto condition = row->current_state
            ? udp_level_condition(
                declaration.output, *row->current_state, row->span)
            : udp_literal('1', row->span);
        for (std::size_t index = 0; index < row->inputs.size(); ++index) {
            condition = udp_binary(
                "&&",
                std::move(condition),
                udp_edge_condition(
                    "$udp_previous$" + std::to_string(index),
                    declaration.inputs[index],
                    row->inputs[index]),
                row->span);
        }
        frontend::Statement branch;
        branch.kind = frontend::StatementKind::If;
        branch.condition = std::move(condition);
        if (row->output != frontend::VerilogUdpOutputSymbol::NoChange) {
            branch.statements.push_back(
                udp_assignment(declaration.output, row->output, row->span));
        }
        branch.else_statements = std::move(tail);
        branch.span = row->span;
        tail.clear();
        tail.push_back(std::move(branch));
    }
    if (declaration.initial_output) {
        frontend::Statement initialize;
        initialize.kind = frontend::StatementKind::If;
        initialize.condition = udp_binary(
            "!==",
            udp_identifier("$udp_initialized", declaration.span),
            udp_literal('1', declaration.span),
            declaration.span);
        initialize.statements.push_back(udp_assignment(
            declaration.output,
            *declaration.initial_output,
            declaration.span));
        frontend::Statement mark;
        mark.kind = frontend::StatementKind::Assignment;
        mark.assignment_kind = frontend::AssignmentKind::Blocking;
        mark.target = udp_identifier("$udp_initialized", declaration.span);
        mark.value = udp_literal('1', declaration.span);
        mark.span = declaration.span;
        initialize.statements.push_back(std::move(mark));
        initialize.span = declaration.span;
        process.statements.push_back(std::move(initialize));
    }
    process.statements.insert(
        process.statements.end(),
        std::make_move_iterator(tail.begin()),
        std::make_move_iterator(tail.end()));
    for (std::size_t index = 0; index < declaration.inputs.size(); ++index) {
        frontend::Statement update;
        update.kind = frontend::StatementKind::Assignment;
        update.assignment_kind = frontend::AssignmentKind::Blocking;
        update.target = udp_identifier(
            "$udp_previous$" + std::to_string(index), declaration.span);
        update.value = udp_identifier(
            declaration.inputs[index], declaration.span);
        update.span = declaration.span;
        process.statements.push_back(std::move(update));
    }
    process.span = declaration.span;
    return process;
}

frontend::DesignUnit udp_profile(
    const frontend::VerilogUdpDeclaration& declaration,
    const std::optional<frontend::Delay>& propagation_delay,
    const std::optional<frontend::VerilogDriveStrength>& drive_strength) {
    frontend::DesignUnit result;
    result.kind = frontend::UnitKind::VerilogModule;
    result.language = declaration.language;
    result.library = declaration.library;
    result.name = declaration.name;
    result.time_unit = declaration.time_unit;
    result.time_precision = declaration.time_precision;
    result.span = declaration.span;

    const frontend::Type scalar{
        frontend::ValueDomain::Logic4, "wire", std::nullopt, false};
    result.ports.emplace_back(
        declaration.output,
        scalar,
        frontend::PortDirection::Output,
        true,
        declaration.span);
    for (const auto& input : declaration.inputs) {
        result.ports.emplace_back(
            input,
            scalar,
            frontend::PortDirection::Input,
            true,
            declaration.span);
    }
    result.signals.emplace_back(
        "$udp_value",
        frontend::Type{
            frontend::ValueDomain::Logic4,
            "logic",
            std::nullopt,
            false},
        frontend::PortDirection::Unknown,
        false,
        declaration.span);
    frontend::Statement driver;
    driver.kind = frontend::StatementKind::Assignment;
    driver.assignment_kind = frontend::AssignmentKind::Continuous;
    driver.target = udp_identifier(declaration.output, declaration.span);
    driver.value = udp_identifier("$udp_value", declaration.span);
    driver.delay = propagation_delay;
    driver.verilog_drive_strength = drive_strength;
    driver.span = declaration.span;
    result.concurrent_statements.push_back(std::move(driver));
    auto executable = declaration;
    executable.output = "$udp_value";
    if (!declaration.sequential) {
        result.processes.push_back(combinational_udp_process(executable));
    } else {
        if (declaration.initial_output) {
            result.signals.emplace_back(
                "$udp_initialized",
                frontend::Type{
                    frontend::ValueDomain::Logic4,
                    "logic",
                    std::nullopt,
                    false},
                frontend::PortDirection::Unknown,
                false,
                declaration.span);
        }
        for (std::size_t index = 0;
             index < declaration.inputs.size(); ++index) {
            result.signals.emplace_back(
                "$udp_previous$" + std::to_string(index),
                frontend::Type{
                    frontend::ValueDomain::Logic4,
                    "logic",
                    std::nullopt,
                    false},
                frontend::PortDirection::Unknown,
                false,
                declaration.span);
        }
        result.processes.push_back(sequential_udp_process(executable));
    }
    return result;
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

} // namespace

UdpTableId HierarchyBuilder::normalized_udp_table(
    const frontend::VerilogUdpDeclaration& declaration) {
    const auto library = declaration.library.empty()
        ? std::string{"work"}
        : declaration.library;
    const auto identity = "udp:" + library + "." + declaration.name;
    if (const auto found = udp_table_by_identity_.find(identity);
        found != udp_table_by_identity_.end()) {
        return found->second;
    }

    support::Sha256 digest;
    digest_field(digest, "fsim-udp-table-v1");
    digest_field(digest, identity);
    digest_number(digest, declaration.sequential);
    digest_field(digest, declaration.output);
    digest_number(
        digest,
        declaration.initial_output
            ? static_cast<unsigned>(*declaration.initial_output) + 1U
            : 0U);
    for (const auto& input : declaration.inputs) {
        digest_field(digest, input);
    }
    for (const auto& row : declaration.rows) {
        digest_number(
            digest,
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

    UdpTableInfo table;
    table.id = static_cast<UdpTableId>(design_.udp_tables_.size());
    table.identity = identity;
    table.digest = support::Sha256::hex(digest.finish());
    table.sequential = declaration.sequential;
    table.initial_output = declaration.initial_output;
    table.terminals.reserve(declaration.inputs.size() + 1);
    table.terminals.push_back(declaration.output);
    table.terminals.insert(
        table.terminals.end(),
        declaration.inputs.begin(),
        declaration.inputs.end());
    table.rows = declaration.rows;
    const auto id = table.id;
    design_.udp_tables_.push_back(std::move(table));
    udp_table_by_identity_.emplace(identity, id);
    return id;
}

void HierarchyBuilder::instantiate_udp(
    const frontend::VerilogUdpDeclaration& declaration,
    const frontend::Instance& instance,
    const std::string& path,
    const SignalMap& parent_signals,
    const StringMap& parent_strings,
    const std::unordered_set<StringObjectId>& read_only_strings,
    const ContainerMap& parent_containers,
    const std::unordered_set<std::string>& read_only_containers,
    const Binding* binding) {
    if (!instance.parameter_overrides.empty()) {
        report(
            "FSIM-ELAB-BIND-060",
            "UDP instance '" + path
                + "' cannot have parameter overrides",
            instance.span);
        return;
    }
    if (std::ranges::any_of(
            instance.connections,
            [](const frontend::PortConnection& connection) {
                return connection.port.has_value();
            })) {
        report(
            "FSIM-ELAB-BIND-061",
            "UDP instance '" + path
                + "' requires positional terminal connections",
            instance.span);
        return;
    }

    auto profile = udp_profile(
        declaration, instance.udp_delay, instance.drive_strength);
    const auto table_id = normalized_udp_table(declaration);
    const auto& table = design_.udp_tables_[table_id];
    auto aliases = connect_instance(
        instance,
        profile,
        path,
        parent_signals,
        parent_strings,
        read_only_strings,
        parent_containers,
        read_only_containers,
        binding,
        false);
    instantiate(
        profile,
        path,
        std::move(aliases.signals),
        std::move(aliases.strings),
        std::move(aliases.containers),
        std::move(aliases.read_only_signals),
        std::move(aliases.read_only_strings),
        {},
        {},
        {},
        {{"__udp", table.identity}, {"__udp_table", table.digest}},
        {});
}

} // namespace fsim::elaboration
