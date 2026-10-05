// SPDX-License-Identifier: Apache-2.0
#include "logic9_word_lowering.hpp"

#include <fsim/runtime/simir.hpp>

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

#include <algorithm>
#include <limits>

namespace fsim::compiler::llvm_detail {
namespace {

using Builder = llvm::IRBuilder<>;
using runtime::simir::BinaryOperator;

[[nodiscard]] std::uint32_t word_count_for(
    const std::uint32_t width) noexcept
{
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
}

[[nodiscard]] std::uint64_t word_mask(
    const std::uint32_t width, const std::uint32_t word) noexcept
{
    if (word + 1U < word_count_for(width) || width % 64U == 0U) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (UINT64_C(1) << (width % 64U)) - 1U;
}

[[nodiscard]] llvm::ConstantInt* word_mask_value(
    Builder& builder, const std::uint32_t width, const std::uint32_t word)
{
    return builder.getInt64(word_mask(width, word));
}

[[nodiscard]] bool is_logic9_binary_operator(
    const BinaryOperator operation) noexcept
{
    return operation == BinaryOperator::bit_and
        || operation == BinaryOperator::bit_or
        || operation == BinaryOperator::bit_xor;
}

[[nodiscard]] Logic9WordValue zero_value(
    const std::uint32_t width, const std::uint32_t word_count)
{
    Logic9WordValue result;
    result.width = width;
    for (auto& plane : result.planes) {
        plane.resize(word_count);
    }
    return result;
}

} // namespace

bool logic9_word_value_is_well_formed(
    const Logic9WordValue& value,
    const llvm::LLVMContext& context) noexcept
{
    if (value.width == 0U) {
        return false;
    }
    const auto expected_words = word_count_for(value.width);
    for (const auto& plane : value.planes) {
        if (plane.size() != expected_words) {
            return false;
        }
        for (const auto* const word : plane) {
            if (word == nullptr || !word->getType()->isIntegerTy(64)
                || &word->getContext() != &context) {
                return false;
            }
        }
    }
    return true;
}

llvm::Value* emit_logic9_canonical_guard(
    Builder& builder,
    const Logic9WordValue& value)
{
    if (!logic9_word_value_is_well_formed(value, builder.getContext())) {
        return llvm::ConstantInt::getFalse(builder.getContext());
    }

    llvm::Value* valid = llvm::ConstantInt::getTrue(builder.getContext());
    const auto zero = builder.getInt64(0U);
    for (std::uint32_t word = 0U;
         word < word_count_for(value.width); ++word) {
        const auto p0 = value.planes[0U][word];
        const auto p1 = value.planes[1U][word];
        const auto p2 = value.planes[2U][word];
        const auto p3 = value.planes[3U][word];
        auto* const lower_planes = builder.CreateOr(
            p0, builder.CreateOr(p1, p2));
        auto* const reserved = builder.CreateAnd(p3, lower_planes);
        auto* invalid = builder.CreateICmpNE(reserved, zero);

        const auto mask = word_mask(value.width, word);
        if (mask != std::numeric_limits<std::uint64_t>::max()) {
            auto* const all_planes = builder.CreateOr(
                lower_planes, p3);
            auto* const outside_width = builder.CreateAnd(
                all_planes, builder.getInt64(~mask));
            invalid = builder.CreateOr(invalid,
                builder.CreateICmpNE(outside_width, zero));
        }
        valid = builder.CreateAnd(valid, builder.CreateNot(invalid));
    }
    return valid;
}

std::optional<Logic9WordValue> emit_logic9_copy(
    Builder& builder,
    const Logic9WordValue& source)
{
    if (!logic9_word_value_is_well_formed(source, builder.getContext())) {
        return std::nullopt;
    }

    auto result = zero_value(source.width, word_count_for(source.width));
    for (std::uint32_t word = 0U;
         word < word_count_for(source.width); ++word) {
        auto* const mask = word_mask_value(builder, source.width, word);
        for (std::size_t plane = 0U; plane < source.planes.size(); ++plane) {
            result.planes[plane][word] = builder.CreateAnd(
                source.planes[plane][word], mask);
        }
    }
    return result;
}

std::optional<Logic9WordValue> emit_logic9_not(
    Builder& builder,
    const Logic9WordValue& source)
{
    if (!logic9_word_value_is_well_formed(source, builder.getContext())) {
        return std::nullopt;
    }

    auto result = zero_value(source.width, word_count_for(source.width));
    const auto zero = builder.getInt64(0U);
    for (std::uint32_t word = 0U;
         word < word_count_for(source.width); ++word) {
        const auto p0 = source.planes[0U][word];
        const auto p1 = source.planes[1U][word];
        const auto p2 = source.planes[2U][word];
        const auto p3 = source.planes[3U][word];
        auto* const mask = word_mask_value(builder, source.width, word);

        // The UX01 result table maps 0/L -> 1 and 1/H -> 0. U stays U;
        // X, Z, W and '-' map to X. p2 and p3 are consequently zero.
        auto* const known = builder.CreateAnd(
            p1, builder.CreateNot(p3));
        auto* const non_u = builder.CreateOr(
            builder.CreateOr(p0, p1), builder.CreateOr(p2, p3));
        auto* const known_one = builder.CreateAnd(known, p0);
        result.planes[0U][word] = builder.CreateAnd(
            non_u, builder.CreateNot(known_one));
        result.planes[1U][word] = builder.CreateAnd(known, mask);
        result.planes[2U][word] = zero;
        result.planes[3U][word] = zero;
        result.planes[0U][word] = builder.CreateAnd(
            result.planes[0U][word], mask);
    }
    return result;
}

std::optional<Logic9WordValue> emit_logic9_binary(
    Builder& builder,
    const Logic9WordValue& left,
    const Logic9WordValue& right,
    const BinaryOperator operation)
{
    if (!is_logic9_binary_operator(operation)
        || left.width != right.width
        || !logic9_word_value_is_well_formed(left, builder.getContext())
        || !logic9_word_value_is_well_formed(right, builder.getContext())) {
        return std::nullopt;
    }

    auto result = zero_value(left.width, word_count_for(left.width));
    const auto zero = builder.getInt64(0U);
    for (std::uint32_t word = 0U;
         word < word_count_for(left.width); ++word) {
        const auto mask = word_mask_value(builder, left.width, word);
        const auto classify = [&](const Logic9WordValue& value) {
            const auto p0 = value.planes[0U][word];
            const auto p1 = value.planes[1U][word];
            const auto p2 = value.planes[2U][word];
            const auto p3 = value.planes[3U][word];
            auto* const not_p3 = builder.CreateNot(p3);
            auto* const all_planes = builder.CreateOr(
                builder.CreateOr(p0, p1), builder.CreateOr(p2, p3));
            return std::array<llvm::Value*, 3U> {
                builder.CreateAnd(builder.CreateNot(all_planes), mask),
                builder.CreateAnd(builder.CreateAnd(p1,
                    builder.CreateNot(p0)), not_p3),
                builder.CreateAnd(builder.CreateAnd(p1, p0), not_p3),
            };
        };
        const auto left_class = classify(left);
        const auto right_class = classify(right);
        const auto left_u = left_class[0U];
        const auto left_zero = left_class[1U];
        const auto left_one = left_class[2U];
        const auto right_u = right_class[0U];
        const auto right_zero = right_class[1U];
        const auto right_one = right_class[2U];

        llvm::Value* result_zero = nullptr;
        llvm::Value* result_one = nullptr;
        llvm::Value* result_u = nullptr;
        if (operation == BinaryOperator::bit_and) {
            result_zero = builder.CreateOr(left_zero, right_zero);
            result_one = builder.CreateAnd(left_one, right_one);
            result_u = builder.CreateAnd(
                builder.CreateNot(result_zero),
                builder.CreateOr(left_u, right_u));
        } else if (operation == BinaryOperator::bit_or) {
            result_zero = builder.CreateAnd(left_zero, right_zero);
            result_one = builder.CreateOr(left_one, right_one);
            result_u = builder.CreateAnd(
                builder.CreateNot(result_one),
                builder.CreateOr(left_u, right_u));
        } else {
            result_zero = builder.CreateOr(
                builder.CreateAnd(left_zero, right_zero),
                builder.CreateAnd(left_one, right_one));
            result_one = builder.CreateOr(
                builder.CreateAnd(left_zero, right_one),
                builder.CreateAnd(left_one, right_zero));
            result_u = builder.CreateOr(left_u, right_u);
        }

        result.planes[0U][word] = builder.CreateAnd(
            builder.CreateNot(builder.CreateOr(result_zero, result_u)), mask);
        result.planes[1U][word] = builder.CreateAnd(
            builder.CreateOr(result_zero, result_one), mask);
        result.planes[2U][word] = zero;
        result.planes[3U][word] = zero;
    }
    return result;
}

} // namespace fsim::compiler::llvm_detail
