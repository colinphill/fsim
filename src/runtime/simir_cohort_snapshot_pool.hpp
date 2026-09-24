// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

/// Owns immutable, one-shot snapshots for queued static-cohort wakeups.
/// Released slots retain vector capacity for later snapshots.
class CohortSnapshotPool {
public:
    class Token {
    public:
        Token() = default;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return slot_ != std::numeric_limits<std::size_t>::max()
                && generation_ != 0U;
        }

    private:
        friend class CohortSnapshotPool;

        Token(const std::size_t slot, const std::uint64_t generation)
            : slot_ { slot }
            , generation_ { generation }
        {
        }

        std::size_t slot_ { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation_ { };
    };

    CohortSnapshotPool() = default;
    CohortSnapshotPool(const CohortSnapshotPool&) = delete;
    CohortSnapshotPool& operator=(const CohortSnapshotPool&) = delete;
    CohortSnapshotPool(CohortSnapshotPool&&) = delete;
    CohortSnapshotPool& operator=(CohortSnapshotPool&&) = delete;

    [[nodiscard]] Token acquire(
        const std::span<const ProcessId> members)
    {
        if (members.empty()) {
            throw std::invalid_argument(
                "cannot acquire an empty cohort snapshot");
        }

        if (free_head_ != no_slot) {
            const auto index = free_head_;
            auto& slot = slots_[index];
            assign_members(slot.members, members);
            free_head_ = slot.next_free;
            slot.next_free = no_slot;
            slot.state = State::pending;
            return Token { index, slot.generation };
        }

        Slot slot;
        assign_members(slot.members, members);
        slot.state = State::pending;
        slots_.push_back(std::move(slot));
        const auto index = slots_.size() - 1U;
        return Token { index, slots_[index].generation };
    }

    /// Consume a pending snapshot once, for the lifetime of `consumer`.
    /// A stale or already consumed token returns false without invoking it.
    template <typename Consumer>
    [[nodiscard]] bool consume(const Token token, Consumer&& consumer)
    {
        auto* const slot = pending_slot(token);
        if (slot == nullptr) {
            return false;
        }

        slot->state = State::consuming;
        ConsumeGuard guard { *this, token.slot_ };
        const std::span<const ProcessId> members {
            slot->members.data(), slot->members.size() };
        std::invoke(std::forward<Consumer>(consumer), members);
        return true;
    }

    /// Invalidate one pending token and retain its snapshot capacity.
    void release(const Token token) noexcept
    {
        auto* const slot = pending_slot(token);
        if (slot == nullptr) {
            return;
        }
        retire(token.slot_);
    }

    /// Invalidate every pending token. An in-flight consume span remains valid
    /// through a reentrant discard and until its consumer returns.
    void discard() noexcept
    {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            if (slots_[index].state == State::pending) {
                retire(index);
            }
        }
    }

private:
    static constexpr std::size_t no_slot
        = std::numeric_limits<std::size_t>::max();

    enum class State : std::uint8_t {
        free,
        pending,
        consuming,
        retired,
    };

    struct Slot {
        std::vector<ProcessId> members;
        std::uint64_t generation { 1U };
        std::size_t next_free { no_slot };
        State state { State::free };
    };

    struct ConsumeGuard {
        CohortSnapshotPool& pool;
        std::size_t slot;

        ~ConsumeGuard()
        {
            pool.finish_consume(slot);
        }
    };

    static void assign_members(
        std::vector<ProcessId>& destination,
        const std::span<const ProcessId> source)
    {
        destination.clear();
        destination.reserve(source.size());
        destination.insert(destination.end(), source.begin(), source.end());
    }

    [[nodiscard]] Slot* pending_slot(const Token token) noexcept
    {
        if (!token || token.slot_ >= slots_.size()) {
            return nullptr;
        }
        auto& slot = slots_[token.slot_];
        if (slot.state != State::pending
            || slot.generation != token.generation_) {
            return nullptr;
        }
        return &slot;
    }

    void finish_consume(const std::size_t index) noexcept
    {
        retire(index);
    }

    void retire(const std::size_t index) noexcept
    {
        auto& slot = slots_[index];
        slot.members.clear();
        slot.next_free = no_slot;
        if (slot.generation == std::numeric_limits<std::uint64_t>::max()) {
            slot.state = State::retired;
            return;
        }

        ++slot.generation;
        slot.state = State::free;
        slot.next_free = free_head_;
        free_head_ = index;
    }

    std::vector<Slot> slots_;
    std::size_t free_head_ { no_slot };
};

static_assert(std::is_trivially_copyable_v<CohortSnapshotPool::Token>);
static_assert(std::is_default_constructible_v<CohortSnapshotPool::Token>);

} // namespace fsim::runtime::simir
