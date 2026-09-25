// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace fsim::semantic {

struct HierarchyPathTag final { };
using HierarchyPathId = Id<HierarchyPathTag>;

/// An immutable owner for interned hierarchy path spellings.
///
/// IDs are local to one table and are not persistent artifact identities.
/// Copying a table shares its immutable storage. A view returned by view() or
/// Builder::view() remains valid while the corresponding table or builder
/// storage is alive; views obtained from a builder also survive later intern()
/// calls.
class HierarchyPathTable final {
    struct Storage;

public:
    /// Mutable path collection used before a design publishes path views.
    class Builder final {
    public:
        Builder();
        /// Copy an immutable table and preserve its existing ID prefix.
        explicit Builder(const HierarchyPathTable& table);
        ~Builder();
        Builder(Builder&&) noexcept;
        Builder& operator=(Builder&&) noexcept;

        Builder(const Builder&) = delete;
        Builder& operator=(const Builder&) = delete;

        /// Return the existing ID or append the path and return its new ID.
        [[nodiscard]] HierarchyPathId intern(std::string_view path);

        /// Find a path without changing the builder.
        [[nodiscard]] std::optional<HierarchyPathId> find(
            std::string_view path) const;

        [[nodiscard]] bool contains(std::string_view path) const;
        [[nodiscard]] std::string_view view(HierarchyPathId id) const;
        [[nodiscard]] std::size_t size() const noexcept;

        /// Consume the builder and publish an immutable path owner.
        [[nodiscard]] HierarchyPathTable freeze() &&;

    private:
        friend class HierarchyPathTable;
        std::shared_ptr<Storage> storage_;
    };

    HierarchyPathTable();

    /// Find a path without changing this table.
    [[nodiscard]] std::optional<HierarchyPathId> find(
        std::string_view path) const;

    [[nodiscard]] bool contains(std::string_view path) const;
    [[nodiscard]] std::string_view view(HierarchyPathId id) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    explicit HierarchyPathTable(std::shared_ptr<const Storage> storage);

    std::shared_ptr<const Storage> storage_;
};

} // namespace fsim::semantic
