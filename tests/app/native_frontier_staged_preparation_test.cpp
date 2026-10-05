// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"

#include "../../src/app/application_region_kernel_backend.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../runtime/runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::app::native_frontier_staged_preparation {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_)) {
            previous_ = previous;
        }
#if defined(_WIN32)
        if (::_putenv_s(name_, value) != 0) {
#else
        if (::setenv(name_, value, 1) != 0) {
#endif
            throw std::runtime_error { "failed to set native test policy" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(
            name_, previous_ ? previous_->c_str() : ""));
#else
        if (previous_) {
            static_cast<void>(::setenv(name_, previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_));
        }
#endif
    }

private:
    const char* name_ { };
    std::optional<std::string> previous_;
};

enum class PlanMode : std::uint8_t {
    valid,
    reject_preflight,
    reject_materialized_layout,
};

enum class StageEventKind : std::uint8_t {
    prepare,
    materialize,
    legacy_create,
};

struct StageEvent final {
    StageEventKind kind { };
    ProcessId first_member { };
};

struct ProviderState final {
    std::shared_ptr<RegionKernelBackendProvider> delegate;
    RegionFrontierBackendProvider* frontier { };
    RegionFrontierPreparingProvider* preparing { };
    std::string identity;
    PlanMode mode { PlanMode::valid };
    std::vector<StageEvent> events;
    std::size_t prepare_calls { };
    std::size_t materialize_calls { };
    std::size_t legacy_create_calls { };
    std::size_t live_plans { };
    std::size_t destroyed_plans { };
    std::size_t materialize_with_live_plan { };
};

[[nodiscard]] ProcessId first_member(
    const RegionConeActivationKernel& kernel)
{
    if (kernel.members.empty()) {
        return std::numeric_limits<ProcessId>::max();
    }
    return std::ranges::min(kernel.members,
        { }, &RegionConeKernelMember::process).process;
}

class AlteredLayoutBackend final : public RegionFrontierBackend {
public:
    explicit AlteredLayoutBackend(
        std::unique_ptr<RegionFrontierBackend> delegate)
        : delegate_ { std::move(delegate) }
        , layout_ { delegate_->layout() }
    {
        layout_.abi_version = 0U;
    }

    [[nodiscard]] RegionFrontierStepEntryV2 step_entry()
        const noexcept override
    {
        return delegate_->step_entry();
    }

    [[nodiscard]] const RegionFrontierLayoutV2& layout()
        const noexcept override
    {
        return layout_;
    }

private:
    std::unique_ptr<RegionFrontierBackend> delegate_;
    RegionFrontierLayoutV2 layout_;
};

class PreparedBackend final : public RegionFrontierPreparedBackend {
public:
    PreparedBackend(std::shared_ptr<ProviderState> state,
        std::unique_ptr<RegionFrontierPreparedBackend> delegate,
        const PlanMode mode, const ProcessId process)
        : state_ { std::move(state) }
        , delegate_ { std::move(delegate) }
        , layout_ { delegate_->layout() }
        , mode_ { mode }
        , process_ { process }
    {
        ++state_->live_plans;
        if (mode_ == PlanMode::reject_preflight) {
            layout_.abi_version = 0U;
        }
    }

    ~PreparedBackend() override
    {
        --state_->live_plans;
        ++state_->destroyed_plans;
    }

    [[nodiscard]] const RegionFrontierLayoutV2& layout()
        const noexcept override
    {
        return layout_;
    }

    [[nodiscard]] std::optional<std::string_view>
    structural_census_identity() const noexcept override
    {
        return delegate_->structural_census_identity();
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend>
    materialize() && override
    {
        ++state_->materialize_calls;
        state_->events.push_back({ StageEventKind::materialize, process_ });
        if (state_->live_plans != 0U) {
            ++state_->materialize_with_live_plan;
        }
        auto backend = std::move(*delegate_).materialize();
        if (!backend
            || mode_ != PlanMode::reject_materialized_layout) {
            return backend;
        }
        return std::make_unique<AlteredLayoutBackend>(std::move(backend));
    }

private:
    std::shared_ptr<ProviderState> state_;
    std::unique_ptr<RegionFrontierPreparedBackend> delegate_;
    RegionFrontierLayoutV2 layout_;
    PlanMode mode_ { };
    ProcessId process_ { };
};

class PreparingProvider final
    : public RegionKernelBackendProvider
    , public RegionFrontierBackendProvider
    , public RegionFrontierPreparingProvider {
public:
    explicit PreparingProvider(std::shared_ptr<ProviderState> state)
        : state_ { std::move(state) }
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return state_->identity;
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel&) override
    {
        return { };
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend> create_frontier(
        const RegionConeActivationKernel& kernel) override
    {
        ++state_->legacy_create_calls;
        state_->events.push_back({ StageEventKind::legacy_create,
            first_member(kernel) });
        return state_->frontier->create_frontier(kernel);
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierPreparedBackend>
    prepare_frontier(const RegionConeActivationKernel& kernel) override
    {
        ++state_->prepare_calls;
        const auto process = first_member(kernel);
        state_->events.push_back({ StageEventKind::prepare, process });
        auto prepared = state_->preparing->prepare_frontier(kernel);
        if (!prepared) {
            return { };
        }
        return std::make_unique<PreparedBackend>(state_,
            std::move(prepared), state_->mode, process);
    }

private:
    std::shared_ptr<ProviderState> state_;
};

class LegacyProvider final
    : public RegionKernelBackendProvider
    , public RegionFrontierBackendProvider {
public:
    explicit LegacyProvider(std::shared_ptr<ProviderState> state)
        : state_ { std::move(state) }
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return state_->identity;
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel&) override
    {
        return { };
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend> create_frontier(
        const RegionConeActivationKernel& kernel) override
    {
        ++state_->legacy_create_calls;
        state_->events.push_back({ StageEventKind::legacy_create,
            first_member(kernel) });
        return state_->frontier->create_frontier(kernel);
    }

private:
    std::shared_ptr<ProviderState> state_;
};

[[nodiscard]] std::shared_ptr<ProviderState> make_provider_state(
    const std::string_view design_identity)
{
    auto state = std::make_shared<ProviderState>();
    fsim::compiler::LlvmJitOptions options;
    options.optimization = fsim::compiler::JitOptimizationLevel::o0;
    options.debug_instrumentation = false;
    state->delegate
        = fsim::app::application_detail::make_llvm_region_kernel_backend_provider(
            std::move(options), design_identity);
    require(state->delegate != nullptr,
        "the staged preparation test requires the LLVM provider");
    state->frontier = dynamic_cast<RegionFrontierBackendProvider*>(
        state->delegate.get());
    state->preparing = dynamic_cast<RegionFrontierPreparingProvider*>(
        state->delegate.get());
    require(state->frontier != nullptr && state->preparing != nullptr,
        "the production provider must expose both frontier preparation interfaces");
    state->identity.assign(state->delegate->identity());
    state->identity.append("|runtime-staging-test");
    return state;
}

void add_component(Interpreter& interpreter, const ProcessId first_process,
    const std::string_view suffix)
{
    const auto input = interpreter.add_signal({
        std::string { "staging.input." } + std::string { suffix },
        PackedLogic4 { 8U, Logic4::zero }, ResolutionKind::sv_wire,
        ValueKind::logic4 });
    const auto internal = interpreter.add_signal({
        std::string { "staging.internal." } + std::string { suffix },
        PackedLogic4 { 8U, Logic4::zero }, ResolutionKind::sv_wire,
        ValueKind::logic4 });
    const auto output = interpreter.add_signal({
        std::string { "staging.output." } + std::string { suffix },
        PackedLogic4 { 8U, Logic4::zero }, ResolutionKind::sv_wire,
        ValueKind::logic4 });

    Process producer;
    producer.id = first_process;
    producer.name = std::string { "staging.producer." }
        + std::string { suffix };
    producer.scheduling_domain = ProcessSchedulingDomain::generic;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { input, EdgeKind::any } };
    producer.driver_regions = { { internal, 0U, 8U, true } };
    producer.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { internal, 0U, SignalUpdateDomain::generic },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(producer)) == first_process,
        "staging fixture producer process IDs remain dense");

    Process consumer;
    consumer.id = first_process + 1U;
    consumer.name = std::string { "staging.consumer." }
        + std::string { suffix };
    consumer.scheduling_domain = ProcessSchedulingDomain::generic;
    consumer.register_count = 1U;
    consumer.register_value_kinds = { ValueKind::logic4 };
    consumer.static_sensitivity = { { internal, EdgeKind::any } };
    consumer.driver_regions = { { output, 0U, 8U, true } };
    consumer.operations = {
        ReadSignal { 0U, internal },
        WriteUpdate { output, 0U, SignalUpdateDomain::generic },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(consumer)) == first_process + 1U,
        "staging fixture consumer process IDs remain dense");
}

[[nodiscard]] std::size_t frontier_runtime_count(
    OwnedDriverDemotionTestAccess::Implementation& implementation)
{
    return static_cast<std::size_t>(std::ranges::count_if(
        implementation.region_frontier_runtime_by_component,
        [](const auto& runtime) { return runtime != nullptr; }));
}

void build_graph(Interpreter& interpreter)
{
    OwnedDriverDemotionTestAccess::implementation(interpreter)
        .build_region_graph();
}

void test_prepare_all_before_materialize()
{
    Interpreter interpreter;
    add_component(interpreter, 0U, "first");
    add_component(interpreter, 2U, "second");
    auto state = make_provider_state("staged-order-test");
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<PreparingProvider>(state));

    build_graph(interpreter);

    require(state->prepare_calls == 2U && state->materialize_calls == 2U,
        "both certified component plans must prepare and materialize");
    require(state->legacy_create_calls == 0U,
        "the preparing provider must not use the legacy factory path");
    require(state->events.size() == 4U
            && state->events[0U].kind == StageEventKind::prepare
            && state->events[1U].kind == StageEventKind::prepare
            && state->events[2U].kind == StageEventKind::materialize
            && state->events[3U].kind == StageEventKind::materialize,
        "every component must finish preparation before materialization starts");
    require(state->events[0U].first_member == state->events[2U].first_member
            && state->events[1U].first_member == state->events[3U].first_member,
        "component order must be preserved across both staging passes");
    require(state->destroyed_plans == 2U && state->live_plans == 0U
            && state->materialize_with_live_plan == 2U,
        "each prepared object must remain alive through its materialization");
    require(frontier_runtime_count(
                OwnedDriverDemotionTestAccess::implementation(interpreter))
                == 2U,
        "both authenticated components must retain a V2 runtime");
}

void test_preflight_decline_skips_materialization()
{
    Interpreter interpreter;
    add_component(interpreter, 0U, "preflight");
    auto state = make_provider_state("staged-preflight-test");
    state->mode = PlanMode::reject_preflight;
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<PreparingProvider>(state));

    build_graph(interpreter);

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    require(state->prepare_calls == 1U && state->materialize_calls == 0U,
        "a rejected pure preflight must skip backend materialization");
    require(state->destroyed_plans == 1U && state->live_plans == 0U,
        "a preflight-declined plan must be released without escaping the pass");
    require(frontier_runtime_count(implementation) == 0U
            && implementation.region_frontier_backend_pool.empty(),
        "preflight decline must publish neither runtime nor backend pool entry");
}

void test_final_auth_decline_retry_and_pool_reuse()
{
    Interpreter interpreter;
    add_component(interpreter, 0U, "retry");
    auto state = make_provider_state("staged-retry-test");
    state->mode = PlanMode::reject_materialized_layout;
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<PreparingProvider>(state));
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);

    build_graph(interpreter);
    require(state->prepare_calls == 1U && state->materialize_calls == 1U
            && frontier_runtime_count(implementation) == 0U
            && implementation.region_frontier_backend_pool.empty(),
        "final layout authentication must reject and not pool a changed backend");

    state->mode = PlanMode::valid;
    build_graph(interpreter);
    require(state->prepare_calls == 2U && state->materialize_calls == 2U
            && frontier_runtime_count(implementation) == 1U
            && implementation.region_frontier_backend_pool.size() == 1U,
        "an equivalent later snapshot must reprepare and authenticate after decline");

    build_graph(interpreter);
    require(state->prepare_calls == 2U && state->materialize_calls == 2U
            && frontier_runtime_count(implementation) == 1U
            && implementation.region_frontier_backend_pool.size() == 1U,
        "a later exact pool hit must bind without preparing or materializing again");
}

void test_legacy_provider_path()
{
    Interpreter interpreter;
    add_component(interpreter, 0U, "legacy");
    auto state = make_provider_state("staged-legacy-test");
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<LegacyProvider>(state));

    build_graph(interpreter);

    require(state->legacy_create_calls == 1U
            && state->prepare_calls == 0U
            && state->materialize_calls == 0U,
        "providers without the optional staging interface keep create_frontier");
    require(frontier_runtime_count(
                OwnedDriverDemotionTestAccess::implementation(interpreter))
                == 1U,
        "the legacy provider backend must pass the unchanged final binding path");
}

} // namespace
} // namespace fsim::tests::app::native_frontier_staged_preparation

int main()
{
    try {
        [[maybe_unused]] fsim::tests::app::native_frontier_staged_preparation::
            ScopedEnvironment enable_regions {
                "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        fsim::tests::app::native_frontier_staged_preparation::
            test_prepare_all_before_materialize();
        fsim::tests::app::native_frontier_staged_preparation::
            test_preflight_decline_skips_materialization();
        fsim::tests::app::native_frontier_staged_preparation::
            test_final_auth_decline_retry_and_pool_reuse();
        fsim::tests::app::native_frontier_staged_preparation::
            test_legacy_provider_path();
    } catch (const std::exception& error) {
        std::cerr << "runtime staged-preparation test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
