// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {
using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

} // namespace

void test_vhdl_package_generics() {
  const auto parsed = parse_text(
      "package_generics.vhd",
      R"(
package arithmetic_template is
  generic (
    scale : integer := 2;
    type value_type;
    function transform(value : integer) return integer;
    procedure observe(value : integer));
  constant initial : integer := scale;
  function apply(value : integer) return integer;
  procedure publish(value : integer);
end package;

package body arithmetic_template is
  function apply(value : integer) return integer is
  begin
    return transform(value) * scale;
  end function;
  procedure publish(value : integer) is
  begin
    observe(value);
  end procedure;
end package body;

entity package_user is
  generic (
    package selected is new work.arithmetic_template
      generic map (<>);
    package fixed is new work.arithmetic_template
      generic map (
        scale => 4,
        value_type => integer,
        transform => plus_one,
        observe => emit);
    package defaulted is new work.arithmetic_template
      generic map (
        scale => <>,
        value_type => integer,
        transform => <>,
        observe => <>));
  package entity_local is new work.arithmetic_template
    generic map (3, integer, plus_one, emit);
end entity;

architecture rtl of package_user is
  package local_math is new work.arithmetic_template
    generic map (
      scale => 5,
      value_type => integer,
      transform => plus_one,
      observe => emit);
begin
end architecture;
)",
      Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  require(parsed.ok(), "bounded VHDL package generic forms must parse");
  require(
      parsed.design.units.size() == 4,
      "generic package declaration, body, entity, and architecture units");

  const auto& generic_package = parsed.design.units.front();
  require(
      generic_package.kind == UnitKind::VhdlPackage
          && generic_package.parameters.size() == 5
          && !generic_package.parameters[0].local
          && generic_package.parameters[0].kind
              == ParameterKind::Value
          && generic_package.parameters[1].kind
              == ParameterKind::Type
          && generic_package.parameters[2].kind
              == ParameterKind::Function
          && generic_package.parameters[3].kind
              == ParameterKind::Procedure
          && generic_package.parameters[4].local,
      "generic package template retains all bounded generic families");

  const auto& entity = parsed.design.units[2];
  require(
      entity.parameters.size() == 3
          && std::ranges::all_of(
              entity.parameters,
              [](const ParameterDeclaration& parameter) {
                return parameter.kind
                        == ParameterKind::Package
                    && parameter.package_profile
                    && parameter.package_profile->template_name
                        == "work.arithmetic_template";
              }),
      "interface package generics retain distinct template profiles");
  require(
      entity.parameters[0].package_profile->generic_map_box
          && entity.parameters[1].package_profile
                 ->generic_map.size() == 4
          && entity.parameters[1].package_profile
                 ->generic_map[0].name
          && *entity.parameters[1].package_profile
                  ->generic_map[0].name
              == "scale"
          && entity.parameters[2].package_profile
                 ->generic_map[0].default_box
          && entity.parameters[2].package_profile
                 ->generic_map[2].default_box,
      "box, explicit, and default package generic maps remain distinct");
  require(
      entity.package_instances.size() == 1
          && entity.package_instances.front().name
              == "entity_local"
          && entity.package_instances.front().generic_map.size()
              == 4,
      "entity-local package instantiation HIR");

  const auto& architecture = parsed.design.units.back();
  require(
      architecture.package_instances.size() == 1
          && architecture.package_instances.front().name
              == "local_math"
          && architecture.package_instances.front().template_name
              == "work.arithmetic_template"
          && architecture.package_instances.front()
                 .generic_map.size() == 4,
      "architecture-local package instantiation HIR");

  const auto invalid = parse_text(
      "invalid_package_generics.vhd",
      R"(
entity invalid_package_generics is
  generic (
    package duplicate is new work.template
      generic map (size => 1, size => 2);
    package ordered is new work.template
      generic map (size => 1, integer));
end entity;
architecture rtl of invalid_package_generics is
  package local_one is new work.template generic map (<>);
  package local_one is new work.template generic map (<>);
begin
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid.ok(),
      "invalid package generic associations must be rejected");
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      has_code("FSIM-VHDL-SEM-057")
          && has_code("FSIM-VHDL-SEM-058")
          && has_code("FSIM-VHDL-SEM-059"),
      "package generic duplicate and association-order diagnostics");

  const auto malformed = parse_text(
      "malformed_package_generics.vhd",
      R"(
entity malformed_package_generics is
  generic (
    package missing_new is work.template generic map (<>);
    package missing_box_end is new work.template
      generic map (<));
end entity;
)",
      Language::Vhdl2008);
  require(
      !malformed.ok(),
      "malformed package generic profiles must recover with diagnostics");
  require(
      std::ranges::any_of(
          malformed.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-PARSE-188"
                && diagnostic.span.source_name
                    == "malformed_package_generics.vhd";
          })
          && std::ranges::any_of(
              malformed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-183";
              }),
      "package generic recovery retains precise source spans");
}

} // namespace fsim::tests::frontend
