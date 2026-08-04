// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/app/application.hpp"

#include "tcl.hpp"

#include "fsim/compiler/object_cache.hpp"
#if defined(FSIM_HAS_LLVM)
#include "fsim/compiler/llvm_jit.hpp"
#endif
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/preprocessor.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/plugin_compiler.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
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
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {


[[nodiscard]] std::string_view report_severity_name(
    const runtime::simir::AssertionSeverity severity) noexcept;

using runtime::PackedLogic4;
using runtime::SimulationTick;
using runtime::simir::SignalId;

[[nodiscard]] std::uint64_t entropy_seed();

[[nodiscard]] semantic::Model build_semantic_model(
    const frontend::ParsedDesign& parsed,
    std::span<const CheckedSource> hdl_sources,
    std::span<const CheckedSource> systemc_sources,
    std::span<const CheckedSource> standard_sources);

[[nodiscard]] semantic::SourceSpanId intern_semantic_span(
    semantic::Model& model,
    const frontend::SourceSpan& source);

[[nodiscard]] semantic::vhdl::Hir build_vhdl_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics);

void complete_vhdl_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::vhdl::Hir& hir);

[[nodiscard]] semantic::sv::Hir build_systemverilog_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics);

void complete_systemverilog_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::sv::Hir& hir);

[[nodiscard]] semantic::design::DesignIr build_design_ir(
    CheckedProject& checked,
    const elaboration::ElaboratedDesign& elaborated);

[[nodiscard]] bool design_object_is_signal_bearing(
    const semantic::design::Object& object) noexcept;

[[nodiscard]] bool valid_runtime_projection(
    const semantic::design::DesignIr& design,
    const elaboration::ElaboratedDesign& runtime) noexcept;

class SystemCProcessExecutor final
    : public runtime::simir::ProcessExecutor {
 public:
  SystemCProcessExecutor(
      std::shared_ptr<systemc::HierarchyRegistry> hierarchy,
      const std::uint64_t process);

  [[nodiscard]] runtime::simir::ProcessResumeResult resume(
      runtime::simir::ProcessExecutionContext& context,
      runtime::simir::InstructionIndex) override;

  void update_channel(
      const std::uint64_t channel,
      runtime::simir::ProcessExecutionContext& context) override;

 private:
  std::shared_ptr<systemc::HierarchyRegistry> hierarchy_;
  std::uint64_t process_{};
};

#if defined(FSIM_HAS_LLVM)

class LlvmProcessExecutor final : public runtime::simir::ProcessExecutor {
 public:
  LlvmProcessExecutor(
      compiler::LlvmJit& jit,
      const compiler::JitProcessHandle handle,
      const runtime::simir::Process& process,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind> signal_value_kinds);

  [[nodiscard]] std::unique_ptr<runtime::simir::ProcessExecutor>
  fork_clone(
      runtime::simir::InstructionIndex start_instruction) override;

  [[nodiscard]] runtime::simir::ProcessResumeResult resume(
      runtime::simir::ProcessExecutionContext& context,
      const runtime::simir::InstructionIndex start_instruction) override;

  [[nodiscard]] PackedLogic4 read_register(
      const runtime::simir::RegisterId id,
      const std::size_t width) const override;

  void write_register(
      const runtime::simir::RegisterId id,
      const PackedLogic4& value) override;

  [[nodiscard]] std::string read_string_register(
      runtime::simir::StringRegisterId id) const override;
  void write_string_register(
      runtime::simir::StringRegisterId id,
      std::string_view value) override;
  [[nodiscard]] runtime::simir::ContainerValue
  read_container_register(
      runtime::simir::ContainerRegisterId id) const override;
  void write_container_register(
      runtime::simir::ContainerRegisterId id,
      const runtime::simir::ContainerValue& value) override;

 private:
  struct FrameStorage {
    std::vector<std::uint64_t> register_aval;
    std::vector<std::uint64_t> register_bval;
    std::vector<std::uint64_t> register_logic9_plane2;
    std::vector<std::uint64_t> register_logic9_plane3;
    std::vector<std::uint8_t> register_initialized;
    std::vector<std::string> string_registers;
    std::vector<runtime::simir::ContainerValue> container_registers;
  };

  LlvmProcessExecutor(
      compiler::LlvmJit& jit,
      compiler::JitProcessHandle handle,
      const runtime::simir::Process& process,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind> signal_value_kinds,
      std::shared_ptr<FrameStorage> storage,
      const fsim_jit_frame_v1& parent_frame,
      runtime::simir::InstructionIndex start_instruction);

  struct CallbackState {
    LlvmProcessExecutor* executor{};
    runtime::simir::ProcessExecutionContext* context{};
    const runtime::simir::Process* process{};
    std::span<const std::uint32_t> signal_widths;
    std::span<const runtime::simir::ValueKind> signal_value_kinds;
    std::exception_ptr failure;
  };

  template <typename Boundary>
  void require_boundary(
      const runtime::simir::InstructionIndex instruction,
      const std::string_view status) const;

  static void capture_failure(CallbackState& state) noexcept;
  static void capture_file_failure(
      CallbackState&, std::uint32_t, std::uint32_t) noexcept;

  static std::uint32_t load_string(
      void*, std::uint32_t, const char*, std::uint64_t) noexcept;
  static std::uint32_t copy_string(
      void*, std::uint32_t, std::uint32_t) noexcept;
  static std::uint32_t read_string_object(
      void*, std::uint32_t, std::uint32_t) noexcept;
  static std::uint32_t write_string_object(
      void*, std::uint32_t, std::uint32_t) noexcept;
  static std::uint32_t concatenate_strings(
      void*, std::uint32_t, std::uint32_t, std::uint32_t,
      const std::uint32_t*, std::uint32_t) noexcept;
  static std::uint32_t compare_strings(
      void*, std::uint32_t, std::uint32_t, std::uint32_t,
      std::uint32_t*) noexcept;
  static std::uint32_t string_length(
      void*, std::uint32_t, std::uint32_t*) noexcept;
  static std::uint32_t string_index(
      void*, std::uint32_t, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t, std::uint32_t,
      std::uint32_t*) noexcept;
  static std::uint32_t string_replace_byte(
      void*, std::uint32_t, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t, std::uint32_t,
      std::uint64_t, std::uint64_t) noexcept;
  static std::uint32_t write_string_output(
      void*, std::uint32_t, std::uint32_t, const char*, std::uint64_t,
      const char*, std::uint64_t, std::uint32_t, std::uint32_t) noexcept;
  static std::uint32_t file_open(
      void*, std::uint32_t, std::uint32_t, std::uint32_t*) noexcept;
  static std::uint32_t file_close(
      void*, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t) noexcept;
  static std::uint32_t file_write(
      void*, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t) noexcept;
  static std::uint32_t file_read_line(
      void*, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t, std::uint32_t*) noexcept;
  static std::uint32_t file_end_of_file(
      void*, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t, std::uint32_t*) noexcept;
  static std::uint32_t file_error(
      void*, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t, std::uint32_t*) noexcept;
  static std::uint32_t container_operation(
      void*, std::uint32_t, std::uint32_t,
      std::uint64_t, std::uint64_t,
      std::uint64_t, std::uint64_t,
      std::uint64_t*, std::uint64_t*) noexcept;
  static runtime::simir::FileHandle checked_file_handle(
      std::uint64_t aval, std::uint64_t bval);
  static const runtime::simir::Operation& callback_operation(
      const CallbackState&, std::uint32_t, std::uint32_t);

  static std::uint64_t read_signal(
      void* context,
      const std::uint32_t signal,
      std::uint64_t* bval) noexcept;

  static void read_signal_logic9(
      void* context,
      const std::uint32_t signal,
      fsim_jit_logic9_word_v1* result) noexcept;

  static void write_signal(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval) noexcept;

  static void write_signal_logic9(
      void* context,
      const std::uint32_t signal,
      const fsim_jit_logic9_word_v1* value) noexcept;

  static void write_update(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval) noexcept;

  static void write_update_logic9(
      void* context,
      const std::uint32_t signal,
      const fsim_jit_logic9_word_v1* value) noexcept;

  static void write_after(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval,
      const std::uint64_t delay) noexcept;

  static void write_after_logic9(
      void* context,
      const std::uint32_t signal,
      const fsim_jit_logic9_word_v1* value,
      const std::uint64_t delay) noexcept;

  static void write_inertial(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval,
      const std::uint64_t rise_delay,
      const std::uint64_t fall_delay,
      const std::uint64_t turnoff_delay) noexcept;

  static void write_inertial_logic9(
      void* context,
      const std::uint32_t signal,
      const fsim_jit_logic9_word_v1* value,
      const std::uint64_t rise_delay,
      const std::uint64_t fall_delay,
      const std::uint64_t turnoff_delay) noexcept;

  static void write_projected(
      void* context,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval,
      const std::uint64_t delay,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  static void write_projected_logic9(
      void* context,
      const std::uint32_t signal,
      const fsim_jit_logic9_word_v1* value,
      const std::uint64_t delay,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  static void write_signal_slice(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const std::uint64_t aval,
      const std::uint64_t bval) noexcept;

  static void write_signal_slice_logic9(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_logic9_word_v1* value) noexcept;

  static void force_signal_slice(
      void* context,
      std::uint32_t signal,
      std::uint32_t offset,
      std::uint32_t width,
      std::uint64_t aval,
      std::uint64_t bval) noexcept;

  static void force_signal_slice_logic9(
      void* context,
      std::uint32_t signal,
      std::uint32_t offset,
      std::uint32_t width,
      const fsim_jit_logic9_word_v1* value) noexcept;

  static void release_signal_slice(
      void* context,
      std::uint32_t signal,
      std::uint32_t offset,
      std::uint32_t width) noexcept;

  static void write_update_slice(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const std::uint64_t aval,
      const std::uint64_t bval) noexcept;

  static void write_update_slice_logic9(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_logic9_word_v1* value) noexcept;

  static void write_after_slice(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const std::uint64_t aval,
      const std::uint64_t bval,
      const std::uint64_t delay) noexcept;

  static void write_after_slice_logic9(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_logic9_word_v1* value,
      const std::uint64_t delay) noexcept;

  static void write_inertial_slice(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const std::uint64_t aval,
      const std::uint64_t bval,
      const std::uint64_t rise_delay,
      const std::uint64_t fall_delay,
      const std::uint64_t turnoff_delay) noexcept;

  static void write_inertial_slice_logic9(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_logic9_word_v1* value,
      const std::uint64_t rise_delay,
      const std::uint64_t fall_delay,
      const std::uint64_t turnoff_delay) noexcept;

  static void write_projected_slice(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const std::uint64_t aval,
      const std::uint64_t bval,
      const std::uint64_t delay,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  static void write_projected_slice_logic9(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_logic9_word_v1* value,
      const std::uint64_t delay,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  static void write_projected_waveform(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t width,
      const fsim_jit_projected_element_v1* elements,
      const std::uint32_t count,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  static void write_projected_waveform_logic9(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t width,
      const fsim_jit_logic9_projected_element_v1* elements,
      const std::uint32_t count,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  static void write_projected_waveform_slice(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_projected_element_v1* elements,
      const std::uint32_t count,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  static void write_projected_waveform_slice_logic9(
      void* context,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_logic9_projected_element_v1* elements,
      const std::uint32_t count,
      const std::uint64_t rejection,
      const std::uint32_t mode) noexcept;

  [[nodiscard]] static runtime::simir::ProjectedDelayMode
  projected_delay_mode(const std::uint32_t mode);

  [[nodiscard]] static runtime::Logic4Word checked_write_word(
      const CallbackState& state,
      const std::uint32_t signal,
      const std::uint64_t aval,
      const std::uint64_t bval);

  static void clear_logic9_word(
      fsim_jit_logic9_word_v1* value) noexcept;

  static void require_logic9_signal(
      const CallbackState& state,
      const std::uint32_t signal,
      const fsim_jit_logic9_word_v1* value);

  [[nodiscard]] static PackedLogic4 checked_logic9_value(
      const CallbackState& state,
      const std::uint32_t signal,
      const fsim_jit_logic9_word_v1* value);

  [[nodiscard]] static PackedLogic4 checked_logic9_slice(
      const CallbackState& state,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const fsim_jit_logic9_word_v1* value);

  [[nodiscard]] static runtime::Logic4Word checked_slice_word(
      const CallbackState& state,
      const std::uint32_t signal,
      const std::uint32_t offset,
      const std::uint32_t width,
      const std::uint64_t aval,
      const std::uint64_t bval);

  static void assert_failed(
      void* context,
      std::uint32_t,
      std::uint32_t,
      const char*,
      std::uint64_t) noexcept;

  static std::uint32_t signal_event(
      void* context,
      const std::uint32_t signal) noexcept;

  static std::uint64_t signal_last_value(
      void* context,
      const std::uint32_t signal,
      std::uint64_t* bval) noexcept;

  static void signal_last_value_logic9(
      void* context,
      const std::uint32_t signal,
      fsim_jit_logic9_word_v1* result) noexcept;

  static std::uint64_t signal_last_event(
      void* context,
      const std::uint32_t signal) noexcept;

  static std::uint32_t signal_active(
      void* context,
      const std::uint32_t signal) noexcept;

  static void write_output(
      void* context,
      const std::uint32_t,
      const char* text,
      const std::uint64_t text_size,
      const std::uint32_t newline) noexcept;

  static void schedule_output(
      void* context,
      const std::uint32_t,
      const char* text,
      const std::uint64_t text_size,
      const std::uint32_t newline) noexcept;

  static void write_report(
      void* context,
      const std::uint32_t process,
      const std::uint32_t instruction) noexcept;

  static void write_formatted(
      void* context,
      const std::uint32_t process,
      const std::uint32_t instruction,
      const std::uint32_t width,
      const std::uint64_t aval,
      const std::uint64_t bval) noexcept;

  static void write_formatted_logic9(
      void* context,
      const std::uint32_t process,
      const std::uint32_t instruction,
      const std::uint32_t width,
      const fsim_jit_logic9_word_v1* value) noexcept;

  static void write_time(
      void* context,
      const std::uint32_t process,
      const std::uint32_t instruction) noexcept;

  static void install_monitor(
      void* context,
      const std::uint32_t process,
      const std::uint32_t instruction) noexcept;

  static void control_monitor(
      void* context,
      const std::uint32_t process,
      const std::uint32_t instruction) noexcept;

  static std::uint64_t random_value(
      void* context,
      const std::uint32_t process,
      const std::uint32_t instruction,
      const std::uint64_t maximum_aval,
      const std::uint64_t maximum_bval,
      const std::uint64_t minimum_aval,
      const std::uint64_t minimum_bval,
      std::uint64_t* result_bval) noexcept;

  compiler::LlvmJit& jit_;
  compiler::JitProcessHandle handle_;
  const runtime::simir::Process& process_;
  std::span<const std::uint32_t> signal_widths_;
  std::span<const runtime::simir::ValueKind> signal_value_kinds_;
  std::shared_ptr<FrameStorage> storage_;
  fsim_jit_frame_v1 frame_{};
  std::vector<std::uint64_t>& register_aval_;
  std::vector<std::uint64_t>& register_bval_;
  std::vector<std::uint64_t>& register_logic9_plane2_;
  std::vector<std::uint64_t>& register_logic9_plane3_;
  std::vector<std::uint8_t>& register_initialized_;
  std::vector<std::string>& string_registers_;
  std::vector<runtime::simir::ContainerValue>& container_registers_;
};

[[nodiscard]] compiler::JitOptimizationLevel jit_optimization(
    const project::Optimization optimization) noexcept;

#endif

extern std::atomic_bool interrupt_requested;
static_assert(
    std::atomic_bool::is_always_lock_free,
    "the supported Ctrl-C path requires a lock-free atomic flag");

extern "C" void handle_interrupt(int);

class InterruptSignalGuard final {
 public:
  InterruptSignalGuard() noexcept;

  ~InterruptSignalGuard();

  InterruptSignalGuard(const InterruptSignalGuard&) = delete;
  InterruptSignalGuard& operator=(const InterruptSignalGuard&) = delete;

 private:
  using Handler = void (*)(int);
  Handler previous_{SIG_ERR};
};

diagnostic::SourcePosition position(const frontend::SourceLocation& source);

diagnostic::SourceSpan span(const frontend::SourceSpan& source);

void import_diagnostic(
    diagnostic::Engine& output,
    const frontend::Diagnostic& input);

frontend::Language frontend_language(const project::Language language);

struct ParseInput {
  std::filesystem::path path;
  frontend::Language language{frontend::Language::SystemVerilog2017};
  std::string library{"work"};
  std::size_t source_order{};
};

struct ParseGroup {
  frontend::Language language{frontend::Language::SystemVerilog2017};
  std::string standard;
  std::vector<ParseInput> inputs;
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
};

struct ParsedSnapshot {
  frontend::ParseResult result;
  std::vector<CheckedSource> sources;
  std::vector<std::size_t> unit_source_orders;
};

bool same_source_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right);

std::string compilation_unit_digest(
    const std::vector<frontend::PreprocessedRoot>& roots,
    const std::vector<frontend::PreprocessedDependency>& inputs);

ParsedSnapshot parse_group_snapshot(const ParseGroup& group);

bool load_required_mapped_libraries(
    const project::Config& config,
    CheckedProject& checked,
    diagnostic::Engine& diagnostics);

std::optional<systemc::PluginCompileRequest> systemc_request(
    const project::Config& config);

struct SystemCLibraryCompileRequest {
  std::string library;
  systemc::PluginCompileRequest request;
};

std::vector<SystemCLibraryCompileRequest> systemc_requests(
    const project::Config& config);

std::shared_ptr<systemc::HierarchyRegistry> load_systemc_plugin(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics);

struct SystemCLibraryRegistry {
  std::string library;
  std::shared_ptr<systemc::HierarchyRegistry> registry;
};

std::string unit_key(const frontend::DesignUnit& unit);

void validate_vhdl_analysis_order(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

void inject_vhdl_standard_libraries(
    CheckedProject& checked,
    diagnostic::Engine& diagnostics);

std::vector<project::ProjectSection::TopLevel> selected_tops(
    const project::Config& config,
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics);

struct BindingTarget {
  std::string language;
  std::string qualifier;
  std::string unit;
};

std::optional<BindingTarget> parse_binding_target(std::string_view target);

frontend::PortDirection systemc_direction(
    const fsim_sc_port_direction_v1 direction);

frontend::Type systemc_type(
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width);

PackedLogic4 systemc_value(
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    const std::span<const std::uint8_t> storage);

elaboration::SystemCInstanceDescription systemc_description(
    const std::string_view path,
    const std::string_view target,
    const systemc::ModuleDescription& module);

std::optional<std::vector<elaboration::SystemCInstanceDescription>>
construct_systemc_instances(
    std::span<const project::ProjectSection::TopLevel> tops,
    std::span<const SystemCLibraryRegistry> registries,
    diagnostic::Engine& diagnostics);

class ApplicationSystemCFactoryProvider final
    : public elaboration::SystemCFactoryProvider {
public:
  ApplicationSystemCFactoryProvider(
      std::span<const SystemCLibraryRegistry> registries,
      const std::span<
          const elaboration::SystemCInstanceDescription> eager_instances,
      std::vector<std::uint64_t>& lifecycle_roots);

  [[nodiscard]] std::shared_ptr<systemc::HierarchyRegistry>
  registry_for_handle(std::uint64_t handle) const;

  std::vector<elaboration::SystemCFactoryCandidate>
  candidates() const override;

  std::vector<std::string> libraries() const override;

  std::optional<std::vector<
      elaboration::SystemCConstructionParameter>>
  schema(
      const std::string_view target,
      std::string& error) override;

  std::optional<elaboration::SystemCInstanceDescription>
  instantiate(
      const std::string_view path,
      const std::string_view target,
      const std::span<
          const std::pair<std::string, std::int64_t>>
          construction_values,
      std::string& error) override;

private:
  void record_handles(
      const std::string& path,
      const elaboration::SystemCInstanceDescription& description,
      systemc::HierarchyRegistry* registry);

  void record_handles(
      const std::string& path,
      const systemc::ModuleDescription& description,
      systemc::HierarchyRegistry* registry);

  struct HandleOwner {
    fsim_sc_handle_v1 handle{};
    systemc::HierarchyRegistry* registry{};
  };

  std::vector<SystemCLibraryRegistry> registries_;
  std::vector<std::uint64_t>& lifecycle_roots_;
  std::map<std::string, HandleOwner> handles_;
};

void validate_bindings(
    const project::Config& config,
    const frontend::ParsedDesign& parsed,
    std::span<const SystemCLibraryRegistry> systemc_registries,
    diagnostic::Engine& diagnostics);

std::string target_name();

std::string make_cache_key(
    const project::Config& config,
    const CheckedProject& checked,
    std::span<const project::ProjectSection::TopLevel> tops,
    const std::string_view resolution,
    const std::string_view systemc_plugin_key,
    diagnostic::Engine& diagnostics);

std::optional<std::vector<std::string>>
make_specialization_cache_keys(
    const project::Config& config,
    const CheckedProject& checked,
    const semantic::design::DesignIr& design,
    std::string_view systemc_plugin_key,
    diagnostic::Engine& diagnostics);

bool wildcard_match(std::string_view pattern, std::string_view text);

bool trace_selected(
    const std::vector<std::string>& filters,
    const std::string_view name);

struct VcdScale {
  std::string timescale;
  SimulationTick tick_multiplier{1};
};

std::optional<VcdScale> vcd_scale(
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

struct TraceState {
  std::ofstream stream;
  std::unique_ptr<runtime::VcdWriter> writer;
  std::vector<std::vector<runtime::VcdSignal>> handles;
  std::vector<bool> enabled;
  SimulationTick tick_multiplier{1};
};

std::unique_ptr<TraceState> attach_trace(
    Simulation& simulation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const bool dynamic_selection = false);

std::optional<SimulationTick> configured_duration(
    const project::Config& config,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

void install_interrupt_hook(Simulation& simulation);

void report_native_cache_failures(
    const Simulation& simulation,
    diagnostic::Engine& diagnostics);

int handle_check(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&);

int handle_build(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&);

int handle_run(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&);

int run_built_project(
    BuiltProject project,
    SimulationEngine engine,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output);

bool compile_object(
    const project::Config& config,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

int handle_compile(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_elaborate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_simulate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

void print_debug_help(std::ostream& output);

std::vector<std::string> words(const std::string& line);

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
      TraceState* trace = nullptr);

  ~DebuggerSession();

  DebuggerSession(const DebuggerSession&) = delete;
  DebuggerSession& operator=(const DebuggerSession&) = delete;

  void execute(const std::vector<std::string>& command);

 private:
  struct ExecutionGuard {
    bool& executing;
    ~ExecutionGuard();
  };

  [[nodiscard]] static std::string format_value(
      const PackedLogic4& value,
      const std::vector<std::string>& enumeration_literals);

  [[nodiscard]] bool canonical_path(const std::string_view path) const;

  [[nodiscard]] std::vector<std::string> lexical_paths(
      std::string_view name) const;

  [[nodiscard]] std::optional<std::string> resolve_scope(
      const std::string_view requested) const;

  [[nodiscard]] std::optional<std::pair<std::string, SignalId>>
  resolve_signal(const std::string_view name);

  [[nodiscard]] std::optional<std::pair<
      std::string, runtime::simir::StringObjectId>>
  resolve_string_object(std::string_view name) const;
  [[nodiscard]] std::optional<std::pair<
      std::string, runtime::simir::ContainerObjectId>>
  resolve_container_object(std::string_view name) const;

  [[nodiscard]] std::optional<SimulationTick> command_time(
      const std::string_view text);

  void scope_command(const std::vector<std::string>& command);

  void scopes_command(const std::vector<std::string>& command);

  void signals_command(const std::vector<std::string>& command);

  void modify_signal(const std::vector<std::string>& command);

  void show_locals();

  void trace_command(const std::vector<std::string>& command);

  void set_trace_enabled(const SignalId signal, const bool enable);

  void add_breakpoint(const std::vector<std::string>& command);

  void list_breakpoints() const;

  void delete_breakpoint(const std::string_view id_text);

  [[nodiscard]] std::optional<DebugBreakpoint> earliest_time_breakpoint(
      const SimulationTick start,
      const std::optional<SimulationTick> requested_limit) const;

  [[nodiscard]] static bool source_path_matches(
      const std::string_view requested,
      const std::string_view actual);

  [[nodiscard]] static bool is_statement_point(
      const runtime::simir::ExecutionPointKind kind) noexcept;

  void install_execution_hook(
      const std::optional<DebugBreakpoint>& time_breakpoint,
      const std::function<bool(runtime::Scheduler&, runtime::SchedulerPhase)>&
          additional_stop = {},
      const std::function<bool(const runtime::simir::ExecutionPoint&)>&
          additional_execution_stop = {});

  void report_execution_point();

  void report_result(const runtime::RunResult& result);

  [[nodiscard]] bool can_execute();

  void run(const std::optional<SimulationTick> requested_limit);

  void step(const bool delta_step);

  void step_execution(const bool process_step);

  Simulation& simulation_;
  std::ostream& output_;
  std::ostream& error_;
  TraceState* trace_{};
  std::string scope_;
  std::vector<std::pair<std::string, SignalId>> signal_paths_;
  std::vector<std::pair<
      std::string, runtime::simir::ContainerObjectId>>
      container_paths_;
  std::vector<std::string> execution_scope_paths_;
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
    TraceState* trace);

int handle_debug(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::istream& input,
    std::ostream& output,
    std::ostream& error_output);

struct ParsedMagnitude {
  std::uint64_t magnitude{};
  std::string unit;
};

std::optional<ParsedMagnitude> magnitude_and_unit(std::string_view text);

std::optional<std::uint64_t> unit_femtoseconds(std::string_view unit);

template <typename Function>
void visit_delay(frontend::Delay& delay, Function& function);

template <typename Function>
void visit_delays(
    std::vector<frontend::Statement>& statements,
    Function&& function);

void validate_vhdl_rejection_limits(
    const std::vector<frontend::Statement>& statements,
    diagnostic::Engine& diagnostics,
    bool& valid);

std::string effective_resolution(
    const project::Config& config,
    frontend::ParsedDesign& parsed);

bool normalize_delays(
    frontend::ParsedDesign& parsed,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

void select_delay_alternatives(
    frontend::ParsedDesign& parsed,
    const project::DelayMode mode);

bool validate_declared_time_precisions(
    const frontend::ParsedDesign& parsed,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);


} // namespace fsim::app::application_detail
