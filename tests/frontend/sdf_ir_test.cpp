// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using fsim::frontend::Diagnostic;
using fsim::frontend::SdfConstructKind;
using fsim::frontend::SdfIr;
using fsim::frontend::SdfIrNode;
using fsim::frontend::SdfParseResult;
using fsim::frontend::SdfSyntaxNode;

static_assert(std::is_same_v<decltype(std::declval<const SdfIr&>().nodes()),
    std::span<const SdfIrNode>>);
static_assert(!std::is_assignable_v<
    decltype((std::declval<const SdfIr&>().nodes().front().kind)),
    SdfConstructKind>);

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

const Diagnostic& require_diagnostic(const std::vector<Diagnostic>& diagnostics,
    const std::string_view code)
{
    const auto found = std::ranges::find_if(diagnostics,
        [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
    if (found == diagnostics.end()) {
        std::string message = "missing SDF IR diagnostic ";
        message += code;
        message += "; observed";
        for (const auto& diagnostic : diagnostics) {
            message += ' ';
            message += diagnostic.code;
        }
        throw std::runtime_error(message);
    }
    return *found;
}

SdfParseResult parse_complete_ir(const std::string_view source_name = "ir.sdf")
{
    using namespace fsim::frontend;
    return parse_sdf(SourceText { std::string { source_name }, R"((DELAYFILE
  (SDFVERSION "4.0")
  (DIVIDER /)
  (TIMESCALE 10 ps)
  (CELL
    (CELLTYPE "DFF")
    (INSTANCE top/u0)
    (DELAY
      (ABSOLUTE
        (IOPATH (posedge D) Q (1:2:3))
        (COND (EN == 1) (IOPATH D QN (2)))))
    (TIMINGCHECK
      (SETUP D CLK (3)))
    (TIMINGENV
      (ARRIVAL D (4)))
    (LABEL
      (NAME "mode" (5))))
  (CELL (CELLTYPE "TOP") (INSTANCE)
    (DELAY (ABSOLUTE (DEVICE (6)))))
))" });
}

void test_immutable_ordered_ir_and_introspection()
{
    using namespace fsim::frontend;
    const auto result = parse_complete_ir();
    require(result.ok() && result.file.normalized_ir,
        "complete normalized syntax must publish one immutable IR snapshot");
    const auto& ir = *result.file.normalized_ir;
    require(ir.revision() == SdfRevision::Sdf40
            && ir.revision_adapter() == SdfRevisionAdapter::None
            && ir.source_name() == "ir.sdf" && ir.timescale()
            && ir.timescale()->canonical == "1e4fs"
            && ir.cells().size() == 2U && !ir.nodes().empty(),
        "IR must retain revision provenance, source identity, exact timescale, cells, and nodes");

    for (std::size_t index = 0U; index < ir.nodes().size(); ++index) {
        require(ir.nodes()[index].id == index + 1U
                && ir.find_node(index + 1U) == &ir.nodes()[index],
            "pre-order node IDs must be contiguous and directly introspectable");
    }
    require(ir.find_node(0U) == nullptr
            && ir.find_node(ir.nodes().size() + 1U) == nullptr
            && ir.find_cell(1U) == &ir.cells().front()
            && ir.find_cell(3U) == nullptr,
        "IR ID lookup must reject sentinels and out-of-range identities");
    require(ir.nodes()[0].kind == SdfConstructKind::Delay
            && ir.nodes()[0].parent_id == 0U
            && ir.nodes()[0].depth == 0U
            && ir.nodes()[1].kind == SdfConstructKind::Absolute
            && ir.nodes()[1].parent_id == ir.nodes()[0].id
            && ir.nodes()[1].depth == 1U,
        "IR nodes must retain deterministic declaration pre-order, parent IDs, and depth");

    const auto value = std::ranges::find_if(ir.nodes(), [](const auto& node) {
        return node.kind == SdfConstructKind::Value && node.exact_value
            && node.exact_value->canonical == "1e0:2e0:3e0";
    });
    require(value != ir.nodes().end() && value->scaled_femtoseconds
            && value->scaled_femtoseconds->canonical == "1e4:2e4:3e4"
            && value->span.begin.line == 10U && value->token_count != 0U
            && value->source_identity.find("cell/1/node/") == 0U,
        "IR value nodes must retain exact/scaled triples, token intervals, coordinates, and stable source IDs");
    require(ir.cells()[0].first_node == 0U
            && ir.cells()[0].node_count < ir.nodes().size()
            && ir.cells()[1].first_node == ir.cells()[0].node_count
            && ir.cells()[1].first_node + ir.cells()[1].node_count
                == ir.nodes().size(),
        "cell node ranges must partition the single ordered IR node vector");

    const auto repeated = parse_complete_ir();
    require(repeated.file.normalized_ir
            && repeated.file.normalized_ir->describe() == ir.describe()
            && repeated.file.normalized_ir->semantic_identity()
                == ir.semantic_identity(),
        "IR description and canonical identity must be deterministic across repeated parsing");
    const auto relocated = parse_complete_ir("relocated/input.sdf");
    require(relocated.file.normalized_ir && *relocated.file.normalized_ir == ir
            && relocated.file.normalized_ir->source_name() != ir.source_name()
            && relocated.file.normalized_ir->describe() != ir.describe(),
        "semantic equality must ignore source location while introspection retains provenance");
}

SdfParseResult parse_overlap(const std::string_view version,
    const std::string_view timescale, const bool repeated_instance,
    const bool wildcard = false)
{
    using namespace fsim::frontend;
    std::string source = "(DELAYFILE (SDFVERSION \"";
    source += version;
    source += "\") (DIVIDER /) (TIMESCALE ";
    source += timescale;
    source += ") (CELL (CELLTYPE \"BUF\") ";
    if (wildcard) {
        source += "(INSTANCE *) ";
    } else if (repeated_instance) {
        source += "(INSTANCE top) (INSTANCE u0) ";
    } else {
        source += "(INSTANCE top/u0) ";
    }
    source += "(DELAY (ABSOLUTE (IOPATH (01 A) Z (1.00))))))";
    return parse_sdf(SourceText { "overlap-ir.sdf", std::move(source) });
}

void test_revision_equality_and_distinctions()
{
    using namespace fsim::frontend;
    const auto sdf21 = parse_overlap("OVI 2.1", "10.0 ps", true);
    const auto sdf30 = parse_overlap("OVI 3.0", "10.0 ps", false);
    const auto sdf40 = parse_overlap("4.0", "10 ps", false);
    require(sdf21.ok() && sdf30.ok() && sdf40.ok()
            && sdf21.file.normalized_ir && sdf30.file.normalized_ir
            && sdf40.file.normalized_ir
            && *sdf21.file.normalized_ir == *sdf30.file.normalized_ir
            && *sdf30.file.normalized_ir == *sdf40.file.normalized_ir
            && sdf21.file.normalized_ir->revision() == SdfRevision::Sdf21
            && sdf30.file.normalized_ir->revision() == SdfRevision::Sdf30,
        "overlapping 2.1/3.0/4.0 semantics must share canonical equality while retaining revision provenance");

    const auto wildcard21 = parse_overlap("2.1", "10.0 ps", false, true);
    const auto wildcard30 = parse_overlap("3.0", "10.0 ps", false, true);
    require(wildcard21.ok() && wildcard30.ok()
            && wildcard21.file.normalized_ir && wildcard30.file.normalized_ir
            && *wildcard21.file.normalized_ir
                != *wildcard30.file.normalized_ir
            && wildcard21.file.normalized_ir->cells().front().wildcard_requires_physical_primitive
            && !wildcard30.file.normalized_ir->cells().front().wildcard_requires_physical_primitive,
        "revision-specific wildcard semantics must remain distinct in canonical IR identity");
}

SdfSyntaxNode* find_mutable_value(std::vector<SdfSyntaxNode>& roots)
{
    std::vector<SdfSyntaxNode*> pending;
    for (auto& root : roots)
        pending.push_back(&root);
    while (!pending.empty()) {
        auto* node = pending.back();
        pending.pop_back();
        if (node->kind == SdfConstructKind::Value)
            return node;
        for (auto& child : node->children)
            pending.push_back(&child);
    }
    return nullptr;
}

void test_transactional_invariant_and_resource_rejection()
{
    using namespace fsim::frontend;
    auto result = parse_complete_ir("invariant.sdf");
    require(result.ok() && result.file.normalized_ir,
        "valid IR fixture must publish before invariant testing");
    auto* value = find_mutable_value(result.file.cells.front().declarations);
    require(value != nullptr, "IR invariant fixture must contain a value node");
    value->exact_value.reset();
    std::vector<Diagnostic> diagnostics;
    lower_sdf_ir(result.file, diagnostics);
    require_diagnostic(diagnostics, "FSIM-SDF-IR-001");
    require(!result.file.normalized_ir,
        "incomplete normalized syntax must not publish a partial IR");

    result = parse_complete_ir("resource.sdf");
    diagnostics.clear();
    lower_sdf_ir(result.file, diagnostics,
        SdfIrLimits { 2U, 1U, 64U * 1024U * 1024U });
    require_diagnostic(diagnostics, "FSIM-SDF-IR-002");
    require(!result.file.normalized_ir,
        "node-limit rejection must discard the transactional IR build");

    result = parse_complete_ir("identity-resource.sdf");
    diagnostics.clear();
    lower_sdf_ir(result.file, diagnostics, SdfIrLimits { 2U, 100U, 16U });
    require_diagnostic(diagnostics, "FSIM-SDF-IR-002");
    require(!result.file.normalized_ir,
        "identity-byte rejection must discard the transactional IR build");

    const auto malformed = parse_sdf(SourceText { "malformed-ir.sdf", R"((DELAYFILE
  (SDFVERSION "4.0")
  (CELL (CELLTYPE "X") (INSTANCE top)
    (DELAY (ABSOLUTE (IOPATH A Z (1e999999999999999999999)))))
))" });
    require(!malformed.ok() && !malformed.file.normalized_ir,
        "failed parsing or normalization must never publish an IR snapshot");
}

} // namespace

int main()
{
    try {
        test_immutable_ordered_ir_and_introspection();
        test_revision_equality_and_distinctions();
        test_transactional_invariant_and_resource_rejection();
        std::cout << "SDF immutable IR tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SDF immutable IR test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
