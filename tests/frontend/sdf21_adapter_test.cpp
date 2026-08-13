// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsim::frontend::Diagnostic;
using fsim::frontend::SdfConstructKind;
using fsim::frontend::SdfParseResult;
using fsim::frontend::SdfSyntaxNode;

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

const Diagnostic& require_diagnostic(const SdfParseResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find_if(result.diagnostics,
        [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
    if (found == result.diagnostics.end()) {
        std::string message = "missing SDF 2.1 diagnostic ";
        message += code;
        message += "; observed";
        for (const auto& diagnostic : result.diagnostics) {
            message += ' ';
            message += diagnostic.code;
        }
        throw std::runtime_error(message);
    }
    return *found;
}

std::size_t count_kind(const std::vector<SdfSyntaxNode>& roots,
    const SdfConstructKind kind)
{
    std::vector<const SdfSyntaxNode*> pending;
    for (const auto& root : roots)
        pending.push_back(&root);
    std::size_t count = 0U;
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->kind == kind)
            ++count;
        for (const auto& child : node->children)
            pending.push_back(&child);
    }
    return count;
}

void test_complete_sdf21_adapter()
{
    using namespace fsim::frontend;
    const auto result = parse_sdf(SourceText { "ovi21.sdf", R"((DELAYFILE
  (SDFVERSION "OVI 2.1")
  (DESIGN "legacy")
  (DIVIDER /)
  (TIMESCALE 1.0 ns)
  (CELL
    (CELLTYPE "DFF")
    (INSTANCE top)
    (INSTANCE u0)
    (CORRELATION "group-a" 10 20 30)
    (DELAY
      (PATHPULSE A Z (1:2:3) (4:5:6))
      (GLOBALPATHPULSE A Z (10:20:30) (20:30:40))
      (ABSOLUTE
        (IOPATH (01 A) Z (1:2:3) (4:5:6))
        (COND EN (IOPATH A Z (1:2:3)))
        (PORT Z (1:2:3))
        (INTERCONNECT top/src Z (1:2:3))
        (NETDELAY n1 (1:2:3))
        (NETDELAY (INSTANCE top) n2 (1:2:3))
        (DEVICE Z (1:2:3)))
      (INCREMENT (IOPATH B Z (-1:0:1))))
    (TIMINGCHECK
      (SETUP D CLK (1:2:3))
      (HOLD D CLK (1:2:3))
      (SETUPHOLD D CLK (1:2:3) (2:3:4))
      (RECOVERY RN CLK (1:2:3))
      (SKEW A B (1:2:3))
      (WIDTH CLK (1:2:3))
      (PERIOD CLK (1:2:3))
      (NOCHANGE D CLK (1:2:3) (2:3:4))
      (PATHCONSTRAINT A Z (1:2:3) (2:3:4))
      (SUM (A Z) (B Y) (1:2:3) (2:3:4))
      (DIFF (A Z) (B Y) (1:2:3))
      (SKEWCONSTRAINT (10 CLK) (1:2:3))))
  (CELL (CELLTYPE "INV") (INSTANCE *))
  (CELL (CELLTYPE "TOP") (INSTANCE))
))" });

    require(result.ok(), "complete OVI SDF 2.1 profile must adapt");
    require(result.file.revision == SdfRevision::Sdf21
            && result.file.revision_adapter == SdfRevisionAdapter::Sdf21
            && std::string_view { to_string(result.file.revision_adapter) }
                == "sdf21",
        "SDF 2.1 revision and explicit adapter identity must be retained");
    require(result.file.find_header(SdfHeaderKind::SdfVersion)->canonical_value
                == "OVI 2.1"
            && result.file.find_header(SdfHeaderKind::Timescale)->canonical_value
                == "1.0ns",
        "legacy header spelling and legal decimal timescale must be retained");
    require(result.file.cells.size() == 3U
            && result.file.cells[0].instance_kind
                == SdfInstanceSelectorKind::Exact
            && result.file.cells[0].instance_spelling == "top/u0"
            && result.file.cells[0].instance_spellings
                == std::vector<std::string>({ "top", "u0" }),
        "repeated SDF 2.1 INSTANCE forms must map to one common exact path");
    require(result.file.cells[1].instance_kind
                == SdfInstanceSelectorKind::Wildcard
            && result.file.cells[1].wildcard_requires_physical_primitive
            && result.file.cells[2].instance_kind
                == SdfInstanceSelectorKind::Empty,
        "legacy wildcard restriction and empty instance must stay explicit");

    const auto& declarations = result.file.cells.front().declarations;
    require(count_kind(declarations, SdfConstructKind::Correlation) == 1U
            && count_kind(declarations, SdfConstructKind::PathPulsePercent) == 1U
            && count_kind(declarations, SdfConstructKind::NetDelay) == 2U
            && count_kind(declarations, SdfConstructKind::Edge) == 2U
            && count_kind(declarations, SdfConstructKind::PathConstraint) == 1U
            && count_kind(declarations, SdfConstructKind::SkewConstraint) == 1U,
        "legacy constructs must map to the common typed syntax vocabulary");
}

SdfParseResult parse_bad(const std::string_view body,
    const std::string_view timescale = { })
{
    using namespace fsim::frontend;
    std::string source = "(DELAYFILE\n  (SDFVERSION \"2.1\")\n";
    if (!timescale.empty()) {
        source += "  (TIMESCALE ";
        source += timescale;
        source += ")\n";
    }
    source += body;
    source += "\n)";
    return parse_sdf(SourceText { "bad21.sdf", std::move(source) });
}

void test_sdf21_profile_rejections()
{
    using namespace fsim::frontend;
    auto result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY
      (PATHPULSEPERCENT A Z (1) (2))
      (ABSOLUTE
        (COND "later-name" EN (IOPATH A Z (1)))
        (CONDELSE (IOPATH A Z (1)))
        (IOPATH A Z (RETAIN (1)) (1))))
    (TIMINGCHECK (REMOVAL RN CLK (1)))
    (TIMINGENV (ARRIVAL A (1)))
    (LABEL (legacy (1)))))");
    const auto& later = require_diagnostic(result, "FSIM-SDF-21-001");
    require(later.span.begin.line >= 4U,
        "later-only SDF 2.1 rejection must retain a narrow construct coordinate");

    result = parse_bad(R"(  (CELL (CELLTYPE "X")
    (INSTANCE *) (INSTANCE extra)))");
    require_diagnostic(result, "FSIM-SDF-21-002");

    result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY (ABSOLUTE
      (IOPATH A Z (1) (2:3:4))))))");
    require_diagnostic(result, "FSIM-SDF-21-003");

    result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY (ABSOLUTE
      (IOPATH A Z (1:2:3) (2:3:4) (3:4:5) (4:5:6))))))");
    require_diagnostic(result, "FSIM-SDF-21-004");

    result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)
    (TIMINGCHECK
      (DIFF (A Z) (1:2:3)))))");
    require_diagnostic(result, "FSIM-SDF-21-005");

    result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)))", "1 ms");
    require_diagnostic(result, "FSIM-SDF-21-006");

    result = parse_sdf(SourceText { "still-disabled.sdf", R"((DELAYFILE
  (SDFVERSION "OVI 3.0")
  (CELL (CELLTYPE "X") (INSTANCE top))
))" });
    require(result.file.revision == SdfRevision::Sdf30
            && result.file.revision_adapter == SdfRevisionAdapter::Sdf30
            && result.file.cells.size() == 1U,
        "SDF 3.0 must remain outside the SDF 2.1 adapter");
}

} // namespace

int main()
{
    try {
        test_complete_sdf21_adapter();
        test_sdf21_profile_rejections();
        std::cout << "SDF 2.1 adapter tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SDF 2.1 adapter test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
