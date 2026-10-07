// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct SignalHot;

namespace operation_list_detail {

/// Shared operation-body comparator used by Process and derived program views.
/// Shape/layout validation remains with the caller; this helper checks every
/// operation alternative and stores hierarchy-specific differences sparsely.
struct ShareAccess {
    [[nodiscard]] static bool shareable(const OperationList& operations);
    [[nodiscard]] static bool share(
        const OperationList& representative,
        OperationList& candidate,
        std::span<const Signal> signals,
        OperationList::Storage* recycled_operations = nullptr);
    [[nodiscard]] static bool share(
        const OperationList& representative,
        OperationList& candidate,
        std::span<const SignalHot> signals,
        OperationList::Storage* recycled_operations = nullptr);

    // Runtime-state codec access. An instance list is its shared body plus
    // per-instruction overrides plus debug-scope remaps (canonical scope of
    // the body to this instance's scope).

    /// Instructions whose operation differs from the shared body other than
    /// through a debug-scope remap, ascending.
    [[nodiscard]] static std::vector<OperationList::size_type>
    instance_override_indices(const OperationList& operations);
    [[nodiscard]] static std::size_t debug_scope_count(
        const OperationList& operations) noexcept;
    [[nodiscard]] static const InternedString& debug_scope_canonical(
        const OperationList& operations, std::size_t index);
    [[nodiscard]] static const InternedString& debug_scope_instance(
        const OperationList& operations, std::size_t index);
    [[nodiscard]] static InternedString& debug_scope_instance(
        OperationList& operations, std::size_t index);
    /// Whether instruction `index` has an operation or debug-point override.
    [[nodiscard]] static bool has_instruction_override(
        const OperationList& operations, OperationList::size_type index);
    /// Remap the debug scope `canonical` of the shared body to `instance`
    /// (replacing an existing remap of it).
    static void remap_debug_scope(OperationList& operations,
        const InternedString& canonical, InternedString instance);
    /// Install decoded debug-scope remaps; false (nothing changed) when a
    /// canonical scope is empty or repeated.
    [[nodiscard]] static bool set_debug_scopes(OperationList& operations,
        std::vector<std::pair<InternedString, InternedString>> scopes);

private:
    template<typename SignalRecord>
    [[nodiscard]] static bool share_impl(
        const OperationList& representative,
        OperationList& candidate,
        std::span<const SignalRecord> signals,
        OperationList::Storage* recycled_operations);
};

} // namespace operation_list_detail
} // namespace fsim::runtime::simir
