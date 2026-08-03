// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/support/path.hpp"

namespace fsim::app::application_detail {

std::atomic_bool interrupt_requested{};

ApplicationSystemCFactoryProvider::ApplicationSystemCFactoryProvider(
      systemc::HierarchyRegistry& registry,
      const std::span<
          const elaboration::SystemCInstanceDescription> eager_instances,
      std::vector<std::uint64_t>& lifecycle_roots)
      : registry_(registry), lifecycle_roots_(lifecycle_roots)  {
    for (const auto& instance : eager_instances) {
      record_handles(instance.path, instance);
    }
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
    const auto parameters =
        registry_.factory_parameters(parsed->unit);
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
    fsim_sc_handle_v1 parent = 0;
    if (const auto separator = path.rfind('.');
        separator != std::string_view::npos) {
      const auto found =
          handles_.find(std::string{path.substr(0, separator)});
      if (found != handles_.end()) {
        parent = found->second;
      }
    }
    auto module = registry_.instantiate(
        parsed->unit,
        path,
        parent,
        construction_values,
        error);
    if (!module) {
      return std::nullopt;
    }
    record_handles(std::string{path}, *module);
    lifecycle_roots_.push_back(module->handle);
    return systemc_description(path, target, *module);
  }

void ApplicationSystemCFactoryProvider::record_handles(
      const std::string& path,
      const elaboration::SystemCInstanceDescription& description)  {
    handles_.emplace(path, description.handle);
    for (const auto& child : description.native_children) {
      record_handles(child.path, child);
    }
  }

void ApplicationSystemCFactoryProvider::record_handles(
    const std::string& path,
    const systemc::ModuleDescription& description)  {
    handles_.emplace(path, description.handle);
    for (const auto& child : description.native_children) {
      record_handles(path + "." + child.instance, child);
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
          std::filesystem::path{physical_source(unit.span)};
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
      snapshot.unit_source_orders.push_back(source_order);
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
    snapshot.unit_source_orders.push_back(input.source_order);
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
  }
  return {};
}

std::string selected_top(
    const project::Config& config,
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics)  {
  if (!config.project.top.empty()) {
    return config.project.top;
  }
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
            : "the project has multiple possible tops; set project.top or --top");
    return {};
  }
  return candidates.front();
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
    converted.ports.reserve(child.ports.size());
    for (const auto& port : child.ports) {
      converted.ports.push_back({
          port.name,
          systemc_type(port.encoding, port.width),
          systemc_direction(port.direction),
          port.object,
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
          {sensitivity.object, sensitivity.edge});
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
    const std::string_view top,
    systemc::HierarchyRegistry* registry,
    diagnostic::Engine& diagnostics)  {
  struct Request {
    std::string path;
    std::string target;
    BindingTarget parsed;
  };
  std::vector<Request> requests;
  if (const auto parsed = parse_binding_target(top);
      parsed && parsed->language == "systemc") {
    requests.push_back({parsed->unit, std::string{top}, *parsed});
  }
  // HDL-bound SystemC instances are constructed on demand by the common
  // hierarchy walk after their source-language actuals are canonicalized.
  // Only a selected SystemC top has no HDL parent and is eager here.
  std::sort(
      requests.begin(), requests.end(),
      [](const Request& left, const Request& right) {
        const auto left_depth =
            std::count(left.path.begin(), left.path.end(), '.');
        const auto right_depth =
            std::count(right.path.begin(), right.path.end(), '.');
        return left_depth != right_depth
            ? left_depth < right_depth
            : left.path < right.path;
      });

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
  if (!unique.empty() && registry == nullptr) {
    diagnostics.error(
        "FSIM-SC-A002",
        "SystemC hierarchy requires a compiled plug-in");
    return std::nullopt;
  }

  std::vector<elaboration::SystemCInstanceDescription> result;
  std::map<std::string, fsim_sc_handle_v1> handles;
  result.reserve(unique.size());
  for (const auto& request : unique) {
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
      if (found != handles.end()) {
        parent = found->second;
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
          handles.emplace(path, description.handle);
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
    const systemc::HierarchyRegistry* systemc_hierarchy,
    diagnostic::Engine& diagnostics)  {
  for (const auto& binding : config.bindings) {
    const auto target = parse_binding_target(binding.target);
    if (!target) {
      diagnostics.error(
          "FSIM-ELAB-BIND-0001",
          "binding target '" + binding.target
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
            "SystemC target '" + binding.target
                + "' must use systemc:plugin.factory spelling");
        continue;
      }
      found = systemc_hierarchy != nullptr
          && systemc_hierarchy->has_factory(target->unit);
      if (found
          && !systemc_hierarchy->has_elaboration_factory(target->unit)) {
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
          "binding target unit '" + binding.target + "' was not found");
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
