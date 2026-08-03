// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>

int main() {
  fsim::test::ApplicationTestFixture fixture;
  std::cerr << "systemc matrix: integration\n";
  fixture.test_systemc_integration();
  std::cerr << "systemc matrix: scheduling\n";
  fixture.test_systemc_scheduling_matrix();
  std::cerr << "systemc matrix: complete\n";
  std::cout << "SystemC application matrix passed\n";
  return 0;
}
