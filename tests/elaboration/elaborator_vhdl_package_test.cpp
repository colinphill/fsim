// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_interface_package_generics() {
    auto parsed = fsim::frontend::parse_text(
        "vhdl-package-generics.vhd",
        R"(
package math_template is
  generic (bias : integer := 1);
  constant selected_value : integer := bias + 1;
  function adjust(value : integer) return integer;
end package;

package body math_template is
  function adjust(value : integer) return integer is
  begin
    return value + selected_value;
  end function;
end package body;

package service_template is
  generic (
    type item_type;
    function transform(value : item_type) return item_type;
    procedure observe(value : item_type));
  subtype exported_type is item_type;
  function apply(value : item_type) return item_type;
  procedure publish(value : item_type);
end package;

package body service_template is
  function apply(value : item_type) return item_type is
  begin
    return transform(value);
  end function;
  procedure publish(value : item_type) is
  begin
    observe(value);
  end procedure;
end package body;

entity package_leaf is
  generic (
    package api is new work.math_template
      generic map (<>));
end entity;
architecture rtl of package_leaf is
  signal input_value : integer;
begin
  process(input_value)
    variable result : integer;
  begin
    result := api.adjust(input_value) + api.selected_value;
  end process;
end architecture;

entity package_wrapper is
  generic (
    package forwarded is new work.math_template
      generic map (<>));
end entity;
architecture rtl of package_wrapper is
begin
  nested: entity work.package_leaf(rtl)
    generic map (api => forwarded)
    port map ();
end architecture;

entity fixed_package_leaf is
  generic (
    package api is new work.math_template
      generic map (bias => 4));
end entity;
architecture rtl of fixed_package_leaf is
begin
end architecture;

entity default_package_leaf is
  generic (
    package api is new work.math_template
      generic map (bias => <>));
end entity;
architecture rtl of default_package_leaf is
begin
end architecture;

entity service_leaf is
  generic (
    package api is new work.service_template
      generic map (<>));
end entity;
architecture rtl of service_leaf is
  signal input_value : integer;
begin
  process(input_value)
    variable result : api.exported_type;
  begin
    result := api.apply(input_value);
    api.publish(result);
  end process;
end architecture;

entity dependent_service_leaf is
  generic (
    type bound_type;
    function bound_transform(value : bound_type)
      return bound_type;
    procedure bound_observe(value : bound_type);
    package api is new work.service_template
      generic map (
        item_type => bound_type,
        transform => bound_transform,
        observe => bound_observe));
end entity;
architecture rtl of dependent_service_leaf is
begin
end architecture;

entity package_top is
end entity;
architecture rtl of package_top is
  package selected is new work.math_template
    generic map (bias => 4);
  package default_selected is new work.math_template
    generic map (<>);
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure sink(value : integer) is
  begin
    null;
  end procedure;
  package service is new work.service_template
    generic map (
      item_type => integer,
      transform => increment,
      observe => sink);
  signal source_value : integer;
begin
  local_use: process(source_value)
    variable result : service.exported_type;
  begin
    result :=
      service.apply(source_value)
      + selected.selected_value;
    service.publish(result);
  end process;
  direct_instance: entity work.package_leaf(rtl)
    generic map (selected)
    port map ();
  nested_instance: entity work.package_wrapper(rtl)
    generic map (forwarded => selected)
    port map ();
  fixed_instance: entity work.fixed_package_leaf(rtl)
    generic map (api => selected)
    port map ();
  default_instance: entity work.default_package_leaf(rtl)
    generic map (api => default_selected)
    port map ();
  service_instance: entity work.service_leaf(rtl)
    generic map (api => service)
    port map ();
  dependent_instance: entity work.dependent_service_leaf(rtl)
    generic map (
      bound_type => integer,
      bound_transform => increment,
      bound_observe => sink,
      api => service)
    port map ();
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
        parsed.design, "vhdl:work.package_top(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->specializations().size() == 8);
    const auto direct = std::ranges::find_if(
        elaborated.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "package_top.direct_instance";
        });
    const auto nested = std::ranges::find_if(
        elaborated.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "package_top.nested_instance.nested";
        });
    assert(direct != elaborated.design->specializations().end());
    assert(nested != elaborated.design->specializations().end());
    assert(std::ranges::any_of(
        direct->parameter_identity_values,
        [](const auto& value) {
            return value.first == "api"
                && value.second.find("work.math_template")
                    != std::string::npos;
        }));
    assert(std::ranges::any_of(
        nested->parameter_identity_values,
        [](const auto& value) {
            return value.first == "api"
                && value.second.find("bias=4")
                    != std::string::npos;
        }));

    const auto missing = fsim::frontend::parse_text(
        "missing-package-actual.vhd",
        R"(
package template is
  generic (value : integer := 1);
  constant item : integer := value;
end package;
entity missing is
  generic (
    package required is new work.template generic map (<>));
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
        missing_result, "FSIM-ELAB-VHPKG-004"));

    const auto invalid = fsim::frontend::parse_text(
        "invalid-package-actuals.vhd",
        R"(
package first_template is
  generic (value : integer := 1);
  constant item : integer := value;
end package;
package second_template is
  generic (value : integer := 1);
  constant item : integer := value;
end package;
package nongeneric is
  constant item : integer := 1;
end package;
package incomplete_template is
  generic (value : integer := 1);
  function missing(value : integer) return integer;
end package;
package duplicate_template is
  generic (value : integer := 1);
  constant item : integer := value;
end package;
package duplicate_template is
  generic (value : integer := 2);
  constant item : integer := value;
end package;
use work.cycle_helper.all;
package cycle_template is
  generic (value : integer := 1);
  constant item : integer := value;
end package;
use work.cycle_template.all;
package cycle_helper is
  constant helper : integer := 1;
end package;
entity package_target is
  generic (
    package required is new work.first_template
      generic map (<>));
end entity;
architecture rtl of package_target is
begin
end architecture;
entity fixed_target is
  generic (
    package required is new work.first_template
      generic map (value => 3));
end entity;
architecture rtl of fixed_target is
begin
end architecture;
entity invalid_top is
end entity;
architecture rtl of invalid_top is
  package wrong_template is new work.second_template
    generic map (value => 2);
  package wrong_map is new work.first_template
    generic map (value => 2);
  package unspecialized is new work.nongeneric
    generic map (<>);
  package incomplete is new work.incomplete_template
    generic map (<>);
  package ambiguous is new work.duplicate_template
    generic map (<>);
  package cyclic is new work.cycle_template
    generic map (value => 1);
  function wrong_kind(value : integer) return integer is
  begin
    return value;
  end function;
begin
  wrong_template_instance: entity work.package_target(rtl)
    generic map (wrong_template)
    port map ();
  invisible_instance: entity work.package_target(rtl)
    generic map (absent)
    port map ();
  wrong_kind_instance: entity work.package_target(rtl)
    generic map (wrong_kind)
    port map ();
  expression_instance: entity work.package_target(rtl)
    generic map (1)
    port map ();
  scoped_instance: entity work.package_target(rtl)
    generic map (generated.actual)
    port map ();
  incompatible_instance: entity work.fixed_target(rtl)
    generic map (required => wrong_map)
    port map ();
  unspecialized_actual: entity work.package_target(rtl)
    generic map (required => first_template)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto invalid_result = fsim::elaboration::elaborate(
        invalid.design, "vhdl:work.invalid_top(rtl)");
    assert(!invalid_result.ok());
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-003"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-005"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-006"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-007"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-008"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-011"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-010"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-012"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-013"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHPKG-014"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-PKG-007"));

    auto cross_language_parent = fsim::frontend::parse_text(
        "cross-language-package.sv",
        R"(
module cross_language_package;
  foreign_package_target #(.required(1)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto cross_language_child = fsim::frontend::parse_text(
        "cross-language-package.vhd",
        R"(
package template is
  generic (value : integer := 1);
  constant item : integer := value;
end package;
entity foreign_package_target is
  generic (
    package required is new work.template generic map (<>));
end entity;
architecture rtl of foreign_package_target is
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
            "cross_language_package.child",
            "vhdl:work.foreign_package_target(rtl)",
            std::nullopt}};
    const auto cross_language_result =
        fsim::elaboration::elaborate(
            cross_language_parent.design,
            "sv:work.cross_language_package",
            cross_language_binding);
    assert(!cross_language_result.ok());
    assert(has_diagnostic(
        cross_language_result, "FSIM-ELAB-VHPKG-002"));
}

} // namespace fsim::tests::elaboration
