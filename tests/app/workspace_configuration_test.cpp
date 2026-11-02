// SPDX-License-Identifier: Apache-2.0
#include "fsim/cli/driver.hpp"
#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/configuration.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/path.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace project = fsim::project;

void set_environment_variable(const char* const name, const char* const value)
{
#if defined(_WIN32)
    const auto result = ::_putenv_s(name, value == nullptr ? "" : value);
#else
    const auto result = value == nullptr
        ? ::unsetenv(name)
        : ::setenv(name, value, 1);
#endif
    if (result != 0) {
        std::abort();
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
        , previous_(fsim::support::environment_variable(name))
    {
        set_environment_variable(name_.c_str(), value);
    }

    ~ScopedEnvironment()
    {
        set_environment_variable(
            name_.c_str(), previous_ ? previous_->c_str() : nullptr);
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

class Directory final {
public:
    Directory()
        : previous_(fs::current_path())
    {
        static std::atomic_uint64_t sequence { 0U };
        const auto stamp
            = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("fsim-workspace-configuration-"
            + std::to_string(stamp) + "-" + std::to_string(sequence++));
        fs::create_directories(path / "workspace");
        fs::create_directories(path / "user" / "fsim");
        fs::current_path(path / "workspace");
    }

    ~Directory()
    {
        std::error_code error;
        fs::current_path(previous_, error);
        fs::remove_all(path, error);
    }

    fs::path path;

private:
    fs::path previous_;
};

struct Capture {
    int status { };
    std::string output;
    std::string error;
};

Capture invoke(std::vector<std::string> arguments)
{
    arguments.insert(arguments.begin(), "fsim");
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments) {
        raw.push_back(argument.c_str());
    }
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int status = fsim::cli::run(static_cast<int>(raw.size()), raw.data(),
        fsim::app::make_cli_services(input), output, error);
    return { status, output.str(), error.str() };
}

Capture expect(std::vector<std::string> arguments, const bool success = true)
{
    auto result = invoke(arguments);
    if ((result.status == 0) != success) {
        std::cerr << "Unexpected result " << result.status << ":";
        for (const auto& argument : arguments) {
            std::cerr << ' ' << argument;
        }
        std::cerr << '\n' << result.output << result.error;
    }
    assert((result.status == 0) == success);
    return result;
}

bool contains(const std::string_view text, const std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}

void write_file(const fs::path& path, const std::string_view text)
{
    std::ofstream output { path, std::ios::binary };
    output << text;
    assert(output.good());
}

project::ConfigOrigin origin(const project::ConfigLayer layer)
{
    return { layer, "fsim.tcl", 1U };
}

void test_settings_and_layers()
{
    project::Configuration configuration;
    assert(!configuration.set("compile.library", { "base" },
        origin(project::ConfigLayer::user)));
    assert(!configuration.set("compile.library", { "top" },
        origin(project::ConfigLayer::workspace)));
    assert(configuration.find("compile.library")->values.front() == "top");
    assert(configuration.unset("compile.library", project::ConfigLayer::workspace));
    assert(!configuration.unset("compile.library", project::ConfigLayer::workspace));
    assert(configuration.find("compile.library")->values.front() == "base");
    assert(configuration.find("compile.library")->origin.layer
        == project::ConfigLayer::user);

    // A language-scoped key wins over the plain one for that language only.
    // A standard names one language, so only the scoped keys exist.
    assert(configuration.set("compile.standard", { "2012" },
        origin(project::ConfigLayer::workspace)));
    assert(!configuration.set("compile.vhdl.standard", { "2008" },
        origin(project::ConfigLayer::workspace)));
    assert(configuration.find("compile", project::Language::vhdl, "standard")
               ->values.front()
        == "2008");
    assert(configuration.find("compile", project::Language::system_verilog,
               "standard")
        == nullptr);

    // Errors leave the configuration as it was.
    const auto unknown = configuration.set("compile.libary", { "x" },
        origin(project::ConfigLayer::session));
    assert(unknown && contains(*unknown, "compile.library"));
    assert(configuration.set("elaborate.aot", { "maybe" },
        origin(project::ConfigLayer::session)));
    assert(configuration.set("compile.jobs", { "-1" },
        origin(project::ConfigLayer::session)));
    assert(configuration.find("elaborate.aot") == nullptr);
    assert(!configuration.set("elaborate.aot", { "1" },
        origin(project::ConfigLayer::session)));
    assert(configuration.find("elaborate.aot")->values.front() == "true");
    assert(project::closest_config_key("simulate.duraton")
        == std::optional<std::string_view> { "simulate.duration" });
}

// Lists the command gives add to the configured lists, configured values
// first; a generic of the same name replaces its configured value.
void test_list_settings_add()
{
    project::Configuration configuration;
    const auto workspace = origin(project::ConfigLayer::workspace);
    assert(!configuration.set("compile.defines", { "SIM" }, workspace));
    assert(!configuration.set("elaborate.search_libraries", { "models" },
        workspace));
    assert(!configuration.set("elaborate.generics", { "W=1", "D=2" },
        workspace));
    const auto parse = [](std::vector<const char*> arguments) {
        arguments.insert(arguments.begin(), "fsim");
        fsim::diagnostic::Engine diagnostics;
        auto invocation = fsim::cli::parse_arguments(
            static_cast<int>(arguments.size()), arguments.data(), diagnostics);
        assert(invocation && !diagnostics.has_error());
        return *invocation;
    };

    auto compile = parse({ "compile", "-D", "DEBUG", "x.sv" });
    compile.configuration
        = std::make_shared<project::Configuration>(configuration);
    fsim::cli::apply_configuration_defaults(configuration, compile);
    fsim::diagnostic::Engine diagnostics;
    const auto sources = fsim::cli::make_source_set(
        compile, project::Language::system_verilog, diagnostics);
    assert(sources);
    assert((sources->defines == std::vector<std::string> { "SIM", "DEBUG" }));

    auto elaborate = parse({ "elaborate", "--search-library", "cells",
        "--generic", "W=3", "top" });
    fsim::cli::apply_configuration_defaults(configuration, elaborate);
    assert((elaborate.search_libraries
        == std::vector<std::string> { "models", "cells" }));
    assert((elaborate.generics
        == std::vector<std::pair<std::string, std::string>> {
            { "D", "2" }, { "W", "3" } }));
}

void test_keyed_entries()
{
    project::Configuration configuration;
    const auto session = origin(project::ConfigLayer::session);
    project::ConfigChange change {};
    std::string id;
    project::CoverageExclusionEntry exclusion;
    exclusion.hierarchy = "top.dut*";
    exclusion.metric = "statement";
    exclusion.reason = "unused";
    assert(!configuration.put_exclusion(
        std::nullopt, exclusion, session, change, id));
    assert(change == project::ConfigChange::added && id == "x1");
    exclusion.reason = "still unused";
    assert(!configuration.put_exclusion(
        std::string { "x1" }, exclusion, session, change, id));
    assert(change == project::ConfigChange::replaced && id == "x1");
    assert(configuration.exclusions().size() == 1U);
    assert(configuration.exclusions().front().entry.reason == "still unused");
    exclusion.reason.clear();
    assert(configuration.put_exclusion(
        std::string { "x2" }, exclusion, session, change, id));
    assert(configuration.exclusions().size() == 1U);
    assert(configuration.remove_exclusion("x1"));
    assert(!configuration.remove_exclusion("x1"));

    project::CoverageFsmHintEntry hint;
    hint.instance = "top.ctrl";
    hint.current_state = "state";
    assert(!configuration.put_fsm_hint(hint, session, change));
    hint.next_state = "next_state";
    assert(!configuration.put_fsm_hint(hint, session, change));
    assert(change == project::ConfigChange::replaced);
    hint.current_state = "mode";
    assert(!configuration.put_fsm_hint(hint, session, change));
    assert(configuration.fsm_hints().size() == 2U);
    assert(configuration.remove_fsm_hints("top.ctrl", "state") == 1U);
    assert(configuration.remove_fsm_hints("top.ctrl", std::nullopt) == 1U);
    assert(configuration.fsm_hints().empty());

    project::Binding binding;
    binding.instance = "top.u0";
    binding.target = "vhdl:work.leaf";
    assert(!configuration.put_binding(binding, session, change));
    binding.resolver = "std_logic";
    assert(!configuration.put_binding(binding, session, change));
    assert(change == project::ConfigChange::replaced);
    assert(configuration.bindings().size() == 1U);

    project::Config config;
    assert(!configuration.set("elaborate.time_resolution", { "1ps" }, session));
    configuration.apply_elaboration_settings(config);
    assert(config.coverage.exclusions.empty());
    assert(config.bindings.size() == 1U);
    assert(config.bindings.front().resolver == "std_logic");
    assert(config.project.time_resolution == "1ps");
    assert(configuration.remove_binding("top.u0"));
    configuration.clear_bindings();
}

bool tcl_available()
{
    const auto result = invoke({ "--no-config", "tcl", "-c", "puts ok" });
    if (result.status == 0) {
        return true;
    }
    assert(contains(result.error, "FSIM-TCL-0001"));
    return false;
}

void test_scripts_set_command_defaults(const bool tcl)
{
    Directory directory;
    const ScopedEnvironment user { "XDG_CONFIG_HOME",
        fsim::support::path_to_utf8(directory.path / "user").c_str() };
    const ScopedEnvironment appdata { "APPDATA",
        fsim::support::path_to_utf8(directory.path / "user").c_str() };
    const ScopedEnvironment enabled { "FSIM_NO_CONFIG", nullptr };
    write_file("a.vhd",
        "entity a is end entity;\n"
        "architecture rtl of a is begin\n"
        "  process begin report \"CONFIGURED\"; wait; end process;\n"
        "end architecture;\n");
    write_file(directory.path / "user" / "fsim" / "fsim.tcl",
        "fsim::config set compile.library userlib\n"
        "fsim::config set compile.vhdl.standard 2008\n");
    write_file("fsim.tcl",
        "config set compile.library models\n"
        "config set elaborate.search_libraries {models}\n");
    if (!tcl) {
        const auto failed = expect({ "compile", "a.vhd" }, false);
        assert(contains(failed.error, "FSIM-TCL-CFG-002"));
        expect({ "--no-config", "compile", "a.vhd" });
        return;
    }

    // The workspace script wins over the per-user script.
    expect({ "compile", "-q", "a.vhd" });
    assert(fs::exists(".fsim/libraries/models"));
    assert(!fs::exists(".fsim/libraries/userlib"));
    expect({ "elaborate", "-q", "a(rtl)" });
    assert(contains(expect({ "simulate" }).output, "CONFIGURED"));

    // An explicit option wins over the default; the opt-outs skip both.
    expect({ "compile", "-q", "--library", "explicit", "a.vhd" });
    assert(fs::exists(".fsim/libraries/explicit"));
    expect({ "--no-config", "compile", "-q", "a.vhd" });
    assert(fs::exists(".fsim/libraries/work"));
    {
        const ScopedEnvironment disabled { "FSIM_NO_CONFIG", "1" };
        fs::remove_all(".fsim/libraries/work");
        expect({ "compile", "-q", "a.vhd" });
        assert(fs::exists(".fsim/libraries/work"));
    }

    // The session sees both layers and where each value came from.
    const auto listed = expect({ "tcl", "-c",
        "puts [dict get [fsim::config list compile.*] compile.library]" });
    assert(contains(listed.output, "value models"));
    assert(contains(listed.output, "layer workspace"));
    assert(contains(listed.output, "fsim.tcl:1"));
    const auto standard = expect(
        { "tcl", "-c", "puts [fsim::config get compile.vhdl.standard]" });
    assert(contains(standard.output, "2008"));
    const auto compiled = expect({ "tcl", "-c",
        "puts [dict get [fsim::compile a.vhd] library]" });
    assert(contains(compiled.output, "models"));

    // Keyed entries can be added, replaced, shown and removed.
    const auto entries = expect({ "tcl", "-c",
        "puts [dict get [fsim::coverage exclude add -name skip "
        "-hierarchy a* -metric statement -reason first] change]\n"
        "puts [dict get [fsim::coverage exclude add -name skip "
        "-hierarchy a* -metric statement -reason second] change]\n"
        "puts [dict get [fsim::coverage exclude show skip] reason]\n"
        "fsim::coverage exclude remove skip\n"
        "puts [llength [fsim::coverage exclude list]]\n"
        "puts [fsim::bind add top.u0 -resolver std_logic]\n"
        "puts [fsim::bind add top.u0 -resolver sv_wire]\n"
        "puts [dict get [fsim::bind show top.u0] resolver]\n"
        "fsim::bind clear\n"
        "puts [llength [fsim::bind list]]\n"
        "puts [fsim::coverage fsm add -instance top.c -current state]\n"
        "puts [fsim::coverage fsm remove -instance top.c]\n"
        "puts [fsim::config unset compile.library]\n"
        "puts [fsim::config get compile.library]\n" });
    assert(contains(entries.output,
        "added\nreplaced\nsecond\n0\nadded\nreplaced\nsv_wire\n0\nadded\n1\n"));
    // The session layer had no value; the workspace value stays.
    assert(contains(entries.output, "0\nmodels\n"));
    const auto removal = invoke({ "tcl", "-c", "fsim::bind remove top.none" });
    assert(removal.status != 0);
    assert(contains(removal.error, "no binding for instance 'top.none'"));

    // A failing script stops every command and names itself.
    write_file("fsim.tcl", "config set compile.libary models\n");
    const auto failed = expect({ "compile", "a.vhd" }, false);
    assert(contains(failed.error, "FSIM-TCL-CFG-001"));
    assert(contains(failed.error, "fsim.tcl"));
    assert(contains(failed.error, "did you mean 'compile.library'"));
    expect({ "--no-config", "compile", "-q", "a.vhd" });
}

void test_scripts_set_coverage_settings(const bool tcl)
{
    if (!tcl) {
        return;
    }
    Directory directory;
    const ScopedEnvironment user { "XDG_CONFIG_HOME",
        fsim::support::path_to_utf8(directory.path / "user").c_str() };
    const ScopedEnvironment enabled { "FSIM_NO_CONFIG", nullptr };
    write_file("a.vhd",
        "entity a is end entity;\n"
        "architecture rtl of a is begin\n"
        "  process begin report \"COVERED\"; wait; end process;\n"
        "end architecture;\n");
    write_file("fsim.tcl",
        "fsim::config set compile.code_coverage true\n"
        "fsim::config set elaborate.code_coverage true\n"
        "fsim::config set simulate.coverage_output cov.fsimcov\n"
        "fsim::coverage exclude add -name all -hierarchy * -metric statement "
        "-reason configured\n");
    expect({ "compile", "-q", "a.vhd" });
    expect({ "elaborate", "-q", "a(rtl)" });
    const auto unchanged = expect({ "simulate" });
    assert(!contains(unchanged.error, "FSIM-TCL-CFG-003"));
    assert(fs::exists("cov.fsimcov"));
    const auto report = expect({ "coverage", "report", "cov.fsimcov" });
    assert(contains(report.output, "configured"));

    // The snapshot records the settings it was elaborated with.
    const auto info = expect({ "snapshot", "info" });
    assert(contains(info.output, "coverage.exclude[all]"));
    assert(contains(info.output, "reason=configured"));
    assert(contains(info.output, "elaborate.code_coverage true"));
    assert(contains(info.output, "fsim.tcl:4"));
    const auto settings = expect({ "tcl", "-c",
        "puts [llength [fsim::snapshot settings default]]" });
    assert(contains(settings.output, "2\n"));

    // A changed setting takes effect at the next elaborate; until then
    // simulate warns and names it.
    write_file("fsim.tcl",
        "fsim::config set compile.code_coverage true\n"
        "fsim::config set elaborate.code_coverage true\n"
        "fsim::config set simulate.coverage_output cov.fsimcov\n");
    const auto changed = expect({ "simulate" });
    assert(contains(changed.error, "FSIM-TCL-CFG-003"));
    assert(contains(changed.error, "coverage.exclude[all]"));
    assert(!contains(changed.error, "elaborate.code_coverage"));
    expect({ "elaborate", "-q", "a(rtl)" });
    assert(!contains(expect({ "simulate" }).error, "FSIM-TCL-CFG-003"));
    // Without the scripts there is nothing to compare.
    write_file("fsim.tcl", "fsim::config set compile.code_coverage true\n");
    assert(!contains(
        expect({ "--no-config", "simulate" }).error, "FSIM-TCL-CFG-003"));
    const auto bare = expect({ "snapshot", "info" });
    assert(!contains(bare.output, "coverage.exclude"));
}

} // namespace

int main()
{
    test_settings_and_layers();
    test_list_settings_add();
    test_keyed_entries();
    const bool tcl = tcl_available();
    test_scripts_set_command_defaults(tcl);
    test_scripts_set_coverage_settings(tcl);
    return 0;
}
