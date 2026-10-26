// SPDX-License-Identifier: Apache-2.0
//
// SimIR operation kinds by ordinal, for code that chooses an operation's
// helper once (generated code calls the helper of an instruction's kind by
// name) instead of dispatching over operations at run time.
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace fsim::runtime::simir::static_kernel_detail {

/// Operation kinds by ordinal: the alternatives of Operation's groups in
/// declaration order. A generic instruction carries its operation's ordinal
/// (KInst::sub), so generated code calls that kind's helper by name.
template <typename Stored>
struct UnboxedOperationGroup {
    using type = Stored;
};

template <typename Group>
struct UnboxedOperationGroup<BoxedOperationGroup<Group>> {
    using type = Group;
};

template <std::size_t G>
using OperationGroupAt = typename UnboxedOperationGroup<
    std::variant_alternative_t<G, Operation::Storage>>::type;

template <std::size_t G>
inline constexpr std::size_t operation_group_offset
    = operation_group_offset<G - 1U>
    + std::variant_size_v<typename OperationGroupAt<G - 1U>::Storage>;

template <>
inline constexpr std::size_t operation_group_offset<0U> = 0U;

inline constexpr std::size_t operation_group_count
    = std::variant_size_v<Operation::Storage>;
inline constexpr std::size_t operation_kind_count
    = operation_group_offset<operation_group_count - 1U>
    + std::variant_size_v<
        typename OperationGroupAt<operation_group_count - 1U>::Storage>;
static_assert(operation_kind_count <= 256U,
    "operation ordinals must fit KInst::sub");

/// The ordinal of `operation`'s alternative.
[[nodiscard]] inline std::size_t operation_kind_ordinal(
    const Operation& operation)
{
    return std::visit(
        [](const auto& stored) {
            using Stored = std::decay_t<decltype(stored)>;
            using Group = typename UnboxedOperationGroup<Stored>::type;
            constexpr auto group = []<std::size_t... G>(
                                       std::index_sequence<G...>) {
                std::size_t found = 0U;
                ((std::is_same_v<OperationGroupAt<G>, Group> ? (found = G, 0)
                                                             : 0),
                    ...);
                return found;
            }(std::make_index_sequence<operation_group_count>());
            if constexpr (requires { stored.value; }) {
                return operation_group_offset<group> + stored.value->storage.index();
            } else {
                return operation_group_offset<group> + stored.storage.index();
            }
        },
        operation.storage);
}

/// The alternative with ordinal K.
template <std::size_t K, std::size_t G = 0U>
struct OperationKindAtImpl {
    static constexpr std::size_t size
        = std::variant_size_v<typename OperationGroupAt<G>::Storage>;
    using type = typename std::conditional_t<(K < size),
        std::variant_alternative<(K < size ? K : 0U),
            typename OperationGroupAt<G>::Storage>,
        OperationKindAtImpl<(K < size ? 0U : K - size), G + 1U>>::type;
};

template <std::size_t K>
using OperationKindAt = typename OperationKindAtImpl<K>::type;

/// The ordinal of alternative T.
template <typename T>
inline constexpr std::size_t operation_kind_of = []<std::size_t... K>(
                                                     std::index_sequence<K...>) {
    std::size_t found = operation_kind_count;
    ((std::is_same_v<OperationKindAt<K>, T> ? (found = K, 0) : 0), ...);
    return found;
}(std::make_index_sequence<operation_kind_count>());

/// Returned by an operation-tier helper that stops the run (an error, a
/// thread boundary, a hosted process's return to its scheduler).
inline constexpr std::uint32_t operation_tier_stop = 0xffffffffU;

} // namespace fsim::runtime::simir::static_kernel_detail
