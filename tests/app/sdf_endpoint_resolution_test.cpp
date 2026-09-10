// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_endpoint_resolution.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const std::vector<fsim::frontend::Diagnostic>& diagnostics,
    const std::string_view code)
{
    const auto found = std::ranges::find_if(diagnostics,
        [&](const fsim::frontend::Diagnostic& diagnostic) {
            return diagnostic.code == code;
        });
    if (found == diagnostics.end()) {
        std::string message
            = "missing expected diagnostic " + std::string { code } + "; observed";
        for (const auto& diagnostic : diagnostics)
            message += ' ' + diagnostic.code;
        throw std::runtime_error(message);
    }
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
    result.is_cell = true;
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
    add_signal(state, "top.net", 4U, false, PortDirection::Unknown,
        Language::SystemVerilog2017);
    add_signal(state, "top.CLK", 1U, true, PortDirection::Input,
        Language::SystemVerilog2017);
    add_signal(state, "top.EN", 1U, false, PortDirection::Unknown,
        Language::SystemVerilog2017);
    add_signal(state, "vhdl.a", 1U, true, PortDirection::Input,
        Language::Vhdl2008);
    add_signal(state, "vhdl.z", 1U, true, PortDirection::Output,
        Language::Vhdl2008);
    add_signal(state, "native.in", 1U, true, PortDirection::Input,
        Language::SystemVerilog2017);
    add_signal(state, "native.out", 1U, true, PortDirection::Output,
        Language::SystemVerilog2017);
    add_signal(state, "top.actual", 4U, false, PortDirection::Unknown,
        Language::SystemVerilog2017);

    BoundaryConversionInfo conversion;
    conversion.kind = BoundaryConversionKind::state_domain_alias;
    conversion.path = "top.A";
    conversion.formal_signal = 0U;
    conversion.actual_signal = 9U;
    conversion.direction = PortDirection::Input;
    conversion.formal_width = 4U;
    conversion.actual_width = 4U;
    state.boundary_conversions.push_back(std::move(conversion));

    VerilogSpecifyPathInfo path;
    path.id = 0U;
    path.identity = "sdf:iopath:top:A[0]:Z";
    path.instance = "top";
    path.sources.push_back({ 0U, 0U, 1U });
    path.destinations.push_back({ 1U, 0U, 1U });
    path.source_edge = fsim::frontend::VerilogSpecifyEdge::Posedge;
    path.delays.push_back(1U);
    state.verilog_specify_paths.push_back(path);
    auto wrong_edge_path = path;
    wrong_edge_path.id = 1U;
    wrong_edge_path.identity = "sdf:iopath:top:A[0]:Z:negedge";
    wrong_edge_path.source_edge
        = fsim::frontend::VerilogSpecifyEdge::Negedge;
    state.verilog_specify_paths.push_back(std::move(wrong_edge_path));
    auto wrong_select_path = path;
    wrong_select_path.id = 2U;
    wrong_select_path.identity = "sdf:iopath:top:A[1]:Z";
    wrong_select_path.sources.front().offset = 1U;
    state.verilog_specify_paths.push_back(std::move(wrong_select_path));
    auto conditional_path = path;
    conditional_path.id = 3U;
    conditional_path.identity = "sdf:iopath:top:A[1]:Z:conditional";
    conditional_path.sources.front().offset = 1U;
    conditional_path.source_edge = fsim::frontend::VerilogSpecifyEdge::None;
    conditional_path.conditional = true;
    conditional_path.condition_program.nodes.push_back(
        fsim::runtime::simir::ModulePathExpressionNode {
            fsim::runtime::simir::ModulePathExpressionOperator::terminal,
            { }, fsim::runtime::PackedLogic4 { }, { 4U, 0U, 1U },
            fsim::runtime::simir::BinaryOperator::bit_and,
            fsim::runtime::simir::LogicalBinaryOperator::logical_and,
            fsim::runtime::simir::ShiftOperator::logical_left,
            fsim::runtime::simir::ReductionOperator::bit_and, 1U, false });
    state.verilog_specify_paths.push_back(std::move(conditional_path));
    auto ifnone_path = path;
    ifnone_path.id = 4U;
    ifnone_path.identity = "sdf:iopath:top:A[2]:Z:ifnone";
    ifnone_path.sources.front().offset = 2U;
    ifnone_path.source_edge = fsim::frontend::VerilogSpecifyEdge::None;
    ifnone_path.ifnone = true;
    state.verilog_specify_paths.push_back(std::move(ifnone_path));

    fsim::runtime::simir::ModuleTimingCheck check;
    check.id = 0U;
    check.identity = "sdf:timingcheck:top:setup:A:CLK";
    check.kind = fsim::runtime::simir::ModuleTimingCheckKind::setup;
    check.reference.terminal.signal = 3U;
    check.reference.terminal.width = 1U;
    fsim::runtime::simir::ModuleTimingEvent data;
    data.terminal.signal = 0U;
    data.terminal.width = 1U;
    check.data = std::move(data);
    check.limits.push_back(1);
    state.verilog_timing_checks.push_back(check);
    auto wrong_edge_check = check;
    wrong_edge_check.id = 1U;
    wrong_edge_check.identity
        = "sdf:timingcheck:top:setup:A:posedge-CLK";
    wrong_edge_check.reference.edge
        = fsim::runtime::simir::ModulePathEdge::posedge;
    state.verilog_timing_checks.push_back(std::move(wrong_edge_check));
    auto wrong_role_check = check;
    wrong_role_check.id = 2U;
    wrong_role_check.identity = "sdf:timingcheck:top:setup:CLK:A";
    wrong_role_check.reference.terminal.signal = 0U;
    wrong_role_check.data->terminal.signal = 3U;
    state.verilog_timing_checks.push_back(std::move(wrong_role_check));

    SystemCInstanceInfo native;
    native.id = 0U;
    native.target = "systemc:work.native_top";
    native.instance = "native";
    native.native_handle = 100U;
    native.ports = { { "in", 101U, 7U }, { "out", 102U, 8U } };
    state.systemc_instances.push_back(std::move(native));
    SystemCNamedObjectInfo native_in;
    native_in.kind = SystemCNamedObjectKind::port;
    native_in.native_handle = 101U;
    native_in.name = "native.in";
    native_in.parent = "native";
    native_in.type_name = "sc_in<bool>";
    native_in.signal = 7U;
    SystemCNamedObjectInfo native_out;
    native_out.kind = SystemCNamedObjectKind::port;
    native_out.native_handle = 102U;
    native_out.name = "native.out";
    native_out.parent = "native";
    native_out.type_name = "sc_out<bool>";
    native_out.signal = 8U;
    state.systemc_objects
        = { std::move(native_in), std::move(native_out) };

    auto design = ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(),
        "mixed-language endpoint-resolution fixture must be valid");
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
      "      (IOPATH (posedge A[0]) Z (1))\n"
      "      (COND (EN == 1) (IOPATH A[1] Z (2)))\n"
      "      (INTERCONNECT A[3] net[1] (1))\n"
      "      (PORT Z (1))\n"
      "      (DEVICE (1))))\n"
      "    (TIMINGCHECK\n"
      "      (SETUP A[0] CLK (1))\n"
      "      (HOLD (COND EN A[1]) CLK (1))))\n"
      "  (CELL (CELLTYPE \"gate\") (INSTANCE vhdl)\n"
      "    (DELAY (ABSOLUTE (IOPATH A Z (1)))))\n"
      "  (CELL (CELLTYPE \"native_top\") (INSTANCE native)\n"
      "    (DELAY (ABSOLUTE (IOPATH in out (1)))))\n"
      ")";
}

fsim::app::SdfEndpointResolutionResult resolve_source(
    const std::string_view source_name, const std::string_view source,
    const fsim::elaboration::ElaboratedDesign& design,
    const fsim::app::SdfEndpointResolutionLimits limits = { })
{
    using namespace fsim::app;
    const auto parsed = fsim::frontend::parse_sdf(
        { std::string { source_name }, std::string { source } });
    if (!parsed.ok()) {
        std::string message = "endpoint SDF fixture must parse; observed";
        for (const auto& diagnostic : parsed.diagnostics) {
            message += ' ' + diagnostic.code + '@'
                + std::to_string(diagnostic.span.begin.line) + ':'
                + diagnostic.message;
        }
        throw std::runtime_error(message);
    }
    SdfAnnotationScopeRequest request;
    request.selection = SdfScopeSelection::All;
    request.expected_project_identity = "project:endpoints";
    request.expected_design_identity = "design:endpoints";
    const auto scope = bind_sdf_annotation_scope(parsed.file, design,
        "project:endpoints", "design:endpoints", request);
    require(scope.ok(), "endpoint annotation scope must bind");
    const auto cells = resolve_sdf_cells(scope.scope, design);
    require(cells.ok(), "endpoint cell selectors must resolve");
    return resolve_sdf_endpoints(cells.resolution, design, limits);
}

fsim::app::SdfEndpointResolutionResult resolve_revision(
    const std::string_view revision,
    const fsim::elaboration::ElaboratedDesign& design,
    const fsim::app::SdfEndpointResolutionLimits limits = { })
{
    return resolve_source(
        "endpoints.sdf", sdf_source(revision), design, limits);
}

const fsim::app::SdfResolvedNodeEndpoints& find_mapping(
    const fsim::app::SdfEndpointResolution& resolution,
    const fsim::frontend::SdfConstructKind kind,
    const std::string_view target, const std::size_t ordinal = 0U)
{
    std::size_t found_ordinal = 0U;
    for (const auto& node : resolution.nodes()) {
        if (node.construct_kind == kind
            && node.target_instance_path == target) {
            if (found_ordinal == ordinal)
                return node;
            ++found_ordinal;
        }
    }
    throw std::runtime_error("missing endpoint mapping");
}

void test_mixed_language_endpoints_and_links()
{
    using namespace fsim::app;
    using fsim::frontend::SdfConstructKind;
    const auto design = make_design();
    const auto before = design.state();
    const auto result = resolve_revision("4.0", design);
    require(result.ok(), "mixed-language endpoint resolution must succeed");

    const auto& iopath
        = find_mapping(*result.resolution, SdfConstructKind::Iopath, "top");
    require(iopath.endpoints.size() == 2U && iopath.specify_path == 0U,
        "IOPATH endpoints must link to the retained specify path");
    const auto same_node = result.resolution->find_node(iopath.node_id);
    require(!same_node.empty()
            && std::ranges::all_of(same_node,
                [&](const SdfResolvedNodeEndpoints& mapping) {
                    return mapping.node_id == iopath.node_id;
                }),
        "node lookup must return the complete equal-ID mapping range");
    require(result.resolution->find_node(
                                 std::numeric_limits<std::uint64_t>::max())
                .empty(),
        "node lookup must return an empty range for an unknown ID");
    const auto input = std::ranges::find_if(iopath.endpoints,
        [](const SdfResolvedEndpoint& endpoint) {
            return endpoint.role == SdfEndpointRole::Input;
        });
    require(input != iopath.endpoints.end() && input->signal == 0U
            && input->select && input->select->left == 0
            && !input->edge_identity.empty() && input->conversion
            && input->conversion_peer == 9U,
        "IOPATH input must retain select, edge, and exact conversion peer");

    const auto& conditional = find_mapping(
        *result.resolution, SdfConstructKind::Iopath, "top", 1U);
    require(conditional.specify_path == 3U
            && !conditional.condition_identity.empty()
            && std::ranges::any_of(conditional.endpoints,
                [](const SdfResolvedEndpoint& endpoint) {
                    return endpoint.role == SdfEndpointRole::Condition
                        && endpoint.object_path == "top.EN";
                }),
        "conditional IOPATH must retain and resolve its condition signal");

    const auto conditional_else = resolve_source("conditional-else.sdf",
        "(DELAYFILE (SDFVERSION \"4.0\") "
        "(CELL (CELLTYPE \"top\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (CONDELSE (IOPATH A[2] Z (3)))))))",
        design);
    require(conditional_else.ok(),
        "CONDELSE endpoint resolution must succeed");
    const auto& ifnone = find_mapping(*conditional_else.resolution,
        SdfConstructKind::Iopath, "top");
    require(ifnone.specify_path == 4U && !ifnone.condition_identity.empty(),
        "CONDELSE must bind only the exact elaborated ifnone path");

    const auto& interconnect = find_mapping(
        *result.resolution, SdfConstructKind::Interconnect, "top");
    require(interconnect.endpoints.size() == 2U
            && interconnect.endpoints[0].select
            && interconnect.endpoints[0].select->width == 1U
            && interconnect.endpoints[1].select
            && interconnect.endpoints[1].select->left == 1,
        "interconnect endpoints must retain both vector selectors");

    const auto& device
        = find_mapping(*result.resolution, SdfConstructKind::Device, "top");
    require(device.endpoints.size() == 1U
            && device.endpoints.front().role == SdfEndpointRole::Device
            && device.endpoints.front().object_path == "top.Z",
        "an empty DEVICE selector must resolve the cell's output port set");

    const auto& setup
        = find_mapping(*result.resolution, SdfConstructKind::Setup, "top");
    require(setup.timing_check == 0U
            && std::ranges::any_of(setup.endpoints,
                [](const SdfResolvedEndpoint& endpoint) {
                    return endpoint.role == SdfEndpointRole::TimingReference
                        && endpoint.signal == 3U;
                })
            && std::ranges::any_of(setup.endpoints,
                [](const SdfResolvedEndpoint& endpoint) {
                    return endpoint.role == SdfEndpointRole::TimingData
                        && endpoint.signal == 0U;
                }),
        "SETUP endpoints must link to the stable elaborated timing-check ID");

    const auto edge_setup = resolve_source("edge-setup.sdf",
        "(DELAYFILE (SDFVERSION \"4.0\") "
        "(CELL (CELLTYPE \"top\") (INSTANCE top) "
        "(TIMINGCHECK (SETUP A[0] (posedge CLK) (1)))))",
        design);
    require(edge_setup.ok(),
        "edge-qualified timing-check endpoint resolution must succeed");
    const auto& edge_setup_mapping = find_mapping(
        *edge_setup.resolution, SdfConstructKind::Setup, "top");
    require(edge_setup_mapping.timing_check == 1U
            && std::ranges::any_of(edge_setup_mapping.endpoints,
                [](const SdfResolvedEndpoint& endpoint) {
                    return endpoint.role == SdfEndpointRole::TimingReference
                        && endpoint.signal == 3U
                        && !endpoint.edge_identity.empty();
                }),
        "edge-qualified SETUP must bind and re-role the exact elaborated check");

    const auto& vhdl
        = find_mapping(*result.resolution, SdfConstructKind::Iopath, "vhdl");
    require(vhdl.endpoints.size() == 2U
            && vhdl.endpoints[0].language == SdfScopeRootLanguage::Vhdl,
        "VHDL endpoints must resolve with ASCII-insensitive ownership");
    const auto& native = find_mapping(
        *result.resolution, SdfConstructKind::Iopath, "native");
    require(native.endpoints.size() == 2U
            && std::ranges::all_of(native.endpoints,
                [](const SdfResolvedEndpoint& endpoint) {
                    return endpoint.object_kind
                        == SdfEndpointObjectKind::SystemCPort;
                }),
        "native SystemC proxy ports must retain native object identity");

    const auto after = design.state();
    require(before.verilog_specify_paths.size()
                == after.verilog_specify_paths.size()
            && before.verilog_specify_paths.front().delays
                == after.verilog_specify_paths.front().delays
            && before.verilog_timing_checks.size()
                == after.verilog_timing_checks.size()
            && before.verilog_timing_checks.front().limits
                == after.verilog_timing_checks.front().limits,
        "endpoint resolution must not apply or mutate simulator timing");
}

void test_revision_identity_determinism_and_resources()
{
    const auto design = make_design();
    const auto sdf21 = resolve_revision("OVI 2.1", design);
    const auto sdf30 = resolve_revision("OVI 3.0", design);
    const auto sdf40 = resolve_revision("4.0", design);
    const auto repeated = resolve_revision("4.0", design);
    require(sdf21.ok() && sdf30.ok() && sdf40.ok() && repeated.ok()
            && sdf21.resolution->semantic_identity()
                == sdf30.resolution->semantic_identity()
            && sdf30.resolution->semantic_identity()
                == sdf40.resolution->semantic_identity()
            && sdf40.resolution->semantic_identity()
                == repeated.resolution->semantic_identity()
            && std::ranges::equal(sdf40.resolution->nodes(),
                repeated.resolution->nodes()),
        "SDF 2.1/3.0/4.0 endpoint mappings must be deterministic and semantically equal");

    auto candidate_limits = fsim::app::SdfEndpointResolutionLimits { };
    candidate_limits.max_candidates = 1U;
    const auto candidate_bounded
        = resolve_revision("4.0", design, candidate_limits);
    require(!candidate_bounded.resolution,
        "candidate exhaustion must reject transactionally");
    require_diagnostic(
        candidate_bounded.diagnostics, "FSIM-SDF-ENDPOINT-005");

    auto endpoint_limits = fsim::app::SdfEndpointResolutionLimits { };
    endpoint_limits.max_endpoints = 1U;
    const auto endpoint_bounded
        = resolve_revision("4.0", design, endpoint_limits);
    require(!endpoint_bounded.resolution,
        "endpoint exhaustion must reject transactionally");
    require_diagnostic(endpoint_bounded.diagnostics, "FSIM-SDF-ENDPOINT-005");

    auto identity_limits = fsim::app::SdfEndpointResolutionLimits { };
    identity_limits.max_identity_bytes = 16U;
    const auto identity_bounded
        = resolve_revision("4.0", design, identity_limits);
    require(!identity_bounded.resolution,
        "identity exhaustion must reject transactionally");
    require_diagnostic(identity_bounded.diagnostics, "FSIM-SDF-ENDPOINT-005");
}

void test_missing_ambiguous_and_stale_rejection()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto parsed = fsim::frontend::parse_sdf({ "missing.sdf",
        "(DELAYFILE (SDFVERSION \"4.0\") "
        "(CELL (CELLTYPE \"top\") (INSTANCE top) "
        "(DELAY (ABSOLUTE (PORT absent (1))))))" });
    require(parsed.ok(), "missing-endpoint fixture must parse");
    SdfAnnotationScopeRequest request;
    request.selection = SdfScopeSelection::Single;
    request.root_aliases = { "top" };
    request.expected_project_identity = "project:endpoints";
    request.expected_design_identity = "design:endpoints";
    const auto scope = bind_sdf_annotation_scope(parsed.file, design,
        "project:endpoints", "design:endpoints", request);
    const auto cells = resolve_sdf_cells(scope.scope, design);
    require(scope.ok() && cells.ok(), "missing fixture must reach endpoints");
    const auto missing = resolve_sdf_endpoints(cells.resolution, design);
    require(!missing.resolution,
        "missing endpoint must not publish a partial resolution");
    const auto& diagnostic
        = require_diagnostic(missing.diagnostics, "FSIM-SDF-ENDPOINT-002");
    const auto port_node = std::ranges::find(
        parsed.file.normalized_ir->nodes(),
        fsim::frontend::SdfConstructKind::Port,
        &fsim::frontend::SdfIrNode::kind);
    require(port_node != parsed.file.normalized_ir->nodes().end(),
        "missing fixture must retain its PORT node");
    require(diagnostic.message.find("top.A") != std::string::npos
            && diagnostic.span.begin == port_node->span.begin,
        "missing endpoint diagnostics must retain candidates and source coordinates");

    const auto valid = resolve_revision("4.0", design);
    require(valid.ok(), "valid fixture must resolve before stale-state checks");

    auto path_ambiguous_state = design.state();
    auto duplicate_path = path_ambiguous_state.verilog_specify_paths.front();
    duplicate_path.id = static_cast<fsim::elaboration::VerilogSpecifyPathId>(
        path_ambiguous_state.verilog_specify_paths.size());
    duplicate_path.identity = "sdf:iopath:top:A[0]:Z:duplicate";
    path_ambiguous_state.verilog_specify_paths.push_back(
        std::move(duplicate_path));
    const auto path_ambiguous_design
        = fsim::elaboration::ElaboratedDesign::from_state(
            std::move(path_ambiguous_state));
    require(path_ambiguous_design.has_value(),
        "ambiguous specify-path fixture must remain structurally loadable");
    const auto path_ambiguous
        = resolve_revision("4.0", *path_ambiguous_design);
    require(!path_ambiguous.resolution,
        "ambiguous exact specify-path link must reject transactionally");
    require_diagnostic(
        path_ambiguous.diagnostics, "FSIM-SDF-ENDPOINT-006");

    auto ambiguous_state = design.state();
    fsim::elaboration::SystemCNamedObjectInfo duplicate;
    duplicate.kind = fsim::elaboration::SystemCNamedObjectKind::signal;
    duplicate.native_handle = 999U;
    duplicate.name = "top.A";
    duplicate.parent = "top";
    duplicate.type_name = "proxy";
    duplicate.signal = 9U;
    ambiguous_state.systemc_objects.push_back(std::move(duplicate));
    const auto ambiguous_design
        = fsim::elaboration::ElaboratedDesign::from_state(
            std::move(ambiguous_state));
    require(ambiguous_design.has_value(),
        "ambiguous endpoint fixture must remain structurally loadable");
    const auto ambiguous = resolve_sdf_endpoints(
        valid.resolution->cells(), *ambiguous_design);
    require(!ambiguous.resolution,
        "ambiguous endpoint must not publish a partial resolution");
    require_diagnostic(ambiguous.diagnostics, "FSIM-SDF-ENDPOINT-003");

    auto stale_state = design.state();
    stale_state.specializations.front().unit = "sv:work.changed";
    const auto stale_design
        = fsim::elaboration::ElaboratedDesign::from_state(std::move(stale_state));
    require(stale_design.has_value(), "stale fixture must remain valid");
    const auto stale
        = resolve_sdf_endpoints(valid.resolution->cells(), *stale_design);
    require(!stale.resolution,
        "stale cell targets must reject before endpoint publication");
    require_diagnostic(stale.diagnostics, "FSIM-SDF-ENDPOINT-001");
}
} // namespace

int main()
{
    try {
        test_mixed_language_endpoints_and_links();
        test_revision_identity_determinism_and_resources();
        test_missing_ambiguous_and_stale_rejection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
