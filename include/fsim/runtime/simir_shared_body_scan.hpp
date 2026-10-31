// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

/// Visits the operations that `select` keeps in many operation lists, most
/// of which share a body with others (one template lowered for many
/// instances). Each shared body is scanned once, over its canonical
/// operations; every list of that body then tests only the kept positions
/// and the positions its own operation overrides replace. Positions are
/// visited in ascending order, as a full scan visits them.
///
/// `select` must depend only on what instance fields leave unchanged (the
/// operation's alternative and its constant fields), so that a stored
/// operation and its expansion agree.
template <typename Select>
class SharedBodyOperationScan {
public:
    explicit SharedBodyOperationScan(Select select)
        : select_(std::move(select))
    {
    }
    /// A copy keeps the selection and starts with no scanned bodies: one
    /// scan per worker thread, each scanning a body at most once.
    SharedBodyOperationScan(const SharedBodyOperationScan& other)
        : select_(other.select_)
    {
    }
    SharedBodyOperationScan(SharedBodyOperationScan&&) noexcept = default;
    SharedBodyOperationScan& operator=(const SharedBodyOperationScan&) = delete;
    SharedBodyOperationScan& operator=(SharedBodyOperationScan&&) = delete;

    /// Visits each kept operation expanded with the list's instance fields
    /// (OperationList::expanded_ref).
    template <typename Visit>
    void visit(const OperationList& operations, Visit&& visit)
    {
        scan(operations, [&](const std::size_t index) {
            visit(index, operations.expanded_ref(index, scratch_));
        });
    }

    /// Visits each kept operation as the list stores it, with operation
    /// overrides but without instance fields (OperationList::operator[]).
    template <typename Visit>
    void visit_stored(const OperationList& operations, Visit&& visit)
    {
        scan(operations, [&](const std::size_t index) {
            visit(index, operations[index]);
        });
    }

private:
    // Calls `at` for every position whose stored operation `select_` keeps,
    // in ascending order.
    template <typename At>
    void scan(const OperationList& operations, At&& at)
    {
        // Consecutive lists often share a body (instances of one template).
        const void* const body = operations.body_identity();
        if (body != last_body_ || last_kept_ == nullptr) {
            auto [found, inserted] = selected_.try_emplace(body);
            if (inserted) {
                const auto* shared = operations.data();
                for (std::size_t index = 0U; index < operations.size();
                     ++index) {
                    if (select_(shared[index])) {
                        found->second.push_back(
                            static_cast<std::uint32_t>(index));
                    }
                }
            }
            last_body_ = body;
            last_kept_ = &found->second;
        }
        const auto& kept = *last_kept_;
        // A kept position that no override replaces holds the body's
        // operation, which `select_` keeps; a replaced one is tested again.
        std::size_t next = 0U;
        operations.visit_operation_override_positions(
            [&](const std::size_t replaced) {
                while (next < kept.size() && kept[next] < replaced) {
                    at(static_cast<std::size_t>(kept[next++]));
                }
                if (next < kept.size() && kept[next] == replaced) {
                    ++next;
                }
                if (select_(operations[replaced])) {
                    at(replaced);
                }
            });
        while (next < kept.size()) {
            at(static_cast<std::size_t>(kept[next++]));
        }
    }

    Select select_;
    std::unordered_map<const void*, std::vector<std::uint32_t>> selected_;
    const void* last_body_ { };
    const std::vector<std::uint32_t>* last_kept_ { };
    Operation scratch_;
};

} // namespace fsim::runtime::simir
