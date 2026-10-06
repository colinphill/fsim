// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/hierarchy_path.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace fsim::semantic {

namespace {

[[nodiscard]] std::size_t hash_path(const std::string_view path) noexcept
{
    return std::hash<std::string_view> { }(path);
}

constexpr std::size_t kChunkBytes = std::size_t { 1 } << 16U;

} // namespace

// Paths live in a chunked character arena, so views stay valid as the table
// grows and teardown frees a few large blocks. The index is open addressing
// over ID + 1 (zero marks an empty slot), with each path's hash kept for
// probing and growth.
struct HierarchyPathTable::Storage final {
    std::vector<std::unique_ptr<char[]>> chunks;
    std::size_t chunk_used { kChunkBytes };
    std::vector<std::string_view> paths;
    std::vector<std::size_t> hashes;
    std::vector<std::uint32_t> slots;
    // A table extended by a Builder keeps the source arena alive.
    std::shared_ptr<const Storage> base;
    bool frozen { false };

    [[nodiscard]] std::optional<HierarchyPathId> find(
        const std::string_view path, const std::size_t hash) const noexcept
    {
        if (slots.empty()) {
            return std::nullopt;
        }
        const auto mask = slots.size() - 1U;
        for (auto at = hash & mask;; at = (at + 1U) & mask) {
            const auto entry = slots[at];
            if (entry == 0U) {
                return std::nullopt;
            }
            const auto index = entry - 1U;
            if (hashes[index] == hash && paths[index] == path) {
                return HierarchyPathId::from_index(index);
            }
        }
    }

    void place(const std::uint32_t index)
    {
        const auto mask = slots.size() - 1U;
        auto at = hashes[index] & mask;
        while (slots[at] != 0U) {
            at = (at + 1U) & mask;
        }
        slots[at] = index + 1U;
    }

    void reserve_slots(const std::size_t count)
    {
        // Load factor at most one half.
        if (2U * count <= slots.size()) {
            return;
        }
        std::size_t capacity = std::max<std::size_t>(slots.size(), 64U);
        while (capacity < 2U * count) {
            capacity *= 2U;
        }
        slots.assign(capacity, 0U);
        for (std::uint32_t index = 0U; index < paths.size(); ++index) {
            place(index);
        }
    }

    [[nodiscard]] std::string_view store(const std::string_view path)
    {
        if (path.empty()) {
            return { };
        }
        if (path.size() > kChunkBytes - chunk_used) {
            const auto size = std::max(kChunkBytes, path.size());
            chunks.push_back(std::make_unique<char[]>(size));
            chunk_used = size == kChunkBytes ? 0U : size;
            if (size != kChunkBytes) {
                std::memcpy(chunks.back().get(), path.data(), path.size());
                return { chunks.back().get(), path.size() };
            }
        }
        auto* const destination = chunks.back().get() + chunk_used;
        std::memcpy(destination, path.data(), path.size());
        chunk_used += path.size();
        return { destination, path.size() };
    }
};

HierarchyPathTable::Builder::Builder()
    : storage_(std::make_shared<Storage>())
{
}

HierarchyPathTable::Builder::Builder(const HierarchyPathTable& table)
    : Builder()
{
    // Paths are already unique: share the source arena and copy the index.
    if (table.storage_) {
        storage_->base = table.storage_;
        storage_->paths = table.storage_->paths;
        storage_->hashes = table.storage_->hashes;
        storage_->slots = table.storage_->slots;
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
    const auto hash = hash_path(path);
    if (const auto found = storage_->find(path, hash)) {
        return *found;
    }
    if (storage_->paths.size()
        >= std::numeric_limits<std::uint32_t>::max() - 1U) {
        throw std::length_error("hierarchy path table is too large");
    }
    const auto index = static_cast<std::uint32_t>(storage_->paths.size());
    storage_->reserve_slots(storage_->paths.size() + 1U);
    storage_->paths.push_back(storage_->store(path));
    storage_->hashes.push_back(hash);
    storage_->place(index);
    return HierarchyPathId::from_index(index);
}

std::optional<HierarchyPathId> HierarchyPathTable::Builder::find(
    const std::string_view path) const
{
    if (!storage_) {
        return std::nullopt;
    }
    return storage_->find(path, hash_path(path));
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
    return storage_->find(path, hash_path(path));
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
