// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  include <windows.h>

#  include <string>
#  include <vector>

namespace {

[[nodiscard]] bool append_utf8_argument(
    const wchar_t* value,
    std::vector<std::string>& storage) {
  if (value == nullptr) {
    return false;
  }
  const auto required = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
      nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return false;
  }
  std::string encoded(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
          encoded.data(), required, nullptr, nullptr)
      != required) {
    return false;
  }
  encoded.pop_back();
  storage.push_back(std::move(encoded));
  return true;
}

}  // namespace

int wmain(const int argc, wchar_t** wide_argv) {
  try {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
      if (!append_utf8_argument(wide_argv[index], storage)) {
        return 2;
      }
    }
    std::vector<const char*> argv;
    argv.reserve(storage.size());
    for (const auto& argument : storage) {
      argv.push_back(argument.c_str());
    }
    return fsim::cli::run(
        argc, argv.data(), fsim::app::make_cli_services());
  } catch (...) {
    return 3;
  }
}
#else
int main(const int argc, char** argv) {
  return fsim::cli::run(argc, argv, fsim::app::make_cli_services());
}
#endif
