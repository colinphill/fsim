// SPDX-License-Identifier: Apache-2.0
//
// General concurrent-property evaluation (IEEE 1800-2017 16.7-16.12).
//
// A property is parsed from its tokens into sequence and property nodes. Each
// sequence becomes a nondeterministic automaton whose transitions consume one
// clock tick under a sampled boolean guard. The directive's process advances
// every attempt once per clock tick: each automaton state is a vector of the
// attempt slots positioned there, an attempt succeeds at its first accepting
// state and fails when it has no position left, and an antecedent match
// starts consequent attempts in the same tick.

#include "verilog_parser_internal.hpp"
#include "verilog_parser_assertion_support.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

namespace fsim::frontend {
namespace {

constexpr std::size_t maximum_states = 192U;
constexpr std::int64_t unbounded = -1;

struct SequenceNode;
using SequencePointer = std::shared_ptr<SequenceNode>;

enum class SequenceKind {
    boolean,
    concatenation,
    repetition,
    goto_repetition,
    nonconsecutive_repetition,
    alternation,
    throughout,
    conjunction,
    intersection,
};

// A sequence: `boolean` holds an expression; `concatenation` joins
// `left` and `right` with a `##[minimum:maximum]` delay (`left` absent for a
// leading delay); the repetitions repeat `left` (or the boolean) between
// `minimum` and `maximum` times; `alternation` is `or`; `throughout`
// requires `condition` on every tick of `left`.
struct SequenceNode {
    SequenceKind kind { SequenceKind::boolean };
    std::optional<Expression> condition;
    SequencePointer left;
    SequencePointer right;
    std::int64_t minimum { };
    std::int64_t maximum { };
};

struct PropertyNode;
using PropertyPointer = std::shared_ptr<PropertyNode>;

enum class PropertyKind {
    sequence,
    negation,
    implication,
    followed_by,
    conditional,
};

struct PropertyNode {
    PropertyKind kind { PropertyKind::sequence };
    SequencePointer sequence;
    bool nonoverlapped { };
    std::optional<Expression> condition;
    PropertyPointer first;
    PropertyPointer second;
};

[[nodiscard]] bool keyword_token(const Token& token, std::string_view text)
{
    return token.kind == TokenKind::Identifier && token.text == text;
}

class PropertyParser {
public:
    PropertyParser(std::span<const Token> tokens, const DesignUnit& unit,
        std::function<std::optional<Expression>(std::span<const Token>)>
            boolean)
        : tokens_(tokens.begin(), tokens.end())
        , unit_(unit)
        , boolean_(std::move(boolean))
    {
    }

    [[nodiscard]] PropertyPointer parse()
    {
        auto property = parse_property();
        if (!property || position_ != tokens_.size()) {
            return nullptr;
        }
        return property;
    }

private:
    [[nodiscard]] bool at(TokenKind kind, std::size_t offset = 0) const
    {
        return position_ + offset < tokens_.size()
            && tokens_[position_ + offset].kind == kind;
    }
    [[nodiscard]] bool at_keyword(std::string_view text) const
    {
        return position_ < tokens_.size()
            && keyword_token(tokens_[position_], text);
    }
    // `|->`, and `|=>`, which lexes as `|=` then `>`.
    [[nodiscard]] bool at_implication() const
    {
        return (at(TokenKind::Pipe)
                   && (at(TokenKind::ThinArrow, 1) || at(TokenKind::Arrow, 1)))
            || (at(TokenKind::PipeAssign) && at(TokenKind::Greater, 1));
    }
    [[nodiscard]] bool at_followed_by() const
    {
        return at(TokenKind::Hash)
            && (at(TokenKind::Minus, 1) || at(TokenKind::Assign, 1)
                || at(TokenKind::EqualEqual, 1))
            && at(TokenKind::Hash, 2);
    }

    [[nodiscard]] std::optional<std::size_t> matching_close(
        std::size_t open, TokenKind left, TokenKind right) const
    {
        int depth { };
        for (auto index = open; index < tokens_.size(); ++index) {
            if (tokens_[index].kind == left) {
                ++depth;
            } else if (tokens_[index].kind == right && --depth == 0) {
                return index;
            }
        }
        return std::nullopt;
    }

    // A constant delay or repetition bound: a literal, an elaboration
    // constant of the unit, or `$`.
    [[nodiscard]] std::optional<std::int64_t> constant(
        std::span<const Token> tokens) const
    {
        if (tokens.size() == 1U) {
            const auto& token = tokens.front();
            if (token.text == "$") {
                return unbounded;
            }
            if (token.kind == TokenKind::Number) {
                std::string digits;
                auto text = std::string_view { token.text };
                if (const auto quote = text.find('\'');
                    quote != std::string_view::npos) {
                    text.remove_prefix(quote + 1U);
                    if (!text.empty() && (text.front() == 's'
                            || text.front() == 'S')) {
                        text.remove_prefix(1U);
                    }
                    if (text.empty() || (text.front() != 'd'
                            && text.front() != 'D')) {
                        return std::nullopt;
                    }
                    text.remove_prefix(1U);
                }
                for (const auto character : text) {
                    if (character == '_') {
                        continue;
                    }
                    if (character < '0' || character > '9') {
                        return std::nullopt;
                    }
                    digits.push_back(character);
                }
                if (digits.empty() || digits.size() > 9U) {
                    return std::nullopt;
                }
                return std::stoll(digits);
            }
            if (token.kind == TokenKind::Identifier) {
                const auto parameter = std::ranges::find_if(
                    unit_.parameters, [&](const ParameterDeclaration& entry) {
                        return entry.name == token.text;
                    });
                if (parameter != unit_.parameters.end()
                    && parameter->default_value.kind == ExpressionKind::IntegerLiteral) {
                    Token literal = token;
                    literal.kind = TokenKind::Number;
                    literal.text = parameter->default_value.text;
                    return constant(std::span<const Token> { &literal, 1U });
                }
            }
            return std::nullopt;
        }
        if (tokens.size() >= 3U && tokens.front().kind == TokenKind::LeftParen
            && tokens.back().kind == TokenKind::RightParen) {
            return constant(tokens.subspan(1U, tokens.size() - 2U));
        }
        if (tokens.size() == 3U
            && (tokens[1].kind == TokenKind::Plus
                || tokens[1].kind == TokenKind::Minus)) {
            const auto left = constant(tokens.first(1U));
            const auto right = constant(tokens.last(1U));
            if (!left || !right || *left < 0 || *right < 0) {
                return std::nullopt;
            }
            const auto value = tokens[1].kind == TokenKind::Plus
                ? *left + *right : *left - *right;
            return value < 0 ? std::nullopt : std::optional { value };
        }
        return std::nullopt;
    }

    // `[m:n]`, `[n]`, `[*]`, or `[+]` starting at the left bracket; returns
    // the bounds and moves past the right bracket.
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>>
    bracket_range(std::size_t open, std::size_t first_inside)
    {
        const auto close = matching_close(
            open, TokenKind::LeftBracket, TokenKind::RightBracket);
        if (!close) {
            return std::nullopt;
        }
        const std::span<const Token> inside {
            tokens_.data() + first_inside, *close - first_inside
        };
        position_ = *close + 1U;
        if (inside.size() == 1U && inside.front().kind == TokenKind::Star) {
            return std::pair { std::int64_t { 0 }, unbounded };
        }
        if (inside.size() == 1U && inside.front().kind == TokenKind::Plus) {
            return std::pair { std::int64_t { 1 }, unbounded };
        }
        const auto colon = std::ranges::find_if(inside, [](const Token& token) {
            return token.kind == TokenKind::Colon;
        });
        if (colon == inside.end()) {
            const auto value = constant(inside);
            if (!value || *value < 0) {
                return std::nullopt;
            }
            return std::pair { *value, *value };
        }
        const auto split = static_cast<std::size_t>(colon - inside.begin());
        const auto low = constant(inside.first(split));
        const auto high = constant(inside.subspan(split + 1U));
        if (!low || !high || *low < 0
            || (*high != unbounded && *high < *low)) {
            return std::nullopt;
        }
        return std::pair { *low, *high };
    }

    [[nodiscard]] PropertyPointer parse_property()
    {
        if (auto instance = property_instance()) {
            return instance;
        }
        if (at_keyword("not")) {
            ++position_;
            auto operand = parse_property();
            if (!operand) {
                return nullptr;
            }
            if (operand->kind == PropertyKind::negation) {
                return operand->first;
            }
            auto node = std::make_shared<PropertyNode>();
            node->kind = PropertyKind::negation;
            node->first = std::move(operand);
            return node;
        }
        if (at_keyword("if") && at(TokenKind::LeftParen, 1)) {
            const auto close = matching_close(
                position_ + 1U, TokenKind::LeftParen, TokenKind::RightParen);
            if (!close) {
                return nullptr;
            }
            auto condition = boolean_(std::span<const Token> {
                tokens_.data() + position_ + 2U, *close - position_ - 2U });
            if (!condition) {
                return nullptr;
            }
            position_ = *close + 1U;
            auto node = std::make_shared<PropertyNode>();
            node->kind = PropertyKind::conditional;
            node->condition = std::move(condition);
            node->first = parse_property();
            if (!node->first) {
                return nullptr;
            }
            if (at_keyword("else")) {
                ++position_;
                node->second = parse_property();
                if (!node->second) {
                    return nullptr;
                }
            }
            return node;
        }
        if ((at_keyword("strong") || at_keyword("weak"))
            && at(TokenKind::LeftParen, 1)) {
            const auto close = matching_close(
                position_ + 1U, TokenKind::LeftParen, TokenKind::RightParen);
            if (!close || *close + 1U != tokens_.size()) {
                return nullptr;
            }
            PropertyParser inner { std::span<const Token> {
                                       tokens_.data() + position_ + 2U,
                                       *close - position_ - 2U },
                unit_, boolean_ };
            auto sequence = inner.parse_sequence_only();
            if (!sequence) {
                return nullptr;
            }
            position_ = *close + 1U;
            auto node = std::make_shared<PropertyNode>();
            node->sequence = std::move(sequence);
            return node;
        }
        // A parenthesized property that is not a sequence, `(a |-> b)`.
        if (at(TokenKind::LeftParen)) {
            const auto close = matching_close(
                position_, TokenKind::LeftParen, TokenKind::RightParen);
            if (close && *close + 1U == tokens_.size()) {
                PropertyParser inner { std::span<const Token> {
                                           tokens_.data() + position_ + 1U,
                                           *close - position_ - 1U },
                    unit_, boolean_ };
                if (auto property = inner.parse();
                    property && property->kind != PropertyKind::sequence) {
                    position_ = *close + 1U;
                    return property;
                }
            }
        }
        auto sequence = parse_sequence();
        if (!sequence) {
            return nullptr;
        }
        if (at_implication()) {
            const bool nonoverlapped = at(TokenKind::Arrow, 1)
                || at(TokenKind::PipeAssign);
            position_ += 2U;
            auto consequent = parse_property();
            if (!consequent) {
                return nullptr;
            }
            auto node = std::make_shared<PropertyNode>();
            node->kind = PropertyKind::implication;
            node->nonoverlapped = nonoverlapped;
            node->sequence = std::move(sequence);
            node->first = std::move(consequent);
            return node;
        }
        if (at_followed_by()) {
            const bool nonoverlapped = !at(TokenKind::Minus, 1);
            position_ += 3U;
            auto consequent = parse_property();
            if (!consequent) {
                return nullptr;
            }
            auto node = std::make_shared<PropertyNode>();
            node->kind = PropertyKind::followed_by;
            node->nonoverlapped = nonoverlapped;
            node->sequence = std::move(sequence);
            node->first = std::move(consequent);
            return node;
        }
        auto node = std::make_shared<PropertyNode>();
        node->sequence = std::move(sequence);
        return node;
    }

public:
    [[nodiscard]] SequencePointer parse_sequence_only()
    {
        auto sequence = parse_sequence();
        return sequence && position_ == tokens_.size() ? sequence : nullptr;
    }

private:
    [[nodiscard]] SequencePointer binary(SequenceKind kind,
        SequencePointer left, SequencePointer right) const
    {
        auto node = std::make_shared<SequenceNode>();
        node->kind = kind;
        node->left = std::move(left);
        node->right = std::move(right);
        return node;
    }

    // Precedence, loosest first: or, and, intersect, within, throughout.
    [[nodiscard]] SequencePointer parse_sequence()
    {
        auto left = parse_and();
        while (left && at_keyword("or")) {
            ++position_;
            auto right = parse_and();
            if (!right) {
                return nullptr;
            }
            auto node = std::make_shared<SequenceNode>();
            node->kind = SequenceKind::alternation;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    [[nodiscard]] SequencePointer parse_and()
    {
        auto left = parse_intersect();
        while (left && at_keyword("and")) {
            ++position_;
            auto right = parse_intersect();
            if (!right) {
                return nullptr;
            }
            left = binary(SequenceKind::conjunction, std::move(left),
                std::move(right));
        }
        return left;
    }

    [[nodiscard]] SequencePointer parse_intersect()
    {
        auto left = parse_within();
        while (left && at_keyword("intersect")) {
            ++position_;
            auto right = parse_within();
            if (!right) {
                return nullptr;
            }
            left = binary(SequenceKind::intersection, std::move(left),
                std::move(right));
        }
        return left;
    }

    // `a within b` is `(1[*0:$] ##1 a ##1 1[*0:$]) intersect b` (16.9.10).
    [[nodiscard]] SequencePointer parse_within()
    {
        auto left = parse_throughout();
        while (left && at_keyword("within")) {
            ++position_;
            auto right = parse_throughout();
            if (!right) {
                return nullptr;
            }
            const auto idle = [] {
                auto truth = std::make_shared<SequenceNode>();
                auto repeated = std::make_shared<SequenceNode>();
                repeated->kind = SequenceKind::repetition;
                repeated->left = truth;
                repeated->minimum = 0;
                repeated->maximum = unbounded;
                return repeated;
            };
            auto prefix = binary(SequenceKind::concatenation, idle(),
                std::move(left));
            prefix->minimum = prefix->maximum = 1;
            auto padded = binary(SequenceKind::concatenation,
                std::move(prefix), idle());
            padded->minimum = padded->maximum = 1;
            left = binary(SequenceKind::intersection, std::move(padded),
                std::move(right));
        }
        return left;
    }

    [[nodiscard]] SequencePointer parse_throughout()
    {
        const auto start = position_;
        const auto end = boolean_end(position_);
        if (end < tokens_.size() && keyword_token(tokens_[end], "throughout")) {
            auto condition = boolean_(std::span<const Token> {
                tokens_.data() + start, end - start });
            if (!condition) {
                return nullptr;
            }
            position_ = end + 1U;
            auto operand = parse_throughout();
            if (!operand) {
                return nullptr;
            }
            auto node = std::make_shared<SequenceNode>();
            node->kind = SequenceKind::throughout;
            node->condition = std::move(condition);
            node->left = std::move(operand);
            return node;
        }
        return parse_concatenation();
    }

    [[nodiscard]] bool at_delay() const
    {
        return at(TokenKind::Hash) && at(TokenKind::Hash, 1);
    }

    // `##n`, `##[m:n]`, `##[*]`, `##[+]`.
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> delay()
    {
        position_ += 2U;
        if (at(TokenKind::LeftBracket)) {
            return bracket_range(position_, position_ + 1U);
        }
        if (position_ >= tokens_.size()) {
            return std::nullopt;
        }
        if (at(TokenKind::LeftParen)) {
            const auto close = matching_close(
                position_, TokenKind::LeftParen, TokenKind::RightParen);
            if (!close) {
                return std::nullopt;
            }
            const auto value = constant(std::span<const Token> {
                tokens_.data() + position_, *close + 1U - position_ });
            position_ = *close + 1U;
            if (!value || *value < 0) {
                return std::nullopt;
            }
            return std::pair { *value, *value };
        }
        const auto value = constant(std::span<const Token> {
            tokens_.data() + position_, 1U });
        ++position_;
        if (!value || *value < 0) {
            return std::nullopt;
        }
        return std::pair { *value, *value };
    }

    [[nodiscard]] SequencePointer parse_concatenation()
    {
        SequencePointer left;
        if (at_delay()) {
            const auto range = delay();
            auto right = range ? parse_repeated() : nullptr;
            if (!right) {
                return nullptr;
            }
            auto node = std::make_shared<SequenceNode>();
            node->kind = SequenceKind::concatenation;
            node->right = std::move(right);
            node->minimum = range->first;
            node->maximum = range->second;
            left = std::move(node);
        } else {
            left = parse_repeated();
        }
        while (left && at_delay()) {
            const auto range = delay();
            auto right = range ? parse_repeated() : nullptr;
            if (!right) {
                return nullptr;
            }
            auto node = std::make_shared<SequenceNode>();
            node->kind = SequenceKind::concatenation;
            node->left = std::move(left);
            node->right = std::move(right);
            node->minimum = range->first;
            node->maximum = range->second;
            left = std::move(node);
        }
        return left;
    }

    [[nodiscard]] SequencePointer parse_repeated()
    {
        auto operand = parse_primary();
        while (operand && at(TokenKind::LeftBracket)
            && (at(TokenKind::Star, 1) || at(TokenKind::Assign, 1)
                || at(TokenKind::ThinArrow, 1) || at(TokenKind::Plus, 1))) {
            auto kind = SequenceKind::repetition;
            std::size_t first_inside = position_ + 2U;
            if (at(TokenKind::Assign, 1)) {
                kind = SequenceKind::nonconsecutive_repetition;
            } else if (at(TokenKind::ThinArrow, 1)) {
                kind = SequenceKind::goto_repetition;
            } else if (at(TokenKind::Plus, 1)) {
                // `[+]` repeats one or more times.
                first_inside = position_ + 1U;
            } else if (at(TokenKind::RightBracket, 2)) {
                // `[*]` repeats zero or more times.
                first_inside = position_ + 1U;
            }
            const auto range = bracket_range(position_, first_inside);
            if (!range) {
                return nullptr;
            }
            if ((kind == SequenceKind::goto_repetition
                    || kind == SequenceKind::nonconsecutive_repetition)
                && operand->kind != SequenceKind::boolean) {
                return nullptr;
            }
            auto node = std::make_shared<SequenceNode>();
            node->kind = kind;
            node->left = std::move(operand);
            node->minimum = range->first;
            node->maximum = range->second;
            operand = std::move(node);
        }
        return operand;
    }

    // The end of a boolean operand that starts at `start`: the first token
    // at parenthesis depth zero that begins a sequence or property operator.
    [[nodiscard]] std::size_t boolean_end(std::size_t start) const
    {
        static const std::unordered_set<std::string_view> stops {
            "and", "or", "intersect", "within", "throughout", "iff",
            "until", "s_until", "until_with", "s_until_with", "implies",
            "else",
        };
        int depth { };
        for (auto index = start; index < tokens_.size(); ++index) {
            const auto& token = tokens_[index];
            const auto next = index + 1U < tokens_.size()
                ? tokens_[index + 1U].kind : TokenKind::EndOfFile;
            if (token.kind == TokenKind::LeftParen
                || token.kind == TokenKind::LeftBrace) {
                ++depth;
                continue;
            }
            if (token.kind == TokenKind::RightParen
                || token.kind == TokenKind::RightBrace) {
                if (depth == 0) {
                    return index;
                }
                --depth;
                continue;
            }
            if (token.kind == TokenKind::LeftBracket) {
                if (depth == 0
                    && (next == TokenKind::Star || next == TokenKind::Assign
                        || next == TokenKind::ThinArrow
                        || (next == TokenKind::Plus && index + 2U
                                < tokens_.size()
                            && tokens_[index + 2U].kind
                                == TokenKind::RightBracket))) {
                    return index;
                }
                ++depth;
                continue;
            }
            if (token.kind == TokenKind::RightBracket) {
                --depth;
                continue;
            }
            if (depth != 0) {
                continue;
            }
            if (token.kind == TokenKind::Hash
                || (token.kind == TokenKind::Pipe
                    && (next == TokenKind::ThinArrow
                        || next == TokenKind::Arrow))
                || (token.kind == TokenKind::PipeAssign
                    && next == TokenKind::Greater)
                || (token.kind == TokenKind::Identifier
                    && stops.contains(token.text))) {
                return index;
            }
        }
        return tokens_.size();
    }

    [[nodiscard]] SequencePointer instance(const SystemVerilogAssertionDeclaration&
            declaration,
        std::span<const Token> actual_tokens)
    {
        auto body = instance_body(declaration, actual_tokens);
        if (!body) {
            return nullptr;
        }
        PropertyParser inner { *body, unit_, boolean_ };
        inner.depth_ = depth_;
        return inner.parse_sequence_only();
    }

    // The declaration's expression with each formal replaced by its
    // parenthesized actual or default.
    [[nodiscard]] std::optional<std::vector<Token>> instance_body(
        const SystemVerilogAssertionDeclaration& declaration,
        std::span<const Token> actual_tokens)
    {
        if (++depth_ > 16U) {
            return std::nullopt;
        }
        std::vector<std::vector<Token>> actuals;
        if (!actual_tokens.empty()) {
            actuals = split_top_level(actual_tokens, TokenKind::Comma);
        }
        if (actuals.size() > declaration.formals.size()) {
            return std::nullopt;
        }
        std::vector<Token> body;
        for (const auto& token : declaration.expression_tokens) {
            if (token.kind == TokenKind::Semicolon) {
                continue;
            }
            const auto formal = token.kind == TokenKind::Identifier
                ? std::ranges::find_if(declaration.formals,
                      [&](const SystemVerilogAssertionFormal& entry) {
                          return entry.name == token.text;
                      })
                : declaration.formals.end();
            if (formal == declaration.formals.end()) {
                body.push_back(token);
                continue;
            }
            const auto ordinal = static_cast<std::size_t>(
                formal - declaration.formals.begin());
            const auto& replacement = ordinal < actuals.size()
                ? actuals[ordinal] : formal->default_tokens;
            if (replacement.empty()) {
                return std::nullopt;
            }
            Token open = token;
            open.kind = TokenKind::LeftParen;
            open.text = "(";
            Token close = token;
            close.kind = TokenKind::RightParen;
            close.text = ")";
            body.push_back(open);
            body.insert(body.end(), replacement.begin(), replacement.end());
            body.push_back(close);
        }
        return body;
    }

    // A whole operand naming an unclocked property declaration.
    [[nodiscard]] PropertyPointer property_instance()
    {
        if (position_ >= tokens_.size()
            || tokens_[position_].kind != TokenKind::Identifier) {
            return nullptr;
        }
        const auto declaration = std::ranges::find_if(
            unit_.systemverilog_assertion_declarations,
            [&](const SystemVerilogAssertionDeclaration& entry) {
                return entry.kind
                    == SystemVerilogAssertionDeclarationKind::Property
                    && entry.name == tokens_[position_].text;
            });
        if (declaration == unit_.systemverilog_assertion_declarations.end()
            || declaration->clock || declaration->disable
            || !declaration->local_variables.empty()) {
            return nullptr;
        }
        std::span<const Token> actuals;
        auto next = position_ + 1U;
        if (at(TokenKind::LeftParen, 1)) {
            const auto close = matching_close(position_ + 1U,
                TokenKind::LeftParen, TokenKind::RightParen);
            if (!close) {
                return nullptr;
            }
            actuals = std::span<const Token> {
                tokens_.data() + position_ + 2U, *close - position_ - 2U };
            next = *close + 1U;
        }
        if (next != tokens_.size()) {
            return nullptr;
        }
        auto body = instance_body(*declaration, actuals);
        if (!body) {
            return nullptr;
        }
        PropertyParser inner { *body, unit_, boolean_ };
        inner.depth_ = depth_;
        auto property = inner.parse_property();
        if (!property || inner.position_ != inner.tokens_.size()) {
            return nullptr;
        }
        position_ = next;
        return property;
    }

    [[nodiscard]] SequencePointer parse_primary()
    {
        if (position_ >= tokens_.size()) {
            return nullptr;
        }
        if (at_keyword("first_match") && at(TokenKind::LeftParen, 1)) {
            const auto close = matching_close(
                position_ + 1U, TokenKind::LeftParen, TokenKind::RightParen);
            if (!close) {
                return nullptr;
            }
            PropertyParser inner { std::span<const Token> {
                                       tokens_.data() + position_ + 2U,
                                       *close - position_ - 2U },
                unit_, boolean_ };
            position_ = *close + 1U;
            return inner.parse_sequence_only();
        }
        // A named sequence, with or without actual arguments.
        if (tokens_[position_].kind == TokenKind::Identifier) {
            const auto declaration = std::ranges::find_if(
                unit_.systemverilog_assertion_declarations,
                [&](const SystemVerilogAssertionDeclaration& entry) {
                    return entry.kind
                        == SystemVerilogAssertionDeclarationKind::Sequence
                        && entry.name == tokens_[position_].text;
                });
            if (declaration != unit_.systemverilog_assertion_declarations.end()) {
                if (declaration->clock || declaration->disable) {
                    return nullptr;
                }
                std::span<const Token> actuals;
                auto next = position_ + 1U;
                if (at(TokenKind::LeftParen, 1)) {
                    const auto close = matching_close(position_ + 1U,
                        TokenKind::LeftParen, TokenKind::RightParen);
                    if (!close) {
                        return nullptr;
                    }
                    actuals = std::span<const Token> {
                        tokens_.data() + position_ + 2U,
                        *close - position_ - 2U };
                    next = *close + 1U;
                }
                auto sequence = instance(*declaration, actuals);
                position_ = next;
                return sequence;
            }
        }
        // A parenthesized sequence; a parenthesized boolean is an operand
        // of the boolean expression that follows.
        if (at(TokenKind::LeftParen)) {
            const auto close = matching_close(
                position_, TokenKind::LeftParen, TokenKind::RightParen);
            if (!close) {
                return nullptr;
            }
            const std::span<const Token> inside {
                tokens_.data() + position_ + 1U, *close - position_ - 1U };
            if (boolean_end(*close + 1U) == *close + 1U
                && !boolean_(inside)) {
                PropertyParser inner { inside, unit_, boolean_ };
                inner.depth_ = depth_;
                auto sequence = inner.parse_sequence_only();
                if (sequence) {
                    position_ = *close + 1U;
                    return sequence;
                }
            }
        }
        const auto end = boolean_end(position_);
        if (end == position_) {
            return nullptr;
        }
        auto condition = boolean_(std::span<const Token> {
            tokens_.data() + position_, end - position_ });
        if (!condition) {
            return nullptr;
        }
        position_ = end;
        auto node = std::make_shared<SequenceNode>();
        node->condition = std::move(condition);
        return node;
    }

    std::vector<Token> tokens_;
    const DesignUnit& unit_;
    std::function<std::optional<Expression>(std::span<const Token>)> boolean_;
    std::size_t position_ { };
    std::size_t depth_ { };
};

// The automaton: consuming transitions carry a guard (true when empty);
// epsilon transitions consume nothing.
struct Automaton {
    struct Transition {
        std::size_t from { };
        std::size_t to { };
        std::optional<Expression> guard;
    };
    std::size_t states { };
    std::vector<Transition> transitions;
    std::vector<std::pair<std::size_t, std::size_t>> epsilons;
    bool overflow { };

    std::size_t add()
    {
        if (states >= maximum_states) {
            overflow = true;
        }
        return states++;
    }

    [[nodiscard]] std::set<std::size_t> closure(std::size_t state) const
    {
        std::set<std::size_t> result { state };
        std::vector<std::size_t> pending { state };
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            for (const auto& [from, to] : epsilons) {
                if (from == current && result.insert(to).second) {
                    pending.push_back(to);
                }
            }
        }
        return result;
    }
};

struct Fragment {
    std::size_t start { };
    std::size_t end { };
};

[[nodiscard]] std::optional<Expression> conjunction(
    const std::optional<Expression>& left,
    const std::optional<Expression>& right, const SourceSpan& span)
{
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }
    return Expression { ExpressionKind::Binary, "&&", { *left, *right }, span };
}

class AutomatonBuilder {
public:
    AutomatonBuilder(Automaton& automaton, SourceSpan span)
        : automaton_(automaton)
        , span_(std::move(span))
    {
    }

    [[nodiscard]] std::optional<Fragment> build(
        const SequenceNode& node, const std::optional<Expression>& guard)
    {
        if (automaton_.overflow) {
            return std::nullopt;
        }
        switch (node.kind) {
        case SequenceKind::boolean:
            return consume(conjunction(guard, node.condition, span_));
        case SequenceKind::throughout:
            return build(*node.left, conjunction(guard, node.condition, span_));
        case SequenceKind::alternation: {
            const auto left = build(*node.left, guard);
            const auto right = build(*node.right, guard);
            if (!left || !right) {
                return std::nullopt;
            }
            const Fragment result { automaton_.add(), automaton_.add() };
            automaton_.epsilons.emplace_back(result.start, left->start);
            automaton_.epsilons.emplace_back(result.start, right->start);
            automaton_.epsilons.emplace_back(left->end, result.end);
            automaton_.epsilons.emplace_back(right->end, result.end);
            return result;
        }
        case SequenceKind::conjunction:
        case SequenceKind::intersection:
            return product(node, guard);
        case SequenceKind::concatenation: {
            const auto left = node.left
                ? build(*node.left, guard) : consume(guard);
            const auto right = build(*node.right, guard);
            if (!left || !right) {
                return std::nullopt;
            }
            return delay(*left, *right, node.minimum, node.maximum, guard);
        }
        case SequenceKind::repetition:
            return repeat(*node.left, node.minimum, node.maximum, guard);
        case SequenceKind::goto_repetition:
        case SequenceKind::nonconsecutive_repetition: {
            // b[->n] is (!b[*0:$] ##1 b)[*n]; b[=n] continues with !b[*0:$].
            const auto& condition = *node.left->condition;
            Expression negated { ExpressionKind::Unary, "!", { condition },
                span_ };
            SequenceNode wait;
            wait.kind = SequenceKind::boolean;
            wait.condition = negated;
            SequenceNode hit;
            hit.kind = SequenceKind::boolean;
            hit.condition = condition;
            auto goto_unit = std::make_shared<SequenceNode>();
            goto_unit->kind = SequenceKind::concatenation;
            auto waiting = std::make_shared<SequenceNode>();
            waiting->kind = SequenceKind::repetition;
            waiting->left = std::make_shared<SequenceNode>(wait);
            waiting->minimum = 0;
            waiting->maximum = unbounded;
            goto_unit->left = waiting;
            goto_unit->right = std::make_shared<SequenceNode>(hit);
            goto_unit->minimum = 1;
            goto_unit->maximum = 1;
            auto repeated = repeat(*goto_unit, node.minimum, node.maximum, guard);
            if (!repeated
                || node.kind == SequenceKind::goto_repetition) {
                return repeated;
            }
            const auto tail = automaton_.add();
            const auto end = automaton_.add();
            automaton_.epsilons.emplace_back(repeated->end, end);
            automaton_.transitions.push_back(
                { repeated->end, tail, conjunction(guard, negated, span_) });
            automaton_.transitions.push_back(
                { tail, tail, conjunction(guard, negated, span_) });
            automaton_.epsilons.emplace_back(tail, end);
            return Fragment { repeated->start, end };
        }
        }
        return std::nullopt;
    }

private:
    struct Move {
        std::size_t to { };
        std::optional<Expression> guard;
        bool accepting { };
    };
    static constexpr std::size_t done = std::numeric_limits<std::size_t>::max();

    [[nodiscard]] static std::vector<Move> moves(const Automaton& automaton,
        std::size_t state, std::size_t end)
    {
        std::vector<Move> result;
        for (const auto source : automaton.closure(state)) {
            for (const auto& transition : automaton.transitions) {
                if (transition.from == source) {
                    result.push_back({ transition.to, transition.guard,
                        automaton.closure(transition.to).contains(end) });
                }
            }
        }
        return result;
    }

    // `left and right` and `left intersect right`: both operands advance on
    // the same ticks. With `and`, an operand that has matched stays matched
    // while the other continues; `intersect` requires equal lengths.
    [[nodiscard]] std::optional<Fragment> product(const SequenceNode& node,
        const std::optional<Expression>& guard)
    {
        Automaton left_automaton;
        Automaton right_automaton;
        AutomatonBuilder left_builder { left_automaton, span_ };
        AutomatonBuilder right_builder { right_automaton, span_ };
        const auto left = left_builder.build(*node.left, std::nullopt);
        const auto right = right_builder.build(*node.right, std::nullopt);
        if (!left || !right || left_automaton.overflow
            || right_automaton.overflow) {
            return std::nullopt;
        }
        const bool conjunctive = node.kind == SequenceKind::conjunction;
        const Fragment result { automaton_.add(), automaton_.add() };
        std::map<std::pair<std::size_t, std::size_t>, std::size_t> states;
        std::vector<std::pair<std::size_t, std::size_t>> pending;
        const auto state = [&](std::size_t a, std::size_t b) {
            const auto [entry, inserted] = states.try_emplace(
                std::pair { a, b }, 0U);
            if (inserted) {
                entry->second = automaton_.add();
                pending.emplace_back(a, b);
            }
            return entry->second;
        };
        states.emplace(std::pair { left->start, right->start }, result.start);
        pending.emplace_back(left->start, right->start);
        while (!pending.empty() && !automaton_.overflow) {
            const auto [a, b] = pending.back();
            pending.pop_back();
            const auto from = states.at({ a, b });
            const auto a_moves = a == done
                ? std::vector<Move> { Move { done, std::nullopt, true } }
                : moves(left_automaton, a, left->end);
            const auto b_moves = b == done
                ? std::vector<Move> { Move { done, std::nullopt, true } }
                : moves(right_automaton, b, right->end);
            for (const auto& a_move : a_moves) {
                for (const auto& b_move : b_moves) {
                    const auto combined = conjunction(guard,
                        conjunction(a_move.guard, b_move.guard, span_), span_);
                    if (a_move.accepting && b_move.accepting) {
                        automaton_.transitions.push_back(
                            { from, result.end, combined });
                    }
                    if (a_move.to != done && b_move.to != done) {
                        automaton_.transitions.push_back(
                            { from, state(a_move.to, b_move.to), combined });
                    }
                    if (conjunctive && a_move.accepting && b_move.to != done) {
                        automaton_.transitions.push_back(
                            { from, state(done, b_move.to), combined });
                    }
                    if (conjunctive && b_move.accepting && a_move.to != done) {
                        automaton_.transitions.push_back(
                            { from, state(a_move.to, done), combined });
                    }
                }
            }
        }
        if (automaton_.overflow) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] Fragment consume(const std::optional<Expression>& guard)
    {
        const Fragment result { automaton_.add(), automaton_.add() };
        automaton_.transitions.push_back({ result.start, result.end, guard });
        return result;
    }

    // `left ##[minimum:maximum] right`.
    [[nodiscard]] std::optional<Fragment> delay(const Fragment& left,
        const Fragment& right, const std::int64_t minimum,
        const std::int64_t maximum, const std::optional<Expression>& guard)
    {
        if (minimum == 0) {
            fuse(left, right);
        }
        if (maximum == 0) {
            return Fragment { left.start, right.end };
        }
        const auto first = std::max<std::int64_t>(minimum, 1);
        // Chain state k holds the position k ticks after the left match.
        auto previous = left.end;
        const auto chain_end = maximum == unbounded ? first : maximum;
        if (chain_end > static_cast<std::int64_t>(maximum_states)) {
            automaton_.overflow = true;
            return std::nullopt;
        }
        for (std::int64_t step = 1; step <= chain_end; ++step) {
            if (step >= first) {
                automaton_.epsilons.emplace_back(previous, right.start);
            }
            if (step == chain_end) {
                if (maximum == unbounded) {
                    automaton_.transitions.push_back(
                        { previous, previous, guard });
                }
                break;
            }
            const auto next = automaton_.add();
            automaton_.transitions.push_back({ previous, next, guard });
            previous = next;
        }
        return Fragment { left.start, right.end };
    }

    // `left ##0 right`: the last tick of left is the first tick of right.
    void fuse(const Fragment& left, const Fragment& right)
    {
        const auto into_end = [&](std::size_t state) {
            return automaton_.closure(state).contains(left.end);
        };
        const auto right_start = automaton_.closure(right.start);
        const auto transitions = automaton_.transitions;
        for (const auto& last : transitions) {
            if (!into_end(last.to)) {
                continue;
            }
            for (const auto& first : transitions) {
                if (!right_start.contains(first.from)) {
                    continue;
                }
                automaton_.transitions.push_back({ last.from, first.to,
                    conjunction(last.guard, first.guard, span_) });
            }
        }
    }

    [[nodiscard]] std::optional<Fragment> repeat(const SequenceNode& operand,
        const std::int64_t minimum, const std::int64_t maximum,
        const std::optional<Expression>& guard)
    {
        if (minimum > static_cast<std::int64_t>(maximum_states)
            || (maximum != unbounded
                && maximum > static_cast<std::int64_t>(maximum_states))) {
            automaton_.overflow = true;
            return std::nullopt;
        }
        const auto start = automaton_.add();
        const auto end = automaton_.add();
        if (minimum == 0) {
            automaton_.epsilons.emplace_back(start, end);
        }
        auto previous = start;
        const auto copies = maximum == unbounded
            ? std::max<std::int64_t>(minimum, 1) : maximum;
        for (std::int64_t count = 1; count <= copies; ++count) {
            const auto copy = build(operand, guard);
            if (!copy) {
                return std::nullopt;
            }
            automaton_.epsilons.emplace_back(previous, copy->start);
            if (count >= minimum) {
                automaton_.epsilons.emplace_back(copy->end, end);
            }
            if (maximum == unbounded && count == copies) {
                automaton_.epsilons.emplace_back(copy->end, copy->start);
            }
            previous = copy->end;
        }
        return Fragment { start, end };
    }

    Automaton& automaton_;
    SourceSpan span_;
};

// Every directive owns one process that advances all of its attempts once
// per clock tick. Attempts live in 64 slots, one per start tick modulo 64:
// each automaton state is a 64-bit vector of the slots positioned there, and
// each slot counts the attempts that started together. Slots are reused
// after 64 ticks, which abandons an attempt still running by then.
class EvaluationBuilder {
public:
    using TextParser
        = std::function<std::optional<Statement>(const std::string&)>;

    EvaluationBuilder(const GeneralPropertyActions& actions, TextParser parse)
        : actions_(actions)
        , parse_(std::move(parse))
    {
    }

    [[nodiscard]] std::optional<Statement> property(const PropertyNode& node)
    {
        // New attempts start through a forked branch so that assertion
        // control ($assertoff, $assertkill) stops them; running attempts
        // continue (IEEE 1800-2017 20.12).
        std::string steps = "__fsim_pnew = 0; fork __fsim_pnew = 1; join ";
        if (!step(node, "__fsim_pnew", steps)) {
            return std::nullopt;
        }
        // Outcomes are counted while the attempts advance and their actions
        // run afterwards: reads after the first action are not sampled.
        std::string text = "begin int unsigned __fsim_pt, __fsim_ps, "
                           "__fsim_pnew, "
                           "__fsim_rs, __fsim_rf, __fsim_rv; "
                           "bit [63:0] __fsim_po; "
            + declarations_
            + "__fsim_ps = __fsim_pt % 64; __fsim_pt = __fsim_pt + 1; "
              "__fsim_po = 64'd1 << __fsim_ps; __fsim_rs = 0; "
              "__fsim_rf = 0; __fsim_rv = 0; ";
        if (actions_.disable) {
            text += "if (__fsim_disable) begin ";
            for (const auto& name : cleared_) {
                text += name + " = 0; ";
            }
            text += "__fsim_act_abort(); end else begin " + steps + "end ";
        } else {
            text += steps;
        }
        text += "repeat (__fsim_rv) __fsim_act_vacuous(); "
                "repeat (__fsim_rs) __fsim_act_success(); "
                "repeat (__fsim_rf) __fsim_act_failure(); end";
        // Each directive's process gets its own state names.
        for (const std::string_view stem : { "__fsim_p", "__fsim_r" }) {
            const auto replacement = actions_.name_prefix + "_"
                + std::string { stem.substr(7U) };
            for (auto at = text.find(stem); at != std::string::npos;
                 at = text.find(stem, at + replacement.size())) {
                text.replace(at, stem.size(), replacement);
            }
        }
        auto statement = parse_(text);
        if (!statement) {
            return std::nullopt;
        }
        if (!substitute(*statement)) {
            return std::nullopt;
        }
        return statement;
    }

private:
    enum class Report { success, failure, vacuous };

    // The counter of `report` outcomes in this tick.
    [[nodiscard]] static std::string report_name(Report report)
    {
        switch (report) {
        case Report::success:
            return "__fsim_rs";
        case Report::failure:
            return "__fsim_rf";
        case Report::vacuous:
            return "__fsim_rv";
        }
        return "";
    }

    std::string fresh(std::string_view stem)
    {
        return "__fsim_p" + std::to_string(next_name_++) + std::string { stem };
    }

    std::string guard(const Expression& expression)
    {
        const auto name = "__fsim_g" + std::to_string(guards_.size());
        guards_.push_back(expression);
        return name;
    }

    // Emits the steps of `node` for `start` new attempts this tick.
    bool step(const PropertyNode& node, const std::string& start,
        std::string& out)
    {
        switch (node.kind) {
        case PropertyKind::sequence:
            return sequence(*node.sequence, start, true, Report::success,
                Report::failure, out, nullptr);
        case PropertyKind::negation:
            if (node.first->kind != PropertyKind::sequence) {
                return false;
            }
            return sequence(*node.first->sequence, start, true,
                Report::failure, Report::success, out, nullptr);
        case PropertyKind::conditional: {
            const auto first = fresh("_k1");
            const auto second = fresh("_k2");
            const auto vacuous = fresh("_kv");
            declarations_ += "int unsigned " + first + ", " + second + ", "
                + vacuous + "; ";
            out += first + " = 0; " + second + " = 0; " + vacuous + " = 0; ";
            out += "if (" + start + " != 0) begin if ("
                + guard(*node.condition) + ") " + first + " = " + start
                + "; else " + (node.second ? second : vacuous) + " = "
                + start + "; end ";
            out += report_name(Report::vacuous) + " = "
                + report_name(Report::vacuous) + " + " + vacuous + "; ";
            if (!step(*node.first, first, out)) {
                return false;
            }
            return !node.second || step(*node.second, second, out);
        }
        case PropertyKind::implication:
        case PropertyKind::followed_by: {
            // A nonoverlapped sequence consequent is `##1 consequent` from
            // the matching tick; another consequent starts a tick later.
            PropertyNode consequent = *node.first;
            bool delayed = node.nonoverlapped;
            const auto shifted = [&](const SequencePointer& sequence) {
                auto result = std::make_shared<SequenceNode>();
                result->kind = SequenceKind::concatenation;
                result->right = sequence;
                result->minimum = 1;
                result->maximum = 1;
                return result;
            };
            if (delayed && consequent.kind == PropertyKind::sequence) {
                consequent.sequence = shifted(consequent.sequence);
                delayed = false;
            } else if (delayed && consequent.kind == PropertyKind::negation
                && consequent.first->kind == PropertyKind::sequence) {
                auto inner = std::make_shared<PropertyNode>(*consequent.first);
                inner->sequence = shifted(inner->sequence);
                consequent.first = std::move(inner);
                delayed = false;
            }
            std::string matches;
            if (!sequence(*node.sequence, start, false, Report::success,
                    node.kind == PropertyKind::implication
                        ? Report::vacuous
                        : Report::failure,
                    out, &matches)) {
                return false;
            }
            auto consequent_start = matches;
            if (delayed) {
                const auto previous = fresh("_dp");
                const auto current = fresh("_dc");
                declarations_ += "int unsigned " + previous + ", " + current
                    + "; ";
                cleared_.push_back(previous);
                out += current + " = " + previous + "; " + previous + " = "
                    + matches + "; ";
                consequent_start = current;
            }
            return step(consequent, consequent_start, out);
        }
        }
        return false;
    }

    // Emits one automaton. With `first_match` an attempt reports `on_match`
    // at its first match and ends; otherwise every match adds the attempt's
    // count to `matches` and `on_dead` reports attempts that end without a
    // match.
    bool sequence(const SequenceNode& node, const std::string& start,
        bool first_match, Report on_match, Report on_dead, std::string& out,
        std::string* matches)
    {
        Automaton automaton;
        AutomatonBuilder builder { automaton, actions_.span };
        const auto fragment = builder.build(node, std::nullopt);
        if (!fragment || automaton.overflow) {
            return false;
        }
        std::set<std::size_t> live;
        {
            std::vector<std::size_t> pending;
            for (const auto entry : automaton.closure(fragment->start)) {
                if (live.insert(entry).second) {
                    pending.push_back(entry);
                }
            }
            while (!pending.empty()) {
                const auto current = pending.back();
                pending.pop_back();
                for (const auto& transition : automaton.transitions) {
                    if (transition.from != current) {
                        continue;
                    }
                    for (const auto entry : automaton.closure(transition.to)) {
                        if (live.insert(entry).second) {
                            pending.push_back(entry);
                        }
                    }
                }
            }
        }
        std::vector<std::size_t> sources;
        for (const auto index : live) {
            if (std::ranges::any_of(automaton.transitions,
                    [&](const auto& transition) {
                        return transition.from == index;
                    })) {
                sources.push_back(index);
            }
        }
        const auto prefix = fresh("");
        const auto state = [&](std::size_t index) {
            return prefix + "_s" + std::to_string(index);
        };
        const auto next = [&](std::size_t index) {
            return prefix + "_n" + std::to_string(index);
        };
        const auto accept = prefix + "_acc";
        const auto active = prefix + "_act";
        const auto ever = prefix + "_ever";
        const auto remaining = prefix + "_live";
        const auto dead = prefix + "_dead";
        const auto count = prefix + "_cnt";
        const auto index = prefix + "_j";
        declarations_ += "bit [63:0] " + accept + ", " + active + ", " + ever
            + ", " + remaining + ", " + dead;
        for (const auto source : sources) {
            declarations_ += ", " + state(source) + ", " + next(source);
            cleared_.push_back(state(source));
        }
        declarations_ += "; int unsigned " + count + " [0:63]; int "
            + index + "; ";
        cleared_.push_back(active);
        cleared_.push_back(ever);

        out += "if (" + start + " != 0) begin ";
        for (const auto source : sources) {
            out += state(source) + " = " + state(source) + " & ~__fsim_po; ";
        }
        out += count + "[__fsim_ps] = " + start + "; " + active + " = "
            + active + " | __fsim_po; " + ever + " = " + ever
            + " & ~__fsim_po; ";
        const std::set<std::size_t> declared(sources.begin(), sources.end());
        for (const auto entry : automaton.closure(fragment->start)) {
            if (declared.contains(entry)) {
                out += state(entry) + " = " + state(entry) + " | __fsim_po; ";
            }
        }
        out += "end ";
        for (const auto source : sources) {
            out += next(source) + " = 0; ";
        }
        out += accept + " = 0; ";
        for (const auto& transition : automaton.transitions) {
            if (!declared.contains(transition.from)) {
                continue;
            }
            std::string effects;
            for (const auto target : automaton.closure(transition.to)) {
                if (declared.contains(target)) {
                    effects += next(target) + " = " + next(target) + " | "
                        + state(transition.from) + "; ";
                }
            }
            if (automaton.closure(transition.to).contains(fragment->end)) {
                effects += accept + " = " + accept + " | "
                    + state(transition.from) + "; ";
            }
            if (effects.empty()) {
                continue;
            }
            if (transition.guard) {
                out += "if (" + guard(*transition.guard) + ") begin " + effects
                    + "end ";
            } else {
                out += effects;
            }
        }
        for (const auto source : sources) {
            out += state(source) + " = " + next(source) + "; ";
        }
        out += accept + " = " + accept + " & " + active + "; ";
        const auto for_each = [&](const std::string& mask,
                                  const std::string& body) {
            return "if (" + mask + " != 0) for (" + index + " = 0; " + index
                + " < 64; " + index + "++) if (" + mask + "[" + index
                + "]) begin " + body + "end ";
        };
        const auto repeated = [&](Report report) {
            return report_name(report) + " = " + report_name(report) + " + "
                + count + "[" + index + "]; ";
        };
        if (first_match) {
            out += for_each(accept, repeated(on_match));
            for (const auto source : sources) {
                out += state(source) + " = " + state(source) + " & ~" + accept
                    + "; ";
            }
            out += active + " = " + active + " & ~" + accept + "; ";
        } else {
            const auto total = prefix + "_k";
            declarations_ += "int unsigned " + total + "; ";
            out += total + " = 0; ";
            out += for_each(accept,
                total + " = " + total + " + " + count + "[" + index + "]; ");
            out += ever + " = " + ever + " | " + accept + "; ";
            *matches = total;
        }
        out += remaining + " = 0";
        for (const auto source : sources) {
            out += " | " + state(source);
        }
        out += "; " + dead + " = " + active + " & ~" + remaining + "; ";
        out += for_each(dead,
            first_match ? repeated(on_dead)
                        : "if (!" + ever + "[" + index + "]) "
                    + repeated(on_dead));
        out += active + " = " + active + " & " + remaining + "; ";
        return true;
    }

    // Replaces the guard placeholders with their expressions and the
    // action placeholders with the actions.
    bool substitute(Expression& expression)
    {
        if (expression.kind == ExpressionKind::Identifier) {
            if (expression.text == "__fsim_disable") {
                expression = *actions_.disable;
                return true;
            }
            constexpr std::string_view guard_prefix { "__fsim_g" };
            if (expression.text.starts_with(guard_prefix)) {
                const auto ordinal = std::stoul(
                    expression.text.substr(guard_prefix.size()));
                if (ordinal >= guards_.size()) {
                    return false;
                }
                expression = guards_[ordinal];
                return true;
            }
        }
        for (auto& operand : expression.operands) {
            if (!substitute(operand)) {
                return false;
            }
        }
        return true;
    }

    bool substitute(Statement& statement)
    {
        if (statement.kind == StatementKind::TaskCall
            && statement.task_name.starts_with("__fsim_act_")) {
            const auto which = statement.task_name.substr(11U);
            const auto& actions = which == "success" ? actions_.success
                : which == "failure"                 ? actions_.failure
                : which == "vacuous"                 ? actions_.vacuous
                                                     : actions_.abort;
            Statement replacement;
            replacement.kind = StatementKind::Block;
            replacement.span = actions_.span;
            replacement.statements.assign(actions.begin(), actions.end());
            statement = std::move(replacement);
            return true;
        }
        if (!substitute(statement.condition)) {
            return false;
        }
        for (auto& child : statement.statements) {
            if (!substitute(child)) {
                return false;
            }
        }
        for (auto& child : statement.else_statements) {
            if (!substitute(child)) {
                return false;
            }
        }
        return true;
    }

    const GeneralPropertyActions& actions_;
    TextParser parse_;
    std::size_t next_name_ { };
    std::vector<Expression> guards_;
    std::string declarations_;
    std::vector<std::string> cleared_;
};

} // namespace

std::optional<Statement> VerilogParser::general_property_evaluation(
    const std::span<const Token> tokens, const DesignUnit& unit,
    const GeneralPropertyActions& actions)
{
    PropertyParser parser { tokens, unit,
        [this](std::span<const Token> operand) {
            return assertion_expression(operand);
        } };
    const auto property = parser.parse();
    if (!property) {
        return std::nullopt;
    }
    EvaluationBuilder builder { actions,
        [this, &actions](const std::string& text) {
            return parse_generated_statement(
                synthetic_tokens(text, actions.span), actions.span);
        } };
    return builder.property(*property);
}

} // namespace fsim::frontend
