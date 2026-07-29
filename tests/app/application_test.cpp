// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>

int main() {
  fsim::test::ApplicationTestFixture fixture;
  fixture.test_systemc_integration();
  fixture.test_simulation_semantics();
  fixture.test_specialization_and_packages();
  fixture.test_mixed_language_and_generate();
  fixture.test_preprocessing_debug_and_cli();
  std::cout << "application tests passed\n";
}
