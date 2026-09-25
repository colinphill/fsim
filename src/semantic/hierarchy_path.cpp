// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/hierarchy_path.hpp"

#include <deque>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace fsim::semantic {

namespace {

struct HierarchyPathHash final {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(
        const std::string_view path) const noexcept
    {
        return std::hash<std::string_view> { }(path);
    }
};

struct HierarchyPathEqual final {
    using is_transparent = void;

    [[nodiscard]] bool operator()(
        const std::string_view left,
        const std::string_view right) const noexcept
    {
        return left == right;
    }
};

} // namespace

struct HierarchyPathTable::Storage final {
    std::deque<std::string> paths;
    std::unordered_map<std::string_view, HierarchyPathId,
        HierarchyPathHash, HierarchyPathEqual> ids;
    bool frozen { false };
};

HierarchyPathTable::Builder::Builder()
    : storage_(std::make_shared<Storage>())
{
}

HierarchyPathTable::Builder::Builder(const HierarchyPathTable& table)
    : Builder()
{
    for (std::size_t index = 0; index < table.size(); ++index) {
        const auto id = HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        if (intern(table.view(id)) != id) {
            throw std::logic_error(
                "hierarchy path table extension changed an existing ID");
        }
    }
}

HierarchyPathTable::Builder::~Builder() = default;
HierarchyPathTable::Builder::Builder(Builder&&) noexcept = default;
HierarchyPathTable::Builder& HierarchyPathTable::Builder::operator=(
    Builder&&) noexcept = default;

HierarchyPathId HierarchyPathTable::Builder::intern(
    const std::string_view path)
{
    if (!storage_ || storage_->frozen) {
        throw std::logic_error("hierarchy path builder is frozen");
    }

    if (const auto found = storage_->ids.find(path);
        found != storage_->ids.end()) {
        return found->second;
    }

    if (storage_->paths.size()
        >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("hierarchy path table is too large");
    }

    const auto index = static_cast<std::uint32_t>(storage_->paths.size());
    storage_->paths.emplace_back(path);
    const auto stored_path = std::string_view { storage_->paths.back() };
    const auto id = HierarchyPathId::from_index(index);
    try {
        const auto [entry, inserted] = storage_->ids.emplace(stored_path, id);
        if (!inserted) {
            storage_->paths.pop_back();
            return entry->second;
        }
    } catch (...) {
        storage_->paths.pop_back();
        throw;
    }

    return id;
}

std::optional<HierarchyPathId> HierarchyPathTable::Builder::find(
    const std::string_view path) const
{
    if (!storage_) {
        return std::nullopt;
    }
    const auto found = storage_->ids.find(path);
    if (found == storage_->ids.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool HierarchyPathTable::Builder::contains(const std::string_view path) const
{
    return find(path).has_value();
}

std::string_view HierarchyPathTable::Builder::view(
    const HierarchyPathId id) const
{
    if (!storage_ || !id.valid() || id.value() >= storage_->paths.size()) {
        throw std::out_of_range("hierarchy path ID is not in this builder");
    }
    return storage_->paths[id.value()];
}

std::size_t HierarchyPathTable::Builder::size() const noexcept
{
    return storage_ ? storage_->paths.size() : 0U;
}

HierarchyPathTable HierarchyPathTable::Builder::freeze() &&
{
    if (!storage_ || storage_->frozen) {
        throw std::logic_error("hierarchy path builder is already frozen");
    }
    storage_->frozen = true;
    std::shared_ptr<const Storage> frozen_storage { std::move(storage_) };
    return HierarchyPathTable { std::move(frozen_storage) };
}

HierarchyPathTable::HierarchyPathTable() = default;

HierarchyPathTable::HierarchyPathTable(
    std::shared_ptr<const Storage> storage)
    : storage_(std::move(storage))
{
}

std::optional<HierarchyPathId> HierarchyPathTable::find(
    const std::string_view path) const
{
    if (!storage_) {
        return std::nullopt;
    }
    const auto found = storage_->ids.find(path);
    if (found == storage_->ids.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool HierarchyPathTable::contains(const std::string_view path) const
{
    return find(path).has_value();
}

std::string_view HierarchyPathTable::view(const HierarchyPathId id) const
{
    if (!storage_ || !id.valid() || id.value() >= storage_->paths.size()) {
        throw std::out_of_range("hierarchy path ID is not in this table");
    }
    return storage_->paths[id.value()];
}

std::size_t HierarchyPathTable::size() const noexcept
{
    return storage_ ? storage_->paths.size() : 0U;
}

} // namespace fsim::semantic
