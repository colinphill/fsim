// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_random.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace fsim::systemc {
namespace {

    void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    std::uint64_t digest_seed(
        const std::string_view domain,
        const std::uint64_t parent,
        const std::uint64_t high,
        const std::uint64_t low,
        const std::string_view identity,
        const std::uint64_t ordinal)
    {
        constexpr std::string_view prefix = "fsim-scv-random-v1";
        std::vector<std::byte> bytes;
        bytes.reserve(prefix.size() + domain.size() + identity.size() + 40U);
        const auto append_text = [&](const std::string_view text) {
            append_u64(bytes, static_cast<std::uint64_t>(text.size()));
            bytes.insert(bytes.end(),
                reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size()));
        };
        append_text(prefix);
        append_text(domain);
        append_u64(bytes, parent);
        append_u64(bytes, high);
        append_u64(bytes, low);
        append_text(identity);
        append_u64(bytes, ordinal);
        const auto digest = support::Sha256::digest(bytes);
        std::uint64_t result { };
        for (std::size_t index = 0; index < 8U; ++index) {
            result |= static_cast<std::uint64_t>(digest[index]) << (index * 8U);
        }
        return result;
    }

    bool valid_limits(
        const ScvRandomLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_identity_bytes == 0U
            || limits.max_identity_bytes
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_domain_values == 0U
            || limits.max_domain_values
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_draws == 0U) {
            diagnostics.error(
                "FSIM-SCV-R003", "SCV random resource limits must be nonzero");
            return false;
        }
        return true;
    }

    bool valid_thread_identity(
        const std::string_view identity,
        const ScvRandomLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        const auto invalid = std::ranges::find_if(identity, [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte == 0x7fU || value == '\\';
        });
        if (identity.empty() || identity.size() > limits.max_identity_bytes
            || invalid != identity.end() || identity.front() == ' '
            || identity.back() == ' ') {
            diagnostics.error("FSIM-SCV-R001",
                "SCV random thread identity must be bounded canonical text");
            return false;
        }
        return true;
    }

    std::optional<std::pair<std::vector<ScvRandomWeightedValue>, std::uint64_t>>
    normalize_domain(
        const ScvRandomDomain& domain,
        const ScvRandomLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (!valid_limits(limits, diagnostics)) {
            return std::nullopt;
        }
        if (domain.values.empty()
            || domain.values.size() > limits.max_domain_values
            || domain.exclusions.size() > limits.max_domain_values) {
            diagnostics.error("FSIM-SCV-R003",
                "SCV random domain is empty or exceeds its value limit");
            return std::nullopt;
        }
        auto values = domain.values;
        std::ranges::sort(values, { }, &ScvRandomWeightedValue::value);
        if (std::ranges::adjacent_find(values, { },
                &ScvRandomWeightedValue::value)
            != values.end()) {
            diagnostics.error(
                "FSIM-SCV-R002", "SCV random domain contains duplicate values");
            return std::nullopt;
        }
        auto exclusions = domain.exclusions;
        std::ranges::sort(exclusions);
        exclusions.erase(
            std::unique(exclusions.begin(), exclusions.end()), exclusions.end());
        std::erase_if(values, [&](const ScvRandomWeightedValue& entry) {
            return std::ranges::binary_search(exclusions, entry.value);
        });
        std::uint64_t total { };
        for (const auto& entry : values) {
            if (entry.weight == 0U
                || total > std::numeric_limits<std::uint64_t>::max()
                        - entry.weight) {
                diagnostics.error("FSIM-SCV-R002",
                    "SCV random domain has a zero or overflowing weight");
                return std::nullopt;
            }
            total += entry.weight;
        }
        if (values.empty()) {
            diagnostics.error(
                "FSIM-SCV-R002", "SCV random exclusions remove the entire domain");
            return std::nullopt;
        }
        return std::pair { std::move(values), total };
    }

    std::optional<std::size_t> choose_weighted(
        const std::vector<ScvRandomWeightedValue>& values,
        ScvRandomStream& stream,
        diagnostic::Engine& diagnostics)
    {
        std::uint64_t total { };
        for (const auto& entry : values) {
            if (total > std::numeric_limits<std::uint64_t>::max() - entry.weight) {
                diagnostics.error(
                    "FSIM-SCV-R002", "SCV random bag weight total overflowed");
                return std::nullopt;
            }
            total += entry.weight;
        }
        const auto selected = stream.bounded(total, diagnostics);
        if (!selected) {
            return std::nullopt;
        }
        auto ticket = *selected;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (ticket < values[index].weight) {
                return index;
            }
            ticket -= values[index].weight;
        }
        diagnostics.error(
            "FSIM-SCV-R002", "SCV random weighted selection was inconsistent");
        return std::nullopt;
    }

} // namespace

std::optional<ScvRandomSeeds> derive_scv_random_seeds(
    const std::uint64_t root_seed,
    const ScvIslandId island,
    const ScvObjectId object,
    const std::string_view canonical_thread,
    const std::uint64_t thread_ordinal,
    const ScvRandomLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)
        || !valid_thread_identity(canonical_thread, limits, diagnostics)
        || !island.valid() || !object.valid()) {
        if (!island.valid() || !object.valid()) {
            diagnostics.error("FSIM-SCV-R001",
                "SCV random seed derivation requires valid island and object IDs");
        }
        return std::nullopt;
    }
    ScvRandomSeeds result;
    result.root = root_seed;
    result.island = digest_seed(
        "island", root_seed, island.high, island.low, { }, 0U);
    result.object = digest_seed(
        "object", result.island, object.high, object.low, { }, 0U);
    result.thread = digest_seed("thread", result.object, 0U, 0U,
        canonical_thread, thread_ordinal);
    return result;
}

ScvRandomStream::ScvRandomStream(
    const std::uint64_t seed,
    ScvRandomLimits limits)
    : seed_(seed)
    , state_(seed)
    , limits_(limits)
{
}

std::optional<std::uint64_t> ScvRandomStream::next(
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits_, diagnostics) || draws_ >= limits_.max_draws
        || draws_ == std::numeric_limits<std::uint64_t>::max()) {
        if (draws_ >= limits_.max_draws
            || draws_ == std::numeric_limits<std::uint64_t>::max()) {
            diagnostics.error(
                "FSIM-SCV-R003", "SCV random draw limit was exhausted");
        }
        return std::nullopt;
    }
    state_ += UINT64_C(0x9e3779b97f4a7c15);
    auto value = state_;
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    ++draws_;
    return value ^ (value >> 31U);
}

std::optional<std::uint64_t> ScvRandomStream::bounded(
    const std::uint64_t exclusive_bound,
    diagnostic::Engine& diagnostics)
{
    if (exclusive_bound == 0U) {
        diagnostics.error(
            "FSIM-SCV-R002", "SCV random bound must be nonzero");
        return std::nullopt;
    }
    const auto threshold = (std::uint64_t { 0U } - exclusive_bound)
        % exclusive_bound;
    while (true) {
        const auto value = next(diagnostics);
        if (!value) {
            return std::nullopt;
        }
        if (*value >= threshold) {
            return *value % exclusive_bound;
        }
    }
}

ScvRandomSnapshot ScvRandomStream::snapshot() const noexcept
{
    return { seed_, state_, draws_ };
}

bool ScvRandomStream::restore(
    const ScvRandomSnapshot& snapshot,
    diagnostic::Engine& diagnostics)
{
    constexpr std::uint64_t step = UINT64_C(0x9e3779b97f4a7c15);
    const auto expected_state = snapshot.seed + step * snapshot.draws;
    if (snapshot.seed != seed_ || snapshot.state != expected_state
        || snapshot.draws > limits_.max_draws) {
        diagnostics.error("FSIM-SCV-R003",
            "SCV random snapshot has a foreign seed, invalid state, or excessive "
            "draw count");
        return false;
    }
    state_ = snapshot.state;
    draws_ = snapshot.draws;
    return true;
}

std::optional<ScvRandomDistribution> ScvRandomDistribution::create(
    const ScvRandomDomain& domain,
    const ScvRandomLimits& limits,
    diagnostic::Engine& diagnostics)
{
    auto normalized = normalize_domain(domain, limits, diagnostics);
    if (!normalized) {
        return std::nullopt;
    }
    ScvRandomDistribution result;
    result.values_ = std::move(normalized->first);
    result.total_weight_ = normalized->second;
    return result;
}

std::optional<std::int64_t> ScvRandomDistribution::draw(
    ScvRandomStream& stream,
    diagnostic::Engine& diagnostics) const
{
    const auto ticket = stream.bounded(total_weight_, diagnostics);
    if (!ticket) {
        return std::nullopt;
    }
    auto remaining = *ticket;
    for (const auto& entry : values_) {
        if (remaining < entry.weight) {
            return entry.value;
        }
        remaining -= entry.weight;
    }
    diagnostics.error(
        "FSIM-SCV-R002", "SCV random distribution was inconsistent");
    return std::nullopt;
}

bool ScvRandomDistribution::contains(const std::int64_t value) const noexcept
{
    return std::ranges::binary_search(
        values_, value, { }, &ScvRandomWeightedValue::value);
}

const std::vector<ScvRandomWeightedValue>&
ScvRandomDistribution::values() const noexcept
{
    return values_;
}

std::optional<ScvRandomBag> ScvRandomBag::create(
    const ScvRandomDomain& domain,
    const ScvRandomLimits& limits,
    diagnostic::Engine& diagnostics)
{
    auto normalized = normalize_domain(domain, limits, diagnostics);
    if (!normalized) {
        return std::nullopt;
    }
    ScvRandomBag result;
    result.initial_ = std::move(normalized->first);
    result.remaining_ = result.initial_;
    result.limits_ = limits;
    return result;
}

std::optional<std::int64_t> ScvRandomBag::draw(
    ScvRandomStream& stream,
    diagnostic::Engine& diagnostics)
{
    if (remaining_.empty()) {
        diagnostics.error("FSIM-SCV-R002",
            "SCV random bag is empty and requires an explicit reset");
        return std::nullopt;
    }
    const auto selected = choose_weighted(remaining_, stream, diagnostics);
    if (!selected) {
        return std::nullopt;
    }
    const auto value = remaining_[*selected].value;
    remaining_.erase(remaining_.begin() + static_cast<std::ptrdiff_t>(*selected));
    return value;
}

bool ScvRandomBag::reset(diagnostic::Engine& diagnostics)
{
    if (cycle_ == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics.error(
            "FSIM-SCV-R003", "SCV random bag cycle limit was exhausted");
        return false;
    }
    remaining_ = initial_;
    ++cycle_;
    return true;
}

ScvRandomBagSnapshot ScvRandomBag::snapshot(
    const ScvRandomStream& stream) const
{
    ScvRandomBagSnapshot result;
    result.stream = stream.snapshot();
    result.cycle = cycle_;
    result.remaining.reserve(remaining_.size());
    for (const auto& entry : remaining_) {
        result.remaining.push_back(entry.value);
    }
    return result;
}

bool ScvRandomBag::restore(
    const ScvRandomBagSnapshot& snapshot,
    ScvRandomStream& stream,
    diagnostic::Engine& diagnostics)
{
    if (snapshot.remaining.size() > limits_.max_domain_values) {
        diagnostics.error(
            "FSIM-SCV-R003", "SCV random bag replay exceeds its value limit");
        return false;
    }
    if (!std::ranges::is_sorted(snapshot.remaining)
        || std::ranges::adjacent_find(snapshot.remaining)
            != snapshot.remaining.end()) {
        diagnostics.error("FSIM-SCV-R002",
            "SCV random bag replay values are not in canonical order");
        return false;
    }
    std::vector<ScvRandomWeightedValue> staged;
    staged.reserve(snapshot.remaining.size());
    for (const auto value : snapshot.remaining) {
        const auto found = std::ranges::lower_bound(
            initial_, value, { }, &ScvRandomWeightedValue::value);
        if (found == initial_.end() || found->value != value) {
            diagnostics.error("FSIM-SCV-R002",
                "SCV random bag replay contains an unknown or duplicate value");
            return false;
        }
        staged.push_back(*found);
    }
    if (!stream.restore(snapshot.stream, diagnostics)) {
        return false;
    }
    remaining_ = std::move(staged);
    cycle_ = snapshot.cycle;
    return true;
}

std::size_t ScvRandomBag::remaining() const noexcept
{
    return remaining_.size();
}

std::uint64_t ScvRandomBag::cycle() const noexcept
{
    return cycle_;
}

} // namespace fsim::systemc
