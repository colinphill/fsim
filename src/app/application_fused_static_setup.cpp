// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "application_design_artifact_codec_internal.hpp"
#include "application_fused_static_executor.hpp"
#include "application_fused_read_capability.hpp"

#include "fsim/compiler/fused_masked_process.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::app {
namespace {

using runtime::simir::Process;
using runtime::simir::SignalId;

[[nodiscard]] bool supports_generic_fused_scheduling(
    const Process& process)
{
    using namespace runtime::simir;
    if (process.scheduling_domain != ProcessSchedulingDomain::generic) {
        return false;
    }
    return std::ranges::none_of(
        process.operations,
        [](const auto& stored) {
            return visit_operation(
                [](const auto& operation) {
                    if constexpr (requires { operation.domain; }) {
                        return operation.domain != SignalUpdateDomain::generic;
                    }
                    return false;
                },
                stored);
        });
}

struct CanonicalFusedProcess {
    Process process;
    std::vector<SignalId> actual_signals;
    std::vector<std::uint32_t> widths;
    std::vector<runtime::simir::ValueKind> kinds;
    std::vector<runtime::simir::ResolutionKind> resolutions;
    std::string key;
};

std::optional<CanonicalFusedProcess> canonicalize_fused_process(
    Process process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_kinds,
    const std::span<const runtime::simir::ResolutionKind> resolutions)
{
    CanonicalFusedProcess result;
    result.process = std::move(process);
    result.process.id = 0U;
    result.process.name = "fused_static";
    auto remap = std::map<SignalId, SignalId> { };
    bool valid = true;
    const auto assign = [&](const SignalId signal) -> SignalId {
        if (signal >= signal_widths.size()
            || signal >= signal_kinds.size()
            || signal >= resolutions.size()
            || result.actual_signals.size()
                >= std::numeric_limits<SignalId>::max()) {
            valid = false;
            return 0U;
        }
        const auto found = remap.find(signal);
        if (found != remap.end()) {
            return found->second;
        }
        const auto canonical = static_cast<SignalId>(
            result.actual_signals.size());
        remap.emplace(signal, canonical);
        result.actual_signals.push_back(signal);
        result.widths.push_back(signal_widths[signal]);
        result.kinds.push_back(signal_kinds[signal]);
        result.resolutions.push_back(resolutions[signal]);
        return canonical;
    };
    for (auto& sensitivity : result.process.static_sensitivity) {
        sensitivity.signal = assign(sensitivity.signal);
    }
    auto operations = std::vector<runtime::simir::Operation> { };
    operations.reserve(result.process.operations.size());
    for (std::size_t index = 0U;
         index < result.process.operations.size(); ++index) {
        auto operation = result.process.operations.expanded(index);
        runtime::simir::visit_operation([&](auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, runtime::simir::ReadSignal>
                || std::is_same_v<Type, runtime::simir::WriteUpdate>
                || std::is_same_v<Type, runtime::simir::WriteUpdateSlice>
                || std::is_same_v<Type, runtime::simir::WriteProjected>) {
                value.signal = assign(value.signal);
            }
        }, operation);
        operations.push_back(std::move(operation));
    }
    result.process.operations = std::move(operations);
    for (auto& region : result.process.driver_regions) {
        region.signal = assign(region.signal);
    }
    if (!valid) {
        return std::nullopt;
    }
    codec_detail::Writer writer;
    writer.write(result.process.register_count);
    writer.write(runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
        result.process.register_value_kinds).vector());
    writer.write(result.process.static_sensitivity);
    writer.write(result.process.operations);
    writer.write(result.process.driver_regions);
    writer.write(result.widths);
    writer.write(result.kinds);
    writer.write(result.resolutions);
    if (!writer.complete()) {
        return std::nullopt;
    }
    result.key = std::move(writer).finish();
    return result;
}

struct FusedInstance {
    runtime::simir::FusedStaticCohortCandidate candidate;
    std::vector<SignalId> actual_signals;
};

struct FusedGroup {
    Process process;
    compiler::FusedMaskedProcess masked_process;
    std::vector<std::uint32_t> widths;
    std::vector<runtime::simir::ValueKind> kinds;
    std::vector<FusedInstance> instances;
};

} // namespace

void Simulation::Impl::bind_fused_static_cohorts()
{
    if (fused_static_cohorts_bound || !jit || jit_debug_instrumentation) {
        return;
    }
    fused_static_cohorts_bound = true;
    interpreter->set_fused_static_counters_enabled(
        std::getenv("FSIM_PROFILE_FUSED_STATIC") != nullptr);
    const auto candidates = interpreter->fused_static_cohort_candidates();
    auto groups = std::map<std::string, FusedGroup> { };
    for (auto candidate : candidates) {
        auto member_programs = std::vector<Process> { };
        member_programs.reserve(candidate.members.size());
        for (const auto id : candidate.members) {
            member_programs.push_back(
                runtime::simir::InterpreterProgramAccess::view(
                    *interpreter, id).materialize());
        }
        auto members = std::vector<const Process*> { };
        members.reserve(member_programs.size());
        for (const auto& program : member_programs) {
            members.push_back(&program);
        }
        if (!std::ranges::all_of(
                members,
                [](const Process* process) {
                    return supports_generic_fused_scheduling(*process);
                })) {
            continue;
        }
        auto masked = std::optional<compiler::FusedMaskedProcess> { };
        auto canonical = std::optional<CanonicalFusedProcess> { };
        // Every candidate already passed the runtime graph certificate.
        // The masked all-active eligibility bit selects that specialized
        // entry; ordinary certified bodies still get the generic fuser
        // and its own operation, write-owner, and layout validation.
        masked = compiler::fuse_masked_processes(
            members, signal_widths, signal_value_kinds, 0U);
        bool writes_match = masked
            && masked->writes.size() == members.size()
            && masked->gates.size() == members.size()
            && supports_generic_fused_scheduling(masked->process);
        for (std::size_t index = 0U;
             writes_match && index < members.size(); ++index) {
            const auto& member = *members[index];
            const auto& write = masked->writes[index];
            if (write.original_id != candidate.members[index]
                || write.regions.size() != 1U
                || member.driver_regions.size() != 1U
                || masked->gates[index].original_id
                    != candidate.members[index]
                || masked->gates[index].activation_bit != index) {
                writes_match = false;
                break;
            }
            auto expected = member.driver_regions.front();
            if (candidate.projected) {
                const auto signal = expected.signal;
                if (!expected.whole || signal >= signal_widths.size()) {
                    writes_match = false;
                    break;
                }
                expected.offset = 0U;
                expected.width = signal_widths[signal];
            }
            writes_match = write.regions.front() == expected;
        }
        if (writes_match) {
            canonical = canonicalize_fused_process(
                masked->process, signal_widths,
                signal_value_kinds, signal_resolutions);
            if (canonical) {
                masked->process = canonical->process;
                codec_detail::Writer key_writer;
                key_writer.write(canonical->key);
                key_writer.write(masked->gates.size());
                for (const auto& gate : masked->gates) {
                    key_writer.write(gate.begin_instruction);
                    key_writer.write(gate.end_instruction);
                    key_writer.write(gate.activation_bit);
                }
                if (!key_writer.complete()) {
                    canonical.reset();
                } else {
                    canonical->key = std::move(key_writer).finish();
                }
            }
        }
        if (!canonical) {
            masked.reset();
        }
        if (!masked || !canonical) {
            // The general masked kernel is the only fused static route. A
            // rejected or unsupported shape remains on ordinary execution.
            continue;
        }
        auto [found, inserted] = groups.try_emplace(canonical->key);
        if (inserted) {
            found->second.process = std::move(canonical->process);
            found->second.masked_process = std::move(*masked);
            found->second.widths = std::move(canonical->widths);
            found->second.kinds = std::move(canonical->kinds);
        }
        found->second.instances.push_back(FusedInstance {
            std::move(candidate), std::move(canonical->actual_signals)
        });
    }
    auto selected = std::vector<decltype(groups)::iterator> { };
    selected.reserve(groups.size());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        selected.push_back(it);
    }
    std::ranges::sort(selected, [](const auto lhs, const auto rhs) {
        const auto left = lhs->second.instances.size()
            * lhs->second.instances.front().candidate.members.size();
        const auto right = rhs->second.instances.size()
            * rhs->second.instances.front().candidate.members.size();
        const auto left_cost = std::max<std::size_t>(
            1U, lhs->second.process.operations.size());
        const auto right_cost = std::max<std::size_t>(
            1U, rhs->second.process.operations.size());
        const auto left_density = static_cast<double>(left)
            / static_cast<double>(left_cost);
        const auto right_density = static_cast<double>(right)
            / static_cast<double>(right_cost);
        if (left_density != right_density) {
            return left_density > right_density;
        }
        if (left != right) {
            return left > right;
        }
        return lhs->first < rhs->first;
    });
    // This is a cold-compilation budget for the first generic lowering tier,
    // not an operation-count restriction on graph or kernel eligibility.
    // Rank by represented original members per native operation and leave
    // unselected groups on their existing executor path.
    constexpr std::size_t maximum_startup_fused_kernels = 16U;
    constexpr std::size_t maximum_startup_fused_operations = 16'384U;
    std::size_t bound { };
    std::size_t masked_all_active_bound { };
    std::size_t masked_from_static_bound { };
    std::size_t compiled { };
    std::size_t selected_operations { };
    for (const auto group_it : selected) {
        auto& group = group_it->second;
        const auto& compile_process = group.masked_process.process;
        const auto operations = compile_process.operations.size();
        const auto all_active_member_count = group.masked_process.gates.size();
        if (all_active_member_count == 0U
            || std::ranges::any_of(group.instances, [&](const auto& instance) {
                return instance.candidate.members.size()
                    != all_active_member_count;
            })) {
            continue;
        }
        if (compiled >= maximum_startup_fused_kernels
            || operations > maximum_startup_fused_operations
                - selected_operations) {
            continue;
        }
        const auto digest = support::Sha256::hex(
            support::Sha256::digest(group_it->first));
        const auto symbol = "fsim_fused_masked_all_active_g1_" + digest;
        const auto read_capability = fused_detail::read_lowering_capability(
            group.process, group.widths, group.kinds);
        try {
            jit->add_masked_process(symbol, group.masked_process,
                group.widths, group.kinds, group.instances.size(),
                read_capability.require_direct_read_signals,
                read_capability.tiered_read_dedup_safe);
        } catch (const compiler::LlvmJitUnsupportedError&) {
            // Leave the cohort unbound. The runtime keeps every process on
            // its ordinary checked execution route.
            continue;
        }
        const auto handle = jit->lookup(symbol);
        ++compiled;
        selected_operations += operations;
        for (const auto& instance : group.instances) {
            auto executor = make_fused_static_executor(
                *jit, handle, instance.actual_signals,
                group.widths, instance.candidate.outputs,
                group.kinds, instance.candidate.projected,
                read_capability.require_direct_read_signals,
                all_active_member_count);
            const bool use_masked_all_active
                = instance.candidate.masked_all_active_eligible;
            interpreter->install_fused_static_cohort(
                instance.candidate.cohort_id, std::move(executor),
                use_masked_all_active);
            if (use_masked_all_active) {
                ++masked_all_active_bound;
                masked_from_static_bound
                    += !instance.candidate.masked_all_active;
            }
            ++bound;
        }
    }
    if (std::getenv("FSIM_PROFILE_FUSED_STATIC") != nullptr) {
        std::cerr << "fsim fused-static graph: candidates="
                  << candidates.size() << " unique=" << groups.size()
                  << " compiled_kernels=" << compiled
                  << " compiled_operations=" << selected_operations
                  << " bound_cohorts=" << bound
                  << " masked_all_active_bound_cohorts="
                  << masked_all_active_bound
                  << " masked_all_active_from_static="
                  << masked_from_static_bound
                  << " unbound_candidates=" << candidates.size() - bound
                  << '\n';
    }
}

} // namespace fsim::app
