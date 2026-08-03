// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/environment.hpp"

#include <cstdlib>

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace fsim::support {

std::optional<std::string> environment_variable(
    const std::string_view name) {
  const std::string terminated_name{name};
#if defined(_WIN32)
  const auto wide_name_size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS,
      terminated_name.c_str(), -1, nullptr, 0);
  if (wide_name_size <= 0) {
    return std::nullopt;
  }
  std::wstring wide_name(static_cast<std::size_t>(wide_name_size), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS,
          terminated_name.c_str(), -1,
          wide_name.data(), wide_name_size)
      != wide_name_size) {
    return std::nullopt;
  }
  SetLastError(ERROR_SUCCESS);
  const auto required = GetEnvironmentVariableW(
      wide_name.c_str(), nullptr, 0);
  if (required == 0) {
    return GetLastError() == ERROR_ENVVAR_NOT_FOUND
        ? std::optional<std::string>{}
        : std::optional<std::string>{std::string{}};
  }
  std::wstring wide_value(static_cast<std::size_t>(required), L'\0');
  const auto written = GetEnvironmentVariableW(
      wide_name.c_str(), wide_value.data(), required);
  if (written == 0 || written >= required) {
    return std::nullopt;
  }
  wide_value.resize(written);
  if (wide_value.empty()) {
    return std::string{};
  }
  const auto utf8_size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS,
      wide_value.data(), static_cast<int>(wide_value.size()),
      nullptr, 0, nullptr, nullptr);
  if (utf8_size <= 0) {
    return std::nullopt;
  }
  std::string value(static_cast<std::size_t>(utf8_size), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS,
          wide_value.data(), static_cast<int>(wide_value.size()),
          value.data(), utf8_size, nullptr, nullptr)
      != utf8_size) {
    return std::nullopt;
  }
  return value;
#else
  const char* raw_value = std::getenv(terminated_name.c_str());
  if (raw_value == nullptr) {
    return std::nullopt;
  }
  return std::string{raw_value};
#endif
}

}  // namespace fsim::support
