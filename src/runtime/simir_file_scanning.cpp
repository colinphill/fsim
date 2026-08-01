// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/file_scanning.hpp"

#include "fsim/runtime/simir.hpp"

#include <cctype>
#include <limits>
#include <tuple>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] bool space(const std::int32_t character) noexcept {
  return character >= 0
      && std::isspace(static_cast<unsigned char>(character)) != 0;
}

class Scanner {
public:
  Scanner(
      const std::function<std::int32_t()>& read_value,
      const std::function<void(std::int32_t)>& unread_value)
      : read_(read_value), unread_(unread_value) {}

  [[nodiscard]] std::int32_t read() const { return read_(); }
  void unread(const std::int32_t character) const {
    if (character >= 0) unread_(character);
  }
  [[nodiscard]] bool skip_space() const {
    auto character = read();
    while (space(character)) character = read();
    unread(character);
    return character >= 0;
  }

  enum class Match : std::uint8_t { matched, mismatch, eof };
  [[nodiscard]] Match match(const std::string_view literal) const {
    for (std::size_t index = 0; index < literal.size(); ++index) {
      const auto expected = static_cast<unsigned char>(literal[index]);
      if (space(expected)) {
        while (index + 1U < literal.size()
               && space(static_cast<unsigned char>(literal[index + 1U]))) {
          ++index;
        }
        (void)skip_space();
        continue;
      }
      const auto character = read();
      if (character < 0) return Match::eof;
      if (character != expected) {
        unread(character);
        return Match::mismatch;
      }
    }
    return Match::matched;
  }

private:
  const std::function<std::int32_t()>& read_;
  const std::function<void(std::int32_t)>& unread_;
};

[[nodiscard]] std::uint64_t low_mask(const std::uint32_t width) noexcept {
  return width == 64 ? ~std::uint64_t{0}
                     : (std::uint64_t{1} << width) - 1U;
}

[[nodiscard]] std::optional<InputScanValue> packed_digits(
    std::string token,
    const InputScanFormat format,
    const InputScanTarget& target) {
  bool negative = false;
  if (!token.empty() && (token.front() == '+' || token.front() == '-')) {
    negative = token.front() == '-';
    token.erase(token.begin());
  }
  std::string digits;
  digits.reserve(token.size());
  for (const auto character : token) {
    if (character != '_') digits.push_back(character);
  }
  if (digits.empty()) return std::nullopt;

  const auto make = [&](std::uint64_t aval, std::uint64_t bval) {
    if (negative && bval == 0) aval = 0U - aval;
    if (target.two_state) {
      aval &= ~bval;
      bval = 0;
    }
    return InputScanValue{
        PackedLogic4::from_aval_bval(target.width, aval, bval), {}, false};
  };
  if (format == InputScanFormat::decimal
      || format == InputScanFormat::unsigned_decimal) {
    bool unknown = false;
    bool high_impedance = true;
    std::uint64_t value{};
    for (const auto character : digits) {
      if (character >= '0' && character <= '9') {
        if (unknown) return std::nullopt;
        value = value * 10U + static_cast<unsigned>(character - '0');
      } else if (character == 'x' || character == 'X'
                 || character == '?' || character == 'z'
                 || character == 'Z') {
        unknown = true;
        high_impedance = high_impedance
            && (character == 'z' || character == 'Z');
      } else {
        return std::nullopt;
      }
    }
    if (unknown) {
      const auto mask = low_mask(target.width);
      return make(high_impedance ? 0U : mask, mask);
    }
    return make(value, 0);
  }

  unsigned bits{};
  if (format == InputScanFormat::binary) bits = 1;
  else if (format == InputScanFormat::octal) bits = 3;
  else bits = 4;
  if (format == InputScanFormat::hexadecimal && digits.size() > 2U
      && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    digits.erase(0, 2);
  }
  if (format == InputScanFormat::binary && digits.size() > 2U
      && digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B')) {
    digits.erase(0, 2);
  }
  if (format == InputScanFormat::octal && digits.size() > 2U
      && digits[0] == '0' && (digits[1] == 'o' || digits[1] == 'O')) {
    digits.erase(0, 2);
  }
  if (digits.empty()) return std::nullopt;
  const auto digit_mask = (std::uint64_t{1} << bits) - 1U;
  std::uint64_t aval{}, bval{};
  for (const auto character : digits) {
    aval <<= bits;
    bval <<= bits;
    unsigned digit{};
    if (character >= '0' && character <= '9') {
      digit = static_cast<unsigned>(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = 10U + static_cast<unsigned>(character - 'a');
    } else if (character >= 'A' && character <= 'F') {
      digit = 10U + static_cast<unsigned>(character - 'A');
    } else if (character == 'x' || character == 'X'
               || character == '?' || character == 'z'
               || character == 'Z') {
      bval |= digit_mask;
      if (character != 'z' && character != 'Z') aval |= digit_mask;
      continue;
    } else {
      return std::nullopt;
    }
    if (digit > digit_mask) return std::nullopt;
    aval |= digit;
  }
  return make(aval, bval);
}

[[nodiscard]] InputScanValue packed_text(
    const std::string_view text, const InputScanTarget& target) {
  std::uint64_t value{};
  for (const auto character : text) {
    value = (value << 8U) | static_cast<unsigned char>(character);
  }
  return {
      PackedLogic4::from_aval_bval(target.width, value, 0), {}, false};
}

[[nodiscard]] bool numeric_character(
    const std::int32_t character,
    const InputScanFormat format,
    const bool first) noexcept {
  if (character < 0) return false;
  if (first && (character == '+' || character == '-')) return true;
  if (character == '_' || character == '?' || character == 'x'
      || character == 'X' || character == 'z' || character == 'Z') return true;
  if (character >= '0' && character <= '9') {
    if (format == InputScanFormat::binary) return character <= '1';
    if (format == InputScanFormat::octal) return character <= '7';
    return true;
  }
  return format == InputScanFormat::hexadecimal
      && ((character >= 'a' && character <= 'f')
          || (character >= 'A' && character <= 'F'));
}

[[nodiscard]] std::tuple<std::string, bool, bool> read_conversion_text(
    const Scanner& scanner, const InputScanConversion& conversion) {
  std::string text;
  bool eof = false;
  const auto default_limit = conversion.format == InputScanFormat::character
      ? 1U : static_cast<std::uint32_t>(maximum_string_bytes);
  const auto limit = conversion.maximum_characters == 0
      ? default_limit : conversion.maximum_characters;
  if (conversion.format != InputScanFormat::character) {
    (void)scanner.skip_space();
  }
  while (text.size() < limit) {
    const auto character = scanner.read();
    if (character < 0) {
      eof = true;
      break;
    }
    const bool accepted = conversion.format == InputScanFormat::character
        || (conversion.format == InputScanFormat::string
            ? !space(character)
            : numeric_character(character, conversion.format, text.empty()));
    if (!accepted) {
      scanner.unread(character);
      break;
    }
    text.push_back(static_cast<char>(character));
  }
  const bool complete_character =
      conversion.format != InputScanFormat::character || text.size() == limit;
  return {std::move(text), complete_character, eof};
}

}  // namespace

InputScanResult scan_formatted_input(
    const FileScan& operation,
    const std::function<std::int32_t()>& read,
    const std::function<void(std::int32_t)>& unread) {
  Scanner scanner{read, unread};
  InputScanResult result;
  result.values.resize(operation.conversions.size());
  for (std::size_t index = 0; index < operation.conversions.size(); ++index) {
    const auto& conversion = operation.conversions[index];
    const auto prefix = scanner.match(conversion.prefix);
    if (prefix != Scanner::Match::matched) {
      if (prefix == Scanner::Match::eof && result.assignments == 0)
        result.assignments = -1;
      return result;
    }
    auto [text, complete, eof] = read_conversion_text(scanner, conversion);
    if (text.empty() || !complete) {
      if (eof && result.assignments == 0)
        result.assignments = -1;
      return result;
    }
    std::optional<InputScanValue> value;
    const bool text_format = conversion.format == InputScanFormat::character
        || conversion.format == InputScanFormat::string;
    if (text_format) {
      const bool string_target = conversion.target.kind
              == InputScanTargetKind::string_register
          || conversion.target.kind == InputScanTargetKind::string_object;
      value = string_target
          ? InputScanValue{PackedLogic4{1, Logic4::zero}, std::move(text), true}
          : packed_text(text, conversion.target);
    } else {
      value = packed_digits(std::move(text), conversion.format, conversion.target);
    }
    if (!value) return result;
    if (!conversion.suppress) {
      result.values[index] = std::move(value);
      ++result.assignments;
    }
  }
  (void)scanner.match(operation.trailing_text);
  return result;
}

InputScanResult scan_formatted_string(
    const FileScan& operation, const std::string_view input) {
  std::size_t offset{};
  const std::function<std::int32_t()> read = [&]() {
    return offset < input.size()
        ? static_cast<std::int32_t>(static_cast<unsigned char>(input[offset++]))
        : -1;
  };
  const std::function<void(std::int32_t)> unread = [&](const std::int32_t value) {
    if (value >= 0 && offset > 0) --offset;
  };
  return scan_formatted_input(operation, read, unread);
}

}  // namespace fsim::runtime::simir
