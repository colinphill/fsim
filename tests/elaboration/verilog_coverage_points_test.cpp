// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/verilog_coverage_points.hpp"

#include "fsim/frontend/parser.hpp"

#include <algorithm>
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

fsim::elaboration::VerilogCoverageSource make_source(
    const std::filesystem::path& root, const std::filesystem::path& source,
    const std::string_view contents)
{
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, source, bytes(contents));
    require(identity.ok(), "test source identity must be valid");
    return fsim::elaboration::VerilogCoverageSource {
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
    const fsim::elaboration::VerilogCoveragePointResult& result)
{
    std::vector<fsim::runtime::CodeCoveragePointId> ids;
    ids.reserve(result.points.size());
    for (const auto& point : result.points) {
        ids.push_back(point.id);
    }
    return ids;
}

void test_verilog_and_systemverilog_discovery()
{
    using namespace fsim;
    constexpr std::string_view source_text = R"(module statement_points;
  logic a;
  logic b;
  initial begin : body
    integer declaration_only;
    a = 1'b0;
    if (b) begin
      a = 1'b1;
    end else a = 1'b0;
    case (a)
      1'b0: b = 1'b1;
      default: ;
    endcase
    for (int i = 0; i < 2; i++) a = b;
  end
endmodule
)";
    const auto root = checkout_root("checkout");
    const auto source_path = root / "rtl/statement_points.sv";
    const auto source = make_source(root, source_path, source_text);
    const auto parsed = frontend::parse_text(
        source.source_name, source_text, frontend::Language::SystemVerilog2017);
    require(parsed.ok() && parsed.design.units.size() == 1U
            && parsed.design.units.front().processes.size() == 1U,
        "independently authored SystemVerilog statement corpus must parse");

    const auto discovered = elaboration::discover_verilog_statement_points(
        parsed.design.units.front().processes.front().statements,
        frontend::Language::SystemVerilog2017,
        std::span { &source, 1U });
    require(discovered.ok() && discovered.points.size() == 8U,
        "every executable nested SystemVerilog statement must be discovered once");
    const std::vector expected_kinds {
        frontend::StatementKind::Assignment,
        frontend::StatementKind::If,
        frontend::StatementKind::Assignment,
        frontend::StatementKind::Assignment,
        frontend::StatementKind::Case,
        frontend::StatementKind::Assignment,
        frontend::StatementKind::Loop,
        frontend::StatementKind::Assignment,
    };
    require(std::ranges::equal(discovered.points, expected_kinds,
                { }, &elaboration::VerilogStatementCoveragePoint::statement_kind,
                std::identity { }),
        "statement discovery must preserve lexical depth-first order");
    const std::vector<std::uint64_t> expected_lines {
        6U,
        7U,
        8U,
        9U,
        10U,
        11U,
        14U,
        14U,
    };
    require(std::ranges::equal(discovered.points, expected_lines, { },
                &elaboration::VerilogStatementCoveragePoint::line,
                std::identity { }),
        "statement points must retain their one-based source start line");
    require(std::ranges::all_of(discovered.points,
                [](const auto& point) {
                    return point.language
                        == frontend::CodeCoverageLanguage::SystemVerilog
                        && point.source_index == 0U
                        && point.span.begin_offset < point.span.end_offset
                        && point.line > 0U;
                }),
        "SystemVerilog points must retain typed language, source, and span data");
    const auto declaration_begin = source_text.find("integer declaration_only");
    const auto declaration_end = source_text.find(';', declaration_begin) + 1U;
    require(declaration_begin != std::string_view::npos
            && declaration_end != 0U
            && std::ranges::none_of(discovered.points,
                [&](const auto& point) {
                    return point.span.begin_offset >= declaration_begin
                        && point.span.end_offset <= declaration_end;
                }),
        "A procedural declaration must not manufacture an executable point");
    require(elaboration::is_executable_verilog_statement_kind(
                frontend::StatementKind::If)
            && !elaboration::is_executable_verilog_statement_kind(
                frontend::StatementKind::Block)
            && !elaboration::is_executable_verilog_statement_kind(
                frontend::StatementKind::Null),
        "blocks, declarations, and null statements must not become points");

    const auto as_verilog = elaboration::discover_verilog_statement_points(
        parsed.design.units.front().processes.front().statements,
        frontend::Language::Verilog2005, std::span { &source, 1U });
    require(as_verilog.ok() && as_verilog.points.size() == discovered.points.size()
            && as_verilog.points.front().id != discovered.points.front().id
            && as_verilog.points.front().language
                == frontend::CodeCoverageLanguage::Verilog,
        "Verilog and SystemVerilog must share discovery rules but not identities");
    require(std::ranges::none_of(as_verilog.points,
                [&](const auto& point) {
                    return point.span.begin_offset >= declaration_begin
                        && point.span.end_offset <= declaration_end;
                }),
        "Every retained Verilog profile must exclude declarations");

    const auto& first = discovered.points.front();
    const auto direct = frontend::make_code_coverage_point_identity(
        source.identity, frontend::CodeCoverageLanguage::SystemVerilog,
        frontend::CodeCoverageConstructKind::Statement, first.span);
    require(direct.ok() && direct.identity == first.id,
        "discovery must use the canonical Change 4 point identity algorithm");
}

void test_relocation_and_root_order()
{
    using namespace fsim;
    constexpr std::string_view source_text
        = "module m; reg a; initial begin a = 0; a = 1; end endmodule\n";
    const auto root_a = checkout_root("relocated-a");
    const auto root_b = checkout_root("relocated-b");
    const auto source_a = make_source(root_a, root_a / "rtl/m.v", source_text);
    const auto source_b = make_source(root_b, root_b / "rtl/m.v", source_text);
    const auto parsed_a = frontend::parse_text(
        source_a.source_name, source_text, frontend::Language::Verilog2005);
    const auto parsed_b = frontend::parse_text(
        source_b.source_name, source_text, frontend::Language::Verilog2005);
    require(parsed_a.ok() && parsed_b.ok(),
        "relocated Verilog statement corpora must parse");
    const auto& roots_a
        = parsed_a.design.units.front().processes.front().statements;
    const auto& roots_b
        = parsed_b.design.units.front().processes.front().statements;
    require(roots_a.size() == 2U && roots_b.size() == 2U,
        "the Verilog corpus must retain two source statements");
    const auto first = elaboration::discover_verilog_statement_points(roots_a,
        frontend::Language::Verilog2005, std::span { &source_a, 1U });
    const auto relocated = elaboration::discover_verilog_statement_points(roots_b,
        frontend::Language::Verilog2005, std::span { &source_b, 1U });
    require(first.ok() && relocated.ok()
            && point_ids(first) == point_ids(relocated),
        "statement points must remain stable across checkout relocation");

    std::vector<frontend::Statement> reordered {
        roots_a[1], roots_a[0]
    };
    const auto reversed = elaboration::discover_verilog_statement_points(
        reordered, frontend::Language::Verilog2005,
        std::span { &source_a, 1U });
    auto first_ids = point_ids(first);
    auto reversed_ids = point_ids(reversed);
    const auto less = [](const runtime::CodeCoveragePointId left,
                          const runtime::CodeCoveragePointId right) {
        return left.high < right.high
            || (left.high == right.high && left.low < right.low);
    };
    std::ranges::sort(first_ids, less);
    std::ranges::sort(reversed_ids, less);
    require(reversed.ok() && first_ids == reversed_ids,
        "statement identity sets must not depend on root traversal order");
}

void test_rejections_and_limits()
{
    using namespace fsim;
    constexpr std::string_view contents = "0123456789";
    const auto root = checkout_root("negative");
    const auto source_path = root / "rtl/negative.sv";
    const auto source = make_source(root, source_path, contents);
    frontend::Statement statement;
    statement.kind = frontend::StatementKind::Assignment;
    statement.span = span(source.source_name, 1U, 4U);
    const std::vector statements { statement };

    const auto empty = elaboration::discover_verilog_statement_points(
        { }, frontend::Language::Verilog2005, { });
    require(empty.ok() && empty.points.empty(),
        "an empty statement forest must be a valid empty inventory");
    const auto vhdl = elaboration::discover_verilog_statement_points(statements,
        frontend::Language::Vhdl2008, std::span { &source, 1U });
    require(!vhdl.ok()
            && vhdl.error
                == elaboration::VerilogCoveragePointError::InvalidLanguage,
        "VHDL input must not enter Verilog statement discovery");

    auto invalid_source = source;
    ++invalid_source.identity.content_bytes;
    const auto unauthenticated
        = elaboration::discover_verilog_statement_points(statements,
            frontend::Language::SystemVerilog2017,
            std::span { &invalid_source, 1U });
    require(!unauthenticated.ok() && unauthenticated.points.empty()
            && unauthenticated.error
                == elaboration::VerilogCoveragePointError::InvalidSourceIdentity,
        "unauthenticated source metadata must fail transactionally");

    auto unnamed_source = source;
    unnamed_source.source_name.clear();
    const auto unnamed = elaboration::discover_verilog_statement_points(
        statements, frontend::Language::SystemVerilog2017,
        std::span { &unnamed_source, 1U });
    require(!unnamed.ok()
            && unnamed.error
                == elaboration::VerilogCoveragePointError::EmptySourceName,
        "an empty physical source mapping name must be rejected");

    const std::vector duplicate_sources { source, source };
    const auto duplicate_source
        = elaboration::discover_verilog_statement_points(statements,
            frontend::Language::SystemVerilog2017, duplicate_sources);
    require(!duplicate_source.ok()
            && duplicate_source.error
                == elaboration::VerilogCoveragePointError::DuplicateSourceName,
        "ambiguous physical source mappings must be rejected");

    auto unknown_statement = statement;
    unknown_statement.span.source_name = "unknown.sv";
    const auto unknown = elaboration::discover_verilog_statement_points(
        std::span { &unknown_statement, 1U },
        frontend::Language::SystemVerilog2017,
        std::span { &source, 1U });
    require(!unknown.ok()
            && unknown.error
                == elaboration::VerilogCoveragePointError::UnknownStatementSource,
        "a statement without an authenticated source mapping must be rejected");

    auto outside_statement = statement;
    outside_statement.span.end.offset = contents.size() + 1U;
    const auto outside = elaboration::discover_verilog_statement_points(
        std::span { &outside_statement, 1U },
        frontend::Language::SystemVerilog2017,
        std::span { &source, 1U });
    require(!outside.ok()
            && outside.error
                == elaboration::VerilogCoveragePointError::InvalidStatementSpan,
        "a statement span outside authenticated source bytes must be rejected");

    const std::vector duplicate_statements { statement, statement };
    const auto duplicate = elaboration::discover_verilog_statement_points(
        duplicate_statements, frontend::Language::SystemVerilog2017,
        std::span { &source, 1U });
    require(!duplicate.ok() && duplicate.points.empty()
            && duplicate.error
                == elaboration::VerilogCoveragePointError::DuplicatePoint,
        "duplicate statement points must fail without publishing partial output");

    const auto statement_limit
        = elaboration::discover_verilog_statement_points(statements,
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U }, { 1U, 0U, 1U });
    require(!statement_limit.ok()
            && statement_limit.error
                == elaboration::VerilogCoveragePointError::ResourceLimit,
        "the statement-count ceiling must be enforced before allocation");
    const auto source_limit
        = elaboration::discover_verilog_statement_points(statements,
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U }, { 0U, 1U, 1U });
    require(!source_limit.ok()
            && source_limit.error
                == elaboration::VerilogCoveragePointError::ResourceLimit,
        "the authenticated-source ceiling must be enforced before allocation");

    frontend::Statement block;
    block.kind = frontend::StatementKind::Block;
    block.statements.push_back(statement);
    const auto nesting_limit
        = elaboration::discover_verilog_statement_points(
            std::span { &block, 1U },
            frontend::Language::SystemVerilog2017,
            std::span { &source, 1U }, { 1U, 2U, 1U });
    require(!nesting_limit.ok()
            && nesting_limit.error
                == elaboration::VerilogCoveragePointError::ResourceLimit,
        "the nesting ceiling must bound adversarial statement trees");
    require(elaboration::kVerilogCoveragePointDiagnostic == "FSIM-COV-004",
        "statement discovery diagnostic identity must remain stable");
}

} // namespace

int main()
{
    test_verilog_and_systemverilog_discovery();
    test_relocation_and_root_order();
    test_rejections_and_limits();
    return 0;
}
