// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace fsim::runtime::simir {

// Diagnostic causes can overlap. The exact mask accumulated before each
// successful full copy is one histogram bucket; per-bit projections overlap.
enum class FrontierMemberSyncFullReason : std::uint8_t {
    seed = 1U << 0U,
    nonprivate_entry = 1U << 1U,
    unavailable_workset = 1U << 2U,
    boundary_publication = 1U << 3U,
    invalid_journal = 1U << 4U,
    uncertain_status_or_generation = 1U << 5U,
};
inline constexpr std::size_t kFrontierMemberSyncFullReasonCombinations = 64U;

// Compiler-independent, fixed-topology selection scratch. It grants no
// private-entry admission and no baseline or callback lifetime authorization.
// Callers must establish those separately before replacing full validation.
class FrontierMemberSyncWorkset final {
public:
    [[nodiscard]] bool initialize(const RegionFrontierLayoutV2& layout)
    {
        if (layout.member_count == 0U || layout.members == nullptr
            || (layout.fanout_edge_count != 0U && layout.fanout_edges == nullptr)) {
            return false;
        }
        member_count_ = layout.member_count;
        selected_.assign(member_count_, 0U);
        dirty_words_.assign(
            static_cast<std::size_t>(member_count_ / 64U)
                + static_cast<std::size_t>(member_count_ % 64U != 0U),
            0U);
        selected_count_ = 0U;
        const auto signal_count
            = static_cast<std::size_t>(layout.signal_slot_count);
        if (signal_count == std::numeric_limits<std::size_t>::max()
            || signal_count + 1U > fanout_offsets_.max_size()) {
            throw std::length_error { "member synchronization fanout offsets" };
        }
        fanout_offsets_.assign(signal_count + 1U, 0U);
        fanout_members_.assign(layout.fanout_edge_count, 0U);
        for (std::size_t index = 0U; index < layout.fanout_edge_count; ++index) {
            const auto& edge = layout.fanout_edges[index];
            if (edge.signal_slot >= layout.signal_slot_count
                || edge.member_index >= member_count_) {
                return false;
            }
            ++fanout_offsets_[static_cast<std::size_t>(edge.signal_slot) + 1U];
        }
        for (std::size_t slot = 1U; slot < fanout_offsets_.size(); ++slot) {
            fanout_offsets_[slot] += fanout_offsets_[slot - 1U];
        }
        auto insertion_offsets = fanout_offsets_;
        for (std::size_t index = 0U; index < layout.fanout_edge_count; ++index) {
            const auto& edge = layout.fanout_edges[index];
            fanout_members_[insertion_offsets[edge.signal_slot]++] = edge.member_index;
        }
        return true;
    }

    [[nodiscard]] bool mark(const std::uint32_t member) noexcept
    {
        if (member >= member_count_) {
            return false;
        }
        const auto word = static_cast<std::size_t>(member / 64U);
        const auto bit = UINT64_C(1) << (member % 64U);
        if ((dirty_words_[word] & bit) != 0U) {
            return true;
        }
        if (selected_count_ >= selected_.size()) {
            return false;
        }
        dirty_words_[word] |= bit;
        selected_[selected_count_++] = member;
        return true;
    }

    // Called before the commit log is cleared. Range-filtered fanout edges
    // are conservatively included: already queued readers may receive only a
    // trigger-mask update and produce no staged activation event.
    [[nodiscard]] bool mark_changed_commit(
        const RegionFrontierCommittedSignalV2& committed) noexcept
    {
        if (committed.changed > 1U || committed.state_changed > 1U
            || (committed.changed != 0U && committed.state_changed == 0U)
            || fanout_offsets_.empty()
            || static_cast<std::size_t>(committed.signal_slot)
                >= fanout_offsets_.size() - 1U) {
            return false;
        }
        if (committed.changed == 0U) {
            return true;
        }
        const auto begin = fanout_offsets_[committed.signal_slot];
        const auto end = fanout_offsets_[
            static_cast<std::size_t>(committed.signal_slot) + 1U];
        for (auto edge = begin; edge < end; ++edge) {
            if (!mark(fanout_members_[edge])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool mark_activation_payload(const std::uint64_t payload) noexcept
    {
        const auto kind = static_cast<std::uint32_t>(
            payload >> kRegionFrontierPayloadKindShiftV2);
        if (kind != static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::member_activation)) {
            return true;
        }
        const auto index = payload & kRegionFrontierPayloadIndexMaskV2;
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        return mark(static_cast<std::uint32_t>(index));
    }

    [[nodiscard]] std::span<const std::uint32_t> selected() const noexcept
    {
        return { selected_.data(), selected_count_ };
    }

    [[nodiscard]] std::size_t retained_capacity_bytes() const noexcept
    {
        return dirty_words_.capacity() * sizeof(std::uint64_t)
            + selected_.capacity() * sizeof(std::uint32_t)
            + fanout_offsets_.capacity() * sizeof(std::uint32_t)
            + fanout_members_.capacity() * sizeof(std::uint32_t);
    }

    void clear() noexcept
    {
        for (std::size_t index = 0U; index < selected_count_; ++index) {
            const auto member = selected_[index];
            dirty_words_[member / 64U] &= ~(UINT64_C(1) << (member % 64U));
        }
        selected_count_ = 0U;
    }

private:
    std::uint32_t member_count_ { };
    std::size_t selected_count_ { };
    std::vector<std::uint64_t> dirty_words_;
    std::vector<std::uint32_t> selected_;
    std::vector<std::uint32_t> fanout_offsets_;
    std::vector<std::uint32_t> fanout_members_;
};

} // namespace fsim::runtime::simir
