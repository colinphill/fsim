// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include "fsim/semantic/sva_plan.hpp"

namespace fsim::runtime::simir {

// A reference process's registers for the assertion operations.
struct Interpreter::Impl::ProcessAssertionStorage final : AssertionStorage {
    ProcessAssertionStorage(Impl& owner_value, ProcessState& state_value) noexcept
        : owner { owner_value }, state { state_value }
    {
    }
    [[nodiscard]] ProcessId process() const noexcept override
    {
        return state.id;
    }
    [[nodiscard]] ProcessId design_process() const noexcept override
    {
        return state.cold().design_process;
    }
    [[nodiscard]] ProcessSchedulingDomain domain() const noexcept override
    {
        return state.program().scheduling_domain();
    }
    [[nodiscard]] PackedLogic4 read(const RegisterId id,
        const std::size_t width) override
    {
        return state.executor ? state.executor->read_register(id, width)
                              : owner.get_register(state, id);
    }
    void write(const RegisterId id, const PackedLogic4& value) override
    {
        owner.write_process_register(state, id, value);
    }
    [[noreturn]] void fail(const std::string& message) override
    {
        owner.fail(state, message);
    }
    Impl& owner;
    ProcessState& state;
};

void Interpreter::Impl::execute_sva(
    ProcessState& process,
    const SvaOperation& operation)
{
    ProcessAssertionStorage storage { *this, process };
    run_sva(storage, operation);
}

void Interpreter::Impl::execute_sampled_read(
    ProcessState& process,
    const ReadSignal& operation)
{
    ProcessAssertionStorage storage { *this, process };
    run_sampled_read(storage, operation);
}

void Interpreter::Impl::run_sva(
    AssertionStorage& storage,
    const SvaOperation& operation)
{
    const auto known_one = [](const PackedLogic4& value,
                               const std::size_t bit) {
        const auto aval = value.aval_words();
        const auto bval = value.bval_words();
        const auto word = bit / 64U;
        const auto mask = std::uint64_t { 1 } << (bit % 64U);
        return word < aval.size() && (aval[word] & mask) != 0U
            && (word >= bval.size() || (bval[word] & mask) == 0U);
    };
    const auto write_result = [&](const std::size_t value) {
        storage.write(operation.result,
            PackedLogic4::from_aval_bval(
                32U, static_cast<std::uint32_t>(value), 0));
    };
    auto& active_monitor = sva_monitors[{ storage.process(), operation.site }];
    if (operation.kind == SvaKind::outcome) {
        write_result(active_monitor
                ? static_cast<std::size_t>(active_monitor->next_outcome())
                : 0U);
        return;
    }
    if (!active_monitor) {
        auto plan = semantic::sva::decode(operation.plan);
        if (!plan || plan->bounds != operation.bounds.size()
            || plan->guards != operation.guard_count) {
            storage.fail("concurrent property plan is malformed");
        }
        std::vector<std::int64_t> bounds;
        for (const auto bound : operation.bounds) {
            const auto value = storage.read(bound, 64U).known_signed_value();
            if (!value || *value < 0) {
                storage.fail(
                    "concurrent property delay and repetition bounds "
                    "must be known nonnegative constants");
            }
            bounds.push_back(*value);
        }
        std::string error;
        try {
            active_monitor = std::make_unique<sva::Monitor>(std::move(*plan), bounds);
        } catch (const std::runtime_error& failure) {
            error = failure.what();
        }
        if (!active_monitor) {
            storage.fail(error);
        }
    }
    std::vector<bool> guards(operation.guard_count);
    if (operation.guard_count != 0U) {
        const auto values = storage.read(operation.guards, operation.guard_count);
        for (std::size_t index = 0; index < guards.size(); ++index) {
            guards[index] = known_one(values, index);
        }
    }
    bool start = known_one(storage.read(operation.start, 1U), 0U);
    // $assertoff and its VPI form stop new attempts; attempts already
    // running complete (IEEE 1800-2017 20.12).
    if (const auto filter = fork_spawn_filter; start && filter) {
        prepare_callback_observation();
        start = (*filter)(storage.design_process());
    }
    const bool disabled = known_one(storage.read(operation.disabled, 1U), 0U);
    std::string error;
    try {
        active_monitor->tick(guards, start, disabled,
            static_cast<std::int64_t>(scheduler.now()));
    } catch (const std::runtime_error& failure) {
        error = failure.what();
    }
    if (!error.empty()) {
        storage.fail(error);
    }
    write_result(active_monitor->pending_outcomes());
}

} // namespace fsim::runtime::simir
