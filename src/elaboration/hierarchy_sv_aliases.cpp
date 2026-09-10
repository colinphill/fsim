// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <numeric>
#include <ranges>

namespace fsim::elaboration {
namespace {
    using namespace elaboration_detail;

    struct AliasSegment {
        std::string name;
        const frontend::SignalDeclaration* declaration { };
        std::uint64_t offset { };
        std::uint64_t width { };
    };

    bool systemverilog_net(const frontend::Type& type)
    {
        if (!type.systemverilog_net_type.empty()) {
            return true;
        }
        static constexpr std::array net_types {
            std::string_view { "wire" }, std::string_view { "tri" },
            std::string_view { "tri0" }, std::string_view { "tri1" },
            std::string_view { "wand" }, std::string_view { "triand" },
            std::string_view { "wor" }, std::string_view { "trior" },
            std::string_view { "trireg" }, std::string_view { "uwire" },
            std::string_view { "supply0" }, std::string_view { "supply1" }
        };
        return std::ranges::find(net_types, type.spelling) != net_types.end();
    }

    std::string_view net_identity(const frontend::Type& type)
    {
        return type.systemverilog_net_type.empty()
            ? std::string_view { type.spelling }
            : std::string_view { type.systemverilog_net_type };
    }

    bool same_packed_range(
        const std::optional<frontend::PackedRange>& left,
        const std::optional<frontend::PackedRange>& right)
    {
        if (left.has_value() != right.has_value()) {
            return false;
        }
        return !left
            || (left->left == right->left
                && left->right == right->right
                && left->descending == right->descending);
    }

    bool alias_net_compatible(
        const frontend::SignalDeclaration& left,
        const frontend::SignalDeclaration& right)
    {
        return net_identity(left.type) == net_identity(right.type)
            && left.type.domain == right.type.domain
            && left.type.systemverilog_scalar
            == right.type.systemverilog_scalar
            && left.type.systemverilog_resolution_function
            == right.type.systemverilog_resolution_function
            && left.type.is_signed == right.type.is_signed
            && left.type.nominal_type == right.type.nominal_type;
    }

    bool whole_alias_compatible(
        const frontend::SignalDeclaration& left,
        const frontend::SignalDeclaration& right)
    {
        return alias_net_compatible(left, right)
            && left.type.width() == right.type.width()
            && same_packed_range(
                left.type.packed_range, right.type.packed_range);
    }

} // namespace

HierarchyBuilder::SystemVerilogAliasPlan
HierarchyBuilder::apply_systemverilog_aliases(
    const DesignUnit& unit,
    const std::span<const frontend::SignalDeclaration> ports,
    const SystemVerilogConstantEnvironment& integral_environment,
    const ConstantEnvironment& fallback_environment,
    const bool compile_connections,
    const bool diagnose)
{
    std::unordered_map<std::string,
        const frontend::SignalDeclaration*>
        declarations;
    for (const auto& port : ports) {
        declarations.try_emplace(port.name, &port);
    }
    for (const auto& signal : unit.signals) {
        declarations.try_emplace(signal.name, &signal);
    }

    SystemVerilogAliasPlan result;
    const auto emit = [&](std::string code, std::string message,
                          const frontend::SourceSpan& source) {
        if (diagnose) {
            report(std::move(code), std::move(message), source);
        }
    };
    const auto integer = [&](const frontend::Expression& expression)
        -> std::optional<std::int64_t> {
        std::string error;
        const auto value = evaluate_systemverilog_constant_expression(
            expression,
            integral_environment,
            fallback_environment,
            error);
        return value ? value->integer_value()
                     : std::optional<std::int64_t> { };
    };
    const auto declaration_for = [&](const frontend::Expression& expression)
        -> const frontend::SignalDeclaration* {
        const auto found = declarations.find(expression.text);
        return found == declarations.end() ? nullptr : found->second;
    };
    const auto append_selection = [&](
                                      const frontend::Expression& expression,
                                      const frontend::Expression& base,
                                      const std::int64_t right,
                                      const std::uint64_t selected_width,
                                      std::vector<AliasSegment>& segments)
        -> bool {
        const auto* declaration = declaration_for(base);
        if (declaration == nullptr) {
            emit(
                "FSIM-ELAB-SVALIAS-002",
                "unknown SystemVerilog alias terminal '" + base.text + "'",
                expression.span);
            return false;
        }
        const auto width = declaration->type.width();
        if (!systemverilog_net(declaration->type) || !width || *width == 0) {
            emit(
                "FSIM-ELAB-SVALIAS-001",
                "SystemVerilog alias terminals must be statically selected packed nets",
                expression.span);
            return false;
        }
        const auto range = declaration->type.packed_range.value_or(
            frontend::PackedRange {
                static_cast<std::int64_t>(*width - 1U), 0, true });
        const auto lower = std::min(range.left, range.right);
        const auto upper = std::max(range.left, range.right);
        if (right < lower || right > upper) {
            emit(
                "FSIM-ELAB-SVALIAS-001",
                "SystemVerilog alias selection is outside its packed net",
                expression.span);
            return false;
        }
        const auto offset = index_distance(right, range.right);
        if (selected_width == 0 || offset > *width
            || selected_width > *width - offset) {
            emit(
                "FSIM-ELAB-SVALIAS-001",
                "SystemVerilog alias selection is outside its packed net",
                expression.span);
            return false;
        }
        segments.push_back(
            { declaration->name, declaration, offset, selected_width });
        return true;
    };
    const auto flatten = [&](const auto& self,
                             const frontend::Expression& expression,
                             std::vector<AliasSegment>& segments) -> bool {
        if (expression.kind == frontend::ExpressionKind::Concatenation) {
            bool valid = !expression.operands.empty();
            for (auto operand = expression.operands.rbegin();
                operand != expression.operands.rend(); ++operand) {
                valid = self(self, *operand, segments) && valid;
            }
            if (!valid && expression.operands.empty()) {
                emit(
                    "FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias concatenations cannot be empty",
                    expression.span);
            }
            return valid;
        }
        if (expression.kind == frontend::ExpressionKind::Identifier) {
            const auto* declaration = declaration_for(expression);
            if (declaration == nullptr) {
                emit(
                    "FSIM-ELAB-SVALIAS-002",
                    "unknown SystemVerilog alias terminal '"
                        + expression.text + "'",
                    expression.span);
                return false;
            }
            const auto width = declaration->type.width();
            if (!systemverilog_net(declaration->type)
                || !width || *width == 0) {
                emit(
                    "FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias terminals must be packed nets, not variables",
                    expression.span);
                return false;
            }
            segments.push_back(
                { declaration->name, declaration, 0, *width });
            return true;
        }
        if (expression.kind == frontend::ExpressionKind::Index
            && expression.operands.size() == 2
            && expression.operands[0].kind
                == frontend::ExpressionKind::Identifier) {
            const auto selected = integer(expression.operands[1]);
            if (!selected) {
                emit(
                    "FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias indices must be locally constant",
                    expression.span);
                return false;
            }
            return append_selection(
                expression, expression.operands[0], *selected, 1, segments);
        }
        if (expression.kind == frontend::ExpressionKind::Slice
            && expression.operands.size() == 3
            && expression.operands[0].kind
                == frontend::ExpressionKind::Identifier) {
            const auto* declaration = declaration_for(expression.operands[0]);
            const auto source_width = declaration
                ? declaration->type.width()
                : std::optional<std::size_t> { };
            if (declaration == nullptr || !source_width
                || *source_width == 0) {
                return append_selection(
                    expression, expression.operands[0], 0, 0, segments);
            }
            const auto range = declaration->type.packed_range.value_or(
                frontend::PackedRange {
                    static_cast<std::int64_t>(*source_width - 1U), 0, true });
            const auto first = integer(expression.operands[1]);
            const auto second = integer(expression.operands[2]);
            if (!first || !second) {
                emit(
                    "FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias part-selects must be locally constant",
                    expression.span);
                return false;
            }
            std::int64_t left { };
            std::int64_t right { };
            std::uint64_t width { };
            if (expression.text == "+:" || expression.text == "-:") {
                if (*second <= 0) {
                    emit(
                        "FSIM-ELAB-SVALIAS-001",
                        "SystemVerilog alias indexed part-select width must be positive",
                        expression.span);
                    return false;
                }
                const auto distance = static_cast<std::uint64_t>(*second - 1);
                if (distance
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    emit(
                        "FSIM-ELAB-SVALIAS-001",
                        "SystemVerilog alias indexed part-select is outside its packed net",
                        expression.span);
                    return false;
                }
                if (expression.text == "+:") {
                    if (*first > std::numeric_limits<std::int64_t>::max()
                            - static_cast<std::int64_t>(distance)) {
                        emit(
                            "FSIM-ELAB-SVALIAS-001",
                            "SystemVerilog alias indexed part-select is outside its packed net",
                            expression.span);
                        return false;
                    }
                    const auto lower = *first;
                    const auto upper = *first
                        + static_cast<std::int64_t>(distance);
                    left = range.left >= range.right ? upper : lower;
                    right = range.left >= range.right ? lower : upper;
                } else {
                    if (*first < std::numeric_limits<std::int64_t>::min()
                            + static_cast<std::int64_t>(distance)) {
                        emit(
                            "FSIM-ELAB-SVALIAS-001",
                            "SystemVerilog alias indexed part-select is outside its packed net",
                            expression.span);
                        return false;
                    }
                    const auto lower = *first
                        - static_cast<std::int64_t>(distance);
                    const auto upper = *first;
                    left = range.left >= range.right ? upper : lower;
                    right = range.left >= range.right ? lower : upper;
                }
                width = static_cast<std::uint64_t>(*second);
            } else {
                left = *first;
                right = *second;
                if (left != right
                    && (left >= right) != (range.left >= range.right)) {
                    emit(
                        "FSIM-ELAB-SVALIAS-001",
                        "SystemVerilog alias part-select direction does not match its packed net",
                        expression.span);
                    return false;
                }
                width = index_distance(left, right) + 1U;
            }
            (void)left;
            return append_selection(
                expression, expression.operands[0], right, width, segments);
        }
        emit(
            "FSIM-ELAB-SVALIAS-001",
            "SystemVerilog alias terminals must be static packed net lvalues",
            expression.span);
        return false;
    };

    for (const auto& alias : unit.systemverilog_aliases) {
        std::vector<std::vector<AliasSegment>> terminals;
        terminals.reserve(alias.terminals.size());
        bool valid = true;
        for (const auto& terminal : alias.terminals) {
            auto& flattened = terminals.emplace_back();
            valid = flatten(flatten, terminal, flattened) && valid;
        }
        if (!valid || terminals.size() < 2) {
            continue;
        }
        const auto total_width = [](const std::vector<AliasSegment>& value) {
            return std::accumulate(
                value.begin(), value.end(), std::uint64_t { },
                [](const std::uint64_t total, const AliasSegment& segment) {
                    if (segment.width
                        > std::numeric_limits<std::uint64_t>::max() - total) {
                        throw std::overflow_error {
                            "SystemVerilog alias terminal width overflow"
                        };
                    }
                    return total + segment.width;
                });
        };
        const auto width = total_width(terminals.front());
        for (std::size_t index = 1; index < terminals.size(); ++index) {
            if (total_width(terminals[index]) != width) {
                emit(
                    "FSIM-ELAB-SVALIAS-003",
                    "SystemVerilog alias terminals have different bit lengths",
                    alias.terminals[index].span);
                valid = false;
            }
        }
        if (!valid) {
            continue;
        }

        const bool whole = std::ranges::all_of(
            alias.terminals,
            [](const frontend::Expression& terminal) {
                return terminal.kind == frontend::ExpressionKind::Identifier;
            });
        if (whole) {
            const auto* canonical = terminals.front().front().declaration;
            const bool exact = std::ranges::all_of(
                terminals | std::views::drop(1),
                [&](const std::vector<AliasSegment>& terminal) {
                    return whole_alias_compatible(
                        *canonical, *terminal.front().declaration);
                });
            if (exact) {
                std::vector<std::string> names;
                names.reserve(terminals.size());
                for (const auto& terminal : terminals) {
                    names.push_back(terminal.front().name);
                }
                std::vector<std::size_t> matching_groups;
                for (std::size_t group = 0;
                    group < result.whole_groups.size(); ++group) {
                    if (std::ranges::any_of(names, [&](const auto& name) {
                            return std::ranges::find(
                                       result.whole_groups[group], name)
                                != result.whole_groups[group].end();
                        })) {
                        matching_groups.push_back(group);
                    }
                }
                if (matching_groups.empty()) {
                    result.whole_groups.push_back(std::move(names));
                } else {
                    auto& merged = result.whole_groups[matching_groups.front()];
                    for (const auto& name : names) {
                        if (std::ranges::find(merged, name) == merged.end()) {
                            merged.push_back(name);
                        }
                    }
                    for (auto index = matching_groups.size(); index-- > 1;) {
                        const auto group = matching_groups[index];
                        for (const auto& name : result.whole_groups[group]) {
                            if (std::ranges::find(merged, name) == merged.end()) {
                                merged.push_back(name);
                            }
                        }
                        result.whole_groups.erase(
                            result.whole_groups.begin()
                            + static_cast<std::ptrdiff_t>(group));
                    }
                }
                continue;
            }
        }
        if (!compile_connections) {
            continue;
        }

        for (std::size_t terminal_index = 1;
            terminal_index < terminals.size(); ++terminal_index) {
            const auto& left = terminals.front();
            const auto& right = terminals[terminal_index];
            std::size_t left_index { };
            std::size_t right_index { };
            std::uint64_t left_consumed { };
            std::uint64_t right_consumed { };
            while (left_index < left.size() && right_index < right.size()) {
                const auto& left_segment = left[left_index];
                const auto& right_segment = right[right_index];
                if (!alias_net_compatible(
                        *left_segment.declaration,
                        *right_segment.declaration)) {
                    emit(
                        "FSIM-ELAB-SVALIAS-003",
                        "SystemVerilog alias terminals do not have equivalent net types",
                        alias.terminals[terminal_index].span);
                    valid = false;
                    break;
                }
                const auto count = std::min(
                    left_segment.width - left_consumed,
                    right_segment.width - right_consumed);
                SystemVerilogAliasConnection connection {
                    left_segment.name,
                    left_segment.offset + left_consumed,
                    right_segment.name,
                    right_segment.offset + right_consumed,
                    count,
                    alias.span
                };
                if (!result.connections.empty()) {
                    auto& previous = result.connections.back();
                    if (previous.left == connection.left
                        && previous.right == connection.right
                        && previous.left_offset + previous.width
                            == connection.left_offset
                        && previous.right_offset + previous.width
                            == connection.right_offset) {
                        previous.width += connection.width;
                    } else {
                        result.connections.push_back(std::move(connection));
                    }
                } else {
                    result.connections.push_back(std::move(connection));
                }
                left_consumed += count;
                right_consumed += count;
                if (left_consumed == left_segment.width) {
                    ++left_index;
                    left_consumed = 0;
                }
                if (right_consumed == right_segment.width) {
                    ++right_index;
                    right_consumed = 0;
                }
            }
            if (!valid) {
                break;
            }
        }
    }
    return result;
}

void HierarchyBuilder::add_systemverilog_alias_connections(
    const SystemVerilogAliasPlan& plan,
    const std::string_view path,
    const SignalMap& signals,
    SpecializationInfo& specialization)
{
    std::size_t ordinal { };
    for (const auto& connection : plan.connections) {
        const auto left = signals.find(connection.left);
        const auto right = signals.find(connection.right);
        if (left == signals.end() || right == signals.end()) {
            report(
                "FSIM-ELAB-SVALIAS-002",
                "cannot bind a SystemVerilog alias connection to its packed nets",
                connection.source);
            continue;
        }
        if (left->second == right->second
            && connection.left_offset == connection.right_offset) {
            continue;
        }
        const auto process_index = design_.processes_.size();
        const auto process_id = static_cast<runtime::simir::ProcessId>(
            process_index);
        if (static_cast<std::size_t>(process_id) != process_index) {
            throw std::length_error { "too many SimIR processes" };
        }
        runtime::simir::Process process;
        process.id = process_id;
        process.name = std::string { path } + ".$alias_"
            + std::to_string(ordinal++);
        process.switch_source = left->second;
        process.switch_target = right->second;
        process.switch_source_offset = connection.left_offset;
        process.switch_target_offset = connection.right_offset;
        process.switch_width = connection.width;
        process.switch_bidirectional = true;
        process.initialize = false;
        process.operations.emplace_back(runtime::simir::Halt { });
        specialization.processes.push_back(process.id);
        design_.processes_.push_back(std::move(process));
    }
}

} // namespace fsim::elaboration
