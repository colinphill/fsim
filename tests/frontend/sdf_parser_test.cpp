// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using fsim::frontend::Diagnostic;
using fsim::frontend::SdfHeaderKind;
using fsim::frontend::SdfHeaderRecord;
using fsim::frontend::SdfParseResult;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string { message });
    }
}

const SdfHeaderRecord& require_header(const SdfParseResult& result,
    const SdfHeaderKind kind)
{
    const auto* header = result.file.find_header(kind);
    require(header != nullptr, "expected SDF header is missing");
    return *header;
}

const Diagnostic& require_diagnostic(const SdfParseResult& result,
    const std::string_view code)
{
    const auto diagnostic = std::ranges::find_if(result.diagnostics, [&](const Diagnostic& value) {
        return value.code == code;
    });
    if (diagnostic == result.diagnostics.end()) {
        std::string message = "expected SDF parser diagnostic is missing: ";
        message += code;
        message += " (observed";
        for (const auto& observed : result.diagnostics) {
            message += ' ';
            message += observed.code;
        }
        message += ')';
        throw std::runtime_error(message);
    }
    return *diagnostic;
}

void test_complete_sdf40_headers()
{
    using namespace fsim::frontend;
    const auto result = parse_sdf(SourceText { "headers.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (design "top\"quoted")
  (DATE "2026-08-13")
  (VENDOR "fsim")
  (PROGRAM "fsim-test")
  (VERSION "1.0")
  (DIVIDER /)
  (VOLTAGE .90:1.00:1.10)
  (PROCESS "typical")
  (TEMPERATURE -40::125)
  (TIMESCALE 1 NS)
  // Change 4 owns the parsed CELL contents.
  (CELL (CELLTYPE "placeholder") (INSTANCE top/u0))
)
)" });
    require(result.ok() && !result.resource_exhausted,
        "complete SDF 4.0 header surface must parse");
    require(result.file.has_revision && result.file.revision == SdfRevision::Sdf40 && result.file.headers.size() == 11U && result.file.body_forms.size() == 1U,
        "SDF file must retain its revision, eleven headers, and raw body");
    require(std::string_view { to_string(result.file.revision) } == "4.0" && std::string_view { to_string(SdfHeaderKind::ProgramVersion) } == "VERSION",
        "SDF revision and header APIs must be canonical");

    const auto& design = require_header(result, SdfHeaderKind::Design);
    require(design.keyword_spelling == "design" && design.value_spellings.size() == 1U && design.value_spellings.front() == "\"top\\\"quoted\"" && design.canonical_value == "top\"quoted" && design.span.begin.line == 3U && design.span.begin.column == 3U,
        "SDF string header must retain original and canonical identities");
    require(require_header(result, SdfHeaderKind::Divider).canonical_value == "/" && require_header(result, SdfHeaderKind::Voltage).canonical_value == ".90:1.00:1.10" && require_header(result, SdfHeaderKind::Temperature).canonical_value == "-40::125" && require_header(result, SdfHeaderKind::Timescale).canonical_value == "1ns",
        "SDF divider, exact triples, and timescale must canonicalize");

    const auto& body = result.file.body_forms.front();
    require(body.keyword_spelling == "CELL" && body.token_count > 6U && body.span.begin.line == 14U && result.file.tokens[body.first_token].kind == SdfTokenKind::LeftParenthesis,
        "Change 3 must retain ordered raw CELL tokens and exact body span");
    require(std::ranges::count_if(result.file.tokens, [](const SdfToken& token) {
        return token.kind == SdfTokenKind::Comment;
    }) == 1 && result.file.span.source_name == "headers.sdf"
            && result.file.span.physical_source_name == "headers.sdf" && result.file.span.begin.line == 1U,
        "SDF file must retain comments and canonical physical provenance");
}

void test_header_revision_and_order_diagnostics()
{
    using namespace fsim::frontend;
    auto result = parse_sdf(SourceText { "future.sdf", R"((DELAYFILE
  (SDFVERSION "4.1")
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-005");

    result = parse_sdf(SourceText { "older.sdf", R"((DELAYFILE
  (SDFVERSION "3.0")
))" });
    require(result.ok() && result.file.revision == SdfRevision::Sdf30
            && result.file.revision_adapter == SdfRevisionAdapter::Sdf30,
        "known SDF 3.0 revision must select its independent adapter");

    result = parse_sdf(SourceText { "duplicate.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (DESIGN "first")
  (DESIGN "second")
))" });
    const auto& duplicate = require_diagnostic(result, "FSIM-SDF-PARSE-004");
    require(duplicate.span.begin.line == 4U && duplicate.span.begin.column == 4U && result.file.headers.size() == 3U,
        "duplicate SDF header must retain its coordinate and ordered record");

    result = parse_sdf(SourceText { "order.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (VENDOR "vendor")
  (DESIGN "late")
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-006");

    result = parse_sdf(SourceText { "missing.sdf", R"((DELAYFILE
  (DESIGN "top")
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-007");
    require_diagnostic(result, "FSIM-SDF-PARSE-006");
}

void test_header_value_diagnostics()
{
    using namespace fsim::frontend;
    auto result = parse_sdf(SourceText { "divider.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (DIVIDER "bad")
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-003");

    result = parse_sdf(SourceText { "voltage.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (VOLTAGE 1:2:3:4)
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-003");

    result = parse_sdf(SourceText { "timescale.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 2 ns)
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-003");

    result = parse_sdf(SourceText { "string.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (DESIGN top)
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-003");
}

void test_file_structure_diagnostics()
{
    using namespace fsim::frontend;
    auto result = parse_sdf(SourceText { "late-header.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (CELL (CELLTYPE "placeholder") (INSTANCE top))
  (DESIGN "late")
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-008");

    result = parse_sdf(SourceText { "unknown.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (UNKNOWN "value")
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-009");

    result = parse_sdf(SourceText { "trailing.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
)) trailing)" });
    require_diagnostic(result, "FSIM-SDF-PARSE-010");

    result = parse_sdf(
        SourceText { "root.sdf", R"((CELL (SDFVERSION "4.0")))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-002");
}

} // namespace

int main()
{
    try {
        test_complete_sdf40_headers();
        test_header_revision_and_order_diagnostics();
        test_header_value_diagnostics();
        test_file_structure_diagnostics();
        std::cout << "SDF parser tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SDF parser test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
