// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/support/teardown.hpp"

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
    fsim::support::enable_exit_without_teardown();
    return fsim::cli::run(
        argc, argv.data(), fsim::app::make_stdio_cli_services());
  } catch (...) {
    return 3;
  }
}
#else
#  if defined(__linux__) && defined(__GLIBC__)
#    include <array>
#    include <cstdlib>
#    include <cstring>
#    include <fcntl.h>
#    include <string>
#    include <string_view>
#    include <unistd.h>

namespace {

// A large design's heap is hundreds of megabytes of small allocations; with
// transparent huge pages in madvise mode, glibc only asks for huge pages when
// its glibc.malloc.hugetlb tunable is set, and it reads tunables only at
// process start. Re-executing once with the tunable set replaces the image
// in place (same process, arguments and descriptors) and removes most page
// faults and TLB misses. FSIM_HEAP_HUGE_PAGES=0 keeps the default heap.
void enable_heap_huge_pages(char** argv) {
  if (const char* setting = std::getenv("FSIM_HEAP_HUGE_PAGES");
      setting != nullptr && std::strcmp(setting, "0") == 0) {
    return;
  }
  const char* tunables = std::getenv("GLIBC_TUNABLES");
  if (tunables != nullptr && std::strstr(tunables, "glibc.malloc.hugetlb") != nullptr) {
    return;
  }
  const int policy = ::open("/sys/kernel/mm/transparent_hugepage/enabled",
      O_RDONLY | O_CLOEXEC);
  if (policy < 0) {
    return;
  }
  std::array<char, 128> modes {};
  const auto length = ::read(policy, modes.data(), modes.size() - 1U);
  ::close(policy);
  if (length <= 0
      || std::string_view { modes.data(), static_cast<std::size_t>(length) }
             .find("[madvise]") == std::string_view::npos) {
    return;
  }
  const std::string previous = tunables != nullptr ? tunables : "";
  const std::string value = previous.empty()
      ? "glibc.malloc.hugetlb=1" : previous + ":glibc.malloc.hugetlb=1";
  if (::setenv("GLIBC_TUNABLES", value.c_str(), 1) != 0) {
    return;
  }
  ::execv("/proc/self/exe", argv);
  // The image could not be replaced; continue with the default heap.
  if (tunables != nullptr) {
    ::setenv("GLIBC_TUNABLES", previous.c_str(), 1);
  } else {
    ::unsetenv("GLIBC_TUNABLES");
  }
}

}  // namespace
#  endif

int main(const int argc, char** argv) {
#  if defined(__linux__) && defined(__GLIBC__)
  enable_heap_huge_pages(argv);
#  endif
  fsim::support::enable_exit_without_teardown();
  return fsim::cli::run(argc, argv, fsim::app::make_stdio_cli_services());
}
#endif
