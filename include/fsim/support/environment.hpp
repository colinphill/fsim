// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fsim::support {

[[nodiscard]] std::optional<std::string> environment_variable(
    std::string_view name);

/// Report whether Win32 path functions accept paths beyond MAX_PATH in this
/// process. Windows requires both the executable's longPathAware manifest and
/// the machine's LongPathsEnabled registry value, and caches the answer for
/// the life of the process. Always true on other platforms.
[[nodiscard]] bool windows_long_paths_enabled() noexcept;

}  // namespace fsim::support
