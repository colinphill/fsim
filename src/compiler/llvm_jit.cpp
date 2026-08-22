// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit.hpp"
#include "llvm_jit_internal.hpp"

#include "fsim/compiler/object_cache.hpp"

#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/ObjectCache.h>
#if defined(__linux__)
#include <llvm/ExecutionEngine/JITLink/JITLink.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#endif
#include <llvm/ExecutionEngine/Orc/AbsoluteSymbols.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#if defined(__linux__)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Format.h>
#endif
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace fsim::compiler {

std::string_view to_string(const JitOptimizationLevel optimization) noexcept
{
    switch (optimization) {
    case JitOptimizationLevel::o0:
        return "O0";
    case JitOptimizationLevel::o1:
        return "O1";
    case JitOptimizationLevel::o2:
        return "O2";
    }
    return "O2";
}

namespace {

    constexpr std::array<std::byte, 8> kJitMetadataMagic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' },
        std::byte { 'M' }, std::byte { 'J' }, std::byte { 'M' },
        std::byte { '3' }, std::byte { 0 }
    };
    constexpr std::array<std::byte, 8> kJitCacheRecordMagic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' },
        std::byte { 'M' }, std::byte { 'J' }, std::byte { 'O' },
        std::byte { '3' }, std::byte { 0 }
    };
    constexpr std::uint32_t kJitMetadataSchema = 1U;
    constexpr std::uint32_t kJitCacheRecordSchema = 1U;
    constexpr std::size_t kMaximumJitMetadataBytes = 64U * 1024U * 1024U;

    class JitMetadataWriter final {
    public:
        void append_u8(const std::uint8_t value)
        {
            bytes_.push_back(static_cast<std::byte>(value));
        }

        void append_u32(const std::uint32_t value)
        {
            for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
                append_u8(static_cast<std::uint8_t>(value >> shift));
            }
        }

        void append_u64(const std::uint64_t value)
        {
            for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
                append_u8(static_cast<std::uint8_t>(value >> shift));
            }
        }

        void append_bytes(const std::span<const std::byte> bytes)
        {
            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        }

        void append_u32_vector(const std::span<const std::uint32_t> values)
        {
            if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
                throw LlvmJitError("LLVM cache metadata vector is too large");
            }
            append_u32(static_cast<std::uint32_t>(values.size()));
            for (const auto value : values) {
                append_u32(value);
            }
        }

        void patch_u32(const std::size_t offset, const std::uint32_t value)
        {
            if (offset > bytes_.size() || bytes_.size() - offset < 4U) {
                throw LlvmJitError("LLVM cache metadata patch is out of range");
            }
            for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
                bytes_[offset + byte] = static_cast<std::byte>(
                    static_cast<std::uint8_t>(value >> (byte * 8U)));
            }
        }

        [[nodiscard]] std::vector<std::byte> finish()
        {
            if (bytes_.size() > kMaximumJitMetadataBytes
                || bytes_.size()
                    > std::numeric_limits<std::uint32_t>::max()) {
                throw LlvmJitError("LLVM cache metadata is too large");
            }
            return std::move(bytes_);
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return bytes_.size();
        }

    private:
        std::vector<std::byte> bytes_;
    };

    class JitMetadataReader final {
    public:
        explicit JitMetadataReader(const std::span<const std::byte> bytes)
            : bytes_(bytes)
        {
        }

        [[nodiscard]] bool read_u8(std::uint8_t& value)
        {
            if (offset_ == bytes_.size()) {
                return false;
            }
            value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
            return true;
        }

        [[nodiscard]] bool read_u32(std::uint32_t& value)
        {
            value = 0U;
            for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
                std::uint8_t part { };
                if (!read_u8(part)) {
                    return false;
                }
                value |= static_cast<std::uint32_t>(part) << (byte * 8U);
            }
            return true;
        }

        [[nodiscard]] bool read_u64(std::uint64_t& value)
        {
            value = 0U;
            for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
                std::uint8_t part { };
                if (!read_u8(part)) {
                    return false;
                }
                value |= static_cast<std::uint64_t>(part) << (byte * 8U);
            }
            return true;
        }

        [[nodiscard]] bool read_bytes(
            const std::span<const std::byte>& expected)
        {
            if (offset_ > bytes_.size()
                || expected.size() > bytes_.size() - offset_
                || !std::equal(
                    expected.begin(), expected.end(),
                    bytes_.begin() + static_cast<std::ptrdiff_t>(offset_))) {
                return false;
            }
            offset_ += expected.size();
            return true;
        }

        [[nodiscard]] bool read_u32_vector(
            std::vector<std::uint32_t>& values)
        {
            std::uint32_t count { };
            if (!read_u32(count)
                || count > (bytes_.size() - offset_) / 4U) {
                return false;
            }
            values.resize(count);
            for (auto& value : values) {
                if (!read_u32(value)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool finished() const noexcept
        {
            return offset_ == bytes_.size();
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    struct JitCacheRecordView {
        std::span<const std::byte> metadata;
        std::span<const std::byte> object;
    };

    [[nodiscard]] std::vector<std::byte> make_jit_cache_record(
        const std::span<const std::byte> object,
        const std::span<const std::byte> metadata)
    {
        const auto object_offset = (24U + metadata.size() + 7U) & ~std::size_t { 7U };
        if (metadata.size() > kMaximumJitMetadataBytes
            || metadata.size() > std::numeric_limits<std::uint32_t>::max()
            || object.size() > std::numeric_limits<std::uint64_t>::max()
            || object.size()
                > std::numeric_limits<std::size_t>::max() - object_offset) {
            throw LlvmJitError("LLVM native cache record is too large");
        }
        std::vector<std::byte> result;
        result.reserve(object_offset + object.size());
        result.insert(
            result.end(),
            kJitCacheRecordMagic.begin(), kJitCacheRecordMagic.end());
        const auto append_u32 = [&](const std::uint32_t value) {
            for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
                result.push_back(static_cast<std::byte>(
                    static_cast<std::uint8_t>(value >> shift)));
            }
        };
        const auto append_u64 = [&](const std::uint64_t value) {
            for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
                result.push_back(static_cast<std::byte>(
                    static_cast<std::uint8_t>(value >> shift)));
            }
        };
        append_u32(kJitCacheRecordSchema);
        append_u32(static_cast<std::uint32_t>(metadata.size()));
        append_u64(static_cast<std::uint64_t>(object.size()));
        result.insert(result.end(), metadata.begin(), metadata.end());
        result.resize(object_offset);
        result.insert(result.end(), object.begin(), object.end());
        return result;
    }

    [[nodiscard]] std::optional<JitCacheRecordView> parse_jit_cache_record(
        const std::span<const std::byte> bytes)
    {
        if (bytes.size() < 24U
            || !std::equal(
                kJitCacheRecordMagic.begin(), kJitCacheRecordMagic.end(),
                bytes.begin())) {
            return std::nullopt;
        }
        const auto read_u32 = [&](const std::size_t offset) {
            std::uint32_t value { };
            for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
                value |= static_cast<std::uint32_t>(
                             std::to_integer<std::uint8_t>(
                                 bytes[offset + byte]))
                    << (byte * 8U);
            }
            return value;
        };
        const auto read_u64 = [&](const std::size_t offset) {
            std::uint64_t value { };
            for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
                value |= static_cast<std::uint64_t>(
                             std::to_integer<std::uint8_t>(
                                 bytes[offset + byte]))
                    << (byte * 8U);
            }
            return value;
        };
        const auto schema = read_u32(8U);
        const auto metadata_size = static_cast<std::size_t>(read_u32(12U));
        const auto object_size = read_u64(16U);
        const auto object_offset
            = (24U + metadata_size + 7U) & ~std::size_t { 7U };
        if (schema != kJitCacheRecordSchema
            || metadata_size > kMaximumJitMetadataBytes
            || object_size > std::numeric_limits<std::size_t>::max()
            || metadata_size > bytes.size() - 24U
            || object_offset > bytes.size()
            || static_cast<std::size_t>(object_size)
                != bytes.size() - object_offset) {
            return std::nullopt;
        }
        return JitCacheRecordView {
            bytes.subspan(24U, metadata_size),
            bytes.subspan(object_offset)
        };
    }

#if defined(__MINGW32__)
    // LLVM emits GNU Windows stack probes for sufficiently large JIT frames.
    // Referencing the compiler-rt implementation also ensures the linker
    // retains it in the host executable for the ORC absolute-symbol binding.
    extern "C" void ___chkstk_ms();
#endif

    using namespace llvm_detail;
    using runtime::Logic9;
    using runtime::simir::Process;
    using runtime::simir::ReadSignal;
    using runtime::simir::SignalReadKind;
    using runtime::simir::ValueKind;
    using runtime::simir::WriteProjected;
    using runtime::simir::WriteProjectedDynamicSlice;
    using runtime::simir::WriteProjectedSlice;
    using runtime::simir::WriteProjectedWaveform;
    using runtime::simir::WriteProjectedWaveformDynamicSlice;
    using runtime::simir::WriteProjectedWaveformSlice;
    using runtime::simir::WriteUpdate;
    using runtime::simir::WriteUpdateDynamicPartSlice;
    using runtime::simir::WriteUpdateDynamicSlice;
    using runtime::simir::WriteUpdateSlice;
    using NativeProcess = fsim_jit_process_v1;
    using NativeCohort = std::uint32_t(
        const fsim_jit_runtime_v1* const*,
        fsim_jit_frame_v1* const*,
        fsim_jit_resume_result_v1* const*,
        std::uint32_t*, std::uint8_t* const*, std::uint8_t* const*,
        std::uint8_t* const*, std::uint8_t* const*, std::uint32_t);

    [[nodiscard]] std::vector<runtime::simir::SignalId>
    direct_update_signals(
        const Process& process,
        const std::span<const std::uint32_t> signal_widths,
        const std::span<const ValueKind> signal_value_kinds)
    {
        const auto* const profiled_process = std::getenv(
            "FSIM_PROFILE_DIRECT_UPDATE_PROCESS");
        const bool profile = profiled_process != nullptr
            && std::string_view { profiled_process }
                == std::to_string(process.id);
        std::vector<runtime::simir::SignalId> logic4_result;
        std::vector<runtime::simir::SignalId> logic9_result;
        const auto add = [&](const runtime::simir::SignalId signal) {
            const auto kind = signal_value_kinds.empty()
                ? ValueKind::logic4
                : signal < signal_value_kinds.size()
                ? signal_value_kinds[signal]
                : ValueKind::logic4;
            if (signal >= signal_widths.size()
                || signal_widths[signal] == 0U
                || (kind == ValueKind::logic9
                    && signal_widths[signal] > 64U)) {
                if (profile) {
                    llvm::errs()
                        << "fsim-profile: direct-update id=" << process.id
                        << " rejected_signal=" << signal
                        << " width="
                        << (signal < signal_widths.size()
                                ? signal_widths[signal]
                                : 0U)
                        << " kind="
                        << (signal < signal_value_kinds.size()
                                ? static_cast<unsigned>(
                                      signal_value_kinds[signal])
                                : 0U)
                        << '\n';
                }
                return false;
            }
            auto& result = kind == ValueKind::logic9
                ? logic9_result
                : logic4_result;
            if (std::ranges::find(result, signal) == result.end()) {
                result.push_back(signal);
            }
            return true;
        };
        std::vector<runtime::simir::SignalId> projected_candidates;
        std::vector<runtime::simir::SignalId> projected_unsafe;
        const auto add_unique = [](auto& collection, const auto signal) {
            if (std::ranges::find(collection, signal) == collection.end()) {
                collection.push_back(signal);
            }
        };
        for (const auto& stored : process.operations) {
            if (const auto* projected
                = runtime::simir::operation_get_if<WriteProjected>(&stored)) {
                const bool simple = projected->delay == 0U
                    && projected->rejection == 0U
                    && projected->mode
                        == runtime::simir::ProjectedDelayMode::inertial;
                add_unique(simple ? projected_candidates : projected_unsafe,
                    projected->signal);
            } else if (const auto* projected_slice
                = runtime::simir::operation_get_if<WriteProjectedSlice>(
                    &stored)) {
                const bool simple = projected_slice->delay == 0U
                    && projected_slice->rejection == 0U
                    && projected_slice->mode
                        == runtime::simir::ProjectedDelayMode::inertial;
                add_unique(simple ? projected_candidates : projected_unsafe,
                    projected_slice->signal);
            } else if (const auto* waveform
                = runtime::simir::operation_get_if<WriteProjectedWaveform>(
                    &stored)) {
                add_unique(projected_unsafe, waveform->signal);
            } else if (const auto* waveform_slice
                = runtime::simir::operation_get_if<
                    WriteProjectedWaveformSlice>(&stored)) {
                add_unique(projected_unsafe, waveform_slice->signal);
            } else if (const auto* dynamic
                = runtime::simir::operation_get_if<
                    WriteProjectedDynamicSlice>(&stored)) {
                add_unique(projected_unsafe, dynamic->signal);
            } else if (const auto* dynamic_waveform
                = runtime::simir::operation_get_if<
                    WriteProjectedWaveformDynamicSlice>(&stored)) {
                add_unique(projected_unsafe, dynamic_waveform->signal);
            }
        }
        std::erase_if(projected_candidates, [&](const auto signal) {
            return std::ranges::find(projected_unsafe, signal)
                != projected_unsafe.end();
        });
        std::ranges::sort(projected_candidates);
        bool saw_update { };
        std::size_t whole_updates { };
        std::size_t slice_updates { };
        std::size_t dynamic_slice_updates { };
        std::size_t dynamic_part_updates { };
        for (const auto& stored : process.operations) {
            if (const auto* operation
                = runtime::simir::operation_get_if<WriteUpdate>(&stored)) {
                saw_update = true;
                ++whole_updates;
                static_cast<void>(add(operation->signal));
            } else if (const auto* slice_operation
                = runtime::simir::operation_get_if<WriteUpdateSlice>(&stored)) {
                saw_update = true;
                ++slice_updates;
                static_cast<void>(add(slice_operation->signal));
            } else if (const auto* dynamic_operation
                = runtime::simir::operation_get_if<
                    WriteUpdateDynamicSlice>(&stored)) {
                saw_update = true;
                ++dynamic_slice_updates;
                static_cast<void>(add(dynamic_operation->signal));
            } else if (const auto* dynamic_part_operation
                = runtime::simir::operation_get_if<
                    WriteUpdateDynamicPartSlice>(&stored)) {
                saw_update = true;
                ++dynamic_part_updates;
                if (dynamic_part_operation->selection.width <= 64U) {
                    static_cast<void>(add(dynamic_part_operation->signal));
                }
            } else if (const auto* projected
                = runtime::simir::operation_get_if<WriteProjected>(&stored)) {
                if (std::ranges::binary_search(
                        projected_candidates, projected->signal)) {
                    saw_update = true;
                    static_cast<void>(add(projected->signal));
                }
            } else if (const auto* projected_slice
                = runtime::simir::operation_get_if<WriteProjectedSlice>(
                    &stored)) {
                if (std::ranges::binary_search(
                        projected_candidates, projected_slice->signal)) {
                    saw_update = true;
                    static_cast<void>(add(projected_slice->signal));
                }
            }
        }
        logic4_result.insert(
            logic4_result.end(), logic9_result.begin(), logic9_result.end());
        const auto& result = logic4_result;
        if (profile) {
            llvm::errs() << "fsim-profile: direct-update id=" << process.id
                         << " saw_update=" << saw_update
                         << " slots=" << result.size()
                         << " whole=" << whole_updates
                         << " slice=" << slice_updates
                         << " dynamic_slice=" << dynamic_slice_updates
                         << " dynamic_part=" << dynamic_part_updates << '\n';
            for (const auto signal : result) {
                llvm::errs() << "  signal=" << signal
                             << " width=" << signal_widths[signal]
                             << " regions=";
                bool first = true;
                for (const auto& region : process.driver_regions) {
                    if (region.signal != signal) {
                        continue;
                    }
                    if (!first) {
                        llvm::errs() << ',';
                    }
                    first = false;
                    if (region.whole) {
                        llvm::errs() << "whole";
                    } else {
                        llvm::errs() << region.offset << '+' << region.width;
                    }
                }
                llvm::errs() << '\n';
            }
        }
        return saw_update ? result
                          : std::vector<runtime::simir::SignalId> { };
    }

    [[nodiscard]] std::vector<runtime::simir::SignalId>
    direct_read_signals(
        const Process& process,
        const std::span<const std::uint32_t> signal_widths,
        const std::span<const ValueKind> signal_value_kinds)
    {
        std::vector<runtime::simir::SignalId> result;
        for (const auto& stored : process.operations) {
            const auto* read = runtime::simir::operation_get_if<ReadSignal>(
                &stored);
            if (read == nullptr || read->kind != SignalReadKind::current
                || read->signal >= signal_widths.size()
                || signal_widths[read->signal] == 0U
                || (!signal_value_kinds.empty()
                    && signal_value_kinds[read->signal]
                        != ValueKind::logic4)) {
                continue;
            }
            if (std::ranges::find(result, read->signal) == result.end()) {
                result.push_back(read->signal);
            }
        }
        return result;
    }

    [[nodiscard]] llvm::CodeGenOptLevel codegen_optimization(
        const JitOptimizationLevel optimization) noexcept
    {
        switch (optimization) {
        case JitOptimizationLevel::o0:
            return llvm::CodeGenOptLevel::None;
        case JitOptimizationLevel::o1:
            // O1 is the cold-start profile. Its bounded IR pipeline performs
            // the scalar and CFG cleanup; keep target emission on LLVM's fast
            // path so backend work cannot dominate short simulations.
            return llvm::CodeGenOptLevel::None;
        case JitOptimizationLevel::o2:
            // The IR pipeline already performs the O2 transformations.  The
            // optimized machine scheduler has near-quadratic dependency-graph
            // behavior on large generated HDL processes, while contributing
            // little steady-state improvement after outlining.  Keep target
            // emission on the bounded fast path.
            return llvm::CodeGenOptLevel::None;
        }
        return llvm::CodeGenOptLevel::Default;
    }

    struct IrShape {
        std::size_t functions { };
        std::size_t blocks { };
        std::size_t instructions { };
        std::size_t allocas { };
        std::size_t loads { };
        std::size_t stores { };
        std::size_t calls { };
        std::size_t branches { };
        std::size_t switches { };
        std::size_t phis { };
    };

    [[nodiscard]] IrShape ir_shape(const llvm::Module& module)
    {
        IrShape result;
        for (const auto& function : module) {
            if (function.isDeclaration()) {
                continue;
            }
            ++result.functions;
            for (const auto& block : function) {
                ++result.blocks;
                for (const auto& instruction : block) {
                    ++result.instructions;
                    result.allocas += llvm::isa<llvm::AllocaInst>(instruction);
                    result.loads += llvm::isa<llvm::LoadInst>(instruction);
                    result.stores += llvm::isa<llvm::StoreInst>(instruction);
                    result.calls += llvm::isa<llvm::CallBase>(instruction);
                    result.branches += llvm::isa<llvm::BranchInst>(instruction);
                    result.switches += llvm::isa<llvm::SwitchInst>(instruction);
                    result.phis += llvm::isa<llvm::PHINode>(instruction);
                }
            }
        }
        return result;
    }

    void dump_ir(const llvm::Module& module, const char* path)
    {
        std::error_code error;
        llvm::raw_fd_ostream stream(path, error);
        if (error) {
            llvm::errs() << "fsim-profile: llvm-ir-dump error='" << error.message()
                         << "' path='" << path << "'\n";
            return;
        }
        module.print(stream, nullptr);
    }

    void print_ir_shape(
        llvm::raw_ostream& stream,
        const char* phase,
        const IrShape& shape)
    {
        stream
            << " " << phase << "_functions=" << shape.functions
            << " " << phase << "_blocks=" << shape.blocks
            << " " << phase << "_instructions=" << shape.instructions
            << " " << phase << "_allocas=" << shape.allocas
            << " " << phase << "_loads=" << shape.loads
            << " " << phase << "_stores=" << shape.stores
            << " " << phase << "_calls=" << shape.calls
            << " " << phase << "_branches=" << shape.branches
            << " " << phase << "_switches=" << shape.switches
            << " " << phase << "_phis=" << shape.phis;
    }

    static_assert(std::is_standard_layout_v<fsim_jit_runtime_v1>);
    static_assert(std::is_standard_layout_v<fsim_jit_frame_v1>);
    static_assert(std::is_standard_layout_v<fsim_jit_resume_result_v1>);
    static_assert(sizeof(std::uint32_t) == 4);
    static_assert(sizeof(std::uint64_t) == 8);
    static_assert(offsetof(fsim_jit_runtime_v1, abi_version) == 0);
    static_assert(offsetof(fsim_jit_runtime_v1, struct_size) == 4);
    static_assert(offsetof(fsim_jit_runtime_v1, context) == 8);
    static_assert(offsetof(fsim_jit_runtime_v1, read_signal) == 16);
    static_assert(offsetof(fsim_jit_runtime_v1, write_signal) == 24);
    static_assert(offsetof(fsim_jit_runtime_v1, assert_failed) == 32);
    static_assert(offsetof(fsim_jit_runtime_v1, write_update) == 40);
    static_assert(offsetof(fsim_jit_runtime_v1, write_after) == 48);
    static_assert(offsetof(fsim_jit_runtime_v1, flags) == 56);
    static_assert(offsetof(fsim_jit_runtime_v1, reserved) == 60);
    static_assert(offsetof(fsim_jit_runtime_v1, write_signal_slice) == 64);
    static_assert(offsetof(fsim_jit_runtime_v1, write_update_slice) == 72);
    static_assert(offsetof(fsim_jit_runtime_v1, write_after_slice) == 80);
    static_assert(offsetof(fsim_jit_runtime_v1, signal_event) == 88);
    static_assert(offsetof(fsim_jit_runtime_v1, signal_last_value) == 96);
    static_assert(offsetof(fsim_jit_runtime_v1, signal_last_event) == 104);
    static_assert(offsetof(fsim_jit_runtime_v1, signal_active) == 112);
    static_assert(offsetof(fsim_jit_runtime_v1, write_output) == 120);
    static_assert(offsetof(fsim_jit_runtime_v1, schedule_output) == 128);
    static_assert(offsetof(fsim_jit_runtime_v1, write_report) == 136);
    static_assert(offsetof(fsim_jit_runtime_v1, write_formatted) == 144);
    static_assert(offsetof(fsim_jit_runtime_v1, write_time) == 152);
    static_assert(offsetof(fsim_jit_runtime_v1, install_monitor) == 160);
    static_assert(offsetof(fsim_jit_runtime_v1, control_monitor) == 168);
    static_assert(offsetof(fsim_jit_runtime_v1, random_value) == 176);
    static_assert(offsetof(fsim_jit_runtime_v1, write_inertial) == 184);
    static_assert(
        offsetof(fsim_jit_runtime_v1, write_inertial_slice) == 192);
    static_assert(offsetof(fsim_jit_runtime_v1, write_projected) == 200);
    static_assert(
        offsetof(fsim_jit_runtime_v1, write_projected_slice) == 208);
    static_assert(
        offsetof(fsim_jit_runtime_v1, write_projected_waveform) == 216);
    static_assert(
        offsetof(fsim_jit_runtime_v1, write_projected_waveform_slice) == 224);
    static_assert(
        offsetof(fsim_jit_runtime_v1, read_signal_logic9) == 232);
    static_assert(
        offsetof(fsim_jit_runtime_v1, write_formatted_logic9) == 344);
    static_assert(offsetof(fsim_jit_runtime_v1, load_string) == 352);
    static_assert(
        offsetof(fsim_jit_runtime_v1, write_string_output) == 424);
    static_assert(offsetof(fsim_jit_runtime_v1, file_open) == 432);
    static_assert(offsetof(fsim_jit_runtime_v1, file_error) == 472);
    static_assert(
        offsetof(fsim_jit_runtime_v1, container_operation) == 480);
    static_assert(offsetof(fsim_jit_runtime_v1, force_signal_slice) == 488);
    static_assert(
        offsetof(fsim_jit_runtime_v1, force_signal_slice_logic9) == 496);
    static_assert(offsetof(fsim_jit_runtime_v1, release_signal_slice) == 504);
    static_assert(offsetof(fsim_jit_runtime_v1, signal_last_active) == 512);
    static_assert(offsetof(fsim_jit_runtime_v1, signal_driving) == 520);
    static_assert(offsetof(fsim_jit_runtime_v1, signal_driving_value) == 528);
    static_assert(
        offsetof(fsim_jit_runtime_v1, signal_driving_value_logic9) == 536);
    static_assert(offsetof(fsim_jit_runtime_v1, read_simulation_time) == 544);
    static_assert(offsetof(fsim_jit_runtime_v1, vital_timing_check) == 552);
    static_assert(offsetof(fsim_jit_runtime_v1, vital_delay) == 560);
    static_assert(
        offsetof(fsim_jit_runtime_v1, force_driver_signal_slice) == 568);
    static_assert(
        offsetof(fsim_jit_runtime_v1, force_driver_signal_slice_logic9) == 576);
    static_assert(
        offsetof(fsim_jit_runtime_v1, release_driver_signal_slice) == 584);
    static_assert(
        offsetof(fsim_jit_runtime_v1, execute_signal_operation) == 592);
    static_assert(offsetof(fsim_jit_runtime_v1, container_read_word) == 600);
    static_assert(offsetof(fsim_jit_runtime_v1, container_write_word) == 608);
    static_assert(offsetof(fsim_jit_runtime_v1, container_read_packed) == 616);
    static_assert(offsetof(fsim_jit_runtime_v1, container_write_packed) == 624);
    static_assert(offsetof(fsim_jit_runtime_v1, read_signal_packed) == 632);
    static_assert(offsetof(fsim_jit_runtime_v1, write_signal_packed) == 640);
    static_assert(offsetof(fsim_jit_runtime_v1, direct_update_slots) == 648);
    static_assert(offsetof(fsim_jit_runtime_v1, direct_update_slot_count) == 656);
    static_assert(offsetof(fsim_jit_runtime_v1, direct_signal_aval) == 664);
    static_assert(offsetof(fsim_jit_runtime_v1, direct_signal_bval) == 672);
    static_assert(offsetof(fsim_jit_runtime_v1, direct_read_signals) == 680);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_read_signal_count) == 688);
    static_assert(offsetof(fsim_jit_runtime_v1, direct_signal_count) == 692);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_wide_signal_aval) == 704);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_wide_signal_bval) == 712);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_wide_signal_offsets) == 720);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_wide_signal_offset_count) == 728);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_wide_word_count) == 732);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_update_active_words) == 736);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_update_active_word_count) == 744);
    static_assert(offsetof(fsim_jit_runtime_v1, static_trigger_mask) == 752);
    static_assert(
        offsetof(fsim_jit_runtime_v1, read_signal_dynamic_part) == 760);
    static_assert(
        offsetof(fsim_jit_runtime_v1,
            direct_wide_signal_logic9_plane2) == 768);
    static_assert(
        offsetof(fsim_jit_runtime_v1,
            direct_wide_signal_logic9_plane3) == 776);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane0) == 784);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane1) == 792);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane2) == 800);
    static_assert(
        offsetof(fsim_jit_runtime_v1, direct_signal_logic9_plane3) == 808);
    static_assert(
        offsetof(fsim_jit_runtime_v1, code_coverage_hit_counters) == 816);
    static_assert(
        offsetof(fsim_jit_runtime_v1, code_coverage_counter_values) == 824);
    static_assert(
        offsetof(fsim_jit_runtime_v1, code_coverage_hit_count) == 832);
    static_assert(
        offsetof(fsim_jit_runtime_v1, code_coverage_counter_count) == 836);
    static_assert(
        offsetof(fsim_jit_runtime_v1, record_code_coverage_counter) == 840);
    static_assert(sizeof(fsim_jit_runtime_v1) == 848);
    static_assert(sizeof(fsim_jit_projected_element_v1) == 24);
    static_assert(sizeof(fsim_jit_logic9_word_v1) == 32);
    static_assert(sizeof(fsim_jit_logic9_projected_element_v1) == 40);
    static_assert(sizeof(fsim_jit_update_slot_v1) == 80);
    static_assert(sizeof(fsim_jit_frame_v1) == 344);
    static_assert(offsetof(fsim_jit_frame_v1, register_aval) == 40);
    static_assert(offsetof(fsim_jit_frame_v1, register_bval) == 48);
    static_assert(offsetof(fsim_jit_frame_v1, register_initialized) == 56);
    static_assert(
        offsetof(fsim_jit_frame_v1, register_logic9_plane2) == 64);
    static_assert(
        offsetof(fsim_jit_frame_v1, register_logic9_plane3) == 72);
    static_assert(offsetof(fsim_jit_frame_v1, native_call_depth) == 80);
    static_assert(offsetof(fsim_jit_frame_v1, native_call_reserved) == 84);
    static_assert(offsetof(fsim_jit_frame_v1, native_return_stack) == 88);
    static_assert(sizeof(fsim_jit_resume_result_v1) == 24);

    constexpr auto kJitRuntimeV1PrefixSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_update));
    constexpr auto kJitFrameV1PrefixSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_frame_v1, register_logic9_plane2));
    constexpr auto kJitRuntimeLogic9Size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, load_string));
    constexpr auto kJitRuntimeStringSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, file_open));
    constexpr auto kJitRuntimeFileSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, container_operation));
    constexpr auto kJitRuntimeForceSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, read_simulation_time));
    constexpr auto kJitRuntimeDriverForceSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, execute_signal_operation));
    constexpr auto kJitRuntimeContainerWordSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, container_read_packed));
    constexpr auto kJitRuntimeExactSignalSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, read_signal_packed));
    constexpr auto kJitRuntimeWideSignalReadSize = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_signal_packed));

    class PersistentLlvmObjectCache final : public llvm::ObjectCache {
    public:
        PersistentLlvmObjectCache(std::filesystem::path root,
            llvm::Triple target_triple,
            const LlvmJitOptions& options)
            : storage_(std::move(root))
            , target_triple_(std::move(target_triple))
        {
            ObjectCachePruneOptions prune_options;
            prune_options.maximum_bytes = options.cache_maximum_bytes;
            prune_options.maximum_entries = options.cache_maximum_entries;
            prune_options.maximum_age = options.cache_maximum_age;
            ObjectCachePruneResult result;
            std::error_code error;
            if (storage_.prune(prune_options, result, error)) {
                pruned_entries_.store(
                    result.removed_entries, std::memory_order_relaxed);
                pruned_bytes_.store(
                    result.bytes_removed, std::memory_order_relaxed);
                if (result.failed_removals != 0) {
                    prune_failures_.store(
                        result.failed_removals, std::memory_order_relaxed);
                }
            } else {
                prune_failures_.store(1, std::memory_order_relaxed);
            }
        }

        void notifyObjectCompiled(const llvm::Module* module,
            const llvm::MemoryBufferRef object) override
        {
            if (module == nullptr || !valid_cache_key(module->getModuleIdentifier())) {
                return;
            }
            try {
                const auto object_bytes = std::span<const std::byte> {
                    reinterpret_cast<const std::byte*>(object.getBufferStart()),
                    object.getBufferSize()
                };
                const auto key = module->getModuleIdentifier();
                std::vector<std::byte> metadata;
                {
                    const std::lock_guard lock { metadata_mutex_ };
                    if (const auto found = pending_metadata_.find(key);
                        found != pending_metadata_.end()) {
                        metadata = std::move(found->second);
                        pending_metadata_.erase(found);
                    }
                }
                const auto record = make_jit_cache_record(
                    object_bytes, metadata);
                std::error_code error;
                if (storage_.store(key, record, error)) {
                    stores_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    store_failures_.fetch_add(1, std::memory_order_relaxed);
                }
            } catch (...) {
                store_failures_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] std::unique_ptr<llvm::MemoryBuffer>
        getObject(const llvm::Module* module) override
        {
            if (module == nullptr || !valid_cache_key(module->getModuleIdentifier())) {
                return nullptr;
            }
            {
                const std::lock_guard lock { preflight_mutex_ };
                if (preflight_misses_.erase(module->getModuleIdentifier()) != 0U) {
                    misses_.fetch_add(1, std::memory_order_relaxed);
                    return nullptr;
                }
            }
            return load_object(module->getModuleIdentifier(), true);
        }

        [[nodiscard]] std::unique_ptr<llvm::MemoryBuffer>
        preflight(
            const std::string_view key,
            std::vector<std::byte>* const metadata = nullptr)
        {
            if (!valid_cache_key(key)) {
                return nullptr;
            }
            auto object = load_object(key, false, metadata);
            if (!object) {
                const std::lock_guard lock { preflight_mutex_ };
                preflight_misses_.emplace(key);
            }
            return object;
        }

        void stage_metadata(
            std::string key, std::vector<std::byte> metadata)
        {
            const std::lock_guard lock { metadata_mutex_ };
            pending_metadata_.insert_or_assign(
                std::move(key), std::move(metadata));
        }

        void discard_staged_metadata(const std::string_view key)
        {
            const std::lock_guard lock { metadata_mutex_ };
            pending_metadata_.erase(std::string { key });
        }

        [[nodiscard]] LlvmJitCacheStatistics statistics() const noexcept
        {
            return {
                hits_.load(std::memory_order_relaxed),
                misses_.load(std::memory_order_relaxed),
                stores_.load(std::memory_order_relaxed),
                rejected_entries_.load(std::memory_order_relaxed),
                load_failures_.load(std::memory_order_relaxed),
                store_failures_.load(std::memory_order_relaxed),
                pruned_entries_.load(std::memory_order_relaxed),
                pruned_bytes_.load(std::memory_order_relaxed),
                prune_failures_.load(std::memory_order_relaxed),
            };
        }

    private:
        [[nodiscard]] std::unique_ptr<llvm::MemoryBuffer>
        load_object(
            const std::string_view key,
            const bool record_miss,
            std::vector<std::byte>* const metadata = nullptr)
        {
            try {
                std::error_code error;
                auto bytes = storage_.load(key, error);
                if (!bytes) {
                    if (record_miss) {
                        misses_.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (error && error != std::errc::no_such_file_or_directory) {
                        if (error == std::errc::illegal_byte_sequence) {
                            rejected_entries_.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            load_failures_.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    return nullptr;
                }
                const auto record = parse_jit_cache_record(*bytes);
                if (!record || !valid_native_object(record->object)) {
                    if (record_miss) {
                        misses_.fetch_add(1, std::memory_order_relaxed);
                    }
                    rejected_entries_.fetch_add(1, std::memory_order_relaxed);
                    return nullptr;
                }

                if (metadata != nullptr) {
                    metadata->assign(
                        record->metadata.begin(), record->metadata.end());
                }
                const auto data = llvm::StringRef {
                    reinterpret_cast<const char*>(record->object.data()),
                    record->object.size()
                };
                auto result = llvm::MemoryBuffer::getMemBufferCopy(
                    data, std::string { key } + ".o");
                hits_.fetch_add(1, std::memory_order_relaxed);
                return result;
            } catch (...) {
                if (record_miss) {
                    misses_.fetch_add(1, std::memory_order_relaxed);
                }
                load_failures_.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
        }
        [[nodiscard]] bool
        valid_native_object(const std::span<const std::byte> bytes) const
        {
            const auto data = llvm::StringRef {
                reinterpret_cast<const char*>(bytes.data()), bytes.size()
            };
            auto parsed = llvm::object::ObjectFile::createObjectFile(
                llvm::MemoryBufferRef { data, "fsim-cached-object" });
            if (!parsed) {
                llvm::consumeError(parsed.takeError());
                return false;
            }
            return (*parsed)->isRelocatableObject()
                && (*parsed)->getBytesInAddress() == sizeof(void*)
                && (*parsed)->getArch() == target_triple_.getArch()
                && (*parsed)->getTripleObjectFormat()
                    == target_triple_.getObjectFormat();
        }

        fsim::compiler::ObjectCache storage_;
        llvm::Triple target_triple_;
        std::mutex preflight_mutex_;
        std::unordered_set<std::string> preflight_misses_;
        std::mutex metadata_mutex_;
        std::unordered_map<std::string, std::vector<std::byte>>
            pending_metadata_;
        std::atomic_uint64_t hits_ { };
        std::atomic_uint64_t misses_ { };
        std::atomic_uint64_t stores_ { };
        std::atomic_uint64_t rejected_entries_ { };
        std::atomic_uint64_t load_failures_ { };
        std::atomic_uint64_t store_failures_ { };
        std::atomic_uint64_t pruned_entries_ { };
        std::atomic<std::uintmax_t> pruned_bytes_ { };
        std::atomic_uint64_t prune_failures_ { };
    };

    [[nodiscard]] std::string llvm_error(llvm::Error error)
    {
        std::string message;
        llvm::raw_string_ostream stream(message);
        llvm::logAllUnhandledErrors(std::move(error), stream);
        stream.flush();
        return message;
    }

    template <typename T>
    [[nodiscard]] T unwrap(llvm::Expected<T> expected,
        const std::string_view action)
    {
        if (!expected) {
            throw LlvmJitError(std::string { action } + ": " + llvm_error(expected.takeError()));
        }
        return std::move(*expected);
    }

    std::once_flag native_target_once;
    std::string native_target_error;

    void initialize_native_target()
    {
        std::call_once(native_target_once, [] {
            if (llvm::InitializeNativeTarget()) {
                native_target_error = "LLVM failed to initialize the native target";
                return;
            }
            if (llvm::InitializeNativeTargetAsmPrinter()) {
                native_target_error = "LLVM failed to initialize the native assembly printer";
            }
        });
        if (!native_target_error.empty()) {
            throw LlvmJitError(native_target_error);
        }
    }

#if defined(__linux__)
    class PerfMapPlugin final : public llvm::orc::ObjectLinkingLayer::Plugin {
    public:
        void modifyPassConfig(
            llvm::orc::MaterializationResponsibility&,
            llvm::jitlink::LinkGraph&,
            llvm::jitlink::PassConfiguration& config) override
        {
            config.PostFixupPasses.push_back(
                [](llvm::jitlink::LinkGraph& graph) -> llvm::Error {
                    static std::mutex map_mutex;
                    static bool initialized { };
                    const std::scoped_lock lock { map_mutex };
                    const auto path = "/tmp/perf-"
                        + std::to_string(static_cast<long long>(::getpid()))
                        + ".map";
                    std::error_code error;
                    llvm::raw_fd_ostream stream(
                        path,
                        error,
                        initialized ? llvm::sys::fs::OF_Append
                                    : llvm::sys::fs::OF_None);
                    if (error) {
                        return llvm::errorCodeToError(error);
                    }
                    initialized = true;
                    for (const auto* const symbol : graph.defined_symbols()) {
                        if (!symbol->hasName() || !symbol->isCallable()
                            || symbol->getSize() == 0U) {
                            continue;
                        }
                        stream
                            << llvm::format_hex_no_prefix(
                                   symbol->getAddress().getValue(), 1U)
                            << ' '
                            << llvm::format_hex_no_prefix(
                                   symbol->getSize(), 1U)
                            << ' ' << *symbol->getName() << '\n';
                    }
                    return llvm::Error::success();
                });
        }

        llvm::Error notifyFailed(
            llvm::orc::MaterializationResponsibility&) override
        {
            return llvm::Error::success();
        }

        llvm::Error notifyRemovingResources(
            llvm::orc::JITDylib&, llvm::orc::ResourceKey) override
        {
            return llvm::Error::success();
        }

        void notifyTransferringResources(
            llvm::orc::JITDylib&,
            llvm::orc::ResourceKey,
            llvm::orc::ResourceKey) override
        {
        }
    };
#endif

} // namespace

using namespace llvm_detail;

LlvmJitGeneratedRuntimeError::LlvmJitGeneratedRuntimeError(
    const std::uint32_t instruction,
    const JitGeneratedRuntimeErrorReason reason)
    : LlvmJitError(generated_runtime_error_message(instruction, reason))
    , instruction_(instruction)
    , reason_(reason)
{
}

struct LlvmJit::Impl {
    struct ProcessInfo {
        JitProcessFrameLayout frame_layout;
        std::uint32_t operation_count { };
        bool uses_native_call_stack { };
        bool requires_resume { };
        bool uses_write_update { };
        bool uses_write_after { };
        bool uses_write_inertial { };
        bool uses_write_projected { };
        bool uses_write_projected_waveform { };
        bool uses_write_blocking_slice { };
        bool uses_write_update_slice { };
        bool uses_write_after_slice { };
        bool uses_write_inertial_slice { };
        bool uses_write_projected_slice { };
        bool uses_write_projected_waveform_slice { };
        bool uses_force_signal_slice { };
        bool uses_release_signal_slice { };
        bool uses_force_driver_signal_slice { };
        bool uses_release_driver_signal_slice { };
        bool uses_debug_points { };
        bool uses_signal_event { };
        bool uses_signal_last_value { };
        bool uses_signal_last_event { };
        bool uses_simulation_time { };
        bool uses_vital_timing { };
        bool uses_vital_delay { };
        bool uses_signal_active { };
        bool uses_signal_last_active { };
        bool uses_signal_driving { };
        bool uses_signal_driving_value { };
        bool uses_output { };
        bool uses_postponed_output { };
        bool uses_report { };
        bool uses_formatted_output { };
        bool uses_time_output { };
        bool uses_monitor_install { };
        bool uses_monitor_control { };
        bool uses_random_value { };
        bool uses_strings { };
        bool uses_files { };
        bool uses_containers { };
        bool uses_wide_container_operation { };
        bool uses_exact_signal_operation { };
        bool uses_wide_signal_read { };
        bool uses_wide_signal_write { };
        bool uses_code_coverage { };
        std::vector<runtime::simir::InstructionIndex> entry_points;

        static constexpr std::array flags {
            &ProcessInfo::uses_native_call_stack,
            &ProcessInfo::requires_resume,
            &ProcessInfo::uses_write_update,
            &ProcessInfo::uses_write_after,
            &ProcessInfo::uses_write_inertial,
            &ProcessInfo::uses_write_projected,
            &ProcessInfo::uses_write_projected_waveform,
            &ProcessInfo::uses_write_blocking_slice,
            &ProcessInfo::uses_write_update_slice,
            &ProcessInfo::uses_write_after_slice,
            &ProcessInfo::uses_write_inertial_slice,
            &ProcessInfo::uses_write_projected_slice,
            &ProcessInfo::uses_write_projected_waveform_slice,
            &ProcessInfo::uses_force_signal_slice,
            &ProcessInfo::uses_release_signal_slice,
            &ProcessInfo::uses_force_driver_signal_slice,
            &ProcessInfo::uses_release_driver_signal_slice,
            &ProcessInfo::uses_debug_points,
            &ProcessInfo::uses_signal_event,
            &ProcessInfo::uses_signal_last_value,
            &ProcessInfo::uses_signal_last_event,
            &ProcessInfo::uses_simulation_time,
            &ProcessInfo::uses_vital_timing,
            &ProcessInfo::uses_vital_delay,
            &ProcessInfo::uses_signal_active,
            &ProcessInfo::uses_signal_last_active,
            &ProcessInfo::uses_signal_driving,
            &ProcessInfo::uses_signal_driving_value,
            &ProcessInfo::uses_output,
            &ProcessInfo::uses_postponed_output,
            &ProcessInfo::uses_report,
            &ProcessInfo::uses_formatted_output,
            &ProcessInfo::uses_time_output,
            &ProcessInfo::uses_monitor_install,
            &ProcessInfo::uses_monitor_control,
            &ProcessInfo::uses_random_value,
            &ProcessInfo::uses_strings,
            &ProcessInfo::uses_files,
            &ProcessInfo::uses_containers,
            &ProcessInfo::uses_wide_container_operation,
            &ProcessInfo::uses_exact_signal_operation,
            &ProcessInfo::uses_wide_signal_read,
            &ProcessInfo::uses_wide_signal_write,
            &ProcessInfo::uses_code_coverage,
        };
    };

    [[nodiscard]] static std::vector<std::byte> encode_module_metadata(
        std::span<const ProcessInfo> processes);
    [[nodiscard]] static std::optional<std::vector<ProcessInfo>>
    decode_module_metadata(
        std::span<const std::byte> metadata,
        std::span<const JitProcessModuleEntry> entries,
        std::span<const std::uint32_t> signal_widths);

    struct NativeEntry {
        NativeProcess* function { };
        ProcessInfo info;
        std::string symbol;
    };

    struct NativeCohortEntry {
        NativeCohort* function { };
        std::vector<const NativeEntry*> members;
        bool manages_process_state { };
        bool region_mode { };
    };

    struct NativeBoundCohortEntry {
        NativeCohort* function { };
        std::vector<const NativeEntry*> members;
        std::vector<const fsim_jit_runtime_v1*> runtimes;
        std::vector<fsim_jit_frame_v1*> frames;
        std::vector<fsim_jit_resume_result_v1*> results;
        std::vector<std::uint32_t> statuses;
        std::vector<std::uint8_t*> queued;
        std::vector<std::uint8_t*> waiting;
        std::vector<std::uint8_t*> process_statuses;
        std::vector<std::uint8_t*> active;
        bool manages_process_state { };
        bool region_mode { };
    };

    LlvmJitOptions options;
    std::unique_ptr<PersistentLlvmObjectCache> object_cache;
    std::unique_ptr<llvm::orc::LLJIT> jit;
    std::string target_cpu;
    std::vector<std::string> target_features;
    std::string immutable_design_identity;
    std::unordered_set<std::string> module_identities;
    std::unordered_set<std::string> symbols;
    std::unordered_set<std::string> pending_module_identities;
    std::unordered_set<std::string> pending_symbols;
    std::unordered_map<std::string, ProcessInfo> info_by_symbol;
    std::unordered_map<std::string, JitProcessHandle> handles_by_symbol;
    std::unordered_map<std::uint64_t, std::unique_ptr<NativeEntry>> functions;
    std::unordered_map<std::size_t,
        std::vector<std::unique_ptr<NativeCohortEntry>>>
        cohort_functions;
    std::vector<std::unique_ptr<NativeBoundCohortEntry>> bound_cohorts;
    std::unordered_map<const Process*, ValidatedProcess>
        immutable_validated_processes;
    std::mutex validation_mutex;
    std::mutex lookup_mutex;
    std::mutex cohort_mutex;
    std::uint64_t next_handle = 1;
    std::uint64_t next_cohort = 1;
};

std::vector<std::byte> LlvmJit::Impl::encode_module_metadata(
    const std::span<const ProcessInfo> processes)
{
    if (processes.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("LLVM cache metadata has too many processes");
    }
    JitMetadataWriter writer;
    writer.append_bytes(kJitMetadataMagic);
    writer.append_u32(kJitMetadataSchema);
    constexpr std::size_t encoded_size_offset = 12U;
    writer.append_u32(0U);
    writer.append_u32(static_cast<std::uint32_t>(processes.size()));
    for (const auto& process : processes) {
        const auto& frame = process.frame_layout;
        writer.append_u64(frame.layout_id_low);
        writer.append_u64(frame.layout_id_high);
        writer.append_u32(frame.register_count);
        writer.append_u32(frame.register_word_count);
        writer.append_u32(frame.string_register_count);
        writer.append_u8(frame.uses_logic9 ? 1U : 0U);
        writer.append_u8(frame.tracks_register_initialization ? 1U : 0U);
        writer.append_u32_vector(frame.register_widths);
        writer.append_u32_vector(frame.register_word_offsets);
        writer.append_u32_vector(frame.direct_read_signals);
        writer.append_u32_vector(frame.direct_update_signals);
        writer.append_u32(process.operation_count);
        for (const auto flag : ProcessInfo::flags) {
            writer.append_u8(process.*flag ? 1U : 0U);
        }
        writer.append_u32_vector(process.entry_points);
    }
    if (writer.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("LLVM cache metadata is too large");
    }
    writer.patch_u32(
        encoded_size_offset, static_cast<std::uint32_t>(writer.size()));
    return writer.finish();
}

std::optional<std::vector<LlvmJit::Impl::ProcessInfo>>
LlvmJit::Impl::decode_module_metadata(
    const std::span<const std::byte> metadata,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths)
{
    if (metadata.size() < 20U
        || metadata.size() > kMaximumJitMetadataBytes) {
        return std::nullopt;
    }
    JitMetadataReader header { metadata };
    std::uint32_t schema { };
    std::uint32_t encoded_size { };
    if (!header.read_bytes(kJitMetadataMagic)
        || !header.read_u32(schema)
        || schema != kJitMetadataSchema
        || !header.read_u32(encoded_size)
        || encoded_size < 20U
        || encoded_size > metadata.size()) {
        return std::nullopt;
    }

    JitMetadataReader reader { metadata.first(encoded_size) };
    std::uint32_t process_count { };
    if (!reader.read_bytes(kJitMetadataMagic)
        || !reader.read_u32(schema)
        || !reader.read_u32(encoded_size)
        || !reader.read_u32(process_count)
        || process_count != entries.size()) {
        return std::nullopt;
    }

    std::vector<ProcessInfo> result(process_count);
    for (std::size_t index = 0U; index < result.size(); ++index) {
        if (entries[index].process == nullptr) {
            return std::nullopt;
        }
        auto& process = result[index];
        auto& frame = process.frame_layout;
        std::uint8_t uses_logic9 { };
        std::uint8_t tracks_register_initialization { };
        if (!reader.read_u64(frame.layout_id_low)
            || !reader.read_u64(frame.layout_id_high)
            || !reader.read_u32(frame.register_count)
            || !reader.read_u32(frame.register_word_count)
            || !reader.read_u32(frame.string_register_count)
            || !reader.read_u8(uses_logic9)
            || uses_logic9 > 1U
            || !reader.read_u8(tracks_register_initialization)
            || tracks_register_initialization > 1U
            || !reader.read_u32_vector(frame.register_widths)
            || !reader.read_u32_vector(frame.register_word_offsets)
            || !reader.read_u32_vector(frame.direct_read_signals)
            || !reader.read_u32_vector(frame.direct_update_signals)
            || !reader.read_u32(process.operation_count)) {
            return std::nullopt;
        }
        frame.uses_logic9 = uses_logic9 != 0U;
        frame.tracks_register_initialization
            = tracks_register_initialization != 0U;
        for (const auto flag : ProcessInfo::flags) {
            std::uint8_t value { };
            if (!reader.read_u8(value) || value > 1U) {
                return std::nullopt;
            }
            process.*flag = value != 0U;
        }
        if (!reader.read_u32_vector(process.entry_points)) {
            return std::nullopt;
        }

        const auto& source = *entries[index].process;
        if (process.operation_count != source.operations.size()
            || frame.register_count != source.register_count
            || frame.string_register_count != source.string_register_count
            || frame.register_widths.size() != frame.register_count
            || frame.register_word_offsets.size() != frame.register_count) {
            return std::nullopt;
        }
        for (std::size_t reg = 0U;
            reg < frame.register_widths.size(); ++reg) {
            const auto width = frame.register_widths[reg];
            const auto offset = frame.register_word_offsets[reg];
            const auto words = (static_cast<std::uint64_t>(width) + 63U) / 64U;
            if (width == 0U || offset > frame.register_word_count
                || words > frame.register_word_count - offset) {
                return std::nullopt;
            }
        }
        const auto valid_signal = [&](const auto signal) {
            return signal < signal_widths.size()
                && signal_widths[signal] != 0U;
        };
        if (!std::ranges::all_of(
                frame.direct_read_signals, valid_signal)
            || !std::ranges::all_of(
                frame.direct_update_signals, valid_signal)
            || !std::ranges::all_of(
                process.entry_points,
                [&](const auto instruction) {
                    return instruction < process.operation_count;
                })) {
            return std::nullopt;
        }
    }
    if (!reader.finished()) {
        return std::nullopt;
    }
    return result;
}

LlvmJit::LlvmJit(const LlvmJitOptions options)
    : impl_(std::make_unique<Impl>())
{
    initialize_native_target();
    impl_->options = options;

    auto target_builder = unwrap(
        llvm::orc::JITTargetMachineBuilder::detectHost(),
        "cannot detect the native LLVM target");
    target_builder.setCodeGenOptLevel(
        codegen_optimization(options.optimization));
    // HDL process modules contain many simple scalar blocks surrounding a
    // smaller set of wide operations. Ask LLVM to select those blocks through
    // FastISel and fall back to SelectionDAG only for operations it cannot
    // represent; CodeGenOptLevel::None alone does not enable this option on all
    // host configurations.
    target_builder.getOptions().EnableFastISel = true;
    impl_->target_cpu = target_builder.getCPU();
    impl_->target_features = target_builder.getFeatures().getFeatures();
    std::sort(impl_->target_features.begin(), impl_->target_features.end());

    llvm::orc::LLJITBuilder builder;
    // Application materialization already invokes add/lookup from a bounded
    // eight-worker pool.  A second ORC dispatcher clones every module into a
    // different LLVMContext before code generation and oversubscribes cold
    // compilation.  Keep ORC materialization in the calling worker instead.
    builder.setNumCompileThreads(0U);
    if (!options.cache_directory.empty()) {
        impl_->object_cache = std::make_unique<PersistentLlvmObjectCache>(
            options.cache_directory / "llvm" / "objects",
            target_builder.getTargetTriple(),
            options);
        auto* const object_cache = impl_->object_cache.get();
        builder.setCompileFunctionCreator(
            [object_cache](llvm::orc::JITTargetMachineBuilder machine_builder)
                -> llvm::Expected<std::unique_ptr<
                    llvm::orc::IRCompileLayer::IRCompiler>> {
                std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>
                    compiler = std::make_unique<llvm::orc::ConcurrentIRCompiler>(
                        std::move(machine_builder), object_cache);
                return compiler;
            });
    }
    builder.setJITTargetMachineBuilder(std::move(target_builder));
    impl_->jit = unwrap(builder.create(), "cannot create LLVM LLJIT");
#if defined(__linux__)
    if (std::getenv("FSIM_PERF_MAP") != nullptr) {
        auto* const object_layer
            = llvm::dyn_cast<llvm::orc::ObjectLinkingLayer>(
                &impl_->jit->getObjLinkingLayer());
        if (object_layer == nullptr) {
            throw LlvmJitError(
                "LLVM perf-map profiling requires the JITLink object layer");
        }
        object_layer->addPlugin(std::make_shared<PerfMapPlugin>());
    }
#endif
#if defined(__MINGW32__)
    llvm::orc::SymbolMap mingw_runtime_symbols;
    mingw_runtime_symbols[
        impl_->jit->getExecutionSession().intern("___chkstk_ms")] =
        llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&___chkstk_ms),
            llvm::JITSymbolFlags::Exported);
    if (auto error = impl_->jit->getMainJITDylib().define(
            llvm::orc::absoluteSymbols(std::move(mingw_runtime_symbols)))) {
        throw LlvmJitError(
            "cannot register LLVM-MinGW runtime symbols: "
            + llvm_error(std::move(error)));
    }
#endif
}

LlvmJit::~LlvmJit() = default;
LlvmJit::LlvmJit(LlvmJit&&) noexcept = default;
LlvmJit& LlvmJit::operator=(LlvmJit&&) noexcept = default;

void LlvmJit::set_immutable_design_identity(std::string identity)
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::scoped_lock lock { impl_->lookup_mutex };
    if (!impl_->module_identities.empty()
        || !impl_->pending_module_identities.empty()) {
        throw LlvmJitError(
            "immutable design identity must be set before adding modules");
    }
    impl_->immutable_design_identity = std::move(identity);
}

bool LlvmJit::supports_process(
    const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    try {
        auto validated = validate_process(
            process, signal_widths, signal_value_kinds);
        if (!impl_->immutable_design_identity.empty()) {
            const std::scoped_lock lock { impl_->validation_mutex };
            impl_->immutable_validated_processes.insert_or_assign(
                &process, std::move(validated));
        }
        return true;
    } catch (const LlvmJitUnsupportedError& error) {
        if (std::getenv("FSIM_PROFILE_JIT") != nullptr) {
            llvm::errs() << "fsim-profile: jit-unsupported id=" << process.id
                         << " operations=" << process.operations.size()
                         << " name=" << process.name
                         << " reason=" << error.what() << '\n';
        }
        return false;
    }
}

#include "llvm_jit_process_modules.tpp"

#include "llvm_jit_execution.tpp"

LlvmJitCacheStatistics LlvmJit::cache_statistics() const noexcept
{
    if (!impl_ || !impl_->object_cache) {
        return { };
    }
    return impl_->object_cache->statistics();
}

std::string_view LlvmJit::llvm_version() noexcept
{
    return LLVM_VERSION_STRING;
}

LlvmNativeHostIdentity LlvmJit::native_host_identity(
    const JitOptimizationLevel optimization)
{
    initialize_native_target();
    auto target_builder = unwrap(
        llvm::orc::JITTargetMachineBuilder::detectHost(),
        "cannot detect the native LLVM target");
    target_builder.setCodeGenOptLevel(codegen_optimization(optimization));
    const auto target = target_builder.getTargetTriple().str();
    const auto cpu = target_builder.getCPU();
    auto features = target_builder.getFeatures().getFeatures();
    std::sort(features.begin(), features.end());
    auto target_machine = unwrap(
        target_builder.createTargetMachine(),
        "cannot create native LLVM target machine");
    const auto data_layout = target_machine->createDataLayout().getStringRepresentation();

    CacheKeyBuilder builder;
    builder.add("kind", "fsim-llvm-native-host-v1");
    builder.add("build-configuration", FSIM_BUILD_CONFIGURATION);
    builder.add("llvm-version", LLVM_VERSION_STRING);
    builder.add("optimization", to_string(optimization));
    builder.add(
        "runtime-abi-version",
        std::to_string(FSIM_JIT_RUNTIME_ABI_VERSION_V1));
    builder.add("runtime-abi-size", std::to_string(sizeof(fsim_jit_runtime_v1)));
    builder.add(
        "frame-abi-version", std::to_string(FSIM_JIT_FRAME_ABI_VERSION_V1));
    builder.add("frame-abi-size", std::to_string(sizeof(fsim_jit_frame_v1)));
    builder.add(
        "resume-abi-version",
        std::to_string(FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1));
    builder.add(
        "resume-abi-size", std::to_string(sizeof(fsim_jit_resume_result_v1)));
    builder.add("target", target);
    builder.add("data-layout", data_layout);
    builder.add("cpu", cpu);
    builder.add("feature-count", std::to_string(features.size()));
    for (const auto& feature : features) {
        builder.add("feature", feature);
    }
    return {
        builder.finish(), LLVM_VERSION_STRING, target, data_layout, cpu,
        std::move(features)
    };
}

} // namespace fsim::compiler
