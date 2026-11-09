// SPDX-License-Identifier: Apache-2.0
#pragma once

// The section registry of a run-state image (#128; IEEE 1800-2023 D.9, IEEE
// 1076-2019 20.7-20.9). Each subsystem that owns run state registers one
// section: a unique name, the version of its own layout, and a writer and a
// reader. The engine registers "sched.state", "host.*" and "kernel.*"
// (Interpreter::record_run_state); the application and the foreign interfaces
// add theirs ("app.heap", "app.coverage", "app.sva", "app.uvm", "foreign.vpi",
// ...) to a registry of their own and the driver saves and restores the engine
// and the registry into one RunStateImage.
//
// A section is versioned by its owner. There are no readers of older
// versions: an image whose section has another version than the registered
// one is refused (FSIM-SAVE-0009), and so is an image that lacks a registered
// section (FSIM-SAVE-0008) or fails to decode (FSIM-SAVE-0010).

#include "fsim/runtime/run_state_image.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Marks a class S row of docs/run-state-classification.tsv as covered by the
// exact test beside it (scripts/check_run_state_classification.py reads the two
// arguments). The audits of the engines (src/runtime/simir_run_state*.cpp,
// scheduler_events.cpp) and of the foreign interfaces use the same marker.
#ifndef FSIM_RS_IDLE
#define FSIM_RS_IDLE(scope, member) static_cast<void>(scope)
#endif

namespace fsim::runtime {

class RunStateArchive;

struct RunStateSectionOwner {
    /// Unique name, "<subsystem>.<part>".
    std::string name;
    /// The owner's layout version, bumped whenever the section changes.
    std::uint32_t version { };
    /// Writes the section through `archive` (a saving archive).
    std::function<void(RunStateArchive& archive)> save { };
    /// Reads the section back through `archive` (a loading archive); the
    /// owner reports a semantic failure with archive.fail().
    std::function<void(RunStateArchive& archive)> restore { };
    /// False for a section a run may lack (a subsystem that was not active
    /// at the save); restore then skips it when the image has none.
    bool required { true };
    /// Optional. When it returns false the section is not written, so a
    /// subsystem the design does not use adds nothing to the file. An owner
    /// with `present` is implicitly not required: restoring an image without
    /// the section calls `clear`.
    std::function<bool()> present { };
    /// Optional. Returns the subsystem to its empty state; called on restore
    /// for an optional section the image does not hold.
    std::function<void()> clear { };
    /// Optional. A non-empty text refuses the save with FSIM-SAVE-0021 and
    /// names why (a state that cannot be written, such as a chandle holding a
    /// foreign identity). Checked only while the section is present.
    std::function<std::string()> refuse { };
    /// Optional. Appends one FSIM-SAVE-0011 line for each member of the
    /// owner's subsystem that is class S in docs/run-state-classification.tsv
    /// and is not at rest at the save point (a call in flight, a queue that
    /// should have drained by the end of the time slot). Runs whether or not
    /// the section is present, because a subsystem that keeps no section can
    /// still hold scratch that would be lost.
    std::function<void(std::vector<std::string>& messages)> audit { };
};

class RunStateSectionRegistry final {
public:
    /// Adds an owner; a second owner of the same name is a programming error
    /// and throws std::logic_error.
    void add(RunStateSectionOwner owner);

    [[nodiscard]] const std::vector<RunStateSectionOwner>& owners() const noexcept
    {
        return owners_;
    }
    [[nodiscard]] const RunStateSectionOwner* find(
        std::string_view name) const noexcept;

    /// Runs every owner's audit (see RunStateSectionOwner::audit). False, with
    /// the messages added to `errors`, when a member is not at rest.
    [[nodiscard]] bool audit(std::vector<std::string>& errors) const;
    /// Calls every owner's clear (not only those of an absent optional
    /// section): the application returns each subsystem to its empty state
    /// for a reset (IEEE 1800-2023 D.8).
    void clear_all() const;
    /// Writes every present owner's section into `image`. False, with
    /// `errors` listing each owner that refused (FSIM-SAVE-0021) or failed,
    /// when one could not be written.
    [[nodiscard]] bool save(
        RunStateImage& image, std::vector<std::string>& errors) const;
    /// Reads every owner's section from `image`, in registration order; an
    /// optional owner whose section the image lacks is cleared.
    /// Stops at the first failure and lists it in `errors`; the state of the
    /// owners already read is then unspecified (restore into a fresh run).
    [[nodiscard]] bool restore(
        const RunStateImage& image, std::vector<std::string>& errors) const;
    /// The sections of `image` that no owner claims, for the driver to
    /// refuse or ignore.
    [[nodiscard]] std::vector<std::string> unclaimed(
        const RunStateImage& image) const;

private:
    std::vector<RunStateSectionOwner> owners_;
};

/// Writes `visit(archive)` as the section `name`; the common body of a
/// registered writer.
[[nodiscard]] bool save_section(RunStateImage& image, std::string name,
    std::uint32_t version, const std::function<void(RunStateArchive&)>& visit,
    std::vector<std::string>& errors);
/// Reads the section `name` through `visit(archive)`: checks it is present,
/// has `version`, decodes and is fully consumed. Messages carry FSIM-SAVE
/// codes.
[[nodiscard]] bool load_section(const RunStateImage& image,
    std::string_view name, std::uint32_t version,
    const std::function<void(RunStateArchive&)>& visit,
    std::vector<std::string>& errors);

} // namespace fsim::runtime
