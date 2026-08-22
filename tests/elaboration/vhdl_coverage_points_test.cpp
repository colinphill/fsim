// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/vhdl_coverage_points.hpp"

#include "fsim/frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

fsim::elaboration::VhdlCoverageSource make_source(
    const std::filesystem::path& root, const std::filesystem::path& source,
    const std::string_view contents)
{
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, source, bytes(contents));
    require(identity.ok(), "test VHDL source identity must be valid");
    return fsim::elaboration::VhdlCoverageSource {
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
    const fsim::elaboration::VhdlCoveragePointResult& result)
{
    std::vector<fsim::runtime::CodeCoveragePointId> ids;
    ids.reserve(result.points.size());
    for (const auto& point : result.points) {
        ids.push_back(point.id);
    }
    return ids;
}

constexpr std::array kRetainedStandards {
    fsim::frontend::VhdlStandard::Vhdl1987,
    fsim::frontend::VhdlStandard::Vhdl1993,
    fsim::frontend::VhdlStandard::Vhdl2000,
    fsim::frontend::VhdlStandard::Vhdl2002,
    fsim::frontend::VhdlStandard::Vhdl2008,
};

void test_all_profiles_and_statement_families()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(entity statement_points is
  port (b : in bit; a : out bit);
end statement_points;

architecture rtl of statement_points is
begin
  worker : process
    variable declaration_only : integer := 0;
  begin
    a <= '0';
    if b = '1' then
      a <= '1';
    else
      a <= '0';
    end if;
    case b is
      when '0' => a <= '1';
      when others => null;
    end case;
    for i in 0 to 1 loop
      a <= b;
    end loop;
    wait;
  end process worker;
  a <= b;
  assert b = '0' report "concurrent" severity note;
end rtl;
)";
    const auto root = checkout_root("profiles");
    const auto source_path = root / "rtl/statement_points.vhd";
    const auto source = make_source(root, source_path, source_text);
    std::vector<runtime::CodeCoveragePointId> baseline_process_ids;
    std::vector<runtime::CodeCoveragePointId> baseline_concurrent_ids;
    const auto declaration_begin
        = source_text.find("variable declaration_only");
    const auto declaration_end
        = source_text.find(';', declaration_begin) + 1U;
    require(declaration_begin != std::string_view::npos
            && declaration_end != 0U,
        "The VHDL exclusion fixture must retain its declaration span");

    for (const auto standard : kRetainedStandards) {
        const auto parsed = frontend::parse_text(source.source_name,
            source_text, frontend::Language::Vhdl2008, standard);
        require(parsed.ok() && parsed.design.units.size() == 2U,
            "the common VHDL-87 corpus must parse in every retained profile");
        const auto& architecture = parsed.design.units.back();
        require(architecture.processes.size() == 1U
                && architecture.concurrent_statements.size() == 2U,
            "VHDL process and concurrent statement ownership must be retained");
        const auto process = elaboration::discover_vhdl_statement_points(
            architecture.processes.front().statements,
            frontend::Language::Vhdl2008, standard,
            std::span { &source, 1U });
        const auto concurrent = elaboration::discover_vhdl_statement_points(
            architecture.concurrent_statements,
            frontend::Language::Vhdl2008, standard,
            std::span { &source, 1U });
        require(process.ok() && process.points.size() == 9U
                && concurrent.ok() && concurrent.points.size() == 2U,
            "VHDL sequential and concurrent executable points must be complete");
        const std::vector expected_process_kinds {
            frontend::StatementKind::Assignment,
            frontend::StatementKind::If,
            frontend::StatementKind::Assignment,
            frontend::StatementKind::Assignment,
            frontend::StatementKind::Case,
            frontend::StatementKind::Assignment,
            frontend::StatementKind::Loop,
            frontend::StatementKind::Assignment,
            frontend::StatementKind::WaitUntil,
        };
        require(std::ranges::equal(process.points, expected_process_kinds,
                    { },
                    &elaboration::VhdlStatementCoveragePoint::statement_kind,
                    std::identity { })
                && concurrent.points[0].statement_kind
                    == frontend::StatementKind::Assignment
                && concurrent.points[1].statement_kind
                    == frontend::StatementKind::Assert,
            "VHDL discovery must preserve lexical executable-statement order");
        const std::vector<std::uint64_t> expected_process_lines {
            10U,
            11U,
            12U,
            14U,
            16U,
            17U,
            20U,
            21U,
            23U,
        };
        const std::vector<std::uint64_t> expected_concurrent_lines {
            25U,
            26U,
        };
        require(std::ranges::equal(process.points, expected_process_lines, { },
                    &elaboration::VhdlStatementCoveragePoint::line,
                    std::identity { })
                && std::ranges::equal(concurrent.points,
                    expected_concurrent_lines, { },
                    &elaboration::VhdlStatementCoveragePoint::line,
                    std::identity { }),
            "VHDL statement points must retain one-based source start lines");
        require(std::ranges::none_of(process.points,
                    [&](const auto& point) {
                        return point.span.begin_offset >= declaration_begin
                            && point.span.end_offset <= declaration_end;
                    }),
            "Every retained VHDL profile must exclude declarations");
        if (baseline_process_ids.empty()) {
            baseline_process_ids = point_ids(process);
            baseline_concurrent_ids = point_ids(concurrent);
        } else {
            require(point_ids(process) == baseline_process_ids
                    && point_ids(concurrent) == baseline_concurrent_ids,
                "a source point identity must be stable across VHDL revisions");
        }
    }

    require(elaboration::is_executable_vhdl_statement_kind(
                frontend::StatementKind::Assert)
            && !elaboration::is_executable_vhdl_statement_kind(
                frontend::StatementKind::Block)
            && !elaboration::is_executable_vhdl_statement_kind(
                frontend::StatementKind::Null)
            && !elaboration::is_executable_vhdl_statement_kind(
                frontend::StatementKind::TaskCall),
        "VHDL execution kinds must exclude blocks, nulls, and SV-only forms");
}

void test_relocation_and_direct_identity()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(entity e is end e;
architecture rtl of e is begin
  process begin
    assert true;
    wait;
  end process;
end rtl;
)";
    const auto root_a = checkout_root("relocated-a");
    const auto root_b = checkout_root("relocated-b");
    const auto source_a
        = make_source(root_a, root_a / "rtl/e.vhd", source_text);
    const auto source_b
        = make_source(root_b, root_b / "rtl/e.vhd", source_text);
    const auto parsed_a = frontend::parse_text(source_a.source_name,
        source_text, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008);
    const auto parsed_b = frontend::parse_text(source_b.source_name,
        source_text, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008);
    require(parsed_a.ok() && parsed_b.ok(),
        "relocated VHDL statement corpora must parse");
    const auto& statements_a
        = parsed_a.design.units.back().processes.front().statements;
    const auto& statements_b
        = parsed_b.design.units.back().processes.front().statements;
    const auto first = elaboration::discover_vhdl_statement_points(statements_a,
        frontend::Language::Vhdl2008, frontend::VhdlStandard::Vhdl2008,
        std::span { &source_a, 1U });
    const auto relocated
        = elaboration::discover_vhdl_statement_points(statements_b,
            frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source_b, 1U });
    require(first.ok() && first.points.size() == 2U && relocated.ok()
            && point_ids(first) == point_ids(relocated),
        "VHDL statement points must remain stable across checkout relocation");
    const auto direct = frontend::make_code_coverage_point_identity(
        source_a.identity, frontend::CodeCoverageLanguage::Vhdl,
        frontend::CodeCoverageConstructKind::Statement,
        first.points.front().span);
    require(direct.ok() && direct.identity == first.points.front().id,
        "VHDL discovery must use the canonical Change 4 identity algorithm");
}

void test_rejections_and_limits()
{
    using namespace fsim;
    constexpr std::string_view contents = "0123456789";
    const auto root = checkout_root("negative");
    const auto source_path = root / "rtl/negative.vhd";
    const auto source = make_source(root, source_path, contents);
    frontend::Statement statement;
    statement.kind = frontend::StatementKind::Assignment;
    statement.span = span(source.source_name, 1U, 4U);
    const std::vector statements { statement };

    const auto empty = elaboration::discover_vhdl_statement_points({ },
        frontend::Language::Vhdl2008, frontend::VhdlStandard::Vhdl1987, { });
    require(empty.ok() && empty.points.empty(),
        "an empty VHDL statement forest must be a valid empty inventory");
    const auto wrong_language
        = elaboration::discover_vhdl_statement_points(statements,
            frontend::Language::SystemVerilog2017,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &source, 1U });
    require(!wrong_language.ok()
            && wrong_language.error
                == elaboration::VhdlCoveragePointError::InvalidLanguage,
        "non-VHDL input must not enter VHDL statement discovery");
    const auto wrong_standard
        = elaboration::discover_vhdl_statement_points(statements,
            frontend::Language::Vhdl2008,
            static_cast<frontend::VhdlStandard>(255U),
            std::span { &source, 1U });
    require(!wrong_standard.ok()
            && wrong_standard.error
                == elaboration::VhdlCoveragePointError::InvalidStandard,
        "an unknown VHDL revision must be rejected");

    auto invalid_source = source;
    ++invalid_source.identity.content_bytes;
    const auto unauthenticated
        = elaboration::discover_vhdl_statement_points(statements,
            frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008,
            std::span { &invalid_source, 1U });
    require(!unauthenticated.ok() && unauthenticated.points.empty()
            && unauthenticated.error
                == elaboration::VhdlCoveragePointError::InvalidSourceIdentity,
        "unauthenticated VHDL source metadata must fail transactionally");
    const std::vector duplicate_sources { source, source };
    const auto duplicate_source
        = elaboration::discover_vhdl_statement_points(statements,
            frontend::Language::Vhdl2008,
            frontend::VhdlStandard::Vhdl2008, duplicate_sources);
    require(!duplicate_source.ok()
            && duplicate_source.error
                == elaboration::VhdlCoveragePointError::DuplicateSourceName,
        "ambiguous VHDL physical source mappings must be rejected");

    auto unknown_statement = statement;
    unknown_statement.span.source_name = "unknown.vhd";
    const auto unknown = elaboration::discover_vhdl_statement_points(
        std::span { &unknown_statement, 1U }, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U });
    require(!unknown.ok()
            && unknown.error
                == elaboration::VhdlCoveragePointError::UnknownStatementSource,
        "an unmapped VHDL statement source must be rejected");
    auto outside_statement = statement;
    outside_statement.span.end.offset = contents.size() + 1U;
    const auto outside = elaboration::discover_vhdl_statement_points(
        std::span { &outside_statement, 1U }, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U });
    require(!outside.ok()
            && outside.error
                == elaboration::VhdlCoveragePointError::InvalidStatementSpan,
        "an out-of-source VHDL statement span must be rejected");

    const std::vector duplicate_statements { statement, statement };
    const auto duplicate = elaboration::discover_vhdl_statement_points(
        duplicate_statements, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U });
    require(!duplicate.ok() && duplicate.points.empty()
            && duplicate.error
                == elaboration::VhdlCoveragePointError::DuplicatePoint,
        "duplicate VHDL points must not publish partial output");
    const auto source_limit = elaboration::discover_vhdl_statement_points(
        statements, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U },
        { 0U, 1U, 1U });
    const auto statement_limit = elaboration::discover_vhdl_statement_points(
        statements, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U },
        { 1U, 0U, 1U });
    require(!source_limit.ok() && !statement_limit.ok()
            && source_limit.error
                == elaboration::VhdlCoveragePointError::ResourceLimit
            && statement_limit.error
                == elaboration::VhdlCoveragePointError::ResourceLimit,
        "VHDL source and statement ceilings must apply before allocation");
    frontend::Statement block;
    block.kind = frontend::StatementKind::Block;
    block.statements.push_back(statement);
    const auto nesting_limit = elaboration::discover_vhdl_statement_points(
        std::span { &block, 1U }, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008, std::span { &source, 1U },
        { 1U, 2U, 1U });
    require(!nesting_limit.ok()
            && nesting_limit.error
                == elaboration::VhdlCoveragePointError::ResourceLimit,
        "VHDL nesting must be bounded for adversarial statement trees");
    require(elaboration::kVhdlCoveragePointDiagnostic == "FSIM-COV-005",
        "VHDL statement discovery diagnostic identity must remain stable");
}

} // namespace

int main()
{
    test_all_profiles_and_statement_families();
    test_relocation_and_direct_identity();
    test_rejections_and_limits();
    return 0;
}
