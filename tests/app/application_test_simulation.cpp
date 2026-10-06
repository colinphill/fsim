// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "../../src/app/application_internal.hpp"
#include "../../src/app/application_jit_recurrence.hpp"
#include "../../src/app/application_region_kernel_backend.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../../src/runtime/simir_region_activation_reference.hpp"

#include "fsim/compiler/llvm_jit_region_kernel.hpp"
#include "fsim/semantic/compiled_design_normalization.hpp"
#include "fsim/runtime/simir_region_activation.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "fsim/systemc/hierarchy.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
    [[nodiscard]] static std::size_t public_program_facade_count(
        const fsim::app::Simulation& simulation) noexcept
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        const auto& interpreter = *application.interpreter;
        std::size_t count { };
        for (std::size_t id = 0U;
             id < interpreter.impl_->processes.size(); ++id) {
            if (interpreter.impl_->processes.public_facade_materialized(
                    static_cast<ProcessId>(id))) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] static bool has_async_compilation_job(
        const fsim::app::Simulation& simulation) noexcept
    {
#if defined(FSIM_HAS_LLVM)
        return simulation.impl_->jit_materialization.valid()
            && !simulation.impl_->jit_compilations.empty();
#else
        static_cast<void>(simulation);
        return false;
#endif
    }

    [[nodiscard]] static std::size_t installed_executor_count(
        const fsim::app::Simulation& simulation) noexcept
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        const auto& interpreter = *application.interpreter;
        std::size_t count { };
        for (const auto& process : interpreter.impl_->processes) {
            if (process.executor != nullptr) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] static bool instances_share_template(
        const fsim::app::Simulation& simulation,
        const ProcessId first,
        const ProcessId second)
    {
        const auto& interpreter = *simulation.impl_->interpreter;
        const auto first_view = InterpreterProgramAccess::view(
            interpreter, first);
        const auto second_view = InterpreterProgramAccess::view(
            interpreter, second);
        return first_view.valid() && second_view.valid()
            && first_view.id() != second_view.id()
            && first_view.common_identity() != nullptr
            && first_view.common_identity()
                == second_view.common_identity();
    }
};

} // namespace fsim::runtime::simir

namespace fsim::test {

#if defined(FSIM_HAS_LLVM)
namespace {

class MalformedLogic9Context
    : public fsim::runtime::simir::ProcessExecutionContext {
public:
    [[nodiscard]] fsim::runtime::PackedLogic4 read_signal(
        fsim::runtime::simir::SignalId) const override
    {
        return fsim::runtime::PackedLogic4(1U, fsim::runtime::Logic4::zero);
    }

    [[nodiscard]] fsim::runtime::Logic9Word read_signal_logic9_word(
        fsim::runtime::simir::SignalId) const override
    {
        return { 1U, { 1U, 0U, 0U, 1U } };
    }

    void write_blocking(
        fsim::runtime::simir::SignalId signal,
        fsim::runtime::PackedLogic4 value) override
    {
        if (signal == 1U) {
            output = std::move(value);
        }
    }

    void write_blocking_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override
    {
    }

    void write_update(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override
    {
    }

    void write_update_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override
    {
    }

    void write_after(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        fsim::runtime::SimulationTick) override
    {
    }

    void write_after_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        fsim::runtime::SimulationTick) override
    {
    }

    void write_inertial(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        const fsim::runtime::simir::TransitionDelays&) override
    {
    }

    void write_inertial_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        const fsim::runtime::simir::TransitionDelays&) override
    {
    }

    fsim::runtime::PackedLogic4 output {
        1U, fsim::runtime::Logic4::zero
    };
};

class CohortWordUpdateContext final : public MalformedLogic9Context {
public:
    explicit CohortWordUpdateContext(
        const std::span<const std::uint32_t> signal_widths)
        : signal_widths_(signal_widths.begin(), signal_widths.end())
    {
        values_.reserve(signal_widths_.size());
        word_update_counts_.resize(signal_widths_.size());
        slot_update_counts_.resize(signal_widths_.size());
        projected_write_counts_.resize(signal_widths_.size());
        for (const auto width : signal_widths_) {
            values_.emplace_back(width, fsim::runtime::Logic4::x);
        }
    }

    void write_projected(
        const fsim::runtime::simir::SignalId signal,
        fsim::runtime::PackedLogic4 value,
        const fsim::runtime::SimulationTick delay,
        const fsim::runtime::SimulationTick rejection,
        const fsim::runtime::simir::ProjectedDelayMode mode) override
    {
        if (signal >= values_.size()
            || value.width() != signal_widths_[signal]
            || delay != 0U || rejection != 0U
            || mode != fsim::runtime::simir::ProjectedDelayMode::inertial) {
            throw std::runtime_error("invalid test projected write");
        }
        values_[signal] = std::move(value);
        ++projected_write_counts_[signal];
    }

    [[nodiscard]] bool supports_direct_word_updates() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* direct_update_domain() const noexcept override
    {
        return this;
    }

    void write_validated_update_words(
        const std::span<const fsim::runtime::simir::ProcessUpdateWord> updates)
        override
    {
        ++word_batch_calls_;
        word_batch_sizes_.push_back(updates.size());
        for (const auto& update : updates) {
            if (update.signal >= values_.size()
                || update.value.width == 0U
                || update.offset > signal_widths_[update.signal]
                || update.value.width
                    > signal_widths_[update.signal] - update.offset) {
                throw std::runtime_error("invalid test update-word batch");
            }
            const auto value = fsim::runtime::PackedLogic4::from_aval_bval(
                update.value.width, update.value.aval, update.value.bval);
            for (std::size_t bit = 0U; bit < update.value.width; ++bit) {
                values_[update.signal].set(update.offset + bit, value.get(bit));
            }
            ++word_update_counts_[update.signal];
        }
    }

    bool write_validated_update_slot_batches(
        const std::span<const fsim::runtime::simir::ProcessUpdateSlotBatch>
            batches) override
    {
        ++slot_batch_calls_;
        slot_batch_members_ += batches.size();
        if (!accept_slot_batches_) {
            return false;
        }
        for (const auto& batch : batches) {
            for (const auto& slot : batch.slots) {
                if (slot.active == nullptr || *slot.active == 0U) {
                    continue;
                }
                if (slot.signal >= values_.size()
                    || slot.width != signal_widths_[slot.signal]) {
                    throw std::runtime_error("invalid test update-slot batch");
                }
                for (std::uint32_t bit = 0U; bit < slot.width; ++bit) {
                    const auto word = bit / 64U;
                    const auto shift = bit % 64U;
                    if (((slot.mask[word] >> shift) & UINT64_C(1)) == 0U) {
                        continue;
                    }
                    const auto bit_value
                        = fsim::runtime::PackedLogic4::from_aval_bval(
                            1U,
                            (slot.aval[word] >> shift) & UINT64_C(1),
                            (slot.bval[word] >> shift) & UINT64_C(1));
                    values_[slot.signal].set(bit, bit_value.get(0U));
                }
                ++slot_update_counts_[slot.signal];
                slot_publications_.push_back({ batch.process, slot.signal });
                *slot.active = 0U;
                std::fill_n(slot.mask, slot.word_count, UINT64_C(0));
            }
            std::ranges::fill(batch.active_words, UINT64_C(0));
        }
        return true;
    }

    void accept_slot_batches(const bool accept) noexcept
    {
        accept_slot_batches_ = accept;
    }

    [[nodiscard]] const fsim::runtime::PackedLogic4& value(
        const fsim::runtime::simir::SignalId signal) const
    {
        return values_.at(signal);
    }

    [[nodiscard]] std::size_t word_batch_calls() const noexcept
    {
        return word_batch_calls_;
    }

    [[nodiscard]] std::span<const std::size_t>
    word_batch_sizes() const noexcept
    {
        return word_batch_sizes_;
    }

    [[nodiscard]] std::size_t word_update_count(
        const fsim::runtime::simir::SignalId signal) const
    {
        return word_update_counts_.at(signal);
    }

    [[nodiscard]] std::size_t projected_write_count(
        const fsim::runtime::simir::SignalId signal) const
    {
        return projected_write_counts_.at(signal);
    }

    [[nodiscard]] std::size_t slot_batch_calls() const noexcept
    {
        return slot_batch_calls_;
    }

    [[nodiscard]] std::size_t slot_batch_members() const noexcept
    {
        return slot_batch_members_;
    }

    [[nodiscard]] std::size_t slot_update_count(
        const fsim::runtime::simir::SignalId signal) const
    {
        return slot_update_counts_.at(signal);
    }

    [[nodiscard]] std::size_t slot_publication_count() const noexcept
    {
        return slot_publications_.size();
    }

    [[nodiscard]] bool slot_publication_seen(
        const fsim::runtime::simir::ProcessId process,
        const fsim::runtime::simir::SignalId signal) const
    {
        return std::ranges::find(
            slot_publications_, std::pair { process, signal })
            != slot_publications_.end();
    }

private:
    std::vector<std::uint32_t> signal_widths_;
    std::vector<fsim::runtime::PackedLogic4> values_;
    std::vector<std::size_t> word_update_counts_;
    std::vector<std::size_t> slot_update_counts_;
    std::vector<std::size_t> projected_write_counts_;
    std::vector<std::size_t> word_batch_sizes_;
    std::vector<std::pair<fsim::runtime::simir::ProcessId,
        fsim::runtime::simir::SignalId>> slot_publications_;
    std::size_t word_batch_calls_ { };
    std::size_t slot_batch_calls_ { };
    std::size_t slot_batch_members_ { };
    bool accept_slot_batches_ { true };
};

void test_region_register_copyback_preflight()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    using fsim::app::application_detail::LlvmProcessExecutor;
    using fsim::compiler::JitOptimizationLevel;
    using fsim::compiler::JitProcessHandle;
    using fsim::compiler::LlvmJit;
    using fsim::compiler::LlvmJitOptions;

    const std::array<std::uint32_t, 2U> signal_widths { 1U, 1U };
    const std::array<ValueKind, 2U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4
    };
    const std::array<ResolutionKind, 2U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none
    };
    const std::array<ProcessExecutor::RegionRegisterBinding, 4U> bindings {{
        { 0U, 0U, true, 4U, ValueKind::logic4 },
        { 1U, 1U, true, 2U, ValueKind::logic9 },
        { 2U, 2U, true, 129U, ValueKind::logic4 },
        { 3U, 3U, true, 129U, ValueKind::logic4 },
    }};
    auto original_wide = std::string(129U, '0');
    original_wide[0U] = '1';
    original_wide[64U] = 'X';
    original_wide[128U] = 'Z';
    auto activation_wide = std::string(129U, '1');
    activation_wide[0U] = 'Z';
    activation_wide[64U] = 'X';
    activation_wide[128U] = '0';
    const std::array<PackedLogic4, 4U> activation_registers {{
        PackedLogic4::from_msb_string("0110"),
        PackedLogic4::from_logic9_msb_string("UZ"),
        PackedLogic4::from_msb_string(activation_wide),
        PackedLogic4::from_msb_string(activation_wide),
    }};
    const std::array<PackedLogic4, 4U> malformed_registers {{
        PackedLogic4::from_msb_string("110"),
        PackedLogic4::from_logic9_msb_string("UZ"),
        PackedLogic4::from_msb_string(activation_wide),
        PackedLogic4::from_msb_string("0110"),
    }};

    Process process;
    process.id = 0U;
    process.name = "region_register_copyback";
    process.register_count = 4U;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9,
        ValueKind::logic4, ValueKind::logic4
    };
    process.static_sensitivity = { { 0U, EdgeKind::any } };
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1010") },
        LoadConstant { 1U, PackedLogic4::from_logic9_msb_string("XZ") },
        LoadConstant { 2U, PackedLogic4::from_msb_string(original_wide) },
        CopyRegister { 3U, 2U },
        WaitSensitivity { },
        Jump { 0U },
    };

    for (const auto optimization : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        for (const bool tracks_initialization : { false, true }) {
            auto generated_process = process;
            generated_process.id = 77U;
            generated_process.name = process.name + "_generated";
            LlvmJitOptions options { optimization, { } };
            options.debug_instrumentation = tracks_initialization;
            LlvmJit jit { options };
            assert(jit.supports_process(
                generated_process, signal_widths, signal_kinds));
            jit.add_process(generated_process.name, generated_process,
                signal_widths, signal_kinds);
            const auto handle = jit.lookup(generated_process.name);
            assert(handle);
            assert(jit.frame_layout(handle).tracks_register_initialization
                == tracks_initialization);
            const auto& register_persistence
                = jit.frame_layout(handle).register_values_persistent;
            assert(register_persistence.size() == process.register_count);
            assert(std::ranges::all_of(register_persistence,
                [tracks_initialization](const std::uint8_t persistent) {
                    return persistent
                        == static_cast<std::uint8_t>(tracks_initialization);
                }));

            const auto remap
                = std::make_shared<const ProcessSignalRemap>(
                    ProcessSignalRemap { { 0U, 1U } });
            LlvmProcessExecutor executor {
                jit, handle, process, signal_widths, signal_kinds,
                signal_resolutions, remap, ProcessId { 77U }, nullptr,
                &generated_process
            };
            assert(executor.program_access_binding()->valid());
            assert(executor.program_access_binding()
                ->matches_registered_program(0U, process));
            MalformedLogic9Context context;
            const auto read = [&](const RegisterId id) {
                return executor.read_register(
                    id, ProcessExecutor::native_register_width);
            };
            const auto initial_snapshots = std::array {
                executor.snapshot_register(0U),
                executor.snapshot_register(1U),
                executor.snapshot_register(2U),
                executor.snapshot_register(3U),
            };
            const auto initial_snapshot_strings = std::array {
                initial_snapshots[0].to_msb_string(),
                initial_snapshots[1].to_msb_string(),
                initial_snapshots[2].to_msb_string(),
                initial_snapshots[3].to_msb_string(),
            };
            std::array<std::string, 4U> initial_read_strings;
            if (!tracks_initialization) {
                for (std::size_t index = 0U;
                     index < initial_read_strings.size(); ++index) {
                    initial_read_strings[index]
                        = read(static_cast<RegisterId>(index)).to_msb_string();
                }
            }
            auto mismatched_generated_process = generated_process;
            mismatched_generated_process.id = 78U;
            LlvmProcessExecutor mismatched_executor {
                jit, handle, process, signal_widths, signal_kinds,
                signal_resolutions, remap, ProcessId { 77U }, nullptr,
                &mismatched_generated_process
            };
            assert(!mismatched_executor.program_access_binding()->valid());
            (void)mismatched_executor.resume(context, 0U);
            assert(!mismatched_executor.prepare_region_completion(
                0U, 4U, 5U, bindings, activation_registers));

            const auto suspension = executor.resume(context, 0U);
            assert(suspension.instruction == 4U);
            assert(suspension.next_instruction == 5U);
            const auto original_strings = std::array {
                std::string { "1010" }, std::string { "XZ" },
                original_wide, original_wide
            };
            const auto activation_strings = std::array {
                std::string { "0110" }, std::string { "UZ" },
                activation_wide, activation_wide
            };
            const auto expected_after_completion = [&](const std::size_t index) {
                return tracks_initialization
                    ? activation_strings[index]
                    : initial_read_strings[index];
            };
            const auto expected_snapshot_after_completion =
                [&](const std::size_t index) {
                    return tracks_initialization
                        ? activation_strings[index]
                        : initial_snapshot_strings[index];
                };
            for (std::size_t index = 0U; index < bindings.size(); ++index) {
                const auto expected = tracks_initialization
                    ? original_strings[index]
                    : initial_read_strings[index];
                assert(read(static_cast<RegisterId>(index)).to_msb_string()
                    == expected);
            }
            assert(!executor.prepare_region_completion(
                0U, 3U, 5U, bindings, activation_registers));
            assert(!executor.prepare_region_completion(
                1U, 4U, 5U, bindings, activation_registers));
            assert(!executor.prepare_region_completion(
                0U, 4U, 5U, bindings, malformed_registers));
            assert(read(0U).to_msb_string()
                == (tracks_initialization
                        ? original_strings[0U]
                        : initial_read_strings[0U]));

            {
                auto canceled = executor.prepare_region_completion(
                    0U, 4U, 5U, bindings, activation_registers);
                assert(canceled);
                assert(canceled->storage_identity() != nullptr);
            }
            for (std::size_t index = 0U; index < bindings.size(); ++index) {
                const auto expected = tracks_initialization
                    ? original_strings[index]
                    : initial_read_strings[index];
                assert(read(static_cast<RegisterId>(index)).to_msb_string()
                    == expected);
            }

            auto completion = executor.prepare_region_completion(
                0U, 4U, 5U, bindings, activation_registers);
            assert(completion);
            completion->commit();
            for (std::size_t index = 0U; index < bindings.size(); ++index) {
                assert(executor.snapshot_register(
                    static_cast<RegisterId>(index)).to_msb_string()
                    == expected_snapshot_after_completion(index));
                assert(read(static_cast<RegisterId>(index)).to_msb_string()
                    == expected_after_completion(index));
            }

            auto child = executor.fork_clone(5U);
            assert(child->snapshot_register(0U).to_msb_string()
                == expected_snapshot_after_completion(0U));
            const auto resumed = executor.resume(context, 5U);
            assert(resumed.instruction == 4U);
            for (std::size_t index = 0U; index < bindings.size(); ++index) {
                const auto expected = tracks_initialization
                    ? original_strings[index]
                    : initial_read_strings[index];
                assert(read(static_cast<RegisterId>(index)).to_msb_string()
                    == expected);
            }
            assert(child->snapshot_register(0U).to_msb_string()
                == (tracks_initialization
                        ? original_strings[0U]
                        : initial_snapshot_strings[0U]));
        }
    }

    for (const auto optimization : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        auto untracked_persistent_process = process;
        untracked_persistent_process.name
            = "region_register_copyback_untracked_persistent";
        untracked_persistent_process.operations = {
            WaitSensitivity { },
            LoadConstant { 0U, PackedLogic4::from_msb_string("1010") },
            LoadConstant {
                1U, PackedLogic4::from_logic9_msb_string("XZ") },
            LoadConstant { 2U, PackedLogic4::from_msb_string(original_wide) },
            CopyRegister { 3U, 2U },
            WaitSensitivity { },
            Jump { 0U },
        };
        auto generated_process = untracked_persistent_process;
        generated_process.id = 77U;
        generated_process.name += "_generated";
        LlvmJitOptions options { optimization, { } };
        options.debug_instrumentation = false;
        LlvmJit jit { options };
        assert(jit.supports_process(
            generated_process, signal_widths, signal_kinds));
        jit.add_process(generated_process.name, generated_process,
            signal_widths, signal_kinds);
        const auto handle = jit.lookup(generated_process.name);
        assert(handle);
        const auto& layout = jit.frame_layout(handle);
        assert(!layout.tracks_register_initialization);
        assert((layout.register_values_persistent
            == std::vector<std::uint8_t> { 1U, 1U, 1U, 1U }));

        const auto remap
            = std::make_shared<const ProcessSignalRemap>(
                ProcessSignalRemap { { 0U, 1U } });
        LlvmProcessExecutor executor {
            jit, handle, untracked_persistent_process, signal_widths,
            signal_kinds, signal_resolutions, remap, ProcessId { 77U },
            nullptr, &generated_process
        };
        MalformedLogic9Context context;
        const auto first_wait = executor.resume(context, 0U);
        assert(first_wait.instruction == 0U);
        assert(first_wait.next_instruction == 1U);
        const auto second_wait = executor.resume(context, 1U);
        assert(second_wait.instruction == 5U);
        assert(second_wait.next_instruction == 6U);
        const auto initial_snapshots = std::array {
            executor.snapshot_register(0U).to_msb_string(),
            executor.snapshot_register(1U).to_msb_string(),
            executor.snapshot_register(2U).to_msb_string(),
            executor.snapshot_register(3U).to_msb_string(),
        };
        const auto original_strings = std::array {
            std::string { "1010" }, std::string { "XZ" },
            original_wide, original_wide
        };
        const auto activation_strings = std::array {
            std::string { "0110" }, std::string { "UZ" },
            activation_wide, activation_wide
        };
        for (std::size_t index = 0U; index < original_strings.size(); ++index) {
            assert(executor.read_register(
                static_cast<RegisterId>(index),
                ProcessExecutor::native_register_width).to_msb_string()
                == original_strings[index]);
        }
        auto completion = executor.prepare_region_completion(
            0U, 5U, 6U, bindings, activation_registers);
        assert(completion);
        completion->commit();
        for (std::size_t index = 0U; index < original_strings.size(); ++index) {
            assert(executor.snapshot_register(
                static_cast<RegisterId>(index)).to_msb_string()
                == initial_snapshots[index]);
            assert(executor.read_register(
                static_cast<RegisterId>(index),
                ProcessExecutor::native_register_width).to_msb_string()
                == activation_strings[index]);
        }
    }

    for (const auto optimization : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        auto hybrid_process = process;
        hybrid_process.name = "region_register_copyback_hybrid";
        hybrid_process.debug_locals.clear();
        DebugLocal visible_register;
        visible_register.name = "visible_value";
        visible_register.type_name = "logic [3:0]";
        visible_register.register_id = 0U;
        visible_register.width = 4U;
        hybrid_process.debug_locals.push_back(visible_register);

        auto generated_process = hybrid_process;
        generated_process.id = 88U;
        generated_process.name += "_generated";
        LlvmJitOptions options { optimization, { } };
        options.debug_instrumentation = false;
        LlvmJit jit { options };
        assert(jit.supports_process(
            generated_process, signal_widths, signal_kinds));
        jit.add_process(generated_process.name, generated_process,
            signal_widths, signal_kinds);
        const auto handle = jit.lookup(generated_process.name);
        assert(handle);
        const auto& layout = jit.frame_layout(handle);
        assert(layout.tracks_register_initialization);
        assert((layout.register_values_persistent
            == std::vector<std::uint8_t> { 1U, 0U, 0U, 0U }));

        const auto remap
            = std::make_shared<const ProcessSignalRemap>(
                ProcessSignalRemap { { 0U, 1U } });
        LlvmProcessExecutor executor {
            jit, handle, hybrid_process, signal_widths, signal_kinds,
            signal_resolutions, remap, ProcessId { 88U }, nullptr,
            &generated_process
        };
        MalformedLogic9Context context;
        const auto suspension = executor.resume(context, 0U);
        assert(suspension.instruction == 4U);
        assert(suspension.next_instruction == 5U);
        const auto initial_hybrid_snapshots = std::array {
            executor.snapshot_register(0U).to_msb_string(),
            executor.snapshot_register(1U).to_msb_string(),
            executor.snapshot_register(2U).to_msb_string(),
            executor.snapshot_register(3U).to_msb_string(),
        };
        assert(executor.snapshot_register(0U).to_msb_string() == "1010");

        auto completion = executor.prepare_region_completion(
            0U, 4U, 5U, bindings, activation_registers);
        assert(completion);
        completion->commit();
        assert(executor.snapshot_register(0U).to_msb_string() == "0110");
        for (std::size_t index = 1U;
             index < initial_hybrid_snapshots.size(); ++index) {
            assert(executor.snapshot_register(
                static_cast<RegisterId>(index)).to_msb_string()
                == initial_hybrid_snapshots[index]);
        }

        const auto resumed = executor.resume(context, 5U);
        assert(resumed.instruction == 4U);
        assert(executor.snapshot_register(0U).to_msb_string() == "1010");
        for (std::size_t index = 1U;
             index < initial_hybrid_snapshots.size(); ++index) {
            assert(executor.snapshot_register(
                static_cast<RegisterId>(index)).to_msb_string()
                == initial_hybrid_snapshots[index]);
        }
    }
}

void test_logic9_callback_ingress_canonicalization()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Process process;
    process.id = 0U;
    process.name = "logic9_callback_ingress";
    process.register_count = 3U;
    process.register_value_kinds = {
        ValueKind::logic9, ValueKind::logic9, ValueKind::logic4
    };
    process.operations = {
        ReadSignal { 0U, 0U },
        LoadConstant {
            1U, PackedLogic4::from_logic9_msb_string("X") },
        Binary { BinaryOperator::case_equal, 2U, 0U, 1U },
        WriteBlocking { 1U, 2U },
        Halt { }
    };
    const std::array<std::uint32_t, 2U> widths { 1U, 1U };
    const std::array<ValueKind, 2U> kinds {
        ValueKind::logic9, ValueKind::logic4
    };
    const std::array<ResolutionKind, 2U> resolutions {
        ResolutionKind::none, ResolutionKind::none
    };

    for (const auto optimization : {
             fsim::compiler::JitOptimizationLevel::o0,
             fsim::compiler::JitOptimizationLevel::o2 }) {
        fsim::compiler::LlvmJit jit {
            fsim::compiler::LlvmJitOptions { optimization, { } }
        };
        assert(jit.supports_process(process, widths, kinds));
        jit.add_process("logic9_callback_ingress", process, widths, kinds);
        fsim::app::application_detail::LlvmProcessExecutor executor {
            jit, jit.lookup("logic9_callback_ingress"), process,
            widths, kinds, resolutions
        };
        MalformedLogic9Context context;
        (void)executor.resume(context, 0U);
        assert(context.output.to_msb_string() == "1");
    }
}

class PostNativeFailureContext final
    : public fsim::runtime::simir::ProcessExecutionContext {
public:
    [[nodiscard]] fsim::runtime::PackedLogic4 read_signal(
        fsim::runtime::simir::SignalId) const override
    {
        return fsim::runtime::PackedLogic4(1U, fsim::runtime::Logic4::zero);
    }

    void write_blocking(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override
    {
    }

    void write_blocking_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override
    {
    }

    void write_update(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4 value) override
    {
        ++attempts;
        if (attempts == 1U) {
            throw std::runtime_error("injected post-native update failure");
        }
        committed_values.push_back(std::move(value));
    }

    void write_update_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override
    {
    }

    void write_after(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        fsim::runtime::SimulationTick) override
    {
    }

    void write_after_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        fsim::runtime::SimulationTick) override
    {
    }

    void write_inertial(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        const fsim::runtime::simir::TransitionDelays&) override
    {
    }

    void write_inertial_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        const fsim::runtime::simir::TransitionDelays&) override
    {
    }

    std::size_t attempts { };
    std::vector<fsim::runtime::PackedLogic4> committed_values;
};

void test_ordered_cohort_postnative_failure()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    using namespace fsim::app::application_detail;
    using namespace fsim::compiler;

    const std::array<std::uint32_t, 1U> widths { 1U };
    const std::array<ValueKind, 1U> kinds { ValueKind::logic4 };
    const std::array<ResolutionKind, 1U> resolutions {
        ResolutionKind::none
    };
    for (const auto optimization : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        LlvmJit jit { LlvmJitOptions { optimization, { } } };
        std::array<Process, 2U> processes;
        std::array<JitProcessHandle, 2U> handles;
        for (std::size_t index = 0; index < processes.size(); ++index) {
            auto& process = processes[index];
            process.id = static_cast<ProcessId>(index);
            process.name = "ordered_postnative_failure_"
                + std::to_string(index);
            process.register_count = 1U;
            process.scheduling_domain
                = ProcessSchedulingDomain::systemverilog;
            process.static_sensitivity = { { 0U, EdgeKind::any } };
            process.operations = {
                LoadConstant {
                    0U,
                    PackedLogic4::from_aval_bval(
                        1U, static_cast<std::uint64_t>(index + 1U), 0U) },
                WriteUpdate { 0U, 0U, SignalUpdateDomain::generic },
                WaitSensitivity { },
                Jump { 0U },
            };
            assert(jit.supports_process(process, widths, kinds));
            jit.add_process(process.name, process, widths, kinds);
            handles[index] = jit.lookup(process.name);
            assert(handles[index]);
        }

        LlvmProcessExecutor first {
            jit, handles[0], processes[0], widths, kinds, resolutions
        };
        LlvmProcessExecutor second {
            jit, handles[1], processes[1], widths, kinds, resolutions
        };
        PostNativeFailureContext context;
        std::array<bool, 2U> queued { true, true };
        std::array<bool, 2U> waiting { };
        std::array<ProcessStatus, 2U> statuses {
            ProcessStatus::running, ProcessStatus::running
        };
        std::array<ProcessCohortResumeEntry, 2U> entries {
            ProcessCohortResumeEntry {
                &first, &context, 0U, { }, { },
                &queued[0], &waiting[0], &statuses[0] },
            ProcessCohortResumeEntry {
                &second, &context, 0U, { }, { },
                &queued[1], &waiting[1], &statuses[1] },
        };

        // This is a host-conversion fault injection after real native cohort
        // execution. It does not certify RegionGraph SV Active admission.
        const auto accepted = first.resume_ordered_cohort(entries);
        assert(accepted == entries.size());
        assert(context.attempts == entries.size());
        assert(context.committed_values.size() == 1U);
        assert(context.committed_values.front().to_msb_string() == "0");
        assert(entries[0].failure);
        bool injected_failure_observed { };
        try {
            std::rethrow_exception(entries[0].failure);
        } catch (const std::runtime_error& error) {
            injected_failure_observed
                = std::string_view { error.what() }
                == "injected post-native update failure";
        } catch (...) {
        }
        assert(injected_failure_observed);
        assert(!entries[1].failure);
        assert(entries[1].result.instruction == 2U);
        assert(entries[1].result.next_instruction == 3U);
        assert(entries[1].result.external.kind
            == ExternalSuspendKind::wait_sensitivity
            || entries[1].result.external.kind
                == ExternalSuspendKind::validated_wait_sensitivity);
        assert(waiting[0] && waiting[1]);
        assert(statuses[0] == ProcessStatus::waiting
            && statuses[1] == ProcessStatus::waiting);
        assert(!queued[0] && !queued[1]);
    }
}

enum class CohortUpdateBatchCase : std::uint8_t {
    projected_writes_only,
    projected_writes_with_active_slots,
    active_slots_only,
    declined_slot_batch_falls_back,
};

void test_cohort_projected_writes_and_update_batches()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    using fsim::app::application_detail::LlvmProcessExecutor;
    using fsim::compiler::JitOptimizationLevel;
    using fsim::compiler::LlvmJit;
    using fsim::compiler::LlvmJitOptions;

    // The slot-only positive case below proves that this test reached the
    // unprofiled cohort slot-batch branch rather than passing through a
    // profiling or disabled-batch fallback.
    constexpr std::uint32_t wide_width = 129U;
    const std::array<std::uint32_t, 5U> signal_widths {
        1U, wide_width, 4U, wide_width, 4U
    };
    const std::array<ValueKind, 5U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4
    };
    const std::array<ResolutionKind, 5U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none
    };
    std::array<PackedLogic4, 2U> wide_values {
        PackedLogic4(wide_width, Logic4::zero),
        PackedLogic4(wide_width, Logic4::zero)
    };
    for (std::uint32_t bit = 0U; bit < 64U; ++bit) {
        wide_values[0U].set(bit, Logic4::one);
    }
    for (std::uint32_t bit = 64U; bit < 128U; ++bit) {
        wide_values[1U].set(bit, Logic4::one);
    }
    wide_values[0U].set(128U, Logic4::one);
    const std::array<PackedLogic4, 2U> narrow_values {
        PackedLogic4::from_msb_string("1010"),
        PackedLogic4::from_msb_string("0101")
    };

    const auto run_case = [&](const JitOptimizationLevel optimization,
                              const CohortUpdateBatchCase which) {
        LlvmJitOptions options { optimization, { } };
        options.debug_instrumentation = false;
        options.require_direct_update_slots = true;
        LlvmJit jit { options };
        std::array<Process, 2U> processes;
        std::array<compiler::JitProcessHandle, 2U> handles;
        for (std::size_t index = 0U; index < processes.size(); ++index) {
            auto& process = processes[index];
            process.id = static_cast<ProcessId>(index);
            process.name = "cohort_update_words_" + std::to_string(index);
            process.register_count
                = which == CohortUpdateBatchCase::projected_writes_only ? 1U : 2U;
            process.register_value_kinds.assign(
                process.register_count, ValueKind::logic4);
            process.static_sensitivity = { { 0U, EdgeKind::any } };
            const auto wide_signal = static_cast<SignalId>(1U + 2U * index);
            const auto narrow_signal = static_cast<SignalId>(2U + 2U * index);
            const bool writes_wide
                = which == CohortUpdateBatchCase::projected_writes_only
                || which == CohortUpdateBatchCase::projected_writes_with_active_slots;
            const bool writes_narrow
                = which != CohortUpdateBatchCase::projected_writes_only;
            if (writes_wide) {
                process.driver_regions.push_back({
                    wide_signal, 0U, wide_width, true });
                process.operations.push_back(LoadConstant {
                    0U, wide_values[index] });
                process.operations.push_back(WriteProjected {
                    wide_signal, 0U, 0U, 0U,
                    ProjectedDelayMode::inertial });
            }
            if (writes_narrow) {
                process.driver_regions.push_back({
                    narrow_signal, 0U, 4U, true });
                process.operations.push_back(LoadConstant {
                    1U, narrow_values[index] });
                process.operations.push_back(WriteUpdate {
                    narrow_signal, 1U, SignalUpdateDomain::generic });
            }
            process.operations.push_back(WaitSensitivity { });
            process.operations.push_back(Jump { 0U });
            assert(jit.supports_process(
                process, signal_widths, signal_kinds));
            jit.add_process(
                process.name, process, signal_widths, signal_kinds);
            handles[index] = jit.lookup(process.name);
            assert(handles[index]);
            if (writes_narrow) {
                const auto& slots = jit.frame_layout(handles[index])
                                        .direct_update_signals;
                assert(std::ranges::find(slots, narrow_signal) != slots.end());
            }
        }

        std::array<std::unique_ptr<LlvmProcessExecutor>, 2U> executors;
        for (std::size_t index = 0U; index < executors.size(); ++index) {
            executors[index] = std::make_unique<LlvmProcessExecutor>(
                jit, handles[index], processes[index], signal_widths,
                signal_kinds, signal_resolutions);
        }
        CohortWordUpdateContext context { signal_widths };
        context.accept_slot_batches(
            which != CohortUpdateBatchCase::declined_slot_batch_falls_back);
        std::array<bool, 2U> queued { true, true };
        std::array<bool, 2U> waiting { };
        std::array<ProcessStatus, 2U> statuses {
            ProcessStatus::running, ProcessStatus::running
        };
        std::array<ProcessCohortResumeEntry, 2U> entries {
            ProcessCohortResumeEntry {
                executors[0].get(), &context, 0U, { }, { },
                &queued[0], &waiting[0], &statuses[0] },
            ProcessCohortResumeEntry {
                executors[1].get(), &context, 0U, { }, { },
                &queued[1], &waiting[1], &statuses[1] },
        };

        assert(executors[0]->resume_cohort(entries) == entries.size());
        for (std::size_t index = 0U; index < entries.size(); ++index) {
            assert(!entries[index].failure);
            assert(waiting[index]);
            assert(!queued[index]);
            assert(statuses[index] == ProcessStatus::waiting);
            assert(entries[index].result.instruction
                == processes[index].operations.size() - 2U);
            assert(entries[index].result.next_instruction
                == processes[index].operations.size() - 1U);
        }

        for (std::size_t index = 0U; index < processes.size(); ++index) {
            const auto wide_signal = static_cast<SignalId>(1U + 2U * index);
            const auto narrow_signal = static_cast<SignalId>(2U + 2U * index);
            const bool writes_wide
                = which == CohortUpdateBatchCase::projected_writes_only
                || which == CohortUpdateBatchCase::projected_writes_with_active_slots;
            const bool writes_narrow
                = which != CohortUpdateBatchCase::projected_writes_only;
            if (writes_wide) {
                assert(context.value(wide_signal) == wide_values[index]);
                assert(context.projected_write_count(wide_signal) == 1U);
                assert(context.word_update_count(wide_signal) == 0U);
                assert(context.slot_update_count(wide_signal) == 0U);
            } else {
                assert(context.value(wide_signal)
                    == PackedLogic4(wide_width, Logic4::x));
                assert(context.projected_write_count(wide_signal) == 0U);
            }
            if (writes_narrow) {
                assert(context.value(narrow_signal) == narrow_values[index]);
            } else {
                assert(context.value(narrow_signal)
                    == PackedLogic4(4U, Logic4::x));
            }
        }

        if (which == CohortUpdateBatchCase::projected_writes_only) {
            assert(context.word_batch_calls() == 0U);
            assert(context.word_batch_sizes().empty());
            assert(context.slot_batch_calls() == 0U);
            assert(context.slot_publication_count() == 0U);
        } else if (which
            == CohortUpdateBatchCase::projected_writes_with_active_slots) {
            assert(context.word_batch_calls() == 0U);
            assert(context.word_batch_sizes().empty());
            assert(context.word_update_count(2U) == 0U);
            assert(context.word_update_count(4U) == 0U);
            assert(context.slot_update_count(2U) == 1U);
            assert(context.slot_update_count(4U) == 1U);
            assert(context.slot_batch_calls() == 1U);
            assert(context.slot_batch_members() == 2U);
            assert(context.slot_publication_count() == 2U);
            assert(context.slot_publication_seen(0U, 2U));
            assert(context.slot_publication_seen(1U, 4U));
        } else if (which == CohortUpdateBatchCase::active_slots_only) {
            assert(context.word_batch_calls() == 0U);
            assert(context.slot_batch_calls() == 1U);
            assert(context.slot_batch_members() == 2U);
            assert(context.slot_publication_count() == 2U);
            assert(context.slot_update_count(2U) == 1U);
            assert(context.slot_update_count(4U) == 1U);
            assert(context.slot_publication_seen(0U, 2U));
            assert(context.slot_publication_seen(1U, 4U));
            assert(context.word_update_count(2U) == 0U);
            assert(context.word_update_count(4U) == 0U);
        } else {
            assert(context.word_batch_calls() == 2U);
            const std::vector<std::size_t> callback_sizes(
                context.word_batch_sizes().begin(),
                context.word_batch_sizes().end());
            assert((callback_sizes == std::vector<std::size_t> { 1U, 1U }));
            assert(context.slot_batch_calls() == 1U);
            assert(context.slot_batch_members() == 2U);
            assert(context.slot_publication_count() == 0U);
            assert(context.word_update_count(2U) == 1U);
            assert(context.word_update_count(4U) == 1U);
            assert(context.slot_update_count(2U) == 0U);
            assert(context.slot_update_count(4U) == 0U);
        }

        std::vector<PackedLogic4> first_values;
        std::vector<std::size_t> first_word_counts;
        std::vector<std::size_t> first_slot_counts;
        std::vector<std::size_t> first_projected_counts;
        first_values.reserve(signal_widths.size());
        first_word_counts.reserve(signal_widths.size());
        first_slot_counts.reserve(signal_widths.size());
        first_projected_counts.reserve(signal_widths.size());
        for (SignalId signal = 0U; signal < signal_widths.size(); ++signal) {
            first_values.push_back(context.value(signal));
            first_word_counts.push_back(context.word_update_count(signal));
            first_slot_counts.push_back(context.slot_update_count(signal));
            first_projected_counts.push_back(
                context.projected_write_count(signal));
        }
        const auto first_word_batches = context.word_batch_calls();
        const auto first_slot_batches = context.slot_batch_calls();
        const auto first_slot_members = context.slot_batch_members();
        const auto first_slot_publications = context.slot_publication_count();
        const std::vector<std::size_t> first_word_batch_sizes(
            context.word_batch_sizes().begin(),
            context.word_batch_sizes().end());
        for (std::size_t index = 0U; index < entries.size(); ++index) {
            entries[index].start_instruction
                = entries[index].result.next_instruction;
            entries[index].result = { };
            entries[index].failure = { };
            queued[index] = true;
            waiting[index] = false;
            statuses[index] = ProcessStatus::running;
        }
        assert(executors[0]->resume_cohort(entries) == entries.size());
        for (std::size_t index = 0U; index < entries.size(); ++index) {
            assert(!entries[index].failure);
            assert(waiting[index]);
            assert(!queued[index]);
            assert(statuses[index] == ProcessStatus::waiting);
        }
        for (SignalId signal = 0U; signal < signal_widths.size(); ++signal) {
            assert(context.value(signal) == first_values[signal]);
            assert(context.word_update_count(signal)
                == 2U * first_word_counts[signal]);
            assert(context.slot_update_count(signal)
                == 2U * first_slot_counts[signal]);
            assert(context.projected_write_count(signal)
                == 2U * first_projected_counts[signal]);
        }
        assert(context.word_batch_calls() == 2U * first_word_batches);
        assert(context.slot_batch_calls() == 2U * first_slot_batches);
        assert(context.slot_batch_members() == 2U * first_slot_members);
        assert(context.slot_publication_count()
            == 2U * first_slot_publications);
        const auto second_word_batch_sizes = context.word_batch_sizes();
        assert(second_word_batch_sizes.size()
            == 2U * first_word_batch_sizes.size());
        assert(std::ranges::equal(
            second_word_batch_sizes.subspan(first_word_batch_sizes.size()),
            first_word_batch_sizes));
    };

    for (const auto optimization : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        run_case(optimization,
            CohortUpdateBatchCase::projected_writes_only);
        run_case(optimization,
            CohortUpdateBatchCase::projected_writes_with_active_slots);
        run_case(optimization,
            CohortUpdateBatchCase::active_slots_only);
        run_case(optimization,
            CohortUpdateBatchCase::declined_slot_batch_falls_back);
    }
}

void check_application_region_constant_variant_selection()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    std::vector<RegionSignalDescriptor> signals {
        { 4U }, { 4U }, { 4U },
    };
    signals[2U].observations = RegionObservation::current;

    Process producer;
    producer.id = 0U;
    producer.name = "constant_variant_producer";
    producer.language_standard = "2008";
    producer.scheduling_domain = ProcessSchedulingDomain::generic;
    producer.register_count = 1U;
    producer.static_sensitivity = { { 0U, EdgeKind::any } };
    producer.driver_regions = { { 1U, 0U, 0U, true } };
    producer.operations = {
        ReadSignal { 0U, 0U },
        WriteProjected { 1U, 0U, 0U, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };

    Process consumer;
    consumer.id = 1U;
    consumer.name = "constant_variant_consumer";
    consumer.language_standard = "2008";
    consumer.scheduling_domain = ProcessSchedulingDomain::generic;
    consumer.register_count = 2U;
    consumer.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4,
    };
    consumer.static_sensitivity = { { 1U, EdgeKind::any } };
    consumer.driver_regions = { { 2U, 0U, 0U, true } };
    consumer.operations = {
        ReadSignal { 0U, 1U },
        UnaryNot { 1U, 0U },
        WriteProjected { 2U, 1U, 0U, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };

    Process startup;
    startup.id = 2U;
    startup.name = "constant_variant_startup";
    startup.language_standard = "2008";
    startup.scheduling_domain = ProcessSchedulingDomain::generic;
    startup.initialize = true;
    startup.register_count = 1U;
    startup.register_value_kinds = { ValueKind::logic4 };
    startup.driver_regions = { { 0U, 0U, 0U, true } };
    startup.operations = {
        DebugPoint { DebugPointKind::process_entry, { }, { } },
        LoadConstant { 0U, PackedLogic4 { 4U, Logic4::zero } },
        WriteProjected { 0U, 0U, 0U, 0U,
            ProjectedDelayMode::inertial },
        Halt { },
    };

    std::vector<Process> processes;
    processes.push_back(std::move(producer));
    processes.push_back(std::move(consumer));
    processes.push_back(std::move(startup));
    std::vector<const Process*> process_bindings;
    process_bindings.reserve(processes.size());
    for (const auto& process : processes) {
        process_bindings.push_back(&process);
    }
    const auto graph = RegionGraph::build(process_bindings, signals);
    const auto& components = graph.certificate_inventory().components;
    const auto component = std::ranges::find_if(components,
        [](const RegionComponentCertificate& value) {
            return value.members == std::vector<ProcessId> { 0U, 1U };
        });
    assert(component != components.end());
    const auto component_index = static_cast<std::size_t>(
        component - components.begin());
    auto compute = graph.build_compute_program(
        component_index, process_bindings);
    assert(compute.has_value());
    auto kernel = std::move(compute->activation_kernel);
    const auto input = std::ranges::find_if(kernel.inputs,
        [](const RegionConeKernelInput& value) {
            return value.signal == 0U && !value.internal;
        });
    assert(input != kernel.inputs.end());
    kernel.constant_inputs.push_back({ 2U, 0U, 0U, 4U,
        ValueKind::logic4, SignalUpdateDomain::generic,
        PackedLogic4 { 4U, Logic4::zero },
        RegionUpdateKind::vhdl_projected,
        ProjectedDelayMode::inertial, 0U, 0U });

    RegionKernelActivationImage image;
    image.generation = 1U;
    image.active_member_indices.resize(kernel.members.size());
    image.scheduler_prefix.frontier_generation = 1U;
    image.scheduler_prefix.frontier_cursor = 0U;
    image.scheduler_prefix.frontier_end = kernel.members.size();
    image.scheduler_prefix.process_domain
        = ProcessSchedulingDomain::generic;
    image.scheduler_prefix.phase = SchedulerPhase::active;
    image.scheduler_prefix.systemverilog_round = 0U;
    for (std::size_t index = 0U; index < kernel.members.size(); ++index) {
        const auto& member = kernel.members[index];
        image.active_member_indices[index] = index;
        const RegionKernelReadyMember request {
            member.process, Process::full_static_trigger_mask,
            RegionKernelActivationOrigin {
                ProcessSchedulingDomain::generic,
                SchedulerPhase::active, 0U, 0U,
                static_cast<StableOrder>(member.process + 1U),
                static_cast<std::uint64_t>(member.process + 1U), 0U,
            },
        };
        image.ready_processes.push_back(member.process);
        image.requests.push_back(request);
        image.scheduler_prefix.tasks.push_back({ index, request });
    }
    for (const auto& value : kernel.inputs) {
        image.register_inputs.push_back({ value.value_register,
            value.internal
                ? PackedLogic4::from_msb_string("1010")
                : PackedLogic4 { 4U, Logic4::zero } });
    }
    for (const auto& member : kernel.members) {
        image.register_inputs.push_back({ member.readiness_register,
            PackedLogic4 { 1U, Logic4::one } });
    }
    std::ranges::sort(image.register_inputs, std::ranges::less { },
        &RegionKernelRegisterInput::register_id);

    const auto boundary = std::ranges::find(image.register_inputs,
        input->value_register, &RegionKernelRegisterInput::register_id);
    assert(boundary != image.register_inputs.end());
    const auto producer_output = std::ranges::find(kernel.outputs, 0U,
        &RegionConeOutputBinding::owner);
    const auto consumer_output = std::ranges::find(kernel.outputs, 1U,
        &RegionConeOutputBinding::owner);
    assert(producer_output != kernel.outputs.end());
    assert(consumer_output != kernel.outputs.end());

    const auto check_output_values = [&](const RegionKernelBackend& backend,
        const std::string_view producer_value,
        const std::string_view consumer_value) {
        const auto registers = backend.activation_registers();
        assert(producer_output->value_register < registers.size());
        assert(consumer_output->value_register < registers.size());
        assert(registers[producer_output->value_register]
            == PackedLogic4::from_msb_string(producer_value));
        assert(registers[consumer_output->value_register]
            == PackedLogic4::from_msb_string(consumer_value));
    };

    for (const auto optimization : { compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        boundary->value = PackedLogic4 { 4U, Logic4::zero };
        compiler::LlvmJitOptions options;
        options.optimization = optimization;
        options.cache_directory.clear();
        const auto provider = app::application_detail::
            make_llvm_region_kernel_backend_provider(
                options, "application-region-constant-variant-test-v1");
        assert(provider != nullptr);
        auto backend = provider->create(kernel);
        assert(backend != nullptr);
        assert(backend->execute(image));
        const auto constant_selection = app::application_detail::
            last_region_constant_variant_for_testing(*backend);
        assert(constant_selection.has_value());
        assert(*constant_selection);
        check_output_values(*backend, "0000", "0101");

        boundary->value.set(0U, Logic4::one);
        assert(backend->execute(image));
        const auto dynamic_selection = app::application_detail::
            last_region_constant_variant_for_testing(*backend);
        assert(dynamic_selection.has_value());
        assert(!*dynamic_selection);
        check_output_values(*backend, "0001", "0101");
    }
}

void check_application_logic9_constant_variant_selection()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto make_value = [](const std::size_t width,
                               const std::size_t start) {
        constexpr std::string_view states { "UX01ZWLH-" };
        std::string text;
        text.reserve(width);
        for (std::size_t index = 0U; index < width; ++index) {
            text.push_back(states[(index + start) % states.size()]);
        }
        return PackedLogic4::from_logic9_msb_string(text);
    };

    const auto run_case = [&](
        const compiler::JitOptimizationLevel optimization,
        const std::uint32_t width) {
        std::vector<RegionSignalDescriptor> signals {
            { width }, { width }, { width },
        };
        for (auto& signal : signals) {
            signal.value_kind = ValueKind::logic9;
        }
        signals[2U].observations = RegionObservation::current;

        Process producer;
        producer.id = 0U;
        producer.name = "logic9_constant_variant_producer";
        producer.language_standard = "2008";
        producer.scheduling_domain = ProcessSchedulingDomain::generic;
        producer.register_count = 1U;
        producer.register_value_kinds = { ValueKind::logic9 };
        producer.static_sensitivity = { { 0U, EdgeKind::any } };
        producer.driver_regions = { { 1U, 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, 0U },
            WriteProjected { 1U, 0U, 0U, 0U,
                ProjectedDelayMode::inertial },
            WaitSensitivity { },
            Jump { 0U },
        };

        Process consumer;
        consumer.id = 1U;
        consumer.name = "logic9_constant_variant_consumer";
        consumer.language_standard = "2008";
        consumer.scheduling_domain = ProcessSchedulingDomain::generic;
        consumer.register_count = 1U;
        consumer.register_value_kinds = { ValueKind::logic9 };
        consumer.static_sensitivity = { { 1U, EdgeKind::any } };
        consumer.driver_regions = { { 2U, 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, 1U },
            WriteProjected { 2U, 0U, 0U, 0U,
                ProjectedDelayMode::inertial },
            WaitSensitivity { },
            Jump { 0U },
        };

        Process startup;
        startup.id = 2U;
        startup.name = "logic9_constant_variant_startup";
        startup.language_standard = "2008";
        startup.scheduling_domain = ProcessSchedulingDomain::generic;
        startup.register_count = 1U;
        startup.register_value_kinds = { ValueKind::logic9 };
        startup.driver_regions = { { 0U, 0U, 0U, true } };
        const auto constant = make_value(width, 0U);
        startup.operations = {
            DebugPoint { DebugPointKind::process_entry, { } },
            LoadConstant { 0U, constant },
            WriteProjected { 0U, 0U, 0U, 0U,
                ProjectedDelayMode::inertial },
            Halt { },
        };

        std::vector<Process> processes;
        processes.push_back(std::move(producer));
        processes.push_back(std::move(consumer));
        processes.push_back(std::move(startup));
        std::vector<const Process*> process_bindings;
        process_bindings.reserve(processes.size());
        for (const auto& process : processes) {
            process_bindings.push_back(&process);
        }
        const auto graph = RegionGraph::build(process_bindings, signals);
        const auto& components = graph.certificate_inventory().components;
        const auto component = std::ranges::find_if(components,
            [](const RegionComponentCertificate& value) {
                return value.members == std::vector<ProcessId> { 0U, 1U };
            });
        assert(component != components.end());
        const auto component_index = static_cast<std::size_t>(
            component - components.begin());
        auto compute = graph.build_compute_program(
            component_index, process_bindings);
        assert(compute.has_value());
        auto kernel = std::move(compute->activation_kernel);
        const auto boundary = std::ranges::find_if(kernel.inputs,
            [](const RegionConeKernelInput& input) {
                return input.signal == 0U && !input.internal
                    && input.value_kind == ValueKind::logic9;
            });
        assert(boundary != kernel.inputs.end());
        kernel.constant_inputs.push_back({ 2U, 0U, 0U, width,
            ValueKind::logic9,
            SignalUpdateDomain::generic, constant,
            RegionUpdateKind::vhdl_projected,
            ProjectedDelayMode::inertial, 0U, 0U });

        RegionKernelActivationImage image;
        image.generation = 1U;
        image.active_member_indices.resize(kernel.members.size());
        image.scheduler_prefix.frontier_generation = 1U;
        image.scheduler_prefix.frontier_cursor = 0U;
        image.scheduler_prefix.frontier_end = kernel.members.size();
        image.scheduler_prefix.process_domain
            = ProcessSchedulingDomain::generic;
        image.scheduler_prefix.phase = SchedulerPhase::active;
        image.scheduler_prefix.systemverilog_round = 0U;
        for (std::size_t index = 0U; index < kernel.members.size(); ++index) {
            const auto& member = kernel.members[index];
            image.active_member_indices[index] = index;
            const RegionKernelReadyMember request {
                member.process, Process::full_static_trigger_mask,
                RegionKernelActivationOrigin {
                    ProcessSchedulingDomain::generic,
                    SchedulerPhase::active, 0U, 0U,
                    static_cast<StableOrder>(member.process + 1U),
                    static_cast<std::uint64_t>(member.process + 1U), 0U,
                },
            };
            image.ready_processes.push_back(member.process);
            image.requests.push_back(request);
            image.scheduler_prefix.tasks.push_back({ index, request });
        }
        const auto internal_value = make_value(width, 2U);
        for (const auto& input : kernel.inputs) {
            image.register_inputs.push_back({ input.value_register,
                input.signal == 0U ? constant : internal_value });
        }
        for (const auto& member : kernel.members) {
            image.register_inputs.push_back({ member.readiness_register,
                PackedLogic4 { 1U, Logic4::one } });
        }
        std::ranges::sort(image.register_inputs, std::ranges::less { },
            &RegionKernelRegisterInput::register_id);

        compiler::LlvmJitOptions options;
        options.optimization = optimization;
        options.cache_directory.clear();
        const auto provider = app::application_detail::
            make_llvm_region_kernel_backend_provider(
                options, "application-region-logic9-constant-v1");
        assert(provider != nullptr);
        auto backend = provider->create(kernel);
        auto dynamic_kernel = kernel;
        dynamic_kernel.constant_inputs.clear();
        auto dynamic_backend = provider->create(dynamic_kernel);
        assert(backend != nullptr && dynamic_backend != nullptr);
        assert(backend->execute(image));
        const auto constant_selection = app::application_detail::
            last_region_constant_variant_for_testing(*backend);
        assert(constant_selection.has_value() && *constant_selection);
        assert(dynamic_backend->execute(image));
        const auto dynamic_selection = app::application_detail::
            last_region_constant_variant_for_testing(*dynamic_backend);
        assert(dynamic_selection.has_value() && !*dynamic_selection);
        assert(std::ranges::equal(backend->activation_registers(),
            dynamic_backend->activation_registers()));
        const auto expected = evaluate_region_activation_kernel_reference(
            kernel, image);
        for (const auto& output : kernel.outputs) {
            assert(backend->activation_registers()[output.value_register]
                == expected[output.value_register]);
        }

        auto changed_image = image;
        auto changed_value = constant;
        changed_value.set_logic9(width - 1U, Logic9::z);
        const auto changed_input = std::ranges::find(
            changed_image.register_inputs, boundary->value_register,
            &RegionKernelRegisterInput::register_id);
        assert(changed_input != changed_image.register_inputs.end());
        changed_input->value = changed_value;
        assert(backend->execute(changed_image));
        const auto fallback_selection = app::application_detail::
            last_region_constant_variant_for_testing(*backend);
        assert(fallback_selection.has_value() && !*fallback_selection);
        assert(dynamic_backend->execute(changed_image));
        assert(std::ranges::equal(backend->activation_registers(),
            dynamic_backend->activation_registers()));
        const auto changed_expected = evaluate_region_activation_kernel_reference(
            kernel, changed_image);
        for (const auto& output : kernel.outputs) {
            assert(backend->activation_registers()[output.value_register]
                == changed_expected[output.value_register]);
        }
    };

    for (const auto optimization : { compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        run_case(optimization, 65U);
    }
}

void check_application_region_forwarding_provider()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto parsed = fsim::frontend::parse_text(
        "forwarding-provider.sv",
        R"(
module forwarding_provider_top(input logic [128:0] source,
                               output wire [128:0] sink_a,
                               output wire [128:0] sink_b,
                               output wire [128:0] sink_join);
  wire [128:0] internal_a;
  wire [128:0] internal_b;
  assign sink_a = ~internal_a;
  assign sink_b = ~internal_b;
  assign internal_a = source + 129'd1;
  assign internal_b = source;
  assign sink_join = sink_a ^ sink_b;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());

    auto semantics = fsim::app::application_detail::build_semantic_model(
        parsed.design, { }, { }, { });
    auto vhdl = fsim::app::application_detail::build_vhdl_hir(
        parsed.design, semantics);
    auto systemverilog = fsim::app::application_detail::build_systemverilog_hir(
        parsed.design, semantics);
    fsim::semantic::CompiledDesign compiled {
        std::move(semantics), std::move(systemverilog), std::move(vhdl) };
    fsim::semantic::refresh_compiled_design_metadata(compiled);
    if (!fsim::semantic::normalize_compiled_design(compiled)) {
        throw std::runtime_error {
            "forwarding-provider test normalization failed"
        };
    }
    auto elaborated = fsim::elaboration::elaborate(
        compiled, "sv:work.forwarding_provider_top");
    assert(elaborated.ok());
    auto design = elaborated.design->state();
    assert(design.signal_info.size() == design.signals.size());

    std::vector<const Process*> process_bindings;
    process_bindings.reserve(design.processes.size());
    for (const auto& process : design.processes) {
        process_bindings.push_back(&process);
    }
    assert(process_bindings.size() == 5U);

    std::vector<RegionSignalDescriptor> descriptors;
    descriptors.reserve(design.signals.size());
    for (std::size_t index = 0U; index < design.signals.size(); ++index) {
        const auto& signal = design.signals[index];
        const auto& info = design.signal_info[index];
        assert(info.width <= std::numeric_limits<std::uint32_t>::max());
        descriptors.push_back({
            static_cast<std::uint32_t>(info.width),
            info.resolution,
            signal.value_kind,
            signal.implicit_driver.has_value(),
            info.is_port
                && info.direction == fsim::frontend::PortDirection::Input,
            signal.event_variable,
            RegionObservation::none,
        });
    }

    const auto graph = RegionGraph::build(process_bindings, descriptors);
    const auto& components = graph.certificate_inventory().components;
    std::optional<RegionConeForwardingKernel> forwarding;
    for (std::size_t component_index = 0U;
         component_index < components.size(); ++component_index) {
        auto compute = graph.build_compute_program(
            component_index, process_bindings);
        if (compute && compute->forwarding_kernel) {
            forwarding = std::move(compute->forwarding_kernel);
            break;
        }
    }
    assert(forwarding.has_value());
    const auto& kernel = forwarding->execution_kernel;
    assert(kernel.members.size() == 5U);
    assert((kernel.member_execution_order
        == std::vector<std::size_t> { 2U, 0U, 3U, 1U, 4U }));
    assert(kernel.inputs.size() == 1U);
    assert(kernel.inputs.front().width == 129U);
    bool has_add_unsigned { };
    for (std::size_t index = 0U;
         index < kernel.program.operations.size(); ++index) {
        const auto operation = kernel.program.operations.expanded(index);
        const auto* const binary = operation_get_if<Binary>(&operation);
        has_add_unsigned = has_add_unsigned
            || (binary != nullptr
                && binary->operation == BinaryOperator::add_unsigned);
    }
    assert(has_add_unsigned);
    assert(forwarding->dependencies.size() == 4U);
    assert(std::ranges::all_of(forwarding->dependencies,
        [](const RegionConeForwardingDependency& dependency) {
            return dependency.offset == 0U
                && (dependency.width == 0U || dependency.width == 1U);
        }));
    std::vector<ProcessId> roots;
    for (const auto& member : forwarding->members) {
        if (member.dependency_count == 0U) {
            roots.push_back(member.process);
        }
    }
    assert(roots.size() == 2U);
    assert(roots[0] < roots[1]);
    RegionKernelSchedulerPrefix origin;
    origin.frontier_generation = 1U;
    origin.frontier_cursor = 0U;
    origin.frontier_end = roots.size();
    origin.phase = SchedulerPhase::active;
    origin.systemverilog_round = 1U;
    origin.process_domain = ProcessSchedulingDomain::systemverilog;
    assert(std::ranges::find(forwarding->members, roots.front(),
        &RegionConeForwardingMember::process) != forwarding->members.end());
    const auto make_root_task = [&](const ProcessId process,
                                    const std::uint64_t stable_order,
                                    const std::uint64_t sequence,
                                    const std::size_t ordinal) {
        origin.tasks.push_back({ ordinal, RegionKernelReadyMember {
            process,
            Process::full_static_trigger_mask,
            RegionKernelActivationOrigin {
                ProcessSchedulingDomain::systemverilog,
                SchedulerPhase::active,
                0U,
                0U,
                stable_order,
                sequence,
                1U,
            } } });
    };
    // Scheduler order is deliberately the reverse of ProcessId order.
    make_root_task(roots[1], 4U, 10U, 0U);
    make_root_task(roots[0], 5U, 11U, 1U);

    for (const auto optimization : {
             fsim::compiler::JitOptimizationLevel::o0,
             fsim::compiler::JitOptimizationLevel::o2 }) {
        fsim::compiler::LlvmJitOptions options;
        options.optimization = optimization;
        options.cache_directory.clear();
        auto provider = fsim::app::application_detail::
            make_llvm_region_kernel_backend_provider(
                options, "application-region-forwarding-provider-v1");
        assert(provider != nullptr);
        auto* const forwarding_provider = dynamic_cast<
            RegionConeForwardingBackendProvider*>(provider.get());
        assert(forwarding_provider != nullptr);
        auto invalid_range = *forwarding;
        invalid_range.dependencies.front().offset = 1U;
        assert(!forwarding_provider->create_forwarding(invalid_range));
        auto invalid_whole_range = *forwarding;
        invalid_whole_range.dependencies.front().offset = 1U;
        invalid_whole_range.dependencies.front().width = 0U;
        assert(!forwarding_provider->create_forwarding(
            invalid_whole_range));
        auto backend = forwarding_provider->create_forwarding(*forwarding);
        assert(backend != nullptr);

        std::vector<PackedLogic4> boundary_inputs;
        boundary_inputs.reserve(kernel.inputs.size());
        std::vector<PackedLogic4> outputs;
        outputs.reserve(kernel.outputs.size());
        for (const auto& output : kernel.outputs) {
            outputs.emplace_back(output.width, Logic4::zero);
        }

        std::vector<PackedLogic4> input_values {
            PackedLogic4 { 129U, Logic4::zero },
            PackedLogic4 { 129U, Logic4::one },
            PackedLogic4 { 129U, Logic4::x },
            PackedLogic4 { 129U, Logic4::z },
        };
        PackedLogic4 carry_across_word { 129U, Logic4::zero };
        for (std::size_t bit = 0U; bit < 65U; ++bit) {
            carry_across_word.set(bit, Logic4::one);
        }
        input_values.push_back(std::move(carry_across_word));
        for (const auto& source_value : input_values) {
            boundary_inputs.clear();
            for (const auto& input : kernel.inputs) {
                assert(input.width == source_value.width());
                boundary_inputs.push_back(source_value);
            }
            assert(backend->execute_forwarding(
                origin, boundary_inputs, outputs));

            const auto root_output_a = std::ranges::find_if(kernel.outputs,
                [&](const RegionConeOutputBinding& output) {
                    return output.owner == roots[0];
                });
            const auto root_output_b = std::ranges::find_if(kernel.outputs,
                [&](const RegionConeOutputBinding& output) {
                    return output.owner == roots[1];
                });
            const auto child_a = std::ranges::find_if(forwarding->members,
                [&](const RegionConeForwardingMember& member) {
                    const auto root = std::ranges::find(
                        forwarding->members, roots[0],
                        &RegionConeForwardingMember::process);
                    if (root == forwarding->members.end()) {
                        return false;
                    }
                    const auto root_index = static_cast<std::size_t>(
                        root - forwarding->members.begin());
                    return std::ranges::any_of(
                        std::span<const RegionConeForwardingDependency> {
                            forwarding->dependencies }
                            .subspan(member.dependency_begin,
                                member.dependency_count),
                        [&](const RegionConeForwardingDependency& dependency) {
                            return dependency.writer_member_index == root_index;
                        });
                });
            const auto child_b = std::ranges::find_if(forwarding->members,
                [&](const RegionConeForwardingMember& member) {
                    const auto root = std::ranges::find(
                        forwarding->members, roots[1],
                        &RegionConeForwardingMember::process);
                    if (root == forwarding->members.end()) {
                        return false;
                    }
                    const auto root_index = static_cast<std::size_t>(
                        root - forwarding->members.begin());
                    return std::ranges::any_of(
                        std::span<const RegionConeForwardingDependency> {
                            forwarding->dependencies }
                            .subspan(member.dependency_begin,
                                member.dependency_count),
                        [&](const RegionConeForwardingDependency& dependency) {
                            return dependency.writer_member_index == root_index;
                        });
                });
            assert(root_output_a != kernel.outputs.end());
            assert(root_output_b != kernel.outputs.end());
            assert(child_a != forwarding->members.end());
            assert(child_b != forwarding->members.end());
            const auto child_output_a = std::ranges::find_if(kernel.outputs,
                [&](const RegionConeOutputBinding& output) {
                    return output.owner == child_a->process;
                });
            const auto child_output_b = std::ranges::find_if(kernel.outputs,
                [&](const RegionConeOutputBinding& output) {
                    return output.owner == child_b->process;
                });
            assert(child_output_a != kernel.outputs.end());
            assert(child_output_b != kernel.outputs.end());
            const auto join_output = std::ranges::find_if(kernel.outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.owner == 4U;
                });
            assert(join_output != kernel.outputs.end());
            const auto output_index = [&](const auto iterator) {
                return static_cast<std::size_t>(
                    iterator - kernel.outputs.begin());
            };
            auto addend = PackedLogic4 { 129U, Logic4::zero };
            addend.set(0U, Logic4::one);
            const auto added_value = binary_value(
                BinaryOperator::add_unsigned, source_value, addend);
            const auto invert = [](const PackedLogic4& value) {
                PackedLogic4 result { value.width(), Logic4::zero };
                for (std::size_t bit = 0U; bit < value.width(); ++bit) {
                    result.set(bit, fsim::runtime::logic_not(value.get(bit)));
                }
                return result;
            };
            assert(outputs[output_index(root_output_a)] == added_value);
            assert(outputs[output_index(root_output_b)] == source_value);
            assert(outputs[output_index(child_output_a)]
                == invert(added_value));
            assert(outputs[output_index(child_output_b)]
                == invert(source_value));
            assert(outputs[output_index(join_output)]
                == binary_value(BinaryOperator::bit_xor,
                    invert(added_value), invert(source_value)));
        }
    }
}

void check_application_region_forwarding_provider_wide_values()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto make_pattern = [](const std::size_t width,
                                 const std::size_t seed) {
        PackedLogic4 result { width, Logic4::zero };
        for (std::size_t bit = 0U; bit < width; ++bit) {
            const auto selector = (bit * 7U + seed * 3U) % 13U;
            const auto value = selector == 0U ? Logic4::zero
                : selector == 1U ? Logic4::one
                : selector == 2U ? Logic4::x
                : selector == 3U ? Logic4::z
                                 : Logic4::zero;
            result.set(bit, value);
        }
        result.set(width - 1U, seed == 0U ? Logic4::one : Logic4::z);
        if (width > 1U) {
            result.set(width - 2U, seed == 0U ? Logic4::x : Logic4::one);
        }
        return result;
    };

    for (const auto width : { 65U, 129U, 256U, 1024U }) {
        const auto range = std::string { "[" }
            + std::to_string(width - 1U) + ":0]";
        const auto source
            = "module forwarding_provider_wide_top(input logic " + range
            + " source, output wire " + range
            + " sink_a, output wire " + range + " sink_b,\n"
            + "  output wire " + range + " sink_join);\n"
              "  wire " + range + " internal_a;\n"
              "  wire " + range + " internal_b;\n"
              "  assign sink_a = ~internal_a;\n"
              "  assign sink_b = ~internal_b;\n"
              "  assign internal_a = ~source;\n"
              "  assign internal_b = source;\n"
              "  assign sink_join = sink_a ^ sink_b;\n"
              "endmodule\n";
        const auto parsed = fsim::frontend::parse_text(
            "forwarding-provider-wide.sv", source,
            fsim::frontend::Language::SystemVerilog2017);
        assert(parsed.ok());
        auto semantics = fsim::app::application_detail::build_semantic_model(
            parsed.design, { }, { }, { });
        auto vhdl = fsim::app::application_detail::build_vhdl_hir(
            parsed.design, semantics);
        auto systemverilog
            = fsim::app::application_detail::build_systemverilog_hir(
                parsed.design, semantics);
        fsim::semantic::CompiledDesign compiled {
            std::move(semantics), std::move(systemverilog), std::move(vhdl) };
        fsim::semantic::refresh_compiled_design_metadata(compiled);
        if (!fsim::semantic::normalize_compiled_design(compiled)) {
            throw std::runtime_error {
                "wide forwarding-provider test normalization failed"
            };
        }
        auto elaborated = fsim::elaboration::elaborate(
            compiled, "sv:work.forwarding_provider_wide_top");
        assert(elaborated.ok());
        auto design = elaborated.design->state();

        std::vector<const Process*> process_bindings;
        process_bindings.reserve(design.processes.size());
        for (const auto& process : design.processes) {
            process_bindings.push_back(&process);
        }
        assert(process_bindings.size() == 5U);

        std::vector<RegionSignalDescriptor> descriptors;
        descriptors.reserve(design.signals.size());
        for (std::size_t index = 0U; index < design.signals.size(); ++index) {
            const auto& signal = design.signals[index];
            const auto& info = design.signal_info[index];
            assert(info.width == width);
            descriptors.push_back({ static_cast<std::uint32_t>(info.width),
                info.resolution, signal.value_kind, signal.implicit_driver.has_value(),
                info.is_port
                    && info.direction == fsim::frontend::PortDirection::Input,
                signal.event_variable, RegionObservation::none });
        }

        const auto graph = RegionGraph::build(process_bindings, descriptors);
        std::optional<RegionConeForwardingKernel> forwarding;
        const auto& components = graph.certificate_inventory().components;
        for (std::size_t component = 0U;
             component < components.size(); ++component) {
            auto compute = graph.build_compute_program(
                component, process_bindings);
            if (compute && compute->forwarding_kernel) {
                forwarding = std::move(compute->forwarding_kernel);
                break;
            }
        }
        assert(forwarding.has_value());
        assert(forwarding->members.size() == process_bindings.size());
        const auto& kernel = forwarding->execution_kernel;
        assert(kernel.inputs.size() == 1U);
        assert(kernel.outputs.size() == 5U);
        assert(std::ranges::all_of(kernel.outputs,
            [width](const RegionConeOutputBinding& output) {
                return output.width == width
                    && output.value_kind == ValueKind::logic4;
            }));

        std::vector<ProcessId> roots;
        for (const auto& member : forwarding->members) {
            if (member.dependency_count == 0U) {
                roots.push_back(member.process);
            }
        }
        assert(roots.size() == 2U);
        RegionKernelSchedulerPrefix origin;
        origin.frontier_generation = 1U;
        origin.frontier_cursor = 0U;
        origin.frontier_end = roots.size();
        origin.phase = SchedulerPhase::active;
        origin.systemverilog_round = 1U;
        origin.process_domain = ProcessSchedulingDomain::systemverilog;
        for (std::size_t index = 0U; index < roots.size(); ++index) {
            const auto process = roots[roots.size() - index - 1U];
            const RegionKernelReadyMember request {
                process, Process::full_static_trigger_mask,
                RegionKernelActivationOrigin {
                    ProcessSchedulingDomain::systemverilog,
                    SchedulerPhase::active, 0U, 0U,
                    static_cast<StableOrder>(4U + index),
                    static_cast<std::uint64_t>(10U + index), 1U,
                } };
            origin.tasks.push_back({ index, request });
        }

        const auto make_image = [&](const PackedLogic4& boundary) {
            RegionKernelActivationImage image;
            image.generation = 1U;
            const auto& input = kernel.inputs.front();
            image.register_inputs.push_back(
                { input.value_register,
                    PackedLogic4::from_word_planes(boundary.width(),
                        boundary.aval_words(), boundary.bval_words()) });
            for (const auto& member : kernel.members) {
                image.register_inputs.push_back({ member.readiness_register,
                    PackedLogic4 { 1U, Logic4::one } });
                image.active_member_indices.push_back(
                    static_cast<std::size_t>(&member - kernel.members.data()));
                image.ready_processes.push_back(member.process);
            }
            image.scheduler_prefix = origin;
            std::ranges::sort(image.register_inputs, std::ranges::less { },
                &RegionKernelRegisterInput::register_id);
            return image;
        };

        for (const auto optimization : {
                 fsim::compiler::JitOptimizationLevel::o0,
                 fsim::compiler::JitOptimizationLevel::o2 }) {
            fsim::compiler::LlvmJitOptions options;
            options.optimization = optimization;
            options.cache_directory.clear();
            auto provider = fsim::app::application_detail::
                make_llvm_region_kernel_backend_provider(
                    options, "application-region-forwarding-wide-v1");
            assert(provider != nullptr);
            auto* const forwarding_provider = dynamic_cast<
                RegionConeForwardingBackendProvider*>(provider.get());
            assert(forwarding_provider != nullptr);
            auto backend
                = forwarding_provider->create_forwarding(*forwarding);
            assert(backend != nullptr);
            auto logic9_kernel = *forwarding;
            logic9_kernel.execution_kernel.outputs.front().value_kind
                = ValueKind::logic9;
            assert(!forwarding_provider->create_forwarding(logic9_kernel));

            std::array<PackedLogic4, 1U> boundary_values {
                make_pattern(width, 0U) };
            std::vector<PackedLogic4> outputs;
            outputs.reserve(kernel.outputs.size());
            for (const auto& output : kernel.outputs) {
                outputs.emplace_back(output.width, Logic4::zero);
            }
            const auto verify_output_values = [&](const auto& image,
                                                  const auto& actual) {
                const auto expected
                    = evaluate_region_activation_kernel_reference(
                        kernel, image);
                assert(actual.size() == kernel.outputs.size());
                for (std::size_t index = 0U;
                     index < kernel.outputs.size(); ++index) {
                    const auto& binding = kernel.outputs[index];
                    assert(actual[index].width() == width);
                    assert(!actual[index].is_logic9());
                    assert(actual[index]
                        == expected[binding.value_register]);
                }
            };
            const auto describe_outputs = [](const auto& values) {
                std::vector<std::tuple<std::size_t, bool, std::string>> result;
                result.reserve(values.size());
                for (const auto& value : values) {
                    result.emplace_back(value.width(), value.is_logic9(),
                        value.to_msb_string());
                }
                return result;
            };
            const auto expected_output_bits = [&](const auto& image) {
                const auto expected
                    = evaluate_region_activation_kernel_reference(
                        kernel, image);
                std::vector<std::string> result;
                result.reserve(kernel.outputs.size());
                for (const auto& binding : kernel.outputs) {
                    result.push_back(
                        expected[binding.value_register].to_msb_string());
                }
                return result;
            };
            const auto require_output_bits = [](const auto& values,
                                                const auto& expected) {
                assert(values.size() == expected.size());
                for (std::size_t index = 0U;
                     index < values.size(); ++index) {
                    assert(values[index].to_msb_string() == expected[index]);
                }
            };
            auto image = make_image(boundary_values.front());
            assert(backend->execute_forwarding(
                origin, boundary_values, outputs));
            verify_output_values(image, outputs);
            const auto first_output_bits = expected_output_bits(image);
            const auto first_output_summary = describe_outputs(outputs);
            // These are retained owning copies, not aliases for scheduler
            // pending/current/LAST roles. The independent strings above are
            // the oracle for their later lifetime checks.
            const auto retained_first_output_copy_a = outputs;
            const auto retained_first_output_copy_b = outputs;

            // A bad second slot declines before native entry or any output
            // swap; the first slot and every sibling retain their old values.
            const auto bad_output_index = std::size_t { 1U };
            outputs[bad_output_index]
                = PackedLogic4 { width + 1U, Logic4::one };
            const auto wrong_width_before = describe_outputs(outputs);
            assert(!backend->execute_forwarding(
                origin, boundary_values, outputs));
            assert(describe_outputs(outputs) == wrong_width_before);
            outputs[bad_output_index]
                = PackedLogic4 { width, Logic4::zero };
            outputs[bad_output_index].fill(Logic9::u);
            const auto wrong_kind_before = describe_outputs(outputs);
            assert(!backend->execute_forwarding(
                origin, boundary_values, outputs));
            assert(describe_outputs(outputs) == wrong_kind_before);
            require_output_bits(retained_first_output_copy_a,
                first_output_bits);
            require_output_bits(retained_first_output_copy_b,
                first_output_bits);
            assert(describe_outputs(retained_first_output_copy_a)
                == first_output_summary);
            assert(describe_outputs(retained_first_output_copy_b)
                == first_output_summary);
            outputs = retained_first_output_copy_a;
            assert(describe_outputs(outputs) == first_output_summary);

            auto invalid_origin = origin;
            ++invalid_origin.tasks.front().task_ordinal;
            const auto decline_before = describe_outputs(outputs);
            assert(!backend->execute_forwarding(
                invalid_origin, boundary_values, outputs));
            assert(describe_outputs(outputs) == decline_before);
            auto invalid_boundary = boundary_values;
            invalid_boundary.front() = PackedLogic4 { width - 1U,
                Logic4::one };
            assert(!backend->execute_forwarding(
                origin, invalid_boundary, outputs));
            assert(describe_outputs(outputs) == decline_before);
            assert(describe_outputs(retained_first_output_copy_a)
                == first_output_summary);
            assert(describe_outputs(retained_first_output_copy_b)
                == first_output_summary);

            boundary_values.front() = make_pattern(width, 1U);
            image = make_image(boundary_values.front());
            assert(backend->execute_forwarding(
                origin, boundary_values, outputs));
            verify_output_values(image, outputs);
            require_output_bits(retained_first_output_copy_a,
                first_output_bits);
            require_output_bits(retained_first_output_copy_b,
                first_output_bits);
            assert(describe_outputs(retained_first_output_copy_a)
                == first_output_summary);
            assert(describe_outputs(retained_first_output_copy_b)
                == first_output_summary);
            assert(describe_outputs(outputs) != first_output_summary);

            // The preceding batch swap moved the old first result, still
            // shared by the retained copies, into provider scratch. Reusing
            // that scratch must detach before overwriting and preserve them.
            boundary_values.front() = make_pattern(width, 2U);
            image = make_image(boundary_values.front());
            assert(backend->execute_forwarding(
                origin, boundary_values, outputs));
            verify_output_values(image, outputs);
            require_output_bits(retained_first_output_copy_a,
                first_output_bits);
            require_output_bits(retained_first_output_copy_b,
                first_output_bits);
            assert(describe_outputs(retained_first_output_copy_a)
                == first_output_summary);
            assert(describe_outputs(retained_first_output_copy_b)
                == first_output_summary);
        }
    }
}

} // namespace
#endif

void ApplicationTestFixture::test_simulation_semantics()
{
#if defined(FSIM_HAS_LLVM)
    check_application_region_constant_variant_selection();
    check_application_logic9_constant_variant_selection();
    check_application_region_forwarding_provider();
    check_application_region_forwarding_provider_wide_values();
    {
        auto process_view_source
            = directory / "process-program-view-instances.sv";
        {
            std::ofstream output(process_view_source);
            assert(output);
            output
                << "module process_view_leaf;\n"
                << "  logic trigger;\n"
                << "  logic result;\n"
                << "  always @(trigger) result = ~trigger;\n"
                << "endmodule\n"
                << "module process_view_top;\n"
                << "  process_view_leaf left();\n"
                << "  process_view_leaf right();\n"
                << "endmodule\n";
            assert(output);
        }
        for (const auto optimization : {
                 fsim::project::Optimization::o0,
                 fsim::project::Optimization::o2 }) {
            auto view_config = base_config();
            view_config.project.name = "process-program-view-instances";
            view_config.project.top = "sv:work.process_view_top";
            view_config.source_sets.clear();
            view_config.build.optimization = optimization;
            view_config.build.cache_path = directory
                / (optimization == fsim::project::Optimization::o0
                        ? "process-program-view-o0-cache"
                        : "process-program-view-o2-cache");
            fsim::project::SourceSet view_sources;
            view_sources.language
                = fsim::project::Language::system_verilog;
            view_sources.standard = "2017";
            view_sources.library = "work";
            view_sources.files.push_back(process_view_source);
            view_config.source_sets.push_back(std::move(view_sources));

            fsim::diagnostic::Engine diagnostics;
            auto project = fsim::app::build_project(
                view_config, diagnostics);
            assert(project);
            fsim::app::Simulation simulation(
                std::move(*project), view_config.run.max_deltas,
                fsim::app::SimulationEngine::compiled);
            assert(simulation.compiled_process_count() >= 2U);
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    public_program_facade_count(simulation)
                == 0U);
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    has_async_compilation_job(simulation));
            simulation.await_all_native_compilation();
            assert(simulation.compiled_module_count() >= 1U);
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    installed_executor_count(simulation)
                >= 2U);
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    public_program_facade_count(simulation)
                == 0U);

            const auto& occurrences = simulation.design_ir().processes();
            const auto left = std::ranges::find_if(
                occurrences, [](const auto& occurrence) {
                    return occurrence.name.find("left")
                        != std::string::npos;
                });
            const auto right = std::ranges::find_if(
                occurrences, [](const auto& occurrence) {
                    return occurrence.name.find("right")
                        != std::string::npos;
                });
            assert(left != occurrences.end());
            assert(right != occurrences.end());
            assert(left->runtime_index != right->runtime_index);
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    instances_share_template(simulation,
                        left->runtime_index, right->runtime_index));
            assert(!left->sensitivities.empty());
            assert(!right->sensitivities.empty());
            assert(left->sensitivities.front()
                != right->sensitivities.front());

            const auto& late_facade = simulation.process_program(
                left->runtime_index);
            const auto* const late_facade_address = &late_facade;
            assert(late_facade.id == left->runtime_index);
            assert(late_facade.name == left->name);
            assert(!late_facade.operations.empty());
            assert(!late_facade.static_sensitivity.empty());
            assert(!late_facade.driver_regions.empty());
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    public_program_facade_count(simulation)
                == 1U);
            assert(&simulation.process_program(left->runtime_index)
                == late_facade_address);
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    public_program_facade_count(simulation)
                == 1U);
        }
    }
    {
        auto body_sharing_source
            = directory / "jit-body-sharing-overrides.sv";
        {
            std::ofstream output(body_sharing_source);
            assert(output);
            output
                << "module jit_body_leaf(\n"
                << "  input logic [7:0] data,\n"
                << "  output logic [7:0] result\n"
                << ");\n"
                << "  always @(data) result <= data ^ 8'h01;\n"
                << "endmodule\n"
                << "module jit_body_top;\n"
                << "  logic [7:0] first_data;\n"
                << "  logic [7:0] second_data;\n"
                << "  for (genvar index = 0; index < 64; index = index + 1) begin : first_instances\n"
                << "    logic [7:0] result;\n"
                << "    jit_body_leaf leaf(.data(first_data), .result(result));\n"
                << "  end\n"
                << "  for (genvar index = 0; index < 64; index = index + 1) begin : second_instances\n"
                << "    logic [7:0] result;\n"
                << "    jit_body_leaf leaf(.data(second_data), .result(result));\n"
                << "  end\n"
                << "  initial begin\n"
                << "    first_data = 8'h5A;\n"
                << "    second_data = 8'hA5;\n"
                << "  end\n"
                << "endmodule\n";
            assert(output);
        }
        const auto install_sparse_literal_overrides = [](
            fsim::app::BuiltProject& project) {
            using fsim::runtime::PackedLogic4;
            using fsim::runtime::simir::LoadConstant;
            using fsim::runtime::simir::Process;
            using fsim::runtime::simir::ProcessId;
            using fsim::runtime::simir::WriteUpdate;

            auto state = std::move(project.design).state();
            Process* representative { };
            std::size_t literal_index { };
            const auto literal_value = PackedLogic4::from_msb_string(
                "00000001");
            for (auto& process : state.processes) {
                if (process.static_sensitivity.empty()
                    || process.driver_regions.empty()) {
                    continue;
                }
                for (std::size_t index = 0;
                    index < process.operations.size(); ++index) {
                    const auto* const literal
                        = fsim::runtime::simir::operation_get_if<LoadConstant>(
                            &std::as_const(process.operations).data()[index]);
                    if (literal != nullptr
                        && literal->value == literal_value) {
                        representative = &process;
                        literal_index = index;
                        break;
                    }
                }
                if (representative != nullptr) {
                    break;
                }
            }
            assert(representative != nullptr);
            const auto body_identity
                = representative->operations.body_identity();
            std::vector<Process*> shared_instances;
            for (auto& process : state.processes) {
                if (process.operations.body_identity() == body_identity
                    && !process.static_sensitivity.empty()
                    && !process.driver_regions.empty()) {
                    shared_instances.push_back(&process);
                }
            }
            assert(shared_instances.size() >= 64U);
            assert(shared_instances.front() == representative);

            const auto representative_output
                = representative->driver_regions.front().signal;
            const auto representative_id = representative->id;
            std::optional<std::size_t> update_index;
            for (std::size_t index = 0U;
                index < representative->operations.size(); ++index) {
                const auto operation
                    = representative->operations.expanded(index);
                const auto* const write
                    = fsim::runtime::simir::operation_get_if<WriteUpdate>(
                        &operation);
                if (write != nullptr
                    && write->signal == representative_output) {
                    assert(!update_index);
                    assert(write->domain
                        == fsim::runtime::simir::SignalUpdateDomain::
                            systemverilog_nba);
                    update_index = index;
                }
            }
            assert(update_index);
            const auto read_signal = [](const Process& process) {
                for (std::size_t index = 0U;
                    index < process.operations.size(); ++index) {
                    const auto operation = process.operations.expanded(index);
                    if (const auto* const read
                        = fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::ReadSignal>(&operation)) {
                        return std::optional {
                            static_cast<fsim::runtime::simir::SignalId>(
                                read->signal) };
                    }
                }
                return std::optional<fsim::runtime::simir::SignalId> { };
            };
            const auto representative_input = read_signal(*representative);
            assert(representative_input);
            const auto changed_value = PackedLogic4::from_msb_string(
                "00000010");
            constexpr std::size_t changed_instance_count = 32U;
            assert(shared_instances.size() > changed_instance_count);
            std::optional<fsim::runtime::simir::SignalId> changed_output;
            std::optional<fsim::runtime::simir::SignalId> remapped_input;
            std::optional<fsim::runtime::simir::SignalId> remapped_input_output;
            std::optional<ProcessId> remapped_input_process;
            std::optional<ProcessId> domain_mismatch_process;
            ProcessId changed_process { };
            std::size_t changed_instances { };
            std::size_t domain_mismatch_instances { };
            for (auto* const instance : shared_instances) {
                auto& process = *instance;
                if (&process == representative
                    || read_signal(process) != representative_input) {
                    continue;
                }
                assert(process.driver_regions.size() == 1U);
                auto operation = process.operations.expanded(literal_index);
                auto* const literal
                    = fsim::runtime::simir::operation_get_if<LoadConstant>(
                        &operation);
                assert(literal != nullptr
                    && literal->value == literal_value);
                literal->value = changed_value;
                process.operations.replace(literal_index, std::move(operation));
                assert(process.operations.body_identity() == body_identity);
                const auto overrides
                    = process.operations.instance_operation_overrides();
                assert(std::ranges::any_of(
                    overrides, [&](const auto& override) {
                        return override.first == literal_index
                            && fsim::runtime::simir::operation_get_if<
                                   LoadConstant>(&override.second)
                                != nullptr;
                    }));
                if (!changed_output) {
                    changed_output = process.driver_regions.front().signal;
                    changed_process = process.id;
                }
                ++changed_instances;
                if (changed_instances == changed_instance_count) {
                    break;
                }
            }
            assert(changed_instances == changed_instance_count);
            for (auto* const instance : shared_instances) {
                auto& process = *instance;
                if (read_signal(process) == representative_input) {
                    continue;
                }
                assert(process.driver_regions.size() == 1U);
                if (!remapped_input_output) {
                    remapped_input_output
                        = process.driver_regions.front().signal;
                }
                if (domain_mismatch_instances < 32U) {
                    auto operation
                        = process.operations.expanded(*update_index);
                    auto* const write
                        = fsim::runtime::simir::operation_get_if<WriteUpdate>(
                            &operation);
                    assert(write != nullptr
                        && write->domain
                            == fsim::runtime::simir::SignalUpdateDomain::
                                systemverilog_nba);
                    write->domain
                        = fsim::runtime::simir::SignalUpdateDomain::generic;
                    process.operations.replace(
                        *update_index, std::move(operation));
                    assert(process.operations.body_identity() == body_identity);
                    if (!domain_mismatch_process) {
                        domain_mismatch_process = process.id;
                    }
                    ++domain_mismatch_instances;
                } else if (!remapped_input_process) {
                    remapped_input = read_signal(process);
                    remapped_input_process = process.id;
                    remapped_input_output
                        = process.driver_regions.front().signal;
                }
            }
            assert(changed_output);
            assert(remapped_input);
            assert(remapped_input_output);
            assert(remapped_input_process);
            assert(domain_mismatch_process);
            assert(domain_mismatch_instances == 32U);
            assert(*remapped_input != *representative_input);
            assert(*changed_output != representative_output);
            assert(*remapped_input_output != representative_output);
            auto design = fsim::elaboration::ElaboratedDesign::from_state(
                std::move(state));
            assert(design);
            project.design = std::move(*design);
            assert(project.design_ir.processes().size() >= 128U);
            return std::tuple {
                representative_id, changed_process, *domain_mismatch_process,
                *remapped_input_process, *representative_input,
                *remapped_input, representative_output, *changed_output,
                *remapped_input_output,
                changed_instances, domain_mismatch_instances };
        };

        for (const auto optimization : {
                 fsim::project::Optimization::o0,
                 fsim::project::Optimization::o2 }) {
            auto body_config = base_config();
            body_config.project.name = "jit-body-sharing-overrides";
            body_config.project.top = "sv:work.jit_body_top";
            body_config.source_sets.clear();
            body_config.build.optimization = optimization;
            body_config.build.cache_path = directory
                / (optimization == fsim::project::Optimization::o0
                        ? "jit-body-sharing-o0-cache"
                        : "jit-body-sharing-o2-cache");
            fsim::project::SourceSet body_sources;
            body_sources.language
                = fsim::project::Language::system_verilog;
            body_sources.standard = "2017";
            body_sources.library = "work";
            body_sources.files.push_back(body_sharing_source);
            body_config.source_sets.push_back(std::move(body_sources));

            fsim::diagnostic::Engine diagnostics;
            auto project = fsim::app::build_project(
                body_config, diagnostics);
            assert(project);
            const auto [representative, changed, domain_mismatch,
                remapped_instance, representative_input, remapped_input,
                representative_output, changed_output, remapped_input_output,
                changed_instances, domain_mismatch_instances]
                = install_sparse_literal_overrides(*project);
            assert(changed_instances == 32U);
            assert(domain_mismatch_instances == 32U);

            fsim::app::Simulation simulation(
                std::move(*project), body_config.run.max_deltas,
                fsim::app::SimulationEngine::compiled);
            simulation.await_all_native_compilation();
            assert(simulation.compiled_process_count() >= 128U);
            // The two signal banks share their unchanged NBA body. The
            // changed literal and scheduling-domain override each need a
            // separate generated body; the tiny startup process stays on the
            // interpreter tier.
            assert(simulation.compiled_module_count() == 3U);
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    instances_share_template(
                        simulation, representative, changed));
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    instances_share_template(
                        simulation, representative, remapped_instance));
            assert(
                fsim::runtime::simir::NativeRegionAllocationTestAccess::
                    instances_share_template(
                        simulation, representative, domain_mismatch));
            assert(representative_input != remapped_input);
            assert(simulation.run().status
                == fsim::runtime::RunStatus::completed);
            assert(simulation.read_signal_snapshot(representative_output)
                .to_msb_string() == "01011011");
            assert(simulation.read_signal_snapshot(changed_output)
                .to_msb_string() == "01011000");
            // The second bank's A5 input maps to A4; using the representative
            // bank's 5A input would produce 5B instead.
            assert(simulation.read_signal_snapshot(remapped_input_output)
                .to_msb_string() == "10100100");
        }
    }
    struct RequiredDirectReadEntryProbeGuard {
        RequiredDirectReadEntryProbeGuard()
        {
            fsim::app::application_detail::
                set_required_direct_read_entry_counting_for_testing(true);
        }

        ~RequiredDirectReadEntryProbeGuard()
        {
            fsim::app::application_detail::
                set_required_direct_read_entry_counting_for_testing(false);
        }
    } probe_guard;
    {
        auto required_read_config = base_config();
        required_read_config.project.name = "ordinary-required-read-entry";
        required_read_config.project.top = "sv:work.ordinary_required_read";
        required_read_config.source_sets.clear();
        const auto required_read_source
            = directory / "ordinary-required-read-entry.sv";
        {
            std::ofstream output(required_read_source);
            assert(output);
            output
                << "module ordinary_required_read;\n"
                << "  logic a;\n"
                << "  logic b;\n"
                << "  logic y;\n"
                << "  always @(a or b) y <= a ^ b;\n"
                << "  initial begin #10 $finish; end\n"
                << "endmodule\n";
            assert(output);
        }
        fsim::project::SourceSet required_read_sources;
        required_read_sources.language
            = fsim::project::Language::system_verilog;
        required_read_sources.standard = "2017";
        required_read_sources.library = "work";
        required_read_sources.files.push_back(required_read_source);
        required_read_config.source_sets.push_back(
            std::move(required_read_sources));

        struct RequiredReadCapture {
            fsim::runtime::RunResult result;
            std::string a;
            std::string b;
            std::string y;
            std::uint64_t strict_at_start { };
            std::uint64_t strict_after_initial { };
            std::uint64_t strict_before_observation { };
            std::uint64_t strict_after_observation { };
            std::uint64_t strict_after_force { };
            std::uint64_t strict_after_release { };
        };
        const auto run_required_read_sequence = [&] (
            const fsim::project::Optimization optimization,
            const fsim::app::SimulationEngine engine,
            const bool late_observation) {
            auto run_config = required_read_config;
            run_config.build.optimization = optimization;
            run_config.build.cache_path = directory
                / (optimization == fsim::project::Optimization::o0
                        ? "ordinary-required-read-o0-cache"
                        : "ordinary-required-read-o2-cache");
            fsim::diagnostic::Engine diagnostics;
            auto project = fsim::app::build_project(
                run_config, diagnostics);
            assert(project);
            const auto signal_a
                = project->design.find_signal("ordinary_required_read.a");
            const auto signal_b
                = project->design.find_signal("ordinary_required_read.b");
            const auto signal_y
                = project->design.find_signal("ordinary_required_read.y");
            assert(signal_a && signal_b && signal_y);
            fsim::app::Simulation simulation(
                std::move(*project), run_config.run.max_deltas, engine);
            if (engine == fsim::app::SimulationEngine::compiled) {
                simulation.await_all_native_compilation();
            }
            simulation.start();
            simulation.deposit_signal(*signal_a,
                fsim::runtime::PackedLogic4::from_msb_string("0"));
            simulation.deposit_signal(*signal_b,
                fsim::runtime::PackedLogic4::from_msb_string("1"));
            const auto strict_at_start
                = fsim::app::application_detail::
                    required_direct_read_entry_resume_count();
            const auto initial = simulation.run(0U);
            assert(initial.status == fsim::runtime::RunStatus::time_limit);
            const auto strict_after_initial
                = fsim::app::application_detail::
                    required_direct_read_entry_resume_count();

            auto strict_before_observation
                = fsim::app::application_detail::
                    required_direct_read_entry_resume_count();
            if (late_observation) {
                (void)simulation.read_signal(*signal_a);
            }
            simulation.deposit_signal(*signal_a,
                fsim::runtime::PackedLogic4::from_msb_string("1"));
            const auto deposited = simulation.run(0U);
            assert(deposited.status == fsim::runtime::RunStatus::time_limit);
            const auto strict_after_observation
                = fsim::app::application_detail::
                    required_direct_read_entry_resume_count();
            auto strict_after_force = strict_after_observation;
            auto strict_after_release = strict_after_observation;
            if (!late_observation) {
                simulation.force_signal(*signal_a,
                    fsim::runtime::PackedLogic4::from_msb_string("0"));
                assert(simulation.run(0U).status
                    == fsim::runtime::RunStatus::time_limit);
                strict_after_force
                    = fsim::app::application_detail::
                        required_direct_read_entry_resume_count();
                simulation.release_signal(*signal_a);
                assert(simulation.run(0U).status
                    == fsim::runtime::RunStatus::time_limit);
                strict_after_release
                    = fsim::app::application_detail::
                        required_direct_read_entry_resume_count();
            }
            const auto final = simulation.run();
            return RequiredReadCapture {
                final,
                simulation.read_signal(*signal_a).to_msb_string(),
                simulation.read_signal(*signal_b).to_msb_string(),
                simulation.read_signal(*signal_y).to_msb_string(),
                strict_at_start,
                strict_after_initial,
                strict_before_observation,
                strict_after_observation,
                strict_after_force,
                strict_after_release
            };
        };

        for (const auto optimization : {
                 fsim::project::Optimization::o0,
                 fsim::project::Optimization::o2 }) {
            const auto interpreted = run_required_read_sequence(
                optimization,
                fsim::app::SimulationEngine::interpreter, false);
            const auto strict_before
                = fsim::app::application_detail::
                    required_direct_read_entry_resume_count();
            const auto compiled = run_required_read_sequence(
                optimization,
                fsim::app::SimulationEngine::compiled, false);
            const auto strict_after
                = fsim::app::application_detail::
                    required_direct_read_entry_resume_count();
            assert(strict_after - strict_before >= 4U);
            assert(compiled.strict_after_initial
                > compiled.strict_at_start);
            assert(compiled.strict_after_observation
                > compiled.strict_after_initial);
            assert(compiled.strict_after_force
                > compiled.strict_after_observation);
            assert(compiled.strict_after_release
                > compiled.strict_after_force);
            assert(compiled.result.status == interpreted.result.status);
            assert(compiled.result.time == interpreted.result.time);
            assert(compiled.result.delta == interpreted.result.delta);
            assert(compiled.a == interpreted.a);
            assert(compiled.b == interpreted.b);
            assert(compiled.y == interpreted.y);
            assert(compiled.y == "0");

            const auto late_before
                = fsim::app::application_detail::
                    required_direct_read_entry_resume_count();
            const auto observed = run_required_read_sequence(
                optimization,
                fsim::app::SimulationEngine::compiled, true);
            assert(observed.strict_before_observation > late_before);
            assert(observed.strict_after_observation
                == observed.strict_before_observation);
            assert(observed.result.status == interpreted.result.status);
            assert(observed.y == "0");
        }
    }

    {
        auto alias_config = base_config();
        alias_config.project.name = "ordinary-required-read-alias-fallback";
        alias_config.project.top = "sv:work.ordinary_alias_fallback";
        alias_config.build.optimization = fsim::project::Optimization::o2;
        alias_config.build.cache_path
            = directory / "ordinary-required-read-alias-cache";
        alias_config.source_sets.clear();
        const auto alias_source
            = directory / "ordinary-required-read-alias-fallback.sv";
        {
            std::ofstream output(alias_source);
            assert(output);
            output
                << "module ordinary_alias_fallback;\n"
                << "  logic [3:0] values [0:1];\n"
                << "  logic [3:0] y;\n"
                << "  always @(values[0]) y <= values[0];\n"
                << "  initial begin values[0] = 4'h0; #1 values[0] = 4'hA;"
                << " #10 $finish; end\n"
                << "endmodule\n";
            assert(output);
        }
        fsim::project::SourceSet alias_sources;
        alias_sources.language = fsim::project::Language::system_verilog;
        alias_sources.standard = "2017";
        alias_sources.library = "work";
        alias_sources.files.push_back(alias_source);
        alias_config.source_sets.push_back(std::move(alias_sources));
        fsim::diagnostic::Engine alias_diagnostics;
        auto alias_project = fsim::app::build_project(
            alias_config, alias_diagnostics);
        assert(alias_project);
        const auto alias_y
            = alias_project->design.find_signal("ordinary_alias_fallback.y");
        assert(alias_y);
        const auto alias_before
            = fsim::app::application_detail::
                required_direct_read_entry_resume_count();
        const auto alias_capture = capture_simulation(
            std::move(*alias_project),
            fsim::app::SimulationEngine::compiled);
        const auto alias_after
            = fsim::app::application_detail::
                required_direct_read_entry_resume_count();
        assert(alias_after == alias_before);
        assert(alias_capture.compiled_processes != 0U);
        assert(alias_capture.result.status == fsim::runtime::RunStatus::stopped);
        assert(alias_capture.final_values.at(*alias_y) == "1010");
    }
    test_region_register_copyback_preflight();
    test_logic9_callback_ingress_canonicalization();
    test_ordered_cohort_postnative_failure();
    test_cohort_projected_writes_and_update_batches();
    test_fused_static_simulation();
#endif
    auto config = base_config();
    auto differential_config = config;
    std::erase_if(
        differential_config.source_sets,
        [](const fsim::project::SourceSet& source_set) {
            return source_set.language
                == fsim::project::Language::systemc;
        });
    differential_config.build.cache_path = directory / "differential-cache";
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        differential_config.build.optimization = optimization;
        fsim::diagnostic::Engine differential_diagnostics;
        auto reference_project = fsim::app::build_project(
            differential_config, differential_diagnostics);
        auto hybrid_project = fsim::app::build_project(
            differential_config, differential_diagnostics);
        assert(reference_project);
        assert(hybrid_project);
        const auto reference = capture_simulation(
            std::move(*reference_project),
            fsim::app::SimulationEngine::interpreter);
        const auto hybrid = capture_simulation(
            std::move(*hybrid_project),
            fsim::app::SimulationEngine::compiled);
        compare_captures(reference, hybrid);
#if defined(FSIM_HAS_LLVM)
        assert(hybrid.process_count == 2);
        assert(
            hybrid.compiled_processes
            == hybrid.process_count);
        assert(hybrid.compiled_modules == 2);
#endif

        auto warm_project = fsim::app::build_project(
            differential_config, differential_diagnostics);
        assert(warm_project);
        const auto warm = capture_simulation(
            std::move(*warm_project),
            fsim::app::SimulationEngine::compiled);
        compare_captures(reference, warm);
#if defined(FSIM_HAS_LLVM)
        assert(
            hybrid.native_cache.misses
            == hybrid.compiled_modules);
        assert(
            hybrid.native_cache.stores
            == hybrid.compiled_modules);
        assert(hybrid.native_cache.hits == 0);
        assert(
            warm.native_cache.hits
            == warm.compiled_modules);
        assert(warm.native_cache.misses == 0);
        assert(warm.native_cache.load_failures == 0);
        assert(warm.native_cache.store_failures == 0);
#else
        assert(
            hybrid.native_cache
            == fsim::app::NativeCacheStatistics { });
        assert(
            warm.native_cache
            == fsim::app::NativeCacheStatistics { });
#endif
    }

#if defined(FSIM_HAS_LLVM)
    constexpr std::size_t hot_cell_assignment_count = 129U;
    auto hot_cell_config = base_config();
    hot_cell_config.project.name = "executor-hot-cell-pool-test";
    hot_cell_config.project.top = "sv:work.hot_cell_pool";
    hot_cell_config.build.optimization = fsim::project::Optimization::o2;
    hot_cell_config.build.cache_path
        = directory / "executor-hot-cell-pool-cache";
    hot_cell_config.source_sets.clear();
    fsim::project::SourceSet hot_cell_sources;
    hot_cell_sources.language = fsim::project::Language::system_verilog;
    hot_cell_sources.standard = "2017";
    hot_cell_sources.library = "work";
    const auto hot_cell_source = directory / "executor-hot-cell-pool.sv";
    std::ostringstream hot_cell_source_text;
    hot_cell_source_text
        << "module hot_cell_leaf(input logic a, input logic b, output logic y);\n"
        << "  always_comb y = a & b;\n"
        << "endmodule\n"
        << "module hot_cell_pool;\n"
        << "  logic a;\n"
        << "  logic b;\n";
    for (std::size_t index = 0;
         index < hot_cell_assignment_count; ++index) {
        hot_cell_source_text
            << "  logic output_" << index << ";\n";
    }
    for (std::size_t index = 0;
         index < hot_cell_assignment_count; ++index) {
        hot_cell_source_text
            << "  hot_cell_leaf u" << index
            << " (.a(a), .b(b), .y(output_" << index << "));\n";
    }
    hot_cell_source_text
        << "  initial begin\n"
        << "    a = 1'b0;\n"
        << "    b = 1'b0;\n"
        << "    #1 a = 1'b1; b = 1'b1;\n"
        << "    #1 b = 1'b0;\n"
        << "    #1 $finish;\n"
        << "  end\n"
        << "endmodule\n";
    {
        std::ofstream hot_cell_output(hot_cell_source);
        assert(hot_cell_output);
        hot_cell_output << hot_cell_source_text.str();
        assert(hot_cell_output);
    }
    hot_cell_sources.files.push_back(hot_cell_source);
    hot_cell_config.source_sets.push_back(std::move(hot_cell_sources));
    fsim::diagnostic::Engine hot_cell_diagnostics;
    auto hot_cell_reference_project = fsim::app::build_project(
        hot_cell_config, hot_cell_diagnostics);
    auto hot_cell_compiled_project = fsim::app::build_project(
        hot_cell_config, hot_cell_diagnostics);
    assert(hot_cell_reference_project);
    assert(hot_cell_compiled_project);
    std::vector<fsim::runtime::simir::SignalId> hot_cell_outputs;
    hot_cell_outputs.reserve(hot_cell_assignment_count);
    for (std::size_t index = 0;
         index < hot_cell_assignment_count; ++index) {
        const auto output = hot_cell_reference_project->design.find_signal(
            "hot_cell_pool.output_" + std::to_string(index));
        assert(output);
        hot_cell_outputs.push_back(*output);
    }
    const auto hot_cell_reference = capture_simulation(
        std::move(*hot_cell_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto hot_cell_compiled = capture_simulation(
        std::move(*hot_cell_compiled_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(hot_cell_reference, hot_cell_compiled);
    assert(hot_cell_compiled.process_count >= hot_cell_assignment_count);
    assert(
        hot_cell_compiled.compiled_processes
        >= hot_cell_assignment_count);
    assert(
        hot_cell_compiled.result.status
        == fsim::runtime::RunStatus::stopped);
    for (const auto output : hot_cell_outputs) {
        assert(hot_cell_compiled.final_values.at(output) == "0");
        assert(std::ranges::any_of(
            hot_cell_compiled.changes,
            [output](const auto& change) {
                return std::get<0>(change) == output
                    && std::get<1>(change) == "1";
            }));
    }
#endif

#if defined(FSIM_HAS_LLVM)
    {
        using namespace fsim::runtime::simir;
        const auto classify = [](const std::initializer_list<Operation> operations) {
            Process process;
            process.operations = operations;
            return fsim::app::application_detail::has_dynamic_wait_backedge(process);
        };
        assert(classify({ WaitFor { 1U }, Jump { 0U } }));
        assert(classify({ WaitOn { { 0U }, { EdgeKind::posedge } }, Jump { 0U } }));
        assert(classify({ WaitFor { 0U }, Branch { 0U, 0U, 2U }, Halt { } }));
        assert(!classify({ WaitFor { 1U }, Halt { } }));
        assert(!classify({ WaitFor { 1U }, Jump { 1U } }));
        assert(!classify({ WaitOn { }, Jump { 0U } }));
        assert(!classify({ Jump { 0U } }));

        // Cross the general large-design selection threshold with one-shot
        // waits. Only the clock and edge-driven sampling loop recur; their
        // admission must not depend on module names or operation count.
        const auto loop_source = directory / "dynamic-loop-admission.sv";
        {
            std::ofstream output(loop_source);
            assert(output);
            output << "module dynamic_loop_admission;\n"
                   << "  logic clock = 0;\n"
                   << "  integer samples = 0;\n"
                   << "  always #1 clock = ~clock;\n"
                   << "  initial forever @(posedge clock) samples = samples + 1;\n"
                   << "  initial begin #8 $finish; end\n";
            for (std::size_t index = 0U; index < 128U; ++index) {
                output << "  initial begin #1000; end\n";
            }
            output << "endmodule\n";
            assert(output);
        }
        auto loop_config = config;
        loop_config.project.top = "sv:work.dynamic_loop_admission";
        loop_config.source_sets.clear();
        fsim::project::SourceSet loop_sources;
        loop_sources.language = fsim::project::Language::system_verilog;
        loop_sources.standard = "2017";
        loop_sources.library = "work";
        loop_sources.files = { loop_source };
        loop_config.source_sets.push_back(std::move(loop_sources));
        for (const auto optimization : { fsim::project::Optimization::o0,
                 fsim::project::Optimization::o2 }) {
            loop_config.build.optimization = optimization;
            loop_config.build.cache_path = directory / (optimization
                    == fsim::project::Optimization::o0
                ? "dynamic-loop-o0-cache" : "dynamic-loop-o2-cache");
            fsim::diagnostic::Engine diagnostics;
            auto project = fsim::app::build_project(loop_config, diagnostics);
            assert(project);
            const auto samples = project->design.find_signal(
                "dynamic_loop_admission.samples");
            assert(samples);
            auto compiled_project = *project;
            const auto reference = capture_simulation(std::move(*project),
                fsim::app::SimulationEngine::interpreter);
            const auto compiled = capture_simulation(std::move(compiled_project),
                fsim::app::SimulationEngine::compiled);
            compare_captures(reference, compiled);
            assert(compiled.process_count >= 130U);
            assert(compiled.compiled_processes == 2U);
            assert(compiled.result.status == fsim::runtime::RunStatus::stopped);
            assert(compiled.final_values.at(*samples)
                == "00000000000000000000000000000100");
        }
    }
#endif

    struct CapturedAssertion {
        fsim::runtime::simir::ProcessId process { };
        fsim::runtime::simir::InstructionIndex instruction { };
        fsim::runtime::simir::AssertionSeverity severity {
            fsim::runtime::simir::AssertionSeverity::error
        };
        fsim::runtime::simir::SourceLocation source;
        std::string message;
        std::size_t compiled_processes { };
        std::size_t compiled_modules { };
    };
    auto assertion_config = config;
    assertion_config.project.name = "assertion-differential";
    assertion_config.project.top = "vhdl:work.assertion_test(rtl)";
    assertion_config.build.cache_path = directory / "assertion-cache";
    assertion_config.source_sets.clear();
    fsim::project::SourceSet assertion_sources;
    assertion_sources.language = fsim::project::Language::vhdl;
    assertion_sources.standard = "2008";
    assertion_sources.library = "work";
    assertion_sources.files.push_back(assertion_source);
    assertion_config.source_sets.push_back(std::move(assertion_sources));
    const auto capture_assertion =
        [&](const fsim::project::Optimization optimization,
            const fsim::app::SimulationEngine engine) {
            assertion_config.build.optimization = optimization;
            fsim::diagnostic::Engine assertion_diagnostics;
            auto project = fsim::app::build_project(assertion_config, assertion_diagnostics);
            assert(project);
            fsim::app::Simulation simulation(
                std::move(*project), assertion_config.run.max_deltas, engine);
            CapturedAssertion captured;
            captured.compiled_processes = simulation.compiled_process_count();
            captured.compiled_modules = simulation.compiled_module_count();
            try {
                (void)simulation.run();
            } catch (const fsim::runtime::simir::AssertionError& error) {
                captured.process = error.process();
                captured.instruction = error.instruction();
                captured.severity = error.severity();
                captured.source = error.source();
                captured.message = error.what();
                return captured;
            }
            throw std::runtime_error("false assertion completed successfully");
        };
    const auto assertion_reference = capture_assertion(
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter);
    const auto compare_assertion =
        [&](const CapturedAssertion& candidate) {
            assert(candidate.process == assertion_reference.process);
            assert(candidate.instruction == assertion_reference.instruction);
            assert(candidate.severity == assertion_reference.severity);
            assert(candidate.source.path == assertion_reference.source.path);
            assert(candidate.source.line == assertion_reference.source.line);
            assert(candidate.source.column == assertion_reference.source.column);
            assert(candidate.message == assertion_reference.message);
        };
    assert(
        assertion_reference.severity
        == fsim::runtime::simir::AssertionSeverity::failure);
    assert(same_source_path(
        assertion_reference.source.path, assertion_source));
    assert(assertion_reference.source.line == 9);
    assert(assertion_reference.source.column == 5);
    assert(
        assertion_reference.message.find("cross-engine mismatch")
        != std::string::npos);
    assert(assertion_reference.compiled_processes == 0);
    assert(assertion_reference.compiled_modules == 0);
    const auto assertion_o0 = capture_assertion(
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::compiled);
    const auto assertion_o2 = capture_assertion(
        fsim::project::Optimization::o2,
        fsim::app::SimulationEngine::compiled);
    compare_assertion(assertion_o0);
    compare_assertion(assertion_o2);
#if defined(FSIM_HAS_LLVM)
    assert(assertion_o0.compiled_processes == 1);
    assert(assertion_o0.compiled_modules == 1);
    assert(assertion_o2.compiled_processes == 1);
    assert(assertion_o2.compiled_modules == 1);
#else
    assert(assertion_o0.compiled_processes == 0);
    assert(assertion_o0.compiled_modules == 0);
    assert(assertion_o2.compiled_processes == 0);
    assert(assertion_o2.compiled_modules == 0);
#endif

    auto scheduled_config = config;
    scheduled_config.project.name = "scheduled-write-test";
    scheduled_config.project.top = "sv:work.scheduled";
    scheduled_config.build.optimization = fsim::project::Optimization::o2;
    scheduled_config.build.cache_path = directory / "scheduled-write-cache";
    scheduled_config.source_sets.clear();
    fsim::project::SourceSet scheduled_sources;
    scheduled_sources.language = fsim::project::Language::system_verilog;
    scheduled_sources.standard = "2017";
    scheduled_sources.library = "work";
    scheduled_sources.files.push_back(scheduled_source);
    scheduled_config.source_sets.push_back(
        std::move(scheduled_sources));
    fsim::diagnostic::Engine scheduled_diagnostics;
    auto scheduled_reference_project = fsim::app::build_project(
        scheduled_config, scheduled_diagnostics);
    auto scheduled_hybrid_project = fsim::app::build_project(
        scheduled_config, scheduled_diagnostics);
    assert(scheduled_reference_project);
    assert(scheduled_hybrid_project);
    const auto scheduled_reference = capture_simulation(
        std::move(*scheduled_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto scheduled_hybrid = capture_simulation(
        std::move(*scheduled_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        scheduled_reference, scheduled_hybrid);
    assert(scheduled_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
    assert(scheduled_hybrid.compiled_processes == 1);
    assert(scheduled_hybrid.compiled_modules == 1);
#endif
    assert(
        scheduled_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(scheduled_hybrid.result.time == 3);
    assert(scheduled_hybrid.final_values.size() == 1);
    assert(scheduled_hybrid.final_values.front() == "1");
    assert(scheduled_hybrid.changes.size() == 2);
    assert(
        std::get<1>(scheduled_hybrid.changes.front())
        == "0");
    assert(
        std::get<2>(scheduled_hybrid.changes.front())
        == 0);
    assert(
        std::get<3>(scheduled_hybrid.changes.front())
        == 0);
    assert(
        std::get<1>(scheduled_hybrid.changes.back())
        == "1");
    assert(
        std::get<2>(scheduled_hybrid.changes.back())
        == 2);
    assert(
        std::get<3>(scheduled_hybrid.changes.back())
        == 0);

    auto overflow_config = scheduled_config;
    overflow_config.project.name = "scheduled-write-overflow-test";
    overflow_config.project.top = "sv:work.scheduled_overflow";
    overflow_config.build.cache_path = directory / "scheduled-write-overflow-cache";
    for (const auto engine : {
             fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::compiled }) {
        fsim::diagnostic::Engine overflow_diagnostics;
        auto overflow_project = fsim::app::build_project(
            overflow_config, overflow_diagnostics);
        assert(overflow_project);
        fsim::app::Simulation overflow_simulation(
            std::move(*overflow_project),
            overflow_config.run.max_deltas,
            engine);
#if defined(FSIM_HAS_LLVM)
        assert(
            overflow_simulation.compiled_process_count()
            == (engine == fsim::app::SimulationEngine::compiled
                    ? 1U
                    : 0U));
#endif
        const auto overflow_q = overflow_simulation.find_signal("q");
        assert(overflow_q);
        bool overflow_thrown = false;
        try {
            (void)overflow_simulation.run();
        } catch (const std::overflow_error& exception) {
            overflow_thrown = std::string_view { exception.what() }
                == "simulation time overflow while scheduling event";
        }
        assert(overflow_thrown);
        assert(overflow_simulation.poisoned());
        assert(!overflow_simulation.finished());
        assert(overflow_simulation.now() == 1);
        assert(
            overflow_simulation.read_signal(*overflow_q)
                .to_msb_string()
            == "X");
    }

    auto sensitivity_config = config;
    sensitivity_config.project.name = "sensitivity-wakeup-test";
    sensitivity_config.project.top = "sv:work.sensitivity";
    sensitivity_config.build.optimization = fsim::project::Optimization::o2;
    sensitivity_config.build.cache_path = directory / "sensitivity-cache";
    sensitivity_config.source_sets.clear();
    fsim::project::SourceSet sensitivity_sources;
    sensitivity_sources.language = fsim::project::Language::system_verilog;
    sensitivity_sources.standard = "2017";
    sensitivity_sources.library = "work";
    sensitivity_sources.files.push_back(sensitivity_source);
    sensitivity_config.source_sets.push_back(
        std::move(sensitivity_sources));
    fsim::diagnostic::Engine sensitivity_diagnostics;
    auto sensitivity_reference_project = fsim::app::build_project(
        sensitivity_config, sensitivity_diagnostics);
    auto sensitivity_hybrid_project = fsim::app::build_project(
        sensitivity_config, sensitivity_diagnostics);
    assert(sensitivity_reference_project);
    assert(sensitivity_hybrid_project);
    const auto sensitivity_trigger = sensitivity_reference_project->design.find_signal(
        "sensitivity.trigger");
    const auto sensitivity_observed = sensitivity_reference_project->design.find_signal(
        "sensitivity.observed");
    const auto sensitivity_dynamic_observed = sensitivity_reference_project->design.find_signal(
        "sensitivity.dynamic_observed");
    assert(sensitivity_trigger);
    assert(sensitivity_observed);
    assert(sensitivity_dynamic_observed);
    const auto sensitivity_reference = capture_simulation(
        std::move(*sensitivity_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto sensitivity_hybrid = capture_simulation(
        std::move(*sensitivity_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        sensitivity_reference, sensitivity_hybrid);
    assert(sensitivity_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
    assert(sensitivity_hybrid.compiled_processes == 3);
#endif
    assert(
        sensitivity_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(sensitivity_hybrid.result.time == 3);
    // SystemVerilog Active/NBA rounds do not advance the generic/VHDL delta.
    const decltype(sensitivity_reference.changes)
        expected_sensitivity_changes = {
            { *sensitivity_trigger, "0", 0, 0 },
            { *sensitivity_trigger, "1", 1, 0 },
            { *sensitivity_dynamic_observed, "1", 1, 0 },
            { *sensitivity_observed, "1", 1, 0 },
            { *sensitivity_trigger, "0", 2, 0 },
            { *sensitivity_dynamic_observed, "0", 2, 0 },
        };
    assert(
        sensitivity_reference.changes
        == expected_sensitivity_changes);
    assert(
        sensitivity_hybrid.changes
        == expected_sensitivity_changes);
    assert((
        sensitivity_hybrid.final_values
        == std::vector<std::string> { "0", "1", "0" }));
#if defined(FSIM_HAS_LLVM)
    assert(sensitivity_hybrid.native_cache.hits == 0);
    assert(
        sensitivity_hybrid.native_cache.misses
        == sensitivity_hybrid.compiled_modules);
    assert(
        sensitivity_hybrid.native_cache.stores
        == sensitivity_hybrid.compiled_modules);
    auto sensitivity_warm_project = fsim::app::build_project(
        sensitivity_config, sensitivity_diagnostics);
    assert(sensitivity_warm_project);
    const auto sensitivity_warm = capture_simulation(
        std::move(*sensitivity_warm_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        sensitivity_reference, sensitivity_warm);
    assert(sensitivity_warm.compiled_processes == 3);
    assert(
        sensitivity_warm.native_cache.hits
        == sensitivity_warm.compiled_modules);
    assert(sensitivity_warm.native_cache.misses == 0);
    assert(sensitivity_warm.native_cache.stores == 0);
#endif

    auto vhdl_wait_config = config;
    vhdl_wait_config.project.name = "vhdl-explicit-wait-test";
    vhdl_wait_config.project.top = "vhdl:work.vhdl_wait(rtl)";
    vhdl_wait_config.build.optimization = fsim::project::Optimization::o2;
    vhdl_wait_config.build.cache_path = directory / "vhdl-wait-cache";
    vhdl_wait_config.source_sets.clear();
    fsim::project::SourceSet vhdl_wait_sources;
    vhdl_wait_sources.language = fsim::project::Language::vhdl;
    vhdl_wait_sources.standard = "2008";
    vhdl_wait_sources.library = "work";
    vhdl_wait_sources.files.push_back(vhdl_wait_source);
    vhdl_wait_config.source_sets.push_back(
        std::move(vhdl_wait_sources));
    fsim::diagnostic::Engine vhdl_wait_diagnostics;
    auto vhdl_wait_reference_project = fsim::app::build_project(
        vhdl_wait_config, vhdl_wait_diagnostics);
    auto vhdl_wait_hybrid_project = fsim::app::build_project(
        vhdl_wait_config, vhdl_wait_diagnostics);
    assert(vhdl_wait_reference_project);
    assert(vhdl_wait_hybrid_project);
    const auto vhdl_wait_q = vhdl_wait_reference_project->design.find_signal(
        "vhdl_wait.q");
    assert(vhdl_wait_q);
    const auto vhdl_wait_reference = capture_simulation(
        std::move(*vhdl_wait_reference_project),
        fsim::app::SimulationEngine::interpreter,
        4);
    const auto vhdl_wait_hybrid = capture_simulation(
        std::move(*vhdl_wait_hybrid_project),
        fsim::app::SimulationEngine::compiled,
        4);
    compare_captures(vhdl_wait_reference, vhdl_wait_hybrid);
    assert(
        vhdl_wait_hybrid.result.status
        == fsim::runtime::RunStatus::time_limit);
    assert(vhdl_wait_hybrid.result.time == 4);
    assert(vhdl_wait_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
    assert(vhdl_wait_hybrid.compiled_processes == 1);
    assert(vhdl_wait_hybrid.compiled_modules == 1);
#endif
    const decltype(vhdl_wait_reference.changes)
        expected_vhdl_wait_changes = {
            { *vhdl_wait_q, "0", 0, 0 },
            { *vhdl_wait_q, "1", 1, 0 },
            { *vhdl_wait_q, "0", 2, 0 },
            { *vhdl_wait_q, "1", 3, 0 },
            { *vhdl_wait_q, "0", 4, 0 },
        };
    assert(
        vhdl_wait_reference.changes
        == expected_vhdl_wait_changes);
    assert(
        vhdl_wait_hybrid.changes
        == expected_vhdl_wait_changes);
    assert((
        vhdl_wait_hybrid.final_values
        == std::vector<std::string> { "0" }));

    auto wildcard_config = config;
    wildcard_config.project.name = "wildcard-sensitivity-test";
    wildcard_config.project.top = "sv:work.wildcard_app";
    wildcard_config.build.optimization = fsim::project::Optimization::o2;
    wildcard_config.build.cache_path = directory / "wildcard-cache";
    wildcard_config.source_sets.clear();
    fsim::project::SourceSet wildcard_sources;
    wildcard_sources.language = fsim::project::Language::system_verilog;
    wildcard_sources.standard = "2017";
    wildcard_sources.library = "work";
    wildcard_sources.files.push_back(wildcard_source);
    wildcard_config.source_sets.push_back(
        std::move(wildcard_sources));
    fsim::diagnostic::Engine wildcard_diagnostics;
    auto wildcard_reference_project = fsim::app::build_project(
        wildcard_config, wildcard_diagnostics);
    auto wildcard_hybrid_project = fsim::app::build_project(
        wildcard_config, wildcard_diagnostics);
    assert(wildcard_reference_project);
    assert(wildcard_hybrid_project);
    const auto wildcard_a = wildcard_reference_project->design.find_signal(
        "wildcard_app.a");
    const auto wildcard_q = wildcard_reference_project->design.find_signal(
        "wildcard_app.q");
    const auto wildcard_y = wildcard_reference_project->design.find_signal(
        "wildcard_app.y");
    const auto wildcard_latched = wildcard_reference_project->design.find_signal(
        "wildcard_app.latched");
    assert(
        wildcard_a && wildcard_q && wildcard_y
        && wildcard_latched);
    const auto wildcard_reference = capture_simulation(
        std::move(*wildcard_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto wildcard_hybrid = capture_simulation(
        std::move(*wildcard_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(wildcard_reference, wildcard_hybrid);
    assert(
        wildcard_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(wildcard_hybrid.result.time == 2);
    assert(wildcard_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
    assert(wildcard_hybrid.compiled_processes == 4);
#endif
    // SystemVerilog event rounds stay within the current generic/VHDL delta.
    const decltype(wildcard_reference.changes)
        expected_wildcard_changes = {
            { *wildcard_a, "0", 0, 0 },
            { *wildcard_q, "0", 0, 0 },
            { *wildcard_y, "1", 0, 0 },
            { *wildcard_a, "1", 1, 0 },
            { *wildcard_q, "1", 1, 0 },
            { *wildcard_latched, "1", 1, 0 },
            { *wildcard_y, "0", 1, 0 },
        };
    assert(
        wildcard_reference.changes
        == expected_wildcard_changes);
    assert(
        wildcard_hybrid.changes
        == expected_wildcard_changes);
    assert((
        wildcard_hybrid.final_values
        == std::vector<std::string> { "1", "1", "0", "1" }));

    auto case_config = config;
    case_config.project.name = "case-statement-test";
    case_config.project.top = "sv:work.case_app";
    case_config.build.optimization = fsim::project::Optimization::o2;
    case_config.build.cache_path = directory / "case-cache";
    case_config.source_sets.clear();
    fsim::project::SourceSet case_sources;
    case_sources.language = fsim::project::Language::system_verilog;
    case_sources.standard = "2017";
    case_sources.library = "work";
    case_sources.files.push_back(case_source);
    case_config.source_sets.push_back(std::move(case_sources));
    fsim::diagnostic::Engine case_diagnostics;
    auto case_reference_project = fsim::app::build_project(case_config, case_diagnostics);
    auto case_hybrid_project = fsim::app::build_project(case_config, case_diagnostics);
    assert(case_reference_project);
    assert(case_hybrid_project);
    const auto case_selector = case_reference_project->design.find_signal(
        "case_app.selector");
    const auto case_result = case_reference_project->design.find_signal(
        "case_app.result");
    assert(case_selector && case_result);
    const auto case_reference = capture_simulation(
        std::move(*case_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto case_hybrid = capture_simulation(
        std::move(*case_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(case_reference, case_hybrid);
    assert(
        case_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(case_hybrid.result.time == 5);
    assert(case_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(case_hybrid.compiled_processes == 2);
#endif
    assert((
        case_hybrid.final_values
        == std::vector<std::string> {
            "11", "00", "1", "1", "1", "1", "0" }));
    // SystemVerilog event rounds keep these case updates in delta zero.
    assert(std::find(
               case_hybrid.changes.begin(),
               case_hybrid.changes.end(),
               std::tuple {
                   *case_result, std::string { "10" },
                   fsim::runtime::SimulationTick { 2 },
                   std::uint64_t { 0 } })
        != case_hybrid.changes.end());
    assert(std::find(
               case_hybrid.changes.begin(),
               case_hybrid.changes.end(),
               std::tuple {
                   *case_result, std::string { "11" },
                   fsim::runtime::SimulationTick { 3 },
                   std::uint64_t { 0 } })
        != case_hybrid.changes.end());

    auto conditional_config = config;
    conditional_config.project.name = "conditional-expression-test";
    conditional_config.project.top = "sv:work.conditional_app";
    conditional_config.build.optimization = fsim::project::Optimization::o2;
    conditional_config.build.cache_path = directory / "conditional-cache";
    conditional_config.source_sets.clear();
    fsim::project::SourceSet conditional_sources;
    conditional_sources.language = fsim::project::Language::system_verilog;
    conditional_sources.standard = "2017";
    conditional_sources.library = "work";
    conditional_sources.files.push_back(conditional_source);
    conditional_config.source_sets.push_back(
        std::move(conditional_sources));
    fsim::diagnostic::Engine conditional_diagnostics;
    auto conditional_reference_project = fsim::app::build_project(
        conditional_config, conditional_diagnostics);
    auto conditional_hybrid_project = fsim::app::build_project(
        conditional_config, conditional_diagnostics);
    assert(conditional_reference_project);
    assert(conditional_hybrid_project);
    const auto conditional_reference = capture_simulation(
        std::move(*conditional_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto conditional_hybrid = capture_simulation(
        std::move(*conditional_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        conditional_reference, conditional_hybrid);
    assert(
        conditional_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(conditional_hybrid.result.time == 4);
    assert(conditional_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(conditional_hybrid.compiled_processes == 2);
#endif
    assert((
        conditional_hybrid.final_values
        == std::vector<std::string> {
            "Z", "101Z", "100Z", "10XZ" }));

    auto comparison_config = config;
    comparison_config.project.name = "comparison-expression-test";
    comparison_config.project.top = "sv:work.comparison_app";
    comparison_config.build.optimization = fsim::project::Optimization::o2;
    comparison_config.build.cache_path = directory / "comparison-cache";
    comparison_config.source_sets.clear();
    fsim::project::SourceSet comparison_sources;
    comparison_sources.language = fsim::project::Language::system_verilog;
    comparison_sources.standard = "2017";
    comparison_sources.library = "work";
    comparison_sources.files.push_back(comparison_source);
    comparison_config.source_sets.push_back(
        std::move(comparison_sources));
    fsim::diagnostic::Engine comparison_diagnostics;
    auto comparison_reference_project = fsim::app::build_project(
        comparison_config, comparison_diagnostics);
    auto comparison_hybrid_project = fsim::app::build_project(
        comparison_config, comparison_diagnostics);
    assert(comparison_reference_project);
    assert(comparison_hybrid_project);
    const auto comparison_reference = capture_simulation(
        std::move(*comparison_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto comparison_hybrid = capture_simulation(
        std::move(*comparison_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(comparison_reference, comparison_hybrid);
    assert(
        comparison_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(comparison_hybrid.result.time == 4);
    assert(comparison_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(comparison_hybrid.compiled_processes == 2);
#endif
    assert((
        comparison_hybrid.final_values
        == std::vector<std::string> {
            "01Z0", "0011", "X", "X", "X", "X", "X", "0",
            "0", "1", "1", "X" }));

    auto logical_config = config;
    logical_config.project.name = "logical-expression-test";
    logical_config.project.top = "sv:work.logical_app";
    logical_config.build.optimization = fsim::project::Optimization::o2;
    logical_config.build.cache_path = directory / "logical-cache";
    logical_config.source_sets.clear();
    fsim::project::SourceSet logical_sources;
    logical_sources.language = fsim::project::Language::system_verilog;
    logical_sources.standard = "2017";
    logical_sources.library = "work";
    logical_sources.files.push_back(logical_source);
    logical_config.source_sets.push_back(
        std::move(logical_sources));
    fsim::diagnostic::Engine logical_diagnostics;
    auto logical_reference_project = fsim::app::build_project(
        logical_config, logical_diagnostics);
    auto logical_hybrid_project = fsim::app::build_project(
        logical_config, logical_diagnostics);
    assert(logical_reference_project);
    assert(logical_hybrid_project);
    const auto required_logical_signal =
        [&](const std::string_view name) {
            const auto signal = logical_reference_project->design.find_signal(
                std::string { "logical_app." } + std::string { name });
            assert(signal);
            return *signal;
        };
    const auto gate_buf = required_logical_signal("gate_buf");
    const auto gate_buf_second = required_logical_signal("gate_buf_second");
    const auto gate_not = required_logical_signal("gate_not");
    const auto gate_and = required_logical_signal("gate_and");
    const auto gate_nand = required_logical_signal("gate_nand");
    const auto gate_or = required_logical_signal("gate_or");
    const auto gate_nor = required_logical_signal("gate_nor");
    const auto gate_xor = required_logical_signal("gate_xor");
    const auto gate_xnor = required_logical_signal("gate_xnor");
    const auto logical_reference = capture_simulation(
        std::move(*logical_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto logical_hybrid = capture_simulation(
        std::move(*logical_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(logical_reference, logical_hybrid);
    assert(
        logical_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(logical_hybrid.result.time == 5);
    assert(logical_hybrid.process_count == 11);
#if defined(FSIM_HAS_LLVM)
    assert(logical_hybrid.compiled_processes == 11);
#endif
    assert((
        logical_hybrid.final_values
        == std::vector<std::string> {
            "0010", "01", "001", "1", "1",
            "0", "1", "1", "0100", "0001",
            "0", "1", "1", "0", "1", "1", "0", "0", "1" }));
    const auto gate_changes =
        [&](const fsim::runtime::simir::SignalId signal) {
            std::vector<std::pair<
                std::string, fsim::runtime::SimulationTick>>
                changes;
            for (const auto& [changed, value, time, delta] :
                logical_reference.changes) {
                (void)delta;
                if (changed == signal)
                    changes.emplace_back(value, time);
            }
            return changes;
        };
    using GateChange = std::pair<std::string, fsim::runtime::SimulationTick>;
    const auto require_gate_changes =
        [&](const fsim::runtime::simir::SignalId signal,
            const std::string_view name,
            const std::vector<GateChange>& expected) {
            const auto actual = gate_changes(signal);
            if (actual != expected) {
                std::cerr << name << " gate changes:";
                for (const auto& [value, time] : actual) {
                    std::cerr << ' ' << value << '@' << time;
                }
                std::cerr << '\n';
            }
            assert(actual == expected);
        };
    require_gate_changes(gate_buf, "buf", { { "0", 1 } });
    require_gate_changes(
        gate_buf_second, "buf-second",
        { { "0", 1 }, { "X", 2 }, { "1", 4 } });
    require_gate_changes(gate_not, "not", { { "1", 0 } });
    require_gate_changes(gate_and, "and", { { "0", 0 } });
    require_gate_changes(gate_nand, "nand", { { "1", 0 } });
    require_gate_changes(
        gate_or, "or", { { "1", 0 }, { "X", 1 }, { "1", 2 } });
    require_gate_changes(
        gate_nor, "nor", { { "0", 0 }, { "X", 1 }, { "0", 2 } });
    require_gate_changes(
        gate_xor, "xor", { { "1", 0 }, { "X", 1 }, { "0", 4 } });
    require_gate_changes(
        gate_xnor, "xnor", { { "0", 0 }, { "X", 1 }, { "1", 4 } });

    auto arithmetic_config = config;
    arithmetic_config.project.name = "arithmetic-expression-test";
    arithmetic_config.project.top = "sv:work.arithmetic_app";
    arithmetic_config.build.optimization = fsim::project::Optimization::o2;
    arithmetic_config.build.cache_path = directory / "arithmetic-cache";
    arithmetic_config.source_sets.clear();
    fsim::project::SourceSet arithmetic_sources;
    arithmetic_sources.language = fsim::project::Language::system_verilog;
    arithmetic_sources.standard = "2017";
    arithmetic_sources.library = "work";
    arithmetic_sources.files.push_back(arithmetic_source);
    arithmetic_config.source_sets.push_back(
        std::move(arithmetic_sources));
    fsim::diagnostic::Engine arithmetic_diagnostics;
    auto arithmetic_reference_project = fsim::app::build_project(
        arithmetic_config, arithmetic_diagnostics);
    auto arithmetic_hybrid_project = fsim::app::build_project(
        arithmetic_config, arithmetic_diagnostics);
    assert(arithmetic_reference_project);
    assert(arithmetic_hybrid_project);
    const auto arithmetic_reference = capture_simulation(
        std::move(*arithmetic_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto arithmetic_hybrid = capture_simulation(
        std::move(*arithmetic_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(arithmetic_reference, arithmetic_hybrid);
    assert(
        arithmetic_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(arithmetic_hybrid.result.time == 4);
    assert(arithmetic_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(arithmetic_hybrid.compiled_processes == 2);
#endif
    assert((
        arithmetic_hybrid.final_values
        == std::vector<std::string> {
            "11001000", "00000111", "11000001", "01111000",
            "00011100", "00000100", "11001000", "00111000",
            "00000101", "11111101", "00000010", "00001000",
            "11110001", "11111111", "00000010", "0" }));

    auto select_concat_config = config;
    select_concat_config.project.name = "select-concat-test";
    select_concat_config.project.top = "sv:work.select_concat_app";
    select_concat_config.build.optimization = fsim::project::Optimization::o2;
    select_concat_config.build.cache_path = directory / "select-concat-cache";
    select_concat_config.source_sets.clear();
    fsim::project::SourceSet select_concat_sources;
    select_concat_sources.language = fsim::project::Language::system_verilog;
    select_concat_sources.standard = "2017";
    select_concat_sources.library = "work";
    select_concat_sources.files.push_back(
        select_concat_source);
    select_concat_config.source_sets.push_back(
        std::move(select_concat_sources));
    fsim::diagnostic::Engine select_concat_diagnostics;
    auto select_concat_reference_project = fsim::app::build_project(
        select_concat_config, select_concat_diagnostics);
    auto select_concat_hybrid_project = fsim::app::build_project(
        select_concat_config, select_concat_diagnostics);
    assert(select_concat_reference_project);
    assert(select_concat_hybrid_project);
    const auto select_concat_reference = capture_simulation(
        std::move(*select_concat_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto select_concat_hybrid = capture_simulation(
        std::move(*select_concat_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        select_concat_reference, select_concat_hybrid);
    assert(
        select_concat_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(select_concat_hybrid.result.time == 2);
    assert(select_concat_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(select_concat_hybrid.compiled_processes == 2);
#endif
    assert((
        select_concat_hybrid.final_values
        == std::vector<std::string> {
            "Z10100X1", "1100XZ01", "0", "0", "0",
            "Z101", "00XZ", "Z1010XZ01", "10XZ1111",
            "XZ10" }));

    auto vhdl_select_concat_config = config;
    vhdl_select_concat_config.project.name = "vhdl-select-concat-test";
    vhdl_select_concat_config.project.top = "vhdl:work.vhdl_select_concat_app(rtl)";
    vhdl_select_concat_config.build.optimization = fsim::project::Optimization::o2;
    vhdl_select_concat_config.build.cache_path = directory / "vhdl-select-concat-cache";
    vhdl_select_concat_config.source_sets.clear();
    fsim::project::SourceSet vhdl_select_concat_sources;
    vhdl_select_concat_sources.language = fsim::project::Language::vhdl;
    vhdl_select_concat_sources.standard = "2008";
    vhdl_select_concat_sources.library = "work";
    vhdl_select_concat_sources.files.push_back(
        vhdl_select_concat_source);
    vhdl_select_concat_config.source_sets.push_back(
        std::move(vhdl_select_concat_sources));
    fsim::diagnostic::Engine vhdl_select_concat_diagnostics;
    auto vhdl_select_concat_reference_project = fsim::app::build_project(
        vhdl_select_concat_config,
        vhdl_select_concat_diagnostics);
    auto vhdl_select_concat_hybrid_project = fsim::app::build_project(
        vhdl_select_concat_config,
        vhdl_select_concat_diagnostics);
    assert(vhdl_select_concat_reference_project);
    assert(vhdl_select_concat_hybrid_project);
    const auto vhdl_select_concat_reference = capture_simulation(
        std::move(*vhdl_select_concat_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto vhdl_select_concat_hybrid = capture_simulation(
        std::move(*vhdl_select_concat_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        vhdl_select_concat_reference,
        vhdl_select_concat_hybrid);
    assert(
        vhdl_select_concat_hybrid.result.status
        == fsim::runtime::RunStatus::completed);
    assert(vhdl_select_concat_hybrid.result.time == 5);
    assert(vhdl_select_concat_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
    assert(vhdl_select_concat_hybrid.compiled_processes == 4);
#endif
    assert((
        vhdl_select_concat_hybrid.final_values
        == std::vector<std::string> {
            "1XZ0", "01Z1", "Z", "Z", "Z", "X",
            "1X", "1Z", "1X10Z1", "10XZ1110",
            "XZ10" }));

    auto vhdl_signed_config = config;
    vhdl_signed_config.project.name = "vhdl-signed-test";
    vhdl_signed_config.project.top = "vhdl:work.vhdl_signed_app(rtl)";
    vhdl_signed_config.build.optimization = fsim::project::Optimization::o2;
    vhdl_signed_config.build.cache_path = directory / "vhdl-signed-cache";
    vhdl_signed_config.source_sets.clear();
    fsim::project::SourceSet vhdl_signed_sources;
    vhdl_signed_sources.language = fsim::project::Language::vhdl;
    vhdl_signed_sources.standard = "2008";
    vhdl_signed_sources.library = "work";
    vhdl_signed_sources.files.push_back(vhdl_signed_source);
    vhdl_signed_config.source_sets.push_back(
        std::move(vhdl_signed_sources));
    fsim::diagnostic::Engine vhdl_signed_diagnostics;
    auto vhdl_signed_reference_project = fsim::app::build_project(
        vhdl_signed_config, vhdl_signed_diagnostics);
    auto vhdl_signed_hybrid_project = fsim::app::build_project(
        vhdl_signed_config, vhdl_signed_diagnostics);
    assert(vhdl_signed_reference_project);
    assert(vhdl_signed_hybrid_project);
    const auto vhdl_signed_reference = capture_simulation(
        std::move(*vhdl_signed_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto vhdl_signed_hybrid = capture_simulation(
        std::move(*vhdl_signed_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        vhdl_signed_reference, vhdl_signed_hybrid);
    assert(
        vhdl_signed_hybrid.result.status
        == fsim::runtime::RunStatus::completed);
    assert(vhdl_signed_hybrid.result.time == 0);
    assert(vhdl_signed_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
    assert(vhdl_signed_hybrid.compiled_processes == 3);
#endif
    assert((
        vhdl_signed_hybrid.final_values
        == std::vector<std::string> {
            "11111011", "00000011", "11111110", "11111000",
            "11110001", "11111111", "11111110", "00000001",
            "1", "11110110", "01111101", "11111101" }));

    auto conditional_statement_config = config;
    conditional_statement_config.project.name = "conditional-statement-test";
    conditional_statement_config.project.top = "sv:work.conditional_statement_app";
    conditional_statement_config.build.cache_path = directory / "conditional-statement-cache";
    conditional_statement_config.source_sets.clear();
    fsim::project::SourceSet conditional_statement_sources;
    conditional_statement_sources.language = fsim::project::Language::system_verilog;
    conditional_statement_sources.standard = "2017";
    conditional_statement_sources.library = "work";
    conditional_statement_sources.files.push_back(
        conditional_statement_source);
    conditional_statement_config.source_sets.push_back(
        std::move(conditional_statement_sources));
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        conditional_statement_config.build.optimization = optimization;
        fsim::diagnostic::Engine conditional_statement_diagnostics;
        auto conditional_statement_reference_project = fsim::app::build_project(
            conditional_statement_config,
            conditional_statement_diagnostics);
        auto conditional_statement_hybrid_project = fsim::app::build_project(
            conditional_statement_config,
            conditional_statement_diagnostics);
        if (!conditional_statement_reference_project
            || !conditional_statement_hybrid_project) {
            fsim::diagnostic::print_text(
                std::cerr, conditional_statement_diagnostics);
        }
        assert(conditional_statement_reference_project);
        assert(conditional_statement_hybrid_project);
        const auto conditional_statement_reference = capture_simulation(
            std::move(*conditional_statement_reference_project),
            fsim::app::SimulationEngine::interpreter);
        const auto conditional_statement_hybrid = capture_simulation(
            std::move(*conditional_statement_hybrid_project),
            fsim::app::SimulationEngine::compiled);
        compare_captures(
            conditional_statement_reference,
            conditional_statement_hybrid);
        assert(
            conditional_statement_hybrid.result.status
            == fsim::runtime::RunStatus::stopped);
        assert(conditional_statement_hybrid.result.time == 3);
        assert(conditional_statement_hybrid.process_count == 5);
#if defined(FSIM_HAS_LLVM)
        assert(conditional_statement_hybrid.compiled_processes == 5);
#endif
        if (conditional_statement_hybrid.final_values
            != std::vector<std::string> {
                "1000", "0", "1", "0", "0001",
                "0011", "00", "011", "011", "0",
                "0011", "0101", "0100", "0001",
                "1", "1", "0" }) {
            for (const auto& value :
                conditional_statement_hybrid.final_values) {
                std::cerr << value << ' ';
            }
            std::cerr << '\n';
        }
        assert((
            conditional_statement_hybrid.final_values
            == std::vector<std::string> {
                "1000", "0", "1", "0", "0001",
                "0011", "00", "011", "011", "0",
                "0011", "0101", "0100", "0001",
                "1", "1", "0" }));
    }

    auto vhdl_conditional_statement_config = config;
    vhdl_conditional_statement_config.project.name = "vhdl-conditional-statement-test";
    vhdl_conditional_statement_config.project.top = "vhdl:work.vhdl_conditional_statement_app(rtl)";
    vhdl_conditional_statement_config.build.cache_path = directory / "vhdl-conditional-statement-cache";
    vhdl_conditional_statement_config.source_sets.clear();
    fsim::project::SourceSet vhdl_conditional_statement_sources;
    vhdl_conditional_statement_sources.language = fsim::project::Language::vhdl;
    vhdl_conditional_statement_sources.standard = "2008";
    vhdl_conditional_statement_sources.library = "work";
    vhdl_conditional_statement_sources.files.push_back(
        vhdl_conditional_statement_source);
    vhdl_conditional_statement_config.source_sets.push_back(
        std::move(vhdl_conditional_statement_sources));
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        vhdl_conditional_statement_config.build.optimization = optimization;
        fsim::diagnostic::Engine vhdl_conditional_statement_diagnostics;
        auto vhdl_conditional_statement_reference_project = fsim::app::build_project(
            vhdl_conditional_statement_config,
            vhdl_conditional_statement_diagnostics);
        auto vhdl_conditional_statement_hybrid_project = fsim::app::build_project(
            vhdl_conditional_statement_config,
            vhdl_conditional_statement_diagnostics);
        assert(vhdl_conditional_statement_reference_project);
        assert(vhdl_conditional_statement_hybrid_project);
        const auto vhdl_conditional_statement_reference = capture_simulation(
            std::move(*vhdl_conditional_statement_reference_project),
            fsim::app::SimulationEngine::interpreter);
        const auto vhdl_conditional_statement_hybrid = capture_simulation(
            std::move(*vhdl_conditional_statement_hybrid_project),
            fsim::app::SimulationEngine::compiled);
        compare_captures(
            vhdl_conditional_statement_reference,
            vhdl_conditional_statement_hybrid);
        assert(
            vhdl_conditional_statement_hybrid.result.status
            == fsim::runtime::RunStatus::completed);
        assert(vhdl_conditional_statement_hybrid.result.time == 4);
        assert(vhdl_conditional_statement_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
        assert(
            vhdl_conditional_statement_hybrid.compiled_processes
            == 3);
#endif
        if (vhdl_conditional_statement_hybrid.final_values
            != std::vector<std::string> {
                "U", "1", "1", "1", "1", "01",
                "0011", "00", "1", "1", "1", "1", "1",
                "1", "0", "1", "1", "1", "0" }) {
            for (const auto& value :
                vhdl_conditional_statement_hybrid.final_values) {
                std::cerr << value << ' ';
            }
            std::cerr << '\n';
        }
        assert((
            vhdl_conditional_statement_hybrid.final_values
            == std::vector<std::string> {
                "U", "1", "1", "1", "1", "01",
                "0011", "00", "1", "1", "1", "1", "1",
                "1", "0", "1", "1", "1", "0" }));
    }

    auto partial_group_config = config;
    partial_group_config.project.name = "partial-specialization-group-test";
    partial_group_config.project.top = "sv:work.partial_group";
    partial_group_config.build.optimization = fsim::project::Optimization::o2;
    partial_group_config.build.cache_path = directory / "partial-group-cache";
    partial_group_config.source_sets.clear();
    fsim::project::SourceSet partial_group_sources;
    partial_group_sources.language = fsim::project::Language::system_verilog;
    partial_group_sources.standard = "2017";
    partial_group_sources.library = "work";
    partial_group_sources.files.push_back(partial_group_source);
    partial_group_config.source_sets.push_back(
        std::move(partial_group_sources));
    fsim::diagnostic::Engine partial_group_diagnostics;
    auto partial_group_reference_project = fsim::app::build_project(
        partial_group_config, partial_group_diagnostics);
    auto partial_group_hybrid_project = fsim::app::build_project(
        partial_group_config, partial_group_diagnostics);
    assert(partial_group_reference_project);
    assert(partial_group_hybrid_project);
    const auto partial_group_reference = capture_simulation(
        std::move(*partial_group_reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto partial_group_hybrid = capture_simulation(
        std::move(*partial_group_hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(
        partial_group_reference, partial_group_hybrid);
    assert(partial_group_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(partial_group_hybrid.compiled_processes == 2);
    assert(
        partial_group_hybrid.native_cache.misses
        == partial_group_hybrid.compiled_modules);
    assert(
        partial_group_hybrid.native_cache.stores
        == partial_group_hybrid.compiled_modules);
#endif
    assert(
        partial_group_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(partial_group_hybrid.result.time == 1);
}

} // namespace fsim::test
