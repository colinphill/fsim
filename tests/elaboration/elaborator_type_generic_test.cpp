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

entity invalid_type_top is
end entity;
architecture rtl of invalid_type_top is
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
    assert(has_diagnostic(rejected, "FSIM-ELAB-VHSUBTYPE-003"));

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
