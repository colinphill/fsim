// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_shared_body_cache_test.hpp"

#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "llvm_jit_region_frontier_private_access.hpp"
#include "llvm_jit_region_frontier_staging_test.hpp"
#include "llvm_jit_region_frontier_test_access.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace fsim::compiler::test {
namespace {

using llvm_detail::RegionFrontierTestAccess;
using llvm_detail::RegionFrontierPrivateAccess;
using runtime::simir::RegionConeActivationKernel;
using runtime::simir::RegionFrontierCacheBindingResult;
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
        root_ = std::filesystem::temp_directory_path() / ("fsim-frontier-shared-body-lifecycle-" + std::to_string(tick) + "-" + std::to_string(next_suffix.fetch_add(1U, std::memory_order_relaxed)));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryCacheDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    TemporaryCacheDirectory(const TemporaryCacheDirectory&) = delete;
    TemporaryCacheDirectory& operator=(const TemporaryCacheDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return root_;
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] LlvmJitOptions
cache_options(const std::filesystem::path& directory)
{
    LlvmJitOptions options;
    options.optimization = JitOptimizationLevel::o0;
    options.cache_directory = directory;
    options.cache_maximum_bytes.reset();
    options.cache_maximum_entries.reset();
    options.cache_maximum_age.reset();
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    return options;
}

[[nodiscard]] std::vector<std::filesystem::path>
cached_object_paths(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> paths;
    const auto object_root = directory / "llvm" / "objects";
    if (!std::filesystem::exists(object_root)) {
        return paths;
    }
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator { object_root }) {
        if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
            paths.push_back(entry.path());
        }
    }
    std::ranges::sort(paths);
    return paths;
}

[[nodiscard]] RegionFrontierCacheBindingResult
run_probe(const RegionConeActivationKernel& kernel,
    const LlvmRegionFrontierExecutor& executor)
{
    return run_cache_input_binding_witness(kernel, executor.step_entry(),
        executor.layout(), 71U);
}

void require_probe(const RegionConeActivationKernel& kernel,
    const LlvmRegionFrontierExecutor& executor)
{
    const auto result = run_probe(kernel, executor);
    require(result.status == RegionFrontierStatusV2::need_scheduler_keys && result.pending_aval == std::vector<std::uint64_t> { 1U } && result.pending_bval == std::vector<std::uint64_t> { 0U } && result.member_dispatches == 1U,
        "each live exact wrapper stages the expected producer result");
}

[[nodiscard]] bool same_probe_result(
    const RegionFrontierCacheBindingResult& left,
    const RegionFrontierCacheBindingResult& right)
{
    return left.status == right.status
        && left.pending_aval == right.pending_aval
        && left.pending_bval == right.pending_bval
        && left.member_dispatches == right.member_dispatches;
}

void require_private_probe_parity(const RegionConeActivationKernel& kernel,
    const LlvmRegionFrontierExecutor& executor)
{
    const auto canonical_values_entry
        = RegionFrontierPrivateAccess::canonical_values_entry(executor);
    const auto alias_and_canonical_values_entry
        = RegionFrontierPrivateAccess::alias_and_canonical_values_entry(
            executor);
    const auto descriptor_shapes_entry
        = RegionFrontierPrivateAccess::descriptor_shapes_entry(executor);
    require(canonical_values_entry != nullptr
            && alias_and_canonical_values_entry != nullptr
            && descriptor_shapes_entry != nullptr,
        "the exact wrapper resolves the canonical and shape entry addresses");

    const auto checked = run_probe(kernel, executor);
    const auto canonical_values = run_cache_input_binding_witness(kernel,
        canonical_values_entry, executor.layout(), 71U);
    const auto alias_and_canonical_values = run_cache_input_binding_witness(
        kernel, alias_and_canonical_values_entry, executor.layout(), 71U);
    const auto descriptor_shapes = run_cache_input_binding_witness(kernel,
        descriptor_shapes_entry, executor.layout(), 71U);
    require(same_probe_result(checked, canonical_values)
            && same_probe_result(checked, alias_and_canonical_values)
            && same_probe_result(checked, descriptor_shapes),
        "all private entries preserve checked status and complete pending state");
    require(checked.status == RegionFrontierStatusV2::need_scheduler_keys
            && checked.pending_aval == std::vector<std::uint64_t> { 1U }
            && checked.pending_bval == std::vector<std::uint64_t> { 0U }
            && checked.member_dispatches == 1U,
        "canonical-values cache probes use the expected valid producer result");
}

void require_clean_layer_statistics(const LlvmJitCacheStatistics& statistics)
{
    require(statistics.load_failures == 0U && statistics.store_failures == 0U && statistics.prune_failures == 0U,
        "shared body and wrapper cache accounting has no I/O failure");
}

void check_concurrent_registry_and_wrapper_lifetime()
{
    constexpr std::size_t caller_count { 4U };
    TemporaryCacheDirectory cache;
    const auto options = cache_options(cache.path());
    const auto kernel = make_certified_frontier_kernel(
        1U, SignalUpdateDomain::systemverilog_active, ValueKind::logic4);
    const auto design_identity = std::string_view { "frontier-shared-body-concurrent-lifecycle-v1" };

    std::barrier start_gate { static_cast<std::ptrdiff_t>(caller_count) };
    std::array<std::unique_ptr<LlvmRegionFrontierExecutor>, caller_count>
        executors;
    std::array<std::exception_ptr, caller_count> errors;
    std::array<std::thread, caller_count> workers;
    for (std::size_t index = 0U; index < caller_count; ++index) {
        workers[index] = std::thread { [&, index] {
            start_gate.arrive_and_wait();
            try {
                executors[index] = LlvmRegionFrontierExecutor::try_create(
                    kernel, options, design_identity);
            } catch (...) {
                errors[index] = std::current_exception();
            }
        } };
    }
    for (auto& worker : workers) {
        worker.join();
    }

    for (std::size_t index = 0U; index < executors.size(); ++index) {
        if (errors[index]) {
            std::rethrow_exception(errors[index]);
        }
        require(executors[index] != nullptr && executors[index]->step_entry() != nullptr,
            "concurrent exact plans all materialize callable wrappers");
    }

    const auto body_owner = RegionFrontierTestAccess::shared_body_owner_token(*executors.front());
    const auto body_address = RegionFrontierTestAccess::shared_body_address(*executors.front());
    const auto wrapper_entry = executors.front()->step_entry();
    const auto trusted_entry
        = RegionFrontierPrivateAccess::trusted_entry(*executors.front());
    const auto canonical_values_entry
        = RegionFrontierPrivateAccess::canonical_values_entry(
            *executors.front());
    const auto alias_and_canonical_values_entry
        = RegionFrontierPrivateAccess::alias_and_canonical_values_entry(
            *executors.front());
    const auto descriptor_shapes_entry
        = RegionFrontierPrivateAccess::descriptor_shapes_entry(
            *executors.front());
    require(
        body_owner != nullptr
            && body_address != 0U
            && wrapper_entry != nullptr
            && trusted_entry != nullptr
            && canonical_values_entry != nullptr
            && alias_and_canonical_values_entry != nullptr
            && descriptor_shapes_entry != nullptr,
        "the cold plan publishes four existing wrappers plus the shape wrapper");

    std::uint64_t body_misses { };
    std::uint64_t body_hits { };
    std::uint64_t body_stores { };
    std::uint64_t body_reuses { };
    std::uint64_t wrapper_hits { };
    std::uint64_t wrapper_misses { };
    std::uint64_t wrapper_stores { };
    std::uint64_t wrapper_reuses { };
    for (const auto& executor : executors) {
        require(
            RegionFrontierTestAccess::shared_body_owner_token(*executor)
                == body_owner
                && RegionFrontierTestAccess::shared_body_address(*executor)
                    == body_address
                && executor->step_entry() == wrapper_entry
                && RegionFrontierPrivateAccess::trusted_entry(*executor)
                    == trusted_entry
                && RegionFrontierPrivateAccess::canonical_values_entry(
                    *executor) == canonical_values_entry
                && RegionFrontierPrivateAccess::alias_and_canonical_values_entry(
                    *executor) == alias_and_canonical_values_entry
                && RegionFrontierPrivateAccess::descriptor_shapes_entry(
                    *executor) == descriptor_shapes_entry,
            "same-key concurrent plans share all five exact wrapper entries");
        const auto body_stats = RegionFrontierTestAccess::body_cache_statistics(*executor);
        const auto wrapper_stats = RegionFrontierTestAccess::wrapper_cache_statistics(*executor);
        require_clean_layer_statistics(body_stats);
        require_clean_layer_statistics(wrapper_stats);
        body_hits += body_stats.hits;
        body_misses += body_stats.misses;
        body_stores += body_stats.stores;
        wrapper_hits += wrapper_stats.hits;
        wrapper_misses += wrapper_stats.misses;
        wrapper_stores += wrapper_stats.stores;
        body_reuses += RegionFrontierTestAccess::body_registry_reused(*executor) ? 1U : 0U;
        wrapper_reuses += RegionFrontierTestAccess::wrapper_registry_reused(*executor) ? 1U : 0U;
    }
    require(
        body_hits == 0U && body_misses == 1U && body_stores == 1U && body_reuses == static_cast<std::uint64_t>(caller_count - 1),
        "concurrent first use creates one body and shares its registry owner");
    require(wrapper_hits == 0U && wrapper_misses == 1U && wrapper_stores == 1U && wrapper_reuses == static_cast<std::uint64_t>(caller_count - 1),
        "concurrent identical plans create one exact wrapper");
    require(cached_object_paths(cache.path()).size() == 2U,
        "cold shared materialization stores one body and one exact wrapper "
        "object");
    for (const auto& executor : executors) {
        require_probe(kernel, *executor);
    }
    const auto reused_executor = std::find_if(executors.begin(),
        executors.end(), [](const auto& executor) {
            return RegionFrontierTestAccess::wrapper_registry_reused(
                *executor);
        });
    require(reused_executor != executors.end(),
        "the concurrent cohort includes an in-memory wrapper reuse");
    require_private_probe_parity(kernel, **reused_executor);

    auto remapped_kernel = remap_frontier_physical_ids(kernel, 128U, 64U);
    auto remapped_peer = LlvmRegionFrontierExecutor::try_create(
        remapped_kernel, options, design_identity);
    require(
        remapped_peer != nullptr
            && remapped_peer->step_entry() != nullptr
            && RegionFrontierTestAccess::shared_body_owner_token(*remapped_peer)
                == body_owner
            && RegionFrontierTestAccess::shared_body_address(*remapped_peer)
                == body_address
            && remapped_peer->step_entry() != wrapper_entry
            && RegionFrontierPrivateAccess::trusted_entry(*remapped_peer)
                != trusted_entry
            && RegionFrontierPrivateAccess::canonical_values_entry(
                *remapped_peer) != canonical_values_entry
            && RegionFrontierPrivateAccess::alias_and_canonical_values_entry(
                *remapped_peer) != alias_and_canonical_values_entry
            && RegionFrontierPrivateAccess::descriptor_shapes_entry(
                *remapped_peer) != descriptor_shapes_entry,
        "a remapped physical wrapper gets five distinct entries over the shared body");
        require(
            RegionFrontierTestAccess::body_registry_reused(*remapped_peer) && !RegionFrontierTestAccess::wrapper_registry_reused(*remapped_peer),
            "remapped IDs reuse only the certified body, not the exact wrapper");
        const auto remapped_body_stats =
            RegionFrontierTestAccess::body_cache_statistics(*remapped_peer);
        require_clean_layer_statistics(remapped_body_stats);
        require(remapped_body_stats.hits == 0U &&
                    remapped_body_stats.misses == 0U &&
                    remapped_body_stats.stores == 0U &&
                    remapped_body_stats.rejected_entries == 0U,
            "an in-memory shared-body reuse reports no disk-cache operations");
        const auto remapped_wrapper_stats = RegionFrontierTestAccess::wrapper_cache_statistics(*remapped_peer);
        require_clean_layer_statistics(remapped_wrapper_stats);
        require(remapped_wrapper_stats.hits == 0U && remapped_wrapper_stats.misses == 1U && remapped_wrapper_stats.stores == 1U,
            "a new physical binding materializes its own wrapper object");
        const auto remapped_cache_stats = remapped_peer->cache_statistics();
        require(remapped_cache_stats.hits == remapped_wrapper_stats.hits &&
                    remapped_cache_stats.misses == remapped_wrapper_stats.misses &&
                    remapped_cache_stats.stores == remapped_wrapper_stats.stores &&
                    remapped_cache_stats.rejected_entries ==
                        remapped_wrapper_stats.rejected_entries,
            "aggregate cache statistics contain only the new exact wrapper");
    require_probe(remapped_kernel, *remapped_peer);
    require_private_probe_parity(remapped_kernel, *remapped_peer);
    require(cached_object_paths(cache.path()).size() == 3U,
        "two exact physical bindings store separate wrapper objects for one "
        "body");

    // Release every owner of the first exact wrapper while retaining a peer
    // with the same shared-body owner. Reacquiring the exact key concurrently
    // with final wrapper destruction stresses deterministic ORC symbol
    // retirement.
    for (std::size_t index = 1U; index < executors.size(); ++index) {
        executors[index].reset();
    }
    require_probe(kernel, *executors.front());
    executors.front().reset();
    require_probe(remapped_kernel, *remapped_peer);

    auto current = LlvmRegionFrontierExecutor::try_create(kernel, options, design_identity);
    require(
        current != nullptr && RegionFrontierTestAccess::shared_body_owner_token(*current) == body_owner && RegionFrontierTestAccess::body_registry_reused(*current) && !RegionFrontierTestAccess::wrapper_registry_reused(*current),
        "the expired exact wrapper is rebuilt under its still-live body owner");
    auto replacement_wrapper_stats = RegionFrontierTestAccess::wrapper_cache_statistics(*current);
    require(replacement_wrapper_stats.hits == 1U && replacement_wrapper_stats.misses == 0U && replacement_wrapper_stats.stores == 0U,
        "exact wrapper recreation can reload its persisted object");
    require_probe(kernel, *current);
    require_private_probe_parity(kernel, *current);

    for (std::size_t iteration = 0U; iteration < 8U; ++iteration) {
        std::barrier replacement_gate { 2 };
        auto retiring = std::move(current);
        std::thread destroyer {
            [retiring = std::move(retiring), &replacement_gate]() mutable {
                replacement_gate.arrive_and_wait();
                retiring.reset();
            }
        };
        replacement_gate.arrive_and_wait();
        std::exception_ptr create_error;
        try {
            current = LlvmRegionFrontierExecutor::try_create(kernel, options,
                design_identity);
        } catch (...) {
            create_error = std::current_exception();
        }
        destroyer.join();
        if (create_error) {
            std::rethrow_exception(create_error);
        }
        require(current != nullptr && current->step_entry() != nullptr && RegionFrontierTestAccess::shared_body_owner_token(*current) == body_owner && RegionFrontierTestAccess::shared_body_address(*current) == body_address,
            "concurrent wrapper release and exact reacquisition keeps the body "
            "callable");
        require_probe(kernel, *current);
        require_probe(remapped_kernel, *remapped_peer);
    }

    current.reset();
    remapped_peer.reset();

    // No executor can retain either the shared owner or either exact wrapper.
    // Recreate from disk to prove cache hits do not depend on old ORC owners.
    auto warm = LlvmRegionFrontierExecutor::try_create(kernel, options, design_identity);
    require(warm != nullptr && warm->step_entry() != nullptr && !RegionFrontierTestAccess::body_registry_reused(*warm) && !RegionFrontierTestAccess::wrapper_registry_reused(*warm),
        "warm disk reconstruction creates fresh body and wrapper owners");
    const auto warm_body_stats = RegionFrontierTestAccess::body_cache_statistics(*warm);
    const auto warm_wrapper_stats = RegionFrontierTestAccess::wrapper_cache_statistics(*warm);
    require(warm_body_stats.hits == 1U && warm_body_stats.misses == 0U && warm_body_stats.stores == 0U && warm_wrapper_stats.hits == 1U && warm_wrapper_stats.misses == 0U && warm_wrapper_stats.stores == 0U,
        "released owners are reconstructed from separate body and wrapper "
        "objects");
    require_probe(kernel, *warm);
    require_private_probe_parity(kernel, *warm);
    require(cached_object_paths(cache.path()).size() == 3U,
        "warm reconstruction reuses both existing physical object layers");
}

} // namespace

void run_region_frontier_shared_body_cache_tests()
{
    check_concurrent_registry_and_wrapper_lifetime();
}

} // namespace fsim::compiler::test
