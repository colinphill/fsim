// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {
namespace {

namespace di = semantic::design;

[[nodiscard]] std::string_view leaf_name(const std::string_view path) {
  const auto separator = path.find_last_of("./");
  return separator == std::string_view::npos
      ? path
      : path.substr(separator + 1U);
}

[[nodiscard]] std::string parent_path(const std::string_view path) {
  const auto separator = path.find_last_of("./");
  return separator == std::string_view::npos
      ? std::string{}
      : std::string{path.substr(0, separator)};
}

[[nodiscard]] bool owns_path(
    const std::string_view owner,
    const std::string_view path) noexcept {
  return path == owner
      || (path.size() > owner.size()
          && path.starts_with(owner)
          && (path[owner.size()] == '.' || path[owner.size()] == '/'));
}

[[nodiscard]] semantic::Language language(
    const frontend::Language input) noexcept {
  switch (input) {
    case frontend::Language::Vhdl2008:
      return semantic::Language::vhdl;
    case frontend::Language::Verilog2005:
      return semantic::Language::verilog;
    case frontend::Language::SystemVerilog2017:
      return semantic::Language::system_verilog;
  }
  return semantic::Language::system_verilog;
}

[[nodiscard]] di::Direction direction(
    const frontend::PortDirection input) noexcept {
  switch (input) {
    case frontend::PortDirection::Unknown:
      return di::Direction::unknown;
    case frontend::PortDirection::Input:
      return di::Direction::input;
    case frontend::PortDirection::Output:
      return di::Direction::output;
    case frontend::PortDirection::Inout:
      return di::Direction::inout;
    case frontend::PortDirection::Ref:
      return di::Direction::ref;
    case frontend::PortDirection::Buffer:
      return di::Direction::buffer;
  }
  return di::Direction::unknown;
}

[[nodiscard]] di::EdgeKind edge(
    const runtime::simir::EdgeKind input) noexcept {
  switch (input) {
    case runtime::simir::EdgeKind::any:
      return di::EdgeKind::any;
    case runtime::simir::EdgeKind::posedge:
      return di::EdgeKind::positive;
    case runtime::simir::EdgeKind::negedge:
      return di::EdgeKind::negative;
    case runtime::simir::EdgeKind::transaction:
      return di::EdgeKind::transaction;
  }
  return di::EdgeKind::any;
}

[[nodiscard]] di::ConversionKind conversion_kind(
    const elaboration::BoundaryConversionKind input) noexcept {
  switch (input) {
    case elaboration::BoundaryConversionKind::ordinal_alias:
      return di::ConversionKind::ordinal_alias;
    case elaboration::BoundaryConversionKind::width_adapter:
      return di::ConversionKind::width_adapter;
    case elaboration::BoundaryConversionKind::signedness_adapter:
      return di::ConversionKind::signedness_adapter;
    case elaboration::BoundaryConversionKind::width_signedness_adapter:
      return di::ConversionKind::width_signedness_adapter;
    case elaboration::BoundaryConversionKind::boolean_adapter:
      return di::ConversionKind::boolean_adapter;
    case elaboration::BoundaryConversionKind::integer_adapter:
      return di::ConversionKind::integer_adapter;
    case elaboration::BoundaryConversionKind::state_domain_alias:
      return di::ConversionKind::state_domain_alias;
  }
  return di::ConversionKind::ordinal_alias;
}

class DesignIrBuilder final {
 public:
  DesignIrBuilder(
      CheckedProject& checked,
      const elaboration::ElaboratedDesign& elaborated)
      : checked_(checked), model_(checked.semantics), elaborated_(elaborated) {}

  [[nodiscard]] di::DesignIr build() {
    result_.mutable_top() = elaborated_.top();
    result_.mutable_roots() = elaborated_.roots();
    add_hdl_specializations();
    add_systemc_specializations();
    link_instance_parents();
    add_hdl_objects();
    add_systemc_objects();
    add_processes();
    add_conversions();
    return std::move(result_);
  }

 private:
  [[nodiscard]] semantic::SourceSpanId source(
      const frontend::SourceSpan& span) {
    return intern_semantic_span(model_, span);
  }

  [[nodiscard]] std::optional<semantic::SourceSpanId> source(
      const runtime::simir::SourceLocation& location) {
    if (location.path.empty()) {
      return std::nullopt;
    }
    auto file = model_.find_source_file(location.path);
    if (!file) {
      file = model_.intern_source_file(location.path);
    }
    const semantic::SourcePosition position{
        0, location.line == 0 ? 1U : location.line,
        location.column == 0 ? 1U : location.column};
    return model_.intern_source_span(
        *file, location.path, position, position);
  }

  [[nodiscard]] std::optional<semantic::UnitId> find_unit(
      const elaboration::SpecializationInfo& input) const noexcept {
    const auto input_language = language(input.language);
    std::string_view primary = input.unit;
    std::string_view secondary;
    if (const auto open = primary.find('(');
        open != std::string_view::npos && primary.ends_with(')')) {
      secondary = primary.substr(open + 1U, primary.size() - open - 2U);
      primary = primary.substr(0, open);
    }
    if (const auto separator = primary.rfind('.');
        separator != std::string_view::npos) {
      primary = primary.substr(separator + 1U);
    }
    for (const auto& unit : model_.units()) {
      if (unit.language != input_language || unit.library != input.library) {
        continue;
      }
      if (!secondary.empty()) {
        if (unit.name == secondary && unit.secondary_name == primary) {
          return unit.id;
        }
        continue;
      }
      if (unit.name == primary || unit.secondary_name == primary) {
        return unit.id;
      }
    }
    for (const auto& unit : model_.units()) {
      if (unit.language != input_language || input.source.empty()) {
        continue;
      }
      const auto& span = model_.source_spans()[unit.source.value()];
      const auto& file = model_.source_files()[span.file.value()];
      if (file.physical_name == input.source) {
        return unit.id;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<semantic::InstanceId> find_source_instance(
      const elaboration::SpecializationInfo& input) const noexcept {
    auto name = std::string{leaf_name(input.instance)};
    if (const auto generated = name.find('[');
        generated != std::string::npos) {
      name.erase(generated);
    }
    std::optional<semantic::InstanceId> match;
    for (const auto& instance : model_.instances()) {
      if (instance.name != name) {
        continue;
      }
      if (match) {
        return std::nullopt;
      }
      match = instance.id;
    }
    return match;
  }

  [[nodiscard]] std::optional<semantic::DeclarationId> declaration(
      const semantic::ScopeId scope,
      const std::string_view name) const noexcept {
    const auto found = std::ranges::find_if(
        model_.declarations(), [&](const semantic::Declaration& item) {
          return item.scope == scope && item.name == name;
        });
    return found == model_.declarations().end()
        ? std::nullopt
        : std::optional<semantic::DeclarationId>{found->id};
  }

  [[nodiscard]] di::Direction declaration_direction(
      const semantic::DeclarationId id) const noexcept {
    const auto vhdl = std::ranges::find_if(
        checked_.vhdl_hir.declarations(),
        [&](const semantic::vhdl::Declaration& declaration) {
          return declaration.id == id;
        });
    if (vhdl != checked_.vhdl_hir.declarations().end()) {
      switch (vhdl->direction) {
        case semantic::vhdl::Direction::unknown:
          return di::Direction::unknown;
        case semantic::vhdl::Direction::input:
          return di::Direction::input;
        case semantic::vhdl::Direction::output:
          return di::Direction::output;
        case semantic::vhdl::Direction::inout:
          return di::Direction::inout;
        case semantic::vhdl::Direction::buffer:
          return di::Direction::buffer;
      }
    }
    const auto systemverilog = std::ranges::find_if(
        checked_.systemverilog_hir.declarations(),
        [&](const semantic::sv::Declaration& declaration) {
          return declaration.id == id;
        });
    if (systemverilog != checked_.systemverilog_hir.declarations().end()) {
      switch (systemverilog->direction) {
        case semantic::sv::Direction::unknown:
          return di::Direction::unknown;
        case semantic::sv::Direction::input:
          return di::Direction::input;
        case semantic::sv::Direction::output:
          return di::Direction::output;
        case semantic::sv::Direction::inout:
          return di::Direction::inout;
        case semantic::sv::Direction::ref:
          return di::Direction::ref;
      }
    }
    return di::Direction::unknown;
  }

  void add_parameter_values(
      const elaboration::SpecializationInfo& input,
      di::Specialization& output) const {
    for (const auto& [name, value] : input.parameter_values) {
      const auto identity = std::ranges::find_if(
          input.parameter_identity_values,
          [&](const auto& item) { return item.first == name; });
      output.parameters.push_back({
          name,
          value,
          identity == input.parameter_identity_values.end()
              ? value
              : identity->second,
          declaration(output.scope, name)});
    }
  }

  void add_callables(di::Specialization& output) const {
    if (!output.unit.valid()) {
      return;
    }
    for (const auto& declaration : model_.declarations()) {
      if (model_.scopes()[declaration.scope.value()].unit != output.unit) {
        continue;
      }
      if (declaration.kind == semantic::DeclarationKind::function
          || declaration.kind == semantic::DeclarationKind::procedure
          || declaration.kind == semantic::DeclarationKind::task) {
        output.callables.push_back(declaration.id);
      }
    }
  }

  void add_hdl_specializations() {
    auto& specializations = result_.mutable_specializations();
    auto& instances = result_.mutable_instances();
    const auto& inputs = elaborated_.specializations();
    hdl_specialization_count_ = inputs.size();
    specializations.reserve(inputs.size() + elaborated_.systemc_instances().size());
    instances.reserve(inputs.size() + elaborated_.systemc_instances().size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      const auto& input = inputs[index];
      const auto specialization_id = di::SpecializationId::from_index(
          static_cast<std::uint32_t>(specializations.size()));
      const auto instance_id = di::InstanceOccurrenceId::from_index(
          static_cast<std::uint32_t>(instances.size()));
      const auto unit = find_unit(input);
      di::Specialization specialization;
      specialization.id = specialization_id;
      specialization.unit = unit.value_or(semantic::UnitId{});
      specialization.scope = unit
          ? model_.units()[unit->value()].scope
          : semantic::ScopeId{};
      specialization.instance = instance_id;
      specialization.language = language(input.language);
      specialization.library = input.library;
      specialization.name = input.unit;
      if (unit) {
        specialization.source = model_.units()[unit->value()].source;
      }
      specialization.source_dependencies = input.source_dependencies;
      add_parameter_values(input, specialization);
      add_callables(specialization);
      di::InstanceOccurrence instance;
      instance.id = instance_id;
      instance.source_instance = find_source_instance(input);
      instance.specialization = specialization_id;
      instance.name = std::string{leaf_name(input.instance)};
      instance.path = input.instance;
      instance.target = input.unit;
      instance.source = specialization.source;
      if (unit) {
        instance.origin = model_.units()[unit->value()].origin;
      }
      specializations.push_back(std::move(specialization));
      instances.push_back(std::move(instance));
    }
  }

  void add_systemc_specializations() {
    for (const auto& input : elaborated_.systemc_instances()) {
      const auto specialization_id = di::SpecializationId::from_index(
          static_cast<std::uint32_t>(result_.specializations().size()));
      const auto instance_id = di::InstanceOccurrenceId::from_index(
          static_cast<std::uint32_t>(result_.instances().size()));
      di::Specialization specialization;
      specialization.id = specialization_id;
      specialization.instance = instance_id;
      specialization.language = semantic::Language::systemc;
      specialization.library = "systemc";
      specialization.name = input.target;
      for (const auto& [name, value] : input.construction_identity_values) {
        specialization.parameters.push_back({name, value, value, std::nullopt});
      }
      di::InstanceOccurrence instance;
      instance.id = instance_id;
      instance.specialization = specialization_id;
      instance.name = std::string{leaf_name(input.instance)};
      instance.path = input.instance;
      instance.target = input.target;
      result_.mutable_specializations().push_back(std::move(specialization));
      result_.mutable_instances().push_back(std::move(instance));
      systemc_specialization_by_handle_[input.native_handle] =
          specialization_id;
      add_boundary(
          di::BoundaryKind::systemc_instance,
          input.target,
          input.instance,
          instance_id,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          input.native_handle,
          std::nullopt);
    }
  }

  void link_instance_parents() {
    auto& instances = result_.mutable_instances();
    for (auto& instance : instances) {
      const auto parent = parent_path(instance.path);
      if (parent.empty()) {
        continue;
      }
      const auto found = std::ranges::find_if(
          instances, [&](const di::InstanceOccurrence& candidate) {
            return candidate.path == parent;
          });
      if (found != instances.end()) {
        instance.parent = found->id;
      }
    }
  }

  [[nodiscard]] di::SpecializationId specialization_for_path(
      const std::string_view path) const {
    std::optional<di::SpecializationId> result;
    std::size_t best{};
    for (const auto& instance : result_.instances()) {
      if (owns_path(instance.path, path) && instance.path.size() >= best) {
        result = instance.specialization;
        best = instance.path.size();
      }
    }
    if (result) {
      return *result;
    }
    return result_.specializations().empty()
        ? di::SpecializationId{}
        : result_.specializations().front().id;
  }

  [[nodiscard]] std::optional<semantic::ValueId> find_value(
      const std::string_view name,
      const semantic::SourceSpanId declaration_source,
      const di::SpecializationId specialization) const noexcept {
    for (const auto& value : model_.values()) {
      if (value.name == name && value.source == declaration_source) {
        return value.id;
      }
    }
    if (!specialization.valid()) {
      return std::nullopt;
    }
    const auto scope = result_.specializations()[specialization.value()].scope;
    if (!scope.valid()) {
      return std::nullopt;
    }
    const auto found = std::ranges::find_if(
        model_.values(), [&](const semantic::Value& value) {
          return value.scope == scope && value.name == name;
        });
    return found == model_.values().end()
        ? std::nullopt
        : std::optional<semantic::ValueId>{found->id};
  }

  [[nodiscard]] std::optional<semantic::DeclarationId> find_declaration(
      const std::optional<semantic::ValueId> value,
      const semantic::SourceSpanId source,
      const std::string_view name) const noexcept {
    const auto scope = value
        ? model_.values()[value->value()].scope
        : semantic::ScopeId{};
    const auto found = std::ranges::find_if(
        model_.declarations(), [&](const semantic::Declaration& item) {
          return item.name == name && item.source == source
              && (!scope.valid() || item.scope == scope);
        });
    return found == model_.declarations().end()
        ? std::nullopt
        : std::optional<semantic::DeclarationId>{found->id};
  }

  [[nodiscard]] di::ObjectId add_object(
      const di::ObjectKind kind,
      std::string name,
      std::string path,
      const di::SpecializationId specialization,
      const std::uint64_t runtime_index,
      const std::uint64_t native_handle,
      const std::uint64_t width,
      const bool signed_value,
      const std::optional<semantic::SourceSpanId> object_source,
      const std::optional<semantic::ValueId> value = std::nullopt,
      const std::optional<di::ObjectId> parent_object = std::nullopt) {
    const auto id = di::ObjectId::from_index(
        static_cast<std::uint32_t>(result_.objects().size()));
    semantic::TypeReference type;
    if (value) {
      type = model_.values()[value->value()].type;
    } else if (object_source) {
      type.source = *object_source;
    }
    result_.mutable_objects().push_back({
        id,
        specialization,
        kind,
        std::move(name),
        std::move(path),
        value,
        std::move(type),
        object_source,
        parent_object,
        runtime_index,
        native_handle,
        width,
        signed_value,
        {}});
    if (specialization.valid()) {
      result_.mutable_specializations()[specialization.value()]
          .objects.push_back(id);
    }
    return id;
  }

  [[nodiscard]] semantic::PortId add_port(
      const di::ObjectId object,
      const std::optional<semantic::DeclarationId> declaration_id,
      const di::Direction object_direction,
      const bool export_object,
      const bool writable,
      const std::optional<semantic::SourceSpanId> port_source) {
    const auto& stored = result_.objects()[object.value()];
    const auto& specialization =
        result_.specializations()[stored.specialization.value()];
    const auto id = semantic::PortId::from_index(
        static_cast<std::uint32_t>(result_.ports().size()));
    result_.mutable_ports().push_back({
        id,
        specialization.instance,
        object,
        declaration_id,
        object_direction,
        export_object,
        writable,
        port_source});
    return id;
  }

  void add_signal_object(const elaboration::SignalInfo& input) {
    const auto specialization = specialization_for_path(input.name);
    const auto object_source = source(input.declaration_span);
    const auto name = std::string{leaf_name(input.name)};
    const auto value = find_value(name, object_source, specialization);
    const auto object = add_object(
        di::ObjectKind::signal,
        name,
        input.name,
        specialization,
        input.id,
        0,
        input.width,
        input.is_signed,
        object_source,
        value);
    signal_object_by_runtime_.try_emplace(input.id, object);
    object_by_path_.try_emplace(input.name, object);
    if (input.is_port) {
      const auto declaration_id = find_declaration(value, object_source, name);
      const auto port = add_port(
          object,
          declaration_id,
          direction(input.direction),
          false,
          input.direction != frontend::PortDirection::Input,
          object_source);
      port_by_path_[input.name] = port;
    }
  }

  void add_signal_aliases() {
    for (const auto& [path, runtime_signal] : elaborated_.signal_paths()) {
      if (object_by_path_.contains(path)) {
        continue;
      }
      const auto base = signal_object_by_runtime_.find(runtime_signal);
      if (base == signal_object_by_runtime_.end()) {
        continue;
      }
      const auto& base_object = result_.objects()[base->second.value()];
      const auto specialization = specialization_for_path(path);
      const auto& specialization_record =
          result_.specializations()[specialization.value()];
      const auto alias_name = std::string{leaf_name(path)};
      const auto declaration_id = declaration(
          specialization_record.scope, alias_name);
      std::optional<semantic::ValueId> alias_value;
      if (specialization_record.scope.valid()) {
        const auto value = std::ranges::find_if(
            model_.values(), [&](const semantic::Value& candidate) {
              return candidate.scope == specialization_record.scope
                  && candidate.name == alias_name;
            });
        if (value != model_.values().end()) {
          alias_value = value->id;
        }
      }
      const auto alias_source = declaration_id
          ? std::optional<semantic::SourceSpanId>{
                model_.declarations()[declaration_id->value()].source}
          : base_object.source;
      const auto object = add_object(
          di::ObjectKind::signal,
          alias_name,
          path,
          specialization,
          runtime_signal,
          0,
          base_object.width,
          base_object.signed_value,
          alias_source,
          alias_value ? alias_value : base_object.declaration_value,
          base->second);
      object_by_path_[path] = object;
      if (declaration_id
          && model_.declarations()[declaration_id->value()].kind
              == semantic::DeclarationKind::port) {
        port_by_path_[path] = add_port(
            object,
            declaration_id,
            declaration_direction(*declaration_id),
            false,
            declaration_direction(*declaration_id) != di::Direction::input,
            alias_source);
      }
    }
  }

  void add_container_aliases() {
    for (const auto& [path, runtime_object] :
         elaborated_.container_paths()) {
      if (object_by_path_.contains(path)) {
        continue;
      }
      const auto base = container_object_by_runtime_.find(runtime_object);
      if (base == container_object_by_runtime_.end()) {
        continue;
      }
      const auto& base_object = result_.objects()[base->second.value()];
      const auto specialization = specialization_for_path(path);
      const auto& specialization_record =
          result_.specializations()[specialization.value()];
      const auto alias_name = std::string{leaf_name(path)};
      const auto declaration_id = declaration(
          specialization_record.scope, alias_name);
      std::optional<semantic::ValueId> alias_value;
      if (specialization_record.scope.valid()) {
        const auto value = std::ranges::find_if(
            model_.values(), [&](const semantic::Value& candidate) {
              return candidate.scope == specialization_record.scope
                  && candidate.name == alias_name;
            });
        if (value != model_.values().end()) {
          alias_value = value->id;
        }
      }
      const auto alias_source = declaration_id
          ? std::optional<semantic::SourceSpanId>{
                model_.declarations()[declaration_id->value()].source}
          : base_object.source;
      const auto external_type = base_object.external_type;
      const auto object = add_object(
          di::ObjectKind::container,
          alias_name,
          path,
          specialization,
          runtime_object,
          0,
          base_object.width,
          base_object.signed_value,
          alias_source,
          alias_value ? alias_value : base_object.declaration_value,
          base->second);
      result_.mutable_objects()[object.value()].external_type =
          external_type;
      object_by_path_[path] = object;
      if (declaration_id
          && model_.declarations()[declaration_id->value()].kind
              == semantic::DeclarationKind::port) {
        port_by_path_[path] = add_port(
            object,
            declaration_id,
            declaration_direction(*declaration_id),
            false,
            declaration_direction(*declaration_id) != di::Direction::input,
            alias_source);
      }
    }
  }

  void add_hdl_objects() {
    for (const auto& input : elaborated_.signals()) {
      add_signal_object(input);
    }
    add_signal_aliases();
    for (const auto& input : elaborated_.string_objects()) {
      const auto specialization = specialization_for_path(input.name);
      const auto object_source = source(input.declaration_span);
      const auto name = std::string{leaf_name(input.name)};
      const auto value = find_value(name, object_source, specialization);
      const auto object = add_object(
          di::ObjectKind::string,
          name,
          input.name,
          specialization,
          input.id,
          0,
          0,
          false,
          object_source,
          value);
      object_by_path_[input.name] = object;
      if (input.is_port) {
        port_by_path_[input.name] = add_port(
            object,
            find_declaration(value, object_source, name),
            direction(input.direction),
            false,
            input.direction != frontend::PortDirection::Input,
            object_source);
      }
    }
    for (const auto& input : elaborated_.container_objects()) {
      const auto specialization = specialization_for_path(input.name);
      const auto object_source = source(input.declaration_span);
      const auto name = std::string{leaf_name(input.name)};
      const auto value = find_value(name, object_source, specialization);
      std::optional<di::ObjectId> parent;
      if (input.slice_alias) {
        const auto found = container_object_by_runtime_.find(
            input.slice_alias->object);
        if (found != container_object_by_runtime_.end()) {
          parent = found->second;
        }
      }
      const auto object = add_object(
          di::ObjectKind::container,
          name,
          input.name,
          specialization,
          input.id,
          0,
          input.type.element_width,
          input.type.signed_elements,
          object_source,
          value,
          parent);
      container_object_by_runtime_[input.id] = object;
      object_by_path_[input.name] = object;
      if (input.is_port) {
        port_by_path_[input.name] = add_port(
            object,
            find_declaration(value, object_source, name),
            direction(input.direction),
            false,
            input.direction != frontend::PortDirection::Input,
            object_source);
      }
    }
    add_container_aliases();
    for (const auto& input : elaborated_.vhdl_protected_objects()) {
      const auto specialization = specialization_for_path(input.name);
      const auto object_source = source(input.declaration_span);
      const auto object = add_object(
          di::ObjectKind::protected_object,
          std::string{leaf_name(input.name)},
          input.name,
          specialization,
          input.id,
          0,
          0,
          false,
          object_source);
      object_by_path_[input.name] = object;
      for (const auto& member : input.members) {
        const auto member_path = input.name + "." + member.name;
        const auto member_source = source(member.declaration_span);
        const auto child = add_object(
            di::ObjectKind::protected_member,
            member.name,
            member_path,
            specialization,
            member.storage,
            0,
            member.width,
            member.type.is_signed,
            member_source,
            std::nullopt,
            object);
        object_by_path_[member_path] = child;
      }
    }
  }

  [[nodiscard]] di::ObjectKind systemc_object_kind(
      const elaboration::SystemCNamedObjectKind kind) const noexcept {
    switch (kind) {
      case elaboration::SystemCNamedObjectKind::module:
      case elaboration::SystemCNamedObjectKind::foreign_child:
        return di::ObjectKind::systemc_module;
      case elaboration::SystemCNamedObjectKind::port:
        return di::ObjectKind::systemc_port;
      case elaboration::SystemCNamedObjectKind::event:
        return di::ObjectKind::systemc_event;
      case elaboration::SystemCNamedObjectKind::primitive_channel:
        return di::ObjectKind::systemc_channel;
      case elaboration::SystemCNamedObjectKind::signal:
        return di::ObjectKind::systemc_signal;
      case elaboration::SystemCNamedObjectKind::export_object:
        return di::ObjectKind::systemc_export;
      case elaboration::SystemCNamedObjectKind::process:
        return di::ObjectKind::systemc_module;
    }
    return di::ObjectKind::systemc_module;
  }

  [[nodiscard]] di::BoundaryKind systemc_boundary_kind(
      const di::ObjectKind kind) const noexcept {
    switch (kind) {
      case di::ObjectKind::systemc_module:
        return di::BoundaryKind::systemc_instance;
      case di::ObjectKind::systemc_port:
        return di::BoundaryKind::systemc_port;
      case di::ObjectKind::systemc_event:
        return di::BoundaryKind::systemc_event;
      case di::ObjectKind::systemc_channel:
        return di::BoundaryKind::systemc_channel;
      case di::ObjectKind::systemc_signal:
        return di::BoundaryKind::systemc_signal;
      case di::ObjectKind::systemc_export:
        return di::BoundaryKind::systemc_export;
      default:
        return di::BoundaryKind::systemc_instance;
    }
  }

  [[nodiscard]] di::ObjectId add_systemc_object(
      const di::ObjectKind kind,
      const std::string& name,
      const std::string& path,
      const di::SpecializationId specialization,
      const std::uint64_t native_handle,
      const std::optional<runtime::simir::SignalId> signal,
      const bool export_writable = false) {
    const auto named = std::ranges::find_if(
        elaborated_.systemc_objects(), [&](const auto& candidate) {
          return candidate.native_handle == native_handle
              && candidate.name == path;
        });
    const auto object_source = named == elaborated_.systemc_objects().end()
        ? std::nullopt
        : source(named->source);
    const auto object = add_object(
        kind,
        name,
        path,
        specialization,
        signal.value_or(0),
        native_handle,
        signal ? result_.objects()[
            signal_object_by_runtime_.at(*signal).value()].width : 0,
        false,
        object_source);
    if (named != elaborated_.systemc_objects().end()) {
      result_.mutable_objects()[object.value()].external_type =
          named->type_name;
    }
    object_by_path_.try_emplace(path, object);
    std::optional<semantic::PortId> port;
    if (kind == di::ObjectKind::systemc_port
        || kind == di::ObjectKind::systemc_export) {
      port = add_port(
          object,
          std::nullopt,
          kind == di::ObjectKind::systemc_export
              ? di::Direction::inout
              : di::Direction::unknown,
          kind == di::ObjectKind::systemc_export,
          kind == di::ObjectKind::systemc_export && export_writable,
          std::nullopt);
      port_by_path_[path] = *port;
    }
    add_boundary(
        systemc_boundary_kind(kind),
        name,
        path,
        result_.specializations()[specialization.value()].instance,
        object,
        port,
        std::nullopt,
        native_handle,
        object_source);
    return object;
  }

  void add_systemc_objects() {
    for (const auto& instance : elaborated_.systemc_instances()) {
      const auto specialization =
          systemc_specialization_by_handle_.at(instance.native_handle);
      if (!object_by_path_.contains(instance.instance)) {
        static_cast<void>(add_systemc_object(
            di::ObjectKind::systemc_module,
            std::string{leaf_name(instance.instance)},
            instance.instance,
            specialization,
            instance.native_handle,
            std::nullopt));
      }
      for (const auto& port : instance.ports) {
        static_cast<void>(add_systemc_object(
            di::ObjectKind::systemc_port,
            port.name,
            instance.instance + "." + port.name,
            specialization,
            port.native_handle,
            port.signal));
      }
      for (const auto& event : instance.events) {
        static_cast<void>(add_systemc_object(
            di::ObjectKind::systemc_event,
            event.name,
            instance.instance + "." + event.name,
            specialization,
            event.native_handle,
            event.signal));
      }
      for (const auto& channel : instance.primitive_channels) {
        static_cast<void>(add_systemc_object(
            di::ObjectKind::systemc_channel,
            channel.name,
            instance.instance + "." + channel.name,
            specialization,
            channel.native_handle,
            std::nullopt));
      }
      for (const auto& signal : instance.internal_signals) {
        static_cast<void>(add_systemc_object(
            di::ObjectKind::systemc_signal,
            signal.name,
            instance.instance + "." + signal.name,
            specialization,
            signal.native_handle,
            signal.signal));
      }
      for (const auto& export_object : instance.exports) {
        static_cast<void>(add_systemc_object(
            di::ObjectKind::systemc_export,
            export_object.name,
            instance.instance + "." + export_object.name,
            specialization,
            export_object.native_handle,
            export_object.signal,
            export_object.writable));
      }
    }
    for (const auto& input : elaborated_.systemc_objects()) {
      if (input.kind == elaboration::SystemCNamedObjectKind::process) {
        continue;
      }
      const auto expected_kind = systemc_object_kind(input.kind);
      if (const auto existing = object_by_path_.find(input.name);
          existing != object_by_path_.end()
          && result_.objects()[existing->second.value()].kind
              == expected_kind) {
        continue;
      }
      const auto specialization = specialization_for_path(input.name);
      static_cast<void>(add_systemc_object(
          expected_kind,
          std::string{leaf_name(input.name)},
          input.name,
          specialization,
          input.native_handle,
          input.signal));
    }
  }

  void flatten_generate_processes(
      const semantic::vhdl::GenerateRegion& input,
      std::vector<semantic::ProcessId>& output) const {
    output.insert(output.end(), input.processes.begin(), input.processes.end());
    for (const auto& nested : input.nested) {
      flatten_generate_processes(nested, output);
    }
  }

  void flatten_generate_processes(
      const semantic::sv::GenerateRegion& input,
      std::vector<semantic::ProcessId>& output) const {
    output.insert(output.end(), input.processes.begin(), input.processes.end());
    for (const auto& nested : input.nested) {
      flatten_generate_processes(nested, output);
    }
  }

  [[nodiscard]] std::vector<semantic::ProcessId> unit_processes(
      const semantic::UnitId unit) const {
    std::vector<semantic::ProcessId> output;
    const auto vhdl = std::ranges::find_if(
        checked_.vhdl_hir.units(),
        [&](const semantic::vhdl::Unit& item) { return item.id == unit; });
    if (vhdl != checked_.vhdl_hir.units().end()) {
      output = vhdl->processes;
      for (const auto& generate : vhdl->generates) {
        flatten_generate_processes(generate, output);
      }
      return output;
    }
    const auto systemverilog = std::ranges::find_if(
        checked_.systemverilog_hir.units(),
        [&](const semantic::sv::Unit& item) { return item.id == unit; });
    if (systemverilog != checked_.systemverilog_hir.units().end()) {
      output = systemverilog->processes;
      for (const auto& generate : systemverilog->generates) {
        flatten_generate_processes(generate, output);
      }
    }
    return output;
  }

  [[nodiscard]] std::optional<semantic::ProcessId> source_process(
      const di::SpecializationId specialization,
      const runtime::simir::ProcessId runtime_process) const {
    if (specialization.value() >= hdl_specialization_count_) {
      return std::nullopt;
    }
    const auto& legacy = elaborated_.specializations()[specialization.value()];
    const auto found = std::ranges::find(legacy.processes, runtime_process);
    if (found == legacy.processes.end()) {
      return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(
        std::distance(legacy.processes.begin(), found));
    const auto processes = unit_processes(
        result_.specializations()[specialization.value()].unit);
    return index < processes.size()
        ? std::optional<semantic::ProcessId>{processes[index]}
        : std::nullopt;
  }

  [[nodiscard]] di::SpecializationId process_specialization(
      const runtime::simir::ProcessId process,
      const std::string_view name) const {
    for (std::size_t index = 0; index < hdl_specialization_count_; ++index) {
      const auto& processes = elaborated_.specializations()[index].processes;
      if (std::ranges::find(processes, process) != processes.end()) {
        return di::SpecializationId::from_index(
            static_cast<std::uint32_t>(index));
      }
    }
    for (const auto& object : elaborated_.systemc_objects()) {
      if (object.process && *object.process == process) {
        return specialization_for_path(object.name);
      }
    }
    return specialization_for_path(name);
  }

  [[nodiscard]] std::optional<di::ObjectId> signal_object(
      const runtime::simir::SignalId signal) const noexcept {
    const auto found = signal_object_by_runtime_.find(signal);
    return found == signal_object_by_runtime_.end()
        ? std::nullopt
        : std::optional<di::ObjectId>{found->second};
  }

  void add_processes() {
    std::set<runtime::simir::ProcessId> boundary_processes;
    for (const auto& conversion : elaborated_.boundary_conversions()) {
      if (conversion.process) {
        boundary_processes.insert(*conversion.process);
      }
    }
    std::set<runtime::simir::ProcessId> systemc_processes;
    for (const auto& process : elaborated_.systemc_processes()) {
      systemc_processes.insert(process.process);
    }
    for (std::size_t index = 0; index < elaborated_.processes().size(); ++index) {
      const auto& input = elaborated_.processes()[index];
      const auto specialization = process_specialization(input.id, input.name);
      const auto source_id = source_process(specialization, input.id);
      const auto id = di::ProcessOccurrenceId::from_index(
          static_cast<std::uint32_t>(result_.processes().size()));
      di::ProcessOccurrence output;
      output.id = id;
      output.specialization = specialization;
      output.source_process = source_id;
      output.name = input.name;
      output.runtime_index = input.id;
      output.initialize = input.initialize;
      output.observed = input.observed;
      output.reactive = input.reactive;
      output.final = input.final;
      if (source_id) {
        output.source = model_.process_identities()[source_id->value()].source;
      } else {
        const auto named = std::ranges::find_if(
            elaborated_.systemc_objects(), [&](const auto& object) {
              return object.process && *object.process == input.id;
            });
        if (named != elaborated_.systemc_objects().end()) {
          output.source = source(named->source);
        }
      }
      result_.mutable_processes().push_back(std::move(output));
      process_by_runtime_[input.id] = id;
      result_.mutable_specializations()[specialization.value()]
          .processes.push_back(id);
      auto& stored = result_.mutable_processes().back();
      for (const auto& sensitivity : input.static_sensitivity) {
        const auto object = signal_object(sensitivity.signal);
        if (!object) {
          continue;
        }
        const auto sensitivity_id = di::SensitivityId::from_index(
            static_cast<std::uint32_t>(result_.sensitivities().size()));
        result_.mutable_sensitivities().push_back({
            sensitivity_id, id, *object, edge(sensitivity.edge)});
        stored.sensitivities.push_back(sensitivity_id);
      }
      if (input.switch_bidirectional) {
        continue;
      }
      for (const auto& region : input.driver_regions) {
        const auto object = signal_object(region.signal);
        if (!object) {
          continue;
        }
        const auto driver_id = semantic::DriverId::from_index(
            static_cast<std::uint32_t>(result_.drivers().size()));
        const auto transaction_id = di::TransactionId::from_index(
            static_cast<std::uint32_t>(result_.transactions().size()));
        auto transaction_kind = di::TransactionKind::procedural;
        if (boundary_processes.contains(input.id)) {
          transaction_kind = di::TransactionKind::boundary_adapter;
        } else if (systemc_processes.contains(input.id)) {
          transaction_kind = di::TransactionKind::systemc_update;
        } else if (!source_id) {
          transaction_kind = di::TransactionKind::continuous;
        }
        result_.mutable_transactions().push_back({
            transaction_id,
            id,
            driver_id,
            *object,
            transaction_kind,
            region.offset,
            region.width,
            region.whole});
        result_.mutable_drivers().push_back({
            driver_id,
            id,
            *object,
            region.offset,
            region.width,
            region.whole,
            {transaction_id}});
        stored.drivers.push_back(driver_id);
        stored.transactions.push_back(transaction_id);
      }
    }
    for (const auto& input : elaborated_.systemc_processes()) {
      const auto process = process_by_runtime_.find(input.process);
      if (process == process_by_runtime_.end()) {
        continue;
      }
      const auto named = std::ranges::find_if(
          elaborated_.systemc_objects(), [&](const auto& object) {
            return object.process && *object.process == input.process;
          });
      add_boundary(
          di::BoundaryKind::systemc_process,
          named == elaborated_.systemc_objects().end()
              ? result_.processes()[process->second.value()].name
              : std::string{leaf_name(named->name)},
          named == elaborated_.systemc_objects().end()
              ? result_.processes()[process->second.value()].name
              : named->name,
          result_.specializations()[
              result_.processes()[process->second.value()]
                  .specialization.value()].instance,
          std::nullopt,
          std::nullopt,
          process->second,
          input.native_handle,
          result_.processes()[process->second.value()].source);
    }
  }

  void add_conversions() {
    for (const auto& input : elaborated_.boundary_conversions()) {
      const auto formal = signal_object(input.formal_signal);
      const auto actual = signal_object(input.actual_signal);
      if (!formal || !actual) {
        continue;
      }
      const auto id = di::ConversionId::from_index(
          static_cast<std::uint32_t>(result_.conversions().size()));
      const auto process = input.process
          ? process_by_runtime_.find(*input.process)
          : process_by_runtime_.end();
      const auto conversion_source = source(input.connection_span);
      result_.mutable_conversions().push_back({
          id,
          conversion_kind(input.kind),
          input.path,
          *formal,
          *actual,
          process == process_by_runtime_.end()
              ? std::nullopt
              : std::optional<di::ProcessOccurrenceId>{process->second},
          direction(input.direction),
          input.formal_width,
          input.actual_width,
          input.formal_signed,
          input.actual_signed,
          input.state_domain_changed,
          conversion_source});
      add_boundary(
          di::BoundaryKind::language_conversion,
          std::string{leaf_name(input.path)},
          input.path,
          result_.specializations()[
              result_.objects()[formal->value()].specialization.value()]
              .instance,
          formal,
          port_by_path_.contains(input.path)
              ? std::optional<semantic::PortId>{port_by_path_.at(input.path)}
              : std::nullopt,
          process == process_by_runtime_.end()
              ? std::nullopt
              : std::optional<di::ProcessOccurrenceId>{process->second},
          0,
          conversion_source,
          id);
    }
  }

  void add_boundary(
      const di::BoundaryKind kind,
      std::string name,
      std::string path,
      const std::optional<di::InstanceOccurrenceId> instance,
      const std::optional<di::ObjectId> object,
      const std::optional<semantic::PortId> port,
      const std::optional<di::ProcessOccurrenceId> process,
      const std::uint64_t native_handle,
      const std::optional<semantic::SourceSpanId> boundary_source,
      const std::optional<di::ConversionId> conversion = std::nullopt) {
    const auto id = di::BoundaryId::from_index(
        static_cast<std::uint32_t>(result_.boundaries().size()));
    result_.mutable_boundaries().push_back({
        id,
        kind,
        std::move(name),
        std::move(path),
        instance,
        object,
        port,
        process,
        conversion,
        native_handle,
        boundary_source});
  }

  CheckedProject& checked_;
  semantic::Model& model_;
  const elaboration::ElaboratedDesign& elaborated_;
  di::DesignIr result_;
  std::size_t hdl_specialization_count_{};
  std::map<std::uint64_t, di::SpecializationId>
      systemc_specialization_by_handle_;
  std::map<runtime::simir::SignalId, di::ObjectId>
      signal_object_by_runtime_;
  std::map<runtime::simir::ContainerObjectId, di::ObjectId>
      container_object_by_runtime_;
  std::map<std::string, di::ObjectId> object_by_path_;
  std::map<std::string, semantic::PortId> port_by_path_;
  std::map<runtime::simir::ProcessId, di::ProcessOccurrenceId>
      process_by_runtime_;
};

} // namespace

semantic::design::DesignIr build_design_ir(
    CheckedProject& checked,
    const elaboration::ElaboratedDesign& elaborated) {
  return DesignIrBuilder{checked, elaborated}.build();
}

bool design_object_is_signal_bearing(
    const semantic::design::Object& object) noexcept {
  using Kind = semantic::design::ObjectKind;
  if (object.kind == Kind::signal) {
    return true;
  }
  return object.width != 0
      && (object.kind == Kind::systemc_port
          || object.kind == Kind::systemc_event
          || object.kind == Kind::systemc_signal
          || object.kind == Kind::systemc_export);
}

bool valid_runtime_projection(
    const semantic::design::DesignIr& design,
    const elaboration::ElaboratedDesign& runtime) noexcept {
  using ObjectKind = semantic::design::ObjectKind;
  using BoundaryKind = semantic::design::BoundaryKind;
  if (!design.valid() || design.top() != runtime.top()
      || design.roots() != runtime.roots()) {
    return false;
  }
  const auto hdl_specialization_count = static_cast<std::size_t>(
      std::ranges::count_if(
          design.specializations(), [](const auto& specialization) {
            return specialization.language != semantic::Language::systemc;
          }));
  if (hdl_specialization_count != runtime.specializations().size()
      || design.processes().size() != runtime.processes().size()
      || design.conversions().size()
          != runtime.boundary_conversions().size()) {
    return false;
  }
  for (const auto& specialization : runtime.specializations()) {
    if (specialization.id >= design.specializations().size()) {
      return false;
    }
    const auto& projected = design.specializations()[specialization.id];
    if (projected.language == semantic::Language::systemc
        || design.instances()[projected.instance.value()].path
            != specialization.instance) {
      return false;
    }
  }
  for (const auto& signal : runtime.signals()) {
    const auto matches = std::ranges::count_if(
        design.objects(), [&](const auto& object) {
          return object.kind == ObjectKind::signal
              && !object.parent_object && object.runtime_index == signal.id
              && object.width == signal.width;
        });
    if (matches != 1) {
      return false;
    }
  }
  for (const auto& [path, signal] : runtime.signal_paths()) {
    if (std::ranges::none_of(
            design.objects(), [&](const auto& object) {
              return design_object_is_signal_bearing(object)
                  && object.path == path && object.runtime_index == signal;
            })) {
      return false;
    }
  }
  for (const auto& [path, object] : runtime.container_paths()) {
    if (std::ranges::none_of(
            design.objects(), [&](const auto& candidate) {
              return candidate.kind == ObjectKind::container
                  && candidate.path == path
                  && candidate.runtime_index == object;
            })) {
      return false;
    }
  }
  for (std::size_t index = 0; index < runtime.processes().size(); ++index) {
    const auto projected = std::ranges::find(
        design.processes(), index,
        &semantic::design::ProcessOccurrence::runtime_index);
    if (projected == design.processes().end()
        || projected->name != runtime.processes()[index].name
        || projected->initialize != runtime.processes()[index].initialize
        || projected->observed != runtime.processes()[index].observed
        || projected->reactive != runtime.processes()[index].reactive
        || projected->final != runtime.processes()[index].final
        || std::ranges::count(
               design.processes(), index,
               &semantic::design::ProcessOccurrence::runtime_index)
            != 1) {
        return false;
    }
    if (runtime.processes()[index].switch_bidirectional
        && (!projected->drivers.empty()
            || !projected->transactions.empty()
            || std::ranges::any_of(
                design.drivers(), [&](const auto& driver) {
                  return driver.process == projected->id;
                })
            || std::ranges::any_of(
                design.transactions(), [&](const auto& transaction) {
                  return transaction.process == projected->id;
                }))) {
      return false;
    }
  }
  const auto systemc_specialization_count = static_cast<std::size_t>(
      std::ranges::count_if(
          design.specializations(), [](const auto& specialization) {
            return specialization.language == semantic::Language::systemc;
          }));
  if (systemc_specialization_count != runtime.systemc_instances().size()) {
    return false;
  }
  for (const auto& instance : runtime.systemc_instances()) {
    if (std::ranges::none_of(
            design.boundaries(), [&](const auto& boundary) {
              return boundary.kind == BoundaryKind::systemc_instance
                  && boundary.path == instance.instance;
            })) {
      return false;
    }
  }
  for (const auto& input : runtime.systemc_objects()) {
    if (input.kind == elaboration::SystemCNamedObjectKind::process) {
      if (std::ranges::none_of(
              design.boundaries(), [&](const auto& boundary) {
                return boundary.kind == BoundaryKind::systemc_process
                    && boundary.path == input.name && boundary.process
                    && input.process
                    && design.processes()[boundary.process->value()]
                           .runtime_index == *input.process;
              })) {
        return false;
      }
      continue;
    }
    const auto expected = [&] {
      switch (input.kind) {
        case elaboration::SystemCNamedObjectKind::module:
        case elaboration::SystemCNamedObjectKind::foreign_child:
          return ObjectKind::systemc_module;
        case elaboration::SystemCNamedObjectKind::port:
          return ObjectKind::systemc_port;
        case elaboration::SystemCNamedObjectKind::event:
          return ObjectKind::systemc_event;
        case elaboration::SystemCNamedObjectKind::primitive_channel:
          return ObjectKind::systemc_channel;
        case elaboration::SystemCNamedObjectKind::signal:
          return ObjectKind::systemc_signal;
        case elaboration::SystemCNamedObjectKind::export_object:
          return ObjectKind::systemc_export;
        case elaboration::SystemCNamedObjectKind::process:
          break;
      }
      return ObjectKind::systemc_module;
    }();
    if (std::ranges::none_of(
            design.objects(), [&](const auto& object) {
              return object.kind == expected && object.path == input.name;
            })) {
      return false;
    }
  }
  return true;
}

} // namespace fsim::app::application_detail
