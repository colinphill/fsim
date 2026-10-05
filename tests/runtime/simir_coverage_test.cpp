// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_coverage.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename T>
concept HasDestination = requires(T value) { value.destination; };

template <typename T>
concept HasSignal = requires(T value) { value.signal; };

fsim::runtime::CodeCoveragePoint point(
    const std::uint64_t identity,
    const fsim::runtime::CodeCoverageMetric metric,
    const std::uint32_t counter)
{
    return { { 0xCAFEU, identity }, metric, { counter } };
}

fsim::runtime::simir::CodeCoverageHit hit(
    const fsim::runtime::CodeCoveragePoint& owner)
{
    return { owner.id, owner.metric, owner.counter };
}

void require_error(
    const fsim::runtime::simir::CodeCoverageHitError expected,
    const fsim::runtime::simir::CodeCoverageHit& value,
    const fsim::runtime::CodeCoveragePoint& owner,
    const std::size_t counter_count,
    const std::string_view message)
{
    require(
        fsim::runtime::simir::validate_code_coverage_hit(
            value, owner, counter_count).error == expected,
        message);
}

void test_typed_operation_and_exact_owner()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    static_assert(!HasDestination<CodeCoverageHit>);
    static_assert(!HasSignal<CodeCoverageHit>);
    static_assert(std::is_trivially_copyable_v<CodeCoverageHit>);
    require(kSimIRCoverageDiagnostic == "FSIM-COV-009",
        "the SimIR coverage diagnostic must remain stable");

    const auto statement = point(1U, CodeCoverageMetric::Statement, 7U);
    const auto branch = point(2U, CodeCoverageMetric::Branch, 8U);
    require(validate_code_coverage_hit(hit(statement), statement, 9U).ok(),
        "an exact statement owner must validate");
    require(validate_code_coverage_hit(hit(branch), branch, 9U).ok(),
        "an exact branch owner must validate");

    Operation operation { hit(statement) };
    require(operation_holds<CodeCoverageHit>(operation),
        "the typed hit must be an addressable SimIR alternative");
    require(operation_group_index(operation) == 7U,
        "the hit must append to the stable output operation group");
    require(operation_alternative_index(operation) == 10U,
        "the hit must append after existing output alternatives");
}

void test_single_hit_rejections()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    const auto owner = point(1U, CodeCoverageMetric::Statement, 7U);
    auto value = hit(owner);

    value.point = { };
    require_error(CodeCoverageHitError::InvalidPointIdentity, value, owner, 8U,
        "an empty point identity must be rejected");
    value = hit(owner);
    value.metric = CodeCoverageMetric::Line;
    require_error(CodeCoverageHitError::InvalidMetric, value, owner, 8U,
        "a derived line metric must not own a hit operation");
    value = hit(owner);
    value.counter.value = 8U;
    require_error(CodeCoverageHitError::CounterOutOfRange, value, owner, 8U,
        "a counter outside the design table must be rejected");
    value = hit(owner);
    value.point.low ^= 1U;
    require_error(CodeCoverageHitError::PointOwnershipMismatch, value, owner, 8U,
        "a hit must retain its exact point owner");
    value = hit(owner);
    value.metric = CodeCoverageMetric::Branch;
    require_error(CodeCoverageHitError::PointOwnershipMismatch, value, owner, 8U,
        "a hit must retain its exact metric owner");
    value = hit(owner);
    value.counter.value = 6U;
    require_error(CodeCoverageHitError::CounterOwnershipMismatch, value, owner, 8U,
        "a hit must retain its exact counter owner");
}

void test_process_validation_and_resources()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    const std::vector owners {
        point(1U, CodeCoverageMetric::Statement, 7U),
        point(2U, CodeCoverageMetric::Branch, 8U),
    };
    Process process;
    process.id = 42U;
    process.name = "covered";
    process.register_count = 11U;
    process.operations = { Operation { hit(owners[0]) },
        Operation { Halt { } }, Operation { hit(owners[1]) } };
    require(validate_code_coverage_hits(process, owners, 9U).ok(),
        "a process must validate exact hits while ignoring unrelated operations");
    require(process.id == 42U && process.name == "covered"
            && process.register_count == 11U
            && process.operations.size() == 3U,
        "validation must not mutate unrelated process state");

    auto duplicate = process;
    duplicate.operations.push_back(Operation { hit(owners[0]) });
    require(validate_code_coverage_hits(duplicate, owners, 9U).error
            == CodeCoverageHitError::DuplicateHit,
        "one process must not publish duplicate instrumentation for a point");
    require(validate_code_coverage_hits(
                process, owners, 9U, { 1U, 3U }).error
            == CodeCoverageHitError::ResourceLimit,
        "the instance point budget must be bounded");
    require(validate_code_coverage_hits(
                process, owners, 9U, { 2U, 2U }).error
            == CodeCoverageHitError::ResourceLimit,
        "the process operation budget must be bounded");

    auto malformed = owners;
    malformed[1].counter.value = 9U;
    require(validate_code_coverage_hits(process, malformed, 10U).error
            == CodeCoverageHitError::CounterOwnershipMismatch,
        "instance counters must be dense and canonical");
    malformed = owners;
    malformed[1].id = malformed[0].id;
    require(validate_code_coverage_hits(process, malformed, 9U).error
            == CodeCoverageHitError::DuplicatePointOwnership,
        "instance point ownership must be unique");
    malformed = owners;
    malformed[0].id.low = 3U;
    require(validate_code_coverage_hits(process, malformed, 9U).error
            == CodeCoverageHitError::NoncanonicalPointOwnership,
        "instance point ownership must retain canonical identity order");
}

void test_process_layout_copy_on_write_references()
{
    using namespace fsim::runtime::simir;

    Process representative;
    representative.id = 10U;
    representative.name = "layout-representative";
    representative.register_count = 1U;
    representative.register_value_kinds = { ValueKind::logic9 };
    representative.container_register_count = 1U;
    ContainerType element_type;
    element_type.element_width = 8U;
    element_type.fixed = true;
    representative.container_register_types = { element_type };
    representative.static_trigger_regions = {
        Process::StaticTriggerRegion { 0U, 1U, 1U }
    };
    representative.operations = {
        Operation { LoadConstant { 0U,
            fsim::runtime::PackedLogic4::from_msb_string("1") } }
    };

    auto candidate = representative;
    candidate.id = 11U;
    candidate.name = "layout-candidate";
    require(share_process_operations(
                representative, candidate,
                std::span<const Signal> { }, nullptr),
        "matching process layout metadata must share after operation validation");
    require(candidate.register_value_kinds.shares_storage_with(
                representative.register_value_kinds)
            && candidate.container_register_types.shares_storage_with(
                representative.container_register_types)
            && candidate.static_trigger_regions.shares_storage_with(
                representative.static_trigger_regions),
        "the three exact-equal layout vectors must share their backing");

    const auto original_kind
        = process_layout_detail::ProcessLayoutAccess::copy_at(
            representative.register_value_kinds, 0U);
    require(original_kind == ValueKind::logic9,
        "the representative retains its original register kind");

    const Process& const_candidate = candidate;
    const ValueKind& retained_const = const_candidate.register_value_kinds[0U];
    require(retained_const == ValueKind::logic9,
        "public const access returns the stored value");
    require(!candidate.register_value_kinds.shares_storage_with(
                representative.register_value_kinds),
        "first public reference access materializes a private pinned block");

    auto copied_after_const_ref = candidate;
    require(!copied_after_const_ref.register_value_kinds.shares_storage_with(
                candidate.register_value_kinds),
        "copying a pinned vector preserves independent vector-copy semantics");
    candidate.register_value_kinds[0U] = ValueKind::logic4;
    require(retained_const == ValueKind::logic4
            && copied_after_const_ref.register_value_kinds[0U]
                == ValueKind::logic9
            && process_layout_detail::ProcessLayoutAccess::copy_at(
                   representative.register_value_kinds, 0U)
                == ValueKind::logic9,
        "a retained const reference follows its owner while copies stay isolated");

    auto mutable_reference_copy = representative;
    require(mutable_reference_copy.register_value_kinds.shares_storage_with(
                representative.register_value_kinds),
        "an unpinned process copy retains shared immutable backing");
    auto& retained_mutable = mutable_reference_copy.register_value_kinds[0U];
    auto copied_after_mutable_ref = mutable_reference_copy;
    retained_mutable = ValueKind::logic4;
    require(retained_mutable == ValueKind::logic4
            && copied_after_mutable_ref.register_value_kinds[0U]
                == ValueKind::logic9
            && representative.register_value_kinds[0U] == ValueKind::logic9,
        "a retained mutable reference is owner-local and copy-isolated");

    CopyOnWriteVector<int> allocated_empty;
    allocated_empty.reserve(16U);
    const auto empty_copy = allocated_empty;
    require(allocated_empty.empty() && empty_copy.empty()
            && allocated_empty.shares_storage_with(empty_copy),
        "allocated-empty metadata shares safely before a value write");
    auto populated_copy = empty_copy;
    populated_copy.push_back(7);
    require(populated_copy.size() == 1U && populated_copy[0U] == 7
            && allocated_empty.empty(),
        "mutating allocated-empty shared metadata detaches without losing values");

    CopyOnWriteVector<int> reserved_empty;
    reserved_empty.reserve(32U);
    const auto reserved_empty_capacity = reserved_empty.capacity();
    (void)std::as_const(reserved_empty).data();
    require(reserved_empty.empty()
            && reserved_empty.capacity() >= reserved_empty_capacity,
        "const exposure preserves capacity on an allocated-empty vector");

    CopyOnWriteVector<int> reserved_values;
    reserved_values.reserve(32U);
    reserved_values.push_back(4);
    reserved_values.push_back(9);
    const auto reserved_values_capacity = reserved_values.capacity();
    const auto exposed_copy = reserved_values;
    (void)std::as_const(exposed_copy).vector();
    require(exposed_copy.capacity() >= reserved_values_capacity,
        "const vector exposure preserves reserved nonempty capacity");

    auto cleared_copy = reserved_values;
    cleared_copy.clear();
    require(cleared_copy.empty()
            && cleared_copy.capacity() >= reserved_values_capacity
            && reserved_values == std::vector<int> { 4, 9 },
        "shared clear detaches without dropping reserved capacity");

    auto shrunk_copy = reserved_values;
    shrunk_copy.resize(0U);
    require(shrunk_copy.empty()
            && shrunk_copy.capacity() >= reserved_values_capacity
            && reserved_values == std::vector<int> { 4, 9 },
        "shared shrinking resize preserves capacity and the representative");

    auto smaller_reserve_copy = reserved_values;
    smaller_reserve_copy.reserve(1U);
    require(smaller_reserve_copy.capacity() >= reserved_values_capacity
            && reserved_values == std::vector<int> { 4, 9 },
        "a smaller reserve after detach cannot discard prior capacity");

    CopyOnWriteVector<int> assigned_reserved;
    assigned_reserved.reserve(32U);
    auto& assigned_reserved_vector = assigned_reserved.vector();
    assigned_reserved.assign({ 6, 7 });
    require(assigned_reserved_vector.size() == 2U
            && assigned_reserved_vector.capacity() >= 32U,
        "assign through a retained vector facade keeps reserved capacity");
}

struct CopyBomb {
    static inline bool fail_copy { };

    int value { };

    explicit CopyBomb(const int source_value)
        : value(source_value)
    {
    }

    CopyBomb(const CopyBomb& other)
        : value(other.value)
    {
        if (fail_copy) {
            throw std::bad_alloc { };
        }
    }

    CopyBomb(CopyBomb&& other) noexcept
        : value(other.value)
    {
    }

    CopyBomb& operator=(const CopyBomb&) = default;
    CopyBomb& operator=(CopyBomb&&) noexcept = default;
};

void test_process_layout_vector_object_and_move_semantics()
{
    using fsim::runtime::simir::CopyOnWriteVector;

    CopyOnWriteVector<int> assigned { 1, 2 };
    auto& assigned_vector = assigned.vector();
    const std::vector<int>* assigned_vector_address = &assigned_vector;
    const std::vector<int>& retained_const_vector = assigned_vector;
    assigned = std::vector<int> { 3, 4 };
    require(&assigned.vector() == assigned_vector_address
            && retained_const_vector == std::vector<int> { 3, 4 },
        "replacement keeps an escaped vector-object reference attached");

    CopyOnWriteVector<int> copy_source { 8, 9 };
    assigned = copy_source;
    require(&assigned.vector() == assigned_vector_address
            && retained_const_vector == std::vector<int> { 8, 9 },
        "copy assignment updates an escaped vector object in place");

    CopyOnWriteVector<int> move_source { 5, 6 };
    auto& source_vector = move_source.vector();
    const std::vector<int>* source_vector_address = &source_vector;
    int& moved_element = move_source[0U];
    CopyOnWriteVector<int> move_constructed(std::move(move_source));
    require(&move_source.vector() == source_vector_address
            && source_vector.empty()
            && move_constructed.vector() == std::vector<int> { 5, 6 },
        "move construction preserves the source vector object as moved-from");
    move_constructed[0U] = 7;
    require(moved_element == 7,
        "element references follow their values through vector move construction");

    CopyOnWriteVector<int> move_assign_source { 10, 11 };
    auto& move_assign_source_vector = move_assign_source.vector();
    const std::vector<int>* move_assign_source_address
        = &move_assign_source_vector;
    int& move_assign_element = move_assign_source[0U];
    CopyOnWriteVector<int> move_assign_destination { 12 };
    auto& destination_vector = move_assign_destination.vector();
    const std::vector<int>* destination_vector_address = &destination_vector;
    move_assign_destination = std::move(move_assign_source);
    require(&move_assign_source.vector() == move_assign_source_address
            && move_assign_source_vector.empty()
            && &move_assign_destination.vector() == destination_vector_address
            && destination_vector == std::vector<int> { 10, 11 },
        "move assignment preserves both escaped vector-object references");
    move_assign_destination[0U] = 13;
    require(move_assign_element == 13,
        "element references follow moved values into the destination facade");

    CopyOnWriteVector<int> vector_move_destination { 61 };
    (void)vector_move_destination.vector();
    std::vector<int> vector_move_source { 71, 72 };
    int& vector_move_element = vector_move_source[0U];
    vector_move_destination = std::move(vector_move_source);
    require(&vector_move_destination[0U] == &vector_move_element
            && vector_move_element == 71,
        "moving a vector into an exposed facade preserves source element references");

    CopyOnWriteVector<CopyBomb> shared_source;
    shared_source.push_back(CopyBomb { 21 });
    const auto shared_peer = shared_source;
    CopyOnWriteVector<CopyBomb> pinned_destination;
    pinned_destination.push_back(CopyBomb { 22 });
    auto& pinned_destination_vector = pinned_destination.vector();
    const auto* pinned_destination_address = &pinned_destination_vector;

    CopyBomb::fail_copy = true;
    bool move_failed { };
    try {
        pinned_destination = std::move(shared_source);
    } catch (const std::bad_alloc&) {
        move_failed = true;
    }
    CopyBomb::fail_copy = false;
    {
        const auto source_after_failed_move
            = fsim::runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                shared_source);
        const auto peer_after_failed_move
            = fsim::runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                shared_peer);
        require(move_failed
                && &pinned_destination.vector() == pinned_destination_address
                && pinned_destination_vector.size() == 1U
                && pinned_destination_vector[0U].value == 22
                && source_after_failed_move[0U].value == 21
                && peer_after_failed_move[0U].value == 21,
            "a failed prepared move leaves both operands and their copies unchanged");
    }
    pinned_destination = std::move(shared_source);
    require(pinned_destination_vector.size() == 1U
            && pinned_destination_vector[0U].value == 21
            && shared_source.empty()
            && shared_peer[0U].value == 21,
        "a later move retry succeeds without mutating peer copies");

    CopyOnWriteVector<CopyBomb> swap_left;
    swap_left.push_back(CopyBomb { 31 });
    auto& swap_left_vector = swap_left.vector();
    const auto* swap_left_address = &swap_left_vector;
    CopyBomb& swap_left_element = swap_left[0U];
    CopyOnWriteVector<CopyBomb> swap_right;
    swap_right.push_back(CopyBomb { 32 });
    const auto swap_right_peer = swap_right;

    CopyBomb::fail_copy = true;
    bool swap_failed { };
    try {
        swap_left.swap(swap_right);
    } catch (const std::bad_alloc&) {
        swap_failed = true;
    }
    CopyBomb::fail_copy = false;
    const auto swap_right_after_failure
        = fsim::runtime::simir::process_layout_detail::ProcessLayoutAccess::view(swap_right);
    require(swap_failed
            && &swap_left.vector() == swap_left_address
            && swap_left_vector[0U].value == 31
            && swap_right_after_failure[0U].value == 32,
        "a failed mixed-exposure swap leaves both operands unchanged");

    swap_left.swap(swap_right);
    require(&swap_left.vector() == swap_left_address
            && swap_left_vector[0U].value == 32
            && swap_right[0U].value == 31
            && &swap_right[0U] == &swap_left_element
            && swap_right_peer[0U].value == 32,
        "mixed-exposure swap keeps vector objects while element refs follow data");

    CopyOnWriteVector<int> exposed_swap_left { 41, 42 };
    CopyOnWriteVector<int> exposed_swap_right { 51 };
    auto& exposed_swap_left_vector = exposed_swap_left.vector();
    auto& exposed_swap_right_vector = exposed_swap_right.vector();
    const auto* exposed_swap_left_address = &exposed_swap_left_vector;
    const auto* exposed_swap_right_address = &exposed_swap_right_vector;
    exposed_swap_left.swap(exposed_swap_right);
    require(&exposed_swap_left.vector() == exposed_swap_left_address
            && &exposed_swap_right.vector() == exposed_swap_right_address
            && exposed_swap_left_vector == std::vector<int> { 51 }
            && exposed_swap_right_vector == std::vector<int> { 41, 42 },
        "swap preserves both escaped vector-object references");

    CopyOnWriteVector<int> converted { 81, 82 };
    const auto converted_peer = converted;
    const CopyOnWriteVector<int>& const_converted = converted;
    const std::vector<int>& converted_vector = const_converted;
    const std::span<const int> converted_span = const_converted;
    require(converted.references_escaped()
            && !converted.shares_storage_with(converted_peer)
            && converted_span.size() == 2U
            && converted_span[1U] == 82
            && &converted_vector == &const_converted.vector(),
        "public vector and span conversions expose one stable facade");
}

void test_process_layout_concurrent_const_exposure()
{
    using fsim::runtime::simir::CopyOnWriteVector;

    const CopyOnWriteVector<int> values { 3, 5, 8, 13 };
    constexpr std::size_t reader_count = 6U;
    std::array<const std::vector<int>*, reader_count> observed { };
    std::atomic<bool> start { false };
    std::vector<std::thread> readers;
    readers.reserve(reader_count);
    for (std::size_t index = 0U; index < reader_count; ++index) {
        readers.emplace_back([&, index] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const auto& exposed = values.vector();
            require(exposed == std::vector<int> { 3, 5, 8, 13 },
                "concurrent first exposure must preserve exact values");
            observed[index] = &exposed;
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }
    for (const auto* exposed : observed) {
        require(exposed == observed.front(),
            "concurrent first exposure must publish one stable facade");
    }
}

void test_compact_instance_counter_override()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    const auto representative_owner
        = point(1U, CodeCoverageMetric::Statement, 7U);
    const auto candidate_owner
        = point(1U, CodeCoverageMetric::Statement, 23U);
    Process representative;
    representative.operations = { Operation { hit(representative_owner) } };
    Process candidate;
    candidate.operations = { Operation { hit(candidate_owner) } };

    require(process_operations_shareable(representative)
            && process_operations_shareable(candidate),
        "coverage-only programs must remain structurally shareable");
    require(share_process_operations(
                representative, candidate,
                std::span<const Signal> { }, nullptr),
        "equivalent point programs must share across instances");
    require(candidate.operations.shares_body_with(representative.operations),
        "counter differences must not copy the immutable operation body");
    const auto expanded = candidate.operations.expanded(0U);
    require(operation_get<CodeCoverageHit>(expanded).counter
            == candidate_owner.counter,
        "artifact/debug expansion must restore the instance counter");
    require(validate_code_coverage_hits(
                candidate,
                std::span<const CodeCoveragePoint> { &candidate_owner, 1U },
                24U).ok(),
        "validation must observe the compact instance counter override");

    require(share_process_operations(
                representative, candidate,
                std::span<const Signal> { }, nullptr),
        "re-sharing an already shared instance must remain valid");
    require(operation_get<CodeCoverageHit>(
                candidate.operations.expanded(0U)).counter
            == candidate_owner.counter,
        "re-sharing must retain the existing effective instance counter");

    const auto replacement_owner
        = point(1U, CodeCoverageMetric::Statement, 29U);
    candidate.operations.replace(0U, Operation { hit(replacement_owner) });
    require(operation_get<CodeCoverageHit>(
                candidate.operations.expanded(0U)).counter
            == replacement_owner.counter,
        "a full operation replacement must supersede a compact counter override");

    candidate.operations.push_back(Operation { Halt { } });
    require(!candidate.operations.shares_body_with(representative.operations),
        "mutation must materialize an instance-owned operation body");
    require(operation_get<CodeCoverageHit>(candidate.operations[0U]).counter
            == replacement_owner.counter,
        "copy-on-write materialization must preserve the instance counter");
}

void test_scheduling_domain_operation_sharing()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const std::vector signals {
        Signal { "top.first", PackedLogic4 { 1U, Logic4::zero } },
        Signal { "top.second", PackedLogic4 { 1U, Logic4::zero } },
    };
    Process representative;
    representative.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    representative.operations = {
        Operation { WriteUpdate {
            0U, 0U, SignalUpdateDomain::systemverilog_active } }
    };
    Process candidate;
    candidate.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    candidate.operations = {
        Operation { WriteUpdate {
            1U, 0U, SignalUpdateDomain::systemverilog_active } }
    };

    require(share_process_operations(
                representative, candidate, signals, nullptr),
        "same-domain SystemVerilog operations may share their template");
    require(operation_get<WriteUpdate>(candidate.operations.expanded(0U))
                .domain == SignalUpdateDomain::systemverilog_active,
        "shared operation expansion must retain its scheduling domain");

    Process different_domain;
    different_domain.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    different_domain.operations = {
        Operation { WriteUpdate {
            1U, 0U, SignalUpdateDomain::systemverilog_nba } }
    };
    require(!share_process_operations(
                representative, different_domain, signals, nullptr),
        "Active and NBA writes must not share an operation template");

    Process generic_domain;
    generic_domain.operations = {
        Operation { WriteUpdate {
            1U, 0U, SignalUpdateDomain::systemverilog_active } }
    };
    require(!share_process_operations(
                representative, generic_domain, signals, nullptr),
        "generic and SystemVerilog process domains must remain separate");
}

void test_saturating_counter_store()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    require(kCodeCoverageCounterDiagnostic == "FSIM-COV-010",
        "the interpreter counter diagnostic must remain stable");
    CodeCoverageCounters counters;
    require(counters.record({ 0U })
            == CodeCoverageCounterUpdate::Unavailable,
        "an unconfigured counter store must reject execution");
    counters.reset({ std::numeric_limits<std::uint64_t>::max() - 1U });
    require(counters.record({ 0U })
            == CodeCoverageCounterUpdate::Incremented,
        "the last representable increment must succeed");
    require(counters.record({ 0U })
            == CodeCoverageCounterUpdate::FirstOverflow,
        "the first increment beyond uint64 must report overflow");
    require(counters.record({ 0U })
            == CodeCoverageCounterUpdate::Saturated,
        "later overflow attempts must remain saturated without re-reporting");
    require(counters.values()[0] == std::numeric_limits<std::uint64_t>::max()
            && counters.overflowed({ 0U })
            && counters.overflow_count() == 1U,
        "overflow must be sticky and must never wrap the counter");
    require(counters.record({ 1U })
            == CodeCoverageCounterUpdate::OutOfRange,
        "counter execution must remain inside the configured dense table");
    try {
        counters.reset(std::vector<std::uint64_t>(2U), 1U);
        require(false, "an excessive counter table was accepted");
    } catch (const std::length_error&) {
    }
    require(counters.values().size() == 1U
            && counters.values()[0]
                == std::numeric_limits<std::uint64_t>::max()
            && counters.overflow_count() == 1U,
        "a rejected counter-table replacement must preserve prior state");
}

void test_interpreter_counter_execution()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    const auto first = point(1U, CodeCoverageMetric::Statement, 0U);
    const auto second = point(2U, CodeCoverageMetric::Branch, 1U);
    Interpreter interpreter;
    interpreter.set_code_coverage_counters({ 4U, 9U });
    Process process;
    process.id = 0U;
    process.name = "coverage_hits";
    process.operations = { Operation { hit(first) },
        Operation { hit(second) }, Operation { Halt { } } };
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(result.status == RunStatus::completed,
        "the direct interpreter coverage process must complete");
    const auto values = interpreter.code_coverage_counters();
    require(values.size() == 2U && values[0] == 5U && values[1] == 10U,
        "each executed hit must increment its exact counter once");

    Interpreter overflow;
    overflow.set_code_coverage_counters(
        { std::numeric_limits<std::uint64_t>::max() });
    std::vector<CodeCoverageCounterId> reports;
    overflow.set_code_coverage_overflow_hook(
        [&](const CodeCoverageCounterId counter) {
            reports.push_back(counter);
        });
    Process overflowing;
    overflowing.id = 0U;
    overflowing.name = "coverage_overflow";
    overflowing.operations = {
        Operation { hit(first) }, Operation { Halt { } }
    };
    (void)overflow.add_process(std::move(overflowing));
    require(overflow.run().status == RunStatus::completed
            && reports == std::vector<CodeCoverageCounterId> { { 0U } }
            && overflow.code_coverage_counter_overflowed({ 0U })
            && overflow.code_coverage_overflow_count() == 1U
            && overflow.code_coverage_counters()[0]
                == std::numeric_limits<std::uint64_t>::max(),
        "interpreter overflow must saturate and report the counter once");
}

void test_interpreter_counter_failures_and_override()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    const auto owner = point(1U, CodeCoverageMetric::Statement, 0U);
    const auto expect_failure = [&](const bool configure,
                                    const CodeCoverageCounterId counter,
                                    const std::string_view expected) {
        Interpreter interpreter;
        if (configure) {
            interpreter.set_code_coverage_counters({ 0U });
        }
        Process process;
        process.id = 0U;
        process.name = "invalid_coverage_counter";
        auto operation = hit(owner);
        operation.counter = counter;
        process.operations = {
            Operation { operation }, Operation { Halt { } }
        };
        (void)interpreter.add_process(std::move(process));
        try {
            (void)interpreter.run();
            require(false, "invalid interpreter coverage execution succeeded");
        } catch (const InterpreterError& error) {
            require(std::string_view { error.what() }.find(expected)
                    != std::string_view::npos,
                "interpreter coverage failure diagnostic mismatch");
        }
    };
    expect_failure(false, { 0U }, "service is unavailable");
    expect_failure(true, { 1U }, "counter is out of range");

    const auto representative_owner
        = point(1U, CodeCoverageMetric::Statement, 0U);
    const auto candidate_owner
        = point(1U, CodeCoverageMetric::Statement, 1U);
    const std::vector signals {
        Signal { "top.coverage_trigger",
            PackedLogic4::from_msb_string("0") }
    };
    Process representative;
    representative.static_sensitivity = { { 0U } };
    representative.operations = {
        Operation { hit(representative_owner) },
        Operation { WaitSensitivity { } }
    };
    Process candidate;
    candidate.id = 0U;
    candidate.name = "shared_coverage_counter";
    candidate.static_sensitivity = { { 0U } };
    candidate.operations = {
        Operation { hit(candidate_owner) },
        Operation { WaitSensitivity { } }
    };
    require(share_process_operations(
                representative, candidate,
                signals, nullptr),
        "the execution fixture must retain a compact counter override");
    Interpreter shared;
    shared.set_code_coverage_counters({ 0U, 0U });
    (void)shared.add_signal(signals[0]);
    (void)shared.add_process(std::move(candidate));
    require(shared.run().status == RunStatus::completed
            && shared.code_coverage_counters()[0] == 0U
            && shared.code_coverage_counters()[1] == 1U,
        "the direct interpreter must execute the effective instance counter");
}

} // namespace

int main()
{
    test_typed_operation_and_exact_owner();
    test_single_hit_rejections();
    test_process_validation_and_resources();
    test_process_layout_copy_on_write_references();
    test_process_layout_vector_object_and_move_semantics();
    test_process_layout_concurrent_const_exposure();
    test_compact_instance_counter_override();
    test_scheduling_domain_operation_sharing();
    test_saturating_counter_store();
    test_interpreter_counter_execution();
    test_interpreter_counter_failures_and_override();
    return 0;
}
