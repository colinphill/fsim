// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/support/path.hpp"
#include "assertion_application_support.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fsim::test::assertion_application {

void test_wide_concurrent_predicate(const std::filesystem::path& directory)
{
    const auto source = directory / "wide_concurrent_predicate.sv";
    {
        std::ofstream output(source);
        output << R"(
module wide_concurrent_predicate;
  logic clock;
  logic [136:0] observed;
  property exact_wide_value;
    @(posedge clock)
      always observed === 137'h1_0000000000000000_0000000000000000_01;
  endproperty
  exact_check: assert property (exact_wide_value)
    $display("wide pass");
    else $display("wide fail");
  initial begin
    clock = 1'b0;
    observed = 137'h1_0000000000000000_0000000000000000_01;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    observed = 137'h0_0000000000000000_0000000000000000_01;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "wide-concurrent-predicate";
    config.project.top = "sv:work.wide_concurrent_predicate";
    config.build.cache_path = directory / "wide-concurrent-predicate-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs { "wide pass", "wide fail" };
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 1);
    assert(interpreted.coverage.front().attempts == 2);
    assert(interpreted.coverage.front().passes == 1);
    assert(interpreted.coverage.front().failures == 1);
}

void test_scalar_property_operators(const std::filesystem::path& directory)
{
    const auto source = directory / "scalar_property_operators.sv";
    {
        std::ofstream output(source);
        output << R"(
module scalar_property_operators;
  logic clock;
  logic request;
  logic enabled;
  property combined;
    @(posedge clock) always (request and enabled);
  endproperty
  property inverted;
    @(posedge clock) not request;
  endproperty
  combined_check: assert property (combined)
    $display("combined pass");
    else $display("combined fail");
  inverted_check: assert property (inverted)
    $display("inverted pass");
    else $display("inverted fail");
  initial begin
    clock = 1'b0;
    request = 1'b1;
    enabled = 1'b1;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    request = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "scalar-property-operators";
    config.project.top = "sv:work.scalar_property_operators";
    config.build.cache_path = directory / "scalar-property-operators-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs {
        "combined pass",
        "inverted fail",
        "combined fail",
        "inverted pass"
    };
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 2);
    for (const auto& coverage : interpreted.coverage) {
        assert(coverage.attempts == 2);
        assert(coverage.passes == 1);
        assert(coverage.failures == 1);
    }
}

void test_property_formal_actuals(const std::filesystem::path& directory)
{
    const auto source = directory / "property_formal_actuals.sv";
    {
        std::ofstream output(source);
        output << R"(
module property_formal_actuals;
  logic clock;
  logic request;
  logic enabled;
  property selected(logic condition = enabled);
    @(posedge clock) condition;
  endproperty
  positional: assert property (selected(request))
    $display("positional pass");
    else $display("positional fail");
  named: assert property (selected(.condition(enabled)))
    $display("named pass");
    else $display("named fail");
  defaulted: assert property (selected())
    $display("default pass");
    else $display("default fail");
  initial begin
    clock = 1'b0;
    request = 1'b1;
    enabled = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    request = 1'b0;
    enabled = 1'b1;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "property-formal-actuals";
    config.project.top = "sv:work.property_formal_actuals";
    config.build.cache_path = directory / "property-formal-actuals-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs {
        "positional pass", "named fail", "default fail",
        "positional fail", "named pass", "default pass"
    };
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 3);
    assert(std::ranges::all_of(
        interpreted.coverage, [](const auto& coverage) {
            return coverage.attempts == 2 && coverage.passes == 1
                && coverage.failures == 1;
        }));
}

void test_property_local_variables(const std::filesystem::path& directory)
{
    const auto source = directory / "property_local_variables.sv";
    {
        std::ofstream output(source);
        output << R"(
module property_local_variables;
  logic clock;
  logic [136:0] data;
  int counter;
  task automatic record_match(input int value);
    if (value == 4)
      $display("match item call");
    else
      $display("match item bad");
  endtask
  property captures_current_value;
    logic [136:0] saved = data;
    @(posedge clock) saved == data;
  endproperty
  property computes_current_value;
    int next = counter + 1;
    @(posedge clock) next > counter;
  endproperty
  sequence captures_sequence_value;
    logic [136:0] sequence_saved = data;
    sequence_saved == data;
  endsequence
  property uses_sequence_local;
    @(posedge clock) captures_sequence_value;
  endproperty
  sequence captures_first_match_value;
    logic [136:0] matched_value;
    int match_count = 0;
    first_match(1'b1, matched_value = data, match_count = 1,
                match_count += 2, match_count++, record_match(match_count));
  endsequence
  property uses_first_match_local;
    @(posedge clock) captures_first_match_value;
  endproperty
  captured: assert property (captures_current_value)
    $display("captured pass");
    else $display("captured fail");
  computed: assert property (computes_current_value)
    $display("computed pass");
    else $display("computed fail");
  sequence_captured: assert property (uses_sequence_local)
    $display("sequence pass");
    else $display("sequence fail");
  first_match_captured: assert property (uses_first_match_local)
    $display("first_match pass");
    else $display("first_match fail");
  initial begin
    clock = 1'b0;
    data = 137'h1_0000000000000000_0000000000000000_01;
    counter = 0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    data = 137'h1_0000000000000000_0000000000000000_02;
    counter = 7;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "property-local-variables";
    config.project.top = "sv:work.property_local_variables";
    config.build.cache_path = directory / "property-local-variables-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs {
        "captured pass", "computed pass", "sequence pass", "match item call",
        "first_match pass", "captured pass", "computed pass", "sequence pass",
        "match item call", "first_match pass"
    };
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 4U);
    assert(std::ranges::all_of(
        interpreted.coverage,
        [](const auto& coverage) {
            return coverage.attempts == 2U && coverage.passes == 2U
                && coverage.failures == 0U;
        }));
}

void test_sequence_formal_actuals(const std::filesystem::path& directory)
{
    const auto source = directory / "sequence_formal_actuals.sv";
    {
        std::ofstream output(source);
        output << R"(
module sequence_formal_actuals;
  logic clock;
  logic request;
  logic acknowledge;
  sequence handshake(logic start = request, logic finish = acknowledge,
                     int delay = 1);
    start ##delay finish;
  endsequence
  property positional_property;
    @(posedge clock) handshake(request, acknowledge);
  endproperty
  property named_property;
    @(posedge clock) handshake(.finish(acknowledge), .start(request));
  endproperty
  property default_property;
    @(posedge clock) handshake();
  endproperty
  positional: assert property (positional_property)
    $display("sequence positional pass");
    else $display("sequence positional fail");
  named: assert property (named_property)
    $display("sequence named pass");
    else $display("sequence named fail");
  defaulted: assert property (default_property)
    $display("sequence default pass");
    else $display("sequence default fail");
  initial begin
    clock = 1'b0;
    request = 1'b1;
    acknowledge = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    acknowledge = 1'b1;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "sequence-formal-actuals";
    config.project.top = "sv:work.sequence_formal_actuals";
    config.build.cache_path = directory / "sequence-formal-actuals-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs {
        "sequence positional pass", "sequence named pass",
        "sequence default pass"
    };
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 3);
    assert(std::ranges::all_of(
        interpreted.coverage, [](const auto& coverage) {
            return coverage.attempts == 1 && coverage.passes == 1
                && coverage.failures == 0;
        }));
}

void test_disable_iff_scalar_property(const std::filesystem::path& directory)
{
    const auto source = directory / "disable_iff_scalar_property.sv";
    {
        std::ofstream output(source);
        output << R"(
module disable_iff_scalar_property;
  logic clock;
  logic reset;
  logic request;
  property enabled_request;
    @(posedge clock) disable iff (reset) request;
  endproperty
  request_check: assert property (enabled_request)
    $display("enabled pass");
    else $display("enabled fail");
  initial begin
    clock = 1'b0;
    reset = 1'b1;
    request = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    reset = 1'b0;
    request = 1'b1;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    request = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "disable-iff-scalar-property";
    config.project.top = "sv:work.disable_iff_scalar_property";
    config.build.cache_path = directory / "disable-iff-scalar-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs { "enabled pass", "enabled fail" };
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 1);
    assert(interpreted.coverage.front().attempts == 2);
    assert(interpreted.coverage.front().passes == 1);
    assert(interpreted.coverage.front().failures == 1);
    assert(interpreted.coverage.front().vacuous == 0);
    assert(interpreted.events.size() == 2);
    assert(interpreted.events.front().time == 3);
    assert(interpreted.events.back().time == 5);
}

void test_disable_iff_cancels_attempts(const std::filesystem::path& directory)
{
    const auto source = directory / "disable_iff_cancels_attempts.sv";
    {
        std::ofstream output(source);
        output << R"(
module disable_iff_cancels_attempts;
  logic clock;
  logic reset;
  logic request;
  logic acknowledge;
  property delayed_handshake;
    @(posedge clock) disable iff (reset)
      request |-> nexttime[2] acknowledge;
  endproperty
  handshake_check: assert property (delayed_handshake)
    $display("pass action");
    else $display("failure action");
  initial begin
    clock = 1'b0;
    reset = 1'b0;
    request = 1'b1;
    acknowledge = 1'b0;
    $assertvacuousoff;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    reset = 1'b1;
    request = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    reset = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "disable-iff-cancels-attempts";
    config.project.top = "sv:work.disable_iff_cancels_attempts";
    config.build.cache_path = directory / "disable-iff-cancels-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreted.outputs.empty());
    assert(compiled.outputs.empty());
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 1);
    assert(interpreted.coverage.front().attempts == 1);
    assert(interpreted.coverage.front().passes == 0);
    assert(interpreted.coverage.front().failures == 0);
    assert(interpreted.coverage.front().vacuous == 1);
    assert(interpreted.events.size() == 1);
    assert(interpreted.events.front().time == 5);
    assert(interpreted.events.front().outcome
        == fsim::app::ConcurrentAssertionOutcome::vacuous);
}

void test_cover_sequence_revisions(const std::filesystem::path& directory)
{
    const auto source = directory / "cover_sequence_revisions.sv";
    {
        std::ofstream output(source);
        output << R"(
module cover_sequence_revisions;
  logic clock;
  logic request;
  logic grant;
  sequence named_handshake;
    @(posedge clock) request ##1 grant;
  endsequence
  named_cover: cover sequence (named_handshake)
    $display("named sequence hit");
  inline_cover: cover sequence (
    @(posedge clock) request ##1 grant
  ) $display("inline sequence hit");
  initial begin
    clock = 1'b0;
    request = 1'b1;
    grant = 1'b0;
    #1 clock = 1'b1;
    #1 begin
      clock = 1'b0;
      request = 1'b0;
      grant = 1'b1;
      $assertoff;
    end
    #1 clock = 1'b1;
    #1 begin
      clock = 1'b0;
      request = 1'b1;
      grant = 1'b0;
      $asserton;
    end
    #1 clock = 1'b1;
    #1 begin
      clock = 1'b0;
      request = 1'b0;
      $assertoff;
    end
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
        assert(output.good());
    }

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        auto config = config_for(directory, source, optimization);
        config.project.name = optimization == fsim::project::Optimization::o0
            ? "cover-sequence-revisions-o0"
            : "cover-sequence-revisions-o2";
        config.project.top = "sv:work.cover_sequence_revisions";
        config.source_sets.front().standard = "2023";
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "cover-sequence-cache-o0"
                    : "cover-sequence-cache-o2");
        const auto interpreted = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::interpreter);
        const auto compiled = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::compiled);
        for (const auto* capture : { &compiled, &warm }) {
            assert(capture->outputs == interpreted.outputs);
            assert(capture->reports == interpreted.reports);
            assert(capture->coverage == interpreted.coverage);
            assert(capture->events == interpreted.events);
            assert(capture->callback_events == capture->events);
        }
        auto outputs = interpreted.outputs;
        std::ranges::sort(outputs);
        assert((outputs == std::vector<std::string> {
            "inline sequence hit", "named sequence hit" }));
        assert(interpreted.reports.empty());
        assert(interpreted.coverage.size() == 2);
        assert(interpreted.events.size() == 4);
        for (const auto& coverage : interpreted.coverage) {
            assert(coverage.kind
                == fsim::app::ConcurrentAssertionCoverageKind::cover);
            assert(coverage.attempts == 2);
            assert(coverage.passes == 1);
            assert(coverage.failures == 1);
            assert(coverage.vacuous == 0);
        }
        assert(std::ranges::count_if(
                   interpreted.events, [](const auto& event) {
                       return event.outcome
                           == fsim::app::ConcurrentAssertionOutcome::pass;
                   })
            == 2);
        assert(std::ranges::count_if(
                   interpreted.events, [](const auto& event) {
                       return event.outcome
                           == fsim::app::ConcurrentAssertionOutcome::failure;
                   })
            == 2);
#if defined(FSIM_HAS_LLVM)
        assert(compiled.native_cache.misses != 0);
        assert(warm.native_cache.hits != 0);
        assert(warm.native_cache.misses == 0);
#endif
    }
}

void test_concurrent_assertion_execution_regions(
    const std::filesystem::path& directory)
{
    const auto source = directory / "concurrent_assertion_regions.sv";
    {
        std::ofstream output(source);
        output << R"(
module concurrent_assertion_regions;
  logic clock;
  logic sampled_value;
  property sampled_before_update_property;
    @(posedge clock) sampled_value;
  endproperty
  sampled_before_update: assert property (
    sampled_before_update_property
  ) $display("reactive pass");
    else $error("sampled current value instead of preponed value");
  initial begin
    clock = 1'b0;
    sampled_value = 1'b1;
    #1 begin
      sampled_value = 1'b0;
      clock = 1'b1;
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
        auto config = config_for(directory, source, optimization);
        config.project.name = optimization == fsim::project::Optimization::o0
            ? "concurrent-assertion-regions-o0"
            : "concurrent-assertion-regions-o2";
        config.project.top = "sv:work.concurrent_assertion_regions";
        config.source_sets.front().standard = "2023";
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "concurrent-regions-cache-o0"
                    : "concurrent-regions-cache-o2");
        const auto interpreted = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::interpreter);
        const auto compiled = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_concurrent_assertions(
            config, fsim::app::SimulationEngine::compiled);
        for (const auto* capture : { &interpreted, &compiled, &warm }) {
            assert(capture->assertion_processes == 1);
            assert(capture->assertion_processes_observed);
            assert(capture->sampled_signal_reads == 1);
            assert(capture->current_signal_reads == 0);
            assert(capture->reactive_waits == 2);
            assert(capture->outputs
                == std::vector<std::string> { "reactive pass" });
            assert(capture->reports.empty());
            assert(capture->coverage.size() == 1);
            assert(capture->coverage.front().attempts == 1);
            assert(capture->coverage.front().passes == 1);
            assert(capture->coverage.front().failures == 0);
            assert(capture->events.size() == 1);
            assert(capture->events.front().outcome
                == fsim::app::ConcurrentAssertionOutcome::pass);
            assert(capture->events.front().time == 1);
            assert(capture->events.front().delta == 1);
            assert((capture->timeline
                == std::vector<std::string> {
                    "event:pass", "output:reactive pass" }));
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
