// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_coverage.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
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
    test_compact_instance_counter_override();
    test_saturating_counter_store();
    test_interpreter_counter_execution();
    test_interpreter_counter_failures_and_override();
    return 0;
}
