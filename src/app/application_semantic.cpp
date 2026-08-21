// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/support/path.hpp"

#include <limits>
#include <system_error>
#include <utility>

namespace fsim::app::application_detail {
namespace {

[[nodiscard]] semantic::Language semantic_language(
    const frontend::Language language) noexcept {
  switch (language) {
    case frontend::Language::Vhdl2008:
      return semantic::Language::vhdl;
    case frontend::Language::Verilog2005:
      return semantic::Language::verilog;
    case frontend::Language::SystemVerilog2017:
      return semantic::Language::system_verilog;
  }
  return semantic::Language::system_verilog;
}

[[nodiscard]] semantic::UnitKind semantic_unit_kind(
    const frontend::UnitKind kind) noexcept {
  switch (kind) {
    case frontend::UnitKind::VhdlEntity:
      return semantic::UnitKind::vhdl_entity;
    case frontend::UnitKind::VhdlArchitecture:
      return semantic::UnitKind::vhdl_architecture;
    case frontend::UnitKind::VhdlConfiguration:
      return semantic::UnitKind::vhdl_configuration;
    case frontend::UnitKind::VhdlPackage:
      return semantic::UnitKind::vhdl_package;
    case frontend::UnitKind::VhdlContext:
      return semantic::UnitKind::vhdl_context;
    case frontend::UnitKind::VhdlPslVerificationUnit:
      return semantic::UnitKind::vhdl_psl_verification_unit;
    case frontend::UnitKind::SystemVerilogPackage:
      return semantic::UnitKind::systemverilog_package;
    case frontend::UnitKind::SystemVerilogInterface:
      return semantic::UnitKind::systemverilog_interface;
    case frontend::UnitKind::VerilogModule:
      return semantic::UnitKind::verilog_module;
    case frontend::UnitKind::SystemVerilogProgram:
      return semantic::UnitKind::systemverilog_program;
    case frontend::UnitKind::SystemVerilogConfiguration:
        return semantic::UnitKind::systemverilog_configuration;
    case frontend::UnitKind::SystemVerilogBind:
        return semantic::UnitKind::systemverilog_bind;
    }
  return semantic::UnitKind::verilog_module;
}

[[nodiscard]] semantic::TypeKind semantic_type_kind(
    const frontend::TypeDeclarationKind kind) noexcept {
  switch (kind) {
    case frontend::TypeDeclarationKind::VhdlSubtype:
      return semantic::TypeKind::subtype;
    case frontend::TypeDeclarationKind::Alias:
    case frontend::TypeDeclarationKind::SystemVerilogTypedef:
      return semantic::TypeKind::alias;
    case frontend::TypeDeclarationKind::SystemVerilogNettype:
        return semantic::TypeKind::declaration;
    default:
      return semantic::TypeKind::declaration;
  }
}

[[nodiscard]] std::string normalized_source_name(
    const std::string_view name) {
  if (name.empty()) {
    return "<unknown>";
  }
  const auto path = fsim::support::path_from_utf8(name);
  if (!path.is_absolute()) {
    return fsim::support::path_to_utf8(path.lexically_normal());
  }
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  return fsim::support::path_to_utf8(
      error ? path.lexically_normal() : canonical);
}

template <typename Target>
[[nodiscard]] Target bounded_position(const std::size_t value) noexcept {
  if constexpr (sizeof(Target) < sizeof(std::size_t)) {
    constexpr auto maximum = std::numeric_limits<Target>::max();
    return value > static_cast<std::size_t>(maximum)
        ? maximum
        : static_cast<Target>(value);
  } else {
    return static_cast<Target>(value);
  }
}

[[nodiscard]] semantic::SourcePosition semantic_position(
    const frontend::SourceLocation& location) noexcept {
  return {
      bounded_position<std::uint64_t>(location.offset),
      bounded_position<std::uint32_t>(location.line),
      bounded_position<std::uint32_t>(location.column)};
}

[[nodiscard]] std::string canonical_identifier(
    std::string value,
    const frontend::Language language) {
  if (language == frontend::Language::Vhdl2008) {
    // The VHDL parser already folds basic identifiers and intentionally
    // preserves extended-identifier case. Re-folding here would collapse two
    // distinct extended names.
    return value;
  }
  return value;
}

class SemanticModelBuilder final {
 public:
  explicit SemanticModelBuilder(semantic::Model& model) : model_(model) {}

  void add_sources(const std::span<const CheckedSource> sources) {
    for (const auto& source : sources) {
      register_source(source.path, source.content_digest);
      for (const auto& dependency : source.dependencies) {
        register_source(dependency.path, dependency.content_digest);
      }
    }
  }

  void add_unit(const frontend::DesignUnit& source_unit) {
    const auto unit_span = span(source_unit.span);
    const auto unit_origin = model_.add_origin(
        semantic::OriginKind::parsed,
        unit_span,
        std::nullopt,
        source_unit.library + ":" + source_unit.name);
    const auto unit = model_.add_unit(
        semantic_language(source_unit.language),
        semantic_unit_kind(source_unit.kind),
        source_unit.library,
        source_unit.name,
        source_unit.primary_name,
        unit_span,
        unit_origin);
    const auto& semantic_unit = model_.units()[unit.value()];
    add_types(source_unit, semantic_unit.scope, unit_origin);
    add_values(source_unit, semantic_unit.scope, unit_origin);
    add_instances(source_unit, semantic_unit.scope, unit_origin);
  }

 private:
  struct ValueInput {
    semantic::ValueKind kind{semantic::ValueKind::variable};
    std::string_view name;
    const frontend::Type* type{};
    const frontend::SourceSpan* span{};
    std::size_t category{};
    std::size_t index{};
  };

  void register_source(
      const std::filesystem::path& path,
      const std::string_view digest) {
    (void)model_.intern_source_file(
        normalized_source_name(fsim::support::path_to_utf8(path)),
        std::string{digest});
  }

  [[nodiscard]] semantic::SourceSpanId span(
      const frontend::SourceSpan& source_span) {
    return intern_semantic_span(model_, source_span);
  }

  [[nodiscard]] semantic::OriginId declaration_origin(
      const semantic::SourceSpanId source,
      const semantic::OriginId unit_origin,
      const std::string_view name) {
    return model_.add_origin(
        semantic::OriginKind::parsed,
        source,
        unit_origin,
        std::string{name});
  }

  [[nodiscard]] semantic::TypeReference type_reference(
      const frontend::Type& type,
      const frontend::SourceSpan& fallback,
      const frontend::Language language,
      const std::map<std::string, semantic::TypeId, std::less<>>& local_types) {
    const auto spelling = type.named_type.empty()
        ? type.spelling
        : type.named_type;
    const auto& source = type.named_type.empty()
        ? fallback
        : type.named_type_span;
    semantic::TypeId target;
    if (!type.named_type.empty()) {
      const auto found = local_types.find(
          canonical_identifier(type.named_type, language));
      if (found != local_types.end()) {
        target = found->second;
      }
    }
    return {target, span(source), spelling};
  }

  void add_types(
      const frontend::DesignUnit& source_unit,
      const semantic::ScopeId scope,
      const semantic::OriginId unit_origin) {
    std::vector<const frontend::TypeAliasDeclaration*> declarations;
    declarations.reserve(source_unit.type_aliases.size());
    for (const auto& declaration : source_unit.type_aliases) {
      declarations.push_back(&declaration);
    }
    std::stable_sort(
        declarations.begin(), declarations.end(), [](const auto* left,
                                                      const auto* right) {
          return std::tuple{
                     frontend::physical_source(left->span),
                     left->span.begin.offset,
                     std::string_view{left->name}}
              < std::tuple{
                     frontend::physical_source(right->span),
                     right->span.begin.offset,
                     std::string_view{right->name}};
        });

    std::map<std::string, semantic::TypeId, std::less<>> local_types;
    const auto first_type = model_.types().size();
    for (std::size_t index = 0; index < declarations.size(); ++index) {
      local_types.try_emplace(
          canonical_identifier(declarations[index]->name, source_unit.language),
          semantic::TypeId::from_index(static_cast<std::uint32_t>(
              first_type + index)));
    }
    for (const auto* declaration : declarations) {
      const auto source = span(declaration->span);
      const auto origin = declaration_origin(
          source, unit_origin, declaration->name);
      (void)model_.add_type(
          scope,
          semantic_type_kind(declaration->declaration_kind),
          declaration->name,
          type_reference(
              declaration->type,
              declaration->span,
              source_unit.language,
              local_types),
          source,
          origin);
    }
  }

  void add_values(
      const frontend::DesignUnit& source_unit,
      const semantic::ScopeId scope,
      const semantic::OriginId unit_origin) {
    std::vector<ValueInput> values;
    const auto append = [&]<typename Range>(
        const Range& range,
        const semantic::ValueKind kind,
        const std::size_t category,
        const auto type_member) {
      for (std::size_t index = 0; index < range.size(); ++index) {
        values.push_back({
            kind,
            range[index].name,
            type_member ? &(range[index].*type_member) : nullptr,
            &range[index].span,
            category,
            index});
      }
    };
    append(
        source_unit.parameters,
        semantic::ValueKind::parameter,
        0,
        &frontend::ParameterDeclaration::type);
    append(
        source_unit.ports,
        semantic::ValueKind::port,
        1,
        &frontend::SignalDeclaration::type);
    append(
        source_unit.signals,
        semantic::ValueKind::signal,
        2,
        &frontend::SignalDeclaration::type);
    append(
        source_unit.variables,
        semantic::ValueKind::variable,
        3,
        &frontend::VariableDeclaration::type);
    append(
        source_unit.functions,
        semantic::ValueKind::function,
        4,
        &frontend::FunctionDeclaration::return_type);
    append(
        source_unit.tasks,
        semantic::ValueKind::task,
        5,
        static_cast<frontend::Type frontend::TaskDeclaration::*>(nullptr));
    append(
        source_unit.procedures,
        semantic::ValueKind::procedure,
        6,
        static_cast<frontend::Type frontend::ProcedureDeclaration::*>(nullptr));
    for (std::size_t type_index = 0;
         type_index < source_unit.type_aliases.size(); ++type_index) {
      const auto& declaration = source_unit.type_aliases[type_index];
      for (std::size_t literal_index = 0;
           literal_index < declaration.enum_literals.size(); ++literal_index) {
        const auto& literal = declaration.enum_literals[literal_index];
        values.push_back({
            semantic::ValueKind::enumeration_literal,
            literal.name,
            &declaration.type,
            &literal.span,
            7 + type_index,
            literal_index});
      }
    }
    std::stable_sort(values.begin(), values.end(), [](const auto& left,
                                                       const auto& right) {
      return std::tuple{
                 frontend::physical_source(*left.span),
                 left.span->begin.offset,
                 left.category,
                 left.index}
          < std::tuple{
                 frontend::physical_source(*right.span),
                 right.span->begin.offset,
                 right.category,
                 right.index};
    });

    std::map<std::string, semantic::TypeId, std::less<>> local_types;
    for (const auto& type : model_.types()) {
      if (type.scope == scope) {
        local_types.try_emplace(
            canonical_identifier(type.name, source_unit.language), type.id);
      }
    }
    for (const auto& value : values) {
      const auto source = span(*value.span);
      const auto origin = declaration_origin(source, unit_origin, value.name);
      semantic::TypeReference type{{}, source, {}};
      if (value.type != nullptr) {
        type = type_reference(
            *value.type, *value.span, source_unit.language, local_types);
      }
      (void)model_.add_value(
          scope,
          value.kind,
          std::string{value.name},
          std::move(type),
          source,
          origin);
    }
  }

  void add_instances(
      const frontend::DesignUnit& source_unit,
      const semantic::ScopeId scope,
      const semantic::OriginId unit_origin) {
    std::vector<const frontend::Instance*> instances;
    instances.reserve(source_unit.instances.size());
    for (const auto& instance : source_unit.instances) {
      instances.push_back(&instance);
    }
    std::stable_sort(instances.begin(), instances.end(), [](const auto* left,
                                                            const auto* right) {
      return std::tuple{
                 frontend::physical_source(left->span),
                 left->span.begin.offset,
                 std::string_view{left->name}}
          < std::tuple{
                 frontend::physical_source(right->span),
                 right->span.begin.offset,
                 std::string_view{right->name}};
    });
    for (const auto* instance : instances) {
      const auto source = span(instance->span);
      const auto origin = declaration_origin(
          source, unit_origin, instance->name);
      (void)model_.add_instance(
          scope,
          instance->name,
          instance->unit_name,
          source,
          origin);
    }
  }

  semantic::Model& model_;
};

} // namespace

semantic::SourceSpanId intern_semantic_span(
    semantic::Model& model,
    const frontend::SourceSpan& source_span) {
  const auto physical = normalized_source_name(
      frontend::physical_source(source_span));
  auto file = model.find_source_file(physical);
  if (!file) {
    file = model.intern_source_file(physical);
  }
  std::optional<semantic::ExpansionId> expansion;
  if (!source_span.expansion_stack.empty()) {
    expansion = model.intern_expansion(source_span.expansion_stack);
  }
  auto begin = semantic_position(source_span.begin);
  auto end = semantic_position(source_span.end);
  if (end.offset < begin.offset) {
    std::swap(begin, end);
  }
  return model.intern_source_span(
      *file,
      source_span.source_name.empty()
          ? physical
          : source_span.source_name.str(),
      begin,
      end,
      expansion);
}

semantic::Model build_semantic_model(
    const frontend::ParsedDesign& parsed,
    const std::span<const CheckedSource> hdl_sources,
    const std::span<const CheckedSource> systemc_sources,
    const std::span<const CheckedSource> standard_sources) {
  semantic::Model result;
  SemanticModelBuilder builder{result};
  builder.add_sources(hdl_sources);
  builder.add_sources(systemc_sources);
  builder.add_sources(standard_sources);
  for (const auto& unit : parsed.units) {
    builder.add_unit(unit);
  }
  return result;
}

} // namespace fsim::app::application_detail
