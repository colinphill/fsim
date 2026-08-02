// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_textio_lowering() {
  using namespace fsim::runtime::simir;
  const auto parsed = fsim::frontend::parse_text(
      "textio.vhd",
      R"(
entity textio_test is end entity;
architecture rtl of textio_test is
  type text_file is file of string;
begin
  worker: process
    file input_file : text_file open read_mode is "input.txt";
    file output_file : text_file open write_mode is "output.txt";
    variable input_line, output_line : line;
    variable value : integer;
    variable flag, good : boolean;
    variable digit : bit;
  begin
    readline(input_file, input_line);
    read(input_line, value, good);
    read(input_line, flag);
    read(input_line, digit);
    write(output_line, value, right, 8);
    write(output_line, flag);
    write(output_line, digit, left, 2);
    writeline(output_file, output_line);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto result = fsim::elaboration::elaborate(
      parsed.design, "vhdl:work.textio_test(rtl)");
  if (!result.ok()) {
    for (const auto& diagnostic : result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(result.ok());
  const auto& operations = result.design->processes().front().operations;
  assert(std::ranges::any_of(operations, [](const Operation& operation) {
    const auto* line = operation_get_if<FileReadLine>(&operation);
    return line != nullptr && line->vhdl_textio;
  }));
  assert(std::ranges::count_if(operations, [](const Operation& operation) {
    const auto* scan = operation_get_if<FileScan>(&operation);
    return scan != nullptr && scan->string_source
        && scan->consume_string_source;
  }) == 3);
  assert(std::ranges::any_of(operations, [](const Operation& operation) {
    const auto* scan = operation_get_if<FileScan>(&operation);
    return scan != nullptr && scan->success.has_value()
        && !scan->require_assignments;
  }));
  assert(std::ranges::any_of(operations, [](const Operation& operation) {
    const auto* output = operation_get_if<FileWriteString>(&operation);
    return output != nullptr && output->clear_source;
  }));

  const auto invalid = fsim::frontend::parse_text(
      "invalid_textio.vhd",
      R"(
entity invalid_textio is end entity;
architecture rtl of invalid_textio is
  type integer_file is file of integer;
begin
  worker: process
    file data : integer_file;
    variable buffer_value : line;
    variable value, good, field : integer;
    variable choice : side := left;
    variable level : severity_level := warning;
  begin
    readline(data, buffer_value);
    read(buffer_value, level);
    read(buffer_value, value, good);
    write(buffer_value, value, choice);
    write(buffer_value, value, right, field);
    write(buffer_value, level);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design, "vhdl:work.invalid_textio(rtl)");
  assert(!rejected.ok());
  for (const auto* code : {
           "FSIM-ELAB-VHTEXTIO-001",
           "FSIM-ELAB-VHTEXTIO-005",
           "FSIM-ELAB-VHTEXTIO-006",
           "FSIM-ELAB-VHTEXTIO-007",
           "FSIM-ELAB-VHTEXTIO-008",
           "FSIM-ELAB-VHTEXTIO-009"}) {
    assert(has_diagnostic(rejected, code));
  }
}

} // namespace fsim::tests::elaboration
