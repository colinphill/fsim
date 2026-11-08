// SPDX-License-Identifier: Apache-2.0
#pragma once

// One archive for both directions of a run-state section (#128; IEEE 1800-2023
// D.9 $save/$restart, IEEE 1076-2019 20.7-20.9). A section is written and read
// by the same visitor function, `io(archive, state)`, so a field cannot be
// saved without being restored and the byte layout cannot drift between the
// two. The archive is used only at a save point or a restart: nothing on a
// scheduling or kernel path touches it.
//
// Layout: fixed-width little-endian fields (a field of sizeof(T) bytes), a
// u64 count before every sequence, and one byte before every optional or
// pointer. Unordered containers are written in the order of their encoded
// entries, so the same state always gives the same bytes (the hash the
// idempotence tests compare). Aggregates are walked by boost::pfr; any other
// type is described by a `RunStateCodec` specialization.
//
// Saving never writes to the state: the visitor receives non-const references
// so one function serves both directions, and the saving archive only reads
// them (a caller with a const object casts the const away for the call).

#include "fsim/runtime/packed_value.hpp"

#include <boost/pfr.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::runtime {

static_assert(std::endian::native == std::endian::little,
    "run-state sections are little-endian and copied as they are");

/// A hand-written description of a type the generic walker cannot handle.
/// A specialization declares `using is_codec = void;` and
/// `static void io(RunStateArchive&, T&)`.
template <typename T, typename = void> struct RunStateCodec { };

class RunStateArchive final {
public:
    [[nodiscard]] static RunStateArchive writer(std::string& out) noexcept
    {
        return RunStateArchive { &out, std::string_view { } };
    }
    [[nodiscard]] static RunStateArchive reader(std::string_view in) noexcept
    {
        return RunStateArchive { nullptr, in };
    }

    [[nodiscard]] bool saving() const noexcept { return out_ != nullptr; }
    [[nodiscard]] bool loading() const noexcept { return out_ == nullptr; }
    [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    /// A reader that consumed every byte of a good section.
    [[nodiscard]] bool finished() const noexcept
    {
        return ok() && (saving() || in_.empty());
    }
    /// Bytes still unread (a reader).
    [[nodiscard]] std::size_t remaining() const noexcept { return in_.size(); }

    /// Marks the archive failed; the first message is kept.
    void fail(std::string message);

    /// A sequence length: written when saving, read and checked against the
    /// bytes left when loading (every element takes at least one byte).
    [[nodiscard]] std::uint64_t count(std::uint64_t size, bool bounded = true);

    template <typename T> void io(T& value);

    /// A vector whose size the artifact fixes (a per-signal table): the saved
    /// size must equal the current one, and `visit(element)` handles each.
    template <typename Vector, typename Visit>
    void sized(Vector& values, Visit&& visit)
    {
        // The size is the design's own, so the byte bound does not apply: an
        // element may write nothing.
        const auto size = count(values.size(), false);
        if (loading() && ok() && size != values.size()) {
            fail("table size differs from the design");
            return;
        }
        for (auto& element : values) {
            if (!ok()) {
                return;
            }
            visit(element);
        }
    }

    void bytes(const void* data, std::size_t size);
    void read_bytes(void* data, std::size_t size);
    void packed(PackedLogic4& value);

private:
    RunStateArchive(std::string* out, std::string_view in) noexcept
        : out_ { out }
        , in_ { in }
    {
    }

    template <typename T> void integral(T& value);
    template <typename Sequence> void sequence(Sequence& values);
    template <typename Map> void map(Map& values);
    template <typename Set> void set(Set& values);
    template <typename Unordered> void unordered_entries(Unordered& values);
    template <typename Pointer> void pointer(Pointer& value);

    std::string* out_ { };
    std::string_view in_;
    std::string error_;
};

namespace run_state_detail {

template <typename T> struct is_vector : std::false_type { };
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type { };
template <typename T> struct is_deque : std::false_type { };
template <typename T, typename A>
struct is_deque<std::deque<T, A>> : std::true_type { };
template <typename T> struct is_array : std::false_type { };
template <typename T, std::size_t N>
struct is_array<std::array<T, N>> : std::true_type { };
template <typename T> struct is_optional : std::false_type { };
template <typename T>
struct is_optional<std::optional<T>> : std::true_type { };
template <typename T> struct is_pair : std::false_type { };
template <typename A, typename B>
struct is_pair<std::pair<A, B>> : std::true_type { };
template <typename T> struct is_tuple : std::false_type { };
template <typename... Elements>
struct is_tuple<std::tuple<Elements...>> : std::true_type { };
template <typename T> struct is_ordered_map : std::false_type { };
template <typename K, typename V, typename C, typename A>
struct is_ordered_map<std::map<K, V, C, A>> : std::true_type { };
template <typename T> struct is_ordered_set : std::false_type { };
template <typename K, typename C, typename A>
struct is_ordered_set<std::set<K, C, A>> : std::true_type { };
template <typename T> struct is_unordered_map : std::false_type { };
template <typename K, typename V, typename H, typename E, typename A>
struct is_unordered_map<std::unordered_map<K, V, H, E, A>> : std::true_type { };
template <typename T> struct is_unordered_set : std::false_type { };
template <typename K, typename H, typename E, typename A>
struct is_unordered_set<std::unordered_set<K, H, E, A>> : std::true_type { };
template <typename T> struct is_unique_ptr : std::false_type { };
template <typename T, typename D>
struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type { };
template <typename T> struct is_shared_ptr : std::false_type { };
template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type { };

template <typename T>
concept HasCodec = requires { typename RunStateCodec<T>::is_codec; };

template <typename T> [[nodiscard]] T& mutable_ref(const T& value) noexcept
{
    return const_cast<T&>(value);
}

} // namespace run_state_detail

template <typename T> void RunStateArchive::integral(T& value)
{
    if (saving()) {
        bytes(&value, sizeof(T));
    } else {
        read_bytes(&value, sizeof(T));
    }
}

template <typename Sequence> void RunStateArchive::sequence(Sequence& values)
{
    using Element = typename Sequence::value_type;
    // Contiguous integers are copied as one block.
    constexpr bool block = std::is_integral_v<Element>
        && !std::is_same_v<Element, bool> && requires { values.data(); };
    const auto size = count(values.size());
    if (saving()) {
        if constexpr (block) {
            bytes(values.data(), values.size() * sizeof(Element));
        } else {
            for (auto&& element : values) {
                if constexpr (std::is_same_v<Element, bool>) {
                    bool flag = element;
                    io(flag);
                } else {
                    io(run_state_detail::mutable_ref(element));
                }
            }
        }
        return;
    }
    if (!ok()) {
        return;
    }
    values.clear();
    values.resize(static_cast<std::size_t>(size));
    if constexpr (block) {
        read_bytes(values.data(), values.size() * sizeof(Element));
    } else {
        for (std::size_t index = 0U; index < values.size() && ok(); ++index) {
            if constexpr (std::is_same_v<Element, bool>) {
                bool flag { };
                io(flag);
                values[index] = flag;
            } else {
                io(values[index]);
            }
        }
    }
}

template <typename Map> void RunStateArchive::map(Map& values)
{
    using Key = typename Map::key_type;
    using Mapped = typename Map::mapped_type;
    const auto size = count(values.size());
    if (saving()) {
        for (auto& entry : values) {
            io(run_state_detail::mutable_ref(entry.first));
            io(entry.second);
        }
        return;
    }
    values.clear();
    for (std::uint64_t index = 0U; index < size && ok(); ++index) {
        Key key = Key();
        Mapped mapped = Mapped();
        io(key);
        io(mapped);
        if (ok() && !values.emplace(std::move(key), std::move(mapped)).second) {
            fail("duplicate map key");
        }
    }
}

template <typename Set> void RunStateArchive::set(Set& values)
{
    using Key = typename Set::key_type;
    const auto size = count(values.size());
    if (saving()) {
        for (auto& key : values) {
            io(run_state_detail::mutable_ref(key));
        }
        return;
    }
    values.clear();
    for (std::uint64_t index = 0U; index < size && ok(); ++index) {
        Key key = Key();
        io(key);
        if (ok() && !values.insert(std::move(key)).second) {
            fail("duplicate set key");
        }
    }
}

// Unordered containers are written entry by entry into scratch strings that are
// then sorted, so the bytes do not depend on bucket order.
template <typename Unordered> void RunStateArchive::unordered_entries(
    Unordered& values)
{
    constexpr bool is_map = run_state_detail::is_unordered_map<Unordered>::value;
    using Key = typename Unordered::key_type;
    if (saving()) {
        std::vector<std::string> encoded;
        encoded.reserve(values.size());
        for (auto& entry : values) {
            std::string blob;
            auto sub = RunStateArchive::writer(blob);
            if constexpr (is_map) {
                sub.io(run_state_detail::mutable_ref(entry.first));
                sub.io(entry.second);
            } else {
                sub.io(run_state_detail::mutable_ref(entry));
            }
            if (!sub.ok()) {
                fail(sub.error());
                return;
            }
            encoded.push_back(std::move(blob));
        }
        std::ranges::sort(encoded);
        static_cast<void>(count(encoded.size()));
        for (const auto& blob : encoded) {
            bytes(blob.data(), blob.size());
        }
        return;
    }
    const auto size = count(0U);
    values.clear();
    for (std::uint64_t index = 0U; index < size && ok(); ++index) {
        Key key = Key();
        io(key);
        if constexpr (is_map) {
            auto mapped = typename Unordered::mapped_type();
            io(mapped);
            if (ok() && !values.emplace(std::move(key), std::move(mapped)).second) {
                fail("duplicate map key");
            }
        } else if (ok() && !values.insert(std::move(key)).second) {
            fail("duplicate set key");
        }
    }
}

template <typename Pointer> void RunStateArchive::pointer(Pointer& value)
{
    using Element = typename Pointer::element_type;
    std::uint8_t present = value != nullptr ? 1U : 0U;
    io(present);
    if (!ok()) {
        return;
    }
    if (present > 1U) {
        fail("bad pointer flag");
        return;
    }
    if (loading()) {
        if (present == 0U) {
            value.reset();
            return;
        }
        if constexpr (run_state_detail::is_unique_ptr<Pointer>::value) {
            value = std::make_unique<Element>();
        } else {
            value = std::make_shared<Element>();
        }
    }
    if (present != 0U) {
        io(*value);
    }
}

template <typename T> void RunStateArchive::io(T& value)
{
    using namespace run_state_detail;
    if (!ok()) {
        return;
    }
    if constexpr (HasCodec<T>) {
        RunStateCodec<T>::io(*this, value);
    } else if constexpr (requires { value.run_state_io(*this); }) {
        // A class that describes itself (a private pool or token).
        value.run_state_io(*this);
    } else if constexpr (std::is_same_v<T, bool>) {
        std::uint8_t flag = value ? 1U : 0U;
        integral(flag);
        if (loading()) {
            if (flag > 1U) {
                fail("bad boolean");
            } else {
                value = flag != 0U;
            }
        }
    } else if constexpr (std::is_enum_v<T>) {
        auto underlying = static_cast<std::underlying_type_t<T>>(value);
        integral(underlying);
        value = static_cast<T>(underlying);
    } else if constexpr (std::is_integral_v<T>) {
        integral(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        const auto size = count(value.size());
        if (saving()) {
            bytes(value.data(), value.size());
        } else if (ok()) {
            value.resize(static_cast<std::size_t>(size));
            read_bytes(value.data(), value.size());
        }
    } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
        std::string text = value.string();
        io(text);
        if (loading()) {
            value = std::filesystem::path { text };
        }
    } else if constexpr (std::is_same_v<T, PackedLogic4>) {
        packed(value);
    } else if constexpr (is_vector<T>::value || is_deque<T>::value) {
        sequence(value);
    } else if constexpr (is_array<T>::value) {
        for (auto& element : value) {
            io(element);
        }
    } else if constexpr (is_optional<T>::value) {
        std::uint8_t present = value.has_value() ? 1U : 0U;
        io(present);
        if (!ok()) {
            return;
        }
        if (present > 1U) {
            fail("bad optional flag");
        } else if (loading()) {
            if (present == 0U) {
                value.reset();
            } else {
                value.emplace();
                io(*value);
            }
        } else if (present != 0U) {
            io(*value);
        }
    } else if constexpr (is_pair<T>::value) {
        io(mutable_ref(value.first));
        io(value.second);
    } else if constexpr (is_tuple<T>::value) {
        std::apply([&](auto&... element) { (io(element), ...); }, value);
    } else if constexpr (is_ordered_map<T>::value) {
        map(value);
    } else if constexpr (is_ordered_set<T>::value) {
        set(value);
    } else if constexpr (is_unordered_map<T>::value || is_unordered_set<T>::value) {
        unordered_entries(value);
    } else if constexpr (is_unique_ptr<T>::value || is_shared_ptr<T>::value) {
        pointer(value);
    } else {
        static_assert(std::is_aggregate_v<T>,
            "run-state field type needs a RunStateCodec");
        boost::pfr::for_each_field(value, [&](auto& field) { io(field); });
    }
}

} // namespace fsim::runtime
