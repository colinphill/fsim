// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct DriverRecord {
    ProcessId process { };
    PackedLogic4 value;
    DriveStrength strength { };
};

/// Inline-first storage for the drivers of one signal.
///
/// The first inserted record stays in the inline slot. Later records live in
/// ProcessId order in overflow_. Traversal merges the two sources, so callers
/// always see ProcessId order even when drivers were inserted out of order.
/// Pointers returned by find() and sole() are short-lived borrows: reacquire
/// them after any insertion, assignment, or move of this table.
class DriverTable {
public:
    DriverTable() = default;
    DriverTable(const DriverTable&) = default;
    DriverTable& operator=(const DriverTable&) = default;
    DriverTable(DriverTable&& other) noexcept
        : inline_record_(std::move(other.inline_record_)),
          overflow_(std::move(other.overflow_))
    {
        other.inline_record_.reset();
        other.overflow_.clear();
    }

    DriverTable& operator=(DriverTable&& other) noexcept
    {
        if (this == &other)
            return *this;

        inline_record_ = std::move(other.inline_record_);
        overflow_ = std::move(other.overflow_);
        other.inline_record_.reset();
        other.overflow_.clear();
        return *this;
    }

    [[nodiscard]] DriverRecord* find(ProcessId process) noexcept
    {
        if (inline_record_ && inline_record_->process == process)
            return &*inline_record_;

        const auto position = lower_bound(process);
        if (position != overflow_.end() && position->process == process)
            return &*position;
        return nullptr;
    }

    [[nodiscard]] const DriverRecord* find(ProcessId process) const noexcept
    {
        if (inline_record_ && inline_record_->process == process)
            return &*inline_record_;

        const auto position = lower_bound(process);
        if (position != overflow_.end() && position->process == process)
            return &*position;
        return nullptr;
    }

    /// Insert record if its ProcessId is absent. Returns true on insertion.
    /// Use find() to obtain the record, and reacquire it after later inserts.
    [[nodiscard]] bool insert_if_absent(DriverRecord record)
    {
        if (inline_record_ && inline_record_->process == record.process)
            return false;

        const auto position = lower_bound(record.process);
        if (position != overflow_.end() &&
            position->process == record.process)
            return false;

        if (!inline_record_) {
            inline_record_.emplace(std::move(record));
            return true;
        }

        overflow_.insert(position, std::move(record));
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return overflow_.size() + (inline_record_ ? 1U : 0U);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return !inline_record_;
    }

    [[nodiscard]] DriverRecord* sole() noexcept
    {
        return size() == 1 ? &*inline_record_ : nullptr;
    }

    [[nodiscard]] const DriverRecord* sole() const noexcept
    {
        return size() == 1 ? &*inline_record_ : nullptr;
    }

    /// Visit all records in increasing ProcessId order without allocating.
    /// The callback may change record values or strengths, but must not change
    /// this table's membership while traversal is in progress.
    template <typename Visitor>
    void for_each_in_process_order(Visitor&& visitor)
    {
        auto&& callback = visitor;
        std::size_t overflow_index = 0;
        bool inline_pending = inline_record_.has_value();
        while (inline_pending || overflow_index < overflow_.size()) {
            if (inline_pending &&
                (overflow_index == overflow_.size() ||
                    inline_record_->process <=
                        overflow_[overflow_index].process)) {
                callback(*inline_record_);
                inline_pending = false;
            } else {
                callback(overflow_[overflow_index]);
                ++overflow_index;
            }
        }
    }

    /// Const counterpart to for_each_in_process_order().
    template <typename Visitor>
    void for_each_in_process_order(Visitor&& visitor) const
    {
        auto&& callback = visitor;
        std::size_t overflow_index = 0;
        bool inline_pending = inline_record_.has_value();
        while (inline_pending || overflow_index < overflow_.size()) {
            if (inline_pending &&
                (overflow_index == overflow_.size() ||
                    inline_record_->process <=
                        overflow_[overflow_index].process)) {
                callback(*inline_record_);
                inline_pending = false;
            } else {
                callback(overflow_[overflow_index]);
                ++overflow_index;
            }
        }
    }

private:
    using Overflow = std::vector<DriverRecord>;
    using const_iterator = Overflow::const_iterator;

    [[nodiscard]] const_iterator lower_bound(ProcessId process) const noexcept
    {
        return std::lower_bound(
            overflow_.begin(), overflow_.end(), process,
            [](const DriverRecord& record, ProcessId key) {
                return record.process < key;
            });
    }

    [[nodiscard]] Overflow::iterator lower_bound(ProcessId process) noexcept
    {
        return std::lower_bound(
            overflow_.begin(), overflow_.end(), process,
            [](const DriverRecord& record, ProcessId key) {
                return record.process < key;
            });
    }

    std::optional<DriverRecord> inline_record_;
    Overflow overflow_;
};

} // namespace fsim::runtime::simir
