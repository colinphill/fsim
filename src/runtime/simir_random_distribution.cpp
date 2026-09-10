// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace fsim::runtime::simir {
namespace {

    constexpr double kInverseTwoTo23 = 0.00000011920928955078125;
    constexpr std::uint32_t kMaximumDistributionDraws = 1'000'000U;

    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);

    struct DistributionState {
        std::int32_t seed { };
        std::uint32_t draws { };
        bool exhausted { };
    };

    [[nodiscard]] double next_uniform(
        DistributionState& distribution,
        const std::int32_t start,
        const std::int32_t end)
    {
        if (distribution.draws == kMaximumDistributionDraws) {
            distribution.exhausted = true;
            return 0.0;
        }
        ++distribution.draws;
        auto state = std::bit_cast<std::uint32_t>(distribution.seed);
        if (state == 0U)
            state = 259341593U;
        state = state * 69069U + 1U;
        distribution.seed = std::bit_cast<std::int32_t>(state);

        const double low = start < end ? static_cast<double>(start) : 0.0;
        const double high = start < end
            ? static_cast<double>(end)
            : static_cast<double>(std::numeric_limits<std::int32_t>::max());
        const auto fraction_bits = (state & UINT32_C(0x007fffff))
            | UINT32_C(0x3f800000);
        double fraction = static_cast<double>(
            std::bit_cast<float>(fraction_bits));
        fraction += fraction * kInverseTwoTo23;
        return (high - low) * (fraction - 1.0) + low;
    }

    [[nodiscard]] std::int32_t rounded_integer(const double value)
    {
        if (std::isnan(value))
            return 0;
        if (value >= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            return std::numeric_limits<std::int32_t>::max();
        if (value <= static_cast<double>(std::numeric_limits<std::int32_t>::min()))
            return std::numeric_limits<std::int32_t>::min();
        return static_cast<std::int32_t>(std::round(value));
    }

    [[nodiscard]] std::int32_t uniform_integer(
        DistributionState& distribution,
        std::int32_t start,
        std::int32_t end)
    {
        if (start >= end)
            return start;

        double sampled { };
        if (end != std::numeric_limits<std::int32_t>::max()) {
            ++end;
            sampled = next_uniform(distribution, start, end);
            auto value = sampled >= 0.0
                ? static_cast<std::int32_t>(sampled)
                : static_cast<std::int32_t>(sampled - 1.0);
            if (value < start)
                value = start;
            if (value >= end)
                value = end - 1;
            return value;
        }

        if (start != std::numeric_limits<std::int32_t>::min()) {
            --start;
            sampled = next_uniform(distribution, start, end) + 1.0;
            auto value = sampled >= 0.0
                ? static_cast<std::int32_t>(sampled)
                : static_cast<std::int32_t>(sampled - 1.0);
            if (value <= start)
                value = start + 1;
            if (value > end)
                value = end;
            return value;
        }

        sampled = (next_uniform(distribution, start, end) + 2147483648.0)
            / 4294967295.0;
        sampled = sampled * 4294967296.0 - 2147483648.0;
        if (sampled >= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            return std::numeric_limits<std::int32_t>::max();
        if (sampled <= static_cast<double>(std::numeric_limits<std::int32_t>::min()))
            return std::numeric_limits<std::int32_t>::min();
        return sampled >= 0.0
            ? static_cast<std::int32_t>(sampled)
            : static_cast<std::int32_t>(sampled - 1.0);
    }

    [[nodiscard]] double normal_value(
        DistributionState& distribution,
        const std::int32_t mean,
        const std::int32_t deviation)
    {
        double first { };
        double second { };
        double radius = 1.0;
        while (!distribution.exhausted
               && (radius >= 1.0 || radius == 0.0)) {
            first = next_uniform(distribution, -1, 1);
            second = next_uniform(distribution, -1, 1);
            radius = first * first + second * second;
        }
        if (distribution.exhausted)
            return 0.0;
        const auto normal = first * std::sqrt(-2.0 * std::log(radius) / radius);
        return normal * static_cast<double>(deviation)
            + static_cast<double>(mean);
    }

    [[nodiscard]] double exponential_value(
        DistributionState& distribution,
        const std::int32_t mean)
    {
        const auto uniform = next_uniform(distribution, 0, 1);
        return uniform == 0.0
            ? 0.0
            : -std::log(uniform) * static_cast<double>(mean);
    }

    [[nodiscard]] std::int32_t poisson_value(
        DistributionState& distribution,
        const std::int32_t mean)
    {
        std::int32_t result { };
        const auto threshold = std::exp(-static_cast<double>(mean));
        auto product = next_uniform(distribution, 0, 1);
        while (!distribution.exhausted && threshold < product) {
            ++result;
            product *= next_uniform(distribution, 0, 1);
        }
        return result;
    }

    [[nodiscard]] double chi_square_value(
        DistributionState& distribution,
        const std::int32_t degrees)
    {
        double result { };
        if ((degrees & 1) != 0) {
            result = normal_value(distribution, 0, 1);
            result *= result;
        }
        for (std::int64_t degree = 2;
             !distribution.exhausted && degree <= degrees; degree += 2)
            result += 2.0 * exponential_value(distribution, 1);
        return result;
    }

    [[nodiscard]] double student_t_value(
        DistributionState& distribution,
        const std::int32_t degrees)
    {
        const auto denominator = std::sqrt(
            chi_square_value(distribution, degrees)
            / static_cast<double>(degrees));
        return normal_value(distribution, 0, 1) / denominator;
    }

    [[nodiscard]] double erlang_value(
        DistributionState& distribution,
        const std::int32_t stages,
        const std::int32_t mean)
    {
        double product = 1.0;
        for (std::int64_t stage = 0;
             !distribution.exhausted && stage < stages; ++stage)
            product *= next_uniform(distribution, 0, 1);
        return -static_cast<double>(mean) * std::log(product)
            / static_cast<double>(stages);
    }

} // namespace

RandomDistributionResult evaluate_random_distribution(
    const RandomDistributionKind kind,
    const std::int32_t seed,
    const std::int32_t first,
    const std::optional<std::int32_t> second)
{
    DistributionState distribution { seed };
    std::int32_t value { };
    auto issue = RandomDistributionIssue::none;
    switch (kind) {
    case RandomDistributionKind::uniform:
        value = uniform_integer(distribution, first, second.value_or(0));
        break;
    case RandomDistributionKind::normal:
        value = rounded_integer(
            normal_value(distribution, first, second.value_or(0)));
        break;
    case RandomDistributionKind::exponential:
        if (first > 0) {
            value = rounded_integer(exponential_value(distribution, first));
        } else {
            issue = RandomDistributionIssue::exponential_mean;
        }
        break;
    case RandomDistributionKind::poisson:
        if (first > 0) {
            value = poisson_value(distribution, first);
        } else {
            issue = RandomDistributionIssue::poisson_mean;
        }
        break;
    case RandomDistributionKind::chi_square:
        if (first > 0) {
            value = rounded_integer(chi_square_value(distribution, first));
        } else {
            issue = RandomDistributionIssue::chi_square_degrees;
        }
        break;
    case RandomDistributionKind::student_t:
        if (first > 0) {
            value = rounded_integer(student_t_value(distribution, first));
        } else {
            issue = RandomDistributionIssue::student_t_degrees;
        }
        break;
    case RandomDistributionKind::erlang:
        if (first > 0) {
            value = rounded_integer(
                erlang_value(distribution, first, second.value_or(0)));
        } else {
            issue = RandomDistributionIssue::erlang_stages;
        }
        break;
    default:
        throw std::logic_error { "invalid random distribution kind" };
    }
    if (distribution.exhausted) {
        return { 0, seed, RandomDistributionIssue::resource_limit };
    }
    return { value, distribution.seed, issue };
}

std::string_view random_distribution_issue_message(
    const RandomDistributionIssue issue) noexcept
{
    switch (issue) {
    case RandomDistributionIssue::none:
        return { };
    case RandomDistributionIssue::exponential_mean:
        return "exponential distribution mean must be positive";
    case RandomDistributionIssue::poisson_mean:
        return "Poisson distribution mean must be positive";
    case RandomDistributionIssue::chi_square_degrees:
        return "chi-square distribution degrees of freedom must be positive";
    case RandomDistributionIssue::student_t_degrees:
        return "Student-t distribution degrees of freedom must be positive";
    case RandomDistributionIssue::erlang_stages:
        return "Erlang distribution stage count must be positive";
    case RandomDistributionIssue::resource_limit:
        return "random distribution exceeded the bounded draw limit";
    }
    return "random distribution reported an invalid issue";
}

void Interpreter::Impl::execute_random_distribution(
    ProcessState& process,
    const RandomDistribution& operation)
{
    const auto integer_operand = [&](const RegisterId register_id)
        -> std::optional<std::int32_t> {
        const auto operand = process.executor
            ? process.executor->read_register(register_id, 32U)
            : get_register(process, register_id);
        if (operand.width() != 32U || operand.is_logic9()) {
            fail(
                process,
                "random distribution operand must be a 32-bit integer");
        }
        if (operand.low_word().bval != 0U) {
            return std::nullopt;
        }
        return std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(operand.low_word().aval));
    };
    const auto report_warning = [&](const std::string_view message) {
        if (report_hook) {
            report_hook(
                process.program.id, message, AssertionSeverity::warning,
                operation.source, scheduler.now(), scheduler.delta());
        }
    };
    const auto seed = integer_operand(operation.seed);
    const auto first = integer_operand(operation.first);
    const auto second = operation.second
        ? integer_operand(*operation.second)
        : std::nullopt;
    if (!seed || !first || (operation.second && !second)) {
        report_warning(
            "random distribution arguments must not contain X or Z");
        write_process_register(
            process, operation.destination,
            PackedLogic4::from_aval_bval(32U, 0U, 0U));
        return;
    }
    const auto evaluated = evaluate_random_distribution(
        operation.kind, *seed, *first, second);
    if (evaluated.issue != RandomDistributionIssue::none) {
        report_warning(random_distribution_issue_message(evaluated.issue));
    }
    write_process_register(
        process, operation.destination,
        PackedLogic4::from_aval_bval(
            32U, std::bit_cast<std::uint32_t>(evaluated.value), 0U));
    write_process_register(
        process, operation.seed,
        PackedLogic4::from_aval_bval(
            32U, std::bit_cast<std::uint32_t>(evaluated.seed), 0U));
}

} // namespace fsim::runtime::simir
