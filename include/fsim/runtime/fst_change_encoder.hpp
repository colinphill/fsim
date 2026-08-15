// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/trace_model.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace fsim::runtime {

struct FstChangeEncoderLimits {
    std::size_t maximum_events { 1U << 20U };
    std::size_t maximum_timestamps { 1U << 20U };
    std::size_t maximum_payload_bytes { 256U << 20U };
};

struct FstValueChange {
    TraceEvent event;
    FstEncodedValue value;
};

class FstOrderedChanges final {
public:
    FstOrderedChanges(const FstOrderedChanges&) = default;
    FstOrderedChanges(FstOrderedChanges&&) noexcept = default;
    FstOrderedChanges& operator=(const FstOrderedChanges&) = default;
    FstOrderedChanges& operator=(FstOrderedChanges&&) noexcept = default;
    ~FstOrderedChanges();

    [[nodiscard]] std::span<const FstValueChange> values() const noexcept;
    [[nodiscard]] std::size_t timestamp_count() const noexcept;
    [[nodiscard]] std::size_t payload_bytes() const noexcept;

private:
    struct Impl;
    explicit FstOrderedChanges(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
    friend class FstChangeEncoder;
};

/// Bounded transaction that retains every accepted trace change and freezes it
/// in canonical (time, delta, region, sequence, stable-id) order.
class FstChangeEncoder final {
public:
    explicit FstChangeEncoder(FstChangeEncoderLimits limits = { });
    ~FstChangeEncoder();
    FstChangeEncoder(FstChangeEncoder&&) noexcept;
    FstChangeEncoder& operator=(FstChangeEncoder&&) noexcept;
    FstChangeEncoder(const FstChangeEncoder&) = delete;
    FstChangeEncoder& operator=(const FstChangeEncoder&) = delete;

    void append(const TraceEvent& event, const FstEncodedValue& value);
    [[nodiscard]] FstOrderedChanges freeze() &&;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime
