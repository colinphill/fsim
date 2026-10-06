// SPDX-License-Identifier: Apache-2.0
//
// Differential fuzz of the static kernel's narrow four-state word operations
// against the reference PackedLogic4 value functions.
#include "../../src/runtime/simir_internal.hpp"
#include "../../src/runtime/simir_kernel_word_ops.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
namespace kw = fsim::runtime::simir::kernel_word;

std::mt19937_64 random_engine { 0x5eedULL };
std::size_t checks = 0U;

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "kernel word op mismatch: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::uint32_t random_width(const std::uint32_t limit = 64U)
{
    const auto choice = random_engine() % 4U;
    if (choice == 0U) {
        return 1U + static_cast<std::uint32_t>(random_engine() % 3U);
    }
    if (choice == 1U) {
        return limit;
    }
    return 1U + static_cast<std::uint32_t>(random_engine() % limit);
}

[[nodiscard]] kw::Word random_word(const std::uint32_t width)
{
    const auto m = kw::mask(width);
    const auto density = random_engine() % 4U;
    std::uint64_t unknown = 0U;
    if (density == 1U) {
        unknown = random_engine() & random_engine() & random_engine();
    } else if (density == 2U) {
        unknown = random_engine();
    } else if (density == 3U) {
        unknown = std::uint64_t { 1 } << (random_engine() % width);
    }
    auto a = random_engine();
    if (random_engine() % 8U == 0U) {
        a = random_engine() % 5U;
    }
    return { a & m, unknown & m };
}

[[nodiscard]] PackedLogic4 packed(const kw::Word value,
    const std::uint32_t width)
{
    return PackedLogic4::from_aval_bval(width, value.a, value.b);
}

[[nodiscard]] kw::Word word(const PackedLogic4& value)
{
    const auto a = value.aval_words();
    const auto b = value.bval_words();
    return { a.empty() ? 0U : a[0], b.empty() ? 0U : b[0] };
}

void expect(const kw::Word actual, const PackedLogic4& expected,
    const std::string& what)
{
    ++checks;
    if (expected.width() > 64U || actual != word(expected)) {
        fail(what + ": expected " + expected.to_msb_string() + " got a="
            + std::to_string(actual.a) + " b=" + std::to_string(actual.b));
    }
}

void binary_ops()
{
    for (int raw = 0; raw <= static_cast<int>(BinaryOperator::greater_equal_signed);
         ++raw) {
        const auto operation = static_cast<BinaryOperator>(raw);
        for (int trial = 0; trial < 4000; ++trial) {
            const auto width = random_width();
            auto lhs = random_word(width);
            auto rhs = random_word(width);
            if (trial % 3 == 0) {
                rhs = lhs;
            }
            const auto expected = binary_value(operation, packed(lhs, width),
                packed(rhs, width));
            expect(kw::binary(operation, lhs, rhs, width), expected,
                "binary " + std::to_string(raw) + " width "
                    + std::to_string(width));
        }
    }
}

/// Logic9 results over {0, 1, X, Z} as Logic4 words; other codes fail.
[[nodiscard]] PackedLogic4 as_logic4(const PackedLogic4& value,
    const std::string& what)
{
    if (!value.is_logic9()) {
        return value;
    }
    auto result = PackedLogic4(value.width(), Logic4::zero);
    for (std::size_t index = 0; index < value.width(); ++index) {
        switch (value.get_logic9(index)) {
        case Logic9::zero:
            break;
        case Logic9::one:
            result.set(index, Logic4::one);
            break;
        case Logic9::x:
            result.set(index, Logic4::x);
            break;
        case Logic9::z:
            result.set(index, Logic4::z);
            break;
        default:
            fail(what + ": Logic9 result leaves {0, 1, X, Z}");
        }
    }
    return result;
}

void logic9_equivalence()
{
    // VHDL delta-mode compiled code keeps Logic9 values over {0, 1, X, Z}
    // as Logic4 words: the reference result for Logic9 operands must equal
    // the word operation, whichever operands carry the Logic9 kind.
    for (int raw = 0;
         raw <= static_cast<int>(BinaryOperator::vhdl_match_equal); ++raw) {
        const auto operation = static_cast<BinaryOperator>(raw);
        for (int trial = 0; trial < 3000; ++trial) {
            const auto width = random_width();
            auto lhs = random_word(width);
            auto rhs = random_word(width);
            if (trial % 3 == 0) {
                rhs = lhs;
            }
            const auto left = trial % 2 == 0
                ? packed(lhs, width).promoted_to_logic9() : packed(lhs, width);
            const auto right = trial % 4 < 2
                ? packed(rhs, width).promoted_to_logic9() : packed(rhs, width);
            const auto what = "logic9 binary " + std::to_string(raw);
            expect(kw::binary(operation, lhs, rhs, width),
                as_logic4(binary_value(operation, left, right), what), what);
        }
    }
    for (int trial = 0; trial < 5000; ++trial) {
        const auto width = random_width();
        const auto value = random_word(width);
        const auto reference = packed(value, width).promoted_to_logic9();
        expect(kw::unary_not(value, width),
            as_logic4(unary_not(reference), "logic9 not"), "logic9 not");
        for (int raw = 0; raw <= static_cast<int>(
                 ReductionOperator::one_hot_or_zero); ++raw) {
            const auto operation = static_cast<ReductionOperator>(raw);
            expect(kw::reduce(operation, value, width),
                as_logic4(reduce_value(operation, reference), "logic9 reduce"),
                "logic9 reduce " + std::to_string(raw));
        }
    }
}

void logic9_conversions()
{
    for (int trial = 0; trial < 20000; ++trial) {
        const auto width = random_width();
        auto value = PackedLogic4(width, Logic4::zero).promoted_to_logic9();
        bool representable = true;
        for (std::uint32_t index = 0; index < width; ++index) {
            // Mostly 0/1/X/Z, sometimes any code.
            const auto code = trial % 4 == 0 ? std::rand() % 9 : 1 + std::rand() % 4;
            value.set_logic9(index, static_cast<Logic9>(code));
            representable = representable && code >= 1 && code <= 4;
        }
        const auto plane = [&](const std::size_t index) {
            return value.logic9_plane_words(index)[0];
        };
        const auto exact = kw::logic9_word(plane(0U), plane(1U), plane(2U),
            plane(3U), width);
        ++checks;
        if (exact.has_value() != representable) {
            fail("logic9_word representability");
        }
        if (exact) {
            expect(*exact, as_logic4(value, "logic9_word"), "logic9_word");
            const auto planes = kw::logic9_planes(*exact, width);
            const std::uint64_t words[4] = { planes.p0, planes.p1, planes.p2,
                planes.p3 };
            const auto rebuilt = PackedLogic4::from_logic9_word_planes(width,
                std::span { &words[0], 1U }, std::span { &words[1], 1U },
                std::span { &words[2], 1U }, std::span { &words[3], 1U });
            ++checks;
            if (!(rebuilt == value)) {
                fail("logic9_planes round trip");
            }
        }
        expect(kw::logic9_coerced(plane(0U), plane(1U), plane(2U), plane(3U),
                   width),
            collapse_to_logic4(value),
            "logic9_coerced");
    }
}

void unary_ops()
{
    for (int trial = 0; trial < 20000; ++trial) {
        const auto width = random_width();
        const auto value = random_word(width);
        const auto reference = packed(value, width);
        expect(kw::unary_not(value, width), unary_not(reference), "unary_not");
        expect(kw::logical_not(value), logical_not(reference), "logical_not");
        expect(kw::to_two_state(value),
            [&] {
                auto converted = PackedLogic4(width, Logic4::zero);
                for (std::uint32_t index = 0; index < width; ++index) {
                    if (reference.get(index) == Logic4::one) {
                        converted.set(index, Logic4::one);
                    }
                }
                return converted;
            }(),
            "to_two_state");
        for (int raw = 0; raw <= static_cast<int>(
                 ReductionOperator::one_hot_or_zero); ++raw) {
            const auto operation = static_cast<ReductionOperator>(raw);
            expect(kw::reduce(operation, value, width),
                reduce_value(operation, reference),
                "reduce " + std::to_string(raw));
        }
        const auto other_width = random_width();
        const auto other = random_word(other_width);
        for (const auto operation : { LogicalBinaryOperator::logical_and,
                 LogicalBinaryOperator::logical_or }) {
            expect(kw::logical_binary(operation, value, other),
                logical_binary(operation, reference, packed(other, other_width)),
                "logical_binary");
        }
    }
}

void shift_ops()
{
    for (int raw = 0; raw <= static_cast<int>(ShiftOperator::rotate_right);
         ++raw) {
        const auto operation = static_cast<ShiftOperator>(raw);
        for (int trial = 0; trial < 6000; ++trial) {
            const auto width = random_width();
            const auto amount_width = random_width(trial % 2 == 0 ? 8U : 64U);
            const auto value = random_word(width);
            auto amount = random_word(amount_width);
            if (trial % 2 == 0) {
                amount.b = 0U;
            }
            if (trial % 5 == 0) {
                amount.a %= width + 3U;
                amount.a &= kw::mask(amount_width);
            }
            const bool signed_amount = trial % 3 == 0;
            expect(kw::shift(operation, value, width, amount, amount_width,
                       signed_amount),
                shift_value(operation, packed(value, width),
                    packed(amount, amount_width), signed_amount),
                "shift " + std::to_string(raw) + " width "
                    + std::to_string(width) + " amount width "
                    + std::to_string(amount_width));
        }
    }
}

void select_ops()
{
    for (int trial = 0; trial < 20000; ++trial) {
        const auto width = random_width();
        const auto value = random_word(width);
        const auto offset = static_cast<std::uint32_t>(random_engine() % width);
        const auto field = 1U + static_cast<std::uint32_t>(
            random_engine() % (width - offset));
        expect(kw::extract(value, offset, field),
            extract_value(packed(value, width), offset, field), "extract");
        const auto source = random_word(field);
        expect(kw::insert(value, source, offset, field),
            insert_value(packed(value, width), packed(source, field), offset),
            "insert");
        const auto condition = random_word(1U);
        const auto other = random_word(width);
        expect(kw::conditional(condition, value, other, width),
            conditional_value(packed(condition, 1U), packed(value, width),
                packed(other, width)),
            "conditional");
    }
}

void dynamic_ops()
{
    for (int trial = 0; trial < 40000; ++trial) {
        const auto source_width = random_width();
        const auto source = random_word(source_width);
        DynamicIndex index;
        index.left = static_cast<std::int64_t>(random_engine() % 80U) - 8;
        index.right = static_cast<std::int64_t>(random_engine() % 80U) - 8;
        index.base_offset = static_cast<std::uint32_t>(random_engine() % 4U);
        auto selector = random_word(32U);
        if (trial % 2 == 0) {
            selector.b = 0U;
            selector.a = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(random_engine() % 90U) - 10);
        }
        const auto offset = kw::dynamic_index(selector, index);
        try {
            const auto expected
                = dynamic_index_offset(packed(selector, 32U), index);
            ++checks;
            if (!offset || *offset != expected) {
                fail("dynamic_index");
            }
        } catch (const std::invalid_argument&) {
            ++checks;
            if (offset) {
                fail("dynamic_index should be invalid");
            }
        }

        DynamicPartIndex part;
        part.left = static_cast<std::int64_t>(random_engine() % 80U) - 8;
        part.right = static_cast<std::int64_t>(random_engine() % 80U) - 8;
        part.base_offset = static_cast<std::uint32_t>(random_engine() % 4U);
        part.width = 1U + static_cast<std::uint32_t>(random_engine() % 16U);
        part.increasing = (random_engine() & 1U) != 0U;
        part.source_descending = (random_engine() & 1U) != 0U;
        const bool two_state = trial % 7 == 0;
        expect(kw::dynamic_part_select(source, source_width, selector,
                   part.left, part.right, part.base_offset, part.width,
                   part.increasing, part.source_descending, two_state),
            dynamic_part_select_value(packed(source, source_width),
                packed(selector, 32U), part.left, part.right,
                part.base_offset, part.width, part.increasing,
                part.source_descending, two_state),
            "dynamic_part_select");
        const auto value = random_word(part.width);
        const auto write = kw::dynamic_part_write(value, selector, part);
        try {
            const auto expected = dynamic_part_write_value(
                packed(value, part.width), packed(selector, 32U), part);
            ++checks;
            if (!write.valid || write.write.has_value() != expected.has_value()) {
                fail("dynamic_part_write presence");
            }
            if (expected) {
                if (write.write->offset != expected->offset
                    || write.write->width != expected->value.width()) {
                    fail("dynamic_part_write placement");
                }
                expect(write.write->value, expected->value,
                    "dynamic_part_write value");
            }
        } catch (const std::invalid_argument&) {
            ++checks;
            if (write.valid) {
                fail("dynamic_part_write should be invalid");
            }
        }
    }
}

} // namespace

int main()
{
    binary_ops();
    logic9_equivalence();
    logic9_conversions();
    unary_ops();
    shift_ops();
    select_ops();
    dynamic_ops();
    std::cout << "kernel word op tests passed (" << checks << " checks)\n";
    return 0;
}
