// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {
namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string { message });
        }
    }

    [[nodiscard]] std::size_t diagnostic_count(
        const fsim::frontend::ParseResult& result,
        const std::string_view code)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            result.diagnostics, [&](const auto& diagnostic) {
                return diagnostic.code == code;
            }));
    }

    [[nodiscard]] std::string diagnostics_text(
        const fsim::frontend::ParseResult& result)
    {
        std::string text { "embedded and native VHDL PSL ownership must parse" };
        for (const auto& diagnostic : result.diagnostics) {
            text += "\n" + diagnostic.code + ": " + diagnostic.message;
        }
        return text;
    }

} // namespace

void test_vhdl_psl_declaration_ownership()
{
    using namespace fsim::frontend;
    const auto parsed = parse_text(
        "embedded-psl.vhd",
        R"(
entity dut is
end entity;

architecture rtl of dut is
  signal clk, request, acknowledge : bit;
  -- psl default clock is clk = '1';
  -- psl boolean active is request = '1';
  -- psl sequence handshake(limit : natural := 2) is
  -- psl   {request = '1' ; acknowledge = '1'};
  -- psl property completes is handshake(2);
  -- psl endpoint observed is handshake(2);
  sequence native_sequence is request = '1';
begin
  -- psl A_HANDSHAKE: assert completes;
  -- psl M_ENV: assume active;
  -- psl R_ENV: restrict active;
  -- psl C_HANDSHAKE: cover handshake(2);
  NATIVE_ASSUME: assume native_sequence;
end architecture;

-- psl vunit monitor (dut(rtl)) {
-- psl   default clock is clk = '1';
-- psl   boolean ready_seen is acknowledge = '1';
-- psl   sequence response(delay : natural := 1) is
-- psl     {request = '1' ; acknowledge = '1'};
-- psl   property response_holds is response(1);
-- psl   endpoint response_seen is response(1);
-- psl   V_ASSERT: assert response_holds;
-- psl }

vprop protocol_properties (dut) {
  property direct_property is request = '0';
  DIRECT_COVER: cover direct_property;
}

vmode environment_mode (dut) {
  boolean permitted is request = '0';
  DIRECT_RESTRICT: restrict permitted;
}
)",
        Language::Vhdl2008);
    require(parsed.ok(), diagnostics_text(parsed));
    require(parsed.design.units.size() == 5,
        "entity, architecture, vunit, vprop, and vmode are retained");
    const auto& architecture = parsed.design.units[1];
    require(
        architecture.vhdl_psl_declarations.size() == 6
            && architecture.vhdl_psl_directives.size() == 5,
        "architecture PSL declarations and directives retain region ownership");
    require(
        architecture.vhdl_psl_declarations[0].kind
                == VhdlPslDeclarationKind::DefaultClock
            && architecture.vhdl_psl_declarations[0].comment_embedded
            && architecture.vhdl_psl_declarations[2].name == "handshake"
            && architecture.vhdl_psl_declarations[2].formals.size() == 1
            && architecture.vhdl_psl_declarations[2].formals[0].name == "limit"
            && !architecture.vhdl_psl_declarations[2].body_tokens.empty()
            && architecture.vhdl_psl_declarations[5].name == "native_sequence"
            && !architecture.vhdl_psl_declarations[5].comment_embedded,
        "clock, Boolean, sequence, property, endpoint, formals, and native "
        "placement retain exact typed ownership");
    require(
        architecture.vhdl_psl_directives[0].kind
                == VhdlPslDirectiveKind::Assert
            && architecture.vhdl_psl_directives[0].label == "a_handshake"
            && architecture.vhdl_psl_directives[0].comment_embedded
            && architecture.vhdl_psl_directives[4].kind
                == VhdlPslDirectiveKind::Assume
            && architecture.vhdl_psl_directives[4].label == "native_assume"
            && !architecture.vhdl_psl_directives[4].comment_embedded,
        "all four directive kinds, labels, and embedding modes are retained");

    const auto& vunit = parsed.design.units[2];
    require(
        vunit.kind == UnitKind::VhdlPslVerificationUnit
            && vunit.name == "monitor" && vunit.vhdl_context.empty()
            && vunit.vhdl_psl_verification_unit
            && vunit.vhdl_psl_verification_unit->kind
                == VhdlPslVerificationUnitKind::Unit
            && vunit.vhdl_psl_verification_unit->comment_embedded
            && vunit.vhdl_psl_verification_unit->target_tokens.size() == 4
            && vunit.vhdl_psl_declarations.size() == 5
            && vunit.vhdl_psl_directives.size() == 1,
        "comment-embedded vunit target, contents, and exact source ownership");
    require(
        parsed.design.units[3].vhdl_psl_verification_unit->kind
                == VhdlPslVerificationUnitKind::Property
            && parsed.design.units[4].vhdl_psl_verification_unit->kind
                == VhdlPslVerificationUnitKind::Mode,
        "native vprop and vmode standard verification-unit kinds are distinct");

    const auto lexed = lex(
        SourceText { "psl-marker.vhd", "-- PsL assert ready;\n-- ordinary" },
        Language::Vhdl2008);
    require(
        lexed.ok() && lexed.tokens.size() == 5
            && lexed.tokens.front().kind == TokenKind::PslDirective
            && lexed.tokens.front().span.begin.line == 1
            && lexed.tokens[1].text == "assert",
        "case-insensitive -- psl is a source-spanned lexical marker while "
        "ordinary comments remain trivia");

    const auto invalid = parse_text(
        "invalid-psl.vhd",
        R"(
entity invalid_psl is end entity;
architecture rtl of invalid_psl is
  signal clk : bit;
  -- psl default clock is clk = '1';
  -- psl default clock is clk = '0';
  -- psl sequence duplicate is clk = '1';
  -- psl property duplicate is clk = '1';
  -- psl sequence bad_formal(, value : natural) is clk = '1';
begin
  -- psl EMPTY: assert;
  -- psl DUP: cover duplicate;
  -- psl DUP: assume duplicate;
end architecture;
-- psl assert stray;
)",
        Language::Vhdl2008);
    require(
        !invalid.ok()
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-004") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-003") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-005") >= 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-006") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-007") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-009") == 1,
        diagnostics_text(invalid));

    const auto malformed_units = parse_text(
        "malformed-psl-units.vhd",
        R"(
vunit (dut) {
  unsupported_item;
}
vprop malformed (dut) {
  sequence missing_body;
}
)",
        Language::Vhdl2008);
    require(
        !malformed_units.ok()
            && diagnostic_count(malformed_units, "FSIM-VHDL-PSL-001") >= 1
            && diagnostic_count(malformed_units, "FSIM-VHDL-PSL-002") >= 1
            && diagnostic_count(malformed_units, "FSIM-VHDL-PSL-008") == 1,
        diagnostics_text(malformed_units));
}

void test_vhdl_psl_temporal_analysis()
{
    using namespace fsim::frontend;
    const auto parsed = parse_text(
        "temporal-psl.vhd",
        R"(
entity timed_dut is
  port (
    clk, clk2, request, acknowledge : in std_logic;
    vector_value : in std_logic_vector(3 downto 0));
end entity;

architecture rtl of timed_dut is
  -- psl default clock is rising_edge(clk);
  -- psl boolean requested is request = '1';
  -- psl sequence pair is {requested ; acknowledge = '1'};
  -- psl sequence fused is {requested : acknowledge = '1'};
  -- psl sequence repeated is {requested[*2:4] ; acknowledge = '1'};
  -- psl sequence formal_repeat(count : natural := 3) is {requested[*count]};
  -- psl sequence nonconsecutive is {requested[=2]};
  -- psl sequence goto_match is {requested[->2]};
  -- psl sequence enclosed is pair within repeated;
  -- psl property response is pair |-> next[2] always acknowledge = '1';
  -- psl property delayed_response is pair |=> eventually[1:3] acknowledge = '1';
  -- psl property history is prev(requested) until eventually acknowledge = '1';
  -- psl property ordered is requested before acknowledge = '1';
  -- psl endpoint completion is pair;
  -- psl endpoint direct_completion is {requested};
  -- psl property alternate is always acknowledge = '1' @ rising_edge(clk2);
  -- psl property equivalent_clock is always request = '1' @ (clk'event and clk = '1');
begin
  -- psl CHECK_RESPONSE: assert response;
end architecture;
)",
        Language::Vhdl2008);
    require(parsed.ok(), diagnostics_text(parsed));
    const auto& unit = parsed.design.units[1];
    const auto declaration = [&](const std::string_view name)
        -> const VhdlPslDeclaration& {
        const auto found = std::ranges::find(
            unit.vhdl_psl_declarations, name, &VhdlPslDeclaration::name);
        require(found != unit.vhdl_psl_declarations.end(),
            "expected analyzed PSL declaration is retained");
        return *found;
    };
    const auto has_operator = [](const VhdlPslDeclaration& item,
                                  const VhdlPslTemporalOperatorKind kind) {
        return item.analyzed_expression
            && std::ranges::any_of(
                item.analyzed_expression->temporal_operators,
                [&](const VhdlPslTemporalOperator& operation) {
                    return operation.kind == kind;
                });
    };

    const auto& repeated = declaration("repeated");
    require(
        has_operator(repeated,
            VhdlPslTemporalOperatorKind::ConsecutiveRepetition)
            && has_operator(repeated,
                VhdlPslTemporalOperatorKind::SequenceConcatenation),
        "sequence repetition and concatenation retain typed operations");
    const auto repetition = std::ranges::find(
        repeated.analyzed_expression->temporal_operators,
        VhdlPslTemporalOperatorKind::ConsecutiveRepetition,
        &VhdlPslTemporalOperator::kind);
    require(
        repetition != repeated.analyzed_expression->temporal_operators.end()
            && repetition->range && repetition->range->minimum == 2U
            && repetition->range->maximum == 4U,
        "sequence repetition bounds are statically normalized");
    const auto& formal_repeat = declaration("formal_repeat");
    const auto formal_repetition = std::ranges::find(
        formal_repeat.analyzed_expression->temporal_operators,
        VhdlPslTemporalOperatorKind::ConsecutiveRepetition,
        &VhdlPslTemporalOperator::kind);
    require(formal_repeat.formals.size() == 1U,
        "the formal repetition parameter is retained");
    require(formal_repeat.formals.front().expression_class
            == VhdlPslExpressionClass::StaticInteger,
        "the repetition formal is a static integer");
    require(formal_repeat.formals.front().static_default == 3U,
        "the repetition formal retains its default");
    require(formal_repetition
                != formal_repeat.analyzed_expression->temporal_operators.end()
            && formal_repetition->range,
        "the formal repetition retains a static range");
    require(formal_repetition->range->minimum == 3U
            && formal_repetition->range->minimum_formal == "count",
        "integer formals remain locally static temporal bounds");
    require(
        has_operator(
            declaration("fused"), VhdlPslTemporalOperatorKind::SequenceFusion)
            && has_operator(
                declaration("enclosed"), VhdlPslTemporalOperatorKind::Within),
        "sequence fusion and within are typed distinctly");
    require(
        has_operator(declaration("nonconsecutive"),
            VhdlPslTemporalOperatorKind::NonconsecutiveRepetition)
            && has_operator(declaration("goto_match"),
                VhdlPslTemporalOperatorKind::GotoRepetition)
            && has_operator(declaration("delayed_response"),
                VhdlPslTemporalOperatorKind::NonoverlappedSuffixImplication),
        "nonconsecutive/goto repetition and nonoverlapped implication are typed");
    const auto& delayed_response = declaration("delayed_response");
    const auto eventually = std::ranges::find(
        delayed_response.analyzed_expression->temporal_operators,
        VhdlPslTemporalOperatorKind::Eventually,
        &VhdlPslTemporalOperator::kind);
    require(
        eventually
                != delayed_response.analyzed_expression->temporal_operators.end()
            && eventually->range && eventually->range->minimum == 1U
            && eventually->range->maximum == 3U,
        "bounded recurrence ranges are statically normalized");

    const auto& response = declaration("response");
    require(response.analyzed_expression.has_value(),
        "property analysis is retained");
    require(response.analyzed_expression->expression_class
            == VhdlPslExpressionClass::Property,
        "property analysis retains its result class");
    require(response.analyzed_expression->clock.has_value(),
        "property analysis infers a clock");
    require(response.analyzed_expression->clock->canonical_identity
                == "rise:clk"
            && !response.analyzed_expression->clock->explicit_override,
        "property analysis retains its inferred default clock");
    require(response.analyzed_expression->unknown_policy
            == VhdlPslUnknownPolicy::False,
        "property analysis makes the unknown-false policy explicit");
    require(has_operator(response,
                VhdlPslTemporalOperatorKind::OverlappedSuffixImplication),
        "property suffix implication is typed");
    require(has_operator(response, VhdlPslTemporalOperatorKind::Next),
        "property bounded next is typed");
    require(has_operator(response, VhdlPslTemporalOperatorKind::Always),
        "property recurrence is typed");
    const auto& history = declaration("history");
    require(
        has_operator(history, VhdlPslTemporalOperatorKind::Previous)
            && has_operator(history, VhdlPslTemporalOperatorKind::Until)
            && has_operator(history, VhdlPslTemporalOperatorKind::Eventually)
            && has_operator(
                declaration("ordered"), VhdlPslTemporalOperatorKind::Before),
        "previous, until, eventually, and before retain typed operations");
    require(
        declaration("completion").analyzed_expression->expression_class
                == VhdlPslExpressionClass::Endpoint
            && declaration("direct_completion")
                    .analyzed_expression->expression_class
                == VhdlPslExpressionClass::Endpoint
            && declaration("alternate")
                .analyzed_expression->clock->explicit_override
            && declaration("equivalent_clock")
                    .analyzed_expression->clock->canonical_identity
                == response.analyzed_expression->clock->canonical_identity
            && unit.vhdl_psl_directives.front().analyzed_property
            && unit.vhdl_psl_directives.front().analyzed_property->clock,
        "endpoint typing, explicit clock override, and directive clock "
        "inference are retained");

    const auto invalid = parse_text(
        "invalid-temporal-psl.vhd",
        R"(
entity invalid_timed is
  generic (limit : natural := 2);
  port (
    clk, clk2 : in std_logic;
    vector_value : in std_logic_vector(3 downto 0));
end entity;
architecture rtl of invalid_timed is
  -- psl default clock is rising_edge(clk);
  -- psl boolean vector_sample is vector_value;
  -- psl boolean temporal_boolean is always true;
  -- psl sequence dynamic_bound is {true[*limit]};
  -- psl sequence integer_sample(count : natural) is {count};
  -- psl endpoint boolean_endpoint is true;
  -- psl property alternate is always true @ rising_edge(clk2);
  -- psl property crossing is alternate;
  -- psl boolean wrong_reference is alternate;
  -- psl sequence wrong_sequence is always true;
  -- psl property unresolved is missing_name;
  -- psl property malformed_override is true @;
  -- psl property invalid_clock is true @ vector_value;
  -- psl property cycle_a is cycle_b;
  -- psl property cycle_b is cycle_a;
begin
  -- psl CROSSING_DIRECTIVE: assert alternate;
end architecture;
)",
        Language::Vhdl2008);
    require(
        !invalid.ok()
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-011") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-012") >= 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-013") == 4
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-014") >= 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-015") == 3
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-016") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-017") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-018") == 1
            && diagnostic_count(invalid, "FSIM-VHDL-PSL-019") >= 1,
        diagnostics_text(invalid));

    const auto missing_clock = parse_text(
        "missing-psl-clock.vhd",
        R"(
entity unclocked is end entity;
architecture rtl of unclocked is
  -- psl property no_clock is always true;
begin
end architecture;
)",
        Language::Vhdl2008);
    require(
        !missing_clock.ok()
            && diagnostic_count(missing_clock, "FSIM-VHDL-PSL-010") == 1,
        diagnostics_text(missing_clock));
}

} // namespace fsim::tests::frontend
