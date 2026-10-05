// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "fsim/support/environment.hpp"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {
#if defined(FSIM_HAS_LLVM)
namespace {

void set_profile_environment(const char* key, const char* value)
{
#if defined(_WIN32)
    const auto result = ::_putenv_s(key, value == nullptr ? "" : value);
#else
    const auto result = value == nullptr
        ? ::unsetenv(key)
        : ::setenv(key, value, 1);
#endif
    if (result != 0) {
        std::abort();
    }
}

class EnvironmentScope {
public:
    EnvironmentScope(const char* key, const char* value)
        : key_(key)
        , previous_(support::environment_variable(key))
    {
        set_profile_environment(key_, value);
    }

    ~EnvironmentScope()
    {
        set_profile_environment(key_, previous_ ? previous_->c_str() : nullptr);
    }

private:
    const char* key_;
    std::optional<std::string> previous_;
};

class DiagnosticScope {
public:
    explicit DiagnosticScope(std::ostringstream& stream,
        const char* key = "FSIM_PROFILE_FUSED_STATIC")
        : key_(key)
        , previous_(support::environment_variable(key))
        , stream_(std::cerr.rdbuf(stream.rdbuf()))
    {
        set_profile_environment(key_, "1");
    }

    ~DiagnosticScope()
    {
        std::cerr.rdbuf(stream_);
        set_profile_environment(key_, previous_ ? previous_->c_str() : nullptr);
    }

private:
    const char* key_;
    std::optional<std::string> previous_;
    std::streambuf* stream_;
};

struct FusedSimulationResult {
    std::vector<std::tuple<runtime::SimulationTick, std::uint64_t, std::string>> output;
    std::string diagnostics;
    runtime::RunResult result;
};

std::uint64_t metric(const std::string& diagnostics, const std::string& label)
{
    const auto key = label + "=";
    auto found = diagnostics.rfind(key);
    while (found != std::string::npos && found != 0U
        && !std::isspace(static_cast<unsigned char>(
            diagnostics[found - 1U]))) {
        found = diagnostics.rfind(key, found - 1U);
    }
    assert(found != std::string::npos);
    return std::stoull(diagnostics.substr(found + label.size() + 1U));
}

} // namespace
#endif

void ApplicationTestFixture::test_fused_static_simulation()
{
#if defined(FSIM_HAS_LLVM)
    // SystemVerilog Active updates stay on the checked scheduler path. The
    // static fused route is restricted to generic-domain operations.
    EnvironmentScope checked_disjoint_ownership(
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0");
    auto config = base_config();
    config.project.name = "generic-fused-static-test";
    config.project.top = "sv:work.fused_top";
    config.build.optimization = project::Optimization::o2;
    config.build.cache_path = directory / "generic-fused-static-cache";
    config.source_sets.clear();
    const auto source_path = directory / "generic-fused-static.sv";
    {
        std::ofstream file(source_path);
        assert(file);
        file << R"(
module fused_leaf(input wire [64:0] a, b, output wire y);
    wire [64:0] interior;
    // Both producers read the full input ranges so they share a sensitivity
    // cohort even after range-aware dependency lowering. This fixture must
    // reach the scheduling-domain guard before legacy native binding.
    assign interior[63:0] = a ^ b;
    assign interior[64] = ^((a & b) & 65'h1_0000000000000000);
    assign y = ^interior;
endmodule
module masked_leaf(input wire [64:0] a, b, output wire y);
    wire [64:0] interior;
    // Distinct ranges exercise the legacy masked admission path separately.
    assign interior[63:0] = a[63:0] ^ b[63:0];
    assign interior[64] = a[64] & b[64];
    assign y = ^interior;
endmodule
module fused_top;
    reg [64:0] a, b, c, d;
    wire q, r;
    fused_leaf left_instance(a, b, q);
    masked_leaf right_instance(c, d, r);
    initial begin
        a = '0; b = '0; c = '0; d = '0;
        #1 $display("%b %b", q, r);
        a[63] = 1'b1; c[64] = 1'b1; d[64] = 1'b1;
        #1 $display("%b %b", q, r);
        a = 'x; c = 'z;
        #1 $display("%b %b", q, r);
        a = 'z; b = '1; c = '1; d = '1;
        #1 $display("%b %b", q, r);
        $finish;
    end
endmodule
)";
        assert(file);
    }
    project::SourceSet sources;
    sources.language = project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source_path);
    config.source_sets.push_back(std::move(sources));
    diagnostic::Engine diagnostics;
    auto built = app::build_project(config, diagnostics);
    assert(built);
    const auto [reference, compiled] = run_interpreter_compiled_pair(
        std::move(*built), [&](app::BuiltProject project, app::SimulationEngine engine) {
            FusedSimulationResult captured;
            std::ostringstream profile;
            EnvironmentScope masked_profile_scope(
                "FSIM_PROFILE_FUSED_MASKED", "1");
            DiagnosticScope profile_scope(profile);
            app::Simulation simulation(
                std::move(project), config.run.max_deltas, engine,
                app::SystemVerilogVpiRuntimeUpdates::disabled);
            simulation.set_output_hook(
                [&](runtime::simir::ProcessId, std::string_view text, bool,
                    runtime::SimulationTick time,
                    std::uint64_t delta) {
                    captured.output.emplace_back(time, delta, std::string(text));
                });
            captured.result = simulation.run();
            captured.diagnostics = profile.str();
            return captured;
        });
    assert(reference.output == compiled.output);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    std::vector<std::string> rows;
    std::optional<std::pair<runtime::SimulationTick, std::uint64_t>> current_row;
    for (const auto& [time, delta, text] : reference.output) {
        const auto key = std::pair { time, delta };
        if (!current_row || *current_row != key) {
            rows.emplace_back();
            current_row = key;
        }
        rows.back() += text;
    }
    assert((rows == std::vector<std::string> { "0 0", "1 1", "x x", "x 1" }));
    const auto static_profile_start
        = compiled.diagnostics.find("fsim fused-static graph: candidates=");
    assert(static_profile_start != std::string::npos);
    const auto static_profile_end
        = compiled.diagnostics.find('\n', static_profile_start);
    const auto static_profile = compiled.diagnostics.substr(
        static_profile_start,
        static_profile_end == std::string::npos
            ? std::string::npos
            : static_profile_end - static_profile_start);
    assert(metric(static_profile, "compiled_kernels") == 0U);
    assert(metric(static_profile, "bound_cohorts") == 0U);
    assert(metric(static_profile, "candidates") == 0U);
    const auto masked_profile_start
        = compiled.diagnostics.find("fsim masked route:");
    assert(masked_profile_start != std::string::npos);
    const auto masked_profile_end
        = compiled.diagnostics.find('\n', masked_profile_start);
    const auto masked_profile = compiled.diagnostics.substr(
        masked_profile_start,
        masked_profile_end == std::string::npos
            ? std::string::npos
            : masked_profile_end - masked_profile_start);
    assert(metric(masked_profile, "masked_calls") == 0U);
    assert(metric(masked_profile, "global_frontier_callbacks") == 0U);
    assert(metric(masked_profile, "global_frontier_queue_entries") == 0U);
    assert(metric(masked_profile, "terminal_candidates") == 0U);
    assert(metric(masked_profile, "private_candidates") == 0U);
    test_fused_static_vhdl_simulation();
    test_fused_static_vhdl_simulation(true);
    test_fused_static_vhdl_simulation(true, true);
    test_fused_static_vhdl_simulation(true, false, true);
    test_fused_static_vhdl_simulation(false, false, false, true);
#endif
}

void ApplicationTestFixture::test_fused_static_vhdl_simulation(
    const bool masked, const bool interleaved_chain,
    const bool terminal_chain, const bool masked_all_active)
{
#if defined(FSIM_HAS_LLVM)
    std::optional<EnvironmentScope> checked_fallback_route;
    if (masked) {
        // Keep this fixture on the checked compiled-process backend. The
        // default projected route has a separate VHDL ticket witness.
        checked_fallback_route.emplace("FSIM_ENABLE_SV_REGION_KERNEL", "0");
    }
    struct Capture {
        std::vector<std::vector<std::string>> values;
        std::vector<std::uint64_t> deltas;
        std::vector<std::tuple<runtime::SimulationTick, std::uint64_t, std::string>> output;
        std::size_t compiled_processes { };
        std::string diagnostics;
    };
    bool all_routes_matched = true;
    for (const auto [nine_state, width] : { std::pair { false, 9U },
             std::pair { false, 65U }, std::pair { false, 129U },
             std::pair { true, 9U }, std::pair { true, 65U },
             std::pair { true, 129U } }) {
        if (masked_all_active && (nine_state || width != 65U)) {
            continue;
        }
        // An observing report callback demotes native private state. Keep a
        // report-free native admission case and a separate callback parity case.
        for (const bool observe_output : { false, true }) {
            if (observe_output && !interleaved_chain && !terminal_chain
                && !masked_all_active) {
                continue;
            }
            const std::string scalar = nine_state ? "std_logic" : "bit";
            const std::string vector = scalar + "_vector";
            const std::string prefix = std::string(masked_all_active
                    ? "all_active_"
                    : terminal_chain ? "terminal_"
                    : interleaved_chain ? "chain_"
                    : masked ? "masked_" : "fused_")
                + (nine_state ? "logic" : "bit")
                + std::to_string(width)
                + (observe_output ? "_observed" : "_native");
            const std::string states = nine_state ? "01UXZWLH-" : "010101010";
            auto config = base_config();
            config.project.name = prefix;
            config.project.top = "vhdl:work." + prefix + "_top(tb)";
            config.project.time_resolution = "1ns";
            config.build.optimization = project::Optimization::o2;
            config.build.cache_path = directory / (prefix + "-cache");
            config.source_sets.clear();
            const auto path = directory / (prefix + ".vhd");
            {
                std::ofstream file(path);
                assert(file);
                const auto context = [&] {
                    if (nine_state) {
                        file << "library ieee; use ieee.std_logic_1164.all;\n";
                    }
                };
                context();
                file << "entity " << prefix << "_leaf is port(a, b: in " << vector
                     << '(' << width - 1U << " downto 0); y: out " << scalar << "); end;\n";
                context();
                file << "architecture rtl of " << prefix << "_leaf is\n"
                     << "signal copied, mixed: " << vector << '(' << width - 1U << " downto 0);\n";
                if (interleaved_chain || terminal_chain) {
                    file << "signal c0, c1: " << vector << '(' << width - 1U << " downto 0);\n";
                }
                file << "begin\n";
                if (masked_all_active) {
                    // The conditional expression branches before one
                    // projected write, without exposing a local frame value.
                    for (const bool mix_inputs : { false, true }) {
                        file << (mix_inputs ? "mixed" : "copied")
                             << " <= (" << (mix_inputs ? "a xor b" : "a")
                             << " when a = b else b);\n";
                    }
                } else {
                    file << (masked && !interleaved_chain
                            ? "process(a)" : "process(a, b)")
                         << " begin copied <= a; end process;\n"
                         << "mixed <= a xor b;\n";
                }
                if (interleaved_chain) {
                    file << "process(copied, mixed) begin c0 <= copied; end process;\n"
                         << "process(copied, mixed, a) begin c1 <= mixed; end process;\n"
                         << "y <= (xor c0) xor (xor c1);\n"
                         << "process(y) begin "
                         << (observe_output ? "report \"boundary\";" : "null;")
                         << " end process;\nend;\n";
                } else if (terminal_chain) {
                    file << "process(copied, mixed) begin c0 <= copied; end process;\n"
                         << "process(a, copied, mixed) begin c1 <= mixed; end process;\n"
                         << "y <= (xor c0) xor (xor c1);\n"
                         << "process(y) begin "
                         << (observe_output ? "report \"boundary\";" : "null;")
                         << " end process;\nend;\n";
                } else {
                    file << "y <= (xor copied) xor (xor mixed);\n";
                    if (masked_all_active) {
                        file << "process(y) begin "
                             << (observe_output
                                    ? "report \"boundary\";"
                                    : "null;")
                             << " end process;\n";
                    }
                    file << "end;\n";
                }
                context();
                file << "entity " << prefix << "_top is end;\n";
                context();
                file << "architecture tb of " << prefix << "_top is\n"
                     << "signal a, b, c, d: " << vector
                     << '(' << width - 1U << " downto 0) := (others => '0');\n"
                     << "signal q, r: " << scalar << ";\nbegin\n"
                     << "left_instance: entity work." << prefix << "_leaf port map(a, b, q);\n"
                     << "right_instance: entity work." << prefix << "_leaf port map(c, d, r);\n"
                     << "stimulus: process begin\n";
                for (std::size_t row = 0U; row < states.size(); ++row) {
                    file << "wait for 1 ns;\n"
                         << "a <= (others => '" << states[row] << "');\n"
                         << "b <= (others => '" << states[(row + 1U) % states.size()] << "');\n"
                         << "c <= (others => '" << states[(row + 2U) % states.size()] << "');\n"
                         << "d <= (others => '" << states[(row + 3U) % states.size()] << "');\n";
                }
                file << "wait; end process;\nend;\n";
                assert(file);
            }
            project::SourceSet sources;
            sources.language = project::Language::vhdl;
            sources.standard = masked_all_active ? "2019" : "2008";
            sources.library = "work";
            sources.files.push_back(path);
            config.source_sets.push_back(std::move(sources));
            diagnostic::Engine diagnostics;
            auto built = app::build_project(config, diagnostics);
            if (!built) {
                for (const auto& item : diagnostics.diagnostics()) {
                    std::cerr << prefix << ": " << item.code << ": " << item.message << '\n';
                }
            }
            assert(built);
            const auto [reference, compiled] = run_interpreter_compiled_pair(
                std::move(*built), [&](app::BuiltProject project, app::SimulationEngine engine) {
                    Capture captured;
                    std::ostringstream profile;
                    DiagnosticScope profile_scope(profile,
                        masked ? "FSIM_PROFILE_FUSED_MASKED" : "FSIM_PROFILE_FUSED_STATIC");
                    app::Simulation simulation(std::move(project), config.run.max_deltas,
                        engine, app::SystemVerilogVpiRuntimeUpdates::disabled);
                    if (observe_output) {
                        simulation.set_report_hook(
                            [&](runtime::simir::ProcessId, std::string_view text,
                                runtime::simir::AssertionSeverity,
                                const runtime::simir::SourceLocation&,
                                runtime::SimulationTick time, std::uint64_t delta) {
                                std::string snapshot(text);
                                for (const auto& signal : simulation.design().signals()) {
                                    snapshot += ":" + simulation.read_signal(signal.id).to_msb_string();
                                }
                                captured.output.emplace_back(time, delta, std::move(snapshot));
                            });
                        for (runtime::SimulationTick time = 0U;
                             time <= states.size(); ++time) {
                            const auto run = simulation.run(time);
                            assert(run.status == runtime::RunStatus::completed
                                || run.status == runtime::RunStatus::time_limit);
                            captured.deltas.push_back(run.delta);
                            auto& values = captured.values.emplace_back();
                            for (const auto& signal : simulation.design().signals()) {
                                values.push_back(simulation.read_signal(signal.id).to_msb_string());
                            }
                        }
                    } else {
                        const auto run = simulation.run();
                        assert(run.status == runtime::RunStatus::completed);
                        captured.deltas.push_back(run.delta);
                        auto& values = captured.values.emplace_back();
                        for (const auto& signal : simulation.design().signals()) {
                            values.push_back(simulation.read_signal(signal.id).to_msb_string());
                        }
                    }
                    captured.compiled_processes = simulation.compiled_process_count();
                    captured.diagnostics = profile.str();
                    return captured;
                });
            assert(reference.values == compiled.values);
            assert(reference.deltas == compiled.deltas);
            assert(reference.output == compiled.output);
            if (observe_output) {
                assert(!compiled.output.empty());
                // All-active kernels are bound to static cohorts, so their
                // observation invalidations use the static route's counter.
                if (masked) {
                    assert(compiled.compiled_processes > 0U);
                    assert(metric(compiled.diagnostics, "masked_calls") == 0U);
                    assert(metric(compiled.diagnostics,
                        "global_frontier_callbacks") == 0U);
                }
                if (!masked && masked_all_active) {
                    assert(metric(compiled.diagnostics,
                        "static_observation_invalidations") > 0U);
                }
                if (masked_all_active) {
                    // Observers keep this process on checked execution even
                    // when the unobserved run uses the native cohort kernel.
                    assert(metric(compiled.diagnostics,
                        "masked_all_active_invocations") == 0U);
                }
                continue;
            }
            assert(compiled.output.empty());
            if (masked) {
                assert(compiled.compiled_processes > 0U);
                assert(metric(compiled.diagnostics, "masked_calls") == 0U);
                assert(metric(compiled.diagnostics,
                    "global_frontier_callbacks") == 0U);
                continue;
            }
            auto static_profile = std::string { };
            if (!masked) {
                const auto profile_start = compiled.diagnostics.find(
                    "fsim fused-static graph: candidates=");
                assert(profile_start != std::string::npos);
                const auto profile_end
                    = compiled.diagnostics.find('\n', profile_start);
                static_profile = compiled.diagnostics.substr(profile_start,
                    profile_end == std::string::npos
                        ? std::string::npos : profile_end - profile_start);
            }
            const auto kernels = metric(compiled.diagnostics, "compiled_kernels");
            const auto cohorts = metric(compiled.diagnostics, "bound_cohorts");
            const auto invocations = metric(compiled.diagnostics, "invocations");
            const bool retired_route = !interleaved_chain
                || (metric(compiled.diagnostics, "private_candidates") == 0U
                    && metric(compiled.diagnostics, "private_local_commits") == 0U
                    && metric(compiled.diagnostics,
                        "private_owned_direct_commits") == 0U
                    && metric(compiled.diagnostics,
                        "private_fanout_entries_avoided") == 0U
                    && metric(compiled.diagnostics,
                        "private_masked_notifications") == 0U);
            const bool all_active_route = !masked_all_active
                || (metric(static_profile,
                        "masked_all_active_bound_cohorts") == 2U
                    && metric(compiled.diagnostics,
                        "masked_all_active_invocations") > 0U);
            const bool checks_static_replacement = !masked
                && !interleaved_chain && !terminal_chain
                && !masked_all_active && !nine_state && width == 9U;
            const bool migrated_static_route = !checks_static_replacement
                || (metric(static_profile,
                        "masked_all_active_from_static") > 0U
                    && metric(compiled.diagnostics,
                        "masked_all_active_invocations") > 0U);
            const bool route_matched = kernels == 1U && cohorts == 2U
                && invocations > 0U && retired_route && all_active_route
                && migrated_static_route;
            if (!route_matched) {
                std::cerr << prefix << " did not enter generic fusion:\n"
                          << compiled.diagnostics;
                all_routes_matched = false;
            }
        }
    }
    assert(all_routes_matched);
#else
    static_cast<void>(masked);
    static_cast<void>(interleaved_chain);
    static_cast<void>(masked_all_active);
#endif
}

} // namespace fsim::test
