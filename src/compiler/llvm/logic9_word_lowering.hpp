// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <llvm/IR/IRBuilder.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace fsim::runtime::simir {
enum class BinaryOperator : std::uint8_t;
}

namespace fsim::compiler::llvm_detail {

/// Four parallel packed-word planes using the Logic9 ordinal encoding.
/// Plane 0 is the least significant ordinal bit, through plane 3. Values are
/// SSA words, not runtime pointers; the caller owns all loading and storage.
struct Logic9WordValue final {
    std::uint32_t width { };
    std::array<std::vector<llvm::Value*>, 4U> planes;
};

/// Check the static word count, non-null values, i64 element types and context.
[[nodiscard]] bool logic9_word_value_is_well_formed(
    const Logic9WordValue& value,
    const llvm::LLVMContext& context) noexcept;

/// Emit a predicate requiring only Logic9 ordinals 0 through 8 and zero tail
/// bits in every plane. It is a read-only preflight guard; it never rewrites
/// malformed values. The caller must branch to decline before executing a
/// body which consumes the value.
[[nodiscard]] llvm::Value* emit_logic9_canonical_guard(
    llvm::IRBuilder<>& builder,
    const Logic9WordValue& value);

/// Copy every plane word and mask unused bits in the final word.
[[nodiscard]] std::optional<Logic9WordValue> emit_logic9_copy(
    llvm::IRBuilder<>& builder,
    const Logic9WordValue& source);

/// IEEE std_logic_1164 unary NOT, producing only UX01 result codes.
[[nodiscard]] std::optional<Logic9WordValue> emit_logic9_not(
    llvm::IRBuilder<>& builder,
    const Logic9WordValue& source);

/// IEEE std_logic_1164 bitwise AND, OR or XOR. Operands must have equal width.
/// The helper declines unsupported operators without emitting IR.
[[nodiscard]] std::optional<Logic9WordValue> emit_logic9_binary(
    llvm::IRBuilder<>& builder,
    const Logic9WordValue& left,
    const Logic9WordValue& right,
    runtime::simir::BinaryOperator operation);

} // namespace fsim::compiler::llvm_detail
