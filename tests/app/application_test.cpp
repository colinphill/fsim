// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>
#include <string_view>

int main() {
  const auto checkpoint =
      [](const std::string_view phase) {
        std::cerr << "application: " << phase << '\n';
      };
  fsim::test::ApplicationTestFixture fixture;
  checkpoint("systemc integration");
  fixture.test_systemc_integration();
  checkpoint("systemc scheduling matrix");
  fixture.test_systemc_scheduling_matrix();
  checkpoint("simulation semantics");
  fixture.test_simulation_semantics();
  checkpoint("specialization and packages");
  fixture.test_specialization_and_packages();
  checkpoint("mixed language and generate");
  fixture.test_mixed_language_and_generate();
  checkpoint("preprocessing debug and cli");
  fixture.test_preprocessing_debug_and_cli();
  checkpoint("complete");
  std::cout << "application tests passed\n";
  return 0;
}
