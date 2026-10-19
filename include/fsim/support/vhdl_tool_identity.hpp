// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/version.hpp"

#include <array>
#include <cctype>
#include <optional>
#include <string_view>
#include <utility>

namespace fsim::support {

/// The value of a VHDL-2019 tool identity: a standard conditional analysis
/// identifier (IEEE 1076-2019 24.2), which the STD.ENV identity functions
/// and GETENV also return (16.5.6). The name is matched ignoring case.
[[nodiscard]] inline std::optional<std::string_view> vhdl_tool_identity(
    const std::string_view name) noexcept
{
    constexpr std::array values {
        std::pair { std::string_view { "vhdl_version" },
            std::string_view { "2019" } },
        std::pair { std::string_view { "tool_type" },
            std::string_view { "SIMULATION" } },
        std::pair { std::string_view { "tool_vendor" },
            std::string_view { "FSIM PROJECT" } },
        std::pair { std::string_view { "tool_name" },
            std::string_view { "FSIM" } },
        std::pair { std::string_view { "tool_edition" },
            std::string_view { "COMMUNITY" } },
        std::pair { std::string_view { "tool_version" },
            fsim::version },
    };
    for (const auto& [identifier, value] : values) {
        if (identifier.size() != name.size()) {
            continue;
        }
        bool same = true;
        for (std::size_t index { }; index < name.size() && same; ++index) {
            same = std::tolower(static_cast<unsigned char>(name[index]))
                == identifier[index];
        }
        if (same) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace fsim::support
