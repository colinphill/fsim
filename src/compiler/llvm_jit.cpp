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
                        << "FSIM-DIRECT-UPDATE-PROFILE id=" << process.id
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
            llvm::errs() << "FSIM-DIRECT-UPDATE-PROFILE id=" << process.id
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
            llvm::errs() << "FSIM-LLVM-IR-DUMP error='" << error.message()
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
    static_assert(sizeof(fsim_jit_runtime_v1) == 816);
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
            llvm::errs() << "FSIM-JIT-UNSUPPORTED id=" << process.id
                         << " operations=" << process.operations.size()
                         << " name=" << process.name
                         << " reason=" << error.what() << '\n';
        }
        return false;
    }
}

void LlvmJit::add_process_module(
    const std::string_view module_identity,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (module_identity.empty()) {
        throw LlvmJitError("LLVM process module identity cannot be empty");
    }
    const std::string owned_module_identity { module_identity };
    if (entries.empty()) {
        throw LlvmJitError("LLVM process module cannot be empty");
    }
    const auto make_process_key = [&](const std::string_view symbol,
                                      const Process& process) {
        return impl_->immutable_design_identity.empty()
            ? make_native_object_cache_key(
                  symbol, process, signal_widths, signal_value_kinds,
                  impl_->options.optimization,
                  impl_->options.debug_instrumentation,
                  impl_->options.require_direct_update_slots,
                  impl_->jit->getTargetTriple(),
                  impl_->jit->getDataLayout(), impl_->target_cpu,
                  impl_->target_features)
            : make_immutable_design_object_cache_key(
                  impl_->immutable_design_identity,
                  owned_module_identity,
                  symbol,
                  impl_->options.optimization,
                  impl_->options.debug_instrumentation,
                  impl_->options.require_direct_update_slots,
                  impl_->jit->getTargetTriple(),
                  impl_->jit->getDataLayout(), impl_->target_cpu,
                  impl_->target_features);
    };
    std::unique_ptr<llvm::MemoryBuffer> preflight_object;
    bool object_preflight_attempted { };
    if (impl_->object_cache) {
        std::vector<std::string> cached_symbols;
        std::vector<std::string> cached_process_keys;
        std::unordered_set<std::string> unique_symbols;
        cached_symbols.reserve(entries.size());
        cached_process_keys.reserve(entries.size());
        for (const auto& entry : entries) {
            if (entry.process == nullptr) {
                throw LlvmJitError(
                    "LLVM process module entry has no SimIR process");
            }
            if (!valid_symbol(entry.symbol)) {
                throw LlvmJitError(
                    "LLVM process symbol must be a non-empty C identifier");
            }
            cached_symbols.emplace_back(entry.symbol);
            if (!unique_symbols.insert(cached_symbols.back()).second) {
                throw LlvmJitError(
                    "duplicate LLVM process symbol '"
                    + cached_symbols.back() + "'");
            }
            cached_process_keys.push_back(
                make_process_key(cached_symbols.back(), *entry.process));
        }
        const auto cached_module_key = make_native_module_cache_key(
            owned_module_identity, cached_process_keys);
        object_preflight_attempted = true;
        std::vector<std::byte> metadata;
        preflight_object = impl_->object_cache->preflight(
            cached_module_key, &metadata);
        auto cached_info = preflight_object && !metadata.empty()
            ? Impl::decode_module_metadata(
                  metadata, entries, signal_widths)
            : std::nullopt;
        if (preflight_object && cached_info) {
            {
                const std::scoped_lock lock { impl_->lookup_mutex };
                if (impl_->module_identities.contains(owned_module_identity)
                    || impl_->pending_module_identities.contains(
                        owned_module_identity)) {
                    throw LlvmJitError(
                        "duplicate LLVM process module identity '"
                        + owned_module_identity + "'");
                }
                for (const auto& symbol : cached_symbols) {
                    if (impl_->symbols.contains(symbol)
                        || impl_->pending_symbols.contains(symbol)) {
                        throw LlvmJitError(
                            "duplicate LLVM process symbol '" + symbol + "'");
                    }
                }
                impl_->pending_module_identities.insert(
                    owned_module_identity);
                impl_->pending_symbols.insert(
                    cached_symbols.begin(), cached_symbols.end());
            }
            try {
                if (auto error = impl_->jit->addObjectFile(
                        std::move(preflight_object))) {
                    throw LlvmJitError(
                        "cannot add cached LLVM process module '"
                        + owned_module_identity + "': "
                        + llvm_error(std::move(error)));
                }
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->module_identities.insert(owned_module_identity);
                impl_->pending_module_identities.erase(owned_module_identity);
                for (std::size_t index = 0U;
                    index < cached_symbols.size(); ++index) {
                    impl_->pending_symbols.erase(cached_symbols[index]);
                    impl_->symbols.insert(cached_symbols[index]);
                    impl_->info_by_symbol.emplace(
                        cached_symbols[index],
                        std::move((*cached_info)[index]));
                }
            } catch (...) {
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->pending_module_identities.erase(owned_module_identity);
                for (const auto& symbol : cached_symbols) {
                    impl_->pending_symbols.erase(symbol);
                }
                throw;
            }
            return;
        }
    }
    struct PreparedProcess {
        std::string symbol;
        const Process* process { };
        ValidatedProcess validated;
        std::string cache_key;
        Impl::ProcessInfo info;
    };
    std::vector<PreparedProcess> prepared;
    prepared.reserve(entries.size());
    std::unordered_set<std::string> module_symbols;
    std::vector<std::string> process_keys;
    process_keys.reserve(entries.size());

    for (const auto& entry : entries) {
        if (entry.process == nullptr) {
            throw LlvmJitError("LLVM process module entry has no SimIR process");
        }
        if (!valid_symbol(entry.symbol)) {
            throw LlvmJitError(
                "LLVM process symbol must be a non-empty C identifier");
        }
        std::string owned_symbol { entry.symbol };
        if (!module_symbols.insert(owned_symbol).second) {
            throw LlvmJitError(
                "duplicate LLVM process symbol '" + owned_symbol + "'");
        }

        ValidatedProcess validated;
        bool reused_validation = false;
        if (!impl_->immutable_design_identity.empty()) {
            const std::scoped_lock lock { impl_->validation_mutex };
            const auto found = impl_->immutable_validated_processes.find(
                entry.process);
            if (found != impl_->immutable_validated_processes.end()) {
                validated = std::move(found->second);
                impl_->immutable_validated_processes.erase(found);
                reused_validation = true;
            }
        }
        if (!reused_validation) {
            validated = validate_process(
                *entry.process, signal_widths, signal_value_kinds);
        }
        auto cache_key = make_process_key(owned_symbol, *entry.process);
        const bool uses_native_call_stack = std::ranges::any_of(
            entry.process->operations,
            [](const runtime::simir::Operation& operation) {
                return runtime::simir::operation_holds<
                    runtime::simir::CallableFramePush>(operation);
            });
        const auto accumulated_update_signals = direct_update_signals(
            *entry.process, signal_widths, signal_value_kinds);
        const auto callback_free_read_signals = direct_read_signals(
            *entry.process, signal_widths, signal_value_kinds);
        auto process_info = Impl::ProcessInfo {
            make_frame_layout(
                cache_key,
                validated.register_widths,
                entry.process->string_register_count,
                validated.uses_logic9,
                impl_->options.debug_instrumentation
                    || !entry.process->debug_locals.empty(),
                callback_free_read_signals,
                accumulated_update_signals),
            static_cast<std::uint32_t>(entry.process->operations.size()),
            uses_native_call_stack,
            validated.requires_resume,
            validated.uses_write_update,
            validated.uses_write_after,
            validated.uses_write_inertial,
            validated.uses_write_projected,
            validated.uses_write_projected_waveform,
            validated.uses_write_blocking_slice,
            validated.uses_write_update_slice,
            validated.uses_write_after_slice,
            validated.uses_write_inertial_slice,
            validated.uses_write_projected_slice,
            validated.uses_write_projected_waveform_slice,
            validated.uses_force_signal_slice,
            validated.uses_release_signal_slice,
            validated.uses_force_driver_signal_slice,
            validated.uses_release_driver_signal_slice,
            validated.uses_debug_points
                && impl_->options.debug_instrumentation,
            validated.uses_signal_event,
            validated.uses_signal_last_value,
            validated.uses_signal_last_event,
            validated.uses_simulation_time,
            validated.uses_vital_timing,
            validated.uses_vital_delay,
            validated.uses_signal_active,
            validated.uses_signal_last_active,
            validated.uses_signal_driving,
            validated.uses_signal_driving_value,
            validated.uses_output,
            validated.uses_postponed_output,
            validated.uses_report,
            validated.uses_formatted_output,
            validated.uses_time_output,
            validated.uses_monitor_install,
            validated.uses_monitor_control,
            validated.uses_random_value,
            validated.uses_strings,
            validated.uses_files,
            validated.uses_containers,
            validated.uses_wide_container_operation,
            validated.uses_exact_signal_operation,
            validated.uses_wide_signal_read,
            validated.uses_wide_signal_write,
            { },
        };
        process_info.entry_points = make_process_lowering_plan(
            *entry.process,
            impl_->options.debug_instrumentation).entry_points;
        process_keys.push_back(cache_key);
        prepared.push_back(
            { std::move(owned_symbol), entry.process, std::move(validated),
                std::move(cache_key), process_info });
    }
    {
        const std::scoped_lock lock { impl_->lookup_mutex };
        if (impl_->module_identities.contains(owned_module_identity)
            || impl_->pending_module_identities.contains(
                owned_module_identity)) {
            throw LlvmJitError(
                "duplicate LLVM process module identity '"
                + owned_module_identity + "'");
        }
        for (const auto& item : prepared) {
            if (impl_->symbols.contains(item.symbol)
                || impl_->pending_symbols.contains(item.symbol)) {
                throw LlvmJitError(
                    "duplicate LLVM process symbol '" + item.symbol + "'");
            }
        }
        impl_->pending_module_identities.insert(owned_module_identity);
        for (const auto& item : prepared) {
            impl_->pending_symbols.insert(item.symbol);
        }
    }

    try {
        const auto module_cache_key = make_native_module_cache_key(
            owned_module_identity, process_keys);
        if (impl_->object_cache) {
            auto object = std::move(preflight_object);
            if (!object_preflight_attempted) {
                object = impl_->object_cache->preflight(module_cache_key);
            }
            if (object) {
                if (auto error = impl_->jit->addObjectFile(std::move(object))) {
                    throw LlvmJitError(
                        "cannot add cached LLVM process module '"
                        + owned_module_identity + "': "
                        + llvm_error(std::move(error)));
                }
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->module_identities.insert(owned_module_identity);
                impl_->pending_module_identities.erase(owned_module_identity);
                for (auto& item : prepared) {
                    impl_->pending_symbols.erase(item.symbol);
                    impl_->symbols.insert(item.symbol);
                    impl_->info_by_symbol.emplace(
                        std::move(item.symbol), item.info);
                }
                return;
            }
        }
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = std::make_unique<llvm::Module>(
            owned_module_identity + ".module", *context);
        module->setDataLayout(impl_->jit->getDataLayout());
        module->setTargetTriple(impl_->jit->getTargetTriple());
        const auto lowering_begin = std::chrono::steady_clock::now();
        for (const auto& item : prepared) {
            lower_process(
                *module, item.symbol, *item.process, signal_widths,
                signal_value_kinds,
                item.info.frame_layout.direct_read_signals,
                item.info.frame_layout.direct_update_signals,
                item.validated,
                impl_->options.optimization,
                impl_->options.debug_instrumentation,
                impl_->options.require_direct_update_slots);
        }
        const auto lowering_end = std::chrono::steady_clock::now();
        if (auto message = verify_error(*module); !message.empty()) {
            throw LlvmJitError(
                "generated invalid LLVM IR for module '"
                + owned_module_identity + "': " + message);
        }
        const auto raw_shape = ir_shape(*module);
        const auto* const dump_process = std::getenv("FSIM_DUMP_LLVM_PROCESS");
        const bool dump_selected = dump_process != nullptr
            && std::any_of(
                prepared.begin(),
                prepared.end(),
                [dump_process](const auto& item) {
                    return item.symbol
                        == std::string("fsim_process_") + dump_process;
                });
        if (dump_selected) {
            dump_ir(*module, "/tmp/fsim-selected-raw.ll");
        }
        if (impl_->object_cache) {
            module->setModuleIdentifier(module_cache_key);
            std::vector<Impl::ProcessInfo> metadata_processes;
            metadata_processes.reserve(prepared.size());
            for (const auto& item : prepared) {
                metadata_processes.push_back(item.info);
            }
            impl_->object_cache->stage_metadata(
                module_cache_key,
                Impl::encode_module_metadata(metadata_processes));
        }
        const auto optimization_begin = std::chrono::steady_clock::now();
        optimize_module(*module, impl_->options.optimization);
        const auto optimization_end = std::chrono::steady_clock::now();
        if (auto message = verify_error(*module); !message.empty()) {
            throw LlvmJitError(
                "LLVM optimization produced invalid IR for module '"
                + owned_module_identity + "': " + message);
        }
        const auto optimized_shape = ir_shape(*module);
        if (dump_selected) {
            dump_ir(*module, "/tmp/fsim-selected-optimized.ll");
        }
        if (std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr
            && (dump_process == nullptr || dump_selected)) {
            const auto milliseconds = [](const auto duration) {
                return std::chrono::duration<double, std::milli>(duration)
                    .count();
            };
            std::string profile_line;
            llvm::raw_string_ostream profile(profile_line);
            profile << "FSIM-LLVM-MODULE-PROFILE identity='"
                    << owned_module_identity << "' processes="
                    << prepared.size() << " process_ids=";
            for (std::size_t index = 0; index < prepared.size(); ++index) {
                if (index != 0) {
                    profile << ',';
                }
                profile << prepared[index].process->id;
            }
            profile << " simir_operations=";
            std::size_t operation_count = 0;
            std::size_t dynamic_calls = 0;
            std::size_t dynamic_returns = 0;
            std::size_t frame_pushes = 0;
            std::size_t frame_pops = 0;
            std::size_t frame_packed_slots = 0;
            for (const auto& item : prepared) {
                operation_count += item.process->operations.size();
                for (const auto& operation : item.process->operations) {
                    if (const auto* call
                        = runtime::simir::operation_get_if<
                            runtime::simir::Call>(&operation);
                        call != nullptr && call->stack.capacity == 0) {
                        ++dynamic_calls;
                    }
                    if (const auto* return_operation
                        = runtime::simir::operation_get_if<
                            runtime::simir::Return>(&operation);
                        return_operation != nullptr
                        && return_operation->stack.capacity == 0) {
                        ++dynamic_returns;
                    }
                    if (const auto* push
                        = runtime::simir::operation_get_if<
                            runtime::simir::CallableFramePush>(&operation)) {
                        ++frame_pushes;
                        frame_packed_slots += push->packed.size();
                    }
                    frame_pops += runtime::simir::operation_holds<
                        runtime::simir::CallableFramePop>(operation);
                }
            }
            profile << operation_count
                    << " dynamic_calls=" << dynamic_calls
                    << " dynamic_returns=" << dynamic_returns
                    << " frame_pushes=" << frame_pushes
                    << " frame_pops=" << frame_pops
                    << " frame_packed_slots=" << frame_packed_slots
                    << " lowering_ms="
                    << milliseconds(lowering_end - lowering_begin)
                    << " optimization_ms="
                    << milliseconds(
                           optimization_end - optimization_begin);
            print_ir_shape(profile, "raw", raw_shape);
            print_ir_shape(profile, "optimized", optimized_shape);
            profile << '\n';
            profile.flush();
            llvm::errs() << profile_line;
        }

        if (auto error = impl_->jit->addIRModule(llvm::orc::ThreadSafeModule(
                std::move(module), std::move(context)))) {
            throw LlvmJitError(
                "cannot add LLVM process module '" + owned_module_identity
                + "': " + llvm_error(std::move(error)));
        }
        const std::scoped_lock lock { impl_->lookup_mutex };
        impl_->module_identities.insert(owned_module_identity);
        impl_->pending_module_identities.erase(owned_module_identity);
        for (auto& item : prepared) {
            impl_->pending_symbols.erase(item.symbol);
            impl_->symbols.insert(item.symbol);
            impl_->info_by_symbol.emplace(
                std::move(item.symbol), item.info);
        }
    } catch (...) {
        if (impl_->object_cache) {
            impl_->object_cache->discard_staged_metadata(
                make_native_module_cache_key(
                    owned_module_identity, process_keys));
        }
        const std::scoped_lock lock { impl_->lookup_mutex };
        impl_->pending_module_identities.erase(owned_module_identity);
        for (const auto& item : prepared) {
            impl_->pending_symbols.erase(item.symbol);
        }
        throw;
    }
}

void LlvmJit::add_process(
    const std::string_view symbol, const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    const std::array entries {
        JitProcessModuleEntry { symbol, &process }
    };
    add_process_module(
        symbol, entries, signal_widths, signal_value_kinds);
}

JitProcessHandle LlvmJit::lookup(const std::string_view symbol)
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::string owned_symbol { symbol };
    {
        const std::scoped_lock lock { impl_->lookup_mutex };
        if (!impl_->symbols.contains(owned_symbol)) {
            throw LlvmJitError(
                "LLVM process symbol was not added: '" + owned_symbol + "'");
        }
        if (const auto found = impl_->handles_by_symbol.find(owned_symbol);
            found != impl_->handles_by_symbol.end()) {
            return found->second;
        }
    }

    auto address = unwrap(impl_->jit->lookup(owned_symbol),
        "cannot materialize LLVM process '" + owned_symbol + "'");
    auto* function = address.template toPtr<NativeProcess>();
    if (function == nullptr) {
        throw LlvmJitError("LLVM returned a null process address for '" + owned_symbol + "'");
    }
    const std::scoped_lock lock { impl_->lookup_mutex };
    if (const auto found = impl_->handles_by_symbol.find(owned_symbol);
        found != impl_->handles_by_symbol.end()) {
        return found->second;
    }
    if (impl_->next_handle == 0) {
        throw LlvmJitError("LLVM process handle space is exhausted");
    }
    const JitProcessHandle handle { impl_->next_handle++ };
    const auto info = impl_->info_by_symbol.find(owned_symbol);
    if (info == impl_->info_by_symbol.end()) {
        throw LlvmJitError("LLVM process frame metadata is missing for '" + owned_symbol + "'");
    }
    impl_->functions.emplace(handle.value,
        std::make_unique<Impl::NativeEntry>(
            Impl::NativeEntry { function, info->second, owned_symbol }));
    impl_->handles_by_symbol.emplace(owned_symbol, handle);
    return handle;
}

JitProcessBinding LlvmJit::bind(const JitProcessHandle process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::scoped_lock lock { impl_->lookup_mutex };
    const auto found = impl_->functions.find(process.value);
    if (process.value == 0 || found == impl_->functions.end()) {
        throw LlvmJitError("invalid LLVM process handle");
    }
    return JitProcessBinding { impl_.get(), found->second.get() };
}

bool LlvmJit::supports_entry(
    const JitProcessHandle process,
    const runtime::simir::InstructionIndex instruction) const
{
    return supports_entry(bind(process), instruction);
}

bool LlvmJit::supports_entry(
    const JitProcessBinding process,
    const runtime::simir::InstructionIndex instruction) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    return std::ranges::binary_search(entry.info.entry_points, instruction);
}

JitProcessFrameLayout
LlvmJit::frame_layout(const JitProcessHandle process) const
{
    return frame_layout(bind(process));
}

JitProcessFrameLayout
LlvmJit::frame_layout(const JitProcessBinding process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    return entry.info.frame_layout;
}

void LlvmJit::initialize_frame(
    const JitProcessHandle process, fsim_jit_frame_v1& frame,
    const std::span<std::uint64_t> register_aval,
    const std::span<std::uint64_t> register_bval,
    const std::span<std::uint8_t> register_initialized,
    const std::span<std::uint64_t> register_logic9_plane2,
    const std::span<std::uint64_t> register_logic9_plane3) const
{
    initialize_frame(
        bind(process), frame, register_aval, register_bval,
        register_initialized, register_logic9_plane2,
        register_logic9_plane3);
}

void LlvmJit::initialize_frame(
    const JitProcessBinding process, fsim_jit_frame_v1& frame,
    const std::span<std::uint64_t> register_aval,
    const std::span<std::uint64_t> register_bval,
    const std::span<std::uint8_t> register_initialized,
    const std::span<std::uint64_t> register_logic9_plane2,
    const std::span<std::uint64_t> register_logic9_plane3) const
{
    const auto layout = frame_layout(process);
    if (register_aval.size() < layout.register_word_count || register_bval.size() < layout.register_word_count
        || register_initialized.size() < layout.register_count
        || (layout.uses_logic9
            && (register_logic9_plane2.size() < layout.register_word_count
                || register_logic9_plane3.size()
                    < layout.register_word_count))) {
        throw LlvmJitError(
            "caller-owned JIT register storage is smaller than the frame layout");
    }
    if (layout.register_word_count != 0 && (register_aval.data() == register_bval.data() || (layout.uses_logic9 && (register_logic9_plane2.data() == register_logic9_plane3.data() || register_logic9_plane2.data() == register_aval.data() || register_logic9_plane2.data() == register_bval.data() || register_logic9_plane3.data() == register_aval.data() || register_logic9_plane3.data() == register_bval.data())))) {
        throw LlvmJitError(
            "caller-owned JIT register planes must be distinct");
    }
    std::fill_n(
        register_aval.begin(), layout.register_word_count, UINT64_C(0));
    std::fill_n(
        register_bval.begin(), layout.register_word_count, UINT64_C(0));
    if (layout.uses_logic9) {
        std::fill_n(
            register_logic9_plane2.begin(),
            layout.register_word_count,
            UINT64_C(0));
        std::fill_n(
            register_logic9_plane3.begin(),
            layout.register_word_count,
            UINT64_C(0));
    }
    std::fill_n(
        register_initialized.begin(), layout.register_count, UINT8_C(0));
    frame = {
        FSIM_JIT_FRAME_ABI_VERSION_V1,
        static_cast<std::uint32_t>(sizeof(fsim_jit_frame_v1)),
        layout.layout_id_low,
        layout.layout_id_high,
        layout.register_count,
        0,
        FSIM_JIT_FRAME_STATE_READY,
        FSIM_JIT_INVALID_INSTRUCTION,
        register_aval.data(),
        register_bval.data(),
        register_initialized.data(),
        layout.uses_logic9
            ? register_logic9_plane2.data()
            : nullptr,
        layout.uses_logic9
            ? register_logic9_plane3.data()
            : nullptr,
        0,
        0,
        { },
    };
}

JitResumeStatus
LlvmJit::resume(const JitProcessHandle process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    return resume(bind(process), runtime, frame, result);
}

JitResumeStatus LlvmJit::resume_prevalidated(
    const JitProcessBinding process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    const auto raw_status = entry.function(&runtime, &frame, &result);
    if (raw_status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
        if (const auto reason = decode_generated_runtime_error(result.delay)) {
            throw LlvmJitGeneratedRuntimeError(
                result.instruction, *reason);
        }
        throw LlvmJitError(
            "generated process returned an invalid runtime error reason");
    }
    return static_cast<JitResumeStatus>(raw_status);
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort");
    }

    constexpr std::size_t inline_cohort_capacity = 512U;
    std::array<const Impl::NativeEntry*, inline_cohort_capacity>
        inline_native_entries { };
    std::vector<const Impl::NativeEntry*> overflow_native_entries;
    std::span<const Impl::NativeEntry*> native_entries;
    if (entries.size() <= inline_cohort_capacity) {
        native_entries = {
            inline_native_entries.data(), entries.size()
        };
    } else {
        overflow_native_entries.resize(entries.size());
        native_entries = overflow_native_entries;
    }
    std::size_t prepared_index { };
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    const bool has_partial_region = std::ranges::any_of(
        entries, [](const auto& entry) { return entry.active != nullptr; })
        && !region_mode;
    if (has_partial_region) {
        throw LlvmJitError(
            "LLVM process region has incomplete active state");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    const bool has_partial_process_state = std::ranges::any_of(
        entries,
        [](const auto& entry) {
            const auto count = static_cast<unsigned>(entry.queued != nullptr)
                + static_cast<unsigned>(entry.waiting_on_static != nullptr)
                + static_cast<unsigned>(entry.process_status != nullptr);
            return count != 0U && count != 3U;
        });
    if (has_partial_process_state
        || (!manages_process_state
            && std::ranges::any_of(
                entries,
                [](const auto& entry) {
                    return entry.queued != nullptr
                        || entry.waiting_on_static != nullptr
                        || entry.process_status != nullptr;
                }))) {
        throw LlvmJitError(
            "LLVM process cohort has incomplete scheduler state");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    if (region_mode) {
        key ^= static_cast<std::size_t>(0x8f31a9c7U)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        if (entry.process.owner_ != impl_.get()
            || entry.process.entry_ == nullptr || entry.runtime == nullptr
            || entry.frame == nullptr || entry.result == nullptr) {
            throw LlvmJitError("invalid prevalidated LLVM process cohort entry");
        }
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        native_entries[prepared_index++] = native;
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    NativeCohort* cohort { };
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto found = impl_->cohort_functions.find(key);
        if (found != impl_->cohort_functions.end()) {
            const auto match = std::ranges::find_if(
                found->second,
                [&](const auto& candidate) {
                    return std::ranges::equal(
                               candidate->members, native_entries)
                        && candidate->manages_process_state
                            == manages_process_state
                        && candidate->region_mode == region_mode;
                });
            if (match != found->second.end()) {
                cohort = (*match)->function;
            }
        }
        if (cohort == nullptr) {
            const auto cohort_number = impl_->next_cohort++;
            const auto symbol = "fsim_process_cohort_"
                + std::to_string(cohort_number);
            auto context = std::make_unique<llvm::LLVMContext>();
            auto module = std::make_unique<llvm::Module>(
                symbol + ".module", *context);
            module->setDataLayout(impl_->jit->getDataLayout());
            module->setTargetTriple(impl_->jit->getTargetTriple());

            auto* const i32 = llvm::Type::getInt32Ty(*context);
            auto* const i8 = llvm::Type::getInt8Ty(*context);
            auto* const pointer = llvm::PointerType::getUnqual(*context);
            auto* const process_type = llvm::FunctionType::get(
                i32, { pointer, pointer, pointer }, false);
            auto* const cohort_type = llvm::FunctionType::get(
                i32,
                { pointer, pointer, pointer, pointer,
                    pointer, pointer, pointer, pointer, i32 },
                false);
            auto* const function = llvm::Function::Create(
                cohort_type, llvm::Function::ExternalLinkage,
                symbol, *module);
            function->setCallingConv(llvm::CallingConv::C);
            auto arguments = function->arg_begin();
            auto* const runtimes = &*arguments++;
            auto* const frames = &*arguments++;
            auto* const results = &*arguments++;
            auto* const statuses = &*arguments++;
            auto* const queued_states = &*arguments++;
            auto* const waiting_states = &*arguments++;
            auto* const process_statuses = &*arguments++;
            auto* const active_states = &*arguments++;
            auto* const count = &*arguments;

            auto* const entry_block = llvm::BasicBlock::Create(
                *context, "entry", function);
            llvm::IRBuilder<> builder(entry_block);
            auto* const valid_block = llvm::BasicBlock::Create(
                *context, "run", function);
            auto* const invalid_block = llvm::BasicBlock::Create(
                *context, "invalid", function);
            builder.CreateCondBr(
                builder.CreateICmpEQ(
                    count,
                    llvm::ConstantInt::get(i32, entries.size())),
                valid_block, invalid_block);
            builder.SetInsertPoint(invalid_block);
            builder.CreateRet(llvm::ConstantInt::get(i32, 0U));
            builder.SetInsertPoint(valid_block);

            for (std::size_t index = 0; index < native_entries.size(); ++index) {
                auto* const offset = llvm::ConstantInt::get(i32, index);
                const auto load_pointer = [&](llvm::Value* array) {
                    return builder.CreateLoad(
                        pointer,
                        builder.CreateGEP(pointer, array, offset));
                };
                auto* const queued_state = load_pointer(queued_states);
                auto* const waiting_state = load_pointer(waiting_states);
                auto* const process_status = load_pointer(process_statuses);
                if (region_mode) {
                    auto* const active_state = load_pointer(active_states);
                    auto* const run_member = llvm::BasicBlock::Create(
                        *context, "active", function);
                    auto* const continue_region = llvm::BasicBlock::Create(
                        *context, "continue", function);
                    builder.CreateCondBr(
                        builder.CreateICmpNE(
                            builder.CreateLoad(i8, active_state),
                            llvm::ConstantInt::get(i8, 0U)),
                        run_member, continue_region);
                    builder.SetInsertPoint(run_member);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), active_state);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), queued_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), process_status);
                    }
                    auto callee = module->getOrInsertFunction(
                        native_entries[index]->symbol, process_type);
                    auto* const status = builder.CreateCall(
                        callee,
                        { load_pointer(runtimes), load_pointer(frames),
                            load_pointer(results) });
                    builder.CreateStore(
                        status,
                        builder.CreateGEP(i32, statuses, offset));
                    auto* const rearm = llvm::BasicBlock::Create(
                        *context, "rearm", function);
                    auto* const stop = llvm::BasicBlock::Create(
                        *context, "stop", function);
                    builder.CreateCondBr(
                        builder.CreateICmpEQ(
                            status,
                            llvm::ConstantInt::get(
                                i32,
                                FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                        rearm, stop);
                    builder.SetInsertPoint(stop);
                    builder.CreateRet(llvm::ConstantInt::get(
                        i32, static_cast<std::uint32_t>(index + 1U)));
                    builder.SetInsertPoint(rearm);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                    }
                    builder.CreateBr(continue_region);
                    builder.SetInsertPoint(continue_region);
                    if (index + 1U == native_entries.size()) {
                        builder.CreateRet(llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                native_entries.size())));
                    }
                    continue;
                }
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), queued_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), process_status);
                }
                auto callee = module->getOrInsertFunction(
                    native_entries[index]->symbol, process_type);
                auto* const status = builder.CreateCall(
                    callee,
                    { load_pointer(runtimes), load_pointer(frames),
                        load_pointer(results) });
                builder.CreateStore(
                    status,
                    builder.CreateGEP(i32, statuses, offset));
                const auto executed = static_cast<std::uint32_t>(index + 1U);
                if (static_cast<std::size_t>(executed)
                    == native_entries.size()) {
                    if (manages_process_state) {
                        auto* const rearm = llvm::BasicBlock::Create(
                            *context, "rearm", function);
                        auto* const finish = llvm::BasicBlock::Create(
                            *context, "finish", function);
                        builder.CreateCondBr(
                            builder.CreateICmpEQ(
                                status,
                                llvm::ConstantInt::get(
                                    i32,
                                    FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                            rearm, finish);
                        builder.SetInsertPoint(rearm);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                        builder.CreateBr(finish);
                        builder.SetInsertPoint(finish);
                    }
                    builder.CreateRet(
                        llvm::ConstantInt::get(i32, executed));
                    break;
                }
                auto* const next = llvm::BasicBlock::Create(
                    *context, "next", function);
                auto* const stop = llvm::BasicBlock::Create(
                    *context, "stop", function);
                builder.CreateCondBr(
                    builder.CreateICmpEQ(
                        status,
                        llvm::ConstantInt::get(
                            i32,
                            FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                    next, stop);
                builder.SetInsertPoint(stop);
                builder.CreateRet(llvm::ConstantInt::get(i32, executed));
                builder.SetInsertPoint(next);
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 2U), process_status);
                }
            }

            if (auto message = verify_error(*module); !message.empty()) {
                throw LlvmJitError(
                    "generated invalid LLVM process cohort: " + message);
            }
            optimize_module(*module, impl_->options.optimization);
            if (auto error = impl_->jit->addIRModule(
                    llvm::orc::ThreadSafeModule(
                        std::move(module), std::move(context)))) {
                throw LlvmJitError(
                    "cannot add LLVM process cohort: "
                    + llvm_error(std::move(error)));
            }
            auto address = unwrap(
                impl_->jit->lookup(symbol),
                "cannot materialize LLVM process cohort");
            cohort = address.template toPtr<NativeCohort>();
            if (cohort == nullptr) {
                throw LlvmJitError(
                    "LLVM returned a null process cohort address");
            }
            auto members = std::vector<const Impl::NativeEntry*> {
                native_entries.begin(), native_entries.end()
            };
            impl_->cohort_functions[key].push_back(
                std::make_unique<Impl::NativeCohortEntry>(
                    Impl::NativeCohortEntry {
                        cohort, std::move(members), manages_process_state,
                        region_mode
                    }));
        }
    }

    std::array<const fsim_jit_runtime_v1*, inline_cohort_capacity>
        inline_runtimes { };
    std::array<fsim_jit_frame_v1*, inline_cohort_capacity>
        inline_frames { };
    std::array<fsim_jit_resume_result_v1*, inline_cohort_capacity>
        inline_results { };
    std::array<std::uint32_t, inline_cohort_capacity> inline_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_queued { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_waiting { };
    std::array<std::uint8_t*, inline_cohort_capacity>
        inline_process_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_active { };
    std::vector<const fsim_jit_runtime_v1*> overflow_runtimes;
    std::vector<fsim_jit_frame_v1*> overflow_frames;
    std::vector<fsim_jit_resume_result_v1*> overflow_results;
    std::vector<std::uint32_t> overflow_statuses;
    std::vector<std::uint8_t*> overflow_queued;
    std::vector<std::uint8_t*> overflow_waiting;
    std::vector<std::uint8_t*> overflow_process_statuses;
    std::vector<std::uint8_t*> overflow_active;
    std::span<const fsim_jit_runtime_v1*> runtimes;
    std::span<fsim_jit_frame_v1*> frames;
    std::span<fsim_jit_resume_result_v1*> results;
    std::span<std::uint32_t> statuses;
    std::span<std::uint8_t*> queued;
    std::span<std::uint8_t*> waiting;
    std::span<std::uint8_t*> process_statuses;
    std::span<std::uint8_t*> active;
    if (entries.size() <= inline_cohort_capacity) {
        runtimes = { inline_runtimes.data(), entries.size() };
        frames = { inline_frames.data(), entries.size() };
        results = { inline_results.data(), entries.size() };
        statuses = { inline_statuses.data(), entries.size() };
        queued = { inline_queued.data(), entries.size() };
        waiting = { inline_waiting.data(), entries.size() };
        process_statuses = {
            inline_process_statuses.data(), entries.size()
        };
        active = { inline_active.data(), entries.size() };
    } else {
        overflow_runtimes.resize(entries.size());
        overflow_frames.resize(entries.size());
        overflow_results.resize(entries.size());
        overflow_statuses.resize(entries.size());
        overflow_queued.resize(entries.size());
        overflow_waiting.resize(entries.size());
        overflow_process_statuses.resize(entries.size());
        overflow_active.resize(entries.size());
        runtimes = overflow_runtimes;
        frames = overflow_frames;
        results = overflow_results;
        statuses = overflow_statuses;
        queued = overflow_queued;
        waiting = overflow_waiting;
        process_statuses = overflow_process_statuses;
        active = overflow_active;
    }
    std::ranges::fill(
        statuses, std::numeric_limits<std::uint32_t>::max());
    for (std::size_t entry_index = 0U;
        entry_index < entries.size(); ++entry_index) {
        runtimes[entry_index] = entries[entry_index].runtime;
        frames[entry_index] = entries[entry_index].frame;
        results[entry_index] = entries[entry_index].result;
        queued[entry_index] = entries[entry_index].queued;
        waiting[entry_index] = entries[entry_index].waiting_on_static;
        process_statuses[entry_index]
            = entries[entry_index].process_status;
        active[entry_index] = entries[entry_index].active;
    }
    const auto executed = cohort(
        runtimes.data(), frames.data(), results.data(), statuses.data(),
        queued.data(), waiting.data(), process_statuses.data(),
        active.data(),
        static_cast<std::uint32_t>(entries.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "generated LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        if (statuses[index]
            == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        entries[index].status = statuses[index];
        if (statuses[index] == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entries[index].result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entries[index].result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

JitProcessCohortBinding LlvmJit::bind_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort binding");
    }

    std::vector<const Impl::NativeEntry*> members;
    members.reserve(entries.size());
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    if (region_mode || std::ranges::any_of(
            entries, [](const auto& entry) {
                return entry.active != nullptr;
            })) {
        throw LlvmJitError(
            "stable LLVM cohort binding does not accept region entries");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    if (std::ranges::any_of(
            entries,
            [&](const auto& entry) {
                const auto count
                    = static_cast<unsigned>(entry.queued != nullptr)
                    + static_cast<unsigned>(
                        entry.waiting_on_static != nullptr)
                    + static_cast<unsigned>(
                        entry.process_status != nullptr);
                return entry.process.owner_ != impl_.get()
                    || entry.process.entry_ == nullptr
                    || entry.runtime == nullptr || entry.frame == nullptr
                    || entry.result == nullptr
                    || (count != 0U && count != 3U)
                    || (manages_process_state && count != 3U);
            })) {
        throw LlvmJitError("invalid LLVM process cohort binding entry");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        members.push_back(native);
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    const Impl::NativeCohortEntry* native_cohort { };
    std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = impl_->cohort_functions.find(key);
    if (found != impl_->cohort_functions.end()) {
        const auto match = std::ranges::find_if(
            found->second,
            [&](const auto& candidate) {
                return std::ranges::equal(candidate->members, members)
                    && candidate->manages_process_state
                        == manages_process_state
                    && !candidate->region_mode;
            });
        if (match != found->second.end()) {
            native_cohort = match->get();
        }
    }
    if (native_cohort == nullptr) {
        throw LlvmJitError(
            "LLVM process cohort must be materialized before binding");
    }

    auto bound = std::make_unique<Impl::NativeBoundCohortEntry>();
    bound->function = native_cohort->function;
    bound->members = std::move(members);
    bound->manages_process_state = manages_process_state;
    bound->region_mode = false;
    bound->runtimes.reserve(entries.size());
    bound->frames.reserve(entries.size());
    bound->results.reserve(entries.size());
    bound->statuses.resize(entries.size());
    bound->queued.reserve(entries.size());
    bound->waiting.reserve(entries.size());
    bound->process_statuses.reserve(entries.size());
    bound->active.reserve(entries.size());
    for (const auto& entry : entries) {
        bound->runtimes.push_back(entry.runtime);
        bound->frames.push_back(entry.frame);
        bound->results.push_back(entry.result);
        bound->queued.push_back(entry.queued);
        bound->waiting.push_back(entry.waiting_on_static);
        bound->process_statuses.push_back(entry.process_status);
        bound->active.push_back(entry.active);
    }
    auto* const result = bound.get();
    impl_->bound_cohorts.push_back(std::move(bound));
    return JitProcessCohortBinding { impl_.get(), result };
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const JitProcessCohortBinding cohort,
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || cohort.owner_ != impl_.get() || cohort.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM cohort binding");
    }
    auto& bound = *static_cast<Impl::NativeBoundCohortEntry*>(
        const_cast<void*>(cohort.entry_));
    if (entries.size() != bound.members.size()) {
        throw LlvmJitError("prevalidated LLVM cohort binding size changed");
    }

    const auto executed = bound.function(
        bound.runtimes.data(), bound.frames.data(), bound.results.data(),
        bound.statuses.data(), bound.queued.data(), bound.waiting.data(),
        bound.process_statuses.data(), bound.active.data(),
        static_cast<std::uint32_t>(bound.members.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "bound LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        entries[index].failure = { };
        entries[index].status = bound.statuses[index];
        if (bound.statuses[index]
            == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        bound.results[index]->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        bound.results[index]->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

std::size_t LlvmJit::resume_region_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries,
    const std::span<const std::size_t> active_indices) const
{
    if (!impl_ || entries.size() < 2U || active_indices.empty()
        || !std::ranges::is_sorted(active_indices)
        || std::ranges::adjacent_find(active_indices)
            != active_indices.end()
        || !std::ranges::all_of(
            active_indices,
            [&](const auto index) {
                if (index >= entries.size()) {
                    return false;
                }
                const auto& entry = entries[index];
                return entry.active != nullptr
                    && entry.queued != nullptr
                    && entry.waiting_on_static != nullptr
                    && entry.process_status != nullptr
                    && entry.process.owner_ == impl_.get()
                    && entry.process.entry_ != nullptr
                    && entry.runtime != nullptr && entry.frame != nullptr
                    && entry.result != nullptr;
            })) {
        throw LlvmJitError("invalid LLVM process region");
    }
    for (const auto index : active_indices) {
        auto& entry = entries[index];
        if (*entry.active == 0U) {
            throw LlvmJitError(
                "LLVM process region active index is not selected");
        }
        *entry.active = 0U;
        *entry.queued = 0U;
        *entry.waiting_on_static = 0U;
        *entry.process_status = 1U;
        const auto& native
            = *static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        entry.status = native.function(
            entry.runtime, entry.frame, entry.result);
        if (entry.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY) {
            *entry.waiting_on_static = 1U;
            *entry.process_status = 2U;
            continue;
        }
        if (entry.status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entry.result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entry.result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entry.failure = std::current_exception();
            }
        }
        return index + 1U;
    }
    return entries.size();
}

JitResumeStatus
LlvmJit::resume(const JitProcessBinding process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1) {
        throw LlvmJitError("JIT runtime ABI version mismatch");
    }
    if (runtime.struct_size < kJitRuntimeV1PrefixSize) {
        throw LlvmJitError("JIT runtime ABI structure is too small");
    }
    if (runtime.read_signal == nullptr || runtime.write_signal == nullptr || runtime.assert_failed == nullptr) {
        throw LlvmJitError("JIT runtime ABI requires all v1 callbacks");
    }
    if (result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1) {
        throw LlvmJitError("JIT resume-result ABI version mismatch");
    }
    if (result.struct_size < sizeof(fsim_jit_resume_result_v1)) {
        throw LlvmJitError("JIT resume-result ABI structure is too small");
    }

    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    if (impl_->options.require_direct_update_slots
        && !entry.info.frame_layout.direct_update_signals.empty()
        && (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.direct_update_slots == nullptr
            || runtime.direct_update_slot_count
                < entry.info.frame_layout.direct_update_signals.size()
            || runtime.direct_update_active_words == nullptr
            || runtime.direct_update_active_word_count
                < (entry.info.frame_layout.direct_update_signals.size() + 63U)
                    / 64U)) {
        throw LlvmJitError(
            "JIT runtime ABI requires every direct-update slot promised at "
            "lowering time");
    }
    if (entry.info.uses_write_update) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, write_after)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_update");
        }
        if (runtime.write_update == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_update for this process");
        }
    }
    if (entry.info.uses_write_after) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, flags)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_after");
        }
        if (runtime.write_after == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_after for this process");
        }
    }
    if (entry.info.uses_write_inertial) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_inertial_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_inertial");
        }
        if (runtime.write_inertial == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_inertial for this process");
        }
    }
    if (entry.info.uses_write_blocking_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_update_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_signal_slice");
        }
        if (runtime.write_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_signal_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_update_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_after_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_update_slice");
        }
        if (runtime.write_update_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_update_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_after_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_event)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_after_slice");
        }
        if (runtime.write_after_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_after_slice for this "
                "process");
        }
    }
    if (entry.info.uses_force_signal_slice) {
        if (runtime.struct_size < kJitRuntimeForceSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include force_signal_slice");
        }
        if (runtime.force_signal_slice == nullptr
            || (entry.info.frame_layout.uses_logic9
                && runtime.force_signal_slice_logic9 == nullptr)) {
            throw LlvmJitError(
                "JIT runtime ABI requires force_signal_slice callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_release_signal_slice) {
        if (runtime.struct_size < kJitRuntimeForceSize
            || runtime.release_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires release_signal_slice for this process");
        }
    }
    if (entry.info.uses_force_driver_signal_slice) {
        if (runtime.struct_size < kJitRuntimeDriverForceSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "force_driver_signal_slice");
        }
        if (runtime.force_driver_signal_slice == nullptr
            || (entry.info.frame_layout.uses_logic9
                && runtime.force_driver_signal_slice_logic9 == nullptr)) {
            throw LlvmJitError(
                "JIT runtime ABI requires force_driver_signal_slice callbacks for "
                "this process");
        }
    }
    if (entry.info.uses_release_driver_signal_slice) {
        if (runtime.struct_size < kJitRuntimeDriverForceSize
            || runtime.release_driver_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires release_driver_signal_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_inertial_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_projected)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_inertial_slice");
        }
        if (runtime.write_inertial_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_inertial_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_projected_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected");
        }
        if (runtime.write_projected == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected for this process");
        }
    }
    if (entry.info.uses_write_projected_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_projected_waveform)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_slice");
        }
        if (runtime.write_projected_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected_waveform) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1,
                write_projected_waveform_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_waveform");
        }
        if (runtime.write_projected_waveform == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_waveform for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected_waveform_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, read_signal_logic9)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_waveform_slice");
        }
        if (runtime.write_projected_waveform_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_waveform_slice for this "
                "process");
        }
    }
    if (entry.info.uses_debug_points
        && runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, reserved)) {
        throw LlvmJitError(
            "JIT runtime ABI structure does not include debug-point flags");
    }
    if (entry.info.uses_signal_event) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_last_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_event");
        }
        if (runtime.signal_event == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_event for this process");
        }
    }
    if (entry.info.uses_signal_last_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_last_event)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_value");
        }
        if (runtime.signal_last_value == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_value for this process");
        }
    }
    if (entry.info.uses_signal_last_event) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_active)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_event");
        }
        if (runtime.signal_last_event == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_event for this process");
        }
    }
    if (entry.info.uses_simulation_time) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, vital_timing_check)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include read_simulation_time");
        }
        if (runtime.read_simulation_time == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires read_simulation_time for this process");
        }
    }
    if (entry.info.uses_vital_timing) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, vital_delay)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include vital_timing_check");
        }
        if (runtime.vital_timing_check == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires vital_timing_check for this process");
        }
    }
    if (entry.info.uses_vital_delay) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, force_driver_signal_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include vital_delay");
        }
        if (runtime.vital_delay == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires vital_delay for this process");
        }
    }
    if (entry.info.uses_signal_active) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_output)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_active");
        }
        if (runtime.signal_active == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_active for this process");
        }
    }
    if (entry.info.uses_signal_last_active) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_driving)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_active");
        }
        if (runtime.signal_last_active == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_active for this process");
        }
    }
    if (entry.info.uses_signal_driving) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_driving_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_driving");
        }
        if (runtime.signal_driving == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_driving for this process");
        }
    }
    if (entry.info.uses_signal_driving_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, read_simulation_time)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_driving_value");
        }
        if (runtime.signal_driving_value == nullptr
            || runtime.signal_driving_value_logic9 == nullptr) {
            throw LlvmJitError(
                "JIT runtime requires signal driving-value callbacks for this process");
        }
    }
    if (entry.info.uses_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, schedule_output)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_output");
        }
        if (runtime.write_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_output for this process");
        }
    }
    if (entry.info.uses_postponed_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_report)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include schedule_output");
        }
        if (runtime.schedule_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires schedule_output for this process");
        }
    }
    if (entry.info.uses_report) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_formatted)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_report");
        }
        if (runtime.write_report == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_report for this process");
        }
    }
    if (entry.info.uses_formatted_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_time)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_formatted");
        }
        if (runtime.write_formatted == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_formatted for this process");
        }
    }
    if (entry.info.uses_time_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, install_monitor)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_time");
        }
        if (runtime.write_time == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_time for this process");
        }
    }
    if (entry.info.uses_monitor_install) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, control_monitor)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include install_monitor");
        }
        if (runtime.install_monitor == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires install_monitor for this process");
        }
    }
    if (entry.info.uses_monitor_control) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, random_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include control_monitor");
        }
        if (runtime.control_monitor == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires control_monitor for this process");
        }
    }
    if (entry.info.uses_random_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_inertial)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include random_value");
        }
        if (runtime.random_value == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires random_value for this process");
        }
    }
    if (entry.info.frame_layout.uses_logic9) {
        if (runtime.struct_size < kJitRuntimeLogic9Size) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include Logic9 callbacks");
        }
        if (runtime.read_signal_logic9 == nullptr
            || runtime.write_signal_logic9 == nullptr
            || runtime.write_update_logic9 == nullptr
            || runtime.write_after_logic9 == nullptr
            || runtime.write_signal_slice_logic9 == nullptr
            || runtime.write_update_slice_logic9 == nullptr
            || runtime.write_after_slice_logic9 == nullptr
            || runtime.signal_last_value_logic9 == nullptr
            || runtime.write_inertial_logic9 == nullptr
            || runtime.write_inertial_slice_logic9 == nullptr
            || runtime.write_projected_logic9 == nullptr
            || runtime.write_projected_slice_logic9 == nullptr
            || runtime.write_projected_waveform_logic9 == nullptr
            || runtime.write_projected_waveform_slice_logic9 == nullptr
            || runtime.write_formatted_logic9 == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires Logic9 callbacks for this process");
        }
    }
    if (entry.info.uses_strings) {
        if (runtime.struct_size < kJitRuntimeStringSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include mutable-string "
                "callbacks");
        }
        if (runtime.load_string == nullptr
            || runtime.copy_string == nullptr
            || runtime.read_string_object == nullptr
            || runtime.write_string_object == nullptr
            || runtime.concatenate_strings == nullptr
            || runtime.compare_strings == nullptr
            || runtime.string_length == nullptr
            || runtime.string_index == nullptr
            || runtime.string_replace_code_point == nullptr
            || runtime.write_string_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires mutable-string callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_files) {
        if (runtime.struct_size < kJitRuntimeFileSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include text-file callbacks");
        }
        if (runtime.file_open == nullptr
            || runtime.file_close == nullptr
            || runtime.file_write == nullptr
            || runtime.file_read_line == nullptr
            || runtime.file_end_of_file == nullptr
            || runtime.file_error == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires text-file callbacks for this process");
        }
    }
    if (entry.info.uses_containers) {
        if (runtime.struct_size
                < kJitRuntimeContainerWordSize
            || runtime.container_operation == nullptr
            || runtime.container_read_word == nullptr
            || runtime.container_write_word == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires bounded-container callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_wide_container_operation) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.container_read_packed == nullptr
            || runtime.container_write_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires arbitrary-width packed-container "
                "callbacks for this process");
        }
    }
    if (entry.info.uses_exact_signal_operation) {
        if (runtime.struct_size < kJitRuntimeExactSignalSize
            || runtime.execute_signal_operation == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires execute_signal_operation for this "
                "process");
        }
    }
    if (entry.info.uses_wide_signal_read) {
        if (runtime.struct_size < kJitRuntimeWideSignalReadSize
            || runtime.read_signal_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires read_signal_packed for this process");
        }
    }
    if (entry.info.uses_wide_signal_write) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.write_signal_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_signal_packed for this process");
        }
    }
    if (frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1) {
        throw LlvmJitError("JIT frame ABI version mismatch");
    }
    if (frame.struct_size < kJitFrameV1PrefixSize
        || ((entry.info.frame_layout.uses_logic9
                || entry.info.uses_native_call_stack)
            && frame.struct_size < sizeof(fsim_jit_frame_v1))) {
        throw LlvmJitError("JIT frame ABI structure is too small");
    }
    if (frame.layout_id_low != entry.info.frame_layout.layout_id_low || frame.layout_id_high != entry.info.frame_layout.layout_id_high || frame.register_count != entry.info.frame_layout.register_count) {
        throw LlvmJitError("JIT frame layout mismatch");
    }
    if ((entry.info.frame_layout.register_word_count != 0
            && (frame.register_aval == nullptr
                || frame.register_bval == nullptr))
        || (frame.register_count != 0
            && frame.register_initialized == nullptr)) {
        throw LlvmJitError("JIT frame register storage is null");
    }
    if (entry.info.frame_layout.register_word_count != 0
        && frame.register_aval == frame.register_bval) {
        throw LlvmJitError(
            "JIT frame aval and bval register storage must be distinct");
    }
    if (entry.info.frame_layout.uses_logic9
        && entry.info.frame_layout.register_word_count != 0
        && (frame.register_logic9_plane2 == nullptr
            || frame.register_logic9_plane3 == nullptr)) {
        throw LlvmJitError("JIT frame Logic9 register storage is null");
    }

    const auto terminal_result =
        [&](const std::uint32_t status) -> JitResumeStatus {
        result.status = status;
        result.instruction = frame.last_instruction;
        result.delay = 0;
        return static_cast<JitResumeStatus>(status);
    };
    switch (frame.state) {
    case FSIM_JIT_FRAME_STATE_READY:
        if (frame.program_counter >= entry.info.operation_count) {
            throw LlvmJitError(
                "JIT frame program counter is outside the operation stream");
        }
        break;
    case FSIM_JIT_FRAME_STATE_COMPLETED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_COMPLETED);
    case FSIM_JIT_FRAME_STATE_STOPPED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_STOPPED);
    case FSIM_JIT_FRAME_STATE_ASSERTION_FAILED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED);
    case FSIM_JIT_FRAME_STATE_RUNTIME_ERROR:
        if (const auto reason = decode_generated_runtime_error(frame.program_counter)) {
            throw LlvmJitGeneratedRuntimeError(
                frame.last_instruction, *reason);
        }
        throw LlvmJitError(
            "JIT frame contains an invalid generated runtime error reason");
    default:
        throw LlvmJitError("JIT frame state is invalid");
    }

    const auto raw_status = entry.function(&runtime, &frame, &result);
    if (raw_status != result.status) {
        throw LlvmJitError(
            "generated process returned an inconsistent resume status");
    }
    switch (raw_status) {
    case FSIM_JIT_RESUME_STATUS_COMPLETED:
        if (frame.state != FSIM_JIT_FRAME_STATE_COMPLETED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::completed;
    case FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED:
        if (frame.state != FSIM_JIT_FRAME_STATE_ASSERTION_FAILED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::assertion_failed;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOR:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_for;
    case FSIM_JIT_RESUME_STATUS_WAIT_ON:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_on;
    case FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_sensitivity;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOREVER:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_forever;
    case FSIM_JIT_RESUME_STATUS_DEBUG_POINT:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::debug_point;
    case FSIM_JIT_RESUME_STATUS_YIELDED:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::yielded;
    case FSIM_JIT_RESUME_STATUS_PAUSED:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::paused;
    case FSIM_JIT_RESUME_STATUS_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork;
    case FSIM_JIT_RESUME_STATUS_FORK_END:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork_end;
    case FSIM_JIT_RESUME_STATUS_WAIT_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_fork;
    case FSIM_JIT_RESUME_STATUS_DISABLE_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::disable_fork;
    case FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::simir_boundary;
    case FSIM_JIT_RESUME_STATUS_STOPPED:
        if (frame.state != FSIM_JIT_FRAME_STATE_STOPPED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::stopped;
    case FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR:
        if (frame.state != FSIM_JIT_FRAME_STATE_RUNTIME_ERROR) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        if (const auto reason = decode_generated_runtime_error(result.delay)) {
            throw LlvmJitGeneratedRuntimeError(
                result.instruction, *reason);
        }
        throw LlvmJitError(
            "generated process returned an invalid runtime error reason");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}

JitExecutionStatus
LlvmJit::execute(const JitProcessHandle process,
    const fsim_jit_runtime_v1& runtime) const
{
    const auto binding = bind(process);
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(binding.entry_);
    if (entry.info.requires_resume) {
        throw LlvmJitUnsupportedError(
            "compiled process can suspend; use initialize_frame() and resume()");
    }

    std::vector<std::uint64_t> register_aval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint64_t> register_bval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(
        entry.info.frame_layout.register_count);
    std::vector<std::uint64_t> register_logic9_plane2(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    std::vector<std::uint64_t> register_logic9_plane3(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    fsim_jit_frame_v1 frame { };
    initialize_frame(
        binding,
        frame,
        register_aval,
        register_bval,
        register_initialized,
        register_logic9_plane2,
        register_logic9_plane3);
    fsim_jit_resume_result_v1 result {
        FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
        static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)),
        0,
        FSIM_JIT_INVALID_INSTRUCTION,
        0,
    };
    switch (resume(binding, runtime, frame, result)) {
    case JitResumeStatus::completed:
        return JitExecutionStatus::completed;
    case JitResumeStatus::assertion_failed:
        return JitExecutionStatus::assertion_failed;
    case JitResumeStatus::stopped:
        return JitExecutionStatus::stopped;
    case JitResumeStatus::wait_for:
    case JitResumeStatus::wait_on:
    case JitResumeStatus::wait_sensitivity:
    case JitResumeStatus::wait_forever:
    case JitResumeStatus::yielded:
    case JitResumeStatus::debug_point:
    case JitResumeStatus::paused:
    case JitResumeStatus::fork:
    case JitResumeStatus::fork_end:
    case JitResumeStatus::wait_fork:
    case JitResumeStatus::disable_fork:
    case JitResumeStatus::simir_boundary:
        throw LlvmJitError(
            "compiled process suspended during one-shot execution");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}

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
