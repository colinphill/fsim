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
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
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
  assert(
      choice_record && others_record && nested_record && record_array
      && nested_array && mode_array && range_bits && packed_bits);

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
      && interpreter->signal_value(*packed_bits).to_msb_string() == "1010");

  const auto reject = [](
      const std::string_view filename,
      const std::string_view source,
      const std::string_view code) {
    const auto invalid = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto result = fsim::elaboration::elaborate(
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
  const auto runtime_design = fsim::elaboration::elaborate(
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
