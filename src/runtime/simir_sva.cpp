// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include "fsim/semantic/sva_plan.hpp"

namespace fsim::runtime::simir {

void Interpreter::Impl::execute_sva(
    ProcessState& process,
    const SvaOperation& operation)
{
    const auto read = [&](const RegisterId id, const std::size_t width) {
        return process.executor ? process.executor->read_register(id, width)
                                : get_register(process, id);
    };
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
        write_process_register(process, operation.result,
            PackedLogic4::from_aval_bval(
                32U, static_cast<std::uint32_t>(value), 0));
    };
    auto& active_monitor = sva_monitors[{ process.id, operation.site }];
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
            fail(process, "concurrent property plan is malformed");
        }
        std::vector<std::int64_t> bounds;
        for (const auto bound : operation.bounds) {
            const auto value = read(bound, 64U).known_signed_value();
            if (!value || *value < 0) {
                fail(process,
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
            fail(process, error);
        }
    }
    std::vector<bool> guards(operation.guard_count);
    if (operation.guard_count != 0U) {
        const auto values = read(operation.guards, operation.guard_count);
        for (std::size_t index = 0; index < guards.size(); ++index) {
            guards[index] = known_one(values, index);
        }
    }
    bool start = known_one(read(operation.start, 1U), 0U);
    // $assertoff and its VPI form stop new attempts; attempts already
    // running complete (IEEE 1800-2017 20.12).
    if (const auto filter = fork_spawn_filter; start && filter) {
        prepare_callback_observation();
        start = (*filter)(process.cold().design_process);
    }
    const bool disabled = known_one(read(operation.disabled, 1U), 0U);
    std::string error;
    try {
        active_monitor->tick(guards, start, disabled);
    } catch (const std::runtime_error& failure) {
        error = failure.what();
    }
    if (!error.empty()) {
        fail(process, error);
    }
    write_result(active_monitor->pending_outcomes());
}

} // namespace fsim::runtime::simir
