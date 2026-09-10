// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct Capture {
    fsim::runtime::RunResult result;
    std::array<double, 4> percentages { };
    std::array<double, 4> instance_percentages { };
    std::size_t compiled_processes { };
    fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const std::string_view cache_name)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "coverage-query";
    config.project.top = "sv:work.coverage_query";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory / cache_name;
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project);
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "coverage-runtime.bin", diagnostics);
    assert(restored && !diagnostics.has_error());
    assert(
        fsim::app::serialize_runtime_state(*restored, diagnostics)
        == encoded);
    project->design = std::move(*restored);

    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.native_cache = simulation.native_cache_statistics();
    const std::array names {
        "coverage_query.empty_coverage",
        "coverage_query.weighted_coverage",
        "coverage_query.advanced_coverage",
        "coverage_query.complete_coverage",
    };
    const std::array instance_names {
        "coverage_query.empty_instance_coverage",
        "coverage_query.weighted_instance_coverage",
        "coverage_query.advanced_instance_coverage",
        "coverage_query.complete_instance_coverage",
    };
    std::array<fsim::runtime::simir::SignalId, names.size()> signals { };
    std::array<fsim::runtime::simir::SignalId, instance_names.size()>
        instance_signals { };
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto signal = simulation.find_signal(names[index]);
        assert(signal);
        signals[index] = *signal;
        const auto instance_signal = simulation.find_signal(
            instance_names[index]);
        assert(instance_signal);
        instance_signals[index] = *instance_signal;
    }
    capture.result = simulation.run();
    for (std::size_t index = 0; index < signals.size(); ++index) {
        const auto value = simulation.read_scalar_signal(signals[index]);
        assert(value.kind == fsim::runtime::SystemVerilogScalarKind::Real);
        assert(value.as_real());
        capture.percentages[index] = *value.as_real();
        const auto instance_value = simulation.read_scalar_signal(
            instance_signals[index]);
        assert(instance_value.kind
                == fsim::runtime::SystemVerilogScalarKind::Real
            && instance_value.as_real());
        capture.instance_percentages[index]
            = *instance_value.as_real();
    }
    return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const std::string_view cache_name)
{
    const auto config = make_config(
        directory, source, optimization, cache_name);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const std::array expected { 0.0, 43.75, 62.5, 100.0 };
    const std::array expected_instances { 0.0, 37.5, 75.0, 100.0 };
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.time == 0);
    assert(reference.percentages == expected);
    assert(reference.instance_percentages == expected_instances);
    assert(cold.result.status == reference.result.status);
    assert(cold.result.time == reference.result.time);
    assert(cold.percentages == reference.percentages);
    assert(cold.instance_percentages == reference.instance_percentages);
    assert(warm.result.status == reference.result.status);
    assert(warm.result.time == reference.result.time);
    assert(warm.percentages == reference.percentages);
    assert(warm.instance_percentages == reference.instance_percentages);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1);
    assert(cold.native_cache.hits == 0);
    assert(cold.native_cache.misses == 1);
    assert(cold.native_cache.stores == 1);
    assert(warm.compiled_processes == 1);
    assert(warm.native_cache.hits == 1);
    assert(warm.native_cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
}

void verify_invalid_arity(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(module coverage_query;
  real invalid;
  initial begin
    invalid = $get_coverage(1);
    invalid = $get_inst_coverage(1);
  end
endmodule
)";
        assert(output.good());
    }
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(
        make_config(
            directory, source, fsim::project::Optimization::o0,
            "invalid-cache"),
        diagnostics);
    assert(!project);
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-075"
                || diagnostic.code == "FSIM-ELAB-SVCOV-001";
        }));

    const auto database_source = source.parent_path()
        / "invalid_coverage_database.sv";
    {
        std::ofstream output(database_source, std::ios::binary);
        output << R"(module coverage_database;
  initial begin
    $set_coverage_db_name();
    $load_coverage_db("first.db", "second.db");
  end
endmodule
)";
        assert(output.good());
    }
    fsim::diagnostic::Engine database_diagnostics;
    auto database_config = make_config(
        directory, database_source,
        fsim::project::Optimization::o0,
        "invalid-database-cache");
    database_config.project.top = "sv:work.coverage_database";
    const auto database_project = fsim::app::build_project(
        database_config, database_diagnostics);
    assert(!database_project);
    assert(std::ranges::any_of(
        database_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVCOV-002";
        }));
}

void verify_real_coverpoint(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine,
    const std::string_view suffix)
{
    const auto source = directory
        / ("real_coverage_" + std::string { suffix } + ".sv");
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(module real_coverage_query;
  covergroup real_group with function sample(input real value);
    value_point: coverpoint value {
      bins exact = {1.5};
      bins tolerance = {[2.0 +/- 0.25]};
    }
  endgroup
  real_group group = new;
  real observed_coverage;
  initial begin
    group.sample(1.5);
    group.sample(2.1);
    observed_coverage = $get_coverage();
    $finish;
  end
endmodule
)";
        assert(output.good());
    }
    auto config = make_config(
        directory, source, optimization,
        "real-cache-" + std::string { suffix });
    config.project.top = "sv:work.real_coverage_query";
    config.source_sets.front().standard = "2023";
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project && !diagnostics.has_error());
    assert(project->systemverilog_coverage.declarations.size() == 1U);
    assert(project->systemverilog_coverage.instances.size() == 1U);
    const auto declaration_identity
        = project->systemverilog_coverage.declarations.front()
              .canonical_identity;
    const auto instance_identity
        = project->systemverilog_coverage.instances.front().runtime_identity;
    assert(!declaration_identity.empty() && !instance_identity.empty());
    assert(project->systemverilog_coverage.declarations.front()
               .standard_revision
        == fsim::frontend::StandardRevision::SystemVerilog2023);
    assert(project->systemverilog_coverage.instances.front()
               .declaration_identity
        == declaration_identity);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const auto signal = simulation.find_signal(
        "real_coverage_query.observed_coverage");
    assert(signal);
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::stopped);
    const auto value = simulation.read_scalar_signal(*signal);
    assert(value.kind == fsim::runtime::SystemVerilogScalarKind::Real);
    assert(value.as_real() && *value.as_real() == 100.0);
    const auto& coverage = simulation.systemverilog_coverage();
    assert(coverage.declarations.size() == 1U
        && coverage.instances.size() == 1U);
    const auto& instance = coverage.instances.front();
    assert(instance.declaration_identity == declaration_identity
        && instance.runtime_identity == instance_identity);
    assert(instance.bin_hits.size() == 2U);
    assert(!instance.bin_hits[0].identity.empty()
        && !instance.bin_hits[1].identity.empty()
        && instance.bin_hits[0].identity != instance.bin_hits[1].identity);
    assert(instance.bin_hits[0].hit_count == 1U
        && instance.bin_hits[1].hit_count == 1U
        && instance.bin_hits[0].covered
        && instance.bin_hits[1].covered);
}

void write_database_source(
    const std::filesystem::path& source,
    const bool load,
    const std::string_view database)
{
    std::ofstream output(source, std::ios::binary);
    output << R"(module coverage_database;
  covergroup value_group with function sample(input bit value);
    value_point: coverpoint value {
      bins zero = {0};
      bins one = {1};
    }
  endgroup
  value_group group = new;
  real merged_coverage;
  initial begin
)";
    output << (load ? "    $load_coverage_db(\""
                    : "    $set_coverage_db_name(\"");
    output << database << "\");\n";
    output << (load ? "    group.sample(1);\n"
                    : "    group.sample(0);\n");
    if (load) {
        output << "    merged_coverage = $get_coverage();\n";
    }
    output << R"(    $finish;
  end
endmodule
)";
    assert(output.good());
}

void verify_coverage_database(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine,
    const std::string_view suffix)
{
    const auto source = directory / ("coverage_database_" + std::string { suffix } + ".sv");
    const auto database = "coverage_" + std::string { suffix } + ".db";
    auto config = make_config(
        directory, source, optimization,
        "database-cache-" + std::string { suffix });
    config.project.top = "sv:work.coverage_database";

    write_database_source(source, false, database);
    for (unsigned repetition = 0U; repetition < 2U; ++repetition) {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        assert(project && !diagnostics.has_error());
        const auto encoded = fsim::app::serialize_runtime_state(
            project->design, diagnostics);
        assert(encoded && !diagnostics.has_error());
        auto restored = fsim::app::deserialize_runtime_state(
            *encoded, "coverage-database-runtime.bin", diagnostics);
        assert(restored && !diagnostics.has_error());
        project->design = std::move(*restored);
        fsim::app::Simulation simulation {
            std::move(*project), config.run.max_deltas, engine
        };
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::stopped);
    }
    assert(std::filesystem::is_regular_file(directory / database));

    write_database_source(source, true, database);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project && !diagnostics.has_error());
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "coverage-database-runtime.bin", diagnostics);
    assert(restored && !diagnostics.has_error());
    project->design = std::move(*restored);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const auto signal = simulation.find_signal(
        "coverage_database.merged_coverage");
    assert(signal);
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::stopped);
    const auto value = simulation.read_scalar_signal(*signal);
    assert(value.kind == fsim::runtime::SystemVerilogScalarKind::Real);
    assert(value.as_real() && *value.as_real() == 100.0);
}

void verify_invalid_database_path(
    const std::filesystem::path& directory)
{
    const auto source = directory / "coverage_bad_path.sv";
    write_database_source(source, false, "../escaped-coverage.db");
    auto config = make_config(
        directory, source, fsim::project::Optimization::o0,
        "bad-path-cache");
    config.project.top = "sv:work.coverage_database";
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project && !diagnostics.has_error());
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter
    };
    bool rejected = false;
    try {
        (void)simulation.run();
    } catch (const std::runtime_error& error) {
        rejected = std::string_view { error.what() }.find(
                       "beneath the project file root")
            != std::string_view::npos;
    }
    assert(rejected);
    assert(!std::filesystem::exists(
        directory.parent_path() / "escaped-coverage.db"));
}

void verify_malformed_database(
    const std::filesystem::path& directory)
{
    const auto source = directory / "coverage_bad_database.sv";
    const auto database = directory / "malformed-coverage.db";
    {
        std::ofstream output(database, std::ios::binary);
        output << "not an fsim coverage database";
        assert(output.good());
    }
    write_database_source(source, true, database.filename().string());
    auto config = make_config(
        directory, source, fsim::project::Optimization::o0,
        "bad-database-cache");
    config.project.top = "sv:work.coverage_database";
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project && !diagnostics.has_error());
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter
    };
    bool rejected = false;
    try {
        (void)simulation.run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
}

} // namespace

int main()
{
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / "fsim-coverage-application-test"
    };
    std::error_code cleanup_error;
    std::filesystem::remove_all(directory.path, cleanup_error);
    assert(!cleanup_error);
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "coverage_query.sv";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(module coverage_query;
  covergroup low_group with function sample(input bit [1:0] value);
    type_option.weight = 1;
    value_point: coverpoint value {
      bins values[] = {0, 1, 2, 3};
    }
  endgroup
  covergroup high_group with function sample(input bit value);
    type_option.weight = 3;
    value_point: coverpoint value {
      bins values[] = {0, 1};
    }
  endgroup
  low_group low = new;
  high_group high = new;
  real empty_coverage;
  real weighted_coverage;
  real advanced_coverage;
  real complete_coverage;
  real empty_instance_coverage;
  real weighted_instance_coverage;
  real advanced_instance_coverage;
  real complete_instance_coverage;
  initial begin
    empty_coverage = $get_coverage();
    empty_instance_coverage = $get_inst_coverage();
    low.sample(0);
    high.sample(0);
    weighted_coverage = $get_coverage();
    weighted_instance_coverage = $get_inst_coverage();
    low.sample(1);
    low.sample(2);
    low.sample(3);
    advanced_coverage = $get_coverage();
    advanced_instance_coverage = $get_inst_coverage();
    high.sample(1);
    complete_coverage = $get_coverage();
    complete_instance_coverage = $get_inst_coverage();
    $finish;
  end
endmodule
)";
        assert(output.good());
    }
    verify_mode(
        directory.path, source,
        fsim::project::Optimization::o0, "cache-o0");
    verify_mode(
        directory.path, source,
        fsim::project::Optimization::o2, "cache-o2");
    verify_invalid_arity(
        directory.path, directory.path / "invalid_coverage_query.sv");
    verify_real_coverpoint(
        directory.path, fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter, "interpreter");
    verify_real_coverpoint(
        directory.path, fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::compiled, "compiled_o0");
    verify_real_coverpoint(
        directory.path, fsim::project::Optimization::o2,
        fsim::app::SimulationEngine::compiled, "compiled_o2");
    verify_coverage_database(
        directory.path, fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter, "interpreter");
    verify_coverage_database(
        directory.path, fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::compiled, "compiled_o0");
    verify_coverage_database(
        directory.path, fsim::project::Optimization::o2,
        fsim::app::SimulationEngine::compiled, "compiled_o2");
    verify_invalid_database_path(directory.path);
    verify_malformed_database(directory.path);
    return 0;
}
