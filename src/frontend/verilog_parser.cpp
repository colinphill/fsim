// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <string>

namespace fsim::frontend {

void SystemVerilogPackageMembers::add(const DesignUnit& package) {
  auto& record = packages_[package.name];
  const auto member = [&](const std::string& name,
                          const SystemVerilogPackageMember kind) {
    record.members.try_emplace(name, kind);
  };
  const auto literals = [&](const Type& type) {
    for (const auto& literal : type.enumeration_literals) {
      member(literal, SystemVerilogPackageMember::value);
    }
  };
  for (const auto& parameter : package.parameters) {
    member(parameter.name, parameter.kind == ParameterKind::Type
            ? SystemVerilogPackageMember::type
            : SystemVerilogPackageMember::value);
    literals(parameter.type);
  }
  for (const auto& variable : package.variables) {
    member(variable.name, SystemVerilogPackageMember::value);
    literals(variable.type);
  }
  for (const auto& signal : package.signals) {
    member(signal.name, SystemVerilogPackageMember::value);
    literals(signal.type);
  }
  for (const auto& alias : package.type_aliases) {
    member(alias.name, alias.type.systemverilog_container
            ? SystemVerilogPackageMember::unpacked_type
            : SystemVerilogPackageMember::type);
    literals(alias.type);
    for (const auto& literal : alias.enum_literals) {
      member(literal.name, SystemVerilogPackageMember::value);
    }
  }
  for (const auto& function : package.functions) {
    member(function.name, SystemVerilogPackageMember::value);
  }
  for (const auto& task : package.tasks) {
    member(task.name, SystemVerilogPackageMember::value);
  }
  for (const auto& let : package.systemverilog_lets) {
    member(let.name, SystemVerilogPackageMember::value);
  }
  // A sequence or property declaration (IEEE 1800-2023 16.8, 16.12).
  for (const auto& declaration : package.systemverilog_assertion_declarations) {
    if (declaration.kind != SystemVerilogAssertionDeclarationKind::Checker) {
      member(declaration.name, SystemVerilogPackageMember::value);
      record.assertion_declarations.push_back(declaration);
    }
  }
  for (const auto& covergroup : package.systemverilog_covergroups) {
    member(covergroup.name, SystemVerilogPackageMember::type);
  }
  for (const auto& declared : package.systemverilog_classes) {
    member(declared.name, SystemVerilogPackageMember::class_type);
  }
  record.imports = package.systemverilog_imports;
  record.exports = package.systemverilog_exports;
  record.wildcard_references.insert(
      package.systemverilog_wildcard_references.begin(),
      package.systemverilog_wildcard_references.end());
}

void SystemVerilogPackageMembers::add_member(const std::string_view package,
    const std::string_view member, const SystemVerilogPackageMember kind) {
  packages_[std::string { package }].members.try_emplace(
      std::string { member }, kind);
}

SystemVerilogPackageMember SystemVerilogPackageMembers::find(
    const std::string_view package, const std::string_view member,
    const SystemVerilogPackageMemberLookup& fallback) const {
  std::vector<std::string_view> visiting;
  return find(package, member, fallback, visiting);
}

std::optional<SystemVerilogPackageAssertions>
SystemVerilogPackageMembers::assertions(const std::string_view package,
    const SystemVerilogPackageAssertionLookup& fallback) const {
  const auto record = packages_.find(std::string { package });
  if (record != packages_.end()) {
    return SystemVerilogPackageAssertions { record->second.assertion_declarations, {} };
  }
  if (fallback) {
    return fallback(package);
  }
  return std::nullopt;
}

SystemVerilogPackageMember SystemVerilogPackageMembers::find(
    const std::string_view package, const std::string_view member,
    const SystemVerilogPackageMemberLookup& fallback,
    std::vector<std::string_view>& visiting) const {
  const auto record = packages_.find(std::string { package });
  if (record == packages_.end()) {
    return fallback ? fallback(package, member)
                    : SystemVerilogPackageMember::none;
  }
  if (std::ranges::find(visiting, package) != visiting.end()) {
    return SystemVerilogPackageMember::none;
  }
  if (const auto found = record->second.members.find(std::string { member });
      found != record->second.members.end()) {
    return found->second;
  }
  // An export carries on a member of a package the package imports; a
  // wildcard export of a wildcard import carries on only the names the
  // package refers to (IEEE 1800-2023 26.6).
  visiting.push_back(package);
  for (const auto& exported : record->second.exports) {
    if ((exported.package == "*" && !exported.name.empty())
        || (!exported.name.empty() && exported.name != member)) {
      continue;
    }
    for (const auto& imported : record->second.imports) {
      if ((exported.package != "*" && exported.package != imported.package)
          || (!imported.name.empty() && imported.name != member)) {
        continue;
      }
      if (exported.name.empty() && imported.name.empty()
          && !record->second.wildcard_references.contains(
              std::string { member })) {
        continue;
      }
      const auto kind = find(imported.package, member, fallback, visiting);
      if (kind != SystemVerilogPackageMember::none) {
        visiting.pop_back();
        return kind;
      }
    }
  }
  visiting.pop_back();
  return SystemVerilogPackageMember::none;
}

ParseResult parse_verilog(SourceText source, bool system_verilog) {
  return parse_verilog(
      std::move(source),
      system_verilog ? StandardRevision::SystemVerilog2017
                     : StandardRevision::Verilog2005);
}

ParseResult parse_verilog(
    SourceText source,
    const StandardRevision standard_revision) {
  return parse_verilog(
      std::move(source), standard_revision, "none");
}

ParseResult parse_verilog(
    SourceText source,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile) {
  return VerilogParser(
      lex(std::move(source), language_for_standard_revision(standard_revision)),
      standard_revision, std::string { compatibility_profile }).run();
}

ParseResult parse_verilog(LexResult lexed, bool system_verilog) {
  return parse_verilog(
      std::move(lexed),
      system_verilog ? StandardRevision::SystemVerilog2017
                     : StandardRevision::Verilog2005);
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision) {
  return parse_verilog(
      std::move(lexed), standard_revision, "none");
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile) {
  return parse_verilog(std::move(lexed), standard_revision,
      compatibility_profile, {});
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile,
    SystemVerilogPackageMemberLookup package_member_lookup) {
  return parse_verilog(std::move(lexed), standard_revision,
      compatibility_profile, std::move(package_member_lookup), {});
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile,
    SystemVerilogPackageMemberLookup package_member_lookup,
    SystemVerilogPackageAssertionLookup package_assertion_lookup) {
  return VerilogParser(std::move(lexed), standard_revision,
      std::string { compatibility_profile },
      std::move(package_member_lookup),
      std::move(package_assertion_lookup)).run();
}

std::optional<Statement> uvm_run_test_statement(const SourceSpan& call,
    std::optional<Expression> test_name, const UvmRunTestNames& names,
    const StandardRevision standard_revision, std::string& diagnostic) {
  std::string text(call.begin.line > 1U ? call.begin.line - 1U : 0U, '\n');
  const bool named = test_name.has_value();
  // Each function phase calls its method on the components in the order
  // next() hands them out; run_phase runs as one process per component
  // (IEEE 1800.2-2020 9.8).
  const auto function_phase = [](const int kind,
                                  const std::string_view method) {
    return "__fsim_uvm_phase = __fsim_uvm_run_test_function_phase("
        + std::to_string(kind)
        + "); __fsim_uvm_component = __fsim_uvm_run_test_next(); "
          "while (__fsim_uvm_component != null) begin __fsim_uvm_component."
        + std::string { method }
        + "(__fsim_uvm_phase); __fsim_uvm_component = "
          "__fsim_uvm_run_test_next(); end ";
  };
  text += "module __fsim_uvm_run_test; initial begin begin : __fsim_uvm_run_test "
          "int __fsim_uvm_count; string __fsim_uvm_test; "
          + names.phase_class + " __fsim_uvm_phase; "
          + names.component_class + " __fsim_uvm_component; ";
  // A named test is created as uvm_test_top, without a parent, through the
  // factory (IEEE 1800.2-2020 F.3.1.2, 13.2.3.6).
  text += named
      ? "__fsim_uvm_test = \"__fsim_uvm_test_name\"; "
        "__fsim_uvm_count = __fsim_uvm_run_test_begin(__fsim_uvm_test); "
        "__fsim_uvm_component = __fsim_uvm_run_test_construct("
        "__fsim_uvm_test, \"uvm_test_top\", null); "
      : "__fsim_uvm_count = __fsim_uvm_run_test_begin(); ";
  text += function_phase(0, names.phase_methods[0]);
  text += function_phase(1, names.phase_methods[1]);
  text += function_phase(2, names.phase_methods[2]);
  text += function_phase(3, names.phase_methods[3]);
  text += "__fsim_uvm_count = __fsim_uvm_run_test_components(); "
          "__fsim_uvm_phase = __fsim_uvm_run_test_phase(); "
          "for (int __fsim_uvm_index = 0; __fsim_uvm_index < __fsim_uvm_count; "
          "__fsim_uvm_index++) fork automatic "
          + names.component_class + " __fsim_uvm_task_component = "
          "__fsim_uvm_run_test_component(__fsim_uvm_index); "
          "__fsim_uvm_task_component." + names.phase_methods[4]
          + "(__fsim_uvm_phase); join_none "
          "__fsim_uvm_run_test_wait(); ";
  text += function_phase(4, names.phase_methods[5]);
  text += function_phase(5, names.phase_methods[6]);
  text += function_phase(6, names.phase_methods[7]);
  text += function_phase(7, names.phase_methods[8]);
  text += "__fsim_uvm_run_test_end(); $finish; end end endmodule\n";
  auto parsed = parse_verilog(
      SourceText { std::string { call.source_name.str() }, std::move(text) },
      standard_revision);
  Statement* block {};
  const auto find = [&](auto& self, auto& items) -> void {
    for (auto& item : items) {
      if (block != nullptr) {
        return;
      }
      if (item.label == "__fsim_uvm_run_test") {
        block = &item;
      } else {
        self(self, item.statements);
      }
    }
  };
  for (auto& unit : parsed.design.units) {
    for (auto& process : unit.processes) {
      find(find, process.statements);
    }
  }
  if (!parsed.ok() || block == nullptr) {
    diagnostic = parsed.diagnostics.empty()
        ? std::string {} : parsed.diagnostics.front().message;
    return std::nullopt;
  }
  // The placeholder names are the native calls of the run.
  const auto rewrite = [&](auto& self, Expression& expression) -> void {
    for (auto& operand : expression.operands) {
      self(self, operand);
    }
    if (expression.kind == ExpressionKind::StringLiteral
        && expression.text.find("__fsim_uvm_test_name") != std::string::npos) {
      expression = *test_name;
    } else if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with("__fsim_uvm_run_test_")) {
      auto member = expression.text.substr(
          std::string_view { "__fsim_uvm_run_test_" }.size());
      std::ranges::replace(member, '_', '-');
      expression.text = "@sv-static-method:@uvm-run-test-" + member;
      const bool count = member == "begin" || member == "components";
      expression.call_result_width = count ? 32U : 64U;
      expression.call_result_signed = count;
      expression.call_result_domain = ValueDomain::Bit2;
    }
  };
  const auto walk = [&](auto& self, Statement& statement) -> void {
    for (auto* expression : { &statement.target, &statement.value,
             &statement.condition, &statement.loop_initial,
             &statement.loop_limit, &statement.loop_update_target }) {
      rewrite(rewrite, *expression);
    }
    for (auto& argument : statement.task_arguments) {
      rewrite(rewrite, argument);
    }
    for (auto& declaration : statement.declarations) {
      if (declaration.initializer) {
        rewrite(rewrite, *declaration.initializer);
      }
    }
    if (statement.kind == StatementKind::TaskCall
        && statement.task_name.starts_with("__fsim_uvm_run_test_")) {
      statement.task_name = "@sv-static-task:@uvm-run-test-"
          + statement.task_name.substr(
              std::string_view { "__fsim_uvm_run_test_" }.size());
    }
    for (auto* nested : { &statement.statements, &statement.loop_updates,
             &statement.else_statements }) {
      for (auto& item : *nested) {
        self(self, item);
      }
    }
  };
  walk(walk, *block);
  block->label.clear();
  return std::move(*block);
}

}  // namespace fsim::frontend
