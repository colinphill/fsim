// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <version>

namespace fsim::support {

#if defined(__cpp_lib_atomic_shared_ptr)

template <typename T>
using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;

#else

// std::atomic<std::shared_ptr<T>> for standard libraries without it, such as
// libc++. A spinlock serializes access, so every operation is sequentially
// consistent whatever order it is given. Replaced pointers are released after
// the lock, so no deleter runs while it is held.
template <typename T>
class AtomicSharedPtr {
public:
    AtomicSharedPtr() noexcept = default;

    // Not explicit, like the std::atomic constructor it stands in for.
    AtomicSharedPtr(std::shared_ptr<T> desired) noexcept
        : value_(std::move(desired))
    {
    }

    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

    [[nodiscard]] std::shared_ptr<T> load(
        std::memory_order = std::memory_order_seq_cst) const noexcept
    {
        const Guard guard { lock_ };
        return value_;
    }

    void store(std::shared_ptr<T> desired,
        std::memory_order = std::memory_order_seq_cst) noexcept
    {
        const Guard guard { lock_ };
        value_.swap(desired);
    }

    [[nodiscard]] std::shared_ptr<T> exchange(std::shared_ptr<T> desired,
        std::memory_order = std::memory_order_seq_cst) noexcept
    {
        {
            const Guard guard { lock_ };
            value_.swap(desired);
        }
        return desired;
    }

    // Succeeds when the stored and expected pointers are equal and share
    // ownership; otherwise loads the stored pointer into expected.
    bool compare_exchange_strong(std::shared_ptr<T>& expected,
        std::shared_ptr<T> desired, std::memory_order,
        std::memory_order) noexcept
    {
        std::shared_ptr<T> observed;
        {
            const Guard guard { lock_ };
            if (value_ == expected && !value_.owner_before(expected)
                && !expected.owner_before(value_)) {
                value_.swap(desired);
                return true;
            }
            observed = value_;
        }
        expected.swap(observed);
        return false;
    }

    bool compare_exchange_strong(std::shared_ptr<T>& expected,
        std::shared_ptr<T> desired,
        std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        return compare_exchange_strong(
            expected, std::move(desired), order, order);
    }

    // The lock rules out spurious failure, so weak is strong.
    bool compare_exchange_weak(std::shared_ptr<T>& expected,
        std::shared_ptr<T> desired, std::memory_order success,
        std::memory_order failure) noexcept
    {
        return compare_exchange_strong(
            expected, std::move(desired), success, failure);
    }

    bool compare_exchange_weak(std::shared_ptr<T>& expected,
        std::shared_ptr<T> desired,
        std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        return compare_exchange_strong(
            expected, std::move(desired), order, order);
    }

private:
    class Guard {
    public:
        explicit Guard(std::atomic_flag& flag) noexcept
            : flag_(flag)
        {
            while (flag_.test_and_set(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        }

        ~Guard() { flag_.clear(std::memory_order_release); }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        std::atomic_flag& flag_;
    };

    mutable std::atomic_flag lock_;
    std::shared_ptr<T> value_;
};

#endif

} // namespace fsim::support
