// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string { message });
        }
    }

    bool has_code(
        const ParseResult& result,
        const std::string_view code)
    {
        return std::ranges::any_of(
            result.diagnostics,
            [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code;
            });
    }

    bool has_exact_diagnostic(
        const ParseResult& result,
        const std::string_view code,
        const std::size_t line,
        const std::size_t column)
    {
        return std::ranges::any_of(
            result.diagnostics,
            [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code
                    && diagnostic.span.begin.line == line
                    && diagnostic.span.begin.column == column;
            });
    }

} // namespace

void test_systemverilog_public_conformance_frontend()
{
    require(
        StandardRevision::Verilog1995 != StandardRevision::Verilog2001
            && StandardRevision::Verilog2001
                != StandardRevision::Verilog2001NoConfig
            && StandardRevision::SystemVerilog2005
                != StandardRevision::SystemVerilog2009
            && StandardRevision::SystemVerilog2009
                != StandardRevision::SystemVerilog2012
            && to_string(StandardRevision::Verilog2001NoConfig)
                == "verilog-2001-noconfig"
            && to_string(StandardRevision::SystemVerilog2009)
                == "systemverilog-2009"
            && revision_string(StandardRevision::SystemVerilog2012) == "2012"
            && to_string(StandardRevision::SystemVerilog2023)
                == "systemverilog-2023"
            && revision_string(StandardRevision::SystemVerilog2023) == "2023",
        "older Verilog/SystemVerilog revisions have distinct typed identities");
    // FSIM-CONFORMANCE CF-SV-PP-001 source=SRC-SV-TESTS expectation=accept
    auto preprocessed = preprocess_verilog(
        SourceText {
            "conformance-macros.sv",
            R"(`define JOIN(left,right) left``right
`define WIDTH(value=4) value
module `JOIN(macro_,unit) #(
  parameter int COUNT = `WIDTH()
) ();
endmodule : macro_unit
)" },
        Language::SystemVerilog2017);
    auto macro_result = parse_verilog(std::move(preprocessed.lexed), true);
    require(
        macro_result.ok()
            && macro_result.design.units.size() == 1
            && macro_result.design.units.front().name == "macro_unit"
            && macro_result.design.units.front().parameters.size() == 1
            && macro_result.design.units.front()
                    .parameters.front()
                    .default_value.text
                == "4",
        "CF-SV-PP-001 defaulted macro and token-paste expansion");

    // The declaration is independently authored from chapter-indexed
    // SRC-SV-TESTS and semantic boundary cases in SRC-SLANG/SRC-SURELOG.
    // FSIM-CONFORMANCE CF-SV-DECL-001 source=SRC-SV-TESTS expectation=accept
    // FSIM-CONFORMANCE CF-SV-TYPE-001 source=SRC-SLANG expectation=accept
    // FSIM-CONFORMANCE CF-SV-PARAM-001 source=SRC-SV-TESTS expectation=accept
    // FSIM-CONFORMANCE CF-SV-PKG-001 source=SRC-SURELOG expectation=accept
    // FSIM-CONFORMANCE CF-SV-IFACE-001 source=SRC-SURELOG expectation=accept
    // FSIM-CONFORMANCE CF-SV-GEN-001 source=SRC-SV-TESTS expectation=accept
    const auto parsed = parse_text(
        "public-conformance.sv",
        R"(package conformance_types;
  parameter int WIDTH = 4;
  typedef logic signed [WIDTH-1:0] word_t;
  typedef enum logic [1:0] {
    idle_state,
    active_state = 2
  } state_t;
endpackage : conformance_types

interface conformance_bus #(
  parameter int WIDTH = conformance_types::WIDTH
);
  logic [WIDTH-1:0] payload;
  modport producer(output payload),
          consumer(input payload);
endinterface : conformance_bus

macromodule conformance_leaf #(
  parameter type element_t = conformance_types::word_t,
  parameter int WIDTH = conformance_types::WIDTH
) (
  input logic enable,
  output element_t result
);
  if (WIDTH > 1) begin : selected_width
    initial result = enable;
  end else begin : scalar_width
    initial result = '0;
  end
endmodule : conformance_leaf

module \module  (output logic \wire  );
  import conformance_types::*;
  conformance_bus #(.WIDTH(WIDTH)) bus();
  conformance_leaf #(
    .element_t(word_t),
    .WIDTH(WIDTH)
  ) child(
    .enable(1'b1),
    .result(bus.payload)
  );
  assign \wire  = bus.payload[0];
endmodule : \module
)",
        Language::SystemVerilog2017);
    require(parsed.ok(), "public SystemVerilog frontend corpus must parse");
    require(
        parsed.design.units.size() == 4,
        "public corpus retains package, interface, macro module, and module");
    require(
        std::ranges::all_of(
            parsed.design.units, [](const DesignUnit& unit) {
                return unit.standard_revision
                    == StandardRevision::SystemVerilog2017;
            }),
        "direct frontend units retain the typed SystemVerilog-2017 baseline");

    const auto* package = parsed.design.find(
        UnitKind::SystemVerilogPackage, "conformance_types");
    const auto* interface = parsed.design.find(
        UnitKind::SystemVerilogInterface, "conformance_bus");
    const auto* leaf = parsed.design.find(
        UnitKind::VerilogModule, "conformance_leaf");
    const auto* escaped = parsed.design.find(
        UnitKind::VerilogModule, "\\module");
    require(
        package != nullptr
            && package->parameters.size() == 3
            && package->type_aliases.size() == 2
            && package->type_aliases[0].type.is_signed
            && package->type_aliases[1].enum_literals.size() == 2,
        "CF-SV-TYPE/PARAM/PKG typed package declarations");
    require(
        interface != nullptr
            && interface->parameters.size() == 1
            && interface->signals.size() == 1
            && interface->systemverilog_modports.size() == 2
            && interface->systemverilog_modports[0]
                    .members.front()
                    .direction
                == PortDirection::Output
            && interface->systemverilog_modports[1]
                    .members.front()
                    .direction
                == PortDirection::Input,
        "CF-SV-IFACE-001 parameterized interface and views");
    require(
        leaf != nullptr
            && leaf->parameters.size() == 2
            && leaf->parameters[0].kind == ParameterKind::Type
            && leaf->ports.size() == 2
            && leaf->generate_regions.size() == 1
            && leaf->generate_regions.front().then_scope
                == "selected_width"
            && leaf->generate_regions.front().else_scope
                == "scalar_width",
        "CF-SV-DECL/PARAM/GEN macromodule typed specialization HIR");
    require(
        escaped != nullptr
            && escaped->ports.size() == 1
            && escaped->ports.front().name == "\\wire"
            && escaped->systemverilog_imports.size() == 1
            && escaped->instances.size() == 2,
        "CF-SV-DECL-001 escaped keyword identifiers retain exact spelling");

    const auto verilog_1995 = parse_verilog(
        SourceText {
            "verilog-1995-declarations.v",
            R"(module legacy_ports(data, result);
  input data;
  output result;
  reg result;
  parameter WIDTH = 4;
  wire [WIDTH-1:0] exact_width;
endmodule
)" },
        StandardRevision::Verilog1995);
    require(
        verilog_1995.ok()
            && verilog_1995.design.units.size() == 1
            && verilog_1995.design.units.front().ports.size() == 2
            && verilog_1995.design.units.front().parameters.size() == 1
            && verilog_1995.design.units.front().standard_revision
                == StandardRevision::Verilog1995,
        "Verilog-1995 retains non-ANSI ports and exact packed widths");

    const auto verilog_2001 = parse_verilog(
        SourceText {
            "verilog-2001-declarations.v",
            R"(module ansi_ports #(
  parameter WIDTH = 4
) (
  input wire signed [WIDTH-1:0] data,
  output reg signed [WIDTH-1:0] result
);
  localparam EXTRA = 1;
  reg signed [WIDTH-1:0] initialized = 4'h3;
endmodule
)" },
        StandardRevision::Verilog2001);
    require(
        verilog_2001.ok()
            && verilog_2001.design.units.front().ports.size() == 2
            && verilog_2001.design.units.front().parameters.size() == 2
            && verilog_2001.design.units.front().processes.size() == 1
            && verilog_2001.design.units.front().processes.front().name
                == "$declaration_initializer_initialized",
        "Verilog-2001 admits parameter ports, ANSI signed ports, localparams, and declaration initialization");

    const auto verilog_2001_noconfig = parse_verilog(
        SourceText {
            "verilog-2001-noconfig-declarations.v",
            "module noconfig_ports(input wire value); endmodule\n" },
        StandardRevision::Verilog2001NoConfig);
    require(
        verilog_2001_noconfig.ok()
            && verilog_2001_noconfig.design.units.front().standard_revision
                == StandardRevision::Verilog2001NoConfig,
        "Verilog-2001-noconfig retains ordinary Verilog-2001 declarations");

    const auto ansi_in_1995 = parse_verilog(
        SourceText {
            "ansi-in-1995.v",
            "module ansi_in_1995(input wire value); endmodule\n" },
        StandardRevision::Verilog1995);
    const auto parameter_port_in_1995 = parse_verilog(
        SourceText {
            "parameter-port-in-1995.v",
            "module parameter_port_in_1995 #(parameter P = 1) (); endmodule\n" },
        StandardRevision::Verilog1995);
    const auto signed_in_1995 = parse_verilog(
        SourceText {
            "signed-in-1995.v",
            "module signed_in_1995; wire signed [3:0] value; endmodule\n" },
        StandardRevision::Verilog1995);
    require(
        !ansi_in_1995.ok() && !parameter_port_in_1995.ok()
            && !signed_in_1995.ok()
            && has_exact_diagnostic(
                ansi_in_1995, "FSIM-SV-PARSE-346", 1U, 21U)
            && has_code(parameter_port_in_1995, "FSIM-SV-PARSE-346")
            && has_code(signed_in_1995, "FSIM-SV-PARSE-346"),
        "Verilog-1995 rejects later ANSI, parameter-port, and signed declaration forms exactly");

    const auto uwire_in_2001 = parse_verilog(
        SourceText {
            "uwire-in-2001.v",
            "module uwire_in_2001; uwire value; endmodule\n" },
        StandardRevision::Verilog2001);
    const auto uwire_in_2005 = parse_verilog(
        SourceText {
            "uwire-in-2005.v",
            "module uwire_in_2005; uwire value; endmodule\n" },
        StandardRevision::Verilog2005);
    require(
        !uwire_in_2001.ok()
            && has_exact_diagnostic(
                uwire_in_2001, "FSIM-SV-PARSE-346", 1U, 23U)
            && uwire_in_2005.ok(),
        "uwire is introduced only by Verilog-2005");

    const auto systemverilog_2005 = parse_verilog(
        SourceText {
            "systemverilog-2005-declarations.sv",
            R"(interface bus_if;
  logic [7:0] data;
endinterface
module typed_ports #(
  parameter type element_t = logic [7:0]
) (
  bus_if bus,
  input logic [7:0] samples [2],
  output element_t result
);
  int initialized = 3;
  string label = "mode";
endmodule
module automatic lifetime_unit;
  automatic int value = 1;
endmodule
program static program_unit;
endprogram
)" },
        StandardRevision::SystemVerilog2005);
    const auto systemverilog_type_in_verilog = parse_verilog(
        SourceText {
            "systemverilog-type-in-verilog.v",
            "module leaked_type; logic value; endmodule\n" },
        StandardRevision::Verilog2005);
    const auto lifetime_in_verilog = parse_verilog(
        SourceText {
            "lifetime-in-verilog.v",
            "module automatic leaked_lifetime; endmodule\n" },
        StandardRevision::Verilog2005);
    const auto program_in_verilog = parse_verilog(
        SourceText {
            "program-in-verilog.v",
            "program leaked_program; endprogram\n" },
        StandardRevision::Verilog2005);
    const auto* typed_ports = systemverilog_2005.design.find(
        UnitKind::VerilogModule, "typed_ports");
    require(
        systemverilog_2005.ok()
            && systemverilog_2005.design.units.size() == 4
            && typed_ports != nullptr && typed_ports->ports.size() == 3
            && typed_ports->parameters.front().kind == ParameterKind::Type
            && typed_ports->processes.size() == 1
            && !systemverilog_type_in_verilog.ok()
            && has_code(
                systemverilog_type_in_verilog, "FSIM-SV-PARSE-346")
            && !lifetime_in_verilog.ok()
            && has_code(lifetime_in_verilog, "FSIM-SV-PARSE-346")
            && !program_in_verilog.ok()
            && has_code(program_in_verilog, "FSIM-SV-PARSE-348"),
        "SystemVerilog-2005 introduces typed/interface/unpacked ports, type parameters, variables, initializers, lifetimes, and program headers");

    const auto checker_in_2005 = parse_verilog(
        SourceText {
            "checker-in-2005.sv",
            "module checker_in_2005; checker bounded; endchecker endmodule\n" },
        StandardRevision::SystemVerilog2005);
    const auto checker_in_2009 = parse_verilog(
        SourceText {
            "checker-in-2009.sv",
            "module checker_in_2009; checker bounded; endchecker endmodule\n" },
        StandardRevision::SystemVerilog2009);
    require(
        !checker_in_2005.ok()
            && has_code(checker_in_2005, "FSIM-SV-PARSE-348")
            && checker_in_2009.ok(),
        "checker declaration headers are introduced only by SystemVerilog-2009");

    const auto nettype_in_2009 = parse_verilog(
        SourceText {
            "nettype-in-2009.sv",
            "module nettype_in_2009; nettype logic word_net; endmodule\n" },
        StandardRevision::SystemVerilog2009);
    const auto nettype_in_2012 = parse_verilog(
        SourceText {
            "nettype-in-2012.sv",
            R"(module nettype_in_2012;
  nettype logic word_net;
  word_net value;
endmodule
)" },
        StandardRevision::SystemVerilog2012);
    require(
        !nettype_in_2009.ok()
            && has_exact_diagnostic(
                nettype_in_2009, "FSIM-SV-PARSE-346", 1U, 25U)
            && nettype_in_2012.ok(),
        "SystemVerilog-2012 introduces nettype declarations without leaking into 2009");

    const auto expressions_1995 = parse_verilog(
        SourceText {
            "expressions-1995.v",
            R"(module expressions_1995;
  reg [256:0] left;
  reg [256:0] right;
  reg [256:0] result;
  initial begin
    result = left + right;
    if ((result === 257'bx) ? 1'b0 : 1'b1)
      result = result << 1;
  end
endmodule
)" },
        StandardRevision::Verilog1995);
    require(
        expressions_1995.ok()
            && expressions_1995.design.units.front().processes.size() == 1,
        "Verilog-1995 preserves arbitrary-width four-state expression and process forms");

    const auto expressions_2001 = parse_verilog(
        SourceText {
            "expressions-2001.v",
            R"(module expressions_2001(input [31:0] data, output reg [3:0] result);
  reg signed [256:0] wide;
  always @* begin
    wide = (data ** 2) >>> 1;
    result = wide[7 +: 4];
  end
endmodule
)" },
        StandardRevision::Verilog2001);
    require(
        expressions_2001.ok()
            && expressions_2001.design.units.front().processes.front().sensitivities.front().signal
                == "*",
        "Verilog-2001 introduces power, arithmetic shifts, indexed selects, and implicit event expressions");

    const auto power_in_1995 = parse_verilog(
        SourceText {
            "power-in-1995.v",
            "module power_in_1995; reg [7:0] q; initial q = 2 ** 3; endmodule\n" },
        StandardRevision::Verilog1995);
    const auto indexed_select_in_1995 = parse_verilog(
        SourceText {
            "indexed-select-in-1995.v",
            "module indexed_select_in_1995; reg [7:0] a; reg [3:0] q; initial q = a[1 +: 4]; endmodule\n" },
        StandardRevision::Verilog1995);
    const auto event_star_in_1995 = parse_verilog(
        SourceText {
            "event-star-in-1995.v",
            "module event_star_in_1995; reg a; always @* a = ~a; endmodule\n" },
        StandardRevision::Verilog1995);
    require(
        !power_in_1995.ok() && !indexed_select_in_1995.ok()
            && !event_star_in_1995.ok()
            && has_code(power_in_1995, "FSIM-SV-PARSE-347")
            && has_code(indexed_select_in_1995, "FSIM-SV-PARSE-347")
            && has_code(event_star_in_1995, "FSIM-SV-PARSE-347"),
        "Verilog-1995 rejects Verilog-2001 expression and event forms at their owning tokens");

    const auto processes_2005 = parse_verilog(
        SourceText {
            "processes-2005.sv",
            R"(module processes_2005(input logic [7:0] data, output logic [7:0] result);
  always_comb result = data;
  initial begin
    int value;
    value = int'(data);
    value += 1;
    value++;
    if (value inside {[0:255]}) result = value;
    unique case (value)
      0: result = 1;
      default: result = value;
    endcase
    fork result = value; join_any
  end
  final result = result;
endmodule
)" },
        StandardRevision::SystemVerilog2005);
    require(
        processes_2005.ok()
            && processes_2005.design.units.front().processes.size() == 3,
        "SystemVerilog-2005 introduces casts, inside, compound updates, qualified cases, join_any, always_comb, and final");

    const auto compound_in_verilog = parse_verilog(
        SourceText {
            "compound-in-verilog.v",
            "module compound_in_verilog; integer value; initial value += 1; endmodule\n" },
        StandardRevision::Verilog2005);
    const auto pattern_in_verilog = parse_verilog(
        SourceText {
            "pattern-in-verilog.v",
            "module pattern_in_verilog; reg [3:0] value; initial value = '{1, 0, 1, 0}; endmodule\n" },
        StandardRevision::Verilog2005);
    require(
        !compound_in_verilog.ok() && !pattern_in_verilog.ok()
            && has_code(compound_in_verilog, "FSIM-SV-PARSE-347")
            && has_code(pattern_in_verilog, "FSIM-SV-PARSE-347"),
        "Verilog-2005 rejects SystemVerilog assignment and pattern forms exactly");

    const auto unique0_in_2005 = parse_verilog(
        SourceText {
            "unique0-in-2005.sv",
            "module unique0_in_2005; int v; initial unique0 case (v) 0: v = 1; endcase endmodule\n" },
        StandardRevision::SystemVerilog2005);
    const auto unique0_in_2009 = parse_verilog(
        SourceText {
            "unique0-in-2009.sv",
            "module unique0_in_2009; int v; initial unique0 case (v) 0: v = 1; endcase endmodule\n" },
        StandardRevision::SystemVerilog2009);
    const auto exact_2012 = parse_verilog(
        SourceText {
            "exact-2012.sv",
            "module exact_2012; logic [300:0] v; initial v = (301'bx === v) ? v : ~v; endmodule\n" },
        StandardRevision::SystemVerilog2012);
    require(
        !unique0_in_2005.ok()
            && has_code(unique0_in_2005, "FSIM-SV-PARSE-347")
            && unique0_in_2009.ok() && exact_2012.ok(),
        "SystemVerilog-2009 introduces unique0 while 2012 preserves exact arbitrary-width four-state behavior");

    const auto hierarchy_2001 = parse_verilog(
        SourceText {
            "hierarchy-2001.v",
            R"(module hierarchy_leaf; endmodule
module hierarchy_2001;
  parameter ENABLE = 1;
  generate
    if (ENABLE) begin : enabled
      hierarchy_leaf item();
    end
  endgenerate
endmodule
)" },
        StandardRevision::Verilog2001);
    const auto generate_in_1995 = parse_verilog(
        SourceText {
            "generate-in-1995.v",
            "module generate_in_1995; generate endgenerate endmodule\n" },
        StandardRevision::Verilog1995);
    require(
        hierarchy_2001.ok()
            && hierarchy_2001.design.units.back().generate_regions.size() == 1
            && !generate_in_1995.ok()
            && has_code(generate_in_1995, "FSIM-SV-PARSE-348"),
        "Verilog-2001 introduces generate hierarchy without leaking into Verilog-1995");

    const auto configuration_source = SourceText {
        "configuration-2001.v",
        R"(module configured_leaf; endmodule
config configured;
  design work.configured_leaf;
endconfig
)"
    };
    const auto configuration_2001 = parse_verilog(
        configuration_source, StandardRevision::Verilog2001);
    const auto configuration_2001_noconfig = parse_verilog(
        configuration_source, StandardRevision::Verilog2001NoConfig);
    const auto configuration_2005 = parse_verilog(
        configuration_source, StandardRevision::Verilog2005);
    require(
        configuration_2001.ok() && configuration_2005.ok()
            && configuration_2001.design.units.back().standard_revision
                == StandardRevision::Verilog2001
            && !configuration_2001_noconfig.ok()
            && has_exact_diagnostic(
                configuration_2001_noconfig,
                "FSIM-SV-PARSE-348", 2U, 1U),
        "verilog-2001-noconfig alone disables configuration declarations while ordinary 2001 and 2005 retain them");

    const auto structure_2005 = parse_verilog(
        SourceText {
            "structure-2005.sv",
            R"(package structural_pkg;
  parameter int ENABLE = 1;
endpackage
interface structural_if;
  logic value;
  modport sample(input value);
endinterface
module observed; endmodule
module checker_unit; endmodule
module structural_top;
  import structural_pkg::*;
  logic active_signal;
  generate
    if (ENABLE) begin : selected
      observed item();
    end
  endgenerate
  class transaction;
    rand int count;
    constraint positive { count > 0; }
  endclass
  property active; active_signal; endproperty
  assert property (active);
  initial assert (active_signal);
  covergroup samples; coverpoint active_signal; endgroup
  bind observed checker_unit checker_instance();
endmodule
)" },
        StandardRevision::SystemVerilog2005);
    const auto package_in_verilog = parse_verilog(
        SourceText {
            "package-in-verilog.v",
            "package leaked_package; endpackage\n" },
        StandardRevision::Verilog2005);
    const auto assertion_in_verilog = parse_verilog(
        SourceText {
            "assertion-in-verilog.v",
            "module assertion_in_verilog; reg value; initial assert (value); endmodule\n" },
        StandardRevision::Verilog2005);
    require(
        structure_2005.ok()
            && structure_2005.design.units.front().standard_revision
                == StandardRevision::SystemVerilog2005
            && structure_2005.design.units[1].standard_revision
                == StandardRevision::SystemVerilog2005
            && !package_in_verilog.ok()
            && has_code(package_in_verilog, "FSIM-SV-PARSE-348")
            && !assertion_in_verilog.ok()
            && has_code(assertion_in_verilog, "FSIM-SV-PARSE-348"),
        "SystemVerilog-2005 introduces packages/imports, interfaces/modports, classes/constraints, assertions/coverage, and bind");

    const auto let_in_2005 = parse_verilog(
        SourceText {
            "let-in-2005.sv",
            "module let_in_2005; let identity(value) = value; endmodule\n" },
        StandardRevision::SystemVerilog2005);
    const auto let_in_2009 = parse_verilog(
        SourceText {
            "let-in-2009.sv",
            "module let_in_2009; let identity(value) = value; endmodule\n" },
        StandardRevision::SystemVerilog2009);
    const auto package_let_in_2005 = parse_verilog(
        SourceText {
            "package-let-in-2005.sv",
            "package package_let_in_2005; let identity(value) = value; endpackage\n" },
        StandardRevision::SystemVerilog2005);
    require(
        !let_in_2005.ok()
            && has_exact_diagnostic(
                let_in_2005, "FSIM-SV-PARSE-348", 1U, 21U)
            && let_in_2009.ok() && !package_let_in_2005.ok()
            && has_code(package_let_in_2005, "FSIM-SV-PARSE-348"),
        "SystemVerilog-2009 introduces let declarations without leaking into 2005");

    const auto interface_class_in_2009 = parse_verilog(
        SourceText {
            "interface-class-in-2009.sv",
            "interface class Contract; endclass\n" },
        StandardRevision::SystemVerilog2009);
    const auto interface_class_in_2012 = parse_verilog(
        SourceText {
            "interface-class-in-2012.sv",
            R"(interface class Contract; endclass
interface class Implementation implements Contract; endclass
package contract_package;
  interface class PackageContract; endclass
endpackage
module contract_owner;
  interface class ModuleContract; endclass
endmodule
)" },
        StandardRevision::SystemVerilog2012);
    const auto package_interface_class_in_2009 = parse_verilog(
        SourceText {
            "package-interface-class-in-2009.sv",
            "package earlier_contracts; interface class Contract; endclass endpackage\n" },
        StandardRevision::SystemVerilog2009);
    require(
        !interface_class_in_2009.ok()
            && has_code(interface_class_in_2009, "FSIM-SV-PARSE-348")
            && interface_class_in_2012.ok()
            && !package_interface_class_in_2009.ok()
            && has_code(
                package_interface_class_in_2009, "FSIM-SV-PARSE-348"),
        "SystemVerilog-2012 introduces interface classes and implements clauses");

    const auto predefined_1995 = parse_verilog(
        SourceText {
            "predefined-1995.v",
            R"(module predefined_1995;
  integer integer_value;
  time time_value;
  real real_value;
  realtime realtime_value;
  reg [7:0] vector_value;
endmodule
)" },
        StandardRevision::Verilog1995);
    require(
        predefined_1995.ok()
            && predefined_1995.design.units.front().signals.size() == 5,
        "Verilog-1995 retains its predefined integer, time, real, realtime, reg, and vector profiles");
    const auto& predefined_1995_unit = predefined_1995.design.units.front();
    const auto legacy_signal = [&](const std::string_view name) {
        return std::ranges::find(
            predefined_1995_unit.signals, name,
            &SignalDeclaration::name);
    };
    const auto legacy_integer = legacy_signal("integer_value");
    const auto legacy_time = legacy_signal("time_value");
    const auto legacy_real = legacy_signal("real_value");
    const auto legacy_realtime = legacy_signal("realtime_value");
    const auto legacy_vector = legacy_signal("vector_value");
    require(
        legacy_integer != predefined_1995_unit.signals.end()
            && legacy_integer->type.width() == 32
            && legacy_integer->type.domain == ValueDomain::Logic4
            && legacy_time != predefined_1995_unit.signals.end()
            && legacy_time->type.width() == 64
            && legacy_time->type.systemverilog_scalar
                == SystemVerilogScalarKind::Time
            && legacy_real != predefined_1995_unit.signals.end()
            && legacy_real->type.systemverilog_scalar
                == SystemVerilogScalarKind::Real
            && legacy_realtime != predefined_1995_unit.signals.end()
            && legacy_realtime->type.systemverilog_scalar
                == SystemVerilogScalarKind::Realtime
            && legacy_vector != predefined_1995_unit.signals.end()
            && legacy_vector->type.width() == 8,
        "Verilog-1995 predefined scalar and vector widths/domains remain exact");

    const auto predefined_2005 = parse_verilog(
        SourceText {
            "predefined-2005.sv",
            R"(module predefined_2005;
  bit two_state;
  byte byte_value;
  shortint short_value;
  int int_value;
  longint long_value;
  shortreal short_real_value;
  logic four_state;
  chandle handle;
  process process_value;
  string text;
  mailbox box;
  semaphore lock;
  int queue[$];
  int result;
  initial begin
    queue.push_back(1);
    result = text.len() + queue.size();
    process_value = process::self();
    handle = null;
    std::randomize(result);
    result = result + $root.predefined_2005.four_state;
  end
endmodule
)" },
        StandardRevision::SystemVerilog2005);
    const auto predefined_in_verilog = parse_verilog(
        SourceText {
            "predefined-in-verilog.v",
            R"(module predefined_in_verilog;
  mailbox box;
  reg result;
  initial begin
    result = null == null;
    std::randomize(result);
    result = $root.predefined_in_verilog.result;
  end
endmodule
)" },
        StandardRevision::Verilog2005);
    const auto& predefined_unit = predefined_2005.design.units.front();
    const auto predefined_signal = [&](const std::string_view name) {
        return std::ranges::find(
            predefined_unit.signals, name,
            &SignalDeclaration::name);
    };
    const auto bit_type = predefined_signal("two_state");
    const auto byte_type = predefined_signal("byte_value");
    const auto short_type = predefined_signal("short_value");
    const auto int_type = predefined_signal("int_value");
    const auto long_type = predefined_signal("long_value");
    const auto shortreal_type = predefined_signal("short_real_value");
    const auto logic_type = predefined_signal("four_state");
    const auto chandle_type = predefined_signal("handle");
    const auto process_type = predefined_signal("process_value");
    const auto string_type = std::ranges::find(
        predefined_unit.variables, std::string { "text" },
        &VariableDeclaration::name);
    const auto mailbox_type = std::ranges::find(
        predefined_unit.signals, std::string { "box" },
        &SignalDeclaration::name);
    const auto queue_type = std::ranges::find(
        predefined_unit.variables, std::string { "queue" },
        &VariableDeclaration::name);
    require(
        predefined_2005.ok(),
        "SystemVerilog-2005 predefined environment parses");
    require(
        bit_type != predefined_unit.signals.end()
            && bit_type->type.width() == 1
            && bit_type->type.domain == ValueDomain::Bit2
            && byte_type != predefined_unit.signals.end()
            && byte_type->type.width() == 8
            && short_type != predefined_unit.signals.end()
            && short_type->type.width() == 16
            && int_type != predefined_unit.signals.end()
            && int_type->type.width() == 32
            && long_type != predefined_unit.signals.end()
            && long_type->type.width() == 64,
        "SystemVerilog-2005 predefined integral widths remain exact");
    require(
        shortreal_type != predefined_unit.signals.end()
            && shortreal_type->type.systemverilog_scalar
                == SystemVerilogScalarKind::ShortReal
            && logic_type != predefined_unit.signals.end()
            && logic_type->type.domain == ValueDomain::Logic4,
        "SystemVerilog-2005 real and four-state scalar profiles remain exact");
    require(
        chandle_type != predefined_unit.signals.end()
            && chandle_type->type.systemverilog_scalar
                == SystemVerilogScalarKind::Chandle
            && process_type != predefined_unit.signals.end()
            && process_type->type.systemverilog_scalar
                == SystemVerilogScalarKind::Chandle,
        "SystemVerilog-2005 chandle and process profiles remain exact");
    require(
        string_type != predefined_unit.variables.end()
            && string_type->type.domain == ValueDomain::String,
        "SystemVerilog-2005 predefined two-state, four-state, integer, scalar, process, and string profiles remain exact");
    require(
        mailbox_type != predefined_unit.signals.end(),
        "SystemVerilog-2005 mailbox declaration is retained");
    require(
        mailbox_type->type.systemverilog_scalar
                == SystemVerilogScalarKind::Chandle
            && mailbox_type->type.domain == ValueDomain::Bit2,
        "SystemVerilog-2005 mailbox retains its predefined handle profile");
    require(
        queue_type != predefined_unit.variables.end(),
        "SystemVerilog-2005 queue declaration is retained");
    require(
        queue_type->type.systemverilog_container.has_value(),
        "SystemVerilog-2005 synchronization and container types retain their predefined profiles");
    require(
        !predefined_in_verilog.ok()
            && has_code(predefined_in_verilog, "FSIM-SV-PARSE-349"),
        "SystemVerilog-2005 introduces predefined scalar, string, synchronization, process, root, null, randomization, and container-method profiles without Verilog leakage");

    const auto identifier_property_2005 = parse_verilog(
        SourceText {
            "identifier-property-2005.sv",
            "module identifier_property_2005; logic clock; logic eventually; property p; @(posedge clock) eventually; endproperty endmodule\n" },
        StandardRevision::SystemVerilog2005);
    const auto recurrence_in_2005 = parse_verilog(
        SourceText {
            "recurrence-in-2005.sv",
            "module recurrence_in_2005; logic clock; logic value; property p; @(posedge clock) eventually value; endproperty endmodule\n" },
        StandardRevision::SystemVerilog2005);
    const auto recurrence_in_2009 = parse_verilog(
        SourceText {
            "recurrence-in-2009.sv",
            "module recurrence_in_2009; logic clock; logic value; property p; @(posedge clock) eventually value; endproperty endmodule\n" },
        StandardRevision::SystemVerilog2009);
    require(
        identifier_property_2005.ok(),
        "SystemVerilog-2005 same-spelled property identifiers remain identifiers");
    require(
        !recurrence_in_2005.ok()
            && has_code(recurrence_in_2005, "FSIM-SV-PARSE-349"),
        "SystemVerilog-2005 rejects the later recurrence operator profile");
    require(
        recurrence_in_2009.ok(),
        "SystemVerilog-2009 property operators do not consume same-spelled 2005 identifiers");

    const auto soft_identifier_2009 = parse_verilog(
        SourceText {
            "soft-identifier-2009.sv",
            "class SoftIdentifier; rand int soft; constraint c { soft; } endclass\n" },
        StandardRevision::SystemVerilog2009);
    const auto soft_constraint_2009 = parse_verilog(
        SourceText {
            "soft-constraint-2009.sv",
            "class SoftConstraint; rand int value; constraint c { soft value > 0; } endclass\n" },
        StandardRevision::SystemVerilog2009);
    const auto soft_constraint_2012 = parse_verilog(
        SourceText {
            "soft-constraint-2012.sv",
            "class SoftConstraint; rand int value; constraint c { soft value > 0; } endclass\n" },
        StandardRevision::SystemVerilog2012);
    require(
        soft_identifier_2009.ok() && !soft_constraint_2009.ok()
            && has_code(soft_constraint_2009, "FSIM-SV-PARSE-349")
            && soft_constraint_2012.ok(),
        "SystemVerilog-2012 soft constraints do not consume same-spelled 2009 identifiers");

    // FSIM-CONFORMANCE CF-SV-PP-N01 source=SRC-SV-TESTS expectation=reject
    const auto bad_macro = preprocess_verilog(
        SourceText {
            "bad-conformance-macro.sv",
            R"(`define SELECT(first,second) first
`SELECT(only_one)
)" },
        Language::SystemVerilog2017);
    require(
        !bad_macro.ok()
            && std::ranges::any_of(
                bad_macro.lexed.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PP-030";
                }),
        "CF-SV-PP-N01 wrong macro arity is rejected exactly");

    // FSIM-CONFORMANCE CF-SV-DECL-N01 source=SRC-SV-TESTS expectation=reject
    const auto mismatched_module = parse_text(
        "mismatched-module-label.sv",
        "module opening; endmodule : closing\n",
        Language::SystemVerilog2017);
    require(
        !mismatched_module.ok()
            && has_code(mismatched_module, "FSIM-SV-SEM-129"),
        "CF-SV-DECL-N01 mismatched module end name is rejected");

    // FSIM-CONFORMANCE CF-SV-IFACE-N01 source=SRC-SLANG expectation=reject
    const auto mismatched_interface = parse_text(
        "mismatched-interface-label.sv",
        "interface opening; endinterface : closing\n",
        Language::SystemVerilog2017);
    require(
        !mismatched_interface.ok()
            && has_code(mismatched_interface, "FSIM-SV-SEM-129"),
        "CF-SV-IFACE-N01 mismatched interface end name is rejected");

    // FSIM-CONFORMANCE CF-SV-DECL-N02 source=SRC-SV-TESTS expectation=reject
    const auto reserved_identifier = parse_text(
        "reserved-module-name.sv",
        "module module; endmodule\n",
        Language::SystemVerilog2017);
    const auto malformed_nettype_2012 = parse_verilog(
        SourceText {
            "malformed-nettype-2012.sv",
            "module malformed_nettype_2012; nettype logic; endmodule\n" },
        StandardRevision::SystemVerilog2012);
    require(
        !malformed_nettype_2012.ok()
            && has_exact_diagnostic(
                malformed_nettype_2012,
                "FSIM-SV-PARSE-001", 1U, 45U),
        "SystemVerilog-2012 malformed nettype diagnostics retain exact coordinates");
    require(
        !reserved_identifier.ok()
            && has_code(reserved_identifier, "FSIM-SV-PARSE-001"),
        "CF-SV-DECL-N02 unescaped reserved identifier is rejected");
}

void test_systemverilog_2023_profile_isolation()
{
    const auto require_isolated = [](
                                      const std::string_view name,
                                      const std::string_view source,
                                      const std::string_view diagnostic) {
        const auto retained = parse_verilog(
            SourceText {
                std::string { name } + "-2017.sv",
                std::string { source } },
            StandardRevision::SystemVerilog2017);
        const auto revised = parse_verilog(
            SourceText {
                std::string { name } + "-2023.sv",
                std::string { source } },
            StandardRevision::SystemVerilog2023);
        require(
            revised.ok() && !retained.ok()
                && has_code(retained, diagnostic),
            std::string { name }
                + " must remain isolated to the exact 2023 profile");
    };

    require_isolated(
        "multiple-interface-class-inheritance",
        R"(interface class LeftContract; endclass
interface class RightContract; endclass
interface class CombinedContract extends LeftContract, RightContract;
endclass
)",
        "FSIM-SV-PARSE-369");
    require_isolated(
        "streaming-assignment-target",
        R"(module streaming_assignment_target;
  logic [7:0] left;
  logic [7:0] right;
  initial {<<8{left, right}} = 16'h1234;
endmodule
)",
        "FSIM-SV-PARSE-370");
    require_isolated(
        "pattern-assignment-target",
        R"(module pattern_assignment_target;
  logic [7:0] left;
  logic [7:0] right;
  initial '{left, right} = 16'h1234;
endmodule
)",
        "FSIM-SV-PARSE-370");
    require_isolated(
        "inside-tolerance-range",
        R"(module inside_tolerance_range;
  logic selected;
  initial selected = 107 inside {[100 +/- 7]};
endmodule
)",
        "FSIM-SV-PARSE-347");
    require_isolated(
        "static-reference-formal",
        R"(module static_reference_formal;
  function automatic int observe(ref static int value);
    return value;
  endfunction
endmodule
)",
        "FSIM-SV-PARSE-368");
    require_isolated(
        "function-background-process",
        R"(module function_background_process;
  function automatic bit launch();
    fork
      begin #1; end
    join_none
    return 1'b1;
  endfunction
endmodule
)",
        "FSIM-SV-SEM-246");
    require_isolated(
        "anonymous-program-interface-class",
        R"(program;
  interface class ProgramContract;
  endclass
endprogram
)",
        "FSIM-SV-PARSE-381");
    require_isolated(
        "program-elaboration-severity",
        R"(program severity_program;
  $info("direct");
  if (1) $warning("selected");
endprogram
)",
        "FSIM-SV-PARSE-381");

    const auto program_revisions = parse_verilog(
        SourceText {
            "program-revisions-2023.sv",
            R"(program;
  interface class ProgramContract;
  endclass
endprogram
program severity_program;
  $info("direct");
  if (1) $warning("selected");
endprogram
)" },
        StandardRevision::SystemVerilog2023);
    const auto program_contract = std::ranges::find(
        program_revisions.design.systemverilog_classes,
        std::string { "ProgramContract" },
        &SystemVerilogClassDeclaration::name);
    const auto severity_program = std::ranges::find(
        program_revisions.design.units,
        std::string { "severity_program" },
        &DesignUnit::name);
    require(
        program_revisions.ok()
            && program_contract
                != program_revisions.design.systemverilog_classes.end()
            && program_contract->is_interface
            && program_contract->canonical_identity
                == "$unit::ProgramContract",
        "a 2023 anonymous program must retain interface-class items in the "
        "compilation-unit namespace");
    require(
        severity_program != program_revisions.design.units.end()
            && severity_program->kind == UnitKind::SystemVerilogProgram
            && severity_program->concurrent_statements.size() == 1
            && severity_program->concurrent_statements.front().kind
                == StatementKind::Report
            && severity_program->generate_regions.size() == 1
            && severity_program->generate_regions.front()
                   .then_body.concurrent_statements.size() == 1
            && severity_program->generate_regions.front()
                   .then_body.concurrent_statements.front().kind
                == StatementKind::Report,
        "2023 program elaboration severity tasks must remain source-owned "
        "through direct and conditional generate forms");

    const auto retained_anonymous_program = parse_verilog(
        SourceText {
            "retained-anonymous-program.sv",
            R"(program;
  function int retained_value();
    return 1;
  endfunction
  class RetainedClass;
  endclass
endprogram
)" },
        StandardRevision::SystemVerilog2017);
    require(
        retained_anonymous_program.ok()
            && retained_anonymous_program.design.functions.size() == 1
            && retained_anonymous_program.design.functions.front().name
                == "retained_value"
            && retained_anonymous_program.design.systemverilog_classes.size()
                == 1
            && retained_anonymous_program.design.systemverilog_classes.front()
                   .canonical_identity == "$unit::RetainedClass",
        "retained anonymous-program callable and class items must preserve "
        "their compilation-unit ownership");

    const auto retained_baseline = parse_verilog(
        SourceText {
            "retained-2017-baseline.sv",
            R"(interface class RetainedContract; endclass
module retained_2017_baseline;
  logic [7:0] left;
  logic [7:0] right;
  initial {left, right} = 16'h1234;
endmodule
)" },
        StandardRevision::SystemVerilog2017);
    require(
        retained_baseline.ok(),
        "2023 profile gates must not narrow established 2017 forms");
}

void test_systemverilog_checker_revisions()
{
    const auto parsed = parse_verilog(
        SourceText {
            "checker-revisions.sv",
            R"(checker value_checker(
    input logic checker_clock,
    input logic observed,
    input logic expected = observed
  );
    property is_expected;
      @(posedge checker_clock) observed == expected;
    endproperty
    okay: assert property (is_expected);
  endchecker : value_checker
module checker_revisions(
  input logic clock,
  input logic named_value,
  input logic ordered_value
);
  logic checker_clock;
  logic observed;
  value_checker named_instance(
    .checker_clock(clock),
    .observed(named_value)
  );
  value_checker ordered_instance(clock, ordered_value, 1'b0);
  value_checker wildcard_instance(.*, .expected());
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    std::string diagnostic_text;
    for (const auto& diagnostic : parsed.diagnostics) {
        diagnostic_text += " " + diagnostic.code + ":" + diagnostic.message;
    }
    const auto* unit = parsed.design.find(
        UnitKind::VerilogModule, "checker_revisions");
    require(
        parsed.ok() && unit != nullptr
            && unit->systemverilog_checker_instances.size() == 3U
            && unit->systemverilog_checker_instances[0].name
                == "named_instance"
            && unit->systemverilog_checker_instances[0].connections.size()
                == 2U
            && unit->systemverilog_checker_instances[0].connections[0]
                   .formal_name
                == "checker_clock"
            && unit->systemverilog_checker_instances[1].name
                == "ordered_instance"
            && unit->systemverilog_checker_instances[1].connections.size()
                == 3U
            && unit->systemverilog_checker_instances[2].name
                == "wildcard_instance"
            && unit->systemverilog_checker_instances[2].connections.size()
                == 2U
            && unit->systemverilog_checker_instances[2].connections[0]
                   .formal_name
                == "*"
            && unit->systemverilog_checker_instances[2].connections[1].open
            && unit->systemverilog_assertion_declarations.size() == 4U
            && unit->systemverilog_assertion_declarations[0].kind
                == SystemVerilogAssertionDeclarationKind::Checker
            && unit->systemverilog_assertion_declarations[0]
                   .checker_declarations.size()
                == 1U
            && unit->systemverilog_assertion_declarations[0]
                   .checker_assertions.size()
                == 1U
            && unit->systemverilog_assertion_declarations[1].name
                == "named_instance$is_expected"
            && unit->systemverilog_assertion_declarations[2].name
                == "ordered_instance$is_expected"
            && unit->systemverilog_assertion_declarations[3].name
                == "wildcard_instance$is_expected"
            && unit->systemverilog_concurrent_assertions.size() == 3U
            && unit->systemverilog_concurrent_assertions[0].label
                == "named_instance.okay"
            && unit->systemverilog_concurrent_assertions[1].label
                == "ordered_instance.okay"
            && unit->systemverilog_concurrent_assertions[2].label
                == "wildcard_instance.okay"
            && unit->processes.size() == 3U
            && std::ranges::all_of(
                unit->processes,
                [](const Process& process) {
                    return process.systemverilog_concurrent_assertion;
                }),
        "checker declarations, instances, connections, specializations, and executable assertions are retained"
            + diagnostic_text);

    const auto invalid = parse_verilog(
        SourceText {
            "invalid-checker-connections.sv",
            R"(checker required(input logic checker_clock, input logic observed);
    property p; @(posedge checker_clock) observed; endproperty
    assert property (p);
  endchecker
module invalid_checker_connections(
  input logic clock,
  input logic value
);
  required mixed(clock, .observed(value));
  required unknown(.checker_clock(clock), .missing(value));
  required duplicate(.checker_clock(clock), .checker_clock(clock),
                     .observed(value));
  required missing(.checker_clock(clock));
  required excessive(clock, value, value);
  required repeated(clock, value), repeated(clock, value);
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        !invalid.ok()
            && has_code(invalid, "FSIM-SV-SEM-252")
            && has_code(invalid, "FSIM-SV-SEM-253")
            && has_code(invalid, "FSIM-SV-SEM-254")
            && has_code(invalid, "FSIM-SV-SEM-255")
            && has_code(invalid, "FSIM-SV-SEM-256"),
        "checker connection form, names, cardinality, required formals, and instance identities are diagnosed");
}

void test_verilog_systemverilog_compatibility_defaults()
{
    using namespace fsim::frontend;

    const auto keyword_default = parse_verilog(
        SourceText { "keyword-default.sv", "module checker; endmodule\n" },
        StandardRevision::SystemVerilog2009);
    const auto keyword_compatibility = parse_verilog(
        SourceText { "keyword-compatibility.sv",
            "module checker; endmodule\n" },
        StandardRevision::SystemVerilog2009,
        "keyword-profile");
    require(
        !keyword_default.ok()
            && has_exact_diagnostic(
                keyword_default, "FSIM-SV-PARSE-001", 1U, 8U)
            && keyword_compatibility.ok()
            && keyword_compatibility.design.units.front()
                    .verilog_compatibility_profile
                == "keyword-profile",
        "keyword-profile selects the legacy SystemVerilog-2005 keyword set only");

    const auto configuration_source = SourceText {
        "configuration-compatibility.v",
        R"(module configured_leaf; endmodule
config configured;
  design work.configured_leaf;
endconfig
)"
    };
    const auto configuration_default = parse_verilog(
        configuration_source, StandardRevision::Verilog2001NoConfig);
    const auto configuration_compatibility = parse_verilog(
        configuration_source, StandardRevision::Verilog2001NoConfig,
        "configuration");
    require(
        !configuration_default.ok()
            && has_exact_diagnostic(
                configuration_default, "FSIM-SV-PARSE-348", 2U, 1U)
            && configuration_compatibility.ok()
            && configuration_compatibility.design.units.back()
                    .verilog_compatibility_profile
                == "configuration",
        "configuration explicitly restores the optional 2001 configuration profile");

    constexpr std::string_view all_switches
        = "keyword-profile,implicit-net,port-connection,sizing,lifetime,"
          "scheduler-assertion,configuration";
    const auto composed = parse_verilog(
        SourceText { "compatibility-composed.sv",
            "module compatibility_composed; logic [256:0] value; endmodule\n" },
        StandardRevision::SystemVerilog2005,
        all_switches);
    const auto later_grammar = parse_verilog(
        SourceText { "compatibility-no-leak.sv",
            "module compatibility_no_leak; nettype logic word_net; endmodule\n" },
        StandardRevision::SystemVerilog2005,
        all_switches);
    require(
        composed.ok()
            && composed.design.units.front().verilog_compatibility_profile
                == all_switches
            && composed.design.units.front().signals.front().type.width()
                == std::optional<std::uint64_t> { 257 }
            && !later_grammar.ok()
            && has_exact_diagnostic(
                later_grammar, "FSIM-SV-PARSE-346", 1U, 31U),
        "all compatibility families compose without widening later grammar or packed widths");

    constexpr std::string_view compatibility_switch_corpus[] = {
        "keyword-profile",
        "implicit-net",
        "port-connection",
        "sizing",
        "lifetime",
        "scheduler-assertion",
        "configuration",
    };
    for (const auto compatibility_switch : compatibility_switch_corpus) {
        const auto positive = parse_verilog(
            SourceText {
                "compatibility-switch-positive.sv",
                "module compatibility_positive; logic [256:0] value; endmodule\n" },
            StandardRevision::SystemVerilog2005,
            compatibility_switch);
        const auto negative = parse_verilog(
            SourceText {
                "compatibility-no-leak.sv",
                "module compatibility_no_leak; nettype logic word_net; endmodule\n" },
            StandardRevision::SystemVerilog2005,
            compatibility_switch);
        require(
            positive.ok()
                && positive.design.units.front().verilog_compatibility_profile
                    == compatibility_switch
                && positive.design.units.front().signals.front().type.width()
                    == std::optional<std::uint64_t> { 257 }
                && !negative.ok()
                && has_exact_diagnostic(
                    negative, "FSIM-SV-PARSE-346", 1U, 31U),
            "each compatibility switch is explicit, width-preserving, and does not widen grammar");
    }
}

void test_msvc_debug_frontend_portability()
{
    const std::string bom { "\xef\xbb\xbf" };
    const std::string windows_sv_path {
        R"(C:\work tree\utf8-source\portable.sv)"
    };
    const auto sv_source = bom
        + "module portable(output logic [3:0] value);\r\n"
          "  assign value = 4'h9;\r\n"
          "endmodule : portable\r\n";
    const auto lexed = lex(
        SourceText { windows_sv_path, sv_source },
        Language::SystemVerilog2017);
    require(
        lexed.ok() && !lexed.tokens.empty()
            && lexed.tokens.front().text == "module"
            && lexed.tokens.front().span.begin.offset == 3U
            && lexed.tokens.front().span.begin.line == 1U
            && lexed.tokens.front().span.begin.column == 1U
            && lexed.tokens.front().span.source_name == windows_sv_path
            && lexed.tokens.front().span.physical_source_name == windows_sv_path,
        "UTF-8 BOM is transparent while retaining byte offsets and Windows spans");

    const auto parsed_sv = parse(
        SourceText { windows_sv_path, sv_source },
        Language::SystemVerilog2017);
    require(
        parsed_sv.ok() && parsed_sv.design.units.size() == 1U
            && parsed_sv.design.units.front().name == "portable",
        "BOM and CRLF SystemVerilog input parses identically");

    auto preprocessed = preprocess_verilog(
        SourceText {
            "portable-preprocessed.sv",
            bom
                + "`define PORTABLE_VALUE 4'h9\r\n"
                  "module portable_preprocessed(output logic [3:0] value);\r\n"
                  "  assign value = `PORTABLE_VALUE;\r\n"
                  "endmodule\r\n" },
        Language::SystemVerilog2017);
    auto parsed_preprocessed = parse_verilog(std::move(preprocessed.lexed), true);
    require(
        parsed_preprocessed.ok()
            && parsed_preprocessed.design.units.size() == 1U,
        "BOM and CRLF remain transparent through preprocessing");

    const auto mismatch = parse(
        SourceText {
            windows_sv_path,
            bom
                + "module opening;\r\n"
                  "endmodule : closing\r\n" },
        Language::SystemVerilog2017);
    const auto mismatch_diagnostic = std::ranges::find_if(
        mismatch.diagnostics,
        [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-129";
        });
    require(
        !mismatch.ok() && mismatch_diagnostic != mismatch.diagnostics.end()
            && mismatch_diagnostic->span.source_name == windows_sv_path
            && mismatch_diagnostic->span.physical_source_name == windows_sv_path
            && mismatch_diagnostic->span.begin.line == 2U,
        "CRLF diagnostics retain exact Windows logical/physical path and line");

    const std::string windows_vhdl_path {
        R"(C:\work tree\utf8-source\portable.vhd)"
    };
    const auto parsed_vhdl = parse(
        SourceText {
            windows_vhdl_path,
            bom
                + "entity portable_vhdl is\r\n"
                  "end entity portable_vhdl;\r\n"
                  "architecture rtl of portable_vhdl is\r\n"
                  "begin\r\n"
                  "end architecture rtl;\r\n" },
        Language::Vhdl2008);
    require(
        parsed_vhdl.ok() && parsed_vhdl.design.units.size() == 2U,
        "BOM and CRLF VHDL input parses identically");
}

} // namespace fsim::tests::frontend
