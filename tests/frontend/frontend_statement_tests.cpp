// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[maybe_unused]] std::filesystem::path make_test_directory(
    std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-" + std::string{name} + "-"
         + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(
      output.good(),
      "frontend test fixture must be writable");
}

} // namespace

void test_wildcard_and_always_comb_processes() {
  const auto packed_events = parse_text(
      "packed_events.sv",
      R"(
module packed_events;
  logic left;
  logic right;
  logic observed;
  always @(left | right) observed = left;
  initial @(left ^ right) observed = right;
endmodule
)",
      Language::SystemVerilog2017);
  require(packed_events.ok(), "packed event expressions must parse");
  const auto& packed_processes =
      packed_events.design.units.front().processes;
  require(
      packed_processes.size() == 2
          && packed_processes[0].sensitivities.size() == 1
          && packed_processes[0].sensitivities.front()
                 .expression.kind == ExpressionKind::Binary
          && packed_processes[0].sensitivities.front()
                 .expression.text == "|"
          && packed_processes[1].statements.front()
                 .sensitivities.front().expression.text == "^",
      "packed event-expression HIR");

  const auto generalized_packed_events = parse_text(
      "generalized_packed_events.sv",
      R"(
module invalid_packed_events;
  logic left;
  logic right;
  always @(posedge (left | right));
  always @(left | right or left);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      generalized_packed_events.ok(),
      "edge-qualified and mixed packed event expressions must parse");
  const auto& generalized =
      generalized_packed_events.design.units.front().processes;
  require(
      generalized.size() == 2
          && generalized[0].sensitivities.front().edge
              == EdgeKind::Positive
          && generalized[0].sensitivities.front().expression.valid()
          && generalized[1].sensitivities.size() == 2
          && generalized[1].sensitivities.front().expression.valid()
          && generalized[1].sensitivities.back().signal == "left",
      "generalized packed event-expression HIR");

  const auto result = parse_text(
      "combinational.sv",
      R"(
module combinational;
  logic a;
  logic q;
  logic y;
  logic latch_q;
  always @* q = a;
  always_comb y = ~q;
  always_latch if (a) latch_q = q;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "wildcard always and bounded always_comb must parse");
  const auto& processes = result.design.units.front().processes;
  require(
      processes.size() == 3
          && processes[0].kind == ProcessKind::VerilogAlways
          && processes[0].sensitivities.size() == 1
          && processes[0].sensitivities.front().signal == "*"
          && processes[1].kind
              == ProcessKind::SystemVerilogAlwaysComb
          && processes[1].sensitivities.size() == 1
          && processes[1].sensitivities.front().signal == "*"
          && processes[2].kind
              == ProcessKind::SystemVerilogAlwaysLatch
          && processes[2].sensitivities.size() == 1
          && processes[2].sensitivities.front().signal == "*",
      "wildcard process metadata");

  const auto body_timed = parse_text(
      "body_timed_always.sv",
      R"(
module body_timed_always;
  logic clock;
  always #1 clock = ~clock;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      body_timed.ok()
          && body_timed.design.units.front().processes.front()
                 .sensitivities.empty()
          && body_timed.design.units.front().processes.front()
                 .statements.front().kind == StatementKind::Delay,
      "body-timed always retains its suspension control");

  const auto nonprogressing_always = parse_text(
      "nonprogressing_always.sv",
      R"(
module nonprogressing_always;
  logic clock;
  always clock = ~clock;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !nonprogressing_always.ok()
          && std::ranges::any_of(
              nonprogressing_always.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-106";
              }),
      "nonprogressing body-timed always diagnostic");

  const auto invalid_system_verilog = parse_text(
      "bad_comb.sv",
      R"(
module bad_comb;
  logic a;
  logic q;
  always_comb @(a) q = a;
  always_comb #1 q = a;
  always_comb q <= a;
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid_system_verilog.ok(), "invalid always_comb forms");
  for (const auto code : {
           std::string_view{"FSIM-SV-SEM-011"},
           std::string_view{"FSIM-SV-SEM-012"},
           std::string_view{"FSIM-SV-SEM-013"}}) {
    require(
        std::any_of(
            invalid_system_verilog.diagnostics.begin(),
            invalid_system_verilog.diagnostics.end(),
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted always_comb diagnostic");
  }

  const auto invalid_always_ff = parse_text(
      "bad_always_ff.sv",
      R"(
module bad_always_ff;
  logic clk;
  logic reset;
  logic q;
  always_ff @(clk) q <= 1'b0;
  always_ff @(posedge clk or negedge reset) q <= 1'b0;
  always_ff @(posedge clk) #1 q <= 1'b0;
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid_always_ff.ok(), "invalid always_ff forms");
  for (const auto code : {
           std::string_view{"FSIM-SV-SEM-101"},
           std::string_view{"FSIM-SV-SEM-102"}}) {
    require(
        std::any_of(
            invalid_always_ff.diagnostics.begin(),
            invalid_always_ff.diagnostics.end(),
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted always_ff diagnostic");
  }

  const auto invalid_verilog = parse_text(
      "bad_comb.v",
      R"(
module bad_comb;
  reg q;
  always_comb q = 1'b0;
  always_latch q = 1'b0;
endmodule
)",
      Language::Verilog2005);
  require(
      !invalid_verilog.ok()
          && std::any_of(
              invalid_verilog.diagnostics.begin(),
              invalid_verilog.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-002";
              })
          && std::any_of(
              invalid_verilog.diagnostics.begin(),
              invalid_verilog.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-003";
              }),
      "always_comb/always_latch language-version diagnostics");
}

void test_systemverilog_case_statements() {
  const auto result = parse_text(
      "case_statement.sv",
      R"(
module case_statement;
  logic [1:0] selector;
  logic [1:0] result;
  always_comb case (selector)
    2'b00: result = 2'b01;
    2'b01, 2'b10: begin
      result = 2'b10;
    end
    default: result = 2'b11;
  endcase
  initial casez (selector)
    2'b0?: result = 2'b01;
    default: result = 2'b00;
  endcase
  initial casex (selector)
    2'b0x: result = 2'b10;
    default: result = 2'b00;
  endcase
  initial case (selector) inside
    2'b00, [2'b01:2'b10]: result = 2'b01;
    2'b1x: result = 2'b10;
    default: result = 2'b11;
  endcase
  initial unique case (selector)
    2'b00: result = 2'b01;
    default: result = 2'b11;
  endcase
  initial unique0 casez (selector)
    2'b0?: result = 2'b01;
  endcase
  initial priority case (selector) inside
    [2'b00:2'b10]: result = 2'b10;
  endcase
  initial unique case (selector) matches
    2'b00: result = 2'b01;
    .*: result = 2'b10;
    default: result = 2'b11;
  endcase
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "exact SystemVerilog case statement must parse");
  const auto& statement =
      result.design.units.front().processes.front().statements.front();
  require(
      statement.kind == StatementKind::Case
          && statement.condition.kind == ExpressionKind::Identifier
          && statement.condition.text == "selector"
          && statement.case_alternatives.size() == 3,
      "case selector and ordered alternatives");
  require(
      statement.case_alternatives[0].choices.size() == 1
          && statement.case_alternatives[1].choices.size() == 2
          && statement.case_alternatives[1].statements.size() == 1
          && statement.case_alternatives[1].statements.front().kind
              == StatementKind::Block
          && statement.case_alternatives[2].is_default
          && statement.case_alternatives[2].choices.empty(),
      "case choices, block body, and default metadata");
  require(
      statement.case_match_kind == CaseMatchKind::Exact
          && result.design.units.front().processes[1]
                     .statements.front().case_match_kind
                 == CaseMatchKind::WildcardZ
          && result.design.units.front().processes[2]
                     .statements.front().case_match_kind
                 == CaseMatchKind::WildcardXZ
          && result.design.units.front().processes[3]
                     .statements.front().case_match_kind
                 == CaseMatchKind::Inside,
      "exact, casez, casex, and inside modes remain distinct in HIR");
  const auto& inside = result.design.units.front().processes[3]
                           .statements.front();
  require(
      inside.case_alternatives.size() == 3
          && inside.case_alternatives[0].choices.size() == 2
          && inside.case_alternatives[0].choices[1].kind
              == ExpressionKind::Call
          && inside.case_alternatives[0].choices[1].text
              == "@inside-range"
          && inside.case_alternatives[0].choices[1].operands.size() == 2
          && inside.case_alternatives[0].choices[1].operands[0].text
              == "2'b01"
          && inside.case_alternatives[0].choices[1].operands[1].text
              == "2'b10"
          && inside.case_alternatives[2].is_default
          && !inside.span.empty(),
      "case inside retains ordered values, explicit ranges, and source span");
  require(
      result.design.units.front().processes[4]
                 .statements.front().case_qualifier
              == CaseQualifier::Unique
          && result.design.units.front().processes[5]
                 .statements.front().case_qualifier
              == CaseQualifier::Unique0
          && result.design.units.front().processes[6]
                 .statements.front().case_qualifier
              == CaseQualifier::Priority
          && result.design.units.front().processes[6]
                 .statements.front().case_match_kind
              == CaseMatchKind::Inside
          && result.design.units.front().processes[7]
                 .statements.front().case_qualifier
              == CaseQualifier::Unique
          && result.design.units.front().processes[7]
                 .statements.front().case_match_kind
              == CaseMatchKind::Matches
          && result.design.units.front().processes[4]
                 .statements.front().span.begin.column == 11,
      "case qualifier kind and source span remain distinct from matching mode");
  const auto& matches = result.design.units.front().processes[7]
                            .statements.front();
  require(
      matches.case_alternatives.size() == 3
          && matches.case_alternatives[0].choices.size() == 1
          && matches.case_alternatives[0].choices.front().text == "2'b00"
          && matches.case_alternatives[1].choices.size() == 1
          && matches.case_alternatives[1].choices.front().kind
              == ExpressionKind::Call
          && matches.case_alternatives[1].choices.front().text
              == "@match-wildcard"
          && matches.case_alternatives[2].is_default
          && !matches.span.empty(),
      "case matches retains one constant or wildcard pattern per item");

  const auto invalid = parse_text(
      "bad_case.sv",
      R"(
module bad_case;
  logic selector;
  logic result;
  always_comb case (selector)
    default: result = 1'b0;
    default: result = 1'b1;
  endcase
  initial casex (selector)
    1'bx: result = 1'b0;
  endcase
  initial unique case (selector)
    1'b0: result = 1'b0;
  endcase
  initial unique0 case (selector)
    1'b0: result = 1'b0;
  endcase
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "unsupported and duplicate case forms must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-SEM-014"}}) {
    require(
        std::any_of(
            invalid.diagnostics.begin(),
            invalid.diagnostics.end(),
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted case diagnostic");
  }
  require(
      invalid.design.units.front().processes.size() == 4,
      "case diagnostics must recover to following processes");

  const auto invalid_qualifiers = parse_text(
      "bad_case_qualifiers.sv",
      R"(
module bad_case_qualifiers;
  logic selector;
  logic result;
  initial unique priority case (selector)
    1'b0: result = 1'b0;
  endcase
  initial unique if (selector) result = 1'b1;
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid_qualifiers.ok(), "invalid case qualifiers must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-PARSE-186"},
           std::string_view{"FSIM-SV-UNSUPPORTED-017"}}) {
    require(
        std::ranges::any_of(
            invalid_qualifiers.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "duplicate and misplaced case qualifier diagnostics");
  }

  const auto verilog_qualifier = parse_text(
      "bad_case_qualifier.v",
      "module bad_case_qualifier; reg a; initial unique case (a) "
      "1'b0: a = 1'b1; endcase endmodule",
      Language::Verilog2005);
  require(
      !verilog_qualifier.ok()
          && std::ranges::any_of(
              verilog_qualifier.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-185";
              }),
      "case qualifiers require SystemVerilog");

  const auto malformed_inside = parse_text(
      "bad_case_inside.sv",
      R"(
module bad_case_inside;
  logic [1:0] selector;
  logic result;
  initial case (selector) inside
    [2'b00 2'b01]: result = 1'b0;
  endcase
  initial case (selector) inside
    [2'b00:2'b01: result = 1'b0;
  endcase
  initial case (selector) inside
    : result = 1'b0;
  endcase
  initial casez (selector) inside
    2'b0?: result = 1'b0;
  endcase
endmodule
)",
      Language::SystemVerilog2017);
  require(!malformed_inside.ok(), "malformed case inside forms must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-PARSE-181"},
           std::string_view{"FSIM-SV-PARSE-182"},
           std::string_view{"FSIM-SV-PARSE-183"},
           std::string_view{"FSIM-SV-PARSE-184"}}) {
    require(
        std::ranges::any_of(
            malformed_inside.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "case inside malformed and deferred-form diagnostics");
  }

  const auto invalid_matches = parse_text(
      "bad_case_matches.sv",
      R"(
module bad_case_matches;
  logic [1:0] selector;
  logic result;
  initial casez (selector) matches
    2'b00: result = 1'b0;
  endcase
  initial case (selector) matches
    .captured: result = 1'b0;
    tagged Some: result = 1'b0;
    '{2'b00}: result = 1'b0;
    2'b00 &&& selector: result = 1'b0;
    2'b00, 2'b01: result = 1'b0;
    .: result = 1'b0;
  endcase
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid_matches.ok(), "deferred case patterns must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-PARSE-188"},
           std::string_view{"FSIM-SV-PARSE-189"},
           std::string_view{"FSIM-SV-PARSE-190"},
           std::string_view{"FSIM-SV-UNSUPPORTED-042"},
           std::string_view{"FSIM-SV-UNSUPPORTED-043"}}) {
    require(
        std::ranges::any_of(
            invalid_matches.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "case matches deferred-pattern diagnostics");
  }
  require(
      invalid_matches.design.units.front().processes.size() == 2,
      "case matches diagnostics recover through following items");

  const auto verilog_matches = parse_text(
      "bad_case_matches.v",
      "module bad_case_matches; reg a; initial case (a) matches "
      "1'b0: a = 1'b1; endcase endmodule",
      Language::Verilog2005);
  require(
      !verilog_matches.ok()
          && std::ranges::any_of(
              verilog_matches.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-187";
              }),
      "case matches requires SystemVerilog");

  const auto verilog_inside = parse_text(
      "bad_case_inside.v",
      "module bad_case_inside; reg a; initial case (a) inside "
      "1'b0: a = 1'b1; endcase endmodule",
      Language::Verilog2005);
  require(
      !verilog_inside.ok()
          && std::ranges::any_of(
              verilog_inside.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-180";
              }),
      "case inside requires SystemVerilog");
}

void test_systemverilog_procedural_for_loops() {
  const auto result = parse_text(
      "procedural_for.sv",
      R"(
module procedural_for;
  logic [3:0] result;
  initial begin
    integer runtime_lane;
    result = 4'b0000;
    for (int lane = 0; lane < 4; lane++)
      result[lane] = 1'b1;
    for (integer lane = 3; lane >= 2; --lane) begin
      result[lane] = 1'b0;
    end
    for (int lane = 2; lane < 1; lane += 1)
      result[0] = 1'b0;
    for (int lane = 0; lane <= 0; lane = lane + 1)
      result[0] = result[0];
    for (int lane = 0; lane > 0; lane -= 1)
      result[0] = 1'b0;
    for (runtime_lane = 0; runtime_lane < 4; runtime_lane += 2)
      result[runtime_lane] = 1'b1;
    for (int lane = 0; lane < 4; lane = lane + 2)
      result[lane] = result[lane];
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "bounded SystemVerilog procedural for loops must parse");
  require(
      result.design.units.front().signals.size() == 1
          && result.design.units.front().signals.front().name
              == "result",
      "inline procedural loop indices must not become implicit nets");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 8
          && statements[1].kind == StatementKind::Loop
          && statements[1].loop_variable == "lane"
          && statements[1].loop_initial.text == "0"
          && statements[1].loop_limit.text == "4"
          && !statements[1].loop_descending
          && statements[1].loop_limit_exclusive
          && statements[2].kind == StatementKind::Loop
          && statements[2].loop_descending
          && !statements[2].loop_limit_exclusive
          && statements[2].statements.size() == 1
          && statements[3].kind == StatementKind::Loop
          && statements[4].kind == StatementKind::Loop
          && !statements[4].loop_limit_exclusive
          && statements[5].kind == StatementKind::Loop
          && statements[5].loop_descending
          && statements[5].loop_limit_exclusive
          && statements[6].loop_runtime
          && !statements[6].loop_variable_declared
          && statements[6].value.text == "+"
          && statements[7].loop_runtime
          && statements[7].loop_variable_declared
          && statements[7].value.text == "+",
      "SystemVerilog loop range normalization and bodies");

  const auto invalid = parse_text(
      "bad_procedural_for.sv",
      R"(
module bad_procedural_for;
  initial begin
    for (int lane = 0; other < 4; lane++);
    for (int lane = 0; lane < 4; other++);
    for (int lane = 0; lane < 4; lane--);
    for (int lane = 0; lane < 4; lane);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "noncanonical procedural loops must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-SEM-027"},
           std::string_view{"FSIM-SV-SEM-028"},
           std::string_view{"FSIM-SV-SEM-029"},
           std::string_view{"FSIM-SV-SEM-103"}}) {
    require(
        std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted bounded procedural loop diagnostic");
  }
}

void test_verilog_repeat_statements() {
  const auto systemverilog = parse_text(
      "repeat_statement.sv",
      R"(
module repeat_statement;
  logic [1:0] result;
  initial begin
    result = 2'b00;
    repeat (3) result = result + 1;
    repeat (0) result = 2'b11;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog repeat statements must parse");
  const auto& statements =
      systemverilog.design.units.front()
          .processes.front()
          .statements;
  require(
      statements.size() == 3
          && statements[1].kind == StatementKind::Loop
          && statements[1].loop_repeat
          && statements[1].loop_limit_exclusive
          && statements[1].loop_initial.text == "0"
          && statements[1].loop_limit.text == "3"
          && statements[1].loop_variable.empty()
          && statements[1].statements.size() == 1
          && statements[2].kind == StatementKind::Loop
          && statements[2].loop_repeat
          && statements[2].loop_limit.text == "0",
      "repeat count and body HIR");

  const auto verilog = parse_text(
      "repeat_statement.v",
      R"(
module repeat_statement;
  reg result;
  initial begin
    result = 1'b0;
    repeat (2) result = ~result;
  end
endmodule
)",
      Language::Verilog2005);
  require(
      verilog.ok()
          && verilog.design.units.front()
                 .processes.front()
                 .statements[1]
                 .loop_repeat,
      "Verilog-2005 repeat statements share the loop HIR");

  const auto malformed = parse_text(
      "bad_repeat.sv",
      R"(
module bad_repeat;
  initial repeat 2;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !malformed.ok()
          && std::ranges::any_of(
              malformed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-100";
              })
          && std::ranges::any_of(
              malformed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-101";
              }),
      "malformed repeat delimiters receive stable diagnostics");
}

void test_runtime_loop_statements() {
  const auto systemverilog = parse_text(
      "runtime_loops.sv",
      R"(
module runtime_loops;
  logic flag;
  logic [2:0] count;
  initial begin
    count = 3'b000;
    while (count < 3) count = count + 1;
    forever begin
      count = count + 1;
      break;
    end
    forever if (flag) break; else @(flag);
    forever #1 flag = ~flag;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog while and timed forever loops must parse");
  const auto& statements =
      systemverilog.design.units.front()
          .processes.front()
          .statements;
  require(
      statements.size() == 5
          && statements[1].kind == StatementKind::Loop
          && statements[1].loop_runtime
          && statements[1].condition.kind
              == ExpressionKind::Binary
          && statements[1].condition.text == "<"
          && statements[4].kind == StatementKind::Loop
          && statements[4].loop_runtime
          && statements[4].condition.kind
              == ExpressionKind::LogicLiteral
          && statements[4].statements.size() == 1
          && statements[4].statements.front().kind
              == StatementKind::Delay,
      "runtime loop conditions and suspension body HIR");

  const auto verilog = parse_text(
      "runtime_loops.v",
      R"(
module runtime_loops;
  reg flag;
  initial begin
    flag = 1'b1;
    while (flag) flag = 1'b0;
    forever #1 flag = ~flag;
  end
endmodule
)",
      Language::Verilog2005);
  require(
      verilog.ok()
          && verilog.design.units.front()
                 .processes.front()
                 .statements[1]
                 .loop_runtime
          && verilog.design.units.front()
                 .processes.front()
                 .statements[2]
                 .loop_runtime,
      "Verilog-2005 runtime loops share the common HIR");

  const auto invalid = parse_text(
      "bad_runtime_loops.sv",
      R"(
module bad_runtime_loops;
  logic flag;
  initial while flag;
  initial forever flag = ~flag;
  initial forever if (flag) #1; else flag = ~flag;
  initial forever continue;
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "malformed or nonsuspending loops must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-PARSE-102"},
           std::string_view{"FSIM-SV-PARSE-103"},
           std::string_view{"FSIM-SV-SEM-030"}}) {
    require(
        std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted runtime-loop diagnostic");
  }
}

void test_loop_control_statements() {
  const auto systemverilog = parse_text(
      "loop_control.sv",
      R"(
module loop_control;
  logic flag;
  initial begin
    for (int outer = 0; outer < 2; outer++) begin
      continue;
      while (flag) begin
        break;
      end
      break;
    end
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "nested SystemVerilog break and continue statements must parse");
  const auto& sv_loop =
      systemverilog.design.units.front()
          .processes.front()
          .statements.front();
  require(
      sv_loop.kind == StatementKind::Loop
          && sv_loop.statements.size() == 3
          && sv_loop.statements[0].kind
              == StatementKind::Continue
          && sv_loop.statements[1].kind == StatementKind::Loop
          && sv_loop.statements[1].statements.size() == 1
          && sv_loop.statements[1].statements[0].kind
              == StatementKind::Break
          && sv_loop.statements[2].kind
              == StatementKind::Break,
      "SystemVerilog loop controls retain their nested HIR scopes");

  const auto invalid_systemverilog = parse_text(
      "invalid_loop_control.sv",
      R"(
module invalid_loop_control;
  initial begin
    break;
    continue
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_systemverilog.ok()
          && std::ranges::any_of(
              invalid_systemverilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-031";
              })
          && std::ranges::any_of(
              invalid_systemverilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-104";
              }),
      "out-of-loop and malformed SystemVerilog controls are diagnosed");

  const auto vhdl = parse_text(
      "loop_control.vhd",
      R"(
entity loop_control is
end entity;
architecture rtl of loop_control is
begin
  exercise: process
  begin
    outer_loop: for outer in 0 to 1 loop
      next outer_loop when outer = 0;
      inner_loop: while true loop
        exit inner_loop;
      end loop inner_loop;
      exit outer_loop when outer = 1;
    end loop outer_loop;
    plain_loop: loop
      next when false;
      exit plain_loop;
    end loop plain_loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      vhdl.ok(),
      "nested VHDL exit and next statements must parse");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  const auto& vhdl_loop =
      architecture->processes.front().statements.front();
  const auto& unconditional_vhdl_loop =
      architecture->processes.front().statements[1];
  require(
      vhdl_loop.kind == StatementKind::Loop
          && vhdl_loop.loop_label == "outer_loop"
          && vhdl_loop.statements.size() == 3
          && vhdl_loop.statements[0].kind == StatementKind::If
          && vhdl_loop.statements[0].statements.front().kind
              == StatementKind::Continue
          && vhdl_loop.statements[0].statements.front()
                 .loop_control_label
              == "outer_loop"
          && vhdl_loop.statements[1].kind == StatementKind::Loop
          && vhdl_loop.statements[1].loop_label == "inner_loop"
          && vhdl_loop.statements[1].statements.front().kind
              == StatementKind::Break
          && vhdl_loop.statements[1].statements.front()
                 .loop_control_label
              == "inner_loop"
          && vhdl_loop.statements[2].kind == StatementKind::If
          && vhdl_loop.statements[2].statements.front().kind
              == StatementKind::Break
          && unconditional_vhdl_loop.kind == StatementKind::Loop
          && unconditional_vhdl_loop.loop_label == "plain_loop"
          && unconditional_vhdl_loop.loop_runtime
          && unconditional_vhdl_loop.condition.kind
              == ExpressionKind::BooleanLiteral
          && unconditional_vhdl_loop.condition.text == "true"
          && unconditional_vhdl_loop.statements.size() == 2,
      "conditional controls and unconditional VHDL loops retain HIR");

  const auto invalid_vhdl = parse_text(
      "invalid_loop_control.vhd",
      R"(
architecture rtl of invalid_loop_control is
begin
  exercise: process
  begin
    exit;
    for lane in 0 to 1 loop
      next missing_loop when lane = 0
    end loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl.ok()
          && std::ranges::any_of(
              invalid_vhdl.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-023";
              })
          && std::ranges::any_of(
              invalid_vhdl.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-024";
              })
          && std::ranges::any_of(
              invalid_vhdl.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-110";
              }),
      "out-of-loop, unknown-target, and malformed controls are diagnosed");

  const auto invalid_labels = parse_text(
      "invalid_loop_labels.vhd",
      R"(
architecture rtl of invalid_loop_labels is
begin
  exercise: process
  begin
    outer_loop: loop
      outer_loop: loop
        exit;
      end loop wrong_loop;
    end loop outer_loop;
    sibling_loop: loop
      exit;
    end loop sibling_loop;
    sibling_loop: loop
      exit;
    end loop sibling_loop;
    loop
      exit;
    end loop orphan_label;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_labels.ok()
          && std::ranges::any_of(
              invalid_labels.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-025";
              })
          && std::ranges::any_of(
              invalid_labels.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-026";
              }),
      "mismatched, orphan, and duplicate VHDL loop labels are diagnosed");

  const auto malformed_vhdl_loop = parse_text(
      "malformed_unconditional_loop.vhd",
      R"(
architecture rtl of malformed_unconditional_loop is
begin
  exercise: process
  begin
    loop
      exit;
    end loop
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !malformed_vhdl_loop.ok()
          && std::ranges::any_of(
              malformed_vhdl_loop.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-113";
              }),
      "malformed unconditional VHDL loops receive a stable diagnostic");
}

void test_systemverilog_do_while_statements() {
  const auto result = parse_text(
      "do_while.sv",
      R"(
module do_while;
  logic [2:0] count;
  initial begin
    do begin
      count = count + 1;
      if (count == 1) continue;
      if (count == 2) break;
    end while (count < 3);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "SystemVerilog do-while statements must parse");
  const auto& statement =
      result.design.units.front()
          .processes.front()
          .statements.front();
  require(
      statement.kind == StatementKind::Loop
          && statement.loop_runtime
          && statement.loop_post_test
          && statement.condition.kind == ExpressionKind::Binary
          && statement.condition.text == "<"
          && statement.statements.size() == 3
          && statement.statements[1].statements.front().kind
              == StatementKind::Continue
          && statement.statements[2].statements.front().kind
              == StatementKind::Break,
      "do-while retains its post-test condition and loop controls");

  const auto malformed = parse_text(
      "malformed_do_while.sv",
      R"(
module malformed_do_while;
  initial do ; (1'b0)
  initial do ; while 1'b0;
endmodule
)",
      Language::SystemVerilog2017);
  require(!malformed.ok(), "malformed do-while statements must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-PARSE-105"},
           std::string_view{"FSIM-SV-PARSE-106"},
           std::string_view{"FSIM-SV-PARSE-107"},
           std::string_view{"FSIM-SV-PARSE-108"}}) {
    require(
        std::ranges::any_of(
            malformed.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted SystemVerilog do-while diagnostic");
  }
}

void test_fork_process_statements() {
  const auto parsed = parse_text(
      "fork_processes.sv",
      R"(
module fork_processes;
  logic result;
  process handle;
  initial begin
    fork : workers
      logic local_value = 1'b0;
      begin
        #1 local_value = 1'b1;
        result = local_value;
      end
      #2 result = 1'b0;
    join : workers
    fork
      #1 result = 1'b1;
      #2 result = 1'b0;
    join_any
    wait fork;
    fork
      #1 result = 1'b1;
    join_none
    disable fork;
    handle = process::self();
    result = handle.status();
    result = handle.completed();
    handle.await();
    handle.kill();
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "fork process controls must parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      parsed.design.units.front().signals.size() == 2
          && parsed.design.units.front().signals[1].type.spelling
              == "process"
          && statements.size() == 10
          && statements[0].kind == StatementKind::Fork
          && statements[0].label == "workers"
          && statements[0].declarations.size() == 1
          && statements[0].statements.size() == 2
          && statements[0].fork_join_kind == ForkJoinKind::All
          && statements[1].kind == StatementKind::Fork
          && statements[1].fork_join_kind == ForkJoinKind::Any
          && statements[2].kind == StatementKind::WaitFork
          && statements[3].kind == StatementKind::Fork
          && statements[3].fork_join_kind == ForkJoinKind::None
          && statements[4].kind == StatementKind::DisableFork
          && statements[5].kind == StatementKind::Assignment
          && statements[6].kind == StatementKind::Assignment
          && statements[7].kind == StatementKind::Assignment
          && statements[8].kind == StatementKind::TaskCall
          && statements[9].kind == StatementKind::TaskCall,
      "fork and process-handle HIR retains typed controls and calls");

  const auto verilog_join_any = parse_text(
      "fork_join_any.v",
      R"(
module fork_join_any;
  initial fork ; join_any
endmodule
)",
      Language::Verilog2005);
  require(
      std::ranges::any_of(
          verilog_join_any.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-108";
          }),
      "join_any requires SystemVerilog");

  const auto invalid_labels = parse_text(
      "fork_labels.sv",
      R"(
module fork_labels;
  initial fork : left ; join : right
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          invalid_labels.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-109";
          }),
      "fork closing labels must match");

  const auto invalid_disable = parse_text(
      "disable_name.sv",
      R"(
module disable_name;
  initial disable named_block;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          invalid_disable.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-PARSE-208";
          }),
      "bounded disable control must select fork");
}

void test_systemverilog_conditional_expression() {
  const auto result = parse_text(
      "conditional.sv",
      R"(
module conditional;
  logic select;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [3:0] result;
  always_comb result = select ? lhs : rhs;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(), "SystemVerilog conditional expression must parse");
  const auto& value =
      result.design.units.front().processes.front()
          .statements.front().value;
  require(
      value.kind == ExpressionKind::Call
          && value.text == "?:"
          && value.operands.size() == 3
          && value.operands[0].text == "select"
          && value.operands[1].text == "lhs"
          && value.operands[2].text == "rhs",
      "conditional-expression operand order");
}

void test_systemverilog_comparison_expressions() {
  const auto result = parse_text(
      "comparisons.sv",
      R"(
module comparisons;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [2:0] amount;
  logic [3:0] shifted;
  logic result;
  always_comb begin
    result = !lhs;
    result = lhs != rhs;
    result = lhs === rhs;
    result = lhs !== rhs;
    result = lhs ==? rhs;
    result = lhs !=? rhs;
    result = lhs < rhs;
    result = lhs <= rhs;
    result = lhs > rhs;
    result = lhs >= rhs;
    result = lhs && result;
    result = result || rhs;
    result = &lhs;
    result = |lhs;
    result = ^lhs;
    result = ~&lhs;
    result = ~|lhs;
    result = ~^lhs;
    result = ^~lhs;
    shifted = lhs ~^ rhs;
    shifted = lhs ^~ rhs;
    shifted = lhs << amount;
    shifted = lhs >> amount;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(), "SystemVerilog comparison expressions must parse");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 23
          && statements[0].value.kind == ExpressionKind::Unary
          && statements[0].value.text == "!",
      "logical-negation expression node");
  const std::array<std::string_view, 9> operators{
      "!=", "===", "!==", "==?", "!=?", "<", "<=", ">", ">="};
  for (std::size_t index = 0; index < operators.size(); ++index) {
    require(
        statements[index + 1].value.kind
                == ExpressionKind::Binary
            && statements[index + 1].value.text
                == operators[index],
        "comparison expression node");
  }
  require(
      statements[10].value.kind == ExpressionKind::Binary
          && statements[10].value.text == "&&"
          && statements[11].value.kind == ExpressionKind::Binary
          && statements[11].value.text == "||",
      "logical binary expression nodes");
  require(
      statements[12].value.kind == ExpressionKind::Unary
          && statements[12].value.text == "&"
          && statements[13].value.kind == ExpressionKind::Unary
          && statements[13].value.text == "|"
          && statements[14].value.kind == ExpressionKind::Unary
          && statements[14].value.text == "^",
      "reduction expression nodes");
  require(
      statements[15].value.kind == ExpressionKind::Unary
          && statements[15].value.text == "~&"
          && statements[16].value.kind == ExpressionKind::Unary
          && statements[16].value.text == "~|"
          && statements[17].value.kind == ExpressionKind::Unary
          && statements[17].value.text == "~^"
          && statements[18].value.kind == ExpressionKind::Unary
          && statements[18].value.text == "^~",
      "complemented reduction expression nodes");
  require(
      statements[19].value.kind == ExpressionKind::Binary
          && statements[19].value.text == "~^"
          && statements[20].value.kind == ExpressionKind::Binary
          && statements[20].value.text == "^~",
      "binary XNOR expression nodes");
  require(
      statements[21].value.kind == ExpressionKind::Binary
          && statements[21].value.text == "<<"
          && statements[22].value.kind == ExpressionKind::Binary
          && statements[22].value.text == ">>",
      "logical-shift expression nodes");

  const auto verilog = parse_text(
      "wildcard_equality.v",
      R"(
module wildcard_equality;
  reg [3:0] lhs;
  reg [3:0] rhs;
  reg result;
  always @* result = lhs ==? rhs;
endmodule
)",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && std::any_of(
              verilog.diagnostics.begin(),
              verilog.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-004";
              }),
      "wildcard equality requires SystemVerilog");
}

void test_systemverilog_membership_expressions() {
  const auto result = parse_text(
      "membership.sv",
      R"(
module membership;
  logic [7:0] selector;
  logic result;
  always_comb begin
    result = selector inside {8'h01, [8'h10:8'h1f], 8'b10xz_0101};
    result = selector + 8'h01 inside {[8'h20:8'h2f]} && result;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "SystemVerilog inside expressions must parse");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  const auto& membership = statements.front().value;
  require(
      statements.size() == 2
          && membership.kind == ExpressionKind::Call
          && membership.text == "inside"
          && membership.operands.size() == 4
          && membership.operands[0].text == "selector"
          && membership.operands[1].text == "8'h01"
          && membership.operands[2].kind == ExpressionKind::Call
          && membership.operands[2].text == "@inside-range"
          && membership.operands[2].operands.size() == 2
          && membership.operands[2].operands[0].text == "8'h10"
          && membership.operands[2].operands[1].text == "8'h1f"
          && membership.operands[3].text == "8'b10xz_0101"
          && !membership.span.empty(),
      "inside HIR retains ordered values, explicit ranges, and source span");
  const auto& combined = statements[1].value;
  require(
      combined.kind == ExpressionKind::Binary
          && combined.text == "&&"
          && combined.operands[0].kind == ExpressionKind::Call
          && combined.operands[0].text == "inside"
          && combined.operands[0].operands[0].kind
              == ExpressionKind::Binary
          && combined.operands[0].operands[0].text == "+",
      "inside precedence retains arithmetic lhs before logical conjunction");

  const auto empty = parse_text(
      "empty_membership.sv",
      "module empty_membership; logic a; initial a = a inside {}; endmodule",
      Language::SystemVerilog2017);
  require(
      !empty.ok()
          && std::ranges::any_of(
              empty.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-177";
              }),
      "empty inside membership list has a targeted parse diagnostic");

  const auto malformed_range = parse_text(
      "malformed_membership_range.sv",
      "module malformed_membership_range; logic [7:0] a; logic r; "
      "initial r = a inside {[8'h01:8'h02}; endmodule",
      Language::SystemVerilog2017);
  require(
      !malformed_range.ok()
          && std::ranges::any_of(
              malformed_range.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-179";
              }),
      "malformed inside range has a targeted parse diagnostic");

  const auto verilog = parse_text(
      "membership.v",
      "module membership; reg a; initial a = a inside {a}; endmodule",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && std::ranges::any_of(
              verilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-175";
              }),
      "inside membership requires SystemVerilog");
}

void test_systemverilog_arithmetic_expressions() {
  const auto result = parse_text(
      "arithmetic.sv",
      R"(
module arithmetic;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic [7:0] result;
  always_comb begin
    result = +lhs;
    result = -lhs;
    result = lhs - rhs;
    result = lhs * rhs;
    result = lhs / rhs;
    result = lhs % rhs;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(), "SystemVerilog arithmetic expressions must parse");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 6
          && statements[0].value.kind == ExpressionKind::Unary
          && statements[0].value.text == "+"
          && statements[1].value.kind == ExpressionKind::Unary
          && statements[1].value.text == "-",
      "unary arithmetic expression nodes");
  const std::array<std::string_view, 4> operators{
      "-", "*", "/", "%"};
  for (std::size_t index = 0; index < operators.size(); ++index) {
    require(
        statements[index + 2].value.kind
                == ExpressionKind::Binary
            && statements[index + 2].value.text
                == operators[index],
        "binary arithmetic expression node");
  }
}

void test_gate_primitives() {
  const auto result = parse_text(
      "gates.sv",
      R"(
module gates;
  logic a;
  logic b;
  logic c;
  logic y_buf;
  logic y_buf_second;
  logic y_not;
  logic y_and;
  logic y_nand;
  logic y_or;
  logic y_nor;
  logic y_xor;
  logic y_xnor;
  logic [3:0] vector_a;
  logic [0:3] vector_b;
  logic [7:4] vector_result;
  logic enable;
  logic tri_buf;
  logic tri_not;
  buf #2 (y_buf, a), named_buf (y_buf_second, b);
  not named_not (y_not, a);
  and (strong1, pull0) (y_and, a, b, c);
  nand (y_nand, a, b, c);
  or (y_or, a, b, c);
  nor (y_nor, a, b, c);
  xor (y_xor, a, b, c);
  xnor (y_xnor, a, b, c);
  and array_gate[3:0] (vector_result, vector_a, vector_b);
  bufif1 #(1, 2, 3) tri_buf_gate (tri_buf, a, enable);
  notif0 tri_not_gate (tri_not, b, enable);
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "built-in gate primitives must parse");
  const auto& statements =
      result.design.units.front().concurrent_statements;
  require(
      statements.size() == 15
          && std::ranges::all_of(
              statements,
              [](const Statement& statement) {
                return statement.kind == StatementKind::Assignment
                    && statement.assignment_kind
                        == AssignmentKind::Continuous;
              })
          && statements[0].delay
          && statements[0].delay->magnitude == 2
          && statements[1].delay
          && statements[1].delay->magnitude == 2
          && statements[0].value.kind == ExpressionKind::Identifier
          && statements[2].value.kind == ExpressionKind::Unary
          && statements[3].value.kind == ExpressionKind::Binary
          && statements[3].verilog_drive_strength
          && statements[3].verilog_drive_strength->zero
              == VerilogStrength::Pull
          && statements[3].verilog_drive_strength->one
              == VerilogStrength::Strong
          && statements[4].value.kind == ExpressionKind::Unary,
      "gate primitives lower into continuous expression HIR");
  require(
      statements[9].label == "array_gate[3]"
          && statements[12].label == "array_gate[0]"
          && statements[9].target.kind == ExpressionKind::Index
          && statements[9].target.operands[1].text == "7"
          && statements[9].value.kind == ExpressionKind::Binary
          && statements[9].value.operands[0].kind
              == ExpressionKind::Index
          && statements[9].value.operands[0].operands[1].text == "3"
          && statements[9].value.operands[1].operands[1].text == "0",
      "gate arrays expand by ordinal across differing packed directions");
  require(
      statements[13].label == "tri_buf_gate"
          && statements[13].value.kind == ExpressionKind::Call
          && statements[13].value.text == "?:"
          && statements[13].delay
          && statements[13].delay->additional_values.size() == 2
          && statements[14].label == "tri_not_gate"
          && statements[14].value.kind == ExpressionKind::Call
          && statements[14].value.operands[0].kind
              == ExpressionKind::Unary
          && statements[14].value.operands[1].kind
              == ExpressionKind::Unary,
      "bufif/notif primitives lower to four-state conditional drivers");

  const auto invalid = parse_text(
      "invalid_gate.sv",
      R"(
module invalid_gate;
  logic a;
  logic y;
  and (y, a);
  not (y, a, a);
  bufif1 (y, a);
  logic [1:0] narrow;
  and too_wide[3:0] (narrow, a, a);
  and enormous[2147483647:0] (y, a, a);
  and [3:0] (y, a, a);
  and symbolic[a:0] (y, a, a);
  and (strong1, pull1) (y, a, a);
  nmos wrong_terminal_count(y, a);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid.ok()
          && std::ranges::count_if(
                 invalid.diagnostics,
                 [](const Diagnostic& diagnostic) {
                   return diagnostic.code == "FSIM-SV-SEM-026";
                 })
              == 3,
      "invalid gate terminal counts are targeted");
  require(
      std::ranges::any_of(
          invalid.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-148";
          }),
      "invalid gate-strength polarity is targeted");
  require(
      std::ranges::any_of(
          invalid.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-111";
          })
          && std::ranges::any_of(
              invalid.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-112";
              })
          && std::ranges::any_of(
              invalid.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-113";
              })
          && std::ranges::any_of(
              invalid.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-114";
              }),
      "gate-array bounds and terminal widths are checked");
  require(
      std::ranges::any_of(
          invalid.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-153";
          }),
      "switch primitive terminal counts are targeted");
}

void test_systemverilog_select_and_concatenation_expressions() {
  const auto result = parse_text(
      "select_concat.sv",
      R"(
module select_concat;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic selected;
  logic [3:0] part;
  logic [8:0] combined;
  logic [7:0] stream_left;
  logic [7:0] stream_right;
  logic [7:0] stream_nested;
  always_comb begin
    selected = descending[10];
    part = ascending[2:5];
    combined = {
      descending[15:12], descending[10], ascending[4:7]
    };
    stream_left = {<<2{descending}};
    stream_right = {>>{ascending}};
    stream_nested = {<<4{{descending[15:12], ascending[4:7]}}};
    descending[9] = selected;
    ascending[4:5] = part;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "SystemVerilog select and concatenation expressions must parse");
  const auto& signals = result.design.units.front().signals;
  require(
      signals[0].type.packed_range
          && signals[0].type.packed_range->left == 15
          && signals[0].type.packed_range->right == 8
          && signals[0].type.packed_range->descending,
      "descending packed range metadata");
  require(
      signals[1].type.packed_range
          && signals[1].type.packed_range->left == 0
          && signals[1].type.packed_range->right == 7
          && !signals[1].type.packed_range->descending,
      "ascending packed range metadata");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 8
          && statements[0].value.kind == ExpressionKind::Index
          && statements[0].value.operands.size() == 2
          && statements[0].value.operands[1].text == "10",
      "bit-select expression node");
  require(
      statements[1].value.kind == ExpressionKind::Slice
          && statements[1].value.operands.size() == 3
          && statements[1].value.operands[1].text == "2"
          && statements[1].value.operands[2].text == "5",
      "ascending part-select expression node");
  require(
      statements[2].value.kind == ExpressionKind::Concatenation
          && statements[2].value.operands.size() == 3
          && statements[2].value.operands[0].kind
              == ExpressionKind::Slice
          && statements[2].value.operands[1].kind
              == ExpressionKind::Index
          && statements[2].value.operands[2].kind
              == ExpressionKind::Slice,
      "concatenation expression node and operand order");
  require(
      statements[3].value.kind == ExpressionKind::Call
          && statements[3].value.text == "@stream-left"
          && statements[3].value.operands.size() == 2
          && statements[3].value.operands[0].text == "2"
          && statements[4].value.kind == ExpressionKind::Call
          && statements[4].value.text == "@stream-right"
          && statements[4].value.operands[0].text == "1"
          && statements[5].value.kind == ExpressionKind::Call
          && statements[5].value.text == "@stream-left"
          && statements[5].value.operands[1].kind
              == ExpressionKind::Concatenation,
      "stream direction, slice size, and nested concatenation HIR");
  require(
      statements[6].target.kind == ExpressionKind::Index
          && statements[6].target.operands.size() == 2
          && statements[6].target.operands[1].text == "9"
          && statements[7].target.kind == ExpressionKind::Slice
          && statements[7].target.operands.size() == 3,
      "SystemVerilog selected assignment target nodes");

  const auto invalid_stream = parse_text(
      "invalid_stream.sv",
      R"(
module invalid_stream;
  logic [7:0] result;
  initial result = {<<{}};
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_stream.ok()
          && std::ranges::any_of(
              invalid_stream.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-192";
              }),
      "empty streaming concatenation diagnostic");
  const auto require_stream_diagnostic =
      [&](const std::string_view source,
          const std::string_view code) {
        const auto parsed = parse_text(
            "malformed_stream.sv",
            source,
            Language::SystemVerilog2017);
        return !parsed.ok()
            && std::ranges::any_of(
                parsed.diagnostics,
                [&](const Diagnostic& diagnostic) {
                  return diagnostic.code == code;
                });
      };
  require(
      require_stream_diagnostic(
          "module m; logic [7:0] r; initial r = {<<2 8'ha5}; "
          "endmodule",
          "FSIM-SV-PARSE-191"),
      "missing streaming operand-open delimiter diagnostic");
  require(
      require_stream_diagnostic(
          "module m; logic [7:0] r; initial r = {<<2{8'ha5; "
          "endmodule",
          "FSIM-SV-PARSE-193"),
      "missing streaming operand-close delimiter diagnostic");
  require(
      require_stream_diagnostic(
          "module m; logic [7:0] r; initial r = {<<2{8'ha5}; "
          "endmodule",
          "FSIM-SV-PARSE-194"),
      "missing streaming outer-close delimiter diagnostic");

  const auto verilog_stream = parse_text(
      "verilog_stream.v",
      R"(
module verilog_stream;
  reg [7:0] value;
  initial value = {>>{8'ha5}};
endmodule
)",
      Language::Verilog2005);
  require(
      !verilog_stream.ok()
          && std::ranges::any_of(
              verilog_stream.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-100";
              }),
      "streaming concatenation language-version diagnostic");
}

void test_conditional_statement_trees() {
  const auto vhdl = parse_text(
      "conditionals.vhd",
      R"(
entity conditionals is end entity;
architecture rtl of conditionals is
  signal result : boolean;
begin
  choose: process
  begin
    if true then
      result <= false;
    elsif false then
      if true then
        result <= true;
      else
        result <= false;
      end if;
    else
      result <= true;
    end if;
    result <= (true nand false) and (false nor false)
              and (true xnor true) and (true /= false);
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      vhdl.ok(),
      "VHDL conditional parsing must retain a trailing bare wait");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 3
          && architecture->processes.front().statements.back().kind
              == StatementKind::WaitUntil,
      "VHDL conditional process representation");
  const auto& outer =
      architecture->processes.front().statements.front();
  require(
      outer.kind == StatementKind::If
          && outer.condition.kind == ExpressionKind::BooleanLiteral
          && outer.condition.text == "true"
          && outer.statements.size() == 1
          && outer.statements.front().value.kind
              == ExpressionKind::BooleanLiteral
          && outer.else_statements.size() == 1,
      "VHDL true branch and boolean literal nodes");
  const auto& elsif = outer.else_statements.front();
  require(
      elsif.kind == StatementKind::If
          && elsif.condition.kind == ExpressionKind::BooleanLiteral
          && elsif.condition.text == "false"
          && elsif.statements.size() == 1
          && elsif.statements.front().kind == StatementKind::If
          && elsif.else_statements.size() == 1,
      "VHDL elsif is retained as an ordered nested false branch");
  require(
      elsif.statements.front().else_statements.size() == 1,
      "nested VHDL else branch representation");
  const auto& boolean_assignment =
      architecture->processes.front().statements[1];
  require(
      boolean_assignment.kind == StatementKind::Assignment
          && boolean_assignment.value.kind == ExpressionKind::Binary
          && boolean_assignment.value.text == "and",
      "VHDL Boolean operator expression root");
  const auto contains_operator =
      [](const auto& self,
         const Expression& expression,
         const std::string_view operation) -> bool {
        if (expression.kind == ExpressionKind::Binary
            && expression.text == operation) {
          return true;
        }
        return std::any_of(
            expression.operands.begin(),
            expression.operands.end(),
            [&](const Expression& operand) {
              return self(self, operand, operation);
            });
      };
  for (const auto operation : {"nand", "nor", "xnor", "/="}) {
    require(
        contains_operator(
            contains_operator,
            boolean_assignment.value,
            operation),
        "VHDL Boolean operator node");
  }

  const auto systemverilog = parse_text(
      "conditionals.sv",
      R"(
module conditionals;
  logic [3:0] selector;
  logic result;
  initial begin
    if (selector)
      if (selector[3])
        result = 1'b1;
      else
        result = 1'b0;
    else begin
      result = 1'bx;
    end
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "nested SystemVerilog conditional statements must parse");
  const auto& sv_outer =
      systemverilog.design.units.front()
          .processes.front()
          .statements.front();
  require(
      sv_outer.kind == StatementKind::If
          && sv_outer.condition.kind == ExpressionKind::Identifier
          && sv_outer.statements.size() == 1
          && sv_outer.statements.front().kind == StatementKind::If
          && sv_outer.else_statements.size() == 1,
      "SystemVerilog dangling else binds to the nested if");
  require(
      sv_outer.statements.front().else_statements.size() == 1
          && sv_outer.else_statements.front().kind
              == StatementKind::Assignment,
      "SystemVerilog nested and outer false branches");
}

} // namespace fsim::tests::frontend
