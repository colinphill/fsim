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
        std::string message = "missing SDF 3.0 diagnostic ";
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

void test_complete_sdf30_adapter()
{
    using namespace fsim::frontend;
    const auto result = parse_sdf(SourceText { "ovi30.sdf", R"((DELAYFILE
  (SDFVERSION "OVI 3.0")
  (DESIGN "three")
  (DIVIDER /)
  (TIMESCALE 10.0 ps)
  (CELL
    (CELLTYPE "DFF")
    (INSTANCE top/u0)
    (DELAY
      (PATHPULSE A Z (1:2:3) (2:3:4))
      (PATHPULSEPERCENT A Z (10:20:30) (20:30:40))
      (ABSOLUTE
        (IOPATH (posedge A) Z (RETAIN (1:2:3)) (2:3:4))
        (COND "named" (EN == 1) (IOPATH A Z (1:2:3)))
        (CONDELSE (IOPATH A Z (2:3:4)))
        (PORT Z (1:2:3))
        (INTERCONNECT src/Z D (1:2:3))
        (DEVICE Z (1:2:3)))
      (INCREMENT (IOPATH B Z (-1:0:1))))
    (TIMINGCHECK
      (SETUP D CLK (1:2:3))
      (HOLD D CLK (1:2:3))
      (SETUPHOLD D CLK (1:2:3) (2:3:4) (SCOND EN) (CCOND EN))
      (RECOVERY RN CLK (1:2:3))
      (REMOVAL RN CLK (1:2:3))
      (RECREM RN CLK (1:2:3) (2:3:4))
      (SKEW A B (1:2:3))
      (BIDIRECTSKEW A B (1:2:3) (2:3:4))
      (WIDTH CLK (1:2:3))
      (PERIOD CLK (1:2:3))
      (NOCHANGE D CLK (1:2:3) (2:3:4)))
    (TIMINGENV
      (PATHCONSTRAINT A Z (NAME "main") (1:2:3))
      (PERIODCONSTRAINT CLK (10:20:30))
      (SKEWCONSTRAINT CLK (1:2:3))
      (SUM (A Z) (B Y) (1:2:3))
      (DIFF (A Z) (B Y) (1:2:3))
      (ARRIVAL A (1:2:3))
      (DEPARTURE Z (2:3:4))
      (SLACK D (3:4:5))
      (WAVEFORM CLK (1:2:3) (2:3:4))
      (EXCEPTION top/u1)))
  (CELL (CELLTYPE "INV") (INSTANCE *))
  (CELL (CELLTYPE "TOP") (INSTANCE))
))" });

    require(result.ok(), "complete OVI SDF 3.0 profile must adapt");
    require(result.file.revision == SdfRevision::Sdf30
            && result.file.revision_adapter == SdfRevisionAdapter::Sdf30
            && std::string_view { to_string(result.file.revision_adapter) }
                == "sdf30",
        "SDF 3.0 revision and independent adapter identity must be retained");
    require(result.file.find_header(SdfHeaderKind::SdfVersion)->canonical_value
                == "OVI 3.0"
            && result.file.find_header(SdfHeaderKind::Timescale)->canonical_value
                == "10.0ps",
        "SDF 3.0 original version and decimal timescale must be retained");
    require(result.file.cells.size() == 3U
            && !result.file.cells[1].wildcard_requires_physical_primitive
            && result.file.cells[2].instance_kind
                == SdfInstanceSelectorKind::Empty,
        "SDF 3.0 wildcard and empty INSTANCE semantics must remain distinct");
    const auto& declarations = result.file.cells.front().declarations;
    require(count_kind(declarations, SdfConstructKind::ConditionalElse) == 1U
            && count_kind(declarations, SdfConstructKind::Retain) == 1U
            && count_kind(declarations, SdfConstructKind::Removal) == 1U
            && count_kind(declarations, SdfConstructKind::RecRem) == 1U
            && count_kind(declarations, SdfConstructKind::TimingEnvironment)
                == 1U
            && count_kind(declarations, SdfConstructKind::PathPulsePercent)
                == 1U,
        "SDF 3.0-only additions must map to the common typed vocabulary");
}

SdfParseResult parse_bad(const std::string_view body,
    const std::string_view timescale = { })
{
    using namespace fsim::frontend;
    std::string source = "(DELAYFILE\n  (SDFVERSION \"3.0\")\n";
    if (!timescale.empty()) {
        source += "  (TIMESCALE ";
        source += timescale;
        source += ")\n";
    }
    source += body;
    source += "\n)";
    return parse_sdf(SourceText { "bad30.sdf", std::move(source) });
}

void test_sdf30_profile_rejections()
{
    using namespace fsim::frontend;
    auto result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)
    (CORRELATION "old")
    (DELAY
      (GLOBALPATHPULSE A Z (1) (2))
      (ABSOLUTE
        (NETDELAY n1 (1))
        (MIPD A (1))))
    (LABEL (later (1)))))");
    const auto& profile = require_diagnostic(result, "FSIM-SDF-30-001");
    require(profile.span.begin.line >= 4U,
        "SDF 3.0 profile rejection must retain a narrow construct coordinate");

    result = parse_bad(R"(  (CELL (CELLTYPE "X")
    (INSTANCE top) (INSTANCE u0)))");
    require_diagnostic(result, "FSIM-SDF-30-002");

    result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)))", "1 ms");
    require_diagnostic(result, "FSIM-SDF-30-003");

    result = parse_bad(R"(  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY (ABSOLUTE
      (IOPATH A Z (1) (2:3:4))))))");
    require_diagnostic(result, "FSIM-SDF-30-004");

    result = parse_sdf(SourceText { "separate21.sdf", R"((DELAYFILE
  (SDFVERSION "2.1")
  (CELL (CELLTYPE "X") (INSTANCE top))
))" });
    require(result.file.revision_adapter == SdfRevisionAdapter::Sdf21,
        "SDF 3.0 adapter must not contaminate the SDF 2.1 profile");

    result = parse_sdf(SourceText { "separate40.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (CELL (CELLTYPE "X") (INSTANCE top))
))" });
    require(result.ok()
            && result.file.revision_adapter == SdfRevisionAdapter::None,
        "SDF 3.0 adapter must not contaminate the SDF 4.0 profile");
}

} // namespace

int main()
{
    try {
        test_complete_sdf30_adapter();
        test_sdf30_profile_rejections();
        std::cout << "SDF 3.0 adapter tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SDF 3.0 adapter test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
