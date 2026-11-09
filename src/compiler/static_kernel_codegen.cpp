// SPDX-License-Identifier: Apache-2.0
//
// LLVM code generation for engine v4 static kernel templates (see
// src/runtime/simir_static_kernel_native.hpp). Each template becomes one
// function `i32 (ptr frame, ptr noalias registers)`. Registers are word pairs
// in the caller-owned register file; four-state bitwise, comparison, simple
// arithmetic, selection and slot accesses are emitted inline with the exact
// formulas of simir_kernel_word_ops.hpp. Shifts, dynamic selections, division
// and power, memories, host accesses and deferred stores call the runtime
// helpers, which share the interpreter tier's implementation.
#include "fsim/compiler/static_kernel_codegen.hpp"
#include "fsim/compiler/object_cache.hpp"
#include "fsim/support/native_filesystem.hpp"

#include "../runtime/simir_static_kernel_native.hpp"

#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Analysis/InstSimplifyFolder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/xxhash.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <sstream>
#include <chrono>
#include <fstream>
#include <functional>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#if defined(__MINGW32__)
// LLVM emits GNU Windows stack probes for large JIT frames. Referencing the
// compiler-rt implementation keeps it in the host for the absolute binding.
extern "C" void ___chkstk_ms();
#endif

namespace fsim::compiler {

namespace {

#if defined(_WIN32)
/// Registers the Windows unwind table (.pdata) of each object RuntimeDyld
/// loads. LLVM's default memory manager only knows DWARF frame registration,
/// so Windows could not unwind JIT frames, and libunwind logged a missing
/// table whenever a C++ handler was entered from JIT code.
class WindowsUnwindMemoryManager final : public llvm::SectionMemoryManager {
public:
    /// One reservation per object keeps its sections within the 32-bit
    /// offsets of its unwind table.
    WindowsUnwindMemoryManager()
        : llvm::SectionMemoryManager(nullptr, true)
    {
    }

    ~WindowsUnwindMemoryManager() override
    {
        WindowsUnwindMemoryManager::deregisterEHFrames();
    }

    std::uint8_t* allocateCodeSection(const std::uintptr_t size,
        const unsigned alignment, const unsigned section,
        const llvm::StringRef name) override
    {
        return note(llvm::SectionMemoryManager::allocateCodeSection(
            size, alignment, section, name));
    }

    std::uint8_t* allocateDataSection(const std::uintptr_t size,
        const unsigned alignment, const unsigned section,
        const llvm::StringRef name, const bool read_only) override
    {
        return note(llvm::SectionMemoryManager::allocateDataSection(
            size, alignment, section, name, read_only));
    }

    void registerEHFrames(std::uint8_t* const table, std::uint64_t,
        const std::size_t size) override
    {
        auto* const functions = reinterpret_cast<RUNTIME_FUNCTION*>(table);
        const auto count = static_cast<DWORD>(size / sizeof(RUNTIME_FUNCTION));
        if (count != 0U && RtlAddFunctionTable(functions, count, image_base_)) {
            tables_.push_back(functions);
        }
    }

    void deregisterEHFrames() override
    {
        for (auto* const functions : tables_) {
            (void)RtlDeleteFunctionTable(functions);
        }
        tables_.clear();
    }

private:
    /// The .pdata entries are relative to the object's lowest section, the
    /// image base RuntimeDyld relocates them against; every section it loads
    /// is allocated here.
    std::uint8_t* note(std::uint8_t* const section)
    {
        if (section != nullptr) {
            image_base_ = std::min(image_base_,
                static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(section)));
        }
        return section;
    }

    DWORD64 image_base_ { std::numeric_limits<DWORD64>::max() };
    std::vector<RUNTIME_FUNCTION*> tables_;
};
#endif

using runtime::simir::StaticKernelCodegen;
using runtime::simir::StaticKernelNativeEntry;
using runtime::simir::StaticKernelNativeFrame;
using runtime::simir::StaticKernelNativeHelpers;
using runtime::simir::StaticKernelTemplate;
using runtime::simir::static_kernel_prologue_entry;
using runtime::simir::BinaryOperator;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::ReductionOperator;
using runtime::simir::SignalReadKind;
using runtime::simir::ProcessSchedulingDomain;
namespace detail = runtime::simir::static_kernel_detail;
using detail::KInst;
using detail::KOp;

/// The symbol of the runtime helper for instructions of kind `op`.
[[nodiscard]] std::string instruction_helper_name(const char* prefix,
    const KOp op)
{
    return prefix + std::to_string(static_cast<unsigned>(op));
}

/// Instructions that generated code runs through the runtime's evaluate,
/// effect or wide helper (call_evaluate, call_effect, call_wide), by kind:
/// the profile reports each as `reason=effect:<kind>` (a kernel whose
/// instructions are all inline reports none; host-signal accesses and the
/// library operations are the ones that stay).
constexpr std::size_t helper_site_kinds = 64U;
std::array<std::atomic<std::uint32_t>, helper_site_kinds> helper_sites { };
/// The same for a helper that only a full deferred queue runs (it grows the
/// queue): `reason=capacity:<kind>`, the one helper the owner allows.
std::array<std::atomic<std::uint32_t>, helper_site_kinds> capacity_sites { };
/// And for a helper that only raises the reference's error for an operand
/// that fails a check (`reason=failure:<kind>`): the run ends there, and the
/// helper formats the reference's exact message.
std::array<std::atomic<std::uint32_t>, helper_site_kinds> failure_sites { };
/// And for an access of a host signal, which the host applies
/// (`reason=host:<kind>`): the boundary the owner accepted.
std::array<std::atomic<std::uint32_t>, helper_site_kinds> host_sites { };
/// And for a helper that only prints a report and goes on (a failed VHDL
/// assertion below failure severity): a library operation,
/// `reason=library:<kind>`.
std::array<std::atomic<std::uint32_t>, helper_site_kinds> library_sites { };

constexpr std::array<const char*, 57> kop_names {
    "nop", "constant", "copy", "load_slot", "load_host", "binary", "reduce",
    "unary_not", "logical_not", "logical_binary", "shift", "extract", "insert",
    "concat", "conditional", "two_state", "dynamic_extract",
    "dynamic_part_select", "dynamic_insert", "dynamic_part_insert", "jump",
    "branch", "store_slot", "store_slot_nba", "store_slot_dynamic",
    "store_slot_part", "store_host", "mem_bind", "mem_read", "mem_write",
    "mem_copy", "mark", "generic", "load_field", "load_slot9", "store_vhdl",
    "integer_binary", "integer_unary", "integer_check", "call", "ret",
    "assert_check", "load_field9", "wide_move", "suspend", "step", "wide_load",
    "wide_copy", "wide_const", "wide_concat", "wide_store", "wide_part_insert",
    "net_delay", "wide_alu", "sva_tick", "sva_outcome", "sva_sampled_load" };

/// category 0: an instruction run by its helper (effect), 1: a full deferred
/// queue's growth (capacity), 2: a failing check's error report (failure),
/// 3: a host signal's access (host); 4: a report that goes on (library).
void note_helper_site(const KOp op, const unsigned category = 0U)
{
    const auto kind = static_cast<std::size_t>(op);
    if (kind < helper_site_kinds) {
        (category == 1U   ? capacity_sites
                : category == 2U ? failure_sites
                : category == 3U ? host_sites
                : category == 4U ? library_sites
                                 : helper_sites)[kind]
            .fetch_add(1U, std::memory_order_relaxed);
    }
}

void report_helper_sites()
{
    for (const unsigned category : { 0U, 1U, 2U, 3U, 4U }) {
        auto& sites = category == 1U ? capacity_sites
            : category == 2U ? failure_sites
            : category == 3U ? host_sites
            : category == 4U ? library_sites : helper_sites;
        for (std::size_t kind = 0U; kind < helper_site_kinds; ++kind) {
            const auto count = sites[kind].exchange(0U);
            if (count != 0U) {
                std::cerr << "fsim-profile: static-kernel-helper reason="
                          << (category == 1U ? "capacity:"
                                  : category == 2U ? "failure:"
                                  : category == 3U ? "host:"
                                  : category == 4U ? "library:" : "effect:")
                          << (kind < kop_names.size() ? kop_names[kind] : "?")
                          << " count=" << count << '\n';
            }
        }
    }
}

static_assert(offsetof(StaticKernelNativeFrame, arena) == 0U);
static_assert(offsetof(StaticKernelNativeFrame, bindings) == 8U);
static_assert(offsetof(StaticKernelNativeFrame, member) == 32U);
static_assert(offsetof(StaticKernelNativeFrame, position) == 36U);
static_assert(offsetof(StaticKernelNativeFrame, status) == 40U);
static_assert(offsetof(StaticKernelNativeFrame, reserved) == 44U);
static_assert(offsetof(StaticKernelNativeFrame, writes) == 56U);
static_assert(offsetof(StaticKernelNativeFrame, containers) == 64U);
static_assert(offsetof(StaticKernelNativeFrame, boundary) == 72U);
static_assert(offsetof(StaticKernelNativeFrame, fast_notify) == 80U);
static_assert(offsetof(StaticKernelNativeFrame, partition_queued) == 88U);
static_assert(offsetof(StaticKernelNativeFrame, running_partition) == 96U);
static_assert(offsetof(StaticKernelNativeFrame, write_round) == 100U);
static_assert(offsetof(StaticKernelNativeFrame, write_marks) == 104U);
static_assert(offsetof(StaticKernelNativeFrame, assertion_starts) == 112U);
static_assert(offsetof(StaticKernelNativeFrame, assertion_filter) == 116U);
static_assert(offsetof(StaticKernelNativeFrame, sva) == 120U);
static_assert(offsetof(StaticKernelNativeFrame, call_stack) == 128U);
static_assert(offsetof(StaticKernelNativeFrame, frame_stack) == 136U);
static_assert(offsetof(detail::NativeCallStack, entries) == 0U);
static_assert(offsetof(detail::NativeCallStack, capacity) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelSvaTable, blocks) == 0U);
static_assert(offsetof(runtime::simir::StaticKernelSvaTable, member_base) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelSvaTable, pop_serial) == 16U);
static_assert(offsetof(runtime::simir::StaticKernelSvaTable, rings) == 24U);
static_assert(offsetof(runtime::simir::StaticKernelSvaTable, sampled) == 32U);
static_assert(offsetof(runtime::simir::StaticKernelSvaTable, now) == 40U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, slot) == 4U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, count) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, left) == 16U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, low) == 24U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, high) == 32U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, unknown_a) == 40U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, unknown_b) == 48U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, element_width)
    == 56U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, flags) == 60U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, elements) == 64U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, packed_offset)
    == 72U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, packed_words)
    == 76U);
static_assert(offsetof(runtime::simir::StaticKernelContainerInfo, packed_width)
    == 80U);
static_assert(offsetof(runtime::simir::StaticKernelWriteQueue, count) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelWriteQueue, capacity) == 12U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, offset) == 4U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, width) == 8U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, member) == 12U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, a) == 16U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, b) == 24U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, unknown) == 32U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, kind) == 40U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, instruction) == 44U);
static_assert(offsetof(runtime::simir::StaticKernelWrite, weak) == 48U);

constexpr std::uint32_t silent_bit = std::uint32_t { 1 } << 31U;

std::once_flag native_target_once;

void initialize_native_target()
{
    std::call_once(native_target_once, [] {
        if (llvm::InitializeNativeTarget()
            || llvm::InitializeNativeTargetAsmPrinter()) {
            throw std::runtime_error(
                "static kernel codegen cannot initialize the native target");
        }
        const char* text = std::getenv("FSIM_STATIC_KERNEL_LLVM_ARGS");
        // The machine scheduler links every memory access of a region to
        // the pending ones until 1000 are pending: on a hot template's long
        // straight-line blocks that is quadratic (mixed_codec hot tier
        // 1030 -> 238 ms with 100; simulate unchanged on mixed_codec,
        // original_codec and LDPC).
        if (text == nullptr
            || std::string_view { text }.find("dag-maps-huge-region")
                == std::string_view::npos) {
            auto& options = llvm::cl::getRegisteredOptions();
            if (const auto found = options.find("dag-maps-huge-region");
                found != options.end()) {
                (void)found->second->addOccurrence(
                    0U, "dag-maps-huge-region", "100");
            }
        }
        // Diagnostic: LLVM command-line options (space separated) for
        // codegen experiments, e.g. "-regalloc=greedy".
        if (text != nullptr) {
            static std::vector<std::string> words;
            std::istringstream stream { text };
            for (std::string word; stream >> word;) {
                words.push_back(word);
            }
            std::vector<const char*> argv { "fsim" };
            for (const auto& word : words) {
                argv.push_back(word.c_str());
            }
            llvm::cl::ParseCommandLineOptions(
                static_cast<int>(argv.size()), argv.data());
        }
    });
}

[[nodiscard]] std::string error_text(llvm::Error error)
{
    std::string message;
    llvm::raw_string_ostream stream(message);
    llvm::logAllUnhandledErrors(std::move(error), stream);
    return message;
}

[[nodiscard]] std::uint64_t mask_of(const std::uint32_t width)
{
    return width >= 64U ? ~std::uint64_t { 0 }
                        : (std::uint64_t { 1 } << width) - 1U;
}

struct Pair {
    llvm::Value* a { };
    llvm::Value* b { };
};

/// FSIM_KERNEL_SVA_CHECK: every generated tick is followed by the host
/// monitor's on a copy of the block, and the blocks must agree
/// (StaticKernel::sva_check_run).
[[nodiscard]] bool sva_verify_enabled()
{
    // FSIM_KERNEL_VERIFY: the generated tick records itself for the replay.
    static const bool enabled = std::getenv("FSIM_KERNEL_VERIFY") != nullptr;
    return enabled;
}

[[nodiscard]] bool sva_check_enabled()
{
    static const bool enabled = std::getenv("FSIM_KERNEL_SVA_CHECK") != nullptr;
    return enabled;
}

/// Generated code for a concurrent assertion directive's attempt automaton
/// (IEEE 1800-2023 16.14, 39.4.2; task #186): the plan is constant at kernel
/// build, so `sva::Monitor::tick` (Instance::step and Matcher::run of
/// src/runtime/sva_engine.cpp) is unrolled over it. One tick function per
/// directive and one matcher function per sequence work directly on the
/// attempt block (sva::AttemptStorage, layout in sva_storage.hpp) through the
/// block's entry in the kernel's table, reloaded at every use because growth
/// moves the block. The only helper is growth of a block region
/// (fsim_sk_sva_grow, a capacity reason), besides the monitor's creation at
/// its first tick, the attempt-start and step reports a library asked for,
/// the $assertoff filter and the FSIM_KERNEL_SVA_CHECK differential check.
///
/// Supported plans are those of sva_native_plan (simir_static_kernel_
/// compiled.cpp): a sequence, `not` of a sequence, or an implication or
/// followed-by of a sequence and a sequence or vacuous consequent, over
/// sequences of delays, forks, jumps, repetition counters and checks. The
/// record words written are exactly those of Instance::encode and
/// Matcher::encode, so the host decodes them (finish, kill, save).
class SvaAutomatonEmitter {
public:
    SvaAutomatonEmitter(llvm::Module& module, const detail::SvaNativePlan& plan,
        const bool check)
        : context_(module.getContext())
        , module_(module)
        , plan_(plan)
        , check_(check)
        , builder_(context_)
        , i8_(llvm::Type::getInt8Ty(context_))
        , i32_(llvm::Type::getInt32Ty(context_))
        , i64_(llvm::Type::getInt64Ty(context_))
        , i64t_(llvm::Type::getInt64Ty(context_))
        , ptr_(llvm::PointerType::getUnqual(context_))
    {
    }

    /// The tick function of instruction `inst` (at position `at`):
    /// `void (ptr frame, ptr registers)`. `guard_storage` is the register
    /// index of the guards' word (the first chunk of a wide register).
    [[nodiscard]] llvm::Function* tick_function(const KInst& inst,
        const std::uint32_t at, const std::uint32_t guard_storage)
    {
        at_ = at;
        guard_storage_ = guard_storage;
        const auto& plan = plan_;
        const auto root = plan.properties[plan.root];
        // Matcher functions of the sequences the root uses.
        std::vector<std::uint32_t> sequences;
        const bool tree_root = !plain_root();
        if (tree_root) {
            tree_sequences(plan.root, sequences);
        } else switch (root.kind) {
        case semantic::sva::PropertyKind::sequence:
            sequences.push_back(root.sequence);
            break;
        case semantic::sva::PropertyKind::negation:
            sequences.push_back(plan.properties[static_cast<std::size_t>(root.child)].sequence);
            break;
        default:
            sequences.push_back(root.sequence);
            if (plan.properties[static_cast<std::size_t>(root.child)].kind
                == semantic::sva::PropertyKind::sequence) {
                sequences.push_back(
                    plan.properties[static_cast<std::size_t>(root.child)].sequence);
            }
            break;
        }
        for (const auto sequence : sequences) {
            if (!matchers_.contains(sequence)) {
                matchers_.emplace(sequence, matcher_function(sequence));
            }
        }
        return build_tick(inst, guard_storage);
    }

private:
    using Layout = runtime::sva::layout::Header;

    // ---- small builder vocabulary ----

    [[nodiscard]] llvm::Value* k(const std::uint64_t value)
    {
        return llvm::ConstantInt::get(i64_, value);
    }
    [[nodiscard]] llvm::Value* ks(const std::int64_t value)
    {
        return llvm::ConstantInt::get(i64_, static_cast<std::uint64_t>(value));
    }
    [[nodiscard]] llvm::Value* k32(const std::uint32_t value)
    {
        return llvm::ConstantInt::get(i32_, value);
    }
    [[nodiscard]] llvm::Value* h(const Layout field)
    {
        return k(static_cast<std::uint64_t>(field));
    }
    [[nodiscard]] llvm::BasicBlock* block(const char* name)
    {
        return llvm::BasicBlock::Create(context_, name, fn_);
    }
    [[nodiscard]] llvm::Value* add(llvm::Value* left, llvm::Value* right)
    {
        return builder_.CreateAdd(left, right);
    }
    [[nodiscard]] llvm::Value* add(llvm::Value* left, const std::uint64_t right)
    {
        return builder_.CreateAdd(left, k(right));
    }
    [[nodiscard]] llvm::Value* sub(llvm::Value* left, llvm::Value* right)
    {
        return builder_.CreateSub(left, right);
    }
    [[nodiscard]] llvm::Value* mul(llvm::Value* left, const std::uint64_t right)
    {
        return builder_.CreateMul(left, k(right));
    }
    [[nodiscard]] llvm::Value* ne0(llvm::Value* value)
    {
        return builder_.CreateICmpNE(value, k(0U));
    }

    /// A 64-bit local variable (an entry-block alloca), set to `initial`.
    [[nodiscard]] llvm::AllocaInst* var(const char* name, const std::uint64_t initial = 0U)
    {
        llvm::IRBuilder<> entry_builder(&fn_->getEntryBlock(), fn_->getEntryBlock().begin());
        auto* slot = entry_builder.CreateAlloca(i64_, nullptr, name);
        builder_.CreateStore(k(initial), slot);
        return slot;
    }
    [[nodiscard]] llvm::AllocaInst* array(const char* name, const std::uint64_t words)
    {
        llvm::IRBuilder<> entry_builder(&fn_->getEntryBlock(), fn_->getEntryBlock().begin());
        return entry_builder.CreateAlloca(llvm::ArrayType::get(i64_, words), nullptr, name);
    }
    [[nodiscard]] llvm::Value* get(llvm::AllocaInst* slot)
    {
        return builder_.CreateLoad(i64_, slot);
    }
    void set(llvm::AllocaInst* slot, llvm::Value* value)
    {
        builder_.CreateStore(value, slot);
    }
    /// Word `index` (a value or constant) of an array local.
    [[nodiscard]] llvm::Value* element(llvm::AllocaInst* local, const std::uint64_t index)
    {
        return builder_.CreateConstInBoundsGEP2_64(local->getAllocatedType(), local, 0U, index);
    }

    /// The attempt block's base, from the kernel's table entry.
    [[nodiscard]] llvm::Value* base()
    {
        return builder_.CreateLoad(ptr_, slot_addr_);
    }
    [[nodiscard]] llvm::Value* word_at(llvm::Value* index)
    {
        return builder_.CreateInBoundsGEP(i64_, base(), index);
    }
    [[nodiscard]] llvm::Value* rd(llvm::Value* index)
    {
        return builder_.CreateLoad(i64_, word_at(index));
    }
    [[nodiscard]] llvm::Value* rd(const Layout field)
    {
        return rd(h(field));
    }
    void wr(llvm::Value* index, llvm::Value* value)
    {
        builder_.CreateStore(value, word_at(index));
    }
    void wr(const Layout field, llvm::Value* value)
    {
        wr(h(field), value);
    }
    /// A word of the current heap (the records being read), `index` from its
    /// start.
    [[nodiscard]] llvm::Value* old_word(llvm::Value* index)
    {
        return rd(add(rd(runtime::sva::layout::h_heap_off), index));
    }
    /// `old_word(index)` when `condition` (an i64 0 or 1), else 0: the read
    /// itself is skipped, since an index past the record may lie outside the
    /// block.
    [[nodiscard]] llvm::Value* guarded_old(llvm::Value* condition, llvm::Value* index)
    {
        auto* result = var("guarded");
        if_then(ne0(condition), [&] { set(result, old_word(index)); });
        return get(result);
    }
    /// Word `index` of the alternate heap (the records being written).
    [[nodiscard]] llvm::Value* alt_index(llvm::Value* index)
    {
        return add(rd(runtime::sva::layout::h_heap_alt_off), index);
    }
    void alt_put(llvm::Value* index, llvm::Value* value)
    {
        wr(alt_index(index), value);
    }

    template <typename Then>
    void if_then(llvm::Value* condition, const Then& then)
    {
        auto* yes = block("then");
        auto* merge = block("merge");
        builder_.CreateCondBr(condition, yes, merge);
        builder_.SetInsertPoint(yes);
        then();
        if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
            builder_.CreateBr(merge);
        }
        builder_.SetInsertPoint(merge);
    }
    template <typename Then, typename Otherwise>
    void if_else(llvm::Value* condition, const Then& then, const Otherwise& otherwise)
    {
        auto* yes = block("then");
        auto* no = block("else");
        auto* merge = block("merge");
        builder_.CreateCondBr(condition, yes, no);
        builder_.SetInsertPoint(yes);
        then();
        if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
            builder_.CreateBr(merge);
        }
        builder_.SetInsertPoint(no);
        otherwise();
        if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
            builder_.CreateBr(merge);
        }
        builder_.SetInsertPoint(merge);
    }
    /// `for (index = 0; index < count; ++index) body(index)` over a local.
    template <typename Body>
    void for_count(llvm::AllocaInst* index, llvm::Value* count, const Body& body)
    {
        auto* head = block("for");
        auto* step = block("for_body");
        auto* done = block("for_done");
        set(index, k(0U));
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        builder_.CreateCondBr(builder_.CreateICmpULT(get(index), count), step, done);
        builder_.SetInsertPoint(step);
        body();
        if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
            set(index, add(get(index), 1U));
            builder_.CreateBr(head);
        }
        builder_.SetInsertPoint(done);
    }

    // ---- helper calls ----

    /// Returns from the function (value `exit_value_`) when a helper set the
    /// frame's status (an error captured by the host).
    void check_status()
    {
        auto* status = builder_.CreateLoad(i32_, builder_.CreateConstInBoundsGEP1_64(i8_, frame_, 40U));
        auto* next = block("ok");
        builder_.CreateCondBr(builder_.CreateICmpNE(status, k32(0U)), failure_, next);
        builder_.SetInsertPoint(next);
    }
    /// Growth of block region `region` to `needed` rows or words.
    void grow(const std::uint32_t region, llvm::Value* needed)
    {
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_, i64_ }, false);
        auto* callee = module_.getOrInsertFunction("fsim_sk_sva_grow", type).getCallee();
        builder_.CreateCall(type, callee, { frame_, k32(at_), k32(region), needed });
        check_status();
    }
    /// Room for `words` more words in the alternate heap.
    void reserve_alt(llvm::Value* words)
    {
        auto* needed = add(rd(runtime::sva::layout::h_heap_alt_used), words);
        if_then(builder_.CreateICmpUGT(needed, rd(runtime::sva::layout::h_heap_cap)), [&] {
            grow(5U, needed);
        });
    }
    void reserve_alt(const std::uint64_t words)
    {
        reserve_alt(k(words));
    }

    /// Appends one record to ring (`count`, `cap`, `head`, `off`) of
    /// `region`: the values go to the next slot (grown when full).
    void ring_push(const Layout count_field, const Layout cap_field, const Layout head_field,
        const Layout off_field, const std::uint32_t region, const std::uint64_t stride,
        const std::vector<llvm::Value*>& values)
    {
        auto* count = rd(count_field);
        if_then(builder_.CreateICmpEQ(count, rd(cap_field)), [&] {
            grow(region, add(count, 1U));
        });
        auto* capacity = rd(cap_field);
        auto* slot = builder_.CreateAnd(add(rd(head_field), count), sub(capacity, k(1U)));
        auto* at_word = add(rd(off_field), mul(slot, stride));
        for (std::size_t index = 0U; index < values.size(); ++index) {
            wr(add(at_word, index), values[index]);
        }
        wr(count_field, add(count, 1U));
    }
    void push_outcome(llvm::Value* code, llvm::Value* started, llvm::Value* line,
        llvm::Value* column)
    {
        namespace l = runtime::sva::layout;
        ring_push(l::h_outcomes_count, l::h_outcomes_cap, l::h_outcomes_head,
            l::h_outcomes_off, 1U, l::outcome_stride, { code, started, line, column });
    }

    /// Bit `bit` of the known-one words of register `reg` (a guard, the start
    /// or disabled flag): the a word's bit set and the b word's clear.
    [[nodiscard]] llvm::Value* known_one(const std::uint32_t reg, const std::uint32_t bit)
    {
        const auto chunk = reg + bit / 64U;
        auto* a = builder_.CreateLoad(i64_,
            builder_.CreateInBoundsGEP(i64_, registers_, k(static_cast<std::uint64_t>(chunk) * 2U)));
        auto* b = builder_.CreateLoad(i64_,
            builder_.CreateInBoundsGEP(i64_, registers_,
                k(static_cast<std::uint64_t>(chunk) * 2U + 1U)));
        auto* mask = k(std::uint64_t { 1 } << (bit % 64U));
        return builder_.CreateAnd(ne0(builder_.CreateAnd(a, mask)),
            builder_.CreateICmpEQ(builder_.CreateAnd(b, mask), k(0U)));
    }

    /// Whether guard `guard` of the tick's sampled booleans is a known one.
    [[nodiscard]] llvm::Value* known_one_guard(const std::uint32_t guard)
    {
        return known_one(guard_storage_, guard);
    }
    [[nodiscard]] static unsigned next_function_id()
    {
        static std::atomic<unsigned> counter { 0U };
        return ++counter;
    }

    // ---- the plan's constants ----

    /// CompiledPlan::state_id (39.4.2): the id of program counter `pc` of
    /// sequence `sequence`.
    [[nodiscard]] std::int64_t state_id(const std::uint32_t sequence, std::uint32_t pc) const
    {
        const auto& code = plan_.sequences[sequence].code;
        const auto settle = [&](std::uint32_t at) {
            while (at < code.size()
                && (code[at].op == semantic::sva::Op::delay
                    || code[at].op == semantic::sva::Op::counter_set
                    || code[at].op == semantic::sva::Op::counter_increment
                    || code[at].op == semantic::sva::Op::throughout_begin
                    || code[at].op == semantic::sva::Op::first_match_begin)) {
                ++at;
            }
            return at;
        };
        pc = settle(pc);
        if (pc == settle(0U)) {
            return 0;
        }
        if (pc >= code.size() || code[pc].op == semantic::sva::Op::match) {
            return 1;
        }
        return 2 + static_cast<std::int64_t>(plan_.sequences[sequence].state_base) + pc;
    }

    llvm::LLVMContext& context_;
    llvm::Module& module_;
    const detail::SvaNativePlan& plan_;
    bool check_ { };
    llvm::IRBuilder<> builder_;
    llvm::Type* i8_ { };
    llvm::Type* i32_ { };
    llvm::Type* i64_ { };
    llvm::IntegerType* i64t_ { };
    llvm::Type* ptr_ { };
    llvm::Function* fn_ { };
    llvm::Value* frame_ { };
    llvm::Value* registers_ { };
    llvm::Value* slot_addr_ { };
    llvm::BasicBlock* failure_ { };
    std::uint32_t at_ { };
    std::uint32_t guard_storage_ { };
    std::map<std::uint32_t, llvm::Function*> matchers_;
    llvm::GlobalVariable* positions_ { };
    llvm::Value* env_ { };
    llvm::Value* tick_ { };

    // ---- the sequence matcher (Matcher::step and Matcher::run) ----

    /// The words of a sequence's threads: a thread is pc, clock, flags,
    /// switch_time, due_lo, due_hi, the counted counters, then the counted
    /// throughout entries (guard, from, line, column), the counted first_match
    /// groups and the join reference count (0). The working form keeps the
    /// maximum entries and groups the program can nest, unused ones zero; the
    /// record holds the used ones only (Matcher::encode).
    struct SeqShape {
        std::uint64_t counters { };
        std::uint64_t throughout { };
        std::uint64_t groups { };
        std::uint64_t off_nt { };
        std::uint64_t off_ng { };
        std::uint64_t joins { };
        std::uint64_t off_jr { };
        std::uint64_t words { };
        bool extended { };
    };

    [[nodiscard]] SeqShape shape_of(const std::uint32_t sequence) const
    {
        SeqShape shape;
        const auto& code = plan_.sequences[sequence].code;
        shape.counters = plan_.sequences[sequence].counters;
        // The slots of a thread's throughout entries, first_match groups and
        // join references: the deepest nesting of each on any path of the
        // program (a data-flow over the program counters; a begin op is
        // matched by its end op, a join's right operand and its continuation
        // start from the thread that began the join). The number of begin
        // ops bounds each, which also stops a loop that would grow one.
        std::array<std::uint64_t, 3> begins { };
        for (const auto& step : code) {
            begins[0] += step.op == semantic::sva::Op::throughout_begin ? 1U : 0U;
            begins[1] += step.op == semantic::sva::Op::first_match_begin ? 1U : 0U;
            begins[2] += step.op == semantic::sva::Op::join_begin ? 1U : 0U;
        }
        using Depth = std::array<std::uint64_t, 3>;
        std::vector<std::optional<Depth>> seen(code.size());
        std::vector<std::pair<std::size_t, Depth>> work;
        shape.throughout = shape.groups = shape.joins = 0U;
        const auto visit = [&](const std::size_t pc, const Depth depth) {
            if (pc >= code.size()) {
                return;
            }
            Depth merged = depth;
            if (seen[pc]) {
                bool grew = false;
                for (std::size_t kind = 0U; kind < 3U; ++kind) {
                    merged[kind] = std::max(depth[kind], (*seen[pc])[kind]);
                    grew = grew || merged[kind] != (*seen[pc])[kind];
                }
                if (!grew) {
                    return;
                }
            }
            for (std::size_t kind = 0U; kind < 3U; ++kind) {
                merged[kind] = std::min(merged[kind], begins[kind]);
            }
            seen[pc] = merged;
            work.emplace_back(pc, merged);
        };
        visit(0U, Depth { });
        while (!work.empty()) {
            const auto [pc, depth] = work.back();
            work.pop_back();
            shape.throughout = std::max(shape.throughout, depth[0]);
            shape.groups = std::max(shape.groups, depth[1]);
            shape.joins = std::max(shape.joins, depth[2]);
            const auto& step = code[pc];
            Depth next = depth;
            switch (step.op) {
            case semantic::sva::Op::fork:
                visit(step.a, depth);
                break;
            case semantic::sva::Op::jump:
                visit(step.a, depth);
                continue;
            case semantic::sva::Op::counter_branch:
                visit(step.b, depth);
                break;
            case semantic::sva::Op::throughout_begin:
                ++next[0];
                break;
            case semantic::sva::Op::throughout_end:
                next[0] -= next[0] != 0U ? 1U : 0U;
                break;
            case semantic::sva::Op::first_match_begin:
                ++next[1];
                break;
            case semantic::sva::Op::first_match_end:
                next[1] -= next[1] != 0U ? 1U : 0U;
                break;
            case semantic::sva::Op::join_begin: {
                // The continuation is the thread as it was; both operands
                // carry one more reference.
                visit(step.c, depth);
                ++next[2];
                visit(step.b, next);
                break;
            }
            case semantic::sva::Op::join_end:
            case semantic::sva::Op::match:
                continue;
            case semantic::sva::Op::check:
            case semantic::sva::Op::delay:
            case semantic::sva::Op::counter_set:
            case semantic::sva::Op::counter_increment:
            case semantic::sva::Op::clock:
                break;
            }
            shape.throughout = std::max(shape.throughout, next[0]);
            shape.groups = std::max(shape.groups, next[1]);
            shape.joins = std::max(shape.joins, next[2]);
            visit(pc + 1U, next);
        }
        shape.extended = shape.throughout != 0U || shape.groups != 0U || shape.joins != 0U;
        shape.off_nt = 7U + shape.counters;
        shape.off_ng = shape.off_nt + 1U + 4U * shape.throughout;
        shape.off_jr = shape.off_ng + 1U + shape.groups;
        shape.words = shape.off_jr + 1U + 2U * shape.joins;
        return shape;
    }

    /// The length in words of the encoded thread (Matcher::encode_thread) at
    /// `at` of the current heap: the header words, the counted throughout
    /// entries, groups and join references.
    [[nodiscard]] llvm::Value* encoded_thread_length(llvm::Value* at, const SeqShape& shape)
    {
        auto* throughout = old_word(add(at, shape.off_nt));
        auto* groups_at = add(add(at, shape.off_nt + 1U), mul(throughout, 4U));
        auto* groups = old_word(groups_at);
        auto* refs = old_word(add(add(groups_at, 1U), groups));
        return add(add(add(mul(throughout, 4U), groups), mul(refs, 2U)), shape.off_nt + 3U);
    }

    /// The length in words of the matcher record (Matcher::encode) at
    /// `at` of the current heap: five words, the counted threads, the join
    /// count and the joins. `at` may be garbage when `present` (an i64 0 or
    /// 1) is 0.
    [[nodiscard]] llvm::Value* matcher_record_length(llvm::Value* present, llvm::Value* at,
        const std::uint32_t sequence)
    {
        const auto shape = shape_of(sequence);
        auto* count = guarded_old(present, add(at, 4U));
        if (!shape.extended) {
            return add(builder_.CreateMul(count, k(shape.words)), 6U);
        }
        auto* position = var("record_end");
        set(position, add(at, 5U));
        auto* index = var("record_thread");
        for_count(index, count, [&] {
            set(position, add(get(position), encoded_thread_length(get(position), shape)));
        });
        if (shape.joins != 0U) {
            // The joins: id, kind, the continuation thread, then the two
            // counted sets of end ticks.
            auto* joins = guarded_old(present, get(position));
            set(position, add(get(position), 1U));
            auto* join = var("record_join");
            for_count(join, joins, [&] {
                auto* thread = add(get(position), 2U);
                auto* ends = add(thread, encoded_thread_length(thread, shape));
                auto* second = add(add(ends, old_word(ends)), 1U);
                set(position, add(add(second, old_word(second)), 1U));
            });
            return sub(get(position), at);
        }
        return add(sub(get(position), at), 1U);
    }

    llvm::Value* position_table()
    {
        if (positions_ == nullptr) {
            std::vector<std::uint64_t> words;
            for (const auto& position : plan_.guard_positions) {
                words.push_back(position.first);
                words.push_back(position.second);
            }
            if (words.empty()) {
                words = { 0U, 0U };
            }
            positions_ = new llvm::GlobalVariable(module_,
                llvm::ArrayType::get(i64_, words.size()), true,
                llvm::GlobalValue::PrivateLinkage,
                llvm::ConstantDataArray::get(context_, llvm::ArrayRef<std::uint64_t>(words)),
                "sva_guard_positions");
        }
        return positions_;
    }

    /// The step events (39.4.2) of a check: when the attempt is stepping
    /// (env[2]), one record per passed or failed boolean. `guard` is a value:
    /// a throughout condition is read from the thread.
    void note_step(llvm::Value* env, const bool failure, const std::uint32_t sequence,
        const std::uint32_t from, const std::uint32_t to, llvm::Value* guard,
        llvm::Value* now)
    {
        namespace l = runtime::sva::layout;
        auto* stepping = builder_.CreateLoad(i64_,
            builder_.CreateConstInBoundsGEP1_64(i64_, env, 2U));
        if_then(ne0(stepping), [&] {
            auto* in_range = builder_.CreateICmpULT(guard, k(plan_.guard_positions.size()));
            auto* safe = builder_.CreateSelect(in_range, guard, k(0U));
            auto* table = position_table();
            auto* line = builder_.CreateSelect(in_range,
                builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_, table, mul(safe, 2U))),
                k(0U));
            auto* column = builder_.CreateSelect(in_range,
                builder_.CreateLoad(i64_,
                    builder_.CreateInBoundsGEP(i64_, table, add(mul(safe, 2U), 1U))),
                k(0U));
            const auto env_word = [&](const std::uint64_t index) {
                return builder_.CreateLoad(i64_,
                    builder_.CreateConstInBoundsGEP1_64(i64_, env, index));
            };
            ring_push(l::h_events_count, l::h_events_cap, l::h_events_head,
                l::h_events_off, 3U, l::event_stride,
                { k(failure ? 1U : 0U), env_word(3U), env_word(1U),
                    ks(state_id(sequence, from)), ks(state_id(sequence, to)),
                    guard, k(sequence), now, line, column });
        });
    }

    /// Whether guard number `index` (a value) of the tick's sampled booleans
    /// is a known one.
    [[nodiscard]] llvm::Value* known_one_dynamic(llvm::Value* index)
    {
        auto* chunk = builder_.CreateAnd(builder_.CreateLShr(index, k(6U)), k(0xffffffffU));
        auto* shift = builder_.CreateAnd(index, k(63U));
        auto* at_word = mul(add(chunk, guard_storage_), 2U);
        auto* a = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_, registers_, at_word));
        auto* b = builder_.CreateLoad(i64_,
            builder_.CreateInBoundsGEP(i64_, registers_, add(at_word, 1U)));
        auto* bit_a = builder_.CreateAnd(builder_.CreateLShr(a, shift), k(1U));
        auto* bit_b = builder_.CreateAnd(builder_.CreateLShr(b, shift), k(1U));
        return builder_.CreateAnd(ne0(bit_a), builder_.CreateICmpEQ(bit_b, k(0U)));
    }

    /// `i64 matcher(frame, registers, slot_addr, env, in_off, start)`: Matcher::step of
    /// sequence `sequence` at tick env[0]. The matcher's input record is at
    /// `in_off` words into the current heap (-1: a new matcher started at
    /// `start`); its output record (Matcher::encode) follows the alternate
    /// heap's cursor, and the result has bit 0 set when the sequence matched
    /// and bit 1 when no thread is left (Matcher::dead). The position of the
    /// last boolean that ended a thread goes to env[4] and env[5].
    ///
    /// A sequence with throughout or first_match keeps its kept threads in the
    /// scratch log (tag 2) in working form and encodes them at the end, after
    /// the threads of a matched first_match group are dropped; the others
    /// write each kept thread to the output record as they go.
    [[nodiscard]] llvm::Function* matcher_function(const std::uint32_t sequence)
    {
        namespace l = runtime::sva::layout;
        const auto& code = plan_.sequences[sequence].code;
        const auto shape = shape_of(sequence);
        const std::uint64_t counters = shape.counters;
        const bool extended = shape.extended;
        const std::uint64_t thread_words = shape.words;
        const std::uint64_t off_nt = shape.off_nt;
        const std::uint64_t off_ng = shape.off_ng;
        // A scratch entry: a tag (0 a thread to run, 1 a visited key, 2 a kept
        // thread, 3 a matched group), a thread.
        const bool has_joins = shape.joins != 0U;
        const std::uint64_t off_jr = shape.off_jr;
        // A matcher with joins keeps five more words per entry: a join's id,
        // kind, the two sides' live flags and its alive flag; an end tick's
        // join and side.
        const std::uint64_t extras = has_joins ? 5U : 0U;
        const std::uint64_t entry_words = thread_words + 1U + extras;
        // The words that make a thread's key (Thread::key): all but the
        // positions of the throughout conditions.
        std::vector<std::uint64_t> key_words;
        for (std::uint64_t word = 0U; word < thread_words; ++word) {
            bool position = false;
            for (std::uint64_t entry = 0U; entry < shape.throughout; ++entry) {
                const auto first = off_nt + 1U + 4U * entry;
                position = position || word == first + 2U || word == first + 3U;
            }
            if (!position) {
                key_words.push_back(word);
            }
        }
        auto* type = llvm::FunctionType::get(i64_, { ptr_, ptr_, ptr_, ptr_, i64_, i64_ }, false);
        auto* saved_fn = fn_;
        auto* saved_frame = frame_;
        auto* saved_registers = registers_;
        auto* saved_slot = slot_addr_;
        auto* saved_failure = failure_;
        auto saved_point = builder_.saveIP();
        fn_ = llvm::Function::Create(type, llvm::Function::InternalLinkage,
            "fsim_sk_sva_matcher_" + std::to_string(next_function_id()), module_);
        fn_->addFnAttr(llvm::Attribute::NoInline);
        fn_->addFnAttr(llvm::Attribute::NoUnwind);
        auto* matcher = fn_;
        frame_ = fn_->getArg(0);
        registers_ = fn_->getArg(1);
        slot_addr_ = fn_->getArg(2);
        auto* env = fn_->getArg(3);
        auto* in_off = fn_->getArg(4);
        auto* start = fn_->getArg(5);
        auto* entry = block("entry");
        failure_ = block("failure");
        builder_.SetInsertPoint(failure_);
        builder_.CreateRet(k(0U));
        builder_.SetInsertPoint(entry);

        const auto env_word = [&](const std::uint64_t index) {
            return builder_.CreateLoad(i64_, builder_.CreateConstInBoundsGEP1_64(i64_, env, index));
        };
        llvm::Value* now = env_word(0U);
        llvm::Value* entry_now = now;
        // A multiclocked sequence (16.13): each thread counts the ticks of
        // its own clock (Matcher::thread_now), so `now` is reloaded from this
        // slot at every program counter; time is the evaluation's time step.
        const bool multi = !plan_.clock_guards.empty();
        const std::uint64_t clock_count = plan_.clock_guards.size();
        auto* time_now = env_word(1U);
        auto* now_slot = multi ? var("now_slot") : nullptr;
        // Word `kind` (0 ticks, 1 time of the last tick, 2 ticked in this
        // evaluation) of clock `clock` in the block's clock words.
        const auto clock_word = [&](const std::uint64_t kind, llvm::Value* clock) {
            return rd(add(add(rd(l::h_clocks_off), kind * clock_count), clock));
        };
        auto* cur = array("thread", thread_words);
        auto* tmp = array("copy", thread_words);
        auto* queue_head = var("queue_head");
        auto* queue_tail = var("queue_tail");
        auto* out_count = var("threads");
        auto* matched = var("matched");
        auto* fail_line = var("fail_line");
        auto* fail_column = var("fail_column");
        auto* next_group = var("next_group", 1U);
        auto* next_join = var("next_join", 1U);
        auto* thread_base = var("thread_base");
        auto* record_start = var("record_start");

        // The output record's matcher words: next_join, next_group, the last
        // failed boolean's position, the thread count (patched at the end).
        const auto has_input = builder_.CreateICmpSGE(in_off, ks(0));
        if_then(has_input, [&] {
            set(next_join, old_word(in_off));
            set(next_group, old_word(add(in_off, 1U)));
            set(fail_line, old_word(add(in_off, 2U)));
            set(fail_column, old_word(add(in_off, 3U)));
        });
        reserve_alt(5U);
        set(record_start, rd(l::h_heap_alt_used));
        alt_put(get(record_start), k(1U));
        alt_put(add(get(record_start), 4U), k(0U));
        wr(l::h_heap_alt_used, add(get(record_start), 5U));
        set(thread_base, add(get(record_start), 5U));
        wr(l::h_scratch_used, k(0U));

        // The scratch log: entries of entry_words words from h_scratch_off.
        const auto scratch_entry = [&](llvm::Value* index) {
            return add(rd(l::h_scratch_off), builder_.CreateMul(index, k(entry_words)));
        };
        // Appends an entry: tag, then the thread words (values, with the pc
        // possibly replaced), then the extra words of a matcher with joins.
        const auto scratch_push = [&](const std::uint64_t tag,
                                      const std::function<llvm::Value*(std::uint64_t)>& word,
                                      const std::array<llvm::Value*, 5>& extra
                                      = std::array<llvm::Value*, 5> { }) {
            auto* count = get(queue_tail);
            auto* needed = builder_.CreateMul(add(count, 1U), k(entry_words));
            if_then(builder_.CreateICmpUGT(needed, rd(l::h_scratch_cap)), [&] {
                grow(6U, needed);
            });
            auto* at_word = scratch_entry(count);
            wr(at_word, k(tag));
            for (std::uint64_t index = 0U; index < thread_words; ++index) {
                wr(add(at_word, index + 1U), word(index));
            }
            for (std::uint64_t index = 0U; index < extras; ++index) {
                wr(add(at_word, thread_words + 1U + index),
                    extra[index] != nullptr ? extra[index] : k(0U));
            }
            set(queue_tail, add(count, 1U));
            wr(l::h_scratch_used, builder_.CreateMul(get(queue_tail), k(entry_words)));
        };
        // The word `index` (a constant) of the extras of entry `entry_index`.
        const auto extra_at = [&](llvm::Value* entry_index, const std::uint64_t index) {
            return add(scratch_entry(entry_index), thread_words + 1U + index);
        };
        const auto cur_word = [&](const std::uint64_t index) {
            return builder_.CreateLoad(i64_, element(cur, index));
        };
        const auto cur_at = [&](llvm::Value* index) {
            return builder_.CreateInBoundsGEP(cur->getAllocatedType(), cur, { k(0U), index });
        };
        const auto copy_cur = [&](llvm::AllocaInst* to) {
            for (std::uint64_t index = 0U; index < thread_words; ++index) {
                builder_.CreateStore(cur_word(index), element(to, index));
            }
        };
        // Whether the scratch entry at `entry_at` is tagged `tag` and has the
        // key of `source` (a word getter).
        const auto same_key = [&](const std::uint64_t tag, llvm::Value* entry_at,
                                  const std::function<llvm::Value*(std::uint64_t)>& source) {
            llvm::Value* equal = builder_.CreateICmpEQ(rd(entry_at), k(tag));
            for (const auto word : key_words) {
                equal = builder_.CreateAnd(equal,
                    builder_.CreateICmpEQ(rd(add(entry_at, word + 1U)), source(word)));
            }
            return equal;
        };

        // Decodes the encoded thread at `thread` of the current heap into the
        // working array `dest`; returns the position after it.
        const auto decode_thread = [&](llvm::AllocaInst* dest, llvm::Value* thread)
            -> llvm::Value* {
            const auto dest_at = [&](llvm::Value* index) {
                return builder_.CreateInBoundsGEP(dest->getAllocatedType(), dest,
                    { k(0U), index });
            };
            for (std::uint64_t word = 0U; word < thread_words; ++word) {
                builder_.CreateStore(k(0U), element(dest, word));
            }
            for (std::uint64_t word = 0U; word < off_nt; ++word) {
                builder_.CreateStore(old_word(add(thread, word)), element(dest, word));
            }
            auto* used = old_word(add(thread, off_nt));
            used = builder_.CreateSelect(builder_.CreateICmpUGT(used, k(shape.throughout)),
                k(shape.throughout), used);
            builder_.CreateStore(used, element(dest, off_nt));
            auto* entry_index = var("decode_entry");
            for_count(entry_index, used, [&] {
                for (std::uint64_t part = 0U; part < 4U; ++part) {
                    auto* slot = add(mul(get(entry_index), 4U), off_nt + 1U + part);
                    builder_.CreateStore(old_word(add(thread, slot)), dest_at(slot));
                }
            });
            auto* groups_at = add(add(thread, off_nt + 1U), mul(used, 4U));
            auto* groups = old_word(groups_at);
            groups = builder_.CreateSelect(builder_.CreateICmpUGT(groups, k(shape.groups)),
                k(shape.groups), groups);
            builder_.CreateStore(groups, element(dest, off_ng));
            auto* group_index = var("decode_group");
            for_count(group_index, groups, [&] {
                builder_.CreateStore(old_word(add(add(groups_at, 1U), get(group_index))),
                    dest_at(add(get(group_index), off_ng + 1U)));
            });
            auto* refs_at = add(add(groups_at, groups), 1U);
            auto* refs = old_word(refs_at);
            refs = builder_.CreateSelect(builder_.CreateICmpUGT(refs, k(shape.joins)),
                k(shape.joins), refs);
            builder_.CreateStore(refs, element(dest, off_jr));
            auto* ref_index = var("decode_ref");
            for_count(ref_index, refs, [&] {
                for (std::uint64_t part = 0U; part < 2U; ++part) {
                    auto* slot = add(mul(get(ref_index), 2U), off_jr + 1U + part);
                    builder_.CreateStore(
                        old_word(add(add(refs_at, 1U), add(mul(get(ref_index), 2U), part))),
                        dest_at(slot));
                }
            });
            return add(add(refs_at, mul(refs, 2U)), 1U);
        };

        // keep(thread): the thread joins the output unless an equal one has.
        const auto keep = [&](llvm::AllocaInst* source) {
            auto* found = var("found");
            auto* index = var("kept_scan");
            if (extended) {
                const auto source_word = [&](const std::uint64_t word) -> llvm::Value* {
                    return builder_.CreateLoad(i64_, element(source, word));
                };
                for_count(index, get(queue_tail), [&] {
                    set(found, builder_.CreateOr(get(found),
                        builder_.CreateZExt(
                            same_key(2U, scratch_entry(get(index)), source_word), i64_)));
                });
                if_then(builder_.CreateICmpEQ(get(found), k(0U)), [&] {
                    scratch_push(2U, source_word);
                    set(out_count, add(get(out_count), 1U));
                });
                return;
            }
            for_count(index, get(out_count), [&] {
                auto* first = add(get(thread_base), builder_.CreateMul(get(index), k(thread_words)));
                llvm::Value* equal = llvm::ConstantInt::getTrue(context_);
                for (std::uint64_t word = 0U; word < thread_words; ++word) {
                    equal = builder_.CreateAnd(equal,
                        builder_.CreateICmpEQ(
                            rd(alt_index(add(first, word))),
                            builder_.CreateLoad(i64_, element(source, word))));
                }
                set(found, builder_.CreateOr(get(found), builder_.CreateZExt(equal, i64_)));
            });
            if_then(builder_.CreateICmpEQ(get(found), k(0U)), [&] {
                reserve_alt(thread_words);
                auto* first = rd(l::h_heap_alt_used);
                for (std::uint64_t word = 0U; word < thread_words; ++word) {
                    alt_put(add(first, word), builder_.CreateLoad(i64_, element(source, word)));
                }
                wr(l::h_heap_alt_used, add(first, thread_words));
                set(out_count, add(get(out_count), 1U));
            });
        };

        // Matcher::step for a multiclocked sequence, on the thread in `cur`:
        // a thread on a clock that did not tick waits, one switching clocks
        // waits for its clock's first tick after the switch; the waiting
        // ones are kept at once (before any thread runs) and the result is 0.
        // A thread that goes on has its window set when it stops switching.
        const auto classify = [&]() -> llvm::Value* {
            auto* runnable = var("seed_runs", 1U);
            auto* clock = cur_word(1U);
            if_else(ne0(clock_word(2U, clock)), [&] {
                auto* flags = cur_word(2U);
                if_then(ne0(builder_.CreateAnd(flags, k(1U))), [&] {
                    auto* switched = cur_word(3U);
                    auto* wait = builder_.CreateOr(builder_.CreateICmpSLT(time_now, switched),
                        builder_.CreateAnd(builder_.CreateICmpEQ(time_now, switched),
                            builder_.CreateICmpEQ(builder_.CreateAnd(flags, k(2U)), k(0U))));
                    if_else(wait, [&] { set(runnable, k(0U)); }, [&] {
                        builder_.CreateStore(builder_.CreateAnd(flags, k(2U)), element(cur, 2U));
                        auto* ticks = clock_word(0U, clock);
                        builder_.CreateStore(ticks, element(cur, 4U));
                        builder_.CreateStore(ticks, element(cur, 5U));
                    });
                });
            }, [&] { set(runnable, k(0U)); });
            if_then(builder_.CreateICmpEQ(get(runnable), k(0U)), [&] { keep(cur); });
            return get(runnable);
        };
        // Seed the queue with the input threads, or a new matcher's one.
        if_else(has_input, [&] {
            auto* count = old_word(add(in_off, 4U));
            auto* index = var("seed");
            if (!extended) {
                for_count(index, count, [&] {
                    auto* first = add(add(in_off, 5U), builder_.CreateMul(get(index), k(thread_words)));
                    if (multi) {
                        for (std::uint64_t word = 0U; word < thread_words; ++word) {
                            builder_.CreateStore(old_word(add(first, word)), element(cur, word));
                        }
                        auto* runnable = classify();
                        if_then(ne0(runnable), [&] {
                            scratch_push(0U, [&](const std::uint64_t word) {
                                return cur_word(word);
                            });
                        });
                        return;
                    }
                    scratch_push(0U, [&](const std::uint64_t word) {
                        return old_word(add(first, word));
                    });
                });
                return;
            }
            // Each thread is decoded into the working form; a thread whose
            // throughout condition fails does not run (Matcher::step).
            auto* position = var("seed_position");
            set(position, add(in_off, 5U));
            for_count(index, count, [&] {
                set(position, decode_thread(cur, get(position)));
                auto* used = cur_word(off_nt);
                // throughout: kept = from < 0 || from > tick || the guard holds.
                auto* alive = var("seed_alive", 1U);
                llvm::Value* seed_tick = now;
                if (multi) {
                    set(alive, classify());
                    seed_tick = clock_word(0U, cur_word(1U));
                }
                auto* check_index = var("seed_check");
                for_count(check_index, used, [&] {
                    if_then(ne0(get(alive)), [&] {
                        auto* slot = add(mul(get(check_index), 4U), off_nt + 1U);
                        auto* from = builder_.CreateLoad(i64_, cur_at(add(slot, 1U)));
                        auto* guard = builder_.CreateLoad(i64_, cur_at(slot));
                        auto* before = builder_.CreateOr(builder_.CreateICmpSLT(from, ks(0)),
                            builder_.CreateICmpSGT(from, seed_tick));
                        auto* open = block("throughout_open");
                        auto* done = block("throughout_done");
                        builder_.CreateCondBr(before, done, open);
                        builder_.SetInsertPoint(open);
                        auto* holds = known_one_dynamic(guard);
                        auto* broken = block("throughout_broken");
                        builder_.CreateCondBr(holds, done, broken);
                        builder_.SetInsertPoint(broken);
                        set(fail_line, builder_.CreateLoad(i64_, cur_at(add(slot, 2U))));
                        set(fail_column, builder_.CreateLoad(i64_, cur_at(add(slot, 3U))));
                        set(alive, k(0U));
                        builder_.CreateBr(done);
                        builder_.SetInsertPoint(done);
                    });
                });
                if_then(ne0(get(alive)), [&] {
                    scratch_push(0U, [&](const std::uint64_t word) { return cur_word(word); });
                });
            });
            if (!has_joins) {
                return;
            }
            // The joins: a continuation thread and the end ticks of each side
            // (Matcher::decode); the entries keep the records' order, which is
            // the order of the ids.
            auto* join_count = old_word(get(position));
            set(position, add(get(position), 1U));
            auto* join_index = var("seed_join");
            for_count(join_index, join_count, [&] {
                auto* id = old_word(get(position));
                auto* kind = old_word(add(get(position), 1U));
                auto* ends_at = var("seed_ends");
                set(ends_at, decode_thread(tmp, add(get(position), 2U)));
                scratch_push(4U,
                    [&](const std::uint64_t word) {
                        return builder_.CreateLoad(i64_, element(tmp, word));
                    },
                    std::array<llvm::Value*, 5> { id, kind, k(0U), k(0U), k(1U) });
                for (std::uint64_t side = 0U; side < 2U; ++side) {
                    auto* ends = old_word(get(ends_at));
                    auto* end_index = var("seed_end");
                    for_count(end_index, ends, [&] {
                        auto* tick = old_word(add(add(get(ends_at), 1U), get(end_index)));
                        scratch_push(5U,
                            [&](const std::uint64_t word) -> llvm::Value* {
                                return word == 0U ? tick : k(0U);
                            },
                            std::array<llvm::Value*, 5> { id, k(side), nullptr, nullptr,
                                nullptr });
                    });
                    set(ends_at, add(add(get(ends_at), ends), 1U));
                }
                set(position, get(ends_at));
            });
        }, [&] {
            scratch_push(0U, [&](const std::uint64_t word) -> llvm::Value* {
                if (word == 4U || word == 5U) {
                    return start;
                }
                if (word == 6U) {
                    return k(counters);
                }
                return k(0U);
            });
        });

        auto* loop = block("next_thread");
        auto* finish = block("finish");
        builder_.CreateBr(loop);
        builder_.SetInsertPoint(loop);
        auto* pop = block("pop");
        builder_.CreateCondBr(builder_.CreateICmpULT(get(queue_head), get(queue_tail)), pop, finish);
        builder_.SetInsertPoint(pop);
        auto* entry_index = get(queue_head);
        set(queue_head, add(entry_index, 1U));
        auto* entry_at = scratch_entry(entry_index);
        auto* run = block("run_thread");
        builder_.CreateCondBr(ne0(rd(entry_at)), loop, run);
        builder_.SetInsertPoint(run);
        for (std::uint64_t word = 0U; word < thread_words; ++word) {
            builder_.CreateStore(rd(add(entry_at, word + 1U)), element(cur, word));
        }
        if (multi) {
            set(now_slot, clock_word(0U, cur_word(1U)));
        }

        // One block per program counter; a thread that runs past the program
        // ends (Matcher::run's loop condition).
        std::vector<llvm::BasicBlock*> pcs;
        for (std::size_t pc = 0U; pc < code.size(); ++pc) {
            pcs.push_back(block("pc"));
        }
        const auto goto_pc = [&](const std::uint64_t pc) {
            builder_.CreateBr(pc < pcs.size() ? pcs[pc] : loop);
        };
        auto* dispatch = builder_.CreateSwitch(cur_word(0U), loop, static_cast<unsigned>(pcs.size()));
        for (std::size_t pc = 0U; pc < pcs.size(); ++pc) {
            dispatch->addCase(llvm::ConstantInt::get(i64t_, pc), pcs[pc]);
        }
        const auto set_cur = [&](const std::uint64_t word, llvm::Value* value) {
            builder_.CreateStore(value, element(cur, word));
        };
        const auto push_copy_at = [&](const std::uint64_t target) {
            scratch_push(0U, [&](const std::uint64_t word) {
                return word == 0U ? k(target) : cur_word(word);
            });
        };
        constexpr std::int64_t unbounded = std::numeric_limits<std::int64_t>::max();
        // The join entries (tag 4) and the end ticks (tag 5) of the scratch log.
        // find_join: the entry index of the join `id`, or -1.
        const auto find_join = [&](llvm::Value* id) -> llvm::Value* {
            auto* found = var("join_found", ~std::uint64_t { 0 });
            auto* index = var("join_scan");
            for_count(index, get(queue_tail), [&] {
                auto* entry_at = scratch_entry(get(index));
                auto* same = builder_.CreateAnd(builder_.CreateICmpEQ(rd(entry_at), k(4U)),
                    builder_.CreateICmpEQ(rd(extra_at(get(index), 0U)), id));
                if_then(same, [&] { set(found, get(index)); });
            });
            return get(found);
        };
        // The end ticks of `side` of join `id`: how many, the earliest, and
        // whether `tick` is one.
        const auto ends_scan = [&](llvm::Value* id, llvm::Value* side, llvm::Value* tick,
                                   llvm::AllocaInst* count, llvm::AllocaInst* earliest,
                                   llvm::AllocaInst* present) {
            auto* index = var("ends_scan");
            for_count(index, get(queue_tail), [&] {
                auto* entry_at = scratch_entry(get(index));
                auto* same = builder_.CreateAnd(
                    builder_.CreateAnd(builder_.CreateICmpEQ(rd(entry_at), k(5U)),
                        builder_.CreateICmpEQ(rd(extra_at(get(index), 0U)), id)),
                    builder_.CreateICmpEQ(rd(extra_at(get(index), 1U)), side));
                if_then(same, [&] {
                    auto* value = rd(add(entry_at, 1U));
                    set(count, add(get(count), 1U));
                    set(earliest, builder_.CreateSelect(
                        builder_.CreateICmpSLT(value, get(earliest)), value, get(earliest)));
                    if (tick != nullptr) {
                        set(present, builder_.CreateOr(get(present),
                            builder_.CreateZExt(builder_.CreateICmpEQ(value, tick), i64_)));
                    }
                });
            });
        };
        const auto ends_count = [&](llvm::Value* id, llvm::Value* side) -> llvm::Value* {
            auto* count = var("ends_n");
            auto* earliest = var("ends_min", static_cast<std::uint64_t>(unbounded));
            auto* present = var("ends_present");
            ends_scan(id, side, nullptr, count, earliest, present);
            return get(count);
        };
        const auto ends_earliest = [&](llvm::Value* id, llvm::Value* side) -> llvm::Value* {
            auto* count = var("ends_n");
            auto* earliest = var("ends_min", static_cast<std::uint64_t>(unbounded));
            auto* present = var("ends_present");
            ends_scan(id, side, nullptr, count, earliest, present);
            return get(earliest);
        };
        const auto ends_has = [&](llvm::Value* id, llvm::Value* side,
                                  llvm::Value* tick) -> llvm::Value* {
            auto* count = var("ends_n");
            auto* earliest = var("ends_min", static_cast<std::uint64_t>(unbounded));
            auto* present = var("ends_present");
            ends_scan(id, side, tick, count, earliest, present);
            return get(present);
        };
        // A delay or repetition bound: a constant, or a word of the block's
        // bounds region (the plan's per-instance constants) plus its offset.
        const auto bound_value = [&](const std::int32_t slot, const std::int64_t offset)
            -> llvm::Value* {
            if (slot < 0) {
                return ks(offset);
            }
            return add(rd(add(rd(l::h_bounds_off), static_cast<std::uint64_t>(slot) + 1U)),
                static_cast<std::uint64_t>(offset));
        };
        // Matcher::settle: the thread ends at this tick (the code goes on with
        // its window moved past it), waits, or dies (the next thread runs).
        const auto settle = [&] {
            auto* due_lo = cur_word(4U);
            auto* due_hi = cur_word(5U);
            auto* end_lo = sub(due_lo, k(1U));
            auto* unbounded_hi = builder_.CreateICmpEQ(due_hi, ks(unbounded));
            auto* end_hi = builder_.CreateSelect(unbounded_hi, ks(unbounded), sub(due_hi, k(1U)));
            auto* after = block("settle_open");
            builder_.CreateCondBr(builder_.CreateICmpSLT(end_hi, now), loop, after);
            builder_.SetInsertPoint(after);
            auto* early = block("settle_early");
            auto* ends = block("settle_ends");
            builder_.CreateCondBr(builder_.CreateICmpSGT(end_lo, now), early, ends);
            builder_.SetInsertPoint(early);
            keep(cur);
            builder_.CreateBr(loop);
            builder_.SetInsertPoint(ends);
            if_then(builder_.CreateICmpSGT(end_hi, now), [&] {
                copy_cur(tmp);
                builder_.CreateStore(add(now, 2U), element(tmp, 4U));
                keep(tmp);
            });
            set_cur(4U, add(now, 1U));
            set_cur(5U, add(now, 1U));
        };
        for (std::size_t pc = 0U; pc < code.size(); ++pc) {
            const auto& step = code[pc];
            builder_.SetInsertPoint(pcs[pc]);
            if (multi) {
                now = get(now_slot);
            }
            const auto pc32 = static_cast<std::uint32_t>(pc);
            switch (step.op) {
            case semantic::sva::Op::check: {
                // Matcher::run's check: the window against the tick, a thread
                // whose window stays open keeps a waiting copy, then the
                // sampled boolean.
                auto* due_lo = cur_word(4U);
                auto* due_hi = cur_word(5U);
                auto* open = block("window_open");
                builder_.CreateCondBr(builder_.CreateICmpSLT(due_hi, now), loop, open);
                builder_.SetInsertPoint(open);
                auto* early = block("window_early");
                auto* due = block("window_due");
                builder_.CreateCondBr(builder_.CreateICmpSGT(due_lo, now), early, due);
                builder_.SetInsertPoint(early);
                keep(cur);
                builder_.CreateBr(loop);
                builder_.SetInsertPoint(due);
                auto* lingers = builder_.CreateICmpSGT(cur_word(5U), now);
                if_then(lingers, [&] {
                    copy_cur(tmp);
                    builder_.CreateStore(add(now, 1U), element(tmp, 4U));
                    keep(tmp);
                });
                auto* sampled = known_one_guard(step.a);
                auto* pass = block("guard_true");
                auto* reject = block("guard_false");
                builder_.CreateCondBr(sampled, pass, reject);
                builder_.SetInsertPoint(reject);
                set(fail_line, k(step.line));
                set(fail_column, k(step.column));
                if_then(builder_.CreateNot(lingers), [&] {
                    note_step(env, true, sequence, pc32, pc32 + 1U, k(step.a), now);
                });
                builder_.CreateBr(loop);
                builder_.SetInsertPoint(pass);
                if (shape.throughout != 0U) {
                    // The conditions of the enclosing throughout operators are
                    // first checked at the operand's first boolean.
                    auto* index = var("start_entry");
                    auto* count = cur_word(off_nt);
                    auto* head = block("entry_head");
                    auto* body = block("entry_body");
                    auto* done = block("entry_done");
                    set(index, k(0U));
                    builder_.CreateBr(head);
                    builder_.SetInsertPoint(head);
                    builder_.CreateCondBr(builder_.CreateICmpULT(get(index), count), body, done);
                    builder_.SetInsertPoint(body);
                    auto* slot = add(mul(get(index), 4U), off_nt + 1U);
                    auto* from_at = cur_at(add(slot, 1U));
                    auto* next_entry = block("entry_next");
                    auto* unstarted = block("entry_unstarted");
                    builder_.CreateCondBr(
                        builder_.CreateICmpSLT(builder_.CreateLoad(i64_, from_at), ks(0)),
                        unstarted, next_entry);
                    builder_.SetInsertPoint(unstarted);
                    builder_.CreateStore(now, from_at);
                    auto* guard = builder_.CreateLoad(i64_, cur_at(slot));
                    auto* broken = block("entry_broken");
                    builder_.CreateCondBr(known_one_dynamic(guard), next_entry, broken);
                    builder_.SetInsertPoint(broken);
                    set(fail_line, builder_.CreateLoad(i64_, cur_at(add(slot, 2U))));
                    set(fail_column, builder_.CreateLoad(i64_, cur_at(add(slot, 3U))));
                    if_then(builder_.CreateNot(lingers), [&] {
                        note_step(env, true, sequence, pc32, pc32 + 1U, guard, now);
                    });
                    builder_.CreateBr(loop);
                    builder_.SetInsertPoint(next_entry);
                    set(index, add(get(index), 1U));
                    builder_.CreateBr(head);
                    builder_.SetInsertPoint(done);
                }
                note_step(env, false, sequence, pc32, pc32 + 1U, k(step.a), now);
                set_cur(4U, add(now, 1U));
                set_cur(5U, add(now, 1U));
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::delay: {
                set_cur(4U, add(cur_word(4U), sub(bound_value(step.lo_slot, step.lo), k(1U))));
                if (step.hi_slot < 0 && step.hi == unbounded) {
                    set_cur(5U, ks(unbounded));
                } else {
                    auto* due_hi = cur_word(5U);
                    set_cur(5U, builder_.CreateSelect(
                        builder_.CreateICmpEQ(due_hi, ks(unbounded)), ks(unbounded),
                        add(due_hi, sub(bound_value(step.hi_slot, step.hi), k(1U)))));
                }
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::fork:
                push_copy_at(step.a);
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            case semantic::sva::Op::jump:
                set_cur(0U, k(step.a));
                goto_pc(step.a);
                break;
            case semantic::sva::Op::counter_set:
                set_cur(7U + step.a, k(0U));
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            case semantic::sva::Op::counter_branch: {
                // A loop that returns with an unchanged state matched nothing
                // new (visited_): the thread's key words are the key.
                auto* seen = var("seen");
                auto* index = var("visit_scan");
                for_count(index, get(queue_tail), [&] {
                    set(seen, builder_.CreateOr(get(seen),
                        builder_.CreateZExt(
                            same_key(1U, scratch_entry(get(index)),
                                [&](const std::uint64_t word) -> llvm::Value* {
                                    return cur_word(word);
                                }),
                            i64_)));
                });
                auto* fresh = block("branch_fresh");
                builder_.CreateCondBr(ne0(get(seen)), loop, fresh);
                builder_.SetInsertPoint(fresh);
                scratch_push(1U, [&](const std::uint64_t word) { return cur_word(word); });
                auto* count = cur_word(7U + step.a);
                auto* leave = builder_.CreateICmpSGE(count, bound_value(step.lo_slot, step.lo));
                auto* repeat = step.hi_slot < 0 && step.hi == unbounded
                    ? static_cast<llvm::Value*>(llvm::ConstantInt::getTrue(context_))
                    : builder_.CreateICmpSLT(count, bound_value(step.hi_slot, step.hi));
                auto* both = block("branch_both");
                auto* only = block("branch_one");
                builder_.CreateCondBr(builder_.CreateAnd(leave, repeat), both, only);
                builder_.SetInsertPoint(both);
                push_copy_at(step.b);
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                builder_.SetInsertPoint(only);
                auto* leave_only = block("branch_leave");
                auto* not_leave = block("branch_stay");
                builder_.CreateCondBr(leave, leave_only, not_leave);
                builder_.SetInsertPoint(leave_only);
                set_cur(0U, k(step.b));
                goto_pc(step.b);
                builder_.SetInsertPoint(not_leave);
                auto* repeat_only = block("branch_repeat");
                builder_.CreateCondBr(repeat, repeat_only, loop);
                builder_.SetInsertPoint(repeat_only);
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::counter_increment: {
                llvm::Value* count = add(cur_word(7U + step.a), 1U);
                if (step.hi_slot < 0 && step.hi == unbounded) {
                    auto* low = bound_value(step.lo_slot, step.lo);
                    auto* cap = builder_.CreateSelect(
                        builder_.CreateICmpSGT(low, ks(1)), low, ks(1));
                    count = builder_.CreateSelect(builder_.CreateICmpSLT(count, cap), count, cap);
                }
                set_cur(7U + step.a, count);
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::match:
                settle();
                set(matched, k(1U));
                builder_.CreateBr(loop);
                break;
            case semantic::sva::Op::throughout_begin: {
                // `a throughout s`: the condition joins the thread, unstarted.
                auto* used = cur_word(off_nt);
                auto* slot = add(mul(used, 4U), off_nt + 1U);
                builder_.CreateStore(k(step.a), cur_at(slot));
                builder_.CreateStore(ks(-1), cur_at(add(slot, 1U)));
                builder_.CreateStore(k(step.line), cur_at(add(slot, 2U)));
                builder_.CreateStore(k(step.column), cur_at(add(slot, 3U)));
                set_cur(off_nt, add(used, 1U));
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::throughout_end: {
                settle();
                auto* used = cur_word(off_nt);
                if_then(ne0(used), [&] {
                    auto* slot = add(mul(sub(used, k(1U)), 4U), off_nt + 1U);
                    for (std::uint64_t part = 0U; part < 4U; ++part) {
                        builder_.CreateStore(k(0U), cur_at(add(slot, part)));
                    }
                    set_cur(off_nt, sub(used, k(1U)));
                });
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::first_match_begin: {
                auto* id = get(next_group);
                set(next_group, add(id, 1U));
                auto* used = cur_word(off_ng);
                builder_.CreateStore(id, cur_at(add(used, off_ng + 1U)));
                set_cur(off_ng, add(used, 1U));
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::first_match_end: {
                // The earliest match ends the group: the threads of the group
                // still waiting are dropped at the end of the step.
                settle();
                auto* used = cur_word(off_ng);
                if_then(ne0(used), [&] {
                    auto* slot = add(used, off_ng);
                    auto* id = builder_.CreateLoad(i64_, cur_at(slot));
                    scratch_push(3U, [&](const std::uint64_t word) -> llvm::Value* {
                        return word == 0U ? id : k(0U);
                    });
                    builder_.CreateStore(k(0U), cur_at(slot));
                    set_cur(off_ng, sub(used, k(1U)));
                });
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::join_begin: {
                // `and` (a == 0) or `intersect` (a == 1): the join keeps the
                // thread as its continuation (at c); the right operand runs
                // from b with a reference to side 1, the left one on.
                auto* id = get(next_join);
                set(next_join, add(id, 1U));
                scratch_push(4U,
                    [&](const std::uint64_t word) {
                        return word == 0U ? k(step.c) : cur_word(word);
                    },
                    std::array<llvm::Value*, 5> { id, k(step.a), k(0U), k(0U), k(1U) });
                auto* used = cur_word(off_jr);
                auto* slot = add(mul(used, 2U), off_jr + 1U);
                copy_cur(tmp);
                builder_.CreateStore(k(step.b), element(tmp, 0U));
                builder_.CreateStore(id, builder_.CreateInBoundsGEP(
                    tmp->getAllocatedType(), tmp, { k(0U), slot }));
                builder_.CreateStore(k(1U), builder_.CreateInBoundsGEP(
                    tmp->getAllocatedType(), tmp, { k(0U), add(slot, 1U) }));
                builder_.CreateStore(add(used, 1U), element(tmp, off_jr));
                scratch_push(0U, [&](const std::uint64_t word) {
                    return builder_.CreateLoad(i64_, element(tmp, word));
                });
                builder_.CreateStore(id, cur_at(slot));
                builder_.CreateStore(k(0U), cur_at(add(slot, 1U)));
                set_cur(off_jr, add(used, 1U));
                set_cur(0U, k(pc + 1U));
                goto_pc(pc + 1U);
                break;
            }
            case semantic::sva::Op::join_end: {
                // The operand ended at this tick: its end joins the side's
                // set, and the continuation runs when the join pairs (`and`:
                // the other side ended at or before now; `intersect`: at now).
                settle();
                auto* used = cur_word(off_jr);
                auto* has_reference = block("join_reference");
                builder_.CreateCondBr(ne0(used), has_reference, loop);
                builder_.SetInsertPoint(has_reference);
                auto* last = add(mul(sub(used, k(1U)), 2U), off_jr + 1U);
                auto* id = builder_.CreateLoad(i64_, cur_at(last));
                auto* side = builder_.CreateLoad(i64_, cur_at(add(last, 1U)));
                auto* join = find_join(id);
                auto* exists = block("join_exists");
                builder_.CreateCondBr(builder_.CreateICmpNE(join, ks(-1)), exists, loop);
                builder_.SetInsertPoint(exists);
                if_then(builder_.CreateICmpEQ(ends_has(id, side, now), k(0U)), [&] {
                    scratch_push(5U,
                        [&](const std::uint64_t word) -> llvm::Value* {
                            return word == 0U ? now : k(0U);
                        },
                        std::array<llvm::Value*, 5> { id, side, nullptr, nullptr, nullptr });
                });
                auto* other = builder_.CreateXor(side, k(1U));
                auto* kind = rd(extra_at(join, 1U));
                auto* earliest = ends_earliest(id, other);
                auto* other_count = ends_count(id, other);
                auto* paired = builder_.CreateSelect(builder_.CreateICmpEQ(kind, k(0U)),
                    builder_.CreateAnd(ne0(other_count), builder_.CreateICmpSLE(earliest, now)),
                    ne0(ends_has(id, other, now)));
                if_then(paired, [&] {
                    scratch_push(0U, [&](const std::uint64_t word) -> llvm::Value* {
                        return word == 4U || word == 5U
                            ? add(now, 1U)
                            : rd(add(add(scratch_entry(join), 1U), word));
                    });
                });
                builder_.CreateBr(loop);
                break;
            }
            case semantic::sva::Op::clock: {
                // The operand before ended at this tick; the rest starts on
                // the clock's first tick strictly after this time step
                // (`##1`) or at or after it (`##0`) (16.13.1).
                const bool inclusive = step.lo == 0;
                set_cur(1U, k(step.a));
                set_cur(0U, k(pc + 1U));
                auto* wait = block("clock_wait");
                if (inclusive) {
                    auto* hit = block("clock_now");
                    builder_.CreateCondBr(
                        builder_.CreateICmpEQ(clock_word(1U, k(step.a)), time_now), hit, wait);
                    builder_.SetInsertPoint(hit);
                    auto* ticks = clock_word(0U, k(step.a));
                    set_cur(4U, ticks);
                    set_cur(5U, ticks);
                    set(now_slot, ticks);
                    goto_pc(pc + 1U);
                } else {
                    builder_.CreateBr(wait);
                }
                builder_.SetInsertPoint(wait);
                set_cur(2U, k(inclusive ? 3U : 1U));
                set_cur(3U, time_now);
                keep(cur);
                builder_.CreateBr(loop);
                break;
            }
            }
        }
        now = entry_now;

        builder_.SetInsertPoint(finish);
        auto* final_count = var("final_threads");
        set(final_count, get(out_count));
        if (extended) {
            // Matcher::step's end: the kept threads of a matched first_match
            // group are dropped (tag 6), then collect_joins prunes the joins
            // no thread can reach and the threads of the pruned ones; the
            // rest is encoded (Matcher::encode).
            auto* index = var("kept_thread");
            for_count(index, get(queue_tail), [&] {
                auto* is_kept = builder_.CreateICmpEQ(rd(scratch_entry(get(index))), k(2U));
                if_then(is_kept, [&] {
                    auto* dropped = var("dropped");
                    auto* groups = rd(add(scratch_entry(get(index)), off_ng + 1U));
                    auto* group = var("group");
                    for_count(group, groups, [&] {
                        auto* id = rd(add(add(scratch_entry(get(index)), off_ng + 2U), get(group)));
                        auto* matched_index = var("matched_group");
                        for_count(matched_index, get(queue_tail), [&] {
                            auto* other = scratch_entry(get(matched_index));
                            auto* same = builder_.CreateAnd(
                                builder_.CreateICmpEQ(rd(other), k(3U)),
                                builder_.CreateICmpEQ(rd(add(other, 1U)), id));
                            if_then(same, [&] { set(dropped, k(1U)); });
                        });
                    });
                    if_then(ne0(get(dropped)), [&] { wr(scratch_entry(get(index)), k(6U)); });
                });
            });
            if (has_joins) {
                // collect_joins: a side is live when a kept thread refers to
                // it; a join is productive when that, or the ends already
                // seen, can still pair it; a productive join's continuation
                // keeps its own references live.
                const auto mark = [&](llvm::Value* entry_index, auto& changed) {
                    auto* refs = rd(add(scratch_entry(entry_index), off_jr + 1U));
                    auto* ref = var("mark_ref");
                    for_count(ref, refs, [&] {
                        auto* base = add(scratch_entry(entry_index), off_jr + 2U);
                        auto* id = rd(add(base, mul(get(ref), 2U)));
                        auto* side = rd(add(base, add(mul(get(ref), 2U), 1U)));
                        auto* join = find_join(id);
                        if_then(builder_.CreateICmpNE(join, ks(-1)), [&] {
                            auto* live = add(extra_at(join, 2U), side);
                            if_then(builder_.CreateICmpEQ(rd(live), k(0U)), [&] {
                                wr(live, k(1U));
                                set(changed, k(1U));
                            });
                        });
                    });
                };
                const auto productive = [&](llvm::Value* join) -> llvm::Value* {
                    auto* id = rd(extra_at(join, 0U));
                    auto* kind = rd(extra_at(join, 1U));
                    auto* live0 = ne0(rd(extra_at(join, 2U)));
                    auto* live1 = ne0(rd(extra_at(join, 3U)));
                    auto* ends0 = ne0(ends_count(id, k(0U)));
                    auto* ends1 = ne0(ends_count(id, k(1U)));
                    auto* both = builder_.CreateAnd(live0, live1);
                    auto* conjunction = builder_.CreateAnd(
                        builder_.CreateAnd(builder_.CreateOr(live0, live1),
                            builder_.CreateOr(live0, ends0)),
                        builder_.CreateOr(live1, ends1));
                    return builder_.CreateSelect(builder_.CreateICmpEQ(kind, k(0U)),
                        conjunction, both);
                };
                auto* changed = var("collect_changed", 1U);
                auto* mark_index = var("mark_thread");
                for_count(mark_index, get(queue_tail), [&] {
                    if_then(builder_.CreateICmpEQ(rd(scratch_entry(get(mark_index))), k(2U)),
                        [&] { mark(get(mark_index), changed); });
                });
                auto* head = block("collect");
                auto* body = block("collect_pass");
                auto* done = block("collected");
                builder_.CreateBr(head);
                builder_.SetInsertPoint(head);
                builder_.CreateCondBr(ne0(get(changed)), body, done);
                builder_.SetInsertPoint(body);
                set(changed, k(0U));
                auto* join_index = var("collect_join");
                for_count(join_index, get(queue_tail), [&] {
                    auto* is_join = builder_.CreateICmpEQ(
                        rd(scratch_entry(get(join_index))), k(4U));
                    if_then(is_join, [&] {
                        if_then(productive(get(join_index)), [&] {
                            mark(get(join_index), changed);
                        });
                    });
                });
                builder_.CreateBr(head);
                builder_.SetInsertPoint(done);
                auto* alive_index = var("alive_join");
                for_count(alive_index, get(queue_tail), [&] {
                    if_then(builder_.CreateICmpEQ(
                                rd(scratch_entry(get(alive_index))), k(4U)),
                        [&] {
                            wr(extra_at(get(alive_index), 4U),
                                builder_.CreateZExt(productive(get(alive_index)), i64_));
                        });
                });
                // The threads that refer to a pruned join do not survive.
                auto* thread_index = var("pruned_thread");
                for_count(thread_index, get(queue_tail), [&] {
                    if_then(builder_.CreateICmpEQ(
                                rd(scratch_entry(get(thread_index))), k(2U)),
                        [&] {
                            auto* lost = var("lost");
                            auto* refs = rd(add(scratch_entry(get(thread_index)), off_jr + 1U));
                            auto* ref = var("pruned_ref");
                            for_count(ref, refs, [&] {
                                auto* id = rd(add(add(scratch_entry(get(thread_index)),
                                                       off_jr + 2U),
                                    mul(get(ref), 2U)));
                                auto* join = find_join(id);
                                auto* missing = builder_.CreateICmpEQ(join, ks(-1));
                                auto* join_at = builder_.CreateSelect(missing, k(0U), join);
                                auto* dead = builder_.CreateOr(missing,
                                    builder_.CreateICmpEQ(rd(extra_at(join_at, 4U)), k(0U)));
                                if_then(dead, [&] { set(lost, k(1U)); });
                            });
                            if_then(ne0(get(lost)), [&] {
                                wr(scratch_entry(get(thread_index)), k(6U));
                            });
                        });
                });
            }
            // Matcher::encode_thread of the thread whose words follow the tag
            // of scratch entry `entry_index`.
            const auto encode_thread = [&](llvm::Value* entry_index) {
                reserve_alt(thread_words);
                auto* at = add(scratch_entry(entry_index), 1U);
                auto* position = rd(l::h_heap_alt_used);
                for (std::uint64_t word = 0U; word < off_nt; ++word) {
                    alt_put(add(position, word), rd(add(at, word)));
                }
                auto* used = rd(add(at, off_nt));
                alt_put(add(position, off_nt), used);
                auto* entry_slot = var("encode_entry");
                for_count(entry_slot, used, [&] {
                    for (std::uint64_t part = 0U; part < 4U; ++part) {
                        auto* slot = add(mul(get(entry_slot), 4U), off_nt + 1U + part);
                        alt_put(add(position, slot), rd(add(at, slot)));
                    }
                });
                auto* groups_at = add(add(position, off_nt + 1U), mul(used, 4U));
                auto* group_count = rd(add(at, off_ng));
                alt_put(groups_at, group_count);
                auto* group_slot = var("encode_group");
                for_count(group_slot, group_count, [&] {
                    alt_put(add(add(groups_at, 1U), get(group_slot)),
                        rd(add(add(at, off_ng + 1U), get(group_slot))));
                });
                auto* refs_at = add(add(groups_at, group_count), 1U);
                auto* ref_count = rd(add(at, off_jr));
                alt_put(refs_at, ref_count);
                auto* ref_slot = var("encode_ref");
                for_count(ref_slot, mul(ref_count, 2U), [&] {
                    alt_put(add(add(refs_at, 1U), get(ref_slot)),
                        rd(add(add(at, off_jr + 1U), get(ref_slot))));
                });
                wr(l::h_heap_alt_used, add(add(refs_at, 1U), mul(ref_count, 2U)));
            };
            set(final_count, k(0U));
            auto* encode_index = var("encode_thread");
            for_count(encode_index, get(queue_tail), [&] {
                if_then(builder_.CreateICmpEQ(rd(scratch_entry(get(encode_index))), k(2U)), [&] {
                    encode_thread(get(encode_index));
                    set(final_count, add(get(final_count), 1U));
                });
            });
            if (has_joins) {
                // The joins that survive, in the order of their ids: id, kind,
                // the continuation thread, then each side's end ticks in
                // ascending order.
                auto* alive_count = var("alive_joins");
                auto* count_index = var("count_join");
                for_count(count_index, get(queue_tail), [&] {
                    auto* is_alive = builder_.CreateAnd(
                        builder_.CreateICmpEQ(rd(scratch_entry(get(count_index))), k(4U)),
                        ne0(rd(extra_at(get(count_index), 4U))));
                    if_then(is_alive, [&] { set(alive_count, add(get(alive_count), 1U)); });
                });
                reserve_alt(1U);
                auto* joins_at = rd(l::h_heap_alt_used);
                alt_put(joins_at, get(alive_count));
                wr(l::h_heap_alt_used, add(joins_at, 1U));
                auto* emit_index = var("emit_join");
                for_count(emit_index, get(queue_tail), [&] {
                    auto* is_alive = builder_.CreateAnd(
                        builder_.CreateICmpEQ(rd(scratch_entry(get(emit_index))), k(4U)),
                        ne0(rd(extra_at(get(emit_index), 4U))));
                    if_then(is_alive, [&] {
                        auto* id = rd(extra_at(get(emit_index), 0U));
                        reserve_alt(2U);
                        auto* head_at = rd(l::h_heap_alt_used);
                        alt_put(head_at, id);
                        alt_put(add(head_at, 1U), rd(extra_at(get(emit_index), 1U)));
                        wr(l::h_heap_alt_used, add(head_at, 2U));
                        encode_thread(get(emit_index));
                        for (std::uint64_t side = 0U; side < 2U; ++side) {
                            auto* count = ends_count(id, k(side));
                            reserve_alt(add(count, 1U));
                            auto* ends_at = rd(l::h_heap_alt_used);
                            alt_put(ends_at, count);
                            auto* last = var("last_end", std::uint64_t { 1 } << 63U);
                            auto* ordinal = var("end_ordinal");
                            for_count(ordinal, count, [&] {
                                auto* best = var("best_end", static_cast<std::uint64_t>(unbounded));
                                auto* scan = var("end_scan");
                                for_count(scan, get(queue_tail), [&] {
                                    auto* entry_at = scratch_entry(get(scan));
                                    auto* same = builder_.CreateAnd(
                                        builder_.CreateAnd(
                                            builder_.CreateICmpEQ(rd(entry_at), k(5U)),
                                            builder_.CreateICmpEQ(
                                                rd(extra_at(get(scan), 0U)), id)),
                                        builder_.CreateICmpEQ(
                                            rd(extra_at(get(scan), 1U)), k(side)));
                                    if_then(same, [&] {
                                        auto* value = rd(add(entry_at, 1U));
                                        auto* later = builder_.CreateAnd(
                                            builder_.CreateICmpSGT(value, get(last)),
                                            builder_.CreateICmpSLT(value, get(best)));
                                        if_then(later, [&] { set(best, value); });
                                    });
                                });
                                alt_put(add(add(ends_at, 1U), get(ordinal)), get(best));
                                set(last, get(best));
                            });
                            wr(l::h_heap_alt_used, add(add(ends_at, count), 1U));
                        }
                    });
                });
            }
        }
        alt_put(get(record_start), get(next_join));
        alt_put(add(get(record_start), 1U), get(next_group));
        alt_put(add(get(record_start), 4U), get(final_count));
        alt_put(add(get(record_start), 2U), get(fail_line));
        alt_put(add(get(record_start), 3U), get(fail_column));
        if (!has_joins) {
            reserve_alt(1U);
            auto* tail = rd(l::h_heap_alt_used);
            alt_put(tail, k(0U));
            wr(l::h_heap_alt_used, add(tail, 1U));
        }
        wr(l::h_scratch_used, k(0U));
        builder_.CreateStore(get(fail_line), builder_.CreateConstInBoundsGEP1_64(i64_, env, 4U));
        builder_.CreateStore(get(fail_column), builder_.CreateConstInBoundsGEP1_64(i64_, env, 5U));
        builder_.CreateRet(builder_.CreateOr(get(matched),
            builder_.CreateShl(builder_.CreateZExt(builder_.CreateICmpEQ(get(final_count), k(0U)), i64_),
                k(1U))));

        fn_ = saved_fn;
        frame_ = saved_frame;
        registers_ = saved_registers;
        slot_addr_ = saved_slot;
        failure_ = saved_failure;
        builder_.restoreIP(saved_point);
        return matcher;
    }

    // ---- property trees of fixed operands (Instance::step) ----
    //
    // not, and, or, implies, iff, if/else, nexttime, accept_on and reject_on
    // over sequences: one instance per node, so the code of the tree is
    // emitted node by node. An instance's record is its header, its matcher
    // when it is a sequence that has started, and its operands (position,
    // instance) in creation order; a decided instance is its header alone.

    struct NodeState {
        llvm::AllocaInst* decided { };
        llvm::AllocaInst* holds { };
        llvm::AllocaInst* vacuous { };
        llvm::AllocaInst* line { };
        llvm::AllocaInst* column { };
    };

    [[nodiscard]] static bool tree_node(const semantic::sva::PropertyKind kind) noexcept
    {
        switch (kind) {
        case semantic::sva::PropertyKind::negation:
        case semantic::sva::PropertyKind::conjunction:
        case semantic::sva::PropertyKind::disjunction:
        case semantic::sva::PropertyKind::equivalence:
        case semantic::sva::PropertyKind::implies:
        case semantic::sva::PropertyKind::conditional:
        case semantic::sva::PropertyKind::nexttime:
        case semantic::sva::PropertyKind::accept:
        case semantic::sva::PropertyKind::reject:
        case semantic::sva::PropertyKind::vacuous:
        case semantic::sva::PropertyKind::always:
        case semantic::sva::PropertyKind::eventually:
        case semantic::sva::PropertyKind::until:
        case semantic::sva::PropertyKind::implication:
        case semantic::sva::PropertyKind::followed_by:
            return true;
        default:
            return false;
        }
    }

    /// Whether the root keeps the code of its own shape (a sequence, `not` of
    /// a sequence, an implication of a sequence or vacuous consequent) rather
    /// than the property tree's.
    [[nodiscard]] bool plain_root() const
    {
        const auto& root = plan_.properties[plan_.root];
        const auto kind_of = [&](const std::int32_t child) {
            return plan_.properties[static_cast<std::size_t>(child)].kind;
        };
        switch (root.kind) {
        case semantic::sva::PropertyKind::sequence:
            return true;
        case semantic::sva::PropertyKind::negation:
            return kind_of(root.child) == semantic::sva::PropertyKind::sequence;
        case semantic::sva::PropertyKind::implication:
        case semantic::sva::PropertyKind::followed_by:
            return kind_of(root.child) == semantic::sva::PropertyKind::sequence
                || kind_of(root.child) == semantic::sva::PropertyKind::vacuous;
        default:
            return false;
        }
    }

    void tree_sequences(const std::uint32_t node, std::vector<std::uint32_t>& out) const
    {
        const auto& property = plan_.properties[node];
        if (property.kind == semantic::sva::PropertyKind::sequence
            || property.kind == semantic::sva::PropertyKind::implication
            || property.kind == semantic::sva::PropertyKind::followed_by) {
            out.push_back(property.sequence);
        }
        for (const auto child : property.children) {
            tree_sequences(static_cast<std::uint32_t>(child), out);
        }
    }

    /// The matcher call of `sequence` (see matcher_function).
    [[nodiscard]] llvm::Value* call_matcher(const std::uint32_t sequence, llvm::Value* in_off,
        llvm::Value* matcher_start)
    {
        auto* function = matchers_.at(sequence);
        auto* result = builder_.CreateCall(function->getFunctionType(), function,
            { frame_, registers_, slot_addr_, env_, in_off, matcher_start });
        check_status();
        return result;
    }

    /// Appends one word to the alternate heap's record.
    void put_word(llvm::Value* value)
    {
        reserve_alt(1U);
        auto* at = rd(runtime::sva::layout::h_heap_alt_used);
        alt_put(at, value);
        wr(runtime::sva::layout::h_heap_alt_used, add(at, 1U));
    }

    /// The end of the input record of `node` at `in` of the current heap
    /// (`in` itself for a node that does not exist yet): a decided instance
    /// is its header, a sequence its matcher, the others their operands.
    [[nodiscard]] llvm::Value* skip_node(const std::uint32_t node, llvm::Value* in,
        llvm::Value* existing)
    {
        const auto& property = plan_.properties[node];
        auto* result = var("skipped");
        set(result, in);
        if_then(ne0(existing), [&] {
            auto* flags = old_word(add(in, 2U));
            set(result, add(in, 10U));
            if_then(builder_.CreateICmpEQ(builder_.CreateAnd(flags, k(3U)), k(0U)), [&] {
                auto* operands = old_word(add(in, 7U));
                switch (property.kind) {
                case semantic::sva::PropertyKind::sequence: {
                    auto* present = builder_.CreateAnd(builder_.CreateLShr(flags, k(7U)), k(1U));
                    if_then(ne0(present), [&] {
                        set(result, add(add(in, 10U),
                            matcher_record_length(present, add(in, 10U), property.sequence)));
                    });
                    break;
                }
                case semantic::sva::PropertyKind::negation:
                case semantic::sva::PropertyKind::nexttime:
                case semantic::sva::PropertyKind::accept:
                case semantic::sva::PropertyKind::reject:
                    if_then(ne0(operands), [&] {
                        set(result, skip_node(static_cast<std::uint32_t>(property.children[0]),
                            add(in, 11U), k(1U)));
                    });
                    break;
                case semantic::sva::PropertyKind::conjunction:
                case semantic::sva::PropertyKind::disjunction:
                case semantic::sva::PropertyKind::equivalence:
                case semantic::sva::PropertyKind::implies:
                    if_then(ne0(operands), [&] {
                        auto* first = skip_node(static_cast<std::uint32_t>(property.children[0]),
                            add(in, 11U), k(1U));
                        set(result, skip_node(static_cast<std::uint32_t>(property.children[1]),
                            add(first, 1U), k(1U)));
                    });
                    break;
                case semantic::sva::PropertyKind::always:
                case semantic::sva::PropertyKind::eventually: {
                    auto* position = var("skip_list");
                    set(position, add(in, 10U));
                    auto* index = var("skip_started");
                    for_count(index, old_word(add(in, 8U)), [&] {
                        set(position, skip_node(static_cast<std::uint32_t>(property.children[0]),
                            get(position), k(1U)));
                    });
                    set(result, get(position));
                    break;
                }
                case semantic::sva::PropertyKind::implication:
                case semantic::sva::PropertyKind::followed_by: {
                    auto* present = builder_.CreateAnd(builder_.CreateLShr(flags, k(7U)), k(1U));
                    auto* position = var("skip_list");
                    set(position, builder_.CreateSelect(ne0(present),
                        add(add(in, 10U), matcher_record_length(present, add(in, 10U),
                                              property.sequence)),
                        add(in, 10U)));
                    auto* index = var("skip_consequent");
                    for_count(index, old_word(add(in, 8U)), [&] {
                        set(position, skip_node(static_cast<std::uint32_t>(property.children[0]),
                            get(position), k(1U)));
                    });
                    set(result, get(position));
                    break;
                }
                case semantic::sva::PropertyKind::until: {
                    auto* position = var("skip_list");
                    set(position, add(in, 10U));
                    auto* index = var("skip_until");
                    for_count(index, old_word(add(in, 9U)), [&] {
                        auto* right = skip_node(static_cast<std::uint32_t>(property.children[0]),
                            get(position), k(1U));
                        set(position, skip_node(static_cast<std::uint32_t>(property.children[1]),
                            right, k(1U)));
                    });
                    set(result, get(position));
                    break;
                }
                case semantic::sva::PropertyKind::conditional:
                    if_then(ne0(operands), [&] {
                        auto* chosen = old_word(add(in, 3U));
                        for (std::size_t branch = 0U; branch < property.children.size(); ++branch) {
                            if_then(builder_.CreateICmpEQ(chosen, k(branch)), [&] {
                                set(result, skip_node(
                                    static_cast<std::uint32_t>(property.children[branch]),
                                    add(in, 11U), k(1U)));
                            });
                        }
                    });
                    break;
                default:
                    break;
                }
            });
        });
        return get(result);
    }

    /// Instance::step of `node` at tick tick_ for the instance whose input
    /// record is at `in` (`existing` an i64 0 or 1; a new instance starts at
    /// `start`). Its record is appended to the alternate heap (a decided one
    /// is its header alone); the state says whether it decided, and how.
    [[nodiscard]] NodeState emit_node(const std::uint32_t node, llvm::Value* in,
        llvm::Value* existing, llvm::Value* start)
    {
        namespace l = runtime::sva::layout;
        const auto& property = plan_.properties[node];
        NodeState state;
        state.decided = var("node_decided");
        state.holds = var("node_holds");
        state.vacuous = var("node_vacuous");
        state.line = var("node_line");
        state.column = var("node_column");
        auto* chosen_bit = var("node_chosen_bit");
        auto* chosen_index = var("node_chosen");
        auto* has_matcher = var("node_matcher");
        auto* operands = var("node_operands");
        auto* listed = var("node_started");
        auto* until_count = var("node_until");
        auto* until_bit = var("node_until_stopped");
        auto* aux_flags = var("node_aux_flags");
        auto* consequent_count = var("node_consequents");
        auto* now = tick_;
        auto* flags_in = guarded_old(existing, add(in, 2U));
        // The operands of an instance that has not stepped yet (a consequent
        // that starts after this tick) do not exist: they are created at its
        // first step (Instance::child).
        auto* operands_existing = builder_.CreateZExt(
            ne0(guarded_old(existing, add(in, 7U))), i64_);
        auto* node_start = builder_.CreateSelect(ne0(existing),
            guarded_old(existing, add(in, 1U)), start);
        auto* record = var("node_record");
        reserve_alt(10U);
        set(record, rd(l::h_heap_alt_used));
        auto* at = get(record);
        alt_put(at, k(node));
        alt_put(add(at, 1U), node_start);
        for (std::uint64_t word = 2U; word < 10U; ++word) {
            alt_put(add(at, word), k(0U));
        }
        wr(l::h_heap_alt_used, add(at, 10U));
        auto* verdict = builder_.CreateAnd(flags_in, k(3U));
        auto* own_line = k(property.line);
        auto* own_column = k(property.column);
        const auto resolve = [&](llvm::Value* holds, llvm::Value* vacuous) {
            set(state.decided, k(1U));
            set(state.holds, holds);
            set(state.vacuous, builder_.CreateAnd(holds, vacuous));
        };
        const auto fail_at = [&](llvm::Value* line, llvm::Value* column) {
            set(state.line, line);
            set(state.column, column);
        };
        const auto child_state = [&](const NodeState& child) {
            fail_at(get(child.line), get(child.column));
            resolve(get(child.holds), get(child.vacuous));
        };
        // One operand: its position word, then its instance.
        const auto operand = [&](const std::uint32_t index, llvm::Value* child_in,
                                 llvm::Value* child_existing, llvm::Value* child_start) {
            put_word(k(index));
            return emit_node(static_cast<std::uint32_t>(property.children[index]), child_in,
                child_existing, child_start);
        };
        const auto step = [&] {
            switch (property.kind) {
            case semantic::sva::PropertyKind::sequence: {
                auto* present = builder_.CreateAnd(builder_.CreateLShr(flags_in, k(7U)), k(1U));
                auto* in_off = builder_.CreateSelect(ne0(present), add(in, 10U), ks(-1));
                auto* result = call_matcher(property.sequence, in_off, node_start);
                set(has_matcher, k(1U));
                auto* matched = builder_.CreateAnd(result, k(1U));
                auto* dead = builder_.CreateAnd(builder_.CreateLShr(result, k(1U)), k(1U));
                auto* env_line = builder_.CreateLoad(i64_,
                    builder_.CreateConstInBoundsGEP1_64(i64_, env_, 4U));
                auto* env_column = builder_.CreateLoad(i64_,
                    builder_.CreateConstInBoundsGEP1_64(i64_, env_, 5U));
                if_else(ne0(matched), [&] {
                    resolve(k(1U), k(0U));
                }, [&] {
                    if_then(ne0(dead), [&] {
                        fail_at(env_line, env_column);
                        resolve(k(0U), k(0U));
                    });
                });
                break;
            }
            case semantic::sva::PropertyKind::vacuous:
                resolve(k(1U), k(1U));
                break;
            case semantic::sva::PropertyKind::negation:
            case semantic::sva::PropertyKind::nexttime: {
                llvm::Value* child_start = node_start;
                if (property.kind == semantic::sva::PropertyKind::nexttime) {
                    // child(0, add(start_, lo)): the operand starts lo ticks on.
                    llvm::Value* offset = ks(property.lo);
                    if (property.lo_slot >= 0) {
                        offset = add(rd(add(rd(l::h_bounds_off),
                                          static_cast<std::uint64_t>(property.lo_slot) + 1U)),
                            static_cast<std::uint64_t>(property.lo));
                    }
                    child_start = add(node_start, offset);
                }
                set(operands, k(1U));
                const auto child = operand(0U, add(in, 11U), operands_existing, child_start);
                if_then(ne0(get(child.decided)), [&] {
                    if (property.kind == semantic::sva::PropertyKind::negation) {
                        // `not p` fails because p held: the property itself
                        // is the failed node.
                        fail_at(own_line, own_column);
                        resolve(builder_.CreateXor(get(child.holds), k(1U)),
                            get(child.vacuous));
                    } else {
                        child_state(child);
                    }
                });
                break;
            }
            case semantic::sva::PropertyKind::accept:
            case semantic::sva::PropertyKind::reject: {
                const bool accept = property.kind == semantic::sva::PropertyKind::accept;
                if_else(known_one_guard(property.guard), [&] {
                    fail_at(own_line, own_column);
                    resolve(k(accept ? 1U : 0U), k(accept ? 1U : 0U));
                }, [&] {
                    set(operands, k(1U));
                    const auto child = operand(0U, add(in, 11U), operands_existing, node_start);
                    if_then(ne0(get(child.decided)), [&] { child_state(child); });
                });
                break;
            }
            case semantic::sva::PropertyKind::conjunction:
            case semantic::sva::PropertyKind::disjunction:
            case semantic::sva::PropertyKind::equivalence:
            case semantic::sva::PropertyKind::implies: {
                set(operands, k(2U));
                const auto left = operand(0U, add(in, 11U), operands_existing, node_start);
                auto* second = add(skip_node(static_cast<std::uint32_t>(property.children[0]),
                                       add(in, 11U), operands_existing),
                    1U);
                const auto right = operand(1U, second, operands_existing, node_start);
                auto* left_decided = ne0(get(left.decided));
                auto* right_decided = ne0(get(right.decided));
                auto* left_holds = ne0(get(left.holds));
                auto* right_holds = ne0(get(right.holds));
                auto* both = builder_.CreateAnd(left_decided, right_decided);
                switch (property.kind) {
                case semantic::sva::PropertyKind::conjunction: {
                    auto* left_fails = builder_.CreateAnd(left_decided,
                        builder_.CreateNot(left_holds));
                    auto* right_fails = builder_.CreateAnd(right_decided,
                        builder_.CreateNot(right_holds));
                    if_else(builder_.CreateOr(left_fails, right_fails), [&] {
                        fail_at(builder_.CreateSelect(left_fails, get(left.line), get(right.line)),
                            builder_.CreateSelect(left_fails, get(left.column),
                                get(right.column)));
                        resolve(k(0U), k(0U));
                    }, [&] {
                        if_then(both, [&] {
                            resolve(k(1U), builder_.CreateAnd(get(left.vacuous),
                                get(right.vacuous)));
                        });
                    });
                    break;
                }
                case semantic::sva::PropertyKind::disjunction: {
                    auto* left_ok = builder_.CreateAnd(left_decided, left_holds);
                    auto* right_ok = builder_.CreateAnd(right_decided, right_holds);
                    if_else(builder_.CreateOr(left_ok, right_ok), [&] {
                        auto* left_real = builder_.CreateAnd(left_ok,
                            builder_.CreateICmpEQ(get(left.vacuous), k(0U)));
                        auto* right_real = builder_.CreateAnd(right_ok,
                            builder_.CreateICmpEQ(get(right.vacuous), k(0U)));
                        resolve(k(1U), builder_.CreateZExt(
                            builder_.CreateNot(builder_.CreateOr(left_real, right_real)), i64_));
                    }, [&] {
                        if_then(both, [&] {
                            fail_at(get(right.line), get(right.column));
                            resolve(k(0U), k(0U));
                        });
                    });
                    break;
                }
                case semantic::sva::PropertyKind::implies: {
                    auto* left_fails = builder_.CreateAnd(left_decided,
                        builder_.CreateNot(left_holds));
                    auto* right_ok = builder_.CreateAnd(right_decided, right_holds);
                    if_else(left_fails, [&] {
                        resolve(k(1U), k(1U));
                    }, [&] {
                        if_else(right_ok, [&] {
                            resolve(k(1U), get(right.vacuous));
                        }, [&] {
                            if_then(both, [&] {
                                fail_at(get(right.line), get(right.column));
                                resolve(k(0U), k(0U));
                            });
                        });
                    });
                    break;
                }
                default:
                    // iff: decided when both are, true when they agree.
                    if_then(both, [&] {
                        fail_at(own_line, own_column);
                        resolve(builder_.CreateZExt(
                                    builder_.CreateICmpEQ(get(left.holds), get(right.holds)), i64_),
                            k(0U));
                    });
                    break;
                }
                break;
            }
            case semantic::sva::PropertyKind::always:
            case semantic::sva::PropertyKind::eventually: {
                // always / eventually over a window of start ticks
                // (Instance::step_window): a new operand instance starts at
                // each tick of the window; a decided one that does not end the
                // property is dropped.
                const bool always = property.kind == semantic::sva::PropertyKind::always;
                const auto bound = [&](const std::int32_t slot, const std::int64_t offset)
                    -> llvm::Value* {
                    if (slot < 0) {
                        return ks(offset);
                    }
                    return add(rd(add(rd(l::h_bounds_off), static_cast<std::uint64_t>(slot) + 1U)),
                        static_cast<std::uint64_t>(offset));
                };
                const bool open_end = property.hi_slot < 0
                    && property.hi == std::numeric_limits<std::int64_t>::max();
                auto* first = add(node_start, bound(property.lo_slot, property.lo));
                llvm::Value* last = open_end ? ks(std::numeric_limits<std::int64_t>::max())
                                             : add(node_start, bound(property.hi_slot, property.hi));
                auto* in_window = builder_.CreateAnd(builder_.CreateICmpSGE(now, first),
                    builder_.CreateICmpSLE(now, last));
                auto* count_in = guarded_old(existing, add(in, 8U));
                auto* total = add(count_in, builder_.CreateZExt(in_window, i64_));
                auto* finished = var("window_done");
                auto* position = var("window_position");
                set(position, add(in, 10U));
                auto* entry = var("window_entry");
                const auto child_node = static_cast<std::uint32_t>(property.children[0]);
                for_count(entry, total, [&] {
                    if_then(builder_.CreateICmpEQ(get(finished), k(0U)), [&] {
                        auto* existing_child = builder_.CreateZExt(
                            builder_.CreateICmpULT(get(entry), count_in), i64_);
                        auto* child_in = get(position);
                        auto* child_record = rd(l::h_heap_alt_used);
                        const auto child = emit_node(child_node, child_in, existing_child, now);
                        if_then(ne0(existing_child), [&] {
                            set(position, skip_node(child_node, child_in, k(1U)));
                        });
                        if_else(ne0(get(child.decided)), [&] {
                            if (always) {
                                if_then(builder_.CreateICmpEQ(get(child.holds), k(0U)), [&] {
                                    fail_at(get(child.line), get(child.column));
                                    resolve(k(0U), k(0U));
                                    set(finished, k(1U));
                                });
                            } else {
                                if_then(ne0(get(child.holds)), [&] {
                                    resolve(k(1U), get(child.vacuous));
                                    set(finished, k(1U));
                                });
                            }
                            if_then(builder_.CreateICmpEQ(get(finished), k(0U)), [&] {
                                wr(l::h_heap_alt_used, child_record);
                            });
                        }, [&] {
                            set(listed, add(get(listed), 1U));
                        });
                    });
                });
                if (!open_end) {
                    auto* expired = builder_.CreateAnd(builder_.CreateICmpSGE(now, last),
                        builder_.CreateICmpEQ(get(listed), k(0U)));
                    if_then(builder_.CreateAnd(builder_.CreateICmpEQ(get(finished), k(0U)), expired),
                        [&] {
                            fail_at(own_line, own_column);
                            resolve(k(always ? 1U : 0U), k(0U));
                        });
                }
                break;
            }
            case semantic::sva::PropertyKind::until: {
                // p until q (Instance::step_until): a pair of instances starts
                // at each tick until one ends the property; the pairs are
                // judged in order, and the leading pairs whose left held and
                // right failed are dropped.
                const auto left_node = static_cast<std::uint32_t>(property.children[0]);
                const auto right_node = static_cast<std::uint32_t>(property.children[1]);
                auto* stopped_in = builder_.CreateAnd(builder_.CreateLShr(flags_in, k(6U)), k(1U));
                set(until_bit, builder_.CreateShl(stopped_in, k(6U)));
                auto* count_in = guarded_old(existing, add(in, 9U));
                auto* fresh_pair = builder_.CreateXor(stopped_in, k(1U));
                auto* total = add(count_in, fresh_pair);
                auto* list_start = add(get(record), 10U);
                auto* position = var("until_position");
                set(position, add(in, 10U));
                auto* earlier = var("until_earlier", 1U);
                auto* right_pending = var("until_pending");
                auto* judged = var("until_judged");
                auto* returned = var("until_returned");
                auto* pruning = var("until_pruning", 1U);
                auto* pruned = var("until_pruned");
                auto* prune_end = var("until_prune_end");
                set(prune_end, list_start);
                auto* entry = var("until_entry");
                for_count(entry, total, [&] {
                    auto* existing_pair = builder_.CreateZExt(
                        builder_.CreateICmpULT(get(entry), count_in), i64_);
                    auto* left_in = get(position);
                    auto* right_in = skip_node(left_node, left_in, existing_pair);
                    const auto left = emit_node(left_node, left_in, existing_pair, now);
                    const auto right = emit_node(right_node, right_in, existing_pair, now);
                    if_then(ne0(existing_pair), [&] {
                        set(position, skip_node(right_node, right_in, k(1U)));
                    });
                    auto* left_decided = ne0(get(left.decided));
                    auto* left_holds = ne0(get(left.holds));
                    auto* right_decided = ne0(get(right.decided));
                    auto* right_holds = ne0(get(right.holds));
                    if_then(builder_.CreateICmpEQ(get(judged), k(0U)), [&] {
                        if_then(builder_.CreateAnd(right_decided, right_holds), [&] {
                            set(until_bit, k(64U));
                            if_then(ne0(get(earlier)), [&] {
                                resolve(k(1U), k(0U));
                                set(judged, k(1U));
                                set(returned, k(1U));
                            });
                        });
                        if_then(builder_.CreateICmpEQ(get(judged), k(0U)), [&] {
                            set(right_pending, builder_.CreateOr(get(right_pending),
                                builder_.CreateZExt(builder_.CreateNot(right_decided), i64_)));
                            if_else(builder_.CreateAnd(left_decided,
                                        builder_.CreateNot(left_holds)), [&] {
                                set(until_bit, k(64U));
                                if_then(builder_.CreateICmpEQ(get(right_pending), k(0U)), [&] {
                                    fail_at(get(left.line), get(left.column));
                                    resolve(k(0U), k(0U));
                                });
                                set(judged, k(1U));
                                set(returned, k(1U));
                            }, [&] {
                                set(earlier, builder_.CreateAnd(get(earlier),
                                    builder_.CreateZExt(left_decided, i64_)));
                            });
                        });
                    });
                    // The leading pairs that decided nothing further.
                    auto* droppable = builder_.CreateAnd(
                        builder_.CreateAnd(left_decided, left_holds),
                        builder_.CreateAnd(right_decided, builder_.CreateNot(right_holds)));
                    if_else(builder_.CreateAnd(ne0(get(pruning)), droppable), [&] {
                        set(pruned, add(get(pruned), 1U));
                        set(prune_end, rd(l::h_heap_alt_used));
                    }, [&] {
                        set(pruning, k(0U));
                    });
                });
                // Without an early return the dropped pairs leave the record.
                auto* drop = builder_.CreateAnd(builder_.CreateICmpEQ(get(returned), k(0U)),
                    ne0(get(pruned)));
                if_then(drop, [&] {
                    auto* removed = sub(get(prune_end), list_start);
                    auto* tail = sub(rd(l::h_heap_alt_used), get(prune_end));
                    auto* word = var("until_shift");
                    for_count(word, tail, [&] {
                        alt_put(add(list_start, get(word)),
                            rd(alt_index(add(get(prune_end), get(word)))));
                    });
                    wr(l::h_heap_alt_used, sub(rd(l::h_heap_alt_used), removed));
                });
                set(until_count, sub(total, builder_.CreateSelect(drop, get(pruned), k(0U))));
                break;
            }
            case semantic::sva::PropertyKind::implication:
            case semantic::sva::PropertyKind::followed_by: {
                // `s |-> p`, `s |=> p`, `s #-# p`, `s #=# p` over any
                // consequent (Instance::step_consequents): one consequent
                // instance per match of the antecedent sequence.
                const bool implication = property.kind
                    == semantic::sva::PropertyKind::implication;
                const auto child_node = static_cast<std::uint32_t>(property.children[0]);
                auto* done_input = builder_.CreateAnd(builder_.CreateLShr(flags_in, k(4U)), k(1U));
                auto* satisfied_input = builder_.CreateAnd(builder_.CreateLShr(flags_in, k(5U)), k(1U));
                auto* matcher_input = builder_.CreateAnd(builder_.CreateLShr(flags_in, k(7U)), k(1U));
                auto* consequents_input = guarded_old(existing, add(in, 4U));
                auto* count_in = guarded_old(existing, add(in, 8U));
                auto* antecedent_done = var("impl_done");
                auto* matched = var("impl_matched");
                auto* satisfied = var("impl_satisfied");
                set(antecedent_done, done_input);
                set(satisfied, satisfied_input);
                if_then(builder_.CreateICmpEQ(get(antecedent_done), k(0U)), [&] {
                    auto* matcher_start = rd(l::h_heap_alt_used);
                    auto* in_off = builder_.CreateSelect(ne0(matcher_input), add(in, 10U), ks(-1));
                    auto* result = call_matcher(property.sequence, in_off, node_start);
                    set(matched, builder_.CreateAnd(result, k(1U)));
                    if_else(ne0(builder_.CreateAnd(builder_.CreateLShr(result, k(1U)), k(1U))), [&] {
                        wr(l::h_heap_alt_used, matcher_start);
                        set(antecedent_done, k(1U));
                    }, [&] {
                        set(has_matcher, k(1U));
                    });
                });
                auto* position = var("impl_position");
                set(position, builder_.CreateSelect(ne0(matcher_input),
                    add(add(in, 10U), matcher_record_length(matcher_input, add(in, 10U),
                                          property.sequence)),
                    add(in, 10U)));
                auto* consequents = add(consequents_input, get(matched));
                set(consequent_count, consequents);
                auto* total = add(count_in, get(matched));
                llvm::Value* new_start = property.overlapped ? now : add(now, 1U);
                auto* nonvacuous = var("impl_nonvacuous");
                auto* finished = var("impl_finished");
                auto* entry = var("impl_entry");
                for_count(entry, total, [&] {
                    if_then(builder_.CreateICmpEQ(get(finished), k(0U)), [&] {
                        auto* existing_child = builder_.CreateZExt(
                            builder_.CreateICmpULT(get(entry), count_in), i64_);
                        auto* child_in = get(position);
                        auto* child_record = rd(l::h_heap_alt_used);
                        const auto child = emit_node(child_node, child_in, existing_child, new_start);
                        if_then(ne0(existing_child), [&] {
                            set(position, skip_node(child_node, child_in, k(1U)));
                        });
                        if_else(ne0(get(child.decided)), [&] {
                            if (implication) {
                                if_then(builder_.CreateICmpEQ(get(child.holds), k(0U)), [&] {
                                    fail_at(get(child.line), get(child.column));
                                    resolve(k(0U), k(0U));
                                    set(finished, k(1U));
                                });
                            } else {
                                if_then(ne0(get(child.holds)), [&] {
                                    resolve(k(1U), get(child.vacuous));
                                    set(finished, k(1U));
                                });
                            }
                            if_then(builder_.CreateICmpEQ(get(finished), k(0U)), [&] {
                                set(nonvacuous, builder_.CreateOr(get(nonvacuous),
                                    builder_.CreateXor(get(child.vacuous), k(1U))));
                                wr(l::h_heap_alt_used, child_record);
                            });
                        }, [&] {
                            set(listed, add(get(listed), 1U));
                        });
                    });
                });
                if_then(builder_.CreateICmpEQ(get(finished), k(0U)), [&] {
                    set(satisfied, builder_.CreateOr(get(satisfied), get(nonvacuous)));
                    auto* ended = builder_.CreateAnd(ne0(get(antecedent_done)),
                        builder_.CreateICmpEQ(get(listed), k(0U)));
                    if_then(ended, [&] {
                        if (implication) {
                            resolve(k(1U), builder_.CreateZExt(
                                builder_.CreateOr(builder_.CreateICmpEQ(consequents, k(0U)),
                                    builder_.CreateICmpEQ(get(satisfied), k(0U))), i64_));
                        } else {
                            fail_at(own_line, own_column);
                            resolve(k(0U), k(0U));
                        }
                    });
                });
                set(aux_flags, builder_.CreateOr(
                    builder_.CreateShl(get(antecedent_done), k(4U)),
                    builder_.CreateShl(get(satisfied), k(5U))));
                break;
            }
            case semantic::sva::PropertyKind::conditional: {
                // The branch is chosen at the first step, by the guard.
                auto* chosen_before = builder_.CreateAnd(builder_.CreateLShr(flags_in, k(8U)), k(1U));
                auto* chosen_input = guarded_old(existing, add(in, 3U));
                set(chosen_index, chosen_input);
                set(chosen_bit, builder_.CreateShl(chosen_before, k(8U)));
                if_then(builder_.CreateICmpEQ(chosen_before, k(0U)), [&] {
                    auto* condition = known_one_guard(property.guard);
                    auto* branch = builder_.CreateSelect(condition, k(0U), k(1U));
                    set(chosen_index, branch);
                    if (property.children.size() < 2U) {
                        // No else: the property holds vacuously.
                        if_else(builder_.CreateICmpUGE(branch, k(property.children.size())), [&] {
                            resolve(k(1U), k(1U));
                        }, [&] {
                            set(chosen_bit, k(256U));
                        });
                    } else {
                        set(chosen_bit, k(256U));
                    }
                });
                for (std::size_t branch = 0U; branch < property.children.size(); ++branch) {
                    auto* here = builder_.CreateAnd(
                        builder_.CreateICmpEQ(get(chosen_index), k(branch)),
                        builder_.CreateICmpEQ(get(state.decided), k(0U)));
                    if_then(here, [&] {
                        set(operands, k(1U));
                        const auto child = operand(static_cast<std::uint32_t>(branch), add(in, 11U),
                            builder_.CreateAnd(existing, chosen_before), node_start);
                        if_then(ne0(get(child.decided)), [&] { child_state(child); });
                    });
                }
                break;
            }
            default:
                throw std::logic_error("static kernel assertion plan has an unsupported property");
            }
        };
        // A decided instance is its header; one that has not reached its
        // start waits unchanged (Instance::step's first line).
        auto* is_decided_in = builder_.CreateICmpNE(verdict, k(0U));
        if_else(is_decided_in, [&] {
            set(state.decided, k(1U));
            set(state.holds, builder_.CreateZExt(builder_.CreateICmpEQ(verdict, k(1U)), i64_));
            set(state.vacuous, builder_.CreateAnd(builder_.CreateLShr(flags_in, k(2U)), k(1U)));
            set(state.line, guarded_old(existing, add(in, 5U)));
            set(state.column, guarded_old(existing, add(in, 6U)));
            set(chosen_index, guarded_old(existing, add(in, 3U)));
            set(chosen_bit, builder_.CreateAnd(flags_in, k(256U)));
            set(until_bit, builder_.CreateAnd(flags_in, k(64U)));
            set(aux_flags, builder_.CreateAnd(flags_in, k(48U)));
            set(consequent_count, guarded_old(existing, add(in, 4U)));
        }, [&] {
            if_then(builder_.CreateICmpSGE(now, node_start), step);
        });
        // The record: a decided instance drops its operands and matcher.
        auto* is_decided = ne0(get(state.decided));
        auto* header = get(record);
        if_else(is_decided, [&] {
            wr(l::h_heap_alt_used, add(header, 10U));
            alt_put(add(header, 2U),
                builder_.CreateOr(builder_.CreateOr(
                    builder_.CreateSelect(ne0(get(state.holds)), k(1U), k(2U)),
                    builder_.CreateShl(get(state.vacuous), k(2U))),
                    builder_.CreateOr(builder_.CreateOr(get(chosen_bit), get(until_bit)),
                        get(aux_flags))));
            alt_put(add(header, 4U), get(consequent_count));
            alt_put(add(header, 3U), get(chosen_index));
            alt_put(add(header, 5U), get(state.line));
            alt_put(add(header, 6U), get(state.column));
        }, [&] {
            alt_put(add(header, 2U),
                builder_.CreateOr(builder_.CreateOr(builder_.CreateOr(get(chosen_bit), get(until_bit)),
                                      get(aux_flags)),
                    builder_.CreateShl(get(has_matcher), k(7U))));
            alt_put(add(header, 4U), get(consequent_count));
            alt_put(add(header, 3U), get(chosen_index));
            alt_put(add(header, 7U), get(operands));
            alt_put(add(header, 8U), get(listed));
            alt_put(add(header, 9U), get(until_count));
        });
        return state;
    }

    // ---- the directive's tick (Monitor::tick and Instance::step) ----

    /// Removes the pending step requests for `start` (AttemptStorage::
    /// erase_requests); the result is 1 when there was one.
    [[nodiscard]] llvm::Value* erase_requests(llvm::Value* started)
    {
        namespace l = runtime::sva::layout;
        auto* removed = var("removed");
        auto* count = rd(l::h_requests_count);
        if_then(ne0(count), [&] {
            auto* kept = var("requests_kept");
            auto* index = var("request");
            for_count(index, count, [&] {
                auto* rows = rd(l::h_requests_off);
                auto* row = add(rows, mul(get(index), l::request_stride));
                auto* request_start = rd(row);
                if_then(builder_.CreateICmpNE(request_start, started), [&] {
                    auto* target = add(rows, mul(get(kept), l::request_stride));
                    wr(target, request_start);
                    wr(add(target, 1U), rd(add(row, 1U)));
                    set(kept, add(get(kept), 1U));
                });
            });
            wr(l::h_requests_count, get(kept));
            set(removed, builder_.CreateZExt(builder_.CreateICmpNE(get(kept), count), i64_));
        });
        return get(removed);
    }

    /// Drops the step requests of attempts that began before `time`
    /// (AttemptStorage::expire_requests; 39.5.2).
    void expire_requests(llvm::Value* time)
    {
        namespace l = runtime::sva::layout;
        auto* count = rd(l::h_requests_count);
        if_then(ne0(count), [&] {
            auto* kept = var("requests_kept");
            auto* index = var("request");
            for_count(index, count, [&] {
                auto* rows = rd(l::h_requests_off);
                auto* row = add(rows, mul(get(index), l::request_stride));
                auto* request_start = rd(row);
                if_then(builder_.CreateICmpSGE(request_start, time), [&] {
                    auto* target = add(rows, mul(get(kept), l::request_stride));
                    wr(target, request_start);
                    wr(add(target, 1U), rd(add(row, 1U)));
                    set(kept, add(get(kept), 1U));
                });
            });
            wr(l::h_requests_count, get(kept));
        });
    }

    [[nodiscard]] llvm::Function* build_tick(const KInst& inst, const std::uint32_t guard_storage)
    {
        namespace l = runtime::sva::layout;
        guard_storage_ = guard_storage;
        const auto start_register = inst.y + static_cast<std::uint32_t>(inst.imm_a >> 32U);
        const auto disabled_register = inst.y + static_cast<std::uint32_t>(inst.imm_b & 0xffff'ffffU);
        const auto& root = plan_.properties[plan_.root];
        const bool implication = root.kind == semantic::sva::PropertyKind::implication;
        const auto type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, ptr_ }, false);
        fn_ = llvm::Function::Create(type, llvm::Function::InternalLinkage,
            "fsim_sk_sva_tick_" + std::to_string(next_function_id()), module_);
        fn_->addFnAttr(llvm::Attribute::NoInline);
        fn_->addFnAttr(llvm::Attribute::NoUnwind);
        frame_ = fn_->getArg(0);
        registers_ = fn_->getArg(1);
        auto* entry = block("entry");
        failure_ = block("failure");
        builder_.SetInsertPoint(failure_);
        builder_.CreateRetVoid();
        builder_.SetInsertPoint(entry);

        // The directive's block: its entry in the kernel's table.
        auto* table = builder_.CreateLoad(ptr_, builder_.CreateConstInBoundsGEP1_64(i8_, frame_, 120U));
        auto* blocks = builder_.CreateLoad(ptr_, table);
        auto* member_base = builder_.CreateLoad(ptr_,
            builder_.CreateConstInBoundsGEP1_64(i8_, table, 8U));
        auto* member = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, frame_, 32U));
        auto* first = builder_.CreateLoad(i32_, builder_.CreateInBoundsGEP(i32_, member_base, member));
        slot_addr_ = builder_.CreateInBoundsGEP(ptr_, blocks,
            add(builder_.CreateZExt(first, i64_), inst.aux));
        {
            // The monitor is created and bound at the first tick (and again
            // after a restore).
            auto* unbound = builder_.CreateIsNull(builder_.CreateLoad(ptr_, slot_addr_));
            if_then(unbound, [&] {
                auto* ensure_type = llvm::FunctionType::get(ptr_, { ptr_, ptr_, i32_ }, false);
                auto ensure = module_.getOrInsertFunction("fsim_sk_sva_ensure", ensure_type);
                builder_.CreateCall(ensure_type, ensure.getCallee(),
                    { frame_, registers_, k32(at_) });
                check_status();
            });
        }
        if (check_) {
            call_check(0U);
        }
        auto* env = array("env", 6U);
        const auto env_cell = [&](const std::uint64_t index) {
            return builder_.CreateConstInBoundsGEP2_64(env->getAllocatedType(), env, 0U, index);
        };
        auto* time = builder_.CreateLoad(i64_,
            builder_.CreateLoad(ptr_, builder_.CreateConstInBoundsGEP1_64(i8_, table, 40U)));
        auto* start = var("start");
        set(start, builder_.CreateZExt(known_one(start_register, 0U), i64_));
        auto* filtered = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, frame_, 116U));
        if_then(builder_.CreateAnd(ne0(get(start)), builder_.CreateICmpNE(filtered, k32(0U))), [&] {
            auto* filter_type = llvm::FunctionType::get(i32_, { ptr_, i32_ }, false);
            auto filter = module_.getOrInsertFunction("fsim_sk_sva_filter", filter_type);
            auto* allowed = builder_.CreateCall(filter_type, filter.getCallee(), { frame_, k32(at_) });
            check_status();
            set(start, builder_.CreateZExt(builder_.CreateICmpNE(allowed, k32(0U)), i64_));
        });
        wr(l::h_time, time);
        auto* disabled = known_one(disabled_register, 0U);
        auto* after = block("ticked");
        llvm::Value* tick = nullptr;
        llvm::Value* fresh = nullptr;
        if (plan_.clock_guards.empty()) {
            tick = add(rd(l::h_tick), 1U);
            wr(l::h_tick, tick);
            fresh = get(start);
        } else {
            // Monitor::tick's multiclocked prologue: a clock ticks at most
            // once in a time step (its tick guard stays set for the rest of
            // the step, 15.5.3); the property counts ticks of the leading
            // clock and starts attempts only on them.
            const std::uint64_t clock_count = plan_.clock_guards.size();
            auto* any = var("any_clock");
            for (std::uint64_t clock = 0U; clock < clock_count; ++clock) {
                auto* base = add(rd(l::h_clocks_off), clock);
                auto* ticked = builder_.CreateAnd(
                    known_one_guard(plan_.clock_guards[clock]),
                    builder_.CreateICmpNE(rd(add(base, clock_count)), time));
                wr(add(base, 2U * clock_count), builder_.CreateZExt(ticked, i64_));
                if_then(ticked, [&] {
                    auto* at = add(rd(l::h_clocks_off), clock);
                    wr(at, add(rd(at), 1U));
                    wr(add(at, clock_count), time);
                    set(any, k(1U));
                });
            }
            auto* go = block("clocked");
            builder_.CreateCondBr(builder_.CreateOr(ne0(get(any)), disabled), go, after);
            builder_.SetInsertPoint(go);
            tick = rd(rd(l::h_clocks_off));
            wr(l::h_tick, tick);
            fresh = builder_.CreateAnd(get(start), rd(add(rd(l::h_clocks_off), 2U * clock_count)));
        }
        auto* starts_bound = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, frame_, 112U));
        if_then(builder_.CreateAnd(ne0(fresh), builder_.CreateICmpNE(starts_bound, k32(0U))), [&] {
            ring_push(l::h_started_count, l::h_started_cap, l::h_started_head,
                l::h_started_off, 2U, l::started_stride, { time });
        });
        auto* running = block("running");
        auto* stopped = block("disabled");
        builder_.CreateCondBr(disabled, stopped, running);

        // `disable iff` (39.4.2): one disabled outcome for each running
        // attempt and for the one that just started.
        builder_.SetInsertPoint(stopped);
        {
            auto* index = var("row");
            for_count(index, rd(l::h_attempts_count), [&] {
                auto* row = add(rd(l::h_attempts_off), mul(get(index), l::attempt_stride));
                push_outcome(k(4U), rd(add(row, l::a_started)), k(0U), k(0U));
            });
            if_then(ne0(fresh), [&] {
                push_outcome(k(4U), time, k(0U), k(0U));
            });
            wr(l::h_attempts_count, k(0U));
            wr(l::h_heap_used, k(0U));
            wr(l::h_heap_alt_used, k(0U));
            wr(l::h_steps_active, rd(l::h_requests_count));
            builder_.CreateBr(after);
        }

        builder_.SetInsertPoint(running);
        expire_requests(time);
        auto* existing = var("existing");
        set(existing, rd(l::h_attempts_count));
        if_then(ne0(fresh), [&] {
            // The new attempt's row; its record is written below.
            auto* count = rd(l::h_attempts_count);
            if_then(builder_.CreateICmpEQ(count, rd(l::h_attempts_cap)), [&] {
                grow(0U, add(count, 1U));
            });
            auto* row = add(rd(l::h_attempts_off), mul(count, l::attempt_stride));
            wr(row, time);
            for (std::uint64_t word = 1U; word < l::attempt_stride; ++word) {
                wr(add(row, word), k(0U));
            }
            wr(l::h_attempts_count, add(count, 1U));
        });
        auto* total = add(get(existing), fresh);
        wr(l::h_heap_alt_used, k(0U));
        auto* kept = var("kept");
        auto* stepping_kept = var("stepping_kept");
        auto* index = var("attempt");
        for_count(index, total, [&] {
            auto* row = add(rd(l::h_attempts_off), mul(get(index), l::attempt_stride));
            auto* started = rd(add(row, l::a_started));
            auto* blob = rd(add(row, l::a_blob));
            auto* is_existing = builder_.CreateICmpULT(get(index), get(existing));
            auto* serial = var("serial");
            auto* flags = var("flags");
            set(serial, rd(add(row, l::a_serial)));
            set(flags, rd(add(row, l::a_flags)));
            if_then(builder_.CreateNot(is_existing), [&] {
                auto* next = rd(l::h_next_serial);
                wr(l::h_next_serial, add(next, 1U));
                set(serial, next);
                set(flags, erase_requests(started));
            });
            builder_.CreateStore(builder_.CreateAnd(get(flags), k(1U)), env_cell(2U));
            builder_.CreateStore(started, env_cell(3U));
            builder_.CreateStore(tick, env_cell(0U));
            builder_.CreateStore(time, env_cell(1U));
            env_ = env;
            tick_ = tick;
            auto* existing_flag = builder_.CreateZExt(is_existing, i64_);
            auto* start_tick = builder_.CreateSelect(is_existing,
                guarded_old(existing_flag, add(blob, 1U)), tick);
            auto* record_start = rd(l::h_heap_alt_used);
            auto* row_next = block("row_next");
            auto* row_keep = block("row_keep");

            const auto call_matcher = [&](const std::uint32_t sequence, llvm::Value* in_off,
                                          llvm::Value* matcher_start) {
                auto* result = builder_.CreateCall(matchers_.at(sequence)->getFunctionType(),
                    matchers_.at(sequence),
                    { frame_, registers_, slot_addr_, env, in_off, matcher_start });
                check_status();
                return result;
            };
            const auto rewind = [&](llvm::Value* to) { wr(l::h_heap_alt_used, to); };
            // A record header: node, start, flags, chosen, consequents, the
            // failed position, the counts of operands, started instances and
            // until entries (Instance::encode).
            const auto put_header = [&](llvm::Value* at_word, const std::uint32_t node,
                                        llvm::Value* header_start, const std::uint64_t flag_bits,
                                        const std::uint64_t operands) {
                alt_put(at_word, k(node));
                alt_put(add(at_word, 1U), header_start);
                alt_put(add(at_word, 2U), k(flag_bits));
                for (std::uint64_t word = 3U; word < 10U; ++word) {
                    alt_put(add(at_word, word), k(word == 7U ? operands : 0U));
                }
            };
            const auto bit = [&](llvm::Value* value, const std::uint64_t shift) {
                return builder_.CreateAnd(builder_.CreateLShr(value, k(shift)), k(1U));
            };

            const bool tree_root = !plain_root();
            if (tree_root) {
                // The root is a property tree: Instance::step over its nodes,
                // then the attempt's outcome.
                const auto result = emit_node(plan_.root, blob, existing_flag, start_tick);
                auto* ending = block("tree_decided");
                builder_.CreateCondBr(ne0(get(result.decided)), ending, row_keep);
                builder_.SetInsertPoint(ending);
                auto* holds = ne0(get(result.holds));
                push_outcome(builder_.CreateSelect(holds,
                                 builder_.CreateSelect(ne0(get(result.vacuous)), k(3U), k(1U)),
                                 k(2U)),
                    started, builder_.CreateSelect(holds, k(0U), get(result.line)),
                    builder_.CreateSelect(holds, k(0U), get(result.column)));
                rewind(record_start);
                builder_.CreateBr(row_next);
            } else switch (root.kind) {
            case semantic::sva::PropertyKind::sequence: {
                reserve_alt(10U);
                put_header(record_start, plan_.root, start_tick, 128U, 0U);
                wr(l::h_heap_alt_used, add(record_start, 10U));
                auto* in_off = builder_.CreateSelect(is_existing, add(blob, 10U), ks(-1));
                auto* result = call_matcher(root.sequence, in_off, start_tick);
                auto* matched = builder_.CreateAnd(result, k(1U));
                auto* dead = builder_.CreateAnd(builder_.CreateLShr(result, k(1U)), k(1U));
                auto* env_line = builder_.CreateLoad(i64_, env_cell(4U));
                auto* env_column = builder_.CreateLoad(i64_, env_cell(5U));
                if (!plan_.every_match) {
                    auto* done_match = block("matched");
                    auto* not_match = block("not_matched");
                    builder_.CreateCondBr(ne0(matched), done_match, not_match);
                    builder_.SetInsertPoint(done_match);
                    push_outcome(k(1U), started, k(0U), k(0U));
                    rewind(record_start);
                    builder_.CreateBr(row_next);
                    builder_.SetInsertPoint(not_match);
                    auto* died = block("died");
                    builder_.CreateCondBr(ne0(dead), died, row_keep);
                    builder_.SetInsertPoint(died);
                    push_outcome(k(2U), started, env_line, env_column);
                    rewind(record_start);
                    builder_.CreateBr(row_next);
                } else {
                    // `cover sequence` (16.14.3): every match is a success; an
                    // attempt that ends without one is a failure.
                    auto* once_old = bit(guarded_old(existing_flag, add(blob, 2U)), 3U);
                    auto* once = builder_.CreateOr(once_old, matched);
                    if_then(ne0(matched), [&] {
                        push_outcome(k(1U), started, k(0U), k(0U));
                    });
                    alt_put(add(record_start, 2U),
                        builder_.CreateOr(k(128U), builder_.CreateShl(once, k(3U))));
                    auto* ended = block("ended");
                    builder_.CreateCondBr(ne0(dead), ended, row_keep);
                    builder_.SetInsertPoint(ended);
                    if_then(builder_.CreateICmpEQ(once, k(0U)), [&] {
                        push_outcome(k(2U), started, k(0U), k(0U));
                    });
                    rewind(record_start);
                    builder_.CreateBr(row_next);
                }
                break;
            }
            case semantic::sva::PropertyKind::negation: {
                // `not s`: the operand instance is the sequence started with
                // the attempt; it holds when s matched.
                const auto operand = static_cast<std::uint32_t>(root.child);
                reserve_alt(21U);
                put_header(record_start, plan_.root, start_tick, 0U, 1U);
                alt_put(add(record_start, 10U), k(0U));
                put_header(add(record_start, 11U), operand, start_tick, 128U, 0U);
                wr(l::h_heap_alt_used, add(record_start, 21U));
                auto* in_off = builder_.CreateSelect(is_existing, add(blob, 21U), ks(-1));
                auto* result = call_matcher(plan_.properties[operand].sequence, in_off, start_tick);
                auto* matched = builder_.CreateAnd(result, k(1U));
                auto* dead = builder_.CreateAnd(builder_.CreateLShr(result, k(1U)), k(1U));
                auto* done_match = block("operand_holds");
                auto* not_match = block("operand_pending");
                builder_.CreateCondBr(ne0(matched), done_match, not_match);
                builder_.SetInsertPoint(done_match);
                push_outcome(k(2U), started, k(root.line), k(root.column));
                rewind(record_start);
                builder_.CreateBr(row_next);
                builder_.SetInsertPoint(not_match);
                auto* died = block("operand_fails");
                builder_.CreateCondBr(ne0(dead), died, row_keep);
                builder_.SetInsertPoint(died);
                push_outcome(k(1U), started, k(0U), k(0U));
                rewind(record_start);
                builder_.CreateBr(row_next);
                break;
            }
            default: {
                // `s |-> c`, `s |=> c`, `s #-# c`, `s #=# c` (Instance::
                // step_consequents): one consequent instance per match of the
                // antecedent s; the record is the header, the antecedent's
                // matcher while it lives, then the pending consequents.
                const std::uint32_t antecedent = root.sequence;
                const auto consequent_node = static_cast<std::uint32_t>(root.child);
                const auto& consequent = plan_.properties[consequent_node];
                const bool vacuous_consequent
                    = consequent.kind == semantic::sva::PropertyKind::vacuous;
                auto* input_flags = guarded_old(existing_flag, add(blob, 2U));
                auto* consequents_in = guarded_old(existing_flag, add(blob, 4U));
                auto* started_in = guarded_old(existing_flag, add(blob, 8U));
                auto* satisfied_in = bit(input_flags, 5U);
                auto* has_matcher_in = bit(input_flags, 7U);
                reserve_alt(10U);
                put_header(record_start, plan_.root, start_tick, 0U, 0U);
                wr(l::h_heap_alt_used, add(record_start, 10U));
                auto* antecedent_done = var("antecedent_done");
                auto* matcher_out = var("matcher_out");
                auto* antecedent_matched = var("antecedent_matched");
                set(antecedent_done, bit(input_flags, 4U));
                auto* input_matcher = add(blob, 10U);
                if_then(builder_.CreateICmpEQ(get(antecedent_done), k(0U)), [&] {
                    auto* matcher_start = rd(l::h_heap_alt_used);
                    auto* in_off = builder_.CreateSelect(ne0(has_matcher_in), input_matcher, ks(-1));
                    auto* result = call_matcher(antecedent, in_off, start_tick);
                    set(antecedent_matched, builder_.CreateAnd(result, k(1U)));
                    auto* dead = builder_.CreateAnd(builder_.CreateLShr(result, k(1U)), k(1U));
                    if_else(ne0(dead), [&] {
                        rewind(matcher_start);
                        set(antecedent_done, k(1U));
                    }, [&] {
                        set(matcher_out, k(1U));
                    });
                });
                // Where the input's consequents begin: past the antecedent's
                // matcher record (five words, its threads, the join count).
                auto* consequent_at = var("consequent_at");
                set(consequent_at, builder_.CreateSelect(ne0(has_matcher_in),
                    add(input_matcher,
                        matcher_record_length(has_matcher_in, input_matcher, antecedent)),
                    input_matcher));
                auto* consequents = add(consequents_in, get(antecedent_matched));
                auto* total_consequents = add(started_in, get(antecedent_matched));
                auto* nonvacuous = var("nonvacuous");
                auto* pending_consequents = var("pending_consequents");
                auto* each = var("consequent");
                auto* new_start = plan_.properties[plan_.root].overlapped ? tick : add(tick, 1U);
                for_count(each, total_consequents, [&] {
                    auto* is_new = builder_.CreateICmpUGE(get(each), started_in);
                    auto* input_flag = builder_.CreateZExt(builder_.CreateNot(is_new), i64_);
                    auto* at_input = get(consequent_at);
                    auto* input_start = guarded_old(input_flag, add(at_input, 1U));
                    auto* c_start = builder_.CreateSelect(is_new, new_start, input_start);
                    auto* c_flags_in = guarded_old(input_flag, add(at_input, 2U));
                    auto* has_matcher = bit(c_flags_in, 7U);
                    auto* input_length = k(10U);
                    if (!vacuous_consequent) {
                        input_length = builder_.CreateSelect(ne0(has_matcher),
                            add(matcher_record_length(has_matcher, add(at_input, 10U),
                                    consequent.sequence),
                                10U),
                            k(10U));
                    }
                    auto* c_record = rd(l::h_heap_alt_used);
                    reserve_alt(10U);
                    put_header(c_record, consequent_node, c_start, 0U, 0U);
                    wr(l::h_heap_alt_used, add(c_record, 10U));
                    auto* decided = var("decided");
                    auto* holds = var("holds");
                    auto* vacuous = var("vacuous");
                    auto* fail_line = var("consequent_line");
                    auto* fail_column = var("consequent_column");
                    // Instance::step: nothing before the instance's start.
                    if_then(builder_.CreateICmpSGE(tick, c_start), [&] {
                        if (vacuous_consequent) {
                            set(decided, k(1U));
                            set(holds, k(1U));
                            set(vacuous, k(1U));
                            return;
                        }
                        alt_put(add(c_record, 2U), k(128U));
                        auto* in_off = builder_.CreateSelect(ne0(has_matcher),
                            add(at_input, 10U), ks(-1));
                        auto* result = call_matcher(consequent.sequence, in_off, c_start);
                        auto* matched = builder_.CreateAnd(result, k(1U));
                        auto* dead = builder_.CreateAnd(builder_.CreateLShr(result, k(1U)), k(1U));
                        if_else(ne0(matched), [&] {
                            set(decided, k(1U));
                            set(holds, k(1U));
                        }, [&] {
                            if_then(ne0(dead), [&] {
                                set(decided, k(1U));
                                set(fail_line, builder_.CreateLoad(i64_, env_cell(4U)));
                                set(fail_column, builder_.CreateLoad(i64_, env_cell(5U)));
                            });
                        });
                    });
                    auto* is_decided = ne0(get(decided));
                    auto* settle = block("consequent_decided");
                    auto* pending = block("consequent_pending");
                    auto* advance = block("consequent_next");
                    builder_.CreateCondBr(is_decided, settle, pending);
                    builder_.SetInsertPoint(pending);
                    set(pending_consequents, add(get(pending_consequents), 1U));
                    builder_.CreateBr(advance);
                    builder_.SetInsertPoint(settle);
                    rewind(c_record);
                    // `|->`: a failed consequent fails the attempt at once;
                    // `#-#`: a held one holds it.
                    auto* ends = builder_.CreateICmpEQ(get(holds), k(implication ? 0U : 1U));
                    auto* ending = block("attempt_decided");
                    auto* continuing = block("attempt_goes_on");
                    builder_.CreateCondBr(ends, ending, continuing);
                    builder_.SetInsertPoint(ending);
                    if (implication) {
                        push_outcome(k(2U), started, get(fail_line), get(fail_column));
                    } else {
                        push_outcome(builder_.CreateSelect(ne0(get(vacuous)), k(3U), k(1U)),
                            started, k(0U), k(0U));
                    }
                    rewind(record_start);
                    builder_.CreateBr(row_next);
                    builder_.SetInsertPoint(continuing);
                    set(nonvacuous, builder_.CreateOr(get(nonvacuous),
                        builder_.CreateXor(get(vacuous), k(1U))));
                    builder_.CreateBr(advance);
                    builder_.SetInsertPoint(advance);
                    set(consequent_at, add(at_input, input_length));
                });
                auto* satisfied = builder_.CreateOr(satisfied_in, get(nonvacuous));
                auto* resolved = builder_.CreateAnd(ne0(get(antecedent_done)),
                    builder_.CreateICmpEQ(get(pending_consequents), k(0U)));
                auto* over = block("attempt_over");
                auto* goes_on = block("attempt_pending");
                builder_.CreateCondBr(resolved, over, goes_on);
                builder_.SetInsertPoint(over);
                if (implication) {
                    auto* vacuous_end = builder_.CreateOr(builder_.CreateICmpEQ(consequents, k(0U)),
                        builder_.CreateICmpEQ(satisfied, k(0U)));
                    push_outcome(builder_.CreateSelect(vacuous_end, k(3U), k(1U)), started, k(0U),
                        k(0U));
                } else {
                    push_outcome(k(2U), started, k(root.line), k(root.column));
                }
                rewind(record_start);
                builder_.CreateBr(row_next);
                builder_.SetInsertPoint(goes_on);
                alt_put(add(record_start, 2U),
                    builder_.CreateOr(builder_.CreateOr(builder_.CreateShl(get(matcher_out), k(7U)),
                                          builder_.CreateShl(get(antecedent_done), k(4U))),
                        builder_.CreateShl(satisfied, k(5U))));
                alt_put(add(record_start, 4U), consequents);
                alt_put(add(record_start, 8U), get(pending_consequents));
                builder_.CreateBr(row_keep);
                break;
            }
            }

            // The attempt survives: its row moves to the kept prefix.
            builder_.SetInsertPoint(row_keep);
            auto* length = sub(rd(l::h_heap_alt_used), record_start);
            auto* out_row = add(rd(l::h_attempts_off), mul(get(kept), l::attempt_stride));
            wr(add(out_row, l::a_started), started);
            wr(add(out_row, l::a_blob), record_start);
            wr(add(out_row, l::a_length), length);
            wr(add(out_row, l::a_serial), get(serial));
            wr(add(out_row, l::a_flags), get(flags));
            set(kept, add(get(kept), 1U));
            set(stepping_kept, add(get(stepping_kept), builder_.CreateAnd(get(flags), k(1U))));
            builder_.CreateBr(row_next);
            builder_.SetInsertPoint(row_next);
        });
        // end_pass: the alternate heap becomes the current one.
        wr(l::h_attempts_count, get(kept));
        auto* current = rd(l::h_heap_off);
        wr(l::h_heap_off, rd(l::h_heap_alt_off));
        wr(l::h_heap_alt_off, current);
        wr(l::h_heap_used, rd(l::h_heap_alt_used));
        wr(l::h_heap_alt_used, k(0U));
        wr(l::h_steps_active, add(rd(l::h_requests_count), get(stepping_kept)));
        builder_.CreateBr(after);

        builder_.SetInsertPoint(after);
        if (check_) {
            call_check(1U);
        }
        {
            // The attempt-start and step reports a library asked for.
            auto* bound = builder_.CreateICmpNE(builder_.CreateLoad(i32_,
                                                    builder_.CreateConstInBoundsGEP1_64(i8_, frame_, 112U)),
                k32(0U));
            auto* events = ne0(rd(l::h_events_count));
            if_then(builder_.CreateOr(bound, events), [&] {
                auto* post_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                    { ptr_, i32_ }, false);
                auto post = module_.getOrInsertFunction("fsim_sk_sva_post", post_type);
                builder_.CreateCall(post_type, post.getCallee(), { frame_, k32(at_) });
                check_status();
            });
        }
        builder_.CreateStore(rd(l::h_outcomes_count),
            builder_.CreateInBoundsGEP(i64_, registers_, k(static_cast<std::uint64_t>(inst.d) * 2U)));
        builder_.CreateStore(k(0U),
            builder_.CreateInBoundsGEP(i64_, registers_, k(static_cast<std::uint64_t>(inst.d) * 2U + 1U)));
        if (sva_verify_enabled()) {
            auto verify_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, ptr_, i32_ }, false);
            auto verify = module_.getOrInsertFunction("fsim_sk_sva_verify", verify_type);
            builder_.CreateCall(verify_type, verify.getCallee(),
                { frame_, registers_, k32(at_) });
            check_status();
        }
        builder_.CreateRetVoid();
        return fn_;
    }

    void call_check(const std::uint32_t phase)
    {
        auto* check_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, ptr_, i32_, i32_ }, false);
        auto check = module_.getOrInsertFunction("fsim_sk_sva_check", check_type);
        builder_.CreateCall(check_type, check.getCallee(),
            { frame_, registers_, k32(at_), k32(phase) });
        check_status();
    }
};

class FunctionEmitter {
public:
    FunctionEmitter(llvm::Module& module, llvm::Function& function,
        const detail::CompiledBody& body,
        const bool local_registers, const bool optimizing, const bool flush_locals,
        const bool two_state, std::vector<std::uint32_t>* const resume_points,
        const bool vhdl)
        : context_(module.getContext())
        , module_(module)
        , function_(function)
        , body_(body)
        , builder_(context_, llvm::InstSimplifyFolder(module.getDataLayout()))
        , entry_builder_(context_, llvm::InstSimplifyFolder(module.getDataLayout()))
        , i32_(llvm::Type::getInt32Ty(context_))
        , i64_(llvm::Type::getInt64Ty(context_))
        , i8_(llvm::Type::getInt8Ty(context_))
        , ptr_(llvm::PointerType::getUnqual(context_))
        , local_registers_(local_registers)
        , optimizing_(optimizing)
        , flush_locals_(flush_locals)
        , two_state_(two_state)
        , resume_points_(resume_points)
        , vhdl_(vhdl)
    {
        if (resume_points_ != nullptr) {
            resume_seen_.insert(resume_points_->begin(), resume_points_->end());
        }
    }

    void outline_stores(const bool outline)
    {
        outline_stores_ = outline;
    }

    void emit()
    {
        const auto size = static_cast<std::uint32_t>(body_.code.size());
        frame_ = function_.getArg(0);
        registers_ = function_.getArg(1);
        auto* entry = llvm::BasicBlock::Create(context_, "entry", &function_);
        entry_ = entry;
        return_block_ = llvm::BasicBlock::Create(context_, "done", &function_);
        failure_block_ = llvm::BasicBlock::Create(context_, "failed", &function_);
        // Leaders: branch targets and the instructions after jumps/branches.
        std::vector<std::uint8_t> leader(size + 1U, 0U);
        leader[0] = 1U;
        leader[std::min(body_.entry, size)] = 1U;
        for (const auto target : body_.return_targets) {
            leader[std::min(target, size)] = 1U;
        }
        for (std::uint32_t at = 0U; at < size; ++at) {
            const auto& inst = body_.code[at];
            if (inst.op == KOp::jump || inst.op == KOp::call) {
                leader[std::min(inst.d, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::branch) {
                leader[std::min(inst.y, size)] = 1U;
                leader[std::min(inst.z, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::ret || inst.op == KOp::suspend) {
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::mark && inst.sub != 0U) {
                // A dissolved partition's gated member: skipped to aux.
                leader[std::min(inst.aux, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::step) {
                leader[at + 1U] = 1U;
                if (inst.z >= 1U) {
                    leader[std::min(inst.d, size)] = 1U;
                }
                if (inst.z >= 2U) {
                    leader[std::min(inst.y, size)] = 1U;
                }
            }
        }
        for (const auto resume : body_.resume_entries) {
            leader[std::min(resume, size)] = 1U;
        }
        // Full code continuing two-state code (see guard_known).
        const auto continuations = !two_state_ && resume_points_ != nullptr
            ? std::span<const std::uint32_t> { *resume_points_ }
            : std::span<const std::uint32_t> { };
        for (const auto resume : continuations) {
            leader[std::min(resume, size)] = 1U;
        }
        blocks_.assign(size + 1U, nullptr);
        for (std::uint32_t at = 0U; at < size; ++at) {
            if (leader[at] != 0U) {
                blocks_[at] = llvm::BasicBlock::Create(
                    context_, "i" + std::to_string(at), &function_);
            }
        }
        blocks_[size] = return_block_;
        builder_.SetInsertPoint(entry);
        out_ = builder_.CreateAlloca(i64_, constant(4U), "out");
        plan_local_registers();
        for (std::size_t reg = 0U; reg < local_.size(); ++reg) {
            if (local_[reg] != nullptr) {
                local_[reg] = builder_.CreateAlloca(
                    llvm::ArrayType::get(i64_, 2U), nullptr, "r");
            }
            if (local_[reg] != nullptr
                && (flush_exits() || !body_.resume_entries.empty())) {
                // Defined (as a known 0) before this run writes it, for the
                // flush at an exit (failure_block_, a suspension).
                builder_.CreateStore(constant(0U),
                    builder_.CreateConstInBoundsGEP1_64(i64_, local_[reg], 0U));
                builder_.CreateStore(constant(0U),
                    builder_.CreateConstInBoundsGEP1_64(i64_, local_[reg], 1U));
            }
        }
        arena_ = builder_.CreateLoad(ptr_, frame_field(0U), "arena");
        bindings_ = builder_.CreateLoad(ptr_, frame_field(8U), "bindings");
        // A VHDL process's first run enters at operation 0, its prologue
        // (static_kernel_prologue_entry in `reserved`).
        const bool prologue
            = detail::has_prologue_entry(body_, vhdl_);
        if (body_.resume_entries.empty() && (!continuations.empty() || prologue)) {
            // Entered at `reserved` when two-state code stopped there; an
            // ordinary run passes 0, which is never a continuation.
            auto* start = builder_.CreateLoad(i32_, frame_field(44U), "resume");
            auto* targets = builder_.CreateSwitch(start,
                blocks_[std::min(body_.entry, size)],
                static_cast<unsigned>(continuations.size() + 1U));
            for (const auto resume : continuations) {
                targets->addCase(llvm::ConstantInt::get(
                                     llvm::cast<llvm::IntegerType>(i32_), resume),
                    blocks_[std::min(resume, size)]);
            }
            if (prologue) {
                targets->addCase(llvm::ConstantInt::get(
                                     llvm::cast<llvm::IntegerType>(i32_),
                                     static_kernel_prologue_entry),
                    blocks_[0]);
            }
        } else if (body_.resume_entries.empty()) {
            builder_.CreateBr(blocks_[std::min(body_.entry, size)]);
        } else {
            // A behavioral thread resumes at the instruction in `reserved`.
            auto* start = builder_.CreateLoad(i32_, frame_field(44U), "resume");
            auto* targets = builder_.CreateSwitch(start, failure_block_,
                static_cast<unsigned>(body_.resume_entries.size()));
            for (const auto resume : body_.resume_entries) {
                targets->addCase(llvm::ConstantInt::get(
                                     llvm::cast<llvm::IntegerType>(i32_), resume),
                    blocks_[std::min(resume, size)]);
            }
        }
        for (std::uint32_t at = 0U; at < size; ++at) {
            if (blocks_[at] != nullptr) {
                if (builder_.GetInsertBlock() != nullptr
                    && builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(blocks_[at]);
                }
                builder_.SetInsertPoint(blocks_[at]);
                cache_.clear();
                spine_.clear();
                spine_.insert(blocks_[at]);
            }
            last_unknown_ = nullptr;
            last_weak_ = nullptr;
            insert_unknown_ = nullptr;
            insert_weak_ = nullptr;
            at_ = at;
            guarded_unknown_ = nullptr;
            emit_unknown_checks(at);
            const auto result_unknown = unknown_before(at);
            const auto result_weak = weak_before(at);
            emit_instruction(at);
            unknown_after(at, result_unknown, result_weak);
            // Every instruction's inner blocks rejoin in the block it ends
            // in, which dominates the rest of the KIR block; values defined
            // in its inner blocks must not be reused after it.
            spine_.insert(builder_.GetInsertBlock());
            sweep_cache();
        }
        if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
            builder_.CreateBr(return_block_);
        }
        builder_.SetInsertPoint(return_block_);
        builder_.CreateRet(llvm::ConstantInt::get(i32_, 0U));
        builder_.SetInsertPoint(failure_block_);
        // VHDL deoptimization and the full code (after two-state code)
        // continue from the register file:
        // local registers go there first. (Other SystemVerilog failures only
        // report errors.)
        if (flush_exits()) {
            flush_local_registers();
        }
        builder_.CreateRet(llvm::ConstantInt::get(i32_, 1U));
    }

    /// Whether an exit continues from the register file: VHDL
    /// deoptimization and the full code.
    [[nodiscard]] bool flush_exits() const noexcept
    {
        return flush_locals_ || body_.call_stack_base != 0U;
    }

    /// Stores every local register to the register file.
    void flush_local_registers()
    {
        for (std::size_t reg = 0U; reg < local_.size(); ++reg) {
            if (local_[reg] == nullptr) {
                continue;
            }
            for (std::uint64_t word = 0U; word < 2U; ++word) {
                builder_.CreateStore(
                    builder_.CreateLoad(i64_,
                        builder_.CreateConstInBoundsGEP1_64(i64_, local_[reg], word)),
                    builder_.CreateConstInBoundsGEP1_64(i64_, registers_,
                        static_cast<std::uint64_t>(reg) * 2U + word));
            }
        }
    }

private:
    [[nodiscard]] llvm::Value* constant(const std::uint64_t value)
    {
        return llvm::ConstantInt::get(i64_, value);
    }

    [[nodiscard]] llvm::Value* frame_field(const std::uint32_t offset)
    {
        return builder_.CreateConstInBoundsGEP1_64(i8_, frame_, offset);
    }

    /// The two words of register `reg`: in the frame's register file, or in
    /// a local slot (promoted to SSA values) when no run reads it before
    /// writing it.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> register_address(
        const std::uint32_t reg)
    {
        if (reg < local_.size() && local_[reg] != nullptr) {
            return { builder_.CreateConstInBoundsGEP1_64(i64_, local_[reg], 0U),
                builder_.CreateConstInBoundsGEP1_64(i64_, local_[reg], 1U) };
        }
        return { builder_.CreateConstInBoundsGEP1_64(i64_, registers_,
                     static_cast<std::uint64_t>(reg) * 2U),
            builder_.CreateConstInBoundsGEP1_64(i64_, registers_,
                static_cast<std::uint64_t>(reg) * 2U + 1U) };
    }

    /// Registers that every run writes before reading carry nothing from one
    /// run to the next. In bodies that never resume from the register file
    /// (no VHDL deoptimization, no behavioral suspension, no generic
    /// instruction reading it) they become local slots, so their values stay
    /// in machine registers instead of being stored to the frame.
    void plan_local_registers()
    {
        local_.clear();
        static const bool disabled = [] {
            const char* text = std::getenv("FSIM_STATIC_KERNEL_LOCAL_REGISTERS");
            return text != nullptr && std::string_view { text } == "0";
        }();
        // Two-state code hands over to the full code, which continues from
        // the register file (as from a resume entry).
        // Exits flush local registers to the register file (failure_block_,
        // and a suspension before it returns), so two-state code,
        // deoptimizing bodies and
        // resumed threads may use them: a resume entry, like the entry,
        // starts with nothing defined. The full code's continuations may not.
        // Unoptimized code spills every value live across a block boundary,
        // so a body whose loops run around calls or suspensions keeps its
        // registers in the frame unless the optimizing backend compiles it.
        if (!local_registers_ || disabled
            || (resume_points_ != nullptr && !resume_points_->empty())
            || (!optimizing_
                && (!body_.resume_entries.empty() || body_.call_stack_base != 0U))) {
            return;
        }
        const auto size = static_cast<std::uint32_t>(body_.code.size());
        const auto count = static_cast<std::uint32_t>(body_.registers.size());
        const auto words = (static_cast<std::size_t>(count) + 63U) / 64U;
        using Bits = std::vector<std::uint64_t>;
        const auto test = [](const Bits& bits, const std::uint32_t reg) {
            return ((bits[reg / 64U] >> (reg % 64U)) & 1U) != 0U;
        };
        const auto set = [](Bits& bits, const std::uint32_t reg) {
            bits[reg / 64U] |= std::uint64_t { 1 } << (reg % 64U);
        };
        // Register operands of each instruction; false for an instruction
        // that may reach the register file another way.
        std::vector<std::uint32_t> reads;
        reads.reserve(8U);
        const auto operands = [&](const KInst& inst, bool& writes) {
            reads.clear();
            writes = false;
            switch (inst.op) {
            case KOp::nop:
            case KOp::mem_bind:
            case KOp::mark:
            case KOp::jump:
                return true;
            case KOp::constant:
            case KOp::load_slot:
            case KOp::load_host:
            case KOp::load_field:
            case KOp::load_slot9:
            case KOp::load_field9:
                writes = true;
                return true;
            case KOp::sva_outcome:
            case KOp::sva_sampled_load:
                // Inline loads of the host's assertion words; d defined.
                writes = true;
                return true;
            case KOp::integer_unary:
                reads = { inst.x };
                writes = true;
                return true;
            case KOp::integer_binary:
                reads = { inst.x, inst.y };
                writes = true;
                return true;
            case KOp::integer_check:
            case KOp::assert_check:
                reads = { inst.x };
                return true;
            case KOp::store_vhdl:
                reads = { inst.x };
                if (inst.sub == 1U || inst.sub == 3U) {
                    reads.push_back(inst.y);
                }
                return true;
            case KOp::copy:
            case KOp::reduce:
            case KOp::unary_not:
            case KOp::logical_not:
            case KOp::two_state:
            case KOp::extract:
                if (inst.sub != 3U) {
                    reads = { inst.x };
                }
                writes = true;
                return true;
            case KOp::binary:
            case KOp::logical_binary:
            case KOp::shift:
            case KOp::insert:
                reads = { inst.x, inst.y };
                writes = true;
                return true;
            case KOp::concat:
                for (std::uint32_t index = 0U; index < inst.x; ++index) {
                    reads.push_back(body_.concat[inst.aux + index].reg);
                }
                writes = true;
                return true;
            case KOp::conditional:
            case KOp::dynamic_insert:
            case KOp::dynamic_part_insert:
                reads = { inst.x, inst.y, inst.z };
                writes = true;
                return true;
            case KOp::dynamic_extract:
            case KOp::dynamic_part_select:
                reads = { inst.y };
                if (inst.sub == 0U) {
                    reads.push_back(inst.x);
                }
                writes = true;
                return true;
            case KOp::mem_read:
                reads = { inst.y };
                // An element of more than 64 bits is wide storage (sub 1):
                // never local, its writes not listed.
                writes = inst.sub != 1U;
                return true;
            case KOp::store_host:
                reads = { inst.x };
                if (inst.aux != 0U) {
                    reads.push_back(inst.y);
                }
                return true;
            case KOp::store_slot:
            case KOp::store_slot_nba:
            case KOp::branch:
                reads = { inst.x };
                return true;
            case KOp::mem_write:
                if (inst.sub == 1U) {
                    // The value is wide storage; the index and the part's base.
                    reads = { inst.y };
                    if (inst.aux != 0U) {
                        reads.push_back(inst.z);
                    }
                    return true;
                }
                reads = { inst.x, inst.y, inst.z };
                return true;
            case KOp::store_slot_dynamic:
            case KOp::store_slot_part:
                reads = { inst.x, inst.y, inst.z };
                return true;
            case KOp::mem_copy:
                return true;
            case KOp::wide_move:
                reads = { inst.y };
                return true;
            // Wide storage is never local: its writes are not listed.
            case KOp::wide_load:
            case KOp::wide_copy:
            case KOp::wide_const:
                return true;
            case KOp::wide_store:
                if (inst.sub == 3U && inst.imm_a != 0U) {
                    // A host signal's dynamic write reads its narrow index.
                    reads = { inst.y };
                }
                return true;
            case KOp::wide_concat:
                for (std::uint32_t index = 0U; index < inst.x; ++index) {
                    if (body_.concat[inst.aux + index].width <= 64U) {
                        reads.push_back(body_.concat[inst.aux + index].reg);
                    }
                }
                return true;
            case KOp::wide_part_insert:
                reads = { inst.y, inst.z };
                return true;
            case KOp::wide_alu: {
                // The narrow operands (a shift amount, a condition, a
                // logical operand) and a narrow result; wide storage is never
                // local.
                const auto family = static_cast<detail::WideFamily>(inst.offset);
                const std::uint32_t used
                    = family == detail::WideFamily::conditional
                        || family == detail::WideFamily::dynamic_insert ? 3U
                    : family == detail::WideFamily::binary
                        || family == detail::WideFamily::shift
                        || family == detail::WideFamily::logical_binary
                        || family == detail::WideFamily::insert
                        || family == detail::WideFamily::dynamic_part_select
                    ? 2U : 1U;
                for (std::uint32_t slot = 1U; slot <= used; ++slot) {
                    if ((inst.aux & (detail::wide_alu_wide << slot)) == 0U) {
                        reads.push_back(slot == 1U ? inst.x : slot == 2U ? inst.y : inst.z);
                    }
                }
                writes = (inst.aux & detail::wide_alu_wide) == 0U;
                return true;
            }
            case KOp::net_delay:
                reads = { inst.x };
                return true;
            case KOp::call:
            case KOp::ret:
                // Fixed-register calls stay inside the function; their
                // pointer and entries stay in the register file (pinned).
                return inst.z != 0U;
            case KOp::frame_push:
            case KOp::frame_pop:
                // Frames address the register file by index: no register
                // of the body is local.
                return false;
            case KOp::suspend:
                return true;
            default:
                return false;
            }
        };
        // Registers generated code addresses dynamically.
        Bits pinned((static_cast<std::size_t>(body_.registers.size()) + 63U) / 64U, 0U);
        const auto pin = [&](const std::uint32_t reg) {
            if (reg < body_.registers.size()) {
                pinned[reg / 64U] |= std::uint64_t { 1 } << (reg % 64U);
            }
        };
        // Basic blocks and their successors.
        const auto entry = std::min(body_.entry, size);
        std::vector<std::uint8_t> leader(size + 1U, 0U);
        leader[entry] = 1U;
        for (std::uint32_t at = 0U; at < size; ++at) {
            const auto& inst = body_.code[at];
            bool writes = false;
            if (!operands(inst, writes)) {
                return;
            }
            if (inst.op == KOp::jump) {
                leader[std::min(inst.d, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::call || inst.op == KOp::ret) {
                if (inst.op == KOp::call) {
                    leader[std::min(inst.d, size)] = 1U;
                }
                leader[at + 1U] = 1U;
                pin(inst.x);
                for (std::uint32_t slot = 0U; slot < inst.z; ++slot) {
                    pin(inst.y + slot);
                }
            } else if (inst.op == KOp::branch) {
                leader[std::min(inst.y, size)] = 1U;
                leader[std::min(inst.z, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::mark && inst.sub != 0U) {
                leader[std::min(inst.aux, size)] = 1U;
                leader[at + 1U] = 1U;
            } else if (inst.op == KOp::suspend) {
                leader[at + 1U] = 1U;
            }
        }
        for (const auto target : body_.return_targets) {
            leader[std::min<std::size_t>(target, size)] = 1U;
        }
        for (const auto resume : body_.resume_entries) {
            leader[std::min<std::size_t>(resume, size)] = 1U;
        }
        std::vector<std::uint32_t> block_begin;
        std::vector<std::uint32_t> block_of(size + 1U, 0U);
        for (std::uint32_t at = 0U; at < size; ++at) {
            if (leader[at] != 0U || at == 0U) {
                block_begin.push_back(at);
            }
            block_of[at] = static_cast<std::uint32_t>(block_begin.size() - 1U);
        }
        const auto blocks = static_cast<std::uint32_t>(block_begin.size());
        const auto exit_block = blocks;
        block_of[size] = exit_block;
        const auto block_end = [&](const std::uint32_t block) {
            return block + 1U < blocks ? block_begin[block + 1U] : size;
        };
        std::vector<std::vector<std::uint32_t>> successors(blocks);
        for (std::uint32_t block = 0U; block < blocks; ++block) {
            const auto last = block_end(block) - 1U;
            const auto& inst = body_.code[last];
            if (inst.op == KOp::jump || inst.op == KOp::call) {
                successors[block] = { block_of[std::min(inst.d, size)] };
            } else if (inst.op == KOp::suspend) {
                // The run ends; it continues at a resume entry.
            } else if (inst.op == KOp::ret) {
                // A return continues at any return target.
                for (const auto target : body_.return_targets) {
                    successors[block].push_back(
                        block_of[std::min<std::size_t>(target, size)]);
                }
            } else if (inst.op == KOp::branch) {
                successors[block] = { block_of[std::min(inst.y, size)],
                    block_of[std::min(inst.z, size)] };
            } else if (inst.op == KOp::mark && inst.sub != 0U) {
                successors[block] = { block_of[last + 1U],
                    block_of[std::min(inst.aux, size)] };
            } else {
                successors[block] = { block_of[last + 1U] };
            }
        }
        // Must-defined registers at block entry: the intersection over
        // predecessors, iterated to a fixed point (loops included).
        std::vector<Bits> in(blocks, Bits(words, ~std::uint64_t { 0 }));
        std::vector<std::uint8_t> reached(blocks, 0U);
        const auto entry_block = block_of[entry];
        if (entry_block == exit_block) {
            return;
        }
        std::ranges::fill(in[entry_block], 0U);
        reached[entry_block] = 1U;
        // A VHDL process's first run enters at operation 0 (its prologue),
        // also with nothing defined.
        const auto prologue_block
            = detail::has_prologue_entry(body_, vhdl_)
            ? block_of[0] : entry_block;
        std::ranges::fill(in[prologue_block], 0U);
        reached[prologue_block] = 1U;
        std::vector<std::uint8_t> run_entry(blocks + 1U, 0U);
        run_entry[entry_block] = 1U;
        run_entry[prologue_block] = 1U;
        for (const auto resume : body_.resume_entries) {
            const auto block = block_of[std::min<std::size_t>(resume, size)];
            if (block != exit_block) {
                std::ranges::fill(in[block], 0U);
                reached[block] = 1U;
                run_entry[block] = 1U;
            }
        }
        Bits state(words, 0U);
        for (bool changed = true; changed;) {
            changed = false;
            for (std::uint32_t block = 0U; block < blocks; ++block) {
                if (reached[block] == 0U) {
                    continue;
                }
                state = in[block];
                for (auto at = block_begin[block]; at < block_end(block); ++at) {
                    const auto& inst = body_.code[at];
                    bool writes = false;
                    (void)operands(inst, writes);
                    if (writes && inst.d < count) {
                        set(state, inst.d);
                    }
                }
                for (const auto successor : successors[block]) {
                    // Each run starts at an entry with nothing defined.
                    if (successor == exit_block || run_entry[successor] != 0U) {
                        continue;
                    }
                    auto& target = in[successor];
                    if (reached[successor] == 0U) {
                        reached[successor] = 1U;
                        target = state;
                        changed = true;
                        continue;
                    }
                    for (std::size_t word = 0U; word < words; ++word) {
                        const auto merged = target[word] & state[word];
                        if (merged != target[word]) {
                            target[word] = merged;
                            changed = true;
                        }
                    }
                }
            }
        }
        Bits live_in(words, 0U);
        Bits written(words, 0U);
        for (std::uint32_t block = 0U; block < blocks; ++block) {
            if (reached[block] == 0U) {
                continue;
            }
            state = in[block];
            for (auto at = block_begin[block]; at < block_end(block); ++at) {
                const auto& inst = body_.code[at];
                bool writes = false;
                (void)operands(inst, writes);
                for (const auto reg : reads) {
                    if (reg < count && !test(state, reg)) {
                        set(live_in, reg);
                    }
                }
                if (writes && inst.d < count) {
                    set(state, inst.d);
                    set(written, inst.d);
                }
            }
        }
        local_.assign(count, nullptr);
        for (std::uint32_t reg = 0U; reg < count; ++reg) {
            if (test(written, reg) && !test(live_in, reg) && !test(pinned, reg)
                && reg < body_.register_widths.size()
                && body_.register_widths[reg] <= 64U && !tracked(reg)
                && (body_.shadow_base == 0U || reg < body_.shadow_base)) {
                // Placeholder; emit() creates the slot.
                local_[reg] = registers_;
            }
        }
    }

    /// Whether both words are defined where the builder inserts: in its
    /// block or in a block of the KIR block's spine (see emit), which
    /// dominates it.
    [[nodiscard]] bool available(const Pair& value) const
    {
        const auto defined = [&](llvm::Value* word) {
            const auto* instruction = llvm::dyn_cast_or_null<llvm::Instruction>(word);
            if (instruction == nullptr) {
                return true;
            }
            const auto* block = instruction->getParent();
            return block == builder_.GetInsertBlock() || spine_.contains(block);
        };
        return defined(value.a) && defined(value.b);
    }

    /// Records a register's value. Only entries written since the last
    /// sweep can be unavailable: the spine only grows within a KIR block and
    /// the builder rests in it after every instruction, so an older entry
    /// stays available.
    void cache_put(const std::uint32_t reg, const Pair value)
    {
        cache_[reg] = value;
        fresh_.push_back(reg);
    }

    /// Drops the entries written since the last sweep that are no longer
    /// available (the whole-cache sweep, restricted to what can have changed).
    void sweep_cache()
    {
        for (const auto reg : fresh_) {
            if (const auto found = cache_.find(reg);
                found != cache_.end() && !available(found->second)) {
                cache_.erase(found);
            }
        }
        fresh_.clear();
    }

    [[nodiscard]] Pair load_register(const std::uint32_t reg)
    {
        if (const auto found = cache_.find(reg);
            found != cache_.end() && available(found->second)) {
            return found->second;
        }
        const auto [base, high] = register_address(reg);
        Pair value { builder_.CreateLoad(i64_, base),
            two_state_ ? constant(0U) : builder_.CreateLoad(i64_, high) };
        cache_put(reg, value);
        return value;
    }

    void store_register(const std::uint32_t reg, const Pair value)
    {
        const auto [base, high] = register_address(reg);
        if (two_state_) {
            // The X plane and 'U' mask stay zero in the register file; a
            // value with either completes the instruction on the reference
            // path. A 'U' mask comes only from a load (last_unknown_).
            builder_.CreateStore(value.a, base);
            if (reg < local_.size() && local_[reg] != nullptr) {
                builder_.CreateStore(constant(0U), high);
            }
            llvm::Value* unknown = tracked(reg) && last_unknown_ != nullptr
                ? last_unknown_ : constant(0U);
            guard_known(value.b, unknown, high,
                tracked(reg) ? register_address(body_.shadow_base + reg).first
                             : nullptr);
            guarded_unknown_ = unknown;
            cache_put(reg, Pair { value.a, constant(0U) });
            return;
        }
        builder_.CreateStore(value.a, base);
        builder_.CreateStore(value.b, high);
        cache_put(reg, value);
    }

    /// Two-state code (see StaticKernelTemplate::two_state) assumes every
    /// register's X plane and 'U' mask are zero. When the value just stored
    /// has X bits `x` or 'U' bits `u`, they are stored too (to `x_address`
    /// and `u_address`) and the template's full code continues after the
    /// instruction, which has completed.
    void guard_known(llvm::Value* x, llvm::Value* u, llvm::Value* x_address,
        llvm::Value* u_address)
    {
        const auto zero = [](llvm::Value* value) {
            const auto* known = llvm::dyn_cast<llvm::ConstantInt>(value);
            return known != nullptr && known->isZero();
        };
        if ((zero(x) && zero(u)) || builder_.GetInsertBlock() == nullptr
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        auto* deopt = llvm::BasicBlock::Create(context_, "unknown", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "known", &function_);
        builder_.CreateCondBr(nonzero(builder_.CreateOr(x, u)), deopt, next);
        builder_.SetInsertPoint(deopt);
        if (x_address != nullptr) {
            builder_.CreateStore(x, x_address);
        }
        if (u_address != nullptr) {
            builder_.CreateStore(u, u_address);
        }
        continue_in_full_code();
        builder_.SetInsertPoint(next);
    }

    /// Ends two-state code: the full code continues at the next instruction.
    void continue_in_full_code()
    {
        builder_.CreateStore(llvm::ConstantInt::get(i32_, continue_point()),
            frame_field(44U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, 4U),
            frame_field(40U));
        builder_.CreateBr(failure_block_);
    }

    /// Builds in the entry block (which dominates every use) once the
    /// body's code is being emitted.
    [[nodiscard]] llvm::IRBuilder<llvm::InstSimplifyFolder>& entry_builder()
    {
        if (auto* terminator = entry_->getTerminator()) {
            entry_builder_.SetInsertPoint(terminator);
        } else {
            entry_builder_.SetInsertPoint(entry_);
        }
        return entry_builder_;
    }

    /// Bindings are fixed for a unit, so each word is read once per run.
    [[nodiscard]] llvm::Value* binding(const std::uint32_t index,
        const std::uint32_t word)
    {
        const auto key = static_cast<std::uint64_t>(index) * 2U + word;
        auto& value = binding_values_[key];
        if (value == nullptr) {
            auto& builder = entry_builder();
            value = builder.CreateLoad(i32_,
                builder.CreateConstInBoundsGEP1_64(i32_, bindings_, key));
        }
        return value;
    }

    /// The first arena word of the slot bound at `index`.
    [[nodiscard]] llvm::Value* slot_pointer(const std::uint32_t index)
    {
        auto& value = slot_pointers_[index];
        if (value == nullptr) {
            auto* offset = binding(index, 0U);
            auto& builder = entry_builder();
            value = builder.CreateInBoundsGEP(i64_, arena_,
                builder.CreateZExt(offset, i64_));
        }
        return value;
    }

    /// Arena word `word` of the slot bound at `index`.
    [[nodiscard]] llvm::Value* slot_word(const std::uint32_t index,
        const std::uint64_t word)
    {
        return builder_.CreateConstInBoundsGEP1_64(i64_, slot_pointer(index), word);
    }

    [[nodiscard]] Pair scalar_bool(llvm::Value* condition)
    {
        return { builder_.CreateZExt(condition, i64_), constant(0U) };
    }

    [[nodiscard]] Pair select(llvm::Value* condition, const Pair left,
        const Pair right)
    {
        return { builder_.CreateSelect(condition, left.a, right.a),
            builder_.CreateSelect(condition, left.b, right.b) };
    }

    [[nodiscard]] Pair x_value(const std::uint32_t width)
    {
        return { constant(mask_of(width)), constant(mask_of(width)) };
    }

    [[nodiscard]] llvm::Value* nonzero(llvm::Value* value)
    {
        return builder_.CreateICmpNE(value, constant(0U));
    }

    /// A loop-carried PHI at the insert point (the loop head), entered from
    /// `before` with `initial` and from `step` with an opaque placeholder
    /// until close_loop_phi gives the real value. builder_'s
    /// InstSimplifyFolder threads compares, shifts and bitwise operations
    /// over a PHI's incoming values, so a PHI holding only its initial value
    /// would fold the first iteration into every iteration. `step` gets its
    /// branch back to the head first, so the analyses that follow the
    /// placeholder's edge find a terminator; its body goes before that
    /// branch.
    [[nodiscard]] llvm::PHINode* loop_phi(llvm::Type* type, llvm::Value* initial,
        llvm::BasicBlock* before, llvm::BasicBlock* step)
    {
        if (step->getTerminator() == nullptr) {
            llvm::BranchInst::Create(builder_.GetInsertBlock(), step);
        }
        auto* phi = builder_.CreatePHI(type, 2U);
        phi->addIncoming(initial, before);
        phi->addIncoming(new llvm::FreezeInst(llvm::PoisonValue::get(type), "",
                             step->getTerminator()->getIterator()),
            step);
        return phi;
    }

    static void close_loop_phi(llvm::PHINode* phi, llvm::Value* next)
    {
        auto* placeholder = llvm::cast<llvm::Instruction>(phi->getIncomingValue(1U));
        phi->setIncomingValue(1U, next);
        placeholder->eraseFromParent();
    }

    [[nodiscard]] llvm::Value* bit_not(llvm::Value* value)
    {
        return builder_.CreateXor(value, constant(~std::uint64_t { 0 }));
    }

    /// truth_value(): returns (is_one, is_unknown) i1 values.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> truth(const Pair value)
    {
        auto* one = nonzero(builder_.CreateAnd(value.a, bit_not(value.b)));
        auto* unknown = builder_.CreateAnd(builder_.CreateNot(one),
            nonzero(value.b));
        return { one, unknown };
    }

    [[nodiscard]] Pair from_truth(llvm::Value* one, llvm::Value* unknown)
    {
        // one -> (1,0); unknown -> (1,1); else (0,0).
        auto* a = builder_.CreateZExt(builder_.CreateOr(one, unknown), i64_);
        auto* b = builder_.CreateZExt(unknown, i64_);
        return { a, b };
    }

    [[nodiscard]] llvm::Value* sign_extend(llvm::Value* value,
        const std::uint32_t width)
    {
        if (width >= 64U) {
            return value;
        }
        auto* shift = constant(64U - width);
        return builder_.CreateAShr(builder_.CreateShl(value, shift), shift);
    }

    [[nodiscard]] Pair emit_binary(const KInst& inst, const Pair l, const Pair r)
    {
        const auto width = inst.width;
        auto* m = constant(mask_of(width));
        auto* unknown = builder_.CreateAnd(builder_.CreateOr(l.b, r.b), m);
        auto* has_unknown = nonzero(unknown);
        const auto operation = static_cast<BinaryOperator>(inst.sub);
        switch (operation) {
        case BinaryOperator::bit_and: {
            auto* zero = builder_.CreateOr(
                builder_.CreateAnd(bit_not(l.a), bit_not(l.b)),
                builder_.CreateAnd(bit_not(r.a), bit_not(r.b)));
            auto* one = builder_.CreateAnd(
                builder_.CreateAnd(l.a, bit_not(l.b)),
                builder_.CreateAnd(r.a, bit_not(r.b)));
            auto* x = builder_.CreateAnd(bit_not(builder_.CreateOr(zero, one)), m);
            return { builder_.CreateAnd(builder_.CreateOr(one, x), m), x };
        }
        case BinaryOperator::bit_or: {
            auto* one = builder_.CreateOr(builder_.CreateAnd(l.a, bit_not(l.b)),
                builder_.CreateAnd(r.a, bit_not(r.b)));
            auto* zero = builder_.CreateAnd(
                builder_.CreateAnd(bit_not(l.a), bit_not(l.b)),
                builder_.CreateAnd(bit_not(r.a), bit_not(r.b)));
            auto* x = builder_.CreateAnd(bit_not(builder_.CreateOr(zero, one)), m);
            return { builder_.CreateAnd(builder_.CreateOr(one, x), m), x };
        }
        case BinaryOperator::bit_xor:
            return { builder_.CreateAnd(
                         builder_.CreateOr(builder_.CreateXor(l.a, r.a), unknown),
                         m),
                unknown };
        case BinaryOperator::equal:
        case BinaryOperator::not_equal: {
            // A known differing bit decides the relation (IEEE 1800-2017
            // 11.4.5).
            auto* differ = nonzero(builder_.CreateAnd(
                builder_.CreateAnd(builder_.CreateXor(l.a, r.a), bit_not(unknown)),
                m));
            return select(differ,
                scalar_bool(operation == BinaryOperator::not_equal
                        ? builder_.getTrue()
                        : builder_.getFalse()),
                select(has_unknown, x_value(1U),
                    scalar_bool(operation == BinaryOperator::not_equal
                            ? builder_.getFalse()
                            : builder_.getTrue())));
        }
        case BinaryOperator::case_equal:
            // 'U' and X differ only in the 'U' mask, 'H' and 1 (and 'L' and
            // 0, 'W' and X, '-' and Z) only in the weak mask.
            if (weak_mode()) {
                return scalar_bool(builder_.CreateAnd(
                    builder_.CreateAnd(builder_.CreateAnd(builder_.CreateICmpEQ(l.a, r.a),
                                           builder_.CreateICmpEQ(l.b, r.b)),
                        builder_.CreateICmpEQ(unknown_of(inst.x), unknown_of(inst.y))),
                    builder_.CreateICmpEQ(weak_of(inst.x), weak_of(inst.y))));
            }
            return scalar_bool(builder_.CreateAnd(
                builder_.CreateAnd(builder_.CreateICmpEQ(l.a, r.a),
                    builder_.CreateICmpEQ(l.b, r.b)),
                builder_.CreateICmpEQ(unknown_of(inst.x), unknown_of(inst.y))));
        case BinaryOperator::casez_equal: {
            auto* z = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateAnd(bit_not(l.a), l.b),
                    builder_.CreateAnd(bit_not(r.a), r.b)),
                m);
            auto* differ = builder_.CreateAnd(
                builder_.CreateAnd(builder_.CreateOr(builder_.CreateXor(l.a, r.a),
                                       builder_.CreateXor(l.b, r.b)),
                    m),
                bit_not(z));
            return scalar_bool(builder_.CreateICmpEQ(differ, constant(0U)));
        }
        case BinaryOperator::casex_equal: {
            auto* differ = builder_.CreateAnd(
                builder_.CreateAnd(builder_.CreateOr(builder_.CreateXor(l.a, r.a),
                                       builder_.CreateXor(l.b, r.b)),
                    m),
                bit_not(unknown));
            return scalar_bool(builder_.CreateICmpEQ(differ, constant(0U)));
        }
        case BinaryOperator::wildcard_equal: {
            auto* care = builder_.CreateAnd(bit_not(r.b), m);
            auto* left_unknown = nonzero(builder_.CreateAnd(l.b, care));
            auto* known_differ = nonzero(builder_.CreateAnd(
                builder_.CreateXor(l.a, r.a),
                builder_.CreateAnd(care, bit_not(l.b))));
            auto* equal = builder_.CreateICmpEQ(
                builder_.CreateAnd(builder_.CreateXor(l.a, r.a), care),
                constant(0U));
            return select(known_differ, scalar_bool(builder_.getFalse()),
                select(left_unknown, x_value(1U), scalar_bool(equal)));
        }
        case BinaryOperator::less_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpULT(l.a, r.a)));
        case BinaryOperator::less_equal_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpULE(l.a, r.a)));
        case BinaryOperator::greater_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpUGT(l.a, r.a)));
        case BinaryOperator::greater_equal_unsigned:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpUGE(l.a, r.a)));
        case BinaryOperator::less_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSLT(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::less_equal_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSLE(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::greater_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSGT(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::greater_equal_signed:
            return select(has_unknown, x_value(1U),
                scalar_bool(builder_.CreateICmpSGE(sign_extend(l.a, width),
                    sign_extend(r.a, width))));
        case BinaryOperator::add_unsigned:
        case BinaryOperator::add_signed:
            return select(has_unknown, x_value(width),
                { builder_.CreateAnd(builder_.CreateAdd(l.a, r.a), m),
                    constant(0U) });
        case BinaryOperator::subtract_unsigned:
        case BinaryOperator::subtract_signed:
            return select(has_unknown, x_value(width),
                { builder_.CreateAnd(builder_.CreateSub(l.a, r.a), m),
                    constant(0U) });
        case BinaryOperator::multiply_unsigned:
        case BinaryOperator::multiply_signed:
            return select(has_unknown, x_value(width),
                { builder_.CreateAnd(builder_.CreateMul(l.a, r.a), m),
                    constant(0U) });
        case BinaryOperator::divide_unsigned:
        case BinaryOperator::modulo_unsigned: {
            // IEEE 1800-2023 11.4.3: a zero divisor, like an unknown bit,
            // makes the result all X.
            auto* zero = builder_.CreateICmpEQ(r.a, constant(0U));
            auto* divisor = builder_.CreateSelect(zero, constant(1U), r.a);
            auto* value = operation == BinaryOperator::divide_unsigned
                ? builder_.CreateUDiv(l.a, divisor)
                : builder_.CreateURem(l.a, divisor);
            return select(builder_.CreateOr(has_unknown, zero), x_value(width),
                { builder_.CreateAnd(value, m), constant(0U) });
        }
        case BinaryOperator::divide_signed:
        case BinaryOperator::remainder_signed:
        case BinaryOperator::modulo_signed: {
            // On magnitudes, as kernel_word::binary: the quotient is
            // negative when the signs differ, the remainder takes the
            // dividend's sign (IEEE 1800-2023 11.4.3), and a modulo the
            // divisor's (1076-2019 9.2.7).
            auto* sign = constant(std::uint64_t { 1 } << (width - 1U));
            auto* left_negative = nonzero(builder_.CreateAnd(l.a, sign));
            auto* right_negative = nonzero(builder_.CreateAnd(r.a, sign));
            auto* left_magnitude = builder_.CreateSelect(left_negative,
                builder_.CreateAnd(builder_.CreateSub(constant(0U), l.a), m), l.a);
            auto* right_magnitude = builder_.CreateSelect(right_negative,
                builder_.CreateAnd(builder_.CreateSub(constant(0U), r.a), m), r.a);
            auto* zero = builder_.CreateICmpEQ(r.a, constant(0U));
            auto* divisor = builder_.CreateSelect(zero, constant(1U), right_magnitude);
            auto* quotient = builder_.CreateAnd(
                builder_.CreateUDiv(left_magnitude, divisor), m);
            auto* remainder = builder_.CreateAnd(
                builder_.CreateURem(left_magnitude, divisor), m);
            auto* differ = builder_.CreateXor(left_negative, right_negative);
            quotient = builder_.CreateSelect(differ,
                builder_.CreateAnd(builder_.CreateSub(constant(0U), quotient), m),
                quotient);
            remainder = builder_.CreateSelect(left_negative,
                builder_.CreateAnd(builder_.CreateSub(constant(0U), remainder), m),
                remainder);
            llvm::Value* value = remainder;
            if (operation == BinaryOperator::divide_signed) {
                value = quotient;
            } else if (operation == BinaryOperator::modulo_signed) {
                value = builder_.CreateSelect(
                    builder_.CreateAnd(nonzero(remainder), differ),
                    builder_.CreateAnd(builder_.CreateAdd(remainder, r.a), m), remainder);
            }
            return select(builder_.CreateOr(has_unknown, zero), x_value(width),
                { value, constant(0U) });
        }
        case BinaryOperator::power_unsigned:
        case BinaryOperator::power_signed: {
            // kernel_word::power_known (IEEE 1800-2023 11.4.3, Table 11-4): a
            // negative signed exponent gives 1 for a base of 1, the base's
            // parity for -1 and 0 otherwise (all X for a zero base).
            const bool signed_exponent = operation == BinaryOperator::power_signed;
            auto* negative = signed_exponent
                ? nonzero(builder_.CreateAnd(
                      builder_.CreateLShr(r.a, constant(width - 1U)), constant(1U)))
                : llvm::ConstantInt::getFalse(context_);
            auto* base = builder_.CreateAnd(l.a, m);
            auto* undefined = builder_.CreateAnd(negative,
                builder_.CreateICmpEQ(l.a, constant(0U)));
            auto* odd = nonzero(builder_.CreateAnd(r.a, constant(1U)));
            auto* inverse = builder_.CreateSelect(
                builder_.CreateICmpEQ(base, constant(1U)), constant(1U),
                builder_.CreateSelect(builder_.CreateICmpEQ(base, m),
                    builder_.CreateSelect(odd, base, constant(1U)), constant(0U)));
            auto* product = emit_power_loop(base,
                builder_.CreateSelect(negative, constant(0U), r.a), m);
            return select(builder_.CreateOr(has_unknown, undefined), x_value(width),
                { builder_.CreateAnd(builder_.CreateSelect(negative, inverse, product), m),
                    constant(0U) });
        }
        case BinaryOperator::vhdl_match_equal: {
            const auto parts = vhdl_match_parts(inst, l, r);
            return select(nonzero(parts.mismatch), Pair { constant(0U), constant(0U) },
                select(builder_.CreateOr(nonzero(parts.uninitialized), nonzero(parts.unknown)),
                    x_value(1U), Pair { constant(1U), constant(0U) }));
        }
        }
        return x_value(width);
    }

    /// base ** exponent over `m`-masked words by repeated squaring, as the
    /// loop of kernel_word::power_known.
    [[nodiscard]] llvm::Value* emit_power_loop(llvm::Value* base, llvm::Value* exponent,
        llvm::Value* m)
    {
        auto* before = builder_.GetInsertBlock();
        auto* header = llvm::BasicBlock::Create(context_, "pow", &function_);
        auto* step = llvm::BasicBlock::Create(context_, "pow_step", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "pow_done", &function_);
        builder_.CreateBr(header);
        builder_.SetInsertPoint(header);
        auto* accumulated = loop_phi(i64_, builder_.CreateAnd(constant(1U), m), before, step);
        auto* factor = loop_phi(i64_, base, before, step);
        auto* remaining = loop_phi(i64_, exponent, before, step);
        builder_.CreateCondBr(nonzero(remaining), step, done);
        builder_.SetInsertPoint(step->getTerminator());
        auto* odd = nonzero(builder_.CreateAnd(remaining, constant(1U)));
        close_loop_phi(accumulated,
            builder_.CreateSelect(odd,
                builder_.CreateAnd(builder_.CreateMul(accumulated, factor), m), accumulated));
        close_loop_phi(factor, builder_.CreateAnd(builder_.CreateMul(factor, factor), m));
        close_loop_phi(remaining, builder_.CreateLShr(remaining, constant(1U)));
        builder_.SetInsertPoint(done);
        return accumulated;
    }

    /// The facts the VHDL matching equality ("?=", IEEE 1076-2019 9.2.3 over
    /// std_ulogic) takes from its operands' elements, as bit masks:
    /// a known pair that differs, 'U' in a pair neither of whose elements
    /// is '-', and X, Z or 'W' in such a pair. '-' (the word Z with its
    /// weak bit) matches anything, 'L' and 'H' count as 0 and 1.
    struct MatchParts {
        llvm::Value* mismatch { };
        llvm::Value* uninitialized { };
        llvm::Value* unknown { };
    };

    [[nodiscard]] MatchParts vhdl_match_parts(const KInst& inst, const Pair l, const Pair r)
    {
        auto* m = constant(mask_of(inst.width));
        auto* left_u = unknown_of(inst.x);
        auto* right_u = unknown_of(inst.y);
        const auto dont_care = [&](const Pair value, llvm::Value* weak) {
            return builder_.CreateAnd(builder_.CreateAnd(weak, bit_not(value.a)), value.b);
        };
        auto* active = builder_.CreateAnd(
            bit_not(builder_.CreateOr(dont_care(l, weak_of(inst.x)),
                dont_care(r, weak_of(inst.y)))),
            m);
        auto* meta = builder_.CreateOr(l.b, r.b);
        auto* either_u = builder_.CreateOr(left_u, right_u);
        return { builder_.CreateAnd(
                     builder_.CreateAnd(builder_.CreateXor(l.a, r.a), bit_not(meta)), active),
            builder_.CreateAnd(either_u, active),
            builder_.CreateAnd(builder_.CreateAnd(meta, active), bit_not(either_u)) };
    }

    [[nodiscard]] Pair emit_reduce(const KInst& inst, const Pair value)
    {
        auto* m = constant(mask_of(inst.width));
        auto* ones = builder_.CreateAnd(builder_.CreateAnd(value.a, bit_not(value.b)), m);
        auto* zeros = builder_.CreateAnd(
            builder_.CreateAnd(bit_not(value.a), bit_not(value.b)), m);
        auto* unknown = nonzero(builder_.CreateAnd(value.b, m));
        // The fast instruction selector has no population count: outside the
        // optimizing tier, one-hot tests clear the lowest set bit and parity
        // folds the word, so a reduction does not send its block to the
        // full selector.
        const auto at_most_one = [&] {
            return builder_.CreateICmpEQ(
                builder_.CreateAnd(ones, builder_.CreateSub(ones, constant(1U))),
                constant(0U));
        };
        switch (static_cast<ReductionOperator>(inst.sub)) {
        case ReductionOperator::one_hot:
            if (!optimizing_) {
                return scalar_bool(builder_.CreateAnd(
                    builder_.CreateICmpNE(ones, constant(0U)), at_most_one()));
            }
            return scalar_bool(builder_.CreateICmpEQ(
                builder_.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, ones),
                constant(1U)));
        case ReductionOperator::one_hot_or_zero:
            if (!optimizing_) {
                return scalar_bool(at_most_one());
            }
            return scalar_bool(builder_.CreateICmpULE(
                builder_.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, ones),
                constant(1U)));
        case ReductionOperator::bit_and:
            return select(nonzero(zeros), Pair { constant(0U), constant(0U) },
                select(unknown, x_value(1U), Pair { constant(1U), constant(0U) }));
        case ReductionOperator::bit_or:
            return select(nonzero(ones), Pair { constant(1U), constant(0U) },
                select(unknown, x_value(1U), Pair { constant(0U), constant(0U) }));
        case ReductionOperator::bit_xor: {
            llvm::Value* parity = ones;
            if (optimizing_) {
                parity = builder_.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, ones);
            } else {
                for (std::uint32_t shift = 32U; shift != 0U; shift /= 2U) {
                    parity = builder_.CreateXor(parity,
                        builder_.CreateLShr(parity, constant(shift)));
                }
            }
            return select(unknown, x_value(1U),
                Pair { builder_.CreateAnd(parity, constant(1U)), constant(0U) });
        }
        }
        return x_value(1U);
    }

    /// A runtime helper, called by name so that code does not depend on
    /// where this process loaded the runtime (see helper_symbols).
    /// The step code's shared return switch (KOp::step, Return), made on
    /// first use: a position that is not a return target goes back to the
    /// runner in `reserved`, as a step's other positions do.
    [[nodiscard]] llvm::PHINode* return_dispatch(const std::uint32_t size)
    {
        if (return_dispatch_ != nullptr) {
            return return_next_;
        }
        const auto saved = builder_.saveIP();
        return_dispatch_ = llvm::BasicBlock::Create(context_, "return", &function_);
        auto* leave = llvm::BasicBlock::Create(context_, "return_leave", &function_);
        builder_.SetInsertPoint(return_dispatch_);
        return_next_ = builder_.CreatePHI(i32_, 4U);
        std::vector<std::uint32_t> targets;
        for (const auto target : body_.return_targets) {
            if (target < size && blocks_[target] != nullptr) {
                targets.push_back(target);
            }
        }
        auto* targets_switch = builder_.CreateSwitch(return_next_, leave,
            static_cast<unsigned>(targets.size()));
        for (const auto target : targets) {
            targets_switch->addCase(
                llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i32_), target),
                blocks_[target]);
        }
        builder_.SetInsertPoint(leave);
        builder_.CreateStore(return_next_, frame_field(44U));
        builder_.CreateRet(llvm::ConstantInt::get(i32_, 3U));
        builder_.restoreIP(saved);
        return return_next_;
    }

    /// Step code's shared exit to the runner, its position already in
    /// `reserved` (KOp::step).
    [[nodiscard]] llvm::BasicBlock* step_leave()
    {
        if (step_leave_ == nullptr) {
            const auto saved = builder_.saveIP();
            step_leave_ = llvm::BasicBlock::Create(context_, "leave", &function_);
            builder_.SetInsertPoint(step_leave_);
            builder_.CreateRet(llvm::ConstantInt::get(i32_, 3U));
            builder_.restoreIP(saved);
        }
        return step_leave_;
    }

    [[nodiscard]] llvm::Value* helper_symbol(const char* name,
        llvm::FunctionType* type)
    {
        return module_.getOrInsertFunction(name, type).getCallee();
    }

    [[nodiscard]] Pair call_helper(const char* helper, const std::uint32_t at,
        const Pair x, const Pair y, const Pair z)
    {
        auto* out = out_;
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_, i64_, i64_, i64_, i64_, i64_, ptr_ }, false);
        auto* callee = helper_symbol(helper, type);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at), x.a, x.b, y.a, y.b, z.a,
                z.b, out });
        check_status();
        last_unknown_ = builder_.CreateLoad(i64_,
            builder_.CreateConstInBoundsGEP1_64(i64_, out, 2U));
        last_weak_ = weak_mode()
            ? builder_.CreateLoad(i64_, builder_.CreateConstInBoundsGEP1_64(i64_, out, 3U))
            : nullptr;
        return { builder_.CreateLoad(i64_, out),
            builder_.CreateLoad(i64_,
                builder_.CreateConstInBoundsGEP1_64(i64_, out, 1U)) };
    }

    /// The evaluate or effect helper of instruction `at`'s kind.
    [[nodiscard]] Pair call_evaluate(const std::uint32_t at, const Pair x,
        const Pair y, const Pair z, const unsigned category = 0U)
    {
        note_helper_site(body_.code[at].op, category);
        return call_helper(instruction_helper_name("fsim_sk_evaluate_",
                               body_.code[at].op).c_str(),
            at, x, y, z);
    }

    void call_effect(const std::uint32_t at, const Pair x, const Pair y,
        const Pair z, const unsigned category = 0U)
    {
        note_helper_site(body_.code[at].op, category);
        (void)call_helper(instruction_helper_name("fsim_sk_effect_",
                              body_.code[at].op).c_str(),
            at, x, y, z);
    }

    /// The wide-register helper of instruction `at`'s kind
    /// (`fsim_sk_wide_<op>`): it reads and writes wide values in the
    /// register file. Narrow operands pass by value (x carries y's 'U'
    /// mask), except a concatenation's, which it reads from the register
    /// file: local ones are stored there first.
    [[nodiscard]] Pair call_wide(const std::uint32_t at, const Pair x,
        const Pair y, const Pair z, const unsigned category = 0U)
    {
        const auto& inst = body_.code[at];
        note_helper_site(inst.op, category);
        if (inst.op == KOp::wide_concat) {
            for (std::uint32_t index = 0U; index < inst.x; ++index) {
                const auto reg = body_.concat[inst.aux + index].reg;
                if (body_.concat[inst.aux + index].width > 64U
                    || reg >= local_.size() || local_[reg] == nullptr) {
                    continue;
                }
                const auto value = load_register(reg);
                builder_.CreateStore(value.a, chunk_address(reg, 0U));
                builder_.CreateStore(value.b, chunk_address(reg, 1U));
            }
        }
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, ptr_, i32_, i64_, i64_, i64_, i64_, i64_, i64_, ptr_ }, false);
        auto* callee = helper_symbol(
            instruction_helper_name("fsim_sk_wide_", inst.op).c_str(), type);
        builder_.CreateCall(type, callee,
            { frame_, registers_, llvm::ConstantInt::get(i32_, at), x.a, x.b, y.a,
                y.b, z.a, z.b, out_ });
        check_status();
        last_unknown_ = builder_.CreateLoad(i64_,
            builder_.CreateConstInBoundsGEP1_64(i64_, out_, 2U));
        last_weak_ = weak_mode()
            ? builder_.CreateLoad(i64_, builder_.CreateConstInBoundsGEP1_64(i64_, out_, 3U))
            : nullptr;
        return { builder_.CreateLoad(i64_, out_),
            builder_.CreateLoad(i64_,
                builder_.CreateConstInBoundsGEP1_64(i64_, out_, 1U)) };
    }

    /// Word `word` of register-file entry `reg` (wide storage is never
    /// local or cached).
    [[nodiscard]] llvm::Value* chunk_address(const std::uint32_t reg,
        const std::uint64_t word)
    {
        return builder_.CreateConstInBoundsGEP1_64(i64_, registers_,
            static_cast<std::uint64_t>(reg) * 2U + word);
    }

    /// Bits [offset, offset + width) of the wide register at storage index
    /// `base` (`chunks` chunks; with 'U' masks when `masks`), as
    /// wide_field in the reference evaluator: the value and its 'U' mask.
    [[nodiscard]] std::pair<Pair, llvm::Value*> wide_field(const std::uint32_t base,
        const std::uint32_t chunks, const bool masks, const std::uint32_t offset,
        const std::uint32_t width)
    {
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        auto* m = constant(mask_of(width));
        if (word >= chunks) {
            return { { m, m }, constant(0U) };
        }
        const bool spans = shift != 0U && shift + width > 64U && word + 1U < chunks;
        const auto field = [&](const std::uint32_t first, const std::uint64_t plane) {
            llvm::Value* value = builder_.CreateLShr(
                builder_.CreateLoad(i64_, chunk_address(first + word, plane)),
                constant(shift));
            if (spans) {
                value = builder_.CreateOr(value,
                    builder_.CreateShl(
                        builder_.CreateLoad(i64_, chunk_address(first + word + 1U, plane)),
                        constant(64U - shift)));
            }
            return builder_.CreateAnd(value, m);
        };
        return { { field(base, 0U), field(base, 1U) },
            masks ? field(base + chunks, 0U) : constant(0U) };
    }

    /// The weak mask of bits [offset, offset + width) of the wide register at
    /// storage index `base` (`chunks` chunks, with masks): the `b` words of
    /// its mask chunks.
    [[nodiscard]] llvm::Value* wide_weak_field(const std::uint32_t base,
        const std::uint32_t chunks, const std::uint32_t offset,
        const std::uint32_t width)
    {
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        auto* m = constant(mask_of(width));
        if (word >= chunks) {
            return constant(0U);
        }
        const bool spans = shift != 0U && shift + width > 64U && word + 1U < chunks;
        llvm::Value* value = builder_.CreateLShr(
            builder_.CreateLoad(i64_, chunk_address(base + chunks + word, 1U)),
            constant(shift));
        if (spans) {
            value = builder_.CreateOr(value,
                builder_.CreateShl(
                    builder_.CreateLoad(i64_, chunk_address(base + chunks + word + 1U, 1U)),
                    constant(64U - shift)));
        }
        return builder_.CreateAnd(value, m);
    }

    void check_status()
    {
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        auto* failed = builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U));
        auto* next = llvm::BasicBlock::Create(context_, "ok", &function_);
        builder_.CreateCondBr(failed, failure_block_, next);
        builder_.SetInsertPoint(next);
        // Helpers may write slots; slot loads must not be reused across them.
    }

    /// A condition the builder folded to `value`: its branch is never
    /// taken the other way, so no block is created for that side (with
    /// the unoptimized backend every block boundary spills live values).
    [[nodiscard]] static bool folded(llvm::Value* condition, const bool value)
    {
        const auto* known = llvm::dyn_cast<llvm::ConstantInt>(condition);
        return known != nullptr && known->isOne() == value;
    }

    [[nodiscard]] Pair zero_pair()
    {
        return { constant(0U), constant(0U) };
    }

    /// Fast path in a fresh block; the slow path calls the evaluate helper.
    /// Returns the merged result in the continuation block.
    [[nodiscard]] Pair with_fallback(llvm::Value* fast_ok, const Pair fast,
        const std::uint32_t at, const Pair x, const Pair y,
        llvm::Value* fast_unknown = nullptr, llvm::Value* z_a = nullptr,
        llvm::Value* fast_weak = nullptr, llvm::Value* z_w = nullptr)
    {
        if (folded(fast_ok, true) || (two_state_ && hand_over_slow_paths_)) {
            // Two-state code leaves a slow path (an error or an unusual
            // value) to the full code, which redoes the instruction.
            if (!folded(fast_ok, true)) {
                read_or_hand_over(fast_ok);
            }
            last_unknown_ = fast_unknown != nullptr ? fast_unknown : constant(0U);
            last_weak_ = fast_weak;
            return fast;
        }
        auto* fast_block = builder_.GetInsertBlock();
        auto* slow = llvm::BasicBlock::Create(context_, "slow", &function_);
        auto* merge = llvm::BasicBlock::Create(context_, "merge", &function_);
        builder_.CreateCondBr(fast_ok, merge, slow);
        builder_.SetInsertPoint(slow);
        const auto slow_value = call_evaluate(at, x, y,
            z_a != nullptr ? Pair { z_a, z_w != nullptr ? z_w : constant(0U) }
                           : zero_pair());
        auto* slow_end = builder_.GetInsertBlock();
        builder_.CreateBr(merge);
        builder_.SetInsertPoint(merge);
        auto* a = builder_.CreatePHI(i64_, 2U);
        a->addIncoming(fast.a, fast_block);
        a->addIncoming(slow_value.a, slow_end);
        auto* b = builder_.CreatePHI(i64_, 2U);
        b->addIncoming(fast.b, fast_block);
        b->addIncoming(slow_value.b, slow_end);
        // The fast path produces 'U' only where it says so.
        auto* unknown = builder_.CreatePHI(i64_, 2U);
        unknown->addIncoming(fast_unknown != nullptr ? fast_unknown : constant(0U),
            fast_block);
        unknown->addIncoming(last_unknown_, slow_end);
        last_unknown_ = unknown;
        if (weak_mode() && last_weak_ != nullptr) {
            // The fast path produces no weak element.
            auto* weak = builder_.CreatePHI(i64_, 2U);
            weak->addIncoming(fast_weak != nullptr ? fast_weak : constant(0U),
                fast_block);
            weak->addIncoming(last_weak_, slow_end);
            last_weak_ = weak;
        }
        return { a, b };
    }

    /// Inline kernel_word::shift (IEEE 1800-2023 11.4.10 shifts; 1076-2019
    /// 9.2.3 sll, srl, sla, sra, rol, ror, whose signed amount reverses the
    /// direction): every operator, a signed amount, and an unknown amount
    /// (all X). `value` is a word of inst.width bits, the amount one of
    /// inst.offset bits.
    [[nodiscard]] Pair emit_shift(const KInst& inst, const std::uint32_t at,
        const Pair value, const Pair amount)
    {
        (void)at;
        using runtime::simir::ShiftOperator;
        const auto operation = static_cast<ShiftOperator>(inst.sub);
        const bool signed_amount = (inst.flags & detail::flag_signed) != 0U;
        const auto width = inst.width;
        const auto amount_width = inst.offset;
        auto* m = constant(mask_of(width));
        auto* amount_mask = constant(mask_of(amount_width));
        auto* unknown = nonzero(builder_.CreateAnd(amount.b, amount_mask));
        auto* magnitude = builder_.CreateAnd(amount.a, amount_mask);
        // The bit `index` of `plane` in every position of the value.
        const auto replicated = [&](llvm::Value* plane, const std::uint32_t index) {
            return builder_.CreateAnd(
                builder_.CreateNeg(builder_.CreateAnd(
                    builder_.CreateLShr(plane, constant(index)), constant(1U))),
                m);
        };
        const auto by = [&](const ShiftOperator op, llvm::Value* count) -> Pair {
            const bool left = op == ShiftOperator::logical_left
                || op == ShiftOperator::arithmetic_left
                || op == ShiftOperator::rotate_left;
            if (op == ShiftOperator::rotate_left || op == ShiftOperator::rotate_right) {
                auto* amt = builder_.CreateURem(count, constant(width));
                // width - amt is the complementary distance; 0 for a zero
                // rotation, which then leaves the value as it is.
                auto* inverse = builder_.CreateSelect(
                    builder_.CreateICmpEQ(amt, constant(0U)), constant(0U),
                    builder_.CreateSub(constant(width), amt));
                const auto rotate = [&](llvm::Value* plane) -> llvm::Value* {
                    return builder_.CreateAnd(
                        left ? builder_.CreateOr(builder_.CreateShl(plane, amt),
                                   builder_.CreateLShr(plane, inverse))
                             : builder_.CreateOr(builder_.CreateLShr(plane, amt),
                                   builder_.CreateShl(plane, inverse)),
                        m);
                };
                return { rotate(value.a), rotate(value.b) };
            }
            Pair fill { constant(0U), constant(0U) };
            if (op == ShiftOperator::arithmetic_right) {
                fill = { replicated(value.a, width - 1U), replicated(value.b, width - 1U) };
            } else if (op == ShiftOperator::arithmetic_left) {
                fill = { replicated(value.a, 0U), replicated(value.b, 0U) };
            }
            auto* full = builder_.CreateICmpUGE(count, constant(width));
            auto* amt = builder_.CreateSelect(full, constant(0U), count);
            Pair shifted;
            if (left) {
                auto* low = builder_.CreateSub(
                    builder_.CreateShl(constant(1U), amt), constant(1U));
                shifted = { builder_.CreateOr(
                                builder_.CreateAnd(builder_.CreateShl(value.a, amt), m),
                                builder_.CreateAnd(fill.a, low)),
                    builder_.CreateOr(
                        builder_.CreateAnd(builder_.CreateShl(value.b, amt), m),
                        builder_.CreateAnd(fill.b, low)) };
            } else {
                auto* keep = builder_.CreateLShr(m, amt);
                auto* high = builder_.CreateAnd(m, bit_not(keep));
                shifted = { builder_.CreateOr(
                                builder_.CreateAnd(builder_.CreateLShr(value.a, amt), keep),
                                builder_.CreateAnd(fill.a, high)),
                    builder_.CreateOr(
                        builder_.CreateAnd(builder_.CreateLShr(value.b, amt), keep),
                        builder_.CreateAnd(fill.b, high)) };
            }
            return select(full, fill, shifted);
        };
        Pair result = by(operation, magnitude);
        if (signed_amount) {
            // A negative amount shifts the other way by its magnitude.
            const auto flipped = [&] {
                switch (operation) {
                case ShiftOperator::logical_left:
                    return ShiftOperator::logical_right;
                case ShiftOperator::logical_right:
                    return ShiftOperator::logical_left;
                case ShiftOperator::arithmetic_left:
                    return ShiftOperator::arithmetic_right;
                case ShiftOperator::arithmetic_right:
                    return ShiftOperator::arithmetic_left;
                case ShiftOperator::rotate_left:
                    return ShiftOperator::rotate_right;
                case ShiftOperator::rotate_right:
                    return ShiftOperator::rotate_left;
                }
                return operation;
            }();
            auto* negative = nonzero(builder_.CreateAnd(
                builder_.CreateLShr(magnitude, constant(amount_width - 1U)), constant(1U)));
            auto* reversed = builder_.CreateAnd(
                builder_.CreateSub(constant(0U), magnitude), amount_mask);
            result = select(negative, by(flipped, reversed), result);
        }
        last_unknown_ = constant(0U);
        return select(unknown, x_value(width), result);
    }

    [[nodiscard]] llvm::Value* signed_index(const Pair index)
    {
        return builder_.CreateSExt(builder_.CreateTrunc(index.a, i32_), i64_);
    }

    [[nodiscard]] Pair emit_dynamic_extract(const KInst& inst,
        const std::uint32_t at, const Pair source, const Pair index)
    {
        const auto& selection = body_.indices[inst.aux];
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* fits = builder_.CreateICmpULT(offset, constant(inst.width));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        const Pair fast { builder_.CreateAnd(builder_.CreateLShr(source.a, safe), constant(1U)),
            builder_.CreateAnd(builder_.CreateLShr(source.b, safe), constant(1U)) };
        // The source's 'U' and weak elements select with it.
        auto* source_unknown = unknown_of(inst.x);
        llvm::Value* source_weak = weak_mode() && tracked(inst.x) ? weak_of(inst.x) : nullptr;
        auto* bit_unknown = builder_.CreateAnd(builder_.CreateLShr(source_unknown, safe),
            constant(1U));
        llvm::Value* bit_weak = source_weak != nullptr
            ? builder_.CreateAnd(builder_.CreateLShr(source_weak, safe), constant(1U))
            : nullptr;
        if (two_state_) {
            // Two-state code leaves an invalid index to the full code.
            read_or_hand_over(ok);
            last_unknown_ = bit_unknown;
            last_weak_ = bit_weak;
            return fast;
        }
        if ((inst.flags & detail::flag_strict) != 0U) {
            // Strict (IEEE 1800-2023 11.5.1; 1076-2019 9.3.3.2): an unknown
            // index or one outside the declared range is reason 3, one past
            // the source reason 4 (native_fail_message).
            check_or_fail(ok, at,
                builder_.CreateSelect(builder_.CreateAnd(known, in_range),
                    llvm::ConstantInt::get(i32_, 4U), llvm::ConstantInt::get(i32_, 3U)));
            last_unknown_ = bit_unknown;
            last_weak_ = bit_weak;
            return fast;
        }
        // Otherwise an invalid index reads X: neither 'U' nor weak.
        last_unknown_ = builder_.CreateSelect(ok, bit_unknown, constant(0U));
        last_weak_ = bit_weak != nullptr
            ? builder_.CreateSelect(ok, bit_weak, constant(0U)) : nullptr;
        return select(ok, fast, x_value(1U));
    }

    [[nodiscard]] Pair emit_dynamic_part_select(const KInst& inst,
        const std::uint32_t at, const Pair source, const Pair base)
    {
        (void)at;
        const auto& part = body_.parts[inst.aux];
        const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        const auto width = inst.width;
        const auto edge = static_cast<std::int64_t>(width - 1U);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(base);
        // selected(bit) = selected_right + bit (descending) or - bit.
        auto* selected_right = builder_.CreateAdd(value,
            constant(static_cast<std::uint64_t>(increasing ? (descending ? 0 : edge)
                                                           : (descending ? -edge : 0))));
        auto* selected_last = builder_.CreateAdd(selected_right,
            constant(static_cast<std::uint64_t>(descending ? edge : -edge)));
        auto* right = constant(static_cast<std::uint64_t>(part.right));
        auto* low = builder_.CreateSelect(
            builder_.CreateICmpSLT(selected_right, selected_last), selected_right,
            selected_last);
        auto* high = builder_.CreateSelect(
            builder_.CreateICmpSLT(selected_right, selected_last), selected_last,
            selected_right);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(low,
                constant(static_cast<std::uint64_t>(std::min(part.left, part.right)))),
            builder_.CreateICmpSLE(high,
                constant(static_cast<std::uint64_t>(std::max(part.left, part.right)))));
        // Offsets increase with the result bit when the selection moves away
        // from `right` as bits increase.
        llvm::Value* increasing_offsets = nullptr;
        if (descending) {
            increasing_offsets = builder_.CreateICmpSGE(low, right);
        } else {
            increasing_offsets = builder_.CreateICmpSLE(high, right);
        }
        auto* first = builder_.CreateSelect(builder_.CreateICmpSGE(selected_right, right),
            builder_.CreateSub(selected_right, right),
            builder_.CreateSub(right, selected_right));
        auto* offset = builder_.CreateAdd(first, constant(part.base_offset));
        auto* fits = builder_.CreateICmpULE(builder_.CreateAdd(offset, constant(width)),
            constant(inst.offset));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range),
            builder_.CreateAnd(increasing_offsets, fits));
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        auto* m = constant(mask_of(width));
        const Pair fast { builder_.CreateAnd(builder_.CreateLShr(source.a, safe), m),
            builder_.CreateAnd(builder_.CreateLShr(source.b, safe), m) };
        auto* source_unknown = unknown_of(inst.x);
        llvm::Value* source_weak = weak_mode() && tracked(inst.x) ? weak_of(inst.x) : nullptr;
        auto* fast_unknown = builder_.CreateAnd(builder_.CreateLShr(source_unknown, safe), m);
        llvm::Value* fast_weak = source_weak != nullptr
            ? builder_.CreateAnd(builder_.CreateLShr(source_weak, safe), m)
            : nullptr;
        if (folded(ok, true) || two_state_) {
            if (!folded(ok, true)) {
                // Two-state code leaves a partial selection to the full code.
                read_or_hand_over(ok);
            }
            last_unknown_ = fast_unknown;
            last_weak_ = fast_weak;
            return fast;
        }
        // A selection that is not one whole field of the source (an unknown
        // base, bits outside the declared range or the source, or the other
        // direction): each result bit is read on its own and the bits
        // outside read X (IEEE 1800-2023 11.5.1), as kw::dynamic_part_select.
        // Everything the slow block reads was loaded before the branch.
        auto* fast_block = builder_.GetInsertBlock();
        auto* slow = llvm::BasicBlock::Create(context_, "part_slow", &function_);
        auto* merge = llvm::BasicBlock::Create(context_, "part_merge", &function_);
        builder_.CreateCondBr(ok, merge, slow);
        builder_.SetInsertPoint(slow);
        Pair result { m, m };
        llvm::Value* result_unknown = constant(0U);
        llvm::Value* result_weak = constant(0U);
        if ((inst.flags & detail::flag_two_state) != 0U) {
            result = { constant(0U), constant(0U) };
        }
        auto* lower_bound = constant(static_cast<std::uint64_t>(std::min(part.left, part.right)));
        auto* upper_bound = constant(static_cast<std::uint64_t>(std::max(part.left, part.right)));
        for (std::uint32_t bit = 0U; bit < width; ++bit) {
            auto* selected = descending
                ? builder_.CreateAdd(selected_right, constant(bit))
                : builder_.CreateSub(selected_right, constant(bit));
            auto* distance = builder_.CreateSub(selected, right);
            auto* from = builder_.CreateAdd(
                builder_.CreateSelect(builder_.CreateICmpSGE(distance, constant(0U)),
                    distance, builder_.CreateNeg(distance)),
                constant(part.base_offset));
            auto* element_ok = builder_.CreateAnd(known,
                builder_.CreateAnd(
                    builder_.CreateAnd(builder_.CreateICmpSGE(selected, lower_bound),
                        builder_.CreateICmpSLE(selected, upper_bound)),
                    builder_.CreateICmpULT(from, constant(inst.offset))));
            auto* from_safe = builder_.CreateSelect(element_ok, from, constant(0U));
            const auto one = std::uint64_t { 1 } << bit;
            const auto put = [&](llvm::Value* plane, llvm::Value* from_plane) {
                auto* element = builder_.CreateAnd(
                    builder_.CreateLShr(from_plane, from_safe), constant(1U));
                return builder_.CreateSelect(element_ok,
                    builder_.CreateOr(builder_.CreateAnd(plane, constant(~one)),
                        builder_.CreateShl(element, constant(bit))),
                    plane);
            };
            result = { put(result.a, source.a), put(result.b, source.b) };
            result_unknown = put(result_unknown, source_unknown);
            if (source_weak != nullptr) {
                result_weak = put(result_weak, source_weak);
            }
        }
        auto* slow_end = builder_.GetInsertBlock();
        builder_.CreateBr(merge);
        builder_.SetInsertPoint(merge);
        const auto join = [&](llvm::Value* on_fast, llvm::Value* on_slow) {
            auto* phi = builder_.CreatePHI(i64_, 2U);
            phi->addIncoming(on_fast, fast_block);
            phi->addIncoming(on_slow, slow_end);
            return static_cast<llvm::Value*>(phi);
        };
        const Pair joined { join(fast.a, result.a), join(fast.b, result.b) };
        last_unknown_ = join(fast_unknown, result_unknown);
        last_weak_ = fast_weak != nullptr ? join(fast_weak, result_weak) : nullptr;
        return joined;
    }

    /// Appends a deferred VHDL assignment to the round's queue.
    // Kernel-owned memories (StaticKernelContainerInfo). Accesses to a
    // memory with storage 0 go through the helpers.
    struct MemoryElement {
        llvm::Value* info { };
        llvm::Value* storage { };
        /// The index is known and inside the declared range.
        llvm::Value* in_range { };
        /// The element ordinal (0 when out of range, so loads stay in
        /// bounds).
        llvm::Value* ordinal { };
    };

    [[nodiscard]] llvm::Value* info_field(llvm::Value* info, llvm::Type* type,
        const std::uint64_t offset)
    {
        return builder_.CreateLoad(type,
            builder_.CreateConstInBoundsGEP1_64(i8_, info, offset));
    }

    [[nodiscard]] MemoryElement memory_element(const std::uint32_t binding_index,
        const Pair index, const std::uint32_t index_width, const bool linear)
    {
        auto* table = builder_.CreateLoad(ptr_, frame_field(64U));
        auto* info = builder_.CreateInBoundsGEP(i8_, table,
            builder_.CreateMul(builder_.CreateZExt(binding(binding_index, 0U), i64_),
                constant(sizeof(runtime::simir::StaticKernelContainerInfo))));
        auto* storage = info_field(info, i32_, 0U);
        auto* count = info_field(info, i64_, 8U);
        auto* value = sign_extend(index.a, index_width);
        auto* known = builder_.CreateNot(nonzero(index.b));
        llvm::Value* in_range { };
        llvm::Value* ordinal { };
        if (linear) {
            in_range = builder_.CreateICmpULT(value, count);
            ordinal = value;
        } else {
            auto* left = info_field(info, i64_, 16U);
            auto* descending = nonzero(builder_.CreateAnd(
                builder_.CreateZExt(info_field(info, i32_, 60U), i64_),
                constant(2U)));
            ordinal = builder_.CreateSelect(descending,
                builder_.CreateSub(left, value), builder_.CreateSub(value, left));
            in_range = builder_.CreateAnd(
                builder_.CreateAnd(
                    builder_.CreateICmpSGE(value, info_field(info, i64_, 24U)),
                    builder_.CreateICmpSLE(value, info_field(info, i64_, 32U))),
                builder_.CreateICmpULT(ordinal, count));
        }
        in_range = builder_.CreateAnd(known, in_range);
        return { info, storage, in_range,
            builder_.CreateSelect(in_range, ordinal, constant(0U)) };
    }

    /// Storage 1: the element slot's arena offset; storage 2: the packed
    /// slot's arena offset; storage 3: the element's arena offset. Element
    /// bit offset within it (storage 2 only).
    struct MemoryLocation {
        llvm::Value* arena_offset { };
        llvm::Value* words { };
        llvm::Value* bit { };
        llvm::Value* slot { };
    };

    [[nodiscard]] MemoryLocation memory_location(const MemoryElement& element)
    {
        auto* packed = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 2U));
        // Storage 1 reads the table only for direct memories (others may
        // have no table).
        auto* elements = builder_.CreateLoad(ptr_,
            builder_.CreateConstInBoundsGEP1_64(i8_, element.info, 64U));
        auto* table_index = builder_.CreateMul(element.ordinal, constant(2U));
        auto* has_table = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 1U));
        auto* safe_index = builder_.CreateSelect(has_table, table_index, constant(0U));
        auto* safe_table = builder_.CreateSelect(has_table, elements,
            builder_.CreateConstInBoundsGEP1_64(i8_, element.info, 72U));
        auto* element_offset = builder_.CreateZExt(builder_.CreateLoad(i32_,
            builder_.CreateInBoundsGEP(i32_, safe_table, safe_index)), i64_);
        auto* element_slot = builder_.CreateLoad(i32_,
            builder_.CreateInBoundsGEP(i32_, safe_table,
                builder_.CreateAdd(safe_index, constant(1U))));
        auto* width = builder_.CreateZExt(info_field(element.info, i32_, 56U), i64_);
        auto* packed_width = builder_.CreateZExt(
            info_field(element.info, i32_, 80U), i64_);
        auto* bit = builder_.CreateSub(packed_width,
            builder_.CreateMul(builder_.CreateAdd(element.ordinal, constant(1U)),
                width));
        auto* packed_offset = builder_.CreateZExt(
            info_field(element.info, i32_, 72U), i64_);
        auto* held = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 3U));
        element_offset = builder_.CreateSelect(held,
            builder_.CreateAdd(packed_offset,
                builder_.CreateMul(element.ordinal, constant(2U))),
            element_offset);
        return {
            builder_.CreateSelect(packed, packed_offset, element_offset),
            builder_.CreateSelect(packed,
                builder_.CreateZExt(info_field(element.info, i32_, 76U), i64_),
                constant(1U)),
            builder_.CreateSelect(packed, bit, constant(0U)),
            builder_.CreateSelect(packed, info_field(element.info, i32_, 4U),
                element_slot),
        };
    }

    [[nodiscard]] Pair emit_mem_read(const KInst& inst, const std::uint32_t at)
    {
        const auto index = load_register(inst.y);
        const auto element = memory_element(inst.x, index, inst.width,
            (inst.flags & detail::flag_linear) != 0U);
        const auto location = memory_location(element);
        auto* m = builder_.CreateSub(builder_.CreateShl(constant(1U),
                                         builder_.CreateZExt(info_field(element.info,
                                                                 i32_, 56U),
                                             i64_)),
            constant(1U));
        auto* width = builder_.CreateZExt(info_field(element.info, i32_, 56U), i64_);
        m = builder_.CreateSelect(builder_.CreateICmpEQ(width, constant(64U)),
            constant(~std::uint64_t { 0 }), m);
        auto* word = builder_.CreateLShr(location.bit, constant(6U));
        auto* shift = builder_.CreateAnd(location.bit, constant(63U));
        auto* last = builder_.CreateSub(location.words, constant(1U));
        auto* next = builder_.CreateSelect(builder_.CreateICmpULT(word, last),
            builder_.CreateAdd(word, constant(1U)), word);
        const auto plane = [&](llvm::Value* first) {
            auto* low = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                arena_, builder_.CreateAdd(first, word)));
            auto* high = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                arena_, builder_.CreateAdd(first, next)));
            auto* spill = builder_.CreateSelect(
                builder_.CreateICmpEQ(shift, constant(0U)), constant(0U),
                builder_.CreateShl(high,
                    builder_.CreateSub(constant(64U), shift)));
            return builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(low, shift), spill), m);
        };
        const Pair loaded { plane(location.arena_offset),
            plane(builder_.CreateAdd(location.arena_offset, location.words)) };
        const auto fast = select(element.in_range, loaded,
            Pair { info_field(element.info, i64_, 40U),
                info_field(element.info, i64_, 48U) });
        // Elements outside the arena always take the slow path: two-state
        // code keeps it rather than leaving every read to the full code.
        hand_over_slow_paths_ = false;
        const auto value = with_fallback(builder_.CreateICmpNE(element.storage,
                                             llvm::ConstantInt::get(i32_, 0U)),
            fast, at, zero_pair(), index);
        hand_over_slow_paths_ = true;
        return value;
    }

    /// Where a memory element of `chunks` words per plane lies (see
    /// memory_location, whose elements are one word): storage 1 the element
    /// slot's arena offset (`chunks` words per plane), storage 2 the packed
    /// slot's with the element's bit offset, storage 3 the element's own
    /// words (a plane after `chunks` words, 2 * chunks words per element).
    [[nodiscard]] MemoryLocation wide_memory_location(const MemoryElement& element,
        const std::uint32_t chunks)
    {
        auto* packed = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 2U));
        auto* held = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 3U));
        auto* has_table = builder_.CreateICmpEQ(element.storage,
            llvm::ConstantInt::get(i32_, 1U));
        auto* elements = builder_.CreateLoad(ptr_,
            builder_.CreateConstInBoundsGEP1_64(i8_, element.info, 64U));
        auto* table_index = builder_.CreateMul(element.ordinal, constant(2U));
        auto* safe_index = builder_.CreateSelect(has_table, table_index, constant(0U));
        auto* safe_table = builder_.CreateSelect(has_table, elements,
            builder_.CreateConstInBoundsGEP1_64(i8_, element.info, 72U));
        auto* element_offset = builder_.CreateZExt(builder_.CreateLoad(i32_,
            builder_.CreateInBoundsGEP(i32_, safe_table, safe_index)), i64_);
        auto* element_slot = builder_.CreateLoad(i32_,
            builder_.CreateInBoundsGEP(i32_, safe_table,
                builder_.CreateAdd(safe_index, constant(1U))));
        auto* width = builder_.CreateZExt(info_field(element.info, i32_, 56U), i64_);
        auto* packed_width = builder_.CreateZExt(
            info_field(element.info, i32_, 80U), i64_);
        auto* bit = builder_.CreateSub(packed_width,
            builder_.CreateMul(builder_.CreateAdd(element.ordinal, constant(1U)),
                width));
        auto* packed_offset = builder_.CreateZExt(
            info_field(element.info, i32_, 72U), i64_);
        element_offset = builder_.CreateSelect(held,
            builder_.CreateAdd(packed_offset,
                builder_.CreateMul(element.ordinal, constant(2U * chunks))),
            element_offset);
        return {
            builder_.CreateSelect(packed, packed_offset, element_offset),
            builder_.CreateSelect(packed,
                builder_.CreateZExt(info_field(element.info, i32_, 76U), i64_),
                constant(chunks)),
            builder_.CreateSelect(packed, bit, constant(0U)),
            builder_.CreateSelect(packed, info_field(element.info, i32_, 4U),
                element_slot),
        };
    }

    /// KOp::mem_read sub 1: an element of more than 64 bits (offset its
    /// width; d the destination's chunks; IEEE 1800-2023 7.4.6, 7.9). Each
    /// chunk of each plane is read from the element's storage (the element
    /// slot, the packed slot at the element's bit offset, or the kernel-held
    /// words) and replaced, where the index is unknown or outside the array,
    /// by X (zero in a two-state array); a memory the kernel does not hold
    /// (storage 0) takes the helper.
    void emit_wide_mem_read(const KInst& inst, const std::uint32_t at)
    {
        const auto element_width = inst.offset;
        const auto chunks = detail::wide_chunks(element_width);
        const auto index = load_register(inst.y);
        const auto element = memory_element(inst.x, index, inst.width,
            (inst.flags & detail::flag_linear) != 0U);
        auto* held = llvm::BasicBlock::Create(context_, "mem_wide_read", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "mem_wide_read_slow", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "mem_wide_read_done", &function_);
        builder_.CreateCondBr(builder_.CreateICmpNE(element.storage,
                                  llvm::ConstantInt::get(i32_, 0U)),
            held, slow);
        builder_.SetInsertPoint(held);
        const auto location = wide_memory_location(element, chunks);
        auto* two_state = nonzero(builder_.CreateAnd(
            builder_.CreateZExt(info_field(element.info, i32_, 60U), i64_), constant(1U)));
        auto* word = builder_.CreateLShr(location.bit, constant(6U));
        auto* shift = builder_.CreateAnd(location.bit, constant(63U));
        auto* last = builder_.CreateSub(location.words, constant(1U));
        // Chunk `chunk` (a value) of `bits` bits.
        const auto read_chunk = [&](llvm::Value* chunk, const std::uint32_t bits) {
            auto* m = constant(mask_of(bits));
            auto* here = builder_.CreateAdd(word, chunk);
            auto* next = builder_.CreateSelect(builder_.CreateICmpULT(here, last),
                builder_.CreateAdd(here, constant(1U)), here);
            const auto plane = [&](const std::uint64_t index_of_plane) {
                auto* first = builder_.CreateAdd(location.arena_offset,
                    builder_.CreateMul(location.words, constant(index_of_plane)));
                auto* low = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                    arena_, builder_.CreateAdd(first, here)));
                auto* high = builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_,
                    arena_, builder_.CreateAdd(first, next)));
                auto* spill = builder_.CreateSelect(
                    builder_.CreateICmpEQ(shift, constant(0U)), constant(0U),
                    builder_.CreateShl(high, builder_.CreateSub(constant(64U), shift)));
                return builder_.CreateAnd(
                    builder_.CreateOr(builder_.CreateLShr(low, shift), spill), m);
            };
            auto* unknown = builder_.CreateSelect(two_state, constant(0U), m);
            builder_.CreateStore(builder_.CreateSelect(element.in_range, plane(0U), unknown),
                dynamic_chunk_address(inst.d, chunk, 0U));
            builder_.CreateStore(builder_.CreateSelect(element.in_range, plane(1U), unknown),
                dynamic_chunk_address(inst.d, chunk, 1U));
            if (inst.aux != 0U) {
                // A container holds four-state elements: no 'U' or weak masks.
                builder_.CreateStore(constant(0U),
                    dynamic_chunk_address(inst.d + static_cast<std::uint32_t>(chunks), chunk,
                        0U));
                builder_.CreateStore(constant(0U),
                    dynamic_chunk_address(inst.d + static_cast<std::uint32_t>(chunks), chunk,
                        1U));
            }
        };
        if (loops_chunks(element_width)) {
            each_chunk(element_width, read_chunk);
        } else {
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                read_chunk(constant(chunk), std::min(64U, element_width - 64U * chunk));
            }
        }
        builder_.CreateBr(done);
        builder_.SetInsertPoint(slow);
        (void)call_wide(at, zero_pair(), index, zero_pair());
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    /// KOp::mem_write sub 1: a write of an element of more than 64 bits
    /// (width the value's; x its chunks; IEEE 1800-2023 7.4.6, 7.9). A
    /// known index inside the array and a value of the element's width
    /// without 'U' or weak elements (and, in a two-state array, without X
    /// or Z) is written chunk by chunk: kernel-held elements in place, an
    /// element or packed slot with the change detection of native slot
    /// stores, or, nonblocking, appended to the deferred queue (one entry
    /// per chunk, the slot marked for the update phase; a full queue takes
    /// the helper, which grows it). Anything else (a memory the kernel does
    /// not hold, an index outside the array, a dynamic part select, a value
    /// the array cannot hold) takes the reference's element write
    /// (call_wide: execute_wide).
    void emit_wide_mem_write(const KInst& inst, const std::uint32_t at)
    {
        const auto chunks = detail::wide_chunks(inst.width);
        const auto index = load_register(inst.y);
        if (inst.aux != 0U) {
            (void)call_wide(at, zero_pair(), index, load_register(inst.z));
            return;
        }
        const auto element = memory_element(inst.d, index, inst.offset,
            (inst.flags & detail::flag_linear) != 0U);
        auto* flags = builder_.CreateZExt(info_field(element.info, i32_, 60U), i64_);
        auto* two_state = nonzero(builder_.CreateAnd(flags, constant(1U)));
        // The value's X and Z bits and masks, over every chunk.
        const bool loops = loops_chunks(inst.width);
        const auto mask_base = inst.x + static_cast<std::uint32_t>(chunks);
        const auto accumulate = [&](llvm::Value* chunk, const std::uint32_t bits,
                                    const Carried& carried) -> Carried {
            auto* four = builder_.CreateOr(carried[0], builder_.CreateAnd(
                builder_.CreateLoad(i64_, dynamic_chunk_address(inst.x, chunk, 1U)),
                constant(mask_of(bits))));
            llvm::Value* held_masks = carried[1];
            if ((inst.imm_b & 1U) != 0U) {
                held_masks = builder_.CreateOr(held_masks,
                    builder_.CreateOr(builder_.CreateLoad(i64_,
                                          dynamic_chunk_address(mask_base, chunk, 0U)),
                        builder_.CreateLoad(i64_,
                            dynamic_chunk_address(mask_base, chunk, 1U))));
            }
            return { four, held_masks };
        };
        Carried accumulated { constant(0U), constant(0U) };
        if (loops) {
            accumulated = fold_chunks(inst.width, accumulated, accumulate);
        } else {
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                accumulated = accumulate(constant(chunk),
                    std::min(64U, inst.width - 64U * chunk), accumulated);
            }
        }
        llvm::Value* four_state = accumulated[0];
        llvm::Value* masks = accumulated[1];
        // Each chunk of the value, unrolled or (loops) in a word loop.
        const auto chunk_loop = [&](const auto& body) {
            if (loops) {
                each_chunk(inst.width, body);
                return;
            }
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                body(constant(chunk), std::min(64U, inst.width - 64U * chunk));
            }
        };
        auto* writable = builder_.CreateAnd(element.in_range,
            builder_.CreateICmpEQ(info_field(element.info, i32_, 56U),
                llvm::ConstantInt::get(i32_, inst.width)));
        writable = builder_.CreateAnd(writable, builder_.CreateNot(nonzero(masks)));
        writable = builder_.CreateAnd(writable,
            builder_.CreateNot(builder_.CreateAnd(two_state, nonzero(four_state))));
        auto* done = llvm::BasicBlock::Create(context_, "mem_wide_written", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "mem_wide_write_slow", &function_);
        const auto chunk_value = [&](llvm::Value* chunk, const std::uint32_t bits) {
            return wide_chunk_value(inst.x, chunk, bits);
        };
        if ((inst.flags & detail::flag_nba) == 0U) {
            auto* held = llvm::BasicBlock::Create(context_, "mem_wide_held", &function_);
            auto* signal = llvm::BasicBlock::Create(context_, "mem_wide_signal", &function_);
            auto* in_place = llvm::BasicBlock::Create(context_, "mem_wide_in_place",
                &function_);
            builder_.CreateCondBr(builder_.CreateAnd(writable,
                                      builder_.CreateICmpEQ(element.storage,
                                          llvm::ConstantInt::get(i32_, 3U))),
                held, signal);
            builder_.SetInsertPoint(signal);
            builder_.CreateCondBr(builder_.CreateAnd(writable,
                                      nonzero(builder_.CreateAnd(flags, constant(4U)))),
                in_place, slow);
            builder_.SetInsertPoint(in_place);
            {
                const auto location = wide_memory_location(element, chunks);
                auto* silent = nonzero(builder_.CreateAnd(flags, constant(8U)));
                chunk_loop([&](llvm::Value* chunk, const std::uint32_t bits) {
                    auto* next = llvm::BasicBlock::Create(context_, "mem_wide_chunk",
                        &function_);
                    emit_store_field(bits, location.arena_offset, location.words,
                        builder_.CreateAdd(location.bit,
                            builder_.CreateShl(chunk, constant(6U))),
                        location.slot, silent, chunk_value(chunk, bits), next);
                    builder_.SetInsertPoint(next);
                });
                builder_.CreateBr(done);
            }
            builder_.SetInsertPoint(held);
            {
                const auto location = wide_memory_location(element, chunks);
                chunk_loop([&](llvm::Value* chunk, const std::uint32_t bits) {
                    const auto value = chunk_value(chunk, bits);
                    builder_.CreateStore(value.a, builder_.CreateInBoundsGEP(i64_, arena_,
                        builder_.CreateAdd(location.arena_offset, chunk)));
                    builder_.CreateStore(value.b, builder_.CreateInBoundsGEP(i64_, arena_,
                        builder_.CreateAdd(location.arena_offset,
                            builder_.CreateAdd(constant(chunks), chunk))));
                });
                builder_.CreateBr(done);
            }
            builder_.SetInsertPoint(slow);
            (void)call_wide(at, zero_pair(), index, zero_pair());
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            return;
        }
        // The nonblocking queue holds slot writes (storage 1 and 2): one
        // entry per chunk, committed in order with the other writes.
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        auto* ok = builder_.CreateAnd(writable, builder_.CreateOr(
            builder_.CreateICmpEQ(element.storage, llvm::ConstantInt::get(i32_, 1U)),
            builder_.CreateICmpEQ(element.storage, llvm::ConstantInt::get(i32_, 2U))));
        ok = builder_.CreateAnd(ok, builder_.CreateICmpULE(
            builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, chunks)), capacity));
        auto* append = llvm::BasicBlock::Create(context_, "mem_wide_append", &function_);
        builder_.CreateCondBr(ok, append, slow);
        builder_.SetInsertPoint(append);
        const auto location = wide_memory_location(element, chunks);
        auto* data = builder_.CreateLoad(ptr_, queue);
        chunk_loop([&](llvm::Value* chunk, const std::uint32_t bits) {
            const auto value = chunk_value(chunk, bits);
            auto* entry = builder_.CreateInBoundsGEP(i8_, data,
                builder_.CreateMul(builder_.CreateAdd(builder_.CreateZExt(count, i64_),
                                       chunk),
                    constant(56U)));
            const auto entry_field = [&](const std::uint64_t offset) {
                return builder_.CreateConstInBoundsGEP1_64(i8_, entry, offset);
            };
            builder_.CreateStore(location.slot, entry_field(0U));
            builder_.CreateStore(builder_.CreateTrunc(
                                     builder_.CreateAdd(location.bit,
                                         builder_.CreateShl(chunk, constant(6U))),
                                     i32_),
                entry_field(4U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, bits), entry_field(8U));
            builder_.CreateStore(builder_.CreateLoad(i32_, frame_field(32U)),
                entry_field(12U));
            builder_.CreateStore(value.a, entry_field(16U));
            builder_.CreateStore(value.b, entry_field(24U));
            builder_.CreateStore(constant(0U), entry_field(32U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, 0U), entry_field(40U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, at), entry_field(44U));
            if (body_.weak != 0U) {
                builder_.CreateStore(constant(0U), entry_field(48U));
            }
        });
        builder_.CreateStore(
            builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, chunks)),
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        emit_write_mark(location.slot, done);
        builder_.SetInsertPoint(slow);
        (void)call_wide(at, zero_pair(), index, zero_pair(), 1U);
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    void emit_mem_write(const KInst& inst, const std::uint32_t at)
    {
        const auto value = load_register(inst.x);
        const auto index = load_register(inst.y);
        if (inst.aux != 0U) {
            emit_mem_write_part(inst, at, value, index);
            return;
        }
        const auto element = memory_element(inst.d, index, inst.offset,
            (inst.flags & detail::flag_linear) != 0U);
        auto* two_state = nonzero(builder_.CreateAnd(
            builder_.CreateZExt(info_field(element.info, i32_, 60U), i64_),
            constant(1U)));
        auto* writable = builder_.CreateAnd(element.in_range,
            builder_.CreateICmpEQ(info_field(element.info, i32_, 56U),
                llvm::ConstantInt::get(i32_, inst.width)));
        writable = builder_.CreateAnd(writable, builder_.CreateNot(
            builder_.CreateAnd(two_state, nonzero(value.b))));
        if ((inst.flags & detail::flag_nba) == 0U) {
            // Blocking: kernel-held elements (storage 3) are stored in place,
            // and so are element and packed slots (flag bit 2), with the
            // change detection of native slot stores. Other memories take
            // the helper.
            auto* held = llvm::BasicBlock::Create(context_, "mem_store", &function_);
            auto* signal = llvm::BasicBlock::Create(context_, "mem_signal", &function_);
            auto* in_place = llvm::BasicBlock::Create(context_, "mem_in_place",
                &function_);
            auto* slow = llvm::BasicBlock::Create(context_, "mem_store_slow", &function_);
            auto* done = llvm::BasicBlock::Create(context_, "mem_stored", &function_);
            builder_.CreateCondBr(builder_.CreateAnd(writable,
                                      builder_.CreateICmpEQ(element.storage,
                                          llvm::ConstantInt::get(i32_, 3U))),
                held, signal);
            builder_.SetInsertPoint(signal);
            builder_.CreateCondBr(builder_.CreateAnd(writable,
                                      nonzero(builder_.CreateAnd(
                                          builder_.CreateZExt(info_field(element.info,
                                                                  i32_, 60U),
                                              i64_),
                                          constant(4U)))),
                in_place, slow);
            builder_.SetInsertPoint(in_place);
            emit_store_element_slot(inst, element, value, done);
            builder_.SetInsertPoint(held);
            auto* offset = builder_.CreateAdd(
                builder_.CreateZExt(info_field(element.info, i32_, 72U), i64_),
                builder_.CreateMul(element.ordinal, constant(2U)));
            builder_.CreateStore(value.a,
                builder_.CreateInBoundsGEP(i64_, arena_, offset));
            builder_.CreateStore(value.b, builder_.CreateInBoundsGEP(i64_,
                arena_, builder_.CreateAdd(offset, constant(1U))));
            builder_.CreateBr(done);
            builder_.SetInsertPoint(slow);
            call_effect(at, value, index, zero_pair());
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            return;
        }
        // The nonblocking queue holds slot writes (storage 1 and 2).
        auto* ok = builder_.CreateAnd(writable, builder_.CreateOr(
            builder_.CreateICmpEQ(element.storage, llvm::ConstantInt::get(i32_, 1U)),
            builder_.CreateICmpEQ(element.storage, llvm::ConstantInt::get(i32_, 2U))));
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        ok = builder_.CreateAnd(ok, builder_.CreateICmpULT(count, capacity));
        auto* fast = llvm::BasicBlock::Create(context_, "mem_append", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "mem_write_slow", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "mem_written", &function_);
        builder_.CreateCondBr(ok, fast, slow);
        builder_.SetInsertPoint(fast);
        // The nonblocking queue's word write to the element's slot, as the
        // reference's element write would make it.
        const auto location = memory_location(element);
        auto* data = builder_.CreateLoad(ptr_, queue);
        auto* entry = builder_.CreateInBoundsGEP(i8_, data,
            builder_.CreateMul(builder_.CreateZExt(count, i64_), constant(56U)));
        const auto entry_field = [&](const std::uint64_t offset) {
            return builder_.CreateConstInBoundsGEP1_64(i8_, entry, offset);
        };
        builder_.CreateStore(location.slot, entry_field(0U));
        builder_.CreateStore(builder_.CreateTrunc(location.bit, i32_),
            entry_field(4U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, inst.width),
            entry_field(8U));
        builder_.CreateStore(builder_.CreateLoad(i32_, frame_field(32U)),
            entry_field(12U));
        builder_.CreateStore(value.a, entry_field(16U));
        builder_.CreateStore(value.b, entry_field(24U));
        builder_.CreateStore(constant(0U), entry_field(32U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, 0U), entry_field(40U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, at), entry_field(44U));
        if (body_.weak != 0U) {
            builder_.CreateStore(constant(0U), entry_field(48U));
        }
        builder_.CreateStore(
            builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, 1U)),
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        emit_write_mark(location.slot, done);
        builder_.SetInsertPoint(slow);
        call_effect(at, value, index, zero_pair(), 1U);
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    /// A dynamic part-select write to a container element of up to 64 bits
    /// (mem_write with aux: imm_a the element's width, aux - 1 the part, z the
    /// base register; IEEE 1800-2023 11.5.1 for the part select of an lvalue,
    /// 7.4.6 for the index): as the reference's element_part write
    /// (StaticKernel::write_element_narrow) does, the in-range interval of
    /// the value's bits (part_write_fields) replaces the same bits of the
    /// element. An unknown base or an empty selection writes nothing. A
    /// blocking write stores in place (kernel-held elements, and element and
    /// packed slots with native slot-store change detection); a nonblocking
    /// one appends the field to the deferred queue (storage 1 and 2). Anything
    /// else (an index outside the array, a field the element does not hold, a
    /// storage the queue does not know) takes the reference path, which
    /// reports the error or ignores the write.
    void emit_mem_write_part(const KInst& inst, const std::uint32_t at,
        const Pair value, const Pair index)
    {
        const auto& part = body_.parts[inst.aux - 1U];
        const auto element_width = static_cast<std::uint32_t>(inst.imm_a);
        if (element_width == 0U || element_width > 64U || part.width == 0U
            || part.width > 64U || part.width != inst.width) {
            call_effect(at, value, index, load_register(inst.z));
            return;
        }
        const auto base = load_register(inst.z);
        const auto element = memory_element(inst.d, index, inst.offset,
            (inst.flags & detail::flag_linear) != 0U);
        const auto fields = part_write_fields(part, base, element_width);
        auto* flags = builder_.CreateZExt(info_field(element.info, i32_, 60U), i64_);
        auto* two_state = nonzero(builder_.CreateAnd(flags, constant(1U)));
        // The element is addressable, of the width the part was lowered for,
        // and a two-state array is not given X or Z.
        auto* writable = builder_.CreateAnd(element.in_range,
            builder_.CreateICmpEQ(info_field(element.info, i32_, 56U),
                llvm::ConstantInt::get(i32_, element_width)));
        writable = builder_.CreateAnd(writable,
            builder_.CreateNot(builder_.CreateAnd(two_state, nonzero(value.b))));
        auto* fits = builder_.CreateAnd(fields.follows, fields.inside);
        auto* stores = builder_.CreateAnd(builder_.CreateAnd(writable, fields.write), fits);
        auto* nothing = builder_.CreateAnd(writable, builder_.CreateNot(fields.write));
        auto* field_offset = builder_.CreateSelect(fields.write, fields.offset, constant(0U));
        const Pair placed {
            builder_.CreateAnd(builder_.CreateLShr(value.a, fields.first_bit), fields.bits),
            builder_.CreateAnd(builder_.CreateLShr(value.b, fields.first_bit), fields.bits) };
        auto* store = llvm::BasicBlock::Create(context_, "mem_part", &function_);
        auto* idle = llvm::BasicBlock::Create(context_, "mem_part_idle", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "mem_part_slow", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "mem_part_done", &function_);
        builder_.CreateCondBr(stores, store, idle);
        builder_.SetInsertPoint(idle);
        builder_.CreateCondBr(nothing, done, slow);
        builder_.SetInsertPoint(slow);
        call_effect(at, value, index, base);
        builder_.CreateBr(done);
        builder_.SetInsertPoint(store);
        if ((inst.flags & detail::flag_nba) == 0U) {
            auto* held = llvm::BasicBlock::Create(context_, "mem_part_held", &function_);
            auto* signal = llvm::BasicBlock::Create(context_, "mem_part_signal", &function_);
            auto* in_place = llvm::BasicBlock::Create(context_, "mem_part_in_place",
                &function_);
            builder_.CreateCondBr(builder_.CreateICmpEQ(element.storage,
                                      llvm::ConstantInt::get(i32_, 3U)),
                held, signal);
            builder_.SetInsertPoint(signal);
            builder_.CreateCondBr(nonzero(builder_.CreateAnd(flags, constant(4U))),
                in_place, slow);
            builder_.SetInsertPoint(in_place);
            const auto location = memory_location(element);
            emit_store_field(fields.bits, fields.count, location.arena_offset,
                location.words, builder_.CreateAdd(location.bit, field_offset),
                location.slot, nonzero(builder_.CreateAnd(flags, constant(8U))), placed,
                done);
            // A kernel-held element: its two words in place, no change
            // report (as the whole-element store).
            builder_.SetInsertPoint(held);
            auto* first = builder_.CreateAdd(
                builder_.CreateZExt(info_field(element.info, i32_, 72U), i64_),
                builder_.CreateMul(element.ordinal, constant(2U)));
            auto* field = builder_.CreateShl(fields.bits, field_offset);
            const auto merge = [&](llvm::Value* word, llvm::Value* bits) {
                auto* address = builder_.CreateInBoundsGEP(i64_, arena_, word);
                builder_.CreateStore(
                    builder_.CreateOr(
                        builder_.CreateAnd(builder_.CreateLoad(i64_, address),
                            bit_not(field)),
                        builder_.CreateAnd(builder_.CreateShl(bits, field_offset), field)),
                    address);
            };
            merge(first, placed.a);
            merge(builder_.CreateAdd(first, constant(1U)), placed.b);
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            return;
        }
        // The nonblocking queue holds slot writes (storage 1 and 2): the
        // field is one of them, committed in order with the whole-element
        // writes.
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        auto* queued = builder_.CreateAnd(
            builder_.CreateOr(
                builder_.CreateICmpEQ(element.storage, llvm::ConstantInt::get(i32_, 1U)),
                builder_.CreateICmpEQ(element.storage, llvm::ConstantInt::get(i32_, 2U))),
            builder_.CreateICmpULT(count, capacity));
        auto* append = llvm::BasicBlock::Create(context_, "mem_part_append", &function_);
        builder_.CreateCondBr(queued, append, slow);
        builder_.SetInsertPoint(append);
        const auto location = memory_location(element);
        auto* data = builder_.CreateLoad(ptr_, queue);
        auto* entry = builder_.CreateInBoundsGEP(i8_, data,
            builder_.CreateMul(builder_.CreateZExt(count, i64_), constant(56U)));
        const auto entry_field = [&](const std::uint64_t offset) {
            return builder_.CreateConstInBoundsGEP1_64(i8_, entry, offset);
        };
        builder_.CreateStore(location.slot, entry_field(0U));
        builder_.CreateStore(
            builder_.CreateTrunc(builder_.CreateAdd(location.bit, field_offset), i32_),
            entry_field(4U));
        builder_.CreateStore(builder_.CreateTrunc(fields.count, i32_), entry_field(8U));
        builder_.CreateStore(builder_.CreateLoad(i32_, frame_field(32U)), entry_field(12U));
        builder_.CreateStore(placed.a, entry_field(16U));
        builder_.CreateStore(placed.b, entry_field(24U));
        builder_.CreateStore(constant(0U), entry_field(32U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, 0U), entry_field(40U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, at), entry_field(44U));
        if (body_.weak != 0U) {
            builder_.CreateStore(constant(0U), entry_field(48U));
        }
        builder_.CreateStore(
            builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, 1U)),
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        emit_write_mark(location.slot, done);
        builder_.SetInsertPoint(done);
    }

    /// A blocking write of a storage 1 or 2 element in place (see
    /// emit_store_field). Branches to `done`.
    void emit_store_element_slot(const KInst& inst, const MemoryElement& element,
        const Pair value, llvm::BasicBlock* done)
    {
        const auto location = memory_location(element);
        emit_store_field(inst.width, location.arena_offset, location.words,
            location.bit, location.slot,
            nonzero(builder_.CreateAnd(
                builder_.CreateZExt(info_field(element.info, i32_, 60U), i64_),
                constant(8U))),
            value, done);
    }

    /// The field [bit, bit + width) of the slot `slot` (arena words from
    /// `arena_offset`, `words` per plane) takes `value`, as
    /// StaticKernel::write_slot_word stores it: a change is announced unless
    /// the slot is `silent`. Branches to `done`.
    void emit_store_field(const std::uint32_t width, llvm::Value* arena_offset,
        llvm::Value* words, llvm::Value* bit, llvm::Value* slot,
        llvm::Value* silent, const Pair value, llvm::BasicBlock* done)
    {
        emit_store_field(constant(mask_of(width)), constant(width), arena_offset, words,
            bit, slot, silent, value, done);
    }

    /// emit_store_field for a field of a run-time `width` (1 to 64; a value)
    /// whose bits are `m`.
    void emit_store_field(llvm::Value* m, llvm::Value* width, llvm::Value* arena_offset,
        llvm::Value* words, llvm::Value* bit, llvm::Value* slot,
        llvm::Value* silent, const Pair value, llvm::BasicBlock* done)
    {
        auto* word = builder_.CreateLShr(bit, constant(6U));
        auto* shift = builder_.CreateAnd(bit, constant(63U));
        auto* spans = builder_.CreateAnd(
            builder_.CreateICmpUGT(builder_.CreateAdd(shift, width),
                constant(64U)),
            builder_.CreateICmpULT(builder_.CreateAdd(word, constant(1U)), words));
        auto* next = builder_.CreateSelect(spans,
            builder_.CreateAdd(word, constant(1U)), word);
        auto* spill = builder_.CreateSub(constant(64U), shift);
        struct Plane {
            llvm::Value* low_address { };
            llvm::Value* high_address { };
            llvm::Value* low { };
            llvm::Value* high { };
            llvm::Value* field { };
        };
        const auto read = [&](llvm::Value* first) {
            Plane plane;
            plane.low_address = builder_.CreateInBoundsGEP(i64_, arena_,
                builder_.CreateAdd(first, word));
            plane.high_address = builder_.CreateInBoundsGEP(i64_, arena_,
                builder_.CreateAdd(first, next));
            plane.low = builder_.CreateLoad(i64_, plane.low_address);
            plane.high = builder_.CreateLoad(i64_, plane.high_address);
            plane.field = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(plane.low, shift),
                    builder_.CreateSelect(spans,
                        builder_.CreateShl(plane.high, spill), constant(0U))),
                m);
            return plane;
        };
        const auto old_a = read(arena_offset);
        const auto old_b = read(builder_.CreateAdd(arena_offset, words));
        auto* next_a = builder_.CreateAnd(value.a, m);
        auto* next_b = builder_.CreateAnd(value.b, m);
        auto* changed = builder_.CreateOr(builder_.CreateXor(old_a.field, next_a),
            builder_.CreateXor(old_b.field, next_b));
        auto* write = llvm::BasicBlock::Create(context_, "field_write", &function_);
        auto* notify = llvm::BasicBlock::Create(context_, "field_notify", &function_);
        builder_.CreateCondBr(nonzero(changed), write, done);
        builder_.SetInsertPoint(write);
        const auto store = [&](const Plane& old, llvm::Value* next_value) {
            // The high word first: without a spill it is the low word, which
            // the low store then rewrites.
            builder_.CreateStore(builder_.CreateSelect(spans,
                                     builder_.CreateOr(builder_.CreateAnd(old.high,
                                                           builder_.CreateNot(
                                                               builder_.CreateLShr(m,
                                                                   spill))),
                                         builder_.CreateLShr(next_value, spill)),
                                     old.high),
                old.high_address);
            builder_.CreateStore(
                builder_.CreateOr(builder_.CreateAnd(old.low,
                                      builder_.CreateNot(builder_.CreateShl(m, shift))),
                    builder_.CreateShl(next_value, shift)),
                old.low_address);
        };
        store(old_a, next_a);
        store(old_b, next_b);
        builder_.CreateCondBr(silent, done, notify);
        builder_.SetInsertPoint(notify);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_notify_field", type);
        builder_.CreateCall(type, callee,
            { frame_, slot, changed, builder_.CreateTrunc(bit, i32_) });
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, done);
    }

    /// A write through a dynamic index (imm_a: the slot's words per plane,
    /// imm_b: its width; sub 2 for a resolved slot, whose width is in offset
    /// and whose imm_b holds its source count and the member's source): a known index in the declared range stores in place, or
    /// appends to the deferred queue (flag_nba); an unknown or out-of-range
    /// one has no effect (IEEE 1800-2023 11.5.1) or, strict (VHDL), is an
    /// error, and so is a field that leaves the slot.
    void emit_store_slot_dynamic(const KInst& inst, const std::uint32_t at)
    {
        const auto value = load_register(inst.x);
        const auto index = load_register(inst.y);
        // sub 2: a resolved slot (offset its width, imm_b its source count and
        // the member's source); else imm_b the width of a Logic4 slot.
        const bool resolved = inst.sub == 2U;
        const std::uint64_t slot_width = resolved ? inst.offset : inst.imm_b;
        if (slot_width == 0U) {
            call_effect(at, value, index, zero_pair());
            return;
        }
        const auto& selection = body_.indices[inst.aux];
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* position = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(position,
                constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(position,
                constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(
            builder_.CreateICmpSGE(position, right),
            builder_.CreateSub(position, right), builder_.CreateSub(right, position));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* selected = builder_.CreateAnd(known, in_range);
        const bool fits_slot = inst.width <= slot_width;
        auto* fits = fits_slot
            ? builder_.CreateICmpULE(offset, constant(slot_width - inst.width))
            : static_cast<llvm::Value*>(llvm::ConstantInt::getFalse(context_));
        if ((inst.flags & detail::flag_strict) != 0U) {
            check_or_fail(selected, at, 3U);
        }
        check_or_fail(builder_.CreateOr(builder_.CreateNot(selected), fits), at, 12U);
        auto* ok = builder_.CreateAnd(selected, fits);
        auto* fast = llvm::BasicBlock::Create(context_, "dynamic_store", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "dynamic_stored", &function_);
        builder_.CreateCondBr(ok, fast, done);
        builder_.SetInsertPoint(fast);
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        if ((inst.flags & detail::flag_nba) != 0U) {
            emit_deferred_store(inst, at, builder_.CreateTrunc(safe, i32_));
            builder_.CreateBr(done);
        } else if (resolved) {
            emit_store_net_dynamic(inst, value, builder_.CreateTrunc(safe, i32_),
                constant(mask_of(inst.width)), constant(inst.width));
            builder_.CreateBr(done);
        } else if (inst.sub == 3U) {
            // A nine-value slot written at once: the codes of the field's
            // bits (with the value's 'U' and weak masks) in the four planes.
            auto* m = constant(mask_of(inst.width));
            llvm::Value* weak = weak_mode() && tracked(inst.x) ? weak_of(inst.x) : nullptr;
            emit_store_field_planes_dynamic(inst.d, inst.imm_a, safe,
                constant(inst.width), m,
                slot_planes(value, unknown_of(inst.x), weak, m, 4U));
            builder_.CreateBr(done);
        } else {
            auto* info = binding(inst.d, 1U);
            emit_store_field(inst.width, builder_.CreateZExt(binding(inst.d, 0U), i64_),
                constant(inst.imm_a), safe,
                builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit)),
                builder_.CreateICmpNE(
                    builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
                    llvm::ConstantInt::get(i32_, 0U)),
                value, done);
        }
        builder_.SetInsertPoint(done);
    }

    /// A dynamic part-select write to a Logic4 slot (kernel_word::
    /// dynamic_part_write, see part_write_fields; imm_a the slot's words per
    /// plane, imm_b its width; sub 2 for a resolved slot as for
    /// store_slot_dynamic): the in-range interval of the value's bits is stored in
    /// place or, nonblocking, appended to the deferred queue; an unknown
    /// base or an empty selection writes nothing.
    void emit_store_slot_part(const KInst& inst, const std::uint32_t at)
    {
        const auto value = load_register(inst.x);
        const auto base_register = load_register(inst.y);
        const auto& part = body_.parts[inst.aux];
        // sub 2: a resolved slot (offset its width, imm_b its source count and
        // the member's source); else imm_b the width of a Logic4 slot.
        const bool resolved = inst.sub == 2U;
        const auto slot_width = static_cast<std::uint32_t>(resolved ? inst.offset : inst.imm_b);
        if (slot_width == 0U || inst.width == 0U || inst.width > 64U
            || part.width != inst.width) {
            call_effect(at, value, base_register,
                tracked(inst.x) ? masks_of(inst.x) : zero_pair());
            return;
        }
        const auto fields = part_write_fields(part, base_register, slot_width);
        check_or_fail(builder_.CreateOr(builder_.CreateNot(fields.write), fields.follows),
            at, 10U);
        check_or_fail(builder_.CreateOr(builder_.CreateNot(fields.write), fields.inside),
            at, 12U);
        auto* offset = builder_.CreateSelect(fields.write, fields.offset, constant(0U));
        const Pair placed {
            builder_.CreateAnd(builder_.CreateLShr(value.a, fields.first_bit), fields.bits),
            builder_.CreateAnd(builder_.CreateLShr(value.b, fields.first_bit), fields.bits) };
        auto* fast = llvm::BasicBlock::Create(context_, "part_store", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "part_stored", &function_);
        builder_.CreateCondBr(fields.write, fast, done);
        builder_.SetInsertPoint(fast);
        if ((inst.flags & detail::flag_nba) != 0U) {
            // The value's 'U' and weak masks go with the bits written
            // (a VHDL assignment to a nine-value slot).
            const Pair placed_masks {
                builder_.CreateAnd(builder_.CreateLShr(unknown_of(inst.x), fields.first_bit),
                    fields.bits),
                builder_.CreateAnd(builder_.CreateLShr(weak_of(inst.x), fields.first_bit),
                    fields.bits) };
            emit_deferred_store(inst, at, builder_.CreateTrunc(offset, i32_), &placed,
                builder_.CreateTrunc(fields.count, i32_), &placed_masks, fields.bits);
            builder_.CreateBr(done);
        } else if (resolved) {
            emit_store_net_dynamic(inst, placed, builder_.CreateTrunc(offset, i32_),
                fields.bits, fields.count);
            builder_.CreateBr(done);
        } else if (inst.sub == 3U) {
            // A shared variable of nine values: the codes of the part's
            // bits (with their 'U' and weak masks) in the slot's four planes.
            auto* placed_unknown = builder_.CreateAnd(
                builder_.CreateLShr(unknown_of(inst.x), fields.first_bit), fields.bits);
            llvm::Value* placed_weak = nullptr;
            if (weak_mode() && tracked(inst.x)) {
                placed_weak = builder_.CreateAnd(
                    builder_.CreateLShr(weak_of(inst.x), fields.first_bit), fields.bits);
            }
            emit_store_field_planes_dynamic(inst.d, inst.imm_a, offset, fields.count,
                fields.bits,
                slot_planes(placed, placed_unknown, placed_weak, fields.bits, 4U));
            builder_.CreateBr(done);
        } else {
            auto* info = binding(inst.d, 1U);
            emit_store_field(fields.bits, fields.count,
                builder_.CreateZExt(binding(inst.d, 0U), i64_), constant(inst.imm_a), offset,
                builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit)),
                builder_.CreateICmpNE(
                    builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
                    llvm::ConstantInt::get(i32_, 0U)),
                placed, done);
        }
        builder_.SetInsertPoint(done);
    }

    /// The planes (two, or four for a Logic9 slot) of the field of
    /// `count_mask`'s bits at the run-time bit `offset` of the slot bound at
    /// `index` (`words` words per plane); the field lies inside the slot.
    [[nodiscard]] std::array<llvm::Value*, 4> read_slot_planes(const std::uint32_t index,
        const std::uint64_t words, const std::uint32_t planes, llvm::Value* offset,
        llvm::Value* count_mask)
    {
        auto* word = builder_.CreateLShr(offset, constant(6U));
        auto* bit = builder_.CreateAnd(offset, constant(63U));
        auto* next = builder_.CreateSelect(
            builder_.CreateICmpULT(word, constant(words - 1U)),
            builder_.CreateAdd(word, constant(1U)), word);
        std::array<llvm::Value*, 4> result { constant(0U), constant(0U), constant(0U),
            constant(0U) };
        for (std::uint32_t plane = 0U; plane < planes; ++plane) {
            auto* first_word = slot_word(index, std::uint64_t { plane } * words);
            auto* lo = builder_.CreateLoad(i64_,
                builder_.CreateInBoundsGEP(i64_, first_word, word));
            auto* hi = builder_.CreateLoad(i64_,
                builder_.CreateInBoundsGEP(i64_, first_word, next));
            auto* spill = builder_.CreateSelect(
                builder_.CreateICmpEQ(bit, constant(0U)), constant(0U),
                builder_.CreateShl(hi, builder_.CreateSub(constant(64U), bit)));
            result[plane] = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(lo, bit), spill), count_mask);
        }
        return result;
    }

    /// Elements read from a slot: the word, and in a Logic9 slot the 'U' and
    /// weak masks (all nine codes, kernel_word::logic9_uword), or the Logic4
    /// word they coerce to for a Logic4 destination (`coerce`;
    /// kernel_word::logic9_coerced).
    struct SlotBits {
        Pair value;
        llvm::Value* unknown { };
        llvm::Value* weak { };
    };

    [[nodiscard]] SlotBits decode_slot_planes(const std::array<llvm::Value*, 4>& raw,
        const std::uint32_t planes, const bool coerce, llvm::Value* count_mask)
    {
        if (planes != 4U) {
            return { { builder_.CreateAnd(raw[0], count_mask),
                         builder_.CreateAnd(raw[1], count_mask) },
                constant(0U), constant(0U) };
        }
        if (coerce) {
            const auto chunk = logic9_chunk(raw[0], raw[1], raw[2], raw[3], count_mask, true);
            return { chunk.value, constant(0U), constant(0U) };
        }
        const auto chunk = nine_chunk(raw[0], raw[1], raw[2], raw[3], count_mask);
        return { chunk.value, chunk.unknown, chunk.weak };
    }

    /// dynamic_extract (sub 1) of one bit of a slot (z planes), as
    /// evaluate_slow_op reads a slot source: an index outside the declared
    /// range or the slot reads X (strict: an error, IEEE 1800-2023 11.5.1);
    /// 'U' and weak elements of a Logic9 slot go to the masks.
    [[nodiscard]] Pair emit_slot_extract(const KInst& inst, const std::uint32_t at)
    {
        const auto& selection = body_.indices[inst.aux];
        const auto index = load_register(inst.y);
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range),
            builder_.CreateICmpULT(offset, constant(inst.width)));
        if ((inst.flags & detail::flag_strict) != 0U) {
            if (two_state_) {
                read_or_hand_over(ok);
            } else {
                auto* fail = llvm::BasicBlock::Create(context_, "index_error", &function_);
                auto* next = llvm::BasicBlock::Create(context_, "index_ok", &function_);
                builder_.CreateCondBr(ok, next, fail);
                builder_.SetInsertPoint(fail);
                auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                    { ptr_, i32_, i32_ }, false);
                auto* callee = helper_symbol("fsim_sk_fail", type);
                builder_.CreateCall(type, callee,
                    { frame_, llvm::ConstantInt::get(i32_, at),
                        builder_.CreateSelect(builder_.CreateAnd(known, in_range),
                            llvm::ConstantInt::get(i32_, 4U),
                            llvm::ConstantInt::get(i32_, 3U)) });
                builder_.CreateBr(failure_block_);
                builder_.SetInsertPoint(next);
            }
        }
        const auto bits = decode_slot_planes(
            read_slot_planes(inst.x, (inst.width + 63U) / 64U, inst.z,
                builder_.CreateSelect(ok, offset, constant(0U)), constant(1U)),
            inst.z, inst.imm_b != 0U, constant(1U));
        if (two_state_ && inst.z == 4U) {
            // Two-state code has no masks: the full code takes 'U' or weak.
            read_or_hand_over(builder_.CreateICmpEQ(
                builder_.CreateOr(bits.unknown, bits.weak), constant(0U)));
        }
        last_unknown_ = builder_.CreateSelect(ok, bits.unknown, constant(0U));
        last_weak_ = weak_mode() ? builder_.CreateSelect(ok, bits.weak, constant(0U))
                                 : nullptr;
        return select(ok, bits.value, x_value(1U));
    }

    /// dynamic_part_select (sub 1) from a slot with z planes, as
    /// evaluate_slow_op reads a slot source: bits whose selected index is
    /// outside the range or the slot read X (0 in two-state code); an
    /// unknown base selects nothing. When the declared right bound is the
    /// low bound of a descending range or the high bound of an ascending
    /// one, the selected bits are one field of the slot; otherwise each bit
    /// is read on its own. 'U' and weak elements of a Logic9 slot go to the
    /// masks.
    [[nodiscard]] Pair emit_slot_part_select(const KInst& inst,
        const std::uint32_t at)
    {
        (void)at;
        const auto base_register = load_register(inst.y);
        const auto& part = body_.parts[inst.aux];
        const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        const bool two_state = (inst.flags & detail::flag_two_state) != 0U;
        const bool coerce = inst.imm_b != 0U;
        const auto lower = std::min(part.left, part.right);
        const auto upper = std::max(part.left, part.right);
        const auto width = inst.width;
        const auto edge = static_cast<std::int64_t>(width) - 1;
        const auto source_width = static_cast<std::int64_t>(inst.offset);
        const auto words = (inst.offset + 63U) / 64U;
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base_register.b, constant(mask_of(32U))),
            constant(0U));
        auto* base = builder_.CreateSExt(
            builder_.CreateTrunc(base_register.a, i32_), i64_);
        const auto signed_constant = [&](const std::int64_t value) {
            return constant(static_cast<std::uint64_t>(value));
        };
        auto* selected_right = increasing
            ? builder_.CreateAdd(base, signed_constant(descending ? 0 : edge))
            : builder_.CreateSub(base, signed_constant(descending ? edge : 0));
        const auto smax = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left,
                right);
        };
        const auto smin = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left,
                right);
        };
        auto* m = constant(mask_of(width));
        auto* fill = two_state ? constant(0U) : m;
        Pair result { fill, fill };
        llvm::Value* result_unknown = constant(0U);
        llvm::Value* result_weak = constant(0U);
        if ((descending && part.right == lower) || (!descending && part.right == upper)) {
            // Result bit i is slot bit first + i, for the bits low .. high.
            auto* first = builder_.CreateAdd(
                signed_constant(static_cast<std::int64_t>(part.base_offset)),
                descending ? builder_.CreateSub(selected_right, signed_constant(part.right))
                           : builder_.CreateSub(signed_constant(part.right), selected_right));
            auto* low = descending
                ? builder_.CreateSub(signed_constant(lower), selected_right)
                : builder_.CreateSub(selected_right, signed_constant(upper));
            auto* high = descending
                ? builder_.CreateSub(signed_constant(upper), selected_right)
                : builder_.CreateSub(selected_right, signed_constant(lower));
            low = smax(smax(low, constant(0U)), builder_.CreateSub(constant(0U), first));
            high = smin(smin(high, signed_constant(edge)),
                builder_.CreateSub(signed_constant(source_width - 1), first));
            auto* any = builder_.CreateAnd(known, builder_.CreateICmpSLE(low, high));
            // Safe values when nothing is selected.
            auto* count = builder_.CreateSelect(any,
                builder_.CreateAdd(builder_.CreateSub(high, low), constant(1U)),
                constant(1U));
            auto* shift_out = builder_.CreateSelect(any, low, constant(0U));
            auto* offset = builder_.CreateSelect(any, builder_.CreateAdd(first, low),
                constant(0U));
            auto* count_mask = builder_.CreateLShr(constant(mask_of(64U)),
                builder_.CreateSub(constant(64U), count));
            const auto bits = decode_slot_planes(
                read_slot_planes(inst.x, words, inst.z, offset, count_mask), inst.z,
                coerce, count_mask);
            auto* placed = builder_.CreateShl(count_mask, shift_out);
            const auto put = [&](llvm::Value* plane, llvm::Value* field) {
                return builder_.CreateSelect(any,
                    builder_.CreateOr(builder_.CreateAnd(plane, builder_.CreateNot(placed)),
                        builder_.CreateShl(field, shift_out)),
                    plane);
            };
            result = { put(fill, bits.value.a), put(fill, bits.value.b) };
            result_unknown = builder_.CreateSelect(any,
                builder_.CreateShl(bits.unknown, shift_out), constant(0U));
            result_weak = builder_.CreateSelect(any,
                builder_.CreateShl(bits.weak, shift_out), constant(0U));
        } else {
            auto* right = signed_constant(part.right);
            for (std::uint32_t bit = 0U; bit < width; ++bit) {
                auto* selected = descending
                    ? builder_.CreateAdd(selected_right, constant(bit))
                    : builder_.CreateSub(selected_right, constant(bit));
                auto* distance = builder_.CreateSub(selected, right);
                auto* from = builder_.CreateAdd(
                    builder_.CreateSelect(builder_.CreateICmpSGE(distance, constant(0U)),
                        distance, builder_.CreateNeg(distance)),
                    constant(part.base_offset));
                auto* ok = builder_.CreateAnd(known,
                    builder_.CreateAnd(
                        builder_.CreateAnd(
                            builder_.CreateICmpSGE(selected, signed_constant(lower)),
                            builder_.CreateICmpSLE(selected, signed_constant(upper))),
                        builder_.CreateICmpULT(from, signed_constant(source_width))));
                const auto element = decode_slot_planes(
                    read_slot_planes(inst.x, words, inst.z,
                        builder_.CreateSelect(ok, from, constant(0U)), constant(1U)),
                    inst.z, coerce, constant(1U));
                const auto one = std::uint64_t { 1 } << bit;
                const auto put = [&](llvm::Value* plane, llvm::Value* bits_in) {
                    return builder_.CreateSelect(ok,
                        builder_.CreateOr(builder_.CreateAnd(plane, constant(~one)),
                            builder_.CreateShl(bits_in, constant(bit))),
                        plane);
                };
                result = { put(result.a, element.value.a), put(result.b, element.value.b) };
                result_unknown = builder_.CreateOr(result_unknown,
                    builder_.CreateSelect(ok,
                        builder_.CreateShl(element.unknown, constant(bit)), constant(0U)));
                result_weak = builder_.CreateOr(result_weak,
                    builder_.CreateSelect(ok,
                        builder_.CreateShl(element.weak, constant(bit)), constant(0U)));
            }
        }
        if (two_state_ && inst.z == 4U) {
            // Two-state code has no masks: the full code takes 'U' or weak.
            read_or_hand_over(builder_.CreateICmpEQ(
                builder_.CreateOr(result_unknown, result_weak), constant(0U)));
        }
        last_unknown_ = result_unknown;
        last_weak_ = weak_mode() ? result_weak : nullptr;
        return result;
    }

    /// Appends a deferred VHDL write at bit `offset` (an i32; the static
    /// offset when null).
    /// Bits [offset, offset + width) of the plane at word `first` of slot
    /// x (inst.imm_a words per plane), for a bit offset known only at run
    /// time (a 32-bit binding): the high word is read (from an in-plane
    /// address) only when the field crosses a word boundary inside the
    /// plane.
    [[nodiscard]] llvm::Value* bound_field_plane(const KInst& inst,
        llvm::Value* bound_offset, const std::uint64_t first)
    {
        auto* offset = builder_.CreateZExt(bound_offset, i64_);
        auto* word = builder_.CreateLShr(offset, constant(6U));
        auto* shift = builder_.CreateAnd(offset, constant(63U));
        auto* crosses = builder_.CreateAnd(
            builder_.CreateICmpNE(shift, constant(0U)),
            builder_.CreateAnd(
                builder_.CreateICmpUGT(
                    builder_.CreateAdd(shift, constant(inst.width)),
                    constant(64U)),
                builder_.CreateICmpULT(
                    builder_.CreateAdd(word, constant(1U)),
                    constant(inst.imm_a))));
        auto* next = builder_.CreateSelect(crosses,
            builder_.CreateAdd(word, constant(1U)), word);
        auto* spill_shift = builder_.CreateAnd(
            builder_.CreateSub(constant(64U), shift), constant(63U));
        auto* start = slot_word(inst.x, first);
        auto* low = builder_.CreateLoad(i64_,
            builder_.CreateInBoundsGEP(i64_, start, word));
        auto* high = builder_.CreateLoad(i64_,
            builder_.CreateInBoundsGEP(i64_, start, next));
        auto* value = builder_.CreateOr(
            builder_.CreateLShr(low, shift),
            builder_.CreateSelect(crosses,
                builder_.CreateShl(high, spill_shift), constant(0U)));
        return builder_.CreateAnd(value, constant(mask_of(inst.width)));
    }

    /// StaticKernelNativeFrame::write_marks and write_round, fixed for a
    /// run: read once in the entry block.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> write_marks()
    {
        if (write_marks_ == nullptr) {
            auto& builder = entry_builder();
            write_marks_ = builder.CreateLoad(ptr_,
                builder.CreateConstInBoundsGEP1_64(i8_, frame_, 104U));
            write_round_ = builder.CreateLoad(i32_,
                builder.CreateConstInBoundsGEP1_64(i8_, frame_, 100U));
        }
        return { write_marks_, write_round_ };
    }

    /// Records in StaticKernelNativeFrame::write_marks that a write to
    /// `slot` is queued this round. Branches to `done`.
    void emit_write_mark(llvm::Value* slot, llvm::BasicBlock* done)
    {
        const auto [marks, round] = write_marks();
        auto* mark = llvm::BasicBlock::Create(context_, "write_mark", &function_);
        builder_.CreateCondBr(builder_.CreateIsNull(marks), done, mark);
        builder_.SetInsertPoint(mark);
        builder_.CreateStore(round,
            builder_.CreateInBoundsGEP(i32_, marks, builder_.CreateZExt(slot, i64_)));
        builder_.CreateBr(done);
    }

    /// A VHDL assignment (store_vhdl sub 0; imm_a words per plane, imm_b
    /// planes) to a slot no write has been queued for this round, whose
    /// field it leaves as it is: StaticKernel::commit_writes would skip it,
    /// so it is not queued. Branches to `skip` or falls through.
    void emit_unchanged_write_skip(const KInst& inst, const Pair value,
        llvm::Value* unknown, llvm::Value* slot, llvm::Value* offset,
        llvm::BasicBlock* skip)
    {
        if (inst.op != KOp::store_vhdl || inst.sub != 0U || inst.aux != 0U
            || (inst.imm_b != 2U && inst.imm_b != 4U) || inst.imm_a == 0U
            || inst.width == 0U || inst.width > 64U
            || (offset == nullptr && inst.offset + inst.width > 64U)) {
            return;
        }
        const auto [marks, round] = write_marks();
        auto* compare = llvm::BasicBlock::Create(context_, "same_check", &function_);
        auto* check = llvm::BasicBlock::Create(context_, "unqueued", &function_);
        auto* queue = llvm::BasicBlock::Create(context_, "queue_write", &function_);
        llvm::Value* shift = constant(inst.offset);
        llvm::Value* usable = builder_.CreateIsNotNull(marks);
        if (offset != nullptr) {
            // A bound offset: the field must lie in the first word.
            shift = builder_.CreateZExt(offset, i64_);
            usable = builder_.CreateAnd(usable, builder_.CreateICmpULE(
                builder_.CreateAdd(shift, constant(inst.width)), constant(64U)));
        }
        builder_.CreateCondBr(usable, compare, queue);
        builder_.SetInsertPoint(compare);
        auto* m = constant(mask_of(inst.width));
        auto* field = builder_.CreateShl(m, shift);
        const auto differs = [&](const std::uint64_t plane, llvm::Value* bits) {
            auto* current = builder_.CreateLoad(i64_,
                slot_word(inst.d, plane * inst.imm_a));
            return builder_.CreateAnd(builder_.CreateXor(current,
                                          builder_.CreateShl(bits, shift)),
                field);
        };
        llvm::Value* changed = nullptr;
        if (inst.imm_b == 2U) {
            changed = builder_.CreateOr(differs(0U, value.a), differs(1U, value.b));
        } else {
            // kernel_word::logic9_planes: 'U' elements have code 0000.
            auto* known = builder_.CreateAnd(m, builder_.CreateNot(unknown));
            auto* p0 = builder_.CreateAnd(value.a, known);
            auto* p1 = builder_.CreateAnd(builder_.CreateNot(value.b), known);
            auto* p2 = builder_.CreateAnd(
                builder_.CreateAnd(builder_.CreateNot(value.a), value.b), known);
            changed = builder_.CreateOr(
                builder_.CreateOr(differs(0U, p0), differs(1U, p1)),
                builder_.CreateOr(differs(2U, p2), differs(3U, constant(0U))));
        }
        builder_.CreateCondBr(builder_.CreateICmpEQ(changed, constant(0U)), check, queue);
        builder_.SetInsertPoint(check);
        auto* mark = builder_.CreateLoad(i32_,
            builder_.CreateInBoundsGEP(i32_, marks, builder_.CreateZExt(slot, i64_)));
        builder_.CreateCondBr(builder_.CreateICmpNE(mark, round), skip, queue);
        builder_.SetInsertPoint(queue);
    }

    /// Large cold templates' deferred store (emit_deferred_store), one
    /// function per module so that each store is a call:
    /// i1 fsim_sk_local_deferred_store(frame, slot, offset, width, a, b,
    /// unknown, at, field, words, planes) skips an unchanged write (planes
    /// 2 or 4 with field, the slot's first word, and words per plane; 0
    /// for none), else appends the write and marks the slot. False: the
    /// queue is full, and the caller takes the effect helper's path.
    [[nodiscard]] llvm::Function* local_deferred_store()
    {
        static constexpr const char* name = "fsim_sk_local_deferred_store";
        if (auto* existing = module_.getFunction(name)) {
            return existing;
        }
        auto* i1 = llvm::Type::getInt1Ty(context_);
        auto* type = llvm::FunctionType::get(i1,
            { ptr_, i32_, i32_, i32_, i64_, i64_, i64_, i32_, ptr_, i64_, i32_ }, false);
        auto* function = llvm::Function::Create(type, llvm::Function::InternalLinkage,
            name, module_);
        function->addFnAttr(llvm::Attribute::NoInline);
        function->addFnAttr(llvm::Attribute::NoUnwind);
        auto* frame = function->getArg(0);
        auto* slot = function->getArg(1);
        auto* offset = function->getArg(2);
        auto* width = function->getArg(3);
        auto* value_a = function->getArg(4);
        auto* value_b = function->getArg(5);
        auto* unknown = function->getArg(6);
        auto* at = function->getArg(7);
        auto* field_base = function->getArg(8);
        auto* words = function->getArg(9);
        auto* planes = function->getArg(10);
        const auto block = [&](const char* label) {
            return llvm::BasicBlock::Create(context_, label, function);
        };
        auto* entry = block("entry");
        auto* compare = block("same_check");
        auto* four = block("logic4");
        auto* nine = block("logic9");
        auto* check = block("unqueued");
        auto* skipped = block("skipped");
        auto* queue = block("queue_write");
        auto* fast = block("append");
        auto* full = block("append_full");
        llvm::IRBuilder<> b(entry);
        const auto word = [&](llvm::Value* base, const std::uint64_t bytes) {
            return b.CreateConstInBoundsGEP1_64(i8_, base, bytes);
        };
        const auto k64 = [&](const std::uint64_t value) {
            return llvm::ConstantInt::get(i64_, value);
        };
        auto* marks = b.CreateLoad(ptr_, word(frame, 104U));
        auto* round = b.CreateLoad(i32_, word(frame, 100U));
        auto* shift = b.CreateZExt(offset, i64_);
        auto* width64 = b.CreateZExt(width, i64_);
        auto* usable = b.CreateAnd(
            b.CreateAnd(b.CreateICmpNE(planes, llvm::ConstantInt::get(i32_, 0U)),
                b.CreateIsNotNull(marks)),
            b.CreateICmpULE(b.CreateAdd(shift, width64), k64(64U)));
        b.CreateCondBr(usable, compare, queue);
        b.SetInsertPoint(compare);
        auto* m = b.CreateSelect(b.CreateICmpUGE(width64, k64(64U)), k64(~0ULL),
            b.CreateSub(b.CreateShl(k64(1U), b.CreateAnd(width64, k64(63U))), k64(1U)));
        auto* field = b.CreateShl(m, shift);
        const auto differs = [&](const std::uint64_t plane, llvm::Value* bits) {
            auto* current = b.CreateLoad(i64_, b.CreateInBoundsGEP(i64_, field_base,
                b.CreateMul(words, k64(plane))));
            return b.CreateAnd(b.CreateXor(current, b.CreateShl(bits, shift)), field);
        };
        b.CreateCondBr(b.CreateICmpEQ(planes, llvm::ConstantInt::get(i32_, 4U)), nine,
            four);
        b.SetInsertPoint(four);
        auto* changed4 = b.CreateOr(differs(0U, value_a), differs(1U, value_b));
        b.CreateCondBr(b.CreateICmpEQ(changed4, k64(0U)), check, queue);
        b.SetInsertPoint(nine);
        // kernel_word::logic9_planes: 'U' elements have code 0000.
        auto* known = b.CreateAnd(m, b.CreateNot(unknown));
        auto* p0 = b.CreateAnd(value_a, known);
        auto* p1 = b.CreateAnd(b.CreateNot(value_b), known);
        auto* p2 = b.CreateAnd(b.CreateAnd(b.CreateNot(value_a), value_b), known);
        auto* changed9 = b.CreateOr(b.CreateOr(differs(0U, p0), differs(1U, p1)),
            b.CreateOr(differs(2U, p2), differs(3U, k64(0U))));
        b.CreateCondBr(b.CreateICmpEQ(changed9, k64(0U)), check, queue);
        b.SetInsertPoint(check);
        auto* mark_at = b.CreateInBoundsGEP(i32_, marks, b.CreateZExt(slot, i64_));
        b.CreateCondBr(b.CreateICmpNE(b.CreateLoad(i32_, mark_at), round), skipped, queue);
        b.SetInsertPoint(skipped);
        b.CreateRet(llvm::ConstantInt::getTrue(context_));
        b.SetInsertPoint(queue);
        auto* writes = b.CreateLoad(ptr_, word(frame, 56U));
        auto* count = b.CreateLoad(i32_, word(writes, 8U));
        auto* capacity = b.CreateLoad(i32_, word(writes, 12U));
        b.CreateCondBr(b.CreateICmpULT(count, capacity), fast, full);
        b.SetInsertPoint(fast);
        auto* entry_at = b.CreateInBoundsGEP(i8_, b.CreateLoad(ptr_, writes),
            b.CreateMul(b.CreateZExt(count, i64_), k64(56U)));
        b.CreateStore(slot, word(entry_at, 0U));
        b.CreateStore(offset, word(entry_at, 4U));
        b.CreateStore(width, word(entry_at, 8U));
        b.CreateStore(b.CreateLoad(i32_, word(frame, 32U)), word(entry_at, 12U));
        b.CreateStore(value_a, word(entry_at, 16U));
        b.CreateStore(value_b, word(entry_at, 24U));
        b.CreateStore(unknown, word(entry_at, 32U));
        b.CreateStore(llvm::ConstantInt::get(i32_, 0U), word(entry_at, 40U));
        b.CreateStore(at, word(entry_at, 44U));
        if (body_.weak != 0U) {
            b.CreateStore(k64(0U), word(entry_at, 48U));
        }
        b.CreateStore(b.CreateAdd(count, llvm::ConstantInt::get(i32_, 1U)),
            word(writes, 8U));
        auto* mark = block("write_mark");
        b.CreateCondBr(b.CreateIsNull(marks), skipped, mark);
        b.SetInsertPoint(mark);
        b.CreateStore(round, b.CreateInBoundsGEP(i32_, marks, b.CreateZExt(slot, i64_)));
        b.CreateRet(llvm::ConstantInt::getTrue(context_));
        b.SetInsertPoint(full);
        b.CreateRet(llvm::ConstantInt::getFalse(context_));
        return function;
    }

    void emit_deferred_store(const KInst& inst, const std::uint32_t at,
        llvm::Value* offset = nullptr, const Pair* placed = nullptr,
        llvm::Value* bits = nullptr, const Pair* placed_masks = nullptr,
        llvm::Value* field_mask = nullptr)
    {
        // A later piece of one wide nonblocking write continues the update of
        // the piece before it (detail::write_continues).
        const auto recorded = at
            | ((inst.op == KOp::store_slot_part || inst.op == KOp::store_slot_dynamic)
                        && (inst.flags & detail::flag_continues) == detail::flag_continues
                    ? detail::write_continues : 0U);
        // The source register, which the full queue's helper takes whole.
        const auto original = load_register(inst.x);
        // A part-select write queues `placed`, `bits` (an i32) bits of it.
        const auto value = placed != nullptr ? *placed : original;
        auto* width_value = bits != nullptr
            ? bits : static_cast<llvm::Value*>(llvm::ConstantInt::get(i32_, inst.width));
        // A part-select write queues the 'U' and weak masks of the bits it
        // writes (`placed_masks`, shifted and masked like `placed`).
        auto* unknown = placed_masks != nullptr
            ? placed_masks->a
            : builder_.CreateAnd(unknown_of(inst.x), constant(mask_of(inst.width)));
        // The value's weak mask, when it can have one.
        llvm::Value* weak = weak_mode() && tracked(inst.x)
            ? (placed_masks != nullptr
                      ? placed_masks->b
                      : builder_.CreateAnd(weak_of(inst.x), constant(mask_of(inst.width))))
            : nullptr;
        auto* slot = builder_.CreateAnd(binding(inst.d, 1U),
            llvm::ConstantInt::get(i32_, ~silent_bit));
        auto* done = llvm::BasicBlock::Create(context_, "appended", &function_);
        // An assignment to a resolved slot is resolved into its next block
        // here and queued as kind 4 (StaticKernelWrite): the outlined store
        // does not know it.
        // A VHDL part-select assignment to a resolved slot (store_slot_part
        // sub 2; imm_b its source count and the member's source, offset its
        // width) likewise.
        std::optional<detail::NetShape> shape;
        if (inst.op == KOp::store_vhdl) {
            shape = detail::net_shape(inst);
        } else if (inst.op == KOp::store_slot_part && inst.sub == 2U
            && field_mask != nullptr) {
            shape = detail::make_net_shape(static_cast<std::uint32_t>(inst.imm_b),
                static_cast<std::uint32_t>(inst.imm_b >> 32U), inst.offset);
        }
        if (outline_stores_ && !shape && weak == nullptr) {
            const bool check = inst.op == KOp::store_vhdl && inst.sub == 0U
                && (inst.imm_b == 2U || inst.imm_b == 4U) && inst.imm_a != 0U
                && inst.width != 0U && inst.width <= 64U
                && (offset != nullptr || inst.offset + inst.width <= 64U);
            auto* helper = local_deferred_store();
            auto* appended = builder_.CreateCall(helper,
                { frame_, slot,
                    offset != nullptr ? offset : llvm::ConstantInt::get(i32_, inst.offset),
                    width_value, value.a, value.b, unknown,
                    llvm::ConstantInt::get(i32_, recorded),
                    check ? slot_word(inst.d, 0U)
                          : llvm::ConstantPointerNull::get(
                                llvm::cast<llvm::PointerType>(ptr_)),
                    constant(check ? inst.imm_a : 0U),
                    llvm::ConstantInt::get(i32_, check ? inst.imm_b : 0U) });
            auto* slow = llvm::BasicBlock::Create(context_, "append_full", &function_);
            builder_.CreateCondBr(appended, done, slow);
            builder_.SetInsertPoint(slow);
            call_effect(at, original,
                inst.sub == 1U || inst.op == KOp::store_slot_part
                    || inst.op == KOp::store_slot_dynamic
                ? load_register(inst.y)
                                                                   : zero_pair(),
                Pair { unknown, constant(0U) }, 1U);
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            return;
        }
        if (weak == nullptr && (!shape || inst.op == KOp::store_vhdl)) {
            emit_unchanged_write_skip(inst, value, unknown, slot, offset, done);
        }
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        auto* fast = llvm::BasicBlock::Create(context_, "append", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "append_full", &function_);
        builder_.CreateCondBr(builder_.CreateICmpULT(count, capacity), fast, slow);
        builder_.SetInsertPoint(fast);
        auto* data = builder_.CreateLoad(ptr_, queue);
        auto* entry = builder_.CreateInBoundsGEP(i8_, data,
            builder_.CreateMul(builder_.CreateZExt(count, i64_), constant(56U)));
        const auto field = [&](const std::uint64_t offset) {
            return builder_.CreateConstInBoundsGEP1_64(i8_, entry, offset);
        };
        builder_.CreateStore(slot, field(0U));
        builder_.CreateStore(offset != nullptr ? offset
                                               : llvm::ConstantInt::get(i32_, inst.offset),
            field(4U));
        builder_.CreateStore(width_value, field(8U));
        builder_.CreateStore(builder_.CreateLoad(i32_, frame_field(32U)),
            field(12U));
        builder_.CreateStore(value.a, field(16U));
        builder_.CreateStore(value.b, field(24U));
        builder_.CreateStore(unknown, field(32U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, shape ? 4U : 0U), field(40U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, recorded), field(44U));
        if (body_.weak != 0U) {
            builder_.CreateStore(weak != nullptr ? weak : constant(0U), field(48U));
        }
        builder_.CreateStore(builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, 1U)),
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        if (shape) {
            emit_net_begin_pending(inst, *shape, (shape->width + 63U) / 64U);
            if (inst.op == KOp::store_slot_part) {
                emit_net_vhdl_field_counted(inst, *shape, offset, field_mask,
                    builder_.CreateZExt(width_value, i64_),
                    net_codes_masked(value, unknown, field_mask, weak));
            } else {
                emit_net_vhdl_field(inst, *shape, offset, inst.offset, inst.width,
                    net_codes(value, unknown, inst.width, weak));
            }
        }
        emit_write_mark(slot, done);
        builder_.SetInsertPoint(slow);
        // y: a dynamic index (store_vhdl sub 1) or a part-select base.
        call_effect(at, original,
            inst.sub == 1U || inst.op == KOp::store_slot_part
                    || inst.op == KOp::store_slot_dynamic
                ? load_register(inst.y)
                                                               : zero_pair(),
            Pair { unknown, weak != nullptr ? weak : constant(0U) }, 1U);
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    /// A wide value stored to a resolved slot (wide_store with imm_a: its
    /// words per plane, from bit 32 its width; imm_b as for store_slot), one
    /// 64-bit chunk at a time as StaticKernel's wide_store effect stores it:
    /// a SystemVerilog store (sub 0) resolves each chunk at once, a VHDL
    /// assignment (sub 2) queues it.
    void emit_wide_net_store(const KInst& inst, const std::uint32_t at)
    {
        if (loops_chunks(inst.width)) {
            emit_wide_net_store_words(inst, at);
            return;
        }
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        const auto chunk_value = [&](const std::uint32_t chunk) {
            auto* m = constant(mask_of(std::min(64U, inst.width - 64U * chunk)));
            return Pair { builder_.CreateAnd(builder_.CreateLoad(i64_,
                                                 chunk_address(inst.x + chunk, 0U)),
                              m),
                builder_.CreateAnd(builder_.CreateLoad(i64_, chunk_address(inst.x + chunk, 1U)),
                    m) };
        };
        if (inst.sub == 0U) {
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                emit_store_net(inst, chunk_value(chunk), inst.imm_a & 0xffffffffU,
                    static_cast<std::uint32_t>(inst.imm_a >> 32U),
                    inst.offset + 64U * chunk, std::min(64U, inst.width - 64U * chunk));
            }
            return;
        }
        std::vector<QueuedChunk> queued;
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            const auto bits = std::min(64U, inst.width - 64U * chunk);
            // The chunk's 'U' mask follows the chunks (wide_unknown_flag).
            queued.push_back({ inst.offset + 64U * chunk, bits, chunk_value(chunk),
                inst.aux != 0U
                    ? builder_.CreateAnd(builder_.CreateLoad(i64_,
                                             chunk_address(inst.x + chunks + chunk, 0U)),
                          constant(mask_of(bits)))
                    : constant(0U),
                wide_chunk_weak(inst, chunks, chunk, bits) });
        }
        const auto shape = *detail::net_shape(inst);
        emit_queued_chunks(inst.d, queued, 4U, nullptr, at, [&] {
            // Each chunk resolves into the slot's next block (kind 4 writes).
            emit_net_begin_pending(inst, shape, (shape.width + 63U) / 64U);
            for (const auto& chunk : queued) {
                emit_net_vhdl_field(inst, shape, nullptr, chunk.offset, chunk.width,
                    net_codes(chunk.value, chunk.unknown, chunk.width, chunk.weak));
            }
        }, [&] { (void)call_wide(at, zero_pair(), zero_pair(), zero_pair(), 1U); });
    }

    /// A nine-value constant assigned to a resolved slot (store_vhdl sub 2,
    /// or sub 3 at `offset`): one kind 3 write per 64-bit chunk, whose codes
    /// the commit takes from the body's constants at imm_a (sub 2) or imm_b
    /// (sub 3; StaticKernelWrite).
    void emit_net_constant_store(const KInst& inst, const std::uint32_t at,
        llvm::Value* offset)
    {
        if (loops_chunks(inst.width)) {
            emit_net_constant_store_words(inst, at, offset);
            return;
        }
        // Sub 3 carries the source count above the constants' index.
        const auto constants = (inst.sub == 2U ? inst.imm_a : inst.imm_b) & 0xffffffffU;
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        std::vector<QueuedChunk> queued;
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            queued.push_back({ (offset != nullptr ? 0U : inst.offset) + 64U * chunk,
                std::min(64U, inst.width - 64U * chunk),
                { constant(constants + 2U * chunk), constant(0U) }, constant(0U) });
        }
        const auto shape = *detail::net_shape(inst);
        emit_queued_chunks(inst.d, queued, 3U, offset, at, [&] {
            // Each chunk's codes, constants of the body, resolve into the
            // slot's next block now.
            emit_net_begin_pending(inst, shape, (shape.width + 63U) / 64U);
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                const auto& low = body_.wide_constants[constants + 2U * chunk];
                const auto& high = body_.wide_constants[constants + 2U * chunk + 1U];
                emit_net_vhdl_field(inst, shape,
                    offset != nullptr
                        ? builder_.CreateAdd(offset, llvm::ConstantInt::get(i32_, 64U * chunk))
                        : nullptr,
                    inst.offset + 64U * chunk, std::min(64U, inst.width - 64U * chunk),
                    { constant(low.a), constant(low.b), constant(high.a), constant(high.b) });
            }
        }, [&] {
            call_effect(at, zero_pair(), inst.sub == 3U ? load_register(inst.y) : zero_pair(),
                zero_pair(), 1U);
        });
    }

    /// Deferred store at a dynamic index (kernel_word::dynamic_index): a
    /// known, in-range index whose element fits the target appends directly;
    /// anything else takes the reference path, which reports the error.
    void emit_indexed_deferred_store(const KInst& inst, const std::uint32_t at)
    {
        const auto& selection = body_.indices[inst.aux];
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        const auto index = load_register(inst.y);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        // offset + width <= slot width (both small; no overflow in 64 bits).
        auto* fits = builder_.CreateICmpULE(
            builder_.CreateAdd(offset, constant(inst.width)),
            constant(inst.imm_a & 0xffffffffU));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        auto* direct = llvm::BasicBlock::Create(context_, "indexed_store", &function_);
        auto* reference = llvm::BasicBlock::Create(context_, "indexed_store_ref", &function_);
        auto* joined = llvm::BasicBlock::Create(context_, "indexed_stored", &function_);
        builder_.CreateCondBr(ok, direct, reference);
        builder_.SetInsertPoint(direct);
        if (inst.sub == 3U) {
            emit_net_constant_store(inst, at, builder_.CreateTrunc(offset, i32_));
        } else {
            emit_deferred_store(inst, at, builder_.CreateTrunc(offset, i32_));
        }
        builder_.CreateBr(joined);
        builder_.SetInsertPoint(reference);
        call_effect(at, load_register(inst.x), index, masks_of(inst.x), 2U);
        builder_.CreateBr(joined);
        builder_.SetInsertPoint(joined);
    }

    // 'U' tracking (see CompiledBody::shadow_base).
    [[nodiscard]] bool tracked(const std::uint32_t reg) const
    {
        return body_.shadow_base != 0U && reg < body_.tracked.size()
            && body_.tracked[reg] != 0U;
    }

    [[nodiscard]] llvm::Value* unknown_of(const std::uint32_t reg)
    {
        return tracked(reg) && !two_state_
            ? load_register(body_.shadow_base + reg).a
            : constant(0U);
    }

    /// Whether the code carries weak masks ('H', 'L', 'W', '-'; see
    /// CompiledBody::weak): only in a design that has such elements, and not
    /// in two-state code, which the template has not (native_units).
    [[nodiscard]] bool weak_mode() const
    {
        // A body of wide registers only has no shadow words (shadow_base 0)
        // but its wide registers carry masks.
        return body_.weak != 0U && !two_state_;
    }

    /// The weak mask of a tracked register (zero without weak mode).
    [[nodiscard]] llvm::Value* weak_of(const std::uint32_t reg)
    {
        return weak_mode() && tracked(reg)
            ? load_register(body_.shadow_base + reg).b
            : constant(0U);
    }

    /// The shadow word of a tracked register: its 'U' and weak masks.
    [[nodiscard]] Pair masks_of(const std::uint32_t reg)
    {
        return { unknown_of(reg), weak_of(reg) };
    }

    void set_unknown(const std::uint32_t reg, llvm::Value* value,
        llvm::Value* weak = nullptr)
    {
        if (two_state_) {
            // The 'U' mask stays zero in the register file; store_register
            // already checked a load's mask.
            if (value != guarded_unknown_) {
                guard_known(constant(0U), value, nullptr,
                    tracked(reg)
                        ? register_address(body_.shadow_base + reg).first
                        : nullptr);
            }
            return;
        }
        if (tracked(reg)) {
            store_register(body_.shadow_base + reg,
                Pair { value, weak != nullptr && weak_mode() ? weak : constant(0U) });
        }
    }

    [[nodiscard]] std::uint8_t mode_of(const std::uint32_t at) const
    {
        return body_.shadow_base != 0U ? body_.u_mode[at] : std::uint8_t { 0 };
    }

    /// Deoptimizes when an operand that compiled code reads by kind holds
    /// 'U'.
    void emit_unknown_checks(const std::uint32_t at)
    {
        // Two-state code holds no 'U'.
        if (two_state_ || (mode_of(at) & detail::u_check) == 0U
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        llvm::Value* any = constant(0U);
        for (auto index = body_.u_operand_begin[at];
             index < body_.u_operand_begin[at + 1U]; ++index) {
            any = builder_.CreateOr(any, unknown_of(body_.u_operands[index]));
        }
        if (folded(nonzero(any), false)) {
            return;
        }
        auto* deopt = llvm::BasicBlock::Create(context_, "deopt", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "known", &function_);
        builder_.CreateCondBr(nonzero(any), deopt, next);
        builder_.SetInsertPoint(deopt);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_fail", type);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at),
                llvm::ConstantInt::get(i32_, 2U) });
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
    }

    /// The result's 'U' mask computed from the operands (before the
    /// instruction may overwrite one of them); null when not applicable.
    [[nodiscard]] llvm::Value* unknown_before(const std::uint32_t at)
    {
        const auto& inst = body_.code[at];
        if ((mode_of(at) & detail::u_aware) == 0U
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return nullptr;
        }
        switch (inst.op) {
        case KOp::constant:
            return constant(detail::constant_unknown(inst.offset, inst.aux));
        case KOp::copy:
            return unknown_of(inst.x);
        case KOp::extract:
            if (inst.sub >= 2U) {
                return nullptr;
            }
            return builder_.CreateAnd(
                builder_.CreateLShr(unknown_of(inst.x), constant(inst.offset)),
                constant(mask_of(inst.width)));
        case KOp::insert: {
            const auto field = mask_of(inst.width) << inst.offset;
            return builder_.CreateOr(
                builder_.CreateAnd(unknown_of(inst.x), constant(~field)),
                builder_.CreateAnd(
                    builder_.CreateShl(unknown_of(inst.y), constant(inst.offset)),
                    constant(field)));
        }
        case KOp::concat: {
            llvm::Value* result = constant(0U);
            std::uint32_t offset = 0U;
            for (std::uint32_t index = inst.x; index-- > 0U;) {
                const auto& operand = body_.concat[inst.aux + index];
                result = builder_.CreateOr(result,
                    builder_.CreateShl(
                        builder_.CreateAnd(unknown_of(operand.reg),
                            constant(mask_of(operand.width))),
                        constant(offset)));
                offset += operand.width;
            }
            return result;
        }
        case KOp::binary: {
            auto* m = constant(mask_of(inst.width));
            auto* left = unknown_of(inst.x);
            auto* right = unknown_of(inst.y);
            auto* either = builder_.CreateOr(left, right);
            const auto l = load_register(inst.x);
            const auto r = load_register(inst.y);
            switch (static_cast<BinaryOperator>(inst.sub)) {
            case BinaryOperator::bit_and: {
                // 'U' unless either side is a strong 0.
                auto* zero_l = builder_.CreateAnd(bit_not(l.a), bit_not(l.b));
                auto* zero_r = builder_.CreateAnd(bit_not(r.a), bit_not(r.b));
                return builder_.CreateAnd(builder_.CreateAnd(either, m),
                    bit_not(builder_.CreateOr(zero_l, zero_r)));
            }
            case BinaryOperator::bit_or: {
                auto* one_l = builder_.CreateAnd(l.a, bit_not(l.b));
                auto* one_r = builder_.CreateAnd(r.a, bit_not(r.b));
                return builder_.CreateAnd(builder_.CreateAnd(either, m),
                    bit_not(builder_.CreateOr(one_l, one_r)));
            }
            case BinaryOperator::vhdl_match_equal: {
                // 'U' (IEEE 1076-2019 9.2.3) unless a known mismatch of
                // another pair decides the relation.
                const auto parts = vhdl_match_parts(inst, l, r);
                return builder_.CreateZExt(
                    builder_.CreateAnd(nonzero(parts.uninitialized),
                        builder_.CreateNot(nonzero(parts.mismatch))),
                    i64_);
            }
            default:
                return builder_.CreateAnd(either, m);
            }
        }
        case KOp::unary_not:
            return unknown_of(inst.x);
        case KOp::conditional: {
            const auto condition = load_register(inst.x);
            auto* a0 = builder_.CreateAnd(condition.a, constant(1U));
            auto* b0 = builder_.CreateAnd(condition.b, constant(1U));
            auto* selected = builder_.CreateSelect(nonzero(a0), unknown_of(inst.y),
                unknown_of(inst.z));
            return builder_.CreateSelect(nonzero(b0), constant(0U), selected);
        }
        default:
            return nullptr;
        }
    }

    /// The weak mask of a dynamic_insert or dynamic_part_insert result: the
    /// instruction on the weak masks of its target and source as words,
    /// inline like the value. Null when neither operand is tracked.
    [[nodiscard]] llvm::Value* weak_insert(const KInst& inst, const std::uint32_t at)
    {
        if (!weak_mode() || (!tracked(inst.x) && !tracked(inst.y))
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return nullptr;
        }
        const auto saved_unknown = insert_unknown_;
        const auto saved_weak = last_weak_;
        const Pair target { weak_of(inst.x), constant(0U) };
        const Pair source { weak_of(inst.y), constant(0U) };
        const auto moved = inst.op == KOp::dynamic_insert
            ? emit_dynamic_insert(inst, at, target, source, load_register(inst.z))
            : emit_dynamic_part_insert(inst, at, target, source, load_register(inst.z));
        insert_unknown_ = saved_unknown;
        last_weak_ = saved_weak;
        return moved.a;
    }

    /// The result's weak mask of a copy-like operation, computed from the
    /// operands before the instruction may overwrite one of them; null when
    /// the result has none (the logic operations give 0, 1, X and 'U' only).
    [[nodiscard]] llvm::Value* weak_before(const std::uint32_t at)
    {
        const auto& inst = body_.code[at];
        if (!weak_mode() || (mode_of(at) & detail::u_aware) == 0U
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return nullptr;
        }
        switch (inst.op) {
        case KOp::constant:
            return constant(detail::constant_weak(inst.x, inst.y));
        case KOp::copy:
            return weak_of(inst.x);
        case KOp::extract:
            if (inst.sub >= 2U) {
                return nullptr;
            }
            return builder_.CreateAnd(
                builder_.CreateLShr(weak_of(inst.x), constant(inst.offset)),
                constant(mask_of(inst.width)));
        case KOp::insert: {
            const auto field = mask_of(inst.width) << inst.offset;
            return builder_.CreateOr(
                builder_.CreateAnd(weak_of(inst.x), constant(~field)),
                builder_.CreateAnd(
                    builder_.CreateShl(weak_of(inst.y), constant(inst.offset)),
                    constant(field)));
        }
        case KOp::concat: {
            llvm::Value* result = constant(0U);
            std::uint32_t offset = 0U;
            for (std::uint32_t index = inst.x; index-- > 0U;) {
                const auto& operand = body_.concat[inst.aux + index];
                result = builder_.CreateOr(result,
                    builder_.CreateShl(
                        builder_.CreateAnd(weak_of(operand.reg),
                            constant(mask_of(operand.width))),
                        constant(offset)));
                offset += operand.width;
            }
            return result;
        }
        case KOp::conditional: {
            // A known condition selects a value with its weak elements; an
            // unknown one merges as Logic4 (none).
            const auto condition = load_register(inst.x);
            auto* a0 = builder_.CreateAnd(condition.a, constant(1U));
            auto* b0 = builder_.CreateAnd(condition.b, constant(1U));
            auto* selected = builder_.CreateSelect(nonzero(a0), weak_of(inst.y),
                weak_of(inst.z));
            return builder_.CreateSelect(nonzero(b0), constant(0U), selected);
        }
        default:
            return nullptr;
        }
    }

    void unknown_after(const std::uint32_t at, llvm::Value* result_unknown,
        llvm::Value* result_weak)
    {
        const auto& inst = body_.code[at];
        const auto mode = mode_of(at);
        if (mode == 0U || builder_.GetInsertBlock() == nullptr
            || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        if ((mode & detail::u_clear) != 0U) {
            set_unknown(inst.d, constant(0U));
            return;
        }
        if ((mode & detail::u_aware) == 0U) {
            return;
        }
        switch (inst.op) {
        case KOp::load_slot9:
        case KOp::load_field9:
        case KOp::load_host:
        case KOp::dynamic_extract:
        case KOp::dynamic_part_select:
        case KOp::shift:
        // A narrow result of a wide operation: the matching equality's 'U'
        // (emit_wide_binary), none for any other.
        case KOp::wide_alu:
            set_unknown(inst.d, last_unknown_ != nullptr ? last_unknown_
                                                         : constant(0U),
                last_weak_);
            break;
        case KOp::dynamic_insert:
        case KOp::dynamic_part_insert:
            set_unknown(inst.d, insert_unknown_ != nullptr ? insert_unknown_
                                                           : constant(0U),
                insert_weak_);
            break;
        case KOp::extract:
            if (inst.sub >= 2U) {
                set_unknown(inst.d,
                    last_unknown_ != nullptr ? last_unknown_ : constant(0U),
                    last_weak_);
            } else {
                set_unknown(inst.d, result_unknown, result_weak);
            }
            break;
        case KOp::store_vhdl:
        case KOp::generic:
        case KOp::net_delay:
            break;
        default:
            if (result_unknown != nullptr) {
                set_unknown(inst.d, result_unknown, result_weak);
            }
            break;
        }
        last_unknown_ = nullptr;
        last_weak_ = nullptr;
    }

    /// kernel_word::dynamic_part_write for a source of part.width bits written
    /// into a target of `target_width` bits (IEEE 1800-2023 11.5.1 for the
    /// part select of an lvalue): the in-range part of the selection is one
    /// interval of the source's bits, bits first_bit .. first_bit + count - 1,
    /// written at `offset` of the target. An unknown base, or a selection
    /// outside the object, writes nothing (`write` false; the other fields
    /// are then safe values). Target offsets that do not follow the source
    /// bits (`follows` false) and a field outside the target (`inside` false)
    /// are errors when `write`.
    struct PartWriteFields {
        llvm::Value* write { };
        llvm::Value* first_bit { };
        llvm::Value* count { };
        llvm::Value* offset { };
        llvm::Value* follows { };
        llvm::Value* inside { };
        /// The `count` low bits.
        llvm::Value* bits { };
    };

    [[nodiscard]] PartWriteFields part_write_fields(const runtime::simir::DynamicPartIndex& part,
        const Pair base, const std::uint32_t target_width)
    {
        const bool descending = part.source_descending;
        const auto edge = static_cast<std::int64_t>(part.width - 1U);
        const auto lower = std::min(part.left, part.right);
        const auto upper = std::max(part.left, part.right);
        const auto signed_constant = [&](const std::int64_t number) {
            return constant(static_cast<std::uint64_t>(number));
        };
        const auto smax = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left, right);
        };
        const auto smin = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left, right);
        };
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* selected_right = builder_.CreateAdd(signed_index(base),
            signed_constant(part.increasing ? (descending ? 0 : edge)
                                            : (descending ? -edge : 0)));
        // Source bit i selects index selected_right + i (descending) or
        // selected_right - i; the ones inside [lower, upper] are bits
        // first .. last.
        auto* from = descending ? builder_.CreateSub(signed_constant(lower), selected_right)
                                : builder_.CreateSub(selected_right, signed_constant(upper));
        auto* to = descending ? builder_.CreateSub(signed_constant(upper), selected_right)
                              : builder_.CreateSub(selected_right, signed_constant(lower));
        auto* first = smax(from, constant(0U));
        auto* last = smin(to, signed_constant(edge));
        PartWriteFields fields;
        fields.write = builder_.CreateAnd(known, builder_.CreateICmpSLE(first, last));
        fields.first_bit = builder_.CreateSelect(fields.write, first, constant(0U));
        fields.count = builder_.CreateSelect(fields.write,
            builder_.CreateAdd(builder_.CreateSub(last, first), constant(1U)),
            constant(1U));
        auto* first_index = descending
            ? builder_.CreateAdd(selected_right, fields.first_bit)
            : builder_.CreateSub(selected_right, fields.first_bit);
        auto* right = signed_constant(part.right);
        auto* distance = builder_.CreateSub(first_index, right);
        fields.offset = builder_.CreateAdd(
            builder_.CreateSelect(builder_.CreateICmpSGE(distance, constant(0U)),
                distance, builder_.CreateNeg(distance)),
            constant(part.base_offset));
        fields.follows = builder_.CreateOr(
            builder_.CreateICmpULT(fields.count, constant(2U)),
            descending ? builder_.CreateICmpSGE(first_index, right)
                       : builder_.CreateICmpSLE(first_index, right));
        fields.inside = builder_.CreateAnd(
            builder_.CreateICmpULE(fields.offset, constant(target_width)),
            builder_.CreateICmpULE(fields.count,
                builder_.CreateSub(constant(target_width), fields.offset)));
        fields.bits = builder_.CreateLShr(constant(mask_of(64U)),
            builder_.CreateSub(constant(64U), fields.count));
        return fields;
    }

    /// Inline dynamic_part_insert (see part_write_fields): the field takes
    /// the selected bits of the source.
    [[nodiscard]] Pair emit_dynamic_part_insert(const KInst& inst,
        const std::uint32_t at, const Pair target, const Pair source,
        const Pair base)
    {
        const auto& part = body_.parts[inst.aux];
        if (part.width == 0U || part.width > 64U || inst.width > 64U) {
            throw std::logic_error("static kernel part insert exceeds a word");
        }
        const auto fields = part_write_fields(part, base, inst.width);
        check_or_fail(builder_.CreateOr(builder_.CreateNot(fields.write), fields.follows),
            at, 10U);
        check_or_fail(builder_.CreateOr(builder_.CreateNot(fields.write), fields.inside),
            at, 11U);
        auto* field_offset = builder_.CreateSelect(fields.write, fields.offset, constant(0U));
        auto* field_mask = builder_.CreateSelect(fields.write,
            builder_.CreateShl(fields.bits, field_offset), constant(0U));
        const auto place = [&](llvm::Value* into, llvm::Value* from_plane) {
            return builder_.CreateOr(builder_.CreateAnd(into, bit_not(field_mask)),
                builder_.CreateAnd(
                    builder_.CreateShl(
                        builder_.CreateAnd(
                            builder_.CreateLShr(from_plane, fields.first_bit), fields.bits),
                        field_offset),
                    field_mask));
        };
        if ((mode_of(at) & detail::u_aware) != 0U) {
            insert_unknown_ = place(unknown_of(inst.x), unknown_of(inst.y));
        }
        last_unknown_ = constant(0U);
        return { place(target.a, source.a), place(target.b, source.b) };
    }

    /// Inline dynamic_insert (IEEE 1800-2023 11.5.1: a write through an
    /// unknown or out-of-range index has no effect; a strict one is an
    /// error): a known index in range puts the source at its offset when the
    /// field lies inside the target.
    [[nodiscard]] Pair emit_dynamic_insert(const KInst& inst,
        const std::uint32_t at, const Pair target, const Pair source,
        const Pair index)
    {
        const auto& selection = body_.indices[inst.aux];
        if (inst.width > 64U || inst.offset == 0U || inst.offset > 64U) {
            throw std::logic_error("static kernel insert exceeds a word");
        }
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* fits = builder_.CreateICmpULE(
            builder_.CreateAdd(offset, constant(inst.offset)), constant(inst.width));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        if ((inst.flags & detail::flag_strict) != 0U) {
            check_or_fail(known, at, 13U);
            check_or_fail(ok, at, 9U);
        }
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        auto* m = constant(mask_of(inst.offset));
        auto* keep = bit_not(builder_.CreateShl(m, safe));
        const Pair fast {
            builder_.CreateOr(builder_.CreateAnd(target.a, keep),
                builder_.CreateShl(builder_.CreateAnd(source.a, m), safe)),
            builder_.CreateOr(builder_.CreateAnd(target.b, keep),
                builder_.CreateShl(builder_.CreateAnd(source.b, m), safe)) };
        if ((mode_of(at) & detail::u_aware) != 0U) {
            auto* target_unknown = unknown_of(inst.x);
            insert_unknown_ = builder_.CreateSelect(ok,
                builder_.CreateOr(builder_.CreateAnd(target_unknown, keep),
                    builder_.CreateShl(
                        builder_.CreateAnd(unknown_of(inst.y), m), safe)),
                target_unknown);
        }
        last_unknown_ = constant(0U);
        // Outside the target or the range the target stays as it is.
        return select(ok, fast, target);
    }

    /// Deoptimizes (the reference evaluator continues at `at`) unless `ok`.
    void fail_unless(llvm::Value* ok, const std::uint32_t at)
    {
        if (folded(ok, true)) {
            return;
        }
        auto* deopt = llvm::BasicBlock::Create(context_, "deopt", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "known", &function_);
        builder_.CreateCondBr(ok, next, deopt);
        builder_.SetInsertPoint(deopt);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_fail", type);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at),
                llvm::ConstantInt::get(i32_, 2U) });
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
    }

    /// Reports the error `reason` (fsim_sk_fail; native_fail_message) unless
    /// `ok`; reason is an i32 value. Two-state code hands the instruction to
    /// the full code, which reports it.
    void check_or_fail(llvm::Value* ok, const std::uint32_t at, llvm::Value* reason)
    {
        if (folded(ok, true)) {
            return;
        }
        if (two_state_) {
            read_or_hand_over(ok);
            return;
        }
        auto* fail = llvm::BasicBlock::Create(context_, "error", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "error_ok", &function_);
        builder_.CreateCondBr(ok, next, fail);
        builder_.SetInsertPoint(fail);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_fail", type);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at), reason });
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
    }

    void check_or_fail(llvm::Value* ok, const std::uint32_t at, const std::uint32_t reason)
    {
        check_or_fail(ok, at, llvm::ConstantInt::get(i32_, reason));
    }

    /// A chunk's two planes and its 'U' mask.
    struct WideChunk {
        Pair value;
        llvm::Value* unknown { };
    };

    /// Chunk `chunk` of the wide value at storage index `reg` (`count`
    /// chunks; `masks`: with 'U' masks), as the reference reads a wide
    /// operand.
    [[nodiscard]] WideChunk load_wide_chunk(const std::uint32_t reg,
        const std::uint32_t count, const bool masks, const std::uint32_t chunk)
    {
        return { { builder_.CreateLoad(i64_, chunk_address(reg + chunk, 0U)),
                     builder_.CreateLoad(i64_, chunk_address(reg + chunk, 1U)) },
            masks ? builder_.CreateLoad(i64_, chunk_address(reg + count + chunk, 0U))
                  : constant(0U) };
    }

    /// KOp::wide_concat, as StaticKernel::execute_wide builds it: the
    /// result's planes (and, for sub 1, its 'U' and weak masks) are
    /// assembled in values from every operand (narrow ones from their
    /// registers and shadows, wide ones from their chunks), then stored; an
    /// operand may be the destination.
    void emit_wide_concat(const KInst& inst)
    {
        if (loops_chunks(inst.width)) {
            emit_wide_concat_words(inst);
            return;
        }
        const auto chunks = detail::wide_chunks(inst.width);
        // Planes 0 and 1 of each chunk, then the 'U' and weak masks.
        const std::uint32_t used = inst.sub != 1U ? 2U : weak_mode() ? 4U : 3U;
        std::vector<llvm::Value*> planes(4U * chunks, nullptr);
        const auto place = [&](llvm::Value* a, llvm::Value* b, llvm::Value* u,
                               llvm::Value* w, const std::uint32_t width,
                               const std::uint32_t offset) {
            const auto word = offset / 64U;
            const auto shift = offset % 64U;
            if (width == 0U || word >= chunks) {
                return;
            }
            auto* m = constant(mask_of(width));
            llvm::Value* parts[4] { builder_.CreateAnd(a, m),
                builder_.CreateAnd(b, m), builder_.CreateAnd(u, m),
                builder_.CreateAnd(w, m) };
            const auto add = [&](const std::uint32_t index, llvm::Value* bits) {
                planes[index] = planes[index] == nullptr
                    ? bits : builder_.CreateOr(planes[index], bits);
            };
            for (std::uint32_t plane = 0U; plane < used; ++plane) {
                add(plane * chunks + word, builder_.CreateShl(parts[plane], constant(shift)));
                if (shift != 0U && shift + width > 64U && word + 1U < chunks) {
                    add(plane * chunks + word + 1U,
                        builder_.CreateLShr(parts[plane], constant(64U - shift)));
                }
            }
        };
        std::uint32_t offset = 0U;
        for (std::uint32_t index = inst.x; index-- > 0U;) {
            const auto& operand = body_.concat[inst.aux + index];
            // A Logic4 result reads a weak operand element through
            // coerce_weak ('W' and '-' as X: the word of '-' is Z, so its
            // a bit is set); 'H' and 'L' keep their 1 and 0.
            const bool coerce = inst.sub != 1U && weak_mode();
            const auto coerced = [&](llvm::Value* a, llvm::Value* b,
                                     llvm::Value* weak) -> llvm::Value* {
                return coerce ? builder_.CreateOr(a, builder_.CreateAnd(weak, b)) : a;
            };
            if (operand.width <= 64U) {
                const auto value = load_register(operand.reg);
                auto* weak = weak_of(operand.reg);
                place(coerced(value.a, value.b, weak), value.b,
                    unknown_of(operand.reg), weak, operand.width, offset);
            } else {
                const auto count = detail::wide_chunks(operand.width);
                for (std::uint32_t chunk = 0U; chunk < count; ++chunk) {
                    const auto part = load_wide_chunk(operand.reg, count,
                        operand.unknown != 0U, chunk);
                    auto* weak = (used == 4U || coerce) && operand.unknown != 0U
                        ? builder_.CreateLoad(i64_,
                              chunk_address(operand.reg + count + chunk, 1U))
                        : constant(0U);
                    place(coerced(part.value.a, part.value.b, weak), part.value.b,
                        part.unknown, weak,
                        std::min(64U, operand.width - 64U * chunk), offset + 64U * chunk);
                }
            }
            offset += operand.width;
        }
        const auto plane = [&](const std::uint32_t index) {
            return planes[index] != nullptr ? planes[index] : constant(0U);
        };
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            builder_.CreateStore(plane(chunk), chunk_address(inst.d + chunk, 0U));
            builder_.CreateStore(plane(chunks + chunk), chunk_address(inst.d + chunk, 1U));
            if (inst.sub == 1U) {
                builder_.CreateStore(plane(2U * chunks + chunk),
                    chunk_address(inst.d + chunks + chunk, 0U));
                builder_.CreateStore(plane(3U * chunks + chunk),
                    chunk_address(inst.d + chunks + chunk, 1U));
            }
        }
    }

    /// The value, 'U' mask and validity (an element that is not 0, 1, X, Z
    /// or 'U' makes `ok` false) of the Logic9 code planes p0..p3 of `width`
    /// elements, as kernel_word::logic9_uword; or, when `coerced`, the
    /// Logic4 word they coerce to (kernel_word::logic9_coerced; always ok).
    struct Logic9Chunk {
        Pair value;
        llvm::Value* unknown { };
        llvm::Value* ok { };
        /// The weak mask (nine_chunk only; null otherwise).
        llvm::Value* weak { };
    };

    [[nodiscard]] Logic9Chunk logic9_chunk(llvm::Value* raw0, llvm::Value* raw1,
        llvm::Value* raw2, llvm::Value* raw3, const std::uint32_t width,
        const bool coerced)
    {
        return logic9_chunk(raw0, raw1, raw2, raw3, constant(mask_of(width)), coerced);
    }

    /// logic9_chunk over the elements selected by the mask `m` (a value).
    [[nodiscard]] Logic9Chunk logic9_chunk(llvm::Value* raw0, llvm::Value* raw1,
        llvm::Value* raw2, llvm::Value* raw3, llvm::Value* m, const bool coerced)
    {
        auto* p0 = builder_.CreateAnd(raw0, m);
        auto* p1 = builder_.CreateAnd(raw1, m);
        auto* p2 = builder_.CreateAnd(raw2, m);
        auto* p3 = builder_.CreateAnd(raw3, m);
        const auto inverted = [&](llvm::Value* value) {
            return builder_.CreateAnd(builder_.CreateNot(value), m);
        };
        if (coerced) {
            auto* not_p3 = inverted(p3);
            auto* zero = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), inverted(p0));
            auto* one = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), p0);
            auto* high_z = builder_.CreateAnd(builder_.CreateAnd(not_p3, p2),
                builder_.CreateAnd(inverted(p1), inverted(p0)));
            auto* unknown = inverted(
                builder_.CreateOr(builder_.CreateOr(zero, one), high_z));
            return { { builder_.CreateOr(one, unknown),
                         builder_.CreateOr(high_z, unknown) },
                constant(0U), llvm::ConstantInt::getTrue(context_) };
        }
        auto* u = inverted(builder_.CreateOr(builder_.CreateOr(p0, p1),
            builder_.CreateOr(p2, p3)));
        auto* q0 = builder_.CreateOr(p0, u);
        auto* x = builder_.CreateAnd(builder_.CreateAnd(inverted(p2), inverted(p1)), q0);
        auto* high_z = builder_.CreateAnd(builder_.CreateAnd(p2, inverted(p1)),
            inverted(q0));
        return { { q0, builder_.CreateOr(x, high_z) }, u,
            llvm::ConstantInt::getTrue(context_) };
    }

    /// The register d from the code planes p0..p3 of `m`-masked elements,
    /// all nine codes (kernel_word::logic9_uword): the word, the 'U' mask
    /// (last_unknown_) and the weak mask (last_weak_).
    void store_nine(const std::uint32_t d, llvm::Value* raw0, llvm::Value* raw1,
        llvm::Value* raw2, llvm::Value* raw3, llvm::Value* m)
    {
        const auto nine = nine_chunk(raw0, raw1, raw2, raw3, m);
        last_unknown_ = nine.unknown;
        last_weak_ = nine.weak;
        store_register(d, nine.value);
    }

    /// The word, 'U' mask and weak mask of the code planes raw0..raw3 of
    /// `m`-masked elements, all nine codes (kernel_word::logic9_uword).
    [[nodiscard]] Logic9Chunk nine_chunk(llvm::Value* raw0, llvm::Value* raw1,
        llvm::Value* raw2, llvm::Value* raw3, llvm::Value* m)
    {
        auto* p0 = builder_.CreateAnd(raw0, m);
        auto* p1 = builder_.CreateAnd(raw1, m);
        auto* p2 = builder_.CreateAnd(raw2, m);
        auto* p3 = builder_.CreateAnd(raw3, m);
        auto* u = builder_.CreateAnd(
            builder_.CreateNot(builder_.CreateOr(builder_.CreateOr(p0, p1),
                builder_.CreateOr(p2, p3))),
            m);
        auto* a = builder_.CreateOr(p0, u);
        auto* b = builder_.CreateAnd(
            builder_.CreateOr(
                builder_.CreateAnd(builder_.CreateNot(p1),
                    builder_.CreateOr(builder_.CreateOr(p0, p2), u)),
                p3),
            m);
        auto* w = builder_.CreateAnd(
            builder_.CreateOr(
                builder_.CreateAnd(p2, builder_.CreateOr(p1, p0)), p3),
            m);
        return { { a, b }, u, llvm::ConstantInt::getTrue(context_), w };
    }

    /// The words of a ring entry's default (a signal's value at the start of
    /// simulation, an empty history read) as a constant table, plane after
    /// plane, `chunks` words each (sampled_default_constants).
    [[nodiscard]] llvm::Value* sampled_default_table(const KInst& inst,
        const std::uint64_t planes, const std::uint64_t chunks)
    {
        std::vector<std::uint64_t> words;
        for (std::uint64_t flat = 0U; flat < planes * chunks; ++flat) {
            const auto& pair = body_.wide_constants[inst.imm_b + flat / 2U];
            words.push_back((flat & 1U) != 0U ? pair.b : pair.a);
        }
        return constant_table(words);
    }

    /// A history ring's header words and entry selection, as the bytecode
    /// tier's SampledEntryReader reads them (Impl::SampledRing).
    struct SampledRingView {
        llvm::Value* ring { };
        llvm::Value* capacity { };
        llvm::Value* first { };
        llvm::Value* count { };
        llvm::Value* value_words { };
        llvm::Value* table { };
        llvm::Value* defaults { };
        std::uint64_t chunks { };
        std::uint64_t planes { };
    };

    [[nodiscard]] SampledRingView sampled_ring_view(const KInst& inst)
    {
        SampledRingView view;
        view.chunks = inst.imm_a & 0xffff'ffffU;
        view.planes = (inst.imm_a >> 32U) != 0U ? 4U : 2U;
        view.table = builder_.CreateLoad(ptr_, frame_field(120U));
        auto* rings = builder_.CreateLoad(ptr_,
            builder_.CreateConstInBoundsGEP1_64(i8_, view.table, 24U));
        view.ring = builder_.CreateConstInBoundsGEP1_64(i64_, rings, inst.x);
        const auto header = [&](const std::uint64_t at_word) {
            return builder_.CreateLoad(i64_,
                builder_.CreateConstInBoundsGEP1_64(i64_, view.ring, at_word));
        };
        view.capacity = header(0U);
        view.first = header(1U);
        view.count = header(2U);
        view.value_words = header(3U);
        view.defaults = sampled_default_table(inst, view.planes, view.chunks);
        return view;
    }

    /// The history slot's word `plane`, chunk `chunk` (an i64 value) of the
    /// entry `index` when `have`, else the default's.
    [[nodiscard]] llvm::Value* sampled_entry_word(const SampledRingView& view,
        llvm::Value* have, llvm::Value* index, const std::uint64_t plane,
        llvm::Value* chunk)
    {
        auto* slot = builder_.CreateURem(builder_.CreateAdd(view.first, index),
            view.capacity);
        auto* at_word = builder_.CreateAdd(
            builder_.CreateAdd(constant(12U),
                builder_.CreateMul(slot, view.value_words)),
            builder_.CreateAdd(constant(plane * view.chunks), chunk));
        auto* stored = builder_.CreateLoad(i64_,
            builder_.CreateInBoundsGEP(i64_, view.ring, at_word));
        auto* fallback = table_word(view.defaults,
            builder_.CreateAdd(constant(plane * view.chunks), chunk));
        return builder_.CreateSelect(have, stored, fallback);
    }

    /// The Logic4 words of a chunk of `bits` elements of an entry
    /// (kernel_word::logic9_coerced for a Logic9 ring).
    [[nodiscard]] Pair sampled_entry_value(const SampledRingView& view,
        llvm::Value* have, llvm::Value* index, llvm::Value* chunk,
        const std::uint32_t bits)
    {
        if (view.planes == 4U) {
            return logic9_chunk(sampled_entry_word(view, have, index, 0U, chunk),
                sampled_entry_word(view, have, index, 1U, chunk),
                sampled_entry_word(view, have, index, 2U, chunk),
                sampled_entry_word(view, have, index, 3U, chunk), bits, true)
                .value;
        }
        auto* m = constant(mask_of(bits));
        return { builder_.CreateAnd(sampled_entry_word(view, have, index, 0U, chunk), m),
            builder_.CreateAnd(sampled_entry_word(view, have, index, 1U, chunk), m) };
    }

    /// Whether the clock's sample of this time step is in the ring (so it is
    /// not a past one, IEEE 1800-2023 16.9.3), as a 0 or 1 i64.
    [[nodiscard]] llvm::Value* sampled_now_count(const SampledRingView& view)
    {
        const auto header = [&](const std::uint64_t at_word) {
            return builder_.CreateLoad(i64_,
                builder_.CreateConstInBoundsGEP1_64(i64_, view.ring, at_word));
        };
        auto* now = builder_.CreateLoad(i64_,
            builder_.CreateLoad(ptr_,
                builder_.CreateConstInBoundsGEP1_64(i8_, view.table, 40U)));
        auto* sampled_now = builder_.CreateAnd(
            builder_.CreateAnd(nonzero(header(6U)),
                builder_.CreateICmpEQ(header(7U), now)),
            builder_.CreateICmpEQ(header(8U),
                constant(static_cast<std::uint64_t>(
                    ProcessSchedulingDomain::systemverilog))));
        return builder_.CreateZExt(sampled_now, i64_);
    }

    /// KOp::wide_load sub 4 and 5: the chunks of a wide $sampled (the
    /// Preponed words, sub 4) or of a history ring entry ($sampled with a
    /// clock, $past; sub 5), as execute_wide reads them; a register with
    /// masks gets them cleared (aux).
    void emit_wide_sampled_load(const KInst& inst)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        const auto store_chunk = [&](llvm::Value* chunk, const Pair value) {
            builder_.CreateStore(value.a, dynamic_chunk_address(inst.d, chunk, 0U));
            builder_.CreateStore(value.b, dynamic_chunk_address(inst.d, chunk, 1U));
            if (inst.aux != 0U) {
                builder_.CreateStore(constant(0U),
                    dynamic_chunk_address(inst.d + chunks, chunk, 0U));
                builder_.CreateStore(constant(0U),
                    dynamic_chunk_address(inst.d + chunks, chunk, 1U));
            }
        };
        if (inst.sub == 4U) {
            auto* table = builder_.CreateLoad(ptr_, frame_field(120U));
            auto* sampled = builder_.CreateLoad(ptr_,
                builder_.CreateConstInBoundsGEP1_64(i8_, table, 32U));
            each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
                auto* m = constant(mask_of(bits));
                const auto plane = [&](const std::uint64_t first) {
                    return builder_.CreateAnd(
                        builder_.CreateLoad(i64_,
                            builder_.CreateInBoundsGEP(i64_, sampled,
                                builder_.CreateAdd(constant(first), chunk))),
                        m);
                };
                store_chunk(chunk, { plane(inst.x), plane(inst.x + chunks) });
            });
            return;
        }
        const auto view = sampled_ring_view(inst);
        llvm::Value* have = builder_.CreateICmpUGE(view.count, constant(1U));
        llvm::Value* index = builder_.CreateSub(view.count, constant(1U));
        if (inst.offset == 1U) {
            auto* prior = builder_.CreateSub(view.count, sampled_now_count(view));
            have = builder_.CreateICmpUGE(prior, constant(inst.z));
            index = builder_.CreateSub(prior, constant(inst.z));
        }
        each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
            store_chunk(chunk, sampled_entry_value(view, have, index, chunk, bits));
        });
    }

    /// KOp::sva_sampled_load with aux bit 1: a clocked history read of a wide
    /// or Logic9 signal into a narrow register (the narrow forms of
    /// run_sampled_read: $sampled and $past of a Logic9 signal of at most 64
    /// bits, and $rose, $fell, $stable and $changed of any width).
    void emit_sampled_general(const KInst& inst)
    {
        const auto view = sampled_ring_view(inst);
        const auto signal_width = inst.offset;
        const auto first_bits = std::min(64U, signal_width);
        auto* zero_chunk = constant(0U);
        auto* m = constant(mask_of(inst.width));
        const auto kind = static_cast<SignalReadKind>(inst.sub);
        auto* have_current = builder_.CreateICmpUGE(view.count, constant(1U));
        auto* current_index = builder_.CreateSub(view.count, constant(1U));
        if (kind == SignalReadKind::sampled) {
            auto value = sampled_entry_value(view, have_current, current_index,
                zero_chunk, first_bits);
            store_register(inst.d, { builder_.CreateAnd(value.a, m),
                                       builder_.CreateAnd(value.b, m) });
            return;
        }
        if (kind == SignalReadKind::past) {
            auto* prior = builder_.CreateSub(view.count, sampled_now_count(view));
            auto value = sampled_entry_value(view,
                builder_.CreateICmpUGE(prior, constant(inst.z)),
                builder_.CreateSub(prior, constant(inst.z)), zero_chunk, first_bits);
            store_register(inst.d, { builder_.CreateAnd(value.a, m),
                                       builder_.CreateAnd(value.b, m) });
            return;
        }
        auto* have_previous = builder_.CreateICmpUGE(view.count, constant(2U));
        auto* previous_index = builder_.CreateSub(view.count, constant(2U));
        llvm::Value* result = nullptr;
        if (kind == SignalReadKind::rose || kind == SignalReadKind::fell) {
            const auto current = sampled_entry_value(view, have_current,
                current_index, zero_chunk, first_bits);
            const auto previous = sampled_entry_value(view, have_previous,
                previous_index, zero_chunk, first_bits);
            const auto bit = [&](llvm::Value* plane) {
                return builder_.CreateTrunc(plane, llvm::Type::getInt1Ty(context_));
            };
            const auto level = [&](const Pair pair) {
                auto* value_a = kind == SignalReadKind::rose
                    ? bit(pair.a) : builder_.CreateNot(bit(pair.a));
                return builder_.CreateAnd(value_a, builder_.CreateNot(bit(pair.b)));
            };
            result = builder_.CreateAnd(level(current),
                builder_.CreateNot(level(previous)));
        } else {
            // Every plane of every chunk (PackedLogic4 equality).
            const auto differing = fold_chunks(signal_width, Carried { constant(0U) },
                [&](llvm::Value* chunk, const std::uint32_t bits, const Carried& carried) {
                    llvm::Value* accumulated = carried[0];
                    auto* chunk_mask = constant(mask_of(bits));
                    for (std::uint64_t plane = 0U; plane < view.planes; ++plane) {
                        auto* difference = builder_.CreateXor(
                            sampled_entry_word(view, have_current, current_index,
                                plane, chunk),
                            sampled_entry_word(view, have_previous, previous_index,
                                plane, chunk));
                        accumulated = builder_.CreateOr(accumulated,
                            builder_.CreateAnd(difference, chunk_mask));
                    }
                    return Carried { accumulated };
                });
            auto* equal = builder_.CreateICmpEQ(differing[0], constant(0U));
            result = kind == SignalReadKind::stable ? equal : builder_.CreateNot(equal);
        }
        store_register(inst.d, { builder_.CreateZExt(result, i64_), constant(0U) });
    }

    /// KOp::wide_load: slot x into the chunks of d, as execute_wide. A
    /// Logic9 slot (sub 1) is read exactly: 0, 1, X, Z and 'U', and in a
    /// design with weak sources all nine codes with the weak mask in the
    /// chunks' masks (kernel_word::logic9_uword).
    void emit_wide_load(const KInst& inst)
    {
        if (inst.sub >= 4U) {
            emit_wide_sampled_load(inst);
            return;
        }
        if (loops_chunks(inst.width)) {
            emit_wide_load_words(inst);
            return;
        }
        const auto chunks = detail::wide_chunks(inst.width);
        std::vector<Logic9Chunk> loaded;
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            const auto width = std::min(64U, inst.width - 64U * chunk);
            const auto plane = [&](const std::uint64_t index) {
                return builder_.CreateLoad(i64_,
                    slot_word(inst.x, index * inst.imm_a + chunk));
            };
            if (inst.sub == 0U) {
                // A Logic4 slot: its planes' words are the chunks.
                auto* m = constant(mask_of(width));
                loaded.push_back({ { builder_.CreateAnd(plane(0U), m),
                                       builder_.CreateAnd(plane(1U), m) },
                    constant(0U), nullptr });
                continue;
            }
            if (inst.sub == 1U && weak_mode()) {
                loaded.push_back(nine_chunk(plane(0U), plane(1U), plane(2U), plane(3U),
                    constant(mask_of(width))));
                continue;
            }
            loaded.push_back(logic9_chunk(plane(0U), plane(1U), plane(2U), plane(3U),
                width, inst.sub != 1U));
        }
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            builder_.CreateStore(loaded[chunk].value.a, chunk_address(inst.d + chunk, 0U));
            builder_.CreateStore(loaded[chunk].value.b, chunk_address(inst.d + chunk, 1U));
            if (inst.sub == 1U || inst.aux != 0U) {
                // 'U' masks: the slot's (exact) or cleared.
                builder_.CreateStore(loaded[chunk].unknown,
                    chunk_address(inst.d + chunks + chunk, 0U));
                builder_.CreateStore(loaded[chunk].weak != nullptr ? loaded[chunk].weak
                                                                   : constant(0U),
                    chunk_address(inst.d + chunks + chunk, 1U));
            }
        }
    }

    /// Word `chunk` (a value) of the wide register at storage index `reg`,
    /// plane `plane`.
    [[nodiscard]] llvm::Value* dynamic_chunk_address(const std::uint32_t reg,
        llvm::Value* chunk, const std::uint64_t plane)
    {
        return builder_.CreateInBoundsGEP(i64_, registers_,
            builder_.CreateAdd(builder_.CreateShl(chunk, constant(1U)),
                constant(static_cast<std::uint64_t>(reg) * 2U + plane)));
    }

    /// KOp::wide_part_insert, as execute_wide: the part selection of the
    /// base register z (kernel_word::dynamic_part_write) places the narrow
    /// source y into a copy of the target x. The selection's in-range
    /// indices form one interval; its first element and length give the
    /// written field. A write outside the target, or one whose target bits
    /// do not follow the source bits, deoptimizes before anything is
    /// written; an unknown base or a selection outside the object writes
    /// nothing.
    void emit_wide_part_insert(const KInst& inst, const std::uint32_t at)
    {
        const auto& part = body_.parts[inst.aux];
        const auto chunks = detail::wide_chunks(inst.width);
        const bool masks = inst.imm_a != 0U;
        const auto source = load_register(inst.y);
        const auto base = load_register(inst.z);
        auto* source_unknown = unknown_of(inst.y);
        const bool descending = part.source_descending;
        const auto edge = static_cast<std::int64_t>(part.width - 1U);
        const auto lower = std::min(part.left, part.right);
        const auto upper = std::max(part.left, part.right);
        const auto signed_constant = [&](const std::int64_t value) {
            return constant(static_cast<std::uint64_t>(value));
        };
        const auto smax = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left, right);
        };
        const auto smin = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left, right);
        };
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* selected_right = builder_.CreateAdd(signed_index(base),
            signed_constant(part.increasing ? (descending ? 0 : edge)
                                            : (descending ? -edge : 0)));
        // Source bit i selects index selected_right + i (descending) or
        // selected_right - i; the ones inside [lower, upper] are bits
        // first .. first + count - 1.
        auto* from = descending ? builder_.CreateSub(signed_constant(lower), selected_right)
                                : builder_.CreateSub(selected_right, signed_constant(upper));
        auto* to = descending ? builder_.CreateSub(signed_constant(upper), selected_right)
                              : builder_.CreateSub(selected_right, signed_constant(lower));
        auto* first = smax(from, constant(0U));
        auto* last = smin(to, signed_constant(edge));
        auto* write = builder_.CreateAnd(known, builder_.CreateICmpSLE(first, last));
        // Without a write the fields below are 0 (and gated off).
        auto* first_bit = builder_.CreateSelect(write, first, constant(0U));
        auto* count = builder_.CreateSelect(write,
            builder_.CreateAdd(builder_.CreateSub(last, first), constant(1U)),
            constant(1U));
        auto* first_index = descending ? builder_.CreateAdd(selected_right, first_bit)
                                       : builder_.CreateSub(selected_right, first_bit);
        auto* right = signed_constant(part.right);
        auto* distance = builder_.CreateSub(first_index, right);
        auto* offset = builder_.CreateAdd(
            builder_.CreateSelect(builder_.CreateICmpSGE(distance, constant(0U)),
                distance, builder_.CreateNeg(distance)),
            constant(part.base_offset));
        auto* follows = builder_.CreateOr(
            builder_.CreateICmpULT(count, constant(2U)),
            descending ? builder_.CreateICmpSGE(first_index, right)
                       : builder_.CreateICmpSLE(first_index, right));
        auto* inside = builder_.CreateAnd(
            builder_.CreateICmpULE(offset, constant(inst.width)),
            builder_.CreateICmpULE(count,
                builder_.CreateSub(constant(inst.width), offset)));
        fail_unless(builder_.CreateNot(
                        builder_.CreateAnd(write,
                            builder_.CreateNot(builder_.CreateAnd(follows, inside)))),
            at);
        auto* field_offset = builder_.CreateSelect(write, offset, constant(0U));
        if (inst.d != inst.x) {
            const auto copied = masks ? 2U * chunks : chunks;
            if (loops_chunks(inst.width)) {
                (void)word_loop(constant(copied), Carried {},
                    [&](llvm::Value* index, const Carried&) {
                        for (std::uint64_t word = 0U; word < 2U; ++word) {
                            builder_.CreateStore(
                                builder_.CreateLoad(i64_,
                                    dynamic_chunk_address(inst.x, index, word)),
                                dynamic_chunk_address(inst.d, index, word));
                        }
                        return Carried {};
                    });
            } else {
                for (std::uint32_t index = 0U; index < copied; ++index) {
                    for (std::uint64_t word = 0U; word < 2U; ++word) {
                        builder_.CreateStore(
                            builder_.CreateLoad(i64_, chunk_address(inst.x + index, word)),
                            chunk_address(inst.d + index, word));
                    }
                }
            }
        }
        auto* word = builder_.CreateLShr(field_offset, constant(6U));
        auto* shift = builder_.CreateAnd(field_offset, constant(63U));
        // The bits of the field in its chunk and, when it crosses into the
        // next one, in that chunk.
        auto* m = builder_.CreateLShr(constant(mask_of(64U)),
            builder_.CreateSub(constant(64U), count));
        auto* spills = builder_.CreateAnd(builder_.CreateICmpNE(shift, constant(0U)),
            builder_.CreateICmpUGT(builder_.CreateAdd(shift, count), constant(64U)));
        auto* spill = builder_.CreateAnd(builder_.CreateSub(constant(64U), shift),
            constant(63U));
        auto* low_field = builder_.CreateSelect(write, builder_.CreateShl(m, shift),
            constant(0U));
        auto* high_field = builder_.CreateSelect(builder_.CreateAnd(write, spills),
            builder_.CreateLShr(m, spill), constant(0U));
        auto* high_word = builder_.CreateSelect(spills,
            builder_.CreateAdd(word, constant(1U)), word);
        const auto place = [&](const std::uint32_t reg, const std::uint64_t plane,
                               llvm::Value* bits) {
            auto* field = builder_.CreateAnd(builder_.CreateLShr(bits, first_bit), m);
            const auto merge = [&](llvm::Value* at_word, llvm::Value* mask,
                                   llvm::Value* placed) {
                auto* address = dynamic_chunk_address(reg, at_word, plane);
                builder_.CreateStore(
                    builder_.CreateOr(
                        builder_.CreateAnd(builder_.CreateLoad(i64_, address),
                            bit_not(mask)),
                        builder_.CreateAnd(placed, mask)),
                    address);
            };
            merge(word, low_field, builder_.CreateShl(field, shift));
            merge(high_word, high_field, builder_.CreateLShr(field, spill));
        };
        place(inst.d, 0U, source.a);
        place(inst.d, 1U, source.b);
        if (masks) {
            place(inst.d + chunks, 0U, source_unknown);
            if (weak_mode()) {
                // The source's weak mask moves with its bits (the
                // reference places both masks through the same selection).
                place(inst.d + chunks, 1U, weak_of(inst.y));
            }
        }
    }

    /// `count` bits (1 to 64; a value) from bit `offset` (a value; the field
    /// lies inside the register) of the wide register at storage index
    /// `reg` (`chunks` chunks; `masks`: with 'U' masks), and their 'U'
    /// mask: wide_field in the reference evaluator for a run-time offset.
    [[nodiscard]] std::pair<Pair, llvm::Value*> dynamic_wide_field(
        const std::uint32_t reg, const std::uint32_t chunks, const bool masks,
        llvm::Value* offset, llvm::Value* count, llvm::Value** weak = nullptr)
    {
        auto* word = builder_.CreateLShr(offset, constant(6U));
        auto* shift = builder_.CreateAnd(offset, constant(63U));
        auto* spills = builder_.CreateAnd(
            builder_.CreateICmpNE(shift, constant(0U)),
            builder_.CreateAnd(
                builder_.CreateICmpUGT(builder_.CreateAdd(shift, count), constant(64U)),
                builder_.CreateICmpULT(builder_.CreateAdd(word, constant(1U)),
                    constant(chunks))));
        // The high chunk is read (from an in-register address) only when
        // the field crosses into it.
        auto* next = builder_.CreateSelect(spills,
            builder_.CreateAdd(word, constant(1U)), word);
        auto* spill_shift = builder_.CreateAnd(
            builder_.CreateSub(constant(64U), shift), constant(63U));
        auto* m = builder_.CreateLShr(constant(mask_of(64U)),
            builder_.CreateSub(constant(64U), count));
        const auto field = [&](const std::uint32_t first, const std::uint64_t plane) {
            auto* low = builder_.CreateLoad(i64_,
                dynamic_chunk_address(first, word, plane));
            auto* high = builder_.CreateLoad(i64_,
                dynamic_chunk_address(first, next, plane));
            return builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(low, shift),
                    builder_.CreateSelect(spills,
                        builder_.CreateShl(high, spill_shift), constant(0U))),
                m);
        };
        if (weak != nullptr) {
            // The weak mask of the field: the mask chunks' second words.
            *weak = masks ? field(reg + chunks, 1U) : constant(0U);
        }
        return { { field(reg, 0U), field(reg, 1U) },
            masks ? field(reg + chunks, 0U) : constant(0U) };
    }

    /// Two-state code hands the instruction to the full code, the full
    /// code deoptimizes (the reference evaluator continues at `at`), unless
    /// `ok`.
    void require(llvm::Value* ok, const std::uint32_t at)
    {
        if (two_state_) {
            read_or_hand_over(ok);
        } else {
            fail_unless(ok, at);
        }
    }

    /// dynamic_extract (sub 3) of one bit of a wide register, as
    /// evaluate_slow_op: an index outside the declared range or the source
    /// reads X (strict: the helper reports it); a 'U' element read into an
    /// untracked register deoptimizes.
    [[nodiscard]] Pair emit_wide_dynamic_extract(const KInst& inst,
        const std::uint32_t at)
    {
        const auto& selection = body_.indices[inst.aux];
        const auto index = load_register(inst.y);
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* fits = builder_.CreateICmpULT(offset, constant(inst.width));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        auto* safe = builder_.CreateSelect(ok, offset, constant(0U));
        const bool masks = inst.imm_a != 0U;
        llvm::Value* weak = nullptr;
        const auto [bit, unknown] = dynamic_wide_field(inst.x,
            static_cast<std::uint32_t>(inst.imm_b), masks, safe, constant(1U),
            weak_mode() && masks ? &weak : nullptr);
        if (masks && !tracked(inst.d)) {
            require(builder_.CreateOr(builder_.CreateNot(ok),
                        builder_.CreateICmpEQ(unknown, constant(0U))),
                at);
        }
        auto* selected_unknown = builder_.CreateSelect(ok, unknown, constant(0U));
        // An element outside the source reads X: neither 'U' nor weak.
        last_weak_ = weak != nullptr ? builder_.CreateSelect(ok, weak, constant(0U))
                                     : nullptr;
        if ((inst.flags & detail::flag_strict) == 0U) {
            last_unknown_ = selected_unknown;
            return select(ok, bit, x_value(1U));
        }
        if (two_state_) {
            // The full code reports the index.
            read_or_hand_over(ok);
            last_unknown_ = selected_unknown;
            return bit;
        }
        // Strict (IEEE 1800-2023 11.5.1 for a packed select, 1076-2019 9.3.3.2
        // for an indexed name): an unknown index, one outside the declared
        // range, or one past the source is an error. The kernel reports it
        // (fsim_sk_fail reason 3: the index; 4: the source) and the run
        // stops; the value continues on the valid path.
        auto* fail = llvm::BasicBlock::Create(context_, "index_error", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "index_ok", &function_);
        builder_.CreateCondBr(ok, next, fail);
        builder_.SetInsertPoint(fail);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_fail", type);
        builder_.CreateCall(type, callee,
            { frame_, llvm::ConstantInt::get(i32_, at),
                builder_.CreateSelect(builder_.CreateAnd(known, in_range),
                    llvm::ConstantInt::get(i32_, 4U), llvm::ConstantInt::get(i32_, 3U)) });
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
        last_unknown_ = selected_unknown;
        return bit;
    }

    /// dynamic_part_select (sub 3) from a wide register, as
    /// evaluate_slow_op reads a wide source: bits whose selected index is
    /// outside the range or the source read X (0 in two-state code); an
    /// unknown base selects nothing; a 'U' element read into an untracked
    /// register deoptimizes. When the declared right bound is the low bound
    /// of a descending range or the high bound of an ascending one, the
    /// selected bits are one field of the source; otherwise each bit is
    /// read on its own.
    [[nodiscard]] Pair emit_wide_part_select(const KInst& inst, const std::uint32_t at)
    {
        const auto base = load_register(inst.y);
        const auto& part = body_.parts[inst.aux];
        const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        const bool two_state = (inst.flags & detail::flag_two_state) != 0U;
        const bool masks = inst.imm_a != 0U;
        const auto chunks = static_cast<std::uint32_t>(inst.imm_b);
        const auto lower = std::min(part.left, part.right);
        const auto upper = std::max(part.left, part.right);
        const auto width = inst.width;
        const auto edge = static_cast<std::int64_t>(width) - 1;
        const auto source_width = static_cast<std::int64_t>(inst.offset);
        const auto signed_constant = [&](const std::int64_t value) {
            return constant(static_cast<std::uint64_t>(value));
        };
        const auto smax = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left, right);
        };
        const auto smin = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left, right);
        };
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* selected_right = builder_.CreateAdd(signed_index(base),
            signed_constant(increasing ? (descending ? 0 : edge)
                                       : (descending ? -edge : 0)));
        auto* m = constant(mask_of(width));
        auto* fill = two_state ? constant(0U) : m;
        Pair result { fill, fill };
        llvm::Value* result_unknown = constant(0U);
        // The weak mask of the selected bits (weak mode, masks only).
        const bool with_weak = weak_mode() && masks;
        llvm::Value* result_weak = constant(0U);
        // A 'U' element read: the destination must track it.
        llvm::Value* unknown_read = llvm::ConstantInt::getFalse(context_);
        if ((descending && part.right == lower) || (!descending && part.right == upper)) {
            // Result bit i is source bit first + i.
            auto* first = builder_.CreateAdd(
                signed_constant(static_cast<std::int64_t>(part.base_offset)),
                descending ? builder_.CreateSub(selected_right, signed_constant(part.right))
                           : builder_.CreateSub(signed_constant(part.right), selected_right));
            auto* low = descending
                ? builder_.CreateSub(signed_constant(lower), selected_right)
                : builder_.CreateSub(selected_right, signed_constant(upper));
            auto* high = descending
                ? builder_.CreateSub(signed_constant(upper), selected_right)
                : builder_.CreateSub(selected_right, signed_constant(lower));
            low = smax(smax(low, constant(0U)), builder_.CreateSub(constant(0U), first));
            high = smin(smin(high, signed_constant(edge)),
                builder_.CreateSub(signed_constant(source_width - 1), first));
            auto* any = builder_.CreateAnd(known, builder_.CreateICmpSLE(low, high));
            // Safe values when nothing is selected.
            auto* count = builder_.CreateSelect(any,
                builder_.CreateAdd(builder_.CreateSub(high, low), constant(1U)),
                constant(1U));
            auto* shift_out = builder_.CreateSelect(any, low, constant(0U));
            auto* offset = builder_.CreateSelect(any, builder_.CreateAdd(first, low),
                constant(0U));
            llvm::Value* field_weak = nullptr;
            const auto [field, field_unknown] = dynamic_wide_field(inst.x, chunks,
                masks, offset, count, with_weak ? &field_weak : nullptr);
            auto* count_mask = builder_.CreateLShr(constant(mask_of(64U)),
                builder_.CreateSub(constant(64U), count));
            auto* keep = builder_.CreateNot(builder_.CreateShl(count_mask, shift_out));
            const auto placed = [&](llvm::Value* plane, llvm::Value* bits) {
                return builder_.CreateSelect(any,
                    builder_.CreateOr(builder_.CreateAnd(plane, keep),
                        builder_.CreateShl(bits, shift_out)),
                    plane);
            };
            result = { placed(fill, field.a), placed(fill, field.b) };
            result_unknown = builder_.CreateSelect(any,
                builder_.CreateShl(field_unknown, shift_out), constant(0U));
            if (with_weak) {
                result_weak = builder_.CreateSelect(any,
                    builder_.CreateShl(field_weak, shift_out), constant(0U));
            }
            unknown_read = builder_.CreateAnd(any,
                builder_.CreateICmpNE(field_unknown, constant(0U)));
        } else {
            for (std::uint32_t bit = 0U; bit < width; ++bit) {
                auto* selected = descending
                    ? builder_.CreateAdd(selected_right, constant(bit))
                    : builder_.CreateSub(selected_right, constant(bit));
                auto* right = signed_constant(part.right);
                auto* distance = builder_.CreateSub(selected, right);
                auto* from = builder_.CreateAdd(
                    builder_.CreateSelect(builder_.CreateICmpSGE(distance, constant(0U)),
                        distance, builder_.CreateNeg(distance)),
                    constant(part.base_offset));
                auto* ok = builder_.CreateAnd(known,
                    builder_.CreateAnd(
                        builder_.CreateAnd(
                            builder_.CreateICmpSGE(selected, signed_constant(lower)),
                            builder_.CreateICmpSLE(selected, signed_constant(upper))),
                        builder_.CreateICmpULT(from, signed_constant(source_width))));
                llvm::Value* source_weak = nullptr;
                const auto [source, source_unknown] = dynamic_wide_field(inst.x, chunks,
                    masks, builder_.CreateSelect(ok, from, constant(0U)), constant(1U),
                    with_weak ? &source_weak : nullptr);
                const auto one = std::uint64_t { 1 } << bit;
                const auto put = [&](llvm::Value* plane, llvm::Value* bits) {
                    return builder_.CreateSelect(ok,
                        builder_.CreateOr(builder_.CreateAnd(plane, constant(~one)),
                            builder_.CreateShl(bits, constant(bit))),
                        plane);
                };
                result = { put(result.a, source.a), put(result.b, source.b) };
                result_unknown = builder_.CreateOr(result_unknown,
                    builder_.CreateSelect(ok,
                        builder_.CreateShl(source_unknown, constant(bit)), constant(0U)));
                if (with_weak) {
                    result_weak = builder_.CreateOr(result_weak,
                        builder_.CreateSelect(ok,
                            builder_.CreateShl(source_weak, constant(bit)), constant(0U)));
                }
                unknown_read = builder_.CreateOr(unknown_read,
                    builder_.CreateAnd(ok, builder_.CreateICmpNE(source_unknown, constant(0U))));
            }
        }
        if (masks && !tracked(inst.d)) {
            require(builder_.CreateNot(unknown_read), at);
        }
        last_unknown_ = result_unknown;
        last_weak_ = with_weak ? result_weak : nullptr;
        return result;
    }

    /// One 64-bit chunk of a wide value written to a slot: bits [offset,
    /// offset + width) with their 'U' mask.
    struct QueuedChunk {
        std::uint32_t offset { };
        std::uint32_t width { };
        Pair value;
        llvm::Value* unknown { };
        /// The chunk's weak mask (null: none).
        llvm::Value* weak { };
    };

    /// Appends `chunks` as writes of `kind` (StaticKernelWrite) to the slot
    /// bound at `binding_index` to the round's queue (StaticKernel::push_write:
    /// one entry each, then one mark of the slot), each chunk's offset from
    /// `base` when it is set. `appended` emits what follows the entries
    /// (a resolved slot's inline resolution). A full queue takes `full`,
    /// which emits the instruction's helper (the runtime grows the queue).
    template <typename Appended, typename Full>
    void emit_queued_chunks(const std::uint32_t binding_index,
        const std::vector<QueuedChunk>& chunks, const std::uint32_t kind, llvm::Value* base,
        const std::uint32_t at, Appended&& appended, Full&& full)
    {
        auto* slot = builder_.CreateAnd(binding(binding_index, 1U),
            llvm::ConstantInt::get(i32_, ~silent_bit));
        auto* done = llvm::BasicBlock::Create(context_, "appended", &function_);
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        auto* next = builder_.CreateAdd(count,
            llvm::ConstantInt::get(i32_, static_cast<std::uint32_t>(chunks.size())));
        auto* fast = llvm::BasicBlock::Create(context_, "append", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "append_full", &function_);
        builder_.CreateCondBr(builder_.CreateICmpULE(next, capacity), fast, slow);
        builder_.SetInsertPoint(fast);
        auto* data = builder_.CreateLoad(ptr_, queue);
        auto* member = builder_.CreateLoad(i32_, frame_field(32U));
        for (std::uint32_t index = 0U; index < chunks.size(); ++index) {
            const auto& chunk = chunks[index];
            auto* entry = builder_.CreateInBoundsGEP(i8_, data,
                builder_.CreateMul(builder_.CreateZExt(
                                       builder_.CreateAdd(count,
                                           llvm::ConstantInt::get(i32_, index)),
                                       i64_),
                    constant(56U)));
            const auto field = [&](const std::uint64_t at_byte) {
                return builder_.CreateConstInBoundsGEP1_64(i8_, entry, at_byte);
            };
            builder_.CreateStore(slot, field(0U));
            builder_.CreateStore(base != nullptr
                    ? builder_.CreateAdd(base, llvm::ConstantInt::get(i32_, chunk.offset))
                    : llvm::ConstantInt::get(i32_, chunk.offset),
                field(4U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, chunk.width), field(8U));
            builder_.CreateStore(member, field(12U));
            builder_.CreateStore(chunk.value.a, field(16U));
            builder_.CreateStore(chunk.value.b, field(24U));
            builder_.CreateStore(chunk.unknown, field(32U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, kind), field(40U));
            // The pieces of one write after the first continue its update.
            builder_.CreateStore(llvm::ConstantInt::get(i32_,
                                     index != 0U ? at | detail::write_continues : at),
                field(44U));
            if (body_.weak != 0U) {
                builder_.CreateStore(chunk.weak != nullptr ? chunk.weak : constant(0U),
                    field(48U));
            }
        }
        builder_.CreateStore(next, builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        appended();
        emit_write_mark(slot, done);
        builder_.SetInsertPoint(slow);
        full();
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    /// Chunk `chunk` of `bits` bits of the wide register at storage index
    /// `reg`.
    [[nodiscard]] Pair wide_chunk_value(const std::uint32_t reg,
        const std::uint32_t chunk, const std::uint32_t bits)
    {
        auto* m = constant(mask_of(bits));
        return { builder_.CreateAnd(builder_.CreateLoad(i64_,
                                        chunk_address(reg + chunk, 0U)),
                     m),
            builder_.CreateAnd(builder_.CreateLoad(i64_, chunk_address(reg + chunk, 1U)), m) };
    }

    /// The weak mask of chunk `chunk` (`bits` bits) of a wide_store's source,
    /// which carries masks (aux) in a design with weak sources; null otherwise
    /// (execute_wide's weak lambda).
    [[nodiscard]] llvm::Value* wide_chunk_weak(const KInst& inst,
        const std::uint32_t chunks, const std::uint32_t chunk, const std::uint32_t bits)
    {
        if (!weak_mode() || inst.aux == 0U) {
            return nullptr;
        }
        return builder_.CreateAnd(
            builder_.CreateLoad(i64_, chunk_address(inst.x + chunks + chunk, 1U)),
            constant(mask_of(bits)));
    }

    /// A field [offset, offset + width) of the slot bound at `index` (`words`
    /// words per plane) set to `planes` (one value per plane of the slot,
    /// not yet masked), as StaticKernel::store_planes_word stores it, with
    /// the change notification of emit_store_wide_slot. With a `chunk` (a
    /// value) the field is at offset + 64 chunk, as for emit_store_wide_slot.
    void emit_store_field_planes(const std::uint32_t index, const std::uint64_t words,
        const std::uint32_t offset, const std::uint32_t width,
        const std::vector<llvm::Value*>& planes, llvm::Value* chunk = nullptr)
    {
        if (const auto* fixed = llvm::dyn_cast_or_null<llvm::ConstantInt>(chunk)) {
            emit_store_field_planes(index, words,
                offset + 64U * static_cast<std::uint32_t>(fixed->getZExtValue()), width,
                planes);
            return;
        }
        auto* info = binding(index, 1U);
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        const bool spans = shift != 0U && shift + width > 64U
            && (chunk != nullptr || word + 1U < words);
        auto* m = constant(mask_of(width));
        const auto address = [&](const std::uint64_t plane, const std::uint64_t at_word) {
            return chunk != nullptr ? slot_word_at(index, plane * words + at_word, chunk)
                                    : slot_word(index, plane * words + at_word);
        };
        struct Old {
            llvm::Value* low { };
            llvm::Value* high { };
        };
        std::vector<Old> old(planes.size());
        std::vector<llvm::Value*> next(planes.size());
        llvm::Value* changed = constant(0U);
        for (std::uint64_t plane = 0U; plane < planes.size(); ++plane) {
            old[plane].low = builder_.CreateLoad(i64_, address(plane, word));
            llvm::Value* field = builder_.CreateLShr(old[plane].low, constant(shift));
            if (spans) {
                old[plane].high = builder_.CreateLoad(i64_, address(plane, word + 1U));
                field = builder_.CreateOr(field,
                    builder_.CreateShl(old[plane].high, constant(64U - shift)));
            }
            field = builder_.CreateAnd(field, m);
            next[plane] = builder_.CreateAnd(planes[plane], m);
            changed = builder_.CreateOr(changed, builder_.CreateXor(field, next[plane]));
        }
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* write = llvm::BasicBlock::Create(context_, "write_planes", &function_);
        auto* notify = llvm::BasicBlock::Create(context_, "notify_planes", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored_planes", &function_);
        builder_.CreateCondBr(nonzero(changed), write, after);
        builder_.SetInsertPoint(write);
        for (std::uint64_t plane = 0U; plane < planes.size(); ++plane) {
            auto* low_mask = constant(~(mask_of(width) << shift));
            builder_.CreateStore(
                builder_.CreateOr(builder_.CreateAnd(old[plane].low, low_mask),
                    builder_.CreateShl(next[plane], constant(shift))),
                address(plane, word));
            if (spans) {
                const auto spill = 64U - shift;
                auto* high_mask = constant(~(mask_of(width) >> spill));
                builder_.CreateStore(
                    builder_.CreateOr(builder_.CreateAnd(old[plane].high, high_mask),
                        builder_.CreateLShr(next[plane], constant(spill))),
                    address(plane, word + 1U));
            }
        }
        builder_.CreateCondBr(silent, after, notify);
        builder_.SetInsertPoint(notify);
        auto* slot = builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit));
        emit_notify_skip(slot, after);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_notify_field", type);
        builder_.CreateCall(type, callee,
            { frame_, slot, changed,
                chunk != nullptr ? builder_.CreateTrunc(chunk_bit(offset, chunk), i32_)
                                 : llvm::ConstantInt::get(i32_, offset) });
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    /// emit_store_field_planes for a field of the run-time width `width`
    /// (1 to 64; `m` its bits) at the run-time bit `offset` of the slot
    /// bound at `index` (`words` words per plane): the planes (two, or four
    /// for a nine-value slot) of the field take `planes` (not yet masked),
    /// and a change is announced unless the slot is silent. The insert point
    /// is left after the store.
    void emit_store_field_planes_dynamic(const std::uint32_t index,
        const std::uint64_t words, llvm::Value* offset, llvm::Value* width,
        llvm::Value* m, const std::vector<llvm::Value*>& planes)
    {
        auto* info = binding(index, 1U);
        auto* word = builder_.CreateLShr(offset, constant(6U));
        auto* shift = builder_.CreateAnd(offset, constant(63U));
        auto* spans = builder_.CreateAnd(
            builder_.CreateICmpUGT(builder_.CreateAdd(shift, width), constant(64U)),
            builder_.CreateICmpULT(builder_.CreateAdd(word, constant(1U)), constant(words)));
        auto* next_word = builder_.CreateSelect(spans, builder_.CreateAdd(word, constant(1U)),
            word);
        auto* spill = builder_.CreateSub(constant(64U), shift);
        auto* base = builder_.CreateZExt(binding(index, 0U), i64_);
        struct Old {
            llvm::Value* low_address { };
            llvm::Value* high_address { };
            llvm::Value* low { };
            llvm::Value* high { };
        };
        std::vector<Old> old(planes.size());
        std::vector<llvm::Value*> next(planes.size());
        llvm::Value* changed = constant(0U);
        for (std::uint64_t plane = 0U; plane < planes.size(); ++plane) {
            auto* first = builder_.CreateAdd(base, constant(plane * words));
            old[plane].low_address = builder_.CreateInBoundsGEP(i64_, arena_,
                builder_.CreateAdd(first, word));
            old[plane].high_address = builder_.CreateInBoundsGEP(i64_, arena_,
                builder_.CreateAdd(first, next_word));
            old[plane].low = builder_.CreateLoad(i64_, old[plane].low_address);
            old[plane].high = builder_.CreateLoad(i64_, old[plane].high_address);
            auto* field = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(old[plane].low, shift),
                    builder_.CreateSelect(spans,
                        builder_.CreateShl(old[plane].high, spill), constant(0U))),
                m);
            next[plane] = builder_.CreateAnd(planes[plane], m);
            changed = builder_.CreateOr(changed, builder_.CreateXor(field, next[plane]));
        }
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* write = llvm::BasicBlock::Create(context_, "write_dynamic_planes", &function_);
        auto* notify = llvm::BasicBlock::Create(context_, "notify_dynamic_planes", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored_dynamic_planes", &function_);
        builder_.CreateCondBr(nonzero(changed), write, after);
        builder_.SetInsertPoint(write);
        for (std::uint64_t plane = 0U; plane < planes.size(); ++plane) {
            // The high word first: without a spill it is the low word, which
            // the low store then rewrites.
            builder_.CreateStore(
                builder_.CreateSelect(spans,
                    builder_.CreateOr(
                        builder_.CreateAnd(old[plane].high,
                            builder_.CreateNot(builder_.CreateLShr(m, spill))),
                        builder_.CreateLShr(next[plane], spill)),
                    old[plane].high),
                old[plane].high_address);
            builder_.CreateStore(
                builder_.CreateOr(
                    builder_.CreateAnd(old[plane].low,
                        builder_.CreateNot(builder_.CreateShl(m, shift))),
                    builder_.CreateShl(next[plane], shift)),
                old[plane].low_address);
        }
        builder_.CreateCondBr(silent, after, notify);
        builder_.SetInsertPoint(notify);
        auto* slot = builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit));
        emit_notify_skip(slot, after);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_notify_field", type);
        builder_.CreateCall(type, callee,
            { frame_, slot, changed, builder_.CreateTrunc(offset, i32_) });
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    /// The planes a VHDL value stores into an unresolved slot of `planes`
    /// planes (StaticKernel::store_planes_word): the nine-value codes of the
    /// word and its 'U' and weak masks (four planes), or the four-state
    /// word with 'W' and '-' read as X (two). `mask` the field's bits.
    [[nodiscard]] std::vector<llvm::Value*> slot_planes(const Pair value,
        llvm::Value* unknown, llvm::Value* weak, llvm::Value* mask,
        const std::uint32_t planes)
    {
        if (planes == 4U) {
            const auto codes = net_codes_masked(value, unknown, mask, weak);
            return { codes[0], codes[1], codes[2], codes[3] };
        }
        return { weak != nullptr
                ? builder_.CreateOr(value.a, builder_.CreateAnd(weak, value.b))
                : value.a,
            value.b };
    }

    /// A VHDL assignment that takes effect at once (a shared variable,
    /// IEEE 1076-2019 6.4.2.4) to an unresolved slot: KOp::store_vhdl with
    /// flag_blocking, sub 0 (the static bit offset, or per-instance binding
    /// z - 1; imm_a the slot's words per plane, imm_b its planes) or sub 1
    /// (a dynamic index through indices[aux]; imm_a the slot's width, z its
    /// planes). The field is stored in place with the change detection of
    /// native slot stores (StaticKernel::store_slot_word); an index that is
    /// unknown, outside the range or leaves the slot is the reference's to
    /// report (IEEE 1076-2019 8.4).
    void emit_vhdl_blocking_store(const KInst& inst, const std::uint32_t at)
    {
        const auto value = load_register(inst.x);
        auto* unknown = unknown_of(inst.x);
        llvm::Value* weak = weak_mode() && tracked(inst.x) ? weak_of(inst.x) : nullptr;
        const auto planes = static_cast<std::uint32_t>(inst.sub == 0U ? inst.imm_b : inst.z);
        if (inst.sub == 0U) {
            const auto words = inst.imm_a;
            auto* m = constant(mask_of(inst.width));
            auto* offset = inst.z != 0U
                ? builder_.CreateZExt(binding(inst.z - 1U, 0U), i64_)
                : constant(inst.offset);
            emit_store_field_planes_dynamic(inst.d, words, offset, constant(inst.width), m,
                slot_planes(value, unknown, weak, m, planes));
            return;
        }
        const auto& selection = body_.indices[inst.aux];
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        const auto index = load_register(inst.y);
        const std::uint64_t slot_width = inst.imm_a & 0xffffffffU;
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* position = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(position, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(position, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(position, right),
            builder_.CreateSub(position, right), builder_.CreateSub(right, position));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* fits = builder_.CreateICmpULE(builder_.CreateAdd(offset, constant(inst.width)),
            constant(slot_width));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        auto* direct = llvm::BasicBlock::Create(context_, "blocking_indexed", &function_);
        auto* reference = llvm::BasicBlock::Create(context_, "blocking_indexed_ref",
            &function_);
        auto* joined = llvm::BasicBlock::Create(context_, "blocking_indexed_done", &function_);
        builder_.CreateCondBr(ok, direct, reference);
        builder_.SetInsertPoint(direct);
        auto* m = constant(mask_of(inst.width));
        emit_store_field_planes_dynamic(inst.d, (slot_width + 63U) / 64U, offset,
            constant(inst.width), m, slot_planes(value, unknown, weak, m, planes));
        builder_.CreateBr(joined);
        builder_.SetInsertPoint(reference);
        call_effect(at, value, index, masks_of(inst.x), 2U);
        builder_.CreateBr(joined);
        builder_.SetInsertPoint(joined);
    }

    /// A VHDL shared variable's wide store (wide_store sub 2, flag_blocking;
    /// imm_b the slot's words per plane and, from bit 32, its planes): each
    /// 64-bit chunk is stored at once (store_planes_word: the codes of a
    /// Logic9 slot, 'W' and '-' read as X in a Logic4 one), notifying the
    /// slot's readers of the bits it changed (IEEE 1076-2019 10.2: a shared
    /// variable updates immediately).
    void emit_wide_shared_store(const KInst& inst)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        const auto words = inst.imm_b & 0xffffffffU;
        const auto planes = inst.imm_b >> 32U;
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            const auto bits = std::min(64U, inst.width - 64U * chunk);
            const auto value = wide_chunk_value(inst.x, chunk, bits);
            auto* unknown = inst.aux != 0U
                ? builder_.CreateAnd(builder_.CreateLoad(i64_,
                                         chunk_address(inst.x + chunks + chunk, 0U)),
                      constant(mask_of(bits)))
                : constant(0U);
            auto* weak = wide_chunk_weak(inst, chunks, chunk, bits);
            std::vector<llvm::Value*> next;
            if (planes == 4U) {
                const auto codes = net_codes(value, unknown, bits, weak);
                next = { codes[0], codes[1], codes[2], codes[3] };
            } else {
                next = { weak != nullptr
                        ? builder_.CreateOr(value.a, builder_.CreateAnd(weak, value.b))
                        : value.a,
                    value.b };
            }
            emit_store_field_planes(inst.d, words, inst.offset + 64U * chunk, bits, next);
        }
    }

    /// KOp::wide_store to a slot that is not resolved (imm_a 0), as
    /// StaticKernel's wide_store effect stores it, one 64-bit chunk at a
    /// time. A SystemVerilog blocking store (sub 0; imm_b the slot's words
    /// per plane) writes each chunk as the field store of a wide slot does;
    /// a nonblocking one (sub 1) and a VHDL signal assignment (sub 2) append
    /// one write per chunk to the round's queue. A VHDL shared variable
    /// (sub 2, flag_blocking) stores at once (emit_wide_shared_store).
    void emit_wide_slot_store(const KInst& inst, const std::uint32_t at)
    {
        if (loops_chunks(inst.width)) {
            emit_wide_slot_store_words(inst, at);
            return;
        }
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        const auto chunk_bits = [&](const std::uint32_t chunk) {
            return std::min(64U, inst.width - 64U * chunk);
        };
        if (inst.sub == 0U && inst.imm_b != 0U) {
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                auto field = inst;
                field.imm_a = inst.imm_b;
                field.offset = inst.offset + 64U * chunk;
                field.width = chunk_bits(chunk);
                auto value = wide_chunk_value(inst.x, chunk, chunk_bits(chunk));
                if (auto* weak = wide_chunk_weak(inst, chunks, chunk, chunk_bits(chunk))) {
                    // A Logic4 slot reads 'W' and '-' as X (coerce_weak).
                    value.a = builder_.CreateOr(value.a, builder_.CreateAnd(weak, value.b));
                }
                emit_store_wide_slot(field, value);
            }
            return;
        }
        if (inst.sub == 0U) {
            // compile_body gives every unresolved SystemVerilog wide store
            // its slot's words in imm_b.
            throw std::logic_error("static kernel wide store has no slot shape");
        }
        if (inst.sub == 2U && (inst.flags & detail::flag_blocking) != 0U) {
            emit_wide_shared_store(inst);
            return;
        }
        std::vector<QueuedChunk> queued;
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            // The chunk's 'U' mask follows the chunks (wide_unknown_flag).
            queued.push_back({ inst.offset + 64U * chunk, chunk_bits(chunk),
                wide_chunk_value(inst.x, chunk, chunk_bits(chunk)),
                inst.aux != 0U
                    ? builder_.CreateAnd(builder_.CreateLoad(i64_,
                                             chunk_address(inst.x + chunks + chunk, 0U)),
                          constant(mask_of(chunk_bits(chunk))))
                    : constant(0U),
                wide_chunk_weak(inst, chunks, chunk, chunk_bits(chunk)) });
        }
        emit_queued_chunks(inst.d, queued, 0U, nullptr, at, [] { }, [&] {
            (void)call_wide(at, zero_pair(), zero_pair(), zero_pair(), 1U);
        });
    }

    /// Whether the part selection of a KOp::wide_move (sub 1) takes result
    /// bit i from source bit first + i: the declared right bound is the low
    /// bound of a descending range or the high bound of an ascending one.
    [[nodiscard]] bool selects_one_field(const KInst& inst) const
    {
        const auto& part = body_.parts[inst.aux];
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        return (descending && part.right == std::min(part.left, part.right))
            || (!descending && part.right == std::max(part.left, part.right));
    }

    /// Whether emit_wide_move covers `inst`: the target is not resolved
    /// (imm_b the target's words per plane, from bit 32 the source slot's
    /// width) and a part selection is one field of the source.
    [[nodiscard]] bool moves_inline(const KInst& inst) const
    {
        return (inst.imm_b & 0xffffffffU) != 0U
            && (inst.sub == 0U || (inst.sub == 1U && selects_one_field(inst)));
    }

    /// `count` bits (1 to 64; a value) at bit `offset` (a value; the field
    /// lies inside the slot) of a Logic4 slot bound at `index` with `words`
    /// words per plane: its two planes (read_bits in StaticKernel::move_wide).
    [[nodiscard]] Pair dynamic_slot_field(const std::uint32_t index,
        const std::uint64_t words, llvm::Value* offset, llvm::Value* count)
    {
        auto* word = builder_.CreateLShr(offset, constant(6U));
        auto* shift = builder_.CreateAnd(offset, constant(63U));
        auto* spills = builder_.CreateAnd(
            builder_.CreateICmpNE(shift, constant(0U)),
            builder_.CreateAnd(
                builder_.CreateICmpUGT(builder_.CreateAdd(shift, count), constant(64U)),
                builder_.CreateICmpULT(builder_.CreateAdd(word, constant(1U)),
                    constant(words))));
        auto* next = builder_.CreateSelect(spills,
            builder_.CreateAdd(word, constant(1U)), word);
        auto* spill_shift = builder_.CreateAnd(
            builder_.CreateSub(constant(64U), shift), constant(63U));
        auto* m = builder_.CreateLShr(constant(mask_of(64U)),
            builder_.CreateSub(constant(64U), count));
        const auto plane = [&](const std::uint64_t first) {
            auto* start = slot_word(index, first);
            auto* low = builder_.CreateLoad(i64_,
                builder_.CreateInBoundsGEP(i64_, start, word));
            auto* high = builder_.CreateLoad(i64_,
                builder_.CreateInBoundsGEP(i64_, start, next));
            return builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(low, shift),
                    builder_.CreateSelect(spills,
                        builder_.CreateShl(high, spill_shift), constant(0U))),
                m);
        };
        return { plane(0U), plane(words) };
    }

    /// KOp::wide_move to a slot that is not resolved (moves_inline), as
    /// move_wide does it: the value (the source slot's bits, or its part
    /// selection of base y, with X where the selection is outside the
    /// source) is read whole, then written one 64-bit chunk at a time,
    /// immediately or (flag_nba) queued.
    void emit_wide_move(const KInst& inst, const std::uint32_t at)
    {
        if (loops_chunks(inst.width)) {
            emit_wide_move_words(inst, at);
            return;
        }
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        const auto source_words = inst.imm_a;
        const auto target_words = inst.imm_b & 0xffffffffU;
        const auto source_width = static_cast<std::int64_t>(inst.imm_b >> 32U);
        std::vector<QueuedChunk> moved;
        if (inst.sub == 0U) {
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                const auto bits = std::min(64U, inst.width - 64U * chunk);
                auto* m = constant(mask_of(bits));
                moved.push_back({ inst.offset + 64U * chunk, bits,
                    { builder_.CreateAnd(builder_.CreateLoad(i64_,
                                             slot_word(inst.x, chunk)),
                          m),
                        builder_.CreateAnd(builder_.CreateLoad(i64_,
                                               slot_word(inst.x, source_words + chunk)),
                            m) },
                    constant(0U) });
            }
        } else {
            const auto base = load_register(inst.y);
            const auto& part = body_.parts[inst.aux];
            const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
            const bool descending = (inst.flags & detail::flag_descending) != 0U;
            const bool two_state = (inst.flags & detail::flag_two_state) != 0U;
            const auto lower = std::min(part.left, part.right);
            const auto upper = std::max(part.left, part.right);
            const auto edge = static_cast<std::int64_t>(inst.width) - 1;
            const auto signed_constant = [&](const std::int64_t value) {
                return constant(static_cast<std::uint64_t>(value));
            };
            const auto smax = [&](llvm::Value* left, llvm::Value* right) {
                return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left, right);
            };
            const auto smin = [&](llvm::Value* left, llvm::Value* right) {
                return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left, right);
            };
            auto* known = builder_.CreateICmpEQ(
                builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
            auto* selected_right = builder_.CreateAdd(signed_index(base),
                signed_constant(increasing ? (descending ? 0 : edge)
                                           : (descending ? -edge : 0)));
            // Result bit i is source bit first + i, for the bits low .. high
            // that select an index of the source.
            auto* first = builder_.CreateAdd(
                signed_constant(static_cast<std::int64_t>(part.base_offset)),
                descending ? builder_.CreateSub(selected_right, signed_constant(part.right))
                           : builder_.CreateSub(signed_constant(part.right), selected_right));
            auto* low = descending
                ? builder_.CreateSub(signed_constant(lower), selected_right)
                : builder_.CreateSub(selected_right, signed_constant(upper));
            auto* high = descending
                ? builder_.CreateSub(signed_constant(upper), selected_right)
                : builder_.CreateSub(selected_right, signed_constant(lower));
            low = smax(smax(low, constant(0U)), builder_.CreateSub(constant(0U), first));
            high = smin(smin(high, signed_constant(edge)),
                builder_.CreateSub(signed_constant(source_width - 1), first));
            for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
                const auto bits = std::min(64U, inst.width - 64U * chunk);
                const auto chunk_low = static_cast<std::int64_t>(64U * chunk);
                // This chunk's share of the selected bits.
                auto* from = smax(low, signed_constant(chunk_low));
                auto* to = smin(high, signed_constant(chunk_low + bits - 1));
                auto* any = builder_.CreateAnd(known, builder_.CreateICmpSLE(from, to));
                auto* count = builder_.CreateSelect(any,
                    builder_.CreateAdd(builder_.CreateSub(to, from), constant(1U)),
                    constant(1U));
                auto* shift_out = builder_.CreateSelect(any,
                    builder_.CreateSub(from, signed_constant(chunk_low)), constant(0U));
                auto* offset = builder_.CreateSelect(any, builder_.CreateAdd(first, from),
                    constant(0U));
                const auto field = dynamic_slot_field(inst.x, source_words, offset, count);
                auto* count_mask = builder_.CreateLShr(constant(mask_of(64U)),
                    builder_.CreateSub(constant(64U), count));
                auto* keep = builder_.CreateNot(builder_.CreateShl(count_mask, shift_out));
                auto* fill = two_state ? constant(0U) : constant(mask_of(bits));
                const auto placed = [&](llvm::Value* plane) {
                    return builder_.CreateSelect(any,
                        builder_.CreateOr(builder_.CreateAnd(fill, keep),
                            builder_.CreateShl(plane, shift_out)),
                        fill);
                };
                moved.push_back({ inst.offset + 64U * chunk, bits,
                    { placed(field.a), placed(field.b) }, constant(0U) });
            }
        }
        if ((inst.flags & detail::flag_nba) == 0U) {
            for (const auto& chunk : moved) {
                auto field = inst;
                field.imm_a = target_words;
                field.offset = chunk.offset;
                field.width = chunk.width;
                emit_store_wide_slot(field, chunk.value);
            }
            return;
        }
        emit_queued_chunks(inst.d, moved, 0U, nullptr, at, [] { }, [&] {
            call_effect(at, zero_pair(),
                inst.sub == 1U ? load_register(inst.y) : zero_pair(), zero_pair(), 1U);
        });
    }

    // ---- KOp::wide_alu: arithmetic and logic over chunked wide registers ----
    //
    // A wide operand's planes (a, b: the Logic4 word; u, w: the 'U' and weak
    // masks of a register with masks, null without) are loaded from the
    // register file's chunks into integers of the operand's width, operated
    // on as such, and stored back chunk by chunk; the bit-level rules are
    // those of the reference value functions (binary_value, shift_value,
    // reduce_value, conditional_value ...) and of kernel_word for 64 bits.
    // Integers of exactly the register's width make every result already
    // modular and masked. The words of operands and results live in the
    // register file, so an operand may be the destination: every load comes
    // before the first store.

    struct WideVal {
        llvm::Value* a { };
        llvm::Value* b { };
        llvm::Value* u { };
        llvm::Value* w { };
    };

    [[nodiscard]] llvm::IntegerType* wide_type(const std::uint32_t bits)
    {
        return llvm::Type::getIntNTy(context_, bits);
    }

    [[nodiscard]] llvm::Value* wide_constant(const std::uint32_t bits,
        const std::uint64_t value)
    {
        return llvm::ConstantInt::get(wide_type(bits), value);
    }

    [[nodiscard]] llvm::Value* wide_ones(const std::uint32_t bits)
    {
        return llvm::Constant::getAllOnesValue(wide_type(bits));
    }

    [[nodiscard]] static bool wide_has(const KInst& inst, const std::uint32_t group,
        const std::uint32_t slot)
    {
        return (inst.aux & (group << slot)) != 0U;
    }

    /// Plane `plane` (0 a, 1 b, 2 'U', 3 weak) of the wide register at
    /// storage index `reg`, as an integer of `bits` bits.
    [[nodiscard]] llvm::Value* load_wide_plane(const std::uint32_t reg,
        const std::uint32_t bits, const std::uint32_t plane)
    {
        const auto chunks = detail::wide_chunks(bits);
        auto* type = wide_type(bits);
        llvm::Value* result = nullptr;
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            auto* address = plane < 2U ? chunk_address(reg + chunk, plane)
                                       : chunk_address(reg + chunks + chunk, plane - 2U);
            llvm::Value* part = builder_.CreateZExt(builder_.CreateLoad(i64_, address), type);
            if (chunk != 0U) {
                part = builder_.CreateShl(part, wide_constant(bits, 64U * chunk));
            }
            result = result == nullptr ? part : builder_.CreateOr(result, part);
        }
        return result;
    }

    /// `value` (an integer of `bits` bits; null: zero) into plane `plane` of
    /// the wide register at storage index `reg`.
    void store_wide_plane(const std::uint32_t reg, const std::uint32_t bits,
        const std::uint32_t plane, llvm::Value* value)
    {
        const auto chunks = detail::wide_chunks(bits);
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            llvm::Value* word = constant(0U);
            if (value != nullptr) {
                word = builder_.CreateTrunc(
                    chunk == 0U ? value
                                : builder_.CreateLShr(value, wide_constant(bits, 64U * chunk)),
                    i64_);
            }
            builder_.CreateStore(word, plane < 2U ? chunk_address(reg + chunk, plane)
                                                  : chunk_address(reg + chunks + chunk, plane - 2U));
        }
    }

    [[nodiscard]] WideVal load_wide(const std::uint32_t reg, const std::uint32_t bits,
        const bool masks)
    {
        WideVal value;
        value.a = load_wide_plane(reg, bits, 0U);
        value.b = load_wide_plane(reg, bits, 1U);
        if (masks) {
            value.u = load_wide_plane(reg, bits, 2U);
            // Without weak elements in the design the weak words are zero.
            if (body_.weak != 0U) {
                value.w = load_wide_plane(reg, bits, 3U);
            }
        }
        return value;
    }

    /// The result `value` into the destination chunks. A destination without
    /// masks reads a weak element through coerce_weak ('-', the word Z with
    /// its weak bit, as X; kernel_word::coerce_weak); 'U' is X in the word.
    void store_wide_result(const KInst& inst, WideVal value)
    {
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        if (!masks && value.w != nullptr) {
            value.a = builder_.CreateOr(value.a, builder_.CreateAnd(value.w, value.b));
        }
        store_wide_plane(inst.d, inst.width, 0U, value.a);
        store_wide_plane(inst.d, inst.width, 1U, value.b);
        if (masks) {
            store_wide_plane(inst.d, inst.width, 2U, value.u);
            store_wide_plane(inst.d, inst.width, 3U, value.w);
        }
    }

    /// truth_value() of a wide value: (is one, is unknown) as i1.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> wide_truth(
        const WideVal& value, const std::uint32_t bits)
    {
        auto* zero = wide_constant(bits, 0U);
        auto* one = builder_.CreateICmpNE(
            builder_.CreateAnd(value.a, builder_.CreateNot(value.b)), zero);
        auto* unknown = builder_.CreateAnd(builder_.CreateNot(one),
            builder_.CreateICmpNE(value.b, zero));
        return { one, unknown };
    }

    /// Restoring division of `bits`-bit integers, a bit at a time as the
    /// reference does (divide_known): quotient and remainder. A zero
    /// divisor gives garbage the caller replaces.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> emit_udivrem(
        llvm::Value* dividend, llvm::Value* divisor, const std::uint32_t bits)
    {
        auto* type = wide_type(bits);
        auto* before = builder_.GetInsertBlock();
        auto* head = llvm::BasicBlock::Create(context_, "div", &function_);
        auto* step = llvm::BasicBlock::Create(context_, "div.step", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "div.done", &function_);
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        auto* remaining = loop_phi(i32_, llvm::ConstantInt::get(i32_, bits), before, step);
        auto* quotient = loop_phi(type, wide_constant(bits, 0U), before, step);
        auto* remainder = loop_phi(type, wide_constant(bits, 0U), before, step);
        builder_.CreateCondBr(
            builder_.CreateICmpNE(remaining, llvm::ConstantInt::get(i32_, 0U)), step, done);
        builder_.SetInsertPoint(step->getTerminator());
        auto* index = builder_.CreateSub(remaining, llvm::ConstantInt::get(i32_, 1U));
        auto* shift = builder_.CreateZExt(index, type);
        auto* bit = builder_.CreateTrunc(builder_.CreateLShr(dividend, shift),
            builder_.getInt1Ty());
        auto* shifted = builder_.CreateOr(
            builder_.CreateShl(remainder, wide_constant(bits, 1U)),
            builder_.CreateZExt(bit, type));
        auto* fits = builder_.CreateICmpUGE(shifted, divisor);
        auto* next_remainder = builder_.CreateSelect(fits,
            builder_.CreateSub(shifted, divisor), shifted);
        auto* next_quotient = builder_.CreateSelect(fits,
            builder_.CreateOr(quotient, builder_.CreateShl(wide_constant(bits, 1U), shift)),
            quotient);
        close_loop_phi(remaining, index);
        close_loop_phi(quotient, next_quotient);
        close_loop_phi(remainder, next_remainder);
        builder_.SetInsertPoint(done);
        return { quotient, remainder };
    }

    /// base ** exponent by squaring over the exponent's bits, modulo 2^bits
    /// (power_known).
    [[nodiscard]] llvm::Value* emit_power(llvm::Value* base, llvm::Value* exponent,
        const std::uint32_t bits)
    {
        auto* type = wide_type(bits);
        auto* before = builder_.GetInsertBlock();
        auto* head = llvm::BasicBlock::Create(context_, "pow", &function_);
        auto* step = llvm::BasicBlock::Create(context_, "pow.step", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "pow.done", &function_);
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        auto* index = loop_phi(i32_, llvm::ConstantInt::get(i32_, 0U), before, step);
        auto* result = loop_phi(type, wide_constant(bits, 1U), before, step);
        auto* factor = loop_phi(type, base, before, step);
        builder_.CreateCondBr(
            builder_.CreateICmpULT(index, llvm::ConstantInt::get(i32_, bits)), step, done);
        builder_.SetInsertPoint(step->getTerminator());
        auto* bit = builder_.CreateTrunc(
            builder_.CreateLShr(exponent, builder_.CreateZExt(index, type)),
            builder_.getInt1Ty());
        auto* next_result = builder_.CreateSelect(bit,
            builder_.CreateMul(result, factor), result);
        auto* next_factor = builder_.CreateMul(factor, factor);
        close_loop_phi(index, builder_.CreateAdd(index, llvm::ConstantInt::get(i32_, 1U)));
        close_loop_phi(result, next_result);
        close_loop_phi(factor, next_factor);
        builder_.SetInsertPoint(done);
        return result;
    }

    /// KOp::wide_alu, WideFamily::binary: binary_value over equal-width
    /// wide operands. Comparisons give a scalar in the narrow register d.
    void emit_wide_binary(const KInst& inst)
    {
        using runtime::simir::BinaryOperator;
        const auto operation = static_cast<BinaryOperator>(inst.sub);
        const auto bits = inst.width;
        const auto left = load_wide(inst.x, bits, wide_has(inst, detail::wide_alu_masks, 1U));
        const auto right = load_wide(inst.y, bits, wide_has(inst, detail::wide_alu_masks, 2U));
        auto* zero = wide_constant(bits, 0U);
        auto* ones = wide_ones(bits);
        auto* unknown = builder_.CreateOr(left.b, right.b);
        auto* has_unknown = builder_.CreateICmpNE(unknown, zero);
        const auto invert = [&](llvm::Value* value) { return builder_.CreateNot(value); };
        const auto either = [&](llvm::Value* x, llvm::Value* y) -> llvm::Value* {
            return x == nullptr ? y : y == nullptr ? x : builder_.CreateOr(x, y);
        };
        // Equality of two masks, either of which may be absent (zero).
        const auto same_mask = [&](llvm::Value* x, llvm::Value* y) -> llvm::Value* {
            if (x == nullptr && y == nullptr) {
                return builder_.getTrue();
            }
            return builder_.CreateICmpEQ(x != nullptr ? x : zero, y != nullptr ? y : zero);
        };
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto flag = [&](llvm::Value* condition, const bool when_true) {
            return scalar_bool(when_true ? condition : builder_.CreateNot(condition));
        };
        // An arithmetic result: unknown operand bits make it all X.
        const auto arithmetic = [&](llvm::Value* value, llvm::Value* bad) {
            WideVal result;
            result.a = builder_.CreateSelect(bad, ones, value);
            result.b = builder_.CreateSelect(bad, ones, zero);
            store_wide_result(inst, result);
        };
        switch (operation) {
        case BinaryOperator::bit_and:
        case BinaryOperator::bit_or: {
            auto* zero_l = builder_.CreateAnd(invert(left.a), invert(left.b));
            auto* zero_r = builder_.CreateAnd(invert(right.a), invert(right.b));
            auto* one_l = builder_.CreateAnd(left.a, invert(left.b));
            auto* one_r = builder_.CreateAnd(right.a, invert(right.b));
            const bool conjunction = operation == BinaryOperator::bit_and;
            // The side that decides: a zero for AND, a one for OR.
            auto* decided = conjunction ? builder_.CreateOr(zero_l, zero_r)
                                        : builder_.CreateOr(one_l, one_r);
            auto* settled = conjunction ? builder_.CreateAnd(one_l, one_r)
                                        : builder_.CreateAnd(zero_l, zero_r);
            auto* x = invert(builder_.CreateOr(decided, settled));
            WideVal result;
            auto* known_one = conjunction ? settled : decided;
            result.a = builder_.CreateOr(known_one, x);
            result.b = x;
            if (masks) {
                // 'U' unless a strong 0 (AND) or 1 (OR) decides
                // (kernel_word::unknown_and / unknown_or).
                auto* pending = either(left.u, right.u);
                result.u = pending == nullptr ? nullptr
                                              : builder_.CreateAnd(pending, invert(decided));
            }
            store_wide_result(inst, result);
            return;
        }
        case BinaryOperator::bit_xor: {
            WideVal result;
            result.a = builder_.CreateOr(builder_.CreateXor(left.a, right.a), unknown);
            result.b = unknown;
            result.u = either(left.u, right.u);
            store_wide_result(inst, result);
            return;
        }
        case BinaryOperator::equal:
        case BinaryOperator::not_equal: {
            // A known differing bit decides the relation (IEEE 1800-2023
            // 11.4.5).
            auto* differ = builder_.CreateICmpNE(
                builder_.CreateAnd(builder_.CreateXor(left.a, right.a), invert(unknown)),
                zero);
            const bool inequality = operation == BinaryOperator::not_equal;
            store_register(inst.d,
                select(differ, flag(builder_.getTrue(), inequality),
                    select(has_unknown, x_value(1U),
                        flag(builder_.getTrue(), !inequality))));
            return;
        }
        case BinaryOperator::case_equal: {
            // 'U' and X differ only in the 'U' mask, 'H' and 1 (and 'L' and
            // 0, 'W' and X, '-' and Z) only in the weak mask.
            llvm::Value* equal = builder_.CreateAnd(
                builder_.CreateICmpEQ(left.a, right.a),
                builder_.CreateICmpEQ(left.b, right.b));
            equal = builder_.CreateAnd(equal, same_mask(left.u, right.u));
            equal = builder_.CreateAnd(equal, same_mask(left.w, right.w));
            store_register(inst.d, scalar_bool(equal));
            return;
        }
        case BinaryOperator::casez_equal: {
            auto* z = builder_.CreateOr(builder_.CreateAnd(invert(left.a), left.b),
                builder_.CreateAnd(invert(right.a), right.b));
            auto* differ = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateXor(left.a, right.a),
                    builder_.CreateXor(left.b, right.b)),
                invert(z));
            store_register(inst.d, scalar_bool(builder_.CreateICmpEQ(differ, zero)));
            return;
        }
        case BinaryOperator::casex_equal: {
            auto* differ = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateXor(left.a, right.a),
                    builder_.CreateXor(left.b, right.b)),
                invert(unknown));
            store_register(inst.d, scalar_bool(builder_.CreateICmpEQ(differ, zero)));
            return;
        }
        case BinaryOperator::wildcard_equal: {
            auto* care = invert(right.b);
            auto* left_unknown = builder_.CreateICmpNE(builder_.CreateAnd(left.b, care), zero);
            auto* known_differ = builder_.CreateICmpNE(
                builder_.CreateAnd(builder_.CreateXor(left.a, right.a),
                    builder_.CreateAnd(care, invert(left.b))),
                zero);
            auto* equal = builder_.CreateICmpEQ(
                builder_.CreateAnd(builder_.CreateXor(left.a, right.a), care), zero);
            store_register(inst.d,
                select(known_differ, scalar_bool(builder_.getFalse()),
                    select(left_unknown, x_value(1U), scalar_bool(equal))));
            return;
        }
        case BinaryOperator::less_unsigned:
        case BinaryOperator::less_equal_unsigned:
        case BinaryOperator::greater_unsigned:
        case BinaryOperator::greater_equal_unsigned:
        case BinaryOperator::less_signed:
        case BinaryOperator::less_equal_signed:
        case BinaryOperator::greater_signed:
        case BinaryOperator::greater_equal_signed: {
            llvm::Value* relation = nullptr;
            switch (operation) {
            case BinaryOperator::less_unsigned:
                relation = builder_.CreateICmpULT(left.a, right.a);
                break;
            case BinaryOperator::less_equal_unsigned:
                relation = builder_.CreateICmpULE(left.a, right.a);
                break;
            case BinaryOperator::greater_unsigned:
                relation = builder_.CreateICmpUGT(left.a, right.a);
                break;
            case BinaryOperator::greater_equal_unsigned:
                relation = builder_.CreateICmpUGE(left.a, right.a);
                break;
            case BinaryOperator::less_signed:
                relation = builder_.CreateICmpSLT(left.a, right.a);
                break;
            case BinaryOperator::less_equal_signed:
                relation = builder_.CreateICmpSLE(left.a, right.a);
                break;
            case BinaryOperator::greater_signed:
                relation = builder_.CreateICmpSGT(left.a, right.a);
                break;
            default:
                relation = builder_.CreateICmpSGE(left.a, right.a);
                break;
            }
            store_register(inst.d,
                select(has_unknown, x_value(1U), scalar_bool(relation)));
            return;
        }
        case BinaryOperator::add_unsigned:
        case BinaryOperator::add_signed:
            arithmetic(builder_.CreateAdd(left.a, right.a), has_unknown);
            return;
        case BinaryOperator::subtract_unsigned:
        case BinaryOperator::subtract_signed:
            arithmetic(builder_.CreateSub(left.a, right.a), has_unknown);
            return;
        case BinaryOperator::multiply_unsigned:
        case BinaryOperator::multiply_signed:
            arithmetic(builder_.CreateMul(left.a, right.a), has_unknown);
            return;
        case BinaryOperator::power_unsigned:
        case BinaryOperator::power_signed: {
            auto* positive = emit_power(left.a, right.a, bits);
            if (operation == BinaryOperator::power_unsigned) {
                arithmetic(positive, has_unknown);
                return;
            }
            // A negative exponent (power_known): 0 ** -n is X, 1 ** -n is 1,
            // (-1) ** -n is -1 for odd n else 1, anything else 0.
            auto* negative = builder_.CreateICmpSLT(right.a, zero);
            auto* base_zero = builder_.CreateICmpEQ(left.a, zero);
            auto* base_one = builder_.CreateICmpEQ(left.a, wide_constant(bits, 1U));
            auto* base_ones = builder_.CreateICmpEQ(left.a, ones);
            auto* odd = builder_.CreateTrunc(right.a, builder_.getInt1Ty());
            auto* reciprocal = builder_.CreateSelect(base_one, wide_constant(bits, 1U),
                builder_.CreateSelect(base_ones,
                    builder_.CreateSelect(odd, left.a, wide_constant(bits, 1U)), zero));
            arithmetic(builder_.CreateSelect(negative, reciprocal, positive),
                builder_.CreateOr(has_unknown, builder_.CreateAnd(negative, base_zero)));
            return;
        }
        case BinaryOperator::divide_unsigned:
        case BinaryOperator::modulo_unsigned: {
            auto [quotient, remainder] = emit_udivrem(left.a, right.a, bits);
            arithmetic(operation == BinaryOperator::divide_unsigned ? quotient : remainder,
                builder_.CreateOr(has_unknown, builder_.CreateICmpEQ(right.a, zero)));
            return;
        }
        case BinaryOperator::divide_signed:
        case BinaryOperator::remainder_signed:
        case BinaryOperator::modulo_signed: {
            // Magnitudes divide; the quotient is negative for operands of
            // different signs, the remainder takes the dividend's sign, and
            // modulo adds the divisor to a nonzero remainder of different
            // signs (divide_known_signed).
            auto* left_negative = builder_.CreateICmpSLT(left.a, zero);
            auto* right_negative = builder_.CreateICmpSLT(right.a, zero);
            auto* left_magnitude = builder_.CreateSelect(left_negative,
                builder_.CreateSub(zero, left.a), left.a);
            auto* right_magnitude = builder_.CreateSelect(right_negative,
                builder_.CreateSub(zero, right.a), right.a);
            auto [quotient, remainder] = emit_udivrem(left_magnitude, right_magnitude, bits);
            auto* differ = builder_.CreateXor(left_negative, right_negative);
            auto* signed_quotient = builder_.CreateSelect(differ,
                builder_.CreateSub(zero, quotient), quotient);
            auto* signed_remainder = builder_.CreateSelect(left_negative,
                builder_.CreateSub(zero, remainder), remainder);
            llvm::Value* value = signed_quotient;
            if (operation == BinaryOperator::remainder_signed) {
                value = signed_remainder;
            } else if (operation == BinaryOperator::modulo_signed) {
                value = builder_.CreateSelect(
                    builder_.CreateAnd(builder_.CreateICmpNE(signed_remainder, zero), differ),
                    builder_.CreateAdd(signed_remainder, right.a), signed_remainder);
            }
            arithmetic(value,
                builder_.CreateOr(has_unknown, builder_.CreateICmpEQ(right.a, zero)));
            return;
        }
        case BinaryOperator::vhdl_match_equal: {
            // The matching equality of std_ulogic arrays (IEEE 1076-2019
            // 9.2.3) over whole planes, as vhdl_match_parts and
            // kernel_word::vhdl_match_equal9 do per word: '-' (the word Z
            // with its weak bit) in either operand matches anything, 'L' and
            // 'H' count as 0 and 1. A known pair that differs gives '0',
            // else a pair with 'U' gives 'U' (X in the word, the result's
            // 'U' mask set), else one with X, Z or 'W' gives X, else '1'.
            const auto dont_care = [&](const WideVal& value) -> llvm::Value* {
                return value.w == nullptr
                    ? nullptr
                    : builder_.CreateAnd(builder_.CreateAnd(value.w, invert(value.a)),
                          value.b);
            };
            auto* dont_care_left = dont_care(left);
            auto* dont_care_right = dont_care(right);
            llvm::Value* active = ones;
            if (dont_care_left != nullptr || dont_care_right != nullptr) {
                active = invert(either(dont_care_left, dont_care_right));
            }
            auto* mismatch = builder_.CreateICmpNE(
                builder_.CreateAnd(
                    builder_.CreateAnd(builder_.CreateXor(left.a, right.a), invert(unknown)),
                    active),
                zero);
            llvm::Value* uninitialized = builder_.getFalse();
            llvm::Value* meta = builder_.CreateAnd(unknown, active);
            if (auto* either_u = either(left.u, right.u)) {
                uninitialized = builder_.CreateICmpNE(
                    builder_.CreateAnd(either_u, active), zero);
                meta = builder_.CreateAnd(meta, invert(either_u));
            }
            auto* indeterminate = builder_.CreateOr(uninitialized,
                builder_.CreateICmpNE(meta, zero));
            store_register(inst.d,
                select(mismatch, Pair { constant(0U), constant(0U) },
                    select(indeterminate, x_value(1U), Pair { constant(1U), constant(0U) })));
            // The 'U' mask of the result (unknown_after), as for the narrow
            // matching equality.
            last_unknown_ = builder_.CreateZExt(
                builder_.CreateAnd(uninitialized, builder_.CreateNot(mismatch)), i64_);
            return;
        }
        }
    }

    /// The shift of the planes `in` of `bits`-bit values by `magnitude` (an
    /// unsigned integer of `amount_bits` bits) for a non-reversed operator:
    /// shift_value's moves. The fill of an arithmetic shift repeats the
    /// bit it names in every plane.
    [[nodiscard]] WideVal emit_shift_core(const runtime::simir::ShiftOperator operation,
        const WideVal& in, llvm::Value* magnitude, const std::uint32_t amount_bits,
        const std::uint32_t bits)
    {
        using runtime::simir::ShiftOperator;
        auto* type = wide_type(bits);
        auto* amount_type = magnitude->getType();
        const auto plane = [&](llvm::Value* value, const auto& move) -> llvm::Value* {
            return value == nullptr ? nullptr : move(value);
        };
        WideVal out;
        if (operation == ShiftOperator::rotate_left
            || operation == ShiftOperator::rotate_right) {
            // The amount modulo the width, from its bits' residues.
            llvm::Value* rotation = llvm::ConstantInt::get(i64_, 0U);
            std::uint64_t residue = 1U % bits;
            for (std::uint32_t index = 0U; index < amount_bits; ++index) {
                auto* set = builder_.CreateTrunc(
                    builder_.CreateLShr(magnitude, llvm::ConstantInt::get(amount_type, index)),
                    builder_.getInt1Ty());
                auto* sum = builder_.CreateAdd(rotation, constant(residue));
                auto* wrapped = builder_.CreateSelect(
                    builder_.CreateICmpUGE(sum, constant(bits)),
                    builder_.CreateSub(sum, constant(bits)), sum);
                rotation = builder_.CreateSelect(set, wrapped, rotation);
                residue = (residue * 2U) % bits;
            }
            auto* turn = builder_.CreateZExt(rotation, type);
            // bits - rotation, or 0 for no rotation (then both halves are
            // the value itself).
            auto* back = builder_.CreateSelect(
                builder_.CreateICmpEQ(rotation, constant(0U)), wide_constant(bits, 0U),
                builder_.CreateSub(wide_constant(bits, bits), turn));
            const bool left = operation == ShiftOperator::rotate_left;
            const auto rotate = [&](llvm::Value* value) -> llvm::Value* {
                return builder_.CreateOr(
                    builder_.CreateShl(value, left ? turn : back),
                    builder_.CreateLShr(value, left ? back : turn));
            };
            out.a = plane(in.a, rotate);
            out.b = plane(in.b, rotate);
            out.u = plane(in.u, rotate);
            out.w = plane(in.w, rotate);
            return out;
        }
        // Amounts of the width or more fill the whole value.
        auto* saturated = builder_.CreateICmpUGE(magnitude,
            llvm::ConstantInt::get(amount_type, bits));
        auto* resized = amount_bits >= bits
            ? builder_.CreateTrunc(magnitude, type)
            : builder_.CreateZExt(magnitude, type);
        auto* amount = builder_.CreateSelect(saturated, wide_constant(bits, 0U), resized);
        auto* ones = wide_ones(bits);
        const bool left = operation == ShiftOperator::logical_left
            || operation == ShiftOperator::arithmetic_left;
        const bool arithmetic = operation == ShiftOperator::arithmetic_left
            || operation == ShiftOperator::arithmetic_right;
        const auto move = [&](llvm::Value* value) -> llvm::Value* {
            llvm::Value* fill = wide_constant(bits, 0U);
            if (arithmetic) {
                // arithmetic_right repeats the top element, arithmetic_left
                // the lowest (shift_value).
                auto* edge = builder_.CreateTrunc(
                    operation == ShiftOperator::arithmetic_right
                        ? builder_.CreateLShr(value, wide_constant(bits, bits - 1U))
                        : value,
                    builder_.getInt1Ty());
                fill = builder_.CreateSExt(edge, type);
            }
            llvm::Value* moved = left ? builder_.CreateShl(value, amount)
                                      : builder_.CreateLShr(value, amount);
            if (arithmetic) {
                auto* vacated = left ? builder_.CreateNot(builder_.CreateShl(ones, amount))
                                     : builder_.CreateNot(builder_.CreateLShr(ones, amount));
                moved = builder_.CreateOr(moved, builder_.CreateAnd(fill, vacated));
            }
            return builder_.CreateSelect(saturated, fill, moved);
        };
        out.a = plane(in.a, move);
        out.b = plane(in.b, move);
        out.u = plane(in.u, move);
        out.w = plane(in.w, move);
        return out;
    }

    /// KOp::wide_alu, WideFamily::shift: shift_value of a wide value by a
    /// narrow or wide amount.
    void emit_wide_shift(const KInst& inst)
    {
        using runtime::simir::ShiftOperator;
        auto operation = static_cast<ShiftOperator>(inst.sub);
        const auto bits = inst.width;
        const auto amount_width = static_cast<std::uint32_t>(inst.imm_a);
        const bool amount_wide = wide_has(inst, detail::wide_alu_wide, 2U);
        const auto value = load_wide(inst.x, bits, wide_has(inst, detail::wide_alu_masks, 1U));
        // The amount as an unsigned integer of `amount_bits` bits.
        const std::uint32_t amount_bits = amount_wide ? amount_width : 64U;
        auto* amount_type = amount_wide ? static_cast<llvm::Type*>(wide_type(amount_width))
                                        : static_cast<llvm::Type*>(i64_);
        llvm::Value* amount_a { };
        llvm::Value* amount_b { };
        if (amount_wide) {
            amount_a = load_wide_plane(inst.y, amount_width, 0U);
            amount_b = load_wide_plane(inst.y, amount_width, 1U);
        } else {
            const auto narrow = load_register(inst.y);
            amount_a = builder_.CreateAnd(narrow.a, constant(mask_of(amount_width)));
            amount_b = builder_.CreateAnd(narrow.b, constant(mask_of(amount_width)));
        }
        auto* amount_zero = llvm::ConstantInt::get(amount_type, 0U);
        auto* amount_unknown = builder_.CreateICmpNE(amount_b, amount_zero);
        WideVal result;
        if ((inst.flags & detail::flag_signed) != 0U) {
            // A negative signed amount reverses the operator and shifts by
            // its magnitude (shift_value).
            auto* negative = builder_.CreateTrunc(
                builder_.CreateLShr(amount_a, llvm::ConstantInt::get(amount_type, amount_width - 1U)),
                builder_.getInt1Ty());
            llvm::Value* negated = builder_.CreateSub(amount_zero, amount_a);
            if (!amount_wide) {
                negated = builder_.CreateAnd(negated, constant(mask_of(amount_width)));
            }
            auto* magnitude = builder_.CreateSelect(negative, negated, amount_a);
            ShiftOperator reversed = operation;
            switch (operation) {
            case ShiftOperator::logical_left:
                reversed = ShiftOperator::logical_right;
                break;
            case ShiftOperator::logical_right:
                reversed = ShiftOperator::logical_left;
                break;
            case ShiftOperator::arithmetic_left:
                reversed = ShiftOperator::arithmetic_right;
                break;
            case ShiftOperator::arithmetic_right:
                reversed = ShiftOperator::arithmetic_left;
                break;
            case ShiftOperator::rotate_left:
                reversed = ShiftOperator::rotate_right;
                break;
            case ShiftOperator::rotate_right:
                reversed = ShiftOperator::rotate_left;
                break;
            }
            const auto forward = emit_shift_core(operation, value, magnitude, amount_bits, bits);
            const auto backward = emit_shift_core(reversed, value, magnitude, amount_bits, bits);
            const auto choose = [&](llvm::Value* x, llvm::Value* y) -> llvm::Value* {
                return x == nullptr ? nullptr : builder_.CreateSelect(negative, y, x);
            };
            result = { choose(forward.a, backward.a), choose(forward.b, backward.b),
                choose(forward.u, backward.u), choose(forward.w, backward.w) };
        } else {
            result = emit_shift_core(operation, value, amount_a, amount_bits, bits);
        }
        // An unknown amount gives all X: Logic4, no masks.
        auto* ones = wide_ones(bits);
        auto* zero = wide_constant(bits, 0U);
        result.a = builder_.CreateSelect(amount_unknown, ones, result.a);
        result.b = builder_.CreateSelect(amount_unknown, ones, result.b);
        if (result.u != nullptr) {
            result.u = builder_.CreateSelect(amount_unknown, zero, result.u);
        }
        if (result.w != nullptr) {
            result.w = builder_.CreateSelect(amount_unknown, zero, result.w);
        }
        store_wide_result(inst, result);
    }

    /// KOp::wide_alu, WideFamily::dynamic_part_select: dynamic_part_select_value
    /// of a wide source into a wide result (IEEE 1800-2023 11.5.1). Result bit
    /// i is the source bit that the index `selected_right -/+ i` names; bits
    /// whose index lies outside the declared range or whose source bit lies
    /// outside the source read X (0 for two-state), and an unknown base
    /// selects nothing. The in-range bits form one interval of the result.
    /// When the declared right bound is the low bound of a descending range
    /// or the high bound of an ascending one, result bit i is source bit
    /// first + i; otherwise the source bits run the other way (K - i), and
    /// the interval comes from the bit-reversed source. Planes are integers
    /// of the larger of the two widths, so shifts never lose bits.
    void emit_wide_dynamic_part_select(const KInst& inst)
    {
        const auto& part = body_.parts[inst.imm_b];
        const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        const bool two_state = (inst.flags & detail::flag_two_state) != 0U;
        const auto bits = inst.width;
        const auto source_bits = static_cast<std::uint32_t>(inst.imm_a);
        const auto span = std::max(bits, source_bits);
        auto* type = wide_type(span);
        const auto lower = std::min(part.left, part.right);
        const auto upper = std::max(part.left, part.right);
        const auto edge = static_cast<std::int64_t>(bits) - 1;
        const auto signed_constant = [&](const std::int64_t value) {
            return constant(static_cast<std::uint64_t>(value));
        };
        const auto smax = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left, right);
        };
        const auto smin = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left, right);
        };
        // A 64 bit count or position as an integer of the plane type.
        const auto widen = [&](llvm::Value* value) {
            return builder_.CreateZExt(value, type);
        };
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto source = load_wide(inst.x, source_bits, masks);
        const auto base = load_register(inst.y);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* selected_right = builder_.CreateAdd(signed_index(base),
            signed_constant(increasing ? (descending ? 0 : edge)
                                       : (descending ? -edge : 0)));
        auto* ones = wide_ones(span);
        auto* zero = wide_constant(span, 0U);
        // The selected interval [low, high] of result bits.
        const bool field = (descending && part.right == lower)
            || (!descending && part.right == upper);
        llvm::Value* low = descending
            ? builder_.CreateSub(signed_constant(lower), selected_right)
            : builder_.CreateSub(selected_right, signed_constant(upper));
        llvm::Value* high = descending
            ? builder_.CreateSub(signed_constant(upper), selected_right)
            : builder_.CreateSub(selected_right, signed_constant(lower));
        // The source bit of result bit i: first + i (field), else K - i.
        llvm::Value* first = nullptr;
        if (field) {
            first = builder_.CreateAdd(
                signed_constant(static_cast<std::int64_t>(part.base_offset)),
                descending ? builder_.CreateSub(selected_right, signed_constant(part.right))
                           : builder_.CreateSub(signed_constant(part.right), selected_right));
            low = smax(smax(low, constant(0U)), builder_.CreateSub(constant(0U), first));
            high = smin(smin(high, signed_constant(edge)),
                builder_.CreateSub(signed_constant(static_cast<std::int64_t>(source_bits) - 1),
                    first));
        } else {
            first = descending
                ? builder_.CreateSub(
                      signed_constant(static_cast<std::int64_t>(part.base_offset) + part.right),
                      selected_right)
                : builder_.CreateAdd(selected_right,
                      signed_constant(static_cast<std::int64_t>(part.base_offset) - part.right));
            low = smax(smax(low, constant(0U)),
                builder_.CreateSub(first,
                    signed_constant(static_cast<std::int64_t>(source_bits) - 1)));
            high = smin(smin(high, signed_constant(edge)), first);
        }
        auto* any = builder_.CreateAnd(known, builder_.CreateICmpSLE(low, high));
        // Safe values when nothing is selected.
        auto* count = builder_.CreateSelect(any,
            builder_.CreateAdd(builder_.CreateSub(high, low), constant(1U)), constant(1U));
        auto* start = builder_.CreateSelect(any, low, constant(0U));
        auto* window = builder_.CreateSelect(any,
            builder_.CreateLShr(ones, widen(builder_.CreateSub(constant(span), count))),
            zero);
        auto* selected = builder_.CreateShl(window, widen(start));
        // The selected bits of a source plane at their result positions.
        llvm::Value* distance = nullptr;
        llvm::Value* backwards = nullptr;
        if (field) {
            distance = builder_.CreateSelect(any, builder_.CreateAdd(first, low), constant(0U));
        } else {
            // Result bit i is bit i + D of the bit-reversed source.
            auto* shift = builder_.CreateSub(
                signed_constant(static_cast<std::int64_t>(source_bits) - 1), first);
            backwards = builder_.CreateICmpSLT(shift, constant(0U));
            distance = builder_.CreateSelect(any,
                builder_.CreateSelect(backwards, builder_.CreateSub(constant(0U), shift), shift),
                constant(0U));
        }
        const auto place = [&](llvm::Value* plane) -> llvm::Value* {
            if (plane == nullptr) {
                return nullptr;
            }
            auto* wide = builder_.CreateZExt(plane, type);
            llvm::Value* moved = nullptr;
            if (field) {
                moved = builder_.CreateShl(
                    builder_.CreateAnd(builder_.CreateLShr(wide, widen(distance)), window),
                    widen(start));
            } else {
                auto* reversed = builder_.CreateZExt(
                    builder_.CreateUnaryIntrinsic(llvm::Intrinsic::bitreverse, plane), type);
                moved = builder_.CreateAnd(
                    builder_.CreateSelect(backwards,
                        builder_.CreateShl(reversed, widen(distance)),
                        builder_.CreateLShr(reversed, widen(distance))),
                    selected);
            }
            return moved;
        };
        // Elements outside the selection read X (0 for two-state), with no
        // mask.
        auto* fill = two_state ? zero : ones;
        const auto finish = [&](llvm::Value* moved, llvm::Value* background) -> llvm::Value* {
            if (moved == nullptr) {
                return nullptr;
            }
            return builder_.CreateTrunc(
                builder_.CreateOr(builder_.CreateAnd(background, builder_.CreateNot(selected)),
                    moved),
                wide_type(bits));
        };
        WideVal result;
        result.a = finish(place(source.a), fill);
        result.b = finish(place(source.b), fill);
        result.u = finish(place(source.u), zero);
        result.w = finish(place(source.w), zero);
        store_wide_result(inst, result);
    }

    /// KOp::wide_alu, WideFamily::reduce.
    void emit_wide_reduce(const KInst& inst)
    {
        using runtime::simir::ReductionOperator;
        const auto bits = inst.width;
        const auto value = load_wide(inst.x, bits, false);
        auto* zero = wide_constant(bits, 0U);
        auto* ones = builder_.CreateAnd(value.a, builder_.CreateNot(value.b));
        auto* zeros = builder_.CreateAnd(builder_.CreateNot(value.a),
            builder_.CreateNot(value.b));
        auto* unknown = builder_.CreateICmpNE(value.b, zero);
        const auto count = [&] {
            return builder_.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, ones);
        };
        switch (static_cast<ReductionOperator>(inst.sub)) {
        case ReductionOperator::one_hot:
            store_register(inst.d,
                scalar_bool(builder_.CreateICmpEQ(count(), wide_constant(bits, 1U))));
            return;
        case ReductionOperator::one_hot_or_zero:
            store_register(inst.d,
                scalar_bool(builder_.CreateICmpULE(count(), wide_constant(bits, 1U))));
            return;
        case ReductionOperator::bit_and:
            store_register(inst.d,
                select(builder_.CreateICmpNE(zeros, zero), Pair { constant(0U), constant(0U) },
                    select(unknown, x_value(1U), Pair { constant(1U), constant(0U) })));
            return;
        case ReductionOperator::bit_or:
            store_register(inst.d,
                select(builder_.CreateICmpNE(ones, zero), Pair { constant(1U), constant(0U) },
                    select(unknown, x_value(1U), Pair { constant(0U), constant(0U) })));
            return;
        case ReductionOperator::bit_xor: {
            auto* odd = builder_.CreateTrunc(count(), builder_.getInt1Ty());
            store_register(inst.d, select(unknown, x_value(1U), scalar_bool(odd)));
            return;
        }
        }
        store_register(inst.d, x_value(1U));
    }

    /// KOp::wide_alu, WideFamily::conditional: conditional_value with a wide
    /// pair of values and a scalar condition (IEEE 1800-2023 11.4.11).
    void emit_wide_conditional(const KInst& inst)
    {
        const auto bits = inst.width;
        const auto condition = load_register(inst.x);
        const auto when_true = load_wide(inst.y, bits, wide_has(inst, detail::wide_alu_masks, 2U));
        const auto when_false = load_wide(inst.z, bits, wide_has(inst, detail::wide_alu_masks, 3U));
        auto* a0 = builder_.CreateTrunc(condition.a, builder_.getInt1Ty());
        auto* b0 = builder_.CreateTrunc(condition.b, builder_.getInt1Ty());
        auto* is_one = builder_.CreateAnd(a0, builder_.CreateNot(b0));
        auto* is_zero = builder_.CreateAnd(builder_.CreateNot(a0), builder_.CreateNot(b0));
        auto* zero = wide_constant(bits, 0U);
        // Only equal known bits survive an ambiguous condition; equal X or Z
        // bits produce X.
        auto* same = builder_.CreateNot(builder_.CreateOr(
            builder_.CreateOr(when_true.b, when_false.b),
            builder_.CreateXor(when_true.a, when_false.a)));
        auto* differ = builder_.CreateNot(same);
        WideVal result;
        result.a = builder_.CreateSelect(is_one, when_true.a,
            builder_.CreateSelect(is_zero, when_false.a,
                builder_.CreateOr(builder_.CreateAnd(when_true.a, same), differ)));
        result.b = builder_.CreateSelect(is_one, when_true.b,
            builder_.CreateSelect(is_zero, when_false.b, differ));
        // A known condition selects a value with its masks; an ambiguous one
        // merges as Logic4.
        const auto selected = [&](llvm::Value* x, llvm::Value* y) -> llvm::Value* {
            if (x == nullptr && y == nullptr) {
                return nullptr;
            }
            return builder_.CreateSelect(is_one, x != nullptr ? x : zero,
                builder_.CreateSelect(is_zero, y != nullptr ? y : zero, zero));
        };
        result.u = selected(when_true.u, when_false.u);
        result.w = selected(when_true.w, when_false.w);
        store_wide_result(inst, result);
    }

    /// KOp::wide_alu, WideFamily::extract: bits [imm_a, imm_a + width) of
    /// the wide x of imm_b bits, plane by plane.
    void emit_wide_extract(const KInst& inst)
    {
        const auto source_bits = static_cast<std::uint32_t>(inst.imm_b);
        const auto value = load_wide(inst.x, source_bits,
            wide_has(inst, detail::wide_alu_masks, 1U));
        auto* shift = wide_constant(source_bits, inst.imm_a);
        const auto field = [&](llvm::Value* plane) -> llvm::Value* {
            return plane == nullptr
                ? nullptr
                : builder_.CreateTrunc(builder_.CreateLShr(plane, shift),
                      wide_type(inst.width));
        };
        store_wide_result(inst, { field(value.a), field(value.b), field(value.u),
                                    field(value.w) });
    }

    /// The source y of a wide insert (`bits` wide: imm_a): a wide register's
    /// planes, or a narrow register's word and, if tracked, its 'U' and weak
    /// masks.
    [[nodiscard]] WideVal load_insert_source(const KInst& inst, const std::uint32_t bits)
    {
        const auto source_bits = static_cast<std::uint32_t>(inst.imm_a);
        if (wide_has(inst, detail::wide_alu_wide, 2U)) {
            auto source = load_wide(inst.y, source_bits,
                wide_has(inst, detail::wide_alu_masks, 2U));
            // The planes in integers of the target's width.
            for (auto* plane : { &source.a, &source.b, &source.u, &source.w }) {
                if (*plane != nullptr) {
                    *plane = builder_.CreateZExt(*plane, wide_type(bits));
                }
            }
            return source;
        }
        auto* m = constant(mask_of(source_bits));
        const auto widen = [&](llvm::Value* word) -> llvm::Value* {
            return builder_.CreateZExt(builder_.CreateAnd(word, m), wide_type(bits));
        };
        const auto value = load_register(inst.y);
        WideVal source;
        source.a = widen(value.a);
        source.b = widen(value.b);
        if (tracked(inst.y) && !two_state_) {
            source.u = widen(unknown_of(inst.y));
            if (weak_mode()) {
                source.w = widen(weak_of(inst.y));
            }
        }
        return source;
    }

    /// `target` with `source` (null: zero) laid into the field of `mask`
    /// shifted left by `amount` (an integer of the target's width): per
    /// plane (target & ~field) | (source << amount).
    [[nodiscard]] llvm::Value* place_field(llvm::Value* target, llvm::Value* source,
        llvm::Value* field_mask, llvm::Value* amount)
    {
        if (target == nullptr && source == nullptr) {
            return nullptr;
        }
        llvm::Value* kept = target == nullptr
            ? nullptr
            : builder_.CreateAnd(target,
                  builder_.CreateNot(builder_.CreateShl(field_mask, amount)));
        llvm::Value* placed = source == nullptr ? nullptr : builder_.CreateShl(source, amount);
        return kept == nullptr ? placed : placed == nullptr ? kept
                                                            : builder_.CreateOr(kept, placed);
    }

    /// KOp::wide_alu, WideFamily::insert: the source at the constant bit
    /// imm_b of the target (Insert; IEEE 1800-2023 11.4.12 for a concatenated
    /// element, 1076-2019 for an aggregate element).
    void emit_wide_insert(const KInst& inst)
    {
        const auto bits = inst.width;
        const auto target = load_wide(inst.x, bits, wide_has(inst, detail::wide_alu_masks, 1U));
        const auto source = load_insert_source(inst, bits);
        auto* field_mask = llvm::ConstantInt::get(context_,
            llvm::APInt::getLowBitsSet(bits, static_cast<std::uint32_t>(inst.imm_a)));
        auto* amount = wide_constant(bits, inst.imm_b);
        store_wide_result(inst, { place_field(target.a, source.a, field_mask, amount),
                                    place_field(target.b, source.b, field_mask, amount),
                                    place_field(target.u, source.u, field_mask, amount),
                                    place_field(target.w, source.w, field_mask, amount) });
    }

    /// KOp::wide_alu, WideFamily::dynamic_insert: as dynamic_insert for a
    /// target of any width (IEEE 1800-2023 11.5.1: a write through an
    /// unknown or out-of-range index, or one whose field leaves the target,
    /// has no effect; a strict one is an error, reason 9).
    void emit_wide_dynamic_insert(const KInst& inst, const std::uint32_t at)
    {
        const auto bits = inst.width;
        const auto source_bits = static_cast<std::uint32_t>(inst.imm_a);
        const auto& selection = body_.indices[static_cast<std::uint32_t>(inst.imm_b)];
        const auto index = load_register(inst.z);
        const auto lower = std::min(selection.left, selection.right);
        const auto upper = std::max(selection.left, selection.right);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
        auto* value = signed_index(index);
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(static_cast<std::uint64_t>(lower))),
            builder_.CreateICmpSLE(value, constant(static_cast<std::uint64_t>(upper))));
        auto* right = constant(static_cast<std::uint64_t>(selection.right));
        auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(value, right),
            builder_.CreateSub(value, right), builder_.CreateSub(right, value));
        auto* offset = builder_.CreateAdd(distance, constant(selection.base_offset));
        auto* fits = builder_.CreateICmpULE(
            builder_.CreateAdd(offset, constant(source_bits)), constant(bits));
        auto* ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
        if ((inst.flags & detail::flag_strict) != 0U) {
            check_or_fail(known, at, 13U);
            check_or_fail(ok, at, 9U);
        }
        const auto target = load_wide(inst.x, bits, wide_has(inst, detail::wide_alu_masks, 1U));
        const auto source = load_insert_source(inst, bits);
        auto* field_mask = llvm::ConstantInt::get(context_,
            llvm::APInt::getLowBitsSet(bits, source_bits));
        auto* amount = builder_.CreateZExt(builder_.CreateSelect(ok, offset, constant(0U)),
            wide_type(bits));
        const auto written = [&](llvm::Value* into, llvm::Value* from) -> llvm::Value* {
            auto* placed = place_field(into, from, field_mask, amount);
            if (placed == nullptr) {
                return nullptr;
            }
            // Outside the target or the range the target stays as it is.
            return builder_.CreateSelect(ok, placed,
                into != nullptr ? into : wide_constant(bits, 0U));
        };
        store_wide_result(inst, { written(target.a, source.a), written(target.b, source.b),
                                    written(target.u, source.u), written(target.w, source.w) });
    }

    // Wide operations over more than wide_word_loop_chunks chunks work a 64
    // bit word at a time in IR loops over the register words (owner 11-08):
    // one LLVM integer of the whole width makes the backend's legalization
    // grow with the width (seconds at 69,120 bits, ivtest pr734). Each
    // family gives the planes its integer path gives, bit for bit.
    static constexpr std::uint32_t wide_word_loop_chunks = 8U;

    using Carried = std::vector<llvm::Value*>;

    /// The planes of a value as word loops read and write them: word k of
    /// plane p (0 a, 1 b, 2 'U', 3 weak) is the i64 at base + offset[p] +
    /// stride * k. An absent plane reads zero and takes no writes; a narrow
    /// register's planes (base null) are its one word each, in `narrow`.
    struct WordPlanes {
        llvm::Value* base { };
        std::uint64_t stride { 1U };
        std::array<std::uint64_t, 4> offset { };
        std::array<bool, 4> present { };
        std::array<llvm::Value*, 4> narrow { };
        std::uint32_t bits { };
        // The register-file storage indices it covers (none for scratch).
        std::uint32_t first { };
        std::uint32_t span { };
    };

    /// The wide register at storage index `reg` (`bits` wide).
    [[nodiscard]] WordPlanes register_words(const std::uint32_t reg,
        const std::uint32_t bits, const bool masks, const bool weak)
    {
        const auto chunks = detail::wide_chunks(bits);
        const auto planes = static_cast<std::uint64_t>(reg) * 2U;
        const auto mask_planes = static_cast<std::uint64_t>(reg + chunks) * 2U;
        WordPlanes words;
        words.base = registers_;
        words.stride = 2U;
        words.offset = { planes, planes + 1U, mask_planes, mask_planes + 1U };
        words.present = { true, true, masks, masks && weak };
        words.bits = bits;
        words.first = reg;
        words.span = 2U * chunks;
        return words;
    }

    /// A wide operand's planes as load_wide reads them.
    [[nodiscard]] WordPlanes operand_words(const std::uint32_t reg,
        const std::uint32_t bits, const bool masks)
    {
        return register_words(reg, bits, masks, body_.weak != 0U);
    }

    /// `bits`-bit scratch words with the planes `present`, in the function's
    /// word scratch: one entry-block alloca as large as the largest
    /// operation needs, its blocks reused by the next operation.
    [[nodiscard]] WordPlanes scratch_words(const std::uint32_t bits,
        const std::array<bool, 4>& present)
    {
        const auto chunks = static_cast<std::uint64_t>(detail::wide_chunks(bits));
        std::uint64_t planes = 0U;
        for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
            if (present[plane]) {
                planes = plane + 1U;
            }
        }
        if (word_scratch_ == nullptr) {
            word_scratch_ = entry_builder().CreateAlloca(i64_, constant(1U), "words");
            word_scratch_size_ = 1U;
        }
        const auto start = word_scratch_used_;
        word_scratch_used_ += planes * chunks;
        if (word_scratch_used_ > word_scratch_size_) {
            word_scratch_size_ = word_scratch_used_;
            word_scratch_->setOperand(0U, constant(word_scratch_size_));
        }
        WordPlanes words;
        words.base = builder_.CreateConstInBoundsGEP1_64(i64_, word_scratch_, start);
        words.offset = { 0U, chunks, 2U * chunks, 3U * chunks };
        words.present = present;
        words.bits = bits;
        return words;
    }

    /// One plane (a) of `bits`-bit scratch words.
    [[nodiscard]] WordPlanes scratch_plane(const std::uint32_t bits)
    {
        return scratch_words(bits, { true, false, false, false });
    }

    [[nodiscard]] llvm::Value* word_address(const WordPlanes& words,
        const std::uint32_t plane, llvm::Value* k)
    {
        auto* index = builder_.CreateAdd(constant(words.offset[plane]),
            builder_.CreateMul(k, constant(words.stride)));
        return builder_.CreateInBoundsGEP(i64_, words.base, index);
    }

    /// The bits of word k of a `bits`-bit value that lie inside its width.
    [[nodiscard]] llvm::Value* valid_bits(const std::uint32_t bits, llvm::Value* k)
    {
        const auto rest = bits % 64U;
        if (rest == 0U) {
            return constant(~std::uint64_t { 0 });
        }
        return builder_.CreateSelect(
            builder_.CreateICmpEQ(k, constant(detail::wide_chunks(bits) - 1U)),
            constant(mask_of(rest)), constant(~std::uint64_t { 0 }));
    }

    /// Word k (below the chunk count) of plane `plane`, its bits past the
    /// width clear.
    [[nodiscard]] llvm::Value* read_word(const WordPlanes& words,
        const std::uint32_t plane, llvm::Value* k)
    {
        if (!words.present[plane]) {
            return constant(0U);
        }
        if (words.base == nullptr) {
            return builder_.CreateSelect(builder_.CreateICmpEQ(k, constant(0U)),
                words.narrow[plane], constant(0U));
        }
        return builder_.CreateAnd(builder_.CreateLoad(i64_, word_address(words, plane, k)),
            valid_bits(words.bits, k));
    }

    /// `value` into word k of plane `plane`, its bits past the width clear.
    void write_word(const WordPlanes& words, const std::uint32_t plane, llvm::Value* k,
        llvm::Value* value)
    {
        if (!words.present[plane]) {
            return;
        }
        builder_.CreateStore(builder_.CreateAnd(value, valid_bits(words.bits, k)),
            word_address(words, plane, k));
    }

    [[nodiscard]] WideVal read_words(const WordPlanes& words, llvm::Value* k)
    {
        WideVal value;
        value.a = read_word(words, 0U, k);
        value.b = read_word(words, 1U, k);
        if (words.present[2]) {
            value.u = read_word(words, 2U, k);
        }
        if (words.present[3]) {
            value.w = read_word(words, 3U, k);
        }
        return value;
    }

    /// The 64 bits of plane `plane` from the signed bit position `position`:
    /// bits below bit 0 read `below`'s, bits at or past the width `above`'s
    /// (words of fill).
    [[nodiscard]] llvm::Value* word_at(const WordPlanes& words, const std::uint32_t plane,
        llvm::Value* position, llvm::Value* below, llvm::Value* above)
    {
        if (!words.present[plane]) {
            return constant(0U);
        }
        const auto chunks = detail::wide_chunks(words.bits);
        const auto fetch = [&](llvm::Value* index) -> llvm::Value* {
            auto* negative = builder_.CreateICmpSLT(index, constant(0U));
            auto* past = builder_.CreateICmpSGE(index, constant(chunks));
            auto* clamped = builder_.CreateSelect(negative, constant(0U),
                builder_.CreateSelect(past, constant(chunks - 1U), index));
            auto* valid = valid_bits(words.bits, clamped);
            llvm::Value* word = words.base == nullptr
                ? words.narrow[plane]
                : builder_.CreateLoad(i64_, word_address(words, plane, clamped));
            word = builder_.CreateOr(builder_.CreateAnd(word, valid),
                builder_.CreateAnd(above, bit_not(valid)));
            return builder_.CreateSelect(negative, below,
                builder_.CreateSelect(past, above, word));
        };
        auto* index = builder_.CreateAShr(position, constant(6U));
        auto* shift = builder_.CreateAnd(position, constant(63U));
        auto* low = fetch(index);
        auto* high = fetch(builder_.CreateAdd(index, constant(1U)));
        // (high << 1) << (63 - shift): no shift by 64 when shift is 0.
        return builder_.CreateOr(builder_.CreateLShr(low, shift),
            builder_.CreateShl(builder_.CreateShl(high, constant(1U)),
                builder_.CreateSub(constant(63U), shift)));
    }

    /// The bits of word k inside the bit interval [low, high) (signed).
    [[nodiscard]] llvm::Value* field_word(llvm::Value* k, llvm::Value* low, llvm::Value* high)
    {
        auto* origin = builder_.CreateShl(k, constant(6U));
        const auto clamp = [&](llvm::Value* bound) -> llvm::Value* {
            auto* relative = builder_.CreateSub(bound, origin);
            relative = builder_.CreateSelect(
                builder_.CreateICmpSLT(relative, constant(0U)), constant(0U), relative);
            return builder_.CreateSelect(
                builder_.CreateICmpSGT(relative, constant(64U)), constant(64U), relative);
        };
        auto* start = clamp(low);
        auto* end = clamp(high);
        auto* count = builder_.CreateSelect(builder_.CreateICmpSGT(end, start),
            builder_.CreateSub(end, start), constant(0U));
        auto* run = builder_.CreateSelect(builder_.CreateICmpEQ(count, constant(64U)),
            constant(~std::uint64_t { 0 }),
            builder_.CreateSub(
                builder_.CreateShl(constant(1U), builder_.CreateAnd(count, constant(63U))),
                constant(1U)));
        return builder_.CreateSelect(builder_.CreateICmpEQ(count, constant(0U)), constant(0U),
            builder_.CreateShl(run, builder_.CreateAnd(start, constant(63U))));
    }

    /// for (k = 0; k < count; ++k) carried = body(k, carried), in IR. The
    /// body may emit loops of its own: whatever block it ends in branches
    /// to the latch. Returns the carried values after the loop.
    template <typename Body>
    Carried word_loop(llvm::Value* count, const Carried& initial, const Body& body)
    {
        auto* before = builder_.GetInsertBlock();
        auto* head = llvm::BasicBlock::Create(context_, "words", &function_);
        auto* step = llvm::BasicBlock::Create(context_, "words.step", &function_);
        auto* latch = llvm::BasicBlock::Create(context_, "words.next", &function_);
        auto* done = llvm::BasicBlock::Create(context_, "words.done", &function_);
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        llvm::BranchInst::Create(head, latch);
        auto* index = loop_phi(i64_, constant(0U), before, latch);
        std::vector<llvm::PHINode*> phis;
        Carried current;
        for (auto* value : initial) {
            phis.push_back(loop_phi(value->getType(), value, before, latch));
            current.push_back(phis.back());
        }
        builder_.CreateCondBr(builder_.CreateICmpULT(index, count), step, done);
        builder_.SetInsertPoint(step);
        const Carried next = body(static_cast<llvm::Value*>(index), current);
        auto* following = builder_.CreateAdd(index, constant(1U));
        builder_.CreateBr(latch);
        close_loop_phi(index, following);
        for (std::size_t slot = 0U; slot < phis.size(); ++slot) {
            close_loop_phi(phis[slot], next[slot]);
        }
        builder_.SetInsertPoint(done);
        return current;
    }

    /// A loop over the words of a `bits`-bit value.
    template <typename Body>
    Carried each_word(const std::uint32_t bits, const Carried& initial, const Body& body)
    {
        return word_loop(constant(detail::wide_chunks(bits)), initial, body);
    }

    // ---- Wide loads, stores, moves and copies as word loops ----
    //
    // Above wide_word_loop_chunks chunks the chunk-by-chunk emitters (wide
    // load, copy, constant, concatenation, slot, shared and resolved
    // stores, moves, memory elements, part inserts) work one chunk at a time
    // in a word loop, as the arithmetic does (owner 11-08); each writes the
    // chunks its unrolled form writes.

    /// Whether a value of `bits` bits moves in word loops.
    [[nodiscard]] static bool loops_chunks(const std::uint64_t bits)
    {
        return (bits + 63U) / 64U > wide_word_loop_chunks;
    }

    /// body(chunk, bits) for every 64-bit chunk of a `width`-bit value: a
    /// word loop over the full chunks (chunk an i64 value, bits 64), then the
    /// top chunk (chunk a constant, bits the rest of the width).
    template <typename Body>
    void each_chunk(const std::uint32_t width, const Body& body)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(width));
        (void)word_loop(constant(chunks - 1U), Carried {},
            [&](llvm::Value* chunk, const Carried&) {
                body(chunk, 64U);
                return Carried {};
            });
        body(constant(chunks - 1U), width - 64U * (chunks - 1U));
    }

    /// each_chunk carrying values: carried = body(chunk, bits, carried).
    template <typename Body>
    Carried fold_chunks(const std::uint32_t width, const Carried& initial, const Body& body)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(width));
        const auto carried = word_loop(constant(chunks - 1U), initial,
            [&](llvm::Value* chunk, const Carried& current) {
                return body(chunk, 64U, current);
            });
        return body(constant(chunks - 1U), width - 64U * (chunks - 1U), carried);
    }

    /// The i64 bit `first` + 64 chunk.
    [[nodiscard]] llvm::Value* chunk_bit(const std::uint64_t first, llvm::Value* chunk)
    {
        return builder_.CreateAdd(constant(first), builder_.CreateShl(chunk, constant(6U)));
    }

    /// Arena word `first` + `word` (a value) of the slot bound at `index`.
    [[nodiscard]] llvm::Value* slot_word_at(const std::uint32_t index,
        const std::uint64_t first, llvm::Value* word)
    {
        return builder_.CreateInBoundsGEP(i64_, slot_pointer(index),
            builder_.CreateAdd(constant(first), word));
    }

    /// wide_chunk_value of chunk `chunk` (a value).
    [[nodiscard]] Pair wide_chunk_value(const std::uint32_t reg, llvm::Value* chunk,
        const std::uint32_t bits)
    {
        auto* m = constant(mask_of(bits));
        return { builder_.CreateAnd(
                     builder_.CreateLoad(i64_, dynamic_chunk_address(reg, chunk, 0U)), m),
            builder_.CreateAnd(
                builder_.CreateLoad(i64_, dynamic_chunk_address(reg, chunk, 1U)), m) };
    }

    /// wide_chunk_weak of chunk `chunk` (a value).
    [[nodiscard]] llvm::Value* wide_chunk_weak(const KInst& inst,
        const std::uint32_t chunks, llvm::Value* chunk, const std::uint32_t bits)
    {
        if (!weak_mode() || inst.aux == 0U) {
            return nullptr;
        }
        return builder_.CreateAnd(
            builder_.CreateLoad(i64_, dynamic_chunk_address(inst.x + chunks, chunk, 1U)),
            constant(mask_of(bits)));
    }

    /// The 'U' mask of chunk `chunk` (a value) of a wide store's source x,
    /// which follows the chunks when the source carries masks (aux); 0
    /// otherwise.
    [[nodiscard]] llvm::Value* wide_chunk_unknown(const KInst& inst,
        const std::uint32_t chunks, llvm::Value* chunk, const std::uint32_t bits)
    {
        if (inst.aux == 0U) {
            return constant(0U);
        }
        return builder_.CreateAnd(
            builder_.CreateLoad(i64_, dynamic_chunk_address(inst.x + chunks, chunk, 0U)),
            constant(mask_of(bits)));
    }

    /// A private constant table of `words` in the kernel's module, which a
    /// word loop reads by index.
    [[nodiscard]] llvm::Value* constant_table(const std::vector<std::uint64_t>& words)
    {
        auto* type = llvm::ArrayType::get(i64_, words.size());
        return new llvm::GlobalVariable(*function_.getParent(), type, true,
            llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantDataArray::get(context_, llvm::ArrayRef<std::uint64_t>(words)),
            "wide_constants");
    }

    /// Word `index` (a value) of a constant_table.
    [[nodiscard]] llvm::Value* table_word(llvm::Value* table, llvm::Value* index)
    {
        return builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_, table, index));
    }

    /// emit_queued_chunks over the chunks of a `width`-bit value in a word
    /// loop: chunk k's write (of `kind`) is at bit first + 64 k (from `base`
    /// when it is set), its value, 'U' and weak masks from chunk(k, bits) (a
    /// QueuedChunk whose offset and width are not used).
    template <typename Chunk, typename Appended, typename Full>
    void emit_queued_words(const std::uint32_t binding_index, const std::uint32_t width,
        const std::uint32_t first, const std::uint32_t kind, llvm::Value* base,
        const std::uint32_t at, const Chunk& chunk, Appended&& appended, Full&& full)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(width));
        auto* slot = builder_.CreateAnd(binding(binding_index, 1U),
            llvm::ConstantInt::get(i32_, ~silent_bit));
        auto* done = llvm::BasicBlock::Create(context_, "appended", &function_);
        auto* queue = builder_.CreateLoad(ptr_, frame_field(56U));
        auto* count = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        auto* capacity = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i8_, queue, 12U));
        auto* next = builder_.CreateAdd(count, llvm::ConstantInt::get(i32_, chunks));
        auto* fast = llvm::BasicBlock::Create(context_, "append", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "append_full", &function_);
        builder_.CreateCondBr(builder_.CreateICmpULE(next, capacity), fast, slow);
        builder_.SetInsertPoint(fast);
        auto* data = builder_.CreateLoad(ptr_, queue);
        auto* member = builder_.CreateLoad(i32_, frame_field(32U));
        each_chunk(width, [&](llvm::Value* index, const std::uint32_t bits) {
            const auto written = chunk(index, bits);
            auto* entry = builder_.CreateInBoundsGEP(i8_, data,
                builder_.CreateMul(
                    builder_.CreateAdd(builder_.CreateZExt(count, i64_), index),
                    constant(56U)));
            const auto field = [&](const std::uint64_t at_byte) {
                return builder_.CreateConstInBoundsGEP1_64(i8_, entry, at_byte);
            };
            auto* offset = builder_.CreateTrunc(chunk_bit(first, index), i32_);
            builder_.CreateStore(slot, field(0U));
            builder_.CreateStore(base != nullptr ? builder_.CreateAdd(base, offset) : offset,
                field(4U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, bits), field(8U));
            builder_.CreateStore(member, field(12U));
            builder_.CreateStore(written.value.a, field(16U));
            builder_.CreateStore(written.value.b, field(24U));
            builder_.CreateStore(written.unknown, field(32U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, kind), field(40U));
            // The pieces of one write after the first continue its update.
            builder_.CreateStore(builder_.CreateSelect(
                                     builder_.CreateICmpEQ(index, constant(0U)),
                                     llvm::ConstantInt::get(i32_, at),
                                     llvm::ConstantInt::get(i32_, at | detail::write_continues)),
                field(44U));
            if (body_.weak != 0U) {
                builder_.CreateStore(written.weak != nullptr ? written.weak : constant(0U),
                    field(48U));
            }
        });
        builder_.CreateStore(next, builder_.CreateConstInBoundsGEP1_64(i8_, queue, 8U));
        appended();
        emit_write_mark(slot, done);
        builder_.SetInsertPoint(slow);
        full();
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    /// KOp::wide_copy in word loops.
    void emit_wide_copy_words(const KInst& inst)
    {
        (void)word_loop(constant(inst.aux), Carried {},
            [&](llvm::Value* chunk, const Carried&) {
                llvm::Value* a = builder_.CreateLoad(i64_,
                    dynamic_chunk_address(inst.x, chunk, 0U));
                auto* b = builder_.CreateLoad(i64_, dynamic_chunk_address(inst.x, chunk, 1U));
                if (weak_mode() && inst.sub == 1U) {
                    // As the unrolled copy: 'W' and '-' read as X.
                    auto* weak = builder_.CreateLoad(i64_,
                        dynamic_chunk_address(inst.x + inst.aux, chunk, 1U));
                    a = builder_.CreateOr(a, builder_.CreateAnd(weak, b));
                }
                builder_.CreateStore(a, dynamic_chunk_address(inst.d, chunk, 0U));
                builder_.CreateStore(b, dynamic_chunk_address(inst.d, chunk, 1U));
                return Carried {};
            });
        (void)word_loop(constant(inst.imm_a), Carried {},
            [&](llvm::Value* chunk, const Carried&) {
                builder_.CreateStore(constant(0U),
                    dynamic_chunk_address(inst.d + inst.aux, chunk, 0U));
                builder_.CreateStore(constant(0U),
                    dynamic_chunk_address(inst.d + inst.aux, chunk, 1U));
                return Carried {};
            });
    }

    /// KOp::wide_const in a word loop over a constant table of its words.
    void emit_wide_const_words(const KInst& inst)
    {
        std::vector<std::uint64_t> words;
        words.reserve(2U * std::size_t { inst.aux });
        for (std::uint32_t index = 0U; index < inst.aux; ++index) {
            const auto& word = body_.wide_constants[inst.imm_a + index];
            words.push_back(word.a);
            words.push_back(word.b);
        }
        auto* table = constant_table(words);
        (void)word_loop(constant(inst.aux), Carried {},
            [&](llvm::Value* chunk, const Carried&) {
                auto* first = builder_.CreateShl(chunk, constant(1U));
                for (std::uint64_t plane = 0U; plane < 2U; ++plane) {
                    builder_.CreateStore(
                        table_word(table, builder_.CreateAdd(first, constant(plane))),
                        dynamic_chunk_address(inst.d, chunk, plane));
                }
                return Carried {};
            });
    }

    /// emit_wide_load in a word loop.
    void emit_wide_load_words(const KInst& inst)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
            const auto plane = [&](const std::uint64_t index) {
                return builder_.CreateLoad(i64_,
                    slot_word_at(inst.x, index * inst.imm_a, chunk));
            };
            Logic9Chunk loaded;
            if (inst.sub == 0U) {
                auto* m = constant(mask_of(bits));
                loaded = { { builder_.CreateAnd(plane(0U), m), builder_.CreateAnd(plane(1U), m) },
                    constant(0U), nullptr };
            } else if (inst.sub == 1U && weak_mode()) {
                loaded = nine_chunk(plane(0U), plane(1U), plane(2U), plane(3U),
                    constant(mask_of(bits)));
            } else {
                loaded = logic9_chunk(plane(0U), plane(1U), plane(2U), plane(3U), bits,
                    inst.sub != 1U);
            }
            builder_.CreateStore(loaded.value.a, dynamic_chunk_address(inst.d, chunk, 0U));
            builder_.CreateStore(loaded.value.b, dynamic_chunk_address(inst.d, chunk, 1U));
            if (inst.sub == 1U || inst.aux != 0U) {
                builder_.CreateStore(loaded.unknown,
                    dynamic_chunk_address(inst.d + chunks, chunk, 0U));
                builder_.CreateStore(loaded.weak != nullptr ? loaded.weak : constant(0U),
                    dynamic_chunk_address(inst.d + chunks, chunk, 1U));
            }
        });
    }

    /// emit_wide_shared_store in a word loop.
    void emit_wide_shared_store_words(const KInst& inst)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        const auto words = inst.imm_b & 0xffffffffU;
        const auto planes = inst.imm_b >> 32U;
        each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
            const auto value = wide_chunk_value(inst.x, chunk, bits);
            auto* unknown = wide_chunk_unknown(inst, chunks, chunk, bits);
            auto* weak = wide_chunk_weak(inst, chunks, chunk, bits);
            std::vector<llvm::Value*> next;
            if (planes == 4U) {
                const auto codes = net_codes(value, unknown, bits, weak);
                next = { codes[0], codes[1], codes[2], codes[3] };
            } else {
                next = { weak != nullptr
                        ? builder_.CreateOr(value.a, builder_.CreateAnd(weak, value.b))
                        : value.a,
                    value.b };
            }
            emit_store_field_planes(inst.d, words, inst.offset, bits, next, chunk);
        });
    }

    /// emit_wide_slot_store in word loops.
    void emit_wide_slot_store_words(const KInst& inst, const std::uint32_t at)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        if (inst.sub == 0U) {
            if (inst.imm_b == 0U) {
                throw std::logic_error("static kernel wide store has no slot shape");
            }
            each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
                auto field = inst;
                field.imm_a = inst.imm_b;
                field.width = bits;
                auto value = wide_chunk_value(inst.x, chunk, bits);
                if (auto* weak = wide_chunk_weak(inst, chunks, chunk, bits)) {
                    // A Logic4 slot reads 'W' and '-' as X (coerce_weak).
                    value.a = builder_.CreateOr(value.a, builder_.CreateAnd(weak, value.b));
                }
                emit_store_wide_slot(field, value, chunk);
            });
            return;
        }
        if (inst.sub == 2U && (inst.flags & detail::flag_blocking) != 0U) {
            emit_wide_shared_store_words(inst);
            return;
        }
        emit_queued_words(inst.d, inst.width, inst.offset, 0U, nullptr, at,
            [&](llvm::Value* chunk, const std::uint32_t bits) {
                return QueuedChunk { 0U, bits, wide_chunk_value(inst.x, chunk, bits),
                    wide_chunk_unknown(inst, chunks, chunk, bits),
                    wide_chunk_weak(inst, chunks, chunk, bits) };
            },
            [] { }, [&] { (void)call_wide(at, zero_pair(), zero_pair(), zero_pair(), 1U); });
    }

    /// emit_wide_net_store in word loops.
    void emit_wide_net_store_words(const KInst& inst, const std::uint32_t at)
    {
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        if (inst.sub == 0U) {
            // emit_store_net_dynamic's shape: words per plane, the source
            // count and the member's source, the slot's width.
            auto field = inst;
            field.imm_a = inst.imm_a & 0xffffffffU;
            field.offset = static_cast<std::uint32_t>(inst.imm_a >> 32U);
            each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
                emit_store_net_dynamic(field, wide_chunk_value(inst.x, chunk, bits),
                    builder_.CreateTrunc(chunk_bit(inst.offset, chunk), i32_),
                    constant(mask_of(bits)), constant(bits));
            });
            return;
        }
        const auto shape = *detail::net_shape(inst);
        emit_queued_words(inst.d, inst.width, inst.offset, 4U, nullptr, at,
            [&](llvm::Value* chunk, const std::uint32_t bits) {
                return QueuedChunk { 0U, bits, wide_chunk_value(inst.x, chunk, bits),
                    wide_chunk_unknown(inst, chunks, chunk, bits),
                    wide_chunk_weak(inst, chunks, chunk, bits) };
            },
            [&] {
                // Each chunk resolves into the slot's next block (kind 4 writes).
                emit_net_begin_pending(inst, shape, (shape.width + 63U) / 64U);
                each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
                    emit_net_vhdl_field(inst, shape,
                        builder_.CreateTrunc(chunk_bit(inst.offset, chunk), i32_), 0U, bits,
                        net_codes(wide_chunk_value(inst.x, chunk, bits),
                            wide_chunk_unknown(inst, chunks, chunk, bits), bits,
                            wide_chunk_weak(inst, chunks, chunk, bits)));
                });
            },
            [&] { (void)call_wide(at, zero_pair(), zero_pair(), zero_pair(), 1U); });
    }

    /// emit_net_constant_store in word loops over a constant table of the
    /// chunks' codes.
    void emit_net_constant_store_words(const KInst& inst, const std::uint32_t at,
        llvm::Value* offset)
    {
        const auto constants = (inst.sub == 2U ? inst.imm_a : inst.imm_b) & 0xffffffffU;
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        std::vector<std::uint64_t> codes;
        codes.reserve(4U * std::size_t { chunks });
        for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
            const auto& low = body_.wide_constants[constants + 2U * chunk];
            const auto& high = body_.wide_constants[constants + 2U * chunk + 1U];
            codes.insert(codes.end(), { low.a, low.b, high.a, high.b });
        }
        auto* table = constant_table(codes);
        const auto shape = *detail::net_shape(inst);
        const auto first = offset != nullptr ? 0U : inst.offset;
        emit_queued_words(inst.d, inst.width, first, 3U, offset, at,
            [&](llvm::Value* chunk, const std::uint32_t bits) {
                return QueuedChunk { 0U, bits,
                    { builder_.CreateAdd(constant(constants),
                          builder_.CreateShl(chunk, constant(1U))),
                        constant(0U) },
                    constant(0U) };
            },
            [&] {
                // Each chunk's codes resolve into the slot's next block now.
                emit_net_begin_pending(inst, shape, (shape.width + 63U) / 64U);
                each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
                    auto* row = builder_.CreateShl(chunk, constant(2U));
                    std::array<llvm::Value*, 4> planes { };
                    for (std::uint64_t plane = 0U; plane < 4U; ++plane) {
                        planes[plane] = table_word(table,
                            builder_.CreateAdd(row, constant(plane)));
                    }
                    auto* bit = builder_.CreateTrunc(chunk_bit(first, chunk), i32_);
                    emit_net_vhdl_field(inst, shape,
                        offset != nullptr ? builder_.CreateAdd(offset, bit) : bit, 0U, bits,
                        planes);
                });
            },
            [&] {
                call_effect(at, zero_pair(),
                    inst.sub == 3U ? load_register(inst.y) : zero_pair(), zero_pair(), 1U);
            });
    }

    /// emit_wide_concat in word loops: the result's planes are assembled in
    /// scratch words, every operand read before d is written.
    void emit_wide_concat_words(const KInst& inst)
    {
        word_scratch_used_ = 0U;
        const auto chunks = static_cast<std::uint32_t>(detail::wide_chunks(inst.width));
        const std::uint32_t used = inst.sub != 1U ? 2U : weak_mode() ? 4U : 3U;
        const auto out = scratch_words(inst.width, { true, true, used > 2U, used > 3U });
        (void)each_word(inst.width, Carried {}, [&](llvm::Value* k, const Carried&) {
            for (std::uint32_t plane = 0U; plane < used; ++plane) {
                builder_.CreateStore(constant(0U), word_address(out, plane, k));
            }
            return Carried {};
        });
        // `bits` into word `word` (a value) of plane `plane`; a word past the
        // result takes nothing.
        const auto add = [&](const std::uint32_t plane, llvm::Value* word, llvm::Value* bits) {
            auto* inside = builder_.CreateICmpULT(word, constant(chunks));
            auto* address = word_address(out, plane,
                builder_.CreateSelect(inside, word, constant(chunks - 1U)));
            builder_.CreateStore(builder_.CreateOr(builder_.CreateLoad(i64_, address),
                                     builder_.CreateSelect(inside, bits, constant(0U))),
                address);
        };
        // A field of `width` bits at word `word` (a value), shifted by `shift`.
        const auto place = [&](llvm::Value* a, llvm::Value* b, llvm::Value* u,
                               llvm::Value* w, const std::uint32_t width, llvm::Value* word,
                               const std::uint32_t shift) {
            if (width == 0U) {
                return;
            }
            auto* m = constant(mask_of(width));
            llvm::Value* parts[4] { builder_.CreateAnd(a, m), builder_.CreateAnd(b, m),
                builder_.CreateAnd(u, m), builder_.CreateAnd(w, m) };
            for (std::uint32_t plane = 0U; plane < used; ++plane) {
                add(plane, word, builder_.CreateShl(parts[plane], constant(shift)));
                if (shift != 0U && shift + width > 64U) {
                    add(plane, builder_.CreateAdd(word, constant(1U)),
                        builder_.CreateLShr(parts[plane], constant(64U - shift)));
                }
            }
        };
        std::uint32_t offset = 0U;
        for (std::uint32_t index = inst.x; index-- > 0U;) {
            const auto& operand = body_.concat[inst.aux + index];
            const bool coerce = inst.sub != 1U && weak_mode();
            const auto coerced = [&](llvm::Value* a, llvm::Value* b,
                                     llvm::Value* weak) -> llvm::Value* {
                return coerce ? builder_.CreateOr(a, builder_.CreateAnd(weak, b)) : a;
            };
            if (offset / 64U >= chunks) {
                offset += operand.width;
                continue;
            }
            if (operand.width <= 64U) {
                const auto value = load_register(operand.reg);
                auto* weak = weak_of(operand.reg);
                place(coerced(value.a, value.b, weak), value.b, unknown_of(operand.reg), weak,
                    operand.width, constant(offset / 64U), offset % 64U);
            } else {
                const auto count = static_cast<std::uint32_t>(detail::wide_chunks(operand.width));
                const auto chunk_of = [&](llvm::Value* chunk, const std::uint32_t bits) {
                    auto* a = builder_.CreateLoad(i64_,
                        dynamic_chunk_address(operand.reg, chunk, 0U));
                    auto* b = builder_.CreateLoad(i64_,
                        dynamic_chunk_address(operand.reg, chunk, 1U));
                    auto* unknown = operand.unknown != 0U
                        ? builder_.CreateLoad(i64_,
                              dynamic_chunk_address(operand.reg + count, chunk, 0U))
                        : constant(0U);
                    auto* weak = (used == 4U || coerce) && operand.unknown != 0U
                        ? builder_.CreateLoad(i64_,
                              dynamic_chunk_address(operand.reg + count, chunk, 1U))
                        : constant(0U);
                    place(coerced(a, b, weak), b, unknown, weak, bits,
                        builder_.CreateAdd(constant(offset / 64U), chunk), offset % 64U);
                };
                if (loops_chunks(operand.width)) {
                    each_chunk(operand.width, chunk_of);
                } else {
                    for (std::uint32_t chunk = 0U; chunk < count; ++chunk) {
                        chunk_of(constant(chunk), std::min(64U, operand.width - 64U * chunk));
                    }
                }
            }
            offset += operand.width;
        }
        (void)each_word(inst.width, Carried {}, [&](llvm::Value* k, const Carried&) {
            const auto plane = [&](const std::uint32_t index) -> llvm::Value* {
                return index < used ? builder_.CreateLoad(i64_, word_address(out, index, k))
                                    : constant(0U);
            };
            builder_.CreateStore(plane(0U), dynamic_chunk_address(inst.d, k, 0U));
            builder_.CreateStore(plane(1U), dynamic_chunk_address(inst.d, k, 1U));
            if (inst.sub == 1U) {
                builder_.CreateStore(plane(2U), dynamic_chunk_address(inst.d + chunks, k, 0U));
                builder_.CreateStore(plane(3U), dynamic_chunk_address(inst.d + chunks, k, 1U));
            }
            return Carried {};
        });
    }

    /// emit_wide_move in word loops. An immediate move stages the value in
    /// scratch words before the first store, as the unrolled move reads it
    /// whole; a nonblocking one queues each chunk.
    void emit_wide_move_words(const KInst& inst, const std::uint32_t at)
    {
        word_scratch_used_ = 0U;
        const auto source_words = inst.imm_a;
        const auto target_words = inst.imm_b & 0xffffffffU;
        const auto source_width = static_cast<std::int64_t>(inst.imm_b >> 32U);
        const bool two_state = (inst.flags & detail::flag_two_state) != 0U;
        const auto signed_constant = [&](const std::int64_t value) {
            return constant(static_cast<std::uint64_t>(value));
        };
        const auto smax = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(left, right), left, right);
        };
        const auto smin = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(left, right), left, right);
        };
        llvm::Value* known { };
        llvm::Value* first { };
        llvm::Value* low { };
        llvm::Value* high { };
        if (inst.sub == 1U) {
            const auto base = load_register(inst.y);
            const auto& part = body_.parts[inst.aux];
            const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
            const bool descending = (inst.flags & detail::flag_descending) != 0U;
            const auto lower = std::min(part.left, part.right);
            const auto upper = std::max(part.left, part.right);
            const auto edge = static_cast<std::int64_t>(inst.width) - 1;
            known = builder_.CreateICmpEQ(
                builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
            auto* selected_right = builder_.CreateAdd(signed_index(base),
                signed_constant(increasing ? (descending ? 0 : edge)
                                           : (descending ? -edge : 0)));
            // Result bit i is source bit first + i, for the bits low .. high
            // that select an index of the source.
            first = builder_.CreateAdd(
                signed_constant(static_cast<std::int64_t>(part.base_offset)),
                descending ? builder_.CreateSub(selected_right, signed_constant(part.right))
                           : builder_.CreateSub(signed_constant(part.right), selected_right));
            low = descending ? builder_.CreateSub(signed_constant(lower), selected_right)
                             : builder_.CreateSub(selected_right, signed_constant(upper));
            high = descending ? builder_.CreateSub(signed_constant(upper), selected_right)
                              : builder_.CreateSub(selected_right, signed_constant(lower));
            low = smax(smax(low, constant(0U)), builder_.CreateSub(constant(0U), first));
            high = smin(smin(high, signed_constant(edge)),
                builder_.CreateSub(signed_constant(source_width - 1), first));
        }
        const auto moved = [&](llvm::Value* chunk, const std::uint32_t bits) -> Pair {
            if (inst.sub == 0U) {
                auto* m = constant(mask_of(bits));
                return { builder_.CreateAnd(
                             builder_.CreateLoad(i64_, slot_word_at(inst.x, 0U, chunk)), m),
                    builder_.CreateAnd(
                        builder_.CreateLoad(i64_, slot_word_at(inst.x, source_words, chunk)),
                        m) };
            }
            auto* chunk_low = builder_.CreateShl(chunk, constant(6U));
            // This chunk's share of the selected bits.
            auto* from = smax(low, chunk_low);
            auto* to = smin(high, builder_.CreateAdd(chunk_low, constant(bits - 1U)));
            auto* any = builder_.CreateAnd(known, builder_.CreateICmpSLE(from, to));
            auto* count = builder_.CreateSelect(any,
                builder_.CreateAdd(builder_.CreateSub(to, from), constant(1U)), constant(1U));
            auto* shift_out = builder_.CreateSelect(any, builder_.CreateSub(from, chunk_low),
                constant(0U));
            auto* offset = builder_.CreateSelect(any, builder_.CreateAdd(first, from),
                constant(0U));
            const auto field = dynamic_slot_field(inst.x, source_words, offset, count);
            auto* count_mask = builder_.CreateLShr(constant(mask_of(64U)),
                builder_.CreateSub(constant(64U), count));
            auto* keep = builder_.CreateNot(builder_.CreateShl(count_mask, shift_out));
            auto* fill = two_state ? constant(0U) : constant(mask_of(bits));
            const auto placed = [&](llvm::Value* plane) {
                return builder_.CreateSelect(any,
                    builder_.CreateOr(builder_.CreateAnd(fill, keep),
                        builder_.CreateShl(plane, shift_out)),
                    fill);
            };
            return { placed(field.a), placed(field.b) };
        };
        if ((inst.flags & detail::flag_nba) != 0U) {
            emit_queued_words(inst.d, inst.width, inst.offset, 0U, nullptr, at,
                [&](llvm::Value* chunk, const std::uint32_t bits) {
                    return QueuedChunk { 0U, bits, moved(chunk, bits), constant(0U) };
                },
                [] { },
                [&] {
                    call_effect(at, zero_pair(),
                        inst.sub == 1U ? load_register(inst.y) : zero_pair(), zero_pair(), 1U);
                });
            return;
        }
        const auto staged = scratch_words(inst.width, { true, true, false, false });
        each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
            const auto value = moved(chunk, bits);
            builder_.CreateStore(value.a, word_address(staged, 0U, chunk));
            builder_.CreateStore(value.b, word_address(staged, 1U, chunk));
        });
        each_chunk(inst.width, [&](llvm::Value* chunk, const std::uint32_t bits) {
            auto field = inst;
            field.imm_a = target_words;
            field.width = bits;
            emit_store_wide_slot(field,
                { builder_.CreateLoad(i64_, word_address(staged, 0U, chunk)),
                    builder_.CreateLoad(i64_, word_address(staged, 1U, chunk)) },
                chunk);
        });
    }

    /// Where a word loop writes the wide d: its registers, or scratch that
    /// commit_words copies there when d overlaps a source the loop reads
    /// after writing. An elementwise loop reads word k before it writes word
    /// k, so the same register of the same chunk count is no overlap there.
    [[nodiscard]] WordPlanes result_words(const KInst& inst,
        const std::vector<const WordPlanes*>& sources, const bool elementwise)
    {
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        auto out = register_words(inst.d, inst.width, masks, masks);
        for (const auto* source : sources) {
            if (source->base != registers_) {
                continue;
            }
            const bool overlap = source->first < out.first + out.span
                && out.first < source->first + source->span;
            const bool aligned = elementwise && source->first == out.first
                && detail::wide_chunks(source->bits) == detail::wide_chunks(out.bits);
            if (overlap && !aligned) {
                return scratch_words(inst.width, out.present);
            }
        }
        return out;
    }

    void commit_words(const KInst& inst, const WordPlanes& out)
    {
        if (out.base == registers_) {
            return;
        }
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto target = register_words(inst.d, inst.width, masks, masks);
        each_word(inst.width, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
                write_word(target, plane, k, read_word(out, plane, k));
            }
            return {};
        });
    }

    /// Word k of a result, as store_wide_result stores it.
    void write_result_word(const WordPlanes& out, const bool masks, llvm::Value* k,
        WideVal value)
    {
        if (!masks && value.w != nullptr) {
            value.a = builder_.CreateOr(value.a, builder_.CreateAnd(value.w, value.b));
        }
        write_word(out, 0U, k, value.a);
        write_word(out, 1U, k, value.b);
        if (masks) {
            write_word(out, 2U, k, value.u != nullptr ? value.u : constant(0U));
            write_word(out, 3U, k, value.w != nullptr ? value.w : constant(0U));
        }
    }

    /// An arithmetic result (plane a of `value`): unknown operand bits make
    /// it all X.
    void write_arithmetic_words(const KInst& inst, const WordPlanes& value, llvm::Value* bad)
    {
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto out = register_words(inst.d, inst.width, masks, masks);
        auto* ones = constant(~std::uint64_t { 0 });
        each_word(inst.width, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            WideVal result;
            result.a = builder_.CreateSelect(bad, ones, read_word(value, 0U, k));
            result.b = builder_.CreateSelect(bad, ones, constant(0U));
            write_result_word(out, masks, k, result);
            return {};
        });
    }

    /// Whether any word of plane `plane` of either value is nonzero.
    [[nodiscard]] llvm::Value* any_word(const WordPlanes& left, const WordPlanes* right,
        const std::uint32_t plane)
    {
        return each_word(left.bits, { builder_.getFalse() },
            [&](llvm::Value* k, const Carried& seen) -> Carried {
                llvm::Value* word = read_word(left, plane, k);
                if (right != nullptr) {
                    word = builder_.CreateOr(word, read_word(*right, plane, k));
                }
                return { builder_.CreateOr(seen[0], nonzero(word)) };
            })[0];
    }

    /// The sign (top bit) of plane a.
    [[nodiscard]] llvm::Value* word_sign(const WordPlanes& words)
    {
        const auto top = detail::wide_chunks(words.bits) - 1U;
        return builder_.CreateTrunc(
            builder_.CreateLShr(read_word(words, 0U, constant(top)),
                constant((words.bits - 1U) % 64U)),
            builder_.getInt1Ty());
    }

    /// target = negate ? -source : source (plane a; may be in place).
    void words_negate(const WordPlanes& target, const WordPlanes& source, llvm::Value* negate)
    {
        each_word(source.bits, { builder_.getTrue() },
            [&](llvm::Value* k, const Carried& carry) -> Carried {
                auto* word = read_word(source, 0U, k);
                auto* negated = builder_.CreateAdd(bit_not(word),
                    builder_.CreateZExt(carry[0], i64_));
                write_word(target, 0U, k, builder_.CreateSelect(negate, negated, word));
                return { builder_.CreateAnd(carry[0],
                    builder_.CreateICmpEQ(word, constant(0U))) };
            });
    }

    /// target = when ? left + right : left (plane a; may be in place) or,
    /// subtracting, left - right.
    void words_add(const WordPlanes& target, const WordPlanes& left, const WordPlanes& right,
        const bool subtract, llvm::Value* when)
    {
        each_word(left.bits, { builder_.getFalse() },
            [&](llvm::Value* k, const Carried& carry) -> Carried {
                auto* x = read_word(left, 0U, k);
                auto* y = read_word(right, 0U, k);
                auto* in = builder_.CreateZExt(carry[0], i64_);
                llvm::Value* value { };
                llvm::Value* out { };
                if (subtract) {
                    auto* difference = builder_.CreateSub(x, y);
                    value = builder_.CreateSub(difference, in);
                    out = builder_.CreateOr(builder_.CreateICmpULT(x, y),
                        builder_.CreateICmpULT(difference, in));
                } else {
                    auto* sum = builder_.CreateAdd(x, y);
                    value = builder_.CreateAdd(sum, in);
                    out = builder_.CreateOr(builder_.CreateICmpULT(sum, x),
                        builder_.CreateICmpULT(value, sum));
                }
                write_word(target, 0U, k,
                    when == nullptr ? value : builder_.CreateSelect(when, value, x));
                return { out };
            });
    }

    void words_copy(const WordPlanes& target, const WordPlanes& source)
    {
        each_word(source.bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            write_word(target, 0U, k, read_word(source, 0U, k));
            return {};
        });
    }

    /// product = x * y modulo 2^bits over the words (product apart from
    /// both factors).
    void words_multiply(const WordPlanes& product, const WordPlanes& x, const WordPlanes& y)
    {
        auto* count = constant(detail::wide_chunks(product.bits));
        auto* wide = builder_.getInt128Ty();
        each_word(product.bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            write_word(product, 0U, k, constant(0U));
            return {};
        });
        word_loop(count, {}, [&](llvm::Value* row, const Carried&) -> Carried {
            auto* factor = builder_.CreateZExt(read_word(x, 0U, row), wide);
            word_loop(builder_.CreateSub(count, row), { constant(0U) },
                [&](llvm::Value* column, const Carried& carry) -> Carried {
                    auto* at = builder_.CreateAdd(row, column);
                    auto* sum = builder_.CreateAdd(
                        builder_.CreateAdd(
                            builder_.CreateMul(factor,
                                builder_.CreateZExt(read_word(y, 0U, column), wide)),
                            builder_.CreateZExt(read_word(product, 0U, at), wide)),
                        builder_.CreateZExt(carry[0], wide));
                    write_word(product, 0U, at, builder_.CreateTrunc(sum, i64_));
                    return { builder_.CreateTrunc(
                        builder_.CreateLShr(sum, llvm::ConstantInt::get(wide, 64U)), i64_) };
                });
            return {};
        });
    }

    /// Restoring division of the `bits`-bit dividend by the divisor a bit at
    /// a time, as emit_udivrem: quotient and remainder (scratch). A zero
    /// divisor gives garbage the caller replaces.
    void words_divide(const WordPlanes& quotient, const WordPlanes& remainder,
        const WordPlanes& dividend, const WordPlanes& divisor)
    {
        const auto bits = dividend.bits;
        auto* count = constant(detail::wide_chunks(bits));
        each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            write_word(quotient, 0U, k, constant(0U));
            write_word(remainder, 0U, k, constant(0U));
            return {};
        });
        word_loop(constant(bits), {}, [&](llvm::Value* step, const Carried&) -> Carried {
            auto* bit_index = builder_.CreateSub(constant(bits - 1U), step);
            auto* word_index = builder_.CreateLShr(bit_index, constant(6U));
            auto* shift = builder_.CreateAnd(bit_index, constant(63U));
            auto* bit = builder_.CreateAnd(
                builder_.CreateLShr(read_word(dividend, 0U, word_index), shift), constant(1U));
            // remainder = remainder << 1 | bit, within the width.
            word_loop(count, { bit }, [&](llvm::Value* k, const Carried& carry) -> Carried {
                auto* word = read_word(remainder, 0U, k);
                write_word(remainder, 0U, k,
                    builder_.CreateOr(builder_.CreateShl(word, constant(1U)), carry[0]));
                return { builder_.CreateLShr(word, constant(63U)) };
            });
            // remainder >= divisor, from the top word down.
            const auto order = word_loop(count, { builder_.getFalse(), builder_.getFalse() },
                [&](llvm::Value* j, const Carried& state) -> Carried {
                    auto* k = builder_.CreateSub(builder_.CreateSub(count, constant(1U)), j);
                    auto* r = read_word(remainder, 0U, k);
                    auto* d = read_word(divisor, 0U, k);
                    auto* differ = builder_.CreateICmpNE(r, d);
                    auto* less = builder_.CreateSelect(state[0], state[1],
                        builder_.CreateAnd(differ, builder_.CreateICmpULT(r, d)));
                    return { builder_.CreateOr(state[0], differ), less };
                });
            auto* fits = builder_.CreateNot(order[1]);
            words_add(remainder, remainder, divisor, true, fits);
            auto* word = read_word(quotient, 0U, word_index);
            write_word(quotient, 0U, word_index,
                builder_.CreateOr(word,
                    builder_.CreateShl(builder_.CreateZExt(fits, i64_), shift)));
            return {};
        });
    }

    /// The truth of a wide value (wide_truth): (is one, is unknown) as i1.
    [[nodiscard]] std::pair<llvm::Value*, llvm::Value*> words_truth(const WordPlanes& words)
    {
        const auto seen = each_word(words.bits, { builder_.getFalse(), builder_.getFalse() },
            [&](llvm::Value* k, const Carried& state) -> Carried {
                auto* a = read_word(words, 0U, k);
                auto* b = read_word(words, 1U, k);
                return { builder_.CreateOr(state[0], nonzero(builder_.CreateAnd(a, bit_not(b)))),
                    builder_.CreateOr(state[1], nonzero(b)) };
            });
        return { seen[0], builder_.CreateAnd(builder_.CreateNot(seen[0]), seen[1]) };
    }

    /// emit_wide_binary over words.
    void emit_words_binary(const KInst& inst)
    {
        using runtime::simir::BinaryOperator;
        const auto operation = static_cast<BinaryOperator>(inst.sub);
        const auto bits = inst.width;
        const auto left = operand_words(inst.x, bits, wide_has(inst, detail::wide_alu_masks, 1U));
        const auto right = operand_words(inst.y, bits, wide_has(inst, detail::wide_alu_masks, 2U));
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        auto* ones = constant(~std::uint64_t { 0 });
        const auto either = [&](llvm::Value* x, llvm::Value* y) -> llvm::Value* {
            return x == nullptr ? y : y == nullptr ? x : builder_.CreateOr(x, y);
        };
        const auto elementwise = [&](const auto& result_of) {
            const auto out = result_words(inst, { &left, &right }, true);
            each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
                write_result_word(out, masks, k, result_of(read_words(left, k), read_words(right, k)));
                return {};
            });
            commit_words(inst, out);
        };
        // Accumulations over both operands' words: one i1 per entry of
        // `initial`, each step OR-ed in.
        const auto accumulate = [&](const std::size_t count, const auto& step) {
            return each_word(bits, Carried(count, builder_.getFalse()),
                [&](llvm::Value* k, const Carried& seen) -> Carried {
                    auto flags = step(read_words(left, k), read_words(right, k));
                    Carried next;
                    for (std::size_t slot = 0U; slot < count; ++slot) {
                        next.push_back(builder_.CreateOr(seen[slot], flags[slot]));
                    }
                    return next;
                });
        };
        const auto flag = [&](llvm::Value* condition, const bool when_true) {
            return scalar_bool(when_true ? condition : builder_.CreateNot(condition));
        };
        switch (operation) {
        case BinaryOperator::bit_and:
        case BinaryOperator::bit_or: {
            const bool conjunction = operation == BinaryOperator::bit_and;
            elementwise([&](const WideVal& l, const WideVal& r) {
                auto* zero_l = builder_.CreateAnd(bit_not(l.a), bit_not(l.b));
                auto* zero_r = builder_.CreateAnd(bit_not(r.a), bit_not(r.b));
                auto* one_l = builder_.CreateAnd(l.a, bit_not(l.b));
                auto* one_r = builder_.CreateAnd(r.a, bit_not(r.b));
                auto* decided = conjunction ? builder_.CreateOr(zero_l, zero_r)
                                            : builder_.CreateOr(one_l, one_r);
                auto* settled = conjunction ? builder_.CreateAnd(one_l, one_r)
                                            : builder_.CreateAnd(zero_l, zero_r);
                auto* x = bit_not(builder_.CreateOr(decided, settled));
                WideVal result;
                result.a = builder_.CreateOr(conjunction ? settled : decided, x);
                result.b = x;
                if (masks) {
                    auto* pending = either(l.u, r.u);
                    result.u = pending == nullptr ? nullptr
                                                  : builder_.CreateAnd(pending, bit_not(decided));
                }
                return result;
            });
            return;
        }
        case BinaryOperator::bit_xor:
            elementwise([&](const WideVal& l, const WideVal& r) {
                auto* unknown = builder_.CreateOr(l.b, r.b);
                WideVal result;
                result.a = builder_.CreateOr(builder_.CreateXor(l.a, r.a), unknown);
                result.b = unknown;
                result.u = either(l.u, r.u);
                return result;
            });
            return;
        case BinaryOperator::equal:
        case BinaryOperator::not_equal: {
            const auto seen = accumulate(2U, [&](const WideVal& l, const WideVal& r) -> Carried {
                auto* unknown = builder_.CreateOr(l.b, r.b);
                return { nonzero(builder_.CreateAnd(builder_.CreateXor(l.a, r.a), bit_not(unknown))),
                    nonzero(unknown) };
            });
            const bool inequality = operation == BinaryOperator::not_equal;
            store_register(inst.d,
                select(seen[0], flag(builder_.getTrue(), inequality),
                    select(seen[1], x_value(1U), flag(builder_.getTrue(), !inequality))));
            return;
        }
        case BinaryOperator::case_equal: {
            const auto seen = accumulate(1U, [&](const WideVal& l, const WideVal& r) -> Carried {
                const auto same = [&](llvm::Value* x, llvm::Value* y) -> llvm::Value* {
                    return builder_.CreateXor(x != nullptr ? x : constant(0U),
                        y != nullptr ? y : constant(0U));
                };
                return { nonzero(builder_.CreateOr(
                    builder_.CreateOr(same(l.a, r.a), same(l.b, r.b)),
                    builder_.CreateOr(same(l.u, r.u), same(l.w, r.w)))) };
            });
            store_register(inst.d, scalar_bool(builder_.CreateNot(seen[0])));
            return;
        }
        case BinaryOperator::casez_equal:
        case BinaryOperator::casex_equal: {
            const bool z_only = operation == BinaryOperator::casez_equal;
            const auto seen = accumulate(1U, [&](const WideVal& l, const WideVal& r) -> Carried {
                auto* ignored = z_only
                    ? builder_.CreateOr(builder_.CreateAnd(bit_not(l.a), l.b),
                          builder_.CreateAnd(bit_not(r.a), r.b))
                    : builder_.CreateOr(l.b, r.b);
                return { nonzero(builder_.CreateAnd(
                    builder_.CreateOr(builder_.CreateXor(l.a, r.a), builder_.CreateXor(l.b, r.b)),
                    bit_not(ignored))) };
            });
            store_register(inst.d, scalar_bool(builder_.CreateNot(seen[0])));
            return;
        }
        case BinaryOperator::wildcard_equal: {
            const auto seen = accumulate(3U, [&](const WideVal& l, const WideVal& r) -> Carried {
                auto* care = bit_not(r.b);
                auto* differ = builder_.CreateAnd(builder_.CreateXor(l.a, r.a), care);
                return { nonzero(builder_.CreateAnd(l.b, care)),
                    nonzero(builder_.CreateAnd(differ, bit_not(l.b))), nonzero(differ) };
            });
            store_register(inst.d,
                select(seen[1], scalar_bool(builder_.getFalse()),
                    select(seen[0], x_value(1U), scalar_bool(builder_.CreateNot(seen[2])))));
            return;
        }
        case BinaryOperator::less_unsigned:
        case BinaryOperator::less_equal_unsigned:
        case BinaryOperator::greater_unsigned:
        case BinaryOperator::greater_equal_unsigned:
        case BinaryOperator::less_signed:
        case BinaryOperator::less_equal_signed:
        case BinaryOperator::greater_signed:
        case BinaryOperator::greater_equal_signed: {
            const bool is_signed = operation == BinaryOperator::less_signed
                || operation == BinaryOperator::less_equal_signed
                || operation == BinaryOperator::greater_signed
                || operation == BinaryOperator::greater_equal_signed;
            const auto chunks = detail::wide_chunks(bits);
            const auto rest = bits % 64U;
            // The top word of a signed operand, sign-extended with its sign
            // flipped, orders as unsigned.
            const auto key = [&](llvm::Value* word, llvm::Value* k) -> llvm::Value* {
                if (!is_signed) {
                    return word;
                }
                llvm::Value* extended = word;
                if (rest != 0U) {
                    extended = builder_.CreateAShr(builder_.CreateShl(word, constant(64U - rest)),
                        constant(64U - rest));
                }
                return builder_.CreateSelect(builder_.CreateICmpEQ(k, constant(chunks - 1U)),
                    builder_.CreateXor(extended, constant(std::uint64_t { 1 } << 63U)), word);
            };
            // From the top word down: (decided, less, unknown).
            const auto order = each_word(bits,
                { builder_.getFalse(), builder_.getFalse(), builder_.getFalse() },
                [&](llvm::Value* j, const Carried& state) -> Carried {
                    auto* k = builder_.CreateSub(constant(chunks - 1U), j);
                    auto* l = key(read_word(left, 0U, k), k);
                    auto* r = key(read_word(right, 0U, k), k);
                    auto* differ = builder_.CreateICmpNE(l, r);
                    auto* less = builder_.CreateSelect(state[0], state[1],
                        builder_.CreateAnd(differ, builder_.CreateICmpULT(l, r)));
                    auto* unknown = nonzero(builder_.CreateOr(read_word(left, 1U, k),
                        read_word(right, 1U, k)));
                    return { builder_.CreateOr(state[0], differ), less,
                        builder_.CreateOr(state[2], unknown) };
                });
            auto* less = order[1];
            auto* equal = builder_.CreateNot(order[0]);
            llvm::Value* relation { };
            switch (operation) {
            case BinaryOperator::less_unsigned:
            case BinaryOperator::less_signed:
                relation = less;
                break;
            case BinaryOperator::less_equal_unsigned:
            case BinaryOperator::less_equal_signed:
                relation = builder_.CreateOr(less, equal);
                break;
            case BinaryOperator::greater_unsigned:
            case BinaryOperator::greater_signed:
                relation = builder_.CreateNot(builder_.CreateOr(less, equal));
                break;
            default:
                relation = builder_.CreateNot(less);
                break;
            }
            store_register(inst.d, select(order[2], x_value(1U), scalar_bool(relation)));
            return;
        }
        case BinaryOperator::add_unsigned:
        case BinaryOperator::add_signed:
        case BinaryOperator::subtract_unsigned:
        case BinaryOperator::subtract_signed: {
            auto* bad = any_word(left, &right, 1U);
            const auto value = scratch_plane(bits);
            words_add(value, left, right,
                operation == BinaryOperator::subtract_unsigned
                    || operation == BinaryOperator::subtract_signed,
                nullptr);
            write_arithmetic_words(inst, value, bad);
            return;
        }
        case BinaryOperator::multiply_unsigned:
        case BinaryOperator::multiply_signed: {
            auto* bad = any_word(left, &right, 1U);
            const auto value = scratch_plane(bits);
            words_multiply(value, left, right);
            write_arithmetic_words(inst, value, bad);
            return;
        }
        case BinaryOperator::power_unsigned:
        case BinaryOperator::power_signed: {
            auto* unknown = any_word(left, &right, 1U);
            // By squaring over the exponent's bits (emit_power).
            const auto result = scratch_plane(bits);
            const auto factor = scratch_plane(bits);
            const auto product = scratch_plane(bits);
            each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
                write_word(result, 0U, k,
                    builder_.CreateZExt(builder_.CreateICmpEQ(k, constant(0U)), i64_));
                return {};
            });
            words_copy(factor, left);
            word_loop(constant(bits), {}, [&](llvm::Value* index, const Carried&) -> Carried {
                auto* bit = builder_.CreateTrunc(
                    builder_.CreateLShr(
                        read_word(right, 0U, builder_.CreateLShr(index, constant(6U))),
                        builder_.CreateAnd(index, constant(63U))),
                    builder_.getInt1Ty());
                words_multiply(product, result, factor);
                each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
                    write_word(result, 0U, k, builder_.CreateSelect(bit,
                        read_word(product, 0U, k), read_word(result, 0U, k)));
                    return {};
                });
                words_multiply(product, factor, factor);
                words_copy(factor, product);
                return {};
            });
            if (operation == BinaryOperator::power_unsigned) {
                write_arithmetic_words(inst, result, unknown);
                return;
            }
            // A negative exponent (power_known): 0 ** -n is X, 1 ** -n is 1,
            // (-1) ** -n is -1 for odd n else 1, anything else 0.
            auto* negative = word_sign(right);
            const auto base = each_word(bits,
                { builder_.getFalse(), builder_.getFalse(), builder_.getFalse() },
                [&](llvm::Value* k, const Carried& state) -> Carried {
                    auto* word = read_word(left, 0U, k);
                    auto* unit = builder_.CreateZExt(builder_.CreateICmpEQ(k, constant(0U)), i64_);
                    return { builder_.CreateOr(state[0], nonzero(word)),
                        builder_.CreateOr(state[1], builder_.CreateICmpNE(word, unit)),
                        builder_.CreateOr(state[2],
                            builder_.CreateICmpNE(word, valid_bits(bits, k))) };
                });
            auto* base_zero = builder_.CreateNot(base[0]);
            auto* base_one = builder_.CreateNot(base[1]);
            auto* base_ones = builder_.CreateNot(base[2]);
            auto* odd = builder_.CreateTrunc(read_word(right, 0U, constant(0U)),
                builder_.getInt1Ty());
            each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
                auto* unit = builder_.CreateZExt(builder_.CreateICmpEQ(k, constant(0U)), i64_);
                auto* reciprocal = builder_.CreateSelect(base_one, unit,
                    builder_.CreateSelect(base_ones,
                        builder_.CreateSelect(odd, read_word(left, 0U, k), unit), constant(0U)));
                write_word(result, 0U, k,
                    builder_.CreateSelect(negative, reciprocal, read_word(result, 0U, k)));
                return {};
            });
            write_arithmetic_words(inst, result,
                builder_.CreateOr(unknown, builder_.CreateAnd(negative, base_zero)));
            return;
        }
        case BinaryOperator::divide_unsigned:
        case BinaryOperator::modulo_unsigned: {
            auto* bad = builder_.CreateOr(any_word(left, &right, 1U),
                builder_.CreateNot(any_word(right, nullptr, 0U)));
            const auto quotient = scratch_plane(bits);
            const auto remainder = scratch_plane(bits);
            words_divide(quotient, remainder, left, right);
            write_arithmetic_words(inst,
                operation == BinaryOperator::divide_unsigned ? quotient : remainder, bad);
            return;
        }
        case BinaryOperator::divide_signed:
        case BinaryOperator::remainder_signed:
        case BinaryOperator::modulo_signed: {
            // Magnitudes divide; the quotient is negative for operands of
            // different signs, the remainder takes the dividend's sign, and
            // modulo adds the divisor to a nonzero remainder of different
            // signs (divide_known_signed).
            auto* bad = builder_.CreateOr(any_word(left, &right, 1U),
                builder_.CreateNot(any_word(right, nullptr, 0U)));
            auto* left_negative = word_sign(left);
            auto* right_negative = word_sign(right);
            const auto left_magnitude = scratch_plane(bits);
            const auto right_magnitude = scratch_plane(bits);
            words_negate(left_magnitude, left, left_negative);
            words_negate(right_magnitude, right, right_negative);
            const auto quotient = scratch_plane(bits);
            const auto remainder = scratch_plane(bits);
            words_divide(quotient, remainder, left_magnitude, right_magnitude);
            auto* differ = builder_.CreateXor(left_negative, right_negative);
            if (operation == BinaryOperator::divide_signed) {
                words_negate(quotient, quotient, differ);
                write_arithmetic_words(inst, quotient, bad);
                return;
            }
            words_negate(remainder, remainder, left_negative);
            if (operation == BinaryOperator::modulo_signed) {
                auto* nonzero_remainder = any_word(remainder, nullptr, 0U);
                words_add(remainder, remainder, right, false,
                    builder_.CreateAnd(nonzero_remainder, differ));
            }
            write_arithmetic_words(inst, remainder, bad);
            return;
        }
        case BinaryOperator::vhdl_match_equal: {
            // As emit_wide_binary's, word by word: (mismatch, uninitialized,
            // meta).
            const auto seen = accumulate(3U, [&](const WideVal& l, const WideVal& r) -> Carried {
                const auto dont_care = [&](const WideVal& value) -> llvm::Value* {
                    return value.w == nullptr
                        ? nullptr
                        : builder_.CreateAnd(builder_.CreateAnd(value.w, bit_not(value.a)),
                              value.b);
                };
                auto* dont_care_left = dont_care(l);
                auto* dont_care_right = dont_care(r);
                llvm::Value* active = ones;
                if (dont_care_left != nullptr || dont_care_right != nullptr) {
                    active = bit_not(either(dont_care_left, dont_care_right));
                }
                auto* unknown = builder_.CreateOr(l.b, r.b);
                auto* mismatch = nonzero(builder_.CreateAnd(
                    builder_.CreateAnd(builder_.CreateXor(l.a, r.a), bit_not(unknown)), active));
                llvm::Value* uninitialized = builder_.getFalse();
                llvm::Value* meta = builder_.CreateAnd(unknown, active);
                if (auto* either_u = either(l.u, r.u)) {
                    uninitialized = nonzero(builder_.CreateAnd(either_u, active));
                    meta = builder_.CreateAnd(meta, bit_not(either_u));
                }
                return { mismatch, uninitialized, nonzero(meta) };
            });
            auto* mismatch = seen[0];
            auto* indeterminate = builder_.CreateOr(seen[1], seen[2]);
            store_register(inst.d,
                select(mismatch, Pair { constant(0U), constant(0U) },
                    select(indeterminate, x_value(1U), Pair { constant(1U), constant(0U) })));
            last_unknown_ = builder_.CreateZExt(
                builder_.CreateAnd(seen[1], builder_.CreateNot(mismatch)), i64_);
            return;
        }
        }
    }

    /// emit_wide_shift over words.
    void emit_words_shift(const KInst& inst)
    {
        using runtime::simir::ShiftOperator;
        const auto operation = static_cast<ShiftOperator>(inst.sub);
        const auto bits = inst.width;
        const auto amount_width = static_cast<std::uint32_t>(inst.imm_a);
        const bool amount_wide = wide_has(inst, detail::wide_alu_wide, 2U);
        const bool is_signed = (inst.flags & detail::flag_signed) != 0U;
        const bool rotate = operation == ShiftOperator::rotate_left
            || operation == ShiftOperator::rotate_right;
        const bool forward_left = operation == ShiftOperator::logical_left
            || operation == ShiftOperator::arithmetic_left
            || operation == ShiftOperator::rotate_left;
        const bool arithmetic = operation == ShiftOperator::arithmetic_left
            || operation == ShiftOperator::arithmetic_right;
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto value = operand_words(inst.x, bits, wide_has(inst, detail::wide_alu_masks, 1U));
        // The amount: unknown, negative (a signed amount reverses the
        // operator and shifts by its magnitude), the magnitude's low word,
        // whether the magnitude reaches the width, and (rotations) the
        // magnitude modulo the width.
        llvm::Value* unknown { };
        llvm::Value* negative = builder_.getFalse();
        llvm::Value* magnitude { };
        llvm::Value* saturated { };
        llvm::Value* residue { };
        if (!amount_wide) {
            const auto narrow = load_register(inst.y);
            auto* m = constant(mask_of(amount_width));
            auto* amount_a = builder_.CreateAnd(narrow.a, m);
            unknown = nonzero(builder_.CreateAnd(narrow.b, m));
            magnitude = amount_a;
            if (is_signed) {
                negative = builder_.CreateTrunc(
                    builder_.CreateLShr(amount_a, constant(amount_width - 1U)),
                    builder_.getInt1Ty());
                magnitude = builder_.CreateSelect(negative,
                    builder_.CreateAnd(builder_.CreateSub(constant(0U), amount_a), m), amount_a);
            }
            saturated = builder_.CreateICmpUGE(magnitude, constant(bits));
            residue = builder_.CreateURem(magnitude, constant(bits));
        } else {
            const auto amount = operand_words(inst.y, amount_width, false);
            if (is_signed) {
                negative = word_sign(amount);
            }
            const auto seen = each_word(amount_width,
                { builder_.getFalse(), builder_.getTrue(), builder_.getFalse(), constant(0U) },
                [&](llvm::Value* k, const Carried& state) -> Carried {
                    auto* a = read_word(amount, 0U, k);
                    auto* b = read_word(amount, 1U, k);
                    auto* negated = builder_.CreateAdd(bit_not(a),
                        builder_.CreateZExt(state[1], i64_));
                    auto* word = builder_.CreateSelect(negative, negated, a);
                    word = builder_.CreateAnd(word, valid_bits(amount_width, k));
                    auto* first = builder_.CreateICmpEQ(k, constant(0U));
                    return { builder_.CreateOr(state[0], nonzero(b)),
                        builder_.CreateAnd(state[1], builder_.CreateICmpEQ(a, constant(0U))),
                        builder_.CreateOr(state[2],
                            builder_.CreateAnd(builder_.CreateNot(first), nonzero(word))),
                        builder_.CreateSelect(first, word, state[3]) };
                });
            unknown = seen[0];
            magnitude = seen[3];
            saturated = builder_.CreateOr(seen[2], builder_.CreateICmpUGE(magnitude, constant(bits)));
            if (rotate) {
                // The amount modulo the width from the top word down; a
                // negative amount's magnitude is 2^W - A.
                const auto chunks = detail::wide_chunks(amount_width);
                auto* wide = builder_.getInt128Ty();
                auto* width = llvm::ConstantInt::get(wide, bits);
                auto* modulus = each_word(amount_width, { constant(0U) },
                    [&](llvm::Value* j, const Carried& state) -> Carried {
                        auto* k = builder_.CreateSub(constant(chunks - 1U), j);
                        auto* folded = builder_.CreateOr(
                            builder_.CreateShl(builder_.CreateZExt(state[0], wide),
                                llvm::ConstantInt::get(wide, 64U)),
                            builder_.CreateZExt(read_word(amount, 0U, k), wide));
                        return { builder_.CreateTrunc(builder_.CreateURem(folded, width), i64_) };
                    })[0];
                std::uint64_t power = 1U % bits;
                for (std::uint32_t index = 0U; index < amount_width; ++index) {
                    power = (power * 2U) % bits;
                }
                residue = builder_.CreateSelect(negative,
                    builder_.CreateURem(
                        builder_.CreateAdd(builder_.CreateSub(constant(power), modulus),
                            constant(bits)),
                        constant(bits)),
                    modulus);
            }
        }
        auto* left = forward_left ? builder_.CreateNot(negative) : negative;
        // Each plane's fills: an arithmetic left shift repeats the lowest
        // element, an arithmetic right shift the top one (shift_value).
        const auto top = detail::wide_chunks(bits) - 1U;
        std::array<llvm::Value*, 4> fill_low { };
        std::array<llvm::Value*, 4> fill_high { };
        for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
            fill_low[plane] = constant(0U);
            fill_high[plane] = constant(0U);
            if (arithmetic && value.present[plane]) {
                fill_low[plane] = builder_.CreateSub(constant(0U),
                    builder_.CreateAnd(read_word(value, plane, constant(0U)), constant(1U)));
                fill_high[plane] = builder_.CreateSub(constant(0U),
                    builder_.CreateAnd(
                        builder_.CreateLShr(read_word(value, plane, constant(top)),
                            constant((bits - 1U) % 64U)),
                        constant(1U)));
            }
        }
        // A left rotation by `turn` (a right one by width - residue).
        llvm::Value* turn { };
        if (rotate) {
            turn = builder_.CreateSelect(left, residue,
                builder_.CreateSelect(builder_.CreateICmpEQ(residue, constant(0U)), constant(0U),
                    builder_.CreateSub(constant(bits), residue)));
        }
        const auto out = result_words(inst, { &value }, false);
        auto* ones = constant(~std::uint64_t { 0 });
        each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            auto* position = builder_.CreateShl(k, constant(6U));
            const auto moved = [&](const std::uint32_t plane) -> llvm::Value* {
                if (!value.present[plane]) {
                    return nullptr;
                }
                if (rotate) {
                    auto* from = builder_.CreateSub(position, turn);
                    return builder_.CreateOr(
                        word_at(value, plane, from, constant(0U), constant(0U)),
                        word_at(value, plane, builder_.CreateAdd(from, constant(bits)),
                            constant(0U), constant(0U)));
                }
                auto* shifted = builder_.CreateSelect(left,
                    word_at(value, plane, builder_.CreateSub(position, magnitude),
                        fill_low[plane], constant(0U)),
                    word_at(value, plane, builder_.CreateAdd(position, magnitude),
                        constant(0U), fill_high[plane]));
                return builder_.CreateSelect(saturated,
                    builder_.CreateSelect(left, fill_low[plane], fill_high[plane]), shifted);
            };
            // An unknown amount gives all X: Logic4, no masks.
            WideVal result;
            result.a = builder_.CreateSelect(unknown, ones, moved(0U));
            result.b = builder_.CreateSelect(unknown, ones, moved(1U));
            if (auto* u = moved(2U)) {
                result.u = builder_.CreateSelect(unknown, constant(0U), u);
            }
            if (auto* w = moved(3U)) {
                result.w = builder_.CreateSelect(unknown, constant(0U), w);
            }
            write_result_word(out, masks, k, result);
            return {};
        });
        commit_words(inst, out);
    }

    /// emit_wide_dynamic_part_select over words: result bit i is source bit
    /// first + i (a field) or first - i (the other direction) inside the
    /// selected interval, the fill outside it.
    void emit_words_dynamic_part_select(const KInst& inst)
    {
        const auto& part = body_.parts[inst.imm_b];
        const bool increasing = (inst.flags & detail::flag_increasing) != 0U;
        const bool descending = (inst.flags & detail::flag_descending) != 0U;
        const bool two_state = (inst.flags & detail::flag_two_state) != 0U;
        const auto bits = inst.width;
        const auto source_bits = static_cast<std::uint32_t>(inst.imm_a);
        const auto lower = std::min(part.left, part.right);
        const auto upper = std::max(part.left, part.right);
        const auto edge = static_cast<std::int64_t>(bits) - 1;
        const auto signed_constant = [&](const std::int64_t number) {
            return constant(static_cast<std::uint64_t>(number));
        };
        const auto smax = [&](llvm::Value* x, llvm::Value* y) {
            return builder_.CreateSelect(builder_.CreateICmpSGT(x, y), x, y);
        };
        const auto smin = [&](llvm::Value* x, llvm::Value* y) {
            return builder_.CreateSelect(builder_.CreateICmpSLT(x, y), x, y);
        };
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto source = operand_words(inst.x, source_bits, masks);
        const auto base = load_register(inst.y);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(base.b, constant(mask_of(32U))), constant(0U));
        auto* selected_right = builder_.CreateAdd(signed_index(base),
            signed_constant(increasing ? (descending ? 0 : edge)
                                       : (descending ? -edge : 0)));
        const bool field = (descending && part.right == lower)
            || (!descending && part.right == upper);
        llvm::Value* low = descending
            ? builder_.CreateSub(signed_constant(lower), selected_right)
            : builder_.CreateSub(selected_right, signed_constant(upper));
        llvm::Value* high = descending
            ? builder_.CreateSub(signed_constant(upper), selected_right)
            : builder_.CreateSub(selected_right, signed_constant(lower));
        llvm::Value* first = nullptr;
        if (field) {
            first = builder_.CreateAdd(
                signed_constant(static_cast<std::int64_t>(part.base_offset)),
                descending ? builder_.CreateSub(selected_right, signed_constant(part.right))
                           : builder_.CreateSub(signed_constant(part.right), selected_right));
            low = smax(smax(low, constant(0U)), builder_.CreateSub(constant(0U), first));
            high = smin(smin(high, signed_constant(edge)),
                builder_.CreateSub(signed_constant(static_cast<std::int64_t>(source_bits) - 1),
                    first));
        } else {
            first = descending
                ? builder_.CreateSub(
                      signed_constant(static_cast<std::int64_t>(part.base_offset) + part.right),
                      selected_right)
                : builder_.CreateAdd(selected_right,
                      signed_constant(static_cast<std::int64_t>(part.base_offset) - part.right));
            low = smax(smax(low, constant(0U)),
                builder_.CreateSub(first,
                    signed_constant(static_cast<std::int64_t>(source_bits) - 1)));
            high = smin(smin(high, signed_constant(edge)), first);
        }
        auto* any = builder_.CreateAnd(known, builder_.CreateICmpSLE(low, high));
        auto* end = builder_.CreateAdd(high, constant(1U));
        auto* fill = constant(two_state ? 0U : ~std::uint64_t { 0 });
        const auto out = result_words(inst, { &source }, false);
        each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            auto* selected = builder_.CreateSelect(any, field_word(k, low, end), constant(0U));
            auto* position = builder_.CreateShl(k, constant(6U));
            const auto moved = [&](const std::uint32_t plane) -> llvm::Value* {
                if (!source.present[plane]) {
                    return nullptr;
                }
                llvm::Value* word = nullptr;
                if (field) {
                    word = word_at(source, plane, builder_.CreateAdd(first, position),
                        constant(0U), constant(0U));
                } else {
                    word = builder_.CreateUnaryIntrinsic(llvm::Intrinsic::bitreverse,
                        word_at(source, plane,
                            builder_.CreateSub(builder_.CreateSub(first, position),
                                constant(63U)),
                            constant(0U), constant(0U)));
                }
                return builder_.CreateAnd(word, selected);
            };
            WideVal result;
            result.a = builder_.CreateOr(builder_.CreateAnd(fill, bit_not(selected)), moved(0U));
            result.b = builder_.CreateOr(builder_.CreateAnd(fill, bit_not(selected)), moved(1U));
            result.u = moved(2U);
            result.w = moved(3U);
            write_result_word(out, masks, k, result);
            return {};
        });
        commit_words(inst, out);
    }

    /// emit_wide_extract over words.
    void emit_words_extract(const KInst& inst)
    {
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto source = operand_words(inst.x, static_cast<std::uint32_t>(inst.imm_b),
            wide_has(inst, detail::wide_alu_masks, 1U));
        const auto out = result_words(inst, { &source }, false);
        each_word(inst.width, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            auto* position = builder_.CreateAdd(builder_.CreateShl(k, constant(6U)),
                constant(inst.imm_a));
            const auto field = [&](const std::uint32_t plane) -> llvm::Value* {
                return source.present[plane]
                    ? word_at(source, plane, position, constant(0U), constant(0U))
                    : nullptr;
            };
            write_result_word(out, masks, k, { field(0U), field(1U), field(2U), field(3U) });
            return {};
        });
        commit_words(inst, out);
    }

    /// emit_wide_insert and emit_wide_dynamic_insert over words.
    void emit_words_insert(const KInst& inst, const std::uint32_t at, const bool dynamic)
    {
        const auto bits = inst.width;
        const auto source_bits = static_cast<std::uint32_t>(inst.imm_a);
        llvm::Value* offset = constant(inst.imm_b);
        llvm::Value* ok = nullptr;
        if (dynamic) {
            const auto& selection = body_.indices[static_cast<std::uint32_t>(inst.imm_b)];
            const auto index = load_register(inst.z);
            const auto lower = std::min(selection.left, selection.right);
            const auto upper = std::max(selection.left, selection.right);
            auto* known = builder_.CreateICmpEQ(
                builder_.CreateAnd(index.b, constant(mask_of(32U))), constant(0U));
            auto* number = signed_index(index);
            auto* in_range = builder_.CreateAnd(
                builder_.CreateICmpSGE(number, constant(static_cast<std::uint64_t>(lower))),
                builder_.CreateICmpSLE(number, constant(static_cast<std::uint64_t>(upper))));
            auto* right = constant(static_cast<std::uint64_t>(selection.right));
            auto* distance = builder_.CreateSelect(builder_.CreateICmpSGE(number, right),
                builder_.CreateSub(number, right), builder_.CreateSub(right, number));
            auto* placed = builder_.CreateAdd(distance, constant(selection.base_offset));
            auto* fits = builder_.CreateICmpULE(
                builder_.CreateAdd(placed, constant(source_bits)), constant(bits));
            ok = builder_.CreateAnd(builder_.CreateAnd(known, in_range), fits);
            if ((inst.flags & detail::flag_strict) != 0U) {
                check_or_fail(known, at, 13U);
            check_or_fail(ok, at, 9U);
            }
            offset = builder_.CreateSelect(ok, placed, constant(0U));
        }
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto target = operand_words(inst.x, bits, wide_has(inst, detail::wide_alu_masks, 1U));
        WordPlanes source;
        if (wide_has(inst, detail::wide_alu_wide, 2U)) {
            source = operand_words(inst.y, source_bits, wide_has(inst, detail::wide_alu_masks, 2U));
        } else {
            // A narrow register's word and, if tracked, its masks
            // (load_insert_source).
            auto* m = constant(mask_of(source_bits));
            const auto narrow = load_register(inst.y);
            source.bits = source_bits;
            source.narrow = { builder_.CreateAnd(narrow.a, m), builder_.CreateAnd(narrow.b, m),
                nullptr, nullptr };
            source.present = { true, true, false, false };
            if (tracked(inst.y) && !two_state_) {
                source.narrow[2] = builder_.CreateAnd(unknown_of(inst.y), m);
                source.present[2] = true;
                if (weak_mode()) {
                    source.narrow[3] = builder_.CreateAnd(weak_of(inst.y), m);
                    source.present[3] = true;
                }
            }
        }
        auto* end = builder_.CreateAdd(offset, constant(source_bits));
        const auto out = result_words(inst, { &target, &source }, false);
        each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            auto* field = field_word(k, offset, end);
            auto* position = builder_.CreateSub(builder_.CreateShl(k, constant(6U)), offset);
            const auto written = [&](const std::uint32_t plane) -> llvm::Value* {
                if (!target.present[plane] && !source.present[plane]) {
                    return nullptr;
                }
                auto* kept = read_word(target, plane, k);
                auto* combined = builder_.CreateOr(builder_.CreateAnd(kept, bit_not(field)),
                    builder_.CreateAnd(
                        word_at(source, plane, position, constant(0U), constant(0U)), field));
                // Outside the target or the range the target stays as it is.
                return ok == nullptr ? combined : builder_.CreateSelect(ok, combined, kept);
            };
            write_result_word(out, masks, k, { written(0U), written(1U), written(2U), written(3U) });
            return {};
        });
        commit_words(inst, out);
    }

    /// emit_wide_reduce over words.
    void emit_words_reduce(const KInst& inst)
    {
        using runtime::simir::ReductionOperator;
        const auto bits = inst.width;
        const auto value = operand_words(inst.x, bits, false);
        // (a zero, a one, an unknown, the count of ones).
        const auto seen = each_word(bits,
            { builder_.getFalse(), builder_.getFalse(), builder_.getFalse(), constant(0U) },
            [&](llvm::Value* k, const Carried& state) -> Carried {
                auto* a = read_word(value, 0U, k);
                auto* b = read_word(value, 1U, k);
                auto* ones = builder_.CreateAnd(a, bit_not(b));
                auto* zeros = builder_.CreateAnd(
                    builder_.CreateAnd(bit_not(a), bit_not(b)), valid_bits(bits, k));
                return { builder_.CreateOr(state[0], nonzero(zeros)),
                    builder_.CreateOr(state[1], nonzero(ones)),
                    builder_.CreateOr(state[2], nonzero(b)),
                    builder_.CreateAdd(state[3],
                        builder_.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, ones)) };
            });
        switch (static_cast<ReductionOperator>(inst.sub)) {
        case ReductionOperator::one_hot:
            store_register(inst.d, scalar_bool(builder_.CreateICmpEQ(seen[3], constant(1U))));
            return;
        case ReductionOperator::one_hot_or_zero:
            store_register(inst.d, scalar_bool(builder_.CreateICmpULE(seen[3], constant(1U))));
            return;
        case ReductionOperator::bit_and:
            store_register(inst.d,
                select(seen[0], Pair { constant(0U), constant(0U) },
                    select(seen[2], x_value(1U), Pair { constant(1U), constant(0U) })));
            return;
        case ReductionOperator::bit_or:
            store_register(inst.d,
                select(seen[1], Pair { constant(1U), constant(0U) },
                    select(seen[2], x_value(1U), Pair { constant(0U), constant(0U) })));
            return;
        case ReductionOperator::bit_xor: {
            auto* odd = builder_.CreateTrunc(seen[3], builder_.getInt1Ty());
            store_register(inst.d, select(seen[2], x_value(1U), scalar_bool(odd)));
            return;
        }
        }
        store_register(inst.d, x_value(1U));
    }

    /// emit_wide_conditional over words.
    void emit_words_conditional(const KInst& inst)
    {
        const auto bits = inst.width;
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto condition = load_register(inst.x);
        const auto when_true = operand_words(inst.y, bits, wide_has(inst, detail::wide_alu_masks, 2U));
        const auto when_false = operand_words(inst.z, bits, wide_has(inst, detail::wide_alu_masks, 3U));
        auto* a0 = builder_.CreateTrunc(condition.a, builder_.getInt1Ty());
        auto* b0 = builder_.CreateTrunc(condition.b, builder_.getInt1Ty());
        auto* is_one = builder_.CreateAnd(a0, builder_.CreateNot(b0));
        auto* is_zero = builder_.CreateAnd(builder_.CreateNot(a0), builder_.CreateNot(b0));
        const auto out = result_words(inst, { &when_true, &when_false }, true);
        each_word(bits, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            const auto t = read_words(when_true, k);
            const auto f = read_words(when_false, k);
            auto* same = bit_not(builder_.CreateOr(builder_.CreateOr(t.b, f.b),
                builder_.CreateXor(t.a, f.a)));
            auto* differ = bit_not(same);
            WideVal result;
            result.a = builder_.CreateSelect(is_one, t.a,
                builder_.CreateSelect(is_zero, f.a,
                    builder_.CreateOr(builder_.CreateAnd(t.a, same), differ)));
            result.b = builder_.CreateSelect(is_one, t.b,
                builder_.CreateSelect(is_zero, f.b, differ));
            const auto chosen = [&](llvm::Value* x, llvm::Value* y) -> llvm::Value* {
                if (x == nullptr && y == nullptr) {
                    return nullptr;
                }
                return builder_.CreateSelect(is_one, x != nullptr ? x : constant(0U),
                    builder_.CreateSelect(is_zero, y != nullptr ? y : constant(0U),
                        constant(0U)));
            };
            result.u = chosen(t.u, f.u);
            result.w = chosen(t.w, f.w);
            write_result_word(out, masks, k, result);
            return {};
        });
        commit_words(inst, out);
    }

    /// The families whose planes are each element's own (unary_not,
    /// two_state) over words.
    void emit_words_unary(const KInst& inst, const bool two_state)
    {
        const bool masks = wide_has(inst, detail::wide_alu_masks, 0U);
        const auto value = operand_words(inst.x, inst.width,
            !two_state && wide_has(inst, detail::wide_alu_masks, 1U));
        const auto out = result_words(inst, { &value }, true);
        each_word(inst.width, {}, [&](llvm::Value* k, const Carried&) -> Carried {
            const auto in = read_words(value, k);
            WideVal result;
            if (two_state) {
                // ConvertToTwoState: the elements that read as 1.
                result.a = builder_.CreateAnd(in.a, bit_not(in.b));
                result.b = constant(0U);
            } else {
                // X, Z and 'U' invert to X (the 'U' mask kept), 0 and 1
                // swap; no weak element survives.
                result.a = builder_.CreateOr(bit_not(in.a), in.b);
                result.b = in.b;
                result.u = in.u;
            }
            write_result_word(out, masks, k, result);
            return {};
        });
        commit_words(inst, out);
    }

    /// Whether the operation's operands are wide enough for word loops.
    [[nodiscard]] static bool takes_word_loops(const KInst& inst)
    {
        using detail::WideFamily;
        auto bits = inst.width;
        switch (static_cast<WideFamily>(inst.offset)) {
        case WideFamily::extract:
            bits = std::max(bits, static_cast<std::uint32_t>(inst.imm_b));
            break;
        case WideFamily::insert:
        case WideFamily::dynamic_insert:
        case WideFamily::dynamic_part_select:
            bits = std::max(bits, static_cast<std::uint32_t>(inst.imm_a));
            break;
        case WideFamily::shift:
            if (wide_has(inst, detail::wide_alu_wide, 2U)) {
                bits = std::max(bits, static_cast<std::uint32_t>(inst.imm_a));
            }
            break;
        case WideFamily::logical_binary:
            bits = 0U;
            if (wide_has(inst, detail::wide_alu_wide, 1U)) {
                bits = std::max(bits, static_cast<std::uint32_t>(inst.imm_b));
            }
            if (wide_has(inst, detail::wide_alu_wide, 2U)) {
                bits = std::max(bits, static_cast<std::uint32_t>(inst.imm_a));
            }
            break;
        default:
            break;
        }
        return detail::wide_chunks(bits) > wide_word_loop_chunks;
    }

    /// emit_wide_alu over words.
    void emit_words_alu(const KInst& inst, const std::uint32_t at)
    {
        using detail::WideFamily;
        word_scratch_used_ = 0U;
        switch (static_cast<WideFamily>(inst.offset)) {
        case WideFamily::extract:
            emit_words_extract(inst);
            break;
        case WideFamily::insert:
            emit_words_insert(inst, at, false);
            break;
        case WideFamily::dynamic_insert:
            emit_words_insert(inst, at, true);
            break;
        case WideFamily::dynamic_part_select:
            emit_words_dynamic_part_select(inst);
            break;
        case WideFamily::binary:
            emit_words_binary(inst);
            break;
        case WideFamily::shift:
            emit_words_shift(inst);
            break;
        case WideFamily::unary_not:
            emit_words_unary(inst, false);
            break;
        case WideFamily::two_state:
            emit_words_unary(inst, true);
            break;
        case WideFamily::reduce:
            emit_words_reduce(inst);
            break;
        case WideFamily::conditional:
            emit_words_conditional(inst);
            break;
        case WideFamily::logical_not: {
            const auto [one, unknown] = words_truth(operand_words(inst.x, inst.width, false));
            auto* zero = builder_.CreateNot(builder_.CreateOr(one, unknown));
            store_register(inst.d, from_truth(zero, unknown));
            break;
        }
        case WideFamily::logical_binary: {
            const auto truth_of = [&](const std::uint32_t slot, const std::uint32_t reg,
                                      const std::uint32_t bits) {
                return wide_has(inst, detail::wide_alu_wide, slot)
                    ? words_truth(operand_words(reg, bits, false))
                    : truth(load_register(reg));
            };
            const auto [left_one, left_unknown]
                = truth_of(1U, inst.x, static_cast<std::uint32_t>(inst.imm_b));
            const auto [right_one, right_unknown]
                = truth_of(2U, inst.y, static_cast<std::uint32_t>(inst.imm_a));
            auto* left_zero = builder_.CreateNot(builder_.CreateOr(left_one, left_unknown));
            auto* right_zero = builder_.CreateNot(builder_.CreateOr(right_one, right_unknown));
            llvm::Value* one { };
            llvm::Value* zero { };
            if (static_cast<LogicalBinaryOperator>(inst.sub)
                == LogicalBinaryOperator::logical_and) {
                zero = builder_.CreateOr(left_zero, right_zero);
                one = builder_.CreateAnd(left_one, right_one);
            } else {
                one = builder_.CreateOr(left_one, right_one);
                zero = builder_.CreateAnd(left_zero, right_zero);
            }
            auto* unknown = builder_.CreateNot(builder_.CreateOr(one, zero));
            store_register(inst.d, from_truth(one, unknown));
            break;
        }
        }
    }

    void emit_wide_alu(const KInst& inst, const std::uint32_t at)
    {
        using detail::WideFamily;
        if (takes_word_loops(inst)) {
            emit_words_alu(inst, at);
            return;
        }
        switch (static_cast<WideFamily>(inst.offset)) {
        case WideFamily::extract:
            emit_wide_extract(inst);
            break;
        case WideFamily::insert:
            emit_wide_insert(inst);
            break;
        case WideFamily::dynamic_insert:
            emit_wide_dynamic_insert(inst, at);
            break;
        case WideFamily::dynamic_part_select:
            emit_wide_dynamic_part_select(inst);
            break;
        case WideFamily::binary:
            emit_wide_binary(inst);
            break;
        case WideFamily::shift:
            emit_wide_shift(inst);
            break;
        case WideFamily::unary_not: {
            // Logic4 and Logic9 alike: X, Z and 'U' elements invert to X
            // (with the 'U' mask kept), 0 and 1 swap; no weak element
            // survives (unary_not, kernel_word::unary_not).
            const auto value = load_wide(inst.x, inst.width,
                wide_has(inst, detail::wide_alu_masks, 1U));
            WideVal result;
            result.a = builder_.CreateOr(builder_.CreateNot(value.a), value.b);
            result.b = value.b;
            result.u = value.u;
            store_wide_result(inst, result);
            break;
        }
        case WideFamily::reduce:
            emit_wide_reduce(inst);
            break;
        case WideFamily::conditional:
            emit_wide_conditional(inst);
            break;
        case WideFamily::logical_not: {
            const auto [one, unknown] = wide_truth(load_wide(inst.x, inst.width, false),
                inst.width);
            // logic_not: one -> 0, zero -> 1, unknown -> x.
            auto* zero = builder_.CreateNot(builder_.CreateOr(one, unknown));
            store_register(inst.d, from_truth(zero, unknown));
            break;
        }
        case WideFamily::logical_binary: {
            const auto truth_of = [&](const std::uint32_t slot, const std::uint32_t reg,
                                      const std::uint32_t bits) {
                return wide_has(inst, detail::wide_alu_wide, slot)
                    ? wide_truth(load_wide(reg, bits, false), bits)
                    : truth(load_register(reg));
            };
            const auto [left_one, left_unknown]
                = truth_of(1U, inst.x, static_cast<std::uint32_t>(inst.imm_b));
            const auto [right_one, right_unknown]
                = truth_of(2U, inst.y, static_cast<std::uint32_t>(inst.imm_a));
            auto* left_zero = builder_.CreateNot(builder_.CreateOr(left_one, left_unknown));
            auto* right_zero = builder_.CreateNot(builder_.CreateOr(right_one, right_unknown));
            llvm::Value* one { };
            llvm::Value* zero { };
            if (static_cast<LogicalBinaryOperator>(inst.sub)
                == LogicalBinaryOperator::logical_and) {
                zero = builder_.CreateOr(left_zero, right_zero);
                one = builder_.CreateAnd(left_one, right_one);
            } else {
                one = builder_.CreateOr(left_one, right_one);
                zero = builder_.CreateAnd(left_zero, right_zero);
            }
            auto* unknown = builder_.CreateNot(builder_.CreateOr(one, zero));
            store_register(inst.d, from_truth(one, unknown));
            break;
        }
        case WideFamily::two_state: {
            // ConvertToTwoState: the elements that read as 1.
            const auto value = load_wide(inst.x, inst.width, false);
            WideVal result;
            result.a = builder_.CreateAnd(value.a, builder_.CreateNot(value.b));
            result.b = wide_constant(inst.width, 0U);
            store_wide_result(inst, result);
            break;
        }
        }
    }

    void emit_instruction(const std::uint32_t at)
    {
        const auto& inst = body_.code[at];
        current_ = at;
        switch (inst.op) {
        case KOp::nop:
        case KOp::mem_bind:
            break;
        case KOp::suspend:
            // Report the suspension operation to the thread runner, with
            // its boundary helper for a behavioral thread (aux 1, the
            // operation's kind in `sub`).
            builder_.CreateStore(llvm::ConstantInt::get(i32_, inst.x),
                frame_field(44U));
            // The thread runner and the next run read the register file.
            flush_local_registers();
            if (inst.aux == 1U) {
                auto* type = llvm::FunctionType::get(i32_, { ptr_, i32_, i32_ }, false);
                builder_.CreateStore(
                    helper_symbol(
                        ("fsim_sk_boundary_" + std::to_string(inst.sub)).c_str(), type),
                    frame_field(72U));
            }
            builder_.CreateRet(llvm::ConstantInt::get(i32_, 3U));
            break;
        case KOp::generic: {
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, ptr_, i32_ }, false);
            // aux 2: an attempt-start report (Display::inert), reached only
            // while a library has bound cbAssertionStart: one test of the
            // frame's flag, nothing else while it is clear.
            llvm::BasicBlock* report_skip = nullptr;
            if (inst.aux == 2U) {
                auto* bound = builder_.CreateICmpNE(
                    builder_.CreateLoad(i32_, frame_field(112U)),
                    llvm::ConstantInt::get(i32_, 0U));
                auto* report = llvm::BasicBlock::Create(context_, "start_report", &function_);
                report_skip = llvm::BasicBlock::Create(context_, "start_skip", &function_);
                builder_.CreateCondBr(bound, report, report_skip);
                builder_.SetInsertPoint(report);
            }
            // The helper of the instruction's operation kind (`sub`).
            auto* callee = helper_symbol(
                ("fsim_sk_generic_" + std::to_string(inst.sub)).c_str(), type);
            builder_.CreateCall(type, callee,
                { frame_, registers_, llvm::ConstantInt::get(i32_, at) });
            // The helper reads and writes registers through the pointer.
            cache_.clear();
            if (two_state_) {
                // A result with X or 'U' bits continues in the full code
                // (status 4, see StaticKernel::native_generic_kind).
                (void)continue_point();
            }
            check_status();
            if (report_skip != nullptr) {
                builder_.CreateBr(report_skip);
                builder_.SetInsertPoint(report_skip);
            }
            break;
        }
        case KOp::sva_tick: {
            if (inst.z != 0U) {
                // The directive's plan is constant: its attempt automaton is
                // generated code (SvaAutomatonEmitter); growth of the block
                // is the helper that stays.
                const auto& plan = body_.sva_plans.at(inst.z - 1U);
                const auto guards = inst.y + static_cast<std::uint32_t>(inst.imm_a & 0xffff'ffffU);
                const auto storage = guards < body_.wide_base.size() && body_.wide_base[guards] != 0U
                    ? body_.wide_base[guards] & ~detail::wide_unknown_flag
                    : guards;
                SvaAutomatonEmitter automaton(module_, plan, sva_check_enabled());
                auto* tick = automaton.tick_function(inst, at, storage);
                note_helper_site(KOp::sva_tick, 1U);
                builder_.CreateCall(tick, { frame_, registers_ });
                cache_.clear();
                check_status();
                break;
            }
            // The directive's clock tick (IEEE 1800-2023 16.14; task #186):
            // the kernel's tick entry reads the guards, start and disabled
            // registers from the register file and writes the pending
            // outcome count to d (StaticKernel::execute_sva_tick).
            note_helper_site(KOp::sva_tick, 0U);
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, ptr_, i32_ }, false);
            builder_.CreateCall(type, helper_symbol("fsim_sk_sva_tick", type),
                { frame_, registers_, llvm::ConstantInt::get(i32_, at) });
            cache_.clear();
            check_status();
            break;
        }
        case KOp::sva_outcome: {
            // The inline ring pop of the directive's attempt block
            // (sva::AttemptStorage::pop_outcome, layout in sva_storage.hpp):
            // the block base comes from the kernel's table at entry
            // member_base[member] + aux; a null entry binds through the
            // helper (a restored monitor), and no monitor gives 0.
            using namespace runtime::sva::layout;
            auto* table = builder_.CreateLoad(ptr_, frame_field(120U));
            auto* blocks = builder_.CreateLoad(ptr_, table);
            auto* member_base = builder_.CreateLoad(ptr_,
                builder_.CreateConstInBoundsGEP1_64(i8_, table, 8U));
            auto* member = builder_.CreateLoad(i32_, frame_field(32U));
            auto* base = builder_.CreateLoad(i32_,
                builder_.CreateInBoundsGEP(i32_, member_base, member));
            auto* index = builder_.CreateAdd(base, llvm::ConstantInt::get(i32_, inst.aux));
            auto* entry = builder_.CreateInBoundsGEP(ptr_, blocks, index);
            auto* loaded = builder_.CreateLoad(ptr_, entry);
            auto* null = llvm::ConstantPointerNull::get(ptr_);
            auto* bind = llvm::BasicBlock::Create(context_, "sva_bind", &function_);
            auto* test = llvm::BasicBlock::Create(context_, "sva_test", &function_);
            auto* pop = llvm::BasicBlock::Create(context_, "sva_pop", &function_);
            auto* done = llvm::BasicBlock::Create(context_, "sva_done", &function_);
            auto* from_load = builder_.GetInsertBlock();
            builder_.CreateCondBr(builder_.CreateICmpEQ(loaded, null), bind, test);
            builder_.SetInsertPoint(bind);
            auto* bind_type = llvm::FunctionType::get(ptr_, { ptr_, i32_ }, false);
            auto* bound = builder_.CreateCall(bind_type,
                helper_symbol("fsim_sk_sva_bind", bind_type),
                { frame_, llvm::ConstantInt::get(i32_, at) });
            auto* from_bind = builder_.GetInsertBlock();
            builder_.CreateBr(test);
            builder_.SetInsertPoint(test);
            auto* block = builder_.CreatePHI(ptr_, 2U);
            block->addIncoming(loaded, from_load);
            block->addIncoming(bound, from_bind);
            const auto word = [&](const std::uint64_t at_word) {
                return builder_.CreateConstInBoundsGEP1_64(i64_, block, at_word);
            };
            // A null block has no words to read: its count is taken as 0,
            // which skips the pop block (the load below is then not reached
            // through the select's chosen value, but it must not trap: the
            // count is loaded only in the pop path of a non-null block).
            auto* has_block = builder_.CreateICmpNE(block, null);
            auto* count_block = llvm::BasicBlock::Create(context_, "sva_count", &function_);
            auto* from_test = builder_.GetInsertBlock();
            builder_.CreateCondBr(has_block, count_block, done);
            builder_.SetInsertPoint(count_block);
            auto* count = builder_.CreateLoad(i64_, word(h_outcomes_count));
            auto* from_count = builder_.GetInsertBlock();
            builder_.CreateCondBr(nonzero(count), pop, done);
            builder_.SetInsertPoint(pop);
            auto* head = builder_.CreateLoad(i64_, word(h_outcomes_head));
            auto* capacity = builder_.CreateLoad(i64_, word(h_outcomes_cap));
            auto* offset = builder_.CreateLoad(i64_, word(h_outcomes_off));
            auto* row = builder_.CreateAdd(offset,
                builder_.CreateMul(head, constant(outcome_stride)));
            const auto row_word = [&](const std::uint64_t field) {
                return builder_.CreateInBoundsGEP(i64_, block,
                    builder_.CreateAdd(row, constant(field)));
            };
            auto* code = builder_.CreateLoad(i64_, row_word(0U));
            builder_.CreateStore(builder_.CreateLoad(i64_, row_word(1U)),
                word(h_last_start));
            builder_.CreateStore(builder_.CreateLoad(i64_, row_word(2U)),
                word(h_last_fail_line));
            builder_.CreateStore(builder_.CreateLoad(i64_, row_word(3U)),
                word(h_last_fail_column));
            builder_.CreateStore(
                builder_.CreateAnd(builder_.CreateAdd(head, constant(1U)),
                    builder_.CreateSub(capacity, constant(1U))),
                word(h_outcomes_head));
            builder_.CreateStore(builder_.CreateSub(count, constant(1U)),
                word(h_outcomes_count));
            auto* serial_address = builder_.CreateLoad(ptr_,
                builder_.CreateConstInBoundsGEP1_64(i8_, table, 16U));
            auto* serial = builder_.CreateAdd(
                builder_.CreateLoad(i64_, serial_address), constant(1U));
            builder_.CreateStore(serial, serial_address);
            builder_.CreateStore(serial, word(h_pop_serial));
            auto* from_pop = builder_.GetInsertBlock();
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            auto* result = builder_.CreatePHI(i64_, 3U);
            result->addIncoming(constant(0U), from_test);
            result->addIncoming(constant(0U), from_count);
            result->addIncoming(code, from_pop);
            store_register(inst.d, { result, constant(0U) });
            if (sva_verify_enabled()) {
                // FSIM_KERNEL_VERIFY: the pop's code, for the replay.
                auto* verify_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                    { ptr_, i32_, i64_ }, false);
                builder_.CreateCall(verify_type,
                    helper_symbol("fsim_sk_sva_verify_outcome", verify_type),
                    { frame_, llvm::ConstantInt::get(i32_, at), result });
                check_status();
            }
            break;
        }
        case KOp::sva_sampled_load: {
            if ((inst.aux & 2U) != 0U) {
                emit_sampled_general(inst);
                break;
            }
            // A sampled-value read (IEEE 1800-2023 16.5.1, 16.9.3) from the
            // host's sampled words: the Preponed value (aux bit 0) or a
            // clocked history ring (Impl::SampledRing layout), as
            // run_sampled_read computes it for a Logic4 signal of at most
            // 64 bits. A slot index is reduced modulo the capacity, so the
            // loads before the count tests stay inside the ring and selects
            // replace branches.
            auto* table = builder_.CreateLoad(ptr_, frame_field(120U));
            auto* m = constant(mask_of(inst.width));
            if (inst.aux == 4U) {
                // $time or $stime (20.3): the time step in the unit's ticks,
                // rounded half up as systemverilog_time_function does, with
                // the table's time as the past read takes it.
                auto* now = builder_.CreateLoad(i64_,
                    builder_.CreateLoad(ptr_,
                        builder_.CreateConstInBoundsGEP1_64(i8_, table, 40U)));
                const auto ticks = inst.imm_a;
                auto* units = builder_.CreateUDiv(now, constant(ticks));
                auto* round = builder_.CreateICmpUGE(
                    builder_.CreateURem(now, constant(ticks)),
                    constant(ticks / 2U + ticks % 2U));
                units = builder_.CreateAdd(units, builder_.CreateZExt(round, i64_));
                store_register(inst.d, { builder_.CreateAnd(units, m), constant(0U) });
                break;
            }
            if ((inst.aux & 1U) != 0U) {
                auto* sampled = builder_.CreateLoad(ptr_,
                    builder_.CreateConstInBoundsGEP1_64(i8_, table, 32U));
                auto* a = builder_.CreateLoad(i64_,
                    builder_.CreateConstInBoundsGEP1_64(i64_, sampled, inst.x));
                auto* b = builder_.CreateLoad(i64_,
                    builder_.CreateConstInBoundsGEP1_64(i64_, sampled, inst.x + 1U));
                store_register(inst.d,
                    { builder_.CreateAnd(a, m), builder_.CreateAnd(b, m) });
                break;
            }
            auto* rings = builder_.CreateLoad(ptr_,
                builder_.CreateConstInBoundsGEP1_64(i8_, table, 24U));
            auto* ring = builder_.CreateConstInBoundsGEP1_64(i64_, rings, inst.x);
            const auto ring_word = [&](const std::uint64_t at_word) {
                return builder_.CreateLoad(i64_,
                    builder_.CreateConstInBoundsGEP1_64(i64_, ring, at_word));
            };
            auto* capacity = ring_word(0U);
            auto* first = ring_word(1U);
            auto* count = ring_word(2U);
            auto* value_words = ring_word(3U);
            const Pair fallback { constant(inst.imm_a), constant(inst.imm_b) };
            const auto value = [&](llvm::Value* index) {
                auto* slot = builder_.CreateURem(builder_.CreateAdd(first, index),
                    capacity);
                auto* at_word = builder_.CreateAdd(constant(12U),
                    builder_.CreateMul(slot, value_words));
                auto* a = builder_.CreateLoad(i64_,
                    builder_.CreateInBoundsGEP(i64_, ring, at_word));
                auto* b = builder_.CreateLoad(i64_,
                    builder_.CreateInBoundsGEP(i64_, ring,
                        builder_.CreateAdd(at_word, constant(1U))));
                return Pair { builder_.CreateAnd(a, m), builder_.CreateAnd(b, m) };
            };
            const auto select = [&](llvm::Value* condition, const Pair left,
                                    const Pair right) {
                return Pair { builder_.CreateSelect(condition, left.a, right.a),
                    builder_.CreateSelect(condition, left.b, right.b) };
            };
            const auto kind = static_cast<SignalReadKind>(inst.sub);
            const auto current = select(
                builder_.CreateICmpUGE(count, constant(1U)),
                value(builder_.CreateSub(count, constant(1U))), fallback);
            if (kind == SignalReadKind::sampled) {
                store_register(inst.d, current);
                break;
            }
            if (kind == SignalReadKind::past) {
                // The sample of this time step is not a past one (16.9.3).
                auto* now = builder_.CreateLoad(i64_,
                    builder_.CreateLoad(ptr_,
                        builder_.CreateConstInBoundsGEP1_64(i8_, table, 40U)));
                auto* sampled_now = builder_.CreateAnd(
                    builder_.CreateAnd(nonzero(ring_word(6U)),
                        builder_.CreateICmpEQ(ring_word(7U), now)),
                    builder_.CreateICmpEQ(ring_word(8U),
                        constant(static_cast<std::uint64_t>(
                            ProcessSchedulingDomain::systemverilog))));
                auto* prior = builder_.CreateSub(count,
                    builder_.CreateZExt(sampled_now, i64_));
                auto* depth = constant(inst.z);
                store_register(inst.d,
                    select(builder_.CreateICmpUGE(prior, depth),
                        value(builder_.CreateSub(prior, depth)), fallback));
                break;
            }
            const auto previous = select(
                builder_.CreateICmpUGE(count, constant(2U)),
                value(builder_.CreateSub(count, constant(2U))), fallback);
            const auto bit = [&](llvm::Value* plane) {
                return builder_.CreateTrunc(plane, llvm::Type::getInt1Ty(context_));
            };
            const auto one = [&](const Pair pair) {
                return builder_.CreateAnd(bit(pair.a), builder_.CreateNot(bit(pair.b)));
            };
            const auto zero = [&](const Pair pair) {
                return builder_.CreateAnd(builder_.CreateNot(bit(pair.a)),
                    builder_.CreateNot(bit(pair.b)));
            };
            llvm::Value* result = nullptr;
            switch (kind) {
            case SignalReadKind::rose:
                result = builder_.CreateAnd(one(current), builder_.CreateNot(one(previous)));
                break;
            case SignalReadKind::fell:
                result = builder_.CreateAnd(zero(current), builder_.CreateNot(zero(previous)));
                break;
            default: {
                auto* equal = builder_.CreateAnd(
                    builder_.CreateICmpEQ(current.a, previous.a),
                    builder_.CreateICmpEQ(current.b, previous.b));
                result = kind == SignalReadKind::stable ? equal
                                                        : builder_.CreateNot(equal);
                break;
            }
            }
            store_register(inst.d, { builder_.CreateZExt(result, i64_), constant(0U) });
            break;
        }
        case KOp::net_delay: {
            // The helper reads its source (and 'U' mask) from the register
            // file: a local source is stored there first. It writes no
            // register and no slot, so nothing cached is stale.
            if (inst.x < local_.size() && local_[inst.x] != nullptr) {
                const auto value = load_register(inst.x);
                builder_.CreateStore(value.a, chunk_address(inst.x, 0U));
                builder_.CreateStore(value.b, chunk_address(inst.x, 1U));
            }
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, ptr_, i32_ }, false);
            builder_.CreateCall(type, helper_symbol("fsim_sk_net_delay", type),
                { frame_, registers_, llvm::ConstantInt::get(i32_, at) });
            check_status();
            break;
        }
        case KOp::step: {
            auto* type = llvm::FunctionType::get(i32_, { ptr_, i32_ }, false);
            // The helper of the operation's kind (`sub`).
            auto* callee = helper_symbol(
                ((inst.aux == 2U       ? "fsim_sk_hosted_step_"
                         : inst.aux != 0U ? "fsim_sk_thread_step_"
                                          : "fsim_sk_step_")
                    + std::to_string(inst.sub))
                    .c_str(),
                type);
            auto* next = builder_.CreateCall(type, callee,
                { frame_, llvm::ConstantInt::get(i32_, inst.x) });
            cache_.clear();
            // Control continues at the operation's static successors; any
            // other position (the end of the run, a thread boundary, an
            // error) goes back to the runner in `reserved`.
            const auto size = static_cast<std::uint32_t>(body_.code.size());
            if (inst.offset != 0U) {
                // A Return continues at one of the return targets: every
                // Return shares one switch over them (a switch per Return
                // makes calls x returns edges).
                return_dispatch(size)->addIncoming(next, builder_.GetInsertBlock());
                builder_.CreateBr(return_dispatch_);
                break;
            }
            std::vector<std::uint32_t> successors;
            if (inst.z >= 1U) {
                successors.push_back(inst.d);
            }
            if (inst.z >= 2U) {
                successors.push_back(inst.y);
            }
            std::ranges::sort(successors);
            successors.erase(std::unique(successors.begin(), successors.end()),
                successors.end());
            std::erase_if(successors, [&](const std::uint32_t target) {
                return target >= size || blocks_[target] == nullptr;
            });
            // The position goes to `reserved` first (the runner reads it
            // only after a return), so every step shares one leave block.
            // Compare-and-branch per successor (at most two): fast
            // instruction selection lowers it, where a switch terminator
            // falls back to the selection DAG in every step's block.
            builder_.CreateStore(next, frame_field(44U));
            for (std::size_t index = 0U; index < successors.size(); ++index) {
                auto* otherwise = index + 1U == successors.size()
                    ? step_leave()
                    : llvm::BasicBlock::Create(context_, "next", &function_);
                builder_.CreateCondBr(
                    builder_.CreateICmpEQ(next,
                        llvm::ConstantInt::get(i32_, successors[index])),
                    blocks_[successors[index]], otherwise);
                if (index + 1U != successors.size()) {
                    builder_.SetInsertPoint(otherwise);
                }
            }
            if (successors.empty()) {
                builder_.CreateBr(step_leave());
            }
            break;
        }
        case KOp::mark:
            builder_.CreateStore(binding(inst.x, 0U), frame_field(32U));
            builder_.CreateStore(llvm::ConstantInt::get(i32_, inst.d),
                frame_field(36U));
            if (inst.sub != 0U) {
                // A dissolved partition's gated member runs when the
                // kernel finds it due; otherwise the next member's mark.
                auto* type = llvm::FunctionType::get(i32_, { ptr_, i32_ }, false);
                auto* due = builder_.CreateCall(type,
                    helper_symbol("fsim_sk_member_due", type),
                    { frame_, llvm::ConstantInt::get(i32_, inst.d) });
                auto* checked = llvm::BasicBlock::Create(context_, "due", &function_);
                auto* status = builder_.CreateLoad(i32_, frame_field(40U));
                builder_.CreateCondBr(
                    builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
                    failure_block_, checked);
                builder_.SetInsertPoint(checked);
                builder_.CreateCondBr(
                    builder_.CreateICmpNE(due, llvm::ConstantInt::get(i32_, 0U)),
                    blocks_[at + 1U],
                    blocks_[std::min<std::size_t>(inst.aux, body_.code.size())]);
            }
            break;
        case KOp::constant:
            if (inst.sub == 1U) {
                // A per-instance constant: each plane is two 32-bit bindings.
                const auto word = [&](const std::uint32_t index) {
                    auto* low = builder_.CreateZExt(binding(index, 0U), i64_);
                    auto* high = builder_.CreateZExt(binding(index, 1U), i64_);
                    return builder_.CreateOr(low,
                        builder_.CreateShl(high, constant(32U)));
                };
                store_register(inst.d, { word(inst.x), word(inst.y) });
                break;
            }
            store_register(inst.d, { constant(inst.imm_a), constant(inst.imm_b) });
            break;
        case KOp::copy: {
            auto value = load_register(inst.x);
            if (weak_mode() && tracked(inst.x) && (mode_of(at) & detail::u_aware) == 0U) {
                // A Logic4 destination reads '-' (the word Z) as X
                // (kernel_word::coerce_weak).
                value.a = builder_.CreateOr(value.a,
                    builder_.CreateAnd(weak_of(inst.x), value.b));
            }
            store_register(inst.d, value);
            break;
        }
        case KOp::load_slot:
            store_register(inst.d,
                { builder_.CreateLoad(i64_, slot_word(inst.x, 0U)),
                    builder_.CreateLoad(i64_, slot_word(inst.x, 1U)) });
            break;
        case KOp::binary:
            store_register(inst.d,
                emit_binary(inst, load_register(inst.x), load_register(inst.y)));
            break;
        case KOp::reduce:
            store_register(inst.d, emit_reduce(inst, load_register(inst.x)));
            break;
        case KOp::unary_not: {
            const auto value = load_register(inst.x);
            auto* m = constant(mask_of(inst.width));
            store_register(inst.d,
                { builder_.CreateAnd(builder_.CreateOr(bit_not(value.a), value.b), m),
                    builder_.CreateAnd(value.b, m) });
            break;
        }
        case KOp::logical_not: {
            const auto [one, unknown] = truth(load_register(inst.x));
            // logic_not: one -> 0, zero -> 1, unknown -> x.
            auto* zero = builder_.CreateNot(builder_.CreateOr(one, unknown));
            store_register(inst.d, from_truth(zero, unknown));
            break;
        }
        case KOp::logical_binary: {
            const auto [left_one, left_unknown] = truth(load_register(inst.x));
            const auto [right_one, right_unknown] = truth(load_register(inst.y));
            auto* left_zero = builder_.CreateNot(builder_.CreateOr(left_one, left_unknown));
            auto* right_zero
                = builder_.CreateNot(builder_.CreateOr(right_one, right_unknown));
            llvm::Value* one { };
            llvm::Value* zero { };
            if (static_cast<LogicalBinaryOperator>(inst.sub)
                == LogicalBinaryOperator::logical_and) {
                zero = builder_.CreateOr(left_zero, right_zero);
                one = builder_.CreateAnd(left_one, right_one);
            } else {
                one = builder_.CreateOr(left_one, right_one);
                zero = builder_.CreateAnd(left_zero, right_zero);
            }
            auto* unknown = builder_.CreateNot(builder_.CreateOr(one, zero));
            store_register(inst.d, from_truth(one, unknown));
            break;
        }
        case KOp::extract: {
            if (inst.sub == 2U) {
                store_register(inst.d,
                    call_evaluate(at, zero_pair(), zero_pair(), zero_pair()));
                break;
            }
            if (inst.sub == 3U) {
                const auto [value, unknown] = wide_field(inst.x,
                    static_cast<std::uint32_t>(inst.imm_b), inst.imm_a != 0U,
                    inst.offset, inst.width);
                if (two_state_ && inst.imm_a != 0U) {
                    // Two-state code holds no 'U' (the full code redoes the
                    // instruction).
                    read_or_hand_over(builder_.CreateICmpEQ(unknown, constant(0U)));
                }
                last_unknown_ = unknown;
                last_weak_ = weak_mode() && inst.imm_a != 0U
                    ? wide_weak_field(inst.x, static_cast<std::uint32_t>(inst.imm_b),
                          inst.offset, inst.width)
                    : nullptr;
                store_register(inst.d, value);
                break;
            }
            const auto value = load_register(inst.x);
            auto* m = constant(mask_of(inst.width));
            auto* shift = constant(inst.offset);
            store_register(inst.d,
                { builder_.CreateAnd(builder_.CreateLShr(value.a, shift), m),
                    builder_.CreateAnd(builder_.CreateLShr(value.b, shift), m) });
            break;
        }
        case KOp::insert: {
            const auto target = load_register(inst.x);
            const auto source = load_register(inst.y);
            store_register(inst.d, insert(target, source, inst.offset, inst.width));
            break;
        }
        case KOp::concat: {
            Pair result = zero_pair();
            std::uint32_t offset = 0U;
            for (std::uint32_t index = inst.x; index-- > 0U;) {
                const auto& operand = body_.concat[inst.aux + index];
                result = insert(result, load_register(operand.reg), offset,
                    operand.width);
                offset += operand.width;
            }
            store_register(inst.d, result);
            break;
        }
        case KOp::conditional: {
            const auto condition = load_register(inst.x);
            const auto when_true = load_register(inst.y);
            const auto when_false = load_register(inst.z);
            auto* a0 = builder_.CreateTrunc(condition.a, llvm::Type::getInt1Ty(context_));
            auto* b0 = builder_.CreateTrunc(condition.b, llvm::Type::getInt1Ty(context_));
            auto* is_one = builder_.CreateAnd(a0, builder_.CreateNot(b0));
            auto* is_zero = builder_.CreateAnd(builder_.CreateNot(a0),
                builder_.CreateNot(b0));
            auto* m = constant(mask_of(inst.width));
            // Only equal known bits survive an ambiguous condition; equal X
            // or Z bits produce X (IEEE 1800-2023 11.4.11, Table 11-20).
            auto* same = builder_.CreateAnd(
                bit_not(builder_.CreateOr(builder_.CreateXor(when_true.a, when_false.a),
                    builder_.CreateOr(when_true.b, when_false.b))),
                m);
            auto* differ = builder_.CreateAnd(bit_not(same), m);
            const Pair merged { builder_.CreateOr(builder_.CreateAnd(when_true.a, same),
                                    differ),
                builder_.CreateOr(builder_.CreateAnd(when_true.b, same), differ) };
            store_register(inst.d,
                select(is_one, when_true, select(is_zero, when_false, merged)));
            break;
        }
        case KOp::two_state: {
            const auto value = load_register(inst.x);
            store_register(inst.d,
                { builder_.CreateAnd(value.a, bit_not(value.b)), constant(0U) });
            break;
        }
        case KOp::load_host:
            store_register(inst.d,
                call_evaluate(at, zero_pair(), zero_pair(), zero_pair(), 3U));
            break;
        case KOp::shift: {
            // The 'U' and weak masks shift as words of their own, inline like
            // the value (an unknown amount gives all X: no 'U' or weak
            // element).
            auto* weak_in = weak_mode() && tracked(inst.x) ? weak_of(inst.x) : nullptr;
            auto* unknown_in = !two_state_ && tracked(inst.x) ? unknown_of(inst.x)
                                                              : nullptr;
            std::optional<Pair> moved;
            std::optional<Pair> moved_unknown;
            if (weak_in != nullptr) {
                moved = emit_shift(inst, at, Pair { weak_in, constant(0U) },
                    load_register(inst.y));
            }
            if (unknown_in != nullptr) {
                moved_unknown = emit_shift(inst, at, Pair { unknown_in, constant(0U) },
                    load_register(inst.y));
            }
            store_register(inst.d,
                emit_shift(inst, at, load_register(inst.x), load_register(inst.y)));
            const auto cleaned = [&](const Pair& word) {
                return builder_.CreateAnd(word.a,
                    builder_.CreateAnd(bit_not(word.b), constant(mask_of(inst.width))));
            };
            if (moved || moved_unknown) {
                last_unknown_ = moved_unknown ? cleaned(*moved_unknown) : constant(0U);
                last_weak_ = moved ? cleaned(*moved) : constant(0U);
            }
            break;
        }
        case KOp::dynamic_extract:
            if (inst.sub == 3U) {
                store_register(inst.d, emit_wide_dynamic_extract(inst, at));
            } else if (inst.sub == 1U) {
                store_register(inst.d, emit_slot_extract(inst, at));
            } else if (inst.sub != 0U) {
                store_register(inst.d,
                    call_evaluate(at, zero_pair(), load_register(inst.y), zero_pair()));
            } else {
                store_register(inst.d, emit_dynamic_extract(inst, at,
                                           load_register(inst.x), load_register(inst.y)));
            }
            break;
        case KOp::dynamic_part_select:
            if (inst.sub == 3U) {
                store_register(inst.d, emit_wide_part_select(inst, at));
            } else if (inst.sub == 1U) {
                store_register(inst.d, emit_slot_part_select(inst, at));
            } else if (inst.sub != 0U) {
                store_register(inst.d,
                    call_evaluate(at, zero_pair(), load_register(inst.y), zero_pair()));
            } else {
                store_register(inst.d, emit_dynamic_part_select(inst, at,
                                           load_register(inst.x), load_register(inst.y)));
            }
            break;
        case KOp::load_field: {
            if (inst.sub == 1U) {
                // The bit offset is a per-instance binding.
                auto* offset = binding(inst.y, 0U);
                store_register(inst.d, { bound_field_plane(inst, offset, 0U),
                                           bound_field_plane(inst, offset, inst.imm_a) });
                break;
            }
            const auto word = inst.offset / 64U;
            const auto shift = inst.offset % 64U;
            const auto plane = [&](const std::uint64_t first) {
                auto* low = builder_.CreateLoad(i64_,
                    slot_word(inst.x, first + word));
                llvm::Value* value = builder_.CreateLShr(low, constant(shift));
                if (shift != 0U && shift + inst.width > 64U
                    && word + 1U < inst.imm_a) {
                    auto* high = builder_.CreateLoad(i64_,
                        slot_word(inst.x, first + word + 1U));
                    value = builder_.CreateOr(value,
                        builder_.CreateShl(high, constant(64U - shift)));
                }
                return builder_.CreateAnd(value, constant(mask_of(inst.width)));
            };
            store_register(inst.d, { plane(0U), plane(inst.imm_a) });
            break;
        }
        case KOp::dynamic_insert:
            store_register(inst.d,
                emit_dynamic_insert(inst, at, load_register(inst.x),
                    load_register(inst.y), load_register(inst.z)));
            insert_weak_ = weak_insert(inst, at);
            break;
        case KOp::dynamic_part_insert:
            store_register(inst.d,
                emit_dynamic_part_insert(inst, at, load_register(inst.x),
                    load_register(inst.y), load_register(inst.z)));
            insert_weak_ = weak_insert(inst, at);
            break;
        case KOp::mem_read:
            if (inst.sub == 1U) {
                emit_wide_mem_read(inst, at);
                break;
            }
            store_register(inst.d, emit_mem_read(inst, at));
            break;
        case KOp::jump:
            builder_.CreateBr(blocks_[std::min<std::size_t>(inst.d, body_.code.size())]);
            break;
        case KOp::branch: {
            const auto condition = inst.sub == 2U
                ? call_evaluate(at, zero_pair(), zero_pair(), zero_pair())
                : load_register(inst.x);
            auto* a0 = builder_.CreateTrunc(condition.a, llvm::Type::getInt1Ty(context_));
            auto* b0 = builder_.CreateTrunc(condition.b, llvm::Type::getInt1Ty(context_));
            auto* when_true = blocks_[std::min<std::size_t>(inst.y, body_.code.size())];
            auto* when_false = blocks_[std::min<std::size_t>(inst.z, body_.code.size())];
            if (folded(b0, false)) {
                // A known condition: no X check.
                if (llvm::isa<llvm::ConstantInt>(a0)) {
                    builder_.CreateBr(folded(a0, true) ? when_true : when_false);
                } else {
                    builder_.CreateCondBr(a0, when_true, when_false);
                }
                break;
            }
            auto* known = llvm::BasicBlock::Create(context_, "known", &function_);
            auto* unknown = llvm::BasicBlock::Create(context_, "unknown", &function_);
            builder_.CreateCondBr(b0, unknown, known);
            builder_.SetInsertPoint(known);
            builder_.CreateCondBr(a0, when_true, when_false);
            builder_.SetInsertPoint(unknown);
            if ((inst.flags & detail::flag_linear) != 0U) {
                builder_.CreateBr(when_false);
            } else {
                auto* type = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(context_), { ptr_, i32_, i32_ }, false);
                auto* callee = helper_symbol("fsim_sk_fail", type);
                builder_.CreateCall(type, callee,
                    { frame_, llvm::ConstantInt::get(i32_, at),
                        llvm::ConstantInt::get(i32_, 1U) });
                builder_.CreateBr(failure_block_);
            }
            break;
        }
        case KOp::store_slot: {
            auto value = load_register(inst.x);
            if (weak_mode() && tracked(inst.x) && inst.sub < 2U) {
                // A four-state slot reads a weak '-' (Z shape) or 'W' as X
                // (kernel_word::coerce_weak); 'U' is already X in the word.
                value.a = builder_.CreateOr(value.a,
                    builder_.CreateAnd(weak_of(inst.x), value.b));
            }
            if (inst.sub >= 2U) {
                emit_store_net(inst, load_register(inst.x));
            } else if (inst.sub == 1U) {
                emit_store_wide_slot(inst, value);
            } else {
                emit_store_slot(inst, value);
            }
            break;
        }
        case KOp::store_slot_nba:
            // Appended to the kernel's ordered nonblocking queue.
            emit_deferred_store(inst, at);
            break;
        case KOp::store_host:
            // z carries the value's weak mask (a VHDL host signal holds the
            // nine values).
            // A dynamic index or part base (aux) is register y.
            call_effect(at, load_register(inst.x),
                inst.aux != 0U ? load_register(inst.y) : zero_pair(),
                tracked(inst.x) ? masks_of(inst.x) : zero_pair(), 3U);
            break;
        case KOp::store_slot_dynamic:
            emit_store_slot_dynamic(inst, at);
            break;
        case KOp::store_slot_part:
            emit_store_slot_part(inst, at);
            break;
        case KOp::mem_write:
            if (inst.sub == 1U) {
                emit_wide_mem_write(inst, at);
                break;
            }
            emit_mem_write(inst, at);
            break;
        case KOp::mem_copy:
            // The runtime copies the arena words of the two containers.
            call_effect(at, zero_pair(), zero_pair(), zero_pair());
            break;
        case KOp::wide_move:
            // compile_body fuses only moves to an unresolved slot with a
            // one-field selection; the rest stay wide_load, wide_alu and
            // wide_store.
            if (!moves_inline(inst)) {
                throw std::logic_error("static kernel wide move has no inline form");
            }
            emit_wide_move(inst, at);
            break;
        case KOp::wide_copy:
            if (loops_chunks(64U * std::uint64_t { inst.aux })
                || loops_chunks(64U * inst.imm_a)) {
                emit_wide_copy_words(inst);
                break;
            }
            for (std::uint32_t index = 0U; index < inst.aux; ++index) {
                llvm::Value* a = builder_.CreateLoad(i64_, chunk_address(inst.x + index, 0U));
                auto* b = builder_.CreateLoad(i64_, chunk_address(inst.x + index, 1U));
                if (weak_mode() && inst.sub == 1U) {
                    // A Logic4 destination reads the source's weak elements
                    // as kernel_word::coerce_weak does ('W' and '-' as X);
                    // the source's masks follow its chunks.
                    auto* weak = builder_.CreateLoad(i64_,
                        chunk_address(inst.x + inst.aux + index, 1U));
                    a = builder_.CreateOr(a, builder_.CreateAnd(weak, b));
                }
                builder_.CreateStore(a, chunk_address(inst.d + index, 0U));
                builder_.CreateStore(b, chunk_address(inst.d + index, 1U));
            }
            for (std::uint64_t index = 0U; index < inst.imm_a; ++index) {
                const auto reg = inst.d + inst.aux + static_cast<std::uint32_t>(index);
                builder_.CreateStore(constant(0U), chunk_address(reg, 0U));
                builder_.CreateStore(constant(0U), chunk_address(reg, 1U));
            }
            break;
        case KOp::wide_const:
            if (loops_chunks(64U * std::uint64_t { inst.aux })) {
                emit_wide_const_words(inst);
                break;
            }
            for (std::uint32_t index = 0U; index < inst.aux; ++index) {
                const auto& word = body_.wide_constants[inst.imm_a + index];
                builder_.CreateStore(constant(word.a), chunk_address(inst.d + index, 0U));
                builder_.CreateStore(constant(word.b), chunk_address(inst.d + index, 1U));
            }
            break;
        case KOp::wide_load:
            if (inst.sub == 3U) {
                // A host signal without a mirror: the host owns its storage,
                // so its chunks come to the register file through the host
                // boundary call (execute_wide), as load_host's word does.
                (void)call_wide(at, zero_pair(), zero_pair(), zero_pair(), 3U);
                break;
            }
            emit_wide_load(inst);
            break;
        case KOp::wide_store:
            if (inst.sub == 3U) {
                // A host signal's write: the host applies it (write_host),
                // as store_host does for a narrow value; a dynamic index or
                // part base is register y.
                (void)call_wide(at, zero_pair(),
                    inst.imm_a != 0U ? load_register(inst.y) : zero_pair(),
                    zero_pair(), 3U);
                break;
            }
            if (inst.imm_a != 0U) {
                emit_wide_net_store(inst, at);
                break;
            }
            emit_wide_slot_store(inst, at);
            break;
        case KOp::wide_concat:
            emit_wide_concat(inst);
            break;
        case KOp::wide_alu:
            emit_wide_alu(inst, at);
            break;
        case KOp::wide_part_insert:
            emit_wide_part_insert(inst, at);
            break;
        case KOp::load_slot9: {
            // Strong 0/1 elements: p1 set, p2 and p3 clear, p0 the value.
            const auto plane = [&](const std::uint64_t index) {
                return builder_.CreateLoad(i64_, slot_word(inst.x, index));
            };
            auto* m = constant(mask_of(inst.width));
            auto* p0 = plane(0U);
            auto* p1 = plane(1U);
            if (inst.sub == 1U) {
                // A Logic4 destination (a SystemVerilog member's register)
                // reads the codes coerced (kernel_word::logic9_coerced): 'U',
                // 'W' and '-' as X, 'L' as 0, 'H' as 1 (IEEE 1800-2023 6.6).
                const auto inv = [&](llvm::Value* value) {
                    return builder_.CreateAnd(builder_.CreateNot(value), m);
                };
                auto* q2 = plane(2U);
                auto* q3 = plane(3U);
                auto* not_p3 = inv(q3);
                auto* zero = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), inv(p0));
                auto* one = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), p0);
                auto* high_z = builder_.CreateAnd(builder_.CreateAnd(not_p3, q2),
                    builder_.CreateAnd(inv(p1), inv(p0)));
                auto* odd = inv(builder_.CreateOr(builder_.CreateOr(zero, one), high_z));
                store_register(inst.d, Pair { builder_.CreateOr(one, odd),
                                           builder_.CreateOr(high_z, odd) });
                last_unknown_ = constant(0U);
                break;
            }
            auto* upper = builder_.CreateOr(plane(2U), plane(3U));
            auto* ok = builder_.CreateAnd(
                builder_.CreateICmpEQ(builder_.CreateAnd(upper, m), constant(0U)),
                builder_.CreateICmpEQ(builder_.CreateAnd(p1, m), m));
            if (two_state_) {
                read_or_hand_over(ok);
                last_unknown_ = constant(0U);
                store_register(inst.d, Pair { builder_.CreateAnd(p0, m), constant(0U) });
                break;
            }
            // All nine codes inline (a design without weak sources has only
            // 0, 1, X, Z and 'U'; nine_chunk decodes those alike), so no
            // value shape takes the evaluate helper.
            store_nine(inst.d, p0, p1, plane(2U), plane(3U), m);
            break;
        }
        case KOp::load_field9: {
            // Strong 0/1 fields read inline; others take the helper.
            const auto plane = [&](const std::uint64_t index) {
                if (inst.z != 0U) {
                    // The bit offset is a per-instance binding (z - 1).
                    return bound_field_plane(inst, binding(inst.z - 1U, 0U),
                        index * inst.imm_a);
                }
                const auto word = inst.offset / 64U;
                const auto shift = inst.offset % 64U;
                const auto first = index * inst.imm_a + word;
                auto* low = builder_.CreateLoad(i64_, slot_word(inst.x, first));
                llvm::Value* value = builder_.CreateLShr(low, constant(shift));
                if (shift != 0U && shift + inst.width > 64U
                    && word + 1U < inst.imm_a) {
                    auto* high = builder_.CreateLoad(i64_,
                        slot_word(inst.x, first + 1U));
                    value = builder_.CreateOr(value,
                        builder_.CreateShl(high, constant(64U - shift)));
                }
                return builder_.CreateAnd(value, constant(mask_of(inst.width)));
            };
            auto* m = constant(mask_of(inst.width));
            auto* p0 = plane(0U);
            auto* p1 = plane(1U);
            auto* p2 = plane(2U);
            auto* p3 = plane(3U);
            const auto inverted = [&](llvm::Value* value) {
                return builder_.CreateAnd(builder_.CreateNot(value), m);
            };
            if (inst.sub == 1U) {
                // Coerced to Logic4 (kernel_word::logic9_coerced): total.
                auto* not_p3 = inverted(p3);
                auto* zero = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), inverted(p0));
                auto* one = builder_.CreateAnd(builder_.CreateAnd(not_p3, p1), p0);
                auto* high_z = builder_.CreateAnd(builder_.CreateAnd(not_p3, p2),
                    builder_.CreateAnd(inverted(p1), inverted(p0)));
                auto* unknown = inverted(builder_.CreateOr(builder_.CreateOr(zero, one), high_z));
                store_register(inst.d, Pair { builder_.CreateOr(one, unknown),
                                           builder_.CreateOr(high_z, unknown) });
                last_unknown_ = constant(0U);
                break;
            }
            if (weak_mode()) {
                // Exact, all nine codes inline (store_nine).
                store_nine(inst.d, p0, p1, p2, p3, m);
                break;
            }
            // Exact (kernel_word::logic9_uword): 0, 1, X, Z and 'U' into the
            // tracked destination. A design without weak sources has no
            // other code (design_may_hold_weak).
            auto* u = inverted(builder_.CreateOr(builder_.CreateOr(p0, p1),
                builder_.CreateOr(p2, p3)));
            auto* q0 = builder_.CreateOr(p0, u);
            auto* x = builder_.CreateAnd(builder_.CreateAnd(inverted(p2), inverted(p1)), q0);
            auto* high_z = builder_.CreateAnd(builder_.CreateAnd(p2, inverted(p1)),
                inverted(q0));
            if (two_state_) {
                // Two-state code continues only with strong 0/1 values.
                read_or_hand_over(builder_.CreateICmpEQ(
                    builder_.CreateOr(builder_.CreateOr(x, high_z), u), constant(0U)));
                last_unknown_ = constant(0U);
                store_register(inst.d, Pair { q0, constant(0U) });
                break;
            }
            last_unknown_ = u;
            store_register(inst.d, Pair { q0, builder_.CreateOr(x, high_z) });
            break;
        }
        case KOp::store_vhdl:
            if (inst.sub == 2U && (inst.flags & detail::flag_blocking) == 0U) {
                emit_net_constant_store(inst, at, nullptr);
                break;
            }
            if (inst.sub == 0U && (inst.flags & detail::flag_blocking) == 0U) {
                // z - 1: a per-instance binding holding the bit offset.
                emit_deferred_store(inst, at,
                    inst.z != 0U ? binding(inst.z - 1U, 0U) : nullptr);
                break;
            }
            if ((inst.sub == 1U || inst.sub == 3U)
                && (inst.flags & detail::flag_blocking) == 0U && inst.imm_a != 0U) {
                emit_indexed_deferred_store(inst, at);
                break;
            }
            if ((inst.flags & detail::flag_blocking) != 0U && inst.sub <= 1U
                && !detail::net_shape(inst).has_value()
                && ((inst.sub == 0U ? inst.imm_b : inst.z) == 2U
                    || (inst.sub == 0U ? inst.imm_b : inst.z) == 4U)
                && inst.width <= 64U) {
                emit_vhdl_blocking_store(inst, at);
                break;
            }
            // z carries the value's 'U' and weak masks.
            call_effect(at, load_register(inst.x),
                inst.sub == 1U || inst.sub == 3U ? load_register(inst.y) : zero_pair(),
                masks_of(inst.x));
            break;
        case KOp::integer_binary:
            store_register(inst.d, emit_integer_binary(inst, at,
                                       load_register(inst.x), load_register(inst.y)));
            break;
        case KOp::integer_unary:
            store_register(inst.d, emit_integer_unary(inst, at, load_register(inst.x)));
            break;
        case KOp::integer_check:
            emit_integer_check(inst, at, load_register(inst.x));
            break;
        case KOp::assert_check: {
            const auto condition = load_register(inst.x);
            auto* one = builder_.CreateAnd(
                builder_.CreateTrunc(condition.a, llvm::Type::getInt1Ty(context_)),
                builder_.CreateNot(builder_.CreateTrunc(condition.b,
                    llvm::Type::getInt1Ty(context_))));
            emit_checked(one, at, condition, 4U);
            break;
        }
        case KOp::call:
            emit_call(inst, at);
            break;
        case KOp::ret:
            emit_return(inst, at);
            break;
        case KOp::frame_push:
            emit_frame_push(inst, at);
            break;
        case KOp::frame_pop:
            emit_frame_pop(inst, at);
            break;
        }
    }

    /// Continues when `ok`; otherwise the effect helper checks (and reports)
    /// the instruction on the reference path first.
    void emit_checked(llvm::Value* ok, const std::uint32_t at, const Pair x,
        const unsigned category = 2U)
    {
        if (folded(ok, true)) {
            return;
        }
        if (two_state_ && hand_over_slow_paths_) {
            // The full code checks (and reports) the instruction instead.
            read_or_hand_over(ok);
            return;
        }
        auto* slow = llvm::BasicBlock::Create(context_, "check", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "checked", &function_);
        builder_.CreateCondBr(ok, next, slow);
        builder_.SetInsertPoint(slow);
        // Only the reference's report of the failed check (a failure, or a library
        // report for an assert_check).
        call_effect(at, x, zero_pair(), zero_pair(), category);
        builder_.CreateBr(next);
        builder_.SetInsertPoint(next);
    }

    /// The most negative VHDL integer of `width` (32 or 64) bits as an i64.
    [[nodiscard]] llvm::Value* integer_minimum(const std::uint32_t width)
    {
        return constant(width == 64U
                ? std::uint64_t { 1 } << 63U
                : static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::min()));
    }

    /// Inline integer_unary_value (IEEE 1076-2019 9.2.6: "-" and "abs" on
    /// INTEGER): an operand with an unknown bit, or of a width other than the
    /// profile's 32 or 64, is an error, and so is the overflow of the most
    /// negative value. The errors are those of the reference operation.
    [[nodiscard]] Pair emit_integer_unary(const KInst& inst, const std::uint32_t at,
        const Pair x)
    {
        using runtime::simir::IntegerUnaryOperator;
        const auto width = inst.width;
        if (width != 32U && width != 64U) {
            check_or_fail(llvm::ConstantInt::getFalse(context_), at, 5U);
            return zero_pair();
        }
        auto* m = constant(mask_of(width));
        check_or_fail(builder_.CreateICmpEQ(builder_.CreateAnd(x.b, m), constant(0U)), at, 5U);
        auto* value = sign_extend(x.a, width);
        check_or_fail(builder_.CreateICmpNE(value, integer_minimum(width)), at, 6U);
        auto* negated = builder_.CreateSub(constant(0U), value);
        auto* result = static_cast<IntegerUnaryOperator>(inst.sub) == IntegerUnaryOperator::negate
            ? negated
            : builder_.CreateSelect(builder_.CreateICmpSLT(value, constant(0U)), negated,
                  value);
        last_unknown_ = constant(0U);
        return { builder_.CreateAnd(result, m), constant(0U) };
    }

    /// Inline integer_binary_value (IEEE 1076-2019 9.2.7, 9.2.8 on INTEGER):
    /// checked add, subtract, multiply, divide, rem, mod and "**" of the
    /// profile's 32- or 64-bit integers. An unknown operand, overflow,
    /// division by zero and a negative exponent are errors with the
    /// reference operation's messages.
    [[nodiscard]] Pair emit_integer_binary(const KInst& inst,
        const std::uint32_t at, const Pair x, const Pair y)
    {
        using runtime::simir::IntegerBinaryOperator;
        const auto operation = static_cast<IntegerBinaryOperator>(inst.sub);
        const auto width = inst.width;
        if (width != 32U && width != 64U) {
            check_or_fail(llvm::ConstantInt::getFalse(context_), at, 5U);
            return zero_pair();
        }
        const bool wide = width == 64U;
        auto* m = constant(mask_of(width));
        check_or_fail(builder_.CreateICmpEQ(
                          builder_.CreateAnd(builder_.CreateOr(x.b, y.b), m), constant(0U)),
            at, 5U);
        auto* left = sign_extend(x.a, width);
        auto* right = sign_extend(y.a, width);
        // A checked add (0), subtract (1) or multiply (2): the 64-bit result
        // and whether it left the integer range. 32-bit operands are
        // sign-extended, so their 64-bit result is exact.
        const auto checked = [&](const int kind, llvm::Value* a, llvm::Value* b)
            -> std::pair<llvm::Value*, llvm::Value*> {
            if (wide) {
                const auto id = kind == 0 ? llvm::Intrinsic::sadd_with_overflow
                    : kind == 1           ? llvm::Intrinsic::ssub_with_overflow
                                          : llvm::Intrinsic::smul_with_overflow;
                auto* call = builder_.CreateBinaryIntrinsic(id, a, b);
                return { builder_.CreateExtractValue(call, 0U),
                    builder_.CreateExtractValue(call, 1U) };
            }
            auto* plain = kind == 0 ? builder_.CreateAdd(a, b)
                : kind == 1         ? builder_.CreateSub(a, b)
                                    : builder_.CreateMul(a, b);
            return { plain, builder_.CreateICmpNE(plain, sign_extend(plain, 32U)) };
        };
        llvm::Value* result = nullptr;
        switch (operation) {
        case IntegerBinaryOperator::add:
        case IntegerBinaryOperator::subtract:
        case IntegerBinaryOperator::multiply: {
            const auto [value, overflow] = checked(
                operation == IntegerBinaryOperator::add        ? 0
                    : operation == IntegerBinaryOperator::subtract ? 1
                                                                   : 2,
                left, right);
            check_or_fail(builder_.CreateNot(overflow), at, 6U);
            result = value;
            break;
        }
        case IntegerBinaryOperator::power: {
            check_or_fail(builder_.CreateICmpSGE(right, constant(0U)), at, 8U);
            // result = left ** right by repeated squaring (while the
            // exponent has bits), the overflow of any step accumulating.
            auto* before = builder_.GetInsertBlock();
            auto* header = llvm::BasicBlock::Create(context_, "power", &function_);
            auto* step = llvm::BasicBlock::Create(context_, "power_step", &function_);
            auto* done = llvm::BasicBlock::Create(context_, "power_done", &function_);
            builder_.CreateBr(header);
            builder_.SetInsertPoint(header);
            auto* accumulated = loop_phi(i64_, constant(1U), before, step);
            auto* factor = loop_phi(i64_, left, before, step);
            auto* exponent = loop_phi(i64_, right, before, step);
            auto* failed = loop_phi(llvm::Type::getInt1Ty(context_),
                llvm::ConstantInt::getFalse(context_), before, step);
            builder_.CreateCondBr(nonzero(exponent), step, done);
            builder_.SetInsertPoint(step->getTerminator());
            auto* odd = nonzero(builder_.CreateAnd(exponent, constant(1U)));
            const auto [product, product_overflow] = checked(2, accumulated, factor);
            auto* next_exponent = builder_.CreateLShr(exponent, constant(1U));
            const auto [square, square_overflow] = checked(2, factor, factor);
            auto* next_failed = builder_.CreateOr(failed,
                builder_.CreateOr(builder_.CreateAnd(odd, product_overflow),
                    builder_.CreateAnd(nonzero(next_exponent), square_overflow)));
            close_loop_phi(accumulated, builder_.CreateSelect(odd, product, accumulated));
            close_loop_phi(factor, square);
            close_loop_phi(exponent, next_exponent);
            close_loop_phi(failed, next_failed);
            builder_.SetInsertPoint(done);
            check_or_fail(builder_.CreateNot(failed), at, 6U);
            result = accumulated;
            break;
        }
        case IntegerBinaryOperator::divide:
        case IntegerBinaryOperator::remainder:
        case IntegerBinaryOperator::modulo: {
            auto* zero = builder_.CreateICmpEQ(right, constant(0U));
            check_or_fail(builder_.CreateNot(zero), at, 7U);
            auto* overflow = builder_.CreateAnd(
                builder_.CreateICmpEQ(left, integer_minimum(width)),
                builder_.CreateICmpEQ(right, constant(~std::uint64_t { 0 })));
            check_or_fail(builder_.CreateNot(overflow), at, 6U);
            // Made safe: the errors above leave this path, but the
            // division is computed before the branch settles in two-state
            // code.
            auto* divisor = builder_.CreateSelect(builder_.CreateOr(zero, overflow),
                constant(1U), right);
            if (operation == IntegerBinaryOperator::divide) {
                result = builder_.CreateSDiv(left, divisor);
                break;
            }
            result = builder_.CreateSRem(left, divisor);
            if (operation == IntegerBinaryOperator::modulo) {
                // The result takes the divisor's sign (1076-2019 9.2.7).
                auto* adjust = builder_.CreateAnd(
                    builder_.CreateICmpNE(result, constant(0U)),
                    builder_.CreateICmpNE(
                        builder_.CreateICmpSLT(result, constant(0U)),
                        builder_.CreateICmpSLT(divisor, constant(0U))));
                result = builder_.CreateSelect(adjust,
                    builder_.CreateAdd(result, divisor), result);
            }
            break;
        }
        }
        last_unknown_ = constant(0U);
        return { builder_.CreateAnd(result, m), constant(0U) };
    }

    void emit_integer_check(const KInst& inst, const std::uint32_t at,
        const Pair x)
    {
        const auto width = inst.width;
        const auto lower = static_cast<std::int64_t>(inst.imm_a);
        auto* m = constant(mask_of(width));
        auto* known = builder_.CreateICmpEQ(builder_.CreateAnd(x.b, m),
            constant(0U));
        auto* bits = builder_.CreateAnd(x.a, m);
        llvm::Value* value = bits;
        if (width == 32U || width == 64U || lower < 0) {
            if (width < 64U) {
                auto* shift = constant(64U - width);
                value = builder_.CreateAShr(builder_.CreateShl(bits, shift), shift);
            }
        }
        auto* in_range = builder_.CreateAnd(
            builder_.CreateICmpSGE(value, constant(inst.imm_a)),
            builder_.CreateICmpSLE(value, constant(inst.imm_b)));
        emit_checked(builder_.CreateAnd(known, in_range), at, x);
    }

    [[nodiscard]] llvm::Value* register_word(llvm::Value* reg,
        const std::uint32_t plane)
    {
        auto* index = builder_.CreateAdd(builder_.CreateShl(reg, constant(1U)),
            constant(plane));
        return builder_.CreateInBoundsGEP(i64_, registers_, index);
    }

    void forget_registers(const std::uint32_t first, const std::uint32_t count)
    {
        for (std::uint32_t reg = first; reg < first + count; ++reg) {
            cache_.erase(reg);
        }
    }

    /// KOp::frame_push: the saved registers (frame_saves[aux]) and a header
    /// word pair (the identity) go on the body's growable frame stack, whose
    /// depth word x counts pairs. A full stack grows first (the one helper
    /// call a push makes, only at a capacity boundary). Every register is in
    /// the register file here (frame bodies keep no local registers).
    void emit_frame_push(const KInst& inst, const std::uint32_t at)
    {
        const auto& save = body_.frame_saves[inst.aux];
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(load_register(inst.x).a, m);
        auto* stack = builder_.CreateLoad(ptr_, frame_field(136U));
        auto* capacity = builder_.CreateLoad(i64_,
            builder_.CreateConstInBoundsGEP1_64(i8_, stack, 8U));
        auto* after = builder_.CreateAdd(depth, constant(save.pairs));
        auto* grow = llvm::BasicBlock::Create(context_, "frame_grow", &function_);
        auto* fast = llvm::BasicBlock::Create(context_, "frame_push", &function_);
        builder_.CreateCondBr(
            builder_.CreateICmpULE(builder_.CreateShl(after, constant(1U)), capacity),
            fast, grow);
        builder_.SetInsertPoint(grow);
        // Capacity growth: the runtime reserves room for the frame.
        call_effect(at, Pair { depth, constant(0U) },
            Pair { constant(save.pairs), constant(0U) },
            Pair { constant(1U), constant(0U) }, 1U);
        builder_.CreateBr(fast);
        builder_.SetInsertPoint(fast);
        auto* entries = builder_.CreateLoad(ptr_, stack);
        auto* base = builder_.CreateInBoundsGEP(i64_, entries,
            builder_.CreateShl(depth, constant(1U)));
        const auto word = [&](const std::uint64_t index) {
            return builder_.CreateInBoundsGEP(i64_, base, constant(index));
        };
        builder_.CreateStore(constant(save.identity), word(0U));
        builder_.CreateStore(constant(0U), word(1U));
        for (auto item = save.item_begin; item < save.item_end; ++item) {
            const auto& entry = body_.frame_items[item];
            for (std::uint32_t pair = 0U; pair < entry.count; ++pair) {
                for (std::uint32_t plane = 0U; plane < 2U; ++plane) {
                    builder_.CreateStore(
                        builder_.CreateLoad(i64_,
                            chunk_address(entry.index + pair, plane)),
                        word(2U * (1U + entry.offset + pair) + plane));
                }
            }
        }
        store_register(inst.x, Pair { after, constant(0U) });
    }

    /// KOp::frame_pop: the frame's registers return (those the call's
    /// results keep are not in the list) and the depth drops; a stack that
    /// holds no such frame is the reference's mismatch error.
    void emit_frame_pop(const KInst& inst, const std::uint32_t at)
    {
        const auto& save = body_.frame_saves[inst.aux];
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(load_register(inst.x).a, m);
        auto* stack = builder_.CreateLoad(ptr_, frame_field(136U));
        auto* entries = builder_.CreateLoad(ptr_, stack);
        auto* enough = builder_.CreateICmpUGE(depth, constant(save.pairs));
        auto* check = llvm::BasicBlock::Create(context_, "frame_check", &function_);
        auto* fast = llvm::BasicBlock::Create(context_, "frame_pop", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "frame_fail", &function_);
        builder_.CreateCondBr(enough, check, slow);
        builder_.SetInsertPoint(check);
        auto* top = builder_.CreateSub(depth, constant(save.pairs));
        auto* base = builder_.CreateInBoundsGEP(i64_, entries,
            builder_.CreateShl(top, constant(1U)));
        auto* header = builder_.CreateLoad(i64_, base);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(header, constant(save.identity)), fast, slow);
        builder_.SetInsertPoint(slow);
        call_effect(at, Pair { depth, constant(0U) }, zero_pair(), zero_pair(), 2U);
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(fast);
        for (auto item = save.item_begin; item < save.item_end; ++item) {
            const auto& entry = body_.frame_items[item];
            for (std::uint32_t pair = 0U; pair < entry.count; ++pair) {
                for (std::uint32_t plane = 0U; plane < 2U; ++plane) {
                    auto* source = builder_.CreateInBoundsGEP(i64_, base,
                        constant(2U * (1U + entry.offset + pair) + plane));
                    builder_.CreateStore(builder_.CreateLoad(i64_, source),
                        chunk_address(entry.index + pair, plane));
                }
            }
            forget_registers(entry.index, entry.count);
        }
        store_register(inst.x, Pair { top, constant(0U) });
    }

    /// A call on the body's growable stack (flag_linear): the return target
    /// goes to entries[depth] and the depth word is incremented; a full
    /// stack grows first (the one helper call a call makes, only at a
    /// capacity boundary).
    void emit_linear_call(const KInst& inst, const std::uint32_t at)
    {
        const auto pointer = load_register(inst.x);
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(pointer.a, m);
        auto* known = builder_.CreateICmpEQ(
            builder_.CreateAnd(pointer.b, m), constant(0U));
        auto* stack = builder_.CreateLoad(ptr_, frame_field(128U));
        auto* capacity = builder_.CreateLoad(i64_,
            builder_.CreateConstInBoundsGEP1_64(i8_, stack, 8U));
        auto* room = llvm::BasicBlock::Create(context_, "call_room", &function_);
        auto* grow = llvm::BasicBlock::Create(context_, "call_grow", &function_);
        auto* fast = llvm::BasicBlock::Create(context_, "call", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "call_fail", &function_);
        builder_.CreateCondBr(known, room, slow);
        builder_.SetInsertPoint(room);
        builder_.CreateCondBr(builder_.CreateICmpULT(depth, capacity), fast, grow);
        builder_.SetInsertPoint(grow);
        // Capacity growth: the runtime reserves depth + 1 entries.
        call_effect(at, pointer, zero_pair(), Pair { constant(1U), constant(0U) },
            1U);
        builder_.CreateBr(fast);
        builder_.SetInsertPoint(slow);
        call_effect(at, pointer, zero_pair(), zero_pair(), 2U);
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(fast);
        auto* entries = builder_.CreateLoad(ptr_, stack);
        builder_.CreateStore(constant(inst.imm_a),
            builder_.CreateInBoundsGEP(i64_, entries, depth));
        store_register(inst.x,
            Pair { builder_.CreateAdd(depth, constant(1U)), constant(0U) });
        builder_.CreateBr(blocks_[std::min<std::size_t>(inst.d, body_.code.size())]);
    }

    /// A return on the body's growable stack: the popped target selects the
    /// continuation.
    void emit_linear_return(const KInst& inst, const std::uint32_t at)
    {
        const auto pointer = load_register(inst.x);
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(pointer.a, m);
        auto* ok = builder_.CreateAnd(
            builder_.CreateICmpEQ(builder_.CreateAnd(pointer.b, m), constant(0U)),
            builder_.CreateICmpUGE(depth, constant(1U)));
        auto* fast = llvm::BasicBlock::Create(context_, "ret", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "ret_fail", &function_);
        builder_.CreateCondBr(ok, fast, slow);
        builder_.SetInsertPoint(fast);
        auto* stack = builder_.CreateLoad(ptr_, frame_field(128U));
        auto* entries = builder_.CreateLoad(ptr_, stack);
        auto* top = builder_.CreateSub(depth, constant(1U));
        auto* target = builder_.CreateLoad(i64_,
            builder_.CreateInBoundsGEP(i64_, entries, top));
        store_register(inst.x, Pair { top, constant(0U) });
        auto* targets = builder_.CreateSwitch(target, slow,
            static_cast<unsigned>(body_.return_targets.size()));
        for (const auto value : body_.return_targets) {
            targets->addCase(llvm::ConstantInt::get(
                                 llvm::cast<llvm::IntegerType>(i64_), value),
                blocks_[std::min<std::size_t>(value, body_.code.size())]);
        }
        builder_.SetInsertPoint(slow);
        // The helper reports the reference failure for this return.
        call_effect(at, pointer, zero_pair(), zero_pair(), 2U);
        builder_.CreateBr(failure_block_);
    }

    void emit_call(const KInst& inst, const std::uint32_t at)
    {
        if (inst.z == 0U) {
            // The helper pushes onto the member's runtime call stack.
            call_effect(at, zero_pair(), zero_pair(), zero_pair());
            builder_.CreateBr(
                blocks_[std::min<std::size_t>(inst.d, body_.code.size())]);
            return;
        }
        if ((inst.flags & detail::flag_linear) != 0U) {
            emit_linear_call(inst, at);
            return;
        }
        const auto pointer = load_register(inst.x);
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(pointer.a, m);
        auto* ok = builder_.CreateAnd(
            builder_.CreateICmpEQ(builder_.CreateAnd(pointer.b, m), constant(0U)),
            builder_.CreateICmpULT(depth, constant(inst.z)));
        auto* fast = llvm::BasicBlock::Create(context_, "call", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "call_fail", &function_);
        builder_.CreateCondBr(ok, fast, slow);
        builder_.SetInsertPoint(slow);
        call_effect(at, pointer, zero_pair(), zero_pair(), 2U);
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(fast);
        auto* entry = builder_.CreateAdd(depth, constant(inst.y));
        builder_.CreateStore(constant(inst.imm_a), register_word(entry, 0U));
        builder_.CreateStore(constant(0U), register_word(entry, 1U));
        forget_registers(inst.y, inst.z);
        store_register(inst.x,
            Pair { builder_.CreateAdd(depth, constant(1U)), constant(0U) });
        builder_.CreateBr(blocks_[std::min<std::size_t>(inst.d, body_.code.size())]);
    }

    void emit_return(const KInst& inst, const std::uint32_t at)
    {
        if (inst.z == 0U) {
            // The helper pops the member's runtime call stack.
            const auto target = call_evaluate(at, zero_pair(), zero_pair(),
                zero_pair());
            auto* invalid = llvm::BasicBlock::Create(context_, "ret_invalid",
                &function_);
            auto* targets = builder_.CreateSwitch(target.a, invalid,
                static_cast<unsigned>(body_.return_targets.size()));
            for (const auto value : body_.return_targets) {
                targets->addCase(llvm::ConstantInt::get(
                                     llvm::cast<llvm::IntegerType>(i64_), value),
                    blocks_[std::min<std::size_t>(value, body_.code.size())]);
            }
            builder_.SetInsertPoint(invalid);
            call_effect(at, Pair { constant(1U), constant(0U) }, zero_pair(),
                zero_pair(), 2U);
            builder_.CreateBr(failure_block_);
            return;
        }
        if ((inst.flags & detail::flag_linear) != 0U) {
            emit_linear_return(inst, at);
            return;
        }
        const auto pointer = load_register(inst.x);
        auto* m = constant(mask_of(32U));
        auto* depth = builder_.CreateAnd(pointer.a, m);
        auto* ok = builder_.CreateAnd(
            builder_.CreateICmpEQ(builder_.CreateAnd(pointer.b, m), constant(0U)),
            builder_.CreateAnd(builder_.CreateICmpUGE(depth, constant(1U)),
                builder_.CreateICmpULE(depth, constant(inst.z))));
        auto* fast = llvm::BasicBlock::Create(context_, "ret", &function_);
        auto* slow = llvm::BasicBlock::Create(context_, "ret_fail", &function_);
        builder_.CreateCondBr(ok, fast, slow);
        builder_.SetInsertPoint(fast);
        auto* entry = builder_.CreateAdd(
            builder_.CreateSub(depth, constant(1U)), constant(inst.y));
        auto* target = builder_.CreateLoad(i64_, register_word(entry, 0U));
        auto* target_b = builder_.CreateLoad(i64_, register_word(entry, 1U));
        auto* dispatch = llvm::BasicBlock::Create(context_, "ret_dispatch",
            &function_);
        builder_.CreateCondBr(builder_.CreateICmpEQ(target_b, constant(0U)),
            dispatch, slow);
        builder_.SetInsertPoint(dispatch);
        store_register(inst.x,
            Pair { builder_.CreateSub(depth, constant(1U)), constant(0U) });
        auto* targets = builder_.CreateSwitch(target, slow,
            static_cast<unsigned>(body_.return_targets.size()));
        for (const auto value : body_.return_targets) {
            targets->addCase(llvm::ConstantInt::get(
                                llvm::cast<llvm::IntegerType>(i64_), value),
                blocks_[std::min<std::size_t>(value, body_.code.size())]);
        }
        builder_.SetInsertPoint(slow);
        // The helper reports the reference failure for this return.
        call_effect(at, pointer, zero_pair(), zero_pair(), 2U);
        builder_.CreateBr(failure_block_);
    }

    [[nodiscard]] Pair insert(const Pair target, const Pair source,
        const std::uint32_t offset, const std::uint32_t width)
    {
        const auto field = mask_of(width) << offset;
        auto* keep = constant(~field);
        auto* place = constant(field);
        auto* shift = constant(offset);
        return { builder_.CreateOr(builder_.CreateAnd(target.a, keep),
                     builder_.CreateAnd(builder_.CreateShl(source.a, shift), place)),
            builder_.CreateOr(builder_.CreateAnd(target.b, keep),
                builder_.CreateAnd(builder_.CreateShl(source.b, shift), place)) };
    }

    /// A narrow field [offset, offset + width) of a wide slot with imm_a
    /// words per plane, as StaticKernel::write_slot_word stores it. With a
    /// `chunk` (a value: a word loop's chunk of a wide store) the field is at
    /// offset + 64 chunk, 64 bits wide unless the chunk is a constant.
    void emit_store_wide_slot(const KInst& inst, const Pair value,
        llvm::Value* chunk = nullptr)
    {
        if (const auto* fixed = llvm::dyn_cast_or_null<llvm::ConstantInt>(chunk)) {
            auto field = inst;
            field.offset += 64U * static_cast<std::uint32_t>(fixed->getZExtValue());
            emit_store_wide_slot(field, value);
            return;
        }
        auto* info = binding(inst.d, 1U);
        const auto words = inst.imm_a;
        const auto word = inst.offset / 64U;
        const auto shift = inst.offset % 64U;
        // A full chunk inside the slot that does not start at a word
        // boundary always reaches into the next word.
        const bool spans = shift != 0U && shift + inst.width > 64U
            && (chunk != nullptr || word + 1U < words);
        auto* m = constant(mask_of(inst.width));
        const auto address = [&](const std::uint64_t plane,
                                 const std::uint64_t index) {
            return chunk != nullptr ? slot_word_at(inst.d, plane * words + index, chunk)
                                    : slot_word(inst.d, plane * words + index);
        };
        struct Plane {
            llvm::Value* low { };
            llvm::Value* high { };
            llvm::Value* field { };
        };
        const auto read = [&](const std::uint64_t plane) {
            Plane result;
            result.low = builder_.CreateLoad(i64_, address(plane, word));
            llvm::Value* field = builder_.CreateLShr(result.low, constant(shift));
            if (spans) {
                result.high = builder_.CreateLoad(i64_, address(plane, word + 1U));
                field = builder_.CreateOr(field,
                    builder_.CreateShl(result.high, constant(64U - shift)));
            }
            result.field = builder_.CreateAnd(field, m);
            return result;
        };
        const auto old_a = read(0U);
        const auto old_b = read(1U);
        auto* next_a = builder_.CreateAnd(value.a, m);
        auto* next_b = builder_.CreateAnd(value.b, m);
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* changed = builder_.CreateOr(builder_.CreateXor(old_a.field, next_a),
            builder_.CreateXor(old_b.field, next_b));
        auto* write = llvm::BasicBlock::Create(context_, "write_wide", &function_);
        auto* notify = llvm::BasicBlock::Create(context_, "notify_wide", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored_wide", &function_);
        builder_.CreateCondBr(nonzero(changed), write, after);
        builder_.SetInsertPoint(write);
        const auto store = [&](const std::uint64_t plane, const Plane& old,
                               llvm::Value* next) {
            auto* low_mask = constant(~(mask_of(inst.width) << shift));
            builder_.CreateStore(builder_.CreateOr(builder_.CreateAnd(old.low, low_mask),
                                     builder_.CreateShl(next, constant(shift))),
                address(plane, word));
            if (spans) {
                const auto spill = 64U - shift;
                auto* high_mask = constant(~(mask_of(inst.width) >> spill));
                builder_.CreateStore(
                    builder_.CreateOr(builder_.CreateAnd(old.high, high_mask),
                        builder_.CreateLShr(next, constant(spill))),
                    address(plane, word + 1U));
            }
        };
        store(0U, old_a, next_a);
        store(1U, old_b, next_b);
        builder_.CreateCondBr(silent, after, notify);
        builder_.SetInsertPoint(notify);
        auto* slot = builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit));
        emit_notify_skip(slot, after);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_, i32_ }, false);
        auto* callee = helper_symbol("fsim_sk_notify_field", type);
        builder_.CreateCall(type, callee,
            { frame_, slot, changed,
                chunk != nullptr ? builder_.CreateTrunc(chunk_bit(inst.offset, chunk), i32_)
                                 : llvm::ConstantInt::get(i32_, inst.offset) });
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    /// A field [offset, offset + width) of a resolved slot (store_slot sub 2
    /// and 3; imm_a words per plane, imm_b the source count and from bit 32
    /// the member's source, aux the slot's width): the member's source takes
    /// the field's codes, and the words the field touches resolve over every
    /// source as StaticKernel::resolve_net_words resolves them.
    void emit_store_net(const KInst& inst, const Pair value)
    {
        emit_store_net(inst, value, inst.imm_a, inst.aux, inst.offset, inst.width);
    }

    /// emit_store_net's field [offset, offset + width) of the slot bound at
    /// inst.d, with `words` words per plane and `slot_width` bits; imm_b as
    /// for store_slot.
    void emit_store_net(const KInst& inst, const Pair value, const std::uint64_t words,
        const std::uint32_t slot_width, const std::uint32_t offset,
        const std::uint32_t width)
    {
        auto* info = binding(inst.d, 1U);
        const auto shape = detail::make_net_shape(static_cast<std::uint32_t>(inst.imm_b),
            static_cast<std::uint32_t>(inst.imm_b >> 32U), slot_width);
        const auto sources = shape.sources;
        const auto own = shape.own;
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        const bool spans = shift != 0U && shift + width > 64U && word + 1U < words;
        auto* m = constant(mask_of(width));
        // The field's codes: 0 0010, 1 0011, x 0001, z 0100.
        auto* a = builder_.CreateAnd(value.a, m);
        auto* b = builder_.CreateAnd(value.b, m);
        const std::array<llvm::Value*, 3> codes { a,
            builder_.CreateAnd(builder_.CreateNot(b), m),
            builder_.CreateAnd(b, builder_.CreateNot(a)) };
        const auto source_word = [&](const std::uint64_t source,
                                     const std::uint64_t plane, const std::uint64_t index) {
            return slot_word(inst.d, (2U + 4U * source + plane) * words + index);
        };
        for (std::uint64_t plane = 0U; plane < 4U; ++plane) {
            auto* bits = plane < 3U ? codes[plane] : constant(0U);
            auto* low = source_word(own, plane, word);
            builder_.CreateStore(
                builder_.CreateOr(builder_.CreateAnd(builder_.CreateLoad(i64_, low),
                                      constant(~(mask_of(width) << shift))),
                    builder_.CreateShl(bits, constant(shift))),
                low);
            if (spans) {
                const auto spill = 64U - shift;
                auto* high = source_word(own, plane, word + 1U);
                builder_.CreateStore(
                    builder_.CreateOr(builder_.CreateAnd(builder_.CreateLoad(i64_, high),
                                          constant(~(mask_of(width) >> spill))),
                        builder_.CreateLShr(bits, constant(spill))),
                    high);
            }
        }
        // Resolution; the changes, as bits of the field (wide) or the word.
        llvm::Value* changed = constant(0U);
        for (std::uint64_t index = word; index <= word + (spans ? 1U : 0U); ++index) {
            llvm::Value* strong0 = constant(0U);
            llvm::Value* strong1 = constant(0U);
            llvm::Value* weak0 = constant(0U);
            llvm::Value* weak1 = constant(0U);
            for (std::uint64_t source = 0U; source < sources; ++source) {
                auto* p0 = builder_.CreateLoad(i64_, source_word(source, 0U, index));
                auto* p1 = builder_.CreateLoad(i64_, source_word(source, 1U, index));
                auto* p2 = builder_.CreateLoad(i64_, source_word(source, 2U, index));
                auto* p3 = builder_.CreateLoad(i64_, source_word(source, 3U, index));
                auto* not2 = builder_.CreateNot(p2);
                auto* not3 = builder_.CreateNot(p3);
                auto* unknown = builder_.CreateOr(p3,
                    builder_.CreateAnd(not2, builder_.CreateNot(p1)));
                auto* known = builder_.CreateAnd(builder_.CreateAnd(not3, not2), p1);
                auto* weak = builder_.CreateAnd(builder_.CreateAnd(not3, p2),
                    builder_.CreateOr(p1, p0));
                strong0 = builder_.CreateOr(strong0, builder_.CreateOr(unknown,
                    builder_.CreateAnd(known, builder_.CreateNot(p0))));
                strong1 = builder_.CreateOr(strong1, builder_.CreateOr(unknown,
                    builder_.CreateAnd(known, p0)));
                weak0 = builder_.CreateOr(weak0, builder_.CreateAnd(weak,
                    builder_.CreateNot(builder_.CreateAnd(p1, p0))));
                weak1 = builder_.CreateOr(weak1, builder_.CreateAnd(weak, p0));
            }
            auto* free = builder_.CreateNot(builder_.CreateOr(strong0, strong1));
            auto* zero = builder_.CreateOr(strong0, builder_.CreateAnd(free, weak0));
            auto* one = builder_.CreateOr(strong1, builder_.CreateAnd(free, weak1));
            const auto bits = slot_width - 64U * static_cast<std::uint32_t>(index);
            auto* word_mask = constant(bits >= 64U ? ~std::uint64_t { 0 } : mask_of(bits));
            auto* next_a = builder_.CreateAnd(one, word_mask);
            auto* next_b = builder_.CreateAnd(
                builder_.CreateNot(builder_.CreateXor(zero, one)), word_mask);
            auto* address_a = slot_word(inst.d, index);
            auto* address_b = slot_word(inst.d, words + index);
            auto* diff = builder_.CreateOr(
                builder_.CreateXor(builder_.CreateLoad(i64_, address_a), next_a),
                builder_.CreateXor(builder_.CreateLoad(i64_, address_b), next_b));
            builder_.CreateStore(next_a, address_a);
            builder_.CreateStore(next_b, address_b);
            if (words == 1U) {
                changed = diff;
            } else {
                changed = builder_.CreateOr(changed, index == word
                        ? builder_.CreateLShr(diff, constant(shift))
                        : builder_.CreateShl(diff, constant(64U - shift)));
            }
        }
        if (words != 1U) {
            changed = builder_.CreateAnd(changed, m);
        }
        // While a VHDL assignment is pending on the slot, the same write
        // lands in its next block too.
        emit_net_mirror(inst, shape, words,
            net_field(words, nullptr, offset, width),
            { codes[0], codes[1], codes[2], constant(0U) });
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* notify = llvm::BasicBlock::Create(context_, "notify_net", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored_net", &function_);
        builder_.CreateCondBr(builder_.CreateAnd(nonzero(changed),
                                  builder_.CreateNot(silent)),
            notify, after);
        builder_.SetInsertPoint(notify);
        auto* slot = builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit));
        emit_notify_skip(slot, after);
        if (words == 1U) {
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, i32_, i64_ }, false);
            builder_.CreateCall(type, helper_symbol("fsim_sk_notify", type),
                { frame_, slot, changed });
        } else {
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, i32_, i64_, i32_ }, false);
            builder_.CreateCall(type, helper_symbol("fsim_sk_notify_field", type),
                { frame_, slot, changed, llvm::ConstantInt::get(i32_, offset) });
        }
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    /// emit_store_net for a field at the run-time i32 `offset` of `count` bits
    /// (the i64 `mask` of its low bits; 1 to 64): a dynamic-index or dynamic
    /// part-select write to a resolved slot (store_slot_dynamic and
    /// store_slot_part, sub 2; imm_a words per plane, imm_b the source count
    /// and from bit 32 the member's source, offset the slot's width). The
    /// field is inside the slot; the member's source takes its codes, the
    /// words it touches resolve over every source, and the readers of the bits
    /// that changed are notified, as the static emit_store_net does.
    void emit_store_net_dynamic(const KInst& inst, const Pair value, llvm::Value* offset,
        llvm::Value* mask, llvm::Value* count)
    {
        const std::uint64_t words = inst.imm_a;
        const auto shape = detail::make_net_shape(static_cast<std::uint32_t>(inst.imm_b),
            static_cast<std::uint32_t>(inst.imm_b >> 32U), inst.offset);
        auto* info = binding(inst.d, 1U);
        const auto field = net_field_counted(words, offset, mask, count);
        auto* a = builder_.CreateAnd(value.a, mask);
        auto* b = builder_.CreateAnd(value.b, mask);
        // The field's codes: 0 0010, 1 0011, x 0001, z 0100.
        const std::array<llvm::Value*, 4> codes { a,
            builder_.CreateAnd(builder_.CreateNot(b), mask),
            builder_.CreateAnd(b, builder_.CreateNot(a)), constant(0U) };
        // The resolved value's words the field touches, before and after.
        auto* high_word = field.spans != nullptr
            ? builder_.CreateSelect(field.spans,
                  builder_.CreateAdd(field.word, constant(1U)), field.word)
            : field.word;
        const auto resolved_planes = [&](llvm::Value* word) {
            return Pair { builder_.CreateLoad(i64_,
                              net_word(inst, shape, words, false, 0U, word)),
                builder_.CreateLoad(i64_, net_word(inst, shape, words, false, 1U, word)) };
        };
        const auto old_low = resolved_planes(field.word);
        const auto old_high = resolved_planes(high_word);
        emit_net_insert(inst, shape, words, false, field, codes);
        emit_net_resolve(inst, shape, words, false, field);
        const auto new_low = resolved_planes(field.word);
        const auto new_high = resolved_planes(high_word);
        auto* diff_low = builder_.CreateOr(builder_.CreateXor(old_low.a, new_low.a),
            builder_.CreateXor(old_low.b, new_low.b));
        llvm::Value* changed = diff_low;
        if (words != 1U) {
            auto* diff_high = builder_.CreateOr(builder_.CreateXor(old_high.a, new_high.a),
                builder_.CreateXor(old_high.b, new_high.b));
            auto* spill = builder_.CreateAnd(builder_.CreateSub(constant(64U), field.shift),
                constant(63U));
            changed = builder_.CreateAnd(
                builder_.CreateOr(builder_.CreateLShr(diff_low, field.shift),
                    builder_.CreateSelect(field.spans,
                        builder_.CreateShl(diff_high, spill), constant(0U))),
                mask);
        }
        // While a VHDL assignment is pending on the slot, the same write
        // lands in its next block too.
        emit_net_mirror(inst, shape, words, field, codes);
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* notify = llvm::BasicBlock::Create(context_, "notify_net", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored_net", &function_);
        builder_.CreateCondBr(builder_.CreateAnd(nonzero(changed),
                                  builder_.CreateNot(silent)),
            notify, after);
        builder_.SetInsertPoint(notify);
        auto* slot = builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit));
        emit_notify_skip(slot, after);
        if (words == 1U) {
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, i32_, i64_ }, false);
            builder_.CreateCall(type, helper_symbol("fsim_sk_notify", type),
                { frame_, slot, changed });
        } else {
            auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                { ptr_, i32_, i64_, i32_ }, false);
            builder_.CreateCall(type, helper_symbol("fsim_sk_notify_field", type),
                { frame_, slot, changed, offset });
        }
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    /// The bit field a write to a resolved slot touches: its first word and
    /// shift (constants for a static offset), and whether it continues in
    /// the next word: for certain (`spans_fixed`), or when the i1 `spans`
    /// says so at run time. A field is at most 64 bits wide.
    struct NetField {
        llvm::Value* word { };
        llvm::Value* shift { };
        bool spans_fixed { };
        llvm::Value* spans { };
        std::uint32_t width { };
        /// A field of a run-time width (net_field with a count): the mask of
        /// its bits; null for a field of `width` bits.
        llvm::Value* mask { };
    };

    /// The field of `width` bits at the static bit offset `fixed`, or at the
    /// run-time i32 `offset` when it is not null, of a slot of `words` words
    /// per plane.
    [[nodiscard]] NetField net_field(const std::uint64_t words, llvm::Value* offset,
        const std::uint32_t fixed, const std::uint32_t width)
    {
        NetField field;
        field.width = width;
        if (offset == nullptr) {
            field.word = constant(fixed / 64U);
            field.shift = constant(fixed % 64U);
            field.spans_fixed = fixed % 64U != 0U && fixed % 64U + width > 64U
                && fixed / 64U + 1U < words;
            return field;
        }
        auto* bits = builder_.CreateZExt(offset, i64_);
        if (words == 1U) {
            field.word = constant(0U);
            field.shift = bits;
            return field;
        }
        field.word = builder_.CreateLShr(bits, constant(6U));
        field.shift = builder_.CreateAnd(bits, constant(63U));
        field.spans = builder_.CreateAnd(
            builder_.CreateICmpUGT(builder_.CreateAdd(field.shift, constant(width)),
                constant(64U)),
            builder_.CreateICmpULT(builder_.CreateAdd(field.word, constant(1U)),
                constant(words)));
        return field;
    }

    /// net_field for a field of the run-time i64 `count` bits (1 to 64) whose
    /// low bits are `mask`, at the run-time i32 `offset` (a dynamic part-select
    /// write, IEEE 1800-2023 11.5.1).
    [[nodiscard]] NetField net_field_counted(const std::uint64_t words,
        llvm::Value* offset, llvm::Value* mask, llvm::Value* count)
    {
        NetField field;
        field.mask = mask;
        auto* bits = builder_.CreateZExt(offset, i64_);
        if (words == 1U) {
            field.word = constant(0U);
            field.shift = bits;
            return field;
        }
        field.word = builder_.CreateLShr(bits, constant(6U));
        field.shift = builder_.CreateAnd(bits, constant(63U));
        field.spans = builder_.CreateAnd(
            builder_.CreateICmpUGT(builder_.CreateAdd(field.shift, count), constant(64U)),
            builder_.CreateICmpULT(builder_.CreateAdd(field.word, constant(1U)),
                constant(words)));
        return field;
    }

    /// Runs `body` when the i1 `condition` holds.
    template <typename Body>
    void emit_net_when(llvm::Value* condition, const char* name, Body&& body)
    {
        auto* then = llvm::BasicBlock::Create(context_, name, &function_);
        auto* after = llvm::BasicBlock::Create(context_, "net_after", &function_);
        builder_.CreateCondBr(condition, then, after);
        builder_.SetInsertPoint(then);
        body();
        builder_.CreateBr(after);
        builder_.SetInsertPoint(after);
    }

    /// Word `word` of row `row` (the value's planes are rows 0 and 1, source
    /// k's code planes rows 2 + 4 k .. 5 + 4 k) of the current or next block
    /// of the resolved slot bound at inst.d (Slot::net_processes).
    [[nodiscard]] llvm::Value* net_word(const KInst& inst, const detail::NetShape& shape,
        const std::uint64_t words, const bool next, const std::uint64_t row,
        llvm::Value* word)
    {
        const auto base
            = ((next ? shape.rows() + 4U * std::uint64_t { shape.sources } : 0U) + row)
            * words;
        return builder_.CreateInBoundsGEP(i64_, slot_pointer(inst.d),
            builder_.CreateAdd(constant(base), word));
    }

    /// The first VHDL assignment of an update phase to a resolved slot: the
    /// next block takes the current one and the pending flag is set, as
    /// StaticKernel::begin_net_pending does.
    void emit_net_begin_pending(const KInst& inst, const detail::NetShape& shape,
        const std::uint64_t words)
    {
        const auto block = (shape.rows() + 4U * std::uint64_t { shape.sources }) * words;
        auto* flag = slot_word(inst.d, 2U * block);
        emit_net_when(builder_.CreateICmpEQ(builder_.CreateLoad(i64_, flag), constant(0U)),
            "net_start", [&] {
                if (block <= 16U) {
                    for (std::uint64_t index = 0U; index < block; ++index) {
                        builder_.CreateStore(
                            builder_.CreateLoad(i64_, slot_word(inst.d, index)),
                            slot_word(inst.d, block + index));
                    }
                } else {
                    auto* from = builder_.GetInsertBlock();
                    auto* loop = llvm::BasicBlock::Create(context_, "net_copy", &function_);
                    auto* done = llvm::BasicBlock::Create(context_, "net_copied", &function_);
                    builder_.CreateBr(loop);
                    builder_.SetInsertPoint(loop);
                    auto* index = builder_.CreatePHI(i64_, 2);
                    index->addIncoming(constant(0U), from);
                    auto* base = slot_pointer(inst.d);
                    builder_.CreateStore(
                        builder_.CreateLoad(i64_, builder_.CreateInBoundsGEP(i64_, base, index)),
                        builder_.CreateInBoundsGEP(i64_, base,
                            builder_.CreateAdd(index, constant(block))));
                    auto* next = builder_.CreateAdd(index, constant(1U));
                    index->addIncoming(next, loop);
                    builder_.CreateCondBr(builder_.CreateICmpULT(next, constant(block)),
                        loop, done);
                    builder_.SetInsertPoint(done);
                }
                builder_.CreateStore(constant(1U), flag);
            });
    }

    /// The member's source of the current or next block takes `bits` (the
    /// four code planes, masked to the field) at `field`.
    void emit_net_insert(const KInst& inst, const detail::NetShape& shape,
        const std::uint64_t words, const bool next, const NetField& field,
        const std::array<llvm::Value*, 4>& bits)
    {
        auto* m = field.mask != nullptr ? field.mask : constant(mask_of(field.width));
        const auto put = [&](const bool high) {
            auto* word = high ? builder_.CreateAdd(field.word, constant(1U)) : field.word;
            for (std::uint64_t plane = 0U; plane < 4U; ++plane) {
                auto* address = net_word(inst, shape, words, next,
                    shape.rows() + 4U * std::uint64_t { shape.own } + plane, word);
                auto* old = builder_.CreateLoad(i64_, address);
                if (!high) {
                    builder_.CreateStore(
                        builder_.CreateOr(
                            builder_.CreateAnd(old,
                                builder_.CreateNot(builder_.CreateShl(m, field.shift))),
                            builder_.CreateShl(bits[plane], field.shift)),
                        address);
                    continue;
                }
                auto* spill = builder_.CreateSub(constant(64U), field.shift);
                builder_.CreateStore(
                    builder_.CreateOr(
                        builder_.CreateAnd(old,
                            builder_.CreateNot(builder_.CreateLShr(m, spill))),
                        builder_.CreateLShr(bits[plane], spill)),
                    address);
            }
        };
        put(false);
        if (field.spans_fixed) {
            put(true);
        } else if (field.spans != nullptr) {
            emit_net_when(field.spans, "net_insert_high", [&] { put(true); });
        }
    }

    /// The mask of the slot's bits in word `word`.
    [[nodiscard]] llvm::Value* net_word_mask(const std::uint32_t width, llvm::Value* word)
    {
        if (const auto* fixed = llvm::dyn_cast<llvm::ConstantInt>(word)) {
            const auto bits = width - 64U * static_cast<std::uint32_t>(fixed->getZExtValue());
            return constant(bits >= 64U ? ~std::uint64_t { 0 } : mask_of(bits));
        }
        if (width % 64U == 0U) {
            return constant(~std::uint64_t { 0 });
        }
        auto* bits = builder_.CreateSub(constant(width), builder_.CreateShl(word, constant(6U)));
        auto* full = builder_.CreateICmpUGE(bits, constant(64U));
        auto* clamped = builder_.CreateSelect(full, constant(63U), bits);
        return builder_.CreateSelect(full, constant(~std::uint64_t { 0 }),
            builder_.CreateSub(builder_.CreateShl(constant(1U), clamped), constant(1U)));
    }

    /// Resolves word `word` of the current or next block's value over that
    /// block's sources as StaticKernel::resolve_net_words does.
    void emit_net_resolve_word(const KInst& inst, const detail::NetShape& shape,
        const std::uint64_t words, const bool next, llvm::Value* word)
    {
        if (shape.nine) {
            emit_nine_resolve_word(inst, shape, words, next, word);
            return;
        }
        llvm::Value* strong0 = constant(0U);
        llvm::Value* strong1 = constant(0U);
        llvm::Value* weak0 = constant(0U);
        llvm::Value* weak1 = constant(0U);
        for (std::uint64_t source = 0U; source < shape.sources; ++source) {
            const auto plane = [&](const std::uint64_t index) {
                return builder_.CreateLoad(i64_,
                    net_word(inst, shape, words, next, 2U + 4U * source + index, word));
            };
            auto* p0 = plane(0U);
            auto* p1 = plane(1U);
            auto* p2 = plane(2U);
            auto* p3 = plane(3U);
            auto* not2 = builder_.CreateNot(p2);
            auto* not3 = builder_.CreateNot(p3);
            auto* unknown = builder_.CreateOr(p3,
                builder_.CreateAnd(not2, builder_.CreateNot(p1)));
            auto* known = builder_.CreateAnd(builder_.CreateAnd(not3, not2), p1);
            auto* weak = builder_.CreateAnd(builder_.CreateAnd(not3, p2),
                builder_.CreateOr(p1, p0));
            strong0 = builder_.CreateOr(strong0, builder_.CreateOr(unknown,
                builder_.CreateAnd(known, builder_.CreateNot(p0))));
            strong1 = builder_.CreateOr(strong1, builder_.CreateOr(unknown,
                builder_.CreateAnd(known, p0)));
            weak0 = builder_.CreateOr(weak0, builder_.CreateAnd(weak,
                builder_.CreateNot(builder_.CreateAnd(p1, p0))));
            weak1 = builder_.CreateOr(weak1, builder_.CreateAnd(weak, p0));
        }
        auto* unforced = builder_.CreateNot(builder_.CreateOr(strong0, strong1));
        auto* zero = builder_.CreateOr(strong0, builder_.CreateAnd(unforced, weak0));
        auto* one = builder_.CreateOr(strong1, builder_.CreateAnd(unforced, weak1));
        auto* word_mask = net_word_mask(shape.width, word);
        builder_.CreateStore(builder_.CreateAnd(one, word_mask),
            net_word(inst, shape, words, next, 0U, word));
        builder_.CreateStore(
            builder_.CreateAnd(builder_.CreateNot(builder_.CreateXor(zero, one)), word_mask),
            net_word(inst, shape, words, next, 1U, word));
    }

    /// emit_net_resolve_word for a slot of nine states (a std_logic signal):
    /// the std_logic_1164 resolution of the sources that drive each element,
    /// as StaticKernel::resolve_nine_word computes it. One source is the
    /// element as it is; with several, 'U' wins, then 'X' and '-', a strong
    /// 0 against a strong 1 is 'X', a strong value wins over the weak ones
    /// and 'Z', 'W' or both 'L' and 'H' give 'W', and the rest 'L', 'H' or
    /// 'Z'. Codes: U 0000, X 0001, 0 0010, 1 0011, Z 0100, W 0101, L 0110,
    /// H 0111, - 1000; 1111 is an element a source has no driver for.
    void emit_nine_resolve_word(const KInst& inst, const detail::NetShape& shape,
        const std::uint64_t words, const bool next, llvm::Value* word)
    {
        const auto no = [&](llvm::Value* value) { return builder_.CreateNot(value); };
        const auto both = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateAnd(left, right);
        };
        const auto either = [&](llvm::Value* left, llvm::Value* right) {
            return builder_.CreateOr(left, right);
        };
        llvm::Value* seen = constant(0U);
        llvm::Value* many = constant(0U);
        llvm::Value* any_u = constant(0U);
        llvm::Value* any_x = constant(0U);
        llvm::Value* has_0 = constant(0U);
        llvm::Value* has_1 = constant(0U);
        llvm::Value* has_w = constant(0U);
        llvm::Value* has_l = constant(0U);
        llvm::Value* has_h = constant(0U);
        std::array<llvm::Value*, 4> single { constant(0U), constant(0U), constant(0U),
            constant(0U) };
        for (std::uint64_t source = 0U; source < shape.sources; ++source) {
            std::array<llvm::Value*, 4> p { };
            for (std::uint64_t plane = 0U; plane < 4U; ++plane) {
                p[plane] = builder_.CreateLoad(i64_,
                    net_word(inst, shape, words, next,
                        shape.rows() + 4U * source + plane, word));
            }
            auto* present = no(both(both(p[0], p[1]), both(p[2], p[3])));
            many = either(many, both(seen, present));
            seen = either(seen, present);
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                single[plane] = either(single[plane], both(p[plane], present));
            }
            auto* low_clear = both(no(p[3]), no(p[2]));
            auto* weak_row = both(no(p[3]), p[2]);
            any_u = either(any_u, both(low_clear, both(no(p[1]), no(p[0]))));
            any_x = either(any_x,
                either(both(low_clear, both(no(p[1]), p[0])),
                    both(both(p[3], no(p[2])), both(no(p[1]), no(p[0])))));
            has_0 = either(has_0, both(low_clear, both(p[1], no(p[0]))));
            has_1 = either(has_1, both(low_clear, both(p[1], p[0])));
            has_w = either(has_w, both(weak_row, both(no(p[1]), p[0])));
            has_l = either(has_l, both(weak_row, both(p[1], no(p[0]))));
            has_h = either(has_h, both(weak_row, both(p[1], p[0])));
        }
        auto* unknown = both(no(any_u), either(any_x, both(has_0, has_1)));
        auto* strong = both(no(any_u), no(unknown));
        auto* zero_out = both(strong, has_0);
        auto* one_out = both(strong, has_1);
        auto* weak = both(both(strong, no(has_0)), no(has_1));
        auto* w_out = both(weak, either(has_w, both(has_l, has_h)));
        auto* rest = both(weak, no(w_out));
        auto* l_out = both(rest, has_l);
        auto* h_out = both(rest, has_h);
        auto* z_out = both(rest, both(no(has_l), no(has_h)));
        const std::array<llvm::Value*, 4> codes {
            either(either(unknown, one_out), either(w_out, h_out)),
            either(either(zero_out, one_out), either(l_out, h_out)),
            either(either(z_out, w_out), either(l_out, h_out)),
            constant(0U),
        };
        auto* word_mask = net_word_mask(shape.width, word);
        for (std::uint64_t plane = 0U; plane < 4U; ++plane) {
            auto* resolved = either(both(many, codes[plane]), both(no(many), single[plane]));
            builder_.CreateStore(both(resolved, word_mask),
                net_word(inst, shape, words, next, plane, word));
        }
    }

    /// Resolves the words `field` touches.
    void emit_net_resolve(const KInst& inst, const detail::NetShape& shape,
        const std::uint64_t words, const bool next, const NetField& field)
    {
        emit_net_resolve_word(inst, shape, words, next, field.word);
        if (field.spans_fixed) {
            emit_net_resolve_word(inst, shape, words, next,
                builder_.CreateAdd(field.word, constant(1U)));
        } else if (field.spans != nullptr) {
            emit_net_when(field.spans, "net_resolve_high", [&] {
                emit_net_resolve_word(inst, shape, words, next,
                    builder_.CreateAdd(field.word, constant(1U)));
            });
        }
    }

    /// A SystemVerilog write that landed in the current block of a resolved
    /// slot also lands in its next block while the slot is pending.
    void emit_net_mirror(const KInst& inst, const detail::NetShape& shape,
        const std::uint64_t words, const NetField& field,
        const std::array<llvm::Value*, 4>& bits)
    {
        const auto block = (shape.rows() + 4U * std::uint64_t { shape.sources }) * words;
        emit_net_when(builder_.CreateICmpNE(
                          builder_.CreateLoad(i64_, slot_word(inst.d, 2U * block)),
                          constant(0U)),
            "net_mirror", [&] {
                emit_net_insert(inst, shape, words, true, field, bits);
                emit_net_resolve(inst, shape, words, true, field);
            });
    }

    /// The code planes of a field of `width` bits holding `value` ('U' at the
    /// bits of `unknown`, a weak element at those of `weak` when it is not
    /// null), as kernel_word::logic9_planes: 'U' 0000, 'X' 0001, '0' 0010,
    /// '1' 0011, 'Z' 0100, 'W' 0101, 'L' 0110, 'H' 0111, '-' 1000.
    [[nodiscard]] std::array<llvm::Value*, 4> net_codes(const Pair value,
        llvm::Value* unknown, const std::uint32_t width,
        llvm::Value* weak = nullptr)
    {
        return net_codes_masked(value, unknown, constant(mask_of(width)), weak);
    }

    /// net_codes of a field of a run-time width: `mask` its bits.
    [[nodiscard]] std::array<llvm::Value*, 4> net_codes_masked(const Pair value,
        llvm::Value* unknown, llvm::Value* mask, llvm::Value* weak = nullptr)
    {
        auto* known = builder_.CreateAnd(mask, builder_.CreateNot(unknown));
        auto* z_shape = builder_.CreateAnd(builder_.CreateNot(value.a), value.b);
        if (weak == nullptr) {
            return { builder_.CreateAnd(value.a, known),
                builder_.CreateAnd(builder_.CreateNot(value.b), known),
                builder_.CreateAnd(z_shape, known), constant(0U) };
        }
        // A weak element: 'L' 0110, 'H' 0111, 'W' 0101, '-' 1000, from its
        // word 0, 1, X, Z (kernel_word::logic9_planes).
        return { builder_.CreateAnd(value.a, known),
            builder_.CreateAnd(builder_.CreateNot(value.b), known),
            builder_.CreateAnd(builder_.CreateXor(weak, z_shape), known),
            builder_.CreateAnd(builder_.CreateAnd(weak, z_shape), known) };
    }

    /// One VHDL assignment's field of a resolved slot: resolved into the next
    /// block (emit_net_begin_pending first), where the update phase takes it
    /// from (StaticKernel::commit_writes). The field is at the run-time i32
    /// `offset` when it is not null, else at `fixed`.
    void emit_net_vhdl_field(const KInst& inst, const detail::NetShape& shape,
        llvm::Value* offset, const std::uint32_t fixed, const std::uint32_t width,
        const std::array<llvm::Value*, 4>& bits)
    {
        const std::uint64_t words = (shape.width + 63U) / 64U;
        const auto field = net_field(words, offset, fixed, width);
        emit_net_insert(inst, shape, words, true, field, bits);
        emit_net_resolve(inst, shape, words, true, field);
    }

    /// emit_net_vhdl_field for a field of the run-time width `count` bits
    /// (`mask` its bits) at the run-time i32 `offset` (a part-select
    /// assignment).
    void emit_net_vhdl_field_counted(const KInst& inst, const detail::NetShape& shape,
        llvm::Value* offset, llvm::Value* mask, llvm::Value* count,
        const std::array<llvm::Value*, 4>& bits)
    {
        const std::uint64_t words = (shape.width + 63U) / 64U;
        const auto field = net_field_counted(words, offset, mask, count);
        emit_net_insert(inst, shape, words, true, field, bits);
        emit_net_resolve(inst, shape, words, true, field);
    }

    /// Branches to `skip` where the notify helper would do nothing for the
    /// changed slot (the frame's fast_notify table): its one target is the
    /// running partition with readers at or after the frame's position, or
    /// is already queued. Otherwise continues in a new block that calls
    /// the helper.
    void emit_notify_skip(llvm::Value* slot, llvm::BasicBlock* skip)
    {
        if (outline_stores_) {
            // Large cold templates: one function per module.
            auto* call = llvm::BasicBlock::Create(context_, "notify_call", &function_);
            builder_.CreateCondBr(builder_.CreateCall(local_notify_skip(), { frame_, slot }),
                skip, call);
            builder_.SetInsertPoint(call);
            return;
        }
        auto* call = llvm::BasicBlock::Create(context_, "notify_call", &function_);
        auto* lookup = llvm::BasicBlock::Create(context_, "notify_lookup", &function_);
        auto* running = llvm::BasicBlock::Create(context_, "notify_running", &function_);
        auto* queued = llvm::BasicBlock::Create(context_, "notify_queued", &function_);
        auto* table = builder_.CreateLoad(ptr_, frame_field(80U));
        builder_.CreateCondBr(builder_.CreateIsNull(table), call, lookup);
        builder_.SetInsertPoint(lookup);
        auto* entry = builder_.CreateInBoundsGEP(i32_, table,
            builder_.CreateShl(builder_.CreateZExt(slot, i64_), 1U));
        auto* partition = builder_.CreateLoad(i32_, entry);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(partition,
                llvm::ConstantInt::get(i32_, runtime::simir::static_kernel_no_fast_notify)),
            call, running);
        builder_.SetInsertPoint(running);
        auto* min_position = builder_.CreateLoad(i32_,
            builder_.CreateConstInBoundsGEP1_64(i32_, entry, 1U));
        auto* later = builder_.CreateAnd(
            builder_.CreateICmpEQ(partition, builder_.CreateLoad(i32_, frame_field(96U))),
            builder_.CreateICmpUGE(min_position, builder_.CreateLoad(i32_, frame_field(36U))));
        builder_.CreateCondBr(later, skip, queued);
        builder_.SetInsertPoint(queued);
        auto* flags = builder_.CreateLoad(ptr_, frame_field(88U));
        auto* flag = builder_.CreateLoad(llvm::Type::getInt8Ty(context_),
            builder_.CreateInBoundsGEP(llvm::Type::getInt8Ty(context_), flags,
                builder_.CreateZExt(partition, i64_)));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(flag, llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0U)),
            skip, call);
        builder_.SetInsertPoint(call);
    }

    /// i1 fsim_sk_local_notify_skip(frame, slot): emit_notify_skip's test
    /// (true: skip the notify helper) as one function per module.
    [[nodiscard]] llvm::Function* local_notify_skip()
    {
        static constexpr const char* name = "fsim_sk_local_notify_skip";
        if (auto* existing = module_.getFunction(name)) {
            return existing;
        }
        auto* i1 = llvm::Type::getInt1Ty(context_);
        auto* type = llvm::FunctionType::get(i1, { ptr_, i32_ }, false);
        auto* function = llvm::Function::Create(type, llvm::Function::InternalLinkage,
            name, module_);
        function->addFnAttr(llvm::Attribute::NoInline);
        function->addFnAttr(llvm::Attribute::NoUnwind);
        auto* frame = function->getArg(0);
        auto* slot = function->getArg(1);
        const auto block = [&](const char* label) {
            return llvm::BasicBlock::Create(context_, label, function);
        };
        auto* entry = block("entry");
        auto* lookup = block("notify_lookup");
        auto* running = block("notify_running");
        auto* queued = block("notify_queued");
        auto* skip = block("notify_skip");
        auto* call = block("notify_call");
        llvm::IRBuilder<> b(entry);
        const auto field = [&](const std::uint64_t bytes) {
            return b.CreateConstInBoundsGEP1_64(i8_, frame, bytes);
        };
        auto* table = b.CreateLoad(ptr_, field(80U));
        b.CreateCondBr(b.CreateIsNull(table), call, lookup);
        b.SetInsertPoint(lookup);
        auto* entry_at = b.CreateInBoundsGEP(i32_, table,
            b.CreateShl(b.CreateZExt(slot, i64_), 1U));
        auto* partition = b.CreateLoad(i32_, entry_at);
        b.CreateCondBr(b.CreateICmpEQ(partition,
                           llvm::ConstantInt::get(i32_,
                               runtime::simir::static_kernel_no_fast_notify)),
            call, running);
        b.SetInsertPoint(running);
        auto* min_position = b.CreateLoad(i32_,
            b.CreateConstInBoundsGEP1_64(i32_, entry_at, 1U));
        auto* later = b.CreateAnd(
            b.CreateICmpEQ(partition, b.CreateLoad(i32_, field(96U))),
            b.CreateICmpUGE(min_position, b.CreateLoad(i32_, field(36U))));
        b.CreateCondBr(later, skip, queued);
        b.SetInsertPoint(queued);
        auto* flags = b.CreateLoad(ptr_, field(88U));
        auto* flag = b.CreateLoad(i8_, b.CreateInBoundsGEP(i8_, flags,
            b.CreateZExt(partition, i64_)));
        b.CreateCondBr(b.CreateICmpNE(flag, llvm::ConstantInt::get(i8_, 0U)), skip, call);
        b.SetInsertPoint(skip);
        b.CreateRet(llvm::ConstantInt::getTrue(context_));
        b.SetInsertPoint(call);
        b.CreateRet(llvm::ConstantInt::getFalse(context_));
        return function;
    }

    void emit_store_slot(const KInst& inst, const Pair value)
    {
        auto* info = binding(inst.d, 1U);
        auto* low = slot_word(inst.d, 0U);
        auto* high = slot_word(inst.d, 1U);
        const bool linear = (inst.flags & detail::flag_linear) != 0U;
        if (inst.aux != 0U) {
            // The template's slot is silent in every unit (build_native):
            // nothing observes the change, so the store needs no comparison
            // and no notification.
            Pair next = value;
            if (linear) {
                next = insert(Pair { builder_.CreateLoad(i64_, low),
                                  builder_.CreateLoad(i64_, high) },
                    value, inst.offset, inst.width);
            }
            builder_.CreateStore(next.a, low);
            builder_.CreateStore(next.b, high);
            return;
        }
        const Pair old { builder_.CreateLoad(i64_, low),
            builder_.CreateLoad(i64_, high) };
        const auto next = linear ? insert(old, value, inst.offset, inst.width)
                                 : value;
        auto* silent = builder_.CreateICmpNE(
            builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, silent_bit)),
            llvm::ConstantInt::get(i32_, 0U));
        auto* changed = builder_.CreateOr(builder_.CreateXor(old.a, next.a),
            builder_.CreateXor(old.b, next.b));
        auto* write = llvm::BasicBlock::Create(context_, "write", &function_);
        auto* notify = llvm::BasicBlock::Create(context_, "notify", &function_);
        auto* after = llvm::BasicBlock::Create(context_, "stored", &function_);
        builder_.CreateCondBr(builder_.CreateOr(silent, nonzero(changed)), write,
            after);
        builder_.SetInsertPoint(write);
        builder_.CreateStore(next.a, low);
        builder_.CreateStore(next.b, high);
        builder_.CreateCondBr(silent, after, notify);
        builder_.SetInsertPoint(notify);
        auto* slot = builder_.CreateAnd(info, llvm::ConstantInt::get(i32_, ~silent_bit));
        emit_notify_skip(slot, after);
        auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
            { ptr_, i32_, i64_ }, false);
        auto* callee = helper_symbol("fsim_sk_notify", type);
        builder_.CreateCall(type, callee, { frame_, slot, changed });
        auto* status = builder_.CreateLoad(i32_, frame_field(40U));
        builder_.CreateCondBr(
            builder_.CreateICmpNE(status, llvm::ConstantInt::get(i32_, 0U)),
            failure_block_, after);
        builder_.SetInsertPoint(after);
    }

    llvm::LLVMContext& context_;
    llvm::Module& module_;
    llvm::Function& function_;
    const detail::CompiledBody& body_;
    // Simplifies as it builds (x | 0, x & ~0, constant branches): the cold
    // tier runs no clean-up passes.
    llvm::IRBuilder<llvm::InstSimplifyFolder> builder_;
    llvm::IRBuilder<llvm::InstSimplifyFolder> entry_builder_;
    llvm::Type* i32_;
    llvm::Type* i64_;
    llvm::Type* i8_;
    llvm::PointerType* ptr_;
    llvm::Value* frame_ { };
    llvm::Value* registers_ { };
    bool local_registers_ { };
    /// Compiled by the optimizing backend (hot tier).
    bool optimizing_ { };
    /// Large cold templates: deferred stores and notify skips call one
    /// helper per module instead of inlining their checks. Warm and small
    /// members run often enough that the call costs more run time than it
    /// saves compile time.
    bool outline_stores_ { };
    /// Exits store local registers to the register file (VHDL).
    bool flush_locals_ { };
    /// Records the instruction after the current one as a continuation.
    std::uint32_t continue_point(const std::uint32_t offset = 1U)
    {
        if (resume_points_ != nullptr) {
            // The set mirrors the vector (see resume_points_), which keeps
            // the order of first use.
            if (resume_seen_.insert(at_ + offset).second) {
                resume_points_->push_back(at_ + offset);
            }
        }
        return at_ + offset;
    }

    /// Two-state code reading a value that is not all 0/1: unless `ok`,
    /// the full code runs the instruction instead (it has no effects yet,
    /// and every earlier result is in the register file). Unlike a slow
    /// path, this adds no block the fast path must merge back into.
    void read_or_hand_over(llvm::Value* ok)
    {
        if (folded(ok, true)) {
            return;
        }
        auto* hand_over = llvm::BasicBlock::Create(context_, "hand_over", &function_);
        auto* next = llvm::BasicBlock::Create(context_, "read", &function_);
        builder_.CreateCondBr(ok, next, hand_over);
        builder_.SetInsertPoint(hand_over);
        builder_.CreateStore(llvm::ConstantInt::get(i32_, continue_point(0U)),
            frame_field(44U));
        builder_.CreateStore(llvm::ConstantInt::get(i32_, 4U), frame_field(40U));
        builder_.CreateBr(failure_block_);
        builder_.SetInsertPoint(next);
    }

    /// See guard_known.
    bool two_state_ { };
    /// Two-state code hands a with_fallback slow path to the full code
    /// (read_or_hand_over) instead of calling the evaluate helper.
    bool hand_over_slow_paths_ { true };
    std::vector<std::uint32_t>* resume_points_ { };
    std::unordered_set<std::uint32_t> resume_seen_;
    /// The template has a VHDL instance (prologue entry).
    bool vhdl_ { };
    std::uint32_t at_ { };
    llvm::Value* guarded_unknown_ { };
    /// Per register: its local slot, or null when it lives in the frame.
    std::vector<llvm::Value*> local_;
    llvm::Value* arena_ { };
    llvm::Value* out_ { };
    /// 'U' mask returned by the last evaluate helper call (or its merge).
    llvm::Value* last_unknown_ { };
    // The word loops' scratch (scratch_words): one entry-block alloca of
    // word_scratch_size_ words; word_scratch_used_ counts the current
    // operation's blocks.
    llvm::AllocaInst* word_scratch_ { };
    std::uint64_t word_scratch_size_ { };
    std::uint64_t word_scratch_used_ { };
    /// The weak mask beside last_unknown_ (null: none), in weak mode.
    llvm::Value* last_weak_ { };
    /// 'U' mask of a U-aware dynamic insert's result.
    llvm::Value* insert_unknown_ { };
    llvm::Value* insert_weak_ { };
    llvm::Value* bindings_ { };
    llvm::BasicBlock* entry_ { };
    /// Entry-block values: binding words by index * 2 + word, and slot
    /// pointers by binding index.
    std::unordered_map<std::uint64_t, llvm::Value*> binding_values_;
    llvm::Value* write_marks_ { };
    llvm::Value* write_round_ { };
    std::unordered_map<std::uint32_t, llvm::Value*> slot_pointers_;
    llvm::BasicBlock* return_block_ { };
    llvm::BasicBlock* failure_block_ { };
    /// Step code: the one switch over the return targets every Return step
    /// branches to (its position in `return_next_`).
    llvm::BasicBlock* return_dispatch_ { };
    llvm::PHINode* return_next_ { };
    llvm::BasicBlock* step_leave_ { };
    std::vector<llvm::BasicBlock*> blocks_;
    std::map<std::uint32_t, Pair> cache_;
    /// Registers written to `cache_` since the last sweep.
    std::vector<std::uint32_t> fresh_;
    /// Blocks of the current KIR block in which an instruction started or
    /// ended (see emit).
    std::unordered_set<const llvm::BasicBlock*> spine_;
    std::uint32_t current_ { };
};

class LlvmStaticKernelCodegen final : public StaticKernelCodegen {
public:
    explicit LlvmStaticKernelCodegen(StaticKernelCodegenOptions options)
        : ahead_of_time_ { options.ahead_of_time }
        , jobs_ { options.jobs != 0U ? options.jobs
                                     : std::max(1U, std::thread::hardware_concurrency()) }
    {
        initialize_native_target();
        if (!options.cache_root.empty()) {
            cache_.emplace(options.cache_root);
        }
        identity_ = codegen_identity();
        identity_hash_ = llvm::xxh3_128bits(llvm::ArrayRef {
            reinterpret_cast<const std::uint8_t*>(identity_.data()), identity_.size() });
        const char* level = std::getenv("FSIM_STATIC_KERNEL_CODEGEN_LEVEL");
        const auto codegen_level = level == nullptr ? 0 : std::atoi(level);
        cold_machine_ = { codegen_level,
            std::getenv("FSIM_STATIC_KERNEL_NO_FAST_ISEL") == nullptr };
        jit_ = make_jit(capture_, cold_machine_);
        // Hot templates: the optimizing backend. Register allocation and the
        // machine passes carry most of its benefit; fast instruction
        // selection halves its compile time for a small run-time cost, so
        // code built ahead of time (whose compile time is the elaborate
        // step's) uses the full selector (original_codec simulate -0.09 s).
        // FSIM_STATIC_KERNEL_HOT_FAST_ISEL=0/1 chooses either way.
        const char* hot_level = std::getenv("FSIM_STATIC_KERNEL_HOT_LEVEL");
        const char* hot_fast_isel = std::getenv("FSIM_STATIC_KERNEL_HOT_FAST_ISEL");
        hot_machine_ = { hot_level == nullptr ? 2 : std::atoi(hot_level),
            hot_fast_isel == nullptr ? !ahead_of_time_
                                     : std::string_view { hot_fast_isel } != "0" };
        hot_jit_ = make_jit(capture_, hot_machine_);
        // Warm templates: full instruction selection without the optimizing
        // backend's cost (FSIM_STATIC_KERNEL_WARM_LEVEL,
        // FSIM_STATIC_KERNEL_WARM_FAST_ISEL=0/1).
        const char* warm_level = std::getenv("FSIM_STATIC_KERNEL_WARM_LEVEL");
        const char* warm_fast_isel = std::getenv("FSIM_STATIC_KERNEL_WARM_FAST_ISEL");
        warm_machine_ = { warm_level == nullptr ? 0 : std::atoi(warm_level),
            warm_fast_isel != nullptr && std::string_view { warm_fast_isel } != "0" };
        warm_jit_ = make_jit(capture_, warm_machine_);
        if (const char* limit = std::getenv("FSIM_STATIC_KERNEL_OPT_MAX_SIZE")) {
            opt_max_size_ = static_cast<std::size_t>(std::strtoull(limit, nullptr, 10));
            const char* opt_level = std::getenv("FSIM_STATIC_KERNEL_OPT_LEVEL");
            opt_machine_ = { opt_level == nullptr ? 1 : std::atoi(opt_level), true };
            opt_jit_ = make_jit(capture_, opt_machine_);
        }
    }

    [[nodiscard]] bool ahead_of_time() const noexcept override
    {
        return ahead_of_time_;
    }

    /// FSIM_STATIC_KERNEL_PERF_MAP (diagnostic): a perf map for the
    /// templates. Sizes are distances to the next template (capped), so perf
    /// can attribute samples.
    static void write_perf_map(std::span<const StaticKernelTemplate> templates,
        std::span<const StaticKernelNativeEntry> entries)
    {
        if (std::getenv("FSIM_STATIC_KERNEL_PERF_MAP") == nullptr) {
            return;
        }
        std::vector<std::pair<std::uintptr_t, std::size_t>> ordered;
        for (std::size_t index = 0U; index < entries.size(); ++index) {
            if (entries[index] != nullptr) {
                ordered.emplace_back(
                    reinterpret_cast<std::uintptr_t>(entries[index]), index);
            }
        }
        std::ranges::sort(ordered);
        auto map = support::native_fs::open_ofstream(
            std::filesystem::path { "/tmp/perf-" + std::to_string(::getpid())
                + ".map" },
            std::ios::app);
        for (std::size_t index = 0U; index < ordered.size(); ++index) {
            const auto size = index + 1U < ordered.size()
                ? std::min<std::uintptr_t>(
                      ordered[index + 1U].first - ordered[index].first, 65536U)
                : 4096U;
            const auto& code = templates[ordered[index].second];
            map << std::hex << ordered[index].first << ' ' << size << std::dec
                << " sk_template_" << ordered[index].second << "_t"
                << static_cast<unsigned>(code.tier) << "_n"
                << (code.body != nullptr ? code.body->code.size() : 0U)
                << (code.two_state ? "_2s" : "") << '\n';
        }
    }

    std::vector<StaticKernelNativeEntry> available(
        std::span<const StaticKernelTemplate> templates,
        const StaticKernelNativeHelpers& helpers) override
    {
        std::vector<StaticKernelNativeEntry> entries(templates.size(), nullptr);
        if (!cache_) {
            return entries;
        }
        define_helpers(*jit_, helpers);
        std::vector<std::string> keys;
        keys.reserve(templates.size());
        for (const auto& code : templates) {
            keys.push_back(template_key(code));
        }
        load_index(index_key(keys));
        for (std::size_t index = 0U; index < templates.size(); ++index) {
            entries[index] = find(keys[index], templates[index]);
        }
        write_perf_map(templates, entries);
        return entries;
    }

    std::vector<StaticKernelNativeEntry> compile(
        std::span<const StaticKernelTemplate> templates,
        const StaticKernelNativeHelpers& helpers) override
    {
        const auto started = std::chrono::steady_clock::now();
        std::vector<StaticKernelNativeEntry> entries(templates.size(), nullptr);
        std::vector<std::string> keys;
        keys.reserve(templates.size());
        define_helpers(*jit_, helpers);
        for (std::size_t index = 0U; index < templates.size(); ++index) {
            keys.push_back(template_key(templates[index]));
            entries[index] = find(keys.back(), templates[index]);
        }
        if (ahead_of_time_ && index_key_.empty()) {
            // A later run asks for this same set first (available).
            index_key_ = index_key(keys);
        }
        std::size_t instructions = 0U;
        std::array<double, 3> tier_ms { };
        const auto optimized = [&](const std::size_t index) {
            static const std::size_t opt_min_size = [] {
                const char* text = std::getenv("FSIM_STATIC_KERNEL_OPT_MIN_SIZE");
                return text == nullptr ? std::size_t { 0 }
                    : static_cast<std::size_t>(std::strtoull(text, nullptr, 10));
            }();
            static const bool opt_full = std::getenv("FSIM_STATIC_KERNEL_OPT_FULL") != nullptr;
            // Full code that continues two-state code runs rarely.
            const bool full_code = !templates[index].two_state
                && templates[index].resume_points != nullptr
                && !templates[index].resume_points->empty();
            return opt_jit_ && templates[index].tier == 0U && (opt_full || !full_code)
                && templates[index].body->code.size() >= opt_min_size
                && templates[index].body->code.size() <= opt_max_size_
                && templates[index].body->resume_entries.empty();
        };
        for (const std::uint8_t tier : { std::uint8_t { 0U }, std::uint8_t { 1U }, std::uint8_t { 2U }, std::uint8_t { 3U } }) {
            std::vector<std::size_t> group;
            for (std::size_t index = 0U; index < templates.size(); ++index) {
                const auto effective = optimized(index) ? 3U : templates[index].tier;
                if (effective == tier && entries[index] == nullptr) {
                    group.push_back(index);
                    instructions += templates[index].body->code.size();
                }
            }
            if (!group.empty()) {
                const auto group_started = std::chrono::steady_clock::now();
                auto& jit = tier == 3U ? *opt_jit_ : tier == 2U ? *hot_jit_ : tier == 1U ? *warm_jit_ : *jit_;
                define_helpers(jit, helpers);
                compile_group(jit,
                    tier == 3U       ? opt_machine_
                        : tier == 2U ? hot_machine_
                        : tier == 1U ? warm_machine_
                                     : cold_machine_,
                    templates, keys, group, entries,
                    tier == 3U ? std::uint8_t { 1U } : tier);
                tier_ms[std::min<std::uint8_t>(tier, 2U)] += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - group_started).count();
            }
        }
        ++generation_;
        if (ahead_of_time_ && cache_ && !index_.empty()) {
            store_index();
        }
        const auto finished = std::chrono::steady_clock::now();
        write_perf_map(templates, entries);
        if (std::getenv("FSIM_PROFILE_PHASES") != nullptr) {
            const auto ms = [](auto from, auto to) {
                return std::chrono::duration<double, std::milli>(to - from).count();
            };
            std::cerr << "fsim-profile: static-kernel-codegen templates="
                      << templates.size() << " hot="
                      << std::ranges::count_if(templates,
                             [](const StaticKernelTemplate& entry) {
                                 return entry.tier == 2U;
                             })
                      << " warm="
                      << std::ranges::count_if(templates,
                             [](const StaticKernelTemplate& entry) {
                                 return entry.tier == 1U;
                             })
                      << " instructions=" << instructions
                      << " cold_ms=" << tier_ms[0] << " warm_ms=" << tier_ms[1]
                      << " hot_ms=" << tier_ms[2]
                      << " total_ms=" << ms(started, finished) << '\n';
            report_helper_sites();
        }
        return entries;
    }

private:
    /// The runtime helpers generated code calls by name
    /// (FunctionEmitter::helper_symbol), defined once per JIT.
    void define_helpers(llvm::orc::LLJIT& jit,
        const StaticKernelNativeHelpers& helpers)
    {
        if (std::ranges::find(helpers_defined_, &jit) != helpers_defined_.end()) {
            return;
        }
        llvm::orc::SymbolMap symbols;
        const auto add = [&](const char* name, auto* function) {
            symbols[jit.mangleAndIntern(name)] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(
                    reinterpret_cast<void*>(function)),
                llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable);
        };
        for (std::size_t op = 0U; op < helpers.evaluate_ops.size(); ++op) {
            const auto kind = static_cast<detail::KOp>(op);
            if (helpers.evaluate_ops[op] != nullptr) {
                add(instruction_helper_name("fsim_sk_evaluate_", kind).c_str(),
                    helpers.evaluate_ops[op]);
            }
            if (helpers.effect_ops[op] != nullptr) {
                add(instruction_helper_name("fsim_sk_effect_", kind).c_str(),
                    helpers.effect_ops[op]);
            }
            if (helpers.wide_ops[op] != nullptr) {
                add(instruction_helper_name("fsim_sk_wide_", kind).c_str(),
                    helpers.wide_ops[op]);
            }
        }
        add("fsim_sk_notify", helpers.notify);
        add("fsim_sk_notify_field", helpers.notify_field);
        add("fsim_sk_fail", helpers.fail);
        add("fsim_sk_member_due", helpers.member_due);
        add("fsim_sk_net_delay", helpers.net_delay);
        add("fsim_sk_sva_tick", helpers.sva_tick);
        add("fsim_sk_sva_ensure", helpers.sva_ensure);
        add("fsim_sk_sva_grow", helpers.sva_grow);
        add("fsim_sk_sva_post", helpers.sva_post);
        add("fsim_sk_sva_filter", helpers.sva_filter);
        add("fsim_sk_sva_check", helpers.sva_check);
        add("fsim_sk_sva_verify", helpers.sva_verify);
        add("fsim_sk_sva_bind", helpers.sva_bind);
        add("fsim_sk_sva_verify_outcome", helpers.sva_verify_outcome);
#if defined(__MINGW32__)
        add("___chkstk_ms", &___chkstk_ms);
#endif
        for (std::size_t kind = 0U; kind < helpers.generic_ops.size(); ++kind) {
            if (helpers.generic_ops[kind] != nullptr) {
                add(("fsim_sk_generic_" + std::to_string(kind)).c_str(),
                    helpers.generic_ops[kind]);
            }
            if (helpers.step_ops[kind] != nullptr) {
                add(("fsim_sk_step_" + std::to_string(kind)).c_str(),
                    helpers.step_ops[kind]);
            }
            if (helpers.thread_step_ops[kind] != nullptr) {
                add(("fsim_sk_thread_step_" + std::to_string(kind)).c_str(),
                    helpers.thread_step_ops[kind]);
            }
            if (helpers.hosted_step_ops[kind] != nullptr) {
                add(("fsim_sk_hosted_step_" + std::to_string(kind)).c_str(),
                    helpers.hosted_step_ops[kind]);
            }
            if (helpers.boundary_ops[kind] != nullptr) {
                add(("fsim_sk_boundary_" + std::to_string(kind)).c_str(),
                    helpers.boundary_ops[kind]);
            }
        }
        if (auto error = jit.getMainJITDylib().define(
                llvm::orc::absoluteSymbols(std::move(symbols)))) {
            throw std::runtime_error(error_text(std::move(error)));
        }
        helpers_defined_.push_back(&jit);
    }

    /// Keeps the object an ahead-of-time compilation produces.
    class ObjectCapture final : public llvm::ObjectCache {
    public:
        void notifyObjectCompiled(const llvm::Module*,
            const llvm::MemoryBufferRef object) override
        {
            if (enabled) {
                bytes.assign(object.getBufferStart(), object.getBufferEnd());
            }
        }

        std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module*) override
        {
            return nullptr;
        }

        bool enabled { };
        std::string bytes;
    };

    /// A tier's backend settings.
    struct TierMachine {
        int level { };
        bool fast_isel { };
    };

    [[nodiscard]] static llvm::orc::JITTargetMachineBuilder target_builder(
        const TierMachine& machine)
    {
        auto target = llvm::orc::JITTargetMachineBuilder::detectHost();
        if (!target) {
            throw std::runtime_error(error_text(target.takeError()));
        }
        target->setCodeGenOptLevel(machine.level <= 0 ? llvm::CodeGenOptLevel::None
                : machine.level == 1                   ? llvm::CodeGenOptLevel::Less
                : machine.level == 2 ? llvm::CodeGenOptLevel::Default
                                     : llvm::CodeGenOptLevel::Aggressive);
        target->getOptions().EnableFastISel = machine.fast_isel;
        return std::move(*target);
    }

    [[nodiscard]] static std::unique_ptr<llvm::orc::LLJIT> make_jit(
        ObjectCapture& capture, const TierMachine& machine)
    {
        llvm::orc::LLJITBuilder builder;
        builder.setJITTargetMachineBuilder(target_builder(machine))
            .setNumCompileThreads(0U)
            .setCompileFunctionCreator(
                [&capture](llvm::orc::JITTargetMachineBuilder target)
                    -> llvm::Expected<std::unique_ptr<
                        llvm::orc::IRCompileLayer::IRCompiler>> {
                    auto machine = target.createTargetMachine();
                    if (!machine) {
                        return machine.takeError();
                    }
                    return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(
                        std::move(*machine), &capture);
                });
#if defined(_WIN32)
        builder.setObjectLinkingLayerCreator(
            [](llvm::orc::ExecutionSession& session)
                -> llvm::Expected<std::unique_ptr<llvm::orc::ObjectLayer>> {
                auto layer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(
                    session, [](const llvm::MemoryBuffer&) {
                        return std::make_unique<WindowsUnwindMemoryManager>();
                    });
                // As LLJIT's default layer for COFF objects.
                layer->setOverrideObjectFlagsWithResponsibilityFlags(true);
                layer->setAutoClaimResponsibilityForObjectSymbols(true);
                return std::unique_ptr<llvm::orc::ObjectLayer> { std::move(layer) };
            });
#endif
        auto jit = builder.create();
        if (!jit) {
            throw std::runtime_error(error_text(jit.takeError()));
        }
        return std::move(*jit);
    }

    struct BuiltModule {
        std::unique_ptr<llvm::LLVMContext> context;
        std::unique_ptr<llvm::Module> module;
    };

    /// The module of one function per template of `group`, its IR passes
    /// run. Touches no state of this generator but the templates' resume
    /// points, so chunks of a group build concurrently.
    [[nodiscard]] BuiltModule build_module(const llvm::DataLayout& layout,
        std::span<const StaticKernelTemplate> templates,
        const std::vector<std::string>& keys, const std::vector<std::size_t>& group,
        const std::uint8_t tier, const std::size_t chunk) const
    {
        auto context = std::make_unique<llvm::LLVMContext>();
        // Value and block names only serve the IR dump.
        context->setDiscardValueNames(std::getenv("FSIM_STATIC_KERNEL_DUMP_IR") == nullptr);
        auto module = std::make_unique<llvm::Module>("fsim_static_kernel", *context);
        module->setDataLayout(layout);
        auto* i32 = llvm::Type::getInt32Ty(*context);
        auto* ptr = llvm::PointerType::getUnqual(*context);
        auto* type = llvm::FunctionType::get(i32, { ptr, ptr }, false);
        std::vector<std::string> names;
        names.reserve(group.size());
        for (const auto index : group) {
            names.push_back(symbol_name(keys[index]));
            auto* function = llvm::Function::Create(type,
                llvm::Function::ExternalLinkage, names.back(), *module);
            function->addParamAttr(1, llvm::Attribute::NoAlias);
            function->addParamAttr(0, llvm::Attribute::NoAlias);
            function->addFnAttr(llvm::Attribute::NoUnwind);
#if defined(_WIN32)
            // Windows x64 expects unwind data for every non-leaf function;
            // without it nothing can walk through kernel frames.
            function->setUWTableKind(llvm::UWTableKind::Async);
#endif
            // Exits flush local registers (failure_block_), so VHDL bodies,
            // which deoptimize from the register file, may use them too
            // (FSIM_STATIC_KERNEL_VHDL_LOCALS=1). Off by default: with the
            // unoptimized backend they cost compile time and gain nothing.
            static const bool vhdl_locals = [] {
                const char* text = std::getenv("FSIM_STATIC_KERNEL_VHDL_LOCALS");
                return text != nullptr && std::string_view { text } == "1";
            }();
            FunctionEmitter emitter(*module, *function, *templates[index].body,
                vhdl_locals || !templates[index].vhdl, tier == 2U,
                templates[index].vhdl,
                templates[index].two_state,
                templates[index].resume_points, templates[index].vhdl);
            // Cold tiering is static, so small cold templates include the
            // most frequently run members: outlining them costs 3-5% run
            // instructions, and it is runtime-neutral only from about 1024
            // instructions. At 1024 it saves 2-3% of native codegen time
            // with no run-time cost (quiet-host A/B, 2026-11-01).
            // FSIM_STATIC_KERNEL_OUTLINE_STORES=N: the smallest outlined
            // template (0: none; default 1024).
            static const std::size_t outline_size = [] {
                const char* text = std::getenv("FSIM_STATIC_KERNEL_OUTLINE_STORES");
                return text == nullptr ? std::size_t { 1024 }
                                       : static_cast<std::size_t>(
                                             std::strtoull(text, nullptr, 10));
            }();
            emitter.outline_stores(outline_size != 0U && tier == 0U
                && templates[index].body->code.size() >= outline_size);
            emitter.emit();
        }
        if (std::getenv("FSIM_STATIC_KERNEL_VERIFY") != nullptr
            && llvm::verifyModule(*module, &llvm::errs())) {
            throw std::runtime_error("static kernel module is invalid");
        }
        {
            llvm::LoopAnalysisManager loop_analyses;
            llvm::FunctionAnalysisManager function_analyses;
            llvm::CGSCCAnalysisManager cgscc_analyses;
            llvm::ModuleAnalysisManager module_analyses;
            llvm::PassBuilder builder;
            builder.registerModuleAnalyses(module_analyses);
            builder.registerCGSCCAnalyses(cgscc_analyses);
            builder.registerFunctionAnalyses(function_analyses);
            builder.registerLoopAnalyses(loop_analyses);
            builder.crossRegisterProxies(loop_analyses, function_analyses,
                cgscc_analyses, module_analyses);
            llvm::ModulePassManager pipeline;
            // Cold templates compiled while simulating only need registers
            // promoted for the fast instruction selector. Ahead of time they
            // also get their control flow simplified: the unoptimized backend
            // spills live values at every block boundary, and simplifycfg
            // merges straight-line blocks (original_codec simulate -0.14 s
            // for +0.2 s of elaborate).
            const char* custom = std::getenv(tier == 2U
                    && std::getenv("FSIM_STATIC_KERNEL_HOT_PIPELINE") != nullptr
                ? "FSIM_STATIC_KERNEL_HOT_PIPELINE" : "FSIM_STATIC_KERNEL_PIPELINE");
            if (auto error = builder.parsePassPipeline(pipeline,
                    custom != nullptr  ? custom
                        : tier != 0U   ? "function(sroa,early-cse,simplifycfg)"
                        : ahead_of_time_ ? "function(sroa,simplifycfg)"
                                         : "function(sroa)")) {
                throw std::runtime_error(error_text(std::move(error)));
            }
            pipeline.run(*module, module_analyses);
        }
        // simplifycfg merges branch conditions into i1 selects with a
        // constant arm, which the fast instruction selector cannot select:
        // it hands the rest of the block to the full selector. The same
        // value is an and/or with the other arm frozen (a select does not
        // propagate the poison of the arm it does not choose), frozen as i8
        // because the fast selector freezes only legal types.
        if (tier == 0U) {
            for (auto& function : *module) {
                for (auto& block : function) {
                    for (auto next = block.begin(); next != block.end();) {
                        auto* select = llvm::dyn_cast<llvm::SelectInst>(&*next++);
                        if (select == nullptr || !select->getType()->isIntegerTy(1)) {
                            continue;
                        }
                        auto* const when_true
                            = llvm::dyn_cast<llvm::ConstantInt>(select->getTrueValue());
                        auto* const when_false
                            = llvm::dyn_cast<llvm::ConstantInt>(select->getFalseValue());
                        llvm::IRBuilder<> rewrite(select);
                        auto* const condition = select->getCondition();
                        const auto freeze = [&](llvm::Value* const value) {
                            return rewrite.CreateTrunc(
                                rewrite.CreateFreeze(
                                    rewrite.CreateZExt(value, rewrite.getInt8Ty())),
                                rewrite.getInt1Ty());
                        };
                        llvm::Value* replacement = nullptr;
                        if (when_true != nullptr) {
                            auto* const other = freeze(select->getFalseValue());
                            replacement = when_true->isOne()
                                ? rewrite.CreateOr(condition, other)
                                : rewrite.CreateAnd(rewrite.CreateNot(condition), other);
                        } else if (when_false != nullptr) {
                            auto* const other = freeze(select->getTrueValue());
                            replacement = when_false->isZero()
                                ? rewrite.CreateAnd(condition, other)
                                : rewrite.CreateOr(rewrite.CreateNot(condition), other);
                        }
                        if (replacement != nullptr) {
                            select->replaceAllUsesWith(replacement);
                            select->eraseFromParent();
                        }
                    }
                }
            }
        }
        if (const char* dump = std::getenv("FSIM_STATIC_KERNEL_DUMP_IR")) {
            // Diagnostic: the IR handed to instruction selection, one file per
            // group, preceded by the template index of each function.
            auto file = support::native_fs::open_ofstream(
                std::filesystem::path { dump }
                / ("static-kernel-" + std::to_string(generation_)
                    + (chunk != 0U ? "-" + std::to_string(chunk) : std::string { })
                    + (tier == 2U ? "-hot.ll" : tier == 1U ? "-warm.ll" : "-cold.ll")));
            std::string text;
            llvm::raw_string_ostream stream(text);
            module->print(stream, nullptr);
            for (std::size_t slot = 0U; slot < group.size(); ++slot) {
                file << "; template " << group[slot] << " = @" << names[slot]
                     << '\n';
            }
            file << text;
        }
        return { std::move(context), std::move(module) };
    }

    /// Sets the entries of `group` from `jit` (compiling what it holds as
    /// IR) and records them as known.
    void bind_entries(llvm::orc::LLJIT& jit,
        std::span<const StaticKernelTemplate> templates,
        const std::vector<std::string>& keys, const std::vector<std::size_t>& group,
        std::vector<StaticKernelNativeEntry>& entries)
    {
        for (const auto index : group) {
            auto symbol = jit.lookup(symbol_name(keys[index]));
            if (!symbol) {
                capture_.enabled = false;
                throw std::runtime_error(error_text(symbol.takeError()));
            }
            entries[index] = symbol->toPtr<StaticKernelNativeEntry>();
            auto& known = known_[keys[index]];
            known.entry = entries[index];
            if (templates[index].two_state && templates[index].resume_points != nullptr) {
                known.resume_points = *templates[index].resume_points;
            }
        }
    }

    /// `group` in chunks of similar instruction counts that compile on
    /// separate threads (each keeps template order, so its object is the
    /// same for the same templates and job count). A chunk smaller than
    /// a thread's setup is worth stays with another.
    [[nodiscard]] std::vector<std::vector<std::size_t>> split_group(
        std::span<const StaticKernelTemplate> templates,
        const std::vector<std::size_t>& group, const std::uint8_t tier) const
    {
        // The optimizing backend costs about twenty times as much per
        // instruction as the fast one.
        const std::size_t minimum_chunk = tier == 2U ? 256U : 4096U;
        std::size_t total = 0U;
        for (const auto index : group) {
            total += templates[index].body->code.size();
        }
        const auto count = std::min({ jobs_, group.size(),
            std::max<std::size_t>(1U, total / minimum_chunk) });
        if (count <= 1U) {
            return { group };
        }
        auto order = group;
        std::ranges::stable_sort(order, [&](const std::size_t left, const std::size_t right) {
            return templates[left].body->code.size() > templates[right].body->code.size();
        });
        std::vector<std::vector<std::size_t>> chunks(count);
        std::vector<std::size_t> load(count, 0U);
        for (const auto index : order) {
            const auto lightest = static_cast<std::size_t>(
                std::ranges::min_element(load) - load.begin());
            chunks[lightest].push_back(index);
            load[lightest] += templates[index].body->code.size();
        }
        for (auto& chunk : chunks) {
            std::ranges::sort(chunk);
        }
        return chunks;
    }

    void compile_group(llvm::orc::LLJIT& jit, const TierMachine& machine,
        std::span<const StaticKernelTemplate> templates,
        const std::vector<std::string>& keys,
        const std::vector<std::size_t>& all,
        std::vector<StaticKernelNativeEntry>& entries, const std::uint8_t tier)
    {
        // Templates with equal keys share one function.
        std::vector<std::size_t> group;
        std::unordered_map<std::string_view, std::size_t> first;
        for (const auto index : all) {
            if (first.emplace(keys[index], index).second) {
                group.push_back(index);
            }
        }
        const bool store = ahead_of_time_ && cache_;
        const auto chunks = split_group(templates, group, tier);
        if (chunks.size() == 1U) {
            auto built = build_module(jit.getDataLayout(), templates, keys, group, tier, 0U);
            if (auto error = jit.addIRModule(llvm::orc::ThreadSafeModule(
                    std::move(built.module), std::move(built.context)))) {
                throw std::runtime_error(error_text(std::move(error)));
            }
            capture_.enabled = store;
            capture_.bytes.clear();
            bind_entries(jit, templates, keys, group, entries);
            capture_.enabled = false;
            if (store && !capture_.bytes.empty()) {
                store_group(keys, group, capture_.bytes);
            }
        } else {
            // Each chunk builds and compiles its module on its own thread,
            // with its own context and target machine; the objects are then
            // linked and stored in chunk order.
            std::vector<std::unique_ptr<llvm::MemoryBuffer>> objects(chunks.size());
            std::vector<std::exception_ptr> failures(chunks.size());
            std::atomic_size_t next { 0U };
            const auto layout = jit.getDataLayout();
            const auto work = [&] {
                for (auto chunk = next.fetch_add(1U); chunk < chunks.size();
                     chunk = next.fetch_add(1U)) {
                    try {
                        auto built = build_module(
                            layout, templates, keys, chunks[chunk], tier, chunk);
                        auto target = target_builder(machine).createTargetMachine();
                        if (!target) {
                            throw std::runtime_error(error_text(target.takeError()));
                        }
                        llvm::orc::SimpleCompiler compiler(**target);
                        auto object = compiler(*built.module);
                        if (!object) {
                            throw std::runtime_error(error_text(object.takeError()));
                        }
                        objects[chunk] = std::move(*object);
                    } catch (...) {
                        failures[chunk] = std::current_exception();
                    }
                }
            };
            {
                std::vector<std::jthread> workers;
                for (std::size_t worker = 1U; worker < chunks.size(); ++worker) {
                    workers.emplace_back(work);
                }
                work();
            }
            for (const auto& failure : failures) {
                if (failure) {
                    std::rethrow_exception(failure);
                }
            }
            for (std::size_t chunk = 0U; chunk < chunks.size(); ++chunk) {
                std::string bytes;
                if (store) {
                    bytes = std::string { objects[chunk]->getBuffer() };
                }
                if (auto error = jit.addObjectFile(std::move(objects[chunk]))) {
                    throw std::runtime_error(error_text(std::move(error)));
                }
                bind_entries(jit, templates, keys, chunks[chunk], entries);
                if (store) {
                    store_group(keys, chunks[chunk], bytes);
                }
            }
        }
        for (const auto index : all) {
            const auto found = known_.find(keys[index]);
            entries[index] = found->second.entry;
            restore_resume_points(found->second, templates[index]);
        }
    }

    // --- Code built ahead of time ---------------------------------------
    //
    // An ahead-of-time compilation stores each group's object under a key of
    // its templates, and for each template a record naming that object, its
    // function and its resume points, under the template's key (its body,
    // tier and form, and this code generator's identity).

    struct Known {
        StaticKernelNativeEntry entry { };
        std::vector<std::uint32_t> resume_points;
    };

    [[nodiscard]] static std::string symbol_name(const std::string& key)
    {
        return "fsim_sk_" + key;
    }

    static void restore_resume_points(
        const Known& known, const StaticKernelTemplate& code)
    {
        if (code.two_state && code.resume_points != nullptr) {
            *code.resume_points = known.resume_points;
        }
    }

    [[nodiscard]] static std::string codegen_identity()
    {
        // The host CPU and its features do not change within a process.
        static const std::string host = [] {
            std::string text = llvm::sys::getProcessTriple() + ";";
            text += std::string { llvm::sys::getHostCPUName() } + ";";
            for (const auto& [feature, enabled] : llvm::sys::getHostCPUFeatures()) {
                if (enabled) {
                    text += feature.str() + ",";
                }
            }
            return text;
        }();
        std::string identity = "fsim-static-kernel-object-v1;";
        identity += __DATE__ " " __TIME__ ";";
        identity += LLVM_VERSION_STRING ";";
        identity += host;
        for (const char* name : { "FSIM_STATIC_KERNEL_LOCAL_REGISTERS",
                 "FSIM_STATIC_KERNEL_VHDL_LOCALS", "FSIM_STATIC_KERNEL_OPT_MAX_SIZE",
                 "FSIM_STATIC_KERNEL_OPT_MIN_SIZE", "FSIM_STATIC_KERNEL_OPT_FULL",
                 "FSIM_STATIC_KERNEL_OPT_LEVEL",
                 "FSIM_STATIC_KERNEL_CODEGEN_LEVEL", "FSIM_STATIC_KERNEL_NO_FAST_ISEL",
                 "FSIM_STATIC_KERNEL_HOT_LEVEL", "FSIM_STATIC_KERNEL_HOT_FAST_ISEL",
                 "FSIM_STATIC_KERNEL_WARM_LEVEL", "FSIM_STATIC_KERNEL_WARM_FAST_ISEL",
                 "FSIM_STATIC_KERNEL_HOT_THRESHOLD",
                 "FSIM_STATIC_KERNEL_PIPELINE", "FSIM_STATIC_KERNEL_HOT_PIPELINE",
                 "FSIM_STATIC_KERNEL_LLVM_ARGS", "FSIM_STATIC_KERNEL_OUTLINE_STORES" }) {
            const char* value = std::getenv(name);
            identity += ";";
            identity += name;
            identity += "=";
            identity += value != nullptr ? value : "-";
        }
        return identity;
    }

    /// Everything the function emitted for a template depends on.
    [[nodiscard]] std::string template_key(const StaticKernelTemplate& code) const
    {
        std::string text;
        const auto append = [&](const auto& value) {
            text.append(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        const auto append_all = [&](const auto& values) {
            append(values.size());
            for (const auto& value : values) {
                append(value);
            }
        };
        const auto& body = *code.body;
        text.reserve(64U + body.code.size() * sizeof(KInst)
            + body.register_widths.size() * sizeof(std::uint32_t)
            + body.resume_entries.size() * sizeof(std::uint32_t));
        append(identity_hash_.high64);
        append(identity_hash_.low64);
        append(code.tier);
        append(code.vhdl);
        append(code.two_state);
        if (!code.two_state && code.resume_points != nullptr) {
            append_all(*code.resume_points);
        } else {
            append(std::size_t { 0 });
        }
        append(body.code.size());
        for (const auto& inst : body.code) {
            append(inst.op);
            append(inst.sub);
            append(inst.flags);
            append(inst.width);
            append(inst.d);
            append(inst.x);
            append(inst.y);
            append(inst.z);
            append(inst.offset);
            append(inst.aux);
            append(inst.imm_a);
            append(inst.imm_b);
        }
        append(body.registers.size());
        append_all(body.register_widths);
        append(body.indices.size());
        for (const auto& index : body.indices) {
            append(index.index);
            append(index.left);
            append(index.right);
            append(index.base_offset);
            append(index.strict);
        }
        append(body.parts.size());
        for (const auto& part : body.parts) {
            append(part.left);
            append(part.right);
            append(part.base);
            append(part.base_offset);
            append(part.width);
            append(part.increasing);
            append(part.source_descending);
        }
        append(body.concat.size());
        for (const auto& operand : body.concat) {
            append(operand.reg);
            append(operand.width);
            append(operand.unknown);
        }
        append_all(body.wide_base);
        append(body.wide_constants.size());
        for (const auto& word : body.wide_constants) {
            append(word.a);
            append(word.b);
        }
        // The directives' plans the attempt automata are generated from, and
        // whether they are checked (FSIM_KERNEL_SVA_CHECK).
        append(body.sva_plans.size());
        for (const auto& plan : body.sva_plans) {
            append(plan.root);
            append(plan.every_match);
            append_all(plan.clock_guards);
            append(plan.sequences.size());
            for (const auto& sequence : plan.sequences) {
                append(sequence.counters);
                append(sequence.state_base);
                append(sequence.code.size());
                for (const auto& step : sequence.code) {
                    append(step.op);
                    append(step.a);
                    append(step.b);
                    append(step.c);
                    append(step.lo);
                    append(step.hi);
                    append(step.line);
                    append(step.column);
                    append(step.lo_slot);
                    append(step.hi_slot);
                }
            }
            append(plan.properties.size());
            for (const auto& property : plan.properties) {
                append(property.kind);
                append(property.overlapped);
                append(property.sequence);
                append(property.child);
                append_all(property.children);
                append(property.guard);
                append(property.lo);
                append(property.lo_slot);
                append(property.hi);
                append(property.hi_slot);
                append(property.line);
                append(property.column);
            }
            append_all(plan.guard_positions);
        }
        append(sva_check_enabled());
        append(sva_verify_enabled());
        append(body.entry);
        append_all(body.return_targets);
        append(body.shadow_base);
        append_all(body.tracked);
        append_all(body.u_mode);
        append_all(body.u_operand_begin);
        append_all(body.u_operands);
        append(body.call_stack_base);
        append_all(body.resume_entries);
        // A content key for this process's cache and the ahead-of-time
        // image (not an integrity check: the image's objects are checksummed
        // by the cache). Keys are computed for every template a simulation
        // restores, so a fast 128-bit hash.
        const auto hash = llvm::xxh3_128bits(llvm::ArrayRef {
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
        static constexpr char digits[] = "0123456789abcdef";
        std::string key = "x3";
        key.resize(2U + 32U);
        for (std::size_t nibble = 0U; nibble < 16U; ++nibble) {
            key[2U + nibble] = digits[(hash.high64 >> (60U - 4U * nibble)) & 15U];
            key[18U + nibble] = digits[(hash.low64 >> (60U - 4U * nibble)) & 15U];
        }
        return key;
    }

    /// The template's function: compiled in this process or built ahead
    /// of time (loaded from the cache); null otherwise.
    [[nodiscard]] StaticKernelNativeEntry find(
        const std::string& key, const StaticKernelTemplate& code)
    {
        if (const auto found = known_.find(key); found != known_.end()) {
            restore_resume_points(found->second, code);
            return found->second.entry;
        }
        if (!cache_ || ahead_of_time_) {
            return nullptr;
        }
        const auto record = records_.find(key);
        if (record == records_.end()) {
            return nullptr;
        }
        const auto& object_key = record->second.object;
        Known known;
        known.resume_points = record->second.resume_points;
        std::error_code error;
        if (!loaded_objects_.contains(object_key)) {
            const auto object = cache_->load(object_key, error);
            if (!object) {
                return nullptr;
            }
            auto buffer = llvm::MemoryBuffer::getMemBufferCopy(
                llvm::StringRef { reinterpret_cast<const char*>(object->data()),
                    object->size() },
                "fsim-static-kernel-" + object_key);
            if (auto failure = jit_->addObjectFile(std::move(buffer))) {
                llvm::consumeError(std::move(failure));
                return nullptr;
            }
            loaded_objects_.insert(object_key);
        }
        auto symbol = jit_->lookup(symbol_name(key));
        if (!symbol) {
            llvm::consumeError(symbol.takeError());
            return nullptr;
        }
        known.entry = symbol->toPtr<StaticKernelNativeEntry>();
        restore_resume_points(known, code);
        return known_.emplace(key, std::move(known)).first->second.entry;
    }

    void store_group(const std::vector<std::string>& keys,
        const std::vector<std::size_t>& group, const std::string& bytes)
    {
        CacheKeyBuilder object_key;
        object_key.add("codegen", identity_);
        for (const auto index : group) {
            object_key.add("template", keys[index]);
        }
        const auto object = object_key.finish();
        std::error_code error;
        if (!cache_->store(object,
                std::as_bytes(std::span { bytes.data(), bytes.size() }),
                error)) {
            return;
        }
        for (const auto index : group) {
            index_.push_back({ keys[index], object,
                known_.at(keys[index]).resume_points });
        }
    }

    struct Record {
        std::string object;
        std::vector<std::uint32_t> resume_points;
    };

    struct IndexEntry {
        std::string key;
        std::string object;
        std::vector<std::uint32_t> resume_points;
    };

    /// The index of an ahead-of-time build, by the keys of the templates
    /// its first compilation (all of a kernel's) held.
    [[nodiscard]] std::string index_key(const std::vector<std::string>& keys) const
    {
        CacheKeyBuilder key;
        key.add("codegen", identity_);
        key.add("index", "fsim-static-kernel-index-v1");
        for (const auto& template_key : keys) {
            key.add("template", template_key);
        }
        return key.finish();
    }

    // One line per template: "<key> <object key> <count> <resume point>...".
    void store_index()
    {
        std::string text;
        for (const auto& entry : index_) {
            text += entry.key + " " + entry.object + " "
                + std::to_string(entry.resume_points.size());
            for (const auto point : entry.resume_points) {
                text += " " + std::to_string(point);
            }
            text += "\n";
        }
        std::error_code error;
        static_cast<void>(cache_->store(index_key_,
            std::as_bytes(std::span { text.data(), text.size() }), error));
    }

    void load_index(const std::string& key)
    {
        std::error_code error;
        const auto payload = cache_->load(key, error);
        if (!payload) {
            return;
        }
        std::istringstream lines { std::string {
            reinterpret_cast<const char*>(payload->data()), payload->size() } };
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream fields { line };
            std::string template_key;
            Record record;
            std::size_t count { };
            if (!(fields >> template_key >> record.object >> count)
                || count > (std::size_t { 1 } << 24U)) {
                records_.clear();
                return;
            }
            record.resume_points.resize(count);
            for (auto& point : record.resume_points) {
                if (!(fields >> point)) {
                    records_.clear();
                    return;
                }
            }
            records_.insert_or_assign(std::move(template_key), std::move(record));
        }
    }

    bool ahead_of_time_ { };
    std::optional<ObjectCache> cache_;
    std::string identity_;
    llvm::XXH128_hash_t identity_hash_ { };
    std::unordered_map<std::string, Known> known_;
    std::unordered_map<std::string, Record> records_;
    std::vector<IndexEntry> index_;
    std::string index_key_;
    std::unordered_set<std::string> loaded_objects_;
    ObjectCapture capture_;
    std::vector<const llvm::orc::LLJIT*> helpers_defined_;
    std::unique_ptr<llvm::orc::LLJIT> jit_;
    std::unique_ptr<llvm::orc::LLJIT> hot_jit_;
    std::unique_ptr<llvm::orc::LLJIT> warm_jit_;
    std::unique_ptr<llvm::orc::LLJIT> opt_jit_;
    TierMachine cold_machine_;
    TierMachine hot_machine_;
    TierMachine warm_machine_;
    TierMachine opt_machine_;
    /// Threads a group compiles on.
    std::size_t jobs_ { 1U };
    std::size_t opt_max_size_ { };
    std::size_t generation_ { };
};

} // namespace

std::shared_ptr<runtime::simir::StaticKernelCodegen> make_static_kernel_codegen(
    StaticKernelCodegenOptions options)
{
    return std::make_shared<LlvmStaticKernelCodegen>(std::move(options));
}

namespace {

thread_local std::size_t ahead_of_time_scopes { };
thread_local std::uint32_t ahead_of_time_jobs { 1U };

} // namespace

StaticKernelAheadOfTimeScope::StaticKernelAheadOfTimeScope(
    const std::uint32_t jobs) noexcept
    : previous_jobs_ { ahead_of_time_jobs }
{
    ++ahead_of_time_scopes;
    ahead_of_time_jobs = jobs;
}

StaticKernelAheadOfTimeScope::~StaticKernelAheadOfTimeScope()
{
    --ahead_of_time_scopes;
    ahead_of_time_jobs = previous_jobs_;
}

bool StaticKernelAheadOfTimeScope::active() noexcept
{
    return ahead_of_time_scopes != 0U;
}

std::uint32_t StaticKernelAheadOfTimeScope::jobs() noexcept
{
    return ahead_of_time_jobs;
}

std::string native_host_fingerprint()
{
    // Names the host the v4 kernel's native code is generated for: LLVM
    // version, target triple, CPU and enabled CPU features, in a stable order.
    std::vector<std::string> features;
    for (const auto& [feature, enabled] : llvm::sys::getHostCPUFeatures()) {
        if (enabled) {
            features.push_back(feature.str());
        }
    }
    std::ranges::sort(features);
    CacheKeyBuilder builder;
    builder.add("kind", "fsim-static-kernel-host-v1");
    builder.add("build-configuration", FSIM_BUILD_CONFIGURATION);
    builder.add("llvm-version", LLVM_VERSION_STRING);
    builder.add("target", llvm::sys::getProcessTriple());
    builder.add("cpu", std::string { llvm::sys::getHostCPUName() });
    builder.add("feature-count", std::to_string(features.size()));
    for (const auto& feature : features) {
        builder.add("feature", feature);
    }
    return builder.finish();
}

} // namespace fsim::compiler
