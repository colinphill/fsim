// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/systemverilog_scalar.hpp"

#include <cstdint>
#include <optional>
#include <string>
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
    real
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
    FileScan(
        std::uint32_t destination_value = 0,
        std::uint32_t handle_value = 0,
        std::uint32_t source_value = 0,
        bool string_source_value = false,
        std::vector<InputScanConversion> conversions_value = { },
        std::string trailing_value = { },
        bool require_value = false,
        std::optional<std::uint32_t> success_value = std::nullopt,
        bool consume_value = false)
        : destination(destination_value)
        , handle(handle_value)
        , source(source_value)
        , string_source(string_source_value)
        , conversions(std::move(conversions_value))
        , trailing_text(std::move(trailing_value))
        , require_assignments(require_value)
        , success(success_value)
        , consume_string_source(consume_value)
    {
    }
};

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

enum class FilePositionKind : std::uint8_t { seek,
    tell,
    rewind };
struct FilePosition {
    std::uint32_t destination { }, handle { }, offset { }, origin { };
    FilePositionKind kind { FilePositionKind::tell };
};
struct FileFlush {
    std::uint32_t handle { };
    bool all { };
};

} // namespace fsim::runtime::simir
