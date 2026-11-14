// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdexcept>
#include <string>

namespace fsim::runtime {

/// Raised when a resolved signal's source is a composite with null and
/// non-null subelements (IEEE 1076-2019 14.7.3.2): an error of the run.
class VhdlPartialNullSourceError final : public std::runtime_error {
public:
  explicit VhdlPartialNullSourceError(const std::string &signal)
      : std::runtime_error(
            "a source of resolved signal '" + signal +
            "' is a composite with null and non-null subelements (IEEE "
            "1076-2019 14.7.3.2)") {}
};

} // namespace fsim::runtime
