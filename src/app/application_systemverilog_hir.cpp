// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <functional>
#include <set>

namespace fsim::app::application_detail {
namespace {

namespace sv = semantic::sv;

[[nodiscard]] sv::UnitKind unit_kind(
    const frontend::UnitKind kind) noexcept {
  switch (kind) {
    case frontend::UnitKind::SystemVerilogPackage:
      return sv::UnitKind::package;
    case frontend::UnitKind::SystemVerilogInterface:
      return sv::UnitKind::interface;
    case frontend::UnitKind::SystemVerilogProgram:
      return sv::UnitKind::program;
    default:
      return sv::UnitKind::module;
  }
}

[[nodiscard]] sv::Direction direction(
    const frontend::PortDirection value) noexcept {
  switch (value) {
    case frontend::PortDirection::Input:
      return sv::Direction::input;
    case frontend::PortDirection::Output:
      return sv::Direction::output;
    case frontend::PortDirection::Inout:
      return sv::Direction::inout;
    case frontend::PortDirection::Ref:
      return sv::Direction::ref;
    default:
      return sv::Direction::unknown;
  }
}

[[nodiscard]] sv::ConcurrentAssertionKind concurrent_assertion_kind(
    const frontend::SystemVerilogConcurrentAssertionKind kind) noexcept {
  switch (kind) {
    case frontend::SystemVerilogConcurrentAssertionKind::Assume:
      return sv::ConcurrentAssertionKind::assumption;
    case frontend::SystemVerilogConcurrentAssertionKind::Cover:
      return sv::ConcurrentAssertionKind::cover;
    case frontend::SystemVerilogConcurrentAssertionKind::Restrict:
      return sv::ConcurrentAssertionKind::restriction;
    default:
      return sv::ConcurrentAssertionKind::assertion;
  }
}

[[nodiscard]] sv::AssertionRegion assertion_region(
    const frontend::SystemVerilogAssertionRegion region) noexcept {
  switch (region) {
    case frontend::SystemVerilogAssertionRegion::Observed:
      return sv::AssertionRegion::observed;
    case frontend::SystemVerilogAssertionRegion::Reactive:
      return sv::AssertionRegion::reactive;
    default:
      return sv::AssertionRegion::preponed;
  }
}

[[nodiscard]] sv::Lifetime lifetime(
    const bool automatic,
    const bool explicit_lifetime) noexcept {
  if (automatic) {
    return sv::Lifetime::automatic;
  }
  return explicit_lifetime
      ? sv::Lifetime::static_lifetime
      : sv::Lifetime::implicit;
}

class SystemVerilogHirBuilder final {
 public:
  SystemVerilogHirBuilder(
      semantic::Model& model,
      sv::Hir& hir,
      const std::span<const frontend::SystemVerilogClassSpecialization>
          class_specializations)
      : model_(model), hir_(hir),
        class_specializations_(class_specializations) {}

  void add_design(const frontend::ParsedDesign& parsed) {
    for (std::size_t index = 0; index < parsed.units.size(); ++index) {
      const auto& unit = parsed.units[index];
      if (unit.language != frontend::Language::Vhdl2008) {
        add_unit(unit, semantic::UnitId::from_index(
                           static_cast<std::uint32_t>(index)));
      }
    }
    for (const auto& unit : parsed.units) {
      if (unit.language == frontend::Language::SystemVerilog2017) {
        add_classes(unit.systemverilog_classes);
      }
    }
    add_classes(parsed.systemverilog_classes);
    compose_constraints();
  }

 private:
  struct Pending {
    std::string_view physical_source;
    std::size_t offset{};
    std::size_t category{};
    std::size_t index{};
    std::function<semantic::DeclarationId()> build;
  };

  [[nodiscard]] semantic::SourceSpanId source(
      const frontend::SourceSpan& span) {
    return intern_semantic_span(model_, span);
  }

  [[nodiscard]] semantic::OriginId origin(
      const semantic::SourceSpanId span,
      const semantic::OriginId parent,
      const std::string_view detail) {
    return model_.add_origin(
        semantic::OriginKind::parsed, span, parent, std::string{detail});
  }

  [[nodiscard]] semantic::UnitId scope_unit(
      const semantic::ScopeId scope) const {
    return model_.scopes()[scope.value()].unit;
  }

  [[nodiscard]] semantic::ScopeId nested_scope(
      const semantic::ScopeId parent_scope,
      const std::string_view name,
      const semantic::SourceSpanId span,
      const semantic::OriginId scope_origin) {
    return model_.add_scope(
        scope_unit(parent_scope),
        parent_scope,
        std::string{name},
        span,
        scope_origin);
  }

  [[nodiscard]] sv::Name name(
      const std::string_view spelling,
      const frontend::SourceSpan& span,
      const semantic::ScopeId scope) {
    sv::Name result{std::string{spelling}, source(span), std::nullopt, {}};
    auto visible_scope = std::optional<semantic::ScopeId>{scope};
    while (visible_scope) {
      for (const auto& declaration : hir_.declarations()) {
        if (declaration.scope == *visible_scope
            && declaration.name == spelling) {
          result.overloads.push_back(declaration.id);
        }
      }
      if (!result.overloads.empty()) {
        break;
      }
      visible_scope = model_.scopes()[visible_scope->value()].parent;
    }
    if (result.overloads.size() == 1) {
      result.selected = result.overloads.front();
    }
    return result;
  }

  [[nodiscard]] std::optional<semantic::ExpressionId> expression(
      const frontend::Expression& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    if (input.kind == frontend::ExpressionKind::Invalid) {
      return std::nullopt;
    }
    const auto span = source(input.span);
    return model_.add_expression_identity(
        scope, span, origin(span, parent, "SystemVerilog expression"));
  }

  [[nodiscard]] semantic::TypeId find_type(
      const semantic::ScopeId scope,
      const std::string_view type_name,
      const std::optional<semantic::SourceSpanId> exact_source = std::nullopt)
      const noexcept {
    auto visible_scope = std::optional<semantic::ScopeId>{scope};
    while (visible_scope) {
      for (const auto& type : model_.types()) {
        if (type.scope == *visible_scope && type.name == type_name
            && (!exact_source || type.source == *exact_source)) {
          return type.id;
        }
      }
      if (exact_source) {
        break;
      }
      visible_scope = model_.scopes()[visible_scope->value()].parent;
    }
    return {};
  }

  [[nodiscard]] semantic::ValueId find_value(
      const semantic::ScopeId scope,
      const std::string_view value_name,
      const semantic::SourceSpanId exact_source) const noexcept {
    for (const auto& value : model_.values()) {
      if (value.scope == scope && value.source == exact_source
          && value.name == value_name) {
        return value.id;
      }
    }
    return {};
  }

  [[nodiscard]] sv::PackedRange packed_range(
      const frontend::PackedRange& range,
      const semantic::SourceSpanId span) const {
    return {
        range.left,
        range.right,
        std::nullopt,
        std::nullopt,
        range.descending,
        span};
  }

  [[nodiscard]] static sv::ClassVisibility class_visibility(
      const frontend::SystemVerilogClassVisibility visibility) noexcept {
    switch (visibility) {
      case frontend::SystemVerilogClassVisibility::Public:
        return sv::ClassVisibility::public_access;
      case frontend::SystemVerilogClassVisibility::Protected:
        return sv::ClassVisibility::protected_access;
      case frontend::SystemVerilogClassVisibility::Local:
        return sv::ClassVisibility::local_access;
    }
    return sv::ClassVisibility::public_access;
  }

  [[nodiscard]] static sv::ConstraintExpressionKind constraint_kind(
      const frontend::Expression& input) noexcept {
    using FrontendKind = frontend::ExpressionKind;
    using HirKind = sv::ConstraintExpressionKind;
    switch (input.kind) {
      case FrontendKind::Invalid: return HirKind::invalid;
      case FrontendKind::Identifier: return HirKind::name;
      case FrontendKind::IntegerLiteral: return HirKind::integer_literal;
      case FrontendKind::BooleanLiteral: return HirKind::boolean_literal;
      case FrontendKind::LogicLiteral: return HirKind::logic_literal;
      case FrontendKind::StringLiteral: return HirKind::string_literal;
      case FrontendKind::Unary: return HirKind::unary;
      case FrontendKind::Update: return HirKind::unary;
      case FrontendKind::Binary: return HirKind::binary;
      case FrontendKind::Index: return HirKind::index;
      case FrontendKind::Slice: return HirKind::slice;
      case FrontendKind::Aggregate: return HirKind::assignment_pattern;
      case FrontendKind::Concatenation: return HirKind::concatenation;
      case FrontendKind::Replication: return HirKind::replication;
      case FrontendKind::DefaultChoice: return HirKind::assignment_pattern;
      case FrontendKind::Conditional: return HirKind::conditional;
      case FrontendKind::Call:
        if (input.text == "?:") return HirKind::conditional;
        if (input.text == "inside") return HirKind::inside_set;
        if (input.text == "@inside-range") return HirKind::inside_range;
        if (input.text == "dist") return HirKind::distribution;
        if (input.text == "@dist-:=" || input.text == "@dist-:/") {
          return HirKind::distribution_item;
        }
        if (input.text == "soft") return HirKind::soft;
        if (input.text == "@constraint-block") {
          return HirKind::constraint_block;
        }
        if (input.text == "@constraint-implies") {
          return HirKind::implication;
        }
        if (input.text == "@constraint-if") {
          return HirKind::conditional_constraint;
        }
        if (input.text == "@constraint-foreach") {
          return HirKind::foreach_constraint;
        }
        if (input.text == "@solve-before") return HirKind::solve_before;
        if (input.text == "@solve-list") return HirKind::solve_list;
        if (input.text == "@constraint-unique") {
          return HirKind::unique_constraint;
        }
        return HirKind::call;
    }
    return HirKind::invalid;
  }

  [[nodiscard]] sv::ConstraintExpression constraint_expression(
      const frontend::Expression& input) {
    sv::ConstraintExpression output;
    output.kind = constraint_kind(input);
    output.text = input.text;
    output.source = source(input.span);
    for (const auto& operand : input.operands) {
      output.operands.push_back(constraint_expression(operand));
    }
    return output;
  }

  [[nodiscard]] sv::TypeReference class_property_type(
      const frontend::Type& input,
      const semantic::SourceSpanId type_source) {
    sv::TypeReference output;
    const auto spelling = input.named_type.empty()
        ? input.spelling
        : input.named_type;
    output.target = {{}, type_source, spelling};
    output.signed_value = input.is_signed;
    output.executable_width = input.width();
    output.four_state = input.domain == frontend::ValueDomain::Logic4
        || input.domain == frontend::ValueDomain::Integer;
    if (!input.systemverilog_class_declaration.empty()) {
      output.value_form = sv::TypeForm::class_handle;
      output.class_identity = input.systemverilog_class_declaration;
    } else if (input.domain == frontend::ValueDomain::String) {
      output.value_form = sv::TypeForm::string;
    } else if (input.systemverilog_container) {
      switch (input.systemverilog_container->kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
          output.container_form = sv::TypeForm::dynamic_array;
          break;
        case frontend::SystemVerilogContainerKind::Queue:
          output.container_form = sv::TypeForm::queue;
          break;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
          output.container_form = sv::TypeForm::associative_array;
          break;
        case frontend::SystemVerilogContainerKind::StaticArray:
          output.container_form = sv::TypeForm::static_array;
          break;
      }
    } else if (!input.enumeration_literals.empty()) {
      output.value_form = sv::TypeForm::enumeration;
    } else {
      output.value_form = sv::TypeForm::packed_integral;
    }
    if (input.packed_range) {
      output.packed_range = packed_range(*input.packed_range, type_source);
    }
    return output;
  }

  [[nodiscard]] static bool owner_matches(
      const std::string_view owner,
      const std::string_view selected) noexcept {
    return owner == selected
        || (owner.size() > selected.size() + 2U
            && owner.ends_with(selected)
            && owner[owner.size() - selected.size() - 1U] == ':');
  }

  [[nodiscard]] bool specializes_or_derives(
      const frontend::SystemVerilogClassSpecialization& specialization,
      const std::string_view declaration_identity) const {
    const auto* current = &specialization;
    while (current != nullptr) {
      if (current->declaration_identity == declaration_identity) return true;
      if (current->base_specialization_identity.empty()) return false;
      const auto base = std::ranges::find(
          class_specializations_, current->base_specialization_identity,
          &frontend::SystemVerilogClassSpecialization::specialization_identity);
      current = base == class_specializations_.end() ? nullptr : &*base;
    }
    return false;
  }

  [[nodiscard]] std::optional<sv::ConstraintBinding> property_binding(
      const frontend::SystemVerilogClassDeclaration& declaration,
      const frontend::SystemVerilogClassSpecialization& specialization,
      std::string spelling,
      const semantic::SourceSpanId binding_source) {
    enum class Selection { ordinary, this_object, super_object, qualified };
    auto selection = Selection::ordinary;
    std::string owner;
    if (spelling.starts_with("this.")) {
      spelling.erase(0, 5U);
      selection = Selection::this_object;
    } else if (spelling.starts_with("super.")) {
      spelling.erase(0, 6U);
      selection = Selection::super_object;
    } else if (const auto separator = spelling.rfind("::");
               separator != std::string::npos) {
      owner = spelling.substr(0, separator);
      spelling.erase(0, separator + 2U);
      selection = Selection::qualified;
    }
    const auto found = std::ranges::find_if(
        specialization.properties.rbegin(),
        specialization.properties.rend(),
        [&](const frontend::SystemVerilogClassPropertyLayout& property) {
          if (property.name != spelling) return false;
          if (selection == Selection::super_object) {
            return property.owner_identity != declaration.canonical_identity;
          }
          if (selection == Selection::qualified) {
            return owner_matches(property.owner_identity, owner);
          }
          return true;
        });
    if (found == specialization.properties.rend()) return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::property;
    binding.specialization_identity =
        specialization.specialization_identity;
    binding.canonical_identity = found->owner_identity + "::" + found->name;
    binding.type = class_property_type(found->type, binding_source);
    return binding;
  }

  [[nodiscard]] std::optional<sv::ConstraintBinding> parameter_binding(
      const frontend::SystemVerilogClassDeclaration& declaration,
      const frontend::SystemVerilogClassSpecialization& specialization,
      const std::string_view spelling,
      const semantic::SourceSpanId binding_source) {
    const auto value = std::ranges::find(
        specialization.parameter_values,
        spelling,
        &std::pair<std::string, std::string>::first);
    if (value == specialization.parameter_values.end()) return std::nullopt;
    const auto formal = std::ranges::find(
        declaration.parameters,
        spelling,
        &frontend::ParameterDeclaration::name);
    if (formal == declaration.parameters.end()) return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::parameter;
    binding.specialization_identity =
        specialization.specialization_identity;
    binding.canonical_identity = declaration.canonical_identity
        + "::" + std::string{spelling};
    binding.type = class_property_type(formal->type, binding_source);
    binding.constant_value = value->second;
    return binding;
  }

  [[nodiscard]] std::optional<sv::ConstraintBinding> method_binding(
      const frontend::SystemVerilogClassSpecialization& specialization,
      std::string spelling,
      const semantic::SourceSpanId binding_source) {
    if (spelling.starts_with('.')) spelling.erase(0, 1U);
    if (const auto separator = spelling.rfind("::");
        separator != std::string::npos) {
      spelling.erase(0, separator + 2U);
    }
    const auto found = std::ranges::find(
        specialization.methods,
        spelling,
        &frontend::SystemVerilogClassMethodProfile::name);
    if (found == specialization.methods.end()) return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::method;
    binding.specialization_identity =
        specialization.specialization_identity;
    binding.canonical_identity = found->canonical_identity;
    binding.type = class_property_type(found->return_type, binding_source);
    return binding;
  }

  void resolve_constraint_expression(
      sv::ConstraintExpression& expression,
      const frontend::SystemVerilogClassDeclaration& declaration,
      const std::optional<std::string_view> enclosing_foreach =
          std::nullopt) {
    auto foreach_iterator = enclosing_foreach;
    if (expression.kind == sv::ConstraintExpressionKind::foreach_constraint
        && expression.operands.size() == 2U
        && expression.operands[0].kind
            == sv::ConstraintExpressionKind::index
        && expression.operands[0].operands.size() == 2U
        && expression.operands[0].operands[1].kind
            == sv::ConstraintExpressionKind::name) {
      foreach_iterator = expression.operands[0].operands[1].text;
    }
    for (auto& operand : expression.operands) {
      resolve_constraint_expression(operand, declaration, foreach_iterator);
    }
    for (const auto& specialization : class_specializations_) {
      if (!specializes_or_derives(
              specialization, declaration.canonical_identity)) {
        continue;
      }
      std::optional<sv::ConstraintBinding> binding;
      if (expression.kind == sv::ConstraintExpressionKind::name) {
        if (foreach_iterator && expression.text == *foreach_iterator) {
          sv::ConstraintBinding local;
          local.kind = sv::ConstraintReferenceKind::local_variable;
          local.specialization_identity =
              specialization.specialization_identity;
          local.canonical_identity = declaration.canonical_identity
              + "::$foreach::" + expression.text;
          local.type.target = {{}, expression.source, "int"};
          local.type.value_form = sv::TypeForm::packed_integral;
          local.type.signed_value = true;
          local.type.executable_width = 32;
          local.type.four_state = true;
          binding = std::move(local);
        } else {
          binding = property_binding(
              declaration, specialization, expression.text,
              expression.source);
          if (!binding) {
            binding = parameter_binding(
                declaration, specialization, expression.text,
                expression.source);
          }
        }
      } else if (expression.kind == sv::ConstraintExpressionKind::call) {
        binding = method_binding(
            specialization, expression.text, expression.source);
      }
      if (binding) expression.bindings.push_back(std::move(*binding));
    }
  }

  void add_class(const frontend::SystemVerilogClassDeclaration& input) {
    sv::ClassDeclaration output;
    const auto generate_separator = input.name.rfind('.');
    output.name = generate_separator == std::string::npos
        ? input.name
        : input.name.substr(generate_separator + 1U);
    output.canonical_identity = input.canonical_identity;
    if (const auto separator = input.canonical_identity.rfind("::");
        separator != std::string::npos) {
      output.enclosing_identity = input.canonical_identity.substr(
          0, separator);
    }
    if (input.base) {
      output.base_declaration_identity = input.base->declaration_identity;
    }
    output.virtual_class = input.is_virtual;
    output.interface_class = input.is_interface;
    output.source = source(input.span);
    for (const auto& property : input.properties) {
      sv::ClassProperty retained;
      retained.name = property.declaration.name;
      retained.owner_identity = input.canonical_identity;
      retained.canonical_identity = input.canonical_identity + "::"
          + property.declaration.name;
      retained.type = class_property_type(
          property.declaration.type, source(property.span));
      retained.visibility = class_visibility(property.visibility);
      retained.random_kind = property.is_rand
          ? sv::ClassRandomKind::rand
          : property.is_randc
              ? sv::ClassRandomKind::randc
              : sv::ClassRandomKind::none;
      retained.static_storage = property.is_static;
      retained.constant = property.is_const;
      retained.source = source(property.span);
      output.properties.push_back(std::move(retained));
    }
    for (const auto& constraint : input.constraints) {
      sv::ClassConstraint retained;
      retained.name = constraint.name;
      retained.canonical_identity = input.canonical_identity + "::"
          + constraint.name;
      retained.owner_identity = input.canonical_identity;
      retained.visibility = class_visibility(constraint.visibility);
      retained.static_constraint = constraint.is_static;
      retained.pure = constraint.is_pure;
      retained.external = constraint.is_extern;
      retained.defined = constraint.defined;
      retained.source = source(constraint.span);
      for (const auto& expression : constraint.expressions) {
        retained.expressions.push_back(constraint_expression(expression));
      }
      for (auto& expression : retained.expressions) {
        resolve_constraint_expression(expression, input);
      }
      output.constraints.push_back(std::move(retained));
    }
    hir_.mutable_classes().push_back(std::move(output));
    add_classes(input.nested_classes);
  }

  void add_classes(
      const std::vector<frontend::SystemVerilogClassDeclaration>& inputs) {
    for (const auto& input : inputs) {
      add_class(input);
    }
  }

  void compose_constraints() {
    std::set<std::string> complete;
    std::set<std::string> active;
    std::function<void(sv::ClassDeclaration&)> compose;
    compose = [&](sv::ClassDeclaration& declaration) {
      if (complete.contains(declaration.canonical_identity)) return;
      if (!active.insert(declaration.canonical_identity).second) return;
      if (!declaration.base_declaration_identity.empty()) {
        const auto base = std::ranges::find(
            hir_.mutable_classes(),
            declaration.base_declaration_identity,
            &sv::ClassDeclaration::canonical_identity);
        if (base != hir_.mutable_classes().end()) {
          compose(*base);
          declaration.composed_constraints = base->composed_constraints;
        }
      }
      for (const auto& constraint : declaration.constraints) {
        sv::ComposedClassConstraint selected;
        selected.name = constraint.name;
        selected.selected_identity = constraint.canonical_identity;
        selected.mode_enabled = constraint.defined
            && !constraint.pure && !constraint.external;
        const auto inherited = std::ranges::find(
            declaration.composed_constraints,
            constraint.name,
            &sv::ComposedClassConstraint::name);
        if (inherited == declaration.composed_constraints.end()) {
          declaration.composed_constraints.push_back(std::move(selected));
          continue;
        }
        const auto inherited_owner = std::ranges::find_if(
            hir_.classes(), [&](const sv::ClassDeclaration& candidate) {
              return std::ranges::any_of(
                  candidate.constraints,
                  [&](const sv::ClassConstraint& block) {
                    return block.canonical_identity
                        == inherited->selected_identity;
                  });
            });
        const sv::ClassConstraint* inherited_block = nullptr;
        if (inherited_owner != hir_.classes().end()) {
          const auto block = std::ranges::find(
              inherited_owner->constraints,
              inherited->selected_identity,
              &sv::ClassConstraint::canonical_identity);
          if (block != inherited_owner->constraints.end()) {
            inherited_block = &*block;
          }
        }
        selected.overrides = true;
        selected.overridden_identity = inherited->selected_identity;
        selected.override_legal = inherited_block == nullptr
            || inherited_block->static_constraint
                == constraint.static_constraint;
        *inherited = std::move(selected);
      }
      active.erase(declaration.canonical_identity);
      complete.insert(declaration.canonical_identity);
    };
    for (auto& declaration : hir_.mutable_classes()) {
      compose(declaration);
    }
  }

  [[nodiscard]] sv::PackedRange packed_range(
      const frontend::PackedRangeExpression& range,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return {
        std::nullopt,
        std::nullopt,
        expression(range.left, scope, parent),
        expression(range.right, scope, parent),
        range.descending.value_or(true),
        source(range.span)};
  }

  [[nodiscard]] sv::TypeReference type_reference(
      const frontend::Type& input,
      const frontend::SourceSpan& fallback,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto spelling = input.named_type.empty()
        ? input.spelling
        : input.named_type;
    sv::TypeReference output;
    output.target = {
        input.named_type.empty() ? semantic::TypeId{}
                                 : find_type(scope, input.named_type),
        source(input.named_type.empty() ? fallback : input.named_type_span),
        spelling};
    if (!input.systemverilog_class_declaration.empty()) {
      output.value_form = sv::TypeForm::class_handle;
      output.class_identity = input.systemverilog_class_declaration;
    }
    output.signed_value = input.is_signed;
    output.executable_width = input.width();
    output.four_state = input.domain == frontend::ValueDomain::Logic4
        || input.domain == frontend::ValueDomain::Integer;
    if (input.packed_range) {
      output.packed_range = packed_range(
          *input.packed_range, source(fallback));
    } else if (input.packed_range_expression) {
      output.packed_range = packed_range(
          *input.packed_range_expression, scope, parent);
    }
    if (input.systemverilog_container) {
      const auto& container = *input.systemverilog_container;
      output.container_form = type_form(input);
      output.queue_maximum = container.queue_maximum
          ? expression(*container.queue_maximum, scope, parent)
          : std::nullopt;
      if (container.associative_index_type) {
        output.associative_index = type_reference(
            *container.associative_index_type,
            container.span,
            scope,
            parent).target;
      }
      if (!container.static_range_expressions.empty()) {
        for (const auto& range : container.static_range_expressions) {
          output.unpacked_dimensions.push_back(packed_range(
              range, scope, parent));
        }
      } else if (container.static_range) {
        output.unpacked_dimensions.push_back(packed_range(
            *container.static_range, source(container.span)));
      }
    }
    return output;
  }

  [[nodiscard]] semantic::DeclarationId add_declaration_record(
      const semantic::ScopeId scope,
      const sv::DeclarationForm form,
      const semantic::DeclarationKind kind,
      const std::string_view declaration_name,
      const frontend::SourceSpan& frontend_span,
      const semantic::OriginId parent) {
    const auto span = source(frontend_span);
    const auto declaration_origin = origin(span, parent, declaration_name);
    const auto id = model_.add_declaration(
        scope,
        kind,
        std::string{declaration_name},
        span,
        declaration_origin);
    sv::Declaration output;
    output.id = id;
    output.scope = scope;
    output.form = form;
    output.name = declaration_name;
    output.source = span;
    output.origin = declaration_origin;
    hir_.mutable_declarations().push_back(std::move(output));
    return id;
  }

  [[nodiscard]] sv::Declaration& declaration(
      const semantic::DeclarationId id) {
    const auto found = std::ranges::find_if(
        hir_.mutable_declarations(), [&](const sv::Declaration& declaration) {
          return declaration.id == id;
        });
    if (found == hir_.mutable_declarations().end()) {
      throw std::logic_error{"missing SystemVerilog HIR declaration"};
    }
    return *found;
  }

  [[nodiscard]] semantic::TypeId ensure_type(
      const std::string_view type_name,
      const semantic::ScopeId scope,
      const semantic::TypeKind kind,
      const sv::TypeReference& base,
      const semantic::SourceSpanId span,
      const semantic::OriginId declaration_origin) {
    auto id = find_type(scope, type_name, span);
    if (!id.valid()) {
      id = model_.add_type(
          scope,
          kind,
          std::string{type_name},
          base.target,
          span,
          declaration_origin);
    }
    return id;
  }

  [[nodiscard]] semantic::ValueId ensure_value(
      const std::string_view value_name,
      const semantic::ScopeId scope,
      const semantic::ValueKind kind,
      const sv::TypeReference& type,
      const semantic::SourceSpanId span,
      const semantic::OriginId declaration_origin) {
    auto id = find_value(scope, value_name, span);
    if (!id.valid()) {
      id = model_.add_value(
          scope,
          kind,
          std::string{value_name},
          type.target,
          span,
          declaration_origin);
    }
    return id;
  }

  [[nodiscard]] sv::TypeForm type_form(
      const frontend::Type& input) const noexcept {
    if (input.systemverilog_container) {
      switch (input.systemverilog_container->kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
          return sv::TypeForm::dynamic_array;
        case frontend::SystemVerilogContainerKind::Queue:
          return sv::TypeForm::queue;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
          return sv::TypeForm::associative_array;
        case frontend::SystemVerilogContainerKind::StaticArray:
          return sv::TypeForm::static_array;
      }
    }
    switch (input.packed_aggregate) {
      case frontend::PackedAggregateKind::Struct:
        return sv::TypeForm::packed_structure;
      case frontend::PackedAggregateKind::Union:
        return sv::TypeForm::packed_union;
      case frontend::PackedAggregateKind::TaggedUnion:
        return sv::TypeForm::tagged_union;
      case frontend::PackedAggregateKind::UnpackedStruct:
        return sv::TypeForm::unpacked_structure;
      case frontend::PackedAggregateKind::UnpackedUnion:
        return sv::TypeForm::unpacked_union;
      case frontend::PackedAggregateKind::None:
        break;
    }
    if (!input.enumeration_literals.empty()) {
      return sv::TypeForm::enumeration;
    }
    if (input.domain == frontend::ValueDomain::String) {
      return sv::TypeForm::string;
    }
    return sv::TypeForm::packed_integral;
  }

  [[nodiscard]] semantic::DeclarationId add_type_declaration(
      const frontend::TypeAliasDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
      const auto id = add_declaration_record(
          scope,
          input.declaration_kind
                  == frontend::TypeDeclarationKind::SystemVerilogNettype
              ? sv::DeclarationForm::nettype_declaration
              : sv::DeclarationForm::typedef_declaration,
          semantic::DeclarationKind::type,
          input.name,
          input.span,
          parent);
      const auto declaration_origin = declaration(id).origin;
      const auto base = type_reference(
          input.type, input.span, scope, declaration_origin);
      const auto type_id = ensure_type(
          input.name,
          scope,
          semantic::TypeKind::alias,
          base,
          declaration(id).source,
          declaration_origin);
      declaration(id).declared_type = type_id;
      declaration(id).type = base;
      sv::TypeDefinition output;
      output.id = type_id;
      output.declaration = id;
      output.form = type_form(input.type);
      output.name = input.name;
      output.base = base;
      output.resolution_function = input.systemverilog_resolution_function;
      output.source = declaration(id).source;
      output.origin = declaration_origin;
      add_type_payload(input, output, scope, declaration_origin);
      hir_.mutable_types().push_back(std::move(output));
      return id;
  }

  void add_type_payload(
      const frontend::TypeAliasDeclaration& input,
      sv::TypeDefinition& output,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    for (const auto& member : input.type.packed_members) {
      frontend::Type member_type;
      member_type.domain = member.domain;
      member_type.spelling = member.spelling;
      member_type.packed_range = member.packed_range;
      member_type.is_signed = member.is_signed;
      member_type.packed_range_expression = member.packed_range_expression;
      if (!member.nested_types.empty()) {
        member_type = member.nested_types.front();
      }
      output.members.push_back({
          member.name,
          type_reference(member_type, member.span, scope, parent),
          member.lsb_offset,
          source(member.span),
          member.initializer
              ? expression(*member.initializer, scope, parent)
              : std::nullopt});
    }
    for (const auto& literal : input.enum_literals) {
      const auto literal_id = add_declaration_record(
          scope,
          sv::DeclarationForm::enumeration_literal,
          semantic::DeclarationKind::enumeration_literal,
          literal.name,
          literal.span,
          parent);
      const auto& literal_declaration = declaration(literal_id);
      sv::TypeReference literal_type;
      literal_type.target = {
          output.id, literal_declaration.source, output.name};
      const auto literal_value = ensure_value(
          literal.name,
          scope,
          semantic::ValueKind::enumeration_literal,
          literal_type,
          literal_declaration.source,
          literal_declaration.origin);
      output.enumeration_literals.push_back({
          literal_id,
          literal_value,
          literal.name,
          expression(literal.value, scope, literal_declaration.origin),
          literal_declaration.source});
    }
    if (input.type.systemverilog_container) {
      const auto& container = *input.type.systemverilog_container;
      sv::ContainerType converted;
      converted.form = type_form(input.type);
      converted.queue_maximum = container.queue_maximum
          ? expression(*container.queue_maximum, scope, parent)
          : std::nullopt;
      if (container.associative_index_type) {
        converted.associative_index = type_reference(
            *container.associative_index_type,
            container.span,
            scope,
            parent);
      }
      if (!container.static_range_expressions.empty()) {
        for (const auto& range : container.static_range_expressions) {
          converted.static_dimensions.push_back(packed_range(
              range, scope, parent));
        }
      } else if (container.static_range) {
        converted.static_dimensions.push_back(packed_range(
            *container.static_range, source(container.span)));
      }
      converted.source = source(container.span);
      output.container = std::move(converted);
    }
  }

  template <typename Input>
  [[nodiscard]] semantic::DeclarationId add_object(
      const Input& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent,
      const sv::DeclarationForm form,
      const semantic::DeclarationKind declaration_kind,
      const semantic::ValueKind value_kind,
      const sv::Direction object_direction,
      const std::optional<frontend::Expression>& initializer) {
    const auto id = add_declaration_record(
        scope, form, declaration_kind, input.name, input.span, parent);
    auto& output = declaration(id);
    output.type = type_reference(
        input.type, input.span, scope, output.origin);
    output.direction = object_direction;
    output.declared_value = ensure_value(
        input.name,
        scope,
        value_kind,
        *output.type,
        output.source,
        output.origin);
    if (initializer) {
      output.initializer = expression(*initializer, scope, output.origin);
    }
    return id;
  }

  [[nodiscard]] semantic::DeclarationId add_parameter(
      const frontend::ParameterDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto type_parameter = input.kind == frontend::ParameterKind::Type;
    const auto id = add_declaration_record(
        scope,
        type_parameter
            ? sv::DeclarationForm::type_parameter
            : (input.local ? sv::DeclarationForm::local_parameter
                           : sv::DeclarationForm::parameter),
        type_parameter ? semantic::DeclarationKind::type
                       : semantic::DeclarationKind::generic,
        input.name,
        input.span,
        parent);
    auto& output = declaration(id);
    output.type = type_reference(
        input.type, input.span, scope, output.origin);
    if (input.default_type) {
      output.default_type = type_reference(
          *input.default_type, input.span, scope, output.origin);
    }
    if (type_parameter) {
      output.declared_type = ensure_type(
          input.name,
          scope,
          semantic::TypeKind::declaration,
          *output.type,
          output.source,
          output.origin);
    } else {
      output.declared_value = ensure_value(
          input.name,
          scope,
          semantic::ValueKind::parameter,
          *output.type,
          output.source,
          output.origin);
      output.initializer = expression(
          input.default_value, scope, output.origin);
    }
    return id;
  }

  [[nodiscard]] semantic::DeclarationId add_signal(
      const frontend::SignalDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto id = add_object(
        input,
        scope,
        parent,
        input.is_port ? sv::DeclarationForm::port
                      : sv::DeclarationForm::net,
        input.is_port ? semantic::DeclarationKind::port
                      : semantic::DeclarationKind::signal,
        input.is_port ? semantic::ValueKind::port
                      : semantic::ValueKind::signal,
        direction(input.direction),
        input.default_value);
    declaration(id).interface_type = input.interface_type;
    declaration(id).modport = input.modport;
    return id;
  }

  [[nodiscard]] semantic::DeclarationId add_variable(
      const frontend::VariableDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return add_object(
        input,
        scope,
        parent,
        sv::DeclarationForm::variable,
        semantic::DeclarationKind::variable,
        semantic::ValueKind::variable,
        sv::Direction::unknown,
        input.initializer);
  }

  [[nodiscard]] semantic::DeclarationId add_function_argument(
      const frontend::FunctionArgument& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return add_object(
        input,
        scope,
        parent,
        sv::DeclarationForm::port,
        semantic::DeclarationKind::port,
        semantic::ValueKind::parameter,
        direction(input.direction),
        input.default_value);
  }

  [[nodiscard]] semantic::DeclarationId add_task_argument(
      const frontend::TaskArgument& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return add_object(
        input,
        scope,
        parent,
        sv::DeclarationForm::port,
        semantic::DeclarationKind::port,
        semantic::ValueKind::parameter,
        direction(input.direction),
        input.default_value);
  }

  template <typename Range, typename Builder>
  void add_children(
      const Range& inputs,
      const semantic::ScopeId scope,
      const semantic::OriginId parent,
      std::vector<semantic::DeclarationId>& children,
      Builder builder) {
    for (const auto& input : inputs) {
      children.push_back((this->*builder)(input, scope, parent));
    }
  }

  [[nodiscard]] semantic::DeclarationId add_function(
      const frontend::FunctionDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto id = add_declaration_record(
        scope,
        sv::DeclarationForm::function,
        semantic::DeclarationKind::function,
        input.name,
        input.span,
        parent);
    const auto declaration_source = declaration(id).source;
    const auto declaration_origin = declaration(id).origin;
    const auto function_scope = nested_scope(
        scope, input.name, declaration_source, declaration_origin);
    auto result_type = type_reference(
        input.return_type, input.span, scope, declaration_origin);
    auto& output = declaration(id);
    output.type = result_type;
    output.nested_scope = function_scope;
    const auto callable_lifetime = lifetime(
        input.automatic, input.lifetime_explicit);
    output.lifetime = callable_lifetime;
    output.declared_value = ensure_value(
        input.name,
        scope,
        semantic::ValueKind::function,
        result_type,
        declaration_source,
        declaration_origin);
    sv::CallableProfile profile;
    profile.function = true;
    profile.return_type = result_type;
    profile.lifetime = callable_lifetime;
    for (const auto& argument : input.arguments) {
      profile.formals.push_back(add_function_argument(
          argument, function_scope, declaration_origin));
    }
    declaration(id).callable = std::move(profile);
    auto children = declaration(id).callable->formals;
    add_children(
        input.constants,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_parameter);
    add_children(
        input.type_aliases,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_type_declaration);
    add_children(
        input.variables,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_variable);
    add_children(
        input.functions,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_function);
    declaration(id).children = std::move(children);
    return id;
  }

  [[nodiscard]] semantic::DeclarationId add_task(
      const frontend::TaskDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto id = add_declaration_record(
        scope,
        sv::DeclarationForm::task,
        semantic::DeclarationKind::task,
        input.name,
        input.span,
        parent);
    const auto declaration_source = declaration(id).source;
    const auto declaration_origin = declaration(id).origin;
    const auto task_scope = nested_scope(
        scope, input.name, declaration_source, declaration_origin);
    auto& output = declaration(id);
    output.nested_scope = task_scope;
    const auto callable_lifetime = lifetime(
        input.automatic, input.lifetime_explicit);
    output.lifetime = callable_lifetime;
    sv::TypeReference no_type;
    no_type.target.source = declaration_source;
    output.declared_value = ensure_value(
        input.name,
        scope,
        semantic::ValueKind::task,
        no_type,
        declaration_source,
        declaration_origin);
    sv::CallableProfile profile;
    profile.function = false;
    profile.lifetime = callable_lifetime;
    for (const auto& argument : input.arguments) {
      profile.formals.push_back(add_task_argument(
          argument, task_scope, declaration_origin));
    }
    declaration(id).callable = std::move(profile);
    auto children = declaration(id).callable->formals;
    add_children(
        input.variables,
        task_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_variable);
    declaration(id).children = std::move(children);
    return id;
  }

  [[nodiscard]] semantic::DeclarationId add_modport(
      const frontend::SystemVerilogModport& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return add_declaration_record(
        scope,
        sv::DeclarationForm::modport,
        semantic::DeclarationKind::port,
        input.name,
        input.span,
        parent);
  }

  [[nodiscard]] semantic::InstanceId ensure_instance(
      const frontend::Instance& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto instance_source = source(input.span);
    for (const auto& instance : model_.instances()) {
      if (instance.scope == scope && instance.source == instance_source
          && instance.name == input.name) {
        return instance.id;
      }
    }
    const auto instance_origin = origin(
        instance_source, parent, input.name);
    return model_.add_instance(
        scope,
        input.name,
        input.unit_name,
        instance_source,
        instance_origin);
  }

  [[nodiscard]] semantic::ProcessId add_process_skeleton(
      const frontend::Process& input,
      const semantic::ScopeId parent_scope,
      const semantic::OriginId parent_origin) {
    const auto process_source = source(input.span);
    const auto process_name = input.name.empty()
        ? "<process>"
        : input.name;
    const auto process_origin = origin(
        process_source, parent_origin, process_name);
    const auto process_scope = nested_scope(
        parent_scope, process_name, process_source, process_origin);
    const auto id = model_.add_process_identity(
        process_scope,
        process_name,
        process_source,
        process_origin);
    std::vector<semantic::DeclarationId> unused;
    add_children(
        input.constants,
        process_scope,
        process_origin,
        unused,
        &SystemVerilogHirBuilder::add_parameter);
    add_children(
        input.type_aliases,
        process_scope,
        process_origin,
        unused,
        &SystemVerilogHirBuilder::add_type_declaration);
    add_children(
        input.variables,
        process_scope,
        process_origin,
        unused,
        &SystemVerilogHirBuilder::add_variable);
    add_children(
        input.functions,
        process_scope,
        process_origin,
        unused,
        &SystemVerilogHirBuilder::add_function);
    return id;
  }

  void add_generate_body(
      const frontend::GenerateBody& input,
      sv::GenerateRegion& output) {
    std::vector<Pending> pending;
    queue_declarations(input, output.scope, output.origin, pending);
    std::stable_sort(pending.begin(), pending.end(), pending_less);
    for (auto& item : pending) {
      output.declarations.push_back(item.build());
    }
    for (const auto& instance : input.instances) {
      output.instances.push_back(ensure_instance(
          instance, output.scope, output.origin));
    }
    for (const auto& process : input.processes) {
      output.processes.push_back(add_process_skeleton(
          process, output.scope, output.origin));
    }
    for (const auto& nested : input.generate_regions) {
      output.nested.push_back(add_generate(
          nested, output.scope, output.origin));
    }
  }

  [[nodiscard]] sv::GenerateRegion add_generate(
      const frontend::GenerateRegion& input,
      const semantic::ScopeId parent_scope,
      const semantic::OriginId parent_origin) {
    const bool owns_scope = !input.then_scope.empty();
    const auto label = owns_scope
        ? input.then_scope
        : "<generate>";
    const auto declaration_id = add_declaration_record(
        parent_scope,
        sv::DeclarationForm::generated,
        semantic::DeclarationKind::generate,
        label,
        input.span,
        parent_origin);
    const auto declaration_source = declaration(declaration_id).source;
    const auto declaration_origin = declaration(declaration_id).origin;
    sv::GenerateRegion output;
    output.declaration = declaration_id;
    output.scope = owns_scope
        ? nested_scope(
              parent_scope, label, declaration_source, declaration_origin)
        : parent_scope;
    switch (input.kind) {
      case frontend::GenerateKind::StaticBlock:
        output.kind = sv::GenerateKind::block;
        break;
      case frontend::GenerateKind::Conditional:
        output.kind = sv::GenerateKind::conditional;
        break;
      case frontend::GenerateKind::Iterative:
        output.kind = sv::GenerateKind::iterative;
        break;
      case frontend::GenerateKind::Selection:
        output.kind = sv::GenerateKind::selection;
        break;
    }
    output.label = input.then_scope;
    output.alternative_label = input.else_scope;
    output.iterator = input.variable;
    output.initial = expression(
        input.initial, output.scope, declaration_origin);
    output.condition = expression(
        input.condition, output.scope, declaration_origin);
    output.iteration = expression(
        input.iteration, output.scope, declaration_origin);
    output.source = declaration_source;
    output.origin = declaration_origin;
    if (owns_scope) {
      declaration(declaration_id).nested_scope = output.scope;
    }
    add_generate_body(input.then_body, output);
    if (!input.else_scope.empty()
        || !input.else_body.constants.empty()
        || !input.else_body.type_aliases.empty()
        || !input.else_body.signals.empty()
        || !input.else_body.variables.empty()
        || !input.else_body.functions.empty()
        || !input.else_body.tasks.empty()
        || !input.else_body.systemverilog_classes.empty()
        || !input.else_body.concurrent_statements.empty()
        || !input.else_body.processes.empty()
        || !input.else_body.instances.empty()
        || !input.else_body.generate_regions.empty()) {
      frontend::GenerateRegion synthetic;
      synthetic.kind = frontend::GenerateKind::StaticBlock;
      const auto alternative_scope = input.else_scope.empty()
          ? label + ".else"
          : input.else_scope;
      const bool shares_scope = owns_scope
          && alternative_scope == input.then_scope;
      synthetic.then_scope = shares_scope
          ? std::string { }
          : alternative_scope;
      synthetic.then_body = input.else_body;
      synthetic.span = input.span;
      output.nested.push_back(add_generate(
          synthetic,
          shares_scope ? output.scope : parent_scope,
          declaration_origin));
    }
    for (const auto& alternative : input.alternatives) {
      frontend::GenerateRegion synthetic;
      synthetic.kind = frontend::GenerateKind::StaticBlock;
      const auto alternative_scope = alternative.scope.empty()
          ? label + ".alternative"
          : alternative.scope;
      const bool shares_scope = owns_scope
          && alternative_scope == input.then_scope;
      synthetic.then_scope = shares_scope
          ? std::string { }
          : alternative_scope;
      synthetic.then_body = alternative.body;
      synthetic.span = alternative.span;
      output.nested.push_back(add_generate(
          synthetic,
          shares_scope ? output.scope : parent_scope,
          declaration_origin));
    }
    declaration(declaration_id).children = output.declarations;
    return output;
  }

  template <typename Input, typename Builder>
  void queue(
      const std::vector<Input>& inputs,
      const std::size_t category,
      const semantic::ScopeId scope,
      const semantic::OriginId parent,
      std::vector<Pending>& pending,
      Builder builder) {
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      const auto& item = inputs[index];
      pending.push_back({
          frontend::physical_source(item.span),
          item.span.begin.offset,
          category,
          index,
          [this, item_ptr = &item, builder, scope, parent] {
            return (this->*builder)(*item_ptr, scope, parent);
          }});
    }
  }

  void queue_declarations(
      const frontend::GenerateBody& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent,
      std::vector<Pending>& pending) {
    queue(input.constants, 0, scope, parent, pending,
          &SystemVerilogHirBuilder::add_parameter);
    queue(input.type_aliases, 1, scope, parent, pending,
          &SystemVerilogHirBuilder::add_type_declaration);
    queue(input.signals, 2, scope, parent, pending,
          &SystemVerilogHirBuilder::add_signal);
    queue(input.variables, 3, scope, parent, pending,
          &SystemVerilogHirBuilder::add_variable);
    queue(input.functions, 4, scope, parent, pending,
          &SystemVerilogHirBuilder::add_function);
    queue(input.tasks, 5, scope, parent, pending,
          &SystemVerilogHirBuilder::add_task);
  }

  static bool pending_less(
      const Pending& left,
      const Pending& right) noexcept {
    return std::tuple{
               left.physical_source, left.offset, left.category, left.index}
        < std::tuple{
               right.physical_source,
               right.offset,
               right.category,
               right.index};
  }

  void add_unit(
      const frontend::DesignUnit& input,
      const semantic::UnitId unit_id) {
    const auto& common = model_.units()[unit_id.value()];
    sv::Unit output;
    output.id = unit_id;
    output.scope = common.scope;
    output.kind = unit_kind(input.kind);
    output.library = input.library;
    output.name = input.name;
    output.source = common.source;
    output.origin = common.origin;
    output.compilation = {
        input.time_unit,
        input.time_precision,
        input.default_nettype,
        input.is_cell};
    for (const auto& input_import : input.systemverilog_imports) {
      output.imports.push_back({
          name(input_import.package, input_import.span, output.scope),
          input_import.name.empty()
              ? std::nullopt
              : std::optional<sv::Name>{name(
                    input_import.name, input_import.span, output.scope)},
          input_import.name.empty(),
          source(input_import.span)});
    }
    for (const auto& input_export : input.systemverilog_exports) {
      output.exports.push_back({
          name(input_export.package, input_export.span, output.scope),
          input_export.name.empty()
              ? std::nullopt
              : std::optional<sv::Name>{name(
                    input_export.name, input_export.span, output.scope)},
          input_export.name.empty(),
          source(input_export.span)});
    }
    for (std::size_t index = 0;
        index < input.systemverilog_aliases.size(); ++index) {
        const auto& input_alias = input.systemverilog_aliases[index];
        sv::Alias alias;
        alias.source = source(input_alias.span);
        alias.origin = origin(
            alias.source, output.origin,
            "$alias$" + std::to_string(index + 1U));
        for (const auto& terminal : input_alias.terminals) {
            const auto terminal_expression = expression(
                terminal, output.scope, alias.origin);
            if (terminal_expression) {
                alias.terminals.push_back(*terminal_expression);
            }
        }
        output.aliases.push_back(std::move(alias));
    }
    for (const auto& input_let : input.systemverilog_lets) {
        sv::LetDeclaration let;
        let.name = input_let.name;
        let.source = source(input_let.span);
        let.origin = origin(let.source, output.origin, let.name);
        for (const auto& input_port : input_let.ports) {
            sv::LetPort port;
            port.name = input_port.name;
            port.source = source(input_port.span);
            if (input_port.type) {
                port.type = type_reference(
                    *input_port.type, input_port.span, output.scope, let.origin);
            }
            if (input_port.default_value) {
                port.default_value = expression(
                    *input_port.default_value, output.scope, let.origin);
            }
            let.ports.push_back(std::move(port));
        }
        const auto let_expression = expression(
            input_let.expression, output.scope, let.origin);
        if (let_expression) {
            let.expression = *let_expression;
        }
        output.lets.push_back(std::move(let));
    }

    std::vector<Pending> pending;
    queue(input.parameters, 0, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_parameter);
    queue(input.ports, 1, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_signal);
    queue(input.type_aliases, 2, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_type_declaration);
    queue(input.signals, 3, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_signal);
    queue(input.variables, 4, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_variable);
    queue(input.functions, 5, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_function);
    queue(input.tasks, 6, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_task);
    queue(input.systemverilog_modports, 7, output.scope, output.origin, pending,
          &SystemVerilogHirBuilder::add_modport);
    std::stable_sort(pending.begin(), pending.end(), pending_less);
    for (auto& item : pending) {
      output.declarations.push_back(item.build());
    }
    for (const auto& input_modport : input.systemverilog_modports) {
      const auto declaration_id = std::ranges::find_if(
          hir_.declarations(), [&](const sv::Declaration& declaration) {
            return declaration.scope == output.scope
                && declaration.form == sv::DeclarationForm::modport
                && declaration.name == input_modport.name
                && declaration.source == source(input_modport.span);
          })->id;
      sv::Modport modport;
      modport.declaration = declaration_id;
      modport.name = input_modport.name;
      modport.source = source(input_modport.span);
      modport.origin = declaration(declaration_id).origin;
      for (const auto& input_member : input_modport.members) {
        sv::ModportMemberKind kind = sv::ModportMemberKind::signal;
        switch (input_member.kind) {
          case frontend::SystemVerilogModportMemberKind::Signal:
            kind = sv::ModportMemberKind::signal;
            break;
          case frontend::SystemVerilogModportMemberKind::FunctionImport:
            kind = sv::ModportMemberKind::function_import;
            break;
          case frontend::SystemVerilogModportMemberKind::FunctionExport:
            kind = sv::ModportMemberKind::function_export;
            break;
          case frontend::SystemVerilogModportMemberKind::TaskImport:
            kind = sv::ModportMemberKind::task_import;
            break;
          case frontend::SystemVerilogModportMemberKind::TaskExport:
            kind = sv::ModportMemberKind::task_export;
            break;
          case frontend::SystemVerilogModportMemberKind::Clocking:
            kind = sv::ModportMemberKind::clocking;
            break;
        }
        modport.members.push_back({
            kind,
            name(input_member.name, input_member.span, output.scope),
            direction(input_member.direction),
            source(input_member.span)});
      }
      output.modports.push_back(std::move(modport));
    }
    for (const auto& instance : input.instances) {
      output.instances.push_back(ensure_instance(
          instance, output.scope, output.origin));
    }
    for (const auto& process : input.processes) {
      output.processes.push_back(add_process_skeleton(
          process, output.scope, output.origin));
    }
    for (std::size_t index = 0;
         index < input.systemverilog_concurrent_assertions.size();
         ++index) {
      const auto& input_assertion =
          input.systemverilog_concurrent_assertions[index];
      sv::ConcurrentAssertion assertion;
      assertion.kind = concurrent_assertion_kind(input_assertion.kind);
      assertion.form = input_assertion.form
              == frontend::SystemVerilogConcurrentAssertionForm::Sequence
          ? sv::ConcurrentAssertionForm::sequence
          : sv::ConcurrentAssertionForm::property;
      assertion.explicit_label = !input_assertion.label.empty();
      assertion.name = assertion.explicit_label
          ? input_assertion.label
          : "$assertion$" + std::to_string(index + 1U);
      for (const auto& token : input_assertion.property_tokens) {
        assertion.property_tokens.push_back(token.text);
      }
      assertion.has_pass_action = input_assertion.has_pass_action;
      for (const auto& token : input_assertion.pass_action_tokens) {
        assertion.pass_action_tokens.push_back(token.text);
      }
      assertion.has_failure_action =
          input_assertion.has_failure_action;
      for (const auto& token : input_assertion.failure_action_tokens) {
        assertion.failure_action_tokens.push_back(token.text);
      }
      assertion.sampling_region =
          assertion_region(input_assertion.sampling_region);
      assertion.evaluation_region =
          assertion_region(input_assertion.evaluation_region);
      assertion.action_region =
          assertion_region(input_assertion.action_region);
      assertion.observers.callback_on_failure =
          input_assertion.kind
              != frontend::SystemVerilogConcurrentAssertionKind::Cover
          && input_assertion.kind
              != frontend::SystemVerilogConcurrentAssertionKind::Restrict;
      assertion.coverage_slot = static_cast<std::uint32_t>(index);
      assertion.source = source(input_assertion.span);
      if (!input_assertion.label.empty()) {
        assertion.label_source = source(input_assertion.label_span);
      }
      if (input_assertion.has_pass_action
          && !input_assertion.pass_action_tokens.empty()) {
        assertion.pass_action_source =
            source(input_assertion.pass_action_span);
      }
      if (input_assertion.has_failure_action
          && !input_assertion.failure_action_tokens.empty()) {
        assertion.failure_action_source =
            source(input_assertion.failure_action_span);
      }
      assertion.origin = origin(
          assertion.source, output.origin, assertion.name);
      output.concurrent_assertions.push_back(std::move(assertion));
    }
    for (const auto& generate : input.generate_regions) {
      output.generates.push_back(add_generate(
          generate, output.scope, output.origin));
    }
    hir_.mutable_units().push_back(std::move(output));
  }

  semantic::Model& model_;
  sv::Hir& hir_;
  std::span<const frontend::SystemVerilogClassSpecialization>
      class_specializations_;
};

} // namespace

semantic::sv::Hir build_systemverilog_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    const std::span<frontend::SystemVerilogClassSpecialization>
        class_specializations) {
  semantic::sv::Hir result;
  SystemVerilogHirBuilder builder{
      semantics, result, class_specializations};
  builder.add_design(parsed);
  complete_systemverilog_executable_hir(parsed, semantics, result);
  for (auto& specialization : class_specializations) {
    specialization.constraint_modes.clear();
    const auto declaration = std::ranges::find(
        result.classes(),
        specialization.declaration_identity,
        &sv::ClassDeclaration::canonical_identity);
    if (declaration == result.classes().end()) continue;
    for (const auto& constraint : declaration->composed_constraints) {
      if (constraint.override_legal) {
        specialization.constraint_modes.emplace_back(
            constraint.selected_identity, constraint.mode_enabled);
      }
    }
  }
  return result;
}

} // namespace fsim::app::application_detail
