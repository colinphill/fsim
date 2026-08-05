// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {
namespace {

std::optional<VerilogStrength> strength_rank(
    const std::string_view spelling) {
  const auto base = spelling.ends_with('0') || spelling.ends_with('1')
      ? spelling.substr(0, spelling.size() - 1) : spelling;
  if (base == "highz") return VerilogStrength::HighZ;
  if (base == "small") return VerilogStrength::Small;
  if (base == "medium") return VerilogStrength::Medium;
  if (base == "weak") return VerilogStrength::Weak;
  if (base == "large") return VerilogStrength::Large;
  if (base == "pull") return VerilogStrength::Pull;
  if (base == "strong") return VerilogStrength::Strong;
  if (base == "supply") return VerilogStrength::Supply;
  return std::nullopt;
}

bool drive_strength_spelling(const std::string_view spelling) {
  return spelling.size() > 1
      && (spelling.ends_with('0') || spelling.ends_with('1'))
      && strength_rank(spelling).has_value();
}

}  // namespace

bool VerilogParser::verilog_drive_strength_start() const {
  return at(TokenKind::LeftParen) && at(TokenKind::Identifier, 1)
      && drive_strength_spelling(current(1).text);
}

std::optional<VerilogDriveStrength>
VerilogParser::parse_verilog_drive_strength(
    const std::string_view context) {
  if (!verilog_drive_strength_start()) return std::nullopt;
  const auto start = advance();
  const auto first = advance();
  expect(TokenKind::Comma, "',' between Verilog drive strengths",
         "FSIM-SV-PARSE-244");
  const auto second = current();
  if (!at(TokenKind::Identifier)
      || !drive_strength_spelling(second.text)) {
    error(second, "FSIM-SV-PARSE-245",
          "expected a zero/one drive strength in " + std::string{context});
    if (!at(TokenKind::RightParen)) advance();
    expect(TokenKind::RightParen, "')' after Verilog drive strengths",
           "FSIM-SV-PARSE-246");
    return std::nullopt;
  }
  advance();
  expect(TokenKind::RightParen, "')' after Verilog drive strengths",
         "FSIM-SV-PARSE-246");
  const bool first_zero = first.text.ends_with('0');
  const bool second_zero = second.text.ends_with('0');
  const auto first_rank = strength_rank(first.text);
  const auto second_rank = strength_rank(second.text);
  if (!first_rank || !second_rank || first_zero == second_zero) {
    error(start, "FSIM-SV-SEM-148",
          "a Verilog drive-strength pair must contain one zero strength and "
          "one one strength");
    return std::nullopt;
  }
  if (*first_rank == VerilogStrength::HighZ
      && *second_rank == VerilogStrength::HighZ) {
    error(start, "FSIM-SV-SEM-155",
          "a Verilog drive-strength pair cannot make both logic values "
          "high impedance");
    return std::nullopt;
  }
  return VerilogDriveStrength{
      first_zero ? *first_rank : *second_rank,
      first_zero ? *second_rank : *first_rank,
      span_from(start, previous())};
}

std::optional<VerilogChargeStrength>
VerilogParser::parse_verilog_charge_strength(
    const std::string_view context) {
  if (!at(TokenKind::LeftParen) || !at(TokenKind::Identifier, 1)
      || !contains_word({"small", "medium", "large"}, current(1).text)) {
    return std::nullopt;
  }
  const auto start = advance();
  const auto strength = advance();
  expect(TokenKind::RightParen, "')' after Verilog charge strength",
         "FSIM-SV-PARSE-247");
  const auto rank = strength_rank(strength.text);
  if (!rank) {
    error(strength, "FSIM-SV-SEM-149",
          "invalid charge strength in " + std::string{context});
    return std::nullopt;
  }
  return VerilogChargeStrength{*rank, span_from(start, previous())};
}

}  // namespace fsim::frontend
