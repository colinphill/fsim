// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <cassert>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  struct Change {
    fsim::runtime::SimulationTick time{};
    std::uint64_t delta{};
    std::string value;
  };

  fsim::runtime::RunResult result;
  std::string event;
  std::string observed;
  std::string repeated;
  std::string function_observed;
  std::string task_observed;
  std::string expression_observed;
  std::string controlled;
  std::string body_clock;
  std::string repeat_count_calls;
  std::string blocking_missed;
  std::string blocking_caught;
  std::string nba_caught;
  std::string zero_nba_caught;
  std::string triple_caught;
  std::string order_success;
  std::string order_failure;
  std::string triggered_same_time;
  std::string triggered_expired;
  std::string procedural_alias_caught;
  std::string declaration_alias_caught;
  std::string null_triggered;
  std::string vcd;
  std::vector<Change> event_changes;
  std::vector<Change> observed_changes;
  std::vector<Change> triple_changes;
  std::size_t compiled_processes{};
};

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine) {
  fsim::app::Simulation simulation{
      std::move(project), 1000, engine};
  const auto event = simulation.find_signal("named_event_test.fired");
  const auto observed =
      simulation.find_signal("named_event_test.observed");
  const auto repeated =
      simulation.find_signal("named_event_test.repeated");
  const auto function_observed =
      simulation.find_signal("named_event_test.function_observed");
  const auto task_observed =
      simulation.find_signal("named_event_test.task_observed");
  const auto expression_observed =
      simulation.find_signal("named_event_test.expression_observed");
  const auto controlled =
      simulation.find_signal("named_event_test.controlled");
  const auto body_clock =
      simulation.find_signal("named_event_test.body_clock");
  const auto repeat_count_calls =
      simulation.find_signal("named_event_test.repeat_count_calls");
  const auto blocking_missed =
      simulation.find_signal("named_event_test.blocking_missed");
  const auto blocking_caught =
      simulation.find_signal("named_event_test.blocking_caught");
  const auto nba_caught =
      simulation.find_signal("named_event_test.nba_caught");
  const auto zero_nba_caught =
      simulation.find_signal("named_event_test.zero_nba_caught");
  const auto triple_caught =
      simulation.find_signal("named_event_test.triple_caught");
  const auto order_success = simulation.find_signal("named_event_test.order_success");
  const auto order_failure = simulation.find_signal("named_event_test.order_failure");
  const auto triggered_same_time = simulation.find_signal("named_event_test.triggered_same_time");
  const auto triggered_expired = simulation.find_signal("named_event_test.triggered_expired");
  const auto procedural_alias_caught = simulation.find_signal("named_event_test.procedural_alias_caught");
  const auto declaration_alias_caught = simulation.find_signal("named_event_test.declaration_alias_caught");
  const auto null_triggered = simulation.find_signal("named_event_test.null_triggered");
  assert(
      event && observed && repeated
      && function_observed && task_observed
      && expression_observed && controlled && body_clock
      && repeat_count_calls);
  assert(
      blocking_missed && blocking_caught
      && nba_caught && zero_nba_caught && triple_caught
      && order_success && order_failure
      && triggered_same_time && triggered_expired
      && procedural_alias_caught && declaration_alias_caught
      && null_triggered);

  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  std::ostringstream vcd_text;
  fsim::runtime::VcdWriter vcd{vcd_text, "1ns", 32};
  const std::array signal_ids{
      *event, *observed, *repeated, *expression_observed,
      *controlled, *body_clock, *repeat_count_calls, *triple_caught};
  const std::array signal_names{
      "named_event_test.fired",
      "named_event_test.observed",
      "named_event_test.repeated",
      "named_event_test.expression_observed",
      "named_event_test.controlled",
      "named_event_test.body_clock",
      "named_event_test.repeat_count_calls",
      "named_event_test.triple_caught"};
  std::array<fsim::runtime::VcdSignal, signal_ids.size()> traces{};
  for (std::size_t index = 0; index < traces.size(); ++index) {
    traces[index] = vcd.declare_signal(
        signal_names[index],
        simulation.read_signal(signal_ids[index]).width());
  }
  vcd.begin(simulation.now());
  for (std::size_t index = 0; index < traces.size(); ++index) {
    vcd.change(
        traces[index], simulation.read_signal(signal_ids[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        for (std::size_t index = 0; index < signal_ids.size(); ++index) {
          if (signal_ids[index] == signal) {
            vcd.set_time(time);
            vcd.change(traces[index], value);
          }
        }
        auto* changes =
            signal == *event
            ? &capture.event_changes
            : signal == *observed
                ? &capture.observed_changes
                : signal == *triple_caught
                    ? &capture.triple_changes
                    : nullptr;
        if (changes != nullptr) {
          changes->push_back(
              Capture::Change{
                  time, delta, value.to_msb_string()});
        }
      });
  capture.result = simulation.run();
  capture.event = simulation.read_signal(*event).to_msb_string();
  capture.observed =
      simulation.read_signal(*observed).to_msb_string();
  capture.repeated =
      simulation.read_signal(*repeated).to_msb_string();
  capture.function_observed =
      simulation.read_signal(*function_observed).to_msb_string();
  capture.task_observed =
      simulation.read_signal(*task_observed).to_msb_string();
  capture.expression_observed =
      simulation.read_signal(*expression_observed).to_msb_string();
  capture.controlled =
      simulation.read_signal(*controlled).to_msb_string();
  capture.body_clock =
      simulation.read_signal(*body_clock).to_msb_string();
  capture.repeat_count_calls =
      simulation.read_signal(*repeat_count_calls).to_msb_string();
  capture.blocking_missed =
      simulation.read_signal(*blocking_missed).to_msb_string();
  capture.blocking_caught =
      simulation.read_signal(*blocking_caught).to_msb_string();
  capture.nba_caught =
      simulation.read_signal(*nba_caught).to_msb_string();
  capture.zero_nba_caught =
      simulation.read_signal(*zero_nba_caught).to_msb_string();
  capture.triple_caught =
      simulation.read_signal(*triple_caught).to_msb_string();
  capture.order_success = simulation.read_signal(*order_success).to_msb_string();
  capture.order_failure = simulation.read_signal(*order_failure).to_msb_string();
  capture.triggered_same_time = simulation.read_signal(*triggered_same_time).to_msb_string();
  capture.triggered_expired = simulation.read_signal(*triggered_expired).to_msb_string();
  capture.procedural_alias_caught = simulation.read_signal(*procedural_alias_caught).to_msb_string();
  capture.declaration_alias_caught = simulation.read_signal(*declaration_alias_caught).to_msb_string();
  capture.null_triggered = simulation.read_signal(*null_triggered).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_text.str();
  return capture;
}

void test_named_events(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "named-event-test";
  config.project.top = "sv:work.named_event_test";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(reference_project && compiled_project);

  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  for (const auto* capture : {&reference, &compiled}) {
    assert(
        capture->result.status
        == fsim::runtime::RunStatus::stopped);
    assert(capture->result.time == 4);
    assert(capture->event == "0");
    assert(capture->observed == "10");
    assert(capture->repeated == "111");
    assert(capture->function_observed == "1");
    assert(capture->task_observed == "1");
    assert(capture->expression_observed == "01");
    assert(capture->controlled == "11");
    assert(capture->body_clock == "0");
    assert(capture->repeat_count_calls == "01");
    assert(capture->blocking_missed == "0");
    assert(capture->blocking_caught == "1");
    assert(capture->nba_caught == "1");
    assert(capture->zero_nba_caught == "1");
    assert(capture->triple_caught == "1");
    assert(capture->order_success == "1");
    assert(capture->order_failure == "1");
    assert(capture->triggered_same_time == "1");
    assert(capture->triggered_expired == "1");
    assert(capture->procedural_alias_caught == "1");
    assert(capture->declaration_alias_caught == "1");
    assert(capture->null_triggered == "0");
    assert(capture->event_changes.size() == 2);
    assert(capture->event_changes[0].time == 1);
    assert(capture->event_changes[0].value == "1");
    assert(capture->event_changes[1].time == 3);
    assert(capture->event_changes[1].value == "0");
    assert(capture->observed_changes.size() == 3);
    assert(capture->observed_changes[0].time == 0);
    assert(capture->observed_changes[0].value == "00");
    assert(capture->observed_changes[1].time == 1);
    assert(capture->observed_changes[1].value == "01");
    assert(
        capture->observed_changes[1].delta
        > capture->event_changes[0].delta);
    assert(capture->observed_changes[2].time == 3);
    assert(capture->observed_changes[2].value == "10");
    assert(
        capture->observed_changes[2].delta
        > capture->event_changes[1].delta);
    assert(capture->triple_changes.size() == 1);
    assert(capture->triple_changes[0].time == 2);
    assert(capture->triple_changes[0].value == "1");
  }
  assert(reference.compiled_processes == 0);
  assert(reference.vcd == compiled.vcd);
  assert(reference.vcd.find("#4") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 28);
#else
  assert(compiled.compiled_processes == 0);
#endif
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-named-event-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "named_event_test.sv";
  {
    std::ofstream output(source);
    output << R"(
module named_event_test;
  event fired;
  logic [1:0] observed;
  logic [2:0] repeated;
  bit source_a;
  bit source_b;
  logic function_observed;
  logic task_observed;
  bit [1:0] expression_observed;
  bit [1:0] controlled;
  bit body_clock;
  bit [1:0] repeat_count_calls;
  event blocking_race_missed;
  event blocking_race_caught;
  event nba_race;
  event zero_nba_race;
  event triple_event;
  bit blocking_missed;
  bit blocking_caught;
  bit nba_caught;
  bit zero_nba_caught;
  bit triple_caught;
  event order_a;
  event order_b;
  event order_c;
  bit order_success;
  bit order_failure;
  event property_event;
  bit triggered_same_time;
  bit triggered_expired;
  event alias_source;
  event alias_target;
  event alias_decl = alias_source;
  bit procedural_alias_caught;
  bit declaration_alias_caught;
  event null_event;
  bit null_triggered;
  function automatic logic read_a_leaf;
    return source_a;
  endfunction
  function automatic logic read_a;
    logic nested = read_a_leaf();
    return nested;
  endfunction
  task automatic read_b_leaf;
    task_observed = source_b;
  endtask
  task automatic read_b;
    read_b_leaf();
  endtask
  function automatic logic [2:0] counted_repeat;
    repeat_count_calls = repeat_count_calls + 1'b1;
    return 3'd2;
  endfunction
  always_comb function_observed = read_a();
  always_comb read_b();
  always @(source_a | source_b)
    expression_observed = expression_observed + 1'b1;
  initial controlled <= repeat (2) @(fired) 2'b11;
  always #1 body_clock = ~body_clock;
  initial -> blocking_race_missed;
  initial begin
    @(blocking_race_missed);
    blocking_missed = 1'b1;
  end
  initial begin
    @(blocking_race_caught);
    blocking_caught = 1'b1;
  end
  initial -> blocking_race_caught;
  initial ->> nba_race;
  initial begin
    @(nba_race);
    nba_caught = 1'b1;
  end
  initial ->> #0 zero_nba_race;
  initial begin
    @(zero_nba_race);
    zero_nba_caught = 1'b1;
  end
  initial ->> #(1:2:3) triple_event;
  initial begin
    @(triple_event);
    triple_caught = 1'b1;
  end
  initial begin
    wait_order (order_a, order_b, order_a)
      order_success = 1'b1;
    else
      order_success = 1'b0;
  end
  initial begin
    wait_order (order_a, order_b, order_c)
      order_failure = 1'b0;
    else
      order_failure = 1'b1;
  end
  initial begin
    #1 -> order_a;
    #1 -> order_b;
    #1 -> order_a;
  end
  initial #1 -> property_event;
  initial begin
    @(property_event);
    #0 triggered_same_time = property_event.triggered;
    #1 triggered_expired = !property_event.triggered;
  end
  initial begin
    alias_target = alias_source;
    @(alias_target);
    procedural_alias_caught = 1'b1;
  end
  initial begin
    @(alias_decl);
    declaration_alias_caught = 1'b1;
  end
  initial #1 -> alias_source;
  initial begin
    null_event = null;
    -> null_event;
    #0 null_triggered = null_event.triggered;
  end
  initial begin
    #1 -> fired;
    #1 ->> #1 fired;
    #2 $finish;
  end
  initial begin
    observed = 2'b00;
    repeat (2) begin
      @(fired);
      observed = observed + 2'b01;
    end
  end
  initial begin
    integer runtime_lane;
    repeated = 3'd0;
    repeat (counted_repeat()) repeated = repeated + 1'b1;
    for (runtime_lane = 0; runtime_lane < 4; runtime_lane += 2)
      repeated = repeated + 1'b1;
    for (int local_lane = 0; local_lane < 4;
         local_lane = local_lane + 2) begin
      repeated = repeated + 1'b1;
      continue;
      repeated = 3'b111;
    end
    forever begin
      repeated = repeated + 1'b1;
      break;
    end
    #1 source_a = 1'b1;
    #1 source_b = 1'b1;
  end
endmodule
)";
  }

  test_named_events(
      directory.path, source, fsim::project::Optimization::o0);
  test_named_events(
      directory.path, source, fsim::project::Optimization::o2);
  std::cout << "named event application tests passed\n";
  return 0;
}
