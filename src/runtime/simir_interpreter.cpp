// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <cstdlib>
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
        std::cerr << "FSIM-NATIVE-PHASE-PROFILE attempts="
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
        std::cerr << "FSIM-NATIVE-PROCESS-COUNTS processes="
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
                std::cerr << "FSIM-NATIVE-SCHEDULING-BOUNDARY index="
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
            std::cerr << "FSIM-NATIVE-PROCESS-COUNT id=" << id
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
                std::cerr << "FSIM-NATIVE-PROCESS-SENSITIVITY id=" << id
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
                std::cerr << "FSIM-NATIVE-PROCESS-TRIGGERS id=" << id
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
        std::cerr << "FSIM-NATIVE-STATIC-REGIONS processes="
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
            std::cerr << "FSIM-NATIVE-STATIC-REGION size="
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
        std::cerr << "FSIM-NATIVE-UPDATE-PROFILE calls="
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
        std::cerr << "FSIM-NATIVE-REGION-PROFILE regions="
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
    std::cerr << "FSIM-PROCESS-PROFILE-SUMMARY processes=" << processes.size()
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
        std::cerr << "FSIM-PROCESS-PROFILE rank=" << rank + 1U
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
    std::cerr << "FSIM-UPDATE-PROFILE commits=" << update_profile_commits
              << " updates=" << update_profile_updates
              << " whole=" << update_profile_whole
              << " slices=" << update_profile_slices
              << " unresolved=" << update_profile_unresolved
              << " resolved=" << update_profile_resolved
              << " resolved_single_driver="
              << update_profile_resolved_single_driver
              << " bits=" << update_profile_bits << '\n';
}

void Interpreter::set_file_root(std::filesystem::path root)
{
    impl_->set_file_root(std::move(root));
}

void Interpreter::set_plusargs(const std::span<const std::string> plusargs)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot set SimIR plusargs after start"
        };
    }
    impl_->plusargs.assign(plusargs.begin(), plusargs.end());
}

void Interpreter::set_time_resolution_femtoseconds(
    const std::uint64_t femtoseconds)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot set SimIR time resolution after start"
        };
    }
    if (femtoseconds == 0) {
        throw std::invalid_argument {
            "SimIR time resolution must be positive"
        };
    }
    impl_->time_format.resolution_femtoseconds = femtoseconds;
    auto magnitude = femtoseconds;
    auto exponent = std::int32_t { -15 };
    while (magnitude >= 10 && exponent < 0) {
        magnitude /= 10;
        ++exponent;
    }
    impl_->time_format.units = exponent;
}

void Interpreter::set_class_allocate_hook(ClassAllocateHook hook)
{
    impl_->class_allocate_hook = std::move(hook);
}

void Interpreter::set_coverage_sample_hook(CoverageSampleHook hook)
{
    impl_->coverage_sample_hook = std::move(hook);
}

void Interpreter::set_coverage_query_hook(CoverageQueryHook hook)
{
    impl_->coverage_query_hook = std::move(hook);
}

void Interpreter::set_system_command_hook(SystemCommandHook hook)
{
    impl_->system_command_hook = std::move(hook);
}

void Interpreter::set_vcd_control_hook(VcdControlHook hook)
{
    impl_->vcd_control_hook = std::move(hook);
}

void Interpreter::set_coverage_database_control_hook(
    CoverageDatabaseControlHook hook)
{
    impl_->coverage_database_control_hook = std::move(hook);
}

void Interpreter::set_class_property_read_hook(ClassPropertyReadHook hook)
{
    impl_->class_property_read_hook = std::move(hook);
}

void Interpreter::set_class_property_write_hook(ClassPropertyWriteHook hook)
{
    impl_->class_property_write_hook = std::move(hook);
}

void Interpreter::set_class_method_call_hook(ClassMethodCallHook hook)
{
    impl_->class_method_call_hook = std::move(hook);
}

void Interpreter::set_class_static_property_read_hook(
    ClassStaticPropertyReadHook hook)
{
    impl_->class_static_property_read_hook = std::move(hook);
}

void Interpreter::set_class_static_property_write_hook(
    ClassStaticPropertyWriteHook hook)
{
    impl_->class_static_property_write_hook = std::move(hook);
}

void Interpreter::set_class_static_method_call_hook(
    ClassStaticMethodCallHook hook)
{
    impl_->class_static_method_call_hook = std::move(hook);
}

SignalId Interpreter::add_signal(Signal signal)
{
    if (impl_->started) {
        throw std::logic_error("cannot add a SimIR signal after start");
    }
    const auto id = static_cast<SignalId>(impl_->signals.size());
    if (static_cast<std::size_t>(id) != impl_->signals.size()) {
        throw std::length_error("too many SimIR signals");
    }
    const auto valid_strength = [](const StrengthRank rank) {
        return static_cast<std::underlying_type_t<StrengthRank>>(rank)
            <= static_cast<std::underlying_type_t<StrengthRank>>(
                StrengthRank::supply);
    };
    if (!valid_strength(signal.implicit_drive_strength.zero)
        || !valid_strength(signal.implicit_drive_strength.one)
        || (signal.charge_strength
            && !valid_strength(*signal.charge_strength))) {
        throw std::invalid_argument { "invalid SimIR signal strength metadata" };
    }
    if (signal.systemverilog_scalar != SystemVerilogScalarKind::None) {
        const auto scalar = decode_systemverilog_scalar_payload(
            signal.initial_value, signal.systemverilog_scalar);
        const auto classification = scalar
            ? classify_systemverilog_scalar(scalar.value)
            : SystemVerilogScalarClassification {
                  .error = SystemVerilogScalarError::InvalidKind
              };
        const bool valid_value = signal.systemverilog_scalar
                == SystemVerilogScalarKind::Time
            ? signal.initial_value.width() == 64U
                && !signal.initial_value.is_logic9()
            : scalar
                && (signal.systemverilog_scalar
                        == SystemVerilogScalarKind::Chandle
                    || (classification && classification.finite));
        if (!valid_value
            || signal.resolution != ResolutionKind::none
            || signal.implicit_driver || signal.charge_strength
            || signal.charge_decay) {
            throw std::invalid_argument {
                "invalid SimIR SystemVerilog scalar signal metadata"
            };
        }
    }
    impl_->driven_values.push_back(signal.initial_value);
    impl_->driver_values.emplace_back();
    impl_->driver_strengths.emplace_back();
    impl_->direct_single_driver_routes.emplace_back();
    impl_->direct_single_driver_word_scratch.emplace_back();
    impl_->direct_single_driver_logic9_word_scratch.emplace_back();
    impl_->native_word_update_signals.emplace_back();
    impl_->native_logic9_word_update_signals.emplace_back();
    impl_->direct_single_driver_processes.push_back(
        std::numeric_limits<ProcessId>::max());
    impl_->stable_single_writer_processes.push_back(
        std::numeric_limits<ProcessId>::max());
    impl_->signal_writer_counts.push_back(0U);
    impl_->forced_driver_values.emplace_back();
    impl_->forced_driver_masks.emplace_back();
    impl_->external_driver_values.emplace_back();
    impl_->charge_decay_handles.emplace_back();
    impl_->charge_values.push_back(
        signal.charge_strength
            ? std::optional<PackedLogic4> { signal.initial_value }
            : std::nullopt);
    impl_->signal_last_values.push_back(signal.initial_value);
    impl_->sampled_values.push_back(signal.initial_value);
    impl_->sampled_defaults.push_back(signal.initial_value);
    impl_->forced_values.emplace_back();
    impl_->forced_masks.emplace_back(
        signal.initial_value.width(), Logic4::zero);
    if (!signal.initial_value.is_logic9()
        && signal.initial_value.width() <= 64U) {
        const auto word = signal.initial_value.unchecked_low_word();
        impl_->direct_signal_aval.push_back(word.aval);
        impl_->direct_signal_bval.push_back(word.bval);
        impl_->direct_signal_last_aval.push_back(word.aval);
        impl_->direct_signal_last_bval.push_back(word.bval);
    } else {
        impl_->direct_signal_aval.push_back(0U);
        impl_->direct_signal_bval.push_back(0U);
        impl_->direct_signal_last_aval.push_back(0U);
        impl_->direct_signal_last_bval.push_back(0U);
    }
    if (signal.initial_value.is_logic9()
        && signal.initial_value.width() <= 64U) {
        const auto word = signal.initial_value.logic9_low_word();
        impl_->direct_signal_logic9_plane0.push_back(word.planes[0]);
        impl_->direct_signal_logic9_plane1.push_back(word.planes[1]);
        impl_->direct_signal_logic9_plane2.push_back(word.planes[2]);
        impl_->direct_signal_logic9_plane3.push_back(word.planes[3]);
        impl_->direct_signal_last_logic9_plane0.push_back(word.planes[0]);
        impl_->direct_signal_last_logic9_plane1.push_back(word.planes[1]);
        impl_->direct_signal_last_logic9_plane2.push_back(word.planes[2]);
        impl_->direct_signal_last_logic9_plane3.push_back(word.planes[3]);
    } else {
        impl_->direct_signal_logic9_plane0.push_back(0U);
        impl_->direct_signal_logic9_plane1.push_back(0U);
        impl_->direct_signal_logic9_plane2.push_back(0U);
        impl_->direct_signal_logic9_plane3.push_back(0U);
        impl_->direct_signal_last_logic9_plane0.push_back(0U);
        impl_->direct_signal_last_logic9_plane1.push_back(0U);
        impl_->direct_signal_last_logic9_plane2.push_back(0U);
        impl_->direct_signal_last_logic9_plane3.push_back(0U);
    }
    impl_->direct_signal_materialization_pending.push_back(0U);
    if (impl_->direct_wide_signal_aval.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error {
            "SimIR direct signal plane exceeds its native offset range"
        };
    }
    impl_->direct_wide_signal_offsets.push_back(
        static_cast<std::uint32_t>(
            impl_->direct_wide_signal_aval.size()));
    const auto words = signal.initial_value.aval_words().size();
    if (words > std::numeric_limits<std::uint32_t>::max()
            - impl_->direct_wide_signal_aval.size()) {
        throw std::length_error {
            "SimIR direct signal plane exceeds its native word range"
        };
    }
    if (!signal.initial_value.is_logic9()) {
        const auto aval = signal.initial_value.aval_words();
        const auto bval = signal.initial_value.bval_words();
        impl_->direct_wide_signal_aval.insert(
            impl_->direct_wide_signal_aval.end(),
            aval.begin(), aval.end());
        impl_->direct_wide_signal_bval.insert(
            impl_->direct_wide_signal_bval.end(),
            bval.begin(), bval.end());
        impl_->direct_wide_signal_logic9_plane2.resize(
            impl_->direct_wide_signal_logic9_plane2.size() + words);
        impl_->direct_wide_signal_logic9_plane3.resize(
            impl_->direct_wide_signal_logic9_plane3.size() + words);
    } else {
        const auto plane0 = signal.initial_value.logic9_plane_words(0U);
        const auto plane1 = signal.initial_value.logic9_plane_words(1U);
        const auto plane2 = signal.initial_value.logic9_plane_words(2U);
        const auto plane3 = signal.initial_value.logic9_plane_words(3U);
        impl_->direct_wide_signal_aval.insert(
            impl_->direct_wide_signal_aval.end(),
            plane0.begin(), plane0.end());
        impl_->direct_wide_signal_bval.insert(
            impl_->direct_wide_signal_bval.end(),
            plane1.begin(), plane1.end());
        impl_->direct_wide_signal_logic9_plane2.insert(
            impl_->direct_wide_signal_logic9_plane2.end(),
            plane2.begin(), plane2.end());
        impl_->direct_wide_signal_logic9_plane3.insert(
            impl_->direct_wide_signal_logic9_plane3.end(),
            plane3.begin(), plane3.end());
    }
    impl_->signals.push_back(std::move(signal));
    impl_->event_identities.push_back(id);
    impl_->static_fanout.emplace_back();
    impl_->dynamic_fanout.emplace_back();
    impl_->event_states.emplace_back();
    impl_->signal_events.emplace_back();
    impl_->signal_transactions.emplace_back();
    impl_->signal_container_aliases.emplace_back();
    impl_->signal_value_revisions.push_back(1U);
    return id;
}

StringObjectId Interpreter::add_string_object(StringObject object)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR string object after start"
        };
    }
    if (object.initial_value.size() > maximum_string_bytes) {
        throw std::length_error { "SimIR string object exceeds byte limit" };
    }
    (void)systemverilog_string_length(object.initial_value);
    const auto id = static_cast<StringObjectId>(impl_->string_objects.size());
    if (static_cast<std::size_t>(id)
        != impl_->string_objects.size()) {
        throw std::length_error { "too many SimIR string objects" };
    }
    impl_->string_objects.push_back(std::move(object));
    return id;
}

ContainerObjectId Interpreter::add_container_object(
    ContainerObject object)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR container object after start"
        };
    }
    validate_container_value(object.initial_value);
    const auto id = static_cast<ContainerObjectId>(
        impl_->container_objects.size());
    if (static_cast<std::size_t>(id)
        != impl_->container_objects.size()) {
        throw std::length_error { "too many SimIR container objects" };
    }
    if (object.slice_alias) {
        const auto& alias = *object.slice_alias;
        if (alias.object >= id) {
            throw std::invalid_argument {
                "a SimIR container slice alias must reference an earlier object"
            };
        }
        const auto& source = impl_->container_objects[alias.object].initial_value.type;
        const auto& target = object.initial_value.type;
        const auto element_count =
            [](const std::int32_t left,
                const std::int32_t right) {
                return static_cast<std::uint64_t>(
                           left >= right
                               ? static_cast<std::int64_t>(left) - right
                               : static_cast<std::int64_t>(right) - left)
                    + 1U;
            };
        const auto source_low = std::min(source.index_left, source.index_right);
        const auto source_high = std::max(source.index_left, source.index_right);
        const bool direction_matches = alias.selected_left == alias.selected_right
            || (alias.selected_left >= alias.selected_right)
                == (source.index_left >= source.index_right);
        if (!source.fixed || !target.fixed
            || alias.selected_left < source_low
            || alias.selected_left > source_high
            || alias.selected_right < source_low
            || alias.selected_right > source_high
            || !direction_matches
            || element_count(
                   alias.selected_left,
                   alias.selected_right)
                != element_count(
                    target.index_left, target.index_right)
            || source.element_width != target.element_width
            || source.two_state != target.two_state
            || source.signed_elements != target.signed_elements) {
            throw std::invalid_argument {
                "invalid SimIR static-array slice alias"
            };
        }
    }
    impl_->container_objects.push_back(std::move(object));
    impl_->container_signal_aliases.push_back(std::nullopt);
    impl_->container_materialized_revisions.push_back(std::nullopt);
    impl_->container_dynamic_fanout.emplace_back();
    return id;
}

void Interpreter::add_container_signal_alias(
    const ContainerSignalAlias alias)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR container signal alias after start"
        };
    }
    if (alias.object >= impl_->container_objects.size()
        || alias.signal >= impl_->signals.size()) {
        throw std::out_of_range {
            "SimIR container signal alias identifier is out of range"
        };
    }
    if (!alias.readable && !alias.writable) {
        throw std::invalid_argument {
            "a SimIR container signal alias must be readable or writable"
        };
    }
    if (impl_->container_signal_aliases[alias.object]) {
        throw std::invalid_argument {
            "a SimIR container object has more than one signal alias"
        };
    }
    const auto& object = impl_->container_objects[alias.object];
    if (object.slice_alias) {
        throw std::invalid_argument {
            "a SimIR container slice cannot alias a packed signal"
        };
    }
    const auto width = container_signal_bridge_width(object.initial_value.type);
    if (!width
        || *width
            != impl_->signals[alias.signal].initial_value.width()) {
        throw std::invalid_argument {
            "a SimIR container signal alias has incompatible width or shape"
        };
    }
    impl_->container_signal_aliases[alias.object] = alias;
    impl_->signal_container_aliases[alias.signal].push_back(alias.object);
}

ProcessId Interpreter::add_process(Process process)
{
    if (impl_->validation_only) {
        throw std::logic_error(
            "cannot add an executable SimIR process to a validation-only interpreter");
    }
    return add_process_impl(process, &process);
}

ProcessId Interpreter::validate_process(const Process& process)
{
    if (!impl_->validation_only && !impl_->processes.empty()) {
        throw std::logic_error(
            "cannot add a validation-only SimIR process after executable processes");
    }
    impl_->validation_only = true;
    return add_process_impl(process, nullptr);
}

ProcessId Interpreter::add_process_impl(
    const Process& process,
    Process* const owned_process)
{
    if (impl_->started) {
        throw std::logic_error("cannot add a SimIR process after start");
    }
    const auto id = static_cast<ProcessId>(impl_->processes.size());
    if (static_cast<std::size_t>(id) != impl_->processes.size()) {
        throw std::length_error("too many SimIR processes");
    }
    if (process.id != id) {
        throw std::invalid_argument("SimIR process IDs must be dense and ordered");
    }
    const auto valid_strength = [](const StrengthRank rank) {
        return static_cast<std::underlying_type_t<StrengthRank>>(rank)
            <= static_cast<std::underlying_type_t<StrengthRank>>(
                StrengthRank::supply);
    };
    if (!valid_strength(process.drive_strength.zero)
        || !valid_strength(process.drive_strength.one)) {
        throw std::invalid_argument { "invalid SimIR process drive strength" };
    }
    const bool has_switch_metadata = process.switch_source.has_value()
        || process.switch_target.has_value()
        || process.switch_control.has_value();
    const bool has_switch_region = process.switch_source_offset != 0
        || process.switch_target_offset != 0 || process.switch_width != 0;
    const bool switch_connection = process.switch_bidirectional;
    if ((switch_connection
            && (!process.switch_source || !process.switch_target))
        || (has_switch_region && !has_switch_metadata)
        || (has_switch_metadata
            && (!process.switch_source || !process.switch_target
                || *process.switch_source >= impl_->signals.size()
                || *process.switch_target >= impl_->signals.size()
                || (process.switch_control
                    && *process.switch_control >= impl_->signals.size())))) {
        throw std::invalid_argument {
            "SimIR transmission connection has invalid endpoint metadata"
        };
    }
    if (has_switch_metadata) {
        const auto source_width = impl_->signals[*process.switch_source]
                                      .initial_value.width();
        const auto target_width = impl_->signals[*process.switch_target]
                                      .initial_value.width();
        const auto selected_width = process.switch_width;
        const bool invalid_selected_region = selected_width != 0
            && (process.switch_source_offset > source_width
                || selected_width
                    > source_width - process.switch_source_offset
                || process.switch_target_offset > target_width
                || selected_width
                    > target_width - process.switch_target_offset);
        if (invalid_selected_region
            || (selected_width == 0
                && (process.switch_source_offset != 0
                    || process.switch_target_offset != 0
                    || (source_width != target_width
                        && source_width != 1 && target_width != 1)))
            || (process.switch_control
                && impl_->signals[*process.switch_control]
                        .initial_value.width()
                    != 1
                && impl_->signals[*process.switch_control]
                        .initial_value.width()
                    != (selected_width == 0
                            ? std::max(source_width, target_width)
                            : selected_width))) {
            throw std::invalid_argument {
                "SimIR transmission connection has incompatible endpoint widths"
            };
        }
    }
    if (process.final && process.initialize) {
        throw std::invalid_argument(
            "a SimIR final process cannot initialize at time zero");
    }
    const auto scheduling_regions = static_cast<unsigned>(process.observed)
        + static_cast<unsigned>(process.reactive)
        + static_cast<unsigned>(process.postponed);
    if (scheduling_regions > 1) {
        throw std::invalid_argument(
            "a SimIR process cannot occupy multiple scheduling regions");
    }
    if (!process.register_value_kinds.empty()
        && process.register_value_kinds.size()
            != process.register_count) {
        throw std::invalid_argument(
            "SimIR register value-kind count does not match register_count");
    }
    for (std::size_t sensitivity_index = 0;
         sensitivity_index < process.static_sensitivity.size();
         ++sensitivity_index) {
        const auto signal = process.static_sensitivity[sensitivity_index];
        if (signal.signal >= impl_->signals.size()) {
            throw std::invalid_argument("process sensitivity references invalid signal");
        }
        if (signal.edge != EdgeKind::any
            && signal.edge != EdgeKind::transaction && impl_->signals[signal.signal].initial_value.width() != 1) {
            throw std::invalid_argument(
                "edge sensitivity currently requires a scalar signal");
        }
        const auto trigger_mask
            = sensitivity_index < 63U
                && !process.static_trigger_regions.empty()
            ? UINT64_C(1) << sensitivity_index
            : Process::full_static_trigger_mask;
        impl_->static_fanout[signal.signal].push_back(
            { id, signal.edge, trigger_mask });
    }
    std::set<std::string> local_names;
    for (const auto& local : process.debug_locals) {
        if (local.name.empty()
            || local.register_id >= process.register_count) {
            throw std::invalid_argument { "invalid SimIR debug-local metadata" };
        }
        if (local.systemverilog_scalar != SystemVerilogScalarKind::None) {
            const auto expected_width = local.systemverilog_scalar
                    == SystemVerilogScalarKind::ShortReal
                ? 32U
                : local.systemverilog_scalar == SystemVerilogScalarKind::Real
                    || local.systemverilog_scalar
                        == SystemVerilogScalarKind::Realtime
                    || local.systemverilog_scalar
                        == SystemVerilogScalarKind::Time
                    || local.systemverilog_scalar
                        == SystemVerilogScalarKind::Chandle
                ? 64U
                : 0U;
            if (local.width != expected_width
                || local.value_kind != ValueKind::logic4) {
                throw std::invalid_argument {
                    "invalid SimIR scalar debug-local metadata"
                };
            }
        }
        if (!local_names.insert(local.name).second) {
            throw std::invalid_argument { "duplicate SimIR debug-local name" };
        }
    }
    std::set<std::string> string_local_names;
    for (const auto& local : process.debug_string_locals) {
        if (local.name.empty()
            || local.register_id >= process.string_register_count) {
            throw std::invalid_argument {
                "invalid SimIR string debug-local metadata"
            };
        }
        if (!string_local_names.insert(local.name).second
            || local_names.contains(local.name)) {
            throw std::invalid_argument {
                "duplicate SimIR debug-local name"
            };
        }
    }
    if (process.container_register_types.size()
        != process.container_register_count) {
        throw std::invalid_argument {
            "SimIR container register type count does not match register count"
        };
    }
    std::set<std::string> container_local_names;
    for (const auto& local : process.debug_container_locals) {
        if (local.name.empty()
            || local.register_id >= process.container_register_count
            || local.type != process.container_register_types.at(local.register_id)) {
            throw std::invalid_argument {
                "invalid SimIR container debug-local metadata"
            };
        }
        if (!container_local_names.insert(local.name).second
            || local_names.contains(local.name)
            || string_local_names.contains(local.name)) {
            throw std::invalid_argument {
                "duplicate SimIR debug-local name"
            };
        }
    }
    std::map<SignalId, std::vector<Process::DriverRegion>> outputs;
    if (process.driver_regions.empty()) {
        for (const auto& operation : process.operations) {
            const auto signal = output_signal(operation);
            if (signal) {
                outputs[*signal].push_back(
                    Process::DriverRegion { *signal, 0, 0, true });
            }
        }
    } else {
        for (const auto& region : process.driver_regions) {
            outputs[region.signal].push_back(region);
        }
    }
    for (const auto& [signal, regions] : outputs) {
        if (signal >= impl_->signals.size()) {
            throw std::invalid_argument(
                "process output references invalid signal");
        }
        if (!switch_connection) {
            auto& writer_count = impl_->signal_writer_counts[signal];
            if (writer_count != std::numeric_limits<std::uint32_t>::max()) {
                ++writer_count;
            }
            impl_->stable_single_writer_processes[signal]
                = writer_count == 1U
                ? id
                : std::numeric_limits<ProcessId>::max();
            ++impl_->signal_writer_revision;
            if (impl_->signal_writer_revision == 0U) {
                throw std::overflow_error {
                    "SimIR signal-writer topology revision overflow"
                };
            }
            impl_->register_driver(
                id, signal, regions, process.drive_strength);
        }
    }

    Impl::ProcessState state;
    if (impl_->next_process_generation == 0) {
        throw std::overflow_error { "SimIR process generation overflow" };
    }
    state.generation = impl_->next_process_generation++;
    if (owned_process != nullptr) {
        state.frame = std::make_shared<Impl::ProcessFrame>();
        state.frame->registers.assign(
            process.register_count, PackedLogic4 { });
        state.frame->string_registers.assign(
            process.string_register_count, { });
        state.frame->container_registers.reserve(
            process.container_register_count);
        for (const auto& type : process.container_register_types) {
            state.frame->container_registers.push_back(
                default_container_value(type));
        }
    }
    state.random_state = Impl::initial_random_state(
        impl_->root_seed, id);
    state.design_process = id;
    state.static_trigger_mask = process.initialize
        ? Process::full_static_trigger_mask
        : 0U;
    state.waiting_on_static = !process.initialize;
    state.status = process.initialize
        ? ProcessStatus::running
        : ProcessStatus::waiting;
    if (process.program_owner) {
        impl_->program_owners.insert(*process.program_owner);
    }
    impl_->requires_sampled_values = impl_->requires_sampled_values
        || std::ranges::any_of(
            process.operations, [](const Operation& operation) {
                const auto* read
                    = fsim::runtime::simir::operation_get_if<ReadSignal>(
                        &operation);
                return read && read->kind != SignalReadKind::current;
            });
    impl_->has_bidirectional_switches
        = impl_->has_bidirectional_switches || switch_connection;
    if (owned_process != nullptr) {
        state.program = std::move(*owned_process);
    } else {
        state.program.id = id;
    }
    impl_->processes.push_back(std::move(state));
    if (owned_process != nullptr) {
        impl_->register_static_sensitivity_cohort(id);
    }
    return id;
}

std::uint32_t Interpreter::add_module_path(ModulePath path)
{
    if (impl_->started) {
        throw std::logic_error("cannot add a SimIR module path after start");
    }
    const auto id = static_cast<std::uint32_t>(impl_->module_paths.size());
    if (static_cast<std::size_t>(id) != impl_->module_paths.size()) {
        throw std::length_error("too many SimIR module paths");
    }
    if (path.id != id) {
        throw std::invalid_argument(
            "SimIR module-path IDs must be dense and ordered");
    }
    if (path.identity.empty()
        || std::ranges::any_of(
            impl_->module_paths,
            [&](const ModulePath& candidate) {
                return candidate.identity == path.identity;
            })
        || std::ranges::any_of(
            impl_->module_timing_checks,
            [&](const ModuleTimingCheck& candidate) {
                return candidate.identity == path.identity;
            })) {
        throw std::invalid_argument(
            "SimIR module-path identities must be nonempty and unique");
    }
    const auto valid_terminal = [&](const ModulePathTerminal& terminal) {
        if (terminal.signal >= impl_->signals.size() || terminal.width == 0) {
            return false;
        }
        const auto width = impl_->signals[terminal.signal].initial_value.width();
        return terminal.offset <= width
            && terminal.width <= width - terminal.offset;
    };
    if (path.sources.empty() || path.destinations.empty()
        || !std::ranges::all_of(path.sources, valid_terminal)
        || !std::ranges::all_of(path.destinations, valid_terminal)) {
        throw std::invalid_argument("invalid SimIR module-path terminals");
    }
    const bool valid_delay_count = path.delays.size() == 1
        || path.delays.size() == 2 || path.delays.size() == 3
        || path.delays.size() == 6 || path.delays.size() == 12;
    if (!valid_delay_count) {
        throw std::invalid_argument("invalid SimIR module-path delay count");
    }
    const auto valid_optional_delay_table = [](const auto& values) {
        return values.empty() || values.size() == 1U || values.size() == 2U
            || values.size() == 3U || values.size() == 6U
            || values.size() == 12U;
    };
    if (!valid_optional_delay_table(path.pulse_reject_delays)
        || !valid_optional_delay_table(path.pulse_error_delays)
        || !valid_optional_delay_table(path.retain_delays)
        || path.pulse_reject_delays.empty()
            != path.pulse_error_delays.empty()
        || path.pulse_reject_delays.size()
            != path.pulse_error_delays.size()
        || (!path.pulse_reject_delays.empty()
            && !std::ranges::equal(path.pulse_reject_delays,
                path.pulse_error_delays,
                [](const auto reject, const auto error) {
                    return reject <= error;
                }))) {
        throw std::invalid_argument("invalid SimIR module-path pulse tables");
    }
    if (!path.full
        && (path.sources.size() != path.destinations.size()
            || !std::ranges::equal(
                path.sources, path.destinations,
                [](const auto& source, const auto& destination) {
                    return source.width == destination.width;
                }))) {
        throw std::invalid_argument(
            "parallel SimIR module-path terminal widths do not match");
    }
    if (!std::ranges::all_of(
            path.drivers,
            [&](const ProcessId driver) {
                return driver < impl_->processes.size();
            })
        || !std::ranges::is_sorted(path.drivers)
        || std::ranges::adjacent_find(path.drivers)
            != path.drivers.end()) {
        throw std::invalid_argument("invalid SimIR module-path driver set");
    }
    if (path.source_edge > ModulePathEdge::edge
        || path.polarity > ModulePathPolarity::negative
        || path.pulse_style > ModulePathPulseStyle::ondetect
        || path.selection_group > path.id || (path.conditional && path.ifnone)
        || path.conditional == path.condition.empty()
        || path.pulse_reject_limit.has_value()
            != path.pulse_error_limit.has_value()
        || (path.pulse_reject_limit
            && *path.pulse_reject_limit > *path.pulse_error_limit)) {
        throw std::invalid_argument("invalid SimIR module-path enumeration");
    }
    validate_module_path_expression(path.condition, impl_->signals);
    validate_module_path_expression(path.data_source, impl_->signals);
    impl_->module_paths.push_back(std::move(path));
    return id;
}

std::uint32_t Interpreter::add_module_timing_check(
    ModuleTimingCheck check)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot add a SimIR module timing check after start"
        };
    }
    const auto id = static_cast<std::uint32_t>(
        impl_->module_timing_checks.size());
    if (static_cast<std::size_t>(id)
        != impl_->module_timing_checks.size()) {
        throw std::length_error { "too many SimIR module timing checks" };
    }
    const auto valid_event = [&](const ModuleTimingEvent& event) {
        return event.terminal.signal < impl_->signals.size()
            && event.terminal.width == 1
            && event.terminal.offset
            < impl_->signals[event.terminal.signal].initial_value.width()
            && event.edge <= ModulePathEdge::edge;
    };
    const bool identity_valid = !check.identity.empty()
        && std::ranges::none_of(
            impl_->module_timing_checks,
            [&](const ModuleTimingCheck& candidate) {
                return candidate.identity == check.identity;
            })
        && std::ranges::none_of(
            impl_->module_paths,
            [&](const ModulePath& candidate) {
                return candidate.identity == check.identity;
            });
    const auto valid_delayed_terminal = [&](const ModulePathTerminal& terminal) {
        return terminal.signal < impl_->signals.size()
            && terminal.width == 1
            && terminal.offset
            < impl_->signals[terminal.signal].initial_value.width();
    };
    const bool compound = check.kind == ModuleTimingCheckKind::setuphold
        || check.kind == ModuleTimingCheckKind::recrem
        || check.kind == ModuleTimingCheckKind::fullskew
        || check.kind == ModuleTimingCheckKind::nochange;
    const auto expected_limits = compound ? 2U : 1U;
    bool valid_compound_sum = !compound;
    if (check.limits.size() == 2) {
        const bool overflow = (check.limits[1] > 0
                                  && check.limits[0]
                                      > std::numeric_limits<std::int64_t>::max()
                                          - check.limits[1])
            || (check.limits[1] < 0
                && check.limits[0]
                    < std::numeric_limits<std::int64_t>::min()
                        - check.limits[1]);
        valid_compound_sum = !overflow
            && check.limits[0] + check.limits[1] > 0;
    }
    const bool controlled_reference = check.reference.edge != ModulePathEdge::none;
    if (check.id != id || !identity_valid
        || check.kind > ModuleTimingCheckKind::nochange
        || !valid_event(check.reference)
        || ((check.kind == ModuleTimingCheckKind::period
                || check.kind == ModuleTimingCheckKind::width)
            && !controlled_reference)
        || (check.kind != ModuleTimingCheckKind::period
            && check.kind != ModuleTimingCheckKind::width
            && (!check.data || !valid_event(*check.data)))
        || check.limits.size() != expected_limits
        || ((check.kind != ModuleTimingCheckKind::setuphold
                && check.kind != ModuleTimingCheckKind::recrem
                && check.kind != ModuleTimingCheckKind::nochange)
            && std::ranges::any_of(
                check.limits,
                [](const std::int64_t limit) { return limit < 0; }))
        || ((check.kind == ModuleTimingCheckKind::setuphold
                || check.kind == ModuleTimingCheckKind::recrem)
            && !valid_compound_sum)
        || (check.kind == ModuleTimingCheckKind::nochange
            && check.limits.size() == 2
            && check.limits[0] > check.limits[1])
        || (check.threshold.has_value()
            && check.kind != ModuleTimingCheckKind::width)
        || (check.notifier
            && (*check.notifier >= impl_->signals.size()
                || impl_->signals[*check.notifier].initial_value.width() != 1))
        || (check.delayed_reference
            && !valid_delayed_terminal(*check.delayed_reference))
        || (check.delayed_data
            && !valid_delayed_terminal(*check.delayed_data))) {
        throw std::invalid_argument { "invalid SimIR module timing check" };
    }
    validate_module_path_expression(check.reference.condition, impl_->signals);
    if (check.data) {
        validate_module_path_expression(check.data->condition, impl_->signals);
    }
    validate_module_path_expression(check.timestamp_condition, impl_->signals);
    validate_module_path_expression(check.timecheck_condition, impl_->signals);
    impl_->module_timing_checks.push_back(std::move(check));
    impl_->module_timing_check_states.emplace_back();
    return id;
}

void Interpreter::reannotate_module_timing(
    const std::span<const ModulePath> paths,
    const std::span<const ModuleTimingCheck> checks)
{
    if (impl_->started && !impl_->scheduler.at_safe_point()) {
        throw std::logic_error {
            "SimIR timing reannotation requires a scheduler safe point"
        };
    }
    if (paths.size() != impl_->module_paths.size()
        || checks.size() != impl_->module_timing_checks.size()) {
        throw std::invalid_argument {
            "SimIR timing reannotation changed timing topology"
        };
    }
    const auto valid_delay_table = [](const auto& values) {
        return values.size() == 1U || values.size() == 2U
            || values.size() == 3U || values.size() == 6U
            || values.size() == 12U;
    };
    const auto valid_optional_delay_table = [&](const auto& values) {
        return values.empty() || valid_delay_table(values);
    };
    const auto same_expression = [](const ModulePathExpression& left,
                                     const ModulePathExpression& right) {
        return left.root == right.root && left.nodes.size() == right.nodes.size()
            && std::ranges::equal(
                left.nodes,
                right.nodes,
                [](const ModulePathExpressionNode& left_node,
                    const ModulePathExpressionNode& right_node) {
                    return left_node.operation == right_node.operation
                        && left_node.operands == right_node.operands
                        && left_node.constant == right_node.constant
                        && left_node.terminal == right_node.terminal
                        && left_node.binary == right_node.binary
                        && left_node.logical == right_node.logical
                        && left_node.shift == right_node.shift
                        && left_node.reduction == right_node.reduction
                        && left_node.width == right_node.width
                        && left_node.is_signed == right_node.is_signed;
                });
    };
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& before = impl_->module_paths[index];
        const auto& after = paths[index];
        if (after.id != before.id || after.identity != before.identity
            || after.sources != before.sources
            || after.destinations != before.destinations
            || after.drivers != before.drivers || after.full != before.full
            || after.conditional != before.conditional
            || after.ifnone != before.ifnone
            || after.selection_group != before.selection_group
            || after.source_edge != before.source_edge
            || after.polarity != before.polarity
            || after.pulse_style != before.pulse_style
            || after.show_cancelled != before.show_cancelled
            || !same_expression(after.condition, before.condition)
            || !same_expression(after.data_source, before.data_source)
            || !valid_delay_table(after.delays)
            || !valid_optional_delay_table(after.pulse_reject_delays)
            || !valid_optional_delay_table(after.pulse_error_delays)
            || !valid_optional_delay_table(after.retain_delays)
            || after.pulse_reject_delays.empty()
                != after.pulse_error_delays.empty()
            || after.pulse_reject_delays.size()
                != after.pulse_error_delays.size()
            || (!after.pulse_reject_delays.empty()
                && !std::ranges::equal(
                    after.pulse_reject_delays,
                    after.pulse_error_delays,
                    [](const auto reject, const auto error) {
                        return reject <= error;
                    }))
            || after.pulse_reject_limit.has_value()
                != after.pulse_error_limit.has_value()
            || (after.pulse_reject_limit
                && *after.pulse_reject_limit > *after.pulse_error_limit)) {
            throw std::invalid_argument {
                "SimIR timing reannotation changed path topology"
            };
        }
    }
    for (std::size_t index = 0; index < checks.size(); ++index) {
        const auto& before = impl_->module_timing_checks[index];
        const auto& after = checks[index];
        if (after.id != before.id || after.identity != before.identity
            || after.kind != before.kind
            || after.reference.terminal != before.reference.terminal
            || after.reference.edge != before.reference.edge
            || after.reference.edge_descriptors
                != before.reference.edge_descriptors
            || !same_expression(
                after.reference.condition, before.reference.condition)
            || after.data.has_value() != before.data.has_value()
            || (after.data
                && (after.data->terminal != before.data->terminal
                    || after.data->edge != before.data->edge
                    || after.data->edge_descriptors
                        != before.data->edge_descriptors
                    || !same_expression(
                        after.data->condition, before.data->condition)))
            || after.notifier != before.notifier
            || after.delayed_reference != before.delayed_reference
            || after.delayed_data != before.delayed_data
            || after.event_based != before.event_based
            || after.remain_active != before.remain_active
            || !same_expression(
                after.timestamp_condition, before.timestamp_condition)
            || !same_expression(
                after.timecheck_condition, before.timecheck_condition)
            || after.limits.size() != before.limits.size()) {
            throw std::invalid_argument {
                "SimIR timing reannotation changed timing-check topology"
            };
        }
        const bool compound = after.kind == ModuleTimingCheckKind::setuphold
            || after.kind == ModuleTimingCheckKind::recrem
            || after.kind == ModuleTimingCheckKind::fullskew
            || after.kind == ModuleTimingCheckKind::nochange;
        bool valid_compound_sum = !compound;
        if (after.limits.size() == 2U) {
            const bool overflow = (after.limits[1] > 0
                                      && after.limits[0]
                                          > std::numeric_limits<
                                                std::int64_t>::max()
                                              - after.limits[1])
                || (after.limits[1] < 0
                    && after.limits[0]
                        < std::numeric_limits<std::int64_t>::min()
                            - after.limits[1]);
            valid_compound_sum = !overflow
                && after.limits[0] + after.limits[1] > 0;
        }
        if (((after.kind != ModuleTimingCheckKind::setuphold
                 && after.kind != ModuleTimingCheckKind::recrem
                 && after.kind != ModuleTimingCheckKind::nochange)
                && std::ranges::any_of(
                    after.limits,
                    [](const std::int64_t limit) { return limit < 0; }))
            || ((after.kind == ModuleTimingCheckKind::setuphold
                    || after.kind == ModuleTimingCheckKind::recrem)
                && !valid_compound_sum)
            || (after.kind == ModuleTimingCheckKind::nochange
                && after.limits[0] > after.limits[1])
            || (after.threshold.has_value()
                && after.kind != ModuleTimingCheckKind::width)) {
            throw std::invalid_argument {
                "SimIR timing reannotation has invalid timing-check values"
            };
        }
    }
    std::vector<ModulePath> replacement_paths(paths.begin(), paths.end());
    std::vector<ModuleTimingCheck> replacement_checks(
        checks.begin(), checks.end());
    impl_->module_paths.swap(replacement_paths);
    impl_->module_timing_checks.swap(replacement_checks);
}

void Interpreter::reannotate_vital_timing(
    const std::span<const VitalTimingReannotation> annotations,
    const bool reset_timing_state)
{
    if (impl_->started && !impl_->scheduler.at_safe_point()) {
        throw std::logic_error {
            "SimIR VITAL reannotation requires a scheduler safe point"
        };
    }
    struct Replacement {
        struct Definition {
            InstructionIndex instruction { };
            RegisterId target { };
            SimulationTick value { };
        };

        Impl::ProcessState* process { };
        InstructionIndex instruction { };
        bool timing_check { };
        std::array<SimulationTick, 6> values { };
        std::size_t value_count { };
        std::vector<Definition> definitions;
    };
    std::vector<Replacement> replacements;
    replacements.reserve(annotations.size());
    std::set<std::pair<ProcessId, InstructionIndex>> calls;
    std::map<std::pair<ProcessId, InstructionIndex>,
        std::pair<RegisterId, SimulationTick>>
        definitions;
    for (const auto& annotation : annotations) {
        if (annotation.process >= impl_->processes.size()
            || impl_->processes[annotation.process].program.id
                != annotation.process
            || annotation.instruction
                >= impl_->processes[annotation.process]
                    .program.operations.size()) {
            throw std::invalid_argument {
                "SimIR VITAL reannotation references a stale call"
            };
        }
        if (!calls.emplace(annotation.process, annotation.instruction).second) {
            throw std::invalid_argument {
                "SimIR VITAL reannotation references a call more than once"
            };
        }
        auto& process = impl_->processes[annotation.process];
        const auto& operation
            = process.program.operations[annotation.instruction];
        if (annotation.timing_check) {
            if (annotation.value_count != 4U
                || operation_get_if<VitalTimingCheck>(&operation) == nullptr) {
                throw std::invalid_argument {
                    "SimIR VITAL reannotation changed timing-check topology"
                };
            }
        } else {
            const auto* delay = operation_get_if<VitalDelay>(&operation);
            const auto expected = delay == nullptr
                ? 0U
                : delay->shape == VitalDelayShape::single
                ? 1U
                : delay->shape == VitalDelayShape::delay01 ? 2U
                                                           : 6U;
            if (delay == nullptr || annotation.value_count != expected) {
                throw std::invalid_argument {
                    "SimIR VITAL reannotation changed delay topology"
                };
            }
        }
        Replacement replacement { &process, annotation.instruction,
            annotation.timing_check, annotation.values,
            annotation.value_count, { } };
        if (!annotation.timing_check) {
            const auto* delay = operation_get_if<VitalDelay>(&operation);
            std::unordered_map<RegisterId, InstructionIndex> retained;
            for (InstructionIndex index = 0; index < annotation.instruction;
                ++index) {
                if (const auto* load = operation_get_if<LoadConstant>(
                        &process.program.operations[index])) {
                    retained[load->destination] = index;
                } else if (const auto* extract = operation_get_if<Extract>(
                               &process.program.operations[index])) {
                    retained[extract->destination] = index;
                }
            }
            const auto plan = [&](const RegisterId target,
                                  const SimulationTick value) {
                const auto found = retained.find(target);
                if (found == retained.end()) {
                    throw std::invalid_argument {
                        "SimIR VITAL reannotation lost a static delay definition"
                    };
                }
                const auto key
                    = std::pair { annotation.process, found->second };
                const auto [planned, inserted]
                    = definitions.emplace(key, std::pair { target, value });
                if (!inserted
                    && (planned->second.first != target
                        || planned->second.second != value)) {
                    throw std::invalid_argument {
                        "SimIR VITAL reannotation has conflicting static delay values"
                    };
                }
                if (inserted) {
                    replacement.definitions.push_back(
                        { found->second, target, value });
                }
            };
            for (std::size_t index = 0; index < annotation.value_count;
                ++index) {
                plan(delay->default_delays[index], annotation.values[index]);
                for (const auto& path : delay->paths)
                    plan(path.delays[index], annotation.values[index]);
            }
        }
        replacements.push_back(std::move(replacement));
    }
    for (const auto& replacement : replacements) {
        auto& operation
            = replacement.process->program.operations[replacement.instruction];
        if (replacement.timing_check) {
            auto* check = operation_get_if<VitalTimingCheck>(&operation);
            std::copy_n(replacement.values.begin(), 4U,
                check->limits.begin());
            if (reset_timing_state) {
                replacement.process->vital_timing_states.erase(
                    replacement.instruction);
            }
            continue;
        }
        for (const auto& definition : replacement.definitions) {
            replacement.process->program.operations[definition.instruction]
                = LoadConstant { definition.target,
                      PackedLogic4::from_aval_bval(
                          64U, definition.value, 0U) };
        }
    }
}

void Interpreter::set_process_executor(
    const ProcessId process,
    std::unique_ptr<ProcessExecutor> executor)
{
    if (impl_->started) {
        throw std::logic_error(
            "cannot install a SimIR process executor after start");
    }
    if (!executor) {
        throw std::invalid_argument("SimIR process executor cannot be null");
    }
    auto& state = impl_->get_process(process);
    if (state.executor) {
        throw std::logic_error(
            "a SimIR process executor is already installed");
    }
    state.executor = std::move(executor);
}

void Interpreter::set_deferred_process_executor(
    const ProcessId process,
    std::function<bool()> ready,
    std::function<std::unique_ptr<ProcessExecutor>()> take)
{
    if (impl_->started) {
        throw std::logic_error(
            "cannot defer a SimIR process executor after start");
    }
    if (!ready || !take) {
        throw std::invalid_argument(
            "deferred SimIR process executor requires both callbacks");
    }
    auto& state = impl_->get_process(process);
    if (state.executor || state.deferred_executor) {
        throw std::logic_error(
            "a SimIR process executor is already installed or deferred");
    }
    state.deferred_executor.emplace(
        Impl::ProcessState::DeferredExecutor {
            std::move(ready), std::move(take) });
}

void Interpreter::materialize_ready_process_executors()
{
    if (impl_->started) {
        return;
    }
    for (auto& process : impl_->processes) {
        if (!process.executor && process.deferred_executor
            && process.frame.use_count() == 1
            && process.frame->vital_memories.empty()
            && process.dynamic_call_stack.empty()
            && process.callable_frames.empty()
            && process.deferred_executor->ready()) {
            impl_->install_deferred_executor(process);
        }
    }
}

void Interpreter::start()
{
    if (impl_->validation_only) {
        throw std::logic_error(
            "cannot start a validation-only SimIR interpreter");
    }
    if (impl_->started) {
        return;
    }
    impl_->started = true;
    if (impl_->native_process_count_profile_enabled) {
        impl_->native_process_resume_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_single_resume_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_single_static_wait_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_cohort_resume_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_cohort_static_wait_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_word_fanout_ready_counts.assign(
            impl_->processes.size(), std::uint64_t { });
        impl_->native_process_static_trigger_counts.assign(
            impl_->processes.size(), { });
        impl_->native_process_single_wave_processes.clear();
        impl_->native_process_single_wave_offsets.clear();
        impl_->native_process_single_wave_identity.reset();
    }
    if (std::getenv("FSIM_PROFILE_STATIC_COHORTS") != nullptr) {
        std::vector<std::size_t> cohorts;
        for (std::size_t index = 0;
            index < impl_->static_sensitivity_cohorts.size(); ++index) {
            if (impl_->static_sensitivity_cohorts[index].members.size() > 1U) {
                cohorts.push_back(index);
            }
        }
        std::ranges::sort(cohorts, [&](const auto left, const auto right) {
            return impl_->static_sensitivity_cohorts[left].members.size()
                > impl_->static_sensitivity_cohorts[right].members.size();
        });
        std::size_t grouped_processes { };
        for (const auto cohort : cohorts) {
            grouped_processes += impl_->static_sensitivity_cohorts[cohort].members.size();
        }
        std::cerr << "FSIM-STATIC-COHORTS cohorts=" << cohorts.size()
                  << " processes=" << grouped_processes << '\n';
        for (const auto cohort : cohorts | std::views::take(20U)) {
            const auto& members
                = impl_->static_sensitivity_cohorts[cohort].members;
            const auto& first = impl_->processes[members.front()].program;
            std::cerr << "  size=" << members.size()
                      << " sensitivity=" << first.static_sensitivity.size()
                      << " first=" << first.name << '\n';
        }
    }
    impl_->build_native_static_regions();
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (impl_->processes[id].program.initialize
            && !impl_->processes[id].program.final) {
            impl_->queue_at(id, impl_->scheduler.now());
        }
    }
}

RunResult Interpreter::run(std::optional<SimulationTick> until)
{
    start();
    auto ordinary = impl_->scheduler.run(until);
    const bool design_stop = ordinary.status == RunStatus::stopped
        && impl_->stopped_by_design;
    if (impl_->finals_ran
        || (ordinary.status != RunStatus::completed
            && !design_stop)) {
        return ordinary;
    }

    impl_->finals_ran = true;
    if (design_stop) {
        impl_->scheduler.discard_pending();
        impl_->scheduler.clear_stop();
    }
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (impl_->processes[id].program.final) {
            impl_->queue_at(id, impl_->scheduler.now());
        }
    }
    if (!impl_->scheduler.has_pending()) {
        if (design_stop) {
            impl_->scheduler.request_stop();
        }
        return ordinary;
    }

    const auto final_result = impl_->scheduler.run();
    ordinary.time = final_result.time;
    ordinary.delta = final_result.delta;
    ordinary.callbacks_executed += final_result.callbacks_executed;
    if (design_stop) {
        ordinary.status = RunStatus::stopped;
        impl_->scheduler.request_stop();
    } else {
        ordinary.status = final_result.status;
    }
    return ordinary;
}

RunResult Interpreter::finish()
{
    start();
    RunResult result {
        RunStatus::stopped,
        impl_->scheduler.now(),
        impl_->scheduler.delta(),
        0,
    };
    if (impl_->finals_ran) {
        impl_->scheduler.request_stop();
        return result;
    }

    impl_->finals_ran = true;
    impl_->scheduler.discard_pending();
    impl_->scheduler.clear_stop();
    for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
        if (impl_->processes[id].program.final) {
            impl_->queue_at(id, impl_->scheduler.now());
        }
    }
    if (impl_->scheduler.has_pending()) {
        const auto final_result = impl_->scheduler.run();
        result.time = final_result.time;
        result.delta = final_result.delta;
        result.callbacks_executed = final_result.callbacks_executed;
    }
    result.status = RunStatus::stopped;
    impl_->scheduler.request_stop();
    return result;
}

void Interpreter::deposit_signal(SignalId signal, PackedLogic4 value)
{
    impl_->commit(signal, std::move(value));
}

void Interpreter::deposit_scalar_signal(
    const SignalId signal,
    const SystemVerilogScalarValue value)
{
    const auto& stored = impl_->get_signal(signal);
    if (stored.systemverilog_scalar != value.kind) {
        throw std::invalid_argument { "SimIR scalar signal kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded) {
        throw std::invalid_argument { "invalid SimIR scalar signal value" };
    }
    deposit_signal(signal, encoded.value);
}

void Interpreter::force_signal(SignalId signal, PackedLogic4 value)
{
    const auto width = impl_->get_signal(signal).initial_value.width();
    if (width != value.width()) {
        throw std::invalid_argument("SimIR signal force width mismatch");
    }
    impl_->force_slice(signal, std::move(value), 0);
}

void Interpreter::force_scalar_signal(
    const SignalId signal,
    const SystemVerilogScalarValue value)
{
    const auto& stored = impl_->get_signal(signal);
    if (stored.systemverilog_scalar != value.kind) {
        throw std::invalid_argument { "SimIR scalar signal kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded) {
        throw std::invalid_argument { "invalid SimIR scalar signal value" };
    }
    force_signal(signal, encoded.value);
}

void Interpreter::release_signal(SignalId signal)
{
    const auto width = impl_->get_signal(signal).initial_value.width();
    impl_->release_slice(signal, 0, width);
}

void Interpreter::force_signal_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset)
{
    impl_->force_slice(signal, std::move(value), offset);
}

void Interpreter::release_signal_slice(
    const SignalId signal,
    const std::size_t offset,
    const std::size_t width)
{
    impl_->release_slice(signal, offset, width);
}

bool Interpreter::signal_is_forced(const SignalId signal) const
{
    (void)impl_->get_signal(signal);
    return impl_->forced_values[signal].has_value();
}

void Interpreter::schedule_signal_at(SignalId signal, PackedLogic4 value,
    SimulationTick time, StableOrder order)
{
    // Validate eagerly so a malformed drive does not fail much later.
    if (impl_->get_signal(signal).initial_value.width() != value.width()) {
        throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    impl_->scheduler.schedule_at(
        time, SchedulerPhase::update, order,
        [state = impl_.get(), signal, value = std::move(value)](
            Scheduler&) mutable {
            state->stage_update(signal, std::move(value));
        });
}

void Interpreter::schedule_scalar_signal_at(
    const SignalId signal,
    const SystemVerilogScalarValue value,
    const SimulationTick time,
    const StableOrder order)
{
    const auto& stored = impl_->get_signal(signal);
    if (stored.systemverilog_scalar != value.kind) {
        throw std::invalid_argument { "SimIR scalar signal kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded) {
        throw std::invalid_argument { "invalid SimIR scalar signal value" };
    }
    schedule_signal_at(signal, encoded.value, time, order);
}

void Interpreter::schedule_signal_after(SignalId signal, PackedLogic4 value,
    SimulationTick delay,
    StableOrder order)
{
    if (delay > std::numeric_limits<SimulationTick>::max() - impl_->scheduler.now()) {
        throw std::overflow_error("simulation time overflow scheduling signal");
    }
    schedule_signal_at(signal, std::move(value), impl_->scheduler.now() + delay,
        order);
}

void Interpreter::schedule_scalar_signal_after(
    const SignalId signal,
    const SystemVerilogScalarValue value,
    const SimulationTick delay,
    const StableOrder order)
{
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - impl_->scheduler.now()) {
        throw std::overflow_error { "simulation time overflow scheduling signal" };
    }
    schedule_scalar_signal_at(
        signal, value, impl_->scheduler.now() + delay, order);
}

const PackedLogic4& Interpreter::signal_value(SignalId signal) const
{
    return impl_->get_signal(signal).initial_value;
}

const PackedLogic4& Interpreter::stored_signal_value(
    const SignalId signal) const
{
    (void)impl_->get_signal(signal);
    return impl_->driven_values.at(signal);
}

SystemVerilogScalarValue Interpreter::scalar_signal_value(
    const SignalId signal) const
{
    const auto& stored = impl_->get_signal(signal);
    const auto decoded = decode_systemverilog_scalar_payload(
        stored.initial_value, stored.systemverilog_scalar);
    if (!decoded) {
        throw std::logic_error { "SimIR signal is not a valid scalar value" };
    }
    return decoded.value;
}

std::vector<SystemVerilogScalarSignalSnapshot>
Interpreter::scalar_signal_snapshots() const
{
    std::vector<SystemVerilogScalarSignalSnapshot> result;
    for (std::size_t index = 0; index < impl_->signals.size(); ++index) {
        const auto signal = static_cast<SignalId>(index);
        const auto& stored = impl_->signals[index];
        if (stored.systemverilog_scalar == SystemVerilogScalarKind::None) {
            continue;
        }
        result.push_back({ signal, stored.name, scalar_signal_value(signal) });
    }
    return result;
}

const std::string& Interpreter::string_object_value(
    const StringObjectId object) const
{
    return impl_->get_string_object(object).initial_value;
}

void Interpreter::deposit_string_object(
    const StringObjectId object,
    const std::string_view value)
{
    if (value.size() > maximum_string_bytes) {
        throw std::length_error {
            "SimIR string object exceeds byte limit"
        };
    }
    (void)systemverilog_string_length(value);
    impl_->get_string_object(object).initial_value = value;
}

const ContainerValue& Interpreter::container_object_value(
    const ContainerObjectId object) const
{
    return impl_->read_container_object_value(object);
}

void Interpreter::deposit_container_object(
    const ContainerObjectId object,
    ContainerValue value)
{
    impl_->write_container_object_value(object, value);
}

PackedLogic4 Interpreter::driver_value(
    const ProcessId process,
    const SignalId signal) const
{
    (void)impl_->get_process(process);
    return impl_->underlying_driver_value(process, signal);
}

ProcessId Interpreter::design_process(const ProcessId process) const
{
    return impl_->get_process(process).design_process;
}

const Process& Interpreter::process_program(const ProcessId process) const
{
    return impl_->get_process(process).program;
}

InstructionIndex Interpreter::process_instruction(
    const ProcessId process) const
{
    return impl_->get_process(process).pc;
}

void Interpreter::track_process_interpreter_operations(
    const ProcessId process)
{
    impl_->get_process(process).track_interpreter_operations = true;
}

std::uint64_t Interpreter::process_interpreter_operations(
    const ProcessId process) const
{
    return impl_->get_process(process).interpreter_operations;
}

ProcessId Interpreter::dynamic_process_root(const ProcessId process) const
{
    auto root = process;
    while (const auto parent = impl_->get_process(root).fork_parent) {
        if (!impl_->get_process(*parent).fork_parent) {
            return root;
        }
        root = *parent;
    }
    return root;
}

void Interpreter::kill_dynamic_processes(
    const std::span<const ProcessId> design_processes)
{
    impl_->kill_dynamic_processes(design_processes);
}

PackedLogic4 Interpreter::read_debug_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_locals.size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = state.program.debug_locals[local_index];
    if (state.executor) {
        return state.executor->read_register(
            local.register_id, local.width);
    }
    const auto& value = state.frame->registers.at(local.register_id);
    if (value.width() != local.width) {
        throw std::logic_error { "SimIR debug local has not been initialized" };
    }
    return value;
}

SystemVerilogScalarValue Interpreter::read_debug_scalar_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_locals.size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = state.program.debug_locals[local_index];
    if (local.systemverilog_scalar == SystemVerilogScalarKind::None) {
        throw std::logic_error { "SimIR debug local is not a scalar value" };
    }
    const auto packed = read_debug_local(process, local_index);
    const auto decoded = decode_systemverilog_scalar_payload(
        packed, local.systemverilog_scalar);
    if (!decoded) {
        throw std::logic_error { "SimIR scalar debug local is not initialized" };
    }
    return decoded.value;
}

void Interpreter::write_debug_scalar_local(
    const ProcessId process,
    const std::size_t local_index,
    const SystemVerilogScalarValue value)
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_locals.size()) {
        throw std::out_of_range { "invalid SimIR debug-local index" };
    }
    const auto& local = state.program.debug_locals[local_index];
    if (value.kind != local.systemverilog_scalar) {
        throw std::invalid_argument { "SimIR scalar debug-local kind mismatch" };
    }
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded || encoded.value.width() != local.width) {
        throw std::invalid_argument { "invalid SimIR scalar debug-local payload" };
    }
    impl_->write_process_register(state, local.register_id, encoded.value);
}

std::string Interpreter::read_debug_string_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_string_locals.size()) {
        throw std::out_of_range { "invalid SimIR string debug-local index" };
    }
    const auto& local = state.program.debug_string_locals[local_index];
    if (state.executor) {
        return state.executor->read_string_register(local.register_id);
    }
    return state.frame->string_registers.at(local.register_id);
}

ContainerValue Interpreter::read_debug_container_local(
    const ProcessId process,
    const std::size_t local_index) const
{
    auto& state = impl_->get_process(process);
    if (local_index >= state.program.debug_container_locals.size()) {
        throw std::out_of_range {
            "invalid SimIR container debug-local index"
        };
    }
    const auto& local = state.program.debug_container_locals[local_index];
    if (state.executor) {
        return state.executor->read_container_register(local.register_id);
    }
    return state.frame->container_registers.at(local.register_id);
}

bool Interpreter::stopped_by_design() const noexcept
{
    return impl_->stopped_by_design;
}

Scheduler& Interpreter::scheduler() noexcept { return impl_->scheduler; }
const Scheduler& Interpreter::scheduler() const noexcept
{
    return impl_->scheduler;
}

void Interpreter::set_signal_change_hook(SignalChangeHook hook)
{
    impl_->signal_change_hook = std::move(hook);
}

void Interpreter::set_native_signal_observation_required_hook(
    NativeSignalObservationRequiredHook hook)
{
    impl_->native_signal_observation_required_hook = std::move(hook);
}

void Interpreter::set_native_signal_observation_any_hook(
    NativeSignalObservationAnyHook hook)
{
    impl_->native_signal_observation_any_hook = std::move(hook);
}

void Interpreter::set_stored_signal_change_hook(StoredSignalChangeHook hook)
{
    impl_->stored_signal_change_hook = std::move(hook);
}

void Interpreter::set_driver_change_hook(DriverChangeHook hook)
{
    impl_->driver_change_hook = std::move(hook);
}

void Interpreter::set_event_trigger_hook(EventTriggerHook hook)
{
    impl_->event_trigger_hook = std::move(hook);
}

void Interpreter::set_container_object_change_hook(
    ContainerObjectChangeHook hook)
{
    impl_->container_object_change_hook = std::move(hook);
}

void Interpreter::set_scalar_signal_change_hook(
    ScalarSignalChangeHook hook)
{
    impl_->scalar_signal_change_hook = std::move(hook);
}

void Interpreter::set_execution_point_hook(ExecutionPointHook hook)
{
    impl_->execution_point_hook = std::move(hook);
}

void Interpreter::set_output_hook(OutputHook hook)
{
    impl_->output_hook = std::move(hook);
}

void Interpreter::set_report_hook(ReportHook hook)
{
    impl_->report_hook = std::move(hook);
}

void Interpreter::set_fork_spawn_filter(ForkSpawnFilter filter)
{
    impl_->fork_spawn_filter = std::move(filter);
}

} // namespace fsim::runtime::simir
