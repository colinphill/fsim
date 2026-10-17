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

  const auto direct_signal_slice_aggregates = fsim::frontend::parse_text(
      "vhdl_direct_signal_slice_aggregate_context.vhd",
      R"(
entity Direct_Signal_Slice_Aggregate_Context is end entity;
architecture rtl of direct_signal_slice_aggregate_context is
  type sample_bits_t is array (integer range <>) of bit;
  signal ascending : sample_bits_t(0 to 3) := (others => '0');
  signal descending : sample_bits_t(3 downto 0) := (others => '0');
  signal source_byte : bit_vector(7 downto 0) := (others => '0');
  signal conditional_byte : bit_vector(7 downto 0) := (others => '0');
begin
  conditional_byte(1 downto 0) <=
    (1 => source_byte(1), 0 => '0') when true else (others => '0');

  drive : process
  begin
    ascending(1 to 2) <= ('1', '0');
    descending(2 downto 1) <= ('0', '1');
    source_byte(0) <= '0';
    source_byte(1) <= '1';
    wait for 1 ns;
    assert ascending(0) = '0' and ascending(1) = '1'
            and ascending(2) = '0' and ascending(3) = '0'
      report "ascending signal-slice aggregate lost its range"
      severity failure;
    assert descending(3) = '0' and descending(2) = '0'
            and descending(1) = '1' and descending(0) = '0'
      report "descending signal-slice aggregate lost its range"
      severity failure;
    assert conditional_byte = "00000010"
      report "conditional named-range slice aggregate lost its context"
      severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(direct_signal_slice_aggregates.ok());
  const auto direct_signal_slice_design = compile_and_elaborate(
      direct_signal_slice_aggregates.design,
      "vhdl:work.direct_signal_slice_aggregate_context(rtl)");
  if (!direct_signal_slice_design.ok()) {
    for (const auto& diagnostic : direct_signal_slice_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(direct_signal_slice_design.ok());
  const auto direct_ascending =
      direct_signal_slice_design.design->find_signal("ascending");
  const auto direct_descending =
      direct_signal_slice_design.design->find_signal("descending");
  const auto direct_conditional =
      direct_signal_slice_design.design->find_signal("conditional_byte");
  assert(direct_ascending && direct_descending && direct_conditional);
  auto direct_signal_slice_interpreter =
      direct_signal_slice_design.design->create_interpreter();
  const auto direct_signal_slice_run =
      direct_signal_slice_interpreter->run();
  assert(
      direct_signal_slice_run.status == fsim::runtime::RunStatus::completed);
  assert(
      direct_signal_slice_interpreter->signal_value(*direct_ascending)
          .to_msb_string()
          == "0100"
      && direct_signal_slice_interpreter->signal_value(*direct_descending)
             .to_msb_string()
          == "0010"
      && direct_signal_slice_interpreter->signal_value(*direct_conditional)
             .to_msb_string()
          == "00000010");

  const auto constrained_bit_row_slice = fsim::frontend::parse_text(
      "vhdl_constrained_bit_row_slice_context.vhd",
      R"(
entity Constrained_Bit_Row_Slice_Context is end entity;
architecture test of constrained_bit_row_slice_context is
  type bit_rows_t is array (natural range <>) of bit_vector;
  subtype byte_rows_t is bit_rows_t(open)(7 downto 0);
  signal data : byte_rows_t(1 to 3);
begin
  check : process is
  begin
    data(2) <= X"AB";
    wait for 1 ns;
    assert data(2) = X"AB"
      report "constrained open-array row lost its subtype"
      severity failure;
    data(1 to 2) <= (X"01", X"02");
    wait for 1 ns;
    assert data(1) = X"01" and data(2) = X"02"
            and data(3) = X"00"
      report "nested row slice lost its element constraints"
      severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(constrained_bit_row_slice.ok());
  const auto constrained_bit_row_design = compile_and_elaborate(
      constrained_bit_row_slice.design,
      "vhdl:work.constrained_bit_row_slice_context(test)");
  if (!constrained_bit_row_design.ok()) {
    for (const auto& diagnostic : constrained_bit_row_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(constrained_bit_row_design.ok());
  const auto constrained_rows =
      constrained_bit_row_design.design->find_signal("data");
  assert(constrained_rows);
  auto constrained_bit_row_interpreter =
      constrained_bit_row_design.design->create_interpreter();
  const auto constrained_bit_row_run =
      constrained_bit_row_interpreter->run();
  assert(
      constrained_bit_row_run.status == fsim::runtime::RunStatus::completed);
  assert(
      constrained_bit_row_interpreter->signal_value(*constrained_rows)
          .to_msb_string()
      == "000000010000001000000000");

  const auto short_bit_row_slice = fsim::frontend::parse_text(
      "vhdl_short_bit_row_slice_missing_index.vhd",
      R"(
entity Short_Bit_Row_Slice_Missing_Index is end entity;
architecture test of short_bit_row_slice_missing_index is
  type bit_rows_t is array (natural range <>) of bit_vector;
  subtype byte_rows_t is bit_rows_t(open)(7 downto 0);
  signal data : byte_rows_t(1 to 3);
begin
  check : process is
  begin
    data(1 to 2) <= (1 => X"01");
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(short_bit_row_slice.ok());
  const auto rejected_short_bit_row_slice = compile_and_elaborate(
      short_bit_row_slice.design,
      "vhdl:work.short_bit_row_slice_missing_index(test)");
  // A named aggregate without others takes its bounds from its choices
  // (1 to 1), so it fails the length check against the two-row target
  // (IEEE 1076-2008 9.3.3.3).
  if (rejected_short_bit_row_slice.ok()
      || !has_diagnostic(
          rejected_short_bit_row_slice, "FSIM-ELAB-VHARRAYAGG-006")) {
    for (const auto& diagnostic : rejected_short_bit_row_slice.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(
      !rejected_short_bit_row_slice.ok()
      && has_diagnostic(
          rejected_short_bit_row_slice, "FSIM-ELAB-VHARRAYAGG-006"));

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

  const auto dynamic_scalar_target = fsim::frontend::parse_text(
      "vhdl_dynamic_scalar_indexed_assignment.vhd",
      R"(
entity Dynamic_Scalar_Indexed_Assignment is end entity;
architecture rtl of dynamic_scalar_indexed_assignment is
  signal Value : bit_vector(0 to 1) := "00";
begin
  assign_value : process
    variable Index : integer := 0;
  begin
    Value(Index) <= '1';
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(dynamic_scalar_target.ok());
  const auto accepted_dynamic_scalar_target = compile_and_elaborate(
      dynamic_scalar_target.design,
      "vhdl:work.dynamic_scalar_indexed_assignment(rtl)");
  assert(accepted_dynamic_scalar_target.ok());

  const auto static_alias_target = fsim::frontend::parse_text(
      "vhdl_aggregate_signal_target_static_alias_bound.vhd",
      R"(
entity Static_Alias_Target is end entity;
architecture rtl of static_alias_target is
  subtype First_Index_T is integer range 0 to 1;
  subtype Index_T is First_Index_T;
  constant I : Index_T := 0;
  signal S, T : bit_vector(0 to 1);
begin
  assign_values : process
  begin
    (S(I), T(I)) <= bit_vector'("11");
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(static_alias_target.ok());
  const auto accepted_static_alias_target = compile_and_elaborate(
      static_alias_target.design, "vhdl:work.static_alias_target(rtl)");
  assert(accepted_static_alias_target.ok());

  reject(
      "vhdl_aggregate_signal_target_variable_index.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal S, T : bit_vector(0 to 1);
begin
  assign_values : process
    variable I : integer := 0;
  begin
    (S(I), T(I)) <= bit_vector'("11");
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHASSIGN-005");
  reject(
      "vhdl_aggregate_signal_target_generic_index.vhd",
      R"(
entity Invalid is generic (GEN : integer); end entity;
architecture rtl of invalid is
  signal S, T : bit_vector(0 to 1);
begin
  assign_values : process
  begin
    (S(GEN), T(GEN)) <= bit_vector'("11");
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHASSIGN-005");
  reject(
      "vhdl_aggregate_signal_target_constant_generic_alias_bound.vhd",
      R"(
entity Invalid is generic (GEN : integer := 1); end entity;
architecture rtl of invalid is
  subtype First_Index_T is integer range 0 to GEN;
  subtype Index_T is First_Index_T;
  constant I : Index_T := 0;
  signal S, T : bit_vector(0 to 1);
begin
  assign_values : process
  begin
    (S(I), T(I)) <= bit_vector'("11");
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHASSIGN-005");
  reject(
      "vhdl_aggregate_variable_target_dynamic_index.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
begin
  assign_values : process
    type Pair_T is array (0 to 1) of bit;
    variable I, J : integer := 0;
    variable S, T : bit_vector(0 to 1);
  begin
    (S(I), T(J)) := Pair_T'('0', '1');
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHASSIGN-005");

  const auto qualified_aggregate_targets = fsim::frontend::parse_text(
      "vhdl_qualified_aggregate_assignment_targets.vhd",
      R"(
entity Qualified_Aggregate_Assignment_Targets is end entity;
architecture rtl of qualified_aggregate_assignment_targets is
  type Bit_Pair_T is array (0 to 1) of bit;
  type Integer_Pair_T is array (0 to 1) of integer;
  signal A, B : bit;
begin
  assign_values : process
    variable First, Second : integer;
  begin
    (A, B) <= Bit_Pair_T'('1', '0');
    (First, Second) := Integer_Pair_T'(3, 4);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(qualified_aggregate_targets.ok());
  const auto accepted_qualified_aggregate_targets = compile_and_elaborate(
      qualified_aggregate_targets.design,
      "vhdl:work.qualified_aggregate_assignment_targets(rtl)");
  if (!accepted_qualified_aggregate_targets.ok()) {
    for (const auto& diagnostic :
         accepted_qualified_aggregate_targets.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(accepted_qualified_aggregate_targets.ok());

  reject(
      "vhdl_signal_aggregate_target_unqualified_rhs.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal A, B : bit;
begin
  assign_values : process
  begin
    (A, B) <= ('1', '0');
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHAGG-001");
  reject(
      "vhdl_variable_aggregate_target_unqualified_rhs.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
begin
  assign_values : process
    variable First, Second : integer;
  begin
    (First, Second) := (3, 4);
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHAGG-001");

  const auto same_type_rows = fsim::frontend::parse_text(
      "vhdl_aggregate_same_type_rows.vhd",
      R"(
entity Aggregate_Same_Type_Rows is end entity;
architecture rtl of aggregate_same_type_rows is
  type Row_T is array (integer range <>) of integer;
  type Rows_T is array (integer range <>) of Row_T;
  type Matrix_T is array (integer range <>, integer range <>) of integer;
  signal Value : Rows_T(0 to 1)(0 to 1);
  signal Matrix : Matrix_T(0 to 1, 0 to 1);
begin
  Value <= (0 => Row_T'(1, 2), 1 => Row_T'(3, 4));
  Matrix <= (0 => (0 => 1, 1 => 2), 1 => (0 => 3, 1 => 4));
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(same_type_rows.ok());
  const auto accepted_same_type_rows = compile_and_elaborate(
      same_type_rows.design, "vhdl:work.aggregate_same_type_rows(rtl)");
  if (!accepted_same_type_rows.ok()) {
    for (const auto& diagnostic :
         accepted_same_type_rows.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(accepted_same_type_rows.ok());

  const auto named_bounds_sliding_rows = fsim::frontend::parse_text(
      "vhdl_aggregate_named_bounds_sliding_rows.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity Aggregate_Named_Bounds_Sliding_Rows is end entity;
architecture rtl of aggregate_named_bounds_sliding_rows is
  type Row_T is array (integer range <>) of std_logic;
  type Rows_T is array (integer range <>) of Row_T;
  signal Value : Rows_T(0 to 1)(0 to 1);
begin
  Value <= (0 => Row_T'(3 => '0', 4 => '1'),
            1 => Row_T'(5 => '1', 6 => '0'));
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(named_bounds_sliding_rows.ok());
  const auto accepted_named_bounds_sliding_rows = compile_and_elaborate(
      named_bounds_sliding_rows.design,
      "vhdl:work.aggregate_named_bounds_sliding_rows(rtl)");
  if (!accepted_named_bounds_sliding_rows.ok()) {
    for (const auto& diagnostic :
         accepted_named_bounds_sliding_rows.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(accepted_named_bounds_sliding_rows.ok());

  reject(
      "vhdl_aggregate_named_bounds_sliding_rows_wrong_extent.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity Invalid is end entity;
architecture rtl of invalid is
  type Row_T is array (integer range <>) of std_logic;
  type Rows_T is array (integer range <>) of Row_T;
  signal Value : Rows_T(0 to 1)(0 to 1);
begin
  Value <= (0 => Row_T'(3 => '0', 4 => '1', 5 => '0'),
            1 => Row_T'(5 => '1', 6 => '0', 7 => '1'));
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-006");

  const auto same_type_positional_segments = fsim::frontend::parse_text(
      "vhdl_aggregate_same_type_positional_segments.vhd",
      R"(
entity Aggregate_Same_Type_Positional_Segments is end entity;
architecture rtl of aggregate_same_type_positional_segments is
  type Word_T is array (integer range <>) of integer;
  signal Value : Word_T(0 to 3);
begin
  Value <= (Word_T'(1, 2), Word_T'(3, 4));
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(same_type_positional_segments.ok());
  const auto accepted_same_type_positional_segments = compile_and_elaborate(
      same_type_positional_segments.design,
      "vhdl:work.aggregate_same_type_positional_segments(rtl)");
  if (!accepted_same_type_positional_segments.ok()) {
    for (const auto& diagnostic :
         accepted_same_type_positional_segments.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(accepted_same_type_positional_segments.ok());

  reject(
      "vhdl_aggregate_wrong_nominal_positional_segments.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Word_T is array (integer range <>) of integer;
  type Other_Word_T is array (integer range <>) of integer;
  signal Value : Word_T(0 to 3);
begin
  Value <= (Other_Word_T'(1, 2), Other_Word_T'(3, 4));
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009");

  reject(
      "vhdl_aggregate_row_wrong_nominal_type.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Row_T is array (integer range <>) of integer;
  type Other_Row_T is array (integer range <>) of integer;
  type Rows_T is array (integer range <>) of Row_T;
  signal Value : Rows_T(0 to 1)(0 to 1);
begin
  Value <= (0 => Other_Row_T'(1, 2), 1 => Other_Row_T'(3, 4));
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009");

  reject(
      "vhdl_rank2_aggregate_element_wrong_rank.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Matrix_T is array (integer range <>, integer range <>) of integer;
  type Wrong_Matrix_T is array (integer range <>, integer range <>) of real;
  signal Value : Matrix_T(0 to 1, 0 to 1);
begin
  Value <=
    (0 =>
       (0 => Wrong_Matrix_T'((0 => (0 => 1.0, 1 => 2.0),
                              1 => (0 => 3.0, 1 => 4.0))),
        1 => 0),
     1 => (0 => 0, 1 => 0));
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009");

  const auto unselected_time_leaf = fsim::frontend::parse_text(
      "vhdl_unselected_time_integer_matrix_leaf.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Matrix_T is array (integer range <>, integer range <>) of integer;
  constant Values : Matrix_T(1 to 2, 1 to 2) := ((1, 2), (3, 4 ns));
begin
  check_selected_cell : process
  begin
    assert Values(1, 1) = 1;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(unselected_time_leaf.ok());
  const auto rejected_time_matrix = compile_and_elaborate(
      unselected_time_leaf.design, "vhdl:work.invalid(rtl)");
  assert(!rejected_time_matrix.ok()
      && !rejected_time_matrix.diagnostics.empty());

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
      // Without others the choices give the bounds; the length then
      // mismatches the target (IEEE 1076-2008 9.3.3.3).
      "FSIM-ELAB-VHARRAYAGG-006");
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
      "vhdl_ragged_rank2_constant_shape.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Matrix_T is array (natural range <>, natural range <>) of integer;
  constant Values : Matrix_T := ((1, 2), (3, 4, 5));
begin
  process is
  begin
    assert Values'length(2) = 3;
  end process;
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
      // Without others the choices give the bounds; the length then
      // mismatches the target (IEEE 1076-2008 9.3.3.3).
      "FSIM-ELAB-VHARRAYAGG-006");
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

  const auto unconstrained_matrix_row_comparison = fsim::frontend::parse_text(
      "vhdl_unconstrained_matrix_row_string_comparison.vhd",
      R"(
entity Control is end entity;
architecture rtl of control is
  type Matrix_T is array (natural range <>, positive range <>) of bit;
  signal Matrix : Matrix_T(0 to 1, 3 downto 1);
  signal Equal : boolean;
begin
  Equal <= Matrix = ("101", "010");
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(unconstrained_matrix_row_comparison.ok());
  const auto unconstrained_matrix_row_design = compile_and_elaborate(
      unconstrained_matrix_row_comparison.design,
      "vhdl:work.control(rtl)");
  if (!unconstrained_matrix_row_design.ok()) {
    for (const auto& diagnostic :
         unconstrained_matrix_row_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(unconstrained_matrix_row_design.ok());

  const auto constrained_matrix_row_width_mismatch
      = fsim::frontend::parse_text(
          "vhdl_constrained_matrix_row_string_width_mismatch.vhd",
          R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
  signal Value : Matrix_T := ("1010", "0101");
begin
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(constrained_matrix_row_width_mismatch.ok());
  const auto constrained_matrix_row_width_result = compile_and_elaborate(
      constrained_matrix_row_width_mismatch.design,
      "vhdl:work.invalid(rtl)");
  assert(
      !constrained_matrix_row_width_result.ok()
      && has_diagnostic(
          constrained_matrix_row_width_result,
          "FSIM-ELAB-VHARRAYAGG-006"));

  const auto multidimensional_string_enum_control
      = fsim::frontend::parse_text(
          "vhdl_multidimensional_string_enum_membership_control.vhd",
          R"(
entity Control is end entity;
architecture rtl of control is
  type Glyph_T is ('a', 'b');
  type Matrix_T is array (natural range <>, positive range <>) of Glyph_T;
  signal Matrix : Matrix_T(0 to 1, 1 to 2);
  signal Equal : boolean;
begin
  Equal <= Matrix = ("ab", "ba");
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(multidimensional_string_enum_control.ok());
  const auto accepted_multidimensional_string_enum_control
      = compile_and_elaborate(
          multidimensional_string_enum_control.design,
          "vhdl:work.control(rtl)");
  if (!accepted_multidimensional_string_enum_control.ok()) {
    for (const auto& diagnostic :
         accepted_multidimensional_string_enum_control.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(accepted_multidimensional_string_enum_control.ok());

  reject(
      "vhdl_multidimensional_string_enum_rejects_nonmember.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Glyph_T is ('a', 'b');
  type Matrix_T is array (natural range <>, positive range <>) of Glyph_T;
  signal Matrix : Matrix_T(0 to 1, 1 to 2);
  signal Equal : boolean;
begin
  Equal <= Matrix = ("01", "ab");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-014");

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

  const auto nested_record_string = fsim::frontend::parse_text(
      "vhdl_nested_record_string_positional_segment.vhd",
      R"(
entity Nested_Record_String_Segment is end entity;
architecture rtl of nested_record_string_segment is
  type Packet_T is record
    Name : string(1 to 20);
  end record;
  signal Value : Packet_T;
begin
  Value <= (Name => ("hello", others => NUL));
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(nested_record_string.ok());
  const auto nested_record_string_design = compile_and_elaborate(
      nested_record_string.design,
      "vhdl:work.nested_record_string_segment(rtl)");
  if (!nested_record_string_design.ok()) {
    for (const auto& diagnostic : nested_record_string_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(nested_record_string_design.ok());
  const auto nested_record_string_value
      = nested_record_string_design.design->find_signal("value");
  assert(nested_record_string_value);
  auto nested_record_string_interpreter
      = nested_record_string_design.design->create_interpreter();
  const auto nested_record_string_run
      = nested_record_string_interpreter->run();
  assert(nested_record_string_run.status
      == fsim::runtime::RunStatus::completed);
  assert(nested_record_string_interpreter
          ->signal_value(*nested_record_string_value).to_msb_string()
      == std::string { "0110100001100101011011000110110001101111" }
          + std::string(15U * 8U, '0'));

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
  reject(
      "vhdl_null_function_result_wrong_enum_row.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Glyph_T is (A, B);
  type Rows_T is array (natural range <>) of bit_vector;
  signal Source : Rows_T(0 to 1)(0 to 1);
  function Empty_Rows(constant Value : in Rows_T) return Rows_T is
  begin
    return Value(1 to 0);
  end function;
begin
  check: process
  begin
    assert Empty_Rows(Source) = (1 to 0 => (A, B)) severity failure;
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-014");
  reject(
      "vhdl_null_function_result_wrong_boolean_row.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Rows_T is array (natural range <>) of integer_vector;
  signal Source : Rows_T(0 to 1)(0 to 1);
  function Empty_Rows(constant Value : in Rows_T) return Rows_T is
  begin
    return Value(1 to 0);
  end function;
begin
  check: process
  begin
    assert Empty_Rows(Source) = (1 to 0 => (true, false)) severity failure;
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-014");
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

  const auto slice_index_controls = fsim::frontend::parse_text(
      "vhdl_slice_index_type_controls.vhd",
      R"(
entity Slice_Index_Type_Controls is end entity;
architecture rtl of slice_index_type_controls is
  type Index_T is range 0 to 3;
  type Values_T is array (Index_T range <>) of bit;
  signal Literal_Values, Typed_Values : Values_T(0 to 3);
  constant Same_Left : Index_T := 0;
  constant Same_Right : Index_T := 1;
begin
  Literal_Values(1 to 2) <= "10";
  Typed_Values(Same_Left to Same_Right) <= "01";
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(slice_index_controls.ok());
  const auto accepted_slice_index_controls = compile_and_elaborate(
      slice_index_controls.design,
      "vhdl:work.slice_index_type_controls(rtl)");
  if (!accepted_slice_index_controls.ok()) {
    for (const auto& diagnostic :
         accepted_slice_index_controls.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(accepted_slice_index_controls.ok());

  const auto package_slice_index_controls = fsim::frontend::parse_text(
      "vhdl_package_slice_index_shadow_controls.vhd",
      R"(
package Index_Pkg is
  type Index_T is range 0 to 3;
  type Values_T is array (Index_T range <>) of bit;
end package;

entity Slice_Package_Index_Shadow is end entity;
architecture rtl of slice_package_index_shadow is
  type Index_T is range 0 to 3;
  signal Value : work.Index_Pkg.Values_T(0 to 3);
  constant Good_Left : work.Index_Pkg.Index_T := 0;
  constant Good_Right : work.Index_Pkg.Index_T := 1;
begin
  Value(Good_Left to Good_Right) <= "10";
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(package_slice_index_controls.ok());
  const auto accepted_package_slice_index_controls = compile_and_elaborate(
      package_slice_index_controls.design,
      "vhdl:work.slice_package_index_shadow(rtl)");
  if (!accepted_package_slice_index_controls.ok()) {
    for (const auto& diagnostic :
         accepted_package_slice_index_controls.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(accepted_package_slice_index_controls.ok());

  reject(
      "vhdl_package_slice_index_local_shadow_rejected.vhd",
      R"(
package Index_Pkg is
  type Index_T is range 0 to 3;
  type Values_T is array (Index_T range <>) of bit;
end package;

entity Invalid is end entity;
architecture rtl of invalid is
  type Index_T is range 0 to 3;
  signal Value : work.Index_Pkg.Values_T(0 to 3);
  constant Bad_Left : Index_T := 0;
  constant Bad_Right : Index_T := 1;
begin
  Value(Bad_Left to Bad_Right) <= "10";
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-006");

  reject(
      "vhdl_slice_bound_distinct_index_type.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Index_T is range 0 to 3;
  type Other_Index_T is range 0 to 3;
  type Values_T is array (Index_T range <>) of bit;
  signal Value : Values_T(0 to 3);
  constant Bad_Left : Other_Index_T := 0;
  constant Bad_Right : Other_Index_T := 1;
begin
  Value(Bad_Left to Bad_Right) <= "10";
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-006");
}

}  // namespace fsim::tests::elaboration

namespace fsim::tests::elaboration {

void test_vhdl_bit_vector_array_segment_literals() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_bit_vector_positional_literal_segments.vhd",
      R"(
entity Bit_Vector_Positional_Literal_Segments is end entity;
architecture rtl of bit_vector_positional_literal_segments is
  constant Prefix : bit_vector(3 downto 0) := X"2";
  signal Bit_String_Value : bit_vector(7 downto 0);
  signal Ordinary_String_Value : bit_vector(7 downto 0);
begin
  Bit_String_Value <= (Prefix, X"5");
  Ordinary_String_Value <= (Prefix, "0101");
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto design = compile_and_elaborate(
      parsed.design,
      "vhdl:work.bit_vector_positional_literal_segments(rtl)");
  if (!design.ok()) {
    for (const auto& diagnostic : design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(design.ok());
  const auto bit_string_value =
      design.design->find_signal("bit_string_value");
  const auto ordinary_string_value =
      design.design->find_signal("ordinary_string_value");
  assert(bit_string_value && ordinary_string_value);
  auto interpreter = design.design->create_interpreter();
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  assert(
      interpreter->signal_value(*bit_string_value).to_msb_string()
          == "00100101"
      && interpreter->signal_value(*ordinary_string_value).to_msb_string()
          == "00100101");

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
  };

  reject(
      "vhdl_bit_vector_positional_literal_invalid_character.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  constant Prefix : bit_vector(3 downto 0) := X"2";
  signal Value : bit_vector(7 downto 0);
begin
  Value <= (Prefix, "0X01");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009");
  reject(
      "vhdl_bit_vector_positional_literal_wrong_nominal.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Other_Bits_T is array (natural range <>) of bit;
  signal Prefix : Other_Bits_T(3 downto 0);
  signal Value : bit_vector(7 downto 0);
begin
  Value <= (Prefix, "0101");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-009");
  reject(
      "vhdl_bit_vector_positional_literal_outside_range.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  constant Prefix : bit_vector(3 downto 0) := X"2";
  signal Value : bit_vector(7 downto 0);
begin
  Value <= (Prefix, "01010");
end architecture;
)",
      "FSIM-ELAB-VHARRAYAGG-003");
}

}  // namespace fsim::tests::elaboration

namespace fsim::tests::elaboration {

void test_vhdl_slice_index_range_propagation() {
  const auto formal_range_index = fsim::frontend::parse_text(
      "vhdl_formal_range_index_positive.vhd",
      R"(
entity Formal_Range_Index_Positive is end entity;
architecture rtl of formal_range_index_positive is
  procedure touch_range(signal source : in bit_vector) is
    variable temporary : bit_vector(source'range);
  begin
    temporary(temporary'left) := not temporary(temporary'left);
  end procedure;
  signal Ascending : bit_vector(0 to 3) := "0000";
  signal Descending : bit_vector(3 downto 0) := "0000";
  signal Ascending_Singleton : bit_vector(2 to 2) := "0";
  signal Descending_Singleton : bit_vector(2 downto 2) := "0";
begin
  check : process
  begin
    touch_range(Ascending);
    touch_range(Descending);
    touch_range(Ascending_Singleton);
    touch_range(Descending_Singleton);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(formal_range_index.ok());
  const auto formal_range_index_design = compile_and_elaborate(
      formal_range_index.design,
      "vhdl:work.formal_range_index_positive(rtl)");
  assert(formal_range_index_design.ok());

  const auto positive = fsim::frontend::parse_text(
      "vhdl_slice_index_range_positive.vhd",
      R"(
entity Slice_Index_Range_Positive is end entity;
architecture rtl of slice_index_range_positive is
  type idx is range 1 to 10;
  type aray1 is array (idx) of bit;
  type aray2 is array (idx range <>) of aray1;
  type row_t is array (idx range <>) of bit;
  type rows_t is array (idx range <>) of row_t;
begin
  check : process
    variable v2 : aray1;
    variable v3 : aray2(1 to 2);
    variable row_value : row_t(4 to 5);
    variable rows_value : rows_t(1 to 2)(4 to 5);
  begin
    v2 := v3(2 to 2)(2);
    v2 := v3(v3'range)(2);
    row_value := rows_value(2 to 2)(2);
    row_value := rows_value(rows_value'range)(2);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(positive.ok());
  const auto positive_design = compile_and_elaborate(
      positive.design, "vhdl:work.slice_index_range_positive(rtl)");
  if (!positive_design.ok()) {
    for (const auto& diagnostic : positive_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(positive_design.ok());

  const auto reject = [](
      const std::string_view filename,
      const std::string_view source,
      const std::string_view code) {
    const auto parsed = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto result = compile_and_elaborate(
        parsed.design, "vhdl:work.invalid(rtl)");
    if (result.ok() || !has_diagnostic(result, code)) {
      std::cerr << filename << " expected " << code << '\n';
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message
                  << " at " << diagnostic.span.begin.line << ':'
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(!result.ok() && has_diagnostic(result, code));
  };

  reject(
      "vhdl_slice_index_range_wrong_index.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type idx is range 1 to 10;
  type aray1 is array (idx) of bit;
  type aray2 is array (idx range <>) of aray1;
begin
  check : process
    variable v2 : aray1;
    variable v3 : aray2(1 to 2);
  begin
    v2 := v3(2 to 2)(1);
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-003");

  reject(
      "vhdl_slice_index_range_null.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type idx is range 1 to 10;
  type aray1 is array (idx) of bit;
  type aray2 is array (idx range <>) of aray1;
begin
  check : process
    variable v2 : aray1;
    variable v3 : aray2(1 to 2);
  begin
    v2 := v3(2 to 1)(2);
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-003");

  reject(
      "vhdl_slice_index_range_direction.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type idx is range 1 to 10;
  type aray1 is array (idx) of bit;
  type aray2 is array (idx range <>) of aray1;
begin
  check : process
    variable v2 : aray1;
    variable v3 : aray2(1 to 2);
  begin
    v2 := v3(2 downto 1)(2);
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-004");

  reject(
      "vhdl_slice_nested_reverse_range_direction.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type idx is range 1 to 10;
  type aray1 is array (idx) of bit;
  type aray2 is array (idx range <>) of aray1;
begin
  check : process
    variable v2 : aray1;
    variable v3 : aray2(1 to 2);
  begin
    v2 := v3(2 to 2)(v3(2 to 2)'reverse_range)(2);
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-004");

  reject(
      "vhdl_slice_index_range_nominal_index.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type idx is range 1 to 10;
  type other_idx is range 1 to 10;
  type aray1 is array (idx) of bit;
  type aray2 is array (idx range <>) of aray1;
begin
  check : process
    variable v2 : aray1;
    variable v3 : aray2(1 to 2);
    variable wrong_index : other_idx := 2;
  begin
    v2 := v3(2 to 2)(wrong_index);
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-006");

  reject(
      "vhdl_slice_index_range_attribute_wrong_index.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type idx is range 1 to 10;
  type aray1 is array (idx) of bit;
  type aray2 is array (idx range <>) of aray1;
begin
  check : process
    variable v2 : aray1;
    variable v3 : aray2(1 to 2);
  begin
    v2 := v3(v3'range)(3);
    wait;
  end process;
end architecture;
)",
      "FSIM-ELAB-VHARRAYSEL-003");

  const auto enumeration_index_choices = fsim::frontend::parse_text(
      "vhdl_enumeration_index_aggregate_choices.vhd",
      R"(
entity Enumeration_Index_Aggregate_Choices is
end entity;
architecture rtl of enumeration_index_aggregate_choices is
  type Index_T is ('U', 'X', '0', '1');
  type Table_T is array (Index_T range <>) of bit;
  constant Selected_Index : Index_T := 'X';
  signal Values : Table_T('U' to '1');
begin
  drive : process
  begin
    Values <= ('U' => '1', Selected_Index => '0', '0' to '1' => '0');
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(enumeration_index_choices.ok());
  const auto enumeration_index_design = compile_and_elaborate(
      enumeration_index_choices.design,
      "vhdl:work.enumeration_index_aggregate_choices(rtl)");
  assert(enumeration_index_design.ok());
  const auto enum_index_values
      = enumeration_index_design.design->find_signal("values");
  assert(enum_index_values);
  auto enum_index_interpreter
      = enumeration_index_design.design->create_interpreter();
  const auto enum_index_run = enum_index_interpreter->run();
  assert(enum_index_run.status == fsim::runtime::RunStatus::completed);
  assert(
      enum_index_interpreter->signal_value(*enum_index_values)
          .to_msb_string()
      == "1000");

  const auto enumeration_constant_index_choices = fsim::frontend::parse_text(
      "vhdl_enumeration_constant_index_aggregate_choices.vhd",
      R"(
entity Enumeration_Constant_Index_Aggregate_Choices is
end entity;
architecture rtl of enumeration_constant_index_aggregate_choices is
  type Index_T is ('U', 'X', '0', '1');
  type Table_T is array (Index_T range <>) of bit;
  constant High_Index : Index_T := '1';
  signal Values : Table_T('U' to High_Index);
begin
  drive : process
  begin
    Values <= (
      'U' => '1', 'X' => '0', '0' => '0', High_Index => '0');
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(enumeration_constant_index_choices.ok());
  const auto enumeration_constant_index_design = compile_and_elaborate(
      enumeration_constant_index_choices.design,
      "vhdl:work.enumeration_constant_index_aggregate_choices(rtl)");
  assert(enumeration_constant_index_design.ok());
  const auto enumeration_constant_index_values
      = enumeration_constant_index_design.design->find_signal("values");
  assert(enumeration_constant_index_values);
  auto enumeration_constant_index_interpreter
      = enumeration_constant_index_design.design->create_interpreter();
  const auto enumeration_constant_index_run
      = enumeration_constant_index_interpreter->run();
  assert(
      enumeration_constant_index_run.status
      == fsim::runtime::RunStatus::completed);
  assert(
      enumeration_constant_index_interpreter
          ->signal_value(*enumeration_constant_index_values)
          .to_msb_string()
      == "1000");

  const auto enumeration_qualified_constant_index_choices
      = fsim::frontend::parse_text(
          "vhdl_enumeration_qualified_constant_index_aggregate_choices.vhd",
          R"(
entity Enumeration_Qualified_Constant_Index_Aggregate_Choices is
end entity;
architecture rtl of enumeration_qualified_constant_index_aggregate_choices is
  type Index_T is ('U', 'X', '0', '1');
  type Table_T is array (Index_T range <>) of bit;
  constant High_Index : Index_T := Index_T'('1');
  constant Selected_Index : Index_T := Index_T'('X');
  signal Values : Table_T('U' to High_Index);
begin
  drive : process
  begin
    Values <= ('U' => '1', Selected_Index => '0', '0' to '1' => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(enumeration_qualified_constant_index_choices.ok());
  const auto enumeration_qualified_constant_index_design
      = compile_and_elaborate(
          enumeration_qualified_constant_index_choices.design,
          "vhdl:work.enumeration_qualified_constant_index_aggregate_choices(rtl)");
  assert(enumeration_qualified_constant_index_design.ok());
  const auto enumeration_qualified_constant_index_values
      = enumeration_qualified_constant_index_design.design->find_signal(
          "values");
  assert(enumeration_qualified_constant_index_values);
  auto enumeration_qualified_constant_index_interpreter
      = enumeration_qualified_constant_index_design.design
            ->create_interpreter();
  const auto enumeration_qualified_constant_index_run
      = enumeration_qualified_constant_index_interpreter->run();
  assert(
      enumeration_qualified_constant_index_run.status
      == fsim::runtime::RunStatus::completed);
  assert(
      enumeration_qualified_constant_index_interpreter
          ->signal_value(*enumeration_qualified_constant_index_values)
          .to_msb_string()
      == "1000");

  const auto enumeration_type_mark_endpoint_choices =
      fsim::frontend::parse_text(
          "vhdl_enumeration_type_mark_endpoint_choices.vhd",
          R"(
entity Enumeration_Type_Mark_Endpoint_Choices is
end entity;
architecture rtl of enumeration_type_mark_endpoint_choices is
  type Index_T is (First_Index, Middle_Index, Last_Index);
  type Table_T is array (Index_T range <>) of bit;
  signal Low_Values : Table_T(First_Index to Last_Index);
  signal Left_Values : Table_T(First_Index to Last_Index);
  signal High_Values : Table_T(First_Index to Last_Index);
  signal Right_Values : Table_T(First_Index to Last_Index);
begin
  drive : process
  begin
    Low_Values <= (Index_T'low => '1', others => '0');
    Left_Values <= (Index_T'left => '1', others => '0');
    High_Values <= (Index_T'high => '1', others => '0');
    Right_Values <= (Index_T'right => '1', others => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(enumeration_type_mark_endpoint_choices.ok());
  const auto endpoint_design = compile_and_elaborate(
      enumeration_type_mark_endpoint_choices.design,
      "vhdl:work.enumeration_type_mark_endpoint_choices(rtl)");
  assert(endpoint_design.ok());
  const auto low_values = endpoint_design.design->find_signal("low_values");
  const auto left_values = endpoint_design.design->find_signal("left_values");
  const auto high_values = endpoint_design.design->find_signal("high_values");
  const auto right_values = endpoint_design.design->find_signal("right_values");
  assert(low_values && left_values && high_values && right_values);
  auto endpoint_interpreter = endpoint_design.design->create_interpreter();
  const auto endpoint_run = endpoint_interpreter->run();
  assert(endpoint_run.status == fsim::runtime::RunStatus::completed);
  assert(
      endpoint_interpreter->signal_value(*low_values).to_msb_string()
      == "100");
  assert(
      endpoint_interpreter->signal_value(*left_values).to_msb_string()
      == "100");
  assert(
      endpoint_interpreter->signal_value(*high_values).to_msb_string()
      == "001");
  assert(
      endpoint_interpreter->signal_value(*right_values).to_msb_string()
      == "001");

  const auto local_line_integer_index_choices = fsim::frontend::parse_text(
      "vhdl_local_line_integer_index_choices.vhd",
      R"(
entity Local_Line_Integer_Index_Choices is
end entity;
architecture rtl of local_line_integer_index_choices is
  subtype line is integer range 0 to 15;
  type Data_Lines is array (line range <>) of bit;
  signal Values : Data_Lines(0 to 3);
  signal Source_Values : Data_Lines(0 to 15)
      := (0 to 3 => '1', others => '0');
  signal Slice_Values : Data_Lines(0 to 3);
begin
  drive : process
  begin
    Values <= (0 to 3 => '1');
    Slice_Values <= Source_Values(0 to 3);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(local_line_integer_index_choices.ok());
  const auto local_line_design = compile_and_elaborate(
      local_line_integer_index_choices.design,
      "vhdl:work.local_line_integer_index_choices(rtl)");
  assert(local_line_design.ok());
  const auto local_line_values = local_line_design.design->find_signal("values");
  const auto local_line_slice_values
      = local_line_design.design->find_signal("slice_values");
  assert(local_line_values && local_line_slice_values);
  auto local_line_interpreter = local_line_design.design->create_interpreter();
  const auto local_line_run = local_line_interpreter->run();
  assert(local_line_run.status == fsim::runtime::RunStatus::completed);
  assert(
      local_line_interpreter->signal_value(*local_line_values).to_msb_string()
      == "1111");
  assert(
      local_line_interpreter
          ->signal_value(*local_line_slice_values).to_msb_string()
      == "1111");

  const auto wrong_enumeration_low_type_choice =
      fsim::frontend::parse_text(
          "vhdl_wrong_nominal_enumeration_low_type_choice.vhd",
          R"(
entity Wrong_Nominal_Enumeration_Low_Type_Choice is
end entity;
architecture rtl of wrong_nominal_enumeration_low_type_choice is
  type Index_T is ('U', 'X', '0', '1');
  type Other_Index_T is ('U', 'X', '0', '1');
  type Table_T is array (Index_T range <>) of bit;
  signal Values : Table_T('U' to '1');
begin
  drive : process
  begin
    Values <= (Other_Index_T'low => '1', others => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(wrong_enumeration_low_type_choice.ok());
  const auto rejected_wrong_enumeration_low_type_choice
      = compile_and_elaborate(
          wrong_enumeration_low_type_choice.design,
          "vhdl:work.wrong_nominal_enumeration_low_type_choice(rtl)");
  assert(
      !rejected_wrong_enumeration_low_type_choice.ok()
      && has_diagnostic(
          rejected_wrong_enumeration_low_type_choice,
          "FSIM-ELAB-DYNINDEX-002"));

  const auto wrong_enumeration_index_choice =
      fsim::frontend::parse_text(
          "vhdl_wrong_nominal_enumeration_index_choice.vhd",
          R"(
entity Wrong_Nominal_Enumeration_Index_Choice is
end entity;
architecture rtl of wrong_nominal_enumeration_index_choice is
  type Index_T is ('U', 'X', '0', '1');
  type Other_Index_T is ('U', 'X', '0', '1');
  type Table_T is array (Index_T range <>) of bit;
  constant Wrong_Index : Other_Index_T := 'X';
  signal Values : Table_T('U' to '1');
begin
  drive : process
  begin
    Values <= (Wrong_Index => '1', others => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(wrong_enumeration_index_choice.ok());
  const auto rejected_wrong_enumeration_index_choice
      = compile_and_elaborate(
          wrong_enumeration_index_choice.design,
          "vhdl:work.wrong_nominal_enumeration_index_choice(rtl)");
  assert(
      !rejected_wrong_enumeration_index_choice.ok()
      && has_diagnostic(
          rejected_wrong_enumeration_index_choice,
          "FSIM-ELAB-DYNINDEX-002"));

  const auto dynamic_enumeration_index_choice =
      fsim::frontend::parse_text(
          "vhdl_dynamic_enumeration_index_choice.vhd",
          R"(
entity Dynamic_Enumeration_Index_Choice is
end entity;
architecture rtl of dynamic_enumeration_index_choice is
  type Index_T is ('U', 'X', '0', '1');
  type Table_T is array (Index_T range <>) of bit;
  signal Dynamic_Index : Index_T := 'X';
  signal Values : Table_T('U' to '1');
begin
  drive : process
  begin
    Values <= (Dynamic_Index => '1', others => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(dynamic_enumeration_index_choice.ok());
  const auto rejected_dynamic_enumeration_index_choice
      = compile_and_elaborate(
          dynamic_enumeration_index_choice.design,
          "vhdl:work.dynamic_enumeration_index_choice(rtl)");
  assert(
      !rejected_dynamic_enumeration_index_choice.ok()
      && has_diagnostic(
          rejected_dynamic_enumeration_index_choice,
          "FSIM-ELAB-DYNINDEX-002"));

  const auto nonstatic_enumeration_constant_choice =
      fsim::frontend::parse_text(
          "vhdl_nonstatic_enumeration_constant_choice.vhd",
          R"(
entity Nonstatic_Enumeration_Constant_Choice is
end entity;
architecture rtl of nonstatic_enumeration_constant_choice is
  type Index_T is ('U', 'X', '0', '1');
  type Table_T is array (Index_T range <>) of bit;
  signal Values : Table_T('U' to '1');
begin
  drive : process
    variable Dynamic_Index : Index_T := 'X';
    constant Captured_Index : Index_T := Dynamic_Index;
  begin
    Values <= (Captured_Index => '1', others => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(nonstatic_enumeration_constant_choice.ok());
  const auto rejected_nonstatic_enumeration_constant_choice
      = compile_and_elaborate(
          nonstatic_enumeration_constant_choice.design,
          "vhdl:work.nonstatic_enumeration_constant_choice(rtl)");
  assert(
      !rejected_nonstatic_enumeration_constant_choice.ok()
      && has_diagnostic(
          rejected_nonstatic_enumeration_constant_choice,
          "FSIM-ELAB-DYNINDEX-002"));

  const auto ieee_std_ulogic_index_choices = fsim::frontend::parse_text(
      "vhdl_ieee_std_ulogic_index_choices.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity IEEE_Std_Ulogic_Index_Choices is
end entity;
architecture rtl of ieee_std_ulogic_index_choices is
  type Table_T is array (std_ulogic) of bit;
  signal Values : Table_T;
begin
  drive : process
  begin
    Values <= (
      'U' => '1', 'X' => '0', '0' => '1', '1' => '0', 'Z' => '1',
      'W' => '0', 'L' => '1', 'H' => '0', '-' => '1');
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(ieee_std_ulogic_index_choices.ok());
  const auto ieee_std_ulogic_index_design = compile_and_elaborate(
      ieee_std_ulogic_index_choices.design,
      "vhdl:work.ieee_std_ulogic_index_choices(rtl)");
  assert(ieee_std_ulogic_index_design.ok());
  const auto ieee_std_ulogic_values
      = ieee_std_ulogic_index_design.design->find_signal("values");
  assert(ieee_std_ulogic_values);
  auto ieee_std_ulogic_interpreter
      = ieee_std_ulogic_index_design.design->create_interpreter();
  const auto ieee_std_ulogic_run = ieee_std_ulogic_interpreter->run();
  assert(ieee_std_ulogic_run.status == fsim::runtime::RunStatus::completed);
  assert(
      ieee_std_ulogic_interpreter->signal_value(*ieee_std_ulogic_values)
          .to_msb_string()
      == "101010101");

  const auto invalid_ieee_std_ulogic_index_choice =
      fsim::frontend::parse_text(
          "vhdl_invalid_ieee_std_ulogic_index_choice.vhd",
          R"(
library ieee;
use ieee.std_logic_1164.all;
entity Invalid_IEEE_Std_Ulogic_Index_Choice is
end entity;
architecture rtl of invalid_ieee_std_ulogic_index_choice is
  type Table_T is array (std_ulogic) of bit;
  signal Values : Table_T;
begin
  drive : process
  begin
    Values <= ('Q' => '1', others => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(invalid_ieee_std_ulogic_index_choice.ok());
  const auto rejected_invalid_ieee_std_ulogic_index_choice
      = compile_and_elaborate(
          invalid_ieee_std_ulogic_index_choice.design,
          "vhdl:work.invalid_ieee_std_ulogic_index_choice(rtl)");
  assert(
      !rejected_invalid_ieee_std_ulogic_index_choice.ok()
      && has_diagnostic(
          rejected_invalid_ieee_std_ulogic_index_choice,
          "FSIM-ELAB-VHARRAYAGG-003"));

  const auto shadowed_std_ulogic_index_choice =
      fsim::frontend::parse_text(
          "vhdl_shadowed_std_ulogic_index_choice.vhd",
          R"(
library ieee;
use ieee.std_logic_1164.all;
entity Shadowed_Std_Ulogic_Index_Choice is
end entity;
architecture rtl of shadowed_std_ulogic_index_choice is
  type std_ulogic is ('A', 'B');
  type Table_T is array (std_ulogic) of bit;
  signal Values : Table_T;
begin
  drive : process
  begin
    Values <= ('U' => '1', others => '0');
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(shadowed_std_ulogic_index_choice.ok());
  const auto rejected_shadowed_std_ulogic_index_choice
      = compile_and_elaborate(
          shadowed_std_ulogic_index_choice.design,
          "vhdl:work.shadowed_std_ulogic_index_choice(rtl)");
  assert(
      !rejected_shadowed_std_ulogic_index_choice.ok()
      && has_diagnostic(
          rejected_shadowed_std_ulogic_index_choice,
          "FSIM-ELAB-VHARRAYAGG-003"));
}

}  // namespace fsim::tests::elaboration
