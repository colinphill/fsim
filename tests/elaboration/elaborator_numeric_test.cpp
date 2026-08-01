// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_numeric_and_system_function_lowering() {
const auto vhdl_signed_arithmetic =
        fsim::frontend::parse_text(
            "vhdl_signed_arithmetic.vhd",
            R"(
entity vhdl_signed_arithmetic is
  port (
    lhs : in signed(7 downto 0);
    rhs : in signed(7 downto 0);
    sum : out signed(7 downto 0);
    difference : out signed(7 downto 0);
    product : out signed(7 downto 0);
    quotient : out signed(7 downto 0);
    remainder : out signed(7 downto 0);
    modulo : out signed(7 downto 0);
    less : out std_logic;
    shifted_left : out signed(7 downto 0);
    shifted_right : out signed(7 downto 0);
    shifted_arithmetic : out signed(7 downto 0);
    shifted_arithmetic_left : out signed(7 downto 0);
    rotated_left : out signed(7 downto 0);
    rotated_right : out signed(7 downto 0);
    reversed_logical_left : out signed(7 downto 0);
    reversed_arithmetic_right : out signed(7 downto 0);
    reversed_rotate_left : out signed(7 downto 0);
    absolute : out signed(7 downto 0)
  );
end entity;

architecture rtl of vhdl_signed_arithmetic is
begin
  calculate: process(lhs, rhs)
  begin
    sum <= lhs + rhs;
    difference <= lhs - rhs;
    product <= lhs * rhs;
    quotient <= lhs / rhs;
    remainder <= lhs rem rhs;
    modulo <= lhs mod rhs;
    less <= lhs < rhs;
    shifted_left <= lhs sll 1;
    shifted_right <= lhs srl 1;
    shifted_arithmetic <= lhs sra 1;
    shifted_arithmetic_left <= lhs sla 1;
    rotated_left <= lhs rol 1;
    rotated_right <= lhs ror 1;
    reversed_logical_left <= lhs sll -1;
    reversed_arithmetic_right <= lhs sra -1;
    reversed_rotate_left <= lhs rol -1;
    absolute <= abs lhs;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_signed_arithmetic.ok());
    const auto elaborated_vhdl_signed_arithmetic =
        fsim::elaboration::elaborate(
            vhdl_signed_arithmetic.design,
            "vhdl:work.vhdl_signed_arithmetic(rtl)");
    if (!elaborated_vhdl_signed_arithmetic.ok()) {
      for (const auto& diagnostic :
           elaborated_vhdl_signed_arithmetic.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_vhdl_signed_arithmetic.ok());
    const auto vhdl_signed_lhs =
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "lhs");
    const auto vhdl_signed_rhs =
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "rhs");
    const std::array vhdl_signed_outputs{
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "sum"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "difference"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "product"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "quotient"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "remainder"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "modulo"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "less"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_right"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_arithmetic"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_arithmetic_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "rotated_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "rotated_right"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "reversed_logical_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "reversed_arithmetic_right"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "reversed_rotate_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "absolute")};
    assert(vhdl_signed_lhs && vhdl_signed_rhs);
    assert(std::ranges::all_of(
        vhdl_signed_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto vhdl_signed_interpreter =
        elaborated_vhdl_signed_arithmetic.design
            ->create_interpreter();
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "11111011"));
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_rhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "00000011"));
    (void)vhdl_signed_interpreter->run();
    const std::array<std::string_view, 17>
        expected_vhdl_signed{
            "11111110",
            "11111000",
            "11110001",
            "11111111",
            "11111110",
            "00000001",
            "1",
            "11110110",
            "01111101",
            "11111101",
            "11110111",
            "11110111",
            "11111101",
            "01111101",
            "11110111",
            "11111101",
            "00000101"};
    for (std::size_t index = 0;
         index < vhdl_signed_outputs.size(); ++index) {
      assert(
          vhdl_signed_interpreter
              ->signal_value(*vhdl_signed_outputs[index])
              .to_msb_string()
          == expected_vhdl_signed[index]);
    }
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "10X01000"));
    (void)vhdl_signed_interpreter->run();
    const std::array<std::string_view, 9>
        expected_unknown_shifts{
            "0X010000",
            "010X0100",
            "110X0100",
            "0X010000",
            "0X010001",
            "010X0100",
            "010X0100",
            "0X010000",
            "010X0100"};
    for (std::size_t index = 0;
         index < expected_unknown_shifts.size(); ++index) {
      assert(
          vhdl_signed_interpreter
              ->signal_value(*vhdl_signed_outputs[index + 7])
              .to_msb_string()
          == expected_unknown_shifts[index]);
    }
    assert(
        vhdl_signed_interpreter
            ->signal_value(*vhdl_signed_outputs.back())
            .to_msb_string()
        == "XXXXXXXX");
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "10000000"));
    (void)vhdl_signed_interpreter->run();
    assert(
        vhdl_signed_interpreter
            ->signal_value(*vhdl_signed_outputs.back())
            .to_msb_string()
        == "10000000");
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "00000101"));
    (void)vhdl_signed_interpreter->run();
    assert(
        vhdl_signed_interpreter
            ->signal_value(*vhdl_signed_outputs.back())
            .to_msb_string()
        == "00000101");

    const auto dynamic_vhdl_shift =
        fsim::frontend::parse_text(
            "dynamic_vhdl_shift.vhd",
            R"(
entity dynamic_vhdl_shift is
  port (
    value : in std_logic_vector(7 downto 0);
    count : in integer;
    logical_left : out std_logic_vector(7 downto 0);
    logical_right : out std_logic_vector(7 downto 0);
    arithmetic_left : out std_logic_vector(7 downto 0);
    arithmetic_right : out std_logic_vector(7 downto 0);
    rotate_left : out std_logic_vector(7 downto 0);
    rotate_right : out std_logic_vector(7 downto 0);
    integer_result : out integer
  );
end entity;

architecture rtl of dynamic_vhdl_shift is
  signal natural_value : natural;
  signal positive_value : positive;
  signal constrained_value : integer range -3 to 4;
  signal reverse_value : integer range 3 downto -2;
begin
  calculate: process(value, count)
    variable adjusted : integer := -1;
    variable bounded : integer range -2 to 2 := -2;
  begin
    adjusted := count + 1;
    bounded := adjusted mod 3;
    integer_result <= abs adjusted;
    logical_left <= value sll count;
    logical_right <= value srl count;
    arithmetic_left <= value sla count;
    arithmetic_right <= value sra count;
    rotate_left <= value rol count;
    rotate_right <= value ror count;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(dynamic_vhdl_shift.ok());
    const auto elaborated_dynamic_vhdl_shift =
        fsim::elaboration::elaborate(
            dynamic_vhdl_shift.design,
            "vhdl:work.dynamic_vhdl_shift(rtl)");
    if (!elaborated_dynamic_vhdl_shift.ok()) {
      for (const auto& diagnostic :
           elaborated_dynamic_vhdl_shift.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_dynamic_vhdl_shift.ok());
    const auto& dynamic_design =
        *elaborated_dynamic_vhdl_shift.design;
    const auto dynamic_count =
        dynamic_design.find_signal("count");
    const auto dynamic_value =
        dynamic_design.find_signal("value");
    const auto natural_value =
        dynamic_design.find_signal("natural_value");
    const auto positive_value =
        dynamic_design.find_signal("positive_value");
    const auto constrained_value =
        dynamic_design.find_signal("constrained_value");
    const auto reverse_value =
        dynamic_design.find_signal("reverse_value");
    const std::array dynamic_outputs{
        dynamic_design.find_signal("logical_left"),
        dynamic_design.find_signal("logical_right"),
        dynamic_design.find_signal("arithmetic_left"),
        dynamic_design.find_signal("arithmetic_right"),
        dynamic_design.find_signal("rotate_left"),
        dynamic_design.find_signal("rotate_right"),
        dynamic_design.find_signal("integer_result")};
    assert(
        dynamic_count && dynamic_value && natural_value
        && positive_value && constrained_value && reverse_value);
    assert(std::ranges::all_of(
        dynamic_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    const auto& count_info =
        dynamic_design.signals().at(*dynamic_count);
    assert(count_info.width == 32);
    assert(
        count_info.source_domain
        == fsim::frontend::ValueDomain::Integer);
    assert(count_info.is_signed);
    const auto& natural_info =
        dynamic_design.signals().at(*natural_value);
    const auto& positive_info =
        dynamic_design.signals().at(*positive_value);
    const auto& constrained_info =
        dynamic_design.signals().at(*constrained_value);
    const auto& reverse_info =
        dynamic_design.signals().at(*reverse_value);
    assert(
        natural_info.integer_range
        && natural_info.integer_range->left == 0
        && positive_info.integer_range
        && positive_info.integer_range->left == 1
        && constrained_info.integer_range
        && constrained_info.integer_range->left == -3
        && constrained_info.integer_range->right == 4
        && reverse_info.integer_range
        && reverse_info.integer_range->left == 3
        && reverse_info.integer_range->right == -2
        && reverse_info.integer_range->descending);
    assert(dynamic_design.processes().size() == 1);
    const auto signed_shift_count = std::ranges::count_if(
        dynamic_design.processes().front().operations,
        [](const auto& operation) {
          const auto* shift =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Shift>(
                  &operation);
          return shift != nullptr && shift->signed_amount;
        });
    assert(signed_shift_count == 6);
    assert(
        std::ranges::count_if(
            dynamic_design.processes().front().operations,
            [](const auto& operation) {
              return fsim::runtime::simir::operation_holds<
                  fsim::runtime::simir::IntegerCheck>(
                  operation);
            })
        >= 4);
    assert(std::ranges::any_of(
        dynamic_design.processes().front().operations,
        [](const auto& operation) {
          const auto* binary =
              fsim::runtime::simir::operation_get_if<
                  fsim::runtime::simir::IntegerBinary>(
                  &operation);
          return binary != nullptr
              && binary->operation
                  == fsim::runtime::simir::
                      IntegerBinaryOperator::add;
        }));
    assert(std::ranges::any_of(
        dynamic_design.processes().front().operations,
        [](const auto& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::IntegerUnary>(
              operation);
        }));
    assert(
        dynamic_design.processes().front().debug_locals.size() == 2
        && dynamic_design.processes().front()
               .debug_locals[1].integer_lower
        && *dynamic_design.processes().front()
                .debug_locals[1].integer_lower
            == -2
        && dynamic_design.processes().front()
               .debug_locals[1].integer_upper
        && *dynamic_design.processes().front()
                .debug_locals[1].integer_upper
            == 2);

    auto dynamic_interpreter =
        dynamic_design.create_interpreter();
    assert(
        dynamic_interpreter
            ->signal_value(*natural_value)
            .to_msb_string()
        == "00000000000000000000000000000000");
    assert(
        dynamic_interpreter
            ->signal_value(*positive_value)
            .to_msb_string()
        == "00000000000000000000000000000001");
    assert(
        dynamic_interpreter
            ->signal_value(*constrained_value)
            .to_msb_string()
        == "11111111111111111111111111111101");
    assert(
        dynamic_interpreter
            ->signal_value(*reverse_value)
            .to_msb_string()
        == "00000000000000000000000000000011");
    assert(
        dynamic_interpreter
            ->signal_value(*dynamic_count)
            .to_msb_string()
        == "10000000000000000000000000000000");
    dynamic_interpreter->deposit_signal(
        *dynamic_value,
        fsim::runtime::PackedLogic4::from_msb_string(
            "10X0000Z"));
    dynamic_interpreter->deposit_signal(
        *dynamic_count,
        fsim::runtime::PackedLogic4::from_msb_string(
            "11111111111111111111111111111111"));
    (void)dynamic_interpreter->run();
    const std::array<std::string_view, 7>
        negative_expected{
            "010X0000",
            "0X0000Z0",
            "110X0000",
            "0X0000ZZ",
            "Z10X0000",
            "0X0000Z1",
            "00000000000000000000000000000000"};
    for (std::size_t index = 0;
         index < dynamic_outputs.size(); ++index) {
      assert(
          dynamic_interpreter
              ->signal_value(*dynamic_outputs[index])
              .to_msb_string()
          == negative_expected[index]);
    }

    const auto vhdl_subtype_source =
        fsim::frontend::parse_text(
            "vhdl_subtype_execution.vhd",
            R"(
package Subtype_Types is
  subtype Count_Base_T is natural range 0 to 15;
  subtype Count_T is Count_Base_T range 2 to 9;
  constant Default_Count : Count_T := 5;
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  subtype Packet_Alias_T is Packet_T;
end package;

use work.subtype_types.all;
entity subtype_execution is
  generic (
    Width : natural := 4;
    Initial_Count : Count_T := Default_Count
  );
  subtype Entity_Word_T is std_logic_vector(Width - 1 downto 0);
end entity;

use work.subtype_types.all;
architecture rtl of subtype_execution is
  subtype Local_Count_T is Count_T range 4 to 7;
  subtype Local_Packet_T is Packet_Alias_T;
  signal source : Entity_Word_T;
  signal result : Entity_Word_T;
  signal count : Local_Count_T;
  signal count_result : Count_T;
  signal packet : Local_Packet_T;
begin
  drive : process
    variable local_count : Local_Count_T := Initial_Count;
    variable local_packet : Local_Packet_T :=
      (Valid => true, Data => "ULH-");
  begin
    source <= "10Z-";
    count <= local_count;
    packet <= local_packet;
    wait;
  end process;

  result <= source;
  count_result <= count;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_subtype_source.ok());
    const auto vhdl_subtype_design =
        fsim::elaboration::elaborate(
            vhdl_subtype_source.design,
            "vhdl:work.subtype_execution(rtl)");
    if (!vhdl_subtype_design.ok()) {
      for (const auto& diagnostic :
           vhdl_subtype_design.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << " at "
                  << diagnostic.span.begin.line << ":"
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(vhdl_subtype_design.ok());
    const auto subtype_source_signal =
        vhdl_subtype_design.design->find_signal("source");
    const auto subtype_result_signal =
        vhdl_subtype_design.design->find_signal("result");
    const auto subtype_count_signal =
        vhdl_subtype_design.design->find_signal("count");
    const auto subtype_count_result_signal =
        vhdl_subtype_design.design->find_signal(
            "count_result");
    const auto subtype_packet_signal =
        vhdl_subtype_design.design->find_signal("packet");
    assert(
        subtype_source_signal && subtype_result_signal
        && subtype_count_signal
        && subtype_count_result_signal
        && subtype_packet_signal);
    const auto& subtype_source_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_source_signal);
    const auto& subtype_count_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_count_signal);
    const auto& subtype_count_result_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_count_result_signal);
    const auto& subtype_packet_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_packet_signal);
    assert(
        subtype_source_info.width == 4
        && subtype_source_info.source_domain
            == fsim::frontend::ValueDomain::Logic9
        && subtype_count_info.width == 32
        && subtype_count_info.integer_range
        && subtype_count_info.integer_range->left == 4
        && subtype_count_info.integer_range->right == 7
        && subtype_count_result_info.integer_range
        && subtype_count_result_info.integer_range->left == 2
        && subtype_count_result_info.integer_range->right == 9
        && subtype_packet_info.width == 5
        && subtype_packet_info.packed_members.size() == 2);
    assert(
        std::ranges::any_of(
            vhdl_subtype_design.design->processes(),
            [](const auto& process) {
                return std::ranges::any_of(
                    process.operations,
                    [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::IntegerCheck>(
                            operation);
                    });
            }));
    auto subtype_interpreter =
        vhdl_subtype_design.design->create_interpreter();
    assert(
        subtype_interpreter
            ->signal_value(*subtype_count_signal)
            .to_msb_string()
        == "00000000000000000000000000000100");
    const auto subtype_run = subtype_interpreter->run();
    assert(
        subtype_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        subtype_interpreter
            ->signal_value(*subtype_source_signal)
            .to_msb_string()
        == "10Z-");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_result_signal)
            .to_msb_string()
        == "10Z-");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_count_signal)
            .to_msb_string()
        == "00000000000000000000000000000101");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_count_result_signal)
            .to_msb_string()
        == "00000000000000000000000000000101");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_packet_signal)
            .to_msb_string()
        == "ULH-1");

    const auto invalid_vhdl_subtypes =
        fsim::frontend::parse_text(
            "invalid_vhdl_subtypes.vhd",
            R"(
package Invalid_Subtype_Types is
  subtype Imported_Count_T is natural range 2 to 9;
  subtype Imported_Vector_T is bit_vector(3 downto 0);
  constant Bad_Constant : Imported_Count_T := 10;
end package;

use work.invalid_subtype_types.all;
entity invalid_vhdl_subtypes is
  generic (
    Bad_Generic : Imported_Count_T := 10;
    Bad_Vector_Generic : Imported_Vector_T := "0000"
  );
  subtype Later_Port_T is bit;
  port (Bad_Port : in Later_Port_T);
end entity;

use work.invalid_subtype_types.all;
architecture rtl of invalid_vhdl_subtypes is
  subtype Bad_Scalar_T is boolean range 0 to 1;
  subtype Count_Base_T is integer range 0 to 3;
  subtype Bad_Count_T is Count_Base_T range 0 to 4;
  subtype Null_Count_T is integer range 3 to 2;
  subtype Bad_Packed_T is bit(3 downto 0);
  subtype Vector_T is bit_vector(3 downto 0);
  subtype Reconstraint_T is Vector_T(1 downto 0);
  subtype Unknown_T is Missing_T;
  subtype Cycle_A_T is Cycle_B_T;
  subtype Cycle_B_T is Cycle_A_T;
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_subtypes.ok());
    const auto rejected_vhdl_subtypes =
        fsim::elaboration::elaborate(
            invalid_vhdl_subtypes.design,
            "vhdl:work.invalid_vhdl_subtypes(rtl)");
    assert(!rejected_vhdl_subtypes.ok());
    const bool all_subtype_diagnostics =
        has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-001")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-002")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-003")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-004")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-INTEGER-002")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-PKG-006")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-GENERIC-008")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-GENERIC-010")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHTYPE-001")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHTYPE-002");
    if (!all_subtype_diagnostics) {
      for (const auto& diagnostic :
           rejected_vhdl_subtypes.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(all_subtype_diagnostics);
    assert(std::ranges::any_of(
        rejected_vhdl_subtypes.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHTYPE-001"
                && diagnostic.message.find("later_port_t")
                    != std::string::npos;
        }));

    const auto vhdl_enumeration_source =
        fsim::frontend::parse_text(
            "vhdl_enumeration_execution.vhd",
            R"(
package State_Types is
  type State_T is (Idle, Load, Running, Done);
  subtype State_Alias_T is State_T;
  constant Initial_State : State_T := Load;
  type Symbol_T is ('A', 'B', 'C');
end package;

use work.state_types.all;
entity enumeration_execution is
  generic (Reset_State : State_T := Initial_State);
end entity;

use work.state_types.all;
architecture rtl of enumeration_execution is
  signal current : State_Alias_T;
  signal selected : State_T;
  signal symbol : Symbol_T;
  signal equal_result : boolean;
  signal ordered_result : boolean;
  signal position_result : integer;
  signal successor_result : State_T;
begin
  drive : process
    variable local_state : State_T := Reset_State;
    variable local_symbol : Symbol_T := 'A';
  begin
    current <= local_state;
    local_state := Running;
    case local_state is
      when Running =>
        selected <= state_types.Done;
      when others =>
        selected <= Idle;
    end case;
    local_symbol := 'C';
    symbol <= local_symbol;
    wait;
  end process;

  equal_result <= current /= Done;
  ordered_result <= current < Done;
  position_result <= State_T'pos(current);
  successor_result <= State_T'succ(current);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_enumeration_source.ok());
    const auto vhdl_enumeration_design =
        fsim::elaboration::elaborate(
            vhdl_enumeration_source.design,
            "vhdl:work.enumeration_execution(rtl)");
    if (!vhdl_enumeration_design.ok()) {
      for (const auto& diagnostic :
           vhdl_enumeration_design.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << " at "
                  << diagnostic.span.begin.line << ":"
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(vhdl_enumeration_design.ok());
    const auto enum_current =
        vhdl_enumeration_design.design->find_signal("current");
    const auto enum_selected =
        vhdl_enumeration_design.design->find_signal("selected");
    const auto enum_symbol =
        vhdl_enumeration_design.design->find_signal("symbol");
    const auto enum_equal =
        vhdl_enumeration_design.design->find_signal(
            "equal_result");
    const auto enum_ordered =
        vhdl_enumeration_design.design->find_signal(
            "ordered_result");
    const auto enum_position =
        vhdl_enumeration_design.design->find_signal(
            "position_result");
    const auto enum_successor =
        vhdl_enumeration_design.design->find_signal(
            "successor_result");
    assert(
        enum_current && enum_selected && enum_symbol
        && enum_equal && enum_ordered
        && enum_position && enum_successor);
    const auto& enum_info =
        vhdl_enumeration_design.design->signals().at(
            *enum_current);
    const std::vector<std::string> expected_state_literals{
        "idle", "load", "running", "done"};
    const std::vector<std::string> expected_symbol_literals{
        "'A'", "'B'", "'C'"};
    assert(
        enum_info.width == 2
        && enum_info.source_domain
            == fsim::frontend::ValueDomain::Bit2
        && !enum_info.nominal_type.empty()
        && enum_info.enumeration_literals
            == expected_state_literals);
    const auto enum_process = std::find_if(
        vhdl_enumeration_design.design->processes().begin(),
        vhdl_enumeration_design.design->processes().end(),
        [](const auto& process) {
            return process.name.ends_with(".drive");
        });
    assert(
        enum_process
            != vhdl_enumeration_design.design->processes().end()
        && enum_process->debug_locals.size() == 2
        && enum_process->debug_locals[0].enumeration_literals
            == enum_info.enumeration_literals
        && enum_process->debug_locals[1].enumeration_literals
            == expected_symbol_literals);
    auto enum_interpreter =
        vhdl_enumeration_design.design->create_interpreter();
    assert(
        enum_interpreter->signal_value(*enum_current)
            .to_msb_string()
        == "00");
    const auto enum_run = enum_interpreter->run();
    assert(enum_run.status == fsim::runtime::RunStatus::completed);
    assert(
        enum_interpreter->signal_value(*enum_current)
                .to_msb_string()
            == "01"
        && enum_interpreter->signal_value(*enum_selected)
                .to_msb_string()
            == "11"
        && enum_interpreter->signal_value(*enum_symbol)
                .to_msb_string()
            == "10"
        && enum_interpreter->signal_value(*enum_equal)
                .to_msb_string()
            == "1"
        && enum_interpreter->signal_value(*enum_ordered)
                .to_msb_string()
            == "1"
        && enum_interpreter->signal_value(*enum_position)
                .to_msb_string()
            == "00000000000000000000000000000001"
        && enum_interpreter->signal_value(*enum_successor)
                .to_msb_string()
            == "10");
    assert(
        std::ranges::any_of(
            vhdl_enumeration_design.design->processes(),
            [](const auto& process) {
                return std::ranges::any_of(
                    process.operations,
                    [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                                   fsim::runtime::simir::IntegerCheck>(
                                   operation)
                            || fsim::runtime::simir::operation_holds<
                                   fsim::runtime::simir::IntegerBinary>(
                                   operation);
                    });
            }));

    const auto invalid_vhdl_enumerations =
        fsim::frontend::parse_text(
            "invalid_vhdl_enumerations.vhd",
            R"(
entity invalid_vhdl_enumerations is
end entity;
architecture rtl of invalid_vhdl_enumerations is
  type First_T is (Low, High);
  type Second_T is (Low, High);
  signal first : First_T;
  signal second : Second_T;
  signal position : integer;
begin
  first <= second;
  second <= Missing;
  first <= first + first;
  first <= First_T'val(2);
  position <= First_T'pos(1);
  first <= First_T'val(A);
  first <= First_T'left(A);
  first <= first'right;
  first <= Second_T'left;
  position <= First_T'pos(first);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_enumerations.ok());
    const auto rejected_vhdl_enumerations =
        fsim::elaboration::elaborate(
            invalid_vhdl_enumerations.design,
            "vhdl:work.invalid_vhdl_enumerations(rtl)");
    assert(
        !rejected_vhdl_enumerations.ok()
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUM-001")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUM-002")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUM-003")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUMATTR-001")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUMATTR-002"));
    assert(std::ranges::any_of(
        rejected_vhdl_enumerations.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHENUMATTR-001"
                && diagnostic.message.find(
                       "requires a value of enumeration type")
                    != std::string::npos;
        }));
    assert(std::ranges::any_of(
        rejected_vhdl_enumerations.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHENUMATTR-001"
                && diagnostic.message.find(
                       "'left on enumeration type")
                    != std::string::npos
                && diagnostic.message.find(
                       "requires 0 arguments")
                    != std::string::npos;
        }));
    assert(std::ranges::any_of(
        rejected_vhdl_enumerations.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHENUMATTR-001"
                && diagnostic.message.find(
                       "requires an integer-family argument")
                    != std::string::npos;
        }));

    const auto constrained_vhdl_enumerations =
        fsim::frontend::parse_text(
            "constrained_vhdl_enumerations.vhd",
            R"(
package Constrained_State_Types is
  type State_T is (Idle, Load, Running, Done);
  subtype Active_T is State_T range Load to Done;
  subtype Reverse_T is State_T range Done downto Load;
  subtype Narrow_T is Active_T range Running to Done;
  constant Active_Default : Active_T := Load;
end package;

use work.constrained_state_types.all;
entity constrained_enumeration_execution is
  generic (Reset_State : Active_T := Active_Default);
end entity;

use work.constrained_state_types.all;
architecture rtl of constrained_enumeration_execution is
  signal active_default : Active_T;
  signal reverse_default : Reverse_T;
  signal next_value : Active_T;
  signal right_value : Reverse_T;
  signal left_value : Reverse_T;
  signal low_value : Reverse_T;
  signal high_value : Reverse_T;
  signal length_value : integer;
  signal ascending_value : boolean;
  signal position_value : integer;
  signal converted_value : Reverse_T;
begin
  next_value <= Active_T'succ(active_default);
  right_value <= Reverse_T'rightof(reverse_default);
  left_value <= Reverse_T'leftof(Load);
  low_value <= Reverse_T'low;
  high_value <= Reverse_T'high;
  length_value <= Reverse_T'length;
  ascending_value <= Reverse_T'ascending;
  position_value <= Reverse_T'pos(Load);
  converted_value <= Reverse_T'val(2);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(constrained_vhdl_enumerations.ok());
    const auto constrained_enumeration_design =
        fsim::elaboration::elaborate(
            constrained_vhdl_enumerations.design,
            "vhdl:work.constrained_enumeration_execution(rtl)");
    if (!constrained_enumeration_design.ok()) {
      for (const auto& diagnostic :
           constrained_enumeration_design.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(constrained_enumeration_design.ok());
    const auto active_default =
        constrained_enumeration_design.design->find_signal(
            "active_default");
    const auto reverse_default =
        constrained_enumeration_design.design->find_signal(
            "reverse_default");
    const auto next_value =
        constrained_enumeration_design.design->find_signal(
            "next_value");
    const auto right_value =
        constrained_enumeration_design.design->find_signal(
            "right_value");
    const auto left_value =
        constrained_enumeration_design.design->find_signal(
            "left_value");
    const auto low_value =
        constrained_enumeration_design.design->find_signal(
            "low_value");
    const auto high_value =
        constrained_enumeration_design.design->find_signal(
            "high_value");
    const auto length_value =
        constrained_enumeration_design.design->find_signal(
            "length_value");
    const auto ascending_value =
        constrained_enumeration_design.design->find_signal(
            "ascending_value");
    const auto position_value =
        constrained_enumeration_design.design->find_signal(
            "position_value");
    const auto converted_value =
        constrained_enumeration_design.design->find_signal(
            "converted_value");
    assert(
        active_default && reverse_default && next_value
        && right_value && left_value && low_value && high_value
        && length_value && ascending_value && position_value
        && converted_value);
    const auto& active_info =
        constrained_enumeration_design.design->signals().at(
            *active_default);
    const auto& reverse_enum_info =
        constrained_enumeration_design.design->signals().at(
            *reverse_default);
    assert(
        active_info.enumeration_range
        && active_info.enumeration_range->left == 1
        && active_info.enumeration_range->right == 3
        && !active_info.enumeration_range->descending
        && reverse_enum_info.enumeration_range
        && reverse_enum_info.enumeration_range->left == 3
        && reverse_enum_info.enumeration_range->right == 1
        && reverse_enum_info.enumeration_range->descending);
    auto constrained_enumeration_interpreter =
        constrained_enumeration_design.design->create_interpreter();
    assert(
        constrained_enumeration_interpreter
                ->signal_value(*active_default)
                .to_msb_string()
            == "01"
        && constrained_enumeration_interpreter
                ->signal_value(*reverse_default)
                .to_msb_string()
            == "11");
    const auto constrained_enumeration_run =
        constrained_enumeration_interpreter->run();
    assert(
        constrained_enumeration_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        constrained_enumeration_interpreter
                ->signal_value(*next_value)
                .to_msb_string()
            == "10"
        && constrained_enumeration_interpreter
                ->signal_value(*right_value)
                .to_msb_string()
            == "10"
        && constrained_enumeration_interpreter
                ->signal_value(*left_value)
                .to_msb_string()
            == "10"
        && constrained_enumeration_interpreter
                ->signal_value(*low_value)
                .to_msb_string()
            == "01"
        && constrained_enumeration_interpreter
                ->signal_value(*high_value)
                .to_msb_string()
            == "11"
        && constrained_enumeration_interpreter
                ->signal_value(*length_value)
                .to_msb_string()
            == "00000000000000000000000000000011"
        && constrained_enumeration_interpreter
                ->signal_value(*ascending_value)
                .to_msb_string()
            == "0"
        && constrained_enumeration_interpreter
                ->signal_value(*position_value)
                .to_msb_string()
            == "00000000000000000000000000000001"
        && constrained_enumeration_interpreter
                ->signal_value(*converted_value)
                .to_msb_string()
            == "10");
    assert(std::ranges::any_of(
        constrained_enumeration_design.design->processes(),
        [](const auto& process) {
            return std::ranges::any_of(
                process.operations,
                [](const auto& operation) {
                    return fsim::runtime::simir::operation_holds<
                        fsim::runtime::simir::IntegerCheck>(
                        operation);
                });
        }));

    const auto invalid_constrained_vhdl_enumerations =
        fsim::frontend::parse_text(
            "invalid_constrained_vhdl_enumerations.vhd",
            R"(
package Invalid_Constrained_State_Types is
  type State_T is (Idle, Load, Running, Done);
  type Other_T is (Cold, Hot);
  subtype Active_T is State_T range Load to Done;
  subtype Missing_T is State_T range Missing to Done;
  subtype Wrong_T is State_T range Cold to Done;
  subtype Null_T is State_T range Done to Load;
  subtype Outside_T is Active_T range Idle to Running;
  constant Bad_Constant : Active_T := Idle;
end package;

use work.invalid_constrained_state_types.all;
entity invalid_constrained_enumeration_execution is
end entity;
architecture rtl of invalid_constrained_enumeration_execution is
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_constrained_vhdl_enumerations.ok());
    const auto rejected_constrained_vhdl_enumerations =
        fsim::elaboration::elaborate(
            invalid_constrained_vhdl_enumerations.design,
            "vhdl:work.invalid_constrained_enumeration_execution(rtl)");
    assert(
        !rejected_constrained_vhdl_enumerations.ok()
        && has_diagnostic(
            rejected_constrained_vhdl_enumerations,
            "FSIM-ELAB-VHENUMRANGE-001")
        && has_diagnostic(
            rejected_constrained_vhdl_enumerations,
            "FSIM-ELAB-VHENUMRANGE-002")
        && has_diagnostic(
            rejected_constrained_vhdl_enumerations,
            "FSIM-ELAB-VHENUMRANGE-003")
        && has_diagnostic(
            rejected_constrained_vhdl_enumerations,
            "FSIM-ELAB-PKG-006"));

    const auto invalid_constrained_enumeration_values =
        fsim::frontend::parse_text(
            "invalid_constrained_enumeration_values.vhd",
            R"(
package Constrained_Value_Types is
  type State_T is (Idle, Load, Running, Done);
  subtype Active_T is State_T range Load to Done;
end package;

use work.constrained_value_types.all;
entity invalid_constrained_store is
end entity;
use work.constrained_value_types.all;
architecture rtl of invalid_constrained_store is
  signal target : Active_T;
begin
  target <= Idle;
end architecture;

use work.constrained_value_types.all;
entity constrained_generic_child is
  generic (Reset_State : Active_T := Load);
  port (Value : in Active_T);
end entity;
architecture rtl of constrained_generic_child is
begin
end architecture;

use work.constrained_value_types.all;
entity invalid_constrained_generic_default is
  generic (Reset_State : Active_T := Idle);
end entity;
architecture rtl of invalid_constrained_generic_default is
begin
end architecture;

use work.constrained_value_types.all;
entity invalid_constrained_generic_parent is
end entity;
use work.constrained_value_types.all;
architecture rtl of invalid_constrained_generic_parent is
  signal Actual : Active_T;
begin
  Child : entity work.constrained_generic_child(rtl)
    generic map (Reset_State => Idle)
    port map (Value => Actual);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    if (!invalid_constrained_enumeration_values.ok()) {
      for (const auto& diagnostic :
           invalid_constrained_enumeration_values.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << " at "
                  << diagnostic.span.begin.line << ":"
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(invalid_constrained_enumeration_values.ok());
    const auto rejected_constrained_enumeration_store =
        fsim::elaboration::elaborate(
            invalid_constrained_enumeration_values.design,
            "vhdl:work.invalid_constrained_store(rtl)");
    assert(
        !rejected_constrained_enumeration_store.ok()
        && has_diagnostic(
            rejected_constrained_enumeration_store,
            "FSIM-ELAB-VHENUMRANGE-004"));
    const auto rejected_constrained_enumeration_generic =
        fsim::elaboration::elaborate(
            invalid_constrained_enumeration_values.design,
            "vhdl:work.invalid_constrained_generic_parent(rtl)");
    assert(
        !rejected_constrained_enumeration_generic.ok()
        && has_diagnostic(
            rejected_constrained_enumeration_generic,
            "FSIM-ELAB-GENERIC-008"));
    const auto rejected_constrained_enumeration_default =
        fsim::elaboration::elaborate(
            invalid_constrained_enumeration_values.design,
            "vhdl:work.invalid_constrained_generic_default(rtl)");
    assert(
        !rejected_constrained_enumeration_default.ok()
        && has_diagnostic(
            rejected_constrained_enumeration_default,
            "FSIM-ELAB-GENERIC-008"));

    const auto enumeration_boundary_vhdl =
        fsim::frontend::parse_text(
            "enumeration_boundaries.vhd",
            R"(
package Enumeration_Boundary_Types is
  type First_T is (Low, High);
  type Second_T is (Low, High);
  subtype High_Only_T is First_T range High to High;
end package;

use work.enumeration_boundary_types.all;
entity Enumeration_Child is
  port (Value : in First_T);
end entity;
architecture rtl of enumeration_child is
begin
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Native_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_native_parent is
  signal Actual : Second_T;
begin
  Child : entity work.Enumeration_Child(rtl)
    port map (Value => Actual);
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Range_Child is
  port (Value : in High_Only_T);
end entity;
architecture rtl of enumeration_range_child is
begin
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Range_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_range_parent is
  signal Actual : First_T;
begin
  Child : entity work.Enumeration_Range_Child(rtl)
    port map (Value => Actual);
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Output_Range_Child is
  port (Value : out First_T);
end entity;
architecture rtl of enumeration_output_range_child is
begin
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Output_Range_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_output_range_parent is
  signal Actual : High_Only_T;
begin
  Child : entity work.Enumeration_Output_Range_Child(rtl)
    port map (Value => Actual);
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Inout_Child is
  port (Value : inout High_Only_T);
end entity;
architecture rtl of enumeration_inout_child is
begin
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Inout_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_inout_parent is
  signal Actual : High_Only_T;
begin
  Child : entity work.Enumeration_Inout_Child(rtl)
    port map (Value => Actual);
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Inout_Range_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_inout_range_parent is
  signal Actual : First_T;
begin
  Child : entity work.Enumeration_Inout_Child(rtl)
    port map (Value => Actual);
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Mixed_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_mixed_parent is
  signal Actual : First_T;
begin
  Child : Foreign_Enumeration_Child
    port map (Value => Actual);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(enumeration_boundary_vhdl.ok());
    const auto rejected_native_enumeration_boundary =
        fsim::elaboration::elaborate(
            enumeration_boundary_vhdl.design,
            "vhdl:work.enumeration_native_parent(rtl)");
    assert(
        !rejected_native_enumeration_boundary.ok()
        && has_diagnostic(
            rejected_native_enumeration_boundary,
            "FSIM-ELAB-BIND-053"));
    const auto rejected_range_enumeration_boundary =
        fsim::elaboration::elaborate(
            enumeration_boundary_vhdl.design,
            "vhdl:work.enumeration_range_parent(rtl)");
    assert(
        !rejected_range_enumeration_boundary.ok()
        && has_diagnostic(
            rejected_range_enumeration_boundary,
            "FSIM-ELAB-BIND-054"));
    const auto rejected_output_range_enumeration_boundary =
        fsim::elaboration::elaborate(
            enumeration_boundary_vhdl.design,
            "vhdl:work.enumeration_output_range_parent(rtl)");
    assert(
        !rejected_output_range_enumeration_boundary.ok()
        && has_diagnostic(
            rejected_output_range_enumeration_boundary,
            "FSIM-ELAB-BIND-054"));
    const auto accepted_inout_enumeration_boundary =
        fsim::elaboration::elaborate(
            enumeration_boundary_vhdl.design,
            "vhdl:work.enumeration_inout_parent(rtl)");
    assert(accepted_inout_enumeration_boundary.ok());
    const auto rejected_inout_range_enumeration_boundary =
        fsim::elaboration::elaborate(
            enumeration_boundary_vhdl.design,
            "vhdl:work.enumeration_inout_range_parent(rtl)");
    assert(
        !rejected_inout_range_enumeration_boundary.ok()
        && has_diagnostic(
            rejected_inout_range_enumeration_boundary,
            "FSIM-ELAB-BIND-054"));

    const auto enumeration_boundary_sv =
        fsim::frontend::parse_text(
            "foreign_enumeration_child.sv",
            R"(
module foreign_enumeration_child(input bit value);
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(enumeration_boundary_sv.ok());
    auto mixed_enumeration_boundary =
        enumeration_boundary_vhdl.design;
    mixed_enumeration_boundary.units.insert(
        mixed_enumeration_boundary.units.end(),
        enumeration_boundary_sv.design.units.begin(),
        enumeration_boundary_sv.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        enumeration_boundary_binding{
            {
                "enumeration_mixed_parent.child",
                "sv:work.foreign_enumeration_child",
                std::nullopt}};
    const auto rejected_mixed_enumeration_boundary =
        fsim::elaboration::elaborate(
            mixed_enumeration_boundary,
            "vhdl:work.enumeration_mixed_parent(rtl)",
            enumeration_boundary_binding);
    assert(
        !rejected_mixed_enumeration_boundary.ok()
        && has_diagnostic(
            rejected_mixed_enumeration_boundary,
            "FSIM-ELAB-BIND-052"));

    const auto invalid_vhdl_shift =
        fsim::frontend::parse_text(
            "invalid_vhdl_shift.vhd",
            R"(
entity invalid_vhdl_shift is
  port (
    lhs : in signed(7 downto 0);
    result : out signed(7 downto 0)
  );
end entity;

architecture rtl of invalid_vhdl_shift is
begin
  result <= lhs sll lhs;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_shift.ok());
    const auto rejected_vhdl_shift =
        fsim::elaboration::elaborate(
            invalid_vhdl_shift.design,
            "vhdl:work.invalid_vhdl_shift(rtl)");
    assert(!rejected_vhdl_shift.ok());
    assert(has_diagnostic(
        rejected_vhdl_shift, "FSIM-ELAB-070"));

    const auto invalid_vhdl_abs =
        fsim::frontend::parse_text(
            "invalid_vhdl_abs.vhd",
            R"(
entity invalid_vhdl_abs is
  port (
    lhs : in unsigned(7 downto 0);
    result : out unsigned(7 downto 0)
  );
end entity;

architecture rtl of invalid_vhdl_abs is
begin
  result <= abs lhs;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_abs.ok());
    const auto rejected_vhdl_abs =
        fsim::elaboration::elaborate(
            invalid_vhdl_abs.design,
            "vhdl:work.invalid_vhdl_abs(rtl)");
    assert(!rejected_vhdl_abs.ok());
    assert(has_diagnostic(
        rejected_vhdl_abs, "FSIM-ELAB-082"));

    const auto generic_integer_constraint =
        fsim::frontend::parse_text(
            "generic_integer_constraint.vhd",
            R"(
entity generic_integer_constraint is
  generic (limit : natural := 4);
  port (value : out integer range -limit to limit);
end entity;
architecture rtl of generic_integer_constraint is
begin
  value <= limit;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(generic_integer_constraint.ok());
    const auto elaborated_integer_constraint =
        fsim::elaboration::elaborate(
            generic_integer_constraint.design,
            "vhdl:work.generic_integer_constraint(rtl)");
    assert(elaborated_integer_constraint.ok());
    const auto generic_value =
        elaborated_integer_constraint.design->find_signal("value");
    assert(generic_value);
    const auto& generic_value_info =
        elaborated_integer_constraint.design
            ->signals().at(*generic_value);
    assert(
        generic_value_info.integer_range
        && generic_value_info.integer_range->left == -4
        && generic_value_info.integer_range->right == 4);

    const auto invalid_integer_constraints =
        fsim::frontend::parse_text(
            "invalid_integer_constraints.vhd",
            R"(
entity invalid_integer_constraints is
end entity;
architecture rtl of invalid_integer_constraints is
  signal outside_base : natural range -1 to 2;
  signal null_range : integer range 3 to -2;
  signal assigned : positive;
  signal bits : bit_vector(31 downto 0);
  signal integer_target : integer;
begin
  assigned <= 0;
  integer_target <= bits;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_integer_constraints.ok());
    const auto rejected_integer_constraints =
        fsim::elaboration::elaborate(
            invalid_integer_constraints.design,
            "vhdl:work.invalid_integer_constraints(rtl)");
    assert(!rejected_integer_constraints.ok());
    assert(has_diagnostic(
        rejected_integer_constraints,
        "FSIM-ELAB-INTEGER-002"));
    assert(has_diagnostic(
        rejected_integer_constraints,
        "FSIM-ELAB-INTEGER-003"));
    assert(has_diagnostic(
        rejected_integer_constraints,
        "FSIM-ELAB-INTEGER-004"));

    const auto mixed_vhdl_arithmetic =
        fsim::frontend::parse_text(
            "mixed_vhdl_arithmetic.vhd",
            R"(
entity mixed_vhdl_arithmetic is
  port (
    lhs : in signed(7 downto 0);
    rhs : in unsigned(7 downto 0);
    result : out signed(7 downto 0)
  );
end entity;
architecture rtl of mixed_vhdl_arithmetic is
begin
  result <= lhs + rhs;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(mixed_vhdl_arithmetic.ok());
    const auto rejected_mixed_vhdl_arithmetic =
        fsim::elaboration::elaborate(
            mixed_vhdl_arithmetic.design,
            "vhdl:work.mixed_vhdl_arithmetic(rtl)");
    assert(!rejected_mixed_vhdl_arithmetic.ok());
    assert(has_diagnostic(
        rejected_mixed_vhdl_arithmetic, "FSIM-ELAB-067"));
}

} // namespace fsim::tests::elaboration

