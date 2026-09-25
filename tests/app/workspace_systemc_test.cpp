// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_workspace_store.hpp"
#include "../../src/app/application_workspace_systemc.hpp"

#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/systemc/incremental.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    namespace workspace = fsim::app::workspace;

    struct TemporaryWorkspace {
        std::filesystem::path previous { std::filesystem::current_path() };
        std::filesystem::path path {
            std::filesystem::temp_directory_path()
                / ("fsim-workspace-systemc-"
                    + std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()))
        };

        TemporaryWorkspace()
        {
            std::filesystem::create_directories(path);
            std::filesystem::current_path(path);
        }

        ~TemporaryWorkspace()
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
            // The native loader deliberately retains mapped plug-ins until
            // process exit. Windows may retain those test files until then.
            std::filesystem::remove_all(path, error);
        }
    };

    struct Capture {
        int status { };
        fsim::diagnostic::Engine diagnostics;
        std::string output;
    };

    void write_file(const std::filesystem::path& path, const std::string& content)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path);
        output << content;
        assert(output.good());
    }

    Capture invoke(
        const fsim::cli::Invocation& invocation,
        const fsim::project::Config& config, const bool expect_success)
    {
        Capture capture;
        std::ostringstream output;
        std::ostringstream error;
        const auto handler = invocation.command == fsim::cli::Command::systemc_compile
            ? fsim::app::application_detail::handle_workspace_systemc_compile
            : fsim::app::application_detail::handle_workspace_systemc_link;
        capture.status = handler(invocation, config, capture.diagnostics, output, error);
        capture.output = output.str();
        if (expect_success && capture.status != 0) {
            fsim::diagnostic::print_text(std::cerr, capture.diagnostics);
        }
        assert((capture.status == 0) == expect_success);
        assert(capture.diagnostics.has_error() != expect_success);
        return capture;
    }

    workspace::LibraryCatalog read_catalog(
        const workspace::Store& store, const std::string& name)
    {
        std::string error;
        auto catalog = store.read_library(name, error);
        if (!catalog) {
            std::cerr << error << '\n';
        }
        assert(catalog);
        return std::move(*catalog);
    }

    std::vector<workspace::ArtifactRecord> records_of_kind(
        const workspace::LibraryCatalog& catalog, const workspace::ArtifactKind kind)
    {
        std::vector<workspace::ArtifactRecord> result;
        for (const auto& record : catalog.artifacts) {
            if (record.kind == kind) {
                result.push_back(record);
            }
        }
        return result;
    }

    std::vector<std::string> factory_names(const workspace::LibraryCatalog& catalog)
    {
        std::vector<std::string> result;
        for (const auto& record : records_of_kind(
                 catalog, workspace::ArtifactKind::SystemCPlugin)) {
            for (const auto& unit : record.units) {
                assert(unit.unit.language == "systemc");
                assert(unit.unit.kind == "module");
                result.push_back(unit.unit.name);
            }
        }
        std::ranges::sort(result);
        return result;
    }

    std::string module_source(const std::string& alias)
    {
        return "#include \"fsim/systemc.hpp\"\n"
               "extern int fsim_workspace_value();\n"
               "SC_MODULE(WorkspaceModule) {\n"
               "    sc_core::sc_signal<sc_dt::sc_logic> value{\"value\", sc_dt::SC_LOGIC_0};\n"
               "    void run() {\n"
               "        value.write(fsim_workspace_value() == 7 ? sc_dt::SC_LOGIC_1 : sc_dt::SC_LOGIC_0);\n"
               "    }\n"
               "    SC_CTOR(WorkspaceModule) { SC_THREAD(run); }\n"
               "};\n"
               "SC_FSIM_EXPORT_AS(WorkspaceModule, \""
            + alias + "\");\n";
    }

    void run_workspace_command(const std::vector<std::string>& arguments)
    {
        std::vector<const char*> raw;
        for (const auto& argument : arguments) {
            raw.push_back(argument.c_str());
        }
        std::istringstream input;
        std::ostringstream output;
        std::ostringstream error;
        const auto status = fsim::cli::run(static_cast<int>(raw.size()), raw.data(),
            fsim::app::make_cli_services(input), output, error);
        if (status != 0) {
            std::cerr << error.str();
        }
        assert(status == 0);
        assert(error.str().empty());
    }

    std::filesystem::path publish_snapshots(const workspace::Store& store)
    {
        run_workspace_command({ "fsim", "elaborate", "--top", "vendor.renamed" });
        run_workspace_command({ "fsim", "elaborate", "--top", "systemc:vendor.renamed",
            "--snapshot", "named" });
        std::string error;
        const auto snapshot = store.read_snapshot("default", error);
        if (!snapshot) {
            std::cerr << error << '\n';
        }
        assert(snapshot);
        return snapshot->path;
    }

    void verify_snapshot(const std::filesystem::path& path)
    {
        fsim::diagnostic::Engine diagnostics;
        auto built = fsim::app::load_design_artifact(path, diagnostics);
        if (!built) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        assert(built && !diagnostics.has_error());
        assert(built->systemc_plugins.size() == 1);
        assert(built->design.systemc_instances().size() == 1);
        const auto& signals = built->design.systemc_instances().front().internal_signals;
        assert(signals.size() == 1);
        const auto value = signals.front().signal;
        fsim::app::Simulation simulation {
            std::move(*built), 1000, fsim::app::SimulationEngine::interpreter
        };
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::completed);
        assert(result.callbacks_executed != 0);
        assert(simulation.read_signal(value).to_msb_string() == "1");
    }

} // namespace

int main()
{
    TemporaryWorkspace temporary;
    workspace::Store store(temporary.path);
    fsim::project::Config config;
    config.base_directory = temporary.path;
    config.project.name = "workspace-systemc";
    config.build.cache_path = temporary.path / ".fsim" / "cache";
    config.systemc.include_directories = { temporary.path / "include" };
    config.systemc.defines = { "FSIM_WORKSPACE_VALUE=7" };
#if defined(FSIM_TEST_ASAN_ENABLED)
    config.systemc.link_options = { "-fsanitize=address,undefined" };
#endif
    const auto helper = temporary.path / "helper.cpp";
    const auto first = temporary.path / "first.cpp";
    const auto second = temporary.path / "second.cpp";
    write_file(temporary.path / "include" / "workspace_value.hpp",
        "#ifndef FSIM_WORKSPACE_VALUE\n#error missing workspace define\n#endif\n");
    write_file(helper, "#include \"workspace_value.hpp\"\n"
                       "int fsim_workspace_value() { return FSIM_WORKSPACE_VALUE; }\n");
    write_file(first, module_source("first"));
    write_file(second,
        "#include \"fsim/systemc.hpp\"\n"
        "SC_MODULE(OtherWorkspaceModule) { SC_CTOR(OtherWorkspaceModule) {} };\n"
        "SC_FSIM_EXPORT_AS(OtherWorkspaceModule, \"second\");\n");

    fsim::cli::Invocation compile;
    compile.command = fsim::cli::Command::systemc_compile;
    compile.library = "vendor";
    compile.files = { "helper.cpp" };
    compile.verbosity = fsim::cli::Verbosity::quiet;
    assert(invoke(compile, config, true).output.empty());
    auto catalog = read_catalog(store, "vendor");
    assert(catalog.location.directory == temporary.path / ".fsim" / "libraries" / "vendor");
    assert(records_of_kind(catalog, workspace::ArtifactKind::SystemCObject).size() == 1);
    assert(factory_names(catalog).empty());

    fsim::cli::Invocation link;
    link.command = fsim::cli::Command::systemc_link;
    link.library = "vendor";
    invoke(link, config, false);
    assert(records_of_kind(read_catalog(store, "vendor"),
        workspace::ArtifactKind::SystemCObject).size() == 1);

    compile.files = { "first.cpp", "second.cpp" };
    compile.verbosity = fsim::cli::Verbosity::normal;
    const auto compiled = invoke(compile, config, true);
    assert(compiled.output.find("compiled 2 SystemC translation unit(s)") != std::string::npos);
    link.verbosity = fsim::cli::Verbosity::verbose;
    const auto linked = invoke(link, config, true);
    assert(linked.output.find("systemc:vendor.first") != std::string::npos);
    catalog = read_catalog(store, "vendor");
    assert(records_of_kind(catalog, workspace::ArtifactKind::SystemCObject).size() == 3);
    assert((factory_names(catalog) == std::vector<std::string> { "first", "second" }));

    const auto first_plugin = records_of_kind(catalog, workspace::ArtifactKind::SystemCPlugin).front();
    write_file(first, module_source("renamed"));
    compile.files = { "./first.cpp" };
    compile.verbosity = fsim::cli::Verbosity::verbose;
    assert(invoke(compile, config, true).output.find("define: FSIM_WORKSPACE_VALUE=7")
        != std::string::npos);
    catalog = read_catalog(store, "vendor");
    assert(records_of_kind(catalog, workspace::ArtifactKind::SystemCObject).size() == 3);
    assert(records_of_kind(catalog, workspace::ArtifactKind::SystemCPlugin).empty());
    assert(factory_names(catalog).empty());
    link.verbosity = fsim::cli::Verbosity::quiet;
    assert(invoke(link, config, true).output.empty());
    catalog = read_catalog(store, "vendor");
    assert((factory_names(catalog) == std::vector<std::string> { "renamed", "second" }));
    const auto replacement = records_of_kind(catalog, workspace::ArtifactKind::SystemCPlugin).front();
    assert(first_plugin.id != replacement.id && first_plugin.path != replacement.path);
    const auto snapshot = publish_snapshots(store);

    auto broken_link_config = config;
    broken_link_config.systemc.libraries.push_back("fsim_workspace_missing_native_library_for_test");
    invoke(link, broken_link_config, false);
    catalog = read_catalog(store, "vendor");
    assert(records_of_kind(catalog, workspace::ArtifactKind::SystemCPlugin).front().id == replacement.id);

    const auto objects_before_failure = records_of_kind(catalog, workspace::ArtifactKind::SystemCObject);
    write_file(helper, "int fsim_workspace_value() { return 22; }\n");
    write_file(temporary.path / "broken.cpp", "this is not C++\n");
    compile.files = { "helper.cpp", "broken.cpp" };
    invoke(compile, config, false);
    catalog = read_catalog(store, "vendor");
    const auto objects_after_failure = records_of_kind(catalog, workspace::ArtifactKind::SystemCObject);
    assert(objects_before_failure.size() == objects_after_failure.size());
    for (const auto& before : objects_before_failure) {
        assert(std::ranges::any_of(objects_after_failure, [&](const auto& after) {
            return before.id == after.id && before.fingerprint == after.fingerprint;
        }));
    }
    assert(records_of_kind(catalog, workspace::ArtifactKind::SystemCPlugin).front().id == replacement.id);

    compile.files = { "helper.cpp" };
    invoke(compile, config, true);
    assert(factory_names(read_catalog(store, "vendor")).empty());
    std::filesystem::remove(helper);
    std::filesystem::remove(first);
    std::filesystem::remove(second);
    run_workspace_command({ "fsim", "simulate", "--engine", "interpreter" });
    run_workspace_command({ "fsim", "simulate", "--snapshot", "named", "--engine", "interpreter" });
    verify_snapshot(snapshot);
}
