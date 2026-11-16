// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace fsim::support {

// A method of the built-in process class (IEEE 1800-2023 9.7). self() is
// static and is selected through the class scope, not a handle.
enum class ProcessMethod : std::uint8_t {
    status,
    kill,
    await,
    suspend,
    resume,
    srandom,
    get_randstate,
    set_randstate,
};

// The method `member` names in the process class's scope: the lookup of a
// member identifier selected from a handle whose type is process.
[[nodiscard]] constexpr std::optional<ProcessMethod> process_method(
    const std::string_view member) noexcept
{
    if (member == "status") {
        return ProcessMethod::status;
    }
    if (member == "kill") {
        return ProcessMethod::kill;
    }
    if (member == "await") {
        return ProcessMethod::await;
    }
    if (member == "suspend") {
        return ProcessMethod::suspend;
    }
    if (member == "resume") {
        return ProcessMethod::resume;
    }
    if (member == "srandom") {
        return ProcessMethod::srandom;
    }
    if (member == "get_randstate") {
        return ProcessMethod::get_randstate;
    }
    if (member == "set_randstate") {
        return ProcessMethod::set_randstate;
    }
    return std::nullopt;
}

} // namespace fsim::support
