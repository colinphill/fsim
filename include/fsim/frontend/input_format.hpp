// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

enum class InputScanFormat : std::uint8_t {
  Binary, Octal, Decimal, UnsignedDecimal, Hexadecimal, Character, String,
  Real, Unformatted2, Unformatted4
};
struct ParsedInputConversion {
  std::string prefix;
  InputScanFormat format{InputScanFormat::Decimal};
  std::uint32_t maximum_characters{};
  bool suppress{};
};
struct ParsedInputFormat {
  bool valid{true};
  std::vector<ParsedInputConversion> conversions;
  std::string trailing_text;
};

[[nodiscard]] ParsedInputFormat parse_input_format(std::string_view text);

}  // namespace fsim::frontend
