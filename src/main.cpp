// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

int main(const int argc, char** argv) {
  return fsim::cli::run(argc, argv, fsim::app::make_cli_services());
}
