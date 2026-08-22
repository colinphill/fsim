// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/vhdl_coverage_conditions.hpp"

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

fsim::elaboration::VhdlCoverageConditionSource make_source(
    const std::filesystem::path& root, const std::filesystem::path& source,
    const std::string_view contents)
{
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, source, bytes(contents));
    require(identity.ok(), "test VHDL condition source identity must be valid");
    return fsim::elaboration::VhdlCoverageConditionSource {
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

std::string_view point_text(const std::string_view source,
    const fsim::elaboration::VhdlCoverageConditionPoint& point)
{
    return source.substr(static_cast<std::size_t>(point.span.begin_offset),
        static_cast<std::size_t>(
            point.span.end_offset - point.span.begin_offset));
}

std::vector<fsim::runtime::CodeCoveragePointId> point_ids(
    const fsim::elaboration::VhdlCoverageConditionResult& result)
{
    std::vector<fsim::runtime::CodeCoveragePointId> ids;
    ids.reserve(result.points.size());
    for (const auto& point : result.points) {
        ids.push_back(point.id);
    }
    return ids;
}

void test_vhdl_operator_and_decision_model()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(entity conditions is
  port (a : in boolean; b : in boolean; c : in boolean; d : in boolean);
end conditions;
architecture rtl of conditions is
begin
  process
  begin
    if ((a and b) or ((not c) and (d = true))) then null; end if;
    assert ((a nand b) xor (c nor d));
    while (a xnor b) loop exit; end loop;
    wait until a or b;
  end process;
end rtl;
)";
    const auto root = checkout_root("vhdl-conditions");
    const auto source = make_source(
        root, root / "rtl/conditions.vhd", source_text);
    auto parsed = frontend::parse_text(source.source_name, source_text,
        frontend::Language::Vhdl2008, frontend::VhdlStandard::Vhdl2008);
    require(parsed.ok() && parsed.design.units.back().processes.size() == 1U,
        "independently authored VHDL Boolean condition corpus must parse");
    const auto& statements
        = parsed.design.units.back().processes.front().statements;
    require(statements.size() == 4U,
        "VHDL condition corpus must retain four decision statements");
    const auto root_text = statements.front().condition.text;
    const auto root_operands = statements.front().condition.operands.size();

    const auto discovered = elaboration::discover_vhdl_coverage_conditions(
        statements, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U });
    require(discovered.ok() && discovered.points.size() == 12U,
        "VHDL if, assertion, while, and wait decisions must yield twelve atoms");
    const std::array expected_text {
        std::string_view { "a" },
        std::string_view { "b" },
        std::string_view { "c" },
        std::string_view { "(d = true)" },
        std::string_view { "a" },
        std::string_view { "b" },
        std::string_view { "c" },
        std::string_view { "d" },
        std::string_view { "a" },
        std::string_view { "b" },
        std::string_view { "a" },
        std::string_view { "b" },
    };
    for (std::size_t index = 0U; index < expected_text.size(); ++index) {
        const auto actual = point_text(source_text, discovered.points[index]);
        require(actual == expected_text[index],
            std::string { "VHDL atom " } + std::to_string(index)
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
            && discovered.points[2].evaluation_path.back().logical_operator == elaboration::CoverageConditionLogicalOperator::Not && discovered.points[4].evaluation_path.back().logical_operator == elaboration::CoverageConditionLogicalOperator::Nand && discovered.points[6].evaluation_path.back().logical_operator == elaboration::CoverageConditionLogicalOperator::Nor && discovered.points[8].evaluation_path.front().logical_operator == elaboration::CoverageConditionLogicalOperator::Xnor,
        "VHDL paths must retain each language-specific Boolean operator");
    require(discovered.points[0].decision
                == elaboration::CoverageConditionDecisionKind::If
            && discovered.points[4].decision
                == elaboration::CoverageConditionDecisionKind::ImmediateAssertion
            && discovered.points[8].decision
                == elaboration::CoverageConditionDecisionKind::Loop
            && discovered.points[10].decision
                == elaboration::CoverageConditionDecisionKind::WaitUntil
            && discovered.points[10].decision_index == 3U,
        "VHDL decision families and lexical ordinals must remain explicit");
    require(std::ranges::all_of(discovered.points,
                [](const auto& point) {
                    return point.language
                        == frontend::CodeCoverageLanguage::Vhdl
                        && point.source_index == 0U;
                }),
        "every VHDL atom must retain typed language and source ownership");
    require(statements.front().condition.text == root_text
            && statements.front().condition.operands.size() == root_operands,
        "VHDL decomposition must not mutate the retained expression tree");
    const auto direct = frontend::make_code_coverage_point_identity(
        source.identity, frontend::CodeCoverageLanguage::Vhdl,
        frontend::CodeCoverageConstructKind::AtomicCondition,
        discovered.points.front().span);
    require(direct.ok() && direct.identity == discovered.points.front().id,
        "VHDL atoms must use the shared canonical point identity");
}

void test_all_retained_vhdl_profiles_and_relocation()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(entity profile_conditions is
  port (a : in boolean; b : in boolean; c : in boolean);
end profile_conditions;
architecture rtl of profile_conditions is
begin
  process
  begin
    if (a and b) or (not c) then null; end if;
    while a or b loop exit; end loop;
    wait until a and b;
  end process;
end rtl;
)";
    constexpr std::array standards {
        frontend::VhdlStandard::Vhdl1987,
        frontend::VhdlStandard::Vhdl1993,
        frontend::VhdlStandard::Vhdl2000,
        frontend::VhdlStandard::Vhdl2002,
        frontend::VhdlStandard::Vhdl2008,
    };
    for (const auto standard : standards) {
        const auto root_a = checkout_root("vhdl-profile-a");
        const auto root_b = checkout_root("vhdl-profile-b");
        const auto relative
            = std::filesystem::path { "rtl/profile_conditions.vhd" };
        const auto source_a
            = make_source(root_a, root_a / relative, source_text);
        const auto source_b
            = make_source(root_b, root_b / relative, source_text);
        const auto parsed_a = frontend::parse_text(source_a.source_name,
            source_text, frontend::Language::Vhdl2008, standard);
        const auto parsed_b = frontend::parse_text(source_b.source_name,
            source_text, frontend::Language::Vhdl2008, standard);
        require(parsed_a.ok() && parsed_b.ok(),
            "every retained VHDL profile must parse the Boolean corpus");
        const auto first = elaboration::discover_vhdl_coverage_conditions(
            parsed_a.design.units.back().processes.front().statements,
            frontend::Language::Vhdl2008, standard,
            std::span { &source_a, 1U });
        const auto relocated
            = elaboration::discover_vhdl_coverage_conditions(
                parsed_b.design.units.back().processes.front().statements,
                frontend::Language::Vhdl2008, standard,
                std::span { &source_b, 1U });
        require(first.ok() && first.points.size() == 7U && relocated.ok()
                && point_ids(first) == point_ids(relocated),
            "VHDL atomic identities must be revision-complete and relocation independent");
    }
}

void test_vhdl_rejections_and_bounds()
{
    using namespace fsim;
    constexpr std::string_view contents = "0123456789abcdef";
    const auto root = checkout_root("vhdl-negative");
    const auto source
        = make_source(root, root / "rtl/negative.vhd", contents);
    frontend::Expression left { frontend::ExpressionKind::Identifier, "a", { },
        span(source.source_name, 2U, 3U) };
    frontend::Expression right { frontend::ExpressionKind::Identifier, "b", { },
        span(source.source_name, 5U, 6U) };
    frontend::Statement decision;
    decision.kind = frontend::StatementKind::If;
    decision.span = span(source.source_name, 0U, 7U);
    decision.condition = frontend::Expression {
        frontend::ExpressionKind::Binary, "and", { left, right },
        span(source.source_name, 2U, 6U)
    };

    auto condition_conversion = decision;
    condition_conversion.condition = frontend::Expression {
        frontend::ExpressionKind::Unary, "??", { left },
        span(source.source_name, 0U, 3U)
    };
    const auto converted = elaboration::discover_vhdl_coverage_conditions(
        std::span { &condition_conversion, 1U },
        frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008,
        std::span { &source, 1U });
    require(converted.ok() && converted.points.size() == 1U
            && converted.points.front().expression_kind
                == frontend::ExpressionKind::Unary
            && converted.points.front().evaluation_path.empty(),
        "the VHDL-2008 condition conversion must remain one source-exact atom");

    const auto invalid_common_language
        = elaboration::discover_coverage_conditions(
            std::span { &decision, 1U },
            static_cast<frontend::CodeCoverageLanguage>(255U),
            std::span { &source, 1U });
    require(!invalid_common_language.ok()
            && invalid_common_language.error
                == elaboration::CoverageConditionError::InvalidLanguage,
        "the common condition engine must reject unknown language identities");

    const auto invalid_language
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &decision, 1U },
            frontend::Language::SystemVerilog2017,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U });
    require(!invalid_language.ok()
            && invalid_language.error
                == elaboration::VhdlCoverageConditionError::InvalidLanguage,
        "non-VHDL language families must be rejected");
    const auto invalid_standard
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &decision, 1U }, frontend::Language::Vhdl2008,
            static_cast<frontend::VhdlStandard>(255U),
            std::span { &source, 1U });
    require(!invalid_standard.ok()
            && invalid_standard.error
                == elaboration::VhdlCoverageConditionError::InvalidStandard,
        "unknown VHDL revisions must be rejected before discovery");
    auto invalid_source = source;
    ++invalid_source.identity.content_bytes;
    const auto unauthenticated
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &decision, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &invalid_source, 1U });
    require(!unauthenticated.ok() && unauthenticated.points.empty()
            && unauthenticated.error
                == elaboration::VhdlCoverageConditionError::InvalidSourceIdentity,
        "unauthenticated VHDL sources must fail transactionally");
    auto missing = decision;
    missing.condition = { };
    const auto missing_condition
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &missing, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U });
    require(!missing_condition.ok()
            && missing_condition.error
                == elaboration::VhdlCoverageConditionError::MissingDecisionCondition,
        "a VHDL decision without a condition must be rejected");
    auto malformed = decision;
    malformed.condition.operands.pop_back();
    const auto malformed_binary
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &malformed, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U });
    require(!malformed_binary.ok()
            && malformed_binary.error
                == elaboration::VhdlCoverageConditionError::MalformedLogicalExpression,
        "VHDL binary logical operators require two retained operands");
    auto malformed_not = decision;
    malformed_not.condition = frontend::Expression {
        frontend::ExpressionKind::Unary, "not", { },
        span(source.source_name, 2U, 3U)
    };
    const auto malformed_unary
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &malformed_not, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U });
    require(!malformed_unary.ok()
            && malformed_unary.error
                == elaboration::VhdlCoverageConditionError::MalformedLogicalExpression,
        "VHDL not requires one retained operand");
    auto unknown = decision;
    unknown.condition.operands.front().span.source_name = "unknown.vhd";
    const auto unknown_source
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &unknown, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U });
    require(!unknown_source.ok()
            && unknown_source.error
                == elaboration::VhdlCoverageConditionError::UnknownConditionSource,
        "VHDL atoms require authenticated source ownership");
    auto outside = decision;
    outside.condition.operands.front().span.end.offset = contents.size() + 1U;
    const auto outside_source
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &outside, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U });
    require(!outside_source.ok()
            && outside_source.error
                == elaboration::VhdlCoverageConditionError::InvalidConditionSpan,
        "VHDL atomic spans must stay inside authenticated bytes");
    const std::vector duplicates { decision, decision };
    const auto duplicate = elaboration::discover_vhdl_coverage_conditions(
        duplicates, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U });
    require(!duplicate.ok() && duplicate.points.empty()
            && duplicate.error
                == elaboration::VhdlCoverageConditionError::DuplicatePoint,
        "duplicate VHDL condition identities must not publish partial output");

    auto limits = elaboration::VhdlCoverageConditionLimits { };
    limits.maximum_conditions = 1U;
    const auto condition_limit
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &decision, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U }, limits);
    require(!condition_limit.ok()
            && condition_limit.error
                == elaboration::VhdlCoverageConditionError::ResourceLimit,
        "VHDL atom count must obey the shared ceiling");
    limits = { };
    limits.maximum_expression_nesting = 1U;
    const auto nesting_limit
        = elaboration::discover_vhdl_coverage_conditions(
            std::span { &decision, 1U }, frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U }, limits);
    require(!nesting_limit.ok()
            && nesting_limit.error
                == elaboration::VhdlCoverageConditionError::ResourceLimit,
        "VHDL logical nesting must obey the shared ceiling");

    frontend::Statement unconditional_loop;
    unconditional_loop.kind = frontend::StatementKind::Loop;
    unconditional_loop.condition = frontend::Expression {
        frontend::ExpressionKind::BooleanLiteral, "true", { },
        span(source.source_name, 0U, 1U)
    };
    frontend::Statement bare_wait = unconditional_loop;
    bare_wait.kind = frontend::StatementKind::WaitUntil;
    const std::vector unconditional { unconditional_loop, bare_wait };
    const auto omitted = elaboration::discover_vhdl_coverage_conditions(
        unconditional, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U });
    require(omitted.ok() && omitted.points.empty(),
        "synthetic unconditional loop and bare-wait conditions must not manufacture atoms");
    require(elaboration::kVhdlCoverageConditionDiagnostic == "FSIM-COV-016",
        "VHDL condition diagnostic identity must remain stable");
}

} // namespace

int main()
{
    test_vhdl_operator_and_decision_model();
    test_all_retained_vhdl_profiles_and_relocation();
    test_vhdl_rejections_and_bounds();
    return 0;
}
