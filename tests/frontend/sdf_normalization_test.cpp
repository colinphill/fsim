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
        std::string message = "missing SDF normalization diagnostic ";
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

std::vector<const SdfSyntaxNode*> nodes_of_kind(
    const std::vector<SdfSyntaxNode>& roots, const SdfConstructKind kind)
{
    std::vector<const SdfSyntaxNode*> result;
    std::vector<const SdfSyntaxNode*> pending;
    for (const auto& root : roots)
        pending.push_back(&root);
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->kind == kind)
            result.push_back(node);
        for (auto child = node->children.rbegin(); child != node->children.rend();
            ++child) {
            pending.push_back(&*child);
        }
    }
    return result;
}

std::vector<std::string> canonical_nodes(const SdfParseResult& result)
{
    std::vector<std::string> identities;
    std::vector<const SdfSyntaxNode*> pending;
    for (const auto& declaration : result.file.cells.front().declarations)
        pending.push_back(&declaration);
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        identities.push_back(node->canonical_identity);
        for (auto child = node->children.rbegin(); child != node->children.rend();
            ++child) {
            pending.push_back(&*child);
        }
    }
    return identities;
}

void test_exact_values_timescale_names_edges_conditions()
{
    using namespace fsim::frontend;
    const auto result = parse_sdf(SourceText { "normalized.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (DIVIDER /)
  (VOLTAGE .90:1.00:1.10)
  (TEMPERATURE -40::125)
  (TIMESCALE 10 NS)
  (CELL
    (CELLTYPE "DFF")
    (INSTANCE top/a\/b/u0)
    (DELAY
      (PATHPULSEPERCENT A Z (.50:1.0:2.500))
      (ABSOLUTE
        (IOPATH (PoSeDgE A) Z (001.2300e+2::-.0))
        (COND (top/en == 1.00) (IOPATH A Z (:2.0:))))))
))" });

    if (!result.ok()) {
        std::string message = "exact SDF normalization fixture must parse; observed";
        for (const auto& diagnostic : result.diagnostics) {
            message += ' ';
            message += diagnostic.code;
        }
        throw std::runtime_error(message);
    }
    require(result.file.normalized_timescale
            && result.file.normalized_timescale->magnitude.canonical == "1e1"
            && result.file.normalized_timescale->femtoseconds.canonical == "1e7"
            && result.file.normalized_timescale->canonical == "1e7fs",
        "timescale must normalize exactly to a power-of-ten femtosecond value");
    require(result.file.cells.front().normalized_instance
            && result.file.cells.front().normalized_instance->segments
                == std::vector<std::string>({ "top", "a/b", "u0" }),
        "escaped hierarchy divider must remain part of its decoded segment");

    const auto values = nodes_of_kind(result.file.cells.front().declarations,
        SdfConstructKind::Value);
    const auto exact = std::ranges::find_if(values, [](const auto* node) {
        return node->exact_value
            && node->exact_value->canonical == "123e0::0e0";
    });
    require(exact != values.end()
            && (*exact)->exact_value->kind == SdfExactValueKind::Triple
            && (*exact)->exact_value->components[0]
            && !(*exact)->exact_value->components[1]
            && (*exact)->exact_value->components[2]
            && !(*exact)->exact_value->components[2]->negative
            && (*exact)->scaled_femtoseconds
            && (*exact)->scaled_femtoseconds->canonical == "123e7::0e0",
        "min/typ/max normalization must preserve missing slots and signed zero without selecting one slot");

    const auto percent = nodes_of_kind(result.file.cells.front().declarations,
        SdfConstructKind::PathPulsePercent);
    require(percent.size() == 1U && percent.front()->children.size() == 1U
            && percent.front()->children.front().exact_value
            && percent.front()->children.front().exact_value->canonical
                == "5e-1:1e0:25e-1"
            && !percent.front()->children.front().scaled_femtoseconds,
        "percentage triples must normalize exactly without time scaling");

    const auto edges = nodes_of_kind(result.file.cells.front().declarations,
        SdfConstructKind::Edge);
    require(edges.size() == 1U
            && edges.front()->canonical_identity == "edge{posedge}",
        "edge spelling must have a stable case-independent identity");
    const auto conditions = nodes_of_kind(result.file.cells.front().declarations,
        SdfConstructKind::ConditionExpression);
    require(conditions.size() == 1U
            && std::ranges::find(conditions.front()->canonical_atoms,
                   "number:1e0")
                != conditions.front()->canonical_atoms.end(),
        "condition decimals must share exact canonical atom identity");

    const auto* voltage = result.file.find_header(SdfHeaderKind::Voltage);
    const auto* temperature = result.file.find_header(SdfHeaderKind::Temperature);
    require(voltage && voltage->exact_value
            && voltage->exact_value->canonical == "9e-1:1e0:11e-1"
            && temperature && temperature->exact_value
            && temperature->exact_value->canonical == "-4e1::125e0",
        "numeric headers must use the same exact scalar and triple normalization");
}

SdfParseResult parse_overlap(const std::string_view version,
    const std::string_view timescale, const bool repeated_instance)
{
    using namespace fsim::frontend;
    std::string source = "(DELAYFILE\n  (SDFVERSION \"";
    source += version;
    source += "\")\n  (DIVIDER /)\n  (TIMESCALE ";
    source += timescale;
    source += ")\n  (CELL (CELLTYPE \"BUF\")\n";
    source += repeated_instance
        ? "    (INSTANCE top) (INSTANCE u0)\n"
        : "    (INSTANCE top/u0)\n";
    source += "    (DELAY (ABSOLUTE (IOPATH (01 A) Z (1.00)))))\n)";
    return parse_sdf(SourceText { "overlap.sdf", std::move(source) });
}

void test_revision_overlap_and_divider_equivalence()
{
    using namespace fsim::frontend;
    const auto sdf21 = parse_overlap("OVI 2.1", "10.0 ps", true);
    const auto sdf30 = parse_overlap("OVI 3.0", "10.0 ps", false);
    const auto sdf40 = parse_overlap("4.0", "10 ps", false);
    if (!sdf21.ok() || !sdf30.ok() || !sdf40.ok()) {
        std::string message = "overlapping revisions must normalize; observed";
        for (const auto* result : { &sdf21, &sdf30, &sdf40 }) {
            message += " [";
            for (const auto& diagnostic : result->diagnostics) {
                message += diagnostic.code;
                message += ' ';
            }
            message += ']';
        }
        throw std::runtime_error(message);
    }
    require(sdf21.file.normalized_timescale == sdf30.file.normalized_timescale
            && sdf30.file.normalized_timescale == sdf40.file.normalized_timescale
            && sdf21.file.cells.front().normalized_instance
                == sdf30.file.cells.front().normalized_instance
            && sdf30.file.cells.front().normalized_instance
                == sdf40.file.cells.front().normalized_instance
            && canonical_nodes(sdf21) == canonical_nodes(sdf30)
            && canonical_nodes(sdf30) == canonical_nodes(sdf40),
        "revision adapters must converge on identical exact values, names, edges, and syntax identities where semantics overlap");

    const auto slash = parse_sdf(SourceText { "slash.sdf", R"((DELAYFILE
  (SDFVERSION "4.0") (DIVIDER /)
  (CELL (CELLTYPE "X") (INSTANCE top/a\.b/u0))
))" });
    const auto dot = parse_sdf(SourceText { "dot.sdf", R"((DELAYFILE
  (SDFVERSION "4.0") (DIVIDER .)
  (CELL (CELLTYPE "X") (INSTANCE top.a\.b.u0))
))" });
    if (!slash.ok() || !dot.ok()
        || slash.file.cells.front().normalized_instance
            != dot.file.cells.front().normalized_instance) {
        std::string message = "divider normalization mismatch";
        for (const auto* result : { &slash, &dot }) {
            message += " [";
            for (const auto& diagnostic : result->diagnostics) {
                message += diagnostic.code;
                message += ' ';
            }
            if (result->file.cells.front().normalized_instance)
                message += result->file.cells.front().normalized_instance->canonical;
            message += ']';
        }
        throw std::runtime_error(message);
    }

    const auto upper = parse_sdf(SourceText { "upper.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY (ABSOLUTE
      (COND (EN == 1.0) (IOPATH A Z (2.00))))))
))" });
    const auto lower = parse_sdf(SourceText { "lower.sdf", R"((delayfile
  (sdfversion "4.0")
  (cell (celltype "X") (instance top)
    (delay (absolute
      (cond (EN == 1) (iopath A Z (2))))))
))" });
    require(upper.ok() && lower.ok()
            && canonical_nodes(upper) == canonical_nodes(lower),
        "keyword case and redundant decimal spelling must not change canonical condition or construct identity");
}

void test_overflow_invalid_scaling_and_loss_detection()
{
    using namespace fsim::frontend;
    auto result = parse_sdf(SourceText { "overflow.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY (ABSOLUTE (IOPATH A Z (1e999999999999999999999999)))))
))" });
    require_diagnostic(result, "FSIM-SDF-NORM-001");

    result = parse_sdf(SourceText { "scale.sdf", R"((DELAYFILE
  (SDFVERSION "4.0") (TIMESCALE 2 ns)
  (CELL (CELLTYPE "X") (INSTANCE top))
))" });
    require_diagnostic(result, "FSIM-SDF-PARSE-003");
    require_diagnostic(result, "FSIM-SDF-NORM-003");
    require(!result.file.normalized_timescale,
        "invalid scaling must not publish a normalized timescale");

    result = parse_sdf(SourceText { "name.sdf", R"((DELAYFILE
  (SDFVERSION "4.0") (DIVIDER .)
  (CELL (CELLTYPE "X") (INSTANCE top..u0))
))" });
    require_diagnostic(result, "FSIM-SDF-NORM-004");

    result = parse_sdf(SourceText { "fraction.sdf", R"((DELAYFILE
  (SDFVERSION "4.0") (TIMESCALE 1 fs)
  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY (ABSOLUTE (IOPATH A Z (.1)))))
))" });
    require(result.ok(),
        "sub-femtosecond exact input must remain representable until a consumer requests conversion");
    const auto values = nodes_of_kind(result.file.cells.front().declarations,
        SdfConstructKind::Value);
    require(values.size() == 1U && values.front()->scaled_femtoseconds,
        "fractional femtoseconds must remain exact in normalized syntax");
    const auto& fraction = *values.front()->scaled_femtoseconds->components[0];
    require(sdf_exact_integer_at(fraction, 0).error
            == SdfExactConversionError::Lossy,
        "integer conversion must explicitly reject loss instead of rounding");
    auto enormous = fraction;
    enormous.exponent10 = 100;
    require(sdf_exact_integer_at(enormous, 0, 8U).error
            == SdfExactConversionError::Overflow,
        "bounded integer conversion must report expansion overflow");

    std::string large_digits(4000U, '7');
    std::string source = "(DELAYFILE (SDFVERSION \"4.0\") "
                         "(CELL (CELLTYPE \"X\") (INSTANCE top) "
                         "(DELAY (ABSOLUTE (IOPATH A Z (";
    source += large_digits;
    source += "))))))";
    result = parse_sdf(SourceText { "large.sdf", std::move(source) });
    const auto large_values = nodes_of_kind(
        result.file.cells.front().declarations, SdfConstructKind::Value);
    require(result.ok() && large_values.size() == 1U
            && large_values.front()->exact_value
            && large_values.front()->exact_value->components[0]->coefficient.size()
                == large_digits.size(),
        "large governed decimal coefficients must remain exact without host numeric conversion");
}

} // namespace

int main()
{
    try {
        test_exact_values_timescale_names_edges_conditions();
        test_revision_overlap_and_divider_equivalence();
        test_overflow_invalid_scaling_and_loss_detection();
        std::cout << "SDF normalization tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SDF normalization test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
