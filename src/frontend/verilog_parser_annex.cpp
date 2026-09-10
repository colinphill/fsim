// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <array>
#include <ranges>

namespace fsim::frontend {
namespace {
enum class AnnexDisposition {
    removed,
    deprecation_candidate,
};

struct AnnexRule {
    SystemVerilogAnnexConstruct construct;
    AnnexDisposition disposition;
    std::string_view description;
    std::string_view replacement;
};

constexpr std::array annex_rules {
    AnnexRule { SystemVerilogAnnexConstruct::sampled_clock_argument,
        AnnexDisposition::removed, "$sampled clocking-event argument",
        "use the clock-independent $sampled form" },
    AnnexRule { SystemVerilogAnnexConstruct::ended_sequence_method,
        AnnexDisposition::removed, "sequence ended method",
        "use the sequence triggered endpoint" },
    AnnexRule { SystemVerilogAnnexConstruct::checker_always,
        AnnexDisposition::removed, "general always procedure in a checker",
        "use always_comb, always_latch, or always_ff" },
    AnnexRule { SystemVerilogAnnexConstruct::operator_overloading,
        AnnexDisposition::removed, "operator-overload bind declaration",
        "call an ordinary function explicitly" },
    AnnexRule { SystemVerilogAnnexConstruct::defparam,
        AnnexDisposition::deprecation_candidate, "defparam declaration",
        "use an instance parameter assignment" },
    AnnexRule { SystemVerilogAnnexConstruct::procedural_assign_deassign,
        AnnexDisposition::deprecation_candidate,
        "procedural assign/deassign statement",
        "use a procedural variable assignment" },
};
} // namespace

void VerilogParser::diagnose_systemverilog_2023_annex(
    const SystemVerilogAnnexConstruct construct,
    const Token& token)
{
    if (standard_revision_ != StandardRevision::SystemVerilog2023) {
        return;
    }
    const auto rule = std::ranges::find(
        annex_rules, construct, &AnnexRule::construct);
    if (rule == annex_rules.end()) {
        return;
    }
    auto message = std::string { rule->description };
    if (rule->disposition == AnnexDisposition::removed) {
        message += " is not part of the SystemVerilog-2023 source grammar; ";
        message += rule->replacement;
        error(token, "FSIM-SV-DEPR-001", std::move(message));
        return;
    }
    message += " remains supported but is identified for possible removal; ";
    message += rule->replacement;
    warning(token, "FSIM-SV-DEPR-002", std::move(message));
}

void VerilogParser::parse_defparam_declaration(
    std::vector<VerilogDefparamDeclaration>& declarations,
    const Token& start)
{
    diagnose_systemverilog_2023_annex(
        SystemVerilogAnnexConstruct::defparam, start);
    bool first = true;
    do {
        const auto assignment_start = current();
        VerilogDefparamDeclaration declaration;
        for (;;) {
            const auto name = expect_identifier("defparam hierarchical name");
            VerilogDefparamPathSegment segment;
            segment.name = name.text;
            while (match(TokenKind::LeftBracket)) {
                segment.indices.push_back(parse_expression());
                expect(TokenKind::RightBracket,
                    "']' after defparam hierarchy index",
                    "FSIM-SV-PARSE-339");
            }
            segment.span = span_from(name, previous());
            declaration.path.push_back(std::move(segment));
            if (!match(TokenKind::Dot)) {
                break;
            }
        }
        if (declaration.path.size() < 2U) {
            error(assignment_start, "FSIM-SV-SEM-231",
                "a defparam target must contain an instance path and parameter name");
        }
        if (!declaration.path.empty()
            && !declaration.path.back().indices.empty()) {
            error(assignment_start, "FSIM-SV-SEM-232",
                "the final defparam path segment must name a parameter, not an indexed object");
        }
        expect(TokenKind::Assign,
            "'=' after defparam hierarchical name",
            "FSIM-SV-PARSE-340");
        declaration.value = parse_expression();
        declaration.span = first
            ? span_from(start, previous())
            : span_from(assignment_start, previous());
        declarations.push_back(std::move(declaration));
        first = false;
    } while (match(TokenKind::Comma));
    expect(TokenKind::Semicolon,
        "';' after defparam declaration", "FSIM-SV-PARSE-341");
}

} // namespace fsim::frontend
