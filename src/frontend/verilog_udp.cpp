// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/design.hpp"

#include <algorithm>
#include <unordered_set>

namespace fsim::frontend {
namespace {

[[nodiscard]] bool udp_language_revision_matches(
    const Language language,
    const StandardRevision revision) noexcept {
  if (language == Language::Verilog2005) {
    return revision == StandardRevision::Verilog1995
        || revision == StandardRevision::Verilog2001
        || revision == StandardRevision::Verilog2001NoConfig
        || revision == StandardRevision::Verilog2005;
  }
  if (language == Language::SystemVerilog2017) {
    return revision == StandardRevision::SystemVerilog2005
        || revision == StandardRevision::SystemVerilog2009
        || revision == StandardRevision::SystemVerilog2012
        || revision == StandardRevision::SystemVerilog2017
        || revision == StandardRevision::SystemVerilog2023;
  }
  return false;
}

}  // namespace

bool verilog_udp_table_within_resource_budget(
    const std::size_t input_count,
    const std::size_t row_count) noexcept {
  if (input_count
      > (maximum_udp_table_storage_bytes
         - sizeof(VerilogUdpTableRow))
          / sizeof(VerilogUdpInputPattern)) {
    return false;
  }
  const auto row_bytes = sizeof(VerilogUdpTableRow)
      + input_count * sizeof(VerilogUdpInputPattern);
  return row_bytes != 0
      && row_count <= maximum_udp_table_storage_bytes / row_bytes;
}

bool verilog_udp_declaration_well_formed(
    const VerilogUdpDeclaration& declaration) noexcept {
  const auto valid_level = [](const VerilogUdpLevelSymbol symbol) {
    switch (symbol) {
    case VerilogUdpLevelSymbol::Zero:
    case VerilogUdpLevelSymbol::One:
    case VerilogUdpLevelSymbol::Unknown:
    case VerilogUdpLevelSymbol::DontCare:
    case VerilogUdpLevelSymbol::Binary:
      return true;
    }
    return false;
  };
  const auto valid_edge = [](const VerilogUdpEdgeSymbol symbol) {
    switch (symbol) {
    case VerilogUdpEdgeSymbol::None:
    case VerilogUdpEdgeSymbol::Rising:
    case VerilogUdpEdgeSymbol::Falling:
    case VerilogUdpEdgeSymbol::Positive:
    case VerilogUdpEdgeSymbol::Negative:
    case VerilogUdpEdgeSymbol::Any:
    case VerilogUdpEdgeSymbol::Explicit:
      return true;
    }
    return false;
  };
  const auto valid_output = [](const VerilogUdpOutputSymbol symbol) {
    switch (symbol) {
    case VerilogUdpOutputSymbol::Zero:
    case VerilogUdpOutputSymbol::One:
    case VerilogUdpOutputSymbol::Unknown:
    case VerilogUdpOutputSymbol::NoChange:
      return true;
    }
    return false;
  };
  if (!udp_language_revision_matches(
          declaration.language, declaration.standard_revision)
      || declaration.verilog_compatibility_profile.empty()
      || declaration.name.empty() || declaration.output.empty()
      || declaration.inputs.empty() || declaration.rows.empty()
      || declaration.sequential != declaration.output_reg
      || !verilog_udp_table_within_resource_budget(
          declaration.inputs.size(), declaration.rows.size())) {
    return false;
  }
  if (declaration.initial_output
      && (!declaration.sequential
          || !valid_output(*declaration.initial_output)
          || *declaration.initial_output
              == VerilogUdpOutputSymbol::NoChange)) {
    return false;
  }
  std::unordered_set<std::string> terminals;
  terminals.insert(declaration.output);
  for (const auto& input : declaration.inputs) {
    if (input.empty() || !terminals.insert(input).second) return false;
  }
  const auto same_pattern = [](const VerilogUdpInputPattern& left,
                               const VerilogUdpInputPattern& right) {
    return left.level == right.level && left.edge == right.edge
        && left.previous == right.previous
        && left.current == right.current;
  };
  for (std::size_t index = 0; index < declaration.rows.size(); ++index) {
    const auto& row = declaration.rows[index];
    if (row.inputs.size() != declaration.inputs.size()
        || row.current_state.has_value() != declaration.sequential
        || (row.current_state && !valid_level(*row.current_state))
        || !valid_output(row.output)
        || (!declaration.sequential
            && row.output == VerilogUdpOutputSymbol::NoChange)) {
      return false;
    }
    std::size_t edges{};
    for (const auto& input : row.inputs) {
      if (!valid_level(input.level) || !valid_edge(input.edge)
          || !valid_level(input.previous) || !valid_level(input.current)) {
        return false;
      }
      edges += input.edge != VerilogUdpEdgeSymbol::None;
    }
    if ((!declaration.sequential && edges != 0)
        || (declaration.sequential && edges > 1)) {
      return false;
    }
    for (std::size_t prior = 0; prior < index; ++prior) {
      const auto& earlier = declaration.rows[prior];
      if (row.current_state == earlier.current_state
          && row.inputs.size() == earlier.inputs.size()
          && std::ranges::equal(
              row.inputs, earlier.inputs, same_pattern)) {
        return false;
      }
    }
  }
  return true;
}

bool verilog_udp_level_matches(
    const VerilogUdpLevelSymbol pattern,
    const VerilogUdpLevelSymbol actual) noexcept {
  switch (pattern) {
  case VerilogUdpLevelSymbol::Zero:
    return actual == VerilogUdpLevelSymbol::Zero;
  case VerilogUdpLevelSymbol::One:
    return actual == VerilogUdpLevelSymbol::One;
  case VerilogUdpLevelSymbol::Unknown:
    return actual == VerilogUdpLevelSymbol::Unknown;
  case VerilogUdpLevelSymbol::DontCare:
    return actual == VerilogUdpLevelSymbol::Zero
        || actual == VerilogUdpLevelSymbol::One
        || actual == VerilogUdpLevelSymbol::Unknown;
  case VerilogUdpLevelSymbol::Binary:
    return actual == VerilogUdpLevelSymbol::Zero
        || actual == VerilogUdpLevelSymbol::One;
  }
  return false;
}

bool verilog_udp_input_matches(
    const VerilogUdpInputPattern& pattern,
    const VerilogUdpLevelSymbol previous,
    const VerilogUdpLevelSymbol current) noexcept {
  switch (pattern.edge) {
  case VerilogUdpEdgeSymbol::None:
    return verilog_udp_level_matches(pattern.level, current);
  case VerilogUdpEdgeSymbol::Rising:
    return previous == VerilogUdpLevelSymbol::Zero
        && current == VerilogUdpLevelSymbol::One;
  case VerilogUdpEdgeSymbol::Falling:
    return previous == VerilogUdpLevelSymbol::One
        && current == VerilogUdpLevelSymbol::Zero;
  case VerilogUdpEdgeSymbol::Positive:
    return (previous == VerilogUdpLevelSymbol::Zero
            && (current == VerilogUdpLevelSymbol::One
                || current == VerilogUdpLevelSymbol::Unknown))
        || (previous == VerilogUdpLevelSymbol::Unknown
            && current == VerilogUdpLevelSymbol::One);
  case VerilogUdpEdgeSymbol::Negative:
    return (previous == VerilogUdpLevelSymbol::One
            && (current == VerilogUdpLevelSymbol::Zero
                || current == VerilogUdpLevelSymbol::Unknown))
        || (previous == VerilogUdpLevelSymbol::Unknown
            && current == VerilogUdpLevelSymbol::Zero);
  case VerilogUdpEdgeSymbol::Any:
    return previous != current;
  case VerilogUdpEdgeSymbol::Explicit:
    return verilog_udp_level_matches(pattern.previous, previous)
        && verilog_udp_level_matches(pattern.current, current)
        && previous != current;
  }
  return false;
}

const VerilogUdpTableRow* find_verilog_udp_table_row(
    const VerilogUdpDeclaration& declaration,
    const std::span<const VerilogUdpLevelSymbol> previous_inputs,
    const std::span<const VerilogUdpLevelSymbol> current_inputs,
    const VerilogUdpLevelSymbol current_output) noexcept {
  if (previous_inputs.size() != declaration.inputs.size()
      || current_inputs.size() != declaration.inputs.size()) {
    return nullptr;
  }
  for (const auto& row : declaration.rows) {
    if (row.inputs.size() != current_inputs.size()) continue;
    if (row.current_state
        && !verilog_udp_level_matches(*row.current_state, current_output)) {
      continue;
    }
    bool match = true;
    for (std::size_t index = 0; index < row.inputs.size(); ++index) {
      if (!verilog_udp_input_matches(
              row.inputs[index], previous_inputs[index],
              current_inputs[index])) {
        match = false;
        break;
      }
    }
    if (match) return &row;
  }
  return nullptr;
}

}  // namespace fsim::frontend
