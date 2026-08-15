// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <array>
#include <string>

namespace fsim::tests::elaboration {
namespace {

using fsim::elaboration::Binding;
using fsim::elaboration::SystemCInstanceDescription;

fsim::elaboration::ElaborationResult elaborate_with_search(
    const fsim::frontend::ParsedDesign& design,
    const std::string_view top,
    const std::span<const std::string> search_libraries,
    const std::span<const Binding> bindings = {},
    const std::span<const SystemCInstanceDescription> systemc_instances = {},
    fsim::elaboration::SystemCFactoryProvider* provider = nullptr) {
    return fsim::elaboration::elaborate(
        design,
        top,
        bindings,
        systemc_instances,
        provider,
        search_libraries);
}

void assign_library(
    fsim::frontend::ParsedDesign& design,
    const std::string_view library) {
    for (auto& unit : design.units) {
        unit.library = library;
    }
}

} // namespace

void test_multi_library_resolution() {
    auto parent = fsim::frontend::parse_text(
        "search-parent.sv",
        "module search_parent; searched child(); endmodule\n",
        fsim::frontend::Language::SystemVerilog2017);
    auto vendor_child = fsim::frontend::parse_text(
        "vendor-child.sv",
        "module searched; endmodule\n",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parent.ok() && vendor_child.ok());
    assign_library(vendor_child.design, "vendor");
    parent.design.units.insert(
        parent.design.units.end(),
        vendor_child.design.units.begin(),
        vendor_child.design.units.end());

    const std::array<std::string, 2> duplicate_search{
        "vendor", "vendor"};
    const auto selected = elaborate_with_search(
        parent.design, "sv:work.search_parent", duplicate_search);
    assert(selected.ok());
    assert(selected.design->specializations().size() == 2);
    assert(
        selected.design->specializations()[1].unit
        == "sv:vendor.searched");

    const auto searched_top = elaborate_with_search(
        parent.design, "searched", duplicate_search);
    assert(searched_top.ok());
    assert(searched_top.design->top() == "searched");
    assert(
        searched_top.design->specializations().front().unit
        == "sv:vendor.searched");

    auto shared_child = fsim::frontend::parse_text(
        "shared-child.sv",
        "module searched; endmodule\n",
        fsim::frontend::Language::SystemVerilog2017);
    assert(shared_child.ok());
    assign_library(shared_child.design, "shared");
    auto ambiguous_design = parent.design;
    ambiguous_design.units.insert(
        ambiguous_design.units.end(),
        shared_child.design.units.begin(),
        shared_child.design.units.end());
    const std::array<std::string, 2> complete_scope{
        "vendor", "shared"};
    const auto ambiguous_child = elaborate_with_search(
        ambiguous_design, "sv:work.search_parent", complete_scope);
    assert(!ambiguous_child.ok());
    assert(has_diagnostic(
        ambiguous_child, "FSIM-ELAB-BIND-017"));
    const auto ambiguous_top = elaborate_with_search(
        ambiguous_design, "searched", complete_scope);
    assert(!ambiguous_top.ok());
    assert(has_diagnostic(ambiguous_top, "FSIM-ELAB-005"));

    const std::array<std::string, 1> unavailable_search{"absent"};
    const auto unavailable = elaborate_with_search(
        parent.design, "sv:work.search_parent", unavailable_search);
    assert(!unavailable.ok());
    assert(has_diagnostic(
        unavailable, "FSIM-ELAB-BIND-059"));

    auto leaf = fsim::frontend::parse_text(
        "leaf.sv",
        "module leaf; endmodule\n",
        fsim::frontend::Language::SystemVerilog2017);
    assert(leaf.ok());
    const auto unused_unavailable = elaborate_with_search(
        leaf.design, "sv:work.leaf", unavailable_search);
    assert(unused_unavailable.ok());

    const std::array explicit_binding{
        Binding{
            "search_parent.child",
            std::string{"sv:vendor.searched"},
            std::nullopt}};
    const auto explicit_override = elaborate_with_search(
        parent.design,
        "sv:work.search_parent",
        unavailable_search,
        explicit_binding);
    assert(explicit_override.ok());

    auto missing_parent = fsim::frontend::parse_text(
        "missing-parent.sv",
        "module missing_parent; unknown child(); endmodule\n",
        fsim::frontend::Language::SystemVerilog2017);
    assert(missing_parent.ok());
    missing_parent.design.units.insert(
        missing_parent.design.units.end(),
        vendor_child.design.units.begin(),
        vendor_child.design.units.end());
    const std::array<std::string, 1> vendor_search{"vendor"};
    const auto missing = elaborate_with_search(
        missing_parent.design,
        "sv:work.missing_parent",
        vendor_search);
    assert(!missing.ok());
    assert(has_diagnostic(missing, "FSIM-ELAB-BIND-012"));
    assert(
        missing.diagnostics.front().message.find("work, vendor")
        != std::string::npos);

    auto vhdl_parent = fsim::frontend::parse_text(
        "vhdl-search-parent.vhd",
        R"(
entity vhdl_search_parent is end entity;
architecture rtl of vhdl_search_parent is
  component vhdl_searched is
    port (value : in std_logic);
  end component;
  signal value : std_logic;
begin
  child: vhdl_searched port map (value => value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    auto vendor_vhdl_child = fsim::frontend::parse_text(
        "vendor-vhdl-child.sv",
        "module vhdl_searched(input logic value); endmodule\n",
        fsim::frontend::Language::SystemVerilog2017);
    assert(vhdl_parent.ok() && vendor_vhdl_child.ok());
    assign_library(vendor_vhdl_child.design, "vendor");
    vhdl_parent.design.units.insert(
        vhdl_parent.design.units.end(),
        vendor_vhdl_child.design.units.begin(),
        vendor_vhdl_child.design.units.end());
    const auto vhdl_to_sv = elaborate_with_search(
        vhdl_parent.design,
        "vhdl:work.vhdl_search_parent(rtl)",
        vendor_search);
    if (!vhdl_to_sv.ok()) {
        for (const auto& diagnostic : vhdl_to_sv.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_to_sv.ok());
    assert(
        vhdl_to_sv.design->specializations().back().unit
        == "sv:vendor.vhdl_searched");

    TestSystemCFactoryProvider provider;
    provider.available_libraries.push_back("empty_models");
    const std::array<std::string, 1> empty_library_search{
        "empty_models"};
    const auto available_without_factories = elaborate_with_search(
        missing_parent.design,
        "sv:work.missing_parent",
        empty_library_search,
        {},
        {},
        &provider);
    assert(!available_without_factories.ok());
    assert(has_diagnostic(
        available_without_factories, "FSIM-ELAB-BIND-012"));
    assert(!has_diagnostic(
        available_without_factories, "FSIM-ELAB-BIND-059"));
    provider.factory_candidates.push_back({
        "vendor", "factory_leaf", "systemc:vendor.factory_leaf"});
    auto factory_parent = fsim::frontend::parse_text(
        "factory-parent.sv",
        "module factory_parent; factory_leaf child(); endmodule\n",
        fsim::frontend::Language::SystemVerilog2017);
    assert(factory_parent.ok());
    const auto hdl_to_systemc = elaborate_with_search(
        factory_parent.design,
        "sv:work.factory_parent",
        vendor_search,
        {},
        {},
        &provider);
    assert(hdl_to_systemc.ok());
    assert(hdl_to_systemc.design->systemc_instances().size() == 1);

    provider.factory_candidates.push_back({
        "vendor", "factory_top", "systemc:vendor.factory_top"});
    const auto inferred_systemc_top = elaborate_with_search(
        factory_parent.design,
        "factory_top",
        vendor_search,
        {},
        {},
        &provider);
    assert(inferred_systemc_top.ok());
    assert(inferred_systemc_top.design->top() == "factory_top");
    provider.construction_failure = "injected root construction failure";
    const auto rejected_systemc_top = elaborate_with_search(
        factory_parent.design,
        "factory_top",
        vendor_search,
        {},
        {},
        &provider);
    assert(!rejected_systemc_top.ok());
    assert(has_diagnostic(rejected_systemc_top, "FSIM-ELAB-007"));
    provider.construction_failure.clear();

}

} // namespace fsim::tests::elaboration
