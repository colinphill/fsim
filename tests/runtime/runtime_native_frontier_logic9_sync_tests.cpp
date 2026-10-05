// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using RuntimeImplementation = OwnedDriverDemotionTestAccess::Implementation;
using FrontierRuntime = RuntimeImplementation::RegionFrontierComponentRuntime;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        set(value);
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (had_previous_) {
#if defined(_WIN32)
            static_cast<void>(_putenv_s(name_.c_str(), previous_.c_str()));
#else
            static_cast<void>(setenv(name_.c_str(), previous_.c_str(), 1));
#endif
        } else {
            unset();
        }
    }

private:
    void set(const char* value)
    {
#if defined(_WIN32)
        const auto result = _putenv_s(name_.c_str(), value);
#else
        const auto result = setenv(name_.c_str(), value, 1);
#endif
        if (result != 0) {
            throw std::runtime_error {
                "failed to set native-frontier Logic9 test environment"
            };
        }
    }

    void unset() noexcept
    {
#if defined(_WIN32)
        static_cast<void>(_putenv_s(name_.c_str(), ""));
#else
        static_cast<void>(unsetenv(name_.c_str()));
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

[[nodiscard]] PackedLogic4 logic9_pattern(
    const std::uint32_t width, const std::size_t phase)
{
    constexpr std::string_view states { "UX01ZWLH-" };
    std::string value(width, 'U');
    for (std::size_t index = 0U; index < value.size(); ++index) {
        value[index] = states[(index + phase) % states.size()];
    }
    return PackedLogic4::from_logic9_msb_string(value);
}

[[nodiscard]] Process whole_active_logic9_writer(
    const ProcessId process,
    const SignalId sensitivity,
    const SignalId output,
    const PackedLogic4& initial)
{
    Process writer;
    writer.id = process;
    writer.name = "frontier.logic9.sync.owner." + std::to_string(process);
    writer.language_standard = "2017";
    writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    writer.initialize = true;
    writer.register_count = 1U;
    writer.register_value_kinds = { ValueKind::logic9 };
    writer.static_sensitivity = { { sensitivity, EdgeKind::any } };
    writer.driver_regions = { { output, 0U,
        static_cast<std::uint32_t>(initial.width()), true } };
    writer.operations = {
        LoadConstant { 0U, initial },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    return writer;
}

/// This test exercises only host synchronization of committed typed planes.
/// The backend exists to provide a stable immutable layout; no generated entry
/// is invoked or counted as executed.
class SyncOnlyFrontierBackend final : public RegionFrontierBackend {
public:
    SyncOnlyFrontierBackend(const ProcessId owner,
        const SignalId signal, const std::uint32_t width,
        const std::uint64_t certificate_generation,
        const std::uint64_t component_generation)
    {
        const auto words = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(width) + 63U) / 64U);
        member_.process_id = owner;
        member_.first_write_site = 0U;
        member_.write_site_count = 1U;
        member_.max_pending_writes = 1U;
        member_.max_staged_events = 1U;

        signal_.signal_id = signal;
        signal_.owner_process_id = owner;
        signal_.value_kind = RegionFrontierValueKindV2::logic9;
        signal_.width = width;
        signal_.word_count = words;
        signal_.plane_count = 4U;
        signal_.flags = static_cast<std::uint32_t>(
            RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
        signal_.metadata_index = 0U;

        write_site_.member_index = 0U;
        write_site_.signal_slot = 0U;
        write_site_.source_instruction = 1U;
        write_site_.update_kind = static_cast<std::uint32_t>(
            RegionUpdateKind::systemverilog_active);
        write_site_.event_kind = static_cast<std::uint32_t>(
            RegionFrontierEventKindV2::internal_commit);
        write_site_.value_kind = RegionFrontierValueKindV2::logic9;
        write_site_.width = width;
        write_site_.word_count = words;
        write_site_.plane_count = 4U;
        write_site_.pending_slot = 0U;

        layout_.struct_size = sizeof(RegionFrontierLayoutV2);
        layout_.certificate_generation = certificate_generation;
        layout_.component_generation = component_generation;
        layout_.member_count = 1U;
        layout_.readiness_word_count = 1U;
        layout_.signal_slot_count = 1U;
        layout_.metadata_count = 1U;
        layout_.fanout_edge_count = 0U;
        layout_.write_site_count = 1U;
        layout_.pending_write_capacity = 1U;
        layout_.staged_event_capacity = 1U;
        layout_.max_commit_fanout_events = 0U;
        layout_.committed_signal_capacity = 1U;
        layout_.members = &member_;
        layout_.signals = &signal_;
        layout_.write_sites = &write_site_;
        layout_.max_member_write_counts = &max_member_write_count_;
        layout_.max_member_staged_event_counts
            = &max_member_staged_event_count_;
        layout_.fanout_edges = nullptr;
    }

    [[nodiscard]] RegionFrontierStepEntryV2
    step_entry() const noexcept override
    {
        return nullptr;
    }

    [[nodiscard]] const RegionFrontierLayoutV2&
    layout() const noexcept override
    {
        return layout_;
    }

private:
    RegionFrontierMemberLayoutV2 member_;
    RegionFrontierSignalLayoutV2 signal_;
    RegionFrontierWriteSiteV2 write_site_;
    std::uint32_t max_member_write_count_ { 1U };
    std::uint32_t max_member_staged_event_count_ { 1U };
    RegionFrontierLayoutV2 layout_;
};

void copy_logic9_to_role(
    AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal,
    const ProcessId owner,
    const PackedPlaneRole role,
    const PackedLogic4& value)
{
    require(value.is_logic9(), "the sync fixture supplies Logic9 values");
    std::array<std::span<std::uint64_t>, 4U> destination;
    require(lease.plane_words(signal, role, owner, destination),
        "the real A4 lease exposes every Logic9 role plane");
    for (std::size_t plane = 0U; plane < destination.size(); ++plane) {
        const auto source = value.logic9_plane_words(plane);
        require(destination[plane].size() == source.size(),
            "the A4 role uses the exact per-plane word count");
        std::copy(source.begin(), source.end(), destination[plane].begin());
    }
}

void bind_role_planes(
    const AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal,
    const ProcessId owner,
    const PackedPlaneRole role,
    std::span<std::uint64_t*, 4U> destination,
    const std::size_t word_count)
{
    std::array<std::span<std::uint64_t>, 4U> source;
    require(lease.plane_words(signal, role, owner, source),
        "the V2 descriptor borrows the real A4 role allocation");
    for (std::size_t plane = 0U; plane < source.size(); ++plane) {
        require(source[plane].size() == word_count,
            "all four role planes use the same packed word extent");
        destination[plane] = source[plane].data();
    }
}

void require_plane_mirror(const std::vector<std::uint64_t>& mirror,
    const std::size_t offset, const std::span<const std::uint64_t> expected,
    const std::string_view message)
{
    require(offset <= mirror.size()
            && expected.size() <= mirror.size() - offset
            && std::ranges::equal(expected,
                std::span<const std::uint64_t> { mirror }.subspan(
                    offset, expected.size())),
        message);
}

void require_logic9_direct_mirrors(
    const RuntimeImplementation& impl,
    const SignalId signal,
    const PackedLogic4& current,
    const PackedLogic4& previous)
{
    require(current.is_logic9() && previous.is_logic9()
            && current.width() == previous.width(),
        "direct Logic9 mirrors compare complete typed values");
    if (current.width() <= 64U) {
        const auto current0 = current.logic9_plane_words(0U);
        const auto current1 = current.logic9_plane_words(1U);
        const auto current2 = current.logic9_plane_words(2U);
        const auto current3 = current.logic9_plane_words(3U);
        const auto previous0 = previous.logic9_plane_words(0U);
        const auto previous1 = previous.logic9_plane_words(1U);
        const auto previous2 = previous.logic9_plane_words(2U);
        const auto previous3 = previous.logic9_plane_words(3U);
        require(impl.direct_signal_logic9_plane0.at(signal) == current0[0U]
                && impl.direct_signal_logic9_plane1.at(signal) == current1[0U]
                && impl.direct_signal_logic9_plane2.at(signal) == current2[0U]
                && impl.direct_signal_logic9_plane3.at(signal) == current3[0U]
                && impl.direct_signal_last_logic9_plane0.at(signal)
                    == previous0[0U]
                && impl.direct_signal_last_logic9_plane1.at(signal)
                    == previous1[0U]
                && impl.direct_signal_last_logic9_plane2.at(signal)
                    == previous2[0U]
                && impl.direct_signal_last_logic9_plane3.at(signal)
                    == previous3[0U],
            "narrow sync updates current and LAST in all four Logic9 mirrors");
        return;
    }

    const auto offset = static_cast<std::size_t>(
        impl.direct_wide_signal_offsets.at(signal));
    require_plane_mirror(impl.direct_wide_signal_aval, offset,
        current.aval_words(),
        "wide sync mirrors every current aval word");
    require_plane_mirror(impl.direct_wide_signal_bval, offset,
        current.bval_words(),
        "wide sync mirrors every current bval word");
    require_plane_mirror(impl.direct_wide_signal_logic9_plane2, offset,
        current.logic9_plane_words(2U),
        "wide sync mirrors every current Logic9 plane two word");
    require_plane_mirror(impl.direct_wide_signal_logic9_plane3, offset,
        current.logic9_plane_words(3U),
        "wide sync mirrors every current Logic9 plane three word");
}

template <typename T>
[[nodiscard]] bool add_test_workspace_slice(
    const std::size_t count, std::size_t& byte_count) noexcept
{
    if (count == 0U) {
        return true;
    }
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        return false;
    }
    const auto bytes = count * sizeof(T);
    const auto padding = alignof(T) - 1U;
    if (bytes > std::numeric_limits<std::size_t>::max() - padding
        || byte_count > std::numeric_limits<std::size_t>::max()
            - bytes - padding) {
        return false;
    }
    byte_count += bytes + padding;
    return true;
}

void configure_logic9_runtime_arena(
    FrontierRuntime& runtime,
    AuthoritativeSignalPlanes& values)
{
    const auto counts = values.component_plane_word_counts();
    std::size_t plane_words { };
    for (const auto& role_counts : counts) {
        for (const auto count : role_counts) {
            if (count > std::numeric_limits<std::size_t>::max() - plane_words) {
                throw std::runtime_error {
                    "Logic9 test arena plane count must not overflow"
                };
            }
            plane_words += count;
        }
    }
    if (plane_words > std::numeric_limits<std::size_t>::max()
            / sizeof(std::uint64_t)) {
        throw std::runtime_error {
            "Logic9 test arena plane byte count must not overflow"
        };
    }

    std::size_t byte_count { };
    require(add_test_workspace_slice<
                AuthoritativeSignalPlanes::FrontierWriteBinding>(
                    1U, byte_count)
            && add_test_workspace_slice<RegionFrontierMemberV2>(
                1U, byte_count)
            && add_test_workspace_slice<RegionFrontierPlaneV2>(
                1U, byte_count)
            && add_test_workspace_slice<RegionFrontierSignalMetadataV2>(
                1U, byte_count)
            && add_test_workspace_slice<RegionFrontierPendingWriteV2>(
                1U, byte_count)
            && add_test_workspace_slice<RegionFrontierCommittedSignalV2>(
                1U, byte_count)
            && add_test_workspace_slice<std::uint64_t>(plane_words, byte_count),
        "Logic9 test arena extent must include frame and A4 role slices");
    require(runtime.workspace_allocation.configure(byte_count),
        "Logic9 test workspace configures once before runtime vectors allocate");
}

void rehome_logic9_runtime_roles(
    FrontierRuntime& runtime,
    AuthoritativeSignalPlanes& values,
    const SignalId signal,
    const ProcessId owner)
{
    const auto counts = values.component_plane_word_counts();
    std::size_t plane_words_count { };
    for (const auto& role_counts : counts) {
        for (const auto count : role_counts) {
            plane_words_count += count;
        }
    }
    auto* const words = static_cast<std::uint64_t*>(
        runtime.workspace_allocation.allocate(
            plane_words_count * sizeof(std::uint64_t),
            alignof(std::uint64_t)));
    require(runtime.workspace_allocation.owns(words,
                plane_words_count * sizeof(std::uint64_t)),
        "Logic9 A4 role words occupy the runtime's shared component arena");

    AuthoritativeSignalPlanes::ComponentPlaneSpans slices { };
    auto remaining = std::span<std::uint64_t> { words, plane_words_count };
    std::size_t offset { };
    for (std::size_t role = 0U; role < 4U; ++role) {
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
            const auto count = counts[role][plane];
            require(offset <= remaining.size()
                    && count <= remaining.size() - offset,
                "Logic9 A4 plane partitions fit the reserved arena tail");
            slices[role][plane] = remaining.subspan(offset, count);
            offset += count;
        }
    }
    require(offset == plane_words_count
            && values.rehome_component_planes(
                runtime.workspace_allocation.storage_lifetime(), slices),
        "Logic9 A4 roles migrate atomically into the runtime arena");

    const auto owners = values.layout().owners(signal);
    require(owners.size() == 1U,
        "the Logic9 arena fixture retains its single owner mapping");
    const bool aliases_stored = owners.front().aliases_stored;
    const auto stored_lease = values.plane_read_lease(
        signal, PackedPlaneRole::stored);
    constexpr std::array<PackedPlaneRole, 4U> roles {
        PackedPlaneRole::current,
        PackedPlaneRole::previous,
        PackedPlaneRole::stored,
        PackedPlaneRole::owner
    };
    for (const auto role : roles) {
        const auto lease = values.plane_read_lease(signal, role, owner);
        require(static_cast<bool>(lease),
            "each Logic9 role retains a plane lease after arena adoption");
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
            const auto span = lease.plane_words(plane);
            const auto expected_count
                = role == PackedPlaneRole::owner && aliases_stored
                ? counts[static_cast<std::size_t>(PackedPlaneRole::stored)][plane]
                : counts[static_cast<std::size_t>(role)][plane];
            require(span.size() == expected_count
                    && (span.empty()
                        || runtime.workspace_allocation.owns(span.data(),
                            span.size() * sizeof(std::uint64_t)))
                    && (role != PackedPlaneRole::owner || !aliases_stored
                        || (stored_lease
                            && span.data()
                                == stored_lease.plane_words(plane).data())),
                "each Logic9 role plane is a correctly sized arena slice");
        }
    }
}

void check_borrowed_plane_storage_detaches()
{
    const std::array<std::uint64_t, 3U> expected {
        0x0123456789abcdefULL, 0xfedcba9876543210ULL,
        0x55aa55aa55aa55aaULL
    };
    auto storage_owner = std::make_shared<std::vector<std::uint64_t>>(
        expected.begin(), expected.end());
    auto block = std::make_shared<PackedLogic4PlaneBlock>();
    block->storage_owner = storage_owner;
    block->planes[0U].resize(expected.size());
    block->planes[0U].bind(std::span<std::uint64_t> { *storage_owner });

    auto copied = block->planes[0U];
    auto moved = std::move(block->planes[0U]);
    require(copied.size() == expected.size()
            && moved.size() == expected.size()
            && !copied.borrowed() && !moved.borrowed()
            && block->planes[0U].borrowed()
            && std::ranges::equal(copied.span(), expected)
            && std::ranges::equal(moved.span(), expected)
            && std::ranges::equal(block->planes[0U].span(), expected),
        "copy/move of a borrowed role plane detaches while preserving its source view");

    block->storage_owner.reset();
    storage_owner.reset();
    block.reset();
    require(std::ranges::equal(copied.span(), expected)
            && std::ranges::equal(moved.span(), expected),
        "detached PlaneStorage copies survive the borrowed block and owner");
}

void exercise_logic9_sync_width(const std::uint32_t width)
{
    constexpr ProcessId owner = 0U;
    constexpr SignalId trigger = 0U;
    constexpr SignalId target = 1U;
    const auto initial = logic9_pattern(width, 0U);
    const auto trigger_value = PackedLogic4 { 1U, Logic4::zero };
    const bool aliases_stored = width > 64U;
    const auto target_resolution = aliases_stored
        ? ResolutionKind::none : ResolutionKind::std_logic;

    Interpreter interpreter;
    require(interpreter.add_signal({ "frontier.logic9.sync.trigger",
                trigger_value, ResolutionKind::none, ValueKind::logic4 })
            == trigger,
        "the Logic9 sync fixture registers its sensitivity signal");
    require(interpreter.add_signal({ "frontier.logic9.sync.target",
                initial, target_resolution, ValueKind::logic9 }) == target,
        "the Logic9 sync fixture registers the target with its path-specific resolution kind");
    auto writer = whole_active_logic9_writer(
        owner, trigger, target, initial);
    require(interpreter.add_process(std::move(writer)) == owner,
        "the Logic9 output retains one original SV Active owner");
    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "the original owner reaches its static wait before test publication");

    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* const driver = impl.driver_values.at(target).find(owner);
    require(impl.region_runtime_generation != 0U
            && impl.region_graph.has_value()
            && impl.signals.at(target).resolution == target_resolution
            && impl.signals.at(target).value_kind == ValueKind::logic9
            && (aliases_stored ? driver == nullptr
                : driver != nullptr && driver->value == initial
                    && impl.driver_values.at(target).size() == 1U),
        "the Logic9 owner uses a wide unresolved alias or a real narrow resolver record");
    impl.demote_all_region_authoritative_slots(false);

    const auto* const process_view = &interpreter.process_program(owner);
    const std::array<const Process*, 1U> process_views { process_view };
    const std::array<SignalId, 1U> component_signals { target };
    const std::array<SignalId, 0U> no_partial_certificates { };
    const std::array<SignalId, 1U> owner_alias_certificates { target };
    const std::span<const SignalId> certified_owner_aliases
        = aliases_stored
        ? std::span<const SignalId> { owner_alias_certificates }
        : std::span<const SignalId> { no_partial_certificates };
    auto driver_layout = SignalDriverLayout::build(*impl.region_graph,
        component_signals, no_partial_certificates,
        certified_owner_aliases);
    const std::array<ProcessId, 1U> component_members { owner };
    auto state = std::make_shared<RegionAuthoritativeComponentState>(
        impl.region_runtime_generation, std::move(driver_layout),
        RegionGroupedFanout::build(process_views, component_members), 1U,
        PackedSlotBindingPolicy::experimental_wide);
    auto& values = state->values();

    auto& current = impl.signals.at(target).initial_value;
    auto& previous = impl.signal_last_values.at(target);
    auto& stored = impl.driven_values.at(target);
    auto& owner_value = aliases_stored ? stored : driver->value;
    require(current == initial && stored == initial,
        "startup leaves the sole Logic9 owner value in current and stored");
    values.seed_signal(target, current, previous, stored);
    values.seed_owner(target, owner, owner_value);

    require(owner < impl.region_component_by_process.size()
            && impl.region_graph.has_value(),
        "the Logic9 owner has a current graph component mapping");
    const auto component = impl.region_component_by_process[owner];
    const auto& certificates
        = impl.region_graph->certificate_inventory().components;
    require(component < certificates.size()
            && component < impl.region_authoritative_state_by_component.size()
            && impl.region_graph->component_epochs_current(component)
            && std::ranges::find(certificates[component].members, owner)
                != certificates[component].members.end(),
        "the test state is installed at the owner's current certified component");

    const auto word_count = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    const auto certificate_generation = std::uint64_t { 71U };
    const auto component_generation = std::uint64_t { 19U };
    auto runtime = std::make_shared<FrontierRuntime>();
    runtime->owner = &impl;
    runtime->component = component;
    runtime->runtime_generation = impl.region_runtime_generation;
    runtime->authoritative_state = state;
    if (width == 129U) {
        configure_logic9_runtime_arena(*runtime, values);
    }
    runtime->writable_signals.push_back({ target, owner });
    runtime->members.resize(1U);
    runtime->members[0U].process_id = owner;
    runtime->planes.resize(1U);
    runtime->metadata.resize(1U);
    runtime->pending_writes.resize(1U);
    runtime->committed_signals.resize(1U);

    auto backend_entry
        = std::make_shared<RuntimeImplementation::RegionFrontierBackendEntry>();
    backend_entry->provider_identity = "logic9-sync-contract-test";
    backend_entry->executor = std::make_unique<SyncOnlyFrontierBackend>(owner,
        target, width, certificate_generation, component_generation);
    runtime->backend = backend_entry;

    auto& plane = runtime->planes[0U];
    plane.signal_id = target;
    plane.owner_process_id = owner;
    plane.value_kind = RegionFrontierValueKindV2::logic9;
    plane.width = width;
    plane.word_count = static_cast<std::uint32_t>(word_count);
    plane.plane_count = 4U;
    plane.flags = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
    plane.metadata_index = 0U;

    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 1U>
        writable { { { target, owner } } };
    auto& frame = runtime->frame;
    frame.abi_version = kRegionFrontierAbiVersionV2;
    frame.struct_size = sizeof(RegionFrontierFrameV2);
    frame.value_plane_contract = kRegionFrontierValuePlaneContractV2;
    frame.runtime_generation = impl.region_runtime_generation;
    frame.bound_runtime_generation = impl.region_runtime_generation;
    frame.certificate_generation = certificate_generation;
    frame.component_generation = component_generation;
    frame.member_count = 1U;
    frame.signal_slot_count = 1U;
    frame.metadata_count = 1U;
    frame.committed_signal_capacity = 1U;
    frame.pending_write_capacity = 1U;
    frame.staged_event_capacity = 1U;
    frame.members = runtime->members.data();
    frame.planes = runtime->planes.data();
    frame.metadata = runtime->metadata.data();
    frame.pending_writes = runtime->pending_writes.data();
    frame.committed_signals = runtime->committed_signals.data();
    runtime->frame_initialized = true;
    if (width == 129U) {
        rehome_logic9_runtime_roles(*runtime, values, target, owner);
    }

    values.stage_packed_signal_slots(target, current, previous, stored);
    if (aliases_stored) {
        values.stage_packed_owner_stored_alias(target, owner);
    } else {
        values.stage_packed_owner_slot(target, owner, owner_value);
    }
    const auto expected_physical_roles = aliases_stored ? 3U : 4U;
    require(values.bind_packed_slots() == expected_physical_roles
            && values.packed_slot_count() == expected_physical_roles
            && values.packed_signal_slots_bound(target)
            && values.packed_owner_slot_bound(target, owner)
            && values.owner_value(target, owner) == values.stored(target)
            && values.owner_value(target, owner) == initial,
        aliases_stored
            ? "the wide Logic9 owner aliases stored without a fourth role"
            : "the narrow Logic9 owner has its own real packed role");

    impl.region_authoritative_state_by_component[component] = state;
    if (impl.region_authoritative_component_by_signal.size()
        < impl.signals.size()) {
        impl.region_authoritative_component_by_signal.resize(
            impl.signals.size(), std::numeric_limits<std::size_t>::max());
    }
    impl.region_authoritative_component_by_signal[target] = component;
    require(impl.region_authoritative_state_for_signal(target) == state.get(),
        "the test-installed synchronization fixture owns the signal's A4 component");

    constexpr std::array<std::string_view, 9U> all_states {
        "U", "X", "0", "1", "Z", "W", "L", "H", "-" };
    std::array<bool, 9U> observed_states { };
    const auto mark_states = [&observed_states](const PackedLogic4& value) {
        constexpr std::string_view state_codes { "UX01ZWLH-" };
        for (const auto digit : value.to_msb_string()) {
            const auto found = state_codes.find(digit);
            require(found != std::string_view::npos,
                "Logic9 values contain only the nine standard state symbols");
            observed_states[found] = true;
        }
    };
    mark_states(initial);

    for (std::size_t phase = 1U; phase < all_states.size(); ++phase) {
        const auto next_current = logic9_pattern(width, phase);
        auto expected_previous = values.current(target);
        const auto old_generation = values.revision();
        AuthoritativeSignalPlanes::FrontierWriteLease lease;
        require(values.try_acquire_frontier_write_lease(
                    old_generation, writable, lease)
                && lease.active(),
            "each Logic9 commit holds the real A4 exclusive write lease");

        copy_logic9_to_role(lease, target, owner,
            PackedPlaneRole::current, next_current);
        copy_logic9_to_role(lease, target, owner,
            PackedPlaneRole::previous, expected_previous);
        copy_logic9_to_role(lease, target, owner,
            PackedPlaneRole::stored, next_current);
        copy_logic9_to_role(lease, target, owner,
            PackedPlaneRole::owner, next_current);
        bind_role_planes(lease, target, owner, PackedPlaneRole::current,
            plane.current_planes, word_count);
        bind_role_planes(lease, target, owner, PackedPlaneRole::previous,
            plane.previous_planes, word_count);
        bind_role_planes(lease, target, owner, PackedPlaneRole::stored,
            plane.stored_planes, word_count);
        bind_role_planes(lease, target, owner, PackedPlaneRole::owner,
            plane.owner_planes, word_count);
        for (std::size_t value_plane = 0U;
             value_plane < plane.plane_count; ++value_plane) {
            require((plane.stored_planes[value_plane]
                        == plane.owner_planes[value_plane]) == aliases_stored,
                aliases_stored
                    ? "the wide Logic9 raw-owner binding aliases stored planes"
                    : "the narrow Logic9 raw-owner binding has distinct storage");
        }

        auto& metadata = runtime->metadata[0U];
        const auto phase_value = static_cast<std::uint64_t>(phase);
        metadata.event_time = 100U + phase_value;
        metadata.event_delta = 7U + phase_value;
        metadata.transaction_time = 100U + phase_value;
        metadata.transaction_delta = 7U + phase_value;
        metadata.value_revision = impl.signal_value_revisions.at(target) + 1U;
        metadata.systemverilog_round = phase_value;
        metadata.event_process_domain = static_cast<std::uint32_t>(
            ProcessSchedulingDomain::systemverilog);
        metadata.event_phase = static_cast<std::uint32_t>(SchedulerPhase::active);
        metadata.event_valid = 1U;
        metadata.transaction_valid = 1U;
        runtime->committed_signals[0U] = { 0U, 1U, 1U };
        frame.committed_signal_count = 1U;

        runtime->synchronize_committed_state(lease);
        require(!runtime->invalidated
                && frame.committed_signal_count == 0U
                && impl.direct_signal_materialization_pending.at(target) == 0U,
            "typed Logic9 sync drains committed metadata without scalar materialization");
        require(values.current(target) == next_current
                && values.previous(target) == expected_previous
                && values.stored(target) == next_current
                && values.owner_value(target, owner) == next_current
                && values.packed_signal_slots_bound(target)
                && values.packed_owner_slot_bound(target, owner)
                && values.packed_slot_count() == expected_physical_roles
                && (aliases_stored
                    ? impl.driver_values.at(target).find(owner) == nullptr
                    : impl.driver_values.at(target).find(owner) != nullptr
                        && impl.driver_values.at(target).find(owner)->value
                            == next_current),
            aliases_stored
                ? "wide CURRENT/LAST/STORED/raw-owner remain alias-backed"
                : "narrow CURRENT/LAST/STORED/raw-owner remain separately bound");
        require(impl.signals.at(target).initial_value == next_current
                && impl.signal_last_values.at(target) == expected_previous
                && impl.driven_values.at(target) == next_current,
            "the runtime public current/LAST and unforced raw value match all four planes");
        require(impl.signal_events.at(target)
                    == std::pair { SimulationTick { 100U + phase_value },
                        std::uint64_t { 7U + phase_value } }
                && impl.signal_transactions.at(target)
                    == std::pair { SimulationTick { 100U + phase_value },
                        std::uint64_t { 7U + phase_value } }
                && impl.signal_value_revisions.at(target)
                    == metadata.value_revision
                && impl.signal_event_scheduling_stamps.at(target).origin.process_domain
                    == ProcessSchedulingDomain::systemverilog
                && impl.signal_event_scheduling_stamps.at(target).origin.phase
                    == SchedulerPhase::active
                && impl.signal_event_scheduling_stamps.at(target)
                        .systemverilog_round == phase_value,
            "each Logic9 commit publishes its exact event, transaction and origin stamps");
        require_logic9_direct_mirrors(impl, target,
            next_current, expected_previous);
        mark_states(next_current);
        lease.release();
        require(values.revision() == old_generation + 1U,
            "each state-changing four-plane commit advances one A4 revision");
    }

    require(std::ranges::all_of(observed_states,
                [](const bool present) { return present; }),
        "width one covers all nine states over commits and wider values cover every state");

    std::array<PackedLogic4PlaneReadLease, 4U> retained_role_leases;
    std::array<std::array<std::vector<std::uint64_t>, 4U>, 4U>
        retained_role_words;
    std::array<PackedLogic4, 4U> retained_role_values {
        values.current(target), values.previous(target), values.stored(target),
        values.owner_value(target, owner)
    };
    constexpr std::array<PackedPlaneRole, 4U> retained_roles {
        PackedPlaneRole::current,
        PackedPlaneRole::previous,
        PackedPlaneRole::stored,
        PackedPlaneRole::owner
    };
    for (std::size_t role = 0U; role < retained_roles.size(); ++role) {
        retained_role_leases[role]
            = values.plane_read_lease(target, retained_roles[role], owner);
        require(static_cast<bool>(retained_role_leases[role]),
            "each Logic9 role can retain an immutable plane snapshot");
        for (std::size_t plane_index = 0U; plane_index < 4U; ++plane_index) {
            const auto words
                = retained_role_leases[role].plane_words(plane_index);
            retained_role_words[role][plane_index]
                .assign(words.begin(), words.end());
        }
    }

    if (width == 129U) {
        const auto workspace_used = runtime->workspace_allocation.used();
        const auto next = logic9_pattern(width, 1U);
        require(next != values.current(target),
            "the Logic9 COW witness changes all nine-state value contents");
        auto mutation = values.prepare_value_change(target, next, next);
        require(values.begin_prepared_publication(mutation),
            "pinned Logic9 roles prepare an atomic COW publication");
        values.publish(std::move(mutation));
        require(runtime->workspace_allocation.used() == workspace_used
                && values.current(target) == next
                && values.previous(target) == retained_role_values[0U]
                && values.stored(target) == next
                && values.owner_value(target, owner) == next,
            "Logic9 COW updates all roles without growing the component slab");

        for (const auto role : { PackedPlaneRole::current,
                 PackedPlaneRole::stored, PackedPlaneRole::owner }) {
            const auto lease = values.plane_read_lease(target, role, owner);
            require(static_cast<bool>(lease),
                "changed Logic9 roles retain their new COW block");
            const auto first_plane = lease.plane_words(0U);
            require(!first_plane.empty()
                    && !runtime->workspace_allocation.owns(first_plane.data(),
                        first_plane.size() * sizeof(std::uint64_t)),
                "pinned Logic9 role COW detaches changed words from the slab");
        }
        for (std::size_t role = 0U; role < retained_roles.size(); ++role) {
            for (std::size_t plane_index = 0U; plane_index < 4U;
                 ++plane_index) {
                require(std::ranges::equal(
                            retained_role_leases[role].plane_words(plane_index),
                            retained_role_words[role][plane_index]),
                    "old Logic9 plane leases preserve all nine-state encodings");
            }
        }
    }

    const auto expected_current = values.current(target);
    const auto expected_previous = values.previous(target);
    const auto expected_stored = values.stored(target);
    const auto expected_event = impl.signal_events.at(target);
    const auto expected_transaction = impl.signal_transactions.at(target);
    const auto expected_revision = impl.signal_value_revisions.at(target);
    require(expected_current == expected_stored
            && values.owner_value(target, owner) == expected_stored
            && (aliases_stored
                ? impl.driver_values.at(target).find(owner) == nullptr
                : impl.driver_values.at(target).find(owner) != nullptr
                    && impl.driver_values.at(target).find(owner)->value
                        == expected_stored),
        aliases_stored
            ? "late observation starts from the committed stored-owner alias"
            : "late observation starts from the committed resolved owner record");
    const auto observed = interpreter.signal_value(target);
    require(observed == expected_current,
        "a late public reference observes the full four-plane Logic9 value");
    require(impl.signals.at(target).initial_value == expected_current
            && impl.signal_last_values.at(target) == expected_previous
            && impl.driven_values.at(target) == expected_stored
            && impl.signal_events.at(target) == expected_event
            && impl.signal_transactions.at(target) == expected_transaction
            && impl.signal_value_revisions.at(target) == expected_revision
            && (aliases_stored
                ? impl.driver_values.at(target).find(owner) == nullptr
                : impl.driver_values.at(target).find(owner) != nullptr
                    && impl.driver_values.at(target).find(owner)->value
                        == expected_stored),
        "late observation preserves CURRENT/LAST/raw owner and exact metadata");

    if (width == 129U) {
        std::weak_ptr<RegionAuthoritativeComponentState> weak_state = state;
        std::weak_ptr<void> weak_workspace
            = runtime->workspace_allocation.storage_lifetime();
        static_cast<void>(values.unbind_packed_slots());
        impl.region_authoritative_state_by_component[component].reset();
        impl.region_authoritative_component_by_signal[target]
            = std::numeric_limits<std::size_t>::max();
        runtime->authoritative_state.reset();
        runtime.reset();
        state.reset();
        require(weak_state.expired() && !weak_workspace.expired(),
            "the A4 state can die while retained plane leases pin the slab");
        for (std::size_t role = 0U; role < retained_roles.size(); ++role) {
            for (std::size_t plane_index = 0U; plane_index < 4U;
                 ++plane_index) {
                require(std::ranges::equal(
                            retained_role_leases[role].plane_words(plane_index),
                            retained_role_words[role][plane_index]),
                    "retained Logic9 role leases remain readable after runtime teardown");
            }
        }
    }
}

} // namespace

void run_native_frontier_logic9_sync_tests()
{
    check_borrowed_plane_storage_detaches();
    ScopedEnvironment kernel_enabled { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_single_owner {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment wide_disjoint_owner {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled { "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    for (const auto width : { 1U, 64U, 65U, 129U, 256U, 1024U }) {
        exercise_logic9_sync_width(width);
    }
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::run_native_frontier_logic9_sync_tests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native frontier Logic9 sync test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
