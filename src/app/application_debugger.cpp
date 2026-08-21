// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_trace_control.hpp"
#include "fsim/support/path.hpp"

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
    const runtime::simir::ContainerValue& value);

[[nodiscard]] std::string format_container_element(
    const runtime::simir::ContainerValue& value,
    const std::size_t index) {
  using runtime::simir::ContainerElementKind;
  switch (value.type.element_kind) {
  case ContainerElementKind::Packed:
    return value.elements[index].to_msb_string();
  case ContainerElementKind::Scalar: {
    const auto scalar = runtime::decode_systemverilog_scalar_payload(
        value.elements[index], value.type.scalar_kind);
    if (!scalar) return "<invalid scalar>";
    if (scalar.value.kind == runtime::SystemVerilogScalarKind::Chandle) {
      return scalar.value.bits == 0
          ? "null"
          : "chandle(" + std::to_string(scalar.value.bits) + ")";
    }
    const auto formatted = runtime::format_systemverilog_scalar(
        scalar.value);
    return formatted ? formatted.text : "<invalid scalar>";
  }
  case ContainerElementKind::String:
    return escaped_string(value.string_elements[index]);
  case ContainerElementKind::Container:
  case ContainerElementKind::Aggregate:
    return format_container(value.nested_elements[index]);
  }
  return "<invalid element>";
}

[[nodiscard]] std::string format_container(
    const runtime::simir::ContainerValue& value) {
  const bool aggregate_value =
      value.type.element_kind
          == runtime::simir::ContainerElementKind::Aggregate
      && value.type.aggregate_value;
  if (aggregate_value) {
    std::string result{"{"};
    for (std::size_t index = 0;
         index < value.nested_elements.size(); ++index) {
      if (index != 0) result += ", ";
      if (index < value.type.member_names.size()) {
        result += value.type.member_names[index] + "=";
      }
      const auto& member = value.nested_elements[index];
      result += member.type.fixed
              && runtime::simir::container_value_size(member) == 1
          ? format_container_element(member, 0)
          : format_container(member);
    }
    result += "}";
    return result;
  }
  std::string result{"["};
  const auto size = runtime::simir::container_value_size(value);
  for (std::size_t index = 0; index < size; ++index) {
    if (index != 0) {
      result += ", ";
    }
    if (value.type.associative) {
      result += value.type.string_indices
          ? escaped_string(value.string_keys[index])
          : value.keys[index].to_msb_string();
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
    result += format_container_element(value, index);
  }
  result += "]";
  return result;
}

[[nodiscard]] std::string format_class_property(
    const runtime::SystemVerilogClassPropertyValue& value) {
  if (value.packed.width() != 0) return value.packed.to_msb_string();
  if (value.kind == runtime::SystemVerilogClassPropertyKind::String) {
    return escaped_string(value.string);
  }
  if (value.kind == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
    return value.handle == 0 ? "null" : std::to_string(value.handle);
  }
  if (value.handle_container) {
    return "<class-handle-container size="
        + std::to_string(value.handle_container->size()) + ">";
  }
  return "<uninitialized>";
}

[[nodiscard]] std::optional<std::string> class_local_declaration(
    const Simulation& simulation,
    const std::string_view type_name) {
  const auto type = std::ranges::find_if(
      simulation.class_specializations(), [&](const auto& specialization) {
        return specialization.declaration_identity == type_name
            || specialization.declaration_identity.ends_with(
                "::" + std::string{type_name});
      });
  if (type == simulation.class_specializations().end()) return std::nullopt;
  return type->declaration_identity;
}

[[nodiscard]] std::optional<std::string> format_class_local(
    const Simulation& simulation,
    const std::string_view type_name,
    const runtime::PackedLogic4& value) {
  const auto declared = class_local_declaration(simulation, type_name);
  if (!declared) return std::nullopt;
  const auto word = value.low_word();
  if (word.bval != 0) return "<unknown class handle>";
  if (word.aval == 0) return "null declared " + *declared;
  if (!simulation.class_heap().contains(word.aval)) {
    return "stale handle " + std::to_string(word.aval);
  }
  const auto& object = simulation.class_heap().object(word.aval);
  return "handle " + std::to_string(word.aval) + " declared "
      + *declared + " dynamic " + object.dynamic_type;
}

[[nodiscard]] std::string format_scalar_local(
    const Simulation& simulation,
    const runtime::SystemVerilogScalarValue& value) {
  if (value.kind == runtime::SystemVerilogScalarKind::Chandle) {
    return simulation.chandle_registry().format(value.bits);
  }
  const auto formatted = runtime::format_systemverilog_scalar(value);
  return formatted ? formatted.text : "<invalid scalar>";
}

[[nodiscard]] std::vector<std::pair<std::string, SignalId>>
design_signal_paths(const Simulation& simulation) {
  std::vector<std::pair<std::string, SignalId>> result;
  for (const auto& object : simulation.design_ir().objects()) {
    if (design_object_is_signal_bearing(object)
        && object.runtime_index <= std::numeric_limits<SignalId>::max()) {
      result.emplace_back(
          object.path, static_cast<SignalId>(object.runtime_index));
    }
  }
  std::ranges::sort(result);
  return result;
}

[[nodiscard]] std::vector<std::pair<
    std::string, runtime::simir::ContainerObjectId>>
design_container_paths(const Simulation& simulation) {
  std::vector<std::pair<
      std::string, runtime::simir::ContainerObjectId>> result;
  for (const auto& object : simulation.design_ir().objects()) {
    if (object.kind == semantic::design::ObjectKind::container
        && object.runtime_index
            <= std::numeric_limits<runtime::simir::ContainerObjectId>::max()) {
      result.emplace_back(
          object.path,
          static_cast<runtime::simir::ContainerObjectId>(
              object.runtime_index));
    }
  }
  std::ranges::sort(result);
  return result;
}

[[nodiscard]] const semantic::design::Object* design_signal_object(
    const Simulation& simulation, const SignalId signal) noexcept {
  const auto& objects = simulation.design_ir().objects();
  const auto found = std::ranges::find_if(
      objects, [&](const semantic::design::Object& object) {
        return design_object_is_signal_bearing(object)
            && !object.parent_object && object.runtime_index == signal;
      });
  return found == objects.end() ? nullptr : &*found;
}

[[nodiscard]] const semantic::design::ProcessOccurrence*
design_process_occurrence(
    const Simulation& simulation,
    const runtime::simir::ProcessId process) noexcept {
  const auto& processes = simulation.design_ir().processes();
  const auto found = std::ranges::find_if(
      processes, [&](const semantic::design::ProcessOccurrence& candidate) {
        return candidate.runtime_index == process;
      });
  return found == processes.end() ? nullptr : &*found;
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
       scope_(simulation.design_ir().top()),
       signal_paths_(design_signal_paths(simulation)),
       container_paths_(design_container_paths(simulation))  {
    const auto retain_scope = [&](const std::string_view path) {
      auto candidate = std::string{path};
      while (!candidate.empty()) {
        if (std::ranges::find(execution_scope_paths_, candidate)
            == execution_scope_paths_.end()) {
          execution_scope_paths_.push_back(candidate);
        }
        if (std::ranges::find(
                simulation.design_ir().roots(), candidate)
            != simulation.design_ir().roots().end()) {
          break;
        }
        const auto separator = candidate.rfind('.');
        if (separator == std::string::npos) {
          break;
        }
        candidate.resize(separator);
      }
    };
    for (const auto& process : simulation.design_ir().processes()) {
      retain_scope(process.name);
      const auto& runtime_process =
          simulation.process_program(process.runtime_index);
      for (std::size_t instruction = 0;
           instruction < runtime_process.operations.size();
           ++instruction) {
        const auto& operation = runtime_process.operations[instruction];
        if (const auto* point =
                runtime::simir::operation_get_if<
                    runtime::simir::DebugPoint>(&operation)) {
          const auto& actual = runtime_process.operations.debug_point(
              instruction, *point);
          retain_scope(runtime_process.operations.debug_scope(actual.scope));
        }
      }
    }
    for (const auto& object : simulation.design_ir().objects()) {
      if (object.kind >= semantic::design::ObjectKind::systemc_module) {
        retain_scope(object.path);
      }
    }
    std::ranges::sort(execution_scope_paths_);
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
    uvm_observer_ = simulation_.add_uvm_activity_hook(
        [this](const runtime::SystemVerilogUvmActivityEvent& event) {
          if (!executing_ || hit_) {
            return;
          }
          const auto found = std::find_if(
              breakpoints_.begin(), breakpoints_.end(),
              [&](const DebugBreakpoint& breakpoint) {
                return breakpoint.kind == DebugBreakpointKind::uvm
                    && (breakpoint.path == "*"
                        || breakpoint.path == event.identity);
              });
          if (found == breakpoints_.end()) {
            return;
          }
          hit_ = DebugBreakpointHit{
              found->id,
              "UVM activity " + event.identity + " action "
                  + std::to_string(static_cast<unsigned>(event.action))
                  + " at time " + std::to_string(event.time) + ", delta "
                  + std::to_string(event.delta)};
          simulation_.request_stop();
        });
  }

DebuggerSession::~DebuggerSession()  {
    simulation_.remove_signal_change_hook(observer_);
    simulation_.remove_uvm_activity_hook(uvm_observer_);
    simulation_.set_execution_point_hook({});
    install_interrupt_hook(simulation_);
  }

void DebuggerSession::execute(const std::vector<std::string>& command)  {
    if (command[0] == "where") {
      output_ << "time " << simulation_.now() << ", delta "
              << simulation_.delta() << ", scope " << scope_ << '\n';
      return;
    }
    if (command[0] == "provenance") {
      if (command.size() > 2) {
        output_ << "usage: provenance [PATH]\n";
        return;
      }
      const auto show = [&](const VerilogScopeProvenance& provenance) {
        output_ << provenance.path << " unit " << provenance.library << ':'
                << provenance.unit_name << " source "
                << provenance.source_path << ':' << provenance.source_line
                << ':' << provenance.source_column << " language "
                << (provenance.language == semantic::Language::verilog
                        ? "verilog" : "systemverilog")
                << " standard " << provenance.standard << " profile "
                << provenance.compatibility_profile << '\n';
      };
      if (command.size() == 2) {
        if (const auto provenance
            = simulation_.verilog_scope_provenance(command[1])) {
          show(*provenance);
        } else {
          output_ << "(no Verilog/SystemVerilog provenance for "
                  << command[1] << ")\n";
        }
        return;
      }
      const auto provenance = simulation_.verilog_scope_provenance();
      if (provenance.empty()) {
        output_ << "(no Verilog/SystemVerilog provenance)\n";
      }
      for (const auto& item : provenance) {
        show(item);
      }
      return;
    }
    if (command[0] == "locals" && command.size() == 1) {
      show_locals();
      return;
    }
    if (command[0] == "classes" && command.size() == 1) {
      const auto handles = simulation_.class_heap().live_handles();
      if (handles.empty()) {
        output_ << "(no class objects)\n";
      }
      for (const auto handle : handles) {
        const auto& object = simulation_.class_heap().object(handle);
        output_ << "handle " << handle
                << " declared " << object.declared_type
                << " dynamic " << object.dynamic_type
                << " specialization " << object.specialization_identity
                << '\n';
      }
      return;
    }
    if (command[0] == "chandles" && command.size() == 1) {
      const auto values = simulation_.chandle_registry().snapshots();
      if (values.empty()) output_ << "(no live chandles)\n";
      for (const auto& value : values) {
        output_ << simulation_.chandle_registry().format(value.handle)
                << " aliases " << value.alias_transfers << '\n';
      }
      return;
    }
    if (command[0] == "uvm") {
      uvm_command(command);
      return;
    }
    if (command[0] == "vhdl") {
      vhdl_command(command);
      return;
    }
    if (command[0] == "chandle" && command.size() == 2) {
      std::uint64_t handle{};
      const auto converted = std::from_chars(
          command[1].data(), command[1].data() + command[1].size(), handle);
      if (converted.ec != std::errc{}
          || converted.ptr != command[1].data() + command[1].size()) {
        output_ << "usage: chandle HANDLE\n";
      } else {
        output_ << simulation_.chandle_registry().format(handle) << '\n';
      }
      return;
    }
    if (command[0] == "class" && command.size() == 2
        && command[1] == "statics") {
      for (const auto& state : simulation_.class_static_store().snapshots()) {
        output_ << state.specialization_identity << '\n';
        for (std::size_t index = 0; index < state.properties.size(); ++index) {
          output_ << "  " << state.property_names[index] << " = "
                  << format_class_property(state.properties[index]) << '\n';
        }
      }
      return;
    }
    if (command[0] == "class" && command.size() == 2
        && command[1] == "frames") {
      const auto frames = simulation_.class_methods().pending_invocations();
      if (frames.empty()) output_ << "(no suspended class calls)\n";
      for (const auto& frame : frames) {
        output_ << "continuation " << frame.continuation << " method "
                << frame.canonical_method << " this " << frame.this_handle
                << " point " << frame.continuation_point << '\n';
        for (std::size_t index = 0; index < frame.arguments.size(); ++index) {
          output_ << "  argument[" << index << "] = "
                  << format_class_property(frame.arguments[index]) << '\n';
        }
        for (std::size_t index = 0; index < frame.locals.size(); ++index) {
          output_ << "  local[" << index << "] = "
                  << format_class_property(frame.locals[index]) << '\n';
        }
      }
      return;
    }
    if (command[0] == "class"
        && (command.size() == 3 || command.size() == 4)
        && command[1] == "static") {
      const auto snapshots = simulation_.class_static_store().snapshots();
      const auto state = std::ranges::find(
          snapshots, command[2],
          &runtime::SystemVerilogClassStaticSnapshot::specialization_identity);
      if (state == snapshots.end()) {
        output_ << "unknown class static specialization: " << command[2]
                << '\n';
        return;
      }
      for (std::size_t index = 0; index < state->properties.size(); ++index) {
        if (command.size() == 4 && state->property_names[index] != command[3]) {
          continue;
        }
        output_ << state->property_names[index] << " = "
                << format_class_property(state->properties[index]) << '\n';
      }
      return;
    }
    if (command[0] == "class"
        && (command.size() == 2 || command.size() == 3)) {
      std::uint64_t handle{};
      const auto converted = std::from_chars(
          command[1].data(), command[1].data() + command[1].size(), handle);
      if (converted.ec != std::errc{}
          || converted.ptr != command[1].data() + command[1].size()) {
        output_ << "usage: class HANDLE [PROPERTY]\n";
        return;
      }
      try {
        const auto& object = simulation_.class_heap().object(handle);
        if (command.size() == 3) {
          output_ << command[2] << " = "
                  << format_class_property(
                         simulation_.read_class_property(handle, command[2]))
                  << '\n';
        } else {
          output_ << "declared " << object.declared_type
                  << " dynamic " << object.dynamic_type
                  << " specialization " << object.specialization_identity
                  << '\n';
          for (std::size_t index = 0;
               index < object.properties.size(); ++index) {
            output_ << "  " << object.property_names[index] << " = "
                    << format_class_property(object.properties[index]) << '\n';
            const auto& random = object.properties[index].random_state;
            if (random) {
              output_ << "    "
                      << (random->kind
                                  == runtime::SystemVerilogClassRandomKind::Randc
                              ? "randc" : "rand")
                      << " enabled " << random->enabled
                      << " revision " << random->revision
                      << " stream " << random->stream_seed
                      << " domain " << random->randc_domain_signature
                      << " cycle " << random->randc_cycle
                      << " used " << random->randc_used_values.size() << '\n';
            }
          }
          for (const auto& [identity, enabled] : object.constraint_modes) {
            output_ << "  constraint " << identity
                    << " enabled " << enabled << '\n';
          }
        }
      } catch (const std::exception& exception) {
        output_ << exception.what() << '\n';
      }
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
            simulation_.runtime_adapter().signals().at(signal->second);
        const auto& value = simulation_.read_signal(signal->second);
        output_ << signal->first << " = ";
        if (info.systemverilog_scalar
            != runtime::SystemVerilogScalarKind::None) {
          output_ << format_scalar_local(
              simulation_, simulation_.read_scalar_signal(signal->second));
        } else {
        const auto* object = design_signal_object(
            simulation_, signal->second);
        if (object != nullptr) {
          if (const auto handle = format_class_local(
                  simulation_, object->type.spelling, value)) {
            output_ << *handle;
          } else {
            output_ << format_value(value, info.enumeration_literals);
          }
        } else {
          output_ << format_value(value, info.enumeration_literals);
        }
        }
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
      } else if (command[1] == "phase") {
        step_phase();
      } else if (command[1] == "delta" || command[1] == "time") {
        step(command[1] == "delta");
      } else {
        output_ << "usage: step statement|process|phase|delta|time\n";
        return;
      }
      return;
    }
    output_ << "unknown or malformed command; type help\n";
  }

[[nodiscard]] std::string DebuggerSession::format_value(
    const PackedLogic4& value,
    const std::vector<std::string>& enumeration_literals)  {
    if (value.width() == 0) {
      return "<null>";
    }
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
    if (std::ranges::find(simulation_.design_ir().roots(), path)
        != simulation_.design_ir().roots().end()) {
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
            })
        || std::ranges::find(execution_scope_paths_, path)
            != execution_scope_paths_.end();
  }

[[nodiscard]] std::vector<std::string> DebuggerSession::lexical_paths(
    const std::string_view name) const {
  std::vector<std::string> paths;
  auto scope = scope_;
  const auto root = std::ranges::find_if(
      simulation_.design_ir().roots(),
      [&](const auto& candidate) {
        return scope == candidate
            || (scope.size() > candidate.size()
                && scope.starts_with(candidate)
                && scope[candidate.size()] == '.');
      });
  const auto& top = root == simulation_.design_ir().roots().end()
      ? simulation_.design_ir().top()
      : *root;
  while (true) {
    paths.push_back(scope + "." + std::string{name});
    if (scope == top) {
      break;
    }
    const auto separator = scope.rfind('.');
    if (separator == std::string::npos) {
      break;
    }
    scope.resize(separator);
  }
  if (std::ranges::find(paths, name) == paths.end()) {
    paths.emplace_back(name);
  }
  return paths;
}

[[nodiscard]] std::optional<std::string> DebuggerSession::resolve_scope(
    const std::string_view requested) const  {
    if (requested.empty() || requested == ".") {
      return scope_;
    }
    if (requested == "/") {
      return simulation_.design_ir().top();
    }
    if (requested == "..") {
      if (std::ranges::find(simulation_.design_ir().roots(), scope_)
          != simulation_.design_ir().roots().end()) {
        return scope_;
      }
      const auto separator = scope_.rfind('.');
      return separator == std::string::npos
          ? std::string{simulation_.design_ir().top()}
          : scope_.substr(0, separator);
    }
    std::string candidate;
    const auto absolute_root = std::ranges::find_if(
        simulation_.design_ir().roots(),
        [&](const auto& root) {
          return requested == root
              || (requested.size() > root.size()
                  && requested.starts_with(root)
                  && requested[root.size()] == '.');
        });
    if (absolute_root != simulation_.design_ir().roots().end()) {
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
    for (const auto& path : lexical_paths(name)) {
      if (const auto signal = simulation_.find_signal(path)) {
        return std::pair{path, *signal};
      }
    }
    if (const auto signal = simulation_.find_signal(name)) {
      const auto found = std::find_if(
          signal_paths_.begin(), signal_paths_.end(),
          [&](const auto& entry) {
            return entry.second == *signal
                && std::ranges::any_of(
                    simulation_.design_ir().roots(),
                    [&](const auto& root) {
                      return entry.first == root
                          || entry.first.starts_with(root + ".");
                    });
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
  for (const auto& path : lexical_paths(name)) {
    for (const auto& object : simulation_.design_ir().objects()) {
      if (object.kind == semantic::design::ObjectKind::string
          && object.path == path
          && object.runtime_index
              <= std::numeric_limits<runtime::simir::StringObjectId>::max()) {
        return std::pair{
            object.path,
            static_cast<runtime::simir::StringObjectId>(
                object.runtime_index)};
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::pair<
    std::string, runtime::simir::ContainerObjectId>>
  DebuggerSession::resolve_container_object(
    const std::string_view name) const {
  for (const auto& path : lexical_paths(name)) {
    const auto object = std::ranges::find_if(
        simulation_.design_ir().objects(), [&](const auto& candidate) {
          return candidate.kind == semantic::design::ObjectKind::container
              && candidate.path == path
              && candidate.runtime_index <= std::numeric_limits<
                  runtime::simir::ContainerObjectId>::max();
        });
    if (object != simulation_.design_ir().objects().end()) {
      return std::pair{
          path,
          static_cast<runtime::simir::ContainerObjectId>(
              object->runtime_index)};
    }
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
    for (const auto& path : execution_scope_paths_) {
      if (!path.starts_with(prefix)) {
        continue;
      }
      const auto remainder = std::string_view{path}.substr(prefix.size());
      const auto separator = remainder.find('.');
      children.insert(
          prefix
          + std::string(remainder.substr(0, separator)));
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
          simulation_.runtime_adapter().signals().at(signal);
      output_ << path << " = ";
      if (info.systemverilog_scalar
          != runtime::SystemVerilogScalarKind::None) {
        output_ << format_scalar_local(
            simulation_, simulation_.read_scalar_signal(signal));
      } else {
        output_ << format_value(
            simulation_.read_signal(signal), info.enumeration_literals);
      }
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
    const auto* info = design_signal_object(simulation_, signal->second);
    if (info == nullptr) {
      throw std::logic_error{"signal lacks its DesignIR runtime adapter"};
    }
    const auto& runtime_info =
        simulation_.runtime_adapter().signals().at(signal->second);
    if (runtime_info.systemverilog_scalar
        != runtime::SystemVerilogScalarKind::None) {
      if (runtime_info.systemverilog_scalar
          == runtime::SystemVerilogScalarKind::Chandle) {
        runtime::SystemVerilogChandle handle{};
        if (command[2] != "null") {
          const auto converted = std::from_chars(
              command[2].data(), command[2].data() + command[2].size(),
              handle);
          if (converted.ec != std::errc{}
              || converted.ptr != command[2].data() + command[2].size()
              || !simulation_.chandle_registry().contains(handle)) {
            output_ << "invalid or stale chandle value\n";
            return;
          }
        }
        const auto value = runtime::SystemVerilogScalarValue::chandle(handle);
        if (command[0] == "deposit") {
          simulation_.deposit_scalar_signal(signal->second, value);
        } else {
          simulation_.force_scalar_signal(signal->second, value);
        }
        return;
      }
      const auto value = runtime::scan_systemverilog_scalar(
          command[2], runtime_info.systemverilog_scalar);
      if (!value) {
        output_ << "invalid scalar value\n";
      } else if (command[0] == "deposit") {
        simulation_.deposit_scalar_signal(signal->second, value.value);
      } else {
        simulation_.force_scalar_signal(signal->second, value.value);
      }
      return;
    }
    std::string value_error;
    auto value = parse_value(command[2], info->width, value_error);
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
        simulation_.process_program(
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
      output_ << local.name << " = ";
      try {
        if (local.systemverilog_scalar
            != runtime::SystemVerilogScalarKind::None) {
          output_ << format_scalar_local(
              simulation_,
              simulation_.read_process_scalar_local(process_id, index));
          output_ << '\n';
          continue;
        }
        const auto value = simulation_.read_process_local(process_id, index);
        if (const auto handle = format_class_local(
                simulation_, local.type_name, value)) {
          output_ << *handle;
        } else {
          output_ << format_value(value, local.enumeration_literals);
        }
      } catch (const std::logic_error&) {
        if (const auto declared = class_local_declaration(
                simulation_, local.type_name)) {
          output_ << "<uninitialized class handle> declared " << *declared;
        } else {
          output_ << "<uninitialized>";
        }
      }
      output_ << '\n';
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

std::vector<std::pair<std::string, runtime::SystemVerilogUvmPhaseState>>
DebuggerSession::phase_states() const {
  std::vector<std::pair<
      std::string, runtime::SystemVerilogUvmPhaseState>> result;
  for (const auto& domain : simulation_.uvm_phases().domains()) {
    const auto domain_snapshot = simulation_.uvm_phases().snapshot(domain);
    for (const auto& phase : simulation_.uvm_phases().phases(domain)) {
      const auto snapshot = simulation_.uvm_phases().snapshot(phase);
      result.emplace_back(
          domain_snapshot.identity + "." + snapshot.identity,
          snapshot.state);
    }
  }
  return result;
}

void DebuggerSession::uvm_command(
    const std::vector<std::string>& command) {
  if (command.size() > 2) {
    output_ << "usage: uvm [summary|phases|objections|tlm1|tlm2|callbacks|transactions|sequences|registers|all]\n";
    return;
  }
  auto section = UvmDebugSection::summary;
  if (command.size() == 2) {
    if (command[1] == "phases") {
      section = UvmDebugSection::phases;
    } else if (command[1] == "objections") {
      section = UvmDebugSection::objections;
    } else if (command[1] == "tlm1") {
      section = UvmDebugSection::tlm1;
    } else if (command[1] == "tlm2") {
      section = UvmDebugSection::tlm2;
    } else if (command[1] == "callbacks") {
      section = UvmDebugSection::callbacks;
    } else if (command[1] == "transactions") {
      section = UvmDebugSection::transactions;
    } else if (command[1] == "sequences") {
      section = UvmDebugSection::sequences;
    } else if (command[1] == "configuration") {
      section = UvmDebugSection::configuration;
    } else if (command[1] == "registers") {
      section = UvmDebugSection::register_model;
    } else if (command[1] == "all") {
      section = UvmDebugSection::all;
    } else if (command[1] != "summary") {
      output_ << "usage: uvm [summary|phases|objections|tlm1|tlm2|callbacks|transactions|sequences|configuration|registers|all]\n";
      return;
    }
  }
  try {
    const auto limits = UvmDebugLimits{};
    output_ << format_uvm_debug_snapshot(
        simulation_.uvm_debug_snapshot(limits), section,
        limits.maximum_formatted_bytes);
  } catch (const UvmDebugError& error) {
    error_ << error.diagnostic_code() << ": " << error.what() << '\n';
  }
}

void DebuggerSession::vhdl_command(
    const std::vector<std::string>& command) {
  if (command.size() > 2) {
    output_ << "usage: vhdl [summary|scopes|objects|processes|psl|all]\n";
    return;
  }
  auto section = std::string_view{"summary"};
  if (command.size() == 2) {
    section = command[1];
  }
  if (section != "summary" && section != "scopes"
      && section != "objects" && section != "processes"
      && section != "psl" && section != "all") {
    output_ << "usage: vhdl [summary|scopes|objects|processes|psl|all]\n";
    return;
  }
  try {
    const auto limits = VhdlDebugLimits{};
    const auto formatted = format_vhdl_debug_snapshot(
        simulation_.vhdl_debug_snapshot(limits),
        limits.maximum_formatted_bytes);
    if (section == "all") {
      output_ << formatted;
      return;
    }
    std::istringstream lines{formatted};
    std::string line;
    while (std::getline(lines, line)) {
      const auto retain = section == "summary"
          ? line.starts_with("vhdl ")
          : section == "scopes"
          ? line.starts_with("scope ")
          : section == "objects"
          ? line.starts_with("object ")
          : section == "processes"
          ? line.starts_with("process ")
          : line.starts_with("psl ") || line.starts_with("coverage ");
      if (retain) {
        output_ << line << '\n';
      }
    }
  } catch (const VhdlDebugError& error) {
    error_ << "VHDL debug snapshot: " << error.what() << '\n';
  }
}

void DebuggerSession::trace_command(const std::vector<std::string>& command)  {
    if (trace_ == nullptr) {
      output_ << "trace output is not configured\n";
      return;
    }
    if (command.size() == 2 && command[1] == "list") {
      const auto status = trace_->selection->status();
      for (const auto& owner : status.selected_owners) {
        output_ << owner << '\n';
      }
      if (status.selected_owners.empty()) {
        output_ << "(no traced signals)\n";
      }
      return;
    }
    if (command.size() == 2 && command[1] == "status") {
      const auto status = trace_->selection->status();
      output_ << "format " << project::to_string(trace_->format)
              << ", output " << support::path_to_utf8(trace_->output_path)
              << ", compression "
              << project::to_string(
                     trace_->control->status().effective_compression)
              << ", lifecycle "
              << (trace_->terminal_status == TraceTerminalStatus::open
                      ? "open"
                      : trace_->terminal_status == TraceTerminalStatus::complete
                      ? "complete"
                      : "failed")
              << ", declared " << status.declared
              << ", selected " << status.selected
              << ", generation " << status.generation << '\n';
      return;
    }
    if (command.size() == 2 && command[1] == "report") {
      for (const auto& entry : trace_->control->report()) {
        output_ << trace_control_entry_kind_name(entry.kind) << ' '
                << entry.name << '=' << entry.value << " identity="
                << entry.canonical_identity << '\n';
      }
      return;
    }
    if (command.size() == 2 && command[1] == "flush") {
      if (trace_->terminal_status == TraceTerminalStatus::complete) {
        output_ << "trace already complete\n";
      } else if (trace_->diagnostics
                 && flush_trace(*trace_, *trace_->diagnostics)) {
        output_ << "trace flushed\n";
      } else {
        error_ << (trace_->terminal_diagnostic.empty()
                       ? "trace flush failed"
                       : trace_->terminal_diagnostic)
               << '\n';
      }
      return;
    }
    if (command.size() == 2 && command[1] == "close") {
      if (!simulation_.finished()) {
        error_ << "trace close requires a finished simulation\n";
      } else if (trace_->diagnostics
                 && finish_trace(*trace_, *trace_->diagnostics)) {
        output_ << "trace complete\n";
      } else {
        error_ << (trace_->terminal_diagnostic.empty()
                       ? "trace close failed"
                       : trace_->terminal_diagnostic)
               << '\n';
      }
      return;
    }
    if (command.size() == 2
        && (command[1] == "all" || command[1] == "clear")) {
      const auto enable = command[1] == "all";
      for (const auto signal : trace_->selection->declared_signals()) {
        set_trace_enabled(signal, enable);
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
               "trace all|clear|list|status|report|flush|close\n";
  }

void DebuggerSession::set_trace_enabled(const SignalId signal, const bool enable)  {
    static_cast<void>(trace_->selection->set_enabled(
        signal, enable, simulation_.now(), simulation_.delta(),
        simulation_.read_signal(signal), *trace_->observations));
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
        const auto* info = design_signal_object(
            simulation_, breakpoint.signal);
        if (info == nullptr) {
          throw std::logic_error{
              "signal breakpoint lacks its DesignIR runtime adapter"};
        }
        std::string value_error;
        auto condition =
            parse_value(command[4], info->width, value_error);
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
    if (kind == "phase") {
      const auto states = phase_states();
      auto matches = std::vector<std::string>{};
      for (const auto& [identity, state] : states) {
        (void)state;
        if (identity == location
            || (location.find('.') == std::string_view::npos
                && identity.ends_with("." + std::string{location}))) {
          matches.push_back(identity);
        }
      }
      if (location != "*" && matches.empty()) {
        output_ << "unknown UVM phase: " << location << '\n';
        return;
      }
      if (matches.size() > 1) {
        output_ << "ambiguous UVM phase: " << location << '\n';
        return;
      }
      breakpoint.kind = DebugBreakpointKind::phase;
      breakpoint.id = next_breakpoint_++;
      breakpoint.path = location == "*"
          ? std::string{location} : std::move(matches.front());
      breakpoints_.push_back(breakpoint);
      phase_states_ = states;
      output_ << "breakpoint " << breakpoint.id << " set on UVM phase "
              << location << '\n';
      return;
    }
    if (kind == "uvm") {
      breakpoint.kind = DebugBreakpointKind::uvm;
      breakpoint.id = next_breakpoint_++;
      breakpoint.path = std::string{location};
      breakpoints_.push_back(breakpoint);
      output_ << "breakpoint " << breakpoint.id << " set on UVM activity "
              << location << '\n';
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
               "break source [PATH:]LINE | break phase IDENTITY|* | "
               "break uvm IDENTITY|*\n";
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
      } else if (breakpoint.kind == DebugBreakpointKind::source) {
        output_ << "source ";
        if (!breakpoint.path.empty()) {
          output_ << breakpoint.path << ':';
        }
        output_ << breakpoint.line;
      } else if (breakpoint.kind == DebugBreakpointKind::phase) {
        output_ << "phase " << breakpoint.path;
      } else {
        output_ << "uvm " << breakpoint.path;
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
    return support::path_from_utf8(actual).filename()
        == support::path_from_utf8(requested).filename();
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
          const auto current_phase_states = phase_states();
          for (const auto& [identity, state] : current_phase_states) {
            const auto previous = std::ranges::find(
                phase_states_, identity,
                &std::pair<std::string,
                           runtime::SystemVerilogUvmPhaseState>::first);
            if (previous != phase_states_.end()
                && previous->second == state) {
              continue;
            }
            phase_transition_ = identity + " state "
                + std::to_string(static_cast<unsigned>(state));
            const auto found = std::find_if(
                breakpoints_.begin(), breakpoints_.end(),
                [&](const DebugBreakpoint& breakpoint) {
                  return breakpoint.kind == DebugBreakpointKind::phase
                      && (breakpoint.path == "*"
                          || breakpoint.path == identity);
                });
            if (found != breakpoints_.end()) {
              hit_ = DebugBreakpointHit{found->id, *phase_transition_};
              scheduler.request_stop();
              break;
            }
          }
          phase_states_ = current_phase_states;
          if (!hit_ && stop_on_phase_transition_ && phase_transition_) {
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
    const auto* process = design_process_occurrence(
        simulation_, point.design_process);
    output_ << "process "
            << (process == nullptr
                    ? std::to_string(point.design_process)
                    : process->name)
            << " at " << point.source.path << ':' << point.source.line
            << ':' << point.source.column << '\n';
  }

void DebuggerSession::report_result(const runtime::RunResult& result)  {
    if (hit_) {
      output_ << "hit breakpoint " << hit_->id << ": "
              << hit_->description << '\n';
    }
    if (stop_on_phase_transition_ && phase_transition_) {
      output_ << "phase transition " << *phase_transition_ << '\n';
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
    phase_states_ = phase_states();
    phase_transition_.reset();
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
    phase_states_ = phase_states();
    phase_transition_.reset();
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

void DebuggerSession::step_phase() {
  if (!can_execute()) return;
  hit_.reset();
  current_execution_point_.reset();
  phase_states_ = phase_states();
  phase_transition_.reset();
  stop_on_phase_transition_ = true;
  install_execution_hook(earliest_time_breakpoint(
      simulation_.now(), std::nullopt));
  try {
    simulation_.clear_stop();
    executing_ = true;
    ExecutionGuard guard{executing_};
    const auto result = simulation_.run();
    report_result(result);
  } catch (const std::exception& exception) {
    error_ << exception.what() << '\n';
  }
  stop_on_phase_transition_ = false;
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
    phase_states_ = phase_states();
    phase_transition_.reset();
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
    const cli::Invocation& invocation,
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
  if (!apply_uvm_command_line(
          simulation, invocation.plusargs, diagnostics)) {
    return 1;
  }
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
  if (config.run.trace_file && config.run.trace_enabled && !trace) {
    return 1;
  }
  install_interrupt_hook(simulation);
  const InterruptSignalGuard interrupt_signal;
  simulation.start();
  output << "fsim debugger: ";
  for (std::size_t index = 0;
       index < simulation.design_ir().roots().size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << simulation.design_ir().roots()[index];
  }
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
  if (trace && !finish_trace(*trace, diagnostics)) {
    return 1;
  }
  return status;
}

} // namespace fsim::app::application_detail
