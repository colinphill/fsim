// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"
#include "fsim/compiler/cache_support.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/payload_checksum.hpp"
#include "fsim/support/sqlite_database.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <utility>
#include <limits>
#include <mutex>
#include <new>
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

constexpr std::uintmax_t kMaximumCacheEntryBytes =
    256U * 1024U * 1024U;
// The database's application id is fsim's ("FSIM"); user_version is the
// schema of the entries table. A database of another schema is rebuilt.
constexpr std::int64_t kApplicationId = 0x4653494D;
constexpr std::int64_t kCacheSchema = 1;

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
    const auto size = support::native_fs::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    if (size > maximum_size
        || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())
        || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        error = std::make_error_code(std::errc::file_too_large);
        return std::nullopt;
    }

    auto stream = support::native_fs::open_ifstream(path, std::ios::binary);
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

[[nodiscard]] std::int64_t now_seconds() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
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

struct LockOwner {
    std::uint64_t process_id{};
    std::string token;
};

[[nodiscard]] std::optional<LockOwner> read_owner(
    const std::filesystem::path& directory) {
    auto stream = support::native_fs::open_ifstream(directory / "owner", std::ios::binary);
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
        support::path_for_native_io(marker).c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    const auto stamp = support::native_fs::last_write_time(path, error);
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
    support::native_fs::rename(path, quarantine, error);
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
        support::native_fs::remove_all(quarantine, ignored);
        return true;
    }

    // Ownership changed while recovery was being attempted. Restore the
    // directory if possible and conservatively leave it alone otherwise.
    support::native_fs::rename(quarantine, path, ignored);
    return false;
}

[[nodiscard]] std::string new_lock_token() {
    const auto serial = lock_counter.fetch_add(1, std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(current_process_id()) + "-" + std::to_string(now) + "-"
        + std::to_string(serial);
}

} // namespace

bool process_is_alive(const std::uint64_t process_id) noexcept {
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
        if (support::native_fs::create_directory(path_, error)) {
            const LockOwner owner{current_process_id(), token_};
            // Register before publishing the owner record. A same-process
            // contender that observes the record must never reclaim a lock
            // that is still active in this process.
            try {
                register_active_lock(token_);
            } catch (const std::bad_alloc&) {
                error = std::make_error_code(std::errc::not_enough_memory);
                std::error_code ignored;
                support::native_fs::remove_all(path_, ignored);
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
                support::native_fs::remove_all(path_, ignored);
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
    support::native_fs::remove(path_ / "owner", ignored);
    support::native_fs::remove(path_, ignored);
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
                support::path_for_native_io(source).c_str(),
                support::path_for_native_io(destination).c_str(),
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

struct ObjectCache::Connection {
    std::filesystem::path path;
    std::mutex mutex;
    support::sqlite::Database database;
    bool open{};
};

namespace {

// Ends a transaction that was not committed.
class Transaction final {
public:
    Transaction(support::sqlite::Database& database, const char* begin, std::string& error)
        : database_(database), active_(database.execute(begin, error)) {}
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction() {
        if (active_) {
            std::string ignored;
            static_cast<void>(database_.execute("ROLLBACK", ignored));
        }
    }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool commit(std::string& error) {
        active_ = false;
        return database_.execute("COMMIT", error);
    }

private:
    support::sqlite::Database& database_;
    bool active_{};
};

[[nodiscard]] bool create_schema(support::sqlite::Database& database, std::string& error) {
    Transaction transaction{database, "BEGIN IMMEDIATE", error};
    if (!transaction.active()) {
        return false;
    }
    // Another process may have created it since this one looked.
    const auto id = database.pragma_integer("PRAGMA application_id", error);
    const auto schema = database.pragma_integer("PRAGMA user_version", error);
    if (!id || !schema) {
        return false;
    }
    if (*id == kApplicationId && *schema == kCacheSchema) {
        return transaction.commit(error);
    }
    if (*id != 0 && *id != kApplicationId) {
        error = database.context() + ": the file is not an fsim cache";
        return false;
    }
    const auto statements = std::string{
        "DROP TABLE IF EXISTS entries;"
        "CREATE TABLE entries(key TEXT PRIMARY KEY NOT NULL, checksum TEXT NOT NULL,"
        " size INTEGER NOT NULL, last_used INTEGER NOT NULL, data BLOB NOT NULL);"
        "CREATE INDEX entries_by_use ON entries(last_used);"
        "PRAGMA application_id = "} + std::to_string(kApplicationId)
        + ";PRAGMA user_version = " + std::to_string(kCacheSchema) + ";";
    return database.execute(statements.c_str(), error) && transaction.commit(error);
}

} // namespace

bool ObjectCache::open_connection(const bool create, std::error_code& error) const {
    auto& connection = *connection_;
    if (connection.open) {
        return true;
    }
    std::error_code status;
    if (!create && !support::native_fs::exists(connection.path, status)) {
        error = status ? status : std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }
    if (create && !connection.path.parent_path().empty()) {
        support::native_fs::create_directories(connection.path.parent_path(), status);
        // Racing first-time creators may report an existing directory.
        if (status && !support::native_fs::is_directory(connection.path.parent_path(), status)) {
            error = status ? status : std::make_error_code(std::errc::not_a_directory);
            return false;
        }
    }
    support::sqlite::Database database{
        "object cache " + support::path_to_utf8(connection.path)};
    support::sqlite::Database::Limits limits;
    // A row is its payload plus a key, checksum and two integers.
    limits.value_bytes = static_cast<std::int64_t>(kMaximumCacheEntryBytes) + 4096;
    std::string message;
    const auto mode = create ? support::sqlite::Database::OpenMode::read_write_create
                             : support::sqlite::Database::OpenMode::read_write_existing;
    if (!database.open(connection.path, mode, limits, message)) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    const auto id = database.pragma_integer("PRAGMA application_id", message);
    const auto schema = database.pragma_integer("PRAGMA user_version", message);
    if (!id || !schema) {
        error = std::make_error_code(std::errc::illegal_byte_sequence);
        return false;
    }
    if (*id != kApplicationId || *schema != kCacheSchema) {
        // A new file: incremental vacuum lets prune give space back. It
        // only takes effect before the first table exists.
        if (*id == 0 && *schema == 0
            && !database.execute("PRAGMA auto_vacuum = INCREMENTAL", message)) {
            error = std::make_error_code(std::errc::io_error);
            return false;
        }
        if (!create_schema(database, message)) {
            error = std::make_error_code(*id != 0 && *id != kApplicationId
                    ? std::errc::illegal_byte_sequence
                    : std::errc::io_error);
            return false;
        }
    }
    // Losing the newest entries to a power failure only costs a rebuild.
    if (!database.execute("PRAGMA synchronous = OFF", message)) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    connection.database = std::move(database);
    connection.open = true;
    return true;
}

ObjectCache::ObjectCache(std::filesystem::path database)
    : connection_(std::make_shared<Connection>()) {
    connection_->path = std::move(database);
}

const std::filesystem::path& ObjectCache::path() const noexcept {
    return connection_->path;
}

std::optional<std::vector<std::byte>> ObjectCache::load(
    const std::string_view key, std::error_code& error) const {
    auto payload = load_payload(key, error);
    if (!payload) {
        return std::nullopt;
    }
    const auto bytes = payload->bytes();
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

std::optional<ObjectCachePayload> ObjectCache::load_payload(
    const std::string_view key, std::error_code& error) const {
    error.clear();
    if (!valid_cache_key(key)) {
        error = std::make_error_code(std::errc::invalid_argument);
        return std::nullopt;
    }
    const std::scoped_lock lock{connection_->mutex};
    if (!open_connection(false, error)) {
        return std::nullopt;
    }
    auto& database = connection_->database;
    std::string message;
    std::unique_ptr<char[]> buffer;
    std::size_t size{};
    std::string checksum;
    {
        // One read transaction, so the row and its blob are one version.
        Transaction transaction{database, "BEGIN", message};
        if (!transaction.active()) {
            error = std::make_error_code(std::errc::io_error);
            return std::nullopt;
        }
        std::int64_t row{};
        {
            support::sqlite::Statement query{database,
                "SELECT rowid, checksum, size FROM entries WHERE key = ?1", message};
            if (!query || !query.bind_text(1, key, message)) {
                error = std::make_error_code(std::errc::io_error);
                return std::nullopt;
            }
            const auto step = query.step(message);
            if (step != support::sqlite::Step::row) {
                error = std::make_error_code(step == support::sqlite::Step::done
                        ? std::errc::no_such_file_or_directory
                        : std::errc::io_error);
                return std::nullopt;
            }
            const auto stored_row = query.integer(0, message);
            const auto stored_checksum = query.text(1, message);
            const auto stored_size = query.integer(2, message);
            if (!stored_row || !stored_checksum || !stored_size || *stored_size < 0) {
                error = std::make_error_code(std::errc::illegal_byte_sequence);
                return std::nullopt;
            }
            // The entry ceiling is checked before allocation.
            if (static_cast<std::uint64_t>(*stored_size) > kMaximumCacheEntryBytes) {
                error = std::make_error_code(std::errc::file_too_large);
                return std::nullopt;
            }
            row = *stored_row;
            checksum = *stored_checksum;
            size = static_cast<std::size_t>(*stored_size);
        }
        buffer = std::make_unique_for_overwrite<char[]>(size);
        if (!database.read_blob("entries", "data", row, {buffer.get(), size}, message)) {
            error = std::make_error_code(std::errc::illegal_byte_sequence);
            return std::nullopt;
        }
        if (!transaction.commit(message)) {
            error = std::make_error_code(std::errc::io_error);
            return std::nullopt;
        }
    }
    // Entries are only ever written with an xxh64 checksum.
    if (!checksum.starts_with("xxh64:") || !support::payload_checksum_spelling(checksum)
        || !support::payload_checksum_matches({buffer.get(), size}, checksum)) {
        error = std::make_error_code(std::errc::illegal_byte_sequence);
        return std::nullopt;
    }
    // A hit refreshes LRU recency. Failing to record it (a read-only
    // cache) never turns a valid hit into a failure.
    support::sqlite::Statement touch{database,
        "UPDATE entries SET last_used = ?2 WHERE key = ?1 AND last_used < ?2", message};
    if (touch && touch.bind_text(1, key, message)
        && touch.bind_integer(2, now_seconds(), message)) {
        static_cast<void>(touch.finish(message));
    }
    return ObjectCachePayload{std::move(buffer), size};
}

std::shared_ptr<const std::string_view> ObjectCachePayload::share() && {
    struct Owner {
        std::unique_ptr<char[]> storage;
        std::string_view view;
    };
    auto owner = std::make_shared<Owner>();
    owner->view = text();
    owner->storage = std::move(storage_);
    return std::shared_ptr<const std::string_view>{owner, &owner->view};
}

bool ObjectCache::store(
    const std::string_view key,
    const std::span<const std::byte> payload,
    std::error_code& error) const {
    error.clear();
    if (!valid_cache_key(key)) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (!object_cache_payload_fits(payload.size())) {
        error = std::make_error_code(std::errc::file_too_large);
        return false;
    }
    const std::scoped_lock lock{connection_->mutex};
    if (!open_connection(true, error)) {
        return false;
    }
    // One statement, so a concurrent reader sees the old entry or the new.
    std::string message;
    const std::string_view bytes{
        reinterpret_cast<const char*>(payload.data()), payload.size()};
    support::sqlite::Statement upsert{connection_->database,
        "INSERT INTO entries(key, checksum, size, last_used, data)"
        " VALUES(?1, ?2, ?3, ?4, ?5) ON CONFLICT(key) DO UPDATE SET"
        " checksum = excluded.checksum, size = excluded.size,"
        " last_used = excluded.last_used, data = excluded.data",
        message};
    if (!upsert || !upsert.bind_text(1, key, message)
        || !upsert.bind_text(2, support::payload_checksum(bytes), message)
        || !upsert.bind_integer(3, static_cast<std::int64_t>(payload.size()), message)
        || !upsert.bind_integer(4, now_seconds(), message)
        || !upsert.bind_blob(5, bytes, message) || !upsert.finish(message)) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    return true;
}

std::uintmax_t maximum_object_cache_payload_bytes() noexcept
{
    return kMaximumCacheEntryBytes;
}

bool object_cache_payload_fits(const std::uintmax_t payload_bytes) noexcept
{
    return payload_bytes <= maximum_object_cache_payload_bytes();
}

bool ObjectCache::erase(const std::string_view key, std::error_code& error) const {
    error.clear();
    if (!valid_cache_key(key)) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    const std::scoped_lock lock{connection_->mutex};
    if (!open_connection(false, error)) {
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
        }
        return false;
    }
    std::string message;
    support::sqlite::Statement remove{
        connection_->database, "DELETE FROM entries WHERE key = ?1", message};
    if (!remove || !remove.bind_text(1, key, message) || !remove.finish(message)) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    return connection_->database.changes() != 0;
}

bool ObjectCache::prune(
    const ObjectCachePruneOptions& options,
    ObjectCachePruneResult& result,
    std::error_code& error) const {
    result = {};
    error.clear();
    if (options.maximum_age && *options.maximum_age < std::chrono::seconds::zero()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    const std::scoped_lock lock{connection_->mutex};
    if (!open_connection(false, error)) {
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            return true;
        }
        return false;
    }
    auto& database = connection_->database;
    std::string message;
    const auto failed = [&] {
        error = std::make_error_code(std::errc::io_error);
        return false;
    };
    {
        Transaction transaction{database, "BEGIN IMMEDIATE", message};
        if (!transaction.active()) {
            return failed();
        }
        struct Entry {
            std::string key;
            std::uint64_t size{};
            std::int64_t last_used{};
        };
        std::vector<Entry> entries;
        {
            support::sqlite::Statement query{database,
                "SELECT key, size, last_used FROM entries ORDER BY last_used, key", message};
            if (!query) {
                return failed();
            }
            while (true) {
                const auto step = query.step(message);
                if (step == support::sqlite::Step::done) {
                    break;
                }
                const auto key = step == support::sqlite::Step::row
                    ? query.text(0, message) : std::nullopt;
                const auto size = key ? query.integer(1, message) : std::nullopt;
                const auto last_used = size ? query.integer(2, message) : std::nullopt;
                if (!last_used || *size < 0) {
                    return failed();
                }
                entries.push_back({std::string{*key},
                    static_cast<std::uint64_t>(*size), *last_used});
                ++result.scanned_entries;
                result.bytes_before += static_cast<std::uint64_t>(*size);
            }
        }
        result.remaining_entries = result.scanned_entries;
        result.remaining_bytes = result.bytes_before;
        support::sqlite::Statement remove{
            database, "DELETE FROM entries WHERE key = ?1", message};
        if (!remove) {
            return failed();
        }
        const auto remove_entry = [&](const Entry& entry) {
            if (!remove.reset(message) || !remove.bind_text(1, entry.key, message)
                || !remove.finish(message)) {
                return false;
            }
            ++result.removed_entries;
            result.bytes_removed += entry.size;
            --result.remaining_entries;
            result.remaining_bytes -= entry.size;
            return true;
        };
        const auto over_limit = [&] {
            return (options.maximum_entries
                       && result.remaining_entries > *options.maximum_entries)
                || (options.maximum_bytes
                    && result.remaining_bytes > *options.maximum_bytes);
        };
        const auto cutoff = now_seconds()
            - (options.maximum_age ? options.maximum_age->count() : 0);
        // Oldest first: aged entries, then whatever the limits still need.
        for (const auto& entry : entries) {
            if (!(options.maximum_age && entry.last_used <= cutoff) && !over_limit()) {
                break;
            }
            if (!remove_entry(entry)) {
                return failed();
            }
        }
        if (!transaction.commit(message)) {
            return failed();
        }
    }
    // Free pages go back to the file system; failing to do so loses nothing.
    if (result.removed_entries != 0) {
        static_cast<void>(database.execute("PRAGMA incremental_vacuum", message));
    }
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
