// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_internal.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <ranges>

namespace fsim::runtime::simir {
Interpreter::Interpreter(
    SchedulerOptions options,
    const std::uint64_t seed)
    : impl_(std::make_unique<Impl>(options, seed))
{
}
Interpreter::~Interpreter()
{
    if (impl_->native_phase_profile_enabled) {
        std::cerr << "fsim-profile: native-phase attempts="
                  << impl_->native_phase_profile_attempts
                  << " published=" << impl_->native_phase_profile_published
                  << " rejected_structure="
                  << impl_->native_phase_profile_rejected_structure
                  << " rejected_observer="
                  << impl_->native_phase_profile_rejected_observer
                  << " rejected_route="
                  << impl_->native_phase_profile_rejected_route
                  << " rejected_semantics="
                  << impl_->native_phase_profile_rejected_semantics
                  << " rejected_runtime="
                  << impl_->native_phase_profile_rejected_runtime
                  << " single_resumes="
                  << impl_->native_phase_profile_single_resumes
                  << " cohort_resumes="
                  << impl_->native_phase_profile_cohort_resumes
                  << " cohort_members="
                  << impl_->native_phase_profile_cohort_members
                  << " module_paths=" << impl_->module_paths.size()
                  << " timing_checks=" << impl_->module_timing_checks.size()
                  << " sampled=" << impl_->requires_sampled_values
                  << " signal_hook=" << static_cast<bool>(impl_->signal_change_hook)
                  << " stored_hook="
                  << static_cast<bool>(impl_->stored_signal_change_hook)
                  << " driver_hook="
                  << static_cast<bool>(impl_->driver_change_hook)
                  << " scalar_hook="
                  << static_cast<bool>(impl_->scalar_signal_change_hook)
                  << " container_hook="
                  << static_cast<bool>(impl_->container_object_change_hook)
                  << " monitor=" << static_cast<bool>(impl_->monitor)
                  << '\n';
    }
    if (impl_->native_process_count_profile_enabled) {
        const auto total = std::accumulate(
            impl_->native_process_resume_counts.begin(),
            impl_->native_process_resume_counts.end(),
            std::uint64_t { });
        const auto single_total = std::accumulate(
            impl_->native_process_single_resume_counts.begin(),
            impl_->native_process_single_resume_counts.end(),
            std::uint64_t { });
        const auto single_static_wait = std::accumulate(
            impl_->native_process_single_static_wait_counts.begin(),
            impl_->native_process_single_static_wait_counts.end(),
            std::uint64_t { });
        const auto cohort_total = std::accumulate(
            impl_->native_process_cohort_resume_counts.begin(),
            impl_->native_process_cohort_resume_counts.end(),
            std::uint64_t { });
        const auto cohort_static_wait = std::accumulate(
            impl_->native_process_cohort_static_wait_counts.begin(),
            impl_->native_process_cohort_static_wait_counts.end(),
            std::uint64_t { });
        std::cerr << "fsim-profile: native-process-counts processes="
                  << impl_->native_process_resume_counts.size()
                  << " resumes=" << total
                  << " single=" << single_total
                  << " single_static_wait=" << single_static_wait
                  << " cohort=" << cohort_total
                  << " cohort_static_wait=" << cohort_static_wait
                  << " word_changes=" << impl_->native_process_word_changes
                  << " word_fanout_matches="
                  << impl_->native_process_word_fanout_matches
                  << " word_fanout_ready="
                  << impl_->native_process_word_fanout_ready
                  << " single_boundaries="
                  << impl_->native_process_single_boundary_counts[0] << ','
                  << impl_->native_process_single_boundary_counts[1] << ','
                  << impl_->native_process_single_boundary_counts[2] << ','
                  << impl_->native_process_single_boundary_counts[3] << ','
                  << impl_->native_process_single_boundary_counts[4] << ','
                  << impl_->native_process_single_boundary_counts[5]
                  << " cohort_boundaries="
                  << impl_->native_process_cohort_boundary_counts[0] << ','
                  << impl_->native_process_cohort_boundary_counts[1] << ','
                  << impl_->native_process_cohort_boundary_counts[2] << ','
                  << impl_->native_process_cohort_boundary_counts[3] << ','
                  << impl_->native_process_cohort_boundary_counts[4] << ','
                  << impl_->native_process_cohort_boundary_counts[5]
                  << " simir_groups="
                  << impl_->native_process_simir_boundary_groups[0] << ','
                  << impl_->native_process_simir_boundary_groups[1] << ','
                  << impl_->native_process_simir_boundary_groups[2] << ','
                  << impl_->native_process_simir_boundary_groups[3] << ','
                  << impl_->native_process_simir_boundary_groups[4] << ','
                  << impl_->native_process_simir_boundary_groups[5] << ','
                  << impl_->native_process_simir_boundary_groups[6] << ','
                  << impl_->native_process_simir_boundary_groups[7] << ','
                  << impl_->native_process_simir_boundary_groups[8] << '\n';
        for (std::size_t index = 0;
             index < impl_->native_process_scheduling_boundaries.size();
             ++index) {
            if (impl_->native_process_scheduling_boundaries[index] != 0U) {
                std::cerr << "fsim-profile: native-scheduling-boundary index="
                          << index << " count="
                          << impl_->native_process_scheduling_boundaries[index]
                          << '\n';
            }
        }
        for (std::size_t id = 0;
             id < impl_->native_process_resume_counts.size(); ++id) {
            const auto count = impl_->native_process_resume_counts[id];
            if (count == 0U || id >= impl_->processes.size()) {
                continue;
            }
            std::cerr << "fsim-profile: native-process-count id=" << id
                      << " resumes=" << count
                      << " single="
                      << impl_->native_process_single_resume_counts[id]
                      << " single_static_wait="
                      << impl_->native_process_single_static_wait_counts[id]
                      << " cohort="
                      << impl_->native_process_cohort_resume_counts[id]
                      << " cohort_static_wait="
                      << impl_->native_process_cohort_static_wait_counts[id]
                      << " word_fanout_ready="
                      << impl_->native_process_word_fanout_ready_counts[id]
                      << " sensitivity="
                      << impl_->processes[id].program.static_sensitivity.size()
                      << " operations="
                      << impl_->processes[id].program.operations.size()
                      << " name='" << impl_->processes[id].program.name
                      << "'\n";
            if (!impl_->processes[id].program.static_sensitivity.empty()) {
                std::cerr << "fsim-profile: native-process-sensitivity id=" << id
                          << " signals=";
                const auto& sensitivity
                    = impl_->processes[id].program.static_sensitivity;
                for (std::size_t item = 0; item < sensitivity.size(); ++item) {
                    if (item != 0U) {
                        std::cerr << ',';
                    }
                    const auto signal = sensitivity[item].signal;
                    std::cerr << static_cast<std::size_t>(signal);
                    if (signal < impl_->signals.size()) {
                        std::cerr << ":'" << impl_->signals[signal].name << "'";
                    }
                }
                std::cerr << '\n';
            }
            if (id < impl_->native_process_static_trigger_counts.size()
                && !impl_->native_process_static_trigger_counts[id].empty()) {
                std::vector<std::pair<SignalId, std::uint64_t>> triggers(
                    impl_->native_process_static_trigger_counts[id].begin(),
                    impl_->native_process_static_trigger_counts[id].end());
                std::ranges::sort(
                    triggers,
                    [](const auto& left, const auto& right) {
                        return left.second != right.second
                            ? left.second > right.second
                            : left.first < right.first;
                    });
                std::cerr << "fsim-profile: native-process-triggers id=" << id
                          << " total=";
                const auto trigger_total = std::accumulate(
                    triggers.begin(), triggers.end(), std::uint64_t { },
                    [](const auto accumulated, const auto& trigger) {
                        return accumulated + trigger.second;
                    });
                std::cerr << trigger_total << " signals=";
                for (std::size_t trigger = 0; trigger < triggers.size();
                     ++trigger) {
                    if (trigger != 0U) {
                        std::cerr << ',';
                    }
                    std::cerr << static_cast<std::size_t>(
                                     triggers[trigger].first)
                              << ':' << triggers[trigger].second;
                }
                std::cerr << '\n';
            }
        }

        const auto process_count = std::min(
            {impl_->processes.size(),
             impl_->native_process_single_resume_counts.size(),
             impl_->native_process_single_static_wait_counts.size(),
             impl_->native_process_cohort_resume_counts.size(),
             impl_->native_process_word_fanout_ready_counts.size()});
        std::vector<bool> eligible(process_count, false);
        std::vector<std::size_t> parent(process_count);
        std::vector<std::size_t> component_size(process_count, 0U);
        std::vector<std::uint64_t> component_resumes(process_count, 0U);
        std::vector<std::uint64_t> component_fanout(process_count, 0U);
        std::vector<std::set<SignalId>> process_outputs(process_count);
        std::iota(parent.begin(), parent.end(), std::size_t { });
        std::size_t eligible_processes { };
        std::uint64_t eligible_resumes { };
        for (std::size_t id = 0; id < process_count; ++id) {
            const auto singles
                = impl_->native_process_single_resume_counts[id];
            eligible[id] = singles != 0U
                && singles
                    == impl_->native_process_single_static_wait_counts[id]
                && impl_->native_process_cohort_resume_counts[id] == 0U
                && !impl_->processes[id].program.static_sensitivity.empty();
            if (eligible[id]) {
                ++eligible_processes;
                eligible_resumes += singles;
            }
        }
        const auto root = [&](std::size_t id) {
            while (parent[id] != id) {
                parent[id] = parent[parent[id]];
                id = parent[id];
            }
            return id;
        };
        const auto join = [&](const std::size_t left,
                              const std::size_t right) {
            const auto left_root = root(left);
            const auto right_root = root(right);
            if (left_root != right_root) {
                parent[right_root] = left_root;
            }
        };
        std::set<std::uint64_t> edges;
        for (std::size_t id = 0; id < process_count; ++id) {
            if (!eligible[id]) {
                continue;
            }
            auto& outputs = process_outputs[id];
            const auto& program = impl_->processes[id].program;
            for (const auto& region : program.driver_regions) {
                outputs.insert(region.signal);
            }
            if (outputs.empty()) {
                for (const auto& operation : program.operations) {
                    if (const auto signal = output_signal(operation)) {
                        outputs.insert(*signal);
                    }
                }
            }
            for (const auto signal : outputs) {
                if (signal >= impl_->static_fanout.size()) {
                    continue;
                }
                for (const auto& fanout : impl_->static_fanout[signal]) {
                    const auto target
                        = static_cast<std::size_t>(fanout.process);
                    if (target >= process_count || !eligible[target]) {
                        continue;
                    }
                    const auto edge
                        = (static_cast<std::uint64_t>(id) << 32U)
                        | static_cast<std::uint64_t>(fanout.process);
                    if (edges.insert(edge).second) {
                        join(id, target);
                    }
                }
            }
        }
        for (std::size_t id = 0; id < process_count; ++id) {
            if (!eligible[id]) {
                continue;
            }
            const auto component = root(id);
            ++component_size[component];
            component_resumes[component]
                += impl_->native_process_single_resume_counts[id];
            component_fanout[component]
                += impl_->native_process_word_fanout_ready_counts[id];
        }
        std::vector<std::set<SignalId>> component_outputs(process_count);
        for (std::size_t id = 0; id < process_count; ++id) {
            if (!eligible[id]) {
                continue;
            }
            auto& outputs = component_outputs[root(id)];
            outputs.insert(
                process_outputs[id].begin(), process_outputs[id].end());
        }
        std::vector<std::size_t> component_direct_outputs(
            process_count, 0U);
        std::vector<std::size_t> component_unsafe_outputs(
            process_count, 0U);
        std::vector<std::size_t> component_internal_fanout(
            process_count, 0U);
        std::vector<std::size_t> component_external_fanout(
            process_count, 0U);
        std::vector<std::size_t> component_direct_internal_fanout(
            process_count, 0U);
        std::vector<std::size_t> component_unsafe_internal_fanout(
            process_count, 0U);
        std::array<std::uint64_t, 5U> sensitivity_kinds { };
        std::size_t direct_components { };
        std::size_t closed_direct_components { };
        std::uint64_t direct_component_resumes { };
        std::uint64_t closed_direct_component_resumes { };
        for (std::size_t component = 0; component < process_count;
             ++component) {
            if (component_size[component] == 0U) {
                continue;
            }
            for (const auto signal : component_outputs[component]) {
                const auto route_process
                    = signal < impl_->direct_single_driver_routes.size()
                        ? impl_->direct_single_driver_routes[signal].process
                        : ProcessId { };
                const bool direct
                    = impl_->can_publish_native_word(signal, route_process);
                if (direct) {
                    ++component_direct_outputs[component];
                } else {
                    ++component_unsafe_outputs[component];
                }
                if (signal >= impl_->static_fanout.size()) {
                    continue;
                }
                for (const auto& fanout : impl_->static_fanout[signal]) {
                    const auto kind = static_cast<std::size_t>(fanout.edge);
                    if (kind < sensitivity_kinds.size()) {
                        ++sensitivity_kinds[kind];
                    }
                    const auto target
                        = static_cast<std::size_t>(fanout.process);
                    if (target < process_count && eligible[target]
                        && root(target) == component) {
                        ++component_internal_fanout[component];
                        if (direct) {
                            ++component_direct_internal_fanout[component];
                        } else {
                            ++component_unsafe_internal_fanout[component];
                        }
                    } else {
                        ++component_external_fanout[component];
                    }
                }
            }
            if (component_unsafe_outputs[component] == 0U) {
                ++direct_components;
                direct_component_resumes += component_resumes[component];
                if (component_external_fanout[component] == 0U) {
                    ++closed_direct_components;
                    closed_direct_component_resumes
                        += component_resumes[component];
                }
            }
        }
        std::vector<std::uint32_t> wave_component_counts(
            process_count, 0U);
        std::vector<std::size_t> touched_wave_components;
        std::array<std::uint64_t, 4U> wave_size_buckets { };
        std::uint64_t eligible_wave_activations { };
        std::uint64_t component_waves { };
        std::uint64_t multi_process_component_waves { };
        std::uint64_t multi_process_wave_activations { };
        std::uint32_t maximum_processes_per_component_wave { };
        for (std::size_t wave = 0;
             wave < impl_->native_process_single_wave_offsets.size(); ++wave) {
            const auto begin
                = impl_->native_process_single_wave_offsets[wave];
            const auto end
                = wave + 1U
                        < impl_->native_process_single_wave_offsets.size()
                ? impl_->native_process_single_wave_offsets[wave + 1U]
                : impl_->native_process_single_wave_processes.size();
            touched_wave_components.clear();
            for (auto index = begin; index < end; ++index) {
                const auto id = static_cast<std::size_t>(
                    impl_->native_process_single_wave_processes[index]);
                if (id >= process_count || !eligible[id]) {
                    continue;
                }
                const auto component = root(id);
                if (wave_component_counts[component]++ == 0U) {
                    touched_wave_components.push_back(component);
                }
                ++eligible_wave_activations;
            }
            for (const auto component : touched_wave_components) {
                const auto count = wave_component_counts[component];
                wave_component_counts[component] = 0U;
                ++component_waves;
                maximum_processes_per_component_wave = std::max(
                    maximum_processes_per_component_wave, count);
                if (count == 1U) {
                    ++wave_size_buckets[0];
                } else if (count <= 4U) {
                    ++wave_size_buckets[1];
                } else if (count <= 16U) {
                    ++wave_size_buckets[2];
                } else {
                    ++wave_size_buckets[3];
                }
                if (count > 1U) {
                    ++multi_process_component_waves;
                    multi_process_wave_activations += count;
                }
            }
        }
        std::vector<std::size_t> components;
        for (std::size_t id = 0; id < process_count; ++id) {
            if (component_size[id] != 0U) {
                components.push_back(id);
            }
        }
        std::ranges::sort(
            components,
            [&](const auto left, const auto right) {
                return component_resumes[left] > component_resumes[right];
            });
        std::cerr << "fsim-profile: native-static-regions processes="
                  << eligible_processes
                  << " components=" << components.size()
                  << " edges=" << edges.size()
                  << " resumes=" << eligible_resumes
                  << " direct_components=" << direct_components
                  << " direct_resumes=" << direct_component_resumes
                  << " closed_direct_components="
                  << closed_direct_components
                  << " closed_direct_resumes="
                  << closed_direct_component_resumes
                  << " sensitivity_kinds=" << sensitivity_kinds[0] << ','
                  << sensitivity_kinds[1] << ',' << sensitivity_kinds[2]
                  << ',' << sensitivity_kinds[3] << ','
                  << sensitivity_kinds[4]
                  << " wave_activations=" << eligible_wave_activations
                  << " component_waves=" << component_waves
                  << " multi_component_waves="
                  << multi_process_component_waves
                  << " multi_wave_activations="
                  << multi_process_wave_activations
                  << " wave_sizes=" << wave_size_buckets[0] << ','
                  << wave_size_buckets[1] << ',' << wave_size_buckets[2]
                  << ',' << wave_size_buckets[3]
                  << " max_wave_size="
                  << maximum_processes_per_component_wave << '\n';
        for (const auto component : components | std::views::take(20U)) {
            auto first = process_count;
            for (std::size_t id = 0; id < process_count; ++id) {
                if (eligible[id] && root(id) == component) {
                    first = id;
                    break;
                }
            }
            std::cerr << "fsim-profile: native-static-region size="
                      << component_size[component]
                      << " resumes=" << component_resumes[component]
                      << " word_fanout_ready="
                      << component_fanout[component]
                      << " outputs="
                      << component_outputs[component].size()
                      << " direct_outputs="
                      << component_direct_outputs[component]
                      << " unsafe_outputs="
                      << component_unsafe_outputs[component]
                      << " internal_fanout="
                      << component_internal_fanout[component]
                      << " direct_internal_fanout="
                      << component_direct_internal_fanout[component]
                      << " unsafe_internal_fanout="
                      << component_unsafe_internal_fanout[component]
                      << " external_fanout="
                      << component_external_fanout[component]
                      << " first='"
                      << (first == process_count
                              ? std::string_view { }
                              : std::string_view {
                                    impl_->processes[first].program.name })
                      << "'\n";
        }
    }
    if (impl_->native_update_profile_enabled) {
        std::cerr << "fsim-profile: native-update calls="
                  << impl_->native_update_profile_calls
                  << " fallbacks=" << impl_->native_update_profile_fallbacks
                  << " batches=" << impl_->native_update_profile_batches
                  << " slots=" << impl_->native_update_profile_slots
                  << " inactive=" << impl_->native_update_profile_inactive
                  << " untouched=" << impl_->native_update_profile_untouched
                  << " unchanged=" << impl_->native_update_profile_unchanged
                  << " unchanged_direct_word="
                  << impl_->native_update_profile_unchanged_direct_word
                  << " unchanged_direct_packed="
                  << impl_->native_update_profile_unchanged_direct_packed
                  << " unchanged_unresolved="
                  << impl_->native_update_profile_unchanged_unresolved
                  << " unchanged_resolved="
                  << impl_->native_update_profile_unchanged_resolved
                  << " direct_word="
                  << impl_->native_update_profile_direct_word
                  << " direct_packed="
                  << impl_->native_update_profile_direct_packed
                  << " unresolved="
                  << impl_->native_update_profile_unresolved
                  << " resolved=" << impl_->native_update_profile_resolved
                  << " schedule_requests="
                  << impl_->native_update_profile_schedule_requests
                  << " schedule_coalesced="
                  << impl_->native_update_profile_schedule_coalesced
                  << " commits=" << impl_->native_update_profile_commits
                  << " commit_word_signals="
                  << impl_->native_update_profile_commit_word_signals
                  << " commit_value_signals="
                  << impl_->native_update_profile_commit_value_signals
                  << " word_calls="
                  << impl_->native_update_profile_word_calls
                  << " words=" << impl_->native_update_profile_words
                  << " word_fallbacks="
                  << impl_->native_update_profile_word_fallbacks
                  << " word_scalar="
                  << impl_->native_update_profile_word_scalar
                  << " word_unchanged="
                  << impl_->native_update_profile_word_unchanged
                  << " word_direct="
                  << impl_->native_update_profile_word_direct
                  << " word_unresolved="
                  << impl_->native_update_profile_word_unresolved
                  << " word_resolved="
                  << impl_->native_update_profile_word_resolved
                  << '\n';
    }
    if (std::getenv("FSIM_ENABLE_NATIVE_STATIC_REGIONS") != nullptr) {
        const auto members = std::accumulate(
            impl_->native_static_regions.begin(),
            impl_->native_static_regions.end(), std::size_t { },
            [](const auto total, const auto& region) {
                return total + region.members.size();
            });
        std::cerr << "fsim-profile: native-region regions="
                  << impl_->native_static_regions.size()
                  << " members=" << members
                  << " attempts=" << impl_->native_static_region_attempts
                  << " calls=" << impl_->native_static_region_calls
                  << " ready=" << impl_->native_static_region_ready
                  << " consumed=" << impl_->native_static_region_consumed
                  << " declines="
                  << impl_->native_static_region_declines[0] << ','
                  << impl_->native_static_region_declines[1] << ','
                  << impl_->native_static_region_declines[2] << ','
                  << impl_->native_static_region_declines[3] << ','
                  << impl_->native_static_region_declines[4] << '\n';
    }
    impl_->report_process_profile();
    impl_->report_update_profile();
}
Interpreter::Interpreter(Interpreter&&) noexcept = default;
Interpreter& Interpreter::operator=(Interpreter&&) noexcept = default;

void Interpreter::Impl::report_process_profile()
{
    if (!process_profile_enabled || process_profile_reported) {
        return;
    }
    process_profile_reported = true;
    std::vector<std::size_t> order(processes.size());
    std::iota(order.begin(), order.end(), std::size_t { });
    std::ranges::sort(order, [&](const auto left, const auto right) {
        if (update_profile_enabled
            && processes[left].profile_updates
                != processes[right].profile_updates) {
            return processes[left].profile_updates
                > processes[right].profile_updates;
        }
        return processes[left].profile_total_nanoseconds
            > processes[right].profile_total_nanoseconds;
    });
    std::uint64_t total_nanoseconds { };
    std::uint64_t native_nanoseconds { };
    std::uint64_t calls { };
    std::uint64_t native_resumes { };
    std::uint64_t interpreter_operations { };
    for (const auto& process : processes) {
        total_nanoseconds += process.profile_total_nanoseconds;
        native_nanoseconds += process.profile_native_nanoseconds;
        calls += process.profile_calls;
        native_resumes += process.profile_native_resumes;
        interpreter_operations += process.profile_interpreter_operations;
    }
    std::cerr << "fsim-profile: process-summary processes=" << processes.size()
              << " calls=" << calls
              << " interpreter_operations=" << interpreter_operations
              << " native_resumes=" << native_resumes
              << " total_ms="
              << static_cast<double>(total_nanoseconds) / 1'000'000.0
              << " native_ms="
              << static_cast<double>(native_nanoseconds) / 1'000'000.0
              << '\n';
    const auto count =
        std::getenv("FSIM_PROFILE_PROCESSES_ALL") != nullptr
        ? order.size()
        : std::min<std::size_t>(30U, order.size());
    for (std::size_t rank = 0; rank < count; ++rank) {
        const auto id = order[rank];
        const auto& process = processes[id];
        if (process.profile_calls == 0U) {
            break;
        }
        std::cerr << "fsim-profile: process rank=" << rank + 1U
                  << " id=" << id
                  << " compiled=" << (process.executor ? 1 : 0)
                  << " static_operations=" << process.program.operations.size()
                  << " sensitivity="
                  << process.program.static_sensitivity.size()
                  << " calls=" << process.profile_calls
                  << " interpreter_operations="
                  << process.profile_interpreter_operations
                  << " native_resumes=" << process.profile_native_resumes
                  << " updates=" << process.profile_updates
                  << " total_ms="
                  << static_cast<double>(process.profile_total_nanoseconds)
                / 1'000'000.0
                  << " native_ms="
                  << static_cast<double>(process.profile_native_nanoseconds)
                / 1'000'000.0
                  << " name=" << process.program.name << '\n';
    }
}

void Interpreter::Impl::report_update_profile()
{
    if (!update_profile_enabled || update_profile_reported) {
        return;
    }
    update_profile_reported = true;
    std::cerr << "fsim-profile: update commits=" << update_profile_commits
              << " updates=" << update_profile_updates
              << " whole=" << update_profile_whole
              << " slices=" << update_profile_slices
              << " unresolved=" << update_profile_unresolved
              << " resolved=" << update_profile_resolved
              << " resolved_single_driver="
              << update_profile_resolved_single_driver
              << " bits=" << update_profile_bits << '\n';
}

} // namespace fsim::runtime::simir
