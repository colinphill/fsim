// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/path.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace fsim::test {

[[nodiscard]] inline bool same_source_path(
    const std::string_view actual,
    const std::filesystem::path& expected) {
  const auto actual_path = support::path_from_utf8(actual);
  if (actual_path.lexically_normal() == expected.lexically_normal()) {
    return true;
  }
  std::error_code actual_error;
  std::error_code expected_error;
  const auto canonical_actual =
      std::filesystem::weakly_canonical(actual_path, actual_error);
  const auto canonical_expected =
      std::filesystem::weakly_canonical(expected, expected_error);
  return !actual_error && !expected_error
      && canonical_actual == canonical_expected;
}

template <typename Range>
[[nodiscard]] bool has_source_dependency(
    const Range& dependencies,
    const std::filesystem::path& expected) {
  return std::ranges::any_of(
      dependencies,
      [&](const std::string& dependency) {
        return same_source_path(dependency, expected);
      });
}

}  // namespace fsim::test
