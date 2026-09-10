// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

namespace fsim::diagnostic {

[[nodiscard]] std::string unsupported_artifact_identity(
    std::string_view family, std::string_view found,
    std::string_view required, std::string_view artifact);

} // namespace fsim::diagnostic
