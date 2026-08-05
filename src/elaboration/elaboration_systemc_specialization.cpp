// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

bool valid_systemc_construction_value(
    const fsim_sc_construction_type_v1 type,
    const std::int64_t value) {
  switch (type) {
  case FSIM_SC_CONSTRUCTION_INTEGER: return true;
  case FSIM_SC_CONSTRUCTION_NATURAL: return value >= 0;
  case FSIM_SC_CONSTRUCTION_POSITIVE: return value > 0;
  case FSIM_SC_CONSTRUCTION_BOOLEAN:
  case FSIM_SC_CONSTRUCTION_BIT: return value == 0 || value == 1;
  }
  return false;
}

std::optional<std::vector<std::pair<std::string, std::int64_t>>>
specialize_systemc_construction(
    const std::vector<SystemCConstructionParameter>& schema,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
  const auto initial_diagnostic_count = diagnostics.size();
  const bool vhdl_association =
      association_language == frontend::Language::Vhdl2008;
  std::vector<std::optional<std::int64_t>> actuals(schema.size());
  std::size_t next_positional = 0;
  bool saw_named = false;
  bool saw_positional = false;
  for (const auto& override : overrides) {
    std::string evaluation_error;
    const auto value = evaluate_constant_expression(
        override.value, parent_environment, evaluation_error);
    if (!value) {
      diagnostics.push_back({
          "FSIM-ELAB-SC-PARAM-004",
          "cannot evaluate SystemC construction actual: " + evaluation_error,
          override.span});
      continue;
    }
    std::optional<std::size_t> index;
    if (override.name) {
      saw_named = true;
      std::vector<std::size_t> matches;
      for (std::size_t candidate = 0; candidate < schema.size(); ++candidate) {
        const auto matches_name = vhdl_association
            ? parameter_name_matches(
                  schema[candidate].name, *override.name,
                  frontend::Language::SystemVerilog2017,
                  association_language)
            : schema[candidate].name == *override.name;
        if (matches_name) matches.push_back(candidate);
      }
      if (matches.size() > 1) {
        diagnostics.push_back({
            "FSIM-ELAB-SC-PARAM-006",
            "VHDL generic name '" + *override.name
                + "' ambiguously matches multiple case-sensitive SystemC "
                  "construction parameters",
            override.span});
        continue;
      }
      if (matches.empty()) {
        diagnostics.push_back({
            "FSIM-ELAB-SC-PARAM-001",
            "unknown SystemC construction parameter '" + *override.name + "'",
            override.span});
        continue;
      }
      index = matches.front();
    } else {
      saw_positional = true;
      if (vhdl_association && saw_named) {
        diagnostics.push_back({
            "FSIM-ELAB-SC-PARAM-003",
            "a positional SystemC construction actual cannot follow a named "
            "VHDL actual",
            override.span});
      }
      if (next_positional >= schema.size()) {
        diagnostics.push_back({
            "FSIM-ELAB-SC-PARAM-001",
            "too many positional SystemC construction actuals",
            override.span});
        continue;
      }
      index = next_positional++;
    }
    if (actuals[*index]) {
      diagnostics.push_back({
          "FSIM-ELAB-SC-PARAM-002",
          "duplicate SystemC construction actual for '"
              + schema[*index].name + "'",
          override.span});
    } else {
      actuals[*index] = *value;
    }
  }
  if (!vhdl_association && saw_named && saw_positional) {
    diagnostics.push_back({
        "FSIM-ELAB-SC-PARAM-003",
        "named and positional SystemC construction actuals cannot be mixed",
        overrides.empty() ? frontend::SourceSpan{} : overrides.front().span});
  }
  std::vector<std::pair<std::string, std::int64_t>> values;
  values.reserve(schema.size());
  for (std::size_t index = 0; index < schema.size(); ++index) {
    const auto value = actuals[index].has_value()
        ? actuals[index] : schema[index].default_value;
    if (!value) {
      diagnostics.push_back({
          "FSIM-ELAB-SC-PARAM-001",
          "SystemC construction parameter '" + schema[index].name
              + "' requires an actual",
          {}});
      continue;
    }
    if (!valid_systemc_construction_value(schema[index].type, *value)) {
      diagnostics.push_back({
          "FSIM-ELAB-SC-PARAM-005",
          "SystemC construction parameter '" + schema[index].name
              + "' violates its declared scalar subtype",
          {}});
      continue;
    }
    values.emplace_back(schema[index].name, *value);
  }
  if (diagnostics.size() != initial_diagnostic_count) return std::nullopt;
  return values;
}

}  // namespace fsim::elaboration::elaboration_detail
