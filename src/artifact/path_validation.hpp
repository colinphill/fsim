// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace fsim::artifact::detail {

inline bool safe_relative_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name()
        || path.has_root_directory()) {
        return false;
    }
    return path.lexically_normal() == path
        && std::ranges::none_of(path, [](const auto& component) {
               return component == ".." || component == ".";
           });
}

inline std::filesystem::path staging_path(
    const std::filesystem::path& destination,
    std::atomic_uint64_t& sequence_counter)
{
    const auto sequence = sequence_counter.fetch_add(1, std::memory_order_relaxed);
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path()
        / ("." + destination.filename().string() + ".staging-"
            + std::to_string(nonce) + "-" + std::to_string(sequence));
}

} // namespace fsim::artifact::detail
