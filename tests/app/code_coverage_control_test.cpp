// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/project/project.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path = std::filesystem::temp_directory_path()
            / ("fsim-code-coverage-control-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_text(
    const std::filesystem::path& path,
    const std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(output.good());
}

std::string manifest(const std::string_view coverage = {})
{
    return "schema = 3\n"
           "[project]\n"
           "top = \"sv:work.top\"\n"
        + std::string { coverage }
        + "[[source_set]]\n"
          "language = \"systemverilog\"\n"
          "standard = \"2017\"\n"
          "files = [\"top.sv\"]\n";
}

std::optional<fsim::project::Config> parse_manifest(
    const TemporaryDirectory& directory,
    const std::string_view text,
    fsim::diagnostic::Engine& diagnostics)
{
    return fsim::project::parse(
        text, "fsim.toml", directory.path, diagnostics);
}

void test_manifest_surface(const TemporaryDirectory& directory)
{
    fsim::diagnostic::Engine default_diagnostics;
    const auto disabled
        = parse_manifest(directory, manifest(), default_diagnostics);
    assert(disabled && !default_diagnostics.has_error());
    assert(disabled->schema == 3U);
    assert(!disabled->coverage.enabled);
    assert(!fsim::app::code_coverage_enabled(*disabled));

    fsim::diagnostic::Engine enabled_diagnostics;
    const auto enabled = parse_manifest(directory,
        manifest("[coverage]\nenabled = true\n"), enabled_diagnostics);
    assert(enabled && !enabled_diagnostics.has_error());
    assert(enabled->coverage.enabled);
    assert(fsim::app::code_coverage_enabled(*enabled));

    for (const auto& bad : {
             manifest("[coverage]\nenabled = \"yes\"\n"),
             manifest("[coverage]\nunknown = true\n"),
             manifest("[coverage]\nenabled = true\n[coverage]\n") }) {
        fsim::diagnostic::Engine diagnostics;
        assert(!parse_manifest(directory, bad, diagnostics));
        assert(diagnostics.has_error());
    }

    auto stale = manifest("[coverage]\nenabled = true\n");
    stale.replace(9U, 1U, "2");
    fsim::diagnostic::Engine stale_diagnostics;
    assert(!parse_manifest(directory, stale, stale_diagnostics));
    assert(stale_diagnostics.has_error());
}

void test_cli_override(const TemporaryDirectory& directory)
{
    const auto manifest_path = directory.path / "fsim.toml";
    write_text(manifest_path,
        manifest("[coverage]\nenabled = false\n"));

    bool invoked = false;
    fsim::cli::Services services;
    services.run = [&](const fsim::cli::Invocation& invocation,
                       const fsim::project::Config& config,
                       fsim::diagnostic::Engine&,
                       std::ostream&,
                       std::ostream&) {
        invoked = true;
        assert(invocation.code_coverage == std::optional<bool> { true });
        assert(config.coverage.enabled);
        assert(fsim::app::code_coverage_enabled(config));
        return 0;
    };
    const auto manifest_text = manifest_path.generic_string();
    const std::array arguments {
        "fsim", "run", "--project", manifest_text.c_str(),
        "--code-coverage" };
    std::ostringstream output;
    std::ostringstream error;
    assert(fsim::cli::run(static_cast<int>(arguments.size()),
               arguments.data(), services, output, error)
        == 0);
    assert(invoked);
    assert(error.str().empty());

    const auto source_text = (directory.path / "top.sv").generic_string();
    const std::array invalid { "fsim", "check", "--code-coverage",
        source_text.c_str() };
    fsim::diagnostic::Engine invalid_diagnostics;
    assert(!fsim::cli::parse_arguments(static_cast<int>(invalid.size()),
        invalid.data(), invalid_diagnostics));
    assert(invalid_diagnostics.has_error());
}

void test_disabled_build_has_no_runtime_state(
    const TemporaryDirectory& directory)
{
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.manifest_path = directory.path / "fsim.toml";
    config.project.name = "coverage-control";
    config.project.top = "sv:work.top";
    config.project.tops = { { "sv:work.top", "top" } };
    config.build.cache_path = directory.path / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.files = { directory.path / "top.sv" };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    const auto built = fsim::app::build_project(config, diagnostics);
    assert(built && !diagnostics.has_error());
    assert(!built->code_coverage_enabled);
    assert(!built->design.code_coverage_inventory());

    config.coverage.enabled = true;
    fsim::diagnostic::Engine enabled_diagnostics;
    const auto enabled = fsim::app::build_project(config, enabled_diagnostics);
    assert(enabled && !enabled_diagnostics.has_error());
    assert(enabled->code_coverage_enabled);
}

} // namespace

int main()
{
    TemporaryDirectory directory;
    write_text(directory.path / "top.sv",
        "module top; initial begin $finish; end endmodule\n");
    test_manifest_surface(directory);
    test_cli_override(directory);
    test_disabled_build_has_no_runtime_state(directory);
}
