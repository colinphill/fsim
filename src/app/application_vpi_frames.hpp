// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace fsim::app::application_detail {

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
    };

    struct Frame {
        std::uint64_t serial { };
        std::uint64_t thread { };
        std::uint64_t parent { };
        runtime::simir::ProcessId design_process { };
        // The callable's name.
        std::string callee;
        // The statement of the caller that activated the frame, where its
        // call is (vpiOrigin, 37.43 details 5 and 6), and the statement the
        // frame executes (the frame to stmt transition).
        runtime::simir::SourceLocation origin;
        runtime::simir::SourceLocation statement;
        bool has_origin { };
        bool has_statement { };
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
    [[nodiscard]] std::vector<Occurrence> apply(
        const runtime::simir::ExecutionPoint& point);
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
    /// The live threads in creation order.
    [[nodiscard]] std::vector<std::uint64_t> threads() const;

private:
    std::uint64_t next_serial_ { };
    std::uint64_t current_ { };
    std::map<std::uint64_t, Thread> threads_;
    std::unordered_map<std::uint64_t, Frame> frames_;
    std::unordered_map<runtime::simir::ProcessId, std::uint64_t> by_process_;
};

} // namespace fsim::app::application_detail
