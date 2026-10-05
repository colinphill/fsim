// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/packed_value.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>

namespace {
using namespace fsim::runtime;
using namespace fsim::tests::runtime::staging_failure_support;

constexpr std::array logic4_states {
    Logic4::zero, Logic4::one, Logic4::x, Logic4::z
};
constexpr std::array logic9_states {
    Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
    Logic9::w, Logic9::l, Logic9::h, Logic9::dont_care
};
constexpr std::array expanded_states {
    Logic9::zero, Logic9::one, Logic9::x, Logic9::z
};

PackedLogic4 pattern(const std::size_t width)
{
    PackedLogic4 value { width, Logic4::zero };
    for (std::size_t bit = 0U; bit < width; ++bit) {
        value.set(bit, logic4_states[bit % logic4_states.size()]);
    }
    return value;
}

void check_pattern(const PackedLogic4& value)
{
    require(!value.is_logic9(), "Logic4 copy preserves its value kind");
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        require(value.get(bit) == logic4_states[bit % logic4_states.size()],
            "copied Logic4 pattern is exact across word boundaries");
    }
}

void copy_and_mutate(const std::size_t width)
{
    const auto original = pattern(width);
    auto copy = original;
    auto moved = std::move(copy);
    PackedLogic4 assigned { 1U, Logic4::x };
    assigned = moved;
    PackedLogic4 move_assigned { 256U, Logic4::z };
    move_assigned = std::move(assigned);
    check_pattern(original);
    check_pattern(moved);
    check_pattern(move_assigned);
    if (width != 0U) {
        move_assigned.set(width - 1U, Logic4::one);
        check_pattern(original);
        check_pattern(moved);
        require(move_assigned.get(width - 1U) == Logic4::one,
            "copy mutation reaches the last bit");
    }
    if (width >= 65U) {
        const auto inserted = PackedLogic4::from_msb_string("Z1XZ");
        moved.insert_bits(inserted, 61U);
        for (std::size_t bit = 0U; bit < 4U; ++bit) {
            require(moved.get(61U + bit) == inserted.get(bit),
                "bit insertion crosses the inline word boundary");
        }
        check_pattern(original);
        const auto slice = original.extract_bits(61U, 4U);
        for (std::size_t bit = 0U; bit < slice.width(); ++bit) {
            require(slice.get(bit) == original.get(61U + bit),
                "bit extraction crosses the inline word boundary");
        }
    }
    const auto words = (width + 63U) / 64U;
    require(original.aval_words().size() == words
            && original.bval_words().size() == words,
        "packed planes have the exact word count");
    if (width % 64U != 0U) {
        const auto mask = (std::uint64_t { 1U } << (width % 64U)) - 1U;
        require((original.aval_words().back() & ~mask) == 0U
                && (original.bval_words().back() & ~mask) == 0U,
            "partial last words retain zero padding");
    }
}

void inline_allocation_window(const std::size_t width)
{
    begin_allocation_count();
    {
        const auto original = pattern(width);
        auto copy = original;
        auto moved = std::move(copy);
        PackedLogic4 assigned;
        assigned = moved;
        PackedLogic4 move_assigned;
        move_assigned = std::move(assigned);
        if (width != 0U) {
            moved.set(width - 1U, Logic4::z);
        }
        check_pattern(original);
        check_pattern(move_assigned);
        if (width != 0U) {
            const auto roundtrip = PackedLogic4::from_word_planes(width,
                original.aval_words(), original.bval_words());
            require(roundtrip == original, "inline plane round trip is exact");
        }
    }
    require(end_allocation_count() == 0U,
        "Logic4 through 128 bits constructs copies mutates and destroys without allocation");
}

void logic9_roundtrip(const std::size_t width)
{
    const auto original = pattern(width);
    auto promoted = original.promoted_to_logic9();
    require(promoted.is_logic9(), "promotion preserves Logic9 kind");
    for (std::size_t bit = 0U; bit < width; ++bit) {
        require(promoted.get_logic9(bit) == expanded_states[bit % 4U],
            "promotion preserves every Logic4 state");
        promoted.set_logic9(bit, logic9_states[bit % logic9_states.size()]);
    }
    const auto retained = promoted;
    if (width != 0U) {
        const auto from_planes = PackedLogic4::from_logic9_word_planes(width,
            promoted.logic9_plane_words(0U), promoted.logic9_plane_words(1U),
            promoted.logic9_plane_words(2U), promoted.logic9_plane_words(3U));
        require(from_planes == retained,
            "all nine Logic9 states round trip through planes");
    }
    promoted.fill(Logic9::h);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        require(retained.get_logic9(bit) == logic9_states[bit % logic9_states.size()],
            "retained Logic9 copy is independent of later mutations");
        require(promoted.get_logic9(bit) == Logic9::h,
            "Logic9 fill reaches both inline and heap words");
    }
    check_pattern(original);
}

void promotion_failure(const std::size_t width)
{
    const auto original = pattern(width);
    bool observed_failure = false;
    bool completed = false;
    for (std::size_t fail_after = 0U; fail_after < 32U; ++fail_after) {
        auto value = original;
        arm_allocation_failure(fail_after);
        try {
            value.set_logic9(width - 1U, Logic9::h);
            clear_allocation_failure();
            require(value.is_logic9() && value.get_logic9(width - 1U) == Logic9::h,
                "promotion retry completes after its allocation preflight");
            completed = true;
        } catch (const std::bad_alloc&) {
            clear_allocation_failure();
            observed_failure = true;
            require(value == original,
                "failed promotion preserves the complete original Logic4 value and kind");
        } catch (...) {
            clear_allocation_failure();
            throw;
        }
        check_pattern(original);
        if (completed) {
            break;
        }
    }
    require(observed_failure && completed,
        "promotion sweep covers failure and eventual success");
}
}

int main()
{
    try {
        for (const auto width : std::array<std::size_t, 12U> {
                 0U, 1U, 63U, 64U, 65U, 100U, 127U, 128U,
                 129U, 256U, 1024U, 2048U }) {
            copy_and_mutate(width);
            logic9_roundtrip(width);
            if (width <= 128U) {
                inline_allocation_window(width);
            }
        }
        for (const auto width : std::array<std::size_t, 4U> { 65U, 128U, 129U, 256U }) {
            promotion_failure(width);
        }
    } catch (const std::exception& error) {
        clear_allocation_failure();
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
