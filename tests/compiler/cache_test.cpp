// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"
#include "fsim/compiler/cache_support.hpp"
#include "fsim/support/sqlite_database.hpp"
#include "fsim/support/sha256.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

// Runs SQL against a cache database; ?1 binds `key` when it is not empty.
void execute(
    const std::filesystem::path& database,
    const std::string& sql,
    const std::string_view key,
    const bool create = false) {
    fsim::support::sqlite::Database connection{"cache test"};
    std::string error;
    const bool opened = connection.open(database,
        create ? fsim::support::sqlite::Database::OpenMode::read_write_create
               : fsim::support::sqlite::Database::OpenMode::read_write_existing,
        {}, error);
    assert(opened);
    if (key.empty()) {
        const bool executed = connection.execute(sql.c_str(), error);
        assert(executed);
        return;
    }
    fsim::support::sqlite::Statement statement{connection, sql.c_str(), error};
    const bool done = statement && statement.bind_text(1, key, error)
        && statement.finish(error);
    assert(done);
    assert(connection.changes() == 1);
}

void age_entry(
    const std::filesystem::path& database,
    const std::string_view key,
    const std::chrono::seconds age) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    execute(database,
        "UPDATE entries SET last_used = " + std::to_string((now - age).count())
            + " WHERE key = ?1",
        key);
}

} // namespace

int main() {
    using fsim::compiler::CacheKeyBuilder;
    using fsim::compiler::ObjectCache;
    using fsim::support::Sha256;

    assert(
        Sha256::hex(Sha256::digest("abc"))
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string million_a(1'000'000U, 'a');
    assert(
        Sha256::hex(Sha256::digest(million_a))
        == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    Sha256 chunked;
    for (std::size_t offset = 0; offset < million_a.size(); offset += 137U) {
        chunked.update(std::string_view { million_a }.substr(offset, 137U));
    }
    assert(
        Sha256::hex(chunked.finish())
        == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    CacheKeyBuilder first;
    const auto key = first.add("source", "module m; endmodule").add("llvm", "22.1.8").finish();
    CacheKeyBuilder second;
    assert(
        key
        == second.add("source", "module m; endmodule").add("llvm", "22.1.8").finish());

    CacheKeyBuilder debug_native;
    const auto debug_native_key = debug_native
                                      .add("engine", "llvm")
                                      .add("optimization", "O0")
                                      .add("build-configuration", "Debug")
                                      .finish();
    CacheKeyBuilder release_native;
    const auto release_native_key = release_native
                                        .add("engine", "llvm")
                                        .add("optimization", "O0")
                                        .add("build-configuration", "Release")
                                        .finish();
    CacheKeyBuilder optimized_native;
    const auto optimized_native_key = optimized_native
                                          .add("engine", "llvm")
                                          .add("optimization", "O2")
                                          .add("build-configuration", "Debug")
                                          .finish();
    CacheKeyBuilder interpreted;
    const auto interpreter_key = interpreted
                                     .add("engine", "interpreter")
                                     .add("optimization", "O0")
                                     .add("build-configuration", "Debug")
                                     .finish();
    assert(debug_native_key != release_native_key);
    assert(debug_native_key != optimized_native_key);
    assert(debug_native_key != interpreter_key);

    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto build_hash = std::hash<std::string>{}(
        std::filesystem::current_path().generic_string());
    const auto root =
        std::filesystem::temp_directory_path()
        / ("fsim-cache-test-" + key.substr(0, 12) + "-"
           + std::to_string(build_hash) + "-" + std::to_string(nonce));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto database = root / "objects.sqlite3";
    ObjectCache cache{database};
    const std::array payload{
        std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};

    // A load from an absent database is a miss and creates nothing.
    assert(!cache.load(key, error));
    assert(error == std::errc::no_such_file_or_directory);
    assert(!std::filesystem::exists(database));
    std::filesystem::create_directories(root, error);
    assert(!error);

    // A process that died before publishing lock ownership must not wedge a
    // directory lock forever. Marker-less locks are only reclaimed after a
    // grace age.
    const auto stale_lock = root / "stale.lock";
    std::filesystem::create_directories(stale_lock, error);
    assert(!error);
    std::filesystem::last_write_time(
        stale_lock,
        std::filesystem::file_time_type::clock::now() - std::chrono::hours{1},
        error);
    assert(!error);
    {
        std::error_code lock_error;
        fsim::compiler::detail::CacheDirectoryLock reclaimed{
            stale_lock, lock_error, std::chrono::milliseconds{200}};
        assert(reclaimed.held());
        assert(!lock_error);
    }
    assert(!std::filesystem::exists(stale_lock));

    // Age alone never permits stealing a lock whose recorded local process is
    // still alive.
    const auto live_lock_path = root / "live.lock";
    {
        std::error_code owner_error;
        fsim::compiler::detail::CacheDirectoryLock owner{
            live_lock_path, owner_error, std::chrono::milliseconds{20}};
        assert(owner.held());
        assert(!owner_error);
        std::filesystem::last_write_time(
            live_lock_path,
            std::filesystem::file_time_type::clock::now() - std::chrono::hours{1},
            owner_error);
        assert(!owner_error);
        std::error_code contender_error;
        fsim::compiler::detail::CacheDirectoryLock contender{
            live_lock_path,
            contender_error,
            std::chrono::milliseconds{20},
            std::chrono::seconds{0}};
        assert(!contender.held());
        assert(contender_error == std::errc::timed_out);
    }
    assert(!std::filesystem::exists(live_lock_path));

#if defined(_WIN32)
    // A Windows scanner can retain a non-delete-sharing handle after the lock
    // owner has finished. The stale owner record must not make a later lock
    // in this same process wait forever.
    const auto stranded_lock_path = root / "stranded.lock";
    HANDLE retained_owner = INVALID_HANDLE_VALUE;
    {
        std::error_code owner_error;
        fsim::compiler::detail::CacheDirectoryLock owner{
            stranded_lock_path, owner_error, std::chrono::milliseconds{20}};
        assert(owner.held());
        assert(!owner_error);
        retained_owner = CreateFileW(
            (stranded_lock_path / "owner").c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(retained_owner != INVALID_HANDLE_VALUE);
    }
    assert(std::filesystem::exists(stranded_lock_path / "owner"));
    assert(CloseHandle(retained_owner));
    {
        std::error_code contender_error;
        fsim::compiler::detail::CacheDirectoryLock contender{
            stranded_lock_path, contender_error,
            std::chrono::milliseconds{200}};
        assert(contender.held());
        assert(!contender_error);
    }
    assert(!std::filesystem::exists(stranded_lock_path));
#endif

    assert(cache.store(key, payload, error));
    assert(!error);
    const auto loaded = cache.load(key, error);
    assert(loaded);
    assert(!error);
    assert(std::equal(loaded->begin(), loaded->end(), payload.begin(), payload.end()));

    // Storing again replaces the entry.
    const std::array replacement{
        std::byte{0xca}, std::byte{0xfe}, std::byte{0xba}, std::byte{0xbe}};
    assert(cache.store(key, replacement, error));
    const auto replaced = cache.load(key, error);
    assert(replaced);
    assert(std::equal(
        replaced->begin(), replaced->end(), replacement.begin(), replacement.end()));

    constexpr std::uintmax_t maximum_cache_entry_bytes =
        256U * 1024U * 1024U;
    const auto maximum_payload
        = fsim::compiler::maximum_object_cache_payload_bytes();
    assert(maximum_payload == maximum_cache_entry_bytes);
    assert(fsim::compiler::object_cache_payload_fits(maximum_payload));
    assert(!fsim::compiler::object_cache_payload_fits(
        maximum_payload + 1U));

    // Corrupt and truncated entries are isolated as cache misses with a stable
    // data-integrity error. A subsequent publisher can replace either entry.
    execute(database, "UPDATE entries SET data = x'cafeba00' WHERE key = ?1", key);
    assert(!cache.load(key, error));
    assert(error == std::errc::illegal_byte_sequence);
    assert(cache.store(key, replacement, error));
    execute(database, "UPDATE entries SET data = x'cafe' WHERE key = ?1", key);
    assert(!cache.load(key, error));
    assert(error == std::errc::illegal_byte_sequence);
    assert(cache.store(key, replacement, error));
    execute(database, "UPDATE entries SET checksum = 'xxh64:0' WHERE key = ?1", key);
    assert(!cache.load(key, error));
    assert(error == std::errc::illegal_byte_sequence);
    assert(cache.store(key, replacement, error));

    // Entries have a governed size ceiling, checked before allocation.
    execute(database,
        "UPDATE entries SET size = " + std::to_string(maximum_cache_entry_bytes + 1U)
            + " WHERE key = ?1",
        key);
    assert(!cache.load(key, error));
    assert(error == std::errc::file_too_large);
    assert(cache.store(key, replacement, error));

    // Valid cache hits require no write access. Restore owner write permission
    // before exercising replacement and cleanup on both POSIX and Windows.
    std::filesystem::permissions(
        database,
        std::filesystem::perms::owner_write
            | std::filesystem::perms::group_write
            | std::filesystem::perms::others_write,
        std::filesystem::perm_options::remove,
        error);
    assert(!error);
    {
        const ObjectCache read_only_cache{database};
        const auto read_only = read_only_cache.load(key, error);
        assert(read_only);
        assert(!error);
        assert(std::equal(
            read_only->begin(), read_only->end(), replacement.begin(), replacement.end()));
    }
    std::filesystem::permissions(
        database,
        std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add,
        error);
    assert(!error);

    // Concurrent writers, through one cache and through separate connections
    // to the same file, serialize in SQLite and leave a complete entry and
    // no side files behind.
    std::vector<std::future<std::pair<bool, std::error_code>>> publishers;
    for (int writer = 0; writer < 8; ++writer) {
        publishers.emplace_back(std::async(
            std::launch::async,
            [&cache, &database, &key, &replacement, writer] {
                std::error_code publisher_error;
                const ObjectCache separate{database};
                const auto& target = writer % 2 == 0 ? cache : separate;
                const auto stored = target.store(key, replacement, publisher_error);
                return std::make_pair(stored, publisher_error);
            }));
    }
    for (auto& publisher : publishers) {
        const auto [stored, publisher_error] = publisher.get();
        assert(stored);
        assert(!publisher_error);
    }
    const auto concurrently_published = cache.load(key, error);
    assert(concurrently_published);
    assert(std::equal(
        concurrently_published->begin(),
        concurrently_published->end(),
        replacement.begin(),
        replacement.end()));
    for (const auto& entry : std::filesystem::directory_iterator{root}) {
        assert(entry.path().filename() == "objects.sqlite3");
    }

    // Canonical keys are lowercase hexadecimal; case variants cannot alias
    // the same cache entry.
    auto uppercase_key = key;
    uppercase_key.front() = static_cast<char>(
        uppercase_key.front() >= 'a' && uppercase_key.front() <= 'f'
            ? uppercase_key.front() - ('a' - 'A')
            : 'G');
    assert(!cache.load(uppercase_key, error));
    assert(error == std::errc::invalid_argument);
    assert(!cache.store(uppercase_key, payload, error));
    assert(error == std::errc::invalid_argument);

    assert(cache.erase(key, error));
    assert(!cache.erase(key, error));
    assert(!error);
    assert(!cache.load(key, error));
    assert(error == std::errc::no_such_file_or_directory);

    const auto make_key = [](const std::string_view identity) {
        CacheKeyBuilder builder;
        return builder.add("identity", identity).finish();
    };
    const auto eviction_database = root / "eviction.sqlite3";
    ObjectCache eviction_cache{eviction_database};
    const auto key_a = make_key("entry-a");
    const auto key_b = make_key("entry-b");
    const auto key_c = make_key("entry-c");
    const auto key_d = make_key("entry-d");
    assert(eviction_cache.store(key_a, payload, error));
    assert(eviction_cache.store(key_b, payload, error));
    assert(eviction_cache.store(key_c, payload, error));
    assert(eviction_cache.store(key_d, replacement, error));
    age_entry(eviction_database, key_a, std::chrono::hours{4});
    age_entry(eviction_database, key_b, std::chrono::hours{3});
    age_entry(eviction_database, key_c, std::chrono::hours{2});
    age_entry(eviction_database, key_d, std::chrono::hours{1});
    const auto present = [&](const std::string& entry) {
        std::error_code ignored;
        return eviction_cache.load(entry, ignored).has_value();
    };

    // A successful hit refreshes LRU recency, so count pruning preserves A and
    // D while evicting the older untouched B and C entries.
    assert(eviction_cache.load(key_a, error));
    fsim::compiler::ObjectCachePruneResult prune;
    fsim::compiler::ObjectCachePruneOptions count_limit;
    count_limit.maximum_entries = 2;
    assert(eviction_cache.prune(count_limit, prune, error));
    assert(!error);
    assert(prune.scanned_entries == 4);
    assert(prune.removed_entries == 2);
    assert(prune.remaining_entries == 2);
    assert(!present(key_b));
    assert(!present(key_c));
    assert(present(key_a));
    assert(present(key_d));

    // A byte limit removes oldest entries until it is met.
    fsim::compiler::ObjectCachePruneOptions byte_limit;
    byte_limit.maximum_bytes = payload.size() - 1U;
    assert(eviction_cache.prune(byte_limit, prune, error));
    assert(prune.bytes_before == 2U * payload.size());
    assert(prune.removed_entries == 2);
    assert(prune.remaining_entries == 0);
    assert(prune.remaining_bytes == 0);
    assert(prune.bytes_removed == prune.bytes_before);

    const auto key_e = make_key("entry-e");
    const auto key_f = make_key("entry-f");
    assert(eviction_cache.store(key_e, payload, error));
    assert(eviction_cache.store(key_f, payload, error));
    age_entry(eviction_database, key_e, std::chrono::hours{2});
    fsim::compiler::ObjectCachePruneOptions age_limit;
    age_limit.maximum_age = std::chrono::hours{1};
    assert(eviction_cache.prune(age_limit, prune, error));
    assert(prune.removed_entries == 1);
    assert(!present(key_e));
    assert(present(key_f));

    fsim::compiler::ObjectCachePruneOptions remove_all;
    remove_all.maximum_entries = 0;
    assert(eviction_cache.prune(remove_all, prune, error));
    assert(prune.removed_entries == 1);
    assert(prune.remaining_entries == 0);

    // Pruning gives the freed pages back to the file system.
    const std::vector<std::byte> large(8U * 1024U * 1024U, std::byte{0x5a});
    assert(eviction_cache.store(make_key("entry-large"), large, error));
    const auto stored_size = std::filesystem::file_size(eviction_database, error);
    assert(!error && stored_size > large.size());
    assert(eviction_cache.prune(remove_all, prune, error));
    assert(prune.removed_entries == 1);
    assert(std::filesystem::file_size(eviction_database, error) < large.size() / 4U);

    fsim::compiler::ObjectCachePruneOptions invalid;
    invalid.maximum_age = std::chrono::seconds{-1};
    assert(!eviction_cache.prune(invalid, prune, error));
    assert(error == std::errc::invalid_argument);

    const auto absent_database = root / "absent.sqlite3";
    ObjectCache absent{absent_database};
    fsim::compiler::ObjectCachePruneOptions no_limits;
    assert(absent.prune(no_limits, prune, error));
    assert(!error);
    assert(prune == fsim::compiler::ObjectCachePruneResult{});
    assert(!std::filesystem::exists(absent_database));

    // A file that is not an fsim cache is never written to.
    const auto foreign_text = root / "foreign.txt";
    {
        std::ofstream foreign(foreign_text, std::ios::binary);
        foreign << "not a database";
    }
    assert(!ObjectCache{foreign_text}.store(key, payload, error));
    assert(error);
    {
        std::ifstream foreign(foreign_text, std::ios::binary);
        std::string text;
        std::getline(foreign, text);
        assert(text == "not a database");
    }
    const auto foreign_database = root / "foreign.sqlite3";
    execute(foreign_database,
        "PRAGMA application_id = 7; CREATE TABLE kept(value INTEGER)", { }, true);
    assert(!ObjectCache{foreign_database}.store(key, payload, error));
    assert(error == std::errc::illegal_byte_sequence);
    execute(foreign_database, "SELECT value FROM kept", { });

    // A cache of another schema version is rebuilt.
    const auto older_database = root / "older.sqlite3";
    execute(older_database,
        "PRAGMA application_id = 1179863373; PRAGMA user_version = 99;"
        " CREATE TABLE entries(key TEXT)",
        { }, true);
    {
        const ObjectCache older{older_database};
        assert(older.store(key, payload, error));
        const auto rebuilt = older.load(key, error);
        assert(rebuilt && !error);
    }

    // A v268 namespace entry cannot satisfy the current native-object key.
    CacheKeyBuilder legacy_native;
    const auto legacy_native_key = legacy_native
        .add("llvm-object-schema", "fsim-llvm-native-object-v268")
        .add("process-identity", "legacy-namespace-fixture")
        .finish();
    CacheKeyBuilder current_native;
    const auto current_native_key = current_native
        .add("llvm-object-schema",
            "fsim-llvm-native-object-v275")
        .add("process-identity", "legacy-namespace-fixture")
        .finish();
    assert(legacy_native_key != current_native_key);
    ObjectCache native_namespace{root / "native-namespace.sqlite3"};
    assert(native_namespace.store(legacy_native_key, payload, error));
    assert(!error);
    assert(!native_namespace.load(current_native_key, error));
    const auto legacy_payload = native_namespace.load(legacy_native_key, error);
    assert(legacy_payload);
    assert(std::equal(legacy_payload->begin(), legacy_payload->end(),
        payload.begin(), payload.end()));

    std::filesystem::remove_all(root, error);
    std::cout << "cache tests passed\n";
}
