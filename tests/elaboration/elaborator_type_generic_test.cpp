// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_interface_type_generics() {
    const auto parsed = fsim::frontend::parse_text(
        "interface-type-generics.vhd",
        R"(
package Type_Pkg is
  type Package_Packet_T is record
    ready : bit;
    payload : bit_vector(1 downto 0);
  end record;
end package;

entity typed_copy is
  generic (type Data_T);
  port (
    input_value : in Data_T;
    output_value : out Data_T
  );
end entity;

architecture rtl of typed_copy is
  signal local_value : Data_T;
begin
  local_value <= input_value;
  output_value <= local_value;
end architecture;

entity typed_wrapper is
  generic (type Wrapped_T);
  port (
    input_value : in Wrapped_T;
    output_value : out Wrapped_T
  );
end entity;

architecture rtl of typed_wrapper is
  signal nested_value : Wrapped_T;
begin
  nested_copy: entity work.typed_copy(rtl)
    generic map (Wrapped_T)
    port map (
      input_value => input_value,
      output_value => nested_value
    );
  output_value <= nested_value;
end architecture;

entity constrained_copy is
  generic (
    type Vector_T;
    Width : positive := 4
  );
  port (
    input_value : in Vector_T(Width - 1 downto 0);
    output_value : out Vector_T(Width - 1 downto 0)
  );
end entity;

architecture rtl of constrained_copy is
begin
  output_value <= input_value;
end architecture;

entity typed_copy_top is
end entity;

architecture rtl of typed_copy_top is
  subtype Word_T is bit_vector(3 downto 0);
  type Packet_T is record
    valid : bit;
    payload : bit_vector(2 downto 0);
  end record;
  type State_T is (Idle, Run);
  type Array_T is array (-1 to 2) of bit;
  signal word_input : Word_T;
  signal word_output : Word_T;
  signal bit_input : bit;
  signal bit_output : bit;
  signal packet_input : Packet_T;
  signal packet_output : Packet_T;
  signal state_input : State_T;
  signal state_output : State_T;
  signal array_input : Array_T;
  signal array_output : Array_T;
  signal wrapped_input : Word_T;
  signal wrapped_output : Word_T;
  signal constrained4_input : bit_vector(3 downto 0);
  signal constrained4_output : bit_vector(3 downto 0);
  signal constrained8_input : bit_vector(7 downto 0);
  signal constrained8_output : bit_vector(7 downto 0);
  signal package_input : Type_Pkg.Package_Packet_T;
  signal package_output : Type_Pkg.Package_Packet_T;
begin
  word_copy: entity work.typed_copy(rtl)
    generic map (Data_T => Word_T)
    port map (
      input_value => word_input,
      output_value => word_output
    );
  bit_copy: entity work.typed_copy(rtl)
    generic map (bit)
    port map (
      input_value => bit_input,
      output_value => bit_output
    );
  packet_copy: entity work.typed_copy(rtl)
    generic map (Packet_T)
    port map (
      input_value => packet_input,
      output_value => packet_output
    );
  state_copy: entity work.typed_copy(rtl)
    generic map (State_T)
    port map (
      input_value => state_input,
      output_value => state_output
    );
  array_copy: entity work.typed_copy(rtl)
    generic map (Array_T)
    port map (
      input_value => array_input,
      output_value => array_output
    );
  wrapper: entity work.typed_wrapper(rtl)
    generic map (Word_T)
    port map (
      input_value => wrapped_input,
      output_value => wrapped_output
    );
  constrained4: entity work.constrained_copy(rtl)
    generic map (bit_vector)
    port map (
      input_value => constrained4_input,
      output_value => constrained4_output
    );
  constrained8: entity work.constrained_copy(rtl)
    generic map (bit_vector, 8)
    port map (
      input_value => constrained8_input,
      output_value => constrained8_output
    );
  package_copy: entity work.typed_copy(rtl)
    generic map (Type_Pkg.Package_Packet_T)
    port map (
      input_value => package_input,
      output_value => package_output
    );
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design,
        "vhdl:work.typed_copy_top(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->specializations().size() == 11);

    const auto find_specialization =
        [&](const std::string_view instance) {
          return std::ranges::find_if(
              elaborated.design->specializations(),
              [&](const auto& specialization) {
                return specialization.instance == instance;
              });
        };
    const auto word_specialization =
        find_specialization("typed_copy_top.word_copy");
    const auto bit_specialization =
        find_specialization("typed_copy_top.bit_copy");
    const auto packet_specialization =
        find_specialization("typed_copy_top.packet_copy");
    const auto state_specialization =
        find_specialization("typed_copy_top.state_copy");
    const auto array_specialization =
        find_specialization("typed_copy_top.array_copy");
    const auto wrapper_specialization =
        find_specialization("typed_copy_top.wrapper");
    const auto nested_specialization =
        find_specialization(
            "typed_copy_top.wrapper.nested_copy");
    const auto constrained4_specialization =
        find_specialization("typed_copy_top.constrained4");
    const auto constrained8_specialization =
        find_specialization("typed_copy_top.constrained8");
    const auto package_specialization =
        find_specialization("typed_copy_top.package_copy");
    assert(
        word_specialization
        != elaborated.design->specializations().end());
    assert(
        bit_specialization
        != elaborated.design->specializations().end());
    assert(
        packet_specialization
            != elaborated.design->specializations().end()
        && state_specialization
            != elaborated.design->specializations().end()
        && array_specialization
            != elaborated.design->specializations().end()
        && wrapper_specialization
            != elaborated.design->specializations().end()
        && nested_specialization
            != elaborated.design->specializations().end()
        && constrained4_specialization
            != elaborated.design->specializations().end()
        && constrained8_specialization
            != elaborated.design->specializations().end()
        && package_specialization
            != elaborated.design->specializations().end());
    assert(
        constrained4_specialization->parameter_values.size() == 2
        && constrained4_specialization->parameter_values[0].first
            == "vector_t"
        && constrained4_specialization->parameter_values[1].first
            == "width"
        && constrained4_specialization->parameter_values[1].second
            == "4"
        && constrained8_specialization->parameter_values.size() == 2
        && constrained8_specialization->parameter_values[1].first
            == "width"
        && constrained8_specialization->parameter_values[1].second
            == "8");
    assert(
        word_specialization->parameter_values.size() == 1
        && word_specialization->parameter_values[0].first
            == "data_t"
        && word_specialization->parameter_values[0].second
            .starts_with("vhdl-type-v1;")
        && bit_specialization->parameter_values.size() == 1
        && bit_specialization->parameter_values[0].first
            == "data_t"
        && bit_specialization->parameter_values[0].second
            .starts_with("vhdl-type-v1;")
        && word_specialization->parameter_values[0].second
            != bit_specialization->parameter_values[0].second);

    const auto word_input =
        elaborated.design->find_signal("word_input");
    const auto word_output =
        elaborated.design->find_signal("word_output");
    const auto bit_input =
        elaborated.design->find_signal("bit_input");
    const auto bit_output =
        elaborated.design->find_signal("bit_output");
    const auto packet_input =
        elaborated.design->find_signal("packet_input");
    const auto packet_output =
        elaborated.design->find_signal("packet_output");
    const auto state_input =
        elaborated.design->find_signal("state_input");
    const auto state_output =
        elaborated.design->find_signal("state_output");
    const auto array_input =
        elaborated.design->find_signal("array_input");
    const auto array_output =
        elaborated.design->find_signal("array_output");
    const auto wrapped_input =
        elaborated.design->find_signal("wrapped_input");
    const auto wrapped_output =
        elaborated.design->find_signal("wrapped_output");
    const auto constrained4_input =
        elaborated.design->find_signal("constrained4_input");
    const auto constrained4_output =
        elaborated.design->find_signal("constrained4_output");
    const auto constrained8_input =
        elaborated.design->find_signal("constrained8_input");
    const auto constrained8_output =
        elaborated.design->find_signal("constrained8_output");
    const auto package_input =
        elaborated.design->find_signal("package_input");
    const auto package_output =
        elaborated.design->find_signal("package_output");
    assert(
        word_input && word_output && bit_input && bit_output
        && packet_input && packet_output && state_input
        && state_output && array_input && array_output
        && wrapped_input && wrapped_output
        && constrained4_input && constrained4_output
        && constrained8_input && constrained8_output
        && package_input && package_output);
    assert(
        elaborated.design->signals().at(*word_input).width == 4
        && elaborated.design->signals().at(*word_output).width == 4
        && elaborated.design->signals().at(*bit_input).width == 1
        && elaborated.design->signals().at(*bit_output).width == 1
        && elaborated.design->signals().at(*packet_input).width == 4
        && elaborated.design->signals().at(*state_input).width == 1
        && elaborated.design->signals().at(*array_input).width == 4
        && elaborated.design->signals().at(*wrapped_input).width == 4
        && elaborated.design->signals().at(*constrained4_input).width
            == 4
        && elaborated.design->signals().at(*constrained8_input).width
            == 8
        && elaborated.design->signals().at(*package_input).width == 3);

    auto interpreter = elaborated.design->create_interpreter();
    interpreter->deposit_signal(
        *word_input,
        fsim::runtime::PackedLogic4::from_msb_string("1010"));
    interpreter->deposit_signal(
        *bit_input,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    interpreter->deposit_signal(
        *packet_input,
        fsim::runtime::PackedLogic4::from_msb_string("1101"));
    interpreter->deposit_signal(
        *state_input,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    interpreter->deposit_signal(
        *array_input,
        fsim::runtime::PackedLogic4::from_msb_string("0110"));
    interpreter->deposit_signal(
        *wrapped_input,
        fsim::runtime::PackedLogic4::from_msb_string("0011"));
    interpreter->deposit_signal(
        *constrained4_input,
        fsim::runtime::PackedLogic4::from_msb_string("1001"));
    interpreter->deposit_signal(
        *constrained8_input,
        fsim::runtime::PackedLogic4::from_msb_string("10100101"));
    interpreter->deposit_signal(
        *package_input,
        fsim::runtime::PackedLogic4::from_msb_string("101"));
    interpreter->start();
    const auto run = interpreter->run();
    assert(run.status == fsim::runtime::RunStatus::completed);
    assert(
        interpreter->signal_value(*word_output).to_msb_string()
        == "1010");
    assert(
        interpreter->signal_value(*bit_output).to_msb_string()
        == "1");
    assert(
        interpreter->signal_value(*packet_output).to_msb_string()
        == "1101");
    assert(
        interpreter->signal_value(*state_output).to_msb_string()
        == "1");
    assert(
        interpreter->signal_value(*array_output).to_msb_string()
        == "0110");
    assert(
        interpreter->signal_value(*wrapped_output).to_msb_string()
        == "0011");
    assert(
        interpreter->signal_value(*constrained4_output).to_msb_string()
        == "1001");
    assert(
        interpreter->signal_value(*constrained8_output).to_msb_string()
        == "10100101");
    assert(
        interpreter->signal_value(*package_output).to_msb_string()
        == "101");

    const auto constrained_actuals = fsim::frontend::parse_text(
        "constrained-interface-type-actuals.vhd",
        R"(
package Constrained_Type_Pkg is
  type Package_Vector_T is array (integer range <>) of bit;
end package;

entity subtype_copy is
  generic (type Data_T);
  port (
    input_value : in Data_T;
    output_value : out Data_T
  );
end entity;

architecture rtl of subtype_copy is
begin
  output_value <= input_value;
end architecture;

entity subtype_forward is
  generic (type Forward_T);
  port (
    input_value : in Forward_T;
    output_value : out Forward_T
  );
end entity;

architecture rtl of subtype_forward is
  signal nested_value : Forward_T;
begin
  nested: entity work.subtype_copy(rtl)
    generic map (Forward_T)
    port map (
      input_value => input_value,
      output_value => nested_value
    );
  output_value <= nested_value;
end architecture;

entity constrained_actual_top is
  generic (Width : positive := 4);
end entity;

architecture rtl of constrained_actual_top is
  type State_T is (Idle, Run);
  subtype Reverse_State_T is State_T range Run downto Idle;
  type Array_Base_T is array (integer range <>) of bit;
  subtype Array_T is Array_Base_T(-1 to 2);
  signal vector_input : bit_vector(Width - 1 downto 0);
  signal vector_output : bit_vector(Width - 1 downto 0);
  signal integer_input : integer range 3 to 12;
  signal integer_output : integer range 3 to 12;
  signal state_input : Reverse_State_T;
  signal state_output : Reverse_State_T;
  signal array_input : Array_T;
  signal array_output : Array_T;
  signal forward_input : bit_vector(5 downto 0);
  signal forward_output : bit_vector(5 downto 0);
  signal package_input :
    Constrained_Type_Pkg.Package_Vector_T(6 downto 3);
  signal package_output :
    Constrained_Type_Pkg.Package_Vector_T(6 downto 3);
begin
  vector_copy: entity work.subtype_copy(rtl)
    generic map (bit_vector(Width - 1 downto 0))
    port map (
      input_value => vector_input,
      output_value => vector_output
    );
  integer_copy: entity work.subtype_copy(rtl)
    generic map (integer range 3 to 12)
    port map (
      input_value => integer_input,
      output_value => integer_output
    );
  state_copy: entity work.subtype_copy(rtl)
    generic map (State_T range Run downto Idle)
    port map (
      input_value => state_input,
      output_value => state_output
    );
  array_copy: entity work.subtype_copy(rtl)
    generic map (Array_Base_T(-1 to 2))
    port map (
      input_value => array_input,
      output_value => array_output
    );
  forwarded_copy: entity work.subtype_forward(rtl)
    generic map (bit_vector(5 downto 0))
    port map (
      input_value => forward_input,
      output_value => forward_output
    );
  package_copy: entity work.subtype_copy(rtl)
    generic map (
      Constrained_Type_Pkg.Package_Vector_T(6 downto 3)
    )
    port map (
      input_value => package_input,
      output_value => package_output
    );
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(constrained_actuals.ok());
    const auto constrained_elaborated =
        fsim::elaboration::elaborate(
            constrained_actuals.design,
            "vhdl:work.constrained_actual_top(rtl)");
    if (!constrained_elaborated.ok()) {
        for (const auto& diagnostic :
             constrained_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(constrained_elaborated.ok());
    assert(
        constrained_elaborated.design->specializations().size()
        == 8);
    const auto find_constrained_signal =
        [&](const std::string_view name) {
          const auto signal =
              constrained_elaborated.design->find_signal(name);
          assert(signal);
          return *signal;
        };
    const auto constrained_vector_input =
        find_constrained_signal("vector_input");
    const auto constrained_vector_output =
        find_constrained_signal("vector_output");
    const auto constrained_integer_input =
        find_constrained_signal("integer_input");
    const auto constrained_state_input =
        find_constrained_signal("state_input");
    const auto constrained_array_input =
        find_constrained_signal("array_input");
    const auto constrained_array_output =
        find_constrained_signal("array_output");
    const auto constrained_forward_input =
        find_constrained_signal("forward_input");
    const auto constrained_forward_output =
        find_constrained_signal("forward_output");
    const auto constrained_package_input =
        find_constrained_signal("package_input");
    const auto constrained_package_output =
        find_constrained_signal("package_output");
    assert(
        constrained_elaborated.design->signals()
                .at(constrained_vector_input)
                .width
            == 4
        && constrained_elaborated.design->signals()
                .at(constrained_integer_input)
                .width
            == 32
        && constrained_elaborated.design->signals()
                .at(constrained_state_input)
                .width
            == 1
        && constrained_elaborated.design->signals()
                .at(constrained_array_input)
                .width
            == 4
        && constrained_elaborated.design->signals()
                .at(constrained_forward_input)
                .width
            == 6
        && constrained_elaborated.design->signals()
                .at(constrained_package_input)
                .width
            == 4);
    auto constrained_interpreter =
        constrained_elaborated.design->create_interpreter();
    constrained_interpreter->deposit_signal(
        constrained_vector_input,
        fsim::runtime::PackedLogic4::from_msb_string("1101"));
    constrained_interpreter->deposit_signal(
        constrained_array_input,
        fsim::runtime::PackedLogic4::from_msb_string("1010"));
    constrained_interpreter->deposit_signal(
        constrained_forward_input,
        fsim::runtime::PackedLogic4::from_msb_string("100101"));
    constrained_interpreter->deposit_signal(
        constrained_package_input,
        fsim::runtime::PackedLogic4::from_msb_string("0110"));
    constrained_interpreter->start();
    const auto constrained_run = constrained_interpreter->run();
    assert(
        constrained_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        constrained_interpreter
                ->signal_value(constrained_vector_output)
                .to_msb_string()
            == "1101"
        && constrained_interpreter
                ->signal_value(constrained_array_output)
                .to_msb_string()
            == "1010"
        && constrained_interpreter
                ->signal_value(constrained_forward_output)
                .to_msb_string()
            == "100101"
        && constrained_interpreter
                ->signal_value(constrained_package_output)
                .to_msb_string()
            == "0110");

    const auto enumeration_forwarding =
        fsim::frontend::parse_text(
            "enumeration-type-actual-forwarding.vhd",
            R"(
entity subtype_probe is
  generic (type Data_T);
end entity;
architecture rtl of subtype_probe is
begin
end architecture;

entity enumeration_forward is
  generic (
    type Forward_T;
    Lower, Upper : Forward_T
  );
end entity;
architecture rtl of enumeration_forward is
begin
  nested: entity work.subtype_probe(rtl)
    generic map (Forward_T range Lower to Upper)
    port map ();
end architecture;

entity enumeration_forward_top is
end entity;
architecture rtl of enumeration_forward_top is
  type State_T is (Idle, Run, Stop);
begin
  forwarded: entity work.enumeration_forward(rtl)
    generic map (State_T, Idle, Run)
    port map ();
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(enumeration_forwarding.ok());
    const auto enumeration_forwarded =
        fsim::elaboration::elaborate(
            enumeration_forwarding.design,
            "vhdl:work.enumeration_forward_top(rtl)");
    if (!enumeration_forwarded.ok()) {
        for (const auto& diagnostic :
             enumeration_forwarded.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(enumeration_forwarded.ok());
    const auto nested_enumeration =
        std::ranges::find_if(
            enumeration_forwarded.design->specializations(),
            [](const auto& specialization) {
              return specialization.instance
                  == "enumeration_forward_top.forwarded.nested";
            });
    assert(
        nested_enumeration
            != enumeration_forwarded.design
                   ->specializations()
                   .end()
        && nested_enumeration->parameter_values.size() == 1
        && nested_enumeration->parameter_values[0].second.find(
               ";enum-range=0:1:0")
            != std::string::npos);

    const auto invalid = fsim::frontend::parse_text(
        "invalid-interface-type-generics.vhd",
        R"(
entity required_type is
  generic (type Data_T);
end entity;
architecture rtl of required_type is
begin
end architecture;

entity constrained_type is
  generic (type Data_T);
  port (input_value : in Data_T(3 downto 0));
end entity;
architecture rtl of constrained_type is
begin
end architecture;

entity value_only is
  generic (Count : integer);
end entity;
architecture rtl of value_only is
begin
end architecture;

entity unconstrained_object is
  generic (type Data_T);
  port (input_value : in Data_T);
end entity;
architecture rtl of unconstrained_object is
begin
end architecture;

entity invalid_type_top is
end entity;
architecture rtl of invalid_type_top is
  subtype Word_T is bit_vector(3 downto 0);
  subtype Small_Integer_T is integer range 0 to 3;
  type State_T is (Idle, Run, Stop);
  subtype Small_State_T is State_T range Idle to Run;
  signal scalar_value : bit;
begin
  missing: entity work.required_type(rtl)
    port map ();
  unknown: entity work.required_type(rtl)
    generic map (Missing_T)
    port map ();
  value_actual: entity work.required_type(rtl)
    generic map (4)
    port map ();
  wrong_base: entity work.constrained_type(rtl)
    generic map (bit)
    port map (input_value => scalar_value);
  wrong_value_kind: entity work.value_only(rtl)
    generic map (integer range 1 to 3)
    port map ();
  null_vector: entity work.required_type(rtl)
    generic map (bit_vector(0 downto 3))
    port map ();
  reconstrained_vector: entity work.required_type(rtl)
    generic map (Word_T(1 downto 0))
    port map ();
  outside_integer: entity work.required_type(rtl)
    generic map (Small_Integer_T range 0 to 4)
    port map ();
  outside_enumeration: entity work.required_type(rtl)
    generic map (Small_State_T range Idle to Stop)
    port map ();
  unconstrained_vector: entity work.unconstrained_object(rtl)
    generic map (bit_vector)
    port map (input_value => scalar_value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto rejected = fsim::elaboration::elaborate(
        invalid.design,
        "vhdl:work.invalid_type_top(rtl)");
    assert(!rejected.ok());
    assert(has_diagnostic(rejected, "FSIM-ELAB-GENTYPE-001"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-GENTYPE-002"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-GENTYPE-003"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-GENTYPE-005"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-VHSUBTYPE-003"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-VHSUBTYPE-004"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-VHSUBTYPE-002"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-VHENUMRANGE-003"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-VHARRAY-005"));

    auto cross_language_design = invalid.design;
    const auto systemverilog_host = fsim::frontend::parse_text(
        "type-generic-host.sv",
        R"(
module type_generic_host;
  required_type #(.Data_T(TYPE_MARK)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_host.ok());
    cross_language_design.units.insert(
        cross_language_design.units.end(),
        systemverilog_host.design.units.begin(),
        systemverilog_host.design.units.end());
    const std::vector<fsim::elaboration::Binding> bindings{
        {"type_generic_host.child",
         "vhdl:work.required_type(rtl)",
         std::nullopt}};
    const auto rejected_cross_language =
        fsim::elaboration::elaborate(
            cross_language_design,
            "sv:work.type_generic_host",
            bindings);
    assert(!rejected_cross_language.ok());
    assert(has_diagnostic(
        rejected_cross_language, "FSIM-ELAB-GENTYPE-004"));
}

} // namespace fsim::tests::elaboration
