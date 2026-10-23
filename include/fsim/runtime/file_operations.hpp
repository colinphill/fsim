// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/systemverilog_scalar.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

enum class OutputFormat : std::uint8_t;

struct FileOpen {
    std::uint32_t destination { }, path { }, mode { };
    std::optional<std::uint32_t> status;
    bool vhdl { };
    FileOpen(std::uint32_t destination_value = 0,
        std::uint32_t path_value = 0, std::uint32_t mode_value = 0,
        std::optional<std::uint32_t> status_value = std::nullopt,
        bool vhdl_value = false)
        : destination(destination_value)
        , path(path_value)
        , mode(mode_value)
        , status(status_value)
        , vhdl(vhdl_value)
    {
    }
};
struct FileClose {
    std::uint32_t handle { };
    bool clear_handle { }, ignore_zero { };
    FileClose(std::uint32_t handle_value = 0,
        bool clear_value = false, bool ignore_value = false)
        : handle(handle_value)
        , clear_handle(clear_value)
        , ignore_zero(ignore_value)
    {
    }
};
struct FileWriteLiteral {
    std::uint32_t handle { };
    std::string text;
    bool newline { true };
};
struct FileWriteFormatted {
    std::uint32_t handle { }, source { }, width { };
    OutputFormat format { };
    std::string prefix, suffix;
    bool newline { true }, signed_decimal { }, suppress_leading_zero { };
    std::uint32_t minimum_width { };
    bool left_justify { }, zero_pad { };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
    // %e/%f/%g digits after the point; the maximum value is automatic.
    std::uint32_t precision { std::numeric_limits<std::uint32_t>::max() };
    // For %t of an integral value: its time unit (IEEE 1800-2017
    // 21.2.1.3); zero when unknown.
    std::uint64_t time_unit_femtoseconds { };
};
struct FileWriteString {
    std::uint32_t handle { }, source { };
    std::string prefix, suffix;
    bool newline { true }, clear_source { };
};

enum class FileReadKind : std::uint8_t { line,
    character,
    unget };
enum class FileTextTargetKind : std::uint8_t {
    string_register,
    packed_register,
    packed_signal
};
struct FileReadLine {
    std::uint32_t destination { }, handle { }, target { }, source { };
    FileReadKind kind { FileReadKind::line };
    bool vhdl_textio { };
    FileTextTargetKind target_kind { FileTextTargetKind::string_register };
    std::uint32_t target_width { 1 };
};
struct FileEndOfFile {
    std::uint32_t destination { }, handle { };
    bool lookahead { };
    FileEndOfFile(std::uint32_t destination_value = 0,
        std::uint32_t handle_value = 0,
        bool lookahead_value = false)
        : destination(destination_value)
        , handle(handle_value)
        , lookahead(lookahead_value)
    {
    }
};
struct FileErrorStatus {
    std::uint32_t destination { }, handle { }, target { };
    FileTextTargetKind target_kind { FileTextTargetKind::string_register };
    std::uint32_t target_width { 1 };
};

enum class InputScanFormat : std::uint8_t {
    binary,
    octal,
    decimal,
    unsigned_decimal,
    hexadecimal,
    character,
    string,
    boolean_value,
    real,
    unformatted2,
    unformatted4,
    hierarchy,
    time
};
enum class InputScanTargetKind : std::uint8_t {
    packed_register,
    packed_signal,
    string_register,
    string_object
};
struct InputScanTarget {
    InputScanTargetKind kind { InputScanTargetKind::packed_register };
    std::uint32_t id { }, width { 1 };
    bool two_state { };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
};
struct InputScanConversion {
    std::string prefix;
    InputScanFormat format { InputScanFormat::decimal };
    std::uint32_t maximum_characters { };
    bool suppress { };
    InputScanTarget target;
};
struct FileScan {
    std::uint32_t destination { }, handle { }, source { };
    bool string_source { };
    std::vector<InputScanConversion> conversions;
    std::string trailing_text;
    bool require_assignments { };
    std::optional<std::uint32_t> success;
    bool consume_string_source { };
    std::string hierarchy_path;
    // The lexical time unit for %t conversions; the active $timeformat is
    // supplied at execution because $timeformat can change at run time.
    std::uint64_t time_unit_femtoseconds { };
    // A string register holding a run-time format (IEEE 1800-2017 21.3.4.3,
    // 21.6). `conversions` then hold only the targets of the unsuppressed
    // conversions, in order; execution parses the format and pairs them.
    std::optional<std::uint32_t> runtime_format;
    // $value$plusargs conversion (IEEE 1800-2017 21.6): a remainder with no
    // digits stores zero or an empty string, one with characters illegal for
    // the conversion stores 'bx.
    bool plusarg_value { };
    FileScan(
        std::uint32_t destination_value = 0,
        std::uint32_t handle_value = 0,
        std::uint32_t source_value = 0,
        bool string_source_value = false,
        std::vector<InputScanConversion> conversions_value = { },
        std::string trailing_value = { },
        bool require_value = false,
        std::optional<std::uint32_t> success_value = std::nullopt,
        bool consume_value = false,
        std::uint64_t time_unit_femtoseconds_value = 0U)
        : destination(destination_value)
        , handle(handle_value)
        , source(source_value)
        , string_source(string_source_value)
        , conversions(std::move(conversions_value))
        , trailing_text(std::move(trailing_value))
        , require_assignments(require_value)
        , success(success_value)
        , consume_string_source(consume_value)
        , time_unit_femtoseconds(time_unit_femtoseconds_value)
    {
    }
};

// A VHDL TextIO READ/SREAD is lowered to a string-source FileScan with one
// conversion whose prefix is one of these markers.
inline constexpr std::string_view vhdl_textio_read_marker
    = "\x1f" "fsim-vhdl-textio-read";
inline constexpr std::string_view vhdl_textio_read_2008_marker
    = "\x1f" "fsim-vhdl-textio-read-2008";
inline constexpr std::string_view vhdl_textio_radix_read_2008_marker
    = "\x1f" "fsim-vhdl-textio-radix-read-2008";
inline constexpr std::string_view vhdl_textio_sread_marker
    = "\x1f" "fsim-vhdl-textio-sread";

/// The TextIO marker of a VHDL TextIO scan, or empty for any other scan.
[[nodiscard]] inline std::string_view vhdl_textio_marker(
    const FileScan& operation) noexcept
{
    if (!operation.string_source || !operation.consume_string_source
        || operation.conversions.size() != 1U) {
        return { };
    }
    const std::string_view prefix { operation.conversions.front().prefix };
    return prefix == vhdl_textio_read_marker
            || prefix == vhdl_textio_read_2008_marker
            || prefix == vhdl_textio_radix_read_2008_marker
            || prefix == vhdl_textio_sread_marker
        ? prefix
        : std::string_view { };
}

[[nodiscard]] inline bool vhdl_textio_read(const FileScan& operation) noexcept
{
    return !vhdl_textio_marker(operation).empty();
}

[[nodiscard]] inline bool vhdl_textio_read_2008(
    const FileScan& operation) noexcept
{
    const auto marker = vhdl_textio_marker(operation);
    return marker == vhdl_textio_read_2008_marker
        || marker == vhdl_textio_radix_read_2008_marker;
}

[[nodiscard]] inline bool vhdl_textio_radix_read_2008(
    const FileScan& operation) noexcept
{
    return vhdl_textio_marker(operation)
        == vhdl_textio_radix_read_2008_marker;
}

[[nodiscard]] inline bool vhdl_textio_sread(const FileScan& operation) noexcept
{
    return vhdl_textio_marker(operation) == vhdl_textio_sread_marker;
}

enum class FileBinaryTargetKind : std::uint8_t {
    packed_register,
    packed_signal,
    container_register,
    container_object
};
struct FileBinaryRead {
    std::uint32_t destination { }, handle { }, target { };
    FileBinaryTargetKind target_kind { FileBinaryTargetKind::packed_register };
    std::uint32_t width { 1 };
    bool two_state { };
    std::uint32_t start { }, count { };
    bool has_start { }, has_count { };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
};

// position, size, truncate, state, mode and canseek are the VHDL
// FILE_POSITION, FILE_SIZE, FILE_TRUNCATE, FILE_STATE, FILE_MODE and
// FILE_CANSEEK operations (IEEE 1076-2019 5.5.2).
enum class FilePositionKind : std::uint8_t { seek,
    tell,
    rewind,
    position,
    size,
    truncate,
    state,
    mode,
    canseek };
struct FilePosition {
    std::uint32_t destination { }, handle { }, offset { }, origin { };
    FilePositionKind kind { FilePositionKind::tell };
    // A VHDL typed file holds one element per line, and its positions and
    // sizes count elements (IEEE 1076-2019 5.5.2).
    bool lines { };
};
// Whether the operation reads its offset and origin registers.
[[nodiscard]] constexpr bool file_position_uses_operands(
    const FilePositionKind kind) noexcept
{
    return kind == FilePositionKind::seek
        || kind == FilePositionKind::position
        || kind == FilePositionKind::truncate;
}
struct FileFlush {
    std::uint32_t handle { };
    bool all { };
};

} // namespace fsim::runtime::simir
