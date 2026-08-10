// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {
namespace {

    [[nodiscard]] VhdlPslVerificationUnitKind verification_unit_kind(
        const std::string_view text)
    {
        if (detail::iequals(text, "vprop")) {
            return VhdlPslVerificationUnitKind::Property;
        }
        if (detail::iequals(text, "vmode")) {
            return VhdlPslVerificationUnitKind::Mode;
        }
        return VhdlPslVerificationUnitKind::Unit;
    }

    [[nodiscard]] VhdlPslDeclarationKind declaration_kind(
        const std::string_view text)
    {
        if (detail::iequals(text, "sequence")) {
            return VhdlPslDeclarationKind::Sequence;
        }
        if (detail::iequals(text, "property")) {
            return VhdlPslDeclarationKind::Property;
        }
        if (detail::iequals(text, "endpoint")) {
            return VhdlPslDeclarationKind::Endpoint;
        }
        return VhdlPslDeclarationKind::Boolean;
    }

    [[nodiscard]] VhdlPslDirectiveKind directive_kind(
        const std::string_view text)
    {
        if (detail::iequals(text, "assume")) {
            return VhdlPslDirectiveKind::Assume;
        }
        if (detail::iequals(text, "restrict")) {
            return VhdlPslDirectiveKind::Restrict;
        }
        if (detail::iequals(text, "cover")) {
            return VhdlPslDirectiveKind::Cover;
        }
        return VhdlPslDirectiveKind::Assert;
    }

} // namespace

void VhdlParser::skip_vhdl_psl_markers()
{
    while (match(TokenKind::PslDirective)) {
    }
}

bool VhdlParser::parse_vhdl_psl_declaration(DesignUnit& unit)
{
    const auto before = position();
    bool comment_embedded = match(TokenKind::PslDirective);
    skip_vhdl_psl_markers();
    const bool default_clock = keyword("default", 0, true)
        && keyword("clock", 1, true);
    if (!default_clock
        && !any_keyword(
            { "boolean", "sequence", "property", "endpoint" }, true)) {
        rewind(before);
        return false;
    }

    const auto start = advance();
    VhdlPslDeclaration declaration;
    declaration.comment_embedded = comment_embedded;
    if (default_clock) {
        declaration.kind = VhdlPslDeclarationKind::DefaultClock;
        (void)advance();
    } else {
        declaration.kind = declaration_kind(start.text);
        skip_vhdl_psl_markers();
        if (!at(TokenKind::Identifier)) {
            error(current(), "FSIM-VHDL-PSL-002",
                "a PSL declaration requires an identifier");
        } else {
            const auto name = advance();
            declaration.name = vhdl_name(name.text);
            declaration.name_token = name;
        }
    }

    skip_vhdl_psl_markers();
    if (!default_clock && match(TokenKind::LeftParen)) {
        std::vector<Token> formal_tokens;
        std::size_t nested = 0;
        const auto flush_formal = [&]() {
            if (formal_tokens.empty()) {
                error(current(), "FSIM-VHDL-PSL-005",
                    "a PSL formal parameter cannot be empty");
                return;
            }
            if (formal_tokens.front().kind != TokenKind::Identifier) {
                error(formal_tokens.front(), "FSIM-VHDL-PSL-005",
                    "a PSL formal parameter requires an identifier");
                formal_tokens.clear();
                return;
            }
            VhdlPslFormal formal;
            formal.name = vhdl_name(formal_tokens.front().text);
            formal.name_token = formal_tokens.front();
            formal.profile_tokens.assign(
                formal_tokens.begin() + 1, formal_tokens.end());
            formal.span = span_from(formal_tokens.front(), formal_tokens.back());
            if (std::ranges::any_of(
                    declaration.formals, [&](const auto& existing) {
                        return existing.name == formal.name;
                    })) {
                error(formal.name_token, "FSIM-VHDL-PSL-005",
                    "duplicate PSL formal parameter '" + formal.name + "'");
            } else {
                declaration.formals.push_back(std::move(formal));
            }
            formal_tokens.clear();
        };
        while (!at_end()) {
            skip_vhdl_psl_markers();
            if (at(TokenKind::RightParen) && nested == 0) {
                flush_formal();
                (void)advance();
                break;
            }
            if (at(TokenKind::Comma) && nested == 0) {
                flush_formal();
                (void)advance();
                continue;
            }
            if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket)
                || at(TokenKind::LeftBrace)) {
                ++nested;
            } else if (at(TokenKind::RightParen) || at(TokenKind::RightBracket)
                || at(TokenKind::RightBrace)) {
                if (nested == 0) {
                    error(current(), "FSIM-VHDL-PSL-005",
                        "a PSL formal parameter list is unbalanced");
                    break;
                }
                --nested;
            }
            formal_tokens.push_back(advance());
        }
        if (at_end()) {
            error(start, "FSIM-VHDL-PSL-005",
                "an unterminated PSL formal parameter list was started here");
        }
    }

    skip_vhdl_psl_markers();
    const bool has_separator = match_keyword("is", true)
        || match(TokenKind::Assign) || match(TokenKind::ColonEqual);
    if (!has_separator) {
        error(current(), "FSIM-VHDL-PSL-002",
            "a PSL declaration requires 'is', '=' or ':='");
    }

    std::size_t nesting = 0;
    while (!at_end()) {
        skip_vhdl_psl_markers();
        if (at(TokenKind::Semicolon) && nesting == 0) {
            break;
        }
        if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket)
            || at(TokenKind::LeftBrace)) {
            ++nesting;
        } else if (at(TokenKind::RightParen) || at(TokenKind::RightBracket)
            || at(TokenKind::RightBrace)) {
            if (nesting == 0) {
                break;
            }
            --nesting;
        }
        declaration.body_tokens.push_back(advance());
    }
    if (declaration.body_tokens.empty()) {
        error(start, "FSIM-VHDL-PSL-002",
            "a PSL declaration requires a body expression");
    }
    if (!match(TokenKind::Semicolon)) {
        error(start, "FSIM-VHDL-PSL-002",
            "a PSL declaration must end with ';'");
    }
    declaration.span = span_from(start, previous());

    if (default_clock) {
        if (std::ranges::any_of(
                unit.vhdl_psl_declarations, [](const auto& existing) {
                    return existing.kind == VhdlPslDeclarationKind::DefaultClock;
                })) {
            error(start, "FSIM-VHDL-PSL-004",
                "a VHDL declarative region may contain only one PSL default "
                "clock declaration");
            return true;
        }
    } else if (!declaration.name.empty()
        && std::ranges::any_of(
            unit.vhdl_psl_declarations, [&](const auto& existing) {
                return !existing.name.empty()
                    && existing.name == declaration.name;
            })) {
        error(*declaration.name_token, "FSIM-VHDL-PSL-003",
            "duplicate PSL declaration '" + declaration.name + "'");
        return true;
    }
    unit.vhdl_psl_declarations.push_back(std::move(declaration));
    return true;
}

bool VhdlParser::parse_vhdl_psl_directive(
    DesignUnit& unit, std::optional<Token> label, const bool marker_consumed)
{
    const auto before = position();
    bool comment_embedded = marker_consumed;
    if (!marker_consumed) {
        comment_embedded = match(TokenKind::PslDirective);
    }
    skip_vhdl_psl_markers();
    if (!label && at(TokenKind::Identifier) && at(TokenKind::Colon, 1)) {
        label = advance();
        (void)advance();
        skip_vhdl_psl_markers();
    }
    if (!any_keyword({ "assert", "assume", "restrict", "cover" }, true)) {
        rewind(before);
        return false;
    }
    // An unmarked VHDL `assert` remains the ordinary VHDL assertion statement
    // unless the explicit PSL `property` introducer removes the ambiguity.
    if (!comment_embedded && keyword("assert", 0, true)
        && unit.kind != UnitKind::VhdlPslVerificationUnit
        && !keyword("property", 1, true)) {
        rewind(before);
        return false;
    }

    const auto start = advance();
    VhdlPslDirective directive;
    directive.kind = directive_kind(start.text);
    directive.comment_embedded = comment_embedded;
    if (label) {
        directive.label = vhdl_name(label->text);
        directive.label_token = *label;
    }
    skip_vhdl_psl_markers();
    (void)match_keyword("property", true);
    std::size_t nesting = 0;
    while (!at_end()) {
        skip_vhdl_psl_markers();
        if (at(TokenKind::Semicolon) && nesting == 0) {
            break;
        }
        if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket)
            || at(TokenKind::LeftBrace)) {
            ++nesting;
        } else if (at(TokenKind::RightParen) || at(TokenKind::RightBracket)
            || at(TokenKind::RightBrace)) {
            if (nesting == 0) {
                break;
            }
            --nesting;
        }
        directive.property_tokens.push_back(advance());
    }
    const bool terminated = match(TokenKind::Semicolon);
    if (directive.property_tokens.empty() || !terminated) {
        error(start, "FSIM-VHDL-PSL-006",
            "a PSL directive requires a property and terminating ';'");
    }
    directive.span = span_from(label ? *label : start, previous());
    if (!directive.label.empty()
        && std::ranges::any_of(
            unit.vhdl_psl_directives, [&](const auto& existing) {
                return !existing.label.empty()
                    && existing.label == directive.label;
            })) {
        error(*directive.label_token, "FSIM-VHDL-PSL-007",
            "duplicate PSL directive label '" + directive.label + "'");
        return true;
    }
    unit.vhdl_psl_directives.push_back(std::move(directive));
    return true;
}

DesignUnit VhdlParser::parse_vhdl_psl_verification_unit(
    const Token& start, const bool comment_embedded)
{
    DesignUnit unit;
    unit.kind = UnitKind::VhdlPslVerificationUnit;
    unit.language = Language::Vhdl2008;
    unit.vhdl_psl_verification_unit = VhdlPslVerificationUnit {
        verification_unit_kind(start.text), { }, comment_embedded
    };
    skip_vhdl_psl_markers();
    if (at(TokenKind::Identifier)) {
        const auto name = advance();
        unit.name = vhdl_name(name.text);
    } else {
        error(current(), "FSIM-VHDL-PSL-001",
            "a PSL verification unit requires a name");
    }

    skip_vhdl_psl_markers();
    if (!match(TokenKind::LeftParen)) {
        error(current(), "FSIM-VHDL-PSL-001",
            "a PSL verification unit requires a parenthesized VHDL target");
    } else {
        std::size_t depth = 1;
        while (!at_end() && depth != 0) {
            skip_vhdl_psl_markers();
            if (match(TokenKind::LeftParen)) {
                ++depth;
                unit.vhdl_psl_verification_unit->target_tokens.push_back(previous());
            } else if (match(TokenKind::RightParen)) {
                --depth;
                if (depth != 0) {
                    unit.vhdl_psl_verification_unit->target_tokens.push_back(previous());
                }
            } else {
                unit.vhdl_psl_verification_unit->target_tokens.push_back(advance());
            }
        }
        if (depth != 0
            || unit.vhdl_psl_verification_unit->target_tokens.empty()) {
            error(start, "FSIM-VHDL-PSL-001",
                "a PSL verification-unit target is empty or unbalanced");
        }
    }

    skip_vhdl_psl_markers();
    if (!match(TokenKind::LeftBrace)) {
        error(current(), "FSIM-VHDL-PSL-001",
            "a PSL verification unit requires a braced body");
    }
    while (!at_end()) {
        if (match(TokenKind::RightBrace)) {
            break;
        }
        if (parse_vhdl_psl_declaration(unit)
            || parse_vhdl_psl_directive(unit)) {
            continue;
        }
        skip_vhdl_psl_markers();
        if (match(TokenKind::RightBrace)) {
            break;
        }
        const auto unexpected = advance();
        error(unexpected, "FSIM-VHDL-PSL-008",
            "unsupported PSL verification-unit item '" + unexpected.text + "'");
        skip_to_semicolon();
    }
    if (at_end() && previous().kind != TokenKind::RightBrace) {
        error(start, "FSIM-VHDL-PSL-001",
            "an unterminated PSL verification unit was started here");
    }
    skip_vhdl_psl_markers();
    (void)match(TokenKind::Semicolon);
    unit.span = span_from(start, previous());
    return unit;
}

} // namespace fsim::frontend
