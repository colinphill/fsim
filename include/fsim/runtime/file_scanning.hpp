// SPDX-License-Identifier: Apache-2.0
#pragma once

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

/// Whether a scan converted everything it requests: the single value of a
/// VHDL SREAD, or every unsuppressed conversion otherwise.
[[nodiscard]] bool file_scan_complete(
    const FileScan& operation, const InputScanResult& scanned) noexcept;

} // namespace fsim::runtime::simir
