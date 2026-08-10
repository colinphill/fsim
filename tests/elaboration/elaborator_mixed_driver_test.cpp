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
  const auto recursive = fsim::elaboration::elaborate(
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
  const auto siblings = fsim::elaboration::elaborate(
      sibling_design, "sv:work.sibling_driver_top", sibling_bindings);
  assert(!siblings.ok());
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
  const auto input_result = fsim::elaboration::elaborate(
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
  const auto wired = fsim::elaboration::elaborate(
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
  const auto std_logic = fsim::elaboration::elaborate(
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
}

}  // namespace fsim::tests::elaboration
