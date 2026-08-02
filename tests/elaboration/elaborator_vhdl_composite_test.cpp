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
