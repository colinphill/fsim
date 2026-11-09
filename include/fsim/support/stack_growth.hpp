// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsim::support {

/// The stack that must remain below a running recursion level before it
/// continues on the current stack. A level of the evaluators uses far less.
inline constexpr std::size_t kStackRedZone = std::size_t { 256 } * 1024U;

/// The stack reserved for each segment that `with_stack` switches to. The
/// pages are committed as they are touched. This is the size of one segment, not
/// a limit on depth: segments chain until memory is exhausted.
inline constexpr std::size_t kStackSegmentBytes
    = std::size_t { 256 } * 1024U * 1024U;

namespace stack_growth_detail {

/// The stack of the running thread, or of the segment it runs on.
struct StackBounds {
    std::uintptr_t low { };
    bool known { };
};

/// Reads the bounds of the running thread's own stack.
/// A thread whose bounds cannot be read is taken to have room.
void discover_bounds() noexcept;

/// Runs `entry(argument)` on a fresh stack segment of `kStackSegmentBytes`
/// on the SAME thread (a context switch, so thread-local state is shared with
/// the caller), and returns when `entry` has finished. `entry` must not let an
/// exception escape. Throws std::bad_alloc when no segment can be obtained.
void run_on_segment(void (*entry)(void*), void* argument);

/// Whether the running stack has more than `kStackRedZone` left. Defined in
/// stack_growth.cpp so every shared library reads the one thread-local
/// `StackBounds` of the thread.
[[nodiscard]] bool has_room() noexcept;

} // namespace stack_growth_detail

/// Runs `body` on the current stack when at least `kStackRedZone` bytes
/// remain; otherwise on a fresh stack segment of the same thread, then
/// returns its result
/// (exceptions propagate). Segments chain without limit, so the depth of a
/// recursion is bounded only by memory. The check is one pointer compare.
template <class Body>
auto with_stack(Body&& body) -> std::invoke_result_t<Body&>
{
    using Result = std::invoke_result_t<Body&>;
    if (stack_growth_detail::has_room()) {
        return body();
    }
    struct Job {
        Body& body;
        std::exception_ptr failure;
        std::conditional_t<std::is_void_v<Result>, char,
            std::conditional_t<std::is_reference_v<Result>,
                std::remove_reference_t<Result>*, std::optional<Result>>>
            result { };
    } job { body, { }, { } };
    stack_growth_detail::run_on_segment(
        [](void* argument) {
            auto& self = *static_cast<Job*>(argument);
            try {
                if constexpr (std::is_void_v<Result>) {
                    self.body();
                } else if constexpr (std::is_reference_v<Result>) {
                    self.result = &self.body();
                } else {
                    self.result.emplace(self.body());
                }
            } catch (...) {
                self.failure = std::current_exception();
            }
        },
        &job);
    if (job.failure) {
        std::rethrow_exception(job.failure);
    }
    if constexpr (std::is_void_v<Result>) {
        return;
    } else if constexpr (std::is_reference_v<Result>) {
        return static_cast<Result>(*job.result);
    } else {
        return std::move(*job.result);
    }
}

} // namespace fsim::support
