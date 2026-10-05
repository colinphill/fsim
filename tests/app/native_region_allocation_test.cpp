// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../runtime/runtime_fused_staging_failure_support.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <streambuf>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
    struct RawDriver {
        ProcessId process { };
        PackedLogic4 value;
        DriveStrength strength;

        friend bool operator==(const RawDriver&, const RawDriver&) = default;
    };

    struct SignalSnapshot {
        SignalId signal { };
        PackedLogic4 current;
        PackedLogic4 last;
        PackedLogic4 stored;
        std::vector<RawDriver> raw_drivers;
        std::optional<PackedLogic4> owned_raw;
        std::optional<PackedLogic4> external_raw;
        std::optional<PackedLogic4> force_value;
        std::optional<PackedLogic4> force_mask;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        ProcessSchedulingDomain event_domain {
            ProcessSchedulingDomain::generic };
        SchedulerPhase event_phase { SchedulerPhase::active };
        std::uint64_t systemverilog_round { };
        SimulationTick now { };
        std::uint64_t delta { };
        bool materialization_pending { };

        friend bool operator==(
            const SignalSnapshot&, const SignalSnapshot&) = default;
    };

    struct RawDriverSummary {
        ProcessId process { };
        std::string value;
        DriveStrength strength;

        friend bool operator==(
            const RawDriverSummary&, const RawDriverSummary&) = default;
    };

    struct SignalSnapshotSummary {
        SignalId signal { };
        std::string current;
        std::string last;
        std::string stored;
        std::vector<RawDriverSummary> raw_drivers;
        std::optional<std::string> owned_raw;
        std::optional<std::string> external_raw;
        std::optional<std::string> force_value;
        std::optional<std::string> force_mask;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        ProcessSchedulingDomain event_domain {
            ProcessSchedulingDomain::generic };
        SchedulerPhase event_phase { SchedulerPhase::active };
        std::uint64_t systemverilog_round { };
        SimulationTick now { };
        std::uint64_t delta { };
        bool materialization_pending { };

        friend bool operator==(
            const SignalSnapshotSummary&,
            const SignalSnapshotSummary&) = default;
    };

    struct PreparedOutputCounters {
        std::uint64_t batches { };
        std::uint64_t seals { };
        std::uint64_t fallbacks { };
        std::uint64_t direct_ready_attempts { };
        std::uint64_t direct_ready_completions { };
        std::uint64_t successor_mask_batches { };
        std::uint64_t region_backend_completions { };
        std::uint64_t a3_mapped_successor_batches { };
        std::uint64_t a3_mapped_successor_readers { };
    };

    struct RegionForwardingCounters {
        std::uint64_t attempts { };
        std::uint64_t evaluations { };
        std::uint64_t member_consumptions { };
        std::uint64_t declines { };
        std::uint64_t private_parent_slots_elided { };
    };

    struct DisjointA4RouteMetrics {
        bool signal_slots_bound { };
        bool versioned_storage_ready { };
        std::array<bool, 2U> owner_slots_bound { };
        std::array<bool, 2U> owner_commit_admitted { };
        std::uint64_t authoritative_revision { };
        std::uint64_t signal_revision { };
        std::uint64_t owner_mirrors { };
        std::array<std::optional<std::uint64_t>, 2U> owner_native_resumes { };
    };

    struct GenericProjectedCounters {
        std::uint64_t attempts { };
        std::uint64_t backend_runs { };
        std::uint64_t completions { };
        std::uint64_t declines { };
        std::uint64_t failures { };
    };

    struct GenericProjectedSignalProof {
        std::size_t component { };
        RegionComponentCertificateStatus status {
            RegionComponentCertificateStatus::no_internal_state };
        ResolutionKind resolution { ResolutionKind::none };
        ValueKind value_kind { ValueKind::logic4 };
        RegionDriverClass drivers { RegionDriverClass::undriven };
        ProcessSchedulingDomain scheduling_domain {
            ProcessSchedulingDomain::generic };
        RegionUpdateKind update_kind { RegionUpdateKind::generic };
        bool structural_internal { };
        bool boundary { };
    };

    class ActivationOnlyBackendProvider final
        : public RegionKernelBackendProvider {
    public:
        explicit ActivationOnlyBackendProvider(
            std::shared_ptr<RegionKernelBackendProvider> provider)
            : provider_(std::move(provider))
            , identity_(std::string { provider_->identity() }
                + ":activation-only-test")
        {
        }

        [[nodiscard]] std::string_view identity() const noexcept override
        {
            return identity_;
        }

        [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
            const RegionConeActivationKernel& kernel) override
        {
            return provider_->create(kernel);
        }

    private:
        std::shared_ptr<RegionKernelBackendProvider> provider_;
        std::string identity_;
    };

    class ForwardingTestBackendProvider final
        : public RegionKernelBackendProvider
        , public RegionConeForwardingBackendProvider {
    public:
        explicit ForwardingTestBackendProvider(
            std::shared_ptr<RegionKernelBackendProvider> provider)
            : provider_(std::move(provider))
            , forwarding_provider_(provider_
                      ? dynamic_cast<RegionConeForwardingBackendProvider*>(
                            provider_.get())
                      : nullptr)
        {
            if (!forwarding_provider_) {
                throw std::invalid_argument {
                    "the forwarding test route requires an LLVM forwarding provider"
                };
            }
            identity_ = std::string { provider_->identity() }
                + ":forwarding-test";
        }

        [[nodiscard]] std::string_view identity() const noexcept override
        {
            return identity_;
        }

        [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
            const RegionConeActivationKernel& kernel) override
        {
            return provider_->create(kernel);
        }

        [[nodiscard]] std::unique_ptr<RegionConeForwardingBackend>
        create_forwarding(
            const RegionConeForwardingKernel& kernel) override
        {
            return forwarding_provider_->create_forwarding(kernel);
        }

    private:
        std::shared_ptr<RegionKernelBackendProvider> provider_;
        RegionConeForwardingBackendProvider* forwarding_provider_ { };
        std::string identity_;
    };

    struct ForwardingFailureProbe {
        static constexpr std::size_t retained_snapshot_count = 2U;

        std::vector<std::uint32_t> output_widths;
        std::vector<PackedLogic4> outputs_before;
        std::vector<std::string> output_text_before;
        std::vector<std::string> failure_expected_text;
        std::vector<PackedLogic4> retained_outputs;
        std::vector<std::string> retained_output_text;
        std::size_t successful_calls { };
        std::size_t successful_calls_before_failure { };
        std::size_t retry_successful_calls { };
        std::size_t retained_snapshots { };
        bool configured { };
        bool failure_enabled { };
        bool failure_attempted { };
        bool retry_allowed { };
        bool allocation_injected { };
        bool backend_declined { };
        bool caller_outputs_unchanged { };
        bool retained_outputs_unchanged { };

        void configure(const RegionConeForwardingKernel& kernel)
        {
            const auto outputs
                = std::span { kernel.execution_kernel.outputs };
            if (configured) {
                if (outputs.size() != output_widths.size()) {
                    throw std::invalid_argument {
                        "the failure probe saw inconsistent output shapes"
                    };
                }
                for (std::size_t index = 0U; index < outputs.size(); ++index) {
                    if (outputs[index].width != output_widths[index]
                        || outputs[index].value_kind != ValueKind::logic4
                        || outputs[index].width <= 128U) {
                        throw std::invalid_argument {
                            "the failure probe requires stable wide Logic4 outputs"
                        };
                    }
                }
                return;
            }

            if (outputs.empty()) {
                throw std::invalid_argument {
                    "the forwarding failure probe needs output slots"
                };
            }
            output_widths.reserve(outputs.size());
            for (const auto& output : outputs) {
                if (output.width <= 128U
                    || output.value_kind != ValueKind::logic4) {
                    throw std::invalid_argument {
                        "the failure probe requires heap-backed wide Logic4 outputs"
                    };
                }
                output_widths.push_back(output.width);
            }
            outputs_before.resize(outputs.size());
            output_text_before.resize(outputs.size());
            failure_expected_text.resize(outputs.size());
            retained_outputs.resize(
                outputs.size() * retained_snapshot_count);
            retained_output_text.resize(
                outputs.size() * retained_snapshot_count);
            configured = true;
        }

        void save_outputs_before(
            const std::span<const PackedLogic4> outputs)
        {
            if (!configured || outputs.size() != outputs_before.size()) {
                throw std::invalid_argument {
                    "the failure probe output span has the wrong shape"
                };
            }
            for (std::size_t index = 0U; index < outputs.size(); ++index) {
                if (outputs[index].width() != output_widths[index]
                    || outputs[index].is_logic9()) {
                    throw std::invalid_argument {
                        "the failure probe output value has the wrong shape"
                    };
                }
                outputs_before[index] = outputs[index];
                output_text_before[index] = outputs[index].to_msb_string();
            }
        }

        void retain_saved_outputs()
        {
            if (retained_snapshots >= retained_snapshot_count) {
                return;
            }
            // The forwarding provider recycles its private output storage
            // through the caller slots. Retaining these owning values makes
            // the later provider-side preparation exercise wide-value COW.
            const auto output_count = output_widths.size();
            const auto base = retained_snapshots * output_count;
            for (std::size_t index = 0U; index < output_count; ++index) {
                retained_outputs[base + index] = outputs_before[index];
                retained_output_text[base + index]
                    = output_text_before[index];
            }
            ++retained_snapshots;
        }

        [[nodiscard]] bool retained_snapshots_match() const
        {
            if (retained_snapshots != retained_snapshot_count) {
                return false;
            }
            for (std::size_t snapshot = 0U;
                 snapshot < retained_snapshots; ++snapshot) {
                const auto base = snapshot * output_widths.size();
                for (std::size_t index = 0U;
                     index < output_widths.size(); ++index) {
                    if (retained_outputs[base + index].to_msb_string()
                        != retained_output_text[base + index]) {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] static bool can_configure(
            const RegionConeForwardingKernel& kernel) noexcept
        {
            const auto& outputs = kernel.execution_kernel.outputs;
            return !outputs.empty()
                && std::all_of(outputs.begin(), outputs.end(),
                    [](const auto& output) {
                        return output.width > 128U
                            && output.value_kind == ValueKind::logic4;
                    });
        }
    };

    class ForwardingFailureBackend final
        : public RegionConeForwardingBackend {
    public:
        ForwardingFailureBackend(
            std::unique_ptr<RegionConeForwardingBackend> backend,
            std::shared_ptr<ForwardingFailureProbe> probe)
            : backend_(std::move(backend))
            , probe_(std::move(probe))
        {
        }

        [[nodiscard]] bool execute_forwarding(
            const RegionKernelSchedulerPrefix& origin,
            const std::span<const PackedLogic4> boundary_inputs,
            const std::span<PackedLogic4> output_values) noexcept override
        {
            try {
                probe_->save_outputs_before(output_values);
                if (probe_->failure_attempted) {
                    if (!probe_->retry_allowed) {
                        return false;
                    }
                    const bool executed = backend_->execute_forwarding(
                        origin, boundary_inputs, output_values);
                    if (executed) {
                        ++probe_->retry_successful_calls;
                    }
                    return executed;
                }

                if (probe_->failure_enabled
                    && probe_->successful_calls
                        >= ForwardingFailureProbe::retained_snapshot_count) {
                    using namespace fsim::tests::runtime::staging_failure_support;
                    probe_->successful_calls_before_failure
                        = probe_->successful_calls;
                    // Keep an independent immutable value oracle before the
                    // failpoint. The provider may return false before native
                    // entry when its retained wide scratch needs preparation.
                    for (std::size_t index = 0U;
                         index < probe_->output_text_before.size(); ++index) {
                        probe_->failure_expected_text[index]
                            = probe_->output_text_before[index];
                    }
                    arm_allocation_failure(0U);
                    const bool executed = backend_->execute_forwarding(
                        origin, boundary_inputs, output_values);
                    probe_->allocation_injected = allocation_failure_was_injected();
                    clear_allocation_failure();
                    probe_->failure_attempted = true;
                    probe_->backend_declined = !executed;
                    probe_->caller_outputs_unchanged = true;
                    for (std::size_t index = 0U;
                         index < output_values.size(); ++index) {
                        probe_->caller_outputs_unchanged
                            = probe_->caller_outputs_unchanged
                            && output_values[index].to_msb_string()
                                == probe_->failure_expected_text[index];
                    }
                    probe_->retained_outputs_unchanged
                        = probe_->retained_snapshots_match();
                    return executed;
                }

                const bool executed = backend_->execute_forwarding(
                    origin, boundary_inputs, output_values);
                if (executed) {
                    probe_->retain_saved_outputs();
                    ++probe_->successful_calls;
                }
                return executed;
            } catch (...) {
                fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
                return false;
            }
        }

    private:
        std::unique_ptr<RegionConeForwardingBackend> backend_;
        std::shared_ptr<ForwardingFailureProbe> probe_;
    };

    class ForwardingFailureBackendProvider final
        : public RegionKernelBackendProvider
        , public RegionConeForwardingBackendProvider {
    public:
        ForwardingFailureBackendProvider(
            std::shared_ptr<RegionKernelBackendProvider> provider,
            std::shared_ptr<ForwardingFailureProbe> probe)
            : provider_(std::move(provider))
            , forwarding_provider_(
                  dynamic_cast<RegionConeForwardingBackendProvider*>(
                      provider_.get()))
            , probe_(std::move(probe))
            , identity_(std::string { provider_->identity() }
                + ":wide-forwarding-failure-test")
        {
            if (forwarding_provider_ == nullptr) {
                throw std::invalid_argument {
                    "the failure wrapper requires a forwarding-capable provider"
                };
            }
        }

        [[nodiscard]] std::string_view identity() const noexcept override
        {
            return identity_;
        }

        [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
            const RegionConeActivationKernel& kernel) override
        {
            return provider_->create(kernel);
        }

        [[nodiscard]] std::unique_ptr<RegionConeForwardingBackend>
        create_forwarding(
            const RegionConeForwardingKernel& kernel) override
        {
            auto backend = forwarding_provider_->create_forwarding(kernel);
            if (!backend) {
                return { };
            }
            if (probe_->configured
                || !ForwardingFailureProbe::can_configure(kernel)) {
                return backend;
            }
            probe_->configure(kernel);
            return std::make_unique<ForwardingFailureBackend>(
                std::move(backend), probe_);
        }

    private:
        std::shared_ptr<RegionKernelBackendProvider> provider_;
        RegionConeForwardingBackendProvider* forwarding_provider_ { };
        std::shared_ptr<ForwardingFailureProbe> probe_;
        std::string identity_;
    };

    [[nodiscard]] static std::shared_ptr<ForwardingFailureProbe>
    install_wide_forwarding_failure_provider(
        fsim::app::Simulation& simulation)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the forwarding failure probe requires an interpreter"
            };
        }
        auto provider
            = application.interpreter->impl_->region_kernel_backend_provider;
        if (!provider) {
            throw std::logic_error {
                "the forwarding failure probe requires the original LLVM provider"
            };
        }
        auto probe = std::make_shared<ForwardingFailureProbe>();
        auto wrapper = std::make_shared<ForwardingFailureBackendProvider>(
            std::move(provider), probe);
        application.interpreter->set_region_kernel_backend_provider(
            std::move(wrapper));
        return probe;
    }

    static void install_activation_only_backend_provider(
        fsim::app::Simulation& simulation)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the activation-only route requires an interpreter"
            };
        }
        auto provider
            = application.interpreter->impl_->region_kernel_backend_provider;
        if (!provider) {
            throw std::logic_error {
                "the activation-only route requires the original LLVM provider"
            };
        }
        application.interpreter->set_region_kernel_backend_provider(
            std::make_shared<ActivationOnlyBackendProvider>(
                std::move(provider)));
    }

    static void install_forwarding_test_backend_provider(
        fsim::app::Simulation& simulation)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the forwarding test route requires an interpreter"
            };
        }
        auto provider
            = application.interpreter->impl_->region_kernel_backend_provider;
        if (!provider) {
            throw std::logic_error {
                "the forwarding test route requires the original LLVM provider"
            };
        }
        application.interpreter->set_region_kernel_backend_provider(
            std::make_shared<ForwardingTestBackendProvider>(
                std::move(provider)));
    }

    [[nodiscard]] static PreparedOutputCounters prepared_output_counters(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the prepared-output probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        return { state.systemverilog_wave_profile_prepared_output_batches,
            state.systemverilog_wave_profile_prepared_output_seals,
            state.systemverilog_wave_profile_prepared_output_fallbacks,
            state.systemverilog_wave_profile_direct_ready_window_attempts,
            state.systemverilog_wave_profile_direct_ready_window_completions,
            state.systemverilog_wave_profile_generated_successor_mask_batches,
            state.systemverilog_wave_profile_region_backend_completions,
            state.systemverilog_wave_profile_a3_mapped_successor_batches,
            state.systemverilog_wave_profile_a3_mapped_successor_readers };
    }

    [[nodiscard]] static GenericProjectedCounters
    generic_projected_counters(const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the generic projected-region probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        return { state.generic_projected_region_attempts,
            state.generic_projected_region_backend_runs,
            state.generic_projected_region_completions,
            state.generic_projected_region_declines,
            state.generic_projected_region_failures };
    }

    [[nodiscard]] static fsim::runtime::RunResult
    run_interpreter_after_application_setup(
        fsim::app::Simulation& simulation,
        const std::optional<fsim::runtime::SimulationTick> until)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the allocation retry probe has no interpreter"
            };
        }
        return application.interpreter->run(until);
    }

    [[nodiscard]] static std::optional<GenericProjectedSignalProof>
    generic_projected_signal_proof(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        if (!state.region_graph
            || signal >= state.region_graph->signals().size()) {
            return std::nullopt;
        }
        const auto& node = state.region_graph->signals()[signal];
        if (node.writers.size() != 1U) {
            return std::nullopt;
        }
        const auto process = node.writers.front().process;
        if (process >= state.region_graph->processes().size()
            || process >= state.region_component_by_process.size()) {
            return std::nullopt;
        }
        const auto component = state.region_component_by_process[process];
        const auto& inventory = state.region_graph->certificate_inventory();
        if (component >= inventory.components.size()) {
            return std::nullopt;
        }
        const auto& certificate = inventory.components[component];
        const auto& process_node = state.region_graph->processes()[process];
        return GenericProjectedSignalProof {
            component,
            certificate.status,
            node.descriptor.resolution,
            node.descriptor.value_kind,
            node.drivers,
            process_node.scheduling_domain,
            process_node.update_kind,
            std::ranges::find(certificate.structural_internal_signal_candidates,
                signal) != certificate.structural_internal_signal_candidates.end(),
            std::ranges::find(certificate.boundary_signals, signal)
                != certificate.boundary_signals.end(),
        };
    }

    [[nodiscard]] static std::size_t
    generic_projected_boundary_reason_count(
        const fsim::app::Simulation& simulation,
        const RegionBoundaryReason reason)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter
            || !application.interpreter->impl_->region_graph) {
            return 0U;
        }
        const auto& counts = application.interpreter->impl_->region_graph
                                 ->certificate_inventory()
                                 .boundary_reason_counts;
        const auto index = static_cast<std::size_t>(reason);
        return index < counts.size() ? counts[index] : 0U;
    }

    [[nodiscard]] static std::optional<bool>
    component_supports_direct_ready_window(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        for (const auto& entry : state.region_kernel_backends_by_component) {
            if (entry == nullptr
                || std::ranges::find(entry->kernel.internal_signals, signal)
                    == entry->kernel.internal_signals.end()) {
                continue;
            }
            const auto* const direct_ready
                = dynamic_cast<const RegionKernelDirectReadyWindowBackend*>(
                    entry->executor.get());
            return direct_ready != nullptr
                && direct_ready->supports_direct_ready_window();
        }
        return std::nullopt;
    }

    [[nodiscard]] static RegionForwardingCounters
    region_forwarding_counters(const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the forwarding probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        return { state.systemverilog_wave_profile_region_forwarding_attempts,
            state.systemverilog_wave_profile_region_forwarding_evaluations,
            state.systemverilog_wave_profile_region_forwarding_member_consumptions,
            state.systemverilog_wave_profile_region_forwarding_declines,
            state
                .systemverilog_wave_profile_region_forwarding_private_parent_slots_elided };
    }

    [[nodiscard]] static std::optional<std::vector<ProcessId>>
    forwarding_process_order(const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        for (const auto& program : state.region_activation_programs) {
            if (!program || !program->forwarding_kernel) {
                continue;
            }
            const auto& forwarding = *program->forwarding_kernel;
            std::vector<ProcessId> result;
            result.reserve(forwarding.topological_member_indices.size());
            for (const auto member_index
                : forwarding.topological_member_indices) {
                if (member_index >= forwarding.members.size()) {
                    throw std::logic_error {
                        "the forwarding order contains an invalid member"
                    };
                }
                result.push_back(
                    forwarding.members[member_index].process);
            }
            return result;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<ProcessId>
    forwarding_output_owner(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        for (const auto& program : state.region_activation_programs) {
            if (!program || !program->forwarding_kernel) {
                continue;
            }
            const auto& outputs
                = program->forwarding_kernel->execution_kernel.outputs;
            std::optional<ProcessId> owner;
            for (const auto& output : outputs) {
                if (output.signal != signal || output.offset != 0U
                    || output.width == 0U
                    || output.value_kind != ValueKind::logic4
                    || output.domain
                        != SignalUpdateDomain::systemverilog_active
                    || output.update_kind
                        != RegionUpdateKind::systemverilog_active) {
                    continue;
                }
                if (owner && *owner != output.owner) {
                    return std::nullopt;
                }
                owner = output.owner;
            }
            if (owner) {
                return owner;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<std::uint64_t>
    native_process_resume_count(
        const fsim::app::Simulation& simulation, const ProcessId process)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& counts
            = application.interpreter->impl_->native_process_resume_counts;
        const auto index = static_cast<std::size_t>(process);
        if (index >= counts.size()) {
            return std::nullopt;
        }
        return counts[index];
    }

    [[nodiscard]] static std::optional<ProcessId>
    forwarding_parent_with_two_outputs(
        const fsim::app::Simulation& simulation,
        const SignalId first_signal, const SignalId second_signal,
        const std::size_t expected_width)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        for (const auto& program : state.region_activation_programs) {
            if (!program || !program->forwarding_kernel) {
                continue;
            }
            const auto& forwarding = *program->forwarding_kernel;
            if (forwarding.internal_signals.size() != 2U
                || std::ranges::find(
                    forwarding.internal_signals, first_signal)
                    == forwarding.internal_signals.end()
                || std::ranges::find(
                    forwarding.internal_signals, second_signal)
                    == forwarding.internal_signals.end()) {
                continue;
            }
            for (const auto& member : forwarding.members) {
                if (member.output_count != 2U
                    || member.output_begin
                        > forwarding.execution_kernel.outputs.size()
                    || member.output_count
                        > forwarding.execution_kernel.outputs.size()
                            - member.output_begin) {
                    continue;
                }
                const auto outputs
                    = std::span<const RegionConeOutputBinding> {
                        forwarding.execution_kernel.outputs }
                          .subspan(member.output_begin, member.output_count);
                const bool owns_first = std::ranges::any_of(outputs,
                    [&](const RegionConeOutputBinding& output) {
                        return output.owner == member.process
                            && output.signal == first_signal
                            && output.offset == 0U
                            && output.width == expected_width
                            && output.value_kind == ValueKind::logic4
                            && output.domain
                                == SignalUpdateDomain::systemverilog_active
                            && output.update_kind
                                == RegionUpdateKind::systemverilog_active;
                    });
                const bool owns_second = std::ranges::any_of(outputs,
                    [&](const RegionConeOutputBinding& output) {
                        return output.owner == member.process
                            && output.signal == second_signal
                            && output.offset == 0U
                            && output.width == expected_width
                            && output.value_kind == ValueKind::logic4
                            && output.domain
                                == SignalUpdateDomain::systemverilog_active
                            && output.update_kind
                                == RegionUpdateKind::systemverilog_active;
                    });
                if (owns_first && owns_second) {
                    return member.process;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<ProcessId>
    forwarding_direct_reader(
        const fsim::app::Simulation& simulation,
        const ProcessId parent_process, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        for (const auto& program : state.region_activation_programs) {
            if (!program || !program->forwarding_kernel) {
                continue;
            }
            const auto& forwarding = *program->forwarding_kernel;
            for (std::size_t parent_index = 0U;
                 parent_index < forwarding.members.size(); ++parent_index) {
                const auto& parent = forwarding.members[parent_index];
                if (parent.process != parent_process
                    || parent.dependency_begin > forwarding.dependencies.size()
                    || parent.dependency_count
                        > forwarding.dependencies.size()
                            - parent.dependency_begin) {
                    continue;
                }
                for (std::size_t reader_index = 0U;
                     reader_index < forwarding.members.size(); ++reader_index) {
                    if (reader_index == parent_index) {
                        continue;
                    }
                    const auto& reader = forwarding.members[reader_index];
                    if (reader.dependency_begin
                            > forwarding.dependencies.size()
                        || reader.dependency_count
                            > forwarding.dependencies.size()
                                - reader.dependency_begin) {
                        continue;
                    }
                    const auto dependencies
                        = std::span<const RegionConeForwardingDependency> {
                            forwarding.dependencies }
                              .subspan(reader.dependency_begin,
                                  reader.dependency_count);
                    if (std::ranges::any_of(dependencies,
                            [&](const RegionConeForwardingDependency& dependency) {
                                return dependency.writer_member_index
                                        == parent_index
                                    && dependency.signal == signal
                                    && dependency.edge == EdgeKind::any;
                            })) {
                        return reader.process;
                    }
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static SignalSnapshot snapshot(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the private metadata probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.signals.size()
            || state.has_container_signal_alias(signal)) {
            throw std::logic_error {
                "the metadata probe requires a non-aliased signal"
            };
        }

        SignalSnapshot result;
        result.signal = signal;
        const AuthoritativeSignalPlanes* authoritative_wide_values { };
        result.materialization_pending
            = signal < state.direct_signal_materialization_pending.size()
            && state.direct_signal_materialization_pending[signal] != 0U;
        const auto width = state.signals[signal].initial_value.width();
        if (width > 64U
            && signal < state.region_authoritative_component_by_signal.size()) {
            const auto component
                = state.region_authoritative_component_by_signal[signal];
            if (component
                    < state.region_authoritative_state_by_component.size()
                && state.region_authoritative_state_by_component[component]
                && state.region_authoritative_state_by_component[component]
                       ->values().packed_signal_slots_bound(signal)) {
                authoritative_wide_values
                    = &state.region_authoritative_state_by_component[component]
                           ->values();
            }
        }
        if (authoritative_wide_values != nullptr) {
            result.current = authoritative_wide_values->current(signal);
            result.last = authoritative_wide_values->previous(signal);
            result.stored = authoritative_wide_values->stored(signal);
        } else if (result.materialization_pending) {
            if (width == 0U || width > 64U) {
                throw std::logic_error {
                    "the passive metadata probe cannot read this pending plane"
                };
            }
            if (state.signals[signal].value_kind == ValueKind::logic9) {
                if (signal >= state.direct_signal_logic9_plane0.size()
                    || signal >= state.direct_signal_logic9_plane1.size()
                    || signal >= state.direct_signal_logic9_plane2.size()
                    || signal >= state.direct_signal_logic9_plane3.size()
                    || signal >= state.direct_signal_last_logic9_plane0.size()
                    || signal >= state.direct_signal_last_logic9_plane1.size()
                    || signal >= state.direct_signal_last_logic9_plane2.size()
                    || signal >= state.direct_signal_last_logic9_plane3.size()) {
                    throw std::logic_error {
                        "the passive metadata probe cannot read this pending Logic9 plane"
                    };
                }
                result.current = PackedLogic4::from_logic9_word(Logic9Word {
                    width,
                    { state.direct_signal_logic9_plane0[signal],
                        state.direct_signal_logic9_plane1[signal],
                        state.direct_signal_logic9_plane2[signal],
                        state.direct_signal_logic9_plane3[signal] }
                });
                result.last = PackedLogic4::from_logic9_word(Logic9Word {
                    width,
                    { state.direct_signal_last_logic9_plane0[signal],
                        state.direct_signal_last_logic9_plane1[signal],
                        state.direct_signal_last_logic9_plane2[signal],
                        state.direct_signal_last_logic9_plane3[signal] }
                });
            } else if (state.signals[signal].value_kind == ValueKind::logic4
                && signal < state.direct_signal_aval.size()
                && signal < state.direct_signal_bval.size()
                && signal < state.direct_signal_last_aval.size()
                && signal < state.direct_signal_last_bval.size()) {
                result.current = PackedLogic4::from_aval_bval(width,
                    state.direct_signal_aval[signal],
                    state.direct_signal_bval[signal]);
                result.last = PackedLogic4::from_aval_bval(width,
                    state.direct_signal_last_aval[signal],
                    state.direct_signal_last_bval[signal]);
            } else {
                throw std::logic_error {
                    "the passive metadata probe cannot read this pending plane"
                };
            }
            // materialize_direct_signal copies the pending current plane
            // into the packed stored role before exposing it publicly.
            result.stored = result.current;
        } else {
            result.current = state.signals[signal].initial_value;
            result.last = state.signal_last_values.at(signal);
            result.stored = state.driven_values.at(signal);
        }
        state.driver_values.at(signal).for_each_in_process_order(
            [&](const DriverRecord& record) {
                const auto value = [&] {
                    if (authoritative_wide_values != nullptr) {
                        return authoritative_wide_values->owner_value(
                            signal, record.process);
                    }
                    if (result.materialization_pending
                        && state.direct_single_driver_record(signal) == &record) {
                        return result.current;
                    }
                    if (state.owned_driver_active(signal)) {
                        return state.owned_driver_value(record.process, signal);
                    }
                    return record.value;
                }();
                result.raw_drivers.push_back(
                    { record.process, value, record.strength });
            });
        if (signal < state.owned_driver_composites.size()
            && state.owned_driver_composites[signal].active) {
            result.owned_raw
                = state.owned_driver_composites[signal].committed;
        }
        if (signal < state.external_driver_values.size()
            && state.external_driver_values[signal]) {
            result.external_raw = *state.external_driver_values[signal];
        }
        if (signal < state.forced_values.size()
            && state.forced_values[signal]) {
            result.force_value = *state.forced_values[signal];
        }
        if (signal < state.forced_masks.size()
            && state.forced_masks[signal]) {
            result.force_mask = *state.forced_masks[signal];
        }
        result.event = state.signal_events.at(signal);
        result.transaction = state.signal_transactions.at(signal);
        const auto& stamp
            = state.signal_event_scheduling_stamps.at(signal);
        result.event_domain = stamp.origin.process_domain;
        result.event_phase = stamp.origin.phase;
        result.systemverilog_round = stamp.systemverilog_round;
        result.now = state.scheduler.now();
        result.delta = state.scheduler.delta();
        return result;
    }

    [[nodiscard]] static SignalSnapshotSummary summarize(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        auto captured = snapshot(simulation, signal);
        const auto value_text = [](const PackedLogic4& value) {
            return value.to_msb_string();
        };
        const auto optional_value_text = [&](const auto& value)
            -> std::optional<std::string> {
            if (!value) {
                return std::nullopt;
            }
            return value_text(*value);
        };
        SignalSnapshotSummary result;
        result.signal = captured.signal;
        result.current = value_text(captured.current);
        result.last = value_text(captured.last);
        result.stored = value_text(captured.stored);
        result.raw_drivers.reserve(captured.raw_drivers.size());
        for (const auto& driver : captured.raw_drivers) {
            result.raw_drivers.push_back({ driver.process,
                value_text(driver.value), driver.strength });
        }
        result.owned_raw = optional_value_text(captured.owned_raw);
        result.external_raw = optional_value_text(captured.external_raw);
        result.force_value = optional_value_text(captured.force_value);
        result.force_mask = optional_value_text(captured.force_mask);
        result.event = captured.event;
        result.transaction = captured.transaction;
        result.event_domain = captured.event_domain;
        result.event_phase = captured.event_phase;
        result.systemverilog_round = captured.systemverilog_round;
        result.now = captured.now;
        result.delta = captured.delta;
        result.materialization_pending = captured.materialization_pending;
        return result;
    }

    /// Overlay only committed private role rows onto an A4-backed frame.
    /// Prepared rows are never evidence of a visible callback.
    static void overlay_applied_forwarding_role_journal(
        const fsim::app::Simulation& simulation,
        const std::span<SignalSnapshotSummary> frame)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the private forwarding probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        auto component = std::numeric_limits<std::size_t>::max();
        for (const auto& summary : frame) {
            if (summary.signal
                    >= state.region_authoritative_component_by_signal.size()) {
                continue;
            }
            const auto candidate
                = state.region_authoritative_component_by_signal[
                    summary.signal];
            if (candidate < state.region_authoritative_state_by_component.size()
                && state.region_authoritative_state_by_component[candidate]
                && state.region_authoritative_state_by_component[candidate]->valid()
                && candidate < state.region_local_wave_state_by_component.size()
                && state.region_local_wave_state_by_component[candidate]
                && state.region_local_wave_state_by_component[candidate]
                       ->forwarding_results) {
                component = candidate;
                break;
            }
        }
        if (component == std::numeric_limits<std::size_t>::max()) {
            return;
        }

        const auto& authoritative
            = *state.region_authoritative_state_by_component[component];
        const auto& values = authoritative.values();
        const auto& layout = values.layout();
        for (auto& summary : frame) {
            if (!layout.contains(summary.signal)
                || !values.packed_signal_slots_bound(summary.signal)) {
                continue;
            }
            summary.current = values.current(summary.signal).to_msb_string();
            summary.last = values.previous(summary.signal).to_msb_string();
            summary.stored = values.stored(summary.signal).to_msb_string();
            for (auto& driver : summary.raw_drivers) {
                if (values.packed_owner_slot_bound(
                        summary.signal, driver.process)) {
                    driver.value = values.owner_value(
                        summary.signal, driver.process).to_msb_string();
                }
            }
        }

        const auto& local
            = *state.region_local_wave_state_by_component[component];
        const auto& bank = *local.forwarding_results;
        if (bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty()) {
            return;
        }
        if (bank.applied_role_mutations.empty()
            || bank.applied_role_mutations.size()
                != bank.applied_role_metadata.size()
            || !bank.role_journal_enabled || local.generation == 0U
            || local.generation != bank.runtime_generation
            || local.generation != state.region_runtime_generation
            || component >= state.region_activation_programs.size()
            || !state.region_activation_programs[component]) {
            throw std::logic_error {
                "the private forwarding probe found an invalid applied journal"
            };
        }

        const auto& outputs
            = state.region_activation_programs[component]
                  ->activation_kernel.outputs;
        std::uint64_t previous_callback_order { };
        for (std::size_t row_index = 0U;
             row_index < bank.applied_role_mutations.size(); ++row_index) {
            const auto& mutation = bank.applied_role_mutations[row_index];
            const auto& metadata = bank.applied_role_metadata[row_index];
            if (metadata.output_index >= outputs.size()
                || metadata.signal >= state.signal_events.size()
                || metadata.signal >= state.signals.size()
                || metadata.signal >= state.signal_transactions.size()
                || metadata.signal >= state.signal_event_scheduling_stamps.size()
                || metadata.signal >= state.signal_value_revisions.size()
                || metadata.signal >= state.direct_signal_materialization_pending.size()
                || state.direct_signal_materialization_pending[metadata.signal]
                    != 0U
                || metadata.callback_order == 0U
                || metadata.callback_order <= previous_callback_order
                || metadata.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || metadata.origin.phase != SchedulerPhase::active
                || metadata.callback_systemverilog_round == 0U
                || !metadata.expected_transaction
                || metadata.callback_time
                    != metadata.expected_transaction->first
                || metadata.callback_delta
                    == std::numeric_limits<std::uint64_t>::max()
                || metadata.expected_transaction->second
                    != metadata.callback_delta + 1U
                || metadata.expected_signal_event
                    != state.signal_events[metadata.signal]
                || metadata.expected_transaction
                    != state.signal_transactions[metadata.signal]
                || metadata.expected_value_revision
                    != state.signal_value_revisions[metadata.signal]
                || metadata.expected_event_stamp.origin.process_domain
                    != state.signal_event_scheduling_stamps[metadata.signal]
                           .origin.process_domain
                || metadata.expected_event_stamp.origin.phase
                    != state.signal_event_scheduling_stamps[metadata.signal]
                           .origin.phase
                || metadata.expected_event_stamp.systemverilog_round
                    != state.signal_event_scheduling_stamps[metadata.signal]
                           .systemverilog_round
                || metadata.output_index
                    >= bank.prepared_role_mutation_ready.size()
                || bank.prepared_role_mutation_ready[metadata.output_index] != 0U
                || mutation.signal != metadata.signal
                || mutation.preflighted || mutation.words.empty()
                || !mutation.any_state_changed) {
                throw std::logic_error {
                    "the private forwarding probe found an unauthenticated applied row"
                };
            }
            previous_callback_order = metadata.callback_order;

            const auto& output = outputs[metadata.output_index];
            if (output.signal != metadata.signal
                || output.owner != metadata.owner || output.offset != 0U
                || output.width == 0U || output.value_kind != ValueKind::logic4
                || output.domain != SignalUpdateDomain::systemverilog_active
                || output.update_kind != RegionUpdateKind::systemverilog_active
                || !layout.contains(metadata.signal)
                || !values.packed_signal_slots_bound(metadata.signal)) {
                throw std::logic_error {
                    "the private forwarding probe found a non-whole applied output"
                };
            }
            const auto& signal_layout = layout.signal(metadata.signal);
            const auto owners = layout.owners(metadata.signal);
            if (signal_layout.storage_class
                    != SignalDriverStorageClass::single_owner
                || signal_layout.value_kind != ValueKind::logic4
                || signal_layout.width != output.width
                || signal_layout.word_count != mutation.words.size()
                || owners.size() != 1U || owners.front().process != metadata.owner
                || mutation.owner_index != signal_layout.first_owner
                || mutation.has_owner == mutation.owner_is_stored_alias
                || owners.front().aliases_stored != mutation.owner_is_stored_alias
                || mutation.words.size()
                    != static_cast<std::size_t>(output.width / 64U)
                        + static_cast<std::size_t>(output.width % 64U != 0U)
                || metadata.output_index >= bank.output_values.size()
                || bank.output_values[metadata.output_index].width()
                    != output.width
                || bank.output_values[metadata.output_index].is_logic9()) {
                throw std::logic_error {
                    "the private forwarding probe found an invalid single-owner row"
                };
            }
            if (std::ranges::any_of(
                    std::span { bank.applied_role_metadata }
                        .first(row_index),
                    [&](const auto& earlier) {
                        return earlier.signal == metadata.signal;
                    })) {
                throw std::logic_error {
                    "the private forwarding probe found duplicate signal rows"
                };
            }

            const auto pack_role = [&](const auto member) {
                std::vector<std::uint64_t> aval;
                std::vector<std::uint64_t> bval;
                aval.reserve(mutation.words.size());
                bval.reserve(mutation.words.size());
                for (std::size_t word_index = 0U;
                     word_index < mutation.words.size(); ++word_index) {
                    const auto& word = mutation.words[word_index];
                    const auto& planes = word.*member;
                    if (word.signal_word
                            != signal_layout.first_value_word + word_index
                        || planes[2U] != 0U || planes[3U] != 0U) {
                        throw std::logic_error {
                            "the private forwarding probe found an invalid role word offset"
                        };
                    }
                    if (mutation.has_owner
                        && word.owner_word
                            != owners.front().first_value_word + word_index) {
                        throw std::logic_error {
                            "the private forwarding probe found an invalid owner word offset"
                        };
                    }
                    aval.push_back(planes[0U]);
                    bval.push_back(planes[1U]);
                }
                return PackedLogic4::from_word_planes(output.width, aval, bval);
            };

            const auto old_current = pack_role(
                &AuthoritativeSignalPlanes::PreparedWord::old_current);
            const auto old_previous = pack_role(
                &AuthoritativeSignalPlanes::PreparedWord::old_previous);
            const auto old_stored = pack_role(
                &AuthoritativeSignalPlanes::PreparedWord::old_stored);
            const auto old_owner = pack_role(
                &AuthoritativeSignalPlanes::PreparedWord::old_owner);
            const auto new_current = pack_role(
                &AuthoritativeSignalPlanes::PreparedWord::new_current);
            const auto new_stored = pack_role(
                &AuthoritativeSignalPlanes::PreparedWord::new_stored);
            const auto new_owner = pack_role(
                &AuthoritativeSignalPlanes::PreparedWord::new_owner);
            const auto& predicted = bank.output_values[metadata.output_index];
            auto summary = std::ranges::find(
                frame, metadata.signal, &SignalSnapshotSummary::signal);
            if (summary == frame.end()
                || old_current != values.current(metadata.signal)
                || old_previous != values.previous(metadata.signal)
                || old_stored != values.stored(metadata.signal)
                || new_current != predicted || new_stored != predicted
                || (mutation.owner_is_stored_alias
                    ? new_owner != new_stored : new_owner != predicted)
                || mutation.any_current_changed
                    != (old_current != new_current)
                || mutation.any_stored_changed
                    != (old_stored != new_stored)
                || mutation.any_owner_changed
                    != (mutation.has_owner && old_owner != new_owner)
                || mutation.any_state_changed
                    != (mutation.any_current_changed
                        || mutation.any_stored_changed
                        || mutation.any_owner_changed)) {
                throw std::logic_error {
                    "the private forwarding probe found inconsistent applied role values"
                };
            }
            if (summary->current != old_current.to_msb_string()
                || summary->last != old_previous.to_msb_string()
                || summary->stored != old_stored.to_msb_string()) {
                throw std::logic_error {
                    "the private forwarding probe did not start from its A4 role baseline"
                };
            }
            if (!values.packed_owner_slot_bound(
                    metadata.signal, metadata.owner)
                || old_owner
                    != values.owner_value(metadata.signal, metadata.owner)) {
                throw std::logic_error {
                    "the private forwarding probe found a mismatched raw owner role"
                };
            }
            const auto driver = std::ranges::find(summary->raw_drivers,
                metadata.owner, &RawDriverSummary::process);
            if ((mutation.has_owner
                    && driver == summary->raw_drivers.end())
                || (driver != summary->raw_drivers.end()
                    && driver->value != old_owner.to_msb_string())) {
                throw std::logic_error {
                    "the private forwarding probe found a mismatched raw driver baseline"
                };
            }

            if (mutation.any_current_changed) {
                summary->current = new_current.to_msb_string();
                summary->last = old_current.to_msb_string();
            }
            if (mutation.any_stored_changed) {
                summary->stored = new_stored.to_msb_string();
            }
            if (mutation.has_owner) {
                if (mutation.any_owner_changed) {
                    driver->value = new_owner.to_msb_string();
                }
            } else if (driver != summary->raw_drivers.end()
                && mutation.any_stored_changed) {
                // This layout aliases the sole owner to stored state; it
                // still may have a DriverTable record for that process.
                driver->value = new_stored.to_msb_string();
            }
        }
    }

    [[nodiscard]] static bool packed_a4_signal_slots_bound(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            return false;
        }
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        return component < state.region_authoritative_state_by_component.size()
            && state.region_authoritative_state_by_component[component]
            && state.region_authoritative_state_by_component[component]
                   ->values().packed_signal_slots_bound(signal);
    }

    [[nodiscard]] static DisjointA4RouteMetrics disjoint_a4_route_metrics(
        const fsim::app::Simulation& simulation,
        const SignalId signal,
        const std::array<ProcessId, 2U>& owners)
    {
        DisjointA4RouteMetrics result;
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return result;
        }
        auto& state = *application.interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            return result;
        }
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]) {
            return result;
        }
        const auto& authoritative
            = *state.region_authoritative_state_by_component[component];
        const auto& values = authoritative.values();
        result.signal_slots_bound
            = values.packed_signal_slots_bound(signal);
        result.versioned_storage_ready
            = values.requires_prewrite_unbind();
        for (std::size_t index = 0U; index < owners.size(); ++index) {
            result.owner_slots_bound[index]
                = values.packed_owner_slot_bound(signal, owners[index]);
            result.owner_commit_admitted[index]
                = state.can_try_wide_disjoint_owner_commit(
                    owners[index], signal);
        }
        result.authoritative_revision = values.revision();
        if (signal < state.signal_value_revisions.size()) {
            result.signal_revision = state.signal_value_revisions[signal];
        }
        result.owner_mirrors
            = state.systemverilog_wave_profile_a4_owner_mirrors;
        for (std::size_t index = 0U; index < owners.size(); ++index) {
            result.owner_native_resumes[index]
                = native_process_resume_count(simulation, owners[index]);
        }
        return result;
    }

    [[nodiscard]] static std::optional<std::array<ProcessId, 2U>>
    compiled_disjoint_update_slice_owners(
        const fsim::app::Simulation& simulation,
        const SignalId signal,
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 2U>& ranges)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        if (!state.region_graph
            || signal >= state.region_graph->signals().size()) {
            return std::nullopt;
        }
        const auto& node = state.region_graph->signals()[signal];
        if (node.drivers != RegionDriverClass::disjoint_partial
            || node.writers_unknown || node.dynamic_fork_writers
            || node.writers.size() != ranges.size()) {
            return std::nullopt;
        }

        std::array<ProcessId, 2U> owners { };
        std::array<bool, 2U> matched { };
        for (const auto& writer : node.writers) {
            std::size_t range_index = ranges.size();
            for (std::size_t index = 0U; index < ranges.size(); ++index) {
                if (writer.offset == ranges[index].first
                    && writer.width == ranges[index].second) {
                    range_index = index;
                    break;
                }
            }
            if (range_index == ranges.size() || matched[range_index]
                || writer.process >= state.processes.size()
                || writer.process >= state.region_graph->processes().size()
                || !state.process_signal_access_is_complete(writer.process)) {
                return std::nullopt;
            }
            const auto& process = state.processes[writer.process];
            if (!process.executor
                || dynamic_cast<const fsim::app::LlvmProcessExecutor *>(
                       process.executor.get()) == nullptr) {
                return std::nullopt;
            }
            const auto program = process.program();
            if (program.driver_regions().size() != 1U) {
                return std::nullopt;
            }
            const auto& driver_region = program.driver_regions().front();
            if (driver_region.signal != signal || driver_region.whole
                || driver_region.offset != writer.offset
                || driver_region.width != writer.width) {
                return std::nullopt;
            }

            std::size_t matching_slice_writes { };
            const auto& operations = program.operations();
            for (std::size_t index = 0U; index < operations.size(); ++index) {
                const auto operation = operations.expanded(index);
                if (const auto* const slice
                    = operation_get_if<WriteUpdateSlice>(&operation)) {
                    if (slice->signal != signal) {
                        continue;
                    }
                    if (slice->offset != writer.offset) {
                        return std::nullopt;
                    }
                    ++matching_slice_writes;
                } else if (const auto* const whole
                    = operation_get_if<WriteUpdate>(&operation)) {
                    if (whole->signal == signal) {
                        return std::nullopt;
                    }
                }
            }
            if (matching_slice_writes != 1U) {
                return std::nullopt;
            }
            owners[range_index] = writer.process;
            matched[range_index] = true;
        }
        if (!std::ranges::all_of(matched, [](const bool value) {
                return value;
            })) {
            return std::nullopt;
        }
        return owners;
    }

    [[nodiscard]] static bool disjoint_region_geometry(
        const fsim::app::Simulation& simulation,
        const SignalId signal,
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 2U>& ranges)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        const auto& state = *application.interpreter->impl_;
        if (!state.region_graph
            || signal >= state.region_graph->signals().size()) {
            return false;
        }
        const auto& node = state.region_graph->signals()[signal];
        if (node.drivers != RegionDriverClass::disjoint_partial
            || node.writers_unknown || node.dynamic_fork_writers
            || node.writers.size() != ranges.size()) {
            return false;
        }
        std::array<bool, 2U> matched { };
        for (const auto& writer : node.writers) {
            std::size_t range_index = ranges.size();
            for (std::size_t index = 0U; index < ranges.size(); ++index) {
                if (writer.offset == ranges[index].first
                    && writer.width == ranges[index].second) {
                    range_index = index;
                    break;
                }
            }
            if (range_index == ranges.size() || matched[range_index]) {
                return false;
            }
            matched[range_index] = true;
        }
        return std::ranges::all_of(matched, [](const bool value) {
            return value;
        });
    }

    [[nodiscard]] static bool has_compiled_unresolved_vhdl_whole_owner(
        const fsim::app::Simulation& simulation, const SignalId signal,
        const ValueKind expected_kind = ValueKind::logic4)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        const auto& state = *application.interpreter->impl_;
        if (!state.region_graph
            || signal >= state.signals.size()
            || signal >= state.region_graph->signals().size()
            || signal >= state.region_authoritative_component_by_signal.size()) {
            return false;
        }

        const auto& node = state.region_graph->signals()[signal];
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (state.signals[signal].resolution != ResolutionKind::none
            || state.signals[signal].value_kind != expected_kind
            || state.signals[signal].systemverilog_scalar
                != SystemVerilogScalarKind::None
            || state.signals[signal].initial_value.width() <= 64U
            || node.descriptor.resolution != ResolutionKind::none
            || node.descriptor.value_kind != expected_kind
            || node.drivers != RegionDriverClass::single_whole
            || node.writers_unknown || node.dynamic_fork_writers
            || node.writers.size() != 1U
            || node.observations != RegionObservation::none
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || component >= state.region_activation_programs.size()
            || !state.region_graph->component_epochs_current(component)
            || state.region_authoritative_state_by_component[component]
                    ->generation() != state.region_runtime_generation
            || !state.region_authoritative_state_by_component[component]
                    ->valid()) {
            return false;
        }

        const auto& writer = node.writers.front();
        if (writer.process >= state.processes.size()
            || writer.process >= state.region_graph->processes().size()
            || writer.process >= state.region_component_by_process.size()
            || state.region_component_by_process[writer.process] != component
            || state.region_graph->processes()[writer.process]
                    .scheduling_domain
                != ProcessSchedulingDomain::generic
            || state.region_graph->processes()[writer.process].update_kind
                != RegionUpdateKind::vhdl_projected
            || !state.process_signal_access_is_complete(writer.process)) {
            return false;
        }

        const auto& runtime_process = state.processes[writer.process];
        if (!runtime_process.executor
            || dynamic_cast<const fsim::app::LlvmProcessExecutor *>(
                   runtime_process.executor.get()) == nullptr) {
            return false;
        }
        const auto* const binding
            = runtime_process.executor->program_access_binding();
        const auto program = runtime_process.program();
        if (binding == nullptr
            || !program.matches_registered_binding(writer.process, *binding)
            || program.scheduling_domain() != ProcessSchedulingDomain::generic) {
            return false;
        }

        std::size_t matching_writes { };
        const auto& operations = program.operations();
        for (std::size_t index = 0U; index < operations.size(); ++index) {
            const auto operation = operations.expanded(index);
            if (const auto* const write
                = operation_get_if<WriteProjected>(&operation)) {
                if (write->signal != signal) {
                    continue;
                }
                if (write->delay != 0U || write->rejection != 0U
                    || write->mode != ProjectedDelayMode::inertial) {
                    return false;
                }
                ++matching_writes;
            } else if (const auto* const slice_write
                = operation_get_if<WriteProjectedSlice>(&operation)) {
                if (slice_write->signal == signal) {
                    return false;
                }
            } else if (const auto* const dynamic_write
                = operation_get_if<WriteProjectedDynamicSlice>(&operation)) {
                if (dynamic_write->signal == signal) {
                    return false;
                }
            } else if (const auto* const waveform_write
                = operation_get_if<WriteProjectedWaveformSlice>(&operation)) {
                if (waveform_write->signal == signal) {
                    return false;
                }
            }
        }
        if (matching_writes != 1U) {
            return false;
        }

        const auto& planes
            = state.region_authoritative_state_by_component[component]->values();
        if (!planes.packed_slots_bound()
            || !planes.packed_signal_slots_bound(signal)
            || !planes.packed_owner_slot_bound(signal, writer.process)
            || state.driver_values.at(signal).find(writer.process) != nullptr
            || planes.owner_value(signal, writer.process)
                != planes.stored(signal)) {
            return false;
        }

        // Count only the normal physical current/LAST/stored bindings and
        // registered DriverRecord owners. The no-resolution owner alias must
        // not contribute a fourth PackedLogic4 binding.
        std::size_t expected_bindings { };
        for (SignalId candidate = 0U;
             candidate < state.signals.size(); ++candidate) {
            if (candidate >= state.region_authoritative_component_by_signal.size()
                || state.region_authoritative_component_by_signal[candidate]
                    != component) {
                continue;
            }
            if (planes.packed_signal_slots_bound(candidate)) {
                expected_bindings += 3U;
            }
            state.driver_values.at(candidate).for_each_in_process_order(
                [&](const DriverRecord& record) {
                    if (planes.packed_owner_slot_bound(
                            candidate, record.process)) {
                        ++expected_bindings;
                    }
                });
        }
        return planes.packed_slot_count() == expected_bindings;
    }

    [[nodiscard]] static std::optional<ProcessId>
    single_whole_writer_process(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        if (!state.region_graph
            || signal >= state.region_graph->signals().size()) {
            return std::nullopt;
        }
        const auto& node = state.region_graph->signals()[signal];
        if (node.drivers != RegionDriverClass::single_whole
            || node.writers.size() != 1U) {
            return std::nullopt;
        }
        return node.writers.front().process;
    }

    [[nodiscard]] static bool has_compiled_wide_vhdl_projected_owners(
        const fsim::app::Simulation& simulation,
        const SignalId signal,
        const std::size_t expected_owner_count)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        const auto& state = *application.interpreter->impl_;
        if (!state.region_graph
            || signal >= state.region_graph->signals().size()
            || signal >= state.region_authoritative_component_by_signal.size()) {
            return false;
        }

        const auto& node = state.region_graph->signals()[signal];
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (node.drivers != RegionDriverClass::disjoint_partial
            || !node.partial_projected_transactions
            || node.writers_unknown || node.dynamic_fork_writers
            || node.descriptor.width <= 64U
            || node.descriptor.value_kind != ValueKind::logic9
            || node.descriptor.resolution != ResolutionKind::std_logic
            || node.writers.size() != expected_owner_count
            || component >= state.region_authoritative_state_by_component.size()
            || !state.region_authoritative_state_by_component[component]
            || component >= state.region_activation_programs.size()
            || state.region_activation_programs[component]) {
            return false;
        }

        const auto& planes
            = state.region_authoritative_state_by_component[component]->values();
        if (!planes.packed_slots_bound()
            || !planes.packed_signal_slots_bound(signal)) {
            return false;
        }

        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
        ranges.reserve(node.writers.size());
        for (const auto& writer : node.writers) {
            if (writer.process >= state.processes.size()
                || writer.process >= state.region_graph->processes().size()
                || writer.process >= state.region_component_by_process.size()
                || state.region_component_by_process[writer.process] != component
                || state.region_graph->processes()[writer.process]
                        .scheduling_domain
                    != ProcessSchedulingDomain::generic
                || state.region_graph->processes()[writer.process].update_kind
                    != RegionUpdateKind::vhdl_projected
                || !state.process_signal_access_is_complete(writer.process)
                || !planes.packed_owner_slot_bound(signal, writer.process)) {
                return false;
            }

            const auto& runtime_process = state.processes[writer.process];
            if (!runtime_process.executor) {
                return false;
            }
            if (dynamic_cast<const fsim::app::LlvmProcessExecutor *>(
                    runtime_process.executor.get()) == nullptr) {
                return false;
            }
            const auto* const binding
                = runtime_process.executor->program_access_binding();
            const auto program = runtime_process.program();
            if (binding == nullptr
                || !program.matches_registered_binding(
                    writer.process, *binding)
                || program.scheduling_domain()
                    != ProcessSchedulingDomain::generic
                || writer.width == 0U
                || writer.offset > node.descriptor.width
                || writer.width > node.descriptor.width - writer.offset) {
                return false;
            }

            std::size_t matching_writes { };
            const auto& operations = program.operations();
            for (std::size_t index = 0U; index < operations.size(); ++index) {
                const auto operation = operations.expanded(index);
                const auto* const write
                    = operation_get_if<WriteProjectedSlice>(&operation);
                if (write == nullptr || write->signal != signal) {
                    continue;
                }
                if (write->offset != writer.offset || write->delay != 0U
                    || write->rejection != 0U
                    || write->mode != ProjectedDelayMode::inertial) {
                    return false;
                }
                ++matching_writes;
            }
            if (matching_writes != 1U) {
                return false;
            }
            ranges.emplace_back(writer.offset, writer.width);
        }

        std::ranges::sort(ranges);
        std::uint32_t covered { };
        for (const auto& [offset, width] : ranges) {
            if (offset != covered || width > node.descriptor.width - covered) {
                return false;
            }
            covered += width;
        }
        return covered == node.descriptor.width;
    }
};

} // namespace fsim::runtime::simir

namespace {

using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::app::SystemVerilogVpiRuntimeUpdates;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunStatus;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::SignalId;
using fsim::runtime::simir::ValueKind;
namespace staging_failure_support
    = fsim::tests::runtime::staging_failure_support;
using staging_failure_support::arm_allocation_failure;
using staging_failure_support::allocation_failure_was_injected;
using staging_failure_support::begin_allocation_count;
using staging_failure_support::clear_allocation_failure;
using staging_failure_support::end_allocation_count;
using staging_failure_support::require;

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error("failed to update process environment");
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        static_cast<void>(set(previous_ ? previous_->c_str() : nullptr));
    }

private:
    [[nodiscard]] bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
        const auto status = value == nullptr
            ? ::unsetenv(name_.c_str())
            : ::setenv(name_.c_str(), value, 1);
        return status == 0;
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

class ScopedCerrCapture final {
public:
    explicit ScopedCerrCapture(std::streambuf& output)
        : previous_(std::cerr.rdbuf(&output))
    {
    }

    ~ScopedCerrCapture()
    {
        std::cerr.rdbuf(previous_);
    }

private:
    std::streambuf* previous_;
};

class FixedCerrBuffer final : public std::streambuf {
public:
    FixedCerrBuffer()
    {
        setp(storage_.data(), storage_.data() + storage_.size());
    }

    [[nodiscard]] std::string_view view() const noexcept
    {
        return { pbase(), static_cast<std::size_t>(pptr() - pbase()) };
    }

    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

protected:
    int_type overflow(const int_type) override
    {
        overflowed_ = true;
        return traits_type::eof();
    }

private:
    std::array<char, 256U * 1024U> storage_ { };
    bool overflowed_ { };
};

[[nodiscard]] std::string bits(const std::uint8_t value)
{
    return std::bitset<8U> { value }.to_string();
}

[[nodiscard]] std::uint64_t profile_count(
    const std::string_view profile, const std::string_view key)
{
    const auto position = profile.find(key);
    require(position != std::string_view::npos,
        "the SystemVerilog wave profile omitted a required counter");
    const auto begin = position + key.size();
    const auto end = profile.find_first_not_of("0123456789", begin);
    require(end != begin,
        "the SystemVerilog wave profile counter is not an unsigned integer");
    return std::stoull(std::string { profile.substr(begin, end - begin) });
}

[[nodiscard]] std::uint64_t projected_profile_count(
    const std::string_view profile, const std::string_view key)
{
    const auto begin = profile.find(
        "fsim-profile: generic-projected-region-summary ");
    require(begin != std::string_view::npos,
        "the generic projected-region profile must be present");
    const auto end = profile.find('\n', begin);
    return profile_count(profile.substr(begin, end - begin), key);
}

[[nodiscard]] bool same_signal_semantics(
    const NativeRegionAllocationTestAccess::SignalSnapshotSummary& left,
    const NativeRegionAllocationTestAccess::SignalSnapshotSummary& right)
{
    // materialization_pending is an implementation detail; native execution
    // may keep the same semantic value pending in its private plane. Compare
    // every public signal value and scheduling/publication field explicitly.
    return left.signal == right.signal
        && left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.raw_drivers == right.raw_drivers
        && left.owned_raw == right.owned_raw
        && left.external_raw == right.external_raw
        && left.force_value == right.force_value
        && left.force_mask == right.force_mask
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_domain == right.event_domain
        && left.event_phase == right.event_phase
        && left.systemverilog_round == right.systemverilog_round
        && left.now == right.now
        && left.delta == right.delta;
}

[[nodiscard]] bool same_signal_published_state(
    const NativeRegionAllocationTestAccess::SignalSnapshotSummary& left,
    const NativeRegionAllocationTestAccess::SignalSnapshotSummary& right)
{
    return left.signal == right.signal
        && left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.raw_drivers == right.raw_drivers
        && left.owned_raw == right.owned_raw
        && left.external_raw == right.external_raw
        && left.force_value == right.force_value
        && left.force_mask == right.force_mask
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_domain == right.event_domain
        && left.event_phase == right.event_phase
        && left.systemverilog_round == right.systemverilog_round;
}

template<std::size_t SignalCount>
[[nodiscard]] bool same_signal_snapshots(
    const std::vector<std::array<
        NativeRegionAllocationTestAccess::SignalSnapshotSummary, SignalCount>>& left,
    const std::vector<std::array<
        NativeRegionAllocationTestAccess::SignalSnapshotSummary, SignalCount>>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t frame = 0U; frame < left.size(); ++frame) {
        for (std::size_t signal = 0U; signal < left[frame].size(); ++signal) {
            if (!same_signal_semantics(
                    left[frame][signal], right[frame][signal])) {
                return false;
            }
        }
    }
    return true;
}

struct RouteResult {
    std::array<std::size_t, 8U> measured_allocations { };
    std::string profile;
};

struct PreparedOutputRouteResult {
    std::array<std::size_t, 4U> measured_allocations { };
    std::vector<std::array<
        NativeRegionAllocationTestAccess::SignalSnapshotSummary, 8U>>
        activation_snapshots;
    std::string profile;
};

struct ForwardingApplicationResult {
    using Frame = std::array<
        NativeRegionAllocationTestAccess::SignalSnapshotSummary, 5U>;
    std::vector<Frame> activation_snapshots;
    NativeRegionAllocationTestAccess::RegionForwardingCounters counters;
    std::string profile;
};

struct ForwardingDiamondApplicationResult {
    using Frame = std::array<
        NativeRegionAllocationTestAccess::SignalSnapshotSummary, 6U>;
    std::vector<Frame> activation_snapshots;
    Frame public_observation_snapshot;
    NativeRegionAllocationTestAccess::RegionForwardingCounters counters;
    std::string profile;
};

struct MultioutputForwardingApplicationResult {
    using Frame = std::array<
        NativeRegionAllocationTestAccess::SignalSnapshotSummary, 5U>;
    std::vector<Frame> activation_snapshots;
    Frame public_observation_snapshot;
    NativeRegionAllocationTestAccess::RegionForwardingCounters counters;
    std::string profile;
};

struct WideForwardingFailureApplicationResult {
    using Frame = ForwardingDiamondApplicationResult::Frame;
    std::vector<Frame> activation_snapshots;
    Frame public_observation_snapshot;
    NativeRegionAllocationTestAccess::RegionForwardingCounters counters;
    std::shared_ptr<
        NativeRegionAllocationTestAccess::ForwardingFailureProbe> probe;
    std::optional<std::size_t> failure_window;
    bool failure_counted_as_forwarding_decline { };
    bool retry_completed_natively { };
};

struct VhdlSignalEvent {
    std::string signal;
    std::string value;
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };

    bool operator==(const VhdlSignalEvent&) const = default;
};

struct VhdlSignalMetadata {
    std::string name;
    NativeRegionAllocationTestAccess::SignalSnapshot snapshot;

    bool operator==(const VhdlSignalMetadata&) const = default;
};

struct VhdlMetadataFrame {
    std::vector<VhdlSignalMetadata> signals;

    bool operator==(const VhdlMetadataFrame&) const = default;
};

using VhdlSignalHandles
    = std::array<std::pair<SignalId, std::string_view>, 7U>;

constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 5U>
    vhdl_input_pairs {{
        { 0xffU, 0xf0U }, { 0x0fU, 0x3cU },
        { 0xaaU, 0x55U }, { 0xaaU, 0x55U },
        { 0xffU, 0x81U },
    }};

[[nodiscard]] std::size_t count_changed_vhdl_input_pairs(
    const std::size_t first_input, const std::size_t input_count)
{
    constexpr std::pair<std::uint8_t, std::uint8_t> initial_inputs {
        0U, 0U
    };
    auto changed = std::size_t { 0U };
    for (std::size_t offset = 0U; offset < input_count; ++offset) {
        const auto index = first_input + offset;
        const auto& current
            = vhdl_input_pairs[index % vhdl_input_pairs.size()];
        const auto& previous = index == 0U
            ? initial_inputs
            : vhdl_input_pairs[(index - 1U) % vhdl_input_pairs.size()];
        if (current != previous) {
            ++changed;
        }
    }
    return changed;
}

[[nodiscard]] VhdlMetadataFrame capture_vhdl_metadata(
    const Simulation& simulation, const VhdlSignalHandles& handles)
{
    VhdlMetadataFrame frame;
    frame.signals.reserve(handles.size());
    for (const auto& [signal, name] : handles) {
        frame.signals.push_back({ std::string { name },
            NativeRegionAllocationTestAccess::snapshot(simulation, signal) });
    }
    return frame;
}

struct VhdlRouteResult {
    std::string profile;
    std::string output;
    std::vector<VhdlMetadataFrame> metadata;
};

enum VhdlCycleBoundarySignal : std::size_t {
    vhdl_cycle_active_source,
    vhdl_cycle_nba_source,
    vhdl_cycle_active_middle,
    vhdl_cycle_active_leaf_output,
    vhdl_cycle_active_output,
    vhdl_cycle_active_seen,
    vhdl_cycle_nba_middle,
    vhdl_cycle_nba_leaf_output,
    vhdl_cycle_nba_output,
    vhdl_cycle_nba_seen,
    vhdl_cycle_signal_count,
};

struct VhdlCycleBoundaryFrame {
    fsim::runtime::SchedulerPhase completed_phase {
        fsim::runtime::SchedulerPhase::active };
    std::array<NativeRegionAllocationTestAccess::SignalSnapshot,
        vhdl_cycle_signal_count> signals;

    bool operator==(const VhdlCycleBoundaryFrame&) const = default;
};

[[nodiscard]] bool same_vhdl_cycle_signal_semantics(
    const NativeRegionAllocationTestAccess::SignalSnapshot& lhs,
    const NativeRegionAllocationTestAccess::SignalSnapshot& rhs)
{
    // This flag records whether the compiled engine has materialized its
    // packed facade. The passive probe reads the authoritative pending
    // planes, so the storage detail can differ while the observable signal
    // roles and scheduling metadata are identical.
    return lhs.signal == rhs.signal
        && lhs.current == rhs.current
        && lhs.last == rhs.last
        && lhs.stored == rhs.stored
        && lhs.raw_drivers == rhs.raw_drivers
        && lhs.owned_raw == rhs.owned_raw
        && lhs.external_raw == rhs.external_raw
        && lhs.force_value == rhs.force_value
        && lhs.force_mask == rhs.force_mask
        && lhs.event == rhs.event
        && lhs.transaction == rhs.transaction
        && lhs.event_domain == rhs.event_domain
        && lhs.event_phase == rhs.event_phase
        && lhs.systemverilog_round == rhs.systemverilog_round
        && lhs.now == rhs.now
        && lhs.delta == rhs.delta;
}

[[nodiscard]] bool same_vhdl_cycle_boundary_frame(
    const VhdlCycleBoundaryFrame& lhs, const VhdlCycleBoundaryFrame& rhs)
{
    return lhs.completed_phase == rhs.completed_phase
        && std::ranges::equal(lhs.signals, rhs.signals,
            same_vhdl_cycle_signal_semantics);
}

[[nodiscard]] bool same_vhdl_cycle_boundary_frames(
    const std::vector<VhdlCycleBoundaryFrame>& lhs,
    const std::vector<VhdlCycleBoundaryFrame>& rhs)
{
    return std::ranges::equal(lhs, rhs,
        same_vhdl_cycle_boundary_frame);
}

struct VhdlCycleBoundaryRun {
    std::vector<VhdlCycleBoundaryFrame> frames;
    std::vector<NativeRegionAllocationTestAccess::GenericProjectedCounters>
        projected_by_frame;
    NativeRegionAllocationTestAccess::GenericProjectedCounters projected;
    std::array<std::optional<
                   NativeRegionAllocationTestAccess::GenericProjectedSignalProof>,
        2U> certificate_proofs;
    std::array<std::optional<
                   NativeRegionAllocationTestAccess::GenericProjectedSignalProof>,
        2U> output_proofs;
    std::size_t unsupported_resolution_boundaries { };
    std::size_t compiled_process_count { };
    RunStatus status { RunStatus::completed };
    fsim::runtime::SimulationTick time { };
};

struct VhdlDisjointProjectedFrame {
    std::array<NativeRegionAllocationTestAccess::SignalSnapshot, 4U> signals;

    bool operator==(const VhdlDisjointProjectedFrame&) const = default;
};

struct VhdlDisjointProjectedResult {
    std::vector<VhdlDisjointProjectedFrame> frames;
    bool compiled_writers { };
    std::size_t compiled_process_count { };
};

struct VhdlObservedRouteResult {
    std::vector<VhdlSignalEvent> events;
    std::string profile;
};

struct ReservationRetryProbe {
    bool saw_allocation_failure { };
    bool committed { };
    std::size_t dispatched { };
    std::uint64_t sequence_after_failure { };
    std::uint64_t sequence_after_commit { };
};

struct ReservationRetryPayload {
    ReservationRetryProbe* probe { };
};

void dispatch_reservation_retry(
    fsim::runtime::Scheduler&,
    const ReservationRetryPayload& payload)
{
    ++payload.probe->dispatched;
}

class ReservationRetryBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    ReservationRetryBatch(ReservationRetryProbe& probe,
        const fsim::runtime::detail::SchedulerTaskDescriptor task) noexcept
        : probe_(probe), task_(task)
    {
    }

    [[nodiscard]] fsim::runtime::SchedulerBatchResult execute(
        fsim::runtime::Scheduler& scheduler,
        const std::span<const std::uint64_t>) override
    {
        const auto frontier = scheduler.current_batch_frontier();
        require(frontier.has_value(),
            "the allocation retry runs inside the matching frontier");
        arm_allocation_failure(0U);
        try {
            auto failed = scheduler
                .reserve_internal_systemverilog_batch_from_frontier(
                    frontier->generation, 4096U);
            if (failed) {
                failed.cancel();
            }
        } catch (const std::bad_alloc&) {
            probe_.saw_allocation_failure = true;
        }
        clear_allocation_failure();
        require(probe_.saw_allocation_failure,
            "the first large reservation must fail before queue mutation");
        probe_.sequence_after_failure
            = scheduler.reserve_order_key(11U).sequence;

        auto retry = scheduler
            .reserve_internal_systemverilog_batch_from_frontier(
                frontier->generation, 1U);
        require(static_cast<bool>(retry),
            "the same frontier must accept a reservation retry");
        const std::array<fsim::runtime::StableOrder, 1U> orders { 11U };
        const std::array<fsim::runtime::detail::SchedulerTaskDescriptor, 1U>
            tasks { task_ };
        probe_.committed = retry.commit(orders, tasks);
        require(probe_.committed,
            "the retry publishes its complete reserved task");
        probe_.sequence_after_commit
            = scheduler.reserve_order_key(12U).sequence;
        return { 1U, { } };
    }

private:
    ReservationRetryProbe& probe_;
    fsim::runtime::detail::SchedulerTaskDescriptor task_;
};

[[nodiscard]] std::filesystem::path write_route_source(
    const std::filesystem::path& root, const bool sv_adapter = false)
{
    const auto source = root / "native_region_route.sv";
    std::ofstream output { source, std::ios::binary };
    output << "module native_region_route;\n";
    if (sv_adapter) {
        // A host deposit has a generic origin. The adapter gives the fixed
        // local-wave chain a genuine SystemVerilog boundary publication.
        output << R"(
  logic [7:0] stimulus;
  logic [7:0] source;
  always @(stimulus)
    source = stimulus;
)";
    } else {
        output << "  logic [7:0] source;\n";
    }
    output << R"(
  wire [7:0] left_branch;
  wire [7:0] right_branch;
  wire [7:0] middle;
  wire [7:0] result;
  wire [7:0] mirror;
  integer output_events = 0;

  assign left_branch = source;
  assign right_branch = source;
  assign middle = left_branch & right_branch;
  assign result = middle;
  assign mirror = right_branch;
  always @(result or mirror)
    output_events = output_events + 1;

  initial begin
    #100;
    $finish;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the fixed native-region source must be written completely");
    return source;
}

[[nodiscard]] fsim::project::Config make_route_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root, const bool sv_adapter = false)
{
    const auto source = write_route_source(root, sv_adapter);
    fsim::project::Config config;
    config.project.name = "native-region-full-route-allocation";
    config.project.top = "sv:work.native_region_route";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] fsim::project::Config make_successor_mask_route_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const bool narrow_direct_ready_input)
{
    const auto source = root / "native_region_successor_route.sv";
    std::ofstream output { source, std::ios::binary };
    const auto input_width = narrow_direct_ready_input ? 1U : 65U;
    const auto right_bit = narrow_direct_ready_input ? 0U : 1U;
    output << "module native_region_successor_route;\n"
           << "  logic [" << (input_width - 1U)
           << ":0] stimulus = '0;\n"
           << "  logic [" << (input_width - 1U) << ":0] source;\n"
           << R"(
  always @(stimulus)
    source = stimulus;

  wire left_branch;
  wire right_branch;
  wire middle;
  wire result;
  wire mirror;
  assign left_branch = source[0];
)" << "  assign right_branch = source[" << right_bit << "];\n"
           << R"(
  assign middle = left_branch & right_branch;
  assign result = middle;
  assign mirror = right_branch;

  initial begin
    #100;
    $finish;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the successor-mask source must be written completely");
    output.close();

    fsim::project::Config config;
    config.project.name = "native-region-successor-mask-route";
    config.project.top = "sv:work.native_region_successor_route";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] std::array<std::string, 4U> run_successor_mask_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const bool narrow_direct_ready_input)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wide_single_owner {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT",
        compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_successor_mask_route_config(
        optimization, root, narrow_direct_ready_input);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the successor-mask route fixture must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, SystemVerilogVpiRuntimeUpdates::omitted);
    if (compiled) {
        NativeRegionAllocationTestAccess::
            install_activation_only_backend_provider(simulation);
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the successor-mask fixture must install compiled processes");
    }

    const auto stimulus
        = simulation.find_signal("native_region_successor_route.stimulus");
    const auto left
        = simulation.find_signal("native_region_successor_route.left_branch");
    const auto right
        = simulation.find_signal("native_region_successor_route.right_branch");
    const auto result
        = simulation.find_signal("native_region_successor_route.result");
    const auto mirror
        = simulation.find_signal("native_region_successor_route.mirror");
    require(stimulus && left && right && result && mirror,
        "the successor-mask route must retain each signal handle");
    static_cast<void>(simulation.read_signal(*result));
    static_cast<void>(simulation.read_signal(*mirror));

    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the successor-mask route must reach the same quiet point");
    const auto before = compiled
        ? NativeRegionAllocationTestAccess::prepared_output_counters(simulation)
        : NativeRegionAllocationTestAccess::PreparedOutputCounters { };
    if (compiled) {
        const auto direct_ready_supported
            = NativeRegionAllocationTestAccess::
                component_supports_direct_ready_window(simulation, *left);
        require(direct_ready_supported.has_value()
                && *direct_ready_supported == narrow_direct_ready_input,
            "the boundary width selects the expected direct-ready capability");
    }
    simulation.deposit_signal(*stimulus,
        PackedLogic4(narrow_direct_ready_input ? 1U : 65U,
            fsim::runtime::Logic4::one));
    require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
        "the successor-mask route must settle its changed boundary input");

    if (compiled) {
        const auto after
            = NativeRegionAllocationTestAccess::prepared_output_counters(
                  simulation);
        const auto successor_delta
            = after.successor_mask_batches - before.successor_mask_batches;
        const auto direct_ready_attempt_delta
            = after.direct_ready_attempts - before.direct_ready_attempts;
        const auto direct_ready_completion_delta
            = after.direct_ready_completions - before.direct_ready_completions;
        const auto mapped_successor_batch_delta
            = after.a3_mapped_successor_batches
            - before.a3_mapped_successor_batches;
        const auto mapped_successor_reader_delta
            = after.a3_mapped_successor_readers
            - before.a3_mapped_successor_readers;
        require(after.successor_mask_batches
                    > before.successor_mask_batches
                && after.region_backend_completions
                    > before.region_backend_completions
                && mapped_successor_batch_delta != 0U
                && mapped_successor_reader_delta != 0U,
            "the real application backend must complete a native "
            "successor-mask activation and consume mapped successors");
        if (narrow_direct_ready_input) {
            require(direct_ready_attempt_delta != 0U
                    && direct_ready_attempt_delta
                        == direct_ready_completion_delta
                    && direct_ready_completion_delta == successor_delta,
                "every direct-ready app activation computes and consumes its successor masks");
        } else {
            require(direct_ready_attempt_delta == 0U
                    && direct_ready_completion_delta == 0U,
                "a 65-bit boundary input retains the non-direct-ready route");
        }
    }
    const std::array<std::string, 4U> values {
        simulation.read_signal(*left).to_msb_string(),
        simulation.read_signal(*right).to_msb_string(),
        simulation.read_signal(*result).to_msb_string(),
        simulation.read_signal(*mirror).to_msb_string(),
    };
    require(values == std::array<std::string, 4U> {
                "1", "1", "1", "1" },
        "the wide-boundary route must preserve each projected internal result");
    return values;
}

[[nodiscard]] fsim::project::Config make_forwarding_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    const auto source = root / "native_region_forwarding.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_region_forwarding;
  logic source;
  wire stage0;
  wire stage1;
  wire stage2;
  wire sink;

  // Deliberately declare assignments from the sink toward the source.
  // Elaboration therefore assigns ProcessIds opposite the dependency order.
  assign sink = stage2;
  assign stage2 = stage1;
  assign stage1 = stage0;
  assign stage0 = source;

  initial begin
    source = 1'b0;
    #2; source = 1'b1;
    #1; source = 1'bx;
    #1; source = 1'bz;
    #1; source = 1'b0;
    #100;
    $finish;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the forwarding fixture must be written completely");

    fsim::project::Config config;
    config.project.name = "native-region-forwarding-application";
    config.project.top = "sv:work.native_region_forwarding";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] std::vector<std::string> forwarding_diamond_source_values(
    const std::size_t width)
{
    std::vector<std::string> values;
    if (width == 1U) {
        values = { "1", "x", "z", "0" };
    } else {
        require(width > 64U,
            "the vector forwarding witness uses a high word above bit 63");
        const auto make_high_state = [width](const char state) {
            return std::string(width - 64U, state) + std::string(64U, '0');
        };
        std::string mixed_high(width - 64U, '0');
        constexpr std::array<char, 4U> states { '1', '0', 'x', 'z' };
        for (std::size_t index = 0U; index < mixed_high.size(); ++index) {
            mixed_high[index] = states[index % states.size()];
        }
        values = { make_high_state('1'), make_high_state('x'),
            make_high_state('z'),
            std::move(mixed_high) + std::string(64U, '0'),
            std::string(width, '0') };
    }

    std::string previous(width, '0');
    for (std::size_t phase = 0U; phase < values.size(); ++phase) {
        require(values[phase].size() == width,
            "each forwarding stimulus must retain the declared vector width");
        if (phase < 4U) {
            require(values[phase] != previous,
                "each of the first four forwarding phases must change the source");
        }
        previous = values[phase];
    }
    return values;
}

[[nodiscard]] std::string normalize_logic_text(std::string value)
{
    for (auto& bit : value) {
        if (bit == 'x') {
            bit = 'X';
        } else if (bit == 'z') {
            bit = 'Z';
        }
    }
    return value;
}

[[nodiscard]] std::string forwarding_diamond_join_value(
    std::string value)
{
    for (auto& bit : value) {
        if (bit == 'z') {
            bit = 'X';
        } else if (bit == 'x') {
            bit = 'X';
        }
    }
    return value;
}

[[nodiscard]] std::vector<std::string> forwarding_add_source_values(
    const std::size_t width)
{
    require(width > 64U,
        "the forwarding add witness requires a value wider than one word");
    std::vector<std::string> values {
        std::string(width, '1'),
        std::string(width, 'x'),
        std::string(width, 'z'),
        std::string(width, '0'),
    };
    auto carry_source = std::string(width, '0');
    const auto low_ones = std::min<std::size_t>(65U, width);
    for (std::size_t bit = 0U; bit < low_ones; ++bit) {
        carry_source[width - bit - 1U] = '1';
    }
    if (carry_source != values.front()) {
        values.push_back(std::move(carry_source));
    }
    return values;
}

[[nodiscard]] std::string forwarding_diamond_add_value(
    std::string value)
{
    if (std::ranges::any_of(value, [](const char bit) {
            return bit == 'x' || bit == 'z';
        })) {
        std::ranges::fill(value, 'X');
        return value;
    }
    for (std::size_t index = 0U; index + 1U < value.size(); ++index) {
        value[index] = value[index + 1U];
    }
    value.back() = '0';
    return value;
}

[[nodiscard]] fsim::project::Config make_forwarding_diamond_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t width = 1U,
    const bool add_unsigned = false)
{
    const auto source = root / "native_region_forwarding_diamond.sv";
    std::ofstream output { source, std::ios::binary };
    const std::string range = width == 1U
        ? std::string { }
        : "[" + std::to_string(width - 1U) + ":0] ";
    const std::string initial_value = width == 1U
        ? "1'b0" : std::to_string(width) + "'b" + std::string(width, '0');
    const auto source_values = add_unsigned
        ? forwarding_add_source_values(width)
        : forwarding_diamond_source_values(width);

    output << "\nmodule native_region_forwarding_diamond(output wire "
           << range << "sink);\n"
           << "  logic " << range << "source;\n"
           << "  wire " << range << "root_value;\n"
           << "  wire " << range << "left_branch;\n"
           << "  wire " << range << "right_branch;\n"
           << "  wire " << range << "joined;\n\n"
           << "  // Keep ProcessIds in dependency order so both branch UPDATEs settle before\n"
           << "  // the multi-parent join callback on a complete input cut.\n"
           << "  assign root_value = source;\n"
           << "  assign left_branch = root_value;\n"
           << "  assign right_branch = root_value;\n"
           << "  assign joined = left_branch "
           << (add_unsigned ? "+" : "&") << " right_branch;\n"
           << "  assign sink = joined;\n\n"
           << "  initial begin\n"
           << "    source = " << initial_value << ";\n";
    for (std::size_t index = 0U; index < source_values.size(); ++index) {
        output << "    #" << (index == 0U ? 2U : 1U) << "; source = ";
        if (width == 1U) {
            output << "1'b" << source_values[index];
        } else {
            output << width << "'b" << source_values[index];
        }
        output << ";\n";
    }
    output << "    #100;\n"
           << "    $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the forwarding diamond fixture must be written completely");

    fsim::project::Config config;
    config.project.name = "native-region-forwarding-diamond-application";
    config.project.top = "sv:work.native_region_forwarding_diamond";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] std::string mixed_forwarding_source_value(
    const std::size_t width)
{
    require(width != 0U,
        "the mixed forwarding fixture requires a nonempty packed value");
    std::string value(width, '0');
    value.front() = 'x';
    if (width > 128U) {
        value[1U] = 'z';
    }
    return value;
}

[[nodiscard]] fsim::project::Config
make_mixed_internal_external_forwarding_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t width)
{
    const auto source
        = root / "native_region_mixed_internal_external_forwarding.sv";
    std::ofstream output { source, std::ios::binary };
    const std::string range = width == 1U
        ? std::string { }
        : "[" + std::to_string(width - 1U) + ":0] ";
    const auto simultaneous_source
        = mixed_forwarding_source_value(width);
    const auto literal = [width](const std::string_view value) {
        return width == 1U
            ? "1'b" + std::string { value }
            : std::to_string(width) + "'b" + std::string { value };
    };

    output << "module native_region_mixed_internal_external_forwarding(\n"
           << "  output wire " << range << "mixed_out,\n"
           << "  output wire " << range << "parent_out\n"
           << ");\n"
           << "  logic " << range << "source;\n"
           << "  logic " << range << "external_input;\n"
           << "  wire " << range << "parent_value;\n\n"
           << "  // Child members precede the parent ProcessId. The later joint\n"
           << "  // source/external change therefore exercises the original\n"
           << "  // callback cut rather than imposing a synthetic barrier.\n"
           << "  assign mixed_out = parent_value ^ external_input;\n"
           << "  assign parent_out = parent_value;\n"
           << "  assign parent_value = source;\n\n"
           << "  initial begin\n"
           << "    source = " << literal(std::string(width, '0')) << ";\n"
           << "    external_input = " << literal(std::string(width, '0'))
           << ";\n"
           << "    #2; external_input = "
           << literal(std::string(width, '1')) << ";\n"
           << "    #1; source = " << literal(simultaneous_source)
           << "; external_input = " << literal(std::string(width, '0'))
           << ";\n"
           << "    #100; $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the mixed forwarding fixture must be written completely");

    fsim::project::Config config;
    config.project.name
        = "native-region-mixed-internal-external-forwarding-application";
    config.project.top
        = "sv:work.native_region_mixed_internal_external_forwarding";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] fsim::project::Config make_multioutput_forwarding_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t width)
{
    const auto source = root / "native_region_multioutput_forwarding.sv";
    std::ofstream output { source, std::ios::binary };
    const std::string range = width == 1U
        ? std::string { }
        : "[" + std::to_string(width - 1U) + ":0] ";
    const std::string initial_value = width == 1U
        ? "1'b0" : std::to_string(width) + "'b" + std::string(width, '0');
    const auto source_values = forwarding_diamond_source_values(width);

    output << "\nmodule native_region_multioutput_forwarding(output wire "
           << range << "left_seen, output wire " << range << "right_seen);\n"
           << "  logic " << range << "source;\n"
           << "  wire " << range << "left;\n"
           << "  wire " << range << "right;\n"
           << "\n"
           << "  // One parsed parent process captures two whole internal outputs.\n"
           << "  assign {left,right} = {source,source};\n"
           << "  // Separate downstream callbacks read each parent output.\n"
           << "  assign left_seen = left;\n"
           << "  assign right_seen = right;\n\n"
           << "  initial begin\n"
           << "    source = " << initial_value << ";\n";
    for (std::size_t index = 0U; index < source_values.size(); ++index) {
        output << "    #" << (index == 0U ? 2U : 1U) << "; source = ";
        if (width == 1U) {
            output << "1'b" << source_values[index];
        } else {
            output << width << "'b" << source_values[index];
        }
        output << ";\n";
    }
    output << "    #100;\n"
           << "    $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the multi-output forwarding fixture must be written completely");

    fsim::project::Config config;
    config.project.name = "native-region-multioutput-forwarding-application";
    config.project.top = "sv:work.native_region_multioutput_forwarding";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] fsim::project::Config make_unequal_depth_forwarding_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t width)
{
    const auto source = root / "native_region_unequal_depth_forwarding.sv";
    std::ofstream output { source, std::ios::binary };
    const auto values = forwarding_diamond_source_values(width);
    const std::string range
        = "[" + std::to_string(width - 1U) + ":0] ";
    const std::string initial_value
        = std::to_string(width) + "'b" + std::string(width, '0');

    output << "\nmodule native_region_unequal_depth_forwarding(output wire "
           << range << "sink);\n"
           << "  logic " << range << "source;\n"
           << "  wire " << range << "a_stage;\n"
           << "  wire " << range << "a_value;\n"
           << "  wire " << range << "b_value;\n"
           << "  wire " << range << "joined;\n\n"
           << "  // The join and shallow writer have smaller original ProcessIds\n"
           << "  // than the deeper root/middle path. The join must decline its\n"
           << "  // mixed input cut and let its original-key callback run.\n"
           << "  assign joined = a_value ^ b_value;\n"
           << "  assign b_value = ~source;\n"
           << "  assign a_value = a_stage;\n"
           << "  assign a_stage = source;\n"
           << "  assign sink = joined;\n\n"
           << "  initial begin\n"
           << "    source = " << initial_value << ";\n";
    for (std::size_t index = 0U; index < values.size(); ++index) {
        output << "    #" << (index == 0U ? 2U : 1U) << "; source = "
               << width << "'b" << values[index] << ";\n";
    }
    output << "    #100;\n"
           << "    $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the unequal-depth forwarding fixture must be written completely");

    fsim::project::Config config;
    config.project.name = "native-region-unequal-depth-forwarding-application";
    config.project.top = "sv:work.native_region_unequal_depth_forwarding";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] std::filesystem::path write_wide_a4_route_source(
    const std::filesystem::path& root)
{
    const auto source = root / "native_wide_a4_route.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_wide_a4_route;
  logic [128:0] source;
  wire [128:0] left_branch;
  wire [128:0] right_branch;
  wire [128:0] target;
  wire [128:0] middle;
  wire [128:0] result;
  wire [128:0] mirror;

  assign left_branch = source;
  assign right_branch = source;
  assign target = left_branch & right_branch;
  assign middle = target;
  assign result = middle;
  assign mirror = right_branch;

  initial begin
    #100;
    $finish;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the wide A4 route source must be written completely");
    return source;
}

[[nodiscard]] fsim::project::Config make_wide_a4_route_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    const auto source = write_wide_a4_route_source(root);
    fsim::project::Config config;
    config.project.name = "native-wide-a4-route-allocation";
    config.project.top = "sv:work.native_wide_a4_route";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] PackedLogic4 wide_route_pattern(const std::uint32_t phase)
{
    constexpr std::size_t width = 129U;
    std::string value(width, '0');
    for (std::size_t bit = 8U; bit < width; ++bit) {
        if ((bit * 17U + static_cast<std::size_t>(phase) * 29U
                + (bit >> 2U) * 7U) % 31U < 15U) {
            value[width - bit - 1U] = '1';
        }
    }
    for (std::size_t bit = 0U; bit < 8U; ++bit) {
        value[width - bit - 1U]
            = ((phase >> bit) & 1U) != 0U ? '1' : '0';
    }
    return PackedLogic4::from_msb_string(value);
}

struct WideA4RouteResult {
    std::array<std::size_t, 8U> measured_allocations { };
    std::string profile;
};

[[nodiscard]] WideA4RouteResult run_wide_a4_app_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t warm_runs,
    const std::size_t measured_runs)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", nullptr };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment wide_a4_commit {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
    ScopedEnvironment disjoint_a4_commit {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_wide_a4_route_config(optimization, root);
    FixedCerrBuffer profile;
    WideA4RouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the wide A4 HDL route must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            SimulationEngine::compiled,
            SystemVerilogVpiRuntimeUpdates::omitted);
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the wide A4 route must execute compiled process bodies");

        const auto source_id = simulation.find_signal(
            "native_wide_a4_route.source");
        const auto target_id = simulation.find_signal(
            "native_wide_a4_route.target");
        const auto middle_id = simulation.find_signal(
            "native_wide_a4_route.middle");
        const auto result_id = simulation.find_signal(
            "native_wide_a4_route.result");
        const auto mirror_id = simulation.find_signal(
            "native_wide_a4_route.mirror");
        require(source_id && target_id && middle_id && result_id && mirror_id,
            "the wide A4 route must retain all internal signal handles");

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the wide A4 route must reach the delayed finish frontier");
        simulation.await_all_native_compilation();
        require(NativeRegionAllocationTestAccess::packed_a4_signal_slots_bound(
                    simulation, *target_id),
            "the wide target must begin with its actual four bound A4 roles");

        constexpr std::array<std::uint32_t, 4U> phases { 0U, 1U, 2U, 3U };
        const std::array<PackedLogic4, phases.size()> inputs {
            wide_route_pattern(phases[0U]),
            wide_route_pattern(phases[1U]),
            wide_route_pattern(phases[2U]),
            wide_route_pattern(phases[3U]),
        };
        for (std::size_t left = 0U; left < inputs.size(); ++left) {
            for (std::size_t right = left + 1U;
                 right < inputs.size(); ++right) {
                require(inputs[left] != inputs[right],
                    "the wide A4 measured stimulus must change each target");
            }
        }
        const auto run_input = [&](const std::size_t index) {
            simulation.deposit_signal(
                *source_id, inputs[index % inputs.size()]);
            const auto run = simulation.run(simulation.now() + 1U);
            require(run.status
                    == RunStatus::time_limit,
                "the wide A4 route must preserve its delayed finish");
        };

        for (std::size_t index = 0U; index < warm_runs; ++index) {
            run_input(index);
            require(NativeRegionAllocationTestAccess::packed_a4_signal_slots_bound(
                        simulation, *target_id),
                "wide warm-up writes must retain target authority");
        }
        simulation.await_all_native_compilation();

        for (std::size_t index = 0U; index < measured_runs; ++index) {
            const auto input_index = (warm_runs + index) % inputs.size();
            if (index < result.measured_allocations.size()) {
                begin_allocation_count();
                try {
                    run_input(input_index);
                } catch (...) {
                    static_cast<void>(end_allocation_count());
                    throw;
                }
                result.measured_allocations[index] = end_allocation_count();
            } else {
                run_input(input_index);
            }
            require(NativeRegionAllocationTestAccess::packed_a4_signal_slots_bound(
                        simulation, *target_id),
                "measured compiled wide writes must stay on the bound A4 route");
        }

        const auto final_index
            = (warm_runs + measured_runs - 1U) % inputs.size();
        const auto previous_index
            = (warm_runs + measured_runs - 2U) % inputs.size();
        const auto roles = NativeRegionAllocationTestAccess::snapshot(
            simulation, *target_id);
        require(roles.current == inputs[final_index]
                && roles.last == inputs[previous_index]
                && roles.stored == inputs[final_index]
                && roles.raw_drivers.size() == 1U
                && roles.raw_drivers.front().value == inputs[final_index],
            "compiled wide A4 publication must update all four roles");
        require(simulation.read_signal_snapshot(*middle_id)
                    == inputs[final_index]
                && simulation.read_signal_snapshot(*result_id)
                    == inputs[final_index]
                && simulation.read_signal_snapshot(*mirror_id)
                    == inputs[final_index],
            "ordinary compiled readers must observe the wide A4 publication");
    }
    require(!profile.overflowed(),
        "the wide A4 profile buffer must remain complete");
    result.profile = profile.view();
    return result;
}

void test_o0_and_o2_wide_a4_app_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-wide-a4-app-route-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto warm_root = optimization_root / "warm-only";
        const auto measured_root = optimization_root / "measured";
        std::filesystem::create_directories(warm_root);
        std::filesystem::create_directories(measured_root);
        const auto warm = run_wide_a4_app_route(
            optimization, warm_root, 4U, 0U);
        const auto measured = run_wide_a4_app_route(
            optimization, measured_root, 4U, 8U);
        require(profile_count(measured.profile,
                    "region_backend_completions=")
                    > profile_count(warm.profile,
                        "region_backend_completions="),
            "the O0/O2 measured window must complete compiled wide regions");
        require(profile_count(warm.profile,
                    "a2_local_update_dispatches=")
                    == 0U
                && profile_count(measured.profile,
                    "a2_local_update_dispatches=")
                    == 0U,
            "the wide scheduled-commit witness must bypass private A2 publication");
        require(profile_count(measured.profile, "slot_bindings=") > 0U,
            "the compiled app must bind its A4 current/LAST/stored/raw roles");
        for (std::size_t index = 0U;
             index < measured.measured_allocations.size(); ++index) {
            const auto allocations = measured.measured_allocations[index];
            require(allocations == 0U,
                "the warmed O0/O2 wide A4 route must allocate nothing");
        }
    }
}

[[nodiscard]] std::filesystem::path write_narrow_disjoint_a4_route_source(
    const std::filesystem::path& root)
{
    const auto source = root / "native_narrow_disjoint_a4_route.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_narrow_disjoint_a4_route;
  logic [31:0] low_input;
  logic [31:0] high_input;
  wire [63:0] target;

  // Runtime-valued continuous assignments remain separate original owners.
  assign target[31:0] = low_input;
  assign target[63:32] = high_input;

  initial begin
    low_input = 32'h00000000;
    high_input = 32'h00000000;
    #2 low_input = 32'h01234567;
    #1 high_input = 32'h89abcdef;
    #1 begin
      low_input = 32'hfedcba98;
      high_input = 32'h76543210;
    end
    #96;
    $finish;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the narrow disjoint A4 source must be written completely");
    return source;
}

[[nodiscard]] fsim::project::Config make_narrow_disjoint_a4_route_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    const auto source = write_narrow_disjoint_a4_route_source(root);
    fsim::project::Config config;
    config.project.name = "native-narrow-disjoint-a4-route";
    config.project.top = "sv:work.native_narrow_disjoint_a4_route";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

struct NarrowDisjointA4ApplicationResult {
    using Frame = std::array<
        NativeRegionAllocationTestAccess::SignalSnapshotSummary, 3U>;
    std::vector<Frame> windows;
    std::optional<std::array<ProcessId, 2U>> compiled_owners;
    NativeRegionAllocationTestAccess::DisjointA4RouteMetrics before;
    NativeRegionAllocationTestAccess::DisjointA4RouteMetrics after;
    std::size_t compiled_process_count { };
    bool has_exact_disjoint_geometry { };
    std::string profile;
};

[[nodiscard]] NarrowDisjointA4ApplicationResult
run_narrow_disjoint_a4_application_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const bool disjoint_a4_enabled)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_a4 {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner_a4 {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        disjoint_a4_enabled ? nullptr : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment native_process_counts {
        "FSIM_PROFILE_NATIVE_PROCESS_COUNTS",
        engine == SimulationEngine::compiled ? "1" : nullptr };
    ScopedEnvironment process_profile { "FSIM_PROFILE_PROCESSES", nullptr };
    ScopedEnvironment checked_update_profile { "FSIM_PROFILE_UPDATES", nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config
        = make_narrow_disjoint_a4_route_config(optimization, root);
    FixedCerrBuffer profile;
    NarrowDisjointA4ApplicationResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the narrow disjoint A4 HDL route must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        simulation.await_all_native_compilation();

        const auto low_id = simulation.find_signal(
            "native_narrow_disjoint_a4_route.low_input");
        const auto high_id = simulation.find_signal(
            "native_narrow_disjoint_a4_route.high_input");
        const auto target_id = simulation.find_signal(
            "native_narrow_disjoint_a4_route.target");
        require(low_id && high_id && target_id,
            "the narrow disjoint A4 route must retain all signal handles");
        constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 2U>
            owner_ranges { std::pair { 0U, 32U }, std::pair { 32U, 32U } };

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the narrow disjoint A4 route must reach its first quiet time");
        simulation.await_all_native_compilation();
        result.compiled_process_count = simulation.compiled_process_count();
        result.has_exact_disjoint_geometry
            = NativeRegionAllocationTestAccess::disjoint_region_geometry(
                simulation, *target_id, owner_ranges);
        if (engine == SimulationEngine::compiled) {
            result.compiled_owners
                = NativeRegionAllocationTestAccess::
                    compiled_disjoint_update_slice_owners(
                        simulation, *target_id, owner_ranges);
        }
        const auto capture_window = [&] {
            result.windows.push_back({
                NativeRegionAllocationTestAccess::summarize(
                    simulation, *low_id),
                NativeRegionAllocationTestAccess::summarize(
                    simulation, *high_id),
                NativeRegionAllocationTestAccess::summarize(
                    simulation, *target_id),
            });
        };
        const std::array<ProcessId, 2U> owners = result.compiled_owners
            .value_or(std::array<ProcessId, 2U> { 0U, 1U });
        result.before = NativeRegionAllocationTestAccess::
            disjoint_a4_route_metrics(simulation, *target_id, owners);
        capture_window();

        require(simulation.run(2U).status == RunStatus::time_limit,
            "the first low-owner change must settle at its original update key");
        capture_window();
        require(simulation.run(3U).status == RunStatus::time_limit,
            "the high-owner change must settle at its original update key");
        capture_window();
        require(simulation.run(4U).status == RunStatus::time_limit,
            "the simultaneous owner updates must settle at their original keys");
        capture_window();
        require(simulation.run(5U).status == RunStatus::time_limit,
            "the settled disjoint owners must remain stable before finish");
        capture_window();
        const auto final_run = simulation.run();
        require(final_run.status == RunStatus::stopped
                && simulation.now() == 100U,
            "the narrow disjoint A4 route must stop at its delayed finish");
        result.after = NativeRegionAllocationTestAccess::
            disjoint_a4_route_metrics(simulation, *target_id, owners);
        capture_window();
    }
    require(!profile.overflowed(),
        "the narrow disjoint A4 profile buffer must remain complete");
    result.profile = profile.view();
    return result;
}

void test_o0_and_o2_narrow_disjoint_owner_a4_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-narrow-disjoint-a4-route-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    const auto expected_target
        = bits(0x76U) + bits(0x54U) + bits(0x32U) + bits(0x10U)
        + bits(0xfeU) + bits(0xdcU) + bits(0xbaU) + bits(0x98U);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
        const auto compiled_root
            = root.path / optimization_name / "compiled-a4";
        const auto fallback_root
            = root.path / optimization_name / "compiled-checked";
        const auto reference_root
            = root.path / optimization_name / "interpreter-checked";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(fallback_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled = run_narrow_disjoint_a4_application_route(
            optimization, compiled_root, SimulationEngine::compiled, true);
        const auto compiled_checked = run_narrow_disjoint_a4_application_route(
            optimization, fallback_root, SimulationEngine::compiled, false);
        const auto reference = run_narrow_disjoint_a4_application_route(
            optimization, reference_root, SimulationEngine::interpreter, false);
        require(compiled.has_exact_disjoint_geometry
                && compiled.compiled_owners.has_value()
                && compiled.compiled_process_count >= 2U,
            "parsed disjoint slices must remain two compiled original owners");
        require(compiled.windows.size() == 6U
                && same_signal_snapshots(
                    compiled.windows, compiled_checked.windows)
                && same_signal_snapshots(
                    compiled_checked.windows, reference.windows),
            "narrow O0/O2 A4 updates preserve checked full-role and event parity at every time cut");
        require(compiled.windows.back()[2U].current == expected_target
                && compiled.windows.back()[2U].stored == expected_target
                && compiled.windows.back()[2U].raw_drivers.size() == 2U,
            "both original narrow owners publish the expected final 64-bit net");
        require(compiled.before.signal_slots_bound
                && compiled.before.versioned_storage_ready
                && compiled.before.owner_slots_bound[0U]
                && compiled.before.owner_slots_bound[1U]
                && compiled.before.owner_commit_admitted[0U]
                && compiled.before.owner_commit_admitted[1U]
                && compiled.after.signal_slots_bound
                && compiled.after.versioned_storage_ready
                && compiled.after.owner_slots_bound[0U]
                && compiled.after.owner_slots_bound[1U]
                && compiled.after.owner_commit_admitted[0U]
                && compiled.after.owner_commit_admitted[1U]
                && compiled.after.authoritative_revision
                    > compiled.before.authoritative_revision
                && compiled.after.signal_revision
                    > compiled.before.signal_revision
                && compiled.after.owner_mirrors > compiled.before.owner_mirrors,
            "default narrow disjoint updates must publish through versioned A4 owner slots");
        require(compiled.before.owner_native_resumes[0U]
                && compiled.before.owner_native_resumes[1U]
                && compiled.after.owner_native_resumes[0U]
                && compiled.after.owner_native_resumes[1U]
                && *compiled.after.owner_native_resumes[0U]
                    > *compiled.before.owner_native_resumes[0U]
                && *compiled.after.owner_native_resumes[1U]
                    > *compiled.before.owner_native_resumes[1U],
            "both original compiled owners must execute native callbacks after startup");
        require(!compiled_checked.after.signal_slots_bound
                && !compiled_checked.after.owner_slots_bound[0U]
                && !compiled_checked.after.owner_slots_bound[1U]
                && !compiled_checked.after.owner_commit_admitted[0U]
                && !compiled_checked.after.owner_commit_admitted[1U],
            "explicit disjoint A4 opt-out must retain checked publication");
        require(profile_count(compiled.profile, "owner_mirrors=") != 0U,
            "the parsed compiled witness must expose A4 owner writes");
    }
}

[[nodiscard]] std::filesystem::path write_vhdl_route_source(
    const std::filesystem::path& root)
{
    const auto source = root / "native_vhdl_projected_route.vhd";
    std::ofstream output { source, std::ios::binary };
    output << R"(
entity native_vhdl_projected_route is
  port (
    a : in bit_vector(7 downto 0);
    b : in bit_vector(7 downto 0);
    y : out bit_vector(7 downto 0)
  );
end entity;

architecture rtl of native_vhdl_projected_route is
  signal left_branch : bit_vector(7 downto 0);
  signal right_branch : bit_vector(7 downto 0);
  signal middle : bit_vector(7 downto 0);
  signal result : bit_vector(7 downto 0);
begin
  left_branch <= a;
  right_branch <= b;
  middle <= left_branch and right_branch;
  result <= middle xor a;
  y <= result;

  finish_process: process
  begin
    wait for 100 ns;
    wait;
  end process;
end architecture;
)";
    require(static_cast<bool>(output),
        "the VHDL projected-route source must be written completely");
    return source;
}

constexpr std::array<std::uint32_t, 4U> wide_vhdl_projected_widths {
    65U, 129U, 256U, 1024U
};

[[nodiscard]] std::filesystem::path write_vhdl_wide_projected_source(
    const std::filesystem::path& root, const std::uint32_t width,
    const std::uint64_t finish_delay_ns = 100U,
    const bool shared_pair_writer = false,
    const ValueKind value_kind = ValueKind::logic4)
{
    const auto source = root / "native_vhdl_wide_projected_route.vhd";
    std::ofstream output { source, std::ios::binary };
    const auto vector_type = value_kind == ValueKind::logic9
        ? "std_ulogic_vector" : "bit_vector";
    if (value_kind == ValueKind::logic9) {
        output << "library ieee;\nuse ieee.std_logic_1164.all;\n\n";
    }
    output << "entity native_vhdl_wide_projected_route is\n"
              "  port (\n";
    bool first_port = true;
    const std::array<std::pair<std::string_view, std::string_view>, 3U>
        ports {{
            { "a", "in" }, { "b", "in" }, { "y", "out" },
        }};
    for (const auto& [name, direction] : ports) {
        if (!first_port) {
            output << ";\n";
        }
        first_port = false;
        output << "    " << name << width << " : " << direction
               << " " << vector_type << "(" << width - 1U
               << " downto 0)";
    }
    output << "\n  );\nend entity;\n\n"
              "architecture rtl of native_vhdl_wide_projected_route is\n"
           << "  signal left" << width << ", right" << width
           << ", middle" << width << ", result" << width
           << " : " << vector_type << "(" << width - 1U
           << " downto 0);\n"
           << "begin\n";
    if (shared_pair_writer) {
        output << "  paired_writer" << width << ": process (a" << width
               << ", b" << width << ")\n"
               << "  begin\n"
               << "    left" << width << " <= a" << width << ";\n"
               << "    right" << width << " <= b" << width << ";\n"
               << "  end process;\n";
    } else {
        output << "  left" << width << " <= a" << width << ";\n"
               << "  right" << width << " <= b" << width << ";\n";
    }
    output << "  middle" << width << " <= left" << width
           << " and right" << width << ";\n"
           << "  result" << width << " <= middle" << width
           << " xor a" << width << ";\n"
           << "  y" << width << " <= result" << width << ";\n"
           << "  finish_process: process\n"
           << "  begin\n"
           << "    wait for " << finish_delay_ns << " ns;\n"
           << "    wait;\n"
           << "  end process;\n"
           << "end architecture;\n";
    require(static_cast<bool>(output),
        "the wide VHDL projected-route source must be written completely");
    return source;
}

[[nodiscard]] fsim::project::Config make_vhdl_wide_projected_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root, const std::uint32_t width,
    const std::uint64_t finish_delay_ns = 100U,
    const bool shared_pair_writer = false,
    const ValueKind value_kind = ValueKind::logic4)
{
    const auto source
        = write_vhdl_wide_projected_source(
            root, width, finish_delay_ns, shared_pair_writer, value_kind);
    fsim::project::Config config;
    config.project.name = "native-vhdl-wide-projected-region-route";
    config.project.top = "vhdl:work.native_vhdl_wide_projected_route(rtl)";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] std::filesystem::path write_vhdl_disjoint_projected_source(
    const std::filesystem::path& root)
{
    const auto source = root / "native_vhdl_disjoint_projected_route.vhd";
    std::ofstream output { source, std::ios::binary };
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity native_vhdl_disjoint_projected_route is
  port (
    low_input : in std_logic_vector(63 downto 0);
    high_input : in std_logic_vector(64 downto 0);
    y : out std_logic_vector(128 downto 0)
  );
end entity;

architecture rtl of native_vhdl_disjoint_projected_route is
  signal target : std_logic_vector(128 downto 0);
begin
  target(63 downto 0) <= low_input;
  target(128 downto 64) <= high_input;
  y <= target;

  finish_process: process
  begin
    wait for 100 ns;
    wait;
  end process;
end architecture;
)";
    require(static_cast<bool>(output),
        "the VHDL disjoint-projected source must be written completely");
    return source;
}

[[nodiscard]] fsim::project::Config make_vhdl_route_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    const auto source = write_vhdl_route_source(root);
    fsim::project::Config config;
    config.project.name = "native-vhdl-projected-region-route";
    config.project.top = "vhdl:work.native_vhdl_projected_route(rtl)";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] fsim::project::Config make_vhdl_disjoint_projected_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    const auto source = write_vhdl_disjoint_projected_source(root);
    fsim::project::Config config;
    config.project.name = "native-vhdl-disjoint-projected-a4-route";
    config.project.top
        = "vhdl:work.native_vhdl_disjoint_projected_route(rtl)";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] std::string vhdl_logic9_pattern(
    const std::size_t width, const std::uint32_t phase)
{
    constexpr std::array<char, 9U> values {
        'U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-'
    };
    std::string result(width, '0');
    for (std::size_t bit = 0U; bit < width; ++bit) {
        const auto value_index
            = (bit * 5U + static_cast<std::size_t>(phase) * 2U)
            % values.size();
        result[width - bit - 1U] = values[value_index];
    }
    return result;
}

[[nodiscard]] VhdlRouteResult run_vhdl_native_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t warm_runs,
    const std::size_t measured_runs)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", nullptr };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_vhdl_route_config(optimization, root);
    FixedCerrBuffer profile;
    VhdlRouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the VHDL projected-route fixture must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            SimulationEngine::compiled,
            SystemVerilogVpiRuntimeUpdates::omitted);
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the VHDL witness must use compiled process execution");

        const auto input_a = simulation.find_signal(
            "native_vhdl_projected_route.a");
        const auto input_b = simulation.find_signal(
            "native_vhdl_projected_route.b");
        const auto left = simulation.find_signal(
            "native_vhdl_projected_route.left_branch");
        const auto right = simulation.find_signal(
            "native_vhdl_projected_route.right_branch");
        const auto middle = simulation.find_signal(
            "native_vhdl_projected_route.middle");
        const auto output_result = simulation.find_signal(
            "native_vhdl_projected_route.result");
        const auto output = simulation.find_signal(
            "native_vhdl_projected_route.y");
        require(input_a && input_b && left && right && middle
                && output_result && output,
            "the VHDL route must retain all signal handles");
        const VhdlSignalHandles handles {{
            { *input_a, "a" }, { *input_b, "b" },
            { *left, "left" }, { *right, "right" },
            { *middle, "middle" }, { *output_result, "result" },
            { *output, "y" },
        }};

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the delayed VHDL finish must leave later work pending");
        simulation.await_all_native_compilation();
        result.metadata.reserve(warm_runs + measured_runs + 1U);
        result.metadata.push_back(capture_vhdl_metadata(simulation, handles));

        const auto run_input = [&](const std::size_t index) {
            const auto [a, b]
                = vhdl_input_pairs[index % vhdl_input_pairs.size()];
            simulation.deposit_signal(*input_a,
                PackedLogic4::from_msb_string(bits(a)));
            simulation.deposit_signal(*input_b,
                PackedLogic4::from_msb_string(bits(b)));
            require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
                "the VHDL native route must retain its delayed finish");
            result.metadata.push_back(
                capture_vhdl_metadata(simulation, handles));
        };
        for (std::size_t index = 0U; index < warm_runs; ++index) {
            run_input(index);
        }
        for (std::size_t index = 0U; index < measured_runs; ++index) {
            run_input(warm_runs + index);
        }
        if (warm_runs + measured_runs != 0U) {
            const auto [a, b]
                = vhdl_input_pairs[(warm_runs + measured_runs - 1U)
                    % vhdl_input_pairs.size()];
            result.output
                = simulation.read_signal_snapshot(*output).to_msb_string();
            require(result.output == bits(
                        static_cast<std::uint8_t>((a & b) ^ a)),
                "the VHDL projected region must publish its final value");
        }
    }
    require(!profile.overflowed(),
        "the VHDL region profile buffer must remain complete");
    result.profile = profile.view();
    return result;
}

[[nodiscard]] std::filesystem::path write_vhdl_cycle_boundary_sv_source(
    const std::filesystem::path& root)
{
    const auto source = root / "native_vhdl_cycle_boundary.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_vhdl_cycle_boundary;
  logic active_source;
  logic nba_source;
  logic active_seen;
  logic nba_seen;
  wire active_output;
  wire nba_output;

  mixed_language_leaf active_leaf(
    .a(active_source), .y(active_output));
  mixed_language_leaf nba_leaf(
    .a(nba_source), .y(nba_output));

  always @(posedge active_output) begin
    active_seen = 1'b1;
  end

  always @(posedge nba_output) begin
    nba_seen = 1'b1;
  end

  initial begin
    active_source = 1'b0;
    nba_source = 1'b0;
    active_seen = 1'b0;
    nba_seen = 1'b0;
    #1 begin
      active_source = 1'b1;
      nba_source <= 1'b1;
    end
    #100;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the mixed VHDL cycle-boundary source must be written completely");
    return source;
}

[[nodiscard]] std::filesystem::path write_vhdl_cycle_boundary_native_leaf(
    const std::filesystem::path& root)
{
    const auto source = root / "native_vhdl_cycle_boundary_leaf.vhd";
    std::ofstream output { source, std::ios::binary };
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity native_vhdl_cycle_boundary_leaf is
  port (
    a : in std_ulogic;
    y : out std_ulogic
  );
end entity;

architecture rtl of native_vhdl_cycle_boundary_leaf is
  signal middle : std_ulogic := '0';
begin
  middle <= a;
  y <= middle;
end architecture;
)";
    require(static_cast<bool>(output),
        "the unresolved VHDL cycle leaf must be written completely");
    return source;
}

[[nodiscard]] fsim::project::Config make_vhdl_cycle_boundary_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const bool unresolved_leaf)
{
    const auto sv_source = write_vhdl_cycle_boundary_sv_source(root);
    auto vhdl_leaf = unresolved_leaf
        ? write_vhdl_cycle_boundary_native_leaf(root)
        : std::filesystem::path { __FILE__ }
              .parent_path().parent_path()
            / "fixtures/systemverilog/scheduling/mixed_language_leaf.vhd";
    if (!unresolved_leaf && !vhdl_leaf.is_absolute()) {
        vhdl_leaf = std::filesystem::absolute(vhdl_leaf);
    }
    require(std::filesystem::exists(vhdl_leaf),
        unresolved_leaf
            ? "the unresolved cycle witness must retain its generated VHDL leaf"
            : "the checked cycle witness must reuse the existing mixed-language VHDL leaf");

    fsim::project::Config config;
    config.project.name = unresolved_leaf
        ? "native-vhdl-cycle-boundary-unresolved"
        : "native-vhdl-cycle-boundary-checked";
    config.project.top = "sv:work.native_vhdl_cycle_boundary";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;

    fsim::project::SourceSet vhdl_sources;
    vhdl_sources.language = fsim::project::Language::vhdl;
    vhdl_sources.standard = "2008";
    vhdl_sources.library = "work";
    vhdl_sources.compilation_unit = "file";
    vhdl_sources.files.push_back(vhdl_leaf);
    config.source_sets.push_back(std::move(vhdl_sources));

    fsim::project::SourceSet sv_sources;
    sv_sources.language = fsim::project::Language::system_verilog;
    sv_sources.standard = "2017";
    sv_sources.library = "work";
    sv_sources.compilation_unit = "file";
    sv_sources.files.push_back(sv_source);
    config.source_sets.push_back(std::move(sv_sources));
    const auto target = unresolved_leaf
        ? "vhdl:work.native_vhdl_cycle_boundary_leaf(rtl)"
        : "vhdl:work.mixed_language_leaf(rtl)";
    config.bindings = {
        { "native_vhdl_cycle_boundary.active_leaf", target, std::nullopt },
        { "native_vhdl_cycle_boundary.nba_leaf", target, std::nullopt },
    };
    return config;
}

[[nodiscard]] VhdlCycleBoundaryRun run_vhdl_cycle_boundary(
    const fsim::project::Optimization optimization,
    const SimulationEngine engine,
    const std::filesystem::path& root,
    const bool unresolved_leaf)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", nullptr };
    ScopedEnvironment wave_profile {
        "FSIM_PROFILE_SV_WAVES",
        engine == SimulationEngine::compiled ? "1" : nullptr };
    const auto config = make_vhdl_cycle_boundary_config(
        optimization, root, unresolved_leaf);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    require(project.has_value(),
        "the mixed-language VHDL cycle-boundary design must elaborate");

    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, SystemVerilogVpiRuntimeUpdates::omitted);
    if (engine == SimulationEngine::compiled) {
        simulation.await_all_native_compilation();
    }

    constexpr std::array<std::string_view, vhdl_cycle_signal_count> names {
        "native_vhdl_cycle_boundary.active_source",
        "native_vhdl_cycle_boundary.nba_source",
        "native_vhdl_cycle_boundary.active_leaf.middle",
        "native_vhdl_cycle_boundary.active_leaf.y",
        "native_vhdl_cycle_boundary.active_output",
        "native_vhdl_cycle_boundary.active_seen",
        "native_vhdl_cycle_boundary.nba_leaf.middle",
        "native_vhdl_cycle_boundary.nba_leaf.y",
        "native_vhdl_cycle_boundary.nba_output",
        "native_vhdl_cycle_boundary.nba_seen",
    };
    std::array<SignalId, vhdl_cycle_signal_count> handles { };
    for (std::size_t index = 0U; index < names.size(); ++index) {
        const auto signal = simulation.find_signal(names[index]);
        require(signal.has_value(),
            "the mixed-language cycle witness must retain every signal handle");
        handles[index] = *signal;
    }

    VhdlCycleBoundaryRun result;
    result.compiled_process_count = simulation.compiled_process_count();
    result.frames.reserve(16U);
    result.projected_by_frame.reserve(16U);
    const auto hook = simulation.add_safe_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::SchedulerPhase phase) {
            if (scheduler.now() != 1U || scheduler.delta() > 8U
                || (phase != fsim::runtime::SchedulerPhase::active
                    && phase != fsim::runtime::SchedulerPhase::update)) {
                return;
            }
            VhdlCycleBoundaryFrame frame;
            frame.completed_phase = phase;
            for (std::size_t index = 0U; index < handles.size(); ++index) {
                frame.signals[index]
                    = NativeRegionAllocationTestAccess::snapshot(
                        simulation, handles[index]);
            }
            result.frames.push_back(std::move(frame));
            result.projected_by_frame.push_back(
                NativeRegionAllocationTestAccess::generic_projected_counters(
                    simulation));
        });

    simulation.start();
    result.certificate_proofs = {
        NativeRegionAllocationTestAccess::generic_projected_signal_proof(
            simulation, handles[vhdl_cycle_active_middle]),
        NativeRegionAllocationTestAccess::generic_projected_signal_proof(
            simulation, handles[vhdl_cycle_nba_middle]),
    };
    result.output_proofs = {
        NativeRegionAllocationTestAccess::generic_projected_signal_proof(
            simulation, handles[vhdl_cycle_active_output]),
        NativeRegionAllocationTestAccess::generic_projected_signal_proof(
            simulation, handles[vhdl_cycle_nba_output]),
    };
    result.unsupported_resolution_boundaries
        = NativeRegionAllocationTestAccess::generic_projected_boundary_reason_count(
            simulation,
            fsim::runtime::simir::RegionBoundaryReason::unsupported_resolution_mode);
    const auto run = simulation.run(1U);
    simulation.remove_safe_point_hook(hook);
    result.status = run.status;
    result.time = run.time;
    result.projected
        = NativeRegionAllocationTestAccess::generic_projected_counters(
            simulation);
    return result;
}

[[nodiscard]] std::filesystem::path write_vhdl_logic9_cycle_source(
    const std::filesystem::path& root)
{
    const auto source = root / "native_vhdl_logic9_cycle.vhd";
    std::ofstream output { source, std::ios::binary };
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity native_vhdl_logic9_cycle is
  port (
    source : in std_ulogic;
    result : out std_ulogic
  );
end entity;

architecture rtl of native_vhdl_logic9_cycle is
  signal middle : std_ulogic := '0';
begin
  middle <= source;
  result <= middle;
end architecture;
)";
    require(static_cast<bool>(output),
        "the homogeneous Logic9 VHDL cycle source must be written completely");
    return source;
}

[[nodiscard]] fsim::project::Config make_vhdl_logic9_cycle_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    const auto source = write_vhdl_logic9_cycle_source(root);
    fsim::project::Config config;
    config.project.name = "native-vhdl-logic9-cycle";
    config.project.top = "vhdl:work.native_vhdl_logic9_cycle(rtl)";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;

    fsim::project::SourceSet vhdl_sources;
    vhdl_sources.language = fsim::project::Language::vhdl;
    vhdl_sources.standard = "2008";
    vhdl_sources.library = "work";
    vhdl_sources.compilation_unit = "file";
    vhdl_sources.files.push_back(source);
    config.source_sets.push_back(std::move(vhdl_sources));
    return config;
}

enum VhdlLogic9CycleSignal : std::size_t {
    vhdl_logic9_source,
    vhdl_logic9_middle,
    vhdl_logic9_result,
    vhdl_logic9_signal_count,
};

struct VhdlLogic9CycleFrame {
    fsim::runtime::SchedulerPhase completed_phase {
        fsim::runtime::SchedulerPhase::active };
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };
    std::array<NativeRegionAllocationTestAccess::SignalSnapshot,
        vhdl_logic9_signal_count> signals;
};

[[nodiscard]] bool same_vhdl_logic9_cycle_frame(
    const VhdlLogic9CycleFrame& lhs, const VhdlLogic9CycleFrame& rhs)
{
    return lhs.completed_phase == rhs.completed_phase
        && lhs.time == rhs.time
        && lhs.delta == rhs.delta
        && std::ranges::equal(lhs.signals, rhs.signals,
            same_vhdl_cycle_signal_semantics);
}

struct VhdlLogic9CycleRun {
    std::vector<VhdlLogic9CycleFrame> frames;
    std::vector<NativeRegionAllocationTestAccess::GenericProjectedCounters>
        projected_by_frame;
    NativeRegionAllocationTestAccess::GenericProjectedCounters projected;
    std::array<std::optional<
                   NativeRegionAllocationTestAccess::GenericProjectedSignalProof>,
        2U> certificate_proofs;
    std::size_t compiled_process_count { };
    RunStatus status { RunStatus::completed };
    fsim::runtime::SimulationTick time { };
};

[[nodiscard]] VhdlLogic9CycleRun run_vhdl_logic9_cycle(
    const fsim::project::Optimization optimization,
    const SimulationEngine engine,
    const std::filesystem::path& root)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? nullptr : "0" };
    ScopedEnvironment wave_profile {
        "FSIM_PROFILE_SV_WAVES", compiled ? "1" : nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_vhdl_logic9_cycle_config(optimization, root);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    require(project.has_value(),
        "the homogeneous Logic9 VHDL cycle must elaborate");

    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, SystemVerilogVpiRuntimeUpdates::omitted);
    if (compiled) {
        simulation.await_all_native_compilation();
    }
    constexpr std::array<std::string_view, vhdl_logic9_signal_count> names {
        "native_vhdl_logic9_cycle.source",
        "native_vhdl_logic9_cycle.middle",
        "native_vhdl_logic9_cycle.result",
    };
    std::array<SignalId, vhdl_logic9_signal_count> handles { };
    for (std::size_t index = 0U; index < names.size(); ++index) {
        const auto signal = simulation.find_signal(names[index]);
        require(signal.has_value(),
            "the homogeneous Logic9 VHDL route must retain all signal handles");
        handles[index] = *signal;
    }

    VhdlLogic9CycleRun result;
    result.compiled_process_count = simulation.compiled_process_count();
    result.frames.reserve(48U);
    result.projected_by_frame.reserve(48U);
    const auto hook = simulation.add_safe_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::SchedulerPhase phase) {
            if (scheduler.now() > 4U || scheduler.delta() > 8U
                || (phase != fsim::runtime::SchedulerPhase::active
                    && phase != fsim::runtime::SchedulerPhase::update)) {
                return;
            }
            VhdlLogic9CycleFrame frame;
            frame.completed_phase = phase;
            frame.time = scheduler.now();
            frame.delta = scheduler.delta();
            for (std::size_t index = 0U; index < handles.size(); ++index) {
                frame.signals[index]
                    = NativeRegionAllocationTestAccess::snapshot(
                        simulation, handles[index]);
            }
            result.frames.push_back(std::move(frame));
            result.projected_by_frame.push_back(
                NativeRegionAllocationTestAccess::generic_projected_counters(
                    simulation));
        });

    simulation.start();
    if (compiled) {
        simulation.await_all_native_compilation();
    }
    result.certificate_proofs = {
        NativeRegionAllocationTestAccess::generic_projected_signal_proof(
            simulation, handles[vhdl_logic9_middle]),
        NativeRegionAllocationTestAccess::generic_projected_signal_proof(
            simulation, handles[vhdl_logic9_result]),
    };

    const auto run_to = [&](const fsim::runtime::SimulationTick time) {
        const auto run = simulation.run(time);
        require(run.status == RunStatus::time_limit && run.time == time,
            "the homogeneous Logic9 cycle must stop at its requested time");
    };
    const auto deposit = [&](const std::string_view value) {
        simulation.deposit_signal(handles[vhdl_logic9_source],
            PackedLogic4::from_msb_string(value));
    };

    deposit("0");
    run_to(1U);
    auto previous_backend_runs
        = NativeRegionAllocationTestAccess::generic_projected_counters(
            simulation).backend_runs;
    constexpr std::array<std::string_view, 3U> transitions {
        "1", "Z", "0"
    };
    fsim::runtime::SimulationTick time = 1U;
    for (const auto next_value : transitions) {
        deposit(next_value);
        run_to(time + 1U);
        const auto counters
            = NativeRegionAllocationTestAccess::generic_projected_counters(
                simulation);
        if (compiled) {
            require(counters.backend_runs > previous_backend_runs
                    && counters.completions > 0U,
                "each Logic9 transition must enter the generated generic backend");
        }
        previous_backend_runs = counters.backend_runs;
        ++time;
    }
    simulation.remove_safe_point_hook(hook);
    const auto final_run = simulation.run(time + 1U);
    result.status = final_run.status;
    result.time = final_run.time;
    result.projected
        = NativeRegionAllocationTestAccess::generic_projected_counters(
            simulation);
    return result;
}

struct WideVhdlProjectedRouteFrame {
    std::vector<NativeRegionAllocationTestAccess::SignalSnapshotSummary>
        signals;
};

struct WideVhdlProjectedRouteResult {
    std::vector<WideVhdlProjectedRouteFrame> frames;
    std::string profile;
};

[[nodiscard]] std::string wide_vhdl_bits(
    const std::uint32_t width, const std::size_t phase)
{
    std::string value(width, '0');
    for (std::size_t bit = 0U; bit < width; ++bit) {
        const auto pattern = (bit * 17U + phase * 13U + bit / 5U) % 19U;
        value[static_cast<std::size_t>(width) - bit - 1U]
            = pattern < 9U ? '1' : '0';
    }
    return value;
}

[[nodiscard]] WideVhdlProjectedRouteResult run_vhdl_wide_projected_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine, const std::uint32_t width,
    const bool shared_pair_writer = false,
    const ValueKind value_kind = ValueKind::logic4)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? nullptr : "0" };
    ScopedEnvironment wave_profile {
        "FSIM_PROFILE_SV_WAVES", compiled ? "1" : nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };
    ScopedEnvironment wide_owner_policy {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT",
        compiled ? nullptr : "0" };

    const auto config = make_vhdl_wide_projected_config(
        optimization, root, width, 100U, shared_pair_writer, value_kind);
    FixedCerrBuffer profile;
    WideVhdlProjectedRouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the wide VHDL projected-route fixture must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the wide VHDL route must retain compiled process bodies");
        }

        const std::array<std::string_view, 7U> names {
            "a", "b", "left", "right", "middle", "result", "y",
        };
        std::array<SignalId, 7U> group { };
        for (std::size_t signal_index = 0U;
             signal_index < names.size(); ++signal_index) {
            const auto full_name
                = "native_vhdl_wide_projected_route."
                + std::string { names[signal_index] }
                + std::to_string(width);
            const auto signal = simulation.find_signal(full_name);
            require(signal.has_value(),
                "the wide VHDL route must retain every chain signal");
            group[signal_index] = *signal;
        }
        const auto capture_frame = [&] {
            if (compiled && shared_pair_writer) {
                const auto left_owner
                    = NativeRegionAllocationTestAccess::single_whole_writer_process(
                        simulation, group[2U]);
                const auto right_owner
                    = NativeRegionAllocationTestAccess::single_whole_writer_process(
                        simulation, group[3U]);
                require(left_owner && right_owner
                        && *left_owner == *right_owner,
                    "one ordinary VHDL process owns both wide whole outputs");
            }
            WideVhdlProjectedRouteFrame frame;
            frame.signals.reserve(group.size());
            for (const auto signal : group) {
                frame.signals.push_back(
                    NativeRegionAllocationTestAccess::summarize(
                        simulation, signal));
            }
            if (compiled && width > 64U) {
                for (std::size_t signal_index = 2U;
                     signal_index <= 5U; ++signal_index) {
                    require(NativeRegionAllocationTestAccess::
                                has_compiled_unresolved_vhdl_whole_owner(
                                    simulation, group[signal_index], value_kind),
                        "each eligible unresolved whole writer uses the "
                        "stored-owner alias without a duplicate slot");
                }
            }
            result.frames.push_back(std::move(frame));
        };

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the wide VHDL finish process must leave future work pending");
        if (compiled) {
            simulation.await_all_native_compilation();
        }
        capture_frame();

        const auto stimulus_count = value_kind == ValueKind::logic9
            ? 9U : 6U;
        constexpr std::string_view logic9_symbols = "UX01ZWLH-";
        std::array<bool, 9U> observed_logic9_a { };
        std::array<bool, 9U> observed_logic9_b { };
        for (std::size_t phase = 1U; phase <= stimulus_count; ++phase) {
            std::array<std::string, 4U> expected;
            if (value_kind == ValueKind::logic9) {
                expected[0U] = vhdl_logic9_pattern(
                    width, static_cast<std::uint32_t>(phase));
                expected[1U] = vhdl_logic9_pattern(
                    width, static_cast<std::uint32_t>(phase + 2U));
                for (std::size_t bit = 0U; bit < width; ++bit) {
                    const auto state_a = logic9_symbols.find(expected[0U][bit]);
                    const auto state_b = logic9_symbols.find(expected[1U][bit]);
                    require(state_a != std::string_view::npos
                            && state_b != std::string_view::npos,
                        "Logic9 stimulus contains only declared nine-state values");
                    observed_logic9_a[state_a] = true;
                    observed_logic9_b[state_b] = true;
                }
                if (width >= logic9_symbols.size()) {
                    for (const auto symbol : logic9_symbols) {
                        require(expected[0U].find(symbol) != std::string::npos
                                && expected[1U].find(symbol) != std::string::npos,
                            "each wide Logic9 input deposit includes all nine states");
                    }
                }
            } else if (phase == 1U) {
                expected[0U].assign(width, '1');
                expected[1U].assign(width, '0');
            } else if (phase == 2U) {
                expected[0U].assign(width, '0');
                expected[1U].assign(width, '0');
            } else {
                expected[0U] = wide_vhdl_bits(width, phase * 3U);
                expected[1U] = wide_vhdl_bits(width, phase * 7U + 1U);
            }
            expected[2U].resize(width, '0');
            expected[3U].resize(width, '0');
            const auto packed_input = [value_kind](const std::string& text) {
                return value_kind == ValueKind::logic9
                    ? PackedLogic4::from_logic9_msb_string(text)
                    : PackedLogic4::from_msb_string(text);
            };
            simulation.deposit_signal(group[0U], packed_input(expected[0U]));
            simulation.deposit_signal(group[1U], packed_input(expected[1U]));

            if (value_kind == ValueKind::logic4) {
                for (std::size_t bit = 0U; bit < width; ++bit) {
                    const auto index
                        = static_cast<std::size_t>(width) - bit - 1U;
                    expected[2U][index]
                        = expected[0U][index] == '1'
                            && expected[1U][index] == '1' ? '1' : '0';
                    expected[3U][index]
                        = expected[2U][index] == expected[0U][index]
                            ? '0' : '1';
                }
            }
            const auto run = simulation.run(simulation.now() + 1U);
            require(run.status == RunStatus::time_limit,
                "each wide VHDL input update must settle before capture");
            capture_frame();
            const auto& frame = result.frames.back();
            require(frame.signals.size() == group.size()
                    && frame.signals[0U].current == expected[0U]
                    && frame.signals[1U].current == expected[1U]
                    && frame.signals[2U].current == expected[0U]
                    && frame.signals[3U].current == expected[1U]
                    && (value_kind == ValueKind::logic9
                        || (frame.signals[4U].current == expected[2U]
                            && frame.signals[5U].current == expected[3U]
                            && frame.signals[6U].current == expected[3U])),
                "wide projected Logic4 values or Logic9 inputs must match");
        }
        if (value_kind == ValueKind::logic9) {
            require(std::all_of(observed_logic9_a.begin(),
                        observed_logic9_a.end(),
                        [](const bool observed) { return observed; })
                    && std::all_of(observed_logic9_b.begin(),
                        observed_logic9_b.end(),
                        [](const bool observed) { return observed; }),
                "each Logic9 input exercises all nine states across settled phases");
        }
    }
    require(!profile.overflowed(),
        "the wide VHDL projected-route profile must remain complete");
    result.profile = profile.view();
    return result;
}

using WideProjectedFailureFrame = std::array<
    NativeRegionAllocationTestAccess::SignalSnapshotSummary, 7U>;

struct WideProjectedFailureSweepResult {
    std::vector<WideProjectedFailureFrame> frames;
    std::vector<std::size_t> phases;
    std::size_t normal_run_allocations { };
    std::size_t injected_failures { };
    std::size_t propagated_generic_failures { };
    std::size_t pre_body_generic_failures { };
    std::size_t post_body_generic_failures { };
    std::size_t scheduler_failures { };
    std::size_t optional_reservation_fallbacks { };
    std::optional<std::size_t> captured_failure_cut;
    bool pre_completion_output_state_preserved { };
};

[[nodiscard]] WideProjectedFailureFrame capture_wide_projected_frame(
    const Simulation& simulation,
    const std::array<SignalId, 7U>& signals)
{
    WideProjectedFailureFrame frame;
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        frame[index] = NativeRegionAllocationTestAccess::summarize(
            simulation, signals[index]);
    }
    return frame;
}

[[nodiscard]] std::pair<std::string, std::string>
wide_projected_inputs(const std::size_t phase)
{
    return { wide_vhdl_bits(129U, phase * 3U),
        wide_vhdl_bits(129U, phase * 7U + 1U) };
}

[[nodiscard]] WideProjectedFailureSweepResult
run_wide_projected_failure_sweep(const std::filesystem::path& root)
{
    constexpr std::uint32_t width = 129U;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_vhdl_wide_projected_config(
        fsim::project::Optimization::o0, root, width, 100'000U);
    FixedCerrBuffer profile;
    WideProjectedFailureSweepResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the 129-bit failure-witness fixture must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            SimulationEngine::compiled,
            SystemVerilogVpiRuntimeUpdates::omitted);
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the failure witness must install compiled process bodies");

        const std::array<std::string_view, 7U> names {
            "a", "b", "left", "right", "middle", "result", "y",
        };
        std::array<SignalId, 7U> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto full_name
                = "native_vhdl_wide_projected_route."
                + std::string { names[index] } + std::to_string(width);
            const auto signal = simulation.find_signal(full_name);
            require(signal.has_value(),
                "the failure witness must retain every chain signal");
            signals[index] = *signal;
        }
        const auto queue_inputs = [&](const std::size_t phase) {
            const auto [input_a, input_b] = wide_projected_inputs(phase);
            simulation.deposit_signal(signals[0U],
                PackedLogic4::from_msb_string(input_a));
            simulation.deposit_signal(signals[1U],
                PackedLogic4::from_msb_string(input_b));
        };
        const auto record_phase = [&](const std::size_t phase) {
            result.phases.push_back(phase);
            result.frames.push_back(
                capture_wide_projected_frame(simulation, signals));
        };

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the long delayed finish must leave the failure route available");
        simulation.await_all_native_compilation();
        result.frames.push_back(
            capture_wide_projected_frame(simulation, signals));

        const auto before_warm
            = NativeRegionAllocationTestAccess::generic_projected_counters(
                simulation);
        queue_inputs(1U);
        require(NativeRegionAllocationTestAccess::
                    run_interpreter_after_application_setup(
                        simulation, simulation.now() + 1U).status
                == RunStatus::time_limit,
            "the failure witness warmup must settle normally");
        record_phase(1U);
        const auto after_warm
            = NativeRegionAllocationTestAccess::generic_projected_counters(
                simulation);
        require(after_warm.backend_runs > before_warm.backend_runs
                && after_warm.completions > before_warm.completions,
            "the 129-bit warmup must complete through the native backend");

        queue_inputs(2U);
        const auto before_normal
            = NativeRegionAllocationTestAccess::generic_projected_counters(
                simulation);
        begin_allocation_count();
        RunStatus normal_status { };
        try {
            normal_status
                = NativeRegionAllocationTestAccess::
                      run_interpreter_after_application_setup(
                          simulation, simulation.now() + 1U).status;
        } catch (...) {
            static_cast<void>(end_allocation_count());
            clear_allocation_failure();
            throw;
        }
        result.normal_run_allocations = end_allocation_count();
        require(normal_status == RunStatus::time_limit,
            "the measured native allocation window must settle normally");
        record_phase(2U);
        const auto after_normal
            = NativeRegionAllocationTestAccess::generic_projected_counters(
                simulation);
        require(after_normal.backend_runs > before_normal.backend_runs
                && after_normal.completions > before_normal.completions,
            "the measured baseline must complete through the native backend");

        // Simulation::run intentionally latches the app object as poisoned
        // after any exception. This witness targets runtime scheduler retry,
        // so all measured runs use the already prepared interpreter directly.
        // Each cut is one-shot. A thrown allocation error must be retried at
        // the same absolute deadline so the scheduler can reoffer retained
        // work. Capture a frame only after the original stimulus settles.
        for (std::size_t cut = 0U;
             cut < result.normal_run_allocations; ++cut) {
            const auto phase = 3U + cut;
            queue_inputs(phase);
            const auto before_attempt
                = capture_wide_projected_frame(simulation, signals);
            const auto deadline = simulation.now() + 1U;
            const auto before_failure
                = NativeRegionAllocationTestAccess::generic_projected_counters(
                    simulation);
            arm_allocation_failure(cut);
            RunStatus attempt_status { };
            bool threw_bad_alloc { };
            try {
                attempt_status = NativeRegionAllocationTestAccess::
                    run_interpreter_after_application_setup(
                        simulation, deadline).status;
            } catch (const std::bad_alloc&) {
                if (!allocation_failure_was_injected()) {
                    clear_allocation_failure();
                    throw;
                }
                threw_bad_alloc = true;
            } catch (...) {
                clear_allocation_failure();
                throw;
            }
            const bool injected = allocation_failure_was_injected();
            clear_allocation_failure();
            result.injected_failures += injected ? 1U : 0U;
            const auto after_failure
                = NativeRegionAllocationTestAccess::generic_projected_counters(
                    simulation);
            const auto backend_delta
                = after_failure.backend_runs - before_failure.backend_runs;
            const auto completion_delta
                = after_failure.completions - before_failure.completions;

            if (threw_bad_alloc) {
                require(injected,
                    "only an observed failpoint allocation may enter retry");
                require(after_failure.failures >= before_failure.failures
                        && after_failure.declines >= before_failure.declines,
                    "generic-region counters must remain monotonic after failure");

                const auto failure_delta
                    = after_failure.failures - before_failure.failures;
                const auto decline_delta
                    = after_failure.declines - before_failure.declines;
                if (failure_delta != 0U) {
                    require(failure_delta == 1U
                            && decline_delta == 0U
                            && after_failure.attempts
                                > before_failure.attempts,
                        "a captured backend allocation failure records an "
                        "attempt and one failure without becoming a decline");
                    require(backend_delta >= completion_delta
                            && backend_delta - completion_delta <= 1U,
                        "backend entry may be absent or one incomplete run ahead "
                        "of completed generic work");
                    ++result.propagated_generic_failures;
                    if (backend_delta == completion_delta) {
                        ++result.pre_body_generic_failures;
                    } else {
                        ++result.post_body_generic_failures;
                    }
                } else {
                    require(backend_delta == completion_delta,
                        "an uncaptured allocation failure cannot leave a partial "
                        "generic backend run");
                    ++result.scheduler_failures;
                }

                if (completion_delta == 0U) {
                    const auto after_failed_attempt
                        = capture_wide_projected_frame(simulation, signals);
                    for (std::size_t signal = 2U; signal < signals.size();
                         ++signal) {
                        require(same_signal_published_state(
                                    before_attempt[signal],
                                    after_failed_attempt[signal]),
                            "a pre-completion allocation failure must not "
                            "publish any wide-chain output or metadata");
                    }
                    if (failure_delta != 0U
                        && backend_delta == completion_delta) {
                        result.pre_completion_output_state_preserved = true;
                        if (!result.captured_failure_cut) {
                            result.captured_failure_cut = cut;
                        }
                    }
                }

                const auto retry_before
                    = NativeRegionAllocationTestAccess::generic_projected_counters(
                        simulation);
                require(NativeRegionAllocationTestAccess::
                            run_interpreter_after_application_setup(
                                simulation, deadline).status
                        == RunStatus::time_limit,
                    "the same queued stimulus must settle at its original deadline "
                    "after allocation failure");
                const auto retry_after
                    = NativeRegionAllocationTestAccess::generic_projected_counters(
                        simulation);
                require(retry_after.failures == retry_before.failures
                        && retry_after.declines == retry_before.declines,
                    "the allocation retry must complete without another failure "
                    "or checked decline");
                if (failure_delta != 0U) {
                    require(retry_after.attempts > retry_before.attempts
                            && retry_after.backend_runs
                                > retry_before.backend_runs
                            && retry_after.completions
                                > retry_before.completions,
                        "the retained generic work must complete through the "
                        "native backend on same-deadline retry");
                }
                record_phase(phase);
                continue;
            }

            require(attempt_status == RunStatus::time_limit,
                "a nonthrowing allocation cut must leave the run settled");
            if (injected) {
                require(after_failure.failures == before_failure.failures
                        && after_failure.declines == before_failure.declines
                        && after_failure.attempts
                            > before_failure.attempts
                        && after_failure.backend_runs
                            > before_failure.backend_runs
                        && after_failure.completions
                            > before_failure.completions,
                    "a caught optional reservation failure must fall back and "
                    "complete without backend failure or decline");
                ++result.optional_reservation_fallbacks;
            }
            record_phase(phase);
        }
        if (result.normal_run_allocations != 0U) {
            require(result.injected_failures != 0U,
                "the measured run allocation bound must contain an injected cut");
            require(result.propagated_generic_failures != 0U,
                "the measured allocation sweep must reach a captured generic "
                "backend failure");
        }

        const auto retry_phase = result.phases.back() + 1U;
        queue_inputs(retry_phase);
        const auto before_retry
            = NativeRegionAllocationTestAccess::generic_projected_counters(
                simulation);
        require(NativeRegionAllocationTestAccess::
                    run_interpreter_after_application_setup(
                        simulation, simulation.now() + 1U).status
                == RunStatus::time_limit,
            "the allocation-failure route must permit a later native retry");
        record_phase(retry_phase);
        const auto after_retry
            = NativeRegionAllocationTestAccess::generic_projected_counters(
                simulation);
        require(after_retry.backend_runs > before_retry.backend_runs
                && after_retry.completions > before_retry.completions,
            "the post-failure retry must complete through the native backend");
    }
    require(!profile.overflowed(),
        "the generic projected failure profile must remain complete");
    return result;
}

[[nodiscard]] std::vector<WideProjectedFailureFrame>
run_wide_projected_failure_reference(
    const std::filesystem::path& root,
    const std::span<const std::size_t> phases)
{
    constexpr std::uint32_t width = 129U;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_vhdl_wide_projected_config(
        fsim::project::Optimization::o0, root, width, 100'000U);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the wide projected interpreter twin must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::interpreter,
        SystemVerilogVpiRuntimeUpdates::omitted);

    const std::array<std::string_view, 7U> names {
        "a", "b", "left", "right", "middle", "result", "y",
    };
    std::array<SignalId, 7U> signals { };
    for (std::size_t index = 0U; index < names.size(); ++index) {
        const auto full_name = "native_vhdl_wide_projected_route."
            + std::string { names[index] } + std::to_string(width);
        const auto signal = simulation.find_signal(full_name);
        require(signal.has_value(),
            "the interpreter twin must retain every chain signal");
        signals[index] = *signal;
    }
    const auto queue_inputs = [&](const std::size_t phase) {
        const auto [input_a, input_b] = wide_projected_inputs(phase);
        simulation.deposit_signal(signals[0U],
            PackedLogic4::from_msb_string(input_a));
        simulation.deposit_signal(signals[1U],
            PackedLogic4::from_msb_string(input_b));
    };

    std::vector<WideProjectedFailureFrame> frames;
    frames.reserve(phases.size() + 1U);
    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the interpreter twin must reach the same initial quiet point");
    frames.push_back(capture_wide_projected_frame(simulation, signals));
    for (const auto phase : phases) {
        queue_inputs(phase);
        require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
            "the interpreter twin must settle every recorded stimulus");
        frames.push_back(capture_wide_projected_frame(simulation, signals));
    }
    return frames;
}

[[nodiscard]] std::vector<VhdlMetadataFrame>
run_vhdl_metadata_reference(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t input_count)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_vhdl_route_config(optimization, root);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the passive VHDL metadata reference must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::interpreter,
        SystemVerilogVpiRuntimeUpdates::omitted);
    const auto input_a = simulation.find_signal(
        "native_vhdl_projected_route.a");
    const auto input_b = simulation.find_signal(
        "native_vhdl_projected_route.b");
    const auto left = simulation.find_signal(
        "native_vhdl_projected_route.left_branch");
    const auto right = simulation.find_signal(
        "native_vhdl_projected_route.right_branch");
    const auto middle = simulation.find_signal(
        "native_vhdl_projected_route.middle");
    const auto output_result = simulation.find_signal(
        "native_vhdl_projected_route.result");
    const auto output = simulation.find_signal(
        "native_vhdl_projected_route.y");
    require(input_a && input_b && left && right && middle
            && output_result && output,
        "the passive VHDL reference must retain all signal handles");
    const VhdlSignalHandles handles {{
        { *input_a, "a" }, { *input_b, "b" },
        { *left, "left" }, { *right, "right" },
        { *middle, "middle" }, { *output_result, "result" },
        { *output, "y" },
    }};

    std::vector<VhdlMetadataFrame> metadata;
    metadata.reserve(input_count + 1U);
    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the passive VHDL reference must reach the native route quiet point");
    metadata.push_back(capture_vhdl_metadata(simulation, handles));

    for (std::size_t index = 0U; index < input_count; ++index) {
        const auto [a, b]
            = vhdl_input_pairs[index % vhdl_input_pairs.size()];
        simulation.deposit_signal(*input_a,
            PackedLogic4::from_msb_string(bits(a)));
        simulation.deposit_signal(*input_b,
            PackedLogic4::from_msb_string(bits(b)));
        require(simulation.run(simulation.now() + 1U).status
                    == RunStatus::time_limit,
            "the passive VHDL reference must settle each input pair");
        metadata.push_back(capture_vhdl_metadata(simulation, handles));
    }
    return metadata;
}

[[nodiscard]] VhdlDisjointProjectedResult
run_vhdl_disjoint_projected_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const bool require_wide_compiled_owners)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL",
        require_wide_compiled_owners ? nullptr : "0" };
    ScopedEnvironment single_owner {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        require_wide_compiled_owners ? nullptr : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_vhdl_disjoint_projected_config(optimization, root);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the VHDL disjoint-projected fixture must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, SystemVerilogVpiRuntimeUpdates::omitted);
    if (engine == SimulationEngine::compiled) {
        simulation.await_all_native_compilation();
    }

    const auto low_input = simulation.find_signal(
        "native_vhdl_disjoint_projected_route.low_input");
    const auto high_input = simulation.find_signal(
        "native_vhdl_disjoint_projected_route.high_input");
    const auto target = simulation.find_signal(
        "native_vhdl_disjoint_projected_route.target");
    const auto output = simulation.find_signal(
        "native_vhdl_disjoint_projected_route.y");
    require(low_input && high_input && target && output,
        "the VHDL disjoint-projected fixture must retain all signal handles");

    VhdlDisjointProjectedResult result;
    if (engine == SimulationEngine::compiled) {
        result.compiled_process_count = simulation.compiled_process_count();
        require(result.compiled_process_count >= 2U,
            "both VHDL projected owners must have compiled process bodies");
    }

    const auto capture_frame = [&] {
        if (require_wide_compiled_owners) {
            result.compiled_writers
                = NativeRegionAllocationTestAccess::
                    has_compiled_wide_vhdl_projected_owners(
                        simulation, *target, 2U);
            require(result.compiled_writers,
                "both disjoint VHDL slice owners must retain their exact compiled bindings");
        }
        return VhdlDisjointProjectedFrame {{
            NativeRegionAllocationTestAccess::snapshot(
                simulation, *low_input),
            NativeRegionAllocationTestAccess::snapshot(
                simulation, *high_input),
            NativeRegionAllocationTestAccess::snapshot(
                simulation, *target),
            NativeRegionAllocationTestAccess::snapshot(
                simulation, *output),
        }};
    };

    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the VHDL finish process must retain future work");
    result.frames.reserve(6U);
    result.frames.push_back(capture_frame());

    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 5U>
        input_phases {{
            { 0U, 3U }, { 1U, 4U }, { 2U, 1U }, { 2U, 1U }, { 3U, 0U },
        }};
    for (const auto [low_phase, high_phase] : input_phases) {
        const auto low_text = vhdl_logic9_pattern(64U, low_phase);
        const auto high_text = vhdl_logic9_pattern(65U, high_phase);
        constexpr std::string_view logic9_symbols = "UX01ZWLH-";
        for (const auto symbol : logic9_symbols) {
            require(low_text.find(symbol) != std::string::npos
                    && high_text.find(symbol) != std::string::npos,
                "each VHDL owner input must exercise all nine std_logic states");
        }
        simulation.deposit_signal(*low_input,
            PackedLogic4::from_logic9_msb_string(low_text));
        simulation.deposit_signal(*high_input,
            PackedLogic4::from_logic9_msb_string(high_text));
        require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
            "each VHDL input pair must settle before metadata capture");

        auto frame = capture_frame();
        const auto expected_target = high_text + low_text;
        require(frame.signals[2U].current.to_msb_string() == expected_target
                && frame.signals[3U].current.to_msb_string()
                    == expected_target,
            "the two projected VHDL owners must publish the expected 129-bit value");
        const auto& raw_owners = frame.signals[2U].raw_drivers;
        require(raw_owners.size() == 2U,
            "the projected target must retain both original raw owners");
        bool low_owner_matches { };
        bool high_owner_matches { };
        for (const auto& raw_owner : raw_owners) {
            const auto raw = raw_owner.value.to_msb_string();
            require(raw.size() == 129U,
                "each owner record must retain the full target width");
            low_owner_matches = low_owner_matches
                || raw.substr(65U) == low_text;
            high_owner_matches = high_owner_matches
                || raw.substr(0U, 65U) == high_text;
        }
        require(low_owner_matches && high_owner_matches,
            "each original owner record must preserve its exact projected slice");
        result.frames.push_back(std::move(frame));
    }
    if (require_wide_compiled_owners) {
        const auto& final_target = result.frames.back().signals[2U];
        const auto public_value
            = simulation.read_signal_snapshot(*target);
        const auto materialized
            = NativeRegionAllocationTestAccess::snapshot(simulation, *target);
        // This direct-signal materialization diagnostic is separate from the
        // A4 authoritative owner/value state and is intentionally not compared.
        require(public_value == final_target.current
                && materialized.current == final_target.current
                && materialized.last == final_target.last
                && materialized.stored == final_target.stored
                && materialized.owned_raw == final_target.owned_raw
                && materialized.external_raw == final_target.external_raw
                && materialized.force_value == final_target.force_value
                && materialized.force_mask == final_target.force_mask
                && materialized.raw_drivers == final_target.raw_drivers
                && materialized.event == final_target.event
                && materialized.transaction == final_target.transaction
                && materialized.event_domain == final_target.event_domain
                && materialized.event_phase == final_target.event_phase
                && materialized.systemverilog_round
                    == final_target.systemverilog_round
                && materialized.now == final_target.now
                && materialized.delta == final_target.delta,
            "late public observation must materialize identical wide owner and event state");
    }
    return result;
}

void require_repeated_equal_vhdl_deposits(
    const std::vector<VhdlMetadataFrame>& metadata)
{
    require(metadata.size() >= 2U,
        "the VHDL metadata sequence must include settled samples");
    auto inspected_repeats = std::size_t { 0U };
    for (std::size_t input_index = 1U;
        input_index < metadata.size() - 1U; ++input_index) {
        const auto current = input_index % vhdl_input_pairs.size();
        const auto previous = (input_index - 1U)
            % vhdl_input_pairs.size();
        if (vhdl_input_pairs[current] != vhdl_input_pairs[previous]) {
            continue;
        }
        ++inspected_repeats;

        const auto& before = metadata[input_index].signals;
        const auto& after = metadata[input_index + 1U].signals;
        require(before.size() == 7U && after.size() == before.size(),
            "the repeated-input sample must retain all signal metadata");
        for (std::size_t input_signal = 0U;
            input_signal < 2U; ++input_signal) {
            const auto& old_state = before[input_signal].snapshot;
            const auto& new_state = after[input_signal].snapshot;
            require(old_state.current == new_state.current
                    && old_state.last == new_state.last
                    && old_state.stored == new_state.stored
                    && old_state.raw_drivers == new_state.raw_drivers
                    && old_state.owned_raw == new_state.owned_raw
                    && old_state.external_raw == new_state.external_raw
                    && old_state.force_value == new_state.force_value
                    && old_state.force_mask == new_state.force_mask
                    && old_state.event == new_state.event
                    && old_state.event_domain == new_state.event_domain
                    && old_state.event_phase == new_state.event_phase
                    && old_state.systemverilog_round
                        == new_state.systemverilog_round,
                "an equal VHDL deposit must preserve current/last/raw and "
                "event metadata");
            require(old_state.transaction && new_state.transaction
                    && old_state.transaction != new_state.transaction,
                "an equal VHDL deposit must still advance its transaction "
                "metadata");
        }
    }
    require(inspected_repeats >= 1U,
        "the VHDL sequence must exercise at least one repeated equal pair");
}

[[nodiscard]] VhdlObservedRouteResult run_vhdl_observed_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const bool enable_region_kernel)
{
    ScopedEnvironment region_kernel { "FSIM_ENABLE_SV_REGION_KERNEL",
        enable_region_kernel ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_vhdl_route_config(optimization, root);
    FixedCerrBuffer profile;
    VhdlObservedRouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the observed VHDL route fixture must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (engine == SimulationEngine::compiled) {
            simulation.await_all_native_compilation();
        }
        const auto input_a = simulation.find_signal(
            "native_vhdl_projected_route.a");
        const auto input_b = simulation.find_signal(
            "native_vhdl_projected_route.b");
        const auto left = simulation.find_signal(
            "native_vhdl_projected_route.left_branch");
        const auto right = simulation.find_signal(
            "native_vhdl_projected_route.right_branch");
        const auto middle = simulation.find_signal(
            "native_vhdl_projected_route.middle");
        const auto output_result = simulation.find_signal(
            "native_vhdl_projected_route.result");
        const auto output = simulation.find_signal(
            "native_vhdl_projected_route.y");
        require(input_a && input_b && left && right && middle
                && output_result && output,
            "the observed VHDL route must retain every trace signal handle");

        const std::array handles {
            std::pair { *input_a, std::string_view { "a" } },
            std::pair { *input_b, std::string_view { "b" } },
            std::pair { *left, std::string_view { "left" } },
            std::pair { *right, std::string_view { "right" } },
            std::pair { *middle, std::string_view { "middle" } },
            std::pair { *output_result, std::string_view { "result" } },
            std::pair { *output, std::string_view { "y" } },
        };
        simulation.set_signal_change_hook(
            [&](const fsim::runtime::simir::SignalId signal,
                const PackedLogic4& value,
                const fsim::runtime::SimulationTick time,
                const std::uint64_t delta) {
                for (const auto& [id, name] : handles) {
                    if (id == signal) {
                        result.events.push_back({ std::string { name },
                            value.to_msb_string(), time, delta });
                        break;
                    }
                }
            });

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the observed VHDL route must reach the same quiet point");
        for (const auto [a, b] : vhdl_input_pairs) {
            simulation.deposit_signal(*input_a,
                PackedLogic4::from_msb_string(bits(a)));
            simulation.deposit_signal(*input_b,
                PackedLogic4::from_msb_string(bits(b)));
            require(simulation.run(simulation.now() + 1U).status
                        == RunStatus::time_limit,
                "the observed VHDL route must preserve its delayed finish");
        }
        if (engine == SimulationEngine::compiled) {
            simulation.await_all_native_compilation();
        }
    }
    require(!profile.overflowed(),
        "the observed VHDL profile buffer must remain complete");
    result.profile = profile.view();
    return result;
}

[[nodiscard]] RouteResult run_native_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t warm_runs,
    const std::size_t measured_runs,
    const std::optional<bool> local_wave = false,
    const bool activation_only_backend = false,
    const bool forwarding_test_backend = false)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", nullptr };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE",
        local_wave ? (*local_wave ? "1" : "0") : nullptr };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_route_config(optimization, root, local_wave.value_or(true));

    FixedCerrBuffer profile;
    RouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the fixed native-region HDL fixture must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            SimulationEngine::compiled,
            SystemVerilogVpiRuntimeUpdates::omitted);
        if (activation_only_backend) {
            NativeRegionAllocationTestAccess::
                install_activation_only_backend_provider(simulation);
        }
        if (forwarding_test_backend) {
            if (activation_only_backend) {
                throw std::invalid_argument {
                    "one native route cannot select two provider capability views"
                };
            }
            NativeRegionAllocationTestAccess::
                install_forwarding_test_backend_provider(simulation);
        }
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the full-route witness must use native process execution");

        const auto source_id = simulation.find_signal(local_wave.value_or(true)
                ? "native_region_route.stimulus"
                : "native_region_route.source");
        const auto middle_id = simulation.find_signal(
            "native_region_route.middle");
        const auto result_id = simulation.find_signal(
            "native_region_route.result");
        const auto mirror_id = simulation.find_signal(
            "native_region_route.mirror");
        const auto event_id = simulation.find_signal(
            "native_region_route.output_events");
        require(source_id && middle_id && result_id && mirror_id && event_id,
            "the route witness must retain all HDL signal handles");

        simulation.start();
        const auto startup = simulation.run(1U);
        require(startup.status == RunStatus::time_limit,
            "the delayed finish must leave nonzero-time work pending");
        simulation.await_all_native_compilation();

        constexpr std::array<std::uint8_t, 4U> inputs {
            0xffU, 0x00U, 0xf0U, 0x0fU
        };
        std::array<PackedLogic4, inputs.size()> packed_inputs {
            PackedLogic4::from_msb_string("11111111"),
            PackedLogic4::from_msb_string("00000000"),
            PackedLogic4::from_msb_string("11110000"),
            PackedLogic4::from_msb_string("00001111")
        };
        const auto check_values_for = [&](const std::uint8_t value) {
            require(simulation.read_signal_snapshot(*middle_id).to_msb_string()
                    == bits(value)
                    && simulation.read_signal_snapshot(*result_id)
                            .to_msb_string() == bits(value)
                    && simulation.read_signal_snapshot(*mirror_id)
                            .to_msb_string() == bits(value),
                "the native region must publish every dependent result");
        };
        const auto run_input = [&](const std::size_t index) {
            simulation.deposit_signal(*source_id, packed_inputs[index]);
            const auto run_result = simulation.run(simulation.now() + 1U);
            require(run_result.status == RunStatus::time_limit,
                "the native-region run must retain the future finish");
        };

        // Keep an identical warm-only control. Comparing its completed-native
        // count with the measured run proves the measured window itself used
        // the native route, rather than relying on warm-up activity.
        for (std::size_t index = 0U; index < warm_runs; ++index) {
            run_input(index % inputs.size());
        }
        simulation.await_all_native_compilation();

        for (std::size_t index = 0U; index < measured_runs; ++index) {
            const auto input_index = (warm_runs + index) % inputs.size();
            if (index < result.measured_allocations.size()) {
                begin_allocation_count();
                try {
                    run_input(input_index);
                } catch (...) {
                    static_cast<void>(end_allocation_count());
                    throw;
                }
                result.measured_allocations[index] = end_allocation_count();
            } else {
                run_input(input_index);
            }
        }

        // The final observation happens only after the measured window. No
        // host read can demote/materialize the region between its activations.
        if (warm_runs + measured_runs != 0U) {
            check_values_for(inputs[(warm_runs + measured_runs - 1U)
                % inputs.size()]);
            require(simulation.read_signal_snapshot(*event_id)
                        .to_msb_string().find('1') != std::string::npos,
                "the HDL output observer must witness boundary events");
        }
        // The profile is emitted by Interpreter destruction at scope exit.
    }
    require(!profile.overflowed(),
        "the fixed SystemVerilog profile buffer must remain complete");
    result.profile = profile.view();
    return result;
}

void run_observed_reference(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    ScopedEnvironment region_kernel { "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_route_config(optimization, root);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the observed reference fixture must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::interpreter,
        SystemVerilogVpiRuntimeUpdates::omitted);
    const auto source_id = simulation.find_signal(
        "native_region_route.source");
    const auto middle_id = simulation.find_signal(
        "native_region_route.middle");
    const auto result_id = simulation.find_signal(
        "native_region_route.result");
    const auto mirror_id = simulation.find_signal(
        "native_region_route.mirror");
    require(source_id && middle_id && result_id && mirror_id,
        "the reference must retain all HDL signal handles");
    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the observed reference must reach the same nonzero quiet point");

    constexpr std::array<std::uint8_t, 4U> inputs {
        0xffU, 0x00U, 0xf0U, 0x0fU
    };
    for (const auto value : inputs) {
        simulation.deposit_signal(*source_id,
            PackedLogic4::from_msb_string(bits(value)));
        require(simulation.run(simulation.now() + 1U).status
                    == RunStatus::time_limit,
            "the observed reference must preserve the future finish");
        require(simulation.read_signal_snapshot(*middle_id).to_msb_string()
                    == bits(value)
                && simulation.read_signal_snapshot(*result_id).to_msb_string()
                    == bits(value)
                && simulation.read_signal_snapshot(*mirror_id).to_msb_string()
                    == bits(value),
            "the checked reference must publish every dependent result");
    }
}

[[nodiscard]] PreparedOutputRouteResult run_prepared_output_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const bool enable_local_wave = true)
{
    const bool compiled = engine == SimulationEngine::compiled;
    const bool use_local_wave = compiled && enable_local_wave;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", use_local_wave ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_route_config(optimization, root, true);
    FixedCerrBuffer profile;
    PreparedOutputRouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the prepared-output fixture must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            NativeRegionAllocationTestAccess::
                install_activation_only_backend_provider(simulation);
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the prepared-output witness must install native processes");
        }

        const std::array<std::string_view, 8U> names {
            "native_region_route.stimulus",
            "native_region_route.source",
            "native_region_route.left_branch",
            "native_region_route.right_branch",
            "native_region_route.middle",
            "native_region_route.result",
            "native_region_route.mirror",
            "native_region_route.output_events",
        };
        std::array<SignalId, 8U> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto signal = simulation.find_signal(names[index]);
            require(signal.has_value(),
                "the prepared-output fixture must retain every signal");
            signals[index] = *signal;
        }

        constexpr std::array<std::string_view, 5U> inputs {
            "10100101", "01011010", "11110000", "00001111", "10010110"
        };
        result.activation_snapshots.resize(inputs.size() - 1U);
        const auto capture_activation = [&](const std::size_t activation) {
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                result.activation_snapshots[activation][index]
                    = NativeRegionAllocationTestAccess::summarize(
                        simulation, signals[index]);
            }
        };

        // Seed the adapter input before the first scheduler wave so the
        // prepared-output path sees the component's first internal writes.
        simulation.deposit_signal(signals[0U],
            PackedLogic4::from_msb_string(inputs[0U]));
        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the prepared-output route must retain delayed finish work");
        // Exercise the complete scheduler and ticket topology once before
        // measuring the four repeated prepared-output activations.
        const auto warm_before = use_local_wave
            ? NativeRegionAllocationTestAccess::prepared_output_counters(
                  simulation)
            : NativeRegionAllocationTestAccess::PreparedOutputCounters { };
        simulation.deposit_signal(signals[0U],
            PackedLogic4::from_msb_string("00110011"));
        require(simulation.run(simulation.now() + 1U).status
                    == RunStatus::time_limit,
            "the prepared-output warmup retains delayed finish work");
        if (use_local_wave) {
            const auto warm_after
                = NativeRegionAllocationTestAccess::prepared_output_counters(
                    simulation);
            require(warm_after.batches > warm_before.batches
                    && warm_after.seals > warm_before.seals
                    && warm_after.fallbacks == warm_before.fallbacks
                    && warm_after.direct_ready_attempts
                        > warm_before.direct_ready_attempts
                    && warm_after.direct_ready_completions
                        > warm_before.direct_ready_completions,
                "the warmup must execute and seal a direct-ready entry");
        }
        for (std::size_t index = 1U; index < inputs.size(); ++index) {
            const auto before = compiled
                ? NativeRegionAllocationTestAccess::prepared_output_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::PreparedOutputCounters { };
            const auto run_input = [&] {
                simulation.deposit_signal(signals[0U],
                    PackedLogic4::from_msb_string(inputs[index]));
                require(simulation.run(simulation.now() + 1U).status
                            == RunStatus::time_limit,
                    "the prepared-output route must retain delayed finish work");
            };
            begin_allocation_count();
            try {
                run_input();
            } catch (...) {
                static_cast<void>(end_allocation_count());
                throw;
            }
            result.measured_allocations[index - 1U]
                = end_allocation_count();
            if (use_local_wave) {
                const auto after
                    = NativeRegionAllocationTestAccess::prepared_output_counters(
                        simulation);
                require(after.batches > before.batches
                        && after.seals > before.seals
                        && after.fallbacks == before.fallbacks,
                    "each warmed activation must execute and seal a prepared "
                    "entry without fallback");
                require(after.direct_ready_attempts
                            > before.direct_ready_attempts
                        && after.direct_ready_completions
                            > before.direct_ready_completions,
                    "each measured narrow Logic4 wave must enter and complete "
                    "the direct-ready window");
            }
            capture_activation(index - 1U);
        }

        for (std::size_t signal = 1U; signal <= 6U; ++signal) {
            require(result.activation_snapshots.back()[signal].current
                        == inputs.back(),
                "prepared output must match the final complete HDL value");
        }

        // output_events is an SV integer, so native publication uses the
        // checked materialization path. Compare the immediate state with the
        // final activation before exercising the public observation path.
        const auto before_observation
            = NativeRegionAllocationTestAccess::summarize(simulation, signals[7U]);
        require(!result.activation_snapshots.empty()
                && same_signal_semantics(before_observation,
                    result.activation_snapshots.back()[7U]),
            "the counter retains exact current/LAST/stored/raw-driver/event/"
            "transaction metadata from the final activation");
        const auto observed_counter
            = simulation.read_signal(signals[7U]).to_msb_string();
        const auto after_observation
            = NativeRegionAllocationTestAccess::summarize(simulation, signals[7U]);
        require(!after_observation.materialization_pending
                && observed_counter == before_observation.current
                && same_signal_semantics(before_observation, after_observation),
            "public observation preserves exact roles and materializes any "
            "pending state without changing drivers or event/transaction metadata");
    }
    require(!profile.overflowed(),
        "the prepared-output profile buffer must remain complete");
    result.profile = profile.view();
    return result;
}

void test_o0_and_o2_prepared_output_application_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-prepared-output-route-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto compiled_root = root.path / optimization_name / "compiled";
        const auto fallback_root = root.path / optimization_name / "fallback";
        const auto reference_root = root.path / optimization_name / "reference";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(fallback_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled = run_prepared_output_route(
            optimization, compiled_root, SimulationEngine::compiled);
        const auto fallback = run_prepared_output_route(
            optimization, fallback_root, SimulationEngine::compiled, false);
        const auto reference = run_prepared_output_route(
            optimization, reference_root, SimulationEngine::interpreter);
        require(same_signal_snapshots(compiled.activation_snapshots,
                    reference.activation_snapshots),
            "prepared publication must preserve current/LAST, raw drivers, "
            "transaction/event stamps, origin, and scheduler round at every "
            "warmed activation");
        require(same_signal_snapshots(fallback.activation_snapshots,
                    reference.activation_snapshots)
                && same_signal_snapshots(compiled.activation_snapshots,
                    fallback.activation_snapshots),
            "ordinary compiled fallback must preserve the same input-image "
            "values and publication metadata as direct-window execution");
        for (const auto allocations : compiled.measured_allocations) {
            require(allocations == 0U,
                "each warmed prepared-output application wave must allocate nothing");
        }
        require(profile_count(compiled.profile, "prepared_output_batches=")
                    > 1U
                && profile_count(compiled.profile, "prepared_output_seals=")
                    > 1U
                && profile_count(compiled.profile,
                       "prepared_output_fallbacks=") == 0U,
            "O0/O2 must repeatedly execute and seal prepared native entries");
    }
}

void test_failed_ticket_reservation_retries_without_sequence_gap()
{
    using fsim::runtime::SchedulerPhase;
    using fsim::runtime::detail::make_scheduler_task_descriptor;

    fsim::runtime::Scheduler scheduler;
    ReservationRetryProbe probe;
    const auto task
        = make_scheduler_task_descriptor<ReservationRetryPayload,
            dispatch_reservation_retry>({ &probe });
    ReservationRetryBatch batch { probe, task };
    scheduler.schedule_systemverilog_batchable(
        SchedulerPhase::active, 11U, batch, 0U,
        [](fsim::runtime::Scheduler&) {
            throw std::runtime_error {
                "an accepted reservation retry must not take fallback" };
        });
    const auto result = scheduler.run();
    clear_allocation_failure();
    require(result.status == RunStatus::completed && probe.committed
            && probe.dispatched == 1U
            && probe.sequence_after_failure == 1U
            && probe.sequence_after_commit == 3U,
        "failed reservation and retry preserve task identity and dispatch "
        "once");
}

void test_o0_and_o2_native_full_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-route-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto warm_root = optimization_root / "warm-only";
        const auto measured_root = optimization_root / "measured";
        const auto observed_root = optimization_root / "observed";
        std::filesystem::create_directories(warm_root);
        std::filesystem::create_directories(measured_root);
        std::filesystem::create_directories(observed_root);
        const auto warm = run_native_route(optimization, warm_root, 4U, 0U);
        const auto measured
            = run_native_route(optimization, measured_root, 4U, 8U);
        run_observed_reference(optimization, observed_root);
        const auto warm_completions = profile_count(
            warm.profile, "region_backend_completions=");
        const auto measured_completions = profile_count(
            measured.profile, "region_backend_completions=");
        const auto warm_completion_fast_members = profile_count(
            warm.profile, "a2_completion_fast_members=");
        const auto measured_completion_fast_members = profile_count(
            measured.profile, "a2_completion_fast_members=");
        const auto warm_input_completions = profile_count(
            warm.profile, "a4_native_input_completions=");
        const auto warm_input_handoffs = profile_count(
            warm.profile, "a4_native_input_handoffs=");
        const auto measured_input_handoffs = profile_count(
            measured.profile, "a4_native_input_handoffs=");
        const auto measured_input_completions = profile_count(
            measured.profile, "a4_native_input_completions=");
        constexpr auto measured_activation_count = 8U;
        const auto measured_run_delta
            = profile_count(measured.profile, "region_backend_runs=")
            - profile_count(warm.profile, "region_backend_runs=");
        const auto warm_readiness_images = profile_count(warm.profile,
            "component_readiness_mask_images=");
        const auto measured_readiness_images = profile_count(measured.profile,
            "component_readiness_mask_images=");
        require(measured_run_delta >= measured_activation_count
                && measured_completions - warm_completions
                    == measured_run_delta,
            "the measured window must complete native region executions");
        require(measured_readiness_images > warm_readiness_images,
            "actual native activation consumes a certified readiness-mask image");
        require(measured_completion_fast_members
                    > warm_completion_fast_members,
            "the O0/O2 application route uses stateless completion after warmup");
        require(measured_input_handoffs - warm_input_handoffs
                    == measured_run_delta
                && measured_input_completions - warm_input_completions
                    == measured_run_delta,
            "every measured native region execution must consume borrowed "
            "Logic4 planes");
        for (const auto allocations : measured.measured_allocations) {
            require(allocations == 0U,
                "the warmed public native route must allocate nothing");
        }
    }
    test_failed_ticket_reservation_retries_without_sequence_gap();
}


void test_o0_and_o2_local_wave_forwarding_allocation()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-local-wave-allocation-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    constexpr std::size_t measured_windows = 8U;
    constexpr std::size_t expected_forwarding_members_per_window = 5U;
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto warm_root = optimization_root / "warm-only";
        const auto measured_root = optimization_root / "measured";
        std::filesystem::create_directories(warm_root);
        std::filesystem::create_directories(measured_root);

        const auto warm = run_native_route(
            optimization, warm_root, 4U, 0U, std::nullopt, false, true);
        const auto measured = run_native_route(
            optimization, measured_root, 4U, measured_windows,
            std::nullopt, false, true);
        const auto warm_forwarding_evaluations = profile_count(
            warm.profile, "region_forwarding_evaluations=");
        const auto measured_forwarding_evaluations = profile_count(
            measured.profile, "region_forwarding_evaluations=");
        const auto warm_forwarding_members = profile_count(
            warm.profile, "region_forwarding_member_consumptions=");
        const auto measured_forwarding_members = profile_count(
            measured.profile, "region_forwarding_member_consumptions=");
        require(measured_forwarding_evaluations
                    >= warm_forwarding_evaluations
                && measured_forwarding_members >= warm_forwarding_members
                && measured_forwarding_evaluations
                    - warm_forwarding_evaluations == measured_windows
                && measured_forwarding_members - warm_forwarding_members
                    == measured_windows
                        * expected_forwarding_members_per_window,
            "each of the eight default A2 local-wave windows completes and "
            "consumes all five forwarding members");
        require(profile_count(measured.profile, "a2_local_update_dispatches=")
                    > profile_count(warm.profile,
                        "a2_local_update_dispatches=")
                && profile_count(measured.profile,
                       "a2_local_update_fallbacks=") == 0U
                && profile_count(measured.profile,
                       "a2_ordinary_internal_updates=")
                    == profile_count(warm.profile,
                        "a2_ordinary_internal_updates="),
            "measured internal publications use the private local-wave route "
            "without checked ordinary fallback");
        for (const auto allocations : measured.measured_allocations) {
            require(allocations == 0U,
                "the warmed default forwarding/local-wave route allocates "
                "nothing in every measured window");
        }
    }
}

void test_o0_and_o2_activation_only_local_wave_allocation()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-activation-only-local-wave-allocation-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto warm_root = optimization_root / "warm-only";
        const auto measured_root = optimization_root / "measured";
        std::filesystem::create_directories(warm_root);
        std::filesystem::create_directories(measured_root);

        const auto warm = run_native_route(
            optimization, warm_root, 4U, 0U, std::nullopt, true);
        const auto measured = run_native_route(
            optimization, measured_root, 4U, 8U, std::nullopt, true);
        require(profile_count(measured.profile, "region_backend_completions=")
                    > profile_count(warm.profile,
                        "region_backend_completions="),
            "the activation-only measured window completes its native body");
        require(profile_count(measured.profile, "a2_local_update_dispatches=")
                    > profile_count(warm.profile,
                        "a2_local_update_dispatches=")
                && profile_count(measured.profile,
                       "a2_local_update_fallbacks=") == 0U
                && profile_count(measured.profile,
                       "a2_ordinary_internal_updates=")
                    == profile_count(warm.profile,
                        "a2_ordinary_internal_updates="),
            "activation-only publications use the private local-wave route");
        const auto warm_seed_reads = profile_count(
            warm.profile, "a2_internal_seed_reads=");
        require(warm_seed_reads != 0U
                && profile_count(measured.profile, "a2_internal_seed_reads=")
                    == warm_seed_reads
                && profile_count(measured.profile, "a2_internal_state_seeds=")
                    == profile_count(warm.profile, "a2_internal_state_seeds=")
                && profile_count(measured.profile,
                       "a2_internal_state_reuses=")
                    > profile_count(warm.profile,
                        "a2_internal_state_reuses="),
            "the activation-only route reuses privately seeded registers "
            "without rereading logical planes");
        for (const auto allocations : measured.measured_allocations) {
            require(allocations == 0U,
                "the warmed activation-only route allocates nothing in every "
                "measured window");
        }
    }
}

void test_o0_and_o2_native_vhdl_projected_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-vhdl-region-route-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto warm_root = optimization_root / "warm-only";
        const auto measured_root = optimization_root / "measured";
        const auto metadata_root = optimization_root / "metadata-reference";
        const auto reference_root = optimization_root / "reference";
        const auto observed_root = optimization_root / "observed";
        std::filesystem::create_directories(warm_root);
        std::filesystem::create_directories(measured_root);
        std::filesystem::create_directories(metadata_root);
        std::filesystem::create_directories(reference_root);
        std::filesystem::create_directories(observed_root);

        constexpr std::size_t warm_runs = 4U;
        constexpr std::size_t measured_runs = 8U;
        const auto warm = run_vhdl_native_route(
            optimization, warm_root, warm_runs, 0U);
        const auto measured = run_vhdl_native_route(
            optimization, measured_root, warm_runs, measured_runs);
        const auto reference_metadata = run_vhdl_metadata_reference(
            optimization, metadata_root, warm_runs + measured_runs);
        const auto expected = run_vhdl_observed_route(optimization,
            reference_root, SimulationEngine::interpreter, false);
        const auto observed = run_vhdl_observed_route(optimization,
            observed_root, SimulationEngine::compiled, true);

        const auto warm_completions = projected_profile_count(warm.profile,
            "completions=");
        const auto measured_completions = projected_profile_count(measured.profile,
            "completions=");
        const auto warm_backend_runs = projected_profile_count(warm.profile,
            "backend_runs=");
        const auto measured_backend_runs = projected_profile_count(measured.profile,
            "backend_runs=");
        const auto changed_measured_pairs
            = count_changed_vhdl_input_pairs(warm_runs, measured_runs);
        // Every changed pair in this measured stimulus changes b, so the
        // right-branch projected region must complete for each changed pair.
        // Equal deposits still exercise transaction metadata but need not
        // wake readers.
        require(changed_measured_pairs != 0U,
            "the measured VHDL window must contain a changed input pair");
        require(measured_completions
                    >= warm_completions + changed_measured_pairs
                && measured_backend_runs
                    >= warm_backend_runs + changed_measured_pairs,
            "each changed measured VHDL input pair must complete a native "
            "generic region");
        require(measured.metadata == reference_metadata,
            "passive current/last/raw/stored values, event and transaction "
            "stamps must match the interpreter after every settled VHDL run");
        require_repeated_equal_vhdl_deposits(reference_metadata);
        require_repeated_equal_vhdl_deposits(measured.metadata);
        require(!expected.events.empty()
                && observed.events == expected.events,
            "observed compiled VHDL fallback must match every reference event, including its generic delta");
        require(projected_profile_count(observed.profile,
                    "completions=") == 0U,
            "the observed VHDL run must retain the checked per-delta route");
    }
}

void test_o0_and_o2_vhdl_cycle_boundary_contract()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-vhdl-cycle-boundary-contract-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    using EventStamp = std::pair<fsim::runtime::SimulationTick,
        std::uint64_t>;
    const auto first_frame = [](const VhdlCycleBoundaryRun& run,
                                 const auto& predicate,
                                 const char* const message)
        -> const VhdlCycleBoundaryFrame& {
        const auto found = std::ranges::find_if(run.frames, predicate);
        require(found != run.frames.end(), message);
        return *found;
    };
    const auto value = [](const VhdlCycleBoundaryFrame& frame,
                           const VhdlCycleBoundarySignal signal) {
        return frame.signals[signal].current.to_msb_string();
    };
    const auto committed_stamp = [](const auto& snapshot,
                                     const char* const label) {
        require(snapshot.event.has_value()
                && snapshot.transaction.has_value(),
            label);
        require(snapshot.event == snapshot.transaction, label);
        return *snapshot.event;
    };
    const auto next_event_stamp = [](const EventStamp& previous) {
        return EventStamp { previous.first, previous.second + 1U };
    };
    const auto require_committed_value = [](
        const VhdlCycleBoundaryFrame& frame,
        const VhdlCycleBoundarySignal signal,
        const std::string_view expected,
        const std::string_view previous,
        const fsim::runtime::simir::ProcessSchedulingDomain domain,
        const fsim::runtime::SchedulerPhase origin_phase,
        const std::uint64_t systemverilog_round,
        const EventStamp expected_stamp,
        const bool expect_runtime_driver_record,
        const char* const label) {
        const auto& snapshot = frame.signals[signal];
        require(snapshot.now == expected_stamp.first
                && snapshot.current.to_msb_string() == expected
                && snapshot.last.to_msb_string() == previous
                && snapshot.stored.to_msb_string() == expected,
            label);
        // DriverValues stores resolution drivers, not every procedural or
        // unresolved signal assignment. Frame parity still compares the raw
        // driver lists for all signals above.
        if (expect_runtime_driver_record) {
            require(!snapshot.raw_drivers.empty()
                    && std::ranges::any_of(snapshot.raw_drivers,
                        [&](const auto& driver) {
                            return driver.value.to_msb_string() == expected;
                        }),
                label);
        }
        require(snapshot.event == expected_stamp
                && snapshot.transaction == expected_stamp,
            label);
        require(snapshot.event_domain == domain
                && snapshot.event_phase == origin_phase
                && snapshot.systemverilog_round == systemverilog_round,
            label);
    };

    const std::array<std::pair<bool, fsim::project::Optimization>, 4U>
        routes {{
            { false, fsim::project::Optimization::o0 },
            { false, fsim::project::Optimization::o2 },
            { true, fsim::project::Optimization::o0 },
            { true, fsim::project::Optimization::o2 },
        }};
    for (const auto& [unresolved_leaf, optimization] : routes) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto route_name = unresolved_leaf
            ? "unresolved-mixed-checked" : "resolved-checked";
        const bool vhdl_signals_have_driver_records = !unresolved_leaf;
        const auto optimization_root
            = root.path / route_name / optimization_name;
        const auto interpreter_root = optimization_root / "interpreter";
        const auto compiled_root = optimization_root / "compiled";
        std::filesystem::create_directories(interpreter_root);
        std::filesystem::create_directories(compiled_root);

        const auto reference = run_vhdl_cycle_boundary(
            optimization, SimulationEngine::interpreter, interpreter_root,
            unresolved_leaf);
        const auto compiled = run_vhdl_cycle_boundary(
            optimization, SimulationEngine::compiled, compiled_root,
            unresolved_leaf);
        require(reference.status == RunStatus::time_limit
                && compiled.status == reference.status
                && reference.time == 1U && compiled.time == reference.time,
            "the cycle witness must settle at time 1 before its delayed stop");
        require(compiled.certificate_proofs[0U].has_value()
                && compiled.certificate_proofs[1U].has_value()
                && compiled.output_proofs[0U].has_value()
                && compiled.output_proofs[1U].has_value(),
            "VHDL middles and mixed outputs must retain their graph certificates");
        const auto& active_middle_certificate
            = *compiled.certificate_proofs[0U];
        const auto& nba_middle_certificate
            = *compiled.certificate_proofs[1U];
        const auto is_vhdl_projected_writer = [](const auto& certificate) {
            return certificate.value_kind
                    == fsim::runtime::simir::ValueKind::logic9
                && certificate.drivers
                    == fsim::runtime::simir::RegionDriverClass::single_whole
                && certificate.scheduling_domain
                    == fsim::runtime::simir::ProcessSchedulingDomain::generic
                && certificate.update_kind
                    == fsim::runtime::simir::RegionUpdateKind::vhdl_projected;
        };
        require(is_vhdl_projected_writer(active_middle_certificate)
                && is_vhdl_projected_writer(nba_middle_certificate),
            "both VHDL middle signals must retain whole generic projected writers");
        if (unresolved_leaf) {
            require(active_middle_certificate.resolution
                        == fsim::runtime::simir::ResolutionKind::none
                    && nba_middle_certificate.resolution
                        == fsim::runtime::simir::ResolutionKind::none
                    && active_middle_certificate.status
                        == fsim::runtime::simir::RegionComponentCertificateStatus::structural_candidate
                    && nba_middle_certificate.status
                        == fsim::runtime::simir::RegionComponentCertificateStatus::structural_candidate
                    && active_middle_certificate.structural_internal
                    && nba_middle_certificate.structural_internal
                    && !active_middle_certificate.boundary
                    && !nba_middle_certificate.boundary,
                "each unresolved leaf must certify its middle as internal state");
            const auto is_logic4_sv_wire = [](const auto& proof) {
                return proof.resolution
                        == fsim::runtime::simir::ResolutionKind::sv_wire
                    && proof.value_kind
                        == fsim::runtime::simir::ValueKind::logic4;
            };
            require(is_logic4_sv_wire(*compiled.output_proofs[0U])
                    && is_logic4_sv_wire(*compiled.output_proofs[1U]),
                "the mixed unresolved leaf must retain its Logic4 SV-wire actuals");
            require(compiled.compiled_process_count != 0U
                    && compiled.projected.backend_runs == 0U
                    && compiled.projected.completions == 0U,
                "the mixed Logic9-to-Logic4 outputs must stay on checked execution");
        } else {
            require(active_middle_certificate.resolution
                        == fsim::runtime::simir::ResolutionKind::std_logic
                    && nba_middle_certificate.resolution
                        == fsim::runtime::simir::ResolutionKind::std_logic
                    && active_middle_certificate.boundary
                    && nba_middle_certificate.boundary
                    && !active_middle_certificate.structural_internal
                    && !nba_middle_certificate.structural_internal
                    && compiled.unsupported_resolution_boundaries >= 2U,
                "resolved middle signals must remain checked boundary state");
            require(compiled.projected.backend_runs == 0U
                    && compiled.projected.completions == 0U,
                "the resolved assertion leaf must retain checked per-delta execution");
        }
        require(same_vhdl_cycle_boundary_frames(
                    compiled.frames, reference.frames)
                && !compiled.frames.empty()
                && compiled.projected_by_frame.size() == compiled.frames.size(),
            "every VHDL/SV phase-cut snapshot must match the interpreter");

        const auto& active_cut = first_frame(compiled,
            [&](const VhdlCycleBoundaryFrame& frame) {
                return value(frame, vhdl_cycle_active_source) == "1"
                    && value(frame, vhdl_cycle_nba_source) == "0"
                    && value(frame, vhdl_cycle_active_middle) == "0"
                    && value(frame, vhdl_cycle_nba_middle) == "0";
            },
            "the SV Active write must be visible before the NBA write");
        const auto& nba_cut = first_frame(compiled,
            [&](const VhdlCycleBoundaryFrame& frame) {
                return value(frame, vhdl_cycle_active_source) == "1"
                    && value(frame, vhdl_cycle_nba_source) == "1"
                    && value(frame, vhdl_cycle_active_middle) == "0"
                    && value(frame, vhdl_cycle_nba_middle) == "0";
            },
            "the SV NBA write must settle before either VHDL projection");
        require(active_cut.completed_phase
                    == fsim::runtime::SchedulerPhase::active
                && nba_cut.completed_phase
                    == fsim::runtime::SchedulerPhase::update
                && active_cut.signals[vhdl_cycle_active_source].delta == 0U
                && nba_cut.signals[vhdl_cycle_nba_source].delta == 0U,
            "SV Active and NBA source commits occupy their original round");
        const EventStamp source_stamp { 1U, 1U };
        require(committed_stamp(
                    active_cut.signals[vhdl_cycle_active_source],
                    "the Active source must have an event and transaction")
                    == source_stamp
                && committed_stamp(
                    nba_cut.signals[vhdl_cycle_nba_source],
                    "the NBA source must have an event and transaction")
                    == source_stamp,
            "both SV source commits must retain their exact time-1 stamps");
        require_committed_value(active_cut, vhdl_cycle_active_source,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::systemverilog,
            fsim::runtime::SchedulerPhase::active, 1U, source_stamp, false,
            "the Active procedural source must retain its value and event stamp");
        require_committed_value(nba_cut, vhdl_cycle_nba_source,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::systemverilog,
            fsim::runtime::SchedulerPhase::update, 1U, source_stamp, false,
            "the NBA procedural source must retain its value and event stamp");

        const auto& first_vhdl_cycle = first_frame(compiled,
            [&](const VhdlCycleBoundaryFrame& frame) {
                return value(frame, vhdl_cycle_active_middle) == "1"
                    && value(frame, vhdl_cycle_nba_middle) == "1"
                    && value(frame, vhdl_cycle_active_leaf_output) == "0"
                    && value(frame, vhdl_cycle_nba_leaf_output) == "0";
            },
            "the first VHDL projection must publish both middle values only");
        const auto middle_stamp = next_event_stamp(source_stamp);
        require(first_vhdl_cycle.completed_phase
                    == fsim::runtime::SchedulerPhase::update
                && first_vhdl_cycle.signals[vhdl_cycle_active_middle].delta
                    == active_cut.signals[vhdl_cycle_active_source].delta + 1U
                && first_vhdl_cycle.signals[vhdl_cycle_nba_middle].delta
                    == first_vhdl_cycle.signals[vhdl_cycle_active_middle].delta
                && committed_stamp(
                    first_vhdl_cycle.signals[vhdl_cycle_active_middle],
                    "the Active-fed middle must have an event stamp")
                    == middle_stamp
                && committed_stamp(
                    first_vhdl_cycle.signals[vhdl_cycle_nba_middle],
                    "the NBA-fed middle must have an event stamp")
                    == middle_stamp,
            "both mixed-domain inputs must enter the same first VHDL cycle");
        require_committed_value(first_vhdl_cycle, vhdl_cycle_active_middle,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::generic,
            unresolved_leaf
                ? fsim::runtime::SchedulerPhase::active
                : fsim::runtime::SchedulerPhase::update,
            0U, middle_stamp,
            vhdl_signals_have_driver_records,
            "the Active-fed VHDL middle must retain projected metadata");
        require_committed_value(first_vhdl_cycle, vhdl_cycle_nba_middle,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::generic,
            unresolved_leaf
                ? fsim::runtime::SchedulerPhase::active
                : fsim::runtime::SchedulerPhase::update,
            0U, middle_stamp,
            vhdl_signals_have_driver_records,
            "the NBA-fed VHDL middle must retain projected metadata");

        const auto& vhdl_output_cut = first_frame(compiled,
            [&](const VhdlCycleBoundaryFrame& frame) {
                return value(frame, vhdl_cycle_active_leaf_output) == "1"
                    && value(frame, vhdl_cycle_nba_leaf_output) == "1"
                    && value(frame, vhdl_cycle_active_seen) == "0"
                    && value(frame, vhdl_cycle_nba_seen) == "0";
            },
            "VHDL outputs must publish before SV event readers run");
        const auto output_stamp = next_event_stamp(middle_stamp);
        require(vhdl_output_cut.completed_phase
                    == fsim::runtime::SchedulerPhase::update
                && vhdl_output_cut.signals[vhdl_cycle_active_leaf_output].delta
                    == first_vhdl_cycle.signals[vhdl_cycle_active_middle].delta + 1U
                && vhdl_output_cut.signals[vhdl_cycle_nba_leaf_output].delta
                    == vhdl_output_cut.signals[vhdl_cycle_active_leaf_output].delta
                && committed_stamp(
                    vhdl_output_cut.signals[vhdl_cycle_active_leaf_output],
                    "the Active-fed leaf output must have an event stamp")
                    == output_stamp
                && committed_stamp(
                    vhdl_output_cut.signals[vhdl_cycle_nba_leaf_output],
                    "the NBA-fed leaf output must have an event stamp")
                    == output_stamp,
            "the second VHDL projection must occupy the next delta cycle");
        require_committed_value(vhdl_output_cut,
            vhdl_cycle_active_leaf_output, "1", "0",
            fsim::runtime::simir::ProcessSchedulingDomain::generic,
            fsim::runtime::SchedulerPhase::active, 0U, output_stamp,
            vhdl_signals_have_driver_records,
            "the Active-fed VHDL output must retain owner and event metadata");
        require_committed_value(vhdl_output_cut,
            vhdl_cycle_nba_leaf_output, "1", "0",
            fsim::runtime::simir::ProcessSchedulingDomain::generic,
            fsim::runtime::SchedulerPhase::active, 0U, output_stamp,
            vhdl_signals_have_driver_records,
            "the NBA-fed VHDL output must retain owner and event metadata");
        require_committed_value(vhdl_output_cut, vhdl_cycle_active_output,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::generic,
            fsim::runtime::SchedulerPhase::active, 0U, output_stamp, true,
            "the Active-fed VHDL boundary must be visible before its SV reader");
        require_committed_value(vhdl_output_cut, vhdl_cycle_nba_output,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::generic,
            fsim::runtime::SchedulerPhase::active, 0U, output_stamp, true,
            "the NBA-fed VHDL boundary must be visible before its SV reader");

        const auto& sv_boundary_cut = first_frame(compiled,
            [&](const VhdlCycleBoundaryFrame& frame) {
                return value(frame, vhdl_cycle_active_output) == "1"
                    && value(frame, vhdl_cycle_nba_output) == "1"
                    && value(frame, vhdl_cycle_active_seen) == "1"
                    && value(frame, vhdl_cycle_nba_seen) == "1";
            },
            "both VHDL boundary events must reach their SV Active readers");
        const auto seen_stamp = next_event_stamp(output_stamp);
        const auto check_retained_output = [&](
            const VhdlCycleBoundarySignal signal) {
            const auto& retained = sv_boundary_cut.signals[signal];
            const auto& committed = vhdl_output_cut.signals[signal];
            require(retained.current == committed.current
                    && retained.last == committed.last
                    && retained.stored == committed.stored
                    && retained.raw_drivers == committed.raw_drivers
                    && retained.owned_raw == committed.owned_raw
                    && retained.external_raw == committed.external_raw
                    && retained.force_value == committed.force_value
                    && retained.force_mask == committed.force_mask
                    && retained.event == committed.event
                    && retained.transaction == committed.transaction
                    && retained.event_domain == committed.event_domain
                    && retained.event_phase == committed.event_phase
                    && retained.systemverilog_round
                        == committed.systemverilog_round
                    && retained.materialization_pending
                        == committed.materialization_pending,
                "later SV observation must preserve the prior VHDL output publication");
        };
        check_retained_output(vhdl_cycle_active_output);
        check_retained_output(vhdl_cycle_nba_output);
        require(sv_boundary_cut.completed_phase
                    == fsim::runtime::SchedulerPhase::active
                && sv_boundary_cut.signals[vhdl_cycle_active_seen].delta
                    == vhdl_output_cut.signals[vhdl_cycle_active_output].delta + 1U
                && sv_boundary_cut.signals[vhdl_cycle_nba_seen].delta
                    == vhdl_output_cut.signals[vhdl_cycle_nba_output].delta + 1U
                && committed_stamp(
                    sv_boundary_cut.signals[vhdl_cycle_active_seen],
                    "the Active VHDL-to-SV reader must have an event stamp")
                    == seen_stamp
                && committed_stamp(
                    sv_boundary_cut.signals[vhdl_cycle_nba_seen],
                    "the NBA VHDL-to-SV reader must have an event stamp")
                    == seen_stamp,
            "VHDL-to-SV event delivery must retain its next-Active-cycle cut");
        require_committed_value(sv_boundary_cut, vhdl_cycle_active_seen,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::systemverilog,
            fsim::runtime::SchedulerPhase::active, 2U, seen_stamp, false,
            "the VHDL-to-SV reader must keep the new Active round stamp");
        require_committed_value(sv_boundary_cut, vhdl_cycle_nba_seen,
            "1", "0", fsim::runtime::simir::ProcessSchedulingDomain::systemverilog,
            fsim::runtime::SchedulerPhase::active, 2U, seen_stamp, false,
            "the second VHDL-to-SV reader must keep the new Active round stamp");
        const auto frame_index = [&](const VhdlCycleBoundaryFrame& frame) {
            return static_cast<std::size_t>(
                std::addressof(frame) - compiled.frames.data());
        };
        require(frame_index(active_cut) < frame_index(nba_cut)
                && frame_index(nba_cut) < frame_index(first_vhdl_cycle)
                && frame_index(first_vhdl_cycle) < frame_index(vhdl_output_cut)
                && frame_index(vhdl_output_cut) < frame_index(sv_boundary_cut),
            "the phase snapshots must preserve Active, NBA, two VHDL cycles, then SV Active order");
    }
}

void test_o0_and_o2_native_vhdl_logic9_generic_cycle()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-vhdl-logic9-cycle-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    const auto value = [](const VhdlLogic9CycleFrame& frame,
                           const VhdlLogic9CycleSignal signal) {
        return frame.signals[signal].current.to_msb_string();
    };
    const auto first_frame = [](const VhdlLogic9CycleRun& run,
                                 const auto& predicate,
                                 const char* const message)
        -> const VhdlLogic9CycleFrame& {
        const auto found = std::ranges::find_if(run.frames, predicate);
        require(found != run.frames.end(), message);
        return *found;
    };
    const auto check_commit = [](const VhdlLogic9CycleFrame& frame,
                                  const VhdlLogic9CycleSignal signal,
                                  const std::string_view expected,
                                  const std::string_view previous,
                                  const fsim::runtime::SimulationTick time,
                                  const fsim::runtime::SchedulerPhase phase,
                                  const char* const label) {
        const auto& snapshot = frame.signals[signal];
        require(snapshot.current.to_msb_string() == expected
                && snapshot.last.to_msb_string() == previous
                && snapshot.stored.to_msb_string() == expected,
            label);
        require(snapshot.event.has_value()
                && snapshot.transaction == snapshot.event
                && snapshot.event->first == time
                && snapshot.now == time,
            label);
        require(snapshot.event_domain
                    == fsim::runtime::simir::ProcessSchedulingDomain::generic
                && snapshot.event_phase == phase
                && snapshot.systemverilog_round == 0U,
            label);
    };
    const auto frame_index = [](const VhdlLogic9CycleRun& run,
                                 const VhdlLogic9CycleFrame& frame) {
        return static_cast<std::size_t>(
            std::addressof(frame) - run.frames.data());
    };

    const std::array<fsim::project::Optimization, 2U> optimizations {{
        fsim::project::Optimization::o0,
        fsim::project::Optimization::o2,
    }};
    for (const auto optimization : optimizations) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto interpreter_root = optimization_root / "interpreter";
        const auto compiled_root = optimization_root / "compiled";
        std::filesystem::create_directories(interpreter_root);
        std::filesystem::create_directories(compiled_root);

        const auto reference = run_vhdl_logic9_cycle(optimization,
            SimulationEngine::interpreter, interpreter_root);
        const auto compiled = run_vhdl_logic9_cycle(optimization,
            SimulationEngine::compiled, compiled_root);
        require(reference.status == RunStatus::time_limit
                && compiled.status == reference.status
                && reference.time == 5U && compiled.time == reference.time,
            "the Logic9 route must retain the same requested-time boundary");
        require(compiled.compiled_process_count != 0U
                && compiled.projected.backend_runs != 0U
                && compiled.projected.completions != 0U,
            "the homogeneous Logic9 cycle must enter generated generic work");
        require(compiled.certificate_proofs[0U].has_value()
                && compiled.certificate_proofs[1U].has_value(),
            "the Logic9 middle and output writers must retain graph certificates");
        const auto& middle_proof = *compiled.certificate_proofs[0U];
        const auto& result_proof = *compiled.certificate_proofs[1U];
        using CertificateStatus = fsim::runtime::simir::
            RegionComponentCertificateStatus;
        const auto is_logic9_generic_writer = [](const auto& proof) {
            return proof.resolution
                    == fsim::runtime::simir::ResolutionKind::none
                && proof.value_kind
                    == fsim::runtime::simir::ValueKind::logic9
                && proof.drivers
                    == fsim::runtime::simir::RegionDriverClass::single_whole
                && proof.scheduling_domain
                    == fsim::runtime::simir::ProcessSchedulingDomain::generic
                && proof.update_kind
                    == fsim::runtime::simir::RegionUpdateKind::vhdl_projected
                && proof.status == CertificateStatus::structural_candidate;
        };
        require(is_logic9_generic_writer(middle_proof)
                && is_logic9_generic_writer(result_proof)
                && middle_proof.component == result_proof.component,
            "the Logic9 copy chain must form one certified generic component");
        require(!compiled.frames.empty() && !reference.frames.empty()
                && same_vhdl_logic9_cycle_frame(
                    compiled.frames.front(), reference.frames.front())
                && std::ranges::equal(compiled.frames, reference.frames,
                    same_vhdl_logic9_cycle_frame)
                && compiled.projected_by_frame.size() == compiled.frames.size()
                && reference.projected_by_frame.size() == reference.frames.size(),
            "passive Logic9 role and transaction frames must match");

        const std::array<std::pair<std::string_view, std::string_view>, 3U>
            transitions {{ { "1", "0" }, { "Z", "1" }, { "0", "Z" } }};
        std::optional<std::size_t> previous_frame_index;
        std::uint64_t previous_native_runs { };
        std::uint64_t previous_native_completions { };
        fsim::runtime::SimulationTick time = 1U;
        for (const auto& [next_value, previous_value] : transitions) {
            const auto& source_cut = first_frame(compiled,
                [&](const VhdlLogic9CycleFrame& frame) {
                    return frame.time == time
                        && value(frame, vhdl_logic9_source) == next_value
                        && value(frame, vhdl_logic9_middle) == previous_value
                        && value(frame, vhdl_logic9_result) == previous_value;
                },
                "each Logic9 input transition must precede its middle update");
            const auto& middle_cut = first_frame(compiled,
                [&](const VhdlLogic9CycleFrame& frame) {
                    return frame.time == time
                        && value(frame, vhdl_logic9_source) == next_value
                        && value(frame, vhdl_logic9_middle) == next_value
                        && value(frame, vhdl_logic9_result) == previous_value;
                },
                "the Logic9 middle must update before the result");
            const auto& result_cut = first_frame(compiled,
                [&](const VhdlLogic9CycleFrame& frame) {
                    return frame.time == time
                        && value(frame, vhdl_logic9_source) == next_value
                        && value(frame, vhdl_logic9_middle) == next_value
                        && value(frame, vhdl_logic9_result) == next_value;
                },
                "the Logic9 result must update in the following cut");

            const auto source_index = frame_index(compiled, source_cut);
            const auto middle_index = frame_index(compiled, middle_cut);
            const auto result_index = frame_index(compiled, result_cut);
            require((!previous_frame_index
                        || source_index > *previous_frame_index)
                    && source_index < middle_index
                    && middle_index < result_index,
                "Logic9 source, middle, and result must preserve callback order");
            check_commit(source_cut, vhdl_logic9_source,
                next_value, previous_value, time,
                fsim::runtime::SchedulerPhase::active,
                "the Logic9 input commit must preserve all value roles");
            check_commit(middle_cut, vhdl_logic9_middle,
                next_value, previous_value, time,
                fsim::runtime::SchedulerPhase::active,
                "the Logic9 middle commit must preserve all value roles");
            check_commit(result_cut, vhdl_logic9_result,
                next_value, previous_value, time,
                fsim::runtime::SchedulerPhase::active,
                "the Logic9 result commit must preserve all value roles");

            const auto& native_after_result
                = compiled.projected_by_frame[result_index];
            require(native_after_result.backend_runs > previous_native_runs
                    && native_after_result.completions
                        > previous_native_completions,
                "each eligible Logic9 transition must add native generic work");
            previous_native_runs = native_after_result.backend_runs;
            previous_native_completions = native_after_result.completions;
            previous_frame_index = result_index;
            ++time;
        }
    }
}

void test_o0_and_o2_native_vhdl_wide_projected_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-vhdl-wide-projected-route-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto width : wide_vhdl_projected_widths) {
        for (const auto optimization : {
                 fsim::project::Optimization::o0,
                 fsim::project::Optimization::o2 }) {
            const auto optimization_name
                = optimization == fsim::project::Optimization::o0
                    ? "o0" : "o2";
            const auto width_root
                = root.path / std::to_string(width) / optimization_name;
            const auto compiled_root = width_root / "compiled";
            const auto reference_root = width_root / "interpreter";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(reference_root);

            const auto compiled = run_vhdl_wide_projected_route(
                optimization, compiled_root, SimulationEngine::compiled,
                width);
            const auto reference = run_vhdl_wide_projected_route(
                optimization, reference_root, SimulationEngine::interpreter,
                width);
            require(compiled.frames.size() == reference.frames.size()
                    && !compiled.frames.empty(),
                "compiled and interpreter wide routes must capture the same settled windows");
            for (std::size_t frame_index = 0U;
                 frame_index < compiled.frames.size(); ++frame_index) {
                const auto& compiled_frame = compiled.frames[frame_index];
                const auto& reference_frame = reference.frames[frame_index];
                require(compiled_frame.signals.size()
                        == reference_frame.signals.size(),
                    "wide route metadata frames must retain every signal");
                for (std::size_t signal_index = 0U;
                     signal_index < compiled_frame.signals.size();
                     ++signal_index) {
                    const auto& actual = compiled_frame.signals[signal_index];
                    const auto& expected = reference_frame.signals[signal_index];
                    if (!same_signal_semantics(actual, expected)) {
                        const auto stamp = [](const auto& value) {
                            return value
                                ? std::to_string(value->first) + ":"
                                    + std::to_string(value->second)
                                : std::string { "none" };
                        };
                        const auto drivers = [](const auto& records) {
                            std::string result;
                            for (const auto& record : records) {
                                result += std::to_string(record.process)
                                    + ":" + record.value + ",";
                            }
                            return result;
                        };
                        std::cerr << "WIDE_PROJECTED_PARITY width=" << width
                                  << " optimization="
                                  << static_cast<int>(optimization)
                                  << " frame=" << frame_index
                                  << " signal=" << signal_index
                                  << " current=" << actual.current << "/" << expected.current
                                  << " last=" << actual.last << "/" << expected.last
                                  << " stored=" << actual.stored << "/" << expected.stored
                                  << " raw=" << drivers(actual.raw_drivers) << "/"
                                  << drivers(expected.raw_drivers)
                                  << " event=" << stamp(actual.event) << "/"
                                  << stamp(expected.event)
                                  << " transaction=" << stamp(actual.transaction)
                                  << "/" << stamp(expected.transaction)
                                  << " domain=" << static_cast<int>(actual.event_domain)
                                  << "/" << static_cast<int>(expected.event_domain)
                                  << " phase=" << static_cast<int>(actual.event_phase)
                                  << "/" << static_cast<int>(expected.event_phase)
                                  << " round=" << actual.systemverilog_round
                                  << "/" << expected.systemverilog_round
                                  << " time=" << actual.now << "/" << expected.now
                                  << " delta=" << actual.delta << "/" << expected.delta
                                  << '\n';
                    }
                    require(same_signal_semantics(
                                compiled_frame.signals[signal_index],
                                reference_frame.signals[signal_index]),
                        "wide projected O0/O2 current/LAST/raw/event/transaction "
                        "metadata must match the interpreter at every settled window");
                }
            }
            const auto check_equal_middle_transactions = [width](
                const WideVhdlProjectedRouteResult& route) {
                require(route.frames.size() >= 3U,
                    "the bit_vector route retains both equal-output samples");
                const auto& first_equal = route.frames[1U].signals[4U];
                const auto& second_equal = route.frames[2U].signals[4U];
                const auto zero = std::string(width, '0');
                require(first_equal.current == zero
                        && first_equal.stored == zero
                        && second_equal.current == zero
                        && second_equal.stored == zero
                        && first_equal.event == second_equal.event
                        && first_equal.transaction
                        && second_equal.transaction
                        && first_equal.transaction
                            != second_equal.transaction,
                    "an equal no-resolution projected value preserves event "
                    "state and advances transaction metadata");
            };
            check_equal_middle_transactions(compiled);
            check_equal_middle_transactions(reference);
            require(projected_profile_count(compiled.profile,
                        "backend_runs=") > 0U
                    && projected_profile_count(compiled.profile,
                        "completions=") > 0U,
                "each 65/129/256/1024-bit O0/O2 application must independently "
                "complete the native generic projected backend");
        }
    }
}

void test_o0_and_o2_native_vhdl_unresolved_logic9_route()
{
    constexpr std::array<std::uint32_t, 3U> widths { 1U, 65U, 129U };
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-vhdl-unresolved-logic9-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto width : widths) {
        for (const auto optimization : {
                 fsim::project::Optimization::o0,
                 fsim::project::Optimization::o2 }) {
            const auto optimization_name
                = optimization == fsim::project::Optimization::o0
                    ? "o0" : "o2";
            const auto width_root
                = root.path / ("width-" + std::to_string(width));
            const auto optimization_root = width_root / optimization_name;
            const auto compiled_root = optimization_root / "compiled";
            const auto reference_root = optimization_root / "interpreter";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(reference_root);

            const auto compiled = run_vhdl_wide_projected_route(optimization,
                compiled_root, SimulationEngine::compiled, width, false,
                ValueKind::logic9);
            const auto reference = run_vhdl_wide_projected_route(optimization,
                reference_root, SimulationEngine::interpreter, width, false,
                ValueKind::logic9);
            require(compiled.frames.size() == reference.frames.size()
                    && !compiled.frames.empty(),
                "compiled and interpreter Logic9 routes capture each "
                "settled window");
            for (std::size_t frame_index = 0U;
                 frame_index < compiled.frames.size(); ++frame_index) {
                const auto& compiled_signals
                    = compiled.frames[frame_index].signals;
                const auto& reference_signals
                    = reference.frames[frame_index].signals;
                require(compiled_signals.size() == reference_signals.size(),
                    "Logic9 frames retain every source and chain signal");
                for (std::size_t signal_index = 0U;
                     signal_index < compiled_signals.size(); ++signal_index) {
                    require(same_signal_semantics(
                                compiled_signals[signal_index],
                                reference_signals[signal_index]),
                        "compiled Logic9 current/LAST/stored/raw/event/transaction "
                        "metadata matches the interpreter at every settled "
                        "window");
                }
            }
            require(projected_profile_count(compiled.profile,
                        "backend_runs=") > 0U
                    && projected_profile_count(compiled.profile,
                        "completions=") > 0U,
                "each Logic9 width and O0/O2 route completes its native "
                "projected backend");
        }
    }
}

void test_o0_and_o2_native_vhdl_multioutput_projected_owner()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-vhdl-multioutput-owner-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    constexpr auto width = 129U;
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto compiled_root = optimization_root / "compiled";
        const auto reference_root = optimization_root / "interpreter";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled = run_vhdl_wide_projected_route(
            optimization, compiled_root, SimulationEngine::compiled, width,
            true);
        const auto reference = run_vhdl_wide_projected_route(
            optimization, reference_root, SimulationEngine::interpreter, width,
            true);
        require(compiled.frames.size() == reference.frames.size()
                && !compiled.frames.empty(),
            "shared-process VHDL runs must capture identical frame counts");
        for (std::size_t frame_index = 0U;
             frame_index < compiled.frames.size(); ++frame_index) {
            const auto& compiled_signals
                = compiled.frames[frame_index].signals;
            const auto& reference_signals
                = reference.frames[frame_index].signals;
            require(compiled_signals.size() == reference_signals.size(),
                "shared-process VHDL frames must retain every signal");
            for (std::size_t signal_index = 0U;
                 signal_index < compiled_signals.size(); ++signal_index) {
                require(same_signal_semantics(compiled_signals[signal_index],
                            reference_signals[signal_index]),
                    "one process with two whole projected outputs preserves "
                    "current/LAST/raw/event/transaction metadata at O0/O2");
            }
        }
        require(projected_profile_count(compiled.profile, "backend_runs=")
                    != 0U
                && projected_profile_count(compiled.profile, "completions=")
                    != 0U,
            "each O0/O2 shared-process run must compile and execute its route");
    }
}

void test_o0_wide_generic_projected_allocation_failure_retry()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-wide-projected-allocation-failure-"
            + std::to_string(nonce)) };
    const auto compiled_root = root.path / "compiled";
    const auto reference_root = root.path / "interpreter";
    std::filesystem::create_directories(compiled_root);
    std::filesystem::create_directories(reference_root);

    const auto compiled
        = run_wide_projected_failure_sweep(compiled_root);
    std::cerr << "FSIM_WIDE_PROJECTED_FAILURE_SWEEP allocations="
              << compiled.normal_run_allocations
              << " injected=" << compiled.injected_failures
              << " captured_backend_cut="
              << (compiled.captured_failure_cut
                      ? std::to_string(*compiled.captured_failure_cut)
                      : std::string { "none" })
              << " pre_body=" << compiled.pre_body_generic_failures
              << " post_body=" << compiled.post_body_generic_failures
              << " scheduler_errors=" << compiled.scheduler_failures
              << " optional_fallbacks="
              << compiled.optional_reservation_fallbacks
              << '\n';
    const auto reference = run_wide_projected_failure_reference(
        reference_root, compiled.phases);
    require(compiled.frames.size() == reference.size()
            && !compiled.frames.empty(),
        "the wide allocation-failure and interpreter twins must capture each window");
    for (std::size_t frame = 0U; frame < compiled.frames.size(); ++frame) {
        for (std::size_t signal = 0U;
             signal < compiled.frames[frame].size(); ++signal) {
            require(same_signal_semantics(
                        compiled.frames[frame][signal],
                        reference[frame][signal]),
                "retried generic-region work and interpreter execution must "
                "preserve full signal metadata");
        }
    }
    if (compiled.normal_run_allocations != 0U) {
        require(compiled.injected_failures != 0U,
            "the run-only failpoint sweep must inject within its measured bound");
        require(compiled.propagated_generic_failures != 0U
                && compiled.pre_body_generic_failures != 0U
                && compiled.pre_completion_output_state_preserved
                && compiled.captured_failure_cut
                && *compiled.captured_failure_cut
                    < compiled.normal_run_allocations,
            "the measured sweep must propagate a pre-body generic backend "
            "failure and retry the same queued work");
    }
    // Captured backend errors and optional no-throw ticket fallbacks are
    // counted separately by the sweep. Settled frames are recorded after a
    // same-deadline retry, so the interpreter comparison includes the full
    // effects of every stimulus.
}

void test_o0_and_o2_compiled_vhdl_disjoint_projected_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-vhdl-disjoint-a4-compiled-route-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto optimization_root = root.path / optimization_name;
        const auto compiled_root = optimization_root / "compiled-default";
        const auto reference_root = optimization_root / "interpreter";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled = run_vhdl_disjoint_projected_route(
            optimization, compiled_root, SimulationEngine::compiled, true);
        const auto reference = run_vhdl_disjoint_projected_route(
            optimization, reference_root, SimulationEngine::interpreter, false);
        require(compiled.compiled_writers
                && compiled.compiled_process_count >= 2U,
            "the positive application witness must execute both bound LLVM owners");
        require(compiled.frames.size() == 6U
                && compiled.frames == reference.frames,
            "compiled O0/O2 VHDL projected owners must match interpreter current/LAST/stored/raw and transaction metadata after every settled step");
    }
}

[[nodiscard]] ForwardingApplicationResult run_forwarding_application_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_forwarding_config(optimization, root);
    FixedCerrBuffer profile;
    ForwardingApplicationResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the forwarding fixture must parse and elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            NativeRegionAllocationTestAccess::
                install_forwarding_test_backend_provider(simulation);
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the forwarding application must install native processes");
        }

        constexpr std::array<std::string_view, 5U> names {
            "native_region_forwarding.source",
            "native_region_forwarding.stage0",
            "native_region_forwarding.stage1",
            "native_region_forwarding.stage2",
            "native_region_forwarding.sink",
        };
        std::array<SignalId, names.size()> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto signal = simulation.find_signal(names[index]);
            require(signal.has_value(),
                "the forwarding application must retain every chain signal");
            signals[index] = *signal;
        }

        // The HDL driver initializes the boundary at time zero and waits two
        // ticks before its first stimulus, so the initial wave settles before
        // the measured Active-region changes.
        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the forwarding application must park before its delayed finish");

        if (compiled) {
            const auto order
                = NativeRegionAllocationTestAccess::forwarding_process_order(
                    simulation);
            require(order && order->size() == 4U,
                "the parsed chain must produce one four-member forwarding body");
            require(std::adjacent_find(order->begin(), order->end(),
                        [](const ProcessId before, const ProcessId after) {
                            return before <= after;
                        }) == order->end(),
                "the native body must execute in reverse ProcessId order");
        }

        const auto capture_frame = [&] {
            ForwardingApplicationResult::Frame frame;
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                frame[index] = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            }
            return frame;
        };
        result.activation_snapshots.reserve(5U);
        result.activation_snapshots.push_back(capture_frame());

        constexpr std::array<std::string_view, 4U> inputs {
            "1", "X", "Z", "0" };
        for (const auto input : inputs) {
            const auto before = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            require(simulation.run(simulation.now() + 1U).status
                        == RunStatus::time_limit,
                "each forwarding stimulus must preserve delayed finish work");
            const auto after = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            if (compiled) {
                require(after.attempts > before.attempts
                        && after.evaluations > before.evaluations
                        && after.member_consumptions
                            >= before.member_consumptions + 4U,
                    "each 0/1/X/Z input wave must evaluate the native forwarding "
                    "body and consume all four original member callbacks");
            }
            result.activation_snapshots.push_back(capture_frame());
            const auto& frame = result.activation_snapshots.back();
            const auto expected = std::string { input };
            for (std::size_t signal = 0U; signal < frame.size(); ++signal) {
                require(frame[signal].current == expected,
                    "every continuous-assignment stage must preserve 0/1/X/Z");
            }
        }
        result.counters
            = NativeRegionAllocationTestAccess::region_forwarding_counters(
                simulation);
    }
    require(!profile.overflowed(),
        "the forwarding wave profile must remain complete");
    result.profile = profile.view();
    return result;
}

[[nodiscard]] ForwardingApplicationResult
run_mixed_internal_external_forwarding_application_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const std::size_t width)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment native_process_counts {
        "FSIM_PROFILE_NATIVE_PROCESS_COUNTS", compiled ? "1" : nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_mixed_internal_external_forwarding_config(
        optimization, root, width);
    const std::string initial_value(width, '0');
    const std::string external_only_value(width, '1');
    const auto simultaneous_source
        = mixed_forwarding_source_value(width);
    const std::string mixed_after_simultaneous
        = forwarding_diamond_join_value(simultaneous_source);
    FixedCerrBuffer profile;
    ForwardingApplicationResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the mixed forwarding fixture must parse and elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            NativeRegionAllocationTestAccess::
                install_forwarding_test_backend_provider(simulation);
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the mixed forwarding design must install native processes");
        }

        constexpr std::array<std::string_view, 5U> names {
            "native_region_mixed_internal_external_forwarding.source",
            "native_region_mixed_internal_external_forwarding.external_input",
            "native_region_mixed_internal_external_forwarding.parent_value",
            "native_region_mixed_internal_external_forwarding.mixed_out",
            "native_region_mixed_internal_external_forwarding.parent_out",
        };
        std::array<SignalId, names.size()> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto signal = simulation.find_signal(names[index]);
            require(signal.has_value(),
                "the mixed forwarding design must retain all five signals");
            signals[index] = *signal;
        }

        // These outputs are real public sinks. Register them before graph
        // certification so they remain outside the private internal cut.
        static_cast<void>(simulation.read_signal(signals[3U]));
        static_cast<void>(simulation.read_signal(signals[4U]));

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the mixed forwarding fixture must settle its initial zero values");

        std::optional<ProcessId> root_process;
        std::optional<ProcessId> mixed_child_process;
        std::optional<ProcessId> parent_only_child_process;
        if (compiled) {
            const auto order
                = NativeRegionAllocationTestAccess::forwarding_process_order(
                    simulation);
            require(order && order->size() == 3U,
                "the mixed graph must retain its root and two distinct readers");
            root_process
                = NativeRegionAllocationTestAccess::forwarding_output_owner(
                    simulation, signals[2U]);
            mixed_child_process
                = NativeRegionAllocationTestAccess::forwarding_output_owner(
                    simulation, signals[3U]);
            parent_only_child_process
                = NativeRegionAllocationTestAccess::forwarding_output_owner(
                    simulation, signals[4U]);
            const auto first_parent_reader
                = root_process
                ? NativeRegionAllocationTestAccess::forwarding_direct_reader(
                      simulation, *root_process, signals[2U])
                : std::optional<ProcessId> { };
            require(root_process && mixed_child_process
                    && parent_only_child_process
                    && *root_process == order->front()
                    && *root_process > *mixed_child_process
                    && *root_process > *parent_only_child_process
                    && *root_process != *mixed_child_process
                    && *root_process != *parent_only_child_process
                    && *mixed_child_process != *parent_only_child_process
                    && first_parent_reader == mixed_child_process,
                "the forwarding member map must identify one root and both children");
        }

        const auto capture_frame = [&] {
            ForwardingApplicationResult::Frame frame;
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                frame[index] = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            }
            return frame;
        };
        result.activation_snapshots.reserve(3U);
        result.activation_snapshots.push_back(capture_frame());
        for (const auto& signal : result.activation_snapshots.back()) {
            require(signal.current == initial_value,
                "the initial root, external input, and both children must settle to zero");
        }

        NativeRegionAllocationTestAccess::RegionForwardingCounters before;
        std::optional<std::uint64_t> root_resumes_before;
        std::optional<std::uint64_t> mixed_child_resumes_before;
        std::optional<std::uint64_t> parent_child_resumes_before;
        if (compiled) {
            before
                = NativeRegionAllocationTestAccess::region_forwarding_counters(
                    simulation);
            root_resumes_before
                = NativeRegionAllocationTestAccess::native_process_resume_count(
                    simulation, *root_process);
            mixed_child_resumes_before
                = NativeRegionAllocationTestAccess::native_process_resume_count(
                    simulation, *mixed_child_process);
            parent_child_resumes_before
                = NativeRegionAllocationTestAccess::native_process_resume_count(
                    simulation, *parent_only_child_process);
            require(root_resumes_before && mixed_child_resumes_before
                    && parent_child_resumes_before,
                "native per-process resume counts must be available after startup");
        }

        require(simulation.run(simulation.now() + 1U).status
                    == RunStatus::time_limit,
            "the external-only stimulus must preserve delayed finish work");
        if (compiled) {
            const auto after
                = NativeRegionAllocationTestAccess::region_forwarding_counters(
                    simulation);
            const auto root_resumes_after
                = NativeRegionAllocationTestAccess::native_process_resume_count(
                    simulation, *root_process);
            const auto mixed_child_resumes_after
                = NativeRegionAllocationTestAccess::native_process_resume_count(
                    simulation, *mixed_child_process);
            const auto parent_child_resumes_after
                = NativeRegionAllocationTestAccess::native_process_resume_count(
                    simulation, *parent_only_child_process);
            require(root_resumes_after && mixed_child_resumes_after
                    && parent_child_resumes_after
                    && after.attempts > before.attempts
                    && after.evaluations > before.evaluations
                    && after.member_consumptions
                        == before.member_consumptions + 1U
                    && *root_resumes_after == *root_resumes_before
                    && *mixed_child_resumes_after
                        == *mixed_child_resumes_before
                    && *parent_child_resumes_after
                        == *parent_child_resumes_before,
                "an external-only event must consume the mixed child through native forwarding while all three process executors stay quiet");
        }

        const auto& external_frame = capture_frame();
        require(external_frame[0U].current == initial_value
                && external_frame[1U].current == external_only_value
                && external_frame[2U].current == initial_value
                && external_frame[3U].current == external_only_value
                && external_frame[4U].current == initial_value,
            "the external-only child must update while the internal parent and parent-only child remain unchanged");
        result.activation_snapshots.push_back(external_frame);

        require(simulation.run(simulation.now() + 1U).status
                    == RunStatus::time_limit,
            "the simultaneous source/external stimulus must settle before finish");
        const auto simultaneous_frame = capture_frame();
        require(simultaneous_frame[0U].current
                    == normalize_logic_text(simultaneous_source)
                && simultaneous_frame[1U].current == initial_value
                && simultaneous_frame[2U].current
                    == normalize_logic_text(simultaneous_source)
                && simultaneous_frame[3U].current
                    == normalize_logic_text(mixed_after_simultaneous)
                && simultaneous_frame[4U].current
                    == normalize_logic_text(simultaneous_source),
            "both changed boundaries must update the parent and both children with four-state values");
        result.activation_snapshots.push_back(simultaneous_frame);
        if (compiled) {
            result.counters
                = NativeRegionAllocationTestAccess::region_forwarding_counters(
                    simulation);
            require(result.counters.evaluations != 0U
                    && result.counters.member_consumptions != 0U,
                "the parsed mixed graph must report native forwarding consumption");
        }
    }
    require(!profile.overflowed(),
        "the mixed forwarding profile must remain complete");
    result.profile = profile.view();
    return result;
}

void test_o0_and_o2_application_region_forwarding()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-forwarding-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        const auto compiled_root = root.path / optimization_name / "compiled";
        const auto reference_root = root.path / optimization_name / "reference";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled = run_forwarding_application_route(optimization,
            compiled_root, SimulationEngine::compiled);
        const auto reference = run_forwarding_application_route(optimization,
            reference_root, SimulationEngine::interpreter);
        require(same_signal_snapshots(compiled.activation_snapshots,
                    reference.activation_snapshots),
            "native forwarding must match interpreter values, current/LAST, "
            "raw drivers, event/transaction stamps, and scheduler metadata at "
            "every settled input wave");
        require(compiled.counters.attempts > 0U
                && compiled.counters.evaluations >= 4U
                && compiled.counters.member_consumptions >= 16U
                && compiled.counters.declines < compiled.counters.attempts,
            "the actual O0/O2 application route must report successful native "
            "forwarding work");
        require(profile_count(compiled.profile, "region_forwarding_evaluations=")
                    >= 4U
                && profile_count(compiled.profile,
                       "region_forwarding_member_consumptions=") >= 16U,
            "the emitted wave profile must expose successful forwarding work");
    }
}

void test_o0_and_o2_mixed_internal_external_forwarding()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-mixed-forwarding-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        for (const auto width : { 1U, 65U, 129U }) {
            const auto case_root = root.path / optimization_name
                / ("width-" + std::to_string(width));
            const auto compiled_root = case_root / "compiled";
            const auto reference_root = case_root / "reference";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(reference_root);

            const auto compiled
                = run_mixed_internal_external_forwarding_application_route(
                    optimization, compiled_root, SimulationEngine::compiled,
                    width);
            const auto reference
                = run_mixed_internal_external_forwarding_application_route(
                    optimization, reference_root,
                    SimulationEngine::interpreter, width);
            require(compiled.activation_snapshots.size() == 3U
                    && same_signal_snapshots(compiled.activation_snapshots,
                        reference.activation_snapshots),
                "mixed internal/external O0/O2 callbacks must match the interpreter's complete values and event metadata at every settled cut");
            require(compiled.counters.attempts >= 1U
                    && compiled.counters.evaluations >= 1U
                    && compiled.counters.member_consumptions >= 1U,
                "the parsed mixed-sensitivity application must consume native forwarding members");
            require(profile_count(compiled.profile,
                        "region_forwarding_evaluations=") >= 1U
                    && profile_count(compiled.profile,
                        "region_forwarding_member_consumptions=") >= 1U,
                "the O0/O2 wave profiles must expose native mixed forwarding");
        }
    }
}

[[nodiscard]] ForwardingDiamondApplicationResult
run_forwarding_diamond_application_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const std::size_t width = 1U,
    const bool add_unsigned = false)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_forwarding_diamond_config(
        optimization, root, width, add_unsigned);
    FixedCerrBuffer profile;
    ForwardingDiamondApplicationResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the forwarding diamond must parse and elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            NativeRegionAllocationTestAccess::
                install_forwarding_test_backend_provider(simulation);
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the forwarding diamond must install native processes");
        }

        constexpr std::array<std::string_view, 6U> names {
            "native_region_forwarding_diamond.source",
            "native_region_forwarding_diamond.root_value",
            "native_region_forwarding_diamond.left_branch",
            "native_region_forwarding_diamond.right_branch",
            "native_region_forwarding_diamond.joined",
            "native_region_forwarding_diamond.sink",
        };
        std::array<SignalId, names.size()> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto signal = simulation.find_signal(names[index]);
            require(signal.has_value(),
                "the forwarding diamond must retain each signal handle");
            signals[index] = *signal;
        }

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the forwarding diamond must park before its delayed finish");
        if (compiled) {
            const auto order
                = NativeRegionAllocationTestAccess::forwarding_process_order(
                    simulation);
            require(order && order->size() == 5U,
                "the parsed diamond must install all five forwarding members");
            require(std::adjacent_find(order->begin(), order->end(),
                        [](const ProcessId before, const ProcessId after) {
                            return before >= after;
                        }) == order->end(),
                "root, both branch writers, join, and sink retain settled-cut order");
        }

        const auto capture_frame = [&] {
            ForwardingDiamondApplicationResult::Frame frame;
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                frame[index] = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            }
            if (compiled) {
                NativeRegionAllocationTestAccess::
                    overlay_applied_forwarding_role_journal(
                        simulation, std::span { frame });
            }
            return frame;
        };
        const auto inputs = add_unsigned
            ? forwarding_add_source_values(width)
            : forwarding_diamond_source_values(width);
        result.activation_snapshots.reserve(inputs.size() + 1U);
        result.activation_snapshots.push_back(capture_frame());
        std::string previous_join(width, '0');
        for (const auto& input : inputs) {
            const auto before = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            require(simulation.run(simulation.now() + 1U).status
                        == RunStatus::time_limit,
                "each diamond stimulus must preserve delayed finish work");
            const auto after = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            const auto expected_input = normalize_logic_text(input);
            const auto expected_join = add_unsigned
                ? forwarding_diamond_add_value(input)
                : forwarding_diamond_join_value(input);
            if (compiled) {
                require(after.attempts > before.attempts
                        && after.evaluations > before.evaluations,
                    "each source wave must attempt and evaluate forwarding");
                const auto member_delta
                    = after.member_consumptions - before.member_consumptions;
                const auto expected_member_delta
                    = 4U + (expected_join != previous_join ? 1U : 0U);
                if (add_unsigned) {
                    require(member_delta == expected_member_delta,
                        "add forwarding must consume root, both branches, "
                        "and join, with a sink callback only when the join changes");
                } else {
                    require(member_delta >= 4U,
                        "each changed source consumes the root, both branches, "
                        "and native join callbacks");
                    if (expected_join != previous_join) {
                        require(member_delta >= 5U,
                            "a changed joined output also consumes the real "
                            "sink reader");
                    }
                }
            }
            result.activation_snapshots.push_back(capture_frame());
            const auto& frame = result.activation_snapshots.back();
            require(frame[0U].current == expected_input
                    && frame[1U].current == expected_input
                    && frame[2U].current == expected_input
                    && frame[3U].current == expected_input
                    && frame[4U].current == expected_join
                    && frame[5U].current == expected_join,
                "the root, fanout branches, four-state join, and real sink "
                "reader settle to their expected values");
            previous_join = expected_join;
        }
        result.counters
            = NativeRegionAllocationTestAccess::region_forwarding_counters(
                simulation);

        // Keep all per-wave snapshots passive, then exercise one real public
        // materialization barrier after the native work has been counted.
        static_cast<void>(simulation.read_signal_snapshot(signals[1U]));
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            result.public_observation_snapshot[index]
                = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            require(same_signal_semantics(
                        result.public_observation_snapshot[index],
                        result.activation_snapshots.back()[index]),
                "the final public read must materialize the passive applied-role frame");
        }
    }
    require(!profile.overflowed(),
        "the forwarding diamond profile must remain complete");
    result.profile = profile.view();
    return result;
}

[[nodiscard]] MultioutputForwardingApplicationResult
run_multioutput_forwarding_application_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const std::size_t width)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_multioutput_forwarding_config(
        optimization, root, width);
    FixedCerrBuffer profile;
    MultioutputForwardingApplicationResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the parsed multi-output forwarding design must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            NativeRegionAllocationTestAccess::
                install_forwarding_test_backend_provider(simulation);
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the parsed multi-output design must install native processes");
        }

        constexpr std::array<std::string_view, 5U> names {
            "native_region_multioutput_forwarding.source",
            "native_region_multioutput_forwarding.left",
            "native_region_multioutput_forwarding.right",
            "native_region_multioutput_forwarding.left_seen",
            "native_region_multioutput_forwarding.right_seen",
        };
        std::array<SignalId, names.size()> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto signal = simulation.find_signal(names[index]);
            require(signal.has_value(),
                "the multi-output design must retain every signal handle");
            signals[index] = *signal;
        }

        // The two observed leaf outputs are public sinks, not internal
        // forwarding values. Expose them before the graph is certified.
        static_cast<void>(simulation.read_signal(signals[3U]));
        static_cast<void>(simulation.read_signal(signals[4U]));
        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the multi-output source must park before delayed finish work");
        if (compiled) {
            const auto order
                = NativeRegionAllocationTestAccess::forwarding_process_order(
                    simulation);
            require(order && order->size() == 3U,
                "one two-output parent and two real readers must form the forwarding chain");
            const auto parent
                = NativeRegionAllocationTestAccess::forwarding_parent_with_two_outputs(
                    simulation, signals[1U], signals[2U], width);
            require(parent.has_value(),
                "the certified forwarding member must own the parent's two whole outputs");
            const auto left_reader
                = NativeRegionAllocationTestAccess::forwarding_direct_reader(
                    simulation, *parent, signals[1U]);
            const auto right_reader
                = NativeRegionAllocationTestAccess::forwarding_direct_reader(
                    simulation, *parent, signals[2U]);
            require(left_reader && right_reader
                    && *left_reader != *right_reader
                    && *left_reader != *parent
                    && *right_reader != *parent,
                "both whole parent outputs must feed distinct real child readers");
        }

        const auto capture_frame = [&] {
            MultioutputForwardingApplicationResult::Frame frame;
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                frame[index] = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            }
            if (compiled) {
                NativeRegionAllocationTestAccess::
                    overlay_applied_forwarding_role_journal(
                        simulation, std::span { frame });
            }
            return frame;
        };
        const auto inputs = forwarding_diamond_source_values(width);
        result.activation_snapshots.reserve(inputs.size() + 1U);
        result.activation_snapshots.push_back(capture_frame());
        const std::string initial(width, '0');
        const auto& initial_frame = result.activation_snapshots.back();
        for (const auto& signal : initial_frame) {
            require(signal.current == initial,
                "both whole parent outputs and both child readers settle at initialization");
        }

        for (const auto& input : inputs) {
            const auto before = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            require(simulation.run(simulation.now() + 1U).status
                        == RunStatus::time_limit,
                "each multi-output stimulus must preserve delayed finish work");
            const auto after = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            if (compiled) {
                require(after.attempts > before.attempts
                        && after.evaluations > before.evaluations
                        && after.member_consumptions
                            >= before.member_consumptions + 3U,
                    "each source wave must execute the parent and both readers natively");
                require(after.private_parent_slots_elided
                            == before.private_parent_slots_elided + 2U,
                    "the parent must elide both private output slots "
                    "on each source wave");
            }

            result.activation_snapshots.push_back(capture_frame());
            const auto expected = normalize_logic_text(input);
            for (const auto& signal : result.activation_snapshots.back()) {
                require(signal.current == expected,
                    "the parent concat and both readers preserve every source bit "
                    "and four-state value");
            }
        }
        result.counters
            = NativeRegionAllocationTestAccess::region_forwarding_counters(
                simulation);

        // Keep the per-wave frames passive. Once native work is counted, use
        // the public read barrier to materialize the complete private role
        // journal and compare it with the final applied-row view.
        static_cast<void>(simulation.read_signal_snapshot(signals[1U]));
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            result.public_observation_snapshot[index]
                = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            require(same_signal_semantics(
                        result.public_observation_snapshot[index],
                        result.activation_snapshots.back()[index]),
                "the final public read must materialize the passive multi-output frame");
        }
    }
    require(!profile.overflowed(),
        "the multi-output forwarding profile must remain complete");
    result.profile = profile.view();
    return result;
}

[[nodiscard]] WideForwardingFailureApplicationResult
run_wide_forwarding_failure_application_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    constexpr std::size_t width = 129U;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_forwarding_diamond_config(
        optimization, root, width);
    FixedCerrBuffer profile;
    WideForwardingFailureApplicationResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the wide forwarding failure fixture must parse and elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            SimulationEngine::compiled,
            SystemVerilogVpiRuntimeUpdates::omitted);
        result.probe
            = NativeRegionAllocationTestAccess::
                install_wide_forwarding_failure_provider(simulation);
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the wide forwarding failure fixture must compile its processes");

        constexpr std::array<std::string_view, 6U> names {
            "native_region_forwarding_diamond.source",
            "native_region_forwarding_diamond.root_value",
            "native_region_forwarding_diamond.left_branch",
            "native_region_forwarding_diamond.right_branch",
            "native_region_forwarding_diamond.joined",
            "native_region_forwarding_diamond.sink",
        };
        std::array<SignalId, names.size()> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto signal = simulation.find_signal(names[index]);
            require(signal.has_value(),
                "the wide forwarding failure fixture must retain every signal");
            signals[index] = *signal;
        }

        const auto capture_frame = [&] {
            WideForwardingFailureApplicationResult::Frame frame;
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                frame[index] = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            }
            NativeRegionAllocationTestAccess::
                overlay_applied_forwarding_role_journal(
                    simulation, std::span { frame });
            return frame;
        };

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the wide forwarding fixture must settle its initial source");
        result.probe->failure_enabled = true;
        result.activation_snapshots.push_back(capture_frame());

        const auto inputs = forwarding_diamond_source_values(width);
        result.activation_snapshots.reserve(inputs.size() + 1U);
        for (std::size_t phase = 0U; phase < inputs.size(); ++phase) {
            const bool failure_was_attempted
                = result.probe->failure_attempted;
            if (failure_was_attempted) {
                result.probe->retry_allowed = true;
            }
            const auto before
                = NativeRegionAllocationTestAccess::region_forwarding_counters(
                    simulation);
            require(simulation.run(simulation.now() + 1U).status
                    == RunStatus::time_limit,
                "the failure and retry windows must preserve finish work");
            const auto after
                = NativeRegionAllocationTestAccess::region_forwarding_counters(
                    simulation);

            if (!failure_was_attempted
                && result.probe->failure_attempted) {
                result.failure_window = phase;
                result.failure_counted_as_forwarding_decline
                    = after.attempts > before.attempts
                    && after.declines > before.declines;
                require(result.probe->successful_calls_before_failure
                            == NativeRegionAllocationTestAccess::
                                ForwardingFailureProbe::retained_snapshot_count
                        && result.probe->retained_snapshots
                            == NativeRegionAllocationTestAccess::
                                ForwardingFailureProbe::retained_snapshot_count,
                    "two successful wide provider calls must retain the recycled output slots before failure");
                require(phase + 1U < inputs.size(),
                    "the injected failure must leave a changed native retry window");
            } else if (result.failure_window
                && phase == *result.failure_window + 1U) {
                result.retry_completed_natively
                    = after.evaluations > before.evaluations
                    && after.member_consumptions
                        >= before.member_consumptions + 4U
                    && result.probe->retry_successful_calls != 0U;
            }

            result.activation_snapshots.push_back(capture_frame());
            const auto& frame = result.activation_snapshots.back();
            const auto expected_input = normalize_logic_text(inputs[phase]);
            const auto expected_join
                = forwarding_diamond_join_value(inputs[phase]);
            require(frame[0U].current == expected_input
                    && frame[1U].current == expected_input
                    && frame[2U].current == expected_input
                    && frame[3U].current == expected_input
                    && frame[4U].current == expected_join
                    && frame[5U].current == expected_join,
                "checked fallback and native retry must preserve the 129-bit forwarding values");
        }
        result.counters
            = NativeRegionAllocationTestAccess::region_forwarding_counters(
                simulation);

        // Keep the failure and retry frames passive while checking them.
        // After counting native work, use one public read to materialize any
        // applied private rows and compare all six signal summaries.
        static_cast<void>(simulation.read_signal_snapshot(signals[1U]));
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            result.public_observation_snapshot[index]
                = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            require(same_signal_semantics(
                        result.public_observation_snapshot[index],
                        result.activation_snapshots.back()[index]),
                "the final public read must materialize the passive failure/retry frame");
        }
    }
    require(!profile.overflowed(),
        "the forwarding allocation failure profile must remain complete");
    return result;
}

void test_o0_and_o2_wide_forwarding_provider_preentry_failure()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-wide-forwarding-provider-failure-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    constexpr std::size_t width = 129U;
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
        const auto compiled_root
            = root.path / optimization_name / "compiled";
        const auto reference_root
            = root.path / optimization_name / "reference";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled
            = run_wide_forwarding_failure_application_route(
                optimization, compiled_root);
        const auto reference = run_forwarding_diamond_application_route(
            optimization, reference_root, SimulationEngine::interpreter,
            width);
        require(compiled.activation_snapshots.size() == 6U
                && same_signal_snapshots(compiled.activation_snapshots,
                    reference.activation_snapshots),
            "provider pre-entry failure, checked callback fallback, and retry preserve full interpreter metadata parity");
        require(compiled.probe != nullptr
                && compiled.probe->failure_attempted
                && compiled.probe->allocation_injected
                && compiled.probe->backend_declined
                && compiled.probe->caller_outputs_unchanged
                && compiled.probe->retained_outputs_unchanged
                && compiled.probe->retained_snapshots_match()
                && compiled.probe->successful_calls_before_failure
                    == NativeRegionAllocationTestAccess::
                        ForwardingFailureProbe::retained_snapshot_count,
            "the injected provider preparation failure must leave every caller output and retained wide snapshot unchanged");
        require(compiled.failure_window.has_value()
                && compiled.failure_counted_as_forwarding_decline
                && compiled.retry_completed_natively
                && compiled.probe->retry_successful_calls != 0U
                && compiled.counters.evaluations >= 4U
                && compiled.counters.member_consumptions >= 16U,
            "the failed forwarding attempt must fall back and a later changed window must retry native forwarding");
    }
}

void test_o0_and_o2_application_region_forwarding_multioutput_parent()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-multioutput-forwarding-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        for (const std::size_t width : { 1U, 65U, 129U }) {
            const auto case_name = "width-" + std::to_string(width);
            const auto compiled_root
                = root.path / optimization_name / case_name / "compiled";
            const auto reference_root
                = root.path / optimization_name / case_name / "reference";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(reference_root);

            const auto compiled = run_multioutput_forwarding_application_route(
                optimization, compiled_root, SimulationEngine::compiled, width);
            const auto reference = run_multioutput_forwarding_application_route(
                optimization, reference_root, SimulationEngine::interpreter,
                width);
            const auto input_count
                = forwarding_diamond_source_values(width).size();
            require(compiled.activation_snapshots.size() == input_count + 1U
                    && same_signal_snapshots(compiled.activation_snapshots,
                        reference.activation_snapshots),
                "parsed forwarding matches interpreter current/LAST/stored/raw-owner "
                "and event/transaction metadata at each settled window");
            require(compiled.counters.attempts >= input_count
                    && compiled.counters.evaluations >= input_count
                    && compiled.counters.member_consumptions
                        >= input_count * 3U
                    && compiled.counters.private_parent_slots_elided
                        >= input_count * 2U,
                "each 1/65/129-bit source wave consumes the parent/readers "
                "and elides both private slots");
            require(profile_count(compiled.profile,
                        "region_forwarding_evaluations=") >= input_count
                    && profile_count(compiled.profile,
                        "region_forwarding_member_consumptions=")
                        >= input_count * 3U
                    && profile_count(compiled.profile,
                        "region_forwarding_private_parent_slots_elided=")
                        >= input_count * 2U,
                "the profile must expose the exact multi-output native forwarding work");
        }
    }
}

void test_o0_and_o2_application_region_forwarding_diamond()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-forwarding-diamond-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
        const auto compiled_root = root.path / optimization_name / "compiled";
        const auto reference_root = root.path / optimization_name / "reference";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled = run_forwarding_diamond_application_route(
            optimization, compiled_root, SimulationEngine::compiled);
        const auto reference = run_forwarding_diamond_application_route(
            optimization, reference_root, SimulationEngine::interpreter);
        require(same_signal_snapshots(compiled.activation_snapshots,
                    reference.activation_snapshots),
            "native diamond forwarding matches interpreter current/LAST/raw "
            "and event/transaction metadata at each settled wave");
        require(std::equal(compiled.public_observation_snapshot.begin(),
                    compiled.public_observation_snapshot.end(),
                    reference.public_observation_snapshot.begin(),
                    [](const auto& native, const auto& interpreted) {
                        return same_signal_semantics(native, interpreted);
                    }),
            "the final public materialization must match the interpreter roles and metadata");
        require(compiled.counters.attempts >= 4U
                && compiled.counters.evaluations >= 4U
                && compiled.counters.member_consumptions >= 10U
                && compiled.counters.declines < compiled.counters.attempts,
            "O0/O2 application runs must consume the join in a successful "
            "five-member native forwarding body");
        require(profile_count(compiled.profile,
                    "region_forwarding_evaluations=") >= 4U
                && profile_count(compiled.profile,
                    "region_forwarding_member_consumptions=") >= 10U,
            "the wave profile must expose successful native diamond forwarding");
    }
}

[[nodiscard]] ForwardingDiamondApplicationResult
run_unequal_depth_forwarding_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const std::size_t width)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_unequal_depth_forwarding_config(
        optimization, root, width);
    FixedCerrBuffer profile;
    ForwardingDiamondApplicationResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the unequal-depth forwarding fixture must parse and elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            NativeRegionAllocationTestAccess::
                install_forwarding_test_backend_provider(simulation);
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the unequal-depth fixture must install compiled processes");
        }

        constexpr std::array<std::string_view, 6U> names {
            "native_region_unequal_depth_forwarding.source",
            "native_region_unequal_depth_forwarding.a_stage",
            "native_region_unequal_depth_forwarding.a_value",
            "native_region_unequal_depth_forwarding.b_value",
            "native_region_unequal_depth_forwarding.joined",
            "native_region_unequal_depth_forwarding.sink",
        };
        std::array<SignalId, names.size()> signals { };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            const auto signal = simulation.find_signal(names[index]);
            require(signal.has_value(),
                "the unequal-depth fixture must retain every signal handle");
            signals[index] = *signal;
        }

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the unequal-depth fixture must park before its delayed finish");
        if (compiled) {
            const auto order
                = NativeRegionAllocationTestAccess::forwarding_process_order(
                    simulation);
            require(order && order->size() == 5U,
                "the unequal-depth circuit must retain its five forwarding members");
        }

        const auto capture_frame = [&] {
            ForwardingDiamondApplicationResult::Frame frame;
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                frame[index] = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            }
            if (compiled) {
                NativeRegionAllocationTestAccess::
                    overlay_applied_forwarding_role_journal(
                        simulation, std::span { frame });
            }
            return frame;
        };
        const auto inputs = forwarding_diamond_source_values(width);
        result.activation_snapshots.reserve(inputs.size() + 1U);
        result.activation_snapshots.push_back(capture_frame());
        for (std::size_t phase = 0U; phase < inputs.size(); ++phase) {
            const auto before = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            require(simulation.run(simulation.now() + 1U).status
                        == RunStatus::time_limit,
                "each unequal-depth source change must preserve delayed finish work");
            const auto after = compiled
                ? NativeRegionAllocationTestAccess::region_forwarding_counters(
                      simulation)
                : NativeRegionAllocationTestAccess::RegionForwardingCounters { };
            if (compiled) {
                require(after.evaluations > before.evaluations,
                    "each unequal-depth source wave evaluates its native root prefix");
                require(after.member_consumptions
                            >= before.member_consumptions + 1U,
                    "an unequal-depth partial cut consumes native root work");
                if (phase == 0U) {
                    require(after.declines > before.declines,
                        "the first mixed-depth join cut declines before its checked callback");
                }
            }

            result.activation_snapshots.push_back(capture_frame());
            const auto& frame = result.activation_snapshots.back();
            const auto expected_source = normalize_logic_text(inputs[phase]);
            std::string expected_b = expected_source;
            for (auto& bit : expected_b) {
                if (bit == '0') {
                    bit = '1';
                } else if (bit == '1') {
                    bit = '0';
                } else {
                    bit = 'X';
                }
            }
            std::string expected_join(width, '1');
            for (std::size_t bit = 0U; bit < width; ++bit) {
                if (expected_source[bit] == 'X'
                    || expected_source[bit] == 'Z') {
                    expected_join[bit] = 'X';
                }
            }
            require(frame[0U].current == expected_source
                    && frame[1U].current == expected_source
                    && frame[2U].current == expected_source
                    && frame[3U].current == expected_b
                    && frame[4U].current == expected_join
                    && frame[5U].current == expected_join,
                "the unequal-depth XOR output must settle to its four-state value");
        }
        result.counters
            = NativeRegionAllocationTestAccess::region_forwarding_counters(
                simulation);

        // The partial mixed-depth callback may leave applied private rows
        // behind. Count its native work first, then materialize once through
        // the public read barrier and compare every role with the passive
        // applied-row frame.
        static_cast<void>(simulation.read_signal_snapshot(signals[2U]));
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            result.public_observation_snapshot[index]
                = NativeRegionAllocationTestAccess::summarize(
                    simulation, signals[index]);
            require(same_signal_semantics(
                        result.public_observation_snapshot[index],
                        result.activation_snapshots.back()[index]),
                "the final public read must materialize the passive unequal-depth frame");
        }
    }
    require(!profile.overflowed(),
        "the unequal-depth forwarding profile must remain complete");
    result.profile = profile.view();
    return result;
}

void test_o0_and_o2_application_region_forwarding_unequal_depth_wide()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-forwarding-unequal-depth-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);
    constexpr std::size_t width = 65U;

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
        const auto compiled_root
            = root.path / optimization_name / "compiled";
        const auto reference_root
            = root.path / optimization_name / "reference";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(reference_root);

        const auto compiled = run_unequal_depth_forwarding_route(
            optimization, compiled_root, SimulationEngine::compiled, width);
        const auto reference = run_unequal_depth_forwarding_route(
            optimization, reference_root, SimulationEngine::interpreter, width);
        require(compiled.activation_snapshots.size() == 6U
                && same_signal_snapshots(compiled.activation_snapshots,
                    reference.activation_snapshots),
            "wide unequal-depth native work must match interpreter metadata "
            "at every settled source window");
        const std::string expected_glitch
            = std::string(width - 64U, '0') + std::string(64U, '1');
        const auto& glitch_frame = compiled.activation_snapshots[1U];
        require(glitch_frame[4U].current == std::string(width, '1')
                && glitch_frame[5U].current == std::string(width, '1')
                && glitch_frame[4U].last == expected_glitch
                && glitch_frame[5U].last == expected_glitch,
            "the unequal-depth source wave preserves the intermediate wide join glitch and LAST value");
        require(compiled.counters.evaluations >= 5U
                && compiled.counters.member_consumptions != 0U
                && compiled.counters.declines != 0U,
            "O0/O2 unequal-depth runs must retain native root work and checked partial-cut fallback");
        require(profile_count(compiled.profile,
                    "region_forwarding_evaluations=") >= 5U
                && profile_count(compiled.profile,
                    "region_forwarding_declines=") != 0U,
            "the unequal-depth O0/O2 profile must expose root evaluation and cut decline");
    }
}

void test_o0_and_o2_application_region_forwarding_add_unsigned()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-forwarding-add-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
        for (const auto width : { 65U, 129U }) {
            const auto case_root = root.path / optimization_name
                / ("width-" + std::to_string(width));
            const auto compiled_root = case_root / "compiled";
            const auto reference_root = case_root / "reference";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(reference_root);

            const auto compiled = run_forwarding_diamond_application_route(
                optimization, compiled_root, SimulationEngine::compiled,
                width, true);
            const auto reference = run_forwarding_diamond_application_route(
                optimization, reference_root, SimulationEngine::interpreter,
                width, true);
            const auto inputs = forwarding_add_source_values(width);
            require(compiled.activation_snapshots.size() == inputs.size() + 1U
                    && same_signal_snapshots(compiled.activation_snapshots,
                        reference.activation_snapshots),
                "parsed 65/129-bit add forwarding must match interpreter values "
                "and full signal metadata at every settled key");

            std::size_t expected_member_consumptions { };
            auto previous_join = std::string(width, '0');
            for (const auto& input : inputs) {
                const auto joined = forwarding_diamond_add_value(input);
                expected_member_consumptions += 4U;
                if (joined != previous_join) {
                    ++expected_member_consumptions;
                }
                previous_join = joined;
            }
            require(compiled.counters.attempts >= inputs.size()
                    && compiled.counters.evaluations >= inputs.size()
                    && compiled.counters.member_consumptions
                        >= expected_member_consumptions
                    && compiled.counters.declines < compiled.counters.attempts,
                "the runtime must execute operation-bearing private forwarding "
                "on every add wave, suppressing downstream dispatch on "
                "unchanged outputs");
            require(profile_count(compiled.profile,
                        "region_forwarding_evaluations=") >= inputs.size()
                    && profile_count(compiled.profile,
                        "region_forwarding_member_consumptions=")
                        >= expected_member_consumptions,
                "the app profile must record generated add forwarding work");

            const auto& carry_input = width == 129U
                ? inputs.back() : inputs.front();
            const auto carry_result
                = forwarding_diamond_add_value(carry_input);
            require(carry_result.back() == '0',
                "the add fixture must preserve the low-bit sum");
            if (width == 129U) {
                require(carry_result[width - 1U - 65U] == '1',
                    "the 129-bit add fixture must propagate carry "
                    "across bit 64");
            } else {
                require(carry_result.front() == '1',
                    "the 65-bit add fixture must retain carry through bit 63");
            }
        }
    }
}

void test_o0_and_o2_application_region_forwarding_wide_diamond()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-forwarding-wide-diamond-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
        for (const auto width : { 65U, 129U }) {
            const auto case_name = "width-" + std::to_string(width);
            const auto compiled_root
                = root.path / optimization_name / case_name / "compiled";
            const auto reference_root
                = root.path / optimization_name / case_name / "reference";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(reference_root);

            const auto compiled = run_forwarding_diamond_application_route(
                optimization, compiled_root, SimulationEngine::compiled, width);
            const auto reference = run_forwarding_diamond_application_route(
                optimization, reference_root, SimulationEngine::interpreter,
                width);
            require(compiled.activation_snapshots.size() == 6U
                    && same_signal_snapshots(compiled.activation_snapshots,
                        reference.activation_snapshots),
                "wide O0/O2 native forwarding must match interpreter current, "
                "LAST, stored/raw owner, event and transaction metadata at "
                "every settled window");
            require(compiled.counters.attempts >= 5U
                    && compiled.counters.evaluations >= 5U
                    && compiled.counters.member_consumptions >= 20U
                    && compiled.counters.declines < compiled.counters.attempts,
                "the 65/129-bit parsed diamonds must consume successful native "
                "root, branch, and join work on every high-word transition");
            require(profile_count(compiled.profile,
                        "region_forwarding_evaluations=") >= 5U
                    && profile_count(compiled.profile,
                        "region_forwarding_member_consumptions=") >= 20U,
                "O0/O2 wide app profiles must expose native forwarding work");
        }
    }
}

void test_o0_and_o2_successor_mask_application_route()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-region-successor-mask-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        for (const bool narrow_direct_ready_input : { false, true }) {
            const auto route_name = narrow_direct_ready_input
                ? "narrow-direct-ready" : "wide-non-direct-ready";
            const auto route_root = root.path / optimization_name / route_name;
            const auto compiled_root = route_root / "compiled";
            const auto reference_root = route_root / "reference";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(reference_root);

            const auto compiled = run_successor_mask_route(optimization,
                compiled_root, SimulationEngine::compiled,
                narrow_direct_ready_input);
            const auto reference = run_successor_mask_route(optimization,
                reference_root, SimulationEngine::interpreter,
                narrow_direct_ready_input);
            require(compiled == reference,
                "the real direct-ready and 65-bit successor-mask routes "
                "must match the ordinary interpreter at O0 and O2");
        }
    }
}

} // namespace

int main()
{
    try {
        test_o0_and_o2_native_full_route();
        test_o0_and_o2_wide_a4_app_route();
        test_o0_and_o2_narrow_disjoint_owner_a4_route();
        test_o0_and_o2_local_wave_forwarding_allocation();
        test_o0_and_o2_activation_only_local_wave_allocation();
        test_o0_and_o2_prepared_output_application_route();
        test_o0_and_o2_application_region_forwarding();
        test_o0_and_o2_mixed_internal_external_forwarding();
        test_o0_and_o2_application_region_forwarding_diamond();
        test_o0_and_o2_application_region_forwarding_multioutput_parent();
        test_o0_and_o2_application_region_forwarding_wide_diamond();
        test_o0_and_o2_application_region_forwarding_add_unsigned();
        test_o0_and_o2_wide_forwarding_provider_preentry_failure();
        test_o0_and_o2_application_region_forwarding_unequal_depth_wide();
        test_o0_and_o2_successor_mask_application_route();
        test_o0_and_o2_native_vhdl_projected_route();
        test_o0_and_o2_vhdl_cycle_boundary_contract();
        test_o0_and_o2_native_vhdl_logic9_generic_cycle();
        test_o0_and_o2_native_vhdl_wide_projected_route();
        test_o0_and_o2_native_vhdl_unresolved_logic9_route();
        test_o0_and_o2_native_vhdl_multioutput_projected_owner();
        test_o0_wide_generic_projected_allocation_failure_retry();
        test_o0_and_o2_compiled_vhdl_disjoint_projected_route();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native region allocation test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
