// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/environment.hpp"

#include <cstdlib>

namespace fsim::support {

std::optional<std::string> environment_variable(
    const std::string_view name) {
  const std::string terminated_name{name};
#if defined(_WIN32)
  char* raw_value = nullptr;
  std::size_t value_size = 0;
  if (_dupenv_s(
          &raw_value,
          &value_size,
          terminated_name.c_str())
      != 0) {
    return std::nullopt;
  }
  if (raw_value == nullptr) {
    return std::nullopt;
  }
  std::string value{raw_value};
  std::free(raw_value);
  return value;
#else
  const char* raw_value = std::getenv(terminated_name.c_str());
  if (raw_value == nullptr) {
    return std::nullopt;
  }
  return std::string{raw_value};
#endif
}

}  // namespace fsim::support
