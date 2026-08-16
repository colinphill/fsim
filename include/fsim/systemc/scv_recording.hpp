// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/runtime/transaction_record.hpp"
#include "fsim/systemc/scv_backend_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fsim::systemc {

struct ScvNativeRecordingLimits {
    std::size_t max_streams { 1024U };
    std::size_t max_generators { 4096U };
    std::size_t max_handles { 65536U };
    std::size_t max_completed_records { 65536U };
    std::size_t max_attributes_per_transaction { 1024U };
    std::size_t max_relations_per_transaction { 1024U };
    std::size_t max_name_bytes { 4096U };
    runtime::TransactionRecordLimits record_limits;
};

struct ScvNativeTransactionCoordinate {
    std::uint64_t delta { };
    runtime::TransactionRegion region { runtime::TransactionRegion::evaluate };
};

class ScvNativeRecordingRegistry {
public:
    ScvNativeRecordingRegistry(
        ScvIslandId island, std::string database_name,
        ScvNativeRecordingLimits limits = { });
    ~ScvNativeRecordingRegistry();
    ScvNativeRecordingRegistry(ScvNativeRecordingRegistry&&) noexcept;
    ScvNativeRecordingRegistry& operator=(
        ScvNativeRecordingRegistry&&) noexcept;
    ScvNativeRecordingRegistry(const ScvNativeRecordingRegistry&) = delete;
    ScvNativeRecordingRegistry& operator=(
        const ScvNativeRecordingRegistry&) = delete;

    [[nodiscard]] bool valid(diagnostic::Engine& diagnostics) const;
    [[nodiscard]] bool create_stream(
        ScvStreamId stream, std::string name, std::string kind,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool create_generator(
        ScvGeneratorId generator, ScvStreamId stream, std::string name,
        std::string begin_attribute_name, std::string end_attribute_name,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool set_recording(
        bool enabled, diagnostic::Engine& diagnostics);
    [[nodiscard]] bool begin_transaction(
        ScvTransactionId transaction, ScvGeneratorId generator,
        std::int64_t begin_value, ScvNativeTransactionCoordinate coordinate,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool record_attribute(
        ScvTransactionId transaction, std::string name,
        const runtime::TransactionTypedValue& value,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool relate_transaction(
        ScvTransactionId transaction, std::string relation,
        ScvTransactionId target, diagnostic::Engine& diagnostics);
    [[nodiscard]] bool end_transaction(
        ScvTransactionId transaction, std::int64_t end_value,
        ScvNativeTransactionCoordinate coordinate,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool release_transaction(
        ScvTransactionId transaction, diagnostic::Engine& diagnostics);

    [[nodiscard]] std::size_t live_streams() const noexcept;
    [[nodiscard]] std::size_t live_generators() const noexcept;
    [[nodiscard]] std::size_t live_handles() const noexcept;
    [[nodiscard]] std::size_t native_callback_count() const noexcept;
    [[nodiscard]] const std::vector<runtime::TransactionRecord>& records() const;
    [[nodiscard]] std::vector<runtime::TransactionRecord> take_records();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::systemc
