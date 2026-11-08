// SPDX-License-Identifier: Apache-2.0
#pragma once

// Run-state sections of the interpreter (#128; IEEE 1800-2023 D.9, IEEE 1076-2019
// 20.7-20.9). Shared by the host (simir_run_state_host.cpp), the reference
// processes (simir_run_state_process.cpp) and the static kernel
// (simir_run_state_kernel.cpp).
//
// Every member that docs/run-state-classification.tsv lists as P or P+ids is
// written by one of the FSIM_RS_* macros below. The macros carry the TSV scope
// and member name as their first two arguments, and
// scripts/check_run_state_classification.py reads them to fail when a member
// has no writer (or a writer names a member the table does not list as saved).

#include "run_state_archive.hpp"
#include "fsim/runtime/run_state_image.hpp"
#include "fsim/runtime/run_state_sections.hpp"
#include "run_state_value_codecs.hpp"
#include "simir_events.hpp"
#include "simir_internal.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A saved member: `archive.io(object.member)`.
#define FSIM_RS_IO(scope, member, archive, object) \
    (static_cast<void>(scope), (archive).io((object).member))
// A saved table sized by the design: the saved size must match, and
// `visit(element)` handles each element.
#define FSIM_RS_TABLE(scope, member, archive, object, visit) \
    (static_cast<void>(scope), (archive).sized((object).member, visit))
// A member the design fixes before the run and that a run may only change
// through structure created after start: the archive checks it equals the
// restored run's own (FSIM-SAVE-0010 otherwise; the journal of structure
// created after start is not replayed).
#define FSIM_RS_SAME(scope, member, archive, object) \
    (static_cast<void>(scope), ::fsim::runtime::simir::run_state::same( \
        (archive), (object).member, #member))
// A member written by the code that follows (a file table, a pool with its own
// layout, a member rebuilt from other saved fields); the marker names it.
#define FSIM_RS_CUSTOM(scope, member) static_cast<void>(scope)
// A class S member the save audit tests (beside its test): marks the row as
// covered for scripts/check_run_state_classification.py. Every class S row of
// an engine scope has one; members that hold no run state at rest are class W
// or D in the table instead (docs/run-state-classification.tsv).
#ifndef FSIM_RS_IDLE
#define FSIM_RS_IDLE(scope, member) static_cast<void>(scope)
#endif

namespace fsim::runtime {

/// A scheduled-task handle names a slot of the running scheduler, so a file
/// holds only whether it was live; the owner of the record binds the new handle
/// when the event is restored (Interpreter::Impl::bind_restored_event_handle).
template <> struct RunStateCodec<ScheduledTaskHandle> {
    using is_codec = void;
    static void io(RunStateArchive& archive, ScheduledTaskHandle& handle)
    {
        std::uint8_t live = static_cast<bool>(handle) ? 1U : 0U;
        archive.io(live);
        if (archive.loading()) {
            handle = ScheduledTaskHandle { };
        }
    }
};

} // namespace fsim::runtime

namespace fsim::runtime::simir::run_state {

using Impl = InterpreterEventAccess::Impl;

/// Section versions: each subsystem bumps its own when its layout changes.
inline constexpr std::uint32_t scheduler_version = 1U;
inline constexpr std::uint32_t host_version = 2U;
inline constexpr std::uint32_t process_version = 1U;
inline constexpr std::uint32_t kernel_version = 2U;

/// Processes per "host.processes.<n>" section (the unit $incsave compares).
inline constexpr std::size_t process_chunk = 4096U;

/// The archive of `value` equals the same value in the restored run.
template <typename T>
void same(RunStateArchive& archive, T& value, const char* name)
{
    if (archive.saving()) {
        archive.io(value);
        return;
    }
    std::string current;
    auto writer = RunStateArchive::writer(current);
    writer.io(value);
    T saved { };
    archive.io(saved);
    std::string expected;
    auto expect_writer = RunStateArchive::writer(expected);
    expect_writer.io(saved);
    if (archive.ok() && expected != current) {
        archive.fail(std::string { name }
            + " differs from the design: structure created after start is "
              "not replayed");
    }
}

/// The result of an engine save or restore: each reason carries its FSIM-SAVE
/// code.
struct Findings {
    std::vector<std::string>* messages { };
    void add(std::string message) const
    {
        if (messages != nullptr) {
            messages->push_back(std::move(message));
        }
    }
};

/// Pointer-identity table for objects shared between processes (lexical
/// frames, escaping callable contexts). A save numbers each object once; a
/// load hands the same object to every reference.
template <typename T> class SharedTable {
public:
    /// 0 for null, otherwise 1 + the object's position.
    std::uint64_t index_of(const std::shared_ptr<T>& pointer)
    {
        if (!pointer) {
            return 0U;
        }
        const auto found = positions_.find(pointer.get());
        if (found != positions_.end()) {
            return found->second;
        }
        objects_.push_back(pointer);
        const auto index = static_cast<std::uint64_t>(objects_.size());
        positions_.emplace(pointer.get(), index);
        return index;
    }
    [[nodiscard]] std::shared_ptr<T> at(const std::uint64_t index) const
    {
        return index == 0U || index > objects_.size()
            ? std::shared_ptr<T> { }
            : objects_[static_cast<std::size_t>(index - 1U)];
    }
    [[nodiscard]] std::vector<std::shared_ptr<T>>& objects() noexcept
    {
        return objects_;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<T>>& objects() const noexcept
    {
        return objects_;
    }

private:
    std::vector<std::shared_ptr<T>> objects_;
    std::unordered_map<const T*, std::uint64_t> positions_;
};

// ---- reference processes (simir_run_state_process.cpp) ---------------------

/// Everything process.* and the shared tables: "host.processes.shared" and the
/// chunk sections. `names` lists the section names written or read.
[[nodiscard]] bool save_processes(
    Impl& impl, RunStateImage& image, std::vector<std::string>& errors);
[[nodiscard]] bool restore_processes(
    Impl& impl, const RunStateImage& image, std::vector<std::string>& errors);
/// Reasons a reference process cannot be saved (an alternate executor holds
/// its state).
void audit_processes(const Impl& impl, const Findings& findings);

}  // namespace fsim::runtime::simir::run_state
