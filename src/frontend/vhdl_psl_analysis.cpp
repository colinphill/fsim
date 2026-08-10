// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

#include <charconv>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace fsim::frontend {
namespace {

    [[nodiscard]] std::string canonical_name(const std::string_view text)
    {
        if (text.size() >= 2U && text.front() == '\\' && text.back() == '\\') {
            return std::string { text };
        }
        std::string result;
        result.reserve(text.size());
        for (const auto character : text) {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
        return result;
    }

    [[nodiscard]] Token token_for(const SourceSpan& span)
    {
        return Token { TokenKind::Identifier, { }, span, { } };
    }

    [[nodiscard]] SourceSpan tokens_span(const std::vector<Token>& tokens)
    {
        return tokens.empty() ? SourceSpan { }
                              : cover(tokens.front().span, tokens.back().span);
    }

    [[nodiscard]] std::string clock_identity(const std::vector<Token>& tokens)
    {
        if (tokens.size() == 4U
            && (canonical_name(tokens[0].text) == "rising_edge"
                || canonical_name(tokens[0].text) == "falling_edge")
            && tokens[1].kind == TokenKind::LeftParen
            && tokens[2].kind == TokenKind::Identifier
            && tokens[3].kind == TokenKind::RightParen) {
            return std::string {
                canonical_name(tokens[0].text) == "rising_edge" ? "rise:" : "fall:"
            }
            + canonical_name(tokens[2].text);
        }
        if (tokens.size() >= 7U && tokens[0].kind == TokenKind::Identifier
            && tokens[1].kind == TokenKind::Apostrophe
            && canonical_name(tokens[2].text) == "event"
            && canonical_name(tokens[3].text) == "and") {
            const auto signal = canonical_name(tokens[0].text);
            for (std::size_t position = 4U; position + 2U < tokens.size(); ++position) {
                if (canonical_name(tokens[position].text) == signal
                    && tokens[position + 1U].kind == TokenKind::Assign
                    && tokens[position + 2U].kind == TokenKind::CharacterLiteral) {
                    if (tokens[position + 2U].text == "'1'") {
                        return "rise:" + signal;
                    }
                    if (tokens[position + 2U].text == "'0'") {
                        return "fall:" + signal;
                    }
                }
            }
        }
        std::string result;
        for (const auto& token : tokens) {
            if (!result.empty()) {
                result.push_back(' ');
            }
            result += canonical_name(token.text);
        }
        return result;
    }

    [[nodiscard]] bool opening_delimiter(const TokenKind kind)
    {
        return kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket
            || kind == TokenKind::LeftBrace;
    }

    [[nodiscard]] std::optional<std::size_t> matching_delimiter(
        const std::vector<Token>& tokens, const std::size_t left)
    {
        if (left >= tokens.size() || !opening_delimiter(tokens[left].kind)) {
            return std::nullopt;
        }
        const auto right_kind = tokens[left].kind == TokenKind::LeftParen
            ? TokenKind::RightParen
            : tokens[left].kind == TokenKind::LeftBracket
            ? TokenKind::RightBracket
            : TokenKind::RightBrace;
        std::size_t depth { };
        for (std::size_t position = left; position < tokens.size(); ++position) {
            if (tokens[position].kind == tokens[left].kind) {
                ++depth;
            } else if (tokens[position].kind == right_kind && --depth == 0U) {
                return position;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool top_level_at(
        const std::vector<Token>& tokens, const std::size_t target)
    {
        std::size_t parentheses { };
        std::size_t brackets { };
        std::size_t braces { };
        for (std::size_t position = 0; position < target; ++position) {
            switch (tokens[position].kind) {
            case TokenKind::LeftParen:
                ++parentheses;
                break;
            case TokenKind::RightParen:
                if (parentheses != 0U) {
                    --parentheses;
                }
                break;
            case TokenKind::LeftBracket:
                ++brackets;
                break;
            case TokenKind::RightBracket:
                if (brackets != 0U) {
                    --brackets;
                }
                break;
            case TokenKind::LeftBrace:
                ++braces;
                break;
            case TokenKind::RightBrace:
                if (braces != 0U) {
                    --braces;
                }
                break;
            default:
                break;
            }
        }
        return parentheses == 0U && brackets == 0U && braces == 0U;
    }

    [[nodiscard]] std::vector<Token> token_slice(
        const std::vector<Token>& tokens, const std::size_t begin,
        const std::size_t end)
    {
        if (begin >= end || end > tokens.size()) {
            return { };
        }
        return { tokens.begin() + static_cast<std::ptrdiff_t>(begin),
            tokens.begin() + static_cast<std::ptrdiff_t>(end) };
    }

    [[nodiscard]] std::optional<std::uint64_t> decimal_value(
        const std::vector<Token>& tokens)
    {
        if (tokens.size() != 1U || tokens.front().kind != TokenKind::Number) {
            return std::nullopt;
        }
        std::string spelling;
        for (const auto character : tokens.front().text) {
            if (character != '_') {
                spelling.push_back(character);
            }
        }
        std::uint64_t value { };
        const auto conversion = std::from_chars(
            spelling.data(), spelling.data() + spelling.size(), value);
        if (conversion.ec != std::errc { }
            || conversion.ptr != spelling.data() + spelling.size()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] bool sampled_scalar_type(const Type& type)
    {
        if (type.spelling == "boolean" || type.spelling == "bit"
            || type.spelling == "std_logic" || type.spelling == "std_ulogic") {
            return true;
        }
        return type.width() == 1U;
    }

    [[nodiscard]] VhdlPslExpressionClass declaration_class(
        const VhdlPslDeclarationKind kind)
    {
        switch (kind) {
        case VhdlPslDeclarationKind::DefaultClock:
        case VhdlPslDeclarationKind::Boolean:
            return VhdlPslExpressionClass::Boolean;
        case VhdlPslDeclarationKind::Sequence:
            return VhdlPslExpressionClass::Sequence;
        case VhdlPslDeclarationKind::Property:
            return VhdlPslExpressionClass::Property;
        case VhdlPslDeclarationKind::Endpoint:
            return VhdlPslExpressionClass::Endpoint;
        }
        return VhdlPslExpressionClass::Invalid;
    }

    [[nodiscard]] bool temporal_class(const VhdlPslExpressionClass kind)
    {
        return kind == VhdlPslExpressionClass::Sequence
            || kind == VhdlPslExpressionClass::Property
            || kind == VhdlPslExpressionClass::Endpoint;
    }

    void classify_formal(VhdlPslFormal& formal)
    {
        for (const auto& token : formal.profile_tokens) {
            if (token.kind != TokenKind::Identifier) {
                continue;
            }
            const auto type = canonical_name(token.text);
            if (type == "boolean" || type == "bit" || type == "std_logic"
                || type == "std_ulogic") {
                formal.expression_class = VhdlPslExpressionClass::Boolean;
            } else if (type == "sequence") {
                formal.expression_class = VhdlPslExpressionClass::Sequence;
            } else if (type == "property") {
                formal.expression_class = VhdlPslExpressionClass::Property;
            } else if (type == "natural" || type == "positive"
                || type == "integer") {
                formal.expression_class = VhdlPslExpressionClass::StaticInteger;
            }
            if (formal.expression_class != VhdlPslExpressionClass::Invalid) {
                break;
            }
        }
        const auto assignment = std::ranges::find(
            formal.profile_tokens, TokenKind::ColonEqual, &Token::kind);
        if (formal.expression_class == VhdlPslExpressionClass::StaticInteger
            && assignment != formal.profile_tokens.end()) {
            const auto position = static_cast<std::size_t>(
                assignment - formal.profile_tokens.begin());
            formal.static_default = decimal_value(token_slice(
                formal.profile_tokens, position + 1U,
                formal.profile_tokens.size()));
        }
    }

    [[nodiscard]] bool reserved_word(const std::string_view text)
    {
        static const std::unordered_set<std::string> words {
            "abort",
            "always",
            "and",
            "async_abort",
            "before",
            "eventually",
            "false",
            "fell",
            "for",
            "inf",
            "in",
            "never",
            "next",
            "next_a",
            "next_e",
            "next_event",
            "not",
            "or",
            "prev",
            "property",
            "rising_edge",
            "falling_edge",
            "rose",
            "stable",
            "strong",
            "sync_abort",
            "true",
            "until",
            "within",
            "weak",
            "xor",
        };
        return words.contains(canonical_name(text));
    }

    struct ExpressionContext {
        const ParsedDesign& design;
        const DesignUnit& object_scope;
        const std::unordered_map<std::string, VhdlPslDeclaration*>& declarations;
        const std::vector<VhdlPslFormal>* formals { };
    };

    [[nodiscard]] const VhdlPslFormal* find_formal(
        const ExpressionContext& context, const std::string_view name)
    {
        if (!context.formals) {
            return nullptr;
        }
        const auto found = std::ranges::find(
            *context.formals, name, &VhdlPslFormal::name);
        return found == context.formals->end() ? nullptr : &*found;
    }

    [[nodiscard]] const SignalDeclaration* find_signal(
        const ExpressionContext& context, const std::string_view name)
    {
        const auto matches = [&](const SignalDeclaration& signal) {
            return signal.name == name;
        };
        const auto search = [&](const DesignUnit& unit)
            -> const SignalDeclaration* {
            if (const auto found = std::ranges::find_if(unit.signals, matches);
                found != unit.signals.end()) {
                return &*found;
            }
            if (const auto found = std::ranges::find_if(unit.ports, matches);
                found != unit.ports.end()) {
                return &*found;
            }
            return nullptr;
        };
        if (const auto* direct = search(context.object_scope)) {
            return direct;
        }
        for (const auto& unit : context.design.units) {
            const auto associated = context.object_scope.kind == UnitKind::VhdlArchitecture
                && unit.kind == UnitKind::VhdlEntity
                && unit.name == context.object_scope.primary_name;
            const auto target_architecture = context.object_scope.kind == UnitKind::VhdlEntity
                && unit.kind == UnitKind::VhdlArchitecture
                && unit.primary_name == context.object_scope.name;
            if (associated || target_architecture) {
                if (const auto* found = search(unit)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] const ParameterDeclaration* find_parameter(
        const ExpressionContext& context, const std::string_view name)
    {
        const auto search = [&](const DesignUnit& unit)
            -> const ParameterDeclaration* {
            const auto found = std::ranges::find(
                unit.parameters, name, &ParameterDeclaration::name);
            return found == unit.parameters.end() ? nullptr : &*found;
        };
        if (const auto* direct = search(context.object_scope)) {
            return direct;
        }
        if (context.object_scope.kind == UnitKind::VhdlArchitecture) {
            const auto entity = std::ranges::find_if(
                context.design.units, [&](const DesignUnit& unit) {
                    return unit.kind == UnitKind::VhdlEntity
                        && unit.name == context.object_scope.primary_name;
                });
            return entity == context.design.units.end() ? nullptr : search(*entity);
        }
        return nullptr;
    }

} // namespace

void VhdlParser::analyze_vhdl_psl(ParsedDesign& design)
{
    std::unordered_map<const DesignUnit*, std::optional<VhdlPslClock>> defaults;
    std::unordered_map<const DesignUnit*, const DesignUnit*> object_scopes;

    const auto find_target = [&](const DesignUnit& verification)
        -> const DesignUnit* {
        if (!verification.vhdl_psl_verification_unit
            || verification.vhdl_psl_verification_unit->target_tokens.empty()) {
            return &verification;
        }
        const auto target = std::ranges::find_if(
            verification.vhdl_psl_verification_unit->target_tokens,
            [](const Token& token) {
                return token.kind == TokenKind::Identifier;
            });
        if (target
            == verification.vhdl_psl_verification_unit->target_tokens.end()) {
            return &verification;
        }
        const auto name = canonical_name(target->text);
        const auto found = std::ranges::find_if(
            design.units, [&](const DesignUnit& unit) {
                return unit.kind != UnitKind::VhdlPslVerificationUnit
                    && (unit.name == name || unit.primary_name == name);
            });
        return found == design.units.end() ? &verification : &*found;
    };

    for (auto& unit : design.units) {
        object_scopes[&unit] = unit.kind == UnitKind::VhdlPslVerificationUnit
            ? find_target(unit)
            : &unit;
        std::optional<VhdlPslClock> clock;
        const auto found = std::ranges::find_if(
            unit.vhdl_psl_declarations, [](const VhdlPslDeclaration& declaration) {
                return declaration.kind == VhdlPslDeclarationKind::DefaultClock;
            });
        if (found != unit.vhdl_psl_declarations.end()
            && !found->body_tokens.empty()) {
            clock = VhdlPslClock { found->body_tokens,
                clock_identity(found->body_tokens), false, true,
                tokens_span(found->body_tokens) };
        }
        defaults[&unit] = std::move(clock);
    }

    const auto inherited_default = [&](const DesignUnit& unit)
        -> std::optional<VhdlPslClock> {
        if (defaults[&unit]) {
            return defaults[&unit];
        }
        const auto* scope = object_scopes[&unit];
        if (scope && defaults[scope]) {
            return defaults[scope];
        }
        if (scope && scope->kind == UnitKind::VhdlEntity) {
            const auto architecture = std::ranges::find_if(
                design.units, [&](const DesignUnit& candidate) {
                    return candidate.kind == UnitKind::VhdlArchitecture
                        && candidate.primary_name == scope->name
                        && defaults[&candidate].has_value();
                });
            if (architecture != design.units.end()) {
                return defaults[&*architecture];
            }
        }
        return std::nullopt;
    };

    const auto static_range = [&](const ExpressionContext& context,
                                  const std::vector<Token>& tokens,
                                  const Token& marker)
        -> std::optional<VhdlPslStaticRange> {
        if (tokens.empty()) {
            VhdlPslStaticRange range;
            range.minimum = 0U;
            range.unbounded = true;
            range.span = marker.span;
            return range;
        }
        struct Bound {
            std::optional<std::uint64_t> value;
            std::string formal;
        };
        const auto resolve_bound = [&](const std::vector<Token>& bound_tokens)
            -> std::optional<Bound> {
            if (const auto value = decimal_value(bound_tokens)) {
                return Bound { *value, { } };
            }
            if (bound_tokens.size() == 1U
                && bound_tokens.front().kind == TokenKind::Identifier) {
                const auto name = canonical_name(bound_tokens.front().text);
                if (const auto* formal = find_formal(context, name);
                    formal && formal->expression_class == VhdlPslExpressionClass::StaticInteger) {
                    return Bound { formal->static_default, name };
                }
            }
            return std::nullopt;
        };
        const auto colon = std::ranges::find(tokens, TokenKind::Colon,
            &Token::kind);
        const auto separator = colon == tokens.end()
            ? tokens.size()
            : static_cast<std::size_t>(colon - tokens.begin());
        const auto minimum = resolve_bound(token_slice(tokens, 0U, separator));
        if (!minimum) {
            error(marker, "FSIM-VHDL-PSL-014",
                "a PSL temporal bound must be a locally static nonnegative integer");
            return std::nullopt;
        }
        VhdlPslStaticRange range;
        range.minimum = minimum->value;
        range.minimum_formal = minimum->formal;
        range.span = tokens_span(tokens);
        if (separator == tokens.size()) {
            range.maximum = minimum->value;
            range.maximum_formal = minimum->formal;
            return range;
        }
        const auto maximum_tokens = token_slice(tokens, separator + 1U,
            tokens.size());
        if (maximum_tokens.size() == 1U
            && canonical_name(maximum_tokens.front().text) == "inf") {
            range.unbounded = true;
            return range;
        }
        const auto maximum = resolve_bound(maximum_tokens);
        if (!maximum
            || (minimum->value && maximum->value
                && *maximum->value < *minimum->value)) {
            error(marker, "FSIM-VHDL-PSL-014",
                "a PSL temporal range requires static bounds in ascending order");
            return std::nullopt;
        }
        range.maximum = maximum->value;
        range.maximum_formal = maximum->formal;
        return range;
    };

    const auto explicit_clock = [&](std::vector<Token>& expression)
        -> std::optional<VhdlPslClock> {
        for (std::size_t position = 0; position < expression.size(); ++position) {
            if (expression[position].kind != TokenKind::At
                || !top_level_at(expression, position)) {
                continue;
            }
            const auto marker = expression[position];
            auto clock_tokens = token_slice(
                expression, position + 1U, expression.size());
            if (clock_tokens.size() >= 2U
                && clock_tokens.front().kind == TokenKind::LeftParen
                && matching_delimiter(clock_tokens, 0U)
                    == clock_tokens.size() - 1U) {
                clock_tokens = token_slice(
                    clock_tokens, 1U, clock_tokens.size() - 1U);
            }
            expression.resize(position);
            if (clock_tokens.empty() || expression.empty()) {
                error(marker, "FSIM-VHDL-PSL-018",
                    "a PSL clock override requires both an expression and a clock");
                return std::nullopt;
            }
            return VhdlPslClock { clock_tokens, clock_identity(clock_tokens),
                true, true, tokens_span(clock_tokens) };
        }
        return std::nullopt;
    };

    const auto validate_clock = [&](const ExpressionContext& context,
                                    const VhdlPslClock& clock) {
        bool sampled_object { };
        for (const auto& token : clock.expression_tokens) {
            if (token.kind != TokenKind::Identifier || reserved_word(token.text)
                || canonical_name(token.text) == "event") {
                continue;
            }
            const auto name = canonical_name(token.text);
            if (const auto* signal = find_signal(context, name)) {
                sampled_object = true;
                if (!sampled_scalar_type(signal->type)) {
                    error(token, "FSIM-VHDL-PSL-011",
                        "PSL clock name '" + name
                            + "' is not a scalar Boolean/bit/logic signal");
                }
            }
        }
        if (!sampled_object) {
            error(token_for(clock.span), "FSIM-VHDL-PSL-011",
                "a PSL clock expression must sample a visible scalar signal");
        }
    };

    const auto analyze_expression = [&](const ExpressionContext& context,
                                        std::vector<Token> tokens,
                                        const VhdlPslExpressionClass result_class,
                                        const std::optional<VhdlPslClock>& default_clock)
        -> VhdlPslAnalyzedExpression {
        VhdlPslAnalyzedExpression result;
        result.expression_class = result_class;
        result.span = tokens_span(tokens);
        if (const auto override = explicit_clock(tokens)) {
            result.clock = *override;
            validate_clock(context, *result.clock);
        } else if (default_clock) {
            result.clock = *default_clock;
        }
        result.expression_tokens = tokens;

        const auto append_operator = [&](const VhdlPslTemporalOperatorKind kind,
                                         const std::size_t position,
                                         const std::size_t width,
                                         std::optional<VhdlPslStaticRange> range
                                         = std::nullopt) {
            VhdlPslTemporalOperator operation;
            operation.kind = kind;
            operation.left_tokens = token_slice(tokens, 0U, position);
            operation.right_tokens = token_slice(
                tokens, position + width, tokens.size());
            operation.range = std::move(range);
            const auto last = std::min(tokens.size() - 1U, position + width - 1U);
            operation.span = cover(tokens[position].span, tokens[last].span);
            result.temporal_operators.push_back(std::move(operation));
        };

        std::size_t parentheses { };
        std::size_t brackets { };
        std::size_t braces { };
        for (std::size_t position = 0; position < tokens.size(); ++position) {
            const auto top_level = parentheses == 0U && brackets == 0U
                && braces == 0U;
            const auto overlapped_suffix = top_level
                && position + 2U < tokens.size()
                && tokens[position].kind == TokenKind::Pipe
                && tokens[position + 1U].kind == TokenKind::Minus
                && tokens[position + 2U].kind == TokenKind::Greater;
            const auto nonoverlapped_suffix = top_level
                && position + 1U < tokens.size()
                && tokens[position].kind == TokenKind::Pipe
                && tokens[position + 1U].kind == TokenKind::Arrow;
            if (overlapped_suffix || nonoverlapped_suffix) {
                append_operator(
                    overlapped_suffix
                        ? VhdlPslTemporalOperatorKind::OverlappedSuffixImplication
                        : VhdlPslTemporalOperatorKind::NonoverlappedSuffixImplication,
                    position, overlapped_suffix ? 3U : 2U);
            }
            if (top_level && tokens[position].kind == TokenKind::Identifier) {
                const auto word = canonical_name(tokens[position].text);
                const auto kind = word == "next"
                    ? std::optional { VhdlPslTemporalOperatorKind::Next }
                    : word == "prev"
                    ? std::optional { VhdlPslTemporalOperatorKind::Previous }
                    : word == "eventually"
                    ? std::optional { VhdlPslTemporalOperatorKind::Eventually }
                    : word == "always"
                    ? std::optional { VhdlPslTemporalOperatorKind::Always }
                    : word == "until"
                    ? std::optional { VhdlPslTemporalOperatorKind::Until }
                    : word == "before"
                    ? std::optional { VhdlPslTemporalOperatorKind::Before }
                    : word == "within"
                    ? std::optional { VhdlPslTemporalOperatorKind::Within }
                    : std::nullopt;
                if (kind) {
                    std::optional<VhdlPslStaticRange> range;
                    if (position + 1U < tokens.size()
                        && tokens[position + 1U].kind == TokenKind::LeftBracket) {
                        const auto right = matching_delimiter(tokens, position + 1U);
                        if (!right) {
                            error(tokens[position], "FSIM-VHDL-PSL-014",
                                "a PSL temporal bound is unbalanced");
                        } else {
                            range = static_range(context, token_slice(tokens, position + 2U, *right),
                                tokens[position]);
                        }
                    }
                    append_operator(*kind, position, 1U, std::move(range));
                }
            }
            const auto goto_repetition = position + 2U < tokens.size()
                && tokens[position + 1U].kind == TokenKind::Minus
                && tokens[position + 2U].kind == TokenKind::Greater;
            if (tokens[position].kind == TokenKind::LeftBracket
                && position + 1U < tokens.size()
                && (tokens[position + 1U].kind == TokenKind::Star
                    || tokens[position + 1U].kind == TokenKind::Assign
                    || goto_repetition)) {
                const auto right = matching_delimiter(tokens, position);
                if (!right) {
                    error(tokens[position], "FSIM-VHDL-PSL-014",
                        "a PSL sequence repetition is unbalanced");
                } else {
                    const auto kind = tokens[position + 1U].kind == TokenKind::Star
                        ? VhdlPslTemporalOperatorKind::ConsecutiveRepetition
                        : tokens[position + 1U].kind == TokenKind::Assign
                        ? VhdlPslTemporalOperatorKind::NonconsecutiveRepetition
                        : VhdlPslTemporalOperatorKind::GotoRepetition;
                    append_operator(kind, position, *right - position + 1U,
                        static_range(context, token_slice(tokens, position + (goto_repetition ? 3U : 2U), *right),
                            tokens[position]));
                }
            }
            if (tokens[position].kind == TokenKind::Semicolon && braces == 1U
                && parentheses == 0U && brackets == 0U) {
                append_operator(
                    VhdlPslTemporalOperatorKind::SequenceConcatenation,
                    position, 1U);
            } else if (tokens[position].kind == TokenKind::Colon && braces == 1U
                && parentheses == 0U && brackets == 0U) {
                append_operator(VhdlPslTemporalOperatorKind::SequenceFusion,
                    position, 1U);
            }
            switch (tokens[position].kind) {
            case TokenKind::LeftParen:
                ++parentheses;
                break;
            case TokenKind::RightParen:
                if (parentheses != 0U) {
                    --parentheses;
                }
                break;
            case TokenKind::LeftBracket:
                ++brackets;
                break;
            case TokenKind::RightBracket:
                if (brackets != 0U) {
                    --brackets;
                }
                break;
            case TokenKind::LeftBrace:
                ++braces;
                break;
            case TokenKind::RightBrace:
                if (braces != 0U) {
                    --braces;
                }
                break;
            default:
                break;
            }
        }

        std::unordered_set<std::string> seen_references;
        std::unordered_set<std::string> seen_samples;
        for (const auto& token : tokens) {
            if (token.kind != TokenKind::Identifier || reserved_word(token.text)) {
                continue;
            }
            const auto name = canonical_name(token.text);
            if (const auto* formal = find_formal(context, name)) {
                const auto in_static_bound = std::ranges::any_of(
                    result.temporal_operators,
                    [&](const VhdlPslTemporalOperator& operation) {
                        return operation.range
                            && token.span.begin.offset
                            >= operation.range->span.begin.offset
                            && token.span.end.offset
                            <= operation.range->span.end.offset;
                    });
                if (formal->expression_class
                        == VhdlPslExpressionClass::StaticInteger
                    && !in_static_bound) {
                    error(token, "FSIM-VHDL-PSL-013",
                        "PSL integer formal '" + name
                            + "' is only legal in a locally static temporal bound");
                }
                if (seen_references.insert(name).second) {
                    result.references.push_back(VhdlPslReference {
                        name, formal->expression_class, token.span });
                }
                continue;
            }
            if (const auto declaration = context.declarations.find(name);
                declaration != context.declarations.end()) {
                if (seen_references.insert(name).second) {
                    result.references.push_back(VhdlPslReference { name,
                        declaration_class(declaration->second->kind),
                        token.span });
                }
                continue;
            }
            const auto* signal = find_signal(context, name);
            const auto* parameter = find_parameter(context, name);
            if (signal && !sampled_scalar_type(signal->type)) {
                error(token, "FSIM-VHDL-PSL-012",
                    "PSL sampled Boolean name '" + name
                        + "' is not a scalar Boolean/bit/logic object");
            } else if (parameter && !sampled_scalar_type(parameter->type)) {
                error(token, "FSIM-VHDL-PSL-012",
                    "PSL sampled Boolean constant '" + name
                        + "' is not scalar Boolean/bit/logic");
            }
            if ((signal || parameter) && seen_samples.insert(name).second) {
                result.sampled_names.push_back(name);
            }
        }

        const auto has_sequence_operator = std::ranges::any_of(
            result.temporal_operators, [](const VhdlPslTemporalOperator& operation) {
                return operation.kind
                    == VhdlPslTemporalOperatorKind::SequenceConcatenation
                    || operation.kind == VhdlPslTemporalOperatorKind::SequenceFusion
                    || operation.kind
                    == VhdlPslTemporalOperatorKind::ConsecutiveRepetition
                    || operation.kind
                    == VhdlPslTemporalOperatorKind::NonconsecutiveRepetition
                    || operation.kind == VhdlPslTemporalOperatorKind::GotoRepetition
                    || operation.kind == VhdlPslTemporalOperatorKind::Within;
            });
        const auto references_sequence = std::ranges::any_of(
            result.references, [](const VhdlPslReference& reference) {
                return reference.expression_class == VhdlPslExpressionClass::Sequence;
            });
        const auto braced_sequence = tokens.size() >= 2U
            && tokens.front().kind == TokenKind::LeftBrace
            && tokens.back().kind == TokenKind::RightBrace;
        for (const auto& reference : result.references) {
            const auto legal = result_class == VhdlPslExpressionClass::Property
                || (result_class == VhdlPslExpressionClass::Boolean
                    && (reference.expression_class
                            == VhdlPslExpressionClass::Boolean
                        || reference.expression_class
                            == VhdlPslExpressionClass::Endpoint))
                || (result_class == VhdlPslExpressionClass::Sequence
                    && reference.expression_class
                        != VhdlPslExpressionClass::Property)
                || result_class == VhdlPslExpressionClass::Endpoint;
            if (!legal) {
                error(token_for(reference.span), "FSIM-VHDL-PSL-013",
                    "PSL declaration reference '" + reference.name
                        + "' has an incompatible temporal type");
            }
        }
        if (tokens.size() == 1U && tokens.front().kind == TokenKind::Identifier
            && !reserved_word(tokens.front().text) && result.references.empty()
            && result.sampled_names.empty()) {
            error(tokens.front(), "FSIM-VHDL-PSL-017",
                "unresolved PSL declaration or sampled name '"
                    + canonical_name(tokens.front().text) + "'");
        }
        if (result_class == VhdlPslExpressionClass::Endpoint
            && !has_sequence_operator && !references_sequence
            && !braced_sequence) {
            error(tokens.empty() ? token_for(result.span) : tokens.front(),
                "FSIM-VHDL-PSL-016",
                "a PSL endpoint declaration requires a sequence expression");
        }
        if (result_class == VhdlPslExpressionClass::Boolean
            && !result.temporal_operators.empty()) {
            error(tokens.front(), "FSIM-VHDL-PSL-013",
                "a PSL Boolean declaration cannot contain temporal operators");
        }
        if (result_class == VhdlPslExpressionClass::Sequence
            && std::ranges::any_of(result.temporal_operators,
                [](const VhdlPslTemporalOperator& operation) {
                    return operation.kind
                        == VhdlPslTemporalOperatorKind::OverlappedSuffixImplication
                        || operation.kind
                        == VhdlPslTemporalOperatorKind::NonoverlappedSuffixImplication
                        || operation.kind == VhdlPslTemporalOperatorKind::Next
                        || operation.kind == VhdlPslTemporalOperatorKind::Previous
                        || operation.kind == VhdlPslTemporalOperatorKind::Eventually
                        || operation.kind == VhdlPslTemporalOperatorKind::Always
                        || operation.kind == VhdlPslTemporalOperatorKind::Until
                        || operation.kind == VhdlPslTemporalOperatorKind::Before;
                })) {
            error(tokens.front(), "FSIM-VHDL-PSL-013",
                "a PSL sequence declaration contains a property-only operator");
        }
        if (temporal_class(result_class) && !result.clock) {
            for (const auto& reference : result.references) {
                const auto declaration = context.declarations.find(reference.name);
                if (declaration != context.declarations.end()
                    && declaration->second->analyzed_expression
                    && declaration->second->analyzed_expression->clock) {
                    result.clock = declaration->second->analyzed_expression->clock;
                    break;
                }
            }
        }
        if (temporal_class(result_class) && !result.clock) {
            error(tokens.empty() ? token_for(result.span) : tokens.front(),
                "FSIM-VHDL-PSL-010",
                "a temporal PSL expression requires an inferred, default, or explicit clock");
        }
        return result;
    };

    for (auto& unit : design.units) {
        std::unordered_map<std::string, VhdlPslDeclaration*> declarations;
        for (auto& declaration : unit.vhdl_psl_declarations) {
            for (auto& formal : declaration.formals) {
                classify_formal(formal);
            }
            if (!declaration.name.empty()) {
                declarations.emplace(declaration.name, &declaration);
            }
        }
        const auto* scope = object_scopes[&unit];
        const ExpressionContext unit_context { design, *scope, declarations };
        const auto default_clock = inherited_default(unit);
        if (default_clock && defaults[&unit]) {
            validate_clock(unit_context, *default_clock);
        }
        for (auto& declaration : unit.vhdl_psl_declarations) {
            if (declaration.kind == VhdlPslDeclarationKind::DefaultClock) {
                VhdlPslAnalyzedExpression analyzed;
                analyzed.expression_class = VhdlPslExpressionClass::Boolean;
                analyzed.expression_tokens = declaration.body_tokens;
                analyzed.span = tokens_span(declaration.body_tokens);
                declaration.analyzed_expression = std::move(analyzed);
                continue;
            }
            const ExpressionContext declaration_context {
                design, *scope, declarations, &declaration.formals
            };
            declaration.analyzed_expression = analyze_expression(declaration_context,
                declaration.body_tokens, declaration_class(declaration.kind),
                default_clock);
        }
        for (auto& directive : unit.vhdl_psl_directives) {
            directive.analyzed_property = analyze_expression(unit_context,
                directive.property_tokens, VhdlPslExpressionClass::Property,
                default_clock);
        }

        for (const auto& declaration : unit.vhdl_psl_declarations) {
            if (!declaration.analyzed_expression
                || !declaration.analyzed_expression->clock) {
                continue;
            }
            for (const auto& reference :
                declaration.analyzed_expression->references) {
                const auto found = declarations.find(reference.name);
                if (found == declarations.end()
                    || !found->second->analyzed_expression
                    || !found->second->analyzed_expression->clock) {
                    continue;
                }
                if (found->second->analyzed_expression->clock->canonical_identity
                    != declaration.analyzed_expression->clock->canonical_identity) {
                    error(token_for(reference.span), "FSIM-VHDL-PSL-015",
                        "PSL declaration '" + declaration.name
                            + "' crosses incompatible clocks through '"
                            + reference.name + "'");
                }
            }
        }
        for (const auto& directive : unit.vhdl_psl_directives) {
            if (!directive.analyzed_property
                || !directive.analyzed_property->clock) {
                continue;
            }
            for (const auto& reference : directive.analyzed_property->references) {
                const auto found = declarations.find(reference.name);
                if (found == declarations.end()
                    || !found->second->analyzed_expression
                    || !found->second->analyzed_expression->clock) {
                    continue;
                }
                if (found->second->analyzed_expression->clock->canonical_identity
                    != directive.analyzed_property->clock->canonical_identity) {
                    error(token_for(reference.span), "FSIM-VHDL-PSL-015",
                        "PSL directive crosses incompatible clocks through '"
                            + reference.name + "'");
                }
            }
        }

        std::unordered_map<std::string, std::vector<std::string>> edges;
        for (const auto& declaration : unit.vhdl_psl_declarations) {
            if (declaration.name.empty() || !declaration.analyzed_expression) {
                continue;
            }
            for (const auto& reference :
                declaration.analyzed_expression->references) {
                edges[declaration.name].push_back(reference.name);
            }
        }
        std::unordered_set<std::string> active;
        std::unordered_set<std::string> complete;
        std::function<void(const std::string&)> visit = [&](const std::string& name) {
            if (complete.contains(name)) {
                return;
            }
            if (!active.insert(name).second) {
                const auto declaration = declarations.find(name);
                error(declaration == declarations.end()
                            || !declaration->second->name_token
                        ? token_for(unit.span)
                        : *declaration->second->name_token,
                    "FSIM-VHDL-PSL-019",
                    "cyclic PSL declaration reference involving '" + name + "'");
                return;
            }
            for (const auto& target : edges[name]) {
                if (edges.contains(target)) {
                    visit(target);
                }
            }
            active.erase(name);
            complete.insert(name);
        };
        for (const auto& [name, unused] : edges) {
            (void)unused;
            visit(name);
        }
    }
}

} // namespace fsim::frontend
