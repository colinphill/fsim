// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_workspace_store.hpp"

#include "fsim/app/application.hpp"
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/support/path.hpp"
#include "../support/test_helpers.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
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

void test_module_procedural_coverage()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("coverage.sv", R"(`timescale 1ns/1ps
module leaf(input logic gate, output logic [7:0] value);
  initial begin
    #2;
    if (gate) value = 8'd11;
    else value = 8'd22;
  end
endmodule
module tb;
  logic gate0 = 1'b1;
  logic gate1 = 1'b0;
  logic [7:0] value0;
  logic [7:0] value1;
  integer saved;
  leaf a(.gate(gate0), .value(value0));
  leaf b(.gate(gate1), .value(value1));
  initial begin
    #3;
    $display("INSTANCE_WITNESS a=%0d b=%0d", value0, value1);
    saved = $coverage_save(`SV_COV_STATEMENT, "run.fsimcov");
    $display("COVERAGE_SAVE status=%0d", saved);
    $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "coverage.sv" });
    fs::remove("coverage.sv");
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "cov", "tb" });
    const auto run = invoke({ "simulate", "--snapshot", "cov",
        "--engine", "interpreter", "--trace-lifecycle", "disabled" });
    assert_output(run, "INSTANCE_WITNESS a=11 b=22");
    assert_output(run, "COVERAGE_SAVE status=1");
    const auto database = fsim::artifact::read_coverage_database(
        directory.path / "run.fsimcov");
    assert(database.ok());
    using Identity = fsim::artifact::CoverageDatabaseIdentity;
    using Family = fsim::artifact::CoverageDatabaseMetricFamily;
    using Scope = fsim::artifact::CoverageDatabaseMetricScope;
    std::map<Identity, std::map<std::uint64_t, std::uint64_t>> branch_hits;
    std::map<Identity, std::size_t> statement_points;
    std::map<Identity, std::size_t> covered_statements;
    for (const auto& metric : database.contents->metrics) {
        if (metric.scope != Scope::Instance)
            continue;
        assert(metric.source_line != 0U);
        if (metric.family == Family::Branch) {
            branch_hits[metric.instance_identity][metric.source_line] = metric.hits;
        } else if (metric.family == Family::Statement) {
            ++statement_points[metric.instance_identity];
            covered_statements[metric.instance_identity] += metric.hits != 0U;
        }
    }
    assert(branch_hits.size() == 2U);
    std::set<std::uint64_t> taken_lines;
    for (const auto& [identity, lines] : branch_hits) {
        assert(statement_points.at(identity) == 4U);
        assert(covered_statements.at(identity) == 3U);
        assert(lines.size() == 2U);
        assert(lines.contains(5U) && lines.contains(6U));
        assert((lines.at(5U) == 1U && lines.at(6U) == 0U)
            || (lines.at(5U) == 0U && lines.at(6U) == 1U));
        taken_lines.insert(lines.at(5U) ? 5U : 6U);
    }
    assert((taken_lines == std::set<std::uint64_t> { 5U, 6U }));
    assert(statement_points.size() == 3U);
    const auto controller = std::ranges::find_if(statement_points,
        [&](const auto& entry) { return !branch_hits.contains(entry.first); });
    assert(controller != statement_points.end() && controller->second == 7U);
    assert(covered_statements.at(controller->first) == 5U);
#if defined(FSIM_HAS_LLVM)
    const auto without_run_identity = [](auto metrics) {
        for (auto& metric : metrics)
            metric.run_identity = { };
        return metrics;
    };
    const auto expected_metrics = without_run_identity(database.contents->metrics);
    for (const auto optimization : { "O0", "O2" }) {
        const auto compiled = invoke({ "simulate", "--snapshot", "cov",
            "--engine", "compiled", "--compiled-processes", "all", "-O",
            optimization, "--trace-lifecycle", "disabled" });
        assert_output(compiled, "INSTANCE_WITNESS a=11 b=22");
        assert_output(compiled, "COVERAGE_SAVE status=1");
        const auto native = fsim::artifact::read_coverage_database(
            directory.path / "run.fsimcov");
        assert(native.ok());
        assert(without_run_identity(native.contents->metrics) == expected_metrics);
    }
#endif
}

void test_procedural_loop_update_coverage()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("loop.sv", R"(module tb;
  integer i;
  integer j = 0;
  integer saved;
  initial begin
    for (i = 0; i < 2; i = i + 1, j = j + 2, j = j + 1) begin
      $display("LOOP i=%0d j=%0d", i, j);
    end
    saved = $coverage_save(`SV_COV_STATEMENT, "run.fsimcov");
    $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "loop.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage", "tb" });
    const auto run = simulate();
    assert_output(run, "LOOP i=0 j=0");
    assert_output(run, "LOOP i=1 j=3");
    const auto database = fsim::artifact::read_coverage_database(
        directory.path / "run.fsimcov");
    assert(database.ok());
    std::vector<std::uint64_t> loop_line_hits;
    for (const auto& metric : database.contents->metrics) {
        if (metric.scope != fsim::artifact::CoverageDatabaseMetricScope::Instance
            || metric.family != fsim::artifact::CoverageDatabaseMetricFamily::Statement
            || metric.source_line != 6U) {
            continue;
        }
        loop_line_hits.push_back(metric.hits);
    }
    std::ranges::sort(loop_line_hits);
    assert((loop_line_hits == std::vector<std::uint64_t> { 1U, 2U, 2U }));
}

void test_coverage_controls_and_callable_boundary()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("controls.sv", R"(module tb;
  integer value;
  integer saved;
  task automatic update_value;
    value = 7;
  endtask
  initial begin
    update_value();
    // fsim coverage off metric=branch reason="conditional arm"
    if (value == 7) value = 8;
    // fsim coverage on metric=branch
    $display("CALLABLE_WITNESS value=%0d", value);
    saved = $coverage_save(`SV_COV_STATEMENT, "run.fsimcov");
    $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "controls.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage", "tb" });
    assert_output(simulate(), "CALLABLE_WITNESS value=8");
    const auto database = fsim::artifact::read_coverage_database(
        directory.path / "run.fsimcov");
    assert(database.ok());
    bool call_statement { };
    bool conditional_statement { };
    for (const auto& metric : database.contents->metrics) {
        if (metric.scope != fsim::artifact::CoverageDatabaseMetricScope::Instance)
            continue;
        assert(metric.family != fsim::artifact::CoverageDatabaseMetricFamily::Branch);
        assert(metric.source_line != 5U);
        call_statement |= metric.source_line == 8U && metric.hits == 1U;
        conditional_statement |= metric.source_line == 10U && metric.hits == 1U;
    }
    assert(call_statement && conditional_statement);
}

// Per-instance statement and branch points (line -> hits) of one database.
std::map<fsim::artifact::CoverageDatabaseIdentity,
    std::map<std::pair<int, std::uint64_t>, std::uint64_t>>
coverage_lines(const fs::path& path)
{
    const auto database = fsim::artifact::read_coverage_database(path);
    assert(database.ok());
    std::map<fsim::artifact::CoverageDatabaseIdentity,
        std::map<std::pair<int, std::uint64_t>, std::uint64_t>> result;
    for (const auto& metric : database.contents->metrics) {
        if (metric.scope != fsim::artifact::CoverageDatabaseMetricScope::Instance)
            continue;
        result[metric.instance_identity][{
            static_cast<int>(metric.family), metric.source_line }]
            += metric.hits;
    }
    return result;
}

// The sorted point hits of one metric family per source line, over every
// instance.
std::map<std::uint64_t, std::vector<std::uint64_t>>
coverage_family_hits(const fs::path& path,
    const fsim::artifact::CoverageDatabaseMetricFamily family)
{
    const auto database = fsim::artifact::read_coverage_database(path);
    assert(database.ok());
    std::map<std::uint64_t, std::vector<std::uint64_t>> result;
    for (const auto& metric : database.contents->metrics) {
        if (metric.scope == fsim::artifact::CoverageDatabaseMetricScope::Instance
            && metric.family == family) {
            result[metric.source_line].push_back(metric.hits);
        }
    }
    for (auto& [line, hits] : result) {
        (void)line;
        std::ranges::sort(hits);
    }
    return result;
}

using LineHits = std::map<std::uint64_t, std::vector<std::uint64_t>>;

// The output of simulating `snapshot` on `engine`, without the library
// object directories that differ between workspaces.
std::string simulation_output(const std::string& snapshot,
    const std::string& engine)
{
    const auto run = invoke({ "simulate", "--snapshot", snapshot, "--engine",
        engine, "--trace-lifecycle", "disabled" });
    auto text = run.output + run.error;
    for (auto at = text.find("objects/"); at != std::string::npos;
         at = text.find("objects/", at)) {
        const auto end = text.find('/', at + 8U);
        if (end == std::string::npos)
            break;
        text.erase(at, end + 1U - at);
    }
    return text;
}

// Coverage must not change what a design does: `source`, compiled again
// without coverage in a fresh workspace, prints on each engine what the
// covered `snapshot` prints, and both engines print the same. A design that
// reads its own coverage (`$coverage_get`) is not checked.
void assert_coverage_transparent(const std::string& snapshot,
    const fs::path& source, const std::string& top, const bool vhdl)
{
    std::map<std::string, std::string> covered;
    for (const auto* engine : { "interpreter", "v4" })
        covered[engine] = simulation_output(snapshot, engine);
    assert(covered["interpreter"] == covered["v4"]);
    std::ifstream input { fs::absolute(source), std::ios::binary };
    const std::string text { std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { } };
    assert(!text.empty());
    Directory directory;
    workspace::Store store { directory.path };
    write_file(source.filename(), text);
    std::vector<std::string> compile { "compile", "--quiet" };
    if (vhdl)
        compile.insert(compile.end(), { "--standard", "2008" });
    compile.push_back(source.filename().string());
    invoke(compile);
    invoke({ "elaborate", "--quiet", "--no-aot", "--snapshot", "plain", top });
    for (const auto* engine : { "interpreter", "v4" })
        assert(simulation_output("plain", engine) == covered[engine]);
}

// Simulates `snapshot` on both engines, which must agree, and checks the
// condition and expression points per line.
void assert_condition_coverage(const std::string& snapshot,
    const std::string_view witness, const LineHits& conditions,
    const LineHits& expressions)
{
    using Family = fsim::artifact::CoverageDatabaseMetricFamily;
    for (const auto* engine : { "interpreter", "v4" }) {
        const auto run = invoke({ "simulate", "--snapshot", snapshot,
            "--engine", engine, "--coverage-output", "run.fsimcov",
            "--trace-lifecycle", "disabled" });
        assert((run.output + run.error).find(witness) != std::string::npos);
        assert(coverage_family_hits("run.fsimcov", Family::Condition)
            == conditions);
        assert(coverage_family_hits("run.fsimcov", Family::Expression)
            == expressions);
    }
}

void test_condition_coverage()
{
    Directory directory;
    workspace::Store store { directory.path };
    // Each operand has true and false points; each short-circuit path is an
    // expression row. An unknown operand ends the walk; a decision with a
    // user call is not decomposed.
    write_file("cond.sv", R"(module tb;
  logic a, b, c;
  int n, k;
  int hits;
  function automatic bit f(input bit x); k++; return x; endfunction
  initial begin
    hits = 0; k = 0;
    a = 0; b = 0; c = 0;
    #1;
    if (a && b) hits = hits + 1;
    a = 1; b = 0;
    if (a && b) hits = hits + 1;
    b = 1;
    if ((a || b) && !c) hits = hits + 1;
    a = 1'bx;
    if (a || c) hits = hits + 1;
    if (f(b) && c) hits = hits + 1;
    // fsim coverage off metric=condition reason="test"
    if (b || c) hits = hits + 1;
    // fsim coverage on metric=condition
    n = 0;
    while (n < 3 && b) n = n + 1;
    $display("COND hits=%0d n=%0d k=%0d", hits, n, k);
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "cond.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "sv", "tb" });
    assert_condition_coverage("sv", "COND hits=2 n=3 k=1",
        { { 10U, { 0U, 0U, 0U, 1U } }, { 12U, { 0U, 0U, 1U, 1U } },
            { 14U, { 0U, 0U, 0U, 0U, 1U, 1U } }, { 16U, { 0U, 0U, 0U, 0U } },
            { 22U, { 0U, 1U, 3U, 3U } } },
        { { 10U, { 0U, 0U, 1U } }, { 12U, { 0U, 0U, 1U } },
            { 14U, { 0U, 0U, 0U, 0U, 1U } }, { 16U, { 0U, 0U, 0U } },
            { 19U, { 0U, 0U, 1U } }, { 22U, { 0U, 1U, 3U } } });
    assert_coverage_transparent("sv", "cond.sv", "tb", false);

    // VHDL: BOOLEAN and STD_ULOGIC operands ('H' is true), xor without
    // short circuit, and a user overload over another type.
    write_file("cond.vhd", R"(library ieee;
use ieee.std_logic_1164.all;
package p is
  type t is (lo, hi);
  function "and"(l, r : t) return boolean;
end package;
package body p is
  function "and"(l, r : t) return boolean is begin return l = hi and r = hi; end function;
end package body;
library ieee;
use ieee.std_logic_1164.all;
use work.p.all;
entity tb is end entity;
architecture sim of tb is
  signal s : std_logic := 'H';
  signal u : std_logic := 'X';
begin
  process
    variable a, b : boolean := false;
    variable x, y : t := hi;
    variable n, hits : integer := 0;
  begin
    wait for 1 ns;
    if a and b then hits := hits + 1; end if;
    a := true;
    if (a or b) and not (s = '1') then hits := hits + 1; end if;
    if a xor b then hits := hits + 1; end if;
    if s and u then hits := hits + 1; end if;
    if x and y then hits := hits + 1; end if;
    b := true;
    while n < 3 and b loop n := n + 1; end loop;
    report "COND hits=" & integer'image(hits) & " n=" & integer'image(n);
    wait;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--code-coverage", "--standard", "2008",
        "cond.vhd" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "vhdl", "tb(sim)" });
    assert_condition_coverage("vhdl", "COND hits=3 n=3",
        { { 24U, { 0U, 0U, 0U, 1U } }, { 26U, { 0U, 0U, 0U, 0U, 1U, 1U } },
            { 27U, { 0U, 0U, 1U, 1U } }, { 28U, { 0U, 0U, 0U, 1U } },
            { 31U, { 0U, 1U, 3U, 3U } } },
        { { 24U, { 0U, 0U, 1U } }, { 26U, { 0U, 0U, 0U, 0U, 1U } },
            { 27U, { 0U, 0U, 0U, 1U } }, { 28U, { 0U, 0U, 0U } },
            { 31U, { 0U, 1U, 3U } } });
    assert_coverage_transparent("vhdl", "cond.vhd", "tb(sim)", true);
}

// Simulates `snapshot` on both engines, which must agree, and checks the
// toggle points per declaration line.
void assert_toggle_coverage(const std::string& snapshot, const LineHits& toggles)
{
    for (const auto* engine : { "interpreter", "v4" }) {
        invoke({ "simulate", "--snapshot", snapshot, "--engine", engine,
            "--coverage-output", "run.fsimcov", "--trace-lifecycle",
            "disabled" });
        assert(coverage_family_hits("run.fsimcov",
                   fsim::artifact::CoverageDatabaseMetricFamily::Toggle)
            == toggles);
    }
}

void test_toggle_coverage()
{
    Directory directory;
    workspace::Store store { directory.path };
    // Per bit, a rise and a fall point; a transition to or from an unknown
    // value counts in neither. A port shares its actual's value; memories,
    // reals and excluded objects have no points.
    write_file("toggle.sv", R"(module leaf(input logic [1:0] a, output logic y);
  logic [7:0] mem [0:3];
  logic [1:0] q;
  real r;
  assign y = a[0];
  initial begin q = 2'b01; #1 q = 2'b10; #1 q = 2'bx1; #1 q = 2'b01; end
endmodule
module tb;
  logic [1:0] x = 0;
  logic w;
  // fsim coverage off metric=toggle reason="test"
  logic z = 0;
  // fsim coverage on metric=toggle
  leaf u (.a(x), .y(w));
  initial begin #1 x = 2'b11; z = 1; #1 x = 2'b10; #1 x = 2'b00; #2 $finish; end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "toggle.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "sv", "tb" });
    assert_toggle_coverage("sv",
        { { 1U, { 1U, 1U, 1U, 1U, 1U, 1U } }, { 3U, { 0U, 1U, 1U, 1U } },
            { 9U, { 1U, 1U, 1U, 1U } }, { 10U, { 1U, 1U } } });
    assert_coverage_transparent("sv", "toggle.sv", "tb", false);

    // STD_ULOGIC bits read through TO_X01; BOOLEAN toggles; INTEGER does
    // not.
    write_file("toggle.vhd", R"(library ieee;
use ieee.std_logic_1164.all;
entity leaf is
  port (a : in std_logic_vector(1 downto 0); y : out std_logic);
end entity;
architecture rtl of leaf is
  signal b : boolean := false;
  signal n : integer := 0;
begin
  y <= a(0);
  process begin
    wait for 1 ns; b <= true; n <= 3;
    wait for 1 ns; b <= false;
    wait;
  end process;
end architecture;
library ieee;
use ieee.std_logic_1164.all;
entity tb is end entity;
architecture sim of tb is
  signal x : std_logic_vector(1 downto 0) := "00";
  signal w : std_logic;
begin
  u : entity work.leaf port map (a => x, y => w);
  process begin
    wait for 1 ns; x <= "1H";
    wait for 1 ns; x <= "L0";
    wait for 1 ns; x <= "X1";
    wait for 1 ns; x <= "01";
    wait;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--code-coverage", "--standard", "2008",
        "toggle.vhd" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "vhdl", "tb(sim)" });
    assert_toggle_coverage("vhdl",
        { { 4U, { 1U, 1U, 1U, 1U, 2U, 2U } }, { 7U, { 1U, 1U } },
            { 21U, { 1U, 1U, 1U, 2U } }, { 22U, { 1U, 2U } } });
    assert_coverage_transparent("vhdl", "toggle.vhd", "tb(sim)", true);
}

// The excluded hits of one metric family per source line, and the
// exclusion reasons per family and line.
std::map<std::uint64_t, std::uint64_t> coverage_family_excluded_hits(
    const fs::path& path,
    const fsim::artifact::CoverageDatabaseMetricFamily family)
{
    const auto database = fsim::artifact::read_coverage_database(path);
    assert(database.ok());
    std::map<std::uint64_t, std::uint64_t> result;
    for (const auto& metric : database.contents->metrics) {
        if (metric.family == family && metric.excluded_hits != 0U) {
            result[metric.source_line] += metric.excluded_hits;
        }
    }
    return result;
}

std::set<std::tuple<int, std::uint64_t, std::string>>
coverage_exclusion_reasons(const fs::path& path)
{
    const auto database = fsim::artifact::read_coverage_database(path);
    assert(database.ok());
    std::set<std::tuple<int, std::uint64_t, std::string>> result;
    for (const auto& exclusion : database.contents->exclusions) {
        result.emplace(static_cast<int>(exclusion.family),
            exclusion.source_line, exclusion.reason);
    }
    return result;
}

// Simulates `snapshot` on both engines, which must agree, and checks the
// state and transition points per state-object line and the hits of the
// informational other-transition rows.
void assert_fsm_coverage(const std::string& snapshot, const LineHits& states,
    const LineHits& transitions,
    const std::map<std::uint64_t, std::uint64_t>& other)
{
    using Family = fsim::artifact::CoverageDatabaseMetricFamily;
    for (const auto* engine : { "interpreter", "v4" }) {
        invoke({ "simulate", "--snapshot", snapshot, "--engine", engine,
            "--coverage-output", "run.fsimcov", "--trace-lifecycle",
            "disabled" });
        assert(coverage_family_hits("run.fsimcov", Family::FsmState)
            == states);
        assert(coverage_family_hits("run.fsimcov", Family::FsmTransition)
            == transitions);
        assert(coverage_family_excluded_hits(
                   "run.fsimcov", Family::FsmTransition)
            == other);
    }
}

void test_fsm_coverage()
{
    using Family = fsim::artifact::CoverageDatabaseMetricFamily;
    const auto reason = [](const Family family, const std::uint64_t line,
                            const std::string_view text) {
        return std::tuple { static_cast<int>(family), line,
            std::string { text } };
    };
    const std::string other_reason {
        "informational: changes between states that no case arm assigns"
    };
    const std::string memory_reason {
        "memory: arrays of words are excluded from toggle coverage by default"
    };
    Directory directory;
    workspace::Store store { directory.path };
    // An enumeration machine: the jump to D is an other transition, counted
    // but excluded. A hinted machine's states are its named-constant
    // choices. The memory and the comment-excluded object are reason rows.
    write_file("fsm.sv", R"(module m3(input logic clk, input logic jump);
  typedef enum logic [1:0] { A, B, C, D } st_t;
  st_t s;
  logic [7:0] mem [0:3];
  // fsim coverage off metric=toggle reason="debug only"
  logic dbg;
  // fsim coverage on metric=toggle
  initial s = A;
  always @(posedge clk)
    if (jump) s <= D;
    else
      case (s)
        A: s <= B;
        B: s <= C;
        C: s <= A;
        D: s <= A;
      endcase
  always @(posedge clk) begin mem[0] <= mem[0] + 1; dbg <= ~dbg; end
endmodule
(* fsm_current_state="p", fsm_legal_states="P0,P1,P2" *)
module h1(input logic clk);
  localparam logic [1:0] P0 = 2'd0, P1 = 2'd1, P2 = 2'd2;
  logic [1:0] p = P0;
  always @(posedge clk)
    case (p)
      P0: p <= P1;
      P1: p <= P2;
      P2: p <= P0;
    endcase
endmodule
module tb;
  logic clk = 0, jump = 0;
  m3 u3 (.clk(clk), .jump(jump));
  h1 uh (.clk(clk));
  always #5 clk = ~clk;
  initial begin
    #12 jump = 1;
    #10 jump = 0;
    #50 $display("D8 done"); $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "fsm.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "sv", "tb" });
    assert_fsm_coverage("sv",
        { { 3U, { 1U, 1U, 3U, 3U } }, { 23U, { 2U, 3U, 3U } } },
        { { 3U, { 0U, 1U, 1U, 1U, 3U } }, { 23U, { 0U, 2U, 2U, 3U } } },
        { { 3U, 1U } });
    assert_coverage_transparent("sv", "fsm.sv", "tb", false);
    assert((coverage_exclusion_reasons("run.fsimcov")
        == std::set { reason(Family::Toggle, 4U, memory_reason),
            reason(Family::Toggle, 6U, "debug only"),
            reason(Family::FsmTransition, 3U, other_reason),
            reason(Family::FsmTransition, 23U, other_reason) }));

    // VHDL: an enumeration machine with a next-state partner, and a hinted
    // BIT_VECTOR machine; an array of words is a memory.
    write_file("fsm.vhd", R"(entity tb is end entity;
architecture sim of tb is
  type state_t is (idle, load, run, stop);
  signal state, next_state : state_t := idle;
  signal clk : bit := '0';
  signal go, jump : boolean := false;
  signal p : bit_vector(1 downto 0) := "00";
  constant P0 : bit_vector(1 downto 0) := "00";
  constant P1 : bit_vector(1 downto 0) := "01";
  constant P2 : bit_vector(1 downto 0) := "10";
  attribute fsm_current_state : string;
  attribute fsm_current_state of p : signal is "true";
  type mem_t is array (0 to 3) of bit_vector(7 downto 0);
  signal mem : mem_t;
begin
  clk <= not clk after 5 ns when now < 200 ns;
  process (clk) begin
    if clk'event and clk = '1' then
      if jump then state <= stop; else state <= next_state; end if;
    end if;
  end process;
  process (state, go) begin
    next_state <= state;
    case state is
      when idle => if go then next_state <= load; end if;
      when load => next_state <= run;
      when run => next_state <= idle;
      when stop => next_state <= idle;
    end case;
  end process;
  process (clk) begin
    if clk'event and clk = '1' then
      case p is
        when P0 => p <= P1;
        when P1 => p <= P2;
        when P2 => p <= P0;
        when others => p <= P0;
      end case;
    end if;
  end process;
  process begin
    wait for 22 ns; go <= true;
    wait for 10 ns; go <= false;
    wait for 20 ns; jump <= true;
    wait for 10 ns; jump <= false;
    wait;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--code-coverage", "--standard", "2008",
        "fsm.vhd" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "vhdl", "tb(sim)" });
    assert_fsm_coverage("vhdl",
        { { 4U, { 1U, 1U, 1U, 3U } }, { 7U, { 7U, 7U, 7U } } },
        { { 4U, { 0U, 1U, 1U, 1U, 1U } }, { 7U, { 0U, 6U, 7U, 7U } } },
        { { 4U, 1U } });
    assert_coverage_transparent("vhdl", "fsm.vhd", "tb(sim)", true);
    assert((coverage_exclusion_reasons("run.fsimcov")
        == std::set { reason(Family::Toggle, 14U, memory_reason),
            reason(Family::FsmTransition, 4U, other_reason),
            reason(Family::FsmTransition, 7U, other_reason) }));

    // STD_LOGIC_VECTOR machines compare elements exactly: "0H" is in no
    // state, so neither the change to it nor the change back is a
    // transition. Choices may be named constants or string literals.
    write_file("fsm2.vhd", R"(library ieee;
use ieee.std_logic_1164.all;
entity tb2 is end entity;
architecture sim of tb2 is
  signal clk : std_logic := '0';
  signal hold : boolean := false;
  constant S0 : std_logic_vector(1 downto 0) := "00";
  constant S1 : std_logic_vector(1 downto 0) := "01";
  constant S2 : std_logic_vector(1 downto 0) := "10";
  signal p : std_logic_vector(1 downto 0) := S0;
  signal q : std_logic_vector(2 downto 0) := "001";
  attribute fsm_current_state : string;
  attribute fsm_current_state of p : signal is "true";
  attribute fsm_current_state of q : signal is "true";
begin
  clk <= not clk after 5 ns when now < 100 ns;
  process (clk) begin
    if rising_edge(clk) then
      if hold then
        p <= "0H";
      else
        case p is
          when S0 => p <= S1;
          when S1 => p <= S2;
          when S2 => p <= S0;
          when others => p <= S0;
        end case;
      end if;
      case q is
        when "001" => q <= "010";
        when "010" => q <= "100";
        when "100" => q <= "001";
        when others => q <= "001";
      end case;
    end if;
  end process;
  process begin
    wait for 32 ns; hold <= true;
    wait for 10 ns; hold <= false;
    wait;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--code-coverage", "--standard", "2008",
        "fsm2.vhd" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "slv", "tb2(sim)" });
    assert_fsm_coverage("slv",
        { { 10U, { 3U, 3U, 4U } }, { 11U, { 3U, 4U, 4U } } },
        { { 10U, { 0U, 2U, 3U, 3U } }, { 11U, { 0U, 3U, 3U, 4U } } }, { });
    assert_coverage_transparent("slv", "fsm2.vhd", "tb2(sim)", true);
}

// Simulates `snapshot` on both engines, which must agree, and checks the
// statement and branch points per line.
void assert_statement_branch_coverage(const std::string& snapshot,
    const std::string_view witness, const LineHits& statements,
    const LineHits& branches)
{
    using Family = fsim::artifact::CoverageDatabaseMetricFamily;
    for (const auto* engine : { "interpreter", "v4" }) {
        const auto run = invoke({ "simulate", "--snapshot", snapshot,
            "--engine", engine, "--coverage-output", "run.fsimcov",
            "--trace-lifecycle", "disabled" });
        assert((run.output + run.error).find(witness) != std::string::npos);
        assert(coverage_family_hits("run.fsimcov", Family::Statement)
            == statements);
        assert(coverage_family_hits("run.fsimcov", Family::Branch)
            == branches);
    }
}

void test_concurrent_assignment_coverage()
{
    Directory directory;
    workspace::Store store { directory.path };
    // Each `assign` evaluation is one statement hit; both outcomes of each
    // `?:` are branch points, an unknown condition counting in neither and
    // a nested `?:` counting only when its outcome is taken.
    write_file("assign.sv", R"(module tb;
  logic a = 0, b = 0, c = 0;
  logic y, z, w;
  assign y = a & b;
  assign z = a ? b : c;
  assign w = a ? (b ? 1'b1 : 1'b0) : c;
  initial begin
    #1 a = 1;
    #1 b = 1;
    #1 a = 1'bx;
    #1 a = 0; c = 1;
    #1 $display("C4B y=%b z=%b w=%b", y, z, w); $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "assign.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "sv", "tb" });
    assert_statement_branch_coverage("sv", "C4B y=0 z=1 w=1",
        { { 2U, { 1U, 1U, 1U } }, { 4U, { 5U } }, { 5U, { 5U } },
            { 6U, { 5U } }, { 8U, { 1U, 1U } }, { 9U, { 1U, 1U } },
            { 10U, { 1U, 1U } }, { 11U, { 1U, 1U, 1U } },
            { 12U, { 1U, 1U, 1U } } },
        { { 5U, { 2U, 2U } }, { 6U, { 1U, 1U, 2U, 2U } } });
    assert_coverage_transparent("sv", "assign.sv", "tb", false);

    // VHDL: each evaluation of a concurrent assignment is one statement
    // hit; conditional and selected assignments have one statement point
    // and a branch point per alternative.
    write_file("assign.vhd", R"(entity tb is end entity;
architecture sim of tb is
  signal a, b : bit := '0';
  signal s : integer := 0;
  signal x, y, z : bit;
begin
  x <= a and b;
  y <= a when b = '1' else '1' when s = 2 else '0';
  with s select z <= a when 0, b when 1, '1' when others;
  process begin
    wait for 1 ns; a <= '1';
    wait for 1 ns; b <= '1';
    wait for 1 ns; s <= 1;
    wait for 1 ns; s <= 2; b <= '0';
    wait for 1 ns;
    report "C4B x=" & bit'image(x) & " y=" & bit'image(y) & " z=" & bit'image(z);
    wait;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--code-coverage", "--standard", "2008",
        "assign.vhd" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "vhdl", "tb(sim)" });
    assert_statement_branch_coverage("vhdl", "C4B x='0' y='1' z='1'",
        { { 7U, { 4U } }, { 8U, { 5U } }, { 9U, { 5U } },
            { 11U, { 1U, 1U } }, { 12U, { 1U, 1U } }, { 13U, { 1U, 1U } },
            { 14U, { 1U, 1U, 1U } }, { 15U, { 1U } }, { 16U, { 1U } },
            { 17U, { 1U } } },
        { { 8U, { 1U, 2U, 2U } }, { 9U, { 1U, 1U, 3U } } });
    assert_coverage_transparent("vhdl", "assign.vhd", "tb(sim)", true);
}

void test_standard_coverage_types()
{
    Directory directory;
    workspace::Store store { directory.path };
    // `$coverage_get` and `$coverage_control` select the state and toggle
    // points of SV_COV_FSM_STATE and SV_COV_TOGGLE. The six `int`
    // variables are toggle objects too: (2 + 1 + 2 + 6 * 32) * 2 = 394.
    write_file("types.sv", R"(module tb;
  typedef enum logic [1:0] { A, B, C } st_t;
  st_t s = A;
  logic clk = 0;
  logic [1:0] t = 0;
  int fsm_now, fsm_max, tog_now, tog_max, tog_stop, tog_after;
  always #5 clk = ~clk;
  always @(posedge clk)
    case (s)
      A: s <= B;
      B: s <= C;
      C: s <= A;
    endcase
  initial begin
    #12 fsm_now = $coverage_get(`SV_COV_FSM_STATE, `SV_COV_HIER, "tb");
    fsm_max = $coverage_get_max(`SV_COV_FSM_STATE, `SV_COV_HIER, "tb");
    tog_now = $coverage_get(`SV_COV_TOGGLE, `SV_COV_HIER, "tb");
    tog_max = $coverage_get_max(`SV_COV_TOGGLE, `SV_COV_HIER, "tb");
    tog_stop = $coverage_control(`SV_COV_STOP, `SV_COV_TOGGLE, `SV_COV_HIER, "tb");
    t = 2'b11;
    #1 tog_after = $coverage_get(`SV_COV_TOGGLE, `SV_COV_HIER, "tb");
    $display("STD fsm=%0d/%0d toggle=%0d/%0d stop=%0d after=%0d reset=%0d zero=%0d",
      fsm_now, fsm_max, tog_now, tog_max, tog_stop, tog_after,
      $coverage_control(`SV_COV_RESET, `SV_COV_TOGGLE, `SV_COV_HIER, "tb"),
      $coverage_get(`SV_COV_TOGGLE, `SV_COV_HIER, "tb"));
    $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "types.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "sv", "tb" });
    for (const auto* engine : { "interpreter", "v4" }) {
        const auto output = simulation_output("sv", engine);
        assert(output.find("STD fsm=2/3 toggle=3/394 stop=1 after=3 "
                           "reset=1 zero=0")
            != std::string::npos);
    }

    // Two instances of one machine: a module selector aggregates both, an
    // instance selector counts one. Merging the two engines' databases sums
    // their hits.
    write_file("agg.sv", R"(module leaf(input logic clk, input logic en);
  typedef enum logic { OFF, ON } st_t;
  st_t s = OFF;
  always @(posedge clk)
    if (en)
      case (s)
        OFF: s <= ON;
        ON: s <= OFF;
      endcase
endmodule
module tb2;
  logic clk = 0;
  leaf u0 (.clk(clk), .en(1'b1));
  leaf u1 (.clk(clk), .en(1'b0));
  always #5 clk = ~clk;
  initial begin
    #12 $display("AGG module=%0d/%0d u0=%0d/%0d u1=%0d/%0d",
      $coverage_get(`SV_COV_FSM_STATE, `SV_COV_MODULE, "leaf"),
      $coverage_get_max(`SV_COV_FSM_STATE, `SV_COV_MODULE, "leaf"),
      $coverage_get(`SV_COV_FSM_STATE, `SV_COV_MODULE, u0),
      $coverage_get_max(`SV_COV_FSM_STATE, `SV_COV_MODULE, u0),
      $coverage_get(`SV_COV_FSM_STATE, `SV_COV_MODULE, u1),
      $coverage_get_max(`SV_COV_FSM_STATE, `SV_COV_MODULE, u1));
    $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "agg.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "agg", "tb2" });
    for (const auto* engine : { "interpreter", "v4" }) {
        const auto run = invoke({ "simulate", "--snapshot", "agg", "--engine",
            engine, "--coverage-output", std::string { engine } + ".fsimcov",
            "--trace-lifecycle", "disabled" });
        assert((run.output + run.error)
                .find("AGG module=3/4 u0=2/2 u1=1/2")
            != std::string::npos);
    }
    invoke({ "coverage", "merge", "interpreter.fsimcov", "v4.fsimcov",
        "--output", "merged.fsimcov" });
    // The combined row joins the instances point by point; each instance
    // keeps its own row. The other-transition row is excluded, so one of
    // the two legal transitions is half of the transition score.
    const auto report = invoke({ "coverage", "report", "merged.fsimcov" });
    for (const auto* row : {
             "family=fsm-state total=2 covered=2 uncovered=0 excluded=0 hits=6 ",
             "family=fsm-state total=2 covered=2 uncovered=0 excluded=0 hits=4 ",
             "family=fsm-state total=2 covered=1 uncovered=1 excluded=0 hits=2 ",
             "family=fsm-transition total=3 covered=1 uncovered=1 excluded=1 " }) {
        assert(report.output.find(row) != std::string::npos);
    }
    invoke({ "coverage", "report", "merged.fsimcov", "--threshold",
        "fsm-transition=50" });
    const auto unmet = invoke({ "coverage", "report", "merged.fsimcov",
                                  "--threshold", "fsm-transition=51" },
        false);
    assert(unmet.status == 4);
}

void test_fsm_hint_diagnostics()
{
    Directory directory;
    workspace::Store store { directory.path };
    // A legal state must be a literal or case choice of the hinted object.
    write_file("hint.sv", R"((* fsm_current_state="p", fsm_legal_states="P0,P9" *)
module h(input logic clk);
  localparam logic [1:0] P0 = 2'd0, P1 = 2'd1;
  logic [1:0] p = P0;
  always @(posedge clk)
    case (p)
      P0: p <= P1;
      P1: p <= P0;
    endcase
endmodule
module tb;
  logic clk = 0;
  h u (.clk(clk));
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "hint.sv" });
    const auto sv = invoke({ "elaborate", "--quiet", "--no-aot",
                               "--code-coverage", "--snapshot", "sv", "tb" },
        false);
    assert(sv.error.find("FSIM-COV-026") != std::string::npos);
    assert(sv.error.find("'P9'") != std::string::npos);

    // fsm_current_state is "true" or "false".
    write_file("hint.vhd", R"(entity tb is end entity;
architecture sim of tb is
  type st_t is (idle, run);
  signal s : st_t := idle;
  signal clk : bit := '0';
  attribute fsm_current_state : string;
  attribute fsm_current_state of s : signal is "maybe";
begin
  process (clk) begin
    if clk'event and clk = '1' then
      case s is
        when idle => s <= run;
        when run => s <= idle;
      end case;
    end if;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--code-coverage", "--standard", "2008",
        "hint.vhd" });
    const auto vhdl = invoke({ "elaborate", "--quiet", "--no-aot",
                                 "--code-coverage", "--snapshot", "vhdl",
                                 "tb(sim)" },
        false);
    assert(vhdl.error.find("FSIM-COV-026") != std::string::npos);
}

void test_vhdl_procedural_coverage()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("coverage.vhd", R"(entity leaf is
  port (gate : in bit; value : out integer);
end entity;
architecture rtl of leaf is
  type state_t is (idle, run, done);
begin
  process
    variable s : state_t := idle;
  begin
    wait for 2 ns;
    if gate = '1' then
      value <= 11;
    elsif s = run then
      value <= 33;
    else
      value <= 22;
    end if;
    case s is
      when idle => s := run;
      when run => s := done;
      -- fsim coverage off metric=branch reason="unreachable"
      when others => null;
      -- fsim coverage on metric=branch
    end case;
    value <= 5 when gate = '1' else 6 when s = done else 7;
    wait;
  end process;
end architecture;
entity tb is end entity;
architecture sim of tb is
  signal g0 : bit := '1';
  signal g1 : bit := '0';
  signal v0, v1 : integer;
begin
  a : entity work.leaf port map (gate => g0, value => v0);
  b : entity work.leaf port map (gate => g1, value => v1);
  process begin
    wait for 3 ns;
    report "INSTANCE_WITNESS a=" & integer'image(v0) & " b=" & integer'image(v1);
    wait;
  end process;
end architecture;
)");
    invoke({ "compile", "--quiet", "--code-coverage", "--standard", "2008",
        "coverage.vhd" });
    fs::remove("coverage.vhd");
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage",
        "--snapshot", "cov", "tb(sim)" });
    std::optional<std::map<fsim::artifact::CoverageDatabaseIdentity,
        std::map<std::pair<int, std::uint64_t>, std::uint64_t>>> expected;
    for (const auto* engine : { "interpreter", "v4" }) {
        const auto run = invoke({ "simulate", "--snapshot", "cov",
            "--engine", engine, "--coverage-output", "run.fsimcov",
            "--trace-lifecycle", "disabled" });
        assert((run.output + run.error).find("INSTANCE_WITNESS a=5 b=7")
            != std::string::npos);
        const auto lines = coverage_lines(directory.path / "run.fsimcov");
        if (expected) {
            assert(lines == *expected);
            continue;
        }
        expected = lines;
        constexpr int statement
            = static_cast<int>(fsim::artifact::CoverageDatabaseMetricFamily::Statement);
        constexpr int branch
            = static_cast<int>(fsim::artifact::CoverageDatabaseMetricFamily::Branch);
        // Two leaf instances and the testbench process.
        assert(lines.size() == 3U);
        std::set<std::uint64_t> taken;
        std::size_t leaves { };
        for (const auto& [identity, points] : lines) {
            if (!points.contains({ branch, 12U }))
                continue;
            ++leaves;
            // if/elsif/else arms; the elsif statement is the false arm.
            assert(points.contains({ branch, 13U }) && points.contains({ branch, 14U })
                && points.contains({ branch, 16U }));
            taken.insert(points.at({ branch, 12U }) ? 12U : 16U);
            // Case items; the excluded others item has no point.
            assert(points.at({ branch, 19U }) == 1U && points.at({ branch, 20U }) == 0U);
            assert(!points.contains({ branch, 22U }));
            assert(points.at({ statement, 20U }) == 0U);
            // The conditional assignment is one statement with three outcomes.
            assert(points.at({ statement, 25U }) == 1U);
            assert(points.at({ branch, 25U }) == 1U);
        }
        assert(leaves == 2U);
        assert((taken == std::set<std::uint64_t> { 12U, 16U }));
    }
}

void test_case_item_coverage()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("case.sv", R"(module tb;
  logic [1:0] sel = 2'd1;
  integer r;
  initial begin
    case (sel)
      2'd0: r = 0;
      2'd1: r = 1;
    endcase
    case (sel)
      2'd0: r = 10;
      default: r = 11;
    endcase
    $display("CASE r=%0d", r);
  end
endmodule
)");
    invoke({ "compile", "--quiet", "--code-coverage", "case.sv" });
    invoke({ "elaborate", "--quiet", "--no-aot", "--code-coverage", "tb" });
    assert_output(invoke({ "simulate", "--coverage-output", "run.fsimcov",
                      "--trace-lifecycle", "disabled" }),
        "CASE r=11");
    const auto lines = coverage_lines(directory.path / "run.fsimcov");
    assert(lines.size() == 1U);
    const auto& points = lines.begin()->second;
    constexpr int branch
        = static_cast<int>(fsim::artifact::CoverageDatabaseMetricFamily::Branch);
    // Items, and the no-match outcome of the case without a default.
    assert(points.at({ branch, 5U }) == 0U);
    assert(points.at({ branch, 6U }) == 0U && points.at({ branch, 7U }) == 1U);
    assert(points.at({ branch, 10U }) == 0U && points.at({ branch, 11U }) == 1U);
    assert(!points.contains({ branch, 9U }));
    const auto rejected = invoke({ "elaborate", "--quiet", "--coverage-output",
        "x.fsimcov", "tb" }, false);
    assert(rejected.error.find("--coverage-output") != std::string::npos);
}

void test_disabled_coverage_snapshot()
{
    Directory directory;
    workspace::Store store { directory.path };
    write_file("disabled.sv", R"(module tb;
  integer saved;
  initial begin
    saved = $coverage_save(`SV_COV_STATEMENT, "run.fsimcov");
    $display("DISABLED_SAVE=%0d", saved);
    $finish;
  end
endmodule
)");
    invoke({ "compile", "--quiet", "disabled.sv" });
    elaborate({ "tb" });
    assert_output(simulate(), "DISABLED_SAVE=0");
    assert(!fs::exists(directory.path / "run.fsimcov"));
    const auto rejected = invoke({ "simulate", "--code-coverage",
        "--engine", "interpreter" }, false);
    assert(rejected.error.find("FSIM-COV-008") != std::string::npos);
    assert(rejected.error.find("re-elaborat") != std::string::npos);
}

void test_sdf_session_application()
{
    using fsim::test::require;
    {
        Directory directory;
        workspace::Store store { directory.path };
        write_file("top.sv", R"sv(`timescale 1ns/1ps
module delay_buf(input logic a, output wire z);
  assign z = a;
  specify
    (a => z) = 5;
  endspecify
endmodule
module top;
  logic a;
  wire z;
  delay_buf u0(.a(a), .z(z));
  initial begin
    a = 0;
    #10 a = 1;
    #20 a = 0;
    #20 $finish;
  end
  always @(z) $display("SDF_WITNESS t=%0t z=%0d", $time, z);
endmodule
)sv");
        write_file("valid.sdf", R"sdf((DELAYFILE
  (SDFVERSION "4.0")
  (DESIGN "top")
  (TIMESCALE 1 ns)
  (CELL (CELLTYPE "delay_buf") (INSTANCE top.u0)
    (DELAY (ABSOLUTE (IOPATH a z (1:3:5)))))
))sdf");
        write_file("malformed.sdf", "(DELAYFILE (CELL");
        invoke({ "compile", "--library", "work", "top.sv" });
        invoke({ "elaborate", "work.top", "--snapshot", "plain" });
        const auto plain = simulate("plain");
        require(plain.output.find("SDF_WITNESS t=15000 z=1")
                != std::string::npos
                && plain.output.find("SDF_WITNESS t=35000 z=0")
                    != std::string::npos,
            "plain module path must retain its 5 ns delay");
        const auto failed = [&](const std::string_view name,
                                const std::string_view source) {
            const auto result = invoke({ "elaborate", "work.top", "--snapshot",
                std::string { name }, "--sdf", std::string { source },
                "--sdf-root", "top", "--sdf-cell", "top.u0" }, false);
            require(!result.error.empty(),
                "invalid SDF input must publish a diagnostic");
            std::string error;
            require(!store.read_snapshot(name, error),
                "invalid SDF input must not publish a snapshot");
        };
        failed("missing", "missing.sdf");
        failed("malformed", "malformed.sdf");
        const auto padded_sdf = [&](const fs::path& path,
                                    const std::uintmax_t target_bytes) {
            fs::copy_file("valid.sdf", path);
            std::ofstream output(path, std::ios::binary | std::ios::app);
            const std::string spaces(4096U, ' ');
            auto written = fs::file_size(path);
            while (written < target_bytes) {
                const auto count = std::min<std::uintmax_t>(
                    spaces.size(), target_bytes - written);
                output.write(spaces.data(), static_cast<std::streamsize>(count));
                written += count;
            }
            output.close();
            require(output.good() && fs::file_size(path) == target_bytes,
                "aggregate SDF fixture must have exact bounded size");
        };
        padded_sdf("aggregate-first.sdf", 1U << 20U);
        padded_sdf("aggregate-second.sdf", (63U << 20U) + 1U);
        const auto aggregate = invoke({ "elaborate", "work.top",
            "--snapshot", "aggregate-excess", "--sdf", "aggregate-first.sdf",
            "--sdf", "aggregate-second.sdf", "--sdf-root", "top",
            "--sdf-cell", "top.u0" }, false);
        require(aggregate.error.find("FSIM-SDF-SESSION-001")
                != std::string::npos,
            "two individually bounded SDF bodies must reject above 64 MiB total");
        std::string aggregate_error;
        require(!store.read_snapshot("aggregate-excess", aggregate_error),
            "aggregate SDF rejection must not publish a snapshot");
        fs::copy_file("valid.sdf", "single-excess.sdf");
        fs::resize_file("single-excess.sdf", (64U << 20U) + 1U);
        const auto oversized = invoke({ "elaborate", "work.top",
            "--snapshot", "single-excess", "--sdf", "single-excess.sdf",
            "--sdf-root", "top", "--sdf-cell", "top.u0" }, false);
        require(oversized.error.find("FSIM-SDF-SESSION-001")
                != std::string::npos,
            "one SDF file above 64 MiB must reject before parsing");
        std::string oversized_error;
        require(!store.read_snapshot("single-excess", oversized_error),
            "per-file SDF rejection must not publish a snapshot");
        const auto annotated = [&](const std::string_view name,
                                   const std::string_view selection,
                                   const std::string_view rise,
                                   const std::string_view fall) {
            invoke({ "elaborate", "work.top", "--snapshot",
                std::string { name }, "--sdf", "valid.sdf",
                "--sdf-root", "top", "--sdf-cell", "top.u0",
                "--delay-mode", std::string { selection } });
            const auto result = simulate(name);
            require(result.output.find(std::string { rise })
                    != std::string::npos
                    && result.output.find(std::string { fall })
                        != std::string::npos,
                "selected SDF min/typ/max delay must affect real output");
            fsim::diagnostic::Engine diagnostics;
            auto metadata = fsim::artifact::load_design_metadata(
                snapshot(store, name).path, diagnostics);
            require(metadata && !diagnostics.has_error()
                    && metadata->sdf_annotations.size() == 1U
                    && std::ranges::any_of(metadata->payloads,
                        [](const auto& payload) {
                            return payload.artifact.generic_string()
                                .starts_with("sdf/");
                        }),
                "annotated snapshot must index its portable SDF payload");
            return metadata->cache_key;
        };
        const auto minimum = annotated("minimum", "min",
            "SDF_WITNESS t=11000 z=1", "SDF_WITNESS t=31000 z=0");
        const auto typical = annotated("typical", "typ",
            "SDF_WITNESS t=13000 z=1", "SDF_WITNESS t=33000 z=0");
        const auto maximum = annotated("maximum", "max",
            "SDF_WITNESS t=15000 z=1", "SDF_WITNESS t=35000 z=0");
        require(minimum != typical && typical != maximum
                && minimum != maximum,
            "SDF min/typ/max selections must separate native cache identity");
        fs::rename("valid.sdf", "valid-hidden.sdf");
        const auto persisted = simulate("typical");
        require(persisted.output.find("SDF_WITNESS t=13000 z=1")
                != std::string::npos
                && persisted.output.find("SDF_WITNESS t=33000 z=0")
                    != std::string::npos,
            "saved timing must replay without the original SDF file");
    }
    {
        Directory directory;
        workspace::Store store { directory.path };
        write_file("two.sv", R"sv(`timescale 1ns/1ps
module delay_buf(input logic a, output wire z);
  assign z = a;
  specify
    (a => z) = 5;
  endspecify
endmodule
module top;
  logic a;
  wire z0, z1;
  delay_buf u0(.a(a), .z(z0));
  delay_buf u1(.a(a), .z(z1));
  initial begin
    a = 0;
    #10 a = 1;
    #20 a = 0;
    #20 $finish;
  end
  always @(z0) $display("U0 t=%0t z=%0d", $time, z0);
  always @(z1) $display("U1 t=%0t z=%0d", $time, z1);
endmodule
)sv");
        const auto sdf_file = [](const std::string_view instance,
                                  const int delay) {
            return "(DELAYFILE\n  (SDFVERSION \"4.0\")\n"
                "  (DESIGN \"top\")\n  (TIMESCALE 1 ns)\n"
                "  (CELL (CELLTYPE \"delay_buf\") (INSTANCE "
                + std::string { instance }
                + ") (DELAY (ABSOLUTE (IOPATH a z ("
                + std::to_string(delay) + "))))))\n";
        };
        write_file("u0-three.sdf", sdf_file("top.u0", 3));
        write_file("u1-two.sdf", sdf_file("top.u1", 2));
        write_file("u0-four.sdf", sdf_file("top.u0", 4));
        write_file("both.sdf", "(DELAYFILE\n"
            "  (SDFVERSION \"4.0\")\n"
            "  (DESIGN \"top\")\n"
            "  (TIMESCALE 1 ns)\n"
            "  (CELL (CELLTYPE \"delay_buf\") (INSTANCE top.u0)\n"
            "    (DELAY (ABSOLUTE (IOPATH a z (3)))))\n"
            "  (CELL (CELLTYPE \"delay_buf\") (INSTANCE top.u1)\n"
            "    (DELAY (ABSOLUTE (IOPATH a z (2)))))\n)");
        invoke({ "compile", "--library", "work", "two.sv" });
        invoke({ "elaborate", "work.top", "--snapshot", "selected",
            "--sdf", "both.sdf", "--sdf-root", "top",
            "--sdf-cell", "top.u0" });
        const auto selected = simulate("selected");
        require(selected.output.find("U0 t=13000 z=1")
                != std::string::npos
                && selected.output.find("U1 t=15000 z=1")
                    != std::string::npos,
            "cell selector must annotate u0 without changing u1");
        invoke({ "elaborate", "work.top", "--snapshot", "all-cells",
            "--sdf", "both.sdf", "--sdf-root", "top",
            "--sdf-cell", "top.u*" });
        const auto all_cells = simulate("all-cells");
        require(all_cells.output.find("U0 t=13000 z=1")
                != std::string::npos
                && all_cells.output.find("U1 t=12000 z=1")
                    != std::string::npos,
            "wildcard selector must annotate both matching cells");
        invoke({ "elaborate", "work.top", "--snapshot", "ordered",
            "--sdf", "u0-three.sdf", "--sdf", "u1-two.sdf",
            "--sdf", "u0-four.sdf", "--sdf-root", "top",
            "--sdf-cell", "top.u*" });
        const auto ordered = simulate("ordered");
        require(ordered.output.find("U0 t=14000 z=1")
                != std::string::npos
                && ordered.output.find("U1 t=12000 z=1")
                    != std::string::npos,
            "ordered files must override u0 without resetting disjoint u1");
        fsim::diagnostic::Engine diagnostics;
        const auto selected_metadata = fsim::artifact::load_design_metadata(
            snapshot(store, "selected").path, diagnostics);
        const auto all_metadata = fsim::artifact::load_design_metadata(
            snapshot(store, "all-cells").path, diagnostics);
        const auto ordered_metadata = fsim::artifact::load_design_metadata(
            snapshot(store, "ordered").path, diagnostics);
        require(selected_metadata && all_metadata && ordered_metadata
                && !diagnostics.has_error()
                && selected_metadata->cache_key != all_metadata->cache_key
                && selected_metadata->design_digest
                    != all_metadata->design_digest
                && ordered_metadata->sdf_annotations.size() == 3U,
            "selected cells and ordered files must have distinct portable identities");
    }
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
        Case { "procedural-coverage", test_module_procedural_coverage },
        Case { "loop-update-coverage", test_procedural_loop_update_coverage },
        Case { "vhdl-procedural-coverage", test_vhdl_procedural_coverage },
        Case { "case-item-coverage", test_case_item_coverage },
        Case { "condition-coverage", test_condition_coverage },
        Case { "toggle-coverage", test_toggle_coverage },
        Case { "fsm-coverage", test_fsm_coverage },
        Case { "concurrent-assignment-coverage",
            test_concurrent_assignment_coverage },
        Case { "standard-coverage-types", test_standard_coverage_types },
        Case { "fsm-hint-diagnostics", test_fsm_hint_diagnostics },
        Case { "coverage-controls-callable", test_coverage_controls_and_callable_boundary },
        Case { "coverage-disabled", test_disabled_coverage_snapshot },
        Case { "sdf-session", test_sdf_session_application },
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
