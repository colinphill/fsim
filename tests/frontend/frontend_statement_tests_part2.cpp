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

void test_systemverilog_do_while_statements()
{
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
    const auto& statement = result.design.units.front()
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
             std::string_view { "FSIM-SV-PARSE-105" },
             std::string_view { "FSIM-SV-PARSE-106" },
             std::string_view { "FSIM-SV-PARSE-107" },
             std::string_view { "FSIM-SV-PARSE-108" } }) {
        require(
            std::ranges::any_of(
                malformed.diagnostics,
                [&](const Diagnostic& diagnostic) {
                    return diagnostic.code == code;
                }),
            "targeted SystemVerilog do-while diagnostic");
    }
}

void test_fork_process_statements()
{
    const auto parsed = parse_text(
        "fork_processes.sv",
        R"(
module fork_processes;
  logic result;
  process handle;
  string random_state;
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
    handle.suspend();
    handle.resume();
    random_state = handle.get_randstate();
    handle.set_randstate(random_state);
    handle.srandom(32'h1234);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(parsed.ok(), "fork process controls must parse");
    const auto& statements = parsed.design.units.front().processes.front().statements;
    require(
        parsed.design.units.front().signals.size() == 2
            && parsed.design.units.front().signals[1].type.spelling
                == "process"
            && statements.size() == 15
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
            && statements[9].kind == StatementKind::TaskCall
            && statements[10].kind == StatementKind::TaskCall
            && statements[11].kind == StatementKind::TaskCall
            && statements[12].kind == StatementKind::Assignment
            && statements[13].kind == StatementKind::TaskCall
            && statements[14].kind == StatementKind::TaskCall,
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

    const auto named_disable = parse_text(
        "disable_name.sv",
        R"(
module disable_name;
  initial begin : named_block
    disable named_block;
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(
        named_disable.ok()
            && named_disable.design.units.front()
                    .processes.front()
                    .statements.front()
                    .statements.front()
                    .kind
                == StatementKind::Disable
            && named_disable.design.units.front()
                    .processes.front()
                    .statements.front()
                    .statements.front()
                    .task_name
                == "named_block",
        "named-block disable retains its target");
}

void test_systemverilog_conditional_expression()
{
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
    const auto& value = result.design.units.front().processes.front().statements.front().value;
    require(
        value.kind == ExpressionKind::Call
            && value.text == "?:"
            && value.operands.size() == 3
            && value.operands[0].text == "select"
            && value.operands[1].text == "lhs"
            && value.operands[2].text == "rhs",
        "conditional-expression operand order");
}

void test_systemverilog_comparison_expressions()
{
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
    const auto& statements = result.design.units.front().processes.front().statements;
    require(
        statements.size() == 23
            && statements[0].value.kind == ExpressionKind::Unary
            && statements[0].value.text == "!",
        "logical-negation expression node");
    const std::array<std::string_view, 9> operators {
        "!=", "===", "!==", "==?", "!=?", "<", "<=", ">", ">="
    };
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

void test_systemverilog_membership_expressions()
{
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
    const auto& statements = result.design.units.front().processes.front().statements;
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

void test_systemverilog_arithmetic_expressions()
{
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
    const auto& statements = result.design.units.front().processes.front().statements;
    require(
        statements.size() == 6
            && statements[0].value.kind == ExpressionKind::Unary
            && statements[0].value.text == "+"
            && statements[1].value.kind == ExpressionKind::Unary
            && statements[1].value.text == "-",
        "unary arithmetic expression nodes");
    const std::array<std::string_view, 4> operators {
        "-", "*", "/", "%"
    };
    for (std::size_t index = 0; index < operators.size(); ++index) {
        require(
            statements[index + 2].value.kind
                    == ExpressionKind::Binary
                && statements[index + 2].value.text
                    == operators[index],
            "binary arithmetic expression node");
    }
}

void test_gate_primitives()
{
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
  logic y_buf_fanout0;
  logic y_buf_fanout1;
  logic y_single_input;
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
  buf fanout (y_buf_fanout0, y_buf_fanout1, a);
  and single_input (y_single_input, a);
endmodule
)",
        Language::SystemVerilog2017);
    require(result.ok(), "built-in gate primitives must parse");
    const auto& statements = result.design.units.front().concurrent_statements;
    require(
        statements.size() == 18
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
                == ExpressionKind::Unary
            && statements[15].label == "fanout$output0"
            && statements[16].label == "fanout$output1"
            && statements[15].value.text == "a"
            && statements[16].value.text == "a"
            && statements[17].label == "single_input"
            && statements[17].value.text == "a",
        "tri-state, multiple-output, and single-input gate forms lower to "
        "continuous drivers");

    const auto invalid = parse_text(
        "invalid_gate.sv",
        R"(
module invalid_gate;
  logic a;
  logic y;
  and (y);
  not (y);
  bufif1 (y, a);
  buf invalid_output(y, (a & a), a);
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
                return diagnostic.code == "FSIM-SV-SEM-388";
            }),
        "buf/not non-lvalue output terminals are targeted");
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

void test_systemverilog_select_and_concatenation_expressions()
{
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
    const auto& statements = result.design.units.front().processes.front().statements;
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

void test_conditional_statement_trees()
{
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
    const auto* architecture = vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
    require(
        architecture != nullptr
            && architecture->processes.size() == 1
            && architecture->processes.front().statements.size() == 3
            && architecture->processes.front().statements.back().kind
                == StatementKind::WaitUntil,
        "VHDL conditional process representation");
    const auto& outer = architecture->processes.front().statements.front();
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
    const auto& boolean_assignment = architecture->processes.front().statements[1];
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
    for (const auto operation : { "nand", "nor", "xnor", "/=" }) {
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
    const auto& sv_outer = systemverilog.design.units.front()
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
