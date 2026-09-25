// SPDX-License-Identifier: Apache-2.0
#include "fsim/cli/driver.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    fsim::cli::Invocation parse(std::initializer_list<const char*> arguments)
    {
        std::vector<const char*> argv { "fsim" };
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        fsim::diagnostic::Engine diagnostics;
        auto result = fsim::cli::parse_arguments(
            static_cast<int>(argv.size()), argv.data(), diagnostics);
        if (!result) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        assert(result && !diagnostics.has_error());
        return std::move(*result);
    }

    void reject(std::initializer_list<const char*> arguments)
    {
        std::vector<const char*> argv { "fsim" };
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        fsim::diagnostic::Engine diagnostics;
        assert(!fsim::cli::parse_arguments(
            static_cast<int>(argv.size()), argv.data(), diagnostics));
        assert(diagnostics.has_error());
    }

    void test_phase_arguments()
    {
        const auto compile = parse({ "compile", "first.sv", "second.sv" });
        assert(compile.command == fsim::cli::Command::compile);
        assert(compile.library == "work");
        assert(compile.files.size() == 2U);
        assert(!compile.language && !compile.standard);
        assert(!compile.artifact_output && compile.objects.empty());
        assert(compile.verbosity == fsim::cli::Verbosity::normal);
        const auto quiet = parse({ "compile", "--verbosity", "quiet",
            "--search-library", "vendor", "--library", "models", "top.vhd" });
        assert(quiet.verbosity == fsim::cli::Verbosity::quiet);
        assert(quiet.library == "models");
        assert(quiet.search_libraries == std::vector<std::string> { "vendor" });
        assert(parse({ "compile", "-q", "first.sv" }).verbosity
            == fsim::cli::Verbosity::quiet);
        assert(parse({ "compile", "-v", "first.sv" }).verbosity
            == fsim::cli::Verbosity::verbose);
        const auto verilog_alias = parse({ "compile", "--lang",
            "verilog-2001-noconfig", "older.v" });
        assert(verilog_alias.language == fsim::project::Language::verilog);
        assert(verilog_alias.standard == "2001-noconfig");
        assert(parse({ "compile", "--lang", "sv-2009", "first.sv" }).standard
            == "2009");
        assert(parse({ "compile", "--lang", "vhdl-93", "top.vhd" }).standard
            == "1993");
        assert(parse({ "compile", "--standard", "2012", "--lang", "sv-2009",
                   "first.sv" }).standard
            == "2012");
        assert(parse({ "compile", "--lang", "sv-2009", "--standard", "2012",
                   "first.sv" }).standard
            == "2012");
        assert(!parse({ "compile", "--lang", "sv-2009", "--lang", "sv",
                    "first.sv" }).standard);
        assert(parse({ "compile", "--standard", "2012", "--lang", "sv-2009",
                   "--lang", "sv", "first.sv" }).standard
            == "2012");

        const auto single = parse({ "elaborate", "sv:work.tb" });
        assert(single.snapshot == "default");
        assert(single.top == "sv:work.tb");
        assert(single.tops.size() == 1U);
        assert(!single.artifact_output && single.objects.empty());
        for (const auto* target : { "tb", "work.tb", "vhdl:tb",
                 "systemverilog:work.tb", "systemc:tb" }) {
            assert(parse({ "elaborate", target }).top == target);
        }
        const auto multiple = parse({ "elaborate", "--snapshot", "nightly-1",
            "--top", "sv:work.producer", "vhdl:vendor.consumer(rtl)",
            "--verbosity=verbose" });
        assert(multiple.snapshot == "nightly-1");
        assert(multiple.verbosity == fsim::cli::Verbosity::verbose);
        assert(!multiple.top && multiple.tops.size() == 2U);
        assert(multiple.tops[0].alias == "producer");
        assert(multiple.tops[1].alias == "consumer");
        const auto aliases = parse({ "elaborate", "first=sv:one.tb",
            "second=sv:two.tb" });
        assert(aliases.tops[0].alias == "first");
        assert(aliases.tops[1].alias == "second");
        reject({ "elaborate", "sv:one.tb", "sv:two.tb" });
        reject({ "elaborate" });

        const auto simulate = parse({ "simulate" });
        assert(simulate.snapshot == "default" && !simulate.design);
        const auto named = parse({ "simulate", "--snapshot=nightly-1",
            "--engine", "interpreter", "--duration", "10ns", "+COUNT=2" });
        assert(named.snapshot == "nightly-1");
        assert(named.plusargs == std::vector<std::string> { "+COUNT=2" });
        assert(parse({ "debug", "--snapshot", "nightly-1" }).snapshot
            == "nightly-1");
        const auto tcl = parse({ "tcl", "--snapshot", "nightly-1",
            "-c", "puts ready" });
        assert(tcl.snapshot == "nightly-1");
        assert(tcl.tcl_commands == std::vector<std::string> { "puts ready" });
        const auto debug_script = parse({ "debug", "debugger.tcl", "fixture-arg" });
        assert(debug_script.tcl_script
            == std::filesystem::path { "debugger.tcl" });
        assert(debug_script.tcl_arguments
            == std::vector<std::string> { "fixture-arg" });
        reject({ "simulate", "source.sv" });
        reject({ "compile", "--snapshot", "default", "source.sv" });
        reject({ "simulate", "--snapshot", "../outside" });
        reject({ "simulate", "--snapshot", "path\\outside" });
        reject({ "simulate", "--snapshot", "COM1" });
        reject({ "simulate", "--snapshot", "nightly.1" });
        reject({ "compile", "--verbosity", "loud", "source.sv" });
        reject({ "compile" });
        reject({ "check" });

        const auto systemc = parse({ "systemc", "compile", "--library",
            "vendor", "-Iinclude", "-DVALUE=1", "-v", "first.cpp", "second.cpp" });
        assert(systemc.command == fsim::cli::Command::systemc_compile);
        assert(systemc.files.size() == 2U && systemc.library == "vendor");
        assert(!systemc.artifact_output);
        const auto linked = parse({ "systemc", "link", "--library", "vendor",
            "--link-library", "pthread" });
        assert(linked.command == fsim::cli::Command::systemc_link);
        assert(linked.objects.empty() && !linked.artifact_output);
        reject({ "systemc", "compile" });
        reject({ "systemc", "link", "first.cpp" });
    }

    void test_library_arguments()
    {
        const auto mapping = parse({ "library", "map", "vendor", "../vendor" });
        assert(mapping.command == fsim::cli::Command::library_map);
        assert(mapping.library_name == "vendor");
        assert(mapping.library_mapping_path == std::filesystem::path { "../vendor" });
        assert(parse({ "library", "list" }).command
            == fsim::cli::Command::library_list);
        assert(parse({ "library", "unmap", "vendor" }).library_name == "vendor");
        const auto objects = parse({ "library", "objects" });
        assert(objects.command == fsim::cli::Command::library_objects);
        assert(!objects.library_name);
        assert(parse({ "library", "objects", "--verbose" }).verbosity
            == fsim::cli::Verbosity::verbose);
        assert(parse({ "library", "map", "vendor", "../vendor", "-q" }).verbosity
            == fsim::cli::Verbosity::quiet);
        assert(parse({ "library", "objects", "vendor" }).library_name == "vendor");
        const auto object = parse({ "library", "delete-object", "vendor", "abc123" });
        assert(object.command == fsim::cli::Command::library_delete_object);
        assert(object.library_name == "vendor" && object.library_object_id == "abc123");
        const auto library = parse({ "library", "delete", "vendor" });
        assert(library.command == fsim::cli::Command::library_delete);
        assert(library.library_name == "vendor");
        reject({ "library", "map", "vendor" });
        reject({ "library", "map", "ieee", "../vendor" });
        reject({ "library", "map", "vendor", "../vendor", "extra" });
        reject({ "library", "list", "vendor" });
        reject({ "library", "objects", "vendor", "extra" });
        reject({ "library", "unmap" });
        reject({ "library", "delete" });
        reject({ "library", "delete-object", "vendor" });
        reject({ "library", "delete-object", "vendor", "../outside" });
    }

    void test_removed_surface_and_preserved_controls()
    {
        reject({ "build" });
        reject({ "run" });
        reject({ "check", "--project", "fsim.toml" });
        reject({ "compile", "--output", "manual.fsimobj", "source.sv" });
        reject({ "elaborate", "--object", "manual.fsimobj", "tb" });
        reject({ "elaborate", "--systemc-plugin", "manual.fsimscplugin", "tb" });
        reject({ "simulate", "--design", "manual.fsimdesign" });
        reject({ "compile", "--map-library", "vendor=../vendor", "source.sv" });
        reject({ "compile", "--export-library", "work=../work", "source.sv" });

        const auto coverage = parse({ "coverage", "merge", "first.fsimcov",
            "--output", "combined.fsimcov" });
        assert(coverage.command == fsim::cli::Command::coverage_merge);
        assert(coverage.artifact_output && coverage.artifact_output->is_absolute());
        const auto aot = parse({ "elaborate", "tb", "--aot", "--aot-scope",
            "all", "--code-coverage", "--sdf", "timing.sdf", "--seed", "7",
            "--trace", "waves.fst", "--trace-format", "fst", "--jobs", "12" });
        assert(aot.aot == true && aot.aot_scope == fsim::cli::AotScope::all);
        assert(aot.code_coverage == true && aot.seed == 7U && aot.jobs == 12U);
        assert(aot.sdf_files.size() == 1U && aot.sdf_files[0].is_absolute());
        assert(aot.trace_format == fsim::project::TraceFormat::fst);
        reject({ "elaborate", "tb", "--aot-scope", "all" });
        reject({ "simulate", "--engine", "interpreter", "--compiled-processes", "all" });

        std::ostringstream output;
        std::ostringstream error;
        const char* arguments[] { "fsim", "--help" };
        assert(fsim::cli::run(2, arguments, {}, output, error) == 0);
        assert(error.str().empty());
        assert(output.str().find(".fsim/libraries.toml") != std::string::npos);
        assert(output.str().find("--snapshot NAME") != std::string::npos);
        assert(output.str().find("--project") == std::string::npos);
        assert(output.str().find("--object") == std::string::npos);
        assert(output.str().find("--design") == std::string::npos);
    }

    class TemporaryWorkspace {
    public:
        TemporaryWorkspace()
            : m_previous(std::filesystem::current_path())
            , m_directory(std::filesystem::temp_directory_path()
                  / ("fsim-workspace-cli-"
                      + std::to_string(std::chrono::steady_clock::now()
                              .time_since_epoch().count())))
        {
            assert(std::filesystem::create_directory(m_directory));
            std::filesystem::current_path(m_directory);
            m_directory = std::filesystem::current_path();
        }

        ~TemporaryWorkspace()
        {
            std::error_code error;
            std::filesystem::current_path(m_previous, error);
            std::filesystem::remove_all(m_directory, error);
        }

        const std::filesystem::path& directory() const { return m_directory; }

    private:
        std::filesystem::path m_previous;
        std::filesystem::path m_directory;
    };

    void test_workspace_configuration()
    {
        TemporaryWorkspace workspace;
        {
            std::ofstream manifest("fsim.toml");
            manifest << "invalid legacy manifest that must never be loaded\n";
            std::ofstream source("top.sv");
            source << "module tb; endmodule\n";
            std::ofstream systemc("top.cpp");
            systemc << "int object;\n";
        }
        fsim::cli::Services services;
        std::size_t calls = 0;
        const auto inspect = [&](const fsim::cli::Invocation& invocation,
                                 const fsim::project::Config& config,
                                 fsim::diagnostic::Engine&, std::ostream&,
                                 std::ostream&) {
            ++calls;
            assert(config.base_directory == workspace.directory());
            assert(config.manifest_path == "<workspace>");
            assert(config.build.cache_path == workspace.directory() / ".fsim" / "cache");
            if (invocation.command == fsim::cli::Command::compile) {
                assert(config.source_sets.size() == 1U);
                assert(config.source_sets[0].language == fsim::project::Language::system_verilog);
                assert(config.source_sets[0].standard == "2017");
                assert(config.source_sets[0].files
                    == std::vector { workspace.directory() / "top.sv" });
            } else if (invocation.command == fsim::cli::Command::systemc_compile) {
                assert(config.source_sets.size() == 1U);
                assert(config.source_sets[0].language == fsim::project::Language::systemc);
            } else {
                assert(config.source_sets.empty());
                assert(invocation.snapshot == "default");
            }
            return 0;
        };
        services.compile = inspect;
        services.simulate = inspect;
        services.debug = inspect;
        services.tcl = inspect;
        services.systemc_compile = inspect;
        services.library_list = inspect;
        const auto invoke = [&](std::initializer_list<const char*> arguments) {
            std::vector<const char*> argv { "fsim" };
            argv.insert(argv.end(), arguments.begin(), arguments.end());
            std::ostringstream output;
            std::ostringstream error;
            const auto status = fsim::cli::run(static_cast<int>(argv.size()),
                argv.data(), services, output, error);
            if (status != 0) {
                std::cerr << error.str();
            }
            assert(status == 0 && error.str().empty());
        };
        invoke({ "compile", "--standard", "17", "top.sv" });
        invoke({ "simulate" });
        invoke({ "debug" });
        invoke({ "tcl", "-c", "puts ready" });
        invoke({ "systemc", "compile", "top.cpp" });
        invoke({ "library", "list" });
        assert(calls == 6U);
        assert(!std::filesystem::exists(".fsim"));
    }

} // namespace

int main()
{
    test_phase_arguments();
    test_library_arguments();
    test_removed_surface_and_preserved_controls();
    test_workspace_configuration();
}
