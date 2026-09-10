// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "class_expression_resolution.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend::class_resolution_detail {

struct Scope {
  std::string library;
  std::string compilation_unit_identity;
  std::string lexical_identity;
  const SystemVerilogClassDeclaration* class_owner{};
  std::map<std::string, Type, std::less<>> objects;
};

struct PropertyMatch {
  const SystemVerilogClassProperty* property{};
  const SystemVerilogClassDeclaration* owner{};
};

struct ClassSelection {
  const SystemVerilogClassDeclaration* declaration{};
  Type type;
};

struct ConstraintMatch {
  const SystemVerilogClassConstraint* constraint{};
  const SystemVerilogClassDeclaration* owner{};
};

struct MethodMatch {
  const SystemVerilogClassMethod* method{};
  const SystemVerilogClassDeclaration* owner{};
};

[[nodiscard]] std::string effective_library(std::string_view library);
[[nodiscard]] std::optional<std::string> class_identity(const Type& type);
[[nodiscard]] Type class_type(
    const SystemVerilogClassDeclaration& declaration);
[[nodiscard]] Type chandle_type();
[[nodiscard]] bool chandle(const std::optional<Type>& type);
void diagnose(
    std::vector<Diagnostic>& diagnostics,
    std::string code,
    std::string message,
    const SourceSpan& span);

class Resolver final {
 public:
  Resolver(ParsedDesign& design, std::vector<Diagnostic>& diagnostics);
  void run();

 private:
  void collect(
      SystemVerilogClassDeclaration& declaration,
      const DesignUnit* unit);
  void append_lexical_type_aliases(
      Statement& statement,
      const SystemVerilogClassMethod& method,
      const SystemVerilogClassDeclaration& owner) const;
  bool hydrate_task_call(
      Statement& statement,
      std::set<std::string>& active,
      std::set<std::string>& expanded);
  void hydrate_statements(
      std::vector<Statement>& statements,
      std::set<std::string>& active,
      std::set<std::string>& expanded);
  void hydrate_task_bodies();

  template <typename Declaration>
  static void add_objects(
      Scope& scope, const std::vector<Declaration>& declarations) {
    for (const auto& declaration : declarations) {
      scope.objects[declaration.name] = declaration.type;
    }
  }

  void resolve_generate_body(GenerateBody& body, const Scope& inherited);
  [[nodiscard]] const SystemVerilogClassDeclaration* find_class(
      std::string_view identity) const;
  [[nodiscard]] const SystemVerilogClassDeclaration* find_base_class(
      const SystemVerilogClassDeclaration& declaration) const;
  [[nodiscard]] bool has_specialization_dependent_base(
      std::string_view identity) const;
  [[nodiscard]] Type resolve_alias_type(
      Type type, std::string lexical_identity) const;
  [[nodiscard]] static std::string_view trim_type_spelling(
      std::string_view spelling);
  [[nodiscard]] Type selected_type_actual(
      std::string_view source, const Scope& scope) const;
  [[nodiscard]] static std::vector<std::string_view>
  selected_parameter_actuals(std::string_view spelling);
  [[nodiscard]] std::optional<ClassSelection> resolve_class_selection(
      std::string_view spelling, const Scope& scope) const;
  [[nodiscard]] static Type specialize_selected_type(
      Type type, const ClassSelection& selection);
  [[nodiscard]] Type canonicalize_selected_type(
      Type type, const Scope& scope) const;
  [[nodiscard]] SystemVerilogClassMethod specialize_selected_method(
      SystemVerilogClassMethod method,
      const ClassSelection& selection,
      const Scope& scope) const;
  [[nodiscard]] static std::string selected_uvm_type_identity(
      const ClassSelection& selection);
  [[nodiscard]] static std::string selected_uvm_intrinsic(
      const ClassSelection& selection,
      std::string_view method);
  [[nodiscard]] std::vector<const SystemVerilogClassDeclaration*>
  resolve_class_name(std::string_view spelling, const Scope& scope) const;
  [[nodiscard]] PropertyMatch find_property(
      std::string_view identity, std::string_view name) const;
  [[nodiscard]] ConstraintMatch find_constraint(
      std::string_view identity, std::string_view name) const;
  [[nodiscard]] bool can_access(
      SystemVerilogClassVisibility visibility,
      const SystemVerilogClassDeclaration& owner,
      const Scope& scope) const;
  static void retain_result_type(Expression& expression, const Type& type);
  [[nodiscard]] std::vector<MethodMatch> find_methods(
      std::string_view identity,
      std::string_view name,
      SystemVerilogClassMethodKind kind) const;
  [[nodiscard]] std::optional<Type> resolve_identifier(
      Expression& expression, const Scope& scope);
  [[nodiscard]] std::optional<MethodMatch> select_method(
      std::string_view identity,
      std::string_view name,
      SystemVerilogClassMethodKind kind,
      std::size_t actual_count,
      const SourceSpan& span);
  static void retain_call_profile(
      Expression& expression,
      const SystemVerilogClassMethod& method,
      bool has_receiver);
  static void retain_task_profile(
      Statement& statement,
      const SystemVerilogClassMethod& method,
      const SystemVerilogClassDeclaration* owner = nullptr,
      const Type* receiver_type = nullptr);
  [[nodiscard]] std::optional<Type> resolve_call(
      Expression& expression,
      const Scope& scope,
      const std::optional<std::string>& expected_class,
      SystemVerilogScalarKind expected_scalar);
  std::optional<Type> resolve_expression(
      Expression& expression,
      const Scope& scope,
      const std::optional<std::string>& expected_class = std::nullopt,
      SystemVerilogScalarKind expected_scalar =
          SystemVerilogScalarKind::None);
  std::optional<Type> resolve_typed_expression(
      Expression& expression,
      const Scope& scope,
      const Type& expected);
  void resolve_task_call(Statement& statement, const Scope& scope);
  void resolve_statement(
      Statement& statement,
      const Scope& inherited_scope,
      const std::optional<std::string>& return_class,
      SystemVerilogScalarKind return_scalar =
          SystemVerilogScalarKind::None);
  void resolve_function(FunctionDeclaration& function, Scope scope);
  void resolve_task(TaskDeclaration& task, Scope scope);
  void resolve_class(
      SystemVerilogClassDeclaration& declaration, Scope scope);

  ParsedDesign& design_;
  std::vector<Diagnostic>& diagnostics_;
  std::map<std::string, SystemVerilogClassDeclaration*, std::less<>> classes_;
  std::map<std::string, const DesignUnit*, std::less<>> class_units_;
  std::map<std::string, const Type*, std::less<>> class_aliases_;
  std::map<std::string, SystemVerilogClassMethod*, std::less<>> methods_;
  std::map<std::string, SystemVerilogClassDeclaration*, std::less<>>
      method_owners_;
};

} // namespace fsim::frontend::class_resolution_detail
