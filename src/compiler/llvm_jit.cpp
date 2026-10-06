// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit.hpp"
#include "llvm_jit_impl.hpp"
#include "llvm_jit_compilation_contexts.hpp"
#include "llvm_jit_codegen_preparation.hpp"
#include "llvm_jit_internal.hpp"
#include "llvm_jit_fast_isel_census.hpp"
#include "llvm_jit_llvm_args.hpp"
#include "native_cache_schema.hpp"
#include "llvm/region_frontier_initial_slot_validation.hpp"

#include "fsim/compiler/object_cache.hpp"
#include "fsim/support/bounded_bytes.hpp"

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
#include <llvm/IR/Metadata.h>

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
#include <string_view>
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

namespace llvm_detail {

    constexpr std::array<std::byte, 8> kJitMetadataMagic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' },
        std::byte { 'M' }, std::byte { 'J' }, std::byte { 'M' },
        std::byte { '5' }, std::byte { 0 }
    };
    constexpr std::array<std::byte, 8> kJitCacheRecordMagic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' },
        std::byte { 'M' }, std::byte { 'J' }, std::byte { 'O' },
        std::byte { '3' }, std::byte { 0 }
    };
    constexpr std::uint32_t kJitMetadataSchema = 9U;
    constexpr std::uint32_t kJitCacheRecordSchema = 1U;
    constexpr std::size_t kMaximumJitMetadataBytes = 64U * 1024U * 1024U;

    class JitMetadataWriter final {
    public:
        void append_u8(const std::uint8_t value)
        {
            require_write(writer_.write_u8(value));
        }

        void append_u32(const std::uint32_t value)
        {
            require_write(writer_.write_u32_le(value));
        }

        void append_u64(const std::uint64_t value)
        {
            require_write(writer_.write_u64_le(value));
        }

        void append_bytes(const std::span<const std::byte> bytes)
        {
            require_write(writer_.append(bytes));
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

        void append_u8_vector(const std::span<const std::uint8_t> values)
        {
            if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
                throw LlvmJitError("LLVM cache metadata vector is too large");
            }
            append_u32(static_cast<std::uint32_t>(values.size()));
            for (const auto value : values) {
                if (value > 1U) {
                    throw LlvmJitError(
                        "LLVM cache metadata register persistence is invalid");
                }
                append_u8(value);
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
        static void require_write(const bool succeeded)
        {
            if (!succeeded) {
                throw LlvmJitError("LLVM cache metadata is too large");
            }
        }

        std::vector<std::byte> bytes_;
        support::BoundedByteWriter<std::vector<std::byte>> writer_ {
            bytes_, kMaximumJitMetadataBytes
        };
    };

    class JitMetadataReader final {
    public:
        explicit JitMetadataReader(const std::span<const std::byte> bytes)
            : bytes_(bytes)
            , reader_(bytes, kMaximumJitMetadataBytes)
        {
        }

        [[nodiscard]] bool read_u8(std::uint8_t& value)
        {
            return reader_.read_u8(value);
        }

        [[nodiscard]] bool read_u32(std::uint32_t& value)
        {
            return reader_.read_u32_le(value);
        }

        [[nodiscard]] bool read_u64(std::uint64_t& value)
        {
            return reader_.read_u64_le(value);
        }

        [[nodiscard]] bool read_bytes(
            const std::span<const std::byte>& expected)
        {
            const auto offset = reader_.position();
            if (offset > bytes_.size()
                || expected.size() > bytes_.size() - offset
                || !std::equal(
                    expected.begin(), expected.end(),
                    bytes_.begin() + static_cast<std::ptrdiff_t>(offset))) {
                return false;
            }
            std::span<const std::byte> ignored;
            return reader_.take(expected.size(), ignored);
        }

        [[nodiscard]] bool read_bytes(
            const std::size_t size,
            std::span<const std::byte>& result)
        {
            return reader_.take(size, result);
        }

        [[nodiscard]] bool read_u32_vector(
            std::vector<std::uint32_t>& values)
        {
            std::uint32_t count { };
            if (!read_u32(count)
                || count > reader_.remaining() / 4U) {
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

        [[nodiscard]] bool read_u8_vector(
            std::vector<std::uint8_t>& values)
        {
            std::uint32_t count { };
            if (!read_u32(count) || count > reader_.remaining()) {
                return false;
            }
            values.resize(count);
            for (auto& value : values) {
                if (!read_u8(value) || value > 1U) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool finished() const noexcept
        {
            return reader_.finished();
        }

    private:
        std::span<const std::byte> bytes_;
        support::BoundedByteReader reader_;
    };

    [[nodiscard]] bool cache_metadata_matches_identity(
        const std::span<const std::byte> metadata,
        const std::string_view expected_identity)
    {
        if (metadata.size() < 37U
            || metadata.size() > kMaximumJitMetadataBytes) {
            return false;
        }
        JitMetadataReader header { metadata };
        std::uint32_t schema { };
        std::uint32_t encoded_size { };
        if (!header.read_bytes(kJitMetadataMagic)
            || !header.read_u32(schema)
            || schema != kJitMetadataSchema
            || !header.read_u32(encoded_size)
            || encoded_size < 37U
            || encoded_size > metadata.size()) {
            return false;
        }

        JitMetadataReader reader { metadata.first(encoded_size) };
        std::uint32_t cache_identity_size { };
        std::span<const std::byte> cache_identity;
        if (!reader.read_bytes(kJitMetadataMagic)
            || !reader.read_u32(schema)
            || !reader.read_u32(encoded_size)
            || !reader.read_u32(cache_identity_size)
            || !reader.read_bytes(cache_identity_size, cache_identity)) {
            return false;
        }
        const auto expected = std::span<const std::byte> {
            reinterpret_cast<const std::byte*>(expected_identity.data()),
            expected_identity.size()
        };
        return std::ranges::equal(cache_identity, expected);
    }

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
        const auto record_size = object_offset + object.size();
        std::vector<std::byte> result;
        result.reserve(record_size);
        support::BoundedByteWriter<std::vector<std::byte>> writer {
            result, record_size
        };
        const auto require_write = [](const bool succeeded) {
            if (!succeeded) {
                throw LlvmJitError("LLVM native cache record is too large");
            }
        };
        require_write(writer.append(kJitCacheRecordMagic));
        require_write(writer.write_u32_le(kJitCacheRecordSchema));
        require_write(writer.write_u32_le(
            static_cast<std::uint32_t>(metadata.size())));
        require_write(writer.write_u64_le(
            static_cast<std::uint64_t>(object.size())));
        require_write(writer.append(metadata));
        constexpr std::array<std::byte, 7> zero_padding { };
        const auto padding_size = object_offset - result.size();
        require_write(writer.append(std::span<const std::byte> {
            zero_padding.data(), padding_size
        }));
        require_write(writer.append(object));
        return result;
    }

    [[nodiscard]] std::optional<JitCacheRecordView> parse_jit_cache_record(
        const std::span<const std::byte> bytes)
    {
        if (bytes.size() < 24U) {
            return std::nullopt;
        }
        support::BoundedByteReader reader { bytes };
        std::span<const std::byte> magic;
        if (!reader.take(kJitCacheRecordMagic.size(), magic)
            || !std::equal(
                kJitCacheRecordMagic.begin(), kJitCacheRecordMagic.end(),
                magic.begin())) {
            return std::nullopt;
        }
        std::uint32_t schema { };
        std::uint32_t encoded_metadata_size { };
        std::uint64_t object_size { };
        if (!reader.read_u32_le(schema)
            || !reader.read_u32_le(encoded_metadata_size)
            || !reader.read_u64_le(object_size)) {
            return std::nullopt;
        }
        const auto metadata_size
            = static_cast<std::size_t>(encoded_metadata_size);
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
        std::span<const std::byte> metadata;
        std::span<const std::byte> padding;
        std::span<const std::byte> object;
        if (!reader.take(metadata_size, metadata)
            || !reader.take(object_offset - reader.position(), padding)
            || !reader.take(static_cast<std::size_t>(object_size), object)
            || !reader.finished()) {
            return std::nullopt;
        }
        return JitCacheRecordView {
            metadata,
            object
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
    using NativeProcess = fsim_jit_process_v2;
    using NativeCohort = std::uint32_t(
        const fsim_jit_runtime_instance_v2* const*,
        fsim_jit_frame_v2* const*,
        fsim_jit_resume_result_v2* const*,
        std::uint32_t*, std::uint8_t* const*, std::uint8_t* const*,
        std::uint8_t* const*, std::uint8_t* const*, std::uint32_t);

    [[nodiscard]] std::vector<runtime::simir::SignalId>
    direct_update_signals(
        const Process& process,
        const std::span<const std::uint32_t> signal_widths,
        const std::span<const ValueKind> signal_value_kinds)
    {
        const bool has_tagged_scheduling = process.scheduling_domain
                != runtime::simir::ProcessSchedulingDomain::generic
            || std::ranges::any_of(
                process.operations,
                [](const auto& stored) {
                    return runtime::simir::visit_operation(
                        [](const auto& operation) {
                            if constexpr (requires { operation.domain; }) {
                                return operation.domain
                                    != runtime::simir::SignalUpdateDomain::generic;
                            }
                            return false;
                        },
                        stored);
                });
        if (has_tagged_scheduling) {
            // Per-operation callbacks preserve the ordering between generic
            // and tagged updates in mixed-domain processes.
            return {};
        }
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
                || signal_widths[read->signal] == 0U) {
                continue;
            }
            if (!signal_value_kinds.empty()) {
                if (read->signal >= signal_value_kinds.size()) {
                    continue;
                }
                const auto kind = signal_value_kinds[read->signal];
                if (kind != ValueKind::logic4
                    && kind != ValueKind::logic9) {
                    continue;
                }
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
                result.max_block_instructions = std::max(
                    result.max_block_instructions, block.size());
                for (const auto& instruction : block) {
                    ++result.instructions;
                    result.allocas += llvm::isa<llvm::AllocaInst>(instruction);
                    result.loads += llvm::isa<llvm::LoadInst>(instruction);
                    result.stores += llvm::isa<llvm::StoreInst>(instruction);
                    result.calls += llvm::isa<llvm::CallBase>(instruction);
                    result.branches += llvm::isa<llvm::BranchInst>(instruction);
                    result.switches += llvm::isa<llvm::SwitchInst>(instruction);
                    result.phis += llvm::isa<llvm::PHINode>(instruction);
                    result.returns += llvm::isa<llvm::ReturnInst>(instruction);
                    // Include operand widths: stores, compares and truncations
                    // can consume a wide integer without producing one.
                    std::size_t integer_width { };
                    const auto include_type = [&](const llvm::Type* type) {
                        if (const auto* integer
                            = llvm::dyn_cast<llvm::IntegerType>(type)) {
                            integer_width = std::max(integer_width,
                                static_cast<std::size_t>(
                                    integer->getBitWidth()));
                        }
                    };
                    include_type(instruction.getType());
                    for (const auto& operand : instruction.operands()) {
                        include_type(operand->getType());
                    }
                    const auto* const vector_type
                        = llvm::dyn_cast<llvm::FixedVectorType>(
                            instruction.getType());
                    const auto* const vector_element = vector_type == nullptr
                        ? nullptr
                        : llvm::dyn_cast<llvm::IntegerType>(
                              vector_type->getElementType());
                    if (vector_type != nullptr
                        && vector_type->getNumElements() == 4U
                        && vector_element != nullptr
                        && vector_element->getBitWidth() == 64U) {
                        const std::string_view opcode {
                            instruction.getOpcodeName() };
                        if (opcode == "and" || opcode == "or"
                            || opcode == "xor") {
                            const auto vector_opcode
                                = opcode == "xor"
                                    && instruction.getName().starts_with(
                                        "wide.bitwise.not")
                                ? std::string { "not" }
                                : std::string { opcode };
                            ++result.wide_vector_opcodes[vector_opcode];
                        }
                    }
                    result.max_integer_width = std::max(
                        result.max_integer_width, integer_width);
                    if (integer_width > 64U) {
                        ++result.wide_integer_opcodes[
                            instruction.getOpcodeName()];
                    }
                }
            }
        }
        return result;
    }

    [[nodiscard]] std::uint64_t ir_instruction_count(
        const llvm::Module& module) noexcept
    {
        std::uint64_t count { };
        for (const auto& function : module) {
            for (const auto& block : function) {
                count += block.size();
            }
        }
        return count;
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
            << " " << phase << "_phis=" << shape.phis
            << " " << phase << "_returns=" << shape.returns
            << " " << phase << "_max_block_instructions="
            << shape.max_block_instructions
            << " " << phase << "_max_integer_width="
            << shape.max_integer_width;
        for (const auto& [opcode, count] : shape.wide_integer_opcodes) {
            stream << " " << phase << "_wide_" << opcode << '=' << count;
        }
        for (const auto& [opcode, count] : shape.wide_vector_opcodes) {
            stream << " " << phase << "_vector_" << opcode << '=' << count;
        }
    }

    static_assert(std::is_standard_layout_v<fsim_jit_services_v2>);
    static_assert(std::is_standard_layout_v<fsim_jit_runtime_instance_v2>);
    static_assert(std::is_standard_layout_v<fsim_jit_frame_v2>);
    static_assert(std::is_standard_layout_v<fsim_jit_resume_result_v2>);
    static_assert(sizeof(std::uint32_t) == 4U);
    static_assert(sizeof(std::uint64_t) == 8U);
    static_assert(sizeof(fsim_jit_logic9_word_v2) == 32U);
    static_assert(sizeof(fsim_jit_projected_element_v2) == 24U);
    static_assert(sizeof(fsim_jit_update_slot_v2) == 80U);

    class TieredIRCompiler final
        : public llvm::orc::IRCompileLayer::IRCompiler {
    public:
        TieredIRCompiler(
            llvm::orc::JITTargetMachineBuilder machine_builder,
            llvm::ObjectCache* const object_cache,
            const bool profile_modules,
            std::shared_ptr<LlvmCompilationContexts> contexts)
            : IRCompiler(
                  llvm::orc::irManglingOptionsFromTargetOptions(
                      machine_builder.getOptions()))
            , none_(make_compiler(
                  machine_builder, object_cache,
                  llvm::CodeGenOptLevel::None))
            , less_(make_compiler(
                  std::move(machine_builder), object_cache,
                  llvm::CodeGenOptLevel::Less))
            , profile_modules_(profile_modules)
            , contexts_(std::move(contexts))
        {
        }

        llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>> operator()(
            llvm::Module& module) override
        {
            const auto tier_value = read_flag(
                module, "fsim.backend-codegen-tier");
            const auto eligibility_value = read_flag(
                module, "fsim.backend-tier-eligible");
            const auto instruction_count = read_flag(
                module, "fsim.backend-ir-instruction-count");
            const auto tier_selection_count = read_flag(
                module, "fsim.backend-tier-selection-ir-instruction-count");
            if (!tier_value && !eligibility_value && !instruction_count
                && !tier_selection_count) {
                return compile_with(*none_, module, llvm::CodeGenOptLevel::None);
            }
            if (!tier_value || !eligibility_value || !instruction_count
                || !tier_selection_count
                || *tier_value
                    > static_cast<std::uint64_t>(LlvmBackendTier::less)
                || *eligibility_value > 1U
                || *instruction_count != ir_instruction_count(module)
                || *instruction_count > *tier_selection_count
                || (static_cast<LlvmBackendTier>(*tier_value)
                        == LlvmBackendTier::none
                    && *instruction_count != *tier_selection_count)) {
                return llvm::createStringError(
                    llvm::inconvertibleErrorCode(),
                    "LLVM process module has invalid backend-tier proof");
            }
            const auto tier = static_cast<LlvmBackendTier>(*tier_value);
            const bool eligible = *eligibility_value != 0U;
            if (!valid_backend_tier_proof(
                    tier, eligible, *tier_selection_count)) {
                return llvm::createStringError(
                    llvm::inconvertibleErrorCode(),
                    "LLVM process module backend tier disagrees with its IR "
                    "instruction-count proof");
            }
            auto& compiler = tier == LlvmBackendTier::less
                ? *less_ : *none_;
            return compile_with(compiler, module,
                tier == LlvmBackendTier::less
                    ? llvm::CodeGenOptLevel::Less : llvm::CodeGenOptLevel::None);
        }

    private:
        [[nodiscard]] static std::unique_ptr<
            llvm::orc::IRCompileLayer::IRCompiler>
        make_compiler(
            llvm::orc::JITTargetMachineBuilder machine_builder,
            llvm::ObjectCache* const object_cache,
            const llvm::CodeGenOptLevel optimization)
        {
            machine_builder.setCodeGenOptLevel(optimization);
            return std::make_unique<llvm::orc::ConcurrentIRCompiler>(
                std::move(machine_builder), object_cache);
        }

        [[nodiscard]] static std::optional<std::uint64_t> read_flag(
            const llvm::Module& module,
            const llvm::StringRef name)
        {
            const auto* const metadata = module.getModuleFlag(name);
            if (metadata == nullptr) {
                return std::nullopt;
            }
            const auto* const integer
                = llvm::mdconst::dyn_extract<llvm::ConstantInt>(metadata);
            if (integer == nullptr || integer->getValue().getActiveBits() > 64U) {
                return std::nullopt;
            }
            return integer->getZExtValue();
        }

        [[nodiscard]] llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
        compile_with(
            llvm::orc::IRCompileLayer::IRCompiler& compiler,
            llvm::Module& module, const llvm::CodeGenOptLevel optimization)
        {
            // The tier proof was checked against optimized IR above. Keep
            // Less unchanged so preparation cannot bypass its instruction cap.
            if (optimization == llvm::CodeGenOptLevel::None
                && prepare_fast_isel_module(module)) {
                std::string error;
                llvm::raw_string_ostream output { error };
                if (llvm::verifyModule(module, &output)) {
                    return llvm::createStringError(
                        llvm::inconvertibleErrorCode(),
                        "LLVM codegen preparation produced invalid IR: " + error);
                }
            }
            FastIselCensus census { module, profile_modules_ };
            auto result = contexts_->compile(module, optimization, compiler);
            census.report(static_cast<bool>(result), llvm::errs());
            return result;
        }

        std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler> none_;
        std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler> less_;
        bool profile_modules_ { };
        std::shared_ptr<LlvmCompilationContexts> contexts_;
    };

    class PersistentLlvmObjectCache final : public LlvmObjectCache {
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
            bool preflight_miss { };
            {
                const std::lock_guard lock { preflight_mutex_ };
                preflight_miss = preflight_misses_.erase(
                    module->getModuleIdentifier()) != 0U;
            }
            if (!preflight_miss) {
                // Every cacheable module is checked by preflight before it is
                // added to ORC. A hit is inserted directly with addObjectFile;
                // this hook is only the compile-through path after a miss.
                // Returning no object here prevents an unvalidated second load.
                load_failures_.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
            misses_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }

        [[nodiscard]] std::unique_ptr<llvm::MemoryBuffer>
        preflight(
            const std::string_view key,
            std::vector<std::byte>* const metadata = nullptr) override
        {
            if (!valid_cache_key(key)) {
                return nullptr;
            }
            bool cache_identity_mismatch { };
            auto object = load_object(
                key, false, metadata, &cache_identity_mismatch, false);
            if (cache_identity_mismatch) {
                throw LlvmJitError(
                    "cached LLVM native object has incompatible ABI, semantics, "
                    "optimization-tier, or target identity");
            }
            if (!object) {
                const std::lock_guard lock { preflight_mutex_ };
                preflight_misses_.emplace(key);
            } else {
                const std::lock_guard lock { preflight_mutex_ };
                preflight_misses_.erase(std::string { key });
            }
            return object;
        }

        void accept_preflight_hit(const std::string_view key) override
        {
            {
                const std::lock_guard lock { preflight_mutex_ };
                const auto found = std::ranges::find(preflight_misses_, key);
                if (found != preflight_misses_.end()) {
                    preflight_misses_.erase(found);
                }
            }
            hits_.fetch_add(1, std::memory_order_relaxed);
        }

        void discard_preflight(const std::string_view key) override
        {
            const std::lock_guard lock { preflight_mutex_ };
            const auto found = std::ranges::find(preflight_misses_, key);
            if (found != preflight_misses_.end()) {
                preflight_misses_.erase(found);
            }
        }

        void stage_metadata(
            std::string key, std::vector<std::byte> metadata) override
        {
            const std::lock_guard lock { metadata_mutex_ };
            pending_metadata_.insert_or_assign(
                std::move(key), std::move(metadata));
        }

        void discard_staged_metadata(const std::string_view key) override
        {
            const std::lock_guard lock { metadata_mutex_ };
            pending_metadata_.erase(std::string { key });
        }

        [[nodiscard]] LlvmJitCacheStatistics statistics() const noexcept override
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
            std::vector<std::byte>* const metadata = nullptr,
            bool* const cache_identity_mismatch = nullptr,
            const bool record_hit = true)
        {
            if (cache_identity_mismatch != nullptr) {
                *cache_identity_mismatch = false;
            }
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
                if (!cache_metadata_matches_identity(record->metadata, key)) {
                    if (cache_identity_mismatch != nullptr) {
                        *cache_identity_mismatch = true;
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
                if (record_hit) {
                    hits_.fetch_add(1, std::memory_order_relaxed);
                }
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

    std::once_flag native_target_once;
    std::string native_target_error;

    void initialize_native_target()
    {
        static_cast<void>(initialize_llvm_arguments());
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

} // namespace llvm_detail

using namespace llvm_detail;

LlvmJitGeneratedRuntimeError::LlvmJitGeneratedRuntimeError(
    const std::uint32_t instruction,
    const JitGeneratedRuntimeErrorReason reason)
    : LlvmJitError(generated_runtime_error_message(instruction, reason))
    , instruction_(instruction)
    , reason_(reason)
{
}

std::vector<std::byte> LlvmJit::Impl::encode_module_metadata(
    const std::span<const ProcessInfo> processes,
    const std::string_view cache_identity,
    const llvm_detail::LlvmBackendTier backend_tier,
    const bool tier_eligible,
    const std::uint64_t tier_selection_instruction_count,
    const std::uint64_t optimized_instruction_count,
    const llvm_detail::TieredReadDedupStatistics read_dedup_statistics)
{
    if (processes.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("LLVM cache metadata has too many processes");
    }
    JitMetadataWriter writer;
    writer.append_bytes(kJitMetadataMagic);
    writer.append_u32(kJitMetadataSchema);
    constexpr std::size_t encoded_size_offset = 12U;
    writer.append_u32(0U);
    if (cache_identity.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("LLVM cache identity is too large");
    }
    writer.append_u32(static_cast<std::uint32_t>(cache_identity.size()));
    writer.append_bytes(std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(cache_identity.data()),
        cache_identity.size()
    });
    writer.append_u32(static_cast<std::uint32_t>(backend_tier));
    writer.append_u8(tier_eligible ? 1U : 0U);
    writer.append_u64(tier_selection_instruction_count);
    writer.append_u64(optimized_instruction_count);
    writer.append_u64(read_dedup_statistics.marked_loads);
    writer.append_u64(read_dedup_statistics.eliminated_loads);
    writer.append_u64(read_dedup_statistics.marked_value_loads);
    writer.append_u64(read_dedup_statistics.eliminated_value_loads);
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
        writer.append_u8(
            frame.signal_callback_ids_are_actual ? 1U : 0U);
        writer.append_u32(frame.signal_callback_operand_word_base);
        writer.append_u32_vector(frame.signal_callback_operands);
        writer.append_u32(process.operation_count);
        for (const auto flag : ProcessInfo::flags) {
            writer.append_u8(process.*flag ? 1U : 0U);
        }
        writer.append_u32_vector(process.entry_points);
        if (frame.register_values_persistent.size() != frame.register_count) {
            throw LlvmJitError(
                "LLVM cache metadata register persistence has the wrong size");
        }
        writer.append_u8_vector(frame.register_values_persistent);
    }
    if (writer.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("LLVM cache metadata is too large");
    }
    writer.patch_u32(
        encoded_size_offset, static_cast<std::uint32_t>(writer.size()));
    return writer.finish();
}

void LlvmJit::Impl::populate_direct_read_metadata(
    ProcessInfo& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds)
{
    const auto count = process.frame_layout.direct_read_signals.size();
    process.direct_read_widths.clear();
    process.direct_read_value_kinds.clear();
    process.direct_read_widths.reserve(count);
    process.direct_read_value_kinds.reserve(count);
    for (const auto signal : process.frame_layout.direct_read_signals) {
        if (signal >= signal_widths.size() || signal_widths[signal] == 0U
            || (!signal_value_kinds.empty()
                && signal >= signal_value_kinds.size())) {
            throw LlvmJitError(
                "LLVM direct-read signal metadata is unavailable");
        }
        const auto kind = signal_value_kinds.empty()
            ? runtime::simir::ValueKind::logic4
            : signal_value_kinds[signal];
        if (kind != runtime::simir::ValueKind::logic4
            && kind != runtime::simir::ValueKind::logic9) {
            throw LlvmJitError(
                "LLVM direct-read signal metadata has an unsupported value kind");
        }
        process.direct_read_widths.push_back(signal_widths[signal]);
        process.direct_read_value_kinds.push_back(kind);
    }
}

std::optional<LlvmJit::Impl::ModuleMetadata>
LlvmJit::Impl::decode_module_metadata(
    const std::span<const std::byte> metadata,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds,
    const std::string_view expected_cache_identity,
    const llvm_detail::LlvmBackendTier expected_backend_tier,
    const bool expected_tier_eligibility,
    const bool global_direct_read_requirement,
    bool* const identity_mismatch)
{
    if (metadata.size() < 37U
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
        || encoded_size < 37U
        || encoded_size > metadata.size()) {
        return std::nullopt;
    }

    if (identity_mismatch != nullptr) {
        *identity_mismatch = false;
    }
    JitMetadataReader reader { metadata.first(encoded_size) };
    std::uint32_t cache_identity_size { };
    std::uint32_t backend_tier_value { };
    std::uint8_t tier_eligibility_value { };
    std::uint64_t tier_selection_instruction_count { };
    std::uint64_t optimized_instruction_count { };
    llvm_detail::TieredReadDedupStatistics read_dedup_statistics;
    std::span<const std::byte> cache_identity;
    std::uint32_t process_count { };
    if (!reader.read_bytes(kJitMetadataMagic)
        || !reader.read_u32(schema)
        || !reader.read_u32(encoded_size)
        || !reader.read_u32(cache_identity_size)
        || !reader.read_bytes(cache_identity_size, cache_identity)
        || !reader.read_u32(backend_tier_value)
        || backend_tier_value
            > static_cast<std::uint32_t>(
                llvm_detail::LlvmBackendTier::less)
        || !reader.read_u8(tier_eligibility_value)
        || tier_eligibility_value > 1U
        || !reader.read_u64(tier_selection_instruction_count)
        || !reader.read_u64(optimized_instruction_count)
        || !reader.read_u64(read_dedup_statistics.marked_loads)
        || !reader.read_u64(read_dedup_statistics.eliminated_loads)
        || !reader.read_u64(read_dedup_statistics.marked_value_loads)
        || !reader.read_u64(read_dedup_statistics.eliminated_value_loads)
        || read_dedup_statistics.eliminated_loads
            > read_dedup_statistics.marked_loads
        || read_dedup_statistics.marked_value_loads
            > read_dedup_statistics.marked_loads
        || read_dedup_statistics.eliminated_value_loads
            > read_dedup_statistics.eliminated_loads
        || read_dedup_statistics.eliminated_value_loads
            > read_dedup_statistics.marked_value_loads) {
        return std::nullopt;
    }
    const auto expected_identity = std::span<const std::byte> {
        reinterpret_cast<const std::byte*>(expected_cache_identity.data()),
        expected_cache_identity.size()
    };
    if (!std::ranges::equal(cache_identity, expected_identity)) {
        if (identity_mismatch != nullptr) {
            *identity_mismatch = true;
        }
        return std::nullopt;
    }
    const auto backend_tier
        = static_cast<llvm_detail::LlvmBackendTier>(backend_tier_value);
    const bool tier_eligible = tier_eligibility_value != 0U;
    if (backend_tier != expected_backend_tier
        || tier_eligible != expected_tier_eligibility
        || optimized_instruction_count > tier_selection_instruction_count
        || (backend_tier == llvm_detail::LlvmBackendTier::none
            && optimized_instruction_count
                != tier_selection_instruction_count)
        || (backend_tier == llvm_detail::LlvmBackendTier::none
            && (read_dedup_statistics.marked_loads != 0U
                || read_dedup_statistics.eliminated_loads != 0U
                || read_dedup_statistics.marked_value_loads != 0U
                || read_dedup_statistics.eliminated_value_loads != 0U))
        || !llvm_detail::valid_backend_tier_proof(
            backend_tier, tier_eligible,
            tier_selection_instruction_count)) {
        if (identity_mismatch != nullptr) {
            *identity_mismatch = true;
        }
        return std::nullopt;
    }
    if (!reader.read_u32(process_count)
        || process_count != entries.size()) {
        return std::nullopt;
    }

    ModuleMetadata result;
    result.backend_tier = backend_tier;
    result.tier_eligible = tier_eligible;
    result.tier_selection_instruction_count
        = tier_selection_instruction_count;
    result.optimized_instruction_count = optimized_instruction_count;
    result.read_dedup_statistics = read_dedup_statistics;
    result.processes.resize(process_count);
    for (std::size_t index = 0U;
        index < result.processes.size(); ++index) {
        if (entries[index].process == nullptr) {
            return std::nullopt;
        }
        auto& process = result.processes[index];
        auto& frame = process.frame_layout;
        std::uint8_t uses_logic9 { };
        std::uint8_t tracks_register_initialization { };
        std::uint8_t signal_callback_ids_are_actual { };
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
            || !reader.read_u8(signal_callback_ids_are_actual)
            || signal_callback_ids_are_actual > 1U
            || !reader.read_u32(frame.signal_callback_operand_word_base)
            || !reader.read_u32_vector(frame.signal_callback_operands)
            || !reader.read_u32(process.operation_count)) {
            return std::nullopt;
        }
        frame.uses_logic9 = uses_logic9 != 0U;
        frame.tracks_register_initialization
            = tracks_register_initialization != 0U;
        frame.signal_callback_ids_are_actual
            = signal_callback_ids_are_actual != 0U;
        for (const auto flag : ProcessInfo::flags) {
            std::uint8_t value { };
            if (!reader.read_u8(value) || value > 1U) {
                return std::nullopt;
            }
            process.*flag = value != 0U;
        }
        if (process.requires_direct_read_signals
                != (global_direct_read_requirement
                    || entries[index].require_direct_read_signals)
            || process.tiered_read_dedup_safe
                != entries[index].tiered_read_dedup_safe
            || frame.signal_callback_ids_are_actual
                != entries[index].signal_callback_ids_are_actual) {
            return std::nullopt;
        }
        if (!reader.read_u32_vector(process.entry_points)) {
            return std::nullopt;
        }
        if (!reader.read_u8_vector(frame.register_values_persistent)) {
            return std::nullopt;
        }

        const auto& source = *entries[index].process;
        if (process.operation_count != source.operations.size()
            || frame.register_count != source.register_count
            || frame.string_register_count != source.string_register_count
            || frame.register_widths.size() != frame.register_count
            || frame.register_word_offsets.size() != frame.register_count
            || frame.register_values_persistent.size()
                != frame.register_count) {
            return std::nullopt;
        }
        const auto validated
            = validate_process(source, signal_widths, signal_value_kinds);
        const auto expected_callback_operands
            = frame.signal_callback_ids_are_actual
            ? llvm_detail::signal_callback_operands(source, signal_widths)
            : std::vector<runtime::simir::SignalId> { };
        if (frame.register_widths != validated.register_widths
            || frame.uses_logic9 != validated.uses_logic9
            || frame.signal_callback_operands
                != expected_callback_operands) {
            return std::nullopt;
        }
        std::uint64_t expected_word_offset { };
        for (std::size_t reg = 0U;
            reg < frame.register_widths.size(); ++reg) {
            const auto width = frame.register_widths[reg];
            const auto offset = frame.register_word_offsets[reg];
            const auto words = (static_cast<std::uint64_t>(width) + 63U) / 64U;
            if (offset != expected_word_offset
                || offset > frame.register_word_count
                || words > frame.register_word_count - offset) {
                return std::nullopt;
            }
            expected_word_offset += words;
            if (expected_word_offset
                > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
        }
        if (expected_word_offset
                != frame.signal_callback_operand_word_base
            || frame.signal_callback_operand_word_base
                > frame.register_word_count
            || frame.signal_callback_operands.size()
                != frame.register_word_count
                    - frame.signal_callback_operand_word_base) {
            return std::nullopt;
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
                frame.signal_callback_operands, valid_signal)
            || !std::ranges::all_of(
                process.entry_points,
                [&](const auto instruction) {
                    return instruction < process.operation_count;
                })) {
            return std::nullopt;
        }
        if (process.requires_direct_read_signals) {
            const auto expected_reads = direct_read_signals(
                *entries[index].process, signal_widths, signal_value_kinds);
            if (frame.direct_read_signals != expected_reads) {
                return std::nullopt;
            }
        }
        populate_direct_read_metadata(
            process, signal_widths, signal_value_kinds);
        process.direct_update_widths.reserve(
            frame.direct_update_signals.size());
        for (const auto signal : frame.direct_update_signals) {
            process.direct_update_widths.push_back(signal_widths[signal]);
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
#ifndef NDEBUG
    impl_->verify_optimized_modules = true;
#else
    const auto* const verify_modules = std::getenv("FSIM_VERIFY_LLVM_MODULES");
    impl_->verify_optimized_modules = verify_modules != nullptr
        && *verify_modules != '\0' && std::string_view { verify_modules } != "0";
#endif
    impl_->ordered_cohort_profile_enabled
        = std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr;

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
    }
    const bool profile_modules
        = std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr;
    // Every module producer uses a thread-safe compiler even without a cache.
    // Process lowering shares locked contexts per application worker, allowing
    // each context to reuse its own None/Less machines. Other module producers
    // retain ConcurrentIRCompiler's fresh-machine fallback.
    auto* const object_cache = impl_->object_cache.get();
    builder.setCompileFunctionCreator(
        [object_cache, profile_modules, implementation = impl_.get()](
            llvm::orc::JITTargetMachineBuilder machine_builder)
            -> llvm::Expected<std::unique_ptr<
                llvm::orc::IRCompileLayer::IRCompiler>> {
            auto contexts = std::make_shared<LlvmCompilationContexts>(
                machine_builder, object_cache);
            std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler> compiler
                = std::make_unique<TieredIRCompiler>(
                    std::move(machine_builder), object_cache,
                    profile_modules, contexts);
            implementation->compilation_contexts = std::move(contexts);
            return compiler;
        });
    builder.setJITTargetMachineBuilder(std::move(target_builder));
    impl_->jit = unwrap(builder.create(), "cannot create LLVM LLJIT");
    if (auto error = llvm_detail::define_initial_slot_validation_helper(
            *impl_->jit)) {
        throw LlvmJitError(
            "cannot register initial pending-slot validation helper: "
            + llvm_error(std::move(error)));
    }
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
    mingw_runtime_symbols[impl_->jit->getExecutionSession().intern("___chkstk_ms")] = llvm::orc::ExecutorSymbolDef(
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

LlvmJit::Impl::~Impl()
{
    if (compilation_contexts
        && std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr) {
        const auto counts = compilation_contexts->statistics();
        llvm::errs() << "fsim-profile: jit-context-summary contexts="
                     << counts.contexts
                     << " target_machines=" << counts.target_machines
                     << " reused_compilations=" << counts.reused_compilations
                     << " fallback_compilations=" << counts.fallback_compilations
                     << '\n';
    }
    if (ordered_cohort_profile_enabled) {
        llvm::errs() << "fsim-profile: sv-ordered-wrapper-summary"
                     << " materialization_attempts="
                     << ordered_cohort_materialization_attempts
                     << " budget_misses=" << ordered_cohort_budget_misses
                     << " fallback_batches="
                     << ordered_cohort_fallback_batches.load(
                            std::memory_order_relaxed)
                     << " fallback_members="
                     << ordered_cohort_fallback_members.load(
                            std::memory_order_relaxed)
                     << '\n';
    }
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

bool LlvmJit::discard_prevalidated_process(const Process& process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::scoped_lock lock { impl_->validation_mutex };
    return impl_->immutable_validated_processes.erase(&process) != 0U;
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

std::string llvm_detail::make_native_host_identity_fingerprint(
    const LlvmNativeHostIdentity& identity,
    const JitOptimizationLevel optimization,
    const std::string_view native_object_schema)
{
    CacheKeyBuilder builder;
    builder.add("kind", "fsim-llvm-native-host-v2");
    builder.add("build-configuration", FSIM_BUILD_CONFIGURATION);
    builder.add("llvm-version", identity.llvm_version);
    builder.add("optimization", to_string(optimization));
    builder.add("llvm-arguments", initialize_llvm_arguments());
    builder.add("backend-tier-policy", llvm_detail::kBackendTierPolicy);
    builder.add(
        "backend-tier-ir-instruction-limit",
        std::to_string(llvm_detail::kLessBackendTierInstructionLimit));
    builder.add("llvm-object-schema", native_object_schema);
    builder.add(
        "services-abi-version",
        std::to_string(FSIM_JIT_SERVICES_ABI_VERSION_V2));
    builder.add("services-abi-size", std::to_string(sizeof(fsim_jit_services_v2)));
    builder.add(
        "runtime-instance-abi-version",
        std::to_string(FSIM_JIT_RUNTIME_ABI_VERSION_V2));
    builder.add(
        "runtime-instance-abi-size",
        std::to_string(sizeof(fsim_jit_runtime_instance_v2)));
    builder.add(
        "frame-abi-version", std::to_string(FSIM_JIT_FRAME_ABI_VERSION_V2));
    builder.add("frame-abi-size", std::to_string(sizeof(fsim_jit_frame_v2)));
    builder.add(
        "resume-abi-version",
        std::to_string(FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2));
    builder.add(
        "resume-abi-size", std::to_string(sizeof(fsim_jit_resume_result_v2)));
    builder.add("target", identity.target);
    builder.add("data-layout", identity.data_layout);
    builder.add("cpu", identity.cpu);
    builder.add("feature-count", std::to_string(identity.features.size()));
    for (const auto& feature : identity.features) {
        builder.add("feature", feature);
    }
    return builder.finish();
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

    LlvmNativeHostIdentity identity {
        { }, LLVM_VERSION_STRING, target, data_layout, cpu,
        std::move(features)
    };
    identity.fingerprint
        = llvm_detail::make_native_host_identity_fingerprint(
            identity, optimization,
            llvm_detail::kNativeObjectCacheSchema);
    return identity;
}

} // namespace fsim::compiler
