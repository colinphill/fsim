// SPDX-License-Identifier: Apache-2.0
// The class heap and the class static store as sections of a run-state image
// (IEEE 1800-2023 D.9): objects keep their slot and generation, so a handle a
// design holds names the same object after a restart.
#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/class_static.hpp"
#include "fsim/runtime/run_state_value_codecs.hpp"

#include <exception>
#include <utility>

namespace fsim::runtime {

void SystemVerilogClassHeap::run_state_io(RunStateArchive& archive)
{
    std::uint64_t seed = simulation_seed_;
    archive.io(seed);
    if (archive.loading() && archive.ok() && seed != simulation_seed_) {
        archive.fail("the class heap was saved with another simulation seed");
        return;
    }
    const auto visit = [&](auto& slots, auto& free_slots, auto& live_objects,
                           auto& storage_bytes, auto& ordinals,
                           auto& next_seed) {
        archive.io(slots);
        archive.io(free_slots);
        archive.io(live_objects);
        archive.io(storage_bytes);
        archive.io(ordinals);
        archive.io(next_seed);
    };
    if (archive.saving()) {
        visit(slots_, free_slots_, live_objects_, storage_bytes_,
            next_object_ordinals_, next_object_seed_);
        return;
    }
    // Decoded aside, so a section that does not decode leaves the heap as it was.
    std::vector<Slot> slots;
    std::set<std::uint32_t> free_slots;
    std::size_t live_objects { };
    std::size_t storage_bytes { };
    std::map<std::string, std::uint64_t, std::less<>> ordinals;
    std::optional<std::uint64_t> next_seed;
    visit(slots, free_slots, live_objects, storage_bytes, ordinals, next_seed);
    if (!archive.ok()) {
        return;
    }
    slots_ = std::move(slots);
    free_slots_ = std::move(free_slots);
    live_objects_ = live_objects;
    storage_bytes_ = storage_bytes;
    next_object_ordinals_ = std::move(ordinals);
    next_object_seed_ = next_seed;
}

void SystemVerilogClassStaticStore::run_state_io(RunStateArchive& archive)
{
    if (archive.saving()) {
        static_cast<void>(archive.count(entries_.size()));
        for (auto& [name, entry] : entries_) {
            std::string key = name;
            archive.io(key);
            archive.io(entry.initialized);
            archive.io(entry.accounted_bytes);
            archive.io(entry.property_names);
            archive.io(entry.values);
        }
        archive.io(property_count_);
        archive.io(storage_bytes_);
        return;
    }
    struct Saved {
        std::string name;
        bool initialized { };
        std::size_t accounted_bytes { };
        std::vector<std::string> property_names;
        std::vector<SystemVerilogClassPropertyValue> values;
    };
    std::vector<Saved> saved;
    const auto count = archive.count(0U);
    for (std::uint64_t index = 0U; index < count && archive.ok(); ++index) {
        auto& item = saved.emplace_back();
        archive.io(item.name);
        archive.io(item.initialized);
        archive.io(item.accounted_bytes);
        archive.io(item.property_names);
        archive.io(item.values);
    }
    std::size_t property_count { };
    std::size_t storage_bytes { };
    archive.io(property_count);
    archive.io(storage_bytes);
    if (!archive.ok()) {
        return;
    }
    // Registering a specialization first named after the start goes through
    // the resolver, as it did in the saved run.
    try {
        for (auto& item : saved) {
            auto& target = entry(item.name);
            if (target.property_names != item.property_names) {
                archive.fail("class '" + item.name
                    + "' has other static properties than the saved run");
                return;
            }
            target.values = std::move(item.values);
            target.initialized = item.initialized;
            target.initializing = false;
            target.accounted_bytes = item.accounted_bytes;
        }
    } catch (const std::exception& error) {
        archive.fail(error.what());
        return;
    }
    property_count_ = property_count;
    storage_bytes_ = storage_bytes;
}

} // namespace fsim::runtime
