// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_aggregate_choice_closure() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_aggregate_choice_closure.vhd",
      R"(
entity Aggregate_Choice_Closure is
end entity;

architecture rtl of aggregate_choice_closure is
  type Mode_T is (Idle, Ready, Busy);
  subtype Active_T is Mode_T range Ready to Busy;
  type Inner_T is record
    Left : bit;
    Right : bit;
    Mode : Active_T;
  end record;
  type Triple_T is record
    A : bit;
    B : bit;
    C : bit;
  end record;
  type Nibble_Base_T is array (natural range <>) of bit;
  subtype Nibble_T is Nibble_Base_T(3 downto 0);
  type Inner_Array_T is array (0 to 1) of Inner_T;
  type Nibble_Array_T is array (0 to 1) of Nibble_T;
  type Mode_Array_T is array (0 to 1) of Active_T;
  type Outer_T is record
    First : Inner_T;
    Second : Inner_T;
    Bits : Nibble_T;
    Tag : Active_T;
  end record;

  signal Choice_Record : Inner_T;
  signal Others_Record : Triple_T;
  signal Nested_Record : Outer_T;
  signal Record_Array : Inner_Array_T;
  signal Nested_Array : Nibble_Array_T;
  signal Mode_Array : Mode_Array_T;
  signal Range_Bits : Nibble_T;
  signal Packed_Bits : bit_vector(3 downto 0);
  signal Descending_Source : bit_vector(7 downto 4);
  signal Ascending_Source : bit_vector(0 to 3);
  signal Attribute_Range_Bits : bit_vector(7 downto 4);
  signal Attribute_Reverse_Bits : bit_vector(3 downto 0);
begin
  drive : process
  begin
    Choice_Record <= (Left | Right => '1', Mode => Ready);
    Others_Record <= (A | B => '1', others => '0');
    Nested_Record <=
      (First | Second =>
         Inner_T'((Left | Right => '0', Mode => Busy)),
       Bits => Nibble_T'("1010"),
       Tag => Ready);
    Record_Array <=
      (0 => Inner_T'((Left => '1', Right => '0', Mode => Ready)),
       others =>
         Inner_T'((Left | Right => '1', Mode => Ready)));
    Nested_Array <=
      (0 => Nibble_T'((3 | 1 => '1', others => '0')),
       others => Nibble_T'("0011"));
    Mode_Array <= (0 => Ready, others => Busy);
    Range_Bits <= (3 downto 2 => '1', 1 | 0 => '0');
    Packed_Bits <= (3 | 1 => '1', others => '0');
    Attribute_Range_Bits <= (Descending_Source'range => '1');
    Attribute_Reverse_Bits <= (Ascending_Source'reverse_range => '0');
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto elaborated = compile_and_elaborate(
      parsed.design, "vhdl:work.aggregate_choice_closure(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(elaborated.ok());

  const auto choice_record =
      elaborated.design->find_signal("choice_record");
  const auto others_record =
      elaborated.design->find_signal("others_record");
  const auto nested_record =
      elaborated.design->find_signal("nested_record");
  const auto record_array =
      elaborated.design->find_signal("record_array");
  const auto nested_array =
      elaborated.design->find_signal("nested_array");
  const auto mode_array = elaborated.design->find_signal("mode_array");
  const auto range_bits = elaborated.design->find_signal("range_bits");
  const auto packed_bits = elaborated.design->find_signal("packed_bits");
  const auto attribute_range_bits = elaborated.design->find_signal(
      "attribute_range_bits");
  const auto attribute_reverse_bits = elaborated.design->find_signal(
      "attribute_reverse_bits");
  assert(
      choice_record && others_record && nested_record && record_array
      && nested_array && mode_array && range_bits && packed_bits
      && attribute_range_bits && attribute_reverse_bits);

  auto interpreter = elaborated.design->create_interpreter();
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  assert(
      interpreter->signal_value(*choice_record).to_msb_string() == "1101"
      && interpreter->signal_value(*others_record).to_msb_string() == "110"
      && interpreter->signal_value(*nested_record).to_msb_string()
          == "00100010101001"
      && interpreter->signal_value(*record_array).to_msb_string()
          == "10011101"
      && interpreter->signal_value(*nested_array).to_msb_string()
          == "10100011"
      && interpreter->signal_value(*mode_array).to_msb_string() == "0110"
      && interpreter->signal_value(*range_bits).to_msb_string() == "1100"
      && interpreter->signal_value(*packed_bits).to_msb_string() == "1010"
      && interpreter->signal_value(*attribute_range_bits).to_msb_string()
          == "1111"
      && interpreter->signal_value(*attribute_reverse_bits).to_msb_string()
          == "0000");

  const auto slice_aggregate = fsim::frontend::parse_text(
      "vhdl_predefined_slice_others_aggregate.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity Predefined_Slice_Others_Aggregate is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of predefined_slice_others_aggregate is
  signal Descending : std_logic_vector(15 downto 0);
  signal Ascending : std_logic_vector(0 to 15);
begin
  Descending(7 downto 0) <=
    (others => '1') when true else (others => '0');
  Ascending(0 to 7) <=
    (others => '1') when true else (others => '0');
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(slice_aggregate.ok());
  const auto slice_elaborated = compile_and_elaborate(
      slice_aggregate.design,
      "vhdl:work.predefined_slice_others_aggregate(rtl)");
  if (!slice_elaborated.ok()) {
    for (const auto& diagnostic : slice_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(slice_elaborated.ok());
  const auto descending = slice_elaborated.design->find_signal(
      "descending");
  const auto ascending = slice_elaborated.design->find_signal("ascending");
  assert(descending && ascending);
  auto slice_interpreter = slice_elaborated.design->create_interpreter();
  const auto slice_run = slice_interpreter->run();
  assert(slice_run.status == fsim::runtime::RunStatus::completed);
  // The selected halves are driven; their untouched halves remain Z.
  assert(
      slice_interpreter->signal_value(*descending).to_msb_string()
          == "ZZZZZZZZ11111111"
      && slice_interpreter->signal_value(*ascending).to_msb_string()
          == "11111111ZZZZZZZZ");

  const auto inactive_generate = fsim::frontend::parse_text(
      "vhdl_inactive_generate_slice_validation.vhd",
      R"(
entity Inactive_Generate_Slice_Validation is end entity;
architecture rtl of inactive_generate_slice_validation is
  signal Source : bit_vector(3 downto 0);
  signal Destination : bit_vector(3 downto 0);
begin
  inactive: if false generate
    Destination <= Source(3 downto 4);
    nested: if true generate
      Destination <= Source(3 downto 4);
    end generate nested;
  end generate inactive;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(inactive_generate.ok());
  const auto inactive_generate_design = compile_and_elaborate(
      inactive_generate.design,
      "vhdl:work.inactive_generate_slice_validation(rtl)");
  if (!inactive_generate_design.ok()) {
    for (const auto& diagnostic : inactive_generate_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(inactive_generate_design.ok());

  const auto expect_active_generate_slice_rejection = [](
      const std::string_view filename,
      const std::string_view source) {
    const auto parsed = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto result = compile_and_elaborate(
        parsed.design, "vhdl:work.active_generate_slice_validation(rtl)");
    assert(
        !result.ok()
        && has_diagnostic(result, "FSIM-ELAB-VHARRAYSEL-004"));
  };
  expect_active_generate_slice_rejection(
      "vhdl_active_generate_slice_validation.vhd",
      R"(
entity Active_Generate_Slice_Validation is end entity;
architecture rtl of active_generate_slice_validation is
  signal Source : bit_vector(3 downto 0);
  signal Destination : bit_vector(3 downto 0);
begin
  active: if true generate
    Destination <= Source(3 downto 4);
  end generate active;
end architecture;
)");
  expect_active_generate_slice_rejection(
      "vhdl_false_generate_else_slice_validation.vhd",
      R"(
entity Active_Generate_Slice_Validation is end entity;
architecture rtl of active_generate_slice_validation is
  signal Source : bit_vector(3 downto 0);
  signal Destination : bit_vector(3 downto 0);
begin
  select_branch: if false generate
    Destination <= Source(3 downto 4);
  else generate
    Destination <= Source(3 downto 4);
  end generate select_branch;
end architecture;
)");

  const auto reject = [](
      const std::string_view filename,
      const std::string_view source,
      const std::string_view code) {
    const auto invalid = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto result = compile_and_elaborate(
        invalid.design, "vhdl:work.invalid(rtl)");
    if (result.ok() || !has_diagnostic(result, code)) {
      std::cerr << filename << " expected " << code << '\n';
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message
                  << " at " << diagnostic.span.begin.line << ':'
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(!result.ok() && has_diagnostic(result, code));
    const auto found = std::ranges::find_if(
        result.diagnostics,
        [&](const auto& diagnostic) { return diagnostic.code == code; });
    assert(
        found != result.diagnostics.end()
        && found->span.source_name == filename && !found->span.empty());
  };

  reject(
      "vhdl_record_choice_overlap.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Value_T is record A, B, C : bit; end record;
  signal Value : Value_T;
begin
  Value <= (A | B => '0', B | C => '1');
end architecture;
)",
      "FSIM-ELAB-VHAGG-004");
  reject(
      "vhdl_record_range_choice.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Value_T is record A, B : bit; end record;
  signal Value : Value_T;
begin
  Value <= (0 to 1 => '0');
end architecture;
)",
      "FSIM-ELAB-VHAGG-008");
  reject(
      "vhdl_record_unknown_choice.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Value_T is record A, B : bit; end record;
  signal Value : Value_T;
begin
  Value <= (A | Missing => '0', B => '1');
end architecture;
)",
      "FSIM-ELAB-VHAGG-003");
  reject(
      "vhdl_record_element_nominal.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is (First_0, First_1);
  type Second_T is (Second_0, Second_1);
  type Holder_T is record Mode : First_T; end record;
  signal Source : Second_T;
  signal Value : Holder_T;
begin
  Value <= (Mode => Source);
end architecture;
)",
      "FSIM-ELAB-VHAGG-009");
  reject(
      "vhdl_record_element_domain.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Holder_T is record Flag : bit; end record;
  signal Source : boolean;
  signal Value : Holder_T;
begin
  Value <= (Flag => Source);
end architecture;
)",
      "FSIM-ELAB-VHAGG-009");
  reject(
      "vhdl_array_choice_overlap.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  subtype Value_T is bit_vector(3 downto 0);
  signal Value : Value_T;
begin
  Value <= (3 downto 1 => '0', 1 | 0 => '1');
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-004");
  reject(
      "vhdl_array_choice_outside.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  subtype Value_T is bit_vector(3 downto 0);
  signal Value : Value_T;
begin
  Value <= (4 => '0', others => '1');
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-003");
  reject(
      "vhdl_user_array_choice_outside_non_slice.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity Invalid is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of invalid is
  type User_Vector_T is array (15 downto 0) of std_logic;
  signal Value : User_Vector_T;
begin
  Value <= (16 => '1', others => '0');
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-003");
  reject(
      "vhdl_array_attribute_choice_outside.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal Source : bit_vector(3 downto 0);
  signal Value : bit_vector(2 downto 0);
begin
  Value <= (Source'range => '0');
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-003");
  reject(
      "vhdl_array_attribute_choice_unconstrained.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Open_T is array (natural range <>) of bit;
  signal Value : bit_vector(3 downto 0);
begin
  Value <= (Open_T'range => '0');
end architecture;
)",
      "FSIM-ELAB-VHARRAYATTR-001");
  reject(
      "vhdl_array_choice_missing.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  subtype Value_T is bit_vector(3 downto 0);
  signal Value : Value_T;
begin
  Value <= (3 downto 1 => '0');
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-005");
  reject(
      "vhdl_array_element_nominal.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is record Flag : bit; end record;
  type Second_T is record Flag : bit; end record;
  type Values_T is array (0 to 1) of First_T;
  signal Source : Second_T;
  signal Value : Values_T;
begin
  Value <= (others => Source);
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009");

  const auto runtime_reject = fsim::frontend::parse_text(
      "vhdl_aggregate_subtype_constraint.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Mode_T is (Idle, Ready, Busy);
  subtype Active_T is Mode_T range Ready to Busy;
  type Values_T is array (0 to 1) of Active_T;
  signal Value : Values_T;
begin
  Value <= (others => Idle);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(runtime_reject.ok());
  const auto runtime_design = compile_and_elaborate(
      runtime_reject.design, "vhdl:work.invalid(rtl)");
  assert(runtime_design.ok());
  bool constrained = false;
  try {
    auto runtime_interpreter = runtime_design.design->create_interpreter();
    static_cast<void>(runtime_interpreter->run());
  } catch (const std::exception& error) {
    constrained = std::string_view{error.what()}.find(
        "VHDL integer subtype range check failed")
        != std::string_view::npos;
  }
  assert(constrained);
}

}  // namespace fsim::tests::elaboration
