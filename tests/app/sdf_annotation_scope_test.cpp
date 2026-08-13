// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_annotation_scope.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using fsim::app::SdfAnnotationScope;
using fsim::app::SdfAnnotationScopeRequest;
using fsim::app::SdfScopeSelection;
using fsim::frontend::Diagnostic;

static_assert(std::is_same_v<
    decltype(std::declval<const SdfAnnotationScope&>().roots()),
    std::span<const fsim::app::SdfAnnotationRoot>>);

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

const Diagnostic& require_diagnostic(
    const std::vector<Diagnostic>& diagnostics, const std::string_view code)
{
    const auto found = std::ranges::find_if(diagnostics,
        [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
    if (found == diagnostics.end()) {
        std::string message = "missing SDF scope diagnostic ";
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

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim::elaboration;
    ElaboratedDesignState state;
    state.top = "mixed";
    state.roots = { "sv_top", "sc_top", "v_top", "vhdl_top" };
    SpecializationInfo sv;
    sv.id = 0U;
    sv.unit = "work.sv_top";
    sv.instance = "sv_top";
    sv.language = fsim::frontend::Language::SystemVerilog2017;
    SpecializationInfo vhdl;
    vhdl.id = 1U;
    vhdl.unit = "work.vhdl_top(rtl)";
    vhdl.instance = "vhdl_top";
    vhdl.language = fsim::frontend::Language::Vhdl2008;
    SpecializationInfo verilog;
    verilog.id = 2U;
    verilog.unit = "work.v_top";
    verilog.instance = "v_top";
    verilog.language = fsim::frontend::Language::Verilog2005;
    state.specializations
        = { std::move(sv), std::move(vhdl), std::move(verilog) };
    SystemCInstanceInfo systemc;
    systemc.id = 0U;
    systemc.target = "native.counter";
    systemc.instance = "sc_top";
    state.systemc_instances = { std::move(systemc) };
    auto design = ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "minimal mixed-language elaborated state must be valid");
    return std::move(*design);
}

fsim::frontend::SdfParseResult parse_scope_sdf(
    const std::string_view version = "4.0",
    const std::string_view source_name = "scope.sdf")
{
    using namespace fsim::frontend;
    std::string source = "(DELAYFILE (SDFVERSION \"";
    source += version;
    source += "\") (DESIGN \"mixed\") (DIVIDER /) (TIMESCALE 1 ns)";
    source += " (CELL (CELLTYPE \"BUF\") ";
    if (version == "OVI 2.1" || version == "2.1")
        source += "(INSTANCE sv_top) (INSTANCE u0)";
    else
        source += "(INSTANCE sv_top/u0)";
    source += " (DELAY (ABSOLUTE (IOPATH A Z (1))))))";
    return parse_sdf(SourceText { std::string { source_name }, std::move(source) });
}

SdfAnnotationScopeRequest request(const SdfScopeSelection selection,
    std::vector<std::string> aliases)
{
    return SdfAnnotationScopeRequest { selection, std::move(aliases),
        "project-alpha", "design-42" };
}

void test_deterministic_multi_root_scope()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto sdf = parse_scope_sdf();
    require(sdf.ok() && sdf.file.normalized_ir,
        "scope fixture must produce normalized SDF IR");

    const auto all = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42", request(SdfScopeSelection::All, { }));
    require(all.ok() && all.scope->roots().size() == 4U,
        "all-root scope must publish every elaborated root");
    require(all.scope->roots()[0].alias == "sc_top"
            && all.scope->roots()[1].alias == "sv_top"
            && all.scope->roots()[2].alias == "v_top"
            && all.scope->roots()[3].alias == "vhdl_top",
        "all-root scope must sort roots by exact elaborated alias");
    require(all.scope->roots()[0].language == SdfScopeRootLanguage::SystemC
            && all.scope->roots()[1].language
                == SdfScopeRootLanguage::SystemVerilog
            && all.scope->roots()[2].language == SdfScopeRootLanguage::Verilog
            && all.scope->roots()[3].language == SdfScopeRootLanguage::Vhdl
            && all.scope->roots()[0].case_policy
                == SdfHierarchyCasePolicy::Sensitive
            && all.scope->roots()[3].case_policy
                == SdfHierarchyCasePolicy::AsciiInsensitive,
        "scope roots must bind native language and hierarchy case policy");
    require(all.scope->source_name() == "scope.sdf"
            && all.scope->source_semantic_identity()
                == sdf.file.normalized_ir->semantic_identity()
            && all.scope->sdf_design_name() == "mixed"
            && all.scope->hierarchy_divider() == '/'
            && all.scope->project_identity() == "project-alpha"
            && all.scope->design_identity() == "design-42"
            && !all.scope->semantic_identity().empty(),
        "scope must bind source, design header, divider, project, and design identities");

    const auto reversed = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Set, { "vhdl_top", "sc_top" }));
    const auto ordered = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Set, { "sc_top", "vhdl_top" }));
    require(reversed.ok() && ordered.ok()
            && reversed.scope->semantic_identity()
                == ordered.scope->semantic_identity()
            && std::ranges::equal(
                reversed.scope->roots(), ordered.scope->roots()),
        "set scope identity must be independent of request order");

    const auto single = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Single, { "sv_top" }));
    require(single.ok() && single.scope->roots().size() == 1U
            && single.scope->roots().front().selected_identity == "work.sv_top",
        "single-root scope must retain the selected semantic unit identity");
}

void test_revision_and_relocation_identity()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto sdf40 = parse_scope_sdf("4.0", "original/scope.sdf");
    const auto relocated = parse_scope_sdf("4.0", "relocated/scope.sdf");
    const auto sdf30 = parse_scope_sdf("3.0", "scope30.sdf");
    const auto sdf21 = parse_scope_sdf("OVI 2.1", "scope21.sdf");
    const auto selection = request(SdfScopeSelection::Single, { "sv_top" });
    const auto first = bind_sdf_annotation_scope(
        sdf40.file, design, "project-alpha", "design-42", selection);
    const auto moved = bind_sdf_annotation_scope(
        relocated.file, design, "project-alpha", "design-42", selection);
    const auto old_revision = bind_sdf_annotation_scope(
        sdf30.file, design, "project-alpha", "design-42", selection);
    const auto oldest_revision = bind_sdf_annotation_scope(
        sdf21.file, design, "project-alpha", "design-42", selection);
    require(first.ok() && moved.ok() && old_revision.ok()
            && oldest_revision.ok()
            && first.scope->semantic_identity() == moved.scope->semantic_identity()
            && first.scope->semantic_identity()
                == old_revision.scope->semantic_identity()
            && first.scope->semantic_identity()
                == oldest_revision.scope->semantic_identity()
            && first.scope->source_name() != moved.scope->source_name(),
        "equivalent revision and relocated source provenance must not perturb scope semantics");
}

void test_transactional_scope_rejection()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto sdf = parse_scope_sdf();

    auto cross_project = request(SdfScopeSelection::Single, { "sv_top" });
    cross_project.expected_project_identity = "other-project";
    const auto cross = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42", cross_project);
    require(!cross.scope && !cross.ok(),
        "cross-project failure must not publish partial scope state");
    require(require_diagnostic(cross.diagnostics, "FSIM-SDF-SCOPE-003")
                .span.begin
            == sdf.file.span.begin,
        "scope diagnostics must retain exact SDF coordinates");

    auto stale = request(SdfScopeSelection::Single, { "sv_top" });
    stale.expected_design_identity = "design-41";
    const auto stale_result = bind_sdf_annotation_scope(
        sdf.file, design, "project-alpha", "design-42", stale);
    require(!stale_result.scope, "stale design failure must not publish scope");
    require_diagnostic(stale_result.diagnostics, "FSIM-SDF-SCOPE-004");

    const auto missing = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Single, { "missing" }));
    require(!missing.scope, "missing selected root must not publish scope");
    require_diagnostic(missing.diagnostics, "FSIM-SDF-SCOPE-002");

    const auto duplicate = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Set, { "sv_top", "sv_top" }));
    require(!duplicate.scope, "duplicate selected roots must not publish scope");
    require_diagnostic(duplicate.diagnostics, "FSIM-SDF-SCOPE-002");

    const auto malformed_single = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Single, { "sv_top", "vhdl_top" }));
    require(!malformed_single.scope,
        "malformed selection policy must not publish scope");
    require_diagnostic(malformed_single.diagnostics, "FSIM-SDF-SCOPE-002");

    auto incomplete = sdf.file;
    incomplete.normalized_ir.reset();
    const auto incomplete_result = bind_sdf_annotation_scope(incomplete, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Single, { "sv_top" }));
    require(!incomplete_result.scope,
        "incomplete normalized input must not publish scope");
    require_diagnostic(incomplete_result.diagnostics, "FSIM-SDF-SCOPE-001");

    const auto bounded = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::All, { }),
        SdfAnnotationScopeLimits { .max_roots = 3U,
            .max_identity_bytes = 1U << 20U });
    require(!bounded.scope, "root resource failure must not publish scope");
    require_diagnostic(bounded.diagnostics, "FSIM-SDF-SCOPE-005");

    const auto bounded_identity = bind_sdf_annotation_scope(sdf.file, design,
        "project-alpha", "design-42",
        request(SdfScopeSelection::Single, { "sv_top" }),
        SdfAnnotationScopeLimits { .max_roots = 4'096U,
            .max_identity_bytes = 16U });
    require(!bounded_identity.scope,
        "identity resource failure must not publish scope");
    require_diagnostic(bounded_identity.diagnostics, "FSIM-SDF-SCOPE-005");
}

} // namespace

int main()
{
    try {
        test_deterministic_multi_root_scope();
        test_revision_and_relocation_identity();
        test_transactional_scope_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf annotation scope test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sdf annotation scope tests passed\n";
    return 0;
}
