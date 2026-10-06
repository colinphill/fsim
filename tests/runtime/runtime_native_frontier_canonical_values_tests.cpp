// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_region_kernel_backend.hpp"
#include "../../src/app/application_internal.hpp"
#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../../src/runtime/simir_region_frontier_trusted_entry.hpp"
#include <fsim/compiler/llvm_jit.hpp>
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using RuntimeImplementation = OwnedDriverDemotionTestAccess::Implementation;
using FrontierRuntime = RuntimeImplementation::RegionFrontierComponentRuntime;

constexpr SignalId input_signal = 0U;
constexpr SignalId internal_signal = 1U;
constexpr SignalId output_signal = 2U;
constexpr SignalId toggle_signal = 3U;
constexpr SignalId anchor_output_signal = 4U;
constexpr std::uint32_t anchor_width = 8U;
constexpr ProcessId producer_process = 0U;
constexpr ProcessId consumer_process = 1U;
constexpr ProcessId clock_process = 2U;
constexpr std::string_view logic9_symbols { "UX01ZWLH-" };

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

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
        static_cast<void>(::_putenv_s(name_.c_str(), value == nullptr ? "" : value));
#else
        if (value == nullptr) {
            static_cast<void>(::unsetenv(name_.c_str()));
        } else {
            static_cast<void>(::setenv(name_.c_str(), value, 1));
        }
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

class TemporaryCacheDirectory final {
public:
    TemporaryCacheDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path()
            / ("fsim-native-frontier-canonical-values-"
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

class AliasOnlyFrontierBackend final
    : public RegionFrontierBackend
    , public fsim::runtime::simir::detail::RegionFrontierTrustedEntryCapability {
public:
    explicit AliasOnlyFrontierBackend(
        std::unique_ptr<RegionFrontierBackend> backend)
        : backend_ { std::move(backend) }
    {
    }

    [[nodiscard]] RegionFrontierStepEntryV2 step_entry()
        const noexcept override
    {
        return backend_->step_entry();
    }

    [[nodiscard]] const RegionFrontierLayoutV2& layout()
        const noexcept override
    {
        return backend_->layout();
    }

    [[nodiscard]] fsim::runtime::simir::detail::RegionFrontierTrustedEntryView trusted_entry()
        const noexcept override
    {
        const auto* const capability
            = dynamic_cast<const fsim::runtime::simir::detail::RegionFrontierTrustedEntryCapability*>(
                backend_.get());
        return capability != nullptr ? capability->trusted_entry()
                                     : fsim::runtime::simir::detail::RegionFrontierTrustedEntryView { };
    }

private:
    std::unique_ptr<RegionFrontierBackend> backend_;
};

class AliasOnlyProvider final
    : public RegionKernelBackendProvider
    , public RegionFrontierBackendProvider {
public:
    explicit AliasOnlyProvider(
        std::shared_ptr<RegionKernelBackendProvider> provider)
        : provider_ { std::move(provider) }
        , frontier_provider_ {
            dynamic_cast<RegionFrontierBackendProvider*>(provider_.get()) }
    {
        require(provider_ != nullptr && frontier_provider_ != nullptr,
            "the alias-only route wraps the real in-tree LLVM frontier provider");
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
        return std::make_unique<AliasOnlyFrontierBackend>(std::move(backend));
    }

private:
    std::shared_ptr<RegionKernelBackendProvider> provider_;
    RegionFrontierBackendProvider* frontier_provider_ { };
};

[[nodiscard]] PackedLogic4 detached_copy(const PackedLogic4& source)
{
    if (source.is_logic9()) {
        return PackedLogic4::from_logic9_word_planes(source.width(),
            source.logic9_plane_words(0U),
            source.logic9_plane_words(1U),
            source.logic9_plane_words(2U),
            source.logic9_plane_words(3U));
    }
    return PackedLogic4::from_word_planes(source.width(),
        source.aval_words(), source.bval_words());
}

[[nodiscard]] std::array<PackedLogic4, logic9_symbols.size()>
make_logic9_patterns(const std::uint32_t width)
{
    std::array<PackedLogic4, logic9_symbols.size()> result;
    for (std::size_t phase = 0U; phase < result.size(); ++phase) {
        PackedLogic4 pattern { width, Logic4::x };
        for (std::size_t bit = 0U; bit < width; ++bit) {
            const auto code = parse_logic9(
                logic9_symbols[(bit + phase) % logic9_symbols.size()]);
            require(code.has_value(),
                "the Logic9 alphabet contains only valid canonical codes");
            pattern.set_logic9(bit, *code);
        }
        result[phase] = std::move(pattern);
    }

    std::array<bool, logic9_symbols.size()> observed { };
    for (std::size_t phase = 0U; phase < result.size(); ++phase) {
        const auto& pattern = result[phase];
        for (std::size_t bit = 0U; bit < pattern.width(); ++bit) {
            const auto expected_character = logic9_symbols[
                (bit + phase) % logic9_symbols.size()];
            const auto character = to_char(pattern.get_logic9(bit));
            require(character == expected_character,
                "the generated packed stimulus preserves each Logic9 code");
            const auto position = logic9_symbols.find(character);
            require(position != std::string_view::npos,
                "the wide Logic9 pattern uses only canonical symbols");
            observed[position] = true;
        }
    }
    require(std::ranges::all_of(observed, [](const bool value) { return value; }),
        "the Logic9 stimulus set covers all nine canonical symbols");
    return result;
}

struct A4RoleSnapshot final {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 owner;
};

struct SignalSnapshot final {
    PackedLogic4 current;
    PackedLogic4 last;
    PackedLogic4 stored;
    PackedLogic4 owner;
    std::optional<A4RoleSnapshot> a4_roles;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp stamp;
    std::uint64_t revision { };
};

using SignalFrame = std::vector<SignalSnapshot>;

[[nodiscard]] bool same_stamp(const SignalEventSchedulingStamp& left,
    const SignalEventSchedulingStamp& right) noexcept
{
    return left.origin.process_domain == right.origin.process_domain
        && left.origin.phase == right.origin.phase
        && left.systemverilog_round == right.systemverilog_round;
}

[[nodiscard]] bool same_signal_state(const SignalSnapshot& left,
    const SignalSnapshot& right) noexcept
{
    return left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.owner == right.owner
        && left.event == right.event
        && left.transaction == right.transaction
        && same_stamp(left.stamp, right.stamp)
        && left.revision == right.revision;
}

[[nodiscard]] SignalSnapshot capture_signal(
    RuntimeImplementation& implementation,
    const SignalId signal,
    const ProcessId owner)
{
    SignalSnapshot result;
    if (auto* const authoritative
        = implementation.region_authoritative_state_for_signal(signal);
        authoritative != nullptr && authoritative->valid()) {
        // A4 owns these roles while the component is authoritative. Packed
        // driven/driver mirrors may remain lazy until the public read below.
        const auto& values = authoritative->values();
        result.a4_roles = A4RoleSnapshot {
            detached_copy(values.current(signal)),
            detached_copy(values.previous(signal)),
            detached_copy(values.stored(signal)),
            detached_copy(values.owner_value(signal, owner)),
        };
    }

    // Read the public logical values after snapshotting authoritative roles.
    // This can materialize a lazy packed mirror, so raw driver/stored vectors
    // are not treated as authoritative while A4 owns the signal.
    result.current = detached_copy(implementation.logical_signal_value(signal));
    result.last = detached_copy(implementation.logical_signal_last_value(signal));
    if (result.a4_roles) {
        result.stored = result.a4_roles->stored;
        result.owner = result.a4_roles->owner;
        require(result.a4_roles->current == result.current
                && result.a4_roles->previous == result.last,
            "the bound A4 Logic9 CURRENT/LAST roles match public observation");
    } else {
        // Public logical access may materialize the deferred packed mirror;
        // read raw driver/storage rows only after that materialization.
        result.stored = detached_copy(implementation.driven_values.at(signal));
        if (implementation.signals.at(signal).resolution
            == ResolutionKind::none) {
            // Unresolved commits store the sole owner's value directly;
            // register_driver intentionally does not create a raw driver row.
            require(implementation.stable_single_writer_processes.at(signal)
                        == owner
                    && implementation.signal_writer_counts.at(signal) == 1,
                "the unresolved signal retains its exact unique declared owner");
            result.owner = detached_copy(
                implementation.underlying_driver_value(owner, signal));
        } else {
            const auto* const driver
                = implementation.driver_values.at(signal).find(owner);
            require(driver != nullptr,
                "the resolved signal retains an explicit row for its owner");
            result.owner = detached_copy(driver->value);
        }
    }

    result.event = implementation.signal_events.at(signal);
    result.transaction = implementation.signal_transactions.at(signal);
    result.stamp = implementation.signal_event_scheduling_stamps.at(signal);
    result.revision = implementation.signal_value_revisions.at(signal);
    return result;
}

[[nodiscard]] SignalFrame capture_frame(RuntimeImplementation& implementation,
    const bool boundary_logic9)
{
    SignalFrame frame {
        capture_signal(implementation, input_signal, clock_process),
        capture_signal(implementation, internal_signal, producer_process),
        capture_signal(implementation, output_signal, consumer_process),
    };
    if (boundary_logic9) {
        frame.push_back(capture_signal(implementation, toggle_signal, clock_process));
        frame.push_back(capture_signal(implementation, anchor_output_signal, consumer_process));
    }
    return frame;
}

struct RouteSnapshot final {
    std::uint64_t native_member_dispatches { };
    std::uint64_t alias_checked_entries { };
    std::uint64_t alias_trusted_entries { };
    std::uint64_t canonical_values_only_entries { };
    std::uint64_t alias_and_canonical_values_entries { };
    std::uint64_t alias_full_collector_calls { };
    std::uint64_t alias_bind_proof_attempts { };
    std::uint64_t alias_bind_proof_reuse_hits { };
    std::uint64_t alias_bind_proof_plane_misses { };
    std::uint64_t alias_bind_proof_unconfirmed_task_count_misses { };

    [[nodiscard]] std::uint64_t canonical_entries() const noexcept
    {
        return canonical_values_only_entries
            + alias_and_canonical_values_entries;
    }
};

[[nodiscard]] std::shared_ptr<FrontierRuntime> runtime_for_process(
    RuntimeImplementation& implementation, const ProcessId process)
{
    if (process >= implementation.region_component_by_process.size()) {
        return { };
    }
    const auto component = implementation.region_component_by_process[process];
    if (component >= implementation.region_frontier_runtime_by_component.size()) {
        return { };
    }
    return implementation.region_frontier_runtime_by_component[component];
}

void exercise_canonical_receipt_lifecycle(FrontierRuntime& runtime)
{
    require(runtime.try_enter(),
        "the idle Logic9 component accepts the direct receipt helper check");
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    struct EntryGuard final {
        FrontierRuntime& runtime;
        AuthoritativeSignalPlanes::FrontierWriteLease& lease;
        ~EntryGuard()
        {
            runtime.end_alias_bind_lease();
            lease.release();
            runtime.leave();
        }
    } entry_guard { runtime, lease };

    auto& values = runtime.authoritative_state->values();
    require(values.try_acquire_frontier_write_lease(values.revision(),
                runtime.writable_signals, lease,
                runtime.writable_layout_indices),
        "the receipt witness acquires the component's certified write lease");
    const auto& layout = runtime.backend->executor->layout();
    const auto* const capability
        = dynamic_cast<const fsim::runtime::simir::detail::RegionFrontierCanonicalValuesEntryCapability*>(
            runtime.backend->executor.get());
    require(capability != nullptr,
        "the in-tree LLVM executor exposes its private canonical-values entries");
    const auto view = capability->canonical_values_entries();
    require(view.layout == &layout && view.canonical_values_entry != nullptr,
        "the private canonical entry is bound to the runtime's exact layout");

    const auto bind = [&] {
        require(runtime.bind_frame_planes_and_metadata(lease),
            "a fresh checked frame bind can mint the canonical-values receipt");
        require(runtime.canonical_values_binding_receipt.valid,
            "the complete active-lease bind issues a receipt");
    };
    const auto consume = [&] {
        return runtime.consume_canonical_values_binding_receipt(
            *runtime.backend, layout, view.canonical_values_entry,
            view.alias_and_canonical_values_entry, lease);
    };

    bind();
    runtime.clear_canonical_values_binding_receipt();
    require(!consume() && !runtime.canonical_values_binding_receipt.valid,
        "a missing receipt refuses canonical selection and stays cleared");

    bind();
    require(consume() && !runtime.canonical_values_binding_receipt.valid,
        "an exact fresh bind consumes its receipt once");
    require(!consume(), "a consumed canonical-values receipt cannot be reused");

    bind();
    runtime.canonical_values_binding_receipt.component_generation
        ^= UINT64_C(1);
    require(!consume() && !runtime.canonical_values_binding_receipt.valid,
        "a stale generation fails closed and clears its receipt");

    bind();
    require(runtime.canonical_values_binding_receipt.valid,
        "a new successful bind remints after a stale receipt was rejected");

    const auto* const shapes_capability
        = dynamic_cast<const fsim::runtime::simir::detail::RegionFrontierDescriptorShapesEntryCapability*>(
            runtime.backend->executor.get());
    require(shapes_capability != nullptr,
        "the authentic LLVM bridge exposes independent descriptor-shape authority");
    const auto shapes_view = shapes_capability->descriptor_shapes_entry();
    require(shapes_view.entry != nullptr && shapes_view.layout == &layout,
        "the descriptor-shape function belongs to this exact builtin layout");
    bool shapes_valid { };
    const auto consume_shapes = [&](const RegionFrontierLayoutV2& candidate_layout,
                                    const RegionFrontierStepEntryV2 candidate_entry) {
        shapes_valid = true;
        return runtime.consume_canonical_values_binding_receipt(
            *runtime.backend, candidate_layout, view.canonical_values_entry,
            view.alias_and_canonical_values_entry, lease,
            candidate_entry, &shapes_valid);
    };

    require(consume_shapes(layout, shapes_view.entry) && shapes_valid
            && runtime.descriptor_shapes_binding_receipt.entry == nullptr,
        "the fresh builtin bind consumes independently named shape authority once");
    require(!consume_shapes(layout, shapes_view.entry) && !shapes_valid,
        "a second call cannot reuse the consumed shape authority");

    bind();
    require(consume_shapes(layout, view.alias_and_canonical_values_entry)
            && !shapes_valid,
        "canonical authority cannot substitute an older function for the shape entry");

    bind();
    const auto wrong_layout = layout;
    require(!consume_shapes(wrong_layout, shapes_view.entry) && !shapes_valid,
        "even an equivalent layout at another address has no shape authority");

    bind();
    runtime.canonical_values_binding_receipt.authoritative_revision ^= UINT64_C(1);
    require(!consume_shapes(layout, shapes_view.entry) && !shapes_valid,
        "a stale backing revision clears canonical and shape authority together");

    bind();
    const auto original_storage = runtime.descriptor_shapes_storage_certificate;
    runtime.descriptor_shapes_storage_certificate.pending_plane_word_count
        ^= std::size_t { 1U };
    require(consume_shapes(layout, shapes_view.entry) && !shapes_valid,
        "wrong pending payload extents decline shape authority without broadening canonical trust");
    runtime.descriptor_shapes_storage_certificate = original_storage;

    bind();
    runtime.descriptor_shapes_storage_certificate.pending_plane_words = nullptr;
    require(consume_shapes(layout, shapes_view.entry) && !shapes_valid,
        "a changed pending payload base cannot reuse its prepared shape certificate");
    runtime.descriptor_shapes_storage_certificate = original_storage;

    bind();
    const auto slot_count = runtime.frame.signal_slot_count;
    runtime.frame.signal_slot_count ^= 1U;
    require(!runtime.bind_frame_planes_and_metadata(lease)
            && runtime.descriptor_shapes_binding_receipt.entry == nullptr,
        "a failed new bind clears the previous shape authority before returning");
    runtime.frame.signal_slot_count = slot_count;
    require(!consume_shapes(layout, shapes_view.entry) && !shapes_valid,
        "a failed binder cannot leave a reusable old shape receipt");

    bind();
    lease.release();
    require(!consume_shapes(layout, shapes_view.entry) && !shapes_valid,
        "released write-lease authority cannot cross a callback or rebind");
    require(values.try_acquire_frontier_write_lease(values.revision(),
                runtime.writable_signals, lease,
                runtime.writable_layout_indices),
        "the receipt witness reacquires a real lease after the expiry check");
    bind();
    require(consume_shapes(layout, shapes_view.entry) && shapes_valid,
        "a fresh complete rebind restores exact shape authority after expiry");
}

void exercise_alias_task_prefix_geometry(FrontierRuntime& runtime)
{
    require(runtime.alias_certificate_valid
            && runtime.alias_certificate_context.layout != nullptr,
        "the active builtin runtime retains its confirmed alias geometry");
    const auto context = runtime.alias_certificate_context;
    const auto original_task_count
        = static_cast<std::size_t>(runtime.frame.scheduler_task_count);
    require(original_task_count != 0U
            && original_task_count < runtime.frame.scheduler_task_capacity
            && original_task_count
                < runtime.alias_certificate_task_count_valid.size(),
        "the quiet builtin frame has a nonempty interval and a larger test extent");

    const auto confirm = runtime.alias_certificate_task_count_valid;
    for (std::size_t prefix = 0U; prefix <= original_task_count; ++prefix) {
        require(confirm[prefix] != 0U,
            "an accepted exact geometry interval covers all shorter prefixes");
    }

    const auto original_cursor
        = static_cast<std::size_t>(runtime.frame.scheduler_task_cursor);
    const auto shorter_count = original_task_count - 1U;
    runtime.frame.scheduler_task_count
        = static_cast<std::uint32_t>(shorter_count);
    runtime.frame.scheduler_task_cursor = static_cast<std::uint32_t>(
        std::min(original_cursor, shorter_count));
    // This direct query asks only about range geometry. Real entries still
    // validate the scheduler-authored task, cursor, and key data.
    const bool shorter_prefix_matches
        = runtime.alias_certificate_matches(context);
    runtime.frame.scheduler_task_count
        = static_cast<std::uint32_t>(original_task_count);
    runtime.frame.scheduler_task_cursor
        = static_cast<std::uint32_t>(original_cursor);
    require(shorter_prefix_matches
            && runtime.alias_certificate_task_count_valid[shorter_count] != 0U
            && runtime.alias_certificate_valid,
        "a shorter interval matches the confirmed prefix without invalidating geometry");

    std::size_t unknown_larger_count
        = runtime.alias_certificate_task_count_valid.size();
    for (std::size_t candidate = original_task_count + 1U;
         candidate <= runtime.frame.scheduler_task_capacity; ++candidate) {
        if (runtime.alias_certificate_task_count_valid[candidate] == 0U) {
            unknown_larger_count = candidate;
            break;
        }
    }
    require(unknown_larger_count
                < runtime.alias_certificate_task_count_valid.size(),
        "the bounded fixture retains a larger extent without a geometry proof");
    const auto confirmed_before_larger_probe
        = runtime.alias_certificate_task_count_valid;
    runtime.frame.scheduler_task_count
        = static_cast<std::uint32_t>(unknown_larger_count);
    runtime.frame.scheduler_task_cursor = static_cast<std::uint32_t>(
        std::min(original_cursor, unknown_larger_count));
    const bool larger_prefix_matches
        = runtime.alias_certificate_matches(context);
    runtime.frame.scheduler_task_count
        = static_cast<std::uint32_t>(original_task_count);
    runtime.frame.scheduler_task_cursor
        = static_cast<std::uint32_t>(original_cursor);
    require(!larger_prefix_matches && runtime.alias_certificate_valid
            && runtime.alias_certificate_task_count_valid
                == confirmed_before_larger_probe
            && runtime.alias_certificate_matches(context),
        "an unconfirmed larger interval declines without dropping shorter proofs");

    const auto internal_plane = std::ranges::find(runtime.planes,
        internal_signal, &RegionFrontierPlaneV2::signal_id);
    require(internal_plane != runtime.planes.end()
            && internal_plane->plane_count != 0U
            && internal_plane->word_count != 0U
            && internal_plane->current_planes[0U] != nullptr,
        "the fixture retains an internal plane range to test invalidation");
    auto* const original_current_plane = internal_plane->current_planes[0U];
    internal_plane->current_planes[0U] += internal_plane->word_count;
    const bool changed_plane_matches
        = runtime.alias_certificate_matches(context);
    internal_plane->current_planes[0U] = original_current_plane;
    require(!changed_plane_matches && !runtime.alias_certificate_valid
            && std::ranges::none_of(
                runtime.alias_certificate_task_count_valid,
                [](const std::uint8_t valid) { return valid != 0U; }),
        "a changed signal-plane pointer clears all shorter-prefix proofs");
}

[[nodiscard]] RouteSnapshot route_snapshot(
    const std::shared_ptr<FrontierRuntime>& runtime) noexcept
{
    if (!runtime) {
        return { };
    }
    return {
        runtime->native_member_dispatches,
        runtime->alias_checked_entries,
        runtime->alias_trusted_entries,
        runtime->canonical_values_only_entries,
        runtime->alias_and_canonical_values_entries,
        runtime->alias_full_collector_calls,
        runtime->alias_bind_proof_attempts,
        runtime->alias_bind_proof_reuse_hits,
        runtime->alias_bind_proof_plane_misses,
        runtime->alias_bind_proof_unconfirmed_task_count_misses,
    };
}

struct RunOutcome final {
    std::vector<SignalFrame> frames;
    RouteSnapshot final_route;
    RouteSnapshot route_before_fallback;
    bool actual_fallback { };
};

[[nodiscard]] RunOutcome run_case(const std::uint32_t width,
    const bool use_frontier, const bool exercise_fallback,
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit* const process_jit,
    const bool require_canonical_route = true,
    const bool boundary_logic9 = true)
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", use_frontier ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", use_frontier ? "1" : "0" };
    ScopedEnvironment wide_single_owner {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT",
        use_frontier ? "1" : "0" };
    ScopedEnvironment wide_disjoint_owner {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };

    const auto patterns = make_logic9_patterns(width);
    const bool exercise_boundary_plane_replacement
        = width == 129U && boundary_logic9 && require_canonical_route;
    // The production SV wave admits Logic4 internals. Keep Logic9 at the
    // public boundaries and use a stateless, semantically observed anchor.
    // The original all-Logic9 topology remains a checked-fallback control.
    const bool expect_native_route = use_frontier && boundary_logic9;
    const auto anchor = [](const std::size_t phase) {
        return PackedLogic4 { anchor_width,
            phase % 2U == 0U ? Logic4::zero : Logic4::one };
    };
    std::vector<std::uint32_t> signal_widths { width,
        boundary_logic9 ? anchor_width : width, width };
    std::vector<ValueKind> signal_kinds { ValueKind::logic9,
        boundary_logic9 ? ValueKind::logic4 : ValueKind::logic9,
        ValueKind::logic9 };
    std::vector<ResolutionKind> signal_resolutions {
        ResolutionKind::std_logic,
        boundary_logic9 ? ResolutionKind::sv_wire : ResolutionKind::none,
        ResolutionKind::std_logic };
    if (boundary_logic9) {
        signal_widths.insert(signal_widths.end(), { anchor_width, anchor_width });
        signal_kinds.insert(signal_kinds.end(), { ValueKind::logic4, ValueKind::logic4 });
        signal_resolutions.insert(signal_resolutions.end(),
            { ResolutionKind::sv_wire, ResolutionKind::sv_wire });
    }

    std::array<std::optional<Process>, 3U> registered_processes;
    Interpreter interpreter;
    require(interpreter.add_signal({ "canonical_values.input",
                patterns.front(), ResolutionKind::std_logic,
                ValueKind::logic9 }) == input_signal,
        "the external Logic9 input is registered at its authenticated slot");
    require(interpreter.add_signal({ "canonical_values.internal",
                boundary_logic9 ? anchor(0U) : patterns.front(),
                signal_resolutions[internal_signal],
                signal_kinds[internal_signal] }) == internal_signal,
        "the private anchor retains its exact width, kind and owner policy");
    require(interpreter.add_signal({ "canonical_values.output",
                patterns.front(), ResolutionKind::std_logic,
                ValueKind::logic9 }) == output_signal,
        "the Logic9 output is registered as a public boundary signal");

    if (boundary_logic9) {
        require(interpreter.add_signal({ "canonical_values.toggle", anchor(0U),
                    ResolutionKind::sv_wire }) == toggle_signal
                && interpreter.add_signal({ "canonical_values.anchor_output",
                    anchor(0U), ResolutionKind::sv_wire }) == anchor_output_signal,
            "the anchor stimulus and observed copy retain dense signal identities");
    }

    const auto add_process = [&](Process process) {
        const auto id = process.id;
        require(id < registered_processes.size(),
            "the Logic9 witness keeps stable storage for every process view");
        auto& registered = registered_processes[id].emplace(std::move(process));
        require(interpreter.add_process(registered) == id,
            "each Logic9 owner retains its expected process identity");
        if (process_jit == nullptr || id == clock_process) {
            return;
        }
        const auto name = registered.name;
        process_jit->add_process(name, registered, signal_widths, signal_kinds);
        const auto handle = process_jit->lookup(name);
        require(static_cast<bool>(handle),
            "the production JIT retains each native member process");
        auto executor = std::make_unique<
            fsim::app::application_detail::LlvmProcessExecutor>(
                *process_jit, handle, registered, signal_widths, signal_kinds,
                signal_resolutions, std::shared_ptr<const ProcessSignalRemap> { }, id);
        const auto* const binding = executor->program_access_binding();
        require(binding != nullptr && binding->valid()
                && executor->region_kernel_equivalent()
                && executor->cohort_manages_process_state()
                && executor->region_kernel_completion_has_no_persistent_registers(),
            "the actual member executor supplies a stateless native cohort contract");
        interpreter.set_process_executor(id, std::move(executor));
    };

    Process producer;
    producer.id = producer_process;
    producer.name = "frontier_canonical_logic9_producer_"
        + std::to_string(width) + (boundary_logic9 ? "_boundary" : "_fallback");
    producer.language_standard = "2017";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 1U;
    producer.register_value_kinds = { signal_kinds[internal_signal] };
    producer.static_sensitivity = {
        { boundary_logic9 ? toggle_signal : input_signal, EdgeKind::any } };
    producer.driver_regions = { { internal_signal, 0U, 0U, true } };
    producer.operations = {
        ReadSignal { 0U, boundary_logic9 ? toggle_signal : input_signal },
        WriteUpdate { internal_signal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    if (!require_canonical_route) {
        producer.name += "_alias";
    }
    add_process(std::move(producer));

    Process consumer;
    consumer.id = consumer_process;
    consumer.name = "frontier_canonical_logic9_consumer_"
        + std::to_string(width) + (boundary_logic9 ? "_boundary" : "_fallback");
    consumer.language_standard = "2017";
    consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    consumer.register_count = 1U;
    consumer.register_value_kinds = { ValueKind::logic9 };
    consumer.static_sensitivity = { { internal_signal, EdgeKind::any } };
    consumer.driver_regions = { { output_signal, 0U, 0U, true } };
    consumer.operations = {
        ReadSignal { 0U, internal_signal },
        WriteUpdate { output_signal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    if (boundary_logic9) {
        consumer.register_count = 2U;
        consumer.register_value_kinds = { ValueKind::logic9, ValueKind::logic4 };
        consumer.driver_regions.push_back({ anchor_output_signal, 0U, 0U, true });
        consumer.operations = {
            ReadSignal { 1U, internal_signal },
            WriteUpdate { anchor_output_signal, 1U,
                SignalUpdateDomain::systemverilog_active },
            ReadSignal { 0U, input_signal },
            WriteUpdate { output_signal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { },
            Jump { 0U },
        };
    }
    if (!require_canonical_route) {
        consumer.name += "_alias";
    }
    add_process(std::move(consumer));

    Process clock;
    clock.id = clock_process;
    clock.name = "frontier_canonical_logic9_clock";
    clock.language_standard = "2017";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.register_value_kinds = { ValueKind::logic9 };
    clock.driver_regions = { { input_signal, 0U, 0U, true } };
    if (boundary_logic9) {
        clock.register_count = 2U;
        clock.register_value_kinds.push_back(ValueKind::logic4);
        clock.driver_regions.push_back({ toggle_signal, 0U, 0U, true });
    }
    clock.operations.emplace_back(LoadConstant { 0U, patterns.front() });
    clock.operations.emplace_back(WriteBlocking { input_signal, 0U });
    if (boundary_logic9) {
        clock.operations.emplace_back(LoadConstant { 1U, anchor(0U) });
        clock.operations.emplace_back(WriteBlocking { toggle_signal, 1U });
    }
    clock.operations.emplace_back(WaitFor { 1U });
    for (std::size_t phase = 1U; phase < patterns.size(); ++phase) {
        clock.operations.emplace_back(LoadConstant { 0U, patterns[phase] });
        clock.operations.emplace_back(WriteBlocking { input_signal, 0U });
        if (boundary_logic9) {
            clock.operations.emplace_back(LoadConstant { 1U, anchor(phase) });
            clock.operations.emplace_back(WriteBlocking { toggle_signal, 1U });
        }
        if (phase + 1U != patterns.size()) {
            clock.operations.emplace_back(WaitFor { 1U });
        }
    }
    if (exercise_boundary_plane_replacement) {
        clock.operations.emplace_back(WaitFor { 1U });
        clock.operations.emplace_back(LoadConstant { 1U, anchor(patterns.size()) });
        clock.operations.emplace_back(WriteBlocking { toggle_signal, 1U });
        for (std::size_t phase = patterns.size() + 1U;
             phase <= patterns.size() + 3U; ++phase) {
            clock.operations.emplace_back(WaitFor { 1U });
            clock.operations.emplace_back(LoadConstant { 1U, anchor(phase) });
            clock.operations.emplace_back(WriteBlocking { toggle_signal, 1U });
        }
    }
    clock.operations.emplace_back(Halt { });
    add_process(std::move(clock));

    // Register the output as an ordinary public boundary before the graph
    // snapshot is built; do not expose internal state while native execution
    // is active.
    static_cast<void>(interpreter.signal_value(output_signal));
    if (boundary_logic9) {
        static_cast<void>(interpreter.signal_value(anchor_output_signal));
    }
    if (use_frontier) {
        require(provider != nullptr,
            "the compiled Logic9 route receives the production LLVM provider");
        interpreter.set_region_kernel_backend_provider(provider);
    } else {
        require(provider == nullptr,
            "the checked Logic9 reference has no native region provider");
    }

    interpreter.start();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto primary_runtime = use_frontier
        ? runtime_for_process(implementation, producer_process)
        : std::shared_ptr<FrontierRuntime> { };
    if (expect_native_route) {
        require(primary_runtime != nullptr && primary_runtime->backend != nullptr
                && primary_runtime->backend->executor != nullptr
                && primary_runtime->backend->executor->step_entry() != nullptr,
            "the in-tree LLVM provider prepares the real V2 Logic9 component");
        require(implementation.region_component_by_process.at(producer_process)
                == implementation.region_component_by_process.at(consumer_process),
            "the producer and consumer share one certified activation component");
        const auto plane = std::ranges::find(primary_runtime->planes,
            input_signal, &RegionFrontierPlaneV2::signal_id);
        require(plane != primary_runtime->planes.end()
                && plane->value_kind == RegionFrontierValueKindV2::logic9
                && plane->width == width
                && plane->word_count == (width + 63U) / 64U
                && plane->plane_count == 4U,
            "the production frame binds all four Logic9 planes at the tested width");
    }

    RunOutcome outcome;
    outcome.frames.reserve(patterns.size()
        + 2U * static_cast<std::size_t>(exercise_boundary_plane_replacement));
    for (std::size_t phase = 0U; phase < patterns.size(); ++phase) {
        const bool replace_input_planes
            = expect_native_route && exercise_boundary_plane_replacement
            && phase + 1U == patterns.size();
        std::optional<PackedLogic4> pinned_input_snapshot;
        std::array<std::uintptr_t, 4U> input_plane_starts { };
        RouteSnapshot route_before_input_write;
        if (replace_input_planes) {
            const auto& input
                = implementation.signals.at(input_signal).initial_value;
            const auto expected_words
                = static_cast<std::size_t>((width + 63U) / 64U);
            for (std::size_t plane = 0U; plane < input_plane_starts.size(); ++plane) {
                const auto words = input.logic9_plane_words(plane);
                require(words.size() == expected_words && words.data() != nullptr,
                    "the owning boundary input exposes every canonical Logic9 plane");
                input_plane_starts[plane]
                    = reinterpret_cast<std::uintptr_t>(words.data());
            }
            // Copying a live packed role retains a read-pinned plane snapshot
            // across the next public signal publication.
            pinned_input_snapshot.emplace(input);
            for (std::size_t plane = 0U; plane < input_plane_starts.size(); ++plane) {
                const auto pinned_words
                    = pinned_input_snapshot->logic9_plane_words(plane);
                require(reinterpret_cast<std::uintptr_t>(pinned_words.data())
                            == input_plane_starts[plane],
                    "the owning snapshot retains the current input plane addresses");
            }
            route_before_input_write = route_snapshot(primary_runtime);
        }
        const auto result = interpreter.run(static_cast<SimulationTick>(phase));
        require(result.status == RunStatus::time_limit
                || result.status == RunStatus::completed,
            "the nine-state stimulus run reaches each absolute time boundary");
        if (replace_input_planes) {
            const auto& input
                = implementation.signals.at(input_signal).initial_value;
            require(*pinned_input_snapshot == patterns[phase - 1U],
                "the owning snapshot preserves the old input value through publication");
            bool every_plane_replaced = true;
            for (std::size_t plane = 0U; plane < input_plane_starts.size(); ++plane) {
                const auto pinned_words
                    = pinned_input_snapshot->logic9_plane_words(plane);
                const auto current_words = input.logic9_plane_words(plane);
                require(pinned_words.size() == current_words.size()
                        && current_words.size()
                            == static_cast<std::size_t>((width + 63U) / 64U)
                        && reinterpret_cast<std::uintptr_t>(pinned_words.data())
                            == input_plane_starts[plane],
                    "the owning snapshot keeps each old plane live through publication");
                every_plane_replaced = every_plane_replaced
                    && reinterpret_cast<std::uintptr_t>(current_words.data())
                        != input_plane_starts[plane];
            }
            require(every_plane_replaced,
                "the next clock write copy-on-write replaces all four live wide input planes");
            const auto active_runtime
                = runtime_for_process(implementation, producer_process);
            const auto route_after_input_write
                = route_snapshot(primary_runtime);
            require(active_runtime == primary_runtime
                    && route_after_input_write.native_member_dispatches
                        > route_before_input_write.native_member_dispatches
                    && route_after_input_write.alias_full_collector_calls
                        > route_before_input_write.alias_full_collector_calls
                    && route_after_input_write.alias_bind_proof_plane_misses
                        > route_before_input_write.alias_bind_proof_plane_misses,
                "the real bind sees replacement plane addresses and fully reproves geometry on the same runtime");
            pinned_input_snapshot.reset();
        }
        auto frame = capture_frame(implementation, boundary_logic9);
        for (std::size_t signal = 0U; signal < frame.size(); ++signal) {
            const bool is_anchor = boundary_logic9
                && (signal == internal_signal || signal >= toggle_signal);
            const auto expected = is_anchor ? anchor(phase) : patterns[phase];
            const auto previous = is_anchor ? anchor(phase == 0U ? 0U : phase - 1U)
                                           : patterns[phase == 0U ? 0U : phase - 1U];
            require(frame[signal].current == expected
                    && frame[signal].stored == expected,
                "input, internal, output and stored values match each Logic9 pattern");
            if (phase != 0U) {
                require(frame[signal].owner == expected
                        && frame[signal].last == previous
                        && frame[signal].event.has_value()
                        && frame[signal].event->first == phase
                        && frame[signal].transaction.has_value()
                        && frame[signal].transaction->first == phase,
                    "every Logic9 phase commits the expected LAST and event/transaction time");
            }
        }
        if (expect_native_route && phase == 0U) {
            require(frame[1U].a4_roles.has_value(),
                "the hidden Logic4 anchor retains its authoritative four-role state");
        }
        outcome.frames.push_back(std::move(frame));

        if (exercise_fallback && phase == 2U) {
            require(use_frontier && primary_runtime != nullptr,
                "the demotion control starts after a real canonical V2 execution");
            outcome.route_before_fallback = route_snapshot(primary_runtime);
            require(outcome.route_before_fallback.canonical_entries() != 0U
                    && outcome.route_before_fallback.native_member_dispatches != 0U,
                "the control first observes an actual canonical native member entry");
            exercise_canonical_receipt_lifecycle(*primary_runtime);
            require(!primary_runtime->canonical_values_binding_receipt.valid,
                "the final bind receipt expires when its lease helper exits");
            interpreter.set_execution_point_hook(
                [](Scheduler&, const ExecutionPoint&) { });
            require(implementation.region_graph
                    && !implementation.region_graph->component_epochs_current(
                        primary_runtime->component),
                "the new observer contract makes the component epoch stale after the receipt expires");
        }

        if (exercise_fallback && phase == 3U) {
            require(primary_runtime->native_member_dispatches
                    == outcome.route_before_fallback.native_member_dispatches
                    && primary_runtime->canonical_values_only_entries
                        + primary_runtime->alias_and_canonical_values_entries
                        == outcome.route_before_fallback.canonical_entries(),
                "the observer alone stops native member and canonical entry dispatch");
            const auto active_runtime
                = runtime_for_process(implementation, producer_process);
            require(!active_runtime || active_runtime == primary_runtime
                    || active_runtime->native_member_dispatches == 0U,
                "the rebuilt component uses checked fallback after the new observer contract");
            outcome.actual_fallback = true;
        }
    }

    if (exercise_boundary_plane_replacement) {
        const auto route_before_rebind = route_snapshot(primary_runtime);
        const auto result = interpreter.run(
            static_cast<SimulationTick>(patterns.size()));
        require(result.status == RunStatus::time_limit
                || result.status == RunStatus::completed,
            "the toggle-only offer reaches the next absolute time boundary");
        const auto active_runtime
            = runtime_for_process(implementation, producer_process);
        require(active_runtime == primary_runtime,
            "the toggle-only offer reuses the pinned builtin runtime");
        if (expect_native_route) {
            const auto rebound_route = route_snapshot(primary_runtime);
            require(rebound_route.native_member_dispatches
                        > route_before_rebind.native_member_dispatches,
                "the toggle-only tail reaches the same authentic builtin component");
        }
        auto rematerialized_frame
            = capture_frame(implementation, boundary_logic9);
        require(same_signal_state(outcome.frames.back().front(),
                    rematerialized_frame.front()),
            "the rematerialization tick leaves input value and event metadata unchanged");
        outcome.frames.push_back(std::move(rematerialized_frame));

        bool stable_geometry_reuse_observed { };
        for (std::size_t phase = patterns.size() + 1U;
             phase <= patterns.size() + 3U; ++phase) {
            const auto route_before_tick = route_snapshot(primary_runtime);
            const auto stable_result
                = interpreter.run(static_cast<SimulationTick>(phase));
            require(stable_result.status == RunStatus::time_limit
                    || stable_result.status == RunStatus::completed,
                "the toggle-only tail reaches each stable geometry cut");
            if (expect_native_route && phase > patterns.size() + 1U) {
                const auto route_after_tick = route_snapshot(primary_runtime);
                stable_geometry_reuse_observed
                    = stable_geometry_reuse_observed
                    || (route_after_tick.alias_bind_proof_reuse_hits
                            > route_before_tick.alias_bind_proof_reuse_hits
                        && route_after_tick.native_member_dispatches
                            > route_before_tick.native_member_dispatches
                        && route_after_tick.alias_full_collector_calls
                            == route_before_tick.alias_full_collector_calls);
            }
        }
        if (expect_native_route) {
            require(stable_geometry_reuse_observed,
                "a stable post-rebind builtin offer reuses geometry without a full collection");
        }
        auto stable_frame = capture_frame(implementation, boundary_logic9);
        require(same_signal_state(outcome.frames.back().front(),
                    stable_frame.front()),
            "the stable toggle-only interval leaves input value and event metadata unchanged");
        outcome.frames.push_back(std::move(stable_frame));
    }

    outcome.final_route = route_snapshot(primary_runtime);
    if (use_frontier && !expect_native_route) {
        require(outcome.final_route.native_member_dispatches == 0U
                && outcome.final_route.canonical_entries() == 0U
                && implementation.systemverilog_wave_profile_native_frontier_member_dispatches == 0U
                && implementation.systemverilog_wave_profile_canonical_values_only_entries == 0U
                && implementation.systemverilog_wave_profile_alias_and_canonical_values_entries == 0U,
            "all-Logic9 internals retain checked fallback without native or canonical entries");
    }
    if (expect_native_route) {
        require(outcome.final_route.alias_checked_entries
                    + outcome.final_route.alias_trusted_entries != 0U
                && outcome.final_route.native_member_dispatches != 0U,
            "the production builtin runtime executes its real V2 component");
        if (require_canonical_route) {
            require(outcome.final_route.canonical_entries() != 0U,
                "the production builtin runtime selects a canonical-values V2 entry");
            require(outcome.final_route.canonical_values_only_entries
                        <= outcome.final_route.alias_checked_entries
                    && outcome.final_route.alias_and_canonical_values_entries
                        <= outcome.final_route.alias_trusted_entries,
                "canonical selection remains counted inside its geometry route");
            require(outcome.final_route.alias_bind_proof_reuse_hits != 0U
                    && outcome.final_route.alias_bind_proof_attempts
                        >= outcome.final_route.alias_bind_proof_reuse_hits,
                "fresh builtin receipts reuse certified geometry on a stable live binding");
        } else {
            require(outcome.final_route.alias_trusted_entries != 0U
                    && outcome.final_route.canonical_entries() == 0U,
                "without the optional canonical entry, runtime keeps alias-only execution");
            require(outcome.final_route.alias_bind_proof_reuse_hits == 0U
                    && outcome.final_route.alias_bind_proof_attempts == 0U,
                "alias-only execution cannot consume builtin alias-plus-canonical bind proofs");
        }
        if (exercise_fallback) {
            require(outcome.actual_fallback,
                "the Logic9 late observer exercises real ordinary-process fallback");
            require(outcome.final_route.canonical_entries()
                    == outcome.route_before_fallback.canonical_entries(),
                "no canonical entry is selected after observer-driven demotion");
        } else if (require_canonical_route) {
            exercise_canonical_receipt_lifecycle(*primary_runtime);
            require(!primary_runtime->canonical_values_binding_receipt.valid,
                "a final bind receipt expires before runtime invalidation");
            if (exercise_boundary_plane_replacement) {
                exercise_alias_task_prefix_geometry(*primary_runtime);
            }
            primary_runtime->invalidate();
            require(!primary_runtime->canonical_values_binding_receipt.valid,
                "runtime invalidation clears the canonical-values receipt");
        }
    } else {
        require(!outcome.actual_fallback,
            "the checked reference does not claim the native demotion route");
    }
    return outcome;
}

void check_logic9_width(const std::uint32_t width,
    const bool exercise_fallback,
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit& process_jit, const bool boundary_logic9 = true)
{
    const auto checked = run_case(width, false, false, nullptr, nullptr,
        true, boundary_logic9);
    const auto compiled = run_case(width, true, exercise_fallback, provider,
        &process_jit, true, boundary_logic9);
    require(compiled.frames.size() == checked.frames.size(),
        "checked and LLVM Logic9 runs capture every code point");
    for (std::size_t phase = 0U; phase < checked.frames.size(); ++phase) {
        for (std::size_t signal = 0U; signal < checked.frames[phase].size(); ++signal) {
            require(same_signal_state(compiled.frames[phase][signal],
                        checked.frames[phase][signal]),
                "LLVM canonical Logic9 execution matches checked CURRENT/LAST/stored/owner/event/transaction state");
        }
    }
    require(!exercise_fallback || compiled.actual_fallback,
        "one width exercises the observer-driven checked fallback control");
}

void check_alias_only_logic9_width(
    const std::uint32_t width,
    const std::shared_ptr<RegionKernelBackendProvider>& provider,
    fsim::compiler::LlvmJit& process_jit)
{
    const auto checked = run_case(width, false, false, nullptr, nullptr);
    const auto alias_only = run_case(width, true, false, provider, &process_jit, false);
    require(alias_only.frames.size() == checked.frames.size(),
        "the alias-only and checked Logic9 runs capture every code point");
    for (std::size_t phase = 0U; phase < checked.frames.size(); ++phase) {
        for (std::size_t signal = 0U; signal < checked.frames[phase].size(); ++signal) {
            require(same_signal_state(alias_only.frames[phase][signal],
                        checked.frames[phase][signal]),
                "alias-only V2 execution preserves checked Logic9 public state");
        }
    }
}

} // namespace

void run_native_frontier_canonical_values_tests()
{
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_single_owner {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment wide_disjoint_owner {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };

    for (const auto optimization : { fsim::compiler::JitOptimizationLevel::o0,
             fsim::compiler::JitOptimizationLevel::o2 }) {
        TemporaryCacheDirectory cache;
        fsim::compiler::LlvmJitOptions options;
        options.optimization = optimization;
        options.debug_instrumentation = false;
        options.cache_directory = cache.path();
        fsim::compiler::LlvmJit process_jit { options };
        auto provider = fsim::app::application_detail::
            make_llvm_region_kernel_backend_provider(std::move(options),
                "runtime-native-frontier-canonical-values");
        require(provider != nullptr,
            "the canonical Logic9 witness creates the real application LLVM provider");
        for (const auto width : { 1U, 65U, 129U, 256U, 1024U }) {
            check_logic9_width(width, width == 65U, provider, process_jit);
            check_logic9_width(width, false, provider, process_jit, false);
        }
        if (optimization == fsim::compiler::JitOptimizationLevel::o2) {
            auto alias_only_provider = std::make_shared<AliasOnlyProvider>(provider);
            check_alias_only_logic9_width(65U, alias_only_provider, process_jit);
        }
    }
}

} // namespace fsim::tests::runtime
