// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"
#include "fsim/compiler/cache_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>
#include <unordered_set>

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
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
constexpr std::uintmax_t kMaximumCacheEntryBytes =
    256U * 1024U * 1024U;
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
    const std::filesystem::path& path,
    std::error_code& error,
    const std::uintmax_t maximum_size =
        std::numeric_limits<std::uintmax_t>::max()) {
    error.clear();
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    if (size > maximum_size
        || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())
        || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
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

[[nodiscard]] bool cache_entry_filename(
    const std::filesystem::path& path,
    std::string& key) {
    const auto filename = path.filename().string();
    constexpr std::string_view suffix = ".fobj";
    if (!filename.ends_with(suffix)) {
        return false;
    }
    key = filename.substr(0, filename.size() - suffix.size());
    return valid_cache_key(key)
        && path.parent_path().filename().string() == key.substr(0, 2);
}

[[nodiscard]] bool cache_temporary_filename(
    const std::filesystem::path& path,
    std::string& key) {
    const auto filename = path.filename().string();
    constexpr std::string_view marker = ".fobj.tmp.";
    if (filename.size() <= 64 + marker.size()
        || std::string_view{filename}.substr(64, marker.size()) != marker) {
        return false;
    }
    key = filename.substr(0, 64);
    return valid_cache_key(key)
        && path.parent_path().filename().string() == key.substr(0, 2);
}

[[nodiscard]] bool elapsed_at_least(
    const std::filesystem::file_time_type stamp,
    const std::chrono::seconds age) {
    if (age <= std::chrono::seconds::zero()) {
        return true;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    return stamp <= now && now - stamp >= age;
}

void add_saturating(
    std::uintmax_t& total,
    const std::uintmax_t value) noexcept {
    if (value > std::numeric_limits<std::uintmax_t>::max() - total) {
        total = std::numeric_limits<std::uintmax_t>::max();
    } else {
        total += value;
    }
}

} // namespace

namespace detail {
namespace {

constexpr std::string_view kLockMagic = "FSIM-CACHE-LOCK-V1";
constexpr std::chrono::milliseconds kLockPoll{5};

struct ActiveLocks {
    std::mutex mutex;
    std::unordered_set<std::string> tokens;
};

[[nodiscard]] ActiveLocks& active_locks() {
    // Construct on first lock acquisition so this registry also outlives any
    // static-duration CacheDirectoryLock whose constructor registered in it.
    static ActiveLocks locks;
    return locks;
}

void register_active_lock(const std::string& token) {
    auto& locks = active_locks();
    const std::scoped_lock lock{locks.mutex};
    locks.tokens.insert(token);
}

void unregister_active_lock(const std::string& token) noexcept {
    auto& locks = active_locks();
    const std::scoped_lock lock{locks.mutex};
    locks.tokens.erase(token);
}

[[nodiscard]] bool active_lock(const std::string& token) noexcept {
    auto& locks = active_locks();
    const std::scoped_lock lock{locks.mutex};
    return locks.tokens.contains(token);
}

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
        if (owner->process_id == current_process_id()) {
            return !active_lock(owner->token);
        }
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
            // Register before publishing the owner record. A same-process
            // contender that observes the record must never reclaim a lock
            // that is still active in this process.
            try {
                register_active_lock(token_);
            } catch (const std::bad_alloc&) {
                error = std::make_error_code(std::errc::not_enough_memory);
                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);
                return;
            }
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
            unregister_active_lock(token_);
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
        unregister_active_lock(token_);
        return;
    }
    std::error_code ignored;
    std::filesystem::remove(path_ / "owner", ignored);
    std::filesystem::remove(path_, ignored);
    // Windows virus scanners and indexers can transiently prevent either
    // removal. Once this destructor has stopped touching the canonical path,
    // make its token reclaimable so a later operation in the same process
    // cannot wait forever on an owner record that no longer owns anything.
    unregister_active_lock(token_);
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
    // Endpoint scanners on hosted Windows runners have held newly replaced
    // cache files for longer than the former 505 ms window. Keep the retry
    // bounded, but allow five seconds for a transient sharing lock to clear.
    constexpr DWORD maximum_attempts = 1001;
    for (DWORD attempt = 0; attempt < maximum_attempts; ++attempt) {
        if (MoveFileExW(
                source.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        const auto native_error = GetLastError();
        const bool transient = native_error == ERROR_ACCESS_DENIED
            || native_error == ERROR_SHARING_VIOLATION
            || native_error == ERROR_LOCK_VIOLATION;
        if (!transient || attempt + 1 == maximum_attempts) {
            error = std::error_code{
                static_cast<int>(native_error), std::system_category()};
            return false;
        }
        Sleep(5);
    }
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
    auto encoded = read_all(path, error, kMaximumCacheEntryBytes);
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
    // Successful reads refresh LRU recency. Failure to update metadata must
    // never turn a valid cache hit into a compilation failure.
    std::error_code ignored;
    std::filesystem::last_write_time(
        path, std::filesystem::file_time_type::clock::now(), ignored);
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
        // Concurrent first-time publishers can race while creating the same
        // shard on Windows. Some filesystem implementations retain an
        // already-exists error even though another publisher completed the
        // directory. Accept only the exact postcondition we require.
        std::error_code status_error;
        if (!std::filesystem::is_directory(path.parent_path(), status_error)) {
            return false;
        }
        error.clear();
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

bool ObjectCache::prune(
    const ObjectCachePruneOptions& options,
    ObjectCachePruneResult& result,
    std::error_code& error) const {
    result = {};
    error.clear();
    if ((options.maximum_age
         && *options.maximum_age < std::chrono::seconds::zero())
        || options.temporary_file_grace < std::chrono::seconds::zero()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (!std::filesystem::exists(root_, error)) {
        if (!error) {
            return true;
        }
        return false;
    }
    if (!std::filesystem::is_directory(root_, error)) {
        if (!error) {
            error = std::make_error_code(std::errc::not_a_directory);
        }
        return false;
    }

    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type stamp;
        std::uintmax_t size{};
        bool removed{};
        bool attempted{};
    };
    std::vector<Entry> entries;
    std::vector<std::filesystem::path> shards;

    std::filesystem::directory_iterator shard_iterator{
        root_, std::filesystem::directory_options::skip_permission_denied, error};
    if (error) {
        return false;
    }
    const std::filesystem::directory_iterator end;
    for (; shard_iterator != end; shard_iterator.increment(error)) {
        if (error) {
            return false;
        }
        std::error_code status_error;
        const auto status = shard_iterator->symlink_status(status_error);
        if (status_error || !std::filesystem::is_directory(status)
            || std::filesystem::is_symlink(status)) {
            continue;
        }
        const auto shard = shard_iterator->path();
        const auto shard_name = shard.filename().string();
        if (shard_name.size() != 2
            || !std::all_of(
                shard_name.begin(), shard_name.end(), [](const char value) {
                    return (value >= '0' && value <= '9')
                        || (value >= 'a' && value <= 'f');
                })) {
            continue;
        }
        shards.push_back(shard);

        std::filesystem::directory_iterator file_iterator{
            shard, std::filesystem::directory_options::skip_permission_denied, error};
        if (error) {
            return false;
        }
        for (; file_iterator != end; file_iterator.increment(error)) {
            if (error) {
                return false;
            }
            const auto path = file_iterator->path();
            const auto file_status = file_iterator->symlink_status(status_error);
            if (status_error || !std::filesystem::is_regular_file(file_status)) {
                continue;
            }
            std::string key;
            if (cache_entry_filename(path, key)) {
                const auto size = file_iterator->file_size(status_error);
                if (status_error) {
                    ++result.failed_removals;
                    continue;
                }
                const auto stamp = file_iterator->last_write_time(status_error);
                if (status_error) {
                    ++result.failed_removals;
                    continue;
                }
                entries.push_back({path, stamp, size, false, false});
                ++result.scanned_entries;
                add_saturating(result.bytes_before, size);
                continue;
            }
            if (!options.remove_stale_temporary_files
                || !cache_temporary_filename(path, key)) {
                continue;
            }
            const auto stamp = file_iterator->last_write_time(status_error);
            if (status_error
                || !elapsed_at_least(stamp, options.temporary_file_grace)) {
                if (status_error) {
                    ++result.failed_removals;
                }
                continue;
            }
            const auto destination = path_for(key);
            std::error_code lock_error;
            detail::CacheDirectoryLock lock{
                destination.string() + ".lock",
                lock_error,
                std::chrono::milliseconds{0}};
            if (!lock.held()) {
                if (lock_error == std::errc::timed_out) {
                    ++result.skipped_locked_entries;
                } else {
                    ++result.failed_removals;
                }
                continue;
            }
            std::error_code remove_error;
            if (std::filesystem::remove(path, remove_error)) {
                ++result.removed_temporary_files;
            } else if (remove_error) {
                ++result.failed_removals;
            }
        }
        if (error) {
            return false;
        }
    }
    if (error) {
        return false;
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.stamp != right.stamp) {
            return left.stamp < right.stamp;
        }
        return left.path.generic_string() < right.path.generic_string();
    });

    std::uint64_t remaining_entries = result.scanned_entries;
    std::uintmax_t remaining_bytes = result.bytes_before;
    const auto remove_entry = [&](Entry& entry) {
        entry.attempted = true;
        std::error_code lock_error;
        detail::CacheDirectoryLock lock{
            entry.path.string() + ".lock",
            lock_error,
            std::chrono::milliseconds{0}};
        if (!lock.held()) {
            if (lock_error == std::errc::timed_out) {
                ++result.skipped_locked_entries;
            } else {
                ++result.failed_removals;
            }
            return false;
        }
        std::error_code size_error;
        const auto current_size =
            std::filesystem::file_size(entry.path, size_error);
        if (size_error == std::errc::no_such_file_or_directory) {
            entry.removed = true;
            if (remaining_entries != 0) {
                --remaining_entries;
            }
            if (entry.size <= remaining_bytes) {
                remaining_bytes -= entry.size;
            } else {
                remaining_bytes = 0;
            }
            return true;
        }
        if (size_error) {
            ++result.failed_removals;
            return false;
        }
        std::error_code remove_error;
        if (!std::filesystem::remove(entry.path, remove_error)) {
            if (remove_error) {
                ++result.failed_removals;
            }
            return false;
        }
        entry.removed = true;
        ++result.removed_entries;
        add_saturating(result.bytes_removed, current_size);
        if (remaining_entries != 0) {
            --remaining_entries;
        }
        if (current_size <= remaining_bytes) {
            remaining_bytes -= current_size;
        } else {
            remaining_bytes = 0;
        }
        return true;
    };

    if (options.maximum_age) {
        for (auto& entry : entries) {
            if (elapsed_at_least(entry.stamp, *options.maximum_age)) {
                (void)remove_entry(entry);
            }
        }
    }
    const auto over_limit = [&] {
        return (options.maximum_entries
                && remaining_entries > *options.maximum_entries)
            || (options.maximum_bytes
                && remaining_bytes > *options.maximum_bytes);
    };
    for (auto& entry : entries) {
        if (!over_limit()) {
            break;
        }
        if (!entry.removed && !entry.attempted) {
            (void)remove_entry(entry);
        }
    }

    result.remaining_entries = remaining_entries;
    result.remaining_bytes = remaining_bytes;
    for (const auto& shard : shards) {
        std::error_code remove_error;
        (void)std::filesystem::remove(shard, remove_error);
    }
    error.clear();
    return true;
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
