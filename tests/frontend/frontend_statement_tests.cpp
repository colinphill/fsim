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

    void require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    [[maybe_unused]] std::filesystem::path make_test_directory(
        std::string_view name)
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto directory = std::filesystem::temp_directory_path()
            / ("fsim-" + std::string { name } + "-"
                + std::to_string(suffix));
        std::filesystem::create_directories(directory);
        return directory;
    }

    [[maybe_unused]] void write_text(
        const std::filesystem::path& path,
        const std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << text;
        require(
            output.good(),
            "frontend test fixture must be writable");
    }

} // namespace

void test_wildcard_and_always_comb_processes()
{
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
    const auto& packed_processes = packed_events.design.units.front().processes;
    require(
        packed_processes.size() == 2
            && packed_processes[0].sensitivities.size() == 1
            && packed_processes[0].sensitivities.front().expression.kind == ExpressionKind::Binary
            && packed_processes[0].sensitivities.front().expression.text == "|"
            && packed_processes[1].statements.front().sensitivities.front().expression.text == "^",
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
    const auto& generalized = generalized_packed_events.design.units.front().processes;
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
            && body_timed.design.units.front().processes.front().sensitivities.empty()
            && body_timed.design.units.front().processes.front().statements.front().kind == StatementKind::Delay,
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
             std::string_view { "FSIM-SV-SEM-011" },
             std::string_view { "FSIM-SV-SEM-012" },
             std::string_view { "FSIM-SV-SEM-013" } }) {
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
             std::string_view { "FSIM-SV-SEM-101" },
             std::string_view { "FSIM-SV-SEM-102" } }) {
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

void test_systemverilog_case_statements()
{
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
    2'b00 &&& selector == 2'b00: result = 2'b01;
    .captured &&& captured == selector: result = captured;
    tagged Some .payload &&& payload == selector: result = payload;
    '{left: .lhs, right: 1'b1}: result = {lhs, 1'b1};
    .*: result = 2'b10;
    default: result = 2'b11;
  endcase
endmodule
)",
        Language::SystemVerilog2017);
    require(result.ok(), "exact SystemVerilog case statement must parse");
    const auto& statement = result.design.units.front().processes.front().statements.front();
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
            && result.design.units.front().processes[1].statements.front().case_match_kind
                == CaseMatchKind::WildcardZ
            && result.design.units.front().processes[2].statements.front().case_match_kind
                == CaseMatchKind::WildcardXZ
            && result.design.units.front().processes[3].statements.front().case_match_kind
                == CaseMatchKind::Inside,
        "exact, casez, casex, and inside modes remain distinct in HIR");
    const auto& inside = result.design.units.front().processes[3].statements.front();
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
        result.design.units.front().processes[4].statements.front().case_qualifier
                == CaseQualifier::Unique
            && result.design.units.front().processes[5].statements.front().case_qualifier
                == CaseQualifier::Unique0
            && result.design.units.front().processes[6].statements.front().case_qualifier
                == CaseQualifier::Priority
            && result.design.units.front().processes[6].statements.front().case_match_kind
                == CaseMatchKind::Inside
            && result.design.units.front().processes[7].statements.front().case_qualifier
                == CaseQualifier::Unique
            && result.design.units.front().processes[7].statements.front().case_match_kind
                == CaseMatchKind::Matches
            && result.design.units.front().processes[4].statements.front().span.begin.column == 11,
        "case qualifier kind and source span remain distinct from matching mode");
    const auto& matches = result.design.units.front().processes[7].statements.front();
    require(
        matches.case_alternatives.size() == 6
            && matches.case_alternatives[0].choices.size() == 1
            && matches.case_alternatives[0].choices.front().kind
                == ExpressionKind::Call
            && matches.case_alternatives[0].choices.front().text
                == "@match-guard"
            && matches.case_alternatives[0].choices.front().operands.size()
                == 2
            && matches.case_alternatives[0].choices.front().operands[0].text
                == "2'b00"
            && matches.case_alternatives[0].choices.front().operands[1].text
                == "=="
            && matches.case_alternatives[1].choices.size() == 1
            && matches.case_alternatives[1].choices.front().kind
                == ExpressionKind::Call
            && matches.case_alternatives[1].choices.front().text
                == "@match-guard"
            && matches.case_alternatives[1].choices.front().operands[0].text
                == "@match-bind:captured"
            && matches.case_alternatives[2].choices.front().text
                == "@match-guard"
            && matches.case_alternatives[2].choices.front().operands[0].text
                == "@match-tagged:Some"
            && matches.case_alternatives[2].choices.front().operands[0].operands.front().text
                == "@match-bind:payload"
            && matches.case_alternatives[3].choices.front().kind
                == ExpressionKind::Aggregate
            && matches.case_alternatives[3].choices.front().text
                == "@match-structure"
            && matches.case_alternatives[3].choices.front().operands.size()
                == 2
            && matches.case_alternatives[4].choices.front().kind
                == ExpressionKind::Call
            && matches.case_alternatives[4].choices.front().text
                == "@match-wildcard"
            && matches.case_alternatives[5].is_default
            && !matches.span.empty(),
        "case matches retains guarded constant and wildcard patterns");

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
             std::string_view { "FSIM-SV-SEM-014" } }) {
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
             std::string_view { "FSIM-SV-PARSE-186" },
             std::string_view { "FSIM-SV-UNSUPPORTED-017" } }) {
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
             std::string_view { "FSIM-SV-PARSE-181" },
             std::string_view { "FSIM-SV-PARSE-182" },
             std::string_view { "FSIM-SV-PARSE-183" },
             std::string_view { "FSIM-SV-PARSE-184" } }) {
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
    tagged Some: result = 1'b0;
    '{2'b00}: result = 1'b0;
    2'b00, 2'b01: result = 1'b0;
    .: result = 1'b0;
  endcase
endmodule
)",
        Language::SystemVerilog2017);
    require(!invalid_matches.ok(), "deferred case patterns must fail");
    for (const auto code : {
             std::string_view { "FSIM-SV-PARSE-188" },
             std::string_view { "FSIM-SV-PARSE-189" },
             std::string_view { "FSIM-SV-PARSE-190" } }) {
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

void test_systemverilog_procedural_for_loops()
{
    const auto foreach_result = parse_verilog(
        SourceText { "procedural_foreach.sv",
        R"(
module procedural_foreach;
  string text;
  int values[$];
  int matrix[1:0][1:0];
  initial begin
    foreach (text[index])
      if (text[index] == "*")
        text[index] = "+";
    foreach (matrix[row, column]) begin
      matrix[row][column] = row + column;
      if (row == 1)
        break;
    end
  end
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        foreach_result.ok(),
        "SystemVerilog procedural foreach loops parse with single and block bodies");
    const auto& foreach_statements = foreach_result.design.units.front().processes.front().statements;
    require(
        foreach_statements.size() == 2
            && foreach_statements.front().kind == StatementKind::Loop
            && foreach_statements.front().loop_runtime
            && foreach_statements.front().loop_variable_declared
            && foreach_statements.front().loop_variable == "index"
            && foreach_statements.front().condition.kind
                == ExpressionKind::Call
            && foreach_statements.front().condition.text == "@sv-foreach"
            && foreach_statements.front().condition.operands.size() == 2
            && foreach_statements.back().loop_variable == "row"
            && foreach_statements.back().condition.operands.size() == 3
            && foreach_statements.back().condition.operands[1].text == "row"
            && foreach_statements.back().condition.operands[2].text == "column"
            && foreach_statements.front().statements.size() == 1
            && foreach_statements.back().statements.size() == 2,
        "foreach collection, index, and bodies remain explicit runtime-loop HIR");

    const auto selected_foreach = parse_verilog(
        SourceText { "selected_foreach.sv",
        R"(
class selected_foreach;
  enum {STANDARD, NON_STANDARD, ILLEGAL} source;
  constraint source_limit {
    source < (64 'h1 << 2);
  }
  int values[$];
  int matrix[1:0][1:0];
  function void scrub();
    foreach (this.values[index])
      values[index] = index;
    foreach (this.matrix[row].entries[column])
      values[column] = column;
    this.values[index].print();
    uvm_config_db#(int)::set(this, "scope", "field", index);
    this.state.scope.configure(index, , "value");
    this.values.sort with (item);
    this.state.scope.down("selected");
    this.state.scope.up();
    selected_foreach::default_value = index;
  endfunction
  function void stream_assign();
    int unpacked[];
    bit [31:0] packed_value;
    { << int { unpacked }} = packed_value;
  endfunction
  function selected_foreach cast_create(selected_foreach rhs);
    selected_foreach found;
    found = rhs.find(rhs);
    found = rhs.configure("name", , rhs);
    if (!randomize(found) with { found != null; })
      return;
    if (!rhs.randomize() with {})
      return;
    if (!$cast(cast_create, found))
      return;
  endfunction
endclass
)" },
        StandardRevision::SystemVerilog2023);
    const auto& inline_randomize = selected_foreach.design
                                       .systemverilog_classes.front()
                                       .methods.back()
                                       .statements[2]
                                       .condition.operands.front();
    const auto& empty_inline_randomize = selected_foreach.design
                                             .systemverilog_classes.front()
                                             .methods.back()
                                             .statements[3]
                                             .condition.operands.front();
    require(
        selected_foreach.ok()
            && selected_foreach.design.systemverilog_classes.size() == 1
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements.front()
                    .target.text
                == "this.values"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[1]
                    .target.kind
                == ExpressionKind::Index
            && selected_foreach.design.systemverilog_classes.front()
                .methods.front()
                .statements[1]
                .target.text.ends_with(
                    ".entries")
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[2]
                    .kind
                == StatementKind::ContainerMethod
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[3]
                    .kind
                == StatementKind::TaskCall
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[3]
                    .task_name
                == "uvm_config_db#(int)::set"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[4]
                    .task_arguments.size()
                == 3
            && !selected_foreach.design.systemverilog_classes.front()
                .methods.front()
                .statements[4]
                .task_arguments[1]
                .valid()
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[5]
                    .kind
                == StatementKind::ContainerMethod
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[5]
                    .value.text
                == ".sort"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[6]
                    .task_name
                == "this.state.scope.down"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[7]
                    .task_name
                == "this.state.scope.up"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.front()
                    .statements[8]
                    .target.text
                == "selected_foreach::default_value"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.size()
                == 3
            && selected_foreach.design.systemverilog_classes.front()
                    .methods[1]
                    .statements.front()
                    .target.text
                == "@stream-left"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.back()
                    .name
                == "cast_create"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.back()
                    .statements[2]
                    .condition.operands.front()
                    .aggregate_choices
                == std::vector<std::string> { "@sv-inline-constraint" }
            && inline_randomize.aggregate_choice_expressions.size() == 1
            && inline_randomize.aggregate_choice_expressions.front().size() == 1
            && inline_randomize.aggregate_choice_expressions.front().front().kind
                == ExpressionKind::Binary
            && inline_randomize.aggregate_choice_expressions.front().front().text
                == "!="
            && inline_randomize.aggregate_choice_expressions.front().front().operands.front().text
                == "found"
            && selected_foreach.design.systemverilog_classes.front()
                    .methods.back()
                    .statements[3]
                    .condition.operands.front()
                    .aggregate_choices
                == std::vector<std::string> { "@sv-inline-constraint" }
            && empty_inline_randomize.aggregate_choice_expressions.size() == 1
            && empty_inline_randomize.aggregate_choice_expressions.front().empty(),
        "foreach, indexed calls, parameterized statics, class find, and "
        "inline constraints parse without source rewriting");

    const auto result = parse_text(
        "procedural_for.sv",
        R"(
module procedural_for;
  logic [3:0] result;
  initial begin
    typedef int loop_item;
    integer runtime_lane;
    integer start_lane;
    integer active_lane;
    loop_item first_item;
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
    for (start_lane = active_lane;
         active_lane < 4;
         ++active_lane)
      result[active_lane] = result[active_lane];
    for (int unsigned lane = 0; lane < 1; lane++)
      result[lane] = result[lane];
    for (loop_item item = first_item;
         item != null;
         item = first_item)
      result[0] = result[0];
    for (int update_lane = 0;
         update_lane < 1;
         update_lane++, active_lane--)
      result[update_lane] = result[update_lane];
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
    const auto& statements = result.design.units.front().processes.front().statements;
    require(
        statements.size() == 12
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
            && statements[7].value.text == "+"
            && statements[8].loop_runtime
            && statements[8].target.text == "start_lane"
            && statements[8].condition.operands.front().text
                == "active_lane"
            && statements[8].loop_update_target.text == "active_lane"
            && statements[8].value.operands.front().text == "active_lane"
            && statements[9].loop_variable_declared
            && statements[9].loop_variable == "lane"
            && statements[10].loop_variable_declared
            && statements[10].loop_variable == "item"
            && statements[10].loop_runtime
            && statements[10].loop_update_target.text == "item"
            && statements[11].loop_updates.size() == 1
            && statements[11].loop_updates.front().target.text
                == "active_lane"
            && statements[11].loop_updates.front().value.text == "-",
        "SystemVerilog loop range normalization and bodies");

    const auto invalid = parse_text(
        "bad_procedural_for.sv",
        R"(
module bad_procedural_for;
  initial begin
    for (int lane = 0; lane < 4; lane);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(
        !invalid.ok()
            && std::ranges::any_of(
                invalid.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-103";
                }),
        "a procedural loop update still requires assignment or increment "
        "syntax");
}

void test_verilog_repeat_statements()
{
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
    const auto& statements = systemverilog.design.units.front()
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

void test_runtime_loop_statements()
{
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
    const auto& statements = systemverilog.design.units.front()
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
  integer lane;
  initial begin
    flag = 1'b1;
    for (lane = 0; lane < 2; lane = lane + 1) flag = ~flag;
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
        "Verilog-2005 for and runtime loops share the common HIR");

    const auto verilog_inline_declaration = parse_text(
        "verilog_inline_loop.v",
        "module verilog_inline_loop; initial for (integer lane = 0; "
        "lane < 1; lane = lane + 1) ; endmodule",
        Language::Verilog2005);
    require(
        !verilog_inline_declaration.ok()
            && std::ranges::any_of(
                verilog_inline_declaration.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VERILOG-SEM-013";
                }),
        "Verilog-2005 rejects SystemVerilog inline for declarations");

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
    require(!invalid.ok(), "malformed runtime loops must fail");
    for (const auto code : {
             std::string_view { "FSIM-SV-PARSE-102" },
             std::string_view { "FSIM-SV-PARSE-103" } }) {
        require(
            std::ranges::any_of(
                invalid.diagnostics,
                [&](const Diagnostic& diagnostic) {
                    return diagnostic.code == code;
                }),
            "targeted runtime-loop diagnostic");
    }
}

void test_loop_control_statements()
{
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
    const auto& sv_loop = systemverilog.design.units.front()
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
    const auto* architecture = vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
    const auto& vhdl_loop = architecture->processes.front().statements.front();
    const auto& unconditional_vhdl_loop = architecture->processes.front().statements[1];
    require(
        vhdl_loop.kind == StatementKind::Loop
            && vhdl_loop.loop_label == "outer_loop"
            && vhdl_loop.statements.size() == 3
            && vhdl_loop.statements[0].kind == StatementKind::If
            && vhdl_loop.statements[0].statements.front().kind
                == StatementKind::Continue
            && vhdl_loop.statements[0].statements.front().loop_control_label
                == "outer_loop"
            && vhdl_loop.statements[1].kind == StatementKind::Loop
            && vhdl_loop.statements[1].loop_label == "inner_loop"
            && vhdl_loop.statements[1].statements.front().kind
                == StatementKind::Break
            && vhdl_loop.statements[1].statements.front().loop_control_label
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

} // namespace fsim::tests::frontend
