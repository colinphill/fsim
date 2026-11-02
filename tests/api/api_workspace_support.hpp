// SPDX-License-Identifier: Apache-2.0
#pragma once

// The C API opens snapshots that the workspace commands elaborated, and the
// application tests run designs through them. These helpers run those
// commands in-process, in the workspace directory, as a user would run
// `fsim compile`, `fsim elaborate` and `fsim simulate` there.

#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/support/path.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fsim::tests::api_workspace {

// Points the per-user configuration script at `directory`, which holds
// none, for the rest of the process: a developer's own fsim.tcl must not
// change what a test elaborates or simulates. Workspace scripts still run.
inline void isolate_user_configuration(const std::filesystem::path& directory)
{
    const auto value = fsim::support::path_to_utf8(directory);
#if defined(_WIN32)
    [[maybe_unused]] const auto status = ::_putenv_s("APPDATA", value.c_str());
#else
    [[maybe_unused]] const auto status
        = ::setenv("XDG_CONFIG_HOME", value.c_str(), 1);
#endif
    assert(status == 0);
}

// Writes the workspace configuration script, e.g.
// "fsim::config set elaborate.time_resolution 1ns\n".
inline void write_configuration(
    const std::filesystem::path& workspace, const std::string_view script)
{
    std::filesystem::create_directories(workspace);
    std::ofstream output { workspace / "fsim.tcl", std::ios::binary };
    output << script;
    assert(output.good());
}

struct CommandResult {
    int status { };
    std::string output;
    // Diagnostics, when the command kept them apart from its output.
    std::string error;
};

// Runs `fsim ARGUMENTS...` with `workspace` as the working directory, then
// restores the previous one.
inline int run(const std::filesystem::path& workspace,
    std::vector<std::string> arguments, std::ostream& output,
    std::ostream& error_output)
{
    std::error_code error;
    std::filesystem::create_directories(workspace, error);
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(workspace);
    arguments.insert(arguments.begin(), "fsim");
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments) {
        raw.push_back(argument.c_str());
    }
    std::istringstream input;
    const int status = fsim::cli::run(static_cast<int>(raw.size()),
        raw.data(), fsim::app::make_cli_services(input), output,
        error_output);
    std::filesystem::current_path(previous);
    return status;
}

// Output and diagnostics in one transcript.
inline CommandResult run(const std::filesystem::path& workspace,
    std::vector<std::string> arguments)
{
    std::ostringstream output;
    const int status = run(workspace, std::move(arguments), output, output);
    return { status, output.str(), { } };
}

// Output and diagnostics apart.
inline CommandResult run_streams(const std::filesystem::path& workspace,
    std::vector<std::string> arguments)
{
    std::ostringstream output;
    std::ostringstream error;
    const int status = run(workspace, std::move(arguments), output, error);
    return { status, output.str(), error.str() };
}

// Runs a command that must succeed; prints its transcript otherwise.
inline void expect(const std::filesystem::path& workspace,
    std::vector<std::string> arguments)
{
    const auto command = arguments;
    const auto result = run(workspace, std::move(arguments));
    if (result.status != 0) {
        std::cerr << "fsim";
        for (const auto& argument : command) {
            std::cerr << ' ' << argument;
        }
        std::cerr << " failed with " << result.status << ":\n"
                  << result.output;
    }
    assert(result.status == 0);
}

// Makes `workspace` a workspace configured by `script` with no per-user
// script, compiles `sources` there and elaborates `top` into the default
// snapshot, as a user would before running `fsim simulate`.
inline void prepare(const std::filesystem::path& workspace,
    const std::string_view script, std::vector<std::string> sources,
    std::string top)
{
    isolate_user_configuration(workspace / "user-configuration");
    write_configuration(workspace, script);
    sources.insert(sources.begin(), "compile");
    expect(workspace, std::move(sources));
    expect(workspace, { "elaborate", std::move(top) });
}

} // namespace fsim::tests::api_workspace
