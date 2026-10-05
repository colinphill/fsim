// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_preparation_test.hpp"

#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "llvm_jit_region_frontier_test_access.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fsim::compiler::test {
namespace {

using runtime::simir::RegionConeActivationKernel;

class TemporaryCacheDirectory final {
public:
    TemporaryCacheDirectory()
    {
        static std::atomic_uint64_t next_suffix { };
        const auto tick = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path()
            / ("fsim-frontier-preparation-" + std::to_string(tick) + "-"
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

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] LlvmJitOptions frontier_options()
{
    LlvmJitOptions options;
    options.optimization = JitOptimizationLevel::o0;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = false;
    options.require_direct_read_signals = false;
    return options;
}

void check_preparation_owns_descriptors_and_inputs()
{
    TemporaryCacheDirectory cache;
    auto options = frontier_options();
    options.cache_directory = cache.path() / "original-plan-cache";
    options.cache_maximum_bytes.reset();
    options.cache_maximum_entries.reset();
    options.cache_maximum_age.reset();
    const auto matching_options = options;
    std::string design_identity { "frontier-preparation-original-design" };
    const auto matching_design_identity = design_identity;
    std::unique_ptr<LlvmPreparedRegionFrontier> prepared;
    std::optional<std::string> expected_census_identity;
    std::uint32_t expected_member_process { };
    std::uint32_t expected_first_signal { };
    std::uint32_t expected_first_write_site { };
    std::uint32_t expected_member_count { };
    std::uint32_t expected_signal_count { };

    {
        auto kernel = make_certified_frontier_kernel(8U);
        prepared = LlvmRegionFrontierExecutor::prepare(
            kernel, options, design_identity);
        require(prepared != nullptr,
            "a certified frontier can prepare before JIT materialization");

        const auto& layout = prepared->layout();
        require(layout.abi_version
                    == runtime::simir::kRegionFrontierAbiVersionV2
                && layout.member_count != 0U
                && layout.members != nullptr
                && layout.signal_slot_count != 0U
                && layout.signals != nullptr
                && layout.write_site_count != 0U
                && layout.write_sites != nullptr,
            "prepared layout exposes its complete owned descriptors");
        expected_member_count = layout.member_count;
        expected_signal_count = layout.signal_slot_count;
        expected_member_process = layout.members[0U].process_id;
        expected_first_signal = layout.signals[0U].signal_id;
        expected_first_write_site = layout.write_sites[0U].source_instruction;

        if (const auto identity = prepared->structural_census_identity()) {
            require(!identity->empty(),
                "an available structural census identity is nonempty");
            expected_census_identity.emplace(*identity);
        }

        kernel = RegionConeActivationKernel { };
        design_identity.assign("mutated-after-preparation");
        options.cache_directory = cache.path() / "mutated-plan-cache";
        options.optimization = JitOptimizationLevel::o2;
        options.debug_instrumentation = true;
        options.code_coverage_identity = "mutated-after-preparation";
    }

    const auto& retained_layout = prepared->layout();
    require(retained_layout.member_count == expected_member_count
            && retained_layout.signal_slot_count == expected_signal_count
            && retained_layout.members != nullptr
            && retained_layout.members[0U].process_id == expected_member_process
            && retained_layout.signals != nullptr
            && retained_layout.signals[0U].signal_id == expected_first_signal
            && retained_layout.write_sites != nullptr
            && retained_layout.write_sites[0U].source_instruction
                == expected_first_write_site,
        "prepared descriptors outlive the source kernel");
    const auto retained_identity = prepared->structural_census_identity();
    require(retained_identity.has_value()
            == expected_census_identity.has_value()
            && (!retained_identity
                || *retained_identity == *expected_census_identity),
        "prepared census identity is owned and stable after source destruction");

    LlvmPreparedRegionFrontier moved { std::move(*prepared) };
    require(prepared->layout().member_count == 0U
            && !prepared->structural_census_identity(),
        "moving a prepared plan leaves an inert source object");
    auto repeated_source_materialization
        = LlvmRegionFrontierExecutor::materialize(std::move(*prepared));
    require(repeated_source_materialization == nullptr,
        "a moved-from prepared plan cannot materialize an entry");

    auto executor = LlvmRegionFrontierExecutor::materialize(std::move(moved));
    require(executor != nullptr && executor->step_entry() != nullptr,
        "materialization consumes the owned plan into a real executable backend");
    const auto cold_statistics = executor->cache_statistics();
    require(cold_statistics.hits == 0U && cold_statistics.misses == 2U
            && cold_statistics.stores == 2U,
        "materialization stores the shared body and exact wrapper using prepared options");
    const auto cold_body_statistics
        = llvm_detail::RegionFrontierTestAccess::body_cache_statistics(*executor);
    const auto cold_wrapper_statistics
        = llvm_detail::RegionFrontierTestAccess::wrapper_cache_statistics(*executor);
    require(cold_body_statistics.hits == 0U
            && cold_body_statistics.misses == 1U
            && cold_body_statistics.stores == 1U
            && cold_wrapper_statistics.hits == 0U
            && cold_wrapper_statistics.misses == 1U
            && cold_wrapper_statistics.stores == 1U,
        "cold cache deltas are attributed to their independent materialization layers");
    require(executor->layout().member_count == expected_member_count
            && executor->layout().signal_slot_count == expected_signal_count
            && executor->layout().members[0U].process_id == expected_member_process
            && executor->layout().signals[0U].signal_id == expected_first_signal,
        "materialization transfers the prepared descriptors to the executor");
    require(moved.layout().member_count == 0U
            && !moved.structural_census_identity(),
        "successful materialization empties the consumed plan");
    auto repeated_consumed_materialization
        = LlvmRegionFrontierExecutor::materialize(std::move(moved));
    require(repeated_consumed_materialization == nullptr,
        "a consumed prepared plan cannot materialize twice");

    const auto matching_kernel = make_certified_frontier_kernel(8U);
    auto adapter_executor = LlvmRegionFrontierExecutor::try_create(
        matching_kernel, matching_options, matching_design_identity);
    require(adapter_executor != nullptr
            && adapter_executor->step_entry() != nullptr,
        "the compatibility adapter materializes the matching native object");
    const auto warm_statistics = adapter_executor->cache_statistics();
    require(warm_statistics.hits == 0U && warm_statistics.misses == 0U
            && warm_statistics.stores == 0U
            && llvm_detail::RegionFrontierTestAccess::body_registry_reused(
                *adapter_executor)
            && llvm_detail::RegionFrontierTestAccess::wrapper_registry_reused(
                *adapter_executor)
            && adapter_executor->step_entry() == executor->step_entry(),
        "the compatibility path reuses the live exact body and wrapper owners");
}

void check_prepare_refusals()
{
    const auto kernel = make_certified_frontier_kernel(8U);
    auto options = frontier_options();
    options.debug_instrumentation = true;
    require(LlvmRegionFrontierExecutor::prepare(
                kernel, options, "frontier-preparation-debug-refusal")
            == nullptr,
        "prepared frontier declines debug-instrumented options before emission");

    options.debug_instrumentation = false;
    options.code_coverage_identity = "frontier-preparation-coverage";
    require(LlvmRegionFrontierExecutor::prepare(
                kernel, options, "frontier-preparation-coverage-refusal")
            == nullptr,
        "prepared frontier declines coverage options before emission");
}

void check_try_create_adapter_remains_available()
{
    const auto kernel = make_certified_frontier_kernel(8U);
    auto options = frontier_options();
    const auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, options, "frontier-preparation-try-create-adapter");
    require(executor != nullptr && executor->step_entry() != nullptr,
        "the compatibility try_create path still returns a real entry");
}

} // namespace

void run_region_frontier_preparation_tests()
{
    check_preparation_owns_descriptors_and_inputs();
    check_prepare_refusals();
    check_try_create_adapter_remains_available();
}

} // namespace fsim::compiler::test
