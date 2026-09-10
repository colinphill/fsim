// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_cross_inventory.hpp"

#include "fsim/frontend/coverage_limits.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {
namespace {

struct FactorBin {
    std::string operand;
    const SystemVerilogCoverageBin* bin { };
    std::string identity;
    bool excluded { };
};

std::optional<std::int64_t> scalar_number(std::string spelling)
{
    spelling.erase(
        std::remove(spelling.begin(), spelling.end(), '_'), spelling.end());
    if (spelling.find('\'') != std::string::npos) return std::nullopt;
    std::int64_t value { };
    const auto parsed = std::from_chars(
        spelling.data(), spelling.data() + spelling.size(), value);
    return parsed.ec == std::errc { }
            && parsed.ptr == spelling.data() + spelling.size()
        ? std::optional<std::int64_t> { value }
        : std::nullopt;
}

bool bin_intersects(
    const SystemVerilogCoverageBin& bin,
    const std::span<const Token> tokens)
{
    std::vector<std::int64_t> endpoints;
    for (const auto& token : tokens) {
        if (token.kind != TokenKind::Number) continue;
        const auto value = scalar_number(token.text);
        if (!value) return false;
        endpoints.push_back(*value);
    }
    if (endpoints.empty() || endpoints.size() > 2U) return false;
    const auto selected_left = endpoints.front();
    const auto selected_right = endpoints.back();
    const auto selected_lower = std::min(selected_left, selected_right);
    const auto selected_upper = std::max(selected_left, selected_right);
    return std::ranges::any_of(
        bin.values,
        [&](const SystemVerilogCoverageBinValue& value) {
            if (value.exact_value) {
                return *value.exact_value >= selected_lower
                    && *value.exact_value <= selected_upper;
            }
            if (value.range_left && value.range_right) {
                const auto lower = std::min(
                    *value.range_left, *value.range_right);
                const auto upper = std::max(
                    *value.range_left, *value.range_right);
                return lower <= selected_upper && upper >= selected_lower;
            }
            return false;
        });
}

std::span<const Token> strip_parentheses(std::span<const Token> tokens)
{
    while (tokens.size() >= 2U
        && tokens.front().kind == TokenKind::LeftParen
        && tokens.back().kind == TokenKind::RightParen) {
        int depth { };
        bool complete { true };
        for (std::size_t index = 0U; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftParen) ++depth;
            if (tokens[index].kind == TokenKind::RightParen) --depth;
            if (depth == 0 && index + 1U != tokens.size()) {
                complete = false;
                break;
            }
        }
        if (!complete) break;
        tokens = tokens.subspan(1U, tokens.size() - 2U);
    }
    return tokens;
}

std::optional<std::size_t> top_level_operator(
    const std::span<const Token> tokens,
    const TokenKind kind)
{
    int depth { };
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        if (tokens[index].kind == TokenKind::LeftParen) ++depth;
        if (tokens[index].kind == TokenKind::RightParen) --depth;
        if (depth == 0 && tokens[index].kind == kind) return index;
    }
    return std::nullopt;
}

bool selection_matches(
    std::span<const Token> tokens,
    const SystemVerilogCoverageDeclaration& cross,
    const std::span<const FactorBin> tuple)
{
    tokens = strip_parentheses(tokens);
    if (tokens.empty()) return false;
    if (const auto operation = top_level_operator(tokens, TokenKind::OrOr)) {
        return selection_matches(tokens.first(*operation), cross, tuple)
            || selection_matches(tokens.subspan(*operation + 1U), cross, tuple);
    }
    if (const auto operation = top_level_operator(tokens, TokenKind::AndAnd)) {
        return selection_matches(tokens.first(*operation), cross, tuple)
            && selection_matches(tokens.subspan(*operation + 1U), cross, tuple);
    }
    if (tokens.front().text == "!") {
        return !selection_matches(tokens.subspan(1U), cross, tuple);
    }
    if (tokens.size() == 1U && tokens.front().text == cross.name) return true;
    if (tokens.size() < 4U || tokens.front().text != "binsof"
        || tokens[1].kind != TokenKind::LeftParen
        || tokens[2].kind != TokenKind::Identifier) {
        return false;
    }
    std::size_t right = 2U;
    int depth { 1 };
    for (; right < tokens.size(); ++right) {
        if (tokens[right].kind == TokenKind::LeftParen) ++depth;
        if (tokens[right].kind == TokenKind::RightParen) --depth;
        if (depth == 0) break;
    }
    if (right >= tokens.size()) return false;
    const auto factor = std::ranges::find(
        tuple, tokens[2].text, &FactorBin::operand);
    if (factor == tuple.end()) return false;
    std::optional<std::string_view> selected_name;
    if (right >= 5U && tokens[3].kind == TokenKind::Dot
        && tokens[4].kind == TokenKind::Identifier) {
        selected_name = tokens[4].text;
    }
    auto suffix = right + 1U;
    if (!selected_name && suffix + 1U < tokens.size()
        && tokens[suffix].kind == TokenKind::Dot
        && tokens[suffix + 1U].kind == TokenKind::Identifier) {
        selected_name = tokens[suffix + 1U].text;
        suffix += 2U;
    }
    if (selected_name
        && factor->bin->name != *selected_name
        && factor->bin->source_name != *selected_name) {
        return false;
    }
    if (suffix == tokens.size()) return true;
    return suffix + 1U < tokens.size()
        && tokens[suffix].text == "intersect"
        && bin_intersects(*factor->bin, tokens.subspan(suffix + 1U));
}

std::string factor_identity(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& coverpoint,
    const SystemVerilogCoverageBin& bin)
{
    const auto& origin = coverpoint.origin_covergroup_identity.empty()
        ? declaration.canonical_identity
        : coverpoint.origin_covergroup_identity;
    return origin + "::" + coverpoint.name + "." + bin.name;
}

std::string cross_identity(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& cross,
    const SystemVerilogCoverageBin* bin,
    const std::span<const FactorBin> tuple)
{
    const auto& origin = cross.origin_covergroup_identity.empty()
        ? declaration.canonical_identity
        : cross.origin_covergroup_identity;
    auto result = origin + "::" + cross.name;
    if (bin != nullptr) return result + "." + bin->name;
    result += "<";
    for (std::size_t index = 0U; index < tuple.size(); ++index) {
        if (index != 0U) result += ",";
        result += tuple[index].identity;
    }
    return result + ">";
}

} // namespace

bool initialize_systemverilog_cross_inventory(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::vector<Diagnostic>& diagnostics)
{
    if (instance.cross_inventory_initialized) return true;
    auto states = instance.cross_bin_state;
    for (const auto& cross : declaration.coverage_declarations) {
        if (cross.kind != SystemVerilogCoverageDeclarationKind::Cross) continue;
        for (const auto& bin : cross.bins) {
            SystemVerilogCoverageCrossBinState state;
            state.coverage_declaration_index = cross.declaration_index;
            state.bin_declaration_index = bin.declaration_index;
            state.identity = cross_identity(declaration, cross, &bin, { });
            state.weight = bin.weight;
            state.goal = bin.goal;
            state.at_least = bin.at_least;
            state.excluded = bin.kind != SystemVerilogCoverageBinKind::Regular
                || bin.weight == 0U;
            states.push_back(std::move(state));
        }
        std::vector<std::vector<FactorBin>> factors;
        std::size_t work { };
        bool finite = !cross.cross_operands.empty();
        for (const auto& operand : cross.cross_operands) {
            if (!operand.resolved_declaration_index
                || *operand.resolved_declaration_index
                    >= declaration.coverage_declarations.size()) {
                finite = false;
                break;
            }
            const auto& coverpoint = declaration.coverage_declarations[
                *operand.resolved_declaration_index];
            std::vector<FactorBin> bins;
            for (const auto& bin : coverpoint.bins) {
                if (bin.selection != SystemVerilogCoverageBinSelection::Explicit
                    || !bin.transitions.empty()) {
                    finite = false;
                    break;
                }
                bins.push_back({ operand.name, &bin,
                    factor_identity(declaration, coverpoint, bin),
                    bin.kind != SystemVerilogCoverageBinKind::Regular
                        || bin.weight == 0U });
            }
            if (!finite || bins.empty()) {
                finite = false;
                break;
            }
            if (work == 0U) work = 1U;
            if (bins.size() > kSystemVerilogCoverageMaximumWork / work) {
                finite = false;
                work = kSystemVerilogCoverageMaximumWork + 1U;
                break;
            }
            work *= bins.size();
            factors.push_back(std::move(bins));
        }
        if (!finite) continue;
        std::vector<FactorBin> tuple(factors.size());
        const auto visit = [&](const auto& self, const std::size_t index) -> bool {
            if (index != factors.size()) {
                for (const auto& factor : factors[index]) {
                    tuple[index] = factor;
                    if (!self(self, index + 1U)) return false;
                }
                return true;
            }
            const auto explicit_match = std::ranges::any_of(
                cross.bins,
                [&](const SystemVerilogCoverageBin& bin) {
                    return selection_matches(
                        bin.cross_selection_tokens, cross, tuple);
                });
            if (explicit_match) return true;
            if (!cross.bins.empty()
                && !declaration.effective_cross_retain_auto_bins) {
                return true;
            }
            SystemVerilogCoverageCrossBinState state;
            state.coverage_declaration_index = cross.declaration_index;
            state.identity = cross_identity(declaration, cross, nullptr, tuple);
            for (const auto& factor : tuple) {
                state.operand_bin_identities.push_back(factor.identity);
                state.excluded = state.excluded || factor.excluded;
            }
            state.weight = cross.effective_weight;
            state.goal = cross.effective_goal;
            state.at_least = cross.effective_at_least;
            state.excluded = state.excluded || state.weight == 0U;
            states.push_back(std::move(state));
            return states.size() <= kSystemVerilogCoverageMaximumStateRecords;
        };
        if (!visit(visit, 0U)) {
            diagnostics.push_back({ DiagnosticSeverity::Error,
                "FSIM-SV-COV-005",
                "coverage cross inventory exceeds the governed state budget",
                cross.span,
                { } });
            return false;
        }
    }
    std::ranges::sort(states, { }, &SystemVerilogCoverageCrossBinState::identity);
    const auto duplicate = std::ranges::adjacent_find(
        states, { }, &SystemVerilogCoverageCrossBinState::identity);
    if (duplicate != states.end()
        || states.size() > kSystemVerilogCoverageMaximumStateRecords) {
        diagnostics.push_back({ DiagnosticSeverity::Error,
            "FSIM-SV-COV-005",
            "coverage cross inventory is duplicate or exceeds the governed state budget",
            declaration.span,
            { } });
        return false;
    }
    instance.cross_bin_state = std::move(states);
    instance.cross_inventory_initialized = true;
    return true;
}

} // namespace fsim::frontend
