// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace fsim::app::application_detail {

/// Diagnostic intervals share a process-local monotonic origin. Intervals
/// may nest: consumers must not add their inclusive durations together.
/// Names are static identifiers without whitespace. This is used only at
/// coarse application phase boundaries, never inside simulation hot loops.
class ScopedPhaseProfile final {
public:
    explicit ScopedPhaseProfile(const char* name) noexcept
        : name_(name)
        , enabled_(std::getenv("FSIM_PROFILE_PHASES") != nullptr)
    {
        if (enabled_) {
            origin_ = origin();
            begin_ = Clock::now();
        }
    }

    ScopedPhaseProfile(const ScopedPhaseProfile&) = delete;
    ScopedPhaseProfile& operator=(const ScopedPhaseProfile&) = delete;

    ~ScopedPhaseProfile()
    {
        if (!enabled_) {
            return;
        }
        const auto end = Clock::now();
        const auto milliseconds = [](const Clock::duration elapsed) {
            return std::chrono::duration<double, std::milli>(elapsed).count();
        };
        // One stdio call keeps each record intact across profiling threads.
        // The interval records scope exit, including early failure returns;
        // it is not evidence that the operation succeeded.
        std::fprintf(stderr,
            "FSIM-PHASE name=%s start_ms=%.6f end_ms=%.6f elapsed_ms=%.6f\n",
            name_, milliseconds(begin_ - origin_),
            milliseconds(end - origin_), milliseconds(end - begin_));
    }

private:
    using Clock = std::chrono::steady_clock;

    static Clock::time_point origin() noexcept
    {
        static const auto value = Clock::now();
        return value;
    }

    const char* name_;
    bool enabled_;
    Clock::time_point origin_ { };
    Clock::time_point begin_ { };
};

} // namespace fsim::app::application_detail
