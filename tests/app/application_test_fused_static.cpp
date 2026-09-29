// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "fsim/support/environment.hpp"

#include <cassert>
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
    assert(::_putenv_s(key, value == nullptr ? "" : value) == 0);
#else
    assert((value == nullptr ? ::unsetenv(key) : ::setenv(key, value, 1)) == 0);
#endif
}

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
    const auto found = diagnostics.rfind(label + "=");
    assert(found != std::string::npos);
    return std::stoull(diagnostics.substr(found + label.size() + 1U));
}

} // namespace
#endif

void ApplicationTestFixture::test_fused_static_simulation()
{
#if defined(FSIM_HAS_LLVM)
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
    assign interior[63:0] = a[63:0] ^ b[63:0];
    assign interior[64] = a[64] & b[64];
    assign y = ^interior;
endmodule
module fused_top;
    reg [64:0] a, b, c, d;
    wire q, r;
    fused_leaf left_instance(a, b, q);
    fused_leaf right_instance(c, d, r);
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
    if (metric(compiled.diagnostics, "compiled_kernels") != 1U
        || metric(compiled.diagnostics, "bound_cohorts") != 2U
        || metric(compiled.diagnostics, "invocations") == 0U) {
        std::cerr << compiled.diagnostics;
    }
    assert(metric(compiled.diagnostics, "compiled_kernels") == 1U);
    assert(metric(compiled.diagnostics, "bound_cohorts") == 2U);
    assert(metric(compiled.diagnostics, "invocations") > 0U);
    test_fused_static_vhdl_simulation();
    test_fused_static_vhdl_simulation(true);
    test_fused_static_vhdl_simulation(true, true);
    test_fused_static_vhdl_simulation(true, false, true);
#endif
}

void ApplicationTestFixture::test_fused_static_vhdl_simulation(
    const bool masked, const bool private_bridge,
    const bool terminal_chain)
{
#if defined(FSIM_HAS_LLVM)
    struct Capture {
        std::vector<std::vector<std::string>> values;
        std::vector<std::uint64_t> deltas;
        std::vector<std::tuple<runtime::SimulationTick, std::uint64_t, std::string>> output;
        std::string diagnostics;
    };
    bool all_routes_matched = true;
    for (const auto [nine_state, width] : { std::pair { false, 9U },
             std::pair { false, 65U }, std::pair { false, 129U },
             std::pair { true, 9U }, std::pair { true, 65U },
             std::pair { true, 129U } }) {
        const std::string scalar = nine_state ? "std_logic" : "bit";
        const std::string vector = scalar + "_vector";
        const std::string prefix = std::string(terminal_chain ? "terminal_"
                : private_bridge ? "bridge_"
                : masked ? "masked_" : "fused_")
            + (nine_state ? "logic" : "bit")
            + std::to_string(width);
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
            if (private_bridge || terminal_chain) {
                file << "signal c0, c1: " << vector << '(' << width - 1U << " downto 0);\n";
            }
            file << "begin\n"
                 << (masked && !private_bridge ? "process(a)" : "process(a, b)")
                 << " begin copied <= a; end process;\n"
                 << "mixed <= a xor b;\n";
            if (private_bridge) {
                file << "process(copied, mixed) begin c0 <= copied; end process;\n"
                     << "process(copied, mixed, a) begin c1 <= mixed; end process;\n"
                     << "y <= (xor c0) xor (xor c1);\n"
                     << "process(y) begin report \"boundary\"; end process;\nend;\n";
            } else if (terminal_chain) {
                file << "process(copied, mixed) begin c0 <= copied; end process;\n"
                     << "process(a, copied, mixed) begin c1 <= mixed; end process;\n"
                     << "y <= (xor c0) xor (xor c1);\n"
                     << "process(y) begin report \"boundary\"; end process;\nend;\n";
            } else {
                file << "y <= (xor copied) xor (xor mixed);\nend;\n";
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
        sources.standard = "2008";
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
                for (runtime::SimulationTick time = 0U; time <= states.size(); ++time) {
                    const auto run = simulation.run(time);
                    assert(run.status == runtime::RunStatus::completed
                        || run.status == runtime::RunStatus::time_limit);
                    captured.deltas.push_back(run.delta);
                    auto& values = captured.values.emplace_back();
                    for (const auto& signal : simulation.design().signals()) {
                        // This preserves all nine states, including weak values.
                        values.push_back(simulation.read_signal(signal.id).to_msb_string());
                    }
                }
                captured.diagnostics = profile.str();
                return captured;
            });
        assert(reference.values == compiled.values);
        assert(reference.deltas == compiled.deltas);
        assert(reference.output == compiled.output);
        assert((!private_bridge && !terminal_chain) || !compiled.output.empty());
        const auto kernels = metric(compiled.diagnostics, "compiled_kernels");
        const auto cohorts = metric(compiled.diagnostics, masked ? "bound_regions" : "bound_cohorts");
        const auto invocations = metric(compiled.diagnostics, masked ? "masked_calls" : "invocations");
        const bool private_route = !private_bridge
            || (metric(compiled.diagnostics, "private_candidates") > 0U
                && metric(compiled.diagnostics, "private_local_commits") > 0U
                && metric(compiled.diagnostics, "private_fanout_entries_avoided") > 0U
                && metric(compiled.diagnostics, "private_masked_notifications") > 0U);
        const bool shared_frontier = !masked
            || (metric(compiled.diagnostics, "global_frontier_callbacks") > 0U
                && metric(compiled.diagnostics, "global_frontier_callbacks") < invocations);
        const bool terminal_route = !terminal_chain
            || (metric(compiled.diagnostics, "terminal_regions_bound") > 0U
                && metric(compiled.diagnostics, "terminal_members_bound") >= 4U
                && metric(compiled.diagnostics, "terminal_activations") > 0U
                && metric(compiled.diagnostics, "terminal_joint_activations") > 0U);
        const bool route_matched = kernels == 1U && cohorts == 2U
            && invocations > 0U && private_route && shared_frontier
            && terminal_route;
        if (!route_matched) {
            std::cerr << prefix << " did not enter generic fusion:\n"
                      << compiled.diagnostics;
            all_routes_matched = false;
        }
    }
    assert(all_routes_matched);
#else
    static_cast<void>(masked);
    static_cast<void>(private_bridge);
#endif
}

} // namespace fsim::test
