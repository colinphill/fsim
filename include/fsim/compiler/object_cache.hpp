// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/sha256.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fsim::compiler {

struct ObjectCachePruneOptions {
    std::optional<std::uintmax_t> maximum_bytes;
    std::optional<std::size_t> maximum_entries;
    std::optional<std::chrono::seconds> maximum_age;
};

// Sizes count payload bytes.
struct ObjectCachePruneResult {
    std::uint64_t scanned_entries{};
    std::uint64_t removed_entries{};
    std::uintmax_t bytes_before{};
    std::uintmax_t bytes_removed{};
    std::uint64_t remaining_entries{};
    std::uintmax_t remaining_bytes{};

    friend bool operator==(
        const ObjectCachePruneResult&,
        const ObjectCachePruneResult&) = default;
};

class CacheKeyBuilder final {
public:
    CacheKeyBuilder();

    CacheKeyBuilder& add(std::string_view label, std::string_view value) noexcept;
    CacheKeyBuilder& add_bytes(std::string_view label, std::span<const std::byte> value) noexcept;
    bool add_file(std::string_view label, const std::filesystem::path& path, std::error_code& error);

    [[nodiscard]] std::string finish();

private:
    support::Sha256 hasher_;
    bool finished_{};
};

/// A verified cache payload, read straight into its own buffer.
class ObjectCachePayload final {
public:
    ObjectCachePayload(std::unique_ptr<char[]> storage, std::size_t size) noexcept
        : storage_(std::move(storage)), size_(size) {}

    [[nodiscard]] std::string_view text() const noexcept {
        return {storage_.get(), size_};
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::as_bytes(std::span{storage_.get(), size_});
    }
    /// Hands the buffer to a shared owner; the view stays valid for as long
    /// as any copy of the returned pointer lives.
    [[nodiscard]] std::shared_ptr<const std::string_view> share() &&;

private:
    std::unique_ptr<char[]> storage_;
    std::size_t size_{};
};

/// A persistent key-value cache in one SQLite database file. Each entry
/// records its payload's xxh64 checksum, which every load verifies; a
/// corrupt entry is a miss. Concurrent processes are serialized by SQLite's
/// file locks. Copies share one connection.
class ObjectCache final {
public:
    explicit ObjectCache(std::filesystem::path database);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    /// A miss reports std::errc::no_such_file_or_directory.
    [[nodiscard]] std::optional<std::vector<std::byte>> load(
        std::string_view key, std::error_code& error) const;
    /// load without copying the payload out of the read buffer.
    [[nodiscard]] std::optional<ObjectCachePayload> load_payload(
        std::string_view key, std::error_code& error) const;
    bool store(
        std::string_view key, std::span<const std::byte> payload, std::error_code& error) const;
    bool erase(std::string_view key, std::error_code& error) const;
    /// Removes entries older than maximum_age, then least recently used
    /// entries until both other limits hold, and returns the freed pages to
    /// the file system.
    bool prune(
        const ObjectCachePruneOptions& options,
        ObjectCachePruneResult& result,
        std::error_code& error) const;

private:
    struct Connection;

    // Opens the database on first use; the caller holds the connection's
    // mutex. Without `create`, a missing file is a miss rather than a new
    // empty database.
    [[nodiscard]] bool open_connection(bool create, std::error_code& error) const;

    std::shared_ptr<Connection> connection_;
};

/// The largest payload one cache entry holds (256 MiB).
[[nodiscard]] std::uintmax_t maximum_object_cache_payload_bytes() noexcept;
[[nodiscard]] bool object_cache_payload_fits(
    std::uintmax_t payload_bytes) noexcept;

[[nodiscard]] bool valid_cache_key(std::string_view key) noexcept;

} // namespace fsim::compiler
