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
#include <map>
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
        snapshot(store, "cov").path.parent_path() / "run.fsimcov");
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
            snapshot(store, "cov").path.parent_path() / "run.fsimcov");
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
        snapshot(store).path.parent_path() / "run.fsimcov");
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
        snapshot(store).path.parent_path() / "run.fsimcov");
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
    assert(!fs::exists(snapshot(store).path.parent_path() / "run.fsimcov"));
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
