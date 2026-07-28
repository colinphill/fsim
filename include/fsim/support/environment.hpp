// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fsim::support {

[[nodiscard]] std::optional<std::string> environment_variable(
    std::string_view name);

}  // namespace fsim::support
