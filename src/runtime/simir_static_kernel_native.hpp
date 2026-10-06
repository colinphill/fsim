// SPDX-License-Identifier: Apache-2.0
//
// Native ABI of the engine v4 static kernel. The kernel groups program units
// (partitions and compiled singleton members) into canonical templates: the
// instructions of a template refer to per-instance binding indices instead of
// slots, host signals, memories and members. A code generator (implemented
// with LLVM in src/compiler/static_kernel_codegen.cpp) compiles each template
// once; every instance calls the same entry with its own binding table and
// register file.
#pragma once

#include "simir_static_kernel_ir.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace fsim::runtime::simir {

/// One deferred VHDL signal assignment (ABI). Word writes carry the value,
/// its 'U' mask and the bit range of their slot; kind 1 refers to a
/// reference-evaluated assignment by index.
struct StaticKernelWrite {
    std::uint32_t slot { };
    std::uint32_t offset { };
    std::uint32_t width { };
    std::uint32_t member { };
    std::uint64_t a { };
    std::uint64_t b { };
    std::uint64_t unknown { };
    std::uint32_t kind { };
    std::uint32_t instruction { };
};
static_assert(sizeof(StaticKernelWrite) == 48U);

/// Shape of a kernel-owned SystemVerilog memory for inline element access
/// (ABI). Generated code sends every access of a memory with `storage` 0, and
/// every write it cannot express as one word write, to the helpers.
struct StaticKernelContainerInfo {
    /// 0: helpers only; 1: one slot per element; 2: one packed slot.
    std::uint32_t storage { };
    /// Storage 2: the packed slot.
    std::uint32_t slot { };
    std::uint64_t count { };
    /// Declared left index and the inclusive index range.
    std::int64_t left { };
    std::int64_t low { };
    std::int64_t high { };
    /// The value of a read with an unknown or out-of-range index.
    std::uint64_t unknown_a { };
    std::uint64_t unknown_b { };
    std::uint32_t element_width { };
    /// Bit 0: two-state elements; bit 1: descending range (left >= right).
    std::uint32_t flags { };
    /// Storage 1: per element, its slot's arena offset and slot index.
    const std::uint32_t* elements { };
    /// Storage 2: the packed slot's arena offset, words per plane and width.
    std::uint32_t packed_offset { };
    std::uint32_t packed_words { };
    std::uint32_t packed_width { };
    std::uint32_t reserved { };
};
static_assert(sizeof(StaticKernelContainerInfo) == 88U);

/// The round's assignments in program order; generated code appends while
/// count < capacity and calls the effect helper otherwise.
struct StaticKernelWriteQueue {
    StaticKernelWrite* data { };
    std::uint32_t count { };
    std::uint32_t capacity { };
};

/// Generated code's view of one unit activation. Field offsets are part of
/// the ABI and are checked by static_asserts in the code generator.
struct StaticKernelNativeFrame {
    /// Kernel word arena (aval words at a slot's offset, bval after them).
    std::uint64_t* arena { };
    /// Two words per binding: for slots (arena offset, slot index with bit
    /// 31 set when the slot is silent); otherwise (value, 0).
    const std::uint32_t* bindings { };
    void* kernel { };
    /// The unit's canonical template body.
    const void* program { };
    /// Updated by mark instructions.
    std::uint32_t member { };
    std::uint32_t position { };
    /// Nonzero after a helper captured an error; generated code returns it.
    std::uint32_t status { };
    std::uint32_t reserved { };
    /// The unit's instance body (wide registers for generic instructions).
    void* instance { };
    /// VHDL delta mode: the round's assignment queue.
    StaticKernelWriteQueue* writes { };
    /// Indexed by memory binding values.
    const StaticKernelContainerInfo* containers { };
};

/// Runtime helpers called by generated code. `evaluate` executes template
/// instruction `at` on the given operand words (the registers named by its
/// x, y and z fields) and writes the result word pair to `out`; `effect`
/// executes a store, memory or host access instruction the same way. `notify`
/// reports that an inline store changed a non-silent slot. `fail` records the
/// reference error for instruction `at` with the given reason code.
struct StaticKernelNativeHelpers {
    void (*evaluate)(StaticKernelNativeFrame*, std::uint32_t at,
        std::uint64_t xa, std::uint64_t xb, std::uint64_t ya, std::uint64_t yb,
        std::uint64_t za, std::uint64_t zb, std::uint64_t* out) { };
    void (*effect)(StaticKernelNativeFrame*, std::uint32_t at,
        std::uint64_t xa, std::uint64_t xb, std::uint64_t ya, std::uint64_t yb,
        std::uint64_t za, std::uint64_t zb, std::uint64_t* out) { };
    void (*notify)(StaticKernelNativeFrame*, std::uint32_t slot,
        std::uint64_t changed) { };
    /// Pure kernel_word::binary for operators generated code does not
    /// inline: `operation_width` is the operator | width << 8.
    void (*binary)(std::uint64_t operation_width, std::uint64_t la,
        std::uint64_t lb, std::uint64_t ra, std::uint64_t rb,
        std::uint64_t* out) { };
    /// `notify` for a field of a wide slot: `changed` is relative to bit
    /// `field_offset`.
    void (*notify_field)(StaticKernelNativeFrame*, std::uint32_t slot,
        std::uint64_t changed, std::uint32_t field_offset) { };
    void (*fail)(StaticKernelNativeFrame*, std::uint32_t at,
        std::uint32_t reason) { };
    /// Executes a generic instruction against the caller's registers.
    void (*generic)(StaticKernelNativeFrame*, kernel_word::Word* registers,
        std::uint32_t at) { };
};
// `evaluate` writes three words: the result pair and, for 'U'-aware
// instructions, the result's 'U' mask.

enum class StaticKernelNativeFailure : std::uint32_t {
    unknown_branch = 1U,
    /// A tracked operand holds 'U': the reference evaluator takes over.
    deoptimize = 2U,
};

using StaticKernelNativeEntry = std::uint32_t (*)(
    StaticKernelNativeFrame*, kernel_word::Word* registers);

struct StaticKernelTemplate {
    const static_kernel_detail::CompiledBody* body { };
    /// Shared by many units and large: worth the optimizing backend.
    bool hot { };
};

class StaticKernelCodegen {
public:
    virtual ~StaticKernelCodegen() = default;
    /// One entry per template; null entries stay on the interpreter tier.
    [[nodiscard]] virtual std::vector<StaticKernelNativeEntry> compile(
        std::span<const StaticKernelTemplate> templates,
        const StaticKernelNativeHelpers& helpers) = 0;
};

} // namespace fsim::runtime::simir
