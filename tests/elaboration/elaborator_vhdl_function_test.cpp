// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_interface_function_generics()
{
    const auto parsed = fsim::frontend::parse_text(
        "interface-function-generics.vhd",
        R"(
package function_pkg is
  function package_increment(value : integer) return integer;
end package;
package body function_pkg is
  function package_increment(value : integer) return integer is
  begin
    return value + 2;
  end function;
end package body;

entity function_leaf is
  generic (
    function transform(value : integer) return integer;
    Width : positive := transform(3));
  port (
    input_value : in integer;
    output_value : out integer);
end entity;

architecture rtl of function_leaf is
begin
  output_value <= transform(input_value);
end architecture;

entity boxed_leaf is
  generic (
    function transform(value : integer) return integer is <>);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;

architecture rtl of boxed_leaf is
begin
  output_value <= transform(input_value);
end architecture;

entity function_wrapper is
  generic (
    function forwarded(value : integer) return integer);
end entity;

architecture rtl of function_wrapper is
  signal input_value : integer;
  signal output_value : integer;
begin
  nested: entity work.function_leaf(rtl)
    generic map (transform => forwarded)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity package_wrapper is
end entity;

use work.function_pkg.all;
architecture rtl of package_wrapper is
  signal input_value : integer;
  signal output_value : integer;
begin
  nested: entity work.function_leaf(rtl)
    generic map (package_increment)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity function_top is
end entity;

architecture rtl of function_top is
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  signal direct_input : integer;
  signal direct_output : integer;
  signal boxed_input : integer;
  signal boxed_output : integer;
begin
  direct_instance: entity work.function_leaf(rtl)
    generic map (increment)
    port map (
      input_value => direct_input,
      output_value => direct_output);
  nested_instance: entity work.function_wrapper(rtl)
    generic map (forwarded => increment)
    port map ();
  boxed_instance: entity work.boxed_leaf(rtl)
    port map (
      input_value => boxed_input,
      output_value => boxed_output);
  package_instance: entity work.package_wrapper(rtl)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design,
        "vhdl:work.function_top(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->specializations().size() == 7);

    const auto find_specialization =
        [&](const std::string_view instance) {
            return std::ranges::find_if(
                elaborated.design->specializations(),
                [&](const auto& specialization) {
                    return specialization.instance == instance;
                });
        };
    const auto direct = find_specialization("function_top.direct_instance");
    const auto wrapper = find_specialization("function_top.nested_instance");
    const auto nested = find_specialization(
        "function_top.nested_instance.nested");
    const auto boxed = find_specialization("function_top.boxed_instance");
    const auto package_actual = find_specialization(
        "function_top.package_instance.nested");
    assert(direct != elaborated.design->specializations().end());
    assert(wrapper != elaborated.design->specializations().end());
    assert(nested != elaborated.design->specializations().end());
    assert(boxed != elaborated.design->specializations().end());
    assert(
        package_actual
        != elaborated.design->specializations().end());
    assert(direct->parameter_values.size() == 2);
    assert(direct->parameter_values.front().first == "transform");
    assert(wrapper->parameter_values.size() == 1);
    assert(wrapper->parameter_values.front().first == "forwarded");
    assert(nested->parameter_values.size() == 2);
    assert(nested->parameter_values.front().first == "transform");
    assert(boxed->parameter_values.size() == 1);
    assert(boxed->parameter_values.front().first == "transform");
    assert(package_actual->parameter_values.size() == 2);
    assert(
        package_actual->parameter_values.front().first
        == "transform");

    const auto missing = fsim::frontend::parse_text(
        "missing-function-actual.vhd",
        R"(
entity missing is
  generic (
    function required(value : integer) return integer);
end entity;
architecture rtl of missing is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(missing.ok());
    const auto missing_result = fsim::elaboration::elaborate(
        missing.design, "vhdl:work.missing(rtl)");
    assert(!missing_result.ok());
    assert(has_diagnostic(
        missing_result, "FSIM-ELAB-VHFUNC-004"));

    const auto invalid = fsim::frontend::parse_text(
        "invalid-function-actuals.vhd",
        R"(
entity required_leaf is
  generic (
    function selected(value : integer) return integer);
end entity;
architecture rtl of required_leaf is
begin
end architecture;

entity default_leaf is
  generic (
    function selected(value : integer) return integer is <>);
end entity;
architecture rtl of default_leaf is
begin
end architecture;

entity invalid_top is
end entity;
architecture rtl of invalid_top is
  impure function impure_actual(value : integer) return integer is
  begin
    return value;
  end function;
  function wrong_profile(value : bit) return bit is
  begin
    return value;
  end function;
  function declared(value : integer) return integer;
  function first_match(value : integer) return integer is
  begin
    return value;
  end function;
  function second_match(value : integer) return integer is
  begin
    return value + 1;
  end function;
begin
  expression_actual: entity work.required_leaf(rtl)
    generic map (selected => 1)
    port map ();
  invisible_actual: entity work.required_leaf(rtl)
    generic map (selected => absent)
    port map ();
  profile_actual: entity work.required_leaf(rtl)
    generic map (selected => wrong_profile)
    port map ();
  impure_binding: entity work.required_leaf(rtl)
    generic map (selected => impure_actual)
    port map ();
  undefined_binding: entity work.required_leaf(rtl)
    generic map (selected => declared)
    port map ();
  ambiguous_default: entity work.default_leaf(rtl)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto invalid_result = fsim::elaboration::elaborate(
        invalid.design, "vhdl:work.invalid_top(rtl)");
    assert(!invalid_result.ok());
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-003"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-005"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-007"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-008"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-006"));

    auto cross_language_parent = fsim::frontend::parse_text(
        "cross-language-function.sv",
        R"(
module cross_language_function;
  function automatic int increment(input int value);
    return value + 1;
  endfunction
  foreign_target #(.selected(increment)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto cross_language_child = fsim::frontend::parse_text(
        "cross-language-function.vhd",
        R"(
entity foreign_target is
  generic (
    function selected(value : integer) return integer);
end entity;
architecture rtl of foreign_target is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(
        cross_language_parent.ok()
        && cross_language_child.ok());
    for (auto& unit : cross_language_child.design.units) {
        cross_language_parent.design.units.push_back(
            std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding>
        cross_language_binding { { "cross_language_function.child",
            "vhdl:work.foreign_target(rtl)",
            std::nullopt } };
    const auto cross_language_result = fsim::elaboration::elaborate(
        cross_language_parent.design,
        "sv:work.cross_language_function",
        cross_language_binding);
    assert(!cross_language_result.ok());
    assert(has_diagnostic(
        cross_language_result, "FSIM-ELAB-VHFUNC-002"));

    const auto recursive = fsim::frontend::parse_text(
        "recursive-function-actual.vhd",
        R"(
entity recursive_leaf is
  generic (
    function selected(value : integer) return integer);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of recursive_leaf is
begin
  output_value <= selected(input_value);
end architecture;

entity recursive_top is
end entity;
architecture rtl of recursive_top is
  function recurse(value : integer) return integer is
  begin
    return recurse(value);
  end function;
  signal input_value : integer;
  signal output_value : integer;
begin
  child: entity work.recursive_leaf(rtl)
    generic map (recurse)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(recursive.ok());
    const auto recursive_result = fsim::elaboration::elaborate(
        recursive.design, "vhdl:work.recursive_top(rtl)");
    assert(recursive_result.ok());
}

} // namespace fsim::tests::elaboration
