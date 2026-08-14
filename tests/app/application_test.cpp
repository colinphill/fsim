// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>
#include <string_view>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FSIM_TEST_ASAN_ENABLED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(FSIM_TEST_ASAN_ENABLED)
#define FSIM_TEST_ASAN_ENABLED 1
#endif

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

int main() {
#if defined(FSIM_TEST_ASAN_ENABLED) && defined(FSIM_MERGED_APPLICATION_TESTS)
  std::cerr << "application: phase-isolated sanitizer cases\n";
#else
#if !defined(FSIM_MERGED_APPLICATION_TESTS)
  run_application_case("systemc integration",
      &fsim::test::ApplicationTestFixture::test_systemc_integration);
  run_application_case("systemc scheduling matrix",
      &fsim::test::ApplicationTestFixture::test_systemc_scheduling_matrix);
#endif
  fsim_application_case_core_simulation();
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
