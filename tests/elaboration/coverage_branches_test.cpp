// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_branches.hpp"

#include "fsim/frontend/parser.hpp"

#include <algorithm>
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

fsim::elaboration::CoverageBranchSource make_source(
    const std::filesystem::path& root, const std::filesystem::path& source,
    const std::string_view contents)
{
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, source, bytes(contents));
    require(identity.ok(), "test branch source identity must be valid");
    return fsim::elaboration::CoverageBranchSource {
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
    const fsim::elaboration::CoverageBranchResult& result)
{
    std::vector<fsim::runtime::CodeCoveragePointId> ids;
    ids.reserve(result.points.size());
    for (const auto& point : result.points) {
        ids.push_back(point.id);
    }
    return ids;
}

void test_systemverilog_arm_model()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(module branches;
  logic a;
  logic b;
  initial begin
    if (a) b = 1'b1;
    if (a) b = 1'b0; else b = 1'b1;
    case (a)
      1'b0: b = 1'b0;
      default: b = 1'b1;
    endcase
    case (a)
      1'b0: b = 1'b0;
    endcase
    while (a) b = 1'b1;
    forever b = 1'b0;
  end
endmodule
)";
    const auto root = checkout_root("systemverilog");
    const auto source = make_source(root, root / "rtl/branches.sv", source_text);
    const auto parsed = frontend::parse_text(source.source_name, source_text,
        frontend::Language::SystemVerilog2017);
    require(parsed.ok() && parsed.design.units.front().processes.size() == 1U,
        "independently authored SystemVerilog branch corpus must parse");
    const auto discovered = elaboration::discover_coverage_branch_points(
        parsed.design.units.front().processes.front().statements,
        frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &source, 1U });
    require(discovered.ok() && discovered.points.size() == 10U,
        "if, case, and conditional loop arms must be individually discovered");
    const std::vector expected_arms {
        frontend::CodeCoverageConstructKind::BranchTrueArm,
        frontend::CodeCoverageConstructKind::BranchImplicitArm,
        frontend::CodeCoverageConstructKind::BranchTrueArm,
        frontend::CodeCoverageConstructKind::BranchFalseArm,
        frontend::CodeCoverageConstructKind::BranchCaseArm,
        frontend::CodeCoverageConstructKind::BranchDefaultArm,
        frontend::CodeCoverageConstructKind::BranchCaseArm,
        frontend::CodeCoverageConstructKind::BranchImplicitArm,
        frontend::CodeCoverageConstructKind::BranchTrueArm,
        frontend::CodeCoverageConstructKind::BranchImplicitArm,
    };
    require(std::ranges::equal(discovered.points, expected_arms, { },
                &elaboration::CoverageBranchPoint::arm, std::identity { }),
        "explicit and implicit arm kinds must retain lexical decision order");
    require(discovered.points[0].decision
                == elaboration::CoverageDecisionKind::If
            && discovered.points[4].decision
                == elaboration::CoverageDecisionKind::Case
            && discovered.points[8].decision
                == elaboration::CoverageDecisionKind::Loop,
        "branch points must retain their decision family");
    require(std::ranges::all_of(discovered.points,
                [](const auto& point) {
                    return point.language
                            == frontend::CodeCoverageLanguage::SystemVerilog
                        && point.source_index == 0U
                        && point.span.begin_offset < point.span.end_offset;
                }),
        "every branch arm must retain typed language, source, and exact span");

    const auto as_verilog = elaboration::discover_coverage_branch_points(
        parsed.design.units.front().processes.front().statements,
        frontend::CodeCoverageLanguage::Verilog,
        std::span { &source, 1U });
    require(as_verilog.ok() && as_verilog.points.size() == 10U
            && as_verilog.points.front().id != discovered.points.front().id,
        "Verilog and SystemVerilog branch identities must remain distinct");
    const auto direct = frontend::make_code_coverage_point_identity(
        source.identity, frontend::CodeCoverageLanguage::SystemVerilog,
        discovered.points.front().arm, discovered.points.front().span);
    require(direct.ok() && direct.identity == discovered.points.front().id,
        "branch discovery must use the canonical Change 4 identity algorithm");
}

void test_vhdl_arms_and_relocation()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(entity branches is
  port (a : in bit; b : out bit);
end branches;
architecture rtl of branches is begin
  process begin
    if a = '1' then b <= '1'; else b <= '0'; end if;
    case a is
      when '0' => b <= '1';
      when others => b <= '0';
    end case;
    for i in 0 to 1 loop b <= a; end loop;
    loop exit; end loop;
    wait;
  end process;
end rtl;
)";
    const auto root_a = checkout_root("vhdl-a");
    const auto root_b = checkout_root("vhdl-b");
    const auto source_a
        = make_source(root_a, root_a / "rtl/branches.vhd", source_text);
    const auto source_b
        = make_source(root_b, root_b / "rtl/branches.vhd", source_text);
    const auto parse = [&](const elaboration::CoverageBranchSource& source) {
        return frontend::parse_text(source.source_name, source_text,
            frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008);
    };
    const auto parsed_a = parse(source_a);
    const auto parsed_b = parse(source_b);
    require(parsed_a.ok() && parsed_b.ok(),
        "independently authored relocated VHDL branch corpora must parse");
    const auto first = elaboration::discover_coverage_branch_points(
        parsed_a.design.units.back().processes.front().statements,
        frontend::CodeCoverageLanguage::Vhdl,
        std::span { &source_a, 1U });
    const auto relocated = elaboration::discover_coverage_branch_points(
        parsed_b.design.units.back().processes.front().statements,
        frontend::CodeCoverageLanguage::Vhdl,
        std::span { &source_b, 1U });
    require(first.ok() && first.points.size() == 6U && relocated.ok()
            && point_ids(first) == point_ids(relocated),
        "VHDL explicit/default/loop arms must be complete and relocatable");
    require(first.points.back().decision
                == elaboration::CoverageDecisionKind::Loop
            && first.points.back().arm
                == frontend::CodeCoverageConstructKind::BranchImplicitArm,
        "bounded VHDL loops need an implicit completion arm");
}

void test_rejections_and_limits()
{
    using namespace fsim;
    constexpr std::string_view contents = "0123456789";
    const auto root = checkout_root("negative");
    const auto source = make_source(root, root / "rtl/negative.sv", contents);
    frontend::Statement assignment;
    assignment.kind = frontend::StatementKind::Assignment;
    assignment.span = span(source.source_name, 2U, 4U);
    frontend::Statement decision;
    decision.kind = frontend::StatementKind::If;
    decision.span = span(source.source_name, 0U, 6U);
    decision.statements.push_back(assignment);
    const std::vector statements { decision };

    const auto invalid_language
        = elaboration::discover_coverage_branch_points(statements,
            static_cast<frontend::CodeCoverageLanguage>(255U),
            std::span { &source, 1U });
    require(!invalid_language.ok()
            && invalid_language.error
                == elaboration::CoverageBranchError::InvalidLanguage,
        "an unknown branch language must be rejected");
    auto invalid_source = source;
    ++invalid_source.identity.content_bytes;
    const auto unauthenticated
        = elaboration::discover_coverage_branch_points(statements,
            frontend::CodeCoverageLanguage::SystemVerilog,
            std::span { &invalid_source, 1U });
    require(!unauthenticated.ok() && unauthenticated.points.empty()
            && unauthenticated.error
                == elaboration::CoverageBranchError::InvalidSourceIdentity,
        "unauthenticated branch source metadata must fail transactionally");
    auto unnamed_source = source;
    unnamed_source.source_name.clear();
    const auto unnamed = elaboration::discover_coverage_branch_points(
        statements, frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &unnamed_source, 1U });
    require(!unnamed.ok()
            && unnamed.error
                == elaboration::CoverageBranchError::EmptySourceName,
        "an empty branch source mapping name must be rejected");
    const std::vector duplicate_sources { source, source };
    const auto duplicate_source
        = elaboration::discover_coverage_branch_points(statements,
            frontend::CodeCoverageLanguage::SystemVerilog,
            duplicate_sources);
    require(!duplicate_source.ok()
            && duplicate_source.error
                == elaboration::CoverageBranchError::DuplicateSourceName,
        "ambiguous branch source mappings must be rejected");
    auto missing = decision;
    missing.statements.clear();
    const auto missing_arm = elaboration::discover_coverage_branch_points(
        std::span { &missing, 1U },
        frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &source, 1U });
    require(!missing_arm.ok()
            && missing_arm.error
                == elaboration::CoverageBranchError::MissingExplicitArm,
        "a malformed decision without its explicit arm must be rejected");
    auto unknown = decision;
    unknown.statements.front().span.source_name = "unknown.sv";
    const auto unknown_source = elaboration::discover_coverage_branch_points(
        std::span { &unknown, 1U },
        frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &source, 1U });
    require(!unknown_source.ok()
            && unknown_source.error
                == elaboration::CoverageBranchError::UnknownArmSource,
        "an arm without an authenticated source mapping must be rejected");
    auto outside = decision;
    outside.statements.front().span.end.offset = contents.size() + 1U;
    const auto outside_source = elaboration::discover_coverage_branch_points(
        std::span { &outside, 1U },
        frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &source, 1U });
    require(!outside_source.ok()
            && outside_source.error
                == elaboration::CoverageBranchError::InvalidArmSpan,
        "an arm outside authenticated source bytes must be rejected");
    const std::vector duplicate_statements { decision, decision };
    const auto duplicate = elaboration::discover_coverage_branch_points(
        duplicate_statements, frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &source, 1U });
    require(!duplicate.ok() && duplicate.points.empty()
            && duplicate.error
                == elaboration::CoverageBranchError::DuplicatePoint,
        "duplicate arm identities must not publish partial output");
    const auto arm_limit = elaboration::discover_coverage_branch_points(
        statements, frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &source, 1U }, { 1U, 1U, 1U, 1U });
    require(!arm_limit.ok()
            && arm_limit.error
                == elaboration::CoverageBranchError::ResourceLimit,
        "branch arm count must obey its explicit ceiling");
    frontend::Statement block;
    block.kind = frontend::StatementKind::Block;
    block.statements.push_back(decision);
    const auto nesting = elaboration::discover_coverage_branch_points(
        std::span { &block, 1U },
        frontend::CodeCoverageLanguage::SystemVerilog,
        std::span { &source, 1U }, { 1U, 2U, 2U, 1U });
    require(!nesting.ok()
            && nesting.error == elaboration::CoverageBranchError::ResourceLimit,
        "branch discovery nesting must be bounded");
    require(elaboration::kCoverageBranchDiagnostic == "FSIM-COV-006",
        "branch discovery diagnostic identity must remain stable");
}

} // namespace

int main()
{
    test_systemverilog_arm_model();
    test_vhdl_arms_and_relocation();
    test_rejections_and_limits();
    return 0;
}
