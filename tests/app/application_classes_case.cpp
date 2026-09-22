// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>

int main() {
  fsim::test::ApplicationTestFixture fixture;
  fixture.test_hir_first_class_visibility();
  fixture.test_class_simulation_integration();
  std::cout << "application class integration tests passed\n";
  return 0;
}
