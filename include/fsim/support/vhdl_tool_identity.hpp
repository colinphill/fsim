// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/native_filesystem.hpp"
#include "fsim/version.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <string>
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

/// The STD.ENV FILE_PATH value for a source file: its fully resolved
/// absolute directory, without the file name and without a trailing
/// directory separator (IEEE 1076-2019 16.5.6).
[[nodiscard]] inline std::string vhdl_environment_file_path(
    const std::filesystem::path& source)
{
    std::error_code error;
    auto resolved = std::filesystem::absolute(source, error);
    if (error) {
        resolved = source;
    }
    if (auto canonical = native_fs::weakly_canonical(resolved, error);
        !error) {
        resolved = std::move(canonical);
    }
    return resolved.lexically_normal().parent_path().generic_string();
}

} // namespace fsim::support
