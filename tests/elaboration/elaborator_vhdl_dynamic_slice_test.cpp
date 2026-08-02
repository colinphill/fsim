// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_dynamic_slices() {
  const auto source = fsim::frontend::parse_text(
      "dynamic_slices.vhd",
      R"(
entity Dynamic_Slices is
  port (
    Descending_Read : out std_logic_vector(3 downto 0);
    Descending_Write : out std_logic_vector(7 downto 0);
    Ascending_Read : out std_logic_vector(0 to 3);
    Ascending_Write : out std_logic_vector(0 to 7);
    Chained_Read : out std_logic_vector(3 downto 0);
    Chained_Write : out std_logic_vector(7 downto 0)
  );
end entity;

architecture Rtl of Dynamic_Slices is
  type Packet_T is record
    Data : std_logic_vector(7 downto 0);
    Valid : boolean;
  end record;
begin
  Worker : process
    variable Descending_Source : std_logic_vector(7 downto 0);
    variable Descending_Target : std_logic_vector(7 downto 0);
    variable Descending_Selected : std_logic_vector(3 downto 0);
    variable Ascending_Source : std_logic_vector(0 to 7);
    variable Ascending_Target : std_logic_vector(0 to 7);
    variable Ascending_Selected : std_logic_vector(0 to 3);
    variable Packet : Packet_T;
    variable Left_Bound : integer;
    variable Right_Bound : integer;
  begin
    Descending_Source := "10XZ0110";
    Descending_Target := "00000000";
    Left_Bound := 6;
    Right_Bound := 3;
    Descending_Selected :=
      Descending_Source(Left_Bound downto Right_Bound);
    Descending_Read <= Descending_Selected;
    Descending_Target(Left_Bound downto Right_Bound) :=
      Descending_Selected;
    Descending_Write <= Descending_Target;

    Ascending_Source := "10XZ0110";
    Ascending_Target := "00000000";
    Left_Bound := 1;
    Right_Bound := 4;
    Ascending_Selected := Ascending_Source(Left_Bound to Right_Bound);
    Ascending_Read <= Ascending_Selected;
    Ascending_Target(Left_Bound to Right_Bound) := Ascending_Selected;
    Ascending_Write <= Ascending_Target;

    Packet := (Data => "10XZ0110", Valid => true);
    Left_Bound := 6;
    Right_Bound := 3;
    Descending_Selected := Packet.Data(Left_Bound downto Right_Bound);
    Chained_Read <= Descending_Selected;
    Packet.Data(Left_Bound downto Right_Bound) := "Z10X";
    Chained_Write <= Packet.Data;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(source.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      source.design, "vhdl:work.dynamic_slices(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto& operations = elaborated.design->processes().front().operations;
  assert(std::count_if(
             operations.begin(), operations.end(), [](const auto& operation) {
               return fsim::runtime::simir::operation_holds<
                   fsim::runtime::simir::DynamicPartSelect>(operation);
             })
         == 3);
  assert(std::count_if(
             operations.begin(), operations.end(), [](const auto& operation) {
               return fsim::runtime::simir::operation_holds<
                   fsim::runtime::simir::DynamicPartInsert>(operation);
             })
         == 3);
  auto interpreter = elaborated.design->create_interpreter();
  assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
  for (const auto& [name, expected] :
       std::array{
           std::pair{"descending_read", "0XZ0"},
           std::pair{"descending_write", "00XZ0000"},
           std::pair{"ascending_read", "0XZ0"},
           std::pair{"ascending_write", "00XZ0000"},
           std::pair{"chained_read", "0XZ0"},
           std::pair{"chained_write", "1Z10X110"}}) {
    const auto signal = elaborated.design->find_signal(name);
    assert(signal);
    assert(interpreter->signal_value(*signal).to_msb_string() == expected);
  }

  const auto invalid = fsim::frontend::parse_text(
      "invalid_dynamic_slices.vhd",
      R"(
entity Bad_Direction is end entity;
architecture Rtl of Bad_Direction is
  signal Value : std_logic_vector(0 to 7);
  signal Result : std_logic_vector(3 downto 0);
  signal Left_Bound : integer;
  signal Right_Bound : integer;
begin
  Result <= Value(Left_Bound downto Right_Bound);
end architecture;

entity Bad_Bound is end entity;
architecture Rtl of Bad_Bound is
  signal Value : std_logic_vector(7 downto 0);
  signal Result : std_logic_vector(3 downto 0);
  signal Bound : std_logic;
begin
  Result <= Value(6 downto Bound);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid.ok());
  const auto bad_direction = fsim::elaboration::elaborate(
      invalid.design, "vhdl:work.bad_direction(rtl)");
  assert(!bad_direction.ok());
  assert(has_diagnostic(bad_direction, "FSIM-ELAB-VHSLICE-001"));
  const auto bad_bound = fsim::elaboration::elaborate(
      invalid.design, "vhdl:work.bad_bound(rtl)");
  assert(!bad_bound.ok());
  assert(has_diagnostic(bad_bound, "FSIM-ELAB-VHSLICE-002"));

  const auto failing = fsim::frontend::parse_text(
      "failing_dynamic_slice.vhd",
      R"(
entity Failing_Dynamic_Slice is end entity;
architecture Rtl of Failing_Dynamic_Slice is
begin
  Worker : process
    variable Value : std_logic_vector(7 downto 0);
    variable Result : std_logic_vector(3 downto 0);
    variable Left_Bound : integer;
    variable Right_Bound : integer;
  begin
    Left_Bound := 6;
    Right_Bound := 4;
    Result := Value(Left_Bound downto Right_Bound);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(failing.ok());
  const auto elaborated_failing = fsim::elaboration::elaborate(
      failing.design, "vhdl:work.failing_dynamic_slice(rtl)");
  assert(elaborated_failing.ok());
  auto failing_interpreter =
      elaborated_failing.design->create_interpreter();
  try {
    (void)failing_interpreter->run();
    assert(false && "dynamic VHDL slice length failure was not reported");
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    assert(std::string_view{error.what()}.find(
               "VHDL integer subtype range check failed")
           != std::string_view::npos);
  }
}

}  // namespace fsim::tests::elaboration
