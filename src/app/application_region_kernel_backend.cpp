// SPDX-License-Identifier: Apache-2.0
#include "application_region_kernel_backend.hpp"

#if defined(FSIM_HAS_LLVM)
#include "fsim/compiler/llvm_jit_region_kernel.hpp"
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "../compiler/llvm_jit_region_frontier_private_access.hpp"
#include "../compiler/llvm_jit_region_frontier_test_access.hpp"
#include "../runtime/simir_region_frontier_trusted_entry.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {
#if defined(FSIM_HAS_LLVM)
namespace {

[[nodiscard]] bool region_backend_diagnostics_enabled() noexcept
{
    return std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr;
}

void report_region_backend_event(
    const std::size_t member_count, const std::size_t input_count,
    const std::size_t output_count, const std::string_view variant,
    const std::string_view event, const std::string_view detail = { }) noexcept
{
    if (!region_backend_diagnostics_enabled()) {
        return;
    }
    std::fprintf(stderr,
        "[fsim region-native-create] layer=app-provider variant=%.*s "
        "event=%.*s members=%zu inputs=%zu outputs=%zu",
        static_cast<int>(variant.size()), variant.data(),
        static_cast<int>(event.size()), event.data(), member_count,
        input_count, output_count);
    if (!detail.empty()) {
        std::fprintf(stderr, " detail=%.*s",
            static_cast<int>(detail.size()), detail.data());
    }
    std::fputc('\n', stderr);
}

void report_region_backend_event(
    const runtime::simir::RegionConeActivationKernel& kernel,
    const std::string_view variant, const std::string_view event,
    const std::string_view detail = { }) noexcept
{
    report_region_backend_event(
        kernel.members.size(), kernel.inputs.size(), kernel.outputs.size(),
        variant, event, detail);
}

void append_identity_field(
    std::string& identity,
    const std::string_view name,
    const std::string_view value)
{
    identity.append(name);
    identity.push_back('=');
    identity.append(std::to_string(value.size()));
    identity.push_back(':');
    identity.append(value);
    identity.push_back(';');
}

[[nodiscard]] std::string make_provider_identity(
    const compiler::LlvmJitOptions& options,
    const std::string_view design_identity)
{
    std::string result { "fsim-llvm-region-provider-v4;" };
    append_identity_field(result, "frontier-abi",
        std::to_string(runtime::simir::kRegionFrontierAbiVersionV2));
    append_identity_field(result, "frontier-value-plane-contract",
        std::to_string(
            runtime::simir::kRegionFrontierValuePlaneContractV2));
    append_identity_field(result, "optimization",
        compiler::to_string(options.optimization));
    append_identity_field(result, "debug",
        options.debug_instrumentation ? "1" : "0");
    append_identity_field(result, "direct-slots",
        options.require_direct_update_slots ? "1" : "0");
    append_identity_field(result, "coverage", options.code_coverage_identity);
    append_identity_field(result, "cache-directory",
        options.cache_directory.generic_string());
    append_identity_field(result, "cache-max-bytes",
        options.cache_maximum_bytes
            ? std::to_string(*options.cache_maximum_bytes) : "none");
    append_identity_field(result, "cache-max-entries",
        options.cache_maximum_entries
            ? std::to_string(*options.cache_maximum_entries) : "none");
    append_identity_field(result, "cache-max-age-seconds",
        options.cache_maximum_age
            ? std::to_string(options.cache_maximum_age->count()) : "none");
    append_identity_field(result, "design", design_identity);
    return result;
}

class LlvmRegionKernelBackend final
    : public runtime::simir::RegionKernelBackend
    , public runtime::simir::RegionKernelLogic4InputBackend
    , public runtime::simir::RegionKernelInputPlaneBackend
    , public runtime::simir::RegionKernelDirectReadyWindowBackend
    , public runtime::simir::RegionKernelPreparedOutputSuccessorMaskBackend
    , public runtime::simir::RegionKernelFailureBackend {
public:
    explicit LlvmRegionKernelBackend(
        std::unique_ptr<compiler::LlvmRegionKernelExecutor> executor,
        std::unique_ptr<compiler::LlvmRegionKernelExecutor>
            constant_executor,
        std::vector<std::pair<runtime::simir::RegisterId,
            runtime::PackedLogic4>> constant_inputs)
        : executor_(std::move(executor))
        , constant_executor_(std::move(constant_executor))
        , constant_inputs_(std::move(constant_inputs))
    {
    }

    [[nodiscard]] bool execute(
        const runtime::simir::RegionKernelActivationImage& image) noexcept
        override
    {
        auto& executor = select_executor(image);
        return executor.execute(image);
    }

    [[nodiscard]] bool execute_with_logic4_input_planes(
        const runtime::simir::RegionKernelActivationImage& image,
        const std::span<const runtime::simir::RegionKernelLogic4InputPlane>
            planes) noexcept override
    {
        auto& executor = select_executor(image);
        return executor.execute_with_logic4_input_planes(image, planes);
    }

    [[nodiscard]] bool execute_with_input_planes(
        const runtime::simir::RegionKernelActivationImage& image,
        const std::span<const runtime::simir::RegionKernelInputPlane> planes)
        noexcept override
    {
        auto& executor = select_executor(image);
        return executor.execute_with_input_planes(image, planes);
    }

    [[nodiscard]] bool execute_internal_output_prefix_prepared(
        const runtime::simir::RegionKernelActivationImage& image,
        const std::span<const runtime::PackedLogic4> current_internal_values,
        const std::span<const runtime::simir::RegionConeOutputBinding>
            ordered_prefix,
        runtime::simir::RegionPreparedOutputBatchV1& outputs) noexcept
        override
    {
        auto& executor = select_executor(image);
        return executor.execute_internal_output_prefix_prepared(
            image, current_internal_values, ordered_prefix, outputs);
    }

    [[nodiscard]] bool
    execute_internal_output_prefix_prepared_with_successor_masks(
        const runtime::simir::RegionKernelActivationImage& image,
        const std::span<const runtime::PackedLogic4> current_internal_values,
        const std::span<const runtime::simir::RegionConeOutputBinding>
            ordered_prefix,
        runtime::simir::RegionPreparedOutputBatchV1& outputs,
        runtime::simir::RegionPreparedOutputSuccessorMasksV1& successors)
        noexcept override
    {
        auto& executor = select_executor(image);
        return executor.execute_internal_output_prefix_prepared(
            image, current_internal_values, ordered_prefix, outputs,
            &successors);
    }

    [[nodiscard]] bool supports_direct_ready_window() const noexcept override
    {
        return executor_ != nullptr
            && executor_->supports_direct_ready_window();
    }

    [[nodiscard]] bool
    supports_direct_ready_window_successor_masks() const noexcept override
    {
        return executor_ != nullptr
            && executor_->supports_direct_ready_window_successor_masks();
    }

    [[nodiscard]] bool
    execute_direct_ready_window_prepared_with_successor_masks(
        const runtime::simir::RegionKernelActivationImage& image,
        const runtime::simir::RegionDirectReadyWindowV1& input_window,
        const std::span<const runtime::PackedLogic4> current_internal_values,
        const std::span<const runtime::simir::RegionConeOutputBinding>
            ordered_prefix,
        runtime::simir::RegionPreparedOutputBatchV1& outputs,
        runtime::simir::RegionPreparedOutputSuccessorMasksV1& successors)
        noexcept override
    {
        last_attempted_executor_ = nullptr;
        if (executor_ == nullptr) {
            return false;
        }
        last_constant_variant_ = false;
        last_attempted_executor_ = executor_.get();
        return executor_->execute_direct_ready_window_prepared(image,
            input_window, current_internal_values, ordered_prefix, outputs,
            &successors);
    }

    [[nodiscard]] bool execute_direct_ready_window_prepared(
        const runtime::simir::RegionKernelActivationImage& image,
        const runtime::simir::RegionDirectReadyWindowV1& input_window,
        const std::span<const runtime::PackedLogic4> current_internal_values,
        const std::span<const runtime::simir::RegionConeOutputBinding>
            ordered_prefix,
        runtime::simir::RegionPreparedOutputBatchV1& outputs) noexcept override
    {
        last_attempted_executor_ = nullptr;
        if (executor_ == nullptr) {
            return false;
        }
        last_constant_variant_ = false;
        last_attempted_executor_ = executor_.get();
        return executor_->execute_direct_ready_window_prepared(image,
            input_window, current_internal_values, ordered_prefix, outputs);
    }

    [[nodiscard]] std::exception_ptr take_failure() noexcept override
    {
        auto* const executor = last_attempted_executor_;
        last_attempted_executor_ = nullptr;
        return executor == nullptr ? std::exception_ptr { }
                                : executor->take_failure();
    }

    [[nodiscard]] std::span<const runtime::PackedLogic4>
    activation_registers() const noexcept override
    {
        return (last_constant_variant_
                ? constant_executor_.get() : executor_.get())
            ->activation_registers();
    }

    [[nodiscard]] bool last_call_used_constant_variant() const noexcept
    {
        return last_constant_variant_;
    }

private:
    [[nodiscard]] bool constant_inputs_match(
        const runtime::simir::RegionKernelActivationImage& image) const noexcept
    {
        if (constant_executor_ == nullptr || constant_inputs_.empty()) {
            return false;
        }
        for (const auto& [register_id, expected] : constant_inputs_) {
            const auto actual = std::ranges::find(image.register_inputs,
                register_id,
                &runtime::simir::RegionKernelRegisterInput::register_id);
            if (actual == image.register_inputs.end()
                || actual->value != expected) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] compiler::LlvmRegionKernelExecutor& selected_executor(
        const runtime::simir::RegionKernelActivationImage& image) noexcept
    {
        last_constant_variant_ = constant_inputs_match(image);
        return last_constant_variant_
            ? *constant_executor_ : *executor_;
    }

    [[nodiscard]] compiler::LlvmRegionKernelExecutor& select_executor(
        const runtime::simir::RegionKernelActivationImage& image) noexcept
    {
        auto& executor = selected_executor(image);
        last_attempted_executor_ = std::addressof(executor);
        return executor;
    }

    std::unique_ptr<compiler::LlvmRegionKernelExecutor> executor_;
    std::unique_ptr<compiler::LlvmRegionKernelExecutor> constant_executor_;
    std::vector<std::pair<runtime::simir::RegisterId,
        runtime::PackedLogic4>> constant_inputs_;
    compiler::LlvmRegionKernelExecutor* last_attempted_executor_ { };
    bool last_constant_variant_ { };
};

class LlvmRegionConeForwardingBackend final
    : public runtime::simir::RegionConeForwardingBackend
    , public runtime::simir::RegionConeForwardingFailureBackend {
public:
    LlvmRegionConeForwardingBackend(
        const runtime::simir::RegionConeForwardingKernel& kernel,
        std::unique_ptr<compiler::LlvmRegionKernelExecutor> executor)
        : executor_(std::move(executor))
    {
        if (!executor_ || !forwarding_contract_is_valid(kernel)) {
            throw std::invalid_argument {
                "invalid LLVM region forwarding kernel"
            };
        }

        member_processes_.reserve(kernel.members.size());
        for (const auto& member : kernel.members) {
            member_processes_.push_back(member.process);
        }
        boundary_input_count_ = kernel.execution_kernel.inputs.size();
        boundary_input_widths_.reserve(boundary_input_count_);
        for (const auto& input : kernel.execution_kernel.inputs) {
            boundary_input_widths_.push_back(input.width);
        }
        output_slots_.reserve(kernel.execution_kernel.outputs.size());
        output_scratch_.reserve(kernel.execution_kernel.outputs.size());
        for (const auto& output : kernel.execution_kernel.outputs) {
            output_slots_.push_back({ output.value_register, output.width,
                output.value_kind });
            output_scratch_.emplace_back(output.width, runtime::Logic4::x);
        }

        std::vector<RegisterInputSlot> slots;
        slots.reserve(kernel.execution_kernel.inputs.size()
            + kernel.execution_kernel.members.size());
        for (std::size_t index = 0U;
             index < kernel.execution_kernel.inputs.size(); ++index) {
            const auto& input = kernel.execution_kernel.inputs[index];
            slots.push_back({ input.value_register, input.width, index, false });
        }
        for (const auto& member : kernel.execution_kernel.members) {
            slots.push_back({ member.readiness_register, 1U, 0U, true });
        }
        std::ranges::sort(slots, std::ranges::less { },
            &RegisterInputSlot::register_id);
        register_inputs_.reserve(slots.size());
        image_.register_inputs.reserve(slots.size());
        for (std::size_t index = 0U; index < slots.size(); ++index) {
            const auto& slot = slots[index];
            if (slot.width == 0U
                || (index != 0U
                    && slots[index - 1U].register_id >= slot.register_id)) {
                throw std::invalid_argument {
                    "region forwarding input register map is invalid"
                };
            }
            register_inputs_.push_back({ slot.register_id, slot.width,
                slot.source_index, slot.readiness });
            image_.register_inputs.push_back({ slot.register_id,
                runtime::PackedLogic4 { slot.width, runtime::Logic4::x } });
        }

        image_.ready_processes.reserve(member_processes_.size());
        image_.requests.reserve(member_processes_.size());
        image_.active_member_indices.reserve(member_processes_.size());
        image_.generation = 0U;
        image_.scheduler_prefix.frontier_generation = 0U;
        image_.scheduler_prefix.frontier_cursor = 0U;
        image_.scheduler_prefix.frontier_end = member_processes_.size();
        image_.scheduler_prefix.phase = runtime::SchedulerPhase::active;
        image_.scheduler_prefix.process_domain
            = runtime::simir::ProcessSchedulingDomain::systemverilog;
        image_.scheduler_prefix.tasks.clear();
        image_.ready_processes.clear();
        image_.requests.clear();
        image_.active_member_indices.clear();
        image_.scheduler_prefix.tasks.reserve(member_processes_.size());
    }

    [[nodiscard]] static bool supported(
        const runtime::simir::RegionConeForwardingKernel& kernel)
    {
        return forwarding_contract_is_valid(kernel);
    }

    [[nodiscard]] bool execute_forwarding(
        const runtime::simir::RegionKernelSchedulerPrefix& origin,
        const std::span<const runtime::PackedLogic4> boundary_inputs,
        const std::span<runtime::PackedLogic4> output_values) noexcept override
    {
        InvocationGuard invocation { in_use_ };
        if (!invocation) {
            return false;
        }
        last_failure_ = { };
        if (!origin_is_exact_member_seed_prefix(origin)
            || boundary_inputs.size() != boundary_input_count_
            || output_values.size() != output_slots_.size()
            || generation_ == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }

        for (std::size_t index = 0U; index < boundary_inputs.size(); ++index) {
            const auto& value = boundary_inputs[index];
            if (value.is_logic9()
                || value.width() != boundary_input_widths_[index]) {
                return false;
            }
        }
        for (std::size_t index = 0U; index < output_slots_.size(); ++index) {
            const auto& slot = output_slots_[index];
            const auto& output = output_values[index];
            const auto& scratch = output_scratch_[index];
            if (slot.value_kind != ValueKind::logic4
                || output.width() != slot.width || output.is_logic9()
                || scratch.width() != slot.width || scratch.is_logic9()) {
                return false;
            }
            for (const auto& input : boundary_inputs) {
                if (&input == &output) {
                    return false;
                }
            }
        }

        try {
            // Make the private deep-copy targets unique before entering JIT.
            // Retained publications can keep their prior planes alive; any
            // required COW allocation must happen before native execution.
            for (auto& output : output_scratch_) {
                output.fill(runtime::Logic4::x);
            }

            const auto next_generation = generation_ + 1U;
            if (next_generation == 0U) {
                return false;
            }

            auto& prefix = image_.scheduler_prefix;
            prefix.frontier_generation = next_generation;
            prefix.time = origin.time;
            prefix.delta = origin.delta;
            prefix.systemverilog_round = origin.systemverilog_round;
            prefix.frontier_cursor = 0U;
            prefix.frontier_end = member_processes_.size();
            prefix.tasks.clear();
            image_.ready_processes.clear();
            image_.requests.clear();
            image_.active_member_indices.clear();

            for (std::size_t index = 0U;
                 index < member_processes_.size(); ++index) {
                const auto process = member_processes_[index];
                const runtime::simir::RegionKernelActivationOrigin request_origin {
                    runtime::simir::ProcessSchedulingDomain::systemverilog,
                    runtime::SchedulerPhase::active,
                    origin.time,
                    origin.delta,
                    static_cast<runtime::StableOrder>(index),
                    static_cast<std::uint64_t>(index),
                    origin.systemverilog_round,
                };
                const runtime::simir::RegionKernelReadyMember request {
                    process,
                    runtime::simir::Process::full_static_trigger_mask,
                    request_origin };
                image_.ready_processes.push_back(process);
                image_.requests.push_back(request);
                image_.active_member_indices.push_back(index);
                prefix.tasks.push_back({ index, request });
            }

            for (std::size_t index = 0U;
                 index < register_inputs_.size(); ++index) {
                const auto& slot = register_inputs_[index];
                auto& input = image_.register_inputs[index];
                if (slot.readiness) {
                    input.value.assign_word({ 1U, 1U, 0U });
                } else {
                    if (slot.source_index >= boundary_inputs.size()) {
                        return false;
                    }
                    input.value = boundary_inputs[slot.source_index];
                }
            }

            image_.generation = next_generation;
            if (!executor_->execute(image_)) {
                generation_ = next_generation;
                last_failure_ = executor_->take_failure();
                return false;
            }
            generation_ = next_generation;
            const auto registers = executor_->activation_registers();
            for (const auto& slot : output_slots_) {
                if (slot.register_id >= registers.size()) {
                    return false;
                }
                const auto& value = registers[slot.register_id];
                if (value.width() != slot.width
                    || value.is_logic9()
                    || slot.value_kind != ValueKind::logic4) {
                    return false;
                }
                const auto words = slot.width / 64U
                    + (slot.width % 64U == 0U ? 0U : 1U);
                const auto aval = value.aval_words();
                const auto bval = value.bval_words();
                if (aval.size() != words || bval.size() != words) {
                    return false;
                }
                const auto remainder = slot.width % 64U;
                if (remainder != 0U) {
                    const auto valid_mask
                        = (UINT64_C(1) << remainder) - 1U;
                    if ((aval.back() & ~valid_mask) != 0U
                        || (bval.back() & ~valid_mask) != 0U) {
                        return false;
                    }
                }
            }

            // Deep-copy every export into private pre-shaped storage first.
            // COW detachment was completed by the pre-entry fill above, so
            // any failure here still leaves all caller slots unchanged.
            for (std::size_t index = 0U;
                 index < output_slots_.size(); ++index) {
                const auto& slot = output_slots_[index];
                output_scratch_[index].insert_bits(
                    registers[slot.register_id], 0U);
            }

            // Runtime output slots are unpublished, ordinary owning values,
            // never live A4-bound slots. Their no-throw swaps are the sole
            // commit point for the whole batch.
            static_assert(std::is_nothrow_swappable_v<
                runtime::PackedLogic4>);
            for (std::size_t index = 0U;
                 index < output_slots_.size(); ++index) {
                std::swap(output_scratch_[index], output_values[index]);
            }
            return true;
        } catch (...) {
            last_failure_ = std::current_exception();
            return false;
        }
    }

    [[nodiscard]] std::exception_ptr take_failure() noexcept override
    {
        auto failure = std::move(last_failure_);
        last_failure_ = { };
        return failure;
    }

private:
    using ProcessId = runtime::simir::ProcessId;
    using SignalId = runtime::simir::SignalId;
    using ValueKind = runtime::simir::ValueKind;

    struct RegisterInputSlot {
        runtime::simir::RegisterId register_id { };
        std::uint32_t width { };
        std::size_t source_index { };
        bool readiness { };
    };

    struct OutputSlot {
        runtime::simir::RegisterId register_id { };
        std::uint32_t width { };
        ValueKind value_kind { ValueKind::logic4 };
    };

    class InvocationGuard {
    public:
        explicit InvocationGuard(std::atomic_flag& flag) noexcept
            : flag_(flag)
            , acquired_(!flag_.test_and_set(std::memory_order_acquire))
        {
        }

        ~InvocationGuard()
        {
            if (acquired_) {
                flag_.clear(std::memory_order_release);
            }
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return acquired_;
        }

    private:
        std::atomic_flag& flag_;
        bool acquired_ { };
    };

    [[nodiscard]] static bool forwarding_contract_is_valid(
        const runtime::simir::RegionConeForwardingKernel& kernel)
    {
        const auto& execution = kernel.execution_kernel;
        if (execution.program.scheduling_domain
                != runtime::simir::ProcessSchedulingDomain::systemverilog
            || execution.members.empty()
            || execution.outputs.empty()
            || kernel.internal_signals.empty()
            || execution.members.size() != kernel.members.size()
            || kernel.topological_member_indices.size()
                != kernel.members.size()
            || execution.member_execution_order
                != kernel.topological_member_indices
            || std::ranges::none_of(kernel.members,
                [](const auto& member) {
                    return member.dependency_count == 0U;
                })) {
            return false;
        }
        std::vector<std::uint8_t> seen(kernel.members.size(), 0U);
        ProcessId previous_process { };
        bool have_previous_process { };
        std::vector<std::size_t> topological_position(
            kernel.members.size(), kernel.members.size());
        for (std::size_t position = 0U;
             position < kernel.topological_member_indices.size(); ++position) {
            const auto index = kernel.topological_member_indices[position];
            if (index >= seen.size() || seen[index] != 0U) {
                return false;
            }
            seen[index] = 1U;
            topological_position[index] = position;
        }
        for (std::size_t index = 0U; index < kernel.members.size(); ++index) {
            const auto& member = kernel.members[index];
            if ((have_previous_process && member.process <= previous_process)
                || member.process
                    != execution.members[index].process
                || member.output_begin > execution.outputs.size()
                || member.output_count
                    > execution.outputs.size() - member.output_begin
                || member.dependency_begin > kernel.dependencies.size()
                || member.dependency_count
                    > kernel.dependencies.size() - member.dependency_begin
                || member.read_begin > kernel.internal_reads.size()
                || member.read_count
                    > kernel.internal_reads.size() - member.read_begin
                || (member.dependency_count == 0U
                    ? member.depth != 0U
                    : member.depth == 0U)) {
                return false;
            }
            for (std::size_t output_index = member.output_begin;
                 output_index < member.output_begin + member.output_count;
                 ++output_index) {
                const auto& output = execution.outputs[output_index];
                if (output.owner != member.process || output.width == 0U
                    || output.value_kind != ValueKind::logic4
                    || output.offset != 0U) {
                    return false;
                }
            }
            previous_process = member.process;
            have_previous_process = true;
        }
        if (std::ranges::any_of(kernel.dependencies,
                [](const auto& dependency) {
                    return dependency.edge != runtime::simir::EdgeKind::any;
                })
            || std::ranges::any_of(execution.inputs,
                [](const auto& input) {
                    return input.internal || input.width == 0U
                        || input.value_kind != ValueKind::logic4;
                })
            || std::ranges::any_of(execution.outputs,
                [](const auto& output) {
                    return output.width == 0U
                        || output.value_kind != ValueKind::logic4
                        || output.offset != 0U;
                })
            || !std::ranges::is_sorted(kernel.internal_signals)
            || std::ranges::adjacent_find(kernel.internal_signals)
                != kernel.internal_signals.end()) {
            return false;
        }
        for (const auto& dependency : kernel.dependencies) {
            if (!std::ranges::binary_search(
                    kernel.internal_signals, dependency.signal)) {
                return false;
            }
        }
        SignalId previous_input { };
        bool have_previous_input { };
        for (const auto& input : execution.inputs) {
            if ((have_previous_input && input.signal <= previous_input)
                || std::ranges::binary_search(
                    kernel.internal_signals, input.signal)) {
                return false;
            }
            previous_input = input.signal;
            have_previous_input = true;
        }
        std::size_t next_output { };
        for (const auto member_index : kernel.topological_member_indices) {
            const auto& member = kernel.members[member_index];
            if (member.output_begin != next_output) {
                return false;
            }
            for (std::size_t output_index = member.output_begin;
                 output_index < member.output_begin + member.output_count;
                 ++output_index) {
                if (execution.outputs[output_index].owner != member.process) {
                    return false;
                }
            }
            next_output += member.output_count;
        }
        if (next_output != execution.outputs.size()) {
            return false;
        }
        const auto output_for_writer = [&](const std::size_t writer_index,
                                           const SignalId signal)
            -> const runtime::simir::RegionConeOutputBinding* {
            if (writer_index >= kernel.members.size()) {
                return nullptr;
            }
            const auto& writer = kernel.members[writer_index];
            if (writer.output_begin > execution.outputs.size()
                || writer.output_count
                    > execution.outputs.size() - writer.output_begin) {
                return nullptr;
            }
            const runtime::simir::RegionConeOutputBinding* result { };
            for (std::size_t output_index = writer.output_begin;
                 output_index < writer.output_begin + writer.output_count;
                 ++output_index) {
                const auto& output = execution.outputs[output_index];
                if (output.signal != signal) {
                    continue;
                }
                if (result != nullptr) {
                    return nullptr;
                }
                result = &output;
            }
            return result;
        };
        for (std::size_t member_index = 0U;
             member_index < kernel.members.size(); ++member_index) {
            const auto& member = kernel.members[member_index];
            std::uint32_t expected_depth { };
            for (std::size_t dependency_index = member.dependency_begin;
                 dependency_index
                    < member.dependency_begin + member.dependency_count;
                 ++dependency_index) {
                const auto& dependency
                    = kernel.dependencies[dependency_index];
                if (dependency.edge != runtime::simir::EdgeKind::any
                    || !std::ranges::binary_search(
                        kernel.internal_signals, dependency.signal)
                    || dependency.writer_member_index >= kernel.members.size()
                    || topological_position[dependency.writer_member_index]
                        >= topological_position[member_index]
                    || kernel.members[dependency.writer_member_index].depth
                        == std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                expected_depth = std::max(expected_depth,
                    kernel.members[dependency.writer_member_index].depth + 1U);
                const auto* const parent_output = output_for_writer(
                    dependency.writer_member_index, dependency.signal);
                if (parent_output == nullptr || parent_output->offset != 0U
                    || parent_output->value_kind != ValueKind::logic4
                    || (dependency.width == 0U
                        ? dependency.offset != 0U
                        : dependency.offset >= parent_output->width
                            || dependency.width
                                > parent_output->width - dependency.offset)) {
                    return false;
                }
            }
            if (member.dependency_count == 0U) {
                if (member.depth != 0U || member.read_count != 0U) {
                    return false;
                }
                continue;
            }
            if (member.depth != expected_depth) {
                return false;
            }
            for (std::size_t read_index = member.read_begin;
                 read_index < member.read_begin + member.read_count;
                 ++read_index) {
                const auto& read = kernel.internal_reads[read_index];
                if (!std::ranges::binary_search(
                        kernel.internal_signals, read.signal)
                    || read.writer_member_index >= kernel.members.size()
                    || topological_position[read.writer_member_index]
                        >= topological_position[member_index]
                    || output_for_writer(read.writer_member_index, read.signal)
                        == nullptr
                    || std::ranges::none_of(
                        std::span<const runtime::simir::RegionConeForwardingDependency> {
                            kernel.dependencies }
                            .subspan(member.dependency_begin,
                                member.dependency_count),
                        [&](const auto& dependency) {
                            return dependency.signal == read.signal
                                && dependency.writer_member_index
                                    == read.writer_member_index;
                        })) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool origin_is_exact_member_seed_prefix(
        const runtime::simir::RegionKernelSchedulerPrefix& origin) const noexcept
    {
        if (origin.frontier_generation == 0U
            || origin.phase != runtime::SchedulerPhase::active
            || origin.process_domain
                != runtime::simir::ProcessSchedulingDomain::systemverilog
            || origin.frontier_cursor >= origin.frontier_end
            || origin.tasks.empty()
            || origin.tasks.size() > member_processes_.size()
            || origin.tasks.size() > origin.frontier_end - origin.frontier_cursor) {
            return false;
        }
        std::optional<std::tuple<runtime::StableOrder, std::uint64_t>>
            previous_key;
        for (std::size_t index = 0U; index < origin.tasks.size(); ++index) {
            const auto& task = origin.tasks[index];
            const auto& member = task.member;
            if (task.task_ordinal != origin.frontier_cursor + index
                || member.origin.process_domain != origin.process_domain
                || member.origin.phase != origin.phase
                || member.origin.time != origin.time
                || member.origin.delta != origin.delta
                || member.origin.systemverilog_round
                    != origin.systemverilog_round
                || member.trigger_mask == 0U
                || !std::ranges::binary_search(
                    member_processes_, member.process)) {
                return false;
            }
            const auto key = std::tuple {
                member.origin.stable_order, member.origin.sequence };
            if (previous_key && !(*previous_key < key)) {
                return false;
            }
            previous_key = key;
            for (std::size_t prior = 0U; prior < index; ++prior) {
                if (origin.tasks[prior].member.process == member.process) {
                    return false;
                }
            }
        }
        return true;
    }

    std::unique_ptr<compiler::LlvmRegionKernelExecutor> executor_;
    std::vector<ProcessId> member_processes_;
    std::vector<std::uint32_t> boundary_input_widths_;
    std::vector<RegisterInputSlot> register_inputs_;
    std::vector<OutputSlot> output_slots_;
    std::vector<runtime::PackedLogic4> output_scratch_;
    std::size_t boundary_input_count_ { };
    runtime::simir::RegionKernelActivationImage image_;
    std::uint64_t generation_ { };
    std::exception_ptr last_failure_;
    std::atomic_flag in_use_ = ATOMIC_FLAG_INIT;
};

class LlvmRegionFrontierBackend final
    : public runtime::simir::RegionFrontierBackend,
      public runtime::simir::detail::RegionFrontierTrustedEntryCapability {
public:
    explicit LlvmRegionFrontierBackend(
        std::unique_ptr<compiler::LlvmRegionFrontierExecutor> executor)
        : executor_(std::move(executor))
    {
        if (executor_ == nullptr || executor_->step_entry() == nullptr) {
            throw std::invalid_argument {
                "invalid LLVM region frontier executor"
            };
        }
    }

    [[nodiscard]] runtime::simir::RegionFrontierStepEntryV2
    step_entry() const noexcept override
    {
        return executor_->step_entry();
    }

    [[nodiscard]] const runtime::simir::RegionFrontierLayoutV2&
    layout() const noexcept override
    {
        return executor_->layout();
    }

    [[nodiscard]] runtime::simir::detail::RegionFrontierTrustedEntryView
    trusted_entry() const noexcept override
    {
        using compiler::llvm_detail::RegionFrontierPrivateAccess;
        return {
            .entry = RegionFrontierPrivateAccess::trusted_entry(*executor_),
            .layout = &executor_->layout(),
        };
    }

    [[nodiscard]] RegionFrontierBackendTestingSnapshot
    testing_snapshot() const noexcept
    {
        using compiler::llvm_detail::RegionFrontierTestAccess;
        return {
            .shared_body_owner_token
                = RegionFrontierTestAccess::shared_body_owner_token(*executor_),
            .shared_body_address
                = RegionFrontierTestAccess::shared_body_address(*executor_),
            .wrapper_entry = executor_->step_entry(),
            .shared_body_registry_reused
                = RegionFrontierTestAccess::body_registry_reused(*executor_),
            .wrapper_registry_reused
                = RegionFrontierTestAccess::wrapper_registry_reused(*executor_),
            .body_object_cache_statistics
                = RegionFrontierTestAccess::body_cache_statistics(*executor_),
            .wrapper_object_cache_statistics
                = RegionFrontierTestAccess::wrapper_cache_statistics(*executor_),
        };
    }

private:
    // The entry pointer and layout arrays are owned by this executor. The
    // runtime retains this backend for the component frame's full lifetime.
    std::unique_ptr<compiler::LlvmRegionFrontierExecutor> executor_;
};

class LlvmRegionFrontierPreparedBackend final
    : public runtime::simir::RegionFrontierPreparedBackend {
public:
    LlvmRegionFrontierPreparedBackend(
        std::unique_ptr<compiler::LlvmPreparedRegionFrontier> prepared,
        const std::size_t member_count, const std::size_t input_count,
        const std::size_t output_count)
        : prepared_(std::move(prepared))
        , member_count_(member_count)
        , input_count_(input_count)
        , output_count_(output_count)
    {
        if (prepared_ == nullptr) {
            throw std::invalid_argument {
                "invalid prepared LLVM region frontier"
            };
        }
    }

    [[nodiscard]] const runtime::simir::RegionFrontierLayoutV2&
    layout() const noexcept override
    {
        static const runtime::simir::RegionFrontierLayoutV2 empty_layout { };
        return prepared_ != nullptr ? prepared_->layout() : empty_layout;
    }

    [[nodiscard]] std::optional<std::string_view>
    structural_census_identity() const noexcept override
    {
        return prepared_ != nullptr
            ? prepared_->structural_census_identity()
            : std::nullopt;
    }

    [[nodiscard]] std::unique_ptr<runtime::simir::RegionFrontierBackend>
    materialize() && override
    {
        auto prepared = std::move(prepared_);
        if (prepared == nullptr) {
            return { };
        }

        std::unique_ptr<compiler::LlvmRegionFrontierExecutor> executor;
        try {
            executor = compiler::LlvmRegionFrontierExecutor::materialize(
                std::move(*prepared));
        } catch (const std::exception& error) {
            report_region_backend_event(member_count_, input_count_,
                output_count_, "frontier", "exception", error.what());
            throw;
        } catch (...) {
            report_region_backend_event(member_count_, input_count_,
                output_count_, "frontier", "exception",
                "non-standard exception");
            throw;
        }
        if (executor == nullptr) {
            report_region_backend_event(member_count_, input_count_,
                output_count_, "frontier", "null-return");
            return { };
        }
        return std::make_unique<LlvmRegionFrontierBackend>(
            std::move(executor));
    }

private:
    // This object owns the complete compiler plan and its options. Only
    // aggregate counts are retained for deferred materialization reporting.
    std::unique_ptr<compiler::LlvmPreparedRegionFrontier> prepared_;
    std::size_t member_count_ { };
    std::size_t input_count_ { };
    std::size_t output_count_ { };
};

class LlvmRegionKernelBackendProvider final
    : public runtime::simir::RegionKernelBackendProvider
    , public runtime::simir::RegionConeForwardingBackendProvider
    , public runtime::simir::RegionFrontierBackendProvider
    , public runtime::simir::RegionFrontierPreparingProvider {
public:
    LlvmRegionKernelBackendProvider(
        compiler::LlvmJitOptions options,
        std::string design_identity)
        : options_(std::move(options))
        , design_identity_(std::move(design_identity))
        , identity_(make_provider_identity(options_, design_identity_))
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return identity_;
    }

    [[nodiscard]] std::unique_ptr<runtime::simir::RegionKernelBackend>
    create(const runtime::simir::RegionConeActivationKernel& kernel) override
    {
        // The V2 frontier and the ordered process cohort are the supported
        // SystemVerilog Active routes. Do not construct a second, eager
        // activation module for this domain when a frontier is declined.
        if (kernel.program.scheduling_domain
                == runtime::simir::ProcessSchedulingDomain::systemverilog
            && !kernel.outputs.empty()
            && std::ranges::all_of(kernel.outputs,
                [](const runtime::simir::RegionConeOutputBinding& output) {
                    return output.domain
                            == runtime::simir::SignalUpdateDomain::
                                systemverilog_active
                        && output.update_kind
                            == runtime::simir::RegionUpdateKind::
                                systemverilog_active;
                })) {
            return { };
        }
        std::unique_ptr<compiler::LlvmRegionKernelExecutor> executor;
        try {
            executor = compiler::LlvmRegionKernelExecutor::try_create(
                kernel, options_, design_identity_);
        } catch (const std::exception& error) {
            report_region_backend_event(kernel, "dynamic", "exception",
                error.what());
            throw;
        } catch (...) {
            report_region_backend_event(kernel, "dynamic", "exception",
                "non-standard exception");
            throw;
        }
        if (!executor) {
            report_region_backend_event(kernel, "dynamic", "null-return");
            return { };
        }
        std::unique_ptr<compiler::LlvmRegionKernelExecutor>
            constant_executor;
        std::vector<std::pair<runtime::simir::RegisterId,
            runtime::PackedLogic4>> constant_inputs;
        if (!kernel.constant_inputs.empty()) {
            bool valid_bindings = true;
            constant_inputs.reserve(kernel.constant_inputs.size());
            for (const auto& constant : kernel.constant_inputs) {
                const auto input = std::ranges::find(kernel.inputs,
                    constant.signal,
                    &runtime::simir::RegionConeKernelInput::signal);
                if (input == kernel.inputs.end() || input->internal
                    || input->width != constant.width
                    || input->value_kind != constant.value_kind) {
                    valid_bindings = false;
                    break;
                }
                constant_inputs.emplace_back(
                    input->value_register, constant.value);
            }
            if (valid_bindings) {
                try {
                    constant_executor
                        = compiler::LlvmRegionKernelExecutor::try_create(
                            kernel, options_, design_identity_,
                            compiler::RegionKernelSpecialization::
                                guarded_constant_inputs);
                } catch (const std::exception& error) {
                    report_region_backend_event(kernel, "constant",
                        "exception", error.what());
                    throw;
                } catch (...) {
                    report_region_backend_event(kernel, "constant",
                        "exception", "non-standard exception");
                    throw;
                }
            } else {
                report_region_backend_event(kernel, "constant",
                    "not-attempted", "input binding check failed");
                constant_inputs.clear();
            }
            if (!constant_executor) {
                report_region_backend_event(kernel, "constant", "null-return");
                constant_inputs.clear();
            }
        }
        return std::make_unique<LlvmRegionKernelBackend>(
            std::move(executor), std::move(constant_executor),
            std::move(constant_inputs));
    }

    [[nodiscard]] std::unique_ptr<runtime::simir::RegionConeForwardingBackend>
    create_forwarding(
        const runtime::simir::RegionConeForwardingKernel& kernel) override
    {
        if (!LlvmRegionConeForwardingBackend::supported(kernel)) {
            return { };
        }
        auto executor = compiler::LlvmRegionKernelExecutor::try_create(
            kernel.execution_kernel, options_, design_identity_);
        if (!executor) {
            return { };
        }
        return std::make_unique<LlvmRegionConeForwardingBackend>(
            kernel, std::move(executor));
    }

    [[nodiscard]] std::unique_ptr<runtime::simir::RegionFrontierBackend>
    create_frontier(
        const runtime::simir::RegionConeActivationKernel& kernel) override
    {
        auto prepared = prepare_frontier(kernel);
        if (prepared == nullptr) {
            return { };
        }
        return std::move(*prepared).materialize();
    }

    [[nodiscard]] std::unique_ptr<
        runtime::simir::RegionFrontierPreparedBackend>
    prepare_frontier(
        const runtime::simir::RegionConeActivationKernel& kernel) override
    {
        std::unique_ptr<compiler::LlvmPreparedRegionFrontier> prepared;
        try {
            prepared = compiler::LlvmRegionFrontierExecutor::prepare(
                kernel, options_, design_identity_);
        } catch (const std::exception& error) {
            report_region_backend_event(kernel, "frontier", "exception",
                error.what());
            throw;
        } catch (...) {
            report_region_backend_event(kernel, "frontier", "exception",
                "non-standard exception");
            throw;
        }
        if (prepared == nullptr) {
            report_region_backend_event(kernel, "frontier", "null-return");
            return { };
        }
        return std::make_unique<LlvmRegionFrontierPreparedBackend>(
            std::move(prepared), kernel.members.size(),
            kernel.inputs.size(), kernel.outputs.size());
    }

private:
    compiler::LlvmJitOptions options_;
    std::string design_identity_;
    std::string identity_;
};

} // namespace
#endif

std::optional<bool> last_region_constant_variant_for_testing(
    const runtime::simir::RegionKernelBackend& backend) noexcept
{
#if defined(FSIM_HAS_LLVM)
    const auto* const llvm_backend
        = dynamic_cast<const LlvmRegionKernelBackend*>(&backend);
    if (llvm_backend != nullptr) {
        return llvm_backend->last_call_used_constant_variant();
    }
#else
    static_cast<void>(backend);
#endif
    return std::nullopt;
}

std::optional<RegionFrontierBackendTestingSnapshot>
region_frontier_backend_testing_snapshot(
    const runtime::simir::RegionFrontierBackend& backend) noexcept
{
#if defined(FSIM_HAS_LLVM)
    const auto* const llvm_backend
        = dynamic_cast<const LlvmRegionFrontierBackend*>(&backend);
    if (llvm_backend == nullptr) {
        return std::nullopt;
    }
    return llvm_backend->testing_snapshot();
#else
    static_cast<void>(backend);
    return std::nullopt;
#endif
}

std::shared_ptr<runtime::simir::RegionKernelBackendProvider>
make_llvm_region_kernel_backend_provider(
    compiler::LlvmJitOptions options,
    const std::string_view immutable_design_identity)
{
#if defined(FSIM_HAS_LLVM)
    return std::make_shared<LlvmRegionKernelBackendProvider>(
        std::move(options), std::string { immutable_design_identity });
#else
    static_cast<void>(options);
    static_cast<void>(immutable_design_identity);
    return { };
#endif
}

} // namespace fsim::app::application_detail
