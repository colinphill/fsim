// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"
#include "runtime_fused_staging_failure_support.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"
#include "../../src/runtime/simir_region_activation_reference.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using staging_failure_support::allocation_failure_was_injected;
using staging_failure_support::arm_allocation_failure;
using staging_failure_support::clear_allocation_failure;
using staging_failure_support::set_allocation_failure_observer;

constexpr std::uint32_t forwarding_fixture_width = 65U;
constexpr std::size_t forwarding_fixture_max_words = 16U;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        set(value);
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
#if defined(_WIN32)
        (void)::_putenv_s(name_.c_str(), had_previous_ ? previous_.c_str() : "");
#else
        if (had_previous_) {
            (void)::setenv(name_.c_str(), previous_.c_str(), 1);
        } else {
            (void)::unsetenv(name_.c_str());
        }
#endif
    }

private:
    void set(const char* const value) const
    {
#if defined(_WIN32)
        if (::_putenv_s(name_.c_str(), value == nullptr ? "" : value) != 0) {
            throw std::runtime_error { "failed to set A2 test environment" };
        }
#else
        const int result = value == nullptr
            ? ::unsetenv(name_.c_str())
            : ::setenv(name_.c_str(), value, 1);
        if (result != 0) {
            throw std::runtime_error { "failed to set A2 test environment" };
        }
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

[[nodiscard]] PackedLogic4 inverted(const PackedLogic4& input)
{
    PackedLogic4 result(input.width(), Logic4::x);
    for (std::size_t bit = 0U; bit < input.width(); ++bit) {
        result.set(bit, logic_not(input.get(bit)));
    }
    return result;
}

struct RoleJournalObservation {
    struct A4RoleWords {
        // Fixed backing keeps allocation-observer snapshots passive while
        // covering the largest requested 1024-bit role row.
        std::array<std::array<std::array<std::uint64_t, 2U>,
                                  forwarding_fixture_max_words>, 4U> words { };
        std::uint32_t width { };
        std::size_t word_count { };
        bool unused_tail_is_zero { };
        bool valid { };

        bool operator==(const A4RoleWords&) const = default;
    } roles;
    std::size_t applied_rows { };
    std::size_t metadata_rows { };
    bool private_epoch_retired { };
    bool journal_enabled { };
    SimulationTick callback_time { };
    std::uint64_t callback_delta { };
    std::uint64_t callback_round { };
    std::uint64_t callback_order { };
    SimulationTick observer_time { };
    std::uint64_t observer_delta { };
    std::uint64_t observer_round { };
    ProcessId callback_owner { std::numeric_limits<ProcessId>::max() };
    SignalId callback_signal { std::numeric_limits<SignalId>::max() };
    SignalChangeOrigin origin;
    std::optional<std::pair<SimulationTick, std::uint64_t>> expected_event;
    SignalEventSchedulingStamp expected_event_stamp;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        expected_transaction;
    std::uint64_t expected_value_revision { };
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::uint64_t value_revision { };
    SignalEventSchedulingStamp event_stamp;
    bool stopped { };
};

struct RoleJournalProbe {
    Interpreter* interpreter { };
    SignalId internal_signal { };
    SignalId output_signal { };
    SignalId input_signal { };
    SignalId trigger_signal { };
    std::uint32_t signal_width { forwarding_fixture_width };
    std::size_t component { };
    bool fail_next { };
    bool inject_optional_preparation_failure { };
    std::size_t allocation_offset { };
    bool cut_next_forwarding { };
    bool cut_waiting_for_applied_row { };
    bool decline_after_cut { };
    std::size_t trace_callbacks { };
    std::exception_ptr pending_failure;
    std::vector<RegionKernelSchedulerPrefix> forwarding_origins;
    RoleJournalObservation cut;
    bool failure_observer_ran { };
    bool failure_observer_saw_no_applied_rows { };
    bool failure_observer_saw_no_output_tickets { };
    std::size_t failure_observer_applied_rows { };
    std::size_t failure_observer_metadata_rows { };
    RoleJournalObservation::A4RoleWords failure_observer_roles;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        failure_observer_event;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        failure_observer_transaction;
    std::uint64_t failure_observer_revision { };
    std::vector<std::string> checked_child_samples;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        baseline_event;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        baseline_transaction;
    std::uint64_t baseline_revision { };
};

[[nodiscard]] bool capture_a4_roles(
    AuthoritativeSignalPlanes& values,
    const SignalId signal,
    const ProcessId owner,
    const std::uint32_t expected_width,
    RoleJournalObservation::A4RoleWords& snapshot) noexcept
{
    constexpr std::array roles {
        PackedPlaneRole::current,
        PackedPlaneRole::previous,
        PackedPlaneRole::stored,
        PackedPlaneRole::owner
    };
    snapshot = { };
    for (std::size_t role_index = 0U; role_index < roles.size(); ++role_index) {
        const auto lease = values.plane_read_lease(signal,
            roles[role_index], owner);
        if (!lease || lease.width() != expected_width
            || lease.is_logic9()) {
            return false;
        }
        const auto aval = lease.plane_words(0U);
        const auto bval = lease.plane_words(1U);
        const auto expected_words = static_cast<std::size_t>(expected_width / 64U)
            + (expected_width % 64U == 0U ? 0U : 1U);
        if (expected_words == 0U
            || expected_words > forwarding_fixture_max_words
            || aval.size() != expected_words || bval.size() != expected_words) {
            return false;
        }
        for (std::size_t word = 0U; word < expected_words; ++word) {
            snapshot.words[role_index][word] = { aval[word], bval[word] };
        }
        if (role_index == 0U) {
            snapshot.width = expected_width;
            snapshot.word_count = expected_words;
            snapshot.unused_tail_is_zero = true;
        } else if (snapshot.width != expected_width
            || snapshot.word_count != expected_words) {
            return false;
        }
        const auto used_tail_bits = expected_width % 64U;
        if (used_tail_bits != 0U) {
            const auto valid_mask
                = (UINT64_C(1) << used_tail_bits) - UINT64_C(1);
            snapshot.unused_tail_is_zero
                = snapshot.unused_tail_is_zero
                && ((aval.back() | bval.back()) & ~valid_mask) == 0U;
        }
    }
    snapshot.valid = true;
    return snapshot.unused_tail_is_zero;
}

template<typename Implementation>
[[nodiscard]] bool has_active_update_slot(
    const Implementation& implementation,
    const SignalId first_signal,
    const SignalId second_signal) noexcept
{
    return std::ranges::any_of(implementation.systemverilog_update_slots,
        [&](const auto& slot) {
            return (slot.occupied || slot.reserved)
                && (slot.reserved || slot.signal == first_signal
                    || slot.signal == second_signal);
        });
}

void observe_allocation_failure(void* const context) noexcept
{
    auto& probe = *static_cast<RoleJournalProbe*>(context);
    try {
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(
                *probe.interpreter);
        const auto local
            = implementation.region_local_wave_state_by_component.at(
                probe.component);
        const auto authoritative
            = implementation.region_authoritative_state_by_component.at(
                probe.component);
        if (!local || !local->forwarding_results || !authoritative) {
            return;
        }
        const auto& bank = *local->forwarding_results;
        auto& values = authoritative->values();
        if (!capture_a4_roles(values, probe.internal_signal, 0U,
                probe.signal_width,
                probe.failure_observer_roles)) {
            return;
        }
        probe.failure_observer_saw_no_applied_rows
            = bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty();
        probe.failure_observer_applied_rows
            = bank.applied_role_mutations.size();
        probe.failure_observer_metadata_rows
            = bank.applied_role_metadata.size();
        probe.failure_observer_saw_no_output_tickets
            = !has_active_update_slot(implementation,
                probe.internal_signal, probe.output_signal);
        probe.failure_observer_event
            = implementation.signal_events.at(probe.internal_signal);
        probe.failure_observer_transaction
            = implementation.signal_transactions.at(probe.internal_signal);
        probe.failure_observer_revision
            = implementation.signal_value_revisions.at(probe.internal_signal);
        probe.failure_observer_ran = true;
    } catch (...) {
        // The allocation-failure observer is noexcept and records no owning
        // runtime values. Any inaccessible setup is reported by the caller.
    }
}

class RoleJournalExecutor final : public ProcessExecutor {
public:
    RoleJournalExecutor(RoleJournalProbe& probe,
        const ProcessId process,
        const SignalId input,
        const SignalId output,
        const InstructionIndex wait_instruction,
        ProcessExecutorProgramBinding binding,
        const bool invert_output,
        const std::uint32_t expected_register_width)
        : probe_(probe)
        , process_(process)
        , input_(input)
        , output_(output)
        , wait_instruction_(wait_instruction)
        , binding_(std::move(binding))
        , invert_output_(invert_output)
        , expected_register_width_(expected_register_width)
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool
    region_kernel_completion_has_no_persistent_registers() const noexcept
        override
    {
        return true;
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process,
        const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::size_t activation_register_count,
        const void** const storage_identity) noexcept override
    {
        if (storage_identity == nullptr || native_completion_prepared_
            || !binding_.valid() || process != process_
            || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U
            || register_bindings.size() != 1U
            || activation_register_count == 0U) {
            return false;
        }
        const auto& binding = register_bindings.front();
        if (binding.source_register != 0U
            || binding.activation_register >= activation_register_count
            || !binding.defined
            || binding.width != expected_register_width_
            || binding.value_kind != ValueKind::logic4) {
            return false;
        }
        native_activation_register_count_ = activation_register_count;
        native_completion_prepared_ = true;
        *storage_identity = this;
        return true;
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> activation_registers) noexcept
        override
    {
        return native_completion_prepared_
            && activation_registers.size()
                == native_activation_register_count_;
    }

    void commit_region_completion_native() noexcept override
    {
        native_completion_prepared_ = false;
        native_activation_register_count_ = 0U;
    }

    void cancel_region_completion_native() noexcept override
    {
        native_completion_prepared_ = false;
        native_activation_register_count_ = 0U;
    }

    void redirect(InstructionIndex) override { }

    ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex start_instruction) override
    {
        ++resumes_;
        if (start_instruction != 0U
            && start_instruction != wait_instruction_ + 1U) {
            ++pc_mismatches_;
        }
        auto value = context.read_signal(input_);
        if (process_ == 2U) {
            probe_.checked_child_samples.push_back(value.to_msb_string());
        }
        if (invert_output_) {
            value = inverted(value);
        }
        context.write_update_in_domain(output_, std::move(value),
            SignalUpdateDomain::systemverilog_active);
        ProcessResumeResult result { wait_instruction_,
            wait_instruction_ + 1U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    std::size_t resume_ordered_cohort(
        const std::span<ProcessCohortResumeEntry> entries) override
    {
        std::size_t completed { };
        for (auto& entry : entries) {
            *entry.queued = false;
            *entry.waiting_on_static = false;
            *entry.status = ProcessStatus::running;
            try {
                entry.result = entry.executor->resume(
                    *entry.context, entry.start_instruction);
            } catch (...) {
                entry.failure = std::current_exception();
                return completed + 1U;
            }
            ++completed;
        }
        return completed;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return &probe_;
    }

    [[nodiscard]] std::size_t resumes() const noexcept { return resumes_; }
    [[nodiscard]] std::size_t pc_mismatches() const noexcept
    {
        return pc_mismatches_;
    }

private:
    RoleJournalProbe& probe_;
    ProcessId process_ { };
    SignalId input_ { };
    SignalId output_ { };
    InstructionIndex wait_instruction_ { };
    ProcessExecutorProgramBinding binding_;
    bool invert_output_ { };
    std::uint32_t expected_register_width_ { };
    std::size_t native_activation_register_count_ { };
    bool native_completion_prepared_ { };
    std::size_t resumes_ { };
    std::size_t pc_mismatches_ { };
};

class RoleJournalForwardingBackend final
    : public RegionConeForwardingBackend
    , public RegionConeForwardingFailureBackend {
public:
    RoleJournalForwardingBackend(RoleJournalProbe& probe,
        const RegionConeForwardingKernel& kernel)
        : probe_(probe)
        , kernel_(kernel)
    {
    }

    [[nodiscard]] bool execute_forwarding(
        const RegionKernelSchedulerPrefix& origin,
        const std::span<const PackedLogic4> boundary_inputs,
        const std::span<PackedLogic4> output_values) noexcept override
    {
        try {
            probe_.forwarding_origins.push_back(origin);
            if (probe_.fail_next) {
                probe_.fail_next = false;
                probe_.pending_failure = std::make_exception_ptr(
                    std::bad_alloc { });
                return false;
            }
            if (boundary_inputs.size() != kernel_.execution_kernel.inputs.size()
                || output_values.size()
                    != kernel_.execution_kernel.outputs.size()) {
                return false;
            }

            RegionKernelActivationImage image;
            image.generation = origin.frontier_generation;
            image.scheduler_prefix = origin;
            for (std::size_t index = 0U;
                 index < kernel_.execution_kernel.inputs.size(); ++index) {
                const auto& input = kernel_.execution_kernel.inputs[index];
                if (input.internal || input.value_kind != ValueKind::logic4
                    || boundary_inputs[index].width() != input.width
                    || boundary_inputs[index].is_logic9()) {
                    return false;
                }
                image.register_inputs.push_back({ input.value_register,
                    boundary_inputs[index] });
            }
            for (const auto& member : kernel_.execution_kernel.members) {
                image.ready_processes.push_back(member.process);
                image.register_inputs.push_back({ member.readiness_register,
                    PackedLogic4(1U, Logic4::one) });
            }
            std::ranges::sort(image.register_inputs, std::ranges::less { },
                &RegionKernelRegisterInput::register_id);
            const auto registers
                = evaluate_region_activation_kernel_reference(
                    kernel_.execution_kernel, image);
            std::vector<PackedLogic4> staged;
            staged.reserve(kernel_.execution_kernel.outputs.size());
            for (const auto& output : kernel_.execution_kernel.outputs) {
                if (output.value_register >= registers.size()
                    || registers[output.value_register].width() != output.width
                    || registers[output.value_register].is_logic9()
                    || output.value_kind != ValueKind::logic4) {
                    return false;
                }
                staged.push_back(registers[output.value_register]);
            }
            for (std::size_t index = 0U; index < output_values.size(); ++index) {
                output_values[index] = std::move(staged[index]);
            }

            if (probe_.inject_optional_preparation_failure) {
                probe_.inject_optional_preparation_failure = false;
                set_allocation_failure_observer(&probe_,
                    &observe_allocation_failure);
                arm_allocation_failure(probe_.allocation_offset);
            }
            if (probe_.cut_next_forwarding) {
                probe_.cut_next_forwarding = false;
                probe_.cut_waiting_for_applied_row = true;
            }
        } catch (...) {
            return false;
        }
        return true;
    }

    [[nodiscard]] std::exception_ptr take_failure() noexcept override
    {
        return std::exchange(probe_.pending_failure, std::exception_ptr { });
    }

private:
    RoleJournalProbe& probe_;
    RegionConeForwardingKernel kernel_;
};

class RoleJournalProvider final
    : public RegionKernelBackendProvider
    , public RegionConeForwardingBackendProvider {
public:
    explicit RoleJournalProvider(RoleJournalProbe& probe) noexcept
        : probe_(probe)
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return "a2-role-journal-reference-provider-v1";
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel&) override
    {
        return { };
    }

    [[nodiscard]] std::unique_ptr<RegionConeForwardingBackend>
    create_forwarding(const RegionConeForwardingKernel& kernel) override
    {
        return std::make_unique<RoleJournalForwardingBackend>(probe_, kernel);
    }

private:
    RoleJournalProbe& probe_;
};

struct ForwardingFixture {
    static constexpr std::uint32_t width = forwarding_fixture_width;

    RoleJournalProbe probe;
    Interpreter interpreter;
    std::uint32_t signal_width { forwarding_fixture_width };
    SignalId input { };
    SignalId internal { };
    SignalId trigger { };
    SignalId output { };
    SignalId unrelated_narrow { };
    std::array<RoleJournalExecutor*, 3U> executors { };

    explicit ForwardingFixture(
        const std::uint32_t requested_width = forwarding_fixture_width)
        : signal_width { requested_width }
    {
        require(signal_width != 0U
                && signal_width <= forwarding_fixture_max_words * 64U,
            "A2 fixture width fits its passive fixed role snapshot");
        probe.interpreter = &interpreter;
        probe.signal_width = signal_width;
        input = interpreter.add_signal({ "a2.input",
            PackedLogic4(signal_width, Logic4::zero) });
        internal = interpreter.add_signal({ "a2.internal",
            PackedLogic4(signal_width, Logic4::zero), ResolutionKind::sv_wire });
        trigger = interpreter.add_signal({ "a2.checked_trigger",
            PackedLogic4(1U, Logic4::zero) });
        output = interpreter.add_signal({ "a2.output",
            PackedLogic4(signal_width, Logic4::zero), ResolutionKind::sv_wire });
        unrelated_narrow = interpreter.add_signal({ "a2.unrelated_narrow",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        probe.internal_signal = internal;
        probe.output_signal = output;
        probe.input_signal = input;
        probe.trigger_signal = trigger;

        Process root;
        root.id = 0U;
        root.name = "a2_root";
        root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        root.initialize = true;
        root.register_count = 1U;
        root.static_sensitivity = { { input, EdgeKind::any } };
        root.driver_regions = { { internal, 0U, 0U, true } };
        root.operations = {
            ReadSignal { 0U, input },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(root)) == 0U,
            "A2 fixture installs the root at stable order zero");

        Process order_spacer;
        order_spacer.id = 1U;
        order_spacer.name = "a2_order_spacer";
        order_spacer.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        order_spacer.initialize = false;
        order_spacer.operations = { Halt { } };
        require(interpreter.add_process(std::move(order_spacer)) == 1U,
            "A2 fixture reserves ProcessId one as the observer order slot");

        Process child;
        child.id = 2U;
        child.name = "a2_child";
        child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        child.initialize = true;
        child.register_count = 1U;
        child.static_sensitivity = {
            { internal, EdgeKind::any }, { trigger, EdgeKind::any },
        };
        child.driver_regions = { { output, 0U, 0U, true } };
        child.operations = {
            ReadSignal { 0U, internal }, UnaryNot { 0U, 0U },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(child)) == 2U,
            "A2 fixture leaves stable order one for the cut observer");

        const auto install = [&](const ProcessId process,
                                 const SignalId source,
                                 const SignalId destination,
                                 const bool negate) {
            const auto& program = interpreter.process_program(process);
            const ProcessExecutorProgramBinding binding {
                program, program, process };
            const auto wait = static_cast<InstructionIndex>(
                program.operations.size() - 2U);
            DeferredProcessExecutorContract contract;
            contract.expected_access = binding;
            contract.callbacks_observation_safe = true;
            contract.expected_region_kernel_equivalent = true;
            interpreter.set_deferred_process_executor(process,
                [] { return true; },
                [this, process, source, destination, negate, binding, wait] {
                    auto executor = std::make_unique<RoleJournalExecutor>(
                        probe, process, source, destination, wait, binding,
                        negate, signal_width);
                    executors[process] = executor.get();
                    return executor;
                }, std::move(contract));
        };
        install(0U, input, internal, false);
        install(2U, internal, output, true);
        interpreter.materialize_ready_process_executors();
        interpreter.set_region_kernel_backend_provider(
            std::make_shared<RoleJournalProvider>(probe));
    }

    void start_and_settle()
    {
        interpreter.start();
        require(interpreter.run(0U).status == RunStatus::completed,
            "A2 fixture completes initial waits at time zero");
        const auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto component = implementation.region_component_by_process.at(0U);
        require(component < implementation.region_local_wave_state_by_component.size(),
            "A2 root has a prepared region component");
        probe.component = component;
        const auto state
            = implementation.region_authoritative_state_by_component.at(component);
        require(state && state->values().packed_slots_bound()
                && state->values().requires_prewrite_unbind()
                && state->values().packed_signal_slots_bound(internal)
                && state->values().packed_owner_slot_bound(internal, 0U),
            "the wide internal signal uses versioned single-owner A4 roles");
        const auto local
            = implementation.region_local_wave_state_by_component.at(component);
        require(local && local->forwarding_results,
            "A2 fixture prepared its forwarding result bank");
        require(implementation.region_activation_programs.at(component)
                && implementation.region_activation_programs.at(component)
                       ->forwarding_kernel,
            "the two-member root-child component has a certified forwarding kernel");
        require(!state->values().layout().contains(unrelated_narrow)
                && !state->values().packed_signal_slots_bound(unrelated_narrow),
            "an unrelated narrow signal stays outside the component role bank");
    }

    [[nodiscard]] Scheduler::SafePointHookToken
    install_role_journal_cut_hook()
    {
        return interpreter.scheduler().add_safe_point_hook(
            [this](Scheduler& scheduler, const SchedulerPhase phase) {
                if (!probe.cut_waiting_for_applied_row
                    || phase != SchedulerPhase::active) {
                    return;
                }
                auto& implementation
                    = OwnedDriverDemotionTestAccess::implementation(
                        interpreter);
                if (probe.component
                        >= implementation.region_local_wave_state_by_component
                               .size()
                    || probe.component
                        >= implementation.region_authoritative_state_by_component
                               .size()) {
                    return;
                }
                const auto local
                    = implementation.region_local_wave_state_by_component[
                        probe.component];
                const auto authoritative
                    = implementation.region_authoritative_state_by_component[
                        probe.component];
                if (!local || !local->forwarding_results || !authoritative
                    || !authoritative->valid()) {
                    return;
                }

                auto& bank = *local->forwarding_results;
                if (!bank.role_journal_enabled
                    || bank.private_epoch_retired
                    || bank.applied_role_mutations.size() != 1U
                    || bank.applied_role_metadata.size() != 1U) {
                    return;
                }
                const auto& metadata = bank.applied_role_metadata.front();
                const auto& mutation = bank.applied_role_mutations.front();
                if (metadata.signal != internal
                    || metadata.owner != 0U
                    || metadata.callback_time != scheduler.now()
                    || metadata.callback_delta != scheduler.delta()
                    || metadata.callback_systemverilog_round
                        != scheduler.systemverilog_round()
                    || metadata.origin.process_domain
                        != ProcessSchedulingDomain::systemverilog
                    || metadata.origin.phase != SchedulerPhase::active
                    || mutation.signal != internal
                    || !mutation.any_state_changed
                    || probe.component
                        >= implementation.region_activation_programs.size()
                    || !implementation.region_activation_programs[
                        probe.component]) {
                    return;
                }
                const auto& activation
                    = implementation.region_activation_programs[
                        probe.component]->activation_kernel;
                if (metadata.output_index >= activation.outputs.size()) {
                    return;
                }
                const auto& activation_output
                    = activation.outputs[metadata.output_index];
                if (activation_output.signal != internal
                    || activation_output.owner != 0U
                    || activation_output.value_kind != ValueKind::logic4
                    || activation_output.offset != 0U
                    || activation_output.width != probe.signal_width) {
                    return;
                }

                auto& values = authoritative->values();
                auto& observation = probe.cut;
                observation.roles.valid = capture_a4_roles(
                    values, internal, 0U, probe.signal_width,
                    observation.roles);
                observation.applied_rows
                    = bank.applied_role_mutations.size();
                observation.metadata_rows
                    = bank.applied_role_metadata.size();
                observation.private_epoch_retired
                    = bank.private_epoch_retired;
                observation.journal_enabled = bank.role_journal_enabled;
                observation.event = implementation.signal_events.at(internal);
                observation.transaction
                    = implementation.signal_transactions.at(internal);
                observation.value_revision
                    = implementation.signal_value_revisions.at(internal);
                observation.callback_time = metadata.callback_time;
                observation.callback_delta = metadata.callback_delta;
                observation.callback_round
                    = metadata.callback_systemverilog_round;
                observation.callback_order = metadata.callback_order;
                observation.callback_owner = metadata.owner;
                observation.callback_signal = metadata.signal;
                observation.origin = metadata.origin;
                observation.event_stamp
                    = implementation.signal_event_scheduling_stamps.at(
                        internal);
                observation.observer_time = scheduler.now();
                observation.observer_delta = scheduler.delta();
                observation.observer_round = scheduler.systemverilog_round();
                observation.expected_event = metadata.expected_signal_event;
                observation.expected_event_stamp
                    = metadata.expected_event_stamp;
                observation.expected_transaction
                    = metadata.expected_transaction;
                observation.expected_value_revision
                    = metadata.expected_value_revision;
                observation.stopped = true;
                probe.cut_waiting_for_applied_row = false;

                if (probe.decline_after_cut) {
                    scheduler.set_trace_hook(&probe,
                        +[](void* const context,
                            const SchedulerTraceRecord&) noexcept {
                            ++static_cast<RoleJournalProbe*>(context)
                                  ->trace_callbacks;
                        });
                }
                scheduler.request_stop();
            });
    }
};

[[nodiscard]] RoleJournalObservation::A4RoleWords fixture_roles(
    ForwardingFixture& fixture)
{
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    auto& values
        = implementation.region_authoritative_state_by_component
              .at(fixture.probe.component)->values();
    RoleJournalObservation::A4RoleWords result;
    require(capture_a4_roles(values, fixture.internal, 0U,
                fixture.signal_width, result),
        "A2 fixture reads fixed A4 word snapshots without materializing values");
    return result;
}

[[nodiscard]] std::array<std::string, 4U> fixture_role_values(
    ForwardingFixture& fixture)
{
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto& values
        = implementation.region_authoritative_state_by_component
              .at(fixture.probe.component)->values();
    return { values.current(fixture.internal).to_msb_string(),
        values.previous(fixture.internal).to_msb_string(),
        values.stored(fixture.internal).to_msb_string(),
        values.owner_value(fixture.internal, 0U).to_msb_string() };
}

template<typename Implementation>
[[nodiscard]] bool event_metadata_matches_cut(
    const Implementation& implementation,
    SignalId signal,
    const RoleJournalObservation& cut);

[[nodiscard]] PackedLogic4 logic4_state_pattern(
    const std::uint32_t width, const std::size_t phase)
{
    constexpr std::array states {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z
    };
    PackedLogic4 value(width, Logic4::zero);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        value.set(bit, states[(bit + phase) % states.size()]);
    }
    return value;
}

[[nodiscard]] bool contains_all_logic4_states(
    const PackedLogic4& value) noexcept
{
    if (value.is_logic9()) {
        return false;
    }
    std::array<bool, 4U> seen { };
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        seen[static_cast<std::size_t>(value.get(bit))] = true;
    }
    return std::ranges::all_of(seen, [](const bool state_seen) {
        return state_seen;
    });
}

void check_one_wide_logic4_role_journal_pattern(
    const std::uint32_t width, const std::size_t phase)
{
    ScopedEnvironment region { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave { "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
    ScopedEnvironment profile { "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingFixture fixture { width };
    fixture.start_and_settle();
    auto& probe = fixture.probe;
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto zero = PackedLogic4(width, Logic4::zero);
    const auto next = logic4_state_pattern(width, phase);
    require(next != zero,
        "each unknown-state wave changes at least one packed input bit");
    if (width >= 4U) {
        require(contains_all_logic4_states(next),
            "each multi-bit stimulus contains 0, 1, X, and Z bits");
    } else {
        require(next.get(0U)
                == std::array { Logic4::zero, Logic4::one,
                    Logic4::x, Logic4::z }[phase],
            "one-bit phases cover the individual 0, 1, X, and Z states");
    }

    auto& values = implementation.region_authoritative_state_by_component
                       .at(probe.component)->values();
    const auto startup_last = PackedLogic4(width, Logic4::z);
    require(values.current(fixture.internal) == zero
            && values.previous(fixture.internal) == startup_last
            && values.stored(fixture.internal) == zero
            && values.owner_value(fixture.internal, 0U) == zero,
        "the fixture preserves the resolved-wire startup LAST while seeding A4 roles");
    const auto before_roles = fixture_roles(fixture);
    const auto before_revision
        = implementation.signal_value_revisions.at(fixture.internal);
    const auto before_child_resumes = fixture.executors[2U]->resumes();

    probe.cut_next_forwarding = true;
    const auto cut_hook = fixture.install_role_journal_cut_hook();
    fixture.interpreter.deposit_signal(fixture.input, next);
    const auto stopped = fixture.interpreter.run();
    fixture.interpreter.scheduler().remove_safe_point_hook(cut_hook);
    require(stopped.status == RunStatus::stopped
            && probe.cut.stopped
            && probe.cut.roles.valid
            && probe.cut.roles.width == width
            && probe.cut.roles.word_count == (width + 63U) / 64U
            && probe.cut.roles.unused_tail_is_zero
            && probe.cut.roles == before_roles
            && probe.cut.applied_rows == 1U
            && probe.cut.metadata_rows == 1U
            && probe.cut.journal_enabled
            && probe.cut.callback_signal == fixture.internal
            && probe.cut.callback_owner == 0U
            && probe.cut.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && probe.cut.origin.phase == SchedulerPhase::active,
        "a multiword Logic4 row remains private and keeps its full task metadata at the cut");
    require(probe.cut.callback_time == probe.cut.observer_time
            && probe.cut.callback_delta == probe.cut.observer_delta
            && probe.cut.callback_delta
                < std::numeric_limits<std::uint64_t>::max()
            && probe.cut.callback_round == probe.cut.observer_round
            && probe.cut.event.has_value()
            && probe.cut.transaction.has_value()
            && probe.cut.event->first == probe.cut.callback_time
            && probe.cut.event->second == probe.cut.callback_delta + 1U
            && probe.cut.transaction->first == probe.cut.callback_time
            && probe.cut.transaction->second == probe.cut.callback_delta + 1U
            && probe.cut.value_revision > before_revision
            && probe.cut.expected_event == probe.cut.event
            && probe.cut.expected_transaction == probe.cut.transaction
            && probe.cut.expected_value_revision == probe.cut.value_revision
            && probe.cut.expected_event_stamp.origin.process_domain
                == probe.cut.origin.process_domain
            && probe.cut.expected_event_stamp.origin.phase
                == probe.cut.origin.phase
            && probe.cut.expected_event_stamp.systemverilog_round
                == probe.cut.callback_round
            && probe.cut.event_stamp.origin.process_domain
                == probe.cut.origin.process_domain
            && probe.cut.event_stamp.origin.phase == probe.cut.origin.phase
            && probe.cut.event_stamp.systemverilog_round
                == probe.cut.callback_round,
        "the authentic callback records its event, transaction, and revision before role publication");

    require(fixture.interpreter.signal_value(fixture.internal) == next,
        "public observation materializes the exact multiword Logic4 row");
    require(values.current(fixture.internal) == next
            && values.previous(fixture.internal) == zero
            && values.stored(fixture.internal) == next
            && values.owner_value(fixture.internal, 0U) == next
            && event_metadata_matches_cut(implementation, fixture.internal,
                probe.cut),
        "the flush updates CURRENT, LAST, STORED, and OWNER with unchanged callback metadata");
    const auto flushed_roles = fixture_roles(fixture);
    require(flushed_roles.valid && flushed_roles.width == width
            && flushed_roles.word_count == (width + 63U) / 64U
            && flushed_roles.unused_tail_is_zero,
        "the flushed A4 snapshot retains exact width and canonical unused tail bits");
    const auto local
        = implementation.region_local_wave_state_by_component.at(
            probe.component);
    require(local && local->forwarding_results
            && local->forwarding_results->applied_role_mutations.empty()
            && local->forwarding_results->applied_role_metadata.empty(),
        "materialization drains the row and its metadata exactly once");

    fixture.interpreter.scheduler().clear_stop();
    fixture.interpreter.deposit_signal(fixture.trigger,
        PackedLogic4(1U, Logic4::one));
    require(fixture.interpreter.run().status == RunStatus::completed
            && fixture.executors[2U]->resumes() == before_child_resumes + 1U
            && !probe.checked_child_samples.empty()
            && probe.checked_child_samples.back() == next.to_msb_string()
            && fixture.interpreter.signal_value(fixture.output)
                == inverted(next),
        "the checked child resumes once and preserves four-state NOT semantics after flush");
}

void check_wide_logic4_role_journal_shapes_and_unknown_states()
{
    constexpr std::array<std::uint32_t, 6U> widths {
        1U, 64U, 65U, 129U, 256U, 1024U
    };
    for (const auto width : widths) {
        for (std::size_t phase = 1U; phase <= 3U; ++phase) {
            check_one_wide_logic4_role_journal_pattern(width, phase);
        }
    }
}

template<typename Implementation>
[[nodiscard]] bool event_metadata_matches_cut(
    const Implementation& implementation,
    const SignalId signal,
    const RoleJournalObservation& cut)
{
    if (implementation.signal_events.at(signal) != cut.event
        || implementation.signal_transactions.at(signal) != cut.transaction
        || implementation.signal_value_revisions.at(signal)
            != cut.value_revision) {
        return false;
    }
    const auto& stamp = implementation.signal_event_scheduling_stamps.at(signal);
    return stamp.origin.process_domain
            == cut.event_stamp.origin.process_domain
        && stamp.origin.phase == cut.event_stamp.origin.phase
        && stamp.systemverilog_round
            == cut.event_stamp.systemverilog_round;
}

[[nodiscard]] bool same_forwarding_keys(
    const RegionKernelSchedulerPrefix& left,
    const RegionKernelSchedulerPrefix& right) noexcept
{
    // A retry may receive a fresh authenticated frontier generation. The
    // borrowed scheduler keys and their ordered range must remain identical.
    return left.frontier_cursor == right.frontier_cursor
        && left.frontier_end == right.frontier_end
        && left.time == right.time && left.delta == right.delta
        && left.phase == right.phase
        && left.systemverilog_round == right.systemverilog_round
        && left.process_domain == right.process_domain
        && left.tasks == right.tasks;
}

void check_previsibility_failure_retries_exact_origin()
{
    ScopedEnvironment region {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile {
        "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingFixture fixture;
    fixture.start_and_settle();
    auto& probe = fixture.probe;
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto before_roles = fixture_roles(fixture);
    const auto before_event
        = implementation.signal_events.at(fixture.internal);
    const auto before_transaction
        = implementation.signal_transactions.at(fixture.internal);
    const auto before_revision
        = implementation.signal_value_revisions.at(fixture.internal);
    const auto before_root_resumes = fixture.executors[0U]->resumes();
    const auto before_child_resumes = fixture.executors[2U]->resumes();

    probe.fail_next = true;
    fixture.interpreter.deposit_signal(fixture.input,
        PackedLogic4(ForwardingFixture::width, Logic4::one));
    bool failed_before_publication { };
    try {
        (void)fixture.interpreter.run();
    } catch (const std::bad_alloc&) {
        failed_before_publication = true;
    }
    require(failed_before_publication
            && probe.forwarding_origins.size() >= 1U
            && !probe.pending_failure,
        "the captured previsibility failure is returned through the forwarding failure channel");
    require(fixture_roles(fixture) == before_roles
            && implementation.signal_events.at(fixture.internal) == before_event
            && implementation.signal_transactions.at(fixture.internal)
                == before_transaction
            && implementation.signal_value_revisions.at(fixture.internal)
                == before_revision
            && !has_active_update_slot(implementation,
                fixture.internal, fixture.output)
            && fixture.executors[0U]->resumes() == before_root_resumes
            && fixture.executors[2U]->resumes() == before_child_resumes,
        "a failed native preflight leaves every internal role, stamp, and executor untouched");

    const auto failed_origin_index = probe.forwarding_origins.size() - 1U;
    const auto failed_origin = probe.forwarding_origins[failed_origin_index];
    require(!failed_origin.tasks.empty()
            && failed_origin.phase == SchedulerPhase::active
            && failed_origin.process_domain
                == ProcessSchedulingDomain::systemverilog,
        "the failure records the exact original SystemVerilog Active key prefix");
    require(fixture.interpreter.run().status == RunStatus::completed,
        "the same original frontier is retried through its ordinary callbacks");
    require(probe.forwarding_origins.size() > failed_origin_index + 1U
            && same_forwarding_keys(
                probe.forwarding_origins[failed_origin_index + 1U],
                failed_origin),
        "the retry receives the exact original prefix identity without key reissue");
    require(fixture.interpreter.signal_value(fixture.internal)
                == PackedLogic4(ForwardingFixture::width, Logic4::one)
            && fixture.interpreter.signal_value(fixture.output)
                == PackedLogic4(ForwardingFixture::width, Logic4::zero),
        "the successful retry publishes one root and child result");
    require(fixture.executors[0U]->resumes() - before_root_resumes <= 1U
            && fixture.executors[2U]->resumes() - before_child_resumes <= 1U,
        "retry executes each checked producer at most once after the failed native attempt");
}

void check_applied_role_journal_survives_discard_and_flushes_on_observation()
{
    ScopedEnvironment region {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile {
        "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingFixture fixture;
    fixture.start_and_settle();
    auto& probe = fixture.probe;
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto before_roles = fixture_roles(fixture);
    probe.baseline_event
        = implementation.signal_events.at(fixture.internal);
    probe.baseline_transaction
        = implementation.signal_transactions.at(fixture.internal);
    probe.baseline_revision
        = implementation.signal_value_revisions.at(fixture.internal);
    const auto before_root_resumes = fixture.executors[0U]->resumes();
    const auto before_child_resumes = fixture.executors[2U]->resumes();

    probe.cut_next_forwarding = true;
    const auto cut_hook = fixture.install_role_journal_cut_hook();
    fixture.interpreter.deposit_signal(fixture.input,
        PackedLogic4(ForwardingFixture::width, Logic4::one));
    const auto stopped = fixture.interpreter.run();
    fixture.interpreter.scheduler().remove_safe_point_hook(cut_hook);
    require(stopped.status == RunStatus::stopped
            && probe.cut.stopped
            && probe.cut.applied_rows == 1U
            && probe.cut.metadata_rows == 1U
            && probe.cut.journal_enabled,
        "the safe point cuts after one authentic internal callback appends its A2 row");
    require(probe.cut.roles.valid
            && probe.cut.roles == before_roles
            && probe.cut.callback_signal == fixture.internal
            && probe.cut.callback_owner == 0U
            && probe.cut.callback_time == probe.cut.observer_time
            && probe.cut.callback_delta == probe.cut.observer_delta
            && probe.cut.callback_round == probe.cut.observer_round
            && probe.cut.callback_order == 1U
            && probe.cut.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && probe.cut.origin.phase == SchedulerPhase::active
            && probe.cut.event.has_value()
            && probe.cut.transaction.has_value()
            && probe.cut.value_revision > probe.baseline_revision
            && probe.cut.expected_event == probe.cut.event
            && probe.cut.expected_transaction == probe.cut.transaction
            && probe.cut.expected_value_revision == probe.cut.value_revision
            && probe.cut.expected_event_stamp.origin.process_domain
                == probe.cut.origin.process_domain
            && probe.cut.expected_event_stamp.origin.phase
                == probe.cut.origin.phase
            && probe.cut.expected_event_stamp.systemverilog_round
                == probe.cut.callback_round
            && probe.cut.event_stamp.origin.process_domain
                == probe.cut.expected_event_stamp.origin.process_domain
            && probe.cut.event_stamp.origin.phase
                == probe.cut.expected_event_stamp.origin.phase
            && probe.cut.event_stamp.systemverilog_round
                == probe.cut.expected_event_stamp.systemverilog_round,
        "applied A2 metadata advances while the four public A4 roles stay at the pre-wave snapshot");
    require(fixture.executors[0U]->resumes() == before_root_resumes
            && fixture.executors[2U]->resumes() == before_child_resumes,
        "the certified forwarding evaluation consumes no checked process body");

    fixture.interpreter.scheduler().discard_pending();
    const auto local
        = implementation.region_local_wave_state_by_component.at(
            probe.component);
    require(local && local->forwarding_results
            && local->forwarding_results->private_epoch_retired
            && local->forwarding_results->applied_role_mutations.size() == 1U
            && local->forwarding_results->applied_role_metadata.size() == 1U
            && fixture_roles(fixture) == before_roles,
        "scheduler discard retires private work without deleting the applied role journal");

    const auto observed = fixture.interpreter.signal_value(fixture.internal);
    require(observed == PackedLogic4(ForwardingFixture::width, Logic4::one),
        "a public getter flushes the latest applied internal role value");
    auto& values = implementation.region_authoritative_state_by_component
                       .at(probe.component)->values();
    require(values.current(fixture.internal)
                == PackedLogic4(ForwardingFixture::width, Logic4::one)
            && values.previous(fixture.internal)
                == PackedLogic4(ForwardingFixture::width, Logic4::zero)
            && values.stored(fixture.internal)
                == PackedLogic4(ForwardingFixture::width, Logic4::one)
            && values.owner_value(fixture.internal, 0U)
                == PackedLogic4(ForwardingFixture::width, Logic4::one)
            && local->forwarding_results->applied_role_mutations.empty()
            && local->forwarding_results->applied_role_metadata.empty(),
        "the observer materializes CURRENT, LAST, STORED and original OWNER once, then drains the journal");

    const auto checked_child_samples_before
        = fixture.executors[2U]->resumes();
    fixture.interpreter.scheduler().clear_stop();
    fixture.interpreter.deposit_signal(fixture.trigger,
        PackedLogic4(1U, Logic4::one));
    require(fixture.interpreter.run().status == RunStatus::completed,
        "a later trigger runs the child through checked fallback after observation");
    require(fixture.executors[0U]->resumes() == before_root_resumes
            && fixture.executors[2U]->resumes()
                == checked_child_samples_before + 1U
            && !fixture.probe.checked_child_samples.empty()
            && fixture.probe.checked_child_samples.back()
                == PackedLogic4(ForwardingFixture::width, Logic4::one)
                    .to_msb_string()
            && fixture.interpreter.signal_value(fixture.output)
                == PackedLogic4(ForwardingFixture::width, Logic4::zero),
        "checked fallback reads the flushed CURRENT value without replaying the root producer");
}

void check_boundary_deposit_flushes_applied_role_journal()
{
    ScopedEnvironment region {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile {
        "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingFixture fixture;
    fixture.start_and_settle();
    auto& probe = fixture.probe;
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto before_role_values = fixture_role_values(fixture);
    const auto before_root_resumes = fixture.executors[0U]->resumes();
    const auto before_child_resumes = fixture.executors[2U]->resumes();

    probe.cut_next_forwarding = true;
    const auto cut_hook = fixture.install_role_journal_cut_hook();
    fixture.interpreter.deposit_signal(fixture.input,
        PackedLogic4(ForwardingFixture::width, Logic4::one));
    const auto stopped = fixture.interpreter.run();
    fixture.interpreter.scheduler().remove_safe_point_hook(cut_hook);
    require(stopped.status == RunStatus::stopped
            && probe.cut.applied_rows == 1U
            && probe.cut.metadata_rows == 1U,
        "the boundary-deposit witness starts with one applied private role row");

    const auto local
        = implementation.region_local_wave_state_by_component.at(
            probe.component);
    require(local && local->forwarding_results,
        "the boundary-deposit witness retains its forwarding bank");
    auto& bank = *local->forwarding_results;
    require(std::ranges::find(bank.boundary_signals, fixture.input)
                != bank.boundary_signals.end()
            && implementation.region_authoritative_component_by_signal.at(
                   fixture.input)
                == std::numeric_limits<std::size_t>::max(),
        "the input is resolved through the bank boundary list, not output ownership");

    fixture.interpreter.scheduler().discard_pending();
    require(bank.private_epoch_retired
            && bank.applied_role_mutations.size() == 1U
            && bank.applied_role_metadata.size() == 1U
            && fixture_role_values(fixture) == before_role_values,
        "discard retains the applied row and leaves A4 roles untouched");

    fixture.interpreter.deposit_signal(fixture.input,
        PackedLogic4(ForwardingFixture::width, Logic4::zero));
    const auto expected = PackedLogic4(
        ForwardingFixture::width, Logic4::one);
    require(bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty()
            && !bank.role_journal_enabled
            && implementation.region_forwarding_role_journal_nonempty_components
                == 0U,
        "a boundary input deposit flushes and retires the related hidden role row first");
    require(fixture_role_values(fixture)
                == std::array<std::string, 4U> {
                    expected.to_msb_string(), before_role_values[0U],
                    expected.to_msb_string(), expected.to_msb_string() },
        "the boundary deposit preserves the just-applied internal CURRENT, LAST, STORED, and OWNER roles");
    const auto* const raw_owner
        = implementation.driver_values.at(fixture.internal).find(0U);
    require(raw_owner != nullptr && raw_owner->value == expected
            && event_metadata_matches_cut(
                implementation, fixture.internal, probe.cut),
        "the boundary deposit materializes the original raw driver without replaying private metadata");
    require(implementation.signals.at(fixture.input).initial_value
                == PackedLogic4(ForwardingFixture::width, Logic4::zero)
            && fixture.executors[0U]->resumes() == before_root_resumes
            && fixture.executors[2U]->resumes() == before_child_resumes,
        "the input mutation is visible only after the role flush and executes no producer inline");
}

void check_driver_value_flushes_applied_role_journal()
{
    ScopedEnvironment region {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile {
        "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingFixture fixture;
    fixture.start_and_settle();
    auto& probe = fixture.probe;
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto before_roles = fixture_roles(fixture);
    const auto before_role_values = fixture_role_values(fixture);
    const auto before_root_resumes = fixture.executors[0U]->resumes();

    probe.cut_next_forwarding = true;
    const auto cut_hook = fixture.install_role_journal_cut_hook();
    fixture.interpreter.deposit_signal(fixture.input,
        PackedLogic4(ForwardingFixture::width, Logic4::one));
    const auto stopped = fixture.interpreter.run();
    fixture.interpreter.scheduler().remove_safe_point_hook(cut_hook);
    require(stopped.status == RunStatus::stopped
            && probe.cut.applied_rows == 1U
            && probe.cut.metadata_rows == 1U
            && fixture_roles(fixture) == before_roles,
        "driver_value starts from an applied role row hidden from A4");

    fixture.interpreter.scheduler().discard_pending();
    require(fixture_roles(fixture) == before_roles,
        "discard retires queued private tokens but retains the role snapshot");
    const auto observed
        = fixture.interpreter.driver_value(0U, fixture.internal);
    const auto local
        = implementation.region_local_wave_state_by_component.at(
            probe.component);
    require(observed
                == PackedLogic4(ForwardingFixture::width, Logic4::one)
            && implementation.region_forwarding_role_journal_nonempty_components
                == 0U
            && local && local->forwarding_results
            && local->forwarding_results->applied_role_mutations.empty()
            && local->forwarding_results->applied_role_metadata.empty(),
        "driver_value flushes the hidden owner role before reading it");
    require(fixture_role_values(fixture)
                == std::array<std::string, 4U> {
                    PackedLogic4(ForwardingFixture::width, Logic4::one)
                        .to_msb_string(),
                    before_role_values[0U],
                    PackedLogic4(ForwardingFixture::width, Logic4::one)
                        .to_msb_string(),
                    PackedLogic4(ForwardingFixture::width, Logic4::one)
                        .to_msb_string() }
            && event_metadata_matches_cut(implementation, fixture.internal,
                probe.cut),
        "driver observation publishes all roles without replaying callback metadata");
    require(fixture.executors[0U]->resumes() == before_root_resumes,
        "driver_value does not replay the consumed root producer");
}

void check_signal_hook_flushes_before_policy_demotion()
{
    ScopedEnvironment region {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile {
        "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingFixture fixture;
    fixture.start_and_settle();
    auto& probe = fixture.probe;
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto before_roles = fixture_roles(fixture);
    const auto before_role_values = fixture_role_values(fixture);

    probe.cut_next_forwarding = true;
    const auto cut_hook = fixture.install_role_journal_cut_hook();
    fixture.interpreter.deposit_signal(fixture.input,
        PackedLogic4(ForwardingFixture::width, Logic4::one));
    const auto stopped = fixture.interpreter.run();
    fixture.interpreter.scheduler().remove_safe_point_hook(cut_hook);
    require(stopped.status == RunStatus::stopped
            && probe.cut.applied_rows == 1U
            && fixture_roles(fixture) == before_roles,
        "hook installation starts with one pending hidden role row");
    fixture.interpreter.scheduler().discard_pending();
    require(fixture_roles(fixture) == before_roles,
        "discard retains the hidden role row until a checked barrier runs");

    fixture.interpreter.set_signal_change_hook(
        [](SignalId, const PackedLogic4&, SimulationTick) { });
    const auto local
        = implementation.region_local_wave_state_by_component.at(
            probe.component);
    require(implementation.region_forwarding_role_journal_nonempty_components
                == 0U
            && local && local->forwarding_results
            && local->forwarding_results->applied_role_mutations.empty()
            && local->forwarding_results->applied_role_metadata.empty(),
        "hook installation flushes the role row before invalidating the graph policy");
    require(fixture_role_values(fixture)
                == std::array<std::string, 4U> {
                    PackedLogic4(ForwardingFixture::width, Logic4::one)
                        .to_msb_string(),
                    before_role_values[0U],
                    PackedLogic4(ForwardingFixture::width, Logic4::one)
                        .to_msb_string(),
                    PackedLogic4(ForwardingFixture::width, Logic4::one)
                        .to_msb_string() }
            && event_metadata_matches_cut(implementation, fixture.internal,
                probe.cut)
            && !implementation.region_authoritative_state_by_component
                    .at(probe.component)->values().packed_slots_bound(),
        "policy demotion preserves the fully committed roles and callback metadata");
    require(fixture.interpreter.signal_value(fixture.internal)
                == PackedLogic4(ForwardingFixture::width, Logic4::one),
        "a public read after hook installation sees the flushed value");
}

void check_checked_fallback_flushes_after_trace_decline()
{
    ScopedEnvironment region {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile {
        "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingFixture fixture;
    fixture.start_and_settle();
    auto& probe = fixture.probe;
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(fixture.interpreter);
    const auto before_roles = fixture_roles(fixture);
    const auto before_root_resumes = fixture.executors[0U]->resumes();
    const auto before_child_resumes = fixture.executors[2U]->resumes();
    const auto before_child_samples = probe.checked_child_samples.size();
    const auto before_trace_declines
        = implementation.systemverilog_wave_profile_region_trace_declines;

    probe.cut_next_forwarding = true;
    probe.decline_after_cut = true;
    const auto cut_hook = fixture.install_role_journal_cut_hook();
    fixture.interpreter.deposit_signal(fixture.input,
        PackedLogic4(ForwardingFixture::width, Logic4::one));
    const auto stopped = fixture.interpreter.run();
    fixture.interpreter.scheduler().remove_safe_point_hook(cut_hook);
    require(stopped.status == RunStatus::stopped
            && probe.cut.applied_rows == 1U
            && fixture_roles(fixture) == before_roles
            && fixture.interpreter.scheduler().trace_hook_installed(),
        "trace decline is installed after a real applied private role row");
    const auto forwarding_attempts_at_cut = probe.forwarding_origins.size();

    fixture.interpreter.scheduler().clear_stop();
    require(fixture.interpreter.run().status == RunStatus::completed,
        "the retained child resumes through checked fallback after trace decline");
    fixture.interpreter.scheduler().set_trace_hook(nullptr, nullptr);

    const auto local
        = implementation.region_local_wave_state_by_component.at(
            probe.component);
    require(implementation.systemverilog_wave_profile_region_trace_declines
                > before_trace_declines
            && probe.trace_callbacks != 0U
            && implementation.region_forwarding_role_journal_nonempty_components
                == 0U
            && local && local->forwarding_results
            && local->forwarding_results->applied_role_mutations.empty()
            && local->forwarding_results->applied_role_metadata.empty(),
        "the early checked fallback flushes the applied role prefix before dispatch");
    require(fixture.executors[0U]->resumes() == before_root_resumes
            && fixture.executors[2U]->resumes() == before_child_resumes + 1U
            && probe.checked_child_samples.size() == before_child_samples + 1U
            && probe.forwarding_origins.size() == forwarding_attempts_at_cut
            && event_metadata_matches_cut(implementation, fixture.internal,
                probe.cut)
            && probe.checked_child_samples.back()
                == PackedLogic4(ForwardingFixture::width, Logic4::one)
                    .to_msb_string()
            && fixture.interpreter.signal_value(fixture.output)
                == PackedLogic4(ForwardingFixture::width, Logic4::zero),
        "checked child samples the flushed CURRENT value once without root replay");
}

void check_optional_role_preparation_failure_is_safe()
{
    ScopedEnvironment region {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile {
        "FSIM_PROFILE_SV_WAVES", "1" };

    bool saw_previsibility_injection { };
    bool reached_no_injection_terminal { };
    for (std::size_t allocation_offset = 0U;
         allocation_offset < 256U && !reached_no_injection_terminal;
         ++allocation_offset) {
        ForwardingFixture fixture;
        fixture.start_and_settle();
        auto& probe = fixture.probe;
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(
                fixture.interpreter);
        const auto before_roles = fixture_roles(fixture);
        probe.baseline_event
            = implementation.signal_events.at(fixture.internal);
        probe.baseline_transaction
            = implementation.signal_transactions.at(fixture.internal);
        probe.baseline_revision
            = implementation.signal_value_revisions.at(fixture.internal);
        const auto before_root_resumes = fixture.executors[0U]->resumes();
        const auto before_child_resumes = fixture.executors[2U]->resumes();
        probe.inject_optional_preparation_failure = true;
        probe.allocation_offset = allocation_offset;
        fixture.interpreter.deposit_signal(fixture.input,
            PackedLogic4(ForwardingFixture::width, Logic4::one));
        bool propagated { };
        RunStatus status { RunStatus::completed };
        try {
            status = fixture.interpreter.run().status;
        } catch (const std::bad_alloc&) {
            propagated = true;
        }
        const bool injected = allocation_failure_was_injected();
        clear_allocation_failure();
        set_allocation_failure_observer(nullptr, nullptr);
        if (!injected) {
            require(!propagated && status == RunStatus::completed,
                "the allocation sweep reaches a no-injection successful terminal");
            reached_no_injection_terminal = true;
            break;
        }

        require(probe.failure_observer_ran,
            "the targeted preparation allocation is observed with fixed passive state");
        const bool previsibility = probe.failure_observer_saw_no_applied_rows
            && probe.failure_observer_saw_no_output_tickets
            && probe.failure_observer_roles.valid
            && probe.failure_observer_roles == before_roles
            && probe.failure_observer_event == probe.baseline_event
            && probe.failure_observer_transaction == probe.baseline_transaction
            && probe.failure_observer_revision == probe.baseline_revision;
        if (!previsibility) {
            // The allocator remains armed through the transaction. A later
            // injected allocation is classified from the actual applied
            // prefix/ticket/state, never mislabeled as previsibility.
            const bool applied_prefix_visible
                = probe.failure_observer_applied_rows != 0U
                || probe.failure_observer_metadata_rows != 0U;
            const bool tickets_visible
                = !probe.failure_observer_saw_no_output_tickets;
            const bool state_visible
                = probe.failure_observer_revision != probe.baseline_revision;
            require(applied_prefix_visible || tickets_visible || state_visible,
                "a later injected allocation is identified by applied journal state, tickets, or a committed stamp");
            if (applied_prefix_visible) {
                require(probe.failure_observer_applied_rows
                            == probe.failure_observer_metadata_rows,
                    "a later injected allocation observes aligned applied rows and metadata");
            }

            if (propagated) {
                require(fixture.interpreter.run().status == RunStatus::completed,
                    "the scheduler resumes safely after a later propagated allocation failure");
            } else {
                require(status == RunStatus::completed,
                    "a later nonthrowing allocation decline completes its current callback");
            }
            const auto internal_value
                = fixture.interpreter.signal_value(fixture.internal);
            const auto output_value
                = fixture.interpreter.signal_value(fixture.output);
            auto& state = implementation
                .region_authoritative_state_by_component.at(
                    probe.component)->values();
            require(internal_value
                        == PackedLogic4(ForwardingFixture::width, Logic4::one)
                    && output_value
                        == PackedLogic4(ForwardingFixture::width, Logic4::zero)
                    && state.previous(fixture.internal)
                        == PackedLogic4(ForwardingFixture::width, Logic4::zero)
                    && state.current(fixture.internal)
                        == PackedLogic4(ForwardingFixture::width, Logic4::one)
                    && state.stored(fixture.internal)
                        == PackedLogic4(ForwardingFixture::width, Logic4::one)
                    && state.owner_value(fixture.internal, 0U)
                        == PackedLogic4(ForwardingFixture::width, Logic4::one),
                "later failure recovery materializes one coherent four-role result");
            require(fixture.executors[0U]->resumes()
                        - before_root_resumes <= 1U
                    && fixture.executors[2U]->resumes()
                        - before_child_resumes <= 1U,
                "later failure recovery never replays a checked producer or child body");
            require(implementation.signal_value_revisions.at(fixture.internal)
                        > probe.baseline_revision,
                "later failure recovery retains the single committed internal revision");
            require(implementation.signal_events.at(fixture.internal).has_value()
                    && implementation.signal_transactions.at(fixture.internal)
                        .has_value(),
                "later failure recovery retains the internal event and transaction metadata");
            continue;
        }

        saw_previsibility_injection = true;
        require(probe.failure_observer_saw_no_applied_rows
                && probe.failure_observer_saw_no_output_tickets
                && probe.failure_observer_roles == before_roles
                && probe.failure_observer_event == probe.baseline_event
                && probe.failure_observer_transaction
                    == probe.baseline_transaction
                && probe.failure_observer_revision == probe.baseline_revision,
            "the previsibility injection observes unchanged roles and no applied metadata or output tickets");
        if (propagated) {
            require(fixture_roles(fixture) == before_roles
                    && implementation.signal_events.at(fixture.internal)
                        == probe.baseline_event
                    && implementation.signal_transactions.at(fixture.internal)
                        == probe.baseline_transaction
                    && implementation.signal_value_revisions.at(fixture.internal)
                        == probe.baseline_revision
                    && !has_active_update_slot(implementation,
                        fixture.internal, fixture.output),
                "a propagated previsibility allocation failure leaves roles and tickets untouched");
            require(!probe.forwarding_origins.empty(),
                "a propagated previsibility failure retains the original forwarding key");
            const auto failed_origin_index
                = probe.forwarding_origins.size() - 1U;
            const auto failed_origin
                = probe.forwarding_origins[failed_origin_index];
            require(fixture.interpreter.run().status == RunStatus::completed
                    && probe.forwarding_origins.size()
                        > failed_origin_index + 1U
                    && same_forwarding_keys(
                        probe.forwarding_origins[failed_origin_index + 1U],
                        failed_origin),
                "propagated preparation failure retries the identical scheduler key");
        } else {
            require(status == RunStatus::completed,
                "optional role preparation failure safely declines to checked publication");
        }
        require(fixture.executors[0U]->resumes() - before_root_resumes <= 1U
                && fixture.executors[2U]->resumes() - before_child_resumes <= 1U
                && fixture.interpreter.signal_value(fixture.internal)
                    == PackedLogic4(ForwardingFixture::width, Logic4::one)
                && fixture.interpreter.signal_value(fixture.output)
                    == PackedLogic4(ForwardingFixture::width, Logic4::zero),
            "both safe fallback and exact retry publish one correct root-child result");
    }
    require(saw_previsibility_injection
            && reached_no_injection_terminal,
        "the bounded allocation sweep observes a previsibility failure and continues through the uninjected terminal");
}

} // namespace
} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::check_wide_logic4_role_journal_shapes_and_unknown_states();
        fsim::tests::runtime::check_previsibility_failure_retries_exact_origin();
        fsim::tests::runtime::check_applied_role_journal_survives_discard_and_flushes_on_observation();
        fsim::tests::runtime::check_boundary_deposit_flushes_applied_role_journal();
        fsim::tests::runtime::check_driver_value_flushes_applied_role_journal();
        fsim::tests::runtime::check_signal_hook_flushes_before_policy_demotion();
        fsim::tests::runtime::check_checked_fallback_flushes_after_trace_decline();
        fsim::tests::runtime::check_optional_role_preparation_failure_is_safe();
        std::cout << "A2 role journal runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
        fsim::tests::runtime::staging_failure_support::set_allocation_failure_observer(
            nullptr, nullptr);
        std::cerr << "A2 role journal runtime tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
