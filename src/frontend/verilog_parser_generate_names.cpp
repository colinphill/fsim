// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {

namespace {

constexpr std::string_view implicit_generate_prefix{
    "@fsim-implicit-genblk:"};

[[nodiscard]] std::optional<std::size_t> implicit_ordinal(
    const std::string_view name) {
  if (!name.starts_with(implicit_generate_prefix)) {
    return std::nullopt;
  }
  const auto digits = name.substr(implicit_generate_prefix.size());
  if (digits.empty()) {
    return std::nullopt;
  }
  std::size_t value = 0;
  for (const auto digit : digits) {
    if (digit < '0' || digit > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<std::size_t>(digit - '0');
  }
  return value;
}

template <typename Range>
void remember_names(
    std::unordered_set<std::string>& names,
    const Range& declarations) {
  for (const auto& declaration : declarations) {
    if (!declaration.name.empty()) {
      names.insert(declaration.name);
    }
  }
}

void remember_body_declarations(
    std::unordered_set<std::string>& names,
    const GenerateBody& body) {
  remember_names(names, body.constants);
  remember_names(names, body.type_aliases);
  remember_names(names, body.signals);
  remember_names(names, body.signal_aliases);
  remember_names(names, body.systemverilog_lets);
  remember_names(names, body.systemverilog_classes);
  remember_names(names, body.variables);
  remember_names(names, body.functions);
  remember_names(names, body.tasks);
  remember_names(names, body.procedures);
  remember_names(names, body.package_instances);
  remember_names(names, body.instances);
}

void remember_unit_declarations(
    std::unordered_set<std::string>& names,
    const DesignUnit& unit) {
  remember_names(names, unit.parameters);
  remember_names(names, unit.type_aliases);
  remember_names(names, unit.ports);
  remember_names(names, unit.signals);
  remember_names(names, unit.signal_aliases);
  remember_names(names, unit.systemverilog_lets);
  remember_names(names, unit.systemverilog_classes);
  remember_names(names, unit.variables);
  remember_names(names, unit.functions);
  remember_names(names, unit.tasks);
  remember_names(names, unit.procedures);
  remember_names(names, unit.package_instances);
  remember_names(names, unit.instances);
  remember_names(names, unit.systemverilog_checker_instances);
  remember_names(names, unit.systemverilog_clocking_blocks);
}

struct GenerateNameState {
  std::unordered_set<std::string> occupied;
  std::unordered_map<std::string, std::string> implicit_names;
};

} // namespace

void VerilogParser::normalize_systemverilog_generate_names(
    DesignUnit& unit) {
  using ExplicitName = std::pair<std::string, SourceSpan>;

  std::function<void(const GenerateRegion&, std::vector<ExplicitName>&)>
      collect_scheme_names;
  const auto collect_branch_names =
      [&](const std::string& scope,
          const GenerateBody& body,
          const SourceSpan& span,
          std::vector<ExplicitName>& names) {
        if (!scope.empty() && !implicit_ordinal(scope)) {
          names.emplace_back(scope, span);
        } else if (scope.empty() && body.generate_regions.size() == 1U) {
          collect_scheme_names(body.generate_regions.front(), names);
        }
      };
  collect_scheme_names =
      [&](const GenerateRegion& region,
          std::vector<ExplicitName>& names) {
        collect_branch_names(
            region.then_scope, region.then_body, region.span, names);
        collect_branch_names(
            region.else_scope, region.else_body, region.span, names);
        for (const auto& alternative : region.alternatives) {
          collect_branch_names(
              alternative.scope,
              alternative.body,
              alternative.span,
              names);
        }
      };

  std::function<void(
      std::vector<GenerateRegion>&, GenerateNameState&)>
      normalize_regions;
  std::function<void(GenerateRegion&, GenerateNameState&)>
      normalize_region;
  std::function<void(GenerateBody&)>
      normalize_body;

  const auto resolve_scope =
      [](std::string& scope, GenerateNameState& state) {
        const auto ordinal = implicit_ordinal(scope);
        if (!ordinal) {
          return;
        }
        if (const auto found = state.implicit_names.find(scope);
            found != state.implicit_names.end()) {
          scope = found->second;
          return;
        }
        const auto digits = std::to_string(*ordinal);
        std::size_t leading_zeros = 0;
        std::string candidate;
        do {
          candidate = "genblk" + std::string(leading_zeros, '0') + digits;
          ++leading_zeros;
        } while (state.occupied.contains(candidate));
        state.occupied.insert(candidate);
        state.implicit_names.emplace(scope, candidate);
        scope = std::move(candidate);
      };

  normalize_body = [&](GenerateBody& body) {
    GenerateNameState nested;
    remember_body_declarations(nested.occupied, body);
    normalize_regions(body.generate_regions, nested);
  };

  const auto normalize_branch =
      [&](std::string& scope,
          GenerateBody& body,
          GenerateNameState& state) {
        if (scope.empty()) {
          if (body.generate_regions.size() == 1U) {
            normalize_region(body.generate_regions.front(), state);
          }
          return;
        }
        resolve_scope(scope, state);
        normalize_body(body);
      };

  normalize_region =
      [&](GenerateRegion& region, GenerateNameState& state) {
        normalize_branch(region.then_scope, region.then_body, state);
        normalize_branch(region.else_scope, region.else_body, state);
        for (auto& alternative : region.alternatives) {
          normalize_branch(alternative.scope, alternative.body, state);
        }
      };

  normalize_regions =
      [&](std::vector<GenerateRegion>& regions,
          GenerateNameState& state) {
        for (const auto& region : regions) {
          std::vector<ExplicitName> scheme_names;
          collect_scheme_names(region, scheme_names);
          std::unordered_set<std::string> names_in_scheme;
          for (const auto& [name, span] : scheme_names) {
            if (!names_in_scheme.insert(name).second) {
              continue;
            }
            if (!state.occupied.insert(name).second) {
              error(
                  Token{TokenKind::Identifier, name, span, {}},
                  "FSIM-SV-SEM-386",
                  "generate block name '" + name
                      + "' conflicts with another declaration or "
                        "generate construct in the same scope");
            }
          }
        }
        for (auto& region : regions) {
          normalize_region(region, state);
        }
      };

  GenerateNameState root;
  remember_unit_declarations(root.occupied, unit);
  for (const auto& genvar : declared_genvars_) {
    root.occupied.insert(genvar);
  }
  normalize_regions(unit.generate_regions, root);
}

} // namespace fsim::frontend
