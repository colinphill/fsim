// SPDX-License-Identifier: Apache-2.0
#include "uvm_process_limits.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace fsim::tests::app {
namespace {

[[noreturn]] void limit_failure(const char *operation) {
  std::cerr << "UVM process address-space ceiling failed: " << operation
            << '\n';
  std::abort();
}

} // namespace

void install_uvm_process_address_space_ceiling() {
#if defined(_WIN32)
  static HANDLE job = [] {
    const auto created = CreateJobObjectW(nullptr, nullptr);
    if (created == nullptr)
      limit_failure("CreateJobObjectW");

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit =
        static_cast<SIZE_T>(uvm_process_address_space_ceiling);
    if (SetInformationJobObject(created, JobObjectExtendedLimitInformation,
                                &limits,
                                static_cast<DWORD>(sizeof(limits))) == 0)
      limit_failure("SetInformationJobObject");
    if (AssignProcessToJobObject(created, GetCurrentProcess()) == 0)
      limit_failure("AssignProcessToJobObject");
    return created;
  }();
  (void)job;
#else
  static_assert(uvm_process_address_space_ceiling <=
                std::numeric_limits<rlim_t>::max());
  rlimit current{};
  if (getrlimit(RLIMIT_AS, &current) != 0)
    limit_failure("getrlimit(RLIMIT_AS)");
  const auto ceiling =
      static_cast<rlim_t>(uvm_process_address_space_ceiling);
  auto bounded = ceiling;
  if (current.rlim_max != RLIM_INFINITY)
    bounded = std::min(bounded, current.rlim_max);
  if (current.rlim_cur != RLIM_INFINITY)
    bounded = std::min(bounded, current.rlim_cur);
  current.rlim_cur = bounded;
  if (setrlimit(RLIMIT_AS, &current) != 0)
    limit_failure("setrlimit(RLIMIT_AS)");
#endif
}

} // namespace fsim::tests::app
