// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {

const std::string& ElaboratedDesign::top() const noexcept {
  return top_;
}

const std::vector<std::string>& ElaboratedDesign::roots() const noexcept {
  return roots_;
}

const std::vector<SignalInfo>&
ElaboratedDesign::signals() const noexcept {
  return signal_info_;
}

const std::vector<BoundaryConversionInfo>&
ElaboratedDesign::boundary_conversions() const noexcept {
  return boundary_conversions_;
}

const std::vector<StringObjectInfo>&
ElaboratedDesign::string_objects() const noexcept {
  return string_object_info_;
}

const std::vector<ContainerObjectInfo>&
ElaboratedDesign::container_objects() const noexcept {
  return container_object_info_;
}

const std::vector<VhdlProtectedObjectInfo>&
ElaboratedDesign::vhdl_protected_objects() const noexcept {
  return vhdl_protected_object_info_;
}

const std::vector<runtime::simir::Process>&
ElaboratedDesign::processes() const noexcept {
  return processes_;
}

const std::vector<SpecializationInfo>&
ElaboratedDesign::specializations() const noexcept {
  return specializations_;
}

const std::vector<UdpTableInfo>&
ElaboratedDesign::udp_tables() const noexcept {
  return udp_tables_;
}

const std::vector<VerilogSpecifyPathInfo>&
ElaboratedDesign::verilog_specify_paths() const noexcept {
  return verilog_specify_paths_;
}

const std::vector<runtime::simir::ModuleTimingCheck>&
ElaboratedDesign::verilog_timing_checks() const noexcept {
  return verilog_timing_checks_;
}

const std::vector<SystemCInstanceInfo>&
ElaboratedDesign::systemc_instances() const noexcept {
  return systemc_instances_;
}

const std::vector<SystemCProcessInfo>&
ElaboratedDesign::systemc_processes() const noexcept {
  return systemc_processes_;
}

const std::vector<SystemCNamedObjectInfo>&
ElaboratedDesign::systemc_objects() const noexcept {
  return systemc_objects_;
}

std::optional<runtime::simir::SignalId>
ElaboratedDesign::find_signal(
    const std::string_view name) const noexcept {
  if (const auto found = signal_by_name_.find(std::string{name});
      found != signal_by_name_.end()) {
    return found->second;
  }
  const auto object = std::find_if(
      systemc_objects_.begin(), systemc_objects_.end(),
      [&](const SystemCNamedObjectInfo& candidate) {
        return candidate.name == name && candidate.signal.has_value();
      });
  if (object != systemc_objects_.end()) {
    return object->signal;
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, runtime::simir::SignalId>>
ElaboratedDesign::signal_paths() const {
  std::vector<std::pair<std::string, runtime::simir::SignalId>> result;
  result.reserve(signal_by_name_.size() + systemc_objects_.size());
  for (const auto& [path, signal] : signal_by_name_) {
    result.emplace_back(path, signal);
  }
  for (const auto& object : systemc_objects_) {
    if (object.signal) {
      result.emplace_back(object.name, *object.signal);
    }
  }
  std::sort(
      result.begin(),
      result.end(),
      [](const auto& left, const auto& right) {
        if (left.first != right.first) {
          return left.first < right.first;
        }
        return left.second < right.second;
      });
  result.erase(
      std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::optional<runtime::simir::ContainerObjectId>
ElaboratedDesign::find_container(
    const std::string_view name) const noexcept {
  if (const auto found =
          container_by_name_.find(std::string{name});
      found != container_by_name_.end()) {
    return found->second;
  }
  return std::nullopt;
}

std::vector<std::pair<
    std::string, runtime::simir::ContainerObjectId>>
ElaboratedDesign::container_paths() const {
  std::vector<std::pair<
      std::string, runtime::simir::ContainerObjectId>> result;
  result.reserve(container_by_name_.size());
  for (const auto& [path, object] : container_by_name_) {
    result.emplace_back(path, object);
  }
  std::ranges::sort(
      result,
      [](const auto& left, const auto& right) {
        if (left.first != right.first) {
          return left.first < right.first;
        }
        return left.second < right.second;
      });
  return result;
}

std::unique_ptr<runtime::simir::Interpreter>
ElaboratedDesign::create_interpreter(
    const runtime::SchedulerOptions options,
    const std::uint64_t seed) const {
  auto interpreter =
      std::make_unique<runtime::simir::Interpreter>(options, seed);
  for (const auto& signal : signals_) {
    (void)interpreter->add_signal(signal);
  }
  for (const auto& object : string_objects_) {
    (void)interpreter->add_string_object(object);
  }
  for (const auto& object : container_objects_) {
    (void)interpreter->add_container_object(object);
  }
  for (const auto& alias : container_signal_aliases_) {
    interpreter->add_container_signal_alias(alias);
  }
  for (const auto& process : processes_) {
    (void)interpreter->add_process(process);
  }
  for (const auto& path : verilog_specify_paths_) {
    runtime::simir::ModulePath runtime_path;
    runtime_path.id = path.id;
    const auto append_terminals = [](const auto& source, auto& destination) {
      destination.reserve(source.size());
      for (const auto& terminal : source) {
        destination.push_back(runtime::simir::ModulePathTerminal{
            terminal.signal, terminal.offset, terminal.width});
      }
    };
    append_terminals(path.sources, runtime_path.sources);
    append_terminals(path.destinations, runtime_path.destinations);
    runtime_path.drivers = path.drivers;
    runtime_path.delays = path.delays;
    runtime_path.condition = path.condition_program;
    runtime_path.data_source = path.data_source_program;
    runtime_path.selection_group = path.selection_group;
    runtime_path.full =
        path.kind == frontend::VerilogModulePathKind::Full;
    runtime_path.conditional = path.conditional;
    runtime_path.ifnone = path.ifnone;
    switch (path.source_edge) {
      case frontend::VerilogSpecifyEdge::None:
        runtime_path.source_edge = runtime::simir::ModulePathEdge::none;
        break;
      case frontend::VerilogSpecifyEdge::Posedge:
        runtime_path.source_edge = runtime::simir::ModulePathEdge::posedge;
        break;
      case frontend::VerilogSpecifyEdge::Negedge:
        runtime_path.source_edge = runtime::simir::ModulePathEdge::negedge;
        break;
      case frontend::VerilogSpecifyEdge::Edge:
        runtime_path.source_edge = runtime::simir::ModulePathEdge::edge;
        break;
    }
    switch (path.polarity) {
      case frontend::VerilogPathPolarity::None:
        runtime_path.polarity = runtime::simir::ModulePathPolarity::none;
        break;
      case frontend::VerilogPathPolarity::Positive:
        runtime_path.polarity = runtime::simir::ModulePathPolarity::positive;
        break;
      case frontend::VerilogPathPolarity::Negative:
        runtime_path.polarity = runtime::simir::ModulePathPolarity::negative;
        break;
    }
    runtime_path.pulse_style =
        path.pulse_style == frontend::VerilogPulseStyle::Ondetect
        ? runtime::simir::ModulePathPulseStyle::ondetect
        : runtime::simir::ModulePathPulseStyle::onevent;
    runtime_path.show_cancelled = path.show_cancelled;
    runtime_path.pulse_reject_limit = path.pulse_reject_limit;
    runtime_path.pulse_error_limit = path.pulse_error_limit;
    runtime_path.source = runtime::simir::SourceLocation{
        path.source.source_name,
        static_cast<std::uint32_t>(path.source.begin.line),
        static_cast<std::uint32_t>(path.source.begin.column)};
    (void)interpreter->add_module_path(std::move(runtime_path));
  }
  for (const auto& check : verilog_timing_checks_) {
    (void)interpreter->add_module_timing_check(check);
  }
  return interpreter;
}

ElaboratedDesignState ElaboratedDesign::state() const {
  ElaboratedDesignState result{
      top_, roots_, signal_info_, boundary_conversions_, signals_,
      string_object_info_, string_objects_, container_object_info_,
      container_objects_, vhdl_protected_object_info_, processes_,
      specializations_, udp_tables_, verilog_specify_paths_,
      verilog_timing_checks_,
      systemc_instances_, systemc_processes_, systemc_objects_, {}, {}, {}};
  result.signal_names.assign(signal_by_name_.begin(), signal_by_name_.end());
  result.string_names.assign(string_by_name_.begin(), string_by_name_.end());
  result.container_names.assign(
      container_by_name_.begin(), container_by_name_.end());
  const auto by_name = [](const auto& left, const auto& right) {
    return left.first < right.first;
  };
  std::ranges::sort(result.signal_names, by_name);
  std::ranges::sort(result.string_names, by_name);
  std::ranges::sort(result.container_names, by_name);
  return result;
}

std::optional<ElaboratedDesign> ElaboratedDesign::from_state(
    ElaboratedDesignState state) {
  if (state.top.empty() || state.roots.empty()
      || state.signal_info.size() != state.signals.size()
      || state.string_object_info.size() != state.string_objects.size()
      || state.container_object_info.size() != state.container_objects.size()) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < state.signal_info.size(); ++index) {
    const auto valid_strength = [](const runtime::simir::StrengthRank rank) {
      return static_cast<std::underlying_type_t<
          runtime::simir::StrengthRank>>(rank)
          <= static_cast<std::underlying_type_t<
              runtime::simir::StrengthRank>>(
                  runtime::simir::StrengthRank::supply);
    };
    if (state.signal_info[index].id != index
        || state.signal_info[index].width
            != state.signals[index].initial_value.width()
        || !valid_strength(
            state.signals[index].implicit_drive_strength.zero)
        || !valid_strength(
            state.signals[index].implicit_drive_strength.one)
        || (state.signals[index].charge_strength
            && !valid_strength(
                *state.signals[index].charge_strength))) {
      return std::nullopt;
    }
  }
  for (std::size_t index = 0; index < state.string_object_info.size(); ++index) {
    if (state.string_object_info[index].id != index) {
      return std::nullopt;
    }
  }
  for (std::size_t index = 0; index < state.container_object_info.size(); ++index) {
    if (state.container_object_info[index].id != index) {
      return std::nullopt;
    }
  }
  for (std::size_t index = 0; index < state.processes.size(); ++index) {
    const auto& process = state.processes[index];
    const auto valid_strength = [](const runtime::simir::StrengthRank rank) {
      return static_cast<std::underlying_type_t<
          runtime::simir::StrengthRank>>(rank)
          <= static_cast<std::underlying_type_t<
              runtime::simir::StrengthRank>>(
                  runtime::simir::StrengthRank::supply);
    };
    if (process.id != index
        || !valid_strength(process.drive_strength.zero)
        || !valid_strength(process.drive_strength.one)
        || (process.switch_source
            && *process.switch_source
                >= state.signals.size())
        || (process.switch_target
            && *process.switch_target
                >= state.signals.size())
        || (process.switch_control
            && *process.switch_control
                >= state.signals.size())
        || (process.switch_bidirectional
            && (!process.switch_source || !process.switch_target))) {
      return std::nullopt;
    }
    if (process.switch_source && process.switch_target) {
      const auto source_width = state.signals[*process.switch_source]
                                    .initial_value.width();
      const auto target_width = state.signals[*process.switch_target]
                                    .initial_value.width();
      if ((source_width != target_width
           && source_width != 1 && target_width != 1)
          || (process.switch_control
              && state.signals[*process.switch_control]
                         .initial_value.width() != 1
              && state.signals[*process.switch_control]
                         .initial_value.width()
                  != std::max(source_width, target_width))) {
        return std::nullopt;
      }
    }
  }
  for (std::size_t index = 0; index < state.specializations.size(); ++index) {
    if (state.specializations[index].id != index) {
      return std::nullopt;
    }
  }
  for (std::size_t index = 0; index < state.udp_tables.size(); ++index) {
    const auto& table = state.udp_tables[index];
    const auto digest_is_hex = table.digest.size() == 64
        && std::ranges::all_of(table.digest, [](const char character) {
             return (character >= '0' && character <= '9')
                 || (character >= 'a' && character <= 'f');
           });
    frontend::VerilogUdpDeclaration declaration;
    declaration.language = frontend::Language::Verilog2005;
    declaration.name = table.identity;
    declaration.sequential = table.sequential;
    declaration.output_reg = table.sequential;
    declaration.initial_output = table.initial_output;
    declaration.rows = table.rows;
    if (!table.terminals.empty()) {
      declaration.output = table.terminals.front();
      declaration.inputs.assign(
          table.terminals.begin() + 1, table.terminals.end());
    }
    if (table.id != index || !table.identity.starts_with("udp:")
        || !digest_is_hex
        || !frontend::verilog_udp_declaration_well_formed(declaration)) {
      return std::nullopt;
    }
  }
  std::unordered_map<std::string, std::string> udp_digests;
  for (const auto& table : state.udp_tables) {
    if (!udp_digests.emplace(table.identity, table.digest).second) {
      return std::nullopt;
    }
  }
  std::unordered_set<std::string> referenced_udp_tables;
  for (const auto& specialization : state.specializations) {
    std::optional<std::string_view> identity;
    std::optional<std::string_view> digest;
    for (const auto& [name, value] :
         specialization.parameter_identity_values) {
      if (name == "__udp") {
        if (identity) return std::nullopt;
        identity = value;
      } else if (name == "__udp_table") {
        if (digest) return std::nullopt;
        digest = value;
      }
    }
    if (identity.has_value() != digest.has_value()) return std::nullopt;
    if (identity) {
      const auto table = udp_digests.find(std::string{*identity});
      if (table == udp_digests.end() || table->second != *digest) {
        return std::nullopt;
      }
      referenced_udp_tables.emplace(*identity);
    }
  }
  if (referenced_udp_tables.size() != state.udp_tables.size()) {
    return std::nullopt;
  }
  for (std::size_t index = 0;
       index < state.verilog_specify_paths.size(); ++index) {
    const auto& path = state.verilog_specify_paths[index];
    const auto valid_terminal = [&](const VerilogSpecifyTerminalInfo& terminal) {
      return terminal.signal < state.signals.size()
          && terminal.width != 0
          && terminal.offset
              <= state.signals[terminal.signal].initial_value.width()
          && terminal.width
              <= state.signals[terminal.signal].initial_value.width()
                  - terminal.offset;
    };
    const bool valid_delay_count = path.delays.size() == 1
        || path.delays.size() == 2 || path.delays.size() == 3
        || path.delays.size() == 6 || path.delays.size() == 12;
    if (path.id != index || path.instance.empty()
        || path.sources.empty() || path.destinations.empty()
        || !valid_delay_count
        || !std::ranges::all_of(path.sources, valid_terminal)
        || !std::ranges::all_of(path.destinations, valid_terminal)
        || (path.kind == frontend::VerilogModulePathKind::Parallel
            && (path.sources.size() != path.destinations.size()
                || !std::ranges::equal(
                    path.sources, path.destinations,
                    [](const auto& left, const auto& right) {
                      return left.width == right.width;
                    })))) {
      return std::nullopt;
    }
    if (!std::ranges::is_sorted(path.drivers)
        || std::ranges::adjacent_find(path.drivers)
            != path.drivers.end()
        || !std::ranges::all_of(
            path.drivers,
            [&](const runtime::simir::ProcessId driver) {
              return driver < state.processes.size();
            })) {
      return std::nullopt;
    }
  }
  ElaboratedDesign result;
  result.top_ = std::move(state.top);
  result.roots_ = std::move(state.roots);
  result.signal_info_ = std::move(state.signal_info);
  result.boundary_conversions_ = std::move(state.boundary_conversions);
  result.signals_ = std::move(state.signals);
  result.string_object_info_ = std::move(state.string_object_info);
  result.string_objects_ = std::move(state.string_objects);
  result.container_object_info_ = std::move(state.container_object_info);
  result.container_objects_ = std::move(state.container_objects);
  result.vhdl_protected_object_info_ =
      std::move(state.vhdl_protected_object_info);
  result.processes_ = std::move(state.processes);
  result.specializations_ = std::move(state.specializations);
  result.udp_tables_ = std::move(state.udp_tables);
  result.verilog_specify_paths_ =
      std::move(state.verilog_specify_paths);
  result.verilog_timing_checks_ =
      std::move(state.verilog_timing_checks);
  result.systemc_instances_ = std::move(state.systemc_instances);
  result.systemc_processes_ = std::move(state.systemc_processes);
  result.systemc_objects_ = std::move(state.systemc_objects);
  for (auto& entry : state.signal_names) {
    if (entry.first.empty() || entry.second >= result.signals_.size()
        || !result.signal_by_name_.emplace(std::move(entry)).second) {
      return std::nullopt;
    }
  }
  for (auto& entry : state.string_names) {
    if (entry.first.empty() || entry.second >= result.string_objects_.size()
        || !result.string_by_name_.emplace(std::move(entry)).second) {
      return std::nullopt;
    }
  }
  for (auto& entry : state.container_names) {
    if (entry.first.empty() || entry.second >= result.container_objects_.size()
        || !result.container_by_name_.emplace(std::move(entry)).second) {
      return std::nullopt;
    }
  }
  try {
    (void)result.create_interpreter();
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return result;
}

}  // namespace fsim::elaboration
