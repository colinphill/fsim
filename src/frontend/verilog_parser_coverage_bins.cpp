// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_coverage_internal.hpp"

#include "fsim/frontend/coverage_sampling.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <span>
#include <unordered_set>
#include <utility>

namespace fsim::frontend {

using namespace coverage_parser_detail;

void VerilogParser::structure_coverpoint_bins(
    SystemVerilogCoverageDeclaration& declaration)
{
    constexpr std::size_t maximum_expanded_bins = 65'536U;
    const std::span<const Token> body { declaration.body_tokens };
    std::unordered_set<std::string> names;
    bool saw_bin_declaration { };
    std::size_t next_bin_index { };
    for (auto statement : split_top_level(body, TokenKind::Semicolon)) {
        if (statement.empty())
            continue;
        const bool wildcard_declaration = statement.front().text == "wildcard";
        const std::size_t keyword_index = wildcard_declaration ? 1U : 0U;
        if (keyword_index >= statement.size())
            continue;
        const auto keyword = statement[keyword_index].text;
        if (keyword != "bins" && keyword != "ignore_bins"
            && keyword != "illegal_bins") {
            continue;
        }
        saw_bin_declaration = true;
        const auto name_index = keyword_index + 1U;
        const auto assignment = find_top_level(statement, TokenKind::Assign);
        if (name_index >= statement.size()
            || statement[name_index].kind != TokenKind::Identifier
            || !assignment || *assignment <= name_index
            || *assignment + 1U == statement.size()) {
            error(
                statement.front(),
                "FSIM-SV-PARSE-319",
                "a coverpoint bin requires 'bins name = selection'");
            continue;
        }
        const auto source_name = statement[name_index].text;
        if (!names.insert(source_name).second) {
            error(
                statement[name_index],
                "FSIM-SV-SEM-212",
                "duplicate coverpoint bin name '" + source_name + "'");
            continue;
        }

        bool arrayed { };
        std::optional<std::size_t> declared_array_size;
        const auto declarator = std::span<const Token> { statement }.subspan(
            name_index + 1U, *assignment - name_index - 1U);
        if (!declarator.empty()) {
            if (declarator.front().kind != TokenKind::LeftBracket
                || declarator.back().kind != TokenKind::RightBracket) {
                error(
                    statement[name_index],
                    "FSIM-SV-PARSE-320",
                    "a bin array declarator must be empty or contain a positive size");
                continue;
            }
            arrayed = true;
            const auto extent = declarator.subspan(1U, declarator.size() - 2U);
            if (!extent.empty()) {
                const auto literal = coverage_literal(extent);
                if (!literal || literal->wildcard || !literal->exact
                    || *literal->exact <= 0
                    || static_cast<std::uint64_t>(*literal->exact)
                        > maximum_expanded_bins) {
                    error(
                        declarator.front(),
                        "FSIM-SV-SEM-213",
                        "a bin array size must be within 1..65536");
                    continue;
                }
                declared_array_size = static_cast<std::size_t>(*literal->exact);
            }
        }

        auto rhs = std::span<const Token> { statement }.subspan(
            *assignment + 1U);
        SystemVerilogCoverageBin bin;
        bin.kind = keyword == "ignore_bins"
            ? SystemVerilogCoverageBinKind::Ignore
            : keyword == "illegal_bins"
            ? SystemVerilogCoverageBinKind::Illegal
            : SystemVerilogCoverageBinKind::Regular;
        bin.name = source_name;
        bin.source_name = source_name;
        bin.name_token = statement[name_index];
        bin.declared_array_size = declared_array_size;
        bin.wildcard = wildcard_declaration;
        bin.weight = declaration.effective_weight;
        bin.goal = declaration.effective_goal;
        bin.at_least = declaration.effective_at_least;
        bin.span = span_from(statement.front(), statement.back());
        const auto iff = find_top_level_keyword(rhs, "iff");
        if (iff) {
            const auto left = *iff + 1U;
            if (*iff == 0U || left >= rhs.size()
                || rhs[left].kind != TokenKind::LeftParen) {
                error(
                    rhs[*iff],
                    "FSIM-SV-PARSE-322",
                    "a bin iff guard requires a nonempty parenthesized expression");
                continue;
            }
            const auto right = matching_right_parenthesis(rhs, left);
            if (!right || *right + 1U != rhs.size() || *right == left + 1U) {
                error(
                    rhs[*iff],
                    "FSIM-SV-PARSE-322",
                    "a bin iff guard requires a nonempty balanced expression");
                continue;
            }
            bin.iff_tokens.assign(
                rhs.begin() + static_cast<std::ptrdiff_t>(left + 1U),
                rhs.begin() + static_cast<std::ptrdiff_t>(*right));
            bin.iff_span = span_from(rhs[*iff], rhs[*right]);
            rhs = rhs.first(*iff);
        }
        if (const auto matches = find_top_level_keyword(rhs, "matches")) {
            error(
                rhs[*matches],
                "FSIM-SV-PARSE-320",
                "matches is only valid in a cross-bin selection expression");
            continue;
        }
        if (const auto with = find_top_level_keyword(rhs, "with")) {
            const auto left = *with + 1U;
            if (*with == 0U || left >= rhs.size()
                || rhs[left].kind != TokenKind::LeftParen) {
                error(
                    rhs[*with],
                    "FSIM-SV-PARSE-320",
                    "a coverpoint with clause requires a nonempty parenthesized expression");
                continue;
            }
            const auto right = matching_right_parenthesis(rhs, left);
            if (!right || *right + 1U != rhs.size() || *right == left + 1U) {
                error(
                    rhs[*with],
                    "FSIM-SV-PARSE-320",
                    "a coverpoint with clause requires one balanced expression");
                continue;
            }
            bin.with_tokens.assign(
                rhs.begin() + static_cast<std::ptrdiff_t>(left + 1U),
                rhs.begin() + static_cast<std::ptrdiff_t>(*right));
            bin.with_span = span_from(rhs[*with], rhs[*right]);
            rhs = rhs.first(*with);
        }
        if (rhs.size() == 1U && rhs.front().text == "default") {
            if (arrayed || wildcard_declaration) {
                error(
                    rhs.front(),
                    "FSIM-SV-PARSE-320",
                    "default bins cannot be wildcard or arrayed");
                continue;
            }
            bin.selection = SystemVerilogCoverageBinSelection::Default;
        } else if (rhs.size() == 1U
            && rhs.front().kind == TokenKind::Identifier
            && rhs.front().text == declaration.name
            && !bin.with_tokens.empty()) {
            // The coverpoint name denotes its complete value domain. The with
            // predicate is evaluated against the sampled candidate as `item`.
        } else if (
            rhs.size() == 2U && rhs[0].text == "default"
            && rhs[1].text == "sequence") {
            if (arrayed || wildcard_declaration) {
                error(
                    rhs.front(),
                    "FSIM-SV-PARSE-322",
                    "default sequence bins cannot be wildcard or arrayed");
                continue;
            }
            bin.selection = SystemVerilogCoverageBinSelection::DefaultSequence;
        } else if (rhs.front().kind == TokenKind::LeftParen) {
            const auto transitions = structure_transition_sequences(
                rhs, wildcard_declaration);
            if (!transitions) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-321",
                    "a transition bin requires balanced bounded sequences and repetitions");
                continue;
            }
            bin.transitions = *transitions;
        } else if (
            rhs.size() >= 3U && rhs.front().kind == TokenKind::LeftBrace
            && rhs.back().kind == TokenKind::RightBrace) {
            const auto right = matching_right_brace(rhs, 0U);
            if (!right || *right + 1U != rhs.size())
                continue;
            auto values = split_top_level(
                rhs.subspan(1U, rhs.size() - 2U), TokenKind::Comma);
            if (values.empty()
                || std::ranges::any_of(
                    values,
                    [](const std::vector<Token>& value) {
                        return value.empty();
                    })) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-319",
                    "an explicit coverpoint bin requires nonempty values");
                continue;
            }
            bool malformed { };
            for (auto& value_tokens : values) {
                const auto value = coverage_bin_value(
                    value_tokens, wildcard_declaration);
                if (!value) {
                    malformed = true;
                    break;
                }
                bin.values.push_back(*value);
            }
            if (malformed) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-320",
                    "a bin value must be a bounded scalar, range, or wildcard literal");
                continue;
            }
        } else {
            error(
                statement.front(),
                "FSIM-SV-PARSE-320",
                "a bin selection must be default, a braced value set, or transitions");
            continue;
        }

        const bool real_coverpoint
            = declaration.sampled_scalar_kind
                == SystemVerilogScalarKind::ShortReal
            || declaration.sampled_scalar_kind
                == SystemVerilogScalarKind::Real
            || declaration.sampled_scalar_kind
                == SystemVerilogScalarKind::Realtime;
        const bool real_values = std::ranges::any_of(
            bin.values,
            [](const SystemVerilogCoverageBinValue& value) {
                return value.exact_real_bits
                    || value.range_left_real_bits
                    || value.range_right_real_bits;
            });
        if (real_coverpoint
            && (bin.selection
                    != SystemVerilogCoverageBinSelection::Explicit
                || bin.wildcard || !bin.transitions.empty())) {
            error(
                statement.front(),
                "FSIM-SV-SEM-266",
                "real-valued coverpoints require explicit non-wildcard scalar or interval bins");
            continue;
        }
        if (real_values && !real_coverpoint) {
            error(
                statement.front(),
                "FSIM-SV-SEM-266",
                "real-valued bin endpoints require a real-valued coverpoint");
            continue;
        }

        if (!arrayed) {
            bin.declaration_index = next_bin_index++;
            declaration.bins.push_back(std::move(bin));
            continue;
        }

        if (!bin.transitions.empty()) {
            const auto bin_count = declared_array_size.value_or(
                bin.transitions.size());
            if (bin_count == 0U || bin_count > maximum_expanded_bins
                || bin.transitions.size() > maximum_expanded_bins
                || bin_count > bin.transitions.size()) {
                error(
                    statement[name_index],
                    "FSIM-SV-SEM-214",
                    "transition array expansion exceeds 65536 sequences or creates empty bins");
                continue;
            }
            std::size_t sequence_offset { };
            for (std::size_t array_index = 0; array_index < bin_count;
                ++array_index) {
                const auto remaining_sequences = bin.transitions.size() - sequence_offset;
                const auto remaining_bins = bin_count - array_index;
                const auto sequence_count = (remaining_sequences + remaining_bins - 1U) / remaining_bins;
                auto expanded = bin;
                expanded.name = source_name + "[" + std::to_string(array_index) + "]";
                expanded.array_index = array_index;
                expanded.declaration_index = next_bin_index++;
                expanded.transitions.assign(
                    bin.transitions.begin()
                        + static_cast<std::ptrdiff_t>(sequence_offset),
                    bin.transitions.begin()
                        + static_cast<std::ptrdiff_t>(
                            sequence_offset + sequence_count));
                sequence_offset += sequence_count;
                declaration.bins.push_back(std::move(expanded));
            }
            continue;
        }

        if (real_coverpoint) {
            if (bin.values.size() != 1U
                || !bin.values.front().range_left_real_bits
                || !bin.values.front().range_right_real_bits) {
                error(
                    statement[name_index],
                    "FSIM-SV-SEM-266",
                    "a real bin array requires one finite interval");
                continue;
            }
            const auto& value = bin.values.front();
            const auto left = std::bit_cast<double>(
                *value.range_left_real_bits);
            const auto right = std::bit_cast<double>(
                *value.range_right_real_bits);
            const auto lower = std::min(left, right);
            const auto upper = std::max(left, right);
            const auto span = upper - lower;
            std::size_t bin_count { };
            if (declared_array_size) {
                bin_count = *declared_array_size;
            } else if (declaration.effective_real_interval_bits) {
                const auto interval = std::bit_cast<double>(
                    *declaration.effective_real_interval_bits);
                const auto count = std::ceil(span / interval);
                if (!std::isfinite(count) || count < 1.0
                    || count > static_cast<double>(maximum_expanded_bins)) {
                    error(
                        statement[name_index],
                        "FSIM-SV-SEM-266",
                        "real_interval expansion exceeds the governed bin limit");
                    continue;
                }
                bin_count = static_cast<std::size_t>(count);
            } else {
                error(
                    statement[name_index],
                    "FSIM-SV-SEM-265",
                    "an unsized real bin array requires type_option.real_interval");
                continue;
            }
            if (bin_count == 0U || span <= 0.0) {
                error(
                    statement[name_index],
                    "FSIM-SV-SEM-266",
                    "a real bin array requires a nonempty finite interval");
                continue;
            }
            const auto step = declared_array_size
                ? span / static_cast<double>(bin_count)
                : std::bit_cast<double>(
                      *declaration.effective_real_interval_bits);
            for (std::size_t array_index = 0U;
                array_index < bin_count; ++array_index) {
                auto expanded = bin;
                expanded.name = source_name + "["
                    + std::to_string(array_index) + "]";
                expanded.array_index = array_index;
                expanded.declaration_index = next_bin_index++;
                auto expanded_value = value;
                const auto interval_left
                    = lower + static_cast<double>(array_index) * step;
                const auto interval_right = array_index + 1U == bin_count
                    ? upper
                    : std::min(
                          upper,
                          lower
                              + static_cast<double>(array_index + 1U)
                                  * step);
                expanded_value.range_left_real_bits
                    = std::bit_cast<std::uint64_t>(interval_left);
                expanded_value.range_right_real_bits
                    = std::bit_cast<std::uint64_t>(interval_right);
                expanded_value.range_left_inclusive = true;
                expanded_value.range_right_inclusive
                    = array_index + 1U == bin_count;
                expanded.values = { std::move(expanded_value) };
                declaration.bins.push_back(std::move(expanded));
            }
            continue;
        }

        std::vector<SystemVerilogCoverageBinValue> expanded_values;
        for (const auto& value : bin.values) {
            if (!value.range_left || !value.range_right) {
                expanded_values.push_back(value);
                continue;
            }
            const auto ascending = *value.range_left <= *value.range_right;
            auto current = *value.range_left;
            for (;;) {
                SystemVerilogCoverageBinValue expanded = value;
                expanded.exact_value = current;
                expanded.range_left.reset();
                expanded.range_right.reset();
                expanded_values.push_back(std::move(expanded));
                if (expanded_values.size() > maximum_expanded_bins)
                    break;
                if (current == *value.range_right)
                    break;
                current += ascending ? 1 : -1;
            }
            if (expanded_values.size() > maximum_expanded_bins)
                break;
        }
        if (!bin.with_tokens.empty()) {
            std::erase_if(
                expanded_values,
                [&](const SystemVerilogCoverageBinValue& value) {
                    if (!value.exact_value && value.exact_bits.empty())
                        return false;
                    const SystemVerilogCoverageSampleValue candidate {
                        value.exact_value.value_or(0),
                        0U,
                        value.width,
                        value.exact_bits,
                        value.exact_unknown_bits,
                        value.exact_signed
                    };
                    return !systemverilog_coverage_with_allows(
                        bin.with_tokens, candidate);
                });
        }
        const auto bin_count = declared_array_size.value_or(
            expanded_values.size());
        if (bin_count == 0U || bin_count > maximum_expanded_bins
            || expanded_values.size() > maximum_expanded_bins
            || bin_count > expanded_values.size()) {
            error(
                statement[name_index],
                "FSIM-SV-SEM-213",
                "bin array expansion exceeds 65536 bins/values or creates empty bins");
            continue;
        }
        std::size_t value_offset { };
        for (std::size_t array_index = 0; array_index < bin_count;
            ++array_index) {
            const auto remaining_values = expanded_values.size() - value_offset;
            const auto remaining_bins = bin_count - array_index;
            const auto value_count = (remaining_values + remaining_bins - 1U) / remaining_bins;
            auto expanded = bin;
            expanded.name = source_name + "[" + std::to_string(array_index) + "]";
            expanded.array_index = array_index;
            expanded.declaration_index = next_bin_index++;
            expanded.values.assign(
                expanded_values.begin()
                    + static_cast<std::ptrdiff_t>(value_offset),
                expanded_values.begin()
                    + static_cast<std::ptrdiff_t>(value_offset + value_count));
            value_offset += value_count;
            declaration.bins.push_back(std::move(expanded));
        }
    }
    if (!saw_bin_declaration) {
        const bool real_coverpoint
            = declaration.sampled_scalar_kind
                == SystemVerilogScalarKind::ShortReal
            || declaration.sampled_scalar_kind
                == SystemVerilogScalarKind::Real
            || declaration.sampled_scalar_kind
                == SystemVerilogScalarKind::Realtime;
        if (real_coverpoint) {
            error(
                declaration.name_token.value_or(
                    declaration.expression_tokens.front()),
                "FSIM-SV-SEM-266",
                "real-valued coverpoints require explicit bins");
            return;
        }
        SystemVerilogCoverageBin automatic;
        automatic.selection = SystemVerilogCoverageBinSelection::Automatic;
        automatic.name = "$auto";
        automatic.source_name = "$auto";
        automatic.weight = declaration.effective_weight;
        automatic.goal = declaration.effective_goal;
        automatic.at_least = declaration.effective_at_least;
        automatic.span = declaration.body_span.empty()
            ? declaration.expression_span
            : declaration.body_span;
        declaration.bins.push_back(std::move(automatic));
    }
}

} // namespace fsim::frontend
