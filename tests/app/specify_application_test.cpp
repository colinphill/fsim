// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator(
                 path, error),
            end;
            !error && iterator != end; iterator.increment(error)) {
            std::filesystem::permissions(
                iterator->path(),
                std::filesystem::perms::owner_all,
                std::filesystem::perm_options::add,
                error);
            error.clear();
        }
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add,
            error);
        std::filesystem::remove_all(path, error);
    }
};

struct Report {
    std::string message;
    fsim::runtime::SimulationTick time { };

    friend bool operator==(const Report&, const Report&) = default;
};

struct Capture {
    fsim::runtime::RunResult run;
    std::string native_result;
    std::string wide_result;
    std::string mixed_result;
    std::string forced_result;
    std::string released_result;
    std::vector<Report> reports;
    std::vector<std::tuple<
        fsim::runtime::simir::SignalId,
        std::string,
        fsim::runtime::SimulationTick>>
        changes;
    std::string vcd;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics cache;
    std::vector<std::string> path_identities;
    std::vector<std::string> timing_check_identities;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& systemverilog,
    const std::filesystem::path& vhdl,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "specify-integration";
    config.project.time_resolution = "1ns";
    config.project.tops = {
        { "sv:work.native_root", "native" },
        { "sv:work.mixed_root", "mixed" }
    };
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "cache-o0"
                : "cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sv_sources;
    sv_sources.language = fsim::project::Language::system_verilog;
    sv_sources.standard = "2017";
    sv_sources.library = "work";
    sv_sources.files = { systemverilog };
    config.source_sets.push_back(std::move(sv_sources));
    fsim::project::SourceSet vhdl_sources;
    vhdl_sources.language = fsim::project::Language::vhdl;
    vhdl_sources.standard = "2008";
    vhdl_sources.library = "work";
    vhdl_sources.files = { vhdl };
    config.source_sets.push_back(std::move(vhdl_sources));
    return config;
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation {
        std::move(project), 1000, engine
    };
    const auto native = simulation.find_signal("native.result");
    const auto wide = simulation.find_signal("native.wide_result");
    const auto mixed = simulation.find_signal("mixed.result");
    if (!native || !wide || !mixed) {
        for (const auto& signal : simulation.design().signals()) {
            std::cerr << "specify signal: " << signal.name << '\n';
        }
    }
    assert(native && wide && mixed);

    Capture capture;
    for (const auto& path : simulation.design().verilog_specify_paths()) {
        capture.path_identities.push_back(path.identity);
    }
    for (const auto& check : simulation.design().verilog_timing_checks()) {
        capture.timing_check_identities.push_back(check.identity);
    }
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.cache = simulation.native_cache_statistics();
    simulation.set_report_hook(
        [&](const fsim::runtime::simir::ProcessId,
            const std::string_view message,
            const fsim::runtime::simir::AssertionSeverity severity,
            const fsim::runtime::simir::SourceLocation&,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t) {
            assert(severity
                == fsim::runtime::simir::AssertionSeverity::error);
            capture.reports.push_back({ std::string { message }, time });
        });

    std::ostringstream vcd_text;
    fsim::runtime::VcdWriter vcd(vcd_text, "1ns", 64);
    const auto vcd_native = vcd.declare_signal("native.result", 2);
    const auto vcd_wide = vcd.declare_signal("native.wide_result", 137);
    const auto vcd_mixed = vcd.declare_signal("mixed.result", 1);
    vcd.begin(0);
    vcd.change(vcd_native, simulation.read_signal(*native));
    vcd.change(vcd_wide, simulation.read_signal(*wide));
    vcd.change(vcd_mixed, simulation.read_signal(*mixed));
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t) {
            capture.changes.emplace_back(
                signal, value.to_msb_string(), time);
            vcd.set_time(time);
            if (signal == *native)
                vcd.change(vcd_native, value);
            if (signal == *wide)
                vcd.change(vcd_wide, value);
            if (signal == *mixed)
                vcd.change(vcd_mixed, value);
        });

    const auto before_force = simulation.run(3);
    assert(before_force.status == fsim::runtime::RunStatus::time_limit);
    simulation.force_signal(
        *mixed, fsim::runtime::PackedLogic4::from_msb_string("X"));
    assert(simulation.signal_is_forced(*mixed));
    const auto during_force = simulation.run(5);
    assert(during_force.status == fsim::runtime::RunStatus::time_limit);
    capture.forced_result = simulation.read_signal(*mixed).to_msb_string();
    simulation.release_signal(*mixed);
    assert(!simulation.signal_is_forced(*mixed));
    capture.released_result = simulation.read_signal(*mixed).to_msb_string();
    capture.run = simulation.run();
    capture.native_result = simulation.read_signal(*native).to_msb_string();
    capture.wide_result = simulation.read_signal(*wide).to_msb_string();
    capture.mixed_result = simulation.read_signal(*mixed).to_msb_string();
    vcd.flush();
    capture.vcd = vcd_text.str();
    return capture;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project)
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    assert(project);
    assert(project->design.roots()
        == std::vector<std::string>({ "native", "mixed" }));
    assert(project->design.verilog_specify_paths().size() == 7);
    assert(project->design.verilog_timing_checks().size() == 3);
    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, artifact_diagnostics);
    assert(encoded && !artifact_diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "specify-runtime", artifact_diagnostics);
    assert(restored && !artifact_diagnostics.has_error());
    assert(restored->verilog_specify_paths().size() == 7);
    assert(restored->verilog_timing_checks().size() == 3);
    assert(fsim::app::serialize_runtime_state(
               *restored, artifact_diagnostics)
        == encoded);
    auto wrong_schema = *encoded;
    wrong_schema[8] = static_cast<char>(
        fsim::app::kRuntimeStateSchema - 1U);
    fsim::diagnostic::Engine wrong_schema_diagnostics;
    assert(!fsim::app::deserialize_runtime_state(
        wrong_schema, "old-specify-runtime", wrong_schema_diagnostics));
    auto excessive_roots = *encoded;
    std::uint64_t top_size { };
    for (std::size_t byte = 0; byte < 8; ++byte) {
        top_size |= static_cast<std::uint64_t>(
                        static_cast<unsigned char>(excessive_roots[16 + byte]))
            << (byte * 8U);
    }
    const auto roots_size_offset = static_cast<std::size_t>(24 + top_size);
    assert(roots_size_offset + 8 <= excessive_roots.size());
    for (std::size_t byte = 0; byte < 8; ++byte) {
        excessive_roots[roots_size_offset + byte] = '\xff';
    }
    fsim::diagnostic::Engine excessive_roots_diagnostics;
    assert(!fsim::app::deserialize_runtime_state(
        excessive_roots,
        "excessive-specify-runtime",
        excessive_roots_diagnostics));
    project->design = std::move(*restored);
    return execute(std::move(*project), engine);
}

Capture run_relocated_library(
    const fsim::project::Config& config,
    const std::filesystem::path& destination)
{
    fsim::diagnostic::Engine diagnostics;
    const auto exported = fsim::app::export_library(
        config, "work", destination, diagnostics);
    if (!exported)
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    assert(exported && !diagnostics.has_error());
    const auto relocated = destination.parent_path()
        / (destination.filename().string() + "-relocated");
    std::filesystem::rename(destination, relocated);
    auto mapped_config = config;
    mapped_config.source_sets.clear();
    mapped_config.library_mappings.push_back({ "work", relocated });
    mapped_config.build.cache_path = relocated.parent_path()
        / (relocated.filename().string() + "-cache");
    auto project = fsim::app::build_project(mapped_config, diagnostics);
    if (!project)
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    assert(project && !diagnostics.has_error());
    assert(project->design.verilog_specify_paths().size() == 7);
    assert(project->design.verilog_timing_checks().size() == 3);
    return execute(
        std::move(*project), fsim::app::SimulationEngine::interpreter);
}

void compare(const Capture& reference, const Capture& candidate)
{
    assert(reference.run.status == candidate.run.status);
    assert(reference.run.time == candidate.run.time);
    assert(reference.native_result == candidate.native_result);
    assert(reference.wide_result == candidate.wide_result);
    assert(reference.mixed_result == candidate.mixed_result);
    assert(reference.forced_result == candidate.forced_result);
    assert(reference.released_result == candidate.released_result);
    assert(reference.reports == candidate.reports);
    assert(reference.changes == candidate.changes);
    assert(reference.vcd == candidate.vcd);
    assert(reference.path_identities == candidate.path_identities);
    assert(reference.timing_check_identities
        == candidate.timing_check_identities);
}

void replace_source_text(
    const std::filesystem::path& path,
    const std::string_view before,
    const std::string_view after)
{
    std::ifstream input(path, std::ios::binary);
    std::string text {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { }
    };
    const auto position = text.find(before);
    assert(position != std::string::npos);
    text.replace(position, before.size(), after);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    assert(output.good());
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-specify-application-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto systemverilog = directory.path / "specify.sv";
    const auto vhdl = directory.path / "middle.vhd";
    {
        std::ofstream output(systemverilog, std::ios::binary);
        output << R"(
module specify_leaf #(
  parameter DELAY = 2
) (
  input source,
  input clock,
  input enable,
  output wire result
);
  reg notifier;
  specify
    if (enable) (source => result) = DELAY;
    ifnone (source => result) = 1;
    $setup(posedge source &&& enable, posedge clock, 2, notifier);
  endspecify
  assign result = source;
  assign result = source;
endmodule

module wide_specify_leaf (
  input [136:0] source,
  output wire [136:0] result
);
  specify
    (source[136:65] *> result[136:65]) = 3;
  endspecify
  assign result = source;
endmodule

module native_root;
  reg source;
  reg clock;
  reg enable;
  wire [1:0] result;
  reg [136:0] wide_source;
  wire [136:0] wide_result;
  genvar lane;
  generate
    for (lane = 0; lane < 2; lane = lane + 1) begin : lanes
      wire lane_result;
      specify_leaf #(.DELAY(lane + 2)) child(
          source, clock, enable, lane_result);
      assign result[lane] = lane_result;
    end
  endgenerate
  wide_specify_leaf wide_child(wide_source, wide_result);
  initial begin
    source = 0;
    clock = 0;
    enable = 1;
    wide_source = 0;
    #1 begin
      source = 1;
      wide_source = 137'h10000000000000000020000000000000001;
    end
    #1 clock = 1;
    #8 $finish;
  end
endmodule

module mixed_root;
  reg source;
  reg clock;
  reg enable;
  wire result;
  mixed_middle middle(source, clock, enable, result);
  initial begin
    source = 0;
    clock = 0;
    enable = 1;
    #1 source = 1;
    #1 clock = 1;
  end
endmodule
)";
        assert(output.good());
    }
    {
        std::ofstream output(vhdl, std::ios::binary);
        output << R"(
entity mixed_middle is
  port (
    source : in std_logic;
    clock : in std_logic;
    enable : in std_logic;
    result : out std_logic
  );
end entity;

architecture rtl of mixed_middle is
  component specify_leaf is
    generic (DELAY : integer := 3);
    port (
      source : in std_logic;
      clock : in std_logic;
      enable : in std_logic;
      result : out std_logic
    );
  end component;
begin
  child: specify_leaf
    generic map (DELAY => 3)
    port map (source, clock, enable, result);
end architecture;
)";
        assert(output.good());
    }

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto config = make_config(
            directory.path, systemverilog, vhdl, optimization);
        const auto reference = run_once(
            config, fsim::app::SimulationEngine::interpreter);
        const auto compiled = run_once(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_once(
            config, fsim::app::SimulationEngine::compiled);
        const auto debug = run_once(
            config, fsim::app::SimulationEngine::debug);
        const auto mapped = run_relocated_library(
            config,
            directory.path
                / (optimization == fsim::project::Optimization::o0
                        ? "specify-o0.fsimlib"
                        : "specify-o2.fsimlib"));
        compare(reference, compiled);
        compare(reference, warm);
        compare(reference, debug);
        compare(reference, mapped);
        assert(reference.run.status == fsim::runtime::RunStatus::stopped);
        assert(reference.run.time == 10);
        assert(reference.native_result == "11");
        assert(reference.wide_result.size() == 137);
        assert(reference.wide_result.front() == '1');
        assert(reference.wide_result[71] == '1');
        assert(reference.wide_result.back() == '1');
        assert(std::ranges::count(reference.wide_result, '1') == 3);
        assert(reference.mixed_result == "1");
        assert(reference.forced_result == "X");
        assert(reference.released_result == "1");
        assert(reference.reports.size() == 3);
        assert(std::ranges::all_of(
            reference.reports,
            [](const Report& report) {
                return report.message
                    == "Verilog specify timing-check violation"
                    && report.time == 2;
            }));
        assert(reference.vcd.find("#4") != std::string::npos);
        assert(std::ranges::find(
                   reference.path_identities,
                   "sdf:iopath:native.wide_child:0")
            != reference.path_identities.end());
#if defined(FSIM_HAS_LLVM)
        assert(compiled.compiled_processes != 0);
        assert(compiled.compiled_modules != 0);
        assert(compiled.cache.misses == compiled.compiled_modules);
        assert(compiled.cache.stores == compiled.compiled_modules);
        assert(warm.cache.hits == warm.compiled_modules);
        assert(debug.compiled_processes != 0);
#else
        assert(compiled.compiled_processes == 0);
        assert(debug.compiled_processes == 0);
#endif
        replace_source_text(
            systemverilog, "lane + 2", "lane + 4");
        const auto changed = run_once(
            config, fsim::app::SimulationEngine::compiled);
        replace_source_text(
            systemverilog, "lane + 4", "lane + 2");
#if defined(FSIM_HAS_LLVM)
        assert(changed.cache.misses != 0);
#endif
        assert(changed.reports == reference.reports);
        assert(changed.changes != reference.changes);
    }
    std::cout << "specify application tests passed\n";
    return 0;
}
