// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

template <typename T>
class CopyOnWriteVector;

namespace process_layout_detail {

template <typename T>
class ProcessLayoutReadView {
public:
    using const_iterator = typename std::vector<T>::const_iterator;
    using const_reverse_iterator
        = typename std::vector<T>::const_reverse_iterator;

    explicit ProcessLayoutReadView(
        std::shared_ptr<const std::vector<T>> values) noexcept
        : owner_(std::move(values))
        , values_(owner_ == nullptr ? &empty_values() : owner_.get())
    {
    }

    explicit ProcessLayoutReadView(const std::vector<T>& values) noexcept
        : values_(&values)
    {
    }

    [[nodiscard]] const std::vector<T>& vector() const noexcept
    {
        return *values_;
    }

    [[nodiscard]] bool empty() const noexcept { return values_->empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return values_->size(); }
    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return values_->capacity();
    }

    [[nodiscard]] const T& operator[](const std::size_t index) const noexcept
    {
        return (*values_)[index];
    }

    [[nodiscard]] const T& at(const std::size_t index) const
    {
        return values_->at(index);
    }

    [[nodiscard]] const T& front() const { return values_->front(); }
    [[nodiscard]] const T& back() const { return values_->back(); }
    [[nodiscard]] const T* data() const noexcept { return values_->data(); }
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return values_->begin();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return values_->end();
    }
    [[nodiscard]] const_iterator cbegin() const noexcept
    {
        return values_->cbegin();
    }
    [[nodiscard]] const_iterator cend() const noexcept
    {
        return values_->cend();
    }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept
    {
        return values_->rbegin();
    }
    [[nodiscard]] const_reverse_iterator rend() const noexcept
    {
        return values_->rend();
    }
    [[nodiscard]] operator const std::vector<T>&() const noexcept
    {
        return *values_;
    }

private:
    friend struct ProcessLayoutAccess;

    [[nodiscard]] static const std::vector<T>& empty_values() noexcept
    {
        static const std::vector<T> empty;
        return empty;
    }

    std::shared_ptr<const std::vector<T>> owner_;
    const std::vector<T>* values_ { };
};

/// Non-pinning read access for synchronous implementation scans. An unexposed
/// view owns its immutable backing snapshot. A view of a public-ref facade is
/// borrowed and follows normal std::vector lifetime/invalidation rules.
struct ProcessLayoutAccess {
    template <typename T>
    [[nodiscard]] static ProcessLayoutReadView<T> view(
        const CopyOnWriteVector<T>& values) noexcept;

    template <typename T>
    [[nodiscard]] static T copy_at(
        const CopyOnWriteVector<T>& values, const std::size_t index)
    {
        return view(values).at(index);
    }

    template <typename T>
    [[nodiscard]] static bool can_share(
        const CopyOnWriteVector<T>& left,
        const CopyOnWriteVector<T>& right) noexcept;

    template <typename T>
    static void share(
        CopyOnWriteVector<T>& destination,
        const CopyOnWriteVector<T>& representative) noexcept;

    template <typename T>
    [[nodiscard]] static std::vector<T>& archive_target(
        CopyOnWriteVector<T>& values);
};

template <typename T>
auto archive_fields(const ProcessLayoutReadView<T>& values)
{
    return std::tie(values.vector());
}

} // namespace process_layout_detail

/// Copy-on-write storage for process-layout metadata.
///
/// Value-only operations can share immutable backing. Public APIs returning a
/// reference, iterator, pointer, span, or vector reference materialize the
/// wrapper's stable inline vector facade. Copies from an exposed wrapper are
/// deep copies. Assignment and move keep the facade object attached to its
/// wrapper, so retained vector-object references remain valid. The first
/// reference transition is serialized with other transitions; mutation and
/// move retain std::vector's single-writer requirements. Internal parallel
/// readers should use ProcessLayoutAccess::view instead of public references.
template <typename T>
class CopyOnWriteVector {
public:
    using value_type = T;
    using allocator_type = typename std::vector<T>::allocator_type;
    using size_type = typename std::vector<T>::size_type;
    using difference_type = typename std::vector<T>::difference_type;
    using reference = typename std::vector<T>::reference;
    using const_reference = typename std::vector<T>::const_reference;
    using pointer = typename std::vector<T>::pointer;
    using const_pointer = typename std::vector<T>::const_pointer;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;
    using reverse_iterator = typename std::vector<T>::reverse_iterator;
    using const_reverse_iterator
        = typename std::vector<T>::const_reverse_iterator;

    CopyOnWriteVector() noexcept = default;

    CopyOnWriteVector(std::initializer_list<T> values)
    {
        assign(values);
    }

    CopyOnWriteVector(const std::vector<T>& values)
    {
        assign(values.begin(), values.end());
    }

    CopyOnWriteVector(std::vector<T>&& values)
    {
        replace(std::move(values));
    }

    CopyOnWriteVector(const CopyOnWriteVector& other)
    {
        TransitionGuard guard { other.transition_lock_ };
        if (other.exposed_.load(std::memory_order_acquire)) {
            storage_.store(
                std::make_shared<Control>(other.exposed_values_),
                std::memory_order_release);
        } else {
            storage_.store(
                other.storage_.load(std::memory_order_acquire),
                std::memory_order_release);
        }
    }

    CopyOnWriteVector(CopyOnWriteVector&& other) noexcept
    {
        TransitionGuard guard { other.transition_lock_ };
        if (other.exposed_.load(std::memory_order_acquire)) {
            exposed_values_ = std::move(other.exposed_values_);
            exposed_.store(true, std::memory_order_release);
            other.storage_.store(nullptr, std::memory_order_release);
        } else {
            storage_.store(
                other.storage_.exchange(
                    nullptr, std::memory_order_acq_rel),
                std::memory_order_release);
        }
    }

    CopyOnWriteVector& operator=(const CopyOnWriteVector& other)
    {
        if (this == &other) {
            return *this;
        }

        auto replacement = snapshot_for_copy(other);
        TransitionGuard guard { transition_lock_ };
        if (exposed_.load(std::memory_order_acquire)) {
            const auto& source = values(replacement);
            auto copy = copy_with_capacity_floor(
                source, exposed_values_.capacity());
            exposed_values_ = std::move(copy);
            storage_.store(nullptr, std::memory_order_release);
        } else {
            storage_.store(
                std::move(replacement), std::memory_order_release);
        }
        return *this;
    }

    CopyOnWriteVector& operator=(CopyOnWriteVector&& other)
    {
        if (this == &other) {
            return *this;
        }

        TransitionGuard source_guard { other.transition_lock_ };
        TransitionGuard destination_guard { transition_lock_ };
        const bool destination_exposed
            = exposed_.load(std::memory_order_acquire);
        const bool source_exposed
            = other.exposed_.load(std::memory_order_acquire);

        if (!destination_exposed && !source_exposed) {
            storage_.store(
                other.storage_.exchange(
                    nullptr, std::memory_order_acq_rel),
                std::memory_order_release);
            return *this;
        }

        if (source_exposed) {
            exposed_values_ = std::move(other.exposed_values_);
            exposed_.store(true, std::memory_order_release);
            storage_.store(nullptr, std::memory_order_release);
            other.storage_.store(nullptr, std::memory_order_release);
            return *this;
        }

        auto source_storage
            = other.storage_.load(std::memory_order_acquire);
        if (source_storage == nullptr) {
            std::vector<T> empty;
            exposed_values_ = std::move(empty);
        } else if (source_storage.use_count() <= 2L) {
            exposed_values_ = std::move(source_storage->values);
        } else {
            // Prepare before either wrapper changes. Shared owners and read
            // views keep their original immutable values if copying throws.
            std::vector<T> replacement
                = copy_preserving_capacity(source_storage->values);
            exposed_values_ = std::move(replacement);
        }

        exposed_.store(true, std::memory_order_release);
        storage_.store(nullptr, std::memory_order_release);
        other.storage_.store(nullptr, std::memory_order_release);
        return *this;
    }

    CopyOnWriteVector& operator=(std::initializer_list<T> values)
    {
        assign(values);
        return *this;
    }

    CopyOnWriteVector& operator=(const std::vector<T>& values)
    {
        if (exposed_.load(std::memory_order_acquire)
            && std::addressof(values) == std::addressof(exposed_values_)) {
            return *this;
        }
        if (exposed_.load(std::memory_order_acquire)) {
            exposed_values_ = values;
            return *this;
        }
        assign(values.begin(), values.end());
        return *this;
    }

    CopyOnWriteVector& operator=(std::vector<T>&& values)
    {
        if (exposed_.load(std::memory_order_acquire)
            && std::addressof(values) == std::addressof(exposed_values_)) {
            return *this;
        }
        replace(std::move(values));
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_.empty();
        }
        TransitionGuard guard { transition_lock_ };
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_.empty();
        }
        const auto current = storage_.load(std::memory_order_acquire);
        return current == nullptr || current->values.empty();
    }

    [[nodiscard]] size_type size() const noexcept
    {
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_.size();
        }
        TransitionGuard guard { transition_lock_ };
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_.size();
        }
        const auto current = storage_.load(std::memory_order_acquire);
        return current == nullptr ? 0U : current->values.size();
    }

    [[nodiscard]] size_type capacity() const noexcept
    {
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_.capacity();
        }
        TransitionGuard guard { transition_lock_ };
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_.capacity();
        }
        const auto current = storage_.load(std::memory_order_acquire);
        return current == nullptr ? 0U : current->values.capacity();
    }

    [[nodiscard]] size_type max_size() const noexcept
    {
        return empty_values().max_size();
    }

    [[nodiscard]] bool references_escaped() const noexcept
    {
        return exposed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool shares_storage_with(
        const CopyOnWriteVector& other) const noexcept
    {
        if (exposed_.load(std::memory_order_acquire)
            || other.exposed_.load(std::memory_order_acquire)) {
            return false;
        }
        const auto current = storage_.load(std::memory_order_acquire);
        return current != nullptr
            && current == other.storage_.load(std::memory_order_acquire);
    }

    reference operator[](const size_type index)
    {
        return expose_storage()[index];
    }

    const_reference operator[](const size_type index) const
    {
        return expose_storage()[index];
    }

    reference at(const size_type index)
    {
        return expose_storage().at(index);
    }

    const_reference at(const size_type index) const
    {
        return expose_storage().at(index);
    }

    reference front() { return expose_storage().front(); }
    const_reference front() const { return expose_storage().front(); }
    reference back() { return expose_storage().back(); }
    const_reference back() const { return expose_storage().back(); }

    pointer data() { return expose_storage().data(); }
    const_pointer data() const { return expose_storage().data(); }

    iterator begin() { return expose_storage().begin(); }
    const_iterator begin() const { return expose_storage().begin(); }
    const_iterator cbegin() const { return expose_storage().cbegin(); }
    iterator end() { return expose_storage().end(); }
    const_iterator end() const { return expose_storage().end(); }
    const_iterator cend() const { return expose_storage().cend(); }
    reverse_iterator rbegin() { return expose_storage().rbegin(); }
    const_reverse_iterator rbegin() const
    {
        return expose_storage().rbegin();
    }
    const_reverse_iterator crbegin() const
    {
        return expose_storage().crbegin();
    }
    reverse_iterator rend() { return expose_storage().rend(); }
    const_reverse_iterator rend() const
    {
        return expose_storage().rend();
    }
    const_reverse_iterator crend() const
    {
        return expose_storage().crend();
    }

    void reserve(const size_type requested_capacity)
    {
        mutable_storage_for_value_operation().reserve(requested_capacity);
    }

    void shrink_to_fit()
    {
        mutable_storage_for_value_operation().shrink_to_fit();
    }

    void clear() { mutable_storage_for_value_operation().clear(); }

    void push_back(const T& value)
    {
        mutable_storage_for_value_operation().push_back(value);
    }

    void push_back(T&& value)
    {
        mutable_storage_for_value_operation().push_back(std::move(value));
    }

    template <typename... Args>
    reference emplace_back(Args&&... args)
    {
        return expose_storage().emplace_back(std::forward<Args>(args)...);
    }

    void pop_back() { mutable_storage_for_value_operation().pop_back(); }

    void resize(const size_type count)
    {
        mutable_storage_for_value_operation().resize(count);
    }

    void resize(const size_type count, const T& value)
    {
        mutable_storage_for_value_operation().resize(count, value);
    }

    void assign(const size_type count, const T& value)
    {
        if (exposed_.load(std::memory_order_acquire)) {
            exposed_values_.assign(count, value);
            return;
        }
        std::vector<T> replacement(count, value);
        replace(std::move(replacement));
    }

    void assign(std::initializer_list<T> values)
    {
        if (exposed_.load(std::memory_order_acquire)) {
            exposed_values_ = values;
            return;
        }
        std::vector<T> replacement(values);
        replace(std::move(replacement));
    }

    template <std::input_iterator InputIterator>
    void assign(InputIterator first, InputIterator last)
    {
        if (exposed_.load(std::memory_order_acquire)) {
            std::vector<T> replacement(first, last);
            exposed_values_.assign(
                replacement.begin(), replacement.end());
            return;
        }
        std::vector<T> replacement(first, last);
        replace(std::move(replacement));
    }

    iterator insert(const_iterator position, const T& value)
    {
        const auto index = position_index(position);
        auto& values = expose_storage();
        return values.insert(values.cbegin() + index, value);
    }

    iterator insert(const_iterator position, T&& value)
    {
        const auto index = position_index(position);
        auto& values = expose_storage();
        return values.insert(values.cbegin() + index, std::move(value));
    }

    iterator insert(
        const_iterator position, const size_type count, const T& value)
    {
        const auto index = position_index(position);
        auto& values = expose_storage();
        return values.insert(values.cbegin() + index, count, value);
    }

    template <std::input_iterator InputIterator>
    iterator insert(
        const_iterator position, InputIterator first, InputIterator last)
    {
        const auto index = position_index(position);
        std::vector<T> inserted(first, last);
        auto& values = expose_storage();
        return values.insert(
            values.cbegin() + index,
            std::make_move_iterator(inserted.begin()),
            std::make_move_iterator(inserted.end()));
    }

    iterator insert(
        const_iterator position, std::initializer_list<T> values)
    {
        return insert(position, values.begin(), values.end());
    }

    template <typename... Args>
    iterator emplace(const_iterator position, Args&&... args)
    {
        const auto index = position_index(position);
        auto& values = expose_storage();
        return values.emplace(
            values.cbegin() + index, std::forward<Args>(args)...);
    }

    iterator erase(const_iterator position)
    {
        const auto index = position_index(position);
        auto& values = expose_storage();
        return values.erase(values.cbegin() + index);
    }

    iterator erase(const_iterator first, const_iterator last)
    {
        const auto begin_index = position_index(first);
        const auto end_index = position_index(last);
        auto& values = expose_storage();
        return values.erase(
            values.cbegin() + begin_index, values.cbegin() + end_index);
    }

    void swap(CopyOnWriteVector& other)
    {
        if (this == &other) {
            return;
        }

        TransitionGuard left_guard { transition_lock_ };
        TransitionGuard right_guard { other.transition_lock_ };
        const bool left_exposed = exposed_.load(std::memory_order_acquire);
        const bool right_exposed
            = other.exposed_.load(std::memory_order_acquire);

        if (!left_exposed && !right_exposed) {
            auto left = storage_.exchange(nullptr, std::memory_order_acq_rel);
            auto right = other.storage_.exchange(
                std::move(left), std::memory_order_acq_rel);
            storage_.store(std::move(right), std::memory_order_release);
            return;
        }

        if (left_exposed && right_exposed) {
            exposed_values_.swap(other.exposed_values_);
            return;
        }

        if (left_exposed) {
            std::vector<T> replacement = copy_unexposed_values(other);
            other.exposed_values_ = std::move(exposed_values_);
            exposed_values_ = std::move(replacement);
            other.exposed_.store(true, std::memory_order_release);
            other.storage_.store(nullptr, std::memory_order_release);
            return;
        }

        std::vector<T> replacement = copy_unexposed_values(*this);
        exposed_values_ = std::move(other.exposed_values_);
        other.exposed_values_ = std::move(replacement);
        exposed_.store(true, std::memory_order_release);
        storage_.store(nullptr, std::memory_order_release);
        return;
    }

    [[nodiscard]] const std::vector<T>& vector() const
    {
        return expose_storage();
    }

    [[nodiscard]] std::vector<T>& vector()
    {
        return expose_storage();
    }

    operator const std::vector<T>&() const { return vector(); }
    operator std::vector<T>&() { return vector(); }

    operator std::span<const T>() const
    {
        const auto& values = expose_storage();
        return { values.data(), values.size() };
    }

    operator std::span<T>()
    {
        auto& values = expose_storage();
        return { values.data(), values.size() };
    }

    friend bool operator==(
        const CopyOnWriteVector& left,
        const CopyOnWriteVector& right)
    {
        const auto left_view = process_layout_detail::ProcessLayoutAccess::view(left);
        const auto right_view = process_layout_detail::ProcessLayoutAccess::view(right);
        return left_view.vector() == right_view.vector();
    }

    friend bool operator==(
        const CopyOnWriteVector& left, const std::vector<T>& right)
    {
        return process_layout_detail::ProcessLayoutAccess::view(left).vector() == right;
    }

    friend bool operator==(
        const std::vector<T>& left, const CopyOnWriteVector& right)
    {
        return left == process_layout_detail::ProcessLayoutAccess::view(right).vector();
    }

    friend void swap(CopyOnWriteVector& left, CopyOnWriteVector& right)
    {
        left.swap(right);
    }

private:
    friend struct process_layout_detail::ProcessLayoutAccess;

    struct Control {
        std::vector<T> values;

        explicit Control(std::vector<T>&& initial_values)
            : values(std::move(initial_values))
        {
        }

        explicit Control(const std::vector<T>& source_values)
            : values(source_values)
        {
        }
    };

    class TransitionGuard {
    public:
        explicit TransitionGuard(std::atomic_flag& lock) noexcept
            : lock_(lock)
        {
            while (lock_.test_and_set(std::memory_order_acquire)) {
            }
        }

        TransitionGuard(const TransitionGuard&) = delete;
        TransitionGuard& operator=(const TransitionGuard&) = delete;

        ~TransitionGuard()
        {
            lock_.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag& lock_;
    };

    mutable std::atomic<std::shared_ptr<Control>> storage_ { };
    mutable std::vector<T> exposed_values_;
    mutable std::atomic<bool> exposed_ { false };
    mutable std::atomic_flag transition_lock_ = ATOMIC_FLAG_INIT;

    [[nodiscard]] static const std::vector<T>& empty_values() noexcept
    {
        static const std::vector<T> empty;
        return empty;
    }

    [[nodiscard]] static const std::vector<T>& values(
        const std::shared_ptr<Control>& control) noexcept
    {
        return control == nullptr ? empty_values() : control->values;
    }

    [[nodiscard]] std::vector<T>& mutable_storage_for_value_operation()
    {
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_;
        }

        for (;;) {
            if (exposed_.load(std::memory_order_acquire)) {
                return exposed_values_;
            }

            auto current = storage_.load(std::memory_order_acquire);
            if (current == nullptr) {
                auto replacement
                    = std::make_shared<Control>(std::vector<T> { });
                std::shared_ptr<Control> expected;
                if (storage_.compare_exchange_weak(
                        expected, replacement,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return replacement->values;
                }
                continue;
            }

            if (current.use_count() <= 2L) {
                return current->values;
            }

            auto replacement = std::make_shared<Control>(
                copy_preserving_capacity(current->values));
            if (storage_.compare_exchange_weak(
                    current, replacement,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return replacement->values;
            }
        }
    }

    [[nodiscard]] std::vector<T>& expose_storage() const
    {
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_;
        }

        TransitionGuard guard { transition_lock_ };
        if (exposed_.load(std::memory_order_acquire)) {
            return exposed_values_;
        }

        const auto current = storage_.load(std::memory_order_acquire);
        std::vector<T> replacement = current == nullptr
            ? std::vector<T> { }
            : copy_preserving_capacity(current->values);
        exposed_values_.swap(replacement);
        exposed_.store(true, std::memory_order_release);
        storage_.store(nullptr, std::memory_order_release);
        return exposed_values_;
    }

    [[nodiscard]] std::vector<T>& expose_mutable_storage()
    {
        return expose_storage();
    }

    void replace(std::vector<T>&& replacement)
    {
        TransitionGuard guard { transition_lock_ };
        if (exposed_.load(std::memory_order_acquire)) {
            exposed_values_ = std::move(replacement);
            storage_.store(nullptr, std::memory_order_release);
            return;
        }

        auto next = std::make_shared<Control>(std::move(replacement));
        storage_.store(std::move(next), std::memory_order_release);
    }

    [[nodiscard]] static std::shared_ptr<Control> snapshot_for_copy(
        const CopyOnWriteVector& source)
    {
        TransitionGuard guard { source.transition_lock_ };
        if (source.exposed_.load(std::memory_order_acquire)) {
            return std::make_shared<Control>(source.exposed_values_);
        }
        return source.storage_.load(std::memory_order_acquire);
    }

    [[nodiscard]] static std::vector<T> copy_unexposed_values(
        const CopyOnWriteVector& source)
    {
        const auto current = source.storage_.load(std::memory_order_acquire);
        return copy_preserving_capacity(values(current));
    }

    [[nodiscard]] static std::vector<T> copy_preserving_capacity(
        const std::vector<T>& source)
    {
        return copy_with_capacity_floor(source, 0U);
    }

    [[nodiscard]] static std::vector<T> copy_with_capacity_floor(
        const std::vector<T>& source,
        const size_type capacity_floor)
    {
        std::vector<T> result;
        result.reserve(std::max(source.capacity(), capacity_floor));
        result.insert(result.end(), source.begin(), source.end());
        return result;
    }

    [[nodiscard]] difference_type position_index(
        const const_iterator position) const
    {
        if (exposed_.load(std::memory_order_acquire)) {
            return position - exposed_values_.cbegin();
        }
        const auto current = storage_.load(std::memory_order_acquire);
        const auto& current_values = values(current);
        return position - current_values.cbegin();
    }
};

template <typename T>
process_layout_detail::ProcessLayoutReadView<T> process_layout_detail::ProcessLayoutAccess::view(
    const CopyOnWriteVector<T>& values) noexcept
{
    if (values.exposed_.load(std::memory_order_acquire)) {
        return ProcessLayoutReadView<T> { values.exposed_values_ };
    }

    typename CopyOnWriteVector<T>::TransitionGuard guard {
        values.transition_lock_
    };
    if (values.exposed_.load(std::memory_order_acquire)) {
        return ProcessLayoutReadView<T> { values.exposed_values_ };
    }
    auto current = values.storage_.load(std::memory_order_acquire);
    if (current == nullptr) {
        return ProcessLayoutReadView<T> { nullptr };
    }
    std::shared_ptr<const std::vector<T>> snapshot(current, &current->values);
    return ProcessLayoutReadView<T> { std::move(snapshot) };
}

template <typename T>
bool process_layout_detail::ProcessLayoutAccess::can_share(
    const CopyOnWriteVector<T>& left,
    const CopyOnWriteVector<T>& right) noexcept
{
    return !left.exposed_.load(std::memory_order_acquire)
        && !right.exposed_.load(std::memory_order_acquire);
}

template <typename T>
void process_layout_detail::ProcessLayoutAccess::share(
    CopyOnWriteVector<T>& destination,
    const CopyOnWriteVector<T>& representative) noexcept
{
    if (&destination == &representative) {
        return;
    }

    const auto* first = &destination;
    const auto* second = &representative;
    if (std::less<const CopyOnWriteVector<T>*> { }(second, first)) {
        std::swap(first, second);
    }
    typename CopyOnWriteVector<T>::TransitionGuard first_guard {
        first->transition_lock_
    };
    typename CopyOnWriteVector<T>::TransitionGuard second_guard {
        second->transition_lock_
    };
    if (destination.exposed_.load(std::memory_order_acquire)
        || representative.exposed_.load(std::memory_order_acquire)) {
        return;
    }

    auto source
        = representative.storage_.load(std::memory_order_acquire);
    auto expected = destination.storage_.load(std::memory_order_acquire);
    if (destination.exposed_.load(std::memory_order_acquire)
        || representative.exposed_.load(std::memory_order_acquire)) {
        return;
    }
    destination.storage_.compare_exchange_strong(
        expected, std::move(source),
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

template <typename T>
std::vector<T>& process_layout_detail::ProcessLayoutAccess::archive_target(
    CopyOnWriteVector<T>& values)
{
    return values.mutable_storage_for_value_operation();
}

template <typename T>
auto archive_fields(CopyOnWriteVector<T>& values)
{
    return std::tie(process_layout_detail::ProcessLayoutAccess::archive_target(values));
}

template <typename T>
auto archive_fields(const CopyOnWriteVector<T>& values)
{
    return std::tuple { process_layout_detail::ProcessLayoutAccess::view(values) };
}

} // namespace fsim::runtime::simir
