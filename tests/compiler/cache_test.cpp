// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"
#include "fsim/compiler/cache_support.hpp"
#include "fsim/support/sha256.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

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

    assert(cache.erase(key, error));
    std::filesystem::remove_all(root, error);
    std::cout << "cache tests passed\n";
}
