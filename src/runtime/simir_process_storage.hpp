// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "simir_slab_page_advice.hpp"

#include <cstddef>
#include <deque>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

// Elaborated processes occupy one reserved contiguous slab. Later processes
// (including fork children) use address-stable overflow storage. A published
// record is never moved when another process is registered.
template <typename T>
class ProcessStateStorage {
public:
    template <bool IsConst>
    class Iterator {
        using Storage = std::conditional_t<IsConst,
            const ProcessStateStorage, ProcessStateStorage>;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<IsConst, const T&, T&>;
        using pointer = std::conditional_t<IsConst, const T*, T*>;

        Iterator() = default;
        Iterator(Storage* storage, const std::size_t index)
            : storage_(storage)
            , index_(index)
        {
        }

        reference operator*() const { return (*storage_)[index_]; }
        pointer operator->() const { return std::addressof(**this); }
        Iterator& operator++()
        {
            ++index_;
            return *this;
        }
        Iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }
        friend bool operator==(const Iterator&, const Iterator&) = default;

    private:
        Storage* storage_ { };
        std::size_t index_ { };
    };

    void reserve_initial(const std::size_t capacity)
    {
        if (!empty()) {
            throw std::logic_error {
                "cannot reserve process storage after registration"
            };
        }
        initial_.reserve(capacity);
        advise_slab_large_pages(initial_.data(), initial_.capacity() * sizeof(T));
    }

    void adopt_initial(std::vector<T>&& initial)
    {
        if (!empty()) {
            throw std::logic_error {
                "cannot adopt initial process storage after registration"
            };
        }
        initial_ = std::move(initial);
        advise_slab_large_pages(initial_.data(), initial_.capacity() * sizeof(T));
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return initial_.size() + overflow_.size();
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0U; }

    T& operator[](const std::size_t index) noexcept
    {
        return index < initial_.size() ? initial_[index]
                                      : overflow_[index - initial_.size()];
    }
    const T& operator[](const std::size_t index) const noexcept
    {
        return index < initial_.size() ? initial_[index]
                                      : overflow_[index - initial_.size()];
    }
    T& at(const std::size_t index)
    {
        if (index >= size()) {
            throw std::out_of_range { "process storage index" };
        }
        return (*this)[index];
    }
    const T& at(const std::size_t index) const
    {
        if (index >= size()) {
            throw std::out_of_range { "process storage index" };
        }
        return (*this)[index];
    }

    void push_back(T&& value)
    {
        if (initial_.size() < initial_.capacity()) {
            initial_.push_back(std::move(value));
        } else {
            overflow_.push_back(std::move(value));
        }
    }

    void resize(const std::size_t requested)
    {
        const auto original_size = size();
        try {
            while (size() < requested) {
                push_back(T { });
            }
        } catch (...) {
            shrink_to(original_size);
            throw;
        }
        shrink_to(requested);
    }

    Iterator<false> begin() noexcept { return { this, 0U }; }
    Iterator<false> end() noexcept { return { this, size() }; }
    Iterator<true> begin() const noexcept { return { this, 0U }; }
    Iterator<true> end() const noexcept { return { this, size() }; }

private:
    void shrink_to(const std::size_t requested) noexcept
    {
        while (size() > requested) {
            if (!overflow_.empty()) {
                overflow_.pop_back();
            } else {
                initial_.pop_back();
            }
        }
    }

    std::vector<T> initial_;
    std::deque<T> overflow_;
};

} // namespace fsim::runtime::simir
