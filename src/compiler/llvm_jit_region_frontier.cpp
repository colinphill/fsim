// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit_region_frontier.hpp"

#include "llvm_jit_compilation_contexts.hpp"
#include "llvm_jit_impl.hpp"
#include "llvm_jit_llvm_args.hpp"
#include "llvm_jit_native_body_registry_internal.hpp"
#include "llvm_jit_region_frontier_private_access.hpp"
#include "llvm_jit_region_frontier_test_access.hpp"
#include "native_cache_schema.hpp"
#include "llvm/region_frontier_codegen_v2.hpp"
#include "llvm/region_frontier_kernel_plan.hpp"
#include "fsim/compiler/object_cache.hpp"

#include <llvm/ADT/SmallString.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::compiler {
namespace {

constexpr std::string_view frontier_symbol = "fsim_region_frontier_step_v2";
using FrontierProfileClock = std::chrono::steady_clock;

void emit_frontier_profile_line(const std::string_view line) noexcept
{
    // One stdio call keeps this diagnostic record intact across compiler threads.
    (void)std::fwrite(line.data(), sizeof(char), line.size(), stderr);
    (void)std::fflush(stderr);
}

void report_frontier_refusal(
    const runtime::simir::RegionConeActivationKernel& kernel,
    const LlvmJitOptions& options, const std::string_view reason,
    const std::uint32_t planner_rejection_site_line,
    const RegionFrontierRejectedSensitivity* const rejected_sensitivity
        = nullptr) noexcept
{
    if (std::getenv("FSIM_PROFILE_SV_WAVES") == nullptr) {
        return;
    }

    const RegionFrontierRejectedSensitivity no_sensitivity_rejection { };
    const auto& sensitivity = rejected_sensitivity != nullptr
        ? *rejected_sensitivity
        : no_sensitivity_rejection;
    const auto first_process = kernel.members.empty()
        ? 0U
        : static_cast<unsigned>(kernel.members.front().process);
    const auto last_process = kernel.members.empty()
        ? 0U
        : static_cast<unsigned>(kernel.members.back().process);
    char line[768] { };
    const auto written = std::snprintf(line, sizeof(line),
        "[fsim region-native-create] layer=llvm-frontier "
        "variant=frontier event=refusal reason=%.*s "
        "planner=region_frontier_kernel_plan.cpp "
        "planner-rejection-site-line=%u program=%u domain-id=%u "
        "members=%zu first-process=%u last-process=%u "
        "inputs=%zu outputs=%zu operations=%zu registers=%zu "
        "sensitivity-present=%u member-process=%u member-index=%zu "
        "sensitivity-index=%zu signal=%u edge=%u offset=%u width=%u "
        "input-present=%u input-width=%u input-kind=%u input-internal=%u "
        "debug=%u coverage=%s\n",
        static_cast<int>(reason.size()), reason.data(),
        static_cast<unsigned>(planner_rejection_site_line),
        static_cast<unsigned>(kernel.program.id),
        static_cast<unsigned>(kernel.program.scheduling_domain),
        kernel.members.size(), first_process, last_process,
        kernel.inputs.size(), kernel.outputs.size(),
        kernel.program.operations.size(), kernel.program.register_count,
        sensitivity.present ? 1U : 0U,
        static_cast<unsigned>(sensitivity.member_process),
        sensitivity.member_index, sensitivity.sensitivity_index,
        static_cast<unsigned>(sensitivity.signal),
        static_cast<unsigned>(sensitivity.edge), sensitivity.offset,
        sensitivity.width,
        sensitivity.matching_input_present ? 1U : 0U,
        sensitivity.matching_input_width,
        static_cast<unsigned>(sensitivity.matching_input_kind),
        sensitivity.matching_input_internal ? 1U : 0U,
        options.debug_instrumentation ? 1U : 0U,
        options.code_coverage_identity == "disabled" ? "disabled" : "enabled");
    if (written <= 0) {
        return;
    }
    const auto line_size = static_cast<std::size_t>(written)
            < sizeof(line)
        ? static_cast<std::size_t>(written)
        : sizeof(line) - 1U;
    emit_frontier_profile_line(std::string_view { line, line_size });
}

struct FrontierIrDumpResult {
    std::string path;
    std::string error;
};

[[nodiscard]] FrontierIrDumpResult dump_raw_frontier_ir(
    const llvm::Module& module,
    const std::string_view directory,
    const std::size_t member_count,
    const JitOptimizationLevel optimization)
{
    std::string filename = "fsim-frontier-members-";
    filename += std::to_string(member_count);
    filename += "-";
    filename += to_string(optimization);
    filename += "-%%%%%%%%.ll";

    llvm::SmallString<256> model { directory };
    llvm::sys::path::append(model, filename);
    int file_descriptor = -1;
    llvm::SmallString<256> result_path;
    if (const auto error = llvm::sys::fs::createUniqueFile(
            model.str(), file_descriptor, result_path)) {
        return { {}, error.message() };
    }

    llvm::raw_fd_ostream output { file_descriptor, true };
    module.print(output, nullptr);
    output.flush();
    output.close();
    const auto path = std::string { result_path.begin(), result_path.end() };
    if (output.has_error()) {
        const auto error = output.error();
        const auto message = error.message();
        output.clear_error();
        return { path, message };
    }
    return { path, {} };
}

[[nodiscard]] auto frontier_profile_nanoseconds(
    const FrontierProfileClock::time_point time) noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        time.time_since_epoch()).count();
}

[[nodiscard]] double frontier_profile_milliseconds(
    const FrontierProfileClock::duration duration) noexcept
{
    return std::chrono::duration<double, std::milli> { duration }.count();
}

void dump_and_report_raw_frontier_ir_if_enabled(
    const llvm::Module& module,
    const std::string_view cache_identity,
    const std::size_t member_count,
    const JitOptimizationLevel optimization,
    const bool profile_frontier,
    const std::string_view variant)
{
    if (!profile_frontier) {
        return;
    }
    const auto* const dump_directory
        = std::getenv("FSIM_DUMP_LLVM_FRONTIER_DIR");
    if (dump_directory == nullptr || dump_directory[0] == '\0') {
        return;
    }

    const auto raw_dump_begin = FrontierProfileClock::now();
    const auto raw_dump_result = dump_raw_frontier_ir(
        module, dump_directory, member_count, optimization);
    const auto raw_dump_end = FrontierProfileClock::now();
    std::string profile_line;
    llvm::raw_string_ostream profile(profile_line);
    profile << "fsim-profile: llvm-frontier-raw-ir";
    if (!variant.empty()) {
        profile << " variant=";
        profile.write(variant.data(), variant.size());
    }
    profile << " cache_identity='";
    profile.write_escaped(cache_identity);
    profile << "'"
            << " members=" << member_count
            << " optimization='" << to_string(optimization) << "'"
            << " raw_ir_dump_ms="
            << frontier_profile_milliseconds(raw_dump_end - raw_dump_begin);
    if (!raw_dump_result.path.empty()) {
        profile << " path='";
        profile.write_escaped(raw_dump_result.path);
        profile << "'";
    }
    if (!raw_dump_result.error.empty()) {
        profile << " error='";
        profile.write_escaped(raw_dump_result.error);
        profile << "'";
    }
    profile << '\n';
    profile.flush();
    emit_frontier_profile_line(profile_line);
}

[[nodiscard]] std::string frontier_cache_key(
    const RegionFrontierKernelPlan& plan,
    const LlvmJitOptions& options,
    const std::string_view immutable_design_identity,
    const llvm_detail::LlvmBackendTier backend_tier)
{
    CacheKeyBuilder key;
    key.add("kind", "fsim-native-region-frontier-v2");
    key.add("frontier-abi", std::to_string(
        runtime::simir::kRegionFrontierAbiVersionV2));
    key.add("frontier-value-plane-contract", std::to_string(
        runtime::simir::kRegionFrontierValuePlaneContractV2));
    key.add("frontier-backend", "llvm-region-frontier-v2");
    key.add("frontier-range-validation", "compact-runtime-pairs-v1");
    key.add("frontier-sensitivity-admission",
        "whole-any+read-only-boundary-ranges+exact-full-width-internal-v2");
    key.add("native-object-schema", llvm_detail::kNativeObjectCacheSchema);
    key.add("frontier-plan", plan.cache_identity());
    key.add("immutable-design", immutable_design_identity);
    key.add("host", LlvmJit::native_host_identity(
        options.optimization).fingerprint);
    key.add("llvm-arguments", llvm_detail::initialize_llvm_arguments());
    key.add("optimization", to_string(options.optimization));
    key.add("code-coverage", options.code_coverage_identity);
    key.add("debug", options.debug_instrumentation ? "on" : "off");
    key.add("direct-update", options.require_direct_update_slots
        ? "required" : "optional");
    key.add("direct-read", options.require_direct_read_signals
        ? "required" : "optional");
    key.add("backend-tier", backend_tier
            == llvm_detail::LlvmBackendTier::less ? "less" : "none");
    return key.finish();
}

[[nodiscard]] std::string shared_body_semantic_key(
    const std::string_view shared_identity, const LlvmJitOptions& options,
    const std::string_view immutable_design_identity)
{
    CacheKeyBuilder key;
    key.add("kind", "fsim-native-region-frontier-shared-body-v3");
    key.add("shared-body-identity", shared_identity);
    key.add("frontier-abi", std::to_string(
        runtime::simir::kRegionFrontierAbiVersionV2));
    key.add("frontier-value-plane-contract", std::to_string(
        runtime::simir::kRegionFrontierValuePlaneContractV2));
    key.add("native-object-schema", llvm_detail::kNativeObjectCacheSchema);
    key.add("shared-body-codegen",
        "literal-physical-binding-v1;alias-prevalidated-geometry-v1");
    key.add("llvm-version", LlvmJit::llvm_version());
    key.add("host", LlvmJit::native_host_identity(
        options.optimization).fingerprint);
    key.add("llvm-arguments", llvm_detail::initialize_llvm_arguments());
    key.add("optimization", to_string(options.optimization));
    key.add("code-coverage", options.code_coverage_identity);
    key.add("debug", options.debug_instrumentation ? "on" : "off");
    key.add("direct-update", options.require_direct_update_slots
        ? "required" : "optional");
    key.add("direct-read", options.require_direct_read_signals
        ? "required" : "optional");
    key.add("immutable-design", immutable_design_identity);
    key.add("backend-tier-policy", llvm_detail::kBackendTierPolicy);
    return key.finish();
}

[[nodiscard]] std::string shared_body_owner_key(
    const std::string_view semantic_key, const LlvmJitOptions& options)
{
    CacheKeyBuilder key;
    key.add("kind", "fsim-native-region-frontier-body-owner-v1");
    key.add("body-semantics", semantic_key);
    const auto cache_directory = options.cache_directory.empty()
        ? std::string { }
        : std::filesystem::absolute(options.cache_directory)
              .lexically_normal().generic_string();
    key.add("cache-directory", cache_directory);
    key.add("cache-maximum-bytes", options.cache_maximum_bytes
            ? std::to_string(*options.cache_maximum_bytes) : "unlimited");
    key.add("cache-maximum-entries", options.cache_maximum_entries
            ? std::to_string(*options.cache_maximum_entries) : "unlimited");
    key.add("cache-maximum-age-seconds", options.cache_maximum_age
            ? std::to_string(options.cache_maximum_age->count())
            : "unlimited");
    return key.finish();
}

[[nodiscard]] std::string shared_body_object_key(
    const std::string_view semantic_key,
    const llvm_detail::LlvmBackendTier backend_tier)
{
    CacheKeyBuilder key;
    key.add("kind", "fsim-native-frontier-shared-body-object-v3");
    key.add("body-semantics", semantic_key);
    key.add("native-object-schema", llvm_detail::kNativeObjectCacheSchema);
    key.add("backend-tier", backend_tier
            == llvm_detail::LlvmBackendTier::less ? "less" : "none");
    return key.finish();
}

[[nodiscard]] std::string exact_wrapper_semantic_key(
    const RegionFrontierKernelPlan& plan,
    const std::string_view body_semantic_key)
{
    CacheKeyBuilder key;
    key.add("kind", "fsim-native-region-frontier-exact-wrapper-v3");
    key.add("frontier-plan", plan.cache_identity());
    key.add("body-semantics", body_semantic_key);
    key.add("wrapper-codegen", "checked-trusted-alias-thunks-v1");
    return key.finish();
}

[[nodiscard]] std::string exact_wrapper_object_key(
    const std::string_view wrapper_semantic_key,
    const std::string_view body_object_key,
    const llvm_detail::LlvmBackendTier backend_tier)
{
    CacheKeyBuilder key;
    key.add("kind", "fsim-native-frontier-exact-wrapper-object-v3");
    key.add("exact-wrapper", wrapper_semantic_key);
    key.add("shared-body-object", body_object_key);
    key.add("native-object-schema", llvm_detail::kNativeObjectCacheSchema);
    key.add("backend-tier", backend_tier
            == llvm_detail::LlvmBackendTier::less ? "less" : "none");
    return key.finish();
}

[[nodiscard]] std::string body_symbol_for(
    const std::string_view body_semantic_key)
{
    return "fsim_region_frontier_shared_body_v3_"
        + std::string { body_semantic_key };
}

[[nodiscard]] std::string wrapper_symbol_for(
    const std::string_view wrapper_semantic_key)
{
    return "fsim_region_frontier_exact_wrapper_v3_"
        + std::string { wrapper_semantic_key };
}

[[nodiscard]] std::string trusted_wrapper_symbol_for(
    const std::string_view wrapper_semantic_key)
{
    return wrapper_symbol_for(wrapper_semantic_key)
        + "_trusted_alias_prevalidated";
}

[[nodiscard]] LlvmJitCacheStatistics cache_statistics_delta(
    const LlvmJitCacheStatistics& after,
    const LlvmJitCacheStatistics& before) noexcept
{
    return {
        after.hits - before.hits,
        after.misses - before.misses,
        after.stores - before.stores,
        after.rejected_entries - before.rejected_entries,
        after.load_failures - before.load_failures,
        after.store_failures - before.store_failures,
        after.pruned_entries - before.pruned_entries,
        after.pruned_bytes - before.pruned_bytes,
        after.prune_failures - before.prune_failures,
    };
}

void add_cache_statistics(LlvmJitCacheStatistics& target,
    const LlvmJitCacheStatistics& value) noexcept
{
    target.hits += value.hits;
    target.misses += value.misses;
    target.stores += value.stores;
    target.rejected_entries += value.rejected_entries;
    target.load_failures += value.load_failures;
    target.store_failures += value.store_failures;
    target.pruned_entries += value.pruned_entries;
    target.pruned_bytes += value.pruned_bytes;
    target.prune_failures += value.prune_failures;
}

struct FrontierSharedBodyOwner;

struct FrontierExactWrapperOwner final {
    ~FrontierExactWrapperOwner();

    std::shared_ptr<FrontierSharedBodyOwner> body_owner;
    std::string registry_key;
    llvm::orc::ResourceTrackerSP resource_tracker;
    runtime::simir::RegionFrontierStepEntryV2 entry { };
    runtime::simir::RegionFrontierStepEntryV2 trusted_entry { };
    std::uint64_t wrapper_address { };
    LlvmJitCacheStatistics cache_statistics { };
};

struct FrontierSharedBodyOwner final {
    explicit FrontierSharedBodyOwner(LlvmJitOptions options)
        : jit(std::move(options))
    {
    }

    LlvmJit jit;
    std::mutex api_mutex;
    llvm_detail::WeakSingleFlightRegistry<FrontierExactWrapperOwner>
        exact_wrappers;
    // This strong tracker handle bridges the weak-owner-expired/destructor
    // window: a replacement cannot register the deterministic symbol until
    // the previous ORC resources have been removed under api_mutex.
    std::unordered_map<std::string, llvm::orc::ResourceTrackerSP>
        exact_wrapper_trackers;
    std::uint64_t body_address { };
    std::string body_semantic_key;
    std::string body_object_key;
    std::string body_symbol;
    llvm_detail::LlvmBackendTier backend_tier
        = llvm_detail::LlvmBackendTier::none;
    LlvmJitCacheStatistics cache_statistics { };
    bool wrapper_resource_removal_failed { };
};

FrontierExactWrapperOwner::~FrontierExactWrapperOwner()
{
    if (resource_tracker == nullptr || body_owner == nullptr) {
        return;
    }
    const std::lock_guard lock { body_owner->api_mutex };
    const auto found = body_owner->exact_wrapper_trackers.find(registry_key);
    auto safe_to_forget_tracker = resource_tracker->isDefunct();
    if (!safe_to_forget_tracker) {
        auto error = resource_tracker->remove();
        if (error) {
            body_owner->wrapper_resource_removal_failed = true;
            llvm::consumeError(std::move(error));
            safe_to_forget_tracker = false;
        } else {
            safe_to_forget_tracker = true;
        }
    }
    if (safe_to_forget_tracker
        && found != body_owner->exact_wrapper_trackers.end()
        && found->second == resource_tracker) {
        body_owner->exact_wrapper_trackers.erase(found);
    }
}

using FrontierSharedBodyRegistry
    = llvm_detail::WeakSingleFlightRegistry<FrontierSharedBodyOwner>;

[[nodiscard]] FrontierSharedBodyRegistry& frontier_shared_body_registry()
{
    static FrontierSharedBodyRegistry registry;
    return registry;
}

void set_backend_tier_flags(llvm::Module& module,
    const llvm_detail::LlvmBackendTier backend_tier,
    const std::uint64_t instruction_count)
{
    auto& context = module.getContext();
    const auto add_flag = [&](const char* const name,
                              const std::uint64_t value) {
        module.addModuleFlag(llvm::Module::Error, name,
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(context), value)));
    };
    add_flag("fsim.backend-codegen-tier",
        static_cast<std::uint64_t>(backend_tier));
    add_flag("fsim.backend-tier-eligible", 1U);
    add_flag("fsim.backend-ir-instruction-count", instruction_count);
    add_flag("fsim.backend-tier-selection-ir-instruction-count",
        instruction_count);
}

void report_shared_frontier_materialization(
    const std::uint32_t member_count,
    const bool body_registry_reused,
    const bool wrapper_registry_reused,
    const LlvmJitCacheStatistics& body_statistics,
    const LlvmJitCacheStatistics& wrapper_statistics) noexcept
{
    if (std::getenv("FSIM_PROFILE_SV_WAVES") == nullptr) {
        return;
    }
    const auto cache_result = [](const bool registry_reused,
                                  const LlvmJitCacheStatistics& statistics) {
        if (registry_reused) {
            return "owner-reused";
        }
        if (statistics.hits != 0U) {
            return "disk-hit";
        }
        return "compiled";
    };
    char line[640] { };
    const auto written = std::snprintf(line, sizeof(line),
        "[fsim region-native-create] layer=llvm-frontier "
        "variant=shared-body event=materialized members=%u "
        "body_registry_reused=%u wrapper_registry_reused=%u "
        "body_status=%s wrapper_status=%s "
        "body_cache_hits=%llu body_cache_misses=%llu "
        "wrapper_cache_hits=%llu wrapper_cache_misses=%llu\n",
        static_cast<unsigned>(member_count),
        body_registry_reused ? 1U : 0U,
        wrapper_registry_reused ? 1U : 0U,
        cache_result(body_registry_reused, body_statistics),
        cache_result(wrapper_registry_reused, wrapper_statistics),
        static_cast<unsigned long long>(body_statistics.hits),
        static_cast<unsigned long long>(body_statistics.misses),
        static_cast<unsigned long long>(wrapper_statistics.hits),
        static_cast<unsigned long long>(wrapper_statistics.misses));
    if (written > 0) {
        const auto line_size = static_cast<std::size_t>(written)
                < sizeof(line)
            ? static_cast<std::size_t>(written)
            : sizeof(line) - 1U;
        emit_frontier_profile_line(std::string_view { line, line_size });
    }
}

} // namespace

struct LlvmPreparedRegionFrontier::Impl {
    Impl(RegionFrontierKernelPlan source_plan,
        LlvmJitOptions effective_options, std::string design_identity)
        : plan(std::move(source_plan))
        , options(std::move(effective_options))
        , immutable_design_identity(std::move(design_identity))
    {
    }

    RegionFrontierKernelPlan plan;
    LlvmJitOptions options;
    std::string immutable_design_identity;
};

struct LlvmRegionFrontierExecutor::Impl {
    explicit Impl(RegionFrontierKernelPlan source_plan)
        : plan(std::move(source_plan))
    {
    }

    // Exact fallback JITs, wrapper resources, and shared owners are destroyed
    // before the exact descriptor storage returned by layout().
    RegionFrontierKernelPlan plan;
    std::unique_ptr<LlvmJit> private_jit;
    std::shared_ptr<FrontierSharedBodyOwner> body_owner;
    std::shared_ptr<FrontierExactWrapperOwner> exact_wrapper;
    runtime::simir::RegionFrontierStepEntryV2 entry { };
    runtime::simir::RegionFrontierStepEntryV2 trusted_entry { };
    LlvmJitCacheStatistics body_cache_statistics { };
    LlvmJitCacheStatistics wrapper_cache_statistics { };
    LlvmJitCacheStatistics materialization_cache_statistics { };
    bool body_registry_reused { };
    bool wrapper_registry_reused { };
};

LlvmPreparedRegionFrontier::LlvmPreparedRegionFrontier(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

LlvmPreparedRegionFrontier::~LlvmPreparedRegionFrontier() = default;
LlvmPreparedRegionFrontier::LlvmPreparedRegionFrontier(
    LlvmPreparedRegionFrontier&&) noexcept = default;
LlvmPreparedRegionFrontier& LlvmPreparedRegionFrontier::operator=(
    LlvmPreparedRegionFrontier&&) noexcept = default;

const runtime::simir::RegionFrontierLayoutV2&
LlvmPreparedRegionFrontier::layout() const noexcept
{
    static const runtime::simir::RegionFrontierLayoutV2 empty_layout { };
    return impl_ ? impl_->plan.layout() : empty_layout;
}

std::optional<std::string_view>
LlvmPreparedRegionFrontier::structural_census_identity() const noexcept
{
    return impl_ ? impl_->plan.structural_census_identity() : std::nullopt;
}

std::unique_ptr<LlvmPreparedRegionFrontier>
LlvmRegionFrontierExecutor::prepare(
    const runtime::simir::RegionConeActivationKernel& kernel,
    LlvmJitOptions options,
    const std::string_view immutable_design_identity)
{
    if (options.debug_instrumentation) {
        report_frontier_refusal(
            kernel, options, "debug-instrumentation", 0U);
        return nullptr;
    }
    if (options.code_coverage_identity != "disabled") {
        report_frontier_refusal(
            kernel, options, "code-coverage", 0U);
        return nullptr;
    }
    std::uint32_t planner_rejection_site_line { };
    RegionFrontierRejectedSensitivity rejected_sensitivity { };
    auto plan = RegionFrontierKernelPlan::try_create(
        kernel, &planner_rejection_site_line, &rejected_sensitivity);
    if (!plan) {
        report_frontier_refusal(kernel, options, "planner-guard",
            planner_rejection_site_line, &rejected_sensitivity);
        return nullptr;
    }

    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    auto prepared = std::make_unique<LlvmPreparedRegionFrontier::Impl>(
        std::move(*plan), std::move(options),
        std::string { immutable_design_identity });
    return std::unique_ptr<LlvmPreparedRegionFrontier> {
        new LlvmPreparedRegionFrontier(std::move(prepared)) };
}

std::unique_ptr<LlvmRegionFrontierExecutor>
LlvmRegionFrontierExecutor::materialize(
    LlvmPreparedRegionFrontier&& prepared)
{
    auto preparation = std::move(prepared.impl_);
    if (preparation == nullptr) {
        return { };
    }
    const auto effective_options = preparation->options;
    auto immutable_design_identity
        = std::move(preparation->immutable_design_identity);
    auto state = std::make_unique<Impl>(
        std::move(preparation->plan));
    const bool trusted_entry_supported
        = state->plan.layout().execution_mode
            == runtime::simir::RegionFrontierExecutionModeV2::systemverilog_active;
    const auto certified_shared_identity
        = state->plan.shared_body_identity();
    if (certified_shared_identity) {
        const auto body_semantics = shared_body_semantic_key(
            *certified_shared_identity, effective_options,
            immutable_design_identity);
        const auto body_owner_identity = shared_body_owner_key(
            body_semantics, effective_options);
        bool body_created { };
        auto body_owner = frontier_shared_body_registry().get_or_create(
            body_owner_identity, [&]()
                -> std::shared_ptr<FrontierSharedBodyOwner> {
                body_created = true;
                auto owner = std::make_shared<FrontierSharedBodyOwner>(
                    effective_options);
                owner->body_semantic_key = body_semantics;
                owner->body_symbol = body_symbol_for(body_semantics);
                owner->jit.set_immutable_design_identity(body_semantics);

                {
                    const std::lock_guard lock { owner->api_mutex };
                    auto& native = *owner->jit.impl_;
                    auto context_owner
                        = native.compilation_contexts->acquire();
                    auto instruction_count = std::uint64_t { };
                    auto object_key = std::string { };
                    auto thread_safe_module = context_owner.withContextDo(
                        [&](llvm::LLVMContext* const context) {
                            auto module = std::make_unique<llvm::Module>(
                                "fsim-region-frontier-shared-body-v2",
                                *context);
                            module->setDataLayout(
                                native.jit->getDataLayout());
                            module->setTargetTriple(
                                native.jit->getTargetTriple());
                            const auto emit_internal_commit
                                = runtime::simir::scratch::
                                    make_region_frontier_internal_commit_emitter_v2(
                                        state->plan.layout(),
                                        state->plan.fanout_range_spans(),
                                        state->plan.fanout_sensitivity_ranges());
                            if (state->plan.emit_shared_body(*module,
                                    owner->body_symbol,
                                    emit_internal_commit) == nullptr) {
                                throw LlvmJitError(
                                    "certified shared frontier emitted no body");
                            }
                            llvm_detail::apply_jit_module_no_unwind_contract(
                                *module);
                            const auto raw_error
                                = llvm_detail::verify_error(*module);
                            if (!raw_error.empty()) {
                                throw LlvmJitError(
                                    "shared native frontier generated invalid LLVM IR: "
                                    + raw_error);
                            }
                            dump_and_report_raw_frontier_ir_if_enabled(
                                *module, body_semantics,
                                state->plan.layout().member_count,
                                effective_options.optimization,
                                std::getenv("FSIM_PROFILE_LLVM_MODULES")
                                    != nullptr,
                                "shared-body");
                            llvm_detail::optimize_module(*module,
                                effective_options.optimization,
                                body_semantics,
                                state->plan.layout().member_count);
                            llvm_detail::require_jit_module_no_unwind_contract(
                                *module);
                            if (native.verify_optimized_modules) {
                                const auto optimized_error
                                    = llvm_detail::verify_error(*module);
                                if (!optimized_error.empty()) {
                                    throw LlvmJitError(
                                        "optimized shared frontier has invalid LLVM IR: "
                                        + optimized_error);
                                }
                            }
                            instruction_count
                                = llvm_detail::ir_instruction_count(*module);
                            owner->backend_tier
                                = llvm_detail::select_backend_tier(
                                    true, instruction_count);
                            set_backend_tier_flags(*module,
                                owner->backend_tier, instruction_count);
                            object_key = shared_body_object_key(
                                body_semantics, owner->backend_tier);
                            module->setModuleIdentifier(object_key);
                            return llvm::orc::ThreadSafeModule(
                                std::move(module), context_owner);
                        });

                    const auto attach_module = [&](auto& jit_impl,
                        const std::string& cache_key,
                        const llvm_detail::LlvmBackendTier backend_tier,
                        const std::uint64_t optimized_instruction_count,
                        llvm::orc::ThreadSafeModule module,
                        const llvm::orc::ResourceTrackerSP& tracker) {
                        auto* const object_cache
                            = jit_impl.object_cache.get();
                        const auto expected_metadata
                            = LlvmJit::Impl::encode_module_metadata(
                                std::span<const LlvmJit::Impl::ProcessInfo> { },
                                cache_key, backend_tier, true,
                                optimized_instruction_count,
                                optimized_instruction_count, { });
                        bool added_cached_object { };
                        if (object_cache != nullptr) {
                            std::vector<std::byte> cached_metadata;
                            auto cached_object = object_cache->preflight(
                                cache_key, &cached_metadata);
                            if (cached_object != nullptr) {
                                if (cached_metadata != expected_metadata) {
                                    object_cache->discard_preflight(cache_key);
                                    throw LlvmJitError(
                                        "cached shared frontier metadata does not match its certified code");
                                }
                                auto add_error = tracker != nullptr
                                    ? jit_impl.jit->addObjectFile(tracker,
                                        std::move(cached_object))
                                    : jit_impl.jit->addObjectFile(
                                        std::move(cached_object));
                                if (add_error) {
                                    object_cache->discard_preflight(cache_key);
                                    throw LlvmJitError(
                                        "cannot add cached shared frontier object: "
                                        + llvm_detail::llvm_error(
                                            std::move(add_error)));
                                }
                                object_cache->accept_preflight_hit(cache_key);
                                added_cached_object = true;
                            }
                        }
                        if (!added_cached_object) {
                            try {
                                if (object_cache != nullptr) {
                                    object_cache->stage_metadata(cache_key,
                                        expected_metadata);
                                }
                                auto add_error = tracker != nullptr
                                    ? jit_impl.jit->addIRModule(tracker,
                                        std::move(module))
                                    : jit_impl.jit->addIRModule(
                                        std::move(module));
                                if (add_error) {
                                    throw LlvmJitError(
                                        "cannot add shared frontier module: "
                                        + llvm_detail::llvm_error(
                                            std::move(add_error)));
                                }
                            } catch (...) {
                                if (object_cache != nullptr) {
                                    object_cache->discard_staged_metadata(
                                        cache_key);
                                    object_cache->discard_preflight(cache_key);
                                }
                                throw;
                            }
                        }
                        return added_cached_object;
                    };

                    owner->body_object_key = object_key;
                    const auto body_cache_before
                        = native.object_cache != nullptr
                        ? native.object_cache->statistics()
                        : LlvmJitCacheStatistics { };
                    const auto body_added_cached_object = attach_module(native,
                        owner->body_object_key, owner->backend_tier,
                        instruction_count, std::move(thread_safe_module),
                        llvm::orc::ResourceTrackerSP { });
                    llvm::orc::ExecutorAddr body_address;
                    using SharedBodyEntry = std::uint32_t (*) (
                        runtime::simir::RegionFrontierFrameV2*, void*, bool);
                    SharedBodyEntry body_entry { };
                    try {
                        body_address = llvm_detail::unwrap(
                            native.jit->lookup(owner->body_symbol),
                            "cannot materialize shared native frontier body");
                        body_entry = body_address.toPtr<SharedBodyEntry>();
                        if (body_entry == nullptr) {
                            throw LlvmJitError(
                                "LLVM returned a null shared frontier body");
                        }
                    } catch (...) {
                        if (native.object_cache != nullptr
                            && !body_added_cached_object) {
                            native.object_cache->discard_staged_metadata(
                                owner->body_object_key);
                            native.object_cache->discard_preflight(
                                owner->body_object_key);
                        }
                        throw;
                    }
                    owner->body_address = body_address.getValue();
                    const auto body_cache_after
                        = native.object_cache != nullptr
                        ? native.object_cache->statistics()
                        : LlvmJitCacheStatistics { };
                    owner->cache_statistics = cache_statistics_delta(
                        body_cache_after, body_cache_before);
                }
                return owner;
            });

        state->body_owner = body_owner;
        state->body_registry_reused = !body_created;
        if (body_created) {
            state->body_cache_statistics = body_owner->cache_statistics;
        }

        const auto wrapper_semantics = exact_wrapper_semantic_key(
            state->plan, body_semantics);
        const auto wrapper_registry_identity = wrapper_semantics;
        bool wrapper_created { };
        auto exact_wrapper = body_owner->exact_wrappers.get_or_create(
            wrapper_registry_identity, [&]()
                -> std::shared_ptr<FrontierExactWrapperOwner> {
                wrapper_created = true;
                auto wrapper = std::make_shared<FrontierExactWrapperOwner>();
                wrapper->body_owner = body_owner;
                wrapper->registry_key = wrapper_registry_identity;
                {
                    const std::lock_guard lock { body_owner->api_mutex };
                    if (body_owner->wrapper_resource_removal_failed) {
                        throw LlvmJitError(
                            "exact frontier wrappers are disabled after resource removal failed");
                    }
                    const auto previous
                        = body_owner->exact_wrapper_trackers.find(
                            wrapper_registry_identity);
                    if (previous
                        != body_owner->exact_wrapper_trackers.end()) {
                        if (!previous->second->isDefunct()) {
                            auto error = previous->second->remove();
                            if (error) {
                                body_owner->wrapper_resource_removal_failed
                                    = true;
                                throw LlvmJitError(
                                    "cannot retire expired exact frontier wrapper: "
                                    + llvm_detail::llvm_error(
                                        std::move(error)));
                            }
                        }
                        body_owner->exact_wrapper_trackers.erase(previous);
                    }
                    auto& native = *body_owner->jit.impl_;
                    wrapper->resource_tracker
                        = native.jit->getMainJITDylib().createResourceTracker();
                    body_owner->exact_wrapper_trackers.emplace(
                        wrapper_registry_identity,
                        wrapper->resource_tracker);
                    auto context_owner
                        = native.compilation_contexts->acquire();
                    auto instruction_count = std::uint64_t { };
                    auto wrapper_object_key = std::string { };
                    auto thread_safe_module = context_owner.withContextDo(
                        [&](llvm::LLVMContext* const context) {
                            auto module = std::make_unique<llvm::Module>(
                                "fsim-region-frontier-exact-wrapper-v2",
                                *context);
                            module->setDataLayout(
                                native.jit->getDataLayout());
                            module->setTargetTriple(
                                native.jit->getTargetTriple());
                            const auto wrapper_symbol
                                = wrapper_symbol_for(wrapper_semantics);
                            const auto trusted_wrapper_symbol
                                = trusted_wrapper_symbol_for(
                                    wrapper_semantics);
                            if (runtime::simir::scratch::
                                    emit_region_frontier_entry_thunk_v2(
                                        *module, wrapper_symbol,
                                        body_owner->body_symbol,
                                        state->plan.layout()) == nullptr) {
                                throw LlvmJitError(
                                    "exact frontier emitted no entry thunk");
                            }
                            if (trusted_entry_supported) {
                                if (runtime::simir::scratch::
                                        emit_region_frontier_entry_thunk_v2(
                                            *module, trusted_wrapper_symbol,
                                            body_owner->body_symbol,
                                            state->plan.layout(), true)
                                    == nullptr) {
                                    throw LlvmJitError(
                                        "exact frontier emitted no trusted entry thunk");
                                }
                            }
                            llvm_detail::apply_jit_module_no_unwind_contract(
                                *module);
                            const auto raw_error
                                = llvm_detail::verify_error(*module);
                            if (!raw_error.empty()) {
                                throw LlvmJitError(
                                    "exact frontier wrapper generated invalid LLVM IR: "
                                    + raw_error);
                            }
                            llvm_detail::optimize_module(*module,
                                effective_options.optimization,
                                wrapper_semantics,
                                state->plan.layout().member_count);
                            llvm_detail::require_jit_module_no_unwind_contract(
                                *module);
                            if (native.verify_optimized_modules) {
                                const auto optimized_error
                                    = llvm_detail::verify_error(*module);
                                if (!optimized_error.empty()) {
                                    throw LlvmJitError(
                                        "optimized exact frontier wrapper has invalid LLVM IR: "
                                        + optimized_error);
                                }
                            }
                            instruction_count
                                = llvm_detail::ir_instruction_count(*module);
                            const auto wrapper_tier
                                = llvm_detail::select_backend_tier(
                                    true, instruction_count);
                            set_backend_tier_flags(*module, wrapper_tier,
                                instruction_count);
                            const auto body_object = body_owner->body_object_key;
                            wrapper_object_key = exact_wrapper_object_key(
                                wrapper_semantics, body_object, wrapper_tier);
                            module->setModuleIdentifier(wrapper_object_key);
                            return llvm::orc::ThreadSafeModule(
                                std::move(module), context_owner);
                        });

                    const auto wrapper_tier
                        = llvm_detail::select_backend_tier(
                            true, instruction_count);
                    auto* const object_cache = native.object_cache.get();
                    const auto wrapper_cache_before = object_cache != nullptr
                        ? object_cache->statistics()
                        : LlvmJitCacheStatistics { };
                    const auto expected_metadata
                        = LlvmJit::Impl::encode_module_metadata(
                            std::span<const LlvmJit::Impl::ProcessInfo> { },
                            wrapper_object_key, wrapper_tier, true,
                            instruction_count, instruction_count, { });
                    bool added_cached_object { };
                    if (object_cache != nullptr) {
                        std::vector<std::byte> cached_metadata;
                        auto cached_object = object_cache->preflight(
                            wrapper_object_key, &cached_metadata);
                        if (cached_object != nullptr) {
                            if (cached_metadata != expected_metadata) {
                                object_cache->discard_preflight(
                                    wrapper_object_key);
                                throw LlvmJitError(
                                    "cached exact frontier wrapper metadata does not match its binding");
                            }
                            if (auto error = native.jit->addObjectFile(
                                    wrapper->resource_tracker,
                                    std::move(cached_object))) {
                                object_cache->discard_preflight(
                                    wrapper_object_key);
                                throw LlvmJitError(
                                    "cannot add cached exact frontier wrapper: "
                                    + llvm_detail::llvm_error(
                                        std::move(error)));
                            }
                            object_cache->accept_preflight_hit(
                                wrapper_object_key);
                            added_cached_object = true;
                        }
                    }
                    if (!added_cached_object) {
                        try {
                            if (object_cache != nullptr) {
                                object_cache->stage_metadata(
                                    wrapper_object_key, expected_metadata);
                            }
                            if (auto error = native.jit->addIRModule(
                                    wrapper->resource_tracker,
                                    std::move(thread_safe_module))) {
                                throw LlvmJitError(
                                    "cannot add exact frontier wrapper: "
                                    + llvm_detail::llvm_error(
                                        std::move(error)));
                            }
                        } catch (...) {
                            if (object_cache != nullptr) {
                                object_cache->discard_staged_metadata(
                                    wrapper_object_key);
                                object_cache->discard_preflight(
                                    wrapper_object_key);
                            }
                            throw;
                        }
                    }
                    llvm::orc::ExecutorAddr wrapper_address;
                    llvm::orc::ExecutorAddr trusted_wrapper_address;
                    try {
                        wrapper_address = llvm_detail::unwrap(
                            native.jit->lookup(
                                wrapper_symbol_for(wrapper_semantics)),
                            "cannot materialize exact native frontier wrapper");
                        wrapper->entry = wrapper_address.template toPtr<
                            std::remove_pointer_t<
                                runtime::simir::RegionFrontierStepEntryV2>>();
                        if (wrapper->entry == nullptr) {
                            throw LlvmJitError(
                                "LLVM returned a null exact frontier wrapper");
                        }
                        if (trusted_entry_supported) {
                            trusted_wrapper_address = llvm_detail::unwrap(
                                native.jit->lookup(trusted_wrapper_symbol_for(
                                    wrapper_semantics)),
                                "cannot materialize trusted native frontier wrapper");
                            wrapper->trusted_entry
                                = trusted_wrapper_address.template toPtr<
                                    std::remove_pointer_t<
                                        runtime::simir::RegionFrontierStepEntryV2>>();
                            if (wrapper->trusted_entry == nullptr) {
                                throw LlvmJitError(
                                    "LLVM returned a null trusted frontier wrapper");
                            }
                        }
                    } catch (...) {
                        if (object_cache != nullptr
                            && !added_cached_object) {
                            object_cache->discard_staged_metadata(
                                wrapper_object_key);
                            object_cache->discard_preflight(
                                wrapper_object_key);
                        }
                        throw;
                    }
                    wrapper->wrapper_address = wrapper_address.getValue();
                    const auto wrapper_cache_after = object_cache != nullptr
                        ? object_cache->statistics()
                        : LlvmJitCacheStatistics { };
                    wrapper->cache_statistics = cache_statistics_delta(
                        wrapper_cache_after, wrapper_cache_before);
                }
                return wrapper;
            });

        state->exact_wrapper = exact_wrapper;
        state->wrapper_registry_reused = !wrapper_created;
        if (wrapper_created) {
            state->wrapper_cache_statistics
                = exact_wrapper->cache_statistics;
        }
        state->entry = exact_wrapper->entry;
        state->trusted_entry = exact_wrapper->trusted_entry;
        state->materialization_cache_statistics
            = state->body_cache_statistics;
        add_cache_statistics(state->materialization_cache_statistics,
            state->wrapper_cache_statistics);
    } else {
        state->private_jit = std::make_unique<LlvmJit>(effective_options);
        state->private_jit->set_immutable_design_identity(
            immutable_design_identity);
        auto& native = *state->private_jit->impl_;
        auto context_owner = native.compilation_contexts->acquire();
        std::string cache_key;
        std::uint64_t instruction_count { };
        auto thread_safe_module = context_owner.withContextDo(
            [&](llvm::LLVMContext* const context) {
                const bool profile_frontier
                    = std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr;
                auto emit_begin = FrontierProfileClock::time_point { };
                if (profile_frontier) {
                    emit_begin = FrontierProfileClock::now();
                }
                auto module = std::make_unique<llvm::Module>(
                    "fsim-region-frontier-v2", *context);
                module->setDataLayout(native.jit->getDataLayout());
                module->setTargetTriple(native.jit->getTargetTriple());
                const auto emit_internal_commit
                    = runtime::simir::scratch::
                        make_region_frontier_internal_commit_emitter_v2(
                            state->plan.layout(),
                            state->plan.fanout_range_spans(),
                            state->plan.fanout_sensitivity_ranges());
                if (state->plan.emit_step(*module, frontier_symbol,
                        emit_internal_commit) == nullptr) {
                    throw LlvmJitError(
                        "certified native frontier emitted no step entry");
                }
                llvm_detail::apply_jit_module_no_unwind_contract(*module);
                const auto emit_end = profile_frontier
                    ? FrontierProfileClock::now()
                    : FrontierProfileClock::time_point { };
                auto verify_begin = FrontierProfileClock::time_point { };
                if (profile_frontier) {
                    verify_begin = FrontierProfileClock::now();
                }
                const auto raw_error = llvm_detail::verify_error(*module);
                const auto verify_end = profile_frontier
                    ? FrontierProfileClock::now()
                    : FrontierProfileClock::time_point { };
                if (!raw_error.empty()) {
                    throw LlvmJitError(
                        "native frontier generated invalid LLVM IR: "
                        + raw_error);
                }
                dump_and_report_raw_frontier_ir_if_enabled(*module,
                    state->plan.cache_identity(),
                    state->plan.layout().member_count,
                    effective_options.optimization, profile_frontier, { });
                auto shape_begin = FrontierProfileClock::time_point { };
                if (profile_frontier) {
                    shape_begin = FrontierProfileClock::now();
                }
                const auto raw_shape = profile_frontier
                    ? llvm_detail::ir_shape(*module)
                    : llvm_detail::IrShape { };
                const auto shape_end = profile_frontier
                    ? FrontierProfileClock::now()
                    : FrontierProfileClock::time_point { };
                if (profile_frontier) {
                    std::string profile_line;
                    llvm::raw_string_ostream profile(profile_line);
                    const auto& layout = state->plan.layout();
                    profile << "fsim-profile: llvm-frontier-compile"
                            << " cache_identity='"
                            << state->plan.cache_identity() << "'"
                            << " optimization='"
                            << to_string(effective_options.optimization) << "'"
                            << " members=" << layout.member_count
                            << " write_sites=" << layout.write_site_count
                            << " signal_slots=" << layout.signal_slot_count
                            << " raw_functions=" << raw_shape.functions
                            << " raw_blocks=" << raw_shape.blocks
                            << " raw_instructions=" << raw_shape.instructions
                            << " emit_begin_ns="
                            << frontier_profile_nanoseconds(emit_begin)
                            << " emit_ms="
                            << frontier_profile_milliseconds(emit_end - emit_begin)
                            << " verify_begin_ns="
                            << frontier_profile_nanoseconds(verify_begin)
                            << " verify_ms="
                            << frontier_profile_milliseconds(verify_end - verify_begin)
                            << " shape_ms="
                            << frontier_profile_milliseconds(shape_end - shape_begin)
                            << '\n';
                    profile.flush();
                    emit_frontier_profile_line(profile_line);
                }
                const auto optimize_begin = profile_frontier
                    ? FrontierProfileClock::now()
                    : FrontierProfileClock::time_point { };
                llvm_detail::optimize_module(*module,
                    effective_options.optimization, state->plan.cache_identity(),
                    state->plan.layout().member_count);
                if (profile_frontier) {
                    const auto optimize_end = FrontierProfileClock::now();
                    std::string profile_line;
                    llvm::raw_string_ostream profile(profile_line);
                    profile << "fsim-profile: llvm-frontier-optimize-end"
                            << " cache_identity='"
                            << state->plan.cache_identity() << "'"
                            << " optimize_begin_ns="
                            << frontier_profile_nanoseconds(optimize_begin)
                            << " optimize_ms="
                            << frontier_profile_milliseconds(
                                optimize_end - optimize_begin) << '\n';
                    profile.flush();
                    emit_frontier_profile_line(profile_line);
                }
                llvm_detail::require_jit_module_no_unwind_contract(*module);
                if (native.verify_optimized_modules) {
                    const auto optimized_error
                        = llvm_detail::verify_error(*module);
                    if (!optimized_error.empty()) {
                        throw LlvmJitError(
                            "optimized native frontier has invalid LLVM IR: "
                            + optimized_error);
                    }
                }
                instruction_count
                    = llvm_detail::ir_instruction_count(*module);
                const auto tier = llvm_detail::select_backend_tier(
                    true, instruction_count);
                set_backend_tier_flags(*module, tier, instruction_count);
                cache_key = frontier_cache_key(state->plan,
                    effective_options, immutable_design_identity, tier);
                module->setModuleIdentifier(cache_key);
                return llvm::orc::ThreadSafeModule(
                    std::move(module), context_owner);
            });

        auto* const object_cache = native.object_cache.get();
        // Existing object-cache preflight checks this native envelope before it
        // returns a warm object. Frontier-specific plain text would be rejected
        // before the exact metadata comparison below.
        const auto expected_metadata = LlvmJit::Impl::encode_module_metadata(
            std::span<const LlvmJit::Impl::ProcessInfo> { }, cache_key,
            llvm_detail::select_backend_tier(true, instruction_count), true,
            instruction_count, instruction_count, { });
        bool added_cached_object { };
        if (object_cache != nullptr) {
            std::vector<std::byte> cached_metadata;
            auto cached_object = object_cache->preflight(
                cache_key, &cached_metadata);
            if (cached_object != nullptr) {
                if (cached_metadata != expected_metadata) {
                    throw LlvmJitError(
                        "cached native frontier metadata does not match its certified layout");
                }
                if (auto error = native.jit->addObjectFile(
                        std::move(cached_object))) {
                    throw LlvmJitError(
                        "cannot add cached native frontier: "
                        + llvm_detail::llvm_error(std::move(error)));
                }
                object_cache->accept_preflight_hit(cache_key);
                added_cached_object = true;
            }
        }
        if (!added_cached_object) {
            try {
                if (object_cache != nullptr) {
                    object_cache->stage_metadata(cache_key,
                        expected_metadata);
                }
                if (auto error = native.jit->addIRModule(
                        std::move(thread_safe_module))) {
                    throw LlvmJitError(
                        "cannot add native frontier module: "
                        + llvm_detail::llvm_error(std::move(error)));
                }
            } catch (...) {
                if (object_cache != nullptr) {
                    object_cache->discard_staged_metadata(cache_key);
                    object_cache->discard_preflight(cache_key);
                }
                throw;
            }
        }

        try {
            const auto address = llvm_detail::unwrap(
                native.jit->lookup(std::string { frontier_symbol }),
                "cannot materialize native frontier step");
            state->entry = address.template toPtr<
                std::remove_pointer_t<
                    runtime::simir::RegionFrontierStepEntryV2>>();
            if (state->entry == nullptr) {
                throw LlvmJitError(
                    "LLVM returned a null native frontier step entry");
            }
        } catch (...) {
            if (object_cache != nullptr && !added_cached_object) {
                object_cache->discard_staged_metadata(cache_key);
                object_cache->discard_preflight(cache_key);
            }
            throw;
        }
        state->materialization_cache_statistics
            = state->private_jit->cache_statistics();
    }
    if (certified_shared_identity) {
        report_shared_frontier_materialization(
            state->plan.layout().member_count,
            state->body_registry_reused,
            state->wrapper_registry_reused,
            state->body_cache_statistics,
            state->wrapper_cache_statistics);
    }
    return std::unique_ptr<LlvmRegionFrontierExecutor> {
        new LlvmRegionFrontierExecutor(std::move(state)) };
}

std::unique_ptr<LlvmRegionFrontierExecutor>
LlvmRegionFrontierExecutor::try_create(
    const runtime::simir::RegionConeActivationKernel& kernel,
    LlvmJitOptions options,
    const std::string_view immutable_design_identity)
{
    auto prepared = prepare(kernel, std::move(options),
        immutable_design_identity);
    if (prepared == nullptr) {
        return { };
    }
    return materialize(std::move(*prepared));
}

LlvmRegionFrontierExecutor::LlvmRegionFrontierExecutor(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

LlvmRegionFrontierExecutor::~LlvmRegionFrontierExecutor() = default;
LlvmRegionFrontierExecutor::LlvmRegionFrontierExecutor(
    LlvmRegionFrontierExecutor&&) noexcept = default;
LlvmRegionFrontierExecutor& LlvmRegionFrontierExecutor::operator=(
    LlvmRegionFrontierExecutor&&) noexcept = default;

runtime::simir::RegionFrontierStepEntryV2
LlvmRegionFrontierExecutor::step_entry() const noexcept
{
    return impl_ ? impl_->entry : nullptr;
}

const runtime::simir::RegionFrontierLayoutV2&
LlvmRegionFrontierExecutor::layout() const noexcept
{
    return impl_->plan.layout();
}

LlvmJitCacheStatistics
LlvmRegionFrontierExecutor::cache_statistics() const noexcept
{
    return impl_ ? impl_->materialization_cache_statistics
                 : LlvmJitCacheStatistics { };
}

namespace llvm_detail {

runtime::simir::RegionFrontierStepEntryV2
RegionFrontierPrivateAccess::trusted_entry(
    const LlvmRegionFrontierExecutor& executor) noexcept
{
    return executor.impl_ != nullptr
        ? executor.impl_->trusted_entry
        : nullptr;
}

std::uint64_t RegionFrontierTestAccess::shared_body_address(
    const LlvmRegionFrontierExecutor& executor) noexcept
{
    return executor.impl_ != nullptr && executor.impl_->body_owner != nullptr
        ? executor.impl_->body_owner->body_address
        : 0U;
}

const void* RegionFrontierTestAccess::shared_body_owner_token(
    const LlvmRegionFrontierExecutor& executor) noexcept
{
    return executor.impl_ != nullptr
            && executor.impl_->body_owner != nullptr
        ? executor.impl_->body_owner.get()
        : nullptr;
}

bool RegionFrontierTestAccess::body_registry_reused(
    const LlvmRegionFrontierExecutor& executor) noexcept
{
    return executor.impl_ != nullptr
        && executor.impl_->body_registry_reused;
}

bool RegionFrontierTestAccess::wrapper_registry_reused(
    const LlvmRegionFrontierExecutor& executor) noexcept
{
    return executor.impl_ != nullptr
        && executor.impl_->wrapper_registry_reused;
}

LlvmJitCacheStatistics RegionFrontierTestAccess::body_cache_statistics(
    const LlvmRegionFrontierExecutor& executor) noexcept
{
    return executor.impl_ != nullptr
        ? executor.impl_->body_cache_statistics
        : LlvmJitCacheStatistics { };
}

LlvmJitCacheStatistics RegionFrontierTestAccess::wrapper_cache_statistics(
    const LlvmRegionFrontierExecutor& executor) noexcept
{
    return executor.impl_ != nullptr
        ? executor.impl_->wrapper_cache_statistics
        : LlvmJitCacheStatistics { };
}

} // namespace llvm_detail

} // namespace fsim::compiler
