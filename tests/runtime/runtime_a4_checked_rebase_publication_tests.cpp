// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using fsim::tests::runtime::staging_failure_support::
    allocation_failure_was_injected;
using fsim::tests::runtime::staging_failure_support::
    arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::
    begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::
    clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::
    end_allocation_count;
using fsim::tests::runtime::staging_failure_support::require;

constexpr auto kUnrelatedSignal = SignalId { 0U };
constexpr auto kFirstSignal = SignalId { 1U };
constexpr auto kSecondSignal = SignalId { 2U };
constexpr auto kFirstOwner = ProcessId { 0U };
constexpr auto kSecondOwner = ProcessId { 1U };

Process whole_writer(const ProcessId process_id, const SignalId output)
{
    Process process;
    process.id = process_id;
    process.name = "checked_group_writer_" + std::to_string(process_id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.static_sensitivity = { { kUnrelatedSignal, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, kUnrelatedSignal },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

RegionGraph make_graph(const std::uint32_t width, const ValueKind kind)
{
    std::array descriptors {
        RegionSignalDescriptor { width },
        RegionSignalDescriptor { width },
        RegionSignalDescriptor { width },
    };
    for (auto& descriptor : descriptors) {
        descriptor.value_kind = kind;
    }
    const auto first = whole_writer(kFirstOwner, kFirstSignal);
    const auto second = whole_writer(kSecondOwner, kSecondSignal);
    const std::array<const Process*, 2U> processes { &first, &second };
    return RegionGraph::build(processes, descriptors);
}

SignalDriverLayout make_layout(
    const RegionGraph& graph, const bool alias_second_owner)
{
    const std::array<SignalId, 3U> signals {
        kUnrelatedSignal, kFirstSignal, kSecondSignal
    };
    const std::array<SignalId, 0U> no_slice_certificates { };
    const std::array<SignalId, 1U> stored_owner_aliases { kSecondSignal };
    return SignalDriverLayout::build(graph, signals,
        no_slice_certificates,
        alias_second_owner
            ? std::span<const SignalId> { stored_owner_aliases }
            : std::span<const SignalId> { });
}

PackedLogic4 value_for(const std::uint32_t width,
    const ValueKind kind, const std::size_t phase)
{
    static constexpr std::string_view logic4_digits { "01XZ" };
    static constexpr std::string_view logic9_digits { "UX01ZWLH-" };
    const auto digits = kind == ValueKind::logic9
        ? logic9_digits : logic4_digits;
    std::string text;
    text.reserve(width);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        text.push_back(digits[(bit + phase * 3U) % digits.size()]);
    }
    auto phase_code = phase;
    const auto distinct_phase_bits = std::min<std::size_t>(
        width, kind == ValueKind::logic9 ? 2U : 3U);
    for (std::size_t bit = 0U; bit < distinct_phase_bits; ++bit) {
        text[width - bit - 1U] = digits[phase_code % digits.size()];
        phase_code /= digits.size();
    }
    return kind == ValueKind::logic9
        ? PackedLogic4::from_logic9_msb_string(text)
        : PackedLogic4::from_msb_string(text);
}

bool lease_matches(const PackedLogic4PlaneReadLease& lease,
    const PackedLogic4& expected)
{
    if (!lease || lease.width() != expected.width()
        || lease.is_logic9() != expected.is_logic9()) {
        return false;
    }
    const auto plane_count = expected.is_logic9() ? 4U : 2U;
    for (std::size_t plane = 0U; plane < plane_count; ++plane) {
        const auto expected_words = plane == 0U ? expected.aval_words()
            : plane == 1U ? expected.bval_words()
                          : expected.logic9_plane_words(plane);
        if (!std::ranges::equal(lease.plane_words(plane), expected_words)) {
            return false;
        }
    }
    return true;
}

bool contains_all_logic9_states(const PackedLogic4& value)
{
    if (!value.is_logic9()) {
        return false;
    }
    std::array<bool, 9U> seen { };
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        const auto state = static_cast<std::size_t>(value.get_logic9(bit));
        if (state >= seen.size()) {
            return false;
        }
        seen[state] = true;
    }
    return std::ranges::all_of(seen, [](const bool present) {
        return present;
    });
}

bool lease_has_new_backing(const PackedLogic4PlaneReadLease& old_lease,
    const PackedLogic4PlaneReadLease& new_lease)
{
    if (!old_lease || !new_lease || old_lease.width() != new_lease.width()
        || old_lease.is_logic9() != new_lease.is_logic9()) {
        return false;
    }
    const auto plane_count = old_lease.is_logic9() ? 4U : 2U;
    for (std::size_t plane = 0U; plane < plane_count; ++plane) {
        if (old_lease.plane_words(plane).data()
            == new_lease.plane_words(plane).data()) {
            return false;
        }
    }
    return true;
}

struct RoleValues {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 owner;

    friend bool operator==(const RoleValues&, const RoleValues&) = default;
};

struct FixtureState {
    std::array<RoleValues, 2U> targets;
    PackedLogic4 unrelated_current;
    PackedLogic4 unrelated_previous;
    PackedLogic4 unrelated_stored;
    std::array<std::uint8_t, 3U> dirty { };
    std::array<bool, 2U> signal_slots_bound { };
    std::array<bool, 2U> owner_slots_bound { };
    std::size_t packed_slot_count { };
    bool packed_slots_bound { };
    std::uint64_t revision { };
    bool valid { };
};

class Fixture final {
public:
    Fixture(const std::uint32_t width, const ValueKind kind,
        const bool alias_second_owner = false,
        const PackedSlotBindingPolicy policy
            = PackedSlotBindingPolicy::experimental_wide)
        : width_ { width }
        , kind_ { kind }
        , alias_second_owner_ { alias_second_owner }
        , graph_ { make_graph(width, kind) }
        , layout_ { make_layout(graph_, alias_second_owner) }
        , values_ { layout_, policy }
    {
        if (width_ == 1U && kind_ == ValueKind::logic4) {
            current_ = { value_for(width_, kind_, 0U),
                value_for(width_, kind_, 0U),
                value_for(width_, kind_, 3U) };
            previous_ = { value_for(width_, kind_, 1U),
                value_for(width_, kind_, 1U),
                value_for(width_, kind_, 2U) };
            stored_ = { value_for(width_, kind_, 2U),
                value_for(width_, kind_, 2U),
                value_for(width_, kind_, 0U) };
        } else {
            for (std::size_t signal = 0U; signal < 3U; ++signal) {
                const auto base = signal * 4U;
                current_[signal] = value_for(width_, kind_, base + 1U);
                previous_[signal] = value_for(width_, kind_, base + 2U);
                stored_[signal] = value_for(width_, kind_, base + 3U);
            }
        }
        for (std::size_t signal = 0U; signal < 3U; ++signal) {
            values_.seed_signal(static_cast<SignalId>(signal),
                current_[signal], previous_[signal], stored_[signal]);
        }
        if (width_ == 1U && kind_ == ValueKind::logic4) {
            owner_values_[0U] = value_for(width_, kind_, 3U);
            owner_values_[1U] = value_for(width_, kind_, 1U);
        } else {
            owner_values_[0U] = value_for(width_, kind_, 13U);
            owner_values_[1U] = value_for(width_, kind_, 14U);
        }
        if (alias_second_owner_) {
            owner_values_[1U] = stored_[kSecondSignal];
        }
        values_.seed_owner(kFirstSignal, kFirstOwner, owner_values_[0U]);
        values_.seed_owner(kSecondSignal, kSecondOwner, owner_values_[1U]);

        for (std::size_t target = 0U; target < 2U; ++target) {
            const auto signal = static_cast<SignalId>(target + 1U);
            live_current_[signal] = current_[signal];
            live_previous_[signal] = previous_[signal];
            live_stored_[signal] = stored_[signal];
            values_.stage_packed_signal_slots(signal,
                live_current_[signal], live_previous_[signal],
                live_stored_[signal]);
            if (target == 1U && alias_second_owner_) {
                values_.stage_packed_owner_stored_alias(
                    signal, kSecondOwner);
            } else {
                live_owner_[target] = owner_values_[target];
                values_.stage_packed_owner_slot(
                    signal, target == 0U ? kFirstOwner : kSecondOwner,
                    live_owner_[target]);
            }
        }
        require(values_.bind_packed_slots() != 0U
                && values_.packed_slots_bound(),
            "checked rebase fixture binds its versioned target roles");
        values_.clear_dirty();

        if (width_ == 1U && kind_ == ValueKind::logic4) {
            next_current_ = { value_for(width_, kind_, 1U),
                value_for(width_, kind_, 2U) };
            next_stored_ = { value_for(width_, kind_, 3U),
                value_for(width_, kind_, 1U) };
            next_owner_ = { value_for(width_, kind_, 0U),
                value_for(width_, kind_, 3U) };
            unrelated_current_ = value_for(width_, kind_, 3U);
            unrelated_stored_ = value_for(width_, kind_, 1U);
        } else {
            next_current_ = { value_for(width_, kind_, 21U),
                value_for(width_, kind_, 24U) };
            next_stored_ = { value_for(width_, kind_, 22U),
                value_for(width_, kind_, 25U) };
            next_owner_ = { value_for(width_, kind_, 23U),
                value_for(width_, kind_, 26U) };
            unrelated_current_ = value_for(width_, kind_, 27U);
            unrelated_stored_ = value_for(width_, kind_, 28U);
        }
        if (alias_second_owner_) {
            next_owner_[1U] = next_stored_[1U];
        }
    }

    [[nodiscard]] std::array<AuthoritativeSignalPlanes::PreparedMutation, 2U>
    prepare_target_rows()
    {
        return {
            prepare_first_target_row(), prepare_second_target_row(),
        };
    }

    [[nodiscard]] AuthoritativeSignalPlanes::PreparedMutation
    prepare_first_target_row()
    {
        return values_.prepare_owner_change(kFirstSignal, kFirstOwner,
            next_owner_[0U], next_current_[0U], next_stored_[0U]);
    }

    [[nodiscard]] AuthoritativeSignalPlanes::PreparedMutation
    prepare_second_target_row()
    {
        return values_.prepare_owner_change(kSecondSignal, kSecondOwner,
            next_owner_[1U], next_current_[1U], next_stored_[1U]);
    }

    [[nodiscard]] FixtureState state() const
    {
        FixtureState result {
            .targets = { {
                { values_.current(kFirstSignal),
                    values_.previous(kFirstSignal),
                    values_.stored(kFirstSignal),
                    values_.owner_value(kFirstSignal, kFirstOwner) },
                { values_.current(kSecondSignal),
                    values_.previous(kSecondSignal),
                    values_.stored(kSecondSignal),
                    values_.owner_value(kSecondSignal, kSecondOwner) },
            } },
            .unrelated_current = values_.current(kUnrelatedSignal),
            .unrelated_previous = values_.previous(kUnrelatedSignal),
            .unrelated_stored = values_.stored(kUnrelatedSignal),
            .signal_slots_bound = {
                values_.packed_signal_slots_bound(kFirstSignal),
                values_.packed_signal_slots_bound(kSecondSignal),
            },
            .owner_slots_bound = {
                values_.packed_owner_slot_bound(kFirstSignal, kFirstOwner),
                values_.packed_owner_slot_bound(kSecondSignal, kSecondOwner),
            },
            .packed_slot_count = values_.packed_slot_count(),
            .packed_slots_bound = values_.packed_slots_bound(),
            .revision = values_.revision(),
            .valid = values_.valid(),
        };
        const auto dirty = values_.dirty_signals();
        require(dirty.size() == result.dirty.size(),
            "fixture observes each component dirty bit");
        std::ranges::copy(dirty, result.dirty.begin());
        return result;
    }

    [[nodiscard]] bool matches(const FixtureState& expected) const
    {
        const auto actual = state();
        return actual.targets[0U].current == expected.targets[0U].current
            && actual.targets[0U].previous == expected.targets[0U].previous
            && actual.targets[0U].stored == expected.targets[0U].stored
            && actual.targets[0U].owner == expected.targets[0U].owner
            && actual.targets[1U].current == expected.targets[1U].current
            && actual.targets[1U].previous == expected.targets[1U].previous
            && actual.targets[1U].stored == expected.targets[1U].stored
            && actual.targets[1U].owner == expected.targets[1U].owner
            && actual.unrelated_current == expected.unrelated_current
            && actual.unrelated_previous == expected.unrelated_previous
            && actual.unrelated_stored == expected.unrelated_stored
            && actual.dirty == expected.dirty
            && actual.signal_slots_bound == expected.signal_slots_bound
            && actual.owner_slots_bound == expected.owner_slots_bound
            && actual.packed_slot_count == expected.packed_slot_count
            && actual.packed_slots_bound == expected.packed_slots_bound
            && actual.revision == expected.revision
            && actual.valid == expected.valid;
    }

    void publish_unrelated_change()
    {
        auto mutation = values_.prepare_value_change(
            kUnrelatedSignal, unrelated_current_, unrelated_stored_);
        require(values_.begin_prepared_publication(mutation),
            "unrelated ordinary A4 update prepares before group rebase");
        values_.publish(std::move(mutation));
    }

    void intervene_on_second(const std::size_t role)
    {
        switch (role) {
        case 0U:
            values_.mirror_visible(kSecondSignal,
                value_for(width_, kind_, 41U), current_[kSecondSignal]);
            break;
        case 1U:
            values_.mirror_visible(kSecondSignal,
                previous_[kSecondSignal], value_for(width_, kind_, 42U));
            break;
        case 2U:
            values_.mirror_stored(
                kSecondSignal, value_for(width_, kind_, 43U));
            break;
        default:
            values_.mirror_owner(kSecondSignal, kSecondOwner,
                value_for(width_, kind_, 44U));
            break;
        }
    }

    [[nodiscard]] PackedLogic4PlaneReadLease pin(
        const SignalId signal, const PackedPlaneRole role) const noexcept
    {
        const auto owner = signal == kFirstSignal
            ? kFirstOwner : kSecondOwner;
        return values_.plane_read_lease(signal, role, owner);
    }

    [[nodiscard]] AuthoritativeSignalPlanes& values() noexcept
    {
        return values_;
    }

    [[nodiscard]] const PackedLogic4& old_current(
        const SignalId signal) const noexcept
    {
        return current_[signal];
    }

    [[nodiscard]] const PackedLogic4& old_previous(
        const SignalId signal) const noexcept
    {
        return previous_[signal];
    }

    [[nodiscard]] const PackedLogic4& old_stored(
        const SignalId signal) const noexcept
    {
        return stored_[signal];
    }

    [[nodiscard]] const PackedLogic4& old_owner(
        const std::size_t index) const noexcept
    {
        return owner_values_[index];
    }

    [[nodiscard]] const PackedLogic4& next_current(
        const std::size_t index) const noexcept
    {
        return next_current_[index];
    }

    [[nodiscard]] const PackedLogic4& next_stored(
        const std::size_t index) const noexcept
    {
        return next_stored_[index];
    }

    [[nodiscard]] const PackedLogic4& next_owner(
        const std::size_t index) const noexcept
    {
        return next_owner_[index];
    }

private:
    std::uint32_t width_ { };
    ValueKind kind_ { ValueKind::logic4 };
    bool alias_second_owner_ { };
    RegionGraph graph_;
    SignalDriverLayout layout_;
    AuthoritativeSignalPlanes values_;
    std::array<PackedLogic4, 3U> current_;
    std::array<PackedLogic4, 3U> previous_;
    std::array<PackedLogic4, 3U> stored_;
    std::array<PackedLogic4, 2U> owner_values_;
    std::array<PackedLogic4, 2U> next_current_;
    std::array<PackedLogic4, 2U> next_stored_;
    std::array<PackedLogic4, 2U> next_owner_;
    std::array<PackedLogic4, 3U> live_current_;
    std::array<PackedLogic4, 3U> live_previous_;
    std::array<PackedLogic4, 3U> live_stored_;
    std::array<PackedLogic4, 2U> live_owner_;
    PackedLogic4 unrelated_current_;
    PackedLogic4 unrelated_stored_;
};

using PreparedRows
    = std::array<AuthoritativeSignalPlanes::PreparedMutation, 2U>;
using RoleLeaseSet = std::array<PackedLogic4PlaneReadLease, 4U>;

RoleLeaseSet pin_target_roles(const Fixture& fixture, const SignalId signal)
{
    return {
        fixture.pin(signal, PackedPlaneRole::current),
        fixture.pin(signal, PackedPlaneRole::previous),
        fixture.pin(signal, PackedPlaneRole::stored),
        fixture.pin(signal, PackedPlaneRole::owner),
    };
}

std::array<RoleLeaseSet, 2U> pin_both_targets(const Fixture& fixture)
{
    return { pin_target_roles(fixture, kFirstSignal),
        pin_target_roles(fixture, kSecondSignal) };
}

void require_old_roles(const Fixture& fixture,
    const std::array<RoleLeaseSet, 2U>& leases)
{
    require(lease_matches(leases[0U][0U], fixture.old_current(kFirstSignal))
            && lease_matches(leases[0U][1U], fixture.old_previous(kFirstSignal))
            && lease_matches(leases[0U][2U], fixture.old_stored(kFirstSignal))
            && lease_matches(leases[0U][3U], fixture.old_owner(0U))
            && lease_matches(leases[1U][0U], fixture.old_current(kSecondSignal))
            && lease_matches(leases[1U][1U], fixture.old_previous(kSecondSignal))
            && lease_matches(leases[1U][2U], fixture.old_stored(kSecondSignal))
            && lease_matches(leases[1U][3U], fixture.old_owner(1U)),
        "late target leases retain all four original role values");
}

void exercise_late_pin_rebase(const std::uint32_t width,
    const ValueKind kind, const bool alias_second_owner)
{
    Fixture fixture { width, kind, alias_second_owner };
    if (kind == ValueKind::logic9 && width >= 9U) {
        require(contains_all_logic9_states(fixture.old_current(kFirstSignal))
                && contains_all_logic9_states(fixture.old_stored(kFirstSignal))
                && contains_all_logic9_states(fixture.old_owner(0U))
                && contains_all_logic9_states(fixture.next_current(0U))
                && contains_all_logic9_states(fixture.next_stored(0U))
                && contains_all_logic9_states(fixture.next_owner(0U)),
            "Logic9 group rows carry every canonical state in all roles");
    }
    auto rows = fixture.prepare_target_rows();
    require(rows[0U].any_current_changed
            && rows[0U].any_stored_changed
            && rows[0U].any_owner_changed
            && rows[0U].any_state_changed
            && rows[1U].any_current_changed
            && rows[1U].any_stored_changed
            && (alias_second_owner ? !rows[1U].any_owner_changed
                                   : rows[1U].any_owner_changed)
            && rows[1U].any_state_changed
            && std::ranges::any_of(rows[0U].words,
                [](const auto& word) {
                    return word.old_previous != word.old_current;
                })
            && std::ranges::any_of(rows[1U].words,
                [](const auto& word) {
                    return word.old_previous != word.old_current;
                }),
        "fixture rows really change every non-aliased role and LAST");
    AuthoritativeSignalPlanes::PreparedGroupScratch scratch;
    fixture.values().prepare_group_scratch(scratch);
    require(scratch.ready() && !scratch.spent(),
        "prepared group scratch is bound to this versioned sidecar");

    const auto original = fixture.state();
    fixture.publish_unrelated_change();
    const auto after_unrelated = fixture.state();
    require(after_unrelated.revision == original.revision + 1U
            && after_unrelated.unrelated_current
                != original.unrelated_current
            && after_unrelated.targets == original.targets,
        "an unrelated ordinary signal update advances the revision only");
    const auto old_leases = pin_both_targets(fixture);
    require_old_roles(fixture, old_leases);

    begin_allocation_count();
    const auto wrong_revision = fixture.values().try_rebase_and_publish_group(
        rows, after_unrelated.revision - 1U, scratch);
    const auto wrong_revision_allocations = end_allocation_count();
    require(!wrong_revision && wrong_revision_allocations == 0U
            && scratch.ready() && !scratch.spent()
            && fixture.matches(after_unrelated),
        "wrong expected revision declines allocation-free and preserves state");

    begin_allocation_count();
    const auto published = fixture.values().try_rebase_and_publish_group(
        rows, after_unrelated.revision, scratch);
    const auto publication_allocations = end_allocation_count();
    require(published && publication_allocations == 0U
            && !scratch.ready() && scratch.spent(),
        "a prepared two-row rebase publishes without allocation and spends scratch");

    const auto& values = fixture.values();
    require(values.current(kUnrelatedSignal) == after_unrelated.unrelated_current
            && values.previous(kUnrelatedSignal)
                == after_unrelated.unrelated_previous
            && values.stored(kUnrelatedSignal)
                == after_unrelated.unrelated_stored,
        "the unrelated update survives refreshing the prepared role blocks");
    require(values.current(kFirstSignal) == fixture.next_current(0U)
            && values.previous(kFirstSignal)
                == fixture.old_current(kFirstSignal)
            && values.stored(kFirstSignal) == fixture.next_stored(0U)
            && values.owner_value(kFirstSignal, kFirstOwner)
                == fixture.next_owner(0U)
            && values.current(kSecondSignal) == fixture.next_current(1U)
            && values.previous(kSecondSignal)
                == fixture.old_current(kSecondSignal)
            && values.stored(kSecondSignal) == fixture.next_stored(1U)
            && values.owner_value(kSecondSignal, kSecondOwner)
                == fixture.next_owner(1U),
        "one group updates CURRENT, LAST, STORED, and each raw owner together");
    require(lease_matches(old_leases[0U][0U], fixture.old_current(kFirstSignal))
            && lease_matches(old_leases[0U][1U], fixture.old_previous(kFirstSignal))
            && lease_matches(old_leases[0U][2U], fixture.old_stored(kFirstSignal))
            && lease_matches(old_leases[0U][3U], fixture.old_owner(0U))
            && lease_matches(old_leases[1U][0U], fixture.old_current(kSecondSignal))
            && lease_matches(old_leases[1U][1U], fixture.old_previous(kSecondSignal))
            && lease_matches(old_leases[1U][2U], fixture.old_stored(kSecondSignal))
            && lease_matches(old_leases[1U][3U], fixture.old_owner(1U)),
        "late leases keep the pre-rebase values immutable");

    const auto new_leases = pin_both_targets(fixture);
    for (std::size_t target = 0U; target < 2U; ++target) {
        for (std::size_t role = 0U; role < 4U; ++role) {
            require(lease_has_new_backing(old_leases[target][role],
                        new_leases[target][role]),
                "late read pins force refreshed COW backing for every role");
        }
    }
    require(values.revision() == after_unrelated.revision + 1U
            && values.valid()
            && values.packed_slots_bound()
            && values.packed_signal_slots_bound(kFirstSignal)
            && values.packed_signal_slots_bound(kSecondSignal)
            && values.packed_owner_slot_bound(kFirstSignal, kFirstOwner)
            && values.packed_owner_slot_bound(kSecondSignal, kSecondOwner),
        "the complete group advances once and retains every packed binding");
}

void exercise_stale_role_declines(const std::size_t role)
{
    Fixture fixture { 129U, ValueKind::logic9 };
    auto rows = fixture.prepare_target_rows();
    AuthoritativeSignalPlanes::PreparedGroupScratch scratch;
    fixture.values().prepare_group_scratch(scratch);
    fixture.intervene_on_second(role);
    const auto after_intervention = fixture.state();
    const auto stale_previous = value_for(129U, ValueKind::logic9, 41U);
    const auto stale_current = value_for(129U, ValueKind::logic9, 42U);
    const auto stale_stored = value_for(129U, ValueKind::logic9, 43U);
    const auto stale_owner = value_for(129U, ValueKind::logic9, 44U);
    require((role == 0U
                ? after_intervention.targets[1U].previous == stale_previous
                : after_intervention.targets[1U].previous
                    == fixture.old_previous(kSecondSignal))
            && (role == 1U
                ? after_intervention.targets[1U].current == stale_current
                : after_intervention.targets[1U].current
                    == fixture.old_current(kSecondSignal))
            && (role == 2U
                ? after_intervention.targets[1U].stored == stale_stored
                : after_intervention.targets[1U].stored
                    == fixture.old_stored(kSecondSignal))
            && (role == 3U
                ? after_intervention.targets[1U].owner == stale_owner
                : after_intervention.targets[1U].owner
                    == fixture.old_owner(1U)),
        "stale-role fixture changes one of PREVIOUS, CURRENT, STORED, or OWNER");

    begin_allocation_count();
    const auto published = fixture.values().try_rebase_and_publish_group(
        rows, after_intervention.revision, scratch);
    const auto allocations = end_allocation_count();
    require(!published && allocations == 0U && scratch.ready()
            && !scratch.spent() && fixture.matches(after_intervention),
        "a stale target role declines the entire batch without partial publication");
    require(fixture.values().current(kFirstSignal)
                == fixture.old_current(kFirstSignal)
            && fixture.values().previous(kFirstSignal)
                == fixture.old_previous(kFirstSignal)
            && fixture.values().stored(kFirstSignal)
                == fixture.old_stored(kFirstSignal)
            && fixture.values().owner_value(kFirstSignal, kFirstOwner)
                == fixture.old_owner(0U),
        "the valid first row stays untouched when the second row is stale");
}

void exercise_duplicate_and_malformed_declines()
{
    Fixture duplicate_fixture { 129U, ValueKind::logic4 };
    const auto first_rows = duplicate_fixture.prepare_target_rows();
    PreparedRows duplicate_rows {
        first_rows[0U], first_rows[0U]
    };
    AuthoritativeSignalPlanes::PreparedGroupScratch duplicate_scratch;
    duplicate_fixture.values().prepare_group_scratch(duplicate_scratch);
    const auto duplicate_before = duplicate_fixture.state();
    begin_allocation_count();
    const auto duplicate_published
        = duplicate_fixture.values().try_rebase_and_publish_group(
            duplicate_rows, duplicate_before.revision, duplicate_scratch);
    const auto duplicate_allocations = end_allocation_count();
    require(!duplicate_published && duplicate_allocations == 0U
            && duplicate_scratch.ready()
            && duplicate_fixture.matches(duplicate_before),
        "repeated target rows decline atomically without allocation");

    Fixture malformed_fixture { 129U, ValueKind::logic9 };
    auto malformed_rows = malformed_fixture.prepare_target_rows();
    AuthoritativeSignalPlanes::PreparedGroupScratch malformed_scratch;
    malformed_fixture.values().prepare_group_scratch(malformed_scratch);
    malformed_rows[1U].words.back().new_current[0U] |= (1ULL << 1U);
    const auto malformed_before = malformed_fixture.state();
    begin_allocation_count();
    const auto malformed_published
        = malformed_fixture.values().try_rebase_and_publish_group(
            malformed_rows, malformed_before.revision, malformed_scratch);
    const auto malformed_allocations = end_allocation_count();
    require(!malformed_published && malformed_allocations == 0U
            && malformed_scratch.ready()
            && malformed_fixture.matches(malformed_before),
        "a nonzero Logic9 tail bit declines the full group before publication");
}

void exercise_wrong_and_spent_scratch()
{
    Fixture wrong_owner { 65U, ValueKind::logic4 };
    Fixture actual_owner { 65U, ValueKind::logic4 };
    auto rows = actual_owner.prepare_target_rows();
    AuthoritativeSignalPlanes::PreparedGroupScratch default_scratch;
    const auto actual_before = actual_owner.state();
    begin_allocation_count();
    const auto default_attempt
        = actual_owner.values().try_rebase_and_publish_group(
            rows, actual_before.revision, default_scratch);
    const auto default_allocations = end_allocation_count();
    require(!default_attempt && default_allocations == 0U
            && !default_scratch.ready() && !default_scratch.spent()
            && actual_owner.matches(actual_before),
        "unprepared scratch declines without allocation or mutation");

    AuthoritativeSignalPlanes::PreparedGroupScratch wrong_scratch;
    wrong_owner.values().prepare_group_scratch(wrong_scratch);
    const auto before = actual_owner.state();
    begin_allocation_count();
    const auto wrong_instance
        = actual_owner.values().try_rebase_and_publish_group(
            rows, before.revision, wrong_scratch);
    const auto wrong_instance_allocations = end_allocation_count();
    require(!wrong_instance && wrong_instance_allocations == 0U
            && wrong_scratch.ready() && actual_owner.matches(before),
        "scratch owned by another sidecar declines without touching either owner");

    AuthoritativeSignalPlanes::PreparedGroupScratch spent_scratch;
    actual_owner.values().prepare_group_scratch(spent_scratch);
    auto first_rows = actual_owner.prepare_target_rows();
    const auto first_revision = actual_owner.values().revision();
    require(actual_owner.values().try_rebase_and_publish_group(
                first_rows, first_revision, spent_scratch),
        "scratch fixture publishes its first valid group");
    const auto spent_state = actual_owner.state();
    auto next_rows = actual_owner.prepare_target_rows();
    begin_allocation_count();
    const auto spent_reuse
        = actual_owner.values().try_rebase_and_publish_group(
            next_rows, spent_state.revision, spent_scratch);
    const auto spent_allocations = end_allocation_count();
    require(!spent_reuse && spent_allocations == 0U
            && spent_scratch.spent() && actual_owner.matches(spent_state),
        "consumed scratch cannot publish another epoch");
}

void exercise_preparation_allocation_failures()
{
    constexpr auto max_allocation_cut = std::size_t { 128U };
    bool found_uninjected_success { };
    bool saw_second_row_preparation_failure { };
    for (std::size_t cut = 0U; cut < max_allocation_cut; ++cut) {
        Fixture fixture { 129U, ValueKind::logic9 };
        const auto before = fixture.state();
        PreparedRows rows;
        AuthoritativeSignalPlanes::PreparedGroupScratch scratch;
        bool first_row_ready { };
        bool second_row_ready { };
        bool prepared { };
        arm_allocation_failure(cut);
        try {
            rows[0U] = fixture.prepare_first_target_row();
            first_row_ready = true;
            rows[1U] = fixture.prepare_second_target_row();
            second_row_ready = true;
            fixture.values().prepare_group_scratch(scratch);
            prepared = true;
        } catch (const std::bad_alloc&) {
        }
        const auto injected = allocation_failure_was_injected();
        clear_allocation_failure();
        saw_second_row_preparation_failure
            = saw_second_row_preparation_failure
                || (injected && first_row_ready && !second_row_ready);

        if (prepared) {
            require(!injected && scratch.ready() && !scratch.spent(),
                "allocation sweep stops at the first uninjected preparation");
            begin_allocation_count();
            const auto published
                = fixture.values().try_rebase_and_publish_group(
                    rows, fixture.values().revision(), scratch);
            const auto publication_allocations = end_allocation_count();
            require(published && publication_allocations == 0U,
                "the first uninjected preparation publishes without allocation");
            found_uninjected_success = true;
            break;
        }

        require(injected && fixture.matches(before),
            "preparation allocation failure leaves public A4 roles untouched");
        rows = PreparedRows { };
        auto retry_rows = fixture.prepare_target_rows();
        AuthoritativeSignalPlanes::PreparedGroupScratch retry_scratch;
        fixture.values().prepare_group_scratch(retry_scratch);
        require(retry_scratch.ready()
                && fixture.values().try_rebase_and_publish_group(
                    retry_rows, fixture.values().revision(), retry_scratch),
            "fresh row and scratch preparation retries on the same sidecar");
    }
    require(found_uninjected_success && saw_second_row_preparation_failure,
        "bounded preparation sweep covers a second-row failure and reaches its terminal cut");
}

void exercise_unsupported_narrow_decline()
{
    Fixture fixture { 64U, ValueKind::logic4, false,
        PackedSlotBindingPolicy::narrow_only };
    auto rows = fixture.prepare_target_rows();
    AuthoritativeSignalPlanes::PreparedGroupScratch scratch;
    const auto before = fixture.state();
    bool unsupported { };
    try {
        fixture.values().prepare_group_scratch(scratch);
    } catch (const std::logic_error&) {
        unsupported = true;
    }
    require(unsupported && !scratch.ready() && !scratch.spent()
            && fixture.matches(before),
        "narrow-only sidecars reject scratch preparation before publication");
    begin_allocation_count();
    const auto published = fixture.values().try_rebase_and_publish_group(
        rows, before.revision, scratch);
    const auto allocations = end_allocation_count();
    require(!published && allocations == 0U
            && fixture.matches(before),
        "unsupported narrow backing declines without public mutation");
}

void run_tests()
{
    exercise_late_pin_rebase(1U, ValueKind::logic4, false);
    exercise_late_pin_rebase(129U, ValueKind::logic4, false);
    exercise_late_pin_rebase(64U, ValueKind::logic9, false);
    exercise_late_pin_rebase(129U, ValueKind::logic9, true);
    exercise_stale_role_declines(0U);
    exercise_stale_role_declines(1U);
    exercise_stale_role_declines(2U);
    exercise_stale_role_declines(3U);
    exercise_duplicate_and_malformed_declines();
    exercise_wrong_and_spent_scratch();
    exercise_preparation_allocation_failures();
    exercise_unsupported_narrow_decline();
}

} // namespace

int main()
{
    try {
        run_tests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "A4 checked rebase test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
