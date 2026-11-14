// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"
#include "fsim/support/sqlite_database.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using fsim::support::sqlite::Database;
using fsim::support::sqlite::Statement;
using fsim::support::sqlite::Step;

Database open(const std::filesystem::path& path)
{
    Database database { "cache wire fixture" };
    std::string error;
    const bool opened = database.open(
        path, Database::OpenMode::read_write_existing, { }, error);
    assert(opened);
    return database;
}

void execute(const std::filesystem::path& path, const std::string& sql,
    const std::string_view key)
{
    auto database = open(path);
    std::string error;
    Statement statement { database, sql.c_str(), error };
    const bool done = statement && statement.bind_text(1, key, error)
        && statement.finish(error);
    assert(done);
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-cache-wire-fixture-" + std::to_string(nonce));
    const auto path = root / "fixture.sqlite3";
    const fsim::compiler::ObjectCache cache { path };
    fsim::compiler::CacheKeyBuilder builder;
    const auto key = builder.add("codec-fixture", "v1").finish();
    const std::array payload {
        std::byte { 0x61 }, std::byte { 0x62 }, std::byte { 0x63 }
    };
    std::error_code error;
    assert(cache.store(key, payload, error) && !error);

    // Freeze the stored form: fsim's application id, cache schema 1, and
    // one row per entry with the payload's xxh64 checksum and size.
    {
        auto database = open(path);
        std::string message;
        assert(database.pragma_integer("PRAGMA application_id", message)
            == 0x4653494D);
        assert(database.pragma_integer("PRAGMA user_version", message) == 1);
        Statement query { database,
            "SELECT key, checksum, size, data FROM entries", message };
        assert(query && query.step(message) == Step::row);
        assert(query.text(0, message) == key);
        assert(query.text(1, message) == "xxh64:44bc2cf5ad770999");
        assert(query.integer(2, message) == 3);
        assert(query.blob(3, message) == "abc");
        assert(query.step(message) == Step::done);
    }
    const auto loaded = cache.load(key, error);
    assert(loaded && std::ranges::equal(*loaded, payload) && !error);

    const auto reject = [&](const std::string& assignment) {
        execute(path, "UPDATE entries SET " + assignment + " WHERE key = ?1", key);
        assert(!cache.load(key, error));
        assert(error == std::errc::illegal_byte_sequence);
        assert(cache.store(key, payload, error) && !error);
    };
    reject("data = x''");
    reject("data = x'6162'");
    reject("data = x'61626364'");
    reject("data = x'616263', size = 4");
    reject("checksum = 'xxh64:44bc2cf5ad770990'");
    // A SHA-256 checksum, as V1 envelopes framed them, does not verify.
    reject("checksum = "
           "'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'");
    reject("checksum = 'xxh64:44BC2CF5AD770999'");

    assert(cache.load(key, error) && !error);
    std::filesystem::remove_all(root, error);
}
