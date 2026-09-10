// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fsim::test::assertion_application {

struct ReportCapture {
    std::string message;
    runtime::simir::AssertionSeverity severity { };
    runtime::simir::SourceLocation source;
    runtime::SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const ReportCapture&, const ReportCapture&) = default;
};

struct ConcurrentCapture {
    std::vector<std::string> outputs;
    std::vector<std::string> timeline;
    std::vector<ReportCapture> reports;
    std::vector<std::string> process_names;
    std::vector<app::ConcurrentAssertionCoverage> coverage;
    std::vector<app::ConcurrentAssertionEvent> events;
    std::vector<app::ConcurrentAssertionEvent> callback_events;
    std::vector<runtime::SchedulerPhase> phases;
    app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes { };
    std::size_t assertion_processes { };
    std::size_t sampled_signal_reads { };
    std::size_t current_signal_reads { };
    std::size_t reactive_waits { };
    bool assertion_processes_observed { true };
};

project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    project::Optimization optimization);

ConcurrentCapture run_concurrent_assertions(
    const project::Config& config,
    app::SimulationEngine engine);

void test_checker_instances(const std::filesystem::path& directory);
void test_ranged_eventually_properties(const std::filesystem::path& directory);
void test_ranged_always_properties(const std::filesystem::path& directory);
void test_scalar_property_aborts(const std::filesystem::path& directory);
void test_sequence_delay_ranges(const std::filesystem::path& directory);
void test_scalar_sequence_combinators(const std::filesystem::path& directory);
void test_vacuous_action_controls(const std::filesystem::path& directory);
void test_concurrent_action_blocks(const std::filesystem::path& directory);
void test_assertkill_cancels_attempts(const std::filesystem::path& directory);
void test_assertoff_preserves_active_attempts(
    const std::filesystem::path& directory);
void test_covergroup_execution(const std::filesystem::path& directory);
void test_wide_transition_cross_execution(
    const std::filesystem::path& directory);
void test_wide_concurrent_predicate(const std::filesystem::path& directory);
void test_scalar_property_operators(const std::filesystem::path& directory);
void test_property_formal_actuals(const std::filesystem::path& directory);
void test_property_local_variables(const std::filesystem::path& directory);
void test_sequence_formal_actuals(const std::filesystem::path& directory);
void test_disable_iff_scalar_property(const std::filesystem::path& directory);
void test_disable_iff_cancels_attempts(const std::filesystem::path& directory);
void test_cover_sequence_revisions(const std::filesystem::path& directory);
void test_concurrent_assertion_execution_regions(
    const std::filesystem::path& directory);

} // namespace fsim::test::assertion_application
