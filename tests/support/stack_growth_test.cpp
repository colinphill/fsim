// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/stack_growth.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

// Thread-local state must be shared by every segment of a thread.
thread_local std::uint64_t visits = 0U;
thread_local std::thread::id deepest_thread;

std::uint64_t depth_sum(const std::uint64_t depth)
{
    return fsim::support::with_stack([&]() -> std::uint64_t {
        // A frame large enough that a million levels need far more than any
        // one stack.
        volatile char padding[256];
        padding[0] = 1;
        ++visits;
        if (depth == 0U) {
            deepest_thread = std::this_thread::get_id();
            return 0U;
        }
        return 1U + depth_sum(depth - 1U) + static_cast<std::uint64_t>(padding[0]) - 1U;
    });
}

std::uint64_t throwing_depth(const std::uint64_t depth)
{
    return fsim::support::with_stack([&]() -> std::uint64_t {
        volatile char padding[256];
        padding[0] = 1;
        if (depth == 0U) {
            throw std::runtime_error { "bottom" };
        }
        return throwing_depth(depth - 1U)
            + static_cast<std::uint64_t>(padding[0]);
    });
}

void test_million_deep_recursion()
{
    visits = 0U;
    assert(depth_sum(1'000'000U) == 1'000'000U);
    // Every level, in whichever segment it ran, counted on this thread, and
    // the deepest level ran on this thread.
    assert(visits == 1'000'001U);
    assert(deepest_thread == std::this_thread::get_id());
}

void test_exceptions_cross_segments()
{
    bool caught = false;
    try {
        static_cast<void>(throwing_depth(200'000U));
    } catch (const std::runtime_error& error) {
        caught = std::string { error.what() } == "bottom";
    }
    assert(caught);
}

void test_return_kinds()
{
    int value = 7;
    auto& same = fsim::support::with_stack([&]() -> int& { return value; });
    assert(&same == &value);
    fsim::support::with_stack([&] { value = 9; });
    assert(value == 9);
    assert(fsim::support::with_stack([] { return std::string { "text" }; })
        == "text");
}

} // namespace

int main()
{
    test_million_deep_recursion();
    test_exceptions_cross_segments();
    test_return_kinds();
    return 0;
}
