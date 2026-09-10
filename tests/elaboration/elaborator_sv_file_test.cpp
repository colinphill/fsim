// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>
#include <type_traits>
#include <variant>

namespace fsim::tests::elaboration {

void test_systemverilog_file_lowering()
{
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
  logic [136:0] wide_scan;
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
    $fwrite(handle, "%u", bits);
    $fwrite(handle, "%z", bits);
    $fwrite(handle, "%s", path);
    $fdisplay(handle, " pair=%0d/%s", 8, path);
    $fwrite(handle, path);
    read_one(handle, line, count);
    status = $fgetc(handle);
    status = $ungetc(status, handle);
    status = $feof(handle);
    status = $ferror(handle, error);
    status = $fscanf(handle, "%d", count);
    status = $fscanf(handle, "%u", bits);
    status = $fscanf(handle, "%z", bits);
    status = $sscanf("value=2a name=ok", "value=%h name=%s", count, line);
    status = $sscanf("real=1.25 time=17 handle=0",
                     "real=%g time=%d handle=%h",
                     real_value, time_value, handle_value);
    status = $sscanf("1", "%h", wide_scan);
    status = $fread(bits, handle);
    status = $fread(real_value, handle);
    status = $fread(time_value, handle);
    status = $fread(handle_value, handle);
    status = $fread(memory, handle, 2, 2);
    status = $fread(packet_memory, handle, 1, 1);
    status = $ftell(handle);
    status = $fseek(
        handle, 129'h1fffffffffffffffffffffffffffffffe, 1);
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
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "file_lowering");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->processes().size() == 1);
    const auto& operations = elaborated.design->processes().front().operations;
    const auto count =
        [&](const auto& example) {
            using Type = std::decay_t<decltype(example)>;
            return std::ranges::count_if(
                operations,
                [](const auto& operation) {
                    return fsim::runtime::simir::operation_holds<Type>(operation);
                });
        };
    assert(count(FileOpen { }) == 1);
    assert(count(FileWriteFormatted { }) == 9);
    assert(count(FileWriteString { }) == 3);
    assert(count(FileReadLine { }) == 3);
    assert(count(FileEndOfFile { }) == 1);
    assert(count(FileErrorStatus { }) == 1);
    assert(count(FileScan { }) == 6);
    assert(count(FileBinaryRead { }) == 6);
    assert(count(FilePosition { }) == 3);
    assert(count(FileFlush { }) == 2);
    assert(count(LoadMemory { }) == 4);
    assert(count(FileClose { }) == 1);
    assert(std::ranges::any_of(
        operations,
        [](const auto& operation) {
            const auto* value = fsim::runtime::simir::operation_get_if<FileWriteFormatted>(&operation);
            return value != nullptr
                && value->width == 32
                && value->format == OutputFormat::decimal
                && value->prefix == "value="
                && value->newline;
        }));
    assert(std::ranges::any_of(
        operations,
        [](const auto& operation) {
            const auto* value = fsim::runtime::simir::operation_get_if<FileScan>(&operation);
            return value != nullptr && value->string_source
                && value->conversions.size() == 1
                && value->conversions.front().target.width == 137;
        }));
    assert(std::ranges::count_if(
               operations,
               [](const auto& operation) {
                   const auto* value = fsim::runtime::simir::operation_get_if<FileWriteFormatted>(&operation);
                   return value != nullptr
                       && value->format >= OutputFormat::real_scientific
                       && value->format <= OutputFormat::real_general
                       && value->scalar_kind == fsim::runtime::SystemVerilogScalarKind::Real;
               })
        == 3);
    assert(std::ranges::count_if(
               operations,
               [](const auto& operation) {
                   const auto* value = operation_get_if<FileWriteFormatted>(
                       &operation);
                   return value != nullptr
                       && (value->format == OutputFormat::unformatted2
                           || value->format == OutputFormat::unformatted4);
               })
        == 2);
    assert(std::ranges::count_if(
               operations,
               [](const auto& operation) {
                   const auto* value = operation_get_if<FileScan>(&operation);
                   return value != nullptr && !value->string_source
                       && value->conversions.size() == 1U
                       && (value->conversions.front().format
                               == InputScanFormat::unformatted2
                           || value->conversions.front().format
                               == InputScanFormat::unformatted4);
               })
        == 2);

    const auto verilog = fsim::frontend::parse_text(
        "file-lowering.v",
        R"(
module verilog_file_lowering;
  integer handle;
  integer result;
  reg [136:0] line;
  reg [136:0] error_text;
  reg [136:0] memory [3:0];
  initial begin
    handle = $fopen("input.txt", "r");
    result = $fgets(line, handle);
    result = $fgetc(handle);
    result = $ungetc(result, handle);
    result = $feof(handle);
    result = $ferror(handle, error_text);
    result = $fscanf(handle, "%h", line);
    result = $sscanf("2a", "%h", line);
    result = $fread(line, handle);
    result = $ftell(handle);
    result = $fseek(handle, 0, 0);
    result = $rewind(handle);
    $fdisplay(handle, "value=%h", line);
    $fwrite(handle, "tail");
    $fflush(handle);
    $readmemh("input.hex", memory);
    $writememh("output.hex", memory);
    $fclose(handle);
  end
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog.ok());
    const auto verilog_elaborated = fsim::elaboration::elaborate(
        verilog.design, "verilog_file_lowering");
    if (!verilog_elaborated.ok()) {
        for (const auto& diagnostic : verilog_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(verilog_elaborated.ok());
    const auto& verilog_operations = verilog_elaborated.design->processes().front().operations;
    assert(std::ranges::any_of(
        verilog_operations,
        [](const auto& operation) {
            const auto* value = operation_get_if<FileReadLine>(&operation);
            return value != nullptr && value->kind == FileReadKind::line
                && value->target_kind == FileTextTargetKind::packed_signal
                && value->target_width == 137;
        }));
    assert(std::ranges::any_of(
        verilog_operations,
        [](const auto& operation) {
            const auto* value = operation_get_if<FileErrorStatus>(&operation);
            return value != nullptr
                && value->target_kind == FileTextTargetKind::packed_signal
                && value->target_width == 137;
        }));
    assert(std::ranges::any_of(
        operations,
        [](const auto& operation) {
            const auto* value = fsim::runtime::simir::operation_get_if<FileWriteFormatted>(&operation);
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
               })
        == 2);
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
               })
        == 1);
    assert(std::ranges::count_if(
               operations,
               [](const auto& operation) {
                   const auto* value = fsim::runtime::simir::operation_get_if<LoadMemory>(&operation);
                   return value != nullptr && value->write;
               })
        == 3);

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
  logic [7:0] dynamic_memory [];
  initial begin
    handle = $fopen(7, "r");
    result = $fgets("bad", handle);
    result = $feof(value);
    result = $ferror(handle, "bad");
    result = $fscanf(value, "%d", result);
    result = $sscanf(value, "%d", value);
    result = $sscanf(value, "%q", result);
    value = $sformatf("%u", bits);
    result = $value$plusargs("RAW=%z", bits);
    $fdisplay(handle, "%g", result);
    result = $sscanf("1", "%d", real_value);
    result = $fread(value, handle);
    result = $fread(bits, handle, 0);
    result = $fread(memory, value, 0, 1);
    result = $fread(matrix, handle);
    $readmemh("matrix.hex", dynamic_memory);
    $writememh("matrix.hex", dynamic_memory);
    result = $fseek(handle, value, 0);
    $fflush(value);
    $fclose(value);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid.ok());
    const auto rejected = fsim::elaboration::elaborate(
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
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVSTRING-019"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVCLI-002"));
}

} // namespace fsim::tests::elaboration
