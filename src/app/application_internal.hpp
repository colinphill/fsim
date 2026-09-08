// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/app/application.hpp"

#include "tcl.hpp"

#include "fsim/compiler/object_cache.hpp"
#if defined(FSIM_HAS_LLVM)
#include "fsim/compiler/llvm_jit.hpp"
#endif
#include "fsim/frontend/coverage_percentage.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/preprocessor.hpp"
#include "fsim/runtime/class_randomize.hpp"
#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/incremental.hpp"
#include "fsim/systemc/plugin_compiler.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
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

namespace fsim::runtime {
class FstWriter;
}

namespace fsim::app::application_detail {

class VhdlPslExecution {
public:
    using SignalReader = std::function<runtime::PackedLogic4(
        runtime::simir::SignalId)>;

    VhdlPslExecution(
        const semantic::vhdl::Hir& hir,
        const elaboration::ElaboratedDesign& design,
        const semantic::design::DesignIr& design_ir,
        SignalReader reader);
    ~VhdlPslExecution();
    VhdlPslExecution(const VhdlPslExecution&) = delete;
    VhdlPslExecution& operator=(const VhdlPslExecution&) = delete;

    void observe(runtime::SimulationTick time, std::uint64_t delta);
    void finish(runtime::SimulationTick time, std::uint64_t delta);
    [[nodiscard]] const std::vector<runtime::VhdlPslAttemptSnapshot>&
    attempts() const noexcept;
    [[nodiscard]] std::vector<ConcurrentAssertionCoverage> coverage() const;
    [[nodiscard]] bool apply_api(
        runtime::simir::VhdlPslApiKind kind,
        std::optional<bool> enable = std::nullopt);
    void set_completion_hook(runtime::VhdlPslCompletionHook hook);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<runtime::VhdlVhpiObjectRegistry>
make_vhdl_debug_registry(const BuiltProject& project);

struct SystemVerilogVpiDriverBinding {
    fsim_vpi_handle_v1 handle { };
    runtime::simir::ProcessId process { };
    std::optional<runtime::SystemVerilogVpiDriveStrength> strength;
};

struct SystemVerilogVpiPublishedDesign {
    std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry> registry;
    std::map<runtime::simir::SignalId, std::vector<fsim_vpi_handle_v1>>
        signals;
    std::map<fsim_vpi_handle_v1, runtime::simir::SignalId> handles;
    std::map<runtime::simir::SignalId,
        std::vector<SystemVerilogVpiDriverBinding>>
        drivers;
    std::map<runtime::simir::SignalId, std::vector<fsim_vpi_handle_v1>> events;
    std::map<std::string, fsim_vpi_handle_v1, std::less<>> assertions;
    std::map<runtime::simir::SignalId, runtime::SystemVerilogScalarKind>
        scalar_kinds;
    std::map<runtime::simir::SignalId,
        runtime::SystemVerilogVpiValueCategory>
        categories;
    std::map<runtime::simir::ContainerObjectId,
        std::vector<std::pair<fsim_vpi_handle_v1, std::size_t>>>
        container_words;
    std::map<fsim_vpi_handle_v1,
        std::pair<runtime::simir::ContainerObjectId, std::size_t>>
        word_handles;
    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogScalarKind>
        container_scalar_kinds;
    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogVpiValueCategory>
        container_categories;
};

[[nodiscard]] SystemVerilogVpiPublishedDesign
make_systemverilog_vpi_design(
    const BuiltProject& project,
    const runtime::simir::Interpreter& interpreter);

[[nodiscard]] std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry>
make_empty_systemverilog_vpi_registry();

[[nodiscard]] runtime::SystemVerilogVpiTimeProfile
systemverilog_vpi_time_profile(std::string_view resolution);

[[nodiscard]] runtime::SystemVerilogVpiStoredValue
systemverilog_vpi_signal_value(
    runtime::PackedLogic4 value,
    runtime::SystemVerilogScalarKind scalar_kind,
    runtime::SystemVerilogVpiValueCategory category);

[[nodiscard]] runtime::PackedLogic4 systemverilog_vpi_packed_value(
    const runtime::SystemVerilogVpiStoredValue& value,
    runtime::SystemVerilogScalarKind scalar_kind,
    runtime::SystemVerilogVpiValueCategory category);

[[nodiscard]] std::string_view report_severity_name(
    const runtime::simir::AssertionSeverity severity) noexcept;

using runtime::PackedLogic4;
using runtime::SimulationTick;
using runtime::simir::SignalId;

[[nodiscard]] runtime::SystemVerilogClassPropertyDescriptor
class_property_descriptor(
    const frontend::SystemVerilogClassPropertyLayout& property,
    bool qualified_name = false);

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

void compose_vhdl_mode_views(semantic::vhdl::Hir& hir);

bool validate_vhdl_mode_view_hir(
    const semantic::vhdl::Hir& hir,
    diagnostic::Engine& diagnostics);

void complete_vhdl_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::vhdl::Hir& hir);

[[nodiscard]] semantic::sv::Hir build_systemverilog_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    std::span<frontend::SystemVerilogClassSpecialization>
        class_specializations = { });

[[nodiscard]] runtime::SystemVerilogConstraintVariableProfile
systemverilog_constraint_profile(
    const semantic::sv::ConstraintBinding& binding);

[[nodiscard]] std::optional<runtime::SystemVerilogConstraintExpression>
lower_systemverilog_constraint_expression(
    const semantic::sv::ConstraintExpression& expression,
    std::string_view specialization_identity,
    const std::map<std::string,
        runtime::SystemVerilogConstraintVariableId>& variables,
    std::vector<runtime::SystemVerilogConstraintVariableId>& dependencies,
    std::string& error,
    const std::map<std::string, std::vector<runtime::SystemVerilogConstraintVariableId>>& container_variables = { });

[[nodiscard]] std::optional<runtime::SystemVerilogConstraintDistribution>
lower_systemverilog_constraint_distribution(
    const semantic::sv::ConstraintExpression& expression,
    std::string_view specialization_identity,
    const std::map<std::string,
        runtime::SystemVerilogConstraintVariableId>& variables,
    std::span<const runtime::SystemVerilogConstraintVariable>
        solver_variables,
    std::string canonical_identity,
    std::string& error);

[[nodiscard]] std::optional<std::vector<std::pair<
    runtime::SystemVerilogConstraintVariableId,
    runtime::SystemVerilogConstraintVariableId>>>
lower_systemverilog_solve_before(
    const semantic::sv::ConstraintExpression& expression,
    std::string_view specialization_identity,
    const std::map<std::string,
        runtime::SystemVerilogConstraintVariableId>& variables,
    std::string& error);

void configure_systemverilog_class_constraints(
    runtime::SystemVerilogConstraintSolver& solver,
    const runtime::SystemVerilogClassRandomizeVariables& variables,
    const semantic::sv::Hir& hir,
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::function<bool(std::string_view)>& constraint_enabled = { });

[[nodiscard]] const frontend::SystemVerilogClassMethodProfile*
systemverilog_randomize_callback(
    std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    const runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogClassHandle handle,
    std::string_view name);

[[nodiscard]] runtime::PackedLogic4
invoke_systemverilog_randomization_mode(
    runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogClassHandle handle,
    std::string_view method,
    std::span<const runtime::PackedLogic4> actuals);

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
    const elaboration::ElaboratedDesign& runtime);

class SystemCProcessExecutor final
    : public runtime::simir::ProcessExecutor {
public:
    SystemCProcessExecutor(
        std::shared_ptr<systemc::HierarchyRegistry> hierarchy,
        const std::uint64_t process);

    [[nodiscard]] runtime::simir::ProcessResumeResult resume(
        runtime::simir::ProcessExecutionContext& context,
        runtime::simir::InstructionIndex) override;

private:
    std::shared_ptr<systemc::HierarchyRegistry> hierarchy_;
    std::uint64_t process_ { };
};

struct DebugCodeCoverageObservation {
    runtime::simir::InstructionIndex instruction { };
    runtime::CodeCoveragePointId point;
    runtime::CodeCoverageMetric metric {
        runtime::CodeCoverageMetric::Statement
    };
    runtime::CodeCoverageCounterId counter;
    std::uint64_t hits { };

    friend bool operator==(
        const DebugCodeCoverageObservation&,
        const DebugCodeCoverageObservation&)
        = default;
};

enum class DebugCodeCoverageSnapshotError : std::uint8_t {
    none,
    resource_limit,
    invalid_point,
    invalid_metric,
    duplicate_point,
    counter_out_of_range,
};

struct DebugCodeCoverageSnapshotResult {
    std::vector<DebugCodeCoverageObservation> observations;
    DebugCodeCoverageSnapshotError error {
        DebugCodeCoverageSnapshotError::none
    };
    runtime::simir::InstructionIndex instruction { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == DebugCodeCoverageSnapshotError::none;
    }
};

[[nodiscard]] DebugCodeCoverageSnapshotResult
debug_code_coverage_snapshot(
    const runtime::simir::Process& process,
    std::span<const std::uint64_t> counters,
    std::size_t maximum_points = 1U << 20U) noexcept;

#if defined(FSIM_HAS_LLVM)

class LlvmProcessExecutor final : public runtime::simir::ProcessExecutor {
public:
    using SignalRemap
        = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

    LlvmProcessExecutor(
        compiler::LlvmJit& jit,
        const compiler::JitProcessHandle handle,
        const runtime::simir::Process& process,
        std::span<const std::uint32_t> signal_widths,
        std::span<const runtime::simir::ValueKind> signal_value_kinds,
        std::span<const runtime::simir::ResolutionKind> signal_resolutions,
        std::shared_ptr<const SignalRemap> signal_remap = { },
        std::optional<runtime::simir::ProcessId> generated_process = { });

    [[nodiscard]] std::unique_ptr<runtime::simir::ProcessExecutor>
    fork_clone(
        runtime::simir::InstructionIndex start_instruction) override;

    void redirect(
        runtime::simir::InstructionIndex instruction) override;

    [[nodiscard]] runtime::simir::ProcessResumeResult resume(
        runtime::simir::ProcessExecutionContext& context,
        const runtime::simir::InstructionIndex start_instruction) override;

    [[nodiscard]] std::size_t resume_cohort(
        std::span<runtime::simir::ProcessCohortResumeEntry> entries) override;

    [[nodiscard]] std::size_t resume_region(
        std::span<runtime::simir::ProcessCohortResumeEntry> entries,
        std::span<const std::size_t> active_indices) override;

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override;

    [[nodiscard]] const void* cohort_domain() const noexcept override;

    [[nodiscard]] PackedLogic4 read_register(
        const runtime::simir::RegisterId id,
        const std::size_t width) const override;

    [[nodiscard]] PackedLogic4 snapshot_register(
        runtime::simir::RegisterId id) const override;

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
    void write_container_register_storage(
        runtime::simir::ContainerRegisterId id,
        std::shared_ptr<runtime::simir::ContainerValue> value) override;

private:
    static constexpr runtime::simir::ContainerObjectId invalid_container_object
        = std::numeric_limits<runtime::simir::ContainerObjectId>::max();

    struct FrameStorage {
        std::vector<std::uint64_t> register_aval;
        std::vector<std::uint64_t> register_bval;
        std::vector<std::uint64_t> register_logic9_plane2;
        std::vector<std::uint64_t> register_logic9_plane3;
        std::vector<std::uint8_t> register_initialized;
        std::vector<std::string> string_registers;
        std::vector<std::shared_ptr<runtime::simir::ContainerValue>>
            container_registers;
        std::vector<std::uint8_t> container_register_shared;
        std::vector<runtime::simir::ContainerObjectId> container_object_aliases;
        std::vector<runtime::simir::ContainerRegisterId>
            active_container_object_aliases;
        std::vector<runtime::simir::VitalMemoryState> vital_memories;
    };

    LlvmProcessExecutor(
        compiler::LlvmJit& jit,
        compiler::JitProcessBinding binding,
        const runtime::simir::Process& process,
        std::span<const std::uint32_t> signal_widths,
        std::span<const runtime::simir::ValueKind> signal_value_kinds,
        std::span<const runtime::simir::ResolutionKind> signal_resolutions,
        std::shared_ptr<const SignalRemap> signal_remap,
        runtime::simir::ProcessId generated_process,
        std::shared_ptr<FrameStorage> storage,
        const fsim_jit_frame_v1& parent_frame,
        runtime::simir::InstructionIndex start_instruction);

    struct CallbackState {
        LlvmProcessExecutor* executor { };
        runtime::simir::ProcessExecutionContext* context { };
        const runtime::simir::Process* process { };
        runtime::simir::ProcessId generated_process { };
        std::span<const std::uint32_t> signal_widths;
        std::span<const runtime::simir::ValueKind> signal_value_kinds;
        std::span<const SignalRemap::value_type> signal_remap;
        std::span<const std::uint32_t> dense_signal_remap;
        std::uint32_t dense_signal_remap_base { };
        std::array<std::span<const std::uint64_t>, 4>
            direct_signal_logic9_planes;
        bool supports_direct_word_updates { };
        std::exception_ptr failure;
        static constexpr std::size_t signal_read_cache_size = 4U;
        std::array<std::uint32_t, signal_read_cache_size>
            signal_read_cache_ids {
                std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max()
            };
        std::array<runtime::Logic4Word, signal_read_cache_size>
            signal_read_cache_words { };
    };

    template <typename Boundary>
    void require_boundary(
        const runtime::simir::InstructionIndex instruction,
        const std::string_view status) const;

    [[nodiscard]] const runtime::simir::ContainerValue& container_register_value(
        runtime::simir::ContainerRegisterId id,
        const runtime::simir::ProcessExecutionContext& context);
    [[nodiscard]] runtime::simir::ContainerValue& mutable_container_register_value(
        runtime::simir::ContainerRegisterId id,
        const runtime::simir::ProcessExecutionContext& context);
    void alias_container_register(
        runtime::simir::ContainerRegisterId destination,
        runtime::simir::ContainerObjectId object,
        const runtime::simir::ProcessExecutionContext& context);
    void materialize_container_object_aliases(
        const runtime::simir::ProcessExecutionContext& context);
    void discard_container_object_aliases() noexcept;
    void initialize_direct_read_signals();
    void initialize_code_coverage_hit_counters();
    void initialize_direct_update_slots();
    void initialize_buffered_logic9_updates();
    [[nodiscard]] bool buffer_logic9_update(
        runtime::simir::SignalId signal,
        std::uint32_t offset,
        std::uint32_t width,
        const fsim_jit_logic9_word_v1& value);
    void flush_buffered_logic9_updates(
        runtime::simir::ProcessExecutionContext& context);
    void flush_update_words(
        runtime::simir::ProcessExecutionContext& context);

    static void capture_failure(CallbackState& state) noexcept;
    static void invalidate_signal_read_cache(CallbackState& state) noexcept;
    static std::uint32_t record_code_coverage_counter(
        void* context,
        std::uint32_t process,
        std::uint32_t instruction,
        std::uint32_t counter) noexcept;
    [[nodiscard]] static std::uint32_t mapped_signal_sparse(
        const CallbackState& state, std::uint32_t signal);
    [[nodiscard]] static std::uint32_t mapped_signal(
        const CallbackState& state, const std::uint32_t signal)
    {
        if (!state.dense_signal_remap.empty()) {
            if (signal < state.dense_signal_remap_base) {
                return signal;
            }
            const auto offset = signal - state.dense_signal_remap_base;
            return offset < state.dense_signal_remap.size()
                ? state.dense_signal_remap[offset]
                : signal;
        }
        return state.signal_remap.empty()
            ? signal
            : mapped_signal_sparse(state, signal);
    }
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
    static std::uint32_t string_replace_code_point(
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
    static std::uint32_t container_read_word(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t,
        std::uint64_t*, std::uint64_t*) noexcept;
    static std::uint32_t container_write_word(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t,
        std::uint64_t, std::uint64_t) noexcept;
    static std::uint32_t container_read_packed(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t,
        std::uint64_t*, std::uint64_t*, std::uint32_t) noexcept;
    static std::uint32_t container_write_packed(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t,
        const std::uint64_t*, const std::uint64_t*, std::uint32_t) noexcept;
    static runtime::simir::FileHandle checked_file_handle(
        std::uint64_t aval, std::uint64_t bval);
    static const runtime::simir::Operation& callback_operation(
        const CallbackState&, std::uint32_t, std::uint32_t);

    static std::uint64_t read_signal(
        void* context,
        const std::uint32_t signal,
        std::uint64_t* bval) noexcept;

    static std::uint32_t read_signal_packed(
        void* context,
        std::uint32_t signal,
        std::uint32_t width,
        std::uint64_t* aval,
        std::uint64_t* bval,
        std::uint64_t* logic9_plane2,
        std::uint64_t* logic9_plane3) noexcept;

    static std::uint32_t read_signal_dynamic_part(
        void* context,
        std::uint32_t signal,
        std::uint32_t source_width,
        std::uint64_t base_aval,
        std::uint64_t base_bval,
        std::int64_t left,
        std::int64_t right,
        std::uint32_t base_offset,
        std::uint32_t width,
        std::uint32_t flags,
        fsim_jit_logic9_word_v1* result) noexcept;

    static std::uint32_t write_signal_packed(
        void* context,
        std::uint32_t signal,
        std::uint32_t offset,
        std::uint32_t width,
        std::uint32_t mode,
        std::uint64_t delay,
        const std::uint64_t* aval,
        const std::uint64_t* bval,
        const std::uint64_t* logic9_plane2,
        const std::uint64_t* logic9_plane3) noexcept;

    static void read_signal_logic9(
        void* context,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* result) noexcept;
    static void read_signal_logic9_identity(
        void* context,
        std::uint32_t signal,
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

    static void force_driver_signal_slice(
        void* context,
        std::uint32_t signal,
        std::uint32_t offset,
        std::uint32_t width,
        std::uint64_t aval,
        std::uint64_t bval) noexcept;

    static void force_driver_signal_slice_logic9(
        void* context,
        std::uint32_t signal,
        std::uint32_t offset,
        std::uint32_t width,
        const fsim_jit_logic9_word_v1* value) noexcept;

    static void release_driver_signal_slice(
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

    static std::uint32_t execute_signal_operation(
        void* context,
        std::uint32_t process,
        std::uint32_t instruction,
        fsim_jit_frame_v1* frame) noexcept;

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

    static std::uint64_t read_simulation_time(void* context) noexcept;

    static std::uint32_t vital_timing_check(
        void* context,
        std::uint32_t process,
        std::uint32_t instruction) noexcept;

    static void vital_delay(
        void* context,
        std::uint32_t process,
        std::uint32_t instruction) noexcept;

    static std::uint32_t signal_active(
        void* context,
        const std::uint32_t signal) noexcept;

    static std::uint64_t signal_last_active(
        void* context,
        const std::uint32_t signal) noexcept;

    static std::uint32_t signal_driving(
        void* context,
        const std::uint32_t signal) noexcept;

    static std::uint64_t signal_driving_value(
        void* context,
        const std::uint32_t signal,
        std::uint64_t* bval) noexcept;

    static void signal_driving_value_logic9(
        void* context,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* result) noexcept;

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
    compiler::JitProcessBinding binding_;
    const runtime::simir::Process& process_;
    std::span<const std::uint32_t> signal_widths_;
    std::span<const runtime::simir::ValueKind> signal_value_kinds_;
    std::span<const runtime::simir::ResolutionKind> signal_resolutions_;
    std::shared_ptr<const SignalRemap> signal_remap_;
    std::vector<std::uint32_t> dense_signal_remap_;
    std::uint32_t dense_signal_remap_base_ { };
    runtime::simir::ProcessId generated_process_ { };
    compiler::JitProcessFrameLayout layout_;
    std::shared_ptr<FrameStorage> storage_;
    fsim_jit_runtime_v1 runtime_ { };
    CallbackState callback_state_ { };
    fsim_jit_frame_v1 frame_ { };
    std::vector<std::uint64_t>& register_aval_;
    std::vector<std::uint64_t>& register_bval_;
    std::vector<std::uint64_t>& register_logic9_plane2_;
    std::vector<std::uint64_t>& register_logic9_plane3_;
    std::vector<std::uint8_t>& register_initialized_;
    std::vector<std::string>& string_registers_;
    std::vector<std::shared_ptr<runtime::simir::ContainerValue>>&
        container_registers_;
    std::vector<std::uint8_t>& container_register_shared_;
    std::vector<runtime::simir::ContainerObjectId>& container_object_aliases_;
    std::vector<runtime::simir::ContainerRegisterId>&
        active_container_object_aliases_;
    std::vector<std::uint32_t> direct_read_signals_;
    std::vector<std::uint32_t> code_coverage_hit_counters_;
    std::vector<std::uint32_t> direct_update_signals_;
    std::vector<fsim_jit_update_slot_v1> direct_update_slots_;
    std::uint64_t direct_update_writer_revision_
        { std::numeric_limits<std::uint64_t>::max() };
    bool stable_direct_update_suppression_allowed_ { true };
    std::vector<std::uint64_t> direct_update_active_words_;
    std::vector<std::uint64_t> direct_update_wide_aval_;
    std::vector<std::uint64_t> direct_update_wide_bval_;
    std::vector<std::uint64_t> direct_update_wide_mask_;
    std::vector<runtime::simir::ProcessUpdateSlotView>
        direct_update_slot_views_;
    std::vector<runtime::simir::ProcessUpdateWord> pending_update_words_;
    struct BufferedLogic9Update {
        runtime::simir::SignalId signal { };
        std::uint32_t width { };
        std::array<std::uint64_t, 4> planes { };
        std::uint64_t mask { };
    };
    std::vector<BufferedLogic9Update> buffered_logic9_updates_;
    std::vector<runtime::simir::ProcessLogic9UpdateSlotView>
        buffered_logic9_update_views_;
    enum class CohortResumeMode : std::uint8_t {
        normal,
        prepare,
        consume,
    };
    CohortResumeMode cohort_resume_mode_ { CohortResumeMode::normal };
    fsim_jit_resume_result_v1 cohort_resume_result_ { };
    std::uint32_t cohort_resume_status_ { };
    std::exception_ptr cohort_resume_failure_;
    std::vector<LlvmProcessExecutor*> cohort_members_;
    std::vector<runtime::simir::InstructionIndex>
        cohort_start_instructions_;
    std::vector<compiler::JitProcessCohortResumeEntry>
        cohort_native_entries_;
    std::vector<runtime::simir::ProcessUpdateSlotBatch>
        cohort_update_batches_;
    std::vector<runtime::simir::ProcessLogic9UpdateBatch>
        cohort_logic9_update_batches_;
    compiler::JitProcessCohortBinding cohort_binding_;
    std::vector<LlvmProcessExecutor*> region_members_;
    std::vector<runtime::simir::ProcessExecutionContext*> region_contexts_;
    std::vector<runtime::simir::InstructionIndex> region_start_instructions_;
    std::vector<compiler::JitProcessCohortResumeEntry>
        region_native_entries_;
    std::vector<runtime::simir::ProcessUpdateSlotBatch>
        region_update_batches_;
    std::vector<runtime::simir::ProcessLogic9UpdateBatch>
        region_logic9_update_batches_;
    std::vector<runtime::simir::ProcessUpdateSlotBatch>
        region_active_update_batches_;
    const runtime::simir::ProcessCohortResumeEntry*
        region_entries_identity_ { };
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
    Handler previous_ { SIG_ERR };
};

diagnostic::SourcePosition position(const frontend::SourceLocation& source);

diagnostic::SourceSpan span(const frontend::SourceSpan& source);

void import_diagnostic(
    diagnostic::Engine& output,
    const frontend::Diagnostic& input);

frontend::Language frontend_language(const project::Language language);
frontend::VhdlStandard frontend_vhdl_standard(
    project::VhdlStandard standard);
frontend::StandardRevision frontend_standard_revision(
    project::Language language,
    std::string_view standard);
std::string_view vhdl_compatibility_profile() noexcept;

struct ParseInput {
    std::filesystem::path path;
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    frontend::VhdlStandard vhdl_standard {
        frontend::VhdlStandard::Vhdl2008
    };
    frontend::StandardRevision standard_revision {
        frontend::StandardRevision::SystemVerilog2017
    };
    std::string standard { "2017" };
    std::string library { "work" };
    std::size_t source_order { };
};

struct ParseGroup {
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    frontend::StandardRevision standard_revision {
        frontend::StandardRevision::SystemVerilog2017
    };
    std::string standard;
    std::string compatibility_profile { "none" };
    std::vector<ParseInput> inputs;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::string> defines;
};

struct ParsedSnapshot {
    frontend::ParseResult result;
    std::vector<CheckedSource> sources;
    std::vector<std::size_t> unit_source_orders;
    std::vector<std::size_t> udp_source_orders;
    std::vector<std::size_t> class_source_orders;
    std::vector<std::size_t> class_method_source_orders;
};

bool same_source_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right);

std::string compilation_unit_digest(
    const std::vector<frontend::PreprocessedRoot>& roots,
    const std::vector<frontend::PreprocessedDependency>& inputs,
    std::string_view language,
    std::string_view standard,
    std::string_view compatibility_profile);

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

void report_vhdl_duplicate_design_unit(
    const frontend::DesignUnit& unit,
    diagnostic::Engine& diagnostics);

void validate_vhdl_analysis_order(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

void validate_vhdl_simulator_api(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

void validate_vhdl_mode_view_interfaces(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

void validate_vhdl_package_declarations(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_vhdl_profile_compatibility(
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics);

void inject_vhdl_standard_libraries(
    CheckedProject& checked,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::vector<library::VhdlPackageDependency>
vhdl_package_dependencies(const CheckedProject& checked);

[[nodiscard]] bool validate_vhdl_package_dependencies(
    std::span<const library::VhdlPackageDependency> archived,
    std::string_view artifact,
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
            const elaboration::SystemCInstanceDescription>
            eager_instances,
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
        fsim_sc_handle_v1 handle { };
        systemc::HierarchyRegistry* registry { };
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
    SimulationTick tick_multiplier { 1 };
};

std::optional<VcdScale> vcd_scale(
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

class TraceObservationRecorder;
class TraceSelectionControl;

enum class TraceTerminalStatus : std::uint8_t {
    open,
    complete,
    failed
};

struct TraceState {
    ~TraceState();

    std::ofstream stream;
    std::unique_ptr<runtime::VcdWriter> writer;
    std::unique_ptr<runtime::FstWriter> fst_writer;
    std::unique_ptr<runtime::TraceDeclarationModel> declarations;
    std::unique_ptr<TraceObservationRecorder> observations;
    std::unique_ptr<TraceSelectionControl> selection;
    std::shared_ptr<const TraceControlApplication> control;
    std::vector<std::vector<runtime::VcdSignal>> handles;
    std::vector<runtime::VcdSignal> declaration_handles;
    std::vector<runtime::TraceSignalId> signal_trace_ids;
    std::vector<runtime::SystemVerilogScalarKind> scalar_kinds;
    std::array<runtime::TraceSignalId, 7> uvm_activity_trace_ids { };
    Simulation* simulation { };
    diagnostic::Engine* diagnostics { };
    project::TraceFormat format { project::TraceFormat::vcd };
    std::filesystem::path output_path;
    std::filesystem::path lock_directory;
    std::filesystem::path staging_path;
    std::filesystem::path backup_path;
    std::uint64_t uvm_activity_observer { };
    SimulationTick tick_multiplier { 1 };
    TraceTerminalStatus terminal_status { TraceTerminalStatus::open };
    std::string terminal_diagnostic;
    bool collecting_fst_initial_values { true };
    bool terminal_diagnostic_reported { };
};

std::optional<project::TraceFormat> resolve_trace_format(
    const project::RunSection& run,
    diagnostic::Engine& diagnostics);

std::optional<std::int8_t> fst_timescale_exponent(
    std::string_view timescale,
    diagnostic::Engine& diagnostics);

bool prepare_trace_output(
    TraceState& state,
    const std::filesystem::path& output,
    diagnostic::Engine& diagnostics);

void fail_trace(
    TraceState& state,
    std::string_view message,
    diagnostic::Engine* diagnostics) noexcept;

bool flush_trace(TraceState& state, diagnostic::Engine& diagnostics);
bool finish_trace(TraceState& state, diagnostic::Engine& diagnostics);

struct HdlVcdState {
    ~HdlVcdState();
    std::ofstream stream;
    std::unique_ptr<runtime::VcdWriter> writer;
    std::filesystem::path file_root;
    std::filesystem::path path;
    Simulation* simulation { };
    std::function<void()> activate_observer;
    std::function<void(std::uint64_t)> remove_observer;
    std::function<SimulationTick()> current_time;
    std::vector<std::vector<runtime::VcdSignal>> handles;
    std::vector<bool> selected;
    std::vector<runtime::SystemVerilogScalarKind> scalar_kinds;
    std::vector<std::pair<std::string, runtime::simir::SignalId>> objects;
    std::vector<std::string> instances;
    std::uint64_t observer { };
    SimulationTick tick_multiplier { 1 };
    std::optional<SimulationTick> dumpvars_time;
    std::optional<std::uint64_t> byte_limit;
    bool inventory_initialized { };
    bool begun { };
    bool enabled { true };
    bool limit_reached { };

    struct ExtendedPort {
        std::string scope;
        std::string reference;
        runtime::simir::SignalId signal { };
        semantic::design::Direction direction {
            semantic::design::Direction::unknown
        };
        std::optional<std::pair<std::int64_t, std::int64_t>> range;
    };

    struct ExtendedFile {
        std::filesystem::path path;
        std::ofstream stream;
        std::vector<std::size_t> ports;
        SimulationTick tick_multiplier { 1 };
        std::uint64_t bytes_written { };
        std::optional<std::uint64_t> byte_limit;
        std::optional<SimulationTick> last_time;
        bool begun { };
        bool enabled { true };
        bool limit_reached { };
        bool closed { };
    };

    std::vector<ExtendedPort> extended_ports;
    std::vector<std::unique_ptr<ExtendedFile>> extended_files;
    std::set<std::string, std::less<>> extended_scopes;
    std::set<std::filesystem::path> extended_paths;
    std::optional<SimulationTick> dumpports_time;
};

void attach_hdl_vcd_control(
    Simulation& simulation,
    HdlVcdState& state,
    const std::filesystem::path& file_root);

std::unique_ptr<TraceState> attach_trace(
    Simulation& simulation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const bool dynamic_selection = false,
    std::shared_ptr<const TraceControlApplication> configured = { });

std::optional<SimulationTick> configured_duration(
    const project::Config& config,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

void install_interrupt_hook(Simulation& simulation);

void report_native_cache_failures(
    const Simulation& simulation,
    diagnostic::Engine& diagnostics,
    bool synchronize = true);

bool apply_uvm_command_line(
    Simulation& simulation,
    std::span<const std::string> plusargs,
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
    std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics,
    std::ostream& output);

bool compile_object(
    const project::Config& config,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

std::optional<CheckedProject> check_project_for_object(
    const project::Config& config,
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

int handle_systemc_compile(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_systemc_link(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_coverage_merge(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_coverage_report(
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
    phase,
    uvm,
};

struct DebugBreakpoint {
    std::uint64_t id { };
    DebugBreakpointKind kind { DebugBreakpointKind::time };
    SimulationTick time { };
    SignalId signal { };
    std::string path;
    std::uint32_t line { };
    std::optional<PackedLogic4> signal_condition;
    bool signal_condition_equal { true };
};

struct DebugBreakpointHit {
    std::uint64_t id { };
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

    void uvm_command(const std::vector<std::string>& command);

    void vhdl_command(const std::vector<std::string>& command);

    [[nodiscard]] std::vector<std::pair<
        std::string, runtime::SystemVerilogUvmPhaseState>>
    phase_states() const;

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
            additional_stop = { },
        const std::function<bool(const runtime::simir::ExecutionPoint&)>&
            additional_execution_stop = { });

    void report_execution_point();

    void report_result(const runtime::RunResult& result);

    [[nodiscard]] bool can_execute();

    void run(const std::optional<SimulationTick> requested_limit);

    void step(const bool delta_step);

    void step_phase();

    void step_execution(const bool process_step);

    Simulation& simulation_;
    std::ostream& output_;
    std::ostream& error_;
    TraceState* trace_ { };
    std::string scope_;
    std::vector<std::pair<std::string, SignalId>> signal_paths_;
    std::vector<std::pair<
        std::string, runtime::simir::ContainerObjectId>>
        container_paths_;
    std::vector<std::string> execution_scope_paths_;
    std::vector<DebugBreakpoint> breakpoints_;
    std::optional<DebugBreakpointHit> hit_;
    std::optional<runtime::simir::ExecutionPoint> current_execution_point_;
    std::vector<std::pair<
        std::string, runtime::SystemVerilogUvmPhaseState>>
        phase_states_;
    std::optional<std::string> phase_transition_;
    std::uint64_t next_breakpoint_ { 1 };
    std::uint64_t observer_ { };
    std::uint64_t uvm_observer_ { };
    bool executing_ { };
    bool stop_on_phase_transition_ { };
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
    std::uint64_t magnitude { };
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
