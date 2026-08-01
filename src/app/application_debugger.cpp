// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

namespace {

[[nodiscard]] std::string escaped_string(
    const std::string_view value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result{"\""};
  for (const auto byte : value) {
    const auto character = static_cast<unsigned char>(byte);
    switch (character) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (character >= 0x20 && character <= 0x7e) {
        result.push_back(static_cast<char>(character));
      } else {
        result += "\\x";
        result.push_back(digits[character >> 4U]);
        result.push_back(digits[character & 0x0fU]);
      }
      break;
    }
  }
  result.push_back('"');
  return result;
}

[[nodiscard]] std::string format_container(
    const runtime::simir::ContainerValue& value) {
  std::string result{"["};
  for (std::size_t index = 0; index < value.elements.size(); ++index) {
    if (index != 0) {
      result += ", ";
    }
    if (value.type.associative) {
      result += value.keys[index].to_msb_string();
      result += "=>";
    } else if (value.type.fixed) {
      const auto declared_index =
          value.type.index_left >= value.type.index_right
              ? static_cast<std::int64_t>(value.type.index_left)
                    - static_cast<std::int64_t>(index)
              : static_cast<std::int64_t>(value.type.index_left)
                    + static_cast<std::int64_t>(index);
      result += std::to_string(declared_index);
      result += ":";
    }
    result += value.elements[index].to_msb_string();
  }
  result += "]";
  return result;
}

}  // namespace

DebuggerSession::ExecutionGuard::~ExecutionGuard() {
  executing = false;
}

DebuggerSession::DebuggerSession(
     Simulation& simulation,
     std::ostream& output,
     std::ostream& error,
     TraceState* trace)
     : simulation_(simulation),
       output_(output),
       error_(error),
       trace_(trace),
       scope_(simulation.design().top()),
       signal_paths_(simulation.design().signal_paths()),
       container_paths_(simulation.design().container_paths())  {
    observer_ = simulation_.add_signal_change_hook(
        [this](
            const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time,
            const std::uint64_t delta) {
          if (!executing_ || hit_) {
            return;
          }
          const auto found = std::find_if(
              breakpoints_.begin(), breakpoints_.end(),
              [signal, &value](const DebugBreakpoint& breakpoint) {
                if (breakpoint.kind != DebugBreakpointKind::signal
                    || breakpoint.signal != signal) {
                  return false;
                }
                if (!breakpoint.signal_condition) {
                  return true;
                }
                const auto equal =
                    value == *breakpoint.signal_condition;
                return equal == breakpoint.signal_condition_equal;
              });
          if (found == breakpoints_.end()) {
            return;
          }
          hit_ = DebugBreakpointHit{
              found->id,
              found->path + " changed to " + value.to_msb_string()
                  + " at time " + std::to_string(time) + ", delta "
                  + std::to_string(delta)};
          simulation_.request_stop();
        });
  }

DebuggerSession::~DebuggerSession()  {
    simulation_.remove_signal_change_hook(observer_);
    simulation_.set_execution_point_hook({});
    install_interrupt_hook(simulation_);
  }

void DebuggerSession::execute(const std::vector<std::string>& command)  {
    if (command[0] == "where") {
      output_ << "time " << simulation_.now() << ", delta "
              << simulation_.delta() << ", scope " << scope_ << '\n';
      return;
    }
    if (command[0] == "locals" && command.size() == 1) {
      show_locals();
      return;
    }
    if (command[0] == "scope") {
      scope_command(command);
      return;
    }
    if (command[0] == "scopes") {
      scopes_command(command);
      return;
    }
    if (command[0] == "signals") {
      signals_command(command);
      return;
    }
    if (command[0] == "show" && command.size() == 2) {
      if (const auto object = resolve_container_object(command[1])) {
        output_ << object->first << " = "
                << format_container(
                       simulation_.read_container_object(
                           object->second))
                << '\n';
        return;
      }
      if (const auto object = resolve_string_object(command[1])) {
        output_ << object->first << " = "
                << escaped_string(
                       simulation_.read_string_object(object->second))
                << '\n';
        return;
      }
      if (const auto signal = resolve_signal(command[1])) {
        const auto& info =
            simulation_.design().signals().at(signal->second);
        output_ << signal->first << " = "
                << format_value(
                       simulation_.read_signal(signal->second),
                       info.enumeration_literals);
        if (simulation_.signal_is_forced(signal->second)) {
          output_ << " (forced)";
        }
        output_ << '\n';
      }
      return;
    }
    if ((command[0] == "deposit" || command[0] == "force")
        && command.size() == 3) {
      if (const auto object = resolve_string_object(command[1])) {
        if (command[0] == "force") {
          output_
              << "force is not supported for mutable string objects; "
                 "use deposit\n";
        } else if (
            command[2].size()
            > runtime::simir::maximum_string_bytes) {
          output_ << "string deposit exceeds the 4096-byte limit\n";
        } else {
          simulation_.deposit_string_object(
              object->second, command[2]);
        }
        return;
      }
      modify_signal(command);
      return;
    }
    if (command[0] == "release" && command.size() == 2) {
      if (const auto signal = resolve_signal(command[1])) {
        simulation_.release_signal(signal->second);
      }
      return;
    }
    if (command[0] == "break"
        && (command.size() == 3 || command.size() == 5)) {
      add_breakpoint(command);
      return;
    }
    if (command[0] == "breakpoints"
        || (command.size() == 2 && command[0] == "info"
            && command[1] == "breakpoints")) {
      list_breakpoints();
      return;
    }
    if (command[0] == "delete" && command.size() == 2) {
      delete_breakpoint(command[1]);
      return;
    }
    if (command[0] == "clear" && command.size() == 1) {
      breakpoints_.clear();
      output_ << "cleared all breakpoints\n";
      return;
    }
    if (command[0] == "trace") {
      try {
        trace_command(command);
      } catch (const std::exception& exception) {
        error_ << exception.what() << '\n';
      }
      return;
    }
    if ((command[0] == "continue" || command[0] == "run")
        && command.size() <= 2) {
      std::optional<SimulationTick> limit;
      if (command.size() == 2) {
        const auto relative = command_time(command[1]);
        if (!relative) {
          return;
        }
        if (*relative > std::numeric_limits<SimulationTick>::max()
                - simulation_.now()) {
          output_ << "time overflow\n";
          return;
        }
        limit = simulation_.now() + *relative;
      }
      run(limit);
      return;
    }
    if (command[0] == "run-until" && command.size() == 2) {
      const auto limit = command_time(command[1]);
      if (!limit) {
        return;
      }
      if (*limit < simulation_.now()) {
        output_ << "run-until time is before the current time\n";
        return;
      }
      run(*limit);
      return;
    }
    if (command[0] == "step" && command.size() == 2) {
      if (command[1] == "statement") {
        step_execution(false);
      } else if (command[1] == "process") {
        step_execution(true);
      } else if (command[1] == "delta" || command[1] == "time") {
        step(command[1] == "delta");
      } else {
        output_ << "usage: step statement|process|delta|time\n";
        return;
      }
      return;
    }
    output_ << "unknown or malformed command; type help\n";
  }

[[nodiscard]] std::string DebuggerSession::format_value(
    const PackedLogic4& value,
    const std::vector<std::string>& enumeration_literals)  {
    const auto bits = value.to_msb_string();
    if (enumeration_literals.empty()
        || value.width()
            > std::numeric_limits<std::size_t>::digits) {
      return bits;
    }
    std::size_t ordinal = 0;
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
      const auto digit = value.get(bit);
      if (digit == runtime::Logic4::one) {
        ordinal |= std::size_t{1} << bit;
      } else if (digit != runtime::Logic4::zero) {
        return bits;
      }
    }
    if (ordinal >= enumeration_literals.size()) {
      return bits;
    }
    return enumeration_literals[ordinal] + " (" + bits + ")";
  }

[[nodiscard]] bool DebuggerSession::canonical_path(const std::string_view path) const  {
    if (path == simulation_.design().top()) {
      return true;
    }
    const auto prefix = std::string(path) + ".";
    return std::any_of(
        signal_paths_.begin(), signal_paths_.end(),
        [&](const auto& entry) {
          return entry.first.starts_with(prefix);
        })
        || std::any_of(
            container_paths_.begin(), container_paths_.end(),
            [&](const auto& entry) {
              return entry.first.starts_with(prefix);
            });
  }

[[nodiscard]] std::optional<std::string> DebuggerSession::resolve_scope(
    const std::string_view requested) const  {
    if (requested.empty() || requested == ".") {
      return scope_;
    }
    if (requested == "/") {
      return simulation_.design().top();
    }
    if (requested == "..") {
      if (scope_ == simulation_.design().top()) {
        return scope_;
      }
      const auto separator = scope_.rfind('.');
      return separator == std::string::npos
          ? std::string{simulation_.design().top()}
          : scope_.substr(0, separator);
    }
    std::string candidate;
    const auto top = std::string_view{simulation_.design().top()};
    if (requested == top
        || (requested.size() > top.size()
            && requested.starts_with(top)
            && requested[top.size()] == '.')) {
      candidate = requested;
    } else {
      candidate = scope_ + "." + std::string(requested);
    }
    if (!canonical_path(candidate)) {
      return std::nullopt;
    }
    return candidate;
  }

[[nodiscard]] std::optional<std::pair<std::string, SignalId>>
DebuggerSession::resolve_signal(const std::string_view name)  {
    const auto relative = scope_ + "." + std::string(name);
    if (const auto signal = simulation_.find_signal(relative)) {
      return std::pair{relative, *signal};
    }
    if (const auto signal = simulation_.find_signal(name)) {
      const auto found = std::find_if(
          signal_paths_.begin(), signal_paths_.end(),
          [&](const auto& entry) {
            return entry.second == *signal
                && entry.first.starts_with(
                    std::string{simulation_.design().top()} + ".");
          });
      return std::pair{
          found == signal_paths_.end() ? std::string{name} : found->first,
          *signal};
    }
    output_ << "unknown signal: " << name << '\n';
    return std::nullopt;
  }

[[nodiscard]] std::optional<std::pair<
    std::string, runtime::simir::StringObjectId>>
DebuggerSession::resolve_string_object(
    const std::string_view name) const {
  const auto relative = scope_ + "." + std::string{name};
  for (const auto& object : simulation_.design().string_objects()) {
    if (object.name == name || object.name == relative) {
      return std::pair{object.name, object.id};
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::pair<
    std::string, runtime::simir::ContainerObjectId>>
DebuggerSession::resolve_container_object(
    const std::string_view name) const {
  const auto relative = scope_ + "." + std::string{name};
  if (const auto object =
          simulation_.design().find_container(relative)) {
    return std::pair{relative, *object};
  }
  if (const auto object = simulation_.design().find_container(name)) {
    return std::pair{std::string{name}, *object};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<SimulationTick> DebuggerSession::command_time(
    const std::string_view text)  {
    std::string time_error;
    const auto result =
        parse_time(text, simulation_.time_resolution(), time_error);
    if (!result) {
      output_ << time_error << '\n';
    }
    return result;
  }

void DebuggerSession::scope_command(const std::vector<std::string>& command)  {
    if (command.size() == 1) {
      output_ << scope_ << '\n';
      return;
    }
    if (command.size() != 2) {
      output_ << "usage: scope [PATH]\n";
      return;
    }
    const auto resolved = resolve_scope(command[1]);
    if (!resolved) {
      output_ << "unknown scope: " << command[1] << '\n';
      return;
    }
    scope_ = *resolved;
    output_ << "scope " << scope_ << '\n';
  }

void DebuggerSession::scopes_command(const std::vector<std::string>& command)  {
    if (command.size() > 2) {
      output_ << "usage: scopes [PATH]\n";
      return;
    }
    const auto base =
        command.size() == 1 ? std::optional{scope_}
                            : resolve_scope(command[1]);
    if (!base) {
      output_ << "unknown scope: " << command[1] << '\n';
      return;
    }
    const auto prefix = *base + ".";
    std::set<std::string> children;
    for (const auto& [path, signal] : signal_paths_) {
      (void)signal;
      if (!path.starts_with(prefix)) {
        continue;
      }
      const auto remainder = std::string_view{path}.substr(prefix.size());
      if (const auto separator = remainder.find('.');
          separator != std::string_view::npos) {
        children.insert(prefix + std::string(remainder.substr(0, separator)));
      }
    }
    for (const auto& [path, object] : container_paths_) {
      (void)object;
      if (!path.starts_with(prefix)) {
        continue;
      }
      const auto remainder =
          std::string_view{path}.substr(prefix.size());
      if (const auto separator = remainder.find('.');
          separator != std::string_view::npos) {
        children.insert(
            prefix + std::string(remainder.substr(0, separator)));
      }
    }
    if (children.empty()) {
      output_ << "(no child scopes)\n";
      return;
    }
    for (const auto& child : children) {
      output_ << child << '\n';
    }
  }

void DebuggerSession::signals_command(const std::vector<std::string>& command)  {
    if (command.size() > 2) {
      output_ << "usage: signals [PATH]\n";
      return;
    }
    const auto base =
        command.size() == 1 ? std::optional{scope_}
                            : resolve_scope(command[1]);
    if (!base) {
      output_ << "unknown scope: " << command[1] << '\n';
      return;
    }
    const auto prefix = *base + ".";
    bool found = false;
    for (const auto& [path, signal] : signal_paths_) {
      if (!path.starts_with(prefix)) {
        continue;
      }
      found = true;
      const auto& info =
          simulation_.design().signals().at(signal);
      output_ << path << " = "
              << format_value(
                     simulation_.read_signal(signal),
                     info.enumeration_literals);
      if (simulation_.signal_is_forced(signal)) {
        output_ << " (forced)";
      }
      output_ << '\n';
    }
    if (!found) {
      output_ << "(no signals)\n";
    }
  }

void DebuggerSession::modify_signal(const std::vector<std::string>& command)  {
    const auto signal = resolve_signal(command[1]);
    if (!signal) {
      return;
    }
    const auto& info = simulation_.design().signals().at(signal->second);
    std::string value_error;
    auto value = parse_value(command[2], info.width, value_error);
    if (!value) {
      output_ << value_error << '\n';
    } else if (command[0] == "deposit") {
      simulation_.deposit_signal(signal->second, std::move(*value));
    } else {
      simulation_.force_signal(signal->second, std::move(*value));
    }
  }

void DebuggerSession::show_locals()  {
    if (!current_execution_point_) {
      output_ << "no process is selected; stop at a source point first\n";
      return;
    }
    const auto process_id = current_execution_point_->process;
    const auto& process =
        simulation_.design().processes().at(
            current_execution_point_->design_process);
    if (process.debug_locals.empty()
        && process.debug_string_locals.empty()
        && process.debug_container_locals.empty()) {
      output_ << "(no locals)\n";
      return;
    }
    for (std::size_t index = 0; index < process.debug_locals.size();
         ++index) {
      const auto& local = process.debug_locals[index];
      const auto& value =
          simulation_.read_process_local(process_id, index);
      output_ << local.name << " = "
              << format_value(
                     value, local.enumeration_literals)
              << '\n';
    }
    for (std::size_t index = 0;
         index < process.debug_string_locals.size();
         ++index) {
      const auto& local = process.debug_string_locals[index];
      output_ << local.name << " = "
              << escaped_string(
                     simulation_.read_process_string_local(
                         process_id, index))
              << '\n';
    }
    for (std::size_t index = 0;
         index < process.debug_container_locals.size();
         ++index) {
      const auto& local = process.debug_container_locals[index];
      output_ << local.name << " = "
              << format_container(
                     simulation_.read_process_container_local(
                         process_id, index))
              << '\n';
    }
  }

void DebuggerSession::trace_command(const std::vector<std::string>& command)  {
    if (trace_ == nullptr) {
      output_ << "trace output is not configured\n";
      return;
    }
    if (command.size() == 2 && command[1] == "list") {
      bool found = false;
      for (const auto& signal : simulation_.design().signals()) {
        if (!trace_->enabled[signal.id]) {
          continue;
        }
        found = true;
        output_ << signal.name << '\n';
      }
      if (!found) {
        output_ << "(no traced signals)\n";
      }
      return;
    }
    if (command.size() == 2
        && (command[1] == "all" || command[1] == "clear")) {
      const auto enable = command[1] == "all";
      for (const auto& signal : simulation_.design().signals()) {
        set_trace_enabled(signal.id, enable);
      }
      output_
          << (enable ? "tracing all signals\n" : "cleared trace selection\n");
      return;
    }
    if (command.size() == 3
        && (command[1] == "add" || command[1] == "remove")) {
      const auto signal = resolve_signal(command[2]);
      if (!signal) {
        return;
      }
      const auto enable = command[1] == "add";
      set_trace_enabled(signal->second, enable);
      output_
          << (enable ? "tracing " : "stopped tracing ")
          << signal->first << '\n';
      return;
    }
    output_ << "usage: trace add|remove SIGNAL | "
               "trace all|clear|list\n";
  }

void DebuggerSession::set_trace_enabled(const SignalId signal, const bool enable)  {
    if (signal >= trace_->enabled.size()
        || signal >= trace_->handles.size()
        || !trace_->handles[signal]) {
      throw std::logic_error{"debug trace signal is not declared"};
    }
    if (trace_->enabled[signal] == enable) {
      return;
    }
    trace_->enabled[signal] = enable;
    if (!enable) {
      return;
    }
    if (simulation_.now()
        > std::numeric_limits<SimulationTick>::max()
              / trace_->tick_multiplier) {
      throw std::overflow_error{"VCD timestamp scaling overflow"};
    }
    trace_->writer->set_time(
        simulation_.now() * trace_->tick_multiplier);
    trace_->writer->change(
        *trace_->handles[signal], simulation_.read_signal(signal));
  }

void DebuggerSession::add_breakpoint(const std::vector<std::string>& command)  {
    const std::string_view kind = command[1];
    const std::string_view location = command[2];
    DebugBreakpoint breakpoint;
    if (command.size() == 5 && kind != "signal") {
      output_ << "only signal breakpoints accept a condition\n";
      return;
    }
    if (kind == "time") {
      const auto time = command_time(location);
      if (!time) {
        return;
      }
      if (*time <= simulation_.now()) {
        output_ << "time breakpoint must be later than the current time\n";
        return;
      }
      breakpoint.kind = DebugBreakpointKind::time;
      breakpoint.id = next_breakpoint_++;
      breakpoint.time = *time;
      breakpoint.path = std::string(location);
      breakpoints_.push_back(breakpoint);
      output_ << "breakpoint " << breakpoint.id << " set at time "
              << breakpoint.time << " (" << location << ")\n";
      return;
    }
    if (kind == "signal") {
      const auto signal = resolve_signal(location);
      if (!signal) {
        return;
      }
      breakpoint.kind = DebugBreakpointKind::signal;
      breakpoint.signal = signal->second;
      breakpoint.path = std::move(signal->first);
      if (command.size() == 5) {
        if (command[3] != "==" && command[3] != "!=") {
          output_ << "signal breakpoint comparison must be == or !=\n";
          return;
        }
        const auto& info =
            simulation_.design().signals().at(breakpoint.signal);
        std::string value_error;
        auto condition =
            parse_value(command[4], info.width, value_error);
        if (!condition) {
          output_ << value_error << '\n';
          return;
        }
        breakpoint.signal_condition_equal = command[3] == "==";
        breakpoint.signal_condition = std::move(*condition);
      }
      breakpoint.id = next_breakpoint_++;
      breakpoints_.push_back(breakpoint);
      output_ << "breakpoint " << breakpoint.id << " set on "
              << breakpoint.path;
      if (breakpoint.signal_condition) {
        output_ << ' '
                << (breakpoint.signal_condition_equal ? "== " : "!= ")
                << breakpoint.signal_condition->to_msb_string();
      }
      output_ << '\n';
      return;
    }
    if (kind == "source") {
      const auto separator = location.rfind(':');
      const auto line_text =
          separator == std::string_view::npos
              ? location
              : location.substr(separator + 1);
      std::uint64_t line{};
      const auto [end, conversion_error] = std::from_chars(
          line_text.data(), line_text.data() + line_text.size(), line);
      if (conversion_error != std::errc{}
          || end != line_text.data() + line_text.size()
          || line == 0
          || line > std::numeric_limits<std::uint32_t>::max()) {
        output_ << "source breakpoint must be LINE or PATH:LINE\n";
        return;
      }
      breakpoint.kind = DebugBreakpointKind::source;
      breakpoint.id = next_breakpoint_++;
      breakpoint.line = static_cast<std::uint32_t>(line);
      if (separator != std::string_view::npos) {
        breakpoint.path = std::string(location.substr(0, separator));
      }
      breakpoints_.push_back(breakpoint);
      output_ << "breakpoint " << breakpoint.id << " set at ";
      if (!breakpoint.path.empty()) {
        output_ << breakpoint.path << ':';
      }
      output_ << breakpoint.line << '\n';
      return;
    }
    output_ << "usage: break time TIME | "
               "break signal SIGNAL [==|!= VALUE] | "
               "break source [PATH:]LINE\n";
  }

void DebuggerSession::list_breakpoints() const  {
    if (breakpoints_.empty()) {
      output_ << "no breakpoints\n";
      return;
    }
    for (const auto& breakpoint : breakpoints_) {
      output_ << breakpoint.id << ": ";
      if (breakpoint.kind == DebugBreakpointKind::time) {
        output_ << "time " << breakpoint.time << " ticks";
        if (!breakpoint.path.empty()) {
          output_ << " (" << breakpoint.path << ")";
        }
      } else if (breakpoint.kind == DebugBreakpointKind::signal) {
        output_ << "signal " << breakpoint.path;
        if (breakpoint.signal_condition) {
          output_ << ' '
                  << (breakpoint.signal_condition_equal ? "== " : "!= ")
                  << breakpoint.signal_condition->to_msb_string();
        }
      } else {
        output_ << "source ";
        if (!breakpoint.path.empty()) {
          output_ << breakpoint.path << ':';
        }
        output_ << breakpoint.line;
      }
      output_ << '\n';
    }
  }

void DebuggerSession::delete_breakpoint(const std::string_view id_text)  {
    std::uint64_t id{};
    const auto [end, conversion_error] = std::from_chars(
        id_text.data(), id_text.data() + id_text.size(), id);
    if (conversion_error != std::errc{}
        || end != id_text.data() + id_text.size()) {
      output_ << "breakpoint ID must be an unsigned integer\n";
      return;
    }
    const auto found = std::find_if(
        breakpoints_.begin(), breakpoints_.end(),
        [id](const DebugBreakpoint& breakpoint) {
          return breakpoint.id == id;
        });
    if (found == breakpoints_.end()) {
      output_ << "unknown breakpoint: " << id << '\n';
      return;
    }
    breakpoints_.erase(found);
    output_ << "deleted breakpoint " << id << '\n';
  }

[[nodiscard]] std::optional<DebugBreakpoint> DebuggerSession::earliest_time_breakpoint(
    const SimulationTick start,
    const std::optional<SimulationTick> requested_limit) const  {
    std::optional<DebugBreakpoint> result;
    for (const auto& breakpoint : breakpoints_) {
      if (breakpoint.kind != DebugBreakpointKind::time
          || breakpoint.time <= start
          || (requested_limit && breakpoint.time > *requested_limit)) {
        continue;
      }
      if (!result || breakpoint.time < result->time
          || (breakpoint.time == result->time
              && breakpoint.id < result->id)) {
        result = breakpoint;
      }
    }
    return result;
  }

[[nodiscard]] bool DebuggerSession::source_path_matches(
    const std::string_view requested,
    const std::string_view actual)  {
    if (requested.empty() || requested == actual) {
      return true;
    }
    return std::filesystem::path{actual}.filename()
        == std::filesystem::path{requested}.filename();
  }

[[nodiscard]] bool DebuggerSession::is_statement_point(
    const runtime::simir::ExecutionPointKind kind) noexcept  {
    return kind == runtime::simir::ExecutionPointKind::statement
        || kind == runtime::simir::ExecutionPointKind::call
        || kind == runtime::simir::ExecutionPointKind::wait
        || kind == runtime::simir::ExecutionPointKind::assertion;
  }

void DebuggerSession::install_execution_hook(
    const std::optional<DebugBreakpoint>& time_breakpoint,
    const std::function<bool(runtime::Scheduler&, runtime::SchedulerPhase)>&
        additional_stop,
    const std::function<bool(const runtime::simir::ExecutionPoint&)>&
        additional_execution_stop)  {
    simulation_.set_safe_point_hook(
        [this, time_breakpoint, additional_stop](
            runtime::Scheduler& scheduler,
            const runtime::SchedulerPhase phase) {
          if (interrupt_requested.exchange(false, std::memory_order_relaxed)) {
            scheduler.request_stop();
            return;
          }
          if (!hit_ && time_breakpoint
              && scheduler.now() >= time_breakpoint->time) {
            hit_ = DebugBreakpointHit{
                time_breakpoint->id,
                "time " + std::to_string(time_breakpoint->time)};
            scheduler.request_stop();
            return;
          }
          if (additional_stop && additional_stop(scheduler, phase)) {
            scheduler.request_stop();
          }
        });
    simulation_.set_execution_point_hook(
        [this, additional_execution_stop](
            runtime::Scheduler& scheduler,
            const runtime::simir::ExecutionPoint& point) {
          if (interrupt_requested.exchange(false, std::memory_order_relaxed)) {
            current_execution_point_ = point;
            scheduler.request_stop();
            return;
          }
          if (!hit_ && is_statement_point(point.kind)) {
            const auto found = std::find_if(
                breakpoints_.begin(), breakpoints_.end(),
                [&](const DebugBreakpoint& breakpoint) {
                  return breakpoint.kind == DebugBreakpointKind::source
                      && breakpoint.line == point.source.line
                      && source_path_matches(
                          breakpoint.path, point.source.path);
                });
            if (found != breakpoints_.end()) {
              hit_ = DebugBreakpointHit{
                  found->id,
                  point.source.path + ":"
                      + std::to_string(point.source.line) + ":"
                      + std::to_string(point.source.column)};
              current_execution_point_ = point;
              scheduler.request_stop();
              return;
            }
          }
          if (additional_execution_stop
              && additional_execution_stop(point)) {
            current_execution_point_ = point;
            scheduler.request_stop();
          }
        });
  }

void DebuggerSession::report_execution_point()  {
    if (!current_execution_point_) {
      return;
    }
    const auto& point = *current_execution_point_;
    output_ << "process "
            << simulation_.design().processes().at(
                   point.design_process).name
            << " at " << point.source.path << ':' << point.source.line
            << ':' << point.source.column << '\n';
  }

void DebuggerSession::report_result(const runtime::RunResult& result)  {
    if (hit_) {
      output_ << "hit breakpoint " << hit_->id << ": "
              << hit_->description << '\n';
    }
    report_execution_point();
    output_
        << (simulation_.finished() ? "simulation finished" : "stopped")
        << " at time " << result.time << ", delta " << result.delta << '\n';
  }

[[nodiscard]] bool DebuggerSession::can_execute()  {
    if (simulation_.poisoned()) {
      output_
          << "simulation is unavailable after a fatal runtime error\n";
      return false;
    }
    if (simulation_.finished()) {
      output_ << "simulation has finished\n";
      return false;
    }
    return true;
  }

void DebuggerSession::run(const std::optional<SimulationTick> requested_limit)  {
    if (!can_execute()) {
      return;
    }
    const auto time_breakpoint =
        earliest_time_breakpoint(simulation_.now(), requested_limit);
    auto effective_limit = requested_limit;
    if (time_breakpoint
        && (!effective_limit || time_breakpoint->time < *effective_limit)) {
      effective_limit = time_breakpoint->time;
    }
    hit_.reset();
    current_execution_point_.reset();
    install_execution_hook(time_breakpoint);
    try {
      simulation_.clear_stop();
      executing_ = true;
      ExecutionGuard guard{executing_};
      const auto result = simulation_.run(effective_limit);
      if (!hit_ && time_breakpoint
          && result.time >= time_breakpoint->time) {
        hit_ = DebugBreakpointHit{
            time_breakpoint->id,
            "time " + std::to_string(time_breakpoint->time)};
      }
      report_result(result);
    } catch (const std::exception& exception) {
      error_ << exception.what() << '\n';
    }
    install_interrupt_hook(simulation_);
  }

void DebuggerSession::step(const bool delta_step)  {
    if (!can_execute()) {
      return;
    }
    const auto start_time = simulation_.now();
    const auto time_breakpoint =
        earliest_time_breakpoint(start_time, std::nullopt);
    hit_.reset();
    current_execution_point_.reset();
    install_execution_hook(
        time_breakpoint,
        [start_time, delta_step](
            runtime::Scheduler& scheduler,
            const runtime::SchedulerPhase phase) {
          return (delta_step
                  && phase == runtime::SchedulerPhase::postponed)
              || (!delta_step && scheduler.now() > start_time);
        });
    try {
      simulation_.clear_stop();
      executing_ = true;
      ExecutionGuard guard{executing_};
      const auto result = simulation_.run(
          time_breakpoint
              ? std::optional<SimulationTick>{time_breakpoint->time}
              : std::nullopt);
      if (!hit_ && time_breakpoint
          && result.time >= time_breakpoint->time) {
        hit_ = DebugBreakpointHit{
            time_breakpoint->id,
            "time " + std::to_string(time_breakpoint->time)};
      }
      report_result(result);
    } catch (const std::exception& exception) {
      error_ << exception.what() << '\n';
    }
    install_interrupt_hook(simulation_);
  }

void DebuggerSession::step_execution(const bool process_step)  {
    if (!can_execute()) {
      return;
    }
    auto selected_process =
        std::make_shared<std::optional<runtime::simir::ProcessId>>(
            current_execution_point_
                ? std::optional{current_execution_point_->process}
                : std::nullopt);
    hit_.reset();
    current_execution_point_.reset();
    install_execution_hook(
        std::nullopt,
        {},
        [process_step, selected_process](
            const runtime::simir::ExecutionPoint& point) {
          if (!process_step) {
            if (!is_statement_point(point.kind)) {
              return false;
            }
            if (!*selected_process) {
              *selected_process = point.process;
              return true;
            }
            return point.process == **selected_process;
          }
          if (!*selected_process) {
            if (point.kind
                == runtime::simir::ExecutionPointKind::process_entry) {
              *selected_process = point.process;
            }
            return false;
          }
          return point.process == **selected_process
              && point.kind
                  == runtime::simir::ExecutionPointKind::process_suspend;
        });
    try {
      simulation_.clear_stop();
      executing_ = true;
      ExecutionGuard guard{executing_};
      const auto result = simulation_.run();
      report_result(result);
    } catch (const std::exception& exception) {
      error_ << exception.what() << '\n';
    }
    install_interrupt_hook(simulation_);
  }

std::vector<std::string> words(const std::string& line)  {
  std::istringstream input(line);
  std::vector<std::string> result;
  for (std::string word; input >> word;) {
    result.push_back(std::move(word));
  }
  return result;
}

int run_debug_repl_impl(
    Simulation& simulation,
    std::istream& input,
    std::ostream& output,
    std::ostream& error,
    TraceState* trace)  {
  DebuggerSession debugger(simulation, output, error, trace);
  std::string line;
  while (true) {
    output << "(fsim) " << std::flush;
    if (!std::getline(input, line)) {
      output << '\n';
      break;
    }
    const auto command = words(line);
    if (command.empty()) {
      continue;
    }
    if (command[0] == "quit" || command[0] == "q") {
      break;
    }
    if (command[0] == "help") {
      print_debug_help(output);
      continue;
    }
    debugger.execute(command);
  }
  return 0;
}

int handle_debug(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::istream& input,
    std::ostream& output,
    std::ostream& error_output)  {
  auto built = build_project(config, diagnostics);
  if (!built) {
    return 1;
  }
  if (built->entropy_seed) {
    output << "random seed " << built->seed << '\n';
  }
  Simulation simulation(
      std::move(*built),
      config.run.max_deltas,
      SimulationEngine::debug);
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
  auto trace = attach_trace(simulation, config, diagnostics, true);
  if (config.run.trace_file && !trace) {
    return 1;
  }
  install_interrupt_hook(simulation);
  const InterruptSignalGuard interrupt_signal;
  simulation.start();
  output << "fsim debugger: " << simulation.design().top();
  if (simulation.compiled_process_count() == 0) {
    output << " (reference evaluator)\n";
  } else {
    output << " (O0 hybrid, "
           << simulation.compiled_process_count()
           << " compiled process(es) in "
           << simulation.compiled_module_count()
           << " specialization module(s))\n";
  }
  print_debug_help(output);
  const auto status = run_debug_repl_impl(
      simulation, input, output, error_output, trace.get());
  if (trace) {
    trace->writer->flush();
  }
  return status;
}

} // namespace fsim::app::application_detail
