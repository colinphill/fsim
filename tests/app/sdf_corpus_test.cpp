// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_mapping_validation.hpp"

#include "fsim/support/path.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
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

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "SDF corpus input must open");
    std::string result { std::istreambuf_iterator<char> { input }, { } };
    require(!input.bad(), "SDF corpus input must read completely");
    return result;
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

    VerilogSpecifyPathInfo path;
    path.id = 0U;
    path.identity = "sdf:iopath:top:A[0]:Z";
    path.instance = "top";
    path.sources.push_back({ 0U, 0U, 1U });
    path.destinations.push_back({ 1U, 0U, 1U });
    path.delays.push_back(1U);
    state.verilog_specify_paths.push_back(std::move(path));
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
    state.verilog_timing_checks.push_back(std::move(check));

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
    require(design.has_value(), "SDF corpus design must be valid");
    return std::move(*design);
}

struct RevisionCorpus {
    std::string_view directory;
    fsim::frontend::SdfRevision revision;
};

void verify_profile(const std::filesystem::path& root,
    const RevisionCorpus& corpus)
{
    const auto path = root / corpus.directory / "profile.sdf";
    const auto text = read_file(path);
    const auto parsed = fsim::frontend::parse_sdf(
        { fsim::support::path_to_utf8(path), text });
    require(parsed.ok() && parsed.file.revision == corpus.revision,
        "revision profile corpus must parse through its exact adapter");
    require(parsed.file.headers.size() >= 10U && parsed.file.cells.size() == 3U
            && parsed.file.normalized_ir
            && !parsed.file.normalized_ir->cells().empty()
            && !parsed.file.normalized_ir->semantic_identity().empty(),
        "revision profile corpus must retain headers, cells and normalized identity");
    require(std::ranges::any_of(parsed.file.cells,
                [](const auto& cell) {
                    return cell.instance_kind
                        == fsim::frontend::SdfInstanceSelectorKind::Wildcard;
                })
            && std::ranges::any_of(parsed.file.cells,
                [](const auto& cell) {
                    return cell.instance_kind
                        == fsim::frontend::SdfInstanceSelectorKind::Empty;
                }),
        "revision profile corpus must retain wildcard and empty selectors");
}

void verify_mixed_resolution(const std::filesystem::path& root,
    const RevisionCorpus& corpus,
    const fsim::elaboration::ElaboratedDesign& design)
{
    using namespace fsim::app;
    const auto path = root / corpus.directory / "mixed_resolution.sdf";
    const auto text = read_file(path);
    const auto parsed = fsim::frontend::parse_sdf(
        { fsim::support::path_to_utf8(path), text });
    require(parsed.ok() && parsed.file.revision == corpus.revision,
        "mixed-resolution corpus must parse through its exact adapter");
    SdfAnnotationScopeRequest request;
    request.selection = SdfScopeSelection::All;
    request.expected_project_identity = "project:sdf-corpus";
    request.expected_design_identity = "design:sdf-corpus";
    const auto scope = bind_sdf_annotation_scope(parsed.file, design,
        "project:sdf-corpus", "design:sdf-corpus", request);
    require(scope.ok(), "mixed-resolution corpus scope must bind");
    const auto cells = resolve_sdf_cells(scope.scope, design);
    require(cells.ok() && cells.resolution->cells().size() == 3U,
        "exact and wildcard corpus cells must resolve across three languages");
    const auto endpoints = resolve_sdf_endpoints(cells.resolution, design);
    if (!endpoints.ok()) {
        for (const auto& diagnostic : endpoints.diagnostics)
            std::cerr << corpus.directory << ' ' << diagnostic.code << ": "
                      << diagnostic.message << '\n';
    }
    require(endpoints.ok() && !endpoints.resolution->nodes().empty(),
        "mixed-language corpus endpoints must resolve");
    const auto mapping = validate_sdf_mapping(endpoints.resolution, design);
    require(mapping.ok() && mapping.summary->targets().size() == 3U
            && mapping.summary->annotation_count() != 0U
            && !mapping.summary->semantic_identity().empty(),
        "mixed-language corpus mapping summary must publish deterministically");
}

} // namespace

int main()
{
    try {
        const auto root = std::filesystem::path { FSIM_TEST_SOURCE_DIR }
            / "tests/fixtures/sdf/corpus";
        const std::array corpora {
            RevisionCorpus { "sdf21", fsim::frontend::SdfRevision::Sdf21 },
            RevisionCorpus { "sdf30", fsim::frontend::SdfRevision::Sdf30 },
            RevisionCorpus { "sdf40", fsim::frontend::SdfRevision::Sdf40 },
        };
        const auto design = make_design();
        for (const auto& corpus : corpora) {
            verify_profile(root, corpus);
            verify_mixed_resolution(root, corpus, design);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "FSIM-SDF-CORPUS-PASS revisions=3 files=6 "
                 "hierarchy=verilog-vhdl-systemc selectors=exact-wildcard\n";
    return 0;
}
