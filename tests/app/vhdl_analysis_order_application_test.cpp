// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

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
    "context.vhd", "libb", R"(
library liba;
use liba.values.all;
context shared is
  library liba;
  use liba.values.all;
end context;
)"};

const Source entity_declaration{
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
    "configuration.vhd", "libb", R"(
configuration leaf_configuration of leaf is
  for rtl
  end for;
end configuration;
)"};

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
  const auto accepted = check(directory.path, "valid", valid);
  assert(accepted.accepted);
  assert(accepted.codes.empty());
  assert((accepted.units == std::vector<std::string>{
      "liba:values", "liba:values", "libb:shared",
      "libb:leaf", "libb:rtl", "libb:leaf_configuration"}));

  expect_code(
      directory.path,
      "architecture-before-entity",
      {architecture_declaration, entity_declaration},
      "FSIM-FE-VHORDER-001");
  expect_code(
      directory.path,
      "body-before-package",
      {package_body, package_declaration},
      "FSIM-FE-VHORDER-002");
  expect_code(
      directory.path,
      "reference-before-context",
      {entity_declaration, context_declaration, package_declaration},
      "FSIM-FE-VHORDER-003");
  expect_code(
      directory.path,
      "use-before-package",
      {context_declaration, package_declaration},
      "FSIM-FE-VHORDER-004");
  expect_code(
      directory.path,
      "configuration-before-entity",
      {configuration_declaration, entity_declaration,
       architecture_declaration},
      "FSIM-FE-VHORDER-005");
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

  return 0;
}
