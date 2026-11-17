// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include "fsim/support/stack_growth.hpp"

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

namespace {

// One name a construct declares in a name space (IEEE 1800-2023 3.13).
struct DeclaredName {
  std::string_view name;
  SourceSpan span;
  std::string_view kind;
  // A net or variable: two of these are reported as duplicate signals
  // (FSIM-SV-SEM-006) or duplicate locals (FSIM-ELAB-053).
  bool data{};
};

[[nodiscard]] bool same_place(const SourceSpan& left, const SourceSpan& right) {
  return left.source_name == right.source_name && left.begin == right.begin;
}

// A begin-end or fork-join block opens a block name space (3.13 f), as
// does a for loop that declares its variable (12.7.1); the frontend's own
// statements open none.
[[nodiscard]] bool opens_scope(const Statement& statement) {
  if (statement.generated_role != support::GeneratedStatementRole::none) {
    return false;
  }
  return statement.kind == StatementKind::Block
      || statement.kind == StatementKind::Fork
      || (statement.kind == StatementKind::Loop
          && statement.loop_variable_declared);
}

// The names the statements declare in the name space that holds them: the
// labels of the blocks they open (9.3.5), however deep inside conditional,
// case and loop statements; each block they open is recorded for its own
// check.
template <typename Range>
void collect_statement_names(std::vector<DeclaredName>& names,
                             std::vector<const Statement*>& nested,
                             const Range& statements) {
  if (!support::stack_growth_detail::has_room()) {
    support::with_stack(
        [&] { collect_statement_names(names, nested, statements); });
    return;
  }
  for (const auto& statement : statements) {
    if (opens_scope(statement)) {
      if (!statement.label.empty()
          && statement.kind != StatementKind::Loop) {
        names.push_back(
            {statement.label, statement.span, "a named block", false});
      }
      nested.push_back(&statement);
      continue;
    }
    collect_statement_names(names, nested, statement.statements);
    collect_statement_names(names, nested, statement.else_statements);
    for (const auto& alternative : statement.case_alternatives) {
      collect_statement_names(names, nested, alternative.statements);
    }
  }
}

template <typename Range>
void collect_types(std::vector<DeclaredName>& names, const Range& aliases) {
  for (const auto& alias : aliases) {
    names.push_back({alias.name, alias.span, "a type", false});
  }
}

template <typename Range>
void collect_parameters(std::vector<DeclaredName>& names,
                        const Range& parameters) {
  for (const auto& parameter : parameters) {
    names.push_back({parameter.name, parameter.span, "a parameter", false});
  }
}

template <typename Range>
void collect_variables(std::vector<DeclaredName>& names,
                       const Range& variables) {
  for (const auto& variable : variables) {
    if (!variable.synthesized) {
      names.push_back({variable.name, variable.span, "a variable", true});
    }
  }
}

template <typename Range>
void collect_functions(std::vector<DeclaredName>& names,
                       std::vector<const FunctionDeclaration*>& bodies,
                       const Range& functions) {
  for (const auto& function : functions) {
    if (!function.extern_prototype
        && function.dpi_export_role == support::DpiExportCallableRole::none) {
      names.push_back({function.name, function.span, "a function", false});
      bodies.push_back(&function);
    }
  }
}

// The names a module, interface, program, checker or package or one of its
// generate blocks declares in its module name space (3.13 e).
template <typename Scope>
void collect_scope_names(std::vector<DeclaredName>& names,
                         std::vector<const Statement*>& nested,
                         std::vector<const FunctionDeclaration*>& functions,
                         std::vector<const TaskDeclaration*>& tasks,
                         std::vector<const Process*>& bodies,
                         const Scope& scope,
                         const std::vector<ParameterDeclaration>& parameters) {
  collect_parameters(names, parameters);
  collect_types(names, scope.type_aliases);
  for (const auto& declaration : scope.systemverilog_classes) {
    if (!declaration.is_forward_declaration) {
      names.push_back({declaration.name, declaration.span, "a class", false});
    }
  }
  for (const auto& signal : scope.signals) {
    if (!signal.synthesized) {
      names.push_back({signal.name, signal.span, "a net or variable", true});
    }
  }
  collect_variables(names, scope.variables);
  collect_functions(names, functions, scope.functions);
  for (const auto& task : scope.tasks) {
    if (!task.extern_prototype
        && task.dpi_export_role == support::DpiExportCallableRole::none) {
      names.push_back({task.name, task.span, "a task", false});
      tasks.push_back(&task);
    }
  }
  for (const auto& instance : scope.instances) {
    names.push_back({instance.name, instance.span, "an instance", false});
  }
  for (const auto& process : scope.processes) {
    // An unnamed begin-end the parser moved into its process is still a
    // block name space of its own.
    if (process.body_block) {
      bodies.push_back(&process);
    } else {
      collect_statement_names(names, nested, process.statements);
    }
  }
  for (const auto& assertion : scope.systemverilog_concurrent_assertions) {
    collect_statement_names(names, nested, assertion.pass_action_statements);
    collect_statement_names(names, nested,
                            assertion.failure_action_statements);
  }
}

}  // namespace

// Within a name space a name declared by one construct is not declared
// again (IEEE 1800-2023 3.13): the module name space of a module,
// interface, program, checker or package and of each generate block (e),
// and the block name space of each block, function and task (f). Ports are
// a name space of their own (g), two nets or variables are duplicate
// signals or locals (FSIM-SV-SEM-006, FSIM-ELAB-053), and generate block
// names are checked as they are normalized (FSIM-SV-SEM-386, -416).
void VerilogParser::check_systemverilog_name_spaces(const DesignUnit& unit) {
  const auto report = [&](const std::vector<DeclaredName>& names) {
    std::unordered_map<std::string_view, std::size_t> first;
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto& name = names[index];
      if (name.name.empty()) continue;
      const auto [found, inserted] = first.try_emplace(name.name, index);
      if (inserted) continue;
      const auto& prior = names[found->second];
      if (same_place(prior.span, name.span) || (prior.data && name.data)) {
        continue;
      }
      diagnostics_.push_back({
          DiagnosticSeverity::Error,
          "FSIM-SV-SEM-478",
          "'" + std::string{name.name} + "' is declared as "
              + std::string{name.kind}
              + " in a scope that already declares it as "
              + std::string{prior.kind},
          name.span,
          {}});
    }
  };
  std::vector<const Statement*> blocks;
  std::vector<const FunctionDeclaration*> functions;
  std::vector<const TaskDeclaration*> tasks;
  std::vector<const Process*> bodies;
  std::function<void(const std::vector<GenerateRegion>&)> regions;
  const auto body_names = [&](const GenerateBody& body) {
    std::vector<DeclaredName> names;
    collect_scope_names(names, blocks, functions, tasks, bodies, body,
                        body.constants);
    report(names);
    regions(body.generate_regions);
  };
  regions = [&](const std::vector<GenerateRegion>& list) {
    for (const auto& region : list) {
      body_names(region.then_body);
      body_names(region.else_body);
      for (const auto& alternative : region.alternatives) {
        body_names(alternative.body);
      }
    }
  };
  {
    std::vector<DeclaredName> names;
    collect_scope_names(names, blocks, functions, tasks, bodies, unit,
                        unit.parameters);
    report(names);
  }
  regions(unit.generate_regions);
  // Each block, function and task found adds the ones it holds.
  while (!blocks.empty() || !functions.empty() || !tasks.empty()
         || !bodies.empty()) {
    std::vector<DeclaredName> names;
    if (!bodies.empty()) {
      const auto* process = bodies.back();
      bodies.pop_back();
      collect_parameters(names, process->constants);
      collect_types(names, process->type_aliases);
      collect_variables(names, process->variables);
      collect_functions(names, functions, process->functions);
      collect_statement_names(names, blocks, process->statements);
    } else if (!blocks.empty()) {
      const auto* block = blocks.back();
      blocks.pop_back();
      collect_parameters(names, block->constants);
      collect_types(names, block->type_aliases);
      collect_variables(names, block->declarations);
      collect_functions(names, functions, block->functions);
      collect_statement_names(names, blocks, block->statements);
    } else if (!functions.empty()) {
      const auto* function = functions.back();
      functions.pop_back();
      collect_parameters(names, function->constants);
      collect_types(names, function->type_aliases);
      collect_variables(names, function->variables);
      collect_functions(names, functions, function->functions);
      collect_statement_names(names, blocks, function->statements);
    } else {
      const auto* task = tasks.back();
      tasks.pop_back();
      collect_types(names, task->type_aliases);
      collect_variables(names, task->variables);
      collect_statement_names(names, blocks, task->statements);
    }
    report(names);
  }
}

} // namespace fsim::frontend
