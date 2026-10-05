// SPDX-License-Identifier: Apache-2.0
#include "runtime_owned_driver_demotion_test_access.hpp"
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error { message };
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
        if (!set(value)) {
            throw std::runtime_error { "failed to set A4 test environment" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (had_previous_) {
            static_cast<void>(set(previous_.c_str()));
        } else {
            unset();
        }
    }

private:
    bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value != nullptr ? value : "") == 0;
#else
        return value != nullptr
            ? ::setenv(name_.c_str(), value, 1) == 0
            : ::unsetenv(name_.c_str()) == 0;
#endif
    }

    void unset() const noexcept
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name_.c_str(), ""));
#else
        static_cast<void>(::unsetenv(name_.c_str()));
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

class ScopedCerrCapture final {
public:
    explicit ScopedCerrCapture(std::ostringstream& output)
        : previous_ { std::cerr.rdbuf(output.rdbuf()) }
    {
    }

    ScopedCerrCapture(const ScopedCerrCapture&) = delete;
    ScopedCerrCapture& operator=(const ScopedCerrCapture&) = delete;

    ~ScopedCerrCapture()
    {
        std::cerr.rdbuf(previous_);
    }

private:
    std::streambuf* previous_;
};

Process partial_writer(const ProcessId id,
    const SignalId input,
    const SignalId output,
    const std::uint32_t offset,
    const std::uint32_t width)
{
    Process process;
    process.id = id;
    process.name = "a4_writer_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, offset, width, false } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteUpdateSlice {
            output, 0U, offset, SignalUpdateDomain::systemverilog_active
        },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

Process whole_writer(const ProcessId id,
    const SignalId input,
    const SignalId output)
{
    auto process = partial_writer(id, input, output, 0U, 0U);
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations[1U] = WriteUpdate {
        output, 0U, SignalUpdateDomain::systemverilog_active
    };
    return process;
}

class WideScheduledUpdateExecutor final : public ProcessExecutor {
public:
    WideScheduledUpdateExecutor(const SignalId input,
        const SignalId output,
        const InstructionIndex wait_instruction,
        std::size_t& resumes,
        ProcessExecutorProgramBinding access_binding)
        : input_ { input }
        , output_ { output }
        , wait_instruction_ { wait_instruction }
        , resumes_ { &resumes }
        , access_binding_ { std::move(access_binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        ++*resumes_;
        context.write_update_in_domain(output_, context.read_signal(input_),
            SignalUpdateDomain::systemverilog_active);
        ProcessResumeResult result {
            wait_instruction_, wait_instruction_ + 1U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    SignalId input_ { };
    SignalId output_ { };
    InstructionIndex wait_instruction_ { };
    std::size_t* resumes_ { };
    ProcessExecutorProgramBinding access_binding_;
};

[[nodiscard]] PackedLogic4 wide_runtime_value(
    const std::size_t width,
    const ValueKind kind,
    const std::size_t phase)
{
    if (kind == ValueKind::logic9) {
        constexpr std::string_view pattern { "UX01ZWLH-" };
        std::string bits;
        bits.reserve(width);
        for (std::size_t bit = 0U; bit < width; ++bit) {
            bits.push_back(pattern[(bit + phase) % pattern.size()]);
        }
        return PackedLogic4::from_logic9_msb_string(bits);
    }
    switch (phase % 4U) {
    case 0U:
        return PackedLogic4(width, Logic4::one);
    case 1U:
        return PackedLogic4(width, Logic4::zero);
    case 2U:
        return PackedLogic4(width, Logic4::x);
    default:
        return PackedLogic4(width, Logic4::z);
    }
}

class WideA4RuntimeCase final {
public:
    WideA4RuntimeCase(const std::size_t width,
        const ValueKind kind,
        const std::string_view suffix,
        const bool boundary_only = false)
        : width_ { width }
        , kind_ { kind }
    {
        const auto initial_source = wide_runtime_value(width_, kind_, 1U);
        const auto initial_output = wide_runtime_value(width_, kind_, 3U);
        source_ = interpreter_.add_signal({
            std::string { "a4.wide.source." } + std::string { suffix },
            initial_source, ResolutionKind::none, kind_ });
        const auto resolution = kind_ == ValueKind::logic9
            ? ResolutionKind::std_logic : ResolutionKind::sv_wire;
        target_ = interpreter_.add_signal({
            std::string { "a4.wide.target." } + std::string { suffix },
            initial_output, resolution, kind_ });
        sink_ = interpreter_.add_signal({
            std::string { "a4.wide.sink." } + std::string { suffix },
            initial_output,
            boundary_only ? resolution : ResolutionKind::none, kind_ });

        auto producer = whole_writer(0U, source_, target_);
        producer.initialize = false;
        if (kind_ == ValueKind::logic9) {
            producer.register_value_kinds = { ValueKind::logic9 };
        }
        require(interpreter_.add_process(std::move(producer)) == 0U,
            "wide A4 producer has a stable process identity");
        const auto& registered = interpreter_.process_program(0U);
        interpreter_.set_process_executor(0U,
            std::make_unique<WideScheduledUpdateExecutor>(
                source_, target_, 2U, resumes_,
                ProcessExecutorProgramBinding {
                    registered, registered, 0U }));

        Process consumer;
        consumer.id = 1U;
        consumer.name = "a4_wide_consumer_" + std::string { suffix };
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.initialize = false;
        consumer.register_count = 1U;
        if (kind_ == ValueKind::logic9) {
            consumer.register_value_kinds = { ValueKind::logic9 };
        }
        consumer.static_sensitivity = { { target_, EdgeKind::any } };
        consumer.driver_regions = { { sink_, 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, target_ },
            WriteUpdate { sink_, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter_.add_process(std::move(consumer)) == 1U,
            "wide A4 reader has a stable process identity");
        interpreter_.start();
    }

    [[nodiscard]] PackedLogic4 value(const std::size_t phase) const
    {
        return wide_runtime_value(width_, kind_, phase);
    }

    void drive(const std::size_t phase)
    {
        interpreter_.deposit_signal(source_, value(phase));
        drain();
    }

    void drain()
    {
        require(interpreter_.run().status == RunStatus::completed,
            "wide A4 scheduled writes drain normally");
    }

    [[nodiscard]] Interpreter& interpreter() noexcept
    {
        return interpreter_;
    }

    [[nodiscard]] SignalId target() const noexcept
    {
        return target_;
    }

    [[nodiscard]] SignalId source_signal() const noexcept
    {
        return source_;
    }

    [[nodiscard]] SignalId sink() const noexcept
    {
        return sink_;
    }

    [[nodiscard]] std::size_t resumes() const noexcept
    {
        return resumes_;
    }

private:
    std::size_t width_ { };
    ValueKind kind_ { ValueKind::logic4 };
    Interpreter interpreter_;
    SignalId source_ { };
    SignalId target_ { };
    SignalId sink_ { };
    std::size_t resumes_ { };
};

RegionGraph build_graph(const std::vector<Process>& processes,
    const std::vector<RegionSignalDescriptor>& signal_descriptors)
{
    std::vector<const Process*> programs;
    programs.reserve(processes.size());
    for (const auto& process : processes) {
        programs.push_back(&process);
    }
    return RegionGraph::build(programs, signal_descriptors);
}

void check_driver_layout_and_owner_masks()
{
    std::vector<RegionSignalDescriptor> descriptors(5U, { 129U });
    descriptors[0U].width = 65U;
    descriptors[1U].width = 64U;
    descriptors[3U].width = 4U;
    descriptors[3U].value_kind = ValueKind::logic9;
    descriptors[4U].width = 4U;
    const std::vector<Process> processes {
        partial_writer(0U, 0U, 2U, 0U, 65U),
        partial_writer(1U, 1U, 2U, 65U, 64U),
        whole_writer(2U, 4U, 3U),
    };
    const auto graph = build_graph(processes, descriptors);
    const std::array component_signals { 3U, 2U, 1U };
    const auto layout
        = SignalDriverLayout::build(graph, component_signals);
    const auto inventory = SignalDriverLayout::inventory(graph);
    require(signal_driver_inventory_matches_structure(inventory, graph),
        "elaboration inventory matches the graph writer structure");
    const auto inventory_layout = SignalDriverLayout::build_from_inventory(
        graph, component_signals, inventory, { }, { });
    require(inventory_layout.signal(2U).storage_class
                == layout.signal(2U).storage_class
            && inventory_layout.owners(2U).size() == layout.owners(2U).size()
            && std::ranges::equal(inventory_layout.owner_mask_words(2U, 0U),
                layout.owner_mask_words(2U, 0U))
            && std::ranges::equal(inventory_layout.owner_mask_words(2U, 1U),
                layout.owner_mask_words(2U, 1U)),
        "a persisted inventory binds the same cross-word owner planes");
    auto observed_graph = graph;
    static_cast<void>(observed_graph.observe_signal(
        2U, RegionObservation::current));
    require(signal_driver_inventory_matches_structure(
                inventory, observed_graph),
        "runtime observations do not invalidate structural writer proof");
    static_cast<void>(observed_graph.observe_signal(
        3U, RegionObservation::mutation));
    require(signal_driver_inventory_matches_structure(
                inventory, observed_graph),
        "force and mutation observations do not invalidate structural proof");
    auto external_descriptors = descriptors;
    external_descriptors[2U].external_driver = true;
    const auto external_graph = build_graph(processes, external_descriptors);
    require(external_graph.signals()[2U].drivers
                == RegionDriverClass::resolved
            && inventory.signals[2U].drivers
                == SignalDriverInventoryDriverClass::disjoint_partial
            && inventory.signals[2U].storage_class
                == SignalDriverStorageClass::disjoint_owner
            && inventory.signals[2U].owner_count == 2U
            && signal_driver_inventory_matches_structure(
                inventory, external_graph),
        "external-driver runtime class stays separate from structural inventory");
    const auto external_layout = SignalDriverLayout::build_from_inventory(
        external_graph, component_signals, inventory, { }, { });
    require(external_layout.signal(2U).storage_class
                == SignalDriverStorageClass::resolved_table
            && external_layout.owners(2U).empty(),
        "external driver state revokes structural owners at the runtime gate");
    require(layout.signal_count() == 3U && layout.contains(1U)
            && layout.contains(2U) && layout.contains(3U)
            && !layout.contains(0U),
        "component plane layout is sparse and signal-ID searchable");
    require(layout.signal(2U).storage_class
                == SignalDriverStorageClass::disjoint_owner
            && layout.owners(2U).size() == 2U,
        "proven disjoint writes receive per-owner storage");
    const auto lower = layout.owner_mask_words(2U, 0U);
    const auto upper = layout.owner_mask_words(2U, 1U);
    require(lower.size() == 3U && upper.size() == 3U
            && lower[0U] == std::numeric_limits<std::uint64_t>::max()
            && lower[1U] == UINT64_C(1) && lower[2U] == 0U
            && upper[0U] == 0U
            && upper[1U] == UINT64_C(0xfffffffffffffffe)
            && upper[2U] == UINT64_C(1),
        "owner masks preserve a cross-word partition exactly");
    require(layout.signal(3U).storage_class
                == SignalDriverStorageClass::single_owner
            && layout.signal(3U).value_kind == ValueKind::logic9
            && layout.owners(3U).size() == 1U,
        "whole-owner Logic9 signal receives four-plane-capable storage");
    require(layout.signal(1U).storage_class
                == SignalDriverStorageClass::resolved_table
            && layout.owners(1U).empty(),
        "undriven signal remains on the ordinary state route");

    auto overlapping = processes;
    auto overlapping_descriptors = descriptors;
    overlapping_descriptors[1U].width = 65U;
    overlapping[1U] = partial_writer(1U, 1U, 2U, 64U, 65U);
    const auto overlapping_graph = build_graph(
        overlapping, overlapping_descriptors);
    const auto overlapping_layout = SignalDriverLayout::build(
        overlapping_graph, std::span<const SignalId> { component_signals });
    require(overlapping_layout.signal(2U).storage_class
                == SignalDriverStorageClass::resolved_table
            && overlapping_layout.owners(2U).empty(),
        "overlapping owners fail closed to retained DriverTable storage");
    require(!signal_driver_inventory_matches_structure(
                inventory, overlapping_graph),
        "a structural writer change invalidates the persisted inventory");

    auto fork_writer = whole_writer(0U, 0U, 4U);
    fork_writer.operations = {
        Fork { { 3U }, ForkJoinKind::none },
        Halt { },
        Halt { },
        LoadConstant { 0U, PackedLogic4 { 4U, Logic4::one } },
        WriteUpdate { 4U, 0U,
            SignalUpdateDomain::systemverilog_active },
        ForkEnd { },
    };
    const auto fork_graph = build_graph(
        std::vector<Process> { std::move(fork_writer) }, descriptors);
    require(fork_graph.signals()[4U].dynamic_fork_writers,
        "fork-owned writes are marked dynamic in the structural graph");
    require(!signal_driver_inventory_matches_structure(
                inventory, fork_graph),
        "a newly dynamic fork writer invalidates the prior owner proof");
    const auto fork_inventory = SignalDriverLayout::inventory(fork_graph);
    require(fork_inventory.signals[4U].storage_class
                == SignalDriverStorageClass::resolved_table
            && fork_inventory.signals[4U].owner_count == 0U,
        "fork-owned signals retain resolved storage without static owners");
}

void check_prepared_wide_owner_publications()
{
    std::vector<RegionSignalDescriptor> descriptors(3U, { 129U });
    descriptors[0U].width = 65U;
    descriptors[1U].width = 64U;
    const std::vector<Process> processes {
        partial_writer(0U, 0U, 2U, 0U, 65U),
        partial_writer(1U, 1U, 2U, 65U, 64U),
    };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };

    constexpr auto width = std::size_t { 129U };
    const PackedLogic4 high_impedance { width, Logic4::z };
    values.seed_signal(2U, high_impedance, high_impedance, high_impedance);
    values.seed_owner(2U, 0U, high_impedance);
    values.seed_owner(2U, 1U, high_impedance);

    PackedLogic4 low_owner { width, Logic4::z };
    low_owner.insert_bits(PackedLogic4 { 65U, Logic4::one }, 0U);
    auto low_mutation = values.prepare_owner_change(
        2U, 0U, low_owner, low_owner, low_owner);
    values.publish(std::move(low_mutation));
    require(values.current(2U) == low_owner
            && values.previous(2U) == high_impedance
            && values.stored(2U) == low_owner
            && values.owner_value(2U, 0U) == low_owner
            && values.owner_value(2U, 1U) == high_impedance,
        "first prepared owner write advances complete prior/current/stored state");

    PackedLogic4 high_owner { width, Logic4::z };
    high_owner.insert_bits(PackedLogic4 { 64U, Logic4::one }, 65U);
    PackedLogic4 combined = low_owner;
    combined.insert_bits(PackedLogic4 { 64U, Logic4::one }, 65U);
    auto high_mutation = values.prepare_owner_change(
        2U, 1U, high_owner, combined, combined);
    values.publish(std::move(high_mutation));
    require(values.current(2U) == combined
            && values.previous(2U) == low_owner
            && values.stored(2U) == combined
            && values.owner_value(2U, 0U) == low_owner
            && values.owner_value(2U, 1U) == high_owner,
        "second owner publication preserves its sibling raw record");

    auto illegal_owner = high_owner;
    illegal_owner.insert_bits(PackedLogic4 { 1U, Logic4::zero }, 0U);
    bool rejected_owner_overlap { };
    try {
        static_cast<void>(values.prepare_owner_change(
            2U, 1U, illegal_owner, combined, combined));
    } catch (const std::invalid_argument&) {
        rejected_owner_overlap = true;
    }
    require(rejected_owner_overlap,
        "one owner cannot prepare a mutation over a sibling's mask");
    require(values.dirty_signals().size() == 1U
            && values.dirty_signals()[0U] != 0U,
        "visible and raw-owner mutations set the component dirty plane");
    values.clear_dirty();
    require(values.dirty_signals()[0U] == 0U,
        "component dirty state clears without allocating");
}

void check_preallocated_wide_role_copy()
{
    constexpr auto width = std::size_t { 129U };
    std::vector<RegionSignalDescriptor> descriptors(2U,
        RegionSignalDescriptor { static_cast<std::uint32_t>(width) });
    descriptors[1U].resolution = ResolutionKind::none;
    const std::vector<Process> processes { whole_writer(0U, 0U, 1U) };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { SignalId { 1U } };
    const std::array<SignalId, 0U> no_projected_slices { };
    const std::array stored_owner_aliases { SignalId { 1U } };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids,
            no_projected_slices, stored_owner_aliases),
        PackedSlotBindingPolicy::experimental_wide
    };

    const auto current = wide_runtime_value(width, ValueKind::logic4, 0U);
    const auto previous = wide_runtime_value(width, ValueKind::logic4, 3U);
    const auto stored = wide_runtime_value(width, ValueKind::logic4, 1U);
    values.seed_signal(1U, current, previous, stored);
    values.seed_owner(1U, 0U, stored);

    auto current_slot = current;
    auto previous_slot = previous;
    auto stored_slot = stored;
    values.stage_packed_signal_slots(
        1U, current_slot, previous_slot, stored_slot);
    values.stage_packed_owner_stored_alias(1U, 0U);
    require(values.bind_packed_slots() == 3U
            && values.packed_slots_bound()
            && values.layout().owners(1U).size() == 1U
            && values.layout().owners(1U)[0U].aliases_stored,
        "wide role-copy fixture binds a certified stored-owner alias");

    PackedLogic4 copied_current(width, Logic4::zero);
    PackedLogic4 copied_previous(width, Logic4::zero);
    PackedLogic4 copied_stored(width, Logic4::zero);
    PackedLogic4 copied_owner(width, Logic4::zero);
    require(values.try_copy_wide_logic4_role_into(
                1U, PackedPlaneRole::current, 0U, copied_current)
            && values.try_copy_wide_logic4_role_into(
                1U, PackedPlaneRole::previous, 0U, copied_previous)
            && values.try_copy_wide_logic4_role_into(
                1U, PackedPlaneRole::stored, 0U, copied_stored)
            && values.try_copy_wide_logic4_role_into(
                1U, PackedPlaneRole::owner, 0U, copied_owner),
        "all wide Logic4 role planes copy into unique owning seed values");
    require(copied_current == current && copied_previous == previous
            && copied_stored == stored && copied_owner == stored,
        "role copies preserve current, LAST, stored, and aliased raw-owner values");
    require((copied_current.aval_words().back() & ~UINT64_C(1)) == 0U
            && (copied_current.bval_words().back() & ~UINT64_C(1)) == 0U
            && (copied_previous.aval_words().back() & ~UINT64_C(1)) == 0U
            && (copied_previous.bval_words().back() & ~UINT64_C(1)) == 0U
            && (copied_stored.aval_words().back() & ~UINT64_C(1)) == 0U
            && (copied_stored.bval_words().back() & ~UINT64_C(1)) == 0U,
        "wide role copies keep unused high tail bits clear");

    const std::vector<std::uint64_t> original_aval {
        copied_current.aval_words().begin(), copied_current.aval_words().end()
    };
    const std::vector<std::uint64_t> original_bval {
        copied_current.bval_words().begin(), copied_current.bval_words().end()
    };
    auto shared_current = copied_current;
    require(!values.try_copy_wide_logic4_role_into(
                1U, PackedPlaneRole::stored, 0U, copied_current)
            && std::ranges::equal(copied_current.aval_words(), original_aval)
            && std::ranges::equal(copied_current.bval_words(), original_bval)
            && shared_current == current,
        "a shared seed destination is rejected without partial mutation");

    PackedLogic4 wrong_width(width + 1U, Logic4::x);
    const auto wrong_width_before = wrong_width;
    require(!values.try_copy_wide_logic4_role_into(
                1U, PackedPlaneRole::current, 0U, wrong_width)
            && wrong_width == wrong_width_before,
        "a mismatched seed width declines without changing its destination");

    const auto external_before = current_slot;
    require(!values.try_copy_wide_logic4_role_into(
                1U, PackedPlaneRole::current, 0U, current_slot)
            && current_slot == external_before,
        "an externally backed destination is rejected without changing the live slot");
}

void check_logic9_owner_planes()
{
    std::vector<RegionSignalDescriptor> descriptors(2U, { 4U });
    descriptors[1U].value_kind = ValueKind::logic9;
    const std::vector<Process> processes { whole_writer(0U, 0U, 1U) };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { 1U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };
    const auto initial = PackedLogic4::from_logic9_msb_string("UZ01");
    const auto replacement = PackedLogic4::from_logic9_msb_string("HX0L");
    values.seed_signal(1U, initial, initial, initial);
    values.seed_owner(1U, 0U, initial);
    auto mutation = values.prepare_owner_change(
        1U, 0U, replacement, replacement, replacement);
    values.publish(std::move(mutation));
    require(values.current(1U) == replacement
            && values.previous(1U) == initial
            && values.stored(1U) == replacement
            && values.owner_value(1U, 0U) == replacement,
        "Logic9 publication preserves all four planes and canonical states");
    std::array<std::span<const std::uint64_t>, 4U> current_planes;
    require(values.current_planes(1U, current_planes)
            && current_planes[0U].size() == 1U
            && current_planes[1U].size() == 1U
            && current_planes[2U].size() == 1U
            && current_planes[3U].size() == 1U,
        "borrowed Logic9 component inputs expose all four current planes");
    for (std::size_t plane = 0U; plane < current_planes.size(); ++plane) {
        require(std::ranges::equal(current_planes[plane],
                    replacement.logic9_plane_words(plane)),
            "borrowed Logic9 component inputs preserve every ordinal bit");
    }
    std::span<const std::uint64_t> logic4_aval;
    std::span<const std::uint64_t> logic4_bval;
    require(!values.current_logic4_planes(
                1U, logic4_aval, logic4_bval)
            && logic4_aval.empty() && logic4_bval.empty(),
        "the legacy two-plane view declines Logic9 signals");

    values.mirror_logic9_word(1U, initial.logic9_low_word(),
        replacement.logic9_low_word(), replacement.logic9_low_word());
    require(values.current(1U) == replacement
            && values.previous(1U) == initial
            && values.stored(1U) == replacement,
        "narrow Logic9 word mirror updates four value planes");
}

void check_mixed_shared_plane_rebind_preflight()
{
    const std::array<std::uint32_t, 4U> widths {
        65U, 129U, 129U, 1U
    };
    const std::array<ValueKind, 4U> kinds {
        ValueKind::logic4, ValueKind::logic9,
        ValueKind::logic4, ValueKind::logic4
    };
    const std::array<SignalId, 4U> signal_ids { 1U, 2U, 3U, 4U };
    const std::array<ProcessId, 4U> owner_ids { 0U, 1U, 2U, 3U };

    const std::array<SignalId, 4U> source_ids { 5U, 6U, 7U, 8U };
    std::vector<RegionSignalDescriptor> descriptors(9U, { 65U });
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        auto& output = descriptors[signal_ids[index]];
        output.width = widths[index];
        output.value_kind = kinds[index];
        auto& source = descriptors[source_ids[index]];
        source.width = widths[index];
        source.value_kind = kinds[index];
    }
    std::vector<Process> processes;
    processes.reserve(signal_ids.size());
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        auto process = whole_writer(owner_ids[index], source_ids[index],
            signal_ids[index]);
        process.register_value_kinds = { kinds[index] };
        processes.push_back(std::move(process));
    }
    const auto graph = build_graph(processes, descriptors);
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };

    std::vector<PackedLogic4> current;
    std::vector<PackedLogic4> previous;
    std::vector<PackedLogic4> stored;
    std::vector<PackedLogic4> owner;
    current.reserve(signal_ids.size());
    previous.reserve(signal_ids.size());
    stored.reserve(signal_ids.size());
    owner.reserve(signal_ids.size());
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        current.push_back(wide_runtime_value(widths[index], kinds[index], 0U));
        previous.push_back(wide_runtime_value(widths[index], kinds[index], 1U));
        stored.push_back(wide_runtime_value(widths[index], kinds[index], 2U));
        owner.push_back(wide_runtime_value(widths[index], kinds[index], 3U));
        values.seed_signal(signal_ids[index], current.back(),
            previous.back(), stored.back());
        const auto owners = values.layout().owners(signal_ids[index]);
        require(owners.size() == 1U
                && owners.front().process == owner_ids[index],
            "each matching-width single writer remains a direct layout owner");
        values.seed_owner(signal_ids[index], owner_ids[index], owner.back());
        require(values.supports_packed_slot_binding(signal_ids[index]),
            "each mixed-kind single-owner value supports versioned binding");
        values.stage_packed_signal_slots(signal_ids[index], current.back(),
            previous.back(), stored.back());
        values.stage_packed_owner_slot(signal_ids[index], owner_ids[index],
            owner.back());
    }

    require(values.can_bind_packed_slots(),
        "preflight accepts mixed Logic4 and Logic9 spans in shared planes");
    require(values.bind_packed_slots() == 16U,
        "all four roles bind across the mixed shared plane buffers");

    std::array<std::array<std::span<const std::uint64_t>, 4U>, 4U>
        current_planes;
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        require(values.current_planes(signal_ids[index],
                    current_planes[index]),
            "every mixed component signal exposes its current plane spans");
    }
    const std::array<std::size_t, 4U> word_counts { 2U, 3U, 3U, 1U };
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        require(current_planes[index][0U].size() == word_counts[index]
                && current_planes[index][1U].size() == word_counts[index],
            "each value-role binding covers exactly its own word span");
    }
    for (std::size_t index = 1U; index < signal_ids.size(); ++index) {
        require(current_planes[index - 1U][0U].data()
                    + current_planes[index - 1U][0U].size()
                    == current_planes[index][0U].data()
                && current_planes[index - 1U][1U].data()
                    + current_planes[index - 1U][1U].size()
                    == current_planes[index][1U].data(),
            "Logic4 and Logic9 values occupy adjacent shared value-plane spans");
    }
    require(current_planes[0U][2U].empty()
            && current_planes[0U][3U].empty()
            && current_planes[1U][2U].size() == word_counts[1U]
            && current_planes[1U][3U].size() == word_counts[1U]
            && current_planes[2U][2U].empty()
            && current_planes[2U][3U].empty()
            && current_planes[3U][2U].empty()
            && current_planes[3U][3U].empty(),
        "only the Logic9 binding occupies the shared four-state extension planes");
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        require(values.current(signal_ids[index]) == current[index]
                && values.previous(signal_ids[index]) == previous[index]
                && values.stored(signal_ids[index]) == stored[index]
                && values.owner_value(
                    signal_ids[index], owner_ids[index]) == owner[index],
            "binding preserves every distinct seeded role");
    }
    const auto bound_revision = values.revision();
    require(!values.can_bind_packed_slots()
            && values.revision() == bound_revision,
        "bound-slot preflight declines without changing the A4 revision");

    require(values.unbind_packed_slots() == 16U,
        "all shared-buffer roles unbind before the checked mirror update");
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        require(current[index] == values.current(signal_ids[index])
                && previous[index] == values.previous(signal_ids[index])
                && stored[index] == values.stored(signal_ids[index])
                && owner[index]
                    == values.owner_value(signal_ids[index], owner_ids[index]),
            "unbinding materializes every mixed-kind role unchanged");
    }

    std::vector<PackedLogic4> updated_current;
    std::vector<PackedLogic4> updated_previous;
    std::vector<PackedLogic4> updated_stored;
    std::vector<PackedLogic4> updated_owner;
    updated_current.reserve(signal_ids.size());
    updated_previous.reserve(signal_ids.size());
    updated_stored.reserve(signal_ids.size());
    updated_owner.reserve(signal_ids.size());
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        updated_current.push_back(
            wide_runtime_value(widths[index], kinds[index], 5U));
        updated_previous.push_back(
            wide_runtime_value(widths[index], kinds[index], 6U));
        updated_stored.push_back(
            wide_runtime_value(widths[index], kinds[index], 7U));
        updated_owner.push_back(
            wide_runtime_value(widths[index], kinds[index], 8U));
        values.mirror_visible(signal_ids[index], updated_previous.back(),
            updated_current.back());
        values.mirror_stored(signal_ids[index], updated_stored.back());
        values.mirror_owner(
            signal_ids[index], owner_ids[index], updated_owner.back());
        current[index] = values.current(signal_ids[index]);
        previous[index] = values.previous(signal_ids[index]);
        stored[index] = values.stored(signal_ids[index]);
        owner[index]
            = values.owner_value(signal_ids[index], owner_ids[index]);
    }

    require(values.can_bind_packed_slots(),
        "preflight accepts updated offsets after an unbound mirror write");
    require(values.bind_packed_slots() == 16U,
        "the same mixed shared-buffer spans can be rebound after update");
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        require(values.packed_signal_slots_bound(signal_ids[index])
                && values.packed_owner_slot_bound(
                    signal_ids[index], owner_ids[index])
                && values.current(signal_ids[index]) == updated_current[index]
                && values.previous(signal_ids[index])
                    == updated_previous[index]
                && values.stored(signal_ids[index]) == updated_stored[index]
                && values.owner_value(
                    signal_ids[index], owner_ids[index])
                    == updated_owner[index],
            "rebind preserves the updated current, LAST, stored, and owner roles");
    }
}

void check_logic9_packed_slot_binding()
{
    std::vector<RegionSignalDescriptor> descriptors(2U, { 9U });
    descriptors[1U].value_kind = ValueKind::logic9;
    const std::vector<Process> processes { whole_writer(0U, 0U, 1U) };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { 1U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };

    auto current = PackedLogic4::from_logic9_msb_string("UX01ZWLH-");
    auto previous = PackedLogic4::from_logic9_msb_string("ZLH0W1UX-");
    auto stored = PackedLogic4::from_logic9_msb_string("LHUWZ10X-");
    auto owner = PackedLogic4::from_logic9_msb_string("H0WXLUZ1-");
    values.seed_signal(1U, current, previous, stored);
    values.seed_owner(1U, 0U, owner);
    require(values.supports_packed_slot_binding(1U),
        "a seeded single-owner narrow Logic9 signal supports slot binding");
    values.stage_packed_signal_slots(
        1U, current, previous, stored);
    values.stage_packed_owner_slot(1U, 0U, owner);
    require(values.bind_packed_slots() == 4U
            && values.packed_signal_slots_bound(1U)
            && values.packed_owner_slot_bound(1U, 0U),
        "narrow Logic9 current, previous, stored, and raw slots bind together");

    std::array<std::span<const std::uint64_t>, 4U> current_planes;
    require(values.current_planes(1U, current_planes),
        "a bound Logic9 current value exposes all four authoritative planes");
    for (std::size_t plane = 0U; plane < current_planes.size(); ++plane) {
        require(current.logic9_plane_words(plane).data()
                    == current_planes[plane].data()
                && current.logic9_plane_words(plane).size() == 1U,
            "each bound Logic9 current plane aliases its slot storage");
    }
    const auto assigned = PackedLogic4::from_logic9_msb_string("HLWZ10UX-");
    current.assign_logic9_word(assigned.logic9_low_word());
    require(current == assigned && values.current(1U) == assigned
            && current.logic9_low_word() == assigned.logic9_low_word(),
        "narrow Logic9 word assignment updates all bound planes");

    const auto masked_source
        = PackedLogic4::from_logic9_msb_string("-UX01ZWLH");
    auto expected_owner = owner;
    expected_owner.insert_masked_logic9_word(
        masked_source.logic9_low_word(), UINT64_C(0x155));
    owner.insert_masked_logic9_word(
        masked_source.logic9_low_word(), UINT64_C(0x155));
    require(owner == expected_owner
            && owner.matches_masked_logic9_word(
                expected_owner.logic9_low_word(), UINT64_C(0x1ff)),
        "masked Logic9 owner updates read and write four bound planes");
    values.mirror_owner(1U, 0U, owner);
    PackedLogic4 owner_snapshot;
    owner_snapshot = owner;
    owner_snapshot.set_logic9(0U, Logic9::zero);
    require(owner_snapshot != owner && values.owner_value(1U, 0U) == owner,
        "copying a bound Logic9 owner creates an independent owning value");

    const auto initial = current;
    const auto next = PackedLogic4::from_logic9_msb_string("U01-HLWXZ");
    auto mutation = values.prepare_owner_change(
        1U, 0U, next, next, next);
    values.publish(std::move(mutation));
    require(current == next && stored == next && owner == next
            && values.previous(1U) == initial
            && values.current(1U) == next
            && values.stored(1U) == next
            && values.owner_value(1U, 0U) == next,
        "Logic9 publication advances LAST and installs current/stored/raw together");

    const auto normalized = Logic9Word {
        9U, { UINT64_C(1), 0U, 0U, UINT64_C(1) }
    };
    auto normalized_target
        = PackedLogic4::from_logic9_msb_string("000000000");
    normalized_target.assign_logic9_word(normalized);
    require(normalized_target.get_logic9(0U) == Logic9::x,
        "named Logic9 word assignment still normalizes reserved encodings");
    require(values.unbind_packed_slots() == 4U,
        "Logic9 unbind releases the four authoritative planes");
    bool materialized_inline { true };
    for (std::size_t plane = 0U; plane < current_planes.size(); ++plane) {
        materialized_inline = materialized_inline
            && current.logic9_plane_words(plane).data()
                != current_planes[plane].data();
    }
    require(materialized_inline
            && current == next
            && previous == initial && stored == next && owner == next,
        "unbind materializes every narrow Logic9 plane into public slots");

    AuthoritativeSignalPlanes invalid_values {
        SignalDriverLayout::build(graph, signal_ids)
    };
    auto invalid_current = initial;
    auto invalid_previous = previous;
    auto invalid_stored = stored;
    auto invalid_owner = owner;
    invalid_values.seed_signal(1U, invalid_current,
        invalid_previous, invalid_stored);
    invalid_values.seed_owner(1U, 0U, invalid_owner);
    invalid_values.stage_packed_signal_slots(1U, invalid_current,
        invalid_previous, invalid_stored);
    invalid_values.stage_packed_owner_slot(1U, 0U, invalid_owner);
    static_cast<void>(invalid_values.bind_packed_slots());
    const Logic9Word reserved {
        9U, { UINT64_C(1), 0U, 0U, UINT64_C(1) }
    };
    invalid_values.mirror_logic9_word(
        1U, initial.logic9_low_word(), reserved, reserved);
    std::array<std::span<const std::uint64_t>, 4U> declined_planes;
    require(!invalid_values.valid()
            && invalid_current == initial
            && invalid_previous == previous
            && invalid_stored == stored
            && invalid_owner == owner
            && !invalid_values.current_planes(1U, declined_planes),
        "reserved plane evidence invalidates a bound Logic9 component before publication");

    std::vector<RegionSignalDescriptor> wide_descriptors(2U, { 65U });
    wide_descriptors[1U].value_kind = ValueKind::logic9;
    const auto wide_graph = build_graph(processes, wide_descriptors);
    AuthoritativeSignalPlanes wide_values {
        SignalDriverLayout::build(wide_graph, signal_ids)
    };
    const std::string wide_bits(65U, 'U');
    const auto wide_initial
        = PackedLogic4::from_logic9_msb_string(wide_bits);
    wide_values.seed_signal(
        1U, wide_initial, wide_initial, wide_initial);
    wide_values.seed_owner(1U, 0U, wide_initial);
    require(!wide_values.supports_packed_slot_binding(1U),
        "wide Logic9 values remain outside the one-word borrowed-slot path");
}

void check_reserved_logic9_invalidates_component()
{
    std::vector<RegionSignalDescriptor> descriptors(2U, { 4U });
    descriptors[1U].value_kind = ValueKind::logic9;
    const std::vector<Process> processes { whole_writer(0U, 0U, 1U) };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { 1U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };
    const auto initial = PackedLogic4::from_logic9_msb_string("UUUU");
    values.seed_signal(1U, initial, initial, initial);
    values.seed_owner(1U, 0U, initial);
    const Logic9Word reserved {
        4U, { UINT64_C(1), 0U, 0U, UINT64_C(1) }
    };
    auto stale_mutation = values.prepare_value_change(
        1U, initial, initial);
    const auto recovered = PackedLogic4::from_logic9_msb_string("U0HX");
    std::vector<AuthoritativeSignalPlanes::PreparedMutation> stale_group;
    stale_group.push_back(values.prepare_owner_change(
        1U, 0U, recovered, recovered, recovered));
    values.mirror_logic9_word(1U,
        initial.logic9_low_word(), reserved, reserved);
    values.publish(std::move(stale_mutation));
    values.publish_group(std::move(stale_group));
    values.mirror_visible(1U, initial, initial);
    std::array<std::span<const std::uint64_t>, 4U> invalid_planes;
    require(!values.valid() && values.current(1U) == initial
            && values.previous(1U) == initial
            && values.stored(1U) == initial
            && values.owner_value(1U, 0U) == initial
            && !values.current_planes(1U, invalid_planes)
            && std::ranges::all_of(invalid_planes,
                [](const auto& plane) { return plane.empty(); }),
        "reserved Logic9 codes invalidate before any component plane changes");
    bool declined_prepared_write { };
    try {
        static_cast<void>(values.prepare_value_change(
            1U, initial, initial));
    } catch (const std::logic_error&) {
        declined_prepared_write = true;
    }
    require(declined_prepared_write,
        "an invalid component cannot prepare new trusted mutations");

    AuthoritativeSignalPlanes replacement_values {
        SignalDriverLayout::build(graph, signal_ids)
    };
    replacement_values.seed_signal(1U, recovered, initial, recovered);
    replacement_values.seed_owner(1U, 0U, recovered);
    require(replacement_values.valid()
            && replacement_values.current(1U) == recovered
            && replacement_values.previous(1U) == initial
            && replacement_values.owner_value(1U, 0U) == recovered,
        "a replacement component snapshot can reseed valid values and owners");
}

void check_nonallocating_runtime_mirrors()
{
    std::vector<RegionSignalDescriptor> descriptors(2U, { 65U });
    const std::vector<Process> processes { whole_writer(0U, 0U, 1U) };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { 1U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };
    const PackedLogic4 initial { 65U, Logic4::z };
    const PackedLogic4 replacement { 65U, Logic4::one };
    values.seed_signal(1U, initial, initial, initial);
    values.seed_owner(1U, 0U, initial);
    values.mirror_owner(1U, 0U, replacement);
    values.mirror_stored(1U, replacement);
    values.mirror_visible(1U, initial, replacement);
    require(values.current(1U) == replacement
            && values.previous(1U) == initial
            && values.stored(1U) == replacement
            && values.owner_value(1U, 0U) == replacement,
        "nonallocating mirrors retain current, previous, stored, and owner state");

    std::vector<RegionSignalDescriptor> narrow_descriptors(2U, { 4U });
    const std::vector<Process> narrow_processes {
        whole_writer(0U, 0U, 1U)
    };
    const auto narrow_graph = build_graph(
        narrow_processes, narrow_descriptors);
    AuthoritativeSignalPlanes narrow {
        SignalDriverLayout::build(narrow_graph, signal_ids)
    };
    const PackedLogic4 narrow_initial { 4U, Logic4::z };
    narrow.seed_signal(1U, narrow_initial, narrow_initial, narrow_initial);
    narrow.seed_owner(1U, 0U, narrow_initial);
    const PackedLogic4 narrow_updated { 4U, Logic4::one };
    narrow.mirror_logic4_word(1U,
        Logic4Word { 4U, 0U, UINT64_C(0xf) },
        Logic4Word { 4U, UINT64_C(0xf), 0U },
        Logic4Word { 4U, UINT64_C(0xf), 0U });
    narrow.mirror_owner(1U, 0U, narrow_updated);
    require(narrow.current(1U) == narrow_updated
            && narrow.previous(1U) == narrow_initial
            && narrow.stored(1U) == narrow_updated
            && narrow.owner_value(1U, 0U) == narrow_updated,
        "narrow native-word publication mirrors value and raw-owner planes");
}

void check_packed_slot_value_semantics()
{
    const auto oversized_width
        = std::numeric_limits<std::size_t>::max() / 4U + 1U;
    bool rejected_oversized_width { };
    try {
        const PackedLogic4 oversized { oversized_width, Logic4::zero };
        static_cast<void>(oversized);
    } catch (const std::length_error&) {
        rejected_oversized_width = true;
    }
    require(rejected_oversized_width,
        "packed value width cannot overlap the reserved representation tags");

    std::vector<RegionSignalDescriptor> descriptors(2U, { 4U });
    const std::vector<Process> processes { whole_writer(0U, 0U, 1U) };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { 1U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };

    auto current = PackedLogic4::from_msb_string("0000");
    auto previous = PackedLogic4::from_msb_string("1010");
    auto stored = PackedLogic4::from_msb_string("0011");
    auto owner = PackedLogic4::from_msb_string("1100");
    values.seed_signal(1U, current, previous, stored);
    values.seed_owner(1U, 0U, owner);
    require(values.supports_packed_slot_binding(1U),
        "a seeded single-owner Logic4 signal has a fixed-width backing");
    values.stage_packed_signal_slots(
        1U, current, previous, stored);
    values.stage_packed_owner_slot(1U, 0U, owner);
    require(values.bind_packed_slots() == 4U
            && values.packed_signal_slots_bound(1U)
            && values.packed_owner_slot_bound(1U, 0U)
            && !values.requires_prewrite_unbind(),
        "three signal roles and the complete owner role bind together");

    std::array<std::span<const std::uint64_t>, 4U> current_planes;
    require(values.current_planes(1U, current_planes)
            && current.aval_words().data() == current_planes[0U].data()
            && current.bval_words().data() == current_planes[1U].data(),
        "the runtime current value directly views its component planes");

    current.set(0U, Logic4::one);
    require(current.to_msb_string() == "0001"
            && values.current(1U).to_msb_string() == "0001",
        "a bound Logic4 mutation updates the authoritative current plane");
    PackedLogic4 copy_assigned_snapshot;
    copy_assigned_snapshot = current;
    copy_assigned_snapshot.set(2U, Logic4::one);
    require(copy_assigned_snapshot.to_msb_string() == "0101"
            && current.to_msb_string() == "0001"
            && values.current(1U).to_msb_string() == "0001",
        "copy assignment from a bound slot creates an independent snapshot");
    const PackedLogic4 copied_current = current;
    auto moved_current = std::move(current);
    require(copied_current.to_msb_string() == "0001"
            && moved_current.to_msb_string() == "0001"
            && current.to_msb_string() == "0001"
            && values.packed_signal_slots_bound(1U),
        "copy and move construction snapshot a bound slot without stealing it");
    auto changed_snapshot = copied_current;
    changed_snapshot.set(1U, Logic4::one);
    const auto copy_replacement = PackedLogic4::from_msb_string("0110");
    current = copy_replacement;
    require(current.to_msb_string() == "0110"
            && values.current(1U).to_msb_string() == "0110",
        "copy assignment into a bound slot writes through its fixed planes");
    current = PackedLogic4::from_msb_string("0101");
    require(changed_snapshot.to_msb_string() == "0011"
            && moved_current.to_msb_string() == "0001"
            && copy_replacement.to_msb_string() == "0110"
            && current.to_msb_string() == "0101"
            && values.current(1U).to_msb_string() == "0101",
        "an owning copy detaches while assignment into a bound slot writes through");

    bool rejected_mismatched_assignment { };
    try {
        const auto mismatch = PackedLogic4::from_msb_string("101");
        current = mismatch;
    } catch (const std::invalid_argument&) {
        rejected_mismatched_assignment = true;
    }
    require(rejected_mismatched_assignment
            && current.to_msb_string() == "0101"
            && values.current(1U).to_msb_string() == "0101"
            && values.packed_signal_slots_bound(1U),
        "a shape-mismatched copy cannot detach or corrupt a bound slot");

    PackedLogic4 move_snapshot;
    move_snapshot = std::move(current);
    require(move_snapshot.to_msb_string() == "0101"
            && current.to_msb_string() == "0101"
            && values.current(1U).to_msb_string() == "0101",
        "move assignment from a bound slot snapshots and preserves its binding");
    auto replacement = PackedLogic4::from_msb_string("1100");
    current = std::move(replacement);
    require(current.to_msb_string() == "1100"
            && values.current(1U).to_msb_string() == "1100",
        "move assignment into a bound slot copies matching Logic4 words");

    std::swap(current, previous);
    require(current.to_msb_string() == "1010"
            && previous.to_msb_string() == "1100"
            && values.current(1U) == current
            && values.previous(1U) == previous,
        "swapping two bound roles moves values without moving their bindings");

    bool rejected_logic9_promotion { };
    try {
        current.set_logic9(0U, Logic9::u);
    } catch (const std::logic_error&) {
        rejected_logic9_promotion = true;
    }
    require(rejected_logic9_promotion
            && current.to_msb_string() == "1010"
            && values.current(1U) == current,
        "a bound Logic4 slot rejects promotion without changing its plane");

    const auto replacement_previous = PackedLogic4::from_msb_string("0110");
    const auto replacement_stored = PackedLogic4::from_msb_string("1001");
    values.clear_dirty();
    values.mirror_logic4_word(1U,
        replacement_previous.low_word(),
        current.low_word(),
        replacement_stored.low_word());
    require(previous == replacement_previous
            && stored == replacement_stored
            && current == values.current(1U)
            && values.dirty_signals()[0U] != 0U,
        "component mirrors publish through all three bound signal roles");
    const auto replacement_owner = PackedLogic4::from_msb_string("0111");
    values.clear_dirty();
    values.mirror_owner(1U, 0U, replacement_owner);
    require(owner == replacement_owner
            && values.owner_value(1U, 0U) == replacement_owner
            && values.dirty_signals()[0U] != 0U,
        "owner mirrors publish through the bound raw-driver value");

    require(values.unbind_packed_slots() == 4U
            && !values.packed_signal_slots_bound(1U)
            && !values.packed_owner_slot_bound(1U, 0U)
            && current == values.current(1U)
            && previous == values.previous(1U)
            && stored == values.stored(1U)
            && owner == values.owner_value(1U, 0U),
        "unbind materializes all four values before releasing the backing");
    current.set(0U, Logic4::one);
    require(current != values.current(1U),
        "a materialized value no longer aliases released component planes");
}

void check_owner_mirror_rejects_unowned_changes()
{
    std::vector<RegionSignalDescriptor> descriptors(3U, { 129U });
    descriptors[0U].width = 65U;
    descriptors[1U].width = 64U;
    const std::vector<Process> processes {
        partial_writer(0U, 0U, 2U, 0U, 65U),
        partial_writer(1U, 1U, 2U, 65U, 64U),
    };
    const auto graph = build_graph(processes, descriptors);
    const std::array signal_ids { 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };
    const PackedLogic4 initial { 129U, Logic4::z };
    values.seed_signal(2U, initial, initial, initial);
    values.seed_owner(2U, 0U, initial);
    values.seed_owner(2U, 1U, initial);

    auto inconsistent = initial;
    inconsistent.insert_bits(PackedLogic4 { 1U, Logic4::one }, 100U);
    values.mirror_owner(2U, 0U, inconsistent);
    require(!values.valid()
            && values.current(2U) == initial
            && values.previous(2U) == initial
            && values.stored(2U) == initial
            && values.owner_value(2U, 0U) == initial
            && values.owner_value(2U, 1U) == initial,
        "an owner update outside its mask invalidates without partial mirroring");
}

void check_grouped_readiness_masks()
{
    Process first;
    first.id = 0U;
    first.name = "a4_range_reader";
    first.static_sensitivity = {
        { 5U, EdgeKind::any, 0U, 1U },
        { 5U, EdgeKind::any, 1U, 1U },
    };
    first.static_trigger_regions = { { 0U, 2U, UINT64_C(3) } };
    Process second;
    second.id = 1U;
    second.name = "a4_whole_reader";
    second.static_sensitivity = { { 5U, EdgeKind::any } };
    const std::array programs {
        static_cast<const Process*>(&first),
        static_cast<const Process*>(&second),
    };
    const std::array members { 0U, 1U };
    const auto fanout = RegionGroupedFanout::build(programs, members);
    RegionReadyMask readiness { members.size() };
    const auto previous = PackedLogic4::from_msb_string("00");
    const auto current = PackedLogic4::from_msb_string("01");
    fanout.mark_transition(
        5U, previous, current, EdgeKind::any, readiness);
    require(readiness.ready(0U) && readiness.ready(1U)
            && readiness.trigger_mask(0U) == UINT64_C(1)
            && readiness.trigger_mask(1U)
                == Process::full_static_trigger_mask,
        "grouped fanout ORs only the matching range trigger and keeps whole readers");
    const auto other = PackedLogic4::from_msb_string("10");
    fanout.mark_transition(
        5U, current, other, EdgeKind::any, readiness);
    require(readiness.trigger_mask(0U) == UINT64_C(3)
            && readiness.trigger_mask(1U)
                == Process::full_static_trigger_mask,
        "repeated transitions accumulate trigger masks until the region drains");
    readiness.clear(0U);
    require(!readiness.ready(0U) && readiness.ready(1U)
            && readiness.trigger_mask(0U) == 0U,
        "member clearing preserves unrelated ready bits and masks");
    readiness.clear_all();
    require(!readiness.ready(1U) && readiness.trigger_mask(1U) == 0U,
        "region readiness can be reset without changing queue order");

    Process transaction;
    transaction.id = 2U;
    transaction.name = "a4_transaction_reader";
    transaction.static_sensitivity = { { 5U, EdgeKind::transaction } };
    const std::array transaction_programs {
        static_cast<const Process*>(&first),
        static_cast<const Process*>(&second),
        static_cast<const Process*>(&transaction),
    };
    const std::array transaction_members { 0U, 1U, 2U };
    const auto transaction_fanout = RegionGroupedFanout::build(
        transaction_programs, transaction_members);
    RegionReadyMask transaction_readiness { transaction_members.size() };
    transaction_fanout.mark_transition(5U, previous, current,
        EdgeKind::any, transaction_readiness);
    require(!transaction_readiness.ready(2U),
        "transaction sensitivities do not wake on a value transition");
    transaction_fanout.mark_transaction(5U, transaction_readiness);
    require(transaction_readiness.ready(2U)
            && transaction_readiness.trigger_mask(2U)
                == Process::full_static_trigger_mask,
        "transaction fanout is tracked independently from value edges");
}

using A4RoleValues = std::array<PackedLogic4, 4U>;

[[nodiscard]] A4RoleValues a4_ordinary_roles(
    Interpreter& interpreter,
    const SignalId signal,
    const ProcessId owner)
{
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto* const record
        = implementation.driver_values.at(signal).find(owner);
    if (record == nullptr) {
        throw std::runtime_error {
            "wide A4 runtime lost its original process driver"
        };
    }
    return { implementation.get_signal(signal).initial_value,
        implementation.signal_last_values.at(signal),
        implementation.driven_values.at(signal), record->value };
}

void require_a4_roles(const A4RoleValues& roles,
    const PackedLogic4& current,
    const PackedLogic4& previous,
    const PackedLogic4& stored,
    const PackedLogic4& raw,
    const char* message)
{
    require(roles[0U] == current && roles[1U] == previous
            && roles[2U] == stored && roles[3U] == raw,
        message);
}

[[nodiscard]] std::array<std::string, 4U> a4_role_texts(
    const A4RoleValues& roles)
{
    return { roles[0U].to_msb_string(), roles[1U].to_msb_string(),
        roles[2U].to_msb_string(), roles[3U].to_msb_string() };
}

void check_wide_default_admission_and_checked_override()
{
    struct PolicyCase {
        const char* region;
        const char* wide;
        bool bound;
    };
    constexpr std::array policies {
        PolicyCase { nullptr, nullptr, true },
        PolicyCase { nullptr, "1", true },
        PolicyCase { nullptr, "0", false },
        PolicyCase { "0", nullptr, false },
    };
    ScopedEnvironment disjoint_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment profile_disabled { "FSIM_PROFILE_SV_WAVES", nullptr };
    for (const auto& policy : policies) {
        ScopedEnvironment kernel_policy {
            "FSIM_ENABLE_SV_REGION_KERNEL", policy.region };
        ScopedEnvironment wide_policy {
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", policy.wide };
        for (const auto kind : { ValueKind::logic4, ValueKind::logic9 }) {
            WideA4RuntimeCase test { 129U, kind, "default_policy" };
            test.drain();
            require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U) == policy.bound,
                "default wide storage and explicit checked overrides select the expected route");
            const auto initial = a4_ordinary_roles(
                test.interpreter(), test.target(), 0U);
            test.drive(0U);
            require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U) == policy.bound,
                "the selected wide route survives its first scheduled write");
            require_a4_roles(a4_ordinary_roles(
                                  test.interpreter(), test.target(), 0U),
                test.value(0U), initial[0U], test.value(0U), test.value(0U),
                "default and checked wide routes preserve current, LAST, stored, and raw values");
            // Admission is captured at startup, including an explicit decline.
            ScopedEnvironment changed_policy {
                "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT",
                policy.bound ? "0" : "1" };
            test.drive(1U);
            require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U) == policy.bound,
                "environment changes after startup cannot switch wide storage policy");
            require_a4_roles(a4_ordinary_roles(
                                  test.interpreter(), test.target(), 0U),
                test.value(1U), test.value(0U), test.value(1U), test.value(1U),
                "both policies advance LAST once on the second publication");
            const auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(test.interpreter());
            require(implementation.get_signal(test.sink()).initial_value
                    == test.value(1U),
                "both policies publish the exact value to an ordinary downstream reader");
        }
    }
}

void check_default_narrow_a2_admission_and_checked_override()
{
    struct PolicyCase {
        const char* wide;
        bool enabled;
        bool explicitly_versioned;
        bool versioned;
    };
    constexpr std::array policies {
        PolicyCase { nullptr, true, false, true },
        PolicyCase { "0", false, false, false },
        PolicyCase { "1", true, true, true },
    };
    ScopedEnvironment disjoint_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", nullptr };
    ScopedEnvironment profile_disabled {
        "FSIM_PROFILE_SV_WAVES", nullptr };
    for (const auto& policy : policies) {
        ScopedEnvironment wide_policy {
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", policy.wide };
        WideA4RuntimeCase test { 1U, ValueKind::logic4,
            policy.explicitly_versioned ? "narrow_explicit_versioned"
            : policy.enabled ? "narrow_default_a2_versioned"
                             : "narrow_forced_checked" };
        test.drain();
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(
                test.interpreter());
        auto* const state = implementation
            .region_authoritative_state_for_signal(test.target());
        require(implementation.a4_wide_single_owner_commit_enabled
                    == policy.enabled
                && implementation
                    .a4_wide_single_owner_versioned_storage_explicit
                    == policy.explicitly_versioned,
            "startup captures default, forced-checked, and explicit-wide policy independently");
        require(state != nullptr
                && state->values().packed_slots_bound()
                && state->values().requires_prewrite_unbind()
                    == policy.versioned,
            "default A2 narrow admission versions eligible internals while "
            "explicit off retains checked narrow storage");
    }
}

void check_wide_scheduled_single_owner_commit()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };

    constexpr std::array<std::size_t, 4U> widths {
        65U, 129U, 256U, 1024U };
    constexpr std::array<ValueKind, 2U> kinds {
        ValueKind::logic4, ValueKind::logic9 };
    for (const auto width : widths) {
        for (const auto kind : kinds) {
            const auto suffix = std::to_string(width)
                + (kind == ValueKind::logic9 ? "_l9" : "_l4");
            WideA4RuntimeCase test { width, kind, suffix };
            test.drain();
            auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(
                    test.interpreter());
            require(implementation.process_signal_access_is_complete(0U)
                    && !implementation.process_region_kernel_eligible(0U)
                    && !implementation.can_queue_systemverilog_wave(0U),
                "exact-access test executor uses the ordinary scheduler, not a replacement kernel");
            require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U),
                "wide A4 runtime binds the real signal and owner objects");

            const auto initial_roles
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    test.interpreter(), test.target(), 0U);
            const auto initial_role_texts = a4_role_texts(initial_roles);
            const auto expected_initial = kind == ValueKind::logic9
                ? PackedLogic4::from_logic9_msb_string(
                    std::string(width, 'U'))
                : test.value(3U);
            require(initial_roles[0U] == expected_initial,
                "wide A4 starts from the expected target value");
            const auto first_value = test.value(0U);
            test.drive(0U);
            require(test.resumes() == 1U
                    && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U),
                "scheduled whole Active WriteUpdate retains wide A4 authority");
            const auto first_roles
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    test.interpreter(), test.target(), 0U);
            const auto first_role_texts = a4_role_texts(first_roles);
            require_a4_roles(first_roles, first_value, initial_roles[0U],
                first_value, first_value,
                "first scheduled wide commit publishes current, LAST, stored, and raw together");
            require(test.interpreter().driver_value(0U, test.target())
                        == first_value,
                "public raw-driver snapshot follows the wide A4 owner slot");

            const auto second_value = test.value(1U);
            test.drive(1U);
            require(test.resumes() == 2U
                    && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U),
                "repeated scheduled writes remain on the bound wide owner path");
            const auto second_roles
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    test.interpreter(), test.target(), 0U);
            require_a4_roles(second_roles, second_value, first_value,
                second_value, second_value,
                "second scheduled wide commit advances LAST exactly once");
            require(test.interpreter().driver_value(0U, test.target())
                        == second_value
                    && implementation.get_signal(test.sink()).initial_value
                        == second_value,
                "public raw value and ordinary downstream reader see the committed wide value");

            require(initial_roles[0U] == expected_initial
                    && first_roles[0U] == first_value
                    && first_roles[1U] == initial_roles[0U]
                    && a4_role_texts(initial_roles) == initial_role_texts
                    && a4_role_texts(first_roles) == first_role_texts,
                "all four owning role snapshots remain stable across later wide publication");

            const auto forced_value = test.value(2U);
            require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U),
                "wide force witness begins while its owner slots are bound");
            test.interpreter().force_signal(test.target(), forced_value);
            test.drain();
            require(!OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                        test.interpreter(), test.target(), 0U),
                "public wide force demotes versioned owner slots before mutation");
            const auto after_force
                = a4_ordinary_roles(test.interpreter(), test.target(), 0U);
            require_a4_roles(after_force, forced_value, second_value,
                second_value, second_value,
                "wide force preserves stored and original raw owner phases");

            test.interpreter().release_signal(test.target());
            test.drain();
            const auto after_release
                = a4_ordinary_roles(test.interpreter(), test.target(), 0U);
            require_a4_roles(after_release, second_value, forced_value,
                second_value, second_value,
                "wide release restores the latest original raw owner value");

            const auto third_value = test.value(3U);
            test.drive(3U);
            const auto after_demoted_write
                = a4_ordinary_roles(test.interpreter(), test.target(), 0U);
            require_a4_roles(after_demoted_write, third_value, second_value,
                third_value, third_value,
                "ordinary wide writes remain exact after force demotion");
        }
    }
}

void check_wide_boundary_storage_without_cone_program()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    WideA4RuntimeCase test {
        129U, ValueKind::logic9, "boundary_only", true };
    test.drain();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(test.interpreter());
    require(implementation.region_graph.has_value(),
        "storage-only fixture retains the ordinary graph certificate");
    const auto component = implementation.region_authoritative_component_by_signal
        .at(test.target());
    const auto& certificates
        = implementation.region_graph->certificate_inventory().components;
    require(component < certificates.size(),
        "wide boundary storage has a real mapped component");
    const auto& certificate = certificates[component];
    require(certificate.status
                == RegionComponentCertificateStatus::no_internal_state
            && certificate.structural_internal_signal_candidates.empty()
            && std::ranges::find(certificate.boundary_signals, test.target())
                != certificate.boundary_signals.end(),
        "resolved Logic9 storage stays public and does not become a hidden cone");
    require(!implementation.region_activation_programs.at(component)
            && implementation.region_readiness_mask_by_component
                    .at(component).word_count == 0U,
        "storage-only component has no native program or readiness mask");
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                test.interpreter(), test.target(), 0U),
        "storage-only admission binds original current/LAST/stored/owner roles");
    const auto before = OwnedDriverDemotionTestAccess::packed_a4_values(
        test.interpreter(), test.target(), 0U);
    test.drive(0U);
    require(test.resumes() == 1U
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                test.interpreter(), test.target(), 0U),
        "ordinary checked execution preserves the bound wide boundary storage");
    const auto after = OwnedDriverDemotionTestAccess::packed_a4_values(
        test.interpreter(), test.target(), 0U);
    require_a4_roles(after, test.value(0U), before[0U], test.value(0U),
        test.value(0U),
        "storage-only checked commit preserves all exact Logic9 role values");
    require(implementation.get_signal(test.sink()).initial_value == test.value(0U),
        "ordinary downstream publication sees the exact Logic9 boundary value");
}

void check_wide_late_observation_demotes_and_keeps_reference_live()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };

    const std::array<std::pair<std::size_t, ValueKind>, 2U> cases {
        std::pair { 129U, ValueKind::logic4 },
        std::pair { 256U, ValueKind::logic9 },
    };
    for (const auto& [width, kind] : cases) {
        const auto suffix = std::string { "late_" }
            + std::to_string(width);
        WideA4RuntimeCase test { width, kind, suffix };
        test.drain();
        test.drive(0U);
        test.drive(1U);
        require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    test.interpreter(), test.target(), 0U),
            "late wide observation starts after real bound publications");
        const auto second_value = test.value(1U);
        const auto& retained
            = test.interpreter().signal_value(test.target());
        require(retained == second_value
                && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    test.interpreter(), test.target(), 0U),
            "first public wide reference materializes and demotes the bound roles");

        const auto third_value = test.value(2U);
        test.drive(2U);
        require(retained == third_value
                && test.interpreter().stored_signal_value_snapshot(
                       test.target()) == third_value
                && test.interpreter().driver_value(0U, test.target())
                    == third_value,
            "retained wide reference follows later current, stored, and raw writes");
    }
}

void check_wide_trace_declines_before_owner_mutation()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    WideA4RuntimeCase test { 129U, ValueKind::logic4, "trace_guard" };
    test.drain();
    test.drive(0U);
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                test.interpreter(), test.target(), 0U),
        "trace guard begins after a successful wide scheduled commit");

    struct TraceProbe {
        WideA4RuntimeCase* test { };
        std::size_t records { };
        bool saw_demoted_slots { };
    } trace_probe { &test };
    test.interpreter().scheduler().set_trace_hook(
        &trace_probe,
        [](void* context, const SchedulerTraceRecord&) noexcept {
            auto& probe = *static_cast<TraceProbe*>(context);
            ++probe.records;
            if (!OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    probe.test->interpreter(), probe.test->target(), 0U)) {
                probe.saw_demoted_slots = true;
            }
        });
    test.interpreter().deposit_signal(test.source_signal(), test.value(1U));
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                test.interpreter(), test.target(), 0U),
        "changing only the boundary input does not prematurely demote the target");
    test.drain();
    require(trace_probe.records != 0U
            && trace_probe.saw_demoted_slots
            && a4_ordinary_roles(test.interpreter(), test.target(), 0U)[0U]
                == test.value(1U),
        "installed tracing declines wide commit and fallback demotes before raw mutation");
}

void check_interpreter_a4_runtime_route()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    std::size_t late_observer_calls { };
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        const auto source = interpreter.add_signal({ "a4.source",
            PackedLogic4(1U, Logic4::zero) });
        const auto internal = interpreter.add_signal({ "a4.internal",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto output = interpreter.add_signal({ "a4.output",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });

        Process producer;
        producer.id = 0U;
        producer.name = "a4_runtime_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.initialize = false;
        producer.register_count = 1U;
        producer.static_sensitivity = { { source, EdgeKind::any } };
        producer.driver_regions = { { internal, 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, source },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(producer)) == 0U,
            "A4 runtime producer has a stable process identity");

        Process consumer;
        consumer.id = 1U;
        consumer.name = "a4_runtime_consumer";
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.initialize = false;
        consumer.register_count = 1U;
        consumer.static_sensitivity = { { internal, EdgeKind::any } };
        consumer.driver_regions = { { output, 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, internal }, UnaryNot { 0U, 0U },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(consumer)) == 1U,
            "A4 runtime consumer has a stable process identity");

        interpreter.start();
        const auto run_to_quiet = [&] {
            require(interpreter.run().status == RunStatus::completed,
                "A4 runtime route drains its checked scheduler work");
        };
        const auto check_public_state = [&](const Logic4 current,
                                            const Logic4 stored,
                                            const Logic4 owner,
                                            const Logic4 output_value) {
            require(interpreter.signal_value_snapshot(internal)
                        == PackedLogic4(1U, current)
                    && interpreter.stored_signal_value_snapshot(internal)
                        == PackedLogic4(1U, stored)
                    && interpreter.driver_value(0U, internal)
                        == PackedLogic4(1U, owner)
                    && interpreter.signal_value_snapshot(output)
                        == PackedLogic4(1U, output_value),
                "A4 mirrors preserve ordinary current, stored, owner, and consumer values");
        };

        interpreter.deposit_signal(source,
            PackedLogic4(1U, Logic4::one));
        run_to_quiet();
        check_public_state(Logic4::one, Logic4::one,
            Logic4::one, Logic4::zero);

        interpreter.force_signal(internal,
            PackedLogic4(1U, Logic4::zero));
        run_to_quiet();
        check_public_state(Logic4::zero, Logic4::one,
            Logic4::one, Logic4::one);

        interpreter.release_signal(internal);
        run_to_quiet();
        check_public_state(Logic4::one, Logic4::one,
            Logic4::one, Logic4::zero);

        interpreter.deposit_signal(source,
            PackedLogic4(1U, Logic4::zero));
        run_to_quiet();
        check_public_state(Logic4::zero, Logic4::zero,
            Logic4::zero, Logic4::one);

        interpreter.set_signal_change_hook(
            [&](const SignalId signal, const PackedLogic4&,
                const SimulationTick) {
                late_observer_calls += signal == internal ? 1U : 0U;
            });
        interpreter.prepare_signal_observation(internal);
        interpreter.deposit_signal(source,
            PackedLogic4(1U, Logic4::one));
        run_to_quiet();
        require(late_observer_calls != 0U,
            "late signal observation uses the ordinary interpreter callback path");
        check_public_state(Logic4::one, Logic4::one,
            Logic4::one, Logic4::zero);

        interpreter.set_signal_change_hook({ });
        interpreter.deposit_signal(source,
            PackedLogic4(1U, Logic4::zero));
        run_to_quiet();
        check_public_state(Logic4::zero, Logic4::zero,
            Logic4::zero, Logic4::one);
    }

    const auto output = captured.str();
    const auto summary_start = output.find("fsim-profile: a4-state-summary ");
    require(summary_start != std::string::npos,
        "runtime A4 profiling emits the state-route counters");
    const auto summary_end = output.find('\n', summary_start);
    const auto summary = output.substr(summary_start,
        summary_end == std::string::npos
            ? std::string::npos : summary_end - summary_start);
    const auto metric = [&](const std::string_view name) {
        const auto key = std::string { name } + '=';
        const auto start = summary.find(key);
        require(start != std::string::npos,
            "A4 state summary contains each required route counter");
        const auto value_start = start + key.size();
        const auto end = summary.find(' ', value_start);
        return static_cast<std::uint64_t>(std::stoull(summary.substr(
            value_start, end == std::string::npos
                ? std::string::npos : end - value_start)));
    };
    require(metric("seeded_components") >= 2U
            && metric("seeded_signals") >= 2U
            && metric("seeded_owners") >= 2U
            && metric("stored_mirrors") != 0U
            && metric("visible_mirrors") != 0U
            && metric("owner_mirrors") != 0U
            && metric("value_marks") != 0U
            && metric("transaction_marks") != 0U
            && metric("ready_consumptions") != 0U,
        "the interpreter seeds and consumes A4 state across the checked runtime routes");
    const auto wave_summary_start = output.find(
        "fsim-profile: sv-ordered-wave-summary ");
    require(wave_summary_start != std::string::npos,
        "late observation emits region recertification counters");
    const auto wave_summary_end = output.find('\n', wave_summary_start);
    const auto wave_summary = output.substr(wave_summary_start,
        wave_summary_end == std::string::npos
            ? std::string::npos : wave_summary_end - wave_summary_start);
    require(wave_summary.find("region_recert_successes=")
                != std::string::npos,
        "recertification summary contains a success count");
    const auto recert_key = std::string { "region_recert_successes=" };
    const auto recert_start = wave_summary.find(recert_key) + recert_key.size();
    const auto recert_end = wave_summary.find(' ', recert_start);
    require(std::stoull(wave_summary.substr(recert_start,
                recert_end == std::string::npos
                    ? std::string::npos : recert_end - recert_start)) != 0U,
        "the late observer is followed by a successful graph recertification");
}

void check_interpreter_logic9_a4_runtime_route()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    std::vector<std::string> current_events;
    std::vector<std::string> stored_events;
    std::vector<std::string> raw_driver_events;
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        const auto initial_source
            = PackedLogic4::from_logic9_msb_string("UX01ZWLH-");
        const auto initial_internal
            = PackedLogic4::from_logic9_msb_string("ZZZZZZZZZ");
        // Registering a Logic9 driver seeds LAST from its undriven U value.
        const auto initial_last_driver
            = PackedLogic4::from_logic9_msb_string("UUUUUUUUU");
        const auto first_drive
            = PackedLogic4::from_logic9_msb_string("UX01ZWLH-");
        const auto forced
            = PackedLogic4::from_logic9_msb_string("Z1UHXWL0-");
        const auto second_drive
            = PackedLogic4::from_logic9_msb_string("H0WLUX1Z-");
        const auto source = interpreter.add_signal({
            "a4.logic9_source", initial_source,
            ResolutionKind::none, ValueKind::logic9 });
        const auto internal = interpreter.add_signal({
            "a4.logic9_internal", initial_internal,
            ResolutionKind::sv_wire, ValueKind::logic9 });

        Process producer;
        producer.id = 0U;
        producer.name = "a4_logic9_runtime_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.register_count = 1U;
        producer.register_value_kinds = { ValueKind::logic9 };
        producer.static_sensitivity = { { source, EdgeKind::any } };
        producer.driver_regions = { { internal, 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, source },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(producer)) == 0U,
            "A4 Logic9 producer has a stable process identity");

        interpreter.start();
        const auto run_to_quiet = [&] {
            require(interpreter.run().status == RunStatus::completed,
                "A4 Logic9 route drains checked scheduler work");
        };
        run_to_quiet();
        require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, internal, 0U),
            "runtime Logic9 signal and raw slots bind before public observation");
        const auto authoritative_before_observation
            = OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, internal, 0U);
        require(std::ranges::all_of(authoritative_before_observation,
                    [](const PackedLogic4& value) {
                        return value.is_logic9();
                    }),
            "runtime authoritative A4 planes retain nine-state storage");
        require(authoritative_before_observation[0U] == first_drive
                && authoritative_before_observation[1U] == initial_last_driver
                && authoritative_before_observation[2U] == first_drive
                && authoritative_before_observation[3U] == first_drive,
            "runtime Logic9 authoritative current, LAST, stored, and raw planes agree");

        const auto& retained_current = interpreter.signal_value(internal);
        require(!OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, internal, 0U),
            "the first late Logic9 reference materializes all bound roles");
        const auto& retained_stored
            = interpreter.stored_signal_value(internal);
        require(retained_current == first_drive
                && retained_stored == first_drive,
            "late Logic9 value getters materialize exact valid nine-state values");

        interpreter.set_signal_change_hook(
            [&](const SignalId signal, const PackedLogic4& value,
                const SimulationTick) {
                if (signal != internal) {
                    return;
                }
                current_events.push_back(value.to_msb_string());
                require(interpreter.signal_value_snapshot(signal) == value,
                    "Logic9 change hooks observe the committed current value");
            });
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId signal, const SimulationTick) {
                if (signal == internal) {
                    stored_events.push_back(
                        interpreter.stored_signal_value_snapshot(signal)
                            .to_msb_string());
                }
            });
        interpreter.set_driver_change_hook(
            [&](const ProcessId process, const SignalId signal,
                const SimulationTick) {
                if (process == 0U && signal == internal) {
                    raw_driver_events.push_back(
                        interpreter.driver_value(process, signal)
                            .to_msb_string());
                }
            });

        interpreter.force_signal(internal, forced);
        run_to_quiet();
        require(interpreter.signal_value_snapshot(internal) == forced
                && interpreter.stored_signal_value_snapshot(internal)
                    == first_drive
                && interpreter.driver_value(0U, internal) == first_drive
                && retained_current == forced
                && retained_stored == first_drive,
            "Logic9 force masks current while retaining stored and raw planes");

        interpreter.deposit_signal(source, second_drive);
        run_to_quiet();
        require(interpreter.signal_value_snapshot(internal) == forced
                && interpreter.stored_signal_value_snapshot(internal)
                    == second_drive
                && interpreter.driver_value(0U, internal) == second_drive
                && retained_current == forced
                && retained_stored == second_drive,
            "Logic9 raw driver updates remain visible beneath an active force");

        interpreter.release_signal(internal);
        run_to_quiet();
        require(interpreter.signal_value_snapshot(internal) == second_drive
                && interpreter.stored_signal_value_snapshot(internal)
                    == second_drive
                && interpreter.driver_value(0U, internal) == second_drive
                && retained_current == second_drive
                && retained_stored == second_drive,
            "Logic9 release restores the most recent original driver value");
        require(std::ranges::find(current_events, forced.to_msb_string())
                    != current_events.end()
                && std::ranges::find(current_events, second_drive.to_msb_string())
                    != current_events.end()
                && std::ranges::find(stored_events, second_drive.to_msb_string())
                    != stored_events.end()
                && std::ranges::find(raw_driver_events,
                       second_drive.to_msb_string()) != raw_driver_events.end(),
            "late Logic9 hooks preserve current, stored, and original-driver phases");
    }

    const auto output = captured.str();
    const auto summary_start = output.find("fsim-profile: a4-state-summary ");
    require(summary_start != std::string::npos,
        "Logic9 runtime route emits its A4 state summary");
    const auto summary_end = output.find('\n', summary_start);
    const auto summary = output.substr(summary_start,
        summary_end == std::string::npos
            ? std::string::npos : summary_end - summary_start);
    const auto read_metric = [&summary](const std::string& key) {
        const auto metric_start = summary.find(key);
        if (metric_start == std::string::npos) {
            throw std::runtime_error { "A4 summary omits a slot metric" };
        }
        const auto value_start = metric_start + key.size();
        const auto value_end = summary.find(' ', value_start);
        return std::stoull(summary.substr(value_start,
            value_end == std::string::npos
                ? std::string::npos : value_end - value_start));
    };
    require(read_metric("slot_bindings=") >= 4U
            && read_metric("bound_slots=") == 0U,
        "runtime Logic9 summary retains four bindings then reports demotion");
}

void check_force_demotes_packed_slots_before_publication()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_default {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
    ScopedEnvironment disjoint_owner_default {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", nullptr };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    Interpreter interpreter;
    const auto source = interpreter.add_signal({
        "a4.force_guard_source", PackedLogic4 { 4U, Logic4::one },
        ResolutionKind::none });
    const auto output = interpreter.add_signal({
        "a4.force_guard_output", PackedLogic4 { 4U, Logic4::zero },
        ResolutionKind::sv_wire });
    require(interpreter.add_process(
                whole_writer(0U, source, output)) == 0U,
        "A4 force-guard writer has a stable process identity");
    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "A4 force-guard setup drains the first active publication");
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U),
        "the force witness begins with live packed signal and owner slots");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto* const state
        = implementation.region_authoritative_state_for_signal(output);
    require(state != nullptr
            && state->values().requires_prewrite_unbind(),
        "default narrow A2 admission uses versioned roles before force");

    const auto before = OwnedDriverDemotionTestAccess::packed_a4_values(
        interpreter, output, 0U);
    require(before[0U] == PackedLogic4 { 4U, Logic4::one }
            && before[2U] == PackedLogic4 { 4U, Logic4::one }
            && before[3U] == PackedLogic4 { 4U, Logic4::one },
        "the bound force witness captures current, stored, and raw roles");

    const PackedLogic4 forced { 4U, Logic4::zero };
    interpreter.force_signal(output, forced);
    require(!OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U),
        "a public force unbinds A4 roles before changing current");
    const auto after_force = OwnedDriverDemotionTestAccess::packed_a4_values(
        interpreter, output, 0U);
    require(after_force[0U] == forced
            && after_force[1U] == before[0U]
            && after_force[2U] == before[2U]
            && after_force[3U] == before[3U],
        "force demotion preserves LAST, stored, and original raw ownership");

    interpreter.release_signal(output);
    const auto after_release = OwnedDriverDemotionTestAccess::packed_a4_values(
        interpreter, output, 0U);
    require(after_release[0U] == before[2U]
            && after_release[1U] == forced
            && after_release[2U] == before[2U]
            && after_release[3U] == before[3U],
        "release after force demotion restores the original driver value");

    ScopedEnvironment single_owner_checked {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner_checked {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    Interpreter driver_interpreter;
    const auto driver_source = driver_interpreter.add_signal({
        "a4.driver_guard_source", PackedLogic4 { 4U, Logic4::one },
        ResolutionKind::none });
    const auto driver_output = driver_interpreter.add_signal({
        "a4.driver_guard_output", PackedLogic4 { 4U, Logic4::zero },
        ResolutionKind::sv_wire });
    require(driver_interpreter.add_process(
                whole_writer(0U, driver_source, driver_output)) == 0U,
        "A4 driver-guard writer has a stable process identity");
    driver_interpreter.start();
    require(driver_interpreter.run().status == RunStatus::completed
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                driver_interpreter, driver_output, 0U),
        "the raw-driver witness begins with live packed owner storage");
    auto& driver_impl
        = OwnedDriverDemotionTestAccess::implementation(driver_interpreter);
    const auto* const checked_state
        = driver_impl.region_authoritative_state_for_signal(driver_output);
    require(checked_state != nullptr
            && !checked_state->values().requires_prewrite_unbind(),
        "explicitly disabled A4 owner admission retains the unversioned checked route");
    driver_impl.commit_driver(0U, driver_output,
        PackedLogic4 { 4U, Logic4::zero }, std::nullopt, false);
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                driver_interpreter, driver_output, 0U),
        "ordinary narrow owner writes retain their in-place packed slots");
    const auto after_driver_write
        = OwnedDriverDemotionTestAccess::packed_a4_values(
            driver_interpreter, driver_output, 0U);
    require(after_driver_write[0U] == PackedLogic4 { 4U, Logic4::zero }
            && after_driver_write[1U] == PackedLogic4 { 4U, Logic4::one }
            && after_driver_write[2U] == PackedLogic4 { 4U, Logic4::zero }
            && after_driver_write[3U] == PackedLogic4 { 4U, Logic4::zero },
        "ordinary narrow owner publication keeps current, LAST, stored, and raw coherent");
}

} // namespace

void test_a4_signal_state()
{
    check_driver_layout_and_owner_masks();
    check_prepared_wide_owner_publications();
    check_preallocated_wide_role_copy();
    check_logic9_owner_planes();
    check_logic9_packed_slot_binding();
    check_mixed_shared_plane_rebind_preflight();
    check_reserved_logic9_invalidates_component();
    check_nonallocating_runtime_mirrors();
    check_packed_slot_value_semantics();
    check_owner_mirror_rejects_unowned_changes();
    check_grouped_readiness_masks();
    check_interpreter_a4_runtime_route();
    check_interpreter_logic9_a4_runtime_route();
    check_force_demotes_packed_slots_before_publication();
    check_wide_default_admission_and_checked_override();
    check_default_narrow_a2_admission_and_checked_override();
    check_wide_scheduled_single_owner_commit();
    check_wide_boundary_storage_without_cone_program();
    check_wide_late_observation_demotes_and_keeps_reference_live();
    check_wide_trace_declines_before_owner_mutation();
}

} // namespace fsim::tests::runtime
