// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_annotation_scope.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace fsim::app::sdf_detail {

inline void append_field(std::string& target, const std::string_view value)
{
    target += std::to_string(value.size());
    target.push_back(':');
    target.append(value);
}

[[nodiscard]] inline bool equal_under_case_policy(
    const std::string_view left, const std::string_view right,
    const SdfHierarchyCasePolicy policy)
{
    if (policy == SdfHierarchyCasePolicy::Sensitive) {
        return left == right;
    }
    return std::ranges::equal(left, right, [](const char lhs, const char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs))
            == std::tolower(static_cast<unsigned char>(rhs));
    });
}

} // namespace fsim::app::sdf_detail
