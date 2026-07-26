// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fsim::support {

class Sha256 final {
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256() noexcept;

    void update(std::span<const std::byte> bytes) noexcept;
    void update(std::string_view text) noexcept;
    [[nodiscard]] Digest finish() noexcept;

    [[nodiscard]] static Digest digest(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] static Digest digest(std::string_view text) noexcept;
    [[nodiscard]] static std::string hex(const Digest& digest);

private:
    void transform(const std::byte* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::uint64_t total_bytes_{};
    std::size_t buffered_{};
    bool finished_{};
};

} // namespace fsim::support
