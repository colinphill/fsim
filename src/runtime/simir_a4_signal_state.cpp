// SPDX-License-Identifier: Apache-2.0
#include "simir_a4_signal_state.hpp"
#include "simir_region_graph_driver_class.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace fsim::runtime::simir {
namespace {

constexpr auto no_word = std::numeric_limits<std::size_t>::max();

static_assert(static_cast<std::uint8_t>(ResolutionKind::sv_user_first)
    == static_cast<std::uint8_t>(
        SignalDriverInventoryResolution::sv_user_first));
static_assert(static_cast<std::uint8_t>(ValueKind::logic9)
    == static_cast<std::uint8_t>(SignalDriverInventoryValueKind::logic9));
static_assert(static_cast<std::uint8_t>(RegionDriverClass::unknown)
    == static_cast<std::uint8_t>(SignalDriverInventoryDriverClass::unknown));
static_assert(static_cast<std::uint8_t>(EdgeKind::transaction)
    == static_cast<std::uint8_t>(SignalDriverInventoryEdge::transaction));

std::size_t checked_add(
    const std::size_t left, const std::size_t right, const char* message)
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::length_error { message };
    }
    return left + right;
}

std::size_t word_count(const std::uint32_t width) noexcept
{
    return static_cast<std::size_t>(width / 64U)
        + (width % 64U == 0U ? 0U : 1U);
}

std::uint64_t valid_word_mask(
    const std::uint32_t width, const std::size_t word) noexcept
{
    const auto used = static_cast<std::size_t>(width) - word * 64U;
    if (used >= 64U) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    if (used == 0U) {
        return 0U;
    }
    return (UINT64_C(1) << used) - UINT64_C(1);
}

std::array<std::uint64_t, 4U> packed_word(
    const PackedLogic4& value, const std::size_t word) noexcept
{
    std::array<std::uint64_t, 4U> result { };
    const auto aval = value.aval_words();
    const auto bval = value.bval_words();
    if (word < aval.size() && word < bval.size()) {
        result[0U] = aval[word];
        result[1U] = bval[word];
    }
    if (value.is_logic9()) {
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
            const auto source = value.logic9_plane_words(plane);
            if (word < source.size()) {
                result[plane] = source[word];
            }
        }
    }
    return result;
}

bool valid_value_kind(
    const ValueKind kind, const PackedLogic4& value) noexcept
{
    switch (kind) {
    case ValueKind::logic4:
        return !value.is_logic9();
    case ValueKind::logic9:
        return value.is_logic9();
    }
    return false;
}

bool canonical_logic9(const PackedLogic4& value) noexcept
{
    if (!value.is_logic9()) {
        return true;
    }
    const auto p0 = value.logic9_plane_words(0U);
    const auto p1 = value.logic9_plane_words(1U);
    const auto p2 = value.logic9_plane_words(2U);
    const auto p3 = value.logic9_plane_words(3U);
    for (std::size_t word = 0U; word < p0.size(); ++word) {
        if ((p3[word] & (p2[word] | p1[word] | p0[word])) != 0U) {
            return false;
        }
    }
    return true;
}

bool same_inventory_writer(const SignalDriverInventoryWriter& writer,
    const RegionAccess& graph_writer) noexcept
{
    return writer.process == graph_writer.process
        && writer.offset == graph_writer.offset
        && writer.width == graph_writer.width
        && writer.edge
            == static_cast<SignalDriverInventoryEdge>(graph_writer.edge);
}

bool canonical_prepared_word(
    const std::array<std::uint64_t, 4U>& word,
    const ValueKind kind,
    const std::uint64_t valid_mask) noexcept
{
    for (const auto plane : word) {
        if ((plane & ~valid_mask) != 0U) {
            return false;
        }
    }
    if (kind == ValueKind::logic4) {
        return word[2U] == 0U && word[3U] == 0U;
    }
    return kind == ValueKind::logic9
        && (word[3U] & (word[2U] | word[1U] | word[0U])) == 0U;
}

bool plane_block_matches_sizes(
    const PackedLogic4PlaneBlock& block,
    const std::size_t value_words,
    const std::size_t logic9_words) noexcept
{
    return block.planes[0U].size() == value_words
        && block.planes[1U].size() == value_words
        && block.planes[2U].size() == logic9_words
        && block.planes[3U].size() == logic9_words;
}

void refresh_plane_block(PackedLogic4PlaneBlock& destination,
    const PackedLogic4PlaneBlock& source) noexcept
{
    for (std::size_t plane = 0U; plane < source.planes.size(); ++plane) {
        std::ranges::copy(source.planes[plane],
            destination.planes[plane].begin());
    }
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

Logic4 edge_value(const PackedLogic4& value) noexcept
{
    if (value.is_logic9()) {
        return to_logic4(value.get_logic9(0U));
    }
    return value.get(0U);
}

} // namespace

AuthoritativeSignalPlanes::PreparedGroupScratch::PreparedGroupScratch(
    PreparedGroupScratch&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr))
    , layout_(std::exchange(other.layout_, nullptr))
    , replacements_(std::move(other.replacements_))
    , spent_(std::exchange(other.spent_, true))
{
}

AuthoritativeSignalPlanes::PreparedGroupScratch&
AuthoritativeSignalPlanes::PreparedGroupScratch::operator=(
    PreparedGroupScratch&& other) noexcept
{
    if (this != &other) {
        owner_ = std::exchange(other.owner_, nullptr);
        layout_ = std::exchange(other.layout_, nullptr);
        replacements_ = std::move(other.replacements_);
        spent_ = std::exchange(other.spent_, true);
    }
    return *this;
}

SignalDriverLayout SignalDriverLayout::build(
    const RegionGraph& graph,
    const std::span<const SignalId> component_signals)
{
    return build(graph, component_signals, std::span<const SignalId> { });
}

SignalDriverLayout SignalDriverLayout::build(
    const RegionGraph& graph,
    const std::span<const SignalId> component_signals,
    const std::span<const SignalId> certified_zero_delay_projected_slices)
{
    return build(graph, component_signals,
        certified_zero_delay_projected_slices, { });
}

SignalDriverLayout SignalDriverLayout::build(
    const RegionGraph& graph,
    const std::span<const SignalId> component_signals,
    const std::span<const SignalId> certified_zero_delay_projected_slices,
    const std::span<const SignalId> certified_unresolved_owner_aliases)
{
    const auto driver_inventory = inventory(graph);
    return build_from_inventory(graph, component_signals, driver_inventory,
        certified_zero_delay_projected_slices,
        certified_unresolved_owner_aliases);
}

SignalDriverInventory SignalDriverLayout::inventory(
    const RegionGraph& graph)
{
    SignalDriverInventory result;
    const auto graph_signals = graph.signals();
    result.process_count = graph.processes().size();
    result.static_access_inventory_complete
        = graph.certificate_inventory().access_inventory_complete;
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

bool signal_driver_inventory_matches_structure(
    const SignalDriverInventory& inventory,
    const RegionGraph& graph) noexcept
{
    const auto graph_signals = graph.signals();
    if (inventory.version != 1U
        || inventory.process_count != graph.processes().size()
        || inventory.static_access_inventory_complete
            != graph.certificate_inventory().access_inventory_complete
        || inventory.signals.size() != graph_signals.size()) {
        return false;
    }
    const auto mask_words = [](const std::uint32_t width) noexcept {
        return static_cast<std::size_t>(width / 64U)
            + (width % 64U == 0U ? 0U : 1U);
    };
    std::size_t next_writer = 0U;
    std::size_t next_owner = 0U;
    std::size_t next_mask_word = 0U;
    for (std::size_t index = 0U; index < graph_signals.size(); ++index) {
        const auto& node = graph_signals[index];
        const auto& descriptor = node.descriptor;
        auto structural_driver_class = node.drivers;
        if (descriptor.external_driver) {
            try {
                structural_driver_class
                    = detail::classify_region_driver_class(node,
                        detail::RegionDriverClassificationMode::persisted_structure);
            } catch (...) {
                return false;
            }
        }
        const auto& entry = inventory.signals[index];
        if (entry.width != descriptor.width
            || entry.resolution
                != static_cast<SignalDriverInventoryResolution>(
                    descriptor.resolution)
            || entry.value_kind
                != static_cast<SignalDriverInventoryValueKind>(
                    descriptor.value_kind)
            || entry.drivers
                != static_cast<SignalDriverInventoryDriverClass>(
                    structural_driver_class)
            || entry.implicit_driver != descriptor.implicit_driver
            || entry.event_variable != descriptor.event_variable
            || entry.writers_unknown != node.writers_unknown
            || entry.dynamic_fork_writers != node.dynamic_fork_writers
            || entry.partial_projected_transactions
                != node.partial_projected_transactions
            || entry.projected_slice_certificate_required
                != node.partial_projected_transactions
            || entry.first_writer != next_writer
            || entry.first_writer > inventory.writers.size()
            || entry.writer_count
                > inventory.writers.size() - entry.first_writer
            || entry.writer_count != node.writers.size()
            || entry.first_owner != next_owner
            || entry.first_owner > inventory.owners.size()
            || entry.owner_count
                > inventory.owners.size() - entry.first_owner) {
            return false;
        }
        for (std::size_t writer_index = 0U;
             writer_index < entry.writer_count; ++writer_index) {
            const auto& writer = inventory.writers[
                entry.first_writer + writer_index];
            const auto& graph_writer = node.writers[writer_index];
            if (!same_inventory_writer(writer, graph_writer)) {
                return false;
            }
        }
        next_writer += entry.writer_count;
        next_owner += entry.owner_count;
        switch (entry.storage_class) {
        case SignalDriverStorageClass::resolved_table:
        case SignalDriverStorageClass::single_owner:
        case SignalDriverStorageClass::disjoint_owner:
            break;
        default:
            return false;
        }
        if ((entry.storage_class == SignalDriverStorageClass::resolved_table
                && entry.owner_count != 0U)
            || (entry.storage_class == SignalDriverStorageClass::single_owner
                && (entry.owner_count != 1U || entry.writer_count != 1U
                    || structural_driver_class
                        != RegionDriverClass::single_whole))
            || (entry.storage_class == SignalDriverStorageClass::disjoint_owner
                && (entry.owner_count == 0U
                    || structural_driver_class
                        != RegionDriverClass::disjoint_partial))) {
            return false;
        }
        ProcessId previous_owner { };
        bool first_owner = true;
        for (std::size_t owner_index = entry.first_owner;
             owner_index < entry.first_owner + entry.owner_count;
             ++owner_index) {
            const auto& owner = inventory.owners[owner_index];
            if (owner.process >= inventory.process_count
                || (!first_owner && owner.process <= previous_owner)
                || owner.first_mask_word != next_mask_word
                || owner.first_mask_word > inventory.owner_mask_words.size()
                || owner.mask_word_count
                    > inventory.owner_mask_words.size()
                        - owner.first_mask_word
                || owner.mask_word_count != mask_words(entry.width)) {
                return false;
            }
            previous_owner = owner.process;
            first_owner = false;
            next_mask_word += owner.mask_word_count;
        }
    }
    return next_writer == inventory.writers.size()
        && next_owner == inventory.owners.size()
        && next_mask_word == inventory.owner_mask_words.size();
}

SignalDriverLayout SignalDriverLayout::build_from_inventory(
    const RegionGraph& graph,
    const std::span<const SignalId> component_signals,
    const SignalDriverInventory& driver_inventory,
    const std::span<const SignalId> certified_zero_delay_projected_slices,
    const std::span<const SignalId> certified_unresolved_owner_aliases)
{
    SignalDriverLayout result;
    const auto graph_signals = graph.signals();
    result.signal_ids_.assign(
        component_signals.begin(), component_signals.end());
    std::ranges::sort(result.signal_ids_);
    if (std::ranges::adjacent_find(result.signal_ids_)
        != result.signal_ids_.end()) {
        throw std::invalid_argument {
            "component driver layout contains duplicate signals"
        };
    }
    result.signals_.reserve(result.signal_ids_.size());

    for (const auto signal_id : result.signal_ids_) {
        if (signal_id >= graph_signals.size()
            || signal_id >= driver_inventory.signals.size()) {
            throw std::invalid_argument {
                "component driver layout references a missing signal"
            };
        }
        const auto& node = graph_signals[signal_id];
        const auto& inventory_signal = driver_inventory.signals[signal_id];
        const auto& descriptor = node.descriptor;
        const auto structural_driver_class
            = detail::classify_region_driver_class(node,
                detail::RegionDriverClassificationMode::persisted_structure);
        const auto writer_end = checked_add(
            inventory_signal.first_writer,
            inventory_signal.writer_count,
            "driver inventory writer span is too large");
        const auto owner_end = checked_add(
            inventory_signal.first_owner,
            inventory_signal.owner_count,
            "driver inventory owner span is too large");
        if (inventory_signal.width != descriptor.width
            || inventory_signal.resolution
                != static_cast<SignalDriverInventoryResolution>(
                    descriptor.resolution)
            || inventory_signal.value_kind
                != static_cast<SignalDriverInventoryValueKind>(
                    descriptor.value_kind)
            || inventory_signal.drivers
                != static_cast<SignalDriverInventoryDriverClass>(
                    structural_driver_class)
            || inventory_signal.implicit_driver != descriptor.implicit_driver
            || inventory_signal.event_variable != descriptor.event_variable
            || inventory_signal.writers_unknown != node.writers_unknown
            || inventory_signal.dynamic_fork_writers
                != node.dynamic_fork_writers
            || inventory_signal.partial_projected_transactions
                != node.partial_projected_transactions
            || inventory_signal.projected_slice_certificate_required
                != node.partial_projected_transactions
            || writer_end > driver_inventory.writers.size()
            || inventory_signal.writer_count != node.writers.size()
            || owner_end > driver_inventory.owners.size()) {
            throw std::invalid_argument {
                "driver inventory signal does not match the graph"
            };
        }
        switch (inventory_signal.storage_class) {
        case SignalDriverStorageClass::resolved_table:
        case SignalDriverStorageClass::single_owner:
        case SignalDriverStorageClass::disjoint_owner:
            break;
        default:
            throw std::invalid_argument {
                "driver inventory signal has an unknown storage class"
            };
        }
        for (std::size_t writer_index = 0U;
             writer_index < inventory_signal.writer_count; ++writer_index) {
            if (!same_inventory_writer(
                    driver_inventory.writers[
                        inventory_signal.first_writer + writer_index],
                    node.writers[writer_index])) {
                throw std::invalid_argument {
                    "driver inventory writer does not match the graph"
                };
            }
        }
        if (inventory_signal.storage_class
                == SignalDriverStorageClass::resolved_table
            && inventory_signal.owner_count != 0U) {
            throw std::invalid_argument {
                "resolved driver inventory signal has owners"
            };
        }
        if (inventory_signal.storage_class
                == SignalDriverStorageClass::single_owner
            && (inventory_signal.owner_count != 1U
                || inventory_signal.writer_count != 1U
                || structural_driver_class
                    != RegionDriverClass::single_whole)) {
            throw std::invalid_argument {
                "single-owner inventory proof has an invalid shape"
            };
        }
        if (inventory_signal.storage_class
                == SignalDriverStorageClass::disjoint_owner
            && (inventory_signal.owner_count == 0U
                || structural_driver_class
                    != RegionDriverClass::disjoint_partial)) {
            throw std::invalid_argument {
                "disjoint-owner inventory proof has an invalid shape"
            };
        }
        ProcessId prior_process { };
        bool first_process = true;
        for (std::size_t index = inventory_signal.first_owner;
             index < owner_end; ++index) {
            const auto& owner = driver_inventory.owners[index];
            const auto mask_end = checked_add(owner.first_mask_word,
                owner.mask_word_count,
                "driver inventory owner mask span is too large");
            if (owner.process >= driver_inventory.process_count
                || (!first_process && owner.process <= prior_process)
                || mask_end > driver_inventory.owner_mask_words.size()
                || owner.mask_word_count != word_count(descriptor.width)) {
                throw std::invalid_argument {
                    "driver inventory owner span is invalid"
                };
            }
            prior_process = owner.process;
            first_process = false;
            const auto mask = std::span<const std::uint64_t> {
                driver_inventory.owner_mask_words
            }.subspan(owner.first_mask_word, owner.mask_word_count);
            if (std::ranges::all_of(mask,
                    [](const std::uint64_t word) { return word == 0U; })
                || (!mask.empty()
                    && (mask.back() & ~valid_word_mask(
                            descriptor.width, mask.size() - 1U)) != 0U)) {
                throw std::invalid_argument {
                    "driver inventory owner mask is invalid"
                };
            }
        }
        auto layout = SignalDriverSignalLayout { };
        layout.width = descriptor.width;
        layout.value_kind = descriptor.value_kind;
        layout.first_value_word = result.value_word_count_;
        layout.word_count = word_count(descriptor.width);
        layout.first_logic9_word = descriptor.value_kind == ValueKind::logic9
            ? result.logic9_word_count_ : no_word;

        result.value_word_count_ = checked_add(result.value_word_count_,
            layout.word_count, "component signal planes are too large");
        if (descriptor.value_kind == ValueKind::logic9) {
            result.logic9_word_count_ = checked_add(
            result.logic9_word_count_, layout.word_count,
            "component Logic9 planes are too large");
        }

        const bool projected_slice_certified
            = !inventory_signal.projected_slice_certificate_required
            || (structural_driver_class
                == RegionDriverClass::disjoint_partial
                && std::ranges::find(certified_zero_delay_projected_slices,
                    signal_id)
                    != certified_zero_delay_projected_slices.end());
        const bool signal_shape_supported
            = inventory_signal.width != 0U
            && (descriptor.value_kind == ValueKind::logic4
                || descriptor.value_kind == ValueKind::logic9)
            && !descriptor.implicit_driver
            && !descriptor.external_driver
            && !descriptor.event_variable
            && !node.writers_unknown
            && !node.dynamic_fork_writers
            && graph.certificate_inventory().access_inventory_complete
            && projected_slice_certified;
        if (signal_shape_supported) {
            layout.storage_class = inventory_signal.storage_class;
        }

        layout.first_owner = result.owners_.size();
        if (layout.storage_class != SignalDriverStorageClass::resolved_table) {
            for (std::size_t index = inventory_signal.first_owner;
                 index < inventory_signal.first_owner
                     + inventory_signal.owner_count; ++index) {
                const auto& inventory_owner = driver_inventory.owners[index];
                const auto first_mask = inventory_owner.first_mask_word;
                const auto mask_count = inventory_owner.mask_word_count;
                const auto mask_end = first_mask + mask_count;
                const auto mask_begin = result.owner_mask_words_.size();
                result.owner_mask_words_.insert(
                    result.owner_mask_words_.end(),
                    driver_inventory.owner_mask_words.begin()
                        + static_cast<std::ptrdiff_t>(first_mask),
                    driver_inventory.owner_mask_words.begin()
                        + static_cast<std::ptrdiff_t>(mask_end));
                SignalDriverOwnerLayout owner_layout;
                owner_layout.process = inventory_owner.process;
                owner_layout.first_mask_word = mask_begin;
                owner_layout.aliases_stored
                    = std::ranges::find(
                        certified_unresolved_owner_aliases, signal_id)
                    != certified_unresolved_owner_aliases.end();
                if (owner_layout.aliases_stored) {
                    const auto& writer = node.writers.front();
                    const bool complete_single_owner
                        = inventory_signal.storage_class
                            == SignalDriverStorageClass::single_owner
                        && inventory_signal.owner_count == 1U
                        && inventory_signal.writer_count == 1U
                        && inventory_owner.process == writer.process
                        && writer.offset == 0U
                        && (writer.width == 0U
                            || writer.width == descriptor.width);
                    if (!complete_single_owner || descriptor.width == 0U
                        || (descriptor.value_kind != ValueKind::logic4
                            && descriptor.value_kind != ValueKind::logic9)
                        || descriptor.resolution != ResolutionKind::none) {
                        throw std::invalid_argument {
                            "stored-owner alias lacks an unresolved whole-owner proof"
                        };
                    }
                    owner_layout.first_value_word = no_word;
                    owner_layout.first_logic9_word = no_word;
                } else {
                    owner_layout.first_value_word
                        = result.owner_value_word_count_;
                    owner_layout.first_logic9_word
                        = descriptor.value_kind == ValueKind::logic9
                        ? result.owner_logic9_word_count_ : no_word;
                    result.owner_value_word_count_ = checked_add(
                        result.owner_value_word_count_, layout.word_count,
                        "component owner planes are too large");
                    if (descriptor.value_kind == ValueKind::logic9) {
                        result.owner_logic9_word_count_ = checked_add(
                            result.owner_logic9_word_count_, layout.word_count,
                            "component Logic9 owner planes are too large");
                    }
                }
                result.owners_.push_back(owner_layout);
            }
            layout.owner_count = result.owners_.size() - layout.first_owner;
            if (layout.owner_count != inventory_signal.owner_count) {
                throw std::invalid_argument {
                    "driver inventory owner span is inconsistent"
                };
            }
        }
        result.signals_.push_back(layout);
    }
    return result;
}

bool SignalDriverLayout::contains(const SignalId signal) const noexcept
{
    return std::ranges::binary_search(signal_ids_, signal);
}

std::size_t SignalDriverLayout::max_signal_word_count() const noexcept
{
    std::size_t result { };
    for (const auto& signal : signals_) {
        result = std::max(result, signal.word_count);
    }
    return result;
}

std::size_t SignalDriverLayout::signal_index(const SignalId signal) const
{
    const auto found = std::ranges::lower_bound(signal_ids_, signal);
    if (found == signal_ids_.end() || *found != signal) {
        throw std::out_of_range {
            "signal is outside the component value layout"
        };
    }
    return static_cast<std::size_t>(found - signal_ids_.begin());
}

const SignalDriverSignalLayout& SignalDriverLayout::signal(
    const SignalId signal) const
{
    return signals_[signal_index(signal)];
}

std::span<const SignalDriverOwnerLayout> SignalDriverLayout::owners(
    const SignalId signal) const
{
    const auto& entry = this->signal(signal);
    return std::span<const SignalDriverOwnerLayout> { owners_ }
        .subspan(entry.first_owner, entry.owner_count);
}

std::size_t SignalDriverLayout::owner_index(
    const SignalId signal, const ProcessId owner) const noexcept
{
    const auto signal_found = std::ranges::lower_bound(signal_ids_, signal);
    if (signal_found == signal_ids_.end() || *signal_found != signal) {
        return no_word;
    }
    const auto index
        = static_cast<std::size_t>(signal_found - signal_ids_.begin());
    const auto& signal_entry = signals_[index];
    const auto begin = owners_.begin()
        + static_cast<std::ptrdiff_t>(signal_entry.first_owner);
    const auto end = begin
        + static_cast<std::ptrdiff_t>(signal_entry.owner_count);
    const auto found = std::ranges::lower_bound(begin, end, owner,
        std::ranges::less { }, &SignalDriverOwnerLayout::process);
    if (found == end || found->process != owner) {
        return no_word;
    }
    return static_cast<std::size_t>(found - owners_.begin());
}

std::span<const std::uint64_t> SignalDriverLayout::owner_mask_words(
    const SignalId signal, const ProcessId owner) const noexcept
{
    const auto index = owner_index(signal, owner);
    if (index == no_word) {
        return { };
    }
    const auto& signal_entry = signals_[
        static_cast<std::size_t>(std::ranges::lower_bound(
            signal_ids_, signal) - signal_ids_.begin())];
    return std::span<const std::uint64_t> { owner_mask_words_ }.subspan(
        owners_[index].first_mask_word, signal_entry.word_count);
}

AuthoritativeSignalPlanes::AuthoritativeSignalPlanes(
    SignalDriverLayout layout,
    const PackedSlotBindingPolicy packed_slot_policy)
    : layout_(std::move(layout))
    , packed_slot_policy_(packed_slot_policy)
{
    const auto allocate = [](PlaneWords& planes,
                             const std::size_t values,
                             const std::size_t logic9) {
        planes.planes[0U].resize(values);
        planes.planes[1U].resize(values);
        planes.planes[2U].resize(logic9);
        planes.planes[3U].resize(logic9);
    };
    allocate(current_, layout_.value_word_count(),
        layout_.logic9_word_count());
    allocate(previous_, layout_.value_word_count(),
        layout_.logic9_word_count());
    allocate(stored_, layout_.value_word_count(),
        layout_.logic9_word_count());
    allocate(owner_values_, layout_.owner_value_word_count(),
        layout_.owner_logic9_word_count());
    if (packed_slot_policy_ == PackedSlotBindingPolicy::experimental_wide
        || packed_slot_policy_
            == PackedSlotBindingPolicy::experimental_wide_disjoint_owners) {
        const auto version = [](PlaneWords& words) {
            auto block = std::make_shared<PackedLogic4PlaneBlock>();
            block->planes = std::move(words.planes);
            words.cell = std::make_shared<PackedLogic4PlaneCell>(
                std::move(block));
        };
        version(current_);
        version(previous_);
        version(stored_);
        if (layout_.owner_value_word_count() != 0U) {
            version(owner_values_);
        }
        versioned_storage_ready_ = true;
    }
    if (versioned_storage_ready_) {
        mirror_mutation_scratch_.words.reserve(
            layout_.max_signal_word_count());
    }
    current_backings_.resize(layout_.signal_count());
    previous_backings_.resize(layout_.signal_count());
    stored_backings_.resize(layout_.signal_count());
    owner_backings_.resize(layout_.owner_count());
    packed_signal_slot_binding_tokens_.resize(layout_.signal_count());
    packed_owner_slot_binding_tokens_.resize(layout_.owner_count());
    if (layout_.signal_count()
        > std::numeric_limits<std::size_t>::max() / 3U) {
        throw std::length_error {
            "component packed slot count is too large"
        };
    }
    packed_slot_bindings_.reserve(checked_add(
        layout_.signal_count() * 3U, layout_.owner_count(),
        "component packed slot count is too large"));
    const auto make_backing = [](
        PlaneWords& planes,
        const std::size_t value_word,
        const std::size_t logic9_word,
        const std::uint32_t width,
        const ValueKind kind) {
        std::array<std::span<std::uint64_t>, 4U> backing_planes;
        const auto words = word_count(width);
        if (!planes.cell) {
            backing_planes[0U]
                = planes.planes[0U].span().subspan(value_word, words);
            backing_planes[1U]
                = planes.planes[1U].span().subspan(value_word, words);
        }
        if (kind == ValueKind::logic9) {
            if (!planes.cell) {
                backing_planes[2U]
                    = planes.planes[2U].span().subspan(logic9_word, words);
                backing_planes[3U]
                    = planes.planes[3U].span().subspan(logic9_word, words);
            }
        }
        return PackedLogic4PlaneBacking { width,
            kind == ValueKind::logic9, value_word, logic9_word,
            planes.cell, backing_planes };
    };
    for (std::size_t index = 0U;
         index < layout_.signals_.size(); ++index) {
        const auto& entry = layout_.signals_[index];
        if ((entry.value_kind != ValueKind::logic4
                && entry.value_kind != ValueKind::logic9)
            || entry.width == 0U
            || (entry.width > 64U && !versioned_storage_ready_)) {
            continue;
        }
        current_backings_[index] = make_backing(
            current_, entry.first_value_word, entry.first_logic9_word,
            entry.width, entry.value_kind);
        previous_backings_[index] = make_backing(
            previous_, entry.first_value_word, entry.first_logic9_word,
            entry.width, entry.value_kind);
        stored_backings_[index] = make_backing(
            stored_, entry.first_value_word, entry.first_logic9_word,
            entry.width, entry.value_kind);
    }
    for (const auto& entry : layout_.signals_) {
        if ((entry.value_kind != ValueKind::logic4
                && entry.value_kind != ValueKind::logic9)
            || entry.width == 0U
            || (entry.width > 64U && !versioned_storage_ready_)) {
            continue;
        }
        for (std::size_t owner = entry.first_owner;
             owner < entry.first_owner + entry.owner_count; ++owner) {
            if (layout_.owners_[owner].aliases_stored) {
                continue;
            }
            owner_backings_[owner] = make_backing(
                owner_values_, layout_.owners_[owner].first_value_word,
                layout_.owners_[owner].first_logic9_word,
                entry.width, entry.value_kind);
        }
    }
    signal_seeded_.resize(layout_.signal_count());
    owner_seeded_.resize(layout_.owners_.size());
    dirty_signals_.resize(layout_.signal_count());
}

bool AuthoritativeSignalPlanes::supports_packed_slot_binding(
    const SignalId signal) const noexcept
{
    const auto found
        = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return false;
    }
    const auto index
        = static_cast<std::size_t>(found - layout_.signal_ids_.begin());
    const auto& entry = layout_.signals_[index];
    const bool allow_disjoint_owners
        = packed_slot_policy_
            == PackedSlotBindingPolicy::experimental_wide_disjoint_owners;
    const bool wide_slots_enabled
        = packed_slot_policy_ == PackedSlotBindingPolicy::experimental_wide
        || allow_disjoint_owners;
    const bool single_owner
        = entry.storage_class == SignalDriverStorageClass::single_owner
        && entry.owner_count == 1U;
    const bool disjoint_owners
        = allow_disjoint_owners
        && entry.storage_class == SignalDriverStorageClass::disjoint_owner
        && entry.owner_count > 1U;
    if ((!single_owner && !disjoint_owners)
        || (entry.value_kind != ValueKind::logic4
            && entry.value_kind != ValueKind::logic9)
        || entry.width == 0U
        || (entry.width > 64U
            && (!versioned_storage_ready_ || !wide_slots_enabled))
        || signal_seeded_[index] == 0U) {
        return false;
    }

    if (single_owner) {
        const auto owner_index = entry.first_owner;
        if (owner_seeded_[owner_index] == 0U) {
            return false;
        }
        const auto& owner = layout_.owners_[owner_index];
        const auto mask = std::span<const std::uint64_t> {
            layout_.owner_mask_words_ }.subspan(
            owner.first_mask_word, entry.word_count);
        for (std::size_t word = 0U; word < mask.size(); ++word) {
            if (mask[word] != valid_word_mask(entry.width, word)) {
                return false;
            }
        }
        return true;
    }

    // A disjoint layout is directly representable only when every signal bit
    // has exactly one registered owner. Sparse or overlapping masks keep the
    // checked resolver and DriverTable route.
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        std::uint64_t assigned { };
        for (std::size_t owner_index = entry.first_owner;
             owner_index < entry.first_owner + entry.owner_count;
             ++owner_index) {
            if (owner_seeded_[owner_index] == 0U) {
                return false;
            }
            const auto& owner = layout_.owners_[owner_index];
            const auto masks = std::span<const std::uint64_t> {
                layout_.owner_mask_words_ }.subspan(
                owner.first_mask_word, entry.word_count);
            if ((assigned & masks[word]) != 0U) {
                return false;
            }
            assigned |= masks[word];
        }
        if (assigned != valid_word_mask(entry.width, word)) {
            return false;
        }
    }
    return true;
}

void AuthoritativeSignalPlanes::stage_packed_signal_slots(
    const SignalId signal,
    PackedLogic4& current,
    PackedLogic4& previous,
    PackedLogic4& stored)
{
    if (packed_slots_bound_ || !supports_packed_slot_binding(signal)) {
        throw std::invalid_argument {
            "signal layout does not support packed slot binding"
        };
    }
    const auto index = layout_.signal_index(signal);
    if (packed_signal_slot_binding_tokens_[index] != 0U) {
        throw std::invalid_argument {
            "packed signal slots were staged more than once"
        };
    }
    const auto& entry = layout_.signals_[index];
    const auto matches = [&entry](const PackedLogic4& value) {
        return value.width() == entry.width
            && valid_value_kind(entry.value_kind, value);
    };
    if (!matches(current) || !matches(previous) || !matches(stored)) {
        throw std::invalid_argument {
            "packed signal slot shape does not match its component planes"
        };
    }
    const auto first_binding_index = packed_slot_bindings_.size();
    packed_slot_bindings_.push_back({ signal, ProcessId { },
        PackedSlotBinding::Role::current, &current,
        &current_backings_[index] });
    packed_slot_bindings_.push_back({ signal, ProcessId { },
        PackedSlotBinding::Role::previous, &previous,
        &previous_backings_[index] });
    packed_slot_bindings_.push_back({ signal, ProcessId { },
        PackedSlotBinding::Role::stored, &stored,
        &stored_backings_[index] });
    packed_signal_slot_binding_tokens_[index] = first_binding_index + 1U;
}

void AuthoritativeSignalPlanes::stage_packed_owner_slot(
    const SignalId signal,
    const ProcessId owner,
    PackedLogic4& value)
{
    if (packed_slots_bound_ || !supports_packed_slot_binding(signal)) {
        throw std::invalid_argument {
            "owner layout does not support packed slot binding"
        };
    }
    const auto index = layout_.owner_index(signal, owner);
    if (index == no_word) {
        throw std::invalid_argument {
            "owner is outside packed component storage"
        };
    }
    if (layout_.owners_[index].aliases_stored) {
        throw std::invalid_argument {
            "stored-owner alias cannot bind a separate packed owner slot"
        };
    }
    if (packed_owner_slot_binding_tokens_[index] != 0U) {
        throw std::invalid_argument {
            "packed owner slot was staged more than once"
        };
    }
    const auto& entry = layout_.signals_[layout_.signal_index(signal)];
    if (value.width() != entry.width
        || !valid_value_kind(entry.value_kind, value)) {
        throw std::invalid_argument {
            "packed owner slot shape does not match its component planes"
        };
    }
    packed_slot_bindings_.push_back({ signal, owner,
        PackedSlotBinding::Role::owner, &value, &owner_backings_[index] });
    packed_owner_slot_binding_tokens_[index] = packed_slot_bindings_.size();
}

void AuthoritativeSignalPlanes::stage_packed_owner_stored_alias(
    const SignalId signal,
    const ProcessId owner)
{
    if (packed_slots_bound_ || !supports_packed_slot_binding(signal)) {
        throw std::invalid_argument {
            "stored-owner alias does not support packed slot binding"
        };
    }
    const auto index = layout_.owner_index(signal, owner);
    if (index == no_word || !layout_.owners_[index].aliases_stored) {
        throw std::invalid_argument {
            "owner is not certified as a stored-plane alias"
        };
    }
    const auto signal_index = layout_.signal_index(signal);
    if (packed_signal_slot_binding_tokens_[signal_index] == 0U) {
        throw std::logic_error {
            "stored-owner alias requires all three signal slots first"
        };
    }
    if (packed_owner_slot_binding_tokens_[index] != 0U) {
        throw std::invalid_argument {
            "packed owner alias was staged more than once"
        };
    }
    packed_owner_slot_binding_tokens_[index] = no_word;
}

bool AuthoritativeSignalPlanes::can_bind_packed_slots() const noexcept
{
    if (!valid_ || frontier_write_active_ || packed_slots_bound_
        || !versioned_storage_ready_ || packed_slot_bindings_.empty()
        || !generation_can_advance()) {
        return false;
    }
    return std::ranges::all_of(packed_slot_bindings_,
        [](const PackedSlotBinding& binding) {
            // This facade may own an older detached snapshot. Binding drops
            // that pin; a live facade still aliases public plane storage.
            if (binding.value == nullptr || binding.backing == nullptr
                || binding.value->has_live_plane_backing()
                || binding.backing->cell == nullptr
                || binding.backing->width == 0U
                || binding.backing->width
                    > std::numeric_limits<std::uint32_t>::max()
                || binding.value->width() != binding.backing->width
                || binding.value->is_logic9() != binding.backing->logic9) {
                return false;
            }
            const auto words = word_count(
                static_cast<std::uint32_t>(binding.backing->width));
            const auto& backing = *binding.backing;
            const auto block
                = backing.cell->current.load(std::memory_order_acquire);
            if (!block) {
                return false;
            }
            const auto span_fits = [words](
                const PackedLogic4PlaneStorage& plane,
                const std::size_t first_word) noexcept {
                return first_word <= plane.size()
                    && words <= plane.size() - first_word;
            };
            const bool logic9_plane2_present
                = span_fits(block->planes[2U], backing.first_logic9_word);
            const bool logic9_plane3_present
                = span_fits(block->planes[3U], backing.first_logic9_word);
            return span_fits(
                       block->planes[0U], backing.first_value_word)
                && span_fits(
                    block->planes[1U], backing.first_value_word)
                && (backing.logic9
                    ? (logic9_plane2_present && logic9_plane3_present)
                    : (!logic9_plane2_present && !logic9_plane3_present));
        });
}

std::size_t AuthoritativeSignalPlanes::bind_packed_slots() noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return 0U;
    }
    if (packed_slots_bound_) {
        return 0U;
    }
    if (!packed_slot_bindings_.empty() && !generation_can_advance()) {
        valid_ = false;
        return 0U;
    }
    for (const auto& binding : packed_slot_bindings_) {
        assert(binding.value != nullptr && binding.backing != nullptr);
        binding.value->bind_plane_backing(*binding.backing);
    }
    packed_slots_bound_ = !packed_slot_bindings_.empty();
    if (packed_slots_bound_) {
        advance_generation();
    }
    return packed_slot_bindings_.size();
}

std::size_t AuthoritativeSignalPlanes::unbind_packed_slots() noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return 0U;
    }
    if (!packed_slots_bound_) {
        return 0U;
    }
    if (!generation_can_advance()) {
        // Revocation must still detach every public value from sidecar-owned
        // descriptors before sidecar destruction, even if its revision has
        // saturated and it can no longer accept prepared mutations.
        valid_ = false;
    }
    for (const auto& binding : packed_slot_bindings_) {
        assert(binding.value != nullptr);
        binding.value->unbind_plane_backing();
    }
    packed_slots_bound_ = false;
    advance_generation();
    return packed_slot_bindings_.size();
}

bool AuthoritativeSignalPlanes::packed_signal_slots_bound(
    const SignalId signal) const noexcept
{
    if (!packed_slots_bound_) {
        return false;
    }
    const auto signal_found
        = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (signal_found == layout_.signal_ids_.end() || *signal_found != signal) {
        return false;
    }
    const auto signal_index = static_cast<std::size_t>(
        signal_found - layout_.signal_ids_.begin());
    if (!packed_signal_role_bindings_bound(signal, signal_index)) {
        return false;
    }
    const auto& entry = layout_.signals_[signal_index];
    if (!supports_packed_slot_binding(signal) || entry.owner_count == 0U) {
        return false;
    }
    for (std::size_t owner_index = entry.first_owner;
         owner_index < entry.first_owner + entry.owner_count;
         ++owner_index) {
        if (!packed_owner_slot_bound(
                signal, layout_.owners_[owner_index].process)) {
            return false;
        }
    }
    return true;
}

bool AuthoritativeSignalPlanes::packed_owner_slot_bound(
    const SignalId signal, const ProcessId owner) const noexcept
{
    if (!packed_slots_bound_) {
        return false;
    }
    const auto owner_index = layout_.owner_index(signal, owner);
    if (owner_index == no_word
        || packed_owner_slot_binding_tokens_[owner_index] == 0U) {
        return false;
    }
    const auto binding_token
        = packed_owner_slot_binding_tokens_[owner_index];
    if (layout_.owners_[owner_index].aliases_stored) {
        if (binding_token != no_word) {
            return false;
        }
        const auto signal_found
            = std::ranges::lower_bound(layout_.signal_ids_, signal);
        if (signal_found == layout_.signal_ids_.end()
            || *signal_found != signal) {
            return false;
        }
        const auto signal_index = static_cast<std::size_t>(
            signal_found - layout_.signal_ids_.begin());
        return packed_signal_role_bindings_bound(signal, signal_index);
    }
    if (binding_token == no_word) {
        return false;
    }
    const auto binding_index = binding_token - 1U;
    if (binding_index >= packed_slot_bindings_.size()) {
        return false;
    }
    const auto& binding = packed_slot_bindings_[binding_index];
    return binding.signal == signal && binding.owner == owner
        && binding.role == PackedSlotBinding::Role::owner
        && binding.value != nullptr
        && binding.backing == &owner_backings_[owner_index]
        && binding.value->has_plane_backing();
}

bool AuthoritativeSignalPlanes::packed_signal_role_bindings_bound(
    const SignalId signal, const std::size_t signal_index) const noexcept
{
    if (!packed_slots_bound_
        || signal_index >= packed_signal_slot_binding_tokens_.size()
        || signal_index >= layout_.signal_ids_.size()
        || layout_.signal_ids_[signal_index] != signal
        || signal_index >= current_backings_.size()
        || signal_index >= previous_backings_.size()
        || signal_index >= stored_backings_.size()) {
        return false;
    }
    const auto binding_token
        = packed_signal_slot_binding_tokens_[signal_index];
    if (binding_token == 0U) {
        return false;
    }
    const auto first_binding_index = binding_token - 1U;
    if (first_binding_index > packed_slot_bindings_.size()
        || packed_slot_bindings_.size() - first_binding_index < 3U) {
        return false;
    }
    const std::array<PackedSlotBinding::Role, 3U> roles {
        PackedSlotBinding::Role::current,
        PackedSlotBinding::Role::previous,
        PackedSlotBinding::Role::stored,
    };
    const std::array<const PackedLogic4PlaneBacking*, 3U> backings {
        &current_backings_[signal_index],
        &previous_backings_[signal_index],
        &stored_backings_[signal_index],
    };
    for (std::size_t role_index = 0U;
         role_index < roles.size(); ++role_index) {
        const auto& binding
            = packed_slot_bindings_[first_binding_index + role_index];
        if (binding.signal != signal
            || binding.role != roles[role_index]
            || binding.value == nullptr
            || binding.backing != backings[role_index]
            || !binding.value->has_plane_backing()) {
            return false;
        }
    }
    return true;
}

void AuthoritativeSignalPlanes::validate_value(
    const SignalId signal, const PackedLogic4& value) const
{
    const auto& entry = layout_.signal(signal);
    if (value.width() != entry.width
        || !valid_value_kind(entry.value_kind, value)
        || !canonical_logic9(value)) {
        throw std::invalid_argument {
            "authoritative signal value has an invalid width or value kind"
        };
    }
}

std::array<std::uint64_t, 4U> AuthoritativeSignalPlanes::load_word(
    const PlaneWords& planes,
    const std::size_t value_word,
    const std::size_t logic9_word,
    const ValueKind kind) const noexcept
{
    return { planes.plane(0U)[value_word],
        planes.plane(1U)[value_word],
        kind == ValueKind::logic9 ? planes.plane(2U)[logic9_word] : 0U,
        kind == ValueKind::logic9 ? planes.plane(3U)[logic9_word] : 0U };
}

bool AuthoritativeSignalPlanes::matches_value_plane(
    const PlaneWords& planes,
    const SignalId signal,
    const PackedLogic4& value) const noexcept
{
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return false;
    }
    const auto index
        = static_cast<std::size_t>(found - layout_.signal_ids_.begin());
    const auto& entry = layout_.signals_[index];
    if (value.width() != entry.width
        || !valid_value_kind(entry.value_kind, value)) {
        return false;
    }
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        const auto actual = packed_word(value, word);
        const auto expected = load_word(
            planes, entry.first_value_word + word,
            entry.value_kind == ValueKind::logic9
                ? entry.first_logic9_word + word : no_word,
            entry.value_kind);
        const auto valid = valid_word_mask(entry.width, word);
        for (std::size_t plane = 0U;
             plane < (entry.value_kind == ValueKind::logic9 ? 4U : 2U);
             ++plane) {
            if ((actual[plane] & valid) != expected[plane]) {
                return false;
            }
        }
    }
    return true;
}

void AuthoritativeSignalPlanes::store_word(PlaneWords& planes,
    const std::size_t value_word,
    const std::size_t logic9_word,
    const ValueKind kind,
    const std::array<std::uint64_t, 4U>& value) noexcept
{
    planes.plane(0U)[value_word] = value[0U];
    planes.plane(1U)[value_word] = value[1U];
    if (kind == ValueKind::logic9) {
        planes.plane(2U)[logic9_word] = value[2U];
        planes.plane(3U)[logic9_word] = value[3U];
    }
}

AuthoritativeSignalPlanes::PlaneWords&
AuthoritativeSignalPlanes::role_words(const PackedPlaneRole role) noexcept
{
    switch (role) {
    case PackedPlaneRole::current:
        return current_;
    case PackedPlaneRole::previous:
        return previous_;
    case PackedPlaneRole::stored:
        return stored_;
    case PackedPlaneRole::owner:
        return owner_values_;
    }
    std::terminate();
}

const AuthoritativeSignalPlanes::PlaneWords&
AuthoritativeSignalPlanes::role_words(
    const PackedPlaneRole role) const noexcept
{
    switch (role) {
    case PackedPlaneRole::current:
        return current_;
    case PackedPlaneRole::previous:
        return previous_;
    case PackedPlaneRole::stored:
        return stored_;
    case PackedPlaneRole::owner:
        return owner_values_;
    }
    std::terminate();
}

std::shared_ptr<PackedLogic4PlaneBlock>
AuthoritativeSignalPlanes::prepare_replacement(
    const PlaneWords& words) const
{
    if (!versioned_storage_ready_ || !words.cell) {
        return { };
    }
    const auto source = words.cell->current.load(std::memory_order_acquire);
    if (!source) {
        throw std::logic_error {
            "versioned component role lost its plane block"
        };
    }
    for (auto& retired : words.retired_blocks) {
        // Only the cache may own this block. In particular, a reader which
        // loaded the former cell but has not acquired its pin still owns a
        // shared pointer and prevents reuse here. Writers are serialized.
        if (!retired || retired.use_count() != 1
            || retired->has_read_pins() || retired->write_locked()) {
            continue;
        }
        bool same_shape = true;
        for (std::size_t plane = 0U; plane < source->planes.size(); ++plane) {
            same_shape = same_shape
                && retired->planes[plane].size() == source->planes[plane].size();
        }
        if (!same_shape) {
            continue;
        }
        auto replacement = std::move(retired);
        for (std::size_t plane = 0U; plane < source->planes.size(); ++plane) {
            std::ranges::copy(source->planes[plane],
                replacement->planes[plane].begin());
        }
        return replacement;
    }
    return PackedLogic4PlaneBlock::clone(*source);
}

void AuthoritativeSignalPlanes::prepare_replacements(
    PreparedMutation& mutation) const
{
    if (!versioned_storage_ready_) {
        return;
    }

    const auto prepare_if_pinned = [this, &mutation](
                                       const PlaneWords& words,
                                       const PackedPlaneRole role) {
        auto& replacement = mutation.replacements[
            static_cast<std::size_t>(role)];
        if (!words.cell) {
            replacement.reset();
            return;
        }
        const auto block
            = words.cell->current.load(std::memory_order_acquire);
        if (!block || !block->has_read_pins()) {
            replacement.reset();
            return;
        }
        if (replacement
            && mutation.prepared_generation == generation_) {
            bool same_shape = replacement->planes.size()
                == block->planes.size();
            for (std::size_t plane = 0U;
                same_shape && plane < block->planes.size(); ++plane) {
                same_shape = replacement->planes[plane].size()
                    == block->planes[plane].size();
            }
            if (same_shape) {
                // A later mirror adjustment changes the values to apply, not
                // the source generation or the detached block's shape. Keep
                // this prepared block; publication applies the final words.
                return;
            }
        }
        replacement = prepare_replacement(words);
    };

    if (mutation.any_current_changed) {
        prepare_if_pinned(previous_, PackedPlaneRole::previous);
        prepare_if_pinned(current_, PackedPlaneRole::current);
    } else {
        mutation.replacements[static_cast<std::size_t>(
            PackedPlaneRole::previous)].reset();
        mutation.replacements[static_cast<std::size_t>(
            PackedPlaneRole::current)].reset();
    }
    if (mutation.any_stored_changed) {
        prepare_if_pinned(stored_, PackedPlaneRole::stored);
    } else {
        mutation.replacements[static_cast<std::size_t>(
            PackedPlaneRole::stored)].reset();
    }
}

void AuthoritativeSignalPlanes::apply_replacement_updates(
    PackedLogic4PlaneBlock& block,
    const std::span<const PreparedMutation> mutations,
    const PackedPlaneRole role) const noexcept
{
    for (const auto& mutation : mutations) {
        for (const auto& word : mutation.words) {
            const std::array<std::uint64_t, 4U>* value { };
            std::size_t value_word { };
            std::size_t logic9_word { };
            switch (role) {
            case PackedPlaneRole::previous:
                if (mutation.any_current_changed) {
                    value = &word.old_current;
                    value_word = word.signal_word;
                    logic9_word = word.logic9_word;
                }
                break;
            case PackedPlaneRole::current:
                if (mutation.any_current_changed) {
                    value = &word.new_current;
                    value_word = word.signal_word;
                    logic9_word = word.logic9_word;
                }
                break;
            case PackedPlaneRole::stored:
                if (mutation.any_stored_changed) {
                    value = &word.new_stored;
                    value_word = word.signal_word;
                    logic9_word = word.logic9_word;
                }
                break;
            case PackedPlaneRole::owner:
                if (mutation.any_owner_changed) {
                    value = &word.new_owner;
                    value_word = word.owner_word;
                    logic9_word = word.owner_logic9_word;
                }
                break;
            }
            if (value == nullptr) {
                continue;
            }
            block.planes[0U][value_word] = (*value)[0U];
            block.planes[1U][value_word] = (*value)[1U];
            if (logic9_word != no_word) {
                block.planes[2U][logic9_word] = (*value)[2U];
                block.planes[3U][logic9_word] = (*value)[3U];
            }
        }
    }
}

void AuthoritativeSignalPlanes::install_replacement(
    PlaneWords& words,
    std::shared_ptr<PackedLogic4PlaneBlock> replacement) noexcept
{
    assert(words.cell != nullptr && replacement != nullptr);
    auto retired = words.cell->current.exchange(std::move(replacement),
        std::memory_order_acq_rel);
    for (auto& cached : words.retired_blocks) {
        if (!cached || (cached.use_count() == 1
                           && !cached->has_read_pins()
                           && !cached->write_locked())) {
            cached = std::move(retired);
            break;
        }
    }
}

bool AuthoritativeSignalPlanes::prepare_writable_planes(
    const std::span<PreparedMutation> mutations,
    WritablePlaneTransaction& transaction) noexcept
{
    transaction = { };
    if (!versioned_storage_ready_) {
        return true;
    }
    std::array<bool, 4U> changed { };
    for (const auto& mutation : mutations) {
        changed[static_cast<std::size_t>(PackedPlaneRole::previous)]
            = changed[static_cast<std::size_t>(PackedPlaneRole::previous)]
                || mutation.any_current_changed;
        changed[static_cast<std::size_t>(PackedPlaneRole::current)]
            = changed[static_cast<std::size_t>(PackedPlaneRole::current)]
                || mutation.any_current_changed;
        changed[static_cast<std::size_t>(PackedPlaneRole::stored)]
            = changed[static_cast<std::size_t>(PackedPlaneRole::stored)]
                || mutation.any_stored_changed;
        changed[static_cast<std::size_t>(PackedPlaneRole::owner)]
            = changed[static_cast<std::size_t>(PackedPlaneRole::owner)]
                || mutation.any_owner_changed;
    }

    for (std::size_t role_index = 0U; role_index < changed.size();
         ++role_index) {
        if (!changed[role_index]) {
            continue;
        }
        const auto role = static_cast<PackedPlaneRole>(role_index);
        auto& words = role_words(role);
        if (!words.cell) {
            finish_writable_planes(transaction);
            return false;
        }
        for (;;) {
            auto block = words.cell->current.load(std::memory_order_acquire);
            if (!block) {
                finish_writable_planes(transaction);
                return false;
            }
            if (block->try_begin_write()) {
                if (words.cell->current.load(std::memory_order_acquire)
                    != block) {
                    block->end_write();
                    continue;
                }
                transaction.locked[transaction.lock_count] = block.get();
                ++transaction.lock_count;
                break;
            }
            if (block->write_locked()) {
                continue;
            }

            const auto prepared = std::ranges::find_if(mutations,
                [role_index](const PreparedMutation& mutation) {
                    return static_cast<bool>(
                        mutation.replacements[role_index]);
                });
            if (prepared == mutations.end()) {
                finish_writable_planes(transaction);
                return false;
            }
            auto replacement = prepared->replacements[role_index];
            apply_replacement_updates(*replacement, mutations, role);
            transaction.replacements[role_index] = std::move(replacement);
            break;
        }
    }
    return true;
}

bool AuthoritativeSignalPlanes::begin_prepared_publication(
    PreparedMutation& mutation)
{
    if (!valid_ || frontier_write_active_ || !versioned_storage_ready_
        || !packed_slots_bound_
        || mutation.preflighted || mutation.owner_group_row
        || mutation.owner_group_preflighted) {
        return false;
    }
    const std::span<const PreparedMutation> mutations {
        &mutation, 1U
    };
    if (!prepared_generation_is_current(mutations)
        || (mutation.any_state_changed && !generation_can_advance())) {
        return false;
    }
    return acquire_prepared_planes(mutation);
}

bool AuthoritativeSignalPlanes::acquire_prepared_planes(
    PreparedMutation& mutation)
{
    const std::array<bool, 4U> changed {
        mutation.any_current_changed,
        mutation.any_current_changed,
        mutation.any_stored_changed,
        mutation.any_owner_changed,
    };
    const std::span<const PreparedMutation> mutations {
        &mutation, 1U
    };
    try {
        for (std::size_t role_index = 0U; role_index < changed.size();
             ++role_index) {
            if (!changed[role_index]) {
                continue;
            }
            const auto role = static_cast<PackedPlaneRole>(role_index);
            auto& words = role_words(role);
            if (!words.cell) {
                cancel_prepared_publication(mutation);
                return false;
            }
            auto block
                = words.cell->current.load(std::memory_order_acquire);
            if (!block) {
                cancel_prepared_publication(mutation);
                return false;
            }
            auto& replacement = mutation.replacements[role_index];
            if (!replacement && !block->try_begin_write()) {
                if (block->write_locked()) {
                    cancel_prepared_publication(mutation);
                    return false;
                }
                replacement = prepare_replacement(words);
                if (!replacement) {
                    cancel_prepared_publication(mutation);
                    return false;
                }
            } else if (!replacement) {
                if (words.cell->current.load(std::memory_order_acquire)
                    != block) {
                    block->end_write();
                    cancel_prepared_publication(mutation);
                    return false;
                }
                mutation.preflight_locked[mutation.preflight_lock_count]
                    = block.get();
                ++mutation.preflight_lock_count;
            }
        }

        for (std::size_t role_index = 0U; role_index < changed.size();
             ++role_index) {
            if (!changed[role_index]
                || !mutation.replacements[role_index]) {
                continue;
            }
            apply_replacement_updates(
                *mutation.replacements[role_index], mutations,
                static_cast<PackedPlaneRole>(role_index));
        }
    } catch (...) {
        cancel_prepared_publication(mutation);
        throw;
    }
    mutation.preflighted = true;
    return true;
}

void AuthoritativeSignalPlanes::cancel_prepared_publication(
    PreparedMutation& mutation) noexcept
{
    while (mutation.preflight_lock_count != 0U) {
        --mutation.preflight_lock_count;
        mutation.preflight_locked[mutation.preflight_lock_count]->end_write();
        mutation.preflight_locked[mutation.preflight_lock_count] = nullptr;
    }
    mutation.preflighted = false;
    mutation.owner_group_preflighted = false;
    for (auto& replacement : mutation.replacements) {
        replacement.reset();
    }
}

bool AuthoritativeSignalPlanes::prepared_owner_group_is_valid(
    const std::span<const PreparedMutation> mutations) const noexcept
{
    if (mutations.size() < 2U || !valid_ || frontier_write_active_
        || !versioned_storage_ready_ || !packed_slots_bound_) {
        return false;
    }
    const auto& first = mutations.front();
    if (first.signal_index >= layout_.signals_.size()
        || layout_.signal_ids_[first.signal_index] != first.signal
        || std::ranges::any_of(mutations,
            [](const PreparedMutation& mutation) {
                return !mutation.owner_group_row;
            })) {
        return false;
    }
    const auto& signal = layout_.signals_[first.signal_index];
    if (signal.word_count == 0U
        || first.words.size() != signal.word_count) {
        return false;
    }
    const auto owners = layout_.owners(first.signal);
    for (std::size_t row_index = 0U;
         row_index < mutations.size(); ++row_index) {
        const auto& row = mutations[row_index];
        if (row.signal != first.signal
            || row.signal_index != first.signal_index
            || !row.has_owner || row.owner_is_stored_alias
            || row.prepared_generation != first.prepared_generation
            || row.words.size() != signal.word_count
            || row.owner_index < signal.first_owner
            || row.owner_index - signal.first_owner >= signal.owner_count) {
            return false;
        }
        const auto& owner = layout_.owners_[row.owner_index];
        if (owner.aliases_stored
            || owner_seeded_[owner_flat_index(row.owner_index)] == 0U
            || std::ranges::find(owners, owner.process,
                    &SignalDriverOwnerLayout::process)
                == owners.end()) {
            return false;
        }
        if (row_index != 0U
            && (row.any_current_changed
                    != first.any_current_changed
                || row.any_stored_changed != first.any_stored_changed)) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < row_index; ++earlier) {
            if (mutations[earlier].owner_index == row.owner_index) {
                return false;
            }
        }
        for (std::size_t word_index = 0U;
             word_index < signal.word_count; ++word_index) {
            const auto& word = row.words[word_index];
            const auto& first_word = first.words[word_index];
            if (word.signal_word != first_word.signal_word
                || word.logic9_word != first_word.logic9_word
                || word.old_previous != first_word.old_previous
                || word.old_current != first_word.old_current
                || word.old_stored != first_word.old_stored
                || word.new_current != first_word.new_current
                || word.new_stored != first_word.new_stored) {
                return false;
            }
        }
    }
    return true;
}

bool AuthoritativeSignalPlanes::begin_prepared_owner_group_publication(
    const std::span<PreparedMutation> mutations) noexcept
{
    if (!prepared_owner_group_is_valid(mutations)
        || std::ranges::any_of(mutations,
            [](const PreparedMutation& mutation) {
                return mutation.preflighted
                    || mutation.owner_group_preflighted
                    || mutation.preflight_lock_count != 0U;
            })
        || !prepared_generation_is_current(mutations)
        || (std::ranges::any_of(mutations,
                [](const PreparedMutation& mutation) {
                    return mutation.any_state_changed;
                })
            && !generation_can_advance())) {
        return false;
    }

    auto& anchor = mutations.front();
    const auto clear_replacements = [mutations]() noexcept {
        for (auto& mutation : mutations) {
            for (auto& replacement : mutation.replacements) {
                replacement.reset();
            }
        }
    };
    for (auto& mutation : mutations) {
        for (auto& replacement : mutation.replacements) {
            replacement.reset();
        }
    }
    try {
        // Group rows deliberately carry no per-owner COW copies. Prepare one
        // replacement for each changed role, then apply every owner row to
        // that shared replacement during whole-span preflight.
        prepare_replacements(anchor);
        const bool owner_changed = std::ranges::any_of(mutations,
            [](const PreparedMutation& mutation) {
                return mutation.any_owner_changed;
            });
        if (versioned_storage_ready_ && owner_changed
            && owner_values_.cell) {
            const auto block
                = owner_values_.cell->current.load(
                    std::memory_order_acquire);
            if (block && block->has_read_pins()) {
                auto replacement = prepare_replacement(owner_values_);
                if (!replacement) {
                    clear_replacements();
                    return false;
                }
                anchor.replacements[static_cast<std::size_t>(
                    PackedPlaneRole::owner)] = std::move(replacement);
            }
        }
    } catch (const std::bad_alloc&) {
        clear_replacements();
        return false;
    }

    WritablePlaneTransaction transaction;
    if (!prepare_writable_planes(mutations, transaction)) {
        clear_replacements();
        return false;
    }
    anchor.preflight_locked = transaction.locked;
    anchor.preflight_lock_count = transaction.lock_count;
    anchor.replacements = std::move(transaction.replacements);
    anchor.preflighted = true;
    anchor.owner_group_preflighted = true;
    return true;
}

void AuthoritativeSignalPlanes::cancel_prepared_owner_group_publication(
    const std::span<PreparedMutation> mutations) noexcept
{
    for (auto& mutation : mutations) {
        cancel_prepared_publication(mutation);
        mutation.owner_group_row = false;
        mutation.owner_group_preflighted = false;
    }
}

bool AuthoritativeSignalPlanes::publish_prepared_owner_group(
    const std::span<PreparedMutation> mutations) noexcept
{
    if (!prepared_owner_group_is_valid(mutations)
        || !mutations.front().preflighted
        || !mutations.front().owner_group_preflighted
        || std::ranges::any_of(mutations.subspan(1U),
            [](const PreparedMutation& mutation) {
                return mutation.preflighted
                    || mutation.owner_group_preflighted
                    || mutation.preflight_lock_count != 0U;
            })
        || !prepared_generation_is_current(mutations)) {
        cancel_prepared_owner_group_publication(mutations);
        return false;
    }
    const bool changes_state = std::ranges::any_of(mutations,
        [](const PreparedMutation& mutation) {
            return mutation.any_state_changed;
        });
    if (changes_state && !generation_can_advance()) {
        cancel_prepared_owner_group_publication(mutations);
        valid_ = false;
        return false;
    }

    auto& anchor = mutations.front();
    WritablePlaneTransaction transaction;
    transaction.locked = anchor.preflight_locked;
    transaction.lock_count = anchor.preflight_lock_count;
    transaction.replacements = std::move(anchor.replacements);
    anchor.preflight_locked = { };
    anchor.preflight_lock_count = 0U;
    anchor.preflighted = false;
    anchor.owner_group_preflighted = false;

    const auto install_role = [this, &transaction](
                                  const PackedPlaneRole role) {
        auto& replacement = transaction.replacements[
            static_cast<std::size_t>(role)];
        if (replacement) {
            install_replacement(role_words(role), std::move(replacement));
            return true;
        }
        return false;
    };
    const auto write_word = [this](const PackedPlaneRole role,
                              const PreparedMutation& mutation,
                              const PreparedWord& word) {
        const std::array<std::uint64_t, 4U>* value { };
        auto value_word = word.signal_word;
        auto logic9_word = word.logic9_word;
        switch (role) {
        case PackedPlaneRole::previous:
            if (mutation.any_current_changed) {
                value = &word.old_current;
            }
            break;
        case PackedPlaneRole::current:
            if (mutation.any_current_changed) {
                value = &word.new_current;
            }
            break;
        case PackedPlaneRole::stored:
            if (mutation.any_stored_changed) {
                value = &word.new_stored;
            }
            break;
        case PackedPlaneRole::owner:
            if (mutation.any_owner_changed) {
                value = &word.new_owner;
                value_word = word.owner_word;
                logic9_word = word.owner_logic9_word;
            }
            break;
        }
        if (value != nullptr) {
            const auto& signal = layout_.signals_[mutation.signal_index];
            store_word(role_words(role), value_word, logic9_word,
                signal.value_kind, *value);
        }
    };

    const std::array roles {
        PackedPlaneRole::previous,
        PackedPlaneRole::current,
        PackedPlaneRole::stored,
        PackedPlaneRole::owner,
    };
    for (const auto role : roles) {
        if (install_role(role)) {
            continue;
        }
        if (role == PackedPlaneRole::owner) {
            for (const auto& mutation : mutations) {
                for (const auto& word : mutation.words) {
                    write_word(role, mutation, word);
                }
            }
        } else {
            const auto& mutation = mutations.front();
            for (const auto& word : mutation.words) {
                write_word(role, mutation, word);
            }
        }
    }

    dirty_signals_[anchor.signal_index]
        = static_cast<std::uint8_t>(
            dirty_signals_[anchor.signal_index] != 0U || changes_state);
    finish_writable_planes(transaction);
    if (changes_state) {
        advance_generation();
    }
    for (auto& mutation : mutations) {
        mutation.owner_group_row = false;
        mutation.owner_group_preflighted = false;
    }
    return valid_;
}

void AuthoritativeSignalPlanes::finish_writable_planes(
    WritablePlaneTransaction& transaction) noexcept
{
    while (transaction.lock_count != 0U) {
        --transaction.lock_count;
        transaction.locked[transaction.lock_count]->end_write();
        transaction.locked[transaction.lock_count] = nullptr;
    }
}

void AuthoritativeSignalPlanes::seed_plane(PlaneWords& planes,
    const SignalDriverSignalLayout& signal,
    const PackedLogic4& value)
{
    for (std::size_t word = 0U; word < signal.word_count; ++word) {
        auto packed = packed_word(value, word);
        const auto mask = valid_word_mask(signal.width, word);
        for (auto& plane : packed) {
            plane &= mask;
        }
        store_word(planes, signal.first_value_word + word,
            signal.value_kind == ValueKind::logic9
                ? signal.first_logic9_word + word : no_word,
            signal.value_kind, packed);
    }
}

void AuthoritativeSignalPlanes::seed_signal(
    const SignalId signal,
    const PackedLogic4& current,
    const PackedLogic4& previous,
    const PackedLogic4& stored)
{
    if (frontier_write_active_) {
        throw std::logic_error {
            "cannot seed a signal while a frontier lease is active"
        };
    }
    const auto index = layout_.signal_index(signal);
    const auto& entry = layout_.signals_[index];
    if (signal_seeded_[index] != 0U) {
        throw std::logic_error {
            "authoritative signal was seeded more than once"
        };
    }
    validate_value(signal, current);
    validate_value(signal, previous);
    validate_value(signal, stored);
    if (!generation_can_advance()) {
        valid_ = false;
        throw std::overflow_error {
            "authoritative sidecar generation is exhausted"
        };
    }
    seed_plane(current_, entry, current);
    seed_plane(previous_, entry, previous);
    seed_plane(stored_, entry, stored);
    signal_seeded_[index] = 1U;
    advance_generation();
}

std::size_t AuthoritativeSignalPlanes::owner_index(
    const SignalId signal, const ProcessId owner) const
{
    const auto index = layout_.owner_index(signal, owner);
    if (index == no_word) {
        throw std::invalid_argument {
            "process is not a direct owner in the component layout"
        };
    }
    return index;
}

std::size_t AuthoritativeSignalPlanes::owner_flat_index(
    const std::size_t owner_index) const noexcept
{
    return owner_index;
}

void AuthoritativeSignalPlanes::seed_owner(
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& value)
{
    if (frontier_write_active_) {
        throw std::logic_error {
            "cannot seed an owner while a frontier lease is active"
        };
    }
    const auto owner_slot = owner_index(signal, owner);
    const auto signal_slot = layout_.signal_index(signal);
    const auto& signal_entry = layout_.signals_[signal_slot];
    const auto& owner_entry = layout_.owners_[owner_slot];
    if (signal_seeded_[signal_slot] == 0U) {
        throw std::logic_error {
            "owner value was seeded before its signal"
        };
    }
    if (owner_seeded_[owner_flat_index(owner_slot)] != 0U) {
        throw std::logic_error {
            "authoritative owner was seeded more than once"
        };
    }
    validate_value(signal, value);
    if (!generation_can_advance()) {
        valid_ = false;
        throw std::overflow_error {
            "authoritative sidecar generation is exhausted"
        };
    }
    const auto masks = layout_.owner_mask_words(signal, owner);
    if (owner_entry.aliases_stored) {
        if (!matches_value_plane(stored_, signal, value)) {
            throw std::invalid_argument {
                "stored-owner alias does not match its stored signal"
            };
        }
        owner_seeded_[owner_flat_index(owner_slot)] = 1U;
        advance_generation();
        return;
    }
    for (std::size_t word = 0U; word < signal_entry.word_count; ++word) {
        auto packed = packed_word(value, word);
        const auto valid = valid_word_mask(signal_entry.width, word);
        const auto outside = valid & ~masks[word];
        if (signal_entry.value_kind == ValueKind::logic4) {
            if ((packed[0U] & outside) != 0U
                || (packed[1U] & outside) != outside) {
                throw std::invalid_argument {
                    "direct owner has raw values outside its proven mask"
                };
            }
        } else if ((packed[0U] & outside) != 0U
            || (packed[1U] & outside) != 0U
            || (packed[2U] & outside) != outside
            || (packed[3U] & outside) != 0U) {
            throw std::invalid_argument {
                "Logic9 owner has raw values outside its proven mask"
            };
        }
    }
    // Do not alter any owner word until the full-width record has passed its
    // mask validation. A bad high word must not leave a partially seeded raw
    // owner image behind while the owner remains marked unseeded.
    for (std::size_t word = 0U; word < signal_entry.word_count; ++word) {
        auto packed = packed_word(value, word);
        const auto valid = valid_word_mask(signal_entry.width, word);
        for (auto& plane : packed) {
            plane &= valid;
        }
        store_word(owner_values_, owner_entry.first_value_word + word,
            signal_entry.value_kind == ValueKind::logic9
                ? owner_entry.first_logic9_word + word : no_word,
            signal_entry.value_kind, packed);
    }
    owner_seeded_[owner_flat_index(owner_slot)] = 1U;
    advance_generation();
}

void AuthoritativeSignalPlanes::advance_generation() noexcept
{
    if (!generation_can_advance()) {
        valid_ = false;
        return;
    }
    ++generation_;
}

bool AuthoritativeSignalPlanes::prepared_generation_is_current(
    const std::span<const PreparedMutation> mutations) noexcept
{
    if (mutations.empty()) {
        return true;
    }
    const auto expected = mutations.front().prepared_generation;
    if (expected == generation_
        && std::ranges::all_of(mutations,
            [expected](const PreparedMutation& mutation) {
                return mutation.prepared_generation == expected;
            })) {
        return true;
    }
    valid_ = false;
    return false;
}

void AuthoritativeSignalPlanes::mirror_stored(
    const SignalId signal, const PackedLogic4& stored) noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return;
    }
    if (!valid_) {
        return;
    }
    if (!generation_can_advance()) {
        valid_ = false;
        return;
    }
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return;
    }
    const auto index = static_cast<std::size_t>(found - layout_.signal_ids_.begin());
    const bool bound = packed_slots_bound_
        && packed_signal_slot_binding_tokens_[index] != 0U;
    assert(signal_seeded_[index] != 0U);
    if (versioned_storage_ready_) {
        try {
            auto& mutation = mirror_mutation_scratch_;
            prepare_value_change_from_roles_into(
                mutation, signal, nullptr, &stored);
            mutation.any_state_changed
                = mutation.any_state_changed || bound;
            publish(std::move(mutation));
        } catch (...) {
            cancel_prepared_publication(mirror_mutation_scratch_);
            valid_ = false;
        }
        return;
    }
    const auto& entry = layout_.signals_[index];
    assert(stored.width() == entry.width);
    assert(valid_value_kind(entry.value_kind, stored));
    if (!canonical_logic9(stored)) {
        valid_ = false;
        return;
    }
    bool changed { };
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        auto next = packed_word(stored, word);
        const auto valid = valid_word_mask(entry.width, word);
        for (auto& plane : next) {
            plane &= valid;
        }
        const auto logic9_word = entry.value_kind == ValueKind::logic9
            ? entry.first_logic9_word + word : no_word;
        const auto old = load_word(stored_, entry.first_value_word + word,
            logic9_word, entry.value_kind);
        changed = changed || old != next;
        store_word(stored_, entry.first_value_word + word, logic9_word,
            entry.value_kind, next);
    }
    dirty_signals_[index] = static_cast<std::uint8_t>(
        dirty_signals_[index] != 0U || changed || bound);
    advance_generation();
}

void AuthoritativeSignalPlanes::mirror_visible(
    const SignalId signal,
    const PackedLogic4& previous,
    const PackedLogic4& current) noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return;
    }
    if (!valid_) {
        return;
    }
    if (!generation_can_advance()) {
        valid_ = false;
        return;
    }
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return;
    }
    const auto index = static_cast<std::size_t>(found - layout_.signal_ids_.begin());
    const bool bound = packed_slots_bound_
        && packed_signal_slot_binding_tokens_[index] != 0U;
    assert(signal_seeded_[index] != 0U);
    if (versioned_storage_ready_) {
        try {
            validate_value(signal, previous);
            auto& mutation = mirror_mutation_scratch_;
            prepare_value_change_from_roles_into(
                mutation, signal, &current, nullptr);
            mutation.any_current_changed = true;
            // This mirror writes the previous plane even when current and
            // stored are unchanged. Treat that role update as state so a
            // prepared mutation from before the mirror cannot later replace
            // LAST with a stale value.
            mutation.any_state_changed = true;
            for (std::size_t word = 0U;
                word < mutation.words.size(); ++word) {
                auto old_current = packed_word(previous, word);
                const auto valid = valid_word_mask(
                    layout_.signals_[index].width, word);
                for (auto& plane : old_current) {
                    plane &= valid;
                }
                mutation.words[word].old_current = old_current;
            }
            prepare_replacements(mutation);
            publish(std::move(mutation));
        } catch (...) {
            cancel_prepared_publication(mirror_mutation_scratch_);
            valid_ = false;
        }
        return;
    }
    const auto& entry = layout_.signals_[index];
    assert(previous.width() == entry.width && current.width() == entry.width);
    assert(valid_value_kind(entry.value_kind, previous));
    assert(valid_value_kind(entry.value_kind, current));
    if (!canonical_logic9(previous) || !canonical_logic9(current)) {
        valid_ = false;
        return;
    }
    bool changed { };
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        auto old_value = packed_word(previous, word);
        auto next_value = packed_word(current, word);
        const auto valid = valid_word_mask(entry.width, word);
        for (auto& plane : old_value) {
            plane &= valid;
        }
        for (auto& plane : next_value) {
            plane &= valid;
        }
        const auto logic9_word = entry.value_kind == ValueKind::logic9
            ? entry.first_logic9_word + word : no_word;
        const auto plane_word = entry.first_value_word + word;
        const auto old_current = load_word(
            current_, plane_word, logic9_word, entry.value_kind);
        changed = changed || old_current != next_value;
        store_word(previous_, plane_word, logic9_word,
            entry.value_kind, old_value);
        store_word(current_, plane_word, logic9_word,
            entry.value_kind, next_value);
    }
    dirty_signals_[index] = static_cast<std::uint8_t>(
        dirty_signals_[index] != 0U || changed || bound);
    advance_generation();
}

void AuthoritativeSignalPlanes::mirror_owner(
    const SignalId signal, const ProcessId owner,
    const PackedLogic4& value) noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return;
    }
    if (!valid_) {
        return;
    }
    if (!generation_can_advance()) {
        valid_ = false;
        return;
    }
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return;
    }
    const auto signal_index
        = static_cast<std::size_t>(found - layout_.signal_ids_.begin());
    const auto owner_index = layout_.owner_index(signal, owner);
    if (owner_index == no_word) {
        return;
    }
    if (layout_.owners_[owner_index].aliases_stored) {
        if (owner_seeded_[owner_flat_index(owner_index)] == 0U
            || !matches_value_plane(stored_, signal, value)) {
            valid_ = false;
        }
        return;
    }
    const bool bound = packed_slots_bound_
        && packed_owner_slot_binding_tokens_[owner_index] != 0U;
    assert(signal_seeded_[signal_index] != 0U);
    assert(owner_seeded_[owner_flat_index(owner_index)] != 0U);
    if (versioned_storage_ready_) {
        try {
            auto& mutation = mirror_mutation_scratch_;
            prepare_owner_change_from_roles_into(
                mutation, signal, owner, value);
            mutation.any_state_changed
                = mutation.any_state_changed || bound;
            publish(std::move(mutation));
        } catch (...) {
            cancel_prepared_publication(mirror_mutation_scratch_);
            valid_ = false;
        }
        return;
    }
    const auto& entry = layout_.signals_[signal_index];
    const auto& owner_entry = layout_.owners_[owner_index];
    assert(value.width() == entry.width);
    assert(valid_value_kind(entry.value_kind, value));
    if (!canonical_logic9(value)) {
        valid_ = false;
        return;
    }
    const auto masks = layout_.owner_mask_words(signal, owner);
    bool changed { };
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        const auto next = packed_word(value, word);
        const auto valid = valid_word_mask(entry.width, word);
        const auto selected = masks[word] & valid;
        const auto logic9_word = entry.value_kind == ValueKind::logic9
            ? owner_entry.first_logic9_word + word : no_word;
        const auto plane_word = owner_entry.first_value_word + word;
        const auto old = load_word(owner_values_, plane_word,
            logic9_word, entry.value_kind);
        const auto outside = valid & ~selected;
        for (std::size_t plane = 0U; plane < next.size(); ++plane) {
            if (((old[plane] ^ next[plane]) & outside) != 0U) {
                // The runtime supplied a full owner record that differs
                // outside the bits proven to belong to this owner. Retaining
                // the old outside bits would conceal an incomplete ownership
                // proof and could make later native reads disagree with the
                // checked DriverTable. Revoke the mirror before changing any
                // of its words.
                valid_ = false;
                return;
            }
        }
        changed = changed || old != next;
    }
    // Validate the complete owner before updating any plane word so a late
    // bad word cannot leave a partially mirrored wide value.
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        const auto next = packed_word(value, word);
        const auto logic9_word = entry.value_kind == ValueKind::logic9
            ? owner_entry.first_logic9_word + word : no_word;
        const auto plane_word = owner_entry.first_value_word + word;
        store_word(owner_values_, plane_word, logic9_word,
            entry.value_kind, next);
    }
    dirty_signals_[signal_index] = static_cast<std::uint8_t>(
        dirty_signals_[signal_index] != 0U || changed || bound);
    advance_generation();
}

void AuthoritativeSignalPlanes::mirror_owner_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& value,
    const PackedLogic4& current,
    const PackedLogic4& stored) noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return;
    }
    if (!valid_ || !versioned_storage_ready_ || !packed_slots_bound_) {
        return;
    }
    if (!generation_can_advance()) {
        valid_ = false;
        return;
    }
    const auto owner_slot = layout_.owner_index(signal, owner);
    if (owner_slot != no_word
        && layout_.owners_[owner_slot].aliases_stored) {
        if (owner_seeded_[owner_flat_index(owner_slot)] == 0U
            || value != stored) {
            valid_ = false;
            return;
        }
        try {
            prepare_value_change_into(mutation, signal, current, stored);
            mutation.owner_index = owner_slot;
            mutation.owner_is_stored_alias = true;
            if (!begin_prepared_publication(mutation)) {
                cancel_prepared_publication(mutation);
                valid_ = false;
                return;
            }
            publish(std::move(mutation));
        } catch (...) {
            cancel_prepared_publication(mutation);
            valid_ = false;
        }
        return;
    }
    try {
        prepare_owner_change_into(
            mutation, signal, owner, value, current, stored);
        if (!begin_prepared_publication(mutation)) {
            cancel_prepared_publication(mutation);
            valid_ = false;
            return;
        }
        publish(std::move(mutation));
    } catch (...) {
        cancel_prepared_publication(mutation);
        valid_ = false;
    }
}

void AuthoritativeSignalPlanes::mirror_logic4_word(
    const SignalId signal,
    const Logic4Word previous,
    const Logic4Word current,
    const Logic4Word stored) noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return;
    }
    if (!valid_) {
        return;
    }
    if (!generation_can_advance()) {
        valid_ = false;
        return;
    }
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return;
    }
    const auto index = static_cast<std::size_t>(found - layout_.signal_ids_.begin());
    const bool bound = packed_slots_bound_
        && packed_signal_slot_binding_tokens_[index] != 0U;
    assert(signal_seeded_[index] != 0U);
    const auto& entry = layout_.signals_[index];
    if (entry.width == 0U) {
        return;
    }
    if (versioned_storage_ready_) {
        try {
            const auto previous_value = PackedLogic4::from_aval_bval(
                previous.width, previous.aval, previous.bval);
            const auto current_value = PackedLogic4::from_aval_bval(
                current.width, current.aval, current.bval);
            const auto stored_value = PackedLogic4::from_aval_bval(
                stored.width, stored.aval, stored.bval);
            auto& mutation = mirror_mutation_scratch_;
            prepare_value_change_into(
                mutation, signal, current_value, stored_value);
            mutation.any_current_changed = true;
            // The explicit previous word is installed even when the value
            // planes compare equal, so it must advance the generation.
            mutation.any_state_changed = true;
            for (auto& word : mutation.words) {
                word.old_current = packed_word(previous_value, 0U);
            }
            prepare_replacements(mutation);
            publish(std::move(mutation));
        } catch (...) {
            cancel_prepared_publication(mirror_mutation_scratch_);
            valid_ = false;
        }
        return;
    }
    assert(entry.value_kind == ValueKind::logic4 && entry.width <= 64U);
    assert(previous.width == entry.width && current.width == entry.width
        && stored.width == entry.width);
    const auto valid = valid_word_mask(entry.width, 0U);
    const auto signal_word = entry.first_value_word;
    const auto old_current = load_word(
        current_, signal_word, no_word, ValueKind::logic4);
    const auto previous_value = std::array<std::uint64_t, 4U> {
        previous.aval & valid, previous.bval & valid, 0U, 0U
    };
    const auto current_value = std::array<std::uint64_t, 4U> {
        current.aval & valid, current.bval & valid, 0U, 0U
    };
    const auto stored_value = std::array<std::uint64_t, 4U> {
        stored.aval & valid, stored.bval & valid, 0U, 0U
    };
    store_word(previous_, signal_word, no_word,
        ValueKind::logic4, previous_value);
    store_word(current_, signal_word, no_word,
        ValueKind::logic4, current_value);
    store_word(stored_, signal_word, no_word,
        ValueKind::logic4, stored_value);
    dirty_signals_[index] = static_cast<std::uint8_t>(
        dirty_signals_[index] != 0U || old_current != current_value || bound);
    advance_generation();
}

void AuthoritativeSignalPlanes::mirror_logic9_word(
    const SignalId signal,
    const Logic9Word previous,
    const Logic9Word current,
    const Logic9Word stored) noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return;
    }
    if (!valid_) {
        return;
    }
    if (!generation_can_advance()) {
        valid_ = false;
        return;
    }
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return;
    }
    const auto index = static_cast<std::size_t>(found - layout_.signal_ids_.begin());
    assert(signal_seeded_[index] != 0U);
    const auto& entry = layout_.signals_[index];
    if (entry.width == 0U) {
        return;
    }
    if (!previous.has_canonical_codes() || !current.has_canonical_codes()
        || !stored.has_canonical_codes()) {
        valid_ = false;
        return;
    }
    if (versioned_storage_ready_) {
        try {
            auto previous_value
                = PackedLogic4::from_logic9_word(previous);
            auto current_value
                = PackedLogic4::from_logic9_word(current);
            auto stored_value
                = PackedLogic4::from_logic9_word(stored);
            auto& mutation = mirror_mutation_scratch_;
            prepare_value_change_into(
                mutation, signal, current_value, stored_value);
            mutation.any_current_changed = true;
            // The previous four-plane value is an installed role even when
            // current and stored match their existing words.
            mutation.any_state_changed = true;
            for (auto& word : mutation.words) {
                word.old_current = packed_word(previous_value, 0U);
            }
            prepare_replacements(mutation);
            publish(std::move(mutation));
        } catch (...) {
            cancel_prepared_publication(mirror_mutation_scratch_);
            valid_ = false;
        }
        return;
    }
    assert(entry.value_kind == ValueKind::logic9 && entry.width <= 64U);
    assert(previous.width == entry.width && current.width == entry.width
        && stored.width == entry.width);
    const auto valid = valid_word_mask(entry.width, 0U);
    const auto signal_word = entry.first_value_word;
    const auto logic9_word = entry.first_logic9_word;
    const auto old_current = load_word(
        current_, signal_word, logic9_word, ValueKind::logic9);
    const auto encode = [valid](const Logic9Word& word) {
        return std::array<std::uint64_t, 4U> {
            word.planes[0U] & valid, word.planes[1U] & valid,
            word.planes[2U] & valid, word.planes[3U] & valid
        };
    };
    const auto previous_value = encode(previous);
    const auto current_value = encode(current);
    const auto stored_value = encode(stored);
    store_word(previous_, signal_word, logic9_word,
        ValueKind::logic9, previous_value);
    store_word(current_, signal_word, logic9_word,
        ValueKind::logic9, current_value);
    store_word(stored_, signal_word, logic9_word,
        ValueKind::logic9, stored_value);
    dirty_signals_[index] = static_cast<std::uint8_t>(
        dirty_signals_[index] != 0U || old_current != current_value);
    advance_generation();
}

AuthoritativeSignalPlanes::PreparedMutation
AuthoritativeSignalPlanes::prepare_value_change(
    const SignalId signal,
    const PackedLogic4& current,
    const PackedLogic4& stored) const
{
    const auto signal_slot = layout_.signal_index(signal);
    PreparedMutation mutation;
    mutation.words.reserve(layout_.signals_[signal_slot].word_count);
    prepare_value_change_into(mutation, signal, current, stored);
    return mutation;
}

void AuthoritativeSignalPlanes::prepare_value_change_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const PackedLogic4& current,
    const PackedLogic4& stored) const
{
    prepare_value_change_from_roles_into(
        mutation, signal, &current, &stored);
}

void AuthoritativeSignalPlanes::prepare_value_change_from_roles_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const PackedLogic4* const current_value,
    const PackedLogic4* const stored_value,
    const bool prepare_copy_on_write) const
{
    if (mutation.preflighted || mutation.preflight_lock_count != 0U) {
        throw std::logic_error {
            "cannot reuse a mutation with an active publication preflight"
        };
    }
    if (!valid_) {
        throw std::logic_error {
            "invalid authoritative component requires a fresh snapshot"
        };
    }
    const auto signal_slot = layout_.signal_index(signal);
    const auto& entry = layout_.signals_[signal_slot];
    if (signal_seeded_[signal_slot] == 0U) {
        throw std::logic_error {
            "authoritative signal mutation preceded its seed"
        };
    }
    if (current_value != nullptr) {
        validate_value(signal, *current_value);
    }
    if (stored_value != nullptr) {
        validate_value(signal, *stored_value);
    }
    if (mutation.words.capacity() < entry.word_count) {
        throw std::length_error {
            "prepared mutation scratch is smaller than its signal"
        };
    }

    mutation.words.clear();
    mutation.prepared_generation = generation_;
    mutation.signal = signal;
    mutation.signal_index = signal_slot;
    mutation.owner_index = 0U;
    mutation.any_current_changed = false;
    mutation.any_stored_changed = false;
    mutation.any_owner_changed = false;
    mutation.any_state_changed = false;
    mutation.has_owner = false;
    mutation.owner_is_stored_alias = false;
    mutation.whole_signal = true;
    mutation.preflight_locked = { };
    mutation.preflight_lock_count = 0U;
    mutation.preflighted = false;
    mutation.owner_group_preflighted = false;
    for (auto& replacement : mutation.replacements) {
        replacement.reset();
    }
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        const auto valid = valid_word_mask(entry.width, word);
        const auto logic9_word = entry.value_kind == ValueKind::logic9
            ? entry.first_logic9_word + word : no_word;
        auto current_word = current_value != nullptr
            ? packed_word(*current_value, word)
            : load_word(current_, entry.first_value_word + word,
                logic9_word, entry.value_kind);
        auto stored_word = stored_value != nullptr
            ? packed_word(*stored_value, word)
            : load_word(stored_, entry.first_value_word + word,
                logic9_word, entry.value_kind);
        for (auto& plane : current_word) {
            plane &= valid;
        }
        for (auto& plane : stored_word) {
            plane &= valid;
        }
        if (entry.value_kind == ValueKind::logic9) {
            const auto remaining_width
                = static_cast<std::size_t>(entry.width) - word * 64U;
            const auto word_width = std::min<std::size_t>(remaining_width, 64U);
            auto canonical_current = Logic9Word {
                word_width, current_word
            };
            canonical_current.normalize_invalid_codes_to_x();
            current_word = canonical_current.planes;
            auto canonical_stored = Logic9Word {
                word_width, stored_word
            };
            canonical_stored.normalize_invalid_codes_to_x();
            stored_word = canonical_stored.planes;
        }
        const auto old_current = load_word(current_,
            entry.first_value_word + word, logic9_word, entry.value_kind);
        const auto old_previous = load_word(previous_,
            entry.first_value_word + word, logic9_word, entry.value_kind);
        const auto old_stored = load_word(stored_,
            entry.first_value_word + word, logic9_word, entry.value_kind);
        mutation.any_current_changed
            = mutation.any_current_changed || old_current != current_word;
        mutation.any_stored_changed
            = mutation.any_stored_changed || old_stored != stored_word;
        mutation.any_state_changed = mutation.any_state_changed
            || old_current != current_word || old_stored != stored_word;
        PreparedWord prepared;
        prepared.signal_word = entry.first_value_word + word;
        prepared.logic9_word = logic9_word;
        prepared.owner_word = no_word;
        prepared.owner_logic9_word = no_word;
        prepared.old_previous = old_previous;
        prepared.old_current = old_current;
        prepared.old_stored = old_stored;
        prepared.new_current = current_word;
        prepared.new_stored = stored_word;
        mutation.words.push_back(prepared);
    }
    if (prepare_copy_on_write) {
        prepare_replacements(mutation);
    }
}

AuthoritativeSignalPlanes::PreparedMutation
AuthoritativeSignalPlanes::prepare_owner_change(
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& owner_value,
    const PackedLogic4& current,
    const PackedLogic4& stored) const
{
    const auto signal_slot = layout_.signal_index(signal);
    PreparedMutation mutation;
    mutation.words.reserve(layout_.signals_[signal_slot].word_count);
    prepare_owner_change_into(
        mutation, signal, owner, owner_value, current, stored);
    return mutation;
}

void AuthoritativeSignalPlanes::prepare_owner_change_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& owner_value,
    const PackedLogic4& current,
    const PackedLogic4& stored) const
{
    prepare_value_change_into(mutation, signal, current, stored);
    prepare_owner_change_from_prepared_values_into(
        mutation, signal, owner, owner_value, &stored, true);
}

void AuthoritativeSignalPlanes::prepare_owner_group_change_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& owner_value,
    const PackedLogic4& current,
    const PackedLogic4& stored) const
{
    if (mutation.preflighted || mutation.preflight_lock_count != 0U) {
        throw std::logic_error {
            "cannot reuse a mutation with an active publication preflight"
        };
    }
    mutation.owner_group_row = true;
    prepare_value_change_from_roles_into(
        mutation, signal, &current, &stored, false);
    prepare_owner_change_from_prepared_values_into(
        mutation, signal, owner, owner_value, &stored, false);
}

void AuthoritativeSignalPlanes::prepare_owner_change_from_roles_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& owner_value) const
{
    prepare_value_change_from_roles_into(
        mutation, signal, nullptr, nullptr, true);
    prepare_owner_change_from_prepared_values_into(
        mutation, signal, owner, owner_value, nullptr, true);
}

void AuthoritativeSignalPlanes::prepare_owner_change_from_prepared_values_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& owner_value,
    const PackedLogic4* const stored_value,
    const bool prepare_copy_on_write) const
{
    validate_value(signal, owner_value);
    const auto owner_slot = owner_index(signal, owner);
    if (owner_seeded_[owner_flat_index(owner_slot)] == 0U) {
        throw std::logic_error {
            "direct owner mutation preceded its seed"
        };
    }
    if (layout_.owners_[owner_slot].aliases_stored) {
        bool matches_stored = stored_value != nullptr
            ? owner_value == *stored_value : true;
        if (stored_value == nullptr) {
            const auto& signal_entry
                = layout_.signals_[mutation.signal_index];
            for (std::size_t word = 0U;
                word < signal_entry.word_count; ++word) {
                auto owner_word = packed_word(owner_value, word);
                const auto valid = valid_word_mask(
                    signal_entry.width, word);
                for (auto& plane : owner_word) {
                    plane &= valid;
                }
                matches_stored = matches_stored
                    && owner_word == mutation.words[word].new_stored;
            }
        }
        if (!matches_stored) {
            throw std::invalid_argument {
                "stored-owner alias diverges from its stored signal"
            };
        }
        mutation.owner_index = owner_slot;
        mutation.owner_is_stored_alias = true;
        for (auto& prepared : mutation.words) {
            prepared.old_owner = prepared.old_stored;
            prepared.new_owner = prepared.new_stored;
        }
        return;
    }
    const auto& signal_entry = layout_.signals_[mutation.signal_index];
    const auto& owner_entry = layout_.owners_[owner_slot];
    const auto masks = layout_.owner_mask_words(signal, owner);
    mutation.has_owner = true;
    mutation.owner_index = owner_slot;
    for (std::size_t word = 0U; word < signal_entry.word_count; ++word) {
        auto new_owner = packed_word(owner_value, word);
        const auto valid = valid_word_mask(signal_entry.width, word);
        for (auto& plane : new_owner) {
            plane &= valid;
        }
        const auto old_owner = load_word(owner_values_,
            owner_entry.first_value_word + word,
            signal_entry.value_kind == ValueKind::logic9
                ? owner_entry.first_logic9_word + word : no_word,
            signal_entry.value_kind);
        const auto outside = valid & ~masks[word];
        for (std::size_t plane = 0U; plane < new_owner.size(); ++plane) {
            if (((new_owner[plane] ^ old_owner[plane]) & outside) != 0U) {
                throw std::invalid_argument {
                    "direct owner mutation changes bits outside its mask"
                };
            }
        }
        mutation.any_owner_changed
            = mutation.any_owner_changed || old_owner != new_owner;
        mutation.any_state_changed
            = mutation.any_state_changed || old_owner != new_owner;
        auto& prepared = mutation.words[word];
        prepared.owner_word = owner_entry.first_value_word + word;
        prepared.owner_logic9_word
            = signal_entry.value_kind == ValueKind::logic9
            ? owner_entry.first_logic9_word + word : no_word;
        prepared.old_owner = old_owner;
        prepared.new_owner = new_owner;
    }
    if (prepare_copy_on_write && versioned_storage_ready_
        && mutation.any_owner_changed
        && owner_values_.cell) {
        const auto block
            = owner_values_.cell->current.load(std::memory_order_acquire);
        if (block && block->has_read_pins()) {
            mutation.replacements[static_cast<std::size_t>(
                PackedPlaneRole::owner)] = prepare_replacement(owner_values_);
        }
    }
}

void AuthoritativeSignalPlanes::prepare_owner_value_change_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& expected_owner_value) const
{
    if (mutation.preflighted || mutation.preflight_lock_count != 0U) {
        throw std::logic_error {
            "cannot reuse a mutation with an active publication preflight"
        };
    }
    if (!valid_) {
        throw std::logic_error {
            "invalid authoritative component requires a fresh snapshot"
        };
    }
    const auto signal_slot = layout_.signal_index(signal);
    const auto& entry = layout_.signals_[signal_slot];
    const auto owner_slot = owner_index(signal, owner);
    const auto& owner_entry = layout_.owners_[owner_slot];
    if (signal_seeded_[signal_slot] == 0U
        || owner_seeded_[owner_flat_index(owner_slot)] == 0U) {
        throw std::logic_error {
            "wide owner publication preceded its authoritative seed"
        };
    }
    validate_value(signal, expected_owner_value);
    if (owner_entry.aliases_stored) {
        if (!matches_value_plane(
                stored_, signal, expected_owner_value)) {
            throw std::logic_error {
                "stored-owner alias disagrees with the expected raw value"
            };
        }
        throw std::invalid_argument {
            "stored-owner alias has no separate raw-owner publication phase"
        };
    }
    if (mutation.words.capacity() < entry.word_count) {
        throw std::length_error {
            "prepared mutation scratch is smaller than its signal"
        };
    }

    mutation.words.clear();
    mutation.prepared_generation = generation_;
    mutation.signal = signal;
    mutation.signal_index = signal_slot;
    mutation.owner_index = owner_slot;
    mutation.any_current_changed = false;
    mutation.any_stored_changed = false;
    mutation.any_owner_changed = false;
    mutation.any_state_changed = false;
    mutation.has_owner = false;
    mutation.owner_is_stored_alias = false;
    mutation.whole_signal = true;
    mutation.preflight_locked = { };
    mutation.preflight_lock_count = 0U;
    mutation.preflighted = false;
    for (auto& replacement : mutation.replacements) {
        replacement.reset();
    }

    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        const auto valid = valid_word_mask(entry.width, word);
        auto owner_value = load_word(owner_values_,
            owner_entry.first_value_word + word,
            entry.value_kind == ValueKind::logic9
                ? owner_entry.first_logic9_word + word : no_word,
            entry.value_kind);
        auto expected = packed_word(expected_owner_value, word);
        for (std::size_t plane = 0U; plane < owner_value.size(); ++plane) {
            owner_value[plane] &= valid;
            expected[plane] &= valid;
        }
        if (owner_value != expected) {
            throw std::logic_error {
                "wide resolved value disagrees with its mirrored raw owner"
            };
        }

        const auto logic9_word = entry.value_kind == ValueKind::logic9
            ? entry.first_logic9_word + word : no_word;
        const auto signal_word = entry.first_value_word + word;
        const auto old_current = load_word(
            current_, signal_word, logic9_word, entry.value_kind);
        const auto old_previous = load_word(
            previous_, signal_word, logic9_word, entry.value_kind);
        const auto old_stored = load_word(
            stored_, signal_word, logic9_word, entry.value_kind);
        mutation.any_current_changed
            = mutation.any_current_changed || old_current != owner_value;
        mutation.any_stored_changed
            = mutation.any_stored_changed || old_stored != owner_value;
        mutation.any_state_changed = mutation.any_state_changed
            || old_current != owner_value || old_stored != owner_value;
        PreparedWord prepared;
        prepared.signal_word = signal_word;
        prepared.logic9_word = logic9_word;
        prepared.owner_word = no_word;
        prepared.owner_logic9_word = no_word;
        prepared.old_previous = old_previous;
        prepared.old_current = old_current;
        prepared.old_stored = old_stored;
        prepared.old_owner = owner_value;
        prepared.new_current = owner_value;
        prepared.new_stored = owner_value;
        mutation.words.push_back(prepared);
    }
    prepare_replacements(mutation);
}

void AuthoritativeSignalPlanes::prepare_owner_slice_change_into(
    PreparedMutation& mutation,
    const SignalId signal,
    const ProcessId owner,
    const PackedLogic4& slice_value,
    const std::size_t offset,
    const PackedLogic4& current,
    const PackedLogic4& stored) const
{
    prepare_value_change_into(mutation, signal, current, stored);
    mutation.whole_signal = false;
    const auto& entry = layout_.signals_[mutation.signal_index];
    if (slice_value.width() == 0U
        || offset > entry.width
        || slice_value.width() > entry.width - offset
        || !valid_value_kind(entry.value_kind, slice_value)
        || !canonical_logic9(slice_value)) {
        throw std::invalid_argument {
            "direct owner slice has an invalid width, range, or value kind"
        };
    }

    const auto owner_slot = owner_index(signal, owner);
    if (layout_.owners_[owner_slot].aliases_stored) {
        throw std::invalid_argument {
            "stored-owner alias does not admit partial mutations"
        };
    }
    if (owner_seeded_[owner_flat_index(owner_slot)] == 0U) {
        throw std::logic_error {
            "direct owner slice mutation preceded its seed"
        };
    }
    const auto& owner_entry = layout_.owners_[owner_slot];
    const auto masks = layout_.owner_mask_words(signal, owner);
    const auto slice_end = offset + slice_value.width();
    for (auto bit = offset; bit < slice_end;) {
        const auto word = bit / 64U;
        const auto in_word = static_cast<unsigned>(bit % 64U);
        const auto count = static_cast<unsigned>(std::min<std::size_t>(
            slice_end - bit, 64U - in_word));
        const auto low_mask = count == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : ((UINT64_C(1) << count) - UINT64_C(1));
        const auto selected_mask = low_mask << in_word;
        if ((masks[word] & selected_mask) != selected_mask) {
            throw std::invalid_argument {
                "direct owner slice extends outside its proven mask"
            };
        }
        bit += count;
    }

    mutation.has_owner = true;
    mutation.owner_index = owner_slot;
    mutation.any_owner_changed = false;
    for (std::size_t word = 0U; word < entry.word_count; ++word) {
        const auto owner_word = owner_entry.first_value_word + word;
        const auto owner_logic9_word = entry.value_kind == ValueKind::logic9
            ? owner_entry.first_logic9_word + word : no_word;
        const auto old_owner = load_word(owner_values_, owner_word,
            owner_logic9_word, entry.value_kind);
        auto new_owner = old_owner;
        const auto target_begin = word * 64U;
        const auto target_end = std::min<std::size_t>(
            target_begin + 64U, entry.width);
        auto bit = std::max(offset, target_begin);
        const auto end = std::min(slice_end, target_end);
        while (bit < end) {
            const auto destination_bit
                = static_cast<unsigned>(bit - target_begin);
            const auto source_offset = bit - offset;
            const auto source_word = source_offset / 64U;
            const auto source_bit
                = static_cast<unsigned>(source_offset % 64U);
            const auto count = static_cast<unsigned>(
                std::min<std::size_t>({ end - bit,
                    64U - destination_bit, 64U - source_bit }));
            const auto low_mask = count == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : ((UINT64_C(1) << count) - UINT64_C(1));
            const auto destination_mask = low_mask << destination_bit;
            const auto source = packed_word(slice_value, source_word);
            for (std::size_t plane = 0U;
                plane < (entry.value_kind == ValueKind::logic9 ? 4U : 2U);
                ++plane) {
                const auto inserted
                    = ((source[plane] >> source_bit) & low_mask)
                    << destination_bit;
                new_owner[plane]
                    = (new_owner[plane] & ~destination_mask) | inserted;
            }
            bit += count;
        }
        const auto valid = valid_word_mask(entry.width, word);
        for (auto& plane : new_owner) {
            plane &= valid;
        }
        mutation.any_owner_changed
            = mutation.any_owner_changed || old_owner != new_owner;
        mutation.any_state_changed
            = mutation.any_state_changed || old_owner != new_owner;
        auto& prepared = mutation.words[word];
        prepared.owner_word = owner_word;
        prepared.owner_logic9_word = owner_logic9_word;
        prepared.new_owner = new_owner;
    }
    if (versioned_storage_ready_ && mutation.any_owner_changed
        && owner_values_.cell) {
        const auto block
            = owner_values_.cell->current.load(std::memory_order_acquire);
        if (block && block->has_read_pins()) {
            mutation.replacements[static_cast<std::size_t>(
                PackedPlaneRole::owner)] = prepare_replacement(owner_values_);
        }
    }
}

void AuthoritativeSignalPlanes::install_previous(
    const PreparedMutation& mutation) noexcept
{
    assert(mutation.signal_index < signal_seeded_.size());
    assert(signal_seeded_[mutation.signal_index] != 0U);
    const auto& entry = layout_.signals_[mutation.signal_index];
    assert(mutation.words.size() == entry.word_count);
    if (mutation.any_current_changed) {
        for (const auto& word : mutation.words) {
            store_word(previous_, word.signal_word, word.logic9_word,
                entry.value_kind, word.old_current);
        }
    }
}

void AuthoritativeSignalPlanes::install_current_stored_owner(
    const PreparedMutation& mutation) noexcept
{
    assert(mutation.signal_index < signal_seeded_.size());
    assert(signal_seeded_[mutation.signal_index] != 0U);
    const auto& entry = layout_.signals_[mutation.signal_index];
    assert(mutation.words.size() == entry.word_count);
    assert(!mutation.has_owner
        || mutation.owner_index < owner_seeded_.size());
    assert(!mutation.has_owner || !mutation.owner_is_stored_alias);
    for (const auto& word : mutation.words) {
        if (mutation.any_current_changed) {
            store_word(current_, word.signal_word, word.logic9_word,
                entry.value_kind, word.new_current);
        }
        if (mutation.any_stored_changed) {
            store_word(stored_, word.signal_word, word.logic9_word,
                entry.value_kind, word.new_stored);
        }
        if (mutation.has_owner && mutation.any_owner_changed) {
            store_word(owner_values_, word.owner_word,
                word.owner_logic9_word, entry.value_kind, word.new_owner);
        }
    }
    dirty_signals_[mutation.signal_index]
        = static_cast<std::uint8_t>(dirty_signals_[mutation.signal_index]
            != 0U || mutation.any_state_changed);
}

void AuthoritativeSignalPlanes::publish(
    PreparedMutation&& mutation) noexcept
{
    if (mutation.owner_group_row || mutation.owner_group_preflighted) {
        valid_ = false;
        cancel_prepared_publication(mutation);
        return;
    }
    if (frontier_write_active_) {
        valid_ = false;
        cancel_prepared_publication(mutation);
        return;
    }
    if (!valid_) {
        cancel_prepared_publication(mutation);
        return;
    }
    const std::span<PreparedMutation> mutations { &mutation, 1U };
    if (!prepared_generation_is_current(mutations)) {
        cancel_prepared_publication(mutation);
        return;
    }
    const bool changes_state = mutation.any_state_changed;
    if (changes_state && !generation_can_advance()) {
        valid_ = false;
        cancel_prepared_publication(mutation);
        return;
    }
    WritablePlaneTransaction transaction;
    if (mutation.preflighted) {
        transaction.locked = mutation.preflight_locked;
        transaction.lock_count = mutation.preflight_lock_count;
        mutation.preflight_locked = { };
        mutation.preflight_lock_count = 0U;
        mutation.preflighted = false;
        transaction.replacements = std::move(mutation.replacements);
    } else {
        if (!prepare_writable_planes(mutations, transaction)) {
            cancel_prepared_publication(mutation);
            valid_ = false;
            return;
        }
    }
    if (!versioned_storage_ready_) {
        install_previous(mutation);
        install_current_stored_owner(mutation);
        if (changes_state) {
            advance_generation();
        }
        return;
    }

    const auto install_role = [this, &transaction](
                                  const PackedPlaneRole role) {
        auto& words = role_words(role);
        auto& replacement = transaction.replacements[
            static_cast<std::size_t>(role)];
        if (replacement) {
            install_replacement(words, replacement);
        }
    };
    const auto write_role = [this, &transaction, &mutations](
                                const PackedPlaneRole role) {
        if (transaction.replacements[static_cast<std::size_t>(role)]) {
            return;
        }
        for (const auto& item : mutations) {
            const auto& entry = layout_.signals_[item.signal_index];
            for (const auto& word : item.words) {
                const std::array<std::uint64_t, 4U>* value { };
                std::size_t value_word { };
                std::size_t logic9_word { };
                switch (role) {
                case PackedPlaneRole::previous:
                    if (item.any_current_changed) {
                        value = &word.old_current;
                        value_word = word.signal_word;
                        logic9_word = word.logic9_word;
                    }
                    break;
                case PackedPlaneRole::current:
                    if (item.any_current_changed) {
                        value = &word.new_current;
                        value_word = word.signal_word;
                        logic9_word = word.logic9_word;
                    }
                    break;
                case PackedPlaneRole::stored:
                    if (item.any_stored_changed) {
                        value = &word.new_stored;
                        value_word = word.signal_word;
                        logic9_word = word.logic9_word;
                    }
                    break;
                case PackedPlaneRole::owner:
                    if (item.any_owner_changed) {
                        value = &word.new_owner;
                        value_word = word.owner_word;
                        logic9_word = word.owner_logic9_word;
                    }
                    break;
                }
                if (value != nullptr) {
                    store_word(role_words(role), value_word, logic9_word,
                        entry.value_kind, *value);
                }
            }
        }
    };

    install_role(PackedPlaneRole::previous);
    write_role(PackedPlaneRole::previous);
    install_role(PackedPlaneRole::current);
    write_role(PackedPlaneRole::current);
    install_role(PackedPlaneRole::stored);
    write_role(PackedPlaneRole::stored);
    install_role(PackedPlaneRole::owner);
    write_role(PackedPlaneRole::owner);
    for (const auto& item : mutations) {
        dirty_signals_[item.signal_index]
            = static_cast<std::uint8_t>(dirty_signals_[item.signal_index]
                != 0U || item.any_state_changed);
    }
    finish_writable_planes(transaction);
    if (changes_state) {
        advance_generation();
    }
    for (auto& replacement : mutation.replacements) {
        replacement.reset();
    }
}

void AuthoritativeSignalPlanes::prepare_group_scratch(
    PreparedGroupScratch& scratch) const
{
    if (!valid_ || frontier_write_active_ || !versioned_storage_ready_
        || !packed_slots_bound_) {
        throw std::logic_error {
            "checked group scratch requires valid bound versioned planes"
        };
    }

    if (scratch.owner_ != this || scratch.layout_ != &layout_) {
        for (auto& replacement : scratch.replacements_) {
            replacement.reset();
        }
        scratch.owner_ = this;
        scratch.layout_ = &layout_;
    }
    scratch.spent_ = true;

    const std::array<const PlaneWords*, 4U> roles {
        &current_, &previous_, &stored_, &owner_values_,
    };
    const std::array<std::size_t, 4U> value_word_counts {
        layout_.value_word_count(), layout_.value_word_count(),
        layout_.value_word_count(), layout_.owner_value_word_count(),
    };
    const std::array<std::size_t, 4U> logic9_word_counts {
        layout_.logic9_word_count(), layout_.logic9_word_count(),
        layout_.logic9_word_count(), layout_.owner_logic9_word_count(),
    };

    for (std::size_t role_index = 0U; role_index < roles.size();
         ++role_index) {
        const auto& role = *roles[role_index];
        auto& replacement = scratch.replacements_[role_index];
        const bool optional_owner = role_index
            == static_cast<std::size_t>(PackedPlaneRole::owner)
            && value_word_counts[role_index] == 0U
            && logic9_word_counts[role_index] == 0U;
        if (optional_owner && !role.cell) {
            replacement.reset();
            continue;
        }
        if (!role.cell) {
            throw std::logic_error {
                "versioned group role has no stable plane cell"
            };
        }
        const auto source = role.cell->current.load(
            std::memory_order_acquire);
        if (!source || !plane_block_matches_sizes(*source,
                           value_word_counts[role_index],
                           logic9_word_counts[role_index])) {
            throw std::logic_error {
                "versioned group role has an invalid plane block"
            };
        }

        bool reusable = replacement
            && replacement.get() != source.get()
            && replacement.use_count() == 1
            && !replacement->has_read_pins()
            && !replacement->write_locked()
            && plane_block_matches_sizes(*replacement,
                value_word_counts[role_index],
                logic9_word_counts[role_index]);
        if (!reusable) {
            replacement.reset();
            replacement = prepare_replacement(role);
            if (!replacement) {
                throw std::logic_error {
                    "versioned group role could not prepare COW scratch"
                };
            }
            const auto refreshed_source = role.cell->current.load(
                std::memory_order_acquire);
            if (!refreshed_source
                || replacement.get() == refreshed_source.get()
                || replacement.use_count() != 1
                || replacement->has_read_pins()
                || replacement->write_locked()
                || !plane_block_matches_sizes(*replacement,
                    value_word_counts[role_index],
                    logic9_word_counts[role_index])) {
                replacement.reset();
                throw std::logic_error {
                    "prepared group COW scratch has an invalid role shape"
                };
            }
            refresh_plane_block(*replacement, *refreshed_source);
            continue;
        }
        refresh_plane_block(*replacement, *source);
    }
    scratch.spent_ = false;
}

bool AuthoritativeSignalPlanes::try_rebase_and_publish_group(
    const std::span<PreparedMutation> ordered_rows,
    const std::uint64_t expected_live_revision,
    PreparedGroupScratch& scratch) noexcept
{
    if (std::ranges::any_of(ordered_rows,
            [](const PreparedMutation& row) {
                return row.owner_group_row
                    || row.owner_group_preflighted;
            })
        || !valid_ || frontier_write_active_ || !versioned_storage_ready_
        || !packed_slots_bound_ || ordered_rows.empty()
        || ordered_rows.size() > layout_.signal_count()
        || expected_live_revision == 0U
        || generation_ != expected_live_revision
        || !scratch.ready() || scratch.owner_ != this
        || scratch.layout_ != &layout_) {
        return false;
    }

    const auto value_words = layout_.value_word_count();
    const auto logic9_words = layout_.logic9_word_count();
    const auto owner_value_words = layout_.owner_value_word_count();
    const auto owner_logic9_words = layout_.owner_logic9_word_count();
    std::array<PlaneWords*, 4U> roles {
        &current_, &previous_, &stored_, &owner_values_,
    };
    const std::array<std::size_t, 4U> role_value_words {
        value_words, value_words, value_words, owner_value_words,
    };
    const std::array<std::size_t, 4U> role_logic9_words {
        logic9_words, logic9_words, logic9_words, owner_logic9_words,
    };
    std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 4U> live_blocks;
    for (std::size_t role_index = 0U; role_index < roles.size();
         ++role_index) {
        const auto& role = *roles[role_index];
        if (!role.cell) {
            if (role_index
                    == static_cast<std::size_t>(PackedPlaneRole::owner)
                && role_value_words[role_index] == 0U
                && role_logic9_words[role_index] == 0U) {
                continue;
            }
            return false;
        }
        live_blocks[role_index]
            = role.cell->current.load(std::memory_order_acquire);
        if (!live_blocks[role_index]
            || !plane_block_matches_sizes(*live_blocks[role_index],
                role_value_words[role_index],
                role_logic9_words[role_index])) {
            return false;
        }
    }

    const auto read_word = [](const PackedLogic4PlaneBlock& block,
                               const std::size_t value_word,
                               const std::size_t logic9_word,
                               const ValueKind kind) noexcept {
        return std::array<std::uint64_t, 4U> {
            block.planes[0U][value_word],
            block.planes[1U][value_word],
            kind == ValueKind::logic9
                ? block.planes[2U][logic9_word] : 0U,
            kind == ValueKind::logic9
                ? block.planes[3U][logic9_word] : 0U,
        };
    };
    const auto backing_matches = [](const PackedLogic4PlaneBacking& backing,
                                     const PlaneWords& role,
                                     const SignalDriverSignalLayout& signal,
                                     const std::size_t value_word,
                                     const std::size_t logic9_word) noexcept {
        return backing.width == signal.width
            && backing.logic9 == (signal.value_kind == ValueKind::logic9)
            && backing.first_value_word == value_word
            && backing.first_logic9_word == logic9_word
            && backing.cell == role.cell;
    };
    const auto has_expected_binding = [this](
        const SignalId signal,
        const PackedSlotBinding::Role role,
        const ProcessId owner,
        const PackedLogic4PlaneBacking* backing) noexcept {
        const PackedSlotBinding* match = nullptr;
        for (const auto& binding : packed_slot_bindings_) {
            if (binding.signal != signal || binding.role != role
                || (role == PackedSlotBinding::Role::owner
                    && binding.owner != owner)) {
                continue;
            }
            if (match != nullptr || binding.value == nullptr
                || binding.backing != backing
                || binding.value->plane_backing() != backing) {
                return false;
            }
            match = &binding;
        }
        return match != nullptr;
    };

    bool any_state_changed = false;
    std::array<bool, 4U> changed_roles { };
    for (std::size_t row_index = 0U; row_index < ordered_rows.size();
         ++row_index) {
        const auto& row = ordered_rows[row_index];
        if (!row.whole_signal || row.prepared_generation == 0U
            || row.prepared_generation > expected_live_revision
            || row.signal_index >= layout_.signals_.size()
            || row.signal_index >= layout_.signal_ids_.size()
            || row.signal_index >= signal_seeded_.size()
            || row.signal_index >= dirty_signals_.size()
            || row.signal_index >= packed_signal_slot_binding_tokens_.size()
            || row.signal_index >= current_backings_.size()
            || row.signal_index >= previous_backings_.size()
            || row.signal_index >= stored_backings_.size()
            || layout_.signal_ids_[row.signal_index] != row.signal
            || signal_seeded_[row.signal_index] == 0U
            || packed_signal_slot_binding_tokens_[row.signal_index] == 0U
            || !packed_signal_slots_bound(row.signal)) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < row_index; ++earlier) {
            if (ordered_rows[earlier].signal == row.signal) {
                return false;
            }
        }

        const auto& signal = layout_.signals_[row.signal_index];
        if (signal.width == 0U
            || (signal.value_kind != ValueKind::logic4
                && signal.value_kind != ValueKind::logic9)
            || signal.storage_class != SignalDriverStorageClass::single_owner
            || signal.owner_count != 1U
            || signal.word_count != word_count(signal.width)
            || signal.first_owner >= layout_.owners_.size()
            || signal.owner_count > layout_.owners_.size() - signal.first_owner
            || signal.first_value_word > value_words
            || signal.word_count > value_words - signal.first_value_word
            || (signal.value_kind == ValueKind::logic9
                && (signal.first_logic9_word > logic9_words
                    || signal.word_count
                        > logic9_words - signal.first_logic9_word))) {
            return false;
        }
        const auto owner_index = layout_.owner_index(row.signal,
            layout_.owners_[signal.first_owner].process);
        if (owner_index != row.owner_index
            || owner_index >= layout_.owners_.size()
            || owner_index >= owner_seeded_.size()
            || owner_index >= packed_owner_slot_binding_tokens_.size()
            || owner_seeded_[owner_index] == 0U
            || packed_owner_slot_binding_tokens_[owner_index] == 0U
            || !packed_owner_slot_bound(
                row.signal, layout_.owners_[owner_index].process)) {
            return false;
        }
        const auto& owner = layout_.owners_[owner_index];
        const bool owner_alias = owner.aliases_stored;
        if (row.owner_is_stored_alias != owner_alias
            || row.has_owner == owner_alias) {
            return false;
        }
        if (owner.first_mask_word > layout_.owner_mask_words_.size()
            || signal.word_count
                > layout_.owner_mask_words_.size() - owner.first_mask_word) {
            return false;
        }
        for (std::size_t word = 0U; word < signal.word_count; ++word) {
            if (layout_.owner_mask_words_[owner.first_mask_word + word]
                != valid_word_mask(signal.width, word)) {
                return false;
            }
        }
        if (!backing_matches(current_backings_[row.signal_index], current_,
                signal, signal.first_value_word, signal.first_logic9_word)
            || !backing_matches(previous_backings_[row.signal_index],
                previous_, signal, signal.first_value_word,
                signal.first_logic9_word)
            || !backing_matches(stored_backings_[row.signal_index], stored_,
                signal, signal.first_value_word, signal.first_logic9_word)) {
            return false;
        }
        const std::array<PackedSlotBinding::Role, 3U> signal_roles {
            PackedSlotBinding::Role::current,
            PackedSlotBinding::Role::previous,
            PackedSlotBinding::Role::stored,
        };
        const std::array<const PackedLogic4PlaneBacking*, 3U> signal_backings {
            &current_backings_[row.signal_index],
            &previous_backings_[row.signal_index],
            &stored_backings_[row.signal_index],
        };
        for (std::size_t role = 0U; role < signal_roles.size(); ++role) {
            if (!has_expected_binding(row.signal, signal_roles[role],
                    ProcessId { }, signal_backings[role])) {
                return false;
            }
        }

        if (!owner_alias) {
            if (owner_index >= owner_backings_.size()
                || owner.first_value_word > owner_value_words
                || signal.word_count
                    > owner_value_words - owner.first_value_word
                || (signal.value_kind == ValueKind::logic9
                    && (owner.first_logic9_word > owner_logic9_words
                        || signal.word_count
                            > owner_logic9_words
                                - owner.first_logic9_word))
                || !backing_matches(owner_backings_[owner_index],
                    owner_values_, signal, owner.first_value_word,
                    owner.first_logic9_word)
                || !has_expected_binding(row.signal,
                    PackedSlotBinding::Role::owner, owner.process,
                    &owner_backings_[owner_index])) {
                return false;
            }
        } else {
            for (const auto& binding : packed_slot_bindings_) {
                if (binding.signal == row.signal
                    && binding.role == PackedSlotBinding::Role::owner) {
                    return false;
                }
            }
        }

        bool row_current_changed = false;
        bool row_stored_changed = false;
        bool row_owner_changed = false;
        for (std::size_t word_index = 0U;
             word_index < signal.word_count; ++word_index) {
            if (word_index >= row.words.size()) {
                return false;
            }
            const auto& word = row.words[word_index];
            const auto signal_word = signal.first_value_word + word_index;
            const auto logic9_word = signal.value_kind == ValueKind::logic9
                ? signal.first_logic9_word + word_index : no_word;
            const auto owner_word = owner_alias
                ? no_word : owner.first_value_word + word_index;
            const auto owner_logic9_word = owner_alias
                || signal.value_kind != ValueKind::logic9
                ? no_word : owner.first_logic9_word + word_index;
            if (word.signal_word != signal_word
                || word.logic9_word != logic9_word
                || word.owner_word != owner_word
                || word.owner_logic9_word != owner_logic9_word) {
                return false;
            }
            const auto valid = valid_word_mask(signal.width, word_index);
            const auto old_previous = read_word(*live_blocks[
                    static_cast<std::size_t>(PackedPlaneRole::previous)],
                signal_word, logic9_word, signal.value_kind);
            const auto old_current = read_word(*live_blocks[
                    static_cast<std::size_t>(PackedPlaneRole::current)],
                signal_word, logic9_word, signal.value_kind);
            const auto old_stored = read_word(*live_blocks[
                    static_cast<std::size_t>(PackedPlaneRole::stored)],
                signal_word, logic9_word, signal.value_kind);
            const auto old_owner = owner_alias ? old_stored
                : read_word(*live_blocks[
                        static_cast<std::size_t>(PackedPlaneRole::owner)],
                    owner_word, owner_logic9_word, signal.value_kind);
            if (word.old_previous != old_previous
                || word.old_current != old_current
                || word.old_stored != old_stored
                || word.old_owner != old_owner
                || !canonical_prepared_word(word.old_previous,
                    signal.value_kind, valid)
                || !canonical_prepared_word(word.old_current,
                    signal.value_kind, valid)
                || !canonical_prepared_word(word.old_stored,
                    signal.value_kind, valid)
                || !canonical_prepared_word(word.old_owner,
                    signal.value_kind, valid)
                || !canonical_prepared_word(word.new_current,
                    signal.value_kind, valid)
                || !canonical_prepared_word(word.new_stored,
                    signal.value_kind, valid)
                || !canonical_prepared_word(word.new_owner,
                    signal.value_kind, valid)) {
                return false;
            }
            if (owner_alias
                && (word.old_owner != word.old_stored
                    || word.new_owner != word.new_stored)) {
                return false;
            }
            row_current_changed = row_current_changed
                || word.old_current != word.new_current;
            row_stored_changed = row_stored_changed
                || word.old_stored != word.new_stored;
            row_owner_changed = row_owner_changed
                || (!owner_alias && word.old_owner != word.new_owner);
        }
        if (row.words.size() != signal.word_count
            || row.any_current_changed != row_current_changed
            || row.any_stored_changed != row_stored_changed
            || row.any_owner_changed != row_owner_changed
            || row.any_state_changed
                != (row_current_changed || row_stored_changed
                    || row_owner_changed)) {
            return false;
        }
        any_state_changed = any_state_changed || row.any_state_changed;
        changed_roles[static_cast<std::size_t>(PackedPlaneRole::previous)]
            = changed_roles[static_cast<std::size_t>(PackedPlaneRole::previous)]
                || row_current_changed;
        changed_roles[static_cast<std::size_t>(PackedPlaneRole::current)]
            = changed_roles[static_cast<std::size_t>(PackedPlaneRole::current)]
                || row_current_changed;
        changed_roles[static_cast<std::size_t>(PackedPlaneRole::stored)]
            = changed_roles[static_cast<std::size_t>(PackedPlaneRole::stored)]
                || row_stored_changed;
        changed_roles[static_cast<std::size_t>(PackedPlaneRole::owner)]
            = changed_roles[static_cast<std::size_t>(PackedPlaneRole::owner)]
                || row_owner_changed;
    }

    if (any_state_changed && !generation_can_advance()) {
        return false;
    }

    WritablePlaneTransaction transaction;
    std::array<bool, 4U> replace_roles { };
    std::array<PackedLogic4PlaneBlock*, 4U> locked_by_role { };
    for (std::size_t role_index = 0U; role_index < changed_roles.size();
         ++role_index) {
        if (!changed_roles[role_index]) {
            continue;
        }
        const auto& role = *roles[role_index];
        const auto source = role.cell->current.load(
            std::memory_order_acquire);
        if (!source || source != live_blocks[role_index]) {
            finish_writable_planes(transaction);
            return false;
        }
        if (source->try_begin_write()) {
            if (role.cell->current.load(std::memory_order_acquire) != source) {
                source->end_write();
                finish_writable_planes(transaction);
                return false;
            }
            transaction.locked[transaction.lock_count] = source.get();
            ++transaction.lock_count;
            locked_by_role[role_index] = source.get();
            continue;
        }
        if (source->write_locked()) {
            finish_writable_planes(transaction);
            return false;
        }
        const auto& replacement = scratch.replacements_[role_index];
        if (!replacement || replacement.get() == source.get()
            || replacement.use_count() != 1
            || replacement->has_read_pins()
            || replacement->write_locked()
            || !plane_block_matches_sizes(*replacement,
                role_value_words[role_index],
                role_logic9_words[role_index])) {
            finish_writable_planes(transaction);
            return false;
        }
        replace_roles[role_index] = true;
    }

    if (generation_ != expected_live_revision) {
        finish_writable_planes(transaction);
        return false;
    }
    for (std::size_t role_index = 0U; role_index < roles.size();
         ++role_index) {
        if (roles[role_index]->cell
            && roles[role_index]->cell->current.load(
                   std::memory_order_acquire)
                != live_blocks[role_index]) {
            finish_writable_planes(transaction);
            return false;
        }
    }

    const auto write_block_word = [](PackedLogic4PlaneBlock& block,
                                      const std::size_t value_word,
                                      const std::size_t logic9_word,
                                      const ValueKind kind,
                                      const std::array<std::uint64_t, 4U>& value) noexcept {
        block.planes[0U][value_word] = value[0U];
        block.planes[1U][value_word] = value[1U];
        if (kind == ValueKind::logic9) {
            block.planes[2U][logic9_word] = value[2U];
            block.planes[3U][logic9_word] = value[3U];
        }
    };

    std::array<PackedLogic4PlaneBlock*, 4U> write_blocks { };
    for (std::size_t role_index = 0U; role_index < roles.size();
         ++role_index) {
        if (!changed_roles[role_index]) {
            continue;
        }
        if (replace_roles[role_index]) {
            auto& replacement = scratch.replacements_[role_index];
            refresh_plane_block(*replacement, *live_blocks[role_index]);
            write_blocks[role_index] = replacement.get();
        } else {
            write_blocks[role_index] = locked_by_role[role_index];
        }
    }

    frontier_write_active_ = true;
    for (std::size_t role_index = 0U; role_index < roles.size();
         ++role_index) {
        auto* block = write_blocks[role_index];
        if (block == nullptr) {
            continue;
        }
        const auto role = static_cast<PackedPlaneRole>(role_index);
        for (const auto& row : ordered_rows) {
            const auto& signal = layout_.signals_[row.signal_index];
            for (const auto& word : row.words) {
                const std::array<std::uint64_t, 4U>* value = nullptr;
                auto value_word = word.signal_word;
                auto logic9_word = word.logic9_word;
                switch (role) {
                case PackedPlaneRole::current:
                    if (row.any_current_changed) {
                        value = &word.new_current;
                    }
                    break;
                case PackedPlaneRole::previous:
                    if (row.any_current_changed) {
                        value = &word.old_current;
                    }
                    break;
                case PackedPlaneRole::stored:
                    if (row.any_stored_changed) {
                        value = &word.new_stored;
                    }
                    break;
                case PackedPlaneRole::owner:
                    if (row.any_owner_changed) {
                        value = &word.new_owner;
                        value_word = word.owner_word;
                        logic9_word = word.owner_logic9_word;
                    }
                    break;
                }
                if (value != nullptr) {
                    write_block_word(*block, value_word, logic9_word,
                        signal.value_kind, *value);
                }
            }
        }
    }

    for (std::size_t role_index = 0U; role_index < replace_roles.size();
         ++role_index) {
        if (!replace_roles[role_index]) {
            continue;
        }
        live_blocks[role_index].reset();
        install_replacement(*roles[role_index],
            std::move(scratch.replacements_[role_index]));
    }
    for (const auto& row : ordered_rows) {
        dirty_signals_[row.signal_index]
            = static_cast<std::uint8_t>(dirty_signals_[row.signal_index]
                != 0U || row.any_state_changed);
    }
    if (any_state_changed) {
        advance_generation();
    }
    finish_writable_planes(transaction);
    frontier_write_active_ = false;
    scratch.spent_ = true;
    return true;
}

void AuthoritativeSignalPlanes::publish_group(
    std::vector<PreparedMutation>&& mutations) noexcept
{
    if (std::ranges::any_of(mutations,
            [](const PreparedMutation& mutation) {
                return mutation.owner_group_row
                    || mutation.owner_group_preflighted;
            })) {
        valid_ = false;
        for (auto& mutation : mutations) {
            cancel_prepared_publication(mutation);
        }
        return;
    }
    if (frontier_write_active_) {
        valid_ = false;
        for (auto& mutation : mutations) {
            cancel_prepared_publication(mutation);
        }
        return;
    }
    if (!valid_) {
        return;
    }
    if (mutations.empty()) {
        return;
    }
    if (!prepared_generation_is_current(mutations)) {
        return;
    }
    const bool changes_state = std::ranges::any_of(mutations,
        [](const PreparedMutation& mutation) {
            return mutation.any_state_changed;
        });
    if (changes_state && !generation_can_advance()) {
        valid_ = false;
        return;
    }
    WritablePlaneTransaction transaction;
    if (!prepare_writable_planes(mutations, transaction)) {
        valid_ = false;
        return;
    }
    if (!versioned_storage_ready_) {
        for (const auto& mutation : mutations) {
            install_previous(mutation);
        }
        for (const auto& mutation : mutations) {
            install_current_stored_owner(mutation);
        }
        if (changes_state) {
            advance_generation();
        }
        return;
    }

    const auto install_role = [this, &transaction](
                                  const PackedPlaneRole role) {
        auto& words = role_words(role);
        auto& replacement = transaction.replacements[
            static_cast<std::size_t>(role)];
        if (replacement) {
            install_replacement(words, replacement);
        }
    };
    const auto write_role = [this, &transaction, &mutations](
                                const PackedPlaneRole role) {
        if (transaction.replacements[static_cast<std::size_t>(role)]) {
            return;
        }
        for (const auto& item : mutations) {
            const auto& entry = layout_.signals_[item.signal_index];
            for (const auto& word : item.words) {
                const std::array<std::uint64_t, 4U>* value { };
                std::size_t value_word { };
                std::size_t logic9_word { };
                switch (role) {
                case PackedPlaneRole::previous:
                    if (item.any_current_changed) {
                        value = &word.old_current;
                        value_word = word.signal_word;
                        logic9_word = word.logic9_word;
                    }
                    break;
                case PackedPlaneRole::current:
                    if (item.any_current_changed) {
                        value = &word.new_current;
                        value_word = word.signal_word;
                        logic9_word = word.logic9_word;
                    }
                    break;
                case PackedPlaneRole::stored:
                    if (item.any_stored_changed) {
                        value = &word.new_stored;
                        value_word = word.signal_word;
                        logic9_word = word.logic9_word;
                    }
                    break;
                case PackedPlaneRole::owner:
                    if (item.any_owner_changed) {
                        value = &word.new_owner;
                        value_word = word.owner_word;
                        logic9_word = word.owner_logic9_word;
                    }
                    break;
                }
                if (value != nullptr) {
                    store_word(role_words(role), value_word, logic9_word,
                        entry.value_kind, *value);
                }
            }
        }
    };

    install_role(PackedPlaneRole::previous);
    write_role(PackedPlaneRole::previous);
    install_role(PackedPlaneRole::current);
    write_role(PackedPlaneRole::current);
    install_role(PackedPlaneRole::stored);
    write_role(PackedPlaneRole::stored);
    install_role(PackedPlaneRole::owner);
    write_role(PackedPlaneRole::owner);
    for (const auto& mutation : mutations) {
        dirty_signals_[mutation.signal_index]
            = static_cast<std::uint8_t>(dirty_signals_[mutation.signal_index]
                != 0U || mutation.any_state_changed);
    }
    finish_writable_planes(transaction);
    if (changes_state) {
        advance_generation();
    }
}

PackedLogic4 AuthoritativeSignalPlanes::materialize(
    const PlaneWords& planes, const SignalId signal) const
{
    const auto& entry = layout_.signal(signal);
    const auto p0 = planes.plane(0U).span().subspan(
        entry.first_value_word, entry.word_count);
    const auto p1 = planes.plane(1U).span().subspan(
        entry.first_value_word, entry.word_count);
    if (entry.value_kind == ValueKind::logic4) {
        return PackedLogic4::from_word_planes(entry.width, p0, p1);
    }
    const auto p2 = planes.plane(2U).span().subspan(
        entry.first_logic9_word, entry.word_count);
    const auto p3 = planes.plane(3U).span().subspan(
        entry.first_logic9_word, entry.word_count);
    return PackedLogic4::from_logic9_word_planes(entry.width, p0, p1, p2, p3);
}

PackedLogic4 AuthoritativeSignalPlanes::current(
    const SignalId signal) const
{
    const auto index = layout_.signal_index(signal);
    if (signal_seeded_[index] == 0U) {
        throw std::logic_error {
            "authoritative signal was read before its seed"
        };
    }
    return materialize(current_, signal);
}

bool AuthoritativeSignalPlanes::current_logic4_planes(
    const SignalId signal,
    std::span<const std::uint64_t>& aval,
    std::span<const std::uint64_t>& bval) const noexcept
{
    aval = { };
    bval = { };
    std::array<std::span<const std::uint64_t>, 4U> planes;
    if (!current_planes(signal, planes)
        || layout_.signal(signal).value_kind != ValueKind::logic4) {
        return false;
    }

    aval = planes[0U];
    bval = planes[1U];
    return true;
}

bool AuthoritativeSignalPlanes::current_planes(
    const SignalId signal,
    std::array<std::span<const std::uint64_t>, 4U>& planes) const noexcept
{
    planes = { };
    if (!valid_) {
        return false;
    }
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return false;
    }
    const auto index = static_cast<std::size_t>(
        found - layout_.signal_ids_.begin());
    const auto& entry = layout_.signals_[index];
    const auto& aval = current_.plane(0U);
    const auto& bval = current_.plane(1U);
    if (entry.width == 0U || entry.word_count == 0U
        || signal_seeded_[index] == 0U
        || entry.first_value_word > aval.size()
        || entry.word_count > aval.size() - entry.first_value_word
        || entry.first_value_word > bval.size()
        || entry.word_count > bval.size() - entry.first_value_word) {
        return false;
    }
    planes[0U] = std::span<const std::uint64_t> { aval }
        .subspan(entry.first_value_word, entry.word_count);
    planes[1U] = std::span<const std::uint64_t> { bval }
        .subspan(entry.first_value_word, entry.word_count);
    if (entry.value_kind == ValueKind::logic9) {
        const auto& plane2 = current_.plane(2U);
        const auto& plane3 = current_.plane(3U);
        if (entry.first_logic9_word > plane2.size()
            || entry.word_count
                > plane2.size() - entry.first_logic9_word
            || entry.first_logic9_word > plane3.size()
            || entry.word_count
                > plane3.size() - entry.first_logic9_word) {
            planes = { };
            return false;
        }
        planes[2U] = std::span<const std::uint64_t> { plane2 }
            .subspan(entry.first_logic9_word, entry.word_count);
        planes[3U] = std::span<const std::uint64_t> { plane3 }
            .subspan(entry.first_logic9_word, entry.word_count);
    }
    return true;
}

PackedLogic4 AuthoritativeSignalPlanes::previous(
    const SignalId signal) const
{
    const auto index = layout_.signal_index(signal);
    if (signal_seeded_[index] == 0U) {
        throw std::logic_error {
            "authoritative signal was read before its seed"
        };
    }
    return materialize(previous_, signal);
}

PackedLogic4 AuthoritativeSignalPlanes::stored(
    const SignalId signal) const
{
    const auto index = layout_.signal_index(signal);
    if (signal_seeded_[index] == 0U) {
        throw std::logic_error {
            "authoritative signal was read before its seed"
        };
    }
    return materialize(stored_, signal);
}

PackedLogic4 AuthoritativeSignalPlanes::owner_value(
    const SignalId signal, const ProcessId owner) const
{
    const auto index = owner_index(signal, owner);
    if (owner_seeded_[owner_flat_index(index)] == 0U) {
        throw std::logic_error {
            "direct owner value was read before its seed"
        };
    }
    if (layout_.owners_[index].aliases_stored) {
        return stored(signal);
    }
    const auto& signal_entry = layout_.signals_[layout_.signal_index(signal)];
    const auto& owner_entry = layout_.owners_[index];
    const auto p0 = std::span<const std::uint64_t> {
        owner_values_.plane(0U) }
        .subspan(owner_entry.first_value_word, signal_entry.word_count);
    const auto p1 = std::span<const std::uint64_t> {
        owner_values_.plane(1U) }
        .subspan(owner_entry.first_value_word, signal_entry.word_count);
    if (signal_entry.value_kind == ValueKind::logic4) {
        return PackedLogic4::from_word_planes(
            signal_entry.width, p0, p1);
    }
    const auto p2 = std::span<const std::uint64_t> {
        owner_values_.plane(2U) }
        .subspan(owner_entry.first_logic9_word, signal_entry.word_count);
    const auto p3 = std::span<const std::uint64_t> {
        owner_values_.plane(3U) }
        .subspan(owner_entry.first_logic9_word, signal_entry.word_count);
    return PackedLogic4::from_logic9_word_planes(
        signal_entry.width, p0, p1, p2, p3);
}

PackedLogic4PlaneReadLease AuthoritativeSignalPlanes::plane_read_lease(
    const SignalId signal,
    const PackedPlaneRole role,
    const ProcessId owner) const noexcept
{
    const auto found = std::ranges::lower_bound(layout_.signal_ids_, signal);
    if (found == layout_.signal_ids_.end() || *found != signal) {
        return { };
    }
    const auto signal_index = static_cast<std::size_t>(
        found - layout_.signal_ids_.begin());
    const auto& entry = layout_.signals_[signal_index];
    if (!versioned_storage_ready_ || entry.width == 0U
        || signal_seeded_[signal_index] == 0U) {
        return { };
    }
    const PackedLogic4PlaneBacking* backing { };
    switch (role) {
    case PackedPlaneRole::current:
        backing = &current_backings_[signal_index];
        break;
    case PackedPlaneRole::previous:
        backing = &previous_backings_[signal_index];
        break;
    case PackedPlaneRole::stored:
        backing = &stored_backings_[signal_index];
        break;
    case PackedPlaneRole::owner: {
        const auto owner_slot = layout_.owner_index(signal, owner);
        if (owner_slot == no_word
            || owner_seeded_[owner_flat_index(owner_slot)] == 0U) {
            return { };
        }
        if (layout_.owners_[owner_slot].aliases_stored) {
            backing = &stored_backings_[signal_index];
            break;
        }
        backing = &owner_backings_[owner_slot];
        break;
    }
    }
    if (backing == nullptr || !backing->cell) {
        return { };
    }
    auto block = backing->acquire_read_block();
    if (!block) {
        return { };
    }
    return PackedLogic4PlaneReadLease { std::move(block), entry.width,
        entry.value_kind == ValueKind::logic9, backing->first_value_word,
        backing->first_logic9_word };
}

std::array<std::span<const std::uint64_t>, 4U>
AuthoritativeSignalPlanes::borrow_packed_value_planes(
    const PackedLogic4& value) noexcept
{
    std::array<std::span<const std::uint64_t>, 4U> planes { };
    if (value.width() == 0U) {
        return planes;
    }
    const auto plane_count = value.is_logic9() ? 4U : 2U;
    const auto* const backing = value.plane_backing();
    if (backing == nullptr || !backing->cell) {
        for (std::size_t plane = 0U; plane < plane_count; ++plane) {
            planes[plane] = value.is_logic9()
                ? value.logic9_plane_words(plane)
                : plane == 0U ? value.aval_words() : value.bval_words();
        }
        return planes;
    }

    // This is a synchronous borrow, not an owning snapshot. The caller must
    // finish using every span before the next publication or callback.
    const auto block = backing->cell->current.load(std::memory_order_acquire);
    if (!block) {
        return planes;
    }
    const auto words = backing->width / 64U
        + static_cast<std::size_t>(backing->width % 64U != 0U);
    for (std::size_t plane = 0U; plane < plane_count; ++plane) {
        const auto first = plane < 2U
            ? backing->first_value_word : backing->first_logic9_word;
        const auto& storage = block->planes[plane];
        if (first > storage.size() || words > storage.size() - first) {
            continue;
        }
        planes[plane] = storage.span().subspan(first, words);
    }
    return planes;
}

bool AuthoritativeSignalPlanes::try_copy_wide_logic4_role_into(
    const SignalId signal,
    const PackedPlaneRole role,
    const ProcessId owner,
    PackedLogic4& destination) const noexcept
{
    if (!valid_ || destination.width() <= 128U
        || destination.is_logic9()) {
        return false;
    }
    const auto source = plane_read_lease(signal, role, owner);
    if (!source || source.is_logic9()
        || source.width() != destination.width()) {
        return false;
    }
    const auto aval = source.plane_words(0U);
    const auto bval = source.plane_words(1U);
    const auto expected_words = destination.width() / 64U
        + static_cast<std::size_t>(destination.width() % 64U != 0U);
    if (aval.size() != expected_words || bval.size() != expected_words) {
        return false;
    }
    return destination.try_assign_wide_logic4_word_planes_noalloc(
        aval, bval);
}

AuthoritativeSignalPlanes::FrontierWriteLease::FrontierWriteLease(
    FrontierWriteLease&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr))
    , writable_signals_(std::exchange(other.writable_signals_,
          std::span<const FrontierWriteBinding> { }))
    , writable_layout_indices_(std::exchange(
          other.writable_layout_indices_, std::span<const std::size_t> { }))
    , blocks_(std::move(other.blocks_))
    , locked_(std::exchange(other.locked_,
          std::array<PackedLogic4PlaneBlock*, 4U> { }))
    , captured_generation_(std::exchange(other.captured_generation_, 0U))
    , lock_count_(std::exchange(other.lock_count_, 0U))
    , versioned_storage_(std::exchange(other.versioned_storage_, false))
    , state_changed_(std::exchange(other.state_changed_, false))
{
}

AuthoritativeSignalPlanes::FrontierWriteLease&
AuthoritativeSignalPlanes::FrontierWriteLease::operator=(
    FrontierWriteLease&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    release();
    owner_ = std::exchange(other.owner_, nullptr);
    writable_signals_ = std::exchange(other.writable_signals_,
        std::span<const FrontierWriteBinding> { });
    writable_layout_indices_ = std::exchange(
        other.writable_layout_indices_, std::span<const std::size_t> { });
    blocks_ = std::move(other.blocks_);
    locked_ = std::exchange(other.locked_,
        std::array<PackedLogic4PlaneBlock*, 4U> { });
    captured_generation_ = std::exchange(other.captured_generation_, 0U);
    lock_count_ = std::exchange(other.lock_count_, 0U);
    versioned_storage_ = std::exchange(other.versioned_storage_, false);
    state_changed_ = std::exchange(other.state_changed_, false);
    return *this;
}

AuthoritativeSignalPlanes::FrontierWriteLease::~FrontierWriteLease()
{
    release();
}

bool AuthoritativeSignalPlanes::FrontierWriteLease::plane_words(
    const SignalId signal,
    const PackedPlaneRole role,
    const ProcessId owner,
    std::array<std::span<std::uint64_t>, 4U>& planes) const noexcept
{
    planes = { };
    if (owner_ == nullptr || !owner_->valid_
        || !owner_->frontier_write_active_
        || owner_->generation_ != captured_generation_
        || owner_->versioned_storage_ready_ != versioned_storage_) {
        return false;
    }
    const auto found = std::ranges::lower_bound(
        owner_->layout_.signal_ids_, signal);
    if (found == owner_->layout_.signal_ids_.end() || *found != signal) {
        return false;
    }
    const auto signal_index = static_cast<std::size_t>(
        found - owner_->layout_.signal_ids_.begin());

    const auto writable_binding = std::ranges::find_if(
        writable_signals_, [signal, owner_process = owner](
                               const FrontierWriteBinding& binding) {
            return binding.signal == signal
                && binding.owner == owner_process;
        });
    if (writable_binding == writable_signals_.end()) {
        return false;
    }

    return plane_words_for_index(signal, signal_index, role, owner, planes);
}

bool AuthoritativeSignalPlanes::FrontierWriteLease::plane_words_at(
    const std::size_t writable_ordinal,
    const SignalId signal,
    const PackedPlaneRole role,
    const ProcessId owner,
    std::array<std::span<std::uint64_t>, 4U>& planes) const noexcept
{
    planes = { };
    if (owner_ == nullptr || !owner_->valid_
        || !owner_->frontier_write_active_
        || owner_->generation_ != captured_generation_
        || owner_->versioned_storage_ready_ != versioned_storage_
        || writable_ordinal >= writable_signals_.size()
        || writable_ordinal >= writable_layout_indices_.size()) {
        return false;
    }
    const auto& binding = writable_signals_[writable_ordinal];
    if (binding.signal != signal || binding.owner != owner) {
        return false;
    }
    const auto signal_index = writable_layout_indices_[writable_ordinal];
    if (signal_index >= owner_->layout_.signal_ids_.size()
        || owner_->layout_.signal_ids_[signal_index] != signal) {
        return false;
    }

    return plane_words_for_index(signal, signal_index, role, owner, planes);
}

bool AuthoritativeSignalPlanes::FrontierWriteLease::plane_words_for_index(
    const SignalId signal,
    const std::size_t signal_index,
    const PackedPlaneRole role,
    const ProcessId owner,
    std::array<std::span<std::uint64_t>, 4U>& planes) const noexcept
{
    // Both callers validate the active lease and resolve signal_index.
    const auto& entry = owner_->layout_.signals_[signal_index];
    if (entry.width == 0U
        || (entry.value_kind != ValueKind::logic4
            && entry.value_kind != ValueKind::logic9)
        || owner_->signal_seeded_[signal_index] == 0U) {
        return false;
    }

    auto role_index = std::size_t { };
    auto storage_role = role;
    auto first_value_word = entry.first_value_word;
    auto first_logic9_word = entry.first_logic9_word;
    switch (role) {
    case PackedPlaneRole::current:
        role_index = 0U;
        break;
    case PackedPlaneRole::previous:
        role_index = 1U;
        break;
    case PackedPlaneRole::stored:
        role_index = 2U;
        break;
    case PackedPlaneRole::owner: {
        const auto owners = owner_->layout_.owners(signal);
        const auto owner_slot = std::ranges::find_if(owners,
            [owner](const SignalDriverOwnerLayout& candidate) {
                return candidate.process == owner;
            });
        if (owner_slot == owners.end()) {
            return false;
        }
        const auto owner_index = entry.first_owner
            + static_cast<std::size_t>(owner_slot - owners.begin());
        if (owner_->owner_seeded_[owner_->owner_flat_index(owner_index)]
            == 0U) {
            return false;
        }
        if (owner_slot->aliases_stored) {
            storage_role = PackedPlaneRole::stored;
            role_index = 2U;
        } else {
            role_index = 3U;
            first_value_word = owner_slot->first_value_word;
            first_logic9_word = owner_slot->first_logic9_word;
        }
        break;
    }
    default:
        return false;
    }
    auto& role_words = owner_->role_words(storage_role);
    PackedLogic4PlaneBlock* captured_block { };
    if (versioned_storage_) {
        // Acquisition checked cell identity under this block's write lock.
        // Live-slot mutation is serialized with this synchronous lease, and
        // A4 publication rejects an active lease. Reuse the retained capture.
        captured_block = blocks_[role_index].get();
        if (captured_block == nullptr || !role_words.cell) {
            return false;
        }
    } else if (role_words.cell) {
        return false;
    }
    const auto plane_count = entry.value_kind == ValueKind::logic9
        ? std::size_t { 4U } : std::size_t { 2U };
    for (std::size_t plane = 0U; plane < plane_count; ++plane) {
        const auto first_word = plane < 2U
            ? first_value_word : first_logic9_word;
        auto& storage = versioned_storage_
            ? captured_block->planes[plane] : role_words.planes[plane];
        if (first_word > storage.size()
            || entry.word_count > storage.size() - first_word) {
            planes = { };
            return false;
        }
        planes[plane] = storage.span().subspan(
            first_word, entry.word_count);
        if (planes[plane].size() != entry.word_count) {
            planes = { };
            return false;
        }
    }
    return true;
}

bool AuthoritativeSignalPlanes::FrontierWriteLease::current_plane_words(
    const SignalId signal,
    std::array<std::span<const std::uint64_t>, 4U>& planes) const noexcept
{
    planes = { };
    if (owner_ == nullptr || !owner_->valid_
        || !owner_->frontier_write_active_
        || owner_->generation_ != captured_generation_
        || owner_->versioned_storage_ready_ != versioned_storage_) {
        return false;
    }
    if (versioned_storage_) {
        if (!blocks_[0] || !owner_->current_.cell
            || owner_->current_.cell->current.load(std::memory_order_acquire)
                != blocks_[0]) {
            return false;
        }
    } else if (owner_->current_.cell) {
        return false;
    }
    const auto found = std::ranges::lower_bound(
        owner_->layout_.signal_ids_, signal);
    if (found == owner_->layout_.signal_ids_.end() || *found != signal) {
        return false;
    }
    const auto signal_index = static_cast<std::size_t>(
        found - owner_->layout_.signal_ids_.begin());
    const auto& entry = owner_->layout_.signals_[signal_index];
    if (entry.width == 0U
        || (entry.value_kind != ValueKind::logic4
            && entry.value_kind != ValueKind::logic9)
        || owner_->signal_seeded_[signal_index] == 0U) {
        return false;
    }
    const auto words = static_cast<std::size_t>(entry.word_count);
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        if (plane >= 2U && entry.value_kind != ValueKind::logic9) {
            continue;
        }
        const auto first = plane < 2U
            ? entry.first_value_word : entry.first_logic9_word;
        const auto& storage = versioned_storage_
            ? blocks_[0]->planes[plane] : owner_->current_.planes[plane];
        if (first > storage.size() || words > storage.size() - first) {
            planes = { };
            return false;
        }
        planes[plane] = storage.span().subspan(first, words);
    }
    return true;
}

void AuthoritativeSignalPlanes::FrontierWriteLease::note_value_change(
    const SignalId signal) noexcept
{
    if (owner_ == nullptr) {
        return;
    }
    if (!owner_->valid_ || !owner_->frontier_write_active_
        || owner_->generation_ != captured_generation_) {
        owner_->valid_ = false;
        return;
    }
    if (std::ranges::none_of(writable_signals_,
            [signal](const FrontierWriteBinding& binding) {
                return binding.signal == signal;
            })) {
        return;
    }
    const auto found = std::ranges::lower_bound(
        owner_->layout_.signal_ids_, signal);
    if (found == owner_->layout_.signal_ids_.end() || *found != signal) {
        return;
    }
    const auto index = static_cast<std::size_t>(
        found - owner_->layout_.signal_ids_.begin());
    owner_->dirty_signals_[index] = 1U;
    state_changed_ = true;
}

void AuthoritativeSignalPlanes::FrontierWriteLease::release() noexcept
{
    if (owner_ == nullptr) {
        return;
    }
    if (owner_->generation_ != captured_generation_) {
        owner_->valid_ = false;
        state_changed_ = false;
    }
    if (state_changed_) {
        owner_->advance_generation();
    }
    while (lock_count_ != 0U) {
        --lock_count_;
        locked_[lock_count_]->end_write();
        locked_[lock_count_] = nullptr;
    }
    owner_->frontier_write_active_ = false;
    blocks_ = { };
    writable_signals_ = { };
    writable_layout_indices_ = { };
    owner_ = nullptr;
    captured_generation_ = 0U;
    versioned_storage_ = false;
    state_changed_ = false;
}

bool AuthoritativeSignalPlanes::try_acquire_frontier_write_lease(
    const std::uint64_t expected_generation,
    const std::span<const FrontierWriteBinding> writable_signals,
    FrontierWriteLease& lease,
    const std::span<std::size_t> writable_layout_indices) noexcept
{
    if (lease.active()) {
        return false;
    }
    lease.writable_signals_ = { };
    lease.writable_layout_indices_ = { };
    if ((!writable_layout_indices.empty()
            && writable_layout_indices.size() != writable_signals.size())
        || !valid_ || !packed_slots_bound_
        || frontier_write_active_
        || generation_ != expected_generation || !generation_can_advance()
        || writable_signals.empty()) {
        return false;
    }
    const bool versioned_storage = versioned_storage_ready_;
    if (!versioned_storage
        && packed_slot_policy_ != PackedSlotBindingPolicy::narrow_only) {
        return false;
    }

    bool needs_owner_values = false;
    for (std::size_t index = 0U; index < writable_signals.size(); ++index) {
        const auto& binding = writable_signals[index];
        const auto found = std::ranges::lower_bound(
            layout_.signal_ids_, binding.signal);
        if (found == layout_.signal_ids_.end() || *found != binding.signal) {
            return false;
        }
        const auto signal_index = static_cast<std::size_t>(
            found - layout_.signal_ids_.begin());
        const auto& entry = layout_.signals_[signal_index];
        const auto owner_index = layout_.owner_index(
            binding.signal, binding.owner);
        if ((entry.value_kind != ValueKind::logic4
                && entry.value_kind != ValueKind::logic9)
            || entry.width == 0U
            || (!versioned_storage && entry.width > 64U)
            || entry.storage_class != SignalDriverStorageClass::single_owner
            || entry.owner_count != 1U || owner_index == no_word
            || !packed_signal_slots_bound(binding.signal)) {
            return false;
        }
        // The slot check covers supported storage and every owner binding.
        // With one owner and the exact owner index above, it also proves this
        // writable binding's owner slot without checking it a second time.
        const auto& owner_layout = layout_.owners_[owner_index];
        if (versioned_storage) {
            if (owner_layout.aliases_stored) {
                if (owner_backings_[owner_index].cell) {
                    return false;
                }
            } else if (!owner_backings_[owner_index].cell) {
                return false;
            }
        } else if (owner_backings_[owner_index].cell) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (writable_signals[earlier].signal == binding.signal) {
                return false;
            }
        }
        if (!writable_layout_indices.empty()) {
            writable_layout_indices[index] = signal_index;
        }
        needs_owner_values = needs_owner_values
            || !layout_.owners_[owner_index].aliases_stored;
    }

    std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 4U> blocks;
    std::array<PackedLogic4PlaneBlock*, 4U> locked { };
    std::size_t lock_count { };
    const auto acquire_role = [&blocks, &locked, &lock_count](
                                  const std::size_t role,
                                  const PlaneWords& words) noexcept {
        if (!words.cell || role >= blocks.size()) {
            return false;
        }
        auto block = words.cell->current.load(std::memory_order_acquire);
        if (!block) {
            return false;
        }
        blocks[role] = block;
        for (std::size_t prior = 0U; prior < role; ++prior) {
            if (blocks[prior] == block) {
                return true;
            }
        }
        if (!block->try_begin_write()) {
            return false;
        }
        if (words.cell->current.load(std::memory_order_acquire) != block) {
            block->end_write();
            return false;
        }
        locked[lock_count] = block.get();
        ++lock_count;
        return true;
    };
    const auto release_locks = [&locked, &lock_count]() noexcept {
        while (lock_count != 0U) {
            --lock_count;
            locked[lock_count]->end_write();
            locked[lock_count] = nullptr;
        }
    };

    if (versioned_storage) {
        if (!acquire_role(0U, current_)
            || !acquire_role(1U, previous_)
            || !acquire_role(2U, stored_)
            || (needs_owner_values
                && !acquire_role(3U, owner_values_))) {
            release_locks();
            return false;
        }
    } else if (current_.cell || previous_.cell || stored_.cell
        || owner_values_.cell) {
        return false;
    }

    lease.owner_ = this;
    lease.writable_signals_ = writable_signals;
    lease.writable_layout_indices_ = writable_layout_indices;
    lease.blocks_ = std::move(blocks);
    lease.locked_ = locked;
    lease.captured_generation_ = expected_generation;
    lease.lock_count_ = lock_count;
    lease.versioned_storage_ = versioned_storage;
    lease.state_changed_ = false;
    frontier_write_active_ = true;
    return true;
}

void AuthoritativeSignalPlanes::clear_dirty() noexcept
{
    if (frontier_write_active_) {
        valid_ = false;
        return;
    }
    std::ranges::fill(dirty_signals_, 0U);
}

AuthoritativeSignalPlanes::ComponentPlaneWordCounts
AuthoritativeSignalPlanes::component_plane_word_counts() const noexcept
{
    const std::array<const PlaneWords*, 4U> roles {
        &current_, &previous_, &stored_, &owner_values_ };
    ComponentPlaneWordCounts result { };
    for (std::size_t role = 0U; role < roles.size(); ++role) {
        const auto* const words = roles[role];
        const auto block = words->cell
            ? words->cell->current.load(std::memory_order_acquire)
            : std::shared_ptr<PackedLogic4PlaneBlock> { };
        for (std::size_t plane = 0U; plane < result[role].size(); ++plane) {
            result[role][plane] = block
                ? block->planes[plane].size()
                : words->planes[plane].size();
        }
    }
    return result;
}

bool AuthoritativeSignalPlanes::rehome_component_planes(
    std::shared_ptr<void> lifetime,
    const ComponentPlaneSpans& slices) noexcept
{
    if (!lifetime || !valid_ || packed_slots_bound_
        || frontier_write_active_ || component_storage_owner_) {
        return false;
    }

    std::array<PlaneWords*, 4U> roles {
        &current_, &previous_, &stored_, &owner_values_ };
    std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 4U> blocks;
    for (std::size_t role = 0U; role < roles.size(); ++role) {
        auto& words = *roles[role];
        if (words.cell) {
            blocks[role] = words.cell->current.load(
                std::memory_order_acquire);
            const auto& block = blocks[role];
            if (!block || block.use_count() != 2U
                || block->storage_owner || block->has_read_pins()
                || block->write_locked()
                || std::ranges::any_of(words.retired_blocks,
                    [](const auto& retired) { return retired != nullptr; })) {
                return false;
            }
            for (std::size_t previous = 0U; previous < role; ++previous) {
                if (blocks[previous] == block) {
                    return false;
                }
            }
        } else if (std::ranges::any_of(words.planes,
                       [](const auto& plane) { return plane.borrowed(); })) {
            return false;
        }
    }

    for (std::size_t role = 0U; role < roles.size(); ++role) {
        const auto& words = *roles[role];
        const auto& source = blocks[role]
            ? blocks[role]->planes : words.planes;
        for (std::size_t plane = 0U; plane < source.size(); ++plane) {
            if (slices[role][plane].size() != source[plane].size()
                || (!slices[role][plane].empty()
                    && slices[role][plane].data() == nullptr)) {
                return false;
            }
        }
    }

    const auto address_range = [](const std::uint64_t* const data,
                                   const std::size_t count,
                                   std::uintptr_t& begin,
                                   std::uintptr_t& end) noexcept {
        if (count == 0U) {
            begin = 0U;
            end = 0U;
            return true;
        }
        if (data == nullptr
            || count > std::numeric_limits<std::uintptr_t>::max()
                    / sizeof(std::uint64_t)) {
            return false;
        }
        begin = reinterpret_cast<std::uintptr_t>(data);
        if (begin % alignof(std::uint64_t) != 0U) {
            return false;
        }
        const auto bytes
            = static_cast<std::uintptr_t>(count)
            * sizeof(std::uint64_t);
        if (bytes > std::numeric_limits<std::uintptr_t>::max() - begin) {
            return false;
        }
        end = begin + bytes;
        return true;
    };
    for (std::size_t role = 0U; role < roles.size(); ++role) {
        for (std::size_t plane = 0U; plane < slices[role].size(); ++plane) {
            std::uintptr_t target_begin { };
            std::uintptr_t target_end { };
            if (!address_range(slices[role][plane].data(),
                    slices[role][plane].size(), target_begin, target_end)) {
                return false;
            }
            if (target_begin == target_end) {
                continue;
            }
            for (std::size_t previous_role = 0U;
                 previous_role <= role; ++previous_role) {
                const auto end_plane = previous_role == role
                    ? plane : slices[previous_role].size();
                for (std::size_t previous_plane = 0U;
                     previous_plane < end_plane; ++previous_plane) {
                    std::uintptr_t other_begin { };
                    std::uintptr_t other_end { };
                    const auto other = slices[previous_role][previous_plane];
                    if (!address_range(other.data(), other.size(),
                            other_begin, other_end)
                        || (target_begin < other_end
                            && other_begin < target_end)) {
                        return false;
                    }
                }
            }
            for (std::size_t source_role = 0U;
                 source_role < roles.size(); ++source_role) {
                const auto& source_planes = blocks[source_role]
                    ? blocks[source_role]->planes : roles[source_role]->planes;
                for (const auto& source_plane : source_planes) {
                    std::uintptr_t source_begin { };
                    std::uintptr_t source_end { };
                    if (!address_range(source_plane.data(),
                            source_plane.size(), source_begin, source_end)
                        || (target_begin < source_end
                            && source_begin < target_end)) {
                        return false;
                    }
                }
            }
        }
    }

    // All validation is complete. Copying words and rebinding descriptors
    // are nonthrowing, so no role can be left half-rehomed on failure.
    for (std::size_t role = 0U; role < roles.size(); ++role) {
        const auto& source = blocks[role]
            ? blocks[role]->planes : roles[role]->planes;
        for (std::size_t plane = 0U; plane < source.size(); ++plane) {
            std::ranges::copy(source[plane], slices[role][plane].begin());
        }
    }
    for (std::size_t role = 0U; role < roles.size(); ++role) {
        if (blocks[role]) {
            for (std::size_t plane = 0U; plane < slices[role].size(); ++plane) {
                blocks[role]->planes[plane].bind(slices[role][plane]);
            }
            blocks[role]->storage_owner = lifetime;
        } else {
            for (std::size_t plane = 0U; plane < slices[role].size(); ++plane) {
                roles[role]->planes[plane].bind(slices[role][plane]);
            }
        }
    }
    component_storage_owner_ = std::move(lifetime);

    const auto refresh_unversioned_backings = [](
        PlaneWords& words,
        const SignalDriverSignalLayout& signal,
        const std::size_t value_word,
        const std::size_t logic9_word,
        PackedLogic4PlaneBacking& backing) {
        if (words.cell) {
            return;
        }
        const auto count = word_count(signal.width);
        auto& planes = backing.unversioned_planes;
        planes[0U] = words.planes[0U].span().subspan(value_word, count);
        planes[1U] = words.planes[1U].span().subspan(value_word, count);
        if (signal.value_kind == ValueKind::logic9) {
            planes[2U] = words.planes[2U].span().subspan(logic9_word, count);
            planes[3U] = words.planes[3U].span().subspan(logic9_word, count);
        }
    };
    for (std::size_t index = 0U; index < layout_.signals_.size(); ++index) {
        const auto& signal = layout_.signals_[index];
        if (signal.width == 0U
            || (signal.value_kind != ValueKind::logic4
                && signal.value_kind != ValueKind::logic9)) {
            continue;
        }
        refresh_unversioned_backings(current_, signal,
            signal.first_value_word, signal.first_logic9_word,
            current_backings_[index]);
        refresh_unversioned_backings(previous_, signal,
            signal.first_value_word, signal.first_logic9_word,
            previous_backings_[index]);
        refresh_unversioned_backings(stored_, signal,
            signal.first_value_word, signal.first_logic9_word,
            stored_backings_[index]);
        for (std::size_t owner = signal.first_owner;
             owner < signal.first_owner + signal.owner_count; ++owner) {
            const auto& owner_layout = layout_.owners_[owner];
            if (!owner_layout.aliases_stored) {
                refresh_unversioned_backings(owner_values_, signal,
                    owner_layout.first_value_word,
                    owner_layout.first_logic9_word,
                    owner_backings_[owner]);
            }
        }
    }
    return true;
}

RegionReadyMask::RegionReadyMask(const std::size_t member_count)
    : member_bits_(member_count / 64U
        + (member_count % 64U == 0U ? 0U : 1U))
    , trigger_masks_(member_count)
{
}

bool RegionReadyMask::mark(
    const std::size_t member, const std::uint64_t trigger_mask) noexcept
{
    if (member >= trigger_masks_.size()) {
        return false;
    }
    if (trigger_mask == 0U) {
        return false;
    }
    const auto word = member / 64U;
    const auto bit = UINT64_C(1) << (member % 64U);
    const bool was_ready = (member_bits_[word] & bit) != 0U;
    member_bits_[word] |= bit;
    trigger_masks_[member] |= trigger_mask;
    return !was_ready;
}

bool RegionReadyMask::mark_mapped(
    const std::size_t member, const std::size_t word,
    const std::uint64_t bit, const std::uint64_t trigger_mask) noexcept
{
    if (member >= trigger_masks_.size() || word >= member_bits_.size()
        || bit == 0U || (bit & (bit - 1U)) != 0U
        || trigger_mask == 0U
        || word != member / 64U
        || bit != (UINT64_C(1) << (member % 64U))) {
        return false;
    }
    const bool was_ready = (member_bits_[word] & bit) != 0U;
    member_bits_[word] |= bit;
    trigger_masks_[member] |= trigger_mask;
    return !was_ready;
}

bool RegionReadyMask::take_mapped(
    const std::size_t member, const std::size_t word,
    const std::uint64_t bit, std::uint64_t& trigger_mask) noexcept
{
    trigger_mask = 0U;
    if (member >= trigger_masks_.size() || word >= member_bits_.size()
        || bit == 0U || (bit & (bit - 1U)) != 0U
        || word != member / 64U
        || bit != (UINT64_C(1) << (member % 64U))
        || (member_bits_[word] & bit) == 0U) {
        return false;
    }
    trigger_mask = trigger_masks_[member];
    member_bits_[word] &= ~bit;
    trigger_masks_[member] = 0U;
    return trigger_mask != 0U;
}

bool RegionReadyMask::ready(const std::size_t member) const noexcept
{
    if (member >= trigger_masks_.size()) {
        return false;
    }
    const auto word = member / 64U;
    const auto bit = UINT64_C(1) << (member % 64U);
    return (member_bits_[word] & bit) != 0U;
}

std::uint64_t RegionReadyMask::trigger_mask(
    const std::size_t member) const noexcept
{
    return member < trigger_masks_.size() ? trigger_masks_[member] : 0U;
}

void RegionReadyMask::clear(const std::size_t member) noexcept
{
    if (member >= trigger_masks_.size()) {
        return;
    }
    const auto word = member / 64U;
    const auto bit = UINT64_C(1) << (member % 64U);
    member_bits_[word] &= ~bit;
    trigger_masks_[member] = 0U;
}

void RegionReadyMask::clear_all() noexcept
{
    std::ranges::fill(member_bits_, 0U);
    std::ranges::fill(trigger_masks_, 0U);
}

RegionGroupedFanout RegionGroupedFanout::build(
    const std::span<const Process* const> programs,
    const std::span<const ProcessId> members)
{
    struct PendingConsumer {
        std::size_t member_index { };
        std::vector<RegionFanoutClause> clauses;
    };
    std::map<SignalId, std::map<ProcessId, PendingConsumer>> pending;
    std::map<ProcessId, std::size_t> member_index;
    for (std::size_t index = 0U; index < members.size(); ++index) {
        const auto process_id = members[index];
        if (process_id >= programs.size() || programs[process_id] == nullptr
            || !member_index.emplace(process_id, index).second) {
            throw std::invalid_argument {
                "grouped fanout has an invalid or duplicate member"
            };
        }
    }

    for (const auto& [process_id, index] : member_index) {
        const auto& process = *programs[process_id];
        for (std::size_t sensitivity_index = 0U;
            sensitivity_index < process.static_sensitivity.size();
            ++sensitivity_index) {
            const auto& sensitivity
                = process.static_sensitivity[sensitivity_index];
            const auto trigger_mask = sensitivity_index < 63U
                    && !process.static_trigger_regions.empty()
                ? (UINT64_C(1) << sensitivity_index)
                : Process::full_static_trigger_mask;
            auto& consumer = pending[sensitivity.signal][process_id];
            consumer.member_index = index;
            consumer.clauses.push_back({ sensitivity.edge,
                sensitivity.offset, sensitivity.width, trigger_mask });
        }
    }

    RegionGroupedFanout result;
    std::size_t consumer_count { };
    std::size_t clause_count { };
    for (const auto& [signal, consumers] : pending) {
        static_cast<void>(signal);
        consumer_count = checked_add(consumer_count, consumers.size(),
            "region grouped fanout has too many consumers");
        for (const auto& [process, consumer] : consumers) {
            static_cast<void>(process);
            clause_count = checked_add(clause_count,
                consumer.clauses.size(),
                "region grouped fanout has too many clauses");
        }
    }
    result.groups_.reserve(pending.size());
    result.consumers_.reserve(consumer_count);
    result.clauses_.reserve(clause_count);
    for (const auto& [signal, consumers] : pending) {
        RegionFanoutSignalGroup group;
        group.signal = signal;
        group.first_consumer = result.consumers_.size();
        group.consumer_count = consumers.size();
        for (const auto& [process, consumer] : consumers) {
            RegionFanoutConsumer flattened;
            flattened.process = process;
            flattened.member_index = consumer.member_index;
            flattened.first_clause = result.clauses_.size();
            flattened.clause_count = consumer.clauses.size();
            result.clauses_.insert(result.clauses_.end(),
                consumer.clauses.begin(), consumer.clauses.end());
            result.consumers_.push_back(flattened);
        }
        result.groups_.push_back(group);
    }
    return result;
}

void RegionGroupedFanout::mark_transition(
    const SignalId signal,
    const PackedLogic4& previous,
    const PackedLogic4& current,
    const EdgeKind edge,
    RegionReadyMask& readiness) const noexcept
{
    const auto group = std::ranges::lower_bound(groups_, signal,
        std::ranges::less { }, &RegionFanoutSignalGroup::signal);
    if (group == groups_.end() || group->signal != signal) {
        return;
    }
    const auto consumers = std::span<const RegionFanoutConsumer> { consumers_ }
        .subspan(group->first_consumer, group->consumer_count);
    for (const auto& consumer : consumers) {
        std::uint64_t trigger_mask { };
        const auto clauses = std::span<const RegionFanoutClause> { clauses_ }
            .subspan(consumer.first_clause, consumer.clause_count);
        for (const auto& clause : clauses) {
            if (clause.edge != edge) {
                continue;
            }
            bool matches { };
            if (clause.edge == EdgeKind::any) {
                matches = clause.width == 0U && clause.offset == 0U
                    ? previous != current
                    : sensitivity_range_changed(previous, current,
                        clause.offset, clause.width);
            } else if ((edge == EdgeKind::posedge
                           || edge == EdgeKind::negedge)
                && previous.width() == 1U && current.width() == 1U) {
                matches = edge_matches(clause.edge,
                    edge_value(previous), edge_value(current));
            } else if (edge == EdgeKind::posedge
                || edge == EdgeKind::negedge) {
                // An invalid/non-scalar edge shape must not lose a wakeup.
                matches = true;
            }
            if (matches) {
                trigger_mask |= clause.trigger_mask;
            }
        }
        if (trigger_mask != 0U) {
            static_cast<void>(readiness.mark(
                consumer.member_index, trigger_mask));
        }
    }
}

void RegionGroupedFanout::mark_transaction(
    const SignalId signal, RegionReadyMask& readiness) const noexcept
{
    const auto group = std::ranges::lower_bound(groups_, signal,
        std::ranges::less { }, &RegionFanoutSignalGroup::signal);
    if (group == groups_.end() || group->signal != signal) {
        return;
    }
    const auto consumers = std::span<const RegionFanoutConsumer> { consumers_ }
        .subspan(group->first_consumer, group->consumer_count);
    for (const auto& consumer : consumers) {
        std::uint64_t trigger_mask { };
        const auto clauses = std::span<const RegionFanoutClause> { clauses_ }
            .subspan(consumer.first_clause, consumer.clause_count);
        for (const auto& clause : clauses) {
            if (clause.edge == EdgeKind::transaction) {
                trigger_mask |= clause.trigger_mask;
            }
        }
        if (trigger_mask != 0U) {
            static_cast<void>(readiness.mark(
                consumer.member_index, trigger_mask));
        }
    }
}

RegionAuthoritativeComponentState::RegionAuthoritativeComponentState(
    const std::uint64_t generation,
    SignalDriverLayout layout,
    RegionGroupedFanout fanout,
    const std::size_t member_count,
    const PackedSlotBindingPolicy packed_slot_policy)
    : generation_(generation)
    , values_(std::move(layout), packed_slot_policy)
    , fanout_(std::move(fanout))
    , readiness_(member_count)
{
    if (generation_ == 0U) {
        throw std::invalid_argument {
            "component state cannot use the invalid generation zero"
        };
    }
    if (packed_slot_policy == PackedSlotBindingPolicy::experimental_wide
        || packed_slot_policy
            == PackedSlotBindingPolicy::experimental_wide_disjoint_owners) {
        wide_mutation_scratch_.words.reserve(
            values_.layout().max_signal_word_count());
    }
}

} // namespace fsim::runtime::simir
