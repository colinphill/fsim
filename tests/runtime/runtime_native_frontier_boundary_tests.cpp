// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_region_kernel_backend.hpp"
#include "../../src/app/application_internal.hpp"
#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../../src/runtime/simir_region_frontier_trusted_entry.hpp"
#include "runtime_fused_staging_failure_support.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using RuntimeImplementation = OwnedDriverDemotionTestAccess::Implementation;
using FrontierRuntime = RuntimeImplementation::RegionFrontierComponentRuntime;
using FrontierAliasRange = RuntimeImplementation::RegionFrontierAliasRange;
using FrontierAliasCertificateContext
    = RuntimeImplementation::RegionFrontierAliasCertificateContext;

constexpr SignalId source_signal_id = 0U;
constexpr SignalId internal_signal_id = 1U;
constexpr SignalId boundary_signal_id = 2U;
constexpr SignalId aliased_internal_signal_id = 3U;
constexpr SignalId boundary_sibling_signal_id = 4U;
constexpr SignalId boundary_proxy_signal_id = 5U;
constexpr ProcessId producer_process_id = 0U;
constexpr ProcessId consumer_process_id = 1U;
constexpr std::uint32_t wide_test_width = 129U;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] PackedLogic4 make_partial_boundary_source(
    const std::uint32_t signal_width,
    const std::uint32_t offset,
    const std::uint32_t slice_width)
{
    require(signal_width != 0U && slice_width != 0U
            && offset < signal_width
            && slice_width <= signal_width - offset,
        "the partial-boundary test source range fits the signal");
    constexpr std::string_view pattern { "XZ01" };
    std::string slice_bits;
    slice_bits.reserve(slice_width);
    for (std::uint32_t index = 0U; index < slice_width; ++index) {
        const auto pattern_index
            = static_cast<std::size_t>(index) % pattern.size();
        slice_bits.push_back(pattern[pattern_index]);
    }
    PackedLogic4 source { signal_width, Logic4::zero };
    source.insert_bits(PackedLogic4::from_msb_string(slice_bits), offset);
    return source;
}

[[nodiscard]] RegionFrontierKeyV2 make_frontier_key(
    const RegionFrontierSlotV2& slot,
    const SchedulerOrderKey& key) noexcept
{
    return RegionFrontierKeyV2 {
        slot.time,
        slot.delta,
        slot.systemverilog_round,
        key.order,
        key.sequence,
        slot.process_domain,
        slot.phase,
    };
}

class TemporaryCacheDirectory final {
public:
    TemporaryCacheDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path()
            / ("fsim-native-frontier-boundary-"
                + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryCacheDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
        set(value);
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        set(previous_ ? previous_->c_str() : nullptr);
    }

private:
    void set(const char* const value) noexcept
    {
#if defined(_WIN32)
        (void)::_putenv_s(name_.c_str(), value == nullptr ? "" : value);
#else
        if (value == nullptr) {
            (void)::unsetenv(name_.c_str());
        } else {
            (void)::setenv(name_.c_str(), value, 1);
        }
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

enum class MalformedFrontierLayoutKind : std::uint8_t {
    bad_header,
    duplicate_member_process,
    swapped_member_processes,
};

class MalformedFrontierLayoutBackend final : public RegionFrontierBackend {
public:
    explicit MalformedFrontierLayoutBackend(
        std::unique_ptr<RegionFrontierBackend> backend,
        const MalformedFrontierLayoutKind kind)
        : backend_ { std::move(backend) }
        , layout_ { backend_->layout() }
    {
        if (kind == MalformedFrontierLayoutKind::bad_header) {
            layout_.abi_version = 0U;
            layout_.max_member_staged_event_counts
                = reinterpret_cast<const std::uint32_t*>(std::uintptr_t { 1U });
            return;
        }

        require(layout_.members != nullptr && layout_.member_count >= 2U,
            "member-row mutations use the authentic two-member frontier layout");
        members_.assign(layout_.members,
            layout_.members + layout_.member_count);
        if (kind == MalformedFrontierLayoutKind::duplicate_member_process) {
            members_[1U].process_id = members_[0U].process_id;
        } else {
            std::swap(members_[0U].process_id, members_[1U].process_id);
        }
        layout_.members = members_.data();
    }

    [[nodiscard]] RegionFrontierStepEntryV2 step_entry()
        const noexcept override
    {
        return backend_->step_entry();
    }

    [[nodiscard]] const RegionFrontierLayoutV2& layout()
        const noexcept override
    {
        return layout_;
    }

private:
    std::unique_ptr<RegionFrontierBackend> backend_;
    RegionFrontierLayoutV2 layout_;
    std::vector<RegionFrontierMemberLayoutV2> members_;
};

class MalformedFrontierLayoutProvider final
    : public RegionKernelBackendProvider
    , public RegionFrontierBackendProvider {
public:
    explicit MalformedFrontierLayoutProvider(
        std::shared_ptr<RegionKernelBackendProvider> provider,
        const MalformedFrontierLayoutKind kind)
        : provider_ { std::move(provider) }
        , kind_ { kind }
        , frontier_provider_ {
            dynamic_cast<RegionFrontierBackendProvider*>(provider_.get()) }
    {
        require(provider_ != nullptr && frontier_provider_ != nullptr,
            "the malformed-layout test wraps the real frontier provider");
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return provider_->identity();
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        return provider_->create(kernel);
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend> create_frontier(
        const RegionConeActivationKernel& kernel) override
    {
        auto backend = frontier_provider_->create_frontier(kernel);
        if (!backend) {
            return nullptr;
        }
        return std::make_unique<MalformedFrontierLayoutBackend>(
            std::move(backend), kind_);
    }

private:
    std::shared_ptr<RegionKernelBackendProvider> provider_;
    MalformedFrontierLayoutKind kind_;
    RegionFrontierBackendProvider* frontier_provider_ { };
};

class FrontierOnlyProvider final
    : public RegionKernelBackendProvider
    , public RegionFrontierBackendProvider {
public:
    explicit FrontierOnlyProvider(
        std::shared_ptr<RegionKernelBackendProvider> provider)
        : provider_ { std::move(provider) }
        , frontier_provider_ {
            dynamic_cast<RegionFrontierBackendProvider*>(provider_.get()) }
    {
        require(provider_ != nullptr && frontier_provider_ != nullptr,
            "the frontier-only test wraps the real LLVM provider");
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return provider_->identity();
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        return provider_->create(kernel);
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend> create_frontier(
        const RegionConeActivationKernel& kernel) override
    {
        return frontier_provider_->create_frontier(kernel);
    }

private:
    // The optional flattened forwarding route is deliberately absent, while
    // the real LLVM V2 frontier provider remains available.
    std::shared_ptr<RegionKernelBackendProvider> provider_;
    RegionFrontierBackendProvider* frontier_provider_ { };
};

class NativeBoundaryFixture final {
public:
    NativeBoundaryFixture(const std::uint32_t width,
        std::shared_ptr<RegionKernelBackendProvider> provider,
        const bool expect_frontier_runtime = true,
        const bool exercise_zero_sequence_receipt = false,
        const bool alias_boundary_output = false,
        const bool enable_native_frontier = true,
        const bool proxy_slice_boundary_output = false,
        const std::uint32_t boundary_slice_offset = 0U,
        const std::uint32_t boundary_slice_width = 0U,
        std::optional<PackedLogic4> source_initial_value = std::nullopt)
        : provider_ { std::move(provider) }
        , proxy_slice_boundary_output_ { proxy_slice_boundary_output }
        , direct_slice_boundary_output_ { boundary_slice_width != 0U }
        , boundary_slice_offset_ { boundary_slice_offset }
        , requested_boundary_slice_width_ { boundary_slice_width }
    {
        require(width != 0U,
            "the native boundary fixture has a nonempty packed width");
        require(!source_initial_value
                || source_initial_value->width() == width,
            "the optional source value has the declared full signal width");
        const auto source_value = source_initial_value
            ? std::move(*source_initial_value)
            : PackedLogic4 { width, Logic4::one };
        source_signal_ = interpreter_.add_signal(Signal {
            "frontier.boundary.source",
            source_value,
            ResolutionKind::sv_wire,
            ValueKind::logic4,
        });
        internal_signal_ = interpreter_.add_signal(Signal {
            "frontier.boundary.internal",
            PackedLogic4 { width, Logic4::one },
            ResolutionKind::sv_wire,
            ValueKind::logic4,
        });
        boundary_signal_ = interpreter_.add_signal(Signal {
            "frontier.boundary.output",
            PackedLogic4 { width, Logic4::zero },
            ResolutionKind::sv_wire,
            ValueKind::logic4,
        });
        aliased_internal_signal_ = interpreter_.add_signal(Signal {
            "frontier.boundary.internal_alias",
            PackedLogic4 { width, Logic4::one },
            // A4 certifies the stored-owner alias only for wide slots. Narrow
            // cases use a normal resolved owner row so every native writable
            // signal has an authoritative owner value to lease.
            width > 64U ? ResolutionKind::none : ResolutionKind::sv_wire,
            ValueKind::logic4,
        });
        if (alias_boundary_output) {
            add_boundary_alias_family(width);
        }
        require(!proxy_slice_boundary_output || alias_boundary_output,
            "a proxy-slice boundary source requires the real alias family");
        require(!(proxy_slice_boundary_output
                    && direct_slice_boundary_output_),
            "the direct and alias proxy slice modes stay distinct");
        require(!direct_slice_boundary_output_
                || (boundary_slice_offset_ < width
                    && requested_boundary_slice_width_
                        <= width - boundary_slice_offset_),
            "the direct boundary slice fits its full signal plane");
        require(source_signal_ == source_signal_id
                && internal_signal_ == internal_signal_id
                && boundary_signal_ == boundary_signal_id
                && aliased_internal_signal_
                    == aliased_internal_signal_id
                && (!alias_boundary_output
                    || (boundary_sibling_signal_
                            == boundary_sibling_signal_id
                        && boundary_proxy_signal_
                            == boundary_proxy_signal_id)),
            "the fixture's process operations use the registered signal IDs");

        add_native_processes(width);
        interpreter_.set_region_kernel_backend_provider(provider_);
        impl_ = &OwnedDriverDemotionTestAccess::implementation(interpreter_);
        impl_->systemverilog_region_kernel_enabled = true;
        impl_->systemverilog_local_wave_enabled = true;
        impl_->a4_wide_single_owner_commit_enabled = true;
        impl_->a4_wide_disjoint_owner_commit_enabled = false;
        if (!enable_native_frontier) {
            impl_->systemverilog_local_wave_enabled = false;
        }

        const SignalChangeOrigin initial_origin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
        };
        impl_->commit_driver(producer_process_id, internal_signal_,
            PackedLogic4 { width, Logic4::zero }, initial_origin, false);
        impl_->commit_driver(producer_process_id, aliased_internal_signal_,
            PackedLogic4 { width, Logic4::zero }, initial_origin, false);

        if (alias_boundary_output) {
            // Real startup promotes complete alias families before the region
            // snapshot is certified. This fixture builds the snapshot directly
            // before Interpreter::start, so perform the same promotion here.
            impl_->promote_all_container_alias_authorities();
        }

        // This fixture treats the final output as a public boundary. Retain
        // the real API reference before certification so the graph records
        // that observation instead of classifying the otherwise-private leaf
        // output as another hidden internal signal.
        if (!alias_boundary_output) {
            boundary_public_reference_
                = &interpreter_.signal_value(boundary_signal_);
        }

        // Match the inventory phase that Interpreter::start runs before it
        // builds a region snapshot. The pre-start fixture calls the builder
        // directly, so seed the exact native signal dependency masks first.
        impl_->build_native_signal_dependency_masks();
        auto snapshot = impl_->build_region_runtime_snapshot(
            false, true, true);
        std::size_t component_count { };
        for (const auto& runtime : snapshot.frontier_runtime_by_component) {
            if (runtime) {
                ++component_count;
            }
        }
        if (!expect_frontier_runtime) {
            require(component_count == 0U,
                "the malformed provider layout declines frontier preparation");
            impl_->publish_region_runtime_snapshot(std::move(snapshot));
            return;
        }
        require(component_count == 1U,
            "the production snapshot prepares one real frontier runtime");
        impl_->publish_region_runtime_snapshot(std::move(snapshot));

        for (std::size_t index = 0U;
             index < impl_->region_frontier_runtime_by_component.size();
             ++index) {
            if (!impl_->region_frontier_runtime_by_component[index]) {
                continue;
            }
            component_ = index;
            runtime_ = impl_->region_frontier_runtime_by_component[index];
            break;
        }
        require(runtime_ != nullptr && runtime_->backend != nullptr
                && runtime_->backend->executor != nullptr
                && runtime_->backend->executor->step_entry() != nullptr,
            "the snapshot retains the production LLVM frontier entry");
        require(component_ == 0U,
            "the connected producer/consumer component is first in graph order");
        require(runtime_->frame_initialized
                && runtime_->frame.member_count == 2U
                && runtime_->frame.pending_write_count == 0U,
            "the production adapter preallocates its actual member and output frame");

        state_ = impl_->region_authoritative_state_by_component.at(component_);
        require(state_ != nullptr && state_->valid()
                && state_->generation() == runtime_->runtime_generation,
            "the frame is paired with its published authoritative A4 state");
        require(impl_->region_authoritative_component_by_signal.at(
                    boundary_signal_)
                == std::numeric_limits<std::size_t>::max(),
            "the scalar output is deliberately outside the internal A4 signal map");
        auto internal_signals = runtime_->backend->kernel.internal_signals;
        std::ranges::sort(internal_signals);
        require(internal_signals == std::vector<SignalId> {
                    internal_signal_, aliased_internal_signal_ },
            "the production certificate contains both internal signals and their owners");
        require(runtime_->backend->kernel.members.size() == 2U
                && runtime_->backend->kernel.members[0U].process
                    == producer_process_id
                && runtime_->backend->kernel.members[1U].process
                    == consumer_process_id,
            "the generated member order retains the original producer and consumer IDs");

        for (const auto& output : runtime_->backend->kernel.outputs) {
            if (output.signal == boundary_signal_) {
                boundary_owner_ = output.owner;
                boundary_source_instruction_ = output.source_instruction;
                boundary_offset_ = output.offset;
                boundary_width_ = output.width;
                boundary_signal_width_ = output.signal_width;
                break;
            }
        }
        const auto expected_boundary_offset = direct_slice_boundary_output_
            ? boundary_slice_offset_ : 0U;
        const auto expected_boundary_width = direct_slice_boundary_output_
            ? requested_boundary_slice_width_ : width;
        require(boundary_owner_ == consumer_process_id
                && boundary_offset_ == expected_boundary_offset
                && boundary_width_ == expected_boundary_width
                && boundary_signal_width_ == width,
            "the boundary site retains slice coordinates and full signal width");

        const auto expected_generation = state_->values().revision();
        require(state_->values().try_acquire_frontier_write_lease(
                    expected_generation, runtime_->writable_signals, lease_)
                && lease_.active(),
            "the real A4 component yields its certified frontier write lease");
        bind_runtime_planes();
        if (exercise_zero_sequence_receipt) {
            check_zero_sequence_readiness_receipt();
        } else {
            initialize_root_activation();
        }
    }

    [[nodiscard]] bool frontier_runtime_prepared() const noexcept
    {
        return runtime_ != nullptr;
    }

    [[nodiscard]] Interpreter& interpreter() noexcept
    {
        return interpreter_;
    }

    [[nodiscard]] RuntimeImplementation& implementation() noexcept
    {
        return *impl_;
    }

    [[nodiscard]] FrontierRuntime& runtime()
        noexcept
    {
        return *runtime_;
    }

    [[nodiscard]] SignalId boundary_signal() const noexcept
    {
        return boundary_signal_;
    }

    [[nodiscard]] bool proxy_slice_boundary_output() const noexcept
    {
        return proxy_slice_boundary_output_;
    }

    [[nodiscard]] ProcessId boundary_owner() const noexcept
    {
        return boundary_owner_;
    }

    [[nodiscard]] bool has_boundary_alias_family() const noexcept
    {
        return boundary_alias_object_.has_value();
    }

    [[nodiscard]] ContainerObjectId boundary_alias_object() const
    {
        require(boundary_alias_object_.has_value(),
            "the requested boundary has a real alias family");
        return *boundary_alias_object_;
    }

    void require_boundary_alias_leaf_mapping() const
    {
        require(has_boundary_alias_family() && impl_->region_graph.has_value(),
            "the native snapshot retains the configured alias family graph");
        const auto families = impl_->region_graph->signal_alias_families();
        const auto family = std::ranges::find(families,
            *boundary_alias_object_, &RegionSignalAliasFamilyDescriptor::object);
        require(family != families.end() && family->complete
                && family->proxy_readable && family->proxy_writable
                && family->proxy == boundary_proxy_signal_
                && family->width == boundary_width_ * 2U
                && family->leaves.size() == 2U
                && family->leaves[0U].signal == boundary_signal_
                && family->leaves[0U].ordinal == 0U
                && family->leaves[0U].offset == boundary_width_
                && family->leaves[0U].width == boundary_width_
                && family->leaves[1U].signal == boundary_sibling_signal_
                && family->leaves[1U].ordinal == 1U
                && family->leaves[1U].offset == 0U
                && family->leaves[1U].width == boundary_width_,
            "the V2 boundary output is the exact first physical alias leaf");
    }

    void require_boundary_proxy_slice_source() const
    {
        require(proxy_slice_boundary_output_
                && boundary_source_instruction_ != UINT32_MAX
                && boundary_width_ != 0U,
            "the fixture requested one proxy-slice boundary source");
        const auto program = impl_->processes.program_view(boundary_owner_);
        const auto operation = program.operations().expanded(
            boundary_source_instruction_);
        const auto* const slice = operation_get_if<WriteUpdateSlice>(
            &operation);
        require(slice != nullptr
                && slice->signal == boundary_proxy_signal_
                && slice->source == 0U
                && slice->offset == boundary_width_
                && slice->domain
                    == SignalUpdateDomain::systemverilog_active,
            "the original source instruction retains the nonzero proxy offset");
        const auto& driver_regions = program.driver_regions();
        require(driver_regions.size() == 1U
                && driver_regions.front().signal == boundary_proxy_signal_
                && driver_regions.front().offset == boundary_width_
                && driver_regions.front().width == boundary_width_
                && !driver_regions.front().whole,
            "the original owner declaration covers the same proxy slice");
        const RegionConeOutputBinding* output { };
        for (const auto& candidate : runtime_->backend->kernel.outputs) {
            if (candidate.owner != boundary_owner_
                || candidate.signal != boundary_signal_
                || candidate.source_instruction
                    != boundary_source_instruction_) {
                continue;
            }
            require(output == nullptr,
                "one kernel output uniquely authenticates the source op");
            output = &candidate;
        }
        require(output != nullptr && output->offset == 0U
                && output->width == boundary_width_
                && output->domain
                    == SignalUpdateDomain::systemverilog_active
                && output->update_kind
                    == RegionUpdateKind::systemverilog_active,
            "the physical leaf binding remains in leaf-local coordinates");
    }

    void require_direct_partial_boundary_source() const
    {
        require(direct_slice_boundary_output_
                && boundary_source_instruction_ != UINT32_MAX
                && boundary_offset_ == boundary_slice_offset_
                && boundary_width_ == requested_boundary_slice_width_
                && boundary_signal_width_ > boundary_width_,
            "the fixture requested a true partial direct boundary output");
        const auto program = impl_->processes.program_view(boundary_owner_);
        const auto operation = program.operations().expanded(
            boundary_source_instruction_);
        const auto* const slice = operation_get_if<WriteUpdateSlice>(
            &operation);
        require(slice != nullptr
                && slice->signal == boundary_signal_
                && slice->source == 2U
                && slice->offset == boundary_offset_
                && slice->domain
                    == SignalUpdateDomain::systemverilog_active,
            "the boundary binding retains its original direct slice instruction");
        const auto& driver_regions = program.driver_regions();
        require(driver_regions.size() == 1U
                && driver_regions.front().signal == boundary_signal_
                && driver_regions.front().offset == boundary_offset_
                && driver_regions.front().width == boundary_width_
                && !driver_regions.front().whole,
            "the original owner region exactly covers the slice range");
        const RegionConeOutputBinding* output { };
        for (const auto& candidate : runtime_->backend->kernel.outputs) {
            if (candidate.owner != boundary_owner_
                || candidate.signal != boundary_signal_
                || candidate.source_instruction
                    != boundary_source_instruction_) {
                continue;
            }
            require(output == nullptr,
                "one admitted boundary binding authenticates the source op");
            output = &candidate;
        }
        require(output != nullptr && output->offset == boundary_offset_
                && output->width == boundary_width_
                && output->signal_width == boundary_signal_width_
                && output->domain
                    == SignalUpdateDomain::systemverilog_active
                && output->update_kind
                    == RegionUpdateKind::systemverilog_active,
            "the native binding separates slice width from full plane width");
    }

    [[nodiscard]] SignalId boundary_sibling_signal() const
    {
        require(has_boundary_alias_family(),
            "the boundary sibling belongs to the configured alias family");
        return boundary_sibling_signal_;
    }

    [[nodiscard]] SignalId boundary_proxy_signal() const
    {
        require(has_boundary_alias_family(),
            "the boundary proxy belongs to the configured alias family");
        return boundary_proxy_signal_;
    }

    [[nodiscard]] std::size_t boundary_pending_slot() const noexcept
    {
        return pending_slot_;
    }

    [[nodiscard]] const SchedulerBatchFrontierEntry& boundary_task() const
    {
        require(runtime_->frame.scheduler_task_cursor
                    < runtime_->frame.scheduler_task_count,
            "the generated boundary publication task remains at the cursor");
        const auto& generated = runtime_->scheduler_tasks.at(
            runtime_->frame.scheduler_task_cursor);
        boundary_task_ = SchedulerBatchFrontierEntry {
            static_cast<StableOrder>(generated.stable_order),
            generated.sequence,
            generated.payload,
        };
        return boundary_task_;
    }

    [[nodiscard]] bool lease_active() const noexcept
    {
        return lease_.active();
    }

    [[nodiscard]] AuthoritativeSignalPlanes::FrontierWriteLease& lease()
        noexcept
    {
        return lease_;
    }

    [[nodiscard]] std::uint64_t native_dispatches() const noexcept
    {
        return runtime_->native_member_dispatches;
    }

    [[nodiscard]] std::uint32_t task_cursor() const noexcept
    {
        return runtime_->frame.scheduler_task_cursor;
    }

    [[nodiscard]] std::uint32_t pending_flags() const
    {
        return runtime_->pending_writes.at(pending_slot_).flags;
    }

    [[nodiscard]] const PackedLogic4& public_boundary_value() const
    {
        if (boundary_public_reference_ != nullptr) {
            return *boundary_public_reference_;
        }
        return interpreter_.signal_value(boundary_signal_);
    }

    [[nodiscard]] PackedLogic4 pending_boundary_value() const
    {
        require(runtime_ != nullptr
                && pending_slot_ < runtime_->pending_writes.size(),
            "the staged boundary value belongs to the authentic pending row");
        const auto& write = runtime_->pending_writes[pending_slot_];
        require(write.value_kind == RegionFrontierValueKindV2::logic4,
            "the alias boundary fixture stages a Logic4 leaf value");
        return PackedLogic4::from_word_planes(write.width,
            { write.value_planes[0U], write.word_count },
            { write.value_planes[1U], write.word_count });
    }

    [[nodiscard]] const PackedLogic4& original_owner_value() const
    {
        const auto* const record = impl_->driver_values.at(boundary_signal_)
            .find(boundary_owner_);
        require(record != nullptr,
            "the checked boundary commit retains its original driver record");
        return record->value;
    }

    [[nodiscard]] RegionFrontierStatusV2 reach_boundary_publication()
    {
        const auto entry = runtime_->backend->executor->step_entry();
        for (std::size_t turn = 0U; turn < 12U; ++turn) {
            if (runtime_->frame.staged_event_count != 0U) {
                issue_staged_events();
            }
            const auto status = entry(&runtime_->frame);
            if (runtime_->frame.committed_signal_count != 0U) {
                runtime_->synchronize_committed_state(lease_);
                require(!runtime_->invalidated,
                    "the real internal-commit log synchronizes through its A4 lease");
            }
            if (status == RegionFrontierStatusV2::boundary_publication) {
                const auto& task = boundary_task();
                const auto kind = static_cast<std::uint32_t>(
                    task.payload >> kRegionFrontierPayloadKindShiftV2);
                pending_slot_ = static_cast<std::size_t>(
                    task.payload & kRegionFrontierPayloadIndexMaskV2);
                require(kind == static_cast<std::uint32_t>(
                            RegionFrontierEventKindV2::boundary_commit)
                        && pending_slot_
                            < runtime_->pending_writes.size(),
                    "the actual generated entry identifies a boundary pending write");
                require(runtime_->native_member_dispatches >= 2U,
                    "both real SystemVerilog member bodies ran in the LLVM entry");
                const auto& write = runtime_->pending_writes[pending_slot_];
                constexpr std::uint32_t required_flags
                    = pending_active | pending_value_ready
                    | pending_key_assigned | pending_boundary_target;
                require((write.flags & required_flags) == required_flags
                        && (write.flags & pending_committed) == 0U
                        && write.member_index < runtime_->members.size()
                        && runtime_->members[write.member_index].process_id
                            == boundary_owner_
                        && write.update_kind == static_cast<std::uint32_t>(
                            RegionUpdateKind::systemverilog_active)
                        && write.signal_slot < runtime_->planes.size()
                        && runtime_->planes[write.signal_slot].signal_id
                            == boundary_signal_
                        && write.width == boundary_width_
                        && write.word_count
                            == (boundary_width_ + 63U) / 64U
                        && runtime_->planes[write.signal_slot].width
                            == boundary_signal_width_
                        && runtime_->planes[write.signal_slot].word_count
                            == (boundary_signal_width_ + 63U) / 64U
                        && write.source_instruction
                            == boundary_source_instruction_,
                    "generated boundary output is private and tied to its original owner");
                return status;
            }
            require(status == RegionFrontierStatusV2::need_scheduler_keys,
                "the real entry reaches boundary publication through staged scheduler work");
            require(runtime_->frame.staged_event_count != 0U,
                "only generated staged events request fresh scheduler keys");
        }
        throw std::runtime_error {
            "the actual generated frontier entry must reach one boundary task"
        };
    }

    [[nodiscard]] std::pair<RegionFrontierStatusV2, bool>
    checked_entry_once_and_confirm_alias_certificate(
        const FrontierAliasCertificateContext& context)
    {
        const auto status = runtime_->backend->executor->step_entry()(
            &runtime_->frame);
        const bool certificate_confirmed
            = runtime_->confirm_alias_certificate(context, status);
        if (runtime_->frame.committed_signal_count != 0U) {
            runtime_->synchronize_committed_state(lease_);
            require(!runtime_->invalidated,
                "the direct checked step synchronizes its real internal commits");
        }
        return { status, certificate_confirmed };
    }

    [[nodiscard]] RegionFrontierStatusV2 acknowledge_boundary()
    {
        const auto before_ack_dispatches = runtime_->native_member_dispatches;
        const auto before_ack_cursor = runtime_->frame.scheduler_task_cursor;
        const auto status = runtime_->backend->executor->step_entry()(
            &runtime_->frame);
        require(runtime_->frame.scheduler_task_cursor == before_ack_cursor + 1U
                && (runtime_->pending_writes[pending_slot_].flags
                    & pending_active) == 0U
                && runtime_->native_member_dispatches == before_ack_dispatches,
            "the generated entry retires its ACK without replaying either member body");
        return status;
    }

private:
    void add_boundary_alias_family(const std::uint32_t width)
    {
        require(width <= std::numeric_limits<std::uint32_t>::max() / 2U,
            "the boundary proxy width fits the signal descriptor");
        boundary_sibling_signal_ = interpreter_.add_signal(Signal {
            "frontier.boundary.sibling",
            PackedLogic4 { width, Logic4::zero },
            ResolutionKind::sv_wire,
            ValueKind::logic4,
        });
        boundary_proxy_signal_ = interpreter_.add_signal(Signal {
            "frontier.boundary.proxy",
            PackedLogic4 { width * 2U, Logic4::zero },
            ResolutionKind::sv_wire,
            ValueKind::logic4,
        });

        ContainerType type;
        type.element_kind = ContainerElementKind::Packed;
        type.element_width = width;
        type.fixed = true;
        type.index_left = 1;
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        const auto initial = ContainerValue {
            type,
            { PackedLogic4 { width, Logic4::zero },
                PackedLogic4 { width, Logic4::zero } },
            { }
        };
        boundary_alias_object_ = interpreter_.add_container_object({
            "frontier.boundary.proxy", initial, std::nullopt });
        interpreter_.add_container_element_signal_alias({
            *boundary_alias_object_, 0U, boundary_signal_, true, true });
        interpreter_.add_container_element_signal_alias({
            *boundary_alias_object_, 1U, boundary_sibling_signal_, true, true });
        interpreter_.add_container_aggregate_signal_alias({
            *boundary_alias_object_, boundary_proxy_signal_, true, true });
    }

    void add_native_processes(const std::uint32_t width)
    {
        Process producer;
        producer.id = producer_process_id;
        producer.name = "frontier_boundary_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.register_count = 2U;
        producer.register_value_kinds.assign(2U, ValueKind::logic4);
        producer.static_sensitivity = {
            { source_signal_, EdgeKind::any },
        };
        producer.driver_regions = {
            { internal_signal_, 0U, 0U, true },
            { aliased_internal_signal_, 0U, 0U, true },
        };
        producer.operations.push_back(ReadSignal { 0U, source_signal_ });
        producer.operations.push_back(CopyRegister { 1U, 0U });
        producer.operations.push_back(WriteUpdate {
            internal_signal_, 1U,
            SignalUpdateDomain::systemverilog_active,
        });
        producer.operations.push_back(WriteUpdate {
            aliased_internal_signal_, 1U,
            SignalUpdateDomain::systemverilog_active,
        });
        producer.operations.push_back(WaitSensitivity { });
        producer.operations.push_back(Jump { 0U });
        require(width != 0U,
            "the producer has a nonzero source and output shape");
        require(interpreter_.add_process(std::move(producer))
                == producer_process_id,
            "the real producer registers before the consumer");

        Process consumer;
        consumer.id = consumer_process_id;
        consumer.name = "frontier_boundary_consumer";
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.register_count = direct_slice_boundary_output_ ? 3U : 2U;
        consumer.register_value_kinds.assign(
            consumer.register_count, ValueKind::logic4);
        consumer.static_sensitivity = {
            { internal_signal_, EdgeKind::any },
            { aliased_internal_signal_, EdgeKind::any },
        };
        if (proxy_slice_boundary_output_) {
            consumer.driver_regions = {
                { boundary_proxy_signal_, width, width, false },
            };
        } else if (direct_slice_boundary_output_) {
            consumer.driver_regions = {
                { boundary_signal_, boundary_slice_offset_,
                    requested_boundary_slice_width_, false },
            };
        } else {
            consumer.driver_regions = {
                { boundary_signal_, 0U, 0U, true },
            };
        }
        consumer.operations.push_back(ReadSignal { 0U, internal_signal_ });
        consumer.operations.push_back(ReadSignal {
            1U, aliased_internal_signal_,
        });
        if (proxy_slice_boundary_output_) {
            consumer.operations.push_back(WriteUpdateSlice {
                boundary_proxy_signal_, 0U, width,
                SignalUpdateDomain::systemverilog_active,
            });
        } else if (direct_slice_boundary_output_) {
            consumer.operations.push_back(Extract {
                2U, 0U, boundary_slice_offset_,
                requested_boundary_slice_width_,
            });
            consumer.operations.push_back(WriteUpdateSlice {
                boundary_signal_, 2U, boundary_slice_offset_,
                SignalUpdateDomain::systemverilog_active,
            });
        } else {
            consumer.operations.push_back(WriteUpdate {
                boundary_signal_, 0U,
                SignalUpdateDomain::systemverilog_active,
            });
        }
        consumer.operations.push_back(WaitSensitivity { });
        consumer.operations.push_back(Jump { 0U });
        require(interpreter_.add_process(std::move(consumer))
                == consumer_process_id,
            "the real consumer registers after its producer");
    }

    [[nodiscard]] RegionFrontierKeyV2 issue_key(
        const StableOrder order)
    {
        const auto key = impl_->scheduler.reserve_order_key(order);
        return make_frontier_key(runtime_->frame.slot, key);
    }

    void bind_runtime_planes()
    {
        require(runtime_->bind_frame_planes_and_metadata(lease_),
            "the production binder binds the prepared frontier frame");
    }

    void initialize_root_activation()
    {
        auto& frame = runtime_->frame;
        auto& scheduler = impl_->scheduler;
        frame.scheduler_frontier_generation = 1U;
        frame.slot.time = scheduler.now();
        frame.slot.delta = scheduler.delta();
        frame.slot.systemverilog_round = scheduler.systemverilog_round();
        frame.slot.process_domain = static_cast<std::uint32_t>(
            ProcessSchedulingDomain::systemverilog);
        frame.slot.phase = static_cast<std::uint32_t>(SchedulerPhase::active);
        frame.scheduler_tasks = runtime_->scheduler_tasks.data();
        frame.scheduler_task_count = 1U;
        frame.scheduler_task_cursor = 0U;
        frame.pending_write_count = 0U;
        frame.committed_signal_count = 0U;
        frame.staged_event_count = 0U;
        frame.current_member = UINT32_MAX;
        frame.current_pending_write = UINT32_MAX;
        std::fill(runtime_->ready_words.begin(),
            runtime_->ready_words.end(), 0U);
        for (auto& member : runtime_->members) {
            member.flags = waiting_on_static;
            member.static_trigger_mask = Process::full_static_trigger_mask;
            member.queued_key = { };
            member.activation_origin = { };
            member.pending_activation_origin = { };
        }

        const auto root_process = runtime_->backend->kernel.members.front().process;
        const auto root = std::ranges::find(runtime_->members, root_process,
            &RegionFrontierMemberV2::process_id);
        require(root != runtime_->members.end(),
            "the certified producer maps to one runtime member slot");
        const auto root_index = static_cast<std::size_t>(
            root - runtime_->members.begin());
        const auto root_key = issue_key(root_process);
        require(root_key.sequence == 0U,
            "the first real scheduler-issued activation uses valid sequence zero");
        runtime_->ready_words.at(root_index / 64U)
            |= UINT64_C(1) << (root_index % 64U);
        root->flags = queued | queued_key_valid;
        root->queued_key = root_key;
        root->activation_origin = root_key;
        root->pending_activation_origin = root_key;
        runtime_->scheduler_tasks[0U] = RegionFrontierSchedulerTaskV2 {
            root_key.stable_order,
            root_key.sequence,
            encode_region_frontier_payload_v1(
                RegionFrontierEventKindV2::member_activation,
                root_index),
        };
        frame.cut.scheduler_frontier_generation
            = frame.scheduler_frontier_generation;
        frame.cut.next_key = root_key;
        frame.cut.next_key.stable_order += 1U;
        frame.cut.next_key.sequence = 0U;
        frame.cut.kind = RegionFrontierCutKindV2::same_slot_key;
    }

    void check_zero_sequence_readiness_receipt()
    {
        const auto root_process
            = runtime_->backend->kernel.members.front().process;
        const auto root = std::ranges::find(runtime_->members, root_process,
            &RegionFrontierMemberV2::process_id);
        require(root != runtime_->members.end()
                && root_process < impl_->region_readiness_queued_by_process.size(),
            "the zero-sequence receipt witness has a certified root member");
        const auto root_index = static_cast<std::size_t>(
            root - runtime_->members.begin());
        SchedulerSystemVerilogKeyReceipt receipt;
        impl_->scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, root_process, *runtime_,
            encode_region_frontier_payload_v1(
                RegionFrontierEventKindV2::member_activation, root_index),
            [](Scheduler&) { }, { }, &receipt);
        require(receipt.valid && receipt.sequence == 0U
                && receipt.systemverilog_round != 0U,
            "the first real queued SV activation receives sequence zero");

        // This separate receipt fixture queues a real scheduler task without
        // running it. The generated-entry fixtures below reserve their own
        // sequence-zero keys and retain their original frame setup.
        runtime_->scheduler_state_seeded = true;
        root->flags = waiting_on_static;
        root->static_trigger_mask = Process::full_static_trigger_mask;
        root->queued_key = { };
        impl_->record_systemverilog_readiness_key(root_process,
            runtime_->component, root_index, runtime_->runtime_generation,
            receipt, root->static_trigger_mask);
        const auto matches_receipt = [&](const RegionFrontierKeyV2& key) {
            return key.time == receipt.time && key.delta == receipt.delta
                && key.systemverilog_round == receipt.systemverilog_round
                && key.stable_order == receipt.stable_order
                && key.sequence == receipt.sequence
                && key.process_domain == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
                && key.phase == static_cast<std::uint32_t>(receipt.phase);
        };
        constexpr auto expected_flags = queued_key_valid | queued;
        const auto& queued_state
            = impl_->region_readiness_queued_by_process[root_process];
        require(!runtime_->invalidated && queued_state.key_valid
                && queued_state.generation == runtime_->runtime_generation
                && queued_state.component == runtime_->component
                && queued_state.member == root_index
                && matches_receipt(queued_state.queued_key)
                && matches_receipt(root->queued_key)
                && (root->flags & expected_flags) == expected_flags,
            "an authenticated sequence-zero receipt reaches the native frame");
    }

    void issue_staged_events()
    {
        auto& frame = runtime_->frame;
        const auto count = static_cast<std::size_t>(frame.staged_event_count);
        require(count != 0U
                && count <= runtime_->staged_events.size()
                && count <= runtime_->scheduler_tasks.size(),
            "generated events fit the runtime's preallocated task storage");
        std::array<std::size_t,
            FrontierRuntime::
                scheduler_task_capacity> order { };
        for (std::size_t index = 0U; index < count; ++index) {
            order[index] = index;
            auto position = index;
            while (position != 0U
                && runtime_->staged_events[order[position]].stable_order
                    < runtime_->staged_events[order[position - 1U]].stable_order) {
                std::swap(order[position], order[position - 1U]);
                --position;
            }
        }

        for (std::size_t index = 0U; index < count; ++index) {
            const auto& event = runtime_->staged_events[order[index]];
            const auto kind = static_cast<RegionFrontierEventKindV2>(event.kind);
            require(kind == RegionFrontierEventKindV2::member_activation
                    || kind == RegionFrontierEventKindV2::internal_commit
                    || kind == RegionFrontierEventKindV2::boundary_commit,
                "generated staged work uses a certified event kind");
            const auto key = issue_key(
                static_cast<StableOrder>(event.stable_order));
            runtime_->scheduler_tasks[index] = RegionFrontierSchedulerTaskV2 {
                key.stable_order,
                key.sequence,
                encode_region_frontier_payload_v1(
                    kind, event.descriptor_index),
            };
            if (kind == RegionFrontierEventKindV2::member_activation) {
                require(event.descriptor_index < runtime_->members.size(),
                    "generated activation addresses one actual member");
                auto& member = runtime_->members[event.descriptor_index];
                member.flags |= queued | queued_key_valid;
                member.queued_key = key;
                member.activation_origin = key;
                member.pending_activation_origin = event.origin;
                runtime_->ready_words.at(event.descriptor_index / 64U)
                    |= UINT64_C(1) << (event.descriptor_index % 64U);
            } else {
                require(event.descriptor_index
                        < runtime_->pending_writes.size(),
                    "generated commit event names one actual output row");
                auto& write = runtime_->pending_writes[event.descriptor_index];
                write.commit_key = key;
                write.flags |= pending_key_assigned;
            }
        }
        frame.scheduler_task_count = static_cast<std::uint32_t>(count);
        frame.scheduler_task_cursor = 0U;
        frame.scheduler_tasks = runtime_->scheduler_tasks.data();
        frame.staged_event_count = 0U;
        ++frame.scheduler_frontier_generation;
        frame.cut.scheduler_frontier_generation
            = frame.scheduler_frontier_generation;
        frame.cut.next_key = { };
        frame.cut.kind = RegionFrontierCutKindV2::closed_prefix;
    }

    std::shared_ptr<RegionKernelBackendProvider> provider_;
    bool proxy_slice_boundary_output_ { };
    bool direct_slice_boundary_output_ { };
    std::uint32_t boundary_slice_offset_ { };
    std::uint32_t requested_boundary_slice_width_ { };
    Interpreter interpreter_;
    RuntimeImplementation* impl_ { };
    SignalId source_signal_ { };
    SignalId internal_signal_ { };
    SignalId boundary_signal_ { };
    const PackedLogic4* boundary_public_reference_ { };
    SignalId boundary_sibling_signal_ {
        std::numeric_limits<SignalId>::max()
    };
    SignalId boundary_proxy_signal_ {
        std::numeric_limits<SignalId>::max()
    };
    std::optional<ContainerObjectId> boundary_alias_object_;
    SignalId aliased_internal_signal_ { };
    ProcessId boundary_owner_ { UINT32_MAX };
    std::uint32_t boundary_source_instruction_ { UINT32_MAX };
    std::uint32_t boundary_offset_ { };
    std::uint32_t boundary_width_ { };
    std::uint32_t boundary_signal_width_ { };
    std::size_t component_ { std::numeric_limits<std::size_t>::max() };
    std::size_t pending_slot_ { std::numeric_limits<std::size_t>::max() };
    std::shared_ptr<FrontierRuntime>
        runtime_;
    std::shared_ptr<RegionAuthoritativeComponentState> state_;
    AuthoritativeSignalPlanes::FrontierWriteLease lease_;
    mutable SchedulerBatchFrontierEntry boundary_task_;
};

void check_malformed_layout_rejected_before_frontier_publication(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    const MalformedFrontierLayoutKind kind,
    const std::string_view message)
{
    auto malformed_provider = std::make_shared<MalformedFrontierLayoutProvider>(
        provider, kind);
    NativeBoundaryFixture fixture { 1U, malformed_provider, false };
    require(!fixture.frontier_runtime_prepared()
            && std::ranges::none_of(
                fixture.implementation().region_frontier_runtime_by_component,
                [](const auto& runtime) { return runtime != nullptr; }),
        message);
}

void check_successful_boundary_commit_acknowledges_once(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { 1U, provider };
    std::size_t original_owner_writes { };
    ProcessId observed_owner = UINT32_MAX;
    std::size_t visible_publications { };
    fixture.interpreter().set_driver_change_hook(
        [&](const ProcessId process, const SignalId signal, SimulationTick) {
            if (signal != fixture.boundary_signal()) {
                return;
            }
            ++original_owner_writes;
            observed_owner = process;
        });
    fixture.interpreter().set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4&, SimulationTick) {
            if (signal == fixture.boundary_signal()) {
                ++visible_publications;
            }
        });

    require(fixture.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the genuine JIT entry stops at its original boundary owner task");
    const auto dispatches_before_commit = fixture.native_dispatches();
    const auto cursor = fixture.task_cursor();
    const auto flags = fixture.pending_flags();
    fixture.runtime().publish_boundary_commit(fixture.boundary_task(),
        static_cast<std::uint32_t>(fixture.boundary_pending_slot()),
        fixture.lease());
    require(!fixture.lease_active()
            && !fixture.runtime().invalidated
            && fixture.task_cursor() == cursor
            && fixture.native_dispatches() == dispatches_before_commit
            && (fixture.pending_flags() & pending_committed) != 0U,
        "successful checked publication releases A4 and records ACK without replay");
    require(fixture.original_owner_value()
                == PackedLogic4 { 1U, Logic4::one }
            && fixture.public_boundary_value()
                == PackedLogic4 { 1U, Logic4::one }
            && original_owner_writes == 1U
            && observed_owner == fixture.boundary_owner()
            && visible_publications == 1U,
        "the original owner and visible signal publish exactly once");
    require((flags & pending_committed) == 0U,
        "the host ACK is a distinct transition after generated staging");

    const auto ack_status = fixture.acknowledge_boundary();
    require(ack_status == RegionFrontierStatusV2::quiescent
            && fixture.runtime().frame.pending_write_count == 0U
            && fixture.runtime().frame.scheduler_task_cursor
                == fixture.runtime().frame.scheduler_task_count,
        "the generated entry retires the acknowledged original scheduler task");
}

struct AliasSignalState final {
    PackedLogic4 current;
    PackedLogic4 last;
    PackedLogic4 stored;
    std::size_t raw_driver_count { };
    std::optional<PackedLogic4> boundary_owner_value;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp stamp;
    std::uint64_t value_revision { };
};

[[nodiscard]] PackedLogic4 alias_stored_value(
    RuntimeImplementation& implementation,
    const SignalId signal,
    const SignalId proxy,
    const ContainerObjectId object)
{
    if (signal != proxy) {
        return implementation.driven_values.at(signal);
    }

    const auto& value
        = implementation.get_container_object(object).initial_value;
    const auto& aliases
        = implementation.container_element_signal_aliases.at(object);
    require(!value.elements.empty() && aliases.size() == value.elements.size(),
        "the aggregate stored snapshot uses the complete physical family");
    PackedLogic4 result {
        value.type.element_width * value.elements.size(), Logic4::zero };
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        require(aliases[ordinal].has_value(),
            "every aggregate position retains its physical signal alias");
        result.insert_bits(implementation.driven_values.at(
                aliases[ordinal]->signal),
            (aliases.size() - ordinal - 1U) * value.type.element_width);
    }
    return result;
}

[[nodiscard]] AliasSignalState capture_alias_signal_state(
    RuntimeImplementation& implementation,
    const SignalId signal,
    const SignalId proxy,
    const ContainerObjectId object,
    const ProcessId raw_owner)
{
    const auto& drivers = implementation.driver_values.at(signal);
    std::optional<PackedLogic4> owner_value;
    if (raw_owner != UINT32_MAX) {
        if (const auto* const record = drivers.find(raw_owner)) {
            owner_value = record->value;
        }
    }
    return AliasSignalState {
        implementation.logical_signal_value(signal),
        implementation.logical_signal_last_value(signal),
        alias_stored_value(implementation, signal, proxy, object),
        drivers.size(),
        std::move(owner_value),
        implementation.signal_events.at(signal),
        implementation.signal_transactions.at(signal),
        implementation.signal_event_scheduling_stamps.at(signal),
        implementation.signal_value_revisions.at(signal),
    };
}

[[nodiscard]] bool same_alias_signal_state(
    const AliasSignalState& left,
    const AliasSignalState& right) noexcept
{
    return left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.raw_driver_count == right.raw_driver_count
        && left.boundary_owner_value == right.boundary_owner_value
        && left.event == right.event
        && left.transaction == right.transaction
        && left.stamp.origin.process_domain
            == right.stamp.origin.process_domain
        && left.stamp.origin.phase == right.stamp.origin.phase
        && left.stamp.systemverilog_round
            == right.stamp.systemverilog_round
        && left.value_revision == right.value_revision;
}

[[nodiscard]] AliasSignalState capture_boundary_signal_state(
    RuntimeImplementation& implementation,
    const SignalId signal,
    const ProcessId owner)
{
    const auto& drivers = implementation.driver_values.at(signal);
    std::optional<PackedLogic4> owner_value;
    if (const auto* const record = drivers.find(owner)) {
        owner_value = record->value;
    }
    return AliasSignalState {
        implementation.logical_signal_value(signal),
        implementation.logical_signal_last_value(signal),
        implementation.driven_values.at(signal),
        drivers.size(),
        std::move(owner_value),
        implementation.signal_events.at(signal),
        implementation.signal_transactions.at(signal),
        implementation.signal_event_scheduling_stamps.at(signal),
        implementation.signal_value_revisions.at(signal),
    };
}

struct AliasFamilyState final {
    std::array<AliasSignalState, 3U> signals;
    std::vector<PackedLogic4> object_elements;
};

[[nodiscard]] AliasFamilyState capture_alias_family_state(
    NativeBoundaryFixture& fixture)
{
    auto& implementation = fixture.implementation();
    const auto object = fixture.boundary_alias_object();
    const auto leaf = fixture.boundary_signal();
    const auto sibling = fixture.boundary_sibling_signal();
    const auto proxy = fixture.boundary_proxy_signal();
    const auto& object_value
        = implementation.get_container_object(object).initial_value;
    return AliasFamilyState {
        { capture_alias_signal_state(implementation, leaf, proxy, object,
                consumer_process_id),
            capture_alias_signal_state(implementation, sibling, proxy,
                object, UINT32_MAX),
            capture_alias_signal_state(implementation, proxy, proxy,
                object, UINT32_MAX) },
        object_value.elements,
    };
}

[[nodiscard]] bool same_alias_family_state(
    const AliasFamilyState& left,
    const AliasFamilyState& right) noexcept
{
    return same_alias_signal_state(left.signals[0U], right.signals[0U])
        && same_alias_signal_state(left.signals[1U], right.signals[1U])
        && same_alias_signal_state(left.signals[2U], right.signals[2U])
        && left.object_elements == right.object_elements;
}

struct AliasObserverTrace final {
    std::vector<std::pair<SignalId, std::string>> callbacks;
    std::vector<std::vector<std::string>> object_reads;
    std::vector<std::string> proxy_reads;
    bool nested_deposit_started { };
};

[[nodiscard]] ContainerValue make_alias_replacement(
    const std::uint32_t width,
    const Logic4 first,
    const Logic4 second)
{
    ContainerType type;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = width;
    type.fixed = true;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    return ContainerValue {
        type,
        { PackedLogic4 { width, first }, PackedLogic4 { width, second } },
        { }
    };
}

void install_reentrant_alias_observer(
    Interpreter& interpreter,
    const ContainerObjectId object,
    const SignalId leaf,
    const SignalId sibling,
    const SignalId proxy,
    const std::uint32_t width,
    AliasObserverTrace& trace)
{
    auto nested_replacement = make_alias_replacement(
        width, Logic4::zero, Logic4::one);
    interpreter.set_signal_change_hook(
        [&interpreter, object, leaf, sibling, proxy, &trace,
            nested_replacement = std::move(nested_replacement)](
                const SignalId signal,
                const PackedLogic4& value,
                const SimulationTick) mutable {
            if (signal != leaf && signal != sibling && signal != proxy) {
                return;
            }
            trace.callbacks.emplace_back(signal, value.to_msb_string());
            if (signal != leaf || trace.nested_deposit_started) {
                return;
            }

            trace.nested_deposit_started = true;
            const auto& visible_object
                = interpreter.container_object_value(object);
            std::vector<std::string> elements;
            elements.reserve(visible_object.elements.size());
            for (const auto& element : visible_object.elements) {
                elements.push_back(element.to_msb_string());
            }
            trace.object_reads.push_back(std::move(elements));
            trace.proxy_reads.push_back(
                interpreter.signal_value(proxy).to_msb_string());
            interpreter.deposit_container_object(object, nested_replacement);
        });
}

void check_direct_partial_boundary_commit_matches_checked_reference(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    constexpr SignalChangeOrigin origin {
        ProcessSchedulingDomain::systemverilog,
        SchedulerPhase::active,
    };
    for (const auto& [signal_width, offset, slice_width] : {
             std::array<std::uint32_t, 3U> { 65U, 63U, 2U },
             std::array<std::uint32_t, 3U> { 129U, 63U, 66U },
         }) {
        const auto source
            = make_partial_boundary_source(signal_width, offset, slice_width);
        const auto expected_slice = source.extract_bits(offset, slice_width);
        NativeBoundaryFixture native {
            signal_width, provider, true, false, false, true, false,
            offset, slice_width, source };
        native.require_direct_partial_boundary_source();
        require(native.reach_boundary_publication()
                    == RegionFrontierStatusV2::boundary_publication,
            "the production V2 entry stages one direct partial boundary row");
        const auto& write = native.runtime().pending_writes.at(
            native.boundary_pending_slot());
        const auto& plane = native.runtime().planes.at(write.signal_slot);
        require(write.width == slice_width
                && write.word_count == (slice_width + 63U) / 64U
                && plane.signal_id == native.boundary_signal()
                && plane.width == signal_width
                && plane.word_count == (signal_width + 63U) / 64U
                && native.pending_boundary_value() == expected_slice,
            "the pending descriptor is slice-sized while its signal plane remains full-width");
        const auto before = capture_boundary_signal_state(
            native.implementation(), native.boundary_signal(),
            native.boundary_owner());
        auto expected_full = PackedLogic4 { signal_width, Logic4::z };
        expected_full.insert_bits(expected_slice, offset);
        const auto dispatches = native.native_dispatches();
        const auto task = native.boundary_task();
        native.runtime().publish_boundary_commit(task,
            static_cast<std::uint32_t>(native.boundary_pending_slot()),
            native.lease());
        require(!native.lease_active() && !native.runtime().invalidated
                && native.native_dispatches() == dispatches,
            "checked slice publication releases the lease without replaying V2 members");
        require(native.acknowledge_boundary()
                    == RegionFrontierStatusV2::quiescent
                && native.native_dispatches() == dispatches,
            "the slice ACK retires once after the native body has completed");
        const auto native_state = capture_boundary_signal_state(
            native.implementation(), native.boundary_signal(),
            native.boundary_owner());

        NativeBoundaryFixture checked {
            signal_width, provider, false, false, false, false, false,
            offset, slice_width, source };
        checked.implementation().commit_driver_slice(
            consumer_process_id, checked.boundary_signal(),
            expected_slice, offset, origin, false);
        const auto checked_state = capture_boundary_signal_state(
            checked.implementation(), checked.boundary_signal(),
            consumer_process_id);
        require(same_alias_signal_state(native_state, checked_state)
                && native_state.current == expected_full
                && native_state.stored == expected_full
                && native_state.last == before.current
                && native_state.boundary_owner_value
                    == std::optional { expected_full }
                && native_state.raw_driver_count == 1U
                && native_state.event != before.event
                && native_state.transaction != before.transaction
                && native_state.stamp.origin.process_domain
                    == ProcessSchedulingDomain::systemverilog
                && native_state.stamp.origin.phase == SchedulerPhase::active,
            "V2 partial publication matches checked full-signal roles, raw owner, and event metadata");
    }
}

void check_alias_boundary_commit_reentry_matches_checked_reference(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    const bool proxy_slice_output = false)
{
    constexpr auto width = 1U;
    constexpr SignalChangeOrigin origin {
        ProcessSchedulingDomain::systemverilog,
        SchedulerPhase::active,
    };
    NativeBoundaryFixture native {
        width, provider, true, false, true, true, proxy_slice_output };
    require(native.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the alias leaf has an authentic pending receipt after both native bodies");
    native.require_boundary_alias_leaf_mapping();
    if (proxy_slice_output) {
        native.require_boundary_proxy_slice_source();
    }
    const auto pending_value = native.pending_boundary_value();
    require(pending_value == PackedLogic4 { width, Logic4::one },
        "the real consumer staged the expected value for its physical alias leaf");

    const auto pending_task = native.boundary_task();
    const auto cursor_before = native.task_cursor();
    const auto flags_before = native.pending_flags();
    const auto dispatches_before = native.native_dispatches();
    const auto generation_before = native.runtime().runtime_generation;
    AliasObserverTrace native_trace;
    install_reentrant_alias_observer(native.interpreter(),
        native.boundary_alias_object(), native.boundary_signal(),
        native.boundary_sibling_signal(), native.boundary_proxy_signal(),
        width, native_trace);
    require(!native.runtime().invalidated && native.lease_active()
            && native.runtime().runtime_generation == generation_before
            && native.task_cursor() == cursor_before
            && native.pending_flags() == flags_before,
        "a late observer leaves the completed body and pending host receipt intact");

    native.runtime().publish_boundary_commit(pending_task,
        static_cast<std::uint32_t>(native.boundary_pending_slot()),
        native.lease());
    require(!native.runtime().invalidated && !native.lease_active()
            && native.task_cursor() == cursor_before
            && native.native_dispatches() == dispatches_before
            && (native.pending_flags() & pending_committed) != 0U,
        "checked alias publication records the original ACK without rerunning the body");
    const auto native_ack = native.acknowledge_boundary();
    require(native_ack == RegionFrontierStatusV2::quiescent
            && native.task_cursor() == cursor_before + 1U
            && native.native_dispatches() == dispatches_before
            && (native.pending_flags() & pending_active) == 0U,
        "the original alias-leaf ACK retires once after the checked commit");
    const auto native_state = capture_alias_family_state(native);

    NativeBoundaryFixture checked {
        width, provider, false, false, true, false, proxy_slice_output };
    AliasObserverTrace checked_trace;
    install_reentrant_alias_observer(checked.interpreter(),
        checked.boundary_alias_object(), checked.boundary_signal(),
        checked.boundary_sibling_signal(), checked.boundary_proxy_signal(),
        width, checked_trace);
    if (proxy_slice_output) {
        checked.implementation().commit_driver_slice(consumer_process_id,
            checked.boundary_proxy_signal(), pending_value, width, origin,
            false);
    } else {
        checked.implementation().commit_driver(consumer_process_id,
            checked.boundary_signal(), pending_value, origin, false);
    }
    const auto checked_state = capture_alias_family_state(checked);

    const auto final = native_state;
    require(native_trace.nested_deposit_started
            && checked_trace.nested_deposit_started
            && !native_trace.object_reads.empty()
            && !native_trace.proxy_reads.empty()
            && native_trace.callbacks == checked_trace.callbacks
            && native_trace.object_reads == checked_trace.object_reads
            && native_trace.proxy_reads == checked_trace.proxy_reads
            && same_alias_family_state(final, checked_state),
        "late observer reentry preserves physical leaf, sibling, proxy, raw owner, and metadata parity");
    require(final.object_elements.size() == 2U
            && final.object_elements[0U] == PackedLogic4 { width, Logic4::zero }
            && final.object_elements[1U] == PackedLogic4 { width, Logic4::one }
            && final.signals[0U].current
                == PackedLogic4 { width, Logic4::zero }
            && final.signals[0U].stored
                == PackedLogic4 { width, Logic4::zero }
            && final.signals[0U].last
                == PackedLogic4 { width, Logic4::one }
            && final.signals[0U].raw_driver_count == 1U
            && final.signals[0U].boundary_owner_value
                == std::optional { pending_value }
            && final.signals[1U].current
                == PackedLogic4 { width, Logic4::one }
            && final.signals[1U].stored
                == PackedLogic4 { width, Logic4::one }
            && final.signals[1U].last
                == PackedLogic4 { width, Logic4::zero }
            && final.signals[1U].raw_driver_count == 0U
            && final.signals[2U].current
                == PackedLogic4::from_msb_string("01")
            && final.signals[2U].stored
                == PackedLogic4::from_msb_string("01")
            && final.signals[2U].last
                == PackedLogic4::from_msb_string("10")
            && final.signals[2U].raw_driver_count == 0U,
        "the reentrant deposit changes family backing while the original leaf driver record remains intact");
}

void check_alias_boundary_materialization_failure_is_precommit(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    constexpr auto width = wide_test_width;
    NativeBoundaryFixture fixture { width, provider, true, false, true };
    const auto baseline = capture_alias_family_state(fixture);
    require(fixture.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the wide alias family has a real native output before allocation failure");
    fixture.require_boundary_alias_leaf_mapping();
    const auto before = capture_alias_family_state(fixture);
    require(baseline.signals[0U].raw_driver_count == 1U
            && baseline.signals[0U].boundary_owner_value
                == std::optional { PackedLogic4 { width, Logic4::z } }
            && baseline.signals[1U].raw_driver_count == 0U
            && baseline.signals[2U].raw_driver_count == 0U
            && same_alias_family_state(baseline, before)
            && before.signals[0U].boundary_owner_value
                != std::optional { fixture.pending_boundary_value() },
        "native staging retains the registered Z driver and every prepublication alias role");
    const auto pending_task = fixture.boundary_task();
    const auto cursor_before = fixture.task_cursor();
    const auto flags_before = fixture.pending_flags();
    const auto dispatches_before = fixture.native_dispatches();
    bool caught_bad_alloc { };
    fsim::tests::runtime::staging_failure_support::
        arm_allocation_failure(0U);
    try {
        fixture.runtime().publish_boundary_commit(pending_task,
            static_cast<std::uint32_t>(fixture.boundary_pending_slot()),
            fixture.lease());
    } catch (const std::bad_alloc&) {
        caught_bad_alloc = true;
    } catch (...) {
        fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure();
        throw;
    }
    const auto injected = fsim::tests::runtime::staging_failure_support::
        allocation_failure_was_injected();
    fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
    require(caught_bad_alloc && injected && fixture.runtime().invalidated
            && fixture.lease_active()
            && fixture.task_cursor() == cursor_before
            && fixture.pending_flags() == flags_before
            && (flags_before & pending_committed) == 0U
            && fixture.native_dispatches() == dispatches_before
            && same_alias_family_state(before,
                capture_alias_family_state(fixture)),
        "precommit wide materialization failure leaves every alias role and the original unacked task unchanged");
}

void check_alias_proxy_slice_commit_failure_is_precommit(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    constexpr auto width = 1U;
    const auto successful_publication_allocations = [&] {
        NativeBoundaryFixture fixture {
            width, provider, true, false, true, true, true };
        require(fixture.reach_boundary_publication()
                    == RegionFrontierStatusV2::boundary_publication,
            "allocation counting starts after a real proxy-slice V2 output");
        fixture.require_boundary_alias_leaf_mapping();
        fixture.require_boundary_proxy_slice_source();
        const auto task = fixture.boundary_task();
        fsim::tests::runtime::staging_failure_support::
            begin_allocation_count();
        try {
            fixture.runtime().publish_boundary_commit(task,
                static_cast<std::uint32_t>(fixture.boundary_pending_slot()),
                fixture.lease());
        } catch (...) {
            static_cast<void>(fsim::tests::runtime::staging_failure_support::
                end_allocation_count());
            throw;
        }
        const auto allocations
            = fsim::tests::runtime::staging_failure_support::
                end_allocation_count();
        require((fixture.pending_flags() & pending_committed) != 0U
                && !fixture.lease_active(),
            "the allocation bound comes from a successful checked proxy commit");
        return allocations;
    }();
    require(successful_publication_allocations != 0U,
        "checked proxy-family publication has an allocation to fail before commit");

    struct FailureSite final {
        NativeBoundaryFixture* fixture { };
        std::uint32_t pending_slot { };
        bool called { };
        bool lease_active { };
        bool callback_started { };

        static void observe(void* const context) noexcept
        {
            auto& site = *static_cast<FailureSite*>(context);
            site.called = true;
            site.lease_active = site.fixture->lease_active();
            site.callback_started
                = site.fixture->runtime().boundary_callback_started_slot
                    == site.pending_slot;
        }
    };

    bool checked_family_failure_found { };
    for (std::size_t failure_index = 0U;
         failure_index < successful_publication_allocations;
         ++failure_index) {
        NativeBoundaryFixture fixture {
            width, provider, true, false, true, true, true };
        const auto baseline = capture_alias_family_state(fixture);
        require(fixture.reach_boundary_publication()
                    == RegionFrontierStatusV2::boundary_publication,
            "each failpoint trial starts from a real proxy-slice V2 output");
        fixture.require_boundary_alias_leaf_mapping();
        fixture.require_boundary_proxy_slice_source();
        const auto before = capture_alias_family_state(fixture);
        const auto pending_value = fixture.pending_boundary_value();
        require(pending_value == PackedLogic4 { width, Logic4::one }
                && baseline.signals[0U].raw_driver_count == 1U
                && baseline.signals[0U].boundary_owner_value
                    == std::optional { PackedLogic4 { width, Logic4::z } }
                && same_alias_family_state(baseline, before)
                && before.signals[0U].boundary_owner_value
                    != std::optional { pending_value },
            "the staged proxy slice leaves the selected leaf's Z row untouched");
        const auto task = fixture.boundary_task();
        const auto pending_slot = static_cast<std::uint32_t>(
            fixture.boundary_pending_slot());
        const auto cursor_before = fixture.task_cursor();
        const auto flags_before = fixture.pending_flags();
        const auto dispatches_before = fixture.native_dispatches();
        FailureSite failure_site { &fixture, pending_slot };
        fsim::tests::runtime::staging_failure_support::
            set_allocation_failure_observer(
                &failure_site, &FailureSite::observe);
        fsim::tests::runtime::staging_failure_support::
            arm_allocation_failure(failure_index);
        bool caught_bad_alloc { };
        try {
            fixture.runtime().publish_boundary_commit(
                task, pending_slot, fixture.lease());
        } catch (const std::bad_alloc&) {
            caught_bad_alloc = true;
        } catch (...) {
            fsim::tests::runtime::staging_failure_support::
                clear_allocation_failure();
            fsim::tests::runtime::staging_failure_support::
                set_allocation_failure_observer(nullptr, nullptr);
            throw;
        }
        const auto injected = fsim::tests::runtime::staging_failure_support::
            allocation_failure_was_injected();
        fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
        fsim::tests::runtime::staging_failure_support::
            set_allocation_failure_observer(nullptr, nullptr);
        if (!injected) {
            break;
        }

        require(caught_bad_alloc && failure_site.called
                && fixture.runtime().invalidated
                && fixture.task_cursor() == cursor_before
                && fixture.pending_flags() == flags_before
                && (flags_before & pending_committed) == 0U
                && fixture.native_dispatches() == dispatches_before
                && same_alias_family_state(before,
                    capture_alias_family_state(fixture)),
            "each injected proxy failure remains precommit, unacked, and replay-free");
        if (failure_site.callback_started) {
            require(!failure_site.lease_active && !fixture.lease_active(),
                "the selected checked-family failure happens after A4 lease release");
            checked_family_failure_found = true;
            break;
        }
        require(failure_site.lease_active && fixture.lease_active(),
            "earlier preparation failures retain the A4 lease");
    }
    require(checked_family_failure_found,
        "the ordinal scan reaches an allocation inside checked proxy-family preparation");
}

void check_alias_boundary_observer_exception_fails_stop_without_replay(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    const bool proxy_slice_output = false)
{
    constexpr auto width = 1U;
    NativeBoundaryFixture native {
        width, provider, true, false, true, true, proxy_slice_output };
    require(native.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the alias exception case begins after a real V2 body and receipt");
    native.require_boundary_alias_leaf_mapping();
    if (proxy_slice_output) {
        native.require_boundary_proxy_slice_source();
    }
    const auto pending_value = native.pending_boundary_value();
    const auto before = capture_alias_family_state(native);
    auto expected_proxy_value = before.signals[2U].current;
    expected_proxy_value.insert_bits(pending_value, width);
    const auto pending_task = native.boundary_task();
    const auto cursor_before = native.task_cursor();
    const auto flags_before = native.pending_flags();
    const auto dispatches_before = native.native_dispatches();
    std::size_t leaf_observers { };
    std::string callback_proxy_value;
    std::vector<std::string> callback_object_elements;
    native.interpreter().set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4&, SimulationTick) {
            if (signal != native.boundary_signal()) {
                return;
            }
            ++leaf_observers;
            const auto& object = native.interpreter().container_object_value(
                native.boundary_alias_object());
            for (const auto& element : object.elements) {
                callback_object_elements.push_back(element.to_msb_string());
            }
            callback_proxy_value = native.interpreter()
                .signal_value(native.boundary_proxy_signal()).to_msb_string();
            throw std::runtime_error { "alias boundary observer failure" };
        });
    bool propagated { };
    try {
        native.runtime().publish_boundary_commit(pending_task,
            static_cast<std::uint32_t>(native.boundary_pending_slot()),
            native.lease());
    } catch (const std::runtime_error& error) {
        propagated = std::string_view { error.what() }
            == "alias boundary observer failure";
    }
    const auto reached_before_retry = capture_alias_family_state(native);
    bool retry_rejected { };
    try {
        native.runtime().publish_boundary_commit(pending_task,
            static_cast<std::uint32_t>(native.boundary_pending_slot()),
            native.lease());
    } catch (const std::logic_error&) {
        retry_rejected = true;
    }
    const auto reached_after_retry = capture_alias_family_state(native);

    NativeBoundaryFixture checked {
        width, provider, false, false, true, false, proxy_slice_output };
    std::size_t checked_leaf_observers { };
    std::string checked_proxy_value;
    std::vector<std::string> checked_object_elements;
    checked.interpreter().set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4&, SimulationTick) {
            if (signal != checked.boundary_signal()) {
                return;
            }
            ++checked_leaf_observers;
            const auto& object = checked.interpreter().container_object_value(
                checked.boundary_alias_object());
            for (const auto& element : object.elements) {
                checked_object_elements.push_back(element.to_msb_string());
            }
            checked_proxy_value = checked.interpreter()
                .signal_value(checked.boundary_proxy_signal()).to_msb_string();
            throw std::runtime_error { "alias boundary observer failure" };
        });
    bool checked_propagated { };
    try {
        const SignalChangeOrigin origin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
        };
        if (proxy_slice_output) {
            checked.implementation().commit_driver_slice(
                consumer_process_id, checked.boundary_proxy_signal(),
                pending_value, width, origin, false);
        } else {
            checked.implementation().commit_driver(consumer_process_id,
                checked.boundary_signal(), pending_value, origin, false);
        }
    } catch (const std::runtime_error& error) {
        checked_propagated = std::string_view { error.what() }
            == "alias boundary observer failure";
    }

    require(propagated && checked_propagated && retry_rejected
            && leaf_observers == 1U && checked_leaf_observers == 1U
            && callback_object_elements
                == std::vector<std::string> { "1", "0" }
            && callback_object_elements == checked_object_elements
            && native.runtime().invalidated && !native.lease_active()
            && native.task_cursor() == cursor_before
            && native.pending_flags() == flags_before
            && (flags_before & pending_committed) == 0U
            && native.native_dispatches() == dispatches_before
            && callback_proxy_value == checked_proxy_value
            && same_alias_family_state(reached_before_retry,
                reached_after_retry)
            && same_alias_family_state(reached_after_retry,
                capture_alias_family_state(checked))
            && reached_after_retry.signals[0U].current == pending_value
            && reached_after_retry.signals[0U].stored == pending_value
            && reached_after_retry.signals[0U].boundary_owner_value
                == std::optional { pending_value }
            && reached_after_retry.signals[1U].current
                == PackedLogic4 { width, Logic4::zero }
            && reached_after_retry.signals[1U].raw_driver_count == 0U
            && reached_after_retry.signals[2U].current
                == expected_proxy_value
            && reached_after_retry.signals[2U].stored
                == expected_proxy_value
            && reached_after_retry.signals[1U].current
                == before.signals[1U].current
            && callback_proxy_value
                == expected_proxy_value.to_msb_string()
            && reached_after_retry.signals[2U].raw_driver_count == 0U,
        "postcommit alias observer failure retains checked side effects but never ACKs or replays the native body");
}

[[nodiscard]] const RegionFrontierPlaneV2& find_frontier_plane(
    const FrontierRuntime& runtime,
    const SignalId signal)
{
    const auto found = std::ranges::find(runtime.planes, signal,
        &RegionFrontierPlaneV2::signal_id);
    require(found != runtime.planes.end(),
        "the prepared JIT layout contains the requested runtime signal plane");
    return *found;
}

void require_metadata_matches_interpreter(
    const RuntimeImplementation& implementation,
    const FrontierRuntime& runtime)
{
    for (const auto& plane : runtime.planes) {
        if (plane.metadata_index == UINT32_MAX) {
            continue;
        }
        const auto signal = static_cast<SignalId>(plane.signal_id);
        const auto& actual = runtime.metadata.at(plane.metadata_index);
        const auto& event = implementation.signal_events.at(signal);
        const auto& stamp
            = implementation.signal_event_scheduling_stamps.at(signal);
        const auto& transaction
            = implementation.signal_transactions.at(signal);
        require(actual.event_valid == (event ? 1U : 0U)
                && actual.transaction_valid == (transaction ? 1U : 0U)
                && actual.value_revision
                    == implementation.signal_value_revisions.at(signal),
            "the binder copies event, transaction, and revision validity exactly");
        if (event) {
            require(actual.event_time == event->first
                    && actual.event_delta == event->second
                    && actual.systemverilog_round
                        == stamp.systemverilog_round
                    && actual.event_process_domain
                        == static_cast<std::uint32_t>(
                            stamp.origin.process_domain)
                    && actual.event_phase
                        == static_cast<std::uint32_t>(stamp.origin.phase),
                "the binder preserves the complete event scheduling stamp");
        } else {
            require(actual.event_time == 0U && actual.event_delta == 0U
                    && actual.systemverilog_round == 0U
                    && actual.event_process_domain == 0U
                    && actual.event_phase == 0U,
                "absent events have zeroed scheduling metadata");
        }
        if (transaction) {
            require(actual.transaction_time == transaction->first
                    && actual.transaction_delta == transaction->second,
                "the binder preserves the transaction timestamp and delta");
        } else {
            require(actual.transaction_time == 0U
                    && actual.transaction_delta == 0U,
                "absent transactions have zeroed metadata");
        }
        require(std::ranges::all_of(actual.reserved,
                    [](const std::uint8_t value) { return value == 0U; }),
            "the binder leaves ABI metadata reserved bytes zero");
    }
}

void require_writable_plane_matches_lease(
    const FrontierRuntime& runtime,
    const AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal)
{
    const auto& plane = find_frontier_plane(runtime, signal);
    const auto binding = std::ranges::find(runtime.writable_signals, signal,
        &AuthoritativeSignalPlanes::FrontierWriteBinding::signal);
    require(binding != runtime.writable_signals.end()
            && plane.value_kind == RegionFrontierValueKindV2::logic4
            && plane.plane_count == kRegionFrontierLogic4PlaneCountV2
            && region_frontier_plane_bindings_valid_v2(plane)
            && binding->owner == plane.owner_process_id,
        "each internal plane retains its original single-owner binding");
    const auto require_role = [&](const PackedPlaneRole role,
                                  const std::uint64_t* const aval,
                                  const std::uint64_t* const bval) {
        std::array<std::span<std::uint64_t>, 4U> words { };
        require(lease.plane_words(signal, role, binding->owner, words)
                && words[0U].size() == plane.word_count
                && words[1U].size() == plane.word_count
                && words[2U].empty() && words[3U].empty()
                && words[0U].data() == aval && words[1U].data() == bval,
            "the bound frame role pointers match the leased A4 backing");
    };
    require_role(PackedPlaneRole::current,
        plane.current_planes[0U], plane.current_planes[1U]);
    require_role(PackedPlaneRole::previous,
        plane.previous_planes[0U], plane.previous_planes[1U]);
    require_role(PackedPlaneRole::stored,
        plane.stored_planes[0U], plane.stored_planes[1U]);
    require_role(PackedPlaneRole::owner,
        plane.owner_planes[0U], plane.owner_planes[1U]);
}

[[nodiscard]] bool same_plane(const RegionFrontierPlaneV2& left,
    const RegionFrontierPlaneV2& right) noexcept
{
    return left.signal_id == right.signal_id
        && left.owner_process_id == right.owner_process_id
        && left.value_kind == right.value_kind
        && left.width == right.width && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && left.flags == right.flags
        && left.metadata_index == right.metadata_index
        && std::equal(std::begin(left.boundary_planes),
            std::end(left.boundary_planes), std::begin(right.boundary_planes))
        && std::equal(std::begin(left.current_planes),
            std::end(left.current_planes), std::begin(right.current_planes))
        && std::equal(std::begin(left.previous_planes),
            std::end(left.previous_planes), std::begin(right.previous_planes))
        && std::equal(std::begin(left.stored_planes),
            std::end(left.stored_planes), std::begin(right.stored_planes))
        && std::equal(std::begin(left.owner_planes),
            std::end(left.owner_planes), std::begin(right.owner_planes));
}

[[nodiscard]] bool same_metadata(
    const RegionFrontierSignalMetadataV2& left,
    const RegionFrontierSignalMetadataV2& right) noexcept
{
    return left.event_time == right.event_time
        && left.event_delta == right.event_delta
        && left.transaction_time == right.transaction_time
        && left.transaction_delta == right.transaction_delta
        && left.value_revision == right.value_revision
        && left.systemverilog_round == right.systemverilog_round
        && left.event_process_domain == right.event_process_domain
        && left.event_phase == right.event_phase
        && left.event_valid == right.event_valid
        && left.transaction_valid == right.transaction_valid
        && std::ranges::equal(left.reserved, right.reserved);
}

[[nodiscard]] bool same_planes(
    std::span<const RegionFrontierPlaneV2> left,
    std::span<const RegionFrontierPlaneV2> right) noexcept
{
    return std::ranges::equal(left, right, same_plane);
}

[[nodiscard]] bool same_metadata_vector(
    std::span<const RegionFrontierSignalMetadataV2> left,
    std::span<const RegionFrontierSignalMetadataV2> right) noexcept
{
    return std::ranges::equal(left, right, same_metadata);
}

[[nodiscard]] bool same_frontier_key(
    const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_pending_write(
    const RegionFrontierPendingWriteV2& left,
    const RegionFrontierPendingWriteV2& right) noexcept
{
    return left.member_index == right.member_index
        && left.signal_slot == right.signal_slot
        && left.source_instruction == right.source_instruction
        && left.update_kind == right.update_kind
        && left.flags == right.flags
        && left.reserved == right.reserved
        && same_frontier_key(left.commit_key, right.commit_key)
        && same_frontier_key(left.origin, right.origin)
        && left.value_kind == right.value_kind
        && left.width == right.width
        && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && std::ranges::equal(left.value_planes, right.value_planes);
}

[[nodiscard]] bool same_frontier_task(
    const RegionFrontierSchedulerTaskV2& left,
    const RegionFrontierSchedulerTaskV2& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.payload == right.payload;
}

[[nodiscard]] bool same_frontier_member(
    const RegionFrontierMemberV2& left,
    const RegionFrontierMemberV2& right) noexcept
{
    return left.process_id == right.process_id
        && left.flags == right.flags
        && left.static_trigger_mask == right.static_trigger_mask
        && same_frontier_key(left.queued_key, right.queued_key)
        && same_frontier_key(left.activation_origin, right.activation_origin)
        && same_frontier_key(left.pending_activation_origin,
            right.pending_activation_origin);
}

[[nodiscard]] bool same_staged_event(
    const RegionFrontierStagedEventV2& left,
    const RegionFrontierStagedEventV2& right) noexcept
{
    return left.kind == right.kind
        && left.descriptor_index == right.descriptor_index
        && left.stable_order == right.stable_order
        && same_frontier_key(left.origin, right.origin);
}

[[nodiscard]] bool same_committed_signal(
    const RegionFrontierCommittedSignalV2& left,
    const RegionFrontierCommittedSignalV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.changed == right.changed
        && left.state_changed == right.state_changed;
}

[[nodiscard]] bool same_event_stamp(
    const SignalEventSchedulingStamp& left,
    const SignalEventSchedulingStamp& right) noexcept
{
    return left.origin.process_domain == right.origin.process_domain
        && left.origin.phase == right.origin.phase
        && left.systemverilog_round == right.systemverilog_round;
}

[[nodiscard]] bool same_frontier_slot(
    const RegionFrontierSlotV2& left,
    const RegionFrontierSlotV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_frontier_cut(
    const RegionFrontierCutV2& left,
    const RegionFrontierCutV2& right) noexcept
{
    return left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && same_frontier_key(left.next_key, right.next_key)
        && left.kind == right.kind
        && std::ranges::equal(left.reserved, right.reserved);
}

[[nodiscard]] bool same_frontier_frame(
    const RegionFrontierFrameV2& left,
    const RegionFrontierFrameV2& right) noexcept
{
    return left.abi_version == right.abi_version
        && left.struct_size == right.struct_size
        && left.value_plane_contract == right.value_plane_contract
        && left.generic_update_ack_count == right.generic_update_ack_count
        && left.runtime_generation == right.runtime_generation
        && left.bound_runtime_generation == right.bound_runtime_generation
        && left.certificate_generation == right.certificate_generation
        && left.component_generation == right.component_generation
        && left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && left.member_count == right.member_count
        && left.scheduler_task_count == right.scheduler_task_count
        && left.scheduler_task_cursor == right.scheduler_task_cursor
        && left.scheduler_task_capacity == right.scheduler_task_capacity
        && left.readiness_word_count == right.readiness_word_count
        && left.signal_slot_count == right.signal_slot_count
        && left.metadata_count == right.metadata_count
        && left.fanout_edge_count == right.fanout_edge_count
        && left.committed_signal_capacity == right.committed_signal_capacity
        && left.committed_signal_count == right.committed_signal_count
        && left.pending_write_capacity == right.pending_write_capacity
        && left.pending_write_count == right.pending_write_count
        && left.staged_event_capacity == right.staged_event_capacity
        && left.staged_event_count == right.staged_event_count
        && left.current_member == right.current_member
        && left.current_pending_write == right.current_pending_write
        && left.current_commit_changed == right.current_commit_changed
        && left.saved_body_pc == right.saved_body_pc
        && left.ready_words == right.ready_words
        && left.members == right.members
        && left.scheduler_tasks == right.scheduler_tasks
        && left.planes == right.planes
        && left.metadata == right.metadata
        && left.fanout_edges == right.fanout_edges
        && left.port_planes == right.port_planes
        && left.pending_writes == right.pending_writes
        && left.staged_events == right.staged_events
        && left.committed_signals == right.committed_signals
        && left.native_frontier_member_dispatches
            == right.native_frontier_member_dispatches
        && left.stop_requested == right.stop_requested
        && same_frontier_slot(left.slot, right.slot)
        && same_frontier_cut(left.cut, right.cut);
}

struct InternalRoleWordSnapshot {
    std::array<std::array<std::uint64_t, 3U>, 4U> aval { };
    std::array<std::array<std::uint64_t, 3U>, 4U> bval { };

    bool operator==(const InternalRoleWordSnapshot&) const = default;
};

[[nodiscard]] InternalRoleWordSnapshot capture_role_words(
    const AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal,
    const ProcessId owner)
{
    constexpr std::array roles {
        PackedPlaneRole::current,
        PackedPlaneRole::previous,
        PackedPlaneRole::stored,
        PackedPlaneRole::owner,
    };
    InternalRoleWordSnapshot snapshot;
    for (std::size_t role_index = 0U;
         role_index < roles.size(); ++role_index) {
        std::array<std::span<std::uint64_t>, 4U> words { };
        require(lease.plane_words(signal, roles[role_index], owner, words)
                && words[0U].size() == 3U && words[1U].size() == 3U,
            "the test captures all three words from each leased Logic4 role");
        std::copy(words[0U].begin(), words[0U].end(),
            snapshot.aval[role_index].begin());
        std::copy(words[1U].begin(), words[1U].end(),
            snapshot.bval[role_index].begin());
    }
    return snapshot;
}

void check_binder_scalar_and_wide_boundary_inputs_are_direct(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    for (const auto width : { 1U, wide_test_width }) {
        NativeBoundaryFixture fixture { width, provider };
        const auto& plane = find_frontier_plane(
            fixture.runtime(), source_signal_id);
        const auto& signal = fixture.implementation().signals.at(
            source_signal_id);
        require(fixture.implementation()
                    .region_authoritative_component_by_signal.at(
                        source_signal_id)
                == std::numeric_limits<std::size_t>::max()
                && plane.flags == read_only_boundary_port
                && plane.value_kind == RegionFrontierValueKindV2::logic4
                && plane.plane_count == kRegionFrontierLogic4PlaneCountV2
                && plane.boundary_planes[0U] == signal.initial_value.aval_words().data()
                && plane.boundary_planes[1U] == signal.initial_value.bval_words().data()
                && plane.boundary_planes[2U] == nullptr
                && plane.boundary_planes[3U] == nullptr
                && fixture.implementation().direct_signal_materialization_pending.at(
                    source_signal_id) == 0U,
            "unseeded scalar and wide boundary inputs borrow current packed planes");
    }
}

void check_binder_dense_pending_boundary_is_allocation_free(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { wide_test_width, provider };
    auto& implementation = fixture.implementation();
    auto& runtime = fixture.runtime();
    const auto offset = static_cast<std::size_t>(
        implementation.direct_wide_signal_offsets.at(source_signal_id));
    const auto word_count = static_cast<std::size_t>(
        (wide_test_width + 63U) / 64U);
    require(offset <= implementation.direct_wide_signal_aval.size()
            && offset <= implementation.direct_wide_signal_bval.size()
            && word_count
                <= implementation.direct_wide_signal_aval.size() - offset
            && word_count
                <= implementation.direct_wide_signal_bval.size() - offset
            && implementation.direct_signal_materialization_pending.at(
                source_signal_id) == 0U,
        "the pending-boundary witness starts from valid preallocated dense storage");
    const auto& original_signal
        = implementation.signals.at(source_signal_id).initial_value;
    require(original_signal.aval_words().size() == word_count
            && original_signal.bval_words().size() == word_count,
        "the wide input has the expected packed word shape");
    std::copy(original_signal.aval_words().begin(),
        original_signal.aval_words().end(),
        implementation.direct_wide_signal_aval.begin()
            + static_cast<std::ptrdiff_t>(offset));
    std::copy(original_signal.bval_words().begin(),
        original_signal.bval_words().end(),
        implementation.direct_wide_signal_bval.begin()
            + static_cast<std::ptrdiff_t>(offset));
    implementation.signals.at(source_signal_id).initial_value
        = PackedLogic4 { wide_test_width, Logic4::zero };
    const PackedLogic4 stale_packed_value
        = implementation.signals.at(source_signal_id).initial_value;
    implementation.direct_signal_materialization_pending.at(
        source_signal_id) = 1U;

    fsim::tests::runtime::staging_failure_support::begin_allocation_count();
    const bool bound = runtime.bind_frame_planes_and_metadata(fixture.lease());
    const auto allocations
        = fsim::tests::runtime::staging_failure_support::end_allocation_count();
    const auto& plane = find_frontier_plane(runtime, source_signal_id);
    require(bound && allocations == 0U
            && plane.value_kind == RegionFrontierValueKindV2::logic4
            && plane.plane_count == kRegionFrontierLogic4PlaneCountV2
            && plane.boundary_planes[0U]
                == implementation.direct_wide_signal_aval.data() + offset
            && plane.boundary_planes[1U]
                == implementation.direct_wide_signal_bval.data() + offset
            && plane.boundary_planes[2U] == nullptr
            && plane.boundary_planes[3U] == nullptr
            && region_frontier_plane_bindings_valid_v2(plane)
            && implementation.direct_signal_materialization_pending.at(
                source_signal_id) == 1U
            && implementation.signals.at(source_signal_id).initial_value
                == stale_packed_value,
        "pending boundary reads bind dense current without materializing or allocating");
}

void check_binder_roles_metadata_and_declines_are_exact(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { wide_test_width, provider };
    auto& implementation = fixture.implementation();
    auto& runtime = fixture.runtime();
    const auto revision_before
        = runtime.authoritative_state->values().revision();
    const auto metadata_before = runtime.metadata;
    const auto* const internal_driver_record
        = implementation.driver_values.at(internal_signal_id)
              .find(producer_process_id);
    require(internal_driver_record != nullptr,
        "the resolved internal signal retains its original owner record");
    const auto saved_internal_roles = capture_role_words(fixture.lease(),
        internal_signal_id, producer_process_id);
    const auto saved_alias_roles = capture_role_words(fixture.lease(),
        aliased_internal_signal_id, producer_process_id);
    const auto saved_events = implementation.signal_events;
    const auto saved_transactions = implementation.signal_transactions;
    const auto saved_revisions = implementation.signal_value_revisions;
    require(runtime.metadata.size() == 2U
            && std::ranges::all_of(runtime.planes,
                [&](const RegionFrontierPlaneV2& plane) {
                    return plane.flags != certified_internal_single_owner
                        || (plane.metadata_index < runtime.metadata.size()
                            && runtime.metadata[plane.metadata_index].event_valid
                                == 1U
                            && runtime.metadata[plane.metadata_index]
                                    .transaction_valid == 1U
                            && runtime.metadata[plane.metadata_index]
                                    .value_revision != 0U);
                }),
        "each internal row starts with concrete event, transaction, and revision metadata");
    require(implementation.signal_events.at(internal_signal_id).has_value()
            && implementation.signal_transactions.at(internal_signal_id)
                .has_value()
            && implementation.signal_value_revisions.at(internal_signal_id)
                != 0U,
        "the fixture seeds nondefault event, transaction, and revision metadata");
    fsim::tests::runtime::staging_failure_support::begin_allocation_count();
    const bool rebound = runtime.bind_frame_planes_and_metadata(fixture.lease());
    const auto success_allocations
        = fsim::tests::runtime::staging_failure_support::end_allocation_count();
    require(rebound && success_allocations == 0U
            && runtime.authoritative_state->values().revision()
                == revision_before
            && implementation.driver_values.at(internal_signal_id)
                   .find(producer_process_id) == internal_driver_record
            && capture_role_words(fixture.lease(), internal_signal_id,
                    producer_process_id) == saved_internal_roles
            && capture_role_words(fixture.lease(), aliased_internal_signal_id,
                    producer_process_id) == saved_alias_roles
            && implementation.signal_events == saved_events
            && implementation.signal_transactions == saved_transactions
            && implementation.signal_value_revisions == saved_revisions
            && same_metadata_vector(runtime.metadata, metadata_before),
        "valid frame rebinding is allocation-free and does not mutate A4 revision");
    require_metadata_matches_interpreter(implementation, runtime);
    require_writable_plane_matches_lease(runtime, fixture.lease(),
        internal_signal_id);
    require_writable_plane_matches_lease(runtime, fixture.lease(),
        aliased_internal_signal_id);
    const auto& aliased_plane = find_frontier_plane(
        runtime, aliased_internal_signal_id);
    const auto& separate_plane = find_frontier_plane(
        runtime, internal_signal_id);
    const auto alias_owner = runtime.authoritative_state->values()
        .layout().owners(aliased_internal_signal_id);
    const auto separate_owner = runtime.authoritative_state->values()
        .layout().owners(internal_signal_id);
    require(alias_owner.size() == 1U && alias_owner.front().aliases_stored
            && aliased_plane.owner_planes[0U] == aliased_plane.stored_planes[0U]
            && aliased_plane.owner_planes[1U] == aliased_plane.stored_planes[1U]
            && separate_owner.size() == 1U
            && !separate_owner.front().aliases_stored
            && separate_plane.owner_planes[0U] != separate_plane.stored_planes[0U]
            && separate_plane.owner_planes[1U] != separate_plane.stored_planes[1U],
        "the binder preserves aliased unresolved and separate resolved owner roles");

    const auto prior_runtime_pin
        = implementation.region_authoritative_state_by_component.at(
            runtime.component);
    const auto frame_plane_snapshot = runtime.planes;
    const auto metadata_snapshot = runtime.metadata;
    implementation.region_authoritative_state_by_component.at(
        runtime.component).reset();
    fsim::tests::runtime::staging_failure_support::begin_allocation_count();
    const bool stale_pin_bound
        = runtime.bind_frame_planes_and_metadata(fixture.lease());
    const auto pin_decline_allocations
        = fsim::tests::runtime::staging_failure_support::end_allocation_count();
    implementation.region_authoritative_state_by_component.at(
        runtime.component) = prior_runtime_pin;
    require(!stale_pin_bound && pin_decline_allocations == 0U
            && same_planes(runtime.planes, frame_plane_snapshot)
            && same_metadata_vector(runtime.metadata, metadata_snapshot)
            && runtime.authoritative_state->values().revision()
                == revision_before
            && implementation.driver_values.at(internal_signal_id)
                   .find(producer_process_id) == internal_driver_record
            && capture_role_words(fixture.lease(), internal_signal_id,
                    producer_process_id) == saved_internal_roles
            && capture_role_words(fixture.lease(), aliased_internal_signal_id,
                    producer_process_id) == saved_alias_roles
            && implementation.signal_events == saved_events
            && implementation.signal_transactions == saved_transactions
            && implementation.signal_value_revisions == saved_revisions
            && !runtime.invalidated,
        "a stale published component pin declines before entry without mutation or allocation");

    const auto old_component_generation = runtime.frame.component_generation;
    runtime.frame.component_generation ^= 1U;
    fsim::tests::runtime::staging_failure_support::begin_allocation_count();
    const bool layout_mismatch_bound
        = runtime.bind_frame_planes_and_metadata(fixture.lease());
    const auto layout_decline_allocations
        = fsim::tests::runtime::staging_failure_support::end_allocation_count();
    runtime.frame.component_generation = old_component_generation;
    require(!layout_mismatch_bound && layout_decline_allocations == 0U
            && same_planes(runtime.planes, frame_plane_snapshot)
            && same_metadata_vector(runtime.metadata, metadata_snapshot)
            && runtime.authoritative_state->values().revision()
                == revision_before
            && implementation.driver_values.at(internal_signal_id)
                   .find(producer_process_id) == internal_driver_record
            && capture_role_words(fixture.lease(), internal_signal_id,
                    producer_process_id) == saved_internal_roles
            && capture_role_words(fixture.lease(), aliased_internal_signal_id,
                    producer_process_id) == saved_alias_roles
            && implementation.signal_events == saved_events
            && implementation.signal_transactions == saved_transactions
            && implementation.signal_value_revisions == saved_revisions
            && !runtime.invalidated,
        "a certificate/layout mismatch declines before entry without mutation or allocation");
}

void check_invalid_boundary_descriptor_fails_closed(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { 1U, provider };
    const auto original_owner_before = fixture.original_owner_value();
    const auto public_boundary_before = fixture.public_boundary_value();
    const auto boundary_revision_before
        = fixture.implementation().signal_value_revisions.at(
            fixture.boundary_signal());
    const auto boundary_transaction_before
        = fixture.implementation().signal_transactions.at(
            fixture.boundary_signal());
    require(fixture.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the real generated entry stages a boundary row before descriptor validation");
    const auto& staged_write = fixture.runtime().pending_writes.at(
        fixture.boundary_pending_slot());
    require(staged_write.value_kind == RegionFrontierValueKindV2::logic4
            && staged_write.width == 1U && staged_write.word_count == 1U
            && staged_write.plane_count == kRegionFrontierLogic4PlaneCountV2
            && staged_write.value_planes[0U] != nullptr
            && staged_write.value_planes[1U] != nullptr
            && staged_write.value_planes[2U] == nullptr
            && staged_write.value_planes[3U] == nullptr
            && (staged_write.value_planes[0U][0U] & 1U) == 1U
            && (staged_write.value_planes[1U][0U] & 1U) == 0U
            && original_owner_before != PackedLogic4 { 1U, Logic4::one }
            && public_boundary_before != PackedLogic4 { 1U, Logic4::one }
            && fixture.original_owner_value() == original_owner_before
            && fixture.public_boundary_value() == public_boundary_before,
        "the staged one differs from both uncommitted boundary values");
    const auto task = fixture.boundary_task();
    auto corrupted_task = task;
    ++corrupted_task.stable_order;
    const auto cursor = fixture.task_cursor();
    const auto flags = fixture.pending_flags();
    const auto dispatches = fixture.native_dispatches();
    bool rejected { };
    try {
        fixture.runtime().publish_boundary_commit(corrupted_task,
            static_cast<std::uint32_t>(fixture.boundary_pending_slot()),
            fixture.lease());
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected && fixture.runtime().invalidated
            && fixture.lease_active()
            && fixture.task_cursor() == cursor
            && fixture.native_dispatches() == dispatches
            && fixture.pending_flags() == flags
            && (flags & pending_committed) == 0U
            && fixture.original_owner_value()
                == original_owner_before
            && fixture.public_boundary_value()
                == public_boundary_before
            && fixture.implementation().signal_value_revisions.at(
                fixture.boundary_signal()) == boundary_revision_before
            && fixture.implementation().signal_transactions.at(
                fixture.boundary_signal()) == boundary_transaction_before,
        "an invalid scheduler descriptor fails before publication or ACK");
}

void check_malformed_fallback_pointer_declines_before_read(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { wide_test_width, provider };
    require(fixture.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the generated entry stages a real wide boundary write first");

    auto& implementation = fixture.implementation();
    auto& runtime = fixture.runtime();
    const auto task = fixture.boundary_task();
    const auto slot = fixture.boundary_pending_slot();
    auto& write = runtime.pending_writes.at(slot);
    constexpr std::uint32_t required_flags
        = pending_active | pending_value_ready | pending_key_assigned
        | pending_boundary_target;
    require((write.flags & required_flags) == required_flags
            && write.value_kind == RegionFrontierValueKindV2::logic4
            && write.plane_count == kRegionFrontierLogic4PlaneCountV2
            && write.word_count == 3U
            && write.value_planes[0U]
                == runtime.pending_plane_words.data()
                    + runtime.pending_plane_offsets.at(slot),
        "the fixture targets an active, storage-backed generated value plane");
    const auto pending_offset = runtime.pending_plane_offsets.at(slot);
    const auto pending_value_before = PackedLogic4::from_word_planes(
        write.width,
        std::span<const std::uint64_t> {
            runtime.pending_plane_words.data() + pending_offset,
            write.word_count,
        },
        std::span<const std::uint64_t> {
            runtime.pending_plane_words.data() + pending_offset
                + write.word_count,
            write.word_count,
        });
    const auto valid_before = runtime.make_fallback_descriptor(task.payload);
    require(valid_before.invoke != nullptr,
        "a well-formed pending boundary row has a fallback descriptor");

    const auto frame_before = runtime.frame;
    const auto pending_before = runtime.pending_writes;
    const auto pending_words_before = runtime.pending_plane_words;
    const auto pending_offsets_before = runtime.pending_plane_offsets;
    const auto members_before = runtime.members;
    const auto tasks_before = runtime.scheduler_tasks;
    const auto staged_before = runtime.staged_events;
    const auto committed_before = runtime.committed_signals;
    const auto ready_before = runtime.ready_words;
    const auto planes_before = runtime.planes;
    const auto metadata_before = runtime.metadata;
    const auto events_before = implementation.signal_events;
    const auto transactions_before = implementation.signal_transactions;
    const auto revisions_before = implementation.signal_value_revisions;
    const auto event_stamps_before
        = implementation.signal_event_scheduling_stamps;
    const auto public_value_before = fixture.public_boundary_value();
    const auto owner_value_before = fixture.original_owner_value();
    const auto internal_roles_before = capture_role_words(fixture.lease(),
        internal_signal_id, producer_process_id);
    const auto alias_roles_before = capture_role_words(fixture.lease(),
        aliased_internal_signal_id, producer_process_id);
    const auto values_revision_before
        = runtime.authoritative_state->values().revision();
    const auto dispatches_before = fixture.native_dispatches();
    const auto invalidated_before = runtime.invalidated;
    const auto lease_active_before = fixture.lease_active();
    const auto scheduler_time_before = implementation.scheduler.now();
    const auto scheduler_delta_before = implementation.scheduler.delta();
    const auto scheduler_round_before
        = implementation.scheduler.systemverilog_round();
    const auto scheduler_phase_before
        = implementation.scheduler.current_phase();
    const auto sequence_probe_before
        = implementation.scheduler.reserve_order_key(task.stable_order);

    auto expected_poisoned_pending = pending_before;
    auto* const original_plane = write.value_planes[0U];
    auto* const unreadable_plane = reinterpret_cast<std::uint64_t*>(
        std::uintptr_t { 1U });
    expected_poisoned_pending[slot].value_planes[0U] = unreadable_plane;
    struct RestorePlanePointer final {
        std::uint64_t*& target;
        std::uint64_t* value;

        ~RestorePlanePointer()
        {
            target = value;
        }
    };

    {
        RestorePlanePointer restore { write.value_planes[0U], original_plane };
        write.value_planes[0U] = unreadable_plane;
        const auto declined
            = runtime.make_fallback_descriptor(task.payload);
        const auto sequence_probe_after
            = implementation.scheduler.reserve_order_key(task.stable_order);

        require(declined.invoke == nullptr
                && sequence_probe_after.sequence
                    == sequence_probe_before.sequence + 1U
                && sequence_probe_after.order == task.stable_order
                && same_frontier_frame(runtime.frame, frame_before)
                && std::ranges::equal(runtime.pending_writes,
                    expected_poisoned_pending, same_pending_write)
                && runtime.pending_plane_words == pending_words_before
                && runtime.pending_plane_offsets == pending_offsets_before
                && std::ranges::equal(runtime.members, members_before,
                    same_frontier_member)
                && std::ranges::equal(runtime.scheduler_tasks, tasks_before,
                    same_frontier_task)
                && std::ranges::equal(runtime.staged_events, staged_before,
                    same_staged_event)
                && std::ranges::equal(runtime.committed_signals,
                    committed_before, same_committed_signal)
                && runtime.ready_words == ready_before
                && same_planes(runtime.planes, planes_before)
                && same_metadata_vector(runtime.metadata, metadata_before)
                && implementation.signal_events == events_before
                && implementation.signal_transactions == transactions_before
                && implementation.signal_value_revisions == revisions_before
                && std::ranges::equal(
                    implementation.signal_event_scheduling_stamps,
                    event_stamps_before, same_event_stamp)
                && fixture.public_boundary_value() == public_value_before
                && fixture.original_owner_value() == owner_value_before
                && capture_role_words(fixture.lease(), internal_signal_id,
                    producer_process_id) == internal_roles_before
                && capture_role_words(fixture.lease(), aliased_internal_signal_id,
                    producer_process_id) == alias_roles_before
                && runtime.authoritative_state->values().revision()
                    == values_revision_before
                && fixture.native_dispatches() == dispatches_before
                && runtime.invalidated == invalidated_before
                && fixture.lease_active() == lease_active_before
                && implementation.scheduler.now() == scheduler_time_before
                && implementation.scheduler.delta() == scheduler_delta_before
                && implementation.scheduler.systemverilog_round()
                    == scheduler_round_before
                && implementation.scheduler.current_phase()
                    == scheduler_phase_before,
            "a forged value-plane pointer declines before any read or state change");
    }

    require(write.value_planes[0U] == original_plane
            && std::ranges::equal(runtime.pending_writes, pending_before,
                same_pending_write)
            && runtime.make_fallback_descriptor(task.payload).invoke != nullptr,
        "restoring the authenticated backing re-enables the original fallback descriptor");
    runtime.publish_boundary_commit(task,
        static_cast<std::uint32_t>(slot), fixture.lease());
    const auto ack_status = fixture.acknowledge_boundary();
    require(fixture.original_owner_value() == pending_value_before
            && fixture.public_boundary_value() == pending_value_before
            && ack_status == RegionFrontierStatusV2::quiescent,
        "the checked boundary commit and ACK still complete after safe decline");
}

void check_direct_partial_boundary_fallback_consumes_once(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    constexpr std::uint32_t signal_width = 129U;
    constexpr std::uint32_t offset = 63U;
    constexpr std::uint32_t slice_width = 66U;
    constexpr SignalChangeOrigin origin {
        ProcessSchedulingDomain::systemverilog,
        SchedulerPhase::active,
    };
    const auto source = make_partial_boundary_source(
        signal_width, offset, slice_width);
    const auto pending_value = source.extract_bits(offset, slice_width);
    NativeBoundaryFixture native {
        signal_width, provider, true, false, false, true, false,
        offset, slice_width, source };
    native.require_direct_partial_boundary_source();
    require(native.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the real native entry stages the direct partial fallback row first");
    const auto task = native.boundary_task();
    const auto pending_slot = native.boundary_pending_slot();
    const auto dispatches = native.native_dispatches();
    const auto before = capture_boundary_signal_state(
        native.implementation(), native.boundary_signal(),
        native.boundary_owner());
    auto& runtime = native.runtime();
    const auto& write_before = runtime.pending_writes.at(pending_slot);
    require(write_before.width == slice_width
            && write_before.word_count == 2U
            && runtime.planes.at(write_before.signal_slot).width
                == signal_width
            && native.pending_boundary_value() == pending_value,
        "the fallback row preserves its partial source value and full target plane");

    auto& scheduler = native.implementation().scheduler;
    SchedulerSystemVerilogKeyReceipt receipt;
    std::size_t decline_calls { };
    bool key_authenticated { };
    bool lease_released { };
    class PartialBoundaryDecliner final : public SchedulerBatchTask {
    public:
        PartialBoundaryDecliner(NativeBoundaryFixture& fixture,
            SchedulerSystemVerilogKeyReceipt& receipt,
            const std::uint64_t payload, std::size_t& calls,
            bool& authenticated, bool& released)
            : fixture_ { fixture }
            , receipt_ { receipt }
            , payload_ { payload }
            , calls_ { calls }
            , authenticated_ { authenticated }
            , released_ { released }
        {
        }

        [[nodiscard]] SchedulerBatchResult execute(
            Scheduler& active_scheduler,
            const std::span<const std::uint64_t> payloads) override
        {
            ++calls_;
            auto& runtime = fixture_.runtime();
            auto& frame = runtime.frame;
            require(receipt_.valid && payloads.size() == 1U
                    && payloads.front() == payload_
                    && active_scheduler.now() == receipt_.time
                    && active_scheduler.delta() == receipt_.delta
                    && active_scheduler.systemverilog_round()
                        == receipt_.systemverilog_round
                    && active_scheduler.current_phase()
                        == std::optional { receipt_.phase }
                    && receipt_.phase == SchedulerPhase::active
                    && frame.current_pending_write
                        == fixture_.boundary_pending_slot()
                    && frame.scheduler_task_cursor
                        < frame.scheduler_task_count,
                "the fallback envelope authenticates its original active boundary task");
            auto& write = runtime.pending_writes.at(
                fixture_.boundary_pending_slot());
            frame.slot.time = receipt_.time;
            frame.slot.delta = receipt_.delta;
            frame.slot.systemverilog_round
                = receipt_.systemverilog_round;
            frame.slot.process_domain = static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog);
            frame.slot.phase = static_cast<std::uint32_t>(receipt_.phase);
            write.commit_key = RegionFrontierKeyV2 {
                receipt_.time,
                receipt_.delta,
                receipt_.systemverilog_round,
                receipt_.stable_order,
                receipt_.sequence,
                static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog),
                static_cast<std::uint32_t>(receipt_.phase),
            };
            runtime.scheduler_tasks.at(frame.scheduler_task_cursor)
                = RegionFrontierSchedulerTaskV2 {
                    receipt_.stable_order,
                    receipt_.sequence,
                    payload_,
                };
            authenticated_ = true;
            fixture_.lease().release();
            released_ = !fixture_.lease_active();
            return { };
        }

    private:
        NativeBoundaryFixture& fixture_;
        SchedulerSystemVerilogKeyReceipt& receipt_;
        std::uint64_t payload_ { };
        std::size_t& calls_;
        bool& authenticated_;
        bool& released_;
    } decliner { native, receipt, task.payload, decline_calls,
        key_authenticated, lease_released };

    std::size_t fallback_calls { };
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active,
        static_cast<StableOrder>(native.boundary_owner()), decliner,
        task.payload,
        [&](Scheduler& active_scheduler) {
            ++fallback_calls;
            require(key_authenticated && lease_released
                    && !active_scheduler.current_batch_frontier().has_value()
                    && !native.lease_active(),
                "the scheduler releases the borrowed frontier before checked fallback");
            const auto descriptor = runtime.make_fallback_descriptor(
                task.payload);
            require(descriptor.invoke != nullptr,
                "an authentic pending direct slice has a production fallback descriptor");
            descriptor(active_scheduler);
        },
        { }, &receipt);
    require(receipt.valid && receipt.phase == SchedulerPhase::active
            && receipt.stable_order == native.boundary_owner(),
        "the queued fallback carries a scheduler-authored active key");
    const auto run = scheduler.run();
    require(run.status == RunStatus::completed
            && run.callbacks_executed == 1U
            && decline_calls == 1U && fallback_calls == 1U
            && key_authenticated && lease_released,
        "one declined batch reaches the checked partial-slice fallback once");
    const auto after = capture_boundary_signal_state(
        native.implementation(), native.boundary_signal(),
        native.boundary_owner());
    auto expected_full = PackedLogic4 { signal_width, Logic4::z };
    expected_full.insert_bits(pending_value, offset);
    const auto& committed = runtime.pending_writes.at(pending_slot);
    const auto repeated = runtime.make_fallback_descriptor(task.payload);
    require(runtime.invalidated && !native.lease_active()
            && native.native_dispatches() == dispatches
            && runtime.frame.pending_write_count == 0U
            && runtime.frame.current_pending_write == UINT32_MAX
            && (committed.flags & pending_active) == 0U
            && (committed.flags & pending_committed) != 0U
            && repeated.invoke == nullptr
            && after.current == expected_full
            && after.stored == expected_full
            && after.last == before.current
            && after.boundary_owner_value
                == std::optional { expected_full }
            && after.raw_driver_count == 1U
            && after.event != before.event
            && after.transaction != before.transaction
            && after.stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && after.stamp.origin.phase == SchedulerPhase::active,
        "checked fallback inserts only the source slice, publishes full-width roles and never replays the member");

    NativeBoundaryFixture checked {
        signal_width, provider, false, false, false, false, false,
        offset, slice_width, source };
    auto& checked_scheduler = checked.implementation().scheduler;
    bool checked_commit_ran { };
    const auto owner_order
        = static_cast<StableOrder>(consumer_process_id);
    checked_scheduler.schedule_systemverilog(
        SchedulerPhase::active, owner_order,
        [&](Scheduler& active_scheduler) {
            require(after.event.has_value()
                    && active_scheduler.now() == after.event->first
                    && active_scheduler.delta() + 1U
                        == after.event->second
                    && active_scheduler.systemverilog_round()
                        == after.stamp.systemverilog_round
                    && active_scheduler.current_phase()
                        == std::optional { SchedulerPhase::active },
                "the checked reference enters the active round before the same event delta");
            checked.implementation().commit_driver_slice(
                consumer_process_id, checked.boundary_signal(),
                pending_value, offset, origin, false);
            checked_commit_ran = true;
        });
    const auto checked_run = checked_scheduler.run();
    require(checked_run.status == RunStatus::completed
            && checked_commit_ran,
        "the checked reference commits once in the scheduler active round");
    const auto checked_state = capture_boundary_signal_state(
        checked.implementation(), checked.boundary_signal(),
        consumer_process_id);
    require(same_alias_signal_state(after, checked_state)
            && checked_state.current == expected_full,
        "fallback full signal, raw owner, history and event stamps match checked slice semantics");
}

void check_proxy_slice_descriptor_fallback_consumes_once(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    constexpr auto width = 1U;
    constexpr SignalChangeOrigin origin {
        ProcessSchedulingDomain::systemverilog,
        SchedulerPhase::active,
    };
    NativeBoundaryFixture native {
        width, provider, true, false, true, true, true };
    require(native.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "both native member bodies stage the real proxy-slice boundary row first");
    native.require_boundary_alias_leaf_mapping();
    native.require_boundary_proxy_slice_source();
    const auto pending_value = native.pending_boundary_value();
    require(pending_value == PackedLogic4 { width, Logic4::one },
        "the authentic V2 output is the selected first alias leaf");
    const auto family_before = capture_alias_family_state(native);
    const auto pending_task = native.boundary_task();
    const auto pending_slot = native.boundary_pending_slot();
    const auto pending_flags = native.pending_flags();
    const auto pending_count = native.runtime().frame.pending_write_count;
    const auto dispatches = native.native_dispatches();
    const auto cursor = native.task_cursor();
    require((pending_flags & (pending_active | pending_value_ready
                                  | pending_key_assigned
                                  | pending_boundary_target))
                == (pending_active | pending_value_ready
                    | pending_key_assigned | pending_boundary_target)
            && (pending_flags & pending_committed) == 0U
            && pending_count == 1U && cursor == 0U,
        "the fallback starts from one active uncommitted boundary row");

    auto& implementation = native.implementation();
    auto& runtime = native.runtime();
    auto& scheduler = implementation.scheduler;
    SchedulerSystemVerilogKeyReceipt receipt;
    std::size_t batch_calls { };
    bool frontier_authenticated { };
    bool lease_released { };

    class BoundaryFallbackEnvelope final : public SchedulerBatchTask {
    public:
        BoundaryFallbackEnvelope(NativeBoundaryFixture& fixture,
            SchedulerSystemVerilogKeyReceipt& key_receipt,
            const std::uint32_t slot, const std::uint64_t task_payload,
            std::size_t& calls, bool& authenticated, bool& released)
            : fixture_ { fixture }
            , receipt_ { key_receipt }
            , slot_ { slot }
            , payload_ { task_payload }
            , calls_ { calls }
            , authenticated_ { authenticated }
            , released_ { released }
        {
        }

        [[nodiscard]] SchedulerBatchResult execute(Scheduler& active_scheduler,
            const std::span<const std::uint64_t> payloads) override
        {
            ++calls_;
            const auto frontier = active_scheduler.current_batch_frontier();
            require(receipt_.valid && frontier.has_value()
                    && frontier->generation != 0U
                    && frontier->time == receipt_.time
                    && frontier->delta == receipt_.delta
                    && frontier->systemverilog_round
                        == receipt_.systemverilog_round
                    && frontier->phase == receipt_.phase
                    && frontier->phase == SchedulerPhase::active
                    && frontier->cursor == 0U && frontier->end == 1U
                    && frontier->tasks.size() == 1U
                    && payloads.size() == 1U
                    && payloads.front() == payload_
                    && frontier->tasks.front().stable_order
                        == receipt_.stable_order
                    && frontier->tasks.front().sequence == receipt_.sequence
                    && frontier->tasks.front().payload == payload_
                    && receipt_.stable_order == fixture_.boundary_owner()
                    && active_scheduler.now() == receipt_.time
                    && active_scheduler.delta() == receipt_.delta
                    && active_scheduler.systemverilog_round()
                        == receipt_.systemverilog_round
                    && active_scheduler.current_phase()
                        == std::optional { receipt_.phase },
                "the scheduler offers the exact receipt for the authentic boundary payload");

            auto& runtime = fixture_.runtime();
            auto& frame = runtime.frame;
            require(!runtime.invalidated && fixture_.lease_active()
                    && slot_ < runtime.pending_writes.size()
                    && frame.scheduler_task_cursor == 0U
                    && frame.scheduler_task_cursor
                        < frame.scheduler_task_count
                    && frame.scheduler_tasks == runtime.scheduler_tasks.data()
                    && frame.current_pending_write == slot_,
                "the declined native row and frame still name the staged output");
            auto& write = runtime.pending_writes.at(slot_);
            const auto& layout = runtime.backend->executor->layout();
            const RegionFrontierWriteSiteV2* write_site { };
            for (std::size_t index = 0U;
                 index < layout.write_site_count; ++index) {
                const auto& candidate = layout.write_sites[index];
                if (candidate.pending_slot != slot_) {
                    continue;
                }
                require(write_site == nullptr,
                    "the generated pending slot has one write site");
                write_site = &candidate;
            }
            require(write.member_index < runtime.members.size()
                    && runtime.members[write.member_index].process_id
                        == fixture_.boundary_owner()
                    && write_site != nullptr
                    && write_site->member_index == write.member_index
                    && write_site->signal_slot == write.signal_slot
                    && write_site->source_instruction
                        == write.source_instruction,
                "the scheduler receipt retains the original leaf owner and source site");

            const auto original_key = write.commit_key;
            const auto& original_task = runtime.scheduler_tasks.at(
                frame.scheduler_task_cursor);
            require(original_key.time == frame.slot.time
                    && original_key.delta == frame.slot.delta
                    && original_key.systemverilog_round
                        == frame.slot.systemverilog_round
                    && original_key.process_domain
                        == frame.slot.process_domain
                    && original_key.phase == frame.slot.phase
                    && original_key.stable_order
                        == original_task.stable_order
                    && original_key.sequence == original_task.sequence
                    && original_key.stable_order
                        == fixture_.boundary_owner()
                    && original_task.payload == payload_,
                "the authentic V2 row and generated frame task share their original key");

            // This test stages the authentic V2 row directly rather than
            // driving Interpreter::start. Bind that row to the receipt for
            // this real queued fallback envelope so production's descriptor
            // checks see the exact scheduler-authored slot key.
            frame.slot.time = receipt_.time;
            frame.slot.delta = receipt_.delta;
            frame.slot.systemverilog_round
                = receipt_.systemverilog_round;
            frame.slot.process_domain = static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog);
            frame.slot.phase = static_cast<std::uint32_t>(receipt_.phase);
            write.commit_key = RegionFrontierKeyV2 {
                receipt_.time,
                receipt_.delta,
                receipt_.systemverilog_round,
                receipt_.stable_order,
                receipt_.sequence,
                static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog),
                static_cast<std::uint32_t>(receipt_.phase),
            };
            runtime.scheduler_tasks.at(frame.scheduler_task_cursor)
                = RegionFrontierSchedulerTaskV2 {
                    receipt_.stable_order,
                    receipt_.sequence,
                    payload_,
                };
            authenticated_ = true;

            // This adapter declines before checked publication, as the real
            // runtime does before invoking an ordinary fallback task.
            fixture_.lease().release();
            released_ = !fixture_.lease_active();
            return { };
        }

    private:
        NativeBoundaryFixture& fixture_;
        SchedulerSystemVerilogKeyReceipt& receipt_;
        std::uint32_t slot_ { };
        std::uint64_t payload_ { };
        std::size_t& calls_;
        bool& authenticated_;
        bool& released_;
    } envelope { native, receipt,
        static_cast<std::uint32_t>(pending_slot), pending_task.payload,
        batch_calls, frontier_authenticated, lease_released };

    std::size_t fallback_calls { };
    std::size_t descriptor_calls { };
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active,
        static_cast<StableOrder>(native.boundary_owner()), envelope,
        pending_task.payload,
        [&](Scheduler& active_scheduler) {
            ++fallback_calls;
            require(frontier_authenticated && lease_released
                    && !active_scheduler.current_batch_frontier().has_value()
                    && active_scheduler.now() == receipt.time
                    && active_scheduler.delta() == receipt.delta
                    && active_scheduler.systemverilog_round()
                        == receipt.systemverilog_round
                    && active_scheduler.current_phase()
                        == std::optional { receipt.phase }
                    && !native.lease_active(),
                "the scheduler clears the borrowed frontier before checked fallback");
            const auto descriptor
                = runtime.make_fallback_descriptor(pending_task.payload);
            require(descriptor.invoke != nullptr,
                "authentic pending proxy-slice output yields its production descriptor");
            ++descriptor_calls;
            descriptor(active_scheduler);
        },
        { }, &receipt);
    require(receipt.valid && receipt.phase == SchedulerPhase::active
            && receipt.stable_order == native.boundary_owner(),
        "enqueue returns a scheduler-issued Active receipt for the boundary owner");

    const auto run = scheduler.run();
    require(run.status == RunStatus::completed
            && run.callbacks_executed == 1U
            && batch_calls == 1U && fallback_calls == 1U
            && descriptor_calls == 1U && frontier_authenticated
            && lease_released,
        "one scheduler batch decline invokes one ordinary fallback descriptor");

    const auto family_after = capture_alias_family_state(native);
    NativeBoundaryFixture checked {
        width, provider, false, false, true, false, true };
    auto& checked_implementation = checked.implementation();
    auto& checked_scheduler = checked_implementation.scheduler;
    checked.lease().release();
    std::size_t checked_publications { };
    checked_scheduler.schedule_systemverilog(SchedulerPhase::active,
        static_cast<StableOrder>(consumer_process_id),
        [&](Scheduler& active_scheduler) {
            require(active_scheduler.now() == receipt.time
                    && active_scheduler.delta() == receipt.delta
                    && active_scheduler.systemverilog_round()
                        == receipt.systemverilog_round
                    && active_scheduler.current_phase()
                        == std::optional { receipt.phase },
                "the checked reference runs in the same scheduler-authored Active slot");
            checked_implementation.commit_driver_slice(
                consumer_process_id, checked.boundary_proxy_signal(),
                pending_value, width, origin, false);
            ++checked_publications;
        });
    const auto checked_run = checked_scheduler.run();
    require(checked_run.status == RunStatus::completed
            && checked_run.callbacks_executed == 1U
            && checked_publications == 1U,
        "the checked proxy-slice reference publishes once in the matching Active round");
    const auto checked_family = capture_alias_family_state(checked);
    const auto& committed = runtime.pending_writes.at(pending_slot);
    const auto repeated_descriptor
        = runtime.make_fallback_descriptor(pending_task.payload);
    require(runtime.invalidated && !native.lease_active()
            && native.native_dispatches() == dispatches
            && native.task_cursor() == cursor
            && runtime.frame.pending_write_count == 0U
            && runtime.frame.current_pending_write == UINT32_MAX
            && (committed.flags & pending_active) == 0U
            && (committed.flags & pending_committed) != 0U
            && committed.commit_key.time == receipt.time
            && committed.commit_key.delta == receipt.delta
            && committed.commit_key.systemverilog_round
                == receipt.systemverilog_round
            && committed.commit_key.stable_order == receipt.stable_order
            && committed.commit_key.sequence == receipt.sequence
            && repeated_descriptor.invoke == nullptr
            && same_alias_family_state(family_after, checked_family),
        "checked fallback publishes the original proxy slice once and retires its pending key without replay");
    require(family_after.signals[0U].current == pending_value
            && family_after.signals[0U].last == family_before.signals[0U].current
            && family_after.signals[0U].stored == pending_value
            && family_after.signals[0U].raw_driver_count == 1U
            && family_after.signals[0U].boundary_owner_value
                == std::optional { pending_value }
            && family_after.signals[1U].current
                == family_before.signals[1U].current
            && family_after.signals[1U].stored
                == family_before.signals[1U].stored
            && family_after.signals[1U].raw_driver_count
                == family_before.signals[1U].raw_driver_count
            && family_after.object_elements.size() == 2U
            && family_after.object_elements
                == family_before.object_elements
            && family_after.signals[2U].current
                == PackedLogic4::from_msb_string("10")
            && family_after.signals[0U].transaction
                != family_before.signals[0U].transaction,
        "the fallback changes only the selected first leaf and its aggregate projection");
    const auto& native_projection = native.interpreter().container_object_value(
        native.boundary_alias_object());
    const auto& checked_projection
        = checked.interpreter().container_object_value(
            checked.boundary_alias_object());
    require(native_projection.elements.size() == 2U
            && native_projection.elements[0U] == pending_value
            && native_projection.elements[1U]
                == family_before.signals[1U].current
            && checked_projection.elements.size() == 2U
            && checked_projection.elements[0U] == pending_value
            && checked_projection.elements[1U]
                == family_before.signals[1U].current,
        "public aggregate projections show the selected leaf and unchanged sibling");
}

void check_wide_materialization_failure_is_terminal(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    constexpr auto width = 129U;
    NativeBoundaryFixture fixture { width, provider };
    const auto original_owner_before = fixture.original_owner_value();
    const auto public_boundary_before = fixture.public_boundary_value();
    const auto boundary_revision_before
        = fixture.implementation().signal_value_revisions.at(
            fixture.boundary_signal());
    const auto boundary_transaction_before
        = fixture.implementation().signal_transactions.at(
            fixture.boundary_signal());
    require(fixture.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the real wide JIT body stages its output before host materialization");
    const auto& staged_write = fixture.runtime().pending_writes.at(
        fixture.boundary_pending_slot());
    require(staged_write.value_kind == RegionFrontierValueKindV2::logic4
            && staged_write.width == width && staged_write.word_count == 3U
            && staged_write.plane_count == kRegionFrontierLogic4PlaneCountV2
            && staged_write.value_planes[0U] != nullptr
            && staged_write.value_planes[1U] != nullptr
            && staged_write.value_planes[2U] == nullptr
            && staged_write.value_planes[3U] == nullptr
            && staged_write.value_planes[0U][0U] == UINT64_MAX
            && staged_write.value_planes[0U][1U] == UINT64_MAX
            && staged_write.value_planes[0U][2U] == 1U
            && staged_write.value_planes[1U][0U] == 0U
            && staged_write.value_planes[1U][1U] == 0U
            && staged_write.value_planes[1U][2U] == 0U
            && original_owner_before != PackedLogic4 { width, Logic4::one }
            && public_boundary_before != PackedLogic4 { width, Logic4::one }
            && fixture.original_owner_value() == original_owner_before
            && fixture.public_boundary_value() == public_boundary_before,
        "the staged wide one differs from both uncommitted boundary values");
    const auto task = fixture.boundary_task();
    const auto cursor = fixture.task_cursor();
    const auto flags = fixture.pending_flags();
    const auto dispatches = fixture.native_dispatches();
    bool threw_bad_alloc { };
    fsim::tests::runtime::staging_failure_support::
        arm_allocation_failure(0U);
    try {
        fixture.runtime().publish_boundary_commit(task,
            static_cast<std::uint32_t>(fixture.boundary_pending_slot()),
            fixture.lease());
    } catch (const std::bad_alloc&) {
        threw_bad_alloc = true;
    } catch (...) {
        fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure();
        throw;
    }
    const auto injected = fsim::tests::runtime::staging_failure_support::
        allocation_failure_was_injected();
    fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
    require(threw_bad_alloc && injected
            && fixture.runtime().invalidated
            && fixture.lease_active()
            && fixture.task_cursor() == cursor
            && fixture.native_dispatches() == dispatches
            && fixture.pending_flags() == flags
            && (flags & pending_committed) == 0U
            && fixture.original_owner_value()
                == original_owner_before
            && fixture.public_boundary_value()
                == public_boundary_before
            && fixture.implementation().signal_value_revisions.at(
                fixture.boundary_signal()) == boundary_revision_before
            && fixture.implementation().signal_transactions.at(
                fixture.boundary_signal()) == boundary_transaction_before,
        "wide owner-value allocation failure preserves the unacked task and forbids replay");
}

void check_checked_publication_exception_is_terminal(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { 1U, provider };
    std::size_t original_owner_writes { };
    ProcessId observed_owner = UINT32_MAX;
    std::size_t visible_publications { };
    fixture.interpreter().set_driver_change_hook(
        [&](const ProcessId process, const SignalId signal, SimulationTick) {
            if (signal != fixture.boundary_signal()) {
                return;
            }
            ++original_owner_writes;
            observed_owner = process;
        });
    fixture.interpreter().set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4&, SimulationTick) {
            if (signal != fixture.boundary_signal()) {
                return;
            }
            ++visible_publications;
            throw std::runtime_error { "checked frontier observer failure" };
        });
    require(fixture.reach_boundary_publication()
                == RegionFrontierStatusV2::boundary_publication,
        "the actual JIT consumer stages before the checked callback is armed");

    const auto task = fixture.boundary_task();
    const auto cursor = fixture.task_cursor();
    const auto flags = fixture.pending_flags();
    const auto dispatches = fixture.native_dispatches();
    bool propagated { };
    try {
        fixture.runtime().publish_boundary_commit(task,
            static_cast<std::uint32_t>(fixture.boundary_pending_slot()),
            fixture.lease());
    } catch (const std::runtime_error& error) {
        propagated = std::string_view { error.what() }
            == "checked frontier observer failure";
    }
    require(propagated && fixture.runtime().invalidated
            && !fixture.lease_active()
            && fixture.task_cursor() == cursor
            && fixture.native_dispatches() == dispatches
            && fixture.pending_flags() == flags
            && (flags & pending_committed) == 0U
            && original_owner_writes == 1U
            && observed_owner == fixture.boundary_owner()
            && visible_publications == 1U
            && fixture.original_owner_value()
                == PackedLogic4 { 1U, Logic4::one }
            && fixture.public_boundary_value()
                == PackedLogic4 { 1U, Logic4::one },
        "checked publication exceptions propagate after the owner side effect "
        "without ACK or replay");
}

struct InternalRangeReadinessRun final {
    std::vector<std::array<AliasSignalState, 3U>> frames;
    std::array<std::uint64_t, 4U> native_dispatch_deltas { };
    std::array<std::uint64_t, 4U> alias_checked_entry_deltas { };
    std::array<std::uint64_t, 4U> alias_trusted_entry_deltas { };
    std::array<std::uint64_t, 4U> alias_sorted_proof_attempt_deltas { };
    std::array<std::uint64_t, 4U> alias_sorted_proof_success_deltas { };
    std::array<std::uint64_t, 4U> alias_sorted_proof_failure_deltas { };
    bool checked_programs_absent { };
    bool v2_runtime_prepared { };
};

struct AliasGeometryReferenceRange final {
    std::uint64_t address { };
    std::size_t bytes { };
    std::uint64_t alias_tag { };
};

struct AliasGeometryCase final {
    std::string_view name;
    std::array<AliasGeometryReferenceRange, 13U> frames { };
    std::vector<AliasGeometryReferenceRange> data;
    bool expected_emitter_acceptance { };
    bool expected_host_acceptance { };
    bool overlap_scratch { };
};

[[nodiscard]] bool alias_geometry_reference_end(
    const AliasGeometryReferenceRange& range,
    std::uint64_t& end) noexcept
{
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        return false;
    }
    if (range.bytes != 0U && range.address == 0U) {
        return false;
    }
    const auto bytes = static_cast<std::uint64_t>(range.bytes);
    if (bytes > std::numeric_limits<std::uint64_t>::max() - range.address) {
        return false;
    }
    end = range.address + bytes;
    return true;
}

[[nodiscard]] bool alias_geometry_reference_disjoint(
    const AliasGeometryReferenceRange& left,
    const AliasGeometryReferenceRange& right) noexcept
{
    std::uint64_t left_end { };
    std::uint64_t right_end { };
    if (!alias_geometry_reference_end(left, left_end)
        || !alias_geometry_reference_end(right, right_end)) {
        return false;
    }
    // A zero-byte frame interval is a point: it conflicts only when strictly
    // inside a positive interval, and is disjoint at either endpoint.
    return left_end <= right.address || right_end <= left.address;
}

[[nodiscard]] bool alias_geometry_reference_emitter_pair_alias(
    const AliasGeometryReferenceRange& left,
    const AliasGeometryReferenceRange& right) noexcept
{
    const auto left_family = left.alias_tag >> 1U;
    const auto right_family = right.alias_tag >> 1U;
    return left.address == right.address
        && left_family != 0U
        && left_family == right_family
        && (left.alias_tag & 1U) != (right.alias_tag & 1U);
}

[[nodiscard]] bool alias_geometry_reference_host_pair_alias(
    const AliasGeometryReferenceRange& left,
    const AliasGeometryReferenceRange& right) noexcept
{
    std::uint64_t left_end { };
    std::uint64_t right_end { };
    return alias_geometry_reference_end(left, left_end)
        && alias_geometry_reference_end(right, right_end)
        && left.address == right.address
        && left_end == right_end
        && left.bytes == right.bytes
        && alias_geometry_reference_emitter_pair_alias(left, right);
}

[[nodiscard]] bool alias_geometry_reference_emitter_accepts(
    const AliasGeometryCase& geometry) noexcept
{
    for (std::size_t left = 0U; left < geometry.frames.size(); ++left) {
        std::uint64_t end { };
        if (!alias_geometry_reference_end(geometry.frames[left], end)) {
            return false;
        }
        for (std::size_t right = left + 1U;
             right < geometry.frames.size(); ++right) {
            if (!alias_geometry_reference_disjoint(
                    geometry.frames[left], geometry.frames[right])) {
                return false;
            }
        }
    }
    for (const auto& range : geometry.data) {
        std::uint64_t end { };
        if (!alias_geometry_reference_end(range, end)) {
            return false;
        }
        for (const auto& frame : geometry.frames) {
            if (!alias_geometry_reference_disjoint(frame, range)) {
                return false;
            }
        }
    }
    for (std::size_t left = 0U; left < geometry.data.size(); ++left) {
        for (std::size_t right = left + 1U;
             right < geometry.data.size(); ++right) {
            if (!alias_geometry_reference_disjoint(
                    geometry.data[left], geometry.data[right])
                && !alias_geometry_reference_emitter_pair_alias(
                    geometry.data[left], geometry.data[right])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool alias_geometry_reference_host_accepts(
    const AliasGeometryCase& geometry,
    const AliasGeometryReferenceRange& scratch) noexcept
{
    if (geometry.frames.size() != 13U || geometry.data.empty()) {
        return false;
    }
    std::uint64_t end { };
    if (!alias_geometry_reference_end(scratch, end)) {
        return false;
    }
    for (const auto& frame : geometry.frames) {
        if (!alias_geometry_reference_end(frame, end)
            || !alias_geometry_reference_disjoint(scratch, frame)) {
            return false;
        }
    }
    for (const auto& data : geometry.data) {
        if (data.bytes == 0U
            || !alias_geometry_reference_end(data, end)
            || !alias_geometry_reference_disjoint(scratch, data)) {
            return false;
        }
        for (const auto& frame : geometry.frames) {
            if (!alias_geometry_reference_disjoint(frame, data)) {
                return false;
            }
        }
    }
    for (std::size_t left = 0U; left < geometry.data.size(); ++left) {
        for (std::size_t right = left + 1U;
             right < geometry.data.size(); ++right) {
            if (!alias_geometry_reference_disjoint(
                    geometry.data[left], geometry.data[right])
                && !alias_geometry_reference_host_pair_alias(
                    geometry.data[left], geometry.data[right])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] const void* alias_geometry_opaque_address(
    const std::uint64_t address) noexcept
{
    return reinterpret_cast<const void*>(
        static_cast<std::uintptr_t>(address));
}

[[nodiscard]] AliasGeometryCase make_alias_geometry_case(
    const std::string_view name,
    const std::uint64_t origin,
    const std::vector<AliasGeometryReferenceRange>& data,
    const bool expected_emitter_acceptance,
    const bool expected_host_acceptance)
{
    AliasGeometryCase geometry;
    geometry.name = name;
    geometry.data = data;
    geometry.expected_emitter_acceptance = expected_emitter_acceptance;
    geometry.expected_host_acceptance = expected_host_acceptance;
    for (std::size_t index = 0U; index < geometry.frames.size(); ++index) {
        geometry.frames[index] = {
            origin + 0x100U + static_cast<std::uint64_t>(index) * 0x100U,
            16U,
            0U,
        };
    }
    return geometry;
}

[[nodiscard]] bool alias_geometry_test_proof(
    FrontierRuntime& runtime,
    const AliasGeometryCase& geometry)
{
    constexpr std::size_t frame_range_count = 13U;
    constexpr std::size_t untouched_index = 0x5a5aU;
    runtime.alias_sorted_indices.resize(geometry.data.size());
    std::ranges::fill(runtime.alias_sorted_indices, untouched_index);

    const auto scratch_address = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(runtime.alias_sorted_indices.data()));
    const auto scratch_bytes
        = runtime.alias_sorted_indices.size() * sizeof(std::size_t);
    const AliasGeometryReferenceRange scratch {
        scratch_address, scratch_bytes, 0U };
    auto input_geometry = geometry;
    if (input_geometry.overlap_scratch) {
        require(!input_geometry.data.empty(),
            "the scratch-overlap case has one positive data range");
        input_geometry.data[0U].address = scratch_address;
        input_geometry.data[0U].bytes = sizeof(std::size_t);
    }

    const auto emitter_accepts
        = alias_geometry_reference_emitter_accepts(input_geometry);
    const auto host_accepts
        = alias_geometry_reference_host_accepts(input_geometry, scratch);
    require(emitter_accepts == input_geometry.expected_emitter_acceptance
            && host_accepts == input_geometry.expected_host_acceptance,
        std::string { input_geometry.name }
            + ": the quadratic oracle retains the specified pairwise contract");

    runtime.alias_candidate_ranges.clear();
    runtime.alias_candidate_ranges.reserve(
        frame_range_count + input_geometry.data.size());
    for (const auto& range : input_geometry.frames) {
        runtime.alias_candidate_ranges.push_back({
            alias_geometry_opaque_address(range.address),
            range.bytes,
            range.alias_tag,
        });
    }
    for (const auto& range : input_geometry.data) {
        runtime.alias_candidate_ranges.push_back({
            alias_geometry_opaque_address(range.address),
            range.bytes,
            range.alias_tag,
        });
    }
    runtime.alias_certificate_ranges = runtime.alias_candidate_ranges;
    runtime.alias_certificate_storage_available = true;
    runtime.alias_certificate_pending_confirmation = true;
    runtime.alias_certificate_pending_task_count = 0U;
    runtime.frame.scheduler_task_count = 0U;
    runtime.frame.staged_event_count = 0U;

    const auto candidate_before = runtime.alias_candidate_ranges;
    const auto certificate_before = runtime.alias_certificate_ranges;
    const auto attempts_before = runtime.alias_sorted_proof_attempts;
    const auto successes_before = runtime.alias_sorted_proof_successes;
    const auto failures_before = runtime.alias_sorted_proof_failures;
    const auto accepted = runtime.prove_alias_geometry_sorted();
    require(runtime.alias_sorted_proof_attempts == attempts_before + 1U
            && runtime.alias_sorted_proof_successes
                == successes_before + (host_accepts ? 1U : 0U)
            && runtime.alias_sorted_proof_failures
                == failures_before + (host_accepts ? 0U : 1U)
            && runtime.alias_candidate_ranges == candidate_before
            && runtime.alias_certificate_ranges == certificate_before,
        std::string { input_geometry.name }
            + ": proof changes only counters and its private index scratch");
    if (input_geometry.overlap_scratch) {
        require(std::ranges::all_of(runtime.alias_sorted_indices,
                    [](const std::size_t index) {
                        return index == untouched_index;
                    }),
            "scratch overlap is rejected before the first index write");
    }
    return accepted;
}

void check_alias_sorted_geometry_oracle()
{
    static_assert(std::numeric_limits<std::uintptr_t>::digits <= 64U);
    FrontierRuntime runtime;
    runtime.alias_sorted_indices.reserve(4U);
    runtime.alias_sorted_indices.resize(1U);
    const auto scratch_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(runtime.alias_sorted_indices.data()));
    constexpr std::uint64_t spacing = UINT64_C(0x100000);
    const auto origin = scratch_begin > spacing
        ? scratch_begin - spacing : scratch_begin + spacing;
    const auto data_begin = origin + UINT64_C(0x10000);

    std::vector<AliasGeometryCase> cases;
    const auto add = [&](AliasGeometryCase geometry) {
        cases.push_back(std::move(geometry));
    };
    add(make_alias_geometry_case("adjacent frame/data endpoint", origin,
        { { data_begin, 8U, 0U } }, true, true));
    cases.back().frames[0U] = { data_begin + 8U, 8U, 0U };
    add(make_alias_geometry_case("zero frame point at data start", origin,
        { { data_begin, 8U, 0U } }, true, true));
    cases.back().frames[0U] = { data_begin, 0U, 0U };
    add(make_alias_geometry_case("zero frame point at data end", origin,
        { { data_begin, 8U, 0U } }, true, true));
    cases.back().frames[0U] = { data_begin + 8U, 0U, 0U };
    add(make_alias_geometry_case("zero frame point inside data", origin,
        { { data_begin, 8U, 0U } }, false, false));
    cases.back().frames[0U] = { data_begin + 4U, 0U, 0U };
    add(make_alias_geometry_case("permuted disjoint ranges with gaps", origin,
        { { data_begin + 0x2000U, 8U, 0U },
          { data_begin, 8U, 0U },
          { data_begin + 0x1000U, 8U, 0U } }, true, true));
    add(make_alias_geometry_case("exact stored/owner alias", origin,
        { { data_begin, 8U, 2U }, { data_begin, 8U, 3U } }, true, true));
    add(make_alias_geometry_case("same-role alias is rejected", origin,
        { { data_begin, 8U, 2U }, { data_begin, 8U, 2U } }, false, false));
    add(make_alias_geometry_case("different alias keys are rejected", origin,
        { { data_begin, 8U, 2U }, { data_begin, 8U, 5U } }, false, false));
    add(make_alias_geometry_case("different-start overlap is rejected", origin,
        { { data_begin, 8U, 2U }, { data_begin + 4U, 8U, 3U } },
        false, false));
    add(make_alias_geometry_case("emitter alias can conservatively fall back",
        origin, { { data_begin, 8U, 2U }, { data_begin, 4U, 3U } },
        true, false));
    add(make_alias_geometry_case("third overlap defeats alias exception", origin,
        { { data_begin, 8U, 2U }, { data_begin, 8U, 3U },
          { data_begin + 4U, 4U, 0U } }, false, false));
    add(make_alias_geometry_case("data/frame overlap ignores alias tag", origin,
        { { data_begin, 8U, 2U } }, false, false));
    cases.back().frames[0U] = { data_begin, 8U, 0U };
    add(make_alias_geometry_case("host proof leaves frame/frame checks to entry",
        origin, { { data_begin + 0x10000U, 8U, 0U } }, false, true));
    cases.back().frames[0U] = { origin + 0x100U, 16U, 0U };
    cases.back().frames[1U] = { origin + 0x108U, 16U, 0U };
    add(make_alias_geometry_case("positive-null frame range", origin,
        { { data_begin, 8U, 0U } }, false, false));
    cases.back().frames[0U] = { 0U, 8U, 0U };
    add(make_alias_geometry_case("positive-null data range", origin,
        { { 0U, 8U, 0U } }, false, false));
    add(make_alias_geometry_case("null zero-byte frame point", origin,
        { { data_begin, 8U, 0U } }, true, true));
    cases.back().frames[0U] = { 0U, 0U, 0U };
    add(make_alias_geometry_case("empty data declines host proof", origin,
        { }, true, false));
    add(make_alias_geometry_case("scratch range overlaps data", origin,
        { { data_begin, 8U, 0U } }, true, false));
    cases.back().overlap_scratch = true;
    if constexpr (std::numeric_limits<std::uintptr_t>::digits == 64U) {
        add(make_alias_geometry_case("uint64 interval end wraps", origin,
            { { std::numeric_limits<std::uint64_t>::max() - 3U, 8U, 0U } },
            false, false));
    }

    for (const auto& geometry : cases) {
        runtime.alias_candidate_ranges.clear();
        runtime.alias_certificate_ranges.clear();
        const auto minimum_index_count
            = std::max<std::size_t>(geometry.data.size(), 1U);
        runtime.alias_sorted_indices.resize(minimum_index_count);
        const auto accepted = alias_geometry_test_proof(runtime, geometry);
        require(accepted == geometry.expected_host_acceptance,
            std::string { geometry.name }
                + ": production host proof matches the quadratic oracle");
    }
}

[[nodiscard]] PackedLogic4 make_internal_range_input(
    const Logic4 first,
    const Logic4 second)
{
    PackedLogic4 value { 8U, Logic4::zero };
    value.set(4U, Logic4::one);
    value.set(1U, first);
    value.set(6U, second);
    return value;
}

[[nodiscard]] InternalRangeReadinessRun run_internal_range_readiness_case(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit* const process_jit,
    const bool exercise_debug_token = false)
{
    constexpr SignalId input_id = 0U;
    constexpr SignalId internal_id = 1U;
    constexpr SignalId output_id = 2U;
    constexpr ProcessId producer_id = 0U;
    constexpr ProcessId reader_id = 1U;
    constexpr ProcessId clock_id = 2U;

    const std::array<std::uint32_t, 3U> signal_widths { 8U, 8U, 8U };
    const std::array<ValueKind, 3U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    const std::array<ResolutionKind, 3U> signal_resolutions {
        ResolutionKind::sv_wire, ResolutionKind::sv_wire,
        ResolutionKind::sv_wire };
    // ProcessProgramView and the executor retain views into these originals.
    std::array<std::optional<Process>, 2U> registered_processes;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "range_v2.input",
        PackedLogic4 { 8U, Logic4::zero }, ResolutionKind::sv_wire });
    const auto internal = interpreter.add_signal({ "range_v2.internal",
        PackedLogic4 { 8U, Logic4::zero }, ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "range_v2.output",
        PackedLogic4 { 8U, Logic4::zero }, ResolutionKind::sv_wire });
    require(input == input_id && internal == internal_id
            && output == output_id,
        "the runtime range witness retains dense signal identities");

    const SourceLocation debug_entry_source { "range_v2_debug.sv", 3U, 1U };
    const InternedString debug_entry_scope { "top.range_v2.producer.entry" };
    const SourceLocation debug_final_source { "range_v2_debug.sv", 10U, 3U };
    const InternedString debug_final_scope { "top.range_v2.producer.final" };

    Process producer;
    producer.id = producer_id;
    producer.name = exercise_debug_token
        ? "range_v2_debug_producer" : "range_v2_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { input, EdgeKind::any } };
    producer.driver_regions = { { internal, 0U, 0U, true } };
    if (exercise_debug_token) {
        producer.operations.emplace_back(DebugPoint {
            DebugPointKind::statement, debug_entry_source, debug_entry_scope });
    }
    producer.operations.emplace_back(ReadSignal { 0U, input });
    producer.operations.emplace_back(WriteUpdate { internal, 0U,
        SignalUpdateDomain::systemverilog_active });
    if (exercise_debug_token) {
        producer.operations.emplace_back(DebugPoint {
            DebugPointKind::statement, debug_final_source, debug_final_scope });
    }
    producer.operations.emplace_back(WaitSensitivity { });
    producer.operations.emplace_back(Jump { 0U });
    const auto add_process = [&](Process process) {
        const auto id = process.id;
        require(id < registered_processes.size(),
            "the native range witness retains only its two V2 members");
        auto& registered_process
            = registered_processes[id].emplace(std::move(process));
        const auto name = registered_process.name;
        require(interpreter.add_process(registered_process) == id,
            "each ranged V2 process retains its dense registration ID");
        if (process_jit == nullptr) {
            return;
        }

        process_jit->add_process(name, registered_process,
            signal_widths, signal_kinds);
        const auto handle = process_jit->lookup(name);
        require(static_cast<bool>(handle),
            "LLVM retains each process needed by the native V2 cohort");
        auto executor = std::make_unique<
            fsim::app::application_detail::LlvmProcessExecutor>(
                *process_jit, handle, registered_process,
                signal_widths, signal_kinds, signal_resolutions,
                std::shared_ptr<const ProcessSignalRemap> { }, id);
        const auto* const binding = executor->program_access_binding();
        require(binding != nullptr && binding->valid()
                && executor->region_kernel_equivalent()
                && executor->cohort_manages_process_state()
                && executor->region_kernel_completion_has_no_persistent_registers(),
            "the actual member executor supplies the native cohort contract");
        interpreter.set_process_executor(id, std::move(executor));
    };

    add_process(std::move(producer));

    Process reader;
    reader.id = reader_id;
    reader.name = "range_v2_reader";
    reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    reader.register_count = 1U;
    reader.register_value_kinds = { ValueKind::logic4 };
    reader.static_sensitivity = {
        { internal, EdgeKind::any, 1U, 1U },
        { internal, EdgeKind::any, 6U, 1U },
    };
    reader.driver_regions = { { output, 0U, 0U, true } };
    reader.operations = {
        ReadSignal { 0U, internal },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    add_process(std::move(reader));

    const std::array<PackedLogic4, 4U> inputs {
        make_internal_range_input(Logic4::zero, Logic4::zero),
        make_internal_range_input(Logic4::x, Logic4::zero),
        make_internal_range_input(Logic4::x, Logic4::z),
        make_internal_range_input(Logic4::z, Logic4::x),
    };
    Process clock;
    clock.id = clock_id;
    clock.name = "range_v2_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.register_value_kinds = { ValueKind::logic4 };
    clock.driver_regions = { { input, 0U, 0U, true } };
    // An undriven SV wire starts at Z. Settle zero at tick zero so the first
    // timed update changes only the unrelated bit.
    clock.operations.emplace_back(LoadConstant {
        0U, PackedLogic4 { 8U, Logic4::zero } });
    clock.operations.emplace_back(WriteBlocking { input, 0U });
    for (const auto& value : inputs) {
        clock.operations.emplace_back(WaitFor { 1U });
        clock.operations.emplace_back(LoadConstant { 0U, value });
        clock.operations.emplace_back(WriteBlocking { input, 0U });
    }
    clock.operations.emplace_back(Halt { });
    require(interpreter.add_process(std::move(clock)) == clock_id,
        "the clock keeps stimulus outside the private producer-reader component");

    if (provider) {
        interpreter.set_region_kernel_backend_provider(provider);
    }
    interpreter.start();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    InternalRangeReadinessRun result;
    // Retained plane-backed copies pin the live A4 write lease. Capture owned
    // bits so earlier comparison frames cannot disable subsequent V2 writes.
    const auto detached_value = [](const PackedLogic4& value) {
        PackedLogic4 detached { value.width(), Logic4::zero };
        for (std::size_t bit = 0U; bit < value.width(); ++bit) {
            detached.set(bit, value.get(bit));
        }
        return detached;
    };
    const auto detached_signal_state
        = [&](const SignalId signal, const ProcessId owner) {
              auto state = capture_boundary_signal_state(
                  implementation, signal, owner);
              state.current = detached_value(state.current);
              state.last = detached_value(state.last);
              state.stored = detached_value(state.stored);
              if (state.boundary_owner_value) {
                  state.boundary_owner_value
                      = detached_value(*state.boundary_owner_value);
              }
              return state;
          };
    const auto capture = [&]() {
        return std::array<AliasSignalState, 3U> {
            detached_signal_state(input, clock_id),
            detached_signal_state(internal, producer_id),
            detached_signal_state(output, reader_id),
        };
    };

    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::time_limit
            || startup.status == RunStatus::completed,
        "normal startup settles the native and checked static-wait frames");
    require(implementation.processes.at(producer_id).waiting_on_static
            && implementation.processes.at(reader_id).waiting_on_static
            && implementation.processes.at(producer_id).status
                == ProcessStatus::waiting
            && implementation.processes.at(reader_id).status
                == ProcessStatus::waiting,
        "startup leaves both owner and ranged reader at their real static wait");
    result.frames.push_back(capture());

    std::shared_ptr<FrontierRuntime> frontier_runtime;
    std::size_t component = std::numeric_limits<std::size_t>::max();
    if (provider) {
        component = implementation.region_component_by_process.at(producer_id);
        require(component != std::numeric_limits<std::size_t>::max()
                && implementation.region_component_by_process.at(reader_id)
                    == component
                && component
                    < implementation.region_activation_programs.size()
                && implementation.region_activation_programs[component],
            "the writer and ranged reader share a certified activation program");
        frontier_runtime
            = implementation.region_frontier_runtime_by_component.at(component);
        require(frontier_runtime && frontier_runtime->backend
                && frontier_runtime->backend->executor
                && frontier_runtime->backend->executor->step_entry() != nullptr
                && !implementation.region_cone_forwarding_backends_by_component.at(
                    component),
            "the runtime witness owns an authentic LLVM V2 entry");
        require(frontier_runtime->backend->kernel.members.size() == 2U
                && frontier_runtime->backend->kernel.members[0U].process
                    == producer_id
                && frontier_runtime->backend->kernel.members[1U].process
                    == reader_id
                && std::ranges::find(
                       frontier_runtime->backend->kernel.internal_signals,
                       internal) != frontier_runtime->backend->kernel.internal_signals.end(),
            "the V2 entry contains the private owner, reader, and full signal plane");

        const auto& local_wave_state
            = implementation.region_local_wave_state_by_component.at(component);
        const auto& activation_program
            = implementation.region_activation_programs.at(component);
        const auto& frontier_entry
            = implementation.region_frontier_backends_by_component.at(component);
        require(local_wave_state != nullptr && activation_program.has_value()
                && frontier_entry == frontier_runtime->backend,
            "the authentic component retains both indexed kernel owners");
        const auto& activation_kernel = activation_program->activation_kernel;
        const auto& frontier_kernel = frontier_entry->kernel;
        require(local_wave_state->generation
                    == implementation.region_runtime_generation
                && local_wave_state->output_index_ready
                && local_wave_state->output_index_generation
                    == local_wave_state->generation
                && local_wave_state->internal_output_indices.size()
                    == activation_kernel.internal_signals.size()
                && local_wave_state->internal_output_indices.size()
                    == frontier_kernel.internal_signals.size()
                && local_wave_state->activation_kernel_identity.matches(
                    activation_kernel)
                && local_wave_state->frontier_kernel_identity.matches(
                    frontier_kernel)
                && implementation.region_local_wave_component_eligible(
                    component, activation_kernel)
                && implementation.region_local_wave_component_eligible(
                    component, frontier_kernel),
            "the prepared internal-output index is current and selected "
            "for both authentic kernels");

        auto duplicate_output_kernel = frontier_kernel;
        const auto indexed_internal_signal
            = duplicate_output_kernel.internal_signals.front();
        const auto original_output = std::ranges::find(
            duplicate_output_kernel.outputs, indexed_internal_signal,
            &RegionConeOutputBinding::signal);
        require(original_output != duplicate_output_kernel.outputs.end(),
            "the copied-kernel fallback witness starts from a real internal output");
        const auto duplicate_output = *original_output;
        duplicate_output_kernel.outputs.push_back(duplicate_output);
        require(!implementation.region_local_wave_component_eligible(
                    component, duplicate_output_kernel),
            "an unregistered kernel copy uses the uniqueness scan and "
            "rejects a duplicate internal output");

        const auto& fanout = implementation.region_grouped_fanout_by_signal.at(
            internal);
        require(fanout.generation == implementation.region_runtime_generation
                && fanout.group_count == 1U
                && fanout.group_offset
                    < implementation.region_grouped_fanout_groups.size(),
            "the ranged reader has one generation-bound grouped fanout row");
        const auto& group = implementation.region_grouped_fanout_groups.at(
            fanout.group_offset);
        require(group.component == component && group.member_count == 1U
                && group.member_offset
                    < implementation.region_grouped_fanout_members.size(),
            "the private signal maps only to the selected ranged reader");
        const auto& binding = implementation.region_grouped_fanout_members.at(
            group.member_offset);
        require(binding.process == reader_id
                && binding.sensitivity_range_generation
                    == implementation.region_runtime_generation
                && binding.sensitivity_range_count == 2U
                && binding.sensitivity_range_offset
                    <= implementation.region_grouped_fanout_sensitivity_ranges.size()
                && binding.sensitivity_range_count
                    <= implementation.region_grouped_fanout_sensitivity_ranges.size()
                        - binding.sensitivity_range_offset,
            "the runtime binding retains both exact range clauses");
        const auto ranges = std::span {
            implementation.region_grouped_fanout_sensitivity_ranges }
            .subspan(binding.sensitivity_range_offset,
                binding.sensitivity_range_count);
        require(ranges[0U].offset == 1U && ranges[0U].width == 1U
                && ranges[1U].offset == 6U && ranges[1U].width == 1U,
            "the runtime map does not widen or merge disjoint sensitivity clauses");
        const auto& prepared
            = implementation.region_prepared_successor_by_signal.at(internal);
        require(prepared.generation == implementation.region_runtime_generation
                && prepared.component == component
                && prepared.reader_count == 1U
                && prepared.expected_mask == 1U
                && prepared.reader_offset
                    < implementation.region_prepared_successor_readers.size(),
            "the verified V2 successor map retains one exact ranged reader");
        const auto& prepared_reader
            = implementation.region_prepared_successor_readers.at(
                prepared.reader_offset);
        require(prepared_reader.process == reader_id
                && prepared_reader.sensitivity_range_generation
                    == implementation.region_runtime_generation
                && prepared_reader.sensitivity_range_count == 2U
                && prepared_reader.sensitivity_range_offset
                    == binding.sensitivity_range_offset
                && prepared_reader.static_trigger_mask
                    == binding.static_trigger_mask
                && binding.static_trigger_mask != 0U,
            "prepared-successor mapping references the same immutable range clause span");
        result.v2_runtime_prepared = true;
    } else {
        result.checked_programs_absent
            = std::ranges::none_of(
                implementation.region_activation_programs,
                [](const auto& program) { return program.has_value(); })
            && std::ranges::none_of(
                implementation.region_frontier_runtime_by_component,
                [](const auto& runtime) { return runtime != nullptr; });
        require(result.checked_programs_absent
                && !implementation.systemverilog_region_kernel_enabled,
            "the checked reference is deliberately built without region programs or providers");
    }

    const RegionConeFinalDebugState* final_debug_target { };
    InstructionIndex debug_entry_instruction { };
    if (exercise_debug_token) {
        require(frontier_runtime != nullptr,
            "the debug-token witness requires the authentic V2 runtime");
        const auto& kernel = frontier_runtime->backend->kernel;
        const auto producer_member = std::ranges::find(
            kernel.members, producer_id, &RegionConeKernelMember::process);
        require(producer_member != kernel.members.end()
                && producer_member->final_debug_state.has_value(),
            "the V2 member retains its final source and scope marker");
        final_debug_target = &*producer_member->final_debug_state;
        require(final_debug_target->source == debug_final_source
                && final_debug_target->scope == debug_final_scope.str(),
            "the immutable V2 marker is the producer's exact final DebugPoint");
        const auto entry_operation
            = registered_processes[producer_id]->operations.expanded(0U);
        const auto* const entry_point
            = operation_get_if<DebugPoint>(&entry_operation);
        require(entry_point != nullptr
                && entry_point->source == debug_entry_source
                && entry_point->scope == debug_entry_scope,
            "the checked boundary probe targets the retained entry DebugPoint");
        debug_entry_instruction = 0U;
    }

    const std::array<std::uint64_t, 4U> expected_dispatches {
        1U, 2U, 2U, 2U };
    auto previous_dispatches = frontier_runtime
        ? frontier_runtime->native_member_dispatches : 0U;
    auto previous_checked_entries = frontier_runtime
        ? frontier_runtime->alias_checked_entries : 0U;
    auto previous_trusted_entries = frontier_runtime
        ? frontier_runtime->alias_trusted_entries : 0U;
    auto previous_alias_sorted_proof_attempts = frontier_runtime
        ? frontier_runtime->alias_sorted_proof_attempts : 0U;
    auto previous_alias_sorted_proof_successes = frontier_runtime
        ? frontier_runtime->alias_sorted_proof_successes : 0U;
    auto previous_alias_sorted_proof_failures = frontier_runtime
        ? frontier_runtime->alias_sorted_proof_failures : 0U;
    bool debug_token_waiting_for_native_restore { };
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        if (frontier_runtime && index == 3U) {
            require(frontier_runtime->alias_certificate_valid
                    && !frontier_runtime->alias_certificate_ranges.empty()
                    && frontier_runtime->frame.scheduler_task_count
                        <= frontier_runtime->frame.scheduler_task_capacity
                    && frontier_runtime->frame.scheduler_task_capacity
                        < frontier_runtime->alias_certificate_task_count_valid.size(),
                "the previous stable entry leaves a certificate within "
                "the exact-count table");
            const auto context
                = frontier_runtime->alias_certificate_context;
            const auto original_task_count
                = static_cast<std::size_t>(
                    frontier_runtime->frame.scheduler_task_count);
            auto& confirmed_counts
                = frontier_runtime->alias_certificate_task_count_valid;
            require(confirmed_counts[original_task_count] != 0U
                    && frontier_runtime->alias_certificate_matches(context),
                "the authentic quiet frame matches an already-confirmed "
                "exact task count");
            require(original_task_count != 0U
                    && confirmed_counts[original_task_count] != 0U,
                "the original quiet point retains an authentic confirmed proof for its nonempty task count");

            const auto original_frame = frontier_runtime->frame;
            const auto original_cut = frontier_runtime->frame.cut;
            const auto original_task_cursor
                = frontier_runtime->frame.scheduler_task_cursor;
            require(original_task_cursor == original_task_count
                    && frontier_runtime->frame.pending_write_count == 0U
                    && frontier_runtime->frame.staged_event_count == 0U
                    && frontier_runtime->frame.committed_signal_count == 0U
                    && frontier_runtime->frame.generic_update_ack_count == 0U
                    && std::ranges::all_of(frontier_runtime->ready_words,
                        [](const std::uint64_t word) { return word == 0U; })
                    && std::ranges::all_of(frontier_runtime->members,
                        [](const RegionFrontierMemberV2& member) {
                            constexpr std::uint32_t queued_flags
                                = RegionFrontierMemberFlagsV2::queued
                                | RegionFrontierMemberFlagsV2::executing
                                | RegionFrontierMemberFlagsV2::queued_key_valid
                                | RegionFrontierMemberFlagsV2::pending_activation;
                            return (member.flags
                                        & RegionFrontierMemberFlagsV2::waiting_on_static)
                                    != 0U
                                && (member.flags & queued_flags) == 0U;
                        })
                    && implementation.processes.at(producer_id).waiting_on_static
                    && implementation.processes.at(reader_id).waiting_on_static,
                "the checked count-zero companion starts from a complete "
                "quiet prefix with no queued or staged work");

            const std::vector<RegionFrontierMemberV2> original_members {
                frontier_runtime->members.begin(),
                frontier_runtime->members.end() };
            const std::vector<RegionFrontierSchedulerTaskV2> original_tasks {
                frontier_runtime->scheduler_tasks.begin(),
                frontier_runtime->scheduler_tasks.end() };
            const std::vector<std::uint64_t> original_ready_words {
                frontier_runtime->ready_words.begin(),
                frontier_runtime->ready_words.end() };
            const auto original_dispatches
                = frontier_runtime->native_member_dispatches;

            frontier_runtime->frame.scheduler_task_count = 0U;
            frontier_runtime->frame.scheduler_task_cursor = 0U;
            frontier_runtime->frame.cut.kind
                = RegionFrontierCutKindV2::closed_prefix;
            frontier_runtime->frame.cut.scheduler_frontier_generation
                = frontier_runtime->frame.scheduler_frontier_generation;
            const auto zero_count_frame = frontier_runtime->frame;
            require(frontier_runtime->stage_alias_certificate(context),
                "the authentic empty prefix stages a certificate for exact count zero");
            const auto zero_count_status
                = frontier_runtime->backend->executor->step_entry()(
                    &frontier_runtime->frame);
            const bool zero_count_confirmed
                = frontier_runtime->confirm_alias_certificate(
                    context, zero_count_status);
            require(zero_count_status == RegionFrontierStatusV2::quiescent
                    && zero_count_confirmed
                    && frontier_runtime->alias_certificate_task_count_valid[0U]
                        != 0U
                    && frontier_runtime->alias_certificate_task_count_valid[
                        original_task_count] != 0U
                    && frontier_runtime->alias_certificate_matches(context),
                "a real checked empty-prefix execution confirms count zero "
                "without replacing the earlier proof");
            const auto same_member = [](const RegionFrontierMemberV2& left,
                                        const RegionFrontierMemberV2& right) {
                return left.process_id == right.process_id
                    && left.flags == right.flags
                    && left.static_trigger_mask == right.static_trigger_mask
                    && same_frontier_key(left.queued_key, right.queued_key)
                    && same_frontier_key(left.activation_origin,
                        right.activation_origin)
                    && same_frontier_key(left.pending_activation_origin,
                        right.pending_activation_origin);
            };
            const auto same_task = [](const RegionFrontierSchedulerTaskV2& left,
                                      const RegionFrontierSchedulerTaskV2& right) {
                return left.stable_order == right.stable_order
                    && left.sequence == right.sequence
                    && left.payload == right.payload;
            };
            require(same_frontier_frame(frontier_runtime->frame, zero_count_frame)
                    && std::ranges::equal(frontier_runtime->members,
                        original_members, same_member)
                    && std::ranges::equal(frontier_runtime->scheduler_tasks,
                        original_tasks, same_task)
                    && std::ranges::equal(frontier_runtime->ready_words,
                        original_ready_words)
                    && frontier_runtime->native_member_dispatches
                        == original_dispatches
                    && frontier_runtime->frame.pending_write_count == 0U
                    && frontier_runtime->frame.staged_event_count == 0U
                    && frontier_runtime->frame.committed_signal_count == 0U,
                "the checked empty-prefix proof leaves frame and backing state unchanged");
            frontier_runtime->frame.scheduler_task_count
                = static_cast<std::uint32_t>(original_task_count);
            frontier_runtime->frame.scheduler_task_cursor = original_task_cursor;
            frontier_runtime->frame.cut = original_cut;
            require(same_frontier_frame(frontier_runtime->frame, original_frame)
                    && frontier_runtime->alias_certificate_matches(context),
                "restoring the real frame retains the original exact-count proof");
            require(std::ranges::count_if(confirmed_counts,
                        [](const std::uint8_t valid) { return valid != 0U; })
                    >= 2,
                "two exact task counts have both been confirmed by checked execution");

            std::size_t alternate_confirmed_count = confirmed_counts.size();
            std::size_t unknown_task_count = confirmed_counts.size();
            for (std::size_t candidate = 0U;
                 candidate <= frontier_runtime->frame.scheduler_task_capacity;
                 ++candidate) {
                if (candidate == original_task_count) {
                    continue;
                }
                if (confirmed_counts[candidate] != 0U
                    && alternate_confirmed_count == confirmed_counts.size()) {
                    alternate_confirmed_count = candidate;
                }
                if (confirmed_counts[candidate] == 0U
                    && unknown_task_count == confirmed_counts.size()) {
                    unknown_task_count = candidate;
                }
            }
            require(alternate_confirmed_count < confirmed_counts.size()
                    && unknown_task_count < confirmed_counts.size(),
                "the bounded frame has both a second proved count and an unseen count");

            frontier_runtime->frame.scheduler_task_count
                = static_cast<std::uint32_t>(alternate_confirmed_count);
            frontier_runtime->frame.scheduler_task_cursor
                = std::min(original_task_cursor,
                    static_cast<std::uint32_t>(alternate_confirmed_count));
            const bool alternate_count_matches
                = frontier_runtime->alias_certificate_matches(context);
            frontier_runtime->frame.scheduler_task_count
                = static_cast<std::uint32_t>(original_task_count);
            frontier_runtime->frame.scheduler_task_cursor = original_task_cursor;
            require(alternate_count_matches
                    && frontier_runtime->alias_certificate_matches(context),
                "alternating between two genuinely confirmed counts reuses both exact proofs");

            const auto confirmed_counts_before_probe = confirmed_counts;
            frontier_runtime->frame.scheduler_task_count
                = static_cast<std::uint32_t>(unknown_task_count);
            frontier_runtime->frame.scheduler_task_cursor
                = std::min(original_task_cursor,
                    static_cast<std::uint32_t>(unknown_task_count));
            const bool unknown_count_matches
                = frontier_runtime->alias_certificate_matches(context);
            frontier_runtime->frame.scheduler_task_count
                = static_cast<std::uint32_t>(original_task_count);
            frontier_runtime->frame.scheduler_task_cursor = original_task_cursor;
            require(!unknown_count_matches
                    && frontier_runtime->alias_certificate_valid
                    && confirmed_counts == confirmed_counts_before_probe
                    && frontier_runtime->alias_certificate_matches(context),
                "an unseen task count declines while preserving every "
                "prior exact-count proof");

            auto* const original_dispatch_counter
                = frontier_runtime->frame.native_frontier_member_dispatches;
            require(original_dispatch_counter != nullptr,
                "the frame has its authentic optional dispatch-counter pointer");
            frontier_runtime->frame.scheduler_task_count
                = static_cast<std::uint32_t>(unknown_task_count);
            frontier_runtime->frame.scheduler_task_cursor
                = std::min(original_task_cursor,
                    static_cast<std::uint32_t>(unknown_task_count));
            frontier_runtime->frame.native_frontier_member_dispatches = nullptr;
            const bool changed_common_tuple_matches
                = frontier_runtime->alias_certificate_matches(context);
            frontier_runtime->frame.native_frontier_member_dispatches
                = original_dispatch_counter;
            frontier_runtime->frame.scheduler_task_count
                = static_cast<std::uint32_t>(original_task_count);
            frontier_runtime->frame.scheduler_task_cursor = original_task_cursor;
            require(!changed_common_tuple_matches
                    && !frontier_runtime->alias_certificate_valid
                    && std::ranges::none_of(confirmed_counts,
                        [](const std::uint8_t valid) { return valid != 0U; }),
                "an unseen count combined with a changed non-task tuple clears all prior proofs");
        }
        const auto target_time = static_cast<SimulationTick>(index + 1U);
        const auto run_result = interpreter.run(target_time);
        require(run_result.status == RunStatus::time_limit
                || run_result.status == RunStatus::completed,
            "each unrelated or selected range stimulus reaches a quiet point");
        const auto frame = capture();
        result.frames.push_back(frame);

        if (frontier_runtime) {
            const auto dispatches = frontier_runtime->native_member_dispatches;
            result.native_dispatch_deltas[index]
                = dispatches - previous_dispatches;
            previous_dispatches = dispatches;
            result.alias_checked_entry_deltas[index]
                = frontier_runtime->alias_checked_entries
                - previous_checked_entries;
            result.alias_trusted_entry_deltas[index]
                = frontier_runtime->alias_trusted_entries
                - previous_trusted_entries;
            previous_checked_entries = frontier_runtime->alias_checked_entries;
            previous_trusted_entries = frontier_runtime->alias_trusted_entries;
            result.alias_sorted_proof_attempt_deltas[index]
                = frontier_runtime->alias_sorted_proof_attempts
                    - previous_alias_sorted_proof_attempts;
            result.alias_sorted_proof_success_deltas[index]
                = frontier_runtime->alias_sorted_proof_successes
                    - previous_alias_sorted_proof_successes;
            result.alias_sorted_proof_failure_deltas[index]
                = frontier_runtime->alias_sorted_proof_failures
                    - previous_alias_sorted_proof_failures;
            previous_alias_sorted_proof_attempts
                = frontier_runtime->alias_sorted_proof_attempts;
            previous_alias_sorted_proof_successes
                = frontier_runtime->alias_sorted_proof_successes;
            previous_alias_sorted_proof_failures
                = frontier_runtime->alias_sorted_proof_failures;
            if (exercise_debug_token && index == 1U) {
                auto& producer_state = implementation.processes.at(producer_id);
                auto& cold = producer_state.cold();
                require(debug_token_waiting_for_native_restore
                        && result.native_dispatch_deltas[index] == 2U
                        && !frontier_runtime->invalidated
                        && implementation.region_runtime_generation
                            == frontier_runtime->runtime_generation
                        && implementation.region_frontier_runtime_by_component.at(
                            component) == frontier_runtime
                        && producer_state.frontier_debug_matches(
                            *final_debug_target,
                            implementation.region_runtime_generation)
                        && producer_state.frontier_debug_target == final_debug_target
                        && producer_state.frontier_debug_runtime_generation
                            == implementation.region_runtime_generation
                        && cold.current_source == final_debug_target->source
                        && cold.current_scope == final_debug_target->scope,
                    "the next native dispatch restores the exact final source/scope token");
                debug_token_waiting_for_native_restore = false;
            }
            if (exercise_debug_token && index == 0U) {
                require(result.native_dispatch_deltas[index] == 1U
                        && !frontier_runtime->invalidated,
                    "the first authentic native dispatch leaves one live debug target");
                const auto generation = implementation.region_runtime_generation;
                require(generation != 0U
                        && generation < std::numeric_limits<std::uint64_t>::max(),
                    "the debug token uses a live, nonwrapping runtime generation");
                auto& producer_state = implementation.processes.at(producer_id);
                auto& cold = producer_state.cold();
                require(producer_state.frontier_debug_matches(
                            *final_debug_target, generation)
                        && producer_state.frontier_debug_target == final_debug_target
                        && producer_state.frontier_debug_runtime_generation
                            == generation
                        && cold.current_source == final_debug_target->source
                        && cold.current_scope == final_debug_target->scope,
                    "authentic V2 synchronization primes the exact final source/scope token");
                require(!producer_state.frontier_debug_matches(
                            *final_debug_target, generation + 1U),
                    "a token cannot match its target at a different generation");
                const auto copied_debug_target = *final_debug_target;
                require(!producer_state.frontier_debug_matches(
                            copied_debug_target, generation),
                    "a token cannot match a copied marker at the same generation");

                const auto retain_final_debug = [&] {
                    require(implementation.synchronize_frontier_process_states(
                                *frontier_runtime)
                            && implementation.region_runtime_generation == generation
                            && implementation.region_frontier_runtime_by_component.at(
                                component) == frontier_runtime
                            && producer_state.frontier_debug_matches(
                                *final_debug_target, generation)
                            && producer_state.frontier_debug_target == final_debug_target
                            && producer_state.frontier_debug_runtime_generation
                            == generation
                            && cold.current_source == final_debug_target->source
                            && cold.current_scope == final_debug_target->scope,
                        "repeated quiet native synchronization retains exact final metadata");
                };
                retain_final_debug();
                retain_final_debug();

                const auto signal_state_before = capture();
                const auto frame_before = frontier_runtime->frame;
                const std::vector<RegionFrontierMemberV2> members_before {
                    frontier_runtime->members.begin(),
                    frontier_runtime->members.end() };
                const std::vector<RegionFrontierSchedulerTaskV2> tasks_before {
                    frontier_runtime->scheduler_tasks.begin(),
                    frontier_runtime->scheduler_tasks.end() };
                const std::vector<std::uint64_t> ready_words_before {
                    frontier_runtime->ready_words.begin(),
                    frontier_runtime->ready_words.end() };
                const auto invalidated_before = frontier_runtime->invalidated;
                const auto dispatches_before
                    = frontier_runtime->native_member_dispatches;
                const auto scheduler_now_before = implementation.scheduler.now();
                const auto scheduler_delta_before = implementation.scheduler.delta();
                const auto scheduler_round_before
                    = implementation.scheduler.systemverilog_round();
                const auto scheduler_phase_before
                    = implementation.scheduler.current_phase();
                const auto scheduler_pending_before
                    = implementation.scheduler.has_pending();
                const auto scheduler_next_time_before
                    = implementation.scheduler.next_pending_time();
                const auto scheduler_stop_before
                    = implementation.scheduler.stop_requested();
                const auto saved_pc = producer_state.pc;
                const auto saved_status = producer_state.status;
                const auto saved_queued = producer_state.queued;
                const auto saved_waiting_on_static
                    = producer_state.waiting_on_static;
                const auto saved_waiting_on_signal
                    = producer_state.waiting_on_signal;
                const auto saved_suspended = producer_state.suspended;
                const auto saved_suspended_wake = producer_state.suspended_wake;
                const auto saved_halted = producer_state.halted;
                const auto saved_boundary_validated
                    = producer_state.region_kernel_completion_boundary_validated;
                const auto saved_wait_timeout_origin
                    = producer_state.wait_timeout_origin;
                const auto saved_static_trigger_mask
                    = producer_state.static_trigger_mask;
                const auto saved_execution_phase = producer_state.execution_phase;
                require(!implementation.execution_point_hook,
                    "the authentic debug-token fixture begins without an observer");
                auto previous_hook = implementation.execution_point_hook;
                require(producer_state.waiting_on_static
                        && producer_state.status == ProcessStatus::waiting
                        && !producer_state.queued
                        && producer_state.executor != nullptr
                        && saved_pc < producer_state.program().operations().size()
                        && frontier_runtime->frame.pending_write_count == 0U
                        && frontier_runtime->frame.staged_event_count == 0U
                        && frontier_runtime->frame.scheduler_task_cursor
                            == frontier_runtime->frame.scheduler_task_count,
                    "the checked DebugPoint boundary probe starts from a quiet parked member");
                bool hook_observed_cleared_token { };
                struct DebugPointProbeStop final { };
                implementation.execution_point_hook
                    = [&](Scheduler&, const ExecutionPoint& point) {
                          require(point.process == producer_id
                                  && point.instruction == debug_entry_instruction
                                  && point.kind == ExecutionPointKind::statement
                                  && point.source == debug_entry_source
                                  && point.scope == debug_entry_scope.str(),
                              "the checked boundary reaches the selected ordinary DebugPoint");
                          require(producer_state.frontier_debug_target == nullptr
                                  && producer_state.frontier_debug_runtime_generation == 0U
                                  && !producer_state.frontier_debug_matches(
                                      *final_debug_target, generation)
                                  && implementation.region_runtime_generation == generation
                                  && cold.current_source == debug_entry_source
                                  && cold.current_scope == debug_entry_scope.str(),
                              "the DebugPoint writer clears the token before its metadata update");
                          hook_observed_cleared_token = true;
                          throw DebugPointProbeStop { };
                      };
                bool probe_stopped_at_debug_point { };
                const auto restore_execution_control = [&] {
                    implementation.execution_point_hook = std::move(previous_hook);
                    producer_state.pc = saved_pc;
                    producer_state.status = saved_status;
                    producer_state.queued = saved_queued;
                    producer_state.waiting_on_static = saved_waiting_on_static;
                    producer_state.waiting_on_signal = saved_waiting_on_signal;
                    producer_state.suspended = saved_suspended;
                    producer_state.suspended_wake = saved_suspended_wake;
                    producer_state.halted = saved_halted;
                    producer_state.region_kernel_completion_boundary_validated
                        = saved_boundary_validated;
                    producer_state.wait_timeout_origin = saved_wait_timeout_origin;
                    producer_state.static_trigger_mask = saved_static_trigger_mask;
                    producer_state.execution_phase = saved_execution_phase;
                    producer_state.executor->redirect(saved_pc);
                };
                producer_state.pc = debug_entry_instruction;
                producer_state.executor->redirect(debug_entry_instruction);
                try {
                    implementation.handle_boundary(producer_state,
                        debug_entry_instruction, debug_entry_instruction + 1U);
                } catch (const DebugPointProbeStop&) {
                    probe_stopped_at_debug_point = true;
                } catch (...) {
                    restore_execution_control();
                    throw;
                }
                restore_execution_control();
                require(probe_stopped_at_debug_point && hook_observed_cleared_token
                        && producer_state.frontier_debug_target == nullptr
                        && producer_state.frontier_debug_runtime_generation == 0U
                        && !frontier_runtime->invalidated
                        && implementation.region_runtime_generation == generation
                        && implementation.region_frontier_runtime_by_component.at(
                            component) == frontier_runtime
                        && frontier_runtime->native_member_dispatches == dispatches_before
                        && same_frontier_frame(frontier_runtime->frame, frame_before)
                        && std::ranges::equal(frontier_runtime->members,
                            members_before, same_frontier_member)
                        && std::ranges::equal(frontier_runtime->scheduler_tasks,
                            tasks_before, same_frontier_task)
                        && std::ranges::equal(frontier_runtime->ready_words,
                            ready_words_before)
                        && std::ranges::equal(capture(), signal_state_before,
                            same_alias_signal_state)
                        && implementation.scheduler.now() == scheduler_now_before
                        && implementation.scheduler.delta() == scheduler_delta_before
                        && implementation.scheduler.systemverilog_round()
                            == scheduler_round_before
                        && implementation.scheduler.current_phase()
                            == scheduler_phase_before
                        && implementation.scheduler.has_pending()
                            == scheduler_pending_before
                        && implementation.scheduler.next_pending_time()
                            == scheduler_next_time_before
                        && implementation.scheduler.stop_requested()
                            == scheduler_stop_before
                        && producer_state.pc == saved_pc
                        && producer_state.status == saved_status
                        && producer_state.queued == saved_queued
                        && producer_state.waiting_on_static == saved_waiting_on_static
                        && producer_state.waiting_on_signal == saved_waiting_on_signal
                        && producer_state.region_kernel_completion_boundary_validated
                            == saved_boundary_validated
                        && cold.current_source == debug_entry_source
                        && cold.current_scope == debug_entry_scope.str()
                        && frontier_runtime->invalidated == invalidated_before,
                    "the ordinary DebugPoint changes only its metadata token and source/scope");
                debug_token_waiting_for_native_restore = true;
            }
            if (index == 3U) {
                const auto reconfirmed_count = static_cast<std::size_t>(
                    frontier_runtime->frame.scheduler_task_count);
                const auto revalidation_accepted
                    = result.alias_checked_entry_deltas[index] != 0U
                    || (result.alias_sorted_proof_success_deltas[index] != 0U
                        && result.alias_trusted_entry_deltas[index] != 0U);
                require(revalidation_accepted
                        && reconfirmed_count
                            < frontier_runtime->alias_certificate_task_count_valid.size()
                        && frontier_runtime->alias_certificate_valid
                        && frontier_runtime->alias_certificate_task_count_valid[
                            reconfirmed_count] != 0U
                        && frontier_runtime->alias_certificate_matches(
                            frontier_runtime->alias_certificate_context),
                    "checked-entry or host-proof revalidation confirms the task count "
                    "after the pointer mismatch");
            }
            require(result.native_dispatch_deltas[index]
                    == expected_dispatches[index],
                "V2 dispatches the producer always and the ranged reader only on a matching change");
        }

        require(frame[1U].current == inputs[index]
                && frame[1U].stored == inputs[index]
                && frame[1U].boundary_owner_value
                    == std::optional { inputs[index] },
            "the private signal retains its full eight-bit owner plane across range filtering");
        if (index == 0U) {
            require(frame[2U].current == result.frames.front()[2U].current
                    && frame[2U].last == result.frames.front()[2U].last
                    && frame[2U].stored == result.frames.front()[2U].stored
                    && frame[2U].boundary_owner_value
                        == result.frames.front()[2U].boundary_owner_value
                    && frame[2U].event == result.frames.front()[2U].event
                    && frame[2U].transaction
                        == result.frames.front()[2U].transaction
                    && frame[2U].value_revision
                        == result.frames.front()[2U].value_revision,
                "an unrelated bit changes the full source but does not activate the ranged reader");
        } else {
            require(frame[2U].current == inputs[index]
                    && frame[2U].stored == inputs[index]
                    && frame[2U].boundary_owner_value
                        == std::optional { inputs[index] }
                    && frame[2U].last
                        == result.frames[index][2U].current
                    && frame[2U].event != result.frames[index][2U].event
                    && frame[2U].transaction
                        != result.frames[index][2U].transaction
                    && frame[2U].value_revision
                        == result.frames[index][2U].value_revision + 1U
                    && frame[2U].stamp.origin.process_domain
                        == ProcessSchedulingDomain::systemverilog
                    && frame[2U].stamp.origin.phase == SchedulerPhase::active,
                "a selected range publishes the full signal once through its original owner");
            require(frame[0U].event && frame[1U].event && frame[2U].event
                    && frame[0U].event->first == target_time
                    && frame[1U].event->first == target_time
                    && frame[2U].event->first == target_time,
                "input, private commit, and reader output retain the stimulus time");
        }
    }
    if (exercise_debug_token) {
        require(!debug_token_waiting_for_native_restore,
            "a subsequent authentic V2 dispatch restores the cleared debug token");
    }
    if (frontier_runtime) {
        require(result.native_dispatch_deltas == expected_dispatches,
            "a simultaneous change to both configured ranges activates the reader only once");
        require(result.alias_trusted_entry_deltas[2U] != 0U,
            "repeated one-bit-of-eight range updates reuse the exact stable alias certificate through the trusted entry");
        bool proof_selected_trusted_entry { };
        for (std::size_t index = 0U;
             index < result.alias_sorted_proof_attempt_deltas.size(); ++index) {
            require(result.alias_sorted_proof_attempt_deltas[index]
                    == result.alias_sorted_proof_success_deltas[index]
                        + result.alias_sorted_proof_failure_deltas[index],
                "each host geometry proof records exactly one result");
            proof_selected_trusted_entry
                = proof_selected_trusted_entry
                || (result.alias_sorted_proof_success_deltas[index] != 0U
                    && result.alias_trusted_entry_deltas[index] != 0U);
        }
        require(proof_selected_trusted_entry,
            "an authentic host geometry proof selects the builtin trusted entry");
    }
    return result;
}

void check_internal_range_v2_readiness(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit& process_jit)
{
    InternalRangeReadinessRun checked;
    {
        ScopedEnvironment region_disabled {
            "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
        ScopedEnvironment local_wave_disabled {
            "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
        ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
        checked = run_internal_range_readiness_case(nullptr, nullptr);
    }
    ScopedEnvironment region_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    const auto compiled = run_internal_range_readiness_case(
        std::make_shared<FrontierOnlyProvider>(provider), &process_jit, true);
    require(checked.checked_programs_absent && compiled.v2_runtime_prepared,
        "the differential pair uses ordinary checked execution and authentic V2 dispatch");
    require(checked.frames.size() == compiled.frames.size(),
        "the checked and native routes capture the same scheduler cuts");
    for (std::size_t frame = 0U; frame < checked.frames.size(); ++frame) {
        for (std::size_t signal = 0U; signal < checked.frames[frame].size();
             ++signal) {
            require(same_alias_signal_state(checked.frames[frame][signal],
                        compiled.frames[frame][signal]),
                "checked and native range routes preserve current/LAST/stored, owner, event, and transaction metadata");
        }
    }
}

void check_alias_sorted_geometry_checked_fallback(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { 1U, provider };
    auto& runtime = fixture.runtime();
    auto& implementation = fixture.implementation();
    require(runtime.alias_certificate_storage_available
            && runtime.frame.staged_event_count == 0U,
        "the fallback witness begins with optional alias storage and no staged events");

    auto* const executor = runtime.backend->executor.get();
    const auto* const capability
        = dynamic_cast<const fsim::runtime::simir::detail::
                RegionFrontierTrustedEntryCapability*>(executor);
    require(executor != nullptr && capability != nullptr,
        "the fallback fixture retains checked and private builtin entries");
    const auto trusted_view = capability->trusted_entry();
    const auto checked_entry = executor->step_entry();
    require(checked_entry != nullptr && trusted_view.entry != nullptr
            && trusted_view.layout == &executor->layout(),
        "the private capability remains bound to the checked entry layout");
    const auto context = runtime.make_alias_certificate_context(
        *runtime.backend, checked_entry, trusted_view.entry);

    const auto internal_flag = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
    const auto internal_plane = std::ranges::find_if(runtime.planes,
        [](const RegionFrontierPlaneV2& plane) {
            return plane.flags == internal_flag && plane.plane_count != 0U;
        });
    require(internal_plane != runtime.planes.end()
            && internal_plane->current_planes[0U] != nullptr,
        "the fixture has a mutable internal plane for a controlled alias collision");
    auto* const plane = &*internal_plane;
    auto* const original_current = plane->current_planes[0U];
    plane->current_planes[0U]
        = reinterpret_cast<std::uint64_t*>(&runtime.frame);
    struct RestoreCurrentPlane final {
        RegionFrontierPlaneV2* plane { };
        std::uint64_t* original { };
        ~RestoreCurrentPlane()
        {
            if (plane != nullptr) {
                plane->current_planes[0U] = original;
            }
        }
    } restore_current_plane { plane, original_current };
    auto* const overlapping_current = plane->current_planes[0U];

    require(runtime.stage_alias_certificate(context),
        "the host stages the exact inventory containing a plane/frame overlap");
    require(runtime.alias_candidate_ranges.size() >= 14U
            && std::ranges::any_of(
                std::span { runtime.alias_candidate_ranges }.subspan(13U),
                [&runtime](const FrontierAliasRange& range) {
                    return range.address == &runtime.frame;
                }),
        "the candidate inventory records the plane/frame overlap as data");
    const auto proof_failures_before = runtime.alias_sorted_proof_failures;
    require(!runtime.prove_alias_geometry_sorted()
            && runtime.alias_sorted_proof_failures
                == proof_failures_before + 1U,
        "a host geometry failure routes to the public checked entry");

    const auto frame_before = runtime.frame;
    const std::vector<RegionFrontierMemberV2> members_before {
        runtime.members.begin(), runtime.members.end() };
    const std::vector<RegionFrontierSchedulerTaskV2> tasks_before {
        runtime.scheduler_tasks.begin(), runtime.scheduler_tasks.end() };
    const std::vector<std::uint64_t> ready_words_before {
        runtime.ready_words.begin(), runtime.ready_words.end() };
    const std::vector<RegionFrontierPendingWriteV2> pending_before {
        runtime.pending_writes.begin(), runtime.pending_writes.end() };
    const std::vector<RegionFrontierStagedEventV2> staged_before {
        runtime.staged_events.begin(), runtime.staged_events.end() };
    const std::vector<RegionFrontierCommittedSignalV2> committed_before {
        runtime.committed_signals.begin(), runtime.committed_signals.end() };
    const std::vector<RegionFrontierPlaneV2> planes_before {
        runtime.planes.begin(), runtime.planes.end() };
    const auto capture_signal = [&](const SignalId signal,
                                    const ProcessId owner) {
        const auto binding = std::ranges::find(runtime.writable_signals,
            signal, &AuthoritativeSignalPlanes::FrontierWriteBinding::signal);
        if (binding == runtime.writable_signals.end()) {
            return capture_boundary_signal_state(implementation, signal, owner);
        }
        const auto& descriptor = find_frontier_plane(runtime, signal);
        const auto capture_role = [&](const PackedPlaneRole role) {
            std::array<std::span<std::uint64_t>, 4U> words { };
            require(fixture.lease().plane_words(signal, role, owner, words)
                    && words[0U].size() == descriptor.word_count
                    && words[1U].size() == descriptor.word_count,
                "the owned write lease exposes every internal role for the no-mutation snapshot");
            return PackedLogic4::from_word_planes(
                descriptor.width, words[0U], words[1U]);
        };
        return AliasSignalState {
            capture_role(PackedPlaneRole::current),
            capture_role(PackedPlaneRole::previous),
            capture_role(PackedPlaneRole::stored),
            implementation.driver_values.at(signal).size(),
            capture_role(PackedPlaneRole::owner),
            implementation.signal_events.at(signal),
            implementation.signal_transactions.at(signal),
            implementation.signal_event_scheduling_stamps.at(signal),
            implementation.signal_value_revisions.at(signal),
        };
    };
    const std::array<AliasSignalState, 4U> signals_before {
        capture_signal( source_signal_id,
            producer_process_id),
        capture_signal( internal_signal_id,
            producer_process_id),
        capture_signal( boundary_signal_id,
            consumer_process_id),
        capture_signal( aliased_internal_signal_id,
            producer_process_id),
    };
    const auto native_dispatches_before = runtime.native_member_dispatches;
    const auto invalidated_before = runtime.invalidated;
    const auto checked_status = checked_entry(&runtime.frame);

    const auto same_plane = [](
        const RegionFrontierPlaneV2& left,
        const RegionFrontierPlaneV2& right) noexcept {
        return left.signal_id == right.signal_id
            && left.owner_process_id == right.owner_process_id
            && left.value_kind == right.value_kind
            && left.width == right.width
            && left.word_count == right.word_count
            && left.plane_count == right.plane_count
            && left.flags == right.flags
            && left.metadata_index == right.metadata_index
            && std::ranges::equal(left.boundary_planes, right.boundary_planes)
            && std::ranges::equal(left.current_planes, right.current_planes)
            && std::ranges::equal(left.previous_planes, right.previous_planes)
            && std::ranges::equal(left.stored_planes, right.stored_planes)
            && std::ranges::equal(left.owner_planes, right.owner_planes);
    };
    bool signals_unchanged = true;
    const std::array<SignalId, 4U> signal_ids {
        source_signal_id, internal_signal_id,
        boundary_signal_id, aliased_internal_signal_id };
    const std::array<ProcessId, 4U> owners {
        producer_process_id, producer_process_id,
        consumer_process_id, producer_process_id };
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        signals_unchanged = signals_unchanged
            && same_alias_signal_state(signals_before[index],
                capture_signal( signal_ids[index], owners[index]));
    }

    require(checked_status == RegionFrontierStatusV2::decline_before_mutation
            && runtime.invalidated == invalidated_before
            && runtime.native_member_dispatches == native_dispatches_before
            && same_frontier_frame(runtime.frame, frame_before)
            && std::ranges::equal(runtime.members, members_before,
                same_frontier_member)
            && std::ranges::equal(runtime.scheduler_tasks, tasks_before,
                same_frontier_task)
            && std::ranges::equal(runtime.ready_words, ready_words_before)
            && std::ranges::equal(runtime.pending_writes, pending_before,
                same_pending_write)
            && std::ranges::equal(runtime.staged_events, staged_before,
                same_staged_event)
            && std::ranges::equal(runtime.committed_signals, committed_before,
                same_committed_signal)
            && std::ranges::equal(runtime.planes, planes_before, same_plane)
            && plane->current_planes[0U] == overlapping_current
            && signals_unchanged,
        "the public checked fallback declines the unproved overlap without mutation");

    restore_current_plane.plane = nullptr;
    plane->current_planes[0U] = original_current;
    runtime.clear_alias_certificate();
}

void check_event_bearing_entry_does_not_prime_alias_certificate(
    const std::shared_ptr<RegionKernelBackendProvider>& provider)
{
    NativeBoundaryFixture fixture { 1U, provider };
    auto& runtime = fixture.runtime();
    require(runtime.frame.staged_event_count == 0U
            && runtime.frame.scheduler_task_count != 0U,
        "the authentic checked fixture begins with a queued root and no staged events");
    const auto* const trusted_capability = dynamic_cast<const fsim::runtime::simir::detail::
        RegionFrontierTrustedEntryCapability*>(runtime.backend->executor.get());
    require(trusted_capability != nullptr,
        "the production LLVM adapter exposes only its private trusted-entry capability");
    const auto trusted_view = trusted_capability->trusted_entry();
    const auto checked_entry = runtime.backend->executor->step_entry();
    require(checked_entry != nullptr && trusted_view.entry != nullptr
            && trusted_view.layout == &runtime.backend->executor->layout(),
        "the capability binds its generated entry to the exact immutable layout");
    const auto context = runtime.make_alias_certificate_context(
        *runtime.backend, checked_entry, trusted_view.entry);

    require(runtime.stage_alias_certificate(context),
        "a checked call with no preexisting staged events can stage its exact alias inventory");
    const auto [first_status, first_certificate_confirmed]
        = fixture.checked_entry_once_and_confirm_alias_certificate(context);
    require(first_status == RegionFrontierStatusV2::need_scheduler_keys
            && runtime.frame.staged_event_count != 0U
            && first_certificate_confirmed,
        "the zero-event checked entry confirms its exact inventory even as it leaves real staged scheduler events pending");

    // Reset only the optional certificate so the following event-bearing
    // entry starts from an unprimed cache. Its pending event rows remain live.
    runtime.clear_alias_certificate();
    require(!runtime.stage_alias_certificate(context)
            && !runtime.alias_certificate_valid,
        "an entry that begins with staged events cannot prime a new certificate");
    const auto [second_status, second_certificate_confirmed]
        = fixture.checked_entry_once_and_confirm_alias_certificate(context);
    require(second_status == RegionFrontierStatusV2::need_scheduler_keys
            && runtime.frame.staged_event_count != 0U
            && !second_certificate_confirmed
            && !runtime.alias_certificate_valid,
        "the early need-keys checked return leaves the pre-call event inventory uncertified");
}

struct PartialBoundaryPublicationRun final {
    std::array<AliasSignalState, 3U> startup;
    std::array<AliasSignalState, 3U> updated;
    std::uint64_t native_dispatch_delta { };
    bool runtime_prepared { };
    bool runtime_retained { };
};

[[nodiscard]] PartialBoundaryPublicationRun
run_partial_boundary_publication_case(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit* const process_jit,
    const std::uint32_t signal_width,
    const std::uint32_t slice_offset,
    const std::uint32_t slice_width)
{
    constexpr SignalId input_id = 0U;
    constexpr SignalId internal_id = 1U;
    constexpr SignalId output_id = 2U;
    constexpr ProcessId producer_id = 0U;
    constexpr ProcessId reader_id = 1U;
    constexpr ProcessId clock_id = 2U;
    constexpr SimulationTick stimulus_time = 1U;

    require(signal_width != 0U && slice_width != 0U
            && slice_offset <= signal_width
            && slice_width <= signal_width - slice_offset
            && ((provider == nullptr) == (process_jit == nullptr)),
        "the observed slice case has a paired route and in-bounds output range");
    const std::array<std::uint32_t, 3U> signal_widths {
        signal_width, signal_width, signal_width };
    const std::array<ValueKind, 3U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    const std::array<ResolutionKind, 3U> signal_resolutions {
        ResolutionKind::sv_wire, ResolutionKind::sv_wire,
        ResolutionKind::sv_wire };
    const auto changed_source
        = make_partial_boundary_source(signal_width, slice_offset, slice_width);
    const auto changed_slice
        = changed_source.extract_bits(slice_offset, slice_width);
    std::array<std::optional<Process>, 2U> registered_processes;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "slice_host.input",
        PackedLogic4 { signal_width, Logic4::zero }, ResolutionKind::sv_wire });
    const auto internal = interpreter.add_signal({ "slice_host.internal",
        PackedLogic4 { signal_width, Logic4::zero }, ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "slice_host.output",
        PackedLogic4 { signal_width, Logic4::zero }, ResolutionKind::sv_wire });
    require(input == input_id && internal == internal_id && output == output_id,
        "the observed slice case keeps dense source, private, and output IDs");
    static_cast<void>(interpreter.signal_value(output));

    const auto add_member = [&](Process process) {
        const auto id = process.id;
        require(id < registered_processes.size(),
            "only the producer and reader belong to this native component");
        auto& registered = registered_processes[id].emplace(std::move(process));
        const auto name = registered.name;
        require(interpreter.add_process(registered) == id,
            "the observed slice component keeps its original process IDs");
        if (process_jit == nullptr) {
            return;
        }
        process_jit->add_process(name, registered,
            signal_widths, signal_kinds);
        const auto handle = process_jit->lookup(name);
        require(static_cast<bool>(handle),
            "LLVM retains each real process in the partial boundary component");
        auto executor = std::make_unique<
            fsim::app::application_detail::LlvmProcessExecutor>(
                *process_jit, handle, registered,
                signal_widths, signal_kinds, signal_resolutions,
                std::shared_ptr<const ProcessSignalRemap> { }, id);
        const auto* const binding = executor->program_access_binding();
        require(binding != nullptr && binding->valid()
                && executor->region_kernel_equivalent()
                && executor->cohort_manages_process_state()
                && executor->region_kernel_completion_has_no_persistent_registers(),
            "the real process executors satisfy the V2 admission contract");
        interpreter.set_process_executor(id, std::move(executor));
    };

    Process producer;
    producer.id = producer_id;
    producer.name = "slice_host_producer_"
        + std::to_string(signal_width);
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { input, EdgeKind::any } };
    producer.driver_regions = { { internal, 0U, 0U, true } };
    producer.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    add_member(std::move(producer));

    Process reader;
    reader.id = reader_id;
    reader.name = "slice_host_reader_"
        + std::to_string(signal_width);
    reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    reader.register_count = 2U;
    reader.register_value_kinds.assign(2U, ValueKind::logic4);
    reader.static_sensitivity = { { internal, EdgeKind::any } };
    reader.driver_regions = {
        { output, slice_offset, slice_width, false },
    };
    reader.operations = {
        ReadSignal { 0U, internal },
        Extract { 1U, 0U, slice_offset, slice_width },
        WriteUpdateSlice { output, 1U, slice_offset,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    add_member(std::move(reader));

    Process clock;
    clock.id = clock_id;
    clock.name = "slice_host_clock_" + std::to_string(signal_width);
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.register_value_kinds = { ValueKind::logic4 };
    clock.driver_regions = { { input, 0U, 0U, true } };
    clock.operations = {
        LoadConstant { 0U, PackedLogic4 { signal_width, Logic4::zero } },
        WriteBlocking { input, 0U },
        WaitFor { stimulus_time },
        LoadConstant { 0U, changed_source },
        WriteBlocking { input, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == clock_id,
        "the timed input writer remains outside the V2 component");
    if (provider) {
        interpreter.set_region_kernel_backend_provider(provider);
    }
    interpreter.start();
    const auto startup_result = interpreter.run(0U);
    require(startup_result.status == RunStatus::time_limit
            || startup_result.status == RunStatus::completed,
        "normal startup settles before the partial boundary stimulus");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto detached_signal_state = [&](const SignalId signal,
                                           const ProcessId owner) {
        auto state = capture_boundary_signal_state(
            implementation, signal, owner);
        const auto detach = [](const PackedLogic4& value) {
            PackedLogic4 result { value.width(), Logic4::zero };
            for (std::size_t bit = 0U; bit < value.width(); ++bit) {
                result.set(bit, value.get(bit));
            }
            return result;
        };
        state.current = detach(state.current);
        state.last = detach(state.last);
        state.stored = detach(state.stored);
        if (state.boundary_owner_value) {
            state.boundary_owner_value = detach(*state.boundary_owner_value);
        }
        return state;
    };
    const auto capture = [&]() {
        return std::array<AliasSignalState, 3U> {
            detached_signal_state(input, clock_id),
            detached_signal_state(internal, producer_id),
            detached_signal_state(output, reader_id),
        };
    };
    auto startup = capture();
    require(startup[0U].current == PackedLogic4 { signal_width, Logic4::zero }
            && startup[1U].current == PackedLogic4 { signal_width, Logic4::zero }
            && startup[2U].boundary_owner_value.has_value()
            && startup[2U].current.get(slice_offset) == Logic4::zero
            && startup[2U].boundary_owner_value->get(slice_offset)
                == Logic4::zero,
        "the zero-time source settles the original output owner before the slice write");

    std::shared_ptr<FrontierRuntime> runtime;
    std::uint64_t generation { };
    std::size_t component = std::numeric_limits<std::size_t>::max();
    bool runtime_prepared { };
    if (provider) {
        component = implementation.region_component_by_process.at(producer_id);
        require(component != std::numeric_limits<std::size_t>::max()
                && implementation.region_component_by_process.at(reader_id)
                    == component,
            "the private producer and observed slice writer share one component");
        runtime = implementation.region_frontier_runtime_by_component.at(component);
        require(runtime && runtime->backend && runtime->backend->executor
                && runtime->backend->executor->step_entry() != nullptr,
            "the observed boundary path owns an authentic generated V2 entry");
        const auto& kernel = runtime->backend->kernel;
        const auto& layout = runtime->backend->executor->layout();
        const auto output_binding = std::ranges::find(kernel.outputs, output,
            &RegionConeOutputBinding::signal);
        require(kernel.members.size() == 2U
                && !implementation.region_cone_forwarding_backends_by_component
                    .at(component)
                && kernel.internal_signals.size() == 1U
                && std::ranges::find(kernel.internal_signals, internal)
                    != kernel.internal_signals.end()
                && std::ranges::find(kernel.internal_signals, output)
                    == kernel.internal_signals.end()
                && output_binding != kernel.outputs.end()
                && output_binding->owner == reader_id
                && output_binding->offset == slice_offset
                && output_binding->width == slice_width
                && output_binding->signal_width == signal_width,
            "the observed output retains its exact source slice and full target width");
        require(layout.write_site_count != 0U && layout.write_sites != nullptr
                && layout.signal_slot_count != 0U
                && layout.signals != nullptr,
            "the boundary site maps through the complete certified layout");
        const RegionFrontierWriteSiteV2* const boundary_site = [&]() {
            for (std::size_t index = 0U;
                 index < layout.write_site_count; ++index) {
                const auto& site = layout.write_sites[index];
                if (site.signal_slot < layout.signal_slot_count
                    && layout.signals[site.signal_slot].signal_id == output) {
                    return &site;
                }
            }
            return static_cast<const RegionFrontierWriteSiteV2*>(nullptr);
        }();
        require(boundary_site != nullptr
                && boundary_site->event_kind == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::boundary_commit)
                && boundary_site->width == slice_width
                && boundary_site->word_count == (slice_width + 63U) / 64U
                && boundary_site->signal_slot < layout.signal_slot_count
                && layout.signals[boundary_site->signal_slot].width == signal_width
                && layout.signals[boundary_site->signal_slot].word_count
                    == (signal_width + 63U) / 64U,
            "the admitted V2 write site is slice-sized over a full boundary plane");
        runtime_prepared = !runtime->invalidated
            && runtime->frame_initialized
            && runtime->runtime_generation
                == implementation.region_runtime_generation;
        require(runtime_prepared,
            "the native boundary runtime remains valid after normal startup");
        generation = implementation.region_runtime_generation;
    } else {
        runtime_prepared
            = !implementation.systemverilog_region_kernel_enabled
            && std::ranges::none_of(
                implementation.region_activation_programs,
                [](const auto& program) { return program.has_value(); })
            && std::ranges::none_of(
                implementation.region_frontier_runtime_by_component,
                [](const auto& value) { return value != nullptr; });
        require(runtime_prepared,
            "the checked reference builds no native activation program");
    }

    const auto before_dispatches
        = runtime ? runtime->native_member_dispatches : 0U;
    const auto run_result = interpreter.run(stimulus_time);
    require(run_result.status == RunStatus::time_limit
            || run_result.status == RunStatus::completed,
        "the partial boundary write reaches a normal scheduler quiet point");
    auto updated = capture();
    bool runtime_retained { };
    std::uint64_t native_dispatch_delta { };
    if (runtime) {
        native_dispatch_delta
            = runtime->native_member_dispatches - before_dispatches;
        runtime_retained
            = !runtime->invalidated
            && implementation.region_frontier_runtime_by_component.at(component).get()
                == runtime.get()
            && implementation.region_runtime_generation == generation
            && runtime->frame.pending_write_count == 0U
            && runtime->frame.staged_event_count == 0U
            && runtime->frame.committed_signal_count == 0U
            && runtime->frame.scheduler_task_cursor
                == runtime->frame.scheduler_task_count;
        require(native_dispatch_delta >= 2U && runtime_retained,
            "native V2 dispatches both real members and retires the partial commit without invalidation");
    }

    auto expected_current = startup[2U].current;
    expected_current.insert_bits(changed_slice, slice_offset);
    require(startup[2U].boundary_owner_value.has_value(),
        "the observed partial writer has an original owner value to preserve");
    auto expected_owner = *startup[2U].boundary_owner_value;
    expected_owner.insert_bits(changed_slice, slice_offset);
    require(updated[0U].current == changed_source
            && updated[0U].stored == changed_source
            && updated[0U].boundary_owner_value
                == std::optional { changed_source }
            && updated[1U].current == changed_source
            && updated[1U].stored == changed_source
            && updated[1U].boundary_owner_value
                == std::optional { changed_source },
        "the source and full-width private owner remain exact through V2 staging");
    const auto& before_output = startup[2U];
    const auto& after_output = updated[2U];
    require(after_output.current == expected_current
            && after_output.stored == expected_owner
            && after_output.boundary_owner_value
                == std::optional { expected_owner }
            && after_output.last == before_output.current
            && after_output.raw_driver_count == 1U
            && after_output.event.has_value()
            && after_output.event->first == stimulus_time
            && after_output.transaction.has_value()
            && after_output.transaction->first == stimulus_time
            && after_output.value_revision
                == before_output.value_revision + 1U
            && after_output.stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && after_output.stamp.origin.phase == SchedulerPhase::active,
        "the original boundary driver changes only the slice and preserves full-width history and metadata");
    return { std::move(startup), std::move(updated), native_dispatch_delta,
        runtime_prepared, runtime_retained };
}

void check_partial_boundary_staged_event_slice_publication(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit& process_jit,
    const std::shared_ptr<RegionKernelBackendProvider>& o0_provider,
    fsim::compiler::LlvmJit& o0_process_jit)
{
    for (const auto& [signal_width, offset, slice_width] : {
             std::array<std::uint32_t, 3U> { 8U, 3U, 1U },
             std::array<std::uint32_t, 3U> { 129U, 32U, 65U },
         }) {
        PartialBoundaryPublicationRun checked;
        {
            ScopedEnvironment region_disabled {
                "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
            ScopedEnvironment wave_disabled {
                "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
            checked = run_partial_boundary_publication_case(nullptr, nullptr,
                signal_width, offset, slice_width);
        }
        const auto optimized = [&]() {
            ScopedEnvironment region_enabled {
                "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
            ScopedEnvironment wave_enabled {
                "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
            return run_partial_boundary_publication_case(
                std::make_shared<FrontierOnlyProvider>(provider), &process_jit,
                signal_width, offset, slice_width);
        }();
        const auto unoptimized = [&]() {
            ScopedEnvironment region_enabled {
                "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
            ScopedEnvironment wave_enabled {
                "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
            return run_partial_boundary_publication_case(
                std::make_shared<FrontierOnlyProvider>(o0_provider),
                &o0_process_jit, signal_width, offset, slice_width);
        }();
        require(checked.runtime_prepared && optimized.runtime_prepared
                && unoptimized.runtime_prepared
                && optimized.runtime_retained && unoptimized.runtime_retained
                && optimized.native_dispatch_delta >= 2U
                && unoptimized.native_dispatch_delta >= 2U,
            "both LLVM levels retain authentic V2 dispatch through partial boundary publication");
        for (const auto* const compiled : { &optimized, &unoptimized }) {
            require(same_alias_signal_state(checked.startup[0U],
                        compiled->startup[0U])
                    && same_alias_signal_state(checked.startup[1U],
                        compiled->startup[1U])
                    && same_alias_signal_state(checked.startup[2U],
                        compiled->startup[2U])
                    && same_alias_signal_state(checked.updated[0U],
                        compiled->updated[0U])
                    && same_alias_signal_state(checked.updated[1U],
                        compiled->updated[1U])
                    && same_alias_signal_state(checked.updated[2U],
                        compiled->updated[2U]),
                "checked and O0/O2 routes preserve full signal roles, LAST, owner, event, and transaction state");
        }
    }
}

struct InternalCommitBudgetRun final {
    std::vector<std::vector<AliasSignalState>> frames;
    std::array<std::uint64_t, 2U> native_dispatch_deltas { };
    bool runtime_prepared { };
    bool runtime_retained { };
    bool runtime_remained_valid { };
};

[[nodiscard]] InternalCommitBudgetRun run_internal_commit_budget_case(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit* const process_jit)
{
    constexpr std::size_t internal_count = 31U;
    constexpr std::size_t reader_count = 3U;
    constexpr ProcessId producer_id = 0U;
    constexpr ProcessId first_reader_id = 1U;
    constexpr ProcessId clock_id = 4U;
    constexpr std::uint32_t frame_event_budget = 64U;

    const auto signal_count = 1U + internal_count + reader_count;
    const auto member_count = 1U + reader_count;
    const auto output_begin = 1U + internal_count;
    std::vector<std::uint32_t> signal_widths(signal_count, 1U);
    std::vector<ValueKind> signal_kinds(signal_count, ValueKind::logic4);
    std::vector<ResolutionKind> signal_resolutions(
        signal_count, ResolutionKind::sv_wire);
    std::vector<std::optional<Process>> registered_processes(member_count);

    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "prefix_budget.input",
        PackedLogic4 { 1U, Logic4::zero }, ResolutionKind::sv_wire });
    require(input == 0U,
        "the event-budget fixture keeps its timed source at signal zero");

    std::vector<SignalId> internal_signals;
    internal_signals.reserve(internal_count);
    for (std::size_t index = 0U; index < internal_count; ++index) {
        const auto signal = interpreter.add_signal({
            "prefix_budget.internal." + std::to_string(index),
            PackedLogic4 { 1U, Logic4::zero }, ResolutionKind::sv_wire });
        require(signal == index + 1U,
            "every bounded private owner signal keeps dense identity");
        internal_signals.push_back(signal);
    }

    std::vector<SignalId> output_signals;
    output_signals.reserve(reader_count);
    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto signal = interpreter.add_signal({
            "prefix_budget.output." + std::to_string(index),
            PackedLogic4 { 1U, Logic4::zero }, ResolutionKind::sv_wire });
        require(signal == output_begin + index,
            "each fanout reader has one distinct output");
        output_signals.push_back(signal);
    }

    InternalCommitBudgetRun result;
    const auto add_member = [&](Process process) {
        const auto process_id = process.id;
        require(process_id < registered_processes.size(),
            "the four-member budget component keeps stable Process storage");
        auto& registered = registered_processes[process_id].emplace(
            std::move(process));
        const auto name = registered.name;
        require(interpreter.add_process(registered) == process_id,
            "each budget member is registered with its authenticated identity");
        if (process_jit == nullptr) {
            return;
        }

        process_jit->add_process(name, registered,
            signal_widths, signal_kinds);
        const auto handle = process_jit->lookup(name);
        require(static_cast<bool>(handle),
            "LLVM retains each real process used by the budget component");
        auto executor = std::make_unique<
            fsim::app::application_detail::LlvmProcessExecutor>(
                *process_jit, handle, registered,
                signal_widths, signal_kinds, signal_resolutions,
                std::shared_ptr<const ProcessSignalRemap> { }, process_id);
        const auto* const binding = executor->program_access_binding();
        require(binding != nullptr && binding->valid()
                && executor->region_kernel_equivalent()
                && executor->cohort_manages_process_state()
                && executor->region_kernel_completion_has_no_persistent_registers(),
            "every real member executor satisfies the native V2 contract");
        interpreter.set_process_executor(process_id, std::move(executor));
    };

    Process producer;
    producer.id = producer_id;
    producer.name = "prefix_budget_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { input, EdgeKind::any } };
    producer.operations.emplace_back(ReadSignal { 0U, input });
    for (const auto signal : internal_signals) {
        producer.driver_regions.push_back({ signal, 0U, 0U, true });
        producer.operations.emplace_back(WriteUpdate { signal, 0U,
            SignalUpdateDomain::systemverilog_active });
    }
    producer.operations.emplace_back(WaitSensitivity { });
    producer.operations.emplace_back(Jump { 0U });
    add_member(std::move(producer));

    for (std::size_t reader_index = 0U;
         reader_index < reader_count; ++reader_index) {
        Process reader;
        reader.id = static_cast<ProcessId>(first_reader_id + reader_index);
        reader.name = "prefix_budget_reader_"
            + std::to_string(reader_index);
        reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        reader.register_count = static_cast<std::uint32_t>(internal_count);
        reader.register_value_kinds.assign(internal_count, ValueKind::logic4);
        reader.static_sensitivity.reserve(internal_count);
        reader.operations.reserve(internal_count * 2U + 2U);
        for (std::size_t index = 0U; index < internal_signals.size(); ++index) {
            const auto signal = internal_signals[index];
            reader.static_sensitivity.push_back(
                { signal, EdgeKind::any });
            reader.operations.emplace_back(ReadSignal {
                static_cast<RegisterId>(index), signal });
        }
        for (RegisterId index = 1U;
             index < static_cast<RegisterId>(internal_count); ++index) {
            reader.operations.emplace_back(Binary {
                BinaryOperator::bit_xor, 0U, 0U, index });
        }
        const auto output = output_signals[reader_index];
        reader.driver_regions = { { output, 0U, 0U, true } };
        reader.operations.emplace_back(WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active });
        reader.operations.emplace_back(WaitSensitivity { });
        reader.operations.emplace_back(Jump { 0U });
        add_member(std::move(reader));
    }

    Process clock;
    clock.id = clock_id;
    clock.name = "prefix_budget_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.register_value_kinds = { ValueKind::logic4 };
    clock.driver_regions = { { input, 0U, 0U, true } };
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::zero } },
        WriteBlocking { input, 0U },
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::one } },
        WriteBlocking { input, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == clock_id,
        "the timed source remains outside the four-member V2 component");

    if (provider) {
        interpreter.set_region_kernel_backend_provider(provider);
    }
    interpreter.start();
    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::time_limit
            || startup.status == RunStatus::completed,
        "the idle setup stops before the first budget stimulus");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto detached_value = [](const PackedLogic4& value) {
        PackedLogic4 detached { value.width(), Logic4::zero };
        for (std::size_t bit = 0U; bit < value.width(); ++bit) {
            detached.set(bit, value.get(bit));
        }
        return detached;
    };
    const auto capture = [&]() {
        std::vector<AliasSignalState> states;
        states.reserve(signal_count);
        for (std::size_t index = 0U; index < signal_count; ++index) {
            const auto signal = static_cast<SignalId>(index);
            const auto owner = signal == input
                ? clock_id
                : signal <= internal_signals.back()
                    ? producer_id
                    : static_cast<ProcessId>(first_reader_id
                        + signal - output_begin);
            auto state = capture_boundary_signal_state(
                implementation, signal, owner);
            state.current = detached_value(state.current);
            state.last = detached_value(state.last);
            state.stored = detached_value(state.stored);
            if (state.boundary_owner_value) {
                state.boundary_owner_value
                    = detached_value(*state.boundary_owner_value);
            }
            states.push_back(std::move(state));
        }
        return states;
    };
    result.frames.push_back(capture());
    const auto initial_source_value = PackedLogic4 { 1U, Logic4::z };
    require(result.frames.front()[input].current == initial_source_value,
        "the undriven timed source begins at Z before the first zero write");

    std::shared_ptr<FrontierRuntime> retained_runtime;
    std::size_t component = std::numeric_limits<std::size_t>::max();
    std::uint64_t retained_generation { };
    if (provider) {
        component = implementation.region_component_by_process.at(producer_id);
        require(component != std::numeric_limits<std::size_t>::max(),
            "the many-write producer receives a certified component");
        for (ProcessId process = first_reader_id;
             process < first_reader_id + reader_count; ++process) {
            require(implementation.region_component_by_process.at(process)
                    == component,
                "all three multi-input readers join the producer component");
        }
        retained_runtime
            = implementation.region_frontier_runtime_by_component.at(component);
        require(retained_runtime && retained_runtime->backend
                && retained_runtime->backend->executor
                && retained_runtime->backend->executor->step_entry() != nullptr,
            "the host fixture exercises a genuine generated V2 entry");
        const auto& kernel = retained_runtime->backend->kernel;
        const auto& layout = retained_runtime->backend->executor->layout();
        std::size_t producer_member_index = layout.member_count;
        if (layout.members != nullptr) {
            for (std::size_t index = 0U;
                 index < layout.member_count; ++index) {
                if (layout.members[index].process_id == producer_id) {
                    producer_member_index = index;
                    break;
                }
            }
        }
        std::cerr << "fsim-test: native-prefix-budget-layout members="
                  << kernel.members.size()
                  << " internal_signals=" << kernel.internal_signals.size()
                  << " outputs=" << kernel.outputs.size()
                  << " layout_members=" << layout.member_count
                  << " writes=" << layout.write_site_count
                  << " fanout_edges=" << layout.fanout_edge_count
                  << " max_fanout=" << layout.max_commit_fanout_events
                  << " producer_bound="
                  << (layout.max_member_staged_event_counts != nullptr
                          && producer_member_index < layout.member_count
                      ? layout.max_member_staged_event_counts[producer_member_index]
                      : UINT32_MAX)
                  << " layout_event_capacity=" << layout.staged_event_capacity
                  << " frame_event_capacity="
                  << retained_runtime->frame.staged_event_capacity << '\n';
        require(kernel.members.size() == member_count
                && kernel.internal_signals.size() == internal_count + reader_count
                && kernel.outputs.size() == internal_count + reader_count
                && layout.member_count == member_count
                && layout.members != nullptr
                && producer_member_index < layout.member_count
                && layout.write_site_count == internal_count + reader_count
                && layout.fanout_edge_count == internal_count * reader_count
                && layout.max_commit_fanout_events == reader_count
                && layout.max_member_staged_event_counts != nullptr
                && layout.max_member_staged_event_counts[producer_member_index]
                    == internal_count
                && layout.staged_event_capacity
                    == layout.write_site_count + layout.fanout_edge_count
                && layout.staged_event_capacity > frame_event_budget
                && retained_runtime->frame.staged_event_capacity
                    == frame_event_budget,
            "31 commits at fanout three exceed the 64-event frame only"
            " in aggregate");
        result.runtime_prepared = true;
        retained_generation = implementation.region_runtime_generation;
        require(!retained_runtime->invalidated
                && retained_runtime->runtime_generation == retained_generation,
            "quiet startup retains a valid native runtime for the timed prefix test");
    } else {
        result.runtime_prepared
            = std::ranges::none_of(
                implementation.region_frontier_runtime_by_component,
                [](const auto& runtime) { return runtime != nullptr; })
            && std::ranges::none_of(
                implementation.region_activation_programs,
                [](const auto& program) { return program.has_value(); });
        require(result.runtime_prepared,
            "the reference run uses the ordinary checked interpreter route");
    }

    for (std::size_t transition = 0U; transition < 2U; ++transition) {
        const auto target_time
            = static_cast<SimulationTick>(transition + 1U);
        const auto before_dispatches = retained_runtime
            ? retained_runtime->native_member_dispatches : 0U;
        const auto run_result = interpreter.run(target_time);
        require(run_result.status == RunStatus::time_limit
                || run_result.status == RunStatus::completed,
            "both timed producer transitions reach a quiet point");
        if (retained_runtime) {
            result.native_dispatch_deltas[transition]
                = retained_runtime->native_member_dispatches
                    - before_dispatches;
            require(result.native_dispatch_deltas[transition]
                    >= 1U + reader_count,
                "the retained V2 runtime executes the producer and every fanout reader");
            result.runtime_retained
                = implementation.region_frontier_runtime_by_component.at(
                    component).get() == retained_runtime.get()
                && implementation.region_runtime_generation
                    == retained_generation;
            result.runtime_remained_valid
                = result.runtime_retained && !retained_runtime->invalidated;
            require(result.runtime_remained_valid,
                "event-budget trimming keeps one valid native runtime through its pending suffix");
        }

        auto states = capture();
        const auto& previous = result.frames.back();
        for (std::size_t index = 0U; index < states.size(); ++index) {
            require(states[index].raw_driver_count == 1U
                    && states[index].event.has_value()
                    && states[index].event->first == target_time
                    && states[index].transaction.has_value()
                    && states[index].transaction->first == target_time
                    && states[index].value_revision
                        == previous[index].value_revision + 1U
                    && states[index].last == previous[index].current
                    && states[index].stamp.origin.process_domain
                        == ProcessSchedulingDomain::systemverilog
                    && states[index].stamp.origin.phase == SchedulerPhase::active,
                "each single-owner signal commits exactly once at its original active key");
        }
        const auto expected_bit = transition == 0U
            ? Logic4::zero : Logic4::one;
        const auto expected_value = PackedLogic4 { 1U, expected_bit };
        if (transition == 0U) {
            require(previous[input].current == initial_source_value
                    && states[input].current == expected_value,
                "the first timed write changes the source from Z to zero");
        }
        require(states[input].current == expected_value
                && states[input].stored == expected_value
                && states[input].boundary_owner_value
                    == std::optional { expected_value },
            "the source retains its exact value and original clock owner");
        for (const auto signal : internal_signals) {
            const auto& state = states[signal];
            require(state.current == expected_value
                    && state.stored == expected_value
                    && state.boundary_owner_value
                        == std::optional { expected_value },
                "every internal commit retains current, stored, and original-owner values");
        }
        for (const auto signal : output_signals) {
            const auto& state = states[signal];
            require(state.current == expected_value
                    && state.stored == expected_value
                    && state.boundary_owner_value
                        == std::optional { expected_value },
                "odd-parity readers publish the exact value once through their own outputs");
        }
        result.frames.push_back(std::move(states));
    }
    return result;
}

void check_internal_commit_event_budget_prefix(
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit& process_jit,
    const std::shared_ptr<RegionKernelBackendProvider>& o0_provider,
    fsim::compiler::LlvmJit& o0_process_jit)
{
    InternalCommitBudgetRun checked;
    {
        ScopedEnvironment region_disabled {
            "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
        ScopedEnvironment local_wave_disabled {
            "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
        checked = run_internal_commit_budget_case(nullptr, nullptr);
    }
    ScopedEnvironment region_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };

    const auto optimized = run_internal_commit_budget_case(
        std::make_shared<FrontierOnlyProvider>(provider), &process_jit);
    const auto unoptimized = run_internal_commit_budget_case(
        std::make_shared<FrontierOnlyProvider>(o0_provider), &o0_process_jit);
    require(checked.runtime_prepared && optimized.runtime_prepared
            && unoptimized.runtime_prepared
            && optimized.runtime_retained && unoptimized.runtime_retained
            && optimized.runtime_remained_valid
            && unoptimized.runtime_remained_valid,
        "both LLVM levels retain the same prepared runtime after bounded commit prefixes");
    for (const auto* const compiled : { &optimized, &unoptimized }) {
        require(compiled->frames.size() == checked.frames.size(),
            "checked and native event-budget routes observe the same scheduler cuts");
        for (std::size_t frame = 0U; frame < checked.frames.size(); ++frame) {
            require(compiled->frames[frame].size()
                    == checked.frames[frame].size(),
                "checked and native captures retain every signal slot");
            for (std::size_t signal = 0U;
                 signal < checked.frames[frame].size(); ++signal) {
                require(same_alias_signal_state(
                            checked.frames[frame][signal],
                            compiled->frames[frame][signal]),
                    "checked and native prefixes preserve values, LAST,"
                    " owners, events, and transactions");
            }
        }
    }
}

} // namespace
} // namespace fsim::tests::runtime

int main()
{
    try {
        using namespace fsim::compiler;
        using namespace fsim::tests::runtime;
        TemporaryCacheDirectory cache;
        LlvmJitOptions options;
        options.debug_instrumentation = false;
        options.cache_directory = cache.path();
        LlvmJit process_jit { options };
        auto provider = fsim::app::application_detail::
            make_llvm_region_kernel_backend_provider(
                std::move(options), "runtime-native-frontier-boundary-tests");
        require(provider != nullptr,
            "the production application provider is available to runtime tests");
        check_malformed_layout_rejected_before_frontier_publication(provider,
            MalformedFrontierLayoutKind::bad_header,
            "an invalid V2 layout header declines before frontier frame publication");
        check_malformed_layout_rejected_before_frontier_publication(provider,
            MalformedFrontierLayoutKind::duplicate_member_process,
            "a duplicate layout ProcessId declines before frontier frame publication");
        check_malformed_layout_rejected_before_frontier_publication(provider,
            MalformedFrontierLayoutKind::swapped_member_processes,
            "out-of-order layout ProcessIds decline before frontier frame publication");
        {
            NativeBoundaryFixture receipt_fixture { 1U, provider, true, true };
        }
        check_successful_boundary_commit_acknowledges_once(provider);
        check_direct_partial_boundary_commit_matches_checked_reference(provider);
        check_direct_partial_boundary_fallback_consumes_once(provider);
        check_alias_boundary_commit_reentry_matches_checked_reference(provider);
        check_alias_boundary_commit_reentry_matches_checked_reference(
            provider, true);
        check_alias_boundary_materialization_failure_is_precommit(provider);
        check_alias_proxy_slice_commit_failure_is_precommit(provider);
        check_alias_boundary_observer_exception_fails_stop_without_replay(
            provider);
        check_alias_boundary_observer_exception_fails_stop_without_replay(
            provider, true);
        check_binder_scalar_and_wide_boundary_inputs_are_direct(provider);
        check_binder_dense_pending_boundary_is_allocation_free(provider);
        check_binder_roles_metadata_and_declines_are_exact(provider);
        check_invalid_boundary_descriptor_fails_closed(provider);
        check_malformed_fallback_pointer_declines_before_read(provider);
        check_proxy_slice_descriptor_fallback_consumes_once(provider);
        check_wide_materialization_failure_is_terminal(provider);
        check_checked_publication_exception_is_terminal(provider);
        check_internal_range_v2_readiness(provider, process_jit);
        check_alias_sorted_geometry_oracle();
        check_alias_sorted_geometry_checked_fallback(provider);
        check_event_bearing_entry_does_not_prime_alias_certificate(provider);
        TemporaryCacheDirectory o0_cache;
        LlvmJitOptions o0_options;
        o0_options.optimization = JitOptimizationLevel::o0;
        o0_options.debug_instrumentation = false;
        o0_options.cache_directory = o0_cache.path();
        LlvmJit o0_process_jit { o0_options };
        auto o0_provider = fsim::app::application_detail::
            make_llvm_region_kernel_backend_provider(std::move(o0_options),
                "runtime-native-frontier-boundary-tests-o0");
        require(o0_provider != nullptr,
            "the O0 production LLVM provider is available to runtime tests");
        check_internal_range_v2_readiness(o0_provider, o0_process_jit);
        check_partial_boundary_staged_event_slice_publication(
            provider, process_jit, o0_provider, o0_process_jit);
        check_internal_commit_event_budget_prefix(
            provider, process_jit, o0_provider, o0_process_jit);
        std::cout << "Native frontier boundary tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure();
        std::cerr << "Native frontier boundary test failure: "
                  << exception.what() << '\n';
        return 1;
    }
}
