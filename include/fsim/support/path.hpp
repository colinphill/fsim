// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

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

}  // namespace fsim::support
