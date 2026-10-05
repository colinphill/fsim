// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::tests::runtime {
namespace {

using fsim::runtime::Logic4;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunStatus;
using fsim::runtime::SimulationTick;
using namespace fsim::runtime::simir;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] ContainerType fixed_word_array_type(
    const std::size_t element_width = 4U)
{
    require(element_width <= std::numeric_limits<std::uint32_t>::max(),
        "fixture element width must fit the container descriptor");
    ContainerType type;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = static_cast<std::uint32_t>(element_width);
    type.fixed = true;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    return type;
}

struct AliasFamily {
    SignalId first { };
    SignalId second { };
    SignalId proxy { };
    ContainerObjectId object { };
};

[[nodiscard]] AliasFamily add_alias_family(
    Interpreter& interpreter,
    const std::string_view prefix,
    const std::size_t element_width = 4U)
{
    const auto name = std::string { prefix };
    const PackedLogic4 zero_element { element_width, Logic4::zero };
    const PackedLogic4 zero_proxy { element_width * 2U, Logic4::zero };
    AliasFamily family;
    family.first = interpreter.add_signal({
        name + "[1]", zero_element, ResolutionKind::sv_wire });
    family.second = interpreter.add_signal({
        name + "[0]", zero_element, ResolutionKind::sv_wire });
    family.proxy = interpreter.add_signal({
        name, zero_proxy, ResolutionKind::sv_wire });
    family.object = interpreter.add_container_object({
        name,
        ContainerValue {
            fixed_word_array_type(element_width),
            { zero_element, zero_element },
            { } },
        std::nullopt });
    interpreter.add_container_element_signal_alias(
        { family.object, 0U, family.first, true, true });
    interpreter.add_container_element_signal_alias(
        { family.object, 1U, family.second, true, true });
    interpreter.add_container_aggregate_signal_alias(
        { family.object, family.proxy, true, true });
    return family;
}

struct AliasReadProbe {
    std::array<std::string, 2U> expected { "0000", "0000" };
    bool direct_read_contract { true };
    bool direct_write_stays_checked { true };
    bool history_keeps_sibling { true };
    bool callbacks_see_current_planes { true };
    std::size_t callback_observations { };
};

[[nodiscard]] bool direct_leaf_matches(
    ProcessExecutionContext& context,
    Interpreter& interpreter,
    const SignalId signal,
    const std::string_view expected_text)
{
    if (!context.supports_direct_signal_read(signal)) {
        return false;
    }
    const auto expected = PackedLogic4::from_msb_string(expected_text);
    const auto expected_word = expected.unchecked_low_word();
    const auto direct_aval = context.direct_signal_aval();
    const auto direct_bval = context.direct_signal_bval();
    const auto wide_aval = context.direct_wide_signal_aval();
    const auto wide_bval = context.direct_wide_signal_bval();
    const auto offsets = context.direct_wide_signal_offsets();
    if (signal >= direct_aval.size() || signal >= direct_bval.size()
        || signal >= offsets.size()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(offsets[signal]);
    if (offset >= wide_aval.size() || offset >= wide_bval.size()) {
        return false;
    }
    const auto checked_word = context.read_signal_word(signal);
    return checked_word == expected_word
        && direct_aval[signal] == expected_word.aval
        && direct_bval[signal] == expected_word.bval
        && wide_aval[offset] == expected_word.aval
        && wide_bval[offset] == expected_word.bval
        && context.read_signal(signal) == expected
        && interpreter.signal_value_snapshot(signal) == expected;
}

[[nodiscard]] bool alias_family_matches(
    ProcessExecutionContext& context,
    Interpreter& interpreter,
    const AliasFamily& family,
    const AliasReadProbe& probe)
{
    if (context.supports_direct_signal_read(family.proxy)) {
        return false;
    }
    const auto stable_writers = context.stable_single_writer_processes();
    const auto direct_writers = context.direct_single_driver_processes();
    if (family.first >= stable_writers.size()
        || family.second >= stable_writers.size()
        || family.first >= direct_writers.size()
        || family.second >= direct_writers.size()) {
        return false;
    }
    const auto no_writer = std::numeric_limits<ProcessId>::max();
    if (stable_writers[family.first] != no_writer
        || stable_writers[family.second] != no_writer
        || direct_writers[family.first] != no_writer
        || direct_writers[family.second] != no_writer) {
        return false;
    }
    if (!direct_leaf_matches(
            context, interpreter, family.first, probe.expected[0U])
        || !direct_leaf_matches(
            context, interpreter, family.second, probe.expected[1U])) {
        return false;
    }
    const auto proxy_expected
        = probe.expected[0U] + probe.expected[1U];
    const auto expected_container = interpreter.container_object_value_snapshot(
        family.object);
    return context.read_signal(family.proxy).to_msb_string()
            == proxy_expected
        && interpreter.signal_value_snapshot(family.proxy).to_msb_string()
            == proxy_expected
        && expected_container.elements.size() == 2U
        && expected_container.elements[0U].to_msb_string()
            == probe.expected[0U]
        && expected_container.elements[1U].to_msb_string()
            == probe.expected[1U];
}

class AliasMutationExecutor final : public ProcessExecutor {
public:
    AliasMutationExecutor(
        Interpreter& interpreter,
        const AliasFamily family,
        AliasReadProbe& probe,
        ProcessExecutionContext*& active_context,
        const ProcessId process)
        : interpreter_(interpreter)
        , family_(family)
        , probe_(probe)
        , active_context_(active_context)
        , process_(process)
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "unexpected element-alias direct-read resume PC"
            };
        }
        active_context_ = &context;
        const auto finish = [&] { active_context_ = nullptr; };
        try {
            probe_.direct_read_contract
                = alias_family_matches(context, interpreter_, family_, probe_);
            probe_.direct_write_stays_checked
                = probe_.direct_write_stays_checked
                && direct_update_slot_declined(context);

            probe_.expected[0U] = "1010";
            context.write_blocking(
                family_.first, PackedLogic4::from_msb_string("1010"));
            probe_.direct_read_contract
                = probe_.direct_read_contract
                && alias_family_matches(
                    context, interpreter_, family_, probe_);
            const auto prior_first
                = context.signal_last_value_word(family_.first);
            const auto prior_sibling
                = context.signal_last_value_word(family_.second);
            probe_.history_keeps_sibling
                = prior_first.aval == 0U && prior_first.bval == 0U
                && prior_sibling.aval == 0U && prior_sibling.bval == 0U
                && context.signal_last_event(family_.second)
                    == std::numeric_limits<SimulationTick>::max();

            probe_.expected = { "0011", "0110" };
            context.write_blocking(
                family_.proxy,
                PackedLogic4::from_msb_string("00110110"));
            probe_.direct_read_contract
                = probe_.direct_read_contract
                && alias_family_matches(
                    context, interpreter_, family_, probe_);

            probe_.expected[1U] = "1111";
            context.write_blocking_slice(
                family_.proxy,
                PackedLogic4::from_msb_string("1111"), 0U);
            probe_.direct_read_contract
                = probe_.direct_read_contract
                && alias_family_matches(
                    context, interpreter_, family_, probe_);

            probe_.expected[1U] = "1100";
            context.force_signal_slice(
                family_.proxy,
                PackedLogic4::from_msb_string("00"), 0U);
            probe_.direct_read_contract
                = probe_.direct_read_contract
                && alias_family_matches(
                    context, interpreter_, family_, probe_);

            probe_.expected[1U] = "1111";
            context.release_signal_slice(family_.proxy, 0U, 2U);
            probe_.direct_read_contract
                = probe_.direct_read_contract
                && alias_family_matches(
                    context, interpreter_, family_, probe_);

            probe_.expected = { "1001", "0110" };
            interpreter_.deposit_container_object(
                family_.object,
                ContainerValue {
                    fixed_word_array_type(),
                    { PackedLogic4::from_msb_string("1001"),
                        PackedLogic4::from_msb_string("0110") },
                    { } });
            probe_.direct_read_contract
                = probe_.direct_read_contract
                && alias_family_matches(
                    context, interpreter_, family_, probe_);
        } catch (...) {
            finish();
            throw;
        }
        finish();
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    [[nodiscard]] bool direct_update_slot_declined(
        ProcessExecutionContext& context)
    {
        std::uint32_t active { 1U };
        std::uint64_t aval { UINT64_C(0x5) };
        std::uint64_t bval { };
        std::uint64_t mask { UINT64_C(0xf) };
        const ProcessUpdateSlotView slot {
            family_.first, 4U, 1U, &active, &aval, &bval, &mask
        };
        const ProcessUpdateSlotBatch batch {
            process_, std::span { &slot, 1U }, { }
        };
        const auto consumed = context.write_validated_update_slot_batches(
            std::span { &batch, 1U });
        return !consumed && active == 1U && mask == UINT64_C(0xf);
    }

    Interpreter& interpreter_;
    AliasFamily family_;
    AliasReadProbe& probe_;
    ProcessExecutionContext*& active_context_;
    ProcessId process_ { };
};

struct WideAliasProbe {
    std::array<PackedLogic4, 2U> expected;
    bool direct_read_contract { true };
    bool callbacks_see_current_planes { true };
    std::size_t callback_observations { };
};

[[nodiscard]] PackedLogic4 make_wide_pattern(
    const std::size_t width,
    const std::size_t stride)
{
    PackedLogic4 value { width, Logic4::zero };
    for (std::size_t bit = stride; bit < width; bit += 5U) {
        value.set(bit, Logic4::one);
    }
    if (stride == 1U) {
        value.set(64U, Logic4::x);
        value.set(2U, Logic4::z);
    } else {
        value.set(5U, Logic4::x);
        value.set(6U, Logic4::z);
    }
    return value;
}

[[nodiscard]] PackedLogic4 make_proxy_value(
    const std::array<PackedLogic4, 2U>& elements)
{
    const auto width = elements[0U].width();
    PackedLogic4 proxy { width * 2U, Logic4::zero };
    proxy.insert_bits(elements[0U], width);
    proxy.insert_bits(elements[1U], 0U);
    return proxy;
}

[[nodiscard]] bool wide_leaf_matches(
    ProcessExecutionContext& context,
    Interpreter& interpreter,
    const SignalId signal,
    const PackedLogic4& expected)
{
    if (!context.supports_direct_signal_read(signal)) {
        return false;
    }
    const auto wide_aval = context.direct_wide_signal_aval();
    const auto wide_bval = context.direct_wide_signal_bval();
    const auto offsets = context.direct_wide_signal_offsets();
    const auto expected_aval = expected.aval_words();
    const auto expected_bval = expected.bval_words();
    if (signal >= offsets.size()
        || expected_aval.empty()
        || expected_aval.size() != expected_bval.size()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(offsets[signal]);
    if (offset > wide_aval.size()
        || expected_aval.size() > wide_aval.size() - offset
        || offset > wide_bval.size()
        || expected_bval.size() > wide_bval.size() - offset) {
        return false;
    }
    return std::equal(
            expected_aval.begin(), expected_aval.end(),
            wide_aval.begin() + static_cast<std::ptrdiff_t>(offset))
        && std::equal(
            expected_bval.begin(), expected_bval.end(),
            wide_bval.begin() + static_cast<std::ptrdiff_t>(offset))
        && context.read_signal(signal) == expected
        && interpreter.signal_value_snapshot(signal) == expected;
}

[[nodiscard]] bool wide_family_matches(
    ProcessExecutionContext& context,
    Interpreter& interpreter,
    const AliasFamily& family,
    const WideAliasProbe& probe)
{
    if (context.supports_direct_signal_read(family.proxy)
        || !wide_leaf_matches(
            context, interpreter, family.first, probe.expected[0U])
        || !wide_leaf_matches(
            context, interpreter, family.second, probe.expected[1U])) {
        return false;
    }
    const auto expected_proxy = make_proxy_value(probe.expected);
    const auto object = interpreter.container_object_value_snapshot(family.object);
    return context.read_signal(family.proxy) == expected_proxy
        && interpreter.signal_value_snapshot(family.proxy) == expected_proxy
        && object.elements.size() == 2U
        && object.elements[0U] == probe.expected[0U]
        && object.elements[1U] == probe.expected[1U];
}

class WideAliasMutationExecutor final : public ProcessExecutor {
public:
    WideAliasMutationExecutor(
        Interpreter& interpreter,
        const AliasFamily family,
        WideAliasProbe& probe,
        ProcessExecutionContext*& active_context)
        : interpreter_(interpreter)
        , family_(family)
        , probe_(probe)
        , active_context_(active_context)
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "unexpected wide element-alias resume PC"
            };
        }
        active_context_ = &context;
        const auto finish = [&] { active_context_ = nullptr; };
        const auto check = [&] {
            const bool matches = wide_family_matches(
                context, interpreter_, family_, probe_);
            probe_.direct_read_contract = probe_.direct_read_contract && matches;
        };
        try {
            const auto width = probe_.expected[0U].width();
            auto target = std::array<PackedLogic4, 2U> {
                make_wide_pattern(width, 1U),
                make_wide_pattern(width, 3U),
            };
            target[0U].set(0U, Logic4::zero);
            target[1U].set(width - 1U, Logic4::one);
            probe_.expected = {
                PackedLogic4 { width, Logic4::zero },
                PackedLogic4 { width, Logic4::zero },
            };
            check();

            probe_.expected = target;
            context.write_blocking(
                family_.proxy, make_proxy_value(probe_.expected));
            check();

            probe_.expected[1U].set(63U, Logic4::zero);
            probe_.expected[1U].set(64U, Logic4::one);
            context.write_blocking_slice(
                family_.proxy,
                PackedLogic4::from_msb_string("10"),
                63U);
            check();

            probe_.expected[0U].set(0U, Logic4::one);
            probe_.expected[1U].set(width - 1U, Logic4::zero);
            context.write_blocking_slice(
                family_.proxy,
                PackedLogic4::from_msb_string("10"),
                width - 1U);
            check();
            const auto release_value = probe_.expected;

            probe_.expected[0U].set(0U, Logic4::zero);
            probe_.expected[1U].set(width - 1U, Logic4::one);
            context.force_signal_slice(
                family_.proxy,
                PackedLogic4::from_msb_string("01"),
                width - 1U);
            check();

            probe_.expected = release_value;
            context.release_signal_slice(
                family_.proxy, width - 1U, 2U);
            check();
        } catch (...) {
            finish();
            throw;
        }
        finish();
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    Interpreter& interpreter_;
    AliasFamily family_;
    WideAliasProbe& probe_;
    ProcessExecutionContext*& active_context_;
};

class DelayedAliasObserver final : public ProcessExecutor {
public:
    DelayedAliasObserver(
        Interpreter& interpreter,
        const AliasFamily family,
        AliasReadProbe& probe,
        const bool enqueue_update)
        : interpreter_(interpreter)
        , family_(family)
        , probe_(probe)
        , enqueue_update_(enqueue_update)
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start == 0U) {
            if (enqueue_update_) {
                context.write_update(
                    family_.first, PackedLogic4::from_msb_string("1100"));
            }
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::wait_for;
            result.external.delay = enqueue_update_ ? 1U : 2U;
            return result;
        }
        if (start != 1U) {
            throw std::logic_error {
                "unexpected delayed element-alias observation resume PC"
            };
        }
        probe_.expected[0U] = enqueue_update_ ? "1100" : "1010";
        probe_.expected[1U] = "0000";
        probe_.direct_read_contract
            = alias_family_matches(context, interpreter_, family_, probe_);
        const auto previous = context.signal_last_value_word(family_.first);
        probe_.history_keeps_sibling
            = previous.aval == 0U && previous.bval == 0U
            && context.signal_last_event(family_.second)
                == std::numeric_limits<SimulationTick>::max();
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    Interpreter& interpreter_;
    AliasFamily family_;
    AliasReadProbe& probe_;
    bool enqueue_update_ { };
};

class RejectedElementAliasObserver final : public ProcessExecutor {
public:
    explicit RejectedElementAliasObserver(
        const SignalId signal, bool& direct_read_rejected)
        : signal_(signal)
        , direct_read_rejected_(direct_read_rejected)
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "unexpected incomplete element-alias resume PC"
            };
        }
        direct_read_rejected_
            = !context.supports_direct_signal_read(signal_);
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    SignalId signal_ { };
    bool& direct_read_rejected_;
};

struct PinnedAliasProbe {
    bool direct_read_cache_invalidated { };
    bool direct_reads_rejected { };
    bool retained_leaf_is_live { };
    bool retained_stored_leaf_is_live { };
    bool retained_proxy_is_live { };
};

class PinnedAliasReferenceObserver final : public ProcessExecutor {
public:
    PinnedAliasReferenceObserver(
        Interpreter& interpreter,
        const AliasFamily family,
        PinnedAliasProbe& probe)
        : interpreter_(interpreter)
        , family_(family)
        , probe_(probe)
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "unexpected late-pin element-alias resume PC"
            };
        }
        const auto prior_capability_key
            = context.direct_signal_read_capability_key();
        const auto& leaf_reference
            = interpreter_.signal_value(family_.first);
        const auto& stored_leaf_reference
            = interpreter_.stored_signal_value(family_.first);
        const auto& proxy_reference
            = interpreter_.signal_value(family_.proxy);
        probe_.direct_read_cache_invalidated
            = context.direct_signal_read_capability_key()
                != prior_capability_key;
        probe_.direct_reads_rejected
            = !context.supports_direct_signal_read(family_.first)
            && !context.supports_direct_signal_read(family_.second)
            && !context.supports_direct_signal_read(family_.proxy);

        context.write_blocking(
            family_.first, PackedLogic4::from_msb_string("1010"));
        probe_.retained_leaf_is_live
            = leaf_reference.to_msb_string() == "1010";
        probe_.retained_stored_leaf_is_live
            = stored_leaf_reference.to_msb_string() == "1010";
        probe_.retained_proxy_is_live
            = proxy_reference.to_msb_string() == "10100000";

        context.write_blocking(
            family_.second, PackedLogic4::from_msb_string("0101"));
        probe_.retained_leaf_is_live
            = probe_.retained_leaf_is_live
            && leaf_reference.to_msb_string() == "1010";
        probe_.retained_stored_leaf_is_live
            = probe_.retained_stored_leaf_is_live
            && stored_leaf_reference.to_msb_string() == "1010";
        probe_.retained_proxy_is_live
            = probe_.retained_proxy_is_live
            && proxy_reference.to_msb_string() == "10100101";

        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    Interpreter& interpreter_;
    AliasFamily family_;
    PinnedAliasProbe& probe_;
};

} // namespace

void test_element_alias_direct_read_planes()
{
    {
        Interpreter interpreter;
        const auto family = add_alias_family(interpreter, "top.prestart_pin");
        const auto& retained = interpreter.signal_value(family.first);
        Process process;
        process.name = "prestart_public_reference_pin";
        process.operations = { Halt { } };
        const auto process_id = interpreter.add_process(std::move(process));
        bool direct_read_rejected { };
        interpreter.set_process_executor(
            process_id,
            std::make_unique<RejectedElementAliasObserver>(
                family.second, direct_read_rejected));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && direct_read_rejected
                && retained.to_msb_string() == "0000",
            "a pre-start public leaf reference excludes every signal in its alias family from direct planes");
    }

    {
        Interpreter interpreter;
        const auto family = add_alias_family(interpreter, "top.late_pin");
        Process process;
        process.name = "late_public_reference_pin";
        process.operations = { Halt { } };
        const auto process_id = interpreter.add_process(std::move(process));
        PinnedAliasProbe probe;
        interpreter.set_process_executor(
            process_id,
            std::make_unique<PinnedAliasReferenceObserver>(
                interpreter, family, probe));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && probe.direct_read_cache_invalidated
                && probe.direct_reads_rejected
                && probe.retained_leaf_is_live
                && probe.retained_stored_leaf_is_live
                && probe.retained_proxy_is_live,
            "a late public reference pins the complete alias family and remains live across subsequent writes");
    }

    {
        Interpreter interpreter;
        const auto first = interpreter.add_signal({
            "top.partial_words[1]",
            PackedLogic4::from_msb_string("0000"),
            ResolutionKind::sv_wire });
        const auto object = interpreter.add_container_object({
            "top.partial_words",
            ContainerValue {
                fixed_word_array_type(),
                { PackedLogic4::from_msb_string("0000"),
                    PackedLogic4::from_msb_string("0000") },
                { } },
            std::nullopt });
        interpreter.add_container_element_signal_alias(
            { object, 0U, first, true, true });
        Process process;
        process.name = "incomplete_element_alias_must_stay_checked";
        process.operations = { Halt { } };
        const auto process_id = interpreter.add_process(std::move(process));
        bool rejected { };
        interpreter.set_process_executor(
            process_id,
            std::make_unique<RejectedElementAliasObserver>(first, rejected));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed && rejected,
            "an incomplete element-alias family never receives direct-read capability");
    }

    {
        Interpreter interpreter;
        const auto family = add_alias_family(interpreter, "top.words");
        Process process;
        process.name = "element_alias_direct_read_mutations";
        process.operations = { Halt { } };
        const auto process_id = interpreter.add_process(std::move(process));
        AliasReadProbe probe;
        ProcessExecutionContext* active_context { };
        interpreter.set_process_executor(
            process_id,
            std::make_unique<AliasMutationExecutor>(
                interpreter, family, probe, active_context, process_id));
        interpreter.set_signal_change_hook(
            [&](const SignalId, const PackedLogic4&, const SimulationTick) {
                if (active_context == nullptr) {
                    return;
                }
                ++probe.callback_observations;
                probe.callbacks_see_current_planes
                    = probe.callbacks_see_current_planes
                    && alias_family_matches(
                        *active_context, interpreter, family, probe);
            });

        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && probe.direct_read_contract
                && probe.direct_write_stays_checked
                && probe.history_keeps_sibling
                && probe.callbacks_see_current_planes
                && probe.callback_observations != 0U,
            "eligible element-alias direct planes remain coherent across leaf, proxy, deposit, force, release, and callback paths");
    }

    for (const bool enqueue_update : std::array { false, true }) {
        Interpreter interpreter;
        const auto family = add_alias_family(
            interpreter,
            enqueue_update ? "top.queued_words" : "top.external_words");
        Process process;
        process.name = enqueue_update
            ? "element_alias_direct_read_pending_queue"
            : "element_alias_direct_read_external_drive";
        process.operations = { Halt { } };
        const auto process_id = interpreter.add_process(std::move(process));
        AliasReadProbe probe;
        interpreter.set_process_executor(
            process_id,
            std::make_unique<DelayedAliasObserver>(
                interpreter, family, probe, enqueue_update));
        if (!enqueue_update) {
            interpreter.schedule_signal_at(
                family.first,
                PackedLogic4::from_msb_string("1010"),
                1U);
        }
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && probe.direct_read_contract
                && probe.history_keeps_sibling,
            enqueue_update
                ? "pending queued element updates refresh certified read planes before resumption"
                : "external element drives refresh certified read planes before resumption");
    }

    for (const auto width : std::array<std::size_t, 2U> { 65U, 129U }) {
        Interpreter interpreter;
        const auto family = add_alias_family(
            interpreter,
            width == 65U ? "top.words_65" : "top.words_129",
            width);
        Process process;
        process.name = width == 65U
            ? "element_alias_direct_read_65bit_mutations"
            : "element_alias_direct_read_129bit_mutations";
        process.operations = { Halt { } };
        const auto process_id = interpreter.add_process(std::move(process));
        WideAliasProbe probe;
        probe.expected = {
            PackedLogic4 { width, Logic4::zero },
            PackedLogic4 { width, Logic4::zero },
        };
        ProcessExecutionContext* active_context { };
        interpreter.set_process_executor(
            process_id,
            std::make_unique<WideAliasMutationExecutor>(
                interpreter, family, probe, active_context));
        interpreter.set_signal_change_hook(
            [&](const SignalId, const PackedLogic4&, const SimulationTick) {
                if (active_context == nullptr) {
                    return;
                }
                ++probe.callback_observations;
                probe.callbacks_see_current_planes
                    = probe.callbacks_see_current_planes
                    && wide_family_matches(
                        *active_context, interpreter, family, probe);
            });
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && probe.direct_read_contract
                && probe.callbacks_see_current_planes
                && probe.callback_observations != 0U,
            width == 65U
                ? "65-bit alias leaf planes match packed and proxy values across cross-word slice and force/release"
                : "129-bit alias leaf planes match packed and proxy values across cross-word slice and force/release");
    }
}

} // namespace fsim::tests::runtime
