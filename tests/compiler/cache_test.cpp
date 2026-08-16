// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"
#include "fsim/compiler/cache_support.hpp"
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
#include <vector>

int main() {
    using fsim::compiler::CacheKeyBuilder;
    using fsim::compiler::ObjectCache;
    using fsim::support::Sha256;

    assert(
        Sha256::hex(Sha256::digest("abc"))
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

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

    ObjectCache cache{root};
    const std::array payload{
        std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};

    // A process that died before publishing lock ownership must not wedge the
    // cache forever. Marker-less locks are only reclaimed after a grace age.
    const auto stale_lock =
        std::filesystem::path{cache.path_for(key).string() + ".lock"};
    std::filesystem::create_directories(stale_lock, error);
    assert(!error);
    std::filesystem::last_write_time(
        stale_lock,
        std::filesystem::file_time_type::clock::now() - std::chrono::hours{1},
        error);
    assert(!error);

    assert(cache.store(key, payload, error));
    assert(!error);
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

    const auto loaded = cache.load(key, error);
    assert(loaded);
    assert(!error);
    assert(std::equal(loaded->begin(), loaded->end(), payload.begin(), payload.end()));

    // Publishing an updated payload replaces the cache file atomically even
    // when a destination already exists.
    const std::array replacement{
        std::byte{0xca}, std::byte{0xfe}, std::byte{0xba}, std::byte{0xbe}};
    assert(cache.store(key, replacement, error));
    const auto replaced = cache.load(key, error);
    assert(replaced);
    assert(std::equal(
        replaced->begin(), replaced->end(), replacement.begin(), replacement.end()));

    // Corrupt and truncated entries are isolated as cache misses with a stable
    // data-integrity error. A subsequent publisher can replace either entry.
    {
        std::fstream corrupt(cache.path_for(key), std::ios::binary | std::ios::in | std::ios::out);
        assert(corrupt);
        corrupt.seekp(-1, std::ios::end);
        corrupt.put('\0');
    }
    assert(!cache.load(key, error));
    assert(error == std::errc::illegal_byte_sequence);
    assert(cache.store(key, replacement, error));
    std::filesystem::resize_file(cache.path_for(key), 4, error);
    assert(!error);
    assert(!cache.load(key, error));
    assert(error == std::errc::illegal_byte_sequence);

    // Cache artifacts have a governed input ceiling, checked before allocation.
    // resize_file creates a sparse file on supported filesystems, keeping this
    // regression cheap while covering hostile oversized inputs.
    constexpr std::uintmax_t maximum_cache_entry_bytes =
        256U * 1024U * 1024U;
    std::filesystem::resize_file(
        cache.path_for(key), maximum_cache_entry_bytes + 1U, error);
    assert(!error);
    assert(!cache.load(key, error));
    assert(error == std::errc::file_too_large);
    assert(cache.store(key, replacement, error));

    // Valid cache hits require no write access. Restore owner write permission
    // before exercising replacement and cleanup on both POSIX and Windows.
    std::filesystem::permissions(
        cache.path_for(key),
        std::filesystem::perms::owner_write
            | std::filesystem::perms::group_write
            | std::filesystem::perms::others_write,
        std::filesystem::perm_options::remove,
        error);
    assert(!error);
    const auto read_only = cache.load(key, error);
    assert(read_only);
    assert(!error);
    assert(std::equal(
        read_only->begin(), read_only->end(), replacement.begin(), replacement.end()));
    std::filesystem::permissions(
        cache.path_for(key),
        std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add,
        error);
    assert(!error);

    // Concurrent writers serialize through the destination lock, publish a
    // complete entry atomically, and leave neither locks nor temporaries behind.
    std::vector<std::future<std::pair<bool, std::error_code>>> publishers;
    for (int writer = 0; writer < 4; ++writer) {
        publishers.emplace_back(std::async(
            std::launch::async,
            [&cache, &key, &replacement] {
                std::error_code publisher_error;
                const auto stored = cache.store(key, replacement, publisher_error);
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
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        const auto filename = entry.path().filename().string();
        assert(!filename.ends_with(".lock"));
        assert(filename.find(".fobj.tmp.") == std::string::npos);
    }

    // Canonical keys are lowercase hexadecimal; case variants and malformed
    // path components cannot alias the same cache entry on Windows.
    auto uppercase_key = key;
    uppercase_key.front() = static_cast<char>(
        uppercase_key.front() >= 'a' && uppercase_key.front() <= 'f'
            ? uppercase_key.front() - ('a' - 'A')
            : 'G');
    assert(cache.path_for(uppercase_key).empty());
    assert(!cache.load(uppercase_key, error));
    assert(error == std::errc::invalid_argument);

    assert(cache.erase(key, error));

    const auto make_key = [](const std::string_view identity) {
        CacheKeyBuilder builder;
        return builder.add("identity", identity).finish();
    };
    const auto eviction_root = root / "eviction";
    ObjectCache eviction_cache{eviction_root};
    const auto key_a = make_key("entry-a");
    const auto key_b = make_key("entry-b");
    const auto key_c = make_key("entry-c");
    const auto key_d = make_key("entry-d");
    assert(eviction_cache.store(key_a, payload, error));
    assert(eviction_cache.store(key_b, payload, error));
    assert(eviction_cache.store(key_c, payload, error));
    assert(eviction_cache.store(key_d, replacement, error));
    const auto file_now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(
        eviction_cache.path_for(key_a), file_now - std::chrono::hours{4}, error);
    assert(!error);
    std::filesystem::last_write_time(
        eviction_cache.path_for(key_b), file_now - std::chrono::hours{3}, error);
    assert(!error);
    std::filesystem::last_write_time(
        eviction_cache.path_for(key_c), file_now - std::chrono::hours{2}, error);
    assert(!error);
    std::filesystem::last_write_time(
        eviction_cache.path_for(key_d), file_now - std::chrono::hours{1}, error);
    assert(!error);

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
    assert(std::filesystem::exists(eviction_cache.path_for(key_a)));
    assert(!std::filesystem::exists(eviction_cache.path_for(key_b)));
    assert(!std::filesystem::exists(eviction_cache.path_for(key_c)));
    assert(std::filesystem::exists(eviction_cache.path_for(key_d)));

    // A byte limit accounts for encoded on-disk entry sizes, not only payload
    // bytes, and removes oldest entries until both active limits are met.
    const auto a_size =
        std::filesystem::file_size(eviction_cache.path_for(key_a), error);
    assert(!error);
    fsim::compiler::ObjectCachePruneOptions byte_limit;
    byte_limit.maximum_bytes = a_size - 1;
    assert(eviction_cache.prune(byte_limit, prune, error));
    assert(prune.removed_entries == 2);
    assert(prune.remaining_entries == 0);
    assert(prune.remaining_bytes == 0);
    assert(prune.bytes_removed == prune.bytes_before);

    const auto key_e = make_key("entry-e");
    const auto key_f = make_key("entry-f");
    assert(eviction_cache.store(key_e, payload, error));
    assert(eviction_cache.store(key_f, payload, error));
    std::filesystem::last_write_time(
        eviction_cache.path_for(key_e), file_now - std::chrono::hours{2}, error);
    assert(!error);
    std::filesystem::last_write_time(
        eviction_cache.path_for(key_f), file_now, error);
    assert(!error);
    fsim::compiler::ObjectCachePruneOptions age_limit;
    age_limit.maximum_age = std::chrono::hours{1};
    assert(eviction_cache.prune(age_limit, prune, error));
    assert(prune.removed_entries == 1);
    assert(!std::filesystem::exists(eviction_cache.path_for(key_e)));
    assert(std::filesystem::exists(eviction_cache.path_for(key_f)));

    // Pruning never steals a live writer lock. Once released, the same policy
    // can remove the entry.
    const auto locked_path =
        std::filesystem::path{
            eviction_cache.path_for(key_f).string() + ".lock"};
    {
        std::error_code lock_error;
        fsim::compiler::detail::CacheDirectoryLock live{
            locked_path, lock_error, std::chrono::milliseconds{20}};
        assert(live.held());
        fsim::compiler::ObjectCachePruneOptions remove_all;
        remove_all.maximum_entries = 0;
        assert(eviction_cache.prune(remove_all, prune, error));
        assert(prune.removed_entries == 0);
        assert(prune.skipped_locked_entries == 1);
        assert(prune.remaining_entries == 1);
    }
    fsim::compiler::ObjectCachePruneOptions remove_all;
    remove_all.maximum_entries = 0;
    assert(eviction_cache.prune(remove_all, prune, error));
    assert(prune.removed_entries == 1);
    assert(prune.remaining_entries == 0);

    // Stale publisher temporaries are reclaimed under the destination lock,
    // while fresh temporaries and unrelated files are preserved.
    const auto key_g = make_key("entry-g");
    const auto g_path = eviction_cache.path_for(key_g);
    std::filesystem::create_directories(g_path.parent_path(), error);
    assert(!error);
    const auto stale_temporary =
        std::filesystem::path{g_path.string() + ".tmp.1"};
    const auto fresh_temporary =
        std::filesystem::path{g_path.string() + ".tmp.2"};
    {
        std::ofstream stale(stale_temporary, std::ios::binary);
        std::ofstream fresh(fresh_temporary, std::ios::binary);
        stale << "stale";
        fresh << "fresh";
    }
    std::filesystem::last_write_time(
        stale_temporary, file_now - std::chrono::hours{2}, error);
    assert(!error);
    std::filesystem::last_write_time(
        fresh_temporary, file_now, error);
    assert(!error);
    const auto unrelated = g_path.parent_path() / "keep.txt";
    {
        std::ofstream keep(unrelated, std::ios::binary);
        keep << "keep";
    }
    fsim::compiler::ObjectCachePruneOptions temporary_cleanup;
    temporary_cleanup.temporary_file_grace = std::chrono::hours{1};
    assert(eviction_cache.prune(temporary_cleanup, prune, error));
    assert(prune.removed_temporary_files == 1);
    assert(!std::filesystem::exists(stale_temporary));
    assert(std::filesystem::exists(fresh_temporary));
    assert(std::filesystem::exists(unrelated));

    // Empty canonical shard directories are removed, while malformed cache
    // content is never interpreted as an object entry.
    const auto isolated_root = root / "isolated";
    ObjectCache isolated{isolated_root};
    const std::string isolated_key =
        "aa00000000000000000000000000000000000000000000000000000000000000";
    assert(isolated.store(isolated_key, payload, error));
    const auto isolated_shard = isolated.path_for(isolated_key).parent_path();
    assert(isolated.prune(remove_all, prune, error));
    assert(!std::filesystem::exists(isolated_shard));
    std::filesystem::create_directories(isolated_root / "zz", error);
    assert(!error);
    {
        std::ofstream malformed(isolated_root / "zz" / "not-an-entry.fobj");
        malformed << "keep";
    }
    assert(isolated.prune(remove_all, prune, error));
    assert(std::filesystem::exists(
        isolated_root / "zz" / "not-an-entry.fobj"));

    fsim::compiler::ObjectCachePruneOptions invalid;
    invalid.maximum_age = std::chrono::seconds{-1};
    assert(!isolated.prune(invalid, prune, error));
    assert(error == std::errc::invalid_argument);

    ObjectCache absent{root / "absent"};
    fsim::compiler::ObjectCachePruneOptions no_limits;
    assert(absent.prune(no_limits, prune, error));
    assert(prune == fsim::compiler::ObjectCachePruneResult{});

    std::filesystem::remove_all(root, error);
    std::cout << "cache tests passed\n";
}
