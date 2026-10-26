// SPDX-License-Identifier: Apache-2.0
#include <iostream>

int main() {
  // The SystemC matrix runs as the separate fsim.application.systemc_matrix_*
  // CTest cases so that its parts schedule in parallel. This name stays as a
  // cheap sentinel without executing the same coverage twice.
  std::cerr << "systemc matrix: partitioned cases registered separately\n";
  std::cout << "SystemC application matrix parts are registered separately\n";
  return 0;
}
