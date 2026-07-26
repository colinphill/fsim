// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"
#include "fsim/compiler/cache_support.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#else
#  include <csignal>
#  include <fcntl.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace fsim::compiler {
namespace {

constexpr std::string_view kMagic = "FSIM-OBJECT-CACHE-V1\n";
std::atomic_uint64_t temp_counter{};
std::atomic_uint64_t lock_counter{};

void hash_size(support::Sha256& hasher, const std::size_t size) noexcept {
    std::array<std::byte, sizeof(std::uint64_t)> encoded{};
    const auto value = static_cast<std::uint64_t>(size);
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        encoded[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
    hasher.update(encoded);
}

std::optional<std::vector<std::byte>> read_all(
    const std::filesystem::path& path, std::error_code& error) {
    error.clear();
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        error = std::make_error_code(std::errc::file_too_large);
        return std::nullopt;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (!stream && !result.empty()) {
        error = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }
    return result;
}

} // namespace

namespace detail {
namespace {

constexpr std::string_view kLockMagic = "FSIM-CACHE-LOCK-V1";
constexpr std::chrono::milliseconds kLockPoll{5};

[[nodiscard]] std::uint64_t current_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

[[nodiscard]] bool process_is_alive(const std::uint64_t process_id) noexcept {
    if (process_id == 0
        || process_id > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
#if defined(_WIN32)
    const HANDLE process =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                    static_cast<DWORD>(process_id));
    if (process == nullptr) {
        // Access denial is conservative: a process that cannot be inspected
        // must be treated as alive.
        return GetLastError() != ERROR_INVALID_PARAMETER;
    }
    const auto state = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return state == WAIT_TIMEOUT;
#else
    const auto native_id = static_cast<pid_t>(process_id);
    if (::kill(native_id, 0) == 0) {
        return true;
    }
    return errno == EPERM;
#endif
}

struct LockOwner {
    std::uint64_t process_id{};
    std::string token;
};

[[nodiscard]] std::optional<LockOwner> read_owner(
    const std::filesystem::path& directory) {
    std::ifstream stream(directory / "owner", std::ios::binary);
    std::string magic;
    LockOwner owner;
    if (!stream || !std::getline(stream, magic) || magic != kLockMagic
        || !(stream >> owner.process_id >> owner.token) || owner.token.empty()) {
        return std::nullopt;
    }
    return owner;
}

[[nodiscard]] bool write_owner_exclusive(
    const std::filesystem::path& directory,
    const LockOwner& owner,
    std::error_code& error) {
    const auto marker = directory / "owner";
    const auto text = std::string{kLockMagic} + "\n"
        + std::to_string(owner.process_id) + " " + owner.token + "\n";
#if defined(_WIN32)
    const HANDLE file = CreateFileW(
        marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = std::error_code{
            static_cast<int>(GetLastError()), std::system_category()};
        return false;
    }
    DWORD written = 0;
    const bool ok =
        WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
        && written == static_cast<DWORD>(text.size()) && FlushFileBuffers(file);
    const auto native_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        error = std::error_code{static_cast<int>(native_error), std::system_category()};
        return false;
    }
#else
    const int descriptor =
        ::open(marker.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        error = std::error_code{errno, std::generic_category()};
        return false;
    }
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto count =
            ::write(descriptor, text.data() + offset, text.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        error = std::error_code{errno, std::generic_category()};
        ::close(descriptor);
        return false;
    }
    if (::fsync(descriptor) != 0) {
        error = std::error_code{errno, std::generic_category()};
        ::close(descriptor);
        return false;
    }
    ::close(descriptor);
#endif
    error.clear();
    return true;
}

[[nodiscard]] bool old_enough(
    const std::filesystem::path& path,
    const std::chrono::seconds stale_after,
    std::error_code& error) {
    const auto stamp = std::filesystem::last_write_time(path, error);
    if (error) {
        return false;
    }
    return std::filesystem::file_time_type::clock::now() - stamp >= stale_after;
}

[[nodiscard]] bool stale_lock(
    const std::filesystem::path& path,
    const std::chrono::seconds stale_after,
    std::error_code& error) {
    error.clear();
    if (const auto owner = read_owner(path)) {
        return !process_is_alive(owner->process_id);
    }
    return old_enough(path, stale_after, error);
}

[[nodiscard]] bool recover_stale_lock(
    const std::filesystem::path& path,
    const std::string_view contender_token,
    const std::chrono::seconds stale_after,
    std::error_code& error) {
    const auto original_owner = read_owner(path);
    if (!stale_lock(path, stale_after, error) || error) {
        return false;
    }

    const auto quarantine =
        std::filesystem::path{path.string() + ".stale." + std::string{contender_token}};
    std::filesystem::rename(path, quarantine, error);
    if (error) {
        // Another contender either recovered the lock or its owner released it.
        error.clear();
        return false;
    }

    const auto quarantined_owner = read_owner(quarantine);
    const bool same_owner =
        (!original_owner && !quarantined_owner)
        || (original_owner && quarantined_owner
            && original_owner->process_id == quarantined_owner->process_id
            && original_owner->token == quarantined_owner->token);
    std::error_code ignored;
    if (same_owner) {
        std::filesystem::remove_all(quarantine, ignored);
        return true;
    }

    // Ownership changed while recovery was being attempted. Restore the
    // directory if possible and conservatively leave it alone otherwise.
    std::filesystem::rename(quarantine, path, ignored);
    return false;
}

[[nodiscard]] std::string new_lock_token() {
    const auto serial = lock_counter.fetch_add(1, std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(current_process_id()) + "-" + std::to_string(now) + "-"
        + std::to_string(serial);
}

} // namespace

CacheDirectoryLock::CacheDirectoryLock(
    std::filesystem::path path,
    std::error_code& error,
    const std::chrono::milliseconds wait_for,
    const std::chrono::seconds stale_after)
    : path_(std::move(path)), token_(new_lock_token()) {
    const bool wait_forever = wait_for == std::chrono::milliseconds::max();
    const auto deadline = wait_forever
        ? std::chrono::steady_clock::time_point::max()
        : std::chrono::steady_clock::now() + wait_for;
    while (true) {
        error.clear();
        if (std::filesystem::create_directory(path_, error)) {
            const LockOwner owner{current_process_id(), token_};
            if (write_owner_exclusive(path_, owner, error)) {
                held_ = true;
                return;
            }
            // Do not remove a directory now owned by a racing process.
            if (const auto published = read_owner(path_);
                published && published->token == token_) {
                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);
            }
            return;
        }
        if (error && error != std::errc::file_exists) {
            return;
        }
        error.clear();
        if (recover_stale_lock(path_, token_, stale_after, error)) {
            continue;
        }
        if (!wait_forever && std::chrono::steady_clock::now() >= deadline) {
            error = std::make_error_code(std::errc::timed_out);
            return;
        }
        std::this_thread::sleep_for(kLockPoll);
    }
}

CacheDirectoryLock::~CacheDirectoryLock() {
    if (!held_) {
        return;
    }
    const auto owner = read_owner(path_);
    if (!owner || owner->token != token_
        || owner->process_id != current_process_id()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove(path_ / "owner", ignored);
    std::filesystem::remove(path_, ignored);
}

bool CacheDirectoryLock::held() const noexcept {
    return held_;
}

bool atomic_replace_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::error_code& error) noexcept {
    error.clear();
#if defined(_WIN32)
    if (MoveFileExW(
            source.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error =
        std::error_code{static_cast<int>(GetLastError()), std::system_category()};
    return false;
#else
    if (::rename(source.c_str(), destination.c_str()) == 0) {
        return true;
    }
    error = std::error_code{errno, std::generic_category()};
    return false;
#endif
}

} // namespace detail

CacheKeyBuilder::CacheKeyBuilder() {
    add("format", "fsim-cache-key-v1");
}

CacheKeyBuilder& CacheKeyBuilder::add(
    const std::string_view label, const std::string_view value) noexcept {
    return add_bytes(label, std::as_bytes(std::span{value.data(), value.size()}));
}

CacheKeyBuilder& CacheKeyBuilder::add_bytes(
    const std::string_view label, const std::span<const std::byte> value) noexcept {
    if (finished_) {
        return *this;
    }
    hash_size(hasher_, label.size());
    hasher_.update(label);
    hash_size(hasher_, value.size());
    hasher_.update(value);
    return *this;
}

bool CacheKeyBuilder::add_file(
    const std::string_view label, const std::filesystem::path& path, std::error_code& error) {
    const auto data = read_all(path, error);
    if (!data) {
        return false;
    }
    add(label, path.generic_string());
    add_bytes("content", *data);
    return true;
}

std::string CacheKeyBuilder::finish() {
    finished_ = true;
    return support::Sha256::hex(hasher_.finish());
}

ObjectCache::ObjectCache(std::filesystem::path root) : root_(std::move(root)) {}

const std::filesystem::path& ObjectCache::root() const noexcept {
    return root_;
}

std::filesystem::path ObjectCache::path_for(const std::string_view key) const {
    if (!valid_cache_key(key)) {
        return {};
    }
    return root_ / std::string{key.substr(0, 2)} / (std::string{key} + ".fobj");
}

std::optional<std::vector<std::byte>> ObjectCache::load(
    const std::string_view key, std::error_code& error) const {
    error.clear();
    const auto path = path_for(key);
    if (path.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return std::nullopt;
    }
    auto encoded = read_all(path, error);
    if (!encoded) {
        return std::nullopt;
    }

    const auto header_size = kMagic.size() + 64 + 1;
    if (encoded->size() < header_size) {
        error = std::make_error_code(std::errc::illegal_byte_sequence);
        return std::nullopt;
    }
    const auto* raw = reinterpret_cast<const char*>(encoded->data());
    if (std::string_view{raw, kMagic.size()} != kMagic
        || raw[kMagic.size() + 64] != '\n') {
        error = std::make_error_code(std::errc::illegal_byte_sequence);
        return std::nullopt;
    }

    const std::string_view expected{raw + kMagic.size(), 64};
    const std::span<const std::byte> payload{
        encoded->data() + static_cast<std::ptrdiff_t>(header_size),
        encoded->size() - header_size};
    if (support::Sha256::hex(support::Sha256::digest(payload)) != expected) {
        error = std::make_error_code(std::errc::illegal_byte_sequence);
        return std::nullopt;
    }
    return std::vector<std::byte>{payload.begin(), payload.end()};
}

bool ObjectCache::store(
    const std::string_view key,
    const std::span<const std::byte> payload,
    std::error_code& error) const {
    error.clear();
    const auto path = path_for(key);
    if (path.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    detail::CacheDirectoryLock lock{path.string() + ".lock", error};
    if (!lock.held()) {
        return false;
    }

    const auto suffix = temp_counter.fetch_add(1, std::memory_order_relaxed);
    const auto temporary = path.string() + ".tmp." + std::to_string(suffix);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = std::make_error_code(std::errc::io_error);
            return false;
        }
        const auto checksum = support::Sha256::hex(support::Sha256::digest(payload));
        stream.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        stream.write(checksum.data(), static_cast<std::streamsize>(checksum.size()));
        stream.put('\n');
        stream.write(
            reinterpret_cast<const char*>(payload.data()),
            static_cast<std::streamsize>(payload.size()));
        stream.flush();
        if (!stream) {
            error = std::make_error_code(std::errc::io_error);
        }
    }
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }

    if (!detail::atomic_replace_file(temporary, path, error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool ObjectCache::erase(const std::string_view key, std::error_code& error) const {
    error.clear();
    const auto path = path_for(key);
    if (path.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    return std::filesystem::remove(path, error);
}

bool valid_cache_key(const std::string_view key) noexcept {
    if (key.size() != 64) {
        return false;
    }
    for (const char c : key) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace fsim::compiler
