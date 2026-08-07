// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_resolution.hpp"
#include "fsim/frontend/coverage_limits.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string_view>

namespace fsim::frontend {

namespace {

struct CovergroupEntry {
  SystemVerilogCovergroupDeclaration* declaration{};
  std::string owner_identity;
  std::string lexical_identity;
  std::set<std::string> owner_objects;
};

void diagnose(
    std::vector<Diagnostic>& diagnostics,
    std::string code,
    std::string message,
    const SourceSpan& span) {
  diagnostics.push_back({
      DiagnosticSeverity::Error,
      std::move(code),
      std::move(message),
      span,
      span.expansion_stack});
}

[[nodiscard]] std::string unit_identity(const DesignUnit& unit) {
  return (unit.library.empty() ? std::string{"work"} : unit.library)
      + "." + unit.name;
}

[[nodiscard]] std::string token_spelling(
    const std::span<const Token> tokens) {
  std::string result;
  for (const auto& token : tokens) result += token.text;
  return result;
}

[[nodiscard]] std::string expression_identity(
    const Expression& expression) {
  std::ostringstream result;
  result << static_cast<unsigned>(expression.kind) << ':' << expression.text;
  if (!expression.call_argument_names.empty()) {
    result << '[';
    for (const auto& name : expression.call_argument_names) {
      result << name << ';';
    }
    result << ']';
  }
  result << '(';
  for (const auto& operand : expression.operands) {
    result << expression_identity(operand) << ';';
  }
  result << ')';
  return result.str();
}

[[nodiscard]] bool coverage_keyword(const std::string_view text) {
  static const std::set<std::string_view> keywords{
      "iff", "with", "function", "sample", "posedge", "negedge",
      "edge", "or", "and", "not", "inside", "intersect", "binsof",
      "bins", "illegal_bins", "ignore_bins", "option", "type_option",
      "this", "super"};
  return keywords.contains(text)
      || (!text.empty() && text.front() == '$');
}

[[nodiscard]] std::set<std::string> unit_objects(
    const DesignUnit& unit) {
  std::set<std::string> result;
  for (const auto& port : unit.ports) result.insert(port.name);
  for (const auto& signal : unit.signals) result.insert(signal.name);
  for (const auto& variable : unit.variables) result.insert(variable.name);
  for (const auto& parameter : unit.parameters) result.insert(parameter.name);
  for (const auto& function : unit.functions) result.insert(function.name);
  for (const auto& task : unit.tasks) result.insert(task.name);
  return result;
}

void add_entry(
    std::vector<CovergroupEntry>& entries,
    SystemVerilogCovergroupDeclaration& declaration,
    std::string owner,
    std::string lexical,
    std::set<std::string> objects) {
  declaration.owner_identity = owner;
  declaration.canonical_identity = owner + "::" + declaration.name;
  declaration.runtime_identity_prefix =
      declaration.canonical_identity + "@";
  std::string profile;
  for (const auto& formal : declaration.formals) {
    profile += formal.name + ":"
        + token_spelling(formal.type_tokens) + "="
        + token_spelling(formal.default_tokens) + ";";
  }
  if (declaration.sampling) {
    profile += declaration.sampling->kind
            == SystemVerilogCovergroupSamplingKind::WithFunctionSample
        ? "sample:"
        : "event:";
    for (const auto& formal : declaration.sampling->formals) {
      profile += formal.name + ":"
          + token_spelling(formal.type_tokens) + ";";
    }
    if (declaration.sampling->kind
        == SystemVerilogCovergroupSamplingKind::Event) {
      profile += token_spelling(declaration.sampling->tokens);
    }
  }
  declaration.specialization_identity =
      declaration.canonical_identity + "<" + profile + ">";
  entries.push_back({
      &declaration,
      std::move(owner),
      std::move(lexical),
      std::move(objects)});
}

void collect_class_entries(
    SystemVerilogClassDeclaration& class_declaration,
    const std::string& library,
    const std::set<std::string>& enclosing_objects,
    std::vector<CovergroupEntry>& entries) {
  auto objects = enclosing_objects;
  for (const auto& property : class_declaration.properties) {
    objects.insert(property.declaration.name);
  }
  for (const auto& method : class_declaration.methods) {
    objects.insert(method.name);
  }
  const auto owner = library + "." + class_declaration.canonical_identity;
  for (auto& covergroup : class_declaration.covergroups) {
    add_entry(
        entries,
        covergroup,
        owner,
        class_declaration.canonical_identity,
        objects);
  }
  for (auto& nested : class_declaration.nested_classes) {
    collect_class_entries(nested, library, objects, entries);
  }
}

[[nodiscard]] std::vector<CovergroupEntry*> find_covergroups(
    std::vector<CovergroupEntry>& entries,
    const std::string_view type_name,
    const std::string_view owner_identity) {
  std::vector<CovergroupEntry*> result;
  for (auto& entry : entries) {
    const auto& declaration = *entry.declaration;
    const bool exact =
        declaration.canonical_identity == type_name
        || entry.lexical_identity + "::" + declaration.name == type_name;
    const bool local =
        entry.owner_identity == owner_identity
        && declaration.name == type_name;
    const bool qualified_suffix =
        type_name.find("::") != std::string_view::npos
        && declaration.canonical_identity.ends_with(
            std::string{type_name});
    if (exact || local || qualified_suffix) result.push_back(&entry);
  }
  return result;
}

void collect_references(
    SystemVerilogCovergroupDeclaration& declaration,
    SystemVerilogCoverageDeclaration& item,
    const std::span<const Token> tokens,
    const std::set<std::string>& constructor_formals,
    const std::set<std::string>& sample_formals,
    const std::map<std::string, std::size_t>& coverpoints,
    const std::set<std::string>& owner_objects,
    std::vector<Diagnostic>& diagnostics) {
  std::size_t position{};
  while (position < tokens.size()) {
    if (tokens[position].kind != TokenKind::Identifier
        || coverage_keyword(tokens[position].text)) {
      ++position;
      continue;
    }
    const auto begin = position;
    std::string name = tokens[position].text;
    bool qualified{};
    while (position + 2U < tokens.size()
           && (tokens[position + 1U].kind == TokenKind::Dot
               || tokens[position + 1U].kind == TokenKind::Scope)
           && tokens[position + 2U].kind == TokenKind::Identifier) {
      name += tokens[position + 1U].kind == TokenKind::Dot ? "." : "::";
      name += tokens[position + 2U].text;
      position += 2U;
      qualified = true;
    }
    SystemVerilogCoverageReference reference;
    if (qualified) {
      reference.kind = SystemVerilogCoverageReferenceKind::Qualified;
      reference.canonical_name = name;
    } else if (constructor_formals.contains(name)) {
      reference.kind =
          SystemVerilogCoverageReferenceKind::ConstructorFormal;
      reference.canonical_name =
          declaration.canonical_identity + "::" + name;
    } else if (sample_formals.contains(name)) {
      reference.kind = SystemVerilogCoverageReferenceKind::SampleFormal;
      reference.canonical_name =
          declaration.canonical_identity + "::sample::" + name;
    } else if (const auto found = coverpoints.find(name);
               found != coverpoints.end()) {
      reference.kind = SystemVerilogCoverageReferenceKind::Coverpoint;
      reference.canonical_name =
          declaration.canonical_identity + "::" + name;
    } else if (owner_objects.contains(name)) {
      reference.kind = SystemVerilogCoverageReferenceKind::OwnerObject;
      reference.canonical_name =
          declaration.owner_identity + "::" + name;
    } else {
      diagnose(
          diagnostics,
          "FSIM-SV-SEM-208",
          "coverage expression name '" + name
              + "' is not visible from covergroup '"
              + declaration.canonical_identity + "'",
          tokens[begin].span);
      ++position;
      continue;
    }
    reference.tokens.assign(
        tokens.begin() + static_cast<std::ptrdiff_t>(begin),
        tokens.begin() + static_cast<std::ptrdiff_t>(position + 1U));
    reference.span = cover(
        reference.tokens.front().span,
        reference.tokens.back().span);
    item.references.push_back(std::move(reference));
    ++position;
  }
}

void resolve_declaration(
    CovergroupEntry& entry,
    std::vector<Diagnostic>& diagnostics) {
  auto& declaration = *entry.declaration;
  std::set<std::string> constructor_formals;
  for (const auto& formal : declaration.formals) {
    constructor_formals.insert(formal.name);
  }
  std::set<std::string> sample_formals;
  if (declaration.sampling) {
    for (const auto& formal : declaration.sampling->formals) {
      sample_formals.insert(formal.name);
    }
  }
  std::map<std::string, std::size_t> coverpoints;
  for (const auto& item : declaration.coverage_declarations) {
    if (item.kind == SystemVerilogCoverageDeclarationKind::Coverpoint) {
      coverpoints.emplace(item.name, item.declaration_index);
    }
  }
  for (auto& item : declaration.coverage_declarations) {
    item.references.clear();
    if (item.kind == SystemVerilogCoverageDeclarationKind::Coverpoint) {
      collect_references(
          declaration,
          item,
          item.expression_tokens,
          constructor_formals,
          sample_formals,
          coverpoints,
          entry.owner_objects,
          diagnostics);
    } else {
      for (auto& operand : item.cross_operands) {
        if (const auto found = coverpoints.find(operand.name);
            found != coverpoints.end()) {
          operand.resolved_declaration_index = found->second;
        } else if (
            constructor_formals.contains(operand.name)
            || sample_formals.contains(operand.name)
            || entry.owner_objects.contains(operand.name)) {
          operand.implicit_coverpoint = true;
        } else {
          diagnose(
              diagnostics,
              "FSIM-SV-SEM-209",
              "cross operand '" + operand.name
                  + "' does not name a coverpoint or visible expression",
              operand.span);
        }
      }
      for (const auto& bin : item.bins) {
        const auto& tokens = bin.cross_selection_tokens;
        bool saw_selection{};
        for (std::size_t index = 0; index < tokens.size(); ++index) {
          if (tokens[index].text != "binsof") continue;
          saw_selection = true;
          if (index + 3U >= tokens.size()
              || tokens[index + 1U].kind != TokenKind::LeftParen) {
            diagnose(
                diagnostics,
                "FSIM-SV-SEM-216",
                "cross bin '" + bin.name
                    + "' has an empty or malformed binsof selection",
                bin.span);
            break;
          }
          std::size_t right = index + 2U;
          int depth{1};
          for (; right < tokens.size(); ++right) {
            if (tokens[right].kind == TokenKind::LeftParen) ++depth;
            if (tokens[right].kind == TokenKind::RightParen) --depth;
            if (depth == 0) break;
          }
          if (right >= tokens.size() || right == index + 2U
              || tokens[index + 2U].kind != TokenKind::Identifier) {
            diagnose(
                diagnostics,
                "FSIM-SV-SEM-216",
                "cross bin '" + bin.name
                    + "' has an empty or malformed binsof selection",
                bin.span);
            break;
          }
          const auto operand_name = tokens[index + 2U].text;
          const auto operand_count = std::ranges::count(
              item.cross_operands,
              operand_name,
              &SystemVerilogCoverageCrossOperand::name);
          if (operand_count != 1) {
            diagnose(
                diagnostics,
                "FSIM-SV-SEM-216",
                "binsof selection '" + operand_name
                    + "' is empty or ambiguous in cross '" + item.name + "'",
                tokens[index + 2U].span);
            break;
          }
          std::optional<std::string> bin_name;
          if (index + 4U < right
              && tokens[index + 3U].kind == TokenKind::Dot
              && tokens[index + 4U].kind == TokenKind::Identifier) {
            bin_name = tokens[index + 4U].text;
          } else if (right + 2U < tokens.size()
                     && tokens[right + 1U].kind == TokenKind::Dot
                     && tokens[right + 2U].kind == TokenKind::Identifier) {
            bin_name = tokens[right + 2U].text;
          }
          if (bin_name) {
            const auto operand = std::ranges::find(
                item.cross_operands,
                operand_name,
                &SystemVerilogCoverageCrossOperand::name);
            if (operand == item.cross_operands.end()
                || !operand->resolved_declaration_index) {
              diagnose(
                  diagnostics,
                  "FSIM-SV-SEM-216",
                  "named bin selection '" + operand_name + "." + *bin_name
                      + "' has no resolved coverpoint",
                  bin.span);
              break;
            }
            const auto& coverpoint = declaration.coverage_declarations[
                *operand->resolved_declaration_index];
            const auto found_bin = std::ranges::find_if(
                coverpoint.bins,
                [&](const SystemVerilogCoverageBin& candidate) {
                  return candidate.name == *bin_name
                      || candidate.source_name == *bin_name;
                });
            if (found_bin == coverpoint.bins.end()) {
              diagnose(
                  diagnostics,
                  "FSIM-SV-SEM-216",
                  "named bin selection '" + operand_name + "." + *bin_name
                      + "' is empty",
                  bin.span);
              break;
            }
          }
          index = right;
        }
        if (!saw_selection) {
          diagnose(
              diagnostics,
              "FSIM-SV-SEM-216",
              "cross bin '" + bin.name
                  + "' has no binsof selection",
              bin.span);
        }
      }
    }
    collect_references(
        declaration,
        item,
        item.iff_tokens,
        constructor_formals,
        sample_formals,
        coverpoints,
        entry.owner_objects,
        diagnostics);
  }
}

void validate_actuals(
    const SystemVerilogCovergroupDeclaration& declaration,
    const Expression& construction,
    std::vector<Diagnostic>& diagnostics) {
  const auto& formals = declaration.formals;
  const auto& actuals = construction.operands;
  std::vector<bool> assigned(formals.size(), false);
  bool malformed{};
  for (std::size_t index = 0; index < actuals.size(); ++index) {
    std::size_t formal_index = index;
    if (index < construction.call_argument_names.size()
        && !construction.call_argument_names[index].empty()) {
      const auto found = std::ranges::find(
          formals,
          construction.call_argument_names[index],
          &SystemVerilogCovergroupFormal::name);
      if (found == formals.end()) {
        malformed = true;
        continue;
      }
      formal_index = static_cast<std::size_t>(
          std::distance(formals.begin(), found));
    }
    if (formal_index >= formals.size() || assigned[formal_index]) {
      malformed = true;
      continue;
    }
    assigned[formal_index] = true;
  }
  for (std::size_t index = 0; index < formals.size(); ++index) {
    if (!assigned[index] && formals[index].default_tokens.empty()) {
      malformed = true;
    }
  }
  if (malformed) {
    diagnose(
        diagnostics,
        "FSIM-SV-SEM-210",
        "covergroup constructor actuals do not match profile '"
            + declaration.canonical_identity + "'",
        construction.span);
  }
}

void add_instance(
    ParsedDesign& design,
    const std::string& owner,
    const VariableDeclaration& variable,
    CovergroupEntry& entry,
    std::vector<Diagnostic>& diagnostics) {
  SystemVerilogCovergroupInstance instance;
  instance.name = variable.name;
  instance.owner_identity = owner;
  instance.declaration_identity =
      entry.declaration->canonical_identity;
  instance.initial_option_state =
      entry.declaration->option_assignments;
  instance.span = variable.span;
  std::string actual_identity;
  if (variable.initializer
      && variable.initializer->kind == ExpressionKind::Call
      && variable.initializer->text == "@sv-new") {
    validate_actuals(*entry.declaration, *variable.initializer, diagnostics);
    instance.constructor_actuals = variable.initializer->operands;
    actual_identity = expression_identity(*variable.initializer);
  } else if (!entry.declaration->formals.empty()) {
    const auto missing_required = std::ranges::any_of(
        entry.declaration->formals,
        [](const SystemVerilogCovergroupFormal& formal) {
          return formal.default_tokens.empty();
        });
    if (missing_required) {
      diagnose(
          diagnostics,
          "FSIM-SV-SEM-210",
          "covergroup instance '" + variable.name
              + "' has no constructor actuals",
          variable.span);
    }
  }
  instance.specialization_identity =
      entry.declaration->specialization_identity + "("
      + actual_identity + ")";
  instance.runtime_identity =
      owner + "." + variable.name + "@"
      + instance.specialization_identity;
  design.systemverilog_covergroup_instances.push_back(
      std::move(instance));
}

void collect_sample_calls(
    ParsedDesign& design,
    const std::string& owner,
    const std::vector<Statement>& statements,
    const std::vector<CovergroupEntry>& entries,
    std::vector<Diagnostic>& diagnostics);

void collect_class_instances(
    ParsedDesign& design,
    SystemVerilogClassDeclaration& declaration,
    const std::string& library,
    std::vector<CovergroupEntry>& entries,
    std::vector<Diagnostic>& diagnostics) {
  const auto owner = library + "." + declaration.canonical_identity;
  for (auto& covergroup : declaration.covergroups) {
    SystemVerilogCovergroupInstance instance;
    instance.name = covergroup.name;
    instance.owner_identity = owner;
    instance.declaration_identity = covergroup.canonical_identity;
    instance.specialization_identity = covergroup.specialization_identity;
    instance.initial_option_state = covergroup.option_assignments;
    instance.runtime_identity =
        covergroup.runtime_identity_prefix + "<object>";
    instance.class_member_template = true;
    instance.span = covergroup.span;
    design.systemverilog_covergroup_instances.push_back(
        std::move(instance));
  }
  for (const auto& property : declaration.properties) {
    auto candidates = find_covergroups(
        entries, property.declaration.type.named_type, owner);
    if (candidates.size() == 1U) {
      add_instance(
          design,
          owner,
          property.declaration,
          *candidates.front(),
          diagnostics);
    } else if (candidates.size() > 1U) {
      diagnose(
          diagnostics,
          "FSIM-SV-SEM-211",
          "covergroup type '" + property.declaration.type.named_type
              + "' is ambiguous",
          property.span);
    }
  }
  for (const auto& method : declaration.methods) {
    collect_sample_calls(
        design, owner, method.statements, entries, diagnostics);
  }
  for (auto& nested : declaration.nested_classes) {
    collect_class_instances(
        design, nested, library, entries, diagnostics);
  }
}

void collect_sample_calls(
    ParsedDesign& design,
    const std::string& owner,
    const std::vector<Statement>& statements,
    const std::vector<CovergroupEntry>& entries,
    std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    if (statement.task_name.ends_with(".sample")) {
      auto receiver = statement.task_name.substr(
          0U, statement.task_name.size() - 7U);
      const auto dot = receiver.rfind('.');
      if (dot != std::string::npos) receiver = receiver.substr(dot + 1U);
      const auto instance = std::ranges::find_if(
          design.systemverilog_covergroup_instances,
          [&](const SystemVerilogCovergroupInstance& candidate) {
            return candidate.owner_identity == owner
                && candidate.name == receiver;
          });
      if (instance
          != design.systemverilog_covergroup_instances.end()) {
        const auto entry = std::ranges::find_if(
            entries,
            [&](const CovergroupEntry& candidate) {
              return candidate.declaration->canonical_identity
                  == instance->declaration_identity;
            });
        if (entry != entries.end()) {
          const auto expected =
              entry->declaration->sampling
                  && entry->declaration->sampling->kind
                      == SystemVerilogCovergroupSamplingKind::
                          WithFunctionSample
              ? entry->declaration->sampling->formals.size()
              : 0U;
          if (statement.task_arguments.size() != expected) {
            diagnose(
                diagnostics,
                "FSIM-SV-SEM-210",
                "covergroup sample actuals do not match profile '"
                    + entry->declaration->canonical_identity + "'",
                statement.span);
          }
        }
        SystemVerilogCovergroupSampleCall call;
        call.declaration_identity = instance->declaration_identity;
        call.instance_identity = instance->runtime_identity;
        call.actuals = statement.task_arguments;
        call.span = statement.span;
        instance->sample_calls.push_back(std::move(call));
      }
    }
    collect_sample_calls(
        design, owner, statement.statements, entries, diagnostics);
    collect_sample_calls(
        design, owner, statement.else_statements, entries, diagnostics);
    for (const auto& alternative : statement.case_alternatives) {
      collect_sample_calls(
          design, owner, alternative.statements, entries, diagnostics);
    }
  }
}

} // namespace

bool resolve_systemverilog_covergroups(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics) {
  const auto initial_diagnostic_count = diagnostics.size();
  design.systemverilog_covergroup_instances.clear();
  std::vector<CovergroupEntry> entries;
  for (auto& unit : design.units) {
    if (unit.language != Language::SystemVerilog2017) continue;
    const auto owner = unit_identity(unit);
    const auto objects = unit_objects(unit);
    for (auto& covergroup : unit.systemverilog_covergroups) {
      add_entry(entries, covergroup, owner, unit.name, objects);
    }
    const auto library =
        unit.library.empty() ? std::string{"work"} : unit.library;
    for (auto& declaration : unit.systemverilog_classes) {
      collect_class_entries(
          declaration, library, objects, entries);
    }
  }
  for (auto& declaration : design.systemverilog_classes) {
    collect_class_entries(
        declaration, "work", {}, entries);
  }
  for (auto& entry : entries) {
    resolve_declaration(entry, diagnostics);
    (void)validate_systemverilog_coverage_resources(
        *entry.declaration, diagnostics);
  }
  for (auto& unit : design.units) {
    if (unit.language != Language::SystemVerilog2017) continue;
    const auto owner = unit_identity(unit);
    for (const auto& variable : unit.variables) {
      auto candidates = find_covergroups(
          entries, variable.type.named_type, owner);
      if (candidates.size() == 1U) {
        add_instance(
            design,
            owner,
            variable,
            *candidates.front(),
            diagnostics);
      } else if (candidates.size() > 1U) {
        diagnose(
            diagnostics,
            "FSIM-SV-SEM-211",
            "covergroup type '" + variable.type.named_type
                + "' is ambiguous",
            variable.span);
      }
    }
    const auto library =
        unit.library.empty() ? std::string{"work"} : unit.library;
    for (auto& declaration : unit.systemverilog_classes) {
      collect_class_instances(
          design, declaration, library, entries, diagnostics);
    }
    for (const auto& process : unit.processes) {
      collect_sample_calls(
          design, owner, process.statements, entries, diagnostics);
    }
  }
  for (auto& declaration : design.systemverilog_classes) {
    collect_class_instances(
        design, declaration, "work", entries, diagnostics);
  }
  return diagnostics.size() == initial_diagnostic_count;
}

} // namespace fsim::frontend
