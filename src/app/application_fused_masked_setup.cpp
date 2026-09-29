// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "application_design_artifact_codec_internal.hpp"
#include "application_fused_static_executor.hpp"

#include "fsim/compiler/fused_masked_process.hpp"

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

struct CanonicalMaskedProcess {
    compiler::FusedMaskedProcess fused;
    std::vector<SignalId> actual_signals;
    std::vector<std::uint32_t> widths;
    std::vector<runtime::simir::ValueKind> kinds;
    std::vector<runtime::simir::ResolutionKind> resolutions;
    std::string key;
};

std::optional<CanonicalMaskedProcess> canonicalize_masked_process(
    compiler::FusedMaskedProcess fused,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_kinds,
    const std::span<const runtime::simir::ResolutionKind> resolutions)
{
    CanonicalMaskedProcess result;
    result.fused = std::move(fused);
    auto& process = result.fused.process;
    process.id = 0U;
    process.name = "fused_masked";
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
    for (auto& sensitivity : process.static_sensitivity) {
        sensitivity.signal = assign(sensitivity.signal);
    }
    auto operations = std::vector<runtime::simir::Operation> { };
    operations.reserve(process.operations.size());
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
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
    process.operations = std::move(operations);
    for (auto& region : process.driver_regions) {
        region.signal = assign(region.signal);
    }
    if (!valid) {
        return std::nullopt;
    }
    codec_detail::Writer writer;
    writer.write(process.register_count);
    writer.write(process.register_value_kinds);
    writer.write(process.static_sensitivity);
    writer.write(process.operations);
    writer.write(process.driver_regions);
    writer.write(result.widths);
    writer.write(result.kinds);
    writer.write(result.resolutions);
    writer.write(result.fused.gates.size());
    for (const auto& gate : result.fused.gates) {
        writer.write(gate.begin_instruction);
        writer.write(gate.end_instruction);
        writer.write(gate.activation_bit);
    }
    if (!writer.complete()) {
        return std::nullopt;
    }
    result.key = std::move(writer).finish();
    return result;
}

struct MaskedInstance {
    runtime::simir::FusedMaskedRegionCandidate candidate;
    std::vector<SignalId> actual_signals;
    std::vector<std::vector<Process::DriverRegion>> mandatory_writes;
};

struct MaskedGroup {
    compiler::FusedMaskedProcess fused;
    std::vector<std::uint32_t> widths;
    std::vector<runtime::simir::ValueKind> kinds;
    std::vector<MaskedInstance> instances;
};

} // namespace

void Simulation::Impl::bind_fused_masked_regions()
{
    if (fused_masked_regions_bound || !jit || jit_debug_instrumentation) {
        return;
    }
    fused_masked_regions_bound = true;
    const bool trace = std::getenv("FSIM_PROFILE_FUSED_MASKED") != nullptr;
    interpreter->set_fused_masked_region_counters_enabled(trace);
    const auto candidates = interpreter->fused_masked_region_candidates();
    auto groups = std::map<std::string, MaskedGroup> { };
    for (auto candidate : candidates) {
        auto members = std::vector<const Process*> { };
        members.reserve(candidate.members.size());
        for (const auto id : candidate.members) {
            members.push_back(candidate.projected
                    ? &interpreter->process_program(id)
                    : &interpreter->fused_masked_member_program(id));
        }
        auto fused = compiler::fuse_masked_processes(
            members, signal_widths, signal_value_kinds, 0U);
        if (!fused || fused->writes.size() != candidate.members.size()) {
            continue;
        }
        auto mandatory_writes
            = std::vector<std::vector<Process::DriverRegion>> { };
        mandatory_writes.reserve(fused->writes.size());
        bool matching = true;
        for (std::size_t index = 0U;
             index < fused->writes.size(); ++index) {
            matching &= fused->writes[index].original_id
                == candidate.members[index];
            mandatory_writes.push_back(fused->writes[index].regions);
        }
        if (!matching) {
            continue;
        }
        auto canonical = canonicalize_masked_process(
            std::move(*fused), signal_widths,
            signal_value_kinds, signal_resolutions);
        if (!canonical) {
            continue;
        }
        auto [found, inserted] = groups.try_emplace(canonical->key);
        if (inserted) {
            found->second.fused = std::move(canonical->fused);
            found->second.widths = std::move(canonical->widths);
            found->second.kinds = std::move(canonical->kinds);
        }
        found->second.instances.push_back(MaskedInstance {
            std::move(candidate), std::move(canonical->actual_signals),
            std::move(mandatory_writes)
        });
    }
    auto selected = std::vector<decltype(groups)::iterator> { };
    selected.reserve(groups.size());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        selected.push_back(it);
    }
    std::ranges::sort(selected, [](const auto left, const auto right) {
        const auto represented = [](const auto it) {
            return it->second.instances.size()
                * it->second.instances.front().candidate.members.size();
        };
        const auto left_size = std::max<std::size_t>(
            1U, left->second.fused.process.operations.size());
        const auto right_size = std::max<std::size_t>(
            1U, right->second.fused.process.operations.size());
        const auto left_density = static_cast<double>(represented(left))
            / static_cast<double>(left_size);
        const auto right_density = static_cast<double>(represented(right))
            / static_cast<double>(right_size);
        if (left_density != right_density) {
            return left_density > right_density;
        }
        return left->first < right->first;
    });
    // Compilation is a cold-start budget. A graph candidate rejected by the
    // budget keeps its original V16 scheduler/executor route unchanged.
    constexpr std::size_t maximum_startup_masked_kernels = 16U;
    constexpr std::size_t maximum_startup_masked_operations = 16'384U;
    std::size_t compiled { };
    std::size_t bound { };
    std::size_t compiled_operations { };
    for (const auto group_it : selected) {
        auto& group = group_it->second;
        const auto operations = group.fused.process.operations.size();
        if (compiled >= maximum_startup_masked_kernels
            || operations > maximum_startup_masked_operations
                - compiled_operations) {
            continue;
        }
        const auto digest = support::Sha256::hex(
            support::Sha256::digest(group_it->first));
        const auto symbol = "fsim_fused_masked_g1_" + digest;
        try {
            jit->add_masked_process(symbol, group.fused,
                group.widths, group.kinds);
            const auto handle = jit->lookup(symbol);
            ++compiled;
            compiled_operations += operations;
            for (auto& instance : group.instances) {
                auto executor = make_fused_masked_region_executor(
                    *jit, handle, instance.actual_signals,
                    group.widths, instance.candidate.outputs,
                    group.kinds, instance.candidate.projected);
                interpreter->install_fused_masked_region(
                    instance.candidate.region_id,
                    std::move(instance.mandatory_writes),
                    std::move(executor));
                ++bound;
            }
        } catch (const compiler::LlvmJitUnsupportedError&) {
            continue;
        }
    }
    if (trace) {
        std::cerr << "fsim masked graph: candidates="
                  << candidates.size() << " unique=" << groups.size()
                  << " compiled_kernels=" << compiled
                  << " compiled_operations=" << compiled_operations
                  << " bound_regions=" << bound
                  << " unbound_candidates=" << candidates.size() - bound
                  << '\n';
    }
}

} // namespace fsim::app
