// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_workspace_store.hpp"

#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace workspace = fsim::app::workspace;
namespace fs = std::filesystem;

class Directory final {
public:
    Directory()
        : previous_(fs::current_path())
    {
        static std::atomic_uint64_t sequence { 0U };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("fsim-workspace-application-"
            + std::to_string(stamp) + "-" + std::to_string(sequence++));
        fs::create_directories(path);
        fs::current_path(path);
    }

    ~Directory()
    {
        std::error_code error;
        fs::current_path(previous_, error);
        clean(path);
    }

    static void clean(const fs::path& root)
    {
        std::error_code error;
        fs::recursive_directory_iterator iterator { root, error };
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            const auto status = iterator->symlink_status(error);
            if (!error && !fs::is_symlink(status)) {
                fs::permissions(iterator->path(), fs::perms::owner_all,
                    fs::perm_options::add, error);
            }
            if (!error)
                iterator.increment(error);
        }
        error.clear();
        fs::remove_all(root, error);
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

Capture invoke(std::vector<std::string> arguments, const bool success = true)
{
    arguments.insert(arguments.begin(), "fsim");
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments)
        raw.push_back(argument.c_str());
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int status = fsim::cli::run(static_cast<int>(raw.size()), raw.data(),
        fsim::app::make_cli_services(input), output, error);
    if ((status == 0) != success) {
        std::cerr << "Unexpected workspace command result " << status << ":";
        for (const auto& argument : arguments)
            std::cerr << ' ' << argument;
        std::cerr << '\n' << output.str() << error.str();
    }
    assert((status == 0) == success);
    if (!success)
        assert(!error.str().empty());
    return { status, output.str(), error.str() };
}

void write_file(const fs::path& path, const std::string_view text)
{
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
    std::ofstream output { path };
    output << text;
    assert(output.good());
}

std::string module(const std::string_view name, const std::string_view marker)
{
    return "module " + std::string { name } + ";\n"
        + "  initial $display(\"" + std::string { marker } + "\");\n"
        + "endmodule\n";
}

workspace::LibraryCatalog catalog(const workspace::Store& store,
    const std::string_view library = "work")
{
    std::string error;
    auto result = store.read_library(library, error);
    if (!result)
        std::cerr << error << '\n';
    assert(result);
    return std::move(*result);
}

workspace::SnapshotRecord snapshot(const workspace::Store& store,
    const std::string_view name = "default")
{
    std::string error;
    auto result = store.read_snapshot(name, error);
    if (!result)
        std::cerr << error << '\n';
    assert(result);
    return std::move(*result);
}

std::set<std::string> unit_names(const workspace::LibraryCatalog& library)
{
    std::set<std::string> names;
    for (const auto& record : library.artifacts) {
        for (const auto& owned : record.units)
            names.insert(owned.unit.name);
    }
    return names;
}

const workspace::ArtifactRecord& record_for(const workspace::LibraryCatalog& library,
    const std::string_view name)
{
    const auto found = std::ranges::find_if(library.artifacts, [&](const auto& record) {
        return std::ranges::any_of(record.units,
            [&](const auto& owned) { return owned.unit.name == name; });
    });
    assert(found != library.artifacts.end());
    return *found;
}

std::set<std::string> artifact_ids(const workspace::LibraryCatalog& library)
{
    std::set<std::string> ids;
    for (const auto& record : library.artifacts)
        ids.insert(record.id);
    return ids;
}

Capture elaborate(std::vector<std::string> tops, const std::string_view name = "default",
    const bool success = true)
{
    std::vector<std::string> arguments { "elaborate", "--no-aot",
        "--trace-lifecycle", "disabled" };
    if (name != "default") {
        arguments.push_back("--snapshot");
        arguments.emplace_back(name);
    }
    arguments.insert(arguments.end(), tops.begin(), tops.end());
    return invoke(std::move(arguments), success);
}

Capture simulate(const std::string_view name = "default")
{
    std::vector<std::string> arguments { "simulate", "--engine", "interpreter",
        "--trace-lifecycle", "disabled" };
    if (name != "default") {
        arguments.push_back("--snapshot");
        arguments.emplace_back(name);
    }
    return invoke(std::move(arguments));
}

void assert_output(const Capture& capture, const std::string_view marker)
{
    if (capture.output.find(marker) == std::string::npos)
        std::cerr << "Missing output '" << marker << "':\n" << capture.output << capture.error;
    assert(capture.output.find(marker) != std::string::npos);
}

void test_managed_objects_default_and_named_snapshots()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("modules.sv", module("alpha", "WORKSPACE_ALPHA")
        + module("beta", "WORKSPACE_BETA"));
    const auto compiled = invoke({ "compile", "--verbose", "modules.sv" });
    assert_output(compiled, "module work.alpha");
    assert_output(compiled, "module work.beta");
    const auto library = catalog(store);
    assert(library.location.directory == directory.path / ".fsim/libraries/work");
    assert(library.artifacts.size() >= 2U);
    const auto alpha_id = record_for(library, "alpha").id;
    const auto beta_id = record_for(library, "beta").id;
    assert(alpha_id != beta_id);
    for (const auto& record : library.artifacts)
        assert(record.kind == workspace::ArtifactKind::Hdl);
    const auto listed = invoke({ "library", "objects", "--verbose" });
    assert_output(listed, alpha_id);
    assert_output(listed, beta_id);
    assert_output(listed, "work.alpha");

    fs::remove("modules.sv");
    elaborate({ "alpha" });
    const auto first = snapshot(store).path;
    const auto alpha = simulate();
    assert_output(alpha, "WORKSPACE_ALPHA");
    assert(alpha.output.find("WORKSPACE_BETA") == std::string::npos);
    elaborate({ "alpha" });
    const auto replacement = snapshot(store).path;
    assert(replacement != first);
    elaborate({ "beta" }, "alternate");
    assert(snapshot(store).path == replacement);
    const auto alternate_path = snapshot(store, "alternate").path;
    const auto beta = simulate("alternate");
    assert_output(beta, "WORKSPACE_BETA");
    assert(beta.output.find("WORKSPACE_ALPHA") == std::string::npos);

    elaborate({ "missing_top" }, "default", false);
    assert(snapshot(store).path == replacement);
    elaborate({ "missing_top" }, "never_published", false);
    std::string error;
    assert(!store.read_snapshot("never_published", error));
    assert(snapshot(store, "alternate").path == alternate_path);
    elaborate({ "left=alpha", "right=beta" }, "pair");
    const auto pair = simulate("pair");
    assert_output(pair, "WORKSPACE_ALPHA");
    assert_output(pair, "WORKSPACE_BETA");

    invoke({ "library", "delete-object", "work", alpha_id });
    assert(!unit_names(catalog(store)).contains("alpha"));
    assert(unit_names(catalog(store)).contains("beta"));
    elaborate({ "alpha" }, "default", false);
    assert(snapshot(store).path == replacement);
    invoke({ "library", "delete", "work" });
    assert(!fs::exists(directory.path / ".fsim/libraries/work"));
    assert_output(invoke({ "simulate" }), "WORKSPACE_ALPHA");
    assert_output(simulate("alternate"), "WORKSPACE_BETA");
}

void test_recompilation_removes_old_names_and_rolls_back_errors()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("changing.sv", module("old_name", "OLD_NAME")
        + module("removed_name", "REMOVED_NAME"));
    write_file("stable.sv", module("stable", "STABLE_NAME"));
    const auto quiet = invoke({ "compile", "--quiet", "changing.sv", "stable.sv" });
    assert(quiet.output.empty());
    const auto original = catalog(store);
    const auto stable_id = record_for(original, "stable").id;
    write_file("changing.sv", module("renamed", "WORKSPACE_RENAMED"));
    assert(invoke({ "compile", "--quiet", "./changing.sv" }).output.empty());
    const auto replacement = catalog(store);
    assert((unit_names(replacement) == std::set<std::string> { "renamed", "stable" }));
    assert(record_for(replacement, "stable").id == stable_id);
    elaborate({ "old_name" }, "default", false);
    assert(invoke({ "elaborate", "--quiet", "--no-aot", "--trace-lifecycle",
        "disabled", "renamed" }).output.empty());
    const auto published = snapshot(store).path;
    const auto before_failure = artifact_ids(replacement);

    write_file("changing.sv", "module renamed; initial begin this is invalid syntax;\n");
    const auto failed = invoke({ "compile", "--quiet", "changing.sv" }, false);
    assert(failed.output.empty());
    assert(!failed.error.empty());
    assert(artifact_ids(catalog(store)) == before_failure);
    assert(snapshot(store).path == published);
    assert_output(simulate(), "WORKSPACE_RENAMED");
    write_file("duplicate.sv", module("renamed", "DUPLICATE_NAME"));
    invoke({ "compile", "--quiet", "duplicate.sv" }, false);
    assert(artifact_ids(catalog(store)) == before_failure);

    write_file("changing.sv", "// All previous definitions were removed.\n");
    invoke({ "compile", "--quiet", "changing.sv" });
    const auto removed = catalog(store);
    assert((unit_names(removed) == std::set<std::string> { "stable" }));
    assert(record_for(removed, "stable").id == stable_id);
    assert_output(simulate(), "WORKSPACE_RENAMED");
}

void test_compiled_packages_and_stale_consumers()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("package.sv", "package stored_pkg; parameter int VALUE = 17; endpackage\n");
    invoke({ "compile", "--quiet", "package.sv" });
    fs::remove("package.sv");
    write_file("consumer.sv", R"(
module package_top;
  import stored_pkg::*;
  initial $display("WORKSPACE_VALUE=%0d", VALUE);
endmodule
)");
    invoke({ "compile", "--quiet", "consumer.sv" });
    const auto imported = catalog(store);
    const auto& consumer = record_for(imported, "package_top");
    assert(std::ranges::any_of(consumer.dependencies, [](const auto& dependency) {
        return dependency.library == "work" && dependency.unit.name == "stored_pkg";
    }));
    elaborate({ "package_top" });
    const auto previous = snapshot(store).path;
    assert_output(simulate(), "WORKSPACE_VALUE=17");

    write_file("package.sv", "package stored_pkg; parameter int VALUE = 29; endpackage\n");
    invoke({ "compile", "--quiet", "package.sv" });
    fs::remove("package.sv");
    const auto stale = elaborate({ "package_top" }, "default", false);
    assert(stale.error.find("FSIM-WS-002") != std::string::npos);
    assert(snapshot(store).path == previous);
    assert_output(simulate(), "WORKSPACE_VALUE=17");
    invoke({ "compile", "--quiet", "consumer.sv" });
    elaborate({ "package_top" });
    assert_output(simulate(), "WORKSPACE_VALUE=29");

    const auto changed = catalog(store);
    const auto package_id = record_for(changed, "stored_pkg").id;
    invoke({ "library", "delete-object", "work", package_id });
    const auto deleted = elaborate({ "package_top" }, "default", false);
    assert(deleted.error.find("FSIM-WS-002") != std::string::npos);
    assert_output(simulate(), "WORKSPACE_VALUE=29");
}

void test_external_library_mapping_and_current_directory()
{
    Directory directory;
    const auto producer = directory.path / "producer";
    const auto consumer = directory.path / "consumer";
    fs::create_directories(producer);
    fs::create_directories(consumer);
    fs::current_path(producer);
    write_file("mapped.sv", module("mapped_top", "WORKSPACE_MAPPED"));
    invoke({ "compile", "--library", "support", "--quiet", "mapped.sv" });
    workspace::Store producer_store { producer };
    const auto external = catalog(producer_store, "support").location.directory;
    fs::remove("mapped.sv");
    write_file(external / "unrelated.txt", "Preserve this external library owner's file.\n");

    fs::current_path(consumer);
    workspace::Store consumer_store { consumer };
    const auto mapping = fsim::support::path_to_utf8(fs::relative(external, consumer));
    invoke({ "library", "map", "wrong_name", mapping }, false);
    invoke({ "library", "map", "support", mapping });
    const auto listed = invoke({ "library", "list" });
    assert_output(listed, "support");
    assert_output(listed, "mapped");
    invoke({ "library", "unmap", "support" });
    assert(fs::exists(external / "library.sqlite3"));
    invoke({ "library", "map", "support", mapping });
    elaborate({ "mapped_top" });
    assert_output(simulate(), "WORKSPACE_MAPPED");

    const auto nested = consumer / "nested";
    fs::create_directory(nested);
    fs::current_path(nested);
    assert(invoke({ "library", "list" }).output.empty());
    elaborate({ "mapped_top" }, "default", false);
    invoke({ "simulate", "--engine", "interpreter" }, false);
    assert(!fs::exists(nested / ".fsim"));
    fs::current_path(consumer);

    invoke({ "library", "delete", "support" });
    assert(fs::exists(external / "unrelated.txt"));
    assert(!fs::exists(external / "library.sqlite3"));
    assert(invoke({ "library", "list" }).output.empty());
    assert(unit_names(catalog(consumer_store, "support")).empty());
    assert_output(invoke({ "simulate" }), "WORKSPACE_MAPPED");
}

void test_language_qualification_and_multiple_roots()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("common.sv", module("common_top", "WORKSPACE_SYSTEMVERILOG")
        + module("unique_top", "WORKSPACE_UNIQUE"));
    invoke({ "compile", "--quiet", "common.sv" });
    write_file("common.vhd", R"(
entity common_top is end entity;
architecture rtl of common_top is
begin
  process begin
    report "WORKSPACE_VHDL";
    wait;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--standard", "2008", "common.vhd" });
    fs::remove("common.sv");
    fs::remove("common.vhd");
    const auto ambiguous = elaborate({ "common_top" }, "default", false);
    assert(ambiguous.error.find("ambiguous") != std::string::npos);
    assert(ambiguous.error.find("sv:work.common_top") != std::string::npos);
    assert(ambiguous.error.find("vhdl:work.common_top") != std::string::npos);
    elaborate({ "sv:common_top" }, "sv_snapshot");
    assert_output(simulate("sv_snapshot"), "WORKSPACE_SYSTEMVERILOG");
    elaborate({ "vhdl:common_top(rtl)" }, "vhdl_snapshot");
    const auto vhdl = simulate("vhdl_snapshot");
    assert((vhdl.output + vhdl.error).find("WORKSPACE_VHDL") != std::string::npos);
    elaborate({ "unique_top" });
    assert_output(simulate(), "WORKSPACE_UNIQUE");
    elaborate({ "s=sv:work.common_top", "v=vhdl:work.common_top(rtl)" }, "mixed");
    const auto mixed = simulate("mixed");
    assert_output(mixed, "WORKSPACE_SYSTEMVERILOG");
    assert((mixed.output + mixed.error).find("WORKSPACE_VHDL") != std::string::npos);
}

} // namespace

int main(const int argc, const char* const* argv)
{
    struct Case {
        std::string_view name;
        void (*run)();
    };
    constexpr std::array cases {
        Case { "managed-snapshots", test_managed_objects_default_and_named_snapshots },
        Case { "recompile-rollback", test_recompilation_removes_old_names_and_rolls_back_errors },
        Case { "compiled-packages", test_compiled_packages_and_stale_consumers },
        Case { "mapped-library", test_external_library_mapping_and_current_directory },
        Case { "language-tops", test_language_qualification_and_multiple_roots },
    };
    if (argc > 2) {
        std::cerr << "usage: workspace-application [CASE]\n";
        return 2;
    }
    const std::string_view selected = argc == 2 ? argv[1] : "";
    bool matched = selected.empty();
    for (const auto& test : cases) {
        if (selected.empty() || selected == test.name) {
            matched = true;
            test.run();
        }
    }
    if (!matched) {
        std::cerr << "unknown workspace application test case: " << selected << '\n';
        return 2;
    }
    return 0;
}
