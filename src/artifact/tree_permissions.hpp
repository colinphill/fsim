// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/native_filesystem.hpp"

#include <filesystem>
#include <system_error>

namespace fsim::artifact::detail {

inline std::error_code set_tree_read_only_permissions(
    const std::filesystem::path& root)
{
    std::error_code error;
    for (auto iterator =
             support::native_fs::recursive_directory_iterator(root, error),
         end = std::filesystem::recursive_directory_iterator {};
        !error && iterator != end; iterator.increment(error)) {
        const auto permissions = iterator->is_directory()
            ? std::filesystem::perms::owner_read
                | std::filesystem::perms::owner_exec
                | std::filesystem::perms::group_read
                | std::filesystem::perms::group_exec
                | std::filesystem::perms::others_read
                | std::filesystem::perms::others_exec
            : std::filesystem::perms::owner_read
                | std::filesystem::perms::group_read
                | std::filesystem::perms::others_read;
        support::native_fs::permissions(
            iterator->path(), permissions,
            std::filesystem::perm_options::replace, error);
    }
    if (!error) {
        support::native_fs::permissions(
            root,
            std::filesystem::perms::owner_read
                | std::filesystem::perms::owner_exec
                | std::filesystem::perms::group_read
                | std::filesystem::perms::group_exec
                | std::filesystem::perms::others_read
                | std::filesystem::perms::others_exec,
            std::filesystem::perm_options::replace, error);
    }
    return error;
}

} // namespace fsim::artifact::detail
