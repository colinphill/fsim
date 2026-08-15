// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_change_encoder.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

    using EventIdentity = std::tuple<SimulationTick, std::uint64_t,
        std::uint8_t, std::uint64_t, std::uint64_t>;

    [[nodiscard]] std::size_t change_payload_bytes(
        const FstEncodedValue& value)
    {
        const auto payload = value.payload_kind() == FstPayloadKind::String
            ? value.string_bytes().size()
            : value.payload_kind() == FstPayloadKind::Symbols
            ? value.symbols().size()
            : sizeof(std::uint64_t);
        if (value.canonical_type().size()
            > std::numeric_limits<std::size_t>::max() - payload) {
            throw std::length_error("FST change payload size overflows");
        }
        return payload + value.canonical_type().size();
    }

    [[nodiscard]] EventIdentity event_identity(
        const TraceEvent& event) noexcept
    {
        return std::tuple {
            event.time,
            event.delta,
            static_cast<std::uint8_t>(event.region),
            event.sequence,
            event.signal.value
        };
    }

} // namespace

struct FstOrderedChanges::Impl {
    std::vector<FstValueChange> values;
    std::size_t timestamp_count { };
    std::size_t payload_bytes { };
};

struct FstChangeEncoder::Impl {
    explicit Impl(FstChangeEncoderLimits configured_limits)
        : limits(configured_limits)
    {
        if (limits.maximum_events == 0
            || limits.maximum_timestamps == 0
            || limits.maximum_payload_bytes == 0) {
            throw std::invalid_argument(
                "FST change encoder limits must be nonzero");
        }
    }

    FstChangeEncoderLimits limits;
    std::vector<FstValueChange> values;
    std::set<EventIdentity> event_identities;
    std::unordered_set<SimulationTick> timestamps;
    std::size_t payload_bytes { };
};

FstOrderedChanges::FstOrderedChanges(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl))
{
}

FstOrderedChanges::~FstOrderedChanges() = default;

std::span<const FstValueChange> FstOrderedChanges::values() const noexcept
{
    return impl_->values;
}

std::size_t FstOrderedChanges::timestamp_count() const noexcept
{
    return impl_->timestamp_count;
}

std::size_t FstOrderedChanges::payload_bytes() const noexcept
{
    return impl_->payload_bytes;
}

FstChangeEncoder::FstChangeEncoder(FstChangeEncoderLimits limits)
    : impl_(std::make_unique<Impl>(limits))
{
}

FstChangeEncoder::~FstChangeEncoder() = default;
FstChangeEncoder::FstChangeEncoder(FstChangeEncoder&&) noexcept = default;
FstChangeEncoder& FstChangeEncoder::operator=(
    FstChangeEncoder&&) noexcept = default;

void FstChangeEncoder::append(
    const TraceEvent& event,
    const FstEncodedValue& value)
{
    if (!impl_) {
        throw std::logic_error("FST change encoder was already frozen");
    }
    if (event.signal.value == 0) {
        throw std::invalid_argument("FST change lacks a stable signal identity");
    }
    if (impl_->values.size() == impl_->limits.maximum_events) {
        throw std::length_error("FST change count exceeds its limit");
    }
    const auto identity = event_identity(event);
    if (impl_->event_identities.contains(identity)) {
        throw std::invalid_argument(
            "FST change ordering identity is duplicated");
    }
    const auto adds_timestamp = !impl_->timestamps.contains(event.time);
    if (adds_timestamp
        && impl_->timestamps.size() == impl_->limits.maximum_timestamps) {
        throw std::length_error("FST timestamp count exceeds its limit");
    }
    const auto bytes = change_payload_bytes(value);
    if (bytes > impl_->limits.maximum_payload_bytes - impl_->payload_bytes) {
        throw std::length_error("FST change payload exceeds its byte limit");
    }
    impl_->values.push_back({ event, value });
    impl_->event_identities.insert(identity);
    if (adds_timestamp) {
        impl_->timestamps.insert(event.time);
    }
    impl_->payload_bytes += bytes;
}

FstOrderedChanges FstChangeEncoder::freeze() &&
{
    if (!impl_) {
        throw std::logic_error("FST change encoder was already frozen");
    }
    std::ranges::sort(impl_->values,
        [](const FstValueChange& left, const FstValueChange& right) {
            return trace_event_precedes(left.event, right.event);
        });
    auto frozen = std::make_shared<FstOrderedChanges::Impl>();
    frozen->values = std::move(impl_->values);
    frozen->timestamp_count = impl_->timestamps.size();
    frozen->payload_bytes = impl_->payload_bytes;
    impl_.reset();
    return FstOrderedChanges { std::move(frozen) };
}

} // namespace fsim::runtime
