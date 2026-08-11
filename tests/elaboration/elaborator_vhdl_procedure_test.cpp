// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_interface_procedure_generics()
{
    auto parsed = fsim::frontend::parse_text(
        "vhdl-procedure-generics.vhd",
        R"(
package procedure_pkg is
  procedure package_exchange(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer);
end package;

package body procedure_pkg is
  procedure package_exchange(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
  begin
    destination := source + 2;
    accumulator := accumulator + destination;
  end procedure;
end package body;

entity procedure_leaf is
  generic (
    procedure selected(
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer));
  port (
    input_value : in integer;
    output_value : out integer;
    total_value : inout integer);
end entity;
architecture rtl of procedure_leaf is
  procedure invoke(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
  begin
    selected(source, destination, accumulator);
  end procedure;
begin
  process(input_value)
  begin
    invoke(input_value, output_value, total_value);
  end process;
end architecture;

entity boxed_leaf is
  generic (
    procedure selected(
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer) is <>);
  port (
    input_value : in integer;
    output_value : out integer;
    total_value : inout integer);
end entity;
architecture rtl of boxed_leaf is
begin
  process(input_value)
  begin
    selected(input_value, output_value, total_value);
  end process;
end architecture;

entity procedure_wrapper is
  generic (
    procedure forwarded(
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer));
end entity;
architecture rtl of procedure_wrapper is
  signal input_value : integer;
  signal output_value : integer;
  signal total_value : integer;
begin
  nested: entity work.procedure_leaf(rtl)
    generic map (forwarded)
    port map (
      input_value => input_value,
      output_value => output_value,
      total_value => total_value);
end architecture;

entity package_wrapper is
end entity;
use work.procedure_pkg.all;
architecture rtl of package_wrapper is
  signal input_value : integer;
  signal output_value : integer;
  signal total_value : integer;
begin
  nested: entity work.procedure_leaf(rtl)
    generic map (package_exchange)
    port map (
      input_value => input_value,
      output_value => output_value,
      total_value => total_value);
end architecture;

entity procedure_top is
end entity;
architecture rtl of procedure_top is
  procedure exchange(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
    variable temporary : integer := source;
  begin
    destination := temporary + 1;
    accumulator := accumulator + destination;
  end procedure;
  signal direct_input : integer;
  signal direct_output : integer;
  signal direct_total : integer;
  signal boxed_input : integer;
  signal boxed_output : integer;
  signal boxed_total : integer;
begin
  direct_instance: entity work.procedure_leaf(rtl)
    generic map (selected => exchange)
    port map (
      input_value => direct_input,
      output_value => direct_output,
      total_value => direct_total);
  nested_instance: entity work.procedure_wrapper(rtl)
    generic map (exchange)
    port map ();
  boxed_instance: entity work.boxed_leaf(rtl)
    port map (
      input_value => boxed_input,
      output_value => boxed_output,
      total_value => boxed_total);
  package_instance: entity work.package_wrapper(rtl)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design,
        "vhdl:work.procedure_top(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->specializations().size() == 7);
    const auto nested = std::ranges::find_if(
        elaborated.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "procedure_top.nested_instance.nested";
        });
    const auto package_actual = std::ranges::find_if(
        elaborated.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "procedure_top.package_instance.nested";
        });
    assert(nested != elaborated.design->specializations().end());
    assert(package_actual
        != elaborated.design->specializations().end());
    assert(!nested->parameter_values.empty());
    assert(nested->parameter_values.front().first == "selected");
    assert(!package_actual->parameter_values.empty());
    assert(package_actual->parameter_values.front().first == "selected");

    const auto missing = fsim::frontend::parse_text(
        "missing-procedure-actual.vhd",
        R"(
entity missing is
  generic (
    procedure required(value : in integer));
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
        missing_result, "FSIM-ELAB-VHPROC-004"));

    const auto invalid = fsim::frontend::parse_text(
        "invalid-procedure-actuals.vhd",
        R"(
entity required_leaf is
  generic (
    procedure selected(
      source : in integer;
      variable destination : out integer));
end entity;
architecture rtl of required_leaf is
begin
end architecture;

entity default_leaf is
  generic (
    procedure selected(value : in integer) is <>);
end entity;
architecture rtl of default_leaf is
begin
end architecture;

entity invalid_top is
end entity;
architecture rtl of invalid_top is
  function function_actual(value : integer) return integer is
  begin
    return value;
  end function;
  procedure wrong_profile(
    source : in bit;
    variable destination : out bit) is
  begin
    destination := source;
  end procedure;
  procedure declared(
    source : in integer;
    variable destination : out integer);
  procedure first_match(value : in integer) is
  begin
    null;
  end procedure;
  procedure second_match(value : in integer) is
  begin
    null;
  end procedure;
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
  function_binding: entity work.required_leaf(rtl)
    generic map (selected => function_actual)
    port map ();
  undefined_binding: entity work.required_leaf(rtl)
    generic map (selected => declared)
    port map ();
  generated_binding: entity work.required_leaf(rtl)
    generic map (selected => generated.actual)
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
        invalid_result, "FSIM-ELAB-VHPROC-003"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPROC-005"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPROC-006"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPROC-007"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPROC-008"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPROC-012"));

    const auto illegal_call = fsim::frontend::parse_text(
        "illegal-procedure-call.vhd",
        R"(
entity illegal_call is
end entity;
architecture rtl of illegal_call is
  procedure write_value(variable value : out integer) is
  begin
    value := 1;
  end procedure;
begin
  process
  begin
    write_value(1);
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(illegal_call.ok());
    const auto illegal_call_result = fsim::elaboration::elaborate(
        illegal_call.design,
        "vhdl:work.illegal_call(rtl)");
    assert(!illegal_call_result.ok());
    assert(has_diagnostic(
        illegal_call_result, "FSIM-ELAB-VHPROC-018"));

    const auto recursive = fsim::frontend::parse_text(
        "recursive-procedure-actual.vhd",
        R"(
entity recursive_leaf is
  generic (procedure selected(value : in integer));
end entity;
architecture rtl of recursive_leaf is
begin
end architecture;

entity recursive_top is
end entity;
architecture rtl of recursive_top is
  procedure recurse(value : in integer) is
  begin
    recurse(value);
  end procedure;
begin
  child: entity work.recursive_leaf(rtl)
    generic map (recurse)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(recursive.ok());
    const auto recursive_result = fsim::elaboration::elaborate(
        recursive.design, "vhdl:work.recursive_top(rtl)");
    assert(recursive_result.ok());

    auto timed = fsim::frontend::parse_text(
        "timed-procedure-actual.vhd",
        R"(
entity timed_leaf is
  generic (procedure selected(value : in integer));
end entity;
architecture rtl of timed_leaf is
begin
end architecture;

entity timed_top is
end entity;
architecture rtl of timed_top is
  procedure actual(value : in integer) is
  begin
    null;
  end procedure;
begin
  child: entity work.timed_leaf(rtl)
    generic map (actual)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(timed.ok());
    auto top_architecture = std::ranges::find_if(
        timed.design.units,
        [](const auto& unit) {
            return unit.kind
                == fsim::frontend::UnitKind::VhdlArchitecture
                && unit.primary_name == "timed_top";
        });
    assert(top_architecture != timed.design.units.end());
    fsim::frontend::Statement delay;
    delay.kind = fsim::frontend::StatementKind::Delay;
    delay.span = top_architecture->procedures.front().span;
    top_architecture->procedures.front().statements.push_back(delay);
    const auto timed_result = fsim::elaboration::elaborate(
        timed.design, "vhdl:work.timed_top(rtl)");
    assert(!timed_result.ok());
    assert(has_diagnostic(
        timed_result, "FSIM-ELAB-VHPROC-009"));

    auto cross_language_parent = fsim::frontend::parse_text(
        "cross-language-procedure.sv",
        R"(
module cross_language_procedure;
  task automatic exchange(input int value);
  endtask
  foreign_target #(.selected(exchange)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto cross_language_child = fsim::frontend::parse_text(
        "cross-language-procedure.vhd",
        R"(
entity foreign_target is
  generic (procedure selected(value : in integer));
end entity;
architecture rtl of foreign_target is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(cross_language_parent.ok()
        && cross_language_child.ok());
    for (auto& unit : cross_language_child.design.units) {
        cross_language_parent.design.units.push_back(
            std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding>
        cross_language_binding { { "cross_language_procedure.child",
            "vhdl:work.foreign_target(rtl)",
            std::nullopt } };
    const auto cross_language_result = fsim::elaboration::elaborate(
        cross_language_parent.design,
        "sv:work.cross_language_procedure",
        cross_language_binding);
    assert(!cross_language_result.ok());
    assert(has_diagnostic(
        cross_language_result, "FSIM-ELAB-VHPROC-002"));
}

} // namespace fsim::tests::elaboration
