// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <limits>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error{message};
}

template <typename Exception, typename Callback>
void require_throws(Callback&& callback, const char* message) {
  try {
    callback();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error{message};
}

}  // namespace

void test_systemverilog_byte_strings() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const std::string original{"A\xcf\x80\xf0\x9f\x98\x80"};
  require(
      systemverilog_string_length(original) == 7
          && systemverilog_string_at(original, 1) == 0xcf
          && systemverilog_string_at(original, 6) == 0x80
          && systemverilog_string_slice(original, 1, 2) == "\xcf\x80",
      "length, indexing, and inclusive slicing use stored bytes");

  auto replaced = original;
  systemverilog_string_replace(
      replaced, 1, 0x1f642, maximum_string_bytes);
  require(
      replaced == "AB\x80\xf0\x9f\x98\x80"
          && systemverilog_string_length(replaced) == 7,
      "assignment stores the low eight bits without changing length");
  auto bounded = std::string(maximum_string_bytes, 'a');
  systemverilog_string_replace(
      bounded, 0, 0x1f642, maximum_string_bytes);
  require(
      bounded.size() == maximum_string_bytes && bounded.front() == 'B',
      "same-size byte replacement is valid at the resource bound");
  auto oversized = std::string(maximum_string_bytes + 1, 'a');
  const auto before_rejection = oversized;
  require_throws<std::length_error>(
      [&] {
        systemverilog_string_replace(
            oversized, 0, 'B', maximum_string_bytes);
      },
      "replacement rejects an already oversized string");
  require(
      oversized == before_rejection,
      "resource-rejected replacement leaves the original string unchanged");

  require(
      systemverilog_string_compare("\x7f", "\x80") < 0
          && systemverilog_string_compare("\xff", "\x80") > 0
          && systemverilog_string_compare(original, original) == 0,
      "comparison follows unsigned byte order and exact equality");
  const std::string arbitrary_bytes { "\xc0\x80\xed\xa0\x80\xe2\x82", 7 };
  require(
      systemverilog_string_length(arbitrary_bytes) == 7,
      "all bounded eight-bit sequences are valid string values");

  Interpreter byte_interpreter;
  Process byte_process;
  byte_process.id = 0;
  byte_process.name = "arbitrary_bytes";
  byte_process.string_register_count = 1;
  byte_process.debug_string_locals = {
      DebugStringLocal { "bytes", 0, { } },
  };
  byte_process.operations = {
      LoadStringConstant{0, "\xc0\x80"}, Halt{}};
  (void)byte_interpreter.add_process(std::move(byte_process));
  require(
      byte_interpreter.run().status == RunStatus::completed
          && byte_interpreter.read_debug_string_local(0, 0) == "\xc0\x80",
      "interpreter string ingress preserves arbitrary bytes");

  auto methods = original;
  require(
      string_getc(methods, 2) == 0x80
          && string_getc(methods, 99) == 0
          && string_substr(methods, 1, 2) == "\xcf\x80",
      "getc and substr share byte indexing");
  string_putc(methods, 0, 0x1f642);
  require(
      methods == "B\xcf\x80\xf0\x9f\x98\x80"
          && string_compare("A\xcf\x80", "a\xcf\x80", true) == 0
          && string_change_case("\xc3\xa9z", true) == "\xc3\xa9Z"
          && string_to_integer("12\xcf\x80", 10) == 12,
      "method mutation, ASCII folding, and conversion use byte semantics");

  std::string real_source{"1_2.5"};
  const auto parsed_real = execute_string_method(
      StringMethodOperator::atoreal,
      real_source, {}, std::nullopt, std::nullopt);
  require(
      parsed_real.scalar_bits
          && SystemVerilogScalarValue{
                 SystemVerilogScalarKind::Real,
                 *parsed_real.scalar_bits}.as_real()
              == 12.5,
      "atoreal uses the locale-independent scalar scanner");
  std::string real_text;
  (void)execute_string_method(
      StringMethodOperator::realtoa,
      real_text, {}, std::nullopt, std::nullopt,
      parsed_real.scalar_bits);
  require(real_text == "12.5", "realtoa uses canonical real formatting");
  std::string malformed_real{"not-a-real"};
  const auto zero_real = execute_string_method(
      StringMethodOperator::atoreal,
      malformed_real, {}, std::nullopt, std::nullopt);
  require(
      zero_real.scalar_bits
          && SystemVerilogScalarValue{
                 SystemVerilogScalarKind::Real,
                 *zero_real.scalar_bits}.as_real()
              == 0.0,
      "malformed atoreal conversion returns the defined zero value");
  std::string infinite_text;
  (void)execute_string_method(
      StringMethodOperator::realtoa,
      infinite_text, {}, std::nullopt, std::nullopt,
      SystemVerilogScalarValue::real(
          std::numeric_limits<double>::infinity()).bits);
  require(
      infinite_text == "inf",
      "realtoa inherits canonical nonfinite formatting");
}

}  // namespace fsim::tests::runtime
