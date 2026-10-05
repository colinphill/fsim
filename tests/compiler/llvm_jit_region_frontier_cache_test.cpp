// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_cache_test.hpp"

#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/compiler/object_cache.hpp"
#include "llvm_jit_region_frontier_staging_test.hpp"
#include "llvm_jit_region_frontier_test_access.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fsim::compiler::test {
namespace {

using runtime::simir::RegionConeActivationKernel;
using runtime::simir::RegionFrontierStatusV2;
using runtime::simir::SignalUpdateDomain;
using runtime::simir::ValueKind;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

class TemporaryCacheDirectory final {
public:
    TemporaryCacheDirectory()
    {
        static std::atomic_uint64_t next_suffix { };
        const auto tick = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path()
            / ("fsim-native-frontier-cache-" + std::to_string(tick) + "-"
                + std::to_string(next_suffix.fetch_add(
                    1U, std::memory_order_relaxed)));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryCacheDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    TemporaryCacheDirectory(const TemporaryCacheDirectory&) = delete;
    TemporaryCacheDirectory& operator=(
        const TemporaryCacheDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return root_;
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] LlvmJitOptions cache_options(
    const std::filesystem::path& directory,
    const JitOptimizationLevel optimization = JitOptimizationLevel::o2)
{
    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory = directory;
    options.cache_maximum_bytes.reset();
    options.cache_maximum_entries.reset();
    options.cache_maximum_age.reset();
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    return options;
}

void require_cache_statistics(const LlvmJitCacheStatistics& statistics,
    const std::uint64_t hits, const std::uint64_t misses,
    const std::uint64_t stores, const std::uint64_t rejected = 0U)
{
    require(statistics.hits == hits && statistics.misses == misses
            && statistics.stores == stores
            && statistics.rejected_entries == rejected,
        "native frontier cache counters match the cold, warm, or recovery path");
    require(statistics.load_failures == 0U
            && statistics.store_failures == 0U
            && statistics.prune_failures == 0U,
        "native frontier cache operation has no I/O or pruning failure");
}

[[nodiscard]] std::vector<std::filesystem::path> cached_object_paths(
    const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> result;
    const auto object_root = directory / "llvm" / "objects";
    if (!std::filesystem::exists(object_root)) {
        return result;
    }
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator { object_root }) {
        if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
            result.push_back(entry.path());
        }
    }
    std::ranges::sort(result);
    return result;
}

[[nodiscard]] RegionFrontierCacheBindingResult run_input_probe(
    const RegionConeActivationKernel& kernel,
    const LlvmRegionFrontierExecutor& executor,
    const RegionFrontierTestValue& external_input = { { 1U }, { 0U } },
    const RegionFrontierTestValue& initial_internal = { { 0U }, { 0U } })
{
    return run_cache_input_binding_witness(kernel, executor.step_entry(),
        executor.layout(), 71U, external_input, initial_internal);
}

void require_same_probe(const RegionFrontierCacheBindingResult& left,
    const RegionFrontierCacheBindingResult& right)
{
    require(left.status == right.status
            && left.pending_aval == right.pending_aval
            && left.pending_bval == right.pending_bval
            && left.member_dispatches == right.member_dispatches,
        "cold and warm native entries stage identical values and dispatches");
}

void require_one_bit_pending(const RegionFrontierCacheBindingResult& result,
    const std::uint64_t aval)
{
    require(result.status == RegionFrontierStatusV2::need_scheduler_keys,
        "the generated entry executes the certified producer member");
    require(result.pending_aval == std::vector<std::uint64_t> { aval }
            && result.pending_bval == std::vector<std::uint64_t> { 0U }
            && result.member_dispatches == 1U,
        "the generated entry stages the expected one-bit internal value");
}

void require_miss_and_store(
    const LlvmRegionFrontierExecutor& executor)
{
    require_cache_statistics(executor.cache_statistics(), 0U, 2U, 2U);
    require_cache_statistics(
        llvm_detail::RegionFrontierTestAccess::body_cache_statistics(executor),
        0U, 1U, 1U);
    require_cache_statistics(
        llvm_detail::RegionFrontierTestAccess::wrapper_cache_statistics(executor),
        0U, 1U, 1U);
}

void check_cold_warm_and_identity_changes()
{
    TemporaryCacheDirectory cache;
    const auto options = cache_options(cache.path());
    auto kernel = make_certified_frontier_kernel(1U);

    RegionFrontierCacheBindingResult cold_result;
    {
        auto cold = LlvmRegionFrontierExecutor::try_create(
            kernel, options, "frontier-cache-design-a");
        require(cold != nullptr && cold->step_entry() != nullptr,
            "a cold certified frontier materializes a callable native entry");
        require_miss_and_store(*cold);
        cold_result = run_input_probe(kernel, *cold);
        require_one_bit_pending(cold_result, 1U);
    }
    require(cached_object_paths(cache.path()).size() == 2U,
        "one cold native frontier stores separate body and wrapper objects");

    {
        auto warm = LlvmRegionFrontierExecutor::try_create(
            kernel, options, "frontier-cache-design-a");
        require(warm != nullptr && warm->step_entry() != nullptr,
            "a warm certified frontier returns a callable native entry");
        require_cache_statistics(warm->cache_statistics(), 2U, 0U, 0U);
        require(!llvm_detail::RegionFrontierTestAccess::body_registry_reused(*warm)
                && !llvm_detail::RegionFrontierTestAccess::wrapper_registry_reused(*warm),
            "disk-warm cache test recreates released owners");
        require_cache_statistics(
            llvm_detail::RegionFrontierTestAccess::body_cache_statistics(*warm),
            1U, 0U, 0U);
        require_cache_statistics(
            llvm_detail::RegionFrontierTestAccess::wrapper_cache_statistics(*warm),
            1U, 0U, 0U);
        const auto warm_result = run_input_probe(kernel, *warm);
        require_same_probe(cold_result, warm_result);
    }

    auto swapped = kernel;
    auto external = std::ranges::find_if(swapped.inputs,
        [](const auto& input) { return !input.internal; });
    auto internal = std::ranges::find_if(swapped.inputs,
        [](const auto& input) { return input.internal; });
    require(external != swapped.inputs.end()
            && internal != swapped.inputs.end()
            && external->width == internal->width
            && external->value_kind == internal->value_kind
            && external->value_register != internal->value_register,
        "the fixture has distinct equal-width boundary and internal inputs");
    std::swap(external->value_register, internal->value_register);
    {
        auto changed = LlvmRegionFrontierExecutor::try_create(
            swapped, options, "frontier-cache-design-a");
        require(changed != nullptr,
            "the swapped equal-width input mapping remains a certified kernel");
        require_miss_and_store(*changed);
        const auto changed_result = run_input_probe(swapped, *changed);
        require_one_bit_pending(changed_result, 0U);
        require(changed_result.pending_aval != cold_result.pending_aval,
            "changed register-to-signal mapping changes generated reads and output");
    }
    require(cached_object_paths(cache.path()).size() == 4U,
        "swapped input bindings cannot reuse the original body or wrapper objects");

    {
        auto changed_design = LlvmRegionFrontierExecutor::try_create(
            kernel, options, "frontier-cache-design-b");
        require(changed_design != nullptr,
            "an immutable design identity variation still compiles natively");
        require_miss_and_store(*changed_design);
        require_one_bit_pending(run_input_probe(kernel, *changed_design), 1U);
    }
    require(cached_object_paths(cache.path()).size() == 6U,
        "immutable design identity participates in both native object keys");

    {
        auto changed_optimization = LlvmRegionFrontierExecutor::try_create(
            kernel, cache_options(cache.path(), JitOptimizationLevel::o0),
            "frontier-cache-design-a");
        require(changed_optimization != nullptr,
            "the O0 native frontier materializes independently");
        require_miss_and_store(*changed_optimization);
        require_one_bit_pending(
            run_input_probe(kernel, *changed_optimization), 1U);
    }
    require(cached_object_paths(cache.path()).size() == 8U,
        "optimization and the host tier invalidate both object keys");

    auto wide_kernel = make_certified_frontier_kernel(65U);
    const RegionFrontierTestValue wide_external { { 1U, 0U }, { 0U, 0U } };
    const RegionFrontierTestValue wide_internal { { 0U, 0U }, { 0U, 0U } };
    {
        auto changed_layout = LlvmRegionFrontierExecutor::try_create(
            wide_kernel, options, "frontier-cache-design-a");
        require(changed_layout != nullptr,
            "a widened certified layout materializes a native entry");
        require_miss_and_store(*changed_layout);
        const auto wide_result = run_input_probe(wide_kernel, *changed_layout,
            wide_external, wide_internal);
        require(wide_result.status == RegionFrontierStatusV2::need_scheduler_keys
                && wide_result.pending_aval
                    == std::vector<std::uint64_t> { 1U, 0U }
                && wide_result.pending_bval
                    == std::vector<std::uint64_t> { 0U, 0U },
            "the changed packed layout executes with the matching wide frame");
    }
    require(cached_object_paths(cache.path()).size() == 10U,
        "signal width and packed layout participate in both object keys");

    auto logic9_kernel = make_certified_frontier_kernel(1U,
        SignalUpdateDomain::systemverilog_active, ValueKind::logic9);
    {
        auto changed_value_kind = LlvmRegionFrontierExecutor::try_create(
            logic9_kernel, options, "frontier-cache-design-a");
        require(changed_value_kind != nullptr,
            "the typed Logic9 layout materializes in the same cache");
        require_miss_and_store(*changed_value_kind);
        const auto& logic9_layout = changed_value_kind->layout();
        require(region_frontier_layout_header_valid_v2(logic9_layout)
                && logic9_layout.signal_slot_count != 0U
                && logic9_layout.signals != nullptr
                && std::all_of(logic9_layout.signals,
                    logic9_layout.signals + logic9_layout.signal_slot_count,
                    [](const auto& signal) {
                        return signal.value_kind
                            == runtime::simir::RegionFrontierValueKindV2::logic9;
                    }),
            "the cached Logic9 entry publishes the V2 typed-plane contract");
        run_region_frontier_staging_tests(logic9_kernel,
            changed_value_kind->step_entry(), changed_value_kind->layout(), 71U);
    }
    require(cached_object_paths(cache.path()).size() == 12U,
        "Logic4 and Logic9 value-plane layouts cannot share native objects");
}

enum class RejectedLayer : std::uint8_t {
    body,
    wrapper,
};

[[nodiscard]] RejectedLayer require_recovered_cache_layer(
    const LlvmRegionFrontierExecutor& executor)
{
    const auto body
        = llvm_detail::RegionFrontierTestAccess::body_cache_statistics(executor);
    const auto wrapper
        = llvm_detail::RegionFrontierTestAccess::wrapper_cache_statistics(executor);
    const bool body_rejected = body.rejected_entries == 1U;
    const bool wrapper_rejected = wrapper.rejected_entries == 1U;
    require(body_rejected != wrapper_rejected,
        "one corrupted native object is attributed to exactly one cache layer");
    if (body_rejected) {
        require_cache_statistics(body, 0U, 1U, 1U, 1U);
        require_cache_statistics(wrapper, 1U, 0U, 0U);
        return RejectedLayer::body;
    }
    require_cache_statistics(wrapper, 0U, 1U, 1U, 1U);
    require_cache_statistics(body, 1U, 0U, 0U);
    return RejectedLayer::wrapper;
}

void check_corrupt_object_recompiles()
{
    TemporaryCacheDirectory cache;
    const auto options = cache_options(cache.path());
    const auto kernel = make_certified_frontier_kernel(1U);
    constexpr std::string_view design_identity {
        "frontier-corruption-recovery"
    };
    {
        auto cold = LlvmRegionFrontierExecutor::try_create(
            kernel, options, design_identity);
        require(cold != nullptr,
            "the corruption test begins with materialized body and wrapper objects");
        require_miss_and_store(*cold);
        require_one_bit_pending(run_input_probe(kernel, *cold), 1U);
    }
    const auto paths = cached_object_paths(cache.path());
    require(paths.size() == 2U,
        "the corruption test has independent body and wrapper objects");

    std::optional<RejectedLayer> first_rejected_layer;
    for (std::size_t index = 0U; index < paths.size(); ++index) {
        {
            std::ofstream object { paths[index],
                std::ios::binary | std::ios::trunc };
            require(static_cast<bool>(object),
                "the test can replace either persistent object with malformed bytes");
            object << "not a native object cache record";
            require(static_cast<bool>(object),
                "malformed bytes reach the selected object layer");
        }

        auto recovered = LlvmRegionFrontierExecutor::try_create(
            kernel, options, design_identity);
        require(recovered != nullptr && recovered->step_entry() != nullptr,
            "a corrupt object falls back to production compilation");
        require_cache_statistics(recovered->cache_statistics(),
            1U, 1U, 1U, 1U);
        const auto rejected_layer = require_recovered_cache_layer(*recovered);
        require(!first_rejected_layer || rejected_layer != *first_rejected_layer,
            "malformed body and wrapper objects recover independently");
        first_rejected_layer = rejected_layer;
        require_one_bit_pending(run_input_probe(kernel, *recovered), 1U);
    }
    require(first_rejected_layer.has_value(),
        "both native object layers are covered by corruption recovery");
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::vector<std::byte>& bytes, const std::size_t offset)
{
    require(offset <= bytes.size() && bytes.size() - offset >= 4U,
        "native cache metadata field is within the stored record");
    std::uint32_t result { };
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[offset + byte]))
            << (byte * 8U);
    }
    return result;
}

void reject_checksum_valid_mutation(
    const std::string_view design_identity,
    const bool mutate_identity_digest)
{
    TemporaryCacheDirectory cache;
    const auto options = cache_options(cache.path());
    const auto kernel = make_certified_frontier_kernel(1U);
    {
        auto cold = LlvmRegionFrontierExecutor::try_create(
            kernel, options, design_identity);
        require(cold != nullptr,
            "metadata rejection starts with both valid object layers");
        require_miss_and_store(*cold);
        require_one_bit_pending(run_input_probe(kernel, *cold), 1U);
    }

    const auto paths = cached_object_paths(cache.path());
    require(paths.size() == 2U,
        "metadata rejection mutates one of two separate cache objects");
    ObjectCache storage { cache.path() / "llvm" / "objects" };
    std::error_code error;
    std::array<std::vector<std::byte>, 2U> valid_records;
    for (std::size_t index = 0U; index < paths.size(); ++index) {
        auto record = storage.load(paths[index].stem().string(), error);
        require(record.has_value() && !error,
            "the original object-cache record is readable before mutation");
        valid_records[index] = std::move(*record);
    }

    std::optional<RejectedLayer> rejected_layer;
    std::size_t identity_rejections { };
    for (std::size_t index = 0U; index < paths.size(); ++index) {
        auto record = valid_records[index];
        require(record.size() >= 49U,
            "the shared body and exact wrapper carry versioned metadata");
        const auto metadata_size = read_u32_le(record, 12U);
        const auto identity_size = read_u32_le(record, 40U);
        require(metadata_size >= 37U && identity_size != 0U
                && identity_size <= metadata_size - 37U,
            "the object record contains a bounded exact cache identity");

        std::size_t mutation_offset { 44U };
        if (!mutate_identity_digest) {
            // Preserve the identity digest and corrupt only the tier bit so
            // metadata comparison, rather than key identity, rejects the object.
            mutation_offset += static_cast<std::size_t>(identity_size)
                + sizeof(std::uint32_t);
            const auto metadata_end
                = std::size_t { 24U } + metadata_size;
            require(mutation_offset < metadata_end
                    && mutation_offset < record.size()
                    && std::to_integer<std::uint8_t>(record[mutation_offset])
                        == 1U,
                "the tier eligibility byte is inside the encoded metadata");
        }
        record[mutation_offset] ^= std::byte { 1U };
        const auto key = paths[index].stem().string();
        require(storage.store(key, record, error) && !error,
            "the object cache reseals the checksum-valid metadata mutation");

        bool rejected { };
        RejectedLayer observed_layer { RejectedLayer::body };
        try {
            auto incompatible = LlvmRegionFrontierExecutor::try_create(
                kernel, options, design_identity);
            static_cast<void>(incompatible);
        } catch (const LlvmJitError& exception) {
            const std::string_view message { exception.what() };
            if (mutate_identity_digest) {
                rejected = message.find(
                               "cached LLVM native object has incompatible ABI, "
                               "semantics, optimization-tier, or target identity")
                    != std::string_view::npos;
                if (rejected) {
                    ++identity_rejections;
                }
            } else if (message.find("cached shared frontier metadata")
                != std::string_view::npos) {
                observed_layer = RejectedLayer::body;
                rejected = true;
            } else if (message.find("cached exact frontier wrapper metadata")
                != std::string_view::npos) {
                observed_layer = RejectedLayer::wrapper;
                rejected = true;
            }
        }
        require(rejected,
            "the selected layer rejects checksum-valid stale metadata");
        if (!mutate_identity_digest) {
            require(!rejected_layer || observed_layer != *rejected_layer,
                "the two distinct files exercise body and wrapper validation");
            rejected_layer = observed_layer;
        }
        require(storage.store(key, valid_records[index], error) && !error,
            "the valid object is restored before testing the other layer");
    }
    if (mutate_identity_digest) {
        require(identity_rejections == paths.size(),
            "the object cache rejects identity mutations in both object records before native metadata checks");
    } else {
        require(rejected_layer.has_value(),
            "both cache layers independently validate embedded metadata");
    }
}

void check_checksum_valid_v2_typed_plane_key_mismatch_rejects()
{
    reject_checksum_valid_mutation(
        "frontier-metadata-identity", true);
}

void check_checksum_valid_frontier_metadata_mismatch_rejects()
{
    reject_checksum_valid_mutation(
        "frontier-metadata-field-mismatch", false);
}

void check_observer_options_decline()
{
    const auto kernel = make_certified_frontier_kernel(1U);
    LlvmJitOptions debug_options;
    require(debug_options.debug_instrumentation,
        "default JIT options retain debug instrumentation");
    require(LlvmRegionFrontierExecutor::try_create(
                kernel, debug_options, "debug-frontier-decline") == nullptr,
        "native frontier declines when debug instrumentation is requested");

    LlvmJitOptions coverage_options;
    coverage_options.debug_instrumentation = false;
    coverage_options.code_coverage_identity = "frontier-coverage-test";
    require(LlvmRegionFrontierExecutor::try_create(
                kernel, coverage_options, "coverage-frontier-decline")
            == nullptr,
        "native frontier declines when its coverage identity is enabled");
}

} // namespace

void run_region_frontier_cache_tests()
{
    check_observer_options_decline();
    check_cold_warm_and_identity_changes();
    check_corrupt_object_recompiles();
    check_checksum_valid_v2_typed_plane_key_mismatch_rejects();
    check_checksum_valid_frontier_metadata_mismatch_rejects();
}

} // namespace fsim::compiler::test
