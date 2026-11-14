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
#include "fsim/frontend/design.hpp"
#include "fsim/frontend/design_core.hpp"
#include "fsim/frontend/source.hpp"
#include "fsim/frontend/token.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

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
    // `@(c) s`: s on clock `clock` of a multiclocked sequence (16.13.1).
    clocked,
    // A named sequence or property instance kept as such (the published
    // tree, IEEE 1800-2023 37.50-37.54) instead of expanded.
    instance,
    // `@(c) p` over a property: p on clock `clock` (16.13.2, 16.13.3). The
    // published tree (`keep_instances`) also uses it for one that restates
    // the clock in effect (37.52 clocked property).
    clocked_property,
    // The published tree only: a property `case` with its items (37.52 case
    // property, case property item). The plan builders never see them.
    case_property,
    case_item,
    // `(s, item, ...)`: sequence s with the match items in actual_tokens
    // performed at the end of each match (IEEE 1800-2023 16.10, 16.11); a
    // `leading` one holds the initialization assignments of a named
    // sequence instance's local variables, performed when the instance's
    // evaluation begins.
    match_items,
    // A property instance whose local variables have initialization
    // assignments (actual_tokens), performed when its evaluation begins.
    initialized,
    // The expansion of an instance of a recursive property (16.12.17), the
    // node its recursive instances return to (operand: the expanded body).
    recursion_target,
    // A recursive instance (16.12.17): a new evaluation of the expansion
    // `target`, whose local formal arguments first take the values of
    // the instance's actuals (actual_tokens: the assignments, in order).
    recursion,
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
    // The clock of a clocked node: an index into ParseContext::clocks.
    std::uint32_t clock { };
    // An instance: the declaration it names, whether that is a sequence,
    // and the actuals as written.
    std::string instance;
    const SystemVerilogAssertionDeclaration* declared { };
    // The recursion_target node a recursion node evaluates again.
    std::size_t target { };
    bool sequence_instance { };
    // An instance's actuals as written; a case item's expressions.
    std::vector<std::vector<Token>> actual_tokens;
    // The default item of a published property case.
    bool default_item { };
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
    case NodeKind::clocked:
    case NodeKind::match_items:
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
    std::vector<Token> clock { };
    // The scope's default disable condition, `1'b0` when none (16.14.7).
    std::vector<Token> inferred_disable { };
    std::string failure { };
    // The clocking events of the property, the directive's first; the
    // clock in effect flows left to right (16.13.3, 16.16).
    std::vector<std::vector<Token>> clocks { };
    std::uint32_t current_clock { };
    // Keep a named instance as an instance node (the published tree).
    bool keep_instances { };
    // Each clock's identity (ResolvedClock::identity), parallel to clocks.
    std::vector<std::string> clock_identities { };
    // The scopes the names of the clocking events resolve in, the module
    // first (SvaNameScope), and the one the tokens being parsed are written
    // in: the directive's (the last) or an instantiated declaration's.
    std::vector<SvaNameScope> scopes { };
    std::size_t origin { };
    // The local variables of the property (IEEE 1800-2023 16.10): each
    // declaration's locals are renamed to names unique in the property
    // (`__fsim_lvN`, N the index), so every instance has its own copy.
    struct Local {
        std::string name;
        std::vector<Token> type;
    };
    std::vector<Local> locals { };
    std::set<std::string, std::less<>> local_names { };
    // Whether the property uses local variables or match items.
    bool uses_locals { };
    // Whether `failure` is a form the standard makes illegal.
    bool illegal { };
    // Whether the property operators IEEE 1800-2009 added (Annex B) are
    // keywords: under an earlier keyword set they are identifiers.
    bool keywords_2009 { true };
    // The replacement of a read of a sequence's end point,
    // `receiver.triggered` or `receiver.matched` (16.13.5, 16.13.6), on
    // the clocking event in effect (VerilogParser::sva_endpoint_use); unset
    // for the published tree, which keeps the method.
    std::function<std::optional<std::vector<Token>>(
        const SystemVerilogAssertionDeclaration&, std::span<const Token>,
        bool, std::string&, std::string&)>
        endpoint { };
    // The tick bits of the end-point monitors the plan's reads wait for.
    std::vector<std::string> endpoint_waits { };
    // The expansions of recursive property instances in progress, the
    // outermost first (16.12.17): the declaration, the identity of the
    // actuals of its formals that are not local variables, the
    // recursion_target node, the clock in effect, and the unique names of
    // the declaration's local formals and local variables.
    struct Expansion {
        const SystemVerilogAssertionDeclaration* declaration { };
        std::string key;
        std::size_t target { };
        std::uint32_t clock { };
        std::map<std::string, std::string, std::less<>> locals;
    };
    std::vector<Expansion> expansions { };
    // The recursive property declarations that break a restriction of
    // 16.12.17 (VerilogParser::sva_illegal_recursive_), by name, line and
    // offset of the name: an error was reported for each.
    const std::set<std::tuple<std::string, std::size_t, std::size_t>>*
        illegal_recursive { };
};

// The property and sequence operator keywords IEEE 1800-2009 added
// (Annex B, Table B.1).
[[nodiscard]] bool property_keyword_2009(const std::string_view text)
{
    static constexpr std::array<std::string_view, 16> words {
        "accept_on", "eventually", "implies", "nexttime", "reject_on",
        "s_always", "s_eventually", "s_nexttime", "s_until", "s_until_with",
        "strong", "sync_accept_on", "sync_reject_on", "until", "until_with",
        "weak",
    };
    return std::ranges::find(words, text) != words.end();
}

// Whether `tokens` reference one of `names` (not as a member after `.`).
[[nodiscard]] bool mentions_local(const std::span<const Token> tokens,
    const std::set<std::string, std::less<>>& names)
{
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        if (tokens[position].kind == TokenKind::Identifier
            && names.contains(tokens[position].text)
            && (position == 0U
                || tokens[position - 1U].kind != TokenKind::Dot)) {
            return true;
        }
    }
    return false;
}

// `tokens` with each name of `names` (not a member after `.`) replaced.
[[nodiscard]] std::vector<Token> renamed(std::vector<Token> tokens,
    const std::map<std::string, std::string, std::less<>>& names)
{
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        auto& token = tokens[position];
        if (token.kind != TokenKind::Identifier
            || (position > 0U && tokens[position - 1U].kind == TokenKind::Dot)) {
            continue;
        }
        if (const auto found = names.find(token.text); found != names.end()) {
            token.text = found->second;
        }
    }
    return tokens;
}

// The keywords of an event expression (9.4.2); every other identifier that
// starts a name is a name.
[[nodiscard]] bool event_keyword(const Token& token)
{
    return token_is_keyword(token, "posedge") || token_is_keyword(token, "negedge")
        || token_is_keyword(token, "edge") || token_is_keyword(token, "iff")
        || token_is_keyword(token, "or");
}

// The scope (an index into `scopes`) a name written in scope `origin`
// resolves to: the innermost from `origin` outwards that declares it (23.9);
// none when it names nothing the assertion's scopes declare (a hierarchical
// name's first part that is no local instance, a package or $root).
[[nodiscard]] std::optional<std::size_t> name_scope(
    const std::vector<SvaNameScope>& scopes, const std::string& name,
    const std::size_t origin)
{
    if (scopes.empty()) {
        return std::nullopt;
    }
    for (auto level = std::min(origin, scopes.size() - 1U) + 1U; level-- > 0U;) {
        if (scopes[level].names.contains(name)) {
            return level;
        }
    }
    return std::nullopt;
}

// The scope a declaration of the unit's list belongs to (its index against
// SvaNameScope::declarations_end); the innermost when the scopes do not
// cover it.
[[nodiscard]] std::size_t declaration_scope(const ParseContext& context,
    const SystemVerilogAssertionDeclaration& declaration)
{
    const auto& declarations = context.unit.systemverilog_assertion_declarations;
    const auto found = std::ranges::find_if(declarations,
        [&](const SystemVerilogAssertionDeclaration& entry) {
            return &entry == &declaration;
        });
    const auto index = static_cast<std::size_t>(found - declarations.begin());
    for (std::size_t level = 0; level < context.scopes.size(); ++level) {
        if (index < context.scopes[level].declarations_end) {
            return level;
        }
    }
    return context.scopes.empty() ? 0U : context.scopes.size() - 1U;
}

// The sequence or property declaration (of `kind` when given) that a name
// written in scope `origin` denotes: the innermost declaration of that name
// in `origin` or a scope enclosing it (16.8, 23.9). Declarations after the
// scopes' ranges (copies of imported package declarations) are visible from
// every scope.
[[nodiscard]] const SystemVerilogAssertionDeclaration* visible_declaration(
    const DesignUnit& unit, const std::vector<SvaNameScope>& scopes,
    const std::string_view name, const std::size_t origin,
    const std::optional<SystemVerilogAssertionDeclarationKind> kind)
{
    const auto& declarations = unit.systemverilog_assertion_declarations;
    for (auto index = declarations.size(); index-- > 0U;) {
        const auto& entry = declarations[index];
        if (entry.kind == SystemVerilogAssertionDeclarationKind::Checker
            || entry.name != name || (kind && entry.kind != *kind)) {
            continue;
        }
        std::size_t level { };
        for (std::size_t candidate = 0; candidate < scopes.size(); ++candidate) {
            if (index < scopes[candidate].declarations_end) {
                level = candidate;
                break;
            }
        }
        if (level <= origin) {
            return &entry;
        }
    }
    return nullptr;
}

// Whether tokens [begin, end) are one name: identifiers joined by `.` or
// `::`.
[[nodiscard]] bool one_name(const std::span<const Token> tokens,
    const std::size_t begin, const std::size_t end)
{
    if (begin >= end || tokens[begin].kind != TokenKind::Identifier) {
        return false;
    }
    for (auto position = begin; position < end; ++position) {
        const auto kind = tokens[position].kind;
        const bool name = (position - begin) % 2U == 0U;
        if (name ? kind != TokenKind::Identifier
                 : kind != TokenKind::Dot && kind != TokenKind::Scope) {
            return false;
        }
    }
    return (end - begin) % 2U == 1U;
}

// A clocking event written in scope `origin`, resolved (16.13.1, 16.16.1):
// `identity` is the event with each name replaced by the scope that declares
// it and redundant parentheses left out, so two events are the same clock
// only when they take the same edges of the same objects under the same iff
// conditions, however they are spelled; `tokens` is the event as the
// directive's scope (the last) reads it, a module-scope name that a
// generate block redeclares qualified by the module's name (23.8).
struct ResolvedClock {
    std::string identity;
    std::vector<Token> tokens;
};

[[nodiscard]] ResolvedClock resolve_clock(const DesignUnit& unit,
    const std::vector<SvaNameScope>& scopes, const std::span<const Token> event,
    const std::size_t origin)
{
    ResolvedClock result;
    const auto innermost = scopes.empty() ? 0U : scopes.size() - 1U;
    // Parentheses around the whole event or around one name.
    std::vector<Token> tokens(event.begin(), event.end());
    const auto clocking_event = [&]() -> std::optional<ResolvedClock> {
        std::size_t begin { };
        auto end = tokens.size();
        while (end - begin >= 3U && tokens[begin].kind == TokenKind::LeftParen
            && tokens[end - 1U].kind == TokenKind::RightParen) {
            ++begin;
            --end;
        }
        if (end - begin != 1U || tokens[begin].kind != TokenKind::Identifier
            || event_keyword(tokens[begin])) {
            return std::nullopt;
        }
        const auto level = name_scope(scopes, tokens[begin].text, origin);
        if (!level) {
            return std::nullopt;
        }
        const auto found
            = scopes[*level].clocking_events.find(tokens[begin].text);
        // A block whose event names itself, directly or through other
        // blocks, keeps its name as its identity.
        thread_local std::vector<const std::vector<Token>*> expanding;
        if (found == scopes[*level].clocking_events.end()
            || found->second.empty()
            || std::ranges::find(expanding, &found->second)
                != expanding.end()) {
            return std::nullopt;
        }
        expanding.push_back(&found->second);
        auto resolved = resolve_clock(unit, scopes, found->second, *level);
        expanding.pop_back();
        return resolved;
    }();
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t open = 0; open < tokens.size() && !changed; ++open) {
            if (tokens[open].kind != TokenKind::LeftParen) {
                continue;
            }
            std::size_t depth { };
            std::size_t close = tokens.size();
            for (auto position = open; position < tokens.size(); ++position) {
                if (tokens[position].kind == TokenKind::LeftParen) {
                    ++depth;
                } else if (tokens[position].kind == TokenKind::RightParen
                    && --depth == 0U) {
                    close = position;
                    break;
                }
            }
            if (close == tokens.size()) {
                break;
            }
            const bool whole = open == 0U && close + 1U == tokens.size();
            const bool after_operator = open == 0U
                || tokens[open - 1U].kind != TokenKind::Identifier
                || event_keyword(tokens[open - 1U]);
            if (whole || (after_operator && one_name(tokens, open + 1U, close))) {
                tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(close));
                tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(open));
                changed = true;
            }
        }
    }
    for (std::size_t position = 0; position < tokens.size(); ++position) {
        const auto& token = tokens[position];
        if (!result.identity.empty()) {
            result.identity += ' ';
        }
        const bool starts_name = token.kind == TokenKind::Identifier
            && !event_keyword(token)
            && (position == 0U
                || (tokens[position - 1U].kind != TokenKind::Dot
                    && tokens[position - 1U].kind != TokenKind::Scope))
            && (position + 1U == tokens.size()
                || tokens[position + 1U].kind != TokenKind::Scope);
        if (!starts_name) {
            result.identity += token.text;
            continue;
        }
        // `m.x` inside module m names x of the module's scope (23.8).
        if (token.text == unit.name && position + 2U < tokens.size()
            && tokens[position + 1U].kind == TokenKind::Dot
            && tokens[position + 2U].kind == TokenKind::Identifier
            && !scopes.empty()
            && !name_scope(scopes, token.text, origin)
            && scopes.front().names.contains(tokens[position + 2U].text)) {
            position += 2U;
            result.identity += "@0:" + tokens[position].text;
            continue;
        }
        const auto level = name_scope(scopes, token.text, origin);
        result.identity += level ? "@" + std::to_string(*level) + ":" + token.text
                                 : "@?:" + token.text;
    }
    // The tokens the directive's scope reads: a name of the module's scope
    // that the directive's block redeclares is qualified by the module.
    for (std::size_t position = 0; position < event.size(); ++position) {
        const auto& token = event[position];
        const bool starts_name = token.kind == TokenKind::Identifier
            && !event_keyword(token)
            && (position == 0U
                || (event[position - 1U].kind != TokenKind::Dot
                    && event[position - 1U].kind != TokenKind::Scope))
            && (position + 1U == event.size()
                || event[position + 1U].kind != TokenKind::Scope);
        if (starts_name && origin != innermost
            && name_scope(scopes, token.text, origin) == std::optional<std::size_t> { 0U }
            && name_scope(scopes, token.text, innermost)
                != std::optional<std::size_t> { 0U }) {
            result.tokens.push_back(punctuation(token, TokenKind::Identifier, unit.name));
            result.tokens.push_back(punctuation(token, TokenKind::Dot, "."));
        }
        result.tokens.push_back(token);
    }
    // A clocking block name, `@(cb)`, is the block's clocking event (IEEE
    // 1800-2023 14.13, 16.16): the same clock as that event however it is
    // written, its names resolved where the block declares them.
    if (clocking_event) {
        result.identity = std::move(clocking_event->identity);
    }
    return result;
}

// Builds the property tree of one token sequence; a named sequence or
// property instance is expanded by a nested parser over its body.
class TreeParser {
public:
    TreeParser(std::vector<Node>& nodes, std::vector<Token> tokens,
        ParseContext& context,
        std::vector<const SystemVerilogAssertionDeclaration*> path)
        : nodes_(nodes)
        , tokens_(std::move(tokens))
        , context_(context)
        , path_(std::move(path))
    {
    }

    [[nodiscard]] std::optional<std::size_t> parse()
    {
        // The scope of a clocking event does not flow out of enclosing
        // parentheses (16.13.3).
        const auto enclosing = context_.current_clock;
        auto result = implication();
        context_.current_clock = enclosing;
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

    // A form IEEE 1800-2023 makes illegal (an error, not a limitation).
    std::optional<std::size_t> fail_illegal(std::string reason)
    {
        if (context_.failure.empty()) {
            context_.failure = std::move(reason);
            context_.illegal = true;
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
            && token_is_keyword(tokens_[position_ + offset], text)
            && (context_.keywords_2009 || !property_keyword_2009(text));
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
        return nodes_[node].kind == NodeKind::instance
            ? nodes_[node].sequence_instance
            : sequence_kind(nodes_[node].kind);
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
        const auto incoming = context_.current_clock;
        auto left = until();
        if (!left) {
            return left;
        }
        const bool implies = at_implication();
        if (!implies && !at_followed_by()) {
            return left;
        }
        // A clocked operand in the antecedent position is a clocked
        // sequence (`(@(c) a) |=> p`, A.2.10), not a clocked property.
        if (nodes_[*left].kind == NodeKind::clocked_property
            && is_sequence(nodes_[*left].operands[0])) {
            nodes_[*left].kind = NodeKind::clocked;
        }
        if (!is_sequence(*left)) {
            return fail("the antecedent of an implication must be a sequence");
        }
        Node node;
        node.kind = implies ? NodeKind::implication : NodeKind::followed_by;
        node.overlapped = implies ? at(TokenKind::Pipe)
                                  : at(TokenKind::Minus, 1);
        position_ += implies ? 2U : 3U;
        // A clock that the antecedent changed to flows across the operator
        // into the consequent (16.13.3).
        const auto flowing = context_.current_clock;
        auto right = implication();
        if (!right) {
            return right;
        }
        if (!context_.keep_instances && flowing != incoming
            && nodes_[*right].kind != NodeKind::clocked
            && nodes_[*right].kind != NodeKind::clocked_property) {
            right = clocked(flowing, right);
            if (!right) {
                return right;
            }
        }
        node.operands = { *left, *right };
        return add(std::move(node));
    }

    std::optional<std::size_t> until()
    {
        const auto incoming = context_.current_clock;
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
        // The clock in effect distributes to the operands of a branching
        // operator (16.13.3).
        context_.current_clock = incoming;
        auto right = until();
        context_.current_clock = incoming;
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
        const auto incoming = context_.current_clock;
        auto left = disjunction();
        if (!left || !at_keyword("iff")) {
            return left;
        }
        ++position_;
        context_.current_clock = incoming;
        auto right = iff();
        context_.current_clock = incoming;
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
        const auto incoming = context_.current_clock;
        auto left = conjunction();
        while (left && at_keyword("or")) {
            ++position_;
            context_.current_clock = incoming;
            auto right = conjunction();
            context_.current_clock = incoming;
            if (!right) {
                return right;
            }
            Node node;
            node.kind = is_sequence(*left) && is_sequence(*right)
                    && singly_clocked(*left, *right, incoming)
                ? NodeKind::sequence_or
                : NodeKind::disjunction;
            node.operands = { *left, *right };
            left = add(std::move(node));
        }
        return left;
    }

    std::optional<std::size_t> conjunction()
    {
        const auto incoming = context_.current_clock;
        auto left = unary();
        while (left && at_keyword("and")) {
            ++position_;
            context_.current_clock = incoming;
            auto right = unary();
            context_.current_clock = incoming;
            if (!right) {
                return right;
            }
            Node node;
            node.kind = is_sequence(*left) && is_sequence(*right)
                    && singly_clocked(*left, *right, incoming)
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
            // An abort condition shall not reference local variables
            // (16.12.14).
            if (!context_.local_names.empty()
                && mentions_local(node.expression, context_.local_names)) {
                return fail_illegal("an abort condition that references a "
                                    "local variable (16.12.14)");
            }
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
            const auto incoming = context_.current_clock;
            auto then = implication();
            context_.current_clock = incoming;
            if (!then) {
                return then;
            }
            node.operands = { *then };
            if (at_keyword("else")) {
                ++position_;
                auto otherwise = implication();
                context_.current_clock = incoming;
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
            TreeParser inner { nodes_, slice(position_, end), context_, path_ };
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
        if (context_.keep_instances) {
            // The published tree keeps the case items as written (37.52
            // case property; the plan builders get the desugared chain).
            Node whole;
            whole.kind = NodeKind::case_property;
            whole.expression = selector;
            for (auto& item : items) {
                Node published;
                published.kind = NodeKind::case_item;
                published.actual_tokens = std::move(item.values);
                published.operands = { item.property };
                whole.operands.push_back(add(std::move(published)));
            }
            if (otherwise) {
                Node published;
                published.kind = NodeKind::case_item;
                published.default_item = true;
                published.operands = { *otherwise };
                whole.operands.push_back(add(std::move(published)));
            }
            if (whole.operands.empty()) {
                return fail("a property case has no items");
            }
            return add(std::move(whole));
        }
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
            auto right = delay_operand();
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
            auto right = delay_operand();
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

    // The right operand of a cycle delay, where a clocking event governs a
    // sequence (`clocking_event sequence_expr`, 16.13.1).
    std::optional<std::size_t> delay_operand()
    {
        const bool enclosing = std::exchange(delay_operand_, true);
        auto result = repetition();
        delay_operand_ = enclosing;
        return result;
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
        // An instance followed by a method call (`s(2).triggered`,
        // `s(2).matched`) is the boolean endpoint of a sequence instance
        // (16.13.6), not an instance in the property.
        if (at_position + 1U < tokens_.size()
            && tokens_[at_position + 1U].kind == TokenKind::LeftParen) {
            const auto close = matching(at_position + 1U);
            if (close && *close + 1U < tokens_.size()
                && tokens_[*close + 1U].kind == TokenKind::Dot) {
                return nullptr;
            }
        }
        return visible_declaration(context_.unit, context_.scopes,
            tokens_[at_position].text, context_.origin, std::nullopt);
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
        if (context_.endpoint) {
            if (auto failure = replace_endpoints(node.expression)) {
                return fail(std::move(*failure));
            }
        }
        if (!context_.boolean(node.expression)) {
            return fail("'" + spelling(node.expression)
                + "' is not a boolean expression");
        }
        position_ = end;
        return add(std::move(node));
    }

    // Replaces each read of a sequence's end point in a boolean,
    // `s.triggered`, `s(a).matched`, `(s).triggered()` (a substituted
    // sequence formal), with the end-point monitor's state (16.13.6);
    // the failure when one cannot be evaluated.
    std::optional<std::string> replace_endpoints(std::vector<Token>& tokens)
    {
        for (std::size_t index = 1U; index < tokens.size(); ++index) {
            if (tokens[index].kind != TokenKind::Identifier
                || (tokens[index].text != "triggered"
                    && tokens[index].text != "matched")
                || tokens[index - 1U].kind != TokenKind::Dot
                || index < 2U) {
                continue;
            }
            // The receiver ends before the dot: a name, a call of a name,
            // or a parenthesized substituted actual.
            const auto receiver_end = index - 1U;
            auto receiver_begin = receiver_end - 1U;
            if (tokens[receiver_begin].kind == TokenKind::RightParen) {
                int depth { };
                auto open = receiver_begin;
                for (;; --open) {
                    if (tokens[open].kind == TokenKind::RightParen) {
                        ++depth;
                    } else if (tokens[open].kind == TokenKind::LeftParen
                        && --depth == 0) {
                        break;
                    }
                    if (open == 0U) {
                        return "an unbalanced sequence method receiver";
                    }
                }
                receiver_begin = open;
                if (open > 0U && tokens[open - 1U].kind == TokenKind::Identifier) {
                    receiver_begin = open - 1U;
                }
            } else if (tokens[receiver_begin].kind != TokenKind::Identifier) {
                continue;
            }
            std::vector<Token> receiver(
                tokens.begin() + static_cast<std::ptrdiff_t>(receiver_begin),
                tokens.begin() + static_cast<std::ptrdiff_t>(receiver_end));
            // A parenthesized actual is the instance it holds.
            while (receiver.size() >= 3U
                && receiver.front().kind == TokenKind::LeftParen
                && receiver.back().kind == TokenKind::RightParen) {
                receiver.erase(receiver.begin());
                receiver.pop_back();
            }
            if (receiver.empty() || receiver.front().kind != TokenKind::Identifier
                || (receiver_begin > 0U
                    && (tokens[receiver_begin - 1U].kind == TokenKind::Dot
                        || tokens[receiver_begin - 1U].kind == TokenKind::Scope))) {
                // A hierarchical receiver, or the method of an event.
                if (receiver_begin > 0U
                    && tokens[receiver_begin - 1U].kind == TokenKind::Dot
                    && sequence_declaration(receiver.front().text)) {
                    return "a sequence end point read through a hierarchical "
                           "name";
                }
                continue;
            }
            const auto* const declared
                = sequence_declaration(receiver.front().text);
            if (declared == nullptr) {
                continue;
            }
            auto method_end = index + 1U;
            if (method_end + 1U < tokens.size()
                && tokens[method_end].kind == TokenKind::LeftParen
                && tokens[method_end + 1U].kind == TokenKind::RightParen) {
                method_end += 2U;
            }
            std::string wait_bit;
            std::string failure;
            auto replacement = context_.endpoint(*declared, receiver,
                tokens[index].text == "matched", wait_bit, failure);
            if (!replacement) {
                return failure;
            }
            if (!wait_bit.empty()
                && std::ranges::find(context_.endpoint_waits, wait_bit)
                    == context_.endpoint_waits.end()) {
                context_.endpoint_waits.push_back(std::move(wait_bit));
            }
            tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(receiver_begin),
                tokens.begin() + static_cast<std::ptrdiff_t>(method_end));
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(receiver_begin),
                replacement->begin(), replacement->end());
            index = receiver_begin + replacement->size();
        }
        return std::nullopt;
    }

    [[nodiscard]] const SystemVerilogAssertionDeclaration* sequence_declaration(
        const std::string_view name) const
    {
        const auto* const found = visible_declaration(context_.unit,
            context_.scopes, name, context_.origin, std::nullopt);
        return found != nullptr
                && found->kind == SystemVerilogAssertionDeclarationKind::Sequence
            ? found
            : nullptr;
    }

    std::optional<std::size_t> nested(const std::size_t open,
        const std::size_t close)
    {
        const auto inside = slice(open + 1U, close);
        auto parts = split_top_level(inside, TokenKind::Comma);
        if (parts.size() > 1U) {
            // `(s, item, ...)`: the items run at the end of each match of s
            // (16.10, 16.11); the published tree keeps them as the match
            // items of s (37.54).
            if (!context_.keep_instances) {
                context_.uses_locals = true;
            }
            TreeParser inner { nodes_, parts.front(), context_, path_ };
            auto operand = inner.parse();
            position_ = close + 1U;
            if (!operand) {
                return operand;
            }
            if (!is_sequence(*operand)) {
                return fail("match items follow a property, not a sequence");
            }
            Node node;
            node.kind = NodeKind::match_items;
            node.operands = { *operand };
            node.actual_tokens.assign(std::make_move_iterator(parts.begin() + 1),
                std::make_move_iterator(parts.end()));
            return add(std::move(node));
        }
        TreeParser inner { nodes_, inside, context_, path_ };
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
            // Only first_match takes match items (16.9.8).
            if (node.kind != NodeKind::first_match
                && split_top_level(slice(position_ + 1U, *close), TokenKind::Comma)
                        .size()
                    > 1U) {
                return fail("strong and weak take one sequence");
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
            // A clocking event applies to the rest of its operand. Another
            // clock than the one in effect makes a multiclocked sequence
            // (16.13.1) or property (16.13.2).
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
            const auto clock = clock_index(event);
            if (context_.keep_instances && delay_operand_) {
                // After `##1` or `##0` the clocking event governs the
                // longest sequence that follows (16.13.1), the same operand
                // the assertion engine takes below; its clock flows on to
                // the right (16.13.3), so a later operator's operand is not
                // part of this clocked sequence.
                delay_operand_ = false;
                const auto enclosing
                    = std::exchange(context_.current_clock, clock);
                auto operand = disjunction();
                delay_operand_ = true;
                if (clock == enclosing || !operand) {
                    return operand;
                }
                Node node;
                node.kind = NodeKind::clocked;
                node.clock = clock;
                node.operands = { *operand };
                return add(std::move(node));
            }
            if (context_.keep_instances) {
                // The published tree keeps every clocking event (37.52,
                // 37.56): a sequence on another clock than the one in
                // effect is a component of a multiclocked sequence, any
                // other operand is a clocked property.
                const auto enclosing
                    = std::exchange(context_.current_clock, clock);
                auto operand = implication();
                context_.current_clock = enclosing;
                if (!operand) {
                    return operand;
                }
                Node node;
                node.kind = is_sequence(*operand) && clock != enclosing
                    ? NodeKind::clocked
                    : NodeKind::clocked_property;
                node.clock = clock;
                node.operands = { *operand };
                return add(std::move(node));
            }
            if (std::exchange(delay_operand_, false)) {
                // After `##1` or `##0` the clocking event governs the
                // longest sequence that follows, and its clock flows on
                // to the right across the linear operators after it (an
                // implication or followed-by, 16.13.3) up to the
                // enclosing parentheses.
                const auto enclosing
                    = std::exchange(context_.current_clock, clock);
                auto operand = disjunction();
                delay_operand_ = true;
                if (clock == enclosing || !operand) {
                    return operand;
                }
                return clocked(clock, operand);
            }
            if (clock == context_.current_clock) {
                return implication();
            }
            const auto enclosing
                = std::exchange(context_.current_clock, clock);
            auto operand = implication();
            context_.current_clock = enclosing;
            return clocked(clock, operand);
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
        if (context_.keep_instances) {
            Node node;
            node.kind = NodeKind::instance;
            node.instance = declaration.name;
            node.declared = &declaration;
            node.sequence_instance = declaration.kind
                == SystemVerilogAssertionDeclarationKind::Sequence;
            ++position_;
            if (at(TokenKind::LeftParen)) {
                const auto close = matching(position_);
                if (!close) {
                    return fail("an instance argument list is unbalanced");
                }
                const auto inside = slice(position_ + 1U, *close);
                if (!inside.empty()) {
                    node.actual_tokens = split_top_level(inside, TokenKind::Comma);
                }
                position_ = *close + 1U;
            }
            return add(std::move(node));
        }
        // A property whose body reaches itself is recursive (16.12.17); an
        // instance of a declaration being expanded is otherwise one an
        // actual argument wrote in an enclosing context (`s2(s1(a))` where
        // s2's body instances s1). A sequence shall not depend on itself
        // (16.8, reported with the declarations).
        const bool recursive = reaches(declaration, declaration);
        if (recursive
            && declaration.kind == SystemVerilogAssertionDeclarationKind::Sequence) {
            return fail("a sequence that depends on itself (16.8)");
        }
        if (recursive && context_.illegal_recursive != nullptr
            && context_.illegal_recursive->contains({ declaration.name,
                declaration.name_span.begin.line,
                declaration.name_span.begin.offset })) {
            return fail("a recursive property that breaks a restriction of "
                        "16.12.17");
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
        // The same recursive instance as one being expanded, with the same
        // actuals of its formals that are not local variables, evaluates
        // that expansion again (16.12.17, Restriction 4).
        std::string key;
        if (recursive) {
            for (const auto& formal : declaration.formals) {
                if (local_input(formal)) {
                    continue;
                }
                Token reference;
                reference.kind = TokenKind::Identifier;
                reference.text = formal.name;
                reference.span = formal.span;
                const auto value = substituted(declaration, actuals,
                    std::span<const Token> { &reference, 1U });
                if (!value) {
                    return fail("the actual arguments of '" + declaration.name
                        + "' do not match its formals");
                }
                key += resolve_clock(context_.unit, context_.scopes, *value,
                    context_.origin).identity;
                key += " , ";
            }
            for (auto expansion = context_.expansions.rbegin();
                 expansion != context_.expansions.rend(); ++expansion) {
                if (expansion->declaration != &declaration
                    || expansion->key != key) {
                    continue;
                }
                return recursion(declaration, actuals, *expansion);
            }
        }
        // Each instance has its own copy of the declaration's local
        // variables (16.10).
        std::vector<std::vector<Token>> initializers;
        std::optional<std::vector<Token>> body;
        std::map<std::string, std::string, std::less<>> local_names;
        if (has_locals(declaration)) {
            body.emplace();
            if (auto error = declare_locals(context_, declaration, actuals,
                    declaration.expression_tokens, *body, initializers,
                    &local_names)) {
                return fail(std::move(*error));
            }
        } else {
            body = substituted_body(declaration, actuals, name);
        }
        if (!body) {
            return fail("the actual arguments of '" + declaration.name
                + "' do not match its formals");
        }
        // The declaration's names resolve in its own scope (16.8); an
        // actual's in the instance's, which this does not tell apart.
        const auto origin = std::exchange(context_.origin,
            declaration_scope(context_, declaration));
        auto clock = context_.current_clock;
        if (declaration.clock) {
            const auto event = substituted(declaration, actuals,
                declaration.clock->event_tokens);
            if (!event) {
                context_.origin = origin;
                return fail("the instance clock does not substitute");
            }
            if (!inferred_clock_only(*event)) {
                clock = clock_index(*event);
            }
        }
        const auto enclosing = std::exchange(context_.current_clock, clock);
        // The node the recursive instances of this expansion return to.
        std::optional<std::size_t> target;
        if (recursive) {
            Node node;
            node.kind = NodeKind::recursion_target;
            target = add(std::move(node));
            context_.expansions.push_back({ &declaration, key, *target, clock,
                std::move(local_names) });
        }
        TreeParser inner { nodes_,
            inferred(std::move(*body), context_.clocks[clock],
                context_.inferred_disable),
            context_, expanded_path(declaration) };
        auto operand = inner.parse();
        if (target) {
            context_.expansions.pop_back();
            if (operand) {
                nodes_[*target].operands = { *operand };
                operand = target;
            }
        }
        context_.current_clock = enclosing;
        context_.origin = origin;
        if (operand && !initializers.empty()) {
            Node wrapper;
            wrapper.kind = is_sequence(*operand) ? NodeKind::match_items
                                                 : NodeKind::initialized;
            wrapper.leading = true;
            wrapper.operands = { *operand };
            wrapper.actual_tokens = std::move(initializers);
            operand = add(std::move(wrapper));
        }
        if (clock == enclosing) {
            return operand;
        }
        return clocked(clock, operand);
    }

    // A recursive instance that evaluates `expansion` again (16.12.17): its
    // local formal arguments take the values of its actuals, all evaluated
    // before any is assigned (16.8.2), then its local variables their
    // initial values (16.10).
    std::optional<std::size_t> recursion(
        const SystemVerilogAssertionDeclaration& declaration,
        const std::vector<std::vector<Token>>& actuals,
        const ParseContext::Expansion& expansion)
    {
        if (context_.current_clock != expansion.clock) {
            return fail("a recursive property instance on another clock than "
                        "the instance it repeats");
        }
        Node node;
        node.kind = NodeKind::recursion;
        node.target = expansion.target;
        const auto& model = tokens_[position_ - 1U];
        const auto assignment = [&](const std::string& target,
                                    const std::span<const Token> value) {
            std::vector<Token> item;
            item.push_back(punctuation(model, TokenKind::Identifier, target));
            item.push_back(punctuation(model, TokenKind::Assign, "="));
            item.push_back(punctuation(model, TokenKind::LeftParen, "("));
            item.insert(item.end(), value.begin(), value.end());
            item.push_back(punctuation(model, TokenKind::RightParen, ")"));
            return item;
        };
        std::vector<std::vector<Token>> stores;
        for (const auto& formal : declaration.formals) {
            if (!local_input(formal)) {
                continue;
            }
            const auto unique = expansion.locals.find(formal.name);
            if (unique == expansion.locals.end()) {
                return fail("a recursive instance's local formal argument");
            }
            Token reference;
            reference.kind = TokenKind::Identifier;
            reference.text = formal.name;
            reference.span = formal.span;
            const auto value = substituted(declaration, actuals,
                std::span<const Token> { &reference, 1U });
            if (!value) {
                return fail("the actual arguments of '" + declaration.name
                    + "' do not match its formals");
            }
            std::vector<Token> type = formal.type_tokens;
            if (!type.empty() && type.front().text == "input") {
                type.erase(type.begin());
            }
            const auto temporary
                = "__fsim_lv" + std::to_string(context_.locals.size());
            context_.locals.push_back({ temporary, std::move(type) });
            context_.local_names.insert(temporary);
            context_.uses_locals = true;
            node.actual_tokens.push_back(assignment(temporary, *value));
            const auto read = punctuation(model, TokenKind::Identifier, temporary);
            stores.push_back(assignment(unique->second,
                std::span<const Token> { &read, 1U }));
        }
        node.actual_tokens.insert(node.actual_tokens.end(),
            std::make_move_iterator(stores.begin()),
            std::make_move_iterator(stores.end()));
        for (const auto& local : declaration.local_variables) {
            const auto unique = expansion.locals.find(local.name);
            if (local.initializer_tokens.empty()
                || unique == expansion.locals.end()) {
                continue;
            }
            const auto value = substituted(declaration, actuals,
                renamed(local.initializer_tokens, expansion.locals));
            if (!value) {
                return fail("a local variable initializer does not substitute");
            }
            node.actual_tokens.push_back(assignment(unique->second, *value));
        }
        return add(std::move(node));
    }

    // The index of a clocking event among the property's clocks: an event
    // identical to one of them (16.16.1: the same edges of the same objects
    // under the same iff conditions, ResolvedClock) is that clock.
    std::uint32_t clock_index(const std::vector<Token>& event)
    {
        auto resolved = resolve_clock(context_.unit, context_.scopes, event,
            context_.origin);
        for (std::size_t index = 0; index < context_.clock_identities.size();
             ++index) {
            if (context_.clock_identities[index] == resolved.identity) {
                return static_cast<std::uint32_t>(index);
            }
        }
        context_.clocks.push_back(std::move(resolved.tokens));
        context_.clock_identities.push_back(std::move(resolved.identity));
        return static_cast<std::uint32_t>(context_.clocks.size() - 1U);
    }

    // `@(c) s` or `@(c) p`: a sequence on clock c is part of a multiclocked
    // sequence (16.13.1), a property on it a clocked property (16.13.2).
    std::optional<std::size_t> clocked(const std::uint32_t clock,
        const std::optional<std::size_t> operand)
    {
        if (!operand) {
            return operand;
        }
        Node node;
        node.kind = is_sequence(*operand) ? NodeKind::clocked
                                          : NodeKind::clocked_property;
        node.clock = clock;
        node.operands = { *operand };
        return add(std::move(node));
    }

public:
    // Whether `declaration` has local variables (16.10): assertion variable
    // declarations or local input formal arguments (16.8.2).
    static bool has_locals(const SystemVerilogAssertionDeclaration& declaration)
    {
        return !declaration.local_variables.empty()
            || std::ranges::any_of(declaration.formals, local_input);
    }

    // The data type of a local formal argument without its direction, and
    // whether the formal is a local input: `local [input] type name`
    // (16.8.2). A local inout or output formal binds the local variable
    // given as its actual (16.8.2: the actual shall be a local variable),
    // which substitution expresses directly.
    static bool local_input(const SystemVerilogAssertionFormal& formal)
    {
        if (!formal.local) {
            return false;
        }
        return formal.type_tokens.empty()
            || (formal.type_tokens.front().text != "inout"
                && formal.type_tokens.front().text != "output");
    }

    // Gives the local variables of `declaration` (16.10) their unique names
    // in the property and records them: `body` is `raw` renamed, then with
    // the formals substituted, and each initialization assignment becomes
    // `name = (expression)` in `initializers`: the local input formals'
    // first, then the declared variables' in declaration order (16.10). The
    // error, if any.
    static std::optional<std::string> declare_locals(ParseContext& context,
        const SystemVerilogAssertionDeclaration& declaration,
        const std::vector<std::vector<Token>>& actuals,
        const std::span<const Token> raw, std::vector<Token>& body,
        std::vector<std::vector<Token>>& initializers,
        std::map<std::string, std::string, std::less<>>* const unique_names
        = nullptr)
    {
        context.uses_locals = true;
        std::map<std::string, std::string, std::less<>> names;
        const auto declare = [&](const std::string& name,
                                 std::vector<Token> type,
                                 std::optional<std::vector<Token>> value,
                                 const Token& model) {
            const auto unique = "__fsim_lv" + std::to_string(context.locals.size());
            if (value) {
                std::vector<Token> item;
                Token target = model;
                target.kind = TokenKind::Identifier;
                target.text = unique;
                item.push_back(std::move(target));
                item.push_back(punctuation(model, TokenKind::Assign, "="));
                item.push_back(punctuation(model, TokenKind::LeftParen, "("));
                item.insert(item.end(), value->begin(), value->end());
                item.push_back(punctuation(model, TokenKind::RightParen, ")"));
                initializers.push_back(std::move(item));
            }
            names[name] = unique;
            context.locals.push_back({ unique, std::move(type) });
            context.local_names.insert(unique);
        };
        // A local input formal is initialized with its actual's value when
        // the instance begins (16.8.2).
        for (const auto& formal : declaration.formals) {
            if (!local_input(formal)) {
                continue;
            }
            std::vector<Token> type = formal.type_tokens;
            if (!type.empty() && type.front().text == "input") {
                type.erase(type.begin());
            }
            if (type.empty()) {
                return "the local formal argument '" + formal.name
                    + "' has no data type";
            }
            Token reference;
            reference.kind = TokenKind::Identifier;
            reference.text = formal.name;
            reference.span = formal.span;
            auto value = substituted(declaration, actuals,
                std::span<const Token> { &reference, 1U });
            if (!value) {
                return std::string { "a local formal argument has no actual" };
            }
            declare(formal.name, std::move(type), std::move(*value), reference);
        }
        for (const auto& local : declaration.local_variables) {
            const auto& declarator = local.declarator_tokens;
            const bool plain = !declarator.empty()
                && declarator.front().kind == TokenKind::Identifier
                && declarator.front().text == local.name
                && (local.initializer_tokens.empty()
                        ? declarator.size() == 1U
                        : declarator.size() >= 3U
                            && declarator[1U].kind == TokenKind::Assign);
            if (!plain || local.type_tokens.empty()) {
                return "the local variable '" + local.name
                    + "' has unpacked dimensions or no data type";
            }
            auto type = substituted(declaration, actuals, local.type_tokens);
            if (!type) {
                return std::string { "a local variable type does not substitute" };
            }
            std::optional<std::vector<Token>> value;
            if (!local.initializer_tokens.empty()) {
                // The expression may read the locals declared before it
                // (16.10).
                value = substituted(declaration, actuals,
                    renamed(local.initializer_tokens, names));
                if (!value) {
                    return std::string { "a local variable initializer does not substitute" };
                }
            }
            declare(local.name, std::move(*type), std::move(value),
                declarator.front());
        }
        auto substituted_raw = substituted(declaration, actuals,
            renamed({ raw.begin(), raw.end() }, names));
        if (!substituted_raw) {
            return std::string { "the instance actuals do not match the formals" };
        }
        body = std::move(*substituted_raw);
        if (unique_names != nullptr) {
            *unique_names = std::move(names);
        }
        return std::nullopt;
    }

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

    // Whether a substituted clocking event is only `$inferred_clock`: the
    // declaration then has the clock of the instance's context (16.14.7),
    // not a clock of its own.
    [[nodiscard]] static bool inferred_clock_only(
        const std::span<const Token> event)
    {
        std::size_t begin { };
        auto end = event.size();
        while (end >= begin + 3U && event[begin].kind == TokenKind::LeftParen
            && event[end - 1U].kind == TokenKind::RightParen) {
            ++begin;
            --end;
        }
        return end == begin + 1U
            && event[begin].kind == TokenKind::Identifier
            && event[begin].text == "$inferred_clock";
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
    // The declarations being expanded on the path to this parser.
    std::vector<const SystemVerilogAssertionDeclaration*> path_;
    std::size_t position_ { };
    // Parsing the right operand of a cycle delay (delay_operand).
    bool delay_operand_ { };

    // The clocks the sequences and booleans under `node` are evaluated on,
    // `incoming` the clock in effect at it (16.13.3).
    void collect_clocks(const std::size_t node, const std::uint32_t incoming,
        std::set<std::uint32_t>& clocks) const
    {
        const auto& item = nodes_[node];
        const bool clocking = item.kind == NodeKind::clocked
            || item.kind == NodeKind::clocked_property;
        const auto inner = clocking ? item.clock : incoming;
        if (!clocking && (!item.expression.empty() || item.operands.empty()
                || (item.kind == NodeKind::delay && item.leading))) {
            clocks.insert(incoming);
        }
        for (const auto operand : item.operands) {
            collect_clocks(operand, inner, clocks);
        }
    }

    // Whether two sequence operands share one clock: `and` and `or` of
    // differently clocked or multiclocked sequences are property operators
    // (16.13.1, 16.13.2). The published tree keeps the operators as
    // written.
    [[nodiscard]] bool singly_clocked(const std::size_t left,
        const std::size_t right, const std::uint32_t incoming) const
    {
        if (context_.keep_instances) {
            return true;
        }
        std::set<std::uint32_t> clocks;
        collect_clocks(left, incoming, clocks);
        collect_clocks(right, incoming, clocks);
        return clocks.size() <= 1U;
    }

    // Whether the body or formal defaults of `from`, through the bodies of
    // the declarations they instance, instance `target`.
    [[nodiscard]] bool reaches(const SystemVerilogAssertionDeclaration& from,
        const SystemVerilogAssertionDeclaration& target) const
    {
        std::vector<const SystemVerilogAssertionDeclaration*> pending { &from };
        std::set<const SystemVerilogAssertionDeclaration*> visited;
        // A declaration's names resolve in its own scope (16.8).
        const auto instanced = [&](const std::span<const Token> tokens,
                                   const std::size_t origin) {
            for (std::size_t index = 0; index < tokens.size(); ++index) {
                const auto& token = tokens[index];
                if (token.kind != TokenKind::Identifier
                    || (index > 0U
                        && (tokens[index - 1U].kind == TokenKind::Dot
                            || tokens[index - 1U].kind == TokenKind::Scope))
                    || (index + 1U < tokens.size()
                        && tokens[index + 1U].kind == TokenKind::Dot)) {
                    continue;
                }
                const auto* const found = visible_declaration(context_.unit,
                    context_.scopes, token.text, origin, std::nullopt);
                if (found == nullptr) {
                    continue;
                }
                if (found == &target) {
                    return true;
                }
                if (visited.insert(found).second) {
                    pending.push_back(found);
                }
            }
            return false;
        };
        while (!pending.empty()) {
            const auto* const current = pending.back();
            pending.pop_back();
            const auto origin = declaration_scope(context_, *current);
            if (instanced(current->expression_tokens, origin)) {
                return true;
            }
            for (const auto& formal : current->formals) {
                if (instanced(formal.default_tokens, origin)) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::vector<const SystemVerilogAssertionDeclaration*>
    expanded_path(const SystemVerilogAssertionDeclaration& declaration) const
    {
        auto path = path_;
        path.push_back(&declaration);
        return path;
    }
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
    // The expressions that read local variables (Plan::expressions), as
    // written: a boolean (target 0) or a match item statement whose target
    // is local target - 1 (16.10).
    std::vector<std::pair<std::vector<Token>, std::uint32_t>> expressions;
    // The subroutine calls attached to sequences (Plan::calls), as written
    // (16.11).
    std::vector<std::vector<Token>> calls;
    // Why a multiclocked sequence could not be compiled.
    std::string failure { };
    // Whether that reason is a form the standard makes illegal, not one
    // the plan does not cover.
    bool illegal { };

    // The property's local variables, by their unique names, in order.
    void use_locals(const std::vector<std::string>& names)
    {
        for (const auto& name : names) {
            local_index_.emplace(name,
                static_cast<std::uint32_t>(local_index_.size()));
            local_names_.insert(name);
        }
        plan.locals.resize(names.size());
    }

    // `clock` is the leading clock of the directive (the clock in effect at
    // the root).
    void compile(const std::size_t root, const bool every_match,
        const std::uint32_t clock = 0U)
    {
        plan.root = property(root, clock);
        plan.every_match = every_match;
        plan.guards = static_cast<std::uint32_t>(guards.size());
        plan.bounds = static_cast<std::uint32_t>(bounds.size());
    }

    // The set of semantic leading clocks of property `index` with clock
    // `incoming` in effect (16.16.1); an inherited leading clock is the
    // incoming one.
    void leading_clocks(const std::size_t index, const std::uint32_t incoming,
        std::set<std::uint32_t>& clocks) const
    {
        const auto& node = nodes_[index];
        if (sequence_kind(node.kind) || node.kind == NodeKind::strong
            || node.kind == NodeKind::weak) {
            clocks.insert(leading(index, incoming));
            return;
        }
        switch (node.kind) {
        case NodeKind::clocked_property:
            leading_clocks(node.operands[0], node.clock, clocks);
            break;
        case NodeKind::negation:
        case NodeKind::conjunction:
        case NodeKind::disjunction:
        case NodeKind::equivalence:
        case NodeKind::implies:
            for (const auto operand : node.operands) {
                leading_clocks(operand, incoming, clocks);
            }
            break;
        case NodeKind::implication:
        case NodeKind::followed_by:
            clocks.insert(leading(node.operands[0], incoming));
            break;
        case NodeKind::initialized:
        case NodeKind::recursion_target:
            leading_clocks(node.operands[0], incoming, clocks);
            break;
        case NodeKind::accept:
        case NodeKind::reject:
            if (node.synchronous) {
                clocks.insert(incoming);
            } else {
                leading_clocks(node.operands[0], incoming, clocks);
            }
            break;
        default:
            clocks.insert(incoming);
            break;
        }
    }

    // The guard of each clock's tick in a multiclocked plan.
    void clock_ticks(const std::span<const std::vector<Token>> ticks)
    {
        for (const auto& tick : ticks) {
            plan.clock_guards.push_back(guard(tick));
        }
        plan.guards = static_cast<std::uint32_t>(guards.size());
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
        // The guard's source span (plan text sva3): the position of its
        // first token, which a step callback reports as the matched
        // expression (IEEE 1800-2023 39.4.2); 0 for a synthetic guard.
        plan::GuardSpan span;
        if (!tokens.empty()) {
            span.line = static_cast<std::uint32_t>(tokens.front().span.begin.line);
            span.column
                = static_cast<std::uint32_t>(tokens.front().span.begin.column);
        }
        plan.guard_spans.push_back(span);
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

    [[nodiscard]] bool reads_locals(const std::span<const Token> tokens) const
    {
        return !local_names_.empty() && mentions_local(tokens, local_names_);
    }

    std::uint32_t expression(std::vector<Token> tokens, const std::uint32_t target)
    {
        auto key = std::to_string(target) + ":" + spelling(tokens);
        if (const auto found = expression_index_.find(key);
            found != expression_index_.end()) {
            return found->second;
        }
        expressions.emplace_back(std::move(tokens), target);
        plan.expressions.push_back({ target });
        const auto index = static_cast<std::uint32_t>(plan.expressions.size() - 1U);
        expression_index_.emplace(std::move(key), index);
        return index;
    }

    static std::vector<Token> negation(const std::vector<Token>& tokens)
    {
        std::vector<Token> negated;
        const auto& model = tokens.front();
        negated.push_back(punctuation(model, TokenKind::Bang, "!"));
        negated.push_back(punctuation(model, TokenKind::LeftParen, "("));
        negated.insert(negated.end(), tokens.begin(), tokens.end());
        negated.push_back(punctuation(model, TokenKind::RightParen, ")"));
        return negated;
    }

    // The check of boolean `tokens`: a guard, or an expression when it
    // reads local variables.
    plan::Instruction check(const std::vector<Token>& tokens)
    {
        return positioned(reads_locals(tokens)
                ? instruction(plan::Op::check_expression, expression(tokens, 0U))
                : instruction(plan::Op::check, guard(tokens)),
            tokens);
    }

    // A sequence match item (16.10): `local = e`, `local op= e` (selects of
    // the local allowed), `local++`, `++local`, `local--` or `--local`; the
    // target may be parenthesized, as when a local is an instance's actual
    // (16.10). The statement with a plain target and the local's index, or
    // nullopt for any other form (a subroutine call, 16.11).
    std::optional<std::pair<std::vector<Token>, std::uint32_t>> match_item(
        const std::vector<Token>& item) const
    {
        const auto is_step = [](const Token& token) {
            return token.kind == TokenKind::PlusPlus
                || token.kind == TokenKind::MinusMinus;
        };
        std::size_t position = 0U;
        const bool prefix = !item.empty() && is_step(item.front());
        position += prefix ? 1U : 0U;
        std::size_t open = 0U;
        while (position < item.size() && item[position].kind == TokenKind::LeftParen) {
            ++open;
            ++position;
        }
        if (position >= item.size() || item[position].kind != TokenKind::Identifier) {
            return std::nullopt;
        }
        const auto found = local_index_.find(item[position].text);
        if (found == local_index_.end()) {
            return std::nullopt;
        }
        std::vector<Token> statement;
        if (prefix) {
            statement.push_back(item.front());
        }
        statement.push_back(item[position]);
        ++position;
        for (; open > 0U; --open, ++position) {
            if (position >= item.size() || item[position].kind != TokenKind::RightParen) {
                return std::nullopt;
            }
        }
        const auto rest = [&] {
            statement.insert(statement.end(),
                item.begin() + static_cast<std::ptrdiff_t>(position), item.end());
            return std::make_pair(std::move(statement), found->second);
        };
        if (prefix) {
            return position == item.size() ? std::optional { rest() } : std::nullopt;
        }
        if (position + 1U == item.size() && is_step(item[position])) {
            return rest();
        }
        const auto selects = position;
        while (position < item.size() && item[position].kind == TokenKind::LeftBracket) {
            int depth = 0;
            for (; position < item.size(); ++position) {
                depth += item[position].kind == TokenKind::LeftBracket ? 1
                    : item[position].kind == TokenKind::RightBracket   ? -1
                                                                        : 0;
                if (depth == 0) {
                    ++position;
                    break;
                }
            }
        }
        if (position + 1U < item.size()
            && is_blocking_assignment_operator(item[position].kind)) {
            position = selects;
            return rest();
        }
        return std::nullopt;
    }

    // Whether match item `item` is a subroutine call (16.11): a task, void
    // function or system task name, possibly hierarchical or a method, with
    // or without an argument list, and no assignment.
    [[nodiscard]] static bool subroutine_call(const std::vector<Token>& item)
    {
        if (item.empty() || item.front().kind != TokenKind::Identifier) {
            return false;
        }
        std::size_t position = 1U;
        while (position + 1U < item.size() && item[position].kind == TokenKind::Dot
            && item[position + 1U].kind == TokenKind::Identifier) {
            position += 2U;
        }
        if (position == item.size()) {
            return true;
        }
        if (item[position].kind != TokenKind::LeftParen
            || item.back().kind != TokenKind::RightParen) {
            return false;
        }
        int depth = 0;
        for (auto at = position; at < item.size(); ++at) {
            depth += item[at].kind == TokenKind::LeftParen ? 1
                : item[at].kind == TokenKind::RightParen   ? -1
                                                           : 0;
            if (depth == 0 && at + 1U != item.size()) {
                return false;
            }
            if (depth == 1 && (is_blocking_assignment_operator(item[at].kind)
                                  || item[at].kind == TokenKind::PlusPlus
                                  || item[at].kind == TokenKind::MinusMinus)) {
                return false;
            }
        }
        return true;
    }

    // Whether sequence `index` admits an empty match (16.12.22).
    [[nodiscard]] bool admits_empty(const std::size_t index) const
    {
        const auto& node = nodes_[index];
        const auto zero = [](const BoundSpec& bound) {
            return bound.kind == BoundSpec::Kind::literal && bound.value == 0;
        };
        switch (node.kind) {
        case NodeKind::repeat:
            return zero(node.lo) || admits_empty(node.operands[0]);
        case NodeKind::goto_repeat:
        case NodeKind::nonconsecutive_repeat:
            return zero(node.lo);
        case NodeKind::delay:
            return false;
        case NodeKind::sequence_or:
            return admits_empty(node.operands[0]) || admits_empty(node.operands[1]);
        case NodeKind::sequence_and:
        case NodeKind::intersect:
            return admits_empty(node.operands[0]) && admits_empty(node.operands[1]);
        case NodeKind::within:
            return admits_empty(node.operands[1]);
        case NodeKind::throughout:
        case NodeKind::first_match:
        case NodeKind::clocked:
        case NodeKind::match_items:
            return admits_empty(node.operands[0]);
        default:
            return false;
        }
    }

    // The local variables sequence `index` assigns (16.10, F.5.4).
    void assigned(const std::size_t index, std::set<std::uint32_t>& locals) const
    {
        const auto& node = nodes_[index];
        if (node.kind == NodeKind::match_items) {
            for (const auto& item : node.actual_tokens) {
                if (const auto target = match_item(item)) {
                    locals.insert(target->second);
                }
            }
        }
        for (const auto operand : node.operands) {
            assigned(operand, locals);
        }
    }

    // The local set `locals` names in a `join_end` (0 none).
    std::uint32_t local_set(const std::set<std::uint32_t>& locals)
    {
        if (locals.empty()) {
            return 0U;
        }
        std::vector<std::uint32_t> set(locals.begin(), locals.end());
        const auto found = std::ranges::find(plan.local_sets, set);
        if (found != plan.local_sets.end()) {
            return static_cast<std::uint32_t>(found - plan.local_sets.begin()) + 1U;
        }
        plan.local_sets.push_back(std::move(set));
        return static_cast<std::uint32_t>(plan.local_sets.size());
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

    // An instruction that evaluates the boolean written as `tokens`: the
    // position of its first token is what a failure of it reports.
    static plan::Instruction positioned(
        plan::Instruction result, const std::vector<Token>& tokens)
    {
        if (!tokens.empty()) {
            result.line = static_cast<std::uint32_t>(tokens.front().span.begin.line);
            result.column
                = static_cast<std::uint32_t>(tokens.front().span.begin.column);
        }
        return result;
    }

    // The position of the leftmost boolean written under property node
    // `index`.
    std::pair<std::uint32_t, std::uint32_t> first_position(
        const std::size_t index, const std::size_t depth = 0U) const
    {
        const auto& node = nodes_[index];
        if (!node.expression.empty()) {
            const auto& at = node.expression.front().span.begin;
            return { static_cast<std::uint32_t>(at.line),
                static_cast<std::uint32_t>(at.column) };
        }
        for (const auto operand : node.operands) {
            const auto found = first_position(operand, depth + 1U);
            if (found.first != 0U) {
                return found;
            }
        }
        return { 0U, 0U };
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

    // The semantic leading clock of the sequence `index` with clock
    // `incoming` in effect (16.16.1): the clock of its first boolean.
    [[nodiscard]] std::uint32_t leading(const std::size_t index,
        const std::uint32_t incoming) const
    {
        const auto& node = nodes_[index];
        switch (node.kind) {
        case NodeKind::clocked:
        case NodeKind::clocked_property:
            return leading(node.operands[0], node.clock);
        case NodeKind::delay:
            // A leading `##n s` starts with a tick of the clock in effect.
            return node.leading ? incoming : leading(node.operands[0], incoming);
        case NodeKind::repeat:
        case NodeKind::sequence_or:
        case NodeKind::sequence_and:
        case NodeKind::intersect:
        case NodeKind::within:
        case NodeKind::first_match:
        case NodeKind::strong:
        case NodeKind::weak:
        case NodeKind::match_items:
        case NodeKind::initialized:
        case NodeKind::recursion_target:
            return leading(node.operands[0], incoming);
        default:
            return incoming;
        }
    }

    // Whether `index` holds initialization assignments that run at a tick
    // of the instance's only semantic leading clock, the clock in effect
    // where its evaluation begins (16.13.7): they run before its operand,
    // as for a singly clocked one, and do not nest it.
    [[nodiscard]] bool leading_initializers(const std::size_t index,
        const std::uint32_t incoming) const
    {
        const auto& node = nodes_[index];
        if ((node.kind != NodeKind::match_items || !node.leading)
            && node.kind != NodeKind::initialized) {
            return false;
        }
        std::set<std::uint32_t> clocks;
        leading_clocks(node.operands[0], incoming, clocks);
        return clocks.size() == 1U && *clocks.begin() == incoming;
    }

    // `incoming` is the clock in effect at node `index` (16.13.3).
    void emit(const std::size_t index, plan::Sequence& sequence,
        const std::uint32_t incoming)
    {
        const auto& node = nodes_[index];
        auto& code = sequence.code;
        const auto here = [&] { return static_cast<std::uint32_t>(code.size()); };
        // A clock may change only where a singly clocked operand of a
        // concatenation ends (16.13.1), not inside another operator.
        struct Nested {
            std::size_t& depth;
            bool active;
            Nested(std::size_t& value, const bool region)
                : depth(value)
                , active(region)
            {
                depth += active ? 1U : 0U;
            }
            ~Nested() { depth -= active ? 1U : 0U; }
            Nested(const Nested&) = delete;
            Nested& operator=(const Nested&) = delete;
        } nested { nested_,
            node.kind != NodeKind::boolean && node.kind != NodeKind::delay
                && node.kind != NodeKind::clocked
                && !leading_initializers(index, incoming) };
        // A boolean is checked on the clock in effect at it; only a cycle
        // delay may change the clock of the thread (16.13.1).
        const auto on_clock = [&] {
            if (incoming != clock_) {
                fail("a clocking event that does not follow ##0 or ##1");
            }
        };
        switch (node.kind) {
        case NodeKind::boolean:
            on_clock();
            code.push_back(check(node.expression));
            break;
        case NodeKind::match_items: {
            // The items run in order at the end of each match of the
            // operand (16.10); a leading node's initialization assignments
            // run at the first tick of the instance's evaluation.
            std::vector<plan::Instruction> items;
            for (const auto& item : node.actual_tokens) {
                auto parsed = match_item(item);
                if (!parsed && !node.leading && subroutine_call(item)) {
                    // A subroutine call runs at each end point (16.11).
                    calls.push_back(item);
                    plan.calls = static_cast<std::uint32_t>(calls.size());
                    items.push_back(positioned(instruction(plan::Op::call,
                                                   plan.calls - 1U),
                        item));
                    continue;
                }
                if (!parsed) {
                    fail(node.leading
                            ? "a local variable initialization assignment"
                            : "a sequence match item that is neither a local "
                              "variable assignment, increment or decrement nor a "
                              "subroutine call (16.10, 16.11)");
                    return;
                }
                items.push_back(positioned(instruction(plan::Op::assign,
                                               parsed->second,
                                               expression(std::move(parsed->first),
                                                   parsed->second + 1U),
                                               node.leading ? 1U : 0U),
                    item));
            }
            if (node.leading) {
                // 16.13.7: with several semantic leading clocks, each one's
                // copy of the locals is initialized at its earliest tick.
                if (!leading_initializers(index, incoming)) {
                    fail("local variable initialization assignments of an "
                         "instance with a leading clock other than the clock "
                         "in effect at its start (16.13.7)");
                    return;
                }
                code.insert(code.end(), items.begin(), items.end());
                emit(node.operands[0], sequence, incoming);
                break;
            }
            if (admits_empty(node.operands[0])) {
                fail_illegal("a sequence match item attached to a "
                             "subsequence that admits an empty match (16.10)");
                return;
            }
            emit(node.operands[0], sequence, incoming);
            code.insert(code.end(), items.begin(), items.end());
            break;
        }
        case NodeKind::clocked:
            emit(node.operands[0], sequence, node.clock);
            break;
        case NodeKind::delay: {
            if (!node.leading) {
                emit(node.operands[0], sequence, incoming);
            }
            const auto target = leading(node.operands.back(), incoming);
            if (target != clock_) {
                // `##1 @(c) s` and `##0 @(c) s` hand the sequence over to
                // clock c (16.13.1).
                if (nested_ != 0U || node.lo.kind != BoundSpec::Kind::literal
                    || node.hi.kind != BoundSpec::Kind::literal
                    || node.lo.value != node.hi.value
                    || node.lo.value < 0 || node.lo.value > 1) {
                    fail("a clock change other than ##0 or ##1 between "
                         "singly clocked sequences");
                }
                auto step = instruction(plan::Op::clock, target);
                step.lo.value = node.lo.value;
                step.hi.value = node.lo.value;
                code.push_back(step);
                clock_ = target;
                emit(node.operands.back(), sequence, incoming);
                break;
            }
            auto step = instruction(plan::Op::delay);
            // A leading delay counts from the attempt's first tick.
            const std::int64_t offset = node.leading ? 1 : 0;
            step.lo = bound(node.lo, offset);
            step.hi = bound(node.hi, offset);
            code.push_back(step);
            emit(node.operands.back(), sequence, incoming);
            break;
        }
        case NodeKind::repeat:
            repeat(sequence, bound(node.lo), bound(node.hi),
                [&] { emit(node.operands[0], sequence, incoming); });
            break;
        case NodeKind::goto_repeat:
        case NodeKind::nonconsecutive_repeat: {
            // b[->n] is (!b[*0:$] ##1 b)[*n]; b[=n] is b[->n] ##1 !b[*0:$].
            on_clock();
            const auto& condition = nodes_[node.operands[0]].expression;
            const auto hit = reads_locals(condition)
                ? positioned(instruction(plan::Op::check_expression,
                                 expression(condition, 0U)),
                      condition)
                : positioned(instruction(plan::Op::check, guard(condition)),
                      condition);
            const auto miss = reads_locals(condition)
                ? positioned(instruction(plan::Op::check_expression,
                                 expression(negation(condition), 0U)),
                      condition)
                : positioned(instruction(plan::Op::check, negated_guard(condition)),
                      condition);
            plan::Bound zero;
            plan::Bound forever;
            forever.mode = plan::Bound::Mode::unbounded;
            repeat(sequence, bound(node.lo), bound(node.hi), [&] {
                repeat(sequence, zero, forever, [&] { code.push_back(miss); });
                code.push_back(hit);
            });
            if (node.kind == NodeKind::nonconsecutive_repeat) {
                repeat(sequence, zero, forever, [&] { code.push_back(miss); });
            }
            break;
        }
        case NodeKind::sequence_or: {
            const auto fork = here();
            code.push_back(instruction(plan::Op::fork));
            emit(node.operands[0], sequence, incoming);
            const auto jump = here();
            code.push_back(instruction(plan::Op::jump));
            code[fork].a = here();
            emit(node.operands[1], sequence, incoming);
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
                emit(node.operands[0], sequence, incoming);
                repeat(sequence, zero, forever, [&] {
                    code.push_back(instruction(plan::Op::check, truth));
                });
            } else {
                emit(node.operands[0], sequence, incoming);
            }
            // A local variable assigned in exactly one operand flows out of
            // the composite from that operand; one assigned in both does not
            // flow out (16.10, F.5.4).
            std::set<std::uint32_t> left;
            std::set<std::uint32_t> right;
            if (!local_index_.empty()) {
                assigned(node.operands[0], left);
                assigned(node.operands[1], right);
            }
            std::set<std::uint32_t> only_left;
            std::set<std::uint32_t> only_right;
            std::ranges::set_difference(left, right,
                std::inserter(only_left, only_left.end()));
            std::ranges::set_difference(right, left,
                std::inserter(only_right, only_right.end()));
            const auto left_set = local_set(only_left);
            const auto right_set = local_set(only_right);
            code.push_back(instruction(plan::Op::join_end, 0U, left_set, right_set));
            code[begin].b = here();
            emit(node.operands[1], sequence, incoming);
            code.push_back(instruction(plan::Op::join_end, 1U, right_set, left_set));
            code[begin].c = here();
            break;
        }
        case NodeKind::throughout:
            on_clock();
            code.push_back(positioned(reads_locals(node.expression)
                    ? instruction(plan::Op::throughout_begin,
                          expression(node.expression, 0U), 0U, 1U)
                    : instruction(plan::Op::throughout_begin, guard(node.expression)),
                node.expression));
            emit(node.operands[0], sequence, incoming);
            code.push_back(instruction(plan::Op::throughout_end));
            break;
        case NodeKind::first_match:
            code.push_back(instruction(plan::Op::first_match_begin));
            emit(node.operands[0], sequence, incoming);
            code.push_back(instruction(plan::Op::first_match_end));
            break;
        default:
            break;
        }
    }

    // A sequence starts on its semantic leading clock (16.16.1).
    std::uint32_t sequence(const std::size_t index,
        const std::uint32_t incoming)
    {
        plan::Sequence compiled;
        clock_ = leading(index, incoming);
        emit(index, compiled, incoming);
        compiled.code.push_back(instruction(plan::Op::match));
        plan.sequences.push_back(std::move(compiled));
        return static_cast<std::uint32_t>(plan.sequences.size() - 1U);
    }

    std::uint32_t add(plan::Property property)
    {
        plan.properties.push_back(std::move(property));
        return static_cast<std::uint32_t>(plan.properties.size() - 1U);
    }

    // `incoming` is the clock in effect at node `index`: it flows across the
    // linear operators and distributes to the operands of the branching
    // ones until a clocking event replaces it (16.13.3).
    std::uint32_t property(const std::size_t index,
        const std::uint32_t incoming)
    {
        const auto& node = nodes_[index];
        if (node.kind == NodeKind::clocked_property) {
            return property(node.operands[0], node.clock);
        }
        plan::Property result;
        result.clock = incoming;
        if (sequence_kind(node.kind)) {
            result.kind = plan::PropertyKind::sequence;
            result.clock = leading(index, incoming);
            result.sequence = sequence(index, incoming);
            const auto at = first_position(index);
            result.line = at.first;
            result.column = at.second;
            return add(std::move(result));
        }
        const auto operand = [&](const std::size_t position) {
            return property(node.operands[position], incoming);
        };
        switch (node.kind) {
        case NodeKind::strong:
        case NodeKind::weak:
            result.kind = plan::PropertyKind::sequence;
            result.strong = node.kind == NodeKind::strong;
            result.clock = leading(node.operands[0], incoming);
            result.sequence = sequence(node.operands[0], incoming);
            break;
        case NodeKind::negation:
            restrict_recursion(index);
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
            result.clock = leading(node.operands[0], incoming);
            result.sequence = sequence(node.operands[0], incoming);
            result.children = { operand(1) };
            break;
        case NodeKind::initialized: {
            // The initialization assignments run when the property's
            // evaluation begins (16.10), at a tick of its only semantic
            // leading clock (16.13.7); several leading clocks would each
            // need their own copy of the locals.
            if (!leading_initializers(index, incoming)) {
                fail("local variable initialization assignments of a "
                     "property with a leading clock other than the clock in "
                     "effect at its start (16.13.7)");
                return 0U;
            }
            auto initializers = assignments(node.actual_tokens);
            if (!initializers) {
                return 0U;
            }
            const auto inner = operand(0);
            // The expansion of a recursive property is evaluated again by
            // its recursive instances, which assign their own values.
            if (nodes_[node.operands[0]].kind == NodeKind::recursion_target) {
                return pass_through(inner, std::move(*initializers), incoming);
            }
            auto& target = plan.properties[inner].initializers;
            target.insert(target.begin(), initializers->begin(), initializers->end());
            return inner;
        }
        case NodeKind::recursion_target: {
            // Its recursive instances refer to its index before its operand
            // is compiled (16.12.17).
            const auto reserved = add(plan::Property { });
            targets_[index] = { reserved, incoming };
            const auto inner = operand(0);
            if (inner + 1U == plan.properties.size()) {
                plan.properties[reserved] = std::move(plan.properties[inner]);
                plan.properties.pop_back();
            } else {
                plan.properties[reserved] = plan.properties[inner];
            }
            return reserved;
        }
        case NodeKind::recursion: {
            const auto target = targets_.find(node.target);
            if (target == targets_.end() || target->second.second != incoming) {
                fail("a recursive property instance outside the expansion "
                     "it repeats, or on another clock");
                return 0U;
            }
            if (node.actual_tokens.empty()) {
                return target->second.first;
            }
            auto initializers = assignments(node.actual_tokens);
            if (!initializers) {
                return 0U;
            }
            return pass_through(target->second.first, std::move(*initializers),
                incoming);
        }
        case NodeKind::conditional:
            result.kind = plan::PropertyKind::conditional;
            result.guard_expression = reads_locals(node.expression);
            result.guard = result.guard_expression
                ? expression(node.expression, 0U)
                : guard(node.expression);
            result.children = { operand(0) };
            if (node.operands.size() > 1U) {
                result.children.push_back(operand(1));
            }
            break;
        case NodeKind::nexttime:
            if (node.strong) {
                restrict_recursion(index);
            }
            result.kind = plan::PropertyKind::nexttime;
            result.strong = node.strong;
            result.lo = bound(node.lo);
            result.children = { operand(0) };
            break;
        case NodeKind::always:
        case NodeKind::eventually:
            if (node.strong) {
                restrict_recursion(index);
            }
            result.kind = node.kind == NodeKind::always
                ? plan::PropertyKind::always
                : plan::PropertyKind::eventually;
            result.strong = node.strong;
            result.lo = bound(node.lo);
            result.hi = bound(node.hi);
            result.children = { operand(0) };
            break;
        case NodeKind::until: {
            if (node.strong) {
                restrict_recursion(index);
            }
            result.kind = plan::PropertyKind::until;
            result.strong = node.strong;
            const auto left = operand(0);
            auto right = operand(1);
            if (node.with) {
                // p until_with q is p until (p and q).
                plan::Property both;
                both.kind = plan::PropertyKind::conjunction;
                both.clock = incoming;
                both.children = { operand(0), right };
                right = add(std::move(both));
            }
            result.children = { left, right };
            break;
        }
        case NodeKind::accept:
        case NodeKind::reject:
            // An abort condition shall not reference local variables
            // (16.12.14).
            if (reads_locals(node.expression)) {
                fail_illegal("an abort condition that references a local "
                             "variable (16.12.14)");
            }
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
        const auto at = first_position(index);
        result.line = at.first;
        result.column = at.second;
        return add(std::move(result));
    }

    // `not` and the strong operators shall not apply to a property that
    // instantiates a recursive property (16.12.17 Restriction 1).
    void restrict_recursion(const std::size_t index)
    {
        std::vector<std::size_t> pending { nodes_[index].operands.begin(),
            nodes_[index].operands.end() };
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (nodes_[current].kind == NodeKind::recursion_target) {
                fail_illegal("not or a strong operator applied to a recursive "
                             "property (16.12.17 Restriction 1)");
                return;
            }
            pending.insert(pending.end(), nodes_[current].operands.begin(),
                nodes_[current].operands.end());
        }
    }

    // The local variable assignments `items` as plan expressions.
    std::optional<std::vector<std::uint32_t>> assignments(
        const std::vector<std::vector<Token>>& items)
    {
        std::vector<std::uint32_t> result;
        for (const auto& item : items) {
            auto parsed = match_item(item);
            if (!parsed) {
                fail("a local variable initialization assignment");
                return std::nullopt;
            }
            result.push_back(expression(std::move(parsed->first),
                parsed->second + 1U));
        }
        return result;
    }

    // Property `child` evaluated after the assignments `initializers`, done
    // when the evaluation begins: `if (1) child` (16.12.8).
    std::uint32_t pass_through(const std::uint32_t child,
        std::vector<std::uint32_t> initializers, const std::uint32_t incoming)
    {
        plan::Property result;
        result.kind = plan::PropertyKind::conditional;
        result.clock = incoming;
        result.guard = guard(truth_);
        result.children = { child };
        result.initializers = std::move(initializers);
        result.line = plan.properties[child].line;
        result.column = plan.properties[child].column;
        return add(std::move(result));
    }

    void fail(std::string reason)
    {
        if (failure.empty()) {
            failure = std::move(reason);
        }
    }

    // A form IEEE 1800-2023 makes illegal (an error, not a limitation).
    void fail_illegal(std::string reason)
    {
        if (failure.empty()) {
            failure = std::move(reason);
            illegal = true;
        }
    }

    const std::vector<Node>& nodes_;
    std::vector<Token> truth_;
    // The plan property and clock of each recursion_target node compiled
    // so far (16.12.17).
    std::map<std::size_t, std::pair<std::uint32_t, std::uint32_t>> targets_;
    std::map<std::string, std::uint32_t, std::less<>> local_index_;
    std::set<std::string, std::less<>> local_names_;
    std::map<std::string, std::uint32_t, std::less<>> expression_index_;
    std::map<std::string, std::uint32_t, std::less<>> guard_index_;
    std::map<std::string, std::size_t, std::less<>> bound_index_;
    // The clock in effect while a sequence is emitted, and how many
    // operators other than concatenation enclose the code being emitted.
    std::uint32_t clock_ { };
    std::size_t nested_ { };
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

namespace {

// Whether a statement list, at any depth, holds an assertion statement; the
// spans (source offsets) of the assertion statements are collected too.
void collect_assertion_spans(const std::vector<Statement>& statements,
    std::vector<std::pair<std::uint64_t, std::uint64_t>>& spans)
{
    for (const auto& statement : statements) {
        if (statement.procedural_concurrent_assertion
            || statement.kind == StatementKind::Assert) {
            spans.emplace_back(
                statement.span.begin.offset, statement.span.end.offset);
            continue;
        }
        collect_assertion_spans(statement.statements, spans);
        collect_assertion_spans(statement.else_statements, spans);
        for (const auto& alternative : statement.case_alternatives) {
            collect_assertion_spans(alternative.statements, spans);
        }
    }
}

void collect_automatic_names(const std::vector<Statement>& statements,
    std::vector<std::string>& names)
{
    for (const auto& statement : statements) {
        for (const auto& declaration : statement.declarations) {
            if (declaration.systemverilog_automatic) {
                names.push_back(declaration.name);
            }
        }
        collect_automatic_names(statement.statements, names);
        collect_automatic_names(statement.else_statements, names);
        for (const auto& alternative : statement.case_alternatives) {
            collect_automatic_names(alternative.statements, names);
        }
    }
}

[[nodiscard]] bool clock_keyword(const Token& token)
{
    return token.text == "posedge" || token.text == "negedge"
        || token.text == "edge" || token.text == "iff" || token.text == "or";
}

} // namespace

void VerilogParser::record_procedure_clock_inference(Process& process,
    const std::size_t control_begin, const std::size_t control_end,
    const std::size_t body_begin, const std::size_t body_end)
{
    // IEEE 1800-2023 16.14.6: only a procedure that holds a concurrent
    // assertion statement needs the inference.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> assertions;
    collect_assertion_spans(process.statements, assertions);
    const auto has_concurrent = [](const auto& self,
                                    const std::vector<Statement>& statements)
        -> bool {
        for (const auto& statement : statements) {
            if ((statement.procedural_concurrent_assertion
                    && !statement.procedural_concurrent_assertion
                            ->expect_statement)
                || self(self, statement.statements)
                || self(self, statement.else_statements)) {
                return true;
            }
            for (const auto& alternative : statement.case_alternatives) {
                if (self(self, alternative.statements)) {
                    return true;
                }
            }
        }
        return false;
    };
    const bool concurrent = has_concurrent(has_concurrent, process.statements);
    if (!concurrent || body_end > tokens_.size() || control_end > tokens_.size()) {
        return;
    }
    const auto in_assertion = [&](const Token& token) {
        const auto offset = token.span.begin.offset;
        return std::ranges::any_of(assertions, [&](const auto& span) {
            return span.first <= offset && offset < span.second;
        });
    };
    // a) no blocking timing control, b) exactly one event control.
    std::size_t event_controls = control_end > control_begin ? 1U : 0U;
    std::size_t only_control = body_end;
    for (std::size_t index = body_begin; index < body_end; ++index) {
        const auto& token = tokens_[index];
        if (in_assertion(token)) {
            continue;
        }
        // (The delay of a nonblocking assignment, `q <= #1 d`, does not
        // block the procedure.)
        if (token.kind == TokenKind::Hash
            && !(index > body_begin
                && tokens_[index - 1U].kind == TokenKind::LessEqual)
            && !(index > body_begin && index + 1U < body_end
                && tokens_[index + 1U].kind == TokenKind::LeftParen
                && tokens_[index - 1U].kind == TokenKind::Identifier
                && tokens_[index - 1U].text != "begin"
                && tokens_[index - 1U].text != "else"
                && tokens_[index - 1U].text != "end"
                && tokens_[index - 1U].text != "do"
                && tokens_[index - 1U].text != "fork")) {
            // (`C#(8)` parameterizes a class and is not a delay.)
            process.systemverilog_clock_blocker
                = "the procedure has a delay (a blocking timing control)";
            return;
        }
        if (token.kind == TokenKind::Identifier && token.text == "wait"
            && index + 1U < body_end
            && (tokens_[index + 1U].kind == TokenKind::LeftParen)) {
            process.systemverilog_clock_blocker
                = "the procedure has a wait statement (a blocking timing "
                  "control)";
            return;
        }
        if (token.kind == TokenKind::At) {
            ++event_controls;
            only_control = index + 1U;
        }
    }
    if (event_controls != 1U) {
        process.systemverilog_clock_blocker = event_controls == 0U
            ? "the procedure has no event control"
            : "the procedure has more than one event control";
        return;
    }
    // The tokens of the one event control, after its `@`.
    std::size_t begin = control_begin;
    std::size_t end = control_end;
    if (control_end <= control_begin) {
        begin = only_control;
        end = begin;
        if (end < body_end && tokens_[end].kind == TokenKind::LeftParen) {
            int depth { };
            for (; end < body_end; ++end) {
                if (tokens_[end].kind == TokenKind::LeftParen) {
                    ++depth;
                } else if (tokens_[end].kind == TokenKind::RightParen
                    && --depth == 0) {
                    ++end;
                    break;
                }
            }
        } else {
            while (end < body_end
                && (tokens_[end].kind == TokenKind::Identifier
                    || tokens_[end].kind == TokenKind::Dot
                    || tokens_[end].kind == TokenKind::Star)) {
                ++end;
            }
        }
    }
    std::vector<Token> control(tokens_.begin()
            + static_cast<std::ptrdiff_t>(begin),
        tokens_.begin() + static_cast<std::ptrdiff_t>(end));
    if (control.size() >= 2U && control.front().kind == TokenKind::LeftParen
        && control.back().kind == TokenKind::RightParen) {
        control.erase(control.begin());
        control.pop_back();
    }
    // The event expressions of the control, split at the top-level `or`
    // and comma.
    std::vector<std::vector<Token>> expressions(1U);
    int depth { };
    for (auto& token : control) {
        if (token.kind == TokenKind::LeftParen
            || token.kind == TokenKind::LeftBracket
            || token.kind == TokenKind::LeftBrace) {
            ++depth;
        } else if (token.kind == TokenKind::RightParen
            || token.kind == TokenKind::RightBracket
            || token.kind == TokenKind::RightBrace) {
            --depth;
        }
        if (depth == 0 && (token.kind == TokenKind::Comma
                || (token.kind == TokenKind::Identifier
                    && token.text == "or"))) {
            expressions.emplace_back();
            continue;
        }
        expressions.back().push_back(std::move(token));
    }
    for (auto& expression : expressions) {
        if (expression.empty()) {
            continue;
        }
        SystemVerilogProcedureClockCandidate candidate;
        candidate.tokens = expression;
        std::size_t first = 0U;
        if (expression.front().text == "posedge"
            || expression.front().text == "negedge"
            || expression.front().text == "edge") {
            candidate.edge = true;
            first = 1U;
        } else if (expression.size() == 1U
            && expression.front().kind == TokenKind::Identifier) {
            candidate.name = expression.front().text;
        }
        if (!candidate.edge && candidate.name.empty()) {
            // Neither of the forms of 16.14.6 c) 1): never the clock.
            continue;
        }
        // expression1 ends at the top-level `iff`; its terms are the
        // identifiers in it.
        std::vector<std::string> terms;
        int nesting { };
        for (std::size_t index = first; index < expression.size(); ++index) {
            const auto& token = expression[index];
            if (token.kind == TokenKind::LeftParen) {
                ++nesting;
            } else if (token.kind == TokenKind::RightParen) {
                --nesting;
            } else if (nesting == 0 && token.text == "iff") {
                break;
            }
            if (token.kind == TokenKind::Identifier && !clock_keyword(token)) {
                terms.push_back(token.text);
            }
        }
        for (std::size_t index = body_begin; index < body_end; ++index) {
            // The event control itself, when it is in the body (`initial
            // begin @(posedge clk); ...`), is the clocking event the terms
            // may appear in (16.14.6 c) 2)).
            if (begin <= index && index < end) {
                continue;
            }
            const auto& token = tokens_[index];
            if (token.kind != TokenKind::Identifier || in_assertion(token)
                || std::ranges::find(terms, token.text) == terms.end()) {
                continue;
            }
            // A clocking block signal is `block.signal` (16.14.6 c) 2)).
            if (!candidate.edge && index + 1U < body_end
                && tokens_[index + 1U].kind == TokenKind::Dot) {
                continue;
            }
            candidate.used_in_body = true;
            break;
        }
        process.systemverilog_clock_candidates.push_back(std::move(candidate));
    }
}

void VerilogParser::extract_procedural_assertions(
    std::vector<Process>& processes,
    std::vector<VariableDeclaration>& variables,
    std::vector<SystemVerilogConcurrentAssertion>& directives,
    std::vector<SystemVerilogAssertionDeclaration>& declarations,
    const std::function<bool(std::string_view)>& names_event_or_clocking)
{
    for (auto& process : processes) {
        // The clock inferred for the context of an always or initial
        // procedure (IEEE 1800-2023 16.14.6): exactly one event expression
        // of its one event control is `edge_identifier expression1 [iff
        // expression2]` (not a part of a longer one) or a lone event variable
        // or clocking block, and no term of it appears in the body outside
        // assertion statements. A procedure that gives none takes the default
        // clocking.
        std::vector<Token> clock;
        std::string clock_failure = process.systemverilog_clock_blocker;
        if (clock_failure.empty()) {
            const SystemVerilogProcedureClockCandidate* found = nullptr;
            std::size_t count { };
            for (const auto& candidate : process.systemverilog_clock_candidates) {
                if (candidate.used_in_body
                    || (!candidate.edge
                        && !names_event_or_clocking(candidate.name))) {
                    continue;
                }
                found = &candidate;
                ++count;
            }
            if (count == 1U) {
                clock = found->tokens;
            } else {
                clock_failure = count == 0U
                    ? "no event expression of its event control is an edge "
                      "expression, event variable or clocking block that the "
                      "procedure leaves unused"
                    : "more than one event expression of its event control "
                      "can be the clock";
            }
        }
        std::vector<std::string> automatic_names;
        collect_automatic_names(process.statements, automatic_names);
        for (const auto& variable : process.variables) {
            if (variable.systemverilog_automatic) {
                automatic_names.push_back(variable.name);
            }
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
                    directive.procedural_clock_events
                        = assertion_clock_events(clock);
                    directive.procedural_clock_failure = clock_failure;
                    directive.procedural_automatic_names = automatic_names;
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

namespace {

using TreeNode = SystemVerilogAssertionTreeNode;
using TreeOperand = SystemVerilogAssertionTreeOperand;

// The positions of a sequence operation's operands that are sequences (the
// others are conditions and bounds).
[[nodiscard]] std::vector<std::size_t> sequence_positions(const TreeNode& node)
{
    using Operator = SystemVerilogAssertionOperator;
    switch (node.op) {
    case Operator::CycleDelay:
    case Operator::CompAnd:
    case Operator::CompOr:
    case Operator::Intersect:
    case Operator::Within:
        return { 0U, 1U };
    case Operator::UnaryCycleDelay:
    case Operator::Repeat:
    case Operator::ConsecutiveRepeat:
    case Operator::GotoRepeat:
    case Operator::FirstMatch:
        return { 0U };
    case Operator::Throughout:
        return { 1U };
    default:
        return { };
    }
}

[[nodiscard]] bool sequence_operation(const TreeNode& node)
{
    return node.kind == TreeNode::Kind::Operation && node.sequence;
}

// Whether a clocked sequence is reached from `operand` through sequence
// operations only.
[[nodiscard]] bool holds_clocked_sequence(
    const SystemVerilogAssertionTree& tree, const TreeOperand& operand,
    const unsigned depth = 0U)
{
    if (operand.node < 0) {
        return false;
    }
    const auto& node = tree.nodes[static_cast<std::size_t>(operand.node)];
    if (node.kind == TreeNode::Kind::ClockedSequence) {
        return true;
    }
    if (!sequence_operation(node)) {
        return false;
    }
    for (const auto position : sequence_positions(node)) {
        if (position < node.operands.size()
            && holds_clocked_sequence(tree, node.operands[position], depth + 1U)) {
            return true;
        }
    }
    return false;
}

} // namespace

// A sequence expression that joins sequences on different clocks is a
// multiclock sequence expression (IEEE 1800-2023 37.52, 37.56): the node
// replaces the outermost sequence operation, and its operands are the clocked
// sequences, in order. A part on the clock of the declaration, which the
// declaration's own clocking event gave, is a clocked sequence of that event.
void VerilogParser::wrap_multiclock_sequences(
    SystemVerilogAssertionTree& tree,
    const std::vector<Sensitivity>& default_clock, const SourceSpan& span,
    const bool property_root)
{
    const std::function<void(const TreeOperand&, std::vector<TreeOperand>&,
        unsigned)>
        components = [&](const TreeOperand& operand,
                         std::vector<TreeOperand>& out, const unsigned depth) {
            if (operand.node < 0) {
                return;
            }
            const auto index = static_cast<std::size_t>(operand.node);
            const auto node = tree.nodes[index];
            if (node.kind == TreeNode::Kind::ClockedSequence) {
                out.push_back(operand);
                if (!node.operands.empty()) {
                    components(node.operands.front(), out, depth + 1U);
                }
                return;
            }
            if (!sequence_operation(node)) {
                return;
            }
            for (const auto position : sequence_positions(node)) {
                if (position >= node.operands.size()) {
                    continue;
                }
                const auto& part = node.operands[position];
                if (holds_clocked_sequence(tree, part)) {
                    components(part, out, depth + 1U);
                } else if (!default_clock.empty()) {
                    TreeNode implied;
                    implied.kind = TreeNode::Kind::ClockedSequence;
                    implied.sequence = true;
                    implied.clock_events = default_clock;
                    implied.operands = { part };
                    implied.span = span;
                    tree.nodes.push_back(std::move(implied));
                    TreeOperand reference;
                    reference.node
                        = static_cast<std::int32_t>(tree.nodes.size() - 1U);
                    out.push_back(reference);
                }
            }
        };
    const auto wrap = [&](TreeOperand& operand) {
        if (!holds_clocked_sequence(tree, operand)) {
            return;
        }
        TreeNode multiclock;
        multiclock.kind = TreeNode::Kind::MulticlockSequence;
        multiclock.sequence = true;
        multiclock.span = span;
        std::vector<TreeOperand> parts;
        components(operand, parts, 0U);
        multiclock.operands = std::move(parts);
        tree.nodes.push_back(std::move(multiclock));
        operand.node = static_cast<std::int32_t>(tree.nodes.size() - 1U);
    };
    // A clocking event over one sequence where a property is expected is a
    // clocked property (the grammar's `clocking_event property_expr`), not a
    // part of a multiclocked sequence.
    const auto as_property = [&](const TreeOperand& operand) {
        if (operand.node >= 0) {
            auto& node = tree.nodes[static_cast<std::size_t>(operand.node)];
            if (node.kind == TreeNode::Kind::ClockedSequence) {
                node.kind = TreeNode::Kind::ClockedProperty;
                node.sequence = false;
            }
        }
    };
    const auto joins_sequences = [&](const TreeNode& node) {
        return sequence_operation(node)
            || node.kind == TreeNode::Kind::ClockedSequence
            || node.kind == TreeNode::Kind::MulticlockSequence;
    };
    const auto count = tree.nodes.size();
    // First the clocked sequences under a property operation, then the
    // sequence operations that join them.
    for (std::size_t index = 0U; index < count; ++index) {
        if (joins_sequences(tree.nodes[index])) {
            continue;
        }
        for (const auto& operand : tree.nodes[index].operands) {
            as_property(operand);
        }
    }
    if (property_root) {
        as_property(tree.root);
    }
    for (std::size_t index = 0U; index < count; ++index) {
        // A clocked property holds a property; a sequence operation's own
        // operands are the parts of the sequence.
        if (sequence_operation(tree.nodes[index])
            || tree.nodes[index].kind == TreeNode::Kind::ClockedSequence
            || tree.nodes[index].kind == TreeNode::Kind::MulticlockSequence) {
            continue;
        }
        for (std::size_t position = 0U;
             position < tree.nodes[index].operands.size(); ++position) {
            auto operand = tree.nodes[index].operands[position];
            wrap(operand);
            tree.nodes[index].operands[position] = operand;
        }
    }
    wrap(tree.root);
}

// The property or sequence expression tree a declaration (or a directive's
// instance) publishes (IEEE 1800-2023 37.50-37.54): the parser's tree with
// named instances kept, its boolean leaves, conditions and bounds as
// expressions, in the operand order of the standard.
namespace {

// A property instance in the declaration of a property (16.12.17, F.7):
// VerilogParser::SvaPropertyInstanceFact with the declaration it names.
struct PropertyInstanceFact {
    const SystemVerilogAssertionDeclaration* callee { };
    std::optional<std::int64_t> weight;
    bool restricted { };
    bool formal_actuals { };
};

[[nodiscard]] std::vector<PropertyInstanceFact> property_instance_facts(
    const DesignUnit& unit, const ParseContext& context,
    const std::vector<Node>& nodes, std::size_t root,
    const SystemVerilogAssertionDeclaration& declaration);

} // namespace

std::optional<SystemVerilogAssertionTree> VerilogParser::build_assertion_tree(
    const DesignUnit& unit, std::vector<Token> tokens,
    const SystemVerilogAssertionDeclaration* declaration,
    std::string* const failure)
{
    using Operator = SystemVerilogAssertionOperator;
    strip_semicolons(tokens);
    if (tokens.empty()) {
        return std::nullopt;
    }
    const auto model = tokens.front();
    std::vector<Node> nodes;
    const bool keywords_2009 = keyword_set_rank(keyword_set_)
        >= keyword_set_rank(KeywordSet::SystemVerilog2009);
    ParseContext context {
        unit,
        [this, keywords_2009](const std::span<const Token> operand) {
            static constexpr std::array<std::string_view, 20> operators {
                "and", "or", "not", "intersect", "within", "throughout",
                "iff", "implies", "until", "s_until", "until_with",
                "s_until_with", "first_match", "nexttime", "s_nexttime",
                "always", "s_always", "eventually", "s_eventually", "strong",
            };
            if (std::ranges::any_of(operand, [&](const Token& token) {
                    return token.kind == TokenKind::Hash
                        || (token.kind == TokenKind::Identifier
                            && std::ranges::find(operators, token.text)
                                != operators.end()
                            && (keywords_2009
                                || !property_keyword_2009(token.text)));
                })) {
                return false;
            }
            const auto saved = diagnostics_.size();
            const bool valid = assertion_expression(operand).has_value();
            diagnostics_.resize(saved);
            return valid;
        },
    };
    context.keep_instances = true;
    context.keywords_2009 = keywords_2009;
    context.scopes = sva_name_scopes(unit);
    context.origin = declaration != nullptr
        ? declaration_scope(context, *declaration)
        : context.scopes.size() - 1U;
    if (declaration != nullptr && declaration->clock) {
        auto resolved = resolve_clock(unit, context.scopes,
            declaration->clock->event_tokens, context.origin);
        context.clocks.push_back(std::move(resolved.tokens));
        context.clock_identities.push_back(std::move(resolved.identity));
    }
    TreeParser parser { nodes, std::move(tokens), context, { } };
    const auto parsed = parser.parse();
    if (!parsed) {
        if (failure != nullptr) {
            *failure = context.failure;
        }
        return std::nullopt;
    }
    if (declaration != nullptr
        && declaration->kind == SystemVerilogAssertionDeclarationKind::Property) {
        const auto key_of = [](const SystemVerilogAssertionDeclaration& entry) {
            return SvaDeclarationKey { entry.name, entry.name_span.begin.line,
                entry.name_span.begin.offset };
        };
        auto& facts = sva_property_facts_[key_of(*declaration)];
        facts.name = declaration->name;
        facts.name_span = declaration->name_span;
        facts.disable = declaration->disable.has_value();
        facts.instances.clear();
        for (const auto& fact : property_instance_facts(
                 unit, context, nodes, *parsed, *declaration)) {
            facts.instances.push_back({ key_of(*fact.callee), fact.callee->name,
                fact.weight, fact.restricted, fact.formal_actuals });
        }
    }
    SystemVerilogAssertionTree tree;
    bool failed { };
    const auto expression_of = [&](const std::span<const Token> operand)
        -> std::optional<Expression> {
        const auto saved = diagnostics_.size();
        auto expression = assertion_expression(operand);
        diagnostics_.resize(saved);
        return expression;
    };
    const auto literal = [&](const std::int64_t value) {
        const Token number = punctuation(model, TokenKind::Number,
            std::to_string(value));
        return expression_of(std::span<const Token> { &number, 1U });
    };
    // A bound, a count or a range end as an operand.
    const auto bound = [&](const BoundSpec& spec) {
        SystemVerilogAssertionTreeOperand operand;
        if (spec.kind == BoundSpec::Kind::literal) {
            operand.expression = literal(spec.value);
        } else if (spec.kind == BoundSpec::Kind::expression) {
            operand.expression = expression_of(spec.tokens);
        } else {
            const Token dollar = punctuation(model, TokenKind::Identifier, "$");
            operand.expression = expression_of(std::span<const Token> { &dollar, 1U });
        }
        return operand;
    };
    const auto same_bound = [](const BoundSpec& left, const BoundSpec& right) {
        return left.kind == right.kind
            && (left.kind == BoundSpec::Kind::unbounded
                || (left.kind == BoundSpec::Kind::literal
                    && left.value == right.value)
                || (left.kind == BoundSpec::Kind::expression
                    && spelling(left.tokens) == spelling(right.tokens)));
    };
    const std::function<SystemVerilogAssertionTreeOperand(std::size_t)> convert
        = [&](const std::size_t index) -> SystemVerilogAssertionTreeOperand {
        // A copy: converting an instance's actuals appends to `nodes`.
        const Node node = nodes[index];
        const auto add = [&](const Operator op, const bool sequence,
                             std::vector<SystemVerilogAssertionTreeOperand> operands,
                             const bool strong = false) {
            SystemVerilogAssertionTreeNode published;
            published.op = op;
            published.sequence = sequence;
            published.strong = strong;
            published.operands = std::move(operands);
            published.span = declaration != nullptr ? declaration->span
                                                    : model.span;
            tree.nodes.push_back(std::move(published));
            SystemVerilogAssertionTreeOperand operand;
            operand.node = static_cast<std::int32_t>(tree.nodes.size() - 1U);
            return operand;
        };
        const auto child = [&](const std::size_t position) {
            return convert(node.operands[position]);
        };
        switch (node.kind) {
        case NodeKind::boolean: {
            SystemVerilogAssertionTreeOperand leaf;
            leaf.expression = expression_of(node.expression);
            failed = failed || !leaf.expression;
            return leaf;
        }
        case NodeKind::clocked:
        case NodeKind::clocked_property: {
            // A clocking event over its operand (37.52 clocked property,
            // 37.56 clocked seq).
            SystemVerilogAssertionTreeNode published;
            published.kind = node.kind == NodeKind::clocked
                ? SystemVerilogAssertionTreeNode::Kind::ClockedSequence
                : SystemVerilogAssertionTreeNode::Kind::ClockedProperty;
            published.sequence = node.kind == NodeKind::clocked;
            if (node.clock < context.clocks.size()) {
                published.clock_events
                    = assertion_clock_events(context.clocks[node.clock]);
            }
            published.operands.push_back(child(0));
            published.span = declaration != nullptr ? declaration->span
                                                    : model.span;
            tree.nodes.push_back(std::move(published));
            SystemVerilogAssertionTreeOperand operand;
            operand.node = static_cast<std::int32_t>(tree.nodes.size() - 1U);
            return operand;
        }
        case NodeKind::case_property: {
            SystemVerilogAssertionTreeNode published;
            published.kind = SystemVerilogAssertionTreeNode::Kind::CaseProperty;
            SystemVerilogAssertionTreeOperand condition;
            condition.expression = expression_of(node.expression);
            failed = failed || !condition.expression;
            published.operands.push_back(std::move(condition));
            for (std::size_t position = 0; position < node.operands.size();
                 ++position) {
                published.operands.push_back(child(position));
            }
            published.span = declaration != nullptr ? declaration->span
                                                    : model.span;
            tree.nodes.push_back(std::move(published));
            SystemVerilogAssertionTreeOperand operand;
            operand.node = static_cast<std::int32_t>(tree.nodes.size() - 1U);
            return operand;
        }
        case NodeKind::case_item: {
            SystemVerilogAssertionTreeNode published;
            published.kind = SystemVerilogAssertionTreeNode::Kind::CaseItem;
            published.default_item = node.default_item;
            for (const auto& value : node.actual_tokens) {
                SystemVerilogAssertionTreeOperand expression;
                expression.expression = expression_of(value);
                failed = failed || !expression.expression;
                published.operands.push_back(std::move(expression));
            }
            published.operands.push_back(child(0));
            published.span = declaration != nullptr ? declaration->span
                                                    : model.span;
            tree.nodes.push_back(std::move(published));
            SystemVerilogAssertionTreeOperand operand;
            operand.node = static_cast<std::int32_t>(tree.nodes.size() - 1U);
            return operand;
        }
        case NodeKind::strong:
        case NodeKind::weak: {
            auto operand = child(0);
            if (operand.node >= 0) {
                tree.nodes[static_cast<std::size_t>(operand.node)].strong
                    = node.kind == NodeKind::strong;
            } else {
                operand.strong = node.kind == NodeKind::strong;
            }
            return operand;
        }
        case NodeKind::delay: {
            std::vector<SystemVerilogAssertionTreeOperand> operands;
            operands.push_back(child(0));
            if (!node.leading) {
                operands.push_back(child(1));
            }
            operands.push_back(bound(node.lo));
            if (!same_bound(node.lo, node.hi)) {
                operands.push_back(bound(node.hi));
            }
            return add(node.leading ? Operator::UnaryCycleDelay
                                    : Operator::CycleDelay,
                true, std::move(operands));
        }
        case NodeKind::repeat:
        case NodeKind::goto_repeat:
        case NodeKind::nonconsecutive_repeat: {
            std::vector<SystemVerilogAssertionTreeOperand> operands;
            operands.push_back(child(0));
            operands.push_back(bound(node.lo));
            if (!same_bound(node.lo, node.hi)) {
                operands.push_back(bound(node.hi));
            }
            return add(node.kind == NodeKind::repeat ? Operator::ConsecutiveRepeat
                    : node.kind == NodeKind::goto_repeat ? Operator::GotoRepeat
                                                         : Operator::Repeat,
                true, std::move(operands));
        }
        case NodeKind::sequence_or:
        case NodeKind::disjunction:
        case NodeKind::sequence_and:
        case NodeKind::conjunction: {
            const bool is_or = node.kind == NodeKind::sequence_or
                || node.kind == NodeKind::disjunction;
            auto left = child(0);
            auto right = child(1);
            return add(is_or ? Operator::CompOr : Operator::CompAnd,
                node.kind == NodeKind::sequence_or
                    || node.kind == NodeKind::sequence_and,
                { std::move(left), std::move(right) });
        }
        case NodeKind::intersect:
        case NodeKind::within: {
            auto left = child(0);
            auto right = child(1);
            return add(node.kind == NodeKind::intersect ? Operator::Intersect
                                                        : Operator::Within,
                true, { std::move(left), std::move(right) });
        }
        case NodeKind::throughout: {
            SystemVerilogAssertionTreeOperand condition;
            condition.expression = expression_of(node.expression);
            failed = failed || !condition.expression;
            return add(Operator::Throughout, true,
                { std::move(condition), child(0) });
        }
        case NodeKind::first_match:
            return add(Operator::FirstMatch, true, { child(0) });
        case NodeKind::match_items: {
            // The sequence with its match items, each parsed as the
            // statement it is (an assignment, an increment or decrement, or
            // a subroutine call; 16.10 A.2.10 sequence_match_item). Names of
            // local variables are not this scope's objects: the statement
            // is kept like a boolean's expression, with its diagnostics and
            // name references dropped.
            auto operand = child(0);
            for (const auto& item : node.actual_tokens) {
                if (item.empty()) {
                    failed = true;
                    continue;
                }
                auto tokens = item;
                tokens.push_back(punctuation(item.back(), TokenKind::Semicolon, ";"));
                const auto saved = diagnostics_.size();
                const auto references = implicit_net_references_.size();
                const auto hoisted = hoisted_statements_.size();
                auto statement = parse_generated_statement(std::move(tokens),
                    cover(item.front().span, item.back().span));
                diagnostics_.resize(saved);
                implicit_net_references_.resize(references);
                hoisted_statements_.resize(hoisted);
                if (!statement) {
                    failed = true;
                    continue;
                }
                operand.match_items.push_back(std::move(*statement));
            }
            return operand;
        }
        case NodeKind::initialized:
        case NodeKind::recursion_target:
        case NodeKind::recursion:
            // Only the monitor's parse makes these (the published tree keeps
            // instances).
            failed = true;
            return SystemVerilogAssertionTreeOperand { };
        case NodeKind::negation:
            return add(Operator::Not, false, { child(0) });
        case NodeKind::equivalence:
        case NodeKind::implies: {
            auto left = child(0);
            auto right = child(1);
            return add(node.kind == NodeKind::implies ? Operator::Implies
                                                      : Operator::Iff,
                false, { std::move(left), std::move(right) });
        }
        case NodeKind::implication:
        case NodeKind::followed_by: {
            auto left = child(0);
            auto right = child(1);
            const bool implication = node.kind == NodeKind::implication;
            return add(implication
                    ? (node.overlapped ? Operator::OverlapImply
                                       : Operator::NonOverlapImply)
                    : (node.overlapped ? Operator::OverlapFollowedBy
                                       : Operator::NonOverlapFollowedBy),
                false, { std::move(left), std::move(right) });
        }
        case NodeKind::conditional: {
            SystemVerilogAssertionTreeOperand condition;
            condition.expression = expression_of(node.expression);
            failed = failed || !condition.expression;
            std::vector<SystemVerilogAssertionTreeOperand> operands;
            operands.push_back(std::move(condition));
            for (std::size_t position = 0; position < node.operands.size();
                 ++position) {
                operands.push_back(child(position));
            }
            return add(node.operands.size() > 1U ? Operator::IfElse
                                                 : Operator::If,
                false, std::move(operands));
        }
        case NodeKind::nexttime: {
            std::vector<SystemVerilogAssertionTreeOperand> operands;
            operands.push_back(child(0));
            if (node.lo.kind != BoundSpec::Kind::literal || node.lo.value != 1) {
                operands.push_back(bound(node.lo));
            }
            return add(Operator::Nexttime, false, std::move(operands),
                node.strong);
        }
        case NodeKind::always:
        case NodeKind::eventually: {
            std::vector<SystemVerilogAssertionTreeOperand> operands;
            operands.push_back(child(0));
            const bool plain = node.lo.kind == BoundSpec::Kind::literal
                && node.lo.value == 0
                && node.hi.kind == BoundSpec::Kind::unbounded;
            if (!plain) {
                operands.push_back(bound(node.lo));
                operands.push_back(bound(node.hi));
            }
            return add(node.kind == NodeKind::always ? Operator::Always
                                                     : Operator::Eventually,
                false, std::move(operands), node.strong);
        }
        case NodeKind::until: {
            auto left = child(0);
            auto right = child(1);
            return add(node.with ? Operator::UntilWith : Operator::Until, false,
                { std::move(left), std::move(right) }, node.strong);
        }
        case NodeKind::accept:
        case NodeKind::reject: {
            SystemVerilogAssertionTreeOperand condition;
            condition.expression = expression_of(node.expression);
            failed = failed || !condition.expression;
            const bool accept = node.kind == NodeKind::accept;
            return add(accept ? (node.synchronous ? Operator::SyncAcceptOn
                                                  : Operator::AcceptOn)
                              : (node.synchronous ? Operator::SyncRejectOn
                                                  : Operator::RejectOn),
                false, { std::move(condition), child(0) });
        }
        case NodeKind::instance: {
            const Node& instance_node = node;
            const auto* const named = instance_node.declared;
            if (named == nullptr) {
                failed = true;
                return { };
            }
            // One operand per formal, in declaration order: the actual of
            // that name or position, else the formal's default (37.51 detail
            // 2, 37.54 detail 1).
            std::vector<std::vector<Token>> bound_tokens(named->formals.size());
            std::size_t positional { };
            for (const auto& actual : instance_node.actual_tokens) {
                if (actual.size() >= 4U && actual[0].kind == TokenKind::Dot
                    && actual[1].kind == TokenKind::Identifier
                    && actual[2].kind == TokenKind::LeftParen
                    && actual.back().kind == TokenKind::RightParen) {
                    const auto formal = std::ranges::find(named->formals,
                        actual[1].text,
                        &SystemVerilogAssertionFormal::name);
                    if (formal == named->formals.end()) {
                        failed = true;
                        continue;
                    }
                    bound_tokens[static_cast<std::size_t>(
                        formal - named->formals.begin())]
                        .assign(actual.begin() + 3, actual.end() - 1);
                } else if (positional < named->formals.size()) {
                    bound_tokens[positional++] = actual;
                } else {
                    failed = true;
                }
            }
            std::vector<SystemVerilogAssertionTreeOperand> operands;
            for (std::size_t position = 0; position < named->formals.size();
                 ++position) {
                const auto& written = bound_tokens[position].empty()
                    ? named->formals[position].default_tokens
                    : bound_tokens[position];
                SystemVerilogAssertionTreeOperand operand;
                if (!written.empty()) {
                    operand.expression = expression_of(written);
                    if (!operand.expression) {
                        // A sequence or property actual is a tree of its
                        // own.
                        // The shared node store keeps the indexes valid.
                        TreeParser shared { nodes, written, context, { } };
                        if (const auto actual_root = shared.parse()) {
                            operand = convert(*actual_root);
                        } else {
                            failed = true;
                        }
                    }
                }
                operands.push_back(std::move(operand));
            }
            SystemVerilogAssertionTreeNode published;
            published.kind = SystemVerilogAssertionTreeNode::Kind::Instance;
            published.instance = instance_node.instance;
            published.sequence = instance_node.sequence_instance;
            published.operands = std::move(operands);
            published.span = named->span;
            tree.nodes.push_back(std::move(published));
            SystemVerilogAssertionTreeOperand operand;
            operand.node = static_cast<std::int32_t>(tree.nodes.size() - 1U);
            return operand;
        }
        }
        failed = true;
        return { };
    };
    tree.root = convert(*parsed);
    if (failed || (tree.root.node < 0 && !tree.root.expression)) {
        return std::nullopt;
    }
    wrap_multiclock_sequences(tree,
        declaration != nullptr && declaration->clock
            ? declaration->clock->events
            : std::vector<Sensitivity> { },
        declaration != nullptr ? declaration->span : model.span,
        declaration == nullptr
            || declaration->kind
                != SystemVerilogAssertionDeclarationKind::Sequence);
    return tree;
}

// Publishes the trees of a scope's declarations and the property instances
// of its directives. `unit` holds the declarations visible in the scope.
void VerilogParser::publish_assertion_trees(const DesignUnit& unit,
    std::vector<SystemVerilogAssertionDeclaration>& declarations,
    std::vector<SystemVerilogConcurrentAssertion>& assertions)
{
    const auto scopes = sva_name_scopes(unit);
    const auto publish_declaration
        = [&](SystemVerilogAssertionDeclaration& declaration,
              const bool declared) {
              if (declaration.kind == SystemVerilogAssertionDeclarationKind::Checker
                  || declaration.tree) {
                  return;
              }
              std::string failure;
              auto tree = build_assertion_tree(
                  unit, declaration.expression_tokens, &declaration, &failure);
              if (!tree && !declared) {
                  // The directive's own sequence: the assertion engine
                  // reports it with the directive.
                  return;
              }
              if (!tree) {
                  // A declaration body that is no sequence or property
                  // expression is an error whether or not an assertion uses
                  // the declaration (16.8, 16.12).
                  Token location;
                  location.kind = TokenKind::Identifier;
                  location.text = declaration.name;
                  location.span = declaration.name_span;
                  error(declaration.expression_tokens.empty()
                          ? location
                          : declaration.expression_tokens.front(),
                      "FSIM-SV-SEM-475",
                      std::string { declaration.kind
                                  == SystemVerilogAssertionDeclarationKind::
                                      Sequence
                              ? "the body of sequence '"
                              : "the body of property '" }
                          + declaration.name + "' is not a "
                          + (declaration.kind
                                      == SystemVerilogAssertionDeclarationKind::
                                          Sequence
                                  ? "sequence"
                                  : "property")
                          + " expression"
                          + (failure.empty() ? std::string { }
                                             : ": " + failure));
                  return;
              }
              for (const auto& formal : declaration.formals) {
                  std::optional<Expression> value;
                  if (!formal.default_tokens.empty()) {
                      const auto saved = diagnostics_.size();
                      value = assertion_expression(formal.default_tokens);
                      diagnostics_.resize(saved);
                  }
                  tree->formal_defaults.push_back(std::move(value));
              }
              declaration.tree = std::move(tree);
          };
    for (auto& declaration : declarations) {
        publish_declaration(declaration, true);
    }
    for (auto& assertion : assertions) {
        if (assertion.inline_sequence) {
            publish_declaration(*assertion.inline_sequence, false);
        }
        // A property that names a declared property or sequence is an
        // instance of it (37.50 vpiProperty); an inline property is the
        // declaration the frontend synthesized, and the directive's own.
        if (assertion.property_instance || assertion.property_tokens.empty()
            || assertion.property_tokens.front().kind != TokenKind::Identifier
            || assertion.property_tokens.front().text.starts_with(
                "$inline_property$")) {
            continue;
        }
        if (visible_declaration(unit, scopes, assertion.property_tokens.front().text,
                scopes.empty() ? 0U : scopes.size() - 1U, std::nullopt)
            == nullptr) {
            continue;
        }
        assertion.property_instance
            = build_assertion_tree(unit, assertion.property_tokens, nullptr);
    }
}

std::vector<SvaNameScope> VerilogParser::sva_name_scopes(
    const DesignUnit& unit) const
{
    if (!sva_scopes_.empty()) {
        return sva_scopes_;
    }
    SvaNameScope scope;
    const auto add = [&](const auto& items) {
        for (const auto& item : items) {
            scope.names.insert(item.name);
        }
    };
    add(unit.parameters);
    add(unit.ports);
    add(unit.signals);
    add(unit.variables);
    add(unit.instances);
    add(unit.systemverilog_clocking_blocks);
    for (const auto& block : unit.systemverilog_clocking_blocks) {
        scope.clocking_events.emplace(block.name, block.event_tokens);
    }
    scope.declarations_end = unit.systemverilog_assertion_declarations.size();
    return { std::move(scope) };
}

std::optional<VerilogParser::SvaPrepared> VerilogParser::prepare_sva_property(
    const DesignUnit& unit, const SystemVerilogConcurrentAssertion& directive,
    std::string& reason, const std::string_view tick_prefix,
    const std::set<std::string>* const expect_names)
{
    const auto fail = [&](std::string why) -> std::optional<SvaPrepared> {
        reason = std::move(why);
        return std::nullopt;
    };
    const auto fail_illegal
        = [&](std::string why) -> std::optional<SvaPrepared> {
        sva_reject_illegal_ = true;
        return fail(std::move(why));
    };
    sva_uses_locals_ = false;
    sva_reject_illegal_ = false;
    std::vector<Token> tokens;
    std::vector<Token> clock { };
    std::vector<Token> disable;
    // The initialization assignments of the local variables of each
    // declaration inlined at the root (16.10), the outermost first.
    std::vector<std::vector<std::vector<Token>>> root_initializers;
    // The scopes the clocking events resolve their names in, and the one
    // the property tokens and the clock are written in: the directive's
    // (the last) or that of the declaration it instantiates (16.8).
    ParseContext scoped { unit, { } };
    scoped.scopes = sva_name_scopes(unit);
    const auto innermost = scoped.scopes.size() - 1U;
    std::size_t origin = innermost;
    std::size_t clock_origin = innermost;
    // Whether the clock taken so far and `event` written in `event_origin`
    // are different clocking events (16.16.1).
    const auto other_clock = [&](const std::span<const Token> event,
                                 const std::size_t event_origin) {
        return !clock.empty()
            && resolve_clock(unit, scoped.scopes, clock, clock_origin).identity
            != resolve_clock(unit, scoped.scopes, event, event_origin).identity;
    };
    const bool cover_sequence
        = directive.form == SystemVerilogConcurrentAssertionForm::Sequence;
    if (cover_sequence) {
        if (!directive.inline_sequence) {
            return fail("a sequence directive without its sequence");
        }
        const auto& sequence = *directive.inline_sequence;
        if (TreeParser::has_locals(sequence)) {
            sva_uses_locals_ = true;
            auto& initializers = root_initializers.emplace_back();
            if (auto error = TreeParser::declare_locals(scoped, sequence, { },
                    sequence.expression_tokens, tokens, initializers)) {
                return fail(std::move(*error));
            }
        } else {
            tokens = sequence.expression_tokens;
        }
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
    std::vector<std::string> inlined;
    // Whether the maximal property is a sequence or property instance.
    bool root_instance { };
    while (!tokens.empty()) {
        if (tokens.front().kind != TokenKind::Identifier) {
            break;
        }
        // A declaration already inlined on this path is recursive; it is
        // left to the recursive-property handling (16.12.17).
        if (std::ranges::find(inlined, tokens.front().text) != inlined.end()) {
            break;
        }
        inlined.push_back(std::string { tokens.front().text });
        // An end point's monitor instances the declaration resolved where
        // the end point was read.
        const auto* const declaration = inlined.size() == 1U
                && sva_root_declaration_ != nullptr
            ? sva_root_declaration_
            : visible_declaration(unit, scoped.scopes, tokens.front().text,
                  origin, std::nullopt);
        if (declaration == nullptr) {
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
        std::optional<std::vector<Token>> body;
        if (TreeParser::has_locals(*declaration)) {
            sva_uses_locals_ = true;
            body.emplace();
            auto& initializers = root_initializers.emplace_back();
            if (auto error = TreeParser::declare_locals(scoped, *declaration,
                    actuals, declaration->expression_tokens, *body, initializers)) {
                return fail(std::move(*error));
            }
        } else {
            body = TreeParser::substituted(
                *declaration, actuals, declaration->expression_tokens);
        }
        if (!body) {
            return fail("the instance actuals do not match the formals");
        }
        origin = declaration_scope(scoped, *declaration);
        // The declaration's clocking event and disable condition shall not
        // reference its local variables or local formals (16.10, 16.12).
        std::set<std::string, std::less<>> declared_locals;
        for (const auto& local : declaration->local_variables) {
            declared_locals.insert(local.name);
        }
        for (const auto& formal : declaration->formals) {
            if (formal.local) {
                declared_locals.insert(formal.name);
            }
        }
        if (declaration->clock
            && mentions_local(declaration->clock->event_tokens, declared_locals)) {
            return fail_illegal("a clocking event that references a local "
                                "variable (16.10)");
        }
        if (declaration->disable
            && mentions_local(
                declaration->disable->condition_tokens, declared_locals)) {
            return fail_illegal("a disable condition that references a local "
                                "variable (16.12)");
        }
        if (declaration->clock) {
            auto event = TreeParser::substituted(
                *declaration, actuals, declaration->clock->event_tokens);
            if (!event) {
                return fail("the instance clock does not substitute");
            }
            if (!TreeParser::inferred_clock_only(*event)) {
                if (other_clock(*event, origin)) {
                    return fail("more than one clocking event");
                }
                clock = std::move(*event);
                clock_origin = origin;
            }
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
        root_instance = true;
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
        if (other_clock(event, origin)) {
            return fail("more than one clocking event");
        }
        clock = std::move(event);
        clock_origin = origin;
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
    // Without a clock flowing in, the property's own unique semantic
    // leading clock clocks it (16.16.1: `s ##1 t` with s and t clocked
    // sequences); clock 0 then stands for the inherited clock until the
    // property is parsed.
    const bool inherited_clock = clock.empty();
    if (inherited_clock
        && (std::ranges::any_of(tokens,
                [](const Token& token) {
                    return token.text == "$inferred_clock";
                })
            || std::ranges::any_of(disable, [](const Token& token) {
                   return token.text == "$inferred_clock";
               }))) {
        return fail("no clocking event");
    }
    // The leading clock as the directive's scope reads it.
    auto leading_clock = inherited_clock
        ? ResolvedClock { }
        : resolve_clock(unit, scoped.scopes, clock, clock_origin);
    clock = std::move(leading_clock.tokens);
    // It is illegal to use an automatic variable in a clocking event
    // (IEEE 1800-2023 16.14.6).
    for (const auto& token : clock) {
        if (token.kind == TokenKind::Identifier
            && std::ranges::find(directive.procedural_automatic_names,
                   token.text)
                != directive.procedural_automatic_names.end()) {
            return fail("automatic variable '" + token.text
                + "' in a clocking event");
        }
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

    const bool keywords_2009 = keyword_set_rank(keyword_set_)
        >= keyword_set_rank(KeywordSet::SystemVerilog2009);
    ParseContext context {
        unit,
        [this, keywords_2009](const std::span<const Token> operand) {
            // Sequence and property operators never occur in an expression.
            static constexpr std::array<std::string_view, 20> operators {
                "and", "or", "not", "intersect", "within", "throughout",
                "iff", "implies", "until", "s_until", "until_with",
                "s_until_with", "first_match", "nexttime", "s_nexttime",
                "always", "s_always", "eventually", "s_eventually", "strong",
            };
            if (std::ranges::any_of(operand, [&](const Token& token) {
                    return token.kind == TokenKind::Hash
                        || (token.kind == TokenKind::Identifier
                            && std::ranges::find(operators, token.text)
                                != operators.end()
                            && (keywords_2009
                                || !property_keyword_2009(token.text)));
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
        { clock },
        0U,
    };
    context.clock_identities = { std::move(leading_clock.identity) };
    context.keywords_2009 = keywords_2009;
    context.scopes = std::move(scoped.scopes);
    context.origin = origin;
    context.locals = std::move(scoped.locals);
    context.local_names = std::move(scoped.local_names);
    context.uses_locals = scoped.uses_locals;
    context.illegal_recursive = &sva_illegal_recursive_;
    context.endpoint = [&](const SystemVerilogAssertionDeclaration& declared,
                           const std::span<const Token> receiver,
                           const bool matched, std::string& wait_bit,
                           std::string& failure) {
        return sva_endpoint_use(unit, context.scopes, context.origin,
            declared, receiver, matched,
            context.clocks[context.current_clock], directive.span, wait_bit,
            failure);
    };
    std::vector<Node> nodes;
    TreeParser parser { nodes, tokens, context, { } };
    auto root = parser.parse();
    sva_uses_locals_ = sva_uses_locals_ || context.uses_locals;
    if (!root) {
        sva_reject_illegal_ = context.illegal;
        return fail(context.failure);
    }
    // The root declarations' initialization assignments run when the
    // evaluation attempt begins, the outermost declaration's first.
    for (auto group = root_initializers.rbegin(); group != root_initializers.rend();
         ++group) {
        if (group->empty()) {
            continue;
        }
        Node wrapper;
        wrapper.kind = sequence_kind(nodes[*root].kind) ? NodeKind::match_items
                                                        : NodeKind::initialized;
        wrapper.leading = true;
        wrapper.operands = { *root };
        wrapper.actual_tokens = std::move(*group);
        nodes.push_back(std::move(wrapper));
        root = nodes.size() - 1U;
    }
    if (!disable.empty() && !context.boolean(disable)) {
        return fail("the disable condition is not a boolean expression");
    }
    // Neither a disable condition nor a clocking event shall reference a
    // local variable (16.10, 16.12.14).
    if (!context.local_names.empty()) {
        if (mentions_local(disable, context.local_names)) {
            return fail_illegal("a disable condition that references a "
                                "local variable (16.12)");
        }
        for (const auto& event : context.clocks) {
            if (mentions_local(event, context.local_names)) {
                return fail_illegal("a clocking event that references a "
                                    "local variable (16.10)");
            }
        }
    }
    PlanCompiler compiler { nodes, synthetic_tokens("1'b1", directive.span) };
    {
        std::vector<std::string> names;
        for (const auto& local : context.locals) {
            names.push_back(local.name);
        }
        compiler.use_locals(names);
    }
    if (inherited_clock) {
        // The inherited clock is no clock: the property's set of semantic
        // leading clocks shall be one clock of its own (16.16.1, 16.16 e).
        std::set<std::uint32_t> leading;
        if (context.clocks.size() > 1U) {
            compiler.leading_clocks(*root, 0U, leading);
        }
        if (leading.empty() || leading.contains(0U)
            || std::ranges::any_of(nodes, [](const Node& node) {
                   return (node.kind == NodeKind::clocked
                              || node.kind == NodeKind::clocked_property)
                       && node.clock == 0U;
               })) {
            return fail("no clocking event");
        }
        if (leading.size() != 1U) {
            return fail_illegal("a multiclocked property without a unique "
                                "semantic leading clock (16.16)");
        }
        // With no clock from the statement, a default clocking or the
        // procedure, the maximal property shall be an instance (16.16 f).
        if (!root_instance) {
            return fail_illegal("a concurrent assertion without a leading "
                                "clocking event whose property is not a "
                                "sequence or property instance (16.16)");
        }
        const auto unique = *leading.begin() - 1U;
        context.clocks.erase(context.clocks.begin());
        context.clock_identities.erase(context.clock_identities.begin());
        for (auto& node : nodes) {
            if (node.kind == NodeKind::clocked
                || node.kind == NodeKind::clocked_property) {
                --node.clock;
                node.clock = node.clock == 0U ? unique
                    : node.clock == unique   ? 0U
                                             : node.clock;
            }
        }
        std::swap(context.clocks[0], context.clocks[unique]);
        std::swap(context.clock_identities[0],
            context.clock_identities[unique]);
        clock = context.clocks[0];
    }
    const bool multiclocked = context.clocks.size() > 1U;
    if (multiclocked && (tick_prefix.empty() || expect_names != nullptr)) {
        return fail("a multiclocked sequence in a procedural assertion");
    }
    // The clock in effect at the root: the directive's (clock 0).
    std::uint32_t root_clock = 0U;
    if (multiclocked) {
        // The maximal property shall have a unique semantic leading clock
        // (16.16 e), 16.16.1); attempts start at its ticks, so it becomes
        // clock 0 of the plan (the directive's clock, when the property
        // replaces it, keeps the other index).
        std::set<std::uint32_t> leading;
        compiler.leading_clocks(*root, 0U, leading);
        if (leading.size() != 1U) {
            return fail_illegal("a multiclocked property without a unique "
                                "semantic leading clock (16.16)");
        }
        const auto unique = *leading.begin();
        if (unique != 0U) {
            std::swap(context.clocks[0], context.clocks[unique]);
            std::swap(context.clock_identities[0],
                context.clock_identities[unique]);
            for (auto& node : nodes) {
                if (node.kind == NodeKind::clocked
                    || node.kind == NodeKind::clocked_property) {
                    node.clock = node.clock == 0U ? unique
                        : node.clock == unique   ? 0U
                                                 : node.clock;
                }
            }
            root_clock = unique;
        }
    }
    compiler.compile(*root, cover_sequence
            && directive.kind == SystemVerilogConcurrentAssertionKind::Cover,
        root_clock);
    if (!compiler.failure.empty()) {
        sva_reject_illegal_ = compiler.illegal;
        return fail(compiler.failure);
    }
    SvaPrepared prepared;
    // The asynchronous aborts are checked at every time step from the start
    // of their evaluation, with sampled values, not only at the ticks of
    // the clock (IEEE 1800-2023 16.12.14). A disable condition reads
    // current values at any time of an attempt (16.12); one with a sampled
    // value function or a sequence's triggered state changes value at the
    // start of a time step without a change of its operands (16.9.3,
    // 16.13.6), which no continuous assignment sees. Either way the
    // monitor also evaluates at time steps without a tick of its clock.
    std::set<std::uint32_t> abort_guards;
    for (const auto& property : compiler.plan.properties) {
        if ((property.kind == plan::PropertyKind::accept
                || property.kind == plan::PropertyKind::reject)
            && !property.synchronous) {
            abort_guards.insert(property.guard);
        }
    }
    const auto unwatchable = [&](const std::span<const Token> condition) {
        for (std::size_t position = 0; position < condition.size();
             ++position) {
            const auto& token = condition[position];
            if (token.kind != TokenKind::Identifier) {
                continue;
            }
            if (token.text.starts_with('$')
                || (position > 0U
                    && condition[position - 1U].kind == TokenKind::Dot
                    && token.text == "triggered")
                || std::ranges::find(directive.procedural_automatic_names,
                       token.text)
                    != directive.procedural_automatic_names.end()
                || (expect_names != nullptr
                    && expect_names->contains(std::string { token.text }))) {
                return true;
            }
        }
        return false;
    };
    for (const auto guard : abort_guards) {
        prepared.async_aborts.push_back(compiler.guards[guard]);
        prepared.every_step
            = prepared.every_step || unwatchable(compiler.guards[guard]);
    }
    prepared.disable_watched = !disable.empty() && !unwatchable(disable);
    if (!disable.empty() && !prepared.disable_watched) {
        prepared.every_step = true;
    }
    prepared.wakes = !prepared.async_aborts.empty() || prepared.every_step;
    if (multiclocked
        || ((prepared.wakes || sva_force_ticks_) && !tick_prefix.empty())) {
        // Each clock's tick is a hidden bit its own clock process sets and
        // the monitor's tick clears (`<prefix>_ck`, `<prefix>_ck<k>` for
        // clock k of a multiclocked plan), read with its current value
        // (SvaOperation::wake_signal): a clock ticks when its bit is set and
        // it has not ticked in the time step yet (16.13, 16.12.14).
        std::vector<std::vector<Token>> ticks;
        for (std::size_t index = 0; index < context.clocks.size(); ++index) {
            ticks.push_back(synthetic_tokens(multiclocked
                    ? std::string { tick_prefix } + "_ck" + std::to_string(index)
                    : std::string { tick_prefix } + "_ck",
                directive.span));
        }
        compiler.clock_ticks(ticks);
        prepared.clocks = std::move(context.clocks);
    }
    prepared.plan = semantic::sva::encode(compiler.plan);
    prepared.guards = std::move(compiler.guards);
    prepared.bounds = std::move(compiler.bounds);
    prepared.clock = std::move(clock);
    prepared.disable = std::move(disable);
    for (auto& local : context.locals) {
        prepared.locals.push_back({ std::move(local.name), std::move(local.type) });
    }
    // A sampled value function in a concurrent assertion without a clocking
    // argument takes the assertion's clock (IEEE 1800-2023 16.9.3). The
    // monitor evaluates an expression of local variables, and a subroutine
    // call's arguments, only when a thread needs them, so the clock is made
    // explicit there: the function's history then follows the clock's
    // ticks, not the evaluations. Its argument reads no local (16.9.3).
    std::string clock_failure;
    const auto clocked_samples = [&](std::vector<Token>& tokens) {
        for (std::size_t position = 0; position + 1U < tokens.size(); ++position) {
            const auto& name = tokens[position].text;
            const bool past = name == "$past";
            if (tokens[position].kind != TokenKind::Identifier
                || tokens[position + 1U].kind != TokenKind::LeftParen
                || !(past || name == "$rose" || name == "$fell"
                    || name == "$stable" || name == "$changed")) {
                continue;
            }
            std::size_t depth = 0U;
            std::size_t commas = 0U;
            std::size_t close = position + 1U;
            for (; close < tokens.size(); ++close) {
                const auto kind = tokens[close].kind;
                if (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket
                    || kind == TokenKind::LeftBrace) {
                    ++depth;
                } else if (kind == TokenKind::RightParen
                    || kind == TokenKind::RightBracket
                    || kind == TokenKind::RightBrace) {
                    if (--depth == 0U) {
                        break;
                    }
                } else if (kind == TokenKind::Comma && depth == 1U) {
                    ++commas;
                }
            }
            if (close >= tokens.size()) {
                return;
            }
            const auto arguments = commas + 1U;
            if (arguments >= (past ? 4U : 2U)) {
                continue;
            }
            if (multiclocked) {
                clock_failure = "a sampled value function without a clocking "
                                "argument in an expression of local variables "
                                "of a multiclocked property (16.9.3, 16.13)";
                return;
            }
            std::string text;
            for (auto missing = arguments; missing < (past ? 3U : 1U); ++missing) {
                text += missing == 1U ? ", 1" : ", ";
            }
            auto added = synthetic_tokens(text + ", @(", directive.span);
            added.insert(added.end(), prepared.clock.begin(),
                prepared.clock.end());
            const auto tail = synthetic_tokens(")", directive.span);
            added.insert(added.end(), tail.begin(), tail.end());
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(close),
                added.begin(), added.end());
            position = close + added.size();
        }
    };
    for (auto& [expression, target] : compiler.expressions) {
        clocked_samples(expression);
        prepared.expressions.push_back({ std::move(expression), target });
    }
    for (auto& call : compiler.calls) {
        clocked_samples(call);
    }
    // The guards of a singly clocked directive's monitor too: the monitor
    // also evaluates at wakes between the ticks, and the function's history
    // follows the clock's ticks (16.9.3), which the compiled kernel then
    // reads in place. Only a clock that is an edge of one name, not a
    // clocking block, is written out; any other keeps the evaluations'
    // history. An expect statement's guards are left as they are.
    const auto simple_clock = [&] {
        const auto& event = prepared.clock;
        if (event.empty() || event.size() > 2U
            || event.back().kind != TokenKind::Identifier
            || event_keyword(event.back())) {
            return false;
        }
        if (event.size() == 2U) {
            return token_is_keyword(event.front(), "posedge")
                || token_is_keyword(event.front(), "negedge")
                || token_is_keyword(event.front(), "edge");
        }
        const auto level = name_scope(scoped.scopes, event.back().text, clock_origin);
        return !level
            || !scoped.scopes[*level].clocking_events.contains(event.back().text);
    };
    if (!multiclocked && !tick_prefix.empty() && expect_names == nullptr
        && simple_clock()) {
        for (auto& guard : prepared.guards) {
            clocked_samples(guard);
        }
    }
    if (!clock_failure.empty()) {
        return fail(clock_failure);
    }
    prepared.calls = std::move(compiler.calls);
    prepared.endpoint_waits = std::move(context.endpoint_waits);
    return prepared;
}

std::vector<Token> VerilogParser::sva_tick_call(const SvaPrepared& prepared,
    const std::uint32_t site, const std::span<const Token> start,
    const SourceSpan& span, const bool sampled,
    const std::span<const Token> disable_signal,
    const std::span<const Token> wake)
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
    if (!disable_signal.empty()) {
        call.insert(call.end(), disable_signal.begin(), disable_signal.end());
    } else if (prepared.disable.empty()) {
        emit("1'b0");
    } else {
        emit_truth(prepared.disable);
    }
    for (const auto& bound : prepared.bounds) {
        emit(", (");
        call.insert(call.end(), bound.begin(), bound.end());
        emit(")");
    }
    // Each local variable's witness: the process variable of its type
    // (16.10), whose width the elaboration gives the plan.
    for (const auto& local : prepared.locals) {
        emit(", " + local.name);
    }
    if (!wake.empty()) {
        emit(", ");
        call.insert(call.end(), wake.begin(), wake.end());
    }
    emit(")");
    return call;
}

std::vector<Token> VerilogParser::sva_local_declarations(
    const SvaPrepared& prepared, const std::string& prefix,
    const SourceSpan& span) const
{
    std::vector<Token> result;
    const auto emit = [&](const std::string_view text) {
        auto lexed = synthetic_tokens(text, span);
        result.insert(result.end(), lexed.begin(), lexed.end());
    };
    if (!prepared.calls.empty()) {
        emit("int " + prefix + "_k; ");
    }
    if (prepared.locals.empty()) {
        return result;
    }
    for (const auto& local : prepared.locals) {
        result.insert(result.end(), local.type.begin(), local.type.end());
        emit(" " + local.name + "; ");
    }
    emit("int " + prefix + "_r; int " + prefix + "_e; int " + prefix + "_a; ");
    return result;
}

std::vector<Token> VerilogParser::sva_call_loop(const SvaPrepared& prepared,
    const std::uint32_t site, const std::string& prefix, const SourceSpan& span)
{
    std::vector<Token> result;
    if (prepared.calls.empty()) {
        return result;
    }
    const auto emit = [&](const std::string_view text) {
        auto lexed = synthetic_tokens(text, span);
        result.insert(result.end(), lexed.begin(), lexed.end());
    };
    // The subroutine calls the tick queued, in the order the threads
    // reached their end points (16.11), each with that thread's locals; the
    // by-value arguments read sampled values, as the request loop does.
    const auto which = prefix + "_k";
    const auto site_text = std::to_string(site);
    const auto next = which + " = $__fsim_sva_call(" + site_text + "); ";
    // The calls run in the Reactive region, like an action block (16.11);
    // the process keeps reading sampled values there.
    emit(next + "if (" + which + " != 0) begin __fsim_act_callregion(); while ("
        + which + " != 0) begin ");
    for (std::size_t local = 0; local < prepared.locals.size(); ++local) {
        emit(prepared.locals[local].name + " = $__fsim_sva_local(" + site_text
            + ", -1, " + std::to_string(local) + "); ");
    }
    for (std::size_t index = 0; index < prepared.calls.size(); ++index) {
        emit(std::string { index == 0U ? "" : "else " } + "if (" + which
            + " == " + std::to_string(index + 1U) + ") begin ");
        result.insert(result.end(), prepared.calls[index].begin(),
            prepared.calls[index].end());
        emit("; end ");
    }
    emit(next + "end end ");
    return result;
}

std::vector<Token> VerilogParser::sva_request_loop(const SvaPrepared& prepared,
    const std::uint32_t site, const std::span<const Token> tick,
    const std::string& prefix, const SourceSpan& span)
{
    std::vector<Token> result;
    if (prepared.expressions.empty()) {
        return result;
    }
    const auto emit = [&](const std::string_view text) {
        auto lexed = synthetic_tokens(text, span);
        result.insert(result.end(), lexed.begin(), lexed.end());
    };
    // A tick that needs the value of an expression for a thread's local
    // variables returns -(requests) and changes nothing (IEEE 1800-2023
    // 16.10): each request loads the thread's locals into the process's
    // variables, evaluates the expression with the sampled values of the
    // design's (16.5.1), and answers; then the tick runs again.
    const auto count = prefix + "_n";
    const auto number = prefix + "_r";
    const auto which = prefix + "_e";
    const auto answer = prefix + "_a";
    const auto site_text = std::to_string(site);
    emit("while (" + count + " < 0) begin for (" + number + " = 0; " + number
        + " < -" + count + "; " + number + " = " + number + " + 1) begin ");
    for (std::size_t local = 0; local < prepared.locals.size(); ++local) {
        emit(prepared.locals[local].name + " = $__fsim_sva_local(" + site_text
            + ", " + number + ", " + std::to_string(local) + "); ");
    }
    emit(which + " = $__fsim_sva_request(" + site_text + ", " + number + "); ");
    for (std::size_t index = 0; index < prepared.expressions.size(); ++index) {
        const auto& expression = prepared.expressions[index];
        emit(std::string { index == 0U ? "" : "else " } + "if (" + which
            + " == " + std::to_string(index) + ") ");
        if (expression.target == 0U) {
            emit(answer + " = $__fsim_sva_answer(" + site_text + ", " + number
                + ", ((|(");
            result.insert(result.end(), expression.tokens.begin(),
                expression.tokens.end());
            emit(")) === 1'b1)); ");
        } else {
            emit("begin ");
            result.insert(result.end(), expression.tokens.begin(),
                expression.tokens.end());
            emit("; " + answer + " = $__fsim_sva_answer(" + site_text + ", "
                + number + ", " + prepared.locals[expression.target - 1U].name
                + "); end ");
        }
    }
    emit("end " + count + " = ");
    result.insert(result.end(), tick.begin(), tick.end());
    emit("; end ");
    return result;
}

void VerilogParser::resolve_expect_statements(const DesignUnit& scope,
    std::vector<Process>& processes,
    std::vector<TaskDeclaration>& tasks,
    std::vector<SignalDeclaration>& signals,
    std::vector<Statement>& concurrent_statements)
{
    using namespace assertion_resolution_detail;
    // The names the process or task that holds the statement declares
    // anywhere in it (its arguments, variables, block declarations and loop
    // variables): a continuous assignment of the scope cannot read them.
    std::set<std::string> local_names;
    // The clock processes of the expect statements that set tick bits,
    // added to the scope once the walk is done.
    std::vector<Process> clock_processes;
    const auto collect_locals = [&](const auto& self,
                                    const std::vector<Statement>& items) -> void {
        for (const auto& item : items) {
            for (const auto& declaration : item.declarations) {
                local_names.insert(declaration.name);
            }
            if (!item.loop_variable.empty()) {
                local_names.insert(item.loop_variable);
            }
            self(self, item.statements);
            self(self, item.else_statements);
            for (const auto& alternative : item.case_alternatives) {
                self(self, alternative.statements);
            }
        }
    };
    const auto resolve = [&](Statement& statement) {
        const auto directive = std::move(*statement.procedural_concurrent_assertion);
        statement = Statement { };
        statement.span = directive.span;
        std::string reason;
        // A monitor that also evaluates between its clock's ticks (an
        // asynchronous abort, 16.12.14, or a disable condition no hidden net
        // can follow, 16.12) tells its ticks from its wakes by the tick bit
        // `<prefix>_ck` (prepare_sva_property).
        const auto prefix = "__fsim_ex" + std::to_string(expect_sites_ + 1U);
        const auto prepared
            = prepare_sva_property(scope, directive, reason, prefix, &local_names);
        if (!prepared) {
            Token location;
            location.kind = TokenKind::Identifier;
            location.text = "expect";
            location.span = directive.span;
            if (sva_reject_illegal_) {
                error(location, "FSIM-SV-SEM-474",
                    "the expect property is illegal: " + reason);
            } else {
                error(location, "FSIM-SV-SEM-200",
                    "the expect property cannot be evaluated: " + reason);
            }
            return;
        }
        // Every expect statement has its own monitor site: a process may
        // run several, directly or through tasks.
        const auto site = ++expect_sites_;
        std::vector<Token> body;
        const auto emit = [&](const std::string_view text) {
            auto lexed = synthetic_tokens(text, directive.span);
            body.insert(body.end(), lexed.begin(), lexed.end());
        };
        // The disable condition reads current values at any time of the
        // attempt (16.12), as a directive's does: a hidden net of the scope
        // holds its truth, kept by a continuous assignment, and the engine
        // watches it (SvaOperation::disable_signal). When it becomes 1 the
        // attempt ends in the disabled state at once, with the disabled
        // outcome (SvaOperation::monitor_flags, monitor_expect), and the
        // statement, which also waits for the net's rising edge, completes
        // in that time step and region. A condition that reads a sampled
        // value function, a triggered state or a name of the process or
        // task is read by the tick at the clock ticks.
        const auto disable_net = prefix + "_dt";
        bool watched_disable = prepared->disable_watched;
        std::optional<Statement> disable_assignment;
        if (watched_disable) {
            std::vector<Token> assignment
                = synthetic_tokens(disable_net + " = ((|(", directive.span);
            assignment.insert(assignment.end(), prepared->disable.begin(),
                prepared->disable.end());
            const auto tail = synthetic_tokens(")) === 1'b1);", directive.span);
            assignment.insert(assignment.end(), tail.begin(), tail.end());
            disable_assignment
                = parse_generated_statement(std::move(assignment), directive.span);
            if (!disable_assignment
                || disable_assignment->kind != StatementKind::Assignment) {
                disable_assignment.reset();
                watched_disable = false;
            } else {
                disable_assignment->assignment_kind = AssignmentKind::Continuous;
            }
        }
        // The asynchronous abort conditions are checked with sampled values
        // at every time step of the attempt (16.12.14), and a disable
        // condition that no hidden net can follow is read at every time
        // step (16.12), as for a directive's monitor: the engine toggles
        // the wake bit `<prefix>_wk` at the start of such a time step
        // (after the abort net `<prefix>_ab` changed, or at each one), and
        // the statement waits for it too. The clock's tick bit pulses in
        // the time step of each clocking event (its process clears it with
        // #0, so a wake at the start of a later time step reads 0), and
        // the tick reads it with its current value.
        const bool wakes = prepared->wakes;
        const auto tick_bit = prefix + "_ck";
        const auto wake_bit = prefix + "_wk";
        const auto abort_net = prefix + "_ab";
        const bool abort_watched = wakes && !prepared->every_step
            && !prepared->async_aborts.empty();
        std::optional<Statement> abort_assignment;
        if (abort_watched) {
            std::vector<Token> assignment
                = synthetic_tokens(abort_net + " = {", directive.span);
            for (std::size_t condition = 0;
                 condition < prepared->async_aborts.size(); ++condition) {
                const auto head = synthetic_tokens(
                    condition == 0U ? "((|(" : ", ((|(", directive.span);
                assignment.insert(assignment.end(), head.begin(), head.end());
                assignment.insert(assignment.end(),
                    prepared->async_aborts[condition].begin(),
                    prepared->async_aborts[condition].end());
                const auto tail = synthetic_tokens(")) === 1'b1)", directive.span);
                assignment.insert(assignment.end(), tail.begin(), tail.end());
            }
            const auto tail = synthetic_tokens("};", directive.span);
            assignment.insert(assignment.end(), tail.begin(), tail.end());
            abort_assignment
                = parse_generated_statement(std::move(assignment), directive.span);
            if (!abort_assignment
                || abort_assignment->kind != StatementKind::Assignment) {
                return;
            }
            abort_assignment->assignment_kind = AssignmentKind::Continuous;
        }
        std::optional<Statement> clock_statement;
        if (wakes) {
            std::vector<Token> trigger = synthetic_tokens("@(", directive.span);
            trigger.insert(trigger.end(), prepared->clock.begin(),
                prepared->clock.end());
            const auto tail = synthetic_tokens(") begin " + tick_bit + " = 1'b1; #0 "
                    + tick_bit + " = 1'b0; end",
                directive.span);
            trigger.insert(trigger.end(), tail.begin(), tail.end());
            clock_statement
                = parse_generated_statement(std::move(trigger), directive.span);
            if (!clock_statement) {
                return;
            }
        }
        // One attempt starts at the next clocking event; the statement
        // completes with the attempt (16.17).
        emit("begin int " + prefix + "_n; int " + prefix + "_o; int " + prefix
            + "_c; bit " + prefix + "_s; ");
        // The property's local variables and the subroutine calls' queue
        // index: variables of the statement's block (16.10, 16.11).
        const auto declarations
            = sva_local_declarations(*prepared, prefix, directive.span);
        body.insert(body.end(), declarations.begin(), declarations.end());
        emit(prefix + "_s = 1'b1; " + prefix + "_o = 0; while (" + prefix
            + "_o == 0) begin ");
        if (wakes) {
            emit("if (" + prefix + "_s) @(posedge " + tick_bit + "); else @(posedge "
                + tick_bit + " or " + wake_bit
                + (watched_disable ? " or posedge " + disable_net : std::string { })
                + "); ");
        } else if (watched_disable) {
            emit("if (" + prefix + "_s) @(");
            body.insert(body.end(), prepared->clock.begin(), prepared->clock.end());
            emit("); else @(");
            body.insert(body.end(), prepared->clock.begin(), prepared->clock.end());
            emit(" or posedge " + disable_net + "); ");
        } else {
            emit("@(");
            body.insert(body.end(), prepared->clock.begin(), prepared->clock.end());
            emit(") ");
        }
        emit(prefix + "_n = ");
        const auto call = sva_tick_call(*prepared, site,
            synthetic_tokens(prefix + "_s", directive.span), directive.span,
            true,
            watched_disable ? synthetic_tokens(disable_net, directive.span)
                            : std::vector<Token> { },
            wakes ? synthetic_tokens(wake_bit + ", "
                            + (abort_watched ? abort_net : std::string { "1'b0" })
                            + ", "
                            + std::to_string(semantic::sva::monitor_expect
                                | (prepared->every_step
                                        ? semantic::sva::monitor_every_step
                                        : 0U))
                            + ", " + tick_bit,
                        directive.span)
            : watched_disable
                ? synthetic_tokens("1'b0, 1'b0, "
                          + std::to_string(semantic::sva::monitor_expect)
                          + ", 1'b0",
                      directive.span)
                : std::vector<Token> { });
        body.insert(body.end(), call.begin(), call.end());
        emit("; ");
        if (!prepared->locals.empty() || !prepared->calls.empty()) {
            // The local-variable requests and the queued subroutine calls
            // read the design's sampled values (16.10, 16.11, 16.5.1), as an
            // assertion process does: the statement's procedure reads them
            // so between the two markers, and its own variables (the
            // locals) with their current values.
            emit("__fsim_act_sampledon(); ");
            const auto loop = sva_request_loop(
                *prepared, site, call, prefix, directive.span);
            body.insert(body.end(), loop.begin(), loop.end());
            const auto calls
                = sva_call_loop(*prepared, site, prefix, directive.span);
            body.insert(body.end(), calls.begin(), calls.end());
            emit("__fsim_act_sampledoff(); ");
        }
        // The pass statement runs on a nonvacuous (1) or vacuous (3)
        // success and the fail statement on a failure (2) only while the
        // assertion action controls enable them for this expect statement
        // (IEEE 1800-2023 20.11; On, Off and Kill do not affect it).
        std::string label;
        for (const auto character : directive.label) {
            if (character == '"' || character == '\\') {
                label += '\\';
            }
            label += character;
        }
        const auto control = prefix + "_c";
        emit(prefix + "_s = 1'b0; if (" + prefix + "_n != 0) " + prefix
            + "_o = $__fsim_sva_outcome(" + std::to_string(site) + "); end "
            + control + " = $__fsim_assertion_control("
            + std::to_string(semantic::sva::control_expect) + ", 0, \""
            + label + "\"); if ((" + prefix + "_o == 1 && (" + control + " & "
            + std::to_string(semantic::sva::control_pass) + ") != 0) || ("
            + prefix + "_o == 3 && (" + control + " & "
            + std::to_string(semantic::sva::control_vacuous)
            + ") != 0)) __fsim_act_success(); else if (" + prefix
            + "_o == 2 && (" + control + " & "
            + std::to_string(semantic::sva::control_fail)
            + ") != 0) __fsim_act_failure(); end");
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
                const auto marker = [&](const std::string_view text) {
                    Statement region;
                    region.kind = StatementKind::Display;
                    region.output_newline = false;
                    region.output_text = std::string { "\x1f" } + std::string { text };
                    region.span = directive.span;
                    replacement.statements = { std::move(region) };
                };
                if (item.task_name == "__fsim_act_callregion") {
                    marker("fsim.concurrent-assertion-call-region|" + prefix);
                } else if (item.task_name == "__fsim_act_sampledon") {
                    marker("fsim.concurrent-assertion-sampled-reads|on");
                } else if (item.task_name == "__fsim_act_sampledoff") {
                    marker("fsim.concurrent-assertion-sampled-reads|off");
                } else {
                    const auto& actions = item.task_name == "__fsim_act_success"
                        ? pass_actions
                        : failure_actions;
                    replacement.statements.assign(actions.begin(), actions.end());
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
        substitute(substitute, *generated);
        statement = std::move(*generated);
        const auto hidden_bit = [&](const std::string& name, const std::size_t width) {
            Type net_type;
            net_type.domain = ValueDomain::Bit2;
            net_type.spelling = "bit";
            if (width > 1U) {
                const auto msb = static_cast<std::int64_t>(width - 1U);
                net_type.packed_range = PackedRange { msb, 0, true };
                net_type.packed_range_expression = PackedRangeExpression {
                    Expression { ExpressionKind::IntegerLiteral,
                        std::to_string(msb), { }, directive.span },
                    Expression { ExpressionKind::IntegerLiteral, "0", { },
                        directive.span },
                    directive.span, std::nullopt };
            }
            signals.push_back(SignalDeclaration { name, std::move(net_type),
                PortDirection::Unknown, false, directive.span });
        };
        if (wakes) {
            hidden_bit(tick_bit, 1U);
            hidden_bit(wake_bit, 1U);
            Process clock_process;
            clock_process.kind = ProcessKind::VerilogAlways;
            clock_process.name = prefix + "_clock0";
            clock_process.span = directive.span;
            clock_process.statements.push_back(std::move(*clock_statement));
            clock_processes.push_back(std::move(clock_process));
        }
        if (abort_assignment) {
            hidden_bit(abort_net, prepared->async_aborts.size());
            concurrent_statements.push_back(std::move(*abort_assignment));
        }
        if (disable_assignment) {
            Type net_type;
            net_type.domain = ValueDomain::Bit2;
            net_type.spelling = "bit";
            signals.push_back(SignalDeclaration { disable_net,
                std::move(net_type), PortDirection::Unknown, false,
                directive.span });
            concurrent_statements.push_back(std::move(*disable_assignment));
        }
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
        local_names.clear();
        for (const auto& variable : process.variables) {
            local_names.insert(variable.name);
        }
        collect_locals(collect_locals, process.statements);
        walk(walk, process.statements);
    }
    for (auto& task : tasks) {
        local_names.clear();
        for (const auto& argument : task.arguments) {
            local_names.insert(argument.name);
        }
        for (const auto& variable : task.variables) {
            local_names.insert(variable.name);
        }
        collect_locals(collect_locals, task.statements);
        walk(walk, task.statements);
    }
    for (auto& clock_process : clock_processes) {
        processes.push_back(std::move(clock_process));
    }
}

bool VerilogParser::resolve_sva_directive(
    DesignUnit& unit, const std::size_t index)
{
    return resolve_sva_monitor(unit,
        unit.systemverilog_concurrent_assertions[index], index, nullptr);
}

bool VerilogParser::resolve_sva_monitor(DesignUnit& unit,
    const SystemVerilogConcurrentAssertion& directive, const std::size_t index,
    const SvaEndpoint* const endpoint)
{
    using namespace assertion_resolution_detail;
    const auto reject = [&](const std::string& reason) {
        sva_reject_reason_ = reason;
        if (trace_enabled()) {
            std::cerr << "fsim sva: directive " << index << " in "
                      << unit.name << " uses the earlier evaluator: " << reason
                      << '\n';
        }
        return false;
    };
    std::string reason;
    const auto prefix = endpoint != nullptr
        ? endpoint->name
        : "__fsim_sva" + std::to_string(index);
    // An end point's readers wait for its monitor's tick bit.
    const auto forced_ticks
        = std::exchange(sva_force_ticks_, endpoint != nullptr);
    const auto root_declaration = std::exchange(sva_root_declaration_,
        endpoint != nullptr
            ? &unit.systemverilog_assertion_declarations[endpoint->declaration]
            : nullptr);
    const auto prepared
        = prepare_sva_property(unit, directive, reason, prefix);
    sva_force_ticks_ = forced_ticks;
    sva_root_declaration_ = root_declaration;
    if (!prepared) {
        return reject(reason);
    }
    if (endpoint != nullptr) {
        sva_endpoints_[index].waits = prepared->endpoint_waits;
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
    if (endpoint == nullptr) {
        process.systemverilog_assertion_directive = index;
        process.name = directive.label.empty()
            ? "$assertion$" + std::to_string(index + 1U)
            : directive.label;
    } else {
        process.name = endpoint->name;
    }
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
    std::vector<Token> body;
    const auto emit = [&](const std::string_view text) {
        auto lexed = synthetic_tokens(text, directive.span);
        body.insert(body.end(), lexed.begin(), lexed.end());
    };
    // The control read names the process as a concurrent assertion of its
    // directive type to the assertion controls (IEEE 1800-2023 20.11),
    // which reach the monitor through the attempt filter and the outcome
    // pop.
    // An end point's monitor is no assertion: the assertion controls do
    // not reach it (20.11).
    const auto control_directive = endpoint != nullptr ? 0U
        : directive.kind == SystemVerilogConcurrentAssertionKind::Assert
        ? semantic::sva::control_assert
        : directive.kind == SystemVerilogConcurrentAssertionKind::Cover
        ? semantic::sva::control_cover
        : directive.kind == SystemVerilogConcurrentAssertionKind::Assume
        ? semantic::sva::control_assume
        : 0U;
    emit("begin int " + prefix + "_n; int " + prefix + "_o; int " + prefix
        + "_c; ");
    {
        const auto declarations
            = sva_local_declarations(*prepared, prefix, directive.span);
        body.insert(body.end(), declarations.begin(), declarations.end());
    }
    emit(prefix + "_c = $__fsim_assertion_control("
        + std::to_string(semantic::sva::control_concurrent) + ", "
        + std::to_string(control_directive) + ", \"\"); @(");
    std::vector<Process> clock_processes;
    // A monitor whose clocks report their ticks through hidden bits (a
    // singly clocked one that also evaluates between its clock's ticks, and
    // every multiclocked one, 16.13): each clock's process sets its tick
    // bit, the monitor waits for any bit's rise (or its wake bit), and its
    // tick reads the bits with their current values and clears them (a
    // clock ticks once in a time step).
    std::vector<std::string> tick_bits;
    if (prepared->clocks.size() == 1U) {
        tick_bits.push_back(prefix + "_ck");
    } else {
        for (std::size_t clock = 0; clock < prepared->clocks.size(); ++clock) {
            tick_bits.push_back(prefix + "_ck" + std::to_string(clock));
        }
    }
    if (prepared->clocks.empty()) {
        body.insert(body.end(), prepared->clock.begin(), prepared->clock.end());
    }
    for (std::size_t clock = 0; clock < tick_bits.size(); ++clock) {
        std::vector<Token> trigger = synthetic_tokens("@(", directive.span);
        trigger.insert(trigger.end(), prepared->clocks[clock].begin(),
            prepared->clocks[clock].end());
        const auto tail = synthetic_tokens(") " + tick_bits[clock] + " = 1'b1;",
            directive.span);
        trigger.insert(trigger.end(), tail.begin(), tail.end());
        auto statement
            = parse_generated_statement(std::move(trigger), directive.span);
        if (!statement) {
            return reject("the generated clock process does not parse");
        }
        Process clock_process;
        clock_process.kind = ProcessKind::VerilogAlways;
        clock_process.name = prefix + "_clock" + std::to_string(clock);
        clock_process.span = directive.span;
        clock_process.statements.push_back(std::move(*statement));
        clock_processes.push_back(std::move(clock_process));
        emit((clock == 0U ? "posedge " : " or posedge ") + tick_bits[clock]);
    }
    // A monitor that also evaluates at time steps without a clock tick
    // waits for its wake bit too, which the engine toggles at the start of
    // such a time step while an attempt is in progress
    // (SvaOperation::wake_signal; 16.12, 16.12.14).
    const auto wake_bit = prefix + "_wk";
    const auto abort_net = prefix + "_ab";
    const bool abort_watched
        = !prepared->every_step && !prepared->async_aborts.empty();
    if (prepared->wakes) {
        emit(" or " + wake_bit);
    }
    const auto outcome = prefix + "_o";
    // An outcome whose action block the assertion controls switched off
    // comes as its code plus 8 (sva::layout::outcome_action_off): it still
    // counts, but its pass or fail statement does not run (20.11).
    const auto outcomes = "repeat (" + prefix + "_n) begin " + outcome
        + " = $__fsim_sva_outcome(0); if (" + outcome
        + " == 1) __fsim_act_success(); else if (" + outcome
        + " == 2) __fsim_act_failure(); else if (" + outcome
        + " == 3) __fsim_act_vacuous(); else if (" + outcome
        + " == 4) __fsim_act_abort(); else if (" + outcome
        + " == 9) __fsim_act_success_off(); else if (" + outcome
        + " == 10) __fsim_act_failure_off(); else if (" + outcome
        + " == 11) __fsim_act_vacuous_off(); end";
    // The disable condition reads current values (16.12). Its truth is the
    // directive's hidden net, which a continuous assignment keeps, and the
    // tick reads the net as its disabled operand: an attempt that starts
    // while it is 1 is disabled. The engine watches the net
    // (SvaOperation::disable_signal), and each time it becomes 1 every
    // attempt in progress ends in the disabled state at once, between the
    // clock ticks too (16.12, 39.4.2). A condition with a sampled value
    // function or a sequence's triggered state changes value at the start
    // or end of a time step without an update of its operands (16.9.3,
    // 16.13.6), which a continuous assignment does not see: the tick reads
    // such a condition itself.
    // Such a condition, or one with an automatic variable, makes the
    // monitor evaluate at every time step of an attempt instead
    // (SvaPrepared::every_step), where the tick reads it.
    const auto disable_net = prefix + "_dt";
    const bool watched_disable = prepared->disable_watched;
    // The asynchronous abort conditions are checked with sampled values at
    // every time step of an attempt (16.12.14). The directive's hidden abort
    // net holds their truth, kept by a continuous assignment; the engine
    // watches it (SvaOperation::abort_signal), and after it changed wakes
    // the monitor at the start of the next time step, where the tick reads
    // the conditions' sampled values. A change and its return within one
    // time step leave the sampled values, and the verdict, unchanged.
    std::vector<Token> wake;
    if (!tick_bits.empty()) {
        // The wake bit, the abort net, the monitor flags and the tick bits.
        std::string group = (prepared->wakes ? wake_bit : std::string { "1'b0" })
            + ", " + (abort_watched ? abort_net : std::string { "1'b0" }) + ", "
            + std::to_string(prepared->every_step
                    ? semantic::sva::monitor_every_step
                    : 0U);
        for (const auto& bit : tick_bits) {
            group += ", " + bit;
        }
        wake = synthetic_tokens(group, directive.span);
    }
    emit(") begin ");
    // The end points this time step's evaluation reads are known once
    // their monitors have ticked (16.13.6).
    for (const auto& bit : prepared->endpoint_waits) {
        emit("wait ($__fsim_current(" + bit + " == 1'b0)); ");
    }
    emit(prefix + "_n = ");
    {
        // A procedural assertion starts an attempt only when its procedure
        // reached the statement (16.14.6.2); the lowering reads this
        // operand's current value.
        const auto call = sva_tick_call(*prepared, 0U,
            synthetic_tokens(directive.procedural_enable.empty()
                    ? std::string { "1'b1" }
                    : directive.procedural_enable,
                directive.span),
            directive.span, false,
            watched_disable ? synthetic_tokens(disable_net, directive.span)
                            : std::vector<Token> { },
            wake);
        body.insert(body.end(), call.begin(), call.end());
        emit("; ");
        // The local-variable requests come before the outcomes, while the
        // process still reads sampled values (16.10).
        const auto loop
            = sva_request_loop(*prepared, 0U, call, prefix, directive.span);
        body.insert(body.end(), loop.begin(), loop.end());
        const auto calls = sva_call_loop(*prepared, 0U, prefix, directive.span);
        body.insert(body.end(), calls.begin(), calls.end());
    }
    if (!directive.procedural_enable.empty()) {
        if (tick_bits.empty()) {
            emit(directive.procedural_enable + " = 1'b0; ");
        } else {
            // A monitor with tick bits also runs at the other clocks' ticks
            // and at its wakes; an attempt starts only at a tick of the
            // leading clock (16.14.6.2, 16.13), so only that one consumes
            // the procedure's enable. The monitor reads sampled values, in
            // which the leading clock's tick bit is never set: a process of
            // its own consumes the enable when the monitor clears the bit
            // after a tick of the leading clock.
            auto consume = parse_generated_statement(
                synthetic_tokens("@(negedge " + tick_bits.front() + ") "
                        + directive.procedural_enable + " = 1'b0;",
                    directive.span),
                directive.span);
            if (!consume) {
                return reject("the generated enable process does not parse");
            }
            Process consume_process;
            consume_process.kind = ProcessKind::VerilogAlways;
            consume_process.name = prefix + "_enable";
            consume_process.span = directive.span;
            consume_process.statements.push_back(std::move(*consume));
            clock_processes.push_back(std::move(consume_process));
        }
    }
    for (const auto& bit : tick_bits) {
        emit(bit + " = 1'b0; ");
    }
    emit(outcomes + " end end");
    auto statement = parse_generated_statement(std::move(body), directive.span);
    if (!statement) {
        return reject("the generated process does not parse");
    }
    // An end point's match keeps the time of the match, and of the last
    // match of an earlier time step, read with current values in the
    // Observed region (16.13.6).
    std::vector<Statement> endpoint_match;
    if (endpoint != nullptr) {
        const auto sampled_reads = [&](const std::string_view state) {
            Statement statement;
            statement.kind = StatementKind::Display;
            statement.output_newline = false;
            statement.output_text = std::string { "\x1f"
                                                  "fsim.concurrent-assertion-sampled-reads|" }
                + std::string { state };
            statement.span = directive.span;
            return statement;
        };
        auto update = parse_generated_statement(
            synthetic_tokens("if (" + prefix + "_t != $realtime + 1.0) begin "
                    + prefix + "_p = " + prefix + "_t; " + prefix
                    + "_t = $realtime + 1.0; end",
                directive.span),
            directive.span);
        if (!update) {
            return reject("the generated end-point update does not parse");
        }
        endpoint_match.push_back(sampled_reads("off"));
        endpoint_match.push_back(std::move(*update));
        endpoint_match.push_back(sampled_reads("on"));
    }
    // Replace the action placeholders.
    const auto substitute = [&](const auto& self, Statement& item) -> void {
        if (item.kind == StatementKind::TaskCall
            && item.task_name.starts_with("__fsim_act_")) {
            const auto which = item.task_name.substr(11U);
            Statement replacement;
            replacement.kind = StatementKind::Block;
            replacement.span = directive.span;
            if (endpoint != nullptr) {
                if (which == "success" || which == "success_off") {
                    replacement.statements = endpoint_match;
                }
            } else if (which == "callregion") {
                Statement region;
                region.kind = StatementKind::Display;
                region.output_newline = false;
                region.output_text = std::string { "\x1f"
                                                   "fsim.concurrent-assertion-call-region|" }
                    + process.name;
                region.span = directive.span;
                replacement.statements = { std::move(region) };
            } else if (which == "success") {
                replacement.statements = { success };
            } else if (which == "failure") {
                replacement.statements = { failure };
            } else if (which == "vacuous") {
                replacement.statements = vacuous;
            } else if (which == "success_off") {
                replacement.statements = { coverage_marker("pass") };
            } else if (which == "failure_off") {
                replacement.statements = { coverage_marker("failure") };
            } else if (which == "vacuous_off") {
                replacement.statements = { coverage_marker("vacuous") };
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
    std::optional<Statement> disable_assignment;
    if (watched_disable) {
        std::vector<Token> assignment
            = synthetic_tokens(disable_net + " = ((|(", directive.span);
        assignment.insert(assignment.end(), prepared->disable.begin(),
            prepared->disable.end());
        const auto tail = synthetic_tokens(")) === 1'b1);", directive.span);
        assignment.insert(assignment.end(), tail.begin(), tail.end());
        disable_assignment
            = parse_generated_statement(std::move(assignment), directive.span);
        if (!disable_assignment
            || disable_assignment->kind != StatementKind::Assignment) {
            return reject("the generated disable condition does not parse");
        }
        disable_assignment->assignment_kind = AssignmentKind::Continuous;
    }
    std::optional<Statement> abort_assignment;
    if (abort_watched) {
        std::vector<Token> assignment
            = synthetic_tokens(abort_net + " = {", directive.span);
        for (std::size_t condition = 0;
             condition < prepared->async_aborts.size(); ++condition) {
            const auto head = synthetic_tokens(
                condition == 0U ? "((|(" : ", ((|(", directive.span);
            assignment.insert(assignment.end(), head.begin(), head.end());
            assignment.insert(assignment.end(),
                prepared->async_aborts[condition].begin(),
                prepared->async_aborts[condition].end());
            const auto tail = synthetic_tokens(")) === 1'b1)", directive.span);
            assignment.insert(assignment.end(), tail.begin(), tail.end());
        }
        const auto tail = synthetic_tokens("};", directive.span);
        assignment.insert(assignment.end(), tail.begin(), tail.end());
        abort_assignment
            = parse_generated_statement(std::move(assignment), directive.span);
        if (!abort_assignment
            || abort_assignment->kind != StatementKind::Assignment) {
            return reject("the generated abort conditions do not parse");
        }
        abort_assignment->assignment_kind = AssignmentKind::Continuous;
    }
    if (endpoint == nullptr) {
        process.statements.push_back(coverage_marker("register"));
    }
    if (checks && endpoint == nullptr) {
        process.statements.push_back(std::move(obligation));
        process.statements.push_back(std::move(vacuous_obligation));
    }
    process.statements.push_back(std::move(*statement));
    unit.processes.push_back(std::move(process));
    for (auto& clock_process : clock_processes) {
        unit.processes.push_back(std::move(clock_process));
    }
    if (disable_assignment) {
        Type net_type;
        net_type.domain = ValueDomain::Bit2;
        net_type.spelling = "bit";
        unit.signals.push_back(SignalDeclaration { disable_net,
            std::move(net_type), PortDirection::Unknown, false,
            directive.span });
        unit.concurrent_statements.push_back(std::move(*disable_assignment));
    }
    const auto hidden_bits = [&](const std::string& name,
                                 const std::size_t width) {
        Type net_type;
        net_type.domain = ValueDomain::Bit2;
        net_type.spelling = "bit";
        if (width > 1U) {
            const auto msb = static_cast<std::int64_t>(width - 1U);
            net_type.packed_range = PackedRange { msb, 0, true };
            net_type.packed_range_expression = PackedRangeExpression {
                Expression { ExpressionKind::IntegerLiteral,
                    std::to_string(msb), { }, directive.span },
                Expression { ExpressionKind::IntegerLiteral, "0", { },
                    directive.span },
                directive.span, std::nullopt };
        }
        unit.signals.push_back(SignalDeclaration { name, std::move(net_type),
            PortDirection::Unknown, false, directive.span });
    };
    if (prepared->wakes) {
        hidden_bits(wake_bit, 1U);
    }
    for (const auto& bit : tick_bits) {
        hidden_bits(bit, 1U);
    }
    if (abort_assignment) {
        hidden_bits(abort_net, prepared->async_aborts.size());
        unit.concurrent_statements.push_back(std::move(*abort_assignment));
    }
    return true;
}

std::optional<std::vector<Token>> VerilogParser::sva_endpoint_use(
    const DesignUnit& unit, const std::vector<SvaNameScope>& scopes,
    const std::size_t origin,
    const SystemVerilogAssertionDeclaration& declaration,
    const std::span<const Token> receiver, const bool matched,
    const std::span<const Token> clock, const SourceSpan& span,
    std::string& wait_bit, std::string& failure)
{
    const auto innermost = scopes.empty() ? 0U : scopes.size() - 1U;
    // A sequence declared without a clock ends on the clock of the context
    // of the read (16.13.5).
    std::vector<Token> source_clock;
    if (!declaration.clock) {
        if (clock.empty()) {
            failure = "the sequence '" + declaration.name
                + "' of an end-point read has no clocking event";
            return std::nullopt;
        }
        source_clock.assign(clock.begin(), clock.end());
    }
    // The same end point is the same declaration with actuals and clock
    // naming the same objects.
    const auto declaration_index = static_cast<std::size_t>(
        &declaration - unit.systemverilog_assertion_declarations.data());
    auto resolved = resolve_clock(unit, scopes, receiver, origin);
    const auto clock_identity = source_clock.empty()
        ? std::string { }
        : resolve_clock(unit, scopes, source_clock, innermost).identity;
    const auto key = std::to_string(declaration_index) + " "
        + resolved.identity + " @ " + clock_identity;
    auto found = std::ranges::find(sva_endpoints_, key, &SvaEndpoint::key);
    if (found == sva_endpoints_.end()) {
        SvaEndpoint endpoint;
        endpoint.key = key;
        endpoint.receiver = std::move(resolved.tokens);
        endpoint.declaration = declaration_index;
        endpoint.clock = std::move(source_clock);
        endpoint.span = span;
        endpoint.name = "__fsim_ep" + std::to_string(sva_endpoint_counter_++);
        sva_endpoints_.push_back(std::move(endpoint));
        found = sva_endpoints_.end() - 1;
    }
    const auto name = found->name;
    if (!matched) {
        // Triggered in this time step: a match at the current time, read
        // once the monitor has ticked (16.13.6).
        wait_bit = name + "_ck";
        return synthetic_tokens(
            "$__fsim_current(" + name + "_t == $realtime + 1.0)", span);
    }
    // Matched at a tick of the destination clock: a match of an earlier
    // time step at or after the previous tick of that clock (16.13.5,
    // F.6.1).
    if (clock.empty()) {
        failure = "a matched end point read without a clocking event";
        return std::nullopt;
    }
    const auto destination_key = name + " @ "
        + resolve_clock(unit, scopes, clock, innermost).identity;
    auto destination = std::ranges::find(sva_endpoint_destinations_,
        destination_key, &SvaEndpointDestination::key);
    if (destination == sva_endpoint_destinations_.end()) {
        SvaEndpointDestination entry;
        entry.key = destination_key;
        entry.clock.assign(clock.begin(), clock.end());
        entry.span = span;
        entry.name = name + "_d"
            + std::to_string(sva_endpoint_destinations_.size());
        sva_endpoint_destinations_.push_back(std::move(entry));
        destination = sva_endpoint_destinations_.end() - 1;
    }
    const auto earlier = "((" + name + "_t < $realtime + 1.0) ? " + name
        + "_t : " + name + "_p)";
    return synthetic_tokens("$__fsim_current(" + earlier + " >= 1.0 && "
            + earlier + " >= " + destination->name + "_prev)",
        span);
}

void VerilogParser::materialize_sva_endpoints(DesignUnit& unit)
{
    // A monitor's sequence may read other end points, which register more.
    for (std::size_t index = 0; index < sva_endpoints_.size(); ++index) {
        if (sva_endpoints_[index].materialized) {
            continue;
        }
        sva_endpoints_[index].materialized = true;
        const auto endpoint = sva_endpoints_[index];
        parse_generated_items(synthetic_tokens("real " + endpoint.name
                    + "_t; real " + endpoint.name + "_p;",
                endpoint.span),
            endpoint.span, unit);
        SystemVerilogConcurrentAssertion directive;
        directive.kind = SystemVerilogConcurrentAssertionKind::Cover;
        directive.form = SystemVerilogConcurrentAssertionForm::Sequence;
        directive.span = endpoint.span;
        if (!endpoint.clock.empty()) {
            directive.property_tokens = synthetic_tokens("@(", endpoint.span);
            directive.property_tokens.insert(directive.property_tokens.end(),
                endpoint.clock.begin(), endpoint.clock.end());
            const auto close = synthetic_tokens(")", endpoint.span);
            directive.property_tokens.insert(directive.property_tokens.end(),
                close.begin(), close.end());
        }
        directive.property_tokens.insert(directive.property_tokens.end(),
            endpoint.receiver.begin(), endpoint.receiver.end());
        {
            SystemVerilogAssertionDeclaration sequence;
            sequence.kind = SystemVerilogAssertionDeclarationKind::Sequence;
            sequence.body_tokens = directive.property_tokens;
            sequence.body_span = endpoint.span;
            structure_assertion_clock_and_disable(sequence, 0U);
            structure_assertion_endpoints(sequence);
            structure_sequence_expression(sequence);
            sequence.span = endpoint.span;
            directive.inline_sequence = std::move(sequence);
        }
        const auto saved_reason = sva_reject_reason_;
        const auto saved_illegal = sva_reject_illegal_;
        if (!resolve_sva_monitor(unit, directive, index,
                &sva_endpoints_[index])) {
            error(endpoint.receiver.front(), "FSIM-SV-SEM-476",
                "the end point of sequence '" + spelling(endpoint.receiver)
                    + "' cannot be evaluated: " + sva_reject_reason_);
        }
        sva_reject_reason_ = saved_reason;
        sva_reject_illegal_ = saved_illegal;
    }
    for (auto& destination : sva_endpoint_destinations_) {
        if (destination.materialized) {
            continue;
        }
        destination.materialized = true;
        std::vector<Token> items = synthetic_tokens("real " + destination.name
                + "_prev; real " + destination.name
                + "_cur; always @(",
            destination.span);
        items.insert(items.end(), destination.clock.begin(),
            destination.clock.end());
        const auto tail = synthetic_tokens(") begin " + destination.name
                + "_prev = " + destination.name + "_cur; " + destination.name
                + "_cur = $realtime + 1.0; end",
            destination.span);
        items.insert(items.end(), tail.begin(), tail.end());
        parse_generated_items(std::move(items), destination.span, unit);
    }
    // There shall be no circular dependencies between sequences induced by
    // triggered (16.13.6).
    for (const auto& endpoint : sva_endpoints_) {
        std::vector<std::string> pending = endpoint.waits;
        std::set<std::string> seen;
        bool circular { };
        while (!pending.empty() && !circular) {
            const auto bit = pending.back();
            pending.pop_back();
            if (bit == endpoint.name + "_ck") {
                circular = true;
                break;
            }
            if (!seen.insert(bit).second) {
                continue;
            }
            for (const auto& other : sva_endpoints_) {
                if (other.name + "_ck" == bit) {
                    pending.insert(pending.end(), other.waits.begin(),
                        other.waits.end());
                }
            }
        }
        if (circular) {
            error(endpoint.receiver.front(), "FSIM-SV-SEM-476",
                "the end point of sequence '" + spelling(endpoint.receiver)
                    + "' depends on itself through triggered (16.13.6)");
        }
    }
}

void VerilogParser::resolve_procedural_sva_endpoints(DesignUnit& unit)
{
    // The procedures are the unit's: their names resolve in its innermost
    // scope.
    const auto scopes = sva_name_scopes(unit);
    const auto innermost = scopes.empty() ? 0U : scopes.size() - 1U;
    const auto sequence = [&](const std::string_view name)
        -> const SystemVerilogAssertionDeclaration* {
        const auto* const found = visible_declaration(unit, scopes, name,
            innermost, std::nullopt);
        return found != nullptr
                && found->kind == SystemVerilogAssertionDeclarationKind::Sequence
            ? found
            : nullptr;
    };
    // The clock of a sequence read outside an assertion: its own, else the
    // default clocking (16.13.6, 16.15).
    const auto endpoint_name = [&](const std::string& name,
                                   const SourceSpan& span,
                                   const std::vector<Token>* instance = nullptr)
        -> std::optional<std::string> {
        std::vector<Token> clock;
        if (unit.systemverilog_default_clocking_block) {
            Token block;
            block.kind = TokenKind::Identifier;
            block.text = *unit.systemverilog_default_clocking_block;
            block.span = span;
            clock.push_back(std::move(block));
        }
        std::string wait_bit;
        std::string failure;
        const auto receiver
            = instance != nullptr ? *instance : synthetic_tokens(name, span);
        auto replacement = sva_endpoint_use(unit, scopes, innermost,
            *sequence(name), receiver, false, clock, span, wait_bit, failure);
        if (!replacement) {
            Token location;
            location.kind = TokenKind::Identifier;
            location.text = name;
            location.span = span;
            error(location, "FSIM-SV-SEM-476",
                "the end point of sequence '" + name
                    + "' cannot be evaluated: " + failure);
            return std::nullopt;
        }
        return wait_bit.substr(0U, wait_bit.size() - 3U);
    };
    const auto rewrite_expression = [&](const auto& self,
                                        Expression& expression) -> void {
        if (expression.kind == ExpressionKind::Identifier
            && expression.text.ends_with(".triggered")) {
            const auto receiver = expression.text.substr(
                0U, expression.text.size() - std::string_view { ".triggered" }.size());
            if (receiver.find('.') == std::string::npos
                && sequence(receiver) != nullptr) {
                if (const auto name = endpoint_name(receiver, expression.span)) {
                    // Outside an assertion the triggered state reads the
                    // current values (9.4.4).
                    if (auto replaced = assertion_expression(synthetic_tokens(
                            "(" + *name + "_t == $realtime + 1.0)",
                            expression.span))) {
                        expression = std::move(*replaced);
                    }
                }
                return;
            }
        }
        // `s(actuals).triggered`: the instance's tokens come from the token
        // stream at the call's source order.
        // (`@sv-select:triggered` of the call `s(actuals)`).
        if (expression.kind == ExpressionKind::Call
            && expression.text == "@sv-select:triggered"
            && expression.operands.size() == 1U
            && expression.operands.front().kind == ExpressionKind::Call
            && (expression.systemverilog_source_order != 0U
                || expression.operands.front().systemverilog_source_order != 0U)) {
            const auto receiver = expression.operands.front().text;
            const auto first = static_cast<std::size_t>(
                (expression.systemverilog_source_order != 0U
                        ? expression.systemverilog_source_order
                        : expression.operands.front().systemverilog_source_order)
                - 1U);
            if (receiver.find('.') == std::string::npos
                && sequence(receiver) != nullptr && first + 1U < tokens_.size()
                && tokens_[first].kind == TokenKind::Identifier
                && tokens_[first].text == receiver
                && tokens_[first].span.begin == expression.span.begin
                && tokens_[first + 1U].kind == TokenKind::LeftParen) {
                std::vector<Token> instance { tokens_[first] };
                std::size_t depth { };
                for (auto index = first + 1U; index < tokens_.size(); ++index) {
                    instance.push_back(tokens_[index]);
                    if (tokens_[index].kind == TokenKind::LeftParen) {
                        ++depth;
                    } else if (tokens_[index].kind == TokenKind::RightParen
                        && --depth == 0U) {
                        break;
                    }
                }
                if (depth == 0U) {
                    if (const auto name
                        = endpoint_name(receiver, expression.span, &instance)) {
                        if (auto replaced = assertion_expression(synthetic_tokens(
                                "(" + *name + "_t == $realtime + 1.0)",
                                expression.span))) {
                            expression = std::move(*replaced);
                        }
                    }
                    return;
                }
            }
        }
        for (auto& operand : expression.operands) {
            self(self, operand);
        }
    };
    const auto rewrite_statement = [&](const auto& self,
                                       Statement& statement) -> void {
        for (auto* expression : { &statement.target, &statement.value,
                 &statement.condition, &statement.loop_initial,
                 &statement.loop_limit }) {
            rewrite_expression(rewrite_expression, *expression);
        }
        for (auto& argument : statement.task_arguments) {
            rewrite_expression(rewrite_expression, argument);
        }
        for (auto& argument : statement.output_arguments) {
            rewrite_expression(rewrite_expression, argument);
        }
        // `@s` waits for an end point of s (9.4.2.4): a change of the time
        // of its last match.
        for (auto& sensitivity : statement.sensitivities) {
            // `@(s(actuals))`: an instance whose actuals are no expressions
            // (kept as its name), or a call of a name that is a sequence.
            const auto stashed = sva_event_instances_.find(
                sensitivity.signal.empty()
                    ? sensitivity.expression.systemverilog_source_order
                    : sensitivity.systemverilog_source_order);
            if (stashed != sva_event_instances_.end()
                && sensitivity.edge == EdgeKind::Any
                && (stashed->second.sequence_only
                        ? sensitivity.signal == stashed->second.tokens.front().text
                        : (sensitivity.expression.kind == ExpressionKind::Call
                              && sensitivity.expression.text
                                  == stashed->second.tokens.front().text))) {
                const auto& tokens = stashed->second.tokens;
                if (sequence(tokens.front().text) != nullptr) {
                    if (const auto name = endpoint_name(
                            tokens.front().text, sensitivity.span, &tokens)) {
                        sensitivity.signal = *name + "_t";
                        sensitivity.expression = {};
                    }
                    continue;
                }
                if (stashed->second.sequence_only) {
                    error(tokens.front(), "FSIM-SV-SEM-476",
                        "the event expression '" + tokens.front().text
                            + "(...)' has actuals that are no expressions, and '"
                            + tokens.front().text
                            + "' names no sequence (9.4.2.4, 16.8)");
                    continue;
                }
            }
            if (!sensitivity.signal.empty()
                && sensitivity.edge == EdgeKind::Any
                && sequence(sensitivity.signal) != nullptr) {
                if (const auto name
                    = endpoint_name(sensitivity.signal, sensitivity.span)) {
                    sensitivity.signal = *name + "_t";
                }
            }
            rewrite_expression(rewrite_expression, sensitivity.expression);
        }
        for (auto& child : statement.statements) {
            self(self, child);
        }
        for (auto& child : statement.else_statements) {
            self(self, child);
        }
        for (auto& child : statement.loop_updates) {
            self(self, child);
        }
    };
    for (auto& process : unit.processes) {
        for (auto& statement : process.statements) {
            rewrite_statement(rewrite_statement, statement);
        }
    }
    for (auto& task : unit.tasks) {
        for (auto& statement : task.statements) {
            rewrite_statement(rewrite_statement, statement);
        }
    }
    for (auto& function : unit.functions) {
        for (auto& statement : function.statements) {
            rewrite_statement(rewrite_statement, statement);
        }
    }
    for (auto& statement : unit.concurrent_statements) {
        rewrite_statement(rewrite_statement, statement);
    }
    if (sva_procedural_body_ != nullptr) {
        auto& body = *sva_procedural_body_;
        for (auto& process : body.processes) {
            for (auto& statement : process.statements) {
                rewrite_statement(rewrite_statement, statement);
            }
        }
        for (auto& task : body.tasks) {
            for (auto& statement : task.statements) {
                rewrite_statement(rewrite_statement, statement);
            }
        }
        for (auto& function : body.functions) {
            for (auto& statement : function.statements) {
                rewrite_statement(rewrite_statement, statement);
            }
        }
        for (auto& statement : body.concurrent_statements) {
            rewrite_statement(rewrite_statement, statement);
        }
    }
}

void VerilogParser::check_sequence_dependencies(const DesignUnit& unit)
{
    const auto scopes = sva_name_scopes(unit);
    const auto innermost = scopes.empty() ? 0U : scopes.size() - 1U;
    const auto& declarations = unit.systemverilog_assertion_declarations;
    const auto level_of = [&](const std::size_t index) {
        for (std::size_t level = 0; level < scopes.size(); ++level) {
            if (index < scopes[level].declarations_end) {
                return level;
            }
        }
        return innermost;
    };
    std::map<const SystemVerilogAssertionDeclaration*,
        std::set<const SystemVerilogAssertionDeclaration*>>
        edges;
    // The sequences the tokens of `owner` (none for an assertion
    // statement), written in scope `origin`, instance; each instance's
    // actuals add the edges of 16.8.
    const auto scan = [&](const auto& self, const std::span<const Token> tokens,
                          const SystemVerilogAssertionDeclaration* const owner,
                          const std::size_t origin,
                          std::set<const SystemVerilogAssertionDeclaration*>& found)
        -> void {
        const auto own_name = [&](const std::string_view name) {
            return owner != nullptr
                && (std::ranges::any_of(owner->formals,
                        [&](const auto& formal) { return formal.name == name; })
                    || std::ranges::any_of(owner->local_variables,
                        [&](const auto& local) { return local.name == name; }));
        };
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            const auto& token = tokens[index];
            if (token.kind != TokenKind::Identifier
                || (index > 0U
                    && (tokens[index - 1U].kind == TokenKind::Dot
                        || tokens[index - 1U].kind == TokenKind::Scope))
                || own_name(token.text)) {
                continue;
            }
            const auto* const named = visible_declaration(unit, scopes,
                token.text, origin, std::nullopt);
            if (named == nullptr
                || named->kind != SystemVerilogAssertionDeclarationKind::Sequence) {
                continue;
            }
            found.insert(named);
            if (index + 1U >= tokens.size()
                || tokens[index + 1U].kind != TokenKind::LeftParen) {
                continue;
            }
            std::size_t depth { };
            auto close = index + 1U;
            for (; close < tokens.size(); ++close) {
                if (tokens[close].kind == TokenKind::LeftParen) {
                    ++depth;
                } else if (tokens[close].kind == TokenKind::RightParen
                    && --depth == 0U) {
                    break;
                }
            }
            if (close >= tokens.size()) {
                continue;
            }
            std::set<const SystemVerilogAssertionDeclaration*> in_actuals;
            self(self,
                tokens.subspan(index + 2U, close - index - 2U), owner, origin,
                in_actuals);
            edges[named].insert(in_actuals.begin(), in_actuals.end());
            found.insert(in_actuals.begin(), in_actuals.end());
            index = close;
        }
    };
    for (std::size_t index = 0; index < declarations.size(); ++index) {
        const auto& declaration = declarations[index];
        if (declaration.kind == SystemVerilogAssertionDeclarationKind::Checker) {
            continue;
        }
        const auto origin = level_of(index);
        std::set<const SystemVerilogAssertionDeclaration*> found;
        scan(scan, declaration.expression_tokens, &declaration, origin, found);
        if (declaration.clock) {
            scan(scan, declaration.clock->event_tokens, &declaration, origin,
                found);
        }
        for (const auto& formal : declaration.formals) {
            scan(scan, formal.default_tokens, &declaration, origin, found);
        }
        if (declaration.kind == SystemVerilogAssertionDeclarationKind::Sequence) {
            edges[&declaration].insert(found.begin(), found.end());
        }
    }
    for (const auto& directive : unit.systemverilog_concurrent_assertions) {
        std::set<const SystemVerilogAssertionDeclaration*> found;
        scan(scan, directive.property_tokens, nullptr, innermost, found);
    }
    for (const auto& [from, targets] : edges) {
        std::vector<const SystemVerilogAssertionDeclaration*> pending {
            targets.begin(), targets.end() };
        std::set<const SystemVerilogAssertionDeclaration*> visited;
        bool cyclic { };
        while (!pending.empty() && !cyclic) {
            const auto* const current = pending.back();
            pending.pop_back();
            if (current == from) {
                cyclic = true;
            } else if (visited.insert(current).second) {
                const auto next = edges.find(current);
                if (next != edges.end()) {
                    pending.insert(pending.end(), next->second.begin(),
                        next->second.end());
                }
            }
        }
        if (!cyclic
            || !sva_cyclic_sequences_reported_
                    .emplace(from->name, from->name_span.begin.line,
                        from->name_span.begin.offset)
                    .second) {
            continue;
        }
        Token location;
        location.kind = TokenKind::Identifier;
        location.text = from->name;
        location.span = from->name_span;
        error(location, "FSIM-SV-SEM-477",
            "the named sequence '" + from->name
                + "' depends on itself through its instances: a cyclic "
                  "dependency among named sequences (16.8)");
    }
}

namespace {

// The minimal length in ticks of sequence node `index` (16.7, 16.9); none
// when a bound is not a literal. A named sequence instance counts as one
// tick.
[[nodiscard]] std::optional<std::int64_t> minimal_length(
    const std::vector<Node>& nodes, const std::size_t index)
{
    const auto& node = nodes[index];
    const auto length = [&](const std::size_t position) {
        return position < node.operands.size()
            ? minimal_length(nodes, node.operands[position])
            : std::optional<std::int64_t> { };
    };
    const auto low = [&]() -> std::optional<std::int64_t> {
        if (node.lo.kind != BoundSpec::Kind::literal) {
            return std::nullopt;
        }
        return node.lo.value;
    };
    switch (node.kind) {
    case NodeKind::boolean:
    case NodeKind::instance:
        return 1;
    case NodeKind::delay: {
        const auto delay = low();
        if (!delay) {
            return std::nullopt;
        }
        if (node.leading) {
            const auto right = length(0);
            return right ? std::optional { *delay + *right } : std::nullopt;
        }
        const auto left = length(0);
        const auto right = length(1);
        return left && right ? std::optional { *left + *delay + *right - 1 }
                             : std::nullopt;
    }
    case NodeKind::repeat: {
        const auto count = low();
        const auto operand = length(0);
        return count && operand ? std::optional { *count * *operand }
                                : std::nullopt;
    }
    case NodeKind::goto_repeat:
    case NodeKind::nonconsecutive_repeat:
        return low();
    case NodeKind::sequence_or: {
        const auto left = length(0);
        const auto right = length(1);
        return left && right ? std::optional { std::min(*left, *right) }
                             : std::nullopt;
    }
    case NodeKind::sequence_and:
    case NodeKind::intersect: {
        const auto left = length(0);
        const auto right = length(1);
        return left && right ? std::optional { std::max(*left, *right) }
                             : std::nullopt;
    }
    case NodeKind::within:
        return length(1);
    case NodeKind::throughout:
    case NodeKind::first_match:
    case NodeKind::clocked:
    case NodeKind::match_items:
    case NodeKind::strong:
    case NodeKind::weak:
        return length(0);
    default:
        return std::nullopt;
    }
}

std::vector<PropertyInstanceFact> property_instance_facts(
    const DesignUnit& unit, const ParseContext& context,
    const std::vector<Node>& nodes, const std::size_t root,
    const SystemVerilogAssertionDeclaration& declaration)
{
    std::vector<PropertyInstanceFact> facts;
    const auto is_formal = [&](const std::string_view name) {
        return std::ranges::any_of(declaration.formals,
            [&](const SystemVerilogAssertionFormal& formal) {
                return formal.name == name;
            });
    };
    // Restriction 4 for one actual of an instance of `callee` bound to its
    // formal `formal`.
    const auto actual_allowed = [&](std::span<const Token> actual,
                                    const SystemVerilogAssertionFormal* formal) {
        if (formal != nullptr && formal->local) {
            return true;
        }
        while (actual.size() >= 3U && actual.front().kind == TokenKind::LeftParen
            && actual.back().kind == TokenKind::RightParen) {
            actual = actual.subspan(1U, actual.size() - 2U);
        }
        if (actual.size() == 1U && actual.front().kind == TokenKind::Identifier
            && is_formal(actual.front().text)) {
            return true;
        }
        return std::ranges::none_of(actual, [&](const Token& token) {
            return token.kind == TokenKind::Identifier && is_formal(token.text);
        });
    };
    // The property instances the tokens of an actual argument hold, at the
    // weight of the instance they are an actual of (F.7).
    const auto actual_instances = [&](const std::span<const Token> tokens,
                                      const std::optional<std::int64_t> weight,
                                      const bool restricted) {
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            const auto& token = tokens[index];
            if (token.kind != TokenKind::Identifier
                || (index > 0U
                    && (tokens[index - 1U].kind == TokenKind::Dot
                        || tokens[index - 1U].kind == TokenKind::Scope))
                || is_formal(token.text)) {
                continue;
            }
            const auto* const named = visible_declaration(unit, context.scopes,
                token.text, context.origin, std::nullopt);
            if (named == nullptr
                || named->kind != SystemVerilogAssertionDeclarationKind::Property) {
                continue;
            }
            facts.push_back({ named, weight, restricted, true });
        }
    };
    const auto add_weight = [](const std::optional<std::int64_t> weight,
                                const std::optional<std::int64_t> step) {
        return weight && step ? std::optional { *weight + std::max<std::int64_t>(*step, 0) }
                              : std::optional<std::int64_t> { };
    };
    const auto walk = [&](const auto& self, const std::size_t index,
                          const std::optional<std::int64_t> weight,
                          const bool restricted) -> void {
        const auto& node = nodes[index];
        const auto operands = [&](const std::optional<std::int64_t> at,
                                  const bool under) {
            for (const auto operand : node.operands) {
                self(self, operand, at, under);
            }
        };
        switch (node.kind) {
        case NodeKind::instance: {
            if (node.declared == nullptr || node.sequence_instance) {
                return;
            }
            const auto& callee = *node.declared;
            bool allowed = true;
            std::size_t positional { };
            for (const auto& actual : node.actual_tokens) {
                const SystemVerilogAssertionFormal* formal { };
                std::span<const Token> value { actual };
                if (actual.size() >= 4U && actual[0].kind == TokenKind::Dot
                    && actual[1].kind == TokenKind::Identifier
                    && actual[2].kind == TokenKind::LeftParen
                    && actual.back().kind == TokenKind::RightParen) {
                    const auto found = std::ranges::find(callee.formals,
                        actual[1].text, &SystemVerilogAssertionFormal::name);
                    formal = found == callee.formals.end() ? nullptr : &*found;
                    value = value.subspan(3U, actual.size() - 4U);
                } else if (positional < callee.formals.size()) {
                    formal = &callee.formals[positional++];
                }
                allowed = allowed && actual_allowed(value, formal);
                actual_instances(value, weight, restricted);
            }
            facts.push_back({ &callee, weight, restricted, allowed });
            return;
        }
        case NodeKind::implication:
        case NodeKind::followed_by: {
            const auto length = minimal_length(nodes, node.operands[0]);
            self(self, node.operands[1],
                add_weight(weight,
                    length ? std::optional { *length - (node.overlapped ? 1 : 0) }
                           : std::nullopt),
                restricted);
            return;
        }
        case NodeKind::nexttime:
        case NodeKind::always:
        case NodeKind::eventually:
            operands(add_weight(weight,
                         node.lo.kind == BoundSpec::Kind::literal
                             ? std::optional { node.lo.value }
                             : std::nullopt),
                restricted || node.strong);
            return;
        case NodeKind::until:
        case NodeKind::negation:
            operands(weight,
                restricted || node.kind == NodeKind::negation || node.strong);
            return;
        default:
            if (sequence_kind(node.kind)) {
                return;
            }
            operands(weight, restricted);
            return;
        }
    };
    walk(walk, root, std::int64_t { 0 }, false);
    return facts;
}

} // namespace

void VerilogParser::check_recursive_properties(const DesignUnit& unit)
{
    std::set<SvaDeclarationKey> declared;
    for (const auto& declaration : unit.systemverilog_assertion_declarations) {
        if (declaration.kind == SystemVerilogAssertionDeclarationKind::Property) {
            declared.emplace(declaration.name, declaration.name_span.begin.line,
                declaration.name_span.begin.offset);
        }
    }
    // Whether `to` is reached from `from` in the dependency digraph (F.7),
    // through edges of zero weight only when `zero`.
    const auto reaches = [&](const SvaDeclarationKey& from,
                             const SvaDeclarationKey& to, const bool zero) {
        std::vector<SvaDeclarationKey> pending { from };
        std::set<SvaDeclarationKey> visited;
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            const auto facts = sva_property_facts_.find(current);
            if (facts == sva_property_facts_.end()) {
                continue;
            }
            for (const auto& instance : facts->second.instances) {
                if (zero && (!instance.weight || *instance.weight > 0)) {
                    continue;
                }
                if (instance.callee == to) {
                    return true;
                }
                if (visited.insert(instance.callee).second) {
                    pending.push_back(instance.callee);
                }
            }
        }
        return false;
    };
    const auto recursive = [&](const SvaDeclarationKey& key) {
        return reaches(key, key, false);
    };
    for (const auto& key : declared) {
        const auto facts = sva_property_facts_.find(key);
        if (facts == sva_property_facts_.end()
            || sva_illegal_recursive_.contains(key)) {
            continue;
        }
        const auto& fact = facts->second;
        std::string reason;
        const bool self = recursive(key);
        if (self && fact.disable) {
            reason = "it has a disable iff (16.12.17 Restriction 2)";
        }
        for (const auto& instance : fact.instances) {
            if (!reason.empty()) {
                break;
            }
            const bool reaches_recursion = recursive(instance.callee)
                || std::ranges::any_of(sva_property_facts_,
                    [&](const auto& entry) {
                        return reaches(instance.callee, entry.first, false)
                            && recursive(entry.first);
                    });
            if (instance.restricted && reaches_recursion) {
                reason = "not or a strong operator applies to its instance of '"
                    + instance.callee_name
                    + "', from which a recursive property is reached "
                      "(16.12.17 Restriction 1)";
            } else if (self && reaches(instance.callee, key, false)
                && !instance.formal_actuals) {
                reason = "an actual argument of its recursive instance of '"
                    + instance.callee_name
                    + "' is not a formal argument, reads a formal argument, "
                      "and is not bound to a local variable formal argument "
                      "(16.12.17 Restriction 4)";
            }
        }
        // Every cycle through it has a positive weight (Restriction 3).
        if (reason.empty() && self && reaches(key, key, true)) {
            reason = "one of its recursive instances does not follow a "
                     "positive advance in time (16.12.17 Restriction 3)";
        }
        if (reason.empty()) {
            continue;
        }
        sva_illegal_recursive_.insert(key);
        Token location;
        location.kind = TokenKind::Identifier;
        location.text = fact.name;
        location.span = fact.name_span;
        error(location, "FSIM-SV-SEM-477",
            "the property '" + fact.name + "' is an illegal recursive "
                "property: " + reason);
    }
}

} // namespace fsim::frontend
