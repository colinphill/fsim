// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/simir.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] unsigned char fold_ascii(
    const unsigned char value,
    const bool case_insensitive) noexcept {
  return case_insensitive && value >= 'A' && value <= 'Z'
      ? static_cast<unsigned char>(value + ('a' - 'A'))
      : value;
}

}  // namespace

std::uint8_t string_getc(
    const std::string_view value,
    const std::optional<std::int32_t> index) noexcept {
  return index && *index >= 0
          && static_cast<std::size_t>(*index) < value.size()
      ? static_cast<std::uint8_t>(value[static_cast<std::size_t>(*index)])
      : std::uint8_t{};
}

std::int32_t string_compare(
    const std::string_view lhs,
    const std::string_view rhs,
    const bool case_insensitive) noexcept {
  const auto count = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < count; ++index) {
    const auto left = fold_ascii(
        static_cast<unsigned char>(lhs[index]), case_insensitive);
    const auto right = fold_ascii(
        static_cast<unsigned char>(rhs[index]), case_insensitive);
    if (left != right) {
      return left < right ? -1 : 1;
    }
  }
  return lhs.size() == rhs.size()
      ? 0 : lhs.size() < rhs.size() ? -1 : 1;
}

std::string string_change_case(
    const std::string_view value,
    const bool uppercase) {
  std::string result{value};
  for (auto& character : result) {
    const auto byte = static_cast<unsigned char>(character);
    if (uppercase && byte >= 'a' && byte <= 'z') {
      character = static_cast<char>(byte - ('a' - 'A'));
    } else if (!uppercase && byte >= 'A' && byte <= 'Z') {
      character = static_cast<char>(byte + ('a' - 'A'));
    }
  }
  return result;
}

std::string string_substr(
    const std::string_view value,
    const std::optional<std::int32_t> first,
    const std::optional<std::int32_t> last) {
  if (!first || !last || *first < 0 || *last < *first
      || static_cast<std::size_t>(*last) >= value.size()) {
    return {};
  }
  return std::string{value.substr(
      static_cast<std::size_t>(*first),
      static_cast<std::size_t>(*last - *first + 1))};
}

void string_putc(
    std::string& value,
    const std::optional<std::int32_t> index,
    const std::optional<std::uint8_t> character) noexcept {
  if (index && character && *character != 0 && *index >= 0
      && static_cast<std::size_t>(*index) < value.size()) {
    value[static_cast<std::size_t>(*index)] =
        static_cast<char>(*character);
  }
}

std::int32_t string_to_integer(
    const std::string_view value,
    const unsigned radix) noexcept {
  std::uint32_t result{};
  bool negative{};
  bool started{};
  for (const auto character : value) {
    if (character == '_') {
      continue;
    }
    if (!started && radix == 10
        && (character == '+' || character == '-')) {
      negative = character == '-';
      started = true;
      continue;
    }
    unsigned digit{};
    if (character >= '0' && character <= '9') {
      digit = static_cast<unsigned>(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = static_cast<unsigned>(character - 'a') + 10U;
    } else if (character >= 'A' && character <= 'F') {
      digit = static_cast<unsigned>(character - 'A') + 10U;
    } else {
      break;
    }
    if (digit >= radix) {
      break;
    }
    started = true;
    result = result * radix + digit;
  }
  return static_cast<std::int32_t>(negative ? 0U - result : result);
}

std::string string_from_integer(
    const std::int32_t value,
    const unsigned radix) {
  std::array<char, 35> buffer{};
  const auto converted = radix == 10
      ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), value)
      : std::to_chars(
            buffer.data(), buffer.data() + buffer.size(),
            static_cast<std::uint32_t>(value), static_cast<int>(radix));
  return converted.ec == std::errc{}
      ? std::string{buffer.data(), converted.ptr} : std::string{};
}

StringMethodResult execute_string_method(
    const StringMethodOperator operation,
    std::string& source,
    const std::string_view argument,
    const std::optional<std::int32_t> first,
    const std::optional<std::int32_t> second) {
  if (operation == StringMethodOperator::getc) {
    return {{string_getc(source, first)}, std::nullopt};
  }
  if (operation == StringMethodOperator::putc) {
    string_putc(
        source, first,
        second ? std::optional<std::uint8_t>{
                     static_cast<std::uint8_t>(*second)}
               : std::nullopt);
    return {};
  }
  if (operation == StringMethodOperator::toupper
      || operation == StringMethodOperator::tolower) {
    return {
        std::nullopt,
        string_change_case(
            source, operation == StringMethodOperator::toupper)};
  }
  if (operation == StringMethodOperator::compare
      || operation == StringMethodOperator::icompare) {
    return {{static_cast<std::uint32_t>(string_compare(
                 source, argument,
                 operation == StringMethodOperator::icompare))},
            std::nullopt};
  }
  if (operation == StringMethodOperator::substr) {
    return {std::nullopt, string_substr(source, first, second)};
  }
  if (operation >= StringMethodOperator::atoi
      && operation <= StringMethodOperator::atobin) {
    const auto radix = operation == StringMethodOperator::atoi
        ? 10U : operation == StringMethodOperator::atohex
            ? 16U : operation == StringMethodOperator::atooct ? 8U : 2U;
    return {{static_cast<std::uint32_t>(
                 string_to_integer(source, radix))}, std::nullopt};
  }
  const auto radix = operation == StringMethodOperator::itoa
      ? 10U : operation == StringMethodOperator::hextoa
          ? 16U : operation == StringMethodOperator::octtoa ? 8U : 2U;
  source = string_from_integer(first.value_or(0), radix);
  return {};
}

void execute_string_format(
    const StringMethod& operation,
    std::string& destination,
    const PackedLogic4& packed_value,
    const std::string_view string_value,
    const std::uint64_t tick) {
  std::string formatted;
  if (operation.operation == StringMethodOperator::format_string) {
    formatted = string_value;
    if (formatted.size() < operation.minimum_width) {
      const auto padding =
          static_cast<std::size_t>(operation.minimum_width)
          - formatted.size();
      if (operation.left_justify) {
        formatted.append(padding, ' ');
      } else {
        formatted.insert(0U, padding, ' ');
      }
    }
  } else if (operation.operation == StringMethodOperator::format_time) {
    formatted = make_time_output(
        {}, {}, tick, operation.minimum_width,
        operation.left_justify, operation.zero_pad);
  } else {
    formatted = make_formatted_output(
        {}, {}, operation.format, packed_value,
        operation.signed_decimal,
        operation.suppress_leading_zero,
        operation.minimum_width,
        operation.left_justify,
        operation.zero_pad);
  }
  if (formatted.size() > maximum_string_bytes - destination.size()) {
    throw std::length_error{
        "formatted string exceeds 4096-byte limit"};
  }
  destination += formatted;
}

void Interpreter::Impl::execute_string(
    ProcessState& process,
    const StringMethod& operation) {
  auto& source = get_string_register(process, operation.source);
  if (operation.operation >= StringMethodOperator::format_packed) {
    const auto packed =
        operation.operation == StringMethodOperator::format_packed
            ? get_register(process, operation.first)
            : PackedLogic4{1, Logic4::zero};
    execute_string_format(
        operation, source, packed,
        operation.operation == StringMethodOperator::format_string
            ? std::string_view{get_string_register(
                  process, operation.argument)}
            : std::string_view{},
        scheduler.now());
    ++process.pc;
    return;
  }
  const auto signed32 = [&](const RegisterId register_id)
      -> std::optional<std::int32_t> {
    const auto word = get_register(process, register_id).low_word();
    return word.bval == 0
        ? std::optional<std::int32_t>{static_cast<std::int32_t>(
              static_cast<std::uint32_t>(word.aval))}
        : std::nullopt;
  };
  const bool compare = operation.operation == StringMethodOperator::compare
      || operation.operation == StringMethodOperator::icompare;
  const bool has_first = operation.operation == StringMethodOperator::getc
      || operation.operation == StringMethodOperator::putc
      || operation.operation == StringMethodOperator::substr
      || (operation.operation >= StringMethodOperator::itoa
          && operation.operation <= StringMethodOperator::bintoa);
  const bool has_second = operation.operation == StringMethodOperator::putc
      || operation.operation == StringMethodOperator::substr;
  const auto result = execute_string_method(
      operation.operation, source,
      compare
          ? std::string_view{get_string_register(process, operation.argument)}
          : std::string_view{},
      has_first ? signed32(operation.first) : std::nullopt,
      has_second ? signed32(operation.second) : std::nullopt);
  if (result.integer) {
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(
            operation.operation == StringMethodOperator::getc ? 8 : 32,
            *result.integer, 0);
  }
  if (result.string) {
    get_string_register(process, operation.string_destination) =
        *result.string;
  }
  ++process.pc;
}

}  // namespace fsim::runtime::simir
