// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void check_ranges(const PackedLogic4& before, const PackedLogic4& after)
{
    constexpr std::array<std::uint32_t, 13> boundaries {
        0U, 1U, 2U, 31U, 32U, 63U, 64U, 65U,
        127U, 128U, 255U, 511U, 1023U
    };
    for (const auto offset : boundaries) {
        if (offset >= before.width())
            continue;
        for (const auto candidate_width : boundaries) {
            const auto width = candidate_width + 1U;
            if (width > before.width() - offset)
                continue;
            bool changed { };
            for (std::size_t bit = offset; bit < offset + width; ++bit)
                changed |= before.get_logic9(bit) != after.get_logic9(bit);
            require(sensitivity_range_changed(before, after, offset, width)
                    == changed,
                "range filtering must agree with exact per-element values");
        }
    }
}

void test_value_ranges()
{
    for (const auto width : { 1U, 65U, 129U, 256U, 1024U }) {
        PackedLogic4 before { width, Logic4::zero };
        for (const auto position : { 0U, width / 2U, width - 1U }) {
            for (const auto state : { Logic4::zero, Logic4::one,
                     Logic4::x, Logic4::z }) {
                auto after = before;
                after.set(position, state);
                check_ranges(before, after);
            }
        }
        before.fill(Logic9::zero);
        for (const auto position : { 0U, width / 2U, width - 1U }) {
            for (unsigned state = 0; state < 9U; ++state) {
                auto after = before;
                after.set_logic9(position, static_cast<Logic9>(state));
                check_ranges(before, after);
            }
        }
        PackedLogic4 four_state { width, Logic4::zero };
        check_ranges(before, four_state);
        four_state.set(width - 1U, Logic4::x);
        check_ranges(before, four_state);
        require(sensitivity_range_changed(before, four_state, 0U, 0U),
            "whole dependencies must never suppress fanout");
        require(sensitivity_range_changed(before, four_state, width, 1U),
            "unknown bounds must conservatively retain fanout");
        require(sensitivity_range_changed(before, four_state, 0U,
                    std::numeric_limits<std::uint32_t>::max()),
            "overflowing bounds must conservatively retain fanout");
    }
}

void test_normalization()
{
    std::vector<Sensitivity> ranges {
        { 7U, EdgeKind::any, 65U, 64U },
        { 2U, EdgeKind::any, 32U, 4U },
        { 7U, EdgeKind::any, 0U, 64U },
        { 7U, EdgeKind::any, 63U, 2U },
        { 2U, EdgeKind::any, 0U, 0U },
        { 7U, EdgeKind::any, 256U, 8U },
        { 7U, EdgeKind::any, 257U, 2U },
        { 9U, EdgeKind::transaction, 2U, 3U },
    };
    normalize_sensitivities(ranges);
    const std::vector<Sensitivity> expected {
        { 2U, EdgeKind::any, 0U, 0U },
        { 7U, EdgeKind::any, 0U, 129U },
        { 7U, EdgeKind::any, 256U, 8U },
        { 9U, EdgeKind::transaction, 0U, 0U },
    };
    require(ranges == expected,
        "ranges must merge overlaps and adjacency without joining disjoint reads");
    normalize_sensitivities(ranges);
    require(ranges == expected, "normalization must be idempotent");
    std::vector<Sensitivity> overflow {
        { 0U, EdgeKind::any, 0U,
            std::numeric_limits<std::uint32_t>::max() },
        { 0U, EdgeKind::any,
            std::numeric_limits<std::uint32_t>::max(), 1U },
    };
    normalize_sensitivities(overflow);
    require(overflow == std::vector<Sensitivity> { { 0U, EdgeKind::any } },
        "an unrepresentable union must conservatively cover the whole signal");
}

void test_filtered_fanout()
{
    for (const auto width : { 64U, 65U, 129U, 256U, 1024U }) {
        Interpreter interpreter;
        const auto aggregate = interpreter.add_signal(
            { "ranges.aggregate", PackedLogic4 { width, Logic4::zero } });
        std::array<SignalId, 5> counts;
        const std::array<std::uint32_t, 5> offsets {
            0U, width / 2U, width - 1U, 0U, 0U
        };
        for (ProcessId id = 0; id < counts.size(); ++id) {
            counts[id] = interpreter.add_signal(
                { "ranges.count" + std::to_string(id),
                    PackedLogic4 { 8U, Logic4::zero } });
            Process reader;
            reader.id = id;
            reader.name = "range_reader" + std::to_string(id);
            reader.register_count = 3U;
            reader.static_sensitivity = {
                { aggregate, EdgeKind::any, offsets[id], id == 3U ? 0U : 1U }
            };
            reader.operations = {
                WaitSensitivity { },
                ReadSignal { 0U, counts[id] },
                LoadConstant { 1U, PackedLogic4::from_aval_bval(8U, 1U, 0U) },
                Binary { BinaryOperator::add_unsigned, 2U, 0U, 1U },
                WriteBlocking { counts[id], 2U },
                Jump { 0U },
            };
            static_cast<void>(interpreter.add_process(std::move(reader)));
        }
        Process driver;
        driver.id = static_cast<ProcessId>(counts.size());
        driver.name = "range_driver";
        driver.register_count = 1U;
        PackedLogic4 value { width, Logic4::zero };
        for (const auto [bit, state] : {
                 std::pair { 0U, Logic4::one },
                 std::pair { width / 2U, Logic4::one },
                 std::pair { width - 1U, Logic4::x },
                 std::pair { 0U, Logic4::z } }) {
            value.set(bit, state);
            driver.operations.emplace_back(WaitFor { 1U });
            driver.operations.emplace_back(LoadConstant { 0U, value });
            driver.operations.emplace_back(WriteBlocking { aggregate, 0U });
        }
        driver.operations.emplace_back(Halt { });
        static_cast<void>(interpreter.add_process(std::move(driver)));
        require(interpreter.run().status == RunStatus::completed,
            "range fanout fixture must settle");
        const std::array<std::uint64_t, 5> expected { 2U, 1U, 1U, 4U, 2U };
        for (std::size_t index = 0; index < counts.size(); ++index) {
            require(interpreter.signal_value(counts[index]).known_unsigned_value()
                    == expected[index],
                "range readers and same-range cohorts must ignore unrelated changes");
        }
    }
}

} // namespace

void test_sensitivity_ranges()
{
    test_value_ranges();
    test_normalization();
    test_filtered_fanout();
}

} // namespace fsim::tests::runtime
