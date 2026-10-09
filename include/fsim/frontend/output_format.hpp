// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

struct ParsedOutputConversion {
  OutputFormat format{OutputFormat::Decimal};
  std::string prefix;
  bool suppress_leading_zero{};
  std::uint32_t minimum_width{};
  bool left_justify{};
  bool zero_pad{};
  // `%.3f`: digits after the point; the maximum value is automatic.
  std::uint32_t precision{std::numeric_limits<std::uint32_t>::max()};
};

struct ParsedOutputFormat {
  bool valid{true};
  std::vector<ParsedOutputConversion> conversions;
  std::string trailing_text;
};

[[nodiscard]] ParsedOutputFormat parse_output_format(std::string_view text);

}  // namespace fsim::frontend
