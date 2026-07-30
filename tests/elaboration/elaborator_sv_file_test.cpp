// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace fsim::tests::elaboration {

void test_systemverilog_file_lowering() {
  using namespace fsim::runtime::simir;

  const auto parsed = fsim::frontend::parse_text(
      "file-lowering.sv",
      R"(
module file_lowering;
  integer handle;
  integer count;
  integer status;
  string path = "data.txt";
  string line;
  string error;

  task automatic read_one(
      input integer file_handle,
      output string value,
      output integer result);
    result = $fgets(value, file_handle);
  endtask

  initial begin
    handle = $fopen(path, "w+");
    $fdisplay(handle, "value=%0d", 7);
    $fwrite(handle, "%s", path);
    read_one(handle, line, count);
    status = $feof(handle);
    status = $ferror(handle, error);
    $fclose(handle);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated =
      fsim::elaboration::elaborate(
          parsed.design, "file_lowering");
  assert(elaborated.ok());
  assert(elaborated.design->processes().size() == 1);
  const auto& operations =
      elaborated.design->processes().front().operations;
  const auto count =
      [&](const auto& example) {
        using Type = std::decay_t<decltype(example)>;
        return std::ranges::count_if(
            operations,
            [](const auto& operation) {
              return std::holds_alternative<Type>(operation);
            });
      };
  assert(count(FileOpen{}) == 1);
  assert(count(FileWriteFormatted{}) == 1);
  assert(count(FileWriteString{}) == 1);
  assert(count(FileReadLine{}) == 1);
  assert(count(FileEndOfFile{}) == 1);
  assert(count(FileErrorStatus{}) == 1);
  assert(count(FileClose{}) == 1);
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value =
            std::get_if<FileWriteFormatted>(&operation);
        return value != nullptr
            && value->width == 32
            && value->format == OutputFormat::decimal
            && value->prefix == "value="
            && value->newline;
      }));

  const auto invalid = fsim::frontend::parse_text(
      "file-invalid.sv",
      R"(
module file_invalid;
  integer handle;
  integer result;
  string value;
  logic [7:0] bits;
  initial begin
    handle = $fopen(7, "r");
    result = $fgets(bits, handle);
    result = $feof(value);
    result = $ferror(handle, bits);
    $fclose(value);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto rejected =
      fsim::elaboration::elaborate(
          invalid.design, "file_invalid");
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-001"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-002"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-003"));
}

}  // namespace fsim::tests::elaboration
