// SPDX-License-Identifier: Apache-2.0
//
// Concurrent assertion directives (IEEE 1800-2017 16.7-16.15).
//
// A directive's property is parsed into a tree that follows the operator
// precedence of Table 16-3, compiled into a semantic::sva::Plan, and
// evaluated by a runtime monitor (runtime::sva::Monitor). The directive
// becomes one always process that waits for the clocking event, samples the
// plan's boolean guards, advances the monitor, and runs the action of each
// attempt that completed.

#include "verilog_parser_internal.hpp"
#include "verilog_parser_assertion_support.hpp"

#include "fsim/semantic/sva_plan.hpp"
#include "fsim/support/environment.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <string>

namespace fsim::frontend {
namespace {

namespace plan = semantic::sva;

struct BoundSpec {
    enum class Kind : std::uint8_t { literal, expression, unbounded };
    Kind kind { Kind::literal };
    std::int64_t value { };
    std::vector<Token> tokens;
};

enum class NodeKind : std::uint8_t {
    boolean,
    delay,
    repeat,
    goto_repeat,
    nonconsecutive_repeat,
    sequence_or,
    sequence_and,
    intersect,
    within,
    throughout,
    first_match,
    strong,
    weak,
    negation,
    conjunction,
    disjunction,
    equivalence,
    implies,
    implication,
    followed_by,
    conditional,
    nexttime,
    always,
    eventually,
    until,
    accept,
    reject,
};

struct Node {
    NodeKind kind { NodeKind::boolean };
    std::vector<std::size_t> operands;
    // A boolean, a throughout or abort condition, or a conditional guard.
    std::vector<Token> expression;
    BoundSpec lo;
    BoundSpec hi;
    bool strong { };
    bool overlapped { };
    bool with { };
    bool synchronous { };
    // A delay with no left operand (`##n s`).
    bool leading { };
};

[[nodiscard]] bool sequence_kind(const NodeKind kind)
{
    switch (kind) {
    case NodeKind::boolean:
    case NodeKind::delay:
    case NodeKind::repeat:
    case NodeKind::goto_repeat:
    case NodeKind::nonconsecutive_repeat:
    case NodeKind::sequence_or:
    case NodeKind::sequence_and:
    case NodeKind::intersect:
    case NodeKind::within:
    case NodeKind::throughout:
    case NodeKind::first_match:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool token_is_keyword(const Token& token, const std::string_view text)
{
    return token.kind == TokenKind::Identifier && token.text == text;
}

[[nodiscard]] std::string spelling(const std::span<const Token> tokens)
{
    std::string result;
    for (const auto& token : tokens) {
        if (!result.empty()) {
            result += ' ';
        }
        result += token.text;
    }
    return result;
}

[[nodiscard]] Token punctuation(const Token& model, const TokenKind kind,
    const std::string_view text)
{
    Token token = model;
    token.kind = kind;
    token.text = std::string { text };
    return token;
}

struct ParseContext {
    const DesignUnit& unit;
    std::function<bool(std::span<const Token>)> boolean;
    // The directive's clocking event (the tokens inside `@( )`).
    std::vector<Token> clock;
    // The scope's default disable condition, `1'b0` when none (16.14.7).
    std::vector<Token> inferred_disable;
    std::string failure;
};

// Builds the property tree of one token sequence; a named sequence or
// property instance is expanded by a nested parser over its body.
class TreeParser {
public:
    TreeParser(std::vector<Node>& nodes, std::vector<Token> tokens,
        ParseContext& context, const std::size_t depth)
        : nodes_(nodes)
        , tokens_(std::move(tokens))
        , context_(context)
        , depth_(depth)
    {
    }

    [[nodiscard]] std::optional<std::size_t> parse()
    {
        auto result = implication();
        if (result && position_ != tokens_.size()) {
            return fail("unexpected tokens after the property: "
                + spelling(std::span<const Token> { tokens_ }.subspan(
                    position_)));
        }
        return result;
    }

private:
    std::optional<std::size_t> fail(std::string reason)
    {
        if (context_.failure.empty()) {
            context_.failure = std::move(reason);
        }
        return std::nullopt;
    }

    [[nodiscard]] bool at(const TokenKind kind, const std::size_t offset = 0) const
    {
        return position_ + offset < tokens_.size()
            && tokens_[position_ + offset].kind == kind;
    }
    [[nodiscard]] bool at_keyword(const std::string_view text,
        const std::size_t offset = 0) const
    {
        return position_ + offset < tokens_.size()
            && token_is_keyword(tokens_[position_ + offset], text);
    }
    [[nodiscard]] std::optional<std::size_t> matching(
        const std::size_t open) const
    {
        const auto left = tokens_[open].kind;
        const auto right = left == TokenKind::LeftParen ? TokenKind::RightParen
            : left == TokenKind::LeftBracket            ? TokenKind::RightBracket
                                                        : TokenKind::RightBrace;
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
    [[nodiscard]] std::vector<Token> slice(const std::size_t begin,
        const std::size_t end) const
    {
        return { tokens_.begin() + static_cast<std::ptrdiff_t>(begin),
            tokens_.begin() + static_cast<std::ptrdiff_t>(end) };
    }

    std::size_t add(Node node)
    {
        nodes_.push_back(std::move(node));
        return nodes_.size() - 1U;
    }

    [[nodiscard]] bool is_sequence(const std::size_t node) const
    {
        return sequence_kind(nodes_[node].kind);
    }

    // `|->`, and `|=>` which lexes as `|=` then `>`.
    [[nodiscard]] bool at_implication() const
    {
        return (at(TokenKind::Pipe) && at(TokenKind::ThinArrow, 1))
            || (at(TokenKind::PipeAssign) && at(TokenKind::Greater, 1));
    }
    [[nodiscard]] bool at_followed_by() const
    {
        return at(TokenKind::Hash)
            && (at(TokenKind::Minus, 1) || at(TokenKind::Assign, 1))
            && at(TokenKind::Hash, 2);
    }

    std::optional<std::size_t> implication()
    {
        auto left = until();
        if (!left) {
            return left;
        }
        const bool implies = at_implication();
        if (!implies && !at_followed_by()) {
            return left;
        }
        if (!is_sequence(*left)) {
            return fail("the antecedent of an implication must be a sequence");
        }
        Node node;
        node.kind = implies ? NodeKind::implication : NodeKind::followed_by;
        node.overlapped = implies ? at(TokenKind::Pipe)
                                  : at(TokenKind::Minus, 1);
        position_ += implies ? 2U : 3U;
        auto right = implication();
        if (!right) {
            return right;
        }
        node.operands = { *left, *right };
        return add(std::move(node));
    }

    std::optional<std::size_t> until()
    {
        auto left = iff();
        if (!left) {
            return left;
        }
        static constexpr std::array<std::string_view, 5> operators {
            "until", "s_until", "until_with", "s_until_with", "implies"
        };
        const auto found = std::ranges::find_if(operators,
            [&](const std::string_view text) { return at_keyword(text); });
        if (found == operators.end()) {
            return left;
        }
        ++position_;
        auto right = until();
        if (!right) {
            return right;
        }
        Node node;
        if (*found == "implies") {
            node.kind = NodeKind::implies;
        } else {
            node.kind = NodeKind::until;
            node.strong = found->starts_with("s_");
            node.with = found->ends_with("_with");
        }
        node.operands = { *left, *right };
        return add(std::move(node));
    }

    std::optional<std::size_t> iff()
    {
        auto left = disjunction();
        if (!left || !at_keyword("iff")) {
            return left;
        }
        ++position_;
        auto right = iff();
        if (!right) {
            return right;
        }
        Node node;
        node.kind = NodeKind::equivalence;
        node.operands = { *left, *right };
        return add(std::move(node));
    }

    std::optional<std::size_t> disjunction()
    {
        auto left = conjunction();
        while (left && at_keyword("or")) {
            ++position_;
            auto right = conjunction();
            if (!right) {
                return right;
            }
            Node node;
            node.kind = is_sequence(*left) && is_sequence(*right)
                ? NodeKind::sequence_or
                : NodeKind::disjunction;
            node.operands = { *left, *right };
            left = add(std::move(node));
        }
        return left;
    }

    std::optional<std::size_t> conjunction()
    {
        auto left = unary();
        while (left && at_keyword("and")) {
            ++position_;
            auto right = unary();
            if (!right) {
                return right;
            }
            Node node;
            node.kind = is_sequence(*left) && is_sequence(*right)
                ? NodeKind::sequence_and
                : NodeKind::conjunction;
            node.operands = { *left, *right };
            left = add(std::move(node));
        }
        return left;
    }

    // A bracketed constant range `[lo:hi]` or `[n]` at the current `[`.
    std::optional<std::pair<BoundSpec, BoundSpec>> range()
    {
        const auto close = matching(position_);
        if (!close) {
            return std::nullopt;
        }
        const auto inside = slice(position_ + 1U, *close);
        position_ = *close + 1U;
        const auto colon = find_top_level(inside, TokenKind::Colon);
        if (!colon) {
            auto value = bound(inside);
            if (!value) {
                return std::nullopt;
            }
            return std::pair { *value, *value };
        }
        auto lo = bound(std::span<const Token> { inside }.first(*colon));
        auto hi = bound(std::span<const Token> { inside }.subspan(*colon + 1U));
        if (!lo || !hi || lo->kind == BoundSpec::Kind::unbounded) {
            return std::nullopt;
        }
        return std::pair { std::move(*lo), std::move(*hi) };
    }

    [[nodiscard]] static std::optional<BoundSpec> bound(
        const std::span<const Token> tokens)
    {
        if (tokens.empty()) {
            return std::nullopt;
        }
        BoundSpec result;
        if (tokens.size() == 1U && tokens.front().text == "$") {
            result.kind = BoundSpec::Kind::unbounded;
            return result;
        }
        if (tokens.size() == 1U && tokens.front().kind == TokenKind::Number) {
            auto text = std::string_view { tokens.front().text };
            if (const auto quote = text.find('\'');
                quote != std::string_view::npos) {
                text.remove_prefix(quote + 1U);
                if (!text.empty() && (text.front() == 's' || text.front() == 'S')) {
                    text.remove_prefix(1U);
                }
                if (text.empty() || (text.front() != 'd' && text.front() != 'D')) {
                    text = { };
                } else {
                    text.remove_prefix(1U);
                }
            }
            std::string digits;
            for (const auto character : text) {
                if (character != '_') {
                    digits.push_back(character);
                }
            }
            if (!digits.empty() && digits.size() <= 15U
                && std::ranges::all_of(digits, [](const char character) {
                       return character >= '0' && character <= '9';
                   })) {
                result.value = std::stoll(digits);
                return result;
            }
        }
        result.kind = BoundSpec::Kind::expression;
        result.tokens.assign(tokens.begin(), tokens.end());
        return result;
    }

    // `not`, `nexttime` and the prefix operators that take the rest of the
    // property (always, eventually, if, case, the abort operators).
    std::optional<std::size_t> unary()
    {
        if (position_ >= tokens_.size()) {
            return fail("a property operand is missing");
        }
        if (at_keyword("not")) {
            ++position_;
            auto operand = unary();
            if (!operand) {
                return operand;
            }
            Node node;
            node.kind = NodeKind::negation;
            node.operands = { *operand };
            return add(std::move(node));
        }
        if (at_keyword("nexttime") || at_keyword("s_nexttime")) {
            Node node;
            node.kind = NodeKind::nexttime;
            node.strong = at_keyword("s_nexttime");
            ++position_;
            node.lo.value = 1;
            if (at(TokenKind::LeftBracket)) {
                auto bounds = range();
                if (!bounds || bounds->first.kind == BoundSpec::Kind::unbounded) {
                    return fail("nexttime requires a constant tick count");
                }
                node.lo = bounds->first;
            }
            auto operand = unary();
            if (!operand) {
                return operand;
            }
            node.operands = { *operand };
            return add(std::move(node));
        }
        const bool always = at_keyword("always") || at_keyword("s_always");
        const bool eventually
            = at_keyword("eventually") || at_keyword("s_eventually");
        if (always || eventually) {
            Node node;
            node.kind = always ? NodeKind::always : NodeKind::eventually;
            node.strong = tokens_[position_].text.starts_with("s_");
            ++position_;
            node.hi.kind = BoundSpec::Kind::unbounded;
            if (at(TokenKind::LeftBracket)) {
                auto bounds = range();
                if (!bounds) {
                    return fail("the range of always or eventually is not "
                                "a constant range");
                }
                node.lo = std::move(bounds->first);
                node.hi = std::move(bounds->second);
            } else if (!(always ? !node.strong : node.strong)) {
                return fail("s_always and eventually require a range");
            }
            auto operand = implication();
            if (!operand) {
                return operand;
            }
            node.operands = { *operand };
            return add(std::move(node));
        }
        static constexpr std::array<std::string_view, 4> aborts {
            "accept_on", "reject_on", "sync_accept_on", "sync_reject_on"
        };
        if (const auto abort = std::ranges::find_if(aborts,
                [&](const std::string_view text) { return at_keyword(text); });
            abort != aborts.end() && at(TokenKind::LeftParen, 1)) {
            Node node;
            node.kind = abort->find("accept") != std::string_view::npos
                ? NodeKind::accept
                : NodeKind::reject;
            node.synchronous = abort->starts_with("sync_");
            ++position_;
            const auto close = matching(position_);
            if (!close) {
                return fail("an abort condition is unbalanced");
            }
            node.expression = slice(position_ + 1U, *close);
            if (!context_.boolean(node.expression)) {
                return fail("an abort condition is not a boolean expression");
            }
            position_ = *close + 1U;
            auto operand = implication();
            if (!operand) {
                return operand;
            }
            node.operands = { *operand };
            return add(std::move(node));
        }
        if (at_keyword("if") && at(TokenKind::LeftParen, 1)) {
            ++position_;
            const auto close = matching(position_);
            if (!close) {
                return fail("a property if condition is unbalanced");
            }
            Node node;
            node.kind = NodeKind::conditional;
            node.expression = slice(position_ + 1U, *close);
            if (!context_.boolean(node.expression)) {
                return fail("a property if condition is not a boolean expression");
            }
            position_ = *close + 1U;
            auto then = implication();
            if (!then) {
                return then;
            }
            node.operands = { *then };
            if (at_keyword("else")) {
                ++position_;
                auto otherwise = implication();
                if (!otherwise) {
                    return otherwise;
                }
                node.operands.push_back(*otherwise);
            }
            return add(std::move(node));
        }
        if (at_keyword("case") && at(TokenKind::LeftParen, 1)) {
            return property_case();
        }
        return intersect();
    }

    // `case (e) v1, v2: p1; default: p2; endcase` (16.12.16) is the chain
    // of conditionals on `(e) === (v)`.
    std::optional<std::size_t> property_case()
    {
        ++position_;
        const auto close = matching(position_);
        if (!close) {
            return fail("a property case expression is unbalanced");
        }
        const auto selector = slice(position_ + 1U, *close);
        position_ = *close + 1U;
        struct Item {
            std::vector<std::vector<Token>> values;
            std::size_t property { };
        };
        std::vector<Item> items;
        std::optional<std::size_t> otherwise;
        while (position_ < tokens_.size() && !at_keyword("endcase")) {
            const auto statement_end = [&](const std::size_t from) {
                int depth { };
                for (auto index = from; index < tokens_.size(); ++index) {
                    const auto kind = tokens_[index].kind;
                    if (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket
                        || kind == TokenKind::LeftBrace) {
                        ++depth;
                    } else if (kind == TokenKind::RightParen
                        || kind == TokenKind::RightBracket
                        || kind == TokenKind::RightBrace) {
                        --depth;
                    } else if (depth == 0 && kind == TokenKind::Semicolon) {
                        return index;
                    }
                }
                return tokens_.size();
            };
            Item item;
            const bool is_default = at_keyword("default");
            if (is_default) {
                ++position_;
                if (at(TokenKind::Colon)) {
                    ++position_;
                }
            } else {
                const auto item_end = statement_end(position_);
                const auto label = find_top_level(
                    std::span<const Token> { tokens_ }.subspan(
                        position_, item_end - position_),
                    TokenKind::Colon);
                if (!label) {
                    return fail("a property case item has no ':'");
                }
                for (auto& value : split_top_level(
                         std::span<const Token> { tokens_ }.subspan(
                             position_, *label),
                         TokenKind::Comma)) {
                    item.values.push_back(std::move(value));
                }
                position_ += *label + 1U;
            }
            const auto end = statement_end(position_);
            if (end == tokens_.size()) {
                return fail("a property case item has no ';'");
            }
            TreeParser inner { nodes_, slice(position_, end), context_, depth_ };
            auto property = inner.parse();
            if (!property) {
                return property;
            }
            position_ = end + 1U;
            if (is_default) {
                otherwise = *property;
            } else {
                item.property = *property;
                items.push_back(std::move(item));
            }
        }
        if (!at_keyword("endcase")) {
            return fail("a property case has no endcase");
        }
        ++position_;
        auto result = otherwise;
        for (auto item = items.rbegin(); item != items.rend(); ++item) {
            Node node;
            node.kind = NodeKind::conditional;
            const auto& model = selector.empty() ? tokens_.front() : selector.front();
            for (const auto& value : item->values) {
                if (!node.expression.empty()) {
                    node.expression.push_back(
                        punctuation(model, TokenKind::OrOr, "||"));
                }
                node.expression.push_back(
                    punctuation(model, TokenKind::LeftParen, "("));
                node.expression.push_back(
                    punctuation(model, TokenKind::LeftParen, "("));
                node.expression.insert(
                    node.expression.end(), selector.begin(), selector.end());
                node.expression.push_back(
                    punctuation(model, TokenKind::RightParen, ")"));
                node.expression.push_back(
                    punctuation(model, TokenKind::CaseEqual, "==="));
                node.expression.push_back(
                    punctuation(model, TokenKind::LeftParen, "("));
                node.expression.insert(
                    node.expression.end(), value.begin(), value.end());
                node.expression.push_back(
                    punctuation(model, TokenKind::RightParen, ")"));
                node.expression.push_back(
                    punctuation(model, TokenKind::RightParen, ")"));
            }
            node.operands = { item->property };
            if (result) {
                node.operands.push_back(*result);
            }
            result = add(std::move(node));
        }
        if (!result) {
            return fail("a property case has no items");
        }
        return result;
    }

    std::optional<std::size_t> intersect()
    {
        auto left = within();
        while (left && at_keyword("intersect")) {
            ++position_;
            auto right = within();
            if (!right) {
                return right;
            }
            if (!is_sequence(*left) || !is_sequence(*right)) {
                return fail("intersect requires sequence operands");
            }
            Node node;
            node.kind = NodeKind::intersect;
            node.operands = { *left, *right };
            left = add(std::move(node));
        }
        return left;
    }

    std::optional<std::size_t> within()
    {
        auto left = throughout();
        while (left && at_keyword("within")) {
            ++position_;
            auto right = throughout();
            if (!right) {
                return right;
            }
            if (!is_sequence(*left) || !is_sequence(*right)) {
                return fail("within requires sequence operands");
            }
            Node node;
            node.kind = NodeKind::within;
            node.operands = { *left, *right };
            left = add(std::move(node));
        }
        return left;
    }

    std::optional<std::size_t> throughout()
    {
        const auto end = boolean_end(position_);
        if (end > position_ && end < tokens_.size()
            && token_is_keyword(tokens_[end], "throughout")) {
            Node node;
            node.kind = NodeKind::throughout;
            node.expression = slice(position_, end);
            if (!context_.boolean(node.expression)) {
                return fail("the left operand of throughout is not a boolean");
            }
            position_ = end + 1U;
            auto operand = throughout();
            if (!operand) {
                return operand;
            }
            if (!is_sequence(*operand)) {
                return fail("throughout requires a sequence operand");
            }
            node.operands = { *operand };
            return add(std::move(node));
        }
        return concatenation();
    }

    [[nodiscard]] bool at_delay() const
    {
        return at(TokenKind::Hash) && at(TokenKind::Hash, 1);
    }

    // `##n`, `##(e)`, `##[m:n]`, `##[*]`, `##[+]`.
    std::optional<std::pair<BoundSpec, BoundSpec>> delay()
    {
        position_ += 2U;
        if (at(TokenKind::LeftBracket)) {
            if (at(TokenKind::Star, 1) && at(TokenKind::RightBracket, 2)) {
                position_ += 3U;
                BoundSpec hi;
                hi.kind = BoundSpec::Kind::unbounded;
                return std::pair { BoundSpec { }, hi };
            }
            if (at(TokenKind::Plus, 1) && at(TokenKind::RightBracket, 2)) {
                position_ += 3U;
                BoundSpec lo;
                lo.value = 1;
                BoundSpec hi;
                hi.kind = BoundSpec::Kind::unbounded;
                return std::pair { lo, hi };
            }
            return range();
        }
        if (at(TokenKind::LeftParen)) {
            const auto close = matching(position_);
            if (!close) {
                return std::nullopt;
            }
            auto value = bound(std::span<const Token> { slice(position_, *close + 1U) });
            position_ = *close + 1U;
            if (!value || value->kind == BoundSpec::Kind::unbounded) {
                return std::nullopt;
            }
            return std::pair { *value, *value };
        }
        if (at(TokenKind::Number) || at(TokenKind::Identifier)) {
            auto value = bound(std::span<const Token> { &tokens_[position_], 1U });
            ++position_;
            if (!value || value->kind == BoundSpec::Kind::unbounded) {
                return std::nullopt;
            }
            return std::pair { *value, *value };
        }
        return std::nullopt;
    }

    std::optional<std::size_t> concatenation()
    {
        std::optional<std::size_t> left;
        if (at_delay()) {
            auto bounds = delay();
            if (!bounds) {
                return fail("a cycle delay is not a constant or range");
            }
            auto right = repetition();
            if (!right) {
                return right;
            }
            if (!is_sequence(*right)) {
                return fail("a cycle delay requires a sequence operand");
            }
            Node node;
            node.kind = NodeKind::delay;
            node.leading = true;
            node.lo = std::move(bounds->first);
            node.hi = std::move(bounds->second);
            node.operands = { *right };
            left = add(std::move(node));
        } else {
            left = repetition();
        }
        while (left && at_delay()) {
            if (!is_sequence(*left)) {
                return fail("a cycle delay requires a sequence operand");
            }
            auto bounds = delay();
            if (!bounds) {
                return fail("a cycle delay is not a constant or range");
            }
            auto right = repetition();
            if (!right) {
                return right;
            }
            if (!is_sequence(*right)) {
                return fail("a cycle delay requires a sequence operand");
            }
            Node node;
            node.kind = NodeKind::delay;
            node.lo = std::move(bounds->first);
            node.hi = std::move(bounds->second);
            node.operands = { *left, *right };
            left = add(std::move(node));
        }
        return left;
    }

    [[nodiscard]] bool at_repetition() const
    {
        return at(TokenKind::LeftBracket)
            && (at(TokenKind::Star, 1) || at(TokenKind::Assign, 1)
                || at(TokenKind::ThinArrow, 1)
                || (at(TokenKind::Plus, 1) && at(TokenKind::RightBracket, 2)));
    }

    std::optional<std::size_t> repetition()
    {
        auto operand = primary();
        while (operand && at_repetition()) {
            Node node;
            node.kind = at(TokenKind::Assign, 1) ? NodeKind::nonconsecutive_repeat
                : at(TokenKind::ThinArrow, 1)    ? NodeKind::goto_repeat
                                                 : NodeKind::repeat;
            if (!is_sequence(*operand)) {
                return fail("a repetition requires a sequence operand");
            }
            if (node.kind != NodeKind::repeat
                && nodes_[*operand].kind != NodeKind::boolean) {
                return fail("goto and nonconsecutive repetition require a "
                            "boolean operand");
            }
            if (at(TokenKind::Plus, 1)) {
                position_ += 3U;
                node.lo.value = 1;
                node.hi.kind = BoundSpec::Kind::unbounded;
            } else if (at(TokenKind::Star, 1) && at(TokenKind::RightBracket, 2)) {
                position_ += 3U;
                node.hi.kind = BoundSpec::Kind::unbounded;
            } else {
                // Drop the operator token so the range reads `[lo:hi]`.
                const auto open = position_;
                tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(open + 1U));
                auto bounds = range();
                if (!bounds) {
                    return fail("a repetition range is not constant");
                }
                node.lo = std::move(bounds->first);
                node.hi = std::move(bounds->second);
            }
            node.operands = { *operand };
            operand = add(std::move(node));
        }
        return operand;
    }

    // The end of a boolean operand that starts at `start`: the first token
    // at depth zero that begins a sequence or property operator.
    [[nodiscard]] std::size_t boolean_end(const std::size_t start) const
    {
        static const std::array<std::string_view, 13> stops {
            "and", "or", "intersect", "within", "throughout", "iff",
            "until", "s_until", "until_with", "s_until_with", "implies",
            "else", "endcase",
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
                        || (next == TokenKind::Plus && index + 2U < tokens_.size()
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
            if (token.kind == TokenKind::Hash || token.kind == TokenKind::Comma
                || token.kind == TokenKind::Semicolon
                || (token.kind == TokenKind::Pipe && next == TokenKind::ThinArrow)
                || (token.kind == TokenKind::PipeAssign
                    && next == TokenKind::Greater)
                || (token.kind == TokenKind::Identifier
                    && std::ranges::find(stops, token.text) != stops.end())) {
                return index;
            }
        }
        return tokens_.size();
    }

    [[nodiscard]] const SystemVerilogAssertionDeclaration* declaration(
        const std::size_t at_position) const
    {
        if (at_position >= tokens_.size()
            || tokens_[at_position].kind != TokenKind::Identifier
            || (at_position + 1U < tokens_.size()
                && tokens_[at_position + 1U].kind == TokenKind::Dot)) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            context_.unit.systemverilog_assertion_declarations,
            [&](const SystemVerilogAssertionDeclaration& entry) {
                return entry.kind != SystemVerilogAssertionDeclarationKind::Checker
                    && entry.name == tokens_[at_position].text;
            });
        return found == context_.unit.systemverilog_assertion_declarations.end()
            ? nullptr
            : &*found;
    }

    // Whether a token range names a sequence or property declaration, so it
    // is not an ordinary boolean.
    [[nodiscard]] bool names_declaration(const std::size_t begin,
        const std::size_t end) const
    {
        for (auto index = begin; index < end; ++index) {
            if (declaration(index) != nullptr
                && (index == 0U || tokens_[index - 1U].kind != TokenKind::Dot)) {
                return true;
            }
        }
        return false;
    }

    std::optional<std::size_t> leaf(const std::size_t end)
    {
        Node node;
        node.expression = slice(position_, end);
        if (!context_.boolean(node.expression)) {
            return fail("'" + spelling(node.expression)
                + "' is not a boolean expression");
        }
        position_ = end;
        return add(std::move(node));
    }

    std::optional<std::size_t> nested(const std::size_t open,
        const std::size_t close)
    {
        const auto inside = slice(open + 1U, close);
        if (split_top_level(inside, TokenKind::Comma).size() > 1U) {
            return fail("sequence match items are not yet evaluated by the "
                        "concurrent-property monitor");
        }
        TreeParser inner { nodes_, inside, context_, depth_ };
        auto result = inner.parse();
        position_ = close + 1U;
        return result;
    }

    std::optional<std::size_t> primary()
    {
        if (position_ >= tokens_.size()) {
            return fail("a sequence operand is missing");
        }
        if (at(TokenKind::LeftParen)) {
            const auto close = matching(position_);
            if (!close) {
                return fail("a parenthesized operand is unbalanced");
            }
            const auto end = boolean_end(position_);
            if (end > *close && !names_declaration(position_, end)
                && context_.boolean(slice(position_, end))) {
                return leaf(end);
            }
            return nested(position_, *close);
        }
        if ((at_keyword("first_match") || at_keyword("strong")
                || at_keyword("weak"))
            && at(TokenKind::LeftParen, 1)) {
            Node node;
            node.kind = at_keyword("first_match") ? NodeKind::first_match
                : at_keyword("strong")             ? NodeKind::strong
                                                   : NodeKind::weak;
            ++position_;
            const auto close = matching(position_);
            if (!close) {
                return fail("an operand list is unbalanced");
            }
            auto operand = nested(position_, *close);
            if (!operand) {
                return operand;
            }
            if (!is_sequence(*operand)) {
                return fail("first_match, strong and weak require a sequence");
            }
            node.operands = { *operand };
            return add(std::move(node));
        }
        if (at(TokenKind::At)) {
            // A clocking event inside the property must be the directive's
            // clock; multiple clocks are not evaluated.
            ++position_;
            std::vector<Token> event;
            if (at(TokenKind::LeftParen)) {
                const auto close = matching(position_);
                if (!close) {
                    return fail("a clocking event is unbalanced");
                }
                event = slice(position_ + 1U, *close);
                position_ = *close + 1U;
            } else if (at(TokenKind::Identifier)) {
                event = slice(position_, position_ + 1U);
                ++position_;
            }
            if (spelling(event) != spelling(context_.clock)) {
                return fail("a property with more than one clocking event");
            }
            return implication();
        }
        if (const auto* found = declaration(position_)) {
            return instance(*found);
        }
        const auto end = boolean_end(position_);
        if (end == position_) {
            return fail("a sequence operand is missing before '"
                + tokens_[position_].text + "'");
        }
        return leaf(end);
    }

    std::optional<std::size_t> instance(
        const SystemVerilogAssertionDeclaration& declaration)
    {
        if (depth_ >= 16U) {
            return fail("sequence and property instances nest too deeply");
        }
        if (!declaration.local_variables.empty()) {
            return fail("assertion local variables are not yet evaluated by "
                        "the concurrent-property monitor");
        }
        if (declaration.disable) {
            return fail("disable iff of a nested property instance");
        }
        const auto name = tokens_[position_];
        ++position_;
        std::vector<std::vector<Token>> actuals;
        if (at(TokenKind::LeftParen)) {
            const auto close = matching(position_);
            if (!close) {
                return fail("an instance argument list is unbalanced");
            }
            const auto inside = slice(position_ + 1U, *close);
            if (!inside.empty()) {
                actuals = split_top_level(inside, TokenKind::Comma);
            }
            position_ = *close + 1U;
        }
        auto body = substituted_body(declaration, actuals, name);
        if (!body) {
            return fail("the actual arguments of '" + declaration.name
                + "' do not match its formals");
        }
        if (declaration.clock) {
            auto clock = substituted(declaration, actuals,
                declaration.clock->event_tokens);
            if (!clock || spelling(*clock) != spelling(context_.clock)) {
                return fail("a property with more than one clocking event");
            }
        }
        TreeParser inner { nodes_,
            inferred(std::move(*body), context_.clock, context_.inferred_disable),
            context_, depth_ + 1U };
        return inner.parse();
    }

public:
    // The tokens with each formal replaced by its parenthesized actual.
    static std::optional<std::vector<Token>> substituted(
        const SystemVerilogAssertionDeclaration& declaration,
        const std::vector<std::vector<Token>>& actuals,
        const std::span<const Token> tokens)
    {
        std::map<std::string, std::vector<Token>, std::less<>> bindings;
        std::size_t positional { };
        for (const auto& actual : actuals) {
            if (actual.size() >= 4U && actual[0].kind == TokenKind::Dot
                && actual[1].kind == TokenKind::Identifier
                && actual[2].kind == TokenKind::LeftParen
                && actual.back().kind == TokenKind::RightParen) {
                bindings[actual[1].text].assign(actual.begin() + 3, actual.end() - 1);
            } else if (positional < declaration.formals.size()) {
                bindings[declaration.formals[positional++].name] = actual;
            } else {
                return std::nullopt;
            }
        }
        for (const auto& formal : declaration.formals) {
            auto& value = bindings[formal.name];
            if (value.empty()) {
                if (formal.default_tokens.empty()) {
                    return std::nullopt;
                }
                value = formal.default_tokens;
            }
        }
        std::vector<Token> result;
        auto end = tokens.size();
        while (end > 0U && tokens[end - 1U].kind == TokenKind::Semicolon) {
            --end;
        }
        for (const auto& token : tokens.first(end)) {
            const auto binding = token.kind == TokenKind::Identifier
                ? bindings.find(token.text)
                : bindings.end();
            if (binding == bindings.end()) {
                result.push_back(token);
                continue;
            }
            result.push_back(punctuation(token, TokenKind::LeftParen, "("));
            result.insert(result.end(), binding->second.begin(),
                binding->second.end());
            result.push_back(punctuation(token, TokenKind::RightParen, ")"));
        }
        return result;
    }

    // A formal's default `$inferred_clock` or `$inferred_disable` is the
    // clocking event or default disable condition of the instance's context
    // (16.14.7).
    static std::vector<Token> inferred(std::vector<Token> tokens,
        const std::span<const Token> clock,
        const std::span<const Token> disable)
    {
        std::vector<Token> result;
        for (auto& token : tokens) {
            const bool is_clock = token.text == "$inferred_clock";
            if (!is_clock && token.text != "$inferred_disable") {
                result.push_back(std::move(token));
                continue;
            }
            const auto replacement = is_clock ? clock : disable;
            result.push_back(punctuation(token, TokenKind::LeftParen, "("));
            result.insert(result.end(), replacement.begin(), replacement.end());
            result.push_back(punctuation(token, TokenKind::RightParen, ")"));
        }
        return result;
    }

    static std::optional<std::vector<Token>> substituted_body(
        const SystemVerilogAssertionDeclaration& declaration,
        const std::vector<std::vector<Token>>& actuals, const Token&)
    {
        return substituted(declaration, actuals, declaration.expression_tokens);
    }

private:
    std::vector<Node>& nodes_;
    std::vector<Token> tokens_;
    ParseContext& context_;
    std::size_t depth_ { };
    std::size_t position_ { };
};

// Compiles a property tree into a plan whose guards and bounds are the
// expressions the directive's process evaluates.
class PlanCompiler {
public:
    PlanCompiler(const std::vector<Node>& nodes, std::vector<Token> truth)
        : nodes_(nodes)
        , truth_(std::move(truth))
    {
    }

    plan::Plan plan;
    std::vector<std::vector<Token>> guards;
    std::vector<std::vector<Token>> bounds;

    void compile(const std::size_t root, const bool every_match)
    {
        plan.root = property(root);
        plan.every_match = every_match;
        plan.guards = static_cast<std::uint32_t>(guards.size());
        plan.bounds = static_cast<std::uint32_t>(bounds.size());
    }

private:
    std::uint32_t guard(const std::vector<Token>& tokens)
    {
        const auto text = spelling(tokens);
        if (const auto found = guard_index_.find(text);
            found != guard_index_.end()) {
            return found->second;
        }
        guards.push_back(tokens);
        const auto index = static_cast<std::uint32_t>(guards.size() - 1U);
        guard_index_.emplace(text, index);
        return index;
    }

    std::uint32_t negated_guard(const std::vector<Token>& tokens)
    {
        std::vector<Token> negated;
        const auto& model = tokens.front();
        negated.push_back(punctuation(model, TokenKind::Bang, "!"));
        negated.push_back(punctuation(model, TokenKind::LeftParen, "("));
        negated.insert(negated.end(), tokens.begin(), tokens.end());
        negated.push_back(punctuation(model, TokenKind::RightParen, ")"));
        return guard(negated);
    }

    plan::Bound bound(const BoundSpec& spec, const std::int64_t offset = 0)
    {
        plan::Bound result;
        result.offset = offset;
        switch (spec.kind) {
        case BoundSpec::Kind::literal:
            result.value = spec.value;
            break;
        case BoundSpec::Kind::unbounded:
            result.mode = plan::Bound::Mode::unbounded;
            break;
        case BoundSpec::Kind::expression: {
            const auto text = spelling(spec.tokens);
            auto found = bound_index_.find(text);
            if (found == bound_index_.end()) {
                bounds.push_back(spec.tokens);
                found = bound_index_.emplace(text, bounds.size() - 1U).first;
            }
            result.mode = plan::Bound::Mode::slot;
            result.value = static_cast<std::int64_t>(found->second);
            break;
        }
        }
        return result;
    }

    static plan::Instruction instruction(const plan::Op op,
        const std::uint32_t a = 0, const std::uint32_t b = 0,
        const std::uint32_t c = 0)
    {
        plan::Instruction result;
        result.op = op;
        result.a = a;
        result.b = b;
        result.c = c;
        return result;
    }

    // Repeat the code emitted by `body` between lo and hi times.
    void repeat(plan::Sequence& sequence, const plan::Bound& lo,
        const plan::Bound& hi, const std::function<void()>& body)
    {
        const auto counter = sequence.counters++;
        sequence.code.push_back(instruction(plan::Op::counter_set, counter));
        const auto head = static_cast<std::uint32_t>(sequence.code.size());
        auto branch = instruction(plan::Op::counter_branch, counter);
        branch.lo = lo;
        branch.hi = hi;
        sequence.code.push_back(branch);
        body();
        auto increment = instruction(plan::Op::counter_increment, counter);
        increment.lo = lo;
        increment.hi = hi;
        sequence.code.push_back(increment);
        sequence.code.push_back(instruction(plan::Op::jump, head));
        sequence.code[head].b = static_cast<std::uint32_t>(sequence.code.size());
    }

    void emit(const std::size_t index, plan::Sequence& sequence)
    {
        const auto& node = nodes_[index];
        auto& code = sequence.code;
        const auto here = [&] { return static_cast<std::uint32_t>(code.size()); };
        switch (node.kind) {
        case NodeKind::boolean:
            code.push_back(instruction(plan::Op::check, guard(node.expression)));
            break;
        case NodeKind::delay: {
            if (!node.leading) {
                emit(node.operands[0], sequence);
            }
            auto step = instruction(plan::Op::delay);
            // A leading delay counts from the attempt's first tick.
            const std::int64_t offset = node.leading ? 1 : 0;
            step.lo = bound(node.lo, offset);
            step.hi = bound(node.hi, offset);
            code.push_back(step);
            emit(node.operands.back(), sequence);
            break;
        }
        case NodeKind::repeat:
            repeat(sequence, bound(node.lo), bound(node.hi),
                [&] { emit(node.operands[0], sequence); });
            break;
        case NodeKind::goto_repeat:
        case NodeKind::nonconsecutive_repeat: {
            // b[->n] is (!b[*0:$] ##1 b)[*n]; b[=n] is b[->n] ##1 !b[*0:$].
            const auto& condition = nodes_[node.operands[0]].expression;
            const auto hit = guard(condition);
            const auto miss = negated_guard(condition);
            plan::Bound zero;
            plan::Bound forever;
            forever.mode = plan::Bound::Mode::unbounded;
            repeat(sequence, bound(node.lo), bound(node.hi), [&] {
                repeat(sequence, zero, forever, [&] {
                    code.push_back(instruction(plan::Op::check, miss));
                });
                code.push_back(instruction(plan::Op::check, hit));
            });
            if (node.kind == NodeKind::nonconsecutive_repeat) {
                repeat(sequence, zero, forever, [&] {
                    code.push_back(instruction(plan::Op::check, miss));
                });
            }
            break;
        }
        case NodeKind::sequence_or: {
            const auto fork = here();
            code.push_back(instruction(plan::Op::fork));
            emit(node.operands[0], sequence);
            const auto jump = here();
            code.push_back(instruction(plan::Op::jump));
            code[fork].a = here();
            emit(node.operands[1], sequence);
            code[jump].a = here();
            break;
        }
        case NodeKind::sequence_and:
        case NodeKind::intersect:
        case NodeKind::within: {
            const auto begin = here();
            code.push_back(instruction(plan::Op::join_begin,
                node.kind == NodeKind::sequence_and ? 0U : 1U));
            if (node.kind == NodeKind::within) {
                // a within b: (1[*0:$] ##1 a ##1 1[*0:$]) intersect b.
                plan::Bound zero;
                plan::Bound forever;
                forever.mode = plan::Bound::Mode::unbounded;
                const auto truth = guard(truth_);
                repeat(sequence, zero, forever, [&] {
                    code.push_back(instruction(plan::Op::check, truth));
                });
                emit(node.operands[0], sequence);
                repeat(sequence, zero, forever, [&] {
                    code.push_back(instruction(plan::Op::check, truth));
                });
            } else {
                emit(node.operands[0], sequence);
            }
            code.push_back(instruction(plan::Op::join_end, 0U));
            code[begin].b = here();
            emit(node.operands[1], sequence);
            code.push_back(instruction(plan::Op::join_end, 1U));
            code[begin].c = here();
            break;
        }
        case NodeKind::throughout:
            code.push_back(instruction(
                plan::Op::throughout_begin, guard(node.expression)));
            emit(node.operands[0], sequence);
            code.push_back(instruction(plan::Op::throughout_end));
            break;
        case NodeKind::first_match:
            code.push_back(instruction(plan::Op::first_match_begin));
            emit(node.operands[0], sequence);
            code.push_back(instruction(plan::Op::first_match_end));
            break;
        default:
            break;
        }
    }

    std::uint32_t sequence(const std::size_t index)
    {
        plan::Sequence compiled;
        emit(index, compiled);
        compiled.code.push_back(instruction(plan::Op::match));
        plan.sequences.push_back(std::move(compiled));
        return static_cast<std::uint32_t>(plan.sequences.size() - 1U);
    }

    std::uint32_t add(plan::Property property)
    {
        plan.properties.push_back(std::move(property));
        return static_cast<std::uint32_t>(plan.properties.size() - 1U);
    }

    std::uint32_t property(const std::size_t index)
    {
        const auto& node = nodes_[index];
        plan::Property result;
        if (sequence_kind(node.kind)) {
            result.kind = plan::PropertyKind::sequence;
            result.sequence = sequence(index);
            return add(std::move(result));
        }
        const auto operand = [&](const std::size_t position) {
            return property(node.operands[position]);
        };
        switch (node.kind) {
        case NodeKind::strong:
        case NodeKind::weak:
            result.kind = plan::PropertyKind::sequence;
            result.strong = node.kind == NodeKind::strong;
            result.sequence = sequence(node.operands[0]);
            break;
        case NodeKind::negation:
            result.kind = plan::PropertyKind::negation;
            result.children = { operand(0) };
            break;
        case NodeKind::conjunction:
        case NodeKind::disjunction:
        case NodeKind::equivalence:
        case NodeKind::implies:
            result.kind = node.kind == NodeKind::conjunction
                ? plan::PropertyKind::conjunction
                : node.kind == NodeKind::disjunction
                ? plan::PropertyKind::disjunction
                : node.kind == NodeKind::equivalence
                ? plan::PropertyKind::equivalence
                : plan::PropertyKind::implies;
            result.children = { operand(0), operand(1) };
            break;
        case NodeKind::implication:
        case NodeKind::followed_by:
            result.kind = node.kind == NodeKind::implication
                ? plan::PropertyKind::implication
                : plan::PropertyKind::followed_by;
            result.overlapped = node.overlapped;
            result.sequence = sequence(node.operands[0]);
            result.children = { operand(1) };
            break;
        case NodeKind::conditional:
            result.kind = plan::PropertyKind::conditional;
            result.guard = guard(node.expression);
            result.children = { operand(0) };
            if (node.operands.size() > 1U) {
                result.children.push_back(operand(1));
            }
            break;
        case NodeKind::nexttime:
            result.kind = plan::PropertyKind::nexttime;
            result.strong = node.strong;
            result.lo = bound(node.lo);
            result.children = { operand(0) };
            break;
        case NodeKind::always:
        case NodeKind::eventually:
            result.kind = node.kind == NodeKind::always
                ? plan::PropertyKind::always
                : plan::PropertyKind::eventually;
            result.strong = node.strong;
            result.lo = bound(node.lo);
            result.hi = bound(node.hi);
            result.children = { operand(0) };
            break;
        case NodeKind::until: {
            result.kind = plan::PropertyKind::until;
            result.strong = node.strong;
            const auto left = operand(0);
            auto right = operand(1);
            if (node.with) {
                // p until_with q is p until (p and q).
                plan::Property both;
                both.kind = plan::PropertyKind::conjunction;
                both.children = { operand(0), right };
                right = add(std::move(both));
            }
            result.children = { left, right };
            break;
        }
        case NodeKind::accept:
        case NodeKind::reject:
            result.kind = node.kind == NodeKind::accept
                ? plan::PropertyKind::accept
                : plan::PropertyKind::reject;
            result.synchronous = node.synchronous;
            result.guard = guard(node.expression);
            result.children = { operand(0) };
            break;
        default:
            break;
        }
        return add(std::move(result));
    }

    const std::vector<Node>& nodes_;
    std::vector<Token> truth_;
    std::map<std::string, std::uint32_t, std::less<>> guard_index_;
    std::map<std::string, std::size_t, std::less<>> bound_index_;
};

[[nodiscard]] bool trace_enabled()
{
    static const bool enabled
        = support::environment_variable("FSIM_SVA_TRACE").has_value();
    return enabled;
}

void strip_semicolons(std::vector<Token>& tokens)
{
    while (!tokens.empty() && tokens.back().kind == TokenKind::Semicolon) {
        tokens.pop_back();
    }
}

} // namespace

void VerilogParser::extract_procedural_assertions(
    std::vector<Process>& processes,
    std::vector<VariableDeclaration>& variables,
    std::vector<SystemVerilogConcurrentAssertion>& directives,
    std::vector<SystemVerilogAssertionDeclaration>& declarations)
{
    for (auto& process : processes) {
        // The clock of an assertion in an always procedure whose event
        // control is one edge expression (16.14.5).
        std::vector<Token> clock;
        if (process.kind == ProcessKind::VerilogAlways
            && process.sensitivities.size() == 1U
            && process.sensitivities.front().edge != EdgeKind::Any
            && !process.sensitivities.front().signal.empty()) {
            const auto& event = process.sensitivities.front();
            clock = synthetic_tokens(
                std::string { event.edge == EdgeKind::Positive ? "posedge "
                                                               : "negedge " }
                    + event.signal,
                event.span);
        }
        const auto walk = [&](const auto& self, auto& statements) -> void {
            for (auto& statement : statements) {
                if (statement.procedural_concurrent_assertion
                    && !statement.procedural_concurrent_assertion
                            ->expect_statement) {
                    auto directive
                        = std::move(*statement.procedural_concurrent_assertion);
                    if (directive.label.empty()) {
                        directive.label = statement.label;
                    }
                    const auto name
                        = "__fsim_pa" + std::to_string(directives.size());
                    directive.procedural_enable = name;
                    directive.procedural_clock = clock;
                    declare_inline_property(directive, declarations);
                    variables.push_back(VariableDeclaration {
                        name,
                        Type { ValueDomain::Bit2, "bit",
                            PackedRange { 0, 0, true }, false },
                        std::nullopt, statement.span });
                    // Reaching the statement enables an attempt at the next
                    // clock tick of the assertion (16.14.6.2).
                    auto enable = parse_generated_statement(
                        synthetic_tokens(name + " = 1'b1;", statement.span),
                        statement.span);
                    statement = enable ? std::move(*enable) : Statement { };
                    directives.push_back(std::move(directive));
                    continue;
                }
                self(self, statement.statements);
                self(self, statement.else_statements);
                for (auto& alternative : statement.case_alternatives) {
                    self(self, alternative.statements);
                }
            }
        };
        walk(walk, process.statements);
    }
}

std::optional<VerilogParser::SvaPrepared> VerilogParser::prepare_sva_property(
    const DesignUnit& unit, const SystemVerilogConcurrentAssertion& directive,
    std::string& reason)
{
    const auto fail = [&](std::string why) -> std::optional<SvaPrepared> {
        reason = std::move(why);
        return std::nullopt;
    };
    std::vector<Token> tokens;
    std::vector<Token> clock;
    std::vector<Token> disable;
    const bool cover_sequence
        = directive.form == SystemVerilogConcurrentAssertionForm::Sequence;
    if (cover_sequence) {
        if (!directive.inline_sequence) {
            return fail("a sequence directive without its sequence");
        }
        const auto& sequence = *directive.inline_sequence;
        if (!sequence.local_variables.empty()) {
            return fail("assertion local variables");
        }
        tokens = sequence.expression_tokens;
        if (sequence.clock) {
            clock = sequence.clock->event_tokens;
        }
        if (sequence.disable) {
            disable = sequence.disable->condition_tokens;
        }
    } else {
        tokens = directive.property_tokens;
    }
    strip_semicolons(tokens);
    // A property that is one instance of a declaration takes that
    // declaration's clock and disable condition (16.12, 16.13).
    for (std::size_t depth = 0; depth < 16U && !tokens.empty(); ++depth) {
        if (tokens.front().kind != TokenKind::Identifier) {
            break;
        }
        const auto declaration = std::ranges::find_if(
            unit.systemverilog_assertion_declarations,
            [&](const SystemVerilogAssertionDeclaration& entry) {
                return entry.kind != SystemVerilogAssertionDeclarationKind::Checker
                    && entry.name == tokens.front().text;
            });
        if (declaration == unit.systemverilog_assertion_declarations.end()) {
            break;
        }
        std::vector<std::vector<Token>> actuals;
        if (tokens.size() > 1U) {
            if (tokens[1].kind != TokenKind::LeftParen
                || tokens.back().kind != TokenKind::RightParen) {
                break;
            }
            int depth_count { };
            std::size_t close { };
            for (std::size_t position = 1U; position < tokens.size(); ++position) {
                if (tokens[position].kind == TokenKind::LeftParen) {
                    ++depth_count;
                } else if (tokens[position].kind == TokenKind::RightParen
                    && --depth_count == 0) {
                    close = position;
                    break;
                }
            }
            if (close + 1U != tokens.size()) {
                break;
            }
            const std::span<const Token> inside {
                tokens.data() + 2, tokens.size() - 3U
            };
            if (!inside.empty()) {
                actuals = split_top_level(inside, TokenKind::Comma);
            }
        }
        if (!declaration->local_variables.empty()) {
            return fail("assertion local variables");
        }
        auto body = TreeParser::substituted(
            *declaration, actuals, declaration->expression_tokens);
        if (!body) {
            return fail("the instance actuals do not match the formals");
        }
        if (declaration->clock) {
            auto event = TreeParser::substituted(
                *declaration, actuals, declaration->clock->event_tokens);
            if (!event) {
                return fail("the instance clock does not substitute");
            }
            if (!clock.empty() && spelling(clock) != spelling(*event)) {
                return fail("more than one clocking event");
            }
            clock = std::move(*event);
        }
        if (declaration->disable) {
            auto condition = TreeParser::substituted(
                *declaration, actuals, declaration->disable->condition_tokens);
            if (!condition || !disable.empty()) {
                return fail("more than one disable condition");
            }
            disable = std::move(*condition);
        }
        tokens = std::move(*body);
        strip_semicolons(tokens);
    }
    // A leading clocking event and disable condition in the property.
    if (!tokens.empty() && tokens.front().kind == TokenKind::At) {
        std::size_t end = 1U;
        std::vector<Token> event;
        if (tokens.size() > 1U && tokens[1].kind == TokenKind::LeftParen) {
            int depth_count { };
            for (std::size_t position = 1U; position < tokens.size(); ++position) {
                if (tokens[position].kind == TokenKind::LeftParen) {
                    ++depth_count;
                } else if (tokens[position].kind == TokenKind::RightParen
                    && --depth_count == 0) {
                    end = position + 1U;
                    break;
                }
            }
            event.assign(tokens.begin() + 2,
                tokens.begin() + static_cast<std::ptrdiff_t>(end - 1U));
        } else if (tokens.size() > 1U) {
            event.push_back(tokens[1]);
            end = 2U;
        }
        if (!clock.empty() && spelling(clock) != spelling(event)) {
            return fail("more than one clocking event");
        }
        clock = std::move(event);
        tokens.erase(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(end));
    }
    if (tokens.size() > 2U && token_is_keyword(tokens[0], "disable")
        && token_is_keyword(tokens[1], "iff") && tokens[2].kind == TokenKind::LeftParen) {
        int depth_count { };
        std::size_t end { };
        for (std::size_t position = 2U; position < tokens.size(); ++position) {
            if (tokens[position].kind == TokenKind::LeftParen) {
                ++depth_count;
            } else if (tokens[position].kind == TokenKind::RightParen
                && --depth_count == 0) {
                end = position;
                break;
            }
        }
        if (end == 0U || !disable.empty()) {
            return fail("more than one disable condition");
        }
        disable.assign(tokens.begin() + 3, tokens.begin() + static_cast<std::ptrdiff_t>(end));
        tokens.erase(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(end + 1U));
    }
    // The clock inferred from a procedure (16.14.5), then default clocking
    // and default disable iff (16.15, 16.16).
    if (clock.empty()) {
        clock = directive.procedural_clock;
    }
    if (clock.empty() && unit.systemverilog_default_clocking_block) {
        Token name;
        name.kind = TokenKind::Identifier;
        name.text = *unit.systemverilog_default_clocking_block;
        name.span = directive.span;
        clock.push_back(std::move(name));
    }
    if (clock.empty()) {
        return fail("no clocking event");
    }
    if (disable.empty()) {
        disable = unit.systemverilog_default_disable_tokens;
    }
    {
        const auto default_disable
            = unit.systemverilog_default_disable_tokens.empty()
            ? synthetic_tokens("1'b0", directive.span)
            : unit.systemverilog_default_disable_tokens;
        tokens = TreeParser::inferred(std::move(tokens), clock, default_disable);
        disable = TreeParser::inferred(std::move(disable), clock, default_disable);
    }
    if (tokens.empty()) {
        return fail("an empty property");
    }

    ParseContext context {
        unit,
        [this](const std::span<const Token> operand) {
            // Sequence and property operators never occur in an expression.
            static constexpr std::array<std::string_view, 20> operators {
                "and", "or", "not", "intersect", "within", "throughout",
                "iff", "implies", "until", "s_until", "until_with",
                "s_until_with", "first_match", "nexttime", "s_nexttime",
                "always", "s_always", "eventually", "s_eventually", "strong",
            };
            if (std::ranges::any_of(operand, [](const Token& token) {
                    return token.kind == TokenKind::Hash
                        || (token.kind == TokenKind::Identifier
                            && std::ranges::find(operators, token.text)
                                != operators.end());
                })) {
                return false;
            }
            const auto saved = diagnostics_.size();
            const bool valid = assertion_expression(operand).has_value();
            diagnostics_.resize(saved);
            return valid;
        },
        clock,
        unit.systemverilog_default_disable_tokens.empty()
            ? synthetic_tokens("1'b0", directive.span)
            : unit.systemverilog_default_disable_tokens,
        { },
    };
    std::vector<Node> nodes;
    TreeParser parser { nodes, tokens, context, 0U };
    const auto root = parser.parse();
    if (!root) {
        return fail(context.failure);
    }
    if (!disable.empty() && !context.boolean(disable)) {
        return fail("the disable condition is not a boolean expression");
    }
    PlanCompiler compiler { nodes, synthetic_tokens("1'b1", directive.span) };
    compiler.compile(*root, cover_sequence
            && directive.kind == SystemVerilogConcurrentAssertionKind::Cover);
    SvaPrepared prepared;
    prepared.plan = semantic::sva::encode(compiler.plan);
    prepared.guards = std::move(compiler.guards);
    prepared.bounds = std::move(compiler.bounds);
    prepared.clock = std::move(clock);
    prepared.disable = std::move(disable);
    return prepared;
}

std::vector<Token> VerilogParser::sva_tick_call(const SvaPrepared& prepared,
    const std::uint32_t site, const std::span<const Token> start,
    const SourceSpan& span, const bool sampled)
{
    std::vector<Token> call;
    const auto emit = [&](const std::string_view text) {
        auto lexed = synthetic_tokens(text, span);
        call.insert(call.end(), lexed.begin(), lexed.end());
    };
    const auto emit_truth = [&](const std::span<const Token> condition) {
        emit("((|(");
        call.insert(call.end(), condition.begin(), condition.end());
        emit(")) === 1'b1)");
    };
    // Outside an assertion process the guards read their sampled values
    // explicitly (16.5.1).
    const auto emit_guard = [&](const std::span<const Token> condition) {
        if (!sampled) {
            emit_truth(condition);
            return;
        }
        emit("$sampled(");
        emit_truth(condition);
        emit(")");
    };
    emit("$__fsim_sva_tick(" + std::to_string(site) + ", \"" + prepared.plan
        + "\", " + std::to_string(prepared.guards.size()) + ", ");
    if (prepared.guards.empty()) {
        emit("1'b0");
    } else {
        emit("{");
        for (auto guard = prepared.guards.rbegin();
             guard != prepared.guards.rend(); ++guard) {
            if (guard != prepared.guards.rbegin()) {
                emit(",");
            }
            emit_guard(*guard);
        }
        emit("}");
    }
    emit(", ");
    call.insert(call.end(), start.begin(), start.end());
    emit(", ");
    if (prepared.disable.empty()) {
        emit("1'b0");
    } else {
        emit_truth(prepared.disable);
    }
    for (const auto& bound : prepared.bounds) {
        emit(", (");
        call.insert(call.end(), bound.begin(), bound.end());
        emit(")");
    }
    emit(")");
    return call;
}

void VerilogParser::resolve_expect_statements(const DesignUnit& scope,
    std::vector<Process>& processes,
    std::vector<TaskDeclaration>& tasks)
{
    using namespace assertion_resolution_detail;
    const auto resolve = [&](Statement& statement) {
        const auto directive = std::move(*statement.procedural_concurrent_assertion);
        statement = Statement { };
        statement.span = directive.span;
        std::string reason;
        const auto prepared = prepare_sva_property(scope, directive, reason);
        if (!prepared) {
            Token location;
            location.kind = TokenKind::Identifier;
            location.text = "expect";
            location.span = directive.span;
            error(location, "FSIM-SV-SEM-200",
                "the expect property cannot be evaluated: " + reason);
            return;
        }
        // Every expect statement has its own monitor site: a process may
        // run several, directly or through tasks.
        const auto site = ++expect_sites_;
        const auto prefix = "__fsim_ex" + std::to_string(site);
        std::vector<Token> body;
        const auto emit = [&](const std::string_view text) {
            auto lexed = synthetic_tokens(text, directive.span);
            body.insert(body.end(), lexed.begin(), lexed.end());
        };
        // One attempt starts at the next clocking event; the statement
        // completes with the attempt (16.17).
        emit("begin int " + prefix + "_n; int " + prefix + "_o; bit " + prefix
            + "_s; " + prefix + "_s = 1'b1; " + prefix + "_o = 0; while ("
            + prefix + "_o == 0) begin @(");
        body.insert(body.end(), prepared->clock.begin(), prepared->clock.end());
        emit(") " + prefix + "_n = ");
        const auto call = sva_tick_call(*prepared, site,
            synthetic_tokens(prefix + "_s", directive.span), directive.span,
            true);
        body.insert(body.end(), call.begin(), call.end());
        emit("; " + prefix + "_s = 1'b0; if (" + prefix + "_n != 0) " + prefix
            + "_o = $__fsim_sva_outcome(" + std::to_string(site) + "); end if ("
            + prefix + "_o == 1 || " + prefix + "_o == 3) __fsim_act_success(); "
            "else if (" + prefix + "_o == 2) __fsim_act_failure(); end");
        auto generated = parse_generated_statement(std::move(body), directive.span);
        if (!generated) {
            return;
        }
        const auto pass_actions = directive.pass_action_statements.empty()
            ? action_statements(directive.pass_action_tokens)
            : directive.pass_action_statements;
        auto failure_actions = directive.failure_action_statements.empty()
            ? action_statements(directive.failure_action_tokens)
            : directive.failure_action_statements;
        if (failure_actions.empty() && !directive.has_failure_action) {
            Statement report;
            report.kind = StatementKind::Report;
            report.output_text = "expect failed";
            report.assertion_severity = AssertionSeverity::Error;
            report.span = directive.span;
            failure_actions.push_back(std::move(report));
        }
        const auto substitute = [&](const auto& self, Statement& item) -> void {
            if (item.kind == StatementKind::TaskCall
                && item.task_name.starts_with("__fsim_act_")) {
                Statement replacement;
                replacement.kind = StatementKind::Block;
                replacement.span = directive.span;
                const auto& actions = item.task_name == "__fsim_act_success"
                    ? pass_actions
                    : failure_actions;
                replacement.statements.assign(actions.begin(), actions.end());
                item = std::move(replacement);
                return;
            }
            for (auto& child : item.statements) {
                self(self, child);
            }
            for (auto& child : item.else_statements) {
                self(self, child);
            }
        };
        substitute(substitute, *generated);
        statement = std::move(*generated);
    };
    const auto walk = [&](const auto& self, auto& statements) -> void {
        for (auto& statement : statements) {
            if (statement.procedural_concurrent_assertion
                && statement.procedural_concurrent_assertion->expect_statement) {
                resolve(statement);
                continue;
            }
            self(self, statement.statements);
            self(self, statement.else_statements);
            for (auto& alternative : statement.case_alternatives) {
                self(self, alternative.statements);
            }
        }
    };
    for (auto& process : processes) {
        walk(walk, process.statements);
    }
    for (auto& task : tasks) {
        walk(walk, task.statements);
    }
}

bool VerilogParser::resolve_sva_directive(
    DesignUnit& unit, const std::size_t index)
{
    using namespace assertion_resolution_detail;
    const auto& directive = unit.systemverilog_concurrent_assertions[index];
    const auto reject = [&](const std::string& reason) {
        if (trace_enabled()) {
            std::cerr << "fsim sva: directive " << index << " in "
                      << unit.name << " uses the earlier evaluator: " << reason
                      << '\n';
        }
        return false;
    };
    std::string reason;
    const auto prepared = prepare_sva_property(unit, directive, reason);
    if (!prepared) {
        return reject(reason);
    }
    const auto& encoded = prepared->plan;
    if (trace_enabled()) {
        std::cerr << "fsim sva: directive " << index << " in " << unit.name
                  << " plan " << encoded << '\n';
    }

    // The actions of each outcome, as the earlier evaluators ran them.
    Process process;
    process.kind = ProcessKind::VerilogAlways;
    process.systemverilog_concurrent_assertion = true;
    process.name = directive.label.empty()
        ? "$assertion$" + std::to_string(index + 1U)
        : directive.label;
    process.span = directive.span;
    const auto kind_name
        = directive.kind == SystemVerilogConcurrentAssertionKind::Assume
        ? "assumption"
        : directive.kind == SystemVerilogConcurrentAssertionKind::Cover
        ? "cover"
        : directive.kind == SystemVerilogConcurrentAssertionKind::Restrict
        ? "restriction"
        : "assertion";
    const auto marker = [&](const std::string_view prefix,
                            const std::string_view outcome) {
        Statement statement;
        statement.kind = StatementKind::Display;
        statement.output_newline = false;
        statement.output_text = std::string { "\x1f" } + std::string { prefix }
            + std::to_string(index) + "|" + kind_name + "|"
            + std::string { outcome } + "|" + process.name;
        statement.span = directive.span;
        return statement;
    };
    const auto coverage_marker = [&](const std::string_view outcome) {
        return marker("fsim.concurrent-assertion|", outcome);
    };
    const auto action_region_marker = [&] {
        Statement statement;
        statement.kind = StatementKind::Display;
        statement.output_newline = false;
        statement.output_text = std::string { "\x1f"
                                              "fsim.concurrent-assertion-action-region|" }
            + process.name;
        statement.span = directive.span;
        return statement;
    };
    const auto append_actions = [&](std::vector<Statement>& destination,
                                    const std::span<const Statement> actions) {
        if (actions.empty()) {
            return;
        }
        destination.push_back(action_region_marker());
        destination.insert(destination.end(), actions.begin(), actions.end());
    };
    const auto pass_actions = directive.pass_action_statements.empty()
        ? action_statements(directive.pass_action_tokens)
        : directive.pass_action_statements;
    const auto failure_actions = directive.failure_action_statements.empty()
        ? action_statements(directive.failure_action_tokens)
        : directive.failure_action_statements;
    Statement assertion;
    assertion.kind = StatementKind::Assert;
    assertion.span = directive.span;
    assertion.assertion_message
        = "concurrent assertion '" + process.name + "' failed";
    assertion.assertion_has_pass_action = true;
    assertion.statements.push_back(coverage_marker("pass"));
    append_actions(assertion.statements, pass_actions);
    assertion.assertion_has_failure_action = true;
    assertion.else_statements.push_back(coverage_marker("failure"));
    append_actions(assertion.else_statements, failure_actions);
    const bool checks = directive.kind == SystemVerilogConcurrentAssertionKind::Assert
        || directive.kind == SystemVerilogConcurrentAssertionKind::Assume;
    if (failure_actions.empty() && !directive.has_failure_action && checks) {
        Statement report;
        report.kind = StatementKind::Report;
        report.output_text = assertion.assertion_message;
        report.assertion_severity = AssertionSeverity::Error;
        report.span = directive.span;
        assertion.else_statements.push_back(action_region_marker());
        assertion.else_statements.push_back(std::move(report));
    }
    auto success = assertion;
    success.condition = Expression {
        ExpressionKind::IntegerLiteral, "1", { }, directive.span };
    auto failure = assertion;
    failure.condition = Expression {
        ExpressionKind::IntegerLiteral, "0", { }, directive.span };
    std::vector<Statement> vacuous { coverage_marker("vacuous") };
    append_actions(vacuous, pass_actions);
    const std::vector<Statement> disabled { coverage_marker("abort") };

    // The obligation marker registers the actions replayed for each attempt
    // the monitor fails at the end of simulation (16.12.2 strong
    // obligations); it shares the pending-attempt marker format.
    const auto obligation_marker = [&](const std::string_view outcome,
                                       const std::span<const Statement> actions) {
        Statement obligation = marker(
            "fsim.concurrent-assertion-pending|", std::string { outcome });
        constexpr char digits[] = "0123456789abcdef";
        const auto encode = [&](const std::string_view text) {
            std::string result;
            for (const auto character : text) {
                const auto byte = static_cast<unsigned char>(character);
                result.push_back(digits[byte >> 4U]);
                result.push_back(digits[byte & 0x0fU]);
            }
            return result;
        };
        std::vector<const Statement*> replayed;
        const auto collect = [&](const auto& self,
                                 const std::span<const Statement> items) -> void {
            for (const auto& item : items) {
                if (item.kind == StatementKind::Block && item.label.empty()) {
                    self(self, std::span<const Statement> { item.statements });
                } else if (item.kind == StatementKind::Display
                    && item.output_text.starts_with("\x1f")) {
                    continue;
                } else if (item.kind == StatementKind::Display
                    || item.kind == StatementKind::Report) {
                    replayed.push_back(&item);
                }
            }
        };
        collect(collect, actions);
        for (const auto* action : replayed) {
            obligation.output_text += "|";
            obligation.output_text += action->kind == StatementKind::Report ? "R" : "D";
            obligation.output_text += ","
                + std::to_string(static_cast<std::underlying_type_t<AssertionSeverity>>(
                    action->assertion_severity));
            obligation.output_text += action->output_newline ? ",1," : ",0,";
            obligation.output_text += encode(action->output_text) + ",";
            obligation.output_text += encode(action->span.source_name) + ",";
            obligation.output_text += std::to_string(action->span.begin.line) + ",";
            obligation.output_text += std::to_string(action->span.begin.column) + ",";
            obligation.output_text += std::to_string(
                static_cast<std::underlying_type_t<GeneratedTextKind>>(
                    action->output_generated_text));
        }
        return obligation;
    };
    // Attempts still running at the end of simulation that hold on the
    // truncated trace report a vacuous success with the pass actions.
    auto obligation = obligation_marker(
        "obligation", std::span<const Statement> { assertion.else_statements });
    auto vacuous_obligation = obligation_marker(
        "obligation-vacuous", std::span<const Statement> { vacuous });

    // The process body: wait for the clocking event, sample the guards,
    // advance the monitor, and run each outcome's action.
    const auto prefix = "__fsim_sva" + std::to_string(index);
    std::vector<Token> body;
    const auto emit = [&](const std::string_view text) {
        auto lexed = synthetic_tokens(text, directive.span);
        body.insert(body.end(), lexed.begin(), lexed.end());
    };
    emit("begin int " + prefix + "_n; int " + prefix + "_o; @(");
    body.insert(body.end(), prepared->clock.begin(), prepared->clock.end());
    emit(") begin " + prefix + "_n = ");
    {
        // A procedural assertion starts an attempt only when its procedure
        // reached the statement (16.14.6.2); the lowering reads this
        // operand's current value.
        const auto call = sva_tick_call(*prepared, 0U,
            synthetic_tokens(directive.procedural_enable.empty()
                    ? std::string { "1'b1" }
                    : directive.procedural_enable,
                directive.span),
            directive.span);
        body.insert(body.end(), call.begin(), call.end());
    }
    const auto outcome = prefix + "_o";
    emit("; ");
    if (!directive.procedural_enable.empty()) {
        emit(directive.procedural_enable + " = 1'b0; ");
    }
    emit("repeat (" + prefix + "_n) begin " + outcome
        + " = $__fsim_sva_outcome(0); if (" + outcome
        + " == 1) __fsim_act_success(); else if (" + outcome
        + " == 2) __fsim_act_failure(); else if (" + outcome
        + " == 3) __fsim_act_vacuous(); else if (" + outcome
        + " == 4) __fsim_act_abort(); end end end");
    auto statement = parse_generated_statement(std::move(body), directive.span);
    if (!statement) {
        return reject("the generated process does not parse");
    }
    // Replace the action placeholders.
    const auto substitute = [&](const auto& self, Statement& item) -> void {
        if (item.kind == StatementKind::TaskCall
            && item.task_name.starts_with("__fsim_act_")) {
            const auto which = item.task_name.substr(11U);
            Statement replacement;
            replacement.kind = StatementKind::Block;
            replacement.span = directive.span;
            if (which == "success") {
                replacement.statements = { success };
            } else if (which == "failure") {
                replacement.statements = { failure };
            } else if (which == "vacuous") {
                replacement.statements = vacuous;
            } else {
                replacement.statements = disabled;
            }
            item = std::move(replacement);
            return;
        }
        for (auto& child : item.statements) {
            self(self, child);
        }
        for (auto& child : item.else_statements) {
            self(self, child);
        }
    };
    substitute(substitute, *statement);
    process.statements.push_back(coverage_marker("register"));
    if (checks) {
        process.statements.push_back(std::move(obligation));
        process.statements.push_back(std::move(vacuous_obligation));
    }
    process.statements.push_back(std::move(*statement));
    unit.processes.push_back(std::move(process));
    return true;
}

} // namespace fsim::frontend
