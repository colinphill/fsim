// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {
namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string { message });
        }
    }

} // namespace

void test_systemverilog_hierarchy_declarations()
{
    using namespace fsim::frontend;
    const auto parsed = parse_text(
        "systemverilog-hierarchy.sv",
        R"(extern module leaf #(parameter int W = 8)(input logic [W-1:0] i);
module leaf #(parameter int W = 8)(input logic [W-1:0] i);
endmodule
module probe #(parameter int W = 1)();
endmodule
module top;
  leaf u();
  bind top.u probe #(.W(137)) local_probe();
  bind leaf probe type_probe();
endmodule
bind top.g[2].u probe selected_probe();
config selected;
  design work.top auxiliary;
  default liblist work fallback;
  instance top.u use mapped.leaf;
  cell work.leaf liblist mapped fallback;
  instance top.g[2].u use work.nested : config;
endconfig : selected
)",
        Language::SystemVerilog2017);
    require(parsed.ok(), "extern, bind, and config declarations must parse");
    require(
        parsed.design.units.size() == 6,
        "hierarchy declarations retain owning design units");

    const auto prototype = std::ranges::find_if(
        parsed.design.units,
        [](const DesignUnit& unit) { return unit.systemverilog_extern; });
    require(
        prototype != parsed.design.units.end()
            && prototype->kind == UnitKind::VerilogModule
            && prototype->name == "leaf"
            && prototype->parameters.size() == 1
            && prototype->ports.size() == 1,
        "extern module prototype retains its complete header");

    const auto top = std::ranges::find(
        parsed.design.units, "top", &DesignUnit::name);
    require(
        top != parsed.design.units.end()
            && top->systemverilog_binds.size() == 2
            && top->systemverilog_binds[0].target == "top.u"
            && top->systemverilog_binds[0].instances.size() == 1
            && top->systemverilog_binds[0].instances[0].unit_name == "probe"
            && top->systemverilog_binds[0]
                    .instances[0]
                    .parameter_overrides[0]
                    .value.text
                == "137"
            && top->systemverilog_binds[1].target == "leaf",
        "module-local bind targets and bound instances remain structured");

    const auto bind = std::ranges::find_if(
        parsed.design.units,
        [](const DesignUnit& unit) {
            return unit.kind == UnitKind::SystemVerilogBind;
        });
    require(
        bind != parsed.design.units.end()
            && bind->name == "$bind$0"
            && bind->systemverilog_binds.size() == 1
            && bind->systemverilog_binds[0].target == "top.g[2].u"
            && bind->systemverilog_binds[0].instances[0].name
                == "selected_probe",
        "compilation-unit bind owns a stable portable declaration");

    const auto config = std::ranges::find_if(
        parsed.design.units,
        [](const DesignUnit& unit) {
            return unit.kind == UnitKind::SystemVerilogConfiguration;
        });
    require(
        config != parsed.design.units.end()
            && config->name == "selected"
            && config->systemverilog_configuration.has_value(),
        "configuration declaration has a distinct owning unit");
    const auto& declaration = *config->systemverilog_configuration;
    require(
        declaration.designs.size() == 2
            && declaration.designs[0].library == "work"
            && declaration.designs[0].cell == "top"
            && declaration.designs[1].cell == "auxiliary"
            && declaration.default_liblist
                == std::vector<std::string> { "work", "fallback" }
            && declaration.rules.size() == 3
            && declaration.rules[0].selector == "top.u"
            && declaration.rules[0].use_library == "mapped"
            && declaration.rules[0].use_cell == "leaf"
            && declaration.rules[1].kind
                == SystemVerilogConfigurationRuleKind::Cell
            && declaration.rules[1].liblist
                == std::vector<std::string> { "mapped", "fallback" }
            && declaration.rules[2].use_configuration,
        "configuration design, default, use, liblist, and nested config rules retain source order");

    const auto invalid = parse_text(
        "invalid-systemverilog-hierarchy.sv",
        R"(config wrong;
  default liblist work;
endconfig : other
)",
        Language::SystemVerilog2017);
    require(
        !invalid.ok()
            && std::ranges::any_of(
                invalid.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-235";
                })
            && std::ranges::any_of(
                invalid.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-236";
                }),
        "configuration end names and required design statements diagnose exactly");
}

} // namespace fsim::tests::frontend
