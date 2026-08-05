// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>
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
  logic [31:0] bits;
  real real_value;
  time time_value;
  chandle handle_value;
  logic [7:0] memory [3:0];
  typedef struct packed {
    logic [3:0] tag;
    logic [3:0] data;
  } packet_t;
  packet_t packet_memory [1:0];

  task automatic read_one(
      input integer file_handle,
      output string value,
      output integer result);
    result = $fgets(value, file_handle);
  endtask

  initial begin
    handle = $fopen(path, "w+");
    $fdisplay(handle, "value=%0d", 7);
    $fwrite(handle, " scalar=%g", real_value);
    $fwrite(handle, " sci=%e", real_value);
    $fwrite(handle, " fixed=%f", real_value);
    $fwrite(handle, "/%t", time_value);
    $fwrite(handle, "/%0h", handle_value);
    $fwrite(handle, "%s", path);
    read_one(handle, line, count);
    status = $fgetc(handle);
    status = $ungetc(status, handle);
    status = $feof(handle);
    status = $ferror(handle, error);
    status = $fscanf(handle, "%d", count);
    status = $sscanf("value=2a name=ok", "value=%h name=%s", count, line);
    status = $sscanf("real=1.25 time=17 handle=0",
                     "real=%g time=%d handle=%h",
                     real_value, time_value, handle_value);
    status = $fread(bits, handle);
    status = $fread(real_value, handle);
    status = $fread(time_value, handle);
    status = $fread(handle_value, handle);
    status = $fread(memory, handle, 2, 2);
    status = $fread(packet_memory, handle, 1, 1);
    status = $ftell(handle);
    status = $fseek(handle, -2, 1);
    status = $rewind(handle);
    $fflush(handle);
    $fflush();
    $writememh("dump.hex", memory);
    $writememb("dump.bin", memory, 2, 1);
    $writememh("packets.hex", packet_memory);
    $readmemh("packets.hex", packet_memory);
    $fclose(handle);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated =
      fsim::elaboration::elaborate(
          parsed.design, "file_lowering");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
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
              return fsim::runtime::simir::operation_holds<Type>(operation);
            });
      };
  assert(count(FileOpen{}) == 1);
  assert(count(FileWriteFormatted{}) == 6);
  assert(count(FileWriteString{}) == 1);
  assert(count(FileReadLine{}) == 3);
  assert(count(FileEndOfFile{}) == 1);
  assert(count(FileErrorStatus{}) == 1);
  assert(count(FileScan{}) == 3);
  assert(count(FileBinaryRead{}) == 6);
  assert(count(FilePosition{}) == 3);
  assert(count(FileFlush{}) == 2);
  assert(count(LoadMemory{}) == 4);
  assert(count(FileClose{}) == 1);
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value =
            fsim::runtime::simir::operation_get_if<FileWriteFormatted>(&operation);
        return value != nullptr
            && value->width == 32
            && value->format == OutputFormat::decimal
            && value->prefix == "value="
            && value->newline;
      }));
  assert(std::ranges::count_if(
      operations,
      [](const auto& operation) {
        const auto* value =
            fsim::runtime::simir::operation_get_if<FileWriteFormatted>(&operation);
        return value != nullptr
            && value->format >= OutputFormat::real_scientific
            && value->format <= OutputFormat::real_general
            && value->scalar_kind == fsim::runtime::SystemVerilogScalarKind::Real;
      }) == 3);
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value =
            fsim::runtime::simir::operation_get_if<FileWriteFormatted>(&operation);
        return value != nullptr && value->format == OutputFormat::real_general
            && value->width == 64
            && value->scalar_kind == fsim::runtime::SystemVerilogScalarKind::Real;
      }));
  assert(std::ranges::count_if(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FileReadLine>(&operation);
        return value != nullptr
            && value->kind != FileReadKind::line;
      }) == 2);
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FileScan>(&operation);
        return value != nullptr && value->string_source
            && value->conversions.size() == 2
            && value->conversions[0].format
                == InputScanFormat::hexadecimal
            && value->conversions[1].target.kind
                == InputScanTargetKind::string_object;
      }));
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FileScan>(&operation);
        return value != nullptr && value->string_source
            && value->conversions.size() == 3
            && value->conversions[0].format == InputScanFormat::real
            && value->conversions[0].target.scalar_kind
                == fsim::runtime::SystemVerilogScalarKind::Real
            && value->conversions[1].target.scalar_kind
                == fsim::runtime::SystemVerilogScalarKind::Time
            && value->conversions[2].target.scalar_kind
                == fsim::runtime::SystemVerilogScalarKind::Chandle;
      }));
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FileBinaryRead>(&operation);
        return value != nullptr
            && value->target_kind
                == FileBinaryTargetKind::packed_signal
            && value->width == 32 && !value->has_start;
      }));
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FileBinaryRead>(&operation);
        return value != nullptr
            && value->target_kind == FileBinaryTargetKind::packed_signal
            && value->scalar_kind
                == fsim::runtime::SystemVerilogScalarKind::Chandle
            && value->width == 64 && value->two_state;
      }));
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FileBinaryRead>(&operation);
        return value != nullptr
            && value->target_kind
                == FileBinaryTargetKind::container_object
            && value->width == 8 && value->has_start
            && value->has_count;
      }));
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FilePosition>(&operation);
        return value != nullptr && value->kind == FilePositionKind::seek
            && value->offset != value->origin;
      }));
  assert(std::ranges::count_if(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<FileFlush>(&operation);
        return value != nullptr && value->all;
      }) == 1);
  assert(std::ranges::count_if(
      operations,
      [](const auto& operation) {
        const auto* value = fsim::runtime::simir::operation_get_if<LoadMemory>(&operation);
        return value != nullptr && value->write;
      }) == 3);

  const auto invalid = fsim::frontend::parse_text(
      "file-invalid.sv",
      R"(
module file_invalid;
  integer handle;
  integer result;
  string value;
  real real_value;
  logic [7:0] bits;
  logic [7:0] memory [3:0];
  logic [7:0] matrix [1:0][0:1];
  initial begin
    handle = $fopen(7, "r");
    result = $fgets(bits, handle);
    result = $feof(value);
    result = $ferror(handle, bits);
    result = $fscanf(value, "%d", result);
    result = $sscanf(value, "%d", value);
    result = $sscanf(value, "%q", result);
    $fdisplay(handle, "%g", result);
    result = $sscanf("1", "%d", real_value);
    result = $fread(value, handle);
    result = $fread(bits, handle, 0);
    result = $fread(memory, value, 0, 1);
    result = $fread(matrix, handle);
    $readmemh("matrix.hex", matrix);
    $writememh("matrix.hex", matrix);
    result = $fseek(handle, value, 0);
    $fflush(value);
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
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-011"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-012"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-013"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-014"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-015"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-016"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVMEMORY-003"));
}

}  // namespace fsim::tests::elaboration
