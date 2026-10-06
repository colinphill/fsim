// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <type_traits>

namespace fsim::runtime::simir::storage_census_detail {

struct OperationBodyStorageRecord {
    std::size_t operation_count { };
    std::size_t operation_capacity { };
    std::size_t boxed_group_allocations { };
    std::size_t boxed_group_object_bytes { };
    std::size_t nested_vector_allocations { };
    std::size_t nested_vector_capacity_elements { };
    std::size_t nested_vector_capacity_bytes { };
};

class UniqueOperationBodyStorageCensus final {
public:
    void add(const OperationList& operations)
    {
        const auto* const identity = operations.body_identity();
        if (identity == nullptr) {
            ++unbacked_views;
            return;
        }
        ++body_references;
        if (unique_bodies.contains(identity)) {
            return;
        }

        auto [entry, inserted] = unique_bodies.emplace(identity,
            OperationBodyStorageRecord {
                operations.size(), operations.capacity() });
        if (inserted) {
            count_owned_storage(operations, entry->second);
        }
    }

    std::size_t body_references { };
    std::size_t unbacked_views { };
    std::map<const void*, OperationBodyStorageRecord> unique_bodies;

private:
    template <typename Sequence>
    static void count_nested_vector(const Sequence& sequence,
        OperationBodyStorageRecord& record)
    {
        using SequenceType = std::remove_cvref_t<Sequence>;
        using Element = typename SequenceType::value_type;
        const auto capacity = static_cast<std::size_t>(sequence.capacity());
        if (capacity == 0U) {
            return;
        }
        ++record.nested_vector_allocations;
        record.nested_vector_capacity_elements += capacity;
        record.nested_vector_capacity_bytes += capacity * sizeof(Element);
    }

    template <typename Value>
    static void count_common_vectors(const Value& value,
        OperationBodyStorageRecord& record)
    {
        if constexpr (requires { value.operands.capacity(); }) {
            count_nested_vector(value.operands, record);
        }
        if constexpr (requires { value.signals.capacity(); }) {
            count_nested_vector(value.signals, record);
        }
        if constexpr (requires { value.edges.capacity(); }) {
            count_nested_vector(value.edges, record);
        }
        if constexpr (requires { value.events.capacity(); }) {
            count_nested_vector(value.events, record);
        }
        if constexpr (requires { value.branches.capacity(); }) {
            count_nested_vector(value.branches, record);
        }
        if constexpr (requires { value.elements.capacity(); }) {
            count_nested_vector(value.elements, record);
        }
    }

    static void count_owned_storage(const OperationList& operations,
        OperationBodyStorageRecord& record)
    {
        // Deduplication names the immutable body, not facade overrides.
        const std::span<const Operation> body {
            operations.data(), operations.size() };
        for (const auto& operation : body) {
            const bool group_is_available = std::visit(
                [&record](const auto& stored) {
                    using Stored = std::remove_cvref_t<decltype(stored)>;
                    if constexpr (requires {
                                      typename Stored::boxed_group_type;
                                  }) {
                        if (stored.value == nullptr) {
                            return false;
                        }
                        ++record.boxed_group_allocations;
                        record.boxed_group_object_bytes +=
                            sizeof(typename Stored::boxed_group_type);
                    }
                    return true;
                },
                operation.storage);
            if (!group_is_available) {
                continue;
            }
            visit_operation([&record](const auto& value) {
                count_common_vectors(value, record);
            }, operation);
        }
    }
};

} // namespace fsim::runtime::simir::storage_census_detail
