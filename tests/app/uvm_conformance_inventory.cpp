// SPDX-License-Identifier: Apache-2.0
#include "uvm_conformance_inventory.hpp"

#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef FSIM_TEST_SOURCE_DIR
#error "FSIM_TEST_SOURCE_DIR must name the fsim source tree"
#endif

namespace fsim::tests::app {
namespace {

[[noreturn]] void fail(const std::string_view message) {
  std::cerr << "UVM conformance inventory failure: " << message << '\n';
  std::abort();
}

std::vector<std::string> split(const std::string_view value,
                               const char delimiter) {
  std::vector<std::string> fields;
  std::size_t begin{};
  while (begin <= value.size()) {
    const auto end = value.find(delimiter, begin);
    fields.emplace_back(value.substr(
        begin, end == std::string_view::npos ? value.size() - begin
                                             : end - begin));
    if (end == std::string_view::npos)
      break;
    begin = end + 1;
  }
  return fields;
}

void require_class(const fsim::app::BuiltProject &project,
                   const std::string_view family,
                   const std::string_view class_name) {
  const std::string suffix = "::" + std::string{class_name};
  const auto found = std::ranges::find_if(
      project.systemverilog_class_specializations,
      [&](const auto &specialization) {
        return specialization.declaration_identity.ends_with(suffix);
      });
  if (found == project.systemverilog_class_specializations.end()) {
    fail("supported family " + std::string{family} +
         " is missing retained class " + std::string{class_name});
  }
}

void require_evidence(const std::filesystem::path &source_root,
                      const std::string_view family,
                      const std::string_view evidence) {
  if (evidence.empty() || evidence == "-" ||
      !std::filesystem::is_regular_file(source_root / evidence)) {
    fail("supported family " + std::string{family} +
         " has missing evidence owner " + std::string{evidence});
  }
}

} // namespace

UvmConformanceInventorySummary require_uvm_conformance_inventory(
    const fsim::app::BuiltProject &project, const std::string_view release) {
  if (release != "uvm-1.2" && release != "uvm-2020.3.1")
    fail("unknown governed release " + std::string{release});

  const std::filesystem::path source_root{FSIM_TEST_SOURCE_DIR};
  const auto inventory_path =
      source_root / "tests/feature_matrix/uvm_conformance_inventory.tsv";
  std::ifstream inventory{inventory_path};
  if (!inventory)
    fail("cannot open " + inventory_path.string());

  std::string line;
  if (!std::getline(inventory, line) ||
      line != "# SPDX-License-Identifier: Apache-2.0" ||
      !std::getline(inventory, line) ||
      line != "family\trelease\tboundary\tgoverned_classes\tproject_classes\t"
              "positive_evidence\tnegative_evidence\texecution_evidence") {
    fail("header does not match the frozen schema");
  }

  UvmConformanceInventorySummary summary;
  std::size_t total_rows{};
  std::set<std::string> families;
  std::set<std::string> governed_classes;
  std::set<std::string> project_classes;
  while (std::getline(inventory, line)) {
    if (line.empty())
      continue;
    ++total_rows;
    const auto fields = split(line, '\t');
    if (fields.size() != 8)
      fail("row " + std::to_string(total_rows) + " does not have eight fields");
    const auto &family = fields[0];
    const auto &selected_release = fields[1];
    const auto &boundary = fields[2];
    const auto &governed = fields[3];
    const auto &owned = fields[4];
    const auto &positive = fields[5];
    const auto &negative = fields[6];
    const auto &execution = fields[7];
    if (!families.insert(family).second)
      fail("duplicate family " + family);
    if (boundary != "supported")
      fail("family " + family + " is outside the locked supported boundary");
    if (selected_release != "both" && selected_release != "uvm-1.2" &&
        selected_release != "uvm-2020.3.1") {
      fail("family " + family + " has unknown release " + selected_release);
    }
    require_evidence(source_root, family, positive);
    require_evidence(source_root, family, negative);
    require_evidence(source_root, family, execution);

    if (selected_release != "both" && selected_release != release)
      continue;
    ++summary.families;
    for (const auto &class_name : split(governed, ',')) {
      if (class_name == "-")
        continue;
      if (class_name.empty() || !governed_classes.insert(class_name).second)
        fail("duplicate or empty governed class in family " + family);
      require_class(project, family, class_name);
    }
    for (const auto &class_name : split(owned, ',')) {
      if (class_name == "-")
        continue;
      if (class_name.empty() || !project_classes.insert(class_name).second)
        fail("duplicate or empty project class in family " + family);
      require_class(project, family, class_name);
    }
  }
  summary.governed_classes = governed_classes.size();
  summary.project_classes = project_classes.size();
  const auto expected_governed = release == "uvm-1.2" ? 53u : 56u;
  if (total_rows != 18 || families.size() != 18 || summary.families != 17 ||
      summary.governed_classes != expected_governed ||
      summary.project_classes != 27) {
    std::ostringstream message;
    message << "expected 18 rows, 17 active families, " << expected_governed
            << " governed classes, and 27 project classes; observed "
            << total_rows << '/' << summary.families << '/'
            << summary.governed_classes << '/' << summary.project_classes;
    fail(message.str());
  }
  return summary;
}

void require_uvm_release_closure(const std::string_view release) {
  using Release = fsim::project::SystemVerilogUvmRelease;
  const auto selected = fsim::project::parse_systemverilog_uvm_release(release);
  if (!selected)
    fail("unknown governed release " + std::string{release});

  const std::filesystem::path source_root{FSIM_TEST_SOURCE_DIR};
  const auto matrix_path =
      source_root / "tests/feature_matrix/uvm_release_closure.tsv";
  std::ifstream matrix{matrix_path};
  std::string line;
  if (!matrix || !std::getline(matrix, line) ||
      line != "# SPDX-License-Identifier: Apache-2.0" ||
      !std::getline(matrix, line) ||
      line != "contract\tuvm_1_2\tuvm_2020_3_1\tevidence") {
    fail("release closure schema does not match the frozen contract");
  }

  std::map<std::string, std::array<std::string, 2>> rows;
  while (std::getline(matrix, line)) {
    if (line.empty())
      continue;
    const auto fields = split(line, '\t');
    if (fields.size() != 4 || fields[0].empty() ||
        !rows.emplace(fields[0], std::array{fields[1], fields[2]}).second) {
      fail("release closure contains a malformed or duplicate row");
    }
    require_evidence(source_root, fields[0], fields[3]);
  }
  if (rows.size() != 21)
    fail("release closure does not contain exactly 21 contracts");

  const auto index = *selected == Release::uvm_1_2 ? 0u : 1u;
  const auto require = [&](const std::string_view contract,
                           const std::string_view expected) {
    const auto found = rows.find(std::string{contract});
    if (found == rows.end() || found->second[index] != expected)
      fail("release closure mismatch for " + std::string{contract});
  };
  const auto retained = [](const bool value) {
    return value ? std::string_view{"retained"} : std::string_view{"removed"};
  };
  const auto present = [](const bool value) {
    return value ? std::string_view{"present"} : std::string_view{"absent"};
  };
  const auto compatibility =
      fsim::project::systemverilog_uvm_compatibility(*selected);
  require("canonical_version",
          *selected == Release::uvm_1_2 ? "1.2" : "2020.3");
  require("legacy_global_controls",
          retained(compatibility.legacy_global_controls));
  require("legacy_registration_macros",
          retained(compatibility.legacy_registration_macros));
  require("legacy_component_stop_methods",
          retained(compatibility.legacy_component_stop_methods));
  require("legacy_sequence_library_methods",
          retained(compatibility.legacy_sequence_library_methods));
  require("component_config_compatibility",
          retained(compatibility.component_config_compatibility));
  require("test_done_objection_compatibility",
          retained(compatibility.test_done_objection_compatibility));
  require("ieee_policy_classes", present(compatibility.ieee_policy_classes));
  require("ieee_object_policy_dispatch",
          present(compatibility.ieee_object_policy_dispatch));
  require("ieee_report_summary_methods",
          present(compatibility.ieee_report_summary_methods));
  require("governed_class_inventory", index == 0 ? "53" : "56");
  require("project_class_inventory", "27");
  require("active_supported_families", "17");
  require("exact_stage_count", "10");
  require("maximum_rss_kib", index == 0 ? "4299292" : "4861336");
  require("trace_count", "7");
  require("trace_bytes", "28345");
  require("trace_sha256",
          "62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057");
  require("artifact_provenance", "release+source+artifact");
  require("cache_provenance", "release+source+artifact");
  require("unresolved_supported_gaps", "0");
}

} // namespace fsim::tests::app
