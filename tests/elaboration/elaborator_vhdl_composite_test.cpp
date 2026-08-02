// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_recursive_composite_layout() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_recursive_composite.vhd",
      R"(
entity Recursive_Composite is
end entity;

architecture rtl of recursive_composite is
  type Mode_T is (Idle, Ready, Busy);
  type Payload_T is record
    Lane : std_logic_vector(3 downto 0);
    Mode : Mode_T;
  end record;
  type Payload_Array_Base_T is array (natural range <>) of Payload_T;
  subtype Payload_Array_T is Payload_Array_Base_T(0 to 1);
  type Envelope_T is record
    Payload : Payload_T;
    Items : Payload_Array_T;
    Tag : Mode_T;
  end record;
  signal Payload_Value : Payload_T;
  signal Array_Value : Payload_Array_T;
  signal Envelope_Value : Envelope_T;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "vhdl:work.recursive_composite(rtl)");
  assert(elaborated.ok());
  const auto payload = elaborated.design->find_signal("payload_value");
  const auto array = elaborated.design->find_signal("array_value");
  const auto envelope = elaborated.design->find_signal("envelope_value");
  assert(payload && array && envelope);

  const auto find_info =
      [&](const fsim::runtime::simir::SignalId id)
          -> const fsim::elaboration::SignalInfo& {
        const auto found = std::ranges::find(
            elaborated.design->signals(), id,
            &fsim::elaboration::SignalInfo::id);
        assert(found != elaborated.design->signals().end());
        return *found;
      };
  const auto& payload_info = find_info(*payload);
  const auto& array_info = find_info(*array);
  const auto& envelope_info = find_info(*envelope);
  assert(
      payload_info.width == 6
      && payload_info.source_domain
          == fsim::frontend::ValueDomain::Logic9
      && payload_info.packed_members.size() == 2
      && payload_info.packed_members[0].name == "lane"
      && payload_info.packed_members[0].lsb_offset == 2
      && payload_info.packed_members[1].name == "mode"
      && payload_info.packed_members[1].lsb_offset == 0
      && payload_info.packed_members[1].nested_types.size() == 1
      && payload_info.packed_members[1]
             .nested_types.front().enumeration_literals.size()
          == 3
      && !payload_info.nominal_type.empty());
  assert(
      array_info.width == 12 && array_info.vhdl_array
      && array_info.vhdl_array->flat_width == 12
      && array_info.vhdl_array->dimensions.size() == 1
      && array_info.vhdl_array->dimensions.front().range
      && array_info.vhdl_array->dimensions.front().range->left == 0
      && array_info.vhdl_array->dimensions.front().range->right == 1
      && array_info.vhdl_array->element_types.size() == 1
      && array_info.vhdl_array->element_types.front().packed_members.size()
          == 2
      && array_info.vhdl_array->element_types.front().nominal_type
          == payload_info.nominal_type);
  assert(
      envelope_info.width == 20
      && envelope_info.source_domain
          == fsim::frontend::ValueDomain::Logic9
      && envelope_info.packed_members.size() == 3
      && envelope_info.packed_members[0].lsb_offset == 14
      && envelope_info.packed_members[1].lsb_offset == 2
      && envelope_info.packed_members[2].lsb_offset == 0
      && envelope_info.packed_members[0].nested_types.front()
             .packed_members.size()
          == 2
      && envelope_info.packed_members[0].nested_types.front().nominal_type
          == payload_info.nominal_type
      && envelope_info.packed_members[1].nested_types.front().vhdl_array
      && envelope_info.packed_members[1].nested_types.front().nominal_type
          == array_info.nominal_type);

  auto interpreter = elaborated.design->create_interpreter();
  assert(
      interpreter->signal_value(*payload).to_msb_string()
          == "UUUU00"
      && interpreter->signal_value(*array).to_msb_string()
          == "UUUU00UUUU00"
      && interpreter->signal_value(*envelope).to_msb_string()
          == "UUUU00UUUU00UUUU0000");

  const auto enumeration = fsim::frontend::parse_text(
      "vhdl_nested_enumeration.vhd",
      R"(
entity Nested_Enumeration is
end entity;

architecture rtl of nested_enumeration is
  type Mode_T is (Idle, Ready, Busy);
  type Other_Mode_T is (Cold, Hot);
  type Payload_T is record
    Mode : Mode_T;
  end record;
  type Envelope_T is record
    Payload : Payload_T;
    Tag : Mode_T;
  end record;
  function Choose(Value : Mode_T) return Mode_T is
  begin
    return Ready;
  end function;
  function Choose(Value : Other_Mode_T) return Mode_T is
  begin
    return Busy;
  end function;
  procedure Observe(Value : in Mode_T) is
  begin
    null;
  end procedure;
  procedure Observe(Value : in Other_Mode_T) is
  begin
    null;
  end procedure;
  signal Source : Envelope_T;
  signal Result : Envelope_T;
  signal Chosen : Mode_T;
  signal Equal_Result : boolean;
begin
  exercise : process
  begin
    Source <= (Payload => (Mode => Ready), Tag => Busy);
    Result <= (Payload => (Mode => Idle), Tag => Idle);
    wait for 1 ns;
    Chosen <= Choose(Source.Payload.Mode);
    Observe(Source.Payload.Mode);
    Equal_Result <= Source.Payload.Mode = Ready;
    case Source.Payload.Mode is
      when Ready =>
        Result.Payload.Mode <= Busy;
        Result.Tag <= Source.Payload.Mode;
      when others =>
        Result <= (Payload => (Mode => Idle), Tag => Idle);
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(enumeration.ok());
  const auto enumeration_result = fsim::elaboration::elaborate(
      enumeration.design, "vhdl:work.nested_enumeration(rtl)");
  if (!enumeration_result.ok()) {
    for (const auto& diagnostic : enumeration_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << '\n';
    }
  }
  assert(enumeration_result.ok());
  const auto source = enumeration_result.design->find_signal("source");
  const auto result = enumeration_result.design->find_signal("result");
  const auto chosen = enumeration_result.design->find_signal("chosen");
  const auto equal =
      enumeration_result.design->find_signal("equal_result");
  assert(source && result && chosen && equal);
  auto enumeration_interpreter =
      enumeration_result.design->create_interpreter();
  const auto run = enumeration_interpreter->run();
  assert(
      run.status == fsim::runtime::RunStatus::completed
      && enumeration_interpreter->signal_value(*source).to_msb_string()
          == "0110"
      && enumeration_interpreter->signal_value(*result).to_msb_string()
          == "1001"
      && enumeration_interpreter->signal_value(*chosen).to_msb_string()
          == "01"
      && enumeration_interpreter->signal_value(*equal).to_msb_string()
          == "1");

  const auto component = fsim::frontend::parse_text(
      "vhdl_nested_enumeration_component.vhd",
      R"(
package Nested_Enumeration_Component_Types is
  type Mode_T is (Idle, Active, Done);
  subtype Active_Mode_T is Mode_T range Active to Done;
  type Other_Mode_T is (Cold, Hot);
  type Mode_Box_T is record
    Mode : Active_Mode_T;
  end record;
end package;

use work.Nested_Enumeration_Component_Types.all;
package Nested_Enumeration_Component_Profiles is
  component Nested_Enumeration_Leaf is
    port (Value : in Active_Mode_T);
  end component;
  component Nested_Enumeration_Leaf is
    port (Value : in Other_Mode_T);
  end component;
end package;

use work.Nested_Enumeration_Component_Types.all;
entity Nested_Enumeration_Leaf is
  port (Target_Value : in Active_Mode_T);
end entity;

architecture rtl of Nested_Enumeration_Leaf is
begin
end architecture;

entity Nested_Enumeration_Component_Top is
end entity;

use work.Nested_Enumeration_Component_Types.all;
use work.Nested_Enumeration_Component_Profiles.all;
architecture rtl of Nested_Enumeration_Component_Top is
  signal Box : Mode_Box_T;
begin
  Child : Nested_Enumeration_Leaf port map (Box.Mode);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(component.ok());
  const auto component_result = fsim::elaboration::elaborate(
      component.design,
      "vhdl:work.nested_enumeration_component_top(rtl)");
  if (!component_result.ok()) {
    for (const auto& diagnostic : component_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << '\n';
    }
  }
  assert(component_result.ok());
  const auto component_child = std::ranges::find_if(
      component_result.design->specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "nested_enumeration_component_top.child";
      });
  assert(
      component_child != component_result.design->specializations().end()
      && component_child->unit
          == "vhdl:work.nested_enumeration_leaf(rtl)");

  const auto nominal_mismatch = fsim::frontend::parse_text(
      "vhdl_nested_enumeration_mismatch.vhd",
      R"(
entity Nested_Enumeration_Mismatch is
end entity;

architecture rtl of nested_enumeration_mismatch is
  type First_Mode_T is (Idle, Ready);
  type Second_Mode_T is (Idle, Ready);
  type First_Box_T is record
    Mode : First_Mode_T;
  end record;
  type Second_Box_T is record
    Mode : Second_Mode_T;
  end record;
  signal First_Value : First_Box_T;
  signal Second_Value : Second_Box_T;
  signal Equal_Result : boolean;
begin
  Equal_Result <= First_Value.Mode = Second_Value.Mode;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(nominal_mismatch.ok());
  const auto nominal_rejected = fsim::elaboration::elaborate(
      nominal_mismatch.design,
      "vhdl:work.nested_enumeration_mismatch(rtl)");
  assert(
      !nominal_rejected.ok()
      && has_diagnostic(nominal_rejected, "FSIM-ELAB-VHENUM-002"));

  const auto invalid = fsim::frontend::parse_text(
      "vhdl_invalid_recursive_composite.vhd",
      R"(
entity Invalid_Recursive_Composite is
end entity;

architecture rtl of invalid_recursive_composite is
  type Open_Array_T is array (natural range <>) of bit;
  type Indefinite_Record_T is record
    Data : Open_Array_T;
  end record;
  type First_T is record
    Next_Value : Second_T;
  end record;
  type Second_T is record
    Prior_Value : First_T;
  end record;
  type Unknown_Record_T is record
    Missing_Value : Missing_T;
  end record;
  signal Indefinite_Value : Indefinite_Record_T;
  signal Cyclic_Value : First_T;
  signal Unknown_Value : Unknown_Record_T;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design,
      "vhdl:work.invalid_recursive_composite(rtl)");
  assert(
      !rejected.ok()
      && has_diagnostic(rejected, "FSIM-ELAB-VHRECORD-001")
      && has_diagnostic(rejected, "FSIM-ELAB-VHTYPE-001")
      && has_diagnostic(rejected, "FSIM-ELAB-VHTYPE-002"));
}

}  // namespace fsim::tests::elaboration
