// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "application_design_artifact_codec_internal.hpp"
#include "application_fused_static_executor.hpp"

#include "fsim/compiler/fused_static_process.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::app {
namespace {

using runtime::simir::Process;
using runtime::simir::SignalId;

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
    writer.write(result.process.register_value_kinds);
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
        auto members = std::vector<const Process*> { };
        members.reserve(candidate.members.size());
        for (const auto id : candidate.members) {
            members.push_back(&interpreter->process_program(id));
        }
        const auto fused = compiler::fuse_static_processes(
            members, signal_widths, signal_value_kinds, 0U);
        if (!fused) {
            continue;
        }
        auto canonical = canonicalize_fused_process(
            fused->process, signal_widths,
            signal_value_kinds, signal_resolutions);
        if (!canonical) {
            continue;
        }
        auto [found, inserted] = groups.try_emplace(canonical->key);
        if (inserted) {
            found->second.process = std::move(canonical->process);
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
    std::size_t compiled { };
    std::size_t selected_operations { };
    for (const auto group_it : selected) {
        auto& group = group_it->second;
        const auto operations = group.process.operations.size();
        if (compiled >= maximum_startup_fused_kernels
            || operations > maximum_startup_fused_operations
                - selected_operations) {
            continue;
        }
        const auto digest = support::Sha256::hex(
            support::Sha256::digest(group_it->first));
        const auto symbol = "fsim_fused_static_g1_" + digest;
        try {
            jit->add_process(symbol, group.process,
                group.widths, group.kinds);
            const auto handle = jit->lookup(symbol);
            ++compiled;
            selected_operations += operations;
            for (const auto& instance : group.instances) {
                auto executor = make_fused_static_executor(
                    *jit, handle, instance.actual_signals,
                    group.widths, instance.candidate.outputs,
                    group.kinds, instance.candidate.projected);
                interpreter->install_fused_static_cohort(
                    instance.candidate.cohort_id, std::move(executor));
                ++bound;
            }
        } catch (const compiler::LlvmJitUnsupportedError&) {
            continue;
        }
    }
    if (std::getenv("FSIM_PROFILE_FUSED_STATIC") != nullptr) {
        std::cerr << "fsim fused-static graph: candidates="
                  << candidates.size() << " unique=" << groups.size()
                  << " compiled_kernels=" << compiled
                  << " compiled_operations=" << selected_operations
                  << " bound_cohorts=" << bound
                  << " unbound_candidates=" << candidates.size() - bound
                  << '\n';
    }
}

} // namespace fsim::app
