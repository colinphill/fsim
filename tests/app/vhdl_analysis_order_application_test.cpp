// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Source {
  std::string name;
  std::string library;
  std::string text;
};

struct CheckResult {
  bool accepted{};
  std::vector<std::string> codes;
  std::vector<std::string> units;
};

CheckResult check(
    const std::filesystem::path& directory,
    const std::string_view case_name,
    const std::vector<Source>& sources) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = std::string{case_name};
  config.build.jobs = 8;

  for (std::size_t index = 0; index < sources.size(); ++index) {
    const auto path = directory
        / (std::string{case_name} + "-" + std::to_string(index)
           + "-" + sources[index].name);
    std::ofstream output(path, std::ios::binary);
    output << sources[index].text;
    assert(output.good());

    fsim::project::SourceSet source_set;
    source_set.language = fsim::project::Language::vhdl;
    source_set.standard = "2008";
    source_set.library = sources[index].library;
    source_set.compilation_unit = "file";
    source_set.files.push_back(path);
    config.source_sets.push_back(std::move(source_set));
  }

  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  CheckResult result;
  result.accepted = checked.has_value();
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    result.codes.push_back(diagnostic.code);
  }
  if (checked) {
    for (const auto& unit : checked->parsed.units) {
      result.units.push_back(unit.library + ":" + unit.name);
    }
  }
  return result;
}

void expect_code(
    const std::filesystem::path& directory,
    const std::string_view case_name,
    const std::vector<Source>& sources,
    const std::string_view code) {
  const auto first = check(directory, case_name, sources);
  const auto second = check(directory, case_name, sources);
  assert(!first.accepted);
  assert(first.codes == second.codes);
  assert(std::ranges::find(first.codes, code) != first.codes.end());
}

const Source package_declaration{
    // FSIM-CONFORMANCE CF-VHDL-LIB-001 source=SRC-IEEE-P1076 expectation=accept
    // FSIM-CONFORMANCE CF-VHDL-DECL-001 source=SRC-IEEE-P1076 expectation=accept
    // FSIM-CONFORMANCE CF-VHDL-TYPE-001 source=SRC-UVVM expectation=accept
    "package.vhd", "liba", R"(
package values is
  constant amount : integer := 7;
end package;
)"};

const Source package_body{
    "package_body.vhd", "liba", R"(
package body values is
end package body;
)"};

const Source context_declaration{
    // FSIM-CONFORMANCE CF-VHDL-CONTEXT-001 source=SRC-IEEE-P1076 expectation=accept
    "context.vhd", "libb", R"(
library liba;
use liba.values.all;
context shared is
  library liba;
  use liba.values.all;
end context;
)"};

const Source entity_declaration{
    // FSIM-CONFORMANCE CF-VHDL-GENERIC-001 source=SRC-UVVM expectation=accept
    "entity.vhd", "libb", R"(
context work.shared;
entity leaf is
end entity;
)"};

const Source architecture_declaration{
    "architecture.vhd", "libb", R"(
architecture rtl of leaf is
begin
end architecture;
)"};

const Source configuration_declaration{
    // FSIM-CONFORMANCE CF-VHDL-CONFIG-001 source=SRC-IEEE-P1076 expectation=accept
    "configuration.vhd", "libb", R"(
configuration leaf_configuration of leaf is
  for rtl
  end for;
end configuration;
)"};

void test_structural_conformance() {
  // This compact fixture is independently authored from the bounded
  // structural expectations identified by SRC-IEEE-P1076 and SRC-UVVM.
  // FSIM-CONFORMANCE CF-VHDL-COMPONENT-001 source=SRC-UVVM expectation=accept
  // FSIM-CONFORMANCE CF-VHDL-GENERATE-001 source=SRC-IEEE-P1076 expectation=accept
  const auto parsed = fsim::frontend::parse_text(
      "structural-conformance.vhd",
      R"(package structural_types is
  subtype element_t is integer range 0 to 15;
  constant lane_count : integer := 2;
end package structural_types;

context structural_context is
  library work;
  use work.structural_types.all;
end context structural_context;

context work.structural_context;
entity structural_leaf is
  generic (offset : element_t := 1);
  port (
    value : in integer;
    observed : out integer);
end entity structural_leaf;

context work.structural_context;
architecture rtl of structural_leaf is
begin
  observed <= value + offset;
end architecture rtl;

context work.structural_context;
entity structural_top is
  generic (lanes : integer := lane_count);
end entity structural_top;

context work.structural_context;
architecture structure of structural_top is
  component structural_leaf is
    generic (offset : element_t := 1);
    port (
      value : in integer;
      observed : out integer);
  end component structural_leaf;
  for all : structural_leaf
    use entity work.structural_leaf(rtl);
begin
  generated: for lane in 0 to lanes - 1 generate
    signal observed : integer;
  begin
    child: structural_leaf
      generic map (offset => lane + 1)
      port map (value => lane, observed => observed);
  end generate generated;
end architecture structure;

configuration structural_selected of structural_top is
  for structure
    for generated(0)
      for all : structural_leaf
        use entity work.structural_leaf(rtl);
      end for;
    end for;
  end for;
end configuration structural_selected;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  assert(parsed.design.units.size() == 7);
  assert(parsed.design.find(
      fsim::frontend::UnitKind::VhdlPackage,
      "structural_types"));
  assert(parsed.design.find(
      fsim::frontend::UnitKind::VhdlContext,
      "structural_context"));
  assert(parsed.design.find(
      fsim::frontend::UnitKind::VhdlConfiguration,
      "structural_selected"));

  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "vhdl:work.structural_top(structure)");
  assert(elaborated.ok());
  assert(elaborated.design->find_signal(
      "structural_top.generated[0].observed"));
  assert(elaborated.design->find_signal(
      "structural_top.generated[1].observed"));
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-analysis-order-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const std::vector<Source> valid{
      package_declaration,
      package_body,
      context_declaration,
      entity_declaration,
      architecture_declaration,
      configuration_declaration,
  };
  // FSIM-CONFORMANCE CF-VHDL-ORDER-P01 source=SRC-IEEE-P1076 expectation=accept
  const auto accepted = check(directory.path, "valid", valid);
  assert(accepted.accepted);
  assert(accepted.codes.empty());
  assert((accepted.units == std::vector<std::string>{
      "liba:values", "liba:values", "libb:shared",
      "libb:leaf", "libb:rtl", "libb:leaf_configuration"}));

  // FSIM-CONFORMANCE CF-VHDL-ORDER-N01 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-001
  expect_code(
      directory.path,
      "architecture-before-entity",
      {architecture_declaration, entity_declaration},
      "FSIM-FE-VHORDER-001");
  // FSIM-CONFORMANCE CF-VHDL-ORDER-N02 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-002
  expect_code(
      directory.path,
      "body-before-package",
      {package_body, package_declaration},
      "FSIM-FE-VHORDER-002");
  // FSIM-CONFORMANCE CF-VHDL-ORDER-N03 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-003
  expect_code(
      directory.path,
      "reference-before-context",
      {entity_declaration, context_declaration, package_declaration},
      "FSIM-FE-VHORDER-003");
  // FSIM-CONFORMANCE CF-VHDL-ORDER-N04 source=SRC-UVVM expectation=FSIM-FE-VHORDER-004
  expect_code(
      directory.path,
      "use-before-package",
      {context_declaration, package_declaration},
      "FSIM-FE-VHORDER-004");
  // FSIM-CONFORMANCE CF-VHDL-ORDER-N05 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-005
  expect_code(
      directory.path,
      "configuration-before-entity",
      {configuration_declaration, entity_declaration,
       architecture_declaration},
      "FSIM-FE-VHORDER-005");
  // FSIM-CONFORMANCE CF-VHDL-ORDER-N06 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-006
  expect_code(
      directory.path,
      "configuration-before-architecture",
      {entity_declaration, configuration_declaration,
       architecture_declaration, context_declaration,
       package_declaration},
      "FSIM-FE-VHORDER-006");

  const Source entity_binding{
      "entity_binding.vhd", "libb", R"(
entity host is
end entity;
architecture rtl of host is
  component leaf is
  end component;
  for all : leaf use entity work.leaf(rtl);
begin
end architecture;
)"};
  // FSIM-CONFORMANCE CF-VHDL-ORDER-N07 source=SRC-UVVM expectation=FSIM-FE-VHORDER-007
  expect_code(
      directory.path,
      "binding-before-entity",
      {entity_binding, entity_declaration, architecture_declaration,
       context_declaration, package_declaration},
      "FSIM-FE-VHORDER-007");

  const Source configuration_binding{
      "configuration_binding.vhd", "libb", R"(
entity host is
end entity;
architecture rtl of host is
  component leaf is
  end component;
  for all : leaf use configuration work.leaf_configuration;
begin
end architecture;
)"};
  // FSIM-CONFORMANCE CF-VHDL-ORDER-N08 source=SRC-UVVM expectation=FSIM-FE-VHORDER-008
  expect_code(
      directory.path,
      "binding-before-configuration",
      {package_declaration, context_declaration, entity_declaration,
       architecture_declaration, configuration_binding,
       configuration_declaration},
      "FSIM-FE-VHORDER-008");

  const Source direct_configuration_instance{
      "direct_configuration_instance.vhd", "libb", R"(
entity configuration_host is
end entity;
architecture rtl of configuration_host is
begin
  child: configuration work.leaf_configuration;
end architecture;
)"};
  expect_code(
      directory.path,
      "instance-before-configuration",
      {direct_configuration_instance, configuration_declaration,
       entity_declaration, architecture_declaration,
       context_declaration, package_declaration},
      "FSIM-FE-VHORDER-008");

  test_structural_conformance();

  return 0;
}
