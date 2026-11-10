// SPDX-License-Identifier: Apache-2.0
//
// Run-state save and restart of the interpreter's host state (#128; IEEE
// 1800-2023 D.9 $save/$restart, IEEE 1076-2019 20.7-20.9; S3). Sections:
//
//   sched.state     time, delta, counters and every pending event as a
//                   registered kind (scheduler_events.hpp)
//   host.run        plain flags, counters, assertion and monitor settings
//   host.signals    signal values, driver contributions, forces, histories
//   host.objects    strings, containers, files, mailboxes, semaphores, events
//   host.pending    update pools, delayed writes, projected transactions
//   host.processes.* reference processes (simir_run_state_process.cpp)
//   kernel.*        the static kernel (simir_run_state_kernel.cpp)
//
// One visitor per section serves both directions, so a member written is a
// member read. The save runs only at the scheduler's quiet point, and nothing
// here is reachable from a scheduling or kernel path.
#include "simir_run_state.hpp"
#include "simir_static_kernel_state.hpp"

#include "fsim/support/native_filesystem.hpp"

#include <algorithm>
#include <fstream>
#include <span>
#include <utility>

namespace fsim::runtime::simir {

namespace run_state {
namespace {

/// The signal's value planes in the direct arrays follow `signals[id]`: after
/// a restore sets the value, bring them back in line (the planes add_signal
/// fills from the initial value).
void refresh_direct_planes(Impl& impl, const SignalId id)
{
    auto& value = impl.signals[id].initial_value;
    const auto last = impl.signal_last_values[id];
    const auto width = value.width();
    if (id < impl.direct_signal_materialization_pending.size()) {
        impl.direct_signal_materialization_pending[id] = 0U;
    }
    if (!value.is_logic9() && width <= 64U) {
        const auto word = value.unchecked_low_word();
        const auto before = last.is_logic9() || last.width() > 64U
            ? word
            : last.unchecked_low_word();
        impl.direct_signal_aval[id] = word.aval;
        impl.direct_signal_bval[id] = word.bval;
        impl.direct_signal_last_aval[id] = before.aval;
        impl.direct_signal_last_bval[id] = before.bval;
    } else {
        impl.direct_signal_aval[id] = 0U;
        impl.direct_signal_bval[id] = 0U;
        impl.direct_signal_last_aval[id] = 0U;
        impl.direct_signal_last_bval[id] = 0U;
    }
    if (value.is_logic9() && width != 0U && width <= 64U
        && id < impl.direct_signal_logic9_plane0.size()) {
        const auto word = value.logic9_low_word();
        const auto before = last.is_logic9() ? last.logic9_low_word() : word;
        impl.direct_signal_logic9_plane0[id] = word.planes[0];
        impl.direct_signal_logic9_plane1[id] = word.planes[1];
        impl.direct_signal_logic9_plane2[id] = word.planes[2];
        impl.direct_signal_logic9_plane3[id] = word.planes[3];
        impl.direct_signal_last_logic9_plane0[id] = before.planes[0];
        impl.direct_signal_last_logic9_plane1[id] = before.planes[1];
        impl.direct_signal_last_logic9_plane2[id] = before.planes[2];
        impl.direct_signal_last_logic9_plane3[id] = before.planes[3];
    }
    const auto offset = impl.direct_wide_signal_offsets[id];
    if (!value.is_logic9()) {
        const auto aval = value.aval_words();
        const auto bval = value.bval_words();
        std::ranges::copy(aval, impl.direct_wide_signal_aval.begin() + offset);
        std::ranges::copy(bval, impl.direct_wide_signal_bval.begin() + offset);
    } else {
        const auto plane0 = value.logic9_plane_words(0U);
        const auto plane1 = value.logic9_plane_words(1U);
        const auto plane2 = value.logic9_plane_words(2U);
        const auto plane3 = value.logic9_plane_words(3U);
        std::ranges::copy(plane0, impl.direct_wide_signal_aval.begin() + offset);
        std::ranges::copy(plane1, impl.direct_wide_signal_bval.begin() + offset);
        std::ranges::copy(
            plane2, impl.direct_wide_signal_logic9_plane2.begin() + offset);
        std::ranges::copy(
            plane3, impl.direct_wide_signal_logic9_plane3.begin() + offset);
    }
}

// ---- host.run --------------------------------------------------------------

void run_io(RunStateArchive& ar, Impl& impl)
{
    FSIM_RS_IO("interp.impl", time_format, ar, impl);
    // What $reset_count and $reset_value read (IEEE 1800-2023 D.8).
    FSIM_RS_IO("interp.impl", reset_count_result, ar, impl);
    FSIM_RS_IO("interp.impl", reset_value_result, ar, impl);
    FSIM_RS_IO("interp.impl", system_random_seed, ar, impl);
    FSIM_RS_IO("interp.impl", next_file_handle, ar, impl);
    FSIM_RS_IO("interp.impl", next_multichannel_channel, ar, impl);
    FSIM_RS_IO("interp.impl", next_fork_group, ar, impl);
    FSIM_RS_IO("interp.impl", next_process_generation, ar, impl);
    FSIM_RS_IO("interp.impl", next_projected_transaction_id, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_assert_counts, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_assert_enabled, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_assert_formats, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_read_severity, ar, impl);
    FSIM_RS_IO("interp.impl", pending_class_allocation_seed, ar, impl);
    FSIM_RS_IO("interp.impl", monitor, ar, impl);
    FSIM_RS_IO("interp.impl", monitor_process, ar, impl);
    FSIM_RS_IO("interp.impl", monitor_file_handle, ar, impl);
    FSIM_RS_IO("interp.impl", monitor_enabled, ar, impl);
    FSIM_RS_IO("interp.impl", monitor_generation, ar, impl);
    FSIM_RS_IO("interp.impl", monitor_publication, ar, impl);
    FSIM_RS_IO("interp.impl", monitor_systemverilog_publication, ar, impl);
    FSIM_RS_IO("interp.impl", started, ar, impl);
    FSIM_RS_IO("interp.impl", late_process_registration, ar, impl);
    FSIM_RS_IO("interp.impl", late_process_kernel_member, ar, impl);
    FSIM_RS_IO("interp.impl", package_static_startup_actions_entered, ar, impl);
    FSIM_RS_IO("interp.impl", stopped_by_design, ar, impl);
    FSIM_RS_IO("interp.impl", paused_in_final, ar, impl);
    FSIM_RS_IO("interp.impl", simulator_status, ar, impl);
    FSIM_RS_IO("interp.impl", finals_ran, ar, impl);
    FSIM_RS_IO("interp.impl", static_kernel_region_host, ar, impl);
    FSIM_RS_IO("interp.impl", program_owners, ar, impl);
    FSIM_RS_IO("interp.impl", exited_programs, ar, impl);
    FSIM_RS_IO("interp.impl", sva_pop_serial, ar, impl);
    // The monitors of concurrent assertions: each is its plan, its bounds and
    // its flat attempt block (sva::Monitor::run_state_io), by (process, site).
    FSIM_RS_IO("interp.impl", sva_monitors, ar, impl);
    // The disable nets each monitor registered when it was created (16.12).
    FSIM_RS_IO("interp.impl", sva_disable_watches, ar, impl);
    FSIM_RS_IO("interp.impl", sva_disable_watched, ar, impl);
    // The wake bits and abort nets each monitor registered when it was
    // created, and the wakes due at the next time step (16.12.14).
    FSIM_RS_IO("interp.impl", sva_wakes, ar, impl);
    FSIM_RS_IO("interp.impl", sva_abort_watches, ar, impl);
    FSIM_RS_IO("interp.impl", sva_abort_watched, ar, impl);
    FSIM_RS_IO("interp.impl", sva_wakes_due, ar, impl);
    if (ar.loading() && ar.ok() && impl.static_kernel) {
        // The restored monitors are new objects: the kernel binds each to
        // its block table entry again at its next tick or outcome pop.
        impl.static_kernel->reset_sva_blocks();
    }
    // Null transactions still pending on VHDL drivers (14.7.2): the VHPI
    // projected-waveform walk reads them beside the saved disconnect events.
    FSIM_RS_IO("interp.impl", vhdl_pending_disconnects, ar, impl);
    // Code coverage counters: the values, the saturation flag and the enable
    // flag of every counter (a counter that saturated stays flagged; one
    // switched off stays off).
    FSIM_RS_CUSTOM("interp.impl", code_coverage_counters);
    bool configured = impl.code_coverage_counters.configured();
    std::vector<std::uint64_t> counts;
    std::vector<std::uint8_t> overflowed;
    std::vector<std::uint8_t> enabled_counters;
    if (ar.saving()) {
        const auto values = std::as_const(impl.code_coverage_counters).values();
        counts.assign(values.begin(), values.end());
        const auto flags = impl.code_coverage_counters.overflow_flags();
        overflowed.assign(flags.begin(), flags.end());
        const auto enabled = impl.code_coverage_counters.enabled_flags();
        enabled_counters.assign(enabled.begin(), enabled.end());
    }
    ar.io(configured);
    ar.io(counts);
    ar.io(overflowed);
    ar.io(enabled_counters);
    if (ar.loading() && ar.ok()) {
        if (configured != impl.code_coverage_counters.configured()
            || (configured && counts.size()
                    != impl.code_coverage_counters.values().size())) {
            ar.fail("code coverage counters differ from the design");
        } else if (configured) {
            std::ranges::copy(
                counts, impl.code_coverage_counters.mutable_values().begin());
            if (!impl.code_coverage_counters.restore_flags(
                    overflowed, enabled_counters)) {
                ar.fail("code coverage counter flags differ from the design");
            }
        }
    }
}

// ---- host.signals ----------------------------------------------------------

void signals_io(RunStateArchive& ar, Impl& impl)
{
    FSIM_RS_TABLE("interp.impl", signals, ar, impl, [&](auto& element) {
        FSIM_RS_IO("interp.signal_hot", initial_value, ar, element);
    });
    FSIM_RS_IO("interp.impl", driven_values, ar, impl);
    FSIM_RS_SAME("interp.impl", vhdl_conversion_drivers, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_disconnected_drivers, ar, impl);
    FSIM_RS_SAME("interp.impl", vhdl_created_drivers, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_resolved_values, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_next_cycle_resolutions, ar, impl);
    FSIM_RS_IO("interp.impl", vhdl_deposited_before_start, ar, impl);
    FSIM_RS_IO("interp.impl", driver_values, ar, impl);
    FSIM_RS_IO("interp.impl", forced_driver_values, ar, impl);
    FSIM_RS_IO("interp.impl", forced_driver_masks, ar, impl);
    FSIM_RS_IO("interp.impl", external_driver_values, ar, impl);
    FSIM_RS_IO("interp.impl", charge_values, ar, impl);
    FSIM_RS_IO("interp.impl", signal_last_values, ar, impl);
    FSIM_RS_IO("interp.impl", signal_selection_histories, ar, impl);
    FSIM_RS_IO("interp.impl", signal_value_revisions, ar, impl);
    FSIM_RS_IO("interp.impl", sampled_values, ar, impl);
    FSIM_RS_IO("interp.impl", sampled_ring_words, ar, impl);
    FSIM_RS_IO("interp.impl", sampled_ring_offsets, ar, impl);
    if (ar.loading() && ar.ok()) {
        // The kernel's flat copy of the sampled values follows them.
        impl.refresh_sampled_value_words();
        // A compiled member's clocked read names its ring's planned start
        // (KOp::sva_sampled_load); a ring the saved run moved (a deeper
        // history allocated late) would read stale words.
        if (impl.static_kernel) {
            for (const auto& entry : impl.sampled_ring_plan()) {
                const auto found = impl.sampled_ring_offsets.find(entry.key);
                if (found != impl.sampled_ring_offsets.end()
                    && found->second != entry.offset) {
                    ar.fail("a sampled-value history moved during the saved run; "
                            "the compiled kernel reads it at its planned start");
                    break;
                }
            }
        }
        // Each key's ring must lie inside the block, and the clock index
        // names the rings by the saved starts (a ring that grew during the
        // saved run moved).
        using Ring = Impl::SampledRing;
        for (const auto& [key, offset] : impl.sampled_ring_offsets) {
            if (offset + Ring::header_words > impl.sampled_ring_words.size()
                || offset + Ring::header_words
                        + impl.sampled_ring_words[offset + Ring::capacity]
                            * impl.sampled_ring_words[offset + Ring::value_words]
                    > impl.sampled_ring_words.size()) {
                ar.fail("a sampled-value history lies outside its block");
                break;
            }
        }
        for (auto& rings : impl.sampled_history_keys_by_clock) {
            for (auto& entry : rings) {
                const auto found = impl.sampled_ring_offsets.find(entry.key);
                if (found == impl.sampled_ring_offsets.end()) {
                    ar.fail("a sampled-value history differs from the design");
                    break;
                }
                entry.ring = found->second;
            }
        }
    }
    FSIM_RS_IO("interp.impl", forced_values, ar, impl);
    FSIM_RS_IO("interp.impl", forced_masks, ar, impl);
    FSIM_RS_IO("interp.impl", port_force_layers, ar, impl);
    FSIM_RS_IO("interp.impl", assign_layers, ar, impl);
    FSIM_RS_IO("interp.impl", event_states, ar, impl);
    FSIM_RS_IO("interp.impl", signal_events, ar, impl);
    FSIM_RS_IO("interp.impl", signal_event_scheduling_stamps, ar, impl);
    FSIM_RS_IO("interp.impl", signal_transactions, ar, impl);
    FSIM_RS_IO("interp.impl", signal_transaction_systemverilog, ar, impl);
    FSIM_RS_IO("interp.impl", driver_delay_overrides, ar, impl);
    FSIM_RS_IO("interp.impl", net_delay_overrides, ar, impl);
    FSIM_RS_IO("interp.impl", foreign_net_delay_loads, ar, impl);
    // Whole-element rows of the records above.
    FSIM_RS_CUSTOM("interp.event_state", kind);
    FSIM_RS_CUSTOM("interp.event_state", due);
    FSIM_RS_CUSTOM("interp.event_state", generation);
    FSIM_RS_CUSTOM("interp.event_state", origin);
}

// ---- host.objects ----------------------------------------------------------

/// The file table: path, mode and flags, with the stream reopened at restore
/// without truncation and seeked to the saved offset (owner decision 4).
void files_io(RunStateArchive& ar, Impl& impl)
{
    FSIM_RS_CUSTOM("interp.impl", files);
    FSIM_RS_CUSTOM("interp.file", owner);
    FSIM_RS_CUSTOM("interp.file", path);
    FSIM_RS_CUSTOM("interp.file", logical_name);
    FSIM_RS_CUSTOM("interp.file", mode);
    FSIM_RS_CUSTOM("interp.file", stream);
    FSIM_RS_CUSTOM("interp.file", pushback);
    FSIM_RS_CUSTOM("interp.file", last_error);
    FSIM_RS_CUSTOM("interp.file", closed);
    FSIM_RS_CUSTOM("interp.file", readable);
    FSIM_RS_CUSTOM("interp.file", writable);
    FSIM_RS_CUSTOM("interp.file", vhdl);
    const auto count = ar.count(impl.files.size());
    if (ar.saving()) {
        for (auto& [handle, file] : impl.files) {
            auto key = handle;
            ar.io(key);
            ar.io(file.owner);
            ar.io(file.path);
            ar.io(file.logical_name);
            ar.io(file.mode);
            ar.io(file.pushback);
            ar.io(file.last_error);
            ar.io(file.closed);
            ar.io(file.readable);
            ar.io(file.writable);
            ar.io(file.vhdl);
            // Buffered output reaches the file first, so the offset is the
            // file's. The buffer's own position is read, because a stream
            // that hit end of file has failed and reports no position.
            const bool open = file.stream != nullptr && file.stream->is_open();
            std::uint64_t offset { };
            std::uint8_t at_eof { };
            if (open) {
                file.stream->flush();
                const auto position = file.stream->rdbuf()->pubseekoff(
                    0, std::ios::cur);
                offset = static_cast<std::streamoff>(position) < 0
                    ? 0U
                    : static_cast<std::uint64_t>(
                          static_cast<std::streamoff>(position));
                at_eof = file.stream->eof() ? 1U : 0U;
            }
            std::uint8_t has_stream = open ? 1U : 0U;
            ar.io(has_stream);
            ar.io(offset);
            ar.io(at_eof);
        }
        return;
    }
    impl.files.clear();
    for (std::uint64_t index = 0U; index < count && ar.ok(); ++index) {
        FileHandle handle { };
        Impl::FileState file;
        std::uint8_t has_stream { };
        std::uint64_t offset { };
        std::uint8_t at_eof { };
        ar.io(handle);
        ar.io(file.owner);
        ar.io(file.path);
        ar.io(file.logical_name);
        ar.io(file.mode);
        ar.io(file.pushback);
        ar.io(file.last_error);
        ar.io(file.closed);
        ar.io(file.readable);
        ar.io(file.writable);
        ar.io(file.vhdl);
        ar.io(has_stream);
        ar.io(offset);
        ar.io(at_eof);
        if (!ar.ok()) {
            return;
        }
        if (has_stream != 0U) {
            // The saved run wrote (or read) this file: reopen it as it is. A
            // write-only file is opened read-write so that no truncation
            // happens; an append file keeps its append flag, which moves
            // every write to the end as before.
            const bool append = file.mode.starts_with("a");
            auto flags = std::ios::binary;
            if (file.writable && !append) {
                flags |= std::ios::in | std::ios::out;
            } else if (file.writable) {
                flags |= std::ios::out | std::ios::app;
                if (file.readable) {
                    flags |= std::ios::in;
                }
            } else {
                flags |= std::ios::in;
            }
            file.stream = std::make_unique<std::fstream>(
                support::native_fs::open_fstream(file.path, flags));
            if (!file.stream->is_open()) {
                ar.fail("cannot reopen file '" + file.logical_name + "'");
                return;
            }
            file.stream->rdbuf()->pubseekpos(static_cast<std::streamoff>(offset));
            if (at_eof != 0U) {
                file.stream->setstate(std::ios::eofbit);
            }
        }
        impl.files.emplace(handle, std::move(file));
    }
}

void objects_io(RunStateArchive& ar, Impl& impl)
{
    FSIM_RS_TABLE("interp.impl", string_objects, ar, impl, [&](auto& element) {
        ar.io(element.initial_value);
    });
    FSIM_RS_TABLE("interp.impl", container_objects, ar, impl, [&](auto& element) {
        ar.io(element.initial_value);
    });
    files_io(ar, impl);
    FSIM_RS_IO("interp.impl", mailboxes, ar, impl);
    FSIM_RS_IO("interp.impl", dynamic_events, ar, impl);
    FSIM_RS_IO("interp.impl", semaphores, ar, impl);
    FSIM_RS_IO("interp.impl", stochastic_queues, ar, impl);
    FSIM_RS_IO("interp.impl", fork_groups, ar, impl);
    FSIM_RS_IO("interp.impl", cohort_snapshots, ar, impl);
    // The queued-wake token of each static sensitivity cohort.
    FSIM_RS_CUSTOM("interp.cohort", pending);
    const auto cohorts = ar.count(impl.static_sensitivity_cohorts.size());
    if (ar.loading() && cohorts != impl.static_sensitivity_cohorts.size()) {
        ar.fail("static sensitivity cohorts differ from the design");
        return;
    }
    for (auto& cohort : impl.static_sensitivity_cohorts) {
        ar.io(cohort.pending);
    }
    // Whole-record rows of the tables above.
    FSIM_RS_CUSTOM("interp.mailbox", element_kind);
    FSIM_RS_CUSTOM("interp.mailbox", element_width);
    FSIM_RS_CUSTOM("interp.mailbox", capacity);
    FSIM_RS_CUSTOM("interp.mailbox", entries);
    FSIM_RS_CUSTOM("interp.mailbox", string_entries);
    FSIM_RS_CUSTOM("interp.mailbox", string_bytes);
    FSIM_RS_CUSTOM("interp.mailbox", readers);
    FSIM_RS_CUSTOM("interp.mailbox", writers);
    FSIM_RS_CUSTOM("interp.semaphore", keys);
    FSIM_RS_CUSTOM("interp.semaphore", waiters);
    FSIM_RS_CUSTOM("interp.dynamic_event", waiters);
    FSIM_RS_CUSTOM("interp.dynamic_event", last_trigger);
    FSIM_RS_CUSTOM("interp.fork_group", parent);
    FSIM_RS_CUSTOM("interp.fork_group", site);
    FSIM_RS_CUSTOM("interp.fork_group", join);
    FSIM_RS_CUSTOM("interp.fork_group", children);
    FSIM_RS_CUSTOM("interp.fork_group", parent_resumed);
}

// ---- host.pending ----------------------------------------------------------

void pending_io(RunStateArchive& ar, Impl& impl)
{
    FSIM_RS_IO("interp.impl", systemverilog_update_slots, ar, impl);
    FSIM_RS_IO("interp.impl", systemverilog_update_free_head, ar, impl);
    FSIM_RS_IO("interp.impl", delayed_update_slots, ar, impl);
    FSIM_RS_IO("interp.impl", delayed_update_free_head, ar, impl);
    FSIM_RS_IO("interp.impl", charge_decay_handles, ar, impl);
    FSIM_RS_IO("interp.impl", pending_inertial_writes, ar, impl);
    FSIM_RS_IO("interp.impl", pending_net_delay_writes, ar, impl);
    FSIM_RS_IO("interp.impl", pending_module_path_writes, ar, impl);
    FSIM_RS_IO("interp.impl", projected_drivers, ar, impl);
    FSIM_RS_IO("interp.impl", module_timing_check_states, ar, impl);
    // Whole-record rows (the handles are rebound from the events).
    FSIM_RS_CUSTOM("interp.update_slot", process);
    FSIM_RS_CUSTOM("interp.update_slot", signal);
    FSIM_RS_CUSTOM("interp.update_slot", value);
    FSIM_RS_CUSTOM("interp.update_slot", offset);
    FSIM_RS_CUSTOM("interp.update_slot", origin);
    FSIM_RS_CUSTOM("interp.update_slot", next_free);
    FSIM_RS_CUSTOM("interp.update_slot", generation);
    FSIM_RS_CUSTOM("interp.update_slot", occupied);
    FSIM_RS_CUSTOM("interp.update_slot", reserved);
    FSIM_RS_CUSTOM("interp.update_slot", retired);
    FSIM_RS_CUSTOM("interp.pending_inertial", handle);
    FSIM_RS_CUSTOM("interp.pending_inertial", source_value);
    FSIM_RS_CUSTOM("interp.projected_driver", transactions);
}

/// The packed projections of the aggregate container aliases come from the
/// element signals the sections restored (the leaves stay mirrored, as
/// promote_container_alias_authority reads them), and every cache keyed by a
/// container revision is dropped so no reader trusts a pre-restore revision.
void rebuild_container_projections(Impl& impl)
{
    std::ranges::fill(impl.container_materialized_revisions, std::nullopt);
    std::ranges::fill(impl.container_aggregate_extract_revisions,
        Impl::no_container_extract_revision);
    for (ContainerObjectId object = 0U;
         object < impl.container_aggregate_signal_aliases.size(); ++object) {
        const auto& alias = impl.container_aggregate_signal_aliases[object];
        if (!alias) {
            continue;
        }
        const auto proxy = alias->signal;
        const auto& leaves = impl.container_element_signal_aliases.at(object);
        const auto width
            = impl.get_container_object(object).initial_value.type.element_width;
        const auto count = leaves.size();
        auto& current = *impl.aggregate_signal_current_projection.at(proxy);
        auto& stored = *impl.aggregate_signal_stored_projection.at(proxy);
        for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
            if (!leaves[ordinal]) {
                continue;
            }
            const auto offset = (count - ordinal - 1U) * width;
            const auto leaf = leaves[ordinal]->signal;
            current.insert_bits(impl.signals.at(leaf).initial_value, offset);
            stored.insert_bits(impl.driven_values.at(leaf), offset);
        }
        impl.aggregate_signal_current_projection_revisions[proxy]
            = impl.container_aggregate_current_revisions[object];
        impl.aggregate_signal_stored_projection_revisions[proxy]
            = impl.container_aggregate_stored_revisions[object];
        impl.aggregate_signal_last_changes.at(proxy).reset();
        impl.aggregate_signal_last_projection_revisions.at(proxy)
            = impl.container_aggregate_current_revisions[object];
        impl.retained_aggregate_authorities.insert(object);
    }
}

/// What the saved state implies for the derived tables, after the sections
/// are in place.
void rebuild_derived(Impl& impl)
{
    rebuild_container_projections(impl);
    for (SignalId id = 0U; id < impl.signals.size(); ++id) {
        if (id < impl.direct_signal_aval.size()
            && id < impl.signal_last_values.size()
            && id < impl.direct_wide_signal_offsets.size()) {
            refresh_direct_planes(impl, id);
        }
    }
}

}  // namespace

void audit_host(const Impl& impl, const Findings& findings)
{
    const auto busy = [&](const bool condition, const char* what) {
        if (condition) {
            findings.add(std::string { "FSIM-SAVE-0011: " } + what
                + " is not empty at the save point");
        }
    };
    // Each test names the class S row it covers.
    FSIM_RS_IDLE("interp.impl", pending_updates);
    busy(!impl.pending_updates.empty(), "the staged signal updates");
    FSIM_RS_IDLE("interp.impl", pending_update_values);
    busy(!impl.pending_update_values.empty(), "the staged update values");
    FSIM_RS_IDLE("interp.impl", driver_update_signals);
    busy(!impl.driver_update_signals.empty(), "the driver update list");
    FSIM_RS_IDLE("interp.impl", resolved_update_signals);
    busy(!impl.resolved_update_signals.empty(), "the resolved update list");
    FSIM_RS_IDLE("interp.impl", unresolved_update_signals);
    busy(!impl.unresolved_update_signals.empty(), "the unresolved update list");
    FSIM_RS_IDLE("interp.impl", direct_single_driver_update_signals);
    busy(!impl.direct_single_driver_update_signals.empty(),
        "the single-driver update list");
    FSIM_RS_IDLE("interp.impl", native_word_update_signals);
    FSIM_RS_IDLE("interp.impl", native_word_update_count);
    busy(impl.native_word_update_count != 0U, "the native word update list");
    FSIM_RS_IDLE("interp.impl", native_logic9_word_update_signals);
    FSIM_RS_IDLE("interp.impl", native_logic9_word_update_count);
    busy(impl.native_logic9_word_update_count != 0U,
        "the native Logic9 word update list");
    FSIM_RS_IDLE("interp.impl", pending_vhdl_resolutions);
    busy(!impl.pending_vhdl_resolutions.empty(), "the VHDL resolution list");
    // static_kernel_dirty_inputs is saved with the kernel (kernel.state): an
    // idle kernel may not yet have read a change of one of its inputs.
    FSIM_RS_IDLE("interp.impl", signal_selection_pending_transaction_signals);
    busy(!impl.signal_selection_pending_transaction_signals.empty(),
        "the pending signal-selection transactions");
    FSIM_RS_IDLE("interp.impl", update_commit_running);
    busy(impl.update_commit_running, "an update commit in progress");
    FSIM_RS_IDLE("interp.impl", start_in_progress);
    busy(impl.start_in_progress, "the start of the run");
    FSIM_RS_IDLE("interp.impl", package_static_initialization_active);
    busy(impl.package_static_initialization_active,
        "package static initialization");
    FSIM_RS_IDLE("interp.impl", switch_refreshing);
    busy(impl.switch_refreshing, "a switch network refresh");
    FSIM_RS_IDLE("interp.impl", static_kernel_publishing);
    busy(impl.static_kernel_publishing, "a kernel publication");
    FSIM_RS_IDLE("interp.impl", static_kernel_materializing);
    busy(impl.static_kernel_materializing, "a kernel materialization");
    // SystemC primitive-channel updates are host closures (FSIM-SAVE-0004).
    FSIM_RS_IDLE("interp.impl", pending_channel_updates);
    busy(!impl.pending_channel_updates.empty(),
        "the SystemC primitive-channel update list");
    // The step scratch of the interpreter (#185): each is emptied or cleared
    // when its step ends, so a save at a slot or cycle end finds it at rest.
    const auto marked = [](const std::vector<std::uint8_t>& marks) {
        return std::ranges::any_of(
            marks, [](const std::uint8_t mark) { return mark != 0U; });
    };
    FSIM_RS_IDLE("interp.impl", active_cohort_ready);
    busy(!impl.active_cohort_ready.empty(), "the static cohort ready list");
    FSIM_RS_IDLE("interp.impl", start_restoring);
    busy(impl.start_restoring, "a restore-mode start");
    FSIM_RS_IDLE("interp.impl", cohort_overflow_contexts);
    FSIM_RS_IDLE("interp.impl", cohort_overflow_entries);
    FSIM_RS_IDLE("interp.impl", cohort_overflow_scratch_in_use);
    busy(!impl.cohort_overflow_contexts.empty()
            || !impl.cohort_overflow_entries.empty()
            || impl.cohort_overflow_scratch_in_use,
        "the cohort overflow scratch");
    FSIM_RS_IDLE("interp.impl", container_value_reference_exposure_in_progress);
    busy(marked(impl.container_value_reference_exposure_in_progress),
        "a container reference exposure");
    FSIM_RS_IDLE("interp.impl", active_container_reference_refresh);
    busy(impl.active_container_reference_refresh != nullptr,
        "a container reference refresh");
    FSIM_RS_IDLE("interp.impl", pending_vhdl_resolution_set);
    busy(!impl.pending_vhdl_resolution_set.empty(),
        "the VHDL resolution set");
    FSIM_RS_IDLE("interp.impl", running_vhdl_resolvers);
    busy(impl.running_vhdl_resolvers, "a VHDL resolver pass");
    FSIM_RS_IDLE("interp.impl", signal_selection_transaction_region_pending);
    busy(marked(impl.signal_selection_transaction_region_pending),
        "the region-pending signal-selection transactions");
    FSIM_RS_IDLE("interp.impl", signal_selection_transaction_pending_listed);
    busy(marked(impl.signal_selection_transaction_pending_listed),
        "the listed signal-selection transactions");
    FSIM_RS_IDLE("interp.impl", unresolved_update_owner_provenance_signals);
    busy(!impl.unresolved_update_owner_provenance_signals.empty(),
        "the unresolved-update provenance list");
    FSIM_RS_IDLE("interp.impl", direct_single_driver_commit_marked);
    busy(std::ranges::any_of(impl.direct_single_driver_commit_marked,
             [](const auto mark) { return static_cast<bool>(mark); }),
        "the single-driver commit marks");
    FSIM_RS_IDLE("interp.impl", resolved_update_marked);
    busy(std::ranges::any_of(impl.resolved_update_marked,
             [](const auto mark) { return static_cast<bool>(mark); }),
        "the resolved-update marks");
    FSIM_RS_IDLE("interp.impl", update_commit_scheduled);
    busy(impl.update_commit_scheduled, "a scheduled update commit");
    FSIM_RS_IDLE("interp.impl", fused_cone_materializing);
    busy(impl.fused_cone_materializing, "a fused-cone materialization");
    // The fields of a staged update are covered by the staged list itself.
    FSIM_RS_IDLE("interp.pending_update", signal);
    FSIM_RS_IDLE("interp.pending_update", driver);
    FSIM_RS_IDLE("interp.pending_update", offset);
    FSIM_RS_IDLE("interp.pending_update", packed_value);
    FSIM_RS_IDLE("interp.pending_update", word);
    FSIM_RS_IDLE("interp.pending_update", flags);
}

bool save_host(Impl& impl, RunStateImage& image, std::vector<std::string>& errors)
{
    // Values the native publication path keeps only in the direct planes
    // become the signal values the file holds.
    for (SignalId id = 0U; id < impl.direct_signal_materialization_pending.size();
         ++id) {
        impl.materialize_direct_signal(id);
    }
    bool ok = true;
    ok = save_section(image, "host.run", host_version,
             [&](RunStateArchive& ar) { run_io(ar, impl); }, errors) && ok;
    ok = save_section(image, "host.signals", host_version,
             [&](RunStateArchive& ar) { signals_io(ar, impl); }, errors) && ok;
    ok = save_section(image, "host.objects", host_version,
             [&](RunStateArchive& ar) { objects_io(ar, impl); }, errors) && ok;
    ok = save_section(image, "host.pending", host_version,
             [&](RunStateArchive& ar) { pending_io(ar, impl); }, errors) && ok;
    return ok;
}

bool restore_host(Impl& impl, const RunStateImage& image,
    std::vector<std::string>& errors)
{
    bool ok = true;
    ok = ok && load_section(image, "host.run", host_version,
                   [&](RunStateArchive& ar) { run_io(ar, impl); }, errors);
    ok = ok && load_section(image, "host.signals", host_version,
                   [&](RunStateArchive& ar) { signals_io(ar, impl); }, errors);
    ok = ok && load_section(image, "host.objects", host_version,
                   [&](RunStateArchive& ar) { objects_io(ar, impl); }, errors);
    ok = ok && load_section(image, "host.pending", host_version,
                   [&](RunStateArchive& ar) { pending_io(ar, impl); }, errors);
    if (ok) {
        rebuild_derived(impl);
    }
    return ok;
}

}  // namespace run_state

namespace {

[[nodiscard]] bool starts_with_any(const std::string_view name) noexcept
{
    return name.starts_with("sched.") || name.starts_with("host.")
        || name.starts_with("kernel.");
}

/// The bytes of a saved scheduler state as one section.
void scheduler_io(RunStateArchive& ar, SchedulerSavedState& state)
{
    FSIM_RS_CUSTOM("sched.impl", now);
    FSIM_RS_CUSTOM("sched.impl", cycles);
    FSIM_RS_CUSTOM("sched.impl", callbacks);
    FSIM_RS_CUSTOM("sched.impl", next_sequence);
    FSIM_RS_CUSTOM("sched.current_slot", time);
    FSIM_RS_CUSTOM("sched.current_slot", delta);
    std::string bytes;
    if (ar.saving()) {
        const auto raw = serialize_scheduler_state(state);
        bytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
    }
    ar.io(bytes);
    if (ar.loading() && ar.ok()) {
        std::string error;
        const auto* const first = reinterpret_cast<const std::byte*>(bytes.data());
        if (!deserialize_scheduler_state(
                std::span<const std::byte> { first, bytes.size() }, state, error)) {
            ar.fail("scheduler state: " + error);
        }
    }
}

}  // namespace

bool Interpreter::owns_run_state_section(const std::string_view name) noexcept
{
    return starts_with_any(name);
}

bool Interpreter::record_run_state(RunStateImage& image) const
{
    RunStateReport report;
    return record_run_state(image, report);
}

bool Interpreter::record_run_state(
    RunStateImage& image, RunStateReport& report) const
{
    auto& impl = *impl_;
    run_state::Findings findings { &report.messages };
    if (!impl.started) {
        findings.add("FSIM-SAVE-0001: the run has not started");
        return false;
    }
    SchedulerEventOwners owners;
    owners.bind("interpreter", impl_.get());
    owners.bind("interpreter_api", const_cast<Interpreter*>(this));
    if (impl.run_state_event_host.bind_owners) {
        impl.run_state_event_host.bind_owners(owners);
    }
    auto saved = impl.scheduler.save_future(owners);
    if (!saved.ok) {
        for (auto& message : saved.refusals) {
            findings.add(std::move(message));
        }
        report.deferrable = saved.deferrable;
        return false;
    }
    const auto before = report.messages.size();
    run_state::audit_host(impl, findings);
    run_state::audit_processes(impl, findings);
    if (impl.static_kernel) {
        impl.static_kernel->audit_run_state(report.messages);
    }
    if (report.messages.size() != before) {
        return false;
    }
    std::vector<std::string> errors;
    bool ok = save_section(image, "sched.state", run_state::scheduler_version,
        [&](RunStateArchive& ar) { scheduler_io(ar, saved.state); }, errors);
    ok = run_state::save_host(impl, image, errors) && ok;
    ok = run_state::save_processes(impl, image, errors) && ok;
    if (impl.static_kernel) {
        ok = impl.static_kernel->save_run_state(image, errors) && ok;
    }
    for (auto& message : errors) {
        findings.add(std::move(message));
    }
    return ok;
}

bool Interpreter::restore_run_state(const RunStateImage& image)
{
    RunStateReport report;
    return restore_run_state(image, report);
}

bool Interpreter::restore_run_state(
    const RunStateImage& image, RunStateReport& report)
{
    auto& impl = *impl_;
    if (!impl.started) {
        report.messages.push_back(
            "FSIM-SAVE-0001: restore needs the run to be built and started "
            "(start() builds the tables a restore fills in)");
        return false;
    }
    SchedulerSavedState scheduler_state;
    std::vector<std::string> errors;
    // The scheduler state is read first only to learn its events; it is
    // applied last, after every owner has its saved state in place.
    if (!load_section(image, "sched.state", run_state::scheduler_version,
            [&](RunStateArchive& ar) { scheduler_io(ar, scheduler_state); },
            errors)) {
        report.messages = std::move(errors);
        return false;
    }
    // Pending work of the freshly started run is discarded: the saved events
    // replace it.
    impl.scheduler.discard_pending();
    // Processes first: a process created after start (a fork child) joins its
    // static sensitivity cohort, whose queued-wake token the host sections put
    // back.
    bool ok = run_state::restore_processes(impl, image, errors);
    ok = ok && run_state::restore_host(impl, image, errors);
    if (ok && impl.static_kernel) {
        ok = impl.static_kernel->restore_run_state(image, errors);
    }
    if (ok) {
        SchedulerEventOwners owners;
        owners.bind("interpreter", impl_.get());
        owners.bind("interpreter_api", this);
        if (impl.run_state_event_host.bind_owners) {
            impl.run_state_event_host.bind_owners(owners);
        }
        const auto result = impl.scheduler.restore_future(scheduler_state, owners,
            [&](const std::string_view kind, const std::uint64_t key,
                const ScheduledTaskHandle handle) {
                if (!impl.bind_restored_event_handle(kind, key, handle)
                    && !(impl.run_state_event_host.bind_handle
                        && impl.run_state_event_host.bind_handle(
                            kind, key, handle))) {
                    errors.push_back("FSIM-SAVE-0010: no owner takes the handle of "
                        "the restored event '" + std::string { kind } + "'");
                }
            });
        if (!result.ok) {
            for (const auto& message : result.errors) {
                errors.push_back("FSIM-SAVE-0010: " + message);
            }
            ok = false;
        }
        impl.update_commit_scheduled = std::ranges::any_of(
            scheduler_state.events, [](const SchedulerSavedEvent& event) {
                return event.kind == "interp.update_commit";
            });
        if (ok && impl.static_kernel) {
            impl.static_kernel->rearm_run_state();
        }
    }
    report.messages.insert(report.messages.end(), errors.begin(), errors.end());
    return ok && errors.empty();
}

std::uint64_t Interpreter::run_state_hash() const
{
    RunStateImage image;
    RunStateReport report;
    if (!record_run_state(image, report)) {
        return 0U;
    }
    return image.state_hash();
}

}  // namespace fsim::runtime::simir
