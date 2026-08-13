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
        std::string message = "missing SDF construct diagnostic ";
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

void test_complete_sdf40_construct_surface()
{
    using namespace fsim::frontend;
    const auto result = parse_sdf(SourceText { "constructs.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (DIVIDER /)
  (CELL
    (CELLTYPE "DFFRX1")
    (INSTANCE top/u0)
    (DELAY
      (PATHPULSE D Q (1) (2))
      (PATHPULSEPERCENT (10) (20))
      (ABSOLUTE
        (IOPATH (posedge D) Q (RETAIN (0.01)) (1:2:3) ())
        (COND "enabled" (EN == 1) (IOPATH D QN (2)))
        (CONDELSE (IOPATH D QN (3)))
        (INTERCONNECT src/Z D (1))
        (NETDELAY n1 (1))
        (PORT Q (1) (2))
        (DEVICE Q (1))
        (MIPD D (1)))
      (INCREMENT (IOPATH CLK Q (0.1))))
    (TIMINGCHECK
      (SETUP D (01 CLK) (1))
      (HOLD (COND EN D) CLK (1))
      (SETUPHOLD D CLK (1) (2) (SCOND EN) (CCOND EN))
      (RECOVERY RN CLK (1))
      (REMOVAL RN CLK (1))
      (RECREM RN CLK (1) (2))
      (SKEW A B (1))
      (BIDIRECTSKEW A B (1) (2))
      (WIDTH (negedge CLK) (1) (0))
      (PERIOD CLK (1))
      (NOCHANGE D CLK (1) (2))
      (NEGATIVE (HOLD D CLK (1))))
    (TIMINGENV
      (PATHCONSTRAINT A Z (NAME "main") (1))
      (PERIODCONSTRAINT CLK (10))
      (SKEWCONSTRAINT A (2))
      (SUM (A Z) (B Y) (1))
      (DIFF (A Z) (B Y) (1))
      (ARRIVAL A (1))
      (DEPARTURE Z (2))
      (SLACK D (3))
      (WAVEFORM CLK (1) (2))
      (EXCEPTION top/u1))
    (LABEL
      (NAME "mode" (1))
      (user_delay (2))))
  (CELL (CELLTYPE "INV") (INSTANCE *)
    (DELAY (ABSOLUTE (IOPATH A Z (1)))))
  (CELL (CELLTYPE "TOP") (INSTANCE)
    (DELAY (ABSOLUTE (DEVICE (1)))))
))" });

    require(result.ok(), "complete SDF 4.0 construct surface must parse");
    require(result.file.cells.size() == 3U,
        "all CELL forms must produce typed records");
    require(result.file.cells[0].cell_type == "DFFRX1"
            && result.file.cells[0].cell_type_spelling == "\"DFFRX1\""
            && result.file.cells[0].instance_kind
                == SdfInstanceSelectorKind::Exact
            && result.file.cells[0].instance_spelling == "top/u0",
        "exact CELLTYPE and INSTANCE spelling must be retained");
    require(result.file.cells[1].instance_kind
                == SdfInstanceSelectorKind::Wildcard
            && result.file.cells[2].instance_kind
                == SdfInstanceSelectorKind::Empty,
        "wildcard and legal empty INSTANCE selectors must remain distinct");
    require(std::string_view { to_string(result.file.cells[0].instance_kind) }
                == "exact"
            && std::string_view { to_string(SdfConstructKind::TimingEnvironment) }
                == "TIMINGENV",
        "construct and selector introspection must be canonical");

    const auto& declarations = result.file.cells.front().declarations;
    require(declarations.size() == 4U
            && count_kind(declarations, SdfConstructKind::Absolute) == 1U
            && count_kind(declarations, SdfConstructKind::Increment) == 1U
            && count_kind(declarations, SdfConstructKind::Iopath) == 4U
            && count_kind(declarations, SdfConstructKind::NetDelay) == 1U
            && count_kind(declarations, SdfConstructKind::Retain) == 1U
            && count_kind(declarations, SdfConstructKind::Conditional) == 2U
            && count_kind(declarations, SdfConstructKind::ConstraintPath) == 4U
            && count_kind(declarations, SdfConstructKind::LabelEntry) == 2U,
        "ordered syntax tree must retain every nested construct family");
    require(declarations.front().span.begin.line == 7U
            && declarations.front().first_token > 0U
            && declarations.front().token_count > 10U,
        "syntax nodes must retain exact physical spans and token intervals");
}

SdfParseResult parse_bad(const std::string_view cell)
{
    using namespace fsim::frontend;
    std::string source = "(DELAYFILE\n  (SDFVERSION \"4.0\")\n  ";
    source += cell;
    source += "\n)";
    return parse_sdf(SourceText { "bad-construct.sdf", std::move(source) });
}

void test_construct_owned_diagnostics()
{
    auto result = parse_bad("(CELL (CELLTYPE \"X\"))");
    require_diagnostic(result, "FSIM-SDF-PARSE-011");

    result = parse_bad("(CELL (CELLTYPE X) (INSTANCE top))");
    require_diagnostic(result, "FSIM-SDF-PARSE-012");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE * extra) "
        "(DELAY (ABSOLUTE (DEVICE (1)))))");
    const auto& selector = require_diagnostic(result, "FSIM-SDF-PARSE-013");
    require(selector.span.begin.line == 3U,
        "INSTANCE diagnostic must retain its exact source line");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE top) (SETUP A B (1)))");
    require_diagnostic(result, "FSIM-SDF-PARSE-014");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (PORT Q))))");
    require_diagnostic(result, "FSIM-SDF-PARSE-015");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (PORT Q (1:2)))))");
    require_diagnostic(result, "FSIM-SDF-PARSE-016");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (IOPATH (posedge A B) Q (1)))))");
    require_diagnostic(result, "FSIM-SDF-PARSE-017");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (COND (IOPATH A Q (1))))))");
    require_diagnostic(result, "FSIM-SDF-PARSE-018");

    result = parse_bad(
        "(CELL (INSTANCE top) (CELLTYPE \"X\") "
        "(DELAY (ABSOLUTE (IOPATH A Q (1)))))");
    require_diagnostic(result, "FSIM-SDF-PARSE-019");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (BOGUS A Q (1)))))");
    require_diagnostic(result, "FSIM-SDF-PARSE-020");

    result = parse_bad(
        "(CELL (CELLTYPE \"X\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (PATHPULSE A Q (1)))))");
    require_diagnostic(result, "FSIM-SDF-PARSE-014");
}

} // namespace

int main()
{
    try {
        test_complete_sdf40_construct_surface();
        test_construct_owned_diagnostics();
        std::cout << "SDF construct parser tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SDF construct parser test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
