// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>
#include <string_view>

namespace {

using ApplicationTest =
    void (fsim::test::ApplicationTestFixture::*)();

int run_application_case(
    const std::string_view phase, const ApplicationTest test) {
  std::cerr << "application: " << phase << '\n';
  fsim::test::ApplicationTestFixture fixture;
  (fixture.*test)();
  std::cout << "application " << phase << " tests passed\n";
  return 0;
}

} // namespace

int fsim_application_case_core_simulation() {
  return run_application_case("simulation semantics",
      &fsim::test::ApplicationTestFixture::test_simulation_semantics);
}

int fsim_application_case_core_mixed() {
  return run_application_case("mixed language and generate",
      &fsim::test::ApplicationTestFixture::test_mixed_language_and_generate);
}

int fsim_application_case_core_multiple_roots() {
  return run_application_case("multiple roots",
      &fsim::test::ApplicationTestFixture::test_multiple_roots);
}

int fsim_application_case_core_preprocessing_cli() {
  return run_application_case("preprocessing debug and cli",
      &fsim::test::ApplicationTestFixture::test_preprocessing_debug_and_cli);
}

int fsim_application_case_core_non_project_cli() {
  return run_application_case("non-project cli",
      &fsim::test::ApplicationTestFixture::test_non_project_cli);
}

int fsim_application_case_systemc_matrix_core() {
  return run_application_case("systemc matrix core",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_core);
}

int fsim_application_case_systemc_matrix_hierarchy_1() {
  return run_application_case("systemc matrix hierarchy 1",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_hierarchy_1);
}

int fsim_application_case_systemc_matrix_hierarchy_2() {
  return run_application_case("systemc matrix hierarchy 2",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_hierarchy_2);
}

int fsim_application_case_systemc_matrix_hierarchy_3() {
  return run_application_case("systemc matrix hierarchy 3",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_hierarchy_3);
}

int fsim_application_case_systemc_matrix_hierarchy_4() {
  return run_application_case("systemc matrix hierarchy 4",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_hierarchy_4);
}

int fsim_application_case_systemc_matrix_lifecycle() {
  return run_application_case("systemc matrix lifecycle",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_lifecycle);
}

int fsim_application_case_systemc_matrix_named_objects() {
  return run_application_case("systemc matrix named objects",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_named_objects);
}

int fsim_application_case_systemc_matrix_threads_methods() {
  return run_application_case("systemc matrix threads and methods",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_threads_methods);
}

int fsim_application_case_systemc_matrix_method_tops() {
  return run_application_case("systemc matrix method tops",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_method_tops);
}

int fsim_application_case_systemc_matrix_event_channels() {
  return run_application_case("systemc matrix event channels",
      &fsim::test::ApplicationTestFixture::test_systemc_matrix_event_channels);
}

int fsim_application_case_systemc_matrix_scheduling() {
  return run_application_case("systemc matrix scheduling",
      &fsim::test::ApplicationTestFixture::test_systemc_scheduling_matrix);
}

#if defined(FSIM_MERGED_APPLICATION_TESTS)
int main() {
  // Each phase below is registered as its own selectable CTest case in merged
  // builds. Keep the historical umbrella name as a cheap dispatcher sentinel
  // without executing the same application coverage twice.
  std::cerr << "application: partitioned cases registered separately\n";
#else
int main(const int argc, const char* const argv[]) {
  if (argc == 2
      && std::string_view{argv[1]} == "class_hir_failure_routes") {
    return run_application_case(
        "class HIR failure routes",
        &fsim::test::ApplicationTestFixture::test_class_hir_failure_routes);
  }
  if (argc == 2 && std::string_view{argv[1]} == "class_hir_reports") {
    return run_application_case(
        "class HIR report and exception routes",
        &fsim::test::ApplicationTestFixture::test_class_hir_report_exceptions);
  }
  if (argc != 1) {
    std::cerr << "usage: " << argv[0]
              << " [class_hir_failure_routes|class_hir_reports]\n";
    return 2;
  }
  run_application_case("systemc integration",
      &fsim::test::ApplicationTestFixture::test_systemc_integration);
  run_application_case("systemc scheduling matrix",
      &fsim::test::ApplicationTestFixture::test_systemc_scheduling_matrix);
  fsim_application_case_core_simulation();
  run_application_case("HIR-first class visibility",
      &fsim::test::ApplicationTestFixture::test_hir_first_class_visibility);
  run_application_case("class simulation integration",
      &fsim::test::ApplicationTestFixture::test_class_simulation_integration);
  run_application_case("specialization and packages",
      &fsim::test::ApplicationTestFixture::test_specialization_and_packages);
  fsim_application_case_core_mixed();
  fsim_application_case_core_multiple_roots();
  fsim_application_case_core_preprocessing_cli();
  fsim_application_case_core_non_project_cli();
  run_application_case("artifact phase semantics",
      &fsim::test::ApplicationTestFixture::test_artifact_phase_semantics);
#endif
  std::cerr << "application: complete\n";
  std::cout << "application tests passed\n";
  return 0;
}
