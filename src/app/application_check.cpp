// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/frontend/coverage_resolution.hpp"
#include "fsim/frontend/class_resolution.hpp"
#include "fsim/frontend/class_inheritance.hpp"
#include "fsim/support/path.hpp"

namespace fsim::app {
using namespace application_detail;

namespace {

std::optional<project::SystemVerilogUvmRelease> selected_uvm_release(
    const project::Config& config,
    diagnostic::Engine& diagnostics) {
  auto selected = project::SystemVerilogUvmRelease::none;
  for (const auto& source_set : config.source_sets) {
    if (source_set.uvm_release == project::SystemVerilogUvmRelease::none) {
      continue;
    }
    if (source_set.language != project::Language::system_verilog) {
      diagnostics.error(
          "FSIM-UVM-VERSION-001",
          "a governed UVM release may be selected only for SystemVerilog");
      return std::nullopt;
    }
    if (selected != project::SystemVerilogUvmRelease::none
        && selected != source_set.uvm_release) {
      diagnostics.error(
          "FSIM-UVM-VERSION-001",
          "mixed governed UVM 1.2 and UVM 2020.3.1 source sets are not "
          "transactionally compatible");
      return std::nullopt;
    }
    selected = source_set.uvm_release;
  }
  return selected;
}

std::string uvm_source_identity(
    const project::SystemVerilogUvmRelease release,
    const std::span<const CheckedSource> sources) {
  if (release == project::SystemVerilogUvmRelease::none) {
    return {};
  }
  compiler::CacheKeyBuilder key;
  key.add("uvm-provenance-schema", "fsim-uvm-source-v1");
  key.add("uvm-release", project::to_string(release));
  for (const auto& source : sources) {
    key.add("source-content", source.content_digest);
    key.add("source-compilation-unit", source.compilation_unit_digest);
    for (const auto& dependency : source.dependencies) {
      key.add("dependency-content", dependency.content_digest);
    }
  }
  return key.finish();
}

bool validate_uvm_api_release(
    const project::SystemVerilogUvmRelease release,
    const std::span<const frontend::SystemVerilogClassSpecialization> classes,
    diagnostic::Engine& diagnostics) {
  if (release == project::SystemVerilogUvmRelease::none) {
    return true;
  }
  const auto has_class = [&](const std::string_view suffix) {
    return std::ranges::any_of(classes, [&](const auto& specialization) {
      return specialization.declaration_identity.ends_with(suffix);
    });
  };
  const bool has_uvm_object = has_class("::uvm_object");
  const bool has_ieee_policy = has_class("::uvm_policy");
  const auto compatibility =
      project::systemverilog_uvm_compatibility(release);
  const bool matches = has_uvm_object
      && has_ieee_policy == compatibility.ieee_policy_classes;
  if (!matches) {
    diagnostics.error(
        "FSIM-UVM-VERSION-002",
        "selected governed UVM release does not match the parsed uvm_pkg API "
        "surface");
  }
  return matches;
}

}  // namespace

std::optional<CheckedProject> check_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics) {
  const auto uvm_release = selected_uvm_release(config, diagnostics);
  if (!uvm_release) {
    return std::nullopt;
  }
  std::vector<ParseGroup> groups;
  std::map<std::string, std::size_t> combined_groups;
  std::size_t systemc_source_count = 0;
  std::size_t hdl_source_count = 0;
  for (const auto& source_set : config.source_sets) {
    if (source_set.language == project::Language::vhdl
        && source_set.compilation_unit != "file") {
      diagnostics.warning(
          "FSIM-FE-CU-0001",
          "VHDL files are independent analysis units; compilation_unit "
          "grouping applies to Verilog/SystemVerilog");
    }
    if (source_set.language == project::Language::vhdl
        && (!source_set.include_directories.empty()
            || !source_set.defines.empty())) {
      diagnostics.error(
          "FSIM-FE-PP-0001",
          "include directories and macro definitions are only valid for "
          "Verilog/SystemVerilog and SystemC source sets");
    }
    if (source_set.language == project::Language::systemc) {
      systemc_source_count += source_set.files.size();
      continue;
    }
    const auto language = frontend_language(source_set.language);
    const auto append_files = [&](ParseGroup& group) {
      group.include_directories.insert(
          group.include_directories.end(),
          source_set.include_directories.begin(),
          source_set.include_directories.end());
      group.defines.insert(
          group.defines.end(),
          source_set.defines.begin(),
          source_set.defines.end());
      for (const auto& file : source_set.files) {
        group.inputs.push_back(
            {file, language, source_set.library, hdl_source_count++});
      }
    };
    if (source_set.compilation_unit == "file"
        || source_set.language == project::Language::vhdl) {
      for (const auto& file : source_set.files) {
        ParseGroup group;
        group.language = language;
        group.standard = source_set.standard;
        group.include_directories = source_set.include_directories;
        group.defines = source_set.defines;
        group.inputs.push_back(
            {file, language, source_set.library, hdl_source_count++});
        groups.push_back(std::move(group));
      }
    } else if (source_set.compilation_unit == "source-set") {
      ParseGroup group;
      group.language = language;
      group.standard = source_set.standard;
      append_files(group);
      groups.push_back(std::move(group));
    } else {
      const auto key =
          std::to_string(static_cast<unsigned>(language))
          + '\n' + source_set.standard;
      auto found = combined_groups.find(key);
      if (found == combined_groups.end()) {
        const auto index = groups.size();
        ParseGroup group;
        group.language = language;
        group.standard = source_set.standard;
        groups.push_back(std::move(group));
        found = combined_groups.emplace(key, index).first;
      }
      append_files(groups[found->second]);
    }
  }
  if (hdl_source_count == 0 && systemc_source_count == 0
      && config.library_mappings.empty()
      && !diagnostics.has_error()) {
    diagnostics.error("FSIM-FE-0001", "the project contains no HDL source files");
  }
  for (const auto& entry : systemc_requests(config)) {
    (void)systemc::plan_plugin_compile(entry.request, diagnostics);
  }
  if (diagnostics.has_error()) {
    return std::nullopt;
  }

  std::vector<std::optional<ParsedSnapshot>> parsed_inputs(
      groups.size());
  std::vector<std::exception_ptr> parse_failures(groups.size());
  std::atomic_size_t next_input{0};
  auto job_count = config.build.jobs == 0
      ? static_cast<std::size_t>(std::thread::hardware_concurrency())
      : static_cast<std::size_t>(config.build.jobs);
  job_count = std::max<std::size_t>(1, job_count);
  job_count = std::min(job_count, groups.size());
  std::vector<std::future<void>> workers;
  workers.reserve(job_count);
  for (std::size_t worker = 0; worker < job_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      while (true) {
        const auto index =
            next_input.fetch_add(1, std::memory_order_relaxed);
        if (index >= groups.size()) {
          return;
        }
        try {
          parsed_inputs[index] =
              parse_group_snapshot(groups[index]);
        } catch (...) {
          parse_failures[index] = std::current_exception();
        }
      }
    }));
  }
  for (auto& worker : workers) {
    worker.get();
  }

  CheckedProject checked;
  checked.source_count = hdl_source_count + systemc_source_count;
  if (const auto request = systemc_request(config)) {
    checked.systemc_sources.reserve(request->sources.size());
    for (const auto& manifest_path : request->sources) {
      CheckedSource source;
      source.path = manifest_path.is_absolute()
          ? manifest_path
          : config.base_directory / manifest_path;
      source.path = source.path.lexically_normal();
      std::ifstream input{source.path, std::ios::binary};
      if (!input) {
        diagnostics.error(
            "FSIM-FE-IO-001",
            "unable to open SystemC source file",
            {fsim::support::path_to_utf8(source.path), {}, {}});
        continue;
      }
      const std::string contents{
          std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
      if (!input.good() && !input.eof()) {
        diagnostics.error(
            "FSIM-FE-IO-002",
            "failed while reading SystemC source file",
            {fsim::support::path_to_utf8(source.path), {}, {}});
        continue;
      }
      source.content_digest = support::Sha256::hex(
          support::Sha256::digest(contents));
      compiler::CacheKeyBuilder key;
      key.add(
          "compilation-unit-snapshot-schema",
          "fsim-systemc-compilation-unit-v1");
      key.add("input-path", fsim::support::path_to_utf8(source.path));
      key.add("input-content", source.content_digest);
      source.compilation_unit_digest = key.finish();
      checked.systemc_sources.push_back(std::move(source));
    }
  }
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  std::vector<std::pair<std::size_t, CheckedSource>> checked_sources;
  struct OrderedUnit {
    std::size_t source_order{};
    std::size_t unit_order{};
    frontend::DesignUnit unit;
  };
  struct OrderedUdp {
    std::size_t source_order{};
    std::size_t declaration_order{};
    frontend::VerilogUdpDeclaration declaration;
  };
  struct OrderedClass {
    std::size_t source_order{};
    std::size_t declaration_order{};
    frontend::SystemVerilogClassDeclaration declaration;
  };
  struct OrderedClassMethod {
    std::size_t source_order{};
    std::size_t declaration_order{};
    frontend::SystemVerilogClassMethod method;
  };
  std::vector<OrderedUnit> ordered_units;
  std::vector<OrderedUdp> ordered_udps;
  std::vector<OrderedClass> ordered_classes;
  std::vector<OrderedClassMethod> ordered_class_methods;
  for (std::size_t input_index = 0;
       input_index < parsed_inputs.size(); ++input_index) {
    if (parse_failures[input_index]) {
      const auto path =
          groups[input_index].inputs.empty()
              ? std::filesystem::path{}
              : groups[input_index].inputs.front().path;
      try {
        std::rethrow_exception(parse_failures[input_index]);
      } catch (const std::exception& error) {
        diagnostics.error(
            "FSIM-FE-0003",
            "source analysis failed: " + std::string{error.what()},
            {fsim::support::path_to_utf8(path), {}, {}});
      } catch (...) {
        diagnostics.error(
            "FSIM-FE-0003",
            "source analysis failed with an unknown exception",
            {fsim::support::path_to_utf8(path), {}, {}});
      }
      continue;
    }
    if (!parsed_inputs[input_index]) {
      const auto path =
          groups[input_index].inputs.empty()
              ? std::filesystem::path{}
              : groups[input_index].inputs.front().path;
      diagnostics.error(
          "FSIM-FE-0003",
          "source analysis produced no result",
          {fsim::support::path_to_utf8(path), {}, {}});
      continue;
    }
    auto snapshot = std::move(*parsed_inputs[input_index]);
    for (auto& source : snapshot.sources) {
      const auto found = std::find_if(
          groups[input_index].inputs.begin(),
          groups[input_index].inputs.end(),
          [&](const ParseInput& input) {
            return same_source_path(input.path, source.path);
          });
      if (found != groups[input_index].inputs.end()
          && !source.content_digest.empty()) {
        checked_sources.emplace_back(
            found->source_order, std::move(source));
      }
    }
    auto result = std::move(snapshot.result);
    for (const auto& frontend_diagnostic : result.diagnostics) {
      import_diagnostic(diagnostics, frontend_diagnostic);
    }
    for (std::size_t unit_index = 0;
         unit_index < result.design.units.size(); ++unit_index) {
      ordered_units.push_back({
          unit_index < snapshot.unit_source_orders.size()
              ? snapshot.unit_source_orders[unit_index]
              : std::size_t{},
          unit_index,
          std::move(result.design.units[unit_index])});
    }
    for (std::size_t udp_index = 0;
         udp_index < result.design.udp_declarations.size(); ++udp_index) {
      ordered_udps.push_back({
          udp_index < snapshot.udp_source_orders.size()
              ? snapshot.udp_source_orders[udp_index]
              : std::size_t{},
          udp_index,
          std::move(result.design.udp_declarations[udp_index])});
    }
    for (std::size_t class_index = 0;
         class_index < result.design.systemverilog_classes.size();
         ++class_index) {
      ordered_classes.push_back({
          class_index < snapshot.class_source_orders.size()
              ? snapshot.class_source_orders[class_index]
              : std::size_t{},
          class_index,
          std::move(result.design.systemverilog_classes[class_index])});
    }
    for (std::size_t method_index = 0;
         method_index
             < result.design.systemverilog_class_method_definitions.size();
         ++method_index) {
      ordered_class_methods.push_back({
          method_index < snapshot.class_method_source_orders.size()
              ? snapshot.class_method_source_orders[method_index]
              : std::size_t{},
          method_index,
          std::move(
              result.design.systemverilog_class_method_definitions[
                  method_index])});
    }
  }
  std::sort(
      checked_sources.begin(),
      checked_sources.end(),
      [](const auto& left, const auto& right) {
        return left.first < right.first;
      });
  checked.hdl_sources.reserve(checked_sources.size());
  for (auto& [order, source] : checked_sources) {
    (void)order;
    checked.hdl_sources.push_back(std::move(source));
  }
  std::stable_sort(
      ordered_units.begin(),
      ordered_units.end(),
      [](const OrderedUnit& left, const OrderedUnit& right) {
        return std::tie(left.source_order, left.unit_order)
            < std::tie(right.source_order, right.unit_order);
      });
  std::stable_sort(
      ordered_udps.begin(),
      ordered_udps.end(),
      [](const OrderedUdp& left, const OrderedUdp& right) {
        return std::tie(left.source_order, left.declaration_order)
            < std::tie(right.source_order, right.declaration_order);
      });
  std::stable_sort(
      ordered_classes.begin(),
      ordered_classes.end(),
      [](const OrderedClass& left, const OrderedClass& right) {
        return std::tie(left.source_order, left.declaration_order)
            < std::tie(right.source_order, right.declaration_order);
      });
  std::stable_sort(
      ordered_class_methods.begin(),
      ordered_class_methods.end(),
      [](const OrderedClassMethod& left,
         const OrderedClassMethod& right) {
        return std::tie(left.source_order, left.declaration_order)
            < std::tie(right.source_order, right.declaration_order);
      });
  std::set<std::string> known_units;
  checked.parsed.units.reserve(ordered_units.size());
  for (auto& ordered : ordered_units) {
    const auto key = unit_key(ordered.unit);
    if (!known_units.insert(key).second) {
      diagnostics.error(
          "FSIM-FE-0002",
          "duplicate design unit '" + key + "'",
          span(ordered.unit.span));
    } else {
      checked.parsed.units.push_back(std::move(ordered.unit));
    }
  }
  std::set<std::string> known_udps;
  checked.parsed.udp_declarations.reserve(ordered_udps.size());
  for (auto& ordered : ordered_udps) {
    const auto library = ordered.declaration.library.empty()
        ? std::string{"work"}
        : ordered.declaration.library;
    const auto key = "verilog:" + library + ":udp:"
        + ordered.declaration.name;
    if (!known_udps.insert(key).second) {
      diagnostics.error(
          "FSIM-FE-0002",
          "duplicate design unit '" + key + "'",
          span(ordered.declaration.span));
    } else {
      checked.parsed.udp_declarations.push_back(
          std::move(ordered.declaration));
    }
  }
  checked.parsed.systemverilog_classes.reserve(ordered_classes.size());
  for (auto& ordered : ordered_classes) {
    checked.parsed.systemverilog_classes.push_back(
        std::move(ordered.declaration));
  }
  checked.parsed.systemverilog_class_method_definitions.reserve(
      ordered_class_methods.size());
  for (auto& ordered : ordered_class_methods) {
    checked.parsed.systemverilog_class_method_definitions.push_back(
        std::move(ordered.method));
  }
  if (!load_required_mapped_libraries(config, checked, diagnostics)) {
    return std::nullopt;
  }
  if (checked.parsed.units.empty()
      && checked.parsed.udp_declarations.empty()
      && checked.parsed.systemverilog_classes.empty()
      && checked.systemc_sources.empty()
      && checked.mapped_libraries.empty() && !diagnostics.has_error()) {
    diagnostics.error(
        "FSIM-FE-0001",
        "the project contains no local or selected mapped design units");
    return std::nullopt;
  }
  inject_vhdl_standard_libraries(checked, diagnostics);
  validate_vhdl_analysis_order(checked.parsed.units, diagnostics);
  std::vector<frontend::Diagnostic> class_diagnostics;
  (void)frontend::resolve_systemverilog_classes(
      checked.parsed, class_diagnostics);
  for (const auto& diagnostic : class_diagnostics) {
    import_diagnostic(diagnostics, diagnostic);
  }
  class_diagnostics.clear();
  (void)frontend::resolve_systemverilog_covergroups(
      checked.parsed, class_diagnostics);
  for (const auto& diagnostic : class_diagnostics) {
    import_diagnostic(diagnostics, diagnostic);
  }
  std::vector<frontend::Diagnostic> inheritance_diagnostics;
  (void)frontend::validate_systemverilog_class_inheritance(
      checked.parsed, inheritance_diagnostics);
  for (const auto& diagnostic : inheritance_diagnostics) {
    import_diagnostic(diagnostics, diagnostic);
  }
  auto class_specializations =
      frontend::specialize_systemverilog_classes(checked.parsed);
  for (const auto& diagnostic : class_specializations.diagnostics) {
    import_diagnostic(diagnostics, diagnostic);
  }
  checked.systemverilog_class_specializations =
      std::move(class_specializations.specializations);
  if (!validate_uvm_api_release(
          *uvm_release, checked.systemverilog_class_specializations,
          diagnostics)) {
    return std::nullopt;
  }
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  checked.semantics = build_semantic_model(
      checked.parsed,
      checked.hdl_sources,
      checked.systemc_sources,
      checked.standard_sources);
  checked.vhdl_hir = build_vhdl_hir(checked.parsed, checked.semantics);
  checked.systemverilog_hir = build_systemverilog_hir(
      checked.parsed,
      checked.semantics,
      checked.systemverilog_class_specializations);
  if (!checked.semantics.valid()) {
    diagnostics.error(
        "FSIM-SEM-0001",
        "source analysis produced an invalid owning semantic projection");
    return std::nullopt;
  }
  checked.systemverilog_uvm_provenance.release = *uvm_release;
  checked.systemverilog_uvm_provenance.source_identity =
      uvm_source_identity(*uvm_release, checked.hdl_sources);
  return checked;
}


} // namespace fsim::app
