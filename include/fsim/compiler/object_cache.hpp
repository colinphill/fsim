// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/sha256.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fsim::compiler {

struct ObjectCachePruneOptions {
    std::optional<std::uintmax_t> maximum_bytes;
    std::optional<std::size_t> maximum_entries;
    std::optional<std::chrono::seconds> maximum_age;
    bool remove_stale_temporary_files{true};
    std::chrono::seconds temporary_file_grace{std::chrono::hours{1}};
};

struct ObjectCachePruneResult {
    std::uint64_t scanned_entries{};
    std::uint64_t removed_entries{};
    std::uint64_t skipped_locked_entries{};
    std::uint64_t removed_temporary_files{};
    std::uint64_t failed_removals{};
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

class ObjectCache final {
public:
    explicit ObjectCache(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path path_for(std::string_view key) const;

    [[nodiscard]] std::optional<std::vector<std::byte>> load(
        std::string_view key, std::error_code& error) const;
    bool store(
        std::string_view key, std::span<const std::byte> payload, std::error_code& error) const;
    bool erase(std::string_view key, std::error_code& error) const;
    bool prune(
        const ObjectCachePruneOptions& options,
        ObjectCachePruneResult& result,
        std::error_code& error) const;

private:
    std::filesystem::path root_;
};

[[nodiscard]] bool valid_cache_key(std::string_view key) noexcept;

} // namespace fsim::compiler
