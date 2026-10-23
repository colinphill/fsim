// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/input_format.hpp"
#include "fsim/runtime/file_operations.hpp"
#include "fsim/runtime/packed_value.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime::simir {

struct SystemVerilogTimeFormat;

struct InputScanValue {
    PackedLogic4 packed;
    std::string text;
    bool string { };
};
struct InputScanResult {
    std::int32_t assignments { };
    std::vector<std::optional<InputScanValue>> values;
    std::size_t consumed { };
};

[[nodiscard]] PackedLogic4 pack_file_text(
    std::string_view text, std::uint32_t width);
[[nodiscard]] InputScanResult scan_formatted_input(
    const FileScan& operation,
    const std::function<std::int32_t()>& read,
    const std::function<void(std::int32_t)>& unread);
[[nodiscard]] InputScanResult scan_formatted_input(
    const FileScan& operation,
    const std::function<std::int32_t()>& read,
    const std::function<void(std::int32_t)>& unread,
    const SystemVerilogTimeFormat& time_format);
[[nodiscard]] InputScanResult scan_formatted_string(
    const FileScan& operation, std::string_view input);
[[nodiscard]] InputScanResult scan_formatted_string(
    const FileScan& operation, std::string_view input,
    const SystemVerilogTimeFormat& time_format);

/// A VHDL TextIO READ into a STRING fills the whole target (IEEE 1076-2019
/// 16.4): give each such conversion without an explicit width the target's
/// current length. `string_length` returns that length for string targets and
/// nothing for other targets. Returns false when a length exceeds the scan
/// limit.
[[nodiscard]] bool size_vhdl_textio_string_reads(FileScan& operation,
    const std::function<std::optional<std::size_t>(const InputScanTarget&)>&
        string_length);

/// Removes the characters a string-source scan consumed when the scan
/// consumes its source. TextIO removes each accepted character from LINE as
/// it forms a representation, so a later conversion failure keeps the valid
/// prefix removed.
void consume_scanned_source(const FileScan& operation, std::string& source,
    const InputScanResult& scanned);

[[nodiscard]] InputScanFormat runtime_input_format(
    frontend::InputScanFormat format);

/// Whether `format` can assign `target`: text conversions assign strings and
/// numeric ones assign packed values of a matching scalar kind.
[[nodiscard]] bool input_scan_target_accepts(
    const InputScanTarget& target, InputScanFormat format) noexcept;

/// Pairs a run-time scan format with the scan's targets (IEEE 1800-2017
/// 21.3.4.3, 21.6): each unsuppressed conversion takes the next target.
/// Returns false, leaving `operation` unspecified, when the format is invalid,
/// its conversions do not match the targets, or a $value$plusargs format has
/// other than one assignment conversion.
[[nodiscard]] bool apply_runtime_scan_format(
    FileScan& operation, std::string_view format);

/// The plusarg string of a $value$plusargs user string: the text before its
/// one conversion (IEEE 1800-2017 21.6); nothing for another format.
[[nodiscard]] std::optional<std::string> plusarg_format_prefix(
    std::string_view user_string);

/// The first plusarg, without its leading '+', whose prefix matches `query`
/// (IEEE 1800-2017 21.6), or nothing. A `format_query` is a $value$plusargs
/// user string whose plusarg string is matched.
[[nodiscard]] std::optional<std::string_view> select_plusarg(
    const std::vector<std::string>& plusargs, std::string_view query,
    bool format_query);

/// Applies the $value$plusargs conversion rules to a scan of the selected
/// plusarg `source` (IEEE 1800-2017 21.6): a remainder with no digits stores
/// zero or an empty string, and one the conversion rejects stores 'bx.
void complete_plusarg_value(const FileScan& operation, std::string_view source,
    InputScanResult& scanned);

/// Whether a scan converted everything it requests: the single value of a
/// VHDL SREAD, or every unsuppressed conversion otherwise.
[[nodiscard]] bool file_scan_complete(
    const FileScan& operation, const InputScanResult& scanned) noexcept;

} // namespace fsim::runtime::simir
