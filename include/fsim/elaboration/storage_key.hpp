// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fsim::elaboration {

/// The key of a design object that is found by its declaration identity and
/// has no name a user can write. It lives beside the name maps, never in
/// them.
struct StorageKey {
    enum class Kind : std::uint8_t {
        /// The storage of a static variable of a callable: `declarations`
        /// holds its declaration; `name` is the instance path in design
        /// maps and empty in the maps of one instance.
        callable_static,
        /// An object of a VHDL package instance: `declarations` holds the
        /// package instance declarations, outermost first; `name` is the
        /// lower-case object name.
        package_object,
        /// An element of a named-event array: `declarations` holds the
        /// array's declaration; `name` is the element index text.
        event_array_element,
    };

    Kind kind { };
    std::vector<std::uint32_t> declarations;
    std::string name;

    [[nodiscard]] friend bool operator==(
        const StorageKey&, const StorageKey&) = default;
};

struct StorageKeyHash {
    [[nodiscard]] std::size_t operator()(const StorageKey& key) const noexcept
    {
        auto hash = std::hash<std::string> { }(key.name);
        const auto mix = [&hash](const std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
        };
        mix(static_cast<std::size_t>(key.kind));
        for (const auto declaration : key.declarations) {
            mix(declaration);
        }
        return hash;
    }
};

} // namespace fsim::elaboration
