// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_impl.hpp"
#include "llvm_jit_compilation_contexts.hpp"
#include "../diagnostic/thread_cpu_clock.hpp"
#include "fsim/support/sha256.hpp"

#include <llvm/IR/Constants.h>

#include <algorithm>
#include <mutex>
#include <optional>

namespace fsim::compiler {
namespace {
std::mutex module_profile_output_mutex;
}

using namespace llvm_detail;
using runtime::simir::Process;
using runtime::simir::ReadSignal;
using runtime::simir::SignalReadKind;
using runtime::simir::ValueKind;

namespace {

void validate_required_direct_read_process(
    const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    for (const auto& stored : process.operations) {
        const auto* const read
            = runtime::simir::operation_get_if<ReadSignal>(&stored);
        if (read == nullptr) {
            continue;
        }
        if (read->kind != SignalReadKind::current
            || read->signal >= signal_widths.size()
            || signal_widths[read->signal] == 0U) {
            throw LlvmJitUnsupportedError(
                "required direct reads support only current signals with a known width");
        }
        if (!signal_value_kinds.empty()
            && (read->signal >= signal_value_kinds.size()
                || (signal_value_kinds[read->signal] != ValueKind::logic4
                    && signal_value_kinds[read->signal] != ValueKind::logic9))) {
            throw LlvmJitUnsupportedError(
                "required direct reads support only Logic4 and Logic9 signals");
        }
    }
}

} // namespace

void LlvmJit::add_process_module(
    const std::string_view module_identity,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    add_process_module_impl(module_identity, entries, signal_widths,
        signal_value_kinds, false, { });
}

void LlvmJit::add_process_module_impl(
    const std::string_view module_identity,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const bool region_known_logic4,
    const std::span<const std::uint32_t> prepared_output_signals,
    const std::span<const RegionDirectReadyLoweringBinding>
        direct_ready_bindings,
    const std::span<const std::uint64_t> successor_member_masks)
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (module_identity.empty()) {
        throw LlvmJitError("LLVM process module identity cannot be empty");
    }
    const std::string owned_module_identity { module_identity };
    if (entries.empty()) {
        throw LlvmJitError("LLVM process module cannot be empty");
    }
    if (!successor_member_masks.empty()
        && (prepared_output_signals.empty()
            || successor_member_masks.size()
                != prepared_output_signals.size())) {
        throw LlvmJitUnsupportedError(
            "prepared-output successor masks require one bounded slot per output");
    }
    if (!prepared_output_signals.empty()) {
        if (entries.size() != 1U
            || prepared_output_signals.size()
                > std::numeric_limits<std::uint32_t>::max()) {
            throw LlvmJitUnsupportedError(
                "prepared-output entry requires one bounded region body");
        }
        std::unordered_set<std::uint32_t> unique_prepared_outputs;
        unique_prepared_outputs.reserve(prepared_output_signals.size());
        for (std::size_t index = 0U;
             index < prepared_output_signals.size(); ++index) {
            const auto signal = prepared_output_signals[index];
            const auto successor_mask = successor_member_masks.empty()
                ? 0U : successor_member_masks[index];
            if ((successor_mask != 0U
                    && (successor_mask & (successor_mask + 1U)) != 0U)
                || signal >= signal_widths.size()
                || signal >= signal_value_kinds.size()
                || signal_widths[signal] == 0U
                || signal_widths[signal] > 64U
                || signal_value_kinds[signal] != ValueKind::logic4
                || !unique_prepared_outputs.insert(signal).second) {
                throw LlvmJitUnsupportedError(
                    "prepared-output entry requires unique narrow Logic4 signals");
            }
        }
    }
    if (!direct_ready_bindings.empty()) {
        if (prepared_output_signals.empty() || entries.size() != 1U
            || direct_ready_bindings.size()
                > std::numeric_limits<std::uint32_t>::max()) {
            throw LlvmJitUnsupportedError(
                "direct-ready entry requires one prepared region body");
        }
        std::unordered_set<std::uint32_t> unique_synthetic_signals;
        unique_synthetic_signals.reserve(direct_ready_bindings.size());
        for (const auto& binding : direct_ready_bindings) {
            const auto kind = static_cast<std::uint32_t>(binding.kind);
            if (kind > static_cast<std::uint32_t>(
                    RegionDirectReadyBindingKind::prefix_current)
                || binding.width == 0U || binding.width > 64U
                || binding.synthetic_signal_id >= signal_widths.size()
                || binding.synthetic_signal_id >= signal_value_kinds.size()
                || signal_widths[binding.synthetic_signal_id] != binding.width
                || signal_value_kinds[binding.synthetic_signal_id]
                    != ValueKind::logic4
                || !unique_synthetic_signals.insert(
                    binding.synthetic_signal_id).second) {
                throw LlvmJitUnsupportedError(
                    "direct-ready binding has an invalid Logic4 mapping");
            }
            // Non-readiness signal IDs stay in the physical RegionGraph
            // namespace. The compact synthetic planes were checked above;
            // runtime preflight authenticates the physical ID and width
            // against the immutable kernel binding.
            if (binding.kind
                    == RegionDirectReadyBindingKind::member_ready) {
                if (binding.width != 1U || binding.signal_id != 0U) {
                    throw LlvmJitUnsupportedError(
                        "direct-ready readiness binding has an invalid shape");
                }
            }
        }
    }
    const auto validate_tiered_read_dedup_process = [&](
        const JitProcessModuleEntry& entry) {
        if (!entry.tiered_read_dedup_safe) {
            return;
        }
        if (!tiered_read_dedup_eligible(
                entry, impl_->options.require_direct_read_signals)) {
            throw LlvmJitUnsupportedError(
                "tiered read deduplication requires strict reads and a fused Less-tier entry");
        }
        for (const auto& operation : entry.process->operations) {
            const auto* const read
                = runtime::simir::operation_get_if<ReadSignal>(&operation);
            if (read == nullptr) {
                continue;
            }
            if (signal_value_kinds.empty()
                || read->signal >= signal_widths.size()
                || signal_widths[read->signal] == 0U
                || signal_widths[read->signal] > 64U
                || read->signal >= signal_value_kinds.size()
                || signal_value_kinds[read->signal] != ValueKind::logic4) {
                throw LlvmJitUnsupportedError(
                    "tiered direct-read deduplication currently supports only narrow Logic4 signals");
            }
        }
    };
    for (const auto& entry : entries) {
        if (entry.process == nullptr) {
            throw LlvmJitError(
                "LLVM process module entry has no SimIR process");
        }
        if (impl_->options.require_direct_read_signals
            || entry.require_direct_read_signals) {
            validate_required_direct_read_process(
                *entry.process, signal_widths, signal_value_kinds);
        }
        validate_tiered_read_dedup_process(entry);
    }
    const auto validate_bound_literals = [&](
        const JitProcessModuleEntry& entry) {
        const auto register_value_kinds
            = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                entry.process->register_value_kinds);
        const auto& sites = entry.bound_literal_sites;
        if (sites.empty()) {
            return;
        }
        if (impl_->options.optimization != JitOptimizationLevel::o2
            || impl_->options.debug_instrumentation
            || !std::ranges::is_sorted(sites)
            || std::adjacent_find(sites.begin(), sites.end()) != sites.end()) {
            throw LlvmJitError("invalid bound-literal module entry");
        }
        for (const auto instruction : sites) {
            if (instruction >= entry.process->operations.size()) {
                throw LlvmJitError("bound-literal instruction is out of range");
            }
            const auto* literal = runtime::simir::operation_get_if<
                runtime::simir::LoadConstant>(
                &entry.process->operations[instruction]);
            if (literal == nullptr || literal->destination
                    >= entry.process->register_count
                || literal->value.width() == 0U
                || literal->value.width() > 64U
                || !literal->value.known_unsigned_value()
                || (!register_value_kinds.empty()
                    && register_value_kinds.size()
                        != entry.process->register_count)
                || (!register_value_kinds.empty()
                    && register_value_kinds[literal->destination]
                        != ValueKind::logic4)) {
                throw LlvmJitError(
                    "bound literal must be a known narrow Logic4 value");
            }
        }
    };
    const auto make_process_key = [&](const std::string_view symbol,
                                      const JitProcessModuleEntry& entry) {
        auto key = impl_->immutable_design_identity.empty()
                || !entry.masked_member_gates.empty()
            ? make_native_object_cache_key(
                  symbol, *entry.process, signal_widths, signal_value_kinds,
                  impl_->options.optimization,
                  impl_->options.debug_instrumentation,
                  impl_->options.require_direct_update_slots,
                  impl_->options.require_direct_read_signals
                      || entry.require_direct_read_signals,
                  entry.tiered_read_dedup_safe,
                  entry.signal_callback_ids_are_actual,
                  ProcessLoweringMode::four_state,
                  impl_->options.code_coverage_identity,
                  impl_->jit->getTargetTriple(),
                  impl_->jit->getDataLayout(), impl_->target_cpu,
                  impl_->target_features,
                  entry.bound_literal_sites)
            : make_immutable_design_object_cache_key(
                  impl_->immutable_design_identity,
                  owned_module_identity,
                  symbol,
                  impl_->options.optimization,
                  impl_->options.debug_instrumentation,
                  impl_->options.require_direct_update_slots,
                  impl_->options.require_direct_read_signals
                      || entry.require_direct_read_signals,
                  entry.tiered_read_dedup_safe,
                  entry.signal_callback_ids_are_actual,
                  ProcessLoweringMode::four_state,
                  impl_->options.code_coverage_identity,
                  impl_->jit->getTargetTriple(),
                  impl_->jit->getDataLayout(), impl_->target_cpu,
                  impl_->target_features,
                  entry.bound_literal_sites);
        if (!prepared_output_signals.empty()) {
            key += ":region-prepared-output-entry-v2-successor-sidecar-v1";
            for (std::size_t index = 0U;
                 index < prepared_output_signals.size(); ++index) {
                key += ":" + std::to_string(prepared_output_signals[index]);
                key += ":" + std::to_string(index);
                key += ":s" + std::to_string(
                    successor_member_masks.empty()
                        ? 0U : successor_member_masks[index]);
            }
        }
        if (!direct_ready_bindings.empty()) {
            key += ":direct-ready-window-v2";
            for (const auto& binding : direct_ready_bindings) {
                key += ":" + std::to_string(
                    static_cast<std::uint32_t>(binding.kind));
                key += ":" + std::to_string(binding.source_index);
                // Physical signal identity is authenticated by the region
                // wrapper before entry. The emitted direct-ready body uses
                // only the synthetic slot/register/width mapping below.
                key += ":" + std::to_string(binding.register_id);
                key += ":" + std::to_string(binding.synthetic_signal_id);
                key += ":" + std::to_string(binding.width);
            }
            if (!successor_member_masks.empty()) {
                key += ":direct-ready-successor-sidecar-v1";
            }
        }
        if (!entry.masked_member_gates.empty()) {
            if (entry.process->operations.size() < 3U) {
                throw LlvmJitError("masked native body is incomplete");
            }
            std::string identity = key + ":masked-members-v1";
            runtime::simir::InstructionIndex expected_begin { };
            for (std::size_t index = 0U; index < entry.masked_member_gates.size(); ++index) {
                const auto& gate = entry.masked_member_gates[index];
                if (gate.begin_instruction != expected_begin
                    || gate.begin_instruction >= gate.end_instruction
                    || gate.end_instruction > entry.process->operations.size() - 2U
                    || gate.activation_bit != index) {
                    throw LlvmJitError("invalid masked member gate layout");
                }
                for (auto instruction = gate.begin_instruction;
                     instruction < gate.end_instruction; ++instruction) {
                    const auto operation = entry.process->operations.expanded(instruction);
                    const auto valid_target = [&](const auto target) {
                        return target > instruction && target <= gate.end_instruction;
                    };
                    const auto* jump = runtime::simir::operation_get_if<runtime::simir::Jump>(&operation);
                    const auto* branch = runtime::simir::operation_get_if<runtime::simir::Branch>(&operation);
                    if ((jump && !valid_target(jump->target))
                        || (branch && (!valid_target(branch->when_true)
                            || !valid_target(branch->when_false)))) {
                        throw LlvmJitError("masked branch escapes its forward member body");
                    }
                }
                expected_begin = gate.end_instruction;
                identity += ":" + std::to_string(gate.begin_instruction)
                    + ":" + std::to_string(gate.end_instruction)
                    + ":" + std::to_string(gate.activation_bit);
            }
            if (expected_begin + 2U != entry.process->operations.size()
                || impl_->options.debug_instrumentation
                || !entry.bound_literal_sites.empty()) {
                throw LlvmJitError("masked native entry requires a complete uninstrumented body");
            }
            key = support::Sha256::hex(support::Sha256::digest(identity));
        }
        if (region_known_logic4) {
            key.append(":region-known-logic4-v1");
        }
        return key;
    };
    struct PreflightCleanup {
        LlvmObjectCache* cache { };
        std::vector<std::string> keys;
        std::optional<std::size_t> preserved_key;

        ~PreflightCleanup()
        {
            try {
                discard_unpreserved();
            } catch (...) {
            }
        }

        void remember(std::string key)
        {
            keys.push_back(std::move(key));
        }

        void preserve(const std::string_view key)
        {
            const auto found = std::ranges::find(keys, key);
            if (found == keys.end()) {
                throw LlvmJitError(
                    "selected LLVM tier cache key was not preflighted");
            }
            preserved_key = static_cast<std::size_t>(
                std::distance(keys.begin(), found));
            discard_unpreserved();
        }

        void discard_unpreserved()
        {
            if (cache == nullptr) {
                return;
            }
            for (std::size_t index = 0U; index < keys.size(); ++index) {
                if (!preserved_key || *preserved_key != index) {
                    cache->discard_preflight(keys[index]);
                }
            }
        }

        void discard_all()
        {
            preserved_key.reset();
            discard_unpreserved();
        }
    } preflight_cleanup {
        impl_->object_cache.get(), { }, std::nullopt
    };
    std::vector<std::string> process_keys;
    process_keys.reserve(entries.size());
    const bool tier_eligible = module_backend_tier_eligible(entries);
    const bool tiered_read_dedup_safe = module_tiered_read_dedup_eligible(
        entries, impl_->options.require_direct_read_signals);
    if (impl_->object_cache) {
        std::vector<std::string> cached_symbols;
        std::unordered_set<std::string> unique_symbols;
        cached_symbols.reserve(entries.size());
        for (const auto& entry : entries) {
            if (entry.process == nullptr) {
                throw LlvmJitError(
                    "LLVM process module entry has no SimIR process");
            }
            validate_bound_literals(entry);
            if (!valid_symbol(entry.symbol)) {
                throw LlvmJitError(
                    "LLVM process symbol must be a non-empty C identifier");
            }
            cached_symbols.emplace_back(entry.symbol);
            if (!unique_symbols.insert(cached_symbols.back()).second) {
                throw LlvmJitError(
                    "duplicate LLVM process symbol '"
                    + cached_symbols.back() + "'");
            }
            process_keys.push_back(
                make_process_key(cached_symbols.back(), entry));
        }
        const std::array candidate_tiers {
            LlvmBackendTier::less, LlvmBackendTier::none
        };
        const auto candidate_count = tier_eligible ? 2U : 1U;
        for (std::size_t candidate_index = 0U;
            candidate_index < candidate_count; ++candidate_index) {
            const auto candidate_tier = tier_eligible
                ? candidate_tiers[candidate_index]
                : LlvmBackendTier::none;
            const auto candidate_key = make_native_module_cache_key(
                owned_module_identity, process_keys, candidate_tier,
                tier_eligible);
            preflight_cleanup.remember(candidate_key);
            std::vector<std::byte> metadata;
            auto preflight_object = impl_->object_cache->preflight(
                candidate_key, &metadata);
            if (!preflight_object) {
                continue;
            }
            bool cache_identity_mismatch { };
            auto cached_info = metadata.empty()
                ? std::nullopt
                : Impl::decode_module_metadata(
                      metadata, entries, signal_widths, signal_value_kinds,
                      candidate_key,
                      candidate_tier, tier_eligible,
                      impl_->options.require_direct_read_signals,
                      &cache_identity_mismatch);
            if (!cached_info) {
                if (cache_identity_mismatch || metadata.empty()) {
                    throw LlvmJitError(
                        "cached LLVM native object has incompatible ABI, "
                        "semantics, optimization-tier, or target identity");
                }
                throw LlvmJitError(
                    "cached LLVM native object metadata does not match its "
                    "compiled process module");
            }
            preflight_cleanup.discard_all();
            {
                const std::scoped_lock lock { impl_->lookup_mutex };
                if (impl_->module_identities.contains(owned_module_identity)
                    || impl_->pending_module_identities.contains(
                        owned_module_identity)) {
                    throw LlvmJitError(
                        "duplicate LLVM process module identity '"
                        + owned_module_identity + "'");
                }
                for (const auto& symbol : cached_symbols) {
                    if (impl_->symbols.contains(symbol)
                        || impl_->pending_symbols.contains(symbol)) {
                        throw LlvmJitError(
                            "duplicate LLVM process symbol '" + symbol + "'");
                    }
                }
                impl_->pending_module_identities.insert(
                    owned_module_identity);
                impl_->pending_symbols.insert(
                    cached_symbols.begin(), cached_symbols.end());
            }
            try {
                if (auto error = impl_->jit->addObjectFile(
                        std::move(preflight_object))) {
                    throw LlvmJitError(
                        "cannot add cached LLVM process module '"
                        + owned_module_identity + "': "
                        + llvm_error(std::move(error)));
                }
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->module_identities.insert(owned_module_identity);
                impl_->pending_module_identities.erase(owned_module_identity);
                for (std::size_t index = 0U;
                    index < cached_symbols.size(); ++index) {
                    impl_->pending_symbols.erase(cached_symbols[index]);
                    impl_->symbols.insert(cached_symbols[index]);
                    impl_->info_by_symbol.emplace(
                        cached_symbols[index],
                        std::move(cached_info->processes[index]));
                }
            } catch (...) {
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->pending_module_identities.erase(owned_module_identity);
                for (const auto& symbol : cached_symbols) {
                    impl_->pending_symbols.erase(symbol);
                }
                throw;
            }
            impl_->object_cache->accept_preflight_hit(candidate_key);
            return;
        }
    }
    struct PreparedProcess {
        std::string symbol;
        const Process* process { };
        std::vector<runtime::simir::InstructionIndex> bound_literal_sites;
        std::vector<FusedMaskedMemberGate> masked_member_gates;
        std::vector<RegionPreparedOutputLoweringBinding>
            prepared_output_bindings;
        std::string prepared_output_symbol;
        std::string prepared_output_successor_symbol;
        std::vector<RegionDirectReadyLoweringBinding>
            direct_ready_bindings;
        std::string direct_ready_symbol;
        std::string direct_ready_successor_symbol;
        ValidatedProcess validated;
        std::string cache_key;
        ProcessLoweringPlan lowering_plan;
        Impl::ProcessInfo info;
    };
    std::vector<PreparedProcess> prepared;
    prepared.reserve(entries.size());
    std::unordered_set<std::string> module_symbols;

    for (const auto& entry : entries) {
        if (entry.process == nullptr) {
            throw LlvmJitError("LLVM process module entry has no SimIR process");
        }
        if (!valid_symbol(entry.symbol)) {
            throw LlvmJitError(
                "LLVM process symbol must be a non-empty C identifier");
        }
        validate_bound_literals(entry);
        std::string owned_symbol { entry.symbol };
        if (!module_symbols.insert(owned_symbol).second) {
            throw LlvmJitError(
                "duplicate LLVM process symbol '" + owned_symbol + "'");
        }

        ValidatedProcess validated;
        bool reused_validation = false;
        if (!impl_->immutable_design_identity.empty()) {
            const std::scoped_lock lock { impl_->validation_mutex };
            const auto found = impl_->immutable_validated_processes.find(
                entry.process);
            if (found != impl_->immutable_validated_processes.end()) {
                validated = std::move(found->second);
                impl_->immutable_validated_processes.erase(found);
                reused_validation = true;
            }
        }
        if (!reused_validation) {
            validated = validate_process(
                *entry.process, signal_widths, signal_value_kinds);
        }
        auto cache_key = impl_->object_cache
            ? process_keys[prepared.size()]
            : make_process_key(owned_symbol, entry);
        const bool uses_native_call_stack = std::ranges::any_of(
            entry.process->operations,
            [](const runtime::simir::Operation& operation) {
                return runtime::simir::operation_holds<
                    runtime::simir::CallableFramePush>(operation);
            });
        const auto accumulated_update_signals = direct_update_signals(
            *entry.process, signal_widths, signal_value_kinds);
        const auto callback_free_read_signals = direct_read_signals(
            *entry.process, signal_widths, signal_value_kinds);
        const auto callback_operands
            = entry.signal_callback_ids_are_actual
            ? signal_callback_operands(*entry.process, signal_widths)
            : std::vector<runtime::simir::SignalId> { };
        auto lowering_plan = make_process_lowering_plan(
            *entry.process, validated.register_widths,
            impl_->options.debug_instrumentation);
        if (!entry.masked_member_gates.empty() && lowering_plan.partial) {
            throw LlvmJitUnsupportedError("masked native body requires complete lowering");
        }
        auto process_info = Impl::ProcessInfo {
            make_frame_layout(
                cache_key,
                validated.register_widths,
                entry.process->string_register_count,
                validated.uses_logic9,
                impl_->options.debug_instrumentation
                    || !entry.process->debug_locals.empty(),
                callback_free_read_signals,
                accumulated_update_signals,
                entry.signal_callback_ids_are_actual,
                callback_operands),
            static_cast<std::uint32_t>(entry.process->operations.size()),
            uses_native_call_stack,
            validated.requires_resume,
            validated.uses_write_update,
            validated.uses_write_after,
            validated.uses_write_inertial,
            validated.uses_write_projected,
            validated.uses_write_projected_waveform,
            validated.uses_write_blocking_slice,
            validated.uses_write_update_slice,
            validated.uses_write_after_slice,
            validated.uses_write_inertial_slice,
            validated.uses_write_projected_slice,
            validated.uses_write_projected_waveform_slice,
            validated.uses_force_signal_slice,
            validated.uses_release_signal_slice,
            validated.uses_force_driver_signal_slice,
            validated.uses_release_driver_signal_slice,
            validated.uses_debug_points
                && impl_->options.debug_instrumentation,
            validated.uses_signal_event,
            validated.uses_signal_last_value,
            validated.uses_signal_last_event,
            validated.uses_simulation_time,
            validated.uses_vital_timing,
            validated.uses_vital_delay,
            validated.uses_signal_active,
            validated.uses_signal_last_active,
            validated.uses_signal_driving,
            validated.uses_signal_driving_value,
            validated.uses_output,
            validated.uses_postponed_output,
            validated.uses_report,
            validated.uses_formatted_output,
            validated.uses_time_output,
            validated.uses_monitor_install,
            validated.uses_monitor_control,
            validated.uses_random_value,
            validated.uses_strings,
            validated.uses_files,
            validated.uses_containers || !entry.bound_literal_sites.empty(),
            validated.uses_wide_container_operation,
            validated.uses_container_read_index64,
            validated.uses_exact_signal_operation,
            validated.uses_wide_signal_read,
            validated.uses_wide_signal_write,
            validated.uses_wide_projected_write,
            validated.uses_code_coverage,
            validated.uses_coverage_sample,
            validated.uses_class_property_operation,
            validated.uses_event_triggered,
            impl_->options.require_direct_read_signals
                || entry.require_direct_read_signals,
            entry.tiered_read_dedup_safe,
            { },
            { },
            { },
            { },
        };
        process_info.entry_points = lowering_plan.entry_points;
        Impl::populate_direct_read_metadata(
            process_info, signal_widths, signal_value_kinds);
        process_info.direct_update_widths.reserve(
            process_info.frame_layout.direct_update_signals.size());
        for (const auto signal :
            process_info.frame_layout.direct_update_signals) {
            if (signal >= signal_widths.size()) {
                throw LlvmJitError(
                    "LLVM direct-update signal width is unavailable");
            }
            process_info.direct_update_widths.push_back(
                signal_widths[signal]);
        }
        std::vector<RegionPreparedOutputLoweringBinding>
            prepared_output_bindings;
        std::string prepared_output_symbol;
        std::string prepared_output_successor_symbol;
        if (!prepared_output_signals.empty()) {
            prepared_output_bindings.reserve(prepared_output_signals.size());
            for (std::size_t index = 0U;
                 index < prepared_output_signals.size(); ++index) {
                const auto signal = prepared_output_signals[index];
                const auto slot = std::ranges::find(
                    process_info.frame_layout.direct_update_signals, signal);
                if (slot
                    == process_info.frame_layout.direct_update_signals.end()) {
                    throw LlvmJitUnsupportedError(
                        "prepared-output signal has no direct-update slot");
                }
                const auto slot_index = static_cast<std::size_t>(
                    slot - process_info.frame_layout.direct_update_signals.begin());
                if (slot_index > std::numeric_limits<std::uint32_t>::max()) {
                    throw LlvmJitUnsupportedError(
                        "prepared-output direct-update slot index is too large");
                }
                const auto successor_mask
                    = successor_member_masks.empty()
                    ? 0U : successor_member_masks[index];
                prepared_output_bindings.push_back({
                    static_cast<std::uint32_t>(slot_index),
                    static_cast<std::uint32_t>(index),
                    signal_widths[signal],
                    successor_mask
                });
            }
            prepared_output_symbol
                = owned_symbol + "_prepared_output_prefix_v1";
            if (!valid_symbol(prepared_output_symbol)) {
                throw LlvmJitUnsupportedError(
                    "prepared-output entry symbol is not a valid C identifier");
            }
            if (!successor_member_masks.empty()) {
                prepared_output_successor_symbol
                    = owned_symbol + "_prepared_output_successor_v1";
                if (!valid_symbol(prepared_output_successor_symbol)) {
                    throw LlvmJitUnsupportedError(
                        "prepared-output successor symbol is invalid");
                }
            }
        }
        std::vector<RegionDirectReadyLoweringBinding>
            direct_ready_process_bindings;
        std::string direct_ready_symbol;
        std::string direct_ready_successor_symbol;
        if (!direct_ready_bindings.empty()) {
            direct_ready_process_bindings.assign(
                direct_ready_bindings.begin(), direct_ready_bindings.end());
            direct_ready_symbol
                = owned_symbol + "_direct_ready_window_prepared_v1";
            if (!valid_symbol(direct_ready_symbol)) {
                throw LlvmJitUnsupportedError(
                    "direct-ready entry symbol is not a valid C identifier");
            }
            if (!prepared_output_successor_symbol.empty()) {
                direct_ready_successor_symbol
                    = owned_symbol
                    + "_direct_ready_window_successor_masks_v1";
                if (!valid_symbol(direct_ready_successor_symbol)) {
                    throw LlvmJitUnsupportedError(
                        "direct-ready successor entry symbol is invalid");
                }
            }
        }
        if (!impl_->object_cache) {
            process_keys.push_back(cache_key);
        }
        prepared.push_back(
            { std::move(owned_symbol), entry.process,
                entry.bound_literal_sites, entry.masked_member_gates,
                std::move(prepared_output_bindings),
                std::move(prepared_output_symbol),
                std::move(prepared_output_successor_symbol),
                std::move(direct_ready_process_bindings),
                std::move(direct_ready_symbol),
                std::move(direct_ready_successor_symbol),
                std::move(validated),
                std::move(cache_key), std::move(lowering_plan),
                std::move(process_info) });
    }
    {
        const std::scoped_lock lock { impl_->lookup_mutex };
        if (impl_->module_identities.contains(owned_module_identity)
            || impl_->pending_module_identities.contains(
                owned_module_identity)) {
            throw LlvmJitError(
                "duplicate LLVM process module identity '"
                + owned_module_identity + "'");
        }
        for (const auto& item : prepared) {
            if (impl_->symbols.contains(item.symbol)
                || impl_->pending_symbols.contains(item.symbol)) {
                throw LlvmJitError(
                    "duplicate LLVM process symbol '" + item.symbol + "'");
            }
        }
        impl_->pending_module_identities.insert(owned_module_identity);
        for (const auto& item : prepared) {
            impl_->pending_symbols.insert(item.symbol);
        }
    }

    std::optional<std::string> module_cache_key;
    try {
        auto context_owner = impl_->compilation_contexts->acquire();
        auto thread_safe_module = context_owner.withContextDo(
            [&](llvm::LLVMContext* context) {
                auto module = std::make_unique<llvm::Module>(
                    owned_module_identity + ".module", *context);
                module->setDataLayout(impl_->jit->getDataLayout());
                module->setTargetTriple(impl_->jit->getTargetTriple());
                const bool profile_cpu
                    = std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr;
                const auto lowering_begin = std::chrono::steady_clock::now();
                const auto lowering_cpu_begin = profile_cpu
                    ? diagnostic::thread_cpu_now() : std::nullopt;
                for (auto& item : prepared) {
                    lower_process(
                        *module, item.symbol, *item.process, signal_widths,
                        signal_value_kinds,
                        item.info.frame_layout.direct_read_signals,
                        item.info.frame_layout.direct_update_signals,
                        item.info.frame_layout.signal_callback_ids_are_actual,
                        item.info.frame_layout.signal_callback_operands,
                        item.info.frame_layout.signal_callback_operand_word_base,
                        item.validated,
                        item.lowering_plan,
                        impl_->options.optimization,
                        impl_->options.debug_instrumentation,
                        impl_->options.require_direct_update_slots,
                        item.info.requires_direct_read_signals,
                        region_known_logic4
                            ? ProcessLoweringMode::region_known_logic4
                            : ProcessLoweringMode::four_state,
                        item.bound_literal_sites, item.masked_member_gates,
                        item.info.frame_layout.register_values_persistent,
                        item.prepared_output_symbol,
                        item.prepared_output_bindings,
                        item.prepared_output_successor_symbol,
                        item.direct_ready_symbol,
                        item.direct_ready_successor_symbol,
                        item.direct_ready_bindings);
                }
                const auto lowering_end = std::chrono::steady_clock::now();
                const auto lowering_cpu = profile_cpu
                    ? diagnostic::thread_cpu_elapsed(
                        lowering_cpu_begin, diagnostic::thread_cpu_now())
                    : std::nullopt;
                const auto raw_verify_cpu_begin = profile_cpu
                    ? diagnostic::thread_cpu_now() : std::nullopt;
                const auto raw_verify_error = verify_error(*module);
                const auto raw_verify_cpu = profile_cpu
                    ? diagnostic::thread_cpu_elapsed(
                        raw_verify_cpu_begin, diagnostic::thread_cpu_now())
                    : std::nullopt;
                if (!raw_verify_error.empty()) {
                    throw LlvmJitError(
                        "generated invalid LLVM IR for module '"
                        + owned_module_identity + "': " + raw_verify_error);
                }
                const auto* const dump_process = std::getenv("FSIM_DUMP_LLVM_PROCESS");
                const bool dump_selected = dump_process != nullptr
                    && std::any_of(
                        prepared.begin(),
                        prepared.end(),
                        [dump_process](const auto& item) {
                            return item.symbol
                                == std::string("fsim_process_") + dump_process;
                        });
                if (dump_selected) {
                    dump_ir(*module, "/tmp/fsim-selected-raw.ll");
                }
                const bool report_shape = profile_cpu
                    && (dump_process == nullptr || dump_selected);
                const auto raw_shape_cpu_begin = report_shape
                    ? diagnostic::thread_cpu_now() : std::nullopt;
                const auto raw_shape = report_shape
                    ? std::optional<IrShape> { ir_shape(*module) }
                    : std::nullopt;
                const auto raw_shape_cpu = report_shape
                    ? diagnostic::thread_cpu_elapsed(
                        raw_shape_cpu_begin, diagnostic::thread_cpu_now())
                    : std::nullopt;
                const auto optimization_begin = std::chrono::steady_clock::now();
                const auto optimization_cpu_begin = profile_cpu
                    ? diagnostic::thread_cpu_now() : std::nullopt;
                optimize_module(*module, impl_->options.optimization,
                    owned_module_identity, prepared.size());
                require_jit_module_no_unwind_contract(*module);
                const auto optimization_end = std::chrono::steady_clock::now();
                const auto optimization_cpu = profile_cpu
                    ? diagnostic::thread_cpu_elapsed(
                        optimization_cpu_begin, diagnostic::thread_cpu_now())
                    : std::nullopt;
                std::optional<diagnostic::ThreadCpuTime> optimized_verify_cpu {
                    diagnostic::ThreadCpuTime::zero()
                };
                if (impl_->verify_optimized_modules) {
                    const auto optimized_verify_cpu_begin = profile_cpu
                        ? diagnostic::thread_cpu_now() : std::nullopt;
                    const auto optimized_verify_error = verify_error(*module);
                    optimized_verify_cpu = profile_cpu
                        ? diagnostic::thread_cpu_elapsed(
                            optimized_verify_cpu_begin, diagnostic::thread_cpu_now())
                        : std::nullopt;
                    if (!optimized_verify_error.empty()) {
                        throw LlvmJitError(
                            "LLVM optimization produced invalid IR for module '"
                            + owned_module_identity + "': " + optimized_verify_error);
                    }
                }
                const auto tier_selection_instruction_count
                    = ir_instruction_count(*module);
                const auto backend_tier
                    = select_backend_tier(
                        tier_eligible, tier_selection_instruction_count);
                TieredReadDedupStatistics read_dedup_statistics;
                if (backend_tier == LlvmBackendTier::less
                    && tiered_read_dedup_safe) {
                    read_dedup_statistics = run_tiered_direct_read_dedup(
                        *module, backend_tier, tiered_read_dedup_safe);
                }
                const auto optimized_instruction_count
                    = ir_instruction_count(*module);
                if (optimized_instruction_count
                        > tier_selection_instruction_count
                    || (backend_tier == LlvmBackendTier::none
                        && optimized_instruction_count
                            != tier_selection_instruction_count)) {
                    throw LlvmJitError(
                        "tiered direct-read deduplication changed the IR instruction-count proof");
                }
                if (impl_->verify_optimized_modules) {
                    const auto post_dedup_verify_error = verify_error(*module);
                    if (!post_dedup_verify_error.empty()) {
                        throw LlvmJitError(
                            "tiered direct-read deduplication produced invalid LLVM IR for module '"
                            + owned_module_identity + "': "
                            + post_dedup_verify_error);
                    }
                }
                module_cache_key = make_native_module_cache_key(
                    owned_module_identity, process_keys, backend_tier,
                    tier_eligible);
                if (impl_->object_cache) {
                    preflight_cleanup.preserve(*module_cache_key);
                }
                auto* const tier_type = llvm::Type::getInt32Ty(*context);
                auto* const eligibility_type = llvm::Type::getInt1Ty(*context);
                auto* const count_type = llvm::Type::getInt64Ty(*context);
                module->addModuleFlag(
                    llvm::Module::Error, "fsim.backend-codegen-tier",
                    llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                        tier_type, static_cast<std::uint32_t>(backend_tier))));
                module->addModuleFlag(
                    llvm::Module::Error, "fsim.backend-tier-eligible",
                    llvm::ConstantAsMetadata::get(
                        llvm::ConstantInt::get(eligibility_type, tier_eligible)));
                module->addModuleFlag(
                    llvm::Module::Error, "fsim.backend-ir-instruction-count",
                    llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                        count_type, optimized_instruction_count)));
                module->addModuleFlag(
                    llvm::Module::Error,
                    "fsim.backend-tier-selection-ir-instruction-count",
                    llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                        count_type, tier_selection_instruction_count)));
                module->setModuleIdentifier(*module_cache_key);
                if (impl_->object_cache) {
                    std::vector<Impl::ProcessInfo> metadata_processes;
                    metadata_processes.reserve(prepared.size());
                    for (const auto& item : prepared) {
                        metadata_processes.push_back(item.info);
                    }
                    impl_->object_cache->stage_metadata(
                        *module_cache_key,
                        Impl::encode_module_metadata(
                            metadata_processes, *module_cache_key, backend_tier,
                            tier_eligible, tier_selection_instruction_count,
                            optimized_instruction_count,
                            read_dedup_statistics));
                }
                const auto optimized_shape_cpu_begin = report_shape
                    ? diagnostic::thread_cpu_now() : std::nullopt;
                const auto optimized_shape = report_shape
                    ? std::optional<IrShape> { ir_shape(*module) }
                    : std::nullopt;
                const auto optimized_shape_cpu = report_shape
                    ? diagnostic::thread_cpu_elapsed(
                        optimized_shape_cpu_begin, diagnostic::thread_cpu_now())
                    : std::nullopt;
                if (dump_selected) {
                    dump_ir(*module, "/tmp/fsim-selected-optimized.ll");
                }
                if (report_shape) {
                    const auto milliseconds = [](const auto duration) {
                        return std::chrono::duration<double, std::milli>(duration)
                            .count();
                    };
                    std::string profile_line;
                    llvm::raw_string_ostream profile(profile_line);
                    profile << "fsim-profile: llvm-module identity='"
                            << owned_module_identity << "' processes="
                            << prepared.size() << " process_ids=";
                    for (std::size_t index = 0; index < prepared.size(); ++index) {
                        if (index != 0) {
                            profile << ',';
                        }
                        profile << prepared[index].process->id;
                    }
                    profile << " simir_operations=";
                    std::size_t operation_count = 0;
                    std::size_t dynamic_calls = 0;
                    std::size_t dynamic_returns = 0;
                    std::size_t frame_pushes = 0;
                    std::size_t frame_pops = 0;
                    std::size_t frame_packed_slots = 0;
                    for (const auto& item : prepared) {
                        operation_count += item.process->operations.size();
                        for (const auto& operation : item.process->operations) {
                            if (const auto* call
                                = runtime::simir::operation_get_if<
                                    runtime::simir::Call>(&operation);
                                call != nullptr && call->stack.capacity == 0) {
                                ++dynamic_calls;
                            }
                            if (const auto* return_operation
                                = runtime::simir::operation_get_if<
                                    runtime::simir::Return>(&operation);
                                return_operation != nullptr
                                && return_operation->stack.capacity == 0) {
                                ++dynamic_returns;
                            }
                            if (const auto* push
                                = runtime::simir::operation_get_if<
                                    runtime::simir::CallableFramePush>(&operation)) {
                                ++frame_pushes;
                                frame_packed_slots += push->packed.size();
                            }
                            frame_pops += runtime::simir::operation_holds<
                                runtime::simir::CallableFramePop>(operation);
                        }
                    }
                    profile << operation_count
                            << " dynamic_calls=" << dynamic_calls
                            << " dynamic_returns=" << dynamic_returns
                            << " frame_pushes=" << frame_pushes
                            << " frame_pops=" << frame_pops
                            << " frame_packed_slots=" << frame_packed_slots
                            << " lowering_ms="
                            << milliseconds(lowering_end - lowering_begin)
                            << " optimization_ms="
                            << milliseconds(
                                   optimization_end - optimization_begin)
                            << " backend_tier="
                            << (backend_tier == LlvmBackendTier::less
                                    ? "less" : "none")
                            << " optimized_ir_instructions="
                            << optimized_instruction_count
                            << " tier_selection_ir_instructions="
                            << tier_selection_instruction_count
                            << " tiered_read_dedup_marked="
                            << read_dedup_statistics.marked_loads
                            << " tiered_read_dedup_eliminated="
                            << read_dedup_statistics.eliminated_loads
                            << " tiered_read_dedup_marked_value_loads="
                            << read_dedup_statistics.marked_value_loads
                            << " tiered_read_dedup_eliminated_value_loads="
                            << read_dedup_statistics.eliminated_value_loads
                            << " optimized_verification="
                            << (impl_->verify_optimized_modules ? "enabled" : "skipped");
                    print_ir_shape(profile, "raw", *raw_shape);
                    print_ir_shape(profile, "optimized", *optimized_shape);
                    profile << '\n';
                    profile.flush();
                    const std::scoped_lock lock { module_profile_output_mutex };
                    llvm::errs() << profile_line;
                }
                if (profile_cpu) {
                    const std::scoped_lock lock { module_profile_output_mutex };
                    const auto write_cpu = [](llvm::raw_ostream& output,
                                               const auto& duration) {
                        if (!duration) {
                            output << "unavailable";
                            return;
                        }
                        output << std::chrono::duration<double, std::milli> {
                            *duration
                        }.count();
                    };
                    const auto summed_cpu = [](const auto& first,
                                                const auto& second)
                        -> std::optional<diagnostic::ThreadCpuTime> {
                        if (!first || !second) {
                            return std::nullopt;
                        }
                        return *first + *second;
                    };
                    std::string cpu_line;
                    llvm::raw_string_ostream profile(cpu_line);
                    profile << "fsim-profile: llvm-module-cpu identity='"
                            << owned_module_identity << "' lower_cpu_ms=";
                    write_cpu(profile, lowering_cpu);
                    profile << " optimize_cpu_ms=";
                    write_cpu(profile, optimization_cpu);
                    profile << " verify_cpu_ms=";
                    write_cpu(profile,
                        summed_cpu(raw_verify_cpu, optimized_verify_cpu));
                    profile << " optimized_verification="
                            << (impl_->verify_optimized_modules ? "enabled" : "skipped");
                    profile << " shape_cpu_ms=";
                    write_cpu(profile,
                        summed_cpu(raw_shape_cpu, optimized_shape_cpu));
                    profile << '\n';
                    profile.flush();
                    llvm::errs() << cpu_line;
                }

                return llvm::orc::ThreadSafeModule(
                    std::move(module), context_owner);
            });

        if (auto error = impl_->jit->addIRModule(
                std::move(thread_safe_module))) {
            throw LlvmJitError(
                "cannot add LLVM process module '" + owned_module_identity
                + "': " + llvm_error(std::move(error)));
        }
        const std::scoped_lock lock { impl_->lookup_mutex };
        impl_->module_identities.insert(owned_module_identity);
        impl_->pending_module_identities.erase(owned_module_identity);
        for (auto& item : prepared) {
            impl_->pending_symbols.erase(item.symbol);
            impl_->symbols.insert(item.symbol);
            impl_->info_by_symbol.emplace(
                std::move(item.symbol), item.info);
        }
    } catch (...) {
        preflight_cleanup.discard_all();
        if (impl_->object_cache && module_cache_key) {
            impl_->object_cache->discard_staged_metadata(
                *module_cache_key);
        }
        const std::scoped_lock lock { impl_->lookup_mutex };
        impl_->pending_module_identities.erase(owned_module_identity);
        for (const auto& item : prepared) {
            impl_->pending_symbols.erase(item.symbol);
        }
        throw;
    }
}

void LlvmJit::add_region_known_logic4_process(
    const std::string_view symbol,
    const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const JitBackendTierHint backend_tier_hint,
    const std::size_t bound_instance_count)
{
    const std::array entries {
        JitProcessModuleEntry {
            symbol, &process, { }, { }, backend_tier_hint,
            bound_instance_count }
    };
    add_process_module_impl(
        symbol, entries, signal_widths, signal_value_kinds, true);
}

void LlvmJit::add_region_prepared_output_process(
    const std::string_view symbol,
    const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const std::span<const std::uint32_t> output_signals,
    const JitBackendTierHint backend_tier_hint,
    const std::size_t bound_instance_count,
    const std::span<const RegionDirectReadyLoweringBinding>
        direct_ready_bindings,
    const std::span<const std::uint64_t> successor_member_masks)
{
    if (output_signals.empty()) {
        throw LlvmJitUnsupportedError(
            "prepared-output entry requires at least one internal output");
    }
    const std::array entries {
        JitProcessModuleEntry {
            symbol, &process, { }, { }, backend_tier_hint,
            bound_instance_count }
    };
    std::vector<std::uint64_t> zero_successor_masks;
    if (successor_member_masks.empty()) {
        zero_successor_masks.resize(output_signals.size());
    }
    const auto masks = successor_member_masks.empty()
        ? std::span<const std::uint64_t> { zero_successor_masks }
        : successor_member_masks;
    add_process_module_impl(
        symbol, entries, signal_widths, signal_value_kinds, false,
        output_signals, direct_ready_bindings, masks);
}

void LlvmJit::add_process(
    const std::string_view symbol,
    const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    add_process(
        symbol, process, signal_widths, signal_value_kinds,
        JitBackendTierHint::none, 1U);
}

void LlvmJit::add_process(
    const std::string_view symbol, const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const JitBackendTierHint backend_tier_hint,
    const std::size_t bound_instance_count,
    const bool require_direct_read_signals,
    const bool tiered_read_dedup_safe)
{
    const std::array entries {
        JitProcessModuleEntry {
            symbol, &process, { }, { }, backend_tier_hint,
            bound_instance_count, require_direct_read_signals,
            tiered_read_dedup_safe }
    };
    add_process_module(
        symbol, entries, signal_widths, signal_value_kinds);
}

void LlvmJit::add_masked_process(const std::string_view symbol,
    const FusedMaskedProcess& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    add_masked_process(
        symbol, process, signal_widths, signal_value_kinds, 1U);
}

void LlvmJit::add_masked_process(const std::string_view symbol,
    const FusedMaskedProcess& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const std::size_t bound_instance_count,
    const bool require_direct_read_signals,
    const bool tiered_read_dedup_safe)
{
    if (process.gates.empty()) {
        throw LlvmJitError("masked native process has no member gates");
    }
    const std::array entries {
        JitProcessModuleEntry {
            symbol, &process.process, { }, process.gates,
            JitBackendTierHint::fused_masked_region,
            bound_instance_count, require_direct_read_signals,
            tiered_read_dedup_safe }
    };
    add_process_module(symbol, entries, signal_widths, signal_value_kinds);
}

JitProcessHandle LlvmJit::lookup(const std::string_view symbol)
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::string owned_symbol { symbol };
    {
        const std::scoped_lock lock { impl_->lookup_mutex };
        if (!impl_->symbols.contains(owned_symbol)) {
            throw LlvmJitError(
                "LLVM process symbol was not added: '" + owned_symbol + "'");
        }
        if (const auto found = impl_->handles_by_symbol.find(owned_symbol);
            found != impl_->handles_by_symbol.end()) {
            return found->second;
        }
    }

    auto address = unwrap(impl_->jit->lookup(owned_symbol),
        "cannot materialize LLVM process '" + owned_symbol + "'");
    auto* function = address.template toPtr<NativeProcess>();
    if (function == nullptr) {
        throw LlvmJitError("LLVM returned a null process address for '" + owned_symbol + "'");
    }
    const std::scoped_lock lock { impl_->lookup_mutex };
    if (const auto found = impl_->handles_by_symbol.find(owned_symbol);
        found != impl_->handles_by_symbol.end()) {
        return found->second;
    }
    if (impl_->next_handle == 0) {
        throw LlvmJitError("LLVM process handle space is exhausted");
    }
    const JitProcessHandle handle { impl_->next_handle++ };
    const auto info = impl_->info_by_symbol.find(owned_symbol);
    if (info == impl_->info_by_symbol.end()) {
        throw LlvmJitError("LLVM process frame metadata is missing for '" + owned_symbol + "'");
    }
    impl_->functions.emplace(handle.value,
        std::make_unique<Impl::NativeEntry>(
            Impl::NativeEntry { function, info->second, owned_symbol }));
    impl_->handles_by_symbol.emplace(owned_symbol, handle);
    return handle;
}

JitProcessBinding LlvmJit::bind(const JitProcessHandle process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::scoped_lock lock { impl_->lookup_mutex };
    const auto found = impl_->functions.find(process.value);
    if (process.value == 0 || found == impl_->functions.end()) {
        throw LlvmJitError("invalid LLVM process handle");
    }
    return JitProcessBinding { impl_.get(), found->second.get() };
}

bool LlvmJit::supports_entry(
    const JitProcessHandle process,
    const runtime::simir::InstructionIndex instruction) const
{
    return supports_entry(bind(process), instruction);
}

bool LlvmJit::supports_entry(
    const JitProcessBinding process,
    const runtime::simir::InstructionIndex instruction) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    return std::ranges::binary_search(entry.info.entry_points, instruction);
}

JitProcessFrameLayout
LlvmJit::frame_layout(const JitProcessHandle process) const
{
    return frame_layout(bind(process));
}

JitProcessFrameLayout
LlvmJit::frame_layout(const JitProcessBinding process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    return entry.info.frame_layout;
}

std::uint32_t LlvmJit::operation_count(
    const JitProcessBinding process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    return entry.info.operation_count;
}

void LlvmJit::initialize_frame(
    const JitProcessHandle process, fsim_jit_frame_v2& frame,
    const std::span<std::uint64_t> register_aval,
    const std::span<std::uint64_t> register_bval,
    const std::span<std::uint8_t> register_initialized,
    const std::span<std::uint64_t> register_logic9_plane2,
    const std::span<std::uint64_t> register_logic9_plane3) const
{
    initialize_frame(
        bind(process), frame, register_aval, register_bval,
        register_initialized, register_logic9_plane2,
        register_logic9_plane3);
}

void LlvmJit::initialize_frame(
    const JitProcessBinding process, fsim_jit_frame_v2& frame,
    const std::span<std::uint64_t> register_aval,
    const std::span<std::uint64_t> register_bval,
    const std::span<std::uint8_t> register_initialized,
    const std::span<std::uint64_t> register_logic9_plane2,
    const std::span<std::uint64_t> register_logic9_plane3) const
{
    const auto layout = frame_layout(process);
    if (register_aval.size() < layout.register_word_count || register_bval.size() < layout.register_word_count
        || register_initialized.size() < layout.register_count
        || (layout.uses_logic9
            && (register_logic9_plane2.size() < layout.register_word_count
                || register_logic9_plane3.size()
                    < layout.register_word_count))) {
        throw LlvmJitError(
            "caller-owned JIT register storage is smaller than the frame layout");
    }
    if (layout.register_word_count != 0 && (register_aval.data() == register_bval.data() || (layout.uses_logic9 && (register_logic9_plane2.data() == register_logic9_plane3.data() || register_logic9_plane2.data() == register_aval.data() || register_logic9_plane2.data() == register_bval.data() || register_logic9_plane3.data() == register_aval.data() || register_logic9_plane3.data() == register_bval.data())))) {
        throw LlvmJitError(
            "caller-owned JIT register planes must be distinct");
    }
    std::fill_n(
        register_aval.begin(), layout.register_word_count, UINT64_C(0));
    std::fill_n(
        register_bval.begin(), layout.register_word_count, UINT64_C(0));
    if (layout.uses_logic9) {
        std::fill_n(
            register_logic9_plane2.begin(),
            layout.register_word_count,
            UINT64_C(0));
        std::fill_n(
            register_logic9_plane3.begin(),
            layout.register_word_count,
            UINT64_C(0));
    }
    const auto callback_operand_base = static_cast<std::size_t>(
        layout.signal_callback_operand_word_base);
    if (callback_operand_base > layout.register_word_count
        || layout.signal_callback_operands.size()
            != static_cast<std::size_t>(layout.register_word_count)
                - callback_operand_base
        || (!layout.signal_callback_ids_are_actual
            && !layout.signal_callback_operands.empty())) {
        throw LlvmJitError(
            "JIT signal-callback operand tail is inconsistent with its frame");
    }
    for (std::size_t index = 0U;
         index < layout.signal_callback_operands.size(); ++index) {
        register_aval[callback_operand_base + index]
            = layout.signal_callback_operands[index];
    }
    std::fill_n(
        register_initialized.begin(), layout.register_count, UINT8_C(0));
    frame = {
        FSIM_JIT_FRAME_ABI_VERSION_V2,
        static_cast<std::uint32_t>(sizeof(fsim_jit_frame_v2)),
        layout.layout_id_low,
        layout.layout_id_high,
        layout.register_count,
        0,
        FSIM_JIT_FRAME_STATE_READY_V2,
        FSIM_JIT_INVALID_INSTRUCTION_V2,
        register_aval.data(),
        register_bval.data(),
        register_initialized.data(),
        layout.uses_logic9
            ? register_logic9_plane2.data()
            : nullptr,
        layout.uses_logic9
            ? register_logic9_plane3.data()
            : nullptr,
        0,
        0,
        { },
    };
}

} // namespace fsim::compiler
