// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

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
