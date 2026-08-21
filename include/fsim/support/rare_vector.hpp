// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace fsim::support {

/// Optional semantics with one pointer of dormant storage. This is intended
/// for large values whose absence is overwhelmingly more common than presence.
template <typename T>
class RareOptional {
public:
    using value_type = T;

    RareOptional() = default;
    RareOptional(std::nullopt_t) noexcept { }
    RareOptional(const T& value) : value_(std::make_unique<T>(value)) { }
    RareOptional(T&& value)
        : value_(std::make_unique<T>(std::move(value)))
    {
    }
    RareOptional(const std::optional<T>& value)
    {
        if (value) {
            value_ = std::make_unique<T>(*value);
        }
    }
    RareOptional(std::optional<T>&& value)
    {
        if (value) {
            value_ = std::make_unique<T>(std::move(*value));
        }
    }
    RareOptional(const RareOptional& other)
    {
        if (other.value_) {
            value_ = std::make_unique<T>(*other.value_);
        }
    }
    RareOptional(RareOptional&&) noexcept = default;
    RareOptional& operator=(const RareOptional& other)
    {
        if (this != &other) {
            value_ = other.value_
                ? std::make_unique<T>(*other.value_)
                : nullptr;
        }
        return *this;
    }
    RareOptional& operator=(RareOptional&&) noexcept = default;
    RareOptional& operator=(std::nullopt_t) noexcept
    {
        reset();
        return *this;
    }
    RareOptional& operator=(const T& value)
    {
        if (value_) {
            *value_ = value;
        } else {
            value_ = std::make_unique<T>(value);
        }
        return *this;
    }
    RareOptional& operator=(T&& value)
    {
        if (value_) {
            *value_ = std::move(value);
        } else {
            value_ = std::make_unique<T>(std::move(value));
        }
        return *this;
    }
    RareOptional& operator=(const std::optional<T>& value)
    {
        if (value) {
            *this = *value;
        } else {
            reset();
        }
        return *this;
    }
    RareOptional& operator=(std::optional<T>&& value)
    {
        if (value) {
            *this = std::move(*value);
        } else {
            reset();
        }
        return *this;
    }

    [[nodiscard]] bool has_value() const noexcept
    {
        return static_cast<bool>(value_);
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }
    [[nodiscard]] const T& operator*() const noexcept { return *value_; }
    [[nodiscard]] T& operator*() noexcept { return *value_; }
    [[nodiscard]] const T* operator->() const noexcept { return value_.get(); }
    [[nodiscard]] T* operator->() noexcept { return value_.get(); }
    [[nodiscard]] const T& value() const
    {
        if (!value_) {
            throw std::bad_optional_access { };
        }
        return *value_;
    }
    [[nodiscard]] T& value()
    {
        if (!value_) {
            throw std::bad_optional_access { };
        }
        return *value_;
    }
    template <typename U>
    [[nodiscard]] T value_or(U&& fallback) const
    {
        return value_ ? *value_ : static_cast<T>(std::forward<U>(fallback));
    }
    [[nodiscard]] operator std::optional<T>() const
    {
        return value_ ? std::optional<T> { *value_ } : std::nullopt;
    }
    void reset() noexcept { value_.reset(); }
    template <typename... Arguments>
    T& emplace(Arguments&&... arguments)
    {
        value_ = std::make_unique<T>(
            std::forward<Arguments>(arguments)...);
        return *value_;
    }

    friend bool operator==(
        const RareOptional& left, const RareOptional& right)
    {
        return left.has_value() == right.has_value()
            && (!left || *left == *right);
    }
    friend bool operator==(const RareOptional& value, std::nullopt_t) noexcept
    {
        return !value;
    }
    friend bool operator==(std::nullopt_t, const RareOptional& value) noexcept
    {
        return !value;
    }

private:
    std::unique_ptr<T> value_;
};

/// Vector semantics with one pointer of dormant storage. Use this only for
/// fields which are empty on the overwhelming majority of their owning nodes.
template <typename T>
class RareVector {
public:
    using value_type = T;
    using Storage = std::vector<T>;
    using size_type = typename Storage::size_type;
    using iterator = typename Storage::iterator;
    using const_iterator = typename Storage::const_iterator;

    RareVector() = default;
    RareVector(std::initializer_list<T> values)
    {
        if (!values.empty()) {
            values_ = std::make_unique<Storage>(values);
        }
    }
    RareVector(Storage values)
    {
        if (!values.empty()) {
            values_ = std::make_unique<Storage>(std::move(values));
        }
    }
    RareVector(const RareVector& other)
    {
        if (other.values_) {
            values_ = std::make_unique<Storage>(*other.values_);
        }
    }
    RareVector(RareVector&&) noexcept = default;
    RareVector& operator=(const RareVector& other)
    {
        if (this != &other) {
            values_ = other.values_
                ? std::make_unique<Storage>(*other.values_)
                : nullptr;
        }
        return *this;
    }
    RareVector& operator=(RareVector&&) noexcept = default;
    RareVector& operator=(Storage values)
    {
        if (values.empty()) {
            values_.reset();
        } else {
            values_ = std::make_unique<Storage>(std::move(values));
        }
        return *this;
    }
    RareVector& operator=(std::initializer_list<T> values)
    {
        if (values.size() == 0U) {
            values_.reset();
        } else {
            values_ = std::make_unique<Storage>(values);
        }
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return !values_ || values_->empty();
    }
    [[nodiscard]] size_type size() const noexcept
    {
        return values_ ? values_->size() : 0U;
    }
    [[nodiscard]] size_type max_size() const noexcept
    {
        return empty_storage().max_size();
    }
    [[nodiscard]] size_type capacity() const noexcept
    {
        return values_ ? values_->capacity() : 0U;
    }
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return storage().begin();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return storage().end();
    }
    [[nodiscard]] iterator begin() noexcept
    {
        return values_ ? values_->begin() : iterator { };
    }
    [[nodiscard]] iterator end() noexcept
    {
        return values_ ? values_->end() : iterator { };
    }
    [[nodiscard]] const T& operator[](const size_type index) const noexcept
    {
        return (*values_)[index];
    }
    [[nodiscard]] T& operator[](const size_type index) noexcept
    {
        return (*values_)[index];
    }
    [[nodiscard]] const T& at(const size_type index) const
    {
        return storage().at(index);
    }
    [[nodiscard]] T& at(const size_type index)
    {
        return mutable_storage().at(index);
    }
    [[nodiscard]] const T& front() const { return storage().front(); }
    [[nodiscard]] T& front() { return mutable_storage().front(); }
    [[nodiscard]] const T& back() const { return storage().back(); }
    [[nodiscard]] T& back() { return mutable_storage().back(); }
    [[nodiscard]] const T* data() const noexcept { return storage().data(); }
    [[nodiscard]] T* data() noexcept
    {
        return values_ ? values_->data() : nullptr;
    }

    void clear() noexcept { values_.reset(); }
    void reserve(const size_type capacity)
    {
        if (capacity != 0U) {
            mutable_storage().reserve(capacity);
        }
    }
    void resize(const size_type size)
    {
        if (size == 0U) {
            clear();
        } else {
            mutable_storage().resize(size);
        }
    }
    void resize(const size_type size, const T& value)
    {
        if (size == 0U) {
            clear();
        } else {
            mutable_storage().resize(size, value);
        }
    }
    template <typename... Arguments>
    T& emplace_back(Arguments&&... arguments)
    {
        return mutable_storage().emplace_back(
            std::forward<Arguments>(arguments)...);
    }
    void push_back(const T& value) { mutable_storage().push_back(value); }
    void push_back(T&& value) { mutable_storage().push_back(std::move(value)); }
    void pop_back()
    {
        values_->pop_back();
        release_if_empty();
    }
    iterator erase(const_iterator position)
    {
        return mutable_storage().erase(position);
    }
    iterator erase(const_iterator first, const_iterator last)
    {
        return mutable_storage().erase(first, last);
    }
    template <typename Iterator>
    iterator insert(const_iterator position, Iterator first, Iterator last)
    {
        return mutable_storage().insert(position, first, last);
    }
    iterator insert(const_iterator position, const T& value)
    {
        return mutable_storage().insert(position, value);
    }
    iterator insert(const_iterator position, T&& value)
    {
        return mutable_storage().insert(position, std::move(value));
    }
    template <typename Iterator>
    void assign(Iterator first, Iterator last)
    {
        auto values = Storage(first, last);
        *this = std::move(values);
    }
    void assign(const size_type count, const T& value)
    {
        if (count == 0U) {
            clear();
        } else {
            mutable_storage().assign(count, value);
        }
    }
    void swap(RareVector& other) noexcept { values_.swap(other.values_); }

    [[nodiscard]] const Storage& storage() const noexcept
    {
        return values_ ? *values_ : empty_storage();
    }
    [[nodiscard]] Storage& mutable_storage()
    {
        if (!values_) {
            values_ = std::make_unique<Storage>();
        }
        return *values_;
    }
    operator const Storage&() const noexcept { return storage(); }
    operator Storage&() { return mutable_storage(); }

    friend bool operator==(const RareVector& left, const RareVector& right)
    {
        return left.storage() == right.storage();
    }
    friend bool operator==(const RareVector& left, const Storage& right)
    {
        return left.storage() == right;
    }
    friend bool operator==(const Storage& left, const RareVector& right)
    {
        return left == right.storage();
    }

private:
    [[nodiscard]] static const Storage& empty_storage() noexcept
    {
        static const Storage empty;
        return empty;
    }
    void release_if_empty() noexcept
    {
        if (values_ && values_->empty()) {
            values_.reset();
        }
    }

    std::unique_ptr<Storage> values_;
};

} // namespace fsim::support
