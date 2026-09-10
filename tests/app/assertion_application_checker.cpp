// SPDX-License-Identifier: Apache-2.0

#include "assertion_application_support.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fsim::test::assertion_application {

void test_checker_instances(
    const std::filesystem::path& directory)
{
    const auto checker_source = directory / "value_checker.sv";
    {
        std::ofstream output(checker_source);
        output << R"(
checker value_checker(
    input logic checker_clock,
    input logic observed,
    input logic expected = observed
  );
    property is_expected;
      @(posedge checker_clock) observed == expected;
    endproperty
    okay: assert property (is_expected)
      $display("checker pass");
      else $error("checker failure");
  endchecker : value_checker
)";
        assert(output.good());
    }
    const auto design_source = directory / "checker_instances.sv";
    {
        std::ofstream output(design_source);
        output << R"(
module checker_instances;
  logic clock;
  logic named_value;
  logic ordered_value;
  logic checker_clock;
  logic observed;
  value_checker named_instance(
    .checker_clock(clock),
    .observed(named_value)
  );
  value_checker ordered_instance(clock, ordered_value, 1'b0);
  value_checker wildcard_instance(.*, .expected());
  initial begin
    clock = 1'b0;
    named_value = 1'b1;
    ordered_value = 1'b0;
    checker_clock = 1'b0;
    observed = 1'b1;
    #1 begin
      clock = 1'b1;
      checker_clock = 1'b1;
    end
    #1 $finish;
  end
endmodule
)";
        assert(output.good());
    }

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        auto config = config_for(directory, checker_source, optimization);
        config.source_sets.front().files.push_back(design_source);
        config.source_sets.front().compilation_unit = "source-set";
        config.project.name = optimization == fsim::project::Optimization::o0
            ? "checker-instances-o0"
            : "checker-instances-o2";
        config.project.top = "sv:work.checker_instances";
        config.source_sets.front().standard = "2023";
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "checker-cache-o0"
                    : "checker-cache-o2");
        const auto interpreted = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::interpreter);
        const auto compiled = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::compiled);
        for (const auto* capture : { &interpreted, &compiled, &warm }) {
            assert(capture->assertion_processes == 3);
            assert(capture->assertion_processes_observed);
            assert(capture->sampled_signal_reads == 5);
            assert(capture->current_signal_reads == 0);
            assert(capture->reactive_waits == 6);
            assert((capture->outputs == std::vector<std::string> { "checker pass", "checker pass", "checker pass" }));
            assert(capture->reports.empty());
            assert(capture->coverage.size() == 3);
            assert(capture->events.size() == 3);
            assert(capture->callback_events == capture->events);
            assert(capture->coverage[0].name == "named_instance.okay");
            assert(capture->coverage[1].name == "ordered_instance.okay");
            assert(capture->coverage[2].name == "wildcard_instance.okay");
            assert(std::ranges::all_of(
                capture->coverage,
                [](const auto& coverage) {
                    return !coverage.instance_identity.empty()
                        && coverage.source_span != 0U;
                }));
            assert(capture->coverage[0].instance_identity
                == capture->coverage[1].instance_identity);
            assert(capture->coverage[1].instance_identity
                == capture->coverage[2].instance_identity);
            assert(std::ranges::all_of(
                capture->coverage,
                [](const auto& coverage) {
                    return coverage.attempts == 1
                        && coverage.passes == 1
                        && coverage.failures == 0;
                }));
            assert(std::ranges::all_of(
                capture->events,
                [](const auto& event) {
                    return event.outcome
                        == fsim::app::ConcurrentAssertionOutcome::pass
                        && !event.instance_identity.empty()
                        && event.source_span != 0U
                        && event.time == 1 && event.delta == 1;
                }));
        }
        assert(interpreted.events == compiled.events);
        assert(interpreted.events == warm.events);
#if defined(FSIM_HAS_LLVM)
        assert(compiled.native_cache.misses != 0);
        assert(warm.native_cache.hits != 0);
        assert(warm.native_cache.misses == 0);
#endif
    }
}

} // namespace fsim::test::assertion_application
