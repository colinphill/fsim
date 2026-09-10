// SPDX-License-Identifier: Apache-2.0
#include "path_test_support.hpp"

#include <system_error>

namespace fsim::test {

bool same_source_path(const std::string_view actual,
    const std::filesystem::path& expected)
{
    const auto actual_path = support::path_from_utf8(actual);
    if (actual_path.lexically_normal() == expected.lexically_normal()) {
        return true;
    }
    std::error_code actual_error;
    std::error_code expected_error;
    const auto canonical_actual
        = std::filesystem::weakly_canonical(actual_path, actual_error);
    const auto canonical_expected
        = std::filesystem::weakly_canonical(expected, expected_error);
    return !actual_error && !expected_error
        && canonical_actual == canonical_expected;
}

} // namespace fsim::test
