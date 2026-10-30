// SPDX-License-Identifier: Apache-2.0
#include "simir_signal_driver_inventory.hpp"
#include "simir_region_graph_driver_class.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace fsim::runtime::simir {
namespace {

static_assert(static_cast<std::uint8_t>(ResolutionKind::sv_user_first)
    == static_cast<std::uint8_t>(
        SignalDriverInventoryResolution::sv_user_first));
static_assert(static_cast<std::uint8_t>(ValueKind::logic9)
    == static_cast<std::uint8_t>(SignalDriverInventoryValueKind::logic9));
static_assert(static_cast<std::uint8_t>(RegionDriverClass::unknown)
    == static_cast<std::uint8_t>(SignalDriverInventoryDriverClass::unknown));
static_assert(static_cast<std::uint8_t>(EdgeKind::transaction)
    == static_cast<std::uint8_t>(SignalDriverInventoryEdge::transaction));
static_assert(static_cast<std::uint8_t>(EdgeKind::falling)
    == static_cast<std::uint8_t>(SignalDriverInventoryEdge::falling));

std::size_t word_count(const std::uint32_t width) noexcept
{
    return static_cast<std::size_t>(width / 64U)
        + (width % 64U == 0U ? 0U : 1U);
}

void add_range_to_mask(std::span<std::uint64_t> mask,
    const std::uint32_t offset,
    const std::uint32_t width)
{
    const auto end = static_cast<std::uint64_t>(offset) + width;
    auto bit = static_cast<std::uint64_t>(offset);
    while (bit < end) {
        const auto word = static_cast<std::size_t>(bit / 64U);
        const auto in_word = static_cast<unsigned>(bit % 64U);
        const auto count = static_cast<unsigned>(std::min<std::uint64_t>(
            end - bit, 64U - in_word));
        const auto low_mask = count == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : ((UINT64_C(1) << count) - UINT64_C(1));
        mask[word] |= low_mask << in_word;
        bit += count;
    }
}

} // namespace

SignalDriverInventory build_signal_driver_inventory(
    const RegionGraph& graph)
{
    SignalDriverInventory result;
    const auto graph_signals = graph.signals();
    result.process_count = graph.processes().size();
    result.static_access_inventory_complete
        = graph.access_inventory_complete();
    result.signals.reserve(graph_signals.size());

    for (SignalId signal_id = 0U; signal_id < graph_signals.size();
         ++signal_id) {
        const auto& node = graph_signals[signal_id];
        const auto& descriptor = node.descriptor;
        const auto structural_driver_class
            = detail::classify_region_driver_class(node,
                detail::RegionDriverClassificationMode::persisted_structure);
        SignalDriverInventorySignal entry;
        entry.width = descriptor.width;
        entry.resolution = static_cast<SignalDriverInventoryResolution>(
            descriptor.resolution);
        entry.value_kind = static_cast<SignalDriverInventoryValueKind>(
            descriptor.value_kind);
        entry.drivers = static_cast<SignalDriverInventoryDriverClass>(
            structural_driver_class);
        entry.implicit_driver = descriptor.implicit_driver;
        entry.event_variable = descriptor.event_variable;
        entry.writers_unknown = node.writers_unknown;
        entry.dynamic_fork_writers = node.dynamic_fork_writers;
        entry.partial_projected_transactions
            = node.partial_projected_transactions;
        entry.projected_slice_certificate_required
            = node.partial_projected_transactions;
        entry.first_writer = result.writers.size();
        entry.writer_count = node.writers.size();
        for (const auto& writer : node.writers) {
            result.writers.push_back({ writer.process, writer.offset,
                writer.width, static_cast<SignalDriverInventoryEdge>(
                    writer.edge) });
        }

        const bool signal_shape_supported
            = descriptor.width != 0U
            && (descriptor.value_kind == ValueKind::logic4
                || descriptor.value_kind == ValueKind::logic9)
            && !descriptor.implicit_driver
            && !descriptor.event_variable
            && !node.writers_unknown
            && !node.dynamic_fork_writers;
        std::map<ProcessId, std::vector<std::uint64_t>> owner_masks;
        bool owner_ranges_valid = signal_shape_supported;
        const auto words = word_count(descriptor.width);
        if (owner_ranges_valid) {
            for (const auto& writer : node.writers) {
                const auto width = writer.width == 0U
                    ? descriptor.width : writer.width;
                if (width == 0U || writer.offset >= descriptor.width
                    || width > descriptor.width - writer.offset) {
                    owner_ranges_valid = false;
                    break;
                }
                auto& mask = owner_masks.try_emplace(writer.process,
                    words, UINT64_C(0)).first->second;
                add_range_to_mask(mask, writer.offset, width);
            }
        }

        bool complete_single_owner = false;
        if (owner_ranges_valid
            && structural_driver_class == RegionDriverClass::single_whole
            && owner_masks.size() == 1U && node.writers.size() == 1U) {
            const auto& writer = node.writers.front();
            complete_single_owner = writer.offset == 0U
                && (writer.width == 0U
                    || writer.width == descriptor.width);
        }

        bool disjoint_owner_ranges = false;
        if (owner_ranges_valid
            && structural_driver_class == RegionDriverClass::disjoint_partial
            && !owner_masks.empty()) {
            std::vector<std::uint64_t> assigned(words, 0U);
            disjoint_owner_ranges = true;
            for (const auto& [owner, mask] : owner_masks) {
                static_cast<void>(owner);
                for (std::size_t word = 0U; word < words; ++word) {
                    if ((mask[word] & assigned[word]) != 0U) {
                        disjoint_owner_ranges = false;
                        break;
                    }
                    assigned[word] |= mask[word];
                }
                if (!disjoint_owner_ranges) {
                    break;
                }
            }
            disjoint_owner_ranges &= std::ranges::none_of(
                owner_masks, [](const auto& owner) {
                    return std::ranges::all_of(owner.second,
                        [](const std::uint64_t word) { return word == 0U; });
                });
        }

        if (complete_single_owner) {
            entry.storage_class = SignalDriverStorageClass::single_owner;
        } else if (disjoint_owner_ranges) {
            entry.storage_class = SignalDriverStorageClass::disjoint_owner;
        }

        entry.first_owner = result.owners.size();
        if (entry.storage_class != SignalDriverStorageClass::resolved_table) {
            for (auto& [process, mask] : owner_masks) {
                const auto first_mask = result.owner_mask_words.size();
                result.owner_mask_words.insert(result.owner_mask_words.end(),
                    mask.begin(), mask.end());
                result.owners.push_back({
                process, first_mask, mask.size() });
            }
            entry.owner_count = result.owners.size() - entry.first_owner;
        }
        result.signals.push_back(entry);
    }
    return result;
}

} // namespace fsim::runtime::simir
