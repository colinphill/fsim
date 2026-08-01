// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fsim::runtime::simir {

enum class OutputFormat : std::uint8_t;

struct FileOpen {
  std::uint32_t destination{}, path{}, mode{};
};
struct FileClose { std::uint32_t handle{}; };
struct FileWriteLiteral {
  std::uint32_t handle{};
  std::string text;
  bool newline{true};
};
struct FileWriteFormatted {
  std::uint32_t handle{}, source{}, width{};
  OutputFormat format{};
  std::string prefix, suffix;
  bool newline{true}, signed_decimal{}, suppress_leading_zero{};
  std::uint32_t minimum_width{};
  bool left_justify{}, zero_pad{};
};
struct FileWriteString {
  std::uint32_t handle{}, source{};
  std::string prefix, suffix;
  bool newline{true};
};

enum class FileReadKind : std::uint8_t { line, character, unget };
struct FileReadLine {
  std::uint32_t destination{}, handle{}, target{}, source{};
  FileReadKind kind{FileReadKind::line};
};
struct FileEndOfFile { std::uint32_t destination{}, handle{}; };
struct FileErrorStatus {
  std::uint32_t destination{}, handle{}, target{};
};

enum class InputScanFormat : std::uint8_t {
  binary, octal, decimal, unsigned_decimal, hexadecimal, character, string
};
enum class InputScanTargetKind : std::uint8_t {
  packed_register, packed_signal, string_register, string_object
};
struct InputScanTarget {
  InputScanTargetKind kind{InputScanTargetKind::packed_register};
  std::uint32_t id{}, width{1};
  bool two_state{};
};
struct InputScanConversion {
  std::string prefix;
  InputScanFormat format{InputScanFormat::decimal};
  std::uint32_t maximum_characters{};
  bool suppress{};
  InputScanTarget target;
};
struct FileScan {
  std::uint32_t destination{}, handle{}, source{};
  bool string_source{};
  std::vector<InputScanConversion> conversions;
  std::string trailing_text;
};

enum class FileBinaryTargetKind : std::uint8_t {
  packed_register, packed_signal, container_register, container_object
};
struct FileBinaryRead {
  std::uint32_t destination{}, handle{}, target{};
  FileBinaryTargetKind target_kind{FileBinaryTargetKind::packed_register};
  std::uint32_t width{1};
  bool two_state{};
  std::uint32_t start{}, count{};
  bool has_start{}, has_count{};
};

enum class FilePositionKind : std::uint8_t { seek, tell, rewind };
struct FilePosition {
  std::uint32_t destination{}, handle{}, offset{}, origin{};
  FilePositionKind kind{FilePositionKind::tell};
};
struct FileFlush {
  std::uint32_t handle{};
  bool all{};
};

}  // namespace fsim::runtime::simir
