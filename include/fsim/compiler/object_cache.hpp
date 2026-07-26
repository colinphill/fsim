// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/sha256.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fsim::compiler {

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

private:
    std::filesystem::path root_;
};

[[nodiscard]] bool valid_cache_key(std::string_view key) noexcept;

} // namespace fsim::compiler
