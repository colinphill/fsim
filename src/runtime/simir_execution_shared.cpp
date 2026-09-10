// SPDX-License-Identifier: Apache-2.0
#include "simir_execution_shared.hpp"

#include "fsim/runtime/systemverilog_string.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fsim::runtime::simir {

[[nodiscard]] PackedLogic4 resize_class_value(
    const PackedLogic4& value,
    const std::size_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    for (std::size_t bit = 0; bit < std::min(width, value.width()); ++bit) {
        result.set(bit, value.get(bit));
    }
    return value.is_logic9() ? result.promoted_to_logic9() : result;
}

[[nodiscard]] SimulationTick normalized_dynamic_wait_delay(
    const WaitFor& wait,
    const PackedLogic4& payload)
{
    if (!wait.source || wait.source_width == 0
        || wait.source_width > 64
        || payload.width() != wait.source_width
        || wait.rounding_quantum == 0) {
        throw std::invalid_argument {
            "runtime WaitFor has invalid dynamic-delay metadata"
        };
    }
    if (wait.source_kind == SystemVerilogScalarKind::ShortReal
        || wait.source_kind == SystemVerilogScalarKind::Real
        || wait.source_kind == SystemVerilogScalarKind::Realtime) {
        const auto decoded = decode_systemverilog_scalar_payload(
            payload, wait.source_kind);
        const auto number = decoded ? decoded.value.as_real() : std::nullopt;
        if (!number || !std::isfinite(*number)) {
            throw std::invalid_argument {
                "runtime WaitFor requires a finite real delay"
            };
        }
        if (*number < 0.0) {
            throw std::invalid_argument {
                "runtime WaitFor delay cannot be negative"
            };
        }
        const auto scaled_quanta = static_cast<long double>(*number)
            * static_cast<long double>(wait.delay)
            / static_cast<long double>(wait.rounding_quantum);
        if (!std::isfinite(scaled_quanta)) {
            throw std::overflow_error {
                "runtime WaitFor delay overflows simulation ticks"
            };
        }
        const auto rounded_quanta = std::round(scaled_quanta);
        const auto maximum_quanta = static_cast<long double>(
                                        std::numeric_limits<SimulationTick>::max())
            / static_cast<long double>(wait.rounding_quantum);
        if (rounded_quanta > maximum_quanta) {
            throw std::overflow_error {
                "runtime WaitFor delay overflows simulation ticks"
            };
        }
        return static_cast<SimulationTick>(rounded_quanta)
            * wait.rounding_quantum;
    }

    std::uint64_t magnitude = 0;
    if (wait.source_kind == SystemVerilogScalarKind::Time) {
        const auto decoded = decode_systemverilog_scalar_payload(
            payload, wait.source_kind);
        const auto ticks = decoded ? decoded.value.as_time() : std::nullopt;
        if (!ticks) {
            throw std::invalid_argument {
                "runtime WaitFor requires a known time delay"
            };
        }
        magnitude = *ticks;
    } else if (wait.source_kind == SystemVerilogScalarKind::None) {
        const auto word = payload.low_word();
        if (word.bval != 0) {
            throw std::invalid_argument {
                "runtime WaitFor requires a known integral delay"
            };
        }
        if (wait.source_signed
            && payload.width() != 0
            && ((word.aval >> (payload.width() - 1U)) & 1U) != 0) {
            throw std::invalid_argument {
                "runtime WaitFor delay cannot be negative"
            };
        }
        magnitude = word.aval;
    } else {
        throw std::invalid_argument {
            "runtime WaitFor has an invalid delay value kind"
        };
    }
    if (magnitude != 0
        && wait.delay
            > std::numeric_limits<SimulationTick>::max() / magnitude) {
        throw std::overflow_error {
            "runtime WaitFor delay overflows simulation ticks"
        };
    }
    return magnitude * wait.delay;
}

[[nodiscard]] bool is_class_execution_boundary(const Operation& operation)
{
    return operation_holds<ClassAllocate>(operation)
        || operation_holds<ClassPropertyRead>(operation)
        || operation_holds<ClassPropertyWrite>(operation)
        || operation_holds<ClassMethodCall>(operation)
        || operation_holds<ClassStaticPropertyRead>(operation)
        || operation_holds<ClassStaticPropertyWrite>(operation)
        || operation_holds<ClassStaticMethodCall>(operation)
        || operation_holds<CoverageSample>(operation)
        || operation_holds<CoverageQuery>(operation)
        || operation_holds<VhdlPslApi>(operation)
        || operation_holds<VhdlAssertApi>(operation)
        || operation_holds<VhdlReflectionApi>(operation)
        || operation_holds<CoverageControl>(operation)
        || operation_holds<CoverageAccess>(operation)
        || operation_holds<PlusArgSelect>(operation);
}

[[nodiscard]] bool is_immediate_process_boundary(const Operation& operation)
{
    const auto* read = operation_get_if<ReadSignal>(&operation);
    const auto* disable_fork = operation_get_if<DisableFork>(&operation);
    return (read && read->kind != SignalReadKind::current)
        || (disable_fork && disable_fork->site)
        || operation_holds<ProcessSelf>(operation)
        || operation_holds<ProcessStatusQuery>(operation)
        || operation_holds<ProcessCompleted>(operation)
        || operation_holds<ProcessResume>(operation)
        || operation_holds<ProcessGetRandState>(operation)
        || operation_holds<ProcessSetRandState>(operation)
        || operation_holds<ProcessSrandom>(operation)
        || operation_holds<SystemCommand>(operation)
        || operation_holds<VcdControl>(operation)
        || operation_holds<CoverageDatabaseControl>(operation)
        || operation_holds<VhdlEnvironmentTime>(operation)
        || operation_holds<VhdlEnvironmentTimeToString>(operation)
        || operation_holds<VhdlEnvironmentDirectory>(operation)
        || operation_holds<VhdlEnvironmentGetenv>(operation)
        || operation_holds<VhdlEnvironmentCallPath>(operation)
        || operation_holds<VhdlEnvironmentGetCallPath>(operation)
        || operation_holds<StochasticQueueOperation>(operation)
        || operation_holds<PlaEvaluate>(operation)
        || operation_holds<TimeFormatControl>(operation)
        || operation_holds<EventTriggered>(operation)
        || operation_holds<EventAlias>(operation)
        || operation_holds<DisableBlock>(operation);
}

[[nodiscard]] bool is_synchronization_boundary(const Operation& operation)
{
    return operation_holds<MailboxCreate>(operation)
        || operation_holds<MailboxPut>(operation)
        || operation_holds<MailboxGet>(operation)
        || operation_holds<MailboxNum>(operation)
        || operation_holds<SemaphoreCreate>(operation)
        || operation_holds<SemaphoreGet>(operation)
        || operation_holds<SemaphorePut>(operation);
}

[[nodiscard]] bool is_dynamic_callable_boundary(const Operation& operation)
{
    const auto* call = operation_get_if<Call>(&operation);
    const auto* return_operation = operation_get_if<Return>(&operation);
    return (call && call->stack.capacity == 0)
        || (return_operation && return_operation->stack.capacity == 0)
        || operation_holds<CallableFramePush>(operation)
        || operation_holds<CallableFramePop>(operation);
}

[[nodiscard]] double known_real(
    const PackedLogic4& value,
    const char* name)
{
    if (value.width() != 64U || value.is_logic9()) {
        throw std::invalid_argument {
            std::string { "STD.ENV " } + name
            + " is not a binary64 REAL"
        };
    }
    const auto word = value.low_word();
    const auto result = std::bit_cast<double>(word.aval);
    if (word.bval != 0U || !std::isfinite(result)) {
        throw std::invalid_argument {
            std::string { "STD.ENV " } + name
            + " must be a known finite REAL"
        };
    }
    return result;
}

[[nodiscard]] PackedLogic4 real_value(const double value)
{
    if (!std::isfinite(value)) {
        throw std::invalid_argument {
            "STD.ENV date/time conversion produced a non-finite REAL"
        };
    }
    return PackedLogic4::from_aval_bval(
        64U, std::bit_cast<std::uint64_t>(value), 0U);
}

[[nodiscard]] bool vhdl_path_below_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate)
{
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end()
            || *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::filesystem::path> vhdl_directory_path(
    const std::filesystem::path& root,
    const std::filesystem::path& working,
    const std::string_view text,
    std::error_code& error)
{
    error.clear();
    if (root.empty() || working.empty() || text.empty()) {
        error = std::make_error_code(std::errc::permission_denied);
        return std::nullopt;
    }
    std::filesystem::path requested;
    try {
        requested = fsim::support::path_from_utf8(text);
    } catch (const std::exception&) {
        error = std::make_error_code(std::errc::invalid_argument);
        return std::nullopt;
    }
    auto candidate = requested.is_absolute()
        ? requested
        : working / requested;
    candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error || !vhdl_path_below_root(root, candidate)) {
        if (!error) {
            error = std::make_error_code(std::errc::permission_denied);
        }
        return std::nullopt;
    }
    return candidate;
}

[[nodiscard]] bool vhdl_access_denied(const std::error_code& error)
{
    return error == std::errc::permission_denied
        || error == std::errc::operation_not_permitted;
}

} // namespace fsim::runtime::simir
