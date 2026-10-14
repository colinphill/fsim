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
  const auto result = compile_and_elaborate(
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

  const auto dynamic_field = fsim::frontend::parse_text(
      "dynamic_textio.vhd",
      R"(
entity dynamic_textio is end entity;
architecture rtl of dynamic_textio is
begin
  worker: process
    variable buffer_value : line;
    variable field : natural := 4;
  begin
    write(buffer_value, string'("xy"), right, field);
    write(buffer_value, bit'('1'), right, field);
    write(buffer_value, bit_vector'("101"), right, field);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(dynamic_field.ok());
  const auto dynamic_result = compile_and_elaborate(
      dynamic_field.design, "vhdl:work.dynamic_textio(rtl)");
  if (!dynamic_result.ok()) {
    for (const auto& diagnostic : dynamic_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(dynamic_result.ok());
  const auto& dynamic_operations
      = dynamic_result.design->processes().front().operations;
  assert(std::ranges::any_of(
      dynamic_operations, [](const Operation& operation) {
        const auto* method = operation_get_if<StringMethod>(&operation);
        return method != nullptr
            && method->operation
                == StringMethodOperator::format_string_dynamic_field;
      }));
  assert(std::ranges::any_of(
      dynamic_operations, [](const Operation& operation) {
        const auto* method = operation_get_if<StringMethod>(&operation);
        return method != nullptr
            && method->operation
                == StringMethodOperator::format_packed_dynamic_field;
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
    variable string_value : string(1 to 4) := "1010";
    variable choice : side := left;
    variable level : severity_level := warning;
  begin
    readline(data, buffer_value);
    read(buffer_value, level);
    read(buffer_value, value, good);
    write(buffer_value, value, choice);
    write(buffer_value, value, right, field);
    hwrite(buffer_value, string_value);
    hwrite(buffer_value, string'("1010"));
    write(buffer_value, level);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid.ok());
  const auto rejected = compile_and_elaborate(
      invalid.design, "vhdl:work.invalid_textio(rtl)");
  assert(!rejected.ok());
  for (const auto* code : {
           "FSIM-ELAB-VHTEXTIO-001",
           "FSIM-ELAB-VHTEXTIO-005",
           "FSIM-ELAB-VHTEXTIO-006",
           "FSIM-ELAB-VHTEXTIO-007",
           "FSIM-ELAB-VHTEXTIO-009"}) {
    assert(has_diagnostic(rejected, code));
  }
  // Two rejected HWRITE forms plus the preexisting unsupported enum WRITE.
  assert(std::ranges::count_if(
      rejected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VHTEXTIO-009";
      }) == 3);

  const auto negative_static_field = fsim::frontend::parse_text(
      "negative_static_textio_field.vhd",
      R"(
entity negative_static_textio_field is end entity;
architecture rtl of negative_static_textio_field is
begin
  worker: process
    variable buffer_value : line;
  begin
    write(buffer_value, string'("x"), right, -1);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(negative_static_field.ok());
  const auto negative_field_result = compile_and_elaborate(
      negative_static_field.design,
      "vhdl:work.negative_static_textio_field(rtl)");
  if (!negative_field_result.ok()) {
    for (const auto& diagnostic : negative_field_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(negative_field_result.ok());
  const auto& negative_field_operations
      = negative_field_result.design->processes().front().operations;
  assert(std::ranges::any_of(
      negative_field_operations, [](const Operation& operation) {
        const auto* method = operation_get_if<StringMethod>(&operation);
        return method != nullptr
            && method->operation
                == StringMethodOperator::format_string_dynamic_field;
      }));

  const auto legacy_radix_textio = fsim::frontend::parse_text(
      "textio_radix_before_2008.vhd",
      R"(
entity TextIO_Radix_Before_2008 is end entity;
architecture rtl of TextIO_Radix_Before_2008 is
begin
  worker: process
    variable buffer_value : line;
    variable value : bit_vector(3 downto 0) := "1010";
    variable good : boolean;
  begin
    hread(buffer_value, value, good);
    hex_read(buffer_value, value, good);
    oread(buffer_value, value, good);
    octal_read(buffer_value, value, good);
    hwrite(buffer_value, value);
    hex_write(buffer_value, value);
    owrite(buffer_value, value);
    octal_write(buffer_value, value);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2002);
  assert(legacy_radix_textio.ok());
  const auto legacy_rejected = compile_and_elaborate(
      legacy_radix_textio.design,
      "vhdl:work.textio_radix_before_2008(rtl)");
  assert(!legacy_rejected.ok());
  assert(std::ranges::count_if(
      legacy_rejected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VHSTD-001";
      }) == 8);

  const auto user_line = fsim::frontend::parse_text(
      "user_line_nominal.vhd",
      R"(
entity user_line_nominal is end entity;
architecture rtl of user_line_nominal is
  type line is access string;
  procedure accept_line(value : inout line) is
  begin
    null;
  end procedure;
begin
  check : process is
    variable standard_line : std.textio.line;
  begin
    accept_line(standard_line);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(user_line.ok());
  const auto user_line_rejected = compile_and_elaborate(
      user_line.design, "vhdl:work.user_line_nominal(rtl)");
  assert(!user_line_rejected.ok());
  assert(has_diagnostic(
      user_line_rejected, "FSIM-ELAB-VHOVER-005"));
}

} // namespace fsim::tests::elaboration
