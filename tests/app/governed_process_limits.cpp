// SPDX-License-Identifier: Apache-2.0
#include "governed_process_limits.hpp"

#include <algorithm>
#include <cstdint>
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

namespace fsim::test {
namespace {

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool governed_process_uses_address_sanitizer = true;
#else
constexpr bool governed_process_uses_address_sanitizer = false;
#endif
#elif defined(__SANITIZE_ADDRESS__)
constexpr bool governed_process_uses_address_sanitizer = true;
#else
constexpr bool governed_process_uses_address_sanitizer = false;
#endif

constexpr std::uint64_t governed_process_address_space_ceiling
    = 6ULL * 1024ULL * 1024ULL * 1024ULL;

[[noreturn]] void governed_process_limit_failure(const char* operation)
{
    std::cerr << "governed process address-space ceiling failed: "
              << operation << '\n';
    std::abort();
}

} // namespace

void install_governed_process_address_space_ceiling()
{
    // ASan reserves a large virtual address range for shadow memory. Applying
    // a finite RLIMIT_AS or job-object limit after startup makes its next
    // internal mapping fail before the governed test workload can run.
    if constexpr (governed_process_uses_address_sanitizer) {
        return;
    }
#if defined(_WIN32)
    static HANDLE job = [] {
        const auto created = CreateJobObjectW(nullptr, nullptr);
        if (created == nullptr) {
            governed_process_limit_failure("CreateJobObjectW");
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits { };
        limits.BasicLimitInformation.LimitFlags
            = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(
            governed_process_address_space_ceiling);
        if (SetInformationJobObject(created, JobObjectExtendedLimitInformation,
                &limits, static_cast<DWORD>(sizeof(limits)))
            == 0) {
            governed_process_limit_failure("SetInformationJobObject");
        }
        if (AssignProcessToJobObject(created, GetCurrentProcess()) == 0) {
            governed_process_limit_failure("AssignProcessToJobObject");
        }
        return created;
    }();
    (void)job;
#else
    static_assert(governed_process_address_space_ceiling
        <= std::numeric_limits<rlim_t>::max());
    rlimit current { };
    if (getrlimit(RLIMIT_AS, &current) != 0) {
        governed_process_limit_failure("getrlimit(RLIMIT_AS)");
    }
    const auto ceiling = static_cast<rlim_t>(
        governed_process_address_space_ceiling);
    auto bounded = ceiling;
    if (current.rlim_max != RLIM_INFINITY) {
        bounded = std::min(bounded, current.rlim_max);
    }
    if (current.rlim_cur != RLIM_INFINITY) {
        bounded = std::min(bounded, current.rlim_cur);
    }
    current.rlim_cur = bounded;
    if (setrlimit(RLIMIT_AS, &current) != 0) {
        governed_process_limit_failure("setrlimit(RLIMIT_AS)");
    }
#endif
}

} // namespace fsim::test
