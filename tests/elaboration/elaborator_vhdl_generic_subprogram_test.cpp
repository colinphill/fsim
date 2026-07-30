// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_generic_subprograms() {
    const auto parsed = fsim::frontend::parse_text(
        "generic-subprograms.vhd",
        R"(
package algorithm_pkg is
  generic (amount : integer := 2)
  function package_add(value : integer) return integer;
  function add_four is new package_add
    generic map (amount => 4);
end package;

package body algorithm_pkg is
  generic (amount : integer := 2)
  function package_add(value : integer) return integer is
  begin
    return value + amount;
  end function;
end package body;

entity generic_function_leaf is
  generic (
    function transform(value : integer) return integer);
end entity;
architecture rtl of generic_function_leaf is
begin
  process
    variable result : integer;
  begin
    result := transform(result);
    wait;
  end process;
end architecture;

entity generic_function_wrapper is
  generic (
    function forwarded(value : integer) return integer);
end entity;
architecture rtl of generic_function_wrapper is
begin
  nested: entity work.generic_function_leaf(rtl)
    generic map (transform => forwarded)
    port map ();
end architecture;

entity generic_subprogram_top is
end entity;

use work.algorithm_pkg.all;
architecture rtl of generic_subprogram_top is
  function adjust(value : integer) return integer is
  begin
    return value + 1;
  end function;

  procedure observe(value : integer) is
  begin
    null;
  end procedure;

  generic (
    type item_t;
    amount : integer := 1;
    function apply(value : item_t) return item_t)
  function mapped(value : item_t) return item_t is
  begin
    return apply(value) + amount;
  end function;

  function mapped_two is new mapped
    generic map (
      item_t => integer,
      amount => 2,
      apply => adjust);

  generic (
    amount : integer := 3;
    function apply(value : integer)
      return integer is adjust)
  function defaulted(value : integer) return integer is
  begin
    return apply(value) + amount;
  end function;

  function mapped_default is new defaulted
    generic map (amount => <>, apply => <>);

  generic (amount : integer := 5)
  function whole_default(value : integer) return integer is
  begin
    return value + amount;
  end function;

  function add_five is new whole_default
    generic map (<>);

  generic (
    type item_t;
    amount : integer := 1;
    function apply(value : item_t) return item_t;
    procedure publish(value : item_t))
  procedure mapped_update(variable value : inout item_t) is
  begin
    value := apply(value) + amount;
    publish(value);
  end procedure;

  procedure update_three is new mapped_update
    generic map (
      item_t => integer,
      amount => 3,
      apply => adjust,
      publish => observe);
begin
  process
    variable value : integer;
  begin
    value := mapped_two(1);
    value := mapped_default(value);
    value := add_five(value);
    value := add_four(value);
    update_three(value);
    wait;
  end process;

  direct: entity work.generic_function_leaf(rtl)
    generic map (transform => mapped_two)
    port map ();
  forwarded: entity work.generic_function_wrapper(rtl)
    generic map (forwarded => mapped_two)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    if (!parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ':'
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design,
        "vhdl:work.generic_subprogram_top(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ':'
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(elaborated.ok());
    const auto top = std::ranges::find_if(
        elaborated.design->specializations(),
        [](const auto& specialization) {
          return specialization.instance
              == "generic_subprogram_top";
        });
    assert(top != elaborated.design->specializations().end());
    for (const auto name : {
             "mapped_two",
             "mapped_default",
             "add_five",
             "update_three"}) {
        assert(std::ranges::any_of(
            top->parameter_identity_values,
            [&](const auto& value) {
              return value.first == name
                  && value.second.find(
                         "vhdl-generic-subprogram-v1")
                      != std::string::npos;
            }));
    }
    const auto nested = std::ranges::find_if(
        elaborated.design->specializations(),
        [](const auto& specialization) {
          return specialization.instance
              == "generic_subprogram_top.forwarded.nested";
        });
    assert(
        nested != elaborated.design->specializations().end());
    assert(std::ranges::any_of(
        nested->parameter_identity_values,
        [](const auto& value) {
          return value.first == "transform"
              && value.second.find(
                     "vhdl-generic-subprogram-v1")
                  != std::string::npos;
        }));

    const auto invalid = fsim::frontend::parse_text(
        "invalid-generic-subprograms.vhd",
        R"(
entity invalid_generic_subprograms is
end entity;
architecture rtl of invalid_generic_subprograms is
  function ordinary(value : integer) return integer is
  begin
    return value;
  end function;

  generic (amount : integer := 1)
  function incomplete(value : integer) return integer;

  generic (amount : integer := 1)
  function recursive(value : integer) return integer is
  begin
    return recursive(value) + amount;
  end function;

  generic (amount : integer := 1)
  procedure template_procedure(value : integer) is
  begin
    null;
  end procedure;

  function missing is new absent;
  function wrong_ordinary is new ordinary;
  function wrong_kind is new template_procedure;
  function scoped is new generated.template;
  function incomplete_instance is new incomplete;
  function recursive_instance is new recursive;
begin
  process
    variable result : integer;
  begin
    result := incomplete(1);
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto invalid_result =
        fsim::elaboration::elaborate(
            invalid.design,
            "vhdl:work.invalid_generic_subprograms(rtl)");
    assert(!invalid_result.ok());
    for (const auto code : {
             "FSIM-ELAB-VHGSUB-001",
             "FSIM-ELAB-VHGSUB-003",
             "FSIM-ELAB-VHGSUB-004",
             "FSIM-ELAB-VHGSUB-006",
             "FSIM-ELAB-VHGSUB-007",
             "FSIM-ELAB-VHGSUB-009"}) {
        assert(has_diagnostic(invalid_result, code));
    }

    const auto mismatched = fsim::frontend::parse_text(
        "mismatched-generic-subprogram.vhd",
        R"(
package mismatch_pkg is
  generic (amount : integer := 1)
  function selected(value : integer) return integer;
  function instance is new selected;
end package;
package body mismatch_pkg is
  generic (amount : natural := 1)
  function selected(value : integer) return integer is
  begin
    return value + amount;
  end function;
end package body;
entity mismatch_top is
end entity;
use work.mismatch_pkg.all;
architecture rtl of mismatch_top is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(mismatched.ok());
    const auto mismatch_result =
        fsim::elaboration::elaborate(
            mismatched.design,
            "vhdl:work.mismatch_top(rtl)");
    assert(!mismatch_result.ok());
    assert(has_diagnostic(
        mismatch_result, "FSIM-ELAB-VHGSUB-013"));

    const auto ambiguous = fsim::frontend::parse_text(
        "ambiguous-generic-subprogram.vhd",
        R"(
package left_algorithms is
  generic (amount : integer := 1)
  function selected(value : integer) return integer;
end package;
package body left_algorithms is
  generic (amount : integer := 1)
  function selected(value : integer) return integer is
  begin
    return value + amount;
  end function;
end package body;
package right_algorithms is
  generic (amount : integer := 2)
  function selected(value : integer) return integer;
end package;
package body right_algorithms is
  generic (amount : integer := 2)
  function selected(value : integer) return integer is
  begin
    return value + amount;
  end function;
end package body;
entity ambiguous_generic_subprogram is
end entity;
use work.left_algorithms.all;
use work.right_algorithms.all;
architecture rtl of ambiguous_generic_subprogram is
  function conflict is new selected;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(ambiguous.ok());
    const auto ambiguous_result =
        fsim::elaboration::elaborate(
            ambiguous.design,
            "vhdl:work.ambiguous_generic_subprogram(rtl)");
    assert(!ambiguous_result.ok());
    assert(has_diagnostic(
        ambiguous_result, "FSIM-ELAB-VHGSUB-002"));

    auto cross_language_parent = fsim::frontend::parse_text(
        "cross-language-generic-subprogram.sv",
        R"(
module cross_language_generic_subprogram;
  function automatic int increment(input int value);
    return value + 1;
  endfunction
  foreign_generic_target #(.selected(increment)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto cross_language_child = fsim::frontend::parse_text(
        "cross-language-generic-subprogram.vhd",
        R"(
entity foreign_generic_target is
  generic (
    function selected(value : integer) return integer);
end entity;
architecture rtl of foreign_generic_target is
  generic (
    function apply(value : integer) return integer)
  function invoke(value : integer) return integer is
  begin
    return apply(value);
  end function;
  function local_invoke is new invoke
    generic map (apply => selected);
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
        cross_language_binding{{
            "cross_language_generic_subprogram.child",
            "vhdl:work.foreign_generic_target(rtl)",
            std::nullopt}};
    const auto cross_language_result =
        fsim::elaboration::elaborate(
            cross_language_parent.design,
            "sv:work.cross_language_generic_subprogram",
            cross_language_binding);
    assert(!cross_language_result.ok());
    assert(has_diagnostic(
        cross_language_result, "FSIM-ELAB-VHFUNC-002"));
}

} // namespace fsim::tests::elaboration
