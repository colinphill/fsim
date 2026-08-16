// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/systemc/scv_backend_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fsim::systemc {

struct ScvRandomLimits {
    std::size_t max_identity_bytes { 4096U };
    std::size_t max_domain_values { 65536U };
    std::uint64_t max_draws { 1024U * 1024U };
};

struct ScvRandomSeeds {
    std::uint64_t root { };
    std::uint64_t island { };
    std::uint64_t object { };
    std::uint64_t thread { };

    friend bool operator==(const ScvRandomSeeds&, const ScvRandomSeeds&) = default;
};

[[nodiscard]] std::optional<ScvRandomSeeds> derive_scv_random_seeds(
    std::uint64_t root_seed,
    ScvIslandId island,
    ScvObjectId object,
    std::string_view canonical_thread,
    std::uint64_t thread_ordinal,
    const ScvRandomLimits& limits,
    diagnostic::Engine& diagnostics);

struct ScvRandomSnapshot {
    std::uint64_t seed { };
    std::uint64_t state { };
    std::uint64_t draws { };

    friend bool operator==(
        const ScvRandomSnapshot&, const ScvRandomSnapshot&) = default;
};

class ScvRandomStream {
public:
    explicit ScvRandomStream(
        std::uint64_t seed,
        ScvRandomLimits limits = { });

    [[nodiscard]] std::optional<std::uint64_t> next(
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::optional<std::uint64_t> bounded(
        std::uint64_t exclusive_bound,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] ScvRandomSnapshot snapshot() const noexcept;
    [[nodiscard]] bool restore(
        const ScvRandomSnapshot& snapshot,
        diagnostic::Engine& diagnostics);

private:
    std::uint64_t seed_ { };
    std::uint64_t state_ { };
    std::uint64_t draws_ { };
    ScvRandomLimits limits_;
};

struct ScvRandomWeightedValue {
    std::int64_t value { };
    std::uint64_t weight { 1U };

    friend bool operator==(
        const ScvRandomWeightedValue&, const ScvRandomWeightedValue&) = default;
};

struct ScvRandomDomain {
    std::vector<ScvRandomWeightedValue> values;
    std::vector<std::int64_t> exclusions;
};

class ScvRandomDistribution {
public:
    [[nodiscard]] static std::optional<ScvRandomDistribution> create(
        const ScvRandomDomain& domain,
        const ScvRandomLimits& limits,
        diagnostic::Engine& diagnostics);

    [[nodiscard]] std::optional<std::int64_t> draw(
        ScvRandomStream& stream,
        diagnostic::Engine& diagnostics) const;
    [[nodiscard]] bool contains(std::int64_t value) const noexcept;
    [[nodiscard]] const std::vector<ScvRandomWeightedValue>& values() const noexcept;

private:
    std::vector<ScvRandomWeightedValue> values_;
    std::uint64_t total_weight_ { };
};

struct ScvRandomBagSnapshot {
    ScvRandomSnapshot stream;
    std::uint64_t cycle { };
    std::vector<std::int64_t> remaining;

    friend bool operator==(
        const ScvRandomBagSnapshot&, const ScvRandomBagSnapshot&) = default;
};

class ScvRandomBag {
public:
    [[nodiscard]] static std::optional<ScvRandomBag> create(
        const ScvRandomDomain& domain,
        const ScvRandomLimits& limits,
        diagnostic::Engine& diagnostics);

    [[nodiscard]] std::optional<std::int64_t> draw(
        ScvRandomStream& stream,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool reset(diagnostic::Engine& diagnostics);
    [[nodiscard]] ScvRandomBagSnapshot snapshot(
        const ScvRandomStream& stream) const;
    [[nodiscard]] bool restore(
        const ScvRandomBagSnapshot& snapshot,
        ScvRandomStream& stream,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] std::uint64_t cycle() const noexcept;

private:
    std::vector<ScvRandomWeightedValue> initial_;
    std::vector<ScvRandomWeightedValue> remaining_;
    std::uint64_t cycle_ { };
    ScvRandomLimits limits_;
};

} // namespace fsim::systemc
