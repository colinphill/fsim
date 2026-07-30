// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"

#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error{std::string{message}};
  }
}

[[nodiscard]] bool has_code(
    const fsim::frontend::ParseResult& result,
    const std::string_view code) {
  return std::ranges::any_of(
      result.diagnostics,
      [code](const auto& diagnostic) {
        return diagnostic.code == code;
      });
}

}  // namespace

void test_systemverilog_text_files() {
  using namespace fsim::frontend;

  const auto parsed = parse_text(
      "text-files.sv",
      R"(
module text_files;
  integer handle;
  integer count;
  integer status;
  string path = "input.txt";
  string line;
  string error;
  initial begin : io
    handle = $fopen(path, "r");
    count = $fgets(line, handle);
    status = $feof(handle);
    status = $ferror(handle, error);
    $fdisplay(handle, "count=%0d", count);
    $fwrite(handle, "%s", line);
    $fclose(handle);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded file syntax parses");
  const auto* unit =
      parsed.design.find(UnitKind::VerilogModule, "text_files");
  require(
      unit != nullptr && unit->processes.size() == 1,
      "file fixture retains one process");
  const auto& block = unit->processes[0].statements.at(0);
  require(
      block.kind == StatementKind::Block
          && block.statements.size() == 7,
      "file statements remain ordered in their block");
  require(
      block.statements[0].kind == StatementKind::Assignment
          && block.statements[0].value.kind
              == ExpressionKind::Call
          && block.statements[0].value.text == "$fopen"
          && block.statements[0].value.operands.size() == 2,
      "$fopen retains filename and mode operands");
  require(
      block.statements[1].value.text == "$fgets"
          && block.statements[2].value.text == "$feof"
          && block.statements[3].value.text == "$ferror",
      "file system functions retain distinct call identities");
  require(
      block.statements[4].kind == StatementKind::FileDisplay
          && block.statements[4].output_newline
          && block.statements[4].output_format
              == OutputFormat::Decimal
          && block.statements[5].kind
              == StatementKind::FileDisplay
          && !block.statements[5].output_newline
          && block.statements[5].output_format
              == OutputFormat::String,
      "file output tasks retain bounded formatting");
  require(
      block.statements[6].kind == StatementKind::FileClose
          && block.statements[6].file_handle.text == "handle",
      "$fclose retains its handle expression");

  const auto invalid_arity = parse_text(
      "file-arity.sv",
      R"(
module file_arity;
  integer handle;
  string line;
  initial begin
    handle = $fopen("only-name");
    handle = $fgets(line);
    handle = $feof(handle, handle);
    handle = $ferror(handle);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_arity.ok()
          && has_code(invalid_arity, "FSIM-SV-SEM-075"),
      "file system function arity is diagnosed");

  const auto invalid_format = parse_text(
      "file-format.sv",
      R"(
module file_format;
  integer handle;
  initial begin
    $fdisplay(handle, "%0d %0d", 1, 2);
    $fwrite(handle, handle);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_format.ok()
          && has_code(invalid_format, "FSIM-SV-SEM-076"),
      "multi-value and nonliteral file output is diagnosed");

  const auto verilog = parse_text(
      "file-verilog.v",
      R"(
module file_verilog;
  integer handle;
  initial begin
    handle = $fopen("file.txt", "r");
    $fclose(handle);
  end
endmodule
)",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && has_code(verilog, "FSIM-SV-SEM-074"),
      "bounded file operations require SystemVerilog-2017");
}

}  // namespace fsim::tests::frontend
