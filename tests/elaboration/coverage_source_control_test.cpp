// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/verilog_coverage_points.hpp"
#include "fsim/elaboration/vhdl_coverage_points.hpp"
#include "fsim/frontend/coverage_source_control.hpp"
#include "fsim/frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace {

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

fsim::frontend::CodeCoverageSourceIdentity source_identity(
    const std::string_view logical_name,
    const std::string_view contents)
{
    const std::filesystem::path root { "/checkout" };
    const auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, root / logical_name, bytes(contents));
    assert(identity.ok());
    return *identity.identity;
}

void test_language_neutral_control_plan()
{
    using namespace fsim::frontend;
    constexpr std::string_view source = R"(module controls;
  string decoy = "// fsim coverage off metric=all reason=\"not a comment\"";
  // fsim coverage off reason="generated \"adapter\"" metric=statement
  int omitted;
  // fsim coverage off metric=branch reason="constant decision"
  if (omitted) omitted = 1;
  // fsim coverage on metric=statement
  int retained;
  // fsim coverage on metric=branch
  // fsim coverage-related prose is an ordinary comment.
  /* // fsim coverage off metric=all reason="block decoy" */
endmodule
)";
    const auto parsed = parse_coverage_source_controls(
        source, Language::SystemVerilog2017);
    assert(parsed.ok());
    assert(parsed.directives.size() == 4U);
    assert(parsed.exclusions.size() == 2U);
    assert(parsed.exclusions[0].metric == CoverageSourceMetric::Statement);
    assert(parsed.exclusions[0].reason == "generated \"adapter\"");
    assert(parsed.exclusions[0].off_line == 3U
        && parsed.exclusions[0].on_line == 7U);
    const auto omitted = source.find("int omitted");
    const auto decision = source.find("if (omitted)");
    const auto retained = source.find("int retained");
    assert(coverage_source_exclusion_at(parsed.exclusions,
               CoverageSourceMetric::Statement, omitted)
        != nullptr);
    assert(coverage_source_exclusion_at(parsed.exclusions,
               CoverageSourceMetric::Branch, decision)
        != nullptr);
    assert(coverage_source_exclusion_at(parsed.exclusions,
               CoverageSourceMetric::Statement, retained)
        == nullptr);
    assert(parse_coverage_source_controls(
               source, Language::SystemVerilog2017)
        == parsed);

    constexpr std::array metrics {
        CoverageSourceMetric::All,
        CoverageSourceMetric::Statement,
        CoverageSourceMetric::Branch,
        CoverageSourceMetric::Line,
        CoverageSourceMetric::Condition,
        CoverageSourceMetric::Expression,
        CoverageSourceMetric::Toggle,
        CoverageSourceMetric::FsmState,
        CoverageSourceMetric::FsmTransition,
        CoverageSourceMetric::SystemVerilogCoverpoint,
        CoverageSourceMetric::SystemVerilogCross,
        CoverageSourceMetric::PslDirective,
        CoverageSourceMetric::PslProperty,
    };
    for (const auto metric : metrics)
        assert(coverage_source_metric_name(metric) != "invalid");
}

void test_verilog_statement_application()
{
    using namespace fsim;
    constexpr std::string_view source = R"(module controlled;
  integer value;
  initial begin
    value = 0;
    // fsim coverage off metric=statement reason="generated assignments"
    value = 1;
    value = 2;
    // fsim coverage on metric=statement
    // fsim coverage off metric=branch reason="branch only"
    if (value) value = 3;
    // fsim coverage on metric=branch
    value = 4;
  end
endmodule
)";
    const auto identity = source_identity("rtl/controlled.sv", source);
    const auto parsed = frontend::parse_text("controlled.sv", source,
        frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto& statements
        = parsed.design.units.front().processes.front().statements;
    const elaboration::VerilogCoverageSource baseline_source {
        "controlled.sv", identity
    };
    const elaboration::VerilogCoverageSource controlled_source {
        "controlled.sv", identity, source
    };
    const auto baseline = elaboration::discover_verilog_statement_points(
        statements, frontend::Language::SystemVerilog2017,
        std::span { &baseline_source, 1U });
    const auto controlled = elaboration::discover_verilog_statement_points(
        statements, frontend::Language::SystemVerilog2017,
        std::span { &controlled_source, 1U });
    assert(baseline.ok() && controlled.ok());
    assert(controlled.points.size() + 2U == baseline.points.size());
    assert(controlled.exclusions.size() == 2U);
    assert(std::ranges::all_of(controlled.exclusions,
        [](const auto& exclusion) {
            return exclusion.reason == "generated assignments";
        }));
    const auto excluded_begin = source.find("value = 1");
    const auto excluded_end = source.find("value = 2") + 10U;
    assert(std::ranges::none_of(controlled.points, [&](const auto& point) {
        return point.span.begin_offset >= excluded_begin
            && point.span.begin_offset < excluded_end;
    }));
    assert(std::ranges::any_of(controlled.points, [](const auto& point) {
        return point.statement_kind == frontend::StatementKind::If;
    }));

    const elaboration::VerilogCoverageSource unauthenticated_source {
        "controlled.sv", identity, source.substr(0U, source.size() - 1U)
    };
    const auto unauthenticated
        = elaboration::discover_verilog_statement_points(statements,
            frontend::Language::SystemVerilog2017,
            std::span { &unauthenticated_source, 1U });
    assert(unauthenticated.error
        == elaboration::VerilogCoveragePointError::InvalidSourceIdentity);
}

void test_vhdl_statement_application()
{
    using namespace fsim;
    constexpr std::string_view source = R"(entity controlled is
  port (a : out bit);
end controlled;
architecture rtl of controlled is
  constant decoy : string := "-- fsim coverage off metric=all reason=""not a comment""";
begin
  process
  begin
    a <= '0';
    -- fsim coverage off metric=statement reason="generated assignments"
    a <= '1';
    -- fsim coverage on metric=statement
    a <= '0';
    wait;
  end process;
end rtl;
)";
    const auto identity = source_identity("rtl/controlled.vhd", source);
    const auto parsed = frontend::parse_text("controlled.vhd", source,
        frontend::Language::Vhdl2008, frontend::VhdlStandard::Vhdl2008);
    assert(parsed.ok());
    const auto& statements
        = parsed.design.units.back().processes.front().statements;
    const elaboration::VhdlCoverageSource baseline_source {
        "controlled.vhd", identity
    };
    const elaboration::VhdlCoverageSource controlled_source {
        "controlled.vhd", identity, source
    };
    const auto baseline = elaboration::discover_vhdl_statement_points(
        statements, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008,
        std::span { &baseline_source, 1U });
    const auto controlled = elaboration::discover_vhdl_statement_points(
        statements, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008,
        std::span { &controlled_source, 1U });
    assert(baseline.ok() && controlled.ok());
    assert(controlled.points.size() + 1U == baseline.points.size());
    assert(controlled.exclusions.size() == 1U);
    assert(controlled.exclusions.front().reason == "generated assignments");
    const auto omitted = source.find("a <= '1'");
    assert(std::ranges::none_of(controlled.points, [&](const auto& point) {
        return point.span.begin_offset == omitted;
    }));
}

void test_eof_and_rejections()
{
    using namespace fsim::frontend;
    const auto eof = parse_coverage_source_controls(
        "// fsim coverage off metric=toggle reason=\"unused output\"\nwire x;\n",
        Language::Verilog2005);
    assert(eof.ok() && eof.exclusions.size() == 1U);
    assert(eof.exclusions.front().on_line == 0U);
    assert(eof.exclusions.front().end_offset
        == std::string_view {
            "// fsim coverage off metric=toggle reason=\"unused output\"\nwire x;\n" }
            .size());

    const auto expect = [](const std::string_view source,
                            const CoverageSourceControlError error) {
        const auto result = parse_coverage_source_controls(
            source, Language::SystemVerilog2017);
        assert(!result.ok() && result.error == error);
        assert(result.directives.empty() && result.exclusions.empty());
    };
    expect("// fsim coverage off reason=\"missing metric\"\n",
        CoverageSourceControlError::MalformedDirective);
    expect("// fsim coverage off metric=statement\n",
        CoverageSourceControlError::MissingReason);
    expect("// fsim coverage on metric=statement reason=\"bad\"\n",
        CoverageSourceControlError::UnexpectedReason);
    expect("// fsim coverage off metric=unknown reason=\"bad\"\n",
        CoverageSourceControlError::UnknownMetric);
    expect("// fsim coverage on metric=statement\n",
        CoverageSourceControlError::UnmatchedOn);
    expect("// fsim coverage off metric=statement reason=\"one\"\n"
           "// fsim coverage off metric=statement reason=\"two\"\n",
        CoverageSourceControlError::DuplicateOff);
    expect("// fsim coverage off metric=all reason=\"all\"\n"
           "// fsim coverage off metric=statement reason=\"specific\"\n",
        CoverageSourceControlError::ConflictingAllMetric);

    CoverageSourceControlLimits limits;
    limits.maximum_directives = 0U;
    assert(parse_coverage_source_controls(
               "// fsim coverage on metric=statement\n",
               Language::SystemVerilog2017, limits)
               .error
        == CoverageSourceControlError::ResourceLimit);
    limits = { };
    limits.maximum_reason_bytes = 2U;
    assert(parse_coverage_source_controls(
               "-- fsim coverage off metric=all reason=\"long\"\n",
               Language::Vhdl2008, limits)
               .error
        == CoverageSourceControlError::ResourceLimit);
    limits = { };
    limits.maximum_source_bytes = 1U;
    assert(parse_coverage_source_controls(
               "-- ordinary\n", Language::Vhdl2008, limits)
               .error
        == CoverageSourceControlError::ResourceLimit);
    assert(parse_coverage_source_controls("", static_cast<Language>(99U)).error
        == CoverageSourceControlError::InvalidLanguage);

    constexpr std::string_view invalid_control = R"(module invalid;
  initial begin
    // fsim coverage off metric=statement
    $display("never discovered");
  end
endmodule
)";
    const auto invalid_parsed = parse_text("invalid.sv", invalid_control,
        Language::SystemVerilog2017);
    assert(invalid_parsed.ok());
    const fsim::elaboration::VerilogCoverageSource invalid_source {
        "invalid.sv", source_identity("rtl/invalid.sv", invalid_control),
        invalid_control
    };
    const auto rejected
        = fsim::elaboration::discover_verilog_statement_points(
            invalid_parsed.design.units.front().processes.front().statements,
            Language::SystemVerilog2017,
            std::span { &invalid_source, 1U });
    assert(rejected.error
        == fsim::elaboration::VerilogCoveragePointError::InvalidSourceControl);
}

} // namespace

int main()
{
    test_language_neutral_control_plan();
    test_verilog_statement_application();
    test_vhdl_statement_application();
    test_eof_and_rejections();
}
