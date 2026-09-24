// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {
namespace {

void append_units(
    fsim::frontend::ParsedDesign& destination,
    const fsim::frontend::ParsedDesign& source) {
  destination.units.insert(
      destination.units.end(), source.units.begin(), source.units.end());
}

}  // namespace

void test_mixed_language_driver_ownership() {
  const auto recursive_top = fsim::frontend::parse_text(
      "recursive_driver_top.sv",
      R"(
module recursive_driver_top;
  logic [7:0] result;
  vhdl_driver_mid child(.result(result));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto recursive_mid = fsim::frontend::parse_text(
      "recursive_driver_mid.vhd",
      R"(
entity Recursive_Driver_Mid is
  port (Result : out bit_vector(3 downto 0));
end entity;
architecture rtl of Recursive_Driver_Mid is
begin
  Leaf : sv_driver_leaf port map (Result => Result);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto recursive_leaf = fsim::frontend::parse_text(
      "recursive_driver_leaf.sv",
      R"(
module recursive_driver_leaf(output bit [1:0] result);
  initial result = 2'b11;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(recursive_top.ok() && recursive_mid.ok() && recursive_leaf.ok());
  auto recursive_design = recursive_top.design;
  append_units(recursive_design, recursive_mid.design);
  append_units(recursive_design, recursive_leaf.design);
  const std::vector<fsim::elaboration::Binding> recursive_bindings{
      {"recursive_driver_top.child",
       "vhdl:work.recursive_driver_mid(rtl)", std::nullopt},
      {"recursive_driver_top.child.leaf", "sv:work.recursive_driver_leaf",
       std::nullopt},
  };
  const auto recursive = compile_and_elaborate(
      recursive_design, "sv:work.recursive_driver_top", recursive_bindings);
  if (!recursive.ok()) {
    for (const auto& diagnostic : recursive.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(recursive.ok());
  assert(recursive.design->boundary_conversions().size() == 2);
  assert(std::ranges::all_of(
      recursive.design->boundary_conversions(),
      [](const auto& conversion) {
        return conversion.kind
                == fsim::elaboration::BoundaryConversionKind::width_adapter
            && conversion.process;
      }));
  auto interpreter = recursive.design->create_interpreter();
  assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
  const auto result = recursive.design->find_signal("result");
  assert(result);
  assert(
      interpreter->signal_value(*result).to_msb_string()
      == "00000011");

  const auto sibling_top = fsim::frontend::parse_text(
      "sibling_driver_top.sv",
      R"(
module sibling_driver_top;
  logic [7:0] result;
  vhdl_driver_writer left(.result(result));
  vhdl_driver_writer right(.result(result));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto sibling_writer = fsim::frontend::parse_text(
      "sibling_driver_writer.vhd",
      R"(
entity Sibling_Driver_Writer is
  port (Result : out bit_vector(3 downto 0));
end entity;
architecture rtl of Sibling_Driver_Writer is
begin
  Result <= "1010";
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(sibling_top.ok() && sibling_writer.ok());
  auto sibling_design = sibling_top.design;
  append_units(sibling_design, sibling_writer.design);
  const std::vector<fsim::elaboration::Binding> sibling_bindings{
      {"sibling_driver_top.left",
       "vhdl:work.sibling_driver_writer(rtl)", std::nullopt},
      {"sibling_driver_top.right",
       "vhdl:work.sibling_driver_writer(rtl)", std::nullopt},
  };
  const auto siblings = compile_and_elaborate(
      sibling_design, "sv:work.sibling_driver_top", sibling_bindings);
  assert(!siblings.ok());
  assert(!siblings.design);
  assert(has_diagnostic(siblings, "FSIM-ELAB-BIND-024"));
  assert(!has_diagnostic(siblings, "FSIM-ELAB-DRV-001"));

  const auto input_writer = fsim::frontend::parse_text(
      "input_driver_writer.vhd",
      R"(
entity Input_Driver_Writer is
  port (Data : in bit_vector(3 downto 0));
end entity;
architecture rtl of Input_Driver_Writer is
begin
  Data <= "1111";
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto input_top = fsim::frontend::parse_text(
      "input_driver_top.sv",
      R"(
module input_driver_top;
  bit [3:0] data;
  vhdl_input_writer child(.data(data));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(input_writer.ok() && input_top.ok());
  auto input_design = input_top.design;
  append_units(input_design, input_writer.design);
  const std::vector<fsim::elaboration::Binding> input_bindings{{
      "input_driver_top.child", "vhdl:work.input_driver_writer(rtl)",
      std::nullopt}};
  const auto input_result = compile_and_elaborate(
      input_design, "sv:work.input_driver_top", input_bindings);
  assert(!input_result.ok());
  assert(has_diagnostic(input_result, "FSIM-ELAB-SVIFACE-006"));

  const auto wired_top = fsim::frontend::parse_text(
      "mixed_wired_top.sv",
      R"(
module mixed_wired_top;
  wand and_conflict;
  wand and_release;
  wand and_float;
  wor or_conflict;
  wor or_release;
  wor or_float;
  vhdl_zero_driver ac0(.result(and_conflict));
  vhdl_one_driver ac1(.result(and_conflict));
  vhdl_one_driver ar1(.result(and_release));
  vhdl_z_driver ar_z(.result(and_release));
  vhdl_z_driver af_z0(.result(and_float));
  vhdl_z_driver af_z1(.result(and_float));
  vhdl_zero_driver oc0(.result(or_conflict));
  vhdl_one_driver oc1(.result(or_conflict));
  vhdl_zero_driver or0(.result(or_release));
  vhdl_z_driver or_z(.result(or_release));
  vhdl_z_driver of_z0(.result(or_float));
  vhdl_z_driver of_z1(.result(or_float));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto wired_driver = fsim::frontend::parse_text(
      "mixed_wired_driver.vhd",
      R"(
entity Vhdl_Zero_Driver is
  port (Result : out std_logic);
end entity;
architecture rtl of Vhdl_Zero_Driver is
begin
  Result <= '0';
end architecture;
entity Vhdl_One_Driver is
  port (Result : out std_logic);
end entity;
architecture rtl of Vhdl_One_Driver is
begin
  Result <= '1';
end architecture;
entity Vhdl_Z_Driver is
  port (Result : out std_logic);
end entity;
architecture rtl of Vhdl_Z_Driver is
begin
  Result <= 'Z';
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!wired_top.ok()) {
    for (const auto& diagnostic : wired_top.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  if (!wired_driver.ok()) {
    for (const auto& diagnostic : wired_driver.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(wired_top.ok() && wired_driver.ok());
  auto wired_design = wired_top.design;
  append_units(wired_design, wired_driver.design);
  std::vector<fsim::elaboration::Binding> wired_bindings;
  for (const auto& [name, target] : {
           std::pair{"ac0", "vhdl_zero_driver"},
           std::pair{"ac1", "vhdl_one_driver"},
           std::pair{"ar1", "vhdl_one_driver"},
           std::pair{"ar_z", "vhdl_z_driver"},
           std::pair{"af_z0", "vhdl_z_driver"},
           std::pair{"af_z1", "vhdl_z_driver"},
           std::pair{"oc0", "vhdl_zero_driver"},
           std::pair{"oc1", "vhdl_one_driver"},
           std::pair{"or0", "vhdl_zero_driver"},
           std::pair{"or_z", "vhdl_z_driver"},
           std::pair{"of_z0", "vhdl_z_driver"},
           std::pair{"of_z1", "vhdl_z_driver"}}) {
    wired_bindings.push_back({
        "mixed_wired_top." + std::string{name},
        "vhdl:work." + std::string{target} + "(rtl)", std::nullopt});
  }
  const auto wired = compile_and_elaborate(
      wired_design, "sv:work.mixed_wired_top", wired_bindings);
  if (!wired.ok()) {
    for (const auto& diagnostic : wired.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(wired.ok());
  for (const auto name : {"and_conflict", "and_release", "and_float"}) {
    const auto signal = wired.design->find_signal(name);
    assert(signal);
    assert(
        wired.design->signals().at(*signal).resolution
        == fsim::runtime::simir::ResolutionKind::sv_wand);
  }
  for (const auto name : {"or_conflict", "or_release", "or_float"}) {
    const auto signal = wired.design->find_signal(name);
    assert(signal);
    assert(
        wired.design->signals().at(*signal).resolution
        == fsim::runtime::simir::ResolutionKind::sv_wor);
  }
  auto wired_interpreter = wired.design->create_interpreter();
  assert(wired_interpreter->run().status
         == fsim::runtime::RunStatus::completed);
  for (const auto& [name, value] : {
           std::pair{"and_conflict", "0"},
           std::pair{"and_release", "1"},
           std::pair{"and_float", "Z"},
           std::pair{"or_conflict", "1"},
           std::pair{"or_release", "0"},
           std::pair{"or_float", "Z"}}) {
    const auto signal = wired.design->find_signal(name);
    assert(signal);
    assert(wired_interpreter->signal_value(*signal).to_msb_string() == value);
  }

  const auto std_logic_top = fsim::frontend::parse_text(
      "mixed_std_logic_top.vhd",
      R"(
entity Mixed_Std_Logic_Top is
  port (Conflict, Released : out std_logic);
end entity;
architecture rtl of Mixed_Std_Logic_Top is
  signal Conflict_Net, Released_Net : std_logic;
begin
  Zero_Conflict : sv_logic_zero port map (Value => Conflict_Net);
  One_Conflict : sv_logic_one port map (Value => Conflict_Net);
  One_Released : sv_logic_one port map (Value => Released_Net);
  Z_Released : sv_logic_z port map (Value => Released_Net);
  Conflict <= Conflict_Net;
  Released <= Released_Net;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto logic_drivers = fsim::frontend::parse_text(
      "mixed_logic_drivers.sv",
      R"(
module sv_logic_zero(output logic value); assign value = 1'b0; endmodule
module sv_logic_one(output logic value); assign value = 1'b1; endmodule
module sv_logic_z(output logic value); assign value = 1'bz; endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(std_logic_top.ok() && logic_drivers.ok());
  auto std_logic_design = std_logic_top.design;
  append_units(std_logic_design, logic_drivers.design);
  const std::vector<fsim::elaboration::Binding> logic_bindings{
      {"mixed_std_logic_top.zero_conflict", "sv:work.sv_logic_zero",
       std::nullopt},
      {"mixed_std_logic_top.one_conflict", "sv:work.sv_logic_one",
       std::nullopt},
      {"mixed_std_logic_top.one_released", "sv:work.sv_logic_one",
       std::nullopt},
      {"mixed_std_logic_top.z_released", "sv:work.sv_logic_z",
       std::nullopt},
  };
  const auto std_logic = compile_and_elaborate(
      std_logic_design,
      "vhdl:work.mixed_std_logic_top(rtl)", logic_bindings);
  assert(std_logic.ok());
  const auto conflict_net = std_logic.design->find_signal("conflict_net");
  const auto released_net = std_logic.design->find_signal("released_net");
  assert(conflict_net && released_net);
  assert(
      std_logic.design->signals().at(*conflict_net).resolution
      == fsim::runtime::simir::ResolutionKind::std_logic);
  auto std_logic_interpreter = std_logic.design->create_interpreter();
  assert(std_logic_interpreter->run().status
         == fsim::runtime::RunStatus::completed);
  assert(std_logic_interpreter->signal_value(*conflict_net).to_msb_string()
         == "X");
  assert(std_logic_interpreter->signal_value(*released_net).to_msb_string()
         == "1");
  assert(std_logic_interpreter->signal_value(
             *std_logic.design->find_signal("conflict")).to_msb_string()
         == "X");
  assert(std_logic_interpreter->signal_value(
             *std_logic.design->find_signal("released")).to_msb_string()
         == "1");

  const auto fused_variable_array = fsim::frontend::parse_text(
      "fused_variable_array_driver.sv",
      R"(
module fused_variable_array_driver;
  logic [3:0] terms [0:2];
  genvar index;
  generate
    for (index = 0; index < 1; index = index + 1) begin : generated_terms
      assign terms[index] = 4'hA;
      assign terms[index + 1] = 4'hB;
    end
  endgenerate
  assign terms[2] = 4'h5;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(fused_variable_array.ok());
  const auto fused_variable_array_result = compile_and_elaborate(
      fused_variable_array.design,
      "sv:work.fused_variable_array_driver");
  if (!fused_variable_array_result.ok()) {
    for (const auto& diagnostic : fused_variable_array_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(fused_variable_array_result.ok());
  const auto variable_array_signal
      = fused_variable_array_result.design->find_signal("terms");
  assert(variable_array_signal);
  const auto& variable_array_info
      = fused_variable_array_result.design->signals().at(
          *variable_array_signal);
  assert(variable_array_info.systemverilog_net_type.empty());
  assert(
      variable_array_info.resolution
      == fsim::runtime::simir::ResolutionKind::none);
  std::vector<fsim::runtime::simir::Process::DriverRegion>
      variable_array_regions;
  for (const auto& process
      : fused_variable_array_result.design->processes()) {
    for (const auto& region : process.driver_regions) {
      if (region.signal != *variable_array_signal) {
        continue;
      }
      const auto leaf = std::string_view { process.name }.substr(
          process.name.find_last_of('.') + 1U);
      assert(leaf.starts_with("concurrent_"));
      variable_array_regions.push_back(region);
    }
  }
  assert(variable_array_regions.size() == 3U);
  assert(std::ranges::all_of(
      variable_array_regions,
      [&](const auto& region) {
        return !region.whole && region.width == 4U;
      }));
  const auto has_offset = [&](const std::uint32_t offset) {
    return std::ranges::any_of(
        variable_array_regions,
        [&](const auto& region) { return region.offset == offset; });
  };
  assert(has_offset(0U) && has_offset(4U) && has_offset(8U));

  const auto overlapping_variable_array = fsim::frontend::parse_text(
      "overlapping_variable_array_driver.sv",
      R"(
module overlapping_variable_array_driver;
  logic [3:0] terms [0:1];
  assign terms[0] = 4'hA;
  assign terms[0] = 4'h5;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(overlapping_variable_array.ok());
  const auto overlapping_variable_array_result = compile_and_elaborate(
      overlapping_variable_array.design,
      "sv:work.overlapping_variable_array_driver");
  assert(!overlapping_variable_array_result.ok());
  assert(has_diagnostic(
      overlapping_variable_array_result, "FSIM-ELAB-DRV-001"));

  const auto overlapping_fused_variable_array
      = fsim::frontend::parse_text(
          "overlapping_fused_variable_array_driver.sv",
          R"(
module overlapping_fused_variable_array_driver;
  logic [3:0] terms [0:1];
  genvar index;
  generate
    for (index = 0; index < 1; index = index + 1) begin : generated_terms
      assign terms[index] = 4'hA;
      assign terms[index] = 4'h5;
    end
  endgenerate
endmodule
)",
          fsim::frontend::Language::SystemVerilog2017);
  assert(overlapping_fused_variable_array.ok());
  const auto overlapping_fused_variable_array_result
      = compile_and_elaborate(
          overlapping_fused_variable_array.design,
          "sv:work.overlapping_fused_variable_array_driver");
  assert(!overlapping_fused_variable_array_result.ok());
  assert(has_diagnostic(
      overlapping_fused_variable_array_result, "FSIM-ELAB-DRV-001"));

  const auto procedural_variable_array = fsim::frontend::parse_text(
      "procedural_variable_array_driver.sv",
      R"(
module procedural_variable_array_driver;
  logic [3:0] terms [0:1];
  logic [3:0] source;
  assign terms[0] = source;
  always_comb terms[0] = source;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(procedural_variable_array.ok());
  const auto procedural_variable_array_result = compile_and_elaborate(
      procedural_variable_array.design,
      "sv:work.procedural_variable_array_driver");
  assert(!procedural_variable_array_result.ok());
  assert(has_diagnostic(
      procedural_variable_array_result, "FSIM-ELAB-DRV-001"));

  const auto procedural_dual_writer_array
      = fsim::frontend::parse_text(
          "procedural_dual_writer_array.sv",
          R"(
module procedural_dual_writer_array(
  input logic clk,
  input logic write_a,
  input logic write_b,
  input logic [1:0] address_a,
  input logic [1:0] address_b,
  input logic [7:0] data_a,
  input logic [7:0] data_b
);
  reg [7:0] mem [0:3];
  always @(posedge clk)
    if (write_a) mem[address_a] <= data_a;
  always @(posedge clk)
    if (write_b) mem[address_b] <= data_b;
endmodule
)",
          fsim::frontend::Language::SystemVerilog2017);
  assert(procedural_dual_writer_array.ok());
  const auto procedural_dual_writer_array_result = compile_and_elaborate(
      procedural_dual_writer_array.design,
      "sv:work.procedural_dual_writer_array");
  if (!procedural_dual_writer_array_result.ok()) {
    for (const auto& diagnostic :
        procedural_dual_writer_array_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(procedural_dual_writer_array_result.ok());
  const auto dual_writer_mem
      = procedural_dual_writer_array_result.design->find_signal("mem");
  assert(dual_writer_mem);
  const auto& dual_writer_mem_info
      = procedural_dual_writer_array_result.design->signals().at(
          *dual_writer_mem);
  assert(dual_writer_mem_info.systemverilog_net_type.empty());
  assert(
      dual_writer_mem_info.resolution
      == fsim::runtime::simir::ResolutionKind::none);

  const auto procedural_array_wakeup = fsim::frontend::parse_text(
      "procedural_array_wakeup.sv",
      R"(
module procedural_array_wakeup;
  reg clk = 1'b0;
  always #5 clk = ~clk;
  reg resetn = 1'b0;
  reg [3:0] parity [0:3];
  wire [3:0] top = parity[3];
  integer index;

  always @(posedge clk) begin
    if (!resetn) begin
      for (index = 0; index < 4; index = index + 1)
        parity[index] <= 4'h0;
    end else begin
      parity[3] <= 4'hA;
    end
  end

  initial begin
    #12 resetn = 1'b1;
    #20 $finish;
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(procedural_array_wakeup.ok());
  const auto procedural_array_wakeup_result = compile_and_elaborate(
      procedural_array_wakeup.design,
      "sv:work.procedural_array_wakeup");
  if (!procedural_array_wakeup_result.ok()) {
    for (const auto& diagnostic : procedural_array_wakeup_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(procedural_array_wakeup_result.ok());
  assert(!has_diagnostic(
      procedural_array_wakeup_result, "FSIM-ELAB-DRV-001"));
  const auto parity_signal
      = procedural_array_wakeup_result.design->find_signal("parity");
  assert(parity_signal);
  const auto& parity_info
      = procedural_array_wakeup_result.design->signals().at(*parity_signal);
  assert(parity_info.systemverilog_net_type.empty());
  assert(
      parity_info.resolution == fsim::runtime::simir::ResolutionKind::none);
  auto procedural_array_interpreter
      = procedural_array_wakeup_result.design->create_interpreter();
  static_cast<void>(procedural_array_interpreter->run());
  const auto top_signal
      = procedural_array_wakeup_result.design->find_signal("top");
  assert(top_signal);
  assert(
      procedural_array_interpreter->signal_value(*top_signal)
          .to_msb_string()
      == "1010");
}

}  // namespace fsim::tests::elaboration
