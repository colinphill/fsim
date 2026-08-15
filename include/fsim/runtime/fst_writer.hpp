// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/trace_model.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

namespace fsim::runtime {

class FstEncodedValue;

struct FstWriterLimits {
    std::size_t maximum_scopes { 1U << 20U };
    std::size_t maximum_signals { 1U << 20U };
    std::size_t maximum_events { 1U << 20U };
    std::size_t maximum_timestamps { 1U << 20U };
    std::size_t maximum_name_bytes { 1U << 20U };
    std::size_t maximum_type_metadata_bytes { 1U << 20U };
    std::size_t maximum_value_bytes { 1U << 20U };
    std::size_t maximum_hierarchy_bytes { 256U << 20U };
    std::size_t maximum_buffer_bytes { 512U << 20U };
    std::size_t maximum_container_bytes { 512U << 20U };
};

enum class FstWriterState : std::uint8_t {
    configuring,
    open,
    complete,
    failed
};

struct FstWriterStatus {
    FstWriterState state { FstWriterState::configuring };
    std::size_t buffered_bytes { };
    std::uint64_t bytes_written { };
    std::string failure;
};

enum class FstWriterCompression : std::uint8_t {
    None,
    Deterministic
};

/// Clean-room deterministic writer for fsim's bounded FST profile.
///
/// No bytes are published until close() has validated and assembled the
/// complete container. The initial unknown frame makes the declaration-only
/// profile independently readable; later stages replace it with typed values.
class FstWriter final {
public:
    explicit FstWriter(
        std::ostream& output,
        std::int8_t timescale_exponent = -9,
        FstWriterLimits limits = { },
        FstWriterCompression compression =
            FstWriterCompression::Deterministic);
    ~FstWriter();
    FstWriter(FstWriter&&) noexcept;
    FstWriter& operator=(FstWriter&&) noexcept;
    FstWriter(const FstWriter&) = delete;
    FstWriter& operator=(const FstWriter&) = delete;

    void declare(const TraceDeclarationModel& model);
    void begin(SimulationTick initial_time = 0);
    void set_initial_value(TraceSignalId signal, const FstEncodedValue& value);
    void change(const TraceEvent& event, const FstEncodedValue& value);
    void flush();
    void close(SimulationTick final_time);

    [[nodiscard]] bool begun() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] std::uint64_t bytes_written() const noexcept;
    [[nodiscard]] FstWriterStatus status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime
