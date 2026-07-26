// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace fsim::compiler::detail {

// A process-aware directory lock used by persistent cache writers. Abandoned
// lock directories are recovered when their recorded owner no longer exists.
// A marker-less or malformed lock is only recovered after stale_after, which
// avoids stealing a lock from a process suspended between directory creation
// and owner publication.
class CacheDirectoryLock final {
public:
    explicit CacheDirectoryLock(
        std::filesystem::path path,
        std::error_code& error,
        std::chrono::milliseconds wait_for = std::chrono::milliseconds::max(),
        std::chrono::seconds stale_after = std::chrono::seconds{30});

    CacheDirectoryLock(const CacheDirectoryLock&) = delete;
    CacheDirectoryLock& operator=(const CacheDirectoryLock&) = delete;

    ~CacheDirectoryLock();

    [[nodiscard]] bool held() const noexcept;

private:
    std::filesystem::path path_;
    std::string token_;
    bool held_{};
};

// Replaces destination with source as one filesystem namespace operation.
// Both paths must be on the same volume. On success source no longer exists.
[[nodiscard]] bool atomic_replace_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::error_code& error) noexcept;

} // namespace fsim::compiler::detail
