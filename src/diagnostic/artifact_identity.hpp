// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

namespace fsim::diagnostic {

inline std::string unsupported_artifact_identity(
    const std::string_view family,
    const std::string_view found,
    const std::string_view required,
    const std::string_view artifact)
{
    std::string message { "unsupported " };
    message.append(family);
    message.append(" identity: found ");
    message.append(found);
    message.append("; required ");
    message.append(required);
    message.append("; regenerate ");
    message.append(artifact);
    message.append(" with this fsim build");
    return message;
}

} // namespace fsim::diagnostic
