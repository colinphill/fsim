// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] fsim::runtime::simir::ContainerType fixed_packed_array_type()
{
    auto type = fsim::runtime::simir::ContainerType { };
    type.element_kind
        = fsim::runtime::simir::ContainerElementKind::Packed;
    type.element_width = 4U;
    type.fixed = true;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    return type;
}

[[nodiscard]] fsim::runtime::PackedLogic4 packed_index(
    const std::uint32_t value)
{
    auto bits = std::string(32U, '0');
    for (std::size_t bit = 0U; bit < 32U; ++bit) {
        if ((value & (std::uint32_t { 1U } << bit)) != 0U) {
            bits[31U - bit] = '1';
        }
    }
    return fsim::runtime::PackedLogic4::from_msb_string(bits);
}

struct NativeProxyProbe {
    bool first_member_planes_available { };
    bool proxy_reader_planes_available { };
    bool proxy_direct_read_rejected { };
    bool slot_declined { };
    bool rejected_slot_retained { };
    std::string proxy_value_before_write;
};

struct DirectReadCapabilityProbe {
    bool ordinary_supported { };
    bool element_alias_supported { };
    bool aggregate_proxy_supported { };
    bool ordinary_direct_planes_available { };
    bool ordinary_plane_matches_logical { };
    bool element_plane_matches_logical { };
    std::string ordinary_value;
    std::string element_value;
    std::string aggregate_value;
};

class DirectReadCapabilityExecutor final
    : public fsim::runtime::simir::ProcessExecutor {
public:
    DirectReadCapabilityExecutor(
        const fsim::runtime::simir::SignalId ordinary,
        const fsim::runtime::simir::SignalId element,
        const fsim::runtime::simir::SignalId aggregate,
        DirectReadCapabilityProbe& probe)
        : ordinary_(ordinary)
        , element_(element)
        , aggregate_(aggregate)
        , probe_(probe)
    {
    }

    [[nodiscard]] fsim::runtime::simir::ProcessResumeResult resume(
        fsim::runtime::simir::ProcessExecutionContext& context,
        const fsim::runtime::simir::InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "unexpected direct-read capability resume PC"
            };
        }
        probe_.ordinary_supported
            = context.supports_direct_signal_read(ordinary_);
        probe_.element_alias_supported
            = context.supports_direct_signal_read(element_);
        probe_.aggregate_proxy_supported
            = context.supports_direct_signal_read(aggregate_);
        const auto direct_aval = context.direct_signal_aval();
        const auto direct_bval = context.direct_signal_bval();
        probe_.ordinary_direct_planes_available
            = !direct_aval.empty() && direct_aval.size() == direct_bval.size();
        if (ordinary_ < direct_aval.size()
            && ordinary_ < direct_bval.size()) {
            const auto word = context.read_signal_word(ordinary_);
            probe_.ordinary_plane_matches_logical
                = fsim::runtime::Logic4Word {
                    word.width, direct_aval[ordinary_], direct_bval[ordinary_]
                } == word;
        }
        probe_.ordinary_value
            = context.read_signal(ordinary_).to_msb_string();
        const auto element_aval = context.direct_signal_aval();
        const auto element_bval = context.direct_signal_bval();
        if (element_ < element_aval.size()
            && element_ < element_bval.size()) {
            const auto word = context.read_signal_word(element_);
            probe_.element_plane_matches_logical
                = context.supports_direct_signal_read(element_)
                && element_aval[element_] == word.aval
                && element_bval[element_] == word.bval;
        }
        probe_.element_value
            = context.read_signal(element_).to_msb_string();
        probe_.aggregate_value
            = context.read_signal(aggregate_).to_msb_string();
        return { 0U, 1U };
    }

private:
    fsim::runtime::simir::SignalId ordinary_ { };
    fsim::runtime::simir::SignalId element_ { };
    fsim::runtime::simir::SignalId aggregate_ { };
    DirectReadCapabilityProbe& probe_;
};

class ProxyNativeFallbackExecutor final
    : public fsim::runtime::simir::ProcessExecutor {
public:
    ProxyNativeFallbackExecutor(
        const fsim::runtime::simir::ProcessId process,
        const fsim::runtime::simir::SignalId proxy,
        NativeProxyProbe& probe,
        const bool read_proxy,
        const bool write_proxy)
        : process_(process)
        , proxy_(proxy)
        , probe_(probe)
        , read_proxy_(read_proxy)
        , write_proxy_(write_proxy)
    {
    }

    [[nodiscard]] fsim::runtime::simir::ProcessResumeResult resume(
        fsim::runtime::simir::ProcessExecutionContext& context,
        const fsim::runtime::simir::InstructionIndex start) override
    {
        using namespace fsim::runtime;
        using namespace fsim::runtime::simir;

        const auto planes_available_with_proxy_fallback = [&] {
            return !context.direct_signal_aval().empty()
                && context.direct_signal_aval().size()
                    == context.direct_signal_bval().size()
                && !context.direct_wide_signal_aval().empty()
                && context.direct_wide_signal_aval().size()
                    == context.direct_wide_signal_bval().size()
                && !context.direct_wide_signal_offsets().empty()
                && !context.supports_direct_signal_read(proxy_);
        };

        if (start == 0U) {
            if (process_ == 0U) {
                probe_.first_member_planes_available
                    = planes_available_with_proxy_fallback();
            }
            if (read_proxy_) {
                probe_.proxy_reader_planes_available
                    = planes_available_with_proxy_fallback();
                probe_.proxy_direct_read_rejected
                    = !context.supports_direct_signal_read(proxy_);
                probe_.proxy_value_before_write
                    = context.read_signal(proxy_).to_msb_string();
            }
            if (write_proxy_) {
                std::uint32_t active { 1U };
                std::uint64_t aval { UINT64_C(0x55) };
                std::uint64_t bval { };
                std::uint64_t mask { UINT64_C(0xff) };
                const ProcessUpdateSlotView slot {
                    proxy_, 8U, 1U, &active, &aval, &bval, &mask
                };
                const ProcessUpdateSlotBatch batch {
                    process_, std::span { &slot, 1U }, { }
                };
                const auto consumed = context
                    .write_validated_update_slot_batches(
                        std::span { &batch, 1U });
                probe_.slot_declined = !consumed;
                probe_.rejected_slot_retained
                    = active == 1U && mask == UINT64_C(0xff);
                if (!consumed) {
                    const ProcessUpdateWord update {
                        proxy_, Logic4Word { 8U, aval, bval }, 0U, false
                    };
                    context.write_validated_update_words(
                        std::span { &update, 1U });
                }
            }
            // Process zero contains only Halt. The proxy reader reaches the
            // WaitFor below before its second, final Halt boundary.
            return process_ == 0U
                ? ProcessResumeResult { 0U, 1U }
                : ProcessResumeResult { 1U, 2U };
        }
        if (process_ == 1U && start == 2U) {
            return { 2U, 3U };
        }
        throw std::logic_error { "unexpected aggregate-proxy resume PC" };
    }

private:
    fsim::runtime::simir::ProcessId process_ { };
    fsim::runtime::simir::SignalId proxy_ { };
    NativeProxyProbe& probe_;
    bool read_proxy_ { };
    bool write_proxy_ { };
};

class WideProxyReadExecutor final
    : public fsim::runtime::simir::ProcessExecutor {
public:
    WideProxyReadExecutor(
        const fsim::runtime::simir::SignalId proxy,
        fsim::runtime::PackedLogic4 expected,
        bool& read_matches,
        bool& direct_wide_planes_available)
        : proxy_(proxy)
        , expected_(std::move(expected))
        , read_matches_(read_matches)
        , direct_wide_planes_available_(direct_wide_planes_available)
    {
    }

    [[nodiscard]] fsim::runtime::simir::ProcessResumeResult resume(
        fsim::runtime::simir::ProcessExecutionContext& context,
        const fsim::runtime::simir::InstructionIndex start) override
    {
        using namespace fsim::runtime;
        using namespace fsim::runtime::simir;
        if (start == 0U) {
            direct_wide_planes_available_
                = !context.direct_wide_signal_aval().empty()
                && !context.direct_wide_signal_bval().empty()
                && !context.direct_wide_signal_offsets().empty()
                && !context.supports_direct_signal_read(proxy_);
            std::array<std::uint64_t, 3U> aval { };
            std::array<std::uint64_t, 3U> bval { };
            context.read_signal_planes(proxy_, aval, bval, { }, { });
            const auto actual = PackedLogic4::from_word_planes(
                expected_.width(), aval, bval);
            read_matches_ = actual == expected_;
            return { 1U, 2U };
        }
        if (start == 2U) {
            return { 2U, 3U };
        }
        throw std::logic_error { "unexpected wide-proxy resume PC" };
    }

private:
    fsim::runtime::simir::SignalId proxy_ { };
    fsim::runtime::PackedLogic4 expected_;
    bool& read_matches_;
    bool& direct_wide_planes_available_;
};

struct StaleProxyWriteProbe {
    bool stable_writer_shadow_disabled { };
    bool raw_proxy_matches_write { };
    bool logical_proxy_differs_from_raw { };
    bool slot_declined { };
    bool rejected_slot_retained { };
};

class StaleProxyWriteExecutor final
    : public fsim::runtime::simir::ProcessExecutor {
public:
    StaleProxyWriteExecutor(
        const fsim::runtime::simir::ProcessId process,
        const fsim::runtime::simir::SignalId proxy,
        StaleProxyWriteProbe& probe)
        : process_(process)
        , proxy_(proxy)
        , probe_(probe)
    {
    }

    [[nodiscard]] fsim::runtime::simir::ProcessResumeResult resume(
        fsim::runtime::simir::ProcessExecutionContext& context,
        const fsim::runtime::simir::InstructionIndex start) override
    {
        using namespace fsim::runtime;
        using namespace fsim::runtime::simir;
        if (start != 0U) {
            throw std::logic_error {
                "unexpected stale aggregate-proxy writer resume PC"
            };
        }
        const auto stable_writers = context.stable_single_writer_processes();
        const auto raw_aval = context.direct_signal_aval();
        const auto raw_bval = context.direct_signal_bval();
        const auto logical = context.read_signal(proxy_);
        probe_.stable_writer_shadow_disabled
            = proxy_ < stable_writers.size()
            && stable_writers[proxy_]
                == std::numeric_limits<ProcessId>::max();
        probe_.raw_proxy_matches_write
            = proxy_ < raw_aval.size() && proxy_ < raw_bval.size()
            && raw_aval[proxy_] == UINT64_C(0x55)
            && raw_bval[proxy_] == 0U;
        probe_.logical_proxy_differs_from_raw
            = logical == PackedLogic4 { 8U, Logic4::zero }
            && probe_.raw_proxy_matches_write;

        std::uint32_t active { 1U };
        std::uint64_t aval { UINT64_C(0x55) };
        std::uint64_t bval { };
        std::uint64_t mask { UINT64_C(0xff) };
        const ProcessUpdateSlotView slot {
            proxy_, 8U, 1U, &active, &aval, &bval, &mask
        };
        const ProcessUpdateSlotBatch batch {
            process_, std::span { &slot, 1U }, { }
        };
        const auto consumed = context.write_validated_update_slot_batches(
            std::span { &batch, 1U });
        probe_.slot_declined = !consumed;
        probe_.rejected_slot_retained
            = active == 1U && mask == UINT64_C(0xff);
        if (!consumed) {
            const ProcessUpdateWord update {
                proxy_, Logic4Word { 8U, aval, bval }, 0U, false
            };
            context.write_validated_update_words(
                std::span { &update, 1U });
        }
        return { 0U, 1U };
    }

private:
    fsim::runtime::simir::ProcessId process_ { };
    fsim::runtime::simir::SignalId proxy_ { };
    StaleProxyWriteProbe& probe_;
};

void add_proxy_aliases(
    fsim::runtime::simir::Interpreter& interpreter,
    const fsim::runtime::simir::SignalId first,
    const fsim::runtime::simir::SignalId second,
    const fsim::runtime::simir::SignalId proxy,
    const fsim::runtime::simir::ContainerObjectId object)
{
    using namespace fsim::runtime::simir;
    interpreter.add_container_element_signal_alias(
        { object, 0U, first, true, true });
    interpreter.add_container_element_signal_alias(
        { object, 1U, second, true, true });
    interpreter.add_container_aggregate_signal_alias(
        { object, proxy, true, true });
}

struct AliasWriteTraceRecord {
    SchedulerTraceKind kind { };
    SignalId signal { };
    SimulationTick time { };
    std::uint64_t delta { };
    std::optional<SchedulerPhase> phase;
    StableOrder order { };
    std::uint64_t sequence { };
    std::uint64_t systemverilog_round { };
    bool systemverilog { };
    bool end_of_time_slot { };

    bool operator==(const AliasWriteTraceRecord&) const = default;
};

struct AliasWriteTrace {
    std::array<SignalId, 3U> signals { };
    std::vector<AliasWriteTraceRecord> records;
    bool failed { };

    static void record(
        void* context, const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<AliasWriteTrace*>(context);
        if (entry.kind != SchedulerTraceKind::signal_transaction
            && entry.kind != SchedulerTraceKind::signal_change) {
            return;
        }
        if (std::ranges::find(trace.signals, entry.signal)
            == trace.signals.end()) {
            return;
        }
        try {
            trace.records.push_back({
                entry.kind,
                entry.signal,
                entry.time,
                entry.delta,
                entry.phase,
                entry.order,
                entry.sequence,
                entry.systemverilog_round,
                entry.systemverilog,
                entry.end_of_time_slot,
            });
        } catch (...) {
            trace.failed = true;
        }
    }
};

struct AliasWriteSnapshot {
    SignalId transaction_signal { };
    SimulationTick time { };
    std::uint64_t delta { };
    std::array<std::string, 3U> current;
    std::array<std::string, 3U> stored;
    std::array<std::string, 3U> previous;
    std::array<SimulationTick, 3U> last_event { };
    std::array<SimulationTick, 3U> last_active { };
    std::array<bool, 3U> event { };
    std::array<bool, 3U> active { };
    std::string raw_proxy_driver;

    bool operator==(const AliasWriteSnapshot&) const = default;
};

struct AliasRawDriverChange {
    SignalId signal { };
    SimulationTick time { };
    std::uint64_t delta { };
    std::string raw_proxy_driver;
    std::string stored_proxy;
    std::string current_proxy;

    bool operator==(const AliasRawDriverChange&) const = default;
};

class AliasWriteTransactionObserver final : public ProcessExecutor {
public:
    AliasWriteTransactionObserver(
        Interpreter& interpreter,
        const ProcessId writer,
        const SignalId transaction_signal,
        const std::array<SignalId, 3U> signals,
        const SignalId proxy,
        std::vector<AliasWriteSnapshot>& snapshots)
        : interpreter_ { interpreter }
        , writer_ { writer }
        , transaction_signal_ { transaction_signal }
        , signals_ { signals }
        , proxy_ { proxy }
        , snapshots_ { snapshots }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (!armed_) {
            require(start == 0U,
                "alias transaction observer starts at its wait");
            armed_ = true;
            return wait_at(0U, 1U);
        }
        require(start == 1U || start == 2U,
            "alias transaction observer resumes at its wait loop");

        AliasWriteSnapshot snapshot;
        snapshot.transaction_signal = transaction_signal_;
        snapshot.time = context.current_time();
        snapshot.delta = interpreter_.scheduler().delta();
        for (std::size_t index = 0U; index < signals_.size(); ++index) {
            const auto signal = signals_[index];
            snapshot.current[index]
                = context.read_signal(signal).to_msb_string();
            snapshot.stored[index]
                = interpreter_.stored_signal_value(signal).to_msb_string();
            const auto previous = context.signal_last_value_word(signal);
            const auto previous_value = PackedLogic4::from_aval_bval(
                previous.width, previous.aval, previous.bval);
            snapshot.previous[index] = previous_value.to_msb_string();
            snapshot.last_event[index] = context.signal_last_event(signal);
            snapshot.last_active[index] = context.signal_last_active(signal);
            snapshot.event[index] = context.signal_event(signal);
            snapshot.active[index] = context.signal_active(signal);
        }
        snapshot.raw_proxy_driver
            = interpreter_.driver_value(writer_, proxy_).to_msb_string();
        snapshots_.push_back(std::move(snapshot));
        return wait_at(1U, 2U);
    }

private:
    [[nodiscard]] static ProcessResumeResult wait_at(
        const InstructionIndex instruction,
        const InstructionIndex next_instruction)
    {
        ProcessResumeResult result { instruction, next_instruction };
        result.external.kind = ExternalSuspendKind::wait_sensitivity;
        return result;
    }

    Interpreter& interpreter_;
    ProcessId writer_ { };
    SignalId transaction_signal_ { };
    std::array<SignalId, 3U> signals_ { };
    SignalId proxy_ { };
    std::vector<AliasWriteSnapshot>& snapshots_;
    bool armed_ { };
};

struct AliasWriteResult {
    RunStatus status { RunStatus::completed };
    std::array<SignalId, 3U> signals { };
    std::array<std::string, 3U> current;
    std::array<std::string, 3U> stored;
    std::string raw_proxy_driver;
    std::vector<AliasWriteSnapshot> snapshots;
    std::vector<AliasWriteTraceRecord> trace;
    std::vector<AliasRawDriverChange> raw_driver_changes;
    bool trace_failed { };
};

void check_proxy_slice_matches_full_leaf_write()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto run_case = [&](const bool write_physical_leaf) {
        Interpreter interpreter;
        const auto high_leaf = interpreter.add_signal({
            "top.proxy_leaf_differential[1]",
            PackedLogic4 { 4U, Logic4::z }, ResolutionKind::sv_wire });
        const auto low_leaf = interpreter.add_signal({
            "top.proxy_leaf_differential[0]",
            PackedLogic4 { 4U, Logic4::z }, ResolutionKind::sv_wire });
        const auto proxy = interpreter.add_signal({
            "top.proxy_leaf_differential",
            PackedLogic4 { 8U, Logic4::z }, ResolutionKind::sv_wire });
        const auto object = interpreter.add_container_object({
            "top.proxy_leaf_differential",
            ContainerValue {
                fixed_packed_array_type(),
                { PackedLogic4 { 4U, Logic4::z },
                    PackedLogic4 { 4U, Logic4::z } },
                { } },
            std::nullopt });
        add_proxy_aliases(
            interpreter, high_leaf, low_leaf, proxy, object);
        const auto trigger = interpreter.add_signal({
            "top.proxy_leaf_differential_trigger",
            PackedLogic4::from_msb_string("0") });
        const auto input = interpreter.add_signal({
            "top.proxy_leaf_differential_input",
            PackedLogic4::from_msb_string("1010") });

        Process writer;
        writer.id = 0U;
        writer.name = write_physical_leaf
            ? "proxy_leaf_differential_leaf_writer"
            : "proxy_leaf_differential_proxy_writer";
        writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        writer.register_count = 1U;
        writer.static_sensitivity = { { trigger, EdgeKind::any } };
        // Both encodings retain the original proxy owner. Registration expands
        // this exact full-family owner into the same physical leaf slots.
        writer.driver_regions = { { proxy, 0U, 0U, true } };
        if (write_physical_leaf) {
            writer.operations = {
                ReadSignal { 0U, input },
                WriteUpdate { low_leaf, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        } else {
            writer.operations = {
                ReadSignal { 0U, input },
                WriteUpdateSlice { proxy, 0U, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        }
        const auto writer_id = interpreter.add_process(std::move(writer));

        const std::array<SignalId, 3U> signals {
            high_leaf, low_leaf, proxy
        };
        AliasWriteResult result;
        result.signals = signals;
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            const auto signal = signals[index];
            Process observer;
            observer.id = static_cast<ProcessId>(index + 1U);
            observer.name = "proxy_leaf_differential_transaction_observer_"
                + std::to_string(signal);
            observer.static_sensitivity = {
                { signal, EdgeKind::transaction }
            };
            observer.operations = {
                WaitSensitivity { }, WaitSensitivity { }, Jump { 1U }
            };
            const auto observer_id
                = interpreter.add_process(std::move(observer));
            interpreter.set_process_executor(observer_id,
                std::make_unique<AliasWriteTransactionObserver>(
                    interpreter, writer_id, signal, signals, proxy,
                    result.snapshots));
        }

        AliasWriteTrace trace { signals, { }, false };
        interpreter.scheduler().set_trace_hook(&trace, &AliasWriteTrace::record);
        interpreter.set_driver_change_hook(
            [&](const ProcessId process,
                const SignalId signal,
                const SimulationTick time) {
                if (process != writer_id
                    || (signal != high_leaf && signal != low_leaf)) {
                    return;
                }
                result.raw_driver_changes.push_back({
                    signal,
                    time,
                    interpreter.scheduler().delta(),
                    interpreter.driver_value(writer_id, proxy).to_msb_string(),
                    interpreter.stored_signal_value(proxy).to_msb_string(),
                    interpreter.signal_value(proxy).to_msb_string(),
                });
            });

        interpreter.schedule_signal_at(
            input, PackedLogic4::from_msb_string("0110"), 2U, 0U);
        interpreter.schedule_signal_at(
            trigger, PackedLogic4::from_msb_string("1"), 2U, 1U);
        result.status = interpreter.run().status;
        interpreter.scheduler().set_trace_hook(nullptr, nullptr);
        result.trace = std::move(trace.records);
        result.trace_failed = trace.failed;
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            result.current[index]
                = interpreter.signal_value(signals[index]).to_msb_string();
            result.stored[index]
                = interpreter.stored_signal_value(signals[index])
                    .to_msb_string();
        }
        result.raw_proxy_driver
            = interpreter.driver_value(writer_id, proxy).to_msb_string();
        return result;
    };

    const auto proxy_write = run_case(false);
    const auto leaf_write = run_case(true);
    const auto count_trace = [](
        const auto& trace, const SchedulerTraceKind kind,
        const SignalId signal) {
        return static_cast<std::size_t>(std::ranges::count_if(
            trace, [&](const AliasWriteTraceRecord& record) {
                return record.kind == kind && record.signal == signal;
            }));
    };
    const auto count_snapshots = [](
        const auto& snapshots, const SignalId signal) {
        return static_cast<std::size_t>(std::ranges::count_if(
            snapshots, [&](const AliasWriteSnapshot& snapshot) {
                return snapshot.transaction_signal == signal;
            }));
    };
    const auto find_snapshot = [](
        const auto& snapshots, const SignalId signal,
        const SimulationTick time) -> const AliasWriteSnapshot* {
        const auto found = std::ranges::find_if(
            snapshots, [&](const AliasWriteSnapshot& snapshot) {
                return snapshot.transaction_signal == signal
                    && snapshot.time == time;
            });
        return found == snapshots.end() ? nullptr : &*found;
    };
    require(
        proxy_write.status == RunStatus::completed
            && leaf_write.status == RunStatus::completed
            && !proxy_write.trace_failed && !leaf_write.trace_failed
            && proxy_write.current == leaf_write.current
            && proxy_write.stored == leaf_write.stored
            && proxy_write.raw_proxy_driver == leaf_write.raw_proxy_driver
            && proxy_write.snapshots == leaf_write.snapshots
            && proxy_write.trace == leaf_write.trace
            && proxy_write.raw_driver_changes == leaf_write.raw_driver_changes,
        "proxy slice writes and same-owner full-leaf writes preserve two-activation publication traces and observations");
    require(
        proxy_write.current
                == std::array<std::string, 3U> {
                    "ZZZZ", "0110", "ZZZZ0110" }
            && proxy_write.stored == proxy_write.current
            && proxy_write.raw_proxy_driver == "ZZZZ0110"
            && proxy_write.snapshots.size() == 4U
            && count_snapshots(proxy_write.snapshots,
                proxy_write.signals[0U]) == 0U
            && count_snapshots(proxy_write.snapshots,
                proxy_write.signals[1U]) == 2U
            && count_snapshots(proxy_write.snapshots,
                proxy_write.signals[2U]) == 2U
            && count_trace(proxy_write.trace,
                SchedulerTraceKind::signal_transaction,
                proxy_write.signals[0U]) == 0U
            && count_trace(proxy_write.trace,
                SchedulerTraceKind::signal_transaction,
                proxy_write.signals[1U]) == 2U
            && count_trace(proxy_write.trace,
                SchedulerTraceKind::signal_transaction,
                proxy_write.signals[2U]) == 2U,
        "both write encodings keep the original proxy driver and exactly one transaction per touched leaf plus proxy per activation");
    const auto* low_first = find_snapshot(
        proxy_write.snapshots, proxy_write.signals[1U], 0U);
    const auto* proxy_first = find_snapshot(
        proxy_write.snapshots, proxy_write.signals[2U], 0U);
    const auto* low_second = find_snapshot(
        proxy_write.snapshots, proxy_write.signals[1U], 2U);
    const auto* proxy_second = find_snapshot(
        proxy_write.snapshots, proxy_write.signals[2U], 2U);
    require(
        low_first != nullptr && proxy_first != nullptr
            && low_second != nullptr && proxy_second != nullptr
            && low_first->current
                == std::array<std::string, 3U> {
                    "ZZZZ", "1010", "ZZZZ1010" }
            && low_first->stored == low_first->current
            && low_first->previous
                == std::array<std::string, 3U> {
                    "ZZZZ", "ZZZZ", "ZZZZZZZZ" }
            && low_first->event
                == std::array<bool, 3U> { false, true, true }
            && low_first->active
                == std::array<bool, 3U> { false, true, true }
            && low_first->last_event
                == std::array<SimulationTick, 3U> {
                    std::numeric_limits<SimulationTick>::max(), 0U, 0U }
            && low_first->last_active == low_first->last_event
            && low_first->raw_proxy_driver == "ZZZZ1010"
            && low_second->current
                == std::array<std::string, 3U> {
                    "ZZZZ", "0110", "ZZZZ0110" }
            && low_second->stored == low_second->current
            && low_second->previous
                == std::array<std::string, 3U> {
                    "ZZZZ", "1010", "ZZZZ1010" }
            && low_second->event
                == std::array<bool, 3U> { false, true, true }
            && low_second->active
                == std::array<bool, 3U> { false, true, true }
            && low_second->last_event
                == std::array<SimulationTick, 3U> {
                    std::numeric_limits<SimulationTick>::max(), 0U, 0U }
            && low_second->last_active == low_second->last_event
            && low_second->raw_proxy_driver == "ZZZZ0110"
            && proxy_first->current == low_first->current
            && proxy_second->current == low_second->current,
        "both activations retain leaf/proxy current, previous, event, transaction and raw-driver observations");
    require(
        proxy_write.raw_driver_changes.size() == 2U
            && proxy_write.raw_driver_changes[0U].signal
                == proxy_write.signals[1U]
            && proxy_write.raw_driver_changes[0U].raw_proxy_driver
                == "ZZZZ1010"
            && proxy_write.raw_driver_changes[0U].stored_proxy
                == "ZZZZZZZZ"
            && proxy_write.raw_driver_changes[0U].current_proxy
                == "ZZZZZZZZ"
            && proxy_write.raw_driver_changes[1U].signal
                == proxy_write.signals[1U]
            && proxy_write.raw_driver_changes[1U].raw_proxy_driver
                == "ZZZZ0110"
            && proxy_write.raw_driver_changes[1U].stored_proxy
                == "ZZZZ1010"
            && proxy_write.raw_driver_changes[1U].current_proxy
                == "ZZZZ1010",
        "proxy and direct-leaf encodings expose the same raw driver before stored/current publication");
}

void run_aggregate_proxy_sampled_data_matches_packed_reference()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto initial_data = PackedLogic4::from_msb_string("11000011");
    const auto changed_data = PackedLogic4::from_msb_string("10100101");
    const auto initial_first = PackedLogic4::from_msb_string("1100");
    const auto initial_second = PackedLogic4::from_msb_string("0011");
    const auto changed_first = PackedLogic4::from_msb_string("1010");
    const auto changed_second = PackedLogic4::from_msb_string("0101");

    const auto run_reference = [&](const bool use_aggregate_proxy) {
        Interpreter interpreter;
        SignalId data { };
        SignalId first_leaf { };
        SignalId second_leaf { };
        ContainerObjectId object { };
        if (use_aggregate_proxy) {
            first_leaf = interpreter.add_signal({
                "top.sampled_words[1]", initial_first,
                ResolutionKind::sv_wire });
            second_leaf = interpreter.add_signal({
                "top.sampled_words[0]", initial_second,
                ResolutionKind::sv_wire });
            data = interpreter.add_signal({
                "top.sampled_words", PackedLogic4 { 8U, Logic4::zero },
                ResolutionKind::sv_wire });
            object = interpreter.add_container_object({
                "top.sampled_words",
                ContainerValue {
                    fixed_packed_array_type(),
                    { initial_first, initial_second }, { } },
                std::nullopt });
            add_proxy_aliases(
                interpreter, first_leaf, second_leaf, data, object);
        } else {
            data = interpreter.add_signal({
                "top.sampled_data", initial_data, ResolutionKind::sv_wire });
        }
        const auto clock = interpreter.add_signal({
            "top.sampled_clock", PackedLogic4::from_msb_string("0") });
        std::array<SignalId, 5U> outputs { };
        for (std::size_t index = 0U; index < outputs.size(); ++index) {
            outputs[index] = interpreter.add_signal({
                "top.sampled_result_" + std::to_string(index),
                PackedLogic4 { 8U, Logic4::zero } });
        }

        const ReadSignal past {
            .destination = 0U,
            .signal = data,
            .kind = SignalReadKind::past,
            .ticks = 1U,
            .clock = clock,
            .clock_edge = SampledClockEdge::positive,
            .gate = std::nullopt,
        };
        Process reader;
        reader.id = 0U;
        reader.name = "aggregate_proxy_sampled_data_reader";
        reader.register_count = 1U;
        reader.reactive = true;
        reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        reader.static_sensitivity.push_back({ clock, EdgeKind::posedge });
        reader.operations = {
            past,
            WriteBlocking { outputs[0], 0U },
            ReadSignal {
                .destination = 0U,
                .signal = data,
                .kind = SignalReadKind::future,
                .ticks = 1U,
                .clock = std::nullopt,
                .clock_edge = SampledClockEdge::any,
                .gate = std::nullopt,
            },
            WriteBlocking { outputs[1], 0U },
            WaitSensitivity { },
            past,
            WriteBlocking { outputs[2], 0U },
            WaitSensitivity { },
            past,
            WriteBlocking { outputs[3], 0U },
            ReadSignal {
                .destination = 0U,
                .signal = data,
                .kind = SignalReadKind::sampled,
                .ticks = 1U,
                .clock = clock,
                .clock_edge = SampledClockEdge::positive,
                .gate = std::nullopt,
            },
            WriteBlocking { outputs[4], 0U },
            Halt { },
        };
        (void)interpreter.add_process(std::move(reader));

        interpreter.schedule_signal_at(
            clock, PackedLogic4::from_msb_string("1"), 1U, 0U);
        interpreter.schedule_signal_at(
            clock, PackedLogic4::from_msb_string("0"), 2U, 0U);
        interpreter.schedule_signal_at(
            clock, PackedLogic4::from_msb_string("1"), 3U, 0U);
        if (use_aggregate_proxy) {
            interpreter.schedule_signal_at(first_leaf, changed_first, 2U, 0U);
            interpreter.schedule_signal_at(
                second_leaf, changed_second, 2U, 0U);
        } else {
            interpreter.schedule_signal_at(data, changed_data, 2U, 0U);
        }

        const auto run = interpreter.run();
        require(
            run.status == RunStatus::completed,
            "packed and aggregate-proxy sampled readers complete");
        std::array<PackedLogic4, 5U> result {
            PackedLogic4 { }, PackedLogic4 { }, PackedLogic4 { },
            PackedLogic4 { }, PackedLogic4 { }
        };
        for (std::size_t index = 0U; index < result.size(); ++index) {
            result[index] = interpreter.signal_value(outputs[index]);
        }
        return result;
    };

    const auto packed_reference = run_reference(false);
    const auto aggregate_proxy = run_reference(true);
    require(
        aggregate_proxy == packed_reference,
        "aggregate-proxy sampled DATA matches an ordinary packed signal with "
        "a separate scalar clock");
    require(
        packed_reference
            == std::array<PackedLogic4, 5U> {
                initial_data, initial_data, initial_data,
                initial_data, changed_data },
        "sampled defaults, future reads, past reads, and captured samples "
        "retain their expected slot values");
}

void run_aggregate_proxy_sampled_clock_matches_packed_reference()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    enum class ClockRoute : std::uint8_t {
        packed,
        aggregate_whole,
        aggregate_low_leaf,
    };

    const auto zero = PackedLogic4::from_msb_string("0");
    const auto one = PackedLogic4::from_msb_string("1");
    const auto zero_clock = PackedLogic4::from_msb_string("00000000");
    const auto low_clock = PackedLogic4::from_msb_string("00000001");
    const auto zero_nibble = PackedLogic4::from_msb_string("0000");
    const auto low_nibble = PackedLogic4::from_msb_string("0001");

    const auto run_reference = [&](const ClockRoute route) {
        Interpreter interpreter;
        const auto data = interpreter.add_signal({
            "top.sampled_clock_data", zero });
        SignalId clock { };
        SignalId low_leaf { };
        if (route == ClockRoute::packed) {
            clock = interpreter.add_signal({
                "top.packed_sampled_clock", zero_clock });
        } else {
            const auto high_leaf = interpreter.add_signal({
                "top.aggregate_clock[1]", zero_nibble,
                ResolutionKind::sv_wire });
            low_leaf = interpreter.add_signal({
                "top.aggregate_clock[0]", zero_nibble,
                ResolutionKind::sv_wire });
            clock = interpreter.add_signal({
                "top.aggregate_clock", zero_clock,
                ResolutionKind::sv_wire });
            const auto object = interpreter.add_container_object({
                "top.aggregate_clock",
                ContainerValue {
                    fixed_packed_array_type(),
                    { zero_nibble, zero_nibble }, { } },
                std::nullopt });
            add_proxy_aliases(
                interpreter, high_leaf, low_leaf, clock, object);
        }

        std::array<SignalId, 4U> results { };
        for (std::size_t index = 0U; index < results.size(); ++index) {
            results[index] = interpreter.add_signal({
                "top.sampled_clock_result_" + std::to_string(index), zero });
        }

        const ReadSignal past_one {
            .destination = 0U,
            .signal = data,
            .kind = SignalReadKind::past,
            .ticks = 1U,
            .clock = clock,
            .clock_edge = SampledClockEdge::positive,
            .gate = std::nullopt,
        };
        const ReadSignal past_two {
            .destination = 1U,
            .signal = data,
            .kind = SignalReadKind::past,
            .ticks = 2U,
            .clock = clock,
            .clock_edge = SampledClockEdge::positive,
            .gate = std::nullopt,
        };
        const ReadSignal sampled_now {
            .destination = 0U,
            .signal = data,
            .kind = SignalReadKind::sampled,
            .ticks = 1U,
            .clock = clock,
            .clock_edge = SampledClockEdge::positive,
            .gate = std::nullopt,
        };
        Process reader;
        reader.id = 0U;
        reader.name = "late_aggregate_proxy_clock_history_reader";
        reader.register_count = 2U;
        reader.reactive = true;
        reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        // There is deliberately no clock sensitivity: all clock edges happen
        // while this reader remains in its timed wait.
        reader.operations = {
            WaitFor { 6U },
            past_one,
            WriteBlocking { results[0], 0U },
            past_two,
            WriteBlocking { results[1], 1U },
            sampled_now,
            WriteBlocking { results[2], 0U },
            past_two,
            WriteBlocking { results[3], 1U },
            Halt { },
        };
        (void)interpreter.add_process(std::move(reader));

        // Slot-start snapshots at the quiet t=2/t=4 clock edges are 1/0.
        // The late t=6 sampled value is 1, so lazy sampling at read time would
        // be observably different from the last edge sample.
        interpreter.schedule_signal_at(data, one, 1U, 0U);
        interpreter.schedule_signal_at(data, zero, 3U, 0U);
        interpreter.schedule_signal_at(data, one, 5U, 0U);
        const auto set_clock = [&](const PackedLogic4& value,
                                   const SimulationTick time) {
            if (route == ClockRoute::aggregate_low_leaf) {
                interpreter.schedule_signal_at(
                    low_leaf,
                    value.get(0U) == Logic4::one ? low_nibble : zero_nibble,
                    time,
                    0U);
                return;
            }
            interpreter.schedule_signal_at(clock, value, time, 0U);
        };
        // Positive edges at t=2 and t=4 are quiet; the reader is not
        // activated until its late t=6 timeout.
        set_clock(low_clock, 2U);
        set_clock(zero_clock, 3U);
        set_clock(low_clock, 4U);
        set_clock(zero_clock, 5U);

        const auto run = interpreter.run();
        require(
            run.status == RunStatus::completed,
            "late sampled-clock readers complete without clock activation");
        std::array<PackedLogic4, 4U> observed {
            zero, zero, zero, zero
        };
        for (std::size_t index = 0U; index < observed.size(); ++index) {
            observed[index] = interpreter.signal_value(results[index]);
        }
        return observed;
    };

    const auto packed_reference = run_reference(ClockRoute::packed);
    const auto whole_proxy = run_reference(ClockRoute::aggregate_whole);
    const auto leaf_proxy = run_reference(ClockRoute::aggregate_low_leaf);
    const std::array<PackedLogic4, 4U> expected {
        zero, one, zero, one
    };
    require(
        packed_reference == expected,
        "ordinary packed clocks retain both quiet positive-edge samples");
    if (whole_proxy != packed_reference
        || leaf_proxy != packed_reference) {
        for (std::size_t index = 0U;
            index < packed_reference.size(); ++index) {
            std::cerr << "clock[" << index << "] packed="
                      << packed_reference[index].to_msb_string()
                      << " whole=" << whole_proxy[index].to_msb_string()
                      << " leaf=" << leaf_proxy[index].to_msb_string()
                      << '\n';
        }
    }
    require(
        whole_proxy == packed_reference,
        "whole-family aggregate-proxy clocks retain the packed-clock history");
    require(
        leaf_proxy == packed_reference,
        "single-leaf aggregate-proxy clocks retain the packed-clock history");
}

void test_multidimensional_element_direct_reads()
{
    for (const bool three_dimensions : { false, true }) {
        Interpreter interpreter;
        auto type = fixed_packed_array_type();
        type.index_left = 2;
        type.index_right = 1;
        type.dimensions = { { 2, 1 }, { -1, 0 } };
        if (three_dimensions) {
            type.dimensions.push_back({ 7, 7 });
        }
        const auto ordinary = interpreter.add_signal({
            "multidimensional.ordinary", PackedLogic4::from_msb_string("0011") });
        const auto proxy = interpreter.add_signal({
            "multidimensional.array", PackedLogic4 { 16U, Logic4::z },
            ResolutionKind::sv_wire });
        std::array<SignalId, 4U> leaves;
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            leaves[ordinal] = interpreter.add_signal({
                "multidimensional.leaf" + std::to_string(ordinal),
                PackedLogic4 { 4U, Logic4::z }, ResolutionKind::sv_wire });
        }
        const auto object = interpreter.add_container_object({
            "multidimensional.array", ContainerValue { type,
                std::vector<PackedLogic4>(4U, PackedLogic4 { 4U, Logic4::z }),
                { } }, std::nullopt });
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            interpreter.add_container_element_signal_alias(
                { object, static_cast<std::uint32_t>(ordinal),
                    leaves[ordinal], true, true });
        }
        interpreter.add_container_aggregate_signal_alias(
            { object, proxy, true, true });
        const std::array<std::string_view, 4U> words {
            "1010", "0X01", "11Z0", "0101" };
        ContainerValue replacement { type, { }, { } };
        for (const auto word : words) {
            replacement.elements.push_back(PackedLogic4::from_msb_string(word));
        }
        interpreter.deposit_container_object(object, replacement);
        std::array<DirectReadCapabilityProbe, 4U> probes;
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            Process process;
            process.id = static_cast<ProcessId>(ordinal);
            process.name = "multidimensional_direct_probe";
            process.operations = { Halt { } };
            const auto id = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(id,
                std::make_unique<DirectReadCapabilityExecutor>(
                    ordinary, leaves[ordinal], proxy, probes[ordinal]));
        }
        require(interpreter.run().status == RunStatus::completed,
            "multidimensional direct-read probes complete");
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            const auto& probe = probes[ordinal];
            require(probe.ordinary_supported && probe.element_alias_supported
                    && !probe.aggregate_proxy_supported
                    && probe.ordinary_plane_matches_logical
                    && probe.element_plane_matches_logical
                    && probe.element_value == words[ordinal]
                    && probe.aggregate_value == "10100X0111Z00101",
                "complete multidimensional families expose each exact leaf plane and keep proxies checked");
        }
    }
}

void test_sparse_element_alias_dispatch()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto type = fixed_packed_array_type();
    const auto initial = std::vector<PackedLogic4> {
        PackedLogic4::from_msb_string("0001"),
        PackedLogic4::from_msb_string("0011") };

    Interpreter unaliased;
    const auto plain_object = unaliased.add_container_object({
        "top.plain_array", ContainerValue { type, initial, { } },
        std::nullopt });
    const auto first_read = unaliased.add_signal({
        "top.plain_array.first_read", PackedLogic4 { 4U, Logic4::zero } });
    const auto second_read = unaliased.add_signal({
        "top.plain_array.second_read", PackedLogic4 { 4U, Logic4::zero } });
    Process plain_process;
    plain_process.id = 0U;
    plain_process.name = "unaliased_fixed_array_access";
    plain_process.register_count = 3U;
    plain_process.container_register_count = 1U;
    plain_process.container_register_types = { type };
    plain_process.operations = {
        ReadContainerObject { 0U, plain_object },
        LoadConstant { 0U, packed_index(0U) },
        ContainerRead { 1U, 0U, 0U, true },
        WriteBlocking { first_read, 1U },
        LoadConstant { 0U, packed_index(1U) },
        ContainerRead { 2U, 0U, 0U, true },
        WriteBlocking { second_read, 2U },
        LoadConstant { 0U, packed_index(1U) },
        LoadConstant { 1U, PackedLogic4::from_msb_string("1010") },
        WriteContainerObjectElement {
            plain_object, 0U, 1U, true, false, false,
            std::nullopt, std::nullopt },
        LoadConstant { 0U, packed_index(0U) },
        LoadConstant { 1U, PackedLogic4::from_msb_string("1100") },
        WriteContainerObjectElement {
            plain_object, 0U, 1U, true, false, false,
            std::nullopt, std::nullopt },
        LoadConstant { 0U, packed_index(0U) },
        LoadConstant { 1U, PackedLogic4::from_msb_string("10") },
        LoadConstant { 2U, packed_index(0U) },
        WriteContainerObjectElement {
            plain_object, 0U, 1U, true, false, false,
            std::nullopt, DynamicPartIndex { 2U, 3, 0, 0U, 2U, true, true } },
        Halt { },
    };
    (void)unaliased.add_process(std::move(plain_process));
    require(
        unaliased.run().status == RunStatus::completed
            && unaliased.signal_value(first_read)
                == PackedLogic4::from_msb_string("0011")
            && unaliased.signal_value(second_read)
                == PackedLogic4::from_msb_string("0001")
            && unaliased.container_object_value(plain_object).elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010"),
                    PackedLogic4::from_msb_string("1110") },
        "fixed arrays without leaf aliases retain ordinary reads and writes");
    auto plain_replacement = ContainerValue {
        type,
        { PackedLogic4::from_msb_string("0101"),
            PackedLogic4::from_msb_string("0110") },
        { } };
    unaliased.deposit_container_object(plain_object, plain_replacement);
    require(
        unaliased.container_object_value(plain_object).elements
            == plain_replacement.elements,
        "a fixed array without leaf aliases accepts a whole-object write");

    Interpreter partial;
    const auto owned_signal = partial.add_signal({
        "top.partial_array[1]", PackedLogic4::from_msb_string("1001"),
        ResolutionKind::sv_wire });
    const auto partial_object = partial.add_container_object({
        "top.partial_array",
        ContainerValue {
            type,
            { PackedLogic4::from_msb_string("0000"),
                PackedLogic4::from_msb_string("0011") },
            { } },
        std::nullopt });
    partial.add_container_element_signal_alias(
        { partial_object, 0U, owned_signal, true, true });
    const auto incomplete_proxy = partial.add_signal({
        "top.partial_array", PackedLogic4 { 8U, Logic4::zero },
        ResolutionKind::sv_wire });
    bool rejected_incomplete_proxy { };
    try {
        partial.add_container_aggregate_signal_alias(
            { partial_object, incomplete_proxy, true, true });
    } catch (const std::invalid_argument&) {
        rejected_incomplete_proxy = true;
    }
    require(rejected_incomplete_proxy,
        "a sparse element map cannot register an aggregate signal projection");
    const auto owned_read = partial.add_signal({
        "top.partial_array.owned_read", PackedLogic4 { 4U, Logic4::zero } });
    const auto unowned_read = partial.add_signal({
        "top.partial_array.unowned_read", PackedLogic4 { 4U, Logic4::zero } });
    require(
        partial.container_object_value(partial_object).elements
            == std::vector<PackedLogic4> {
                PackedLogic4::from_msb_string("1001"),
                PackedLogic4::from_msb_string("0011") },
        "a partial element map reads owned signals and keeps unowned storage");
    const auto& retained_partial
        = partial.container_object_value(partial_object);
    const auto* const retained_partial_elements
        = retained_partial.elements.data();

    Process partial_process;
    partial_process.id = 0U;
    partial_process.name = "partial_fixed_array_access";
    partial_process.register_count = 3U;
    partial_process.container_register_count = 1U;
    partial_process.container_register_types = { type };
    partial_process.driver_regions.push_back(
        { owned_signal, 0U, 4U, false });
    partial_process.operations = {
        ReadContainerObject { 0U, partial_object },
        LoadConstant { 0U, packed_index(1U) },
        ContainerRead { 1U, 0U, 0U, true },
        WriteBlocking { owned_read, 1U },
        LoadConstant { 0U, packed_index(0U) },
        ContainerRead { 2U, 0U, 0U, true },
        WriteBlocking { unowned_read, 2U },
        LoadConstant { 0U, packed_index(1U) },
        LoadConstant { 1U, PackedLogic4::from_msb_string("1010") },
        WriteContainerObjectElement {
            partial_object, 0U, 1U, true, false, false,
            std::nullopt, std::nullopt },
        LoadConstant { 0U, packed_index(0U) },
        LoadConstant { 1U, PackedLogic4::from_msb_string("1100") },
        WriteContainerObjectElement {
            partial_object, 0U, 1U, true, false, false,
            std::nullopt, std::nullopt },
        LoadConstant { 0U, packed_index(0U) },
        LoadConstant { 1U, PackedLogic4::from_msb_string("10") },
        LoadConstant { 2U, packed_index(0U) },
        WriteContainerObjectElement {
            partial_object, 0U, 1U, true, false, false,
            std::nullopt, DynamicPartIndex { 2U, 3, 0, 0U, 2U, true, true } },
        Halt { },
    };
    (void)partial.add_process(std::move(partial_process));
    require(
        retained_partial.elements.data() == retained_partial_elements
            && retained_partial.elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("ZZZZ"),
                    PackedLogic4::from_msb_string("0011") },
        "driver registration refreshes retained sparse aliases before execution");
    const auto partial_result = partial.run();
    const bool partial_matches
        = partial_result.status == RunStatus::completed
            // Registering an SV wire driver resolves its initial Z value
            // before this process reads the aliased element.
            && partial.signal_value(owned_read)
                == PackedLogic4::from_msb_string("ZZZZ")
            && partial.signal_value(unowned_read)
                == PackedLogic4::from_msb_string("0011")
            && partial.signal_value(owned_signal)
                == PackedLogic4::from_msb_string("1010")
            && partial.container_object_value(partial_object).elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010"),
                    PackedLogic4::from_msb_string("1110") };
    if (!partial_matches) {
        std::cerr << "partial element map mismatch: status="
                  << static_cast<int>(partial_result.status)
                  << " time=" << partial_result.time
                  << " owned_read="
                  << partial.signal_value(owned_read).to_msb_string()
                  << " unowned_read="
                  << partial.signal_value(unowned_read).to_msb_string()
                  << " owned_signal="
                  << partial.signal_value(owned_signal).to_msb_string();
        for (const auto& element :
            partial.container_object_value(partial_object).elements) {
            std::cerr << " element=" << element.to_msb_string();
        }
        std::cerr << '\n';
    }
    require(partial_matches,
        "partial element maps preserve reads, owned writes, and unowned dynamic writes");

    partial.deposit_container_object_element(
        partial_object, 1U, PackedLogic4::from_msb_string("0101"));
    const auto before_rejected_whole_write
        = partial.container_object_value(partial_object).elements;
    bool rejected_partial_whole_write { };
    try {
        partial.deposit_container_object(
            partial_object,
            ContainerValue {
                type,
                { PackedLogic4::from_msb_string("0000"),
                    PackedLogic4::from_msb_string("0000") },
                { } });
    } catch (const std::invalid_argument&) {
        rejected_partial_whole_write = true;
    }
    require(
        rejected_partial_whole_write
            && partial.signal_value(owned_signal)
                == PackedLogic4::from_msb_string("1010")
            && partial.container_object_value(partial_object).elements
                == before_rejected_whole_write,
        "a partial map rejects whole writes before mutating owned or stored elements");
}

void test_aggregate_proxy_mixed_target_order_matches_packed_reference()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    struct RawObservation {
        ProcessId process { };
        SignalId signal { };
        std::string own_driver;
        std::string other_driver;
        std::string stored;
        std::string current;
        SimulationTick time { };
        std::uint64_t delta { };
    };
    struct ValueObservation {
        std::string value;
        SimulationTick time { };
        std::uint64_t delta { };
    };
    struct ScenarioResult {
        RunStatus status { RunStatus::completed };
        SignalId logical_signal { };
        SignalId first_leaf { };
        SignalId second_leaf { };
        std::vector<std::string> drivers;
        std::string stored;
        std::string current;
        std::vector<RawObservation> raw;
        std::vector<ValueObservation> stored_hooks;
        std::vector<ValueObservation> current_hooks;
    };

    const auto run_case = [&](const bool use_aliases,
                              const bool whole_first,
                              const bool multiple_owners) {
        Interpreter interpreter;
        SignalId logical_signal { };
        SignalId first_leaf { };
        SignalId second_leaf { };
        if (use_aliases) {
            first_leaf = interpreter.add_signal({
                "top.overlap_array[1]", PackedLogic4 { 4U, Logic4::z },
                ResolutionKind::sv_wire });
            second_leaf = interpreter.add_signal({
                "top.overlap_array[0]", PackedLogic4 { 4U, Logic4::z },
                ResolutionKind::sv_wire });
            logical_signal = interpreter.add_signal({
                "top.overlap_array", PackedLogic4 { 8U, Logic4::z },
                ResolutionKind::sv_wire });
            const auto object = interpreter.add_container_object({
                "top.overlap_array",
                ContainerValue {
                    fixed_packed_array_type(),
                    { PackedLogic4 { 4U, Logic4::z },
                        PackedLogic4 { 4U, Logic4::z } },
                    { } },
                std::nullopt });
            add_proxy_aliases(
                interpreter, first_leaf, second_leaf, logical_signal, object);
        } else {
            logical_signal = interpreter.add_signal({
                "top.packed_overlap_reference",
                PackedLogic4 { 8U, Logic4::z },
                ResolutionKind::sv_wire });
        }

        const auto add_writer = [&](const ProcessId id,
                                    const std::string_view whole_value,
                                    const std::string_view leaf_value) {
            Process writer;
            writer.id = id;
            writer.name = "mixed_target_overlap_writer_"
                + std::to_string(id);
            writer.register_count = 2U;
            if (use_aliases && whole_first) {
                writer.operations = {
                    LoadConstant {
                        0U, PackedLogic4::from_msb_string(whole_value) },
                    WriteUpdate { logical_signal, 0U },
                    LoadConstant {
                        1U, PackedLogic4::from_msb_string(leaf_value) },
                    WriteUpdate { second_leaf, 1U }, Halt { }
                };
            } else if (use_aliases) {
                writer.operations = {
                    LoadConstant {
                        1U, PackedLogic4::from_msb_string(leaf_value) },
                    WriteUpdate { second_leaf, 1U },
                    LoadConstant {
                        0U, PackedLogic4::from_msb_string(whole_value) },
                    WriteUpdate { logical_signal, 0U }, Halt { }
                };
            } else if (whole_first) {
                writer.operations = {
                    LoadConstant {
                        0U, PackedLogic4::from_msb_string(whole_value) },
                    WriteUpdate { logical_signal, 0U },
                    LoadConstant {
                        1U, PackedLogic4::from_msb_string(leaf_value) },
                    WriteUpdateSlice { logical_signal, 1U, 0U }, Halt { }
                };
            } else {
                writer.operations = {
                    LoadConstant {
                        1U, PackedLogic4::from_msb_string(leaf_value) },
                    WriteUpdateSlice { logical_signal, 1U, 0U },
                    LoadConstant {
                        0U, PackedLogic4::from_msb_string(whole_value) },
                    WriteUpdate { logical_signal, 0U }, Halt { }
                };
            }
            return interpreter.add_process(std::move(writer));
        };

        std::vector<ProcessId> owners;
        if (multiple_owners) {
            owners.push_back(add_writer(0U, "10100000", "0011"));
            owners.push_back(add_writer(1U, "10100000", "0101"));
        } else {
            owners.push_back(add_writer(0U, "10100101", "0011"));
        }

        ScenarioResult result;
        result.logical_signal = logical_signal;
        result.first_leaf = first_leaf;
        result.second_leaf = second_leaf;
        const auto raw_target = [&](const SignalId signal) {
            return use_aliases
                ? signal == logical_signal || signal == first_leaf
                    || signal == second_leaf
                : signal == logical_signal;
        };
        interpreter.set_driver_change_hook(
            [&](const ProcessId process,
                const SignalId signal,
                const SimulationTick time) {
                if (!raw_target(signal)) {
                    return;
                }
                std::string other_driver;
                if (owners.size() > 1U) {
                    const auto other = process == owners.front()
                        ? owners.back()
                        : owners.front();
                    other_driver = interpreter.driver_value(
                        other, logical_signal).to_msb_string();
                }
                result.raw.push_back({
                    process,
                    signal,
                    interpreter.driver_value(
                        process, logical_signal).to_msb_string(),
                    std::move(other_driver),
                    interpreter.stored_signal_value(
                        logical_signal).to_msb_string(),
                    interpreter.signal_value(
                        logical_signal).to_msb_string(),
                    time,
                    interpreter.scheduler().delta()
                });
            });
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId signal, const SimulationTick time) {
                if (signal == logical_signal) {
                    result.stored_hooks.push_back({
                        interpreter.stored_signal_value(
                            logical_signal).to_msb_string(),
                        time,
                        interpreter.scheduler().delta()
                    });
                }
            });
        interpreter.set_signal_change_hook(
            [&](const SignalId signal,
                const PackedLogic4& value,
                const SimulationTick time) {
                if (signal == logical_signal) {
                    result.current_hooks.push_back({
                        value.to_msb_string(),
                        time,
                        interpreter.scheduler().delta()
                    });
                }
            });

        result.status = interpreter.run().status;
        for (const auto owner : owners) {
            result.drivers.push_back(
                interpreter.driver_value(owner, logical_signal)
                    .to_msb_string());
        }
        result.stored
            = interpreter.stored_signal_value(logical_signal).to_msb_string();
        result.current
            = interpreter.signal_value(logical_signal).to_msb_string();
        return result;
    };

    for (const bool whole_first : { true, false }) {
        const auto expected = whole_first
            ? std::string { "10100011" }
            : std::string { "10100101" };
        const auto packed = run_case(false, whole_first, false);
        const auto aliases = run_case(true, whole_first, false);
        require(
            packed.status == RunStatus::completed
                && aliases.status == RunStatus::completed
                && packed.drivers == std::vector<std::string> { expected }
                && aliases.drivers == packed.drivers
                && aliases.stored == packed.stored
                && aliases.current == packed.current
                && aliases.current == expected,
            "mixed aggregate and leaf writes match one packed whole/slice driver");
        require(
            packed.raw.size() == 1U && aliases.raw.size() == 3U
                && packed.raw.front().own_driver == expected
                && aliases.raw[0U].signal == aliases.first_leaf
                && aliases.raw[1U].signal == aliases.second_leaf
                && aliases.raw[2U].signal == aliases.logical_signal
                && std::all_of(
                    aliases.raw.begin(), aliases.raw.end(),
                    [&](const RawObservation& observation) {
                        return observation.process == 0U
                            && observation.own_driver == expected
                            && observation.stored == "ZZZZZZZZ"
                            && observation.current == "ZZZZZZZZ"
                            && observation.time
                                == packed.raw.front().time
                            && observation.delta
                                == packed.raw.front().delta;
                    })
                && aliases.raw.front().own_driver
                    == packed.raw.front().own_driver
                && aliases.raw.front().time == packed.raw.front().time
                && aliases.raw.front().delta == packed.raw.front().delta
                && aliases.raw[2U].own_driver
                    == packed.raw.front().own_driver
                && aliases.raw[2U].time == packed.raw.front().time
                && aliases.raw[2U].delta == packed.raw.front().delta,
            "whole/leaf append order preserves the packed driver and emits each leaf plus one proxy raw hook");
        require(
            packed.stored_hooks.size() == 1U
                && aliases.stored_hooks.size() == 1U
                && packed.current_hooks.size() == 1U
                && aliases.current_hooks.size() == 1U
                && aliases.stored_hooks.front().value
                    == packed.stored_hooks.front().value
                && aliases.current_hooks.front().value
                    == packed.current_hooks.front().value
                && aliases.stored_hooks.front().time
                    == packed.stored_hooks.front().time
                && aliases.current_hooks.front().time
                    == packed.current_hooks.front().time
                && aliases.stored_hooks.front().delta
                    == packed.stored_hooks.front().delta
                && aliases.current_hooks.front().delta
                    == packed.current_hooks.front().delta,
            "mixed alias targets publish one proxy stored/current phase like a packed target");
    }

    const auto packed_multi = run_case(false, true, true);
    const auto aliases_multi = run_case(true, true, true);
    const auto resolved_multi = std::string { "10100XX1" };
    require(
        packed_multi.status == RunStatus::completed
            && aliases_multi.status == RunStatus::completed
            && packed_multi.drivers
                == std::vector<std::string> {
                    "10100011", "10100101" }
            && aliases_multi.drivers == packed_multi.drivers
            && packed_multi.stored == resolved_multi
            && aliases_multi.stored == packed_multi.stored
            && aliases_multi.current == packed_multi.current,
        "mixed whole/leaf writes retain distinct owner values across physical leaves");
    require(
        packed_multi.raw.size() == 2U
            && aliases_multi.raw.size() == 6U
            && aliases_multi.raw[0U].process == 0U
            && aliases_multi.raw[1U].process == 0U
            && aliases_multi.raw[2U].process == 0U
            && aliases_multi.raw[3U].process == 1U
            && aliases_multi.raw[4U].process == 1U
            && aliases_multi.raw[5U].process == 1U
            && aliases_multi.raw[0U].signal == aliases_multi.first_leaf
            && aliases_multi.raw[1U].signal == aliases_multi.second_leaf
            && aliases_multi.raw[2U].signal
                == aliases_multi.logical_signal
            && aliases_multi.raw[3U].signal == aliases_multi.first_leaf
            && aliases_multi.raw[4U].signal == aliases_multi.second_leaf
            && aliases_multi.raw[5U].signal
                == aliases_multi.logical_signal
            && packed_multi.raw[0U].process == 0U
            && packed_multi.raw[0U].own_driver == "10100011"
            && packed_multi.raw[0U].other_driver == "ZZZZZZZZ"
            && packed_multi.raw[1U].process == 1U
            && packed_multi.raw[1U].own_driver == "10100101"
            && packed_multi.raw[1U].other_driver == "10100011"
            && std::all_of(
                aliases_multi.raw.begin(), aliases_multi.raw.end(),
                [&](const RawObservation& observation) {
                    if (observation.process >= 2U) {
                        return false;
                    }
                    const auto& expected = packed_multi.raw[
                        static_cast<std::size_t>(observation.process)];
                    return observation.own_driver == expected.own_driver
                        && observation.other_driver == expected.other_driver
                        && observation.stored == "ZZZZZZZZ"
                        && observation.current == "ZZZZZZZZ"
                        && observation.time == expected.time
                        && observation.delta == expected.delta;
                })
            && aliases_multi.current_hooks.size() == 1U
            && packed_multi.current_hooks.size() == 1U
            && aliases_multi.stored_hooks.size() == 1U
            && packed_multi.stored_hooks.size() == 1U
            && aliases_multi.stored_hooks.front().value
                == packed_multi.stored_hooks.front().value
            && aliases_multi.stored_hooks.front().value == resolved_multi
            && aliases_multi.stored_hooks.front().time
                == packed_multi.stored_hooks.front().time
            && aliases_multi.stored_hooks.front().delta
                == packed_multi.stored_hooks.front().delta
            && aliases_multi.current_hooks.front().value
                == packed_multi.current_hooks.front().value
            && aliases_multi.current_hooks.front().value == resolved_multi
            && aliases_multi.current_hooks.front().time
                == packed_multi.current_hooks.front().time
            && aliases_multi.current_hooks.front().delta
                == packed_multi.current_hooks.front().delta,
        "multiple owners install append-folded raw values before one resolved family publication");
}

void test_aggregate_proxy_queued_updates_preserve_per_driver_force()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    struct Result {
        RunStatus status { RunStatus::completed };
        std::string raw_driver;
        std::string stored;
        std::string current;
    };

    const auto run_case = [&](const bool use_aliases) {
        Interpreter interpreter;
        SignalId target { };
        SignalId low_leaf { };
        if (use_aliases) {
            const auto high_leaf = interpreter.add_signal({
                "top.forced_array[1]", PackedLogic4 { 4U, Logic4::z },
                ResolutionKind::sv_wire });
            low_leaf = interpreter.add_signal({
                "top.forced_array[0]", PackedLogic4 { 4U, Logic4::z },
                ResolutionKind::sv_wire });
            target = interpreter.add_signal({
                "top.forced_array", PackedLogic4 { 8U, Logic4::z },
                ResolutionKind::sv_wire });
            const auto object = interpreter.add_container_object({
                "top.forced_array",
                ContainerValue {
                    fixed_packed_array_type(),
                    { PackedLogic4 { 4U, Logic4::z },
                        PackedLogic4 { 4U, Logic4::z } },
                    { } },
                std::nullopt });
            add_proxy_aliases(
                interpreter, high_leaf, low_leaf, target, object);
        } else {
            target = interpreter.add_signal({
                "top.forced_packed_reference",
                PackedLogic4 { 8U, Logic4::z },
                ResolutionKind::sv_wire });
        }

        Process writer;
        writer.id = 0U;
        writer.name = "queued_update_under_per_driver_force";
        writer.register_count = 3U;
        writer.operations = {
            LoadConstant {
                0U, PackedLogic4::from_msb_string("1010") },
            ForceSignalSlice {
                target, 0U, 4U, std::nullopt, true },
            LoadConstant {
                1U, PackedLogic4::from_msb_string("00100000") },
            WriteUpdate { target, 1U },
            LoadConstant {
                2U, PackedLogic4::from_msb_string("0110") },
        };
        if (use_aliases) {
            writer.operations.push_back(WriteUpdate { low_leaf, 2U });
        } else {
            writer.operations.push_back(
                WriteUpdateSlice { target, 2U, 0U });
        }
        writer.operations.push_back(Halt { });
        const auto writer_id = interpreter.add_process(std::move(writer));

        const auto run = interpreter.run();
        return Result {
            run.status,
            interpreter.driver_value(writer_id, target).to_msb_string(),
            interpreter.stored_signal_value(target).to_msb_string(),
            interpreter.signal_value(target).to_msb_string()
        };
    };

    const auto packed = run_case(false);
    const auto aliases = run_case(true);
    require(
        packed.status == RunStatus::completed
            && aliases.status == RunStatus::completed
            && packed.raw_driver == "00100110"
            && aliases.raw_driver == packed.raw_driver
            && packed.stored == "10100110"
            && aliases.stored == packed.stored
            && packed.current == "10100110"
            && aliases.current == packed.current,
        "queued whole and leaf writes preserve per-driver force masks and match the packed reference");
}

void test_aggregate_proxy_selected_slices_preserve_unselected_sibling()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    auto type = fixed_packed_array_type();
    type.index_left = 2;
    type.index_right = 0;
    type.dimensions = { { 2, 0 } };

    std::array<SignalId, 3U> leaves { };
    for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
        leaves[ordinal] = interpreter.add_signal({
            "top.selected_array[" + std::to_string(2U - ordinal) + "]",
            PackedLogic4 { 4U, Logic4::z }, ResolutionKind::sv_wire });
    }
    const auto proxy = interpreter.add_signal({
        "top.selected_array", PackedLogic4 { 12U, Logic4::z },
        ResolutionKind::sv_wire });
    const ContainerValue initial {
        type,
        { PackedLogic4::from_msb_string("0000"),
            PackedLogic4::from_msb_string("0000"),
            PackedLogic4::from_msb_string("0000") },
        { }
    };
    const auto object = interpreter.add_container_object({
        "top.selected_array", initial, std::nullopt });
    for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
        interpreter.add_container_element_signal_alias({
            object, static_cast<std::uint32_t>(ordinal), leaves[ordinal],
            true, true });
    }
    interpreter.add_container_aggregate_signal_alias({
        object, proxy, true, true });

    Process writer;
    writer.id = 0U;
    writer.name = "selected_container_slice_writer";
    writer.register_count = 2U;
    writer.driver_regions = {
        { leaves[0U], 0U, 4U, false },
        { leaves[1U], 0U, 4U, false },
    };
    writer.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1100") },
        WriteUpdateSlice { proxy, 0U, 8U },
        LoadConstant { 1U, PackedLogic4::from_msb_string("0110") },
        WriteUpdate { leaves[1U], 1U },
        Halt { },
    };
    (void)interpreter.add_process(std::move(writer));

    const auto seeded = ContainerValue {
        type,
        { PackedLogic4::from_msb_string("0011"),
            PackedLogic4::from_msb_string("0101"),
            PackedLogic4::from_msb_string("1010") },
        { }
    };
    interpreter.deposit_container_object(object, seeded);

    std::array<std::size_t, 3U> current_notifications { };
    std::array<std::size_t, 3U> stored_notifications { };
    std::vector<std::string> proxy_current_notifications;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick) {
            for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
                if (signal == leaves[ordinal]) {
                    ++current_notifications[ordinal];
                }
            }
            if (signal == proxy) {
                proxy_current_notifications.push_back(
                    value.to_msb_string());
            }
        });
    interpreter.set_stored_signal_change_hook(
        [&](const SignalId signal, const SimulationTick) {
            for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
                if (signal == leaves[ordinal]) {
                    ++stored_notifications[ordinal];
                }
            }
        });

    const auto result = interpreter.run();
    const auto& final_object = interpreter.container_object_value(object);
    require(
        result.status == RunStatus::completed
            && final_object.elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1100"),
                    PackedLogic4::from_msb_string("0110"),
                    PackedLogic4::from_msb_string("1010") }
            && interpreter.signal_value(proxy)
                == PackedLogic4::from_msb_string("110001101010")
            && interpreter.stored_signal_value(proxy)
                == PackedLogic4::from_msb_string("110001101010")
            && interpreter.signal_value(leaves[2U])
                == PackedLogic4::from_msb_string("1010")
            && interpreter.stored_signal_value(leaves[2U])
                == PackedLogic4::from_msb_string("1010")
            && interpreter.driver_value(0U, proxy)
                == PackedLogic4::from_msb_string("11000110ZZZZ")
            && current_notifications
                == std::array<std::size_t, 3U> { 1U, 1U, 0U }
            && stored_notifications
                == std::array<std::size_t, 3U> { 1U, 1U, 0U }
            && proxy_current_notifications
                == std::vector<std::string> { "110001101010" },
        "a proxy slice plus one leaf write publishes their selected union and preserves the untouched sibling");
}

} // namespace

void test_aggregate_proxy_sampled_data_matches_packed_reference()
{
    run_aggregate_proxy_sampled_data_matches_packed_reference();
}

void test_aggregate_proxy_sampled_clock_matches_packed_reference()
{
    run_aggregate_proxy_sampled_clock_matches_packed_reference();
}

void test_aggregate_proxy_native_fallback()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    {
        Interpreter capability_interpreter;
        const auto ordinary = capability_interpreter.add_signal({
            "top.read_capability_plain",
            PackedLogic4::from_msb_string("0011"),
            ResolutionKind::none });
        const auto first = capability_interpreter.add_signal({
            "top.read_capability_array[1]",
            PackedLogic4 { 4U, Logic4::z }, ResolutionKind::sv_wire });
        const auto second = capability_interpreter.add_signal({
            "top.read_capability_array[0]",
            PackedLogic4 { 4U, Logic4::z }, ResolutionKind::sv_wire });
        const auto proxy = capability_interpreter.add_signal({
            "top.read_capability_array",
            PackedLogic4 { 8U, Logic4::z }, ResolutionKind::sv_wire });
        const auto object = capability_interpreter.add_container_object({
            "top.read_capability_array",
            ContainerValue {
                fixed_packed_array_type(),
                { PackedLogic4 { 4U, Logic4::z },
                    PackedLogic4 { 4U, Logic4::z } },
                { } },
            std::nullopt });
        add_proxy_aliases(
            capability_interpreter, first, second, proxy, object);
        capability_interpreter.deposit_container_object(
            object,
            ContainerValue {
                fixed_packed_array_type(),
                { PackedLogic4::from_msb_string("1010"),
                    PackedLogic4::from_msb_string("0101") },
                { } });

        Process process;
        process.name = "direct_read_alias_capability_probe";
        process.operations = { Halt { } };
        const auto process_id
            = capability_interpreter.add_process(std::move(process));
        DirectReadCapabilityProbe probe;
        capability_interpreter.set_process_executor(
            process_id,
            std::make_unique<DirectReadCapabilityExecutor>(
                ordinary, first, proxy, probe));

        const auto result = capability_interpreter.run();
        require(
            result.status == RunStatus::completed
                && probe.ordinary_supported
                && probe.element_alias_supported
                && !probe.aggregate_proxy_supported
                && probe.ordinary_direct_planes_available
                && probe.ordinary_plane_matches_logical
                && probe.element_plane_matches_logical
                && probe.ordinary_value == "0011"
                && probe.element_value == "1010"
                && probe.aggregate_value == "10100101",
            "complete packed element aliases expose current direct planes while their aggregate proxy remains checked");
    }

    {
        Interpreter mixed_kinds;
        const auto before = mixed_kinds.add_signal({
            "top.direct_plane_layout.before",
            PackedLogic4::from_msb_string("0"),
            ResolutionKind::none });
        const auto logic9 = mixed_kinds.add_signal({
            "top.direct_plane_layout.logic9",
            PackedLogic4::from_logic9_msb_string("U"),
            ResolutionKind::none, ValueKind::logic9 });
        const auto after = mixed_kinds.add_signal({
            "top.direct_plane_layout.after",
            PackedLogic4::from_msb_string("1"),
            ResolutionKind::none });

        Process probe_process;
        probe_process.name = "mixed_direct_plane_layout_probe";
        probe_process.operations = { Halt { } };
        const auto probe_id = mixed_kinds.add_process(
            std::move(probe_process));
        struct MixedPlaneProbe {
            bool core_planes_available { };
            bool before_supported { };
            bool logic9_checked { };
            bool after_supported { };
            bool narrow_logic9_fallback { };
        } probe;
        class MixedPlaneExecutor final : public ProcessExecutor {
        public:
            MixedPlaneExecutor(
                const SignalId before_value,
                const SignalId logic9_value,
                const SignalId after_value,
                MixedPlaneProbe& probe_value)
                : before_(before_value)
                , logic9_(logic9_value)
                , after_(after_value)
                , probe_(probe_value)
            {
            }

            [[nodiscard]] ProcessResumeResult resume(
                ProcessExecutionContext& context,
                const InstructionIndex start) override
            {
                if (start != 0U) {
                    throw std::logic_error {
                        "unexpected mixed plane layout resume PC"
                    };
                }
                const auto direct_aval = context.direct_signal_aval();
                const auto direct_bval = context.direct_signal_bval();
                const auto wide_aval = context.direct_wide_signal_aval();
                const auto wide_bval = context.direct_wide_signal_bval();
                const auto wide_offsets
                    = context.direct_wide_signal_offsets();
                probe_.core_planes_available
                    = direct_aval.size() == 3U
                    && direct_bval.size() == 3U
                    && wide_aval.size() == wide_bval.size()
                    && wide_offsets.size() == 3U;
                probe_.before_supported
                    = context.supports_direct_signal_read(before_)
                    && before_ < direct_aval.size()
                    && before_ < direct_bval.size()
                    && direct_aval[before_] == 0U
                    && direct_bval[before_] == 0U;
                probe_.logic9_checked
                    = !context.supports_direct_signal_read(logic9_)
                    && context.read_signal(logic9_)
                        == PackedLogic4::from_logic9_msb_string("U");
                probe_.after_supported
                    = context.supports_direct_signal_read(after_)
                    && after_ < direct_aval.size()
                    && after_ < direct_bval.size()
                    && direct_aval[after_] == 1U
                    && direct_bval[after_] == 0U;
                probe_.narrow_logic9_fallback
                    = context.direct_signal_logic9_plane0().empty()
                    && context.direct_signal_logic9_plane1().empty()
                    && context.direct_signal_logic9_plane2().empty()
                    && context.direct_signal_logic9_plane3().empty()
                    && context.direct_wide_signal_logic9_plane2().empty()
                    && context.direct_wide_signal_logic9_plane3().empty();
                return { 0U, 1U };
            }

        private:
            SignalId before_ { };
            SignalId logic9_ { };
            SignalId after_ { };
            MixedPlaneProbe& probe_;
        };
        mixed_kinds.set_process_executor(
            probe_id,
            std::make_unique<MixedPlaneExecutor>(
                before, logic9, after, probe));
        const auto mixed_result = mixed_kinds.run();
        require(
            mixed_result.status == RunStatus::completed
                && probe.core_planes_available
                && probe.before_supported
                && probe.logic9_checked
                && probe.after_supported
                && probe.narrow_logic9_fallback,
            "sparse Logic9 planes do not disable valid Logic4 direct reads"
            " or expose incomplete Logic9 slots");
    }

    test_multidimensional_element_direct_reads();
    test_sparse_element_alias_dispatch();
    test_aggregate_proxy_mixed_target_order_matches_packed_reference();
    test_aggregate_proxy_queued_updates_preserve_per_driver_force();
    test_aggregate_proxy_selected_slices_preserve_unselected_sibling();

    Interpreter interpreter;
    const auto first = interpreter.add_signal({
        "top.array[1]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto second = interpreter.add_signal({
        "top.array[0]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto proxy = interpreter.add_signal({
        "top.array", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto object = interpreter.add_container_object({
        "top.array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::z },
                PackedLogic4 { 4U, Logic4::z } },
            { } },
        std::nullopt });
    add_proxy_aliases(interpreter, first, second, proxy, object);

    Process first_member;
    first_member.id = 0U;
    first_member.name = "first_proxy_cohort_member";
    first_member.operations = { Halt { } };
    const auto first_id = interpreter.add_process(std::move(first_member));

    Process proxy_reader;
    proxy_reader.id = 1U;
    proxy_reader.name = "aggregate_proxy_reader";
    proxy_reader.register_count = 1U;
    proxy_reader.operations = {
        ReadSignal { 0U, proxy }, WaitFor { 1U }, Halt { }
    };
    const auto reader_id = interpreter.add_process(std::move(proxy_reader));

    NativeProxyProbe probe;
    interpreter.set_process_executor(
        first_id,
        std::make_unique<ProxyNativeFallbackExecutor>(
            first_id, proxy, probe, false, false));
    interpreter.set_process_executor(
        reader_id,
        std::make_unique<ProxyNativeFallbackExecutor>(
            reader_id, proxy, probe, true, true));

    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && probe.first_member_planes_available
            && probe.proxy_reader_planes_available
            && probe.proxy_direct_read_rejected,
        "proxy alias slots are rejected without disabling safe shared planes");
    require(
        probe.proxy_value_before_write == "ZZZZZZZZ",
        "proxy reads use the checked logical container projection");
    require(
        probe.slot_declined && probe.rejected_slot_retained,
        "an alias-backed update declines native slot consumption atomically");
    require(
        interpreter.signal_value(proxy)
                == PackedLogic4::from_msb_string("01010101")
            && interpreter.stored_signal_value(proxy)
                == PackedLogic4::from_msb_string("01010101")
            && interpreter.driver_value(reader_id, first)
                == PackedLogic4::from_msb_string("0101")
            && interpreter.driver_value(reader_id, second)
                == PackedLogic4::from_msb_string("0101"),
        "checked word fallback updates each authoritative leaf driver");

    Interpreter wide;
    const std::array<std::uint64_t, 2U> first_aval {
        UINT64_C(0x0123456789abcdef), UINT64_C(1)
    };
    const std::array<std::uint64_t, 2U> first_bval { };
    const std::array<std::uint64_t, 2U> second_aval {
        UINT64_C(0xfedcba9876543210), UINT64_C(0)
    };
    const std::array<std::uint64_t, 2U> second_bval {
        UINT64_C(0x00000000000000c0), UINT64_C(0)
    };
    const auto first_value = PackedLogic4::from_word_planes(
        65U, first_aval, first_bval);
    const auto second_value = PackedLogic4::from_word_planes(
        65U, second_aval, second_bval);
    auto expected = PackedLogic4 { 130U, Logic4::zero };
    expected.insert_bits(first_value, 65U);
    expected.insert_bits(second_value, 0U);
    const auto wide_first = wide.add_signal({
        "top.wide_array[1]", first_value, ResolutionKind::sv_wire });
    const auto wide_second = wide.add_signal({
        "top.wide_array[0]", second_value, ResolutionKind::sv_wire });
    const auto wide_proxy = wide.add_signal({
        "top.wide_array", PackedLogic4 { 130U, Logic4::zero },
        ResolutionKind::sv_wire });
    auto wide_type = fixed_packed_array_type();
    wide_type.element_width = 65U;
    const auto wide_object = wide.add_container_object({
        "top.wide_array",
        ContainerValue {
            wide_type, { first_value, second_value }, { } },
        std::nullopt });
    add_proxy_aliases(wide, wide_first, wide_second, wide_proxy, wide_object);
    Process wide_reader;
    wide_reader.id = 0U;
    wide_reader.name = "wide_aggregate_proxy_reader";
    wide_reader.register_count = 1U;
    wide_reader.operations = {
        ReadSignal { 0U, wide_proxy }, WaitFor { 1U }, Halt { }
    };
    const auto wide_reader_id = wide.add_process(std::move(wide_reader));
    bool wide_read_matches { };
    bool direct_wide_planes_available { };
    wide.set_process_executor(
        wide_reader_id,
        std::make_unique<WideProxyReadExecutor>(
            wide_proxy, expected, wide_read_matches,
            direct_wide_planes_available));
    require(
        wide.run().status == RunStatus::completed
            && direct_wide_planes_available && wide_read_matches,
        "wide proxy reads use checked logical composition beside available direct planes");

    Interpreter stale_proxy;
    const auto stale_first = stale_proxy.add_signal({
        "top.stale_array[1]", PackedLogic4 { 4U, Logic4::zero },
        ResolutionKind::sv_wire });
    const auto stale_second = stale_proxy.add_signal({
        "top.stale_array[0]", PackedLogic4 { 4U, Logic4::zero },
        ResolutionKind::sv_wire });
    const auto stale_aggregate = stale_proxy.add_signal({
        "top.stale_array",
        PackedLogic4::from_msb_string("01010101"),
        ResolutionKind::sv_wire });
    const auto stale_object = stale_proxy.add_container_object({
        "top.stale_array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::zero },
                PackedLogic4 { 4U, Logic4::zero } },
            { } },
        std::nullopt });
    add_proxy_aliases(
        stale_proxy, stale_first, stale_second,
        stale_aggregate, stale_object);
    Process stale_writer;
    stale_writer.id = 0U;
    stale_writer.name = "aggregate_proxy_stale_shadow_writer";
    stale_writer.operations = { Halt { } };
    stale_writer.driver_regions.push_back(
        { stale_aggregate, 0U, 0U, true });
    const auto stale_writer_id
        = stale_proxy.add_process(std::move(stale_writer));
    // Registration initializes the owned net drivers to Z. Establish the
    // logical zero state while preserving the stale raw proxy plane.
    stale_proxy.deposit_signal(
        stale_first, PackedLogic4 { 4U, Logic4::zero });
    stale_proxy.deposit_signal(
        stale_second, PackedLogic4 { 4U, Logic4::zero });
    StaleProxyWriteProbe stale_probe;
    stale_proxy.set_process_executor(
        stale_writer_id,
        std::make_unique<StaleProxyWriteExecutor>(
            stale_writer_id, stale_aggregate, stale_probe));
    const auto stale_result = stale_proxy.run();
    const bool stale_ok
        = stale_result.status == RunStatus::completed
            && stale_probe.stable_writer_shadow_disabled
            && stale_probe.raw_proxy_matches_write
            && stale_probe.logical_proxy_differs_from_raw
            && stale_probe.slot_declined
            && stale_probe.rejected_slot_retained
            && stale_proxy.signal_value(stale_aggregate)
                == PackedLogic4::from_msb_string("01010101")
            && stale_proxy.driver_value(stale_writer_id, stale_first)
                == PackedLogic4::from_msb_string("0101")
            && stale_proxy.driver_value(stale_writer_id, stale_second)
                == PackedLogic4::from_msb_string("0101");
    if (!stale_ok) {
        std::cerr << "stale status="
                  << static_cast<std::uint32_t>(stale_result.status)
                  << " shadow_disabled="
                  << stale_probe.stable_writer_shadow_disabled
                  << " raw_matches=" << stale_probe.raw_proxy_matches_write
                  << " logical_differs="
                  << stale_probe.logical_proxy_differs_from_raw
                  << " slot_declined=" << stale_probe.slot_declined
                  << " slot_retained="
                  << stale_probe.rejected_slot_retained
                  << " current="
                  << stale_proxy.signal_value(
                         stale_aggregate).to_msb_string()
                  << " first_driver="
                  << stale_proxy.driver_value(
                         stale_writer_id, stale_first).to_msb_string()
                  << " second_driver="
                  << stale_proxy.driver_value(
                         stale_writer_id, stale_second).to_msb_string()
                  << '\n';
    }
    require(
        stale_ok,
        "an aggregate write equal to its stale raw proxy plane still reaches "
        "the authoritative leaf drivers");
}

void test_generic_queue_ordering_control()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    struct Observation {
        std::string phase;
        std::string driver;
        std::string stored;
        std::string current;
        std::string callback;
        SimulationTick time { };
        std::uint64_t delta { };
    };

    const auto run_case = [](const bool whole_first,
                             const std::string_view expected) {
        Interpreter interpreter;
        const auto output = interpreter.add_signal({
            "top.generic_queue_order",
            PackedLogic4::from_msb_string("0000"),
            ResolutionKind::sv_wire });

        Process writer;
        writer.id = 0U;
        writer.name = whole_first
            ? "generic_queue_whole_then_slice"
            : "generic_queue_slice_then_whole";
        writer.register_count = 2U;
        if (whole_first) {
            writer.operations = {
                LoadConstant {
                    0U, PackedLogic4::from_msb_string("1010") },
                WriteUpdate { output, 0U },
                LoadConstant {
                    1U, PackedLogic4::from_msb_string("01") },
                WriteUpdateSlice { output, 1U, 0U },
                Halt { }
            };
        } else {
            writer.operations = {
                LoadConstant {
                    1U, PackedLogic4::from_msb_string("01") },
                WriteUpdateSlice { output, 1U, 0U },
                LoadConstant {
                    0U, PackedLogic4::from_msb_string("1010") },
                WriteUpdate { output, 0U },
                Halt { }
            };
        }
        const auto process = interpreter.add_process(std::move(writer));
        require(
            interpreter.driver_value(process, output)
                    == PackedLogic4::from_msb_string("ZZZZ")
                && interpreter.stored_signal_value(output)
                    == PackedLogic4::from_msb_string("ZZZZ")
                && interpreter.signal_value(output)
                    == PackedLogic4::from_msb_string("ZZZZ"),
            "registered wire driver, stored value, and current value start at Z");

        std::vector<Observation> observations;
        const auto record = [&](const std::string_view phase,
                                const ProcessId changed_process,
                                const SignalId changed_signal,
                                const PackedLogic4* callback_value,
                                const SimulationTick time) {
            if (changed_process != process || changed_signal != output) {
                return;
            }
            observations.push_back(Observation {
                std::string { phase },
                interpreter.driver_value(process, output).to_msb_string(),
                interpreter.stored_signal_value(output).to_msb_string(),
                interpreter.signal_value(output).to_msb_string(),
                callback_value != nullptr
                    ? callback_value->to_msb_string()
                    : std::string { },
                time,
                interpreter.scheduler().delta()
            });
        };
        interpreter.set_driver_change_hook(
            [&](const ProcessId changed_process,
                const SignalId changed_signal,
                const SimulationTick time) {
                record("driver", changed_process, changed_signal,
                    nullptr, time);
            });
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed_signal,
                const SimulationTick time) {
                record("stored", process, changed_signal, nullptr, time);
            });
        interpreter.set_signal_change_hook(
            [&](const SignalId changed_signal,
                const PackedLogic4& value,
                const SimulationTick time) {
                record("current", process, changed_signal, &value, time);
            });

        const auto result = interpreter.run();
        const auto actual = std::string { expected };
        require(
            result.status == RunStatus::completed
                && interpreter.driver_value(process, output).to_msb_string()
                    == actual
                && interpreter.stored_signal_value(output).to_msb_string()
                    == actual
                && interpreter.signal_value(output).to_msb_string() == actual,
            "generic queue whole/slice ordering control reaches its final value");
        require(
            observations.size() == 3U
                && observations[0U].phase == "driver"
                && observations[0U].driver == actual
                && observations[0U].stored == "ZZZZ"
                && observations[0U].current == "ZZZZ"
                && observations[1U].phase == "stored"
                && observations[1U].driver == actual
                && observations[1U].stored == actual
                && observations[1U].current == "ZZZZ"
                && observations[2U].phase == "current"
                && observations[2U].driver == actual
                && observations[2U].stored == actual
                && observations[2U].current == actual
                && observations[2U].callback == actual
                && observations[0U].time == observations[1U].time
                && observations[1U].time == observations[2U].time
                && observations[0U].delta == observations[1U].delta
                && observations[1U].delta == observations[2U].delta,
            "generic queue preserves raw-driver, stored, then current hook order");
    };

    run_case(true, "1001");
    run_case(false, "1010");

    Interpreter multiple_owners;
    const auto shared_output = multiple_owners.add_signal({
        "top.generic_queue_multiple_owners",
        PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });

    const auto add_writer = [&](const ProcessId id,
                                const std::string_view initial) {
        Process writer;
        writer.id = id;
        writer.name = "generic_queue_owner_" + std::to_string(id);
        writer.register_count = 2U;
        writer.operations = {
            LoadConstant {
                0U, PackedLogic4::from_msb_string(initial) },
            WriteUpdate { shared_output, 0U },
            LoadConstant {
                1U, PackedLogic4::from_msb_string("1001") },
            WriteUpdateSlice { shared_output, 1U, 4U },
            Halt { }
        };
        return multiple_owners.add_process(std::move(writer));
    };

    const auto first_owner = add_writer(0U, "00110000");
    const auto second_owner = add_writer(1U, "01010000");
    struct MultiOwnerObservation {
        std::string phase;
        ProcessId changed_process { };
        std::string first_driver;
        std::string second_driver;
        std::string stored;
        std::string current;
        std::string callback;
        SimulationTick time { };
        std::uint64_t delta { };
    };
    std::vector<MultiOwnerObservation> multi_owner_observations;
    const auto record_multi_owner = [&](
        const std::string_view phase,
        const ProcessId changed_process,
        const PackedLogic4* callback_value,
        const SimulationTick time) {
        multi_owner_observations.push_back({
            std::string { phase },
            changed_process,
            multiple_owners.driver_value(
                first_owner, shared_output).to_msb_string(),
            multiple_owners.driver_value(
                second_owner, shared_output).to_msb_string(),
            multiple_owners.stored_signal_value(
                shared_output).to_msb_string(),
            multiple_owners.signal_value(
                shared_output).to_msb_string(),
            callback_value != nullptr
                ? callback_value->to_msb_string()
                : std::string { },
            time,
            multiple_owners.scheduler().delta()
        });
    };
    multiple_owners.set_driver_change_hook(
        [&](const ProcessId process,
            const SignalId signal,
            const SimulationTick time) {
            if (signal == shared_output) {
                record_multi_owner("driver", process, nullptr, time);
            }
        });
    multiple_owners.set_stored_signal_change_hook(
        [&](const SignalId signal, const SimulationTick time) {
            if (signal == shared_output) {
                record_multi_owner("stored", 0U, nullptr, time);
            }
        });
    multiple_owners.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == shared_output) {
                record_multi_owner("current", 0U, &value, time);
            }
        });

    const auto multiple_owner_result = multiple_owners.run();
    const auto final_word = std::string { "10010000" };
    require(
        multiple_owner_result.status == RunStatus::completed
            && multiple_owners.driver_value(first_owner, shared_output)
                .to_msb_string() == final_word
            && multiple_owners.driver_value(second_owner, shared_output)
                .to_msb_string() == final_word
            && multiple_owners.stored_signal_value(shared_output)
                .to_msb_string() == final_word
            && multiple_owners.signal_value(shared_output)
                .to_msb_string() == final_word,
        "generic packed whole/slice updates retain both owner values");
    require(
        multi_owner_observations.size() == 4U
            && multi_owner_observations[0U].phase == "driver"
            && multi_owner_observations[0U].changed_process == first_owner
            && multi_owner_observations[0U].first_driver == final_word
            && multi_owner_observations[0U].second_driver == "ZZZZZZZZ"
            && multi_owner_observations[0U].stored == "ZZZZZZZZ"
            && multi_owner_observations[0U].current == "ZZZZZZZZ"
            && multi_owner_observations[1U].phase == "driver"
            && multi_owner_observations[1U].changed_process == second_owner
            && multi_owner_observations[1U].first_driver == final_word
            && multi_owner_observations[1U].second_driver == final_word
            && multi_owner_observations[1U].stored == "ZZZZZZZZ"
            && multi_owner_observations[1U].current == "ZZZZZZZZ"
            && multi_owner_observations[2U].phase == "stored"
            && multi_owner_observations[2U].first_driver == final_word
            && multi_owner_observations[2U].second_driver == final_word
            && multi_owner_observations[2U].stored == final_word
            && multi_owner_observations[2U].current == "ZZZZZZZZ"
            && multi_owner_observations[3U].phase == "current"
            && multi_owner_observations[3U].first_driver == final_word
            && multi_owner_observations[3U].second_driver == final_word
            && multi_owner_observations[3U].stored == final_word
            && multi_owner_observations[3U].current == final_word
            && multi_owner_observations[3U].callback == final_word
            && std::all_of(
                multi_owner_observations.begin(),
                multi_owner_observations.end(),
                [&](const MultiOwnerObservation& observation) {
                    return observation.time
                            == multi_owner_observations.front().time
                        && observation.delta
                            == multi_owner_observations.front().delta;
                }),
        "generic packed target coalesces each owner's whole/slice sequence, "
        "then resolves once after all raw driver hooks");
}

void test_aggregate_proxy_leaf_publications_are_not_coalesced()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    const auto first = interpreter.add_signal({
        "top.array[1]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto second = interpreter.add_signal({
        "top.array[0]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto proxy = interpreter.add_signal({
        "top.array", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto object = interpreter.add_container_object({
        "top.array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::z },
                PackedLogic4 { 4U, Logic4::z } },
            { } },
        std::nullopt });
    add_proxy_aliases(interpreter, first, second, proxy, object);

    std::vector<std::string> value_publications;
    std::vector<std::string> stored_publications;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick) {
            if (signal == proxy) {
                value_publications.push_back(value.to_msb_string());
            }
        });
    interpreter.set_stored_signal_change_hook(
        [&](const SignalId signal, const SimulationTick) {
            if (signal == proxy) {
                stored_publications.push_back(
                    interpreter.stored_signal_value(proxy).to_msb_string());
            }
        });

    Process first_writer;
    first_writer.id = 0U;
    first_writer.name = "array_first_leaf_writer";
    first_writer.register_count = 1U;
    first_writer.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1010") },
        WriteUpdate { first, 0U }, Halt { }
    };
    (void)interpreter.add_process(std::move(first_writer));

    Process second_writer;
    second_writer.id = 1U;
    second_writer.name = "array_second_leaf_writer";
    second_writer.register_count = 1U;
    second_writer.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("0101") },
        WriteUpdate { second, 0U }, Halt { }
    };
    (void)interpreter.add_process(std::move(second_writer));

    const auto result = interpreter.run();
    const auto final = std::string { "10100101" };
    const auto first_is_intermediate = value_publications.size() == 2U
        && (value_publications.front() == "1010ZZZZ"
            || value_publications.front() == "ZZZZ0101");
    require(
        result.status == RunStatus::completed
            && value_publications.size() == 2U
            && stored_publications.size() == 2U
            && first_is_intermediate
            && value_publications.back() == final
            && stored_publications.front() != stored_publications.back()
            && stored_publications.back() == final,
        "separate leaf commits publish both observable aggregate states");

    Interpreter same_owner;
    const auto same_owner_first = same_owner.add_signal({
        "top.same_owner_array[1]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto same_owner_second = same_owner.add_signal({
        "top.same_owner_array[0]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto same_owner_proxy = same_owner.add_signal({
        "top.same_owner_array", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto same_owner_object = same_owner.add_container_object({
        "top.same_owner_array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::z },
                PackedLogic4 { 4U, Logic4::z } },
            { } },
        std::nullopt });
    add_proxy_aliases(
        same_owner,
        same_owner_first,
        same_owner_second,
        same_owner_proxy,
        same_owner_object);

    Process same_owner_writer;
    same_owner_writer.id = 0U;
    same_owner_writer.name = "same_owner_leaf_writes";
    same_owner_writer.register_count = 2U;
    same_owner_writer.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1010") },
        WriteUpdate { same_owner_first, 0U },
        LoadConstant { 1U, PackedLogic4::from_msb_string("0101") },
        WriteUpdate { same_owner_second, 1U },
        Halt { }
    };
    const auto same_owner_process
        = same_owner.add_process(std::move(same_owner_writer));
    require(
        same_owner.driver_value(same_owner_process, same_owner_first)
                == PackedLogic4::from_msb_string("ZZZZ")
            && same_owner.driver_value(same_owner_process, same_owner_second)
                == PackedLogic4::from_msb_string("ZZZZ"),
        "same-process leaf drivers are registered as Z before queued writes");

    std::vector<SignalId> same_owner_driver_changes;
    std::vector<std::string> same_owner_publications;
    same_owner.set_driver_change_hook(
        [&](const ProcessId process,
            const SignalId signal,
            const SimulationTick) {
            if (process == same_owner_process
                && (signal == same_owner_first
                    || signal == same_owner_second)) {
                same_owner_driver_changes.push_back(signal);
            }
        });
    same_owner.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick) {
            if (signal == same_owner_proxy) {
                same_owner_publications.push_back(value.to_msb_string());
            }
        });
    const auto same_owner_result = same_owner.run();
    require(
        same_owner_result.status == RunStatus::completed
            && same_owner_driver_changes
                == std::vector<SignalId> {
                    same_owner_first, same_owner_second }
            && same_owner_publications.size() == 2U
            && same_owner_publications.front() == "1010ZZZZ"
            && same_owner_publications.back() == "10100101"
            && same_owner.signal_value(same_owner_proxy)
                == PackedLogic4::from_msb_string("10100101"),
        "one process keeps separate generic commits for distinct alias leaves");

    Interpreter whole_writer;
    const auto whole_first = whole_writer.add_signal({
        "top.whole_array[1]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto whole_second = whole_writer.add_signal({
        "top.whole_array[0]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto whole_proxy = whole_writer.add_signal({
        "top.whole_array", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto whole_object = whole_writer.add_container_object({
        "top.whole_array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::z },
                PackedLogic4 { 4U, Logic4::z } },
            { } },
        std::nullopt });
    add_proxy_aliases(
        whole_writer, whole_first, whole_second, whole_proxy, whole_object);

    std::vector<SignalId> leaf_publications;
    std::vector<std::string> leaf_payloads;
    std::vector<std::vector<std::string>> leaf_observations;
    whole_writer.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick) {
            if (signal != whole_first && signal != whole_second) {
                return;
            }
            leaf_publications.push_back(signal);
            leaf_payloads.push_back(value.to_msb_string());
            const auto& observed
                = whole_writer.container_object_value(whole_object);
            leaf_observations.push_back({
                observed.elements[0U].to_msb_string(),
                observed.elements[1U].to_msb_string() });
        });
    const auto whole_replacement = ContainerValue {
        fixed_packed_array_type(),
        { PackedLogic4::from_msb_string("1010"),
            PackedLogic4::from_msb_string("0101") },
        { } };
    whole_writer.deposit_container_object(whole_object, whole_replacement);
    const auto complete_observation = std::vector<std::string> {
        "1010", "0101" };
    require(
            leaf_publications
                == std::vector<SignalId> { whole_first, whole_second }
            && leaf_payloads
                == std::vector<std::string> { "1010", "0101" }
            && leaf_observations
                == std::vector<std::vector<std::string>> {
                    complete_observation, complete_observation }
            && whole_writer.signal_value(whole_proxy)
                == PackedLogic4::from_msb_string("10100101"),
        "a whole-array write keeps complete aggregate visibility in every leaf publication");

    Interpreter reentrant;
    const auto reentrant_first = reentrant.add_signal({
        "top.reentrant_array[1]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto reentrant_second = reentrant.add_signal({
        "top.reentrant_array[0]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto reentrant_proxy = reentrant.add_signal({
        "top.reentrant_array", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto reentrant_object = reentrant.add_container_object({
        "top.reentrant_array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::z },
                PackedLogic4 { 4U, Logic4::z } },
            { } },
        std::nullopt });
    add_proxy_aliases(
        reentrant, reentrant_first, reentrant_second,
        reentrant_proxy, reentrant_object);
    const auto nested_replacement = ContainerValue {
        fixed_packed_array_type(),
        { PackedLogic4::from_msb_string("1111"),
            PackedLogic4::from_msb_string("0000") },
        { } };
    bool nested_write_started { };
    std::vector<std::string> reentrant_payloads;
    std::vector<std::vector<std::string>> reentrant_observations;
    reentrant.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick) {
            if (signal != reentrant_first && signal != reentrant_second) {
                return;
            }
            reentrant_payloads.push_back(value.to_msb_string());
            const auto& observed
                = reentrant.container_object_value(reentrant_object);
            reentrant_observations.push_back({
                observed.elements[0U].to_msb_string(),
                observed.elements[1U].to_msb_string() });
            if (signal == reentrant_first && !nested_write_started) {
                nested_write_started = true;
                reentrant.deposit_container_object(
                    reentrant_object, nested_replacement);
            }
        });
    reentrant.deposit_container_object(
        reentrant_object, whole_replacement);
    require(
        reentrant_payloads
            == std::vector<std::string> {
                "1010", "1111", "0000", "0101" }
            && reentrant_observations
                == std::vector<std::vector<std::string>> {
                    { "1010", "0101" },
                    { "1111", "0000" },
                    { "1111", "0000" },
                    { "1111", "0000" } }
            && reentrant.container_object_value(reentrant_object).elements
                == nested_replacement.elements,
        "reentrant whole writes see authoritative state while queued outer "
        "leaf payloads remain stable");

    Interpreter throwing;
    const auto throwing_first = throwing.add_signal({
        "top.throwing_array[1]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto throwing_second = throwing.add_signal({
        "top.throwing_array[0]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto throwing_proxy = throwing.add_signal({
        "top.throwing_array", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto throwing_object = throwing.add_container_object({
        "top.throwing_array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::z },
                PackedLogic4 { 4U, Logic4::z } },
            { } },
        std::nullopt });
    add_proxy_aliases(
        throwing, throwing_first, throwing_second,
        throwing_proxy, throwing_object);
    bool callback_threw { };
    std::vector<SignalId> after_throw_publications;
    throwing.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4&, const SimulationTick) {
            if (signal != throwing_first && signal != throwing_second) {
                return;
            }
            if (signal == throwing_first && !callback_threw) {
                callback_threw = true;
                throw std::runtime_error { "test callback failure" };
            }
            after_throw_publications.push_back(signal);
        });
    bool callback_failure_propagated { };
    try {
        throwing.deposit_container_object(
            throwing_object, whole_replacement);
    } catch (const std::runtime_error&) {
        callback_failure_propagated = true;
    }
    require(
        callback_failure_propagated && callback_threw
            && after_throw_publications
                == std::vector<SignalId> { throwing_second }
            && throwing.container_object_value(throwing_object).elements
                == whole_replacement.elements,
        "observer failures are rethrown after the batch completes and later "
        "leaf observers run");
    throwing.deposit_container_object(throwing_object, nested_replacement);
    require(
        throwing.container_object_value(throwing_object).elements
                == nested_replacement.elements
            && after_throw_publications
                == std::vector<SignalId> {
                    throwing_second, throwing_first, throwing_second },
        "callback failure cleanup leaves whole-array batching usable");

    Interpreter stored_guard;
    const auto guarded_first = stored_guard.add_signal({
        "top.guarded_array[1]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto guarded_second = stored_guard.add_signal({
        "top.guarded_array[0]", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto guarded_proxy = stored_guard.add_signal({
        "top.guarded_array", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire });
    const auto guarded_object = stored_guard.add_container_object({
        "top.guarded_array",
        ContainerValue {
            fixed_packed_array_type(),
            { PackedLogic4 { 4U, Logic4::z },
                PackedLogic4 { 4U, Logic4::z } },
            { } },
        std::nullopt });
    add_proxy_aliases(
        stored_guard, guarded_first, guarded_second,
        guarded_proxy, guarded_object);
    std::vector<SignalId> stored_observer_order;
    std::vector<std::vector<std::string>> stored_observer_snapshots;
    stored_guard.set_stored_signal_change_hook(
        [&](const SignalId signal, const SimulationTick) {
            if (signal != guarded_first && signal != guarded_second
                && signal != guarded_proxy) {
                return;
            }
            stored_observer_order.push_back(signal);
            stored_observer_snapshots.push_back({
                stored_guard.stored_signal_value(guarded_first).to_msb_string(),
                stored_guard.stored_signal_value(guarded_second).to_msb_string(),
                stored_guard.stored_signal_value(guarded_proxy).to_msb_string(),
                stored_guard.signal_value(guarded_first).to_msb_string(),
                stored_guard.signal_value(guarded_second).to_msb_string(),
                stored_guard.signal_value(guarded_proxy).to_msb_string(),
            });
        });
    stored_guard.deposit_container_object(
        guarded_object, whole_replacement);
    const std::vector<std::string> stored_phase {
        "1010", "0101", "10100101", "ZZZZ", "ZZZZ", "ZZZZZZZZ" };
    require(
        stored_observer_order
                == std::vector<SignalId> {
                    guarded_first, guarded_second, guarded_proxy }
            && stored_observer_snapshots
                == std::vector<std::vector<std::string>> {
                    stored_phase, stored_phase, stored_phase }
            && stored_guard.container_object_value(guarded_object).elements
                == whole_replacement.elements,
        "stored observers see every new family value before current publication");
}

void test_aggregate_proxy_leaf_write_differential()
{
    check_proxy_slice_matches_full_leaf_write();
}

} // namespace fsim::tests::runtime
