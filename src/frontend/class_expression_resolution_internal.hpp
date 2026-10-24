// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "class_expression_resolution.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend::class_resolution_detail {

Type specialize_selected_class_type(Type type,
    const std::vector<std::pair<std::string, const Expression*>>& values,
    const std::map<std::string, Type>& types);

// Objects visible in a scope. A copied scope shares its source's layers;
// its first change adds a layer on top, so copying a scope per class,
// method, or process no longer copies every enclosing object's type.
class ScopeObjects {
 public:
  [[nodiscard]] const Type* find(std::string_view name) const {
    for (const auto* layer = top_.get(); layer != nullptr;
         layer = layer->parent.get()) {
      if (const auto found = layer->objects.find(name);
          found != layer->objects.end()) {
        return &found->second;
      }
      if (layer->hidden.contains(name)) return nullptr;
    }
    return nullptr;
  }
  [[nodiscard]] bool contains(std::string_view name) const {
    return find(name) != nullptr;
  }
  // Declares or redeclares `name` in this scope.
  void assign(std::string_view name, Type type) {
    auto& layer = writable();
    if (const auto hidden = layer.hidden.find(name);
        hidden != layer.hidden.end()) {
      layer.hidden.erase(hidden);
    }
    layer.objects.insert_or_assign(std::string{name}, std::move(type));
  }
  // Declares `name` unless an object of that name is already visible.
  void add(std::string_view name, Type type) {
    if (!contains(name)) assign(name, std::move(type));
  }
  // Hides every visible object whose name satisfies `hidden`.
  void hide_if(const std::function<bool(const std::string&)>& hidden) {
    std::set<std::string, std::less<>> names;
    std::set<std::string, std::less<>> shadowed;
    for (const auto* layer = top_.get(); layer != nullptr;
         layer = layer->parent.get()) {
      for (const auto& entry : layer->objects) {
        if (!shadowed.contains(entry.first)) names.insert(entry.first);
      }
      for (const auto& entry : layer->objects) shadowed.insert(entry.first);
      shadowed.insert(layer->hidden.begin(), layer->hidden.end());
    }
    std::vector<std::string> selected;
    for (const auto& name : names) {
      if (hidden(name)) selected.push_back(name);
    }
    if (selected.empty()) return;
    auto& layer = writable();
    for (auto& name : selected) {
      layer.objects.erase(name);
      layer.hidden.insert(std::move(name));
    }
  }
  void clear() { top_.reset(); }

 private:
  struct Layer {
    std::map<std::string, Type, std::less<>> objects;
    std::set<std::string, std::less<>> hidden;
    std::shared_ptr<const Layer> parent;
  };

  Layer& writable() {
    if (!top_ || top_.use_count() > 1) {
      auto layer = std::make_shared<Layer>();
      layer->parent = std::move(top_);
      top_ = std::move(layer);
    }
    // Unshared: no other scope can observe this layer changing.
    return const_cast<Layer&>(*top_);
  }

  std::shared_ptr<const Layer> top_;
};

struct Scope {
  std::string library;
  std::string compilation_unit_identity;
  std::string lexical_identity;
  const SystemVerilogClassDeclaration* class_owner{};
  ScopeObjects objects;
  // Module and interface instances of the enclosing unit, by instance name,
  // with the name of the instantiated unit.
  std::map<std::string, std::string, std::less<>> instances{};
  // Interface ports of the enclosing unit, which name interface
  // instances rather than data objects.
  std::set<std::string, std::less<>> interface_ports{};
  // Type aliases declared in enclosing procedural blocks, by name
  // (IEEE 1800-2017 6.18).
  std::map<std::string, const Type*, std::less<>> aliases{};
  // Statements of a method body (not property initializers or
  // constraints).
  bool method_body{};
  // Package imports visible in the enclosing unit or class's package
  // (IEEE 1800-2017 26.3).
  std::vector<const SystemVerilogImport*> imports{};
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
// A class property access, or a member selected from one (`c.p.a`).
[[nodiscard]] bool is_class_property_access(const Expression& expression);
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
      scope.objects.assign(declaration.name, declaration.type);
    }
  }

  // Replaces each class-scoped declared type (`C #(...)::t`) by the
  // specialized type it names.
  template <typename Declaration>
  void resolve_scoped_types(
      std::vector<Declaration>& declarations, const Scope& scope) const {
    for (auto& declaration : declarations) {
      if (auto scoped = resolve_class_scoped_type(declaration.type, scope)) {
        declaration.type = std::move(*scoped);
      } else {
        resolve_class_scoped_members(declaration.type, scope);
      }
    }
  }

  void resolve_generate_body(GenerateBody& body, const Scope& inherited);
  // Whether `member.rand_mode` or `member.constraint_mode` in a method names
  // a random property or constraint block of the object itself.
  [[nodiscard]] bool this_mode_member(
      std::string_view call, const Scope& scope) const;
  [[nodiscard]] const SystemVerilogClassDeclaration* find_class(
      std::string_view identity) const;
  // Whether a handle of class `from` may be assigned to one of class `to`;
  // true when the relation cannot be determined.
  [[nodiscard]] bool class_assignable(
      std::string_view from, std::string_view to) const;
  [[nodiscard]] const SystemVerilogClassDeclaration* find_base_class(
      const SystemVerilogClassDeclaration& declaration) const;
  // The base of `declaration` within an object of type `receiver`.
  [[nodiscard]] const SystemVerilogClassDeclaration* find_base_class(
      const SystemVerilogClassDeclaration& declaration,
      const Type& receiver) const;
  // Reports a local or protected member used where it is not visible
  // (IEEE 1800-2017 8.18); returns whether it is visible.
  bool check_member_visibility(const PropertyMatch& match, const Scope& scope,
      std::string_view member, const SourceSpan& span);
  [[nodiscard]] bool has_specialization_dependent_base(
      std::string_view identity) const;
  [[nodiscard]] Type resolve_alias_type(
      Type type, std::string lexical_identity) const;
  // The member of a packed structure type selected by name, following a
  // typedef visible from the compilation unit.
  [[nodiscard]] std::optional<Type> packed_member_type(
      const Type& type, std::string_view member) const;
  // The member of a structure or union type, by name.
  [[nodiscard]] const PackedMember* structure_member(
      const Type& type, std::string_view member) const;
  // The class type of a structure or union member holding a class handle.
  [[nodiscard]] std::optional<Type> structure_member_class_type(
      const Type& type, std::string_view member, const Scope& scope) const;
  [[nodiscard]] static std::string_view trim_type_spelling(
      std::string_view spelling);
  [[nodiscard]] Type selected_type_actual(
      std::string_view source, const Scope& scope) const;
  [[nodiscard]] static std::vector<std::string_view>
  selected_parameter_actuals(std::string_view spelling);
  // The value of a specialization's parameter or local parameter, with the
  // specialization's literal or named actuals substituted.
  // Replaces `C#(...)::name` value references in a module constant.
  void resolve_class_owner_constants(
      Expression& expression, const Scope& scope, std::size_t depth);
  void substitute_class_parameter_references(
      Expression& expression, const Scope& scope,
      std::size_t depth = 0U) const;
  // A class constant at a module constant site (`C#(5)::b`, `P::C::n`,
  // `CFG::w`) names that member of the selected specialization; the
  // elaboration-time evaluator binds the selection's actuals.
  void resolve_class_scope_constants(
      Expression& expression, const Scope& scope, std::size_t depth = 0U);
  void resolve_module_constant(Expression& expression, const Scope& scope);
  void resolve_generate_constants(GenerateRegion& region, const Scope& scope);
  void resolve_range_constants(Type& type, const Scope& scope);
  // With `scope`, an actual that names another specialization's parameter
  // (`C#(15)::b`) takes that parameter's value.
  [[nodiscard]] std::optional<Expression> class_specialization_parameter_value(
      const SystemVerilogClassDeclaration& owner, std::string_view spelling,
      std::string_view member, const Scope* scope = nullptr) const;
  // `parsed` carries the selection's actuals as the parser read them; it
  // applies when its spelling is `spelling`.
  [[nodiscard]] std::optional<ClassSelection> resolve_class_selection(
      std::string_view spelling, const Scope& scope,
      const Type* parsed = nullptr) const;
  // The selection with the selected class's own actuals only.
  [[nodiscard]] std::optional<ClassSelection> resolve_own_class_selection(
      std::string_view spelling, const Scope& scope,
      const Type* parsed) const;
  [[nodiscard]] Type specialize_selected_type(
      Type type, const ClassSelection& selection) const;
  [[nodiscard]] Type canonicalize_selected_type(
      Type type, const Scope& scope) const;
  // The actual of type parameter `named` of `owner` seen through a handle
  // of `receiver`: the receiver's own actuals, a derived class's
  // `extends owner #(...)` actuals, or the parameter default
  // (IEEE 1800-2017 8.25, 8.13).
  [[nodiscard]] std::optional<Type> class_type_parameter_actual(
      const std::string& named, const SystemVerilogClassDeclaration& owner,
      const Type& receiver, const Scope& scope) const;
  [[nodiscard]] std::optional<Type> resolve_class_scoped_type(
      const Type& type, const Scope& scope) const;
  // `parsed`: the actuals are as the parser read them, so built-in type
  // keywords still need their predefined types.
  void normalize_selection_actuals(ClassSelection& selection,
      const Scope& scope, bool parsed = false) const;
  // A class typedef as specialization `selection` sees it (8.25.1).
  void anchor_alias_actuals(
      std::vector<SystemVerilogClassTypeActual>& actuals,
      std::string_view declaring) const;
  [[nodiscard]] Type specialize_class_alias(const Type& type,
      const Scope& scope, ClassSelection selection,
      const Type& alias_type) const;
  // Resolves class-scoped member types of a packed aggregate
  // (`struct packed { C#(1)::t f; }`) and re-lays it out.
  void resolve_class_scoped_members(Type& type, const Scope& scope) const;
  [[nodiscard]] SystemVerilogClassMethod specialize_selected_method(
      SystemVerilogClassMethod method,
      const ClassSelection& selection,
      const Scope& scope) const;
  [[nodiscard]] static std::string selected_uvm_type_identity(
      const ClassSelection& selection);
  [[nodiscard]] static std::string selected_uvm_intrinsic(
      const ClassSelection& selection,
      std::string_view method);
  // A method of the UVM library: a `uvm_pkg` that declares uvm_object.
  // A user package of that name is ordinary code.
  [[nodiscard]] bool uvm_library_method(std::string_view identity) const;
  [[nodiscard]] std::vector<const SystemVerilogClassDeclaration*>
  resolve_class_name(std::string_view spelling, const Scope& scope) const;
  // `receiver` is the handle type the lookup starts from: a base named by a
  // type parameter is that receiver's actual (IEEE 1800-2017 8.25).
  [[nodiscard]] PropertyMatch find_property(
      std::string_view identity, std::string_view name,
      const Type* receiver = nullptr) const;
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
      SystemVerilogClassMethodKind kind,
      const Type* receiver = nullptr) const;
  [[nodiscard]] std::optional<Type> resolve_identifier(
      Expression& expression, const Scope& scope);
  [[nodiscard]] std::optional<MethodMatch> select_method(
      std::string_view identity,
      std::string_view name,
      SystemVerilogClassMethodKind kind,
      std::size_t actual_count,
      const SourceSpan& span,
      const Type* receiver = nullptr);
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
  void resolve_scope_inline_constraints(
      Expression& expression, const Scope& scope);
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
  void resolve_member_sensitivity(
      Sensitivity& sensitivity, const Scope& scope) const;
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
  // The array typedef `named_type` names from `lexical`, walking outward,
  // then the unique compilation-unit typedef of that name.
  [[nodiscard]] static std::optional<std::string>
  class_specialization_spelling(const std::string& owner, const Type& type);
  [[nodiscard]] std::optional<Expression> class_scoped_parameter_value(
      const Type& type, const Scope& scope) const;
  [[nodiscard]] const Type* container_alias(
      std::string lexical, const std::string& named_type) const;
  std::map<std::string, SystemVerilogClassMethod*, std::less<>> methods_;
  std::map<std::string, SystemVerilogClassDeclaration*, std::less<>>
      method_owners_;
};

} // namespace fsim::frontend::class_resolution_detail
