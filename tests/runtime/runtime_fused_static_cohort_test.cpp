// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "runtime_owned_driver_demotion_test_access.hpp"

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

enum class Mode {
    normal, decline, late_hook, partial_initial, outside_reader, body_reader,
    force_pending, deposit_pending, late_driver_hook, unwritten_bit,
    multi_output_driver_hook, unrelated_fork, overlapping_fork,
    overlapping_fork_filter,
    observe_pending, observe_unrelated
};

class AggregateExecutor final : public FusedStaticCohortExecutor {
public:
    AggregateExecutor(const SignalId lhs, const SignalId rhs,
        std::vector<SignalId> targets, const std::uint32_t width, const bool decline,
        const bool outside_owner)
        : lhs_(lhs)
        , rhs_(rhs)
        , targets_(std::move(targets))
        , width_(width)
        , decline_(decline)
        , outside_owner_(outside_owner)
        , slots_(targets_.size())
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext& context) override
    {
        if (decline_) {
            return std::nullopt;
        }
        const auto read = [&](const SignalId signal) {
            if (width_ <= 64U) {
                return PackedLogic4::from_aval_bval(width_,
                    context.signal_aval[signal], context.signal_bval[signal]);
            }
            const auto offset = context.wide_signal_offsets[signal];
            return PackedLogic4::from_word_planes(width_,
                context.wide_signal_aval.subspan(offset, words()),
                context.wide_signal_bval.subspan(offset, words()));
        };
        const auto lhs = read(lhs_);
        const auto rhs = read(rhs_);
        auto value = binary_value(BinaryOperator::bit_xor, lhs, rhs);
        value.set(width_ - 1U,
            binary_value(BinaryOperator::bit_and, lhs, rhs).get(width_ - 1U));
        std::ranges::copy(value.aval_words(), aval_.begin());
        std::ranges::copy(value.bval_words(), bval_.begin());
        mask_.fill(0U);
        for (std::uint32_t bit = 0U; bit < width_; ++bit) {
            if (outside_owner_ && bit == width_ - 2U) {
                continue;
            }
            mask_[bit / 64U] |= UINT64_C(1) << (bit % 64U);
        }
        active_ = 1U;
        for (std::size_t index = 0U; index < targets_.size(); ++index) {
            slots_[index] = { targets_[index], width_, words(), &active_,
                aval_.data(), bval_.data(), mask_.data() };
        }
        return FusedStaticCohortResume { slots_, {} };
    }

private:
    std::uint32_t words() const { return (width_ + 63U) / 64U; }
    SignalId lhs_;
    SignalId rhs_;
    std::vector<SignalId> targets_;
    std::uint32_t width_;
    bool decline_;
    bool outside_owner_;
    std::uint32_t active_ { };
    std::array<std::uint64_t, 3> aval_ { };
    std::array<std::uint64_t, 3> bval_ { };
    std::array<std::uint64_t, 3> mask_ { };
    std::vector<ProcessUpdateSlotView> slots_;
};

using Trace = std::tuple<SimulationTick, std::uint64_t, std::string>;

struct Observation {
    std::vector<Trace> boundary;
    std::vector<Trace> hooks;
    std::vector<std::string> settled;
    std::vector<std::string> drivers;
    FusedStaticCounters counters;
    std::uint64_t invocations_at_first_output { };
    std::uint64_t invocations_before_hook { };
    std::uint64_t invocations_at_first_fork_filter { };
    std::uint64_t fork_filter_calls { };
    bool native_candidate_available { };
    bool legacy_composite_active { };
    bool all_certificates_demoted_before_followup_fork { };
};

class ScopedFusedTestEnvironment final {
public:
    ScopedFusedTestEnvironment(const char* name, const char* value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error(
                "failed to set fused cohort environment");
        }
    }

    ScopedFusedTestEnvironment(const ScopedFusedTestEnvironment&) = delete;
    ScopedFusedTestEnvironment& operator=(
        const ScopedFusedTestEnvironment&) = delete;

    ~ScopedFusedTestEnvironment()
    {
        if (had_previous_) {
            static_cast<void>(set(previous_.c_str()));
        } else {
            unset();
        }
    }

private:
    bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value) == 0;
#else
        return ::setenv(name_.c_str(), value, 1) == 0;
#endif
    }

    void unset() const noexcept
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name_.c_str(), ""));
#else
        static_cast<void>(::unsetenv(name_.c_str()));
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

class CopySignalCohortExecutor final : public FusedStaticCohortExecutor {
public:
    CopySignalCohortExecutor(
        const SignalId input, const SignalId trigger,
        const SignalId output,
        const std::uint32_t width,
        const bool decline,
        const bool malformed_mask,
        std::function<void()> on_resume)
        : input_ { input }
        , trigger_ { trigger }
        , output_ { output }
        , width_ { width }
        , words_ { (width + 63U) / 64U }
        , decline_ { decline }
        , malformed_mask_ { malformed_mask }
        , on_resume_ { std::move(on_resume) }
        , slots_ (1U)
    {
        mask_[0U] = UINT64_MAX;
        mask_[1U] = UINT64_C(1);
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext& context) override
    {
        if (on_resume_) {
            on_resume_();
        }
        if (decline_) {
            return std::nullopt;
        }
        const auto offset = context.wide_signal_offsets[input_];
        const auto value = PackedLogic4::from_word_planes(width_,
            context.wide_signal_aval.subspan(offset, words_),
            context.wide_signal_bval.subspan(offset, words_));
        const auto trigger_value = PackedLogic4::from_aval_bval(1U,
            context.signal_aval[trigger_], context.signal_bval[trigger_]);
        (void)trigger_value;
        std::ranges::copy(value.aval_words(), aval_.begin());
        std::ranges::copy(value.bval_words(), bval_.begin());
        if (malformed_mask_) {
            mask_[0U] &= ~UINT64_C(1);
        }
        active_ = 1U;
        slots_[0U] = { output_, width_, words_, &active_, aval_.data(),
            bval_.data(), mask_.data() };
        return FusedStaticCohortResume { slots_, {} };
    }

private:
    SignalId input_ { };
    SignalId trigger_ { };
    SignalId output_ { };
    std::uint32_t width_ { };
    std::uint32_t words_ { };
    bool decline_ { };
    bool malformed_mask_ { };
    std::function<void()> on_resume_;
    std::uint32_t active_ { };
    std::array<std::uint64_t, 2U> aval_ { };
    std::array<std::uint64_t, 2U> bval_ { };
    std::array<std::uint64_t, 2U> mask_ { };
    std::vector<ProcessUpdateSlotView> slots_;
};

struct GenericSliceCohortResult {
    struct Frame {
        std::string trigger;
        std::string output;
        std::vector<std::string> drivers;
        std::array<std::string, 3U> public_roles;
        std::array<std::string, 3U> a4_roles;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        std::uint64_t value_revision { };
        std::uint64_t a4_revision { };
        std::uint64_t fused_invocations { };
    };

    std::string output;
    std::vector<std::string> drivers;
    Frame before_same_value_round;
    Frame after_same_value_round;
    FusedStaticCounters counters;
    bool exact_graph_proof { };
    bool packed_disjoint_slots { };
    bool composite_route { };
    bool slot_rejected { };
    bool callback_entered { };
    GenericSliceCohortResult::Frame callback_entry;
};

GenericSliceCohortResult run_generic_slice_cohort(
    const bool install_executor,
    const bool decline_executor = false,
    const bool malformed_mask = false)
{
    ScopedFusedTestEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedFusedTestEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedFusedTestEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "1" };
    ScopedFusedTestEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::uint32_t width = 65U;
    Interpreter interpreter;
    interpreter.set_fused_static_counters_enabled(true);
    auto expected = PackedLogic4 { width, Logic4::zero };
    expected.set(0U, Logic4::one);
    expected.set(63U, Logic4::x);
    expected.set(64U, Logic4::one);
    const auto input = interpreter.add_signal({
        "generic_slice_input", expected });
    const auto trigger = interpreter.add_signal({
        "generic_slice_trigger", PackedLogic4 { 1U, Logic4::zero } });
    const auto output = interpreter.add_signal({
        "generic_slice_output", PackedLogic4 { width, Logic4::z },
        ResolutionKind::sv_wire });

    const auto add_owner = [&](const ProcessId id,
                               const std::uint32_t offset,
                               const std::uint32_t slice_width) {
        Process process;
        process.id = id;
        process.name = "generic_slice_owner_" + std::to_string(id);
        process.register_count = 3U;
        process.static_sensitivity = { { trigger, EdgeKind::any } };
        process.driver_regions = {
            { output, offset, slice_width, false }
        };
        process.operations = {
            ReadSignal { 0U, input },
            ReadSignal { 2U, trigger },
            Extract { 1U, 0U, offset, slice_width },
            WriteUpdateSlice {
                output, 1U, offset, SignalUpdateDomain::generic },
            WaitSensitivity { }, Jump { 0U }
        };
        return interpreter.add_process(std::move(process));
    };
    const auto low_owner = add_owner(0U, 0U, 64U);
    const auto high_owner = add_owner(1U, 64U, 1U);

    interpreter.schedule_signal_at(trigger,
        PackedLogic4 { 1U, Logic4::one }, 1U, 0U);
    interpreter.start();

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto* const graph = implementation.region_graph.has_value()
        ? &*implementation.region_graph : nullptr;
    const bool exact_graph_proof = graph != nullptr
        && graph->processes().size() > high_owner
        && graph->signals().size() > output
        && graph->processes()[low_owner].pure
        && graph->processes()[high_owner].pure
        && graph->processes()[low_owner].operation_write_ranges_exact
        && graph->processes()[high_owner].operation_write_ranges_exact
        && graph->processes()[low_owner].update_kind
            == RegionUpdateKind::generic
        && graph->processes()[high_owner].update_kind
            == RegionUpdateKind::generic
        && std::ranges::any_of(graph->signals()[output].writers,
            [&](const RegionAccess& writer) {
                return writer.process == low_owner && writer.offset == 0U
                    && writer.width == 64U;
            })
        && std::ranges::any_of(graph->signals()[output].writers,
            [&](const RegionAccess& writer) {
                return writer.process == high_owner && writer.offset == 64U
                    && writer.width == 1U;
            });
    require(exact_graph_proof,
        "generic slices carry an exact pure graph ownership proof before A4 binding");
    auto* const authoritative
        = implementation.region_authoritative_state_for_signal(output);
    const bool packed_disjoint_slots = authoritative != nullptr
        && authoritative->values().packed_slots_bound()
        && authoritative->values().packed_signal_slots_bound(output)
        && authoritative->values().layout().contains(output)
        && authoritative->values().layout().signal(output).storage_class
            == SignalDriverStorageClass::disjoint_owner
        && authoritative->values().layout().owners(output).size() == 2U
        && authoritative->values().packed_owner_slot_bound(
            output, low_owner)
        && authoritative->values().packed_owner_slot_bound(
            output, high_owner);
    std::optional<std::size_t> cohort_id;
    if (install_executor) {
        require(packed_disjoint_slots
                && !implementation.owned_driver_active(output),
            "generic Update slices seed bound A4 disjoint owner slots without a composite");
        const auto candidates = interpreter.fused_static_cohort_candidates();
        const auto candidate = std::ranges::find_if(candidates,
            [&](const FusedStaticCohortCandidate& item) {
                return item.members
                    == std::vector<ProcessId> { low_owner, high_owner };
            });
        require(candidate != candidates.end(),
            "generic Update slice writers form a fused static cohort");
        cohort_id = candidate->cohort_id;
    }

    const auto capture = [&]() {
        const auto* const state
            = implementation.region_authoritative_state_for_signal(output);
        const auto* const low_record
            = implementation.driver_values[output].find(low_owner);
        const auto* const high_record
            = implementation.driver_values[output].find(high_owner);
        require(low_record != nullptr && high_record != nullptr,
            "generic owner-group frame retains both original raw records");
        auto a4_roles = std::array<std::string, 3U> { };
        if (state != nullptr && state->valid()
            && state->values().layout().contains(output)) {
            a4_roles = {
                state->values().current(output).to_msb_string(),
                state->values().previous(output).to_msb_string(),
                state->values().stored(output).to_msb_string()
            };
        }
        return GenericSliceCohortResult::Frame {
            implementation.signals[trigger].initial_value.to_msb_string(),
            implementation.signals[output].initial_value.to_msb_string(),
            { low_record->value.to_msb_string(),
                high_record->value.to_msb_string() },
            {
                implementation.get_signal(output).initial_value.to_msb_string(),
                implementation.signal_last_values[output].to_msb_string(),
                implementation.driven_values[output].to_msb_string()
            },
            a4_roles,
            implementation.signal_events[output],
            implementation.signal_transactions[output],
            implementation.signal_value_revisions[output],
            state != nullptr ? state->values().revision() : 0U,
            interpreter.fused_static_counters().invocations
        };
    };
    auto callback_entry = GenericSliceCohortResult::Frame { };
    bool callback_entered { };
    if (cohort_id) {
        interpreter.install_fused_static_cohort(*cohort_id,
            std::make_unique<CopySignalCohortExecutor>(
                input, trigger, output, width, decline_executor,
                malformed_mask, [&]() {
                    callback_entry = capture();
                    callback_entered = true;
                }));
    }
    const auto first_run = interpreter.run(0U);
    require(first_run.status == RunStatus::time_limit
            || first_run.status == RunStatus::completed,
        "initial generic owner writes settle before the trigger-only update");
    const auto before_same_value_round = capture();
    bool slot_rejected { };
    auto after_same_value_round = GenericSliceCohortResult::Frame { };
    if (malformed_mask) {
        try {
            static_cast<void>(interpreter.run(1U));
        } catch (const std::logic_error& error) {
            require(std::string(error.what()).find("invalid aggregate slot")
                    != std::string::npos,
                "the A4 owner-group route rejects a malformed aggregate mask");
            slot_rejected = true;
        }
        after_same_value_round = capture();
    } else {
        const auto same_value_run = interpreter.run(1U);
        require(same_value_run.status == RunStatus::time_limit
                || same_value_run.status == RunStatus::completed,
            "a changed sensitivity input schedules a second generic Update round");
        after_same_value_round = capture();
    }
    const auto counters = interpreter.fused_static_counters();
    return {
        after_same_value_round.output,
        after_same_value_round.drivers,
        before_same_value_round,
        after_same_value_round,
        counters,
        exact_graph_proof,
        packed_disjoint_slots,
        implementation.owned_driver_active(output),
        slot_rejected,
        callback_entered,
        callback_entry
    };
}

void check_generic_slice_a4_cohort()
{
    const auto reference = run_generic_slice_cohort(false);
    const auto fused = run_generic_slice_cohort(true);
    auto expected = PackedLogic4 { 65U, Logic4::zero };
    expected.set(0U, Logic4::one);
    expected.set(63U, Logic4::x);
    expected.set(64U, Logic4::one);
    auto expected_low = PackedLogic4 { 65U, Logic4::z };
    auto expected_high = PackedLogic4 { 65U, Logic4::z };
    for (std::uint32_t bit = 0U; bit < 64U; ++bit) {
        expected_low.set(bit, expected.get(bit));
    }
    expected_high.set(64U, expected.get(64U));
    const auto same_semantics = [](const auto& left, const auto& right) {
        return left.trigger == right.trigger
            && left.output == right.output
            && left.drivers == right.drivers
            && left.public_roles == right.public_roles
            && left.a4_roles == right.a4_roles
            && left.event == right.event
            && left.transaction == right.transaction
            && left.value_revision == right.value_revision
            && left.a4_revision == right.a4_revision;
    };
    require(reference.output == expected.to_msb_string()
            && fused.output == expected.to_msb_string()
            && reference.drivers
                == std::vector<std::string> {
                    expected_low.to_msb_string(),
                    expected_high.to_msb_string() }
            && fused.drivers == reference.drivers,
        "generic Update-slice A4 grouping preserves visible and original-owner values");
    require(same_semantics(reference.before_same_value_round,
                fused.before_same_value_round)
            && same_semantics(reference.after_same_value_round,
                fused.after_same_value_round)
            && fused.before_same_value_round.output
                == fused.after_same_value_round.output
            && fused.before_same_value_round.drivers
                == fused.after_same_value_round.drivers
            && fused.before_same_value_round.trigger == "0"
            && fused.after_same_value_round.trigger == "1"
            && fused.before_same_value_round.transaction
            && fused.after_same_value_round.transaction
            && fused.before_same_value_round.transaction
                != fused.after_same_value_round.transaction
            && fused.before_same_value_round.event
                == fused.after_same_value_round.event
            && fused.before_same_value_round.value_revision
                == fused.after_same_value_round.value_revision
            && fused.before_same_value_round.a4_revision
                == fused.after_same_value_round.a4_revision
            && fused.after_same_value_round.fused_invocations
                == fused.before_same_value_round.fused_invocations + 1U,
        "same-value generic slices preserve transaction metadata without a new value event or role revision");
    const auto declined = run_generic_slice_cohort(true, true);
    require(declined.output == reference.output
            && declined.drivers == reference.drivers
            && declined.before_same_value_round.output
                == reference.before_same_value_round.output
            && declined.after_same_value_round.output
                == reference.after_same_value_round.output
            && declined.counters.invocations == 0U
            && declined.counters.fallbacks > 0U,
        "a declined A4 cohort resumes the original checked owners");

    const auto malformed = run_generic_slice_cohort(true, false, true);
    const auto same_frame = [](const auto& left, const auto& right) {
        return left.trigger == right.trigger
            && left.output == right.output
            && left.drivers == right.drivers
            && left.public_roles == right.public_roles
            && left.a4_roles == right.a4_roles
            && left.event == right.event
            && left.transaction == right.transaction
            && left.value_revision == right.value_revision
            && left.a4_revision == right.a4_revision
            && left.fused_invocations == right.fused_invocations;
    };
    require(malformed.slot_rejected
            && malformed.callback_entered
            && malformed.callback_entry.trigger == "1"
            && same_frame(malformed.callback_entry,
                malformed.after_same_value_round)
            && malformed.counters.invocations == 0U,
        "invalid A4 group masks leave current, LAST, drivers, event and role state unchanged");

    require(fused.exact_graph_proof && fused.packed_disjoint_slots
            && !fused.composite_route
            && fused.counters.invocations > 0U
            && fused.counters.aggregate_signals_staged
                == fused.counters.invocations
            && fused.counters.owner_stage_calls_avoided
                == 2U * fused.counters.invocations,
        "the static cohort executes through the certified generic A4 owner group");
}

void check_active_update_cohort_stays_in_active_phase()
{
    ScopedFusedTestEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedFusedTestEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedFusedTestEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "1" };
    ScopedFusedTestEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::uint32_t width = 65U;
    Interpreter interpreter;
    interpreter.set_fused_static_counters_enabled(true);
    const auto input = interpreter.add_signal({
        "active_slice_input", PackedLogic4 { width, Logic4::zero } });
    const auto output = interpreter.add_signal({
        "active_slice_output", PackedLogic4 { width, Logic4::z },
        ResolutionKind::sv_wire });
    const auto add_owner = [&](const ProcessId id,
                               const std::uint32_t offset,
                               const std::uint32_t slice_width) {
        Process process;
        process.id = id;
        process.name = "active_slice_owner_" + std::to_string(id);
        process.register_count = 2U;
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.static_sensitivity = { { input, EdgeKind::any } };
        process.driver_regions = {
            { output, offset, slice_width, false }
        };
        process.operations = {
            ReadSignal { 0U, input },
            Extract { 1U, 0U, offset, slice_width },
            WriteUpdateSlice {
                output, 1U, offset,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U }
        };
        return interpreter.add_process(std::move(process));
    };
    const auto low_owner = add_owner(0U, 0U, 64U);
    const auto high_owner = add_owner(1U, 64U, 1U);

    auto expected = PackedLogic4 { width, Logic4::zero };
    expected.set(0U, Logic4::one);
    expected.set(63U, Logic4::x);
    expected.set(64U, Logic4::one);
    interpreter.schedule_signal_at(input, expected, 1U, 0U);
    interpreter.start();

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* const authoritative
        = implementation.region_authoritative_state_for_signal(output);
    require(authoritative != nullptr
            && authoritative->values().layout().contains(output)
            && authoritative->values().packed_signal_slots_bound(output)
            && authoritative->values().layout().signal(output).storage_class
                == SignalDriverStorageClass::disjoint_owner,
        "SystemVerilog Active slices retain their own bound A4 layout");
    const auto candidates = interpreter.fused_static_cohort_candidates();
    require(std::ranges::none_of(candidates,
                [&](const FusedStaticCohortCandidate& item) {
                    return item.members
                        == std::vector<ProcessId> { low_owner, high_owner };
                }),
        "SystemVerilog Active writers are not admitted to generic Update owner staging");
    const auto result = interpreter.run(1U);
    require(result.status == RunStatus::completed
            || result.status == RunStatus::time_limit,
        "SystemVerilog Active slice writers complete through their original phase");
    auto expected_low = PackedLogic4 { width, Logic4::z };
    auto expected_high = PackedLogic4 { width, Logic4::z };
    for (std::uint32_t bit = 0U; bit < 64U; ++bit) {
        expected_low.set(bit, expected.get(bit));
    }
    expected_high.set(64U, expected.get(64U));
    require(interpreter.signal_value(output) == expected
            && interpreter.driver_value(low_owner, output) == expected_low
            && interpreter.driver_value(high_owner, output) == expected_high
            && interpreter.fused_static_counters().invocations == 0U,
        "Active-phase checked publication preserves visible and owner values without generic fusion");
}

Observation run_case(const std::uint32_t width, const Mode mode,
    const bool install, const bool capture_output,
    const bool capture_intermediate_state)
{
    Interpreter interpreter;
    interpreter.set_fused_static_counters_enabled(true);
    Observation result;
    const auto lhs = interpreter.add_signal(
        { "lhs", PackedLogic4(width, Logic4::zero) });
    const auto rhs = interpreter.add_signal(
        { "rhs", PackedLogic4(width, Logic4::zero) });
    const bool overlapping_fork = mode == Mode::overlapping_fork
        || mode == Mode::overlapping_fork_filter;
    const bool filter_observed_fork = mode == Mode::overlapping_fork_filter;
    const bool multiple_outputs = mode == Mode::multi_output_driver_hook;
    const auto secondary = multiple_outputs
        ? interpreter.add_signal({ "second_private", PackedLogic4(width, Logic4::z),
              ResolutionKind::sv_wire })
        : SignalId { };
    const auto internal = interpreter.add_signal(
        { "private", PackedLogic4(width, Logic4::z), ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal(
        { "result", PackedLogic4(1U, Logic4::z), ResolutionKind::sv_wire });
    const auto observed = mode == Mode::observe_unrelated
        ? interpreter.add_signal({ "unrelated_observation", PackedLogic4(width, Logic4::zero) })
        : internal;
    std::vector<ProcessId> producers(multiple_outputs ? 4U : 2U);
    for (std::uint32_t index = 0U; index < producers.size(); ++index) {
        const auto member = index % 2U;
        const auto target = index < 2U ? internal : secondary;
        const auto offset = member == 0U ? 0U : width - 1U;
        const auto count = member == 0U
            ? width - (overlapping_fork ? 2U : 1U) : 1U;
        const auto written = mode == Mode::unwritten_bit && index == 0U
            ? count - 1U : count;
        Process process;
        process.id = index;
        process.name = "owner_" + std::to_string(index);
        process.initialize = mode != Mode::partial_initial || index == 0U;
        process.register_count = 4U;
        process.static_sensitivity = {
            { lhs, EdgeKind::any }, { rhs, EdgeKind::any }
        };
        process.driver_regions = { { target, offset, count, false } };
        process.operations = {
            ReadSignal { 0U, lhs }, ReadSignal { 1U, rhs },
            Binary { member == 0U ? BinaryOperator::bit_xor
                                  : BinaryOperator::bit_and, 2U, 0U, 1U },
            Extract { 3U, 2U, offset, written },
            WriteUpdateSlice { target, 3U, offset },
        };
        if (member == 0U) {
            process.operations.push_back(WriteUpdateSlice { target, 3U, offset });
        }
        process.operations.push_back(WaitSensitivity { });
        process.operations.push_back(Jump { 0U });
        producers[index] = interpreter.add_process(std::move(process));
    }
    Process consumer;
    consumer.id = static_cast<ProcessId>(producers.size());
    consumer.name = "reduction";
    consumer.register_count = multiple_outputs ? 3U : 2U;
    consumer.static_sensitivity = { { internal, EdgeKind::any } };
    consumer.driver_regions = { { output, 0U, 1U, true } };
    consumer.operations = {
        ReadSignal { 0U, internal },
        Reduction { ReductionOperator::bit_xor, 1U, 0U },
        WriteUpdate { output, 1U }, WaitSensitivity { }, Jump { 0U }
    };
    if (multiple_outputs) {
        consumer.static_sensitivity.push_back({ secondary, EdgeKind::any });
        consumer.operations[0] = ReadSignal { 2U, secondary };
        consumer.operations.insert(consumer.operations.begin() + 1,
            ReadSignal { 0U, internal });
    }
    (void)interpreter.add_process(std::move(consumer));
    Process observer;
    observer.id = static_cast<ProcessId>(producers.size() + 1U);
    observer.name = "boundary_observer";
    observer.register_count = 1U;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = {
        ReadSignal { 0U, output }, Display { "boundary" },
        WaitSensitivity { }, Jump { 0U }
    };
    (void)interpreter.add_process(std::move(observer));
    const bool outside_reader = mode == Mode::outside_reader
        || mode == Mode::body_reader;
    if (outside_reader) {
        Process reader;
        reader.id = 4U;
        reader.name = "additional_reader";
        reader.register_count = 1U;
        reader.static_sensitivity = {
            { mode == Mode::body_reader ? lhs : internal, EdgeKind::any }
        };
        reader.operations = {
            ReadSignal { 0U, internal }, Display { "additional" },
            WaitSensitivity { }, Jump { 0U }
        };
        (void)interpreter.add_process(std::move(reader));
    }
    if (mode == Mode::unrelated_fork || overlapping_fork) {
        const auto child_output = overlapping_fork ? internal
            : interpreter.add_signal({ "unrelated_child_output", PackedLogic4(1U, Logic4::zero) });
        Process parent;
        parent.id = static_cast<ProcessId>(producers.size() + 2U);
        parent.name = "late_fork_parent";
        parent.register_count = 1U;
        if (overlapping_fork) {
            parent.driver_regions = { { internal, width - 2U, 1U, false } };
            parent.operations = {
                LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
                WriteUpdateSlice { internal, 0U, width - 2U }, WaitFor { 2U },
                Fork { { 5U }, ForkJoinKind::all }, Halt {},
                LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
                WriteUpdateSlice { internal, 0U, width - 2U }, WaitFor { 1U },
                Fork { { 10U }, ForkJoinKind::all }, ForkEnd {}, ForkEnd {}
            };
        } else {
            parent.driver_regions = { { child_output, 0U, 1U, true } };
            parent.operations = {
                WaitFor { 2U }, Fork { { 3U }, ForkJoinKind::all }, Halt {},
                LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
                WriteUpdate { child_output, 0U }, ForkEnd {}
            };
        }
        (void)interpreter.add_process(std::move(parent));
        if (mode != Mode::overlapping_fork_filter) {
            interpreter.scheduler().schedule_at(2U, SchedulerPhase::active, 0U,
                [&](Scheduler&) {
                    result.invocations_before_hook
                        = interpreter.fused_static_counters().invocations;
                });
        }
        if (filter_observed_fork) {
            interpreter.set_fork_spawn_filter(
                [&, spawn_count = std::size_t { }](ProcessId) mutable {
                    ++spawn_count;
                    ++result.fork_filter_calls;
                    if (spawn_count == 1U) {
                        result.invocations_at_first_fork_filter
                            = interpreter.fused_static_counters().invocations;
                    }
                    if (spawn_count == 2U) {
                        const auto counters
                            = interpreter.fused_static_counters();
                        result.all_certificates_demoted_before_followup_fork
                            = counters.fork_events == 1U
                            && counters.fork_plans_surviving_after_last
                                == 0U;
                    }
                    return true;
                });
        }
    }
    if (capture_output) {
        interpreter.set_output_hook(
            [&](ProcessId, std::string_view, bool, SimulationTick time,
                std::uint64_t delta) {
                if (result.boundary.empty()) {
                    result.invocations_at_first_output
                        = interpreter.fused_static_counters().invocations;
                }
                result.boundary.emplace_back(time, delta,
                    interpreter.signal_value(output).to_msb_string());
            });
    }
    constexpr std::array<Logic4, 5> stimulus {
        Logic4::one, Logic4::zero, Logic4::x, Logic4::z, Logic4::one
    };
    for (SimulationTick time = 1U; time <= stimulus.size(); ++time) {
        auto left = PackedLogic4(width, stimulus[time - 1U]);
        if (time == 1U) {
            left = PackedLogic4(width, Logic4::zero);
            left.set(width - 2U, Logic4::one);
        }
        interpreter.schedule_signal_at(lhs, left, time, 0U);
        interpreter.schedule_signal_at(rhs,
            PackedLogic4(width, time == 5U ? Logic4::one : Logic4::zero), time, 1U);
    }
    if (mode == Mode::late_hook) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active, 0U,
            [&](Scheduler&) {
                result.invocations_before_hook
                    = interpreter.fused_static_counters().invocations;
                interpreter.set_signal_change_hook(
                    [&](SignalId signal, const PackedLogic4& value,
                        SimulationTick time) {
                        result.hooks.emplace_back(time,
                            interpreter.scheduler().delta(),
                            std::to_string(signal) + ":" + value.to_msb_string());
                    });
            });
    }
    if (mode == Mode::force_pending || mode == Mode::deposit_pending
        || mode == Mode::late_driver_hook || multiple_outputs
        || mode == Mode::observe_pending || mode == Mode::observe_unrelated) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active,
            std::numeric_limits<StableOrder>::max(),
            [&](Scheduler& scheduler) {
                scheduler.schedule_next_delta(SchedulerPhase::active,
                    std::numeric_limits<StableOrder>::max(),
                    [&](Scheduler&) {
                        result.invocations_before_hook
                            = interpreter.fused_static_counters().invocations;
                        if (mode == Mode::observe_pending || mode == Mode::observe_unrelated) {
                            interpreter.prepare_signal_observation(observed);
                            result.hooks.emplace_back(interpreter.scheduler().now(),
                                interpreter.scheduler().delta(),
                                "current:" + interpreter.signal_value(observed).to_msb_string());
                            result.hooks.emplace_back(interpreter.scheduler().now(),
                                interpreter.scheduler().delta(),
                                "stored:" + interpreter.stored_signal_value(observed).to_msb_string());
                            if (mode == Mode::observe_pending) {
                                for (const auto producer : producers) {
                                    result.hooks.emplace_back(interpreter.scheduler().now(),
                                        interpreter.scheduler().delta(),
                                        "driver:" + interpreter.driver_value(producer, internal).to_msb_string());
                                }
                            }
                        } else if (mode == Mode::force_pending) {
                            interpreter.force_signal(internal,
                                PackedLogic4(width, Logic4::one));
                        } else if (mode == Mode::deposit_pending) {
                            interpreter.deposit_signal(internal,
                                PackedLogic4(width, Logic4::one));
                        } else {
                            interpreter.set_driver_change_hook(
                                [&](ProcessId process, SignalId signal,
                                    SimulationTick time) {
                                    result.hooks.emplace_back(time,
                                        interpreter.scheduler().delta(),
                                        std::to_string(process) + ":"
                                            + std::to_string(signal) + ":"
                                            + interpreter.driver_value(process, signal)
                                                  .to_msb_string());
                                });
                        }
                    });
            });
    }
    if (mode == Mode::force_pending) {
        interpreter.scheduler().schedule_at(3U, SchedulerPhase::active, 0U,
            [&](Scheduler&) { interpreter.release_signal(internal); });
    }
    interpreter.start();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    result.legacy_composite_active
        = implementation.owned_driver_active(internal)
        || (multiple_outputs
            && implementation.owned_driver_active(secondary));
    if (install) {
        const auto candidates = interpreter.fused_static_cohort_candidates();
        const auto candidate = std::ranges::find_if(candidates,
            [&](const FusedStaticCohortCandidate& item) {
                return item.members == std::vector<ProcessId>(
                    producers.begin(), producers.end());
            });
        result.native_candidate_available = candidate != candidates.end();
        if (outside_reader) {
            require(candidate == candidates.end()
                    || std::ranges::find(candidate->private_outputs, internal)
                        == candidate->private_outputs.end(),
                "full body reads prevent private admission even without sensitivity");
        }
        if (mode == Mode::unwritten_bit) {
            require(candidate == candidates.end(),
                "inexact declared owner ranges decline static A4 certification");
        } else if (candidate != candidates.end()) {
            const auto has_a4_disjoint_slots = [&](const SignalId signal) {
                const auto* const state
                    = implementation.region_authoritative_state_for_signal(signal);
                return !implementation.owned_driver_active(signal)
                    && state != nullptr && state->valid()
                    && state->values().packed_slots_bound()
                    && state->values().packed_signal_slots_bound(signal)
                    && state->values().layout().contains(signal)
                    && state->values().layout().signal(signal).storage_class
                        == SignalDriverStorageClass::disjoint_owner
                    && state->values().layout().owners(signal).size() >= 2U;
            };
            require(std::ranges::all_of(candidate->private_outputs,
                        has_a4_disjoint_slots)
                    && std::ranges::all_of(candidate->boundary_outputs,
                        has_a4_disjoint_slots),
                "certified static outputs use original-owner A4 slots, never the composite adapter");
            if (!outside_reader) {
                require(std::ranges::find(candidate->private_outputs, internal)
                        != candidate->private_outputs.end(),
                    "the full graph certifies the sole-consumer intermediate");
                interpreter.install_fused_static_cohort(candidate->cohort_id,
                    std::make_unique<AggregateExecutor>(
                        lhs, rhs, candidate->private_outputs,
                        width, mode == Mode::decline, overlapping_fork));
            }
        }
    }
    for (SimulationTick time = 0U; time <= stimulus.size(); ++time) {
        const auto run = interpreter.run(time);
        require(run.status == RunStatus::completed || run.status == RunStatus::time_limit,
            "the fused differential case completes each timestamp");
        if (capture_intermediate_state) {
            result.settled.push_back(interpreter.signal_value(output).to_msb_string());
        }
    }
    if (!capture_intermediate_state) {
        result.settled.push_back(interpreter.signal_value(output).to_msb_string());
    }
    for (std::size_t index = 0U; index < producers.size(); ++index) {
        result.drivers.push_back(interpreter.driver_value(producers[index],
            index < 2U ? internal : secondary).to_msb_string());
    }
    if (overlapping_fork) {
        const auto parent = static_cast<ProcessId>(producers.size() + 2U);
        for (const auto owner : { parent, parent + 1U }) {
            result.drivers.push_back(interpreter.driver_value(owner, internal).to_msb_string());
        }
    }
    result.counters = interpreter.fused_static_counters();
    return result;
}

void check_case(const std::uint32_t width, const Mode mode)
{
    const auto fused = run_case(width, mode, true, false, false);
    const auto reference = run_case(width, mode, false, false, false);
    if (mode == Mode::unwritten_bit) {
        require(!fused.native_candidate_available
                && fused.counters.invocations == 0U
                && fused.drivers.size() >= 2U
                && fused.drivers[0U].size() == width
                && fused.drivers[0U].substr(0U, 2U) == "ZZ",
            "inexact owner ranges use checked publication and preserve unwritten high bits");
        require(fused.settled == reference.settled
                && fused.drivers == reference.drivers,
            "inexact owner-range fallback matches ordinary execution");
        return;
    }
    require(fused.boundary == reference.boundary,
        "fused execution preserves every boundary delta and value");
    require(fused.settled == reference.settled && fused.hooks == reference.hooks,
        "fused execution preserves settled values and late hook order");
    require(fused.drivers == reference.drivers,
        "aggregate writes retain each original owner's raw driver value");
    // Keep native execution and unrestricted host observation as separate
    // contracts. Both retain the same HDL observer processes and stimulus.
    const auto observed_reference = run_case(width, mode, false, true, true);
    const auto observed_fused = run_case(width, mode, true, true, true);
    if (mode == Mode::normal) {
        require((observed_reference.settled
                    == std::vector<std::string> { "0", "1", "0", "X", "X", "1" }),
            "the stimulus exercises known transitions and four-state propagation");
    }
    require(!observed_reference.boundary.empty()
            && observed_fused.boundary == observed_reference.boundary,
        "output callbacks preserve every boundary delta and value after materialization");
    require(observed_fused.settled == observed_reference.settled
            && observed_fused.hooks == observed_reference.hooks
            && observed_fused.drivers == observed_reference.drivers,
        "observed cohorts preserve settled values, late hooks and original drivers");
    require(observed_fused.counters.invocations
            == observed_fused.invocations_at_first_output,
        "an unrestricted output callback prevents later certified cohort execution");
    if (mode == Mode::outside_reader || mode == Mode::body_reader) {
        return;
    }
    if (!fused.native_candidate_available) {
        require(fused.counters.invocations == 0U,
            "ineligible static cohorts use checked member execution");
        return;
    }
    if (mode == Mode::decline) {
        require(fused.counters.invocations == 0U && fused.counters.fallbacks > 0U,
            "declined kernels use the original process execution path");
        return;
    }
    require(fused.counters.invocations > 0U,
        "the test must execute the fused path rather than merely its fallback");
    const auto outputs = mode == Mode::multi_output_driver_hook ? 2U : 1U;
    require(fused.counters.represented_members == 2U * outputs * fused.counters.invocations,
        "each fused invocation replaces every original member execution");
    require(fused.counters.aggregate_signals_staged == outputs * fused.counters.invocations,
        "each fused invocation stages only one aggregate per signal");
    require(fused.counters.owner_stage_calls_avoided == 2U * outputs * fused.counters.invocations,
        "one aggregate per signal replaces the original owner staging calls");
    if (mode == Mode::late_hook) {
        require(fused.invocations_before_hook > 0U
                && fused.counters.invocations == fused.invocations_before_hook,
            "late observation invalidates the installed fused route");
    }
    if (mode == Mode::observe_pending) {
        require(fused.invocations_before_hook >= 2U
                && fused.counters.invocations == fused.invocations_before_hook,
            "late signal materialization demotes the affected graph kernel with pending writes");
    }
    if (mode == Mode::observe_unrelated) {
        require(fused.invocations_before_hook >= 2U
                && fused.counters.invocations > fused.invocations_before_hook,
            "observing an unrelated signal retains the certified graph kernel");
    }
    if (mode == Mode::unrelated_fork) {
        require(fused.invocations_before_hook > 0U
                && fused.counters.invocations > fused.invocations_before_hook,
            "an unrelated dynamic child preserves future graph-kernel activations");
        require(fused.counters.fork_events == 1U
                && fused.counters.fork_plans_invalidated == 0U
                && fused.counters.fork_plans_surviving_after_last > 0U,
            "the unrelated fork leaves the certified cohort live");
    }
    if (mode == Mode::overlapping_fork) {
        require(fused.invocations_before_hook > 0U
                && fused.counters.invocations == fused.invocations_before_hook,
            "a dynamic child that changes certified output ownership invalidates the graph kernel");
        require(fused.counters.fork_events == 2U
                && fused.counters.fork_plans_invalidated > 0U
                && fused.counters.fork_plans_surviving_after_last == 0U,
            "the hook-free ownership-changing fork demotes the last native plan");
    }
    if (mode == Mode::overlapping_fork_filter) {
        require(fused.invocations_at_first_fork_filter > 0U
                && fused.counters.invocations == fused.invocations_at_first_fork_filter,
            "the filter-observed route runs natively until its first filter callback");
        require(fused.fork_filter_calls == 2U
                && fused.counters.fork_events == 2U
                && fused.counters.fork_plans_invalidated == 0U
                && fused.counters.fork_plans_surviving_after_last == 0U
                && fused.all_certificates_demoted_before_followup_fork,
            "the first unrestricted filter callback demotes plans before the follow-up fork");
    }
    if (mode == Mode::force_pending || mode == Mode::deposit_pending
        || mode == Mode::late_driver_hook || mode == Mode::multi_output_driver_hook) {
        require(fused.invocations_before_hook >= 2U,
            "the pending-write intervention follows actual fused activations");
    }
}

void check_owned_composite_static_cohort_falls_back()
{
    ScopedFusedTestEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedFusedTestEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedFusedTestEnvironment disjoint_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedFusedTestEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    const auto fallback = run_case(65U, Mode::normal, true, true, true);
    const auto checked = run_case(65U, Mode::normal, false, true, true);
    require(fallback.legacy_composite_active
            && !fallback.native_candidate_available
            && fallback.counters.invocations == 0U,
        "an active legacy composite uses checked static member execution");
    require(fallback.boundary == checked.boundary
            && fallback.settled == checked.settled
            && fallback.hooks == checked.hooks
            && fallback.drivers == checked.drivers
            && fallback.settled
                == std::vector<std::string> { "0", "1", "0", "X", "X", "1" },
        "composite-backed fallback preserves observer order, exact values and original owner records");
}

} // namespace

void test_fused_static_cohorts()
{
    for (const auto width : { 3U, 9U, 65U, 129U }) {
        check_case(width, Mode::normal);
        check_case(width, Mode::decline);
        check_case(width, Mode::late_hook);
        check_case(width, Mode::partial_initial);
        check_case(width, Mode::outside_reader);
        check_case(width, Mode::body_reader);
        check_case(width, Mode::force_pending);
        check_case(width, Mode::deposit_pending);
        check_case(width, Mode::late_driver_hook);
        check_case(width, Mode::unwritten_bit);
        check_case(width, Mode::multi_output_driver_hook);
        check_case(width, Mode::unrelated_fork);
        check_case(width, Mode::overlapping_fork);
        check_case(width, Mode::overlapping_fork_filter);
        check_case(width, Mode::observe_pending);
        check_case(width, Mode::observe_unrelated);
    }
    check_owned_composite_static_cohort_falls_back();
    check_generic_slice_a4_cohort();
    check_active_update_cohort_stays_in_active_phase();
}

} // namespace fsim::tests::runtime
