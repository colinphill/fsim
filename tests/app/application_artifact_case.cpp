// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>

int main() {
  fsim::test::ApplicationTestFixture fixture;
  fixture.test_artifact_phase_semantics();
  std::cout << "application artifact phase tests passed\n";
  return 0;
}
