// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_cell_resolution.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using fsim::app::SdfAnnotationScopeRequest;
using fsim::app::SdfCellResolution;
using fsim::app::SdfCellResolutionLimits;
using fsim::app::SdfScopeSelection;
using fsim::frontend::Diagnostic;

static_assert(std::is_same_v<
    decltype(std::declval<const SdfCellResolution&>().cells()),
    std::span<const fsim::app::SdfResolvedCell>>);

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
        std::string message = "missing SDF cell-resolution diagnostic ";
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

fsim::elaboration::SpecializationInfo specialization(const std::uint32_t id,
    std::string unit, std::string instance,
    const fsim::frontend::Language language, const bool is_cell = false)
{
    fsim::elaboration::SpecializationInfo result;
    result.id = id;
    result.unit = std::move(unit);
    result.instance = std::move(instance);
    result.language = language;
    result.is_cell = is_cell;
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim::elaboration;
    using fsim::frontend::Language;
    ElaboratedDesignState state;
    state.top = "alpha";
    state.roots = { "alpha", "beta", "configured", "native" };
    state.specializations = {
        specialization(0U, "sv:work.top", "alpha",
            Language::SystemVerilog2017),
        specialization(1U, "vhdl:work.counter(rtl)", "alpha.VHDL_LEAF",
            Language::Vhdl2008, true),
        specialization(2U, "sv:work.buf", "alpha.VHDL_LEAF.child",
            Language::SystemVerilog2017, true),
        specialization(3U, "sv:work.top", "beta",
            Language::SystemVerilog2017),
        specialization(4U, "vhdl:work.counter(fast)", "beta.vhdl_leaf",
            Language::Vhdl2008, true),
        specialization(5U, "sv:work.buf", "beta.vhdl_leaf.child",
            Language::SystemVerilog2017, true),
        specialization(6U, "vhdl:work.counter(rtl)", "beta.nonphysical",
            Language::Vhdl2008, false),
        specialization(7U, "sv:work.escaped", "alpha.dot.label",
            Language::SystemVerilog2017, true),
    };
    auto configured = specialization(8U, "vhdl:work.selected_configuration",
        "configured", Language::Vhdl2008);
    configured.parameter_identity_values.emplace_back("__configuration",
        "vhdl-configuration-v2;library=work;name=selected_configuration;entity=configured_top;architecture=rtl");
    state.specializations.push_back(std::move(configured));
    SystemCInstanceInfo native_root;
    native_root.id = 0U;
    native_root.target = "systemc:work.native_top";
    native_root.instance = "native";
    SystemCInstanceInfo native_device;
    native_device.id = 1U;
    native_device.target = "systemc:work.device";
    native_device.instance = "native.device";
    state.systemc_instances
        = { std::move(native_root), std::move(native_device) };
    auto design = ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(),
        "mixed hierarchy cell-resolution fixture must be valid");
    return std::move(*design);
}

fsim::frontend::SdfParseResult parse_cells(const std::string_view cells,
    const std::string_view version = "4.0",
    const std::string_view source_name = "cells.sdf")
{
    using namespace fsim::frontend;
    std::string source = "(DELAYFILE (SDFVERSION \"";
    source += version;
    source += "\") (DESIGN \"mixed\") (DIVIDER /) (TIMESCALE 1 ns) ";
    source += cells;
    source += ')';
    return parse_sdf(SourceText { std::string { source_name }, std::move(source) });
}

std::shared_ptr<const fsim::app::SdfAnnotationScope> bind_all(
    const fsim::frontend::SdfFile& sdf,
    const fsim::elaboration::ElaboratedDesign& design)
{
    using namespace fsim::app;
    const auto scope = bind_sdf_annotation_scope(sdf, design, "project-alpha",
        "design-42", SdfAnnotationScopeRequest { SdfScopeSelection::All, { }, "project-alpha", "design-42" });
    require(scope.ok(), "cell-resolution fixture scope must bind");
    return scope.scope;
}

std::string delay_cell(const std::string_view type,
    const std::string_view instance)
{
    std::string cell = "(CELL (CELLTYPE \"";
    cell += type;
    cell += "\") ";
    cell += instance;
    cell += " (DELAY (ABSOLUTE (DEVICE (1)))))";
    return cell;
}

void test_exact_wildcard_empty_and_mixed_case_resolution()
{
    using namespace fsim::app;
    const auto design = make_design();
    std::string cells;
    cells += delay_cell("COUNTER", "(INSTANCE alpha/VHDL_LEAF)");
    cells += delay_cell("buf", "(INSTANCE VHDL_LEAF/CHILD)");
    cells += delay_cell("device", "(INSTANCE native/device)");
    cells += delay_cell("counter", "(INSTANCE *)");
    cells += delay_cell("native_top", "(INSTANCE)");
    cells += delay_cell("escaped", "(INSTANCE alpha/dot\\.label)");
    cells += delay_cell("CONFIGURED_TOP", "(INSTANCE)");
    const auto sdf = parse_cells(cells);
    require(sdf.ok(), "positive cell-resolution SDF must parse");
    const auto result = resolve_sdf_cells(bind_all(sdf.file, design), design);
    require(result.ok() && result.resolution->cells().size() == 7U,
        "all valid cells must publish one transactional resolution");

    const auto& exact = result.resolution->cells()[0];
    require(exact.targets.size() == 1U
            && exact.targets.front().instance_path == "alpha.VHDL_LEAF"
            && exact.targets.front().unit_identity
                == "vhdl:work.counter(rtl)"
            && exact.targets.front().declaration_id == 1U,
        "exact resolution must retain declaration and specialization identity");
    require(result.resolution->cells()[1].targets.front().instance_path
            == "alpha.VHDL_LEAF.child",
        "mixed hierarchy path matching must use the parent language case policy per segment");
    require(result.resolution->cells()[2].targets.front().instance_path
            == "native.device",
        "native SystemC instances must participate in deterministic exact resolution");
    require(result.resolution->cells()[3].targets.size() == 3U
            && result.resolution->cells()[3].targets[0].instance_path
                == "alpha.VHDL_LEAF"
            && result.resolution->cells()[3].targets[1].instance_path
                == "beta.nonphysical"
            && result.resolution->cells()[3].targets[2].instance_path
                == "beta.vhdl_leaf",
        "SDF 4.0 wildcard resolution must publish every type match in lexical path order");
    require(result.resolution->cells()[4].targets.size() == 1U
            && result.resolution->cells()[4].targets.front().instance_path
                == "native",
        "empty INSTANCE must mean a matching selected root, not arbitrary descendants");
    require(result.resolution->cells()[5].targets.front().instance_path
            == "alpha.dot.label",
        "escaped divider characters must remain inside one normalized instance segment");
    require(result.resolution->cells()[6].targets.front().unit_identity
            == "vhdl:work.selected_configuration",
        "VHDL configuration roots must match their configured entity while retaining configuration identity");
    require(result.resolution->find_cell(1U) == &exact
            && result.resolution->find_cell(0U) == nullptr
            && !result.resolution->semantic_identity().empty(),
        "cell resolution must provide stable direct lookup and semantic identity");

    const auto repeated = resolve_sdf_cells(bind_all(sdf.file, design), design);
    require(repeated.ok()
            && repeated.resolution->semantic_identity()
                == result.resolution->semantic_identity()
            && std::ranges::equal(
                repeated.resolution->cells(), result.resolution->cells()),
        "repeated cell resolution must be byte-stable and deterministically ordered");
}

fsim::frontend::SdfParseResult parse_revision_exact(
    const std::string_view version)
{
    const bool sdf21 = version == "OVI 2.1";
    return parse_cells(delay_cell("COUNTER",
                           sdf21
                               ? "(INSTANCE alpha) (INSTANCE VHDL_LEAF)"
                               : "(INSTANCE alpha/VHDL_LEAF)"),
        version, "revision-cells.sdf");
}

void test_revision_identity_and_physical_wildcard()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto sdf21 = parse_revision_exact("OVI 2.1");
    const auto sdf30 = parse_revision_exact("OVI 3.0");
    const auto sdf40 = parse_revision_exact("4.0");
    require(sdf21.ok() && sdf30.ok() && sdf40.ok(),
        "equivalent revision fixtures must parse");
    const auto r21 = resolve_sdf_cells(bind_all(sdf21.file, design), design);
    const auto r30 = resolve_sdf_cells(bind_all(sdf30.file, design), design);
    const auto r40 = resolve_sdf_cells(bind_all(sdf40.file, design), design);
    require(r21.ok() && r30.ok() && r40.ok()
            && r21.resolution->semantic_identity()
                == r30.resolution->semantic_identity()
            && r30.resolution->semantic_identity()
                == r40.resolution->semantic_identity(),
        "overlapping SDF 2.1/3.0/4.0 exact cells must resolve identically");

    const auto wildcard21 = parse_cells(
        delay_cell("counter", "(INSTANCE *)"), "OVI 2.1");
    const auto wildcard30 = parse_cells(
        delay_cell("counter", "(INSTANCE *)"), "OVI 3.0");
    const auto physical = resolve_sdf_cells(
        bind_all(wildcard21.file, design), design);
    const auto ordinary = resolve_sdf_cells(
        bind_all(wildcard30.file, design), design);
    require(physical.ok() && ordinary.ok()
            && physical.resolution->cells().front().targets.size() == 2U
            && ordinary.resolution->cells().front().targets.size() == 3U,
        "SDF 2.1 wildcard must retain its physical-cell restriction while SDF 3.0 does not");
}

void test_missing_ambiguous_stale_and_resource_rejection()
{
    using namespace fsim::app;
    const auto design = make_design();

    const auto missing_sdf = parse_cells(
        delay_cell("counter", "(INSTANCE absent/path)"));
    const auto missing = resolve_sdf_cells(
        bind_all(missing_sdf.file, design), design);
    require(!missing.resolution,
        "missing instance must not publish a partial cell resolution");
    const auto& missing_diagnostic
        = require_diagnostic(missing.diagnostics, "FSIM-SDF-RESOLVE-002");
    require(missing_diagnostic.message.find("alpha.VHDL_LEAF[counter]")
                != std::string::npos
            && missing_diagnostic.span.begin
                == missing_sdf.file.normalized_ir->cells().front().span.begin,
        "missing diagnostics must carry sorted actionable candidates and exact coordinates");

    const auto ambiguous_sdf
        = parse_cells(delay_cell("top", "(INSTANCE)"));
    const auto ambiguous = resolve_sdf_cells(
        bind_all(ambiguous_sdf.file, design), design);
    require(!ambiguous.resolution,
        "ambiguous empty-root selection must not publish a resolution");
    const auto& ambiguity
        = require_diagnostic(ambiguous.diagnostics, "FSIM-SDF-RESOLVE-003");
    require(ambiguity.message.find("alpha") < ambiguity.message.find("beta"),
        "ambiguity candidates must be reported in deterministic lexical order");

    auto stale_state = design.state();
    stale_state.specializations.front().unit = "sv:work.changed_top";
    const auto stale_design
        = fsim::elaboration::ElaboratedDesign::from_state(std::move(stale_state));
    require(stale_design.has_value(), "stale fixture must remain structurally valid");
    const auto exact_sdf = parse_revision_exact("4.0");
    const auto stale = resolve_sdf_cells(
        bind_all(exact_sdf.file, design), *stale_design);
    require(!stale.resolution,
        "scope/design semantic mismatch must reject before cell publication");
    require_diagnostic(stale.diagnostics, "FSIM-SDF-RESOLVE-001");

    const auto scope = bind_all(exact_sdf.file, design);
    const auto bounded = resolve_sdf_cells(scope, design,
        SdfCellResolutionLimits { .max_candidates = 2U });
    require(!bounded.resolution,
        "candidate resource exhaustion must not publish resolution");
    require_diagnostic(bounded.diagnostics, "FSIM-SDF-RESOLVE-005");

    const auto wildcard_sdf = parse_cells(
        delay_cell("counter", "(INSTANCE *)"), "OVI 3.0");
    const auto match_bounded = resolve_sdf_cells(
        bind_all(wildcard_sdf.file, design), design,
        SdfCellResolutionLimits { .max_candidates = 1'000'000U,
            .max_matches = 1U });
    require(!match_bounded.resolution,
        "match resource exhaustion must not publish resolution");
    require_diagnostic(
        match_bounded.diagnostics, "FSIM-SDF-RESOLVE-005");

    auto duplicate_state = design.state();
    auto duplicate = duplicate_state.specializations.at(1U);
    duplicate.id = 9U;
    duplicate_state.specializations.push_back(std::move(duplicate));
    const auto duplicate_design
        = fsim::elaboration::ElaboratedDesign::from_state(
            std::move(duplicate_state));
    require(duplicate_design.has_value(),
        "duplicate-owner fixture must remain structurally loadable");
    const auto duplicate_resolution
        = resolve_sdf_cells(scope, *duplicate_design);
    require(!duplicate_resolution.resolution,
        "duplicate semantic instance ownership must reject transactionally");
    require_diagnostic(
        duplicate_resolution.diagnostics, "FSIM-SDF-RESOLVE-001");

    const auto identity_bounded = resolve_sdf_cells(scope, design,
        SdfCellResolutionLimits { .max_candidates = 1'000'000U,
            .max_matches = 1'000'000U,
            .max_identity_bytes = 16U,
            .max_reported_candidates = 8U });
    require(!identity_bounded.resolution,
        "identity resource exhaustion must not publish resolution");
    require_diagnostic(
        identity_bounded.diagnostics, "FSIM-SDF-RESOLVE-005");
}

} // namespace

int main()
{
    try {
        test_exact_wildcard_empty_and_mixed_case_resolution();
        test_revision_identity_and_physical_wildcard();
        test_missing_ambiguous_stale_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf cell-resolution test failure: " << error.what()
                  << '\n';
        return 1;
    }
    std::cout << "sdf cell-resolution tests passed\n";
    return 0;
}
