// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::app::application_detail {

/// The callee suffix of a frame of a method the host class executor runs:
/// `<method>@host-frame-<serial>`, the serial naming the executor's frame
/// whose automatic variables the VPI reads (IEEE 1800-2023 37.43).
inline constexpr std::string_view host_frame_suffix { "@host-frame-" };

/// The live frames and threads of a run with the execution probes (simulate
/// --foreign-probes), as the VPI sees them (IEEE 1800-2023 37.43, 37.44).
/// It is fed the kernel's execution points: a thread is a SystemVerilog
/// process or a branch of a fork, a frame is the activation of a task,
/// function or method with its automatic variables. Objects are identified
/// by a serial that is never reused; an object exists from its start point
/// until its end callbacks have run (37.3.7, 37.3.8).
class VpiFrameModel final {
public:
    struct Thread {
        std::uint64_t serial { };
        runtime::simir::ProcessId process { };
        runtime::simir::ProcessId design_process { };
        std::uint64_t parent { };
        std::vector<std::uint64_t> children;
        // The frames of the call chain, the active one last.
        std::vector<std::uint64_t> frames;
        // The first statement the thread runs (vpiOrigin, 37.44).
        runtime::simir::SourceLocation origin;
        // The statement the thread executes outside any frame.
        runtime::simir::SourceLocation statement;
        bool has_statement { };
        // The start of the disable target block the thread left after that
        // statement, before reaching another one (IEEE 1800-2023 38.36.1).
        runtime::simir::SourceLocation exited;
        bool has_exited { };
        // The last call the thread made: where the next frame it starts
        // was called from (the call point follows the arguments, before the
        // callee's frame starts).
        runtime::simir::SourceLocation call_site;
        bool has_call_site { };
        // A process that is no thread (a continuous assignment, IEEE
        // 1800-2023 37.44): it owns the frames of the calls it makes (37.43
        // detail 1) but is no vpiThread object and has no thread events.
        bool hidden { };
    };

    struct Frame {
        std::uint64_t serial { };
        std::uint64_t thread { };
        std::uint64_t parent { };
        runtime::simir::ProcessId design_process { };
        // The callable's name.
        std::string callee;
        // The callable's HIR declaration (index plus one, 0 when unknown)
        // and the receiver path of a call into another instance's
        // interface (empty for the caller's own instance or a package).
        std::uint32_t callee_declaration { };
        std::string callee_receiver;
        // The suffix naming the lowering of the callable the frame runs
        // (`@hir-callable-<n>`), which its debug locals carry when another
        // lowering in the process took their names.
        std::string lowering;
        // The statement of the caller that activated the frame, where its
        // call is (vpiOrigin, 37.43 details 5 and 6), and the statement the
        // frame executes (the frame to stmt transition).
        runtime::simir::SourceLocation origin;
        runtime::simir::SourceLocation statement;
        bool has_origin { };
        bool has_statement { };
        // The start of the disable target block the frame left after its
        // statement, before reaching another one (38.36.1).
        runtime::simir::SourceLocation exited;
        bool has_exited { };
        // The source position of the call expression that started the
        // frame (the call point's position), which names the call whose
        // value the frame's result is (IEEE 1800-2023 38.15, 37.42).
        runtime::simir::SourceLocation call_site;
        bool has_call_site { };
    };

    enum class Event : std::uint8_t {
        thread_start,
        thread_end,
        thread_enter,
        frame_start,
        frame_end,
    };

    /// A change the callbacks announce; a thread's end is preceded by the
    /// end of each frame it still has (38.36.1 cbEndOfThread).
    struct Occurrence {
        Event event { };
        std::uint64_t thread { };
        std::uint64_t frame { };
    };

    /// Take one execution point; the occurrences it makes, in order.
    // `thread` is false for a process that is no thread (Thread::hidden).
    [[nodiscard]] std::vector<Occurrence> apply(
        const runtime::simir::ExecutionPoint& point, bool thread = true);
    /// An ended thread or frame ceases to exist once its end callbacks ran.
    void retire(const Occurrence& occurrence);

    [[nodiscard]] const Thread* thread(std::uint64_t serial) const;
    [[nodiscard]] const Frame* frame(std::uint64_t serial) const;
    /// The thread that runs now, 0 outside any thread.
    [[nodiscard]] std::uint64_t current_thread() const noexcept
    {
        return current_;
    }
    /// The active (innermost) frame of a thread, 0 when it has none.
    [[nodiscard]] std::uint64_t active_frame(std::uint64_t thread) const;
    /// The live thread a process runs, 0 when none.
    [[nodiscard]] std::uint64_t thread_of(
        runtime::simir::ProcessId process) const
    {
        const auto found = by_process_.find(process);
        return found == by_process_.end() ? 0U : found->second;
    }
    /// The live threads in creation order.
    // The live threads, without the hidden ones.
    [[nodiscard]] std::vector<std::uint64_t> threads() const;

private:
    std::uint64_t next_serial_ { };
    std::uint64_t current_ { };
    std::map<std::uint64_t, Thread> threads_;
    std::unordered_map<std::uint64_t, Frame> frames_;
    std::unordered_map<runtime::simir::ProcessId, std::uint64_t> by_process_;
};

} // namespace fsim::app::application_detail
