// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_composite_operation_closure() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_composite_operations.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;

entity Composite_Operations is
end entity;

architecture rtl of composite_operations is
  type Mode_T is (Idle, Ready, Busy);
  type Cell_T is record
    Flag : bit;
    Mode : Mode_T;
    Data : bit_vector(1 downto 0);
  end record;
  type Pair_T is array (0 to 1) of Cell_T;
  type Bits_Base_T is array (natural range <>) of bit;
  subtype Short_T is Bits_Base_T(0 to 1);
  subtype Long_T is Bits_Base_T(3 downto 0);
  signal Record_Result : Cell_T;
  signal Pair_Result : Pair_T;
  signal Converted_Result : Pair_T;
  signal Selected_Result : Cell_T;
  signal Legacy_Concat : std_logic_vector(3 downto 0);
  signal Record_Equal : boolean;
  signal Record_Different : boolean;
  signal Pair_Equal : boolean;
  signal Length_Different : boolean;
  signal Match_Equal : boolean;
  signal Match_Different : boolean;
  signal Case_Result : Mode_T;
begin
  exercise : process
    variable Left_Cell : Cell_T :=
      (Flag => '1', Mode => Ready, Data => "10");
    variable Right_Cell : Cell_T :=
      (Flag => '0', Mode => Busy, Data => "01");
    variable Pair_Value : Pair_T;
    variable Short_Value : Short_T := "10";
    variable Long_Value : Long_T := "1001";
    variable Logic_Value : std_logic_vector(3 downto 0) := "1LH-";
  begin
    Pair_Value := Left_Cell & Right_Cell;
    Pair_Value(1) := Right_Cell;
    Pair_Value(0).Mode := Ready;
    Record_Result <= Left_Cell when true else Right_Cell;
    Pair_Result <= Pair_Value;
    Converted_Result <= Pair_T(Pair_Value);
    Selected_Result <= Pair_Value(0);
    Legacy_Concat <= '1' & "0H" & '-';
    Record_Equal <= Left_Cell = Pair_Value(0);
    Record_Different <= Left_Cell /= Right_Cell;
    Pair_Equal <= Pair_Value = (Left_Cell & Right_Cell);
    Length_Different <= Short_Value /= Long_Value;
    Match_Equal <= Logic_Value ?= "1---";
    Match_Different <= Logic_Value ?/= "0---";
    case Right_Cell.Mode is
      when Busy => Case_Result <= Pair_Value(0).Mode;
      when others => Case_Result <= Idle;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "vhdl:work.composite_operations(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(elaborated.ok());
  auto interpreter = elaborated.design->create_interpreter();
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  const auto value = [&](const std::string_view name) {
    const auto signal = elaborated.design->find_signal(name);
    assert(signal);
    return interpreter->signal_value(*signal).to_msb_string();
  };
  assert(
      value("record_result") == "10110"
      && value("pair_result") == "1011001001"
      && value("converted_result") == "1011001001"
      && value("selected_result") == "10110"
      && value("legacy_concat") == "10H-"
      && value("record_equal") == "1"
      && value("record_different") == "1"
      && value("pair_equal") == "1"
      && value("length_different") == "1"
      && value("match_equal") == "1"
      && value("match_different") == "1"
      && value("case_result") == "01");

  const auto reject = [](
      const std::string_view filename,
      const std::string_view declarations,
      const std::string_view statement,
      const std::string_view code) {
    const auto source = std::string{
        "entity Invalid is end entity;\n"
        "architecture rtl of invalid is\n"}
        + std::string{declarations}
        + "\nbegin\n" + std::string{statement}
        + "\nend architecture;\n";
    const auto invalid = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto result = fsim::elaboration::elaborate(
        invalid.design, "vhdl:work.invalid(rtl)");
    if (result.ok() || !has_diagnostic(result, code)) {
      std::cerr << filename << " expected " << code << '\n';
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message
                  << '\n';
      }
    }
    assert(!result.ok() && has_diagnostic(result, code));
  };

  constexpr std::string_view records = R"(
  type First_T is record Flag : bit; Data : bit; end record;
  type Second_T is record Flag : bit; Data : bit; end record;
  signal First : First_T;
  signal Second : Second_T;
  signal Result : boolean;)";
  reject(
      "vhdl_record_comparison_nominal.vhd", records,
      "  Result <= First = Second;",
      "FSIM-ELAB-VHCOMPOP-001");
  reject(
      "vhdl_record_assignment_nominal.vhd", records,
      "  First <= Second;",
      "FSIM-ELAB-VHCOMPOP-002");
  reject(
      "vhdl_record_conditional_nominal.vhd", records,
      "  First <= First when true else Second;",
      "FSIM-ELAB-VHCOMPOP-002");
  reject(
      "vhdl_record_matching.vhd", records,
      "  Result <= First ?= First;",
      "FSIM-ELAB-VHDLMATCH-004");

  constexpr std::string_view arrays = R"(
  type First_T is array (0 to 1) of bit;
  type Second_T is array (0 to 1) of bit;
  signal First : First_T;
  signal Second : Second_T;
  signal Result : boolean;)";
  reject(
      "vhdl_array_comparison_nominal.vhd", arrays,
      "  Result <= First = Second;",
      "FSIM-ELAB-VHARRAY-006");
  reject(
      "vhdl_concat_length.vhd", arrays,
      "  First <= First & '1';",
      "FSIM-ELAB-VHCOMPOP-003");

  reject(
      "vhdl_array_assignment_lengths.vhd",
      R"(
  type Values_T is array (natural range <>) of bit;
  subtype Pair_T is Values_T(0 to 1);
  subtype Triple_T is Values_T(0 to 2);
  signal Pair_Value : Pair_T;
  signal Triple_Value : Triple_T;)",
      "  Pair_Value <= Triple_Value;",
      "FSIM-ELAB-VHARRAY-006");
  reject(
      "vhdl_array_record_unknown_member.vhd",
      R"(
  type Cell_T is record Flag : bit; end record;
  type Pair_T is array (0 to 1) of Cell_T;
  signal Pair_Value : Pair_T;
  signal Result : bit;)",
      "  Result <= Pair_Value(0).Missing;",
      "FSIM-ELAB-VHCOMPOP-004");
}

}  // namespace fsim::tests::elaboration
