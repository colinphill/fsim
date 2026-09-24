// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration::elaboration_detail {

using runtime::simir::ContainerObjectId;
using runtime::simir::SignalId;
using runtime::simir::StringObjectId;

/// A parent-linked binding map with an owned local overlay.
///
/// Lookups prefer the local map, then walk parents. Assignments always shadow
/// into the local map; insertion-only operations do nothing when the key is
/// already visible in an ancestor, matching an insertion into a flattened
/// parent copy. Iteration yields each visible exact key once, with local
/// bindings taking precedence over parent bindings.
template <typename Key, typename Value>
class ScopedBindingMap final {
private:
    using Storage = std::unordered_map<Key, Value>;

public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = typename Storage::value_type;
    using size_type = typename Storage::size_type;
    using storage_type = Storage;

    class const_iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        using value_type = typename Storage::value_type;
        using difference_type = typename Storage::difference_type;
        using pointer = const value_type*;
        using reference = const value_type&;

        const_iterator() = default;

        reference operator*() const
        {
            return *position_;
        }

        pointer operator->() const
        {
            return std::addressof(*position_);
        }

        const_iterator& operator++()
        {
            ++position_;
            seek_visible();
            return *this;
        }

        const_iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        friend bool operator==(
            const const_iterator& left,
            const const_iterator& right)
        {
            if (left.layer_ == nullptr || right.layer_ == nullptr) {
                return left.layer_ == right.layer_;
            }
            return left.owner_ == right.owner_
                && left.layer_ == right.layer_
                && left.position_ == right.position_;
        }

    private:
        friend class ScopedBindingMap;

        const_iterator(
            const ScopedBindingMap* owner,
            const ScopedBindingMap* layer,
            typename Storage::const_iterator position)
            : owner_ { owner }
            , layer_ { layer }
            , position_ { position }
        {
            seek_visible();
        }

        [[nodiscard]] bool shadowed_by_child() const
        {
            for (auto* child = owner_;
                child != nullptr && child != layer_;
                child = child->parent_) {
                if (child->local_.contains(position_->first)) {
                    return true;
                }
            }
            return false;
        }

        void seek_visible()
        {
            while (layer_ != nullptr) {
                while (position_ != layer_->local_.end()) {
                    if (!shadowed_by_child()) {
                        return;
                    }
                    ++position_;
                }
                layer_ = layer_->parent_;
                if (layer_ != nullptr) {
                    position_ = layer_->local_.begin();
                }
            }
        }

        const ScopedBindingMap* owner_ { };
        const ScopedBindingMap* layer_ { };
        typename Storage::const_iterator position_ { };
    };

    ScopedBindingMap() = default;
    explicit ScopedBindingMap(const ScopedBindingMap* parent)
        : parent_ { parent }
    {
    }
    ScopedBindingMap(const ScopedBindingMap&) = default;
    ScopedBindingMap& operator=(const ScopedBindingMap&) = default;
    ScopedBindingMap(ScopedBindingMap&&) = default;
    ScopedBindingMap& operator=(ScopedBindingMap&&) = default;

    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator { this, this, local_.begin() };
    }

    [[nodiscard]] const_iterator end() const
    {
        return const_iterator { this, nullptr, { } };
    }

    [[nodiscard]] const_iterator find(const Key& key) const
    {
        for (auto* scope = this;
            scope != nullptr;
            scope = scope->parent_) {
            const auto found = scope->local_.find(key);
            if (found != scope->local_.end()) {
                return const_iterator { this, scope, found };
            }
        }
        return end();
    }

    [[nodiscard]] const Value& at(const Key& key) const
    {
        const auto found = find(key);
        if (found == end()) {
            throw std::out_of_range { "scoped binding key not found" };
        }
        return found->second;
    }

    [[nodiscard]] bool contains(const Key& key) const
    {
        return find(key) != end();
    }

    template <typename KeyArgument, typename ValueArgument>
    std::pair<const_iterator, bool> emplace(
        KeyArgument&& key,
        ValueArgument&& value)
    {
        Key owned_key { std::forward<KeyArgument>(key) };
        const auto existing = find(owned_key);
        if (existing != end()) {
            return { existing, false };
        }
        const auto [inserted_at, inserted] = local_.emplace(
            std::move(owned_key),
            std::forward<ValueArgument>(value));
        if (inserted) {
            local_insertion_order_.push_back(inserted_at->first);
        }
        return {
            const_iterator { this, this, inserted_at }, inserted
        };
    }

    template <typename KeyArgument, typename ValueArgument>
    std::pair<const_iterator, bool> insert_or_assign(
        KeyArgument&& key,
        ValueArgument&& value)
    {
        Key owned_key { std::forward<KeyArgument>(key) };
        const bool inserted = !contains(owned_key);
        const auto [assigned_at, ignored] = local_.insert_or_assign(
            std::move(owned_key),
            std::forward<ValueArgument>(value));
        static_cast<void>(ignored);
        if (inserted) {
            local_insertion_order_.push_back(assigned_at->first);
        }
        return { find(assigned_at->first), inserted };
    }

    /// Rebuild the former flat-map view for rare callers whose decision must
    /// retain unordered-map iteration order. Normal lookups use the overlay.
    [[nodiscard]] Storage compatibility_snapshot() const
    {
        if (parent_ == nullptr) {
            return local_;
        }

        auto result = parent_->compatibility_snapshot();
        for (const auto& key : local_insertion_order_) {
            const auto found = local_.find(key);
            if (found != local_.end()) {
                result.emplace(found->first, found->second);
            }
        }
        for (const auto& [key, value] : local_) {
            result.insert_or_assign(key, value);
        }
        return result;
    }

    [[nodiscard]] bool empty() const
    {
        return begin() == end();
    }

    [[nodiscard]] size_type size() const
    {
        return static_cast<size_type>(
            std::ranges::distance(begin(), end()));
    }

private:
    const ScopedBindingMap* parent_ { };
    Storage local_;
    std::vector<Key> local_insertion_order_;
};

/// A parent-linked read-only set with an owned local overlay.
template <typename Value>
class ScopedBindingSet final {
private:
    using Storage = std::unordered_set<Value>;

public:
    ScopedBindingSet() = default;
    explicit ScopedBindingSet(const ScopedBindingSet* parent)
        : parent_ { parent }
    {
    }
    ScopedBindingSet(const ScopedBindingSet&) = default;
    ScopedBindingSet& operator=(const ScopedBindingSet&) = default;
    ScopedBindingSet(ScopedBindingSet&&) = default;
    ScopedBindingSet& operator=(ScopedBindingSet&&) = default;

    [[nodiscard]] bool contains(const Value& value) const
    {
        for (auto* scope = this;
            scope != nullptr;
            scope = scope->parent_) {
            if (scope->local_.contains(value)) {
                return true;
            }
        }
        return false;
    }

    bool insert(const Value& value)
    {
        return contains(value) ? false : local_.insert(value).second;
    }

    bool emplace(const Value& value)
    {
        return insert(value);
    }

private:
    const ScopedBindingSet* parent_ { };
    Storage local_;
};

using SignalBindings = ScopedBindingMap<std::string, SignalId>;
using StringObjectBindings
    = ScopedBindingMap<std::string, StringObjectId>;
using ContainerObjectBindings
    = ScopedBindingMap<std::string, ContainerObjectId>;
using ReadOnlySignalBindings = ScopedBindingSet<SignalId>;
using ReadOnlyStringBindings = ScopedBindingSet<StringObjectId>;
using ReadOnlyContainerBindings = ScopedBindingSet<std::string>;

} // namespace fsim::elaboration::elaboration_detail
