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
  logic [7:0] strobe_value;
  logic [7:0] memory [3:0];
  string string_memory [0:1];
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
    status = $sscanf("X7", "%mX%d", line, count);
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
    $readmemh("strings.hex", string_memory);
    $writememb("strings.bin", string_memory);
    $fstrobe(
        handle, "strobe=%m/%0t/%0h", $time, strobe_value);
    $fclose(handle);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
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
    assert(count(FileScan { }) == 7);
    assert(count(FileBinaryRead { }) == 6);
    assert(count(FilePosition { }) == 3);
    assert(count(FileFlush { }) == 2);
    assert(count(LoadMemory { }) == 6);
    assert(count(MonitorInstall { }) == 1);
    assert(count(FileClose { }) == 1);
    const auto monitor = std::ranges::find_if(
        operations,
        [](const auto& operation) {
            return operation_holds<MonitorInstall>(operation);
        });
    assert(monitor != operations.end());
    const auto* install = operation_get_if<MonitorInstall>(&*monitor);
    assert(install != nullptr && install->file_handle
        && install->one_shot && install->newline
        && install->values.size() == 2U
        && install->values[0].kind == MonitorValueKind::time
        && install->values[0].prefix
            == "strobe=file_lowering/"
        && install->values[0].suppress_leading_zero
        && !install->values[0].use_timeformat_width
        && install->values[1].kind == MonitorValueKind::signal
        && install->values[1].format == OutputFormat::hexadecimal
        && install->values[1].prefix == "/"
        && install->trailing_text.empty());
    assert(std::ranges::any_of(
        elaborated.design->processes().front().container_register_types,
        [](const auto& type) {
            return type.fixed
                && type.element_kind == ContainerElementKind::String
                && type.index_left == 0 && type.index_right == 1;
        }));
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
    const auto verilog_elaborated = compile_and_elaborate(
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
            const auto* value
                = operation_get_if<FileScan>(&operation);
            return value != nullptr && value->string_source
                && value->hierarchy_path == "file_lowering"
                && value->conversions.size() == 2U
                && value->conversions[0].format
                    == InputScanFormat::hierarchy
                && value->conversions[1].prefix == "X"
                && value->conversions[1].format
                    == InputScanFormat::decimal;
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
        == 4);

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
  logic [7:0] named_memory [string];
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
    $readmemh("matrix.hex", named_memory);
    $writememh("matrix.hex", named_memory);
    $fstrobe(handle, "%0h", bits + 1'b1);
    result = $fseek(handle, value, 0);
    $fflush(value);
    $fclose(value);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid.ok());
    const auto rejected = compile_and_elaborate(
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
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVFILE-007"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVMEMORY-003"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVSTRING-019"));
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVCLI-002"));

    const auto width_invalid = fsim::frontend::parse_text(
        "file-invalid-width.sv",
        R"(
module file_invalid_width;
  integer result;
  string value;
  initial result = $sscanf("x", "%5m", value);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(width_invalid.ok());
    const auto width_rejected = compile_and_elaborate(
        width_invalid.design, "file_invalid_width");
    assert(!width_rejected.ok());
    assert(has_diagnostic(width_rejected, "FSIM-ELAB-SVFILE-011"));

    const auto expect_unformatted_output_rejected = [](
                                                        const std::string_view source,
                                                        const std::string_view top) {
        const auto parsed = fsim::frontend::parse_text(
            "file-invalid-unformatted-output.sv",
            source,
            fsim::frontend::Language::SystemVerilog2017);
        assert(parsed.ok());
        const auto rejected = compile_and_elaborate(parsed.design, top);
        assert(!rejected.ok());
        assert(has_diagnostic(rejected, "FSIM-ELAB-SVSTRING-020"));
    };
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_class_value;
  class holder;
  endclass
  holder object_handle;
  logic [31:0] packed_output;
  initial $sformat(packed_output, "%u", object_handle);
endmodule
)",
        "file_unformatted_class_value");
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_string_value;
  string string_value;
  logic [31:0] packed_output;
  initial $sformat(packed_output, "%z", string_value);
endmodule
)",
        "file_unformatted_string_value");
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_unpacked_value;
  logic [7:0] unpacked_value [0:1];
  logic [31:0] packed_output;
  initial $sformat(packed_output, "%u", unpacked_value);
endmodule
)",
        "file_unformatted_unpacked_value");
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_event_value;
  event event_value;
  logic [31:0] packed_output;
  initial $sformat(packed_output, "%u", event_value);
endmodule
)",
        "file_unformatted_event_value");
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_real_value;
  real real_value;
  logic [63:0] packed_output;
  initial $sformat(packed_output, "%u", real_value);
endmodule
)",
        "file_unformatted_real_value");
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_chandle_value;
  chandle native_handle;
  logic [63:0] packed_output;
  initial $sformat(packed_output, "%u", native_handle);
endmodule
)",
        "file_unformatted_chandle_value");
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_real_target;
  real real_output;
  initial $sformat(real_output, "%u", 8'h5a);
endmodule
)",
        "file_unformatted_real_target");
    expect_unformatted_output_rejected(
        R"(
module file_unformatted_class_target;
  class holder;
  endclass
  holder object_handle;
  initial $sformat(object_handle, "%u", 8'h5a);
endmodule
)",
        "file_unformatted_class_target");

    const auto mixed_swrite_format = fsim::frontend::parse_text(
        "file-mixed-swrite-format.sv",
        R"(
module file_mixed_swrite_format;
  logic [127:0] output_text;
  initial $swrite(output_text, 1, "x=%0d", 2);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mixed_swrite_format.ok());
    const auto mixed_swrite_rejected = compile_and_elaborate(
        mixed_swrite_format.design, "file_mixed_swrite_format");
    assert(!mixed_swrite_rejected.ok());
    assert(has_diagnostic(mixed_swrite_rejected, "FSIM-ELAB-SVSTRING-020"));

    const auto unknown_format_read_only = fsim::frontend::parse_text(
        "file-unknown-format-read-only.sv",
        R"(
module file_unknown_format_read_only;
  reg [80*8:1] source_text;
  parameter logic [7:0] target = 8'h5a;
  integer result;
  initial result = $sscanf(source_text, 'bx, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_read_only.ok());
    const auto read_only_rejected = compile_and_elaborate(
        unknown_format_read_only.design, "file_unknown_format_read_only");
    assert(!read_only_rejected.ok());
    assert(has_diagnostic(
        read_only_rejected, "FSIM-ELAB-SVFILE-012"));

    const auto unknown_format_net = fsim::frontend::parse_text(
        "file-unknown-format-net.sv",
        R"(
module file_unknown_format_net;
  reg [80*8:1] source_text;
  wire [7:0] target;
  integer result;
  initial result = $sscanf(source_text, 'bz, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_net.ok());
    const auto net_rejected = compile_and_elaborate(
        unknown_format_net.design, "file_unknown_format_net");
    assert(!net_rejected.ok());
    assert(has_diagnostic(net_rejected, "FSIM-ELAB-SVFILE-012"));

    const auto unknown_format_read_only_port = fsim::frontend::parse_text(
        "file-unknown-format-read-only-port.sv",
        R"(
module file_unknown_format_read_only_port(input logic [7:0] target);
  reg [80*8:1] source_text;
  integer result;
  initial result = $sscanf(source_text, 'bx, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_read_only_port.ok());
    const auto read_only_port_rejected = compile_and_elaborate(
        unknown_format_read_only_port.design,
        "file_unknown_format_read_only_port");
    assert(!read_only_port_rejected.ok());
    assert(has_diagnostic(
        read_only_port_rejected, "FSIM-ELAB-SVFILE-012"));

    const auto unknown_format_unpacked_source = fsim::frontend::parse_text(
        "file-unknown-format-unpacked-source.sv",
        R"(
module file_unknown_format_unpacked_source;
  logic [7:0] source_text [1:0];
  logic [7:0] target;
  integer result;
  initial result = $sscanf(source_text, 'bx, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_unpacked_source.ok());
    const auto unpacked_source_rejected = compile_and_elaborate(
        unknown_format_unpacked_source.design,
        "file_unknown_format_unpacked_source");
    assert(!unpacked_source_rejected.ok());
    assert(has_diagnostic(
        unpacked_source_rejected, "FSIM-ELAB-SVFILE-011"));

    const auto unknown_format_dynamic_source = fsim::frontend::parse_text(
        "file-unknown-format-dynamic-source.sv",
        R"(
module file_unknown_format_dynamic_source;
  logic [7:0] source_text [];
  logic [7:0] target;
  integer result;
  initial result = $sscanf(source_text, 'bz, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_dynamic_source.ok());
    const auto dynamic_source_rejected = compile_and_elaborate(
        unknown_format_dynamic_source.design,
        "file_unknown_format_dynamic_source");
    assert(!dynamic_source_rejected.ok());
    assert(has_diagnostic(
        dynamic_source_rejected, "FSIM-ELAB-SVFILE-011"));

    const auto unknown_format_class_source = fsim::frontend::parse_text(
        "file-unknown-format-class-source.sv",
        R"(
class file_scan_source_handle; endclass
module file_unknown_format_class_source;
  file_scan_source_handle source_text;
  logic [7:0] target;
  integer result;
  initial result = $sscanf(source_text, 'bx, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_class_source.ok());
    const auto class_source_rejected = compile_and_elaborate(
        unknown_format_class_source.design,
        "file_unknown_format_class_source");
    assert(!class_source_rejected.ok());
    assert(has_diagnostic(
        class_source_rejected, "FSIM-ELAB-SVFILE-011"));

    const auto unknown_format_event_source = fsim::frontend::parse_text(
        "file-unknown-format-event-source.sv",
        R"(
module file_unknown_format_event_source;
  event source_event;
  logic [7:0] target;
  integer result;
  initial result = $sscanf(source_event, 'bx, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_event_source.ok());
    const auto event_source_rejected = compile_and_elaborate(
        unknown_format_event_source.design,
        "file_unknown_format_event_source");
    assert(!event_source_rejected.ok());
    assert(has_diagnostic(
        event_source_rejected, "FSIM-ELAB-SVFILE-011"));

    const auto unknown_format_class_target = fsim::frontend::parse_text(
        "file-unknown-format-class-target.sv",
        R"(
class file_scan_target_handle; endclass
module file_unknown_format_class_target;
  reg [80*8:1] source_text;
  file_scan_target_handle target;
  integer result;
  initial result = $sscanf(source_text, 'bz, target);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_format_class_target.ok());
    const auto class_target_rejected = compile_and_elaborate(
        unknown_format_class_target.design,
        "file_unknown_format_class_target");
    assert(!class_target_rejected.ok());
    assert(has_diagnostic(
        class_target_rejected, "FSIM-ELAB-SVFILE-012"));
}

} // namespace fsim::tests::elaboration
