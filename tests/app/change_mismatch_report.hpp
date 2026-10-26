// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>

namespace fsim::test {

// Prints the first difference between two change lists, so a failing
// equality assert says where the engines diverged.
template <typename Changes>
void report_change_mismatch(
    const char* label, const Changes& reference, const Changes& actual)
{
  if (reference == actual) {
    return;
  }
  const auto describe = [](const auto& change) {
    std::ostringstream out;
    if constexpr (requires { change.value; }) {
      if constexpr (requires { change.signal; }) {
        out << change.signal << '=';
      }
      out << change.value << " @" << change.time << '+' << change.delta;
    } else {
      out << std::get<0>(change) << '=' << std::get<1>(change) << " @"
          << std::get<2>(change) << '+' << std::get<3>(change);
    }
    return out.str();
  };
  std::size_t first = 0U;
  while (first < reference.size() && first < actual.size()
         && reference[first] == actual[first]) {
    ++first;
  }
  std::cerr << label << ": changes differ at " << first << " (reference "
            << reference.size() << ", actual " << actual.size() << ")\n";
  const auto from = first < 3U ? 0U : first - 3U;
  for (auto index = from; index < first + 6U; ++index) {
    std::cerr << "  [" << index << "] "
              << (index < reference.size() ? describe(reference[index])
                                           : std::string{"-"})
              << " | "
              << (index < actual.size() ? describe(actual[index])
                                        : std::string{"-"})
              << '\n';
  }
}

} // namespace fsim::test
