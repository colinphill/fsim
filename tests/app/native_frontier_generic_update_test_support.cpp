// SPDX-License-Identifier: Apache-2.0
#include "native_frontier_generic_update_test_support.hpp"

#include "../../src/app/application_internal.hpp"
#include "../../src/app/application_region_kernel_backend.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../runtime/runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::tests::app::frontier {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_); previous != nullptr) {
            previous_ = previous;
        }
#if defined(_WIN32)
        if (::_putenv_s(name_, value) != 0) {
#else
        if (::setenv(name_, value, 1) != 0) {
#endif
            throw std::runtime_error {
                "failed to enable generic frontier counters"
            };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(
            name_, previous_ ? previous_->c_str() : ""));
#else
        if (previous_) {
            static_cast<void>(::setenv(name_, previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_));
        }
#endif
    }

private:
    const char* name_ { };
    std::optional<std::string> previous_;
};

[[nodiscard]] std::vector<std::uint64_t> copy_words(
    const std::uint64_t* const words, const std::uint32_t count)
{
    if (words == nullptr || count == 0U) {
        return { };
    }
    return { words, words + count };
}

template <typename State>
[[nodiscard]] PackedLogic4 direct_current_value(
    State& state, const SignalId signal, const std::size_t width)
{
    const auto words = static_cast<std::size_t>((width + 63U) / 64U);
    const auto value_kind = state.signals.at(signal).value_kind;
    if (width <= 64U) {
        if (value_kind == ValueKind::logic9) {
            if (signal >= state.direct_signal_logic9_plane0.size()
                || signal >= state.direct_signal_logic9_plane1.size()
                || signal >= state.direct_signal_logic9_plane2.size()
                || signal >= state.direct_signal_logic9_plane3.size()) {
                throw std::logic_error {
                    "pending direct Logic9 signal has incomplete scalar planes"
                };
            }
            const auto scalar_plane = [signal](
                const std::vector<std::uint64_t>& plane) {
                return std::span<const std::uint64_t> {
                    plane.data() + signal, 1U };
            };
            return PackedLogic4::from_logic9_word_planes(width,
                scalar_plane(state.direct_signal_logic9_plane0),
                scalar_plane(state.direct_signal_logic9_plane1),
                scalar_plane(state.direct_signal_logic9_plane2),
                scalar_plane(state.direct_signal_logic9_plane3));
        }
        if (signal >= state.direct_signal_aval.size()
            || signal >= state.direct_signal_bval.size()) {
            throw std::logic_error {
                "pending direct signal has no scalar value planes"
            };
        }
        return PackedLogic4::from_aval_bval(width,
            state.direct_signal_aval[signal], state.direct_signal_bval[signal]);
    }
    if (signal >= state.direct_wide_signal_offsets.size()) {
        throw std::logic_error {
            "pending direct signal has no wide value offset"
        };
    }
    const auto offset = static_cast<std::size_t>(
        state.direct_wide_signal_offsets[signal]);
    if (offset > state.direct_wide_signal_aval.size()
        || words > state.direct_wide_signal_aval.size() - offset
        || offset > state.direct_wide_signal_bval.size()
        || words > state.direct_wide_signal_bval.size() - offset) {
        throw std::logic_error {
            "pending direct signal wide planes are truncated"
        };
    }
    if (value_kind == ValueKind::logic9) {
        if (offset > state.direct_wide_signal_logic9_plane2.size()
            || words
                > state.direct_wide_signal_logic9_plane2.size() - offset
            || offset > state.direct_wide_signal_logic9_plane3.size()
            || words
                > state.direct_wide_signal_logic9_plane3.size() - offset) {
            throw std::logic_error {
                "pending direct Logic9 signal wide planes are truncated"
            };
        }
        const auto plane0
            = std::span<const std::uint64_t> { state.direct_wide_signal_aval }
                  .subspan(offset, words);
        const auto plane1
            = std::span<const std::uint64_t> { state.direct_wide_signal_bval }
                  .subspan(offset, words);
        const auto plane2 = std::span<const std::uint64_t> {
            state.direct_wide_signal_logic9_plane2 }.subspan(offset, words);
        const auto plane3 = std::span<const std::uint64_t> {
            state.direct_wide_signal_logic9_plane3 }.subspan(offset, words);
        return PackedLogic4::from_logic9_word_planes(
            width, plane0, plane1, plane2, plane3);
    }
    return PackedLogic4::from_word_planes(width,
        std::span<const std::uint64_t> { state.direct_wide_signal_aval }
            .subspan(offset, words),
        std::span<const std::uint64_t> { state.direct_wide_signal_bval }
            .subspan(offset, words));
}

template <typename State>
[[nodiscard]] std::array<PackedLogic4, 3U> signal_value_roles(
    State& state, const SignalId signal)
{
    const auto width = state.signals.at(signal).initial_value.width();
    const auto* const authoritative
        = state.region_authoritative_state_for_signal(signal);
    if (authoritative != nullptr
        && authoritative->values().packed_signal_slots_bound(signal)) {
        const auto& values = authoritative->values();
        return { values.current(signal), values.previous(signal),
            values.stored(signal) };
    }

    const bool direct_pending
        = signal < state.direct_signal_materialization_pending.size()
        && state.direct_signal_materialization_pending[signal] != 0U;
    if (direct_pending) {
        auto current = direct_current_value(state, signal, width);
        PackedLogic4 last;
        if (width <= 64U && state.signals.at(signal).value_kind == ValueKind::logic9) {
            if (signal >= state.direct_signal_last_logic9_plane0.size()
                || signal >= state.direct_signal_last_logic9_plane1.size()
                || signal >= state.direct_signal_last_logic9_plane2.size()
                || signal >= state.direct_signal_last_logic9_plane3.size()) {
                throw std::logic_error {
                    "pending direct Logic9 signal has incomplete scalar LAST planes"
                };
            }
            const auto scalar_plane = [signal](
                const std::vector<std::uint64_t>& plane) {
                return std::span<const std::uint64_t> {
                    plane.data() + signal, 1U };
            };
            last = PackedLogic4::from_logic9_word_planes(width,
                scalar_plane(state.direct_signal_last_logic9_plane0),
                scalar_plane(state.direct_signal_last_logic9_plane1),
                scalar_plane(state.direct_signal_last_logic9_plane2),
                scalar_plane(state.direct_signal_last_logic9_plane3));
        } else if (width <= 64U) {
            last = PackedLogic4::from_aval_bval(width,
                state.direct_signal_last_aval.at(signal),
                state.direct_signal_last_bval.at(signal));
        } else {
            last = state.signal_last_values.at(signal);
        }
        return { current, std::move(last), std::move(current) };
    }
    return { state.signals.at(signal).initial_value,
        state.signal_last_values.at(signal), state.driven_values.at(signal) };
}

class CheckedSuffixExecutor final : public ProcessExecutor {
public:
    CheckedSuffixExecutor(const Process& process,
        const ProcessId id, const SignalId input, const SignalId output,
        std::size_t& resumes)
        : input_(input)
        , output_(output)
        , resumes_(resumes)
        , binding_(process, process, id)
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start_instruction) override
    {
        if (start_instruction != 0U && start_instruction != 3U) {
            throw std::logic_error {
                "checked suffix resumes only at its body or wait boundary"
            };
        }
        ++resumes_;
        context.write_update_slice(output_, context.read_signal(input_), 0U);
        ProcessResumeResult result { 2U, 3U };
        result.external.kind = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    SignalId input_ { };
    SignalId output_ { };
    std::size_t& resumes_;
    ProcessExecutorProgramBinding binding_;
};

struct RegionCompletionCallCounters final {
    std::atomic<std::size_t> resume_calls { };
    std::atomic<std::size_t> prepare_calls { };
    std::atomic<std::size_t> stage_calls { };
    std::atomic<std::size_t> commit_calls { };
};

class CountingPreparedRegionCompletion final
    : public ProcessExecutor::PreparedRegionCompletion {
public:
    CountingPreparedRegionCompletion(
        std::unique_ptr<ProcessExecutor::PreparedRegionCompletion> delegate,
        std::shared_ptr<RegionCompletionCallCounters> counters)
        : delegate_(std::move(delegate))
        , counters_(std::move(counters))
    {
    }

    [[nodiscard]] const void* storage_identity() const noexcept override
    {
        return delegate_->storage_identity();
    }

    void commit() noexcept override
    {
        counters_->commit_calls.fetch_add(1U, std::memory_order_relaxed);
        delegate_->commit();
    }

private:
    std::unique_ptr<ProcessExecutor::PreparedRegionCompletion> delegate_;
    std::shared_ptr<RegionCompletionCallCounters> counters_;
};

class ParkedCompletionCountingExecutor final
    : public ProcessExecutor
    , public RegionKernelParkedExecutor {
public:
    ParkedCompletionCountingExecutor(
        std::unique_ptr<ProcessExecutor> executor,
        std::shared_ptr<RegionCompletionCallCounters> counters,
        const bool certify_parked)
        : executor_(std::move(executor))
        , parked_(dynamic_cast<const RegionKernelParkedExecutor*>(
              executor_.get()))
        , counters_(std::move(counters))
        , certify_parked_(certify_parked)
    {
        if (!executor_ || parked_ == nullptr || !counters_) {
            throw std::invalid_argument {
                "parked completion counter needs a certified delegate"
            };
        }
    }

    [[nodiscard]] std::unique_ptr<ProcessExecutor> fork_clone(
        const InstructionIndex instruction) override
    {
        auto executor = executor_->fork_clone(instruction);
        if (!executor) {
            return { };
        }
        return std::make_unique<ParkedCompletionCountingExecutor>(
            std::move(executor), counters_, certify_parked_);
    }

    void redirect(const InstructionIndex instruction) override
    {
        executor_->redirect(instruction);
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex instruction) override
    {
        counters_->resume_calls.fetch_add(1U, std::memory_order_relaxed);
        return executor_->resume(context, instruction);
    }

    [[nodiscard]] std::size_t resume_cohort(
        std::span<ProcessCohortResumeEntry>) override
    {
        return 0U;
    }

    [[nodiscard]] std::size_t resume_ordered_cohort(
        std::span<ProcessCohortResumeEntry>) override
    {
        return 0U;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return executor_->cohort_manages_process_state();
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return executor_->cohort_domain();
    }

    void update_channel(const std::uint64_t channel,
        ProcessExecutionContext& context) override
    {
        executor_->update_channel(channel, context);
    }

    [[nodiscard]] PackedLogic4 read_register(
        const RegisterId id, const std::size_t width) const override
    {
        return executor_->read_register(id, width);
    }

    [[nodiscard]] PackedLogic4 snapshot_register(
        const RegisterId id) const override
    {
        return executor_->snapshot_register(id);
    }

    void write_register(
        const RegisterId id, const PackedLogic4& value) override
    {
        executor_->write_register(id, value);
    }

    [[nodiscard]] std::string read_string_register(
        const StringRegisterId id) const override
    {
        return executor_->read_string_register(id);
    }

    void write_string_register(
        const StringRegisterId id, const std::string_view value) override
    {
        executor_->write_string_register(id, value);
    }

    [[nodiscard]] ContainerValue read_container_register(
        const ContainerRegisterId id) const override
    {
        return executor_->read_container_register(id);
    }

    void write_container_register(
        const ContainerRegisterId id,
        const ContainerValue& value) override
    {
        executor_->write_container_register(id, value);
    }

    void write_container_register_storage(
        const ContainerRegisterId id,
        std::shared_ptr<ContainerValue> value) override
    {
        executor_->write_container_register_storage(id, std::move(value));
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return executor_->program_access_binding();
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return executor_->region_kernel_equivalent();
    }

    [[nodiscard]] bool region_kernel_completion_has_no_persistent_registers()
        const noexcept override
    {
        return executor_->region_kernel_completion_has_no_persistent_registers();
    }

    [[nodiscard]] bool region_kernel_completion_is_parked_native(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::size_t activation_register_count) const noexcept override
    {
        return certify_parked_ && parked_ != nullptr
            && parked_->region_kernel_completion_is_parked_native(process,
            wait_instruction, jump_instruction, register_bindings,
            activation_register_count);
    }

    [[nodiscard]] std::unique_ptr<PreparedRegionCompletion>
    prepare_region_completion(const ProcessId process,
        const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::span<const PackedLogic4> activation_registers) override
    {
        counters_->prepare_calls.fetch_add(1U, std::memory_order_relaxed);
        auto completion = executor_->prepare_region_completion(process,
            wait_instruction, jump_instruction, register_bindings,
            activation_registers);
        if (!completion) {
            return { };
        }
        return std::make_unique<CountingPreparedRegionCompletion>(
            std::move(completion), counters_);
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::size_t activation_register_count,
        const void** storage_identity) noexcept override
    {
        counters_->prepare_calls.fetch_add(1U, std::memory_order_relaxed);
        return executor_->prepare_region_completion_native(process,
            wait_instruction, jump_instruction, register_bindings,
            activation_register_count, storage_identity);
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> activation_registers) noexcept
        override
    {
        counters_->stage_calls.fetch_add(1U, std::memory_order_relaxed);
        return executor_->stage_region_completion_native(
            activation_registers);
    }

    void commit_region_completion_native() noexcept override
    {
        counters_->commit_calls.fetch_add(1U, std::memory_order_relaxed);
        executor_->commit_region_completion_native();
    }

    void cancel_region_completion_native() noexcept override
    {
        executor_->cancel_region_completion_native();
    }

private:
    std::unique_ptr<ProcessExecutor> executor_;
    const RegionKernelParkedExecutor* parked_ { };
    std::shared_ptr<RegionCompletionCallCounters> counters_;
    bool certify_parked_ { true };
};

class CapturingRegionFrontierBackend final : public RegionFrontierBackend {
public:
    CapturingRegionFrontierBackend(
        std::unique_ptr<RegionFrontierBackend> backend,
        Scheduler& scheduler, GenericFrontierEntryCapture& capture)
        : backend_(std::move(backend))
        , scheduler_(scheduler)
        , capture_(capture)
    {
    }

    [[nodiscard]] RegionFrontierStepEntryV2
    step_entry() const noexcept override
    {
        capture_ = { };
        active_ = this;
        return &invoke;
    }

    [[nodiscard]] const RegionFrontierLayoutV2&
    layout() const noexcept override
    {
        return backend_->layout();
    }

private:
    [[nodiscard]] static RegionFrontierStatusV2 invoke(
        RegionFrontierFrameV2* const frame) noexcept
    {
        const auto* const self = std::exchange(active_, nullptr);
        if (self == nullptr || frame == nullptr) {
            return RegionFrontierStatusV2::decline_before_mutation;
        }

        auto& capture = self->capture_;
        capture.called = true;
        if (const auto frontier
            = self->scheduler_.current_generic_batch_frontier()) {
            capture.frontier_present = true;
            capture.generation = frontier->generation;
            capture.time = frontier->time;
            capture.delta = frontier->delta;
            capture.phase = frontier->phase;
            capture.frontier_cursor = frontier->cursor;
            capture.frontier_end = frontier->end;
            capture.borrowed_task_count = frontier->tasks.size();
            const auto copied = std::min(frontier->tasks.size(),
                capture.borrowed_tasks.size());
            capture.tasks_truncated = copied != frontier->tasks.size();
            for (std::size_t index = 0U; index < copied; ++index) {
                const auto& task = frontier->tasks[index];
                capture.borrowed_tasks[index] = {
                    { task.stable_order, task.sequence, task.payload },
                    static_cast<ProcessId>(task.payload
                        & kRegionFrontierPayloadIndexMaskV2) };
            }
        }

        const auto entry = self->backend_->step_entry();
        const auto status = entry == nullptr
            ? RegionFrontierStatusV2::decline_before_mutation
            : entry(frame);
        capture.frame_task_count = frame->scheduler_task_count;
        capture.frame_task_cursor = frame->scheduler_task_cursor;
        capture.status = status;
        return status;
    }

    std::unique_ptr<RegionFrontierBackend> backend_;
    Scheduler& scheduler_;
    GenericFrontierEntryCapture& capture_;
    static thread_local const CapturingRegionFrontierBackend* active_;
};

thread_local const CapturingRegionFrontierBackend*
    CapturingRegionFrontierBackend::active_ { };

class CapturingRegionKernelProvider final
    : public RegionKernelBackendProvider
    , public RegionFrontierBackendProvider
    , public RegionConeForwardingBackendProvider {
public:
    CapturingRegionKernelProvider(
        std::shared_ptr<RegionKernelBackendProvider> provider,
        Scheduler& scheduler, GenericFrontierEntryCapture& capture)
        : provider_(std::move(provider))
        , frontier_provider_(dynamic_cast<RegionFrontierBackendProvider*>(
              provider_.get()))
        , forwarding_provider_(
              dynamic_cast<RegionConeForwardingBackendProvider*>(
                  provider_.get()))
        , scheduler_(scheduler)
        , capture_(capture)
    {
        if (!provider_ || frontier_provider_ == nullptr) {
            throw std::invalid_argument {
                "production LLVM provider lacks its frontier capability"
            };
        }
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return provider_->identity();
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        return provider_->create(kernel);
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend> create_frontier(
        const RegionConeActivationKernel& kernel) override
    {
        auto backend = frontier_provider_->create_frontier(kernel);
        if (!backend) {
            return { };
        }
        return std::make_unique<CapturingRegionFrontierBackend>(
            std::move(backend), scheduler_, capture_);
    }

    [[nodiscard]] std::unique_ptr<RegionConeForwardingBackend>
    create_forwarding(const RegionConeForwardingKernel& kernel) override
    {
        return forwarding_provider_ == nullptr
            ? std::unique_ptr<RegionConeForwardingBackend> { }
            : forwarding_provider_->create_forwarding(kernel);
    }

private:
    std::shared_ptr<RegionKernelBackendProvider> provider_;
    RegionFrontierBackendProvider* frontier_provider_ { };
    RegionConeForwardingBackendProvider* forwarding_provider_ { };
    Scheduler& scheduler_;
    GenericFrontierEntryCapture& capture_;
};

class V1OnlyRegionKernelBackendProvider final
    : public RegionKernelBackendProvider {
public:
    explicit V1OnlyRegionKernelBackendProvider(
        std::shared_ptr<RegionKernelBackendProvider> provider)
        : provider_(std::move(provider))
    {
        if (!provider_) {
            throw std::invalid_argument {
                "V1-only region fixture requires a production provider"
            };
        }
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return provider_->identity();
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        // Return the original object so its optional failure channel and
        // prepared/direct backend capabilities remain visible to the runtime.
        return provider_->create(kernel);
    }

private:
    std::shared_ptr<RegionKernelBackendProvider> provider_;
};

} // namespace

struct GenericWholeWriteFixture::Impl final {
    Impl(const GenericFixtureMode requested_mode,
        const std::uint32_t requested_width,
        const std::size_t requested_write_sites,
        const bool include_checked_suffix,
        const bool same_prefix_old_snapshot_chain,
        const ValueKind requested_value_kind,
        GenericFixturePolicy requested_policy,
        const bool independent_checked_suffix_input,
        const bool include_llvm_cohort_slice_suffix,
        const bool track_region_completion_calls,
        const bool certify_parked_executor)
        : profile("FSIM_PROFILE_SV_WAVES", "1")
        , mode(requested_mode)
        , width(requested_width)
        , write_sites(requested_write_sites)
        , old_snapshot_chain(same_prefix_old_snapshot_chain)
        , separate_suffix_input(independent_checked_suffix_input
              || include_llvm_cohort_slice_suffix)
        , value_kind(requested_value_kind)
        , llvm_cohort_slice_suffix(include_llvm_cohort_slice_suffix)
        , track_completion_calls(track_region_completion_calls)
        , certify_parked(certify_parked_executor)
    {
        if (requested_policy.region_kernel_enabled) {
            policy_environment.push_back(std::make_unique<ScopedEnvironment>(
                "FSIM_ENABLE_SV_REGION_KERNEL",
                *requested_policy.region_kernel_enabled ? "1" : "0"));
        }
        if (requested_policy.local_wave_enabled) {
            policy_environment.push_back(std::make_unique<ScopedEnvironment>(
                "FSIM_ENABLE_SV_LOCAL_WAVE",
                *requested_policy.local_wave_enabled ? "1" : "0"));
        }
        const auto* const region_kernel_environment
            = std::getenv("FSIM_ENABLE_SV_REGION_KERNEL");
        region_kernel_enabled = region_kernel_environment == nullptr
            || std::string_view { region_kernel_environment } == "1";

        if (width == 0U || write_sites == 0U || write_sites > 4U
            || (value_kind != ValueKind::logic4
                && value_kind != ValueKind::logic9)
            || (separate_suffix_input && !include_checked_suffix
                && !llvm_cohort_slice_suffix)
            || (llvm_cohort_slice_suffix
                && (include_checked_suffix || old_snapshot_chain
                    || value_kind != ValueKind::logic4
                    || width == std::numeric_limits<std::uint32_t>::max()))
            || (requested_policy.v1_only_region_backend_provider
                && llvm_cohort_slice_suffix)
            || (requested_policy.output_resolution
                && requested_policy.output_resolution->first >= write_sites)
            || (old_snapshot_chain
                && (write_sites != 2U || include_checked_suffix))) {
            throw std::invalid_argument {
                "generic frontier fixture requires a bounded positive shape"
            };
        }
        const bool include_display_effect
            = requested_policy.include_display_effect;
        const auto output_resolution_for
            = [&requested_policy](const std::size_t output_index) {
                if (requested_policy.output_resolution
                    && requested_policy.output_resolution->first
                        == output_index) {
                    return requested_policy.output_resolution->second;
                }
                return ResolutionKind::none;
            };
        interpreter = std::make_unique<Interpreter>();
        auto initial = PackedLogic4 { width, Logic4::zero };
        if (value_kind == ValueKind::logic9) {
            initial = initial.promoted_to_logic9();
            if (requested_policy.output_resolution
                && requested_policy.output_resolution->second
                    == ResolutionKind::std_logic) {
                initial.fill(Logic9::u);
            }
        }
        input = interpreter->add_signal({ "generic_frontier.input", initial,
            ResolutionKind::none, value_kind });
        const auto idle = interpreter->add_signal({
            "generic_frontier.order_anchor", initial,
            ResolutionKind::none, value_kind });
        if (separate_suffix_input) {
            suffix_input = interpreter->add_signal({
                "generic_frontier.checked_suffix_input", initial,
                ResolutionKind::none, value_kind });
        }

        Process order_anchor;
        order_anchor.id = 0U;
        order_anchor.name = "generic_frontier_inert_order_anchor";
        order_anchor.scheduling_domain = ProcessSchedulingDomain::generic;
        order_anchor.initialize = false;
        order_anchor.static_sensitivity = { { idle, EdgeKind::any } };
        order_anchor.operations = { WaitSensitivity { }, Jump { 0U } };
        if (interpreter->add_process(std::move(order_anchor)) != 0U) {
            throw std::logic_error {
                "generic frontier fixture order anchor must remain process zero"
            };
        }

        outputs.reserve(write_sites);
        if (old_snapshot_chain) {
            outputs.push_back(interpreter->add_signal({
                "generic_frontier.internal_middle", initial,
                output_resolution_for(0U), value_kind }));
            outputs.push_back(interpreter->add_signal({
                "generic_frontier.final_output", initial,
                output_resolution_for(1U), value_kind }));
        } else {
            outputs.resize(write_sites);
            for (std::size_t remaining = write_sites; remaining > 0U;
                 --remaining) {
                const auto index = remaining - 1U;
                const auto name = "generic_frontier.output_"
                    + std::to_string(index);
                const auto output_resolution = output_resolution_for(index);
                auto output_initial = output_resolution
                        == ResolutionKind::std_logic
                    ? initial : PackedLogic4 { width, Logic4::x };
                if (value_kind == ValueKind::logic9
                    && !output_initial.is_logic9()) {
                    output_initial = output_initial.promoted_to_logic9();
                }
                outputs[index] = interpreter->add_signal({ name,
                    std::move(output_initial), output_resolution, value_kind });
            }
        }
        if (include_checked_suffix) {
            auto suffix_initial = PackedLogic4 { width, Logic4::z };
            if (value_kind == ValueKind::logic9) {
                suffix_initial = suffix_initial.promoted_to_logic9();
            }
            suffix_output = interpreter->add_signal({
                "generic_frontier.checked_suffix", std::move(suffix_initial),
                ResolutionKind::none, value_kind });
        } else if (llvm_cohort_slice_suffix) {
            auto suffix_initial
                = PackedLogic4 { width + 1U, Logic4::z };
            suffix_output = interpreter->add_signal({
                "generic_frontier.llvm_cohort_slice_suffix",
                std::move(suffix_initial), ResolutionKind::none, value_kind });
        }

        signal_widths.assign(
            2U + static_cast<std::size_t>(suffix_input.has_value())
                + outputs.size()
                + static_cast<std::size_t>(suffix_output.has_value()),
            width);
        signal_kinds.assign(signal_widths.size(), value_kind);
        signal_resolutions.assign(signal_widths.size(),
            ResolutionKind::none);
        for (std::size_t index = 0U; index < outputs.size(); ++index) {
            signal_resolutions[outputs[index]]
                = output_resolution_for(index);
        }
        if (llvm_cohort_slice_suffix) {
            signal_widths.at(*suffix_output) = width + 1U;
        }

        Process native_process;
        native_process.id = 1U;
        native_process.name = "generic_frontier_whole_update";
        native_process.scheduling_domain = ProcessSchedulingDomain::generic;
        native_process.register_count = 2U;
        native_process.register_value_kinds = {
            value_kind, value_kind };
        native_process.static_sensitivity = { { input, EdgeKind::any } };
        if (old_snapshot_chain) {
            native_process.driver_regions = { { outputs[0U], 0U, width, true } };
            native_process.operations.push_back(ReadSignal { 0U, input });
            native_process.operations.push_back(value_kind == ValueKind::logic9
                ? Operation { CopyRegister { 1U, 0U } }
                : Operation { UnaryNot { 1U, 0U } });
            native_process.operations.push_back(
                WriteUpdate { outputs[0U], 1U });
        } else {
            native_process.driver_regions.reserve(outputs.size());
            for (const auto output : outputs) {
                native_process.driver_regions.push_back(
                    { output, 0U, width, true });
            }
            native_process.operations.push_back(ReadSignal { 0U, input });
            native_process.operations.push_back(value_kind == ValueKind::logic9
                ? Operation { CopyRegister { 1U, 0U } }
                : Operation { UnaryNot { 1U, 0U } });
            for (const auto output : outputs) {
                native_process.operations.push_back(
                    WriteUpdate { output, 1U });
            }
        }
        if (include_display_effect) {
            native_process.operations.emplace_back(Display {
                "generic frontier effect probe", false, false });
        }
        const auto wait_instruction
            = static_cast<InstructionIndex>(native_process.operations.size());
        native_process.operations.push_back(WaitSensitivity { });
        native_process.operations.push_back(Jump { 0U });

        const auto registered_native = native_process;
        process = interpreter->add_process(std::move(native_process));
        if (process != registered_native.id) {
            throw std::logic_error {
                "generic native fixture process IDs must remain dense"
            };
        }
        native_processes.push_back(registered_native);

        if (old_snapshot_chain) {
            Process child;
            child.id = 2U;
            child.name = "generic_frontier_old_snapshot_child";
            child.scheduling_domain = ProcessSchedulingDomain::generic;
            child.register_count = 1U;
            child.register_value_kinds = { value_kind };
            child.static_sensitivity = {
                { input, EdgeKind::any }, { outputs[0U], EdgeKind::any }
            };
            child.driver_regions = { { outputs[1U], 0U, width, true } };
            child.operations = { ReadSignal { 0U, outputs[0U] },
                WriteUpdate { outputs[1U], 0U }, WaitSensitivity { }, Jump { 0U } };
            const auto registered_child = child;
            const auto child_id = interpreter->add_process(std::move(child));
            if (child_id != registered_child.id) {
                throw std::logic_error {
                    "generic child fixture process IDs must remain dense"
                };
            }
            native_processes.push_back(registered_child);
        }

        if (include_checked_suffix || llvm_cohort_slice_suffix) {
            Process checked_process;
            checked_process.id = static_cast<ProcessId>(process + 1U);
            checked_process.name = "generic_frontier_checked_suffix";
            checked_process.scheduling_domain
                = ProcessSchedulingDomain::generic;
            checked_process.register_count = 1U;
            checked_process.register_value_kinds = { value_kind };
            const auto checked_input = suffix_input.value_or(input);
            checked_process.static_sensitivity = {
                { checked_input, EdgeKind::any }
            };
            const auto suffix_offset = llvm_cohort_slice_suffix ? 1U : 0U;
            checked_process.driver_regions = { { *suffix_output,
                suffix_offset, width, false } };
            checked_process.operations = { ReadSignal { 0U, checked_input },
                WriteUpdateSlice { *suffix_output, 0U, suffix_offset },
                WaitSensitivity { }, Jump { 0U } };
            const auto registered_checked = checked_process;
            const auto checked_id
                = interpreter->add_process(std::move(checked_process));
            if (checked_id != registered_checked.id) {
                throw std::logic_error {
                    "generic suffix fixture process IDs must remain dense"
                };
            }
            checked_processes.push_back(checked_id);
            if (llvm_cohort_slice_suffix) {
                native_processes.push_back(registered_checked);
            } else {
                interpreter->set_process_executor(checked_id,
                    std::make_unique<CheckedSuffixExecutor>(registered_checked,
                        checked_id, checked_input, *suffix_output,
                        suffix_resumes));
            }
        }

        if (mode != GenericFixtureMode::interpreter) {
            auto options = make_options(mode);
            jit = std::make_unique<compiler::LlvmJit>(options);
            for (const auto& program : native_processes) {
                jit->add_process(program.name, program,
                    signal_widths, signal_kinds);
                const auto handle = jit->lookup(program.name);
                if (!handle) {
                    throw std::runtime_error {
                        "production LLVM process compiler returned an empty handle"
                    };
                }
                std::unique_ptr<ProcessExecutor> executor
                    = std::make_unique<::fsim::app::application_detail::
                        LlvmProcessExecutor>(*jit, handle, program,
                    signal_widths, signal_kinds, signal_resolutions,
                    std::shared_ptr<const ProcessSignalRemap> { }, program.id);
                if (!executor->program_access_binding()->valid()
                    || !executor->region_kernel_equivalent()
                    || !executor->region_kernel_completion_has_no_persistent_registers()) {
                    throw std::runtime_error {
                        "production LLVM process executor does not certify the complete transient body"
                    };
                }
                if (llvm_cohort_slice_suffix
                    && !checked_processes.empty()
                    && program.id == checked_processes.front()
                    && !executor->cohort_manages_process_state()) {
                    throw std::runtime_error {
                        "production LLVM slice suffix does not manage cohort process state"
                    };
                }
                if (track_completion_calls) {
                    if (!completion_call_counters) {
                        completion_call_counters
                            = std::make_shared<RegionCompletionCallCounters>();
                    }
                    executor = std::make_unique<ParkedCompletionCountingExecutor>(
                        std::move(executor), completion_call_counters,
                        certify_parked);
                }
                interpreter->set_process_executor(program.id,
                    std::move(executor));
            }

            provider = ::fsim::app::application_detail::
                make_llvm_region_kernel_backend_provider(options,
                    value_kind == ValueKind::logic9
                        ? "native-frontier-generic-writeupdate-positive-l9-v1"
                        : "native-frontier-generic-writeupdate-positive-l4-v1");
            if (!provider) {
                throw std::runtime_error {
                    "production LLVM region provider is unavailable"
                };
            }
            if (llvm_cohort_slice_suffix
                || requested_policy.capture_generic_frontier) {
                provider = std::make_shared<CapturingRegionKernelProvider>(
                    provider, interpreter->scheduler(), frontier_entry_capture);
            }
            if (requested_policy.v1_only_region_backend_provider) {
                provider = std::make_shared<
                    V1OnlyRegionKernelBackendProvider>(std::move(provider));
            }
            interpreter->set_region_kernel_backend_provider(provider);
        }
        time = 1U;
        static_cast<void>(wait_instruction);
    }

    [[nodiscard]] static compiler::LlvmJitOptions make_options(
        const GenericFixtureMode selected_mode)
    {
        compiler::LlvmJitOptions options;
        options.optimization = selected_mode == GenericFixtureMode::llvm_o0
            ? compiler::JitOptimizationLevel::o0
            : compiler::JitOptimizationLevel::o2;
        options.cache_directory.clear();
        options.debug_instrumentation = false;
        options.require_direct_update_slots = false;
        options.code_coverage_identity = "disabled";
        return options;
    }

    void schedule_input(PackedLogic4 value, const SimulationTick at,
        const StableOrder order, std::function<void()> before_frontier,
        const StableOrder before_frontier_order,
        std::optional<SchedulerOrderKey>* const before_frontier_key)
    {
        const auto max_order = std::numeric_limits<StableOrder>::max();
        const auto last_available_input_order = suffix_input.has_value()
            ? max_order - 2U : max_order - 1U;
        if (!started || input_scheduled || value.width() != width
            || value.is_logic9() != (value_kind == ValueKind::logic9)
            || order >= last_available_input_order) {
            throw std::logic_error {
                "generic frontier input must be scheduled once after startup"
            };
        }
        time = at;
        input_scheduled = true;
        if (suffix_input) {
            interpreter->schedule_signal_at(input, value, at, order);
            interpreter->schedule_signal_at(*suffix_input, std::move(value),
                at, order + 1U);
        } else {
            interpreter->schedule_signal_at(input, std::move(value), at, order);
        }
        interpreter->scheduler().schedule_at(at, SchedulerPhase::update,
            std::numeric_limits<StableOrder>::max() - 1U,
            [this, before_frontier = std::move(before_frontier),
                before_frontier_order,
                before_frontier_key](Scheduler& scheduler) mutable {
                if (before_frontier) {
                    auto task = [callback = std::move(before_frontier)]
                        (Scheduler&) mutable { callback(); };
                    if (before_frontier_key != nullptr) {
                        const auto key = scheduler.reserve_order_key(
                            before_frontier_order);
                        *before_frontier_key = key;
                        scheduler.schedule_reserved_next_delta(
                            SchedulerPhase::active, key, std::move(task));
                    } else {
                        scheduler.schedule_next_delta(
                            SchedulerPhase::active, before_frontier_order,
                            std::move(task));
                    }
                }
                scheduler.schedule_next_delta(SchedulerPhase::active,
                    std::numeric_limits<StableOrder>::max(),
                    [this](Scheduler& active_scheduler) {
                        cut_reached = true;
                        active_scheduler.request_stop();
                    });
            });
    }

    [[nodiscard]] GenericFixtureSnapshot snapshot() const
    {
        if (!interpreter) {
            throw std::logic_error { "generic frontier fixture was moved" };
        }
        auto& state = OwnedDriverDemotionTestAccess::implementation(
            *interpreter);
        GenericFixtureSnapshot result;
        if (state.region_graph
            && process < state.region_graph->processes().size()) {
            result.process_graph_pure
                = state.region_graph->processes()[process].pure;
        }
        result.pending_update_sizes = {
            state.pending_updates.size(), state.pending_update_values.size()
        };
        result.update_commit_scheduled = state.update_commit_scheduled;
        const auto copy_staged_word_updates =
            [&](const std::vector<SignalId>& signals,
                std::vector<GenericStagedWordUpdateSnapshot>& destination) {
                destination.reserve(signals.size());
                for (const auto signal : signals) {
                    std::optional<PackedLogic4> value;
                    if (signal < state.unresolved_update_scratch.size()
                        && state.unresolved_update_scratch[signal]) {
                        value = *state.unresolved_update_scratch[signal];
                    }
                    destination.push_back({ signal, std::move(value) });
                }
            };
        copy_staged_word_updates(
            state.unresolved_update_signals,
            result.unresolved_word_updates);
        copy_staged_word_updates(
            state.direct_single_driver_update_signals,
            result.direct_single_driver_word_updates);
        result.pending_update_signals.reserve(state.pending_updates.size());
        for (const auto& pending : state.pending_updates) {
            result.pending_update_signals.push_back(pending.signal);
        }
        result.pending_update_values = state.pending_update_values;
        result.native_member_dispatches
            = state.systemverilog_wave_profile_native_frontier_member_dispatches;
        result.generic_projected_region_attempts
            = state.generic_projected_region_attempts;
        result.generic_projected_region_backend_runs
            = state.generic_projected_region_backend_runs;
        result.generic_projected_region_completions
            = state.generic_projected_region_completions;
        result.generic_projected_region_members
            = state.generic_projected_region_members;
        result.generic_projected_region_declines
            = state.generic_projected_region_declines;
        result.generic_projected_region_failures
            = state.generic_projected_region_failures;
        if (completion_call_counters) {
            result.process_executor_resume_calls
                = completion_call_counters->resume_calls.load(
                    std::memory_order_relaxed);
            result.region_completion_prepare_calls
                = completion_call_counters->prepare_calls.load(
                    std::memory_order_relaxed);
            result.region_completion_stage_calls
                = completion_call_counters->stage_calls.load(
                    std::memory_order_relaxed);
            result.region_completion_commit_calls
                = completion_call_counters->commit_calls.load(
                    std::memory_order_relaxed);
        }
        result.checked_suffix_resumes = suffix_resumes;
        result.native_logic9_sidecar_count
            = state.native_logic9_word_update_count;

        const auto signal_count = state.signals.size();
        result.signals.reserve(signal_count);
        result.logic9_sidecars.reserve(signal_count);
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            auto values = signal_value_roles(state, signal);
            GenericSignalSnapshot snapshot;
            snapshot.signal = signal;
            snapshot.value_kind = state.signals.at(signal).value_kind;
            snapshot.resolution = state.signals.at(signal).resolution;
            snapshot.direct_signal_materialization_pending
                = signal < state.direct_signal_materialization_pending.size()
                && state.direct_signal_materialization_pending[signal] != 0U;
            snapshot.current = std::move(values[0U]);
            snapshot.last = std::move(values[1U]);
            snapshot.stored = std::move(values[2U]);
            bool raw_driver_captured { };
            state.driver_values.at(signal).for_each_in_process_order(
                [&](const DriverRecord& driver) {
                    if (!raw_driver_captured) {
                        const auto* const authoritative
                            = state.region_authoritative_state_for_signal(signal);
                        const bool owner_bound = authoritative != nullptr
                            && authoritative->values().packed_owner_slot_bound(
                                signal, driver.process);
                        const bool direct_owner
                            = signal < state.direct_signal_materialization_pending.size()
                            && state.direct_signal_materialization_pending[signal] != 0U
                            && state.direct_single_driver_record(signal) == &driver;
                        snapshot.raw_driver = owner_bound
                            ? authoritative->values().owner_value(
                                  signal, driver.process)
                            : direct_owner ? snapshot.current
                            : state.underlying_driver_value(
                                  driver.process, signal);
                        snapshot.raw_driver_process = driver.process;
                        raw_driver_captured = true;
                    }
                });
            snapshot.event = state.signal_events.at(signal);
            snapshot.transaction = state.signal_transactions.at(signal);
            const auto& stamp = state.signal_event_scheduling_stamps.at(signal);
            snapshot.event_domain = stamp.origin.process_domain;
            snapshot.event_phase = stamp.origin.phase;
            snapshot.systemverilog_round = stamp.systemverilog_round;
            if (signal < state.signal_value_revisions.size()) {
                snapshot.value_revision = state.signal_value_revisions[signal];
            }
            if (snapshot.value_kind == ValueKind::logic9
                && signal
                    < state.direct_single_driver_logic9_word_scratch.size()) {
                const auto& sidecar
                    = state.direct_single_driver_logic9_word_scratch[signal];
                using Sidecar = std::remove_cvref_t<decltype(sidecar)>;
                GenericLogic9SidecarSnapshot sidecar_snapshot;
                sidecar_snapshot.signal = signal;
                sidecar_snapshot.unlisted
                    = sidecar.active == Sidecar::unlisted;
                sidecar_snapshot.generic_blocked
                    = sidecar.active == Sidecar::generic_blocked;
                sidecar_snapshot.mask = sidecar.mask;
                sidecar_snapshot.planes = sidecar.planes;
                const auto queued_count = std::min<std::size_t>(
                    state.native_logic9_word_update_count,
                    state.native_logic9_word_update_signals.size());
                sidecar_snapshot.list_occurrences
                    = static_cast<std::uint32_t>(std::ranges::count(
                        std::span<const SignalId> {
                            state.native_logic9_word_update_signals.data(),
                            queued_count }, signal));
                result.logic9_sidecars.push_back(sidecar_snapshot);
            }
            result.signals.push_back(std::move(snapshot));
        }

        for (const auto& runtime : state.region_frontier_runtime_by_component) {
            if (!runtime || !runtime->backend || !runtime->backend->executor) {
                continue;
            }
            const auto& layout = runtime->backend->executor->layout();
            if (layout.execution_mode
                != RegionFrontierExecutionModeV2::generic_deferred_update) {
                continue;
            }
            result.frontier.found = true;
            result.frontier.execution_mode = layout.execution_mode;
            result.frontier.generic_update_ack_count
                = runtime->frame.generic_update_ack_count;
            result.frontier.mutable_plane_roles_absent = true;
            for (const auto& plane : runtime->planes) {
                for (std::uint32_t index = 0U; index < 4U; ++index) {
                    result.frontier.mutable_plane_roles_absent
                        = result.frontier.mutable_plane_roles_absent
                        && plane.current_planes[index] == nullptr
                        && plane.previous_planes[index] == nullptr
                        && plane.stored_planes[index] == nullptr
                        && plane.owner_planes[index] == nullptr;
                }
                const bool has_boundary = std::ranges::any_of(
                    std::span<const std::uint64_t* const> {
                        plane.boundary_planes, 4U },
                    [](const std::uint64_t* const words) {
                        return words != nullptr;
                    });
                if (!has_boundary) {
                    continue;
                }
                GenericBoundaryPlaneSnapshot boundary;
                boundary.signal = plane.signal_id;
                boundary.value_kind = plane.value_kind
                        == RegionFrontierValueKindV2::logic4
                    ? ValueKind::logic4 : ValueKind::logic9;
                boundary.width = plane.width;
                boundary.word_count = plane.word_count;
                boundary.plane_count = plane.plane_count;
                for (std::uint32_t index = 0U; index < plane.plane_count;
                     ++index) {
                    boundary.words[index]
                        = copy_words(plane.boundary_planes[index],
                            plane.word_count);
                }
                result.frontier.boundary_planes.push_back(
                    std::move(boundary));
            }
            break;
        }
        return result;
    }

    [[nodiscard]] std::array<std::size_t, 2U>
    pending_update_sizes() const noexcept
    {
        if (!interpreter) {
            return { };
        }
        const auto& state = OwnedDriverDemotionTestAccess::implementation(
            *interpreter);
        return { state.pending_updates.size(),
            state.pending_update_values.size() };
    }

    [[nodiscard]] std::uint64_t native_member_dispatches() const noexcept
    {
        if (!interpreter) {
            return 0U;
        }
        return OwnedDriverDemotionTestAccess::implementation(*interpreter)
            .systemverilog_wave_profile_native_frontier_member_dispatches;
    }

    [[nodiscard]] GenericFrontierProbe probe_frontier_state() const noexcept
    {
        GenericFrontierProbe result;
        if (!interpreter) {
            return result;
        }
        const auto& state = OwnedDriverDemotionTestAccess::implementation(
            *interpreter);
        result.pending_update_sizes = { state.pending_updates.size(),
            state.pending_update_values.size() };
        if (const auto frontier
            = state.scheduler.current_generic_batch_frontier()) {
            result.frontier_present = true;
            result.frontier_generation = frontier->generation;
            result.frontier_time = frontier->time;
            result.frontier_delta = frontier->delta;
            result.frontier_systemverilog_round = 0U;
            result.frontier_process_domain
                = ProcessSchedulingDomain::generic;
            result.frontier_phase = frontier->phase;
            result.frontier_cursor = frontier->cursor;
            result.frontier_end = frontier->end;
            result.frontier_task_count = frontier->tasks.size();
            const auto copied = std::min(frontier->tasks.size(),
                generic_frontier_probe_task_capacity);
            result.frontier_tasks_truncated
                = copied != frontier->tasks.size();
            for (std::size_t index = 0U; index < copied; ++index) {
                const auto& task = frontier->tasks[index];
                result.frontier_tasks[index] = { task.stable_order,
                    task.sequence, task.payload };
            }
        }

        for (const auto& runtime : state.region_frontier_runtime_by_component) {
            if (!runtime
                || runtime->execution_mode
                    != RegionFrontierExecutionModeV2::generic_deferred_update) {
                continue;
            }
            result.runtime_found = true;
            result.runtime_invalidated = runtime->invalidated;
            result.runtime_graph_epochs_current = state.region_graph
                && runtime->component
                    < state.region_graph->certificate_inventory()
                        .components.size()
                && state.region_graph->component_epochs_current(
                    runtime->component);
            result.native_member_dispatches = runtime->native_member_dispatches;
            const auto& frame = runtime->frame;
            result.runtime_generation
                = frame.scheduler_frontier_generation;
            result.runtime_time = frame.slot.time;
            result.runtime_delta = frame.slot.delta;
            result.runtime_systemverilog_round
                = frame.slot.systemverilog_round;
            result.runtime_process_domain
                = static_cast<ProcessSchedulingDomain>(
                    frame.slot.process_domain);
            result.runtime_phase = static_cast<SchedulerPhase>(
                frame.slot.phase);
            result.runtime_task_count
                = frame.scheduler_task_count;
            result.runtime_task_cursor
                = frame.scheduler_task_cursor;
            result.runtime_signal_slot_count = frame.signal_slot_count;
            result.pending_write_count = frame.pending_write_count;
            result.staged_event_count = frame.staged_event_count;
            result.generic_update_ack_count
                = frame.generic_update_ack_count;
            const auto& layout = runtime->backend->executor->layout();
            const auto captured_pending_count = std::min<std::size_t>(
                frame.pending_write_count,
                generic_frontier_probe_write_capacity);
            result.pending_writes_truncated
                = captured_pending_count != frame.pending_write_count
                || frame.pending_writes == nullptr;
            if (frame.pending_writes != nullptr) {
                result.captured_pending_write_count = captured_pending_count;
                for (std::size_t index = 0U;
                     index < captured_pending_count; ++index) {
                    const auto& pending = frame.pending_writes[index];
                    auto& copied = result.pending_writes[index];
                    copied.member_index = pending.member_index;
                    copied.signal_slot = pending.signal_slot;
                    copied.source_instruction = pending.source_instruction;
                    copied.value_kind
                        = pending.value_kind == RegionFrontierValueKindV2::logic9
                        ? ValueKind::logic9 : ValueKind::logic4;
                    copied.width = pending.width;
                    copied.word_count = pending.word_count;
                    copied.plane_count = pending.plane_count;
                    copied.origin_time = pending.origin.time;
                    copied.origin_delta = pending.origin.delta;
                    copied.origin_round = pending.origin.systemverilog_round;
                    copied.origin_stable_order = pending.origin.stable_order;
                    copied.origin_sequence = pending.origin.sequence;
                    copied.origin_process_domain
                        = pending.origin.process_domain;
                    copied.origin_phase = pending.origin.phase;
                    if (pending.member_index < layout.member_count
                        && layout.members != nullptr) {
                        copied.member_process_id
                            = layout.members[pending.member_index].process_id;
                    }
                    if (pending.signal_slot < layout.signal_slot_count
                        && layout.signals != nullptr) {
                        copied.signal_id
                            = layout.signals[pending.signal_slot].signal_id;
                        copied.signal_owner_process_id
                            = layout.signals[pending.signal_slot].owner_process_id;
                    }
                    if (pending.word_count
                        > generic_frontier_probe_word_capacity
                        || pending.plane_count > 4U) {
                        copied.words_truncated = true;
                        result.pending_writes_truncated = true;
                        continue;
                    }
                    for (std::uint32_t plane = 0U;
                         plane < pending.plane_count; ++plane) {
                        if (pending.value_planes[plane] == nullptr) {
                            copied.words_truncated = true;
                            result.pending_writes_truncated = true;
                            break;
                        }
                        std::ranges::copy(
                            std::span<const std::uint64_t> {
                                pending.value_planes[plane],
                                pending.word_count },
                            copied.value_planes[plane].begin());
                    }
                }
            }
            const auto retained_count = std::min<std::size_t>(
                frame.scheduler_task_count,
                runtime->original_scheduler_tasks.size());
            const auto retained_copied = std::min(retained_count,
                generic_frontier_probe_task_capacity);
            result.runtime_original_task_count = retained_count;
            result.runtime_original_tasks_truncated
                = retained_copied != retained_count;
            for (std::size_t index = 0U; index < retained_copied; ++index) {
                const auto& task = runtime->original_scheduler_tasks[index];
                result.runtime_original_tasks[index] = { task.stable_order,
                    task.sequence, task.payload };
            }
        const auto queued_count = runtime->generic_queued_members.size();
        const auto queued_copied = std::min(queued_count,
            generic_frontier_probe_task_capacity);
        result.generic_queued_member_count = queued_count;
        result.generic_queued_members_truncated
            = queued_copied != queued_count;
        for (std::size_t index = 0U; index < queued_copied; ++index) {
            const auto& queued = runtime->generic_queued_members[index];
            auto& copied = result.generic_queued_members[index];
            if (index < runtime->members.size()) {
                copied.process_id = runtime->members[index].process_id;
            }
            copied.receipt_valid = queued.receipt.valid;
            copied.key = { queued.receipt.stable_order,
                queued.receipt.sequence, queued.receipt.payload };
            copied.time = queued.receipt.time;
            copied.delta = queued.receipt.delta;
            copied.phase = queued.receipt.phase;
            copied.static_trigger_mask = queued.static_trigger_mask;
            if (copied.process_id < state.processes.size()) {
                copied.process_queued
                    = state.processes[copied.process_id].queued;
                copied.process_static_trigger_mask
                    = state.processes[copied.process_id].static_trigger_mask;
            }
            const auto word = index / 64U;
            const auto bit = UINT64_C(1) << (index % 64U);
            copied.ready = word < runtime->generic_queued_ready_words.size()
                && (runtime->generic_queued_ready_words[word] & bit) != 0U;
        }
            break;
        }
        return result;
    }

    [[nodiscard]] GenericFrontierEntryCapture
    frontier_entry_capture_snapshot() const noexcept
    {
        return frontier_entry_capture;
    }

    [[nodiscard]] std::vector<GenericTaskKeySnapshot>
    generic_frontier_task_keys() const
    {
        std::vector<GenericTaskKeySnapshot> result;
        const auto& state = OwnedDriverDemotionTestAccess::implementation(
            *interpreter);
        for (const auto& runtime : state.region_frontier_runtime_by_component) {
            if (!runtime || !runtime->backend || !runtime->backend->executor
                || runtime->backend->executor->layout().execution_mode
                    != RegionFrontierExecutionModeV2::generic_deferred_update) {
                continue;
            }
            const auto count = std::min<std::size_t>(
                runtime->frame.scheduler_task_count,
                runtime->original_scheduler_tasks.size());
            result.reserve(count);
            for (std::size_t index = 0U; index < count; ++index) {
                const auto& task = runtime->original_scheduler_tasks[index];
                result.push_back({ task.stable_order, task.sequence, task.payload });
            }
            break;
        }
        return result;
    }

    ScopedEnvironment profile;
    std::vector<std::unique_ptr<ScopedEnvironment>> policy_environment;
    GenericFixtureMode mode;
    std::uint32_t width { };
    std::size_t write_sites { };
    bool old_snapshot_chain { };
    bool separate_suffix_input { };
    ValueKind value_kind { ValueKind::logic4 };
    bool region_kernel_enabled { true };
    bool llvm_cohort_slice_suffix { };
    bool track_completion_calls { };
    bool certify_parked { true };
    GenericFrontierEntryCapture frontier_entry_capture;
    // LlvmProcessExecutor and its callback state retain non-owning spans to
    // these descriptors. Keep the completed arrays alive until after both
    // the interpreter-owned executors and JIT are destroyed.
    std::vector<std::uint32_t> signal_widths;
    std::vector<ValueKind> signal_kinds;
    std::vector<ResolutionKind> signal_resolutions;
    // ProcessProgramView borrows each facade; release executors first.
    std::vector<Process> native_processes;
    std::shared_ptr<RegionCompletionCallCounters> completion_call_counters;
    std::unique_ptr<compiler::LlvmJit> jit;
    std::unique_ptr<Interpreter> interpreter;
    std::shared_ptr<RegionKernelBackendProvider> provider;
    SignalId input { };
    std::optional<SignalId> suffix_input;
    std::vector<SignalId> outputs;
    std::optional<SignalId> suffix_output;
    std::vector<ProcessId> checked_processes;
    ProcessId process { };
    std::size_t suffix_resumes { };
    SimulationTick time { };
    bool started { };
    bool input_scheduled { };
    bool cut_reached { };
};

GenericWholeWriteFixture::GenericWholeWriteFixture(
    const GenericFixtureMode mode, const std::uint32_t width,
    const std::size_t whole_write_sites, const bool include_checked_suffix,
    const bool same_prefix_old_snapshot_chain,
    const bool independent_checked_suffix_input, const ValueKind value_kind,
    GenericFixturePolicy policy,
    const bool include_llvm_cohort_slice_suffix,
    const bool track_region_completion_calls,
    const bool certify_parked_executor)
    : impl_(std::make_unique<Impl>(mode, width, whole_write_sites,
          include_checked_suffix, same_prefix_old_snapshot_chain, value_kind,
          std::move(policy), independent_checked_suffix_input,
          include_llvm_cohort_slice_suffix,
          track_region_completion_calls, certify_parked_executor))
{
}

GenericWholeWriteFixture::~GenericWholeWriteFixture() = default;
GenericWholeWriteFixture::GenericWholeWriteFixture(
    GenericWholeWriteFixture&&) noexcept = default;
GenericWholeWriteFixture& GenericWholeWriteFixture::operator=(
    GenericWholeWriteFixture&&) noexcept = default;

void GenericWholeWriteFixture::start_and_settle()
{
    if (!impl_ || impl_->started) {
        throw std::logic_error { "generic frontier fixture starts once" };
    }
    impl_->interpreter->start();
    impl_->started = true;
    const auto result = impl_->interpreter->run(0U);
    if (result.status != RunStatus::completed
        && result.status != RunStatus::time_limit) {
        throw std::runtime_error {
            "generic frontier fixture did not settle its startup slot"
        };
    }
}

void GenericWholeWriteFixture::schedule_input(PackedLogic4 value,
    const SimulationTick time, const StableOrder order,
    std::function<void()> before_frontier,
    const StableOrder before_frontier_order,
    std::optional<SchedulerOrderKey>* const before_frontier_key)
{
    if (!impl_) {
        throw std::logic_error { "generic frontier fixture was moved" };
    }
    impl_->schedule_input(std::move(value), time, order,
        std::move(before_frontier), before_frontier_order,
        before_frontier_key);
}

SchedulerOrderKey GenericWholeWriteFixture::schedule_next_delta_active_stop(
    const StableOrder order)
{
    if (!impl_ || !impl_->interpreter) {
        throw std::logic_error { "generic frontier fixture was moved" };
    }
    auto& scheduler = impl_->interpreter->scheduler();
    const auto key = scheduler.reserve_order_key(order);
    scheduler.schedule_reserved_next_delta(SchedulerPhase::active, key,
        [](Scheduler& active_scheduler) {
            active_scheduler.request_stop();
        });
    return key;
}

void GenericWholeWriteFixture::observe_signal_and_stop(const SignalId signal)
{
    if (!impl_ || !impl_->interpreter) {
        throw std::logic_error { "generic frontier fixture was moved" };
    }
    impl_->interpreter->prepare_signal_observation(signal);
    impl_->interpreter->scheduler().request_stop();
}

void GenericWholeWriteFixture::deposit_signal(
    const SignalId signal, PackedLogic4 value)
{
    if (!impl_ || !impl_->interpreter) {
        throw std::logic_error { "generic frontier fixture was moved" };
    }
    impl_->interpreter->deposit_signal(signal, std::move(value));
}

SchedulerBatchCompactionStats
GenericWholeWriteFixture::generic_batch_compaction_stats() const noexcept
{
    return impl_ && impl_->interpreter
        ? impl_->interpreter->scheduler().generic_batch_compaction_stats()
        : SchedulerBatchCompactionStats { };
}

void GenericWholeWriteFixture::set_output_hook(Interpreter::OutputHook hook)
{
    if (!impl_) {
        throw std::logic_error { "generic frontier fixture was moved" };
    }
    impl_->interpreter->set_output_hook(std::move(hook));
}

RunResult GenericWholeWriteFixture::run_to_pre_update_cut()
{
    if (!impl_ || !impl_->input_scheduled) {
        throw std::logic_error {
            "generic frontier input must be scheduled before dispatch"
        };
    }
    return impl_->interpreter->run(impl_->time);
}

RunResult
GenericWholeWriteFixture::run_to_pre_update_cut_after_external_deposits()
{
    if (!impl_ || !impl_->started || impl_->input_scheduled
        || impl_->cut_reached) {
        throw std::logic_error {
            "external Generic deposits must precede one fresh Active cut"
        };
    }
    auto& scheduler = impl_->interpreter->scheduler();
    scheduler.schedule(SchedulerPhase::active,
        std::numeric_limits<StableOrder>::max(),
        [this](Scheduler& active_scheduler) {
            impl_->cut_reached = true;
            active_scheduler.request_stop();
        });
    impl_->input_scheduled = true;
    return impl_->interpreter->run(impl_->time);
}

RunResult GenericWholeWriteFixture::retry_frontier_attempt()
{
    if (!impl_ || !impl_->input_scheduled) {
        throw std::logic_error {
            "generic frontier attempt has no scheduled input"
        };
    }
    return impl_->interpreter->run(impl_->time);
}

RunResult GenericWholeWriteFixture::resume_update_publication()
{
    if (!impl_ || !impl_->cut_reached) {
        throw std::logic_error {
            "generic Update publication resumes only after the pre-Update cut"
        };
    }
    impl_->interpreter->scheduler().clear_stop();
    const auto result = impl_->interpreter->run(impl_->time);
    if (result.status == RunStatus::completed
        || result.status == RunStatus::time_limit) {
        impl_->input_scheduled = false;
        impl_->cut_reached = false;
    }
    return result;
}

RunResult GenericWholeWriteFixture::run_to_completion()
{
    if (!impl_) {
        throw std::logic_error { "generic frontier fixture was moved" };
    }
    if (impl_->interpreter->scheduler().stop_requested()) {
        impl_->interpreter->scheduler().clear_stop();
    }
    const auto result = impl_->interpreter->run(impl_->time);
    if (result.status == RunStatus::completed
        || result.status == RunStatus::time_limit) {
        impl_->input_scheduled = false;
        impl_->cut_reached = false;
    }
    return result;
}

GenericFixtureSnapshot GenericWholeWriteFixture::snapshot() const
{
    if (!impl_) {
        throw std::logic_error { "generic frontier fixture was moved" };
    }
    return impl_->snapshot();
}

std::array<std::size_t, 2U>
GenericWholeWriteFixture::pending_update_sizes() const noexcept
{
    return impl_ ? impl_->pending_update_sizes() : std::array<std::size_t, 2U> { };
}

std::uint64_t GenericWholeWriteFixture::native_member_dispatches() const noexcept
{
    return impl_ ? impl_->native_member_dispatches() : 0U;
}

GenericFrontierProbe
GenericWholeWriteFixture::probe_frontier_state() const noexcept
{
    return impl_ ? impl_->probe_frontier_state() : GenericFrontierProbe { };
}

GenericFrontierEntryCapture
GenericWholeWriteFixture::frontier_entry_capture() const noexcept
{
    return impl_ ? impl_->frontier_entry_capture_snapshot()
                 : GenericFrontierEntryCapture { };
}

std::vector<GenericTaskKeySnapshot>
GenericWholeWriteFixture::generic_frontier_task_keys() const
{
    if (!impl_) {
        return { };
    }
    return impl_->generic_frontier_task_keys();
}

SignalId GenericWholeWriteFixture::input_signal() const noexcept
{
    return impl_ ? impl_->input : SignalId { };
}

std::span<const SignalId> GenericWholeWriteFixture::output_signals() const noexcept
{
    return impl_ ? std::span<const SignalId> { impl_->outputs }
                 : std::span<const SignalId> { };
}

ProcessId GenericWholeWriteFixture::process_id() const noexcept
{
    return impl_ ? impl_->process : ProcessId { };
}

ValueKind GenericWholeWriteFixture::value_kind() const noexcept
{
    return impl_ ? impl_->value_kind : ValueKind::logic4;
}

bool GenericWholeWriteFixture::region_kernel_enabled() const noexcept
{
    return impl_ && impl_->region_kernel_enabled;
}

std::optional<SignalId>
GenericWholeWriteFixture::checked_suffix_signal() const noexcept
{
    return impl_ ? impl_->suffix_output : std::nullopt;
}

std::optional<SignalId>
GenericWholeWriteFixture::checked_suffix_input_signal() const noexcept
{
    return impl_ ? impl_->suffix_input : std::nullopt;
}

std::optional<SignalId>
GenericWholeWriteFixture::cohort_slice_suffix_signal() const noexcept
{
    return impl_ && impl_->llvm_cohort_slice_suffix
        ? impl_->suffix_output : std::nullopt;
}

std::optional<ProcessId>
GenericWholeWriteFixture::cohort_slice_suffix_process_id() const noexcept
{
    return impl_ && impl_->llvm_cohort_slice_suffix
            && !impl_->checked_processes.empty()
        ? std::optional<ProcessId> { impl_->checked_processes.front() }
        : std::nullopt;
}

std::size_t GenericWholeWriteFixture::checked_suffix_resumes() const noexcept
{
    return impl_ ? impl_->suffix_resumes : 0U;
}

} // namespace fsim::tests::app::frontier
