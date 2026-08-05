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

void test_systemverilog_unicode_strings() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const std::string original{"A\xcf\x80\xf0\x9f\x98\x80"};
  const auto points = systemverilog_string_code_points(original);
  require(
      points.size() == 3
          && points[0].value == 'A'
          && points[0].byte_offset == 0
          && points[1].value == 0x03c0
          && points[1].byte_offset == 1
          && points[2].value == 0x1f600
          && points[2].byte_offset == 3,
      "UTF-8 iteration yields deterministic Unicode scalar offsets");
  require(
      systemverilog_string_length(original) == 3
          && systemverilog_string_at(original, 1) == 0x03c0
          && systemverilog_string_slice(original, 1, 2)
              == "\xcf\x80\xf0\x9f\x98\x80",
      "length, indexing, and inclusive slicing use code points");

  auto replaced = original;
  systemverilog_string_replace(
      replaced, 1, 0x1f642, maximum_string_bytes);
  require(
      replaced == "A\xf0\x9f\x99\x82\xf0\x9f\x98\x80"
          && systemverilog_string_length(replaced) == 3,
      "assignment replaces one complete code point transactionally");
  const auto before_invalid = replaced;
  require_throws<std::invalid_argument>(
      [&] {
        systemverilog_string_replace(
            replaced, 1, 0xd800, maximum_string_bytes);
      },
      "surrogate replacement must reject");
  require(
      replaced == before_invalid,
      "invalid replacement leaves the original string unchanged");
  auto bounded = std::string(maximum_string_bytes, 'a');
  const auto before_expansion = bounded;
  require_throws<std::length_error>(
      [&] {
        systemverilog_string_replace(
            bounded, 0, 0x1f642, maximum_string_bytes);
      },
      "expanding replacement must honor the byte resource bound");
  require(
      bounded == before_expansion,
      "resource-rejected replacement leaves the original string unchanged");

  require(
      systemverilog_string_compare("\xc3\xa9", "\xc4\x80") < 0
          && systemverilog_string_compare(original, original) == 0,
      "comparison follows Unicode scalar order and exact equality");
  require_throws<std::invalid_argument>(
      [] { (void)systemverilog_string_length("\xc0\x80"); },
      "overlong UTF-8 must reject");
  require_throws<std::invalid_argument>(
      [] { (void)systemverilog_string_length("\xed\xa0\x80"); },
      "UTF-8 surrogate encoding must reject");
  require_throws<std::invalid_argument>(
      [] { (void)systemverilog_string_length("\xf4\x90\x80\x80"); },
      "UTF-8 above U+10FFFF must reject");
  require_throws<std::invalid_argument>(
      [] { (void)systemverilog_string_length("\xe2\x82"); },
      "truncated UTF-8 must reject");

  Interpreter invalid_interpreter;
  Process invalid_process;
  invalid_process.id = 0;
  invalid_process.name = "invalid_utf8";
  invalid_process.string_register_count = 1;
  invalid_process.operations = {
      LoadStringConstant{0, "\xc0\x80"}, Halt{}};
  (void)invalid_interpreter.add_process(std::move(invalid_process));
  require_throws<InterpreterError>(
      [&] { (void)invalid_interpreter.run(); },
      "interpreter string ingress rejects invalid UTF-8");

  auto methods = original;
  require(
      string_getc(methods, 2) == 0x1f600
          && string_getc(methods, 99) == 0
          && string_substr(methods, 1, 2)
              == "\xcf\x80\xf0\x9f\x98\x80",
      "getc and substr share code-point indexing");
  string_putc(methods, 0, 0x1f642);
  require(
      methods == "\xf0\x9f\x99\x82\xcf\x80\xf0\x9f\x98\x80"
          && string_compare("A\xcf\x80", "a\xcf\x80", true) == 0
          && string_change_case("\xc3\xa9z", true) == "\xc3\xa9Z"
          && string_to_integer("12\xcf\x80", 10) == 12,
      "method mutation, comparison, and conversion remain deterministic");

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
