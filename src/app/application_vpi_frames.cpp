// SPDX-License-Identifier: Apache-2.0
#include "application_vpi_frames.hpp"

#include <algorithm>
#include <utility>

namespace fsim::app::application_detail {

std::vector<VpiFrameModel::Occurrence> VpiFrameModel::apply(
    const runtime::simir::ExecutionPoint& point)
{
    using Kind = runtime::simir::ExecutionPointKind;
    std::vector<Occurrence> occurrences;
    if (point.kind == Kind::thread_start) {
        // A thread is created by its process starting or by a fork; it runs
        // later, so the running thread stays (IEEE 1800-2023 38.36.1).
        Thread created;
        created.serial = ++next_serial_;
        created.process = point.process;
        created.design_process = point.design_process;
        created.origin = point.source;
        if (point.parent_process) {
            if (const auto found = by_process_.find(*point.parent_process);
                found != by_process_.end()) {
                created.parent = found->second;
                threads_.at(found->second).children.push_back(created.serial);
            }
        }
        by_process_[point.process] = created.serial;
        occurrences.push_back({ Event::thread_start, created.serial, 0U });
        threads_.emplace(created.serial, std::move(created));
        return occurrences;
    }
    const auto owner = by_process_.find(point.process);
    if (owner == by_process_.end()) {
        return occurrences;
    }
    auto& thread = threads_.at(owner->second);
    switch (point.kind) {
    case Kind::thread_enter:
        current_ = thread.serial;
        occurrences.push_back({ Event::thread_enter, thread.serial, 0U });
        break;
    case Kind::thread_end:
        // All the frames the thread activated end before it does
        // (IEEE 1800-2023 38.36.1 cbEndOfThread).
        for (auto frame = thread.frames.rbegin(); frame != thread.frames.rend();
             ++frame) {
            occurrences.push_back({ Event::frame_end, thread.serial, *frame });
        }
        occurrences.push_back({ Event::thread_end, thread.serial, 0U });
        break;
    case Kind::frame_start: {
        Frame started;
        started.serial = ++next_serial_;
        started.thread = thread.serial;
        started.parent = thread.frames.empty() ? 0U : thread.frames.back();
        started.design_process = point.design_process;
        started.callee = point.callee;
        // The caller's active statement holds the call (37.43 detail 5).
        if (started.parent != 0U) {
            const auto& caller = frames_.at(started.parent);
            started.origin = caller.statement;
            started.has_origin = caller.has_statement;
        } else {
            started.origin = thread.statement;
            started.has_origin = thread.has_statement;
        }
        thread.frames.push_back(started.serial);
        current_ = thread.serial;
        occurrences.push_back(
            { Event::frame_start, thread.serial, started.serial });
        frames_.emplace(started.serial, std::move(started));
        break;
    }
    case Kind::frame_end:
        if (!thread.frames.empty()) {
            current_ = thread.serial;
            occurrences.push_back(
                { Event::frame_end, thread.serial, thread.frames.back() });
        }
        break;
    case Kind::call:
        current_ = thread.serial;
        break;
    // The statement the thread or its active frame executes (37.43 detail 4;
    // the parent frame keeps the statement that activated the child, detail
    // 5).
    case Kind::statement:
    case Kind::assertion:
    case Kind::wait:
    case Kind::process_entry:
        current_ = thread.serial;
        if (thread.frames.empty()) {
            thread.statement = point.source;
            thread.has_statement = true;
        } else {
            auto& active = frames_.at(thread.frames.back());
            active.statement = point.source;
            active.has_statement = true;
        }
        break;
    case Kind::process_suspend:
        if (current_ == thread.serial) {
            current_ = 0U;
        }
        break;
    case Kind::call_return:
    case Kind::thread_start:
    case Kind::object_created:
        break;
    }
    return occurrences;
}

void VpiFrameModel::retire(const Occurrence& occurrence)
{
    if (occurrence.event == Event::frame_end) {
        if (const auto found = threads_.find(occurrence.thread);
            found != threads_.end()) {
            std::erase(found->second.frames, occurrence.frame);
        }
        frames_.erase(occurrence.frame);
        return;
    }
    if (occurrence.event != Event::thread_end) {
        return;
    }
    const auto found = threads_.find(occurrence.thread);
    if (found == threads_.end()) {
        return;
    }
    const auto& ended = found->second;
    if (const auto parent = threads_.find(ended.parent);
        ended.parent != 0U && parent != threads_.end()) {
        std::erase(parent->second.children, ended.serial);
    }
    if (const auto owner = by_process_.find(ended.process);
        owner != by_process_.end() && owner->second == ended.serial) {
        by_process_.erase(owner);
    }
    if (current_ == ended.serial) {
        current_ = 0U;
    }
    threads_.erase(found);
}

const VpiFrameModel::Thread* VpiFrameModel::thread(
    const std::uint64_t serial) const
{
    const auto found = threads_.find(serial);
    return found == threads_.end() ? nullptr : &found->second;
}

const VpiFrameModel::Frame* VpiFrameModel::frame(
    const std::uint64_t serial) const
{
    const auto found = frames_.find(serial);
    return found == frames_.end() ? nullptr : &found->second;
}

std::uint64_t VpiFrameModel::active_frame(const std::uint64_t serial) const
{
    const auto* const owner = thread(serial);
    return owner == nullptr || owner->frames.empty() ? 0U
                                                     : owner->frames.back();
}

std::vector<std::uint64_t> VpiFrameModel::threads() const
{
    std::vector<std::uint64_t> live;
    live.reserve(threads_.size());
    for (const auto& entry : threads_) {
        live.push_back(entry.first);
    }
    return live;
}

} // namespace fsim::app::application_detail
