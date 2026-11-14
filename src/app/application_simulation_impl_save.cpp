// SPDX-License-Identifier: Apache-2.0
// Save, restart and reset of a run (IEEE 1800-2023 D.7-D.9; IEEE 1076-2019
// 20.7-20.9): the system tasks' host side, the run-state file, and the
// application's sections of it (the class heap, the class static store,
// functional coverage, the concurrent-assertion bookkeeping).
//
// The engine's own sections (the scheduler, the reference interpreter's
// processes and signals, the static kernel) are written by
// Interpreter::record_run_state; this file adds the sections of the state the
// application keeps beside the engine.
#include "application_simulation_internal.hpp"

#include "../runtime/run_state_archive.hpp"
#include "fsim/runtime/run_state_image.hpp"
#include "fsim/runtime/run_state_sections.hpp"
#include "fsim/runtime/scheduler_events.hpp"
#include "../runtime/uvm_run_state.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_ids.hpp"
#include "fsim/runtime/vpi_value_control.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_class_specialization.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include "../runtime/run_state_value_codecs.hpp"

namespace fsim::runtime {

// A source path is interned text; the file holds the text.
template <> struct RunStateCodec<simir::InternedString> {
    using is_codec = void;
    static void io(RunStateArchive& archive, simir::InternedString& value)
    {
        std::string text = value.str();
        archive.io(text);
        if (archive.loading() && archive.ok()) {
            value = std::move(text);
        }
    }
};

// An id is a private index; the file holds the index.
template <> struct RunStateCodec<semantic::DeclarationId> {
    using is_codec = void;
    static void io(RunStateArchive& archive, semantic::DeclarationId& value)
    {
        std::uint32_t index = value.value();
        archive.io(index);
        if (archive.loading()) {
            value = semantic::DeclarationId::from_index(index);
        }
    }
};

} // namespace fsim::runtime

namespace fsim::app {
namespace {

    [[nodiscard]] Simulation::StateFileResult failure(std::string message)
    {
        return { false, std::move(message) };
    }

    // The messages of a failed save or restore, one per line.
    [[nodiscard]] std::string join_messages(
        const std::vector<std::string>& messages)
    {
        std::string text;
        for (const auto& message : messages) {
            text += text.empty() ? "" : "\n";
            text += message;
        }
        return text;
    }

    // The integer a $reset argument printed as; an unknown value (x or z) or
    // anything else that is not a number is zero.
    [[nodiscard]] std::int32_t parse_control_integer(
        const std::string_view text) noexcept
    {
        std::int64_t value { };
        const auto [end, error]
            = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc { } || end != text.data() + text.size()) {
            return 0;
        }
        return static_cast<std::int32_t>(value);
    }

} // namespace

// ------------------------------------------------------- system tasks

void Simulation::Impl::run_simulation_control(
    const runtime::simir::ProcessId process, const std::string_view payload)
{
    constexpr std::string_view argument_prefix { "arg|" };
    if (payload.starts_with(argument_prefix)) {
        // `arg|<position>|<value>`: one evaluated argument of $reset, sent
        // ahead of the request marker.
        const auto rest = payload.substr(argument_prefix.size());
        const auto bar = rest.find('|');
        if (bar == std::string_view::npos) {
            return;
        }
        const auto position = parse_control_integer(rest.substr(0U, bar));
        if (position >= 0 && position < 3) {
            reset_arguments[process][static_cast<std::size_t>(position)]
                = parse_control_integer(rest.substr(bar + 1U));
        }
        return;
    }
    const auto bar = payload.find('|');
    const auto task = payload.substr(0U, bar);
    const auto rest = bar == std::string_view::npos
        ? std::string_view { }
        : payload.substr(bar + 1U);
    // A relative file name is relative to the workspace, as for $fopen and
    // $readmemh: the design's file root.
    auto file = std::filesystem::path { std::string { rest } };
    if (file.is_relative() && !built.file_root.empty()) {
        file = built.file_root / file;
    }
    if (task == "save" || task == "incsave") {
        // Taken at the end of the current time slot (D.9, owner decision:
        // wait for the slot's events to drain).
        queue_save({ std::move(file), task == "incsave" });
        return;
    }
    if (task == "restart") {
        restart_request = std::move(file);
        interpreter->scheduler().request_stop();
        return;
    }
    if (task == "reset") {
        // $reset [ ( stop_value [ , reset_value [ , diagnostics_value ] ] ) ]
        // (D.8): no stop_value enters interactive mode after the reset, which
        // a batch run answers by ending the run (perform_reset).
        const auto count = parse_control_integer(rest);
        const auto arguments = reset_arguments[process];
        reset_arguments.erase(process);
        Simulation::ResetRequest request;
        request.stop_value = count >= 1 ? arguments[0] : 0;
        request.reset_value = count >= 2 ? arguments[1] : 0;
        request.diagnostics_value = count >= 3 ? arguments[2] : 0;
        if (const auto refusal = reset_refusal(); !refusal.empty()) {
            save_failure = refusal + " ($reset, IEEE 1800-2023 D.8)";
            interpreter->scheduler().request_stop();
            return;
        }
        reset_requests.push_back(request);
        interpreter->scheduler().request_stop();
    }
}

bool Simulation::Impl::uvm_state_live() const
{
    return !uvm_roots_by_scope.empty() || !uvm_phase_objects.empty()
        || !uvm_run_test.roots.empty() || uvm_run_test.phase.has_value();
}

bool Simulation::Impl::uvm_state_present() const
{
    return uvm_state_live() || !uvm_registry.types().empty()
        || uvm_objects.instance_count() != 0U || uvm_resources.size() != 0U
        || !uvm_activity.events().empty();
}

std::string Simulation::Impl::uvm_unsaved_state() const
{
    // Each of these holds a function of the embedding program, which a file
    // cannot carry (the services' own state is written by "app.uvm").
    if (!uvm_components.run_state_idle()) {
        return "a UVM phase callback is running";
    }
    if (!uvm_activity.run_state_idle()) {
        return "a UVM activity observer is running";
    }
    if (!uvm_objections.run_state_idle()) {
        return "a UVM objection is being finalized";
    }
    if (!uvm_resources.run_state_idle()) {
        return "a UVM resource holds a callback of the embedding program";
    }
    if (!uvm_config_db.run_state_idle()) {
        return "a UVM config_db waiter holds a callback of the embedding "
               "program";
    }
    if (!uvm_reports.run_state_idle()) {
        return "a UVM report hook or catcher is a callback of the embedding "
               "program";
    }
    FSIM_RS_IDLE("sim.impl", uvm_pending_constructions);
    if (!uvm_pending_constructions.empty()) {
        return "a UVM object's constructor chain is running";
    }
    return { };
}

std::vector<std::string> Simulation::Impl::uvm_unused_services() const
{
    // The application's run never creates state in these services: only the
    // embedding API does (ports, FIFOs, sequences, registers, callbacks,
    // events, a test runner), with handlers that a file cannot carry. The
    // section holds none of them, so the save is refused while one is not
    // empty (FSIM-SAVE-0011).
    std::vector<std::string> names;
    const auto idle = [&names](const bool at_rest, const char* service) {
        if (!at_rest) {
            names.emplace_back(service);
        }
    };
    FSIM_RS_IDLE("sim.impl", uvm_tlm1);
    idle(uvm_tlm1.run_state_idle(), "UVM TLM 1 service");
    FSIM_RS_IDLE("sim.impl", uvm_tlm2);
    idle(uvm_tlm2.run_state_idle(), "UVM TLM 2 service");
    FSIM_RS_IDLE("sim.impl", uvm_sequences);
    idle(uvm_sequences.run_state_idle(), "UVM sequence service");
    FSIM_RS_IDLE("sim.impl", uvm_callbacks);
    idle(uvm_callbacks.run_state_idle(), "UVM callback service");
    FSIM_RS_IDLE("sim.impl", uvm_transactions);
    idle(uvm_transactions.run_state_idle(), "UVM transaction recorder");
    FSIM_RS_IDLE("sim.impl", uvm_register_model);
    idle(uvm_register_model.run_state_idle(), "UVM register model service");
    FSIM_RS_IDLE("sim.impl", uvm_foreign);
    idle(uvm_foreign.run_state_idle(), "UVM foreign service");
    FSIM_RS_IDLE("sim.impl", uvm_synchronization);
    idle(uvm_synchronization.run_state_idle(), "UVM synchronization service");
    FSIM_RS_IDLE("sim.impl", uvm_test_runner);
    idle(uvm_test_runner.run_state_idle(), "UVM test runner");
    return names;
}

void Simulation::Impl::uvm_run_state_io(runtime::RunStateArchive& archive)
{
    // The handles of the phase and objection services are bound to the
    // services of the restoring run.
    const runtime::SystemVerilogUvmRunStateOwners owners {
        uvm_phases.run_state_owner(), uvm_objections.run_state_owner()
    };
    archive.set_context(&owners);
    // The services in the order of their dependencies; the handles to class
    // objects name the objects of "app.class_heap" by slot and generation, so
    // a restore needs no rebinding of those.
    archive.io(uvm_objects);
    archive.io(uvm_components);
    archive.io(uvm_activity);
    archive.io(uvm_phases);
    archive.io(uvm_objections);
    archive.io(uvm_resources);
    archive.io(uvm_config_db);
    archive.io(uvm_factory);
    archive.io(uvm_command_line);
    archive.io(uvm_reports);
    // What the application keeps beside them: the phase objects handed to
    // user code, the run_test in progress, the port and sequencer maps.
    archive.io(uvm_phase_objects);
    archive.io(uvm_run_test);
    archive.io(uvm_tlm_owners);
    archive.io(uvm_tlm_implementations);
    archive.io(uvm_analysis_connections);
    archive.io(uvm_sequence_sequencers);
    archive.io(uvm_sequencer_items);
    archive.io(uvm_sequencer_current);
    archive.io(uvm_pending_items);
    archive.io(uvm_root_report_object);
    archive.io(uvm_singletons);
    archive.io(uvm_roots_by_scope);
    archive.set_context(nullptr);
}

void Simulation::Impl::reset_uvm_state()
{
    uvm_phase_objects.clear();
    uvm_run_test = { };
    uvm_tlm_owners.clear();
    uvm_tlm_implementations.clear();
    uvm_analysis_connections.clear();
    uvm_sequence_sequencers.clear();
    uvm_sequencer_items.clear();
    uvm_sequencer_current.clear();
    uvm_pending_items.clear();
    uvm_root_report_object = { };
    uvm_singletons.clear();
    uvm_roots_by_scope.clear();
    uvm_objects.reset_run_state();
    uvm_components.reset_run_state();
    uvm_activity.reset_run_state();
    uvm_phases.reset_run_state();
    uvm_objections.reset_run_state();
    uvm_resources.reset_run_state();
    uvm_config_db.reset_run_state();
    uvm_factory.reset_run_state();
    uvm_command_line.reset_run_state();
    uvm_reports.reset_run_state();
}

void Simulation::Impl::reapply_uvm_command_line()
{
    // The factory overrides, configuration and report settings of the command
    // line are part of time zero, as they were at the start.
    const auto arguments = uvm_command_line.settings().arguments;
    uvm_command_line.apply(arguments);
    uvm_command_line.apply_initial_report_settings(uvm_reports, uvm_objections);
    schedule_uvm_report_settings();
}

void Simulation::Impl::queue_save(SaveRequest request)
{
    save_requests.push_back(std::move(request));
    interpreter->scheduler().request_quiet_point(
        runtime::SchedulerQuietPoint::slot_end, this,
        &Simulation::Impl::save_quiet_point);
}

void Simulation::Impl::save_quiet_point(
    void* const context, runtime::Scheduler& scheduler)
{
    auto& self = *static_cast<Simulation::Impl*>(context);
    while (!self.save_requests.empty()) {
        auto request = std::move(self.save_requests.front());
        self.save_requests.pop_front();
        const auto result
            = self.write_state_file(request.file, request.incremental);
        if (!result.ok) {
            // The scheduler cannot carry the error out of its quiet point;
            // run() throws it after the stop.
            self.save_failure = result.error;
            self.save_requests.clear();
            scheduler.request_stop();
            return;
        }
    }
}

bool Simulation::Impl::perform_restart()
{
    const auto file = std::move(*restart_request);
    restart_request.reset();
    const auto result = read_state_file(file);
    if (!result.ok) {
        save_failure = result.error;
        return false;
    }
    interpreter->scheduler().clear_stop();
    return true;
}

// ------------------------------------------------------------ the file

std::string Simulation::Impl::state_identity() const
{
    // The design (its cache key is a content hash), the resolution and seed
    // the run was built with, and the command line and libraries the driver
    // names (set_state_file_identity). A restart must match all of them
    // (IEEE 1800-2023 D.9; owner decision 3).
    return "fsim-run-state|" + built.cache_key + "|" + built.artifact_identity
        + "|" + built.time_resolution + "|" + std::to_string(built.seed) + "|"
        + state_file_identity;
}

Simulation::StateFileResult Simulation::Impl::write_state_file(
    const std::filesystem::path& file, const bool incremental)
{
    if (incremental && !last_full_save) {
        return failure("FSIM-SAVE-0017: $incsave needs a full $save earlier "
                       "in the run (IEEE 1800-2023 D.9)");
    }
    const auto name = support::path_to_utf8(file);
    for (std::size_t index = 0U; index < lifecycle_hooks.size(); ++index) {
        lifecycle_hooks[index](Simulation::LifecycleEvent::start_of_save, name);
    }
    runtime::RunStateImage image { state_identity() };
    runtime::simir::RunStateReport report;
    if (!interpreter->record_run_state(image, report)) {
        return failure(report.messages.empty()
                ? std::string { "FSIM-SAVE-0018: this engine cannot write its "
                                "run state" }
                : join_messages(report.messages));
    }
    // The class S members of the application and the foreign interfaces are
    // at rest here as well (FSIM-SAVE-0011); a member that is not names the
    // work the file would lose.
    std::vector<std::string> section_errors;
    (void)section_registry.audit(section_errors);
    (void)foreign_section_registry.audit(section_errors);
    if (!section_errors.empty()) {
        return failure(join_messages(section_errors));
    }
    if (!section_registry.save(image, section_errors)) {
        return failure(join_messages(section_errors));
    }
    // The end-of-save callbacks run before the foreign sections are taken, so
    // what a library puts from them (vpi_put_data, vhpi_put_data) is in the
    // file (IEEE 1800-2023 38.36.3 cbEndOfSave, IEEE 1076-2019 20.7).
    for (std::size_t index = 0U; index < lifecycle_hooks.size(); ++index) {
        lifecycle_hooks[index](Simulation::LifecycleEvent::end_of_save, name);
    }
    if (!foreign_section_registry.save(image, section_errors)) {
        return failure(join_messages(section_errors));
    }
    std::string bytes;
    if (incremental) {
        // D.9: an incremental file holds what changed since the last full
        // $save, and names that file, which must still be present.
        auto increment = image.incremental_since(*last_full_save);
        std::string base;
        auto writer = runtime::RunStateArchive::writer(base);
        auto base_name = support::path_to_utf8(last_full_save_file);
        auto base_hash = last_full_save->state_hash();
        writer.io(base_name);
        writer.io(base_hash);
        increment.put("save.base", 1U, std::move(base));
        bytes = increment.serialize();
    } else {
        bytes = image.serialize();
    }
    // Written beside the destination and renamed, so a failed save leaves
    // the older file.
    auto temporary = file;
    temporary += ".fsim-tmp";
    std::error_code error;
    if (const auto parent = file.parent_path(); !parent.empty()) {
        support::native_fs::create_directories(parent, error);
        if (error) {
            return failure("FSIM-SAVE-0014: cannot create the directory of '"
                + name + "': " + error.message());
        }
    }
    {
        auto output = support::native_fs::open_ofstream(
            temporary, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            support::native_fs::remove(temporary, error);
            return failure("FSIM-SAVE-0014: cannot write '" + name + "'");
        }
    }
    support::native_fs::rename(temporary, file, error);
    if (error) {
        const auto message = error.message();
        support::native_fs::remove(temporary, error);
        return failure("FSIM-SAVE-0014: cannot publish '" + name
            + "': " + message);
    }
    if (!incremental) {
        last_full_save = std::move(image);
        last_full_save_file = std::filesystem::absolute(file);
    }
    return { };
}

namespace {

    [[nodiscard]] std::optional<std::string> read_whole_file(
        const std::filesystem::path& file)
    {
        auto input = support::native_fs::open_ifstream(file, std::ios::binary);
        if (!input) {
            return std::nullopt;
        }
        std::string contents(std::istreambuf_iterator<char> { input },
            std::istreambuf_iterator<char> { });
        if (!input.good() && !input.eof()) {
            return std::nullopt;
        }
        return contents;
    }

    [[nodiscard]] std::string parse_error_message(
        const runtime::RunStateImageError error, const std::string& name)
    {
        using Error = runtime::RunStateImageError;
        switch (error) {
        case Error::unsupported_format:
            return "FSIM-SAVE-0009: '" + name
                + "' was written by a newer format version";
        case Error::corrupt:
            return "FSIM-SAVE-0015: '" + name
                + "' is damaged (a checksum does not match)";
        default:
            return "FSIM-SAVE-0015: '" + name + "' is not a run-state file";
        }
    }

} // namespace

Simulation::StateFileResult Simulation::Impl::read_state_file(
    const std::filesystem::path& file)
{
    const auto name = support::path_to_utf8(file);
    const auto bytes = read_whole_file(file);
    if (!bytes) {
        return failure("FSIM-SAVE-0015: cannot read '" + name + "'");
    }
    runtime::RunStateImageError error { };
    auto parsed = runtime::RunStateImage::parse(*bytes, error);
    if (!parsed) {
        return failure(parse_error_message(error, name));
    }
    auto image = std::move(*parsed);
    // The file a following $incsave is relative to: the file restored when it
    // is a full one, else the full file the incremental one extends (D.9: an
    // increment is relative to the last $save).
    std::optional<runtime::RunStateImage> base_image;
    std::filesystem::path base_file = std::filesystem::absolute(file);
    if (image.kind() == runtime::RunStateImageKind::incremental) {
        // The full file the increment extends must be present and unchanged
        // (D.9: "errors will result" otherwise).
        const auto* const base = image.find("save.base");
        if (base == nullptr) {
            return failure("FSIM-SAVE-0017: '" + name
                + "' is incremental and names no base file");
        }
        auto in = runtime::RunStateArchive::reader(base->bytes);
        std::string base_name;
        std::uint64_t base_hash { };
        in.io(base_name);
        in.io(base_hash);
        if (!in.finished()) {
            return failure("FSIM-SAVE-0017: '" + name
                + "' names a damaged base file");
        }
        const auto base_bytes = read_whole_file(std::filesystem::path { base_name });
        if (!base_bytes) {
            return failure("FSIM-SAVE-0017: the full save file '" + base_name
                + "' that '" + name + "' extends is missing");
        }
        runtime::RunStateImageError base_error { };
        auto full = runtime::RunStateImage::parse(*base_bytes, base_error);
        if (!full || full->kind() != runtime::RunStateImageKind::full
            || full->state_hash() != base_hash) {
            return failure("FSIM-SAVE-0017: the full save file '" + base_name
                + "' has changed since '" + name + "' was saved");
        }
        base_image = *full;
        base_file = std::filesystem::absolute(std::filesystem::path { base_name });
        if (full->apply(image) != runtime::RunStateImageError::none) {
            return failure("FSIM-SAVE-0017: '" + name
                + "' does not extend '" + base_name + "'");
        }
        image = std::move(*full);
    }
    if (image.identity() != state_identity()) {
        return failure("FSIM-SAVE-0016: '" + name
            + "' was saved by another design, build or command line; a "
              "restart repeats the command line of the save (IEEE 1800-2023 "
              "D.9)");
    }
    // The design is set up as for any run, but without running it: a fresh
    // process hears start-of-restart, not start-of-simulation, and no user
    // code runs before the saved state replaces the tables in place (D.9;
    // owner decisions 2026-11-07). A run that has started already (an
    // in-process $restart) keeps going as it is.
    struct RestoringScope {
        bool& flag;
        explicit RestoringScope(bool& value) : flag(value) { flag = true; }
        ~RestoringScope() { flag = false; }
    } restoring_scope { restoring_state };
    start_vpi(true);
    start_systemc();
    refresh_observation_hooks();
    start_interpreter(true);
    // The records of the delayed VPI writes come first: each "vpi.scheduled_write"
    // event the engine restores finds its record (IEEE 1800-2023 38.34).
    if (image.find("app.vpi_writes") != nullptr) {
        std::vector<std::string> write_errors;
        if (!runtime::load_section(image, "app.vpi_writes", 1U,
                [this](runtime::RunStateArchive& archive) {
                    runtime::SystemVerilogVpiSavedWrites writes;
                    archive.io(writes);
                    std::string error;
                    if (archive.ok()
                        && !vpi_values->restore_pending(
                            std::move(writes), error)) {
                        archive.fail(error);
                    }
                },
                write_errors)) {
            return failure(join_messages(write_errors));
        }
    } else if (vpi_values != nullptr) {
        vpi_values->reset_records();
    }
    // The UVM services, for the same reason: a pending objection drain is an
    // event the engine restores, and it finds its record in the objection
    // service (IEEE 1800.2-2020 9.3.1). A file without the section holds no
    // UVM state; the registry's clear returns the services to time zero.
    if (image.find("app.uvm") != nullptr) {
        std::vector<std::string> uvm_errors;
        if (!runtime::load_section(image, "app.uvm", 1U,
                [this](runtime::RunStateArchive& archive) {
                    uvm_run_state_io(archive);
                },
                uvm_errors)) {
            return failure(join_messages(uvm_errors));
        }
    }
    // The splits the saved run made after its start, rebuilt before the
    // engine restores its state so that the plan fits it.
    if (image.find("app.late_splits") != nullptr) {
        std::vector<std::string> split_errors;
        std::vector<LateSplit> splits;
        if (!runtime::load_section(image, "app.late_splits", 3U,
                [&splits](runtime::RunStateArchive& archive) {
                    archive.io(splits);
                },
                split_errors)) {
            return failure(join_messages(split_errors));
        }
        std::string reason;
        if (!replay_late_splits(splits, reason)) {
            return failure(std::move(reason));
        }
    }
    runtime::simir::RunStateReport report;
    if (!interpreter->restore_run_state(image, report)) {
        return failure(report.messages.empty()
                ? "FSIM-SAVE-0010: the run state of '" + name
                    + "' does not fit this design"
                : join_messages(report.messages));
    }
    std::vector<std::string> section_errors;
    if (!section_registry.restore(image, section_errors)) {
        return failure(join_messages(section_errors));
    }
    // The foreign sections come last of the state and before the callbacks:
    // the data the libraries read in start-of-restart is in place, and the
    // restart callbacks they registered while saving exist to be called
    // (IEEE 1800-2023 38.36.3, IEEE 1076-2019 20.7).
    if (!foreign_section_registry.restore(image, section_errors)) {
        auto message = join_messages(section_errors);
        // A registration the build cannot restore is a changed library.
        if (const auto at = message.find("FSIM-SAVE-0016");
            at != std::string::npos) {
            message = message.substr(at);
        }
        return failure(std::move(message));
    }
    interpreter->set_reset_values(static_cast<std::int32_t>(reset_count),
        reset_value);
    last_full_save = base_image ? std::move(base_image)
                                : std::optional<runtime::RunStateImage> { image };
    last_full_save_file = std::move(base_file);
    for (std::size_t index = 0U; index < lifecycle_hooks.size(); ++index) {
        lifecycle_hooks[index](Simulation::LifecycleEvent::start_of_restart, name);
    }
    for (std::size_t index = 0U; index < lifecycle_hooks.size(); ++index) {
        lifecycle_hooks[index](Simulation::LifecycleEvent::end_of_restart, name);
    }
    return { };
}

// -------------------------------------------------------- app sections

void Simulation::Impl::register_save_sections()
{
    using runtime::RunStateArchive;
    using runtime::RunStateSectionOwner;

    // Reset bookkeeping and the small run flags (IEEE 1800-2023 D.8). One
    // visitor serves both directions.
    const auto control_io = [this](RunStateArchive& archive) {
        archive.io(reset_count);
        archive.io(reset_value);
        archive.io(coverage_execution_state);
        archive.io(stopped_covergroups);
        archive.io(numeric_metavalue_reports);
        archive.io(foreign_structure);
    };
    section_registry.add({
        .name = "app.control",
        .version = 2U,
        .save = control_io,
        .restore = control_io,
        // The class S members of the application (docs/run-state-
        // classification.tsv) and of the VPI callback manager.
        .audit = [this](std::vector<std::string>& messages) {
            const auto busy = [&](const bool condition, const char* what) {
                if (condition) {
                    messages.push_back(std::string { "FSIM-SAVE-0011: " } + what
                        + " is not at rest at the save point");
                }
            };
            // `arg|` payloads are sent in the same statement as the `reset`
            // marker that consumes them.
            FSIM_RS_IDLE("sim.impl", reset_arguments);
            busy(!reset_arguments.empty(), "the arguments of a $reset");
            // Likewise the `arg|` payloads of an assertion control task.
            FSIM_RS_IDLE("sim.impl", assertion_control_arguments);
            busy(!assertion_control_arguments.empty(),
                "the arguments of an assertion control task");
            // And the `src|` payload that names the task's call site.
            FSIM_RS_IDLE("sim.impl", assertion_control_sources);
            busy(!assertion_control_sources.empty(),
                "the call site of an assertion control task");
            // A $reset or $restart stops the run at its call; the request is
            // carried out before the run reaches another save point.
            FSIM_RS_IDLE("sim.impl", reset_requests);
            busy(!reset_requests.empty(), "a pending $reset");
            FSIM_RS_IDLE("sim.impl", restart_request);
            busy(restart_request.has_value(), "a pending $restart");
            // A failed save or restart stops the run and run() throws it.
            FSIM_RS_IDLE("sim.impl", save_failure);
            busy(save_failure.has_value(), "an earlier save or restart error");
            if (vpi_callbacks) {
                vpi_callbacks->audit_rest_state(messages);
            }
        },
    });

    // The specializations the run made from class types whose actuals read
    // a running specialization's parameters (IEEE 1800-2023 8.25): their
    // objects and static properties name them, so the restoring run makes
    // them again, in the same order, before it reads those back. Each recipe
    // travels in the class-specialization record form the design artifact
    // encodes: the running specialization as its identity, the class type
    // as its one parameter's type.
    section_registry.add({
        .name = "app.class_specializations",
        .version = 1U,
        .save = [this](RunStateArchive& archive) {
            std::vector<semantic::sv::ClassSpecialization> recipes;
            recipes.reserve(class_specialization_recipes.size());
            for (const auto& [type, context] : class_specialization_recipes) {
                auto& recipe = recipes.emplace_back();
                recipe.specialization_identity = context;
                recipe.parameters.emplace_back().type = type;
            }
            diagnostic::Engine diagnostics;
            auto bytes = serialize_systemverilog_class_specializations(
                recipes, diagnostics);
            if (!bytes) {
                archive.fail("the class specializations made by the run "
                             "cannot be encoded");
                return;
            }
            archive.io(*bytes);
        },
        .restore = [this](RunStateArchive& archive) {
            std::string bytes;
            archive.io(bytes);
            if (!archive.ok()) {
                return;
            }
            diagnostic::Engine diagnostics;
            const auto recipes = deserialize_systemverilog_class_specializations(
                bytes, "app.class_specializations", diagnostics);
            if (!recipes) {
                archive.fail("the class specializations made by the saved "
                             "run do not decode");
                return;
            }
            for (const auto& recipe : *recipes) {
                if (recipe.parameters.size() != 1U
                    || !recipe.parameters.front().type
                    || !make_class_type_specialization(
                        *recipe.parameters.front().type,
                        recipe.specialization_identity)) {
                    archive.fail("a class specialization made by the saved "
                                 "run cannot be made again");
                    return;
                }
            }
        },
        .present = [this] { return !class_specialization_recipes.empty(); },
    });

    // The class heap with the parameter context of each object (IEEE
    // 1800-2023 8): handles are slot and generation, so a saved handle names
    // the same object after the restart.
    const auto heap_io = [this](RunStateArchive& archive) {
        class_heap.run_state_io(archive);
        archive.io(class_owner_parameter_values);
    };
    section_registry.add({
        .name = "app.class_heap",
        .version = 1U,
        .save = heap_io,
        .restore = heap_io,
        .present = [this] {
            return class_heap.live_objects() != 0U
                || !class_owner_parameter_values.empty();
        },
        .clear = [this] {
            class_heap.clear();
            class_owner_parameter_values.clear();
        },
    });

    // The values of the class static members the run has touched.
    const auto static_io = [this](RunStateArchive& archive) {
        class_static_store.run_state_io(archive);
    };
    section_registry.add({
        .name = "app.class_static",
        .version = 1U,
        .save = static_io,
        .restore = static_io,
        .present = [this] { return class_static_store.property_count() != 0U; },
    });

    // The UVM services (#188): objects, components, phases and their
    // processes, objections and drains, resources and configuration,
    // factory overrides, the report server and the settings it applied, and
    // the maps the application keeps beside them, written as one section.
    // The services' hooks and the registry of types are the design's and the
    // command line's. The section is read by read_state_file ahead of the
    // engine's events, because a pending objection drain is a scheduler event
    // that finds its record here; the owner has no restore for that reason,
    // like "app.vpi_writes". A service that only the embedding API fills
    // (TLM, sequences, callbacks, registers, events) is not in the section;
    // the audit refuses the save while one holds state.
    section_registry.add({
        .name = "app.uvm",
        .version = 1U,
        .save = [this](RunStateArchive& archive) { uvm_run_state_io(archive); },
        .present = [this] { return uvm_state_present(); },
        .clear = [this] {
            if (uvm_state_present()) {
                reset_uvm_state();
            }
        },
        .refuse = [this] { return uvm_unsaved_state(); },
        .audit = [this](std::vector<std::string>& messages) {
            for (const auto& name : uvm_unused_services()) {
                messages.push_back("FSIM-SAVE-0011: the " + name
                    + " holds state that the run-state file does not carry "
                      "(it is filled only through the embedding API)");
            }
        },
    });

    // A chandle holds a foreign identity (a DPI-C pointer) that a file
    // cannot carry (IEEE 1800-2023 35.5.4); the save refuses while one lives.
    // The section has no state: a restart finds no chandle to restore.
    section_registry.add({
        .name = "app.chandle",
        .version = 1U,
        .present = [this] { return chandle_registry.live_handles() != 0U; },
        .refuse = [this]() -> std::string {
            const auto live = chandle_registry.live_handles();
            return live == 0U ? std::string { }
                              : std::to_string(live)
                    + " live chandle(s) hold foreign identities that a "
                      "file cannot carry";
        },
    });

    // Functional coverage (IEEE 1800-2023 19): the covergroup instances with
    // their bin hits, transition progress and cross state, through the codec
    // the coverage database uses.
    section_registry.add({
        .name = "app.coverage",
        .version = 1U,
        .save = [this](RunStateArchive& archive) {
            diagnostic::Engine diagnostics;
            auto bytes = serialize_systemverilog_coverage_state(
                built.systemverilog_coverage, built.systemverilog_hir,
                built.semantics, diagnostics);
            archive.io(bytes);
        },
        .restore = [this](RunStateArchive& archive) {
            std::optional<std::string> bytes;
            archive.io(bytes);
            if (!archive.ok() || !bytes) {
                return;
            }
            diagnostic::Engine diagnostics;
            auto loaded = deserialize_systemverilog_coverage_state(
                *bytes, "run-state", built.systemverilog_hir, built.semantics,
                diagnostics);
            if (!loaded) {
                archive.fail("the functional-coverage state does not fit the "
                             "design");
                return;
            }
            built.systemverilog_coverage = std::move(*loaded);
        },
        .present = [this] {
            return !built.systemverilog_coverage.instances.empty();
        },
    });

    // The delayed vpi_put_value writes still pending (IEEE 1800-2023 38.34):
    // each is a scheduler event of the kind "vpi.scheduled_write" that names its
    // record, so the records are put back before the engine restores the events
    // (read_state_file). The owner has no restore of its own for that reason; the
    // registry's clear covers a restore of a file that holds none.
    section_registry.add({
        .name = "app.vpi_writes",
        .version = 1U,
        .save = [this](RunStateArchive& archive) {
            auto writes = vpi_values->save_pending();
            archive.io(writes);
        },
        .present = [this] {
            return vpi_values != nullptr && vpi_values->pending() != 0U;
        },
        .clear = [this] {
            if (vpi_values != nullptr) {
                vpi_values->reset_records();
            }
        },
    });
    // The gate delay slots and switch control copies that vpi_put_delays
    // added after the start (IEEE 1800-2023 28.16, 38.32). A fresh process
    // has made no put, so read_state_file replays these before the engine
    // restores its state; the owner has no restore and no clear (a reset
    // keeps the structure of the running design).
    section_registry.add({
        .name = "app.late_splits",
        .version = 3U,
        .save = [this](RunStateArchive& archive) { archive.io(late_splits); },
        .present = [this] { return !late_splits.empty(); },
    });
    // The embedder's scheduler events: the owner domains of the kinds the
    // application registered, and the record each restored event's handle goes
    // back to.
    interpreter->set_run_state_event_host({
        .bind_owners = [this](runtime::SchedulerEventOwners& owners) {
            if (vpi_values != nullptr) {
                vpi_values->bind_event_owner(owners);
            }
            // A pending objection drain (set_drain_time) is an event of the
            // objection service ("app.uvm" holds its record).
            uvm_objections.bind_event_owner(owners);
        },
        .bind_handle = [this](const std::string_view kind,
                           const std::uint64_t key,
                           const runtime::ScheduledTaskHandle& handle) {
            if (kind == "uvm.objection_drain") {
                return uvm_objections.bind_restored_drain(key, handle);
            }
            return kind == "vpi.scheduled_write" && vpi_values != nullptr
                && vpi_values->bind_restored_write(key, handle);
        },
    });

    // The concurrent-assertion bookkeeping the application keeps beside the
    // evaluators (IEEE 1800-2023 16.14, 39.5.2): counters, events, the
    // enables and overrides, and the end-of-simulation obligations.
    // The assertion controls (20.11) of each assertion in the order
    // build_assertion_controls made them from the artifact: On, pass,
    // vacuous pass, fail and lock bits and the Kill count, packed.
    const auto assertions_io = [this](RunStateArchive& archive) {
        archive.io(concurrent_assertion_coverage);
        archive.io(concurrent_assertion_indices);
        archive.io(concurrent_assertion_design_processes);
        archive.io(concurrent_assertion_actions_suppressed);
        archive.io(pending_concurrent_assertions);
        archive.io(sva_obligations);
        archive.io(sva_vacuous_obligations);
        std::vector<std::uint64_t> controls;
        controls.reserve(assertion_controls.size());
        for (const auto& control : assertion_controls) {
            controls.push_back((control.on ? 1U : 0U)
                | (control.pass ? 2U : 0U) | (control.vacuous ? 4U : 0U)
                | (control.fail ? 8U : 0U) | (control.locked ? 16U : 0U)
                | (std::uint64_t { control.kills } << 32U));
        }
        archive.io(controls);
        if (archive.saving() || !archive.ok()) {
            return;
        }
        if (controls.size() != assertion_controls.size()) {
            throw std::runtime_error {
                "the saved assertion controls do not match the design"
            };
        }
        for (std::size_t index = 0U; index < controls.size(); ++index) {
            auto& control = assertion_controls[index];
            const auto bits = controls[index];
            control.on = (bits & 1U) != 0U;
            control.pass = (bits & 2U) != 0U;
            control.vacuous = (bits & 4U) != 0U;
            control.fail = (bits & 8U) != 0U;
            control.locked = (bits & 16U) != 0U;
            control.kills = static_cast<std::uint32_t>(bits >> 32U);
            publish_assertion_control(index);
        }
        interpreter->refresh_sva_actions();
    };
    section_registry.add({
        .name = "app.assertions",
        .version = 3U,
        .save = assertions_io,
        .restore = assertions_io,
        .present = [this] {
            return !concurrent_assertion_coverage.empty()
                || !concurrent_assertion_indices.empty()
                || !pending_concurrent_assertions.empty()
                || !sva_obligations.empty() || !sva_vacuous_obligations.empty()
                || std::ranges::any_of(assertion_controls,
                    [](const AssertionControl& control) {
                        return !control.on || !control.pass
                            || !control.vacuous || !control.fail
                            || control.locked || control.kills != 0U;
                    });
        },
    });
}

// ------------------------------------------------------ public interface

bool Simulation::uses_simulation_control() const noexcept
{
    return impl_->simulation_control_tasks;
}

void Simulation::set_state_file_identity(std::string text)
{
    impl_->state_file_identity = std::move(text);
}

Simulation::StateFileResult Simulation::save_state(
    const std::filesystem::path& file, const bool incremental)
{
    if (!impl_->interpreter->scheduler().at_quiescent()) {
        return failure("FSIM-SAVE-0001: the run is not at a save point; "
                       "request the save with a system task or --save-at");
    }
    return impl_->write_state_file(file, incremental);
}

Simulation::StateFileResult Simulation::restore_state(
    const std::filesystem::path& file)
{
    return impl_->read_state_file(file);
}

void Simulation::request_save(std::filesystem::path file, const bool incremental)
{
    impl_->queue_save({ std::move(file), incremental });
}

void Simulation::request_restart(std::filesystem::path file)
{
    impl_->restart_request = std::move(file);
    impl_->interpreter->scheduler().request_stop();
}

} // namespace fsim::app
