// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/path.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

namespace fsim::test {

[[nodiscard]] bool same_source_path(
    std::string_view actual, const std::filesystem::path& expected);

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
