// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/systemverilog_builtin_type.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace fsim::support {

// The built-in methods IEEE 1800-2023 defines on values that are not class
// objects: string methods (6.16), enumeration methods (6.19.5), array,
// associative array and queue methods (7.5.1, 7.9, 7.10.2) and array
// manipulation methods (7.12); and the methods of the built-in classes
// semaphore and mailbox (15.3, 15.4). A method call `receiver.name(...)` is
// resolved once against these member lists; every later reader takes the
// method from the call, never from its name. One name may be a member of
// several lists (`first` of an enumeration and of an associative array,
// `num` of an associative array and of a mailbox): the receiver's type
// tells which one applies, and a call on an object of a user class keeps
// the member list's method but is a call of that class's method.
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
    // Semaphore (15.3) and mailbox (15.4) methods; `num` is above.
    put,
    get,
    try_get,
    try_put,
    peek,
    try_peek,
    // The iterator index query of an array method's with clause (7.12.4)
    // and the triggered state of an event or sequence (15.5.3, 16.13.6).
    index,
    triggered,
};

namespace detail {

inline constexpr std::array<std::pair<std::string_view, BuiltinMethod>, 60>
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
        { "get", BuiltinMethod::get },
        { "getc", BuiltinMethod::getc },
        { "hextoa", BuiltinMethod::hextoa },
        { "icompare", BuiltinMethod::icompare },
        { "index", BuiltinMethod::index },
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
        { "peek", BuiltinMethod::peek },
        { "pop_back", BuiltinMethod::pop_back },
        { "pop_front", BuiltinMethod::pop_front },
        { "prev", BuiltinMethod::prev },
        { "product", BuiltinMethod::product },
        { "push_back", BuiltinMethod::push_back },
        { "push_front", BuiltinMethod::push_front },
        { "put", BuiltinMethod::put },
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
        { "triggered", BuiltinMethod::triggered },
        { "try_get", BuiltinMethod::try_get },
        { "try_peek", BuiltinMethod::try_peek },
        { "try_put", BuiltinMethod::try_put },
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

// The keyword type of a built-in method's value when every receiver gives
// it the same type: the string methods (IEEE 1800-2023 6.16), num (6.19.5.6,
// 7.9.1, 15.4.3), size (7.5.2, 7.10.2.1), exists (7.9.3), and the
// semaphore's and mailbox's try_get, try_put and try_peek (15.3.4, 15.4.5,
// 15.4.6, 15.4.8). None for a task or void function, and for a value of
// the receiver's type (an enumeration's first, last, next and prev; the
// array locator, ordering and reduction methods); an associative array's
// traversal functions first, last, next and prev return int (7.9.4 to
// 7.9.7), which the caller tells from an enumeration's.
[[nodiscard]] constexpr SystemVerilogBuiltinType builtin_method_result_type(
    const BuiltinMethod method) noexcept
{
    using Type = SystemVerilogBuiltinType;
    switch (method) {
    case BuiltinMethod::len:
    case BuiltinMethod::compare:
    case BuiltinMethod::icompare:
    case BuiltinMethod::num:
    case BuiltinMethod::size:
    case BuiltinMethod::exists:
    case BuiltinMethod::try_get:
    case BuiltinMethod::try_put:
    case BuiltinMethod::try_peek:
        return Type::int_type;
    case BuiltinMethod::getc:
        return Type::byte;
    case BuiltinMethod::atoi:
    case BuiltinMethod::atohex:
    case BuiltinMethod::atooct:
    case BuiltinMethod::atobin:
        return Type::integer;
    case BuiltinMethod::atoreal:
        return Type::real;
    case BuiltinMethod::toupper:
    case BuiltinMethod::tolower:
    case BuiltinMethod::substr:
    case BuiltinMethod::name:
        return Type::string;
    default:
        return Type::none;
    }
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
