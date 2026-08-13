// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mapping_validation.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfMappingValidationResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error("missing mapping diagnostic "
            + std::string { code });
    return *found;
}

fsim::elaboration::SpecializationInfo specialization(const std::uint32_t id,
    std::string unit, std::string instance,
    const fsim::frontend::Language language)
{
    fsim::elaboration::SpecializationInfo result;
    result.id = id;
    result.unit = std::move(unit);
    result.instance = std::move(instance);
    result.language = language;
    return result;
}

void add_signal(fsim::elaboration::ElaboratedDesignState& state,
    std::string name, const std::size_t width, const bool port,
    const fsim::frontend::PortDirection direction,
    const fsim::frontend::Language language)
{
    const auto id = static_cast<fsim::runtime::simir::SignalId>(
        state.signal_info.size());
    fsim::elaboration::SignalInfo info;
    info.id = id;
    info.name = name;
    info.width = width;
    info.type_name = language == fsim::frontend::Language::Vhdl2008
        ? "std_logic_vector"
        : "logic";
    info.source_domain = language == fsim::frontend::Language::Vhdl2008
        ? fsim::frontend::ValueDomain::Logic9
        : fsim::frontend::ValueDomain::Logic4;
    info.is_port = port;
    info.direction = direction;
    if (width > 1U) {
        info.packed_range = fsim::frontend::PackedRange {
            static_cast<std::int64_t>(width - 1U), 0, true
        };
    }
    state.signal_info.push_back(std::move(info));
    state.signals.emplace_back(name,
        fsim::runtime::PackedLogic4(width, fsim::runtime::Logic4::zero));
    state.signal_names.emplace_back(std::move(name), id);
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim::elaboration;
    using fsim::frontend::Language;
    using fsim::frontend::PortDirection;
    ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top", "vhdl", "native" };
    state.specializations = {
        specialization(0U, "sv:work.top", "top",
            Language::SystemVerilog2017),
        specialization(1U, "vhdl:work.gate(rtl)", "vhdl",
            Language::Vhdl2008),
    };
    add_signal(state, "top.A", 4U, true, PortDirection::Input,
        Language::SystemVerilog2017);
    add_signal(state, "top.Z", 1U, true, PortDirection::Output,
        Language::SystemVerilog2017);
    add_signal(state, "top.CLK", 1U, true, PortDirection::Input,
        Language::SystemVerilog2017);
    add_signal(state, "vhdl.a", 1U, true, PortDirection::Input,
        Language::Vhdl2008);
    add_signal(state, "vhdl.z", 1U, true, PortDirection::Output,
        Language::Vhdl2008);
    add_signal(state, "native.in", 1U, true, PortDirection::Input,
        Language::SystemVerilog2017);
    add_signal(state, "native.out", 1U, true, PortDirection::Output,
        Language::SystemVerilog2017);

    SystemCInstanceInfo native;
    native.id = 0U;
    native.target = "systemc:work.native_top";
    native.instance = "native";
    native.native_handle = 100U;
    native.ports = { { "in", 101U, 5U }, { "out", 102U, 6U } };
    state.systemc_instances.push_back(std::move(native));
    SystemCNamedObjectInfo native_in;
    native_in.kind = SystemCNamedObjectKind::port;
    native_in.native_handle = 101U;
    native_in.name = "native.in";
    native_in.parent = "native";
    native_in.type_name = "sc_in<bool>";
    native_in.signal = 5U;
    SystemCNamedObjectInfo native_out;
    native_out.kind = SystemCNamedObjectKind::port;
    native_out.native_handle = 102U;
    native_out.name = "native.out";
    native_out.parent = "native";
    native_out.type_name = "sc_out<bool>";
    native_out.signal = 6U;
    state.systemc_objects
        = { std::move(native_in), std::move(native_out) };

    auto design = ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "mapping-validation design must be valid");
    return std::move(*design);
}

std::string sdf_source(const std::string_view revision)
{
    return "(DELAYFILE\n"
           "  (SDFVERSION \""
        + std::string { revision }
    + "\")\n"
      "  (DIVIDER .)\n"
      "  (CELL (CELLTYPE \"top\") (INSTANCE top)\n"
      "    (DELAY (ABSOLUTE\n"
      "      (IOPATH A[0] Z (1))\n"
      "      (PORT Z (1))\n"
      "      (DEVICE (1))))\n"
      "    (TIMINGCHECK (SETUP A[1] CLK (1))))\n"
      "  (CELL (CELLTYPE \"gate\") (INSTANCE vhdl)\n"
      "    (DELAY (ABSOLUTE (IOPATH A Z (1)))))\n"
      "  (CELL (CELLTYPE \"native_top\") (INSTANCE native)\n"
      "    (DELAY (ABSOLUTE (IOPATH in out (1)))))\n"
      ")";
}

std::shared_ptr<const fsim::app::SdfEndpointResolution> resolve_revision(
    const std::string_view revision,
    const fsim::elaboration::ElaboratedDesign& design)
{
    using namespace fsim::app;
    const auto parsed = fsim::frontend::parse_sdf(
        { "mapping.sdf", sdf_source(revision) });
    require(parsed.ok(), "mapping-validation SDF must parse");
    SdfAnnotationScopeRequest request;
    request.selection = SdfScopeSelection::All;
    request.expected_project_identity = "project:mapping";
    request.expected_design_identity = "design:mapping";
    const auto scope = bind_sdf_annotation_scope(parsed.file, design,
        "project:mapping", "design:mapping", request);
    require(scope.ok(), "mapping-validation scope must bind");
    const auto cells = resolve_sdf_cells(scope.scope, design);
    require(cells.ok(), "mapping-validation cells must resolve");
    const auto endpoints = resolve_sdf_endpoints(cells.resolution, design);
    require(endpoints.ok(), "mapping-validation endpoints must resolve");
    return endpoints.resolution;
}

std::shared_ptr<const fsim::app::SdfEndpointResolution> with_nodes(
    const std::shared_ptr<const fsim::app::SdfEndpointResolution>& base,
    std::vector<fsim::app::SdfResolvedNodeEndpoints> nodes)
{
    return std::make_shared<const fsim::app::SdfEndpointResolution>(
        base->cells(), std::move(nodes), std::string { base->semantic_identity() });
}

void test_valid_summary_and_revision_identity()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto sdf21 = validate_sdf_mapping(resolve_revision("2.1", design), design);
    const auto sdf30 = validate_sdf_mapping(resolve_revision("3.0", design), design);
    const auto sdf40 = validate_sdf_mapping(resolve_revision("4.0", design), design);
    require(sdf21.ok() && sdf30.ok() && sdf40.ok(),
        "all governed revisions must validate");
    require(sdf40.summary->targets().size() == 3U
            && sdf40.summary->annotation_count() == 6U
            && sdf40.summary->delay_annotation_count() == 5U
            && sdf40.summary->timing_check_annotation_count() == 1U
            && sdf40.summary->timing_environment_annotation_count() == 0U
            && sdf40.summary->endpoint_count() >= 9U,
        "summary must retain exact target and category totals");
    require(sdf21.summary->semantic_identity()
                == sdf30.summary->semantic_identity()
            && sdf30.summary->semantic_identity()
                == sdf40.summary->semantic_identity(),
        "equivalent revision mappings must have one summary identity");
    const auto repeated
        = validate_sdf_mapping(resolve_revision("4.0", design), design);
    require(repeated.ok()
            && repeated.summary->semantic_identity()
                == sdf40.summary->semantic_identity()
            && std::ranges::equal(
                repeated.summary->targets(), sdf40.summary->targets()),
        "mapping summary publication must be deterministic");
}

void test_duplicate_conflict_and_unconsumed_rejection()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto base = resolve_revision("4.0", design);
    std::vector<SdfResolvedNodeEndpoints> nodes(
        base->nodes().begin(), base->nodes().end());

    auto duplicate_nodes = nodes;
    duplicate_nodes.insert(duplicate_nodes.begin() + 1U, duplicate_nodes.front());
    const auto duplicate
        = validate_sdf_mapping(with_nodes(base, std::move(duplicate_nodes)), design);
    require(!duplicate.ok(), "duplicate node/target mapping must fail");
    require_diagnostic(duplicate, "FSIM-SDF-MAP-002");

    auto conflict_nodes = nodes;
    conflict_nodes.front().endpoints.front().object_width += 1U;
    const auto conflict
        = validate_sdf_mapping(with_nodes(base, std::move(conflict_nodes)), design);
    require(!conflict.ok(), "width conflict must fail");
    const auto& conflict_diagnostic
        = require_diagnostic(conflict, "FSIM-SDF-MAP-003");
    require(conflict_diagnostic.span.begin.line != 0U,
        "mapping conflict must retain the SDF coordinate");

    auto unsupported_nodes = nodes;
    unsupported_nodes.front().endpoints.front().object_kind
        = static_cast<SdfEndpointObjectKind>(999U);
    const auto unsupported = validate_sdf_mapping(
        with_nodes(base, std::move(unsupported_nodes)), design);
    require(!unsupported.ok(), "unsupported object kind must fail");
    require_diagnostic(unsupported, "FSIM-SDF-MAP-003");

    auto direction_nodes = nodes;
    direction_nodes.front().endpoints.front().direction
        = fsim::frontend::PortDirection::Output;
    const auto direction = validate_sdf_mapping(
        with_nodes(base, std::move(direction_nodes)), design);
    require(!direction.ok(), "direction conflict must fail");
    require_diagnostic(direction, "FSIM-SDF-MAP-003");

    auto select_nodes = nodes;
    auto& select = select_nodes.front().endpoints.front().select;
    require(select.has_value(), "selector-conflict fixture needs a selector");
    select->left = 99;
    select->right = 99;
    select->width = 1U;
    const auto selector = validate_sdf_mapping(
        with_nodes(base, std::move(select_nodes)), design);
    require(!selector.ok(), "selector range conflict must fail");
    require_diagnostic(selector, "FSIM-SDF-MAP-003");

    auto missing_nodes = nodes;
    missing_nodes.pop_back();
    const auto missing
        = validate_sdf_mapping(with_nodes(base, std::move(missing_nodes)), design);
    require(!missing.ok(), "unconsumed construct must fail");
    require_diagnostic(missing, "FSIM-SDF-MAP-004");

    auto stale_nodes = nodes;
    stale_nodes.front().endpoints.front().signal
        = std::numeric_limits<fsim::runtime::simir::SignalId>::max();
    const auto stale
        = validate_sdf_mapping(with_nodes(base, std::move(stale_nodes)), design);
    require(!stale.ok(), "stale signal identity must fail");
    require_diagnostic(stale, "FSIM-SDF-MAP-001");
}

void test_overlap_and_resource_rejection()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto base = resolve_revision("4.0", design);
    std::vector<SdfResolvedCell> cells(
        base->cells()->cells().begin(), base->cells()->cells().end());
    require(cells.size() >= 2U, "overlap fixture needs two cells");
    cells[1].selector = fsim::frontend::SdfInstanceSelectorKind::Wildcard;
    cells[1].targets.push_back(cells.front().targets.front());
    auto overlapping_cells = std::make_shared<const SdfCellResolution>(
        base->cells()->scope(), std::move(cells), "overlapping-cells");
    std::vector<SdfResolvedNodeEndpoints> nodes(
        base->nodes().begin(), base->nodes().end());
    auto overlapping = std::make_shared<const SdfEndpointResolution>(
        std::move(overlapping_cells), nodes, "overlapping-endpoints");
    const auto overlap = validate_sdf_mapping(overlapping, design);
    require(!overlap.ok(), "wildcard/exact target overlap must fail");
    require_diagnostic(overlap, "FSIM-SDF-MAP-003");

    SdfMappingValidationLimits limits;
    limits.max_mappings = base->nodes().size() - 1U;
    const auto mapping_limit = validate_sdf_mapping(base, design, limits);
    require(!mapping_limit.ok(), "mapping resource limit must fail");
    require_diagnostic(mapping_limit, "FSIM-SDF-MAP-005");

    limits = { };
    limits.max_endpoints = 0U;
    const auto endpoint_limit = validate_sdf_mapping(base, design, limits);
    require(!endpoint_limit.ok(), "endpoint resource limit must fail");
    require_diagnostic(endpoint_limit, "FSIM-SDF-MAP-005");

    limits = { };
    limits.max_targets = 1U;
    const auto target_limit = validate_sdf_mapping(base, design, limits);
    require(!target_limit.ok(), "target resource limit must fail");
    require_diagnostic(target_limit, "FSIM-SDF-MAP-005");

    limits = { };
    limits.max_identity_bytes = 8U;
    const auto identity_limit = validate_sdf_mapping(base, design, limits);
    require(!identity_limit.ok(), "summary identity resource limit must fail");
    require_diagnostic(identity_limit, "FSIM-SDF-MAP-005");
}
} // namespace

int main()
{
    try {
        test_valid_summary_and_revision_identity();
        test_duplicate_conflict_and_unconsumed_rejection();
        test_overlap_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
