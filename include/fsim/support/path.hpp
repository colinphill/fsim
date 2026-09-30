// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace fsim::support {

/// Construct a native path from fsim's UTF-8 public/tool representation.
[[nodiscard]] inline std::filesystem::path path_from_utf8(
    const std::string_view value) {
  std::u8string encoded;
  encoded.reserve(value.size());
  for (const auto byte : value) {
    encoded.push_back(static_cast<char8_t>(
        static_cast<unsigned char>(byte)));
  }
  return std::filesystem::path{encoded};
}

/// Return a normalized generic UTF-8 representation at public/tool seams.
[[nodiscard]] inline std::string path_to_utf8(
    const std::filesystem::path& value) {
  const auto encoded = value.generic_u8string();
  return {
      reinterpret_cast<const char*>(encoded.data()),
      encoded.size()};
}

#if defined(_WIN32)
namespace detail {

/// Recognize Windows device names such as NUL, which an extended-length
/// spelling would turn into ordinary file names.
[[nodiscard]] inline bool is_windows_device_name(
    const std::filesystem::path& value) {
  auto name = value.filename().native();
  if (const auto dot = name.find(L'.'); dot != name.npos) {
    name.resize(dot);
  }
  while (!name.empty() && name.back() == L' ') {
    name.pop_back();
  }
  for (auto& character : name) {
    if (character >= L'a' && character <= L'z') {
      character = static_cast<wchar_t>(character - L'a' + L'A');
    }
  }
  if (name == L"CON" || name == L"PRN" || name == L"AUX" || name == L"NUL"
      || name == L"CONIN$" || name == L"CONOUT$") {
    return true;
  }
  return name.size() == 4
      && (name.starts_with(L"COM") || name.starts_with(L"LPT"))
      && name[3] >= L'1' && name[3] <= L'9';
}

}  // namespace detail
#endif

/// Use an absolute extended-length path at Windows filesystem I/O seams.
/// Keep public path spellings and serialized artifact names unchanged.
/// Windows does not translate '/' after an extended-length prefix, so apply
/// this again after joining generic relative names onto an extended path.
/// Empty paths and device names such as NUL keep their original spelling.
[[nodiscard]] inline std::filesystem::path path_for_native_io(
    const std::filesystem::path& value) {
#if defined(_WIN32)
  const auto& spelling = value.native();
  if (spelling.empty() || detail::is_windows_device_name(value)) {
    return value;
  }
  if (spelling.starts_with(L"\\\\?\\") || spelling.starts_with(L"\\\\.\\")) {
    auto preferred = value;
    preferred.make_preferred();
    return preferred;
  }
  std::error_code error;
  auto absolute = std::filesystem::absolute(value, error);
  if (error) {
    return value;
  }
  absolute = absolute.lexically_normal();
  absolute.make_preferred();
  const auto& native = absolute.native();
  if (native.starts_with(L"\\\\")) {
    return std::filesystem::path{L"\\\\?\\UNC\\" + native.substr(2)};
  }
  return std::filesystem::path{L"\\\\?\\" + native};
#else
  return value;
#endif
}

/// Return the ordinary spelling of an extended-length path, such as an entry
/// from a directory iterator over a path_for_native_io root, for display,
/// serialization, or comparison with ordinary paths.
[[nodiscard]] inline std::filesystem::path path_from_native_io(
    const std::filesystem::path& value) {
#if defined(_WIN32)
  const auto& spelling = value.native();
  if (spelling.starts_with(L"\\\\?\\UNC\\")) {
    return std::filesystem::path{L"\\\\" + spelling.substr(8)};
  }
  if (spelling.starts_with(L"\\\\?\\")) {
    return std::filesystem::path{spelling.substr(4)};
  }
#endif
  return value;
}

/// Recognize absolute source names produced on either supported host family.
[[nodiscard]] inline bool path_is_portably_absolute(
    const std::filesystem::path& value) {
  if (value.is_absolute()) {
    return true;
  }
  const auto encoded = path_to_utf8(value);
  if (!encoded.empty()
      && (encoded.front() == '/' || encoded.front() == '\\')) {
    return true;
  }
  const auto ascii_letter = [](const char character) {
    return (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z');
  };
  return encoded.size() >= 3U && ascii_letter(encoded[0])
      && encoded[1] == ':'
      && (encoded[2] == '/' || encoded[2] == '\\');
}

namespace detail {

/// Resolve a path with lexical fallbacks when filesystem queries fail.
[[nodiscard]] inline std::filesystem::path normalized_absolute_path(
    const std::filesystem::path& path) {
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return path.lexically_normal();
  }
  auto canonical = std::filesystem::weakly_canonical(
      path_for_native_io(absolute), error);
  return error ? absolute.lexically_normal() : path_from_native_io(canonical);
}

}  // namespace detail

}  // namespace fsim::support
