// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <charconv>
#include <map>

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fsim::frontend {
namespace {

    [[nodiscard]] std::string_view assertion_kind_name(
        const SystemVerilogAssertionDeclarationKind kind)
    {
        switch (kind) {
        case SystemVerilogAssertionDeclarationKind::Sequence:
            return "sequence";
        case SystemVerilogAssertionDeclarationKind::Property:
            return "property";
        case SystemVerilogAssertionDeclarationKind::Checker:
            return "checker";
        }
        return "assertion";
    }

    [[nodiscard]] std::string_view assertion_terminator(
        const SystemVerilogAssertionDeclarationKind kind)
    {
        switch (kind) {
        case SystemVerilogAssertionDeclarationKind::Sequence:
            return "endsequence";
        case SystemVerilogAssertionDeclarationKind::Property:
            return "endproperty";
        case SystemVerilogAssertionDeclarationKind::Checker:
            return "endchecker";
        }
        return "endsequence";
    }

    void update_delimiter_depth(
        const TokenKind kind,
        int& parentheses,
        int& brackets,
        int& braces)
    {
        switch (kind) {
        case TokenKind::LeftParen:
            ++parentheses;
            break;
        case TokenKind::RightParen:
            --parentheses;
            break;
        case TokenKind::LeftBracket:
            ++brackets;
            break;
        case TokenKind::RightBracket:
            --brackets;
            break;
        case TokenKind::LeftBrace:
            ++braces;
            break;
        case TokenKind::RightBrace:
            --braces;
            break;
        default:
            break;
        }
    }

    [[nodiscard]] std::vector<std::vector<Token>> split_top_level(
        const std::span<const Token> tokens,
        const TokenKind separator)
    {
        std::vector<std::vector<Token>> result(1);
        int parentheses { };
        int brackets { };
        int braces { };
        for (const auto& token : tokens) {
            if (token.kind == separator
                && parentheses == 0
                && brackets == 0
                && braces == 0) {
                result.emplace_back();
                continue;
            }
            result.back().push_back(token);
            update_delimiter_depth(
                token.kind, parentheses, brackets, braces);
        }
        return result;
    }

    [[nodiscard]] std::optional<std::size_t> find_top_level(
        const std::span<const Token> tokens,
        const TokenKind sought)
    {
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            const auto& token = tokens[index];
            if (token.kind == sought
                && parentheses == 0
                && brackets == 0
                && braces == 0) {
                return index;
            }
            update_delimiter_depth(
                token.kind, parentheses, brackets, braces);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> matching_right_parenthesis(
        const std::span<const Token> tokens,
        const std::size_t left)
    {
        if (left >= tokens.size()
            || tokens[left].kind != TokenKind::LeftParen) {
            return std::nullopt;
        }
        int depth { };
        for (std::size_t index = left; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftParen) {
                ++depth;
            } else if (tokens[index].kind == TokenKind::RightParen) {
                --depth;
                if (depth == 0) {
                    return index;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> matching_right_bracket(
        const std::span<const Token> tokens,
        const std::size_t left)
    {
        if (left >= tokens.size()
            || tokens[left].kind != TokenKind::LeftBracket) {
            return std::nullopt;
        }
        int depth { };
        for (std::size_t index = left; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftBracket) {
                ++depth;
            } else if (tokens[index].kind == TokenKind::RightBracket) {
                --depth;
                if (depth == 0) {
                    return index;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool assertion_local_type_keyword(
        const std::string_view text)
    {
        constexpr std::string_view keywords[] = {
            "var", "const", "bit", "logic", "reg", "byte", "shortint",
            "int", "longint", "integer", "time", "shortreal", "real",
            "realtime", "string", "chandle", "event"
        };
        return std::ranges::find(keywords, text) != std::ranges::end(keywords);
    }

    [[nodiscard]] bool valid_named_local_type_prefix(
        const std::span<const Token> tokens)
    {
        if (tokens.empty()
            || tokens.back().kind == TokenKind::Scope
            || tokens.back().kind == TokenKind::Dot) {
            return false;
        }
        int brackets { };
        bool have_identifier { };
        bool expect_identifier { true };
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::LeftBracket) {
                ++brackets;
                continue;
            }
            if (token.kind == TokenKind::RightBracket) {
                --brackets;
                continue;
            }
            if (brackets > 0) {
                continue;
            }
            if (token.kind == TokenKind::Identifier) {
                if (!expect_identifier) {
                    return false;
                }
                have_identifier = true;
                expect_identifier = false;
                continue;
            }
            if (token.kind != TokenKind::Scope
                && token.kind != TokenKind::Dot) {
                return false;
            }
            if (expect_identifier) {
                return false;
            }
            expect_identifier = true;
        }
        return brackets == 0 && have_identifier && !expect_identifier;
    }

    [[nodiscard]] bool assertion_reference_keyword(
        const std::string_view text)
    {
        constexpr std::string_view keywords[] = {
            "accept_on", "always", "and", "assert", "assume", "case",
            "default", "disable", "dist", "edge", "else", "endcase",
            "endclocking", "eventually", "first_match", "iff", "if",
            "inside", "intersect", "matches", "negedge", "nexttime", "not",
            "null", "or", "posedge", "property", "reject_on", "restrict",
            "s_always", "s_eventually", "s_nexttime", "s_until",
            "s_until_with", "strong", "sync_accept_on", "sync_reject_on",
            "tagged", "this", "throughout", "until", "until_with", "weak",
            "within", "true", "false", "clocking", "cover", "matched",
            "triggered"
        };
        return std::ranges::find(keywords, text) != std::ranges::end(keywords);
    }

    [[nodiscard]] std::unordered_set<std::string> assertion_design_unit_names(
        const DesignUnit& unit)
    {
        std::unordered_set<std::string> names;
        const auto add_names = [&](const auto& declarations) {
            for (const auto& declaration : declarations) {
                names.insert(declaration.name);
            }
        };
        add_names(unit.parameters);
        add_names(unit.ports);
        add_names(unit.signals);
        add_names(unit.variables);
        add_names(unit.type_aliases);
        add_names(unit.functions);
        add_names(unit.tasks);
        for (const auto& clocking : unit.systemverilog_clocking_blocks) {
            names.insert(clocking.name);
        }
        return names;
    }

} // namespace

#include "verilog_parser_assertion_formals.tpp"

#include "verilog_parser_assertion_locals.tpp"
void VerilogParser::structure_assertion_clock_and_disable(
    SystemVerilogAssertionDeclaration& declaration,
    std::size_t position)
{
    const auto& body = declaration.body_tokens;
    const auto expression_start = position;
    if (position < body.size()
        && body[position].kind == TokenKind::At) {
        const auto at = position++;
        SystemVerilogAssertionClock clock;
        if (position < body.size()
            && body[position].kind == TokenKind::LeftParen) {
            const auto right = matching_right_parenthesis(body, position);
            if (!right || *right == position + 1U) {
                error(
                    body[at],
                    "FSIM-SV-PARSE-296",
                    "an assertion declaration clock has no balanced event expression");
                position = expression_start;
            } else {
                clock.event_tokens.assign(
                    body.begin() + static_cast<std::ptrdiff_t>(position + 1U),
                    body.begin() + static_cast<std::ptrdiff_t>(*right));
                clock.span = span_from(body[at], body[*right]);
                declaration.clock = std::move(clock);
                position = *right + 1U;
            }
        } else if (position < body.size()
            && body[position].kind == TokenKind::Identifier) {
            clock.event_tokens.push_back(body[position]);
            clock.span = span_from(body[at], body[position]);
            declaration.clock = std::move(clock);
            ++position;
        } else {
            error(
                body[at],
                "FSIM-SV-PARSE-296",
                "an assertion declaration clock has no event expression");
            position = expression_start;
        }
    }

    if (position < body.size() && body[position].text == "disable") {
        const auto disable_start = position;
        if (position + 2U >= body.size()
            || body[position + 1U].text != "iff"
            || body[position + 2U].kind != TokenKind::LeftParen) {
            error(
                body[position],
                "FSIM-SV-PARSE-297",
                "an assertion disable clause requires 'disable iff (condition)'");
        } else {
            const auto left = position + 2U;
            const auto right = matching_right_parenthesis(body, left);
            if (!right || *right == left + 1U) {
                error(
                    body[position],
                    "FSIM-SV-PARSE-297",
                    "an assertion disable clause has no balanced condition");
            } else {
                SystemVerilogAssertionDisable disable;
                disable.condition_tokens.assign(
                    body.begin() + static_cast<std::ptrdiff_t>(left + 1U),
                    body.begin() + static_cast<std::ptrdiff_t>(*right));
                disable.span = span_from(body[disable_start], body[*right]);
                declaration.disable = std::move(disable);
                position = *right + 1U;
            }
        }
    }

    declaration.expression_tokens.assign(
        body.begin() + static_cast<std::ptrdiff_t>(position),
        body.end());
    if (!declaration.expression_tokens.empty()) {
        declaration.expression_span = span_from(
            declaration.expression_tokens.front(),
            declaration.expression_tokens.back());
    }
}

void VerilogParser::structure_sequence_expression(
    SystemVerilogAssertionDeclaration& declaration)
{
    if (declaration.kind
            != SystemVerilogAssertionDeclarationKind::Sequence
        || declaration.expression_tokens.empty()) {
        return;
    }
    auto tokens = declaration.expression_tokens;
    if (!tokens.empty()
        && tokens.back().kind == TokenKind::Semicolon) {
        tokens.pop_back();
    }
    if (tokens.empty()) {
        error(
            declaration.expression_tokens.front(),
            "FSIM-SV-PARSE-299",
            "a sequence declaration has no sequence expression");
        return;
    }

    SystemVerilogSequenceExpression sequence;
    const auto append_range = [&](SystemVerilogSequenceRange& range,
                                  const std::span<const Token> range_tokens) {
        const auto colon = find_top_level(range_tokens, TokenKind::Colon);
        const auto minimum_end = colon.value_or(range_tokens.size());
        range.minimum_tokens.assign(
            range_tokens.begin(),
            range_tokens.begin()
                + static_cast<std::ptrdiff_t>(minimum_end));
        if (colon) {
            range.maximum_tokens.assign(
                range_tokens.begin()
                    + static_cast<std::ptrdiff_t>(*colon + 1U),
                range_tokens.end());
        }
    };
    const auto append_element = [&](std::vector<Token> element_tokens) {
        if (element_tokens.empty()) {
            error(
                declaration.expression_tokens.front(),
                "FSIM-SV-PARSE-299",
                "a sequence concatenation has an empty element");
            return;
        }
        SystemVerilogSequenceElement element;
        const auto element_last = element_tokens.back();
        std::optional<std::size_t> repetition_left;
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t index = 0; index < element_tokens.size(); ++index) {
            if (element_tokens[index].kind == TokenKind::LeftBracket
                && parentheses == 0 && brackets == 0 && braces == 0) {
                repetition_left = index;
            }
            update_delimiter_depth(
                element_tokens[index].kind,
                parentheses,
                brackets,
                braces);
        }
        if (repetition_left) {
            const auto right = matching_right_bracket(
                element_tokens, *repetition_left);
            if (right && *right + 1U == element_tokens.size()
                && *repetition_left + 1U < *right) {
                const auto marker = element_tokens[*repetition_left + 1U].kind;
                if (marker == TokenKind::Star
                    || marker == TokenKind::Assign
                    || marker == TokenKind::ThinArrow) {
                    element.repetition = marker == TokenKind::Star
                        ? SystemVerilogSequenceRepetitionKind::Consecutive
                        : marker == TokenKind::Assign
                        ? SystemVerilogSequenceRepetitionKind::Nonconsecutive
                        : SystemVerilogSequenceRepetitionKind::Goto;
                    SystemVerilogSequenceRange range;
                    const std::span count_tokens {
                        element_tokens.data() + *repetition_left + 2U,
                        *right - *repetition_left - 2U
                    };
                    append_range(range, count_tokens);
                    range.span = span_from(
                        element_tokens[*repetition_left],
                        element_tokens[*right]);
                    element.repetition_range = std::move(range);
                    element_tokens.resize(*repetition_left);
                }
            }
        }
        if (element_tokens.empty()) {
            error(
                declaration.expression_tokens.front(),
                "FSIM-SV-PARSE-299",
                "a sequence repetition has no operand");
            return;
        }
        element.expression_tokens = std::move(element_tokens);
        element.span = span_from(
            element.expression_tokens.front(),
            element_last);
        sequence.elements.push_back(std::move(element));
    };

    std::size_t intersection_start { };
    int intersection_parentheses { };
    int intersection_brackets { };
    int intersection_braces { };
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        if (tokens[position].text == "intersect"
            && intersection_parentheses == 0
            && intersection_brackets == 0
            && intersection_braces == 0) {
            if (position == intersection_start) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-300",
                    "a sequence intersection has an empty left operand");
            } else {
                SystemVerilogSequenceIntersectionOperand operand;
                operand.tokens.assign(
                    tokens.begin() + static_cast<std::ptrdiff_t>(intersection_start),
                    tokens.begin() + static_cast<std::ptrdiff_t>(position));
                operand.span = span_from(operand.tokens.front(), operand.tokens.back());
                sequence.intersection_operands.push_back(std::move(operand));
            }
            intersection_start = position + 1U;
        }
        update_delimiter_depth(
            tokens[position].kind,
            intersection_parentheses,
            intersection_brackets,
            intersection_braces);
    }
    if (intersection_start != 0U) {
        if (intersection_start == tokens.size()) {
            error(
                tokens.back(),
                "FSIM-SV-PARSE-300",
                "a sequence intersection has an empty right operand");
        } else {
            SystemVerilogSequenceIntersectionOperand operand;
            operand.tokens.assign(
                tokens.begin() + static_cast<std::ptrdiff_t>(intersection_start),
                tokens.end());
            operand.span = span_from(operand.tokens.front(), operand.tokens.back());
            sequence.intersection_operands.push_back(std::move(operand));
        }
    }

    int binary_parentheses { };
    int binary_brackets { };
    int binary_braces { };
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        if ((tokens[position].text == "throughout"
                || tokens[position].text == "within")
            && binary_parentheses == 0
            && binary_brackets == 0
            && binary_braces == 0) {
            if (position == 0U || position + 1U == tokens.size()) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-301",
                    "a sequence '" + tokens[position].text
                        + "' operator has an empty operand");
            } else {
                SystemVerilogSequenceBinaryOperation operation;
                operation.kind = tokens[position].text == "throughout"
                    ? SystemVerilogSequenceBinaryKind::Throughout
                    : SystemVerilogSequenceBinaryKind::Within;
                operation.left_tokens.assign(
                    tokens.begin(),
                    tokens.begin() + static_cast<std::ptrdiff_t>(position));
                operation.right_tokens.assign(
                    tokens.begin() + static_cast<std::ptrdiff_t>(position + 1U),
                    tokens.end());
                operation.span = span_from(tokens.front(), tokens.back());
                sequence.binary_operations.push_back(std::move(operation));
            }
        }
        update_delimiter_depth(
            tokens[position].kind,
            binary_parentheses,
            binary_brackets,
            binary_braces);
    }

    for (std::size_t position = 0; position < tokens.size(); ++position) {
        if (tokens[position].text != "first_match") {
            continue;
        }
        if (position + 1U >= tokens.size()
            || tokens[position + 1U].kind != TokenKind::LeftParen) {
            error(
                tokens[position],
                "FSIM-SV-PARSE-301",
                "first_match requires a parenthesized sequence expression");
            continue;
        }
        const auto left = position + 1U;
        const auto right = matching_right_parenthesis(tokens, left);
        if (!right || *right == left + 1U) {
            error(
                tokens[position],
                "FSIM-SV-PARSE-301",
                "first_match has no balanced sequence expression");
            continue;
        }
        const std::span inner {
            tokens.data() + left + 1U,
            *right - left - 1U
        };
        const auto comma = find_top_level(inner, TokenKind::Comma);
        const auto sequence_end = comma.value_or(inner.size());
        if (sequence_end == 0U) {
            error(
                tokens[position],
                "FSIM-SV-PARSE-301",
                "first_match has an empty sequence argument");
            continue;
        }
        SystemVerilogSequenceFirstMatch first_match;
        first_match.sequence_tokens.assign(
            inner.begin(),
            inner.begin() + static_cast<std::ptrdiff_t>(sequence_end));
        if (comma && *comma + 1U < inner.size()) {
            first_match.match_item_tokens.assign(
                inner.begin() + static_cast<std::ptrdiff_t>(*comma + 1U),
                inner.end());
        }
        first_match.span = span_from(tokens[position], tokens[*right]);
        sequence.first_matches.push_back(std::move(first_match));
        position = *right;
    }

    std::vector<Token> element_tokens;
    int parentheses { };
    int brackets { };
    int braces { };
    for (std::size_t position = 0; position < tokens.size();) {
        if (tokens[position].kind == TokenKind::Hash
            && position + 1U < tokens.size()
            && tokens[position + 1U].kind == TokenKind::Hash
            && parentheses == 0 && brackets == 0 && braces == 0) {
            append_element(std::move(element_tokens));
            element_tokens.clear();
            const auto delay_start = position;
            position += 2U;
            SystemVerilogSequenceDelay delay;
            if (position < tokens.size()
                && tokens[position].kind == TokenKind::LeftBracket) {
                const auto right = matching_right_bracket(tokens, position);
                if (!right || *right == position + 1U) {
                    error(
                        tokens[delay_start],
                        "FSIM-SV-PARSE-298",
                        "a sequence concatenation delay has no balanced range");
                    return;
                }
                const std::span range_tokens {
                    tokens.data() + position + 1U,
                    *right - position - 1U
                };
                append_range(delay.range, range_tokens);
                delay.range.span = span_from(tokens[position], tokens[*right]);
                delay.span = span_from(tokens[delay_start], tokens[*right]);
                position = *right + 1U;
            } else if (position < tokens.size()
                && (tokens[position].kind == TokenKind::Number
                    || tokens[position].kind == TokenKind::Identifier)) {
                delay.range.minimum_tokens.push_back(tokens[position]);
                delay.range.span = tokens[position].span;
                delay.span = span_from(tokens[delay_start], tokens[position]);
                ++position;
            } else {
                error(
                    tokens[delay_start],
                    "FSIM-SV-PARSE-298",
                    "a sequence concatenation requires a delay value or range");
                return;
            }
            delay.fusion = delay.range.maximum_tokens.empty()
                && delay.range.minimum_tokens.size() == 1U
                && delay.range.minimum_tokens.front().kind == TokenKind::Number
                && delay.range.minimum_tokens.front().text == "0";
            sequence.delays.push_back(std::move(delay));
            continue;
        }
        element_tokens.push_back(tokens[position]);
        update_delimiter_depth(
            tokens[position].kind,
            parentheses,
            brackets,
            braces);
        ++position;
    }
    append_element(std::move(element_tokens));
    if (sequence.elements.size() != sequence.delays.size() + 1U) {
        return;
    }
    sequence.span = span_from(tokens.front(), tokens.back());
    declaration.sequence_expression = std::move(sequence);
}

void VerilogParser::structure_property_expression(
    SystemVerilogAssertionDeclaration& declaration)
{
    if (declaration.kind
            != SystemVerilogAssertionDeclarationKind::Property
        || declaration.expression_tokens.empty()) {
        return;
    }
    auto tokens = declaration.expression_tokens;
    if (!tokens.empty()
        && tokens.back().kind == TokenKind::Semicolon) {
        tokens.pop_back();
    }
    if (tokens.empty()) {
        error(
            declaration.expression_tokens.front(),
            "FSIM-SV-PARSE-303",
            "a property declaration has no property expression");
        return;
    }

    SystemVerilogPropertyExpression property;
    const auto append_range = [&](SystemVerilogSequenceRange& range,
                                  const std::span<const Token> range_tokens) {
        const auto colon = find_top_level(range_tokens, TokenKind::Colon);
        const auto minimum_end = colon.value_or(range_tokens.size());
        range.minimum_tokens.assign(
            range_tokens.begin(),
            range_tokens.begin()
                + static_cast<std::ptrdiff_t>(minimum_end));
        if (colon) {
            range.maximum_tokens.assign(
                range_tokens.begin()
                    + static_cast<std::ptrdiff_t>(*colon + 1U),
                range_tokens.end());
        }
    };

    int parentheses { };
    int brackets { };
    int braces { };
    const bool property_2009 = keyword_set_rank(keyword_set_)
        >= keyword_set_rank(KeywordSet::SystemVerilog2009);
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        const auto top_level = parentheses == 0 && brackets == 0 && braces == 0;
        const auto overlapped = position + 1U < tokens.size()
            && tokens[position].kind == TokenKind::Pipe
            && tokens[position + 1U].kind == TokenKind::ThinArrow;
        const auto nonoverlapped = position + 1U < tokens.size()
            && ((tokens[position].kind == TokenKind::Pipe
                    && tokens[position + 1U].kind == TokenKind::Arrow)
                || (tokens[position].kind == TokenKind::PipeAssign
                    && tokens[position + 1U].kind == TokenKind::Greater));
        if (top_level && (overlapped || nonoverlapped)) {
            if (position == 0U || position + 2U == tokens.size()) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-303",
                    "a property implication has an empty "
                        + std::string { position == 0U
                                ? "antecedent"
                                : "consequent" });
            } else {
                SystemVerilogPropertyImplication implication;
                implication.kind = overlapped
                    ? SystemVerilogPropertyImplicationKind::Overlapped
                    : SystemVerilogPropertyImplicationKind::Nonoverlapped;
                implication.antecedent_tokens.assign(
                    tokens.begin(),
                    tokens.begin() + static_cast<std::ptrdiff_t>(position));
                implication.consequent_tokens.assign(
                    tokens.begin() + static_cast<std::ptrdiff_t>(position + 2U),
                    tokens.end());
                implication.span = span_from(tokens.front(), tokens.back());
                property.implications.push_back(std::move(implication));
            }
            ++position;
            continue;
        }
        const auto until_kind = [&]()
            -> std::optional<SystemVerilogPropertyUntilKind> {
            if (tokens[position].text == "until") {
                return SystemVerilogPropertyUntilKind::Until;
            }
            if (tokens[position].text == "s_until") {
                return SystemVerilogPropertyUntilKind::StrongUntil;
            }
            if (tokens[position].text == "until_with") {
                return SystemVerilogPropertyUntilKind::UntilWith;
            }
            if (tokens[position].text == "s_until_with") {
                return SystemVerilogPropertyUntilKind::StrongUntilWith;
            }
            return std::nullopt;
        }();
        if (top_level && until_kind
            && (property_2009
                || (position != 0U && position + 1U < tokens.size()))) {
            if (position != 0U && position + 1U < tokens.size()) {
                (void)require_standard(
                    "property operator '" + tokens[position].text + "'",
                    StandardRevision::SystemVerilog2009,
                    tokens[position],
                    "FSIM-SV-PARSE-349");
            }
            if (position == 0U || position + 1U == tokens.size()) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-304",
                    "a property '" + tokens[position].text
                        + "' operator has an empty operand");
            } else {
                SystemVerilogPropertyUntilOperation operation;
                operation.kind = *until_kind;
                operation.left_tokens.assign(
                    tokens.begin(),
                    tokens.begin() + static_cast<std::ptrdiff_t>(position));
                operation.right_tokens.assign(
                    tokens.begin() + static_cast<std::ptrdiff_t>(position + 1U),
                    tokens.end());
                operation.span = span_from(tokens.front(), tokens.back());
                property.until_operations.push_back(std::move(operation));
            }
            continue;
        }
        if (top_level
            && (tokens[position].text == "nexttime"
                || tokens[position].text == "s_nexttime")
            && (property_2009 || position + 1U < tokens.size())) {
            if (position + 1U < tokens.size()) {
                (void)require_standard(
                    "property operator '" + tokens[position].text + "'",
                    StandardRevision::SystemVerilog2009,
                    tokens[position],
                    "FSIM-SV-PARSE-349");
            }
            auto operand_start = position + 1U;
            SystemVerilogPropertyNexttime nexttime;
            nexttime.kind = tokens[position].text == "nexttime"
                ? SystemVerilogPropertyNexttimeKind::Nexttime
                : SystemVerilogPropertyNexttimeKind::StrongNexttime;
            auto last = position;
            if (operand_start < tokens.size()
                && tokens[operand_start].kind == TokenKind::LeftBracket) {
                const auto right = matching_right_bracket(tokens, operand_start);
                if (!right || *right == operand_start + 1U) {
                    error(
                        tokens[position],
                        "FSIM-SV-PARSE-304",
                        "a property '" + tokens[position].text
                            + "' count is empty or unbalanced");
                    continue;
                }
                nexttime.count_tokens.assign(
                    tokens.begin()
                        + static_cast<std::ptrdiff_t>(operand_start + 1U),
                    tokens.begin() + static_cast<std::ptrdiff_t>(*right));
                operand_start = *right + 1U;
                last = *right;
            }
            if (operand_start == tokens.size()) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-304",
                    "a property '" + tokens[position].text
                        + "' operator has no operand");
                position = last;
                continue;
            }
            nexttime.operand_tokens.assign(
                tokens.begin() + static_cast<std::ptrdiff_t>(operand_start),
                tokens.end());
            nexttime.span = span_from(tokens[position], tokens.back());
            property.nexttimes.push_back(std::move(nexttime));
            position = last;
            continue;
        }
        const auto recurrence_kind = [&]()
            -> std::optional<SystemVerilogPropertyRecurrenceKind> {
            if (tokens[position].text == "always") {
                return SystemVerilogPropertyRecurrenceKind::Always;
            }
            if (tokens[position].text == "s_always") {
                return SystemVerilogPropertyRecurrenceKind::StrongAlways;
            }
            if (tokens[position].text == "eventually") {
                return SystemVerilogPropertyRecurrenceKind::Eventually;
            }
            if (tokens[position].text == "s_eventually") {
                return SystemVerilogPropertyRecurrenceKind::StrongEventually;
            }
            return std::nullopt;
        }();
        if (top_level && recurrence_kind
            && (tokens[position].text == "always"
                || property_2009 || position + 1U < tokens.size())) {
            if (tokens[position].text != "always"
                && position + 1U < tokens.size()) {
                (void)require_standard(
                    "property operator '" + tokens[position].text + "'",
                    StandardRevision::SystemVerilog2009,
                    tokens[position],
                    "FSIM-SV-PARSE-349");
            }
            auto operand_start = position + 1U;
            SystemVerilogPropertyRecurrence recurrence;
            recurrence.kind = *recurrence_kind;
            auto last = position;
            if (operand_start < tokens.size()
                && tokens[operand_start].kind == TokenKind::LeftBracket) {
                const auto right = matching_right_bracket(tokens, operand_start);
                if (!right || *right == operand_start + 1U) {
                    error(
                        tokens[position],
                        "FSIM-SV-PARSE-305",
                        "a property '" + tokens[position].text
                            + "' range is empty or unbalanced");
                    continue;
                }
                SystemVerilogSequenceRange range;
                const std::span range_tokens {
                    tokens.data() + operand_start + 1U,
                    *right - operand_start - 1U
                };
                append_range(range, range_tokens);
                range.span = span_from(tokens[operand_start], tokens[*right]);
                recurrence.range = std::move(range);
                operand_start = *right + 1U;
                last = *right;
            }
            if (operand_start == tokens.size()) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-305",
                    "a property '" + tokens[position].text
                        + "' operator has no operand");
                position = last;
                continue;
            }
            recurrence.operand_tokens.assign(
                tokens.begin() + static_cast<std::ptrdiff_t>(operand_start),
                tokens.end());
            recurrence.span = span_from(tokens[position], tokens.back());
            property.recurrences.push_back(std::move(recurrence));
            position = last;
            continue;
        }
        if (top_level
            && (tokens[position].text == "strong"
                || tokens[position].text == "weak")
            && (property_2009
                || (position + 1U < tokens.size()
                    && tokens[position + 1U].kind
                        == TokenKind::LeftParen))) {
            if (position + 1U < tokens.size()
                && tokens[position + 1U].kind == TokenKind::LeftParen) {
                (void)require_standard(
                    "property operator '" + tokens[position].text + "'",
                    StandardRevision::SystemVerilog2009,
                    tokens[position],
                    "FSIM-SV-PARSE-349");
            }
            if (position + 1U >= tokens.size()
                || tokens[position + 1U].kind != TokenKind::LeftParen) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-305",
                    "property sequence strength requires a parenthesized sequence");
                continue;
            }
            const auto left = position + 1U;
            const auto right = matching_right_parenthesis(tokens, left);
            if (!right || *right == left + 1U) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-305",
                    "property sequence strength has no balanced sequence operand");
                continue;
            }
            SystemVerilogPropertySequenceStrength strength;
            strength.kind = tokens[position].text == "strong"
                ? SystemVerilogPropertySequenceStrengthKind::Strong
                : SystemVerilogPropertySequenceStrengthKind::Weak;
            strength.sequence_tokens.assign(
                tokens.begin() + static_cast<std::ptrdiff_t>(left + 1U),
                tokens.begin() + static_cast<std::ptrdiff_t>(*right));
            strength.span = span_from(tokens[position], tokens[*right]);
            property.sequence_strengths.push_back(std::move(strength));
            position = *right;
            continue;
        }
        const auto abort_operator = tokens[position].text == "accept_on"
            || tokens[position].text == "reject_on"
            || tokens[position].text == "sync_accept_on"
            || tokens[position].text == "sync_reject_on";
        if (top_level && abort_operator
            && (property_2009
                || (position + 1U < tokens.size()
                    && tokens[position + 1U].kind
                        == TokenKind::LeftParen))) {
            if (position + 1U < tokens.size()
                && tokens[position + 1U].kind == TokenKind::LeftParen) {
                (void)require_standard(
                    "property operator '" + tokens[position].text + "'",
                    StandardRevision::SystemVerilog2009,
                    tokens[position],
                    "FSIM-SV-PARSE-349");
            }
            if (position + 1U >= tokens.size()
                || tokens[position + 1U].kind != TokenKind::LeftParen) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-306",
                    "a property abort operator requires a parenthesized condition");
                continue;
            }
            const auto left = position + 1U;
            const auto right = matching_right_parenthesis(tokens, left);
            if (!right || *right == left + 1U) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-306",
                    "a property abort condition is empty or unbalanced");
                continue;
            }
            if (*right + 1U == tokens.size()) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-306",
                    "a property abort operator has no property operand");
                position = *right;
                continue;
            }
            SystemVerilogPropertyAbort abort;
            abort.outcome = tokens[position].text == "accept_on"
                    || tokens[position].text == "sync_accept_on"
                ? SystemVerilogPropertyAbortOutcome::VacuousSuccess
                : SystemVerilogPropertyAbortOutcome::Failure;
            abort.synchronous = tokens[position].text == "sync_accept_on"
                || tokens[position].text == "sync_reject_on";
            abort.condition_tokens.assign(
                tokens.begin() + static_cast<std::ptrdiff_t>(left + 1U),
                tokens.begin() + static_cast<std::ptrdiff_t>(*right));
            abort.property_tokens.assign(
                tokens.begin() + static_cast<std::ptrdiff_t>(*right + 1U),
                tokens.end());
            abort.span = span_from(tokens[position], tokens.back());
            property.aborts.push_back(std::move(abort));
            position = *right;
            continue;
        }
        if (top_level
            && tokens[position].kind == TokenKind::Hash
            && position + 1U < tokens.size()
            && tokens[position + 1U].kind == TokenKind::Hash) {
            const auto delay_start = position;
            position += 2U;
            SystemVerilogSequenceDelay delay;
            if (position < tokens.size()
                && tokens[position].kind == TokenKind::LeftBracket) {
                const auto right = matching_right_bracket(tokens, position);
                if (!right || *right == position + 1U) {
                    error(
                        tokens[delay_start],
                        "FSIM-SV-PARSE-303",
                        "a property delay has no balanced nonempty range");
                    continue;
                }
                const std::span range_tokens {
                    tokens.data() + position + 1U,
                    *right - position - 1U
                };
                append_range(delay.range, range_tokens);
                delay.range.span = span_from(tokens[position], tokens[*right]);
                delay.span = span_from(tokens[delay_start], tokens[*right]);
                position = *right;
            } else if (position < tokens.size()
                && (tokens[position].kind == TokenKind::Number
                    || tokens[position].kind == TokenKind::Identifier)) {
                delay.range.minimum_tokens.push_back(tokens[position]);
                delay.range.span = tokens[position].span;
                delay.span = span_from(tokens[delay_start], tokens[position]);
            } else {
                error(
                    tokens[delay_start],
                    "FSIM-SV-PARSE-303",
                    "a property delay requires a value or range");
                continue;
            }
            delay.fusion = delay.range.maximum_tokens.empty()
                && delay.range.minimum_tokens.size() == 1U
                && delay.range.minimum_tokens.front().kind == TokenKind::Number
                && delay.range.minimum_tokens.front().text == "0";
            property.delays.push_back(std::move(delay));
            continue;
        }
        update_delimiter_depth(
            tokens[position].kind,
            parentheses,
            brackets,
            braces);
    }
    property.span = span_from(tokens.front(), tokens.back());
    declaration.property_expression = std::move(property);
}

void VerilogParser::structure_assertion_endpoints(
    SystemVerilogAssertionDeclaration& declaration)
{
    const auto& tokens = declaration.expression_tokens;
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        if (tokens[position].text != "matched"
            && tokens[position].text != "triggered") {
            continue;
        }
        if (position < 2U
            || tokens[position - 1U].kind != TokenKind::Dot) {
            error(
                tokens[position],
                "FSIM-SV-PARSE-302",
                "a sequence endpoint method requires a receiver");
            continue;
        }

        const auto receiver_end = position - 2U;
        std::size_t receiver_start = receiver_end;
        if (tokens[receiver_end].kind == TokenKind::RightParen) {
            int depth { };
            std::optional<std::size_t> left;
            for (std::size_t scan = receiver_end + 1U; scan-- > 0U;) {
                if (tokens[scan].kind == TokenKind::RightParen) {
                    ++depth;
                } else if (tokens[scan].kind == TokenKind::LeftParen) {
                    --depth;
                    if (depth == 0) {
                        left = scan;
                        break;
                    }
                }
            }
            if (!left || *left == 0U
                || tokens[*left - 1U].kind != TokenKind::Identifier) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-302",
                    "a sequence endpoint receiver call is malformed");
                continue;
            }
            receiver_start = *left - 1U;
        } else if (tokens[receiver_end].kind != TokenKind::Identifier) {
            error(
                tokens[position],
                "FSIM-SV-PARSE-302",
                "a sequence endpoint receiver is malformed");
            continue;
        }
        while (receiver_start >= 2U
            && (tokens[receiver_start - 1U].kind == TokenKind::Dot
                || tokens[receiver_start - 1U].kind == TokenKind::Scope)
            && tokens[receiver_start - 2U].kind == TokenKind::Identifier) {
            receiver_start -= 2U;
        }

        auto endpoint_end = position;
        bool method_parentheses { };
        if (position + 1U < tokens.size()
            && tokens[position + 1U].kind == TokenKind::LeftParen) {
            const auto right = matching_right_parenthesis(tokens, position + 1U);
            if (!right || *right != position + 2U) {
                error(
                    tokens[position],
                    "FSIM-SV-PARSE-302",
                    "a sequence endpoint method accepts only empty parentheses");
                continue;
            }
            method_parentheses = true;
            endpoint_end = *right;
        }

        SystemVerilogSequenceEndpoint endpoint;
        endpoint.kind = tokens[position].text == "matched"
            ? SystemVerilogSequenceEndpointKind::Matched
            : SystemVerilogSequenceEndpointKind::Triggered;
        endpoint.receiver_tokens.assign(
            tokens.begin() + static_cast<std::ptrdiff_t>(receiver_start),
            tokens.begin() + static_cast<std::ptrdiff_t>(receiver_end + 1U));
        for (const auto& token : endpoint.receiver_tokens) {
            endpoint.receiver_name += token.text;
        }
        endpoint.method_parentheses = method_parentheses;
        endpoint.span = span_from(tokens[receiver_start], tokens[endpoint_end]);
        declaration.sequence_endpoints.push_back(std::move(endpoint));
        position = endpoint_end;
    }
}

#include "verilog_parser_assertion_resolution.tpp"

SystemVerilogAssertionDeclaration
VerilogParser::parse_assertion_declaration(
    const Token& start,
    const SystemVerilogAssertionDeclarationKind kind)
{
    SystemVerilogAssertionDeclaration declaration;
    declaration.kind = kind;
    const auto kind_name = assertion_kind_name(kind);
    const auto terminator = assertion_terminator(kind);

    if (language_ != Language::SystemVerilog2017) {
        error(
            start,
            "FSIM-SV-SEM-191",
            "a " + std::string { kind_name }
                + " declaration requires SystemVerilog-2017");
    }

    if (!at(TokenKind::Identifier)) {
        error(
            current(),
            "FSIM-SV-PARSE-290",
            "expected a name after '" + std::string { kind_name } + "'");
    } else {
        const auto name = advance();
        declaration.name = name.text;
        declaration.name_span = name.span;
    }

    while (!at_end()
        && !at(TokenKind::Semicolon)
        && !keyword(terminator)
        && !any_keyword({ "endmodule", "endinterface", "endprogram" })) {
        declaration.header_tokens.push_back(advance());
    }
    if (!declaration.header_tokens.empty()) {
        declaration.header_span = span_from(
            declaration.header_tokens.front(),
            declaration.header_tokens.back());
    }
    structure_assertion_formals(declaration);
    expect(
        TokenKind::Semicolon,
        "';' after " + std::string { kind_name } + " declaration header",
        "FSIM-SV-PARSE-291");

    while (!at_end()
        && !keyword(terminator)
        && !any_keyword({ "endmodule", "endinterface", "endprogram" })) {
        declaration.body_tokens.push_back(advance());
    }
    if (!declaration.body_tokens.empty()) {
        declaration.body_span = span_from(
            declaration.body_tokens.front(),
            declaration.body_tokens.back());
    }
    const auto expression_position = structure_assertion_locals(declaration);
    structure_assertion_clock_and_disable(declaration, expression_position);
    structure_assertion_endpoints(declaration);
    structure_sequence_expression(declaration);
    structure_property_expression(declaration);
    expect_keyword(terminator, false, "FSIM-SV-PARSE-292");
    if (match(TokenKind::Colon)) {
        if (!at(TokenKind::Identifier)) {
            error(
                current(),
                "FSIM-SV-PARSE-290",
                "expected a name after '" + std::string { terminator } + " :'");
        } else {
            const auto end_name = advance();
            if (end_name.text != declaration.name) {
                error(
                    end_name,
                    "FSIM-SV-SEM-193",
                    std::string { kind_name } + " end name does not match '"
                        + declaration.name + "'");
            }
        }
    }
    declaration.span = span_from(start, previous());
    return declaration;
}

SystemVerilogConcurrentAssertion
VerilogParser::parse_concurrent_assertion(
    const Token& start,
    const SystemVerilogConcurrentAssertionKind kind,
    const std::optional<Token> label)
{
    SystemVerilogConcurrentAssertion assertion;
    assertion.kind = kind;
    if (label) {
        assertion.label = label->text;
        assertion.label_span = label->span;
    }
    if (!match_keyword("property")) {
        error(
            current(),
            "FSIM-SV-PARSE-307",
            "a concurrent assertion directive requires 'property'");
    }
    if (!match(TokenKind::LeftParen)) {
        error(
            current(),
            "FSIM-SV-PARSE-307",
            "a concurrent assertion property requires parentheses");
        skip_to_semicolon();
        assertion.span = span_from(start, previous());
        return assertion;
    }

    int depth { 1 };
    Token right = previous();
    while (!at_end() && depth != 0) {
        const auto token = advance();
        if (token.kind == TokenKind::LeftParen) {
            ++depth;
        } else if (token.kind == TokenKind::RightParen) {
            --depth;
            if (depth == 0) {
                right = token;
                break;
            }
        }
        if (depth != 0) {
            assertion.property_tokens.push_back(token);
        }
    }
    if (depth != 0 || assertion.property_tokens.empty()) {
        error(
            start,
            "FSIM-SV-PARSE-307",
            depth != 0
                ? "a concurrent assertion property is unbalanced"
                : "a concurrent assertion has an empty property");
    }

    const auto at_unit_terminator = [&]() {
        return keyword("endmodule")
            || keyword("endinterface")
            || keyword("endprogram");
    };
    const auto consume_action =
        [&](std::vector<Token>& tokens, SourceSpan& span) {
            if (at_end() || at_unit_terminator()) {
                return false;
            }
            if (keyword("begin")) {
                int block_depth { };
                do {
                    const auto token = advance();
                    tokens.push_back(token);
                    if (token.text == "begin") {
                        ++block_depth;
                    } else if (token.text == "end") {
                        --block_depth;
                    }
                } while (!at_end() && block_depth != 0);
                if (block_depth != 0) {
                    return false;
                }
            } else {
                while (!at_end() && !at_unit_terminator()) {
                    const auto token = advance();
                    tokens.push_back(token);
                    if (token.kind == TokenKind::Semicolon) {
                        break;
                    }
                }
                if (tokens.empty()
                    || tokens.back().kind != TokenKind::Semicolon) {
                    return false;
                }
            }
            span = span_from(tokens.front(), tokens.back());
            return true;
        };
    const auto reject_illegal_action =
        [&](const Token& token, const bool failure) {
            if (kind == SystemVerilogConcurrentAssertionKind::Restrict
                || (failure
                    && kind == SystemVerilogConcurrentAssertionKind::Cover)) {
                error(
                    token,
                    "FSIM-SV-SEM-198",
                    kind == SystemVerilogConcurrentAssertionKind::Restrict
                        ? "a restrict property directive cannot have an action"
                        : "a cover property directive cannot have a failure action");
            }
        };
    const auto parse_action =
        [&](const bool failure) {
            auto& present = failure
                ? assertion.has_failure_action
                : assertion.has_pass_action;
            auto& tokens = failure
                ? assertion.failure_action_tokens
                : assertion.pass_action_tokens;
            auto& span = failure
                ? assertion.failure_action_span
                : assertion.pass_action_span;
            present = true;
            const auto action_start = current();
            reject_illegal_action(action_start, failure);
            if (!consume_action(tokens, span)) {
                error(
                    action_start,
                    "FSIM-SV-PARSE-308",
                    failure
                        ? "expected a complete concurrent assertion failure action"
                        : "expected a complete concurrent assertion pass action");
            }
        };

    if (match(TokenKind::Semicolon)) {
        const auto null_action = previous();
        if (match_keyword("else")) {
            assertion.has_pass_action = true;
            assertion.pass_action_tokens.push_back(null_action);
            assertion.pass_action_span = assertion.pass_action_tokens.front().span;
            parse_action(true);
        }
    } else if (match_keyword("else")) {
        parse_action(true);
    } else if (at_end() || at_unit_terminator()) {
        error(
            current(),
            "FSIM-SV-PARSE-307",
            "expected ';' after concurrent assertion directive");
    } else {
        parse_action(false);
        if (match_keyword("else")) {
            parse_action(true);
        }
    }
    const auto& last = assertion.has_failure_action
            && !assertion.failure_action_tokens.empty()
        ? assertion.failure_action_tokens.back()
        : assertion.has_pass_action
            && !assertion.pass_action_tokens.empty()
        ? assertion.pass_action_tokens.back()
        : right;
    assertion.span = span_from(
        label ? *label : start,
        last);
    return assertion;
}

} // namespace fsim::frontend
