// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <functional>

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
  SystemVerilogHirBuilder(semantic::Model& model, sv::Hir& hir)
      : model_(model), hir_(hir) {}

  void add_design(const frontend::ParsedDesign& parsed) {
    for (std::size_t index = 0; index < parsed.units.size(); ++index) {
      const auto& unit = parsed.units[index];
      if (unit.language != frontend::Language::Vhdl2008) {
        add_unit(unit, semantic::UnitId::from_index(
                           static_cast<std::uint32_t>(index)));
      }
    }
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
    output.signed_value = input.is_signed;
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
      case frontend::PackedAggregateKind::UnpackedStruct:
        return sv::TypeForm::unpacked_structure;
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
        sv::DeclarationForm::typedef_declaration,
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
          source(member.span)});
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
    const auto label = !input.then_scope.empty()
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
    output.scope = nested_scope(
        parent_scope, label, declaration_source, declaration_origin);
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
    declaration(declaration_id).nested_scope = output.scope;
    add_generate_body(input.then_body, output);
    if (!input.else_scope.empty()
        || !input.else_body.constants.empty()
        || !input.else_body.type_aliases.empty()
        || !input.else_body.signals.empty()
        || !input.else_body.variables.empty()
        || !input.else_body.functions.empty()
        || !input.else_body.tasks.empty()
        || !input.else_body.concurrent_statements.empty()
        || !input.else_body.processes.empty()
        || !input.else_body.instances.empty()
        || !input.else_body.generate_regions.empty()) {
      frontend::GenerateRegion synthetic;
      synthetic.kind = frontend::GenerateKind::StaticBlock;
      synthetic.then_scope = input.else_scope.empty()
          ? label + ".else"
          : input.else_scope;
      synthetic.then_body = input.else_body;
      synthetic.span = input.span;
      output.nested.push_back(add_generate(
          synthetic, output.scope, declaration_origin));
    }
    for (const auto& alternative : input.alternatives) {
      frontend::GenerateRegion synthetic;
      synthetic.kind = frontend::GenerateKind::StaticBlock;
      synthetic.then_scope = alternative.scope.empty()
          ? label + ".alternative"
          : alternative.scope;
      synthetic.then_body = alternative.body;
      synthetic.span = alternative.span;
      output.nested.push_back(add_generate(
          synthetic, output.scope, declaration_origin));
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
    for (const auto& generate : input.generate_regions) {
      output.generates.push_back(add_generate(
          generate, output.scope, output.origin));
    }
    hir_.mutable_units().push_back(std::move(output));
  }

  semantic::Model& model_;
  sv::Hir& hir_;
};

} // namespace

semantic::sv::Hir build_systemverilog_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics) {
  semantic::sv::Hir result;
  SystemVerilogHirBuilder builder{semantics, result};
  builder.add_design(parsed);
  complete_systemverilog_executable_hir(parsed, semantics, result);
  return result;
}

} // namespace fsim::app::application_detail
