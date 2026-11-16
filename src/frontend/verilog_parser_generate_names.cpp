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

// A named block of an initial or always construct is declared in the
// module's scope, so an implicit generate block name avoids it (IEEE
// 1800-2017 27.6).
void remember_process_blocks(
    std::unordered_set<std::string>& names,
    const std::vector<Process>& processes) {
  for (const auto& process : processes) {
    for (const auto& statement : process.statements) {
      if (statement.kind == StatementKind::Block
          && !statement.label.empty()) {
        names.insert(statement.label);
      }
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
  remember_names(names, body.systemverilog_clocking_blocks);
  remember_process_blocks(names, body.processes);
}

void remember_unit_declarations(
    std::unordered_set<std::string>& names,
    const DesignUnit& unit) {
  remember_process_blocks(names, unit.processes);
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
  std::unordered_map<std::size_t, std::string> implicit_names;
};

} // namespace

void VerilogParser::normalize_systemverilog_generate_names(
    DesignUnit& unit) {
  using ExplicitName = std::pair<std::string, SourceSpan>;
  unit.implicit_generate_scopes.clear();

  std::function<void(const GenerateRegion&, std::vector<ExplicitName>&)>
      collect_scheme_names;
  const auto collect_branch_names =
      [&](const std::string& scope,
          const std::size_t implicit_ordinal,
          const GenerateBody& body,
          const SourceSpan& span,
          std::vector<ExplicitName>& names) {
        if (!scope.empty()) {
          names.emplace_back(scope, span);
        } else if (implicit_ordinal == 0U
                   && body.generate_regions.size() == 1U) {
          collect_scheme_names(body.generate_regions.front(), names);
        }
      };
  collect_scheme_names =
      [&](const GenerateRegion& region,
          std::vector<ExplicitName>& names) {
        collect_branch_names(
            region.then_scope, region.then_implicit_scope_ordinal,
            region.then_body, region.span, names);
        collect_branch_names(
            region.else_scope, region.else_implicit_scope_ordinal,
            region.else_body, region.span, names);
        for (const auto& alternative : region.alternatives) {
          collect_branch_names(
              alternative.scope,
              alternative.implicit_scope_ordinal,
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
  std::vector<std::string> block_path;

  // Returns whether the scope is named by the implicit-name rule.
  const auto resolve_scope =
      [](std::string& scope, const std::size_t ordinal,
         GenerateNameState& state) {
        if (ordinal == 0U) {
          return false;
        }
        if (const auto found = state.implicit_names.find(ordinal);
            found != state.implicit_names.end()) {
          scope = found->second;
          return true;
        }
        const auto digits = std::to_string(ordinal);
        std::size_t leading_zeros = 0;
        std::string candidate;
        do {
          candidate = "genblk" + std::string(leading_zeros, '0') + digits;
          ++leading_zeros;
        } while (state.occupied.contains(candidate));
        state.occupied.insert(candidate);
        state.implicit_names.emplace(ordinal, candidate);
        scope = std::move(candidate);
        return true;
      };

  normalize_body = [&](GenerateBody& body) {
    GenerateNameState nested;
    remember_body_declarations(nested.occupied, body);
    normalize_regions(body.generate_regions, nested);
  };

  const auto normalize_branch =
      [&](std::string& scope,
          std::size_t& implicit_ordinal,
          GenerateBody& body,
          GenerateNameState& state) {
        if (scope.empty() && implicit_ordinal == 0U) {
          if (body.generate_regions.size() == 1U) {
            normalize_region(body.generate_regions.front(), state);
          }
          return;
        }
        const bool implicit = resolve_scope(scope, implicit_ordinal, state);
        implicit_ordinal = 0U;
        block_path.push_back(scope);
        if (implicit) {
          std::string path;
          for (const auto& block : block_path) {
            path += path.empty() ? block : "." + block;
          }
          unit.implicit_generate_scopes.push_back(std::move(path));
        }
        normalize_body(body);
        block_path.pop_back();
      };

  normalize_region =
      [&](GenerateRegion& region, GenerateNameState& state) {
        normalize_branch(
            region.then_scope, region.then_implicit_scope_ordinal,
            region.then_body, state);
        normalize_branch(
            region.else_scope, region.else_implicit_scope_ordinal,
            region.else_body, state);
        for (auto& alternative : region.alternatives) {
          normalize_branch(
              alternative.scope, alternative.implicit_scope_ordinal,
              alternative.body, state);
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
