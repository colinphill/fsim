// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"

#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error { std::string { message } };
        }
    }

    [[nodiscard]] bool has_code(
        const fsim::frontend::ParseResult& result,
        const std::string_view code)
    {
        return std::ranges::any_of(
            result.diagnostics,
            [code](const auto& diagnostic) {
                return diagnostic.code == code;
            });
    }

} // namespace

void test_systemverilog_text_files()
{
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
  logic [31:0] bits;
  logic [7:0] memory [3:0];
  initial begin : io
    status = $fopen("channel.txt");
    handle = $fopen(path, "r");
    count = $fgets(line, handle);
    count = $fgetc(handle);
    count = $ungetc(count, handle);
    status = $feof(handle);
    status = $ferror(handle, error);
    count = $fscanf(handle, "%d %s", status, line);
    count = $sscanf(line, "%*s %2h", status);
    count = $fread(bits, handle);
    count = $fread(memory, handle, 2, 2);
    status = $ftell(handle);
    status = $fseek(handle, -2, 1);
    status = $rewind(handle);
    $fflush(handle);
    $fflush();
    $fdisplay(handle, "count=%0d line=%s", count, line);
    $fwrite(handle, line);
    $fclose(handle);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(parsed.ok(), "bounded file syntax parses");
    const auto* unit = parsed.design.find(UnitKind::VerilogModule, "text_files");
    require(
        unit != nullptr && unit->processes.size() == 1,
        "file fixture retains one process");
    const auto& block = unit->processes[0].statements.at(0);
    require(
        block.kind == StatementKind::Block
            && block.statements.size() == 19,
        "file statements remain ordered in their block");
    require(
        block.statements[0].value.text == "$fopen"
            && block.statements[0].value.operands.size() == 1
            && block.statements[1].kind == StatementKind::Assignment
            && block.statements[1].value.kind
                == ExpressionKind::Call
            && block.statements[1].value.text == "$fopen"
            && block.statements[1].value.operands.size() == 2,
        "$fopen retains multichannel and file-descriptor forms");
    require(
        block.statements[2].value.text == "$fgets"
            && block.statements[3].value.text == "$fgetc"
            && block.statements[4].value.text == "$ungetc"
            && block.statements[5].value.text == "$feof"
            && block.statements[6].value.text == "$ferror"
            && block.statements[7].value.text == "$fscanf"
            && block.statements[7].value.operands.size() == 4
            && block.statements[8].value.text == "$sscanf"
            && block.statements[8].value.operands.size() == 3
            && block.statements[9].value.text == "$fread"
            && block.statements[9].value.operands.size() == 2
            && block.statements[10].value.text == "$fread"
            && block.statements[10].value.operands.size() == 4
            && block.statements[11].value.text == "$ftell"
            && block.statements[12].value.text == "$fseek"
            && block.statements[13].value.text == "$rewind"
            && block.statements[14].kind == StatementKind::FileFlush
            && block.statements[15].kind == StatementKind::FileFlush,
        "file system functions retain distinct call identities");
    require(
        block.statements[16].kind == StatementKind::FileDisplay
            && block.statements[16].output_newline
            && block.statements[16].output_values.size() == 2
            && block.statements[16].output_values[0].format
                == OutputFormat::Decimal
            && block.statements[16].output_values[1].format
                == OutputFormat::String
            && block.statements[17].kind
                == StatementKind::FileDisplay
            && !block.statements[17].output_newline
            && block.statements[17].output_values.size() == 1
            && block.statements[17].output_values[0].value.text
                == "line",
        "file output tasks retain ordered and unformatted values");
    require(
        block.statements[18].kind == StatementKind::FileClose
            && block.statements[18].file_handle.text == "handle",
        "$fclose retains its handle expression");

    const auto invalid_arity = parse_text(
        "file-arity.sv",
        R"(
module file_arity;
  integer handle;
  string line;
  initial begin
    handle = $fopen();
    handle = $fopen("name", "w", "extra");
    handle = $fgets(line);
    handle = $fgetc();
    handle = $ungetc(handle);
    handle = $feof(handle, handle);
    handle = $ferror(handle);
    handle = $fscanf(handle);
    handle = $sscanf(line);
    handle = $fread(line);
    handle = $fread(line, handle, 0, 1, 2);
    handle = $fseek(handle, 0);
    handle = $ftell();
    handle = $rewind(handle, 0);
    $fflush(handle, handle);
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
    $fdisplay(handle, "%0d %0d", 1);
    $fwrite(handle, "%q", handle);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(
        !invalid_format.ok()
            && has_code(invalid_format, "FSIM-SV-SEM-076"),
        "malformed file-output formatting is diagnosed");

    const auto verilog = parse_text(
        "file-verilog.v",
        R"(
module file_verilog;
  integer handle;
  integer result;
  reg [136:0] line;
  reg [136:0] memory [3:0];
  initial begin
    handle = $fopen("file.txt", "r");
    result = $fgets(line, handle);
    result = $fgetc(handle);
    result = $ungetc(result, handle);
    result = $feof(handle);
    result = $ferror(handle, line);
    result = $fscanf(handle, "%h", line);
    result = $sscanf("2a", "%h", line);
    result = $fread(line, handle);
    result = $fseek(handle, 0, 0);
    result = $ftell(handle);
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
        Language::Verilog2005);
    require(
        verilog.ok(),
        "IEEE 1364-2005 file services parse in Verilog mode");

    const auto legacy_services = parse_verilog(
        SourceText {
            "legacy-services.v",
            R"(module legacy_services;
  integer handle;
  integer result;
  reg [7:0] memory [1:0];
  initial begin : legacy_io
    handle = $fopen("legacy.log");
    result = $random;
    result = $stime;
    $fdisplay(handle, "%0d", result);
    $strobe("%0d", result);
    $readmemh("legacy.hex", memory);
    $fclose(handle);
    $finish;
  end
endmodule
)" },
        StandardRevision::Verilog1995);
    require(
        legacy_services.ok(),
        "Verilog-1995 retains legacy file, formatting, random, time, memory, and control services");
    const auto& legacy_block = legacy_services.design.units.front().processes.front().statements.front();
    require(
        legacy_block.statements.size() == 8,
        "legacy services remain ordered");
    require(
        legacy_block.statements[1].value.call_result_width == 32
            && legacy_block.statements[1].value.call_result_domain
                == ValueDomain::Integer
            && legacy_block.statements[1].value.call_result_signed,
        "legacy random result profile remains exact");
    require(
        legacy_block.statements[2].value.call_result_width == 32
            && legacy_block.statements[2].value.call_result_domain
                == ValueDomain::Bit2,
        "legacy time result profile remains exact");
    require(
        legacy_block.statements[3].kind
                == StatementKind::FileDisplay
            && !legacy_block.statements[3].output_postponed
            && legacy_block.statements[4].kind == StatementKind::Display
            && legacy_block.statements[4].output_postponed,
        "legacy active/postponed scheduling profiles remain exact");

    const auto later_file_services_in_1995 = parse_verilog(
        SourceText {
            "later-files-in-1995.v",
            R"(module later_files_in_1995;
  integer handle;
  integer result;
  reg [7:0] memory [1:0];
  initial begin
    handle = $fopen("later.log", "w");
    result = $fgetc(handle);
    result = $value$plusargs("value=%d", result);
    $writememh("later.hex", memory);
  end
endmodule
)" },
        StandardRevision::Verilog1995);
    const auto services_2001 = parse_verilog(
        SourceText {
            "services-2001.v",
            R"(module services_2001;
  integer handle;
  integer result;
  reg [7:0] memory [1:0];
  initial begin
    handle = $fopen("later.log", "w");
    result = $fgetc(handle);
    result = $value$plusargs("value=%d", result);
    $writememh("later.hex", memory);
  end
endmodule
)" },
        StandardRevision::Verilog2001);
    require(
        !later_file_services_in_1995.ok()
            && has_code(later_file_services_in_1995, "FSIM-SV-PARSE-350")
            && services_2001.ok(),
        "Verilog-2001 introduces later file signatures, input, plusarg, and memory-write services");

    const auto math_2001 = parse_verilog(
        SourceText {
            "math-2001.v",
            "module math_2001; real value; initial value = $ln(2.0); endmodule\n" },
        StandardRevision::Verilog2001);
    const auto math_2005 = parse_verilog(
        SourceText {
            "math-2005.v",
            "module math_2005; real value; initial value = $ln(2.0); endmodule\n" },
        StandardRevision::Verilog2005);
    require(
        !math_2001.ok() && has_code(math_2001, "FSIM-SV-PARSE-350")
            && math_2005.ok(),
        "Verilog-2005 introduces the mathematical system-function family");

    const auto systemverilog_services = parse_verilog(
        SourceText {
            "systemverilog-services.sv",
            R"(module systemverilog_services;
  logic value;
  int result;
  string kind;
  initial begin : system_services
    result = $urandom();
    result = $rose(value);
    result = $get_coverage();
    kind = $typename(value);
    result = $system();
    $error("error");
    $fatal(0, "fatal");
    $asserton;
  end
endmodule
)" },
        StandardRevision::SystemVerilog2005);
    require(
        systemverilog_services.ok(),
        "SystemVerilog-2005 admits random, sampled, coverage, introspection, host, severity, and assertion-control services");
    const auto& systemverilog_block = systemverilog_services.design.units.front()
                                          .processes.front()
                                          .statements.front();
    require(
        systemverilog_block.statements.size() == 8
            && systemverilog_block.statements[0].value.call_result_width == 32
            && !systemverilog_block.statements[0].value.call_result_signed
            && systemverilog_block.statements[1].value.call_result_width == 1
            && systemverilog_block.statements[2].value.call_result_width == 64
            && systemverilog_block.statements[3].value.call_result_domain
                == ValueDomain::String
            && systemverilog_block.statements[4].value.call_result_width == 32
            && systemverilog_block.statements[5].assertion_severity
                == AssertionSeverity::Error
            && systemverilog_block.statements[6].assertion_severity
                == AssertionSeverity::Failure
            && systemverilog_block.statements[7].assertion_control
                == SystemVerilogAssertionControlKind::On,
        "SystemVerilog service results, severity, and assertion-control profiles remain exact");

    const auto introspection_in_verilog = parse_verilog(
        SourceText {
            "introspection-in-verilog.v",
            R"(module introspection_in_verilog;
  reg [7:0] value;
  integer result;
  initial begin
    result = $bits(value);
    result = $onehot(value);
    result = $urandom_range(7);
    result = $sformatf("%0d", result);
  end
endmodule
)" },
        StandardRevision::Verilog2005);
    const auto introspection_2005 = parse_verilog(
        SourceText {
            "introspection-2005.sv",
            R"(module introspection_2005;
  logic [7:0] value;
  int result;
  string text;
  initial begin : introspection
    result = $bits(value);
    result = $countones(value);
    result = $onehot(value);
    result = $urandom_range(7);
    text = $sformatf("%0d", result);
  end
endmodule
)" },
        StandardRevision::SystemVerilog2005);
    require(
        !introspection_in_verilog.ok()
            && has_code(introspection_in_verilog, "FSIM-SV-PARSE-350")
            && introspection_2005.ok(),
        "SystemVerilog-2005 introduces packed introspection, state-query, range-random, and string-format functions");
    const auto& introspection_block = introspection_2005.design.units.front()
                                          .processes.front()
                                          .statements.front();
    require(
        introspection_block.statements[0].value.call_result_width == 32
            && introspection_block.statements[1].value.call_result_width == 32
            && introspection_block.statements[2].value.call_result_width == 1
            && introspection_block.statements[3].value.call_result_width == 32
            && introspection_block.statements[4].value.call_result_domain
                == ValueDomain::String,
        "SystemVerilog introspection, state-query, random, and formatting results retain exact profiles");

    const auto global_services_2005 = parse_verilog(
        SourceText {
            "global-services-2005.sv",
            "module global_services_2005; logic value; int result; initial begin result = $rose_gclk(value); $assertpasson; end endmodule\n" },
        StandardRevision::SystemVerilog2005);
    const auto global_services_2009 = parse_verilog(
        SourceText {
            "global-services-2009.sv",
            "module global_services_2009; logic value; int result; initial begin result = $rose_gclk(value); $assertpasson; end endmodule\n" },
        StandardRevision::SystemVerilog2009);
    require(
        !global_services_2005.ok()
            && has_code(global_services_2005, "FSIM-SV-PARSE-350")
            && global_services_2009.ok(),
        "SystemVerilog-2009 introduces global-clock sampled functions and extended assertion controls");
}

} // namespace fsim::tests::frontend
