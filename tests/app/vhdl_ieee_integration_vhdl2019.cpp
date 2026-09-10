// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "vhdl_ieee_integration_support.hpp"

namespace {

class ScopedEnvironment final {
public:
    ScopedEnvironment(
        std::string name, const std::optional<std::string>& value)
        : name_(std::move(name))
        , previous_(fsim::support::environment_variable(name_))
    {
#if defined(_WIN32)
        assert(::_putenv_s(
            name_.c_str(), value ? value->c_str() : "") == 0);
#else
        if (value) {
            assert(::setenv(name_.c_str(), value->c_str(), 1) == 0);
        } else {
            assert(::unsetenv(name_.c_str()) == 0);
        }
#endif
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
#if defined(_WIN32)
        (void)::_putenv_s(
            name_.c_str(), previous_ ? previous_->c_str() : "");
#else
        if (previous_) {
            (void)::setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            (void)::unsetenv(name_.c_str());
        }
#endif
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

} // namespace
fsim::project::Config simulator_api_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "simulator_api";
    config.project.top = "vhdl:work.simulator_api(rtl)";
    config.project.time_resolution = "1ns";
    config.build.jobs = 8;
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "simulator-api-cache-o0"
                : "simulator-api-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet source_set;
    source_set.language = fsim::project::Language::vhdl;
    source_set.standard = "2019";
    source_set.library = "work";
    source_set.compilation_unit = "file";
    source_set.files.push_back(source);
    config.source_sets.push_back(std::move(source_set));
    return config;
}

struct SimulatorApiCapture {
    fsim::runtime::RunResult paused;
    fsim::runtime::RunResult finished;
    std::array<std::string, 8> paused_values;
    std::array<std::string, 8> finished_values;
    std::array<std::string, 2> formatted_values;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics native_cache;
};

SimulatorApiCapture run_simulator_api(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const bool portable_roundtrip = false)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project && !diagnostics.has_error());
    if (portable_roundtrip) {
        const auto runtime_bytes = fsim::app::serialize_runtime_state(
            project->design, diagnostics);
        auto runtime = runtime_bytes
            ? fsim::app::deserialize_runtime_state(
                  *runtime_bytes, "simulator-api-runtime", diagnostics)
            : std::nullopt;
        assert(runtime && !diagnostics.has_error());
        assert(fsim::app::serialize_runtime_state(*runtime, diagnostics)
            == runtime_bytes);
        project->design = std::move(*runtime);

        const auto hir_bytes = fsim::app::serialize_vhdl_hir_state(
            project->vhdl_hir, project->semantics, diagnostics);
        auto hir = hir_bytes
            ? fsim::app::deserialize_vhdl_hir_state(
                  *hir_bytes, "simulator-api-vhdl-hir",
                  project->semantics, diagnostics)
            : std::nullopt;
        assert(hir && !diagnostics.has_error());
        assert(fsim::app::serialize_vhdl_hir_state(
                   *hir, project->semantics, diagnostics)
            == hir_bytes);
        project->vhdl_hir = std::move(*hir);
    }
    std::size_t pause_operations { };
    std::size_t stop_operations { };
    std::vector<std::tuple<
        std::string, fsim::runtime::simir::ProcessId, std::size_t>>
        formatted_locals;
    for (const auto& process : project->design.processes()) {
        for (const auto& operation : process.operations) {
            if (const auto* pause
                = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::Pause>(&operation)) {
                ++pause_operations;
                assert(pause->status);
            }
            if (const auto* stop
                = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::Stop>(&operation)) {
                ++stop_operations;
                assert(stop->status);
            }
        }
        for (std::size_t index = 0;
            index < process.debug_string_locals.size(); ++index) {
            const auto& local = process.debug_string_locals[index];
            if (local.name.starts_with("formatted_")) {
                formatted_locals.emplace_back(local.name, process.id, index);
            }
        }
    }
    assert(pause_operations == 1 && stop_operations == 1);
    assert(formatted_locals.size() == 2);
    std::ranges::sort(formatted_locals);
    assert(project->cache_path == config.build.cache_path);
    if (engine != fsim::app::SimulationEngine::interpreter) {
        project->compiled_process_selection
            = fsim::app::BuiltProject::CompiledProcessSelection::all;
    }

    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    if (engine != fsim::app::SimulationEngine::interpreter) {
        simulation.await_all_native_compilation();
    }
    SimulatorApiCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    const std::array names {
        "simulator_api.resolution_ok",
        "simulator_api.resumed",
        "simulator_api.escaped_finish",
        "simulator_api.calendar_ok",
        "simulator_api.arithmetic_ok",
        "simulator_api.conversion_ok",
        "simulator_api.roundtrip_ok",
        "simulator_api.current_ok"
    };
    std::array<fsim::runtime::simir::SignalId, names.size()> signals;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto signal = simulation.find_signal(names[index]);
        assert(signal);
        signals[index] = *signal;
    }
    const auto values = [&]() {
        std::array<std::string, names.size()> result;
        for (std::size_t index = 0; index < signals.size(); ++index) {
            result[index] = simulation.read_signal(signals[index]).to_msb_string();
        }
        return result;
    };
    capture.paused = simulation.run();
    capture.paused_values = values();
    simulation.clear_stop();
    capture.finished = simulation.run();
    capture.finished_values = values();
    for (std::size_t index = 0; index < formatted_locals.size(); ++index) {
        const auto& [name, process, local] = formatted_locals[index];
        capture.formatted_values[index]
            = simulation.read_process_string_local(process, local);
    }
    capture.native_cache = simulation.native_cache_statistics();
    return capture;
}

void verify_vhdl2019_simulator_api(const std::filesystem::path& directory)
{
    const auto source = directory / "simulator-api.vhd";
    constexpr std::string_view source_text = R"(
entity simulator_api is end entity;
architecture rtl of simulator_api is
  signal resolution_ok : boolean := false;
  signal resumed : boolean := false;
  signal escaped_finish : boolean := false;
  signal calendar_ok : boolean := false;
  signal arithmetic_ok : boolean := false;
  signal conversion_ok : boolean := false;
  signal roundtrip_ok : boolean := false;
  signal current_ok : boolean := false;
begin
  control : process
    variable epoch_zero : std.env.time_record;
    variable incremented : std.env.time_record;
    variable decremented : std.env.time_record;
    variable commuted : std.env.time_record;
    variable subtracted : std.env.time_record;
    variable local_epoch : std.env.time_record;
    variable utc_roundtrip : std.env.time_record;
    variable local_roundtrip : std.env.time_record;
    variable current_local : std.env.time_record;
    variable current_utc : std.env.time_record;
    variable difference : real;
    variable epoch_roundtrip : real;
    variable current_epoch : real;
    variable formatted_plain : string := "0000000000000000000";
    variable formatted_fraction : string := "00000000000000000000000000";
  begin
    resolution_ok <= std.env.resolution_limit = 1 ns;
    epoch_zero := std.env.gmtime(timer => 0.0);
    incremented := epoch_zero + 90.5;
    decremented := 90.5 - incremented;
    commuted := 90.5 + epoch_zero;
    subtracted := incremented - 90.5;
    difference := incremented - epoch_zero;
    local_epoch := std.env.localtime(0.0);
    utc_roundtrip := std.env.gmtime(trec => local_epoch);
    local_roundtrip := std.env.localtime(trec => epoch_zero);
    epoch_roundtrip := std.env.epoch(trec => local_epoch);
    current_local := std.env.localtime;
    current_utc := std.env.gmtime;
    current_epoch := std.env.epoch;
    calendar_ok <= epoch_zero.microsecond = 0
        and epoch_zero.second = 0
        and epoch_zero.minute = 0
        and epoch_zero.hour = 0
        and epoch_zero.day = 1
        and epoch_zero.month = 0
        and epoch_zero.year = 1970
        and epoch_zero.dayofyear = 0;
    arithmetic_ok <= incremented.microsecond = 500000
        and incremented.second = 30
        and incremented.minute = 1
        and decremented.microsecond = 0
        and decremented.second = 0
        and decremented.minute = 0
        and commuted.microsecond = 500000
        and commuted.second = 30
        and commuted.minute = 1
        and subtracted.microsecond = 0
        and subtracted.second = 0
        and subtracted.minute = 0
        and difference = 90.5;
    conversion_ok <= std.env.time_to_seconds(time_val => 1500 ns)
            = 0.0000015
        and std.env.seconds_to_time(real_val => 0.0000015) = 1500 ns;
    roundtrip_ok <= utc_roundtrip.year = 1970
        and utc_roundtrip.month = 0
        and utc_roundtrip.day = 1
        and utc_roundtrip.hour = 0
        and utc_roundtrip.minute = 0
        and utc_roundtrip.second = 0
        and utc_roundtrip.weekday = epoch_zero.weekday
        and utc_roundtrip.dayofyear = epoch_zero.dayofyear
        and local_roundtrip.year = local_epoch.year
        and local_roundtrip.month = local_epoch.month
        and local_roundtrip.day = local_epoch.day
        and local_roundtrip.hour = local_epoch.hour
        and local_roundtrip.minute = local_epoch.minute
        and local_roundtrip.second = local_epoch.second
        and epoch_roundtrip = 0.0;
    current_ok <= current_local.year >= 2020
        and current_local.month >= 0
        and current_local.month <= 11
        and current_utc.year >= 2020
        and current_utc.month >= 0
        and current_utc.month <= 11
        and current_epoch > 0.0;
    formatted_plain := std.env.to_string(epoch_zero);
    formatted_fraction := std.env.to_string(
        trec => epoch_zero, frac_digits => 6);
    wait for 1 ns;
    std.env.stop(status => 3);
    resumed <= true;
    wait for 1 ns;
    std.env.finish(7);
    escaped_finish <= true;
    wait;
  end process;
end architecture;
)";
    {
        std::ofstream output { source };
        output << source_text;
        assert(output.good());
    }

    const auto parsed = fsim::frontend::parse_text(
        source.generic_string(), source_text,
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(parsed.ok() && parsed.design.units.size() == 2);
    const auto& statements = parsed.design.units.back().processes.back().statements;
    assert(std::ranges::count_if(statements, [](const auto& statement) {
        return statement.procedure_name == "std.env.stop";
    }) == 1);
    assert(std::ranges::count_if(statements, [](const auto& statement) {
        return statement.procedure_name == "std.env.finish";
    }) == 1);

    const auto old_profile = fsim::frontend::parse_text(
        source.generic_string(), source_text,
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2008);
    assert(!old_profile.ok());
    assert(std::ranges::any_of(
        old_profile.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-003";
        }));

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto config = simulator_api_config(
            directory, source, optimization);
        const auto reference = run_simulator_api(
            config, fsim::app::SimulationEngine::interpreter);
        const auto cold = run_simulator_api(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_simulator_api(
            config, fsim::app::SimulationEngine::compiled);
        const auto debug = optimization == fsim::project::Optimization::o0
            ? std::optional<SimulatorApiCapture> { run_simulator_api(
                  config, fsim::app::SimulationEngine::debug) }
            : std::nullopt;
        const auto portable = optimization == fsim::project::Optimization::o0
            ? std::optional<SimulatorApiCapture> { run_simulator_api(
                  config, fsim::app::SimulationEngine::interpreter, true) }
            : std::nullopt;
        for (const auto* capture : {
                 &reference, &cold, &warm }) {
            assert(capture->paused.status == fsim::runtime::RunStatus::stopped);
            assert(capture->paused.time == 1);
            assert(capture->paused.simulator_status == 3);
            assert((capture->paused_values
                == std::array<std::string, 8> {
                    "1", "0", "0", "1", "1", "1", "1", "1" }));
            assert(capture->finished.status == fsim::runtime::RunStatus::stopped);
            assert(capture->finished.time == 2);
            assert(capture->finished.simulator_status == 7);
            assert((capture->finished_values
                == std::array<std::string, 8> {
                    "1", "1", "0", "1", "1", "1", "1", "1" }));
            assert((capture->formatted_values
                == std::array<std::string, 2> {
                    "1970-01-01T00:00:00.000000",
                    "1970-01-01T00:00:00" }));
        }
        if (debug) {
            assert(debug->paused.simulator_status == 3);
            assert(debug->finished.simulator_status == 7);
            assert(debug->paused_values == reference.paused_values);
            assert(debug->finished_values == reference.finished_values);
        }
        if (portable) {
            assert(portable->paused_values == reference.paused_values);
            assert(portable->finished_values == reference.finished_values);
            assert(portable->formatted_values == reference.formatted_values);
        }
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes == 1);
        assert(cold.compiled_modules == 1);
        assert(cold.native_cache.misses == 1);
        assert(warm.native_cache.hits == 1);
#else
        assert(cold.compiled_processes == 0);
        assert(warm.compiled_processes == 0);
#endif
    }

    const auto malformed_source = directory / "simulator-api-malformed.vhd";
    {
        std::ofstream output { malformed_source };
        output << R"(
entity simulator_api is end entity;
architecture rtl of simulator_api is
begin
  process
  begin
    std.env.finish(1, 2);
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    fsim::diagnostic::Engine malformed_diagnostics;
    const auto malformed = fsim::app::build_project(
        simulator_api_config(
            directory, malformed_source, fsim::project::Optimization::o0),
        malformed_diagnostics);
    assert(!malformed);
    assert(std::ranges::any_of(
        malformed_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHENV-002";
        }));

    const auto wrong_type_source = directory / "data-time-wrong-type.vhd";
    {
        std::ofstream output { wrong_type_source };
        output << R"(
entity simulator_api is end entity;
architecture rtl of simulator_api is
begin
  process
    variable value : std.env.time_record;
  begin
    value := std.env.gmtime(timer => 1 ns);
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    fsim::diagnostic::Engine wrong_type_diagnostics;
    const auto wrong_type = fsim::app::build_project(
        simulator_api_config(
            directory, wrong_type_source, fsim::project::Optimization::o0),
        wrong_type_diagnostics);
    assert(!wrong_type);
    assert(std::ranges::any_of(
        wrong_type_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-VHENV-003";
        }));

    const auto expect_runtime_failure = [&directory](
                                            const std::string_view stem,
                                            const std::string_view body,
                                            const std::string_view expected) {
        const auto path = directory / (std::string { stem } + ".vhd");
        {
            std::ofstream output { path };
            output << "entity simulator_api is end entity;\n"
                      "architecture rtl of simulator_api is\n"
                      "begin\n  process\n"
                   << body
                   << "\n  end process;\nend architecture;\n";
            assert(output.good());
        }
        std::optional<std::string> reference;
        for (const auto engine : {
                 fsim::app::SimulationEngine::interpreter,
                 fsim::app::SimulationEngine::compiled }) {
            fsim::diagnostic::Engine diagnostics;
            auto project = fsim::app::build_project(
                simulator_api_config(
                    directory, path, fsim::project::Optimization::o0),
                diagnostics);
            if (!project || diagnostics.has_error()) {
                for (const auto& diagnostic : diagnostics.diagnostics()) {
                    std::cerr << diagnostic.code << ": "
                              << diagnostic.message << '\n';
                }
            }
            assert(project && !diagnostics.has_error());
            if (engine != fsim::app::SimulationEngine::interpreter) {
                project->compiled_process_selection
                    = fsim::app::BuiltProject::CompiledProcessSelection::all;
            }
            fsim::app::Simulation simulation {
                std::move(*project), 1000, engine
            };
            if (engine != fsim::app::SimulationEngine::interpreter) {
                simulation.await_all_native_compilation();
            }
            try {
                (void)simulation.run();
                assert(false && "STD.ENV data/time runtime failure was not reported");
            } catch (const fsim::runtime::simir::InterpreterError& error) {
                const auto message = std::string { error.what() };
                if (message.find(expected) == std::string::npos) {
                    std::cerr << "expected STD.ENV runtime failure containing '"
                              << expected << "' but received '" << message
                              << "'\n";
                }
                assert(message.find(expected) != std::string::npos);
                if (reference) {
                    assert(message == *reference);
                } else {
                    reference = message;
                }
            }
        }
    };
    expect_runtime_failure(
        "data-time-fraction-range",
        R"(    variable value : std.env.time_record;
    variable text : string := "0000000000000000000";
  begin
    value := std.env.gmtime(0.0);
    text := std.env.to_string(value, 7);
    wait;)",
        "STD.ENV TO_STRING FRAC_DIGITS must be in 0 through 6");
    expect_runtime_failure(
        "data-time-negative-seconds",
        R"(    variable value : time;
  begin
    value := std.env.seconds_to_time(-1.0);
    wait;)",
        "STD.ENV SECONDS_TO_TIME result is outside TIME'LOW through TIME'HIGH");
}

void verify_vhdl2019_directory_api(const std::filesystem::path& directory)
{
    const auto source = directory / "directory-api.vhd";
    {
        std::ofstream output { source, std::ios::binary };
        output << R"(
entity directory_api is end entity;
architecture rtl of directory_api is
begin
  model : process
    variable dir : std.env.directory;
    variable opened : std.env.dir_open_status;
    variable created : std.env.dir_create_status;
    variable deleted : std.env.dir_delete_status;
    variable file_deleted : std.env.file_delete_status;
  begin
    assert std.env.dir_separator = std.env.dir_separator severity failure;
    opened := std.env.dir_open(dir, "../outside");
    assert opened = status_access_denied severity failure;
    opened := std.env.dir_open(dir, "missing");
    assert opened = status_not_found severity failure;
    opened := std.env.dir_open(dir, "fixture/a.txt");
    assert opened = status_no_directory severity failure;
    std.env.dir_open(dir, "fixture", opened);
    assert opened = status_ok severity failure;
    assert dir.name.all = dir.name.all severity failure;
    assert dir.items.all(0).all = "a.txt" severity failure;
    assert dir.items.all(1).all = "b_dir" severity failure;
    assert std.env.dir_itemexists("fixture/a.txt") severity failure;
    assert std.env.dir_itemisfile("fixture/a.txt") severity failure;
    assert std.env.dir_itemisdir("fixture/b_dir") severity failure;
    created := std.env.dir_createdir("fixture");
    assert created = status_item_exists severity failure;
    deleted := std.env.dir_deletedir("fixture");
    assert deleted = status_not_empty severity failure;
    file_deleted := std.env.dir_deletefile("missing.txt");
    assert file_deleted = status_no_file severity failure;
    std.env.dir_close(dir);
    std.env.dir_workingdir("fixture", opened);
    assert opened = status_ok severity failure;
    assert std.env.dir_workingdir = std.env.dir_workingdir severity failure;
    assert std.env.dir_itemexists("a.txt") severity failure;
    std.env.dir_createdir("new_parent/child", true, created);
    assert created = status_ok severity failure;
    created := std.env.dir_createdir("new_leaf");
    assert created = status_ok severity failure;
    std.env.dir_deletedir("new_leaf", deleted);
    assert deleted = status_ok severity failure;
    deleted := std.env.dir_deletedir("new_parent", true);
    assert deleted = status_ok severity failure;
    std.env.dir_deletefile("a.txt", file_deleted);
    assert file_deleted = status_ok severity failure;
    std.env.dir_deletedir("b_dir", deleted);
    assert deleted = status_ok severity failure;
    opened := std.env.dir_workingdir("..");
    assert opened = status_ok severity failure;
    deleted := std.env.dir_deletedir("fixture");
    assert deleted = status_ok severity failure;
    std.env.finish(0);
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    const auto fixture = directory / "fixture";
    const auto prepare = [&] {
        std::error_code error;
        std::filesystem::remove_all(fixture, error);
        assert(!error);
        std::filesystem::create_directories(fixture / "b_dir");
        std::ofstream file { fixture / "a.txt", std::ios::binary };
        file << "fixture";
        assert(file.good());
    };
    const auto run = [&](const fsim::app::SimulationEngine engine,
                         const fsim::project::Optimization optimization) {
        prepare();
        fsim::project::Config config;
        config.base_directory = directory;
        config.project.name = "directory_api";
        config.project.top = "vhdl:work.directory_api(rtl)";
        config.project.time_resolution = "1ns";
        config.build.jobs = 8;
        config.build.optimization = optimization;
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "directory-cache-o0" : "directory-cache-o2");
        config.run.max_deltas = 1000;
        fsim::project::SourceSet sources;
        sources.language = fsim::project::Language::vhdl;
        sources.standard = "2019";
        sources.library = "work";
        sources.compilation_unit = "file";
        sources.files = { source };
        config.source_sets.push_back(std::move(sources));
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        if (!project) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": "
                          << diagnostic.message << '\n';
            }
        }
        assert(project && !diagnostics.has_error());
        fsim::diagnostic::Engine artifact_diagnostics;
        const auto artifact = fsim::app::serialize_runtime_state(
            project->design, artifact_diagnostics);
        assert(artifact && !artifact_diagnostics.has_error());
        const auto restored = fsim::app::deserialize_runtime_state(
            *artifact, "directory-api-runtime.bin", artifact_diagnostics);
        assert(restored && !artifact_diagnostics.has_error());
        assert(std::ranges::any_of(
            restored->processes(), [](const auto& process) {
                return std::ranges::any_of(
                    process.operations, [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::VhdlEnvironmentDirectory>(
                            operation);
                    });
            }));
        if (engine != fsim::app::SimulationEngine::interpreter) {
            project->compiled_process_selection
                = fsim::app::BuiltProject::CompiledProcessSelection::all;
        }
        fsim::app::Simulation simulation {
            std::move(*project), config.run.max_deltas, engine };
        if (engine != fsim::app::SimulationEngine::interpreter) {
            simulation.await_all_native_compilation();
            assert(simulation.compiled_process_count() != 0U);
        }
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::stopped);
        assert(result.simulator_status == 0);
        assert(!std::filesystem::exists(fixture));
    };
    const auto source_text = [&] {
        std::ifstream input { source, std::ios::binary };
        return std::string {
            std::istreambuf_iterator<char> { input },
            std::istreambuf_iterator<char> { } };
    }();
    const auto old_profile = fsim::frontend::parse_text(
        source.generic_string(), source_text,
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2008);
    assert(!old_profile.ok());
    assert(std::ranges::any_of(
        old_profile.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-003";
        }));
    const auto malformed_source = directory / "directory-api-malformed.vhd";
    {
        std::ofstream output { malformed_source, std::ios::binary };
        output << R"(
entity directory_api is end entity;
architecture rtl of directory_api is
begin
  process
    variable dir : std.env.directory;
  begin
    std.env.dir_open(dir, "fixture");
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    fsim::project::Config malformed_config;
    malformed_config.base_directory = directory;
    malformed_config.project.name = "directory_api";
    malformed_config.project.top = "vhdl:work.directory_api(rtl)";
    malformed_config.build.jobs = 8;
    fsim::project::SourceSet malformed_sources;
    malformed_sources.language = fsim::project::Language::vhdl;
    malformed_sources.standard = "2019";
    malformed_sources.library = "work";
    malformed_sources.compilation_unit = "file";
    malformed_sources.files = { malformed_source };
    malformed_config.source_sets.push_back(std::move(malformed_sources));
    fsim::diagnostic::Engine malformed_diagnostics;
    assert(!fsim::app::build_project(
        malformed_config, malformed_diagnostics));
    assert(std::ranges::any_of(
        malformed_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHENV-003";
        }));

    const auto wrong_type_source = directory / "directory-api-wrong-type.vhd";
    {
        std::ofstream output { wrong_type_source, std::ios::binary };
        output << R"(
entity directory_api is end entity;
architecture rtl of directory_api is
begin
  process
    variable not_dir : integer;
    variable opened : std.env.dir_open_status;
  begin
    std.env.dir_open(not_dir, "fixture", opened);
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    auto wrong_type_config = malformed_config;
    wrong_type_config.source_sets.front().files = { wrong_type_source };
    fsim::diagnostic::Engine wrong_type_diagnostics;
    assert(!fsim::app::build_project(
        wrong_type_config, wrong_type_diagnostics));
    assert(std::ranges::any_of(
        wrong_type_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-VHENV-006";
        }));
    run(fsim::app::SimulationEngine::interpreter,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::debug,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o2);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o2);
}

void verify_vhdl2019_environment_api(const std::filesystem::path& directory)
{
    constexpr std::string_view value_name {
        "FSIM_VHDL2019_ENVIRONMENT_API_VALUE" };
    constexpr std::string_view missing_name {
        "FSIM_VHDL2019_ENVIRONMENT_API_MISSING" };
    constexpr std::string_view oversized_name {
        "FSIM_VHDL2019_ENVIRONMENT_API_OVERSIZED" };
    const ScopedEnvironment value_environment {
        std::string { value_name }, std::string { "controlled-value" } };
    const ScopedEnvironment missing_environment {
        std::string { missing_name }, std::nullopt };

    const auto source = directory / "environment-api.vhd";
    const auto source_text = std::string { R"FSIM(
entity environment_api is end entity;
architecture rtl of environment_api is
  function capture_call_path(ignore : integer) return string is
  begin
    return std.env.to_string(
        call_path => std.env.get_call_path,
        separator => "|");
  end function;
begin
  model : process
    variable line_value : std.textio.line;
    variable call_path : std.textio.line;
    variable saved_path : std.env.call_path_vector_ptr;
  begin
    assert std.env.getenv(
        name => "FSIM_VHDL2019_ENVIRONMENT_API_VALUE")
        = "controlled-value" severity failure;
    line_value := std.env.getenv(
        "FSIM_VHDL2019_ENVIRONMENT_API_VALUE");
    assert line_value.all = "controlled-value" severity failure;
    assert std.env.getenv(
        "FSIM_VHDL2019_ENVIRONMENT_API_VALUE").all
        = "controlled-value" severity failure;
    assert std.env.getenv(
        "FSIM_VHDL2019_ENVIRONMENT_API_MISSING") = "" severity failure;
    assert std.env.vhdl_version = "2019" severity failure;
    assert std.env.tool_type = "SIMULATION" severity failure;
    assert std.env.tool_vendor = "fsim project" severity failure;
    assert std.env.tool_name = "fsim" severity failure;
    assert std.env.tool_edition = "community" severity failure;
    assert std.env.tool_version = ")FSIM" }
        + std::string { fsim::version }
        + R"FSIM(" severity failure;
    assert std.env.file_name = "environment-api.vhd" severity failure;
    assert std.env.file_path = ")FSIM"
        + source.lexically_normal().generic_string()
        + R"FSIM(" severity failure;
    assert std.env.file_line > 0 severity failure;
    call_path := capture_call_path(0);
    assert call_path.all /= "" severity failure;
    saved_path := std.env.get_call_path;
    assert saved_path.all(0).name.all /= "" severity failure;
    assert saved_path.all(0).file_name.all = "environment-api.vhd"
        severity failure;
    assert saved_path.all(0).file_path.all = ")FSIM"
        + source.lexically_normal().generic_string()
        + R"FSIM(" severity failure;
    assert saved_path.all(0).file_line > 0 severity failure;
    line_value := std.env.to_string(saved_path.all(0));
    assert line_value.all /= "" severity failure;
    line_value := std.env.to_string(saved_path, "#");
    assert line_value.all /= "" severity failure;
    line_value := std.env.to_string(saved_path.all, "~");
    assert line_value.all /= "" severity failure;
    std.env.finish(0);
    wait;
  end process;
end architecture;
)FSIM";
    {
        std::ofstream output { source, std::ios::binary };
        output << source_text;
        assert(output.good());
    }

    const auto make_environment_config = [&](
        const std::filesystem::path& input,
        const fsim::project::Optimization optimization) {
        fsim::project::Config config;
        config.base_directory = directory;
        config.project.name = "environment_api";
        config.project.top = "vhdl:work.environment_api(rtl)";
        config.project.time_resolution = "1ns";
        config.build.jobs = 8;
        config.build.optimization = optimization;
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "environment-cache-o0" : "environment-cache-o2");
        config.run.max_deltas = 1000;
        fsim::project::SourceSet sources;
        sources.language = fsim::project::Language::vhdl;
        sources.standard = "2019";
        sources.library = "work";
        sources.compilation_unit = "file";
        sources.files = { input };
        config.source_sets.push_back(std::move(sources));
        return config;
    };
    const auto run = [&](const fsim::app::SimulationEngine engine,
                         const fsim::project::Optimization optimization) {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(
            make_environment_config(source, optimization), diagnostics);
        if (!project) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": "
                          << diagnostic.message << '\n';
            }
        }
        assert(project && !diagnostics.has_error());
        fsim::diagnostic::Engine artifact_diagnostics;
        const auto artifact = fsim::app::serialize_runtime_state(
            project->design, artifact_diagnostics);
        assert(artifact && !artifact_diagnostics.has_error());
        const auto restored = fsim::app::deserialize_runtime_state(
            *artifact, "environment-api-runtime.bin", artifact_diagnostics);
        assert(restored && !artifact_diagnostics.has_error());
        assert(std::ranges::any_of(
            restored->processes(), [](const auto& process) {
                return std::ranges::any_of(
                    process.operations, [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::VhdlEnvironmentGetenv>(
                            operation);
                    });
            }));
        assert(std::ranges::any_of(
            restored->processes(), [](const auto& process) {
                return std::ranges::any_of(
                    process.operations, [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::VhdlEnvironmentGetCallPath>(
                            operation);
                    });
            }));
        assert(std::ranges::any_of(
            restored->processes(), [](const auto& process) {
                return std::ranges::any_of(
                    process.operations, [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::VhdlEnvironmentCallPath>(
                            operation);
                    });
            }));
        if (engine != fsim::app::SimulationEngine::interpreter) {
            project->compiled_process_selection
                = fsim::app::BuiltProject::CompiledProcessSelection::all;
        }
        fsim::app::Simulation simulation {
            std::move(*project), 1000, engine };
        if (engine != fsim::app::SimulationEngine::interpreter) {
            simulation.await_all_native_compilation();
            assert(simulation.compiled_process_count() != 0U);
        }
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::stopped);
        assert(result.simulator_status == 0);
    };

    const auto old_profile = fsim::frontend::parse_text(
        source.generic_string(), source_text,
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2008);
    assert(!old_profile.ok());
    assert(std::ranges::any_of(
        old_profile.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-003";
        }));

    const auto malformed_source = directory / "environment-api-malformed.vhd";
    {
        std::ofstream output { malformed_source, std::ios::binary };
        output << R"(
entity environment_api is end entity;
architecture rtl of environment_api is
begin
  process
  begin
    assert std.env.tool_name("unexpected") = "fsim";
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    fsim::diagnostic::Engine malformed_diagnostics;
    assert(!fsim::app::build_project(
        make_environment_config(
            malformed_source, fsim::project::Optimization::o0),
        malformed_diagnostics));
    assert(std::ranges::any_of(
        malformed_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHENV-002";
        }));

    const auto wrong_type_source = directory
        / "environment-api-wrong-type.vhd";
    {
        std::ofstream output { wrong_type_source, std::ios::binary };
        output << R"(
entity environment_api is end entity;
architecture rtl of environment_api is
begin
  process
  begin
    assert std.env.getenv(1) = "";
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    fsim::diagnostic::Engine wrong_type_diagnostics;
    assert(!fsim::app::build_project(
        make_environment_config(
            wrong_type_source, fsim::project::Optimization::o0),
        wrong_type_diagnostics));
    assert(std::ranges::any_of(
        wrong_type_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-VHENV-004";
        }));

    run(fsim::app::SimulationEngine::interpreter,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::debug,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o0);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o2);
    run(fsim::app::SimulationEngine::compiled,
        fsim::project::Optimization::o2);

    const ScopedEnvironment oversized_environment {
        std::string { oversized_name }, std::string(4097U, 'x') };
    const auto oversized_source = directory / "environment-api-oversized.vhd";
    {
        std::ofstream output { oversized_source, std::ios::binary };
        output << R"(
entity environment_api is end entity;
architecture rtl of environment_api is
begin
  process
  begin
    assert std.env.getenv(
        "FSIM_VHDL2019_ENVIRONMENT_API_OVERSIZED") = "";
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    std::optional<std::string> reference_failure;
    for (const auto engine : {
             fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::compiled }) {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(
            make_environment_config(
                oversized_source, fsim::project::Optimization::o0),
            diagnostics);
        assert(project && !diagnostics.has_error());
        if (engine == fsim::app::SimulationEngine::compiled) {
            project->compiled_process_selection
                = fsim::app::BuiltProject::CompiledProcessSelection::all;
        }
        fsim::app::Simulation simulation {
            std::move(*project), 1000, engine };
        if (engine == fsim::app::SimulationEngine::compiled) {
            simulation.await_all_native_compilation();
        }
        try {
            (void)simulation.run();
            assert(false && "oversized STD.ENV GETENV result was accepted");
        } catch (const fsim::runtime::simir::InterpreterError& error) {
            const auto message = std::string { error.what() };
            assert(message.find(
                "STD.ENV GETENV result exceeds the bounded string limit")
                != std::string::npos);
            if (reference_failure) {
                assert(message == *reference_failure);
            } else {
                reference_failure = message;
            }
        }
    }
}

struct AssertApiCapture {
    fsim::runtime::RunResult result;
    std::string passed;
    std::vector<std::string> reports;
    std::size_t compiled_processes { };
    fsim::app::NativeCacheStatistics native_cache;
};

void verify_vhdl2019_assert_api(const std::filesystem::path& directory)
{
    const auto source = directory / "assert-api.vhd";
    constexpr std::string_view source_text = R"(
use std.textio.all;
entity assert_api is end entity;
architecture rtl of assert_api is
  signal passed : boolean := false;
begin
  control : process
    variable valid : boolean := false;
    variable input_line : line;
    variable read_value : integer := 0;
  begin
    assert not std.env.IsVhdlAssertFailed severity failure;
    assert std.env.GetVhdlAssertCount = 0 severity failure;
    assert std.env.GetVhdlAssertEnable severity failure;
    assert std.env.GetVhdlReadSeverity = error severity failure;
    std.env.SetVhdlAssertEnable(warning, false);
    report "hidden warning" severity warning;
    assert std.env.GetVhdlAssertCount(level => warning) = 0 severity failure;
    std.env.SetVhdlAssertEnable(Level => warning);
    std.env.SetVhdlAssertFormat(
        warning, "{S}:{r}:{i}:{t.ns}", valid);
    assert valid severity failure;
    assert std.env.GetVhdlAssertFormat(warning)
        = "{S}:{r}:{i}:{t.ns}" severity failure;
    report "visible warning" severity warning;
    assert std.env.GetVhdlAssertCount(warning) = 1 severity failure;
    assert std.env.IsVhdlAssertFailed(warning) severity failure;
    assert std.env.IsVhdlAssertFailed severity failure;
    read(input_line, read_value);
    assert std.env.GetVhdlAssertCount(error) = 1 severity failure;
    std.env.SetVhdlAssertEnable(failure, false);
    report "suppressed failure" severity failure;
    std.env.SetVhdlAssertEnable(failure, true);
    assert std.env.GetVhdlAssertCount(failure) = 0 severity failure;
    std.env.ClearVhdlAssert;
    assert std.env.GetVhdlAssertCount = 0 severity failure;
    std.env.SetVhdlReadSeverity;
    assert std.env.GetVhdlReadSeverity = failure severity failure;
    std.env.SetVhdlAssertFormat(failure, "{unknown}", valid);
    assert not valid severity failure;
    assert std.env.GetVhdlAssertFormat(failure) = "{r}" severity failure;
    passed <= true;
    wait for 1 ns;
    std.env.finish(0);
    wait;
  end process;
end architecture;
)";
    {
        std::ofstream output { source, std::ios::binary };
        output << source_text;
        assert(output.good());
    }

    const auto parsed = fsim::frontend::parse_text(
        source.generic_string(), source_text,
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(parsed.ok());
    const auto old_profile = fsim::frontend::parse_text(
        source.generic_string(), source_text,
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2008);
    assert(!old_profile.ok());
    assert(std::ranges::any_of(
        old_profile.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-003";
        }));

    const auto config_for = [&](const fsim::project::Optimization optimization) {
        auto config = simulator_api_config(directory, source, optimization);
        config.project.name = "assert_api";
        config.project.top = "vhdl:work.assert_api(rtl)";
        config.build.cache_path = directory
            / (optimization == fsim::project::Optimization::o0
                    ? "assert-api-cache-o0" : "assert-api-cache-o2");
        return config;
    };
    const auto run = [&](const fsim::project::Config& config,
                         const fsim::app::SimulationEngine engine,
                         const bool portable = false) {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        if (!project || diagnostics.has_error()) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": "
                          << diagnostic.message << '\n';
            }
        }
        assert(project && !diagnostics.has_error());
        const auto api_operations = std::ranges::count_if(
            project->design.processes().front().operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::VhdlAssertApi>(operation);
            });
        assert(api_operations >= 15);
        if (portable) {
            const auto bytes = fsim::app::serialize_runtime_state(
                project->design, diagnostics);
            auto restored = bytes
                ? fsim::app::deserialize_runtime_state(
                      *bytes, "assert-api-runtime", diagnostics)
                : std::nullopt;
            assert(restored && !diagnostics.has_error());
            assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
                == bytes);
            project->design = std::move(*restored);
        }
        if (engine == fsim::app::SimulationEngine::compiled) {
            project->compiled_process_selection
                = fsim::app::BuiltProject::CompiledProcessSelection::all;
        }
        fsim::app::Simulation simulation {
            std::move(*project), config.run.max_deltas, engine };
        if (engine == fsim::app::SimulationEngine::compiled) {
            simulation.await_all_native_compilation();
        }
        AssertApiCapture capture;
        capture.compiled_processes = simulation.compiled_process_count();
        simulation.set_report_hook(
            [&](const auto, const std::string_view message,
                const auto, const auto& location, const auto, const auto) {
                capture.reports.emplace_back(message);
                assert(!location.path.empty());
                assert(location.line != 0U);
                assert(location.column != 0U);
            });
        const auto passed = simulation.find_signal("assert_api.passed");
        assert(passed);
        capture.result = simulation.run();
        capture.passed = simulation.read_signal(*passed).to_msb_string();
        capture.native_cache = simulation.native_cache_statistics();
        return capture;
    };

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto config = config_for(optimization);
        const auto reference = run(
            config, fsim::app::SimulationEngine::interpreter);
        const auto cold = run(config, fsim::app::SimulationEngine::compiled);
        const auto warm = run(config, fsim::app::SimulationEngine::compiled);
        const auto debug = optimization == fsim::project::Optimization::o0
            ? std::optional<AssertApiCapture> {
                  run(config, fsim::app::SimulationEngine::debug) }
            : std::nullopt;
        const auto portable = optimization == fsim::project::Optimization::o0
            ? std::optional<AssertApiCapture> {
                  run(config, fsim::app::SimulationEngine::interpreter, true) }
            : std::nullopt;
        for (const auto* capture : { &reference, &cold, &warm }) {
            assert(capture->result.status == fsim::runtime::RunStatus::stopped);
            assert(capture->result.simulator_status == 0);
            assert(capture->passed == "1");
            assert(capture->reports.size() == 2U);
            assert(capture->reports[0].find(
                "WARNING:visible warning:assert_api.control:0 ns")
                != std::string::npos);
            assert(capture->reports[1].find(
                "VHDL TextIO read did not convert the requested element")
                != std::string::npos);
        }
        if (debug) {
            assert(debug->passed == reference.passed);
            assert(debug->reports == reference.reports);
        }
        if (portable) {
            assert(portable->passed == reference.passed);
            assert(portable->reports == reference.reports);
        }
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes == 1U);
        assert(cold.native_cache.misses == 1U);
        assert(warm.native_cache.hits == 1U);
#else
        assert(cold.compiled_processes == 0U);
        assert(warm.compiled_processes == 0U);
#endif
    }

    const auto malformed_source = directory / "assert-api-malformed.vhd";
    {
        std::ofstream output { malformed_source, std::ios::binary };
        output << R"(
entity assert_api is end entity;
architecture rtl of assert_api is
begin
  process
  begin
    assert std.env.GetVhdlAssertFormat = "" severity failure;
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    fsim::diagnostic::Engine malformed_diagnostics;
    assert(!fsim::app::build_project(
        [&] {
            auto config = config_for(fsim::project::Optimization::o0);
            config.source_sets.front().files = { malformed_source };
            return config;
        }(),
        malformed_diagnostics));
    assert(std::ranges::any_of(
        malformed_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHENV-002";
        }));

    const auto invalid_source = directory / "assert-api-invalid-format.vhd";
    {
        std::ofstream output { invalid_source, std::ios::binary };
        output << R"(
entity assert_api is end entity;
architecture rtl of assert_api is
begin
  process
  begin
    std.env.SetVhdlAssertFormat(failure, "{invalid}");
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    for (const auto engine : {
             fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::compiled }) {
        auto config = config_for(fsim::project::Optimization::o0);
        config.source_sets.front().files = { invalid_source };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        assert(project && !diagnostics.has_error());
        if (engine == fsim::app::SimulationEngine::compiled) {
            project->compiled_process_selection
                = fsim::app::BuiltProject::CompiledProcessSelection::all;
        }
        fsim::app::Simulation simulation {
            std::move(*project), config.run.max_deltas, engine };
        if (engine == fsim::app::SimulationEngine::compiled) {
            simulation.await_all_native_compilation();
        }
        try {
            (void)simulation.run();
            assert(false && "invalid VHDL assert format was accepted");
        } catch (const fsim::runtime::simir::AssertionError& error) {
            assert(std::string { error.what() }.find(
                "invalid VHDL assert format") != std::string::npos);
        }
    }
}

void verify_vhdl2019_governed_packages(
    const std::filesystem::path& directory)
{
    const auto source = directory / "governed-packages-2019.vhd";
    {
        std::ofstream output { source, std::ios::binary };
        output << R"(
library ieee;
use ieee.numeric_bit_unsigned.all;
use ieee.numeric_std_unsigned.all;
use ieee.math_complex.all;
use std.env.all;
use std.reflection.all;
entity governed_packages_2019 is end entity;
architecture rtl of governed_packages_2019 is
begin
end architecture;
)";
        assert(output.good());
    }

    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "governed_packages_2019";
    config.project.top = "vhdl:work.governed_packages_2019(rtl)";
    config.project.time_resolution = "1ns";
    config.build.jobs = 8;
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2019";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::check_project(config, diagnostics);
    if (!checked) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(checked && !diagnostics.has_error());
    assert(checked->standard_sources.size() == 18U);
    std::vector<std::string> source_paths;
    for (const auto& standard_source : checked->standard_sources) {
        source_paths.push_back(standard_source.path.generic_string());
        assert(standard_source.content_digest.size() == 64U);
        assert(!standard_source.backing_path.empty());
    }
    std::ranges::sort(source_paths);
    assert((source_paths == std::vector<std::string> {
        "fsim-standard/ieee/math_complex-body.vhdl",
        "fsim-standard/ieee/math_complex.vhdl",
        "fsim-standard/ieee/math_real-body.vhdl",
        "fsim-standard/ieee/math_real.vhdl",
        "fsim-standard/ieee/numeric_bit-body.vhdl",
        "fsim-standard/ieee/numeric_bit.vhdl",
        "fsim-standard/ieee/numeric_bit_unsigned-body.vhdl",
        "fsim-standard/ieee/numeric_bit_unsigned.vhdl",
        "fsim-standard/ieee/numeric_std-body.vhdl",
        "fsim-standard/ieee/numeric_std.vhdl",
        "fsim-standard/ieee/numeric_std_unsigned-body.vhdl",
        "fsim-standard/ieee/numeric_std_unsigned.vhdl",
        "fsim-standard/ieee/std_logic_1164-body.vhdl",
        "fsim-standard/ieee/std_logic_1164.vhdl",
        "fsim-standard/std/env.vhdl",
        "fsim-standard/std/reflection.vhdl",
        "fsim-standard/std/standard.vhdl",
        "fsim-standard/std/textio.vhdl",
    }));

    std::vector<std::string> projected_packages;
    for (const auto& unit : checked->parsed.units) {
        if (unit.kind == fsim::frontend::UnitKind::VhdlPackage
            && unit.library == "ieee" && unit.primary_name.empty()
            && !unit.standard_package_revision.empty()) {
            projected_packages.push_back(unit.name);
        }
    }
    assert((projected_packages == std::vector<std::string> {
        "std_logic_1164", "numeric_bit", "numeric_bit_unsigned",
        "numeric_std", "numeric_std_unsigned", "math_real",
        "math_complex" }));

    auto project = fsim::app::build_project(config, diagnostics);
    assert(project && !diagnostics.has_error());
    assert(!project->vhdl_unit_provenance.empty());
    const auto provenance = std::ranges::max_element(
        project->vhdl_unit_provenance, {}, [](const auto& item) {
            return item.package_dependencies.size();
        });
    assert(provenance != project->vhdl_unit_provenance.end());
    const auto& dependencies = provenance->package_dependencies;
    std::vector<std::string> dependency_names;
    for (const auto& dependency : dependencies) {
        dependency_names.push_back(dependency.package);
        assert(dependency.standard == "2019");
        assert(dependency.predefined_environment
            == "ieee-1076-standard:2019:fsim-v3");
        assert(dependency.revision
            == "ieee-p1076:1076-2019:"
               "16a012320947d378611cc7457f64ed76cb52bac4");
        assert(dependency.source_digest.size() == 64U);
    }
    assert((dependency_names == std::vector<std::string> {
        "ieee.math_complex", "ieee.numeric_bit_unsigned",
        "ieee.numeric_std_unsigned", "std.env", "std.reflection" }));
}
