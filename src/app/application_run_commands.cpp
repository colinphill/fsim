// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_vhpi_foreign.hpp"
#include "application_vpi_foreign.hpp"
#include "application_dpi_foreign.hpp"
#include "application_foreign_run_state.hpp"
#include "application_trace_control.hpp"
#include "application_trace_hierarchy.hpp"
#include "application_trace_observation.hpp"
#include "application_workspace_store.hpp"
#if defined(FSIM_ENABLE_ALLOCATION_PROFILING)
#  include "allocation_profile.hpp"
#endif
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/payload_checksum.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#if defined(_WIN32)
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace fsim::app::application_detail {

namespace {

/// Reports flush standard output line by line only on a terminal; a file
/// or pipe gets the buffering C stdio gives it (the report stream and
/// $display output share stdio's buffer, so their order is kept).
[[nodiscard]] bool stdout_is_terminal()
{
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

struct BuiltinStdoutFlush final {
    bool enabled { };

    ~BuiltinStdoutFlush()
    {
        if (enabled) {
            (void)std::fflush(stdout);
        }
    }
};

} // namespace

std::optional<SimulationTick> configured_duration(
    const project::Config& config,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)
{
    if (!config.run.duration) {
        return std::nullopt;
    }
    std::string error;
    const auto duration = parse_time(
        *config.run.duration, resolution, error);
    if (!duration) {
        diagnostics.error("FSIM-TIME-0002", error);
    }
    return duration;
}

void install_interrupt_hook(Simulation& simulation)
{
    interrupt_requested.store(false, std::memory_order_relaxed);
    simulation.set_safe_point_hook([](runtime::Scheduler& scheduler,
                                       runtime::SchedulerPhase) {
        // A plain load first: the exchange is a locked read-modify-write,
        // and this runs at every scheduler safe point.
        if (interrupt_requested.load(std::memory_order_relaxed)
            && interrupt_requested.exchange(false, std::memory_order_relaxed)) {
            scheduler.request_stop();
        }
    });
}

int handle_check(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto checked = check_project(config, diagnostics);
    if (!checked) {
        return 1;
    }
    output << "checked " << checked->source_count << " source file(s), "
           << checked->semantics.units().size() << " design unit(s)\n";
    return 0;
}

int handle_build(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto built = build_project(config, diagnostics);
    if (!built) {
        return 1;
    }
    const auto roots = built->design_ir.roots();
    const auto signal_count = static_cast<std::size_t>(std::ranges::count_if(
        built->design_ir.objects(), [](const auto& object) {
            return object.kind == semantic::design::ObjectKind::signal
                && !object.parent_object;
        }));
    const auto process_count = built->design_ir.processes().size();
    const auto plugin_count = built->systemc_plugins.size();
    const auto cache_hit = built->cache_hit;
    const auto mapped_libraries = built->mapped_libraries;
    const auto selected_seed = built->seed;
    const auto entropy_seed_selected = built->entropy_seed;
    Simulation prepared(
        std::move(*built),
        config.run.max_deltas,
        SimulationEngine::compiled);
    for (const auto& request : invocation.library_exports) {
        if (!export_library(
                config, request.library, request.path, diagnostics)) {
            return 1;
        }
        output << "exported logical library '" << request.library
               << "' to " << support::path_to_utf8(request.path) << '\n';
    }
    output << "built ";
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << prepared.design_ir().path(roots[index]);
    }
    output << " ("
           << signal_count << " signals, "
           << process_count << " processes";
    if (plugin_count != 0) {
        output << ", " << plugin_count
               << " validated SystemC plug-in artifact(s)";
    }
    if (!mapped_libraries.empty()) {
        output << ", " << mapped_libraries.size()
               << " mapped precompiled librar"
               << (mapped_libraries.size() == 1 ? "y" : "ies") << " [";
        for (std::size_t index = 0; index < mapped_libraries.size(); ++index) {
            if (index != 0) {
                output << ", ";
            }
            output << mapped_libraries[index].library;
        }
        output << ']';
    }
    output << "; analysis cache " << (cache_hit ? "hit" : "populated")
           << ")\n";
    if (entropy_seed_selected) {
        output << "random seed " << selected_seed << '\n';
    }
    return 0;
}

struct ForeignLibraries::Impl {
    // Destroyed last to first: the VPI and VHPI services, then the signal
    // trap, then the DPI libraries the services resolve against.
    std::unique_ptr<DpiForeignService> dpi;
    std::optional<InterruptSignalGuard> interrupt_signal;
    std::unique_ptr<VhpiForeignService> vhpi;
    std::unique_ptr<VpiForeignService> vpi;
    // A run-time error that ended the run (IEEE 1800-2023 38.36.3).
    std::optional<std::string> error;
    bool finished { };
};

ForeignLibraries::ForeignLibraries(Simulation& simulation)
    : simulation_(simulation), impl_(std::make_unique<Impl>())
{
}

ForeignLibraries::~ForeignLibraries()
{
    finish();
}

void ForeignLibraries::run_error(std::string message)
{
    impl_->error = std::move(message);
}

void ForeignLibraries::finish_vhpi()
{
    if (impl_->vhpi) {
        impl_->vhpi->finish();
    }
}

bool ForeignLibraries::vhpi_failed() const
{
    return impl_->vhpi && impl_->vhpi->failed();
}

// However the run ends, the foreign interfaces see the end of simulation
// and of the tool while their libraries are still loaded (IEEE 1800-2023
// 38.36.3, IEEE 1076-2019 20.10).
void ForeignLibraries::finish() noexcept
{
    if (impl_->finished) {
        return;
    }
    impl_->finished = true;
    if (impl_->vpi && impl_->error) {
        impl_->vpi->run_error(*impl_->error);
    }
    simulation_.end_foreign_simulation();
    try {
        finish_vhpi();
    } catch (...) {
    }
}

bool foreign_libraries_requested(const project::Config& config)
{
    return !config.run.vpi_modules.empty() || !config.run.vhpi_modules.empty()
        || !config.run.vhpi_registries.empty() || !config.run.sv_libs.empty();
}

// Loads the --sv-lib, --vpi and --vhpi libraries and starts their services;
// the batch run and the Tcl and debugger sessions share it. Null after a
// diagnostic when a library does not load.
std::unique_ptr<ForeignLibraries> attach_foreign_libraries(
    Simulation& simulation,
    const project::Config& config,
    const std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    const SimulationOutputSink sink,
    const std::size_t design_process_count)
{
    const bool vpi_modules = !config.run.vpi_modules.empty();
    const bool vhpi_modules = !config.run.vhpi_modules.empty()
        || !config.run.vhpi_registries.empty();
    const bool foreign = foreign_libraries_requested(config);
    auto libraries = std::make_unique<ForeignLibraries>(simulation);
    auto& impl = *libraries->impl_;
    // A library that did not load gets no end of simulation.
    const auto failed = [&impl]() -> std::unique_ptr<ForeignLibraries> {
        impl.finished = true;
        return nullptr;
    };
    // DPI imports resolve in the --sv-lib libraries, then in the VPI
    // modules, which load with global symbols.
    if (foreign) {
        impl.dpi = load_dpi_libraries(config.run.sv_libs, diagnostics);
        if (!impl.dpi) {
            return failed();
        }
        auto& time = simulation.systemverilog_vpi_time();
        impl.dpi->set_clock({
            [&time] {
                const auto now = time.query(
                    runtime::SystemVerilogVpiTimeFormat::IntegerTicks);
                return (static_cast<std::uint64_t>(now.value.high) << 32U)
                    | now.value.low;
            },
            time.profile().precision_exponent,
            time.profile().unit_exponent,
            [&simulation](const std::string_view name)
                -> std::optional<std::string> {
                const auto found
                    = simulation.systemverilog_vpi_objects().find(name);
                if (!found) {
                    return std::nullopt;
                }
                using Kind = runtime::SystemVerilogVpiObjectKind;
                const auto kind = found.value->kind;
                // A configured top is published as a Root object.
                if (kind != Kind::Root && kind != Kind::Module
                    && kind != Kind::Interface && kind != Kind::Program
                    && kind != Kind::GenerateScope) {
                    return std::nullopt;
                }
                return found.value->full_name;
            },
            [&simulation](std::string_view path)
                -> std::optional<std::pair<int, int>> {
                // A generate scope has the time scale of its module.
                while (true) {
                    if (const auto scale
                        = simulation.systemverilog_instance_time_scale(path)) {
                        return scale;
                    }
                    const auto dot = path.rfind('.');
                    if (dot == std::string_view::npos) {
                        return std::nullopt;
                    }
                    path = path.substr(0U, dot);
                }
            },
        });
    }
    // Installed before the foreign libraries load, so a signal trap that
    // their startup routines set up replaces it (IEEE 1800-2023 38.36.3,
    // IEEE 1076-2019 21.3.7.10), and destroyed after them.
    impl.interrupt_signal.emplace();
    // A library loaded through one interface may load a library that uses
    // the other one (a mixed-language cocotb run loads its VHPI layer from
    // its VPI library, or the reverse), so each interface the design has a
    // language for is served. A service with no modules of its own is
    // attached before the one whose startup routines run; with both served,
    // each answers calls while the other's routines and callbacks run.
    const auto has_language = [&](const auto... languages) {
        return std::ranges::any_of(simulation.design_ir().specializations(),
            [&](const auto& specialization) {
                return ((specialization.language == languages) || ...);
            });
    };
    const bool vpi_wanted = vpi_modules
        || (vhpi_modules
            && has_language(semantic::Language::verilog,
                semantic::Language::system_verilog));
    // A foreign architecture needs a VHPI program that registers its model
    // (IEEE 1076-2019 20.2.4); without a module that is an error the VHPI
    // service reports.
    const bool vhpi_wanted = vhpi_modules
        || (vpi_modules && has_language(semantic::Language::vhdl))
        || (has_language(semantic::Language::vhdl)
            && (design_has_vhdl_foreign_architecture(simulation)
                || design_has_vhdl_foreign_subprogram(simulation)));
    const auto vhpi_print = [&] {
        return sink == SimulationOutputSink::builtin_stdout
            ? VhpiForeignService::PrintSink { [](const std::string_view text) {
                  (void)std::fwrite(text.data(), 1U, text.size(), stdout);
              } }
            : VhpiForeignService::PrintSink {
                  [&output](const std::string_view text) { output << text; } };
    };
    // The workspace's library directories, read only when a VHPI program
    // asks for a physical name.
    const auto vhpi_libraries = [root = config.base_directory] {
        std::vector<std::pair<std::string, std::filesystem::path>> result;
        std::string failure;
        if (const auto locations
            = workspace::Store { root }.library_locations(failure)) {
            for (const auto& location : *locations) {
                result.emplace_back(location.name,
                    location.directory.is_absolute()
                        ? location.directory
                        : root / location.directory);
            }
        }
        return result;
    };
    // The VHDL units analyzed into a library, read from the workspace library
    // catalog (read-only) when a VHPI program lists the library's design
    // units (IEEE 1076-2019 19.4.5).
    const auto vhpi_library_units = [root = config.base_directory](
                                        const std::string_view library) {
        std::vector<VhpiCatalogUnit> result;
        std::string failure;
        if (const auto catalog
            = workspace::Store { root }.read_library(library, failure)) {
            for (const auto& record : catalog->artifacts) {
                for (const auto& owned : record.units) {
                    if (owned.unit.language == "vhdl") {
                        result.push_back({ owned.unit.kind, owned.unit.name,
                            owned.unit.primary_name });
                    }
                }
            }
        }
        return result;
    };
    // The design of one analyzed unit that the elaborated design does not
    // hold, linked from its library object and the objects it depends on, for
    // the VHPI library model (IEEE 1076-2019 19.4.5). A VHPI program that
    // never asks for such a unit never reads these objects.
    const auto vhpi_library_design = [root = config.base_directory](
                                         const std::string_view library_name,
                                         const VhpiCatalogUnit& unit)
        -> std::shared_ptr<const semantic::CompiledDesign> {
        const workspace::Store store { root };
        std::map<std::string, workspace::LibraryCatalog, std::less<>> catalogs;
        const auto catalog_of = [&](const std::string_view name)
            -> const workspace::LibraryCatalog* {
            if (const auto found = catalogs.find(name);
                found != catalogs.end()) {
                return &found->second;
            }
            std::string failure;
            auto catalog = store.read_library(name, failure);
            if (!catalog) {
                return nullptr;
            }
            return &catalogs.emplace(std::string { name }, std::move(*catalog))
                        .first->second;
        };
        const auto same_unit = [](const library::UnitIndexEntry& entry,
                                   const std::string_view kind,
                                   const std::string_view name,
                                   const std::string_view primary) {
            return entry.language == "vhdl" && entry.kind == kind
                && entry.name == name && entry.primary_name == primary;
        };
        struct Wanted {
            std::string library;
            std::string kind;
            std::string name;
            std::string primary;
        };
        std::vector<std::filesystem::path> objects;
        std::vector<Wanted> pending { { std::string { library_name }, unit.kind,
            unit.name, unit.primary_name } };
        std::set<std::string> seen;
        while (!pending.empty()) {
            const auto wanted = std::move(pending.back());
            pending.pop_back();
            const auto* const catalog = catalog_of(wanted.library);
            if (catalog == nullptr) {
                continue;
            }
            for (const auto& artifact : catalog->artifacts) {
                if (artifact.kind != workspace::ArtifactKind::Hdl
                    || std::ranges::none_of(artifact.units, [&](const auto& owned) {
                           return same_unit(owned.unit, wanted.kind,
                               wanted.name, wanted.primary);
                       })) {
                    continue;
                }
                if (!seen.insert(wanted.library + "/" + artifact.id).second) {
                    break;
                }
                std::string failure;
                auto path = store.artifact_path(
                    catalog->location, artifact, failure);
                if (!path) {
                    return nullptr;
                }
                objects.push_back(std::move(*path));
                for (const auto& dependency : artifact.dependencies) {
                    pending.push_back({ dependency.library,
                        dependency.unit.kind, dependency.unit.name,
                        dependency.unit.primary_name });
                }
                break;
            }
        }
        if (objects.empty()) {
            return nullptr;
        }
        diagnostic::Engine quiet;
        auto project = load_objects(objects, quiet);
        if (!project) {
            return nullptr;
        }
        return std::make_shared<const CheckedProject>(std::move(*project));
    };
    if (vhpi_wanted && !vhpi_modules) {
        impl.vhpi = attach_vhpi_modules(
            simulation, {}, {}, vhpi_print(), plusargs, diagnostics, {},
            vhpi_libraries, vhpi_library_units, vhpi_library_design);
        if (!impl.vhpi) {
            return failed();
        }
    }
        if (vpi_wanted) {
        auto print = sink == SimulationOutputSink::builtin_stdout
            ? VpiForeignService::PrintSink { [](const std::string_view text) {
                  (void)std::fwrite(text.data(), 1U, text.size(), stdout);
              } }
            : VpiForeignService::PrintSink {
                  [&output](const std::string_view text) { output << text; } };
        impl.vpi = attach_vpi_modules(simulation, config.run.vpi_modules,
            design_process_count, std::move(print), plusargs,
            impl.dpi.get(), diagnostics,
            impl.vhpi ? impl.vhpi->calls() : ForeignCompanion {});
        if (!impl.vpi) {
            return failed();
        }
    } else if (impl.dpi) {
        simulation.set_systemverilog_dpi_function_hook(
            [dpi = impl.dpi.get()](const std::string_view identity,
                std::vector<runtime::PackedLogic4>& actuals,
                std::vector<std::string>& strings,
                const std::span<const std::string>,
                const std::span<const std::uint8_t>) {
                return dpi->call(identity, actuals, strings);
            });
        simulation.set_systemverilog_dpi_site_hooks({
            [dpi = impl.dpi.get()](const std::string_view identity) {
                return dpi->bind(identity);
            },
            [dpi = impl.dpi.get()](const void* const site,
                std::vector<runtime::PackedLogic4>& actuals,
                std::vector<std::string>& strings) {
                return dpi->call_site(site, actuals, strings);
            },
            [dpi = impl.dpi.get()](
                const runtime::simir::DpiSitePlan& plan) {
                return dpi->bind_plan(plan);
            },
            [dpi = impl.dpi.get()](const void* const site) {
                return dpi->planned_site(site);
            },
            [dpi = impl.dpi.get()](const void* const site,
                const runtime::simir::DpiSitePlan& plan,
                runtime::simir::DpiFrame& frame) {
                return dpi->call_plan(site, plan, frame);
            },
        });
    }
    if (vhpi_modules) {
        impl.vhpi = attach_vhpi_modules(simulation,
            config.run.vhpi_modules, config.run.vhpi_registries, vhpi_print(),
            plusargs, diagnostics,
            impl.vpi ? impl.vpi->calls() : ForeignCompanion {},
            vhpi_libraries, vhpi_library_units, vhpi_library_design);
        if (!impl.vhpi) {
            return failed();
        }
    }
    if (impl.vpi && impl.vhpi) {
        impl.vpi->set_companion(impl.vhpi->calls());
        impl.vhpi->set_companion(impl.vpi->calls());
    }
    // What the libraries put while saving and the restart callbacks they
    // registered travel in the run-state file (IEEE 1800-2023 38.36.3, IEEE
    // 1076-2019 20.7).
    {
        runtime::RunStateSectionRegistry foreign_sections;
        register_foreign_run_state_sections(
            foreign_sections, impl.vpi.get(), impl.vhpi.get());
        simulation.set_foreign_run_state_sections(std::move(foreign_sections));
    }
    // A reset disables the DPI call chains in flight after the libraries
    // heard cbStartOfReset (registered last, so it runs last; IEEE 1800-2023
    // D.8 a).
    if (impl.dpi) {
        simulation.add_lifecycle_hook(
            [dpi = impl.dpi.get()](const Simulation::LifecycleEvent event,
                const std::string_view) {
                if (event == Simulation::LifecycleEvent::start_of_reset) {
                    dpi->reset();
                }
            });
    }
    return libraries;
}

int run_built_project(
    BuiltProject built,
    const SimulationEngine engine,
    const project::Config& config,
    const std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    const SimulationOutputSink sink,
    std::function<std::optional<BuiltProject>()> reload_design)
{
    const auto duration = configured_duration(
        config, built.time_resolution, diagnostics);
    if (config.run.duration && !duration) {
        return 1;
    }
    // `--save-at TIME --save-file FILE` saves the run state at the end of the
    // time slot at TIME (IEEE 1800-2023 D.9); both are needed.
    std::optional<SimulationTick> save_at;
    if (config.run.save_at.has_value() != config.run.save_file.has_value()) {
        diagnostics.error("FSIM-SAVE-0020",
            "--save-at and --save-file are used together");
        return 1;
    }
    if (config.run.save_at) {
        project::Config save_config = config;
        save_config.run.duration = config.run.save_at;
        save_at = configured_duration(
            save_config, built.time_resolution, diagnostics);
        if (!save_at) {
            return 1;
        }
        if (duration && *duration < *save_at) {
            diagnostics.error("FSIM-SAVE-0022",
                "--save-at is after --duration, so the run ends first");
            return 1;
        }
    }
    if (built.entropy_seed) {
        output << "random seed " << built.seed << '\n';
    }
    project::Config trace_config = config;
    std::shared_ptr<const TraceControlApplication> archived_control;
    if (built.trace_archive) {
        auto consumer_root = config.base_directory;
        if (config.run.trace_file && config.run.trace_enabled
            && built.trace_archive->output_intent.parent_path().empty()) {
            consumer_root = config.run.trace_file->parent_path();
        }
        auto restored = restore_trace_archive_control(
            *built.trace_archive, consumer_root);
        if (!restored.ok()) {
            for (const auto& diagnostic : restored.diagnostics)
                application_detail::import_diagnostic(diagnostics, diagnostic);
            return 1;
        }
        if (config.run.trace_file && config.run.trace_enabled) {
            const auto surface = TraceControlSurface::ProjectCli;
            auto current = apply_trace_control(trace_control_request(
                config.run, surface, TraceControlPhase::Simulate));
            if (!current.ok()) {
                for (const auto& diagnostic : current.diagnostics)
                    application_detail::import_diagnostic(diagnostics, diagnostic);
                return 1;
            }
            const auto current_snapshot = make_trace_archive_snapshot(
                *current.application, config.base_directory);
            if (!trace_archive_profiles_compatible(
                    current_snapshot, *built.trace_archive)) {
                diagnostics.error("FSIM-TRACE-ARCHIVE-003",
                    "simulation trace request conflicts with the archived design profile");
                return 1;
            }
        }
        archived_control = std::move(restored.application);
        publish_trace_control(*archived_control, trace_config.run);
    }
    const auto design_process_count = built.design.process_count();
#if defined(FSIM_ENABLE_ALLOCATION_PROFILING)
    allocation_profile::Session allocation_profile_session {
        std::getenv("FSIM_PROFILE_ALLOCATIONS") != nullptr
    };
#endif
    const bool profile_phases = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    const auto simulation_setup_begin = std::chrono::steady_clock::now();
    // Hidden fused nets need no published events when nothing outside the
    // SimIR processes records them.
    // A VPI module may read and write any design object, so the objects
    // stay published and the fused nets visible.
    const bool vpi_modules = !config.run.vpi_modules.empty();
    // A DPI context import may use the VPI too.
    const bool vhpi_modules = !config.run.vhpi_modules.empty()
        || !config.run.vhpi_registries.empty();
    const bool foreign = vpi_modules || vhpi_modules
        || !config.run.sv_libs.empty();
    const bool trace_output
        = trace_config.run.trace_file && trace_config.run.trace_enabled;
    built.cone_fusion = engine != SimulationEngine::debug
        && !built.code_coverage_enabled && !trace_output && !foreign;
    // Cone fusion hides internal nets and stays off for these clients; the
    // static kernel serves them with its default plan (they watch and write
    // its signals at run time).
    plan_kernel_access(built, engine, foreign && config.run.foreign_probes);
    // A VPI, VHPI, PLI or DPI library may write any std_logic value. A
    // design without nine-value signals keeps its access plan (and so the
    // kernel image elaborate --aot recorded).
    built.kernel_access.weak_sources = built.kernel_access.weak_sources
        || (foreign && built.design.has_nine_value_signals());
    // Only a run with a library compiles the primitive delay slots in
    // (vpi_put_delays, acc_replace_delays); the others keep the constant
    // delayed writes. A design without primitives keeps the kernel planned
    // at elaboration.
    if (foreign && built.design.bind_primitive_delay_slots() != 0U) {
        built.kernel_access.delay_slots = true;
    }
    if (profile_phases && !built.cone_fusion
        && !built.kernel_access.admit && static_kernel_requested(engine)) {
        // The census counts these runs as kernel-off, by gate.
        const auto* gate = engine == SimulationEngine::debug ? "gate_debug"
            : built.code_coverage_enabled                    ? "gate_coverage"
            : trace_output                                   ? "gate_trace"
            : vpi_modules                                    ? "gate_vpi"
            : vhpi_modules                                   ? "gate_vhpi"
                                                             : "gate_dpi";
        std::cerr << "fsim-profile: static-kernel disabled=1 reason=" << gate
                  << " members=0 processes=" << design_process_count
                  << " host=" << design_process_count << '\n';
    }
    Simulation simulation(
        std::move(built),
        config.run.max_deltas,
        engine,
        foreign ? SystemVerilogVpiRuntimeUpdates::enabled
                    : SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.set_foreign_probes(foreign && config.run.foreign_probes);
    // $reset, vpi_control(vpiReset) and vhpi_control(vhpiReset) rebuild the
    // time-zero state from the design artifact (IEEE 1800-2023 D.8, 38.4;
    // IEEE 1076-2019 20.9). Installed before the foreign libraries attach so
    // that they see reset_supported().
    if (reload_design) {
        simulation.set_reset_design_source(
            std::move(reload_design), config.run.max_deltas, engine);
    }
    {
        // A restart repeats the command line of the save (IEEE 1800-2023
        // D.9): the libraries, plusargs and options that shape the run state
        // are part of the identity a saved file records.
        std::string identity = std::string { "engine=" }
            + (engine == SimulationEngine::interpreter ? "interpreter"
                    : engine == SimulationEngine::debug ? "debug"
                                                        : "v4");
        const auto append = [&](const std::string_view key, const auto& values) {
            identity += ';';
            identity += key;
            identity += '=';
            for (const auto& value : values) {
                identity += value;
                identity += ',';
            }
        };
        // A restart loads the same builds of the libraries: a registration
        // is stored as module and offset, which only the same file keeps
        // valid (FSIM-SAVE-0016 otherwise).
        const auto build_hashes = [](const auto& paths) {
            std::vector<std::string> hashes;
            for (const auto& path : paths) {
                // `lib[:bootstrap]` names the file before the colon.
                auto input = support::native_fs::open_ifstream(
                    std::filesystem::path { std::string { path } }, std::ios::binary);
                if (!input) {
                    if (const auto colon = path.rfind(':');
                        colon != std::string::npos && colon != 0U) {
                        input = support::native_fs::open_ifstream(
                            std::filesystem::path { path.substr(0U, colon) },
                            std::ios::binary);
                    }
                }
                if (!input) {
                    hashes.emplace_back("unreadable");
                    continue;
                }
                support::Xxh64 state;
                std::array<char, 65536> buffer;
                while (input.read(buffer.data(),
                           static_cast<std::streamsize>(buffer.size()))
                    || input.gcount() > 0) {
                    state.update(std::string_view { buffer.data(),
                        static_cast<std::size_t>(input.gcount()) });
                }
                hashes.push_back(support::payload_checksum(state));
            }
            return hashes;
        };
        append("vpi", config.run.vpi_modules);
        append("vpi_build", build_hashes(config.run.vpi_modules));
        append("sv_libs_build", build_hashes(config.run.sv_libs));
        append("vhpi_build", build_hashes(config.run.vhpi_modules));
        append("sv_libs", config.run.sv_libs);
        append("vhpi", config.run.vhpi_modules);
        append("vhpi_registry", config.run.vhpi_registries);
        append("plusargs", plusargs);
        identity += config.run.foreign_probes ? ";probes" : "";
        simulation.set_state_file_identity(std::move(identity));
    }
    simulation.set_vhpi_end_of_step_timers(
        config.run.vhpi_end_of_step_timers);
#if defined(FSIM_ENABLE_ALLOCATION_PROFILING)
    allocation_profile_session.after_setup();
#endif
    const auto simulation_setup_elapsed = std::chrono::steady_clock::now()
        - simulation_setup_begin;
    const auto simulation_prepare_begin = std::chrono::steady_clock::now();
    if (!apply_uvm_command_line(simulation, plusargs, diagnostics)) {
        return 1;
    }
    if (sink == SimulationOutputSink::builtin_stdout) {
        output.flush();
        simulation.set_builtin_stdout_output();
    } else {
        simulation.set_output_hook(
            [&output](
                const runtime::simir::ProcessId,
                const std::string_view text,
                const bool newline,
                const SimulationTick,
                const std::uint64_t) {
                output << text;
                if (newline) {
                    output << '\n';
                }
            });
    }
    auto foreign_libraries = attach_foreign_libraries(simulation, config,
        plusargs, diagnostics, output, sink, design_process_count);
    if (!foreign_libraries) {
        return 1;
    }
    const BuiltinStdoutFlush stdout_flush {
        sink == SimulationOutputSink::builtin_stdout
    };
    const bool flush_reports = sink == SimulationOutputSink::builtin_stdout
        && stdout_is_terminal();
    simulation.set_trusted_text_report_hook(
        [&output, flush_reports](
            const runtime::simir::ProcessId,
            const std::string_view message,
            const runtime::simir::AssertionSeverity severity,
            const runtime::simir::SourceLocation& source,
            const SimulationTick,
            const std::uint64_t) {
            if (flush_reports) {
                (void)std::fflush(stdout);
            }
            output << source.path << ':' << source.line << ':'
                   << source.column << ": "
                   << report_severity_name(severity)
                   << "[FSIM-HDL-REPORT]: " << message << '\n';
            if (flush_reports) {
                output.flush();
            }
        });
    auto trace = attach_trace(simulation, trace_config, diagnostics, false,
        std::move(archived_control));
    if (trace_config.run.trace_file && trace_config.run.trace_enabled && !trace) {
        return 1;
    }
    install_interrupt_hook(simulation);
    // However the run ends, the foreign interfaces see the end of
    // simulation and of the tool while their libraries are still loaded.
    struct ForeignEnd {
        ForeignLibraries& libraries;
        ~ForeignEnd() { libraries.finish(); }
    } foreign_end { *foreign_libraries };
    try {
        // Native materialization overlaps the initial interpreted execution.
        // Each deferred executor is installed only at a process-resume safe
        // point, preserving its interpreter frame while removing LLVM
        // compilation from the cold simulation critical path. The explicit
        // build command still waits for every requested cache artifact.
        const auto native_await_elapsed
            = std::chrono::steady_clock::duration::zero();
        const auto simulation_run_begin = std::chrono::steady_clock::now();
        const auto simulation_prepare_elapsed
            = simulation_run_begin - simulation_prepare_begin;
#if defined(FSIM_ENABLE_ALLOCATION_PROFILING)
        allocation_profile_session.after_prepare();
#endif
        // `--restart FILE`: the saved state replaces time zero (IEEE 1800-2023
        // D.9); FSIM-SAVE-* errors name what did not fit.
        const auto report_state_error = [&](const std::string& message) {
            const auto separator = message.find(": ");
            if (message.starts_with("FSIM-SAVE-")
                && separator != std::string::npos) {
                diagnostics.error(
                    message.substr(0U, separator), message.substr(separator + 2U));
            } else {
                diagnostics.error("FSIM-SAVE-0018", message);
            }
        };
        if (config.run.restart_file) {
            const auto restored
                = simulation.restore_state(*config.run.restart_file);
            if (!restored.ok) {
                report_state_error(restored.error);
                return 1;
            }
        }
        // A $restart inside the run takes time back to the saved time, which
        // a trace file cannot follow: the trace ends and a new file starts at
        // the restored state, named for the restart (IEEE 1800-2023 D.9; owner
        // decision: wave traces start a new file at restart).
        const auto trace_restarts = std::make_shared<unsigned>(0U);
        if (trace) {
            simulation.add_lifecycle_hook(
                [&, trace_restarts](const Simulation::LifecycleEvent event,
                    const std::string_view) {
                    if (event != Simulation::LifecycleEvent::end_of_restart
                        || !trace) {
                        return;
                    }
                    (void)finish_trace(*trace, diagnostics);
                    auto rotated = trace_config;
                    auto name = trace->output_path.stem().string();
                    name += ".restart" + std::to_string(++*trace_restarts);
                    name += trace->output_path.extension().string();
                    rotated.run.trace_file
                        = trace->output_path.parent_path() / name;
                    trace = attach_trace(simulation, rotated, diagnostics,
                        false, {});
                });
        }
        auto result = simulation.run(save_at ? save_at : duration);
        if (save_at && result.status == runtime::RunStatus::time_limit) {
            // The run stopped after the time slot at --save-at, the point D.9
            // saves at; it then goes on to the end or to --duration.
            const auto saved = simulation.save_state(*config.run.save_file);
            if (!saved.ok) {
                report_state_error(saved.error);
                return 1;
            }
            if (!duration || *duration > *save_at) {
                result = simulation.run(duration);
            }
        } else if (save_at) {
            diagnostics.error("FSIM-SAVE-0022",
                "the simulation ended before --save-at, so nothing was saved");
        }
        if (sink == SimulationOutputSink::builtin_stdout) {
            (void)std::fflush(stdout);
        }
        const auto simulation_run_end = std::chrono::steady_clock::now();
        const auto simulation_execution_elapsed
            = simulation_run_end - simulation_run_begin;
#if defined(FSIM_ENABLE_ALLOCATION_PROFILING)
        allocation_profile_session.after_run();
#endif
        simulation.end_foreign_simulation();
        if (trace && !finish_trace(*trace, diagnostics)) {
            return 1;
        }
        if (config.run.coverage_output) {
            if (const auto failure = simulation.save_code_coverage_database(
                    *config.run.coverage_output)) {
                diagnostics.error("FSIM-COV-008",
                    "cannot save the coverage database: " + *failure);
                return 1;
            }
        }
        foreign_libraries->finish_vhpi();
        if (sink == SimulationOutputSink::builtin_stdout) {
            (void)std::fflush(stdout);
        }
        output << "simulation "
               << (result.status == runtime::RunStatus::completed
                          ? "completed"
                          : result.status == runtime::RunStatus::time_limit
                          ? "reached time limit"
                          : "stopped")
               << " at tick " << result.time << ", delta " << result.delta << '\n';
        if (result.status == runtime::RunStatus::stopped
            && (!simulation.finished() || simulation.paused_in_final())) {
            // $stop suspends rather than ends the simulation (IEEE
            // 1800-2017 20.2); in a batch run nothing resumes it.
            output << "$stop paused the simulation\n";
        }
#if defined(FSIM_ENABLE_ALLOCATION_PROFILING)
        allocation_profile_session.finish(output);
#endif
        if (profile_phases) {
            const auto milliseconds = [](const auto elapsed) {
                return std::chrono::duration<double, std::milli>(elapsed)
                    .count();
            };
            output << "FSIM-PROFILE setup_ms="
                   << milliseconds(simulation_setup_elapsed)
                   << " run_ms=" << milliseconds(simulation_execution_elapsed)
                   << " native_await_ms=" << milliseconds(native_await_elapsed)
                   << " prepare_ms=" << milliseconds(simulation_prepare_elapsed)
                   << " run_begin_ms="
                   << milliseconds(simulation_run_begin - simulation_setup_begin)
                   << " run_end_ms="
                   << milliseconds(simulation_run_end - simulation_setup_begin)
                   << " finalize_ms="
                   << milliseconds(std::chrono::steady_clock::now()
                          - simulation_run_end)
                   << '\n';
        }
        // A split vpi_put_delays asked for failed at its safe point after
        // the put returned success: the old timing ran, so the run fails.
        if (!simulation.deferred_split_errors().empty()) {
            for (const auto& message : simulation.deferred_split_errors()) {
                diagnostics.error("FSIM-VPI-SPLIT-001", message);
            }
            return 1;
        }
        if (result.simulator_status) {
            if (*result.simulator_status < 0
                || *result.simulator_status > 255) {
                diagnostics.error(
                    "FSIM-RUN-VHENV-001",
                    "STD.ENV simulator status must be in the portable range 0 through 255");
                return 1;
            }
            return static_cast<int>(*result.simulator_status);
        }
        return foreign_libraries->vhpi_failed() ? 1 : 0;
    } catch (const runtime::DeltaCycleLimitError& error) {
        std::ostringstream message;
        message << error.what() << "; active process IDs: ";
        if (error.pending_orders().empty()) {
            message << "none";
        } else {
            for (std::size_t index = 0;
                index < error.pending_orders().size(); ++index) {
                if (index != 0) {
                    message << ',';
                }
                message << error.pending_orders()[index];
            }
        }
        message << "; recently changed signal IDs: ";
        if (error.recent_signals().empty()) {
            message << "none";
        } else {
            for (std::size_t index = 0;
                index < error.recent_signals().size(); ++index) {
                if (index != 0) {
                    message << ',';
                }
                message << error.recent_signals()[index];
            }
        }
        foreign_libraries->run_error(message.str());
        diagnostics.error(
            "FSIM-RUN-DELTA-0001", message.str());
    } catch (const runtime::simir::AssertionError& error) {
        diagnostic::SourceSpan span;
        span.path = error.source().path;
        span.begin.line = error.source().line;
        span.begin.column = error.source().column;
        span.end = span.begin;
        const auto severity = [&] {
            switch (error.severity()) {
            case runtime::simir::AssertionSeverity::note:
                return diagnostic::Severity::note;
            case runtime::simir::AssertionSeverity::warning:
                return diagnostic::Severity::warning;
            case runtime::simir::AssertionSeverity::error:
                return diagnostic::Severity::error;
            case runtime::simir::AssertionSeverity::failure:
                return diagnostic::Severity::fatal;
            }
            return diagnostic::Severity::error;
        }();
        diagnostics.report(diagnostic::Diagnostic {
            severity, "FSIM-RUN-ASSERT-0001", error.what(), std::move(span), { } });
    } catch (const runtime::simir::InterpreterError& error) {
        auto message = std::string { error.what() };
        if (error.process() < design_process_count) {
            message += "; process '";
            message += simulation.process_program(error.process()).name;
            message += '\'';
        }
        foreign_libraries->run_error(message);
        diagnostics.error("FSIM-RUN-0001", std::move(message));
    } catch (const std::exception& error) {
        foreign_libraries->run_error(error.what());
        diagnostics.error("FSIM-RUN-0002", error.what());
    }
    return 1;
}

int handle_run(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto built = build_project(config, diagnostics);
    if (!built) {
        return 1;
    }
    return run_built_project(
        std::move(*built), SimulationEngine::compiled,
        config, invocation.plusargs, diagnostics, output,
        SimulationOutputSink::callback,
        [&config, &diagnostics] { return build_project(config, diagnostics); });
}

int handle_run_stdio(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto built = build_project(config, diagnostics);
    if (!built) {
        return 1;
    }
    return run_built_project(
        std::move(*built), SimulationEngine::compiled,
        config, invocation.plusargs, diagnostics, output,
        SimulationOutputSink::builtin_stdout,
        [&config, &diagnostics] { return build_project(config, diagnostics); });
}

} // namespace fsim::app::application_detail
