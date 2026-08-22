// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/verilog_coverage_conditions.hpp"

#include "fsim/frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

std::filesystem::path checkout_root(const std::string_view leaf)
{
#if defined(_WIN32)
    return std::filesystem::path { "C:/fsim-coverage" } / leaf;
#else
    return std::filesystem::path { "/fsim-coverage" } / leaf;
#endif
}

fsim::elaboration::VerilogCoverageConditionSource make_source(
    const std::filesystem::path& root, const std::filesystem::path& source,
    const std::string_view contents)
{
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, source, bytes(contents));
    require(identity.ok(), "test condition source identity must be valid");
    return fsim::elaboration::VerilogCoverageConditionSource {
        source.generic_string(), std::move(*identity.identity)
    };
}

fsim::frontend::SourceSpan span(
    std::string source, const std::size_t begin, const std::size_t end)
{
    return fsim::frontend::SourceSpan {
        std::move(source),
        { begin, 1U, begin + 1U },
        { end, 1U, end + 1U },
        { },
        { },
    };
}

std::vector<fsim::runtime::CodeCoveragePointId> point_ids(
    const fsim::elaboration::VerilogCoverageConditionResult& result)
{
    std::vector<fsim::runtime::CodeCoveragePointId> ids;
    ids.reserve(result.points.size());
    for (const auto& point : result.points) {
        ids.push_back(point.id);
    }
    return ids;
}

std::string_view point_text(const std::string_view source,
    const fsim::elaboration::VerilogCoverageConditionPoint& point)
{
    return source.substr(static_cast<std::size_t>(point.span.begin_offset),
        static_cast<std::size_t>(
            point.span.end_offset - point.span.begin_offset));
}

void test_decomposition_and_structure()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(module conditions;
  reg a;
  reg b;
  reg c;
  reg d;
  initial begin
    if ((a && b) || (!c && (d == 1'b1))) d = 1'b0;
    while (a || b) a = 1'b0;
  end
endmodule
)";
    const auto root = checkout_root("conditions");
    const auto source = make_source(
        root, root / "rtl/conditions.sv", source_text);
    auto parsed = frontend::parse_text(source.source_name, source_text,
        frontend::Language::SystemVerilog2017);
    require(parsed.ok() && parsed.design.units.front().processes.size() == 1U,
        "independently authored SystemVerilog condition corpus must parse");
    const auto& statements
        = parsed.design.units.front().processes.front().statements;
    require(statements.size() == 2U
            && statements[0].kind == frontend::StatementKind::If
            && statements[1].kind == frontend::StatementKind::Loop,
        "condition corpus must retain its two source decisions");
    const auto root_text = statements[0].condition.text;
    const auto root_operand_count = statements[0].condition.operands.size();
    const auto left_text = statements[0].condition.operands.front().text;

    const auto discovered
        = elaboration::discover_verilog_coverage_conditions(
            statements, frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(discovered.ok() && discovered.points.size() == 6U,
        "logical decisions must decompose into six lexical atoms");
    const std::array expected_text {
        std::string_view { "a" },
        std::string_view { "b" },
        std::string_view { "c" },
        std::string_view { "(d == 1'b1)" },
        std::string_view { "a" },
        std::string_view { "b" },
    };
    for (std::size_t index = 0U; index < expected_text.size(); ++index) {
        const auto actual = point_text(source_text, discovered.points[index]);
        require(actual == expected_text[index],
            std::string { "atom " } + std::to_string(index)
                + " must retain exact text; expected '"
                + std::string { expected_text[index] } + "', got '"
                + std::string { actual } + "'");
    }
    require(discovered.points[0].evaluation_path
                == std::vector<elaboration::CoverageConditionPathStep> {
                    { elaboration::CoverageConditionLogicalOperator::Or,
                        elaboration::CoverageConditionOperand::Left },
                    { elaboration::CoverageConditionLogicalOperator::And,
                        elaboration::CoverageConditionOperand::Left },
                }
            && discovered.points[1].evaluation_path.back().operand == elaboration::CoverageConditionOperand::Right && discovered.points[2].evaluation_path == std::vector<elaboration::CoverageConditionPathStep> {
                                                                                                                          { elaboration::CoverageConditionLogicalOperator::Or, elaboration::CoverageConditionOperand::Right },
                                                                                                                          { elaboration::CoverageConditionLogicalOperator::And, elaboration::CoverageConditionOperand::Left },
                                                                                                                          { elaboration::CoverageConditionLogicalOperator::Not, elaboration::CoverageConditionOperand::Only },
                                                                                                                      },
        "atomic paths must retain every enclosing short-circuit connective");
    require(discovered.points[2].expression_kind
                == frontend::ExpressionKind::Identifier
            && discovered.points[3].expression_kind
                == frontend::ExpressionKind::Binary,
        "logical negation must remain a path node while comparisons stay atomic");
    require(discovered.points[0].decision_index == 0U
            && discovered.points[3].condition_index == 3U
            && discovered.points[4].decision_index == 1U
            && discovered.points[4].condition_index == 0U
            && discovered.points[4].decision
                == elaboration::CoverageConditionDecisionKind::Loop,
        "decision and condition ordinals must be lexical and independently addressable");
    require(std::ranges::all_of(discovered.points,
                [](const auto& point) {
                    return point.language
                        == frontend::CodeCoverageLanguage::SystemVerilog
                        && point.source_index == 0U;
                }),
        "every atom must retain typed SystemVerilog and source ownership");
    require(statements[0].condition.text == root_text
            && statements[0].condition.operands.size() == root_operand_count
            && statements[0].condition.operands.front().text == left_text,
        "decomposition must not rewrite or flatten the retained expression tree");
    const auto direct = frontend::make_code_coverage_point_identity(
        source.identity, frontend::CodeCoverageLanguage::SystemVerilog,
        frontend::CodeCoverageConstructKind::AtomicCondition,
        discovered.points.front().span);
    require(direct.ok() && direct.identity == discovered.points.front().id,
        "atomic conditions must use the canonical source point identity");
}

void test_all_retained_profiles_and_relocation()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(module profile_conditions;
  reg a;
  reg b;
  reg c;
  initial begin
    if ((a && b) || !c) a = 1'b0;
    while (a || b) b = 1'b0;
  end
endmodule
)";
    struct Profile {
        frontend::StandardRevision revision;
        frontend::Language language;
    };
    constexpr std::array profiles {
        Profile { frontend::StandardRevision::Verilog1995,
            frontend::Language::Verilog2005 },
        Profile { frontend::StandardRevision::Verilog2001,
            frontend::Language::Verilog2005 },
        Profile { frontend::StandardRevision::Verilog2001NoConfig,
            frontend::Language::Verilog2005 },
        Profile { frontend::StandardRevision::Verilog2005,
            frontend::Language::Verilog2005 },
        Profile { frontend::StandardRevision::SystemVerilog2005,
            frontend::Language::SystemVerilog2017 },
        Profile { frontend::StandardRevision::SystemVerilog2009,
            frontend::Language::SystemVerilog2017 },
        Profile { frontend::StandardRevision::SystemVerilog2012,
            frontend::Language::SystemVerilog2017 },
        Profile { frontend::StandardRevision::SystemVerilog2017,
            frontend::Language::SystemVerilog2017 },
    };
    for (std::size_t index = 0U; index < profiles.size(); ++index) {
        const auto root_a = checkout_root("profile-a");
        const auto root_b = checkout_root("profile-b");
        const auto relative = std::filesystem::path { "rtl/profile.v" };
        const auto source_a
            = make_source(root_a, root_a / relative, source_text);
        const auto source_b
            = make_source(root_b, root_b / relative, source_text);
        const auto parsed_a = frontend::parse_verilog(
            frontend::SourceText { source_a.source_name,
                std::string { source_text } },
            profiles[index].revision);
        const auto parsed_b = frontend::parse_verilog(
            frontend::SourceText { source_b.source_name,
                std::string { source_text } },
            profiles[index].revision);
        require(parsed_a.ok() && parsed_b.ok(),
            "every retained Verilog/SystemVerilog profile must parse the condition corpus");
        const auto first = elaboration::discover_verilog_coverage_conditions(
            parsed_a.design.units.front().processes.front().statements,
            profiles[index].language, std::span { &source_a, 1U });
        const auto relocated
            = elaboration::discover_verilog_coverage_conditions(
                parsed_b.design.units.front().processes.front().statements,
                profiles[index].language, std::span { &source_b, 1U });
        require(first.ok() && first.points.size() == 5U && relocated.ok()
                && point_ids(first) == point_ids(relocated),
            "atomic condition identities must be profile-complete and checkout independent");
        const auto expected_language
            = profiles[index].language == frontend::Language::Verilog2005
            ? frontend::CodeCoverageLanguage::Verilog
            : frontend::CodeCoverageLanguage::SystemVerilog;
        require(std::ranges::all_of(first.points,
                    [&](const auto& point) {
                        return point.language == expected_language;
                    }),
            "profile families must retain distinct Verilog/SystemVerilog identity domains");
    }
}

void test_immediate_assertion_and_rejections()
{
    using namespace fsim;
    constexpr std::string_view contents = "0123456789abcdef";
    const auto root = checkout_root("negative");
    const auto source
        = make_source(root, root / "rtl/negative.sv", contents);
    frontend::Expression left { frontend::ExpressionKind::Identifier, "a", { },
        span(source.source_name, 2U, 3U) };
    frontend::Expression right { frontend::ExpressionKind::Unary, "!",
        { frontend::Expression { frontend::ExpressionKind::Identifier, "b", { },
            span(source.source_name, 5U, 6U) } },
        span(source.source_name, 4U, 6U) };
    frontend::Statement assertion;
    assertion.kind = frontend::StatementKind::Assert;
    assertion.span = span(source.source_name, 0U, 7U);
    assertion.condition = frontend::Expression {
        frontend::ExpressionKind::Binary, "&&", { left, right },
        span(source.source_name, 2U, 6U)
    };
    const auto assertion_result
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &assertion, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(assertion_result.ok() && assertion_result.points.size() == 2U
            && assertion_result.points.front().decision
                == elaboration::CoverageConditionDecisionKind::ImmediateAssertion,
        "immediate assertions must share the stable atomic-condition model");
    auto negated_composite = assertion;
    negated_composite.condition = frontend::Expression {
        frontend::ExpressionKind::Unary, "!", { assertion.condition },
        assertion.condition.span
    };
    const auto negated_result
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &negated_composite, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(negated_result.ok() && negated_result.points.size() == 2U
            && negated_result.points.front().evaluation_path.size() == 2U
            && negated_result.points.front().evaluation_path[0].logical_operator
                == elaboration::CoverageConditionLogicalOperator::Not
            && negated_result.points.front().evaluation_path[1].logical_operator
                == elaboration::CoverageConditionLogicalOperator::And,
        "logical negation around a composite must expose inner atoms without flattening its path");

    const auto invalid_language
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &assertion, 1U }, frontend::Language::Vhdl2008,
            std::span { &source, 1U });
    require(!invalid_language.ok()
            && invalid_language.error
                == elaboration::VerilogCoverageConditionError::InvalidLanguage,
        "VHDL must not enter Verilog condition decomposition");
    auto invalid_source = source;
    ++invalid_source.identity.content_bytes;
    const auto unauthenticated
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &assertion, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &invalid_source, 1U });
    require(!unauthenticated.ok() && unauthenticated.points.empty()
            && unauthenticated.error
                == elaboration::VerilogCoverageConditionError::InvalidSourceIdentity,
        "unauthenticated condition sources must fail transactionally");
    auto unnamed_source = source;
    unnamed_source.source_name.clear();
    const auto unnamed = elaboration::discover_verilog_coverage_conditions(
        std::span { &assertion, 1U },
        frontend::Language::SystemVerilog2017,
        std::span { &unnamed_source, 1U });
    require(!unnamed.ok()
            && unnamed.error
                == elaboration::VerilogCoverageConditionError::EmptySourceName,
        "empty source mapping names must be rejected");
    const std::vector duplicate_sources { source, source };
    const auto duplicate_source
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &assertion, 1U },
            frontend::Language::SystemVerilog2017, duplicate_sources);
    require(!duplicate_source.ok()
            && duplicate_source.error
                == elaboration::VerilogCoverageConditionError::DuplicateSourceName,
        "ambiguous condition source mappings must be rejected");

    auto missing = assertion;
    missing.condition = { };
    const auto missing_condition
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &missing, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(!missing_condition.ok()
            && missing_condition.error
                == elaboration::VerilogCoverageConditionError::MissingDecisionCondition,
        "a malformed decision without a condition must be rejected");
    auto malformed = assertion;
    malformed.condition.operands.pop_back();
    const auto malformed_expression
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &malformed, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(!malformed_expression.ok()
            && malformed_expression.error
                == elaboration::VerilogCoverageConditionError::MalformedLogicalExpression,
        "logical decomposition requires exactly two retained operands");
    auto malformed_negation = negated_composite;
    malformed_negation.condition.operands.clear();
    const auto malformed_unary
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &malformed_negation, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(!malformed_unary.ok()
            && malformed_unary.error
                == elaboration::VerilogCoverageConditionError::MalformedLogicalExpression,
        "logical negation requires exactly one retained operand");
    auto unknown = assertion;
    unknown.condition.operands.front().span.source_name = "unknown.sv";
    const auto unknown_source
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &unknown, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(!unknown_source.ok()
            && unknown_source.error
                == elaboration::VerilogCoverageConditionError::UnknownConditionSource,
        "atoms without authenticated source ownership must be rejected");
    auto outside = assertion;
    outside.condition.operands.front().span.end.offset = contents.size() + 1U;
    const auto outside_source
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &outside, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U });
    require(!outside_source.ok()
            && outside_source.error
                == elaboration::VerilogCoverageConditionError::InvalidConditionSpan,
        "atomic spans outside authenticated source bytes must be rejected");
    const std::vector duplicate_statements { assertion, assertion };
    const auto duplicate = elaboration::discover_verilog_coverage_conditions(
        duplicate_statements, frontend::Language::SystemVerilog2017,
        std::span { &source, 1U });
    require(!duplicate.ok() && duplicate.points.empty()
            && duplicate.error
                == elaboration::VerilogCoverageConditionError::DuplicatePoint,
        "duplicate atomic identities must not publish partial output");

    auto limits = elaboration::VerilogCoverageConditionLimits { };
    limits.maximum_conditions = 1U;
    const auto condition_limit
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &assertion, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U }, limits);
    require(!condition_limit.ok()
            && condition_limit.error
                == elaboration::VerilogCoverageConditionError::ResourceLimit,
        "condition count must obey its configured ceiling");
    limits = { };
    limits.maximum_expression_nodes = 1U;
    const auto expression_limit
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &assertion, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U }, limits);
    require(!expression_limit.ok()
            && expression_limit.error
                == elaboration::VerilogCoverageConditionError::ResourceLimit,
        "expression traversal must be bounded before publication");
    limits = { };
    limits.maximum_path_steps = 1U;
    const auto path_limit = elaboration::discover_verilog_coverage_conditions(
        std::span { &assertion, 1U },
        frontend::Language::SystemVerilog2017,
        std::span { &source, 1U }, limits);
    require(!path_limit.ok()
            && path_limit.error
                == elaboration::VerilogCoverageConditionError::ResourceLimit,
        "retained short-circuit paths must obey a total-step ceiling");
    limits = { };
    limits.maximum_expression_nesting = 1U;
    const auto nesting_limit
        = elaboration::discover_verilog_coverage_conditions(
            std::span { &assertion, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U }, limits);
    require(!nesting_limit.ok()
            && nesting_limit.error
                == elaboration::VerilogCoverageConditionError::ResourceLimit,
        "logical expression nesting must be bounded");
    require(elaboration::kVerilogCoverageConditionDiagnostic
            == "FSIM-COV-015",
        "Verilog condition decomposition diagnostic identity must remain stable");
}

} // namespace

int main()
{
    test_decomposition_and_structure();
    test_all_retained_profiles_and_relocation();
    test_immediate_assertion_and_rejections();
    return 0;
}
