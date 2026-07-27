// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "fsim/compiler/object_cache.hpp"
#if defined(FSIM_HAS_LLVM)
#include "fsim/compiler/llvm_jit.hpp"
#endif
#include "fsim/frontend/parser.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/plugin_compiler.hpp"
#include "fsim/systemc/plugin_loader.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <csignal>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace fsim::app {
namespace {

using runtime::PackedLogic4;
using runtime::SimulationTick;
using runtime::simir::SignalId;

#if defined(FSIM_HAS_LLVM)

class LlvmProcessExecutor final : public runtime::simir::ProcessExecutor {
 public:
  LlvmProcessExecutor(
      compiler::LlvmJit& jit,
      const compiler::JitProcessHandle handle,
      const runtime::simir::Process& process,
      std::span<const std::uint32_t> signal_widths)
      : jit_(jit),
        handle_(handle),
        process_(process),
        signal_widths_(signal_widths) {
    const auto layout = jit_.frame_layout(handle_);
    register_aval_.resize(layout.register_count);
    register_bval_.resize(layout.register_count);
    jit_.initialize_frame(
        handle_, frame_, register_aval_, register_bval_);
  }

  [[nodiscard]] runtime::simir::ProcessResumeResult resume(
      runtime::simir::ProcessExecutionContext& context,
      const runtime::simir::InstructionIndex start_instruction) override {
    if (frame_.program_counter != start_instruction) {
      throw compiler::LlvmJitError(
          "compiled process frame PC disagrees with the simulation kernel");
    }

    CallbackState callback_state{
        &context, signal_widths_, {}};
    fsim_jit_runtime_v1 runtime{};
    runtime.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V1;
    runtime.struct_size = sizeof(runtime);
    runtime.context = &callback_state;
    runtime.read_signal = read_signal;
    runtime.write_signal = write_signal;
    runtime.assert_failed = assert_failed;
    runtime.write_update = write_update;
    runtime.write_after = write_after;
    runtime.flags =
        context.execution_points_enabled()
            ? FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS
            : 0;

    fsim_jit_resume_result_v1 result{};
    result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
    result.struct_size = sizeof(result);
    const auto status = [&]() -> compiler::JitResumeStatus {
      try {
        return jit_.resume(handle_, runtime, frame_, result);
      } catch (const compiler::LlvmJitGeneratedRuntimeError& error) {
        if (callback_state.failure) {
          std::rethrow_exception(callback_state.failure);
        }
        switch (error.reason()) {
          case compiler::JitGeneratedRuntimeErrorReason::
              unknown_branch_condition:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "branch condition is unknown or high impedance");
        }
        throw;
      } catch (...) {
        // A callback failure is the first language/runtime failure observed by
        // generated code and must not be masked by a later adapter status.
        if (callback_state.failure) {
          std::rethrow_exception(callback_state.failure);
        }
        throw;
      }
    }();
    if (callback_state.failure) {
      std::rethrow_exception(callback_state.failure);
    }
    if (result.instruction >= process_.operations.size()) {
      throw compiler::LlvmJitError(
          "compiled process returned an invalid boundary instruction");
    }

    switch (status) {
      case compiler::JitResumeStatus::completed:
        require_boundary<runtime::simir::Halt>(
            result.instruction, "completion");
        break;
      case compiler::JitResumeStatus::assertion_failed: {
        const auto* assertion = std::get_if<runtime::simir::Assert>(
            &process_.operations[result.instruction]);
        if (assertion == nullptr) {
          throw compiler::LlvmJitError(
              "compiled process reported an assertion at a non-assert "
              "instruction");
        }
        throw runtime::simir::AssertionError(
            process_.id,
            result.instruction,
            assertion->message.empty()
                ? "assertion failed"
                : assertion->message,
            assertion->severity,
            assertion->source);
      }
      case compiler::JitResumeStatus::wait_for: {
        const auto* wait = std::get_if<runtime::simir::WaitFor>(
            &process_.operations[result.instruction]);
        if (wait == nullptr) {
          throw compiler::LlvmJitError(
              "compiled process reported WaitFor at a non-wait instruction");
        }
        if (result.delay != wait->delay) {
          throw compiler::LlvmJitError(
              "compiled process returned a WaitFor delay that disagrees "
              "with SimIR");
        }
        break;
      }
      case compiler::JitResumeStatus::wait_on:
        require_boundary<runtime::simir::WaitOn>(
            result.instruction, "WaitOn");
        break;
      case compiler::JitResumeStatus::wait_sensitivity:
        require_boundary<runtime::simir::WaitSensitivity>(
            result.instruction, "WaitSensitivity");
        break;
      case compiler::JitResumeStatus::yielded:
        require_boundary<runtime::simir::Yield>(
            result.instruction, "yield");
        break;
      case compiler::JitResumeStatus::debug_point:
        require_boundary<runtime::simir::DebugPoint>(
            result.instruction, "debug point");
        break;
      case compiler::JitResumeStatus::stopped:
        require_boundary<runtime::simir::Stop>(
            result.instruction, "stop");
        break;
    }
    if (frame_.program_counter != result.instruction + 1U) {
      throw compiler::LlvmJitError(
          "compiled process returned a non-sequential boundary PC");
    }
    return {result.instruction, frame_.program_counter};
  }

 private:
  struct CallbackState {
    runtime::simir::ProcessExecutionContext* context{};
    std::span<const std::uint32_t> signal_widths;
    std::exception_ptr failure;
  };

  template <typename Boundary>
  void require_boundary(
      const runtime::simir::InstructionIndex instruction,
      const std::string_view status) const {
    if (!std::holds_alternative<Boundary>(
            process_.operations[instruction])) {
      throw compiler::LlvmJitError(
          "compiled process reported " + std::string{status}
          + " at the wrong SimIR instruction");
    }
  }

  static void capture_failure(CallbackState& state) noexcept {
    if (!state.failure) {
      state.failure = std::current_exception();
    }
  }

  static std::uint64_t read_signal(
      void* context,
      const std::uint32_t signal,
      std::uint64_t* bval) noexcept {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      if (bval != nullptr) {
        *bval = 0;
      }
      return 0;
    }
    try {
      if (bval == nullptr || state.context == nullptr
          || signal >= state.signal_widths.size()) {
        throw std::logic_error("invalid generated read-signal callback");
      }
      const auto value = state.context->read_signal_word(signal);
      const auto expected_width = state.signal_widths[signal];
      if (value.width != expected_width
          || value.width == 0
          || value.width > 64) {
        throw std::logic_error(
            "generated read-signal callback observed an invalid width");
      }
      *bval = value.bval;
      return value.aval;
    } catch (...) {
      capture_failure(state);
      if (bval != nullptr) {
        *bval = 0;
      }
      return 0;
    }
  }

  static void write_signal(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval) noexcept {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_blocking_word(
          signal, value);
    } catch (...) {
      capture_failure(state);
    }
  }

  static void write_update(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval) noexcept {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_update_word(
          signal, value);
    } catch (...) {
      capture_failure(state);
    }
  }

  static void write_after(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval,
      const std::uint64_t delay) noexcept {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_after_word(
          signal, value, delay);
    } catch (...) {
      capture_failure(state);
    }
  }

  [[nodiscard]] static runtime::Logic4Word checked_write_word(
      const CallbackState& state,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval) {
    if (state.context == nullptr
        || signal >= state.signal_widths.size()) {
      throw std::logic_error("invalid generated write-signal callback");
    }
    const auto width = state.signal_widths[signal];
    if (width == 0 || width > 64) {
      throw std::logic_error(
          "generated write-signal callback received an invalid width");
    }
    return {width, aval, bval};
  }

  static void assert_failed(
      void* context,
      std::uint32_t,
      std::uint32_t,
      const char*,
      std::uint64_t) noexcept {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    // The generated status and the immutable SimIR assertion carry all data
    // needed after the C ABI returns. No C++ allocation or exception is
    // permitted in this thunk.
  }

  compiler::LlvmJit& jit_;
  compiler::JitProcessHandle handle_;
  const runtime::simir::Process& process_;
  std::span<const std::uint32_t> signal_widths_;
  fsim_jit_frame_v1 frame_{};
  std::vector<std::uint64_t> register_aval_;
  std::vector<std::uint64_t> register_bval_;
};

[[nodiscard]] compiler::JitOptimizationLevel jit_optimization(
    const project::Optimization optimization) noexcept {
  return optimization == project::Optimization::o0
      ? compiler::JitOptimizationLevel::o0
      : compiler::JitOptimizationLevel::o2;
}

#endif

std::atomic_bool interrupt_requested{};
static_assert(
    std::atomic_bool::is_always_lock_free,
    "the supported Ctrl-C path requires a lock-free atomic flag");

extern "C" void handle_interrupt(int) {
  interrupt_requested.store(true, std::memory_order_relaxed);
}

diagnostic::SourcePosition position(const frontend::SourceLocation& source) {
  const auto clamp = [](const std::size_t value) {
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(value, std::numeric_limits<std::uint32_t>::max()));
  };
  return {clamp(source.line), clamp(source.column),
          static_cast<std::uint64_t>(source.offset)};
}

diagnostic::SourceSpan span(const frontend::SourceSpan& source) {
  return {source.source_name, position(source.begin), position(source.end)};
}

void import_diagnostic(
    diagnostic::Engine& output,
    const frontend::Diagnostic& input) {
  switch (input.severity) {
    case frontend::DiagnosticSeverity::Note:
      output.note(input.code, input.message, span(input.span));
      break;
    case frontend::DiagnosticSeverity::Warning:
      output.warning(input.code, input.message, span(input.span));
      break;
    case frontend::DiagnosticSeverity::Error:
      output.error(input.code, input.message, span(input.span));
      break;
  }
}

frontend::Language frontend_language(const project::Language language) {
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

struct ParseInput {
  std::filesystem::path path;
  frontend::Language language{frontend::Language::SystemVerilog2017};
  std::string library{"work"};
};

frontend::ParseResult parse_file_snapshot(
    const ParseInput& input,
    std::string& content_digest) {
  std::ifstream stream(input.path, std::ios::binary);
  if (!stream) {
    frontend::ParseResult result;
    result.diagnostics.push_back({
        frontend::DiagnosticSeverity::Error,
        "FSIM-FE-IO-001",
        "unable to open source file",
        {input.path.string(), {}, {}},
        {},
    });
    return result;
  }
  std::string text{
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>()};
  if (!stream.good() && !stream.eof()) {
    frontend::ParseResult result;
    result.diagnostics.push_back({
        frontend::DiagnosticSeverity::Error,
        "FSIM-FE-IO-002",
        "failed while reading source file",
        {input.path.string(), {}, {}},
        {},
    });
    return result;
  }
  content_digest = support::Sha256::hex(
      support::Sha256::digest(text));
  return frontend::parse(
      frontend::SourceText{input.path.string(), std::move(text)},
      input.language);
}

std::optional<systemc::PluginCompileRequest> systemc_request(
    const project::Config& config) {
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

struct SystemCFactoryCollector {
  std::set<std::string> names;
};

extern "C" fsim_sc_status_v1 validate_sc_register_port(
    void*,
    fsim_sc_handle_v1,
    const char*,
    fsim_sc_port_direction_v1,
    fsim_sc_value_encoding_v1,
    std::uint32_t,
    fsim_sc_handle_v1*) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 validate_sc_register_process(
    void*,
    fsim_sc_handle_v1,
    const char*,
    fsim_sc_process_kind_v1,
    fsim_sc_process_entry_v1,
    void*,
    fsim_sc_handle_v1*) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 validate_sc_add_sensitivity(
    void*,
    fsim_sc_handle_v1,
    fsim_sc_handle_v1,
    fsim_sc_edge_kind_v1) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 validate_sc_read_value(
    void*, fsim_sc_handle_v1, fsim_sc_value_view_v1*) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 validate_sc_write_value(
    void*, fsim_sc_handle_v1, const fsim_sc_value_view_v1*) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 validate_sc_wait_time(
    void*, std::uint64_t) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 validate_sc_wait_event(
    void*, fsim_sc_handle_v1) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 validate_sc_notify_event(
    void*, fsim_sc_handle_v1, std::uint64_t) {
  return FSIM_SC_NOT_SUPPORTED;
}

extern "C" void validate_sc_report(void*, int, const char*) {}

extern "C" fsim_sc_status_v1 validate_sc_register_factory(
    void* context,
    const char* name,
    fsim_sc_module_factory_v1 factory,
    fsim_sc_module_destroy_v1 destroy,
    void*) {
  if (context == nullptr || name == nullptr || *name == '\0'
      || factory == nullptr || destroy == nullptr) {
    return FSIM_SC_INVALID_ARGUMENT;
  }
  try {
    auto& collector = *static_cast<SystemCFactoryCollector*>(context);
    return collector.names.insert(name).second
        ? FSIM_SC_OK
        : FSIM_SC_INVALID_ARGUMENT;
  } catch (...) {
    return FSIM_SC_RUNTIME_ERROR;
  }
}

bool validate_systemc_plugin(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics) {
  fsim_sc_host_v1 host{};
  host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
  host.struct_size = sizeof(host);
  host.register_port = validate_sc_register_port;
  host.register_process = validate_sc_register_process;
  host.add_sensitivity = validate_sc_add_sensitivity;
  host.read_value = validate_sc_read_value;
  host.write_value = validate_sc_write_value;
  host.wait_time = validate_sc_wait_time;
  host.wait_event = validate_sc_wait_event;
  host.notify_event = validate_sc_notify_event;
  host.report = validate_sc_report;

  SystemCFactoryCollector collector;
  fsim_sc_registrar_v1 registrar{};
  registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
  registrar.struct_size = sizeof(registrar);
  registrar.context = &collector;
  registrar.register_factory = validate_sc_register_factory;

  std::string error;
  auto plugin = systemc::Plugin::load(path, host, registrar, error);
  if (!plugin) {
    diagnostics.error(
        "FSIM-SC-C009",
        "compiled SystemC plug-in failed ABI validation: " + error);
    return false;
  }
  if (collector.names.empty()) {
    diagnostics.error(
        "FSIM-SC-A001",
        "compiled SystemC plug-in did not register a module factory");
    return false;
  }
  return true;
}

std::string unit_key(const frontend::DesignUnit& unit) {
  switch (unit.kind) {
    case frontend::UnitKind::VhdlEntity:
      return "vhdl:" + unit.library + ":entity:" + unit.name;
    case frontend::UnitKind::VhdlArchitecture:
      return "vhdl:" + unit.library + ":architecture:"
          + unit.primary_name + ':' + unit.name;
    case frontend::UnitKind::VerilogModule:
      return "verilog:" + unit.library + ":module:" + unit.name;
  }
  return {};
}

std::string selected_top(
    const project::Config& config,
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics) {
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

struct BindingTarget {
  std::string language;
  std::string unit;
};

std::optional<BindingTarget> parse_binding_target(std::string_view target) {
  const auto colon = target.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 == target.size()) {
    return std::nullopt;
  }
  BindingTarget result;
  result.language = std::string(target.substr(0, colon));
  auto remainder = target.substr(colon + 1);
  if (const auto dot = remainder.rfind('.'); dot != std::string_view::npos) {
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

void validate_bindings(
    const project::Config& config,
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics) {
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
      diagnostics.error(
          "FSIM-SC-UNSUPPORTED-0002",
          "SystemC factory binding and kernel elaboration are not implemented "
          "in this vertical slice");
      continue;
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

std::string target_name() {
#if defined(_WIN32)
  return "x86_64-pc-windows";
#elif defined(__linux__)
  return "x86_64-unknown-linux";
#else
  return "x86_64-unknown";
#endif
}

std::string make_cache_key(
    const project::Config& config,
    const CheckedProject& checked,
    const std::string_view top,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics) {
  compiler::CacheKeyBuilder key;
  key.add("fsim-version", version);
  key.add("runtime-abi", std::to_string(runtime_abi_version));
  key.add("target", target_name());
  key.add("top", top);
  key.add("time-resolution", resolution);
  key.add("optimization", project::to_string(config.build.optimization));
  key.add("llvm", production_llvm_version);
  key.add("standard-library", standard_library_cache_version);
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
          || checked.hdl_sources[hdl_source_index].path
                  .lexically_normal()
              != file.lexically_normal()) {
        diagnostics.error(
            "FSIM-CACHE-0001",
            "parsed source identity is inconsistent for '"
                + file.generic_string() + "'");
        return {};
      }
      key.add(
          "source-content",
          checked.hdl_sources[hdl_source_index].content_digest);
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
    diagnostic::Engine& diagnostics) {
  struct SourceSettings {
    const project::SourceSet* source_set{};
    const CheckedSource* checked_source{};
  };
  const auto settings_for =
      [&](const elaboration::SpecializationInfo& specialization)
          -> std::optional<SourceSettings> {
        const auto source_path =
            std::filesystem::path{specialization.source}
                .lexically_normal();
        const CheckedSource* checked_source = nullptr;
        for (const auto& candidate : checked.hdl_sources) {
          if (candidate.path.lexically_normal() == source_path) {
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
                  != specialization.language
              || (source_set.library.empty() ? "work"
                                             : source_set.library)
                  != specialization.library) {
            continue;
          }
          if (std::any_of(
                  source_set.files.begin(),
                  source_set.files.end(),
                  [&](const std::filesystem::path& candidate) {
                    return candidate.lexically_normal() == source_path;
                  })) {
            return SourceSettings{&source_set, checked_source};
          }
        }
        return std::nullopt;
      };

  std::vector<std::string> result;
  result.reserve(design.specializations().size());
  for (const auto& specialization : design.specializations()) {
    const auto settings = settings_for(specialization);
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
        "fsim-specialization-provenance-v1");
    key.add("fsim-version", version);
    key.add("standard-library", standard_library_cache_version);
    key.add("unit", specialization.unit);
    key.add(
        "source-path",
        settings->checked_source->path.lexically_normal().generic_string());
    key.add(
        "source-content",
        settings->checked_source->content_digest);
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
    for (const auto& [name, value] :
         specialization.parameter_values) {
      key.add("parameter-name", name);
      key.add("parameter-value", value);
    }
    result.push_back(key.finish());
  }
  return result;
}

bool wildcard_match(std::string_view pattern, std::string_view text) {
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
    const std::string_view name) {
  return filters.empty()
      || std::any_of(filters.begin(), filters.end(), [&](const auto& filter) {
           return wildcard_match(filter, name);
         });
}

struct VcdScale {
  std::string timescale;
  SimulationTick tick_multiplier{1};
};

std::optional<VcdScale> vcd_scale(
    const std::string_view resolution,
    diagnostic::Engine& diagnostics) {
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

struct TraceState {
  std::ofstream stream;
  std::unique_ptr<runtime::VcdWriter> writer;
  std::vector<std::optional<runtime::VcdSignal>> handles;
  std::vector<bool> enabled;
  SimulationTick tick_multiplier{1};
};

std::unique_ptr<TraceState> attach_trace(
    Simulation& simulation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const bool dynamic_selection = false) {
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
    diagnostic::Engine& diagnostics) {
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

void install_interrupt_hook(Simulation& simulation) {
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
    diagnostic::Engine& diagnostics) {
  const auto cache = simulation.native_cache_statistics();
  if (cache.load_failures == 0 && cache.store_failures == 0) {
    return;
  }
  diagnostics.warning(
      "FSIM-CACHE-0004",
      "native LLVM object cache reported "
          + std::to_string(cache.load_failures)
          + " load failure(s) and "
          + std::to_string(cache.store_failures)
          + " store failure(s); simulation remains valid, but cache reuse "
            "may be incomplete");
}

int handle_check(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&) {
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
    std::ostream&) {
  auto built = build_project(config, diagnostics);
  if (!built) {
    return 1;
  }
  const auto top = built->design.top();
  const auto signal_count = built->design.signals().size();
  const auto process_count = built->design.processes().size();
  const auto plugin_count = built->systemc_plugins.size();
  const auto cache_hit = built->cache_hit;
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
  return 0;
}

int handle_run(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&) {
  auto built = build_project(config, diagnostics);
  if (!built) {
    return 1;
  }
  const auto duration = configured_duration(
      config, built->time_resolution, diagnostics);
  if (config.run.duration && !duration) {
    return 1;
  }
  Simulation simulation(
      std::move(*built),
      config.run.max_deltas,
      SimulationEngine::compiled);
  report_native_cache_failures(simulation, diagnostics);
  auto trace = attach_trace(simulation, config, diagnostics);
  if (config.run.trace_file && !trace) {
    return 1;
  }
  install_interrupt_hook(simulation);
  std::signal(SIGINT, handle_interrupt);
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

void print_debug_help(std::ostream& output) {
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
      << "          where, help, quit\n";
}

std::vector<std::string> words(const std::string& line) {
  std::istringstream input(line);
  std::vector<std::string> result;
  for (std::string word; input >> word;) {
    result.push_back(std::move(word));
  }
  return result;
}

enum class DebugBreakpointKind {
  time,
  signal,
  source,
};

struct DebugBreakpoint {
  std::uint64_t id{};
  DebugBreakpointKind kind{DebugBreakpointKind::time};
  SimulationTick time{};
  SignalId signal{};
  std::string path;
  std::uint32_t line{};
  std::optional<PackedLogic4> signal_condition;
  bool signal_condition_equal{true};
};

struct DebugBreakpointHit {
  std::uint64_t id{};
  std::string description;
};

class DebuggerSession final {
 public:
  DebuggerSession(
      Simulation& simulation,
      std::ostream& output,
      std::ostream& error,
      TraceState* trace = nullptr)
      : simulation_(simulation),
        output_(output),
        error_(error),
        trace_(trace),
        scope_(simulation.design().top()),
        signal_paths_(simulation.design().signal_paths()) {
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

  ~DebuggerSession() {
    simulation_.remove_signal_change_hook(observer_);
    simulation_.set_execution_point_hook({});
    install_interrupt_hook(simulation_);
  }

  DebuggerSession(const DebuggerSession&) = delete;
  DebuggerSession& operator=(const DebuggerSession&) = delete;

  void execute(const std::vector<std::string>& command) {
    if (command[0] == "where") {
      output_ << "time " << simulation_.now() << ", delta "
              << simulation_.delta() << ", scope " << scope_ << '\n';
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
      if (const auto signal = resolve_signal(command[1])) {
        output_ << signal->first << " = "
                << simulation_.read_signal(signal->second).to_msb_string();
        if (simulation_.signal_is_forced(signal->second)) {
          output_ << " (forced)";
        }
        output_ << '\n';
      }
      return;
    }
    if ((command[0] == "deposit" || command[0] == "force")
        && command.size() == 3) {
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

 private:
  struct ExecutionGuard {
    bool& executing;
    ~ExecutionGuard() { executing = false; }
  };

  [[nodiscard]] bool canonical_path(const std::string_view path) const {
    if (path == simulation_.design().top()) {
      return true;
    }
    const auto prefix = std::string(path) + ".";
    return std::any_of(
        signal_paths_.begin(), signal_paths_.end(),
        [&](const auto& entry) { return entry.first.starts_with(prefix); });
  }

  [[nodiscard]] std::optional<std::string> resolve_scope(
      const std::string_view requested) const {
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
  resolve_signal(const std::string_view name) {
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

  [[nodiscard]] std::optional<SimulationTick> command_time(
      const std::string_view text) {
    std::string time_error;
    const auto result =
        parse_time(text, simulation_.time_resolution(), time_error);
    if (!result) {
      output_ << time_error << '\n';
    }
    return result;
  }

  void scope_command(const std::vector<std::string>& command) {
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

  void scopes_command(const std::vector<std::string>& command) {
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
    if (children.empty()) {
      output_ << "(no child scopes)\n";
      return;
    }
    for (const auto& child : children) {
      output_ << child << '\n';
    }
  }

  void signals_command(const std::vector<std::string>& command) {
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
      output_ << path << " = "
              << simulation_.read_signal(signal).to_msb_string();
      if (simulation_.signal_is_forced(signal)) {
        output_ << " (forced)";
      }
      output_ << '\n';
    }
    if (!found) {
      output_ << "(no signals)\n";
    }
  }

  void modify_signal(const std::vector<std::string>& command) {
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

  void trace_command(const std::vector<std::string>& command) {
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

  void set_trace_enabled(const SignalId signal, const bool enable) {
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

  void add_breakpoint(const std::vector<std::string>& command) {
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

  void list_breakpoints() const {
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

  void delete_breakpoint(const std::string_view id_text) {
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

  [[nodiscard]] std::optional<DebugBreakpoint> earliest_time_breakpoint(
      const SimulationTick start,
      const std::optional<SimulationTick> requested_limit) const {
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

  [[nodiscard]] static bool source_path_matches(
      const std::string_view requested,
      const std::string_view actual) {
    if (requested.empty() || requested == actual) {
      return true;
    }
    return std::filesystem::path{actual}.filename()
        == std::filesystem::path{requested}.filename();
  }

  [[nodiscard]] static bool is_statement_point(
      const runtime::simir::ExecutionPointKind kind) noexcept {
    return kind == runtime::simir::ExecutionPointKind::statement
        || kind == runtime::simir::ExecutionPointKind::wait
        || kind == runtime::simir::ExecutionPointKind::assertion;
  }

  void install_execution_hook(
      const std::optional<DebugBreakpoint>& time_breakpoint,
      const std::function<bool(runtime::Scheduler&, runtime::SchedulerPhase)>&
          additional_stop = {},
      const std::function<bool(const runtime::simir::ExecutionPoint&)>&
          additional_execution_stop = {}) {
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

  void report_execution_point() {
    if (!current_execution_point_) {
      return;
    }
    const auto& point = *current_execution_point_;
    output_ << "process "
            << simulation_.design().processes().at(point.process).name
            << " at " << point.source.path << ':' << point.source.line
            << ':' << point.source.column << '\n';
  }

  void report_result(const runtime::RunResult& result) {
    if (hit_) {
      output_ << "hit breakpoint " << hit_->id << ": "
              << hit_->description << '\n';
    }
    report_execution_point();
    output_
        << (simulation_.finished() ? "simulation finished" : "stopped")
        << " at time " << result.time << ", delta " << result.delta << '\n';
  }

  [[nodiscard]] bool can_execute() {
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

  void run(const std::optional<SimulationTick> requested_limit) {
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

  void step(const bool delta_step) {
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

  void step_execution(const bool process_step) {
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

  Simulation& simulation_;
  std::ostream& output_;
  std::ostream& error_;
  TraceState* trace_{};
  std::string scope_;
  std::vector<std::pair<std::string, SignalId>> signal_paths_;
  std::vector<DebugBreakpoint> breakpoints_;
  std::optional<DebugBreakpointHit> hit_;
  std::optional<runtime::simir::ExecutionPoint> current_execution_point_;
  std::uint64_t next_breakpoint_{1};
  std::uint64_t observer_{};
  bool executing_{};
};

int run_debug_repl_impl(
    Simulation& simulation,
    std::istream& input,
    std::ostream& output,
    std::ostream& error,
    TraceState* trace) {
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
    std::ostream& error_output) {
  auto built = build_project(config, diagnostics);
  if (!built) {
    return 1;
  }
  Simulation simulation(
      std::move(*built),
      config.run.max_deltas,
      SimulationEngine::debug);
  report_native_cache_failures(simulation, diagnostics);
  auto trace = attach_trace(simulation, config, diagnostics, true);
  if (config.run.trace_file && !trace) {
    return 1;
  }
  install_interrupt_hook(simulation);
  std::signal(SIGINT, handle_interrupt);
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

struct ParsedMagnitude {
  std::uint64_t magnitude{};
  std::string unit;
};

std::optional<ParsedMagnitude> magnitude_and_unit(std::string_view text) {
  std::string compact;
  compact.reserve(text.size());
  for (const char character : text) {
    if (std::isspace(static_cast<unsigned char>(character)) == 0
        && character != '_') {
      compact.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(character))));
    }
  }
  const auto split = std::find_if(
      compact.begin(), compact.end(),
      [](const char character) {
        return character < '0' || character > '9';
      });
  if (split == compact.begin()) {
    return std::nullopt;
  }
  const auto* number_end =
      compact.data() + std::distance(compact.begin(), split);
  std::uint64_t magnitude = 0;
  const auto [end, conversion_error] =
      std::from_chars(compact.data(), number_end, magnitude);
  if (conversion_error != std::errc{} || end != number_end) {
    return std::nullopt;
  }
  return ParsedMagnitude{
      magnitude, std::string(split, compact.end())};
}

std::optional<std::uint64_t> unit_femtoseconds(std::string_view unit) {
  if (unit == "fs") {
    return 1;
  }
  if (unit == "ps") {
    return 1'000;
  }
  if (unit == "ns") {
    return 1'000'000;
  }
  if (unit == "us") {
    return 1'000'000'000;
  }
  if (unit == "ms") {
    return 1'000'000'000'000;
  }
  if (unit == "s") {
    return 1'000'000'000'000'000;
  }
  return std::nullopt;
}

template <typename Function>
void visit_delays(std::vector<frontend::Statement>& statements, Function&& function) {
  for (auto& statement : statements) {
    if (statement.delay) {
      function(*statement.delay);
    }
    visit_delays(statement.statements, function);
    visit_delays(statement.else_statements, function);
  }
}

std::string effective_resolution(
    const project::Config& config,
    frontend::ParsedDesign& parsed) {
  if (config.project.time_resolution != "auto") {
    return config.project.time_resolution;
  }
  std::uint64_t finest_femtoseconds = 1'000'000;
  std::string finest_spelling{"1ns"};
  bool found = false;
  const auto consider_resolution =
      [&](const std::string_view spelling) {
        const auto parsed_time = magnitude_and_unit(spelling);
        if (!parsed_time || parsed_time->unit.empty()) {
          return;
        }
        const auto factor = unit_femtoseconds(parsed_time->unit);
        if (!factor
            || parsed_time->magnitude
                > std::numeric_limits<std::uint64_t>::max() / *factor) {
          return;
        }
        const auto femtoseconds = parsed_time->magnitude * *factor;
        if (!found || femtoseconds < finest_femtoseconds) {
          finest_femtoseconds = femtoseconds;
          finest_spelling =
              std::to_string(parsed_time->magnitude) + parsed_time->unit;
          found = true;
        }
      };
  for (auto& unit : parsed.units) {
    if (!unit.time_precision.empty()) {
      consider_resolution(unit.time_precision);
    }
    auto consider = [&](const frontend::Delay& delay) {
      if (delay.unit.empty()) {
        return;
      }
      consider_resolution("1" + delay.unit);
    };
    visit_delays(unit.concurrent_statements, consider);
    for (auto& process : unit.processes) {
      visit_delays(process.statements, consider);
    }
  }
  return finest_spelling;
}

bool normalize_delays(
    frontend::ParsedDesign& parsed,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics) {
  bool valid = true;
  for (auto& unit : parsed.units) {
    auto normalize = [&](frontend::Delay& delay) {
      if (delay.unit.empty()) {
        return;
      }
      std::string error;
      const auto ticks = parse_time(
          std::to_string(delay.magnitude) + delay.unit, resolution, error);
      if (!ticks) {
        diagnostics.error(
            "FSIM-TIME-0003", error, span(delay.span));
        valid = false;
        return;
      }
      delay.magnitude = *ticks;
      delay.unit.clear();
    };
    visit_delays(unit.concurrent_statements, normalize);
    for (auto& process : unit.processes) {
      visit_delays(process.statements, normalize);
    }
  }
  return valid;
}

bool validate_declared_time_precisions(
    const frontend::ParsedDesign& parsed,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics) {
  bool valid = true;
  for (const auto& unit : parsed.units) {
    if (unit.time_precision.empty()) {
      continue;
    }
    std::string error;
    if (!parse_time(unit.time_precision, resolution, error)) {
      diagnostics.error(
          "FSIM-TIME-0004",
          "declared SystemVerilog time precision '"
              + unit.time_precision
              + "' is not representable at project resolution '"
              + std::string{resolution} + "'",
          span(unit.span));
      valid = false;
    }
  }
  return valid;
}

}  // namespace

std::optional<CheckedProject> check_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics) {
  std::vector<ParseInput> inputs;
  std::size_t systemc_source_count = 0;
  for (const auto& source_set : config.source_sets) {
    if (source_set.compilation_unit != "file") {
      diagnostics.warning(
          "FSIM-FE-CU-0001",
          "source-set compilation-unit grouping is preserved in the manifest, "
          "but files are parsed independently in this vertical slice");
    }
    if (source_set.language != project::Language::systemc
        && (!source_set.include_directories.empty()
            || !source_set.defines.empty())) {
      diagnostics.error(
          "FSIM-FE-PP-0001",
          "include directories and macro definitions require the forthcoming "
          "HDL preprocessor");
    }
    if (source_set.language == project::Language::systemc) {
      systemc_source_count += source_set.files.size();
      continue;
    }
    for (const auto& file : source_set.files) {
      inputs.push_back(
          {file, frontend_language(source_set.language), source_set.library});
    }
  }
  if (inputs.empty() && systemc_source_count == 0 && !diagnostics.has_error()) {
    diagnostics.error("FSIM-FE-0001", "the project contains no HDL source files");
  }
  if (const auto request = systemc_request(config)) {
    (void)systemc::plan_plugin_compile(*request, diagnostics);
  }
  if (diagnostics.has_error()) {
    return std::nullopt;
  }

  std::vector<std::optional<frontend::ParseResult>> parsed_inputs(
      inputs.size());
  std::vector<std::string> source_digests(inputs.size());
  std::vector<std::exception_ptr> parse_failures(inputs.size());
  std::atomic_size_t next_input{0};
  auto job_count = config.build.jobs == 0
      ? static_cast<std::size_t>(std::thread::hardware_concurrency())
      : static_cast<std::size_t>(config.build.jobs);
  job_count = std::max<std::size_t>(1, job_count);
  job_count = std::min(job_count, inputs.size());
  std::vector<std::future<void>> workers;
  workers.reserve(job_count);
  for (std::size_t worker = 0; worker < job_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      while (true) {
        const auto index =
            next_input.fetch_add(1, std::memory_order_relaxed);
        if (index >= inputs.size()) {
          return;
        }
        try {
          parsed_inputs[index] = parse_file_snapshot(
              inputs[index], source_digests[index]);
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
  checked.source_count = inputs.size() + systemc_source_count;
  std::set<std::string> known_units;
  for (std::size_t input_index = 0;
       input_index < parsed_inputs.size(); ++input_index) {
    if (parse_failures[input_index]) {
      try {
        std::rethrow_exception(parse_failures[input_index]);
      } catch (const std::exception& error) {
        diagnostics.error(
            "FSIM-FE-0003",
            "source analysis failed: " + std::string{error.what()},
            {inputs[input_index].path.generic_string(), {}, {}});
      } catch (...) {
        diagnostics.error(
            "FSIM-FE-0003",
            "source analysis failed with an unknown exception",
            {inputs[input_index].path.generic_string(), {}, {}});
      }
      continue;
    }
    if (!parsed_inputs[input_index]) {
      diagnostics.error(
          "FSIM-FE-0003",
          "source analysis produced no result",
          {inputs[input_index].path.generic_string(), {}, {}});
      continue;
    }
    if (!source_digests[input_index].empty()) {
      checked.hdl_sources.push_back({
          inputs[input_index].path,
          source_digests[input_index],
      });
    }
    auto result = std::move(*parsed_inputs[input_index]);
    for (const auto& frontend_diagnostic : result.diagnostics) {
      import_diagnostic(diagnostics, frontend_diagnostic);
    }
    for (auto& unit : result.design.units) {
      unit.library = inputs[input_index].library;
      const auto key = unit_key(unit);
      if (!known_units.insert(key).second) {
        diagnostics.error(
            "FSIM-FE-0002",
            "duplicate design unit '" + key + "'", span(unit.span));
      } else {
        checked.parsed.units.push_back(std::move(unit));
      }
    }
  }
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  return checked;
}

std::optional<BuiltProject> build_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics) {
  auto checked = check_project(config, diagnostics);
  if (!checked) {
    return std::nullopt;
  }
  std::vector<std::filesystem::path> systemc_plugins;
  if (const auto request = systemc_request(config)) {
    auto compiled = systemc::compile_plugin(*request, diagnostics);
    if (!compiled.success) {
      return std::nullopt;
    }
    if (!validate_systemc_plugin(compiled.library_path, diagnostics)) {
      return std::nullopt;
    }
    systemc_plugins.push_back(std::move(compiled.library_path));
  }
  const auto resolution = effective_resolution(config, checked->parsed);
  if (!validate_declared_time_precisions(
          checked->parsed, resolution, diagnostics)
      || !normalize_delays(
          checked->parsed, resolution, diagnostics)) {
    return std::nullopt;
  }
  validate_bindings(config, checked->parsed, diagnostics);
  const auto top = selected_top(config, checked->parsed, diagnostics);
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  std::vector<elaboration::Binding> bindings;
  bindings.reserve(config.bindings.size());
  for (const auto& binding : config.bindings) {
    bindings.push_back(
        {binding.instance, binding.target, binding.resolver});
  }
  auto elaborated = elaboration::elaborate(
      checked->parsed, top, bindings);
  for (const auto& input : elaborated.diagnostics) {
    diagnostics.error(input.code, input.message, span(input.span));
  }
  if (!elaborated.design || diagnostics.has_error()) {
    return std::nullopt;
  }

  auto specialization_cache_keys =
      make_specialization_cache_keys(
          config, *checked, *elaborated.design, diagnostics);
  if (!specialization_cache_keys) {
    return std::nullopt;
  }
  const auto key = make_cache_key(
      config, *checked, top, resolution, diagnostics);
  if (key.empty()) {
    return std::nullopt;
  }
  compiler::ObjectCache cache(config.build.cache_path);
  std::error_code cache_error;
  const auto existing = cache.load(key, cache_error);
  const bool hit = existing.has_value();
  if (!hit) {
    if (cache_error != std::errc::no_such_file_or_directory) {
      std::error_code erase_error;
      (void)cache.erase(key, erase_error);
      diagnostics.warning(
          "FSIM-CACHE-0002",
          "discarded an unreadable or incompatible cache entry: "
              + cache_error.message());
    }
    const std::string record =
        "FSIM-DESIGN-CACHE-V1\n" + top + "\n"
        + std::to_string(elaborated.design->signals().size()) + "\n"
        + std::to_string(elaborated.design->processes().size()) + "\n";
    const auto bytes = std::as_bytes(
        std::span<const char>{record.data(), record.size()});
    cache_error.clear();
    if (!cache.store(key, bytes, cache_error)) {
      diagnostics.error(
          "FSIM-CACHE-0003",
          "cannot populate cache: " + cache_error.message());
      return std::nullopt;
    }
  }
  return BuiltProject{
      std::move(*elaborated.design),
      key,
      resolution,
      config.build.cache_path,
      config.build.optimization,
      std::move(*specialization_cache_keys),
      std::move(systemc_plugins),
      hit};
}

struct Simulation::Impl {
  enum class Lifecycle {
    ready,
    finished,
    poisoned,
  };

  Impl(
      BuiltProject project,
      const std::uint64_t max_deltas,
      const SimulationEngine engine)
      : built(std::move(project)),
        interpreter(built.design.create_interpreter(
            runtime::SchedulerOptions{max_deltas, 32})) {
#if defined(FSIM_HAS_LLVM)
    if (engine != SimulationEngine::interpreter) {
      compiler::LlvmJitOptions options;
      options.optimization =
          engine == SimulationEngine::debug
              ? compiler::JitOptimizationLevel::o0
              : jit_optimization(built.optimization);
      if (!built.cache_path.empty()) {
        options.cache_directory = built.cache_path / "llvm-native";
      }
      jit = std::make_unique<compiler::LlvmJit>(std::move(options));

      signal_widths.reserve(built.design.signals().size());
      for (const auto& signal : built.design.signals()) {
        if (signal.width
            > std::numeric_limits<std::uint32_t>::max()) {
          signal_widths.push_back(
              std::numeric_limits<std::uint32_t>::max());
        } else {
          signal_widths.push_back(
              static_cast<std::uint32_t>(signal.width));
        }
      }
      const auto& processes = built.design.processes();
      for (const auto& specialization :
           built.design.specializations()) {
        std::vector<const runtime::simir::Process*> selected;
        std::vector<std::string> symbols;
        selected.reserve(specialization.processes.size());
        symbols.reserve(specialization.processes.size());
        for (const auto process_id : specialization.processes) {
          const auto& process = processes.at(process_id);
          if (!jit->supports_process(process, signal_widths)) {
            continue;
          }
          selected.push_back(&process);
          symbols.push_back(
              "fsim_process_" + std::to_string(process.id));
        }
        if (selected.empty()) {
          continue;
        }

        std::vector<compiler::JitProcessModuleEntry> entries;
        entries.reserve(selected.size());
        for (std::size_t index = 0; index < selected.size(); ++index) {
          entries.push_back({symbols[index], selected[index]});
        }
        const auto module_identity =
            "fsim-specialization:" +
            std::to_string(specialization.id) + ":" +
            specialization.unit + "@" +
            specialization.instance + "#provenance=" +
            built.specialization_cache_keys.at(
                specialization.id);
        jit->add_process_module(
            module_identity, entries, signal_widths);
        ++compiled_modules;
        for (std::size_t index = 0; index < selected.size(); ++index) {
          const auto handle = jit->lookup(symbols[index]);
          interpreter->set_process_executor(
              selected[index]->id,
              std::make_unique<LlvmProcessExecutor>(
                  *jit, handle, *selected[index], signal_widths));
          ++compiled_processes;
        }
      }
    }
#else
    (void)engine;
#endif
    interpreter->set_signal_change_hook(
        [this](
            const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time) {
          if (signal_change_hook) {
            signal_change_hook(
                signal, value, time, interpreter->scheduler().delta());
          }
          if (!signal_observers.empty()) {
            // Copy callbacks so observers may safely remove themselves while
            // receiving a synchronous simulation-thread notification.
            std::vector<SignalChangeHook> callbacks;
            callbacks.reserve(signal_observers.size());
            for (const auto& [token, callback] : signal_observers) {
              (void)token;
              callbacks.push_back(callback);
            }
            for (const auto& callback : callbacks) {
              callback(signal, value, time, interpreter->scheduler().delta());
            }
          }
        });
  }

  void validate_external_value(
      const SignalId signal,
      const PackedLogic4& value,
      const std::string_view operation) const {
    const auto& info = built.design.signals().at(signal);
    if (info.width != value.width()) {
      throw std::invalid_argument(
          std::string{operation} + " width does not match signal '"
          + info.name + "'");
    }
    if (info.source_domain != frontend::ValueDomain::Bit2
        && info.source_domain != frontend::ValueDomain::Boolean) {
      return;
    }
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
      const auto state = value.get(bit);
      if (state != runtime::Logic4::zero
          && state != runtime::Logic4::one) {
        throw std::invalid_argument(
            std::string{operation}
            + " would place an X/Z value into two-state signal '"
            + info.name + "'");
      }
    }
  }

  BuiltProject built;
#if defined(FSIM_HAS_LLVM)
  // Shared by every compiled executor. It is fully populated before executor
  // installation and outlives the interpreter that owns those executors.
  std::vector<std::uint32_t> signal_widths;
  // The interpreter owns executors referring to this JIT. Member destruction
  // is reversed, so declaring the JIT first destroys the interpreter first.
  std::unique_ptr<compiler::LlvmJit> jit;
#endif
  std::unique_ptr<runtime::simir::Interpreter> interpreter;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  SignalChangeHook signal_change_hook;
  std::map<std::uint64_t, SignalChangeHook> signal_observers;
  std::uint64_t next_signal_observer{1};
  Lifecycle lifecycle{Lifecycle::ready};
};

Simulation::Simulation(
    BuiltProject project,
    const std::uint64_t max_deltas,
    const SimulationEngine engine)
    : impl_(
          std::make_unique<Impl>(
              std::move(project), max_deltas, engine)) {}
Simulation::~Simulation() = default;
Simulation::Simulation(Simulation&&) noexcept = default;
Simulation& Simulation::operator=(Simulation&&) noexcept = default;

const elaboration::ElaboratedDesign& Simulation::design() const noexcept {
  return impl_->built.design;
}

std::string_view Simulation::time_resolution() const noexcept {
  return impl_->built.time_resolution;
}

std::optional<SignalId> Simulation::find_signal(
    const std::string_view path) const noexcept {
  return impl_->built.design.find_signal(path);
}

const PackedLogic4& Simulation::read_signal(const SignalId signal) const {
  return impl_->interpreter->signal_value(signal);
}

void Simulation::deposit_signal(
    const SignalId signal,
    PackedLogic4 value) {
  impl_->validate_external_value(signal, value, "deposit");
  impl_->interpreter->deposit_signal(signal, std::move(value));
}

void Simulation::force_signal(
    const SignalId signal,
    PackedLogic4 value) {
  impl_->validate_external_value(signal, value, "force");
  impl_->interpreter->force_signal(signal, std::move(value));
}

void Simulation::release_signal(const SignalId signal) {
  impl_->interpreter->release_signal(signal);
}

bool Simulation::signal_is_forced(const SignalId signal) const {
  return impl_->interpreter->signal_is_forced(signal);
}

void Simulation::start() {
  impl_->interpreter->start();
}

runtime::RunResult Simulation::run(
    const std::optional<SimulationTick> until) {
  if (impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error(
        "simulation is unavailable after a fatal runtime error");
  }
  if (impl_->lifecycle == Impl::Lifecycle::finished) {
    throw std::logic_error("simulation has finished");
  }
  if (until && *until < now()) {
    throw std::invalid_argument("run time limit is before the current time");
  }
  try {
    auto result = impl_->interpreter->run(until);
    if (result.status == runtime::RunStatus::completed
        || impl_->interpreter->stopped_by_design()) {
      impl_->lifecycle = Impl::Lifecycle::finished;
    }
    return result;
  } catch (...) {
    impl_->lifecycle = Impl::Lifecycle::poisoned;
    throw;
  }
}

void Simulation::request_stop() noexcept {
  impl_->interpreter->scheduler().request_stop();
}

void Simulation::clear_stop() noexcept {
  if (impl_->lifecycle == Impl::Lifecycle::ready) {
    impl_->interpreter->scheduler().clear_stop();
  }
}

SimulationTick Simulation::now() const noexcept {
  return impl_->interpreter->scheduler().now();
}

std::uint64_t Simulation::delta() const noexcept {
  return impl_->interpreter->scheduler().delta();
}

bool Simulation::has_pending() const noexcept {
  return impl_->interpreter->scheduler().has_pending();
}

bool Simulation::finished() const noexcept {
  return impl_->lifecycle == Impl::Lifecycle::finished;
}

bool Simulation::poisoned() const noexcept {
  return impl_->lifecycle == Impl::Lifecycle::poisoned;
}

std::size_t Simulation::compiled_process_count() const noexcept {
  return impl_->compiled_processes;
}

std::size_t Simulation::compiled_module_count() const noexcept {
  return impl_->compiled_modules;
}

NativeCacheStatistics Simulation::native_cache_statistics() const noexcept {
#if defined(FSIM_HAS_LLVM)
  if (impl_->jit) {
    const auto statistics = impl_->jit->cache_statistics();
    return {
        statistics.hits,
        statistics.misses,
        statistics.stores,
        statistics.rejected_entries,
        statistics.load_failures,
        statistics.store_failures,
    };
  }
#endif
  return {};
}

void Simulation::set_signal_change_hook(SignalChangeHook hook) {
  impl_->signal_change_hook = std::move(hook);
}

std::uint64_t Simulation::add_signal_change_hook(SignalChangeHook hook) {
  if (!hook) {
    throw std::invalid_argument("signal change observer cannot be empty");
  }
  if (impl_->next_signal_observer == 0) {
    throw std::overflow_error("signal change observer token space exhausted");
  }
  const auto token = impl_->next_signal_observer++;
  impl_->signal_observers.emplace(token, std::move(hook));
  return token;
}

void Simulation::remove_signal_change_hook(const std::uint64_t token) noexcept {
  impl_->signal_observers.erase(token);
}

void Simulation::set_safe_point_hook(runtime::Scheduler::SafePointHook hook) {
  impl_->interpreter->scheduler().set_safe_point_hook(std::move(hook));
}

void Simulation::set_execution_point_hook(ExecutionPointHook hook) {
  impl_->interpreter->set_execution_point_hook(std::move(hook));
}

int run_debug_repl(
    Simulation& simulation,
    std::istream& input,
    std::ostream& output,
    std::ostream& error) {
  return run_debug_repl_impl(
      simulation, input, output, error, nullptr);
}

std::optional<PackedLogic4> parse_value(
    const std::string_view text,
    const std::size_t width,
    std::string& error) {
  std::string normalized;
  normalized.reserve(text.size());
  for (const char character : text) {
    if (character == '_') {
      continue;
    }
    normalized.push_back(static_cast<char>(
        std::toupper(static_cast<unsigned char>(character))));
  }
  if (normalized.starts_with("0B")) {
    normalized.erase(0, 2);
  }
  if (normalized.size() != width) {
    error = "value width is " + std::to_string(normalized.size())
        + " but the signal width is " + std::to_string(width);
    return std::nullopt;
  }
  try {
    return PackedLogic4::from_msb_string(normalized);
  } catch (const std::invalid_argument&) {
    error = "value must contain only 0, 1, X, or Z";
    return std::nullopt;
  }
}

std::optional<SimulationTick> parse_time(
    const std::string_view text,
    const std::string_view resolution,
    std::string& error) {
  const auto requested = magnitude_and_unit(text);
  if (!requested) {
    error = "invalid time '" + std::string(text) + "'";
    return std::nullopt;
  }
  if (requested->unit.empty()) {
    return requested->magnitude;
  }
  const auto requested_factor = unit_femtoseconds(requested->unit);
  if (!requested_factor) {
    error = "unknown time unit '" + requested->unit + "'";
    return std::nullopt;
  }
  const auto effective_resolution =
      resolution == "auto" ? std::string_view{"1ns"} : resolution;
  const auto tick = magnitude_and_unit(effective_resolution);
  if (!tick || tick->unit.empty()) {
    error = "invalid project time resolution '"
        + std::string(effective_resolution) + "'";
    return std::nullopt;
  }
  const auto tick_factor = unit_femtoseconds(tick->unit);
  if (!tick_factor) {
    error = "unknown project time-resolution unit '" + tick->unit + "'";
    return std::nullopt;
  }
  if (requested->magnitude
      > std::numeric_limits<std::uint64_t>::max() / *requested_factor) {
    error = "time value overflows 64-bit simulation ticks";
    return std::nullopt;
  }
  if (tick->magnitude
      > std::numeric_limits<std::uint64_t>::max() / *tick_factor) {
    error = "time resolution overflows its internal representation";
    return std::nullopt;
  }
  const auto requested_fs = requested->magnitude * *requested_factor;
  const auto tick_fs = tick->magnitude * *tick_factor;
  if (tick_fs == 0 || requested_fs % tick_fs != 0) {
    error = "time is not exactly representable at resolution '"
        + std::string(effective_resolution) + "'";
    return std::nullopt;
  }
  return requested_fs / tick_fs;
}

cli::Services make_cli_services(std::istream& input) {
  cli::Handler debug =
      [&input](
          const cli::Invocation& invocation,
          const project::Config& config,
          diagnostic::Engine& diagnostics,
          std::ostream& output,
          std::ostream& error) {
        return handle_debug(
            invocation, config, diagnostics, input, output, error);
      };
  return {handle_check, handle_build, handle_run, std::move(debug)};
}

cli::Services make_cli_services() {
  return make_cli_services(std::cin);
}

}  // namespace fsim::app
