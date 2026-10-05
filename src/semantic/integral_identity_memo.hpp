// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace fsim::semantic::detail {

// Literal parsing depends only on the complete input bytes. A small bounded
// table owns those bytes, including failed parses, without retaining HIR views
// or allocating memory. Hash collisions replace entries; full keys are checked.
class IntegralIdentityMemo {
public:
    using Result = std::optional<std::int64_t>;
    using Parser = Result (*)(std::string_view);
    static constexpr std::size_t capacity = 64U;
    static constexpr std::size_t maximum_key_bytes = 256U;

    explicit constexpr IntegralIdentityMemo(Parser parser) noexcept
        : parser_(parser)
    {
    }

    [[nodiscard]] Result parse(const std::string_view input)
    {
        if (input.size() > maximum_key_bytes) {
            return parser_(input);
        }
        auto& entry = entries_[std::hash<std::string_view> { }(input)
            % capacity];
        if (entry.occupied && entry.size == input.size()
            && std::equal(input.begin(), input.end(), entry.key.begin())) {
            return entry.value;
        }
        // A throwing parse leaves the old entry untouched. No allocation or
        // other fallible work remains between parsing and publishing the key.
        const auto value = parser_(input);
        std::copy(input.begin(), input.end(), entry.key.begin());
        entry.size = input.size();
        entry.value = value;
        entry.occupied = true;
        return value;
    }

private:
    struct Entry {
        std::array<char, maximum_key_bytes> key { };
        std::size_t size { };
        Result value;
        bool occupied { };
    };

    Parser parser_;
    std::array<Entry, capacity> entries_ { };
};

} // namespace fsim::semantic::detail
