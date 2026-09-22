// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace fsim::elaboration::elaboration_detail {

bool is_two_state_domain(const frontend::ValueDomain domain) noexcept
{
    return domain == frontend::ValueDomain::Bit2
        || domain == frontend::ValueDomain::Boolean
        || domain == frontend::ValueDomain::Integer;
}

ValueKind value_kind(const frontend::ValueDomain domain) noexcept
{
    return domain == frontend::ValueDomain::Logic9
        ? ValueKind::logic9
        : ValueKind::logic4;
}

std::string simple_top_name(std::string_view top)
{
    if (const auto colon = top.rfind(':');
        colon != std::string_view::npos) {
        top.remove_prefix(colon + 1U);
    }
    if (const auto dot = top.rfind('.'); dot != std::string_view::npos) {
        top.remove_prefix(dot + 1U);
    }
    if (const auto architecture = top.find('(');
        architecture != std::string_view::npos) {
        top = top.substr(0U, architecture);
    }
    return std::string { top };
}

std::optional<std::uint64_t> unsigned_decimal(std::string_view text)
{
    std::string cleaned { text };
    cleaned.erase(
        std::remove(cleaned.begin(), cleaned.end(), '_'), cleaned.end());
    std::uint64_t value { };
    const auto result = std::from_chars(
        cleaned.data(), cleaned.data() + cleaned.size(), value);
    if (result.ec != std::errc { }
        || result.ptr != cleaned.data() + cleaned.size()) {
        return std::nullopt;
    }
    return value;
}

std::uint64_t index_distance(
    const std::int64_t lhs,
    const std::int64_t rhs) noexcept
{
    return lhs >= rhs
        ? static_cast<std::uint64_t>(lhs)
            - static_cast<std::uint64_t>(rhs)
        : static_cast<std::uint64_t>(rhs)
            - static_cast<std::uint64_t>(lhs);
}

PackedLogic4 unsigned_value(
    const std::uint64_t value,
    const std::size_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    for (std::size_t bit { }; bit < width && bit < 64U; ++bit) {
        result.set(bit,
            ((value >> bit) & 1U) != 0U ? Logic4::one : Logic4::zero);
    }
    return result;
}

PackedLogic4 integer_value(
    const std::int64_t value,
    const std::size_t width)
{
    PackedLogic4 result(
        width, value < 0 ? Logic4::one : Logic4::zero);
    const auto encoded = static_cast<std::uint64_t>(value);
    for (std::size_t bit { }; bit < width && bit < 64U; ++bit) {
        result.set(bit,
            ((encoded >> bit) & 1U) != 0U
                ? Logic4::one
                : Logic4::zero);
    }
    return result;
}

bool checked_add(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result)
{
    if ((right > 0
            && left > std::numeric_limits<std::int64_t>::max() - right)
        || (right < 0
            && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_subtract(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result)
{
    if ((right < 0
            && left > std::numeric_limits<std::int64_t>::max() + right)
        || (right > 0
            && left < std::numeric_limits<std::int64_t>::min() + right)) {
        return false;
    }
    result = left - right;
    return true;
}

bool checked_multiply(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result)
{
    if (left == 0 || right == 0) {
        result = 0;
        return true;
    }
    if ((left == -1 && right == std::numeric_limits<std::int64_t>::min())
        || (right == -1
            && left == std::numeric_limits<std::int64_t>::min())) {
        return false;
    }
    if (left > 0) {
        if ((right > 0
                && left > std::numeric_limits<std::int64_t>::max() / right)
            || (right < 0
                && right
                    < std::numeric_limits<std::int64_t>::min() / left)) {
            return false;
        }
    } else if ((right > 0
                   && left
                       < std::numeric_limits<std::int64_t>::min() / right)
        || (right < 0
            && left < std::numeric_limits<std::int64_t>::max() / right)) {
        return false;
    }
    result = left * right;
    return true;
}

} // namespace fsim::elaboration::elaboration_detail
