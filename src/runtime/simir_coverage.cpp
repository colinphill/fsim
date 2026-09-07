// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] bool executable_metric(
    const ::fsim::runtime::CodeCoverageMetric metric) noexcept
{
    return metric == ::fsim::runtime::CodeCoverageMetric::Statement
        || metric == ::fsim::runtime::CodeCoverageMetric::Branch;
}

[[nodiscard]] CodeCoverageHitValidationResult error(
    const CodeCoverageHitError value,
    const std::size_t instruction = 0U,
    const std::size_t owner = 0U) noexcept
{
    return { value, instruction, owner };
}

} // namespace

void CodeCoverageCounters::reset(
    std::vector<std::uint64_t> values,
    const std::size_t maximum_points)
{
    if (values.size() > maximum_points) {
        throw std::length_error {
            "code coverage counter table exceeds its point limit"
        };
    }
    std::vector<std::uint8_t> overflowed(values.size(), 0U);
    std::vector<std::uint8_t> enabled(values.size(), 1U);
    values_ = std::move(values);
    overflowed_ = std::move(overflowed);
    enabled_counters_ = std::move(enabled);
    overflow_count_ = 0U;
    configured_ = true;
    enabled_ = true;
}

CodeCoverageCounterUpdate CodeCoverageCounters::record(
    const ::fsim::runtime::CodeCoverageCounterId counter) noexcept
{
    if (!configured_) {
        return CodeCoverageCounterUpdate::Unavailable;
    }
    if (counter.value >= values_.size()) {
        return CodeCoverageCounterUpdate::OutOfRange;
    }
    if (!enabled_ && enabled_counters_[counter.value] == 0U) {
        return CodeCoverageCounterUpdate::Ignored;
    }
    auto& value = values_[counter.value];
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
        return CodeCoverageCounterUpdate::Incremented;
    }
    auto& overflowed = overflowed_[counter.value];
    if (overflowed == 0U) {
        overflowed = 1U;
        ++overflow_count_;
        return CodeCoverageCounterUpdate::FirstOverflow;
    }
    return CodeCoverageCounterUpdate::Saturated;
}

bool CodeCoverageCounters::configured() const noexcept
{
    return configured_;
}

bool CodeCoverageCounters::enabled() const noexcept
{
    return enabled_;
}

void CodeCoverageCounters::set_enabled(const bool enabled) noexcept
{
    enabled_ = enabled;
    std::ranges::fill(enabled_counters_, enabled ? 1U : 0U);
}

bool CodeCoverageCounters::set_enabled(
    const std::span<const ::fsim::runtime::CodeCoverageCounterId> counters,
    const bool enabled) noexcept
{
    for (const auto counter : counters) {
        if (counter.value >= enabled_counters_.size()) {
            return false;
        }
    }
    for (const auto counter : counters) {
        enabled_counters_[counter.value] = enabled ? 1U : 0U;
    }
    enabled_ = std::ranges::all_of(
        enabled_counters_, [](const auto value) { return value != 0U; });
    return true;
}

void CodeCoverageCounters::clear() noexcept
{
    std::ranges::fill(values_, 0U);
    std::ranges::fill(overflowed_, 0U);
    overflow_count_ = 0U;
}

bool CodeCoverageCounters::clear(
    const std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
    noexcept
{
    for (const auto counter : counters) {
        if (counter.value >= values_.size()) {
            return false;
        }
    }
    for (const auto counter : counters) {
        if (overflowed_[counter.value] != 0U) {
            overflowed_[counter.value] = 0U;
            --overflow_count_;
        }
        values_[counter.value] = 0U;
    }
    return true;
}

std::span<const std::uint64_t> CodeCoverageCounters::values() const noexcept
{
    return values_;
}

std::span<std::uint64_t> CodeCoverageCounters::mutable_values() noexcept
{
    return values_;
}

std::span<std::uint64_t> CodeCoverageCounters::direct_values() noexcept
{
    return enabled_ ? std::span<std::uint64_t> { values_ }
                    : std::span<std::uint64_t> { };
}

bool CodeCoverageCounters::overflowed(
    const ::fsim::runtime::CodeCoverageCounterId counter) const noexcept
{
    return counter.value < overflowed_.size()
        && overflowed_[counter.value] != 0U;
}

std::size_t CodeCoverageCounters::overflow_count() const noexcept
{
    return overflow_count_;
}

std::optional<std::size_t> CodeCoverageCounters::overflow_count(
    const std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
    const noexcept
{
    std::size_t result { };
    for (const auto counter : counters) {
        if (counter.value >= overflowed_.size()) {
            return std::nullopt;
        }
        result += overflowed_[counter.value] != 0U ? 1U : 0U;
    }
    return result;
}

// Keep the public interpreter bridge with its coverage-owned storage rather
// than growing the general interpreter translation unit.
void Interpreter::set_code_coverage_counters(
    std::vector<std::uint64_t> counters)
{
    if (impl_->started) {
        throw std::logic_error {
            "cannot set code coverage counters after simulation start"
        };
    }
    impl_->code_coverage_counters.reset(std::move(counters));
}

std::span<const std::uint64_t>
Interpreter::code_coverage_counters() const noexcept
{
    return impl_->code_coverage_counters.values();
}

bool Interpreter::code_coverage_counter_overflowed(
    const ::fsim::runtime::CodeCoverageCounterId counter) const noexcept
{
    return impl_->code_coverage_counters.overflowed(counter);
}

std::size_t Interpreter::code_coverage_overflow_count() const noexcept
{
    return impl_->code_coverage_counters.overflow_count();
}

bool Interpreter::code_coverage_counters_configured() const noexcept
{
    return impl_->code_coverage_counters.configured();
}

bool Interpreter::code_coverage_collection_enabled() const noexcept
{
    return impl_->code_coverage_counters.enabled();
}

void Interpreter::set_code_coverage_collection_enabled(
    const bool enabled) noexcept
{
    impl_->code_coverage_counters.set_enabled(enabled);
}

bool Interpreter::set_code_coverage_collection_enabled(
    const std::span<const ::fsim::runtime::CodeCoverageCounterId> counters,
    const bool enabled) noexcept
{
    return impl_->code_coverage_counters.set_enabled(counters, enabled);
}

void Interpreter::reset_code_coverage_counters() noexcept
{
    impl_->code_coverage_counters.clear();
}

bool Interpreter::reset_code_coverage_counters(
    const std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
    noexcept
{
    return impl_->code_coverage_counters.clear(counters);
}

std::optional<std::size_t> Interpreter::code_coverage_overflow_count(
    const std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
    const noexcept
{
    return impl_->code_coverage_counters.overflow_count(counters);
}

void Interpreter::set_code_coverage_overflow_hook(
    CodeCoverageOverflowHook hook)
{
    impl_->code_coverage_overflow_hook = std::move(hook);
}

void Interpreter::set_coverage_access_hook(CoverageAccessHook hook)
{
    impl_->coverage_access_hook = std::move(hook);
}

std::int32_t Interpreter::Impl::control_coverage(
    const CoverageControlEvent& event) noexcept
{
    using Command = SystemVerilogCoverageCommand;
    using Status = SystemVerilogCoverageStatus;
    using Scope = SystemVerilogCoverageScope;
    using Type = SystemVerilogCoverageType;
    const auto status = [](const Status value) {
        return static_cast<std::int32_t>(value);
    };
    const auto command = static_cast<Command>(event.command);
    if (command != Command::start && command != Command::stop
        && command != Command::reset && command != Command::check) {
        return status(Status::error);
    }
    const auto scope = static_cast<Scope>(event.scope);
    if ((scope != Scope::module && scope != Scope::hierarchy)
        || event.selector.empty()) {
        return status(Status::error);
    }
    const auto type = static_cast<Type>(event.coverage_type);
    if (type != Type::assertion && type != Type::fsm_state
        && type != Type::statement && type != Type::toggle) {
        return status(Status::error);
    }
    return coverage_control_hook
        ? coverage_control_hook(event)
        : status(Status::no_coverage);
}

std::int32_t Interpreter::Impl::access_coverage(
    const CoverageAccessEvent& event) noexcept
{
    using Access = SystemVerilogCoverageAccessKind;
    using Status = SystemVerilogCoverageStatus;
    using Scope = SystemVerilogCoverageScope;
    using Type = SystemVerilogCoverageType;
    const auto status = [](const Status value) {
        return static_cast<std::int32_t>(value);
    };
    if (event.kind != Access::get && event.kind != Access::get_max
        && event.kind != Access::merge && event.kind != Access::save) {
        return status(Status::error);
    }
    const auto selection_call = event.kind == Access::get
        || event.kind == Access::get_max;
    if (selection_call) {
        if (!event.scope || event.selector.empty()) {
            return status(Status::error);
        }
        const auto scope = static_cast<Scope>(*event.scope);
        if (scope != Scope::module && scope != Scope::hierarchy) {
            return status(Status::error);
        }
    } else if (event.scope || !event.selector.empty()) {
        return status(Status::error);
    }
    const auto type = static_cast<Type>(event.coverage_type);
    if (type != Type::assertion && type != Type::fsm_state
        && type != Type::statement && type != Type::toggle) {
        return status(Status::error);
    }
    if (coverage_access_hook) {
        return coverage_access_hook(event);
    }
    if (type != Type::statement
        || !code_coverage_counters.configured()
        || event.kind == Access::merge || event.kind == Access::save) {
        return status(Status::no_coverage);
    }
    const auto values = code_coverage_counters.values();
    const auto count = event.kind == Access::get_max
        ? values.size()
        : static_cast<std::size_t>(std::ranges::count_if(
              values, [](const auto value) { return value != 0U; }));
    return count > static_cast<std::size_t>(
                       std::numeric_limits<std::int32_t>::max())
        ? status(Status::overflow)
        : static_cast<std::int32_t>(count);
}

CodeCoverageHitValidationResult validate_code_coverage_hit(
    const CodeCoverageHit& hit,
    const ::fsim::runtime::CodeCoveragePoint& owner,
    const std::size_t counter_count) noexcept
{
    if (!::fsim::runtime::is_code_coverage_identity_valid(hit.point)
        || !::fsim::runtime::is_code_coverage_identity_valid(owner.id)) {
        return error(CodeCoverageHitError::InvalidPointIdentity);
    }
    if (!executable_metric(hit.metric) || !executable_metric(owner.metric)) {
        return error(CodeCoverageHitError::InvalidMetric);
    }
    if (hit.counter.value >= counter_count
        || owner.counter.value >= counter_count) {
        return error(CodeCoverageHitError::CounterOutOfRange);
    }
    if (hit.point != owner.id || hit.metric != owner.metric) {
        return error(CodeCoverageHitError::PointOwnershipMismatch);
    }
    if (hit.counter != owner.counter) {
        return error(CodeCoverageHitError::CounterOwnershipMismatch);
    }
    return { };
}

CodeCoverageHitValidationResult validate_code_coverage_hits(
    const Process& process,
    const std::span<const ::fsim::runtime::CodeCoveragePoint> owners,
    const std::size_t counter_count,
    const CodeCoverageHitLimits limits) noexcept
{
    if (owners.size() > limits.maximum_points
        || process.operations.size() > limits.maximum_operations
        || process.operations.size()
            > std::numeric_limits<InstructionIndex>::max()) {
        return error(CodeCoverageHitError::ResourceLimit);
    }

    std::uint64_t first_counter { };
    for (std::size_t index = 0U; index < owners.size(); ++index) {
        const auto& owner = owners[index];
        if (!::fsim::runtime::is_code_coverage_identity_valid(owner.id)) {
            return error(
                CodeCoverageHitError::InvalidPointIdentity, 0U, index);
        }
        if (!executable_metric(owner.metric)) {
            return error(CodeCoverageHitError::InvalidMetric, 0U, index);
        }
        if (index == 0U) {
            first_counter = owner.counter.value;
        }
        const auto expected = first_counter + index;
        if (expected > std::numeric_limits<std::uint32_t>::max()
            || owner.counter.value != expected) {
            return error(
                CodeCoverageHitError::CounterOwnershipMismatch, 0U, index);
        }
        if (owner.counter.value >= counter_count) {
            return error(
                CodeCoverageHitError::CounterOutOfRange, 0U, index);
        }
        if (index != 0U) {
            const auto& prior = owners[index - 1U].id;
            if (prior == owner.id) {
                return error(
                    CodeCoverageHitError::DuplicatePointOwnership,
                    0U, index);
            }
            if (prior.high > owner.id.high
                || (prior.high == owner.id.high
                    && prior.low > owner.id.low)) {
                return error(
                    CodeCoverageHitError::NoncanonicalPointOwnership,
                    0U, index);
            }
        }
    }

    try {
        std::vector<std::uint8_t> seen(owners.size(), 0U);
        for (std::size_t instruction = 0U;
             instruction < process.operations.size(); ++instruction) {
            const auto* canonical = operation_get_if<CodeCoverageHit>(
                &process.operations[instruction]);
            if (canonical == nullptr) {
                continue;
            }
            auto hit = *canonical;
            hit.counter = process.operations.code_coverage_counter(
                instruction, hit.counter);
            if (owners.empty()
                || hit.counter.value < first_counter
                || static_cast<std::uint64_t>(hit.counter.value)
                    >= first_counter + owners.size()) {
                return error(
                    CodeCoverageHitError::CounterOutOfRange, instruction);
            }
            const auto owner_index = static_cast<std::size_t>(
                hit.counter.value - first_counter);
            const auto validation = validate_code_coverage_hit(
                hit, owners[owner_index], counter_count);
            if (!validation.ok()) {
                return error(validation.error, instruction, owner_index);
            }
            if (seen[owner_index] != 0U) {
                return error(
                    CodeCoverageHitError::DuplicateHit,
                    instruction, owner_index);
            }
            seen[owner_index] = 1U;
        }
    } catch (const std::bad_alloc&) {
        return error(CodeCoverageHitError::ResourceLimit);
    } catch (const std::length_error&) {
        return error(CodeCoverageHitError::ResourceLimit);
    }
    return { };
}

} // namespace fsim::runtime::simir
