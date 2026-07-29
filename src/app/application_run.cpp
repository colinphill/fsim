// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

std::string make_cache_key(
    const project::Config& config,
    const CheckedProject& checked,
    const std::string_view top,
    const std::string_view resolution,
    const std::string_view systemc_plugin_key,
    diagnostic::Engine& diagnostics)  {
  compiler::CacheKeyBuilder key;
  key.add("fsim-version", version);
  key.add("runtime-abi", std::to_string(runtime_abi_version));
  key.add("target", target_name());
  key.add("top", top);
  key.add("time-resolution", resolution);
  key.add("delay-mode", project::to_string(config.run.delay_mode));
  key.add("optimization", project::to_string(config.build.optimization));
  key.add("llvm", production_llvm_version);
  key.add("standard-library", standard_library_cache_version);
  key.add(
      "verilog-preprocessor",
      frontend::verilog_preprocessor_cache_version);
  key.add("systemc-plugin", systemc_plugin_key);
  std::size_t hdl_source_index = 0;
  for (const auto& set : config.source_sets) {
    key.add("language", project::to_string(set.language));
    key.add("standard", set.standard);
    key.add("library", set.library);
    for (const auto& define : set.defines) {
      key.add("define", define);
    }
    for (const auto& include : set.include_directories) {
      key.add("include", include.generic_string());
    }
    for (const auto& file : set.files) {
      key.add("source-path", file.lexically_normal().generic_string());
      if (set.language == project::Language::systemc) {
        std::error_code error;
        if (!key.add_file("source-content", file, error)) {
          diagnostics.error(
              "FSIM-CACHE-0001",
              "cannot hash source file '" + file.generic_string()
                  + "': " + error.message());
          return {};
        }
        continue;
      }
      if (hdl_source_index >= checked.hdl_sources.size()
          || !same_source_path(
              checked.hdl_sources[hdl_source_index].path, file)) {
        diagnostics.error(
            "FSIM-CACHE-0001",
            "parsed source identity is inconsistent for '"
                + file.generic_string() + "'");
        return {};
      }
      key.add(
          "source-content",
          checked.hdl_sources[hdl_source_index].content_digest);
      key.add(
          "source-compilation-unit",
          checked.hdl_sources[hdl_source_index]
              .compilation_unit_digest);
      for (const auto& dependency :
           checked.hdl_sources[hdl_source_index].dependencies) {
        key.add(
            "dependency-path",
            dependency.path.lexically_normal().generic_string());
        key.add(
            "dependency-content",
            dependency.content_digest);
      }
      ++hdl_source_index;
    }
  }
  if (hdl_source_index != checked.hdl_sources.size()) {
    diagnostics.error(
        "FSIM-CACHE-0001",
        "parsed HDL source count is inconsistent with the project manifest");
    return {};
  }
  for (const auto& binding : config.bindings) {
    key.add("binding-instance", binding.instance);
    key.add("binding-target", binding.target);
    key.add("binding-resolver", binding.resolver.value_or(""));
  }
  return key.finish();
}

std::optional<std::vector<std::string>>
make_specialization_cache_keys(
    const project::Config& config,
    const CheckedProject& checked,
    const elaboration::ElaboratedDesign& design,
    diagnostic::Engine& diagnostics)  {
  struct SourceSettings {
    const project::SourceSet* source_set{};
    const CheckedSource* checked_source{};
  };
  const auto settings_for =
      [&](const elaboration::SpecializationInfo& specialization,
          const std::string_view source,
          const bool require_specialization_library)
          -> std::optional<SourceSettings> {
        const auto source_path =
            std::filesystem::path{source}
                .lexically_normal();
        const CheckedSource* checked_source = nullptr;
        for (const auto& candidate : checked.hdl_sources) {
          if (same_source_path(candidate.path, source_path)
              || std::any_of(
                  candidate.dependencies.begin(),
                  candidate.dependencies.end(),
                  [&](const CheckedSource::Dependency& dependency) {
                    return same_source_path(
                        dependency.path, source_path);
                  })) {
            checked_source = &candidate;
            break;
          }
        }
        if (checked_source == nullptr) {
          return std::nullopt;
        }
        for (const auto& source_set : config.source_sets) {
          if (source_set.language == project::Language::systemc) {
            continue;
          }
          if (frontend_language(source_set.language)
              != specialization.language) {
            continue;
          }
          if (require_specialization_library
              && (source_set.library.empty()
                      ? "work"
                      : source_set.library)
                  != specialization.library) {
            continue;
          }
          if (std::any_of(
                  source_set.files.begin(),
                  source_set.files.end(),
                  [&](const std::filesystem::path& candidate) {
                    return same_source_path(
                        candidate, checked_source->path);
                  })) {
            return SourceSettings{&source_set, checked_source};
          }
        }
        return std::nullopt;
      };

  std::vector<std::string> result;
  result.reserve(design.specializations().size());
  for (const auto& specialization : design.specializations()) {
    const auto settings =
        settings_for(
            specialization, specialization.source, true);
    if (!settings) {
      diagnostics.error(
          "FSIM-CACHE-0001",
          "cannot associate elaborated specialization '"
              + specialization.unit + "' with parsed source '"
              + specialization.source + "'");
      return std::nullopt;
    }

    compiler::CacheKeyBuilder key;
    key.add(
        "specialization-provenance-schema",
        "fsim-specialization-provenance-v3");
    key.add("fsim-version", version);
    key.add("standard-library", standard_library_cache_version);
    key.add("delay-mode", project::to_string(config.run.delay_mode));
    key.add(
        "verilog-preprocessor",
        frontend::verilog_preprocessor_cache_version);
    key.add("unit", specialization.unit);
    key.add(
        "source-path",
        settings->checked_source->path.lexically_normal().generic_string());
    key.add(
        "source-content",
        settings->checked_source->content_digest);
    key.add(
        "source-compilation-unit",
        settings->checked_source->compilation_unit_digest);
    for (const auto& dependency :
         settings->checked_source->dependencies) {
      key.add(
          "dependency-path",
          dependency.path.lexically_normal().generic_string());
      key.add(
          "dependency-content",
          dependency.content_digest);
    }
    for (const auto& dependency_source :
         specialization.source_dependencies) {
      const auto dependency_settings =
          settings_for(
              specialization, dependency_source, false);
      if (!dependency_settings) {
        diagnostics.error(
            "FSIM-CACHE-0001",
            "cannot associate elaborated specialization dependency '"
                + dependency_source + "' for '"
                + specialization.unit + "' with a checked source");
        return std::nullopt;
      }
      key.add(
          "semantic-dependency-source-path",
          dependency_settings->checked_source->path
              .lexically_normal()
              .generic_string());
      key.add(
          "semantic-dependency-source-content",
          dependency_settings->checked_source->content_digest);
      key.add(
          "semantic-dependency-source-compilation-unit",
          dependency_settings->checked_source
              ->compilation_unit_digest);
      key.add(
          "semantic-dependency-language",
          project::to_string(
              dependency_settings->source_set->language));
      key.add(
          "semantic-dependency-standard",
          dependency_settings->source_set->standard);
      key.add(
          "semantic-dependency-library",
          dependency_settings->source_set->library);
      key.add(
          "semantic-dependency-compilation-unit",
          dependency_settings->source_set->compilation_unit);
      for (const auto& define :
           dependency_settings->source_set->defines) {
        key.add("semantic-dependency-define", define);
      }
      for (const auto& include :
           dependency_settings->source_set
               ->include_directories) {
        key.add(
            "semantic-dependency-include",
            include.lexically_normal().generic_string());
      }
      for (const auto& dependency :
           dependency_settings->checked_source->dependencies) {
        key.add(
            "semantic-dependency-transitive-path",
            dependency.path.lexically_normal().generic_string());
        key.add(
            "semantic-dependency-transitive-content",
            dependency.content_digest);
      }
    }
    key.add(
        "language",
        project::to_string(settings->source_set->language));
    key.add("standard", settings->source_set->standard);
    key.add("library", settings->source_set->library);
    key.add(
        "compilation-unit",
        settings->source_set->compilation_unit);
    for (const auto& define : settings->source_set->defines) {
      key.add("define", define);
    }
    for (const auto& include :
         settings->source_set->include_directories) {
      key.add("include", include.lexically_normal().generic_string());
    }
    const auto& parameter_identity =
        specialization.parameter_identity_values.empty()
            ? specialization.parameter_values
            : specialization.parameter_identity_values;
    for (const auto& [name, value] : parameter_identity) {
      key.add("parameter-name", name);
      key.add("parameter-value", value);
    }
    result.push_back(key.finish());
  }
  return result;
}

bool wildcard_match(std::string_view pattern, std::string_view text)  {
  std::size_t pattern_index = 0;
  std::size_t text_index = 0;
  std::size_t star = std::string_view::npos;
  std::size_t retry = 0;
  while (text_index < text.size()) {
    if (pattern_index < pattern.size()
        && (pattern[pattern_index] == '?'
            || pattern[pattern_index] == text[text_index])) {
      ++pattern_index;
      ++text_index;
    } else if (
        pattern_index < pattern.size() && pattern[pattern_index] == '*') {
      star = pattern_index++;
      retry = text_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1;
      text_index = ++retry;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

bool trace_selected(
    const std::vector<std::string>& filters,
    const std::string_view name)  {
  return filters.empty()
      || std::any_of(filters.begin(), filters.end(), [&](const auto& filter) {
           return wildcard_match(filter, name);
         });
}

std::optional<VcdScale> vcd_scale(
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)  {
  if (resolution == "auto") {
    return VcdScale{"1ns", 1};
  }
  std::string compact;
  for (const char character : resolution) {
    if (std::isspace(static_cast<unsigned char>(character)) == 0) {
      compact.push_back(character);
    }
  }
  const auto unit_begin = std::find_if(
      compact.begin(), compact.end(), [](const char character) {
        return character < '0' || character > '9';
      });
  std::uint64_t magnitude = 0;
  const auto* magnitude_end =
      compact.data() + std::distance(compact.begin(), unit_begin);
  const auto [end, conversion_error] =
      std::from_chars(compact.data(), magnitude_end, magnitude);
  const std::string_view unit{unit_begin, compact.end()};
  const bool valid_unit =
      unit == "fs" || unit == "ps" || unit == "ns"
      || unit == "us" || unit == "ms" || unit == "s";
  if (conversion_error != std::errc{} || end != magnitude_end
      || magnitude == 0 || !valid_unit) {
    diagnostics.error(
        "FSIM-TIME-0001",
        "cannot derive a VCD timescale from '" + std::string(resolution) + "'");
    return std::nullopt;
  }
  const std::uint64_t vcd_magnitude =
      magnitude % 100 == 0 ? 100 : magnitude % 10 == 0 ? 10 : 1;
  return VcdScale{
      std::to_string(vcd_magnitude) + std::string(unit),
      magnitude / vcd_magnitude};
}

std::unique_ptr<TraceState> attach_trace(
    Simulation& simulation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const bool dynamic_selection)  {
  if (!config.run.trace_file) {
    return nullptr;
  }
  const auto scale = vcd_scale(
      simulation.time_resolution(), diagnostics);
  if (!scale) {
    return nullptr;
  }
  auto trace = std::make_unique<TraceState>();
  std::error_code parent_error;
  const auto parent = config.run.trace_file->parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, parent_error);
  }
  if (parent_error) {
    diagnostics.error(
        "FSIM-VCD-0001",
        "cannot create trace directory: " + parent_error.message());
    return nullptr;
  }
  trace->stream.open(*config.run.trace_file, std::ios::binary | std::ios::trunc);
  if (!trace->stream) {
    diagnostics.error(
        "FSIM-VCD-0002",
        "cannot open trace file '" + config.run.trace_file->generic_string()
            + "'");
    return nullptr;
  }
  try {
    trace->tick_multiplier = scale->tick_multiplier;
    trace->writer =
        std::make_unique<runtime::VcdWriter>(
            trace->stream, scale->timescale);
    trace->handles.resize(simulation.design().signals().size());
    trace->enabled.resize(simulation.design().signals().size());
    for (const auto& signal : simulation.design().signals()) {
      const auto selected =
          trace_selected(config.run.trace_filters, signal.name);
      trace->enabled[signal.id] = selected;
      if (dynamic_selection || selected) {
        trace->handles[signal.id] =
            trace->writer->declare_signal(signal.name, signal.width);
      }
    }
    if (simulation.now()
        > std::numeric_limits<SimulationTick>::max()
              / trace->tick_multiplier) {
      throw std::overflow_error{"VCD timestamp scaling overflow"};
    }
    trace->writer->begin(simulation.now() * trace->tick_multiplier);
    for (const auto& signal : simulation.design().signals()) {
      if (trace->handles[signal.id] && trace->enabled[signal.id]) {
        trace->writer->change(
            *trace->handles[signal.id], simulation.read_signal(signal.id));
      }
    }
    auto* state = trace.get();
    simulation.set_signal_change_hook(
        [state](
            const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time,
            std::uint64_t) {
          if (signal < state->handles.size() && state->handles[signal]
              && state->enabled[signal]) {
            if (time
                > std::numeric_limits<SimulationTick>::max()
                      / state->tick_multiplier) {
              throw std::overflow_error{"VCD timestamp scaling overflow"};
            }
            state->writer->set_time(time * state->tick_multiplier);
            state->writer->change(*state->handles[signal], value);
          }
        });
  } catch (const std::exception& error) {
    diagnostics.error("FSIM-VCD-0003", error.what());
    return nullptr;
  }
  return trace;
}

std::optional<SimulationTick> configured_duration(
    const project::Config& config,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)  {
  if (!config.run.duration) {
    return std::nullopt;
  }
  std::string error;
  const auto duration = parse_time(
      *config.run.duration, resolution, error);
  if (!duration) {
    diagnostics.error("FSIM-TIME-0002", error);
  }
  return duration;
}

void install_interrupt_hook(Simulation& simulation)  {
  interrupt_requested.store(false, std::memory_order_relaxed);
  simulation.set_safe_point_hook([](runtime::Scheduler& scheduler,
                                    runtime::SchedulerPhase) {
    if (interrupt_requested.exchange(false, std::memory_order_relaxed)) {
      scheduler.request_stop();
    }
  });
}

void report_native_cache_failures(
    const Simulation& simulation,
    diagnostic::Engine& diagnostics)  {
  const auto cache = simulation.native_cache_statistics();
  if (cache.load_failures == 0 && cache.store_failures == 0
      && cache.prune_failures == 0) {
    return;
  }
  diagnostics.warning(
      "FSIM-CACHE-0004",
      "native LLVM object cache reported "
          + std::to_string(cache.load_failures)
          + " load failure(s) and "
          + std::to_string(cache.store_failures)
          + " store failure(s), and "
          + std::to_string(cache.prune_failures)
          + " prune failure(s); simulation remains valid, but cache reuse "
            "or eviction may be incomplete");
}

int handle_check(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)  {
  auto checked = check_project(config, diagnostics);
  if (!checked) {
    return 1;
  }
  output << "checked " << checked->source_count << " source file(s), "
         << checked->parsed.units.size() << " design unit(s)\n";
  return 0;
}

int handle_build(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)  {
  auto built = build_project(config, diagnostics);
  if (!built) {
    return 1;
  }
  const auto top = built->design.top();
  const auto signal_count = built->design.signals().size();
  const auto process_count = built->design.processes().size();
  const auto plugin_count = built->systemc_plugins.size();
  const auto cache_hit = built->cache_hit;
  const auto selected_seed = built->seed;
  const auto entropy_seed_selected = built->entropy_seed;
  Simulation prepared(
      std::move(*built),
      config.run.max_deltas,
      SimulationEngine::compiled);
  report_native_cache_failures(prepared, diagnostics);
  const auto native_cache = prepared.native_cache_statistics();
  output << "built " << top << " ("
         << signal_count << " signals, "
         << process_count << " processes";
  if (plugin_count != 0) {
    output << ", " << plugin_count
           << " validated SystemC plug-in artifact(s)";
  }
  output << ", " << prepared.compiled_process_count()
         << " LLVM-compiled process(es) in "
         << prepared.compiled_module_count()
         << " specialization module(s)";
  output << "; analysis cache "
         << (cache_hit ? "hit" : "populated");
  if (prepared.compiled_process_count() == 0) {
    output << "; native cache unused";
  } else {
    output << "; native cache "
           << native_cache.hits << " hit(s), "
           << native_cache.misses << " miss(es), "
           << native_cache.stores << " store(s), "
           << native_cache.rejected_entries << " rejected";
  }
  output << ")\n";
  if (entropy_seed_selected) {
    output << "random seed " << selected_seed << '\n';
  }
  return 0;
}

int handle_run(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)  {
  auto built = build_project(config, diagnostics);
  if (!built) {
    return 1;
  }
  const auto duration = configured_duration(
      config, built->time_resolution, diagnostics);
  if (config.run.duration && !duration) {
    return 1;
  }
  if (built->entropy_seed) {
    output << "random seed " << built->seed << '\n';
  }
  Simulation simulation(
      std::move(*built),
      config.run.max_deltas,
      SimulationEngine::compiled);
  simulation.set_output_hook(
      [&output](
          const runtime::simir::ProcessId,
          const std::string_view text,
          const bool newline,
          const SimulationTick,
          const std::uint64_t) {
        output << text;
        if (newline) {
          output << '\n';
        }
      });
  simulation.set_report_hook(
      [&output](
          const runtime::simir::ProcessId,
          const std::string_view message,
          const runtime::simir::AssertionSeverity severity,
          const runtime::simir::SourceLocation& source,
          const SimulationTick,
          const std::uint64_t) {
        output << source.path << ':' << source.line << ':'
               << source.column << ": "
               << report_severity_name(severity)
               << "[FSIM-HDL-REPORT]: " << message << '\n';
      });
  report_native_cache_failures(simulation, diagnostics);
  auto trace = attach_trace(simulation, config, diagnostics);
  if (config.run.trace_file && !trace) {
    return 1;
  }
  install_interrupt_hook(simulation);
  const InterruptSignalGuard interrupt_signal;
  try {
    const auto result = simulation.run(duration);
    if (trace) {
      trace->writer->flush();
    }
    output << "simulation "
           << (result.status == runtime::RunStatus::completed
                   ? "completed"
                   : result.status == runtime::RunStatus::time_limit
                       ? "reached time limit"
                       : "stopped")
           << " at tick " << result.time << ", delta " << result.delta << '\n';
    return 0;
  } catch (const runtime::DeltaCycleLimitError& error) {
    std::ostringstream message;
    message << error.what() << "; active process IDs: ";
    if (error.pending_orders().empty()) {
      message << "none";
    } else {
      for (std::size_t index = 0;
           index < error.pending_orders().size(); ++index) {
        if (index != 0) {
          message << ',';
        }
        message << error.pending_orders()[index];
      }
    }
    message << "; recently changed signal IDs: ";
    if (error.recent_signals().empty()) {
      message << "none";
    } else {
      for (std::size_t index = 0;
           index < error.recent_signals().size(); ++index) {
        if (index != 0) {
          message << ',';
        }
        message << error.recent_signals()[index];
      }
    }
    diagnostics.error(
        "FSIM-RUN-DELTA-0001", message.str());
  } catch (const runtime::simir::AssertionError& error) {
    diagnostic::SourceSpan span;
    span.path = error.source().path;
    span.begin.line = error.source().line;
    span.begin.column = error.source().column;
    span.end = span.begin;
    const auto severity = [&] {
      switch (error.severity()) {
        case runtime::simir::AssertionSeverity::note:
          return diagnostic::Severity::note;
        case runtime::simir::AssertionSeverity::warning:
          return diagnostic::Severity::warning;
        case runtime::simir::AssertionSeverity::error:
          return diagnostic::Severity::error;
        case runtime::simir::AssertionSeverity::failure:
          return diagnostic::Severity::fatal;
      }
      return diagnostic::Severity::error;
    }();
    diagnostics.report(diagnostic::Diagnostic{
        severity, "FSIM-RUN-ASSERT-0001", error.what(), std::move(span), {}});
  } catch (const runtime::simir::InterpreterError& error) {
    diagnostics.error("FSIM-RUN-0001", error.what());
  } catch (const std::exception& error) {
    diagnostics.error("FSIM-RUN-0002", error.what());
  }
  return 1;
}

void print_debug_help(std::ostream& output)  {
  output
      << "Commands: continue|run [DURATION], run-until TIME, "
         "step statement|process|delta|time,\n"
      << "          break source [PATH:]LINE, break time TIME, "
         "break signal SIGNAL [==|!= VALUE],\n"
      << "          breakpoints,\n"
      << "          delete ID, clear, scope [PATH], scopes [PATH], "
         "signals [PATH],\n"
      << "          show SIGNAL,\n"
      << "          deposit SIGNAL VALUE, force SIGNAL VALUE, release SIGNAL,\n"
      << "          trace add|remove SIGNAL, trace all|clear|list,\n"
      << "          locals, where, help, quit\n";
}

} // namespace fsim::app::application_detail
