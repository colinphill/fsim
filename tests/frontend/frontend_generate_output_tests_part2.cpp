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

[[maybe_unused]] bool has_code(
    const ParseResult& parsed, const std::string_view code) {
  return std::ranges::any_of(
      parsed.diagnostics,
      [&](const auto& diagnostic) {
        return diagnostic.code == code;
      });
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

void test_systemverilog_named_events() {
  const auto parsed = parse_text(
      "named_events.sv",
      R"(
module named_events;
  event fired, acknowledged;
  logic observed;
  initial begin
    -> fired;
    @(acknowledged);
  end
  always @(fired) observed = 1'b1;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "SystemVerilog named events must parse");
  const auto& unit = parsed.design.units.front();
  require(
      unit.signals.size() == 3
          && unit.signals[0].name == "fired"
          && unit.signals[0].type.spelling == "event"
          && unit.signals[1].name == "acknowledged"
          && unit.signals[1].type.spelling == "event",
      "named event declarations");
  require(
      unit.processes.size() == 2
          && unit.processes[0].statements.size() == 2
          && unit.processes[0].statements[0].kind
              == StatementKind::EventTrigger
          && unit.processes[0].statements[0].target.text == "fired"
          && unit.processes[0].statements[1].kind
              == StatementKind::WaitOn
          && unit.processes[0].statements[1]
                 .sensitivities.front().signal
              == "acknowledged"
          && unit.processes[1].sensitivities.front().signal == "fired",
      "named event trigger and controls");

  const auto nonblocking = parse_text(
      "nonblocking_event.sv",
      R"(
module nonblocking_event;
  event fired;
  initial ->> fired;
  initial ->> #2 fired;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [](const auto& result, const std::string_view code) {
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [&](const auto& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      nonblocking.ok()
          && nonblocking.design.units.front()
                 .processes.front().statements.front()
                 .kind
              == StatementKind::EventTrigger
          && nonblocking.design.units.front()
                 .processes.front().statements.front()
                 .assignment_kind
              == AssignmentKind::NonBlocking
          && !nonblocking.design.units.front()
                  .processes.front().statements.front().delay
          && nonblocking.design.units.front().processes.size() == 2
          && nonblocking.design.units.front()
                 .processes[1].statements.front().delay
          && nonblocking.design.units.front()
                 .processes[1].statements.front().delay->magnitude
              == 2,
      "nonblocking event trigger HIR");

  const auto verilog_nonblocking = parse_text(
      "nonblocking_event.v",
      R"(
module nonblocking_event;
  event fired;
  initial ->> fired;
endmodule
)",
      Language::Verilog2005);
  require(
      has_code(
          verilog_nonblocking, "FSIM-VERILOG-SEM-008"),
      "nonblocking event trigger language diagnostic");

  const auto invalid_immediate_delay = parse_text(
      "immediate_delay.sv",
      R"(
module immediate_delay;
  event fired;
  initial -> #1 fired;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      has_code(invalid_immediate_delay, "FSIM-SV-SEM-036"),
      "delayed immediate event trigger diagnostic");

  const auto class_events = parse_text(
      "class_events.sv",
      R"(
class uvm_waiter;
  event trigger;
endclass
class uvm_event_owner;
  protected event m_event;
  uvm_waiter waiters[int];
  task wake(input int key);
    void'(notify(key));
    -> waiters[key].trigger;
    -> m_event;
  endtask
endclass
)",
      Language::SystemVerilog2017);
  require(class_events.ok(), "UVM-shaped class events must parse");
  require(
      class_events.design.systemverilog_classes.size() == 2
          && class_events.design.systemverilog_classes[0]
                 .properties.front().declaration.type.spelling
              == "event"
          && class_events.design.systemverilog_classes[1]
                 .properties.front().declaration.type.spelling
              == "event"
          && class_events.design.systemverilog_classes[1]
                 .methods.front().statements.size() == 3
          && class_events.design.systemverilog_classes[1]
                 .methods.front().statements[0].kind
              == StatementKind::ContainerMethod
          && class_events.design.systemverilog_classes[1]
                 .methods.front().statements[0].value.text
              == "@sv-cast:void"
          && class_events.design.systemverilog_classes[1]
                 .methods.front().statements[1].kind
              == StatementKind::EventTrigger
          && class_events.design.systemverilog_classes[1]
                 .methods.front().statements[1].target.kind
              == ExpressionKind::Index
          && class_events.design.systemverilog_classes[1]
                 .methods.front().statements[2].target.text
              == "m_event",
      "class event properties, void casts, and selected/indexed triggers retain HIR");

  const auto duplicate = parse_text(
      "duplicate_event.sv",
      R"(
module duplicate_event;
  logic fired;
  event fired;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      has_code(duplicate, "FSIM-SV-SEM-035"),
      "duplicate named event diagnostic");
}

void test_verilog_literal_display() {
  const auto parsed = parse_text(
      "display.v",
      R"(
module display;
  initial begin
    $display("hello\nworld\t\"quote\"\\slash\101\.");
    $display();
    $display;
  end
endmodule
)",
      Language::Verilog2005);
  require(parsed.ok(), "literal $display tasks must parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 3
          && std::all_of(
              statements.begin(),
              statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline;
              })
          && statements.front().output_text
              == "hello\nworld\t\"quote\"\\slashA."
          && statements[1].output_text.empty()
          && statements[2].output_text.empty(),
      "literal and empty $display HIR");

  const auto escaped_punctuation = parse_text(
      "escaped-punctuation.sv",
      R"(
module escaped_punctuation;
  string text = "\%\.";
endmodule
)",
      Language::SystemVerilog2017);
  require(
      escaped_punctuation.ok(),
      "escaped punctuation in ordinary strings must parse");

  const auto formatted = parse_text(
      "formatted_display.sv",
      R"(
module formatted_display;
  logic q;
  initial begin
    $display("q=%%:%b!", q);
    $write("%b", q);
    $display("%h", q);
    $display("%o", q);
    $display("%d", q);
    $display("%c", q);
    $display("%0s", q);
    $display("%0h", q);
    $display("%B", q);
    $display("%X", q);
    $display("%O", q);
    $display("%D", q);
    $display("%C", q);
    $display("%S", q);
    $display("%8h", q);
    $display("%-8x", q);
    $display("%08d", q);
    $display("scope=%m q=%B", q);
    $display("time=%08T");
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      formatted.ok()
          && formatted.design.units.front().processes.front()
                 .statements.size() == 19
          && formatted.design.units.front().processes.front()
                 .statements[0].output_format
              == OutputFormat::Binary
          && formatted.design.units.front().processes.front()
                 .statements[0].output_prefix == "q=%:"
          && formatted.design.units.front().processes.front()
                 .statements[0].output_suffix == "!"
          && formatted.design.units.front().processes.front()
                 .statements[0].value.text == "q"
          && !formatted.design.units.front().processes.front()
                  .statements[1].output_newline
          && formatted.design.units.front().processes.front()
                 .statements[2].output_format
              == OutputFormat::Hexadecimal
          && formatted.design.units.front().processes.front()
                 .statements[3].output_format
              == OutputFormat::Octal
          && formatted.design.units.front().processes.front()
                 .statements[4].output_format
              == OutputFormat::Decimal
          && formatted.design.units.front().processes.front()
                 .statements[5].output_format
              == OutputFormat::Character
          && formatted.design.units.front().processes.front()
                 .statements[6].output_format
              == OutputFormat::String
          && formatted.design.units.front().processes.front()
                 .statements[6].output_suppress_leading_zero
          && formatted.design.units.front().processes.front()
                 .statements[7].output_format
              == OutputFormat::Hexadecimal
          && formatted.design.units.front().processes.front()
                 .statements[7].output_suppress_leading_zero,
          "single-value formats, %0 suppression, and %% decoding");
  const auto& formatted_statements =
      formatted.design.units.front().processes.front().statements;
  require(
      formatted_statements[8].output_format
              == OutputFormat::Binary
          && formatted_statements[9].output_format
              == OutputFormat::Hexadecimal
          && formatted_statements[10].output_format
              == OutputFormat::Octal
          && formatted_statements[11].output_format
              == OutputFormat::Decimal
          && formatted_statements[12].output_format
              == OutputFormat::Character
          && formatted_statements[13].output_format
              == OutputFormat::String,
      "uppercase and %x conversion aliases");
  require(
      formatted_statements[14].output_minimum_width == 8
          && !formatted_statements[14].output_left_justify
          && !formatted_statements[14].output_zero_pad
          && formatted_statements[15].output_minimum_width == 8
          && formatted_statements[15].output_left_justify
          && !formatted_statements[15].output_zero_pad
          && formatted_statements[16].output_minimum_width == 8
          && !formatted_statements[16].output_left_justify
          && formatted_statements[16].output_zero_pad,
      "minimum-width, left-justification, and zero-padding metadata");
  require(
      formatted_statements[17].output_values.size() == 2
          && formatted_statements[17].output_values[0].format
              == OutputFormat::Hierarchy
          && !formatted_statements[17].output_values[0].value.valid()
          && formatted_statements[17].output_values[0].prefix
              == "scope="
          && formatted_statements[17].output_values[1].format
              == OutputFormat::Binary
          && formatted_statements[17].output_values[1].value.text
              == "q",
      "%m hierarchy substitution does not consume a runtime value");
  require(
      formatted_statements[18].output_values.size() == 1
          && formatted_statements[18].output_values[0].format
              == OutputFormat::Time
          && !formatted_statements[18].output_values[0].value.valid()
          && formatted_statements[18].output_values[0].minimum_width
              == 8
          && formatted_statements[18].output_values[0].zero_pad,
      "%t current-time substitution and width metadata");

  const auto explicit_time_format = parse_text(
      "explicit_time_format.sv",
      R"(
module explicit_time_format;
  logic [7:0] data;
  logic flag;
  initial $display("t=%0t data=%x flag=%b", $time, data, flag);
endmodule
)",
      Language::SystemVerilog2017);
  require(explicit_time_format.ok(),
      "an explicit $time argument must align with %t");
  const auto& explicit_time_values = explicit_time_format.design.units
      .front().processes.front().statements.front().output_values;
  require(
      explicit_time_values.size() == 3
          && explicit_time_values[0].format == OutputFormat::Time
          && explicit_time_values[0].value.text == "$time"
          && explicit_time_values[1].format
              == OutputFormat::Hexadecimal
          && explicit_time_values[1].value.text == "data"
          && explicit_time_values[2].format == OutputFormat::Binary
          && explicit_time_values[2].value.text == "flag",
      "explicit %t values must not shift later formatted arguments");

  const auto invalid_format_width = parse_text(
      "invalid_format_width.sv",
      R"(
module invalid_format_width;
  logic q;
  initial begin
    $display("%-h", q);
    $display("%-0s", q);
    $display("%999999999999999999999h", q);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::count_if(
          invalid_format_width.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-042";
          })
          == 3,
      "invalid field modifiers need targeted diagnostics");

  const auto unsupported = parse_text(
      "unsupported_format.sv",
      R"(
module unsupported_format;
  logic q;
  initial $display("%v", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          unsupported.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-042";
          }),
      "unsupported output conversions need a targeted diagnostic");

  const auto bad_escape = parse_text(
      "bad_escape.sv",
      R"(
module bad_escape;
  initial $display("bad\q");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::any_of(
          bad_escape.diagnostics.begin(),
          bad_escape.diagnostics.end(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-040";
          }),
      "unsupported output-string escapes need a targeted diagnostic");

  const auto wide_octal_escape = parse_text(
      "wide_octal_escape.sv",
      R"(
module wide_octal_escape;
  initial $display("\777");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::any_of(
          wide_octal_escape.diagnostics.begin(),
          wide_octal_escape.diagnostics.end(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-040";
          }),
      "out-of-byte-range octal escapes need a targeted diagnostic");

  const auto write = parse_text(
      "write.sv",
      R"(
module write;
  initial begin
    $write("hello");
    $write();
    $write;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(write.ok(), "literal $write tasks must parse");
  const auto& write_statements =
      write.design.units.front().processes.front().statements;
  require(
      write_statements.size() == 3
          && std::all_of(
              write_statements.begin(),
              write_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && !statement.output_newline;
              })
          && write_statements.front().output_text == "hello"
          && write_statements[1].output_text.empty()
          && write_statements[2].output_text.empty(),
      "literal and empty $write HIR");

  const auto multiple_write = parse_text(
      "multiple_write.sv",
      R"(
module formatted_write;
  logic [3:0] a;
  logic [7:0] b;
  initial begin
    $write("a=%b b=%h tail=", a, b, a);
    $display(a, b);
    $display("prefix=", a);
  end
endmodule
)",
      Language::SystemVerilog2017);
  const auto& multi_statements =
      multiple_write.design.units.front().processes.front().statements;
  require(
      multiple_write.ok() && multi_statements.size() == 3
          && multi_statements[0].output_values.size() == 3
          && multi_statements[0].output_values[0].format
              == OutputFormat::Binary
          && multi_statements[0].output_values[0].prefix == "a="
          && multi_statements[0].output_values[1].format
              == OutputFormat::Hexadecimal
          && multi_statements[0].output_values[1].prefix == " b="
          && multi_statements[0].output_values[2].format
              == OutputFormat::Decimal
          && multi_statements[0].output_values[2].prefix == " tail="
          && multi_statements[1].output_values.size() == 2
          && multi_statements[1].output_values[0].format
              == OutputFormat::Decimal
          && multi_statements[2].output_values.size() == 1
          && multi_statements[2].output_values[0].prefix == "prefix=",
      "multiple conversions and default runtime arguments in source order");

  const auto missing_format_value = parse_text(
      "missing_format_value.sv",
      R"(
module missing_format_value;
  logic q;
  initial $display("%b %h", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          missing_format_value.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-037";
          }),
      "missing formatted values need a task-specific diagnostic");

  const auto strobe = parse_text(
      "strobe.sv",
      R"(
module strobe;
  initial begin
    $strobe("later");
    $strobe();
    $strobe;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(strobe.ok(), "literal $strobe tasks must parse");
  const auto& strobe_statements =
      strobe.design.units.front().processes.front().statements;
  require(
      strobe_statements.size() == 3
          && std::all_of(
              strobe_statements.begin(),
              strobe_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline
                    && statement.output_postponed;
              })
          && strobe_statements.front().output_text == "later"
          && strobe_statements[1].output_text.empty()
          && strobe_statements[2].output_text.empty(),
      "literal and empty $strobe HIR");

  const auto unsupported_strobe = parse_text(
      "formatted_strobe.sv",
      R"(
module formatted_strobe;
  logic q;
  initial $strobe("q=%b", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      unsupported_strobe.ok()
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_format
              == OutputFormat::Binary
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_postponed
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_prefix == "q=",
      "formatted $strobe HIR and postponed policy");

  const auto monitor = parse_text(
      "monitor.sv",
      R"(
module monitor;
  initial begin
    $monitor("once");
    $monitor();
    $monitor;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(monitor.ok(), "literal $monitor tasks must parse");
  const auto& monitor_statements =
      monitor.design.units.front().processes.front().statements;
  require(
      monitor_statements.size() == 3
          && std::all_of(
              monitor_statements.begin(),
              monitor_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline
                    && statement.output_postponed;
              })
          && monitor_statements.front().output_text == "once",
      "literal $monitor initial-publication HIR");

  const auto formatted_monitor = parse_text(
      "formatted_monitor.sv",
      R"(
module formatted_monitor;
  logic q;
  initial begin
    $monitor("q=%b t=%t", q);
    $monitoroff;
    $monitoron();
  end
endmodule
)",
      Language::SystemVerilog2017);
  const auto& formatted_monitor_statements =
      formatted_monitor.design.units.front().processes.front().statements;
  require(
      formatted_monitor.ok()
          && formatted_monitor_statements.size() == 3
          && formatted_monitor_statements[0].output_monitor
          && formatted_monitor_statements[0].output_postponed
          && formatted_monitor_statements[0].output_values.size() == 2
          && formatted_monitor_statements[0].output_values[0].format
              == OutputFormat::Binary
          && formatted_monitor_statements[0].output_values[1].format
              == OutputFormat::Time
          && formatted_monitor_statements[1].kind
              == StatementKind::MonitorControl
          && !formatted_monitor_statements[1].monitor_enabled
          && formatted_monitor_statements[2].kind
              == StatementKind::MonitorControl
          && formatted_monitor_statements[2].monitor_enabled,
      "value-sensitive monitor registration and on/off controls");

  const auto numeric = parse_text(
      "numeric_output.sv",
      R"(
module numeric_output;
  initial begin
    $display(42);
    $display(18_446_744_073_709_551_616);
    $write(8'h2a);
    $strobe(6'b10_1010);
    $display(8'shff);
    $write(4'sb0111);
    $display(257'b1_0000000000000000000000000000000000000000000000000000000000000000);
    $display(257'o2_000000000000000000000);
    $display(257'd18_446_744_073_709_551_616);
    $display(257'h1_0000000000000000);
    $display(65'sh1_0000000000000000);
    $display(65'h3_0000000000000000);
    $display('sh800000000);
    $display(4097'h1);
    $display(999999999999999999999999999999999999999999'h1);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(numeric.ok(), "constant numeric output tasks must parse");
  const auto& numeric_statements = numeric.design.units.front().processes.front().statements;
  require(
      numeric_statements.size() == 15
          && numeric_statements[0].output_text == "42"
          && numeric_statements[0].output_newline
          && numeric_statements[1].output_text
              == "18446744073709551616"
          && numeric_statements[2].output_text == "42"
          && !numeric_statements[2].output_newline
          && numeric_statements[3].output_text == "42"
          && numeric_statements[3].output_postponed
          && numeric_statements[4].output_text == "-1"
          && numeric_statements[4].output_newline
          && numeric_statements[5].output_text == "7"
          && !numeric_statements[5].output_newline
          && std::all_of(
              numeric_statements.begin() + 6,
              numeric_statements.begin() + 10,
              [](const Statement& statement) {
                  return statement.output_text
                      == "18446744073709551616";
              })
          && numeric_statements[10].output_text
              == "-18446744073709551616"
          && numeric_statements[11].output_text
              == "18446744073709551616"
          && numeric_statements[12].output_text
              == "-34359738368"
          && numeric_statements[13].output_text == "1"
          && numeric_statements[14].output_text == "1",
      "arbitrary-width binary, octal, decimal, and hex output literal folding");

  const auto unknown_numeric = parse_text(
      "unknown_numeric_output.sv",
      R"(
module unknown_numeric_output;
  initial $display(4'bx001);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          unknown_numeric.diagnostics,
          [](const auto& diagnostic) {
              return diagnostic.code == "FSIM-SV-SEM-037";
          }),
      "unknown numeric output literals need a targeted diagnostic");
}

void test_systemverilog_random_functions() {
  const auto parsed = parse_text(
      "random_functions.sv",
      R"(
module random_functions;
  logic [31:0] a, b, c, d, e, f, u;
  initial begin
    a = $urandom;
    b = $urandom();
    c = $random;
    d = $random();
    e = $urandom_range(9);
    f = $urandom_range(9, 3);
    u = $urandom_range(4'bx);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "random system functions must parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 7
          && std::ranges::all_of(
              statements,
              [](const Statement& statement) {
                return statement.kind == StatementKind::Assignment
                    && statement.value.kind == ExpressionKind::Call;
              })
          && statements[0].value.text == "$urandom"
          && statements[0].value.operands.empty()
          && statements[1].value.text == "$urandom"
          && statements[1].value.operands.empty()
          && statements[2].value.text == "$random"
          && statements[2].value.operands.empty()
          && statements[3].value.text == "$random"
          && statements[3].value.operands.empty()
          && statements[4].value.text == "$urandom_range"
          && statements[4].value.operands.size() == 1
          && statements[5].value.text == "$urandom_range"
          && statements[5].value.operands.size() == 2
          && statements[6].value.text == "$urandom_range"
          && statements[6].value.operands.size() == 1
          && statements[6].value.operands[0].kind
              == ExpressionKind::LogicLiteral,
      "bare/empty random calls and range arguments in typed HIR");
}

void test_verilog_defparam_declarations()
{
    const auto parsed = parse_text(
        "defparam-hierarchy.v",
        R"(
module defparam_leaf #(parameter [256:0] VALUE = 257'd0) ();
endmodule

module defparam_top;
  defparam_leaf lanes [1:0] ();
  defparam lanes[1].VALUE = (257'h1 << 256),
           lanes[0].VALUE = 257'h2;
  generate
    if (1) begin : active
      defparam_leaf child ();
      defparam child.VALUE = 257'h3;
    end
  endgenerate
endmodule
)",
        Language::Verilog2005);
    require(parsed.ok(), "Verilog defparam declarations must parse");
    require(
        parsed.design.units.size() == 2U,
        "defparam frontend unit count");
    const auto& top = parsed.design.units.back();
    require(
        top.verilog_defparams.size() == 2U,
        "comma-separated module defparams are retained");
    require(
        top.verilog_defparams.front().path.size() == 2U
            && top.verilog_defparams.front().path.front().name == "lanes"
            && top.verilog_defparams.front().path.front().indices.size() == 1U
            && top.verilog_defparams.front().path.front().indices.front().text
                == "1"
            && top.verilog_defparams.front().path.back().name == "VALUE",
        "indexed defparam hierarchy is structured");
    require(
        top.generate_regions.size() == 1U
            && top.generate_regions.front().then_body.verilog_defparams.size()
                == 1U,
        "generated defparam remains branch-owned");
}

void test_systemverilog_generate_scope_revision()
{
    const auto parsed = parse_verilog(
        SourceText { "generate-scope-revision.sv", R"(
module generate_scope_revision;
  parameter int genblk2 = 0;
  if (1) begin : explicit_first
    logic selected;
  end
  if (1) logic collision_avoided;
  if (0) logic rejected; else logic selected_else;
  if (0) logic first;
  else if (1) logic direct_selected;
  else logic direct_rejected;
  for (genvar index = 0; index < 1; index++) begin
    logic iterative;
    if (1) logic nested;
  end
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(parsed.ok(), "revised generate naming must parse");
    const auto& regions = parsed.design.units.front().generate_regions;
    require(
        regions.size() == 5U
            && regions[0].then_scope == "explicit_first"
            && regions[1].then_scope == "genblk02"
            && regions[2].then_scope == "genblk3"
            && regions[2].else_scope == "genblk3"
            && regions[3].then_scope == "genblk4"
            && regions[3].else_scope.empty()
            && regions[3].else_body.generate_regions.size() == 1U
            && regions[3].else_body.generate_regions.front().then_scope
                == "genblk4"
            && regions[3].else_body.generate_regions.front().else_scope
                == "genblk4"
            && regions[4].then_scope == "genblk5"
            && regions[4].then_body.generate_regions.size() == 1U
            && regions[4].then_body.generate_regions.front().then_scope
                == "genblk1",
        "one enclosing-scope ordinal owns alternatives, direct nesting, and nested scopes");

    const auto future_explicit = parse_verilog(
        SourceText { "generate-future-explicit.sv", R"(
module generate_future_explicit;
  if (1) logic first;
  if (1) begin : genblk1
    logic second;
  end
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        future_explicit.ok()
            && future_explicit.design.units.front().generate_regions[0]
                   .then_scope == "genblk01"
            && future_explicit.design.units.front().generate_regions[1]
                   .then_scope == "genblk1",
        "implicit names avoid explicit declarations appearing later in the scope");

    const auto same_scheme = parse_verilog(
        SourceText { "generate-same-scheme.sv", R"(
module generate_same_scheme;
  if (1) begin : shared
    logic selected;
  end else begin : shared
    logic rejected;
  end
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        same_scheme.ok(),
        "alternative blocks in one conditional scheme may share a name");

    const auto collision = parse_verilog(
        SourceText { "generate-explicit-collision.sv", R"(
module generate_explicit_collision;
  logic shared;
  if (1) begin : shared
    logic selected;
  end
  if (1) begin : repeated
    logic first;
  end
  if (1) begin : repeated
    logic second;
  end
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        !collision.ok() && has_code(collision, "FSIM-SV-SEM-386"),
        "explicit generate names cannot collide across declarations or schemes");

    const auto generated_classes = parse_verilog(
        SourceText { "generated-interface-class-2023.sv", R"(
module generated_interface_class;
  if (1) begin : selected
    class Worker;
    endclass
    interface class Contract;
      pure virtual function int observe(input int value);
    endclass
  end
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        generated_classes.ok()
            && generated_classes.design.units.front().generate_regions.front()
                   .then_body.systemverilog_classes.size() == 2U
            && !generated_classes.design.units.front().generate_regions.front()
                    .then_body.systemverilog_classes.front().is_interface
            && generated_classes.design.units.front().generate_regions.front()
                   .then_body.systemverilog_classes.back().is_interface,
        "selected generate blocks retain ordinary and 2023 interface classes");

    const auto retained_interface_class = parse_verilog(
        SourceText { "generated-interface-class-2017.sv", R"(
module generated_interface_class;
  if (1) begin : selected
    interface class Contract;
      pure virtual function int observe(input int value);
    endclass
  end
endmodule
)" },
        StandardRevision::SystemVerilog2017);
    require(
        !retained_interface_class.ok()
            && has_code(retained_interface_class, "FSIM-SV-PARSE-387"),
        "an interface class in a generate block remains isolated to 2023");
}

} // namespace fsim::tests::frontend
