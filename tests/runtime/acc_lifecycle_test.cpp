// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"
#include "fsim/version.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

void require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

PLI_BYTE8* text(char* value) { return value; }

}  // namespace

int main() {
  acc_close();
  char enabled[] = "true";
  require(acc_error_flag == 0, "close is an idempotent neutral operation");
  require(acc_configure(accEnableArgs, text(enabled)) == 0 &&
              acc_error_flag == 1,
          "configuration requires an initialized ACC lifecycle");

  require(acc_initialize() == 1 && acc_error_flag == 0,
          "initialization publishes one usable lifecycle");
  require(acc_initialize() == 1 && acc_error_flag == 0,
          "repeated initialization is idempotent");
  require(acc_product_type() == accSimulator && acc_error_flag == 0,
          "the product identifies as a simulator");
  require(std::string_view{acc_product_version()} ==
                  std::string{"fsim "} + std::string{fsim::version} &&
              std::string_view{acc_version()} == "IEEE 1364-2005 ACC",
          "product and ACC interface versions are stable");

  std::array<PLI_INT32, 10> items{
      accPathDelayCount,      accPathDelimStr,      accDisplayErrors,
      accDefaultAttr0,        accToHiZDelay,        accEnableArgs,
      accDisplayWarnings,     accDevelopmentVersion, accMapToMipd,
      accMinTypMaxDelays,
  };
  char value[] = "bounded-value";
  for (const auto item : items) {
    require(acc_configure(item, text(value)) == 1 && acc_error_flag == 0,
            "every standardized configuration selector is accepted");
  }

  require(acc_configure(7, text(value)) == 0 && acc_error_flag == 1,
          "an unassigned selector is rejected");
  require(acc_configure(accEnableArgs, nullptr) == 0 && acc_error_flag == 1,
          "a null configuration value is rejected");
  std::string oversized(4097, 'x');
  require(acc_configure(accEnableArgs, oversized.data()) == 0 &&
              acc_error_flag == 1,
          "configuration storage is bounded before publication");
  char control_value[] = {'o', 'k', '\n', '\0'};
  require(acc_configure(accEnableArgs, control_value) == 0 &&
              acc_error_flag == 1,
          "control-bearing configuration values are rejected");
  require(acc_configure(accEnableArgs, text(enabled)) == 1 &&
              acc_error_flag == 0,
          "a rejected update does not poison the next transaction");

  std::vector<std::thread> workers;
  workers.reserve(4);
  for (int worker = 0; worker < 4; ++worker) {
    workers.emplace_back([worker] {
      char worker_value[] = "worker";
      for (int iteration = 0; iteration < 64; ++iteration) {
        const auto item = worker % 2 == 0 ? accDisplayErrors
                                          : accDisplayWarnings;
        (void)acc_configure(item, worker_value);
      }
    });
  }
  for (auto& worker : workers) worker.join();
  require(acc_error_flag == 0,
          "concurrent lifecycle calls are serialized without partial state");

  acc_reset_buffer();
  require(acc_error_flag == 0,
          "buffer reset succeeds only within an active lifecycle");
  acc_close();
  acc_reset_buffer();
  require(acc_error_flag == 1,
          "buffer reset rejects an inactive lifecycle");
  require(acc_initialize() == 1 && acc_error_flag == 0,
          "the lifecycle can restart after close");
  acc_close();
  return 0;
}
