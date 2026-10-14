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
  // The selected halves are driven; the undriven subelements keep their
  // declared std_logic initial U rather than acquiring a phantom Z driver.
  assert(
      slice_interpreter->signal_value(*descending).to_msb_string()
          == "UUUUUUUU11111111"
      && slice_interpreter->signal_value(*ascending).to_msb_string()
          == "11111111UUUUUUUU");

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
      const std::string_view code,
      const fsim::frontend::VhdlStandard standard
          = fsim::frontend::VhdlStandard::Vhdl2008) {
    const auto invalid = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008, standard);
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

  reject(
      "vhdl_mixed_aggregate_target_nominal_array.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Other_Bit_Vector_T is array (natural range <>) of bit;
  signal Source : bit_vector(2 downto 0);
  signal Prefix : Other_Bit_Vector_T(1 downto 0);
  signal Tail : bit;
begin
  assign_values : process
  begin
    (Prefix, Tail) <= Source;
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-HIR-001");
  reject(
      "vhdl_mixed_aggregate_target_character_split.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal Source : string(1 to 2);
  signal A, B, C, D : bit_vector(3 downto 0);
begin
  assign_values : process
  begin
    (A, B, C, D) <= Source;
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-HIR-001");
  reject(
      "vhdl_array_segment_nominal_mismatch.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Values_T is array (natural range <>) of integer;
  type Other_Values_T is array (natural range <>) of integer;
  signal Prefix : Other_Values_T(1 to 2);
  signal Value : Values_T(1 to 3);
begin
  Value <= (Prefix, 7);
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009");
  reject(
      "vhdl_qualified_array_segment_nominal_mismatch.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Source_Vector is array (integer range <>) of integer;
  type Target_Vector is array (integer range <>) of integer;
  signal Value : Target_Vector(1 to 4);
begin
  assign_values : process
  begin
    Value <= (Source_Vector'(1, 2), Source_Vector'(3, 4));
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-005");
  reject(
      "vhdl_array_segment_pre2008.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Values_T is array (natural range <>) of integer;
  signal Prefix : Values_T(1 to 2);
  signal Value : Values_T(1 to 3);
begin
  Value <= (Prefix, 7);
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009",
      fsim::frontend::VhdlStandard::Vhdl1993);

  reject(
      "vhdl_array_value_choice_direction.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal Source : integer_vector(4 downto 1);
  signal Value : integer_vector(1 to 4);
begin
  Value <= (4 downto 1 => Source);
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-010");
  reject(
      "vhdl_bit_vector_literal_choice_direction.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal Value : bit_vector(1 to 2);
begin
  Value <= (2 downto 1 => "01");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-010");
  reject(
      "vhdl_user_character_array_literal_choice_direction.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Character_Vector_T is array (natural range <>) of character;
  signal Value : Character_Vector_T(1 to 2);
begin
  Value <= (2 downto 1 => "AB");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-010");
  reject(
      "vhdl_shadow_character_enum_literal_choice_direction.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type character is ('0', '1');
  type Character_Vector_T is array (natural range <>) of character;
  signal Value : Character_Vector_T(1 to 2);
begin
  Value <= (2 downto 1 => "10");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-010");

  reject(
      "vhdl_shadowed_std_logic_vector_rejects_ieee_characters.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Glyph_T is ('A', 'B');
  type std_logic_vector is array (natural range <>) of Glyph_T;
  type Packet_T is record Data : std_logic_vector(1 to 2); end record;
  signal Value : Packet_T;
begin
  Value <= (Data => "01");
end architecture;
)",
      "FSIM-ELAB-VHAGG-014");

  reject(
      "vhdl_shadowed_numeric_std_unsigned_rejects_logic_characters.vhd",
      R"(
library ieee;
use ieee.numeric_std.all;
entity Invalid is end entity;
architecture rtl of invalid is
  type Glyph_T is ('A', 'B');
  type unsigned is array (natural range <>) of Glyph_T;
  type Packet_T is record Data : unsigned(1 to 2); end record;
  signal Value : Packet_T;
begin
  Value <= (Data => "01");
end architecture;
)",
      "FSIM-ELAB-VHAGG-014");

  reject(
      "vhdl_contextual_string_array_element_membership.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Glyph_T is ('a', 'b');
  type Row_T is array (1 to 2) of Glyph_T;
  type Rows_T is array (1 to 2) of Row_T;
  signal Value : Rows_T;
begin
  Value <= (1 => "01", 2 => "ab");
end architecture;
)",
      "FSIM-ELAB-VHAGG-014");

  const auto contextual_string_array_control = fsim::frontend::parse_text(
      "vhdl_contextual_string_array_choice_control.vhd",
      R"(
entity Control is end entity;
architecture rtl of control is
  type Character_Vector_T is array (natural range <>) of character;
  type Row_T is array (1 to 2) of character;
  type Rows_T is array (1 to 2) of Row_T;
  signal Bits : bit_vector(0 to 1);
  signal Characters : Character_Vector_T(0 to 1);
  signal Rows : Rows_T;
begin
  Bits <= (0 to 1 => "01");
  Characters <= (0 to 1 => "AB");
  -- Here the string literal has Row_T, the array element type. The range
  -- choice selects outer Rows_T elements and must not be treated as a slice.
  Rows <= (2 downto 1 => "CD");
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(contextual_string_array_control.ok());
  const auto contextual_string_array_design = compile_and_elaborate(
      contextual_string_array_control.design,
      "vhdl:work.control(rtl)");
  if (!contextual_string_array_design.ok()) {
    for (const auto& diagnostic : contextual_string_array_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(contextual_string_array_design.ok());

  const auto shadowed_logic_vector = fsim::frontend::parse_text(
      "vhdl_shadowed_std_logic_vector_string_context.vhd",
      R"(
entity Shadowed is end entity;
architecture rtl of shadowed is
  type Glyph_T is ('A', 'B');
  type std_logic_vector is array (natural range <>) of Glyph_T;
  type Packet_T is record Data : std_logic_vector(1 to 2); end record;
  signal Value : Packet_T;
begin
  Value <= (Data => "AB");
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(shadowed_logic_vector.ok());
  const auto shadowed_logic_vector_design = compile_and_elaborate(
      shadowed_logic_vector.design, "vhdl:work.shadowed(rtl)");
  assert(shadowed_logic_vector_design.ok());

  const auto apostrophe_enum_control = fsim::frontend::parse_text(
      "vhdl_contextual_apostrophe_enum_control.vhd",
      R"(
entity Control is end entity;
architecture rtl of control is
  type Glyph_T is ('a', ''');
  type Glyph_Vector_T is array (1 to 2) of Glyph_T;
  signal Value : Glyph_Vector_T;
begin
  Value <= "a'";
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(apostrophe_enum_control.ok());
  const auto apostrophe_enum_design = compile_and_elaborate(
      apostrophe_enum_control.design, "vhdl:work.control(rtl)");
  assert(apostrophe_enum_design.ok());

  const auto empty_array_element_control = fsim::frontend::parse_text(
      "vhdl_null_string_array_element_control.vhd",
      R"(
entity Control is end entity;
architecture rtl of control is
  type Empty_String_T is array (1 to 0) of character;
  type Empty_Rows_T is array (1 to 1) of Empty_String_T;
  signal Value : Empty_Rows_T;
begin
  Value <= (1 => "");
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(empty_array_element_control.ok());
  const auto empty_array_element_design = compile_and_elaborate(
      empty_array_element_control.design, "vhdl:work.control(rtl)");
  if (!empty_array_element_design.ok()) {
    for (const auto& diagnostic : empty_array_element_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(empty_array_element_design.ok());
  reject(
      "vhdl_null_array_range_wrong_element_type.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Left_T is (left_a, left_b);
  type Right_T is (right_a, right_b);
  type Left_Vector_T is array (natural range <>) of Left_T;
  signal Value : Left_Vector_T(2 to 1);
begin
  Value <= (2 to 1 => right_a);
end architecture;
)",
      "FSIM-ELAB-VHAGG-014");
  const auto null_array_segment_control = fsim::frontend::parse_text(
      "vhdl_null_array_range_empty_segment.vhd",
      R"(
entity Control is end entity;
architecture rtl of control is
  type Bits_T is array (integer range <>) of bit;
  signal Empty_Segment : Bits_T(1 to 0);
  signal Value : Bits_T(2 to 1);
begin
  Value <= (2 to 1 => Empty_Segment);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(null_array_segment_control.ok());
  const auto null_array_segment_design = compile_and_elaborate(
      null_array_segment_control.design, "vhdl:work.control(rtl)");
  if (!null_array_segment_design.ok()) {
    for (const auto& diagnostic : null_array_segment_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(null_array_segment_design.ok());
  reject(
      "vhdl_null_array_range_nonempty_segment.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (integer range <>) of bit;
  signal Nonempty_Segment : Bits_T(0 to 1);
  signal Value : Bits_T(2 to 1);
begin
  Value <= (2 to 1 => Nonempty_Segment);
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-006");
  reject(
      "vhdl_null_range_with_others.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (integer range <>) of bit;
  signal Value : Bits_T(2 to 1);
begin
  Value <= (2 to 1 => '0', others => '1');
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-011");
  reject(
      "vhdl_null_range_with_multiple_choices.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (integer range <>) of bit;
  signal Value : Bits_T(2 to 1);
begin
  Value <= (2 to 1 | 4 to 3 => '0');
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-011");
  reject(
      "vhdl_nonempty_null_string_array_element.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Empty_String_T is array (1 to 0) of character;
  type Empty_Rows_T is array (1 to 1) of Empty_String_T;
  signal Value : Empty_Rows_T;
begin
  Value <= (1 => "x");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-006");

  const auto nested_array_element_control = fsim::frontend::parse_text(
      "vhdl_array_value_choice_element_direction.vhd",
      R"(
entity Control is end entity;
architecture rtl of control is
  type Row_T is array (1 to 2) of character;
  type Rows_T is array (1 to 4) of Row_T;
  signal Source : Row_T;
  signal Value : Rows_T;
begin
  Value <= (4 downto 3 => Source, others => Source);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(nested_array_element_control.ok());
  const auto nested_array_element_design = compile_and_elaborate(
      nested_array_element_control.design, "vhdl:work.control(rtl)");
  if (!nested_array_element_design.ok()) {
    for (const auto& diagnostic : nested_array_element_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(nested_array_element_design.ok());

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
