// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app {
using namespace application_detail;

std::optional<CheckedProject> check_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics) {
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
      && !diagnostics.has_error()) {
    diagnostics.error("FSIM-FE-0001", "the project contains no HDL source files");
  }
  if (const auto request = systemc_request(config)) {
    (void)systemc::plan_plugin_compile(*request, diagnostics);
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
            {source.path.generic_string(), {}, {}});
        continue;
      }
      const std::string contents{
          std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
      if (!input.good() && !input.eof()) {
        diagnostics.error(
            "FSIM-FE-IO-002",
            "failed while reading SystemC source file",
            {source.path.generic_string(), {}, {}});
        continue;
      }
      source.content_digest = support::Sha256::hex(
          support::Sha256::digest(contents));
      compiler::CacheKeyBuilder key;
      key.add(
          "compilation-unit-snapshot-schema",
          "fsim-systemc-compilation-unit-v1");
      key.add("input-path", source.path.generic_string());
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
  std::vector<OrderedUnit> ordered_units;
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
            {path.generic_string(), {}, {}});
      } catch (...) {
        diagnostics.error(
            "FSIM-FE-0003",
            "source analysis failed with an unknown exception",
            {path.generic_string(), {}, {}});
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
          {path.generic_string(), {}, {}});
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
  inject_vhdl_standard_libraries(checked, diagnostics);
  validate_vhdl_analysis_order(checked.parsed.units, diagnostics);
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
      checked.parsed, checked.semantics);
  return checked;
}


} // namespace fsim::app
