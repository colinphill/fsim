// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/sdf_drive_timing.hpp"
#include "fsim/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {
constexpr std::string_view path_identity = "sdf:iopath:top.u:A*>Z:path-0";

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

fsim::elaboration::ElaboratedDesign make_drive_design(
    const bool omit_strong_region = false)
{
    using namespace fsim;
    using namespace runtime::simir;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.u.A", "top.u.Z", "top.peer" }) {
        const auto id = static_cast<SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 2U;
        info.type_name = "logic [1:0]";
        info.source_domain = frontend::ValueDomain::Logic4;
        if (id != 0U)
            info.resolution = ResolutionKind::sv_wire;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(std::string { name },
            runtime::PackedLogic4::from_msb_string(
                id == 0U ? "00" : "ZZ"),
            id == 0U ? ResolutionKind::none : ResolutionKind::sv_wire);
        state.signal_names.emplace_back(name, id);
    }

    Process strong;
    strong.id = 0U;
    strong.name = "strong-path-driver";
    strong.register_count = 1U;
    strong.static_sensitivity = { { 0U, EdgeKind::any } };
    if (!omit_strong_region)
        strong.driver_regions = { { 1U, 0U, 2U, true } };
    strong.drive_strength = { StrengthRank::strong, StrengthRank::strong };
    strong.operations = { ReadSignal { 0U, 0U }, WriteUpdate { 1U, 0U },
        WaitSensitivity { }, Jump { 0U } };
    state.processes.push_back(std::move(strong));

    Process weak;
    weak.id = 1U;
    weak.name = "weak-continuous-driver";
    weak.register_count = 1U;
    weak.driver_regions = { { 1U, 0U, 2U, true } };
    weak.drive_strength = { StrengthRank::weak, StrengthRank::weak };
    weak.operations = {
        LoadConstant { 0U, runtime::PackedLogic4::from_msb_string("00") },
        WriteUpdate { 1U, 0U }, Halt { }
    };
    state.processes.push_back(std::move(weak));

    Process mos;
    mos.id = 2U;
    mos.name = "mos-switch-driver";
    mos.register_count = 1U;
    mos.static_sensitivity = { { 1U, EdgeKind::any } };
    mos.driver_regions = { { 2U, 0U, 2U, true } };
    mos.switch_source = 1U;
    mos.switch_target = 2U;
    mos.switch_width = 2U;
    mos.operations = { ReadSignal { 0U, 1U }, WriteUpdate { 2U, 0U },
        WaitSensitivity { }, Jump { 0U } };
    state.processes.push_back(std::move(mos));

    Process tran;
    tran.id = 3U;
    tran.name = "tran-switch-path";
    tran.switch_source = 1U;
    tran.switch_target = 2U;
    tran.switch_width = 2U;
    tran.switch_bidirectional = true;
    tran.initialize = false;
    tran.operations = { Halt { } };
    state.processes.push_back(std::move(tran));

    elaboration::VerilogSpecifyPathInfo path;
    path.id = 0U;
    path.identity = path_identity;
    path.instance = "top.u";
    path.sources = { { 0U, 0U, 2U } };
    path.destinations = { { 1U, 0U, 2U } };
    path.drivers = { 0U, 1U };
    path.delays = { 3U };
    state.verilog_specify_paths.push_back(std::move(path));

    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "drive timing design fixture must validate");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfSchedulingApplication> make_scheduling(
    fsim::elaboration::ElaboratedDesign design)
{
    return std::make_shared<const fsim::app::SdfSchedulingApplication>(
        nullptr, std::move(design),
        std::vector<fsim::app::SdfScheduledTimingTarget> { },
        "sdf-drive-test-scheduling-v1");
}

void require_diagnostic(const fsim::app::SdfDriveTimingResult& result,
    const std::string_view code)
{
    require(!result.ok()
            && std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) {
                    return diagnostic.code == code;
                }),
        "expected SDF drive-timing diagnostic was not emitted");
}

void test_complete_drive_binding()
{
    using namespace fsim;
    const auto scheduling = make_scheduling(make_drive_design());
    const auto result = app::apply_sdf_drive_timing(scheduling);
    require(result.ok() && result.application->scheduling() == scheduling
            && result.application->bindings().size() == 7U
            && !result.application->semantic_identity().empty(),
        "complete drive timing must publish immutable ownership");
    const auto bindings = result.application->bindings();
    const auto has_role = [&](const app::SdfDriveTimingBindingRole role) {
        return std::ranges::any_of(bindings, [&](const auto& binding) {
            return binding.role == role;
        });
    };
    require(has_role(app::SdfDriveTimingBindingRole::path_driver)
            && has_role(app::SdfDriveTimingBindingRole::propagated_driver)
            && has_role(
                app::SdfDriveTimingBindingRole::switch_unidirectional)
            && has_role(app::SdfDriveTimingBindingRole::switch_bidirectional),
        "path, MOS/CMOS and tran ownership roles must remain distinct");
    const auto strong = std::ranges::find_if(bindings, [](const auto& binding) {
        return binding.process == 0U;
    });
    const auto weak = std::ranges::find_if(bindings, [](const auto& binding) {
        return binding.process == 1U;
    });
    require(strong != bindings.end() && weak != bindings.end()
            && strong->signal == 1U && strong->width == 2U
            && strong->resolution == runtime::simir::ResolutionKind::sv_wire
            && strong->strength.zero
                == runtime::simir::StrengthRank::strong
            && weak->strength.zero == runtime::simir::StrengthRank::weak,
        "drive binding must retain packed region, net resolution and strengths");
}

struct RuntimeCapture {
    std::vector<std::tuple<fsim::runtime::SimulationTick, std::string,
        std::string>>
        driver_changes;
    std::string visible_before_release;
    std::string strong_driver;
    std::string weak_driver;
    std::string visible_after_release;
    std::string peer;
};

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

fsim::project::Config make_config(const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sdf-drive-timing-differential";
    config.project.time_resolution = "1ns";
    config.project.tops = { { "sv:work.top", "top" } };
    config.build.optimization = fsim::project::Optimization::o2;
    config.build.cache_path = directory / "cache";
    config.run.max_deltas = 1000U;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    return config;
}

struct EngineCapture {
    std::vector<std::tuple<fsim::runtime::SimulationTick, std::string,
        std::string>>
        changes;
    std::size_t compiled_processes { };
};

EngineCapture run_engine(
    fsim::app::BuiltProject project, const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation { std::move(project), 1000U, engine };
    const auto output = simulation.find_signal("top.z");
    const auto peer = simulation.find_signal("top.peer");
    require(output && peer, "drive differential outputs must resolve");
    EngineCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time, const std::uint64_t) {
            if (signal == *output)
                capture.changes.emplace_back(
                    time, "top.z", value.to_msb_string());
            if (signal == *peer)
                capture.changes.emplace_back(
                    time, "top.peer", value.to_msb_string());
        });
    const auto result = simulation.run();
    require(result.status == fsim::runtime::RunStatus::stopped,
        "drive differential simulation must finish by design");
    return capture;
}

RuntimeCapture run_forced_path(const std::string_view ones,
    const std::string_view zeros, const std::string_view unknowns)
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter { { 1000U, 64U } };
    const auto source = interpreter.add_signal(
        { "drive.source", PackedLogic4::from_msb_string(zeros) });
    const auto output = interpreter.add_signal({ "drive.output",
        PackedLogic4 { zeros.size(), Logic4::z }, ResolutionKind::sv_wire });
    const auto peer = interpreter.add_signal({ "drive.peer",
        PackedLogic4 { zeros.size(), Logic4::z }, ResolutionKind::sv_wire });

    Process stimulus;
    stimulus.id = 0U;
    stimulus.name = "annotated-path-stimulus";
    stimulus.register_count = 2U;
    stimulus.driver_regions = { { source, 0U,
        static_cast<std::uint32_t>(zeros.size()), true } };
    stimulus.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string(ones) },
        WriteUpdate { source, 0U }, WaitFor { 4U },
        LoadConstant { 1U, PackedLogic4 { zeros.size(), Logic4::z } },
        WriteUpdate { source, 1U }, Halt { }
    };
    (void)interpreter.add_process(std::move(stimulus));

    Process strong;
    strong.id = 1U;
    strong.name = "annotated-strong-driver";
    strong.register_count = 1U;
    strong.static_sensitivity = { { source, EdgeKind::any } };
    strong.driver_regions = { { output, 0U,
        static_cast<std::uint32_t>(zeros.size()), true } };
    strong.drive_strength = { StrengthRank::strong, StrengthRank::strong };
    strong.initialize = false;
    strong.operations = { ReadSignal { 0U, source },
        WriteUpdate { output, 0U }, WaitSensitivity { }, Jump { 0U } };
    (void)interpreter.add_process(std::move(strong));

    Process weak;
    weak.id = 2U;
    weak.name = "weak-competing-driver";
    weak.register_count = 1U;
    weak.driver_regions = { { output, 0U,
        static_cast<std::uint32_t>(zeros.size()), true } };
    weak.drive_strength = { StrengthRank::weak, StrengthRank::weak };
    weak.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string(zeros) },
        WriteUpdate { output, 0U }, Halt { }
    };
    (void)interpreter.add_process(std::move(weak));

    Process tran;
    tran.id = 3U;
    tran.name = "annotated-tran-path";
    tran.switch_source = output;
    tran.switch_target = peer;
    tran.switch_width = zeros.size();
    tran.switch_bidirectional = true;
    tran.initialize = false;
    tran.operations = { Halt { } };
    (void)interpreter.add_process(std::move(tran));

    ModulePath path;
    path.identity = path_identity;
    path.sources = { { source, 0U,
        static_cast<std::uint32_t>(zeros.size()) } };
    path.destinations = { { output, 0U,
        static_cast<std::uint32_t>(zeros.size()) } };
    path.drivers = { 1U };
    path.delays = { 3U };
    (void)interpreter.add_module_path(std::move(path));

    RuntimeCapture capture;
    interpreter.set_driver_change_hook([&](const ProcessId process,
                                           const SignalId signal,
                                           const SimulationTick time) {
        if (signal == output && (process == 1U || process == 2U)) {
            capture.driver_changes.emplace_back(time,
                std::to_string(process),
                interpreter.driver_value(process, output).to_msb_string());
        }
    });
    interpreter.force_signal(
        output, PackedLogic4::from_msb_string(unknowns));
    const auto run = interpreter.run();
    require(run.status == RunStatus::completed,
        "forced annotated path runtime must complete");
    capture.visible_before_release
        = interpreter.signal_value(output).to_msb_string();
    capture.strong_driver = interpreter.driver_value(1U, output).to_msb_string();
    capture.weak_driver = interpreter.driver_value(2U, output).to_msb_string();
    capture.peer = interpreter.signal_value(peer).to_msb_string();
    interpreter.release_signal(output);
    capture.visible_after_release
        = interpreter.signal_value(output).to_msb_string();
    return capture;
}

void test_pending_force_release_strength_and_vector_parity()
{
    const auto scalar = run_forced_path("1", "0", "X");
    const auto vector = run_forced_path("11", "00", "XX");
    require(vector.visible_before_release == "XX"
            && vector.strong_driver == "ZZ" && vector.weak_driver == "00"
            && vector.peer == "00" && vector.visible_after_release == "00",
        "force must mask delayed drivers while release exposes current resolution");
    require(std::ranges::any_of(vector.driver_changes, [](const auto& change) {
        return std::get<0>(change) == 3U
            && std::get<1>(change) == "1"
            && std::get<2>(change) == "11";
    }) && std::ranges::any_of(vector.driver_changes, [](const auto& change) {
        return std::get<0>(change) == 7U
            && std::get<1>(change) == "1"
            && std::get<2>(change) == "ZZ";
    }),
        "annotated pending values and driver removal must update beneath force");
    require(scalar.visible_before_release == "X"
            && scalar.strong_driver == "Z" && scalar.weak_driver == "0"
            && scalar.peer == "0" && scalar.visible_after_release == "0",
        "scalar and packed drive timing must use equivalent resolution rules");
}

void test_interpreter_llvm_force_switch_differential()
{
    using namespace fsim;
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory directory { std::filesystem::temp_directory_path()
        / ("fsim-sdf-drive-timing-" + unique) };
    std::filesystem::create_directories(directory.path);
    const auto source_path = directory.path / "top.sv";
    {
        std::ofstream source(source_path);
        source << R"(module delay_buf(input logic [1:0] a, output wire [1:0] z);
  assign (strong1, strong0) z = a;
  specify
    (a *> z) = 5;
  endspecify
endmodule

module top;
  logic [1:0] a;
  wire [1:0] z;
  wire [1:0] peer;
  assign (weak1, weak0) z = 2'b00;
  tran t0(z[0], peer[0]);
  tran t1(z[1], peer[1]);
  delay_buf u(.a(a), .z(z));
  initial begin
    a = 2'b00;
    #1 force z = 2'bxx;
    #1 a = 2'b11;
    #4 release z;
    #4 a = 2'bzz;
    #10 $finish;
  end
endmodule
)";
    }
    diagnostic::Engine diagnostics;
    auto project
        = app::build_project(make_config(directory.path, source_path), diagnostics);
    if (!project)
        diagnostic::print_text(std::cerr, diagnostics);
    require(project.has_value()
            && !project->design.verilog_specify_paths().empty(),
        "drive differential project must elaborate specify timing");
    auto state = project->design.state();
    for (auto& path : state.verilog_specify_paths)
        path.delays = { 3U };
    auto scheduled_design
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(scheduled_design.has_value(),
        "drive differential scheduled design must validate");
    const auto drive = app::apply_sdf_drive_timing(
        make_scheduling(std::move(*scheduled_design)));
    require(drive.ok()
            && std::ranges::any_of(drive.application->bindings(),
                [](const auto& binding) {
                    return binding.role
                        == app::SdfDriveTimingBindingRole::switch_bidirectional;
                }),
        "drive differential ownership must include tran paths");
    project->design = drive.application->scheduling()->design();
    auto compiled_project = *project;
    const auto reference
        = run_engine(std::move(*project), app::SimulationEngine::interpreter);
    const auto compiled
        = run_engine(std::move(compiled_project), app::SimulationEngine::compiled);
#if defined(FSIM_HAS_LLVM)
    const bool compiled_process_count_ok = compiled.compiled_processes > 0U;
#else
    const bool compiled_process_count_ok = compiled.compiled_processes == 0U;
#endif
    require(compiled_process_count_ok
            && compiled.changes == reference.changes
            && std::ranges::any_of(reference.changes, [](const auto& change) {
                   return std::get<0>(change) == 6U
                       && std::get<1>(change) == "top.z"
                       && std::get<2>(change) == "11";
               })
            && std::ranges::any_of(reference.changes, [](const auto& change) {
                   return std::get<0>(change) == 13U
                       && std::get<1>(change) == "top.peer"
                       && std::get<2>(change) == "00";
               }),
        "LLVM and interpreter must share force/release, strength and tran timing");
}

void test_atomic_rejection()
{
    using namespace fsim;
    require_diagnostic(app::apply_sdf_drive_timing(nullptr),
        "FSIM-SDF-DRIVE-001");
    require_diagnostic(
        app::apply_sdf_drive_timing(make_scheduling(make_drive_design(true))),
        "FSIM-SDF-DRIVE-002");
    require_diagnostic(app::apply_sdf_drive_timing(
                           make_scheduling(make_drive_design()), { 1U, 4096U }),
        "FSIM-SDF-DRIVE-004");
}
} // namespace

int main()
{
    try {
        test_complete_drive_binding();
        test_pending_force_release_strength_and_vector_parity();
        test_interpreter_llvm_force_switch_differential();
        test_atomic_rejection();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
