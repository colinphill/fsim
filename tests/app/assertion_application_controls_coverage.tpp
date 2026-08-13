// SPDX-License-Identifier: Apache-2.0

void test_ranged_eventually_properties(
    const std::filesystem::path& directory)
{
    const auto source = directory / "ranged_eventually_properties.sv";
    {
        std::ofstream output(source);
        output << R"(
module ranged_eventually_properties;
  logic clock;
  logic within_range;
  logic early_only;
  logic never;
  property bounded_hit;
    @(posedge clock) eventually[2:4] within_range;
  endproperty
  property bounded_miss;
    @(posedge clock) eventually[2:4] early_only;
  endproperty
  property weak_unfinished;
    @(posedge clock) eventually[2:8] never;
  endproperty
  property strong_unfinished;
    @(posedge clock) s_eventually[2:8] never;
  endproperty
  bounded_hit_check: assert property (bounded_hit)
    $display("bounded eventually pass");
    else $display("bounded eventually fail");
  bounded_miss_check: assert property (bounded_miss)
    $display("bounded miss pass");
    else $display("bounded miss fail");
  weak_unfinished_check: assert property (weak_unfinished)
    $display("weak ranged end pass");
    else $display("weak ranged end fail");
  strong_unfinished_check: assert property (strong_unfinished)
    $display("strong ranged end pass");
    else $display("strong ranged end fail");
  initial begin
    clock = 1'b0;
    within_range = 1'b0;
    early_only = 1'b0;
    never = 1'b0;
    #1 clock = 1'b1;
    #1 begin
      clock = 1'b0;
      early_only = 1'b1;
      $assertoff;
    end
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; early_only = 1'b0; end
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; within_range = 1'b1; end
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; within_range = 1'b0; end
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "ranged-eventually-properties";
    config.project.top = "sv:work.ranged_eventually_properties";
    config.build.cache_path = directory / "ranged-eventually-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreted.outputs == compiled.outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 4);
    const auto coverage = [&](const std::string_view name) -> const auto& {
        const auto found = std::ranges::find(
            interpreted.coverage, name,
            &fsim::app::ConcurrentAssertionCoverage::name);
        assert(found != interpreted.coverage.end());
        return *found;
    };
    assert(coverage("bounded_hit_check").passes == 1);
    assert(coverage("bounded_miss_check").failures == 1);
    assert(coverage("weak_unfinished_check").vacuous == 1);
    assert(coverage("strong_unfinished_check").failures == 1);
    assert(std::ranges::all_of(interpreted.coverage, [](const auto& value) {
        return value.attempts == 1;
    }));
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 7;
    }) == 1);
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 9;
    }) == 1);
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 10;
    }) == 2);
    auto outputs = interpreted.outputs;
    std::ranges::sort(outputs);
    assert((outputs == std::vector<std::string> { "bounded eventually pass", "bounded miss fail", "strong ranged end fail", "weak ranged end pass" }));
}

void test_ranged_always_properties(const std::filesystem::path& directory)
{
    const auto source = directory / "ranged_always_properties.sv";
    {
        std::ofstream output(source);
        output << R"(
module ranged_always_properties;
  logic clock;
  logic stable_hit;
  logic stable_miss;
  property bounded_hit;
    @(posedge clock) always[1:3] stable_hit;
  endproperty
  property bounded_miss;
    @(posedge clock) always[1:3] stable_miss;
  endproperty
  property weak_unfinished;
    @(posedge clock) always[1:8] stable_hit;
  endproperty
  property strong_unfinished;
    @(posedge clock) s_always[1:8] stable_hit;
  endproperty
  bounded_hit_check: assert property (bounded_hit)
    $display("bounded always pass");
    else $display("bounded always fail");
  bounded_miss_check: assert property (bounded_miss)
    $display("bounded always miss pass");
    else $display("bounded always miss fail");
  weak_unfinished_check: assert property (weak_unfinished)
    $display("weak always end pass");
    else $display("weak always end fail");
  strong_unfinished_check: assert property (strong_unfinished)
    $display("strong always end pass");
    else $display("strong always end fail");
  initial begin
    clock = 1'b0;
    stable_hit = 1'b1;
    stable_miss = 1'b1;
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; $assertoff; end
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; stable_miss = 1'b0; end
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "ranged-always-properties";
    config.project.top = "sv:work.ranged_always_properties";
    config.build.cache_path = directory / "ranged-always-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreted.outputs == compiled.outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 4);
    const auto coverage = [&](const std::string_view name) -> const auto& {
        const auto found = std::ranges::find(
            interpreted.coverage, name,
            &fsim::app::ConcurrentAssertionCoverage::name);
        assert(found != interpreted.coverage.end());
        return *found;
    };
    assert(coverage("bounded_hit_check").passes == 1);
    assert(coverage("bounded_miss_check").failures == 1);
    assert(coverage("weak_unfinished_check").vacuous == 1);
    assert(coverage("strong_unfinished_check").failures == 1);
    assert(std::ranges::all_of(interpreted.coverage, [](const auto& value) {
        return value.attempts == 1;
    }));
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 5;
    }) == 1);
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 7;
    }) == 1);
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 8;
    }) == 2);
    auto outputs = interpreted.outputs;
    std::ranges::sort(outputs);
    assert((outputs == std::vector<std::string> { "bounded always miss fail", "bounded always pass", "strong always end fail", "weak always end pass" }));
}

void test_scalar_property_aborts(const std::filesystem::path& directory)
{
    const auto source = directory / "scalar_property_aborts.sv";
    {
        std::ofstream output(source);
        output << R"(
module scalar_property_aborts;
  logic clock;
  logic reset;
  logic request;
  property async_accept;
    @(posedge clock) accept_on(reset) request;
  endproperty
  property async_reject;
    @(posedge clock) reject_on(reset) request;
  endproperty
  property synchronous_accept;
    @(posedge clock) sync_accept_on(reset) request;
  endproperty
  property synchronous_reject;
    @(posedge clock) sync_reject_on(reset) request;
  endproperty
  async_accept_check: assert property (async_accept)
    $display("async accept pass"); else $display("async accept fail");
  async_reject_check: assert property (async_reject)
    $display("async reject pass"); else $display("async reject fail");
  synchronous_accept_check: assert property (synchronous_accept)
    $display("sync accept pass"); else $display("sync accept fail");
  synchronous_reject_check: assert property (synchronous_reject)
    $display("sync reject pass"); else $display("sync reject fail");
  initial begin
    clock = 1'b0;
    reset = 1'b1;
    request = 1'b0;
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; $assertoff; end
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "scalar-property-aborts";
    config.project.top = "sv:work.scalar_property_aborts";
    config.build.cache_path = directory / "scalar-property-aborts-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreted.outputs == compiled.outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 4);
    const auto coverage = [&](const std::string_view name) -> const auto& {
        const auto found = std::ranges::find(
            interpreted.coverage, name,
            &fsim::app::ConcurrentAssertionCoverage::name);
        assert(found != interpreted.coverage.end());
        return *found;
    };
    assert(coverage("async_accept_check").vacuous == 1);
    assert(coverage("async_reject_check").failures == 1);
    assert(coverage("synchronous_accept_check").vacuous == 1);
    assert(coverage("synchronous_reject_check").failures == 1);
    assert(std::ranges::all_of(interpreted.coverage, [](const auto& value) {
        return value.attempts == 1;
    }));
    auto outputs = interpreted.outputs;
    std::ranges::sort(outputs);
    assert((outputs == std::vector<std::string> { "async accept pass", "async reject fail", "sync accept pass", "sync reject fail" }));
}

void test_sequence_delay_ranges(const std::filesystem::path& directory)
{
    const auto source = directory / "sequence_delay_ranges.sv";
    {
        std::ofstream output(source);
        output << R"(
module sequence_delay_ranges;
  logic clock;
  logic request;
  logic acknowledge;
  logic never;
  sequence hit_path; request ##[1:3] acknowledge; endsequence
  sequence miss_path; request ##[1:3] never; endsequence
  sequence unfinished_path; request ##[1:8] never; endsequence
  property hit; @(posedge clock) hit_path; endproperty
  property miss; @(posedge clock) miss_path; endproperty
  property unfinished; @(posedge clock) unfinished_path; endproperty
  hit_check: assert property (hit)
    $display("range sequence pass"); else $display("range sequence fail");
  miss_check: assert property (miss)
    $display("range miss pass"); else $display("range miss fail");
  unfinished_check: assert property (unfinished)
    $display("range end pass"); else $display("range end fail");
  initial begin
    clock = 1'b0;
    request = 1'b1;
    acknowledge = 1'b0;
    never = 1'b0;
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; request = 1'b0; $assertoff; end
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; acknowledge = 1'b1; end
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; acknowledge = 1'b0; end
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "sequence-delay-ranges";
    config.project.top = "sv:work.sequence_delay_ranges";
    config.build.cache_path = directory / "sequence-delay-ranges-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreted.outputs == compiled.outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 3);
    const auto coverage = [&](const std::string_view name) -> const auto& {
        const auto found = std::ranges::find(
            interpreted.coverage, name,
            &fsim::app::ConcurrentAssertionCoverage::name);
        assert(found != interpreted.coverage.end());
        return *found;
    };
    assert(coverage("hit_check").passes == 1);
    assert(coverage("miss_check").failures == 1);
    assert(coverage("unfinished_check").failures == 1);
    assert(std::ranges::all_of(interpreted.coverage, [](const auto& value) {
        return value.attempts == 1;
    }));
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 5;
    }) == 1);
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 7;
    }) == 1);
    assert(std::ranges::count_if(interpreted.events, [](const auto& event) {
        return event.time == 8;
    }) == 1);
    auto outputs = interpreted.outputs;
    std::ranges::sort(outputs);
    assert((outputs == std::vector<std::string> { "range end fail", "range miss fail", "range sequence pass" }));
}

void test_scalar_sequence_combinators(const std::filesystem::path& directory)
{
    const auto source = directory / "scalar_sequence_combinators.sv";
    {
        std::ofstream output(source);
        output << R"(
module scalar_sequence_combinators;
  logic clock;
  logic left;
  logic right;
  logic missing;
  sequence both; left intersect right; endsequence
  sequence not_both; left intersect missing; endsequence
  sequence held; left throughout right; endsequence
  sequence absent_within; left within missing; endsequence
  sequence earliest; first_match(left); endsequence
  property both_property; @(posedge clock) both; endproperty
  property not_both_property; @(posedge clock) not_both; endproperty
  property held_property; @(posedge clock) held; endproperty
  property absent_within_property; @(posedge clock) absent_within; endproperty
  property earliest_property; @(posedge clock) earliest; endproperty
  both_check: assert property (both_property)
    $display("intersection pass"); else $display("intersection fail");
  not_both_check: assert property (not_both_property)
    $display("intersection unexpected"); else $display("intersection expected fail");
  held_check: assert property (held_property)
    $display("throughout pass"); else $display("throughout fail");
  within_check: assert property (absent_within_property)
    $display("within unexpected"); else $display("within expected fail");
  first_match_check: assert property (earliest_property)
    $display("first match pass"); else $display("first match fail");
  initial begin
    clock = 1'b0;
    left = 1'b1;
    right = 1'b1;
    missing = 1'b0;
    #1 clock = 1'b1;
    #1 begin clock = 1'b0; $assertoff; end
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "scalar-sequence-combinators";
    config.project.top = "sv:work.scalar_sequence_combinators";
    config.build.cache_path = directory / "scalar-sequence-combinators-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreted.outputs == compiled.outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 5);
    assert(std::ranges::count_if(interpreted.coverage, [](const auto& value) {
        return value.passes == 1 && value.failures == 0;
    }) == 3);
    assert(std::ranges::count_if(interpreted.coverage, [](const auto& value) {
        return value.passes == 0 && value.failures == 1;
    }) == 2);
    auto outputs = interpreted.outputs;
    std::ranges::sort(outputs);
    assert((outputs == std::vector<std::string> { "first match pass", "intersection expected fail", "intersection pass", "throughout pass", "within expected fail" }));
}

void test_vacuous_action_controls(const std::filesystem::path& directory)
{
    const auto source = directory / "vacuous_action_controls.sv";
    {
        std::ofstream output(source);
        output << R"(
module vacuous_action_controls;
  logic clock;
  logic request;
  logic acknowledge;
  property handshake;
    @(posedge clock) request |-> acknowledge;
  endproperty
  handshake_check: assert property (handshake)
    $display("pass action");
    else $display("failure action");
  initial begin
    clock = 1'b0;
    request = 1'b0;
    acknowledge = 1'b1;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    $assertvacuousoff;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    request = 1'b1;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    $assertpassoff;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    $assertnonvacuouson;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    request = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    $assertpasson;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "vacuous-action-controls";
    config.project.top = "sv:work.vacuous_action_controls";
    config.build.cache_path = directory / "vacuous-action-controls-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs(4, "pass action");
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 1);
    assert(interpreted.coverage.front().attempts == 7);
    assert(interpreted.coverage.front().passes == 3);
    assert(interpreted.coverage.front().failures == 0);
    assert(interpreted.coverage.front().vacuous == 4);
    constexpr std::array expected_suppression {
        false, true, false, true, false, true, false
    };
    assert(interpreted.events.size() == expected_suppression.size());
    for (std::size_t index = 0; index < expected_suppression.size(); ++index) {
        assert(interpreted.events[index].action_suppressed
            == expected_suppression[index]);
    }
}

void test_concurrent_action_blocks(const std::filesystem::path& directory)
{
    const auto source = directory / "concurrent_action_blocks.sv";
    {
        std::ofstream output(source);
        output << R"(
module concurrent_action_blocks;
  logic clock;
  logic request;
  property tracked;
    @(posedge clock) request;
  endproperty
  property never;
    @(posedge clock) 1'b0;
  endproperty
  block_check: assert property (tracked)
    begin
      $display("pass display");
      $warning("pass warning");
    end
    else begin
      $display("failure display");
      $error("failure error");
    end
  null_check: assert property (never) else ;
  initial begin
    clock = 1'b0;
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
    config.project.name = "concurrent-action-blocks";
    config.project.top = "sv:work.concurrent_action_blocks";
    config.build.cache_path = directory / "concurrent-action-blocks-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> outputs {
        "pass display", "failure display"
    };
    assert(interpreted.outputs == outputs);
    assert(compiled.outputs == outputs);
    assert(interpreted.reports == compiled.reports);
    assert(interpreted.reports.size() == 2);
    assert(interpreted.reports[0].message == "pass warning");
    assert(interpreted.reports[0].severity
        == fsim::runtime::simir::AssertionSeverity::warning);
    assert(interpreted.reports[1].message == "failure error");
    assert(interpreted.reports[1].severity
        == fsim::runtime::simir::AssertionSeverity::error);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 2);
    const auto block = std::ranges::find_if(
        interpreted.coverage, [](const auto& coverage) {
            return coverage.name == "block_check";
        });
    const auto null = std::ranges::find_if(
        interpreted.coverage, [](const auto& coverage) {
            return coverage.name == "null_check";
        });
    assert(block != interpreted.coverage.end());
    assert(block->attempts == 2 && block->passes == 1
        && block->failures == 1);
    assert(null != interpreted.coverage.end());
    assert(null->attempts == 2 && null->passes == 0
        && null->failures == 2);
}

void test_assertkill_cancels_attempts(const std::filesystem::path& directory)
{
    const auto source = directory / "assertkill_cancels_attempts.sv";
    {
        std::ofstream output(source);
        output << R"(
module assertkill_cancels_attempts;
  logic clock;
  logic request;
  logic acknowledge;
  property delayed_handshake;
    @(posedge clock) request |-> nexttime[2] acknowledge;
  endproperty
  handshake_check: assert property (delayed_handshake)
    $display("pass action");
    else $display("failure action");
  initial begin
    clock = 1'b0;
    request = 1'b1;
    acknowledge = 1'b0;
    $assertvacuousoff;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    request = 1'b0;
    $assertkill;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    $asserton;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "assertkill-cancels-attempts";
    config.project.top = "sv:work.assertkill_cancels_attempts";
    config.build.cache_path = directory / "assertkill-cancels-cache";
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
    assert(interpreted.events.back().time == 7);
    assert(interpreted.events.back().outcome
        == fsim::app::ConcurrentAssertionOutcome::vacuous);
}

void test_assertoff_preserves_active_attempts(
    const std::filesystem::path& directory)
{
    const auto source = directory / "assertoff_preserves_active_attempts.sv";
    {
        std::ofstream output(source);
        output << R"(
module assertoff_preserves_active_attempts;
  logic clock;
  logic request;
  logic acknowledge;
  property delayed_handshake;
    @(posedge clock) request |-> nexttime[2] acknowledge;
  endproperty
  handshake_check: assert property (delayed_handshake)
    $display("pass action");
  initial begin
    clock = 1'b0;
    request = 1'b1;
    acknowledge = 1'b0;
    $assertvacuousoff;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    $assertoff;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    acknowledge = 1'b1;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    request = 1'b0;
    $asserton;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2);
    config.project.name = "assertoff-preserves-active-attempts";
    config.project.top = "sv:work.assertoff_preserves_active_attempts";
    config.build.cache_path = directory / "assertoff-preserves-cache";
    const auto interpreted = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_concurrent_assertions(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreted.outputs == std::vector<std::string> { "pass action" });
    assert(interpreted.outputs == compiled.outputs);
    assert(interpreted.coverage == compiled.coverage);
    assert(interpreted.events == compiled.events);
    assert(interpreted.coverage.size() == 1);
    assert(interpreted.coverage.front().attempts == 2);
    assert(interpreted.coverage.front().passes == 1);
    assert(interpreted.coverage.front().failures == 0);
    assert(interpreted.coverage.front().vacuous == 1);
    assert(interpreted.events.size() == 2);
    assert(interpreted.events.front().time == 5);
    assert(interpreted.events.front().outcome
        == fsim::app::ConcurrentAssertionOutcome::pass);
    assert(interpreted.events.back().time == 7);
    assert(interpreted.events.back().outcome
        == fsim::app::ConcurrentAssertionOutcome::vacuous);
}

struct CoverageExecutionCapture {
    std::map<std::string, std::vector<std::uint64_t>> hits;
    std::vector<fsim::frontend::SystemVerilogCoverageCallbackEvent> callbacks;
    std::vector<fsim::frontend::SystemVerilogCoverageTraceEvent> trace;
    std::string report;
    std::size_t compiled_processes { };
    fsim::app::NativeCacheStatistics cache;
};

void test_covergroup_execution(const std::filesystem::path& directory)
{
    const auto source = directory / "coverage_execution.sv";
    {
        std::ofstream output(source);
        output << R"(
module coverage_execution;
  logic clock;
  logic alternate;
  logic guard_clock;
  logic enabled;
  logic [136:0] observed;
  covergroup wide_group with function sample(input logic [136:0] value);
    option.per_instance = 1;
    value_point: coverpoint value {
      bins exact = {137'h1_0000000000000000_0000000000000000_01};
      bins range = {[137'h1_0000000000000000_0000000000000000_10:
                     137'h1_0000000000000000_0000000000000000_12]};
    }
  endgroup
  covergroup guarded_group @(
      posedge guard_clock or posedge alternate
      iff (enabled && (observed != 137'h0)));
    value_point: coverpoint observed {
      bins exact = {137'h1_0000000000000000_0000000000000000_01};
      bins range = {[137'h1_0000000000000000_0000000000000000_10:
                     137'h1_0000000000000000_0000000000000000_12]};
    }
  endgroup
  covergroup explicit_group;
    value_point: coverpoint (((observed + 137'h1) ^ 137'h3) ^ 137'h3) {
      bins exact = {137'h1_0000000000000000_0000000000000000_02};
      bins range = {[137'h1_0000000000000000_0000000000000000_11:
                     137'h1_0000000000000000_0000000000000000_13]};
    }
  endgroup
  covergroup event_group @(posedge clock or posedge alternate);
    value_point: coverpoint (observed ^ 137'h1) {
      bins exact = {137'h1_0000000000000000_0000000000000000_00};
      bins range = {[137'h1_0000000000000000_0000000000000000_0f:
                     137'h1_0000000000000000_0000000000000000_11]};
    }
  endgroup
  wide_group monitor = new();
  explicit_group explicit_monitor = new();
  event_group event_monitor = new();
  guarded_group guarded_monitor = new();
  initial begin
    clock = 1'b0;
    alternate = 1'b0;
    guard_clock = 1'b0;
    enabled = 1'b0;
    observed = 137'h1_0000000000000000_0000000000000000_01;
    monitor.stop();
    monitor.sample(137'h1_0000000000000000_0000000000000000_10);
    monitor.start();
    monitor.sample(137'h1_0000000000000000_0000000000000000_01);
    explicit_monitor.stop();
    explicit_monitor.sample();
    explicit_monitor.start();
    explicit_monitor.sample();
    event_monitor.stop();
    guarded_monitor.stop();
    #1 begin alternate = 1'b1; guard_clock = 1'b1; end
    #1 begin
      clock = 1'b0;
      guard_clock = 1'b0;
      enabled = 1'b1;
      event_monitor.start();
      guarded_monitor.start();
    end
    observed = 137'h1_0000000000000000_0000000000000000_11;
    monitor.sample(137'h1_0000000000000000_0000000000000000_11);
    explicit_monitor.sample();
    #1 begin clock = 1'b1; guard_clock = 1'b1; end
    #1;
    $finish;
  end
endmodule
)";
    }
    const auto make_config = [&](const fsim::project::Optimization optimization) {
        fsim::project::Config config;
        config.base_directory = directory;
        config.project.name = "coverage-execution";
        config.project.top = "sv:work.coverage_execution";
        config.project.time_resolution = "1ns";
        config.build.optimization = optimization;
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "coverage-cache-o0"
                    : "coverage-cache-o2");
        config.run.max_deltas = 1000;
        fsim::project::SourceSet source_set;
        source_set.language = fsim::project::Language::system_verilog;
        source_set.standard = "2017";
        source_set.library = "work";
        source_set.files.push_back(source);
        config.source_sets.push_back(std::move(source_set));
        return config;
    };
    const auto capture_run = [&](fsim::app::BuiltProject project,
                                 const fsim::app::SimulationEngine engine) {
        fsim::app::Simulation simulation { std::move(project), 1000, engine };
        CoverageExecutionCapture result_capture;
        result_capture.compiled_processes = simulation.compiled_process_count();
        result_capture.cache = simulation.native_cache_statistics();
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::stopped);
        const auto& coverage = simulation.systemverilog_coverage();
        assert(coverage.declarations.size() == 4);
        assert(coverage.instances.size() == 4);
        assert(coverage.reports.size() == 4);
        for (const auto& instance : coverage.instances) {
            auto& hits = result_capture.hits[instance.name];
            for (const auto& hit : instance.bin_hits) {
                hits.push_back(hit.hit_count);
            }
        }
        result_capture.callbacks = coverage.callback_events;
        result_capture.trace = coverage.trace_events;
        for (const auto& report : coverage.reports) {
            result_capture.report += fsim::frontend::render_systemverilog_coverage_report(
                report);
        }
        return result_capture;
    };
    const auto build = [&](const fsim::project::Optimization optimization) {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(
            make_config(optimization), diagnostics);
        if (!project || diagnostics.has_error()) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": "
                          << diagnostic.message << '\n';
            }
        }
        assert(project && !diagnostics.has_error());
        return std::move(*project);
    };
    const auto run = [&](const fsim::project::Optimization optimization,
                         const fsim::app::SimulationEngine engine) {
        return capture_run(build(optimization), engine);
    };
    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        const auto interpreted = run(
            optimization, fsim::app::SimulationEngine::interpreter);
        const auto compiled_cold = run(
            optimization, fsim::app::SimulationEngine::compiled);
        const auto compiled_warm = run(
            optimization, fsim::app::SimulationEngine::compiled);
        for (const auto* run_capture :
            { &interpreted, &compiled_cold, &compiled_warm }) {
            assert(run_capture->hits.size() == 4);
            assert(run_capture->hits.at("guarded_monitor")
                == std::vector<std::uint64_t>({ 1 }));
            assert(std::ranges::all_of(
                run_capture->hits, [](const auto& entry) {
                    return entry.second
                        == ((entry.first == "guarded_monitor"
                                || entry.first == "event_monitor")
                                ? std::vector<std::uint64_t> { 1 }
                                : std::vector<std::uint64_t> { 1, 1 });
                }));
            assert(run_capture->callbacks.size() == 18);
            assert(run_capture->trace.size() == 18);
            assert(run_capture->report.find("100.00") != std::string::npos);
            assert(std::ranges::count_if(
                       run_capture->callbacks, [](const auto& event) {
                           return event.trigger
                               == fsim::frontend::SystemVerilogCoverageSampleTrigger::Procedural;
                       })
                == 6);
            assert(std::ranges::count_if(
                       run_capture->callbacks, [](const auto& event) {
                           return event.trigger
                               == fsim::frontend::SystemVerilogCoverageSampleTrigger::Explicit;
                       })
                == 6);
            assert(std::ranges::count_if(
                       run_capture->callbacks, [](const auto& event) {
                           return event.trigger
                               == fsim::frontend::SystemVerilogCoverageSampleTrigger::Event;
                       })
                == 6);
            assert(std::ranges::any_of(
                run_capture->trace, [](const auto& event) {
                    return event.time == 3;
                }));
        }
        assert(interpreted.hits == compiled_cold.hits);
        assert(interpreted.report == compiled_cold.report);
        assert(compiled_cold.report == compiled_warm.report);
#if defined(FSIM_HAS_LLVM)
        assert(compiled_cold.compiled_processes >= 1);
        assert(compiled_warm.cache.hits == 1);
#endif
    }
    const auto object = directory / "coverage-execution.fsimobj";
    const auto artifact = directory / "coverage-execution.fsimdesign";
    auto artifact_config = make_config(fsim::project::Optimization::o2);
    fsim::diagnostic::Engine artifact_diagnostics;
    const auto compiled_object = fsim::app::compile_artifact(
        artifact_config, object, artifact_diagnostics);
    if (!compiled_object || artifact_diagnostics.has_error()) {
        for (const auto& diagnostic : artifact_diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(compiled_object && !artifact_diagnostics.has_error());
    artifact_config.source_sets.clear();
    artifact_config.build.cache_path = directory / "coverage-elaboration-cache";
    const std::array objects { object };
    const auto elaborated = fsim::app::elaborate_artifact(
        artifact_config, objects, artifact, artifact_diagnostics);
    if (!elaborated || artifact_diagnostics.has_error()) {
        for (const auto& diagnostic : artifact_diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated && !artifact_diagnostics.has_error());
    assert(!artifact_diagnostics.has_error());
    auto restored = fsim::app::load_design_artifact(
        artifact, artifact_diagnostics);
    assert(restored && !artifact_diagnostics.has_error());
    restored->cache_path = directory / "coverage-artifact-cache";
    const auto artifact_capture = capture_run(
        std::move(*restored), fsim::app::SimulationEngine::compiled);
    assert(artifact_capture.hits.size() == 4);
    assert(artifact_capture.hits.at("guarded_monitor")
        == std::vector<std::uint64_t> { 1 });
    assert(std::ranges::all_of(
        artifact_capture.hits, [](const auto& entry) {
            return entry.second
                == ((entry.first == "guarded_monitor"
                        || entry.first == "event_monitor")
                        ? std::vector<std::uint64_t> { 1 }
                        : std::vector<std::uint64_t> { 1, 1 });
        }));
    assert(artifact_capture.callbacks.size() == 18);
    assert(artifact_capture.trace.size() == 18);
#if defined(FSIM_HAS_LLVM)
    assert(artifact_capture.compiled_processes >= 1);
#endif
}

struct AdvancedCoverageCapture {
    std::map<std::string, std::uint64_t> bin_hits;
    std::map<std::string, std::uint64_t> cross_hits;
    std::vector<std::string> cross_operands;
    std::vector<fsim::frontend::SystemVerilogCoverageCallbackEvent> callbacks;
    std::size_t compiled_processes { };
};

void test_wide_transition_cross_execution(
    const std::filesystem::path& directory)
{
    const auto source = directory / "wide_transition_cross.sv";
    {
        std::ofstream output(source);
        output << R"(
module wide_transition_cross;
  covergroup transition_group with function sample(
      input logic [136:0] value, input logic [3:0] lane);
    value_point: coverpoint value {
      bins path = (137'h1_0000000000000000_0000000000000000_01 =>
                   137'h1_0000000000000000_0000000000000000_02);
      bins exact_x = {137'bx};
      bins exact_z = {137'bz};
    }
    lane_point: coverpoint lane {
      bins selected = {4'h3};
    }
    value_lane: cross value_point, lane_point;
  endgroup
  transition_group monitor = new();
  initial begin
    monitor.sample(137'h1_0000000000000000_0000000000000000_01, 4'h3);
    monitor.sample(137'h1_0000000000000000_0000000000000000_02, 4'h3);
    monitor.sample(137'bx, 4'h3);
    monitor.sample(137'bz, 4'h3);
    $finish;
  end
endmodule
)";
    }
    const auto run = [&](const fsim::project::Optimization optimization,
                         const fsim::app::SimulationEngine engine) {
        fsim::project::Config config;
        config.base_directory = directory;
        config.project.name = "wide-transition-cross";
        config.project.top = "sv:work.wide_transition_cross";
        config.project.time_resolution = "1ns";
        config.build.optimization = optimization;
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "wide-transition-cross-o0"
                    : "wide-transition-cross-o2");
        config.run.max_deltas = 1000;
        fsim::project::SourceSet sources;
        sources.language = fsim::project::Language::system_verilog;
        sources.standard = "2017";
        sources.library = "work";
        sources.files.push_back(source);
        config.source_sets.push_back(std::move(sources));
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        if (!project || diagnostics.has_error()) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        assert(project && !diagnostics.has_error());
        fsim::app::Simulation simulation { std::move(*project), 1000, engine };
        AdvancedCoverageCapture capture;
        capture.compiled_processes = simulation.compiled_process_count();
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::stopped);
        const auto& coverage = simulation.systemverilog_coverage();
        assert(coverage.instances.size() == 1);
        for (const auto& hit : coverage.instances.front().bin_hits) {
            capture.bin_hits.emplace(hit.identity, hit.hit_count);
        }
        for (const auto& cross : coverage.instances.front().cross_bin_state) {
            capture.cross_hits.emplace(cross.identity, cross.hit_count);
            capture.cross_operands.insert(
                capture.cross_operands.end(),
                cross.operand_bin_identities.begin(),
                cross.operand_bin_identities.end());
        }
        capture.callbacks = coverage.callback_events;
        return capture;
    };
    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        const auto interpreted = run(
            optimization, fsim::app::SimulationEngine::interpreter);
        const auto compiled_cold = run(
            optimization, fsim::app::SimulationEngine::compiled);
        const auto compiled_warm = run(
            optimization, fsim::app::SimulationEngine::compiled);
        assert(interpreted.bin_hits == compiled_cold.bin_hits);
        assert(compiled_cold.bin_hits == compiled_warm.bin_hits);
        assert(interpreted.cross_hits == compiled_cold.cross_hits);
        assert(compiled_cold.cross_hits == compiled_warm.cross_hits);
        assert(interpreted.cross_operands == compiled_cold.cross_operands);
        assert(compiled_cold.cross_operands == compiled_warm.cross_operands);
        assert(interpreted.callbacks.size() == compiled_cold.callbacks.size());
        assert(compiled_cold.callbacks.size() == compiled_warm.callbacks.size());
        assert(interpreted.bin_hits.size() == 4);
        assert(std::ranges::any_of(
            interpreted.bin_hits, [](const auto& hit) {
                return hit.first.ends_with("value_point.path")
                    && hit.second == 1;
            }));
        assert(std::ranges::any_of(
            interpreted.bin_hits, [](const auto& hit) {
                return hit.first.ends_with("lane_point.selected")
                    && hit.second == 4;
            }));
        assert(std::ranges::any_of(
            interpreted.bin_hits, [](const auto& hit) {
                return hit.first.ends_with("value_point.exact_x")
                    && hit.second == 1;
            }));
        assert(std::ranges::any_of(
            interpreted.bin_hits, [](const auto& hit) {
                return hit.first.ends_with("value_point.exact_z")
                    && hit.second == 1;
            }));
        assert(interpreted.cross_hits.size() == 3);
        assert(std::ranges::all_of(
            interpreted.cross_hits, [](const auto& hit) {
                return hit.second == 1;
            }));
        assert(interpreted.cross_operands.size() == 6);
        assert(interpreted.callbacks.size() >= 16);
#if defined(FSIM_HAS_LLVM)
        assert(compiled_cold.compiled_processes >= 1);
#endif
    }
}
