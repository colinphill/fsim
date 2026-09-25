// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

    struct WorkspaceDirectory {
        std::filesystem::path previous { std::filesystem::current_path() };
        std::filesystem::path path {
            std::filesystem::temp_directory_path()
                / ("fsim-workspace-controls-"
                    + std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()))
        };

        WorkspaceDirectory()
        {
            std::filesystem::create_directories(path);
            std::filesystem::current_path(path);
        }

        ~WorkspaceDirectory()
        {
            std::error_code error;
            std::filesystem::current_path(previous, error);
            for (std::filesystem::recursive_directory_iterator iterator(path, error), end;
                 !error && iterator != end; iterator.increment(error)) {
                std::error_code permission_error;
                std::filesystem::permissions(iterator->path(),
                    std::filesystem::perms::owner_all,
                    std::filesystem::perm_options::add, permission_error);
            }
            std::filesystem::remove_all(path, error);
        }
    };

    struct Capture {
        int status { };
        std::string output;
        std::string error;
    };

    Capture run(
        const std::vector<std::string>& arguments,
        const std::string& commands = { })
    {
        std::vector<const char*> raw;
        for (const auto& argument : arguments) {
            raw.push_back(argument.c_str());
        }
        std::istringstream input(commands);
        std::ostringstream output;
        std::ostringstream error;
        const auto status = fsim::cli::run(static_cast<int>(raw.size()), raw.data(),
            fsim::app::make_cli_services(input), output, error);
        return { status, output.str(), error.str() };
    }

    Capture successful_run(
        const std::vector<std::string>& arguments,
        const std::string& commands = { })
    {
        auto result = run(arguments, commands);
        if (result.status != 0) {
            std::cerr << result.error;
        }
        assert(result.status == 0);
        assert(result.error.empty());
        return result;
    }

} // namespace

int main()
{
    WorkspaceDirectory workspace;
    const auto missing = run({ "fsim", "debug" }, "quit\n");
    assert(missing.status != 0);
    if (missing.error.find("default") == std::string::npos) {
        std::cerr << missing.error;
    }
    assert(missing.error.find("default") != std::string::npos);
    const auto source = workspace.path / "controls.sv";
    {
        std::ofstream output(source);
        output << "module first;\n"
                  "  initial begin $display(\"default-control-snapshot\"); $finish; end\n"
                  "endmodule\n"
                  "module second;\n"
                  "  initial begin $display(\"named-control-snapshot\"); $finish; end\n"
                  "endmodule\n";
        assert(output.good());
    }
    successful_run({ "fsim", "compile", source.string() });
    successful_run({ "fsim", "elaborate", "--top", "first" });
    successful_run({ "fsim", "elaborate", "--top", "second", "--snapshot", "named" });
    std::filesystem::remove(source);

    const auto default_snapshot = successful_run({ "fsim", "debug" }, "run\nquit\n");
    assert(default_snapshot.output.find("fsim debugger: first") != std::string::npos);
    assert(default_snapshot.output.find("default-control-snapshot") != std::string::npos);
    assert(default_snapshot.output.find("named-control-snapshot") == std::string::npos);
    const auto named = successful_run(
        { "fsim", "debug", "--snapshot", "named" }, "run\nquit\n");
    assert(named.output.find("fsim debugger: second") != std::string::npos);
    assert(named.output.find("named-control-snapshot") != std::string::npos);
    assert(named.output.find("default-control-snapshot") == std::string::npos);
}
