// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fsim::support {

/// XXH64 with seed 0 (the xxHash specification), incremental.
class Xxh64 final {
public:
    Xxh64() noexcept;

    void update(std::span<const std::byte> bytes) noexcept;
    void update(std::string_view text) noexcept;
    [[nodiscard]] std::uint64_t finish() const noexcept;

    [[nodiscard]] static std::uint64_t digest(std::string_view text) noexcept;

private:
    std::array<std::uint64_t, 4> lanes_ {};
    std::array<std::byte, 32> buffer_ {};
    std::uint64_t total_bytes_ {};
    std::size_t buffered_ {};
};

/// Integrity checksum of a design-snapshot or library-object payload:
/// "xxh64:" followed by sixteen lowercase hex digits. The checksum detects
/// corruption; content identities (source digests, cache keys) stay SHA-256.
[[nodiscard]] std::string payload_checksum(std::string_view bytes);

/// A recorded payload checksum is either xxh64-tagged or SHA-256 hex.
[[nodiscard]] bool payload_checksum_spelling(std::string_view checksum) noexcept;

/// Verifies bytes, in pieces, against a recorded payload checksum of either
/// spelling.
class PayloadChecksumVerifier final {
public:
    explicit PayloadChecksumVerifier(std::string_view expected);

    void update(std::string_view bytes) noexcept;
    [[nodiscard]] bool matches();

private:
    std::string expected_;
    bool xxh64_ {};
    Xxh64 xxh64_state_;
    Sha256 sha256_state_;
};

[[nodiscard]] bool payload_checksum_matches(
    std::string_view bytes, std::string_view expected);

} // namespace fsim::support
