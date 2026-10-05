// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>

#include <cstdint>

namespace fsim::compiler::llvm_detail {

using runtime::simir::BinaryOperator;
using runtime::simir::RegisterId;
using runtime::simir::ValueKind;

namespace {

constexpr unsigned kVectorLanes = 4U;
constexpr std::uint64_t kVectorWords = kVectorLanes;

struct WidePlanes {
    llvm::Value* aval { };
    llvm::Value* bval { };
    llvm::Value* plane2 { };
    llvm::Value* plane3 { };
};

[[nodiscard]] llvm::Constant* vector_splat(
    llvm::LLVMContext& context,
    const std::uint64_t value)
{
    auto* const i64 = llvm::Type::getInt64Ty(context);
    return llvm::ConstantVector::getSplat(
        llvm::ElementCount::getFixed(kVectorLanes),
        llvm::ConstantInt::get(i64, value));
}

[[nodiscard]] llvm::Value* offset_pointer(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    const std::uint64_t word_offset)
{
    if (word_offset == 0U) {
        return base;
    }
    return builder.CreateGEP(
        llvm::Type::getInt64Ty(builder.getContext()),
        base,
        constant_i64(builder.getContext(), word_offset));
}

[[nodiscard]] llvm::Value* load_word(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    std::uint64_t word_offset,
    const char* name);

void store_word(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    std::uint64_t word_offset,
    llvm::Value* value);

[[nodiscard]] llvm::Value* load_vector(
    llvm::IRBuilder<>& builder,
    llvm::FixedVectorType* vector_type,
    llvm::Value* base,
    const std::uint64_t word_offset,
    const char* name)
{
    llvm::Value* value = llvm::UndefValue::get(vector_type);
    for (unsigned lane = 0U; lane < kVectorLanes; ++lane) {
        auto* const word = load_word(
            builder, base, word_offset + lane, name);
        value = builder.CreateInsertElement(value, word, lane);
    }
    return value;
}

void store_vector(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    const std::uint64_t word_offset,
    llvm::Value* value)
{
    for (unsigned lane = 0U; lane < kVectorLanes; ++lane) {
        auto* const word = builder.CreateExtractElement(value, lane);
        store_word(builder, base, word_offset + lane, word);
    }
}

[[nodiscard]] llvm::Value* load_word(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    const std::uint64_t word_offset,
    const char* name)
{
    auto* const pointer = offset_pointer(builder, base, word_offset);
    auto* const load = builder.CreateLoad(
        llvm::Type::getInt64Ty(builder.getContext()), pointer, name);
    load->setAlignment(llvm::Align { 8U });
    return load;
}

void store_word(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    const std::uint64_t word_offset,
    llvm::Value* value)
{
    auto* const pointer = offset_pointer(builder, base, word_offset);
    auto* const store = builder.CreateStore(value, pointer);
    store->setAlignment(llvm::Align { 8U });
}

[[nodiscard]] bool has_constant_plane_forwarding(
    const RegisterSlot& slot) noexcept
{
    if (slot.constant_planes == nullptr) {
        return false;
    }
    if (slot.constant_planes->active) {
        return true;
    }
    for (const auto* const value : slot.constant_planes->published) {
        if (value != nullptr) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] WidePlanes load_vector_planes(
    llvm::IRBuilder<>& builder,
    llvm::FixedVectorType* vector_type,
    const RegisterSlot& slot,
    const std::uint64_t offset)
{
    auto& context = builder.getContext();
    auto* const zero = vector_splat(context, 0U);
    WidePlanes value {
        load_vector(builder, vector_type, slot.aval_base,
            slot.word_offset + offset, "wide.bitwise.aval"),
        load_vector(builder, vector_type, slot.bval_base,
            slot.word_offset + offset, "wide.bitwise.bval"),
        zero,
        zero
    };
    if (slot.kind == ValueKind::logic9) {
        value.plane2 = load_vector(builder, vector_type,
            slot.logic9_plane2_base, slot.word_offset + offset,
            "wide.bitwise.logic9.plane2");
        value.plane3 = load_vector(builder, vector_type,
            slot.logic9_plane3_base, slot.word_offset + offset,
            "wide.bitwise.logic9.plane3");

        auto* const reserved = builder.CreateAnd(
            value.plane3,
            builder.CreateOr(value.aval,
                builder.CreateOr(value.bval, value.plane2)));
        auto* const not_reserved = builder.CreateNot(reserved);
        value.aval = builder.CreateOr(value.aval, reserved);
        value.bval = builder.CreateAnd(value.bval, not_reserved);
        value.plane2 = builder.CreateAnd(value.plane2, not_reserved);
        value.plane3 = builder.CreateAnd(value.plane3, not_reserved);
    }
    return value;
}

[[nodiscard]] WidePlanes coerce_vector_kind(
    llvm::IRBuilder<>& builder,
    WidePlanes value,
    const ValueKind source_kind,
    const ValueKind destination_kind)
{
    if (source_kind == destination_kind) {
        return value;
    }

    auto& context = builder.getContext();
    auto* const zero = vector_splat(context, 0U);
    if (destination_kind == ValueKind::logic9) {
        return {
            value.aval,
            builder.CreateNot(value.bval),
            builder.CreateAnd(
                builder.CreateNot(value.aval), value.bval),
            zero
        };
    }

    return {
        builder.CreateOr(value.aval,
            builder.CreateOr(value.plane3,
                builder.CreateNot(builder.CreateOr(
                    value.bval, value.plane2)))),
        builder.CreateOr(value.plane3,
            builder.CreateNot(value.bval)),
        zero,
        zero
    };
}

[[nodiscard]] WidePlanes lower_logic4_binary(
    llvm::IRBuilder<>& builder,
    WidePlanes lhs,
    WidePlanes rhs,
    const BinaryOperator operation)
{
    if (operation == BinaryOperator::bit_and) {
        auto* const aval = builder.CreateAnd(
            builder.CreateOr(lhs.aval, lhs.bval),
            builder.CreateOr(rhs.aval, rhs.bval));
        return {
            aval,
            builder.CreateAnd(aval,
                builder.CreateOr(lhs.bval, rhs.bval)),
            nullptr,
            nullptr
        };
    }
    if (operation == BinaryOperator::bit_or) {
        auto* const aval = builder.CreateOr(
            builder.CreateOr(lhs.aval, lhs.bval),
            builder.CreateOr(rhs.aval, rhs.bval));
        auto* const lhs_one = builder.CreateAnd(
            lhs.aval, builder.CreateNot(lhs.bval));
        auto* const rhs_one = builder.CreateAnd(
            rhs.aval, builder.CreateNot(rhs.bval));
        auto* const known_one = builder.CreateOr(lhs_one, rhs_one);
        return {
            aval,
            builder.CreateAnd(aval, builder.CreateNot(known_one)),
            nullptr,
            nullptr
        };
    }

    auto* const unknown = builder.CreateOr(lhs.bval, rhs.bval);
    return {
        builder.CreateOr(
            builder.CreateXor(lhs.aval, rhs.aval), unknown),
        unknown,
        nullptr,
        nullptr
    };
}

[[nodiscard]] WidePlanes lower_logic9_binary_words(
    llvm::IRBuilder<>& builder,
    WidePlanes lhs,
    WidePlanes rhs,
    const BinaryOperator operation)
{
    struct Classes {
        llvm::Value* u;
        llvm::Value* zero;
        llvm::Value* one;
    };
    const auto classify = [&](const WidePlanes value) {
        auto* const not_plane3 = builder.CreateNot(value.plane3);
        auto* const zero = builder.CreateAnd(
            builder.CreateAnd(value.bval,
                builder.CreateNot(value.aval)), not_plane3);
        auto* const one = builder.CreateAnd(
            builder.CreateAnd(value.bval, value.aval), not_plane3);
        auto* const any = builder.CreateOr(
            builder.CreateOr(value.aval, value.bval),
            builder.CreateOr(value.plane2, value.plane3));
        return Classes {
            builder.CreateNot(any), zero, one
        };
    };
    const auto left = classify(lhs);
    const auto right = classify(rhs);

    llvm::Value* result_zero = nullptr;
    llvm::Value* result_one = nullptr;
    llvm::Value* result_u = nullptr;
    if (operation == BinaryOperator::bit_and) {
        result_zero = builder.CreateOr(left.zero, right.zero);
        result_one = builder.CreateAnd(left.one, right.one);
        result_u = builder.CreateAnd(
            builder.CreateNot(result_zero),
            builder.CreateOr(left.u, right.u));
    } else if (operation == BinaryOperator::bit_or) {
        result_zero = builder.CreateAnd(left.zero, right.zero);
        result_one = builder.CreateOr(left.one, right.one);
        result_u = builder.CreateAnd(
            builder.CreateNot(result_one),
            builder.CreateOr(left.u, right.u));
    } else {
        result_zero = builder.CreateOr(
            builder.CreateAnd(left.zero, right.zero),
            builder.CreateAnd(left.one, right.one));
        result_one = builder.CreateOr(
            builder.CreateAnd(left.zero, right.one),
            builder.CreateAnd(left.one, right.zero));
        result_u = builder.CreateOr(left.u, right.u);
    }

    auto* const zero = llvm::Constant::getNullValue(lhs.aval->getType());
    return {
        builder.CreateNot(builder.CreateOr(result_zero, result_u)),
        builder.CreateOr(result_zero, result_one),
        zero,
        zero
    };
}

[[nodiscard]] WidePlanes lower_logic4_not(
    llvm::IRBuilder<>& builder,
    WidePlanes source)
{
    return {
        builder.CreateOr(
            builder.CreateNot(source.aval, "wide.bitwise.not.aval"),
            source.bval),
        source.bval,
        nullptr,
        nullptr
    };
}

[[nodiscard]] WidePlanes lower_logic9_not_words(
    llvm::IRBuilder<>& builder,
    WidePlanes source)
{
    auto* const known = builder.CreateAnd(
        source.bval,
        builder.CreateNot(
            source.plane3, "wide.bitwise.not.logic9.plane3"));
    auto* const non_u = builder.CreateOr(
        builder.CreateOr(source.aval, source.bval),
        builder.CreateOr(source.plane2, source.plane3));
    auto* const plane0 = builder.CreateAnd(
        non_u,
        builder.CreateNot(builder.CreateAnd(known, source.aval)));
    return {
        plane0,
        known,
        llvm::Constant::getNullValue(source.plane2->getType()),
        llvm::Constant::getNullValue(source.plane3->getType())
    };
}

[[nodiscard]] bool valid_wide_triplet(
    const std::vector<RegisterSlot>& registers,
    const RegisterId destination,
    const RegisterId source_a,
    const RegisterId source_b) noexcept
{
    if (destination >= registers.size()
        || source_a >= registers.size()
        || source_b >= registers.size()) {
        return false;
    }
    const auto& output = registers[destination];
    const auto& left = registers[source_a];
    const auto& right = registers[source_b];
    if (output.width <= 64U || output.width != left.width
        || output.width != right.width || output.aval_base == nullptr
        || output.bval_base == nullptr || left.aval_base == nullptr
        || left.bval_base == nullptr || right.aval_base == nullptr
        || right.bval_base == nullptr
        || has_constant_plane_forwarding(output)
        || has_constant_plane_forwarding(left)
        || has_constant_plane_forwarding(right)) {
        return false;
    }
    const auto valid_logic9 = [](const RegisterSlot& slot) {
        return slot.kind != ValueKind::logic9
            || (slot.logic9_plane2_base != nullptr
                && slot.logic9_plane3_base != nullptr);
    };
    return valid_logic9(output) && valid_logic9(left)
        && valid_logic9(right);
}

[[nodiscard]] bool valid_wide_pair(
    const std::vector<RegisterSlot>& registers,
    const RegisterId destination,
    const RegisterId source) noexcept
{
    if (destination >= registers.size() || source >= registers.size()) {
        return false;
    }
    const auto& output = registers[destination];
    const auto& input = registers[source];
    if (output.width <= 64U || output.width != input.width
        || output.aval_base == nullptr || output.bval_base == nullptr
        || input.aval_base == nullptr || input.bval_base == nullptr
        || has_constant_plane_forwarding(output)
        || has_constant_plane_forwarding(input)) {
        return false;
    }
    const auto valid_logic9 = [](const RegisterSlot& slot) {
        return slot.kind != ValueKind::logic9
            || (slot.logic9_plane2_base != nullptr
                && slot.logic9_plane3_base != nullptr);
    };
    return valid_logic9(output) && valid_logic9(input);
}

void mark_initialized(
    llvm::IRBuilder<>& builder,
    const RegisterSlot& slot)
{
    if (slot.initialized_base == nullptr) {
        return;
    }
    auto* const pointer = builder.CreateGEP(
        llvm::Type::getInt8Ty(builder.getContext()),
        slot.initialized_base,
        constant_i64(builder.getContext(), slot.index));
    builder.CreateStore(
        llvm::ConstantInt::get(llvm::Type::getInt8Ty(builder.getContext()), 1U),
        pointer);
}

[[nodiscard]] EncodedValue load_tail_value(
    llvm::IRBuilder<>& builder,
    const RegisterSlot& slot,
    const std::uint64_t word_offset,
    const std::uint32_t width)
{
    auto* const mask = packed_mask(builder.getContext(), width);
    auto* const zero = packed_constant(builder.getContext(), width, 0U);
    EncodedValue value {
        builder.CreateAnd(load_word(builder, slot.aval_base,
            slot.word_offset + word_offset, "wide.bitwise.tail.aval"), mask),
        builder.CreateAnd(load_word(builder, slot.bval_base,
            slot.word_offset + word_offset, "wide.bitwise.tail.bval"), mask),
        width,
        zero,
        zero,
        slot.kind
    };
    if (slot.kind == ValueKind::logic9) {
        value.logic9_plane2 = builder.CreateAnd(
            load_word(builder, slot.logic9_plane2_base,
                slot.word_offset + word_offset,
                "wide.bitwise.tail.logic9.plane2"), mask);
        value.logic9_plane3 = builder.CreateAnd(
            load_word(builder, slot.logic9_plane3_base,
                slot.word_offset + word_offset,
                "wide.bitwise.tail.logic9.plane3"), mask);
    }
    return canonicalize_logic9_value(builder, value);
}

void store_tail_value(
    llvm::IRBuilder<>& builder,
    const RegisterSlot& destination,
    const std::uint64_t word_offset,
    EncodedValue value)
{
    value = coerce_value_kind(builder, value, destination.kind);
    value = canonicalize_logic9_value(builder, value);
    const auto active_width = destination.width % 64U;
    if (active_width == 0U) {
        return;
    }
    auto* const mask = packed_mask(builder.getContext(), active_width);
    const auto offset = destination.word_offset + word_offset;
    store_word(builder, destination.aval_base, offset,
        builder.CreateAnd(value.aval, mask));
    store_word(builder, destination.bval_base, offset,
        builder.CreateAnd(value.bval, mask));
    if (destination.kind == ValueKind::logic9) {
        store_word(builder, destination.logic9_plane2_base, offset,
            builder.CreateAnd(value.logic9_plane2, mask));
        store_word(builder, destination.logic9_plane3_base, offset,
            builder.CreateAnd(value.logic9_plane3, mask));
    }
}

[[nodiscard]] EncodedValue lower_scalar_binary(
    llvm::IRBuilder<>& builder,
    EncodedValue lhs,
    EncodedValue rhs,
    const BinaryOperator operation)
{
    if (lhs.kind == ValueKind::logic9 || rhs.kind == ValueKind::logic9) {
        lhs = coerce_value_kind(builder, lhs, ValueKind::logic9);
        rhs = coerce_value_kind(builder, rhs, ValueKind::logic9);
        return lower_logic9_binary(builder, lhs, rhs, operation);
    }
    return lower_binary(builder, operation, lhs, rhs);
}

[[nodiscard]] EncodedValue lower_scalar_not(
    llvm::IRBuilder<>& builder,
    const EncodedValue source)
{
    if (source.kind == ValueKind::logic9) {
        return lower_logic9_not(builder, source);
    }
    auto* const mask = packed_mask(builder.getContext(), source.width);
    return {
        builder.CreateAnd(
            builder.CreateOr(builder.CreateNot(source.aval), source.bval), mask),
        source.bval,
        source.width,
        source.logic9_plane2,
        source.logic9_plane3,
        source.kind
    };
}

void emit_binary_words(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const BinaryOperator operation,
    const RegisterId destination_id,
    const RegisterId lhs_id,
    const RegisterId rhs_id)
{
    const auto& destination = registers[destination_id];
    const auto& lhs_slot = registers[lhs_id];
    const auto& rhs_slot = registers[rhs_id];
    auto& context = builder.getContext();
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const vector_type = llvm::FixedVectorType::get(i64, kVectorLanes);
    const auto output_kind = destination.kind;
    const bool logic9_operation = lhs_slot.kind == ValueKind::logic9
        || rhs_slot.kind == ValueKind::logic9;

    const auto process_vector = [&](const std::uint64_t first_word) {
        // Load both sides before any store so destination/input aliases use
        // the same values as the scalar lowering.
        auto lhs = load_vector_planes(
            builder, vector_type, lhs_slot, first_word);
        auto rhs = load_vector_planes(
            builder, vector_type, rhs_slot, first_word);
        if (logic9_operation) {
            lhs = coerce_vector_kind(
                builder, lhs, lhs_slot.kind, ValueKind::logic9);
            rhs = coerce_vector_kind(
                builder, rhs, rhs_slot.kind, ValueKind::logic9);
        }
        auto value = logic9_operation
            ? lower_logic9_binary_words(builder, lhs, rhs, operation)
            : lower_logic4_binary(builder, lhs, rhs, operation);
        value = coerce_vector_kind(builder, value,
            logic9_operation ? ValueKind::logic9 : ValueKind::logic4,
            output_kind);
        store_vector(builder, destination.aval_base,
            destination.word_offset + first_word, value.aval);
        store_vector(builder, destination.bval_base,
            destination.word_offset + first_word, value.bval);
        if (output_kind == ValueKind::logic9) {
            store_vector(builder, destination.logic9_plane2_base,
                destination.word_offset + first_word, value.plane2);
            store_vector(builder, destination.logic9_plane3_base,
                destination.word_offset + first_word, value.plane3);
        }
    };

    const auto full_words = destination.width / 64U;
    std::uint64_t word = 0U;
    for (; word + kVectorWords <= full_words; word += kVectorWords) {
        process_vector(word);
    }
    for (; word < full_words; ++word) {
        auto lhs = load_tail_value(builder, lhs_slot, word, 64U);
        auto rhs = load_tail_value(builder, rhs_slot, word, 64U);
        auto value = lower_scalar_binary(builder, lhs, rhs, operation);
        value = coerce_value_kind(builder, value, output_kind);
        store_word(builder, destination.aval_base,
            destination.word_offset + word, value.aval);
        store_word(builder, destination.bval_base,
            destination.word_offset + word, value.bval);
        if (output_kind == ValueKind::logic9) {
            store_word(builder, destination.logic9_plane2_base,
                destination.word_offset + word, value.logic9_plane2);
            store_word(builder, destination.logic9_plane3_base,
                destination.word_offset + word, value.logic9_plane3);
        }
    }
    const auto tail_width = destination.width % 64U;
    if (tail_width != 0U) {
        const auto tail_word = destination.width / 64U;
        const auto lhs = load_tail_value(
            builder, lhs_slot, tail_word, tail_width);
        const auto rhs = load_tail_value(
            builder, rhs_slot, tail_word, tail_width);
        store_tail_value(builder, destination, tail_word,
            lower_scalar_binary(builder, lhs, rhs, operation));
    }
    mark_initialized(builder, destination);
}

void emit_not_words(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const RegisterId destination_id,
    const RegisterId source_id)
{
    const auto& destination = registers[destination_id];
    const auto& source_slot = registers[source_id];
    auto& context = builder.getContext();
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const vector_type = llvm::FixedVectorType::get(i64, kVectorLanes);
    const auto process_vector = [&](const std::uint64_t first_word) {
        auto value = load_vector_planes(
            builder, vector_type, source_slot, first_word);
        value = source_slot.kind == ValueKind::logic9
            ? lower_logic9_not_words(builder, value)
            : lower_logic4_not(builder, value);
        value = coerce_vector_kind(builder, value,
            source_slot.kind, destination.kind);
        store_vector(builder, destination.aval_base,
            destination.word_offset + first_word, value.aval);
        store_vector(builder, destination.bval_base,
            destination.word_offset + first_word, value.bval);
        if (destination.kind == ValueKind::logic9) {
            store_vector(builder, destination.logic9_plane2_base,
                destination.word_offset + first_word, value.plane2);
            store_vector(builder, destination.logic9_plane3_base,
                destination.word_offset + first_word, value.plane3);
        }
    };

    const auto full_words = destination.width / 64U;
    std::uint64_t word = 0U;
    for (; word + kVectorWords <= full_words; word += kVectorWords) {
        process_vector(word);
    }
    for (; word < full_words; ++word) {
        const auto source = load_tail_value(builder, source_slot, word, 64U);
        auto value = lower_scalar_not(builder, source);
        value = coerce_value_kind(builder, value, destination.kind);
        store_word(builder, destination.aval_base,
            destination.word_offset + word, value.aval);
        store_word(builder, destination.bval_base,
            destination.word_offset + word, value.bval);
        if (destination.kind == ValueKind::logic9) {
            store_word(builder, destination.logic9_plane2_base,
                destination.word_offset + word, value.logic9_plane2);
            store_word(builder, destination.logic9_plane3_base,
                destination.word_offset + word, value.logic9_plane3);
        }
    }
    const auto tail_width = destination.width % 64U;
    if (tail_width != 0U) {
        const auto tail_word = destination.width / 64U;
        const auto source = load_tail_value(
            builder, source_slot, tail_word, tail_width);
        store_tail_value(builder, destination, tail_word,
            lower_scalar_not(builder, source));
    }
    mark_initialized(builder, destination);
}

} // namespace

bool try_lower_wide_bitwise_binary(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const BinaryOperator operation,
    const RegisterId destination,
    const RegisterId lhs,
    const RegisterId rhs)
{
    if (operation != BinaryOperator::bit_and
        && operation != BinaryOperator::bit_or
        && operation != BinaryOperator::bit_xor) {
        return false;
    }
    if (!valid_wide_triplet(registers, destination, lhs, rhs)) {
        return false;
    }
    emit_binary_words(builder, registers, operation, destination, lhs, rhs);
    return true;
}

bool try_lower_wide_bitwise_not(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const RegisterId destination,
    const RegisterId source)
{
    if (!valid_wide_pair(registers, destination, source)) {
        return false;
    }
    emit_not_words(builder, registers, destination, source);
    return true;
}

} // namespace fsim::compiler::llvm_detail
