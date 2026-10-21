// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/file_binary.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/runtime/output_format.hpp"
#include "fsim/runtime/simir.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error { std::string { message } };
        }
    }

    class TestDirectory {
    public:
        explicit TestDirectory(const std::string_view name)
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path()
                / ("fsim-" + std::string { name } + "-"
                    + std::to_string(suffix));
            std::filesystem::create_directories(path);
        }

        ~TestDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        std::filesystem::path path;
    };

    [[nodiscard]] fsim::runtime::PackedLogic4 number(
        const std::uint32_t value)
    {
        return fsim::runtime::PackedLogic4::from_aval_bval(
            32, value, 0);
    }

} // namespace

void test_simir_text_files()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    TestDirectory files { "simir-files" };
    Interpreter interpreter;
    interpreter.set_file_root(files.path);
    interpreter.set_time_resolution_femtoseconds(2000);
    const auto first = interpreter.add_string_object({ "first", { } });
    const auto second = interpreter.add_string_object({ "second", { } });
    const auto third = interpreter.add_string_object({ "third", { } });
    const auto fourth = interpreter.add_string_object({ "fourth", { } });
    const auto error_text = interpreter.add_string_object({ "error", "not-cleared" });

    Process process;
    process.id = 0;
    process.name = "text_file_round_trip";
    process.register_count = 6;
    process.string_register_count = 6;
    process.debug_locals = {
        { "count", "integer", 3, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "eof", "integer", 4, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "error", "integer", 5, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    process.operations = {
        LoadStringConstant { 0, "round-trip.txt" },
        LoadStringConstant { 1, "w" },
        FileOpen { 0, 0, 1 },
        FileWriteLiteral { 0, "head", true },
        LoadConstant { 1, number(42) },
        FileWriteFormatted {
            0, 1, 32, OutputFormat::decimal,
            "value=", "", true },
        LoadConstant {
            2,
            PackedLogic4::from_aval_bval(
                32, static_cast<std::uint32_t>(-9), 0) },
        LoadConstant { 3, number(3) },
        LoadConstant { 4, number(12) },
        LoadStringConstant { 5, " ns" },
        TimeFormatControl { 2, 3, 5, 4 },
        LoadConstant {
            1,
            encode_systemverilog_scalar_payload(
                SystemVerilogScalarValue::time(1250))
                .value },
        FileWriteFormatted {
            0, 1, 64, OutputFormat::time,
            "time=", "", true, false, false, 0, false, false,
            SystemVerilogScalarKind::Time },
        LoadStringConstant { 3, "body" },
        FileWriteString { 0, 3, "<", ">", false },
        FileFlush { 0, false },
        FileFlush { 0, true },
        FileClose { 0 },
        LoadStringConstant { 2, "r" },
        FileOpen { 2, 0, 2 },
        FileReadLine { 3, 2, 4, 0, FileReadKind::line },
        WriteStringObject { first, 4 },
        FileReadLine { 3, 2, 4, 0, FileReadKind::line },
        WriteStringObject { second, 4 },
        FileReadLine { 3, 2, 4, 0, FileReadKind::line },
        WriteStringObject { third, 4 },
        FileReadLine { 3, 2, 4, 0, FileReadKind::line },
        WriteStringObject { fourth, 4 },
        FileEndOfFile { 4, 2 },
        FileErrorStatus { 5, 2, 5 },
        WriteStringObject { error_text, 5 },
        FileClose { 2 },
        Halt { }
    };
    (void)interpreter.add_process(std::move(process));
    require(
        interpreter.run().status == RunStatus::completed,
        "text-file round trip completes");
    require(
        interpreter.string_object_value(first) == "head\n",
        "first line retains its newline");
    require(
        interpreter.string_object_value(second) == "value=42\n",
        "formatted line round trips");
    require(
        interpreter.string_object_value(third) == "time=    2.500 ns\n",
        "time-formatted line uses the simulation-global profile");
    require(
        interpreter.string_object_value(fourth) == "<body>",
        "final unterminated line round trips");
    require(
        interpreter.string_object_value(error_text).empty(),
        "successful file has an empty error string");
    require(
        interpreter.read_debug_local(0, 0) == number(6),
        "$fgets count includes the final line bytes");
    require(
        interpreter.read_debug_local(0, 1) == number(1),
        "EOF is visible after the final unterminated line");
    require(
        interpreter.read_debug_local(0, 2) == number(0),
        "successful file reports no error");

    std::ifstream written(files.path / "round-trip.txt");
    const std::string contents {
        std::istreambuf_iterator<char> { written },
        std::istreambuf_iterator<char> { }
    };
    require(
        contents == "head\nvalue=42\ntime=    2.500 ns\n<body>",
        "file writes flush in deterministic operation order");

    Interpreter scanner;
    const auto scanned_text = scanner.add_string_object({ "scanned", { } });
    Process scan_process;
    scan_process.id = 0;
    scan_process.name = "formatted_string_scan";
    scan_process.register_count = 3;
    scan_process.string_register_count = 2;
    scan_process.debug_locals = {
        { "count", "integer", 0, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "hex", "logic [15:0]", 1, 16, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "decimal", "integer", 2, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    scan_process.operations = {
        LoadStringConstant { 0, "  tag=2a name=alpha % 17" },
        FileScan {
            0, 0, 0, true,
            {
                { " tag=", InputScanFormat::hexadecimal, 0, false,
                    { InputScanTargetKind::packed_register, 1, 16, false } },
                { " name=", InputScanFormat::string, 0, false,
                    { InputScanTargetKind::string_register, 1, 1, false } },
                { " % ", InputScanFormat::decimal, 0, false,
                    { InputScanTargetKind::packed_register, 2, 32, true } },
            },
            { } },
        WriteStringObject { scanned_text, 1 },
        Halt { }
    };
    (void)scanner.add_process(std::move(scan_process));
    require(
        scanner.run().status == RunStatus::completed,
        "formatted string scan completes");
    require(
        scanner.read_debug_local(0, 0) == number(3),
        "formatted scan reports three assignments");
    require(
        scanner.read_debug_local(0, 1)
            == PackedLogic4::from_aval_bval(16, 0x2a, 0),
        "formatted hexadecimal scan preserves target width");
    require(
        scanner.read_debug_local(0, 2) == number(17),
        "formatted decimal scan stores its value");
    require(
        scanner.string_object_value(scanned_text) == "alpha",
        "formatted string scan stores bounded text");

    // Integer fields take underscores anywhere, also first (IEEE 1800-2023
    // Table 21-7).
    const auto accepts_leading_separator = [](
        const InputScanFormat format,
        const std::string_view input,
        const std::uint32_t expected) {
        FileScan scan;
        scan.string_source = true;
        scan.conversions = {
            { "", format, 0U, false,
                { InputScanTargetKind::packed_register, 0U, 32U, true } },
        };
        const auto result = scan_formatted_string(scan, input);
        return result.assignments == 1 && result.values.size() == 1U
            && result.values[0]
            && result.values[0]->packed == number(expected);
    };
    require(
        accepts_leading_separator(InputScanFormat::binary, "_01", 1U)
            && accepts_leading_separator(InputScanFormat::octal, "_7", 7U)
            && accepts_leading_separator(InputScanFormat::decimal, "_1", 1U)
            && accepts_leading_separator(InputScanFormat::decimal, "+_1", 1U)
            && accepts_leading_separator(
                InputScanFormat::hexadecimal, "_a", 10U),
        "formatted integer scans accept a leading underscore");

    FileScan embedded_separator_scan;
    embedded_separator_scan.string_source = true;
    embedded_separator_scan.conversions = {
        { "", InputScanFormat::decimal, 0U, false,
            { InputScanTargetKind::packed_register, 0U, 32U, true } },
    };
    const auto embedded_separator = scan_formatted_string(
        embedded_separator_scan, "1_2");
    require(
        embedded_separator.assignments == 1
            && embedded_separator.values[0]
            && embedded_separator.values[0]->packed == number(12),
        "formatted integer scans continue to accept embedded underscores");

    FileScan unknown_scan;
    unknown_scan.string_source = true;
    unknown_scan.conversions = {
        { "", InputScanFormat::binary, 0U, false,
            { InputScanTargetKind::packed_register, 0U, 1U, false } },
    };
    const auto unknown_value = scan_formatted_string(unknown_scan, "x");
    FileScan high_impedance_scan;
    high_impedance_scan.string_source = true;
    high_impedance_scan.conversions = {
        { "", InputScanFormat::hexadecimal, 0U, false,
            { InputScanTargetKind::packed_register, 0U, 4U, false } },
    };
    const auto high_impedance_value = scan_formatted_string(
        high_impedance_scan, "z");
    require(
        unknown_value.assignments == 1 && unknown_value.values[0]
            && unknown_value.values[0]->packed == PackedLogic4(1U, Logic4::x)
            && high_impedance_value.assignments == 1
            && high_impedance_value.values[0]
            && high_impedance_value.values[0]->packed
                == PackedLogic4(4U, Logic4::z),
        "valid X and Z starts remain valid numeric scan fields");

    FileScan whitespace_string_scan;
    whitespace_string_scan.string_source = true;
    whitespace_string_scan.conversions = {
        { "", InputScanFormat::string, 0U, false,
            { InputScanTargetKind::string_register, 0U, 1U, false } },
    };
    const auto whitespace_string = scan_formatted_string(
        whitespace_string_scan, " ");
    require(
        whitespace_string.assignments == -1
            && whitespace_string.values.size() == 1U
            && !whitespace_string.values[0],
        "sscanf returns EOF when whitespace-only input ends before conversion");

    FileScan hierarchy_scan;
    hierarchy_scan.string_source = true;
    hierarchy_scan.hierarchy_path = "top.scope";
    hierarchy_scan.conversions = {
        { "", InputScanFormat::hierarchy, 0, false,
            { InputScanTargetKind::packed_register, 1, 64, false } },
        { "X", InputScanFormat::decimal, 0, false,
            { InputScanTargetKind::packed_register, 0, 32, true } },
        { "", InputScanFormat::hierarchy, 0, false,
            { InputScanTargetKind::string_register, 0, 1, false } },
    };
    const auto hierarchy_values = scan_formatted_string(
        hierarchy_scan, "X7");
    require(
        hierarchy_values.assignments == 3
            && hierarchy_values.consumed == 2U
            && hierarchy_values.values[0]
            && hierarchy_values.values[0]->packed
                == pack_file_text("top.scope", 64U)
            && hierarchy_values.values[1]
            && hierarchy_values.values[1]->packed == number(7)
            && hierarchy_values.values[2]
            && hierarchy_values.values[2]->string
            && hierarchy_values.values[2]->text == "top.scope",
        "%m assigns the current path without consuming input before later conversions");
    FileScan suppressed_hierarchy_scan;
    suppressed_hierarchy_scan.string_source = true;
    suppressed_hierarchy_scan.hierarchy_path = "top.scope";
    suppressed_hierarchy_scan.conversions = {
        { "", InputScanFormat::hierarchy, 0, true, { } },
        { "", InputScanFormat::decimal, 0, false,
            { InputScanTargetKind::packed_register, 0, 32, true } },
    };
    const auto suppressed_hierarchy = scan_formatted_string(
        suppressed_hierarchy_scan, "7");
    require(
        suppressed_hierarchy.assignments == 1
            && suppressed_hierarchy.consumed == 1U
            && !suppressed_hierarchy.values[0]
            && suppressed_hierarchy.values[1]
            && suppressed_hierarchy.values[1]->packed == number(7),
        "suppressed %m assigns nothing and consumes no input");

    FileScan scalar_scan;
    scalar_scan.string_source = true;
    scalar_scan.conversions = {
        { "real=", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
        { " time=", InputScanFormat::decimal, 0, false,
            { InputScanTargetKind::packed_register, 1, 64, true,
                SystemVerilogScalarKind::Time } },
        { " handle=", InputScanFormat::hexadecimal, 0, false,
            { InputScanTargetKind::packed_register, 2, 64, true,
                SystemVerilogScalarKind::Chandle } },
    };
    // A floating-point field has no underscores; an integer field may
    // (IEEE 1800-2023 Table 21-7).
    const auto scalar_values = scan_formatted_string(
        scalar_scan, "real=1.25 time=1_7 handle=0");
    require(
        scalar_values.assignments == 3
            && scalar_values.values[0] && scalar_values.values[1]
            && scalar_values.values[2],
        "scalar scan stages every successful conversion");
    const auto decoded_real = decode_systemverilog_scalar_payload(
        scalar_values.values[0]->packed, SystemVerilogScalarKind::Real);
    const auto decoded_time = decode_systemverilog_scalar_payload(
        scalar_values.values[1]->packed, SystemVerilogScalarKind::Time);
    require(
        decoded_real && decoded_real.value.as_real() == 1.25
            && decoded_time && decoded_time.value.as_time() == 17
            && scalar_values.values[2]->packed.low_word().aval == 0,
        "scalar scan preserves real, time, and null chandle payloads");

    FileScan real_overflow_scan;
    real_overflow_scan.string_source = true;
    real_overflow_scan.conversions = {
        { "", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
        { " ", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 1, 32, true,
                SystemVerilogScalarKind::ShortReal } },
        { " ", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 2, 64, true,
                SystemVerilogScalarKind::Realtime } },
    };
    const auto real_overflow_values = scan_formatted_string(
        real_overflow_scan, "1e5000 -1e5000 1e5000");
    const auto overflow_real = real_overflow_values.values[0]
        ? decode_systemverilog_scalar_payload(
              real_overflow_values.values[0]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    const auto overflow_shortreal = real_overflow_values.values[1]
        ? decode_systemverilog_scalar_payload(
              real_overflow_values.values[1]->packed,
              SystemVerilogScalarKind::ShortReal)
        : SystemVerilogScalarResult { };
    const auto overflow_realtime = real_overflow_values.values[2]
        ? decode_systemverilog_scalar_payload(
              real_overflow_values.values[2]->packed,
              SystemVerilogScalarKind::Realtime)
        : SystemVerilogScalarResult { };
    const auto overflow_real_value = overflow_real
        ? overflow_real.value.as_real()
        : std::optional<double> { };
    const auto overflow_shortreal_value = overflow_shortreal
        ? overflow_shortreal.value.as_shortreal()
        : std::optional<float> { };
    const auto overflow_realtime_value = overflow_realtime
        ? overflow_realtime.value.as_real()
        : std::optional<double> { };
    require(
        real_overflow_values.assignments == 3
            && overflow_real_value && std::isinf(*overflow_real_value)
            && !std::signbit(*overflow_real_value)
            && overflow_shortreal_value
            && std::isinf(*overflow_shortreal_value)
            && std::signbit(*overflow_shortreal_value)
            && overflow_realtime_value && std::isinf(*overflow_realtime_value)
            && !std::signbit(*overflow_realtime_value),
        "real input overflow stores signed infinity for real scalar targets");

    FileScan finite_real_scan;
    finite_real_scan.string_source = true;
    finite_real_scan.conversions = {
        { "", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
        { " ", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 1, 32, true,
                SystemVerilogScalarKind::ShortReal } },
    };
    const auto finite_real_values = scan_formatted_string(
        finite_real_scan, "1.7e308 -3.4e38");
    const auto finite_real = finite_real_values.values[0]
        ? decode_systemverilog_scalar_payload(
              finite_real_values.values[0]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    const auto finite_shortreal = finite_real_values.values[1]
        ? decode_systemverilog_scalar_payload(
              finite_real_values.values[1]->packed,
              SystemVerilogScalarKind::ShortReal)
        : SystemVerilogScalarResult { };
    const auto finite_real_value = finite_real
        ? finite_real.value.as_real()
        : std::optional<double> { };
    const auto finite_shortreal_value = finite_shortreal
        ? finite_shortreal.value.as_shortreal()
        : std::optional<float> { };
    require(
        finite_real_values.assignments == 2
            && finite_real_value && std::isfinite(*finite_real_value)
            && *finite_real_value > 1.6e308
            && finite_shortreal_value
            && std::isfinite(*finite_shortreal_value)
            && *finite_shortreal_value < -3.3e38F,
        "large finite real and shortreal input values remain finite");

    FileScan real_underflow_scan;
    real_underflow_scan.string_source = true;
    real_underflow_scan.conversions = {
        { "", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
    };
    const auto real_underflow = scan_formatted_string(
        real_underflow_scan, "1e-5000");
    require(
        real_underflow.assignments == 0 && real_underflow.values.size() == 1U
            && !real_underflow.values[0],
        "real underflow retains the existing failed-conversion behavior");

    FileScan significand_overflow_scan;
    significand_overflow_scan.string_source = true;
    significand_overflow_scan.conversions = {
        { "", InputScanFormat::real, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
    };
    const auto long_significand = std::string(309U, '9');
    const auto significand_overflow = scan_formatted_string(
        significand_overflow_scan, long_significand);
    const auto significand_real = significand_overflow.values[0]
        ? decode_systemverilog_scalar_payload(
              significand_overflow.values[0]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    const auto significand_real_value = significand_real
        ? significand_real.value.as_real()
        : std::optional<double> { };
    require(
        significand_overflow.assignments == 1 && significand_real_value
            && std::isinf(*significand_real_value)
            && !std::signbit(*significand_real_value),
        "real significand overflow without an exponent stores positive infinity");

    SystemVerilogTimeFormat scan_time_format;
    scan_time_format.units = -3;
    scan_time_format.precision = 2;
    scan_time_format.resolution_femtoseconds = 100'000U;
    FileScan scaled_time_scan;
    scaled_time_scan.string_source = true;
    scaled_time_scan.time_unit_femtoseconds = 1'000'000U;
    scaled_time_scan.conversions = {
        { "", InputScanFormat::time, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
        { " ", InputScanFormat::time, 0, false,
            { InputScanTargetKind::packed_register, 1, 32, true,
                SystemVerilogScalarKind::ShortReal } },
        { " ", InputScanFormat::time, 0, false,
            { InputScanTargetKind::packed_register, 2, 64, true,
                SystemVerilogScalarKind::Realtime } },
    };
    const auto scaled_time_values = scan_formatted_string(
        scaled_time_scan, "8.125 -8.125 1.25e+1", scan_time_format);
    const auto scaled_real = scaled_time_values.values[0]
        ? decode_systemverilog_scalar_payload(
              scaled_time_values.values[0]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    const auto scaled_shortreal = scaled_time_values.values[1]
        ? decode_systemverilog_scalar_payload(
              scaled_time_values.values[1]->packed,
              SystemVerilogScalarKind::ShortReal)
        : SystemVerilogScalarResult { };
    const auto scaled_realtime = scaled_time_values.values[2]
        ? decode_systemverilog_scalar_payload(
              scaled_time_values.values[2]->packed,
              SystemVerilogScalarKind::Realtime)
        : SystemVerilogScalarResult { };
    require(
        scaled_time_values.assignments == 3 && scaled_real
            && scaled_real.value.as_real() == 8'130'000.0
            && scaled_shortreal
            && scaled_shortreal.value.as_real() == -8'130'000.0
            && scaled_realtime
            && scaled_realtime.value.as_real() == 12'500'000.0,
        "%t scales by $timeformat units and rounds ties away for real targets");

    scan_time_format.units = -11;
    scan_time_format.precision = 0;
    scan_time_format.resolution_femtoseconds = 10'000U;
    FileScan default_time_scan;
    default_time_scan.string_source = true;
    default_time_scan.time_unit_femtoseconds = 1'000'000U;
    default_time_scan.conversions = {
        { "", InputScanFormat::time, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
    };
    const auto default_time_values = scan_formatted_string(
        default_time_scan, "012345678925", scan_time_format);
    const auto default_time_real = default_time_values.values[0]
        ? decode_systemverilog_scalar_payload(
              default_time_values.values[0]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    require(
        default_time_values.assignments == 1 && default_time_real
            && default_time_real.value.as_real() == 123'456'789.25,
        "%t uses the default global-precision unit when it differs from local timeunit");

    FileScan width_time_scan;
    width_time_scan.string_source = true;
    width_time_scan.time_unit_femtoseconds = 1'000'000U;
    width_time_scan.conversions = {
        { "", InputScanFormat::time, 6, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
        { " ", InputScanFormat::decimal, 0, false,
            { InputScanTargetKind::packed_register, 1, 32, true } },
        { " ", InputScanFormat::time, 6, false,
            { InputScanTargetKind::packed_register, 2, 64, true,
                SystemVerilogScalarKind::Real } },
        { " ", InputScanFormat::decimal, 0, false,
            { InputScanTargetKind::packed_register, 3, 32, true } },
    };
    scan_time_format.units = -9;
    scan_time_format.precision = 3;
    const auto width_time_values = scan_formatted_string(
        width_time_scan, "-2.2566789 +2.e+16789", scan_time_format);
    const auto width_time_real = width_time_values.values[0]
        ? decode_systemverilog_scalar_payload(
              width_time_values.values[0]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    const auto width_exponent_real = width_time_values.values[2]
        ? decode_systemverilog_scalar_payload(
              width_time_values.values[2]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    require(
        width_time_values.assignments == 4 && width_time_real
            && width_time_real.value.as_real() == -2.256
            && width_time_values.values[1]
            && width_time_values.values[1]->packed == number(6789)
            && width_exponent_real
            && width_exponent_real.value.as_real() == 20.0
            && width_time_values.values[3]
            && width_time_values.values[3]->packed == number(6789),
        "bounded %t fields handle exponent text and leave later digits for the next conversion");

    FileScan suppressed_time_scan;
    suppressed_time_scan.string_source = true;
    suppressed_time_scan.time_unit_femtoseconds = 1'000'000U;
    suppressed_time_scan.conversions = {
        { "", InputScanFormat::decimal, 0, false,
            { InputScanTargetKind::packed_register, 1, 32, true } },
        { " ", InputScanFormat::time, 0, true, { } },
        { " ", InputScanFormat::time, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } },
    };
    const auto suppressed_time_values = scan_formatted_string(
        suppressed_time_scan, "123 1.0 2.5", scan_time_format);
    const auto suppressed_time_real = suppressed_time_values.values[2]
        ? decode_systemverilog_scalar_payload(
              suppressed_time_values.values[2]->packed,
              SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    require(
        suppressed_time_values.assignments == 2
            && !suppressed_time_values.values[1] && suppressed_time_real
            && suppressed_time_real.value.as_real() == 2.5,
        "suppressed %t consumes a field without assigning or counting it");

    FileScan unsupported_time_target;
    unsupported_time_target.string_source = true;
    unsupported_time_target.time_unit_femtoseconds = 1'000'000U;
    unsupported_time_target.conversions = {
        { "", InputScanFormat::time, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Time } },
    };
    const auto unsupported_time_values = scan_formatted_string(
        unsupported_time_target, "2.5", scan_time_format);
    require(
        unsupported_time_values.assignments == 0
            && !unsupported_time_values.values[0],
        "the initial %t lowering slice rejects non-real target payloads");

    FileScan time_field_boundary_scan {
        0, 0, 0, true,
        { { "", InputScanFormat::time, 0, false,
            { InputScanTargetKind::packed_register, 0, 64, true,
                SystemVerilogScalarKind::Real } } } };
    time_field_boundary_scan.time_unit_femtoseconds = 1'000'000U;
    const auto time_field_boundary = scan_formatted_string(
        time_field_boundary_scan, "1_2", scan_time_format);
    require(
        time_field_boundary.assignments == 1
            && time_field_boundary.consumed == 1U
            && time_field_boundary.values[0],
        "the %t input field stops before a character outside its decimal grammar");

    const auto native_word_bytes = [](const std::uint32_t word) {
        std::string bytes(sizeof(word), '\0');
        std::memcpy(bytes.data(), &word, sizeof(word));
        return bytes;
    };

    auto one_bit_x = PackedLogic4 { 1U, Logic4::x };
    const auto expected_one_bit = PackedLogic4 { 1U, Logic4::zero };
    const auto raw_one_bit = format_output_value(
        one_bit_x, OutputFormat::unformatted2, false, false);
    const auto one_bit_scan = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted2, 0, false,
                { InputScanTargetKind::packed_register, 0, 1, true } } } },
        raw_one_bit);
    require(
        raw_one_bit == native_word_bytes(0U)
            && one_bit_scan.assignments == 1 && one_bit_scan.values[0]
            && one_bit_scan.values[0]->packed == expected_one_bit,
        "%u pads a one-bit word to four native-endian bytes and maps X to zero");

    auto byte_value = PackedLogic4 { 8U, Logic4::zero };
    byte_value.set(0U, Logic4::one);
    byte_value.set(1U, Logic4::x);
    byte_value.set(2U, Logic4::z);
    byte_value.set(7U, Logic4::one);
    const auto raw_byte = format_output_value(
        byte_value, OutputFormat::unformatted2, false, false);
    const auto byte_scan = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted2, 0, false,
                { InputScanTargetKind::packed_register, 0, 8, true } } } },
        raw_byte);
    auto expected_byte = PackedLogic4 { 8U, Logic4::zero };
    expected_byte.set(0U, Logic4::one);
    expected_byte.set(7U, Logic4::one);
    require(
        raw_byte == native_word_bytes(UINT32_C(0x81))
            && byte_scan.assignments == 1 && byte_scan.values[0]
            && byte_scan.values[0]->packed == expected_byte,
        "%u pads an eight-bit word, preserves native byte order, and clears X/Z");

    auto word_plus_one = PackedLogic4 { 33U, Logic4::zero };
    word_plus_one.set(0U, Logic4::one);
    word_plus_one.set(2U, Logic4::one);
    word_plus_one.set(5U, Logic4::z);
    word_plus_one.set(31U, Logic4::x);
    word_plus_one.set(32U, Logic4::one);
    const auto raw_word_plus_one = format_output_value(
        word_plus_one, OutputFormat::unformatted2, false, false);
    const auto word_plus_one_scan = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted2, 0, false,
                { InputScanTargetKind::packed_register, 0, 33, true } } } },
        raw_word_plus_one);
    auto expected_word_plus_one = PackedLogic4 { 33U, Logic4::zero };
    expected_word_plus_one.set(0U, Logic4::one);
    expected_word_plus_one.set(2U, Logic4::one);
    expected_word_plus_one.set(32U, Logic4::one);
    require(
        raw_word_plus_one
                == native_word_bytes(UINT32_C(5))
                    + native_word_bytes(UINT32_C(1))
            && word_plus_one_scan.assignments == 1
            && word_plus_one_scan.values[0]
            && word_plus_one_scan.values[0]->packed == expected_word_plus_one,
        "%u writes the low word first and pads a 33-bit value to two words");

    auto one_bit_z = PackedLogic4 { 1U, Logic4::z };
    const auto raw_one_bit_four = format_output_value(
        one_bit_z, OutputFormat::unformatted4, false, false);
    require(
        raw_one_bit_four
            == native_word_bytes(UINT32_C(0))
                + native_word_bytes(UINT32_C(1)),
        "%z emits native-endian aval and bval words for a one-bit value");

    auto byte_four_state = PackedLogic4 { 8U, Logic4::zero };
    byte_four_state.set(0U, Logic4::one);
    byte_four_state.set(1U, Logic4::x);
    byte_four_state.set(2U, Logic4::z);
    byte_four_state.set(7U, Logic4::one);
    const auto raw_byte_four = format_output_value(
        byte_four_state, OutputFormat::unformatted4, false, false);
    require(
        raw_byte_four
            == native_word_bytes(UINT32_C(0x83))
                + native_word_bytes(UINT32_C(0x6)),
        "%z preserves the native-endian four-state bit planes at eight bits");

    auto word_plus_one_four = PackedLogic4 { 33U, Logic4::zero };
    word_plus_one_four.set(0U, Logic4::one);
    word_plus_one_four.set(31U, Logic4::x);
    word_plus_one_four.set(32U, Logic4::z);
    const auto raw_word_plus_one_four = format_output_value(
        word_plus_one_four, OutputFormat::unformatted4, false, false);
    const auto expected_word_plus_one_four
        = native_word_bytes(UINT32_C(0x80000001))
        + native_word_bytes(UINT32_C(0x80000000))
        + native_word_bytes(UINT32_C(0))
        + native_word_bytes(UINT32_C(1));
    const auto word_plus_one_four_scan = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted4, 0, false,
                { InputScanTargetKind::packed_register, 0, 33, false } } } },
        raw_word_plus_one_four);
    require(
        raw_word_plus_one_four == expected_word_plus_one_four
            && word_plus_one_four_scan.assignments == 1
            && word_plus_one_four_scan.values[0]
            && word_plus_one_four_scan.values[0]->packed == word_plus_one_four,
        "%z writes low-to-high native aval/bval words across 33 bits");

    auto raw_value = PackedLogic4 { 37U, Logic4::zero };
    raw_value.set(0U, Logic4::one);
    raw_value.set(5U, Logic4::x);
    raw_value.set(8U, Logic4::z);
    raw_value.set(31U, Logic4::one);
    raw_value.set(36U, Logic4::one);
    const auto raw_four = format_output_value(
        raw_value, OutputFormat::unformatted4, false, false);
    const auto expected_four
        = native_word_bytes(UINT32_C(0x80000021))
        + native_word_bytes(UINT32_C(0x120))
        + native_word_bytes(UINT32_C(0x10))
        + native_word_bytes(UINT32_C(0));
    const auto four_values = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted4, 0, false,
                { InputScanTargetKind::packed_register, 0, 37, false } } } },
        raw_four);
    require(
        raw_four == expected_four && raw_four.size() == 16U
            && four_values.assignments == 1
            && four_values.values[0]
            && four_values.values[0]->packed == raw_value,
        "%z round trips native aval and bval words without losing X or Z");

    const auto raw_two = format_output_value(
        raw_value, OutputFormat::unformatted2, false, false);
    const auto expected_two_bytes
        = native_word_bytes(UINT32_C(0x80000001))
        + native_word_bytes(UINT32_C(0x10));
    const auto two_values = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted2, 0, false,
                { InputScanTargetKind::packed_register, 0, 37, true } } } },
        raw_two);
    auto expected_two_value = raw_value;
    expected_two_value.set(5U, Logic4::zero);
    expected_two_value.set(8U, Logic4::zero);
    require(
        raw_two == expected_two_bytes && raw_two.size() == 8U
            && two_values.assignments == 1
            && two_values.values[0]
            && two_values.values[0]->packed == expected_two_value,
        "%u round trips native two-state bytes and collapses X and Z to zero");
    const auto incomplete_two = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted2, 0, false,
                { InputScanTargetKind::packed_register, 0, 37, true } } } },
        std::string_view { raw_two }.substr(0, raw_two.size() - 1U));
    require(
        incomplete_two.assignments == -1 && !incomplete_two.values[0],
        "an incomplete %u word leaves its target unmodified");
    FileScan bounded_two;
    bounded_two.string_source = true;
    bounded_two.trailing_text = "!";
    bounded_two.conversions = {
        { "", InputScanFormat::unformatted2, 12, false,
            { InputScanTargetKind::packed_register, 0, 37, true } },
    };
    const auto bounded_two_values = scan_formatted_string(
        bounded_two, raw_two + "!");
    require(
        bounded_two_values.assignments == 1
            && bounded_two_values.consumed == raw_two.size() + 1U
            && bounded_two_values.values[0]
            && bounded_two_values.values[0]->packed == expected_two_value,
        "%u input consumes its target's complete words under a wider field cap");
    FileScan short_width_two;
    short_width_two.string_source = true;
    short_width_two.conversions = {
        { "", InputScanFormat::unformatted2, 4, false,
            { InputScanTargetKind::packed_register, 0, 37, true } },
    };
    const auto short_width_two_values = scan_formatted_string(
        short_width_two, raw_two);
    require(
        short_width_two_values.assignments == 0
            && short_width_two_values.consumed == 4U
            && !short_width_two_values.values[0],
        "%u reports an incomplete capped field without claiming EOF");
    FileScan short_width_four;
    short_width_four.string_source = true;
    short_width_four.conversions = {
        { "", InputScanFormat::unformatted4, 8, false,
            { InputScanTargetKind::packed_register, 0, 37, false } },
    };
    const auto short_width_four_values = scan_formatted_string(
        short_width_four, raw_four);
    require(
        short_width_four_values.assignments == 0
            && short_width_four_values.consumed == 8U
            && !short_width_four_values.values[0],
        "%z reports an incomplete capped field without claiming EOF");
    const auto incomplete_four = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::unformatted4, 0, false,
                { InputScanTargetKind::packed_register, 0, 37, false } } } },
        std::string_view { raw_four }.substr(0, raw_four.size() - 1U));
    require(
        incomplete_four.assignments == -1 && !incomplete_four.values[0],
        "an incomplete unformatted value leaves its target unmodified");

    const auto overflowing = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::real, 0, false,
                { InputScanTargetKind::packed_register, 0, 64, true,
                    SystemVerilogScalarKind::Real } } } },
        "1e9999");
    const auto overflowing_real = overflowing.values.size() == 1U
            && overflowing.values[0]
        ? decode_systemverilog_scalar_payload(
              overflowing.values[0]->packed, SystemVerilogScalarKind::Real)
        : SystemVerilogScalarResult { };
    const auto overflowing_value = overflowing_real
        ? overflowing_real.value.as_real()
        : std::optional<double> { };
    require(
        overflowing.assignments == 1 && overflowing_value
            && std::isinf(*overflowing_value)
            && !std::signbit(*overflowing_value),
        "overflowing real scan stores positive infinity");
    const auto partial_scalar = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "real=", InputScanFormat::real, 0, false,
                  { InputScanTargetKind::packed_register, 0, 64, true,
                      SystemVerilogScalarKind::Real } },
                { " time=", InputScanFormat::decimal, 0, false,
                    { InputScanTargetKind::packed_register, 1, 64, true,
                        SystemVerilogScalarKind::Time } } } },
        "real=2.5 time=oops");
    require(
        partial_scalar.assignments == 1 && partial_scalar.values[0]
            && !partial_scalar.values[1],
        "failed later conversion preserves only prior staged copy-outs");
    const auto nonnull_chandle = scan_formatted_string(
        FileScan { 0, 0, 0, true,
            { { "", InputScanFormat::hexadecimal, 0, false,
                { InputScanTargetKind::packed_register, 0, 64, true,
                    SystemVerilogScalarKind::Chandle } } } },
        "1");
    require(
        nonnull_chandle.assignments == 0 && !nonnull_chandle.values[0],
        "text input cannot fabricate a non-null chandle identity");
    std::size_t chandle_byte { };
    bool rejected_binary_chandle { };
    try {
        static_cast<void>(read_binary_file(
            FileBinaryRead { 0, 0, 0, FileBinaryTargetKind::packed_register,
                64, true, 0, 0, false, false,
                SystemVerilogScalarKind::Chandle },
            std::nullopt, std::nullopt, std::nullopt,
            [&]() -> std::int32_t {
                return chandle_byte++ == 7U ? 1 : 0;
            }));
    } catch (const std::invalid_argument& error) {
        rejected_binary_chandle = std::string_view { error.what() }.find(
                                      "null handle")
            != std::string_view::npos;
    }
    require(
        rejected_binary_chandle,
        "binary input cannot fabricate a non-null chandle identity");

    const std::array<std::uint8_t, 18> wide_bytes {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08
    };
    std::size_t wide_byte { };
    const auto wide_binary = read_binary_file(
        FileBinaryRead { 0, 0, 0, FileBinaryTargetKind::packed_register,
            137, false, 0, 0, false, false },
        std::nullopt, std::nullopt, std::nullopt,
        [&]() -> std::int32_t {
            return wide_byte < wide_bytes.size()
                ? wide_bytes[wide_byte++]
                : -1;
        });
    auto expected_wide = PackedLogic4 { 137, Logic4::zero };
    expected_wide.set(136, Logic4::one);
    expected_wide.set(73, Logic4::one);
    expected_wide.set(3, Logic4::one);
    require(
        wide_binary.bytes == wide_bytes.size()
            && wide_binary.packed == expected_wide,
        "binary input preserves arbitrary-width packed values and byte order");

    {
        std::ofstream binary(
            files.path / "binary.bin",
            std::ios::binary | std::ios::trunc);
        const std::string bytes { "\x12\x34\x56\x78\x9a", 5 };
        binary.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(binary.good(), "binary input fixture is writable");
    }
    ContainerType memory_type;
    memory_type.element_width = 8;
    memory_type.fixed = true;
    memory_type.index_left = 3;
    memory_type.index_right = 0;
    memory_type.dimensions = { { 3, 0 } };
    Interpreter binary_reader;
    binary_reader.set_file_root(files.path);
    const auto binary_memory = binary_reader.add_container_object(
        { "binary-memory", default_container_value(memory_type), std::nullopt });
    Process binary_process;
    binary_process.id = 0;
    binary_process.name = "binary_file_read";
    binary_process.register_count = 18;
    binary_process.string_register_count = 2;
    binary_process.container_register_count = 1;
    binary_process.container_register_types = { memory_type };
    binary_process.debug_locals = {
        { "packed-bytes", "integer", 1, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "packed", "logic [15:0]", 2, 16, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "memory-bytes", "integer", 3, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "partial", "logic [15:0]", 6, 16, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "partial-bytes", "integer", 7, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "tell", "integer", 8, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "invalid-seek", "integer", 11, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "seek", "integer", 17, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "positioned", "byte", 12, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "positioned-bytes", "integer", 13, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "rewind", "integer", 14, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "rewound", "byte", 15, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "rewound-bytes", "integer", 16, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    binary_process.operations = {
        LoadStringConstant { 0, "binary.bin" },
        LoadStringConstant { 1, "rb" },
        FileOpen { 0, 0, 1 },
        FileBinaryRead {
            1, 0, 2, FileBinaryTargetKind::packed_register,
            16, false, 0, 0, false, false },
        LoadConstant { 4, number(2) },
        LoadConstant { 5, number(2) },
        FileBinaryRead {
            3, 0, 0, FileBinaryTargetKind::container_register,
            8, false, 4, 5, true, true },
        WriteContainerObject { binary_memory, 0, std::nullopt },
        FilePosition { 8, 0, 0, 0, FilePositionKind::tell },
        FileBinaryRead {
            7, 0, 6, FileBinaryTargetKind::packed_register,
            16, false, 0, 0, false, false },
        LoadConstant {
            9, number(static_cast<std::uint32_t>(-3)) },
        LoadConstant { 10, number(3) },
        FilePosition { 11, 0, 9, 10, FilePositionKind::seek },
        LoadConstant { 10, number(1) },
        FilePosition { 17, 0, 9, 10, FilePositionKind::seek },
        FileBinaryRead {
            13, 0, 12, FileBinaryTargetKind::packed_register,
            8, false, 0, 0, false, false },
        FilePosition { 14, 0, 0, 0, FilePositionKind::rewind },
        FileBinaryRead {
            16, 0, 15, FileBinaryTargetKind::packed_register,
            8, false, 0, 0, false, false },
        FileFlush { 0, false },
        FileFlush { 0, true },
        FileClose { 0 },
        Halt { }
    };
    (void)binary_reader.add_process(std::move(binary_process));
    require(
        binary_reader.run().status == RunStatus::completed,
        "binary file read completes");
    require(
        binary_reader.read_debug_local(0, 0) == number(2)
            && binary_reader.read_debug_local(0, 1)
                == PackedLogic4::from_aval_bval(16, 0x1234, 0),
        "packed $fread reports bytes and stores big-endian data");
    const auto& loaded_memory = binary_reader.container_object_value(binary_memory);
    require(
        binary_reader.read_debug_local(0, 2) == number(2)
            && loaded_memory.elements[0]
                == PackedLogic4 { 8, Logic4::x }
            && loaded_memory.elements[1]
                == PackedLogic4::from_aval_bval(8, 0x56, 0)
            && loaded_memory.elements[2]
                == PackedLogic4::from_aval_bval(8, 0x78, 0)
            && loaded_memory.elements[3]
                == PackedLogic4 { 8, Logic4::x },
        "memory $fread honors declared direction, start, and count");
    require(
        binary_reader.read_debug_local(0, 3)
                == PackedLogic4::from_aval_bval(16, 0x9a00, 0)
            && binary_reader.read_debug_local(0, 4) == number(1),
        "partial packed $fread fills from the most-significant byte");
    require(
        binary_reader.read_debug_local(0, 5) == number(4)
            && binary_reader.read_debug_local(0, 6)
                == number(static_cast<std::uint32_t>(-1))
            && binary_reader.read_debug_local(0, 7) == number(0)
            && binary_reader.read_debug_local(0, 8)
                == PackedLogic4::from_aval_bval(8, 0x56, 0)
            && binary_reader.read_debug_local(0, 9) == number(1),
        "$ftell and $fseek report invalid origins and preserve byte positions");
    require(
        binary_reader.read_debug_local(0, 10) == number(0)
            && binary_reader.read_debug_local(0, 11)
                == PackedLogic4::from_aval_bval(8, 0x12, 0)
            && binary_reader.read_debug_local(0, 12) == number(1),
        "$rewind restores the beginning before the next binary read");

    {
        std::ofstream oversized(
            files.path / "oversized.txt",
            std::ios::binary | std::ios::trunc);
        oversized << std::string(maximum_string_bytes + 1U, 'x')
                  << '\n';
        require(oversized.good(), "oversized input fixture is writable");
    }
    Interpreter bounded;
    bounded.set_file_root(files.path);
    const auto bounded_error = bounded.add_string_object({ "bounded-error", { } });
    const auto bounded_value = bounded.add_string_object({ "bounded-value", "sentinel" });
    Process bounded_reader;
    bounded_reader.id = 0;
    bounded_reader.name = "bounded_file_reader";
    bounded_reader.register_count = 3;
    bounded_reader.string_register_count = 4;
    bounded_reader.operations = {
        LoadStringConstant { 0, "oversized.txt" },
        LoadStringConstant { 1, "r" },
        LoadStringConstant { 2, "sentinel" },
        FileOpen { 0, 0, 1 },
        FileReadLine { 1, 0, 2, 0, FileReadKind::line },
        FileErrorStatus { 2, 0, 3 },
        WriteStringObject { bounded_value, 2 },
        WriteStringObject { bounded_error, 3 },
        FileClose { 0 },
        Halt { }
    };
    (void)bounded.add_process(std::move(bounded_reader));
    require(
        bounded.run().status == RunStatus::completed,
        "overlong input is reported through $ferror");
    require(
        bounded.string_object_value(bounded_error).find("4096-byte limit")
            != std::string::npos,
        "overlong input retains a bounded error message");
    require(
        bounded.string_object_value(bounded_value) == "sentinel",
        "overlong input rejects before publishing partial string storage");

    const auto expect_failure =
        [&](std::vector<Operation> operations,
            const std::string_view expected) {
            Interpreter failing;
            failing.set_file_root(files.path);
            Process candidate;
            candidate.id = 0;
            candidate.name = "text_file_failure";
            candidate.register_count = 2;
            candidate.string_register_count = 2;
            candidate.operations = std::move(operations);
            (void)failing.add_process(std::move(candidate));
            try {
                (void)failing.run();
                require(false, "invalid file process must fail");
            } catch (const InterpreterError& error) {
                require(
                    std::string_view { error.what() }.find(expected)
                        != std::string_view::npos,
                    "file failure retains the expected diagnostic");
            }
        };

    expect_failure(
        { LoadStringConstant { 0, "../escape.txt" },
            LoadStringConstant { 1, "w" },
            FileOpen { 0, 0, 1 },
            Halt { } },
        "escapes the manifest root");
    expect_failure(
        { LoadStringConstant { 0, files.path.string() },
            LoadStringConstant { 1, "r" },
            FileOpen { 0, 0, 1 },
            Halt { } },
        "manifest-root-relative");
    const auto expect_systemverilog_open_failure_returns_zero =
        [&](const std::string_view path, const std::string_view mode) {
            Interpreter unable_to_open;
            unable_to_open.set_file_root(files.path);
            Process candidate;
            candidate.id = 0;
            candidate.name = "systemverilog_fopen_failure_returns_zero";
            candidate.register_count = 1;
            candidate.string_register_count = 2;
            candidate.debug_locals = {
                { "handle", "integer", 0, 32, { }, std::nullopt,
                    std::nullopt, ValueKind::logic4, { } }
            };
            candidate.scheduling_domain =
                ProcessSchedulingDomain::systemverilog;
            candidate.operations = {
                LoadStringConstant { 0, std::string { path } },
                LoadStringConstant { 1, std::string { mode } },
                FileOpen { 0, 0, 1 },
                Halt { }
            };
            (void)unable_to_open.add_process(std::move(candidate));
            require(
                unable_to_open.run().status == RunStatus::completed,
                "failed SystemVerilog $fopen completes normally");
            require(
                unable_to_open.read_debug_local(0, 0) == number(0),
                "failed SystemVerilog $fopen returns handle zero");
        };

    expect_systemverilog_open_failure_returns_zero("missing.txt", "r");
    expect_failure(
        { LoadStringConstant { 0, "invalid.txt" },
            LoadStringConstant { 1, "rt" },
            FileOpen { 0, 0, 1 },
            Halt { } },
        "unsupported SimIR text file mode");
    expect_systemverilog_open_failure_returns_zero("invalid.txt", "rt");
    expect_failure(
        { LoadStringConstant { 0, "closed.txt" },
            LoadStringConstant { 1, "w" },
            FileOpen { 0, 0, 1 },
            FileClose { 0 },
            FileClose { 0 },
            Halt { } },
        "already closed");
    expect_failure(
        { LoadConstant { 0, number(0) },
            FileClose { 0 },
            Halt { } },
        "invalid zero");
    expect_failure(
        { LoadConstant { 0, number(99) },
            FileClose { 0 },
            Halt { } },
        "unknown SimIR file handle");

    {
        Interpreter lifecycle;
        lifecycle.set_file_root(files.path);
        Process unclosed;
        unclosed.id = 0;
        unclosed.name = "unclosed_file";
        unclosed.register_count = 1;
        unclosed.string_register_count = 2;
        unclosed.operations = {
            LoadStringConstant { 0, "lifecycle.txt" },
            LoadStringConstant { 1, "w" },
            FileOpen { 0, 0, 1 },
            FileWriteLiteral { 0, "closed-on-destruction", true },
            Halt { }
        };
        (void)lifecycle.add_process(std::move(unclosed));
        require(
            lifecycle.run().status == RunStatus::completed,
            "unclosed file process completes");
    }
    require(
        std::filesystem::file_size(files.path / "lifecycle.txt") == 22,
        "session destruction closes and preserves the final file");

    Interpreter ownership;
    ownership.set_file_root(files.path);
    Process owner;
    owner.id = 0;
    owner.name = "file_owner";
    owner.register_count = 1;
    owner.string_register_count = 2;
    owner.operations = {
        LoadStringConstant { 0, "owned.txt" },
        LoadStringConstant { 1, "w" },
        FileOpen { 0, 0, 1 },
        Halt { }
    };
    Process borrower;
    borrower.id = 1;
    borrower.name = "file_borrower";
    borrower.register_count = 1;
    borrower.operations = {
        LoadConstant { 0, number(UINT32_C(0x80000003)) },
        FileClose { 0 },
        Halt { }
    };
    (void)ownership.add_process(std::move(owner));
    (void)ownership.add_process(std::move(borrower));
    // A file descriptor is a design-wide value (IEEE 1800-2017 21.3.1): a
    // process may close a file another process opened.
    require(
        ownership.run().status == RunStatus::completed,
        "a file opened by one process is closed by another");
}

} // namespace fsim::tests::runtime
