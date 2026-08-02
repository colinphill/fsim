// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_qualified_expressions_and_conversions() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_qualified_conversion.vhd",
      R"(
entity Qualified_Conversion is
end entity;

architecture rtl of qualified_conversion is
  type State_T is (Idle, Run, Done);
  subtype Active_T is State_T range Run to Done;
  type Bits_Base_T is array (natural range <>) of bit;
  subtype Nibble_T is Bits_Base_T(3 downto 0);
  subtype Nibble_Copy_T is Bits_Base_T(3 downto 0);
  subtype Logic_Nibble_T is std_logic_vector(3 downto 0);
  type Pair_T is record
    Mode : State_T;
    Data : Nibble_T;
  end record;
  subtype Small_T is integer range 1 to 7;

  function Keep(Value : Active_T) return Active_T is
  begin
    return Value;
  end function;

  function Qualified_Return(Value : State_T) return Active_T is
  begin
    return Active_T'(Value);
  end function;

  signal State_Result : State_T;
  signal Active_Result : Active_T;
  signal Integer_Result : integer;
  signal Natural_Result : natural;
  signal Positive_Result : positive;
  signal Qualified_Bits : Nibble_T;
  signal Converted_Bits : Nibble_T;
  signal Logic_Bits : Logic_Nibble_T;
  signal Qualified_Record : Pair_T;
  signal Function_Result : Active_T;
  signal Return_Result : Active_T;
begin
  drive : process
  begin
    State_Result <= State_T'(Run);
    Active_Result <= Active_T(State_T'(Done));
    Integer_Result <= integer(Small_T'(5));
    Natural_Result <= natural(Small_T'(6));
    Positive_Result <= positive(Small_T'(7));
    Qualified_Bits <= Nibble_T'("1010");
    Converted_Bits <= Nibble_T(Nibble_Copy_T'("0110"));
    Logic_Bits <= Logic_Nibble_T'("1001");
    Qualified_Record <= Pair_T'((
      Mode => Run,
      Data => Nibble_T'("1100")));
    Function_Result <= Keep(Active_T'(Run));
    Return_Result <= Qualified_Return(Done);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "vhdl:work.qualified_conversion(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(elaborated.ok());

  const auto state = elaborated.design->find_signal("state_result");
  const auto active = elaborated.design->find_signal("active_result");
  const auto integer = elaborated.design->find_signal("integer_result");
  const auto natural = elaborated.design->find_signal("natural_result");
  const auto positive = elaborated.design->find_signal("positive_result");
  const auto qualified_bits =
      elaborated.design->find_signal("qualified_bits");
  const auto converted_bits =
      elaborated.design->find_signal("converted_bits");
  const auto logic_bits = elaborated.design->find_signal("logic_bits");
  const auto qualified_record =
      elaborated.design->find_signal("qualified_record");
  const auto function =
      elaborated.design->find_signal("function_result");
  const auto returned = elaborated.design->find_signal("return_result");
  assert(
      state && active && integer && natural && positive && qualified_bits
      && converted_bits && logic_bits && qualified_record && function
      && returned);

  auto interpreter = elaborated.design->create_interpreter();
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  assert(
      interpreter->signal_value(*state).to_msb_string() == "01"
      && interpreter->signal_value(*active).to_msb_string() == "10"
      && interpreter->signal_value(*integer).to_msb_string()
          == "00000000000000000000000000000101"
      && interpreter->signal_value(*natural).to_msb_string()
          == "00000000000000000000000000000110"
      && interpreter->signal_value(*positive).to_msb_string()
          == "00000000000000000000000000000111"
      && interpreter->signal_value(*qualified_bits).to_msb_string()
          == "1010"
      && interpreter->signal_value(*converted_bits).to_msb_string()
          == "0110"
      && interpreter->signal_value(*logic_bits).to_msb_string() == "1001"
      && interpreter->signal_value(*qualified_record).to_msb_string()
          == "011100"
      && interpreter->signal_value(*function).to_msb_string() == "01"
      && interpreter->signal_value(*returned).to_msb_string() == "10");

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
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message
                  << " at " << diagnostic.span.begin.line << ':'
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(!result.ok() && has_diagnostic(result, code));
  };

  reject(
      "vhdl_qualified_invisible.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal Value : integer;
begin
  Value <= Missing_T'(1);
end architecture;
)",
      "FSIM-ELAB-VHQUAL-001");
  reject(
      "vhdl_qualified_indefinite.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (natural range <>) of bit;
  signal Value : bit;
begin
  Value <= Bits_T'("1");
end architecture;
)",
      "FSIM-ELAB-VHQUAL-002");
  reject(
      "vhdl_qualified_nominal.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is (First_0, First_1);
  type Second_T is (Second_0, Second_1);
  signal Source : Second_T;
  signal Value : First_T;
begin
  Value <= First_T'(Source);
end architecture;
)",
      "FSIM-ELAB-VHQUAL-003");
  reject(
      "vhdl_qualified_width.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal Value : bit;
begin
  Value <= bit'("1010");
end architecture;
)",
      "FSIM-ELAB-VHQUAL-003");
  reject(
      "vhdl_qualified_context.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is (First_0, First_1);
  type Second_T is (Second_0, Second_1);
  signal Value : First_T;
begin
  Value <= Second_T'(Second_0);
end architecture;
)",
      "FSIM-ELAB-VHQUAL-004");
  reject(
      "vhdl_qualified_literal_domain.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  subtype Bits_T is bit_vector(3 downto 0);
  signal Value : Bits_T;
begin
  Value <= Bits_T'("10UX");
end architecture;
)",
      "FSIM-ELAB-VHQUAL-003");
  reject(
      "vhdl_conversion_domain.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  signal Source : boolean;
  signal Value : bit;
begin
  Value <= bit(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");
  reject(
      "vhdl_conversion_nominal.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is (First_0, First_1);
  type Second_T is (Second_0, Second_1);
  signal Source : Second_T;
  signal Value : First_T;
begin
  Value <= First_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");
  reject(
      "vhdl_conversion_shape.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (natural range <>) of bit;
  subtype Descending_T is Bits_T(3 downto 0);
  subtype Ascending_T is Bits_T(0 to 3);
  signal Source : Ascending_T;
  signal Value : Descending_T;
begin
  Value <= Descending_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");
  reject(
      "vhdl_conversion_indefinite.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (natural range <>) of bit;
  signal Source : bit;
  signal Value : bit;
begin
  Value <= Bits_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-002");
  reject(
      "vhdl_conversion_context.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is (First_0, First_1);
  type Second_T is (Second_0, Second_1);
  signal Value : First_T;
begin
  Value <= Second_T(Second_0);
end architecture;
)",
      "FSIM-ELAB-VHCONV-004");
  reject(
      "vhdl_qualified_oversize.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Wide_T is array (natural range <>) of bit;
  subtype Bounded_Wide_T is Wide_T(64 downto 0);
  signal Value : bit;
begin
  Value <= Bounded_Wide_T'("0");
end architecture;
)",
      "FSIM-ELAB-VHQUAL-002");
  reject(
      "vhdl_qualified_record_nominal.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is record Flag : bit; end record;
  type Second_T is record Flag : bit; end record;
  signal Source : Second_T;
  signal Value : First_T;
begin
  Value <= First_T'(Source);
end architecture;
)",
      "FSIM-ELAB-VHQUAL-003");
  reject(
      "vhdl_conversion_record_nominal.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type First_T is record Flag : bit; end record;
  type Second_T is record Flag : bit; end record;
  signal Source : Second_T;
  signal Value : First_T;
begin
  Value <= First_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");
  reject(
      "vhdl_conversion_rank.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type One_Base_T is array (natural range <>) of bit;
  type Two_Base_T is array (natural range <>, natural range <>) of bit;
  subtype One_T is One_Base_T(0 to 3);
  subtype Two_T is Two_Base_T(0 to 1, 0 to 1);
  signal Source : Two_T;
  signal Value : One_T;
begin
  Value <= One_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");
  reject(
      "vhdl_conversion_bounds.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (natural range <>) of bit;
  subtype Zero_Based_T is Bits_T(0 to 3);
  subtype One_Based_T is Bits_T(1 to 4);
  signal Source : One_Based_T;
  signal Value : Zero_Based_T;
begin
  Value <= Zero_Based_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");
  reject(
      "vhdl_conversion_width.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_T is array (natural range <>) of bit;
  subtype Narrow_T is Bits_T(3 downto 0);
  subtype Wide_T is Bits_T(4 downto 0);
  signal Source : Wide_T;
  signal Value : Narrow_T;
begin
  Value <= Narrow_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");
  reject(
      "vhdl_conversion_element_type.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type Bits_Base_T is array (natural range <>) of bit;
  type Boolean_Base_T is array (natural range <>) of boolean;
  subtype Bits_T is Bits_Base_T(0 to 3);
  subtype Booleans_T is Boolean_Base_T(0 to 3);
  signal Source : Booleans_T;
  signal Value : Bits_T;
begin
  Value <= Bits_T(Source);
end architecture;
)",
      "FSIM-ELAB-VHCONV-003");

  const auto reject_at_runtime = [](
      const std::string_view filename,
      const std::string_view source) {
    const auto invalid = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto runtime_elaborated = fsim::elaboration::elaborate(
        invalid.design, "vhdl:work.invalid(rtl)");
    assert(runtime_elaborated.ok());
    bool rejected = false;
    try {
      auto runtime_interpreter =
          runtime_elaborated.design->create_interpreter();
      static_cast<void>(runtime_interpreter->run());
    } catch (const std::exception& error) {
      rejected = std::string_view{error.what()}.find(
          "VHDL integer subtype range check failed")
          != std::string_view::npos;
    }
    assert(rejected);
  };
  reject_at_runtime(
      "vhdl_qualified_integer_constraint.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  subtype Small_T is integer range 1 to 3;
  signal Value : Small_T;
begin
  Value <= Small_T'(4);
end architecture;
)");
  reject_at_runtime(
      "vhdl_conversion_enumeration_constraint.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  type State_T is (Idle, Run, Done);
  subtype Active_T is State_T range Run to Done;
  signal Value : Active_T;
begin
  Value <= Active_T(State_T'(Idle));
end architecture;
)");
}

}  // namespace fsim::tests::elaboration
