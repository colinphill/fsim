// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

void Interpreter::Impl::RegionFrontierComponentRuntime::
    mark_member_sync_write(const std::uint32_t member) noexcept
{
    if (!member_sync_workset_available
        || !member_sync_workset.mark(member)) {
        // Keep the established full validation path when a journal cannot
        // represent an effect; collecting a set never grants admission.
        require_full_member_sync(!member_sync_workset_available
                ? FrontierMemberSyncFullReason::unavailable_workset
                : FrontierMemberSyncFullReason::invalid_journal);
    }
}

void Interpreter::Impl::RegionFrontierComponentRuntime::
    collect_member_sync_writes(const std::uint32_t old_cursor) noexcept
{
    if (!member_sync_workset_available
        || !member_sync_private_entry || member_sync_force_full) {
        return;
    }
    if (frame.members != members.data()
        || frame.scheduler_tasks != scheduler_tasks.data()
        || frame.scheduler_task_cursor < old_cursor
        || frame.scheduler_task_cursor > frame.scheduler_task_count
        || frame.scheduler_task_count > scheduler_tasks.size()
        || frame.committed_signals != committed_signals.data()
        || frame.committed_signal_count > committed_signals.size()
        || frame.staged_events != staged_events.data()
        || frame.staged_event_count > staged_events.size()) {
        require_full_member_sync(FrontierMemberSyncFullReason::invalid_journal);
        return;
    }
    for (std::size_t index = old_cursor;
         index < frame.scheduler_task_cursor; ++index) {
        if (!member_sync_workset.mark_activation_payload(
                scheduler_tasks[index].payload)) {
            require_full_member_sync(FrontierMemberSyncFullReason::invalid_journal);
            return;
        }
    }
    for (std::size_t index = 0U; index < frame.committed_signal_count; ++index) {
        if (!member_sync_workset.mark_changed_commit(committed_signals[index])) {
            require_full_member_sync(FrontierMemberSyncFullReason::invalid_journal);
            return;
        }
    }
    for (std::size_t index = 0U; index < frame.staged_event_count; ++index) {
        const auto& event = staged_events[index];
        if (event.kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::member_activation)) {
            mark_member_sync_write(event.descriptor_index);
        }
    }
}

} // namespace fsim::runtime::simir
