// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error(std::string{message});
}

}  // namespace

void test_verilog_udp_declarations() {
  using namespace fsim::frontend;
  const auto parsed = parse_text(
      "udp.v",
      R"(
primitive mux_udp (out, a, b, select);
  output out;
  input a, b, select;
  table
    0 ? 0 : 0;
    1 ? 0 : 1;
    ? 0 1 : 0;
    ? 1 1 : 1;
  endtable
endprimitive

primitive dff_udp (output reg q, input d, clock);
  initial q = 1'b0;
  table
    0 (01) : ? : 0;
    1 r    : ? : 1;
    ? n    : ? : -;
  endtable
endprimitive : dff_udp
)",
      Language::Verilog2005);
  require(parsed.ok(), "combinational and sequential UDPs must parse");
  require(
      parsed.design.units.empty()
          && parsed.design.udp_declarations.size() == 2,
      "UDPs remain distinct from top-selectable design units");

  const auto& mux = parsed.design.udp_declarations.front();
  require(
      mux.name == "mux_udp" && mux.output == "out"
          && mux.inputs
              == std::vector<std::string>({"a", "b", "select"})
          && !mux.sequential && mux.rows.size() == 4
          && mux.rows.front().inputs.size() == 3
          && mux.rows.front().output == VerilogUdpOutputSymbol::Zero
          && !mux.span.empty(),
      "classic combinational UDP header and table HIR");

  const auto& dff = parsed.design.udp_declarations.back();
  require(
      dff.name == "dff_udp" && dff.output == "q"
          && dff.inputs == std::vector<std::string>({"d", "clock"})
          && dff.sequential && dff.initial_output
          && *dff.initial_output == VerilogUdpOutputSymbol::Zero
          && dff.rows.size() == 3
          && dff.rows[0].inputs[1].edge
              == VerilogUdpEdgeSymbol::Explicit
          && dff.rows[1].inputs[1].edge
              == VerilogUdpEdgeSymbol::Rising
          && dff.rows[2].inputs[1].edge
              == VerilogUdpEdgeSymbol::Negative
          && dff.rows[2].output == VerilogUdpOutputSymbol::NoChange
          && dff.rows[0].current_state
          && *dff.rows[0].current_state
              == VerilogUdpLevelSymbol::DontCare,
      "ANSI sequential UDP state and edge HIR");

  const auto zero = VerilogUdpLevelSymbol::Zero;
  const auto one = VerilogUdpLevelSymbol::One;
  const auto unknown = VerilogUdpLevelSymbol::Unknown;
  require(
      verilog_udp_level_matches(VerilogUdpLevelSymbol::DontCare, unknown)
          && verilog_udp_level_matches(
              VerilogUdpLevelSymbol::Binary, zero)
          && verilog_udp_level_matches(
              VerilogUdpLevelSymbol::Binary, one)
          && !verilog_udp_level_matches(
              VerilogUdpLevelSymbol::Binary, unknown),
      "UDP level wildcard matching");
  VerilogUdpInputPattern edge;
  edge.edge = VerilogUdpEdgeSymbol::Positive;
  require(
      verilog_udp_input_matches(edge, zero, one)
          && verilog_udp_input_matches(edge, zero, unknown)
          && verilog_udp_input_matches(edge, unknown, one)
          && !verilog_udp_input_matches(edge, one, zero),
      "UDP positive transition matching");
  edge.edge = VerilogUdpEdgeSymbol::Negative;
  require(
      verilog_udp_input_matches(edge, one, zero)
          && verilog_udp_input_matches(edge, one, unknown)
          && verilog_udp_input_matches(edge, unknown, zero)
          && !verilog_udp_input_matches(edge, zero, one),
      "UDP negative transition matching");
  edge.edge = VerilogUdpEdgeSymbol::Any;
  require(
      verilog_udp_input_matches(edge, zero, unknown)
          && !verilog_udp_input_matches(edge, one, one),
      "UDP any-transition matching");
  require(
      verilog_udp_table_within_resource_budget(3, 4)
          && !verilog_udp_table_within_resource_budget(
              std::numeric_limits<std::size_t>::max(), 1)
          && !verilog_udp_table_within_resource_budget(
              1, std::numeric_limits<std::size_t>::max())
          && verilog_udp_declaration_well_formed(mux),
      "UDP table geometry uses an owning-storage budget, not a semantic "
      "row or terminal cap");
  auto malformed_hir = mux;
  malformed_hir.rows.front().inputs.clear();
  require(
      !verilog_udp_declaration_well_formed(malformed_hir),
      "artifact-facing UDP HIR validation rejects malformed row geometry");

  const std::array mux_previous{unknown, unknown, unknown};
  const std::array mux_current{zero, one, zero};
  const auto* mux_row = find_verilog_udp_table_row(
      mux, mux_previous, mux_current, unknown);
  require(
      mux_row == &mux.rows.front()
          && mux_row->output == VerilogUdpOutputSymbol::Zero,
      "UDP table lookup preserves first-row priority");
  const std::array dff_previous{zero, zero};
  const std::array dff_current{zero, one};
  const auto* dff_row = find_verilog_udp_table_row(
      dff, dff_previous, dff_current, unknown);
  require(
      dff_row == &dff.rows.front()
          && dff_row->output == VerilogUdpOutputSymbol::Zero,
      "sequential UDP lookup matches edge and current state");

  const auto instances = parse_text(
      "udp_instances.v",
      R"(
primitive inv_udp (q, d);
  output q; input d;
  table 0 : 1; 1 : 0; x : x; endtable
endprimitive
module udp_forms(
    input [1:0] d, output [1:0] q,
    input a, output y0, output y1, output y2);
  inv_udp (y0, a);
  inv_udp first(y1, a), second(y2, a);
  inv_udp arrayed[1:0](q, d);
  inv_udp #(1, 2, 3) delayed(y0, a);
  inv_udp #4 direct_delay(y1, a);
  inv_udp (weak1, strong0) strength_selected(y2, a);
endmodule
)",
      Language::Verilog2005);
  if (!instances.ok()) {
    for (const auto& diagnostic : instances.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  require(instances.ok(), "UDP instance forms must parse");
  require(
      instances.design.units.size() == 1
          && instances.design.units.front().instances.size() == 7,
      "anonymous, multiple, arrayed, and delayed UDP instances retained");
  const auto& forms = instances.design.units.front().instances;
  require(
      forms[0].anonymous && forms[0].udp_instance
          && forms[1].name == "first" && forms[2].name == "second"
          && forms[3].array_indices == std::vector<std::int64_t>({1, 0}),
      "UDP names, comma-separated instances, and array order");
  require(
      forms[4].udp_delay && forms[4].parameter_overrides.empty()
          && forms[4].udp_delay->magnitude == 1
          && forms[4].udp_delay->additional_values.size() == 2
          && forms[4].udp_delay->additional_values[0].magnitude == 2
          && forms[4].udp_delay->additional_values[1].magnitude == 3
          && forms[5].udp_delay && forms[5].udp_delay->magnitude == 4,
      "one/two/three-value UDP delays use the common delay HIR");
  require(
      forms[6].drive_strength
          && forms[6].drive_strength->zero == VerilogStrength::Strong
          && forms[6].drive_strength->one == VerilogStrength::Weak,
      "UDP instance drive strength retains canonical zero/one ranks");

  const auto revised = parse_verilog(
      SourceText{
          "udp-2023.sv",
          R"(
primitive revised_udp(output reg q, input d, clock);
  initial q = 1'b0;
  table
    0 (01) : ? : 0;
    1 (01) : ? : 1;
    ? ?    : ? : -;
  endtable
endprimitive : revised_udp
module revised_udp_top(input logic d, clock, output wire q);
  revised_udp state(q, d, clock);
endmodule
)"},
      StandardRevision::SystemVerilog2023);
  require(
      revised.ok() && revised.design.udp_declarations.size() == 1
          && revised.design.udp_declarations.front().standard_revision
              == StandardRevision::SystemVerilog2023
          && revised.design.udp_declarations.front().language
              == Language::SystemVerilog2017
          && revised.design.units.front().instances.front().udp_instance
          && verilog_udp_declaration_well_formed(
              revised.design.udp_declarations.front()),
      "the exact 2023 profile retains a well-formed sequential UDP and "
      "marks its instance");
  auto mismatched_profile = revised.design.udp_declarations.front();
  mismatched_profile.standard_revision = StandardRevision::Vhdl2019;
  require(
      !verilog_udp_declaration_well_formed(mismatched_profile),
      "artifact-facing UDP validation rejects cross-language revision "
      "metadata");

  const auto program_primitives = parse_verilog(
      SourceText{
          "program-primitives-2023.sv",
          R"(
primitive program_udp(output q, input d);
  table 0 : 1; 1 : 0; x : x; endtable
endprimitive
program illegal_primitives;
  wire a, b;
  and builtin_gate(a, b);
  tran builtin_switch(a, b);
  program_udp user_primitive(a, b);
endprogram
)"},
      StandardRevision::SystemVerilog2023);
  require(
      !program_primitives.ok()
          && std::ranges::count_if(
                 program_primitives.diagnostics,
                 [](const Diagnostic& diagnostic) {
                   return diagnostic.code == "FSIM-SV-SEM-390";
                 })
              == 3,
      "program blocks reject gate, switch, and user-defined primitive "
      "instances");

  const auto invalid = parse_text(
      "invalid_udp.v",
      R"(
primitive bad_order (a, out);
  output out; input a;
  table 0 : 0; endtable
endprimitive
primitive no_input (out);
  output out;
  table : 0; endtable
endprimitive
primitive missing_reg (q, d);
  output q; input d; initial q = 0;
  table 0 : ? : 0; endtable
endprimitive
primitive bad_width (out, a, b);
  output out; input a, b;
  table 0 : 0; endtable
endprimitive
primitive combinational_edge (out, a);
  output out; input a;
  table r : 0; endtable
endprimitive
primitive two_edges (q, a, b);
  output q; reg q; input a, b;
  table r f : ? : 0; endtable
endprimitive
primitive duplicate_row (out, a);
  output out; input a;
  table 0 : 0; 0 : 1; endtable
endprimitive
primitive bad_pair (q, a);
  output q; reg q; input a;
  table (0z) : ? : 0; endtable
endprimitive
primitive duplicate_row (out, a);
  output out; input a;
  table 0 : 0; endtable
endprimitive
module excessive_delay(input a, output q);
  duplicate_row #(1, 2, 3, 4) invalid(q, a);
endmodule
module excessive_array(input a, output q);
  duplicate_row enormous[2147483647:0](q, a);
endmodule
)",
      Language::Verilog2005);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok() && has_code("FSIM-SV-SEM-136")
          && has_code("FSIM-SV-SEM-138")
          && has_code("FSIM-SV-SEM-139")
          && has_code("FSIM-SV-SEM-141")
          && has_code("FSIM-SV-SEM-142")
          && has_code("FSIM-SV-SEM-143")
          && has_code("FSIM-SV-SEM-144")
          && has_code("FSIM-SV-SEM-146")
          && has_code("FSIM-SV-SEM-121")
          && has_code("FSIM-SV-PARSE-229")
          && has_code("FSIM-SV-SEM-135"),
      "UDP declaration, table, edge, and duplicate failures are targeted");
}

}  // namespace fsim::tests::frontend
