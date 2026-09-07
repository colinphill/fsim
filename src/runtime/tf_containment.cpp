// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_containment.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fsim::runtime {
namespace {

[[nodiscard]] bool requested_access(
    const TfNativePointerAccess requested,
    const bool readable, const bool writable,
    const bool executable) noexcept {
  switch (requested) {
    case TfNativePointerAccess::Read:
      return readable;
    case TfNativePointerAccess::Write:
      return writable;
    case TfNativePointerAccess::Execute:
      return executable;
  }
  return false;
}

#if defined(_WIN32)

[[nodiscard]] bool readable_protection(const DWORD protection) noexcept {
  const auto base = protection & UINT32_C(0xff);
  return base == PAGE_READONLY || base == PAGE_READWRITE ||
         base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ ||
         base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool writable_protection(const DWORD protection) noexcept {
  const auto base = protection & UINT32_C(0xff);
  return base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
         base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool executable_protection(const DWORD protection) noexcept {
  const auto base = protection & UINT32_C(0xff);
  return base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
         base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

#endif

}  // namespace

TfContainmentError validate_tf_native_pointer(
    const void* const pointer, const std::size_t size,
    const TfNativePointerAccess access) noexcept {
  if (pointer == nullptr) return TfContainmentError::Null;
  if (size == 0U) return TfContainmentError::Empty;
  const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
  if (begin > std::numeric_limits<std::uintptr_t>::max() - size) {
    return TfContainmentError::Overflow;
  }
  const auto finish = begin + size;

#if defined(_WIN32)
  auto cursor = begin;
  while (cursor < finish) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info,
                     sizeof(info)) == 0U) {
      return TfContainmentError::Inspection;
    }
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (region_begin >
        std::numeric_limits<std::uintptr_t>::max() - info.RegionSize) {
      return TfContainmentError::Overflow;
    }
    const auto region_end = region_begin + info.RegionSize;
    if (info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0U) {
      return TfContainmentError::Unmapped;
    }
    if (!requested_access(access, readable_protection(info.Protect),
                          writable_protection(info.Protect),
                          executable_protection(info.Protect))) {
      return TfContainmentError::Access;
    }
    if (region_end <= cursor) return TfContainmentError::Inspection;
    cursor = std::min(finish, region_end);
  }
  return TfContainmentError::None;
#elif defined(__linux__)
  auto* const mappings = std::fopen("/proc/self/maps", "r");
  if (mappings == nullptr) return TfContainmentError::Inspection;
  struct FileGuard {
    std::FILE* value;
    ~FileGuard() { (void)std::fclose(value); }
  } guard{mappings};
  auto cursor = begin;
  char line[512]{};
  while (std::fgets(line, sizeof(line), mappings) != nullptr) {
    unsigned long long mapping_begin{};
    unsigned long long mapping_end{};
    char permissions[5]{};
    if (std::sscanf(line, "%llx-%llx %4s", &mapping_begin, &mapping_end,
                    permissions) != 3) {
      continue;
    }
    if (mapping_end <= cursor) continue;
    if (mapping_begin > cursor) return TfContainmentError::Unmapped;
    if (!requested_access(access, permissions[0] == 'r',
                          permissions[1] == 'w',
                          permissions[2] == 'x')) {
      return TfContainmentError::Access;
    }
    cursor = std::min(
        finish, static_cast<std::uintptr_t>(mapping_end));
    if (cursor == finish) return TfContainmentError::None;
  }
  return TfContainmentError::Unmapped;
#else
  (void)access;
  return begin < 4096U ? TfContainmentError::Unmapped
                       : TfContainmentError::None;
#endif
}

}  // namespace fsim::runtime
