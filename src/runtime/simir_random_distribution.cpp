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

    [[nodiscard]] double next_uniform(
        std::int32_t& seed,
        const std::int32_t start,
        const std::int32_t end)
    {
        auto state = std::bit_cast<std::uint32_t>(seed);
        if (state == 0U)
            state = 259341593U;
        state = state * 69069U + 1U;
        seed = std::bit_cast<std::int32_t>(state);

        const double low = start < end ? static_cast<double>(start) : 0.0;
        const double high = start < end
            ? static_cast<double>(end)
            : static_cast<double>(std::numeric_limits<std::int32_t>::max());
        double fraction = 1.0
            + static_cast<double>(state >> 9U) * kInverseTwoTo23;
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
        std::int32_t& seed,
        std::int32_t start,
        std::int32_t end)
    {
        if (start >= end)
            return start;

        double sampled { };
        if (end != std::numeric_limits<std::int32_t>::max()) {
            ++end;
            sampled = next_uniform(seed, start, end);
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
            sampled = next_uniform(seed, start, end) + 1.0;
            auto value = sampled >= 0.0
                ? static_cast<std::int32_t>(sampled)
                : static_cast<std::int32_t>(sampled - 1.0);
            if (value <= start)
                value = start + 1;
            if (value > end)
                value = end;
            return value;
        }

        sampled = (next_uniform(seed, start, end) + 2147483648.0)
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
        std::int32_t& seed,
        const std::int32_t mean,
        const std::int32_t deviation)
    {
        double first { };
        double second { };
        double radius = 1.0;
        while (radius >= 1.0 || radius == 0.0) {
            first = next_uniform(seed, -1, 1);
            second = next_uniform(seed, -1, 1);
            radius = first * first + second * second;
        }
        const auto normal = first * std::sqrt(-2.0 * std::log(radius) / radius);
        return normal * static_cast<double>(deviation)
            + static_cast<double>(mean);
    }

    [[nodiscard]] double exponential_value(
        std::int32_t& seed,
        const std::int32_t mean)
    {
        const auto uniform = next_uniform(seed, 0, 1);
        return uniform == 0.0
            ? 0.0
            : -std::log(uniform) * static_cast<double>(mean);
    }

    [[nodiscard]] std::int32_t poisson_value(
        std::int32_t& seed,
        const std::int32_t mean)
    {
        std::int32_t result { };
        const auto threshold = std::exp(-static_cast<double>(mean));
        auto product = next_uniform(seed, 0, 1);
        while (threshold < product) {
            ++result;
            product *= next_uniform(seed, 0, 1);
        }
        return result;
    }

    [[nodiscard]] double chi_square_value(
        std::int32_t& seed,
        const std::int32_t degrees)
    {
        double result { };
        if ((degrees & 1) != 0) {
            result = normal_value(seed, 0, 1);
            result *= result;
        }
        for (std::int32_t degree = 2; degree <= degrees; degree += 2)
            result += 2.0 * exponential_value(seed, 1);
        return result;
    }

    [[nodiscard]] double student_t_value(
        std::int32_t& seed,
        const std::int32_t degrees)
    {
        const auto denominator = std::sqrt(
            chi_square_value(seed, degrees) / static_cast<double>(degrees));
        return normal_value(seed, 0, 1) / denominator;
    }

    [[nodiscard]] double erlang_value(
        std::int32_t& seed,
        const std::int32_t stages,
        const std::int32_t mean)
    {
        double product = 1.0;
        for (std::int32_t stage = 0; stage < stages; ++stage)
            product *= next_uniform(seed, 0, 1);
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
    auto updated_seed = seed;
    std::int32_t value { };
    switch (kind) {
    case RandomDistributionKind::uniform:
        value = uniform_integer(updated_seed, first, second.value_or(0));
        break;
    case RandomDistributionKind::normal:
        value = rounded_integer(
            normal_value(updated_seed, first, second.value_or(0)));
        break;
    case RandomDistributionKind::exponential:
        if (first > 0)
            value = rounded_integer(exponential_value(updated_seed, first));
        break;
    case RandomDistributionKind::poisson:
        if (first > 0)
            value = poisson_value(updated_seed, first);
        break;
    case RandomDistributionKind::chi_square:
        if (first > 0)
            value = rounded_integer(chi_square_value(updated_seed, first));
        break;
    case RandomDistributionKind::student_t:
        if (first > 0)
            value = rounded_integer(student_t_value(updated_seed, first));
        break;
    case RandomDistributionKind::erlang:
        if (first > 0)
            value = rounded_integer(
                erlang_value(updated_seed, first, second.value_or(0)));
        break;
    default:
        throw std::logic_error { "invalid random distribution kind" };
    }
    return { value, updated_seed };
}

} // namespace fsim::runtime::simir
