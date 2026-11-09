// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <vector>

namespace fsim::support {

/// The values a recursive walk is inside of right now. `enter` fails when a
/// value is already active (the walk has come back to itself); an `Entry`
/// leaves the value when it goes out of scope.
template <typename Value>
class ActiveSet {
public:
    [[nodiscard]] bool enter(const Value& value)
    {
        if (std::ranges::find(values_, value) != values_.end()) {
            return false;
        }
        values_.push_back(value);
        return true;
    }

    /// Leaves the value entered last.
    void leave() { values_.pop_back(); }

    class Entry {
    public:
        explicit Entry(ActiveSet& set) noexcept : set_ { &set } { }
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        ~Entry() { set_->leave(); }

    private:
        ActiveSet* set_;
    };

private:
    std::vector<Value> values_;
};

} // namespace fsim::support
