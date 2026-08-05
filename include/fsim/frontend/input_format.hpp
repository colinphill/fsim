// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

enum class InputScanFormat : std::uint8_t {
  Binary, Octal, Decimal, UnsignedDecimal, Hexadecimal, Character, String,
  Real
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

[[nodiscard]] inline ParsedInputFormat
parse_input_format(const std::string_view text) {
  ParsedInputFormat result;
  std::string literal;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '%') {
      literal.push_back(text[index]);
      continue;
    }
    if (++index >= text.size()) {
      result.valid = false;
      return result;
    }
    if (text[index] == '%') {
      literal.push_back('%');
      continue;
    }
    ParsedInputConversion conversion;
    conversion.prefix = std::move(literal);
    literal.clear();
    if (text[index] == '*') {
      conversion.suppress = true;
      if (++index >= text.size()) {
        result.valid = false;
        return result;
      }
    }
    std::uint64_t width{};
    bool has_width = false;
    while (index < text.size()
           && text[index] >= '0' && text[index] <= '9') {
      has_width = true;
      const auto digit = static_cast<unsigned>(text[index] - '0');
      if (width > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
        result.valid = false;
        return result;
      }
      width = width * 10U + digit;
      ++index;
    }
    if ((has_width && width == 0) || index >= text.size()) {
      result.valid = false;
      return result;
    }
    conversion.maximum_characters = static_cast<std::uint32_t>(width);
    const auto code = text[index] >= 'A' && text[index] <= 'Z'
        ? static_cast<char>(text[index] + ('a' - 'A')) : text[index];
    if (code == 'b') conversion.format = InputScanFormat::Binary;
    else if (code == 'o') conversion.format = InputScanFormat::Octal;
    else if (code == 'd' || code == 'i') conversion.format = InputScanFormat::Decimal;
    else if (code == 'u') conversion.format = InputScanFormat::UnsignedDecimal;
    else if (code == 'h' || code == 'x') conversion.format = InputScanFormat::Hexadecimal;
    else if (code == 'c') conversion.format = InputScanFormat::Character;
    else if (code == 's') conversion.format = InputScanFormat::String;
    else if (code == 'e' || code == 'f' || code == 'g')
      conversion.format = InputScanFormat::Real;
    else {
      result.valid = false;
      return result;
    }
    result.conversions.push_back(std::move(conversion));
  }
  result.trailing_text = std::move(literal);
  return result;
}

}  // namespace fsim::frontend
