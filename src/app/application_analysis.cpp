// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/support/path.hpp"

namespace fsim::app::application_detail {

std::atomic_bool interrupt_requested{};

ApplicationSystemCFactoryProvider::ApplicationSystemCFactoryProvider(
      const std::span<const SystemCLibraryRegistry> registries,
      const std::span<
          const elaboration::SystemCInstanceDescription> eager_instances,
      std::vector<std::uint64_t>& lifecycle_roots)
      : registries_(registries.begin(), registries.end()),
        lifecycle_roots_(lifecycle_roots)  {
    for (const auto& instance : eager_instances) {
      const auto parsed = parse_binding_target(instance.target);
      const auto found = parsed
          ? std::ranges::find(
                registries_, parsed->qualifier,
                &SystemCLibraryRegistry::library)
          : registries_.end();
      if (found != registries_.end()) {
        record_handles(instance.path, instance, found->registry.get());
      }
    }
  }

std::shared_ptr<systemc::HierarchyRegistry>
ApplicationSystemCFactoryProvider::registry_for_handle(
    const std::uint64_t handle) const {
  const auto found = std::ranges::find_if(
      registries_, [&](const auto& entry) {
        return entry.registry->owns_handle(handle);
      });
  return found == registries_.end() ? nullptr : found->registry;
}

std::vector<elaboration::SystemCFactoryCandidate>
ApplicationSystemCFactoryProvider::candidates() const {
  std::vector<elaboration::SystemCFactoryCandidate> result;
  for (const auto& entry : registries_) {
    for (const auto& name : entry.registry->factory_names()) {
      if (!entry.registry->has_elaboration_factory(name)) {
        continue;
      }
      result.push_back({
          entry.library,
          name,
          "systemc:" + entry.library + "." + name});
    }
  }
  return result;
}

std::vector<std::string>
ApplicationSystemCFactoryProvider::libraries() const {
  std::vector<std::string> result;
  result.reserve(registries_.size());
  for (const auto& entry : registries_) {
    result.push_back(entry.library);
  }
  return result;
}

std::optional<std::vector<
    elaboration::SystemCConstructionParameter>>
ApplicationSystemCFactoryProvider::schema(
    const std::string_view target,
    std::string& error)  {
    error.clear();
    const auto parsed = parse_binding_target(target);
    if (!parsed || parsed->language != "systemc"
        || parsed->qualifier.empty()) {
      error = "target must use systemc:plugin.factory spelling";
      return std::nullopt;
    }
    const auto registry = std::ranges::find(
        registries_, parsed->qualifier,
        &SystemCLibraryRegistry::library);
    if (registry == registries_.end()) {
      error = "logical library '" + parsed->qualifier
          + "' has no compiled SystemC plug-in";
      return std::nullopt;
    }
    const auto parameters =
        registry->registry->factory_parameters(parsed->unit);
    if (!parameters) {
      error = "factory '" + parsed->unit
          + "' was not registered";
      return std::nullopt;
    }
    std::vector<elaboration::SystemCConstructionParameter>
        result;
    result.reserve(parameters->size());
    for (const auto& parameter : *parameters) {
      result.push_back({
          parameter.name,
          parameter.type,
          parameter.default_value,
      });
    }
    return result;
  }

std::optional<elaboration::SystemCInstanceDescription>
ApplicationSystemCFactoryProvider::instantiate(
    const std::string_view path,
    const std::string_view target,
    const std::span<
        const std::pair<std::string, std::int64_t>>
        construction_values,
    std::string& error)  {
    error.clear();
    const auto parsed = parse_binding_target(target);
    if (!parsed || parsed->language != "systemc"
        || parsed->qualifier.empty()) {
      error = "target must use systemc:plugin.factory spelling";
      return std::nullopt;
    }
    const auto registry = std::ranges::find(
        registries_, parsed->qualifier,
        &SystemCLibraryRegistry::library);
    if (registry == registries_.end()) {
      error = "logical library '" + parsed->qualifier
          + "' has no compiled SystemC plug-in";
      return std::nullopt;
    }
    fsim_sc_handle_v1 parent = 0;
    if (const auto separator = path.rfind('.');
        separator != std::string_view::npos) {
      const auto found =
          handles_.find(std::string{path.substr(0, separator)});
      if (found != handles_.end()
          && found->second.registry == registry->registry.get()) {
        parent = found->second.handle;
      }
    }
    auto module = registry->registry->instantiate(
        parsed->unit,
        path,
        parent,
        construction_values,
        error);
    if (!module) {
      return std::nullopt;
    }
    record_handles(
        std::string{path}, *module, registry->registry.get());
    lifecycle_roots_.push_back(module->handle);
    return systemc_description(path, target, *module);
  }

void ApplicationSystemCFactoryProvider::record_handles(
      const std::string& path,
      const elaboration::SystemCInstanceDescription& description,
      systemc::HierarchyRegistry* registry)  {
    handles_.emplace(path, HandleOwner{description.handle, registry});
    for (const auto& child : description.native_children) {
      record_handles(child.path, child, registry);
    }
  }

void ApplicationSystemCFactoryProvider::record_handles(
    const std::string& path,
    const systemc::ModuleDescription& description,
    systemc::HierarchyRegistry* registry)  {
    handles_.emplace(path, HandleOwner{description.handle, registry});
    for (const auto& child : description.native_children) {
      record_handles(path + "." + child.instance, child, registry);
    }
  }

InterruptSignalGuard::InterruptSignalGuard() noexcept
     : previous_(std::signal(SIGINT, handle_interrupt))  {}

InterruptSignalGuard::~InterruptSignalGuard()  {
    if (previous_ != SIG_ERR) {
      (void)std::signal(SIGINT, previous_);
    }
  }

extern "C" void handle_interrupt(int)  {
  interrupt_requested.store(true, std::memory_order_relaxed);
}

diagnostic::SourcePosition position(const frontend::SourceLocation& source)  {
  const auto clamp = [](const std::size_t value) {
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(value, std::numeric_limits<std::uint32_t>::max()));
  };
  return {clamp(source.line), clamp(source.column),
          static_cast<std::uint64_t>(source.offset)};
}

diagnostic::SourceSpan span(const frontend::SourceSpan& source)  {
  return {source.source_name, position(source.begin), position(source.end)};
}

void import_diagnostic(
    diagnostic::Engine& output,
    const frontend::Diagnostic& input)  {
  diagnostic::Diagnostic converted;
  switch (input.severity) {
    case frontend::DiagnosticSeverity::Note:
      converted.severity = diagnostic::Severity::note;
      break;
    case frontend::DiagnosticSeverity::Warning:
      converted.severity = diagnostic::Severity::warning;
      break;
    case frontend::DiagnosticSeverity::Error:
      converted.severity = diagnostic::Severity::error;
      break;
  }
  converted.code = input.code;
  converted.message = input.message;
  converted.span = span(input.span);
  converted.notes.reserve(input.expansion_stack.size());
  for (const auto& expansion : input.expansion_stack) {
    converted.notes.push_back({expansion, {}});
  }
  output.report(std::move(converted));
}

frontend::Language frontend_language(const project::Language language)  {
  switch (language) {
    case project::Language::vhdl:
      return frontend::Language::Vhdl2008;
    case project::Language::verilog:
      return frontend::Language::Verilog2005;
    case project::Language::system_verilog:
      return frontend::Language::SystemVerilog2017;
    case project::Language::systemc:
      break;
  }
  return frontend::Language::SystemVerilog2017;
}

bool same_source_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right)  {
  if (left.lexically_normal() == right.lexically_normal()) {
    return true;
  }
  std::error_code left_error;
  std::error_code right_error;
  const auto canonical_left =
      std::filesystem::weakly_canonical(left, left_error);
  const auto canonical_right =
      std::filesystem::weakly_canonical(right, right_error);
  return !left_error && !right_error
      && canonical_left == canonical_right;
}

std::string compilation_unit_digest(
    const std::vector<frontend::PreprocessedRoot>& roots,
    const std::vector<frontend::PreprocessedDependency>& inputs)  {
  compiler::CacheKeyBuilder key;
  key.add(
      "compilation-unit-snapshot-schema",
      "fsim-hdl-compilation-unit-v1");
  for (const auto& root : roots) {
    key.add(
        "root-path",
        fsim::support::path_to_utf8(root.path.lexically_normal()));
  }
  for (const auto& input : inputs) {
    key.add(
        "input-path",
        fsim::support::path_to_utf8(input.path.lexically_normal()));
    key.add(
        "input-content",
        support::Sha256::hex(
            support::Sha256::digest(input.contents)));
  }
  return key.finish();
}

void assign_class_source_metadata(
    frontend::SystemVerilogClassDeclaration& declaration,
    const std::string_view library,
    const std::string_view compilation_unit_identity) {
  declaration.library = library;
  declaration.compilation_unit_identity = compilation_unit_identity;
  for (auto& method : declaration.methods) {
    method.library = library;
    method.compilation_unit_identity = compilation_unit_identity;
  }
  for (auto& nested : declaration.nested_classes) {
    assign_class_source_metadata(
        nested, library, compilation_unit_identity);
  }
}

ParsedSnapshot parse_group_snapshot(const ParseGroup& group)  {
  if (group.language == frontend::Language::Verilog2005
      || group.language == frontend::Language::SystemVerilog2017) {
    frontend::PreprocessorOptions options;
    options.include_directories = group.include_directories;
    options.defines = group.defines;
    std::vector<std::filesystem::path> paths;
    paths.reserve(group.inputs.size());
    for (const auto& input : group.inputs) {
      paths.push_back(input.path);
    }
    auto preprocessed =
        frontend::preprocess_verilog_compilation_unit(
            paths, group.language, options);
    ParsedSnapshot snapshot;
    const auto unit_digest =
        compilation_unit_digest(
            preprocessed.roots, preprocessed.inputs);
    snapshot.sources.reserve(preprocessed.roots.size());
    for (std::size_t root_index = 0;
         root_index < preprocessed.roots.size(); ++root_index) {
      const auto& root = preprocessed.roots[root_index];
      CheckedSource source;
      source.path =
          root_index < group.inputs.size()
              ? group.inputs[root_index].path
              : root.path;
      source.content_digest = support::Sha256::hex(
          support::Sha256::digest(root.contents));
      source.dependencies.reserve(root.dependencies.size());
      for (const auto& dependency : root.dependencies) {
        source.dependencies.push_back({
            dependency.path,
            support::Sha256::hex(
                support::Sha256::digest(dependency.contents))});
      }
      source.compilation_unit_digest = unit_digest;
      snapshot.sources.push_back(std::move(source));
    }
    snapshot.result = frontend::parse_verilog(
        std::move(preprocessed.lexed),
        group.language == frontend::Language::SystemVerilog2017);
    for (auto& unit : snapshot.result.design.units) {
      const auto unit_source =
          fsim::support::path_from_utf8(physical_source(unit.span));
      auto source_order =
          group.inputs.empty()
              ? std::size_t{}
              : group.inputs.front().source_order;
      for (std::size_t root_index = 0;
           root_index < snapshot.sources.size()
           && root_index < group.inputs.size(); ++root_index) {
        const auto& source = snapshot.sources[root_index];
        if (same_source_path(source.path, unit_source)
            || std::any_of(
                source.dependencies.begin(),
                source.dependencies.end(),
                [&](const CheckedSource::Dependency& dependency) {
                  return same_source_path(
                      dependency.path, unit_source);
                })) {
          unit.library = group.inputs[root_index].library;
          source_order =
              group.inputs[root_index].source_order;
          break;
        }
      }
      unit.compilation_unit_identity = unit_digest;
      for (auto& declaration : unit.systemverilog_classes) {
        assign_class_source_metadata(
            declaration, unit.library, unit_digest);
      }
      snapshot.unit_source_orders.push_back(source_order);
    }
    for (auto& udp : snapshot.result.design.udp_declarations) {
      const auto udp_source =
          fsim::support::path_from_utf8(physical_source(udp.span));
      auto source_order = group.inputs.empty()
          ? std::size_t{}
          : group.inputs.front().source_order;
      for (std::size_t root_index = 0;
           root_index < snapshot.sources.size()
           && root_index < group.inputs.size(); ++root_index) {
        const auto& source = snapshot.sources[root_index];
        if (same_source_path(source.path, udp_source)
            || std::ranges::any_of(
                source.dependencies,
                [&](const CheckedSource::Dependency& dependency) {
                  return same_source_path(
                      dependency.path, udp_source);
                })) {
          udp.library = group.inputs[root_index].library;
          source_order = group.inputs[root_index].source_order;
          break;
        }
      }
      snapshot.udp_source_orders.push_back(source_order);
    }
    const auto source_metadata = [&](const frontend::SourceSpan& span) {
      const auto declaration_source =
          fsim::support::path_from_utf8(physical_source(span));
      std::string library = group.inputs.empty()
          ? std::string{"work"}
          : group.inputs.front().library;
      auto source_order = group.inputs.empty()
          ? std::size_t{}
          : group.inputs.front().source_order;
      for (std::size_t root_index = 0;
           root_index < snapshot.sources.size()
           && root_index < group.inputs.size(); ++root_index) {
        const auto& source = snapshot.sources[root_index];
        if (same_source_path(source.path, declaration_source)
            || std::ranges::any_of(
                source.dependencies,
                [&](const CheckedSource::Dependency& dependency) {
                  return same_source_path(
                      dependency.path, declaration_source);
                })) {
          library = group.inputs[root_index].library;
          source_order = group.inputs[root_index].source_order;
          break;
        }
      }
      return std::pair{std::move(library), source_order};
    };
    for (auto& declaration :
         snapshot.result.design.systemverilog_classes) {
      auto [library, source_order] = source_metadata(declaration.span);
      assign_class_source_metadata(
          declaration, library, unit_digest);
      snapshot.class_source_orders.push_back(source_order);
    }
    for (auto& method :
         snapshot.result.design.systemverilog_class_method_definitions) {
      auto [library, source_order] = source_metadata(method.span);
      method.library = std::move(library);
      method.compilation_unit_identity = unit_digest;
      snapshot.class_method_source_orders.push_back(source_order);
    }
    return snapshot;
  }

  ParsedSnapshot snapshot;
  if (group.inputs.empty()) {
    return snapshot;
  }
  const auto& input = group.inputs.front();
  CheckedSource source;
  source.path = input.path;
  std::ifstream stream(input.path, std::ios::binary);
  if (!stream) {
    snapshot.result.diagnostics.push_back({
        frontend::DiagnosticSeverity::Error,
        "FSIM-FE-IO-001",
        "unable to open source file",
        {fsim::support::path_to_utf8(input.path), {}, {},
         fsim::support::path_to_utf8(input.path), {}},
        {},
    });
    return snapshot;
  }
  std::string text{
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>()};
  if (!stream.good() && !stream.eof()) {
    snapshot.result.diagnostics.push_back({
        frontend::DiagnosticSeverity::Error,
        "FSIM-FE-IO-002",
        "failed while reading source file",
        {fsim::support::path_to_utf8(input.path), {}, {},
         fsim::support::path_to_utf8(input.path), {}},
        {},
    });
    return snapshot;
  }
  source.content_digest = support::Sha256::hex(
      support::Sha256::digest(text));
  compiler::CacheKeyBuilder key;
  key.add(
      "compilation-unit-snapshot-schema",
      "fsim-hdl-compilation-unit-v1");
  key.add(
      "input-path",
      fsim::support::path_to_utf8(input.path.lexically_normal()));
  key.add("input-content", source.content_digest);
  source.compilation_unit_digest = key.finish();
  snapshot.result = frontend::parse(
      frontend::SourceText{
          fsim::support::path_to_utf8(input.path), std::move(text)},
      input.language);
  for (auto& unit : snapshot.result.design.units) {
    unit.library = input.library;
    unit.compilation_unit_identity = source.compilation_unit_digest;
    for (auto& declaration : unit.systemverilog_classes) {
      assign_class_source_metadata(
          declaration, input.library, source.compilation_unit_digest);
    }
    snapshot.unit_source_orders.push_back(input.source_order);
  }
  for (auto& udp : snapshot.result.design.udp_declarations) {
    udp.library = input.library;
    snapshot.udp_source_orders.push_back(input.source_order);
  }
  for (auto& declaration :
       snapshot.result.design.systemverilog_classes) {
    assign_class_source_metadata(
        declaration, input.library, source.compilation_unit_digest);
    snapshot.class_source_orders.push_back(input.source_order);
  }
  for (auto& method :
       snapshot.result.design.systemverilog_class_method_definitions) {
    method.library = input.library;
    method.compilation_unit_identity = source.compilation_unit_digest;
    snapshot.class_method_source_orders.push_back(input.source_order);
  }
  snapshot.sources.push_back(std::move(source));
  return snapshot;
}

std::optional<systemc::PluginCompileRequest> systemc_request(
    const project::Config& config)  {
  systemc::PluginCompileRequest request;
  request.settings = config.systemc;
  request.working_directory = config.base_directory;
  request.cache_directory = config.build.cache_path;
  for (const auto& source_set : config.source_sets) {
    if (source_set.language != project::Language::systemc) {
      continue;
    }
    request.sources.insert(
        request.sources.end(),
        source_set.files.begin(),
        source_set.files.end());
    request.settings.include_directories.insert(
        request.settings.include_directories.end(),
        source_set.include_directories.begin(),
        source_set.include_directories.end());
    request.settings.defines.insert(
        request.settings.defines.end(),
        source_set.defines.begin(),
        source_set.defines.end());
  }
  return request.sources.empty()
      ? std::nullopt
      : std::optional{std::move(request)};
}

std::vector<SystemCLibraryCompileRequest> systemc_requests(
    const project::Config& config) {
  std::map<std::string, systemc::PluginCompileRequest> grouped;
  for (const auto& source_set : config.source_sets) {
    if (source_set.language != project::Language::systemc) {
      continue;
    }
    auto& request = grouped[source_set.library];
    request.logical_library = source_set.library;
    request.settings = config.systemc;
    request.working_directory = config.base_directory;
    request.cache_directory = config.build.cache_path;
    request.sources.insert(
        request.sources.end(),
        source_set.files.begin(),
        source_set.files.end());
    request.settings.include_directories.insert(
        request.settings.include_directories.end(),
        source_set.include_directories.begin(),
        source_set.include_directories.end());
    request.settings.defines.insert(
        request.settings.defines.end(),
        source_set.defines.begin(),
        source_set.defines.end());
  }
  std::vector<SystemCLibraryCompileRequest> result;
  result.reserve(grouped.size());
  for (auto& [library, request] : grouped) {
    result.push_back({std::move(library), std::move(request)});
  }
  return result;
}

std::shared_ptr<systemc::HierarchyRegistry> load_systemc_plugin(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics)  {
  static std::mutex registry_mutex;
  static std::map<
      std::filesystem::path,
      std::weak_ptr<systemc::HierarchyRegistry>>
      registries;
  const auto normalized = path.lexically_normal();
  std::lock_guard lock(registry_mutex);
  if (const auto found = registries.find(normalized);
      found != registries.end()) {
    if (auto registry = found->second.lock()) {
      return registry;
    }
    registries.erase(found);
  }
  std::string error;
  auto registry = systemc::HierarchyRegistry::load(normalized, error);
  if (!registry) {
    diagnostics.error(
        "FSIM-SC-C009",
        "compiled SystemC plug-in failed ABI validation: " + error);
    return {};
  }
  if (registry->factory_count() == 0) {
    diagnostics.error(
        "FSIM-SC-A001",
        "compiled SystemC plug-in did not register a module factory");
    return {};
  }
  auto shared = std::shared_ptr<systemc::HierarchyRegistry>(
      std::move(registry));
  registries.emplace(normalized, shared);
  return shared;
}

std::string unit_key(const frontend::DesignUnit& unit)  {
  switch (unit.kind) {
    case frontend::UnitKind::VhdlEntity:
      return "vhdl:" + unit.library + ":entity:" + unit.name;
    case frontend::UnitKind::VhdlArchitecture:
      return "vhdl:" + unit.library + ":architecture:"
          + unit.primary_name + ':' + unit.name;
    case frontend::UnitKind::VhdlConfiguration:
      return "vhdl:" + unit.library + ":configuration:"
          + unit.name;
    case frontend::UnitKind::VhdlPackage:
      return "vhdl:" + unit.library
          + (unit.primary_name.empty()
                 ? ":package:"
                 : ":package-body:")
          + unit.name;
    case frontend::UnitKind::VhdlContext:
      return "vhdl:" + unit.library + ":context:" + unit.name;
    case frontend::UnitKind::SystemVerilogPackage:
      return "systemverilog:" + unit.library + ":package:"
          + unit.name;
    case frontend::UnitKind::SystemVerilogInterface:
      return "systemverilog:" + unit.library + ":interface:"
          + unit.name;
    case frontend::UnitKind::VerilogModule:
      return "verilog:" + unit.library + ":module:" + unit.name;
    case frontend::UnitKind::SystemVerilogProgram:
      return "systemverilog:" + unit.library + ":program:" + unit.name;
  }
  return {};
}

std::vector<project::ProjectSection::TopLevel> selected_tops(
    const project::Config& config,
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics)  {
  const auto default_alias = [](const std::string_view target) {
    auto spelling = target;
    if (const auto colon = spelling.find(':');
        colon != std::string_view::npos) {
      spelling.remove_prefix(colon + 1);
    }
    if (const auto dot = spelling.rfind('.');
        dot != std::string_view::npos) {
      spelling.remove_prefix(dot + 1);
    }
    if (const auto architecture = spelling.find('(');
        architecture != std::string_view::npos) {
      spelling = spelling.substr(0, architecture);
    }
    return std::string{spelling};
  };
  std::vector<project::ProjectSection::TopLevel> selected;
  if (!config.project.tops.empty()
      && (config.project.top.empty()
          || config.project.tops.size() != 1
          || config.project.tops.front().target == config.project.top)) {
    selected = config.project.tops;
  } else if (!config.project.top.empty()) {
    selected.push_back(
        {config.project.top, default_alias(config.project.top)});
  }
  if (selected.empty()) {
    std::vector<std::string> candidates;
    for (const auto& unit : parsed.units) {
      if (unit.kind == frontend::UnitKind::VerilogModule) {
        candidates.push_back(
            "sv:" + unit.library + "." + unit.name);
      } else if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
        candidates.push_back(
            "vhdl:" + unit.library + "." + unit.primary_name
            + "(" + unit.name + ")");
      }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    if (candidates.size() != 1) {
      diagnostics.error(
          "FSIM-ELAB-0001",
          candidates.empty()
              ? "the project has no executable HDL design unit"
              : "the project has multiple possible tops; set project.top, "
                "[[project.top]], or --top");
      return {};
    }
    selected.push_back(
        {candidates.front(), default_alias(candidates.front())});
  }
  std::unordered_set<std::string> aliases;
  for (auto& top : selected) {
    if (top.alias.empty() && selected.size() == 1) {
      top.alias = default_alias(top.target);
    }
    if (top.target.empty() || top.alias.empty()) {
      diagnostics.error(
          "FSIM-ELAB-0002",
          "every selected design top requires a non-empty target and alias");
      continue;
    }
    const bool valid_alias =
        (std::isalpha(static_cast<unsigned char>(top.alias.front())) != 0
         || top.alias.front() == '_')
        && std::ranges::all_of(
            top.alias,
            [](const unsigned char character) {
              return std::isalnum(character) != 0 || character == '_';
            });
    if (!valid_alias) {
      diagnostics.error(
          "FSIM-ELAB-0002",
          "top alias '" + top.alias
              + "' is not a portable hierarchy identifier");
    } else if (!aliases.insert(top.alias).second) {
      diagnostics.error(
          "FSIM-ELAB-0002",
          "duplicate top alias '" + top.alias + "'");
    }
  }
  return diagnostics.has_error()
      ? std::vector<project::ProjectSection::TopLevel>{}
      : selected;
}

std::optional<BindingTarget> parse_binding_target(std::string_view target)  {
  const auto colon = target.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 == target.size()) {
    return std::nullopt;
  }
  BindingTarget result;
  result.language = std::string(target.substr(0, colon));
  auto remainder = target.substr(colon + 1);
  if (const auto dot = remainder.rfind('.'); dot != std::string_view::npos) {
    result.qualifier = std::string(remainder.substr(0, dot));
    remainder.remove_prefix(dot + 1);
  }
  if (const auto architecture = remainder.find('(');
      architecture != std::string_view::npos) {
    remainder = remainder.substr(0, architecture);
  }
  if (remainder.empty()) {
    return std::nullopt;
  }
  result.unit = std::string(remainder);
  return result;
}

frontend::PortDirection systemc_direction(
    const fsim_sc_port_direction_v1 direction)  {
  switch (direction) {
    case FSIM_SC_INPUT:
      return frontend::PortDirection::Input;
    case FSIM_SC_OUTPUT:
      return frontend::PortDirection::Output;
    case FSIM_SC_INOUT:
      return frontend::PortDirection::Inout;
  }
  return frontend::PortDirection::Unknown;
}

frontend::Type systemc_type(
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width)  {
  frontend::Type result;
  switch (encoding) {
    case FSIM_SC_BIT2:
      result.domain = frontend::ValueDomain::Bit2;
      result.spelling = "systemc.bit";
      break;
    case FSIM_SC_LOGIC4:
      result.domain = frontend::ValueDomain::Logic4;
      result.spelling = "systemc.logic";
      break;
    case FSIM_SC_SIGNED:
      result.domain = frontend::ValueDomain::Bit2;
      result.spelling = "systemc.signed";
      result.is_signed = true;
      break;
    case FSIM_SC_UNSIGNED:
      result.domain = frontend::ValueDomain::Bit2;
      result.spelling = "systemc.unsigned";
      break;
  }
  if (width > 1) {
    result.packed_range = frontend::PackedRange{
        static_cast<std::int64_t>(width - 1), 0, true};
  }
  return result;
}

PackedLogic4 systemc_value(
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    const std::span<const std::uint8_t> storage)  {
  const auto bytes =
      (static_cast<std::size_t>(width) + 7U) / 8U;
  const bool four_state = encoding != FSIM_SC_BIT2;
  if (width == 0
      || storage.size() != bytes * (four_state ? 2U : 1U)) {
    throw std::invalid_argument{
        "invalid packed SystemC initial value"};
  }
  PackedLogic4 result(width);
  for (std::size_t bit = 0; bit < width; ++bit) {
    const auto byte = bit / 8U;
    const auto mask =
        static_cast<std::uint8_t>(1U << (bit % 8U));
    const bool aval = (storage[byte] & mask) != 0;
    const bool bval = four_state
        && (storage[bytes + byte] & mask) != 0;
    result.set(
        bit,
        !bval
            ? (aval ? runtime::Logic4::one
                    : runtime::Logic4::zero)
            : (aval ? runtime::Logic4::x
                    : runtime::Logic4::z));
  }
  return result;
}

elaboration::SystemCInstanceDescription systemc_description(
    const std::string_view path,
    const std::string_view target,
    const systemc::ModuleDescription& module)  {
  elaboration::SystemCInstanceDescription result;
  result.path = path;
  result.target = target;
  result.handle = module.handle;
  result.parent = module.parent;
  result.construction_values =
      module.construction_values;
  result.ports.reserve(module.ports.size());
  for (const auto& port : module.ports) {
    result.ports.push_back({
        port.handle,
        port.name,
        systemc_type(port.encoding, port.width),
        systemc_direction(port.direction),
        port.bound_object,
    });
  }
  result.foreign_children.reserve(module.foreign_children.size());
  for (const auto& child : module.foreign_children) {
    elaboration::ForeignChild converted;
    converted.handle = child.handle;
    converted.name = child.name;
    converted.construction_actuals =
        child.construction_actuals;
    converted.module_facade = child.module_facade;
    converted.implementation = child.implementation;
    converted.ports.reserve(child.ports.size());
    for (const auto& port : child.ports) {
      converted.ports.push_back({
          port.name,
          systemc_type(port.encoding, port.width),
          systemc_direction(port.direction),
          port.object,
          port.handle,
      });
    }
    result.foreign_children.push_back(std::move(converted));
  }
  result.processes.reserve(module.processes.size());
  for (const auto& process : module.processes) {
    elaboration::ExternalProcess converted;
    converted.handle = process.handle;
    converted.name = process.name;
    converted.kind = process.kind;
    converted.entry = process.entry;
    converted.user = process.user;
    converted.initialize = process.initialize;
    converted.sensitivity.reserve(process.sensitivity.size());
    for (const auto& sensitivity : process.sensitivity) {
      converted.sensitivity.push_back(
          {sensitivity.object,
           static_cast<std::uint32_t>(sensitivity.edge)});
    }
    result.processes.push_back(std::move(converted));
  }
  result.events.reserve(module.events.size());
  for (const auto& event : module.events) {
    result.events.push_back({event.handle, event.name});
  }
  result.primitive_channels.reserve(
      module.primitive_channels.size());
  for (const auto& channel : module.primitive_channels) {
    result.primitive_channels.push_back(
        {channel.handle, channel.name, channel.kind});
  }
  result.internal_signals.reserve(module.internal_signals.size());
  for (const auto& signal : module.internal_signals) {
    result.internal_signals.push_back({
        signal.handle,
        signal.name,
        systemc_type(signal.encoding, signal.width),
        systemc_value(
            signal.encoding,
            signal.width,
            signal.initial_value),
    });
  }
  result.exports.reserve(module.exports.size());
  for (const auto& export_object : module.exports) {
    result.exports.push_back({
        export_object.handle,
        export_object.name,
        systemc_type(
            export_object.encoding, export_object.width),
        export_object.bound_object,
        export_object.writable,
    });
  }
  result.metadata_objects.reserve(module.metadata_objects.size());
  for (const auto& object : module.metadata_objects) {
    result.metadata_objects.push_back({
        object.handle, object.name, object.category, object.kind});
  }
  result.native_children.reserve(module.native_children.size());
  for (const auto& child : module.native_children) {
    const auto child_path =
        std::string{path} + "." + child.instance;
    result.native_children.push_back(
        systemc_description(child_path, target, child));
  }
  return result;
}

std::optional<std::vector<elaboration::SystemCInstanceDescription>>
construct_systemc_instances(
    const std::span<const project::ProjectSection::TopLevel> tops,
    const std::span<const SystemCLibraryRegistry> registries,
    diagnostic::Engine& diagnostics)  {
  struct Request {
    std::string path;
    std::string target;
    BindingTarget parsed;
  };
  std::vector<Request> requests;
  for (const auto& top : tops) {
    if (const auto parsed = parse_binding_target(top.target);
        parsed && parsed->language == "systemc") {
      requests.push_back({top.alias, top.target, *parsed});
    }
  }
  // HDL-bound SystemC instances are constructed on demand by the common
  // hierarchy walk after their source-language actuals are canonicalized.
  // Only a selected SystemC top has no HDL parent and is eager here.
  std::vector<Request> unique;
  for (auto& request : requests) {
    if (request.parsed.qualifier.empty()) {
      diagnostics.error(
          "FSIM-ELAB-BIND-0001",
          "SystemC target '" + request.target
              + "' must use systemc:plugin.factory spelling");
      continue;
    }
    const auto duplicate = std::find_if(
        unique.begin(), unique.end(),
        [&](const Request& candidate) {
          return candidate.path == request.path;
        });
    if (duplicate == unique.end()) {
      unique.push_back(std::move(request));
    } else if (duplicate->target != request.target) {
      diagnostics.error(
          "FSIM-SC-A005",
          "SystemC instance path '" + request.path
              + "' has conflicting factory targets");
    }
  }
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  if (!unique.empty() && registries.empty()) {
    diagnostics.error(
        "FSIM-SC-A002",
        "SystemC hierarchy requires a compiled plug-in");
    return std::nullopt;
  }

  std::vector<elaboration::SystemCInstanceDescription> result;
  std::map<std::string, std::pair<
      fsim_sc_handle_v1, systemc::HierarchyRegistry*>> handles;
  result.reserve(unique.size());
  for (const auto& request : unique) {
    const auto registry_entry = std::ranges::find(
        registries,
        request.parsed.qualifier,
        &SystemCLibraryRegistry::library);
    if (registry_entry == registries.end()) {
      diagnostics.error(
          "FSIM-SC-A002",
          "logical library '" + request.parsed.qualifier
              + "' has no compiled SystemC plug-in");
      continue;
    }
    auto* registry = registry_entry->registry.get();
    if (!registry->has_factory(request.parsed.unit)) {
      diagnostics.error(
          "FSIM-SC-A002",
          "SystemC factory '" + request.parsed.unit
              + "' was not registered by the compiled plug-in");
      continue;
    }
    if (!registry->has_elaboration_factory(request.parsed.unit)) {
      diagnostics.error(
          "FSIM-SC-A003",
          "SystemC factory '" + request.parsed.unit
              + "' uses the legacy untyped construction ABI");
      continue;
    }
    fsim_sc_handle_v1 parent = 0;
    if (const auto separator = request.path.rfind('.');
        separator != std::string::npos) {
      const auto found = handles.find(request.path.substr(0, separator));
      if (found != handles.end() && found->second.second == registry) {
        parent = found->second.first;
      }
    }
    std::string error;
    auto module = registry->instantiate(
        request.parsed.unit, request.path, parent, error);
    if (!module) {
      diagnostics.error(
          "FSIM-SC-A004",
          "cannot construct SystemC instance '" + request.path
              + "': " + error);
      continue;
    }
    const auto record_handles =
        [&](const auto& self,
            const std::string& path,
            const systemc::ModuleDescription& description) -> void {
          handles.emplace(
              path, std::pair{description.handle, registry});
          for (const auto& child : description.native_children) {
            self(
                self,
                path + "." + child.instance,
                child);
          }
        };
    record_handles(record_handles, request.path, *module);
    result.push_back(systemc_description(
        request.path, request.target, *module));
  }
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  return result;
}

void validate_bindings(
    const project::Config& config,
    const frontend::ParsedDesign& parsed,
    const std::span<const SystemCLibraryRegistry> systemc_registries,
    diagnostic::Engine& diagnostics)  {
  for (const auto& binding : config.bindings) {
    if (!binding.target.has_value()) {
      continue;
    }
    const auto target = parse_binding_target(*binding.target);
    if (!target) {
      diagnostics.error(
          "FSIM-ELAB-BIND-0001",
          "binding target '" + *binding.target
              + "' must be language-qualified");
      continue;
    }
    bool found = false;
    if (target->language == "sv" || target->language == "verilog") {
      found = parsed.find(frontend::UnitKind::VerilogModule, target->unit)
              != nullptr;
    } else if (target->language == "vhdl") {
      found = std::any_of(
          parsed.units.begin(), parsed.units.end(),
          [&](const frontend::DesignUnit& unit) {
            return unit.kind == frontend::UnitKind::VhdlArchitecture
                && unit.primary_name == target->unit;
          });
    } else if (target->language == "systemc") {
      if (target->qualifier.empty()) {
        diagnostics.error(
            "FSIM-ELAB-BIND-0001",
            "SystemC target '" + *binding.target
                + "' must use systemc:plugin.factory spelling");
        continue;
      }
      const auto registry = std::ranges::find(
          systemc_registries,
          target->qualifier,
          &SystemCLibraryRegistry::library);
      found = registry != systemc_registries.end()
          && registry->registry->has_factory(target->unit);
      if (found
          && !registry->registry->has_elaboration_factory(target->unit)) {
        diagnostics.error(
            "FSIM-SC-A003",
            "SystemC factory '" + target->unit
                + "' uses the legacy untyped construction ABI");
      }
    } else {
      diagnostics.error(
          "FSIM-ELAB-BIND-0002",
          "unsupported binding language '" + target->language + "'");
      continue;
    }
    if (!found) {
      diagnostics.error(
          "FSIM-ELAB-BIND-0003",
          "binding target unit '" + *binding.target + "' was not found");
    }
    if (binding.resolver
        && *binding.resolver != "std_logic"
        && *binding.resolver != "sv_wire") {
      diagnostics.error(
          "FSIM-ELAB-BIND-0004",
          "binding resolver must be 'std_logic' or 'sv_wire'");
    }
  }
}

std::string target_name()  {
#if defined(_WIN32)
  // The cache identity includes the ABI environment, not only the object
  // format. Windows JIT code and native plug-ins use the MSVC x64 ABI even
  // when clang-cl is the C++ frontend.
  return "x86_64-pc-windows-msvc";
#elif defined(__linux__)
  return "x86_64-unknown-linux-gnu";
#else
  return "x86_64-unknown";
#endif
}

} // namespace fsim::app::application_detail
