// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace fsim::support {

// The built-in methods IEEE 1800-2023 defines on values that are not class
// objects: string methods (6.16), enumeration methods (6.19.5), array,
// associative array and queue methods (7.5.1, 7.9, 7.10.2) and array
// manipulation methods (7.12). A method call `receiver.name(...)` on such a
// value is resolved once against these member lists; every later reader
// takes the method from the call, never from its name. One name may be a
// member of several lists (`first` of an enumeration and of an associative
// array): the receiver's type tells which one applies.
enum class BuiltinMethod : std::uint8_t {
    none,
    // String methods (6.16).
    len,
    putc,
    getc,
    toupper,
    tolower,
    compare,
    icompare,
    substr,
    atoi,
    atohex,
    atooct,
    atobin,
    atoreal,
    itoa,
    hextoa,
    octtoa,
    bintoa,
    realtoa,
    // Enumeration methods (6.19.5) and associative array traversal (7.9).
    first,
    last,
    next,
    prev,
    num,
    name,
    // Array, associative array and queue methods (7.5.1, 7.9, 7.10.2).
    size,
    delete_entries,
    exists,
    insert,
    pop_front,
    pop_back,
    push_front,
    push_back,
    // Array locator methods (7.12.1).
    find,
    find_index,
    find_first,
    find_first_index,
    find_last,
    find_last_index,
    min,
    max,
    unique,
    unique_index,
    // Array ordering methods (7.12.2).
    reverse,
    sort,
    rsort,
    shuffle,
    // Array reduction methods (7.12.3).
    sum,
    product,
    reduce_and,
    reduce_or,
    reduce_xor,
    // Array mapping method (7.12.5).
    map,
};

namespace detail {

inline constexpr std::array<std::pair<std::string_view, BuiltinMethod>, 52>
    kBuiltinMethodNames { {
        { "and", BuiltinMethod::reduce_and },
        { "atobin", BuiltinMethod::atobin },
        { "atohex", BuiltinMethod::atohex },
        { "atoi", BuiltinMethod::atoi },
        { "atooct", BuiltinMethod::atooct },
        { "atoreal", BuiltinMethod::atoreal },
        { "bintoa", BuiltinMethod::bintoa },
        { "compare", BuiltinMethod::compare },
        { "delete", BuiltinMethod::delete_entries },
        { "exists", BuiltinMethod::exists },
        { "find", BuiltinMethod::find },
        { "find_first", BuiltinMethod::find_first },
        { "find_first_index", BuiltinMethod::find_first_index },
        { "find_index", BuiltinMethod::find_index },
        { "find_last", BuiltinMethod::find_last },
        { "find_last_index", BuiltinMethod::find_last_index },
        { "first", BuiltinMethod::first },
        { "getc", BuiltinMethod::getc },
        { "hextoa", BuiltinMethod::hextoa },
        { "icompare", BuiltinMethod::icompare },
        { "insert", BuiltinMethod::insert },
        { "itoa", BuiltinMethod::itoa },
        { "last", BuiltinMethod::last },
        { "len", BuiltinMethod::len },
        { "map", BuiltinMethod::map },
        { "max", BuiltinMethod::max },
        { "min", BuiltinMethod::min },
        { "name", BuiltinMethod::name },
        { "next", BuiltinMethod::next },
        { "num", BuiltinMethod::num },
        { "octtoa", BuiltinMethod::octtoa },
        { "or", BuiltinMethod::reduce_or },
        { "pop_back", BuiltinMethod::pop_back },
        { "pop_front", BuiltinMethod::pop_front },
        { "prev", BuiltinMethod::prev },
        { "product", BuiltinMethod::product },
        { "push_back", BuiltinMethod::push_back },
        { "push_front", BuiltinMethod::push_front },
        { "putc", BuiltinMethod::putc },
        { "realtoa", BuiltinMethod::realtoa },
        { "reverse", BuiltinMethod::reverse },
        { "rsort", BuiltinMethod::rsort },
        { "shuffle", BuiltinMethod::shuffle },
        { "size", BuiltinMethod::size },
        { "sort", BuiltinMethod::sort },
        { "substr", BuiltinMethod::substr },
        { "sum", BuiltinMethod::sum },
        { "tolower", BuiltinMethod::tolower },
        { "toupper", BuiltinMethod::toupper },
        { "unique", BuiltinMethod::unique },
        { "unique_index", BuiltinMethod::unique_index },
        { "xor", BuiltinMethod::reduce_xor },
    } };

// Sorted by name, for builtin_method_named.
static_assert(std::ranges::is_sorted(kBuiltinMethodNames, { },
    &std::pair<std::string_view, BuiltinMethod>::first));

} // namespace detail

// The built-in method a member name names (none for any other name).
[[nodiscard]] constexpr BuiltinMethod builtin_method_named(
    const std::string_view name) noexcept
{
    const auto& table = detail::kBuiltinMethodNames;
    const auto found = std::ranges::lower_bound(
        table, name, { }, &std::pair<std::string_view, BuiltinMethod>::first);
    return found != table.end() && found->first == name ? found->second
                                                         : BuiltinMethod::none;
}

// The member name of a built-in method (empty for none).
[[nodiscard]] constexpr std::string_view builtin_method_name(
    const BuiltinMethod method) noexcept
{
    for (const auto& [name, value] : detail::kBuiltinMethodNames) {
        if (value == method && method != BuiltinMethod::none) {
            return name;
        }
    }
    return { };
}

} // namespace fsim::support
