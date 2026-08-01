// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

struct ParsedOutputConversion {
  OutputFormat format{OutputFormat::Decimal};
  std::string prefix;
  bool suppress_leading_zero{};
  std::uint32_t minimum_width{};
  bool left_justify{};
  bool zero_pad{};
};

struct ParsedOutputFormat {
  bool valid{true};
  std::vector<ParsedOutputConversion> conversions;
  std::string trailing_text;
};

[[nodiscard]] inline ParsedOutputFormat
parse_output_format(const std::string_view text) {
  ParsedOutputFormat result;
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
    ParsedOutputConversion parsed;
    parsed.prefix = std::move(literal);
    literal.clear();
    const auto conversion_start = index;
    bool left_justify = false;
    if (text[index] == '-') {
      left_justify = true;
      if (++index >= text.size()) {
        result.valid = false;
        return result;
      }
    }
    const auto width_start = index;
    std::uint64_t width{};
    while (index < text.size()
           && text[index] >= '0' && text[index] <= '9') {
      const auto digit = static_cast<unsigned>(text[index] - '0');
      if (width > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
        result.valid = false;
        return result;
      }
      width = width * 10U + digit;
      ++index;
    }
    const auto width_spelling = text.substr(width_start, index - width_start);
    if (index >= text.size()) {
      result.valid = false;
      return result;
    }
    const auto conversion = text[index] >= 'A' && text[index] <= 'Z'
        ? static_cast<char>(text[index] + ('a' - 'A')) : text[index];
    if (conversion != 'b' && conversion != 'h'
        && conversion != 'x' && conversion != 'o'
        && conversion != 'd' && conversion != 'c'
        && conversion != 's' && conversion != 'm'
        && conversion != 't') {
      result.valid = false;
      return result;
    }
    if (!width_spelling.empty()) {
      if (width_spelling == "0" && !left_justify) {
        parsed.suppress_leading_zero = true;
      } else {
        if (width == 0
            || width > std::numeric_limits<std::uint32_t>::max()) {
          result.valid = false;
          return result;
        }
        parsed.minimum_width = static_cast<std::uint32_t>(width);
        parsed.left_justify = left_justify;
        parsed.zero_pad = !left_justify && width_spelling.front() == '0';
      }
    } else if (left_justify || conversion_start != width_start) {
      result.valid = false;
      return result;
    }
    if ((parsed.suppress_leading_zero || parsed.zero_pad)
        && (conversion == 'c' || conversion == 's'
            || conversion == 'm')) {
      result.valid = false;
      return result;
    }
    parsed.format = conversion == 'b'
        ? OutputFormat::Binary
        : conversion == 'h' || conversion == 'x'
            ? OutputFormat::Hexadecimal
            : conversion == 'o' ? OutputFormat::Octal
            : conversion == 'd' ? OutputFormat::Decimal
            : conversion == 'c' ? OutputFormat::Character
            : conversion == 's' ? OutputFormat::String
            : conversion == 'm' ? OutputFormat::Hierarchy
                                : OutputFormat::Time;
    result.conversions.push_back(std::move(parsed));
  }
  result.trailing_text = std::move(literal);
  return result;
}

}  // namespace fsim::frontend
