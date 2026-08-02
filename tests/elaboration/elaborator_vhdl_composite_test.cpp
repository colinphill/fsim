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

void test_vhdl_access_type_storage() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_access_storage.vhd",
      R"(
entity Access_Storage is
end entity;

architecture rtl of access_storage is
  subtype Byte_Value is bit_vector(7 downto 0);
  type Byte_Pointer is access bit_vector(7 downto 0);
  type Count_Pointer is access integer range 0 to 15;
  type Packet is record
    Data : bit_vector(3 downto 0);
    Valid : boolean;
  end record;
  type Packet_Pointer is access Packet;
  function Make_Byte(Value : Byte_Value)
      return Byte_Pointer is
  begin
    return new bit_vector'(Value);
  end function;
  function Identity(Value : Byte_Pointer)
      return Byte_Pointer is
  begin
    return Value;
  end function;
  procedure Replace(
      variable Target : inout Byte_Pointer;
      Value : Byte_Value) is
  begin
    Target := new bit_vector'(Value);
  end procedure;
  signal Current : Byte_Pointer;
  signal Observed_Initial : bit_vector(7 downto 0);
  signal Observed_Updated : bit_vector(7 downto 0);
  signal Count_Result : integer;
  signal Packet_Initial : bit_vector(3 downto 0);
  signal Packet_Updated : bit_vector(3 downto 0);
  signal Was_Null : boolean;
  signal Alias_Equal : boolean;
  signal Not_Null : boolean;
  signal Callable_Null : boolean;
  signal Callable_Result : bit_vector(7 downto 0);
  signal Original_Result : bit_vector(7 downto 0);
begin
  retain_defaults : process
    variable First : Byte_Pointer;
    variable Second : Count_Pointer;
    variable Third : Packet_Pointer;
    variable Alias_Value : Byte_Pointer;
    variable Null_Alias : Byte_Pointer;
  begin
    Was_Null <= First = null;
    First := Make_Byte("10100101");
    Observed_Initial <= First.all;
    First.all := "01011010";
    Observed_Updated <= First.all;
    Alias_Value := Identity(First);
    Alias_Equal <= First = Alias_Value;
    Not_Null <= Alias_Value /= null;
    Null_Alias := Identity(null);
    Callable_Null <= Null_Alias = null;
    Replace(Alias_Value, "00110011");
    Callable_Result <= Alias_Value.all;
    Original_Result <= First.all;
    Second := new integer'(7);
    Second.all := 9;
    Count_Result <= Second.all;
    Third := new Packet'(Data => "1010", Valid => true);
    Packet_Initial <= Third.all.Data;
    Third.all := (Data => "0101", Valid => false);
    Packet_Updated <= Third.all.Data;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design,
      "vhdl:work.access_storage(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto current =
      elaborated.design->find_signal("current");
  assert(current);
  const auto& info =
      elaborated.design->signals().at(*current);
  assert(
      info.width == 32
      && info.source_domain
          == fsim::frontend::ValueDomain::Bit2
      && info.vhdl_access
      && info.vhdl_access->handle_width == 32
      && info.vhdl_access->maximum_objects == 4096
      && info.vhdl_access->nullable
      && info.vhdl_access->owns_designated_object
      && info.vhdl_access->simulation_lifetime
      && info.vhdl_access->designated_types.size() == 1
      && info.vhdl_access->designated_types[0].width() == 8);
  const auto access_paths = elaborated.design->signal_paths();
  assert(std::ranges::any_of(
      access_paths,
      [&](const auto& path) {
        return path.first == "access_storage.current"
            && path.second == *current;
      }));
  const auto interpreter =
      elaborated.design->create_interpreter();
  assert(
      interpreter->signal_value(*current).to_msb_string()
      == std::string(32, '0'));

  const auto& process =
      elaborated.design->processes().front();
  assert(
      process.debug_locals.size() == 7
      && process.debug_locals[0].type_name == "byte_pointer"
      && process.debug_locals[0].width == 32
      && process.debug_locals[1].type_name == "count_pointer"
      && process.debug_locals[1].width == 32
      && process.debug_locals[2].type_name == "packet_pointer"
      && process.debug_locals[2].width == 32
      && process.debug_locals[3].type_name == "byte_pointer"
      && process.debug_locals[4].type_name == "byte_pointer"
      && process.debug_locals[5].name == "replace.target"
      && process.debug_locals[5].type_name == "byte_pointer"
      && process.debug_locals[5].width == 32);
  std::size_t null_defaults = 0;
  for (const auto& operation : process.operations) {
    const auto* load =
        fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::LoadConstant>(&operation);
    if (load != nullptr && load->value.width() == 32
        && load->value.to_msb_string() == std::string(32, '0')) {
      ++null_defaults;
    }
  }
  assert(null_defaults >= 5);
  assert(
      process.container_register_count == 3
      && process.container_register_types.size() == 3
      && process.container_register_types[0].queue
      && process.container_register_types[0].maximum_elements == 4096
      && process.container_register_types[0].element_width == 8
      && process.container_register_types[1].element_width == 32
      && process.container_register_types[2].element_width == 5);
  const auto observed_initial =
      elaborated.design->find_signal("observed_initial");
  const auto observed_updated =
      elaborated.design->find_signal("observed_updated");
  const auto count_result =
      elaborated.design->find_signal("count_result");
  const auto packet_initial =
      elaborated.design->find_signal("packet_initial");
  const auto packet_updated =
      elaborated.design->find_signal("packet_updated");
  const auto was_null =
      elaborated.design->find_signal("was_null");
  const auto alias_equal =
      elaborated.design->find_signal("alias_equal");
  const auto not_null =
      elaborated.design->find_signal("not_null");
  const auto callable_null =
      elaborated.design->find_signal("callable_null");
  const auto callable_result =
      elaborated.design->find_signal("callable_result");
  const auto original_result =
      elaborated.design->find_signal("original_result");
  assert(
      observed_initial && observed_updated && count_result
      && packet_initial && packet_updated && was_null
      && alias_equal && not_null && callable_null
      && callable_result && original_result);
  const auto run = interpreter->run();
  assert(
      run.status == fsim::runtime::RunStatus::completed
      && interpreter->signal_value(*observed_initial).to_msb_string()
          == "10100101"
      && interpreter->signal_value(*observed_updated).to_msb_string()
          == "01011010"
      && interpreter->signal_value(*count_result).low_word().aval == 9
      && interpreter->signal_value(*count_result).low_word().bval == 0
      && interpreter->signal_value(*packet_initial).to_msb_string()
          == "1010"
      && interpreter->signal_value(*packet_updated).to_msb_string()
          == "0101"
      && interpreter->signal_value(*was_null).to_msb_string() == "1"
      && interpreter->signal_value(*alias_equal).to_msb_string() == "1"
      && interpreter->signal_value(*not_null).to_msb_string() == "1"
      && interpreter->signal_value(*callable_null).to_msb_string() == "1"
      && interpreter->signal_value(*callable_result).to_msb_string()
          == "00110011"
      && interpreter->signal_value(*original_result).to_msb_string()
          == "01011010");

  const auto cyclic = fsim::frontend::parse_text(
      "invalid_access_cycle.vhd",
      R"(
entity Invalid_Access_Cycle is
end entity;
architecture rtl of invalid_access_cycle is
  type Loop_Pointer is access Loop_Pointer;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(cyclic.ok());
  const auto rejected_cycle = fsim::elaboration::elaborate(
      cyclic.design,
      "vhdl:work.invalid_access_cycle(rtl)");
  assert(
      !rejected_cycle.ok()
      && has_diagnostic(
          rejected_cycle, "FSIM-ELAB-VHACCESS-001"));

  const auto protected_designated =
      fsim::frontend::parse_text(
          "invalid_protected_access.vhd",
          R"(
entity Invalid_Protected_Access is
end entity;
architecture rtl of invalid_protected_access is
  type Guard is protected
    procedure Enter;
  end protected Guard;
  type Guard_Pointer is access Guard;
begin
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(protected_designated.ok());
  const auto rejected_protected =
      fsim::elaboration::elaborate(
          protected_designated.design,
          "vhdl:work.invalid_protected_access(rtl)");
  assert(
      !rejected_protected.ok()
      && has_diagnostic(
          rejected_protected, "FSIM-ELAB-VHACCESS-003"));

  const auto nominal_mismatch =
      fsim::frontend::parse_text(
          "invalid_access_nominal_copy.vhd",
          R"(
entity Invalid_Access_Nominal_Copy is
end entity;
architecture rtl of invalid_access_nominal_copy is
  type First_Pointer is access bit;
  type Second_Pointer is access bit;
begin
  fail : process
    variable First : First_Pointer;
    variable Second : Second_Pointer;
  begin
    First := Second;
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(nominal_mismatch.ok());
  const auto rejected_nominal =
      fsim::elaboration::elaborate(
          nominal_mismatch.design,
          "vhdl:work.invalid_access_nominal_copy(rtl)");
  assert(
      !rejected_nominal.ok()
      && has_diagnostic(
          rejected_nominal, "FSIM-ELAB-VHACCESS-020"));

  const auto escaping = fsim::frontend::parse_text(
      "invalid_access_escape.vhd",
      R"(
entity Invalid_Access_Escape is
end entity;
architecture rtl of invalid_access_escape is
  type Bit_Pointer is access bit;
  signal Published : Bit_Pointer;
begin
  fail : process
    variable Local : Bit_Pointer;
  begin
    Local := new bit;
    Published <= Local;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(escaping.ok());
  const auto rejected_escape = fsim::elaboration::elaborate(
      escaping.design,
      "vhdl:work.invalid_access_escape(rtl)");
  assert(
      !rejected_escape.ok()
      && has_diagnostic(
          rejected_escape, "FSIM-ELAB-VHACCESS-021"));

  const auto deallocation = fsim::frontend::parse_text(
      "invalid_access_deallocation.vhd",
      R"(
entity Invalid_Access_Deallocation is
end entity;
architecture rtl of invalid_access_deallocation is
  type Bit_Pointer is access bit;
begin
  fail : process
    variable Local : Bit_Pointer;
  begin
    Deallocate(Local);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(deallocation.ok());
  const auto rejected_deallocation =
      fsim::elaboration::elaborate(
          deallocation.design,
          "vhdl:work.invalid_access_deallocation(rtl)");
  assert(
      !rejected_deallocation.ok()
      && has_diagnostic(
          rejected_deallocation, "FSIM-ELAB-VHACCESS-022"));

  const auto null_dereference =
      fsim::frontend::parse_text(
          "null_access_dereference.vhd",
          R"(
entity Null_Access_Dereference is
end entity;
architecture rtl of null_access_dereference is
  type Bit_Pointer is access bit;
  signal Observed : bit;
begin
  fail : process
    variable Pointer : Bit_Pointer;
  begin
    Observed <= Pointer.all;
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(null_dereference.ok());
  const auto null_design = fsim::elaboration::elaborate(
      null_dereference.design,
      "vhdl:work.null_access_dereference(rtl)");
  assert(null_design.ok());
  auto null_interpreter =
      null_design.design->create_interpreter();
  bool saw_null_failure = false;
  try {
    (void)null_interpreter->run();
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    saw_null_failure = std::string_view{error.what()}.find(
        "container index is out of range")
        != std::string_view::npos;
  }
  assert(saw_null_failure);

  auto exhausted = fsim::frontend::parse_text(
      "exhausted_access_heap.vhd",
      R"(
entity Exhausted_Access_Heap is
end entity;
architecture rtl of exhausted_access_heap is
  type Bit_Pointer is access bit;
begin
  fail : process
    variable Pointer : Bit_Pointer;
  begin
    Pointer := new bit;
    Pointer := new bit;
    Pointer := new bit;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(exhausted.ok());
  auto& exhausted_architecture = exhausted.design.units.back();
  assert(
      exhausted_architecture.type_aliases.size() == 1
      && exhausted_architecture.type_aliases[0].type.vhdl_access);
  exhausted_architecture.type_aliases[0]
      .type.vhdl_access->maximum_objects = 2;
  const auto exhausted_design = fsim::elaboration::elaborate(
      exhausted.design,
      "vhdl:work.exhausted_access_heap(rtl)");
  assert(exhausted_design.ok());
  auto exhausted_interpreter =
      exhausted_design.design->create_interpreter();
  bool saw_exhaustion = false;
  try {
    (void)exhausted_interpreter->run();
  } catch (const fsim::runtime::simir::AssertionError& error) {
    saw_exhaustion = std::string_view{error.what()}.find(
        "VHDL access allocation exceeded the bounded object limit")
        != std::string_view::npos;
  }
  assert(saw_exhaustion);
}

void test_vhdl_protected_type_storage() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_protected_storage.vhd",
      R"(
package Counter_Types is
  type Counter is protected
    procedure Add(Value : integer);
    impure function Read return integer;
  end protected Counter;
end package;

package body Counter_Types is
  type Counter is protected body
    variable Current_Value : integer := 7;
    variable Enabled : bit := '1';
    procedure Add(Value : integer) is
    begin
      Current_Value := Current_Value + Value;
    end procedure;
    impure function Read return integer is
    begin
      return Current_Value;
    end function;
  end protected body Counter;
end package body;

entity Protected_Storage is
end entity;

use work.Counter_Types.all;
architecture rtl of protected_storage is
  shared variable Shared_Counter : Counter;
begin
  first : process
  begin
    Shared_Counter.Add(2);
    Shared_Counter.Add(Shared_Counter.Read());
    wait;
  end process;
  second : process
  begin
    Shared_Counter.Add(3);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design,
      "vhdl:work.protected_storage(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto& objects =
      elaborated.design->vhdl_protected_objects();
  assert(
      objects.size() == 1
      && objects[0].id == 0
      && objects[0].name
          == "protected_storage.shared_counter"
      && objects[0].type_name == "counter"
      && !objects[0].nominal_type.empty()
      && objects[0].members.size() == 2
      && objects[0].members[0].name == "current_value"
      && objects[0].members[0].offset == 0
      && objects[0].members[0].width == 32
      && objects[0].members[1].name == "enabled"
      && objects[0].members[1].offset == 32
      && objects[0].members[1].width == 1
      && !elaborated.design->find_signal(
          "shared_counter.current_value"));
  const auto current = elaborated.design->find_container(
      "shared_counter.current_value");
  const auto enabled = elaborated.design->find_container(
      "shared_counter.enabled");
  assert(current && enabled);
  const auto protected_paths = elaborated.design->container_paths();
  assert(
      std::ranges::any_of(
          protected_paths,
          [&](const auto& path) {
            return path.first
                    == "protected_storage.shared_counter.current_value"
                && path.second == *current;
          })
      && std::ranges::any_of(
          protected_paths,
          [&](const auto& path) {
            return path.first
                    == "protected_storage.shared_counter.enabled"
                && path.second == *enabled;
          }));
  const auto interpreter =
      elaborated.design->create_interpreter();
  assert(
      interpreter->container_object_value(*current)
              .elements.at(0).low_word().aval == 7
      && interpreter->container_object_value(*enabled)
              .elements.at(0).to_msb_string() == "1");
  const auto run = interpreter->run();
  assert(
      run.status == fsim::runtime::RunStatus::completed
      && interpreter->container_object_value(*current)
              .elements.at(0).low_word().aval == 21);

  const auto mismatched = fsim::frontend::parse_text(
      "vhdl_protected_mismatch.vhd",
      R"(
package Invalid_Counters is
  type Counter is protected
    procedure Add(Value : integer);
  end protected Counter;
end package;
package body Invalid_Counters is
  type Counter is protected body
    procedure Add(Value : boolean) is
    begin
      null;
    end procedure;
  end protected body Counter;
end package body;
entity Invalid_Protected_Profile is
end entity;
use work.Invalid_Counters.all;
architecture rtl of invalid_protected_profile is
  shared variable Shared_Counter : Counter;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(mismatched.ok());
  const auto mismatched_result = fsim::elaboration::elaborate(
      mismatched.design,
      "vhdl:work.invalid_protected_profile(rtl)");
  assert(
      !mismatched_result.ok()
      && has_diagnostic(
          mismatched_result,
          "FSIM-ELAB-VHPROTECTED-004")
      && has_diagnostic(
          mismatched_result,
          "FSIM-ELAB-VHPROTECTED-005"));

  const auto invalid_shared = fsim::frontend::parse_text(
      "vhdl_invalid_shared.vhd",
      R"(
entity Invalid_Shared is
end entity;
architecture rtl of invalid_shared is
  shared variable Plain : integer := 1;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid_shared.ok());
  const auto invalid_shared_result = fsim::elaboration::elaborate(
      invalid_shared.design,
      "vhdl:work.invalid_shared(rtl)");
  assert(
      !invalid_shared_result.ok()
      && has_diagnostic(
          invalid_shared_result,
          "FSIM-ELAB-VHPROTECTED-008"));

  const auto invalid_execution = fsim::frontend::parse_text(
      "vhdl_invalid_protected_execution.vhd",
      R"(
package Invalid_Execution_Types is
  type Guard is protected
    procedure Pause;
    procedure Reenter;
  end protected Guard;
end package;
package body Invalid_Execution_Types is
  type Guard is protected body
    procedure Pause is
    begin
      null;
    end procedure;
    procedure Reenter is
    begin
      Reenter;
    end procedure;
  end protected body Guard;
end package body;
entity Invalid_Protected_Execution is
end entity;
use work.Invalid_Execution_Types.all;
architecture rtl of invalid_protected_execution is
  shared variable Shared_Guard : Guard;
begin
  pause_process : process
  begin
    Shared_Guard.Pause;
    wait;
  end process;
  reentry_process : process
  begin
    Shared_Guard.Reenter;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid_execution.ok());
  auto invalid_execution_design = invalid_execution.design;
  auto& invalid_body = invalid_execution_design.units[1];
  assert(
      invalid_body.type_aliases.size() == 1
      && invalid_body.type_aliases[0].type.vhdl_protected
      && invalid_body.type_aliases[0]
             .type.vhdl_protected->procedures.size() == 2
      && !invalid_body.type_aliases[0]
              .type.vhdl_protected->procedures[0].statements.empty());
  invalid_body.type_aliases[0]
      .type.vhdl_protected->procedures[0]
      .statements[0].kind = fsim::frontend::StatementKind::WaitOn;
  const auto invalid_execution_result =
      fsim::elaboration::elaborate(
          invalid_execution_design,
          "vhdl:work.invalid_protected_execution(rtl)");
  assert(
      !invalid_execution_result.ok()
      && has_diagnostic(
          invalid_execution_result,
          "FSIM-ELAB-VHPROTECTED-020")
      && has_diagnostic(
          invalid_execution_result,
          "FSIM-ELAB-VHPROTECTED-021"));
}

void test_vhdl_physical_type_execution() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_physical_execution.vhd",
      R"(
entity Physical_Execution is
end entity;

architecture rtl of physical_execution is
  type Distance is range -2000000 to 2000000 units
    um;
    mm = 1000 um;
    meter = 1000 mm;
  end units Distance;
  constant Offset : Distance := 250 um;
  function Scale_Value(
      Value : Distance;
      Factor : integer) return Distance is
  begin
    return Value * Factor;
  end function;
  signal Added : Distance;
  signal Scaled : Distance;
  signal Called : Distance;
  signal Copied : Distance;
  signal Converted : Distance;
  signal Ratio : integer;
  signal Less : boolean;
  alias Added_Alias : Distance is Added;
begin
  Copied <= Added_Alias;
  process
    variable Local : Distance;
  begin
    Local := 2 mm;
    Added <= Local + Offset;
    Scaled <= Local * 2;
    Called <= Scale_Value(Local, 2);
    Converted <= Distance(7);
    Ratio <= Local / 1 mm;
    Less <= Local < 3 mm;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design,
      "vhdl:work.physical_execution(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto added = elaborated.design->find_signal("added");
  const auto scaled = elaborated.design->find_signal("scaled");
  const auto called = elaborated.design->find_signal("called");
  const auto copied = elaborated.design->find_signal("copied");
  const auto converted = elaborated.design->find_signal("converted");
  const auto ratio = elaborated.design->find_signal("ratio");
  const auto less = elaborated.design->find_signal("less");
  assert(added && scaled && called && copied && converted && ratio && less);
  const auto& physical_info = elaborated.design->signals().at(*added);
  assert(
      physical_info.source_domain
          == fsim::frontend::ValueDomain::Integer
      && physical_info.is_signed
      && physical_info.nominal_type.find(":distance")
          != std::string::npos
      && physical_info.vhdl_physical
      && physical_info.vhdl_physical->resolved_range
      && physical_info.vhdl_physical->resolved_range->left == -2000000
      && physical_info.vhdl_physical->resolved_range->right == 2000000
      && physical_info.vhdl_physical->units.size() == 3
      && physical_info.vhdl_physical->units[0].scale_factor == 1
      && physical_info.vhdl_physical->units[1].scale_factor == 1000
      && physical_info.vhdl_physical->units[2].scale_factor == 1000000);
  const auto physical_paths = elaborated.design->signal_paths();
  assert(std::ranges::any_of(
      physical_paths,
      [&](const auto& path) {
        return path.first == "physical_execution.added"
            && path.second == *added;
      }));
  const auto physical_process = std::ranges::find_if(
      elaborated.design->processes(),
      [](const auto& process) {
        return !process.debug_locals.empty();
      });
  assert(
      physical_process != elaborated.design->processes().end()
      && physical_process->debug_locals.size() == 1
      && physical_process->debug_locals.front().name == "local"
      && physical_process->debug_locals.front().type_name == "distance"
      && physical_process->debug_locals.front().width == 32);
  const auto interpreter = elaborated.design->create_interpreter();
  const auto run = interpreter->run();
  assert(
      run.status == fsim::runtime::RunStatus::completed
      && interpreter->signal_value(*added).low_word().aval == 2250
      && interpreter->signal_value(*scaled).low_word().aval == 4000
      && interpreter->signal_value(*called).low_word().aval == 4000
      && interpreter->signal_value(*copied).low_word().aval == 2250
      && interpreter->signal_value(*converted).low_word().aval == 7
      && interpreter->signal_value(*ratio).low_word().aval == 2
      && interpreter->signal_value(*less).to_msb_string() == "1");

  const auto hierarchy = fsim::frontend::parse_text(
      "vhdl_physical_hierarchy.vhd",
      R"(
entity Physical_Leaf is
  generic (Scale : positive := 10);
end entity;
architecture rtl of physical_leaf is
  type Distance is range 0 to 100 units
    tick;
    step = 10 tick;
  end units Distance;
  signal Result : Distance;
begin
  Result <= Distance(Scale);
end architecture;

entity Physical_Hierarchy is
end entity;
architecture rtl of physical_hierarchy is
begin
  First : entity work.Physical_Leaf(rtl)
    generic map (Scale => 10);
  Second : entity work.Physical_Leaf(rtl)
    generic map (Scale => 20);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!hierarchy.ok()) {
    for (const auto& diagnostic : hierarchy.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(hierarchy.ok());
  const auto hierarchy_result = fsim::elaboration::elaborate(
      hierarchy.design,
      "vhdl:work.physical_hierarchy(rtl)");
  if (!hierarchy_result.ok()) {
    for (const auto& diagnostic : hierarchy_result.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(hierarchy_result.ok());
  const auto first = hierarchy_result.design->find_signal(
      "physical_hierarchy.first.result");
  const auto second = hierarchy_result.design->find_signal(
      "physical_hierarchy.second.result");
  assert(first && second);
  const auto first_range =
      hierarchy_result.design->signals().at(*first)
          .vhdl_physical->resolved_range;
  const auto second_range =
      hierarchy_result.design->signals().at(*second)
          .vhdl_physical->resolved_range;
  assert(
      first_range && second_range
      && first_range->right == 100
      && second_range->right == 100);
  const auto hierarchy_interpreter =
      hierarchy_result.design->create_interpreter();
  const auto hierarchy_run = hierarchy_interpreter->run();
  assert(
      hierarchy_run.status == fsim::runtime::RunStatus::completed
      && hierarchy_interpreter->signal_value(*first)
              .low_word().aval == 10
      && hierarchy_interpreter->signal_value(*second)
              .low_word().aval == 20);

  const auto unknown_unit = fsim::frontend::parse_text(
      "vhdl_physical_unknown_unit.vhd",
      R"(
entity Physical_Unknown_Unit is
end entity;
architecture rtl of physical_unknown_unit is
  type Distance is range 0 to 10 units
    um;
  end units Distance;
  signal Value : Distance;
begin
  Value <= 1 inch;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(unknown_unit.ok());
  const auto unknown_unit_result = fsim::elaboration::elaborate(
      unknown_unit.design,
      "vhdl:work.physical_unknown_unit(rtl)");
  assert(
      !unknown_unit_result.ok()
      && has_diagnostic(
          unknown_unit_result,
          "FSIM-ELAB-VHPHYSICAL-006"));

  const auto cross_nominal = fsim::frontend::parse_text(
      "vhdl_physical_cross_nominal.vhd",
      R"(
entity Physical_Cross_Nominal is
end entity;
architecture rtl of physical_cross_nominal is
  type First_Distance is range 0 to 10 units
    tick;
  end units First_Distance;
  type Second_Distance is range 0 to 10 units
    tick;
  end units Second_Distance;
  signal Source : First_Distance;
  signal Target : Second_Distance;
begin
  Target <= Source;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(cross_nominal.ok());
  const auto cross_nominal_result = fsim::elaboration::elaborate(
      cross_nominal.design,
      "vhdl:work.physical_cross_nominal(rtl)");
  assert(
      !cross_nominal_result.ok()
      && has_diagnostic(
          cross_nominal_result,
          "FSIM-ELAB-VHPHYSICAL-009"));

  const auto invalid_scale = fsim::frontend::parse_text(
      "vhdl_physical_invalid_scale.vhd",
      R"(
entity Physical_Invalid_Scale is
end entity;
architecture rtl of physical_invalid_scale is
  type Distance is range 0 to 2147483647 units
    tick;
    huge = 2147483647 tick;
    overflow = 2 huge;
  end units Distance;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid_scale.ok());
  const auto invalid_scale_result = fsim::elaboration::elaborate(
      invalid_scale.design,
      "vhdl:work.physical_invalid_scale(rtl)");
  assert(
      !invalid_scale_result.ok()
      && has_diagnostic(
          invalid_scale_result,
          "FSIM-ELAB-VHPHYSICAL-004"));

  const auto overflowing = fsim::frontend::parse_text(
      "vhdl_physical_runtime_overflow.vhd",
      R"(
entity Physical_Runtime_Overflow is
end entity;
architecture rtl of physical_runtime_overflow is
  type Distance is range -2147483648 to 2147483647 units
    tick;
  end units Distance;
  signal Result : Distance;
begin
  process
    variable Maximum : Distance;
  begin
    Maximum := 2147483647 tick;
    Result <= Maximum + 1 tick;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(overflowing.ok());
  const auto overflowing_result = fsim::elaboration::elaborate(
      overflowing.design,
      "vhdl:work.physical_runtime_overflow(rtl)");
  assert(overflowing_result.ok());
  auto overflowing_interpreter =
      overflowing_result.design->create_interpreter();
  bool saw_overflow = false;
  try {
    (void)overflowing_interpreter->run();
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    saw_overflow = std::string_view{error.what()}.find(
        "VHDL integer arithmetic overflow")
        != std::string_view::npos;
  }
  assert(saw_overflow);
}

}  // namespace fsim::tests::elaboration
