// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error(std::string{message});
}

}  // namespace

void test_verilog_specify_blocks() {
  using namespace fsim::frontend;
  const auto parsed = parse_text(
      "specparams.v",
      R"(
module timed(input a, output z);
  specify
    specparam t_setup = 3, t_path = 1:2:3;
    specparam PATHPULSE$ = (4, 5);
    specparam PATHPULSE$a$z = 6;
  endspecify
endmodule
)",
      Language::Verilog2005);
  require(parsed.ok(), "specify block and specparams must parse");
  const auto& blocks = parsed.design.units.front().verilog_specify_blocks;
  require(
      blocks.size() == 1 && blocks.front().specparams.size() == 4,
      "specify declarations retain one source-spanned block");
  const auto& path = blocks.front().specparams[1];
  const auto& global_pulse = blocks.front().specparams[2];
  const auto& terminal_pulse = blocks.front().specparams[3];
  require(
      path.minimum && path.typical && path.maximum
          && path.value.text == "2" && path.minimum->text == "1"
          && path.maximum->text == "3" && global_pulse.path_pulse
          && global_pulse.value.text == "4"
          && global_pulse.path_pulse_error_limit
          && global_pulse.path_pulse_error_limit->text == "5"
          && terminal_pulse.path_pulse
          && terminal_pulse.path_pulse_input == "a"
          && terminal_pulse.path_pulse_output == "z"
          && !blocks.front().span.empty(),
      "specparam mintypmax and PATHPULSE metadata are canonical");

  const auto invalid = parse_text(
      "duplicate-specparams.v",
      R"(
module duplicate_specparams;
  specify
    specparam duplicated = 1, duplicated = 2;
  endspecify
  specify
    specparam duplicated = 3;
  endspecify
endmodule
)",
      Language::Verilog2005);
  require(
      std::ranges::any_of(
          invalid.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-165";
          }),
      "duplicate specparams must have a stable diagnostic");

  const auto invalid_optional = parse_text(
      "invalid-timing-optional.v",
      R"(
module invalid_timing_optional(input a);
  reg notifier;
  specify
    $width(posedge a, 2, , notifier);
  endspecify
endmodule
)",
      Language::Verilog2005);
  require(
      std::ranges::any_of(
          invalid_optional.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-166";
          }),
      "$width rejects a null threshold before a notifier");

  const auto excessive_delays = parse_text(
      "excessive-specify-delays.v",
      R"(
module excessive_specify_delays(input a, output z);
  specify
    (a => z) = (1, 2, 3, 4);
  endspecify
endmodule
)",
      Language::Verilog2005);
  require(
      !excessive_delays.ok(),
      "a nonstandard module-path delay arity rejects transactionally");

  const auto paths = parse_text(
      "module-paths.v",
      R"(
module module_paths(input a, b, data, enable, output z, q);
  specify
    (a => z) = 1;
    (a, b *> z, q) = (1, 2, 3, 4, 5, 6);
    (posedge a => (z +: data)) = (1:2:3, 4);
    if (enable) (negedge a *> (z -: data)) = 2;
    ifnone (a *> z) = 3;
    pulsestyle_onevent z;
    pulsestyle_ondetect q;
    showcancelled z, q;
    noshowcancelled q;
    $setup(posedge data &&& enable, posedge a, 1:2:3, notifier);
    $hold(posedge a, posedge data, 1, notifier);
    $setuphold(posedge a, posedge data, 1, 2, notifier,
               enable, enable, delayed_a, delayed_data);
    $recovery(posedge a, posedge data, 1, notifier);
    $removal(posedge a, posedge data, 1, notifier);
    $recrem(posedge a, posedge data, 1, 2, notifier,
            enable, enable, delayed_a, delayed_data);
    $skew(posedge a, posedge data, 1, notifier);
    $timeskew(posedge a, posedge data, 1, notifier, 1, 2);
    $fullskew(posedge a, posedge data, 1, 2, notifier, 1, 2);
    $period(edge [01, 10] a, 2, notifier);
    $width(negedge a, 2, 1, notifier);
    $nochange(posedge a, posedge data, 1, 2, notifier);
  endspecify
endmodule
)",
      Language::Verilog2005);
  require(paths.ok(), "parallel, full, edge, and conditional paths must parse");
  const auto& declarations =
      paths.design.units.front().verilog_specify_blocks.front().module_paths;
  require(
      declarations.size() == 5
          && declarations[0].kind == VerilogModulePathKind::Parallel
          && declarations[1].kind == VerilogModulePathKind::Full
          && declarations[1].sources.size() == 2
          && declarations[1].destinations.size() == 2
          && declarations[1].delays.size() == 6
          && declarations[2].source_edge == VerilogSpecifyEdge::Posedge
          && declarations[2].polarity == VerilogPathPolarity::Positive
          && declarations[2].destination_data_source.text == "data"
          && declarations[2].delays.front().minimum
          && declarations[2].delays.front().magnitude == 2
          && declarations[3].conditional
          && declarations[3].condition.text == "enable"
          && declarations[3].source_edge == VerilogSpecifyEdge::Negedge
          && declarations[3].polarity == VerilogPathPolarity::Negative
          && declarations[4].ifnone,
      "module-path HIR retains topology, conditions, transforms, and delays");
  const auto& pulses =
      paths.design.units.front().verilog_specify_blocks.front()
          .pulse_declarations;
  require(
      pulses.size() == 4 && pulses[0].controls_style
          && pulses[0].style == VerilogPulseStyle::Onevent
          && pulses[1].style == VerilogPulseStyle::Ondetect
          && !pulses[2].controls_style && pulses[2].show_cancelled
          && pulses[2].terminals.size() == 2
          && !pulses[3].show_cancelled,
      "pulse style and cancellation declarations retain terminal scope");
  const auto& checks =
      paths.design.units.front().verilog_specify_blocks.front().timing_checks;
  require(
      checks.size() == 12
          && checks[0].kind == VerilogTimingCheckKind::Setup
          && checks[0].data_event.expression.text == "data"
          && checks[0].data_event.condition.text == "enable"
          && checks[0].reference_event.expression.text == "a"
          && checks[0].limits.front().text == "2"
          && checks[0].normalized_limits.front().minimum
          && checks[0].normalized_limits.front().maximum
          && checks[2].kind == VerilogTimingCheckKind::SetupHold
          && checks[2].limits.size() == 2
          && checks[2].delayed_reference.text == "delayed_a"
          && checks[2].delayed_data.text == "delayed_data"
          && checks[7].event_based_flag.text == "1"
          && checks[7].remain_active_flag.text == "2"
          && checks[8].limits.size() == 2
          && checks[9].reference_event.edge == VerilogSpecifyEdge::Edge
          && checks[9].reference_event.edge_descriptors
              == std::vector<std::string>({"01", "10"})
          && checks[10].kind == VerilogTimingCheckKind::Width
          && checks[10].limits.size() == 1
          && checks[10].threshold.text == "1"
          && checks[10].notifier.text == "notifier"
          && checks[11].kind == VerilogTimingCheckKind::NoChange,
      "all Verilog timing checks retain event and optional-argument HIR");
}

}  // namespace fsim::tests::frontend
