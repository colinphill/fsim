// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime {

struct TransactionStableId {
    std::uint64_t high { };
    std::uint64_t low { };
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return high != 0U || low != 0U;
    }
    friend constexpr auto operator<=>(
        const TransactionStableId&, const TransactionStableId&) = default;
};

enum class TransactionRegion : std::uint8_t {
    initialize = 1,
    evaluate = 2,
    update = 3,
    notify = 4,
    quiescent = 5,
    postponed = 6,
};

enum class TransactionValueKind : std::uint8_t {
    boolean = 1,
    signed_integer = 2,
    unsigned_integer = 3,
    enumeration = 4,
    string = 5,
    bit_vector = 6,
    logic_vector = 7,
};

struct TransactionTypedValue {
    TransactionValueKind kind { TransactionValueKind::boolean };
    std::string nominal_type;
    std::size_t bit_width { 1U };
    bool signed_type { };
    std::string text;
    std::vector<std::uint64_t> aval_words;
    std::vector<std::uint64_t> bval_words;

    friend bool operator==(
        const TransactionTypedValue&, const TransactionTypedValue&) = default;
};

struct TransactionAttribute {
    std::string name;
    TransactionTypedValue value;
    friend bool operator==(
        const TransactionAttribute&, const TransactionAttribute&) = default;
};

struct TransactionRelation {
    std::string name;
    TransactionStableId target;
    friend bool operator==(
        const TransactionRelation&, const TransactionRelation&) = default;
};

enum class TransactionObjectDomain : std::uint8_t {
    systemc = 1,
    tlm1 = 2,
    tlm2 = 3,
};

struct TransactionCorrelatedObject {
    TransactionObjectDomain domain { TransactionObjectDomain::systemc };
    TransactionStableId object;
    friend auto operator<=>(
        const TransactionCorrelatedObject&,
        const TransactionCorrelatedObject&) = default;
};

inline constexpr std::uint32_t transaction_record_schema_version = 1U;

struct TransactionRecord {
    std::uint32_t schema { transaction_record_schema_version };
    TransactionStableId stream;
    TransactionStableId generator;
    TransactionStableId transaction;
    std::uint64_t begin_time_fs { };
    std::uint64_t begin_delta { };
    TransactionRegion begin_region { TransactionRegion::evaluate };
    std::uint64_t end_time_fs { };
    std::uint64_t end_delta { };
    TransactionRegion end_region { TransactionRegion::evaluate };
    std::vector<TransactionAttribute> attributes;
    std::vector<TransactionRelation> relations;
    std::vector<TransactionCorrelatedObject> correlated_objects;

    friend bool operator==(const TransactionRecord&, const TransactionRecord&) = default;
};

struct TransactionRecordLimits {
    std::size_t max_message_bytes { 4U * 1024U * 1024U };
    std::size_t max_string_bytes { 1024U * 1024U };
    std::size_t max_total_string_bytes { 2U * 1024U * 1024U };
    std::size_t max_value_bits { 1024U * 1024U };
    std::size_t max_total_words { 1024U * 1024U };
    std::size_t max_attributes { 65536U };
    std::size_t max_relations { 65536U };
    std::size_t max_correlated_objects { 65536U };
};

[[nodiscard]] bool validate_transaction_record(
    const TransactionRecord& record,
    const TransactionRecordLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] bool transaction_record_precedes(
    const TransactionRecord& left,
    const TransactionRecord& right) noexcept;
[[nodiscard]] std::optional<std::vector<std::byte>> serialize_transaction_record(
    const TransactionRecord& record,
    const TransactionRecordLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<TransactionRecord> deserialize_transaction_record(
    std::span<const std::byte> bytes,
    const TransactionRecordLimits& limits,
    diagnostic::Engine& diagnostics);

} // namespace fsim::runtime
