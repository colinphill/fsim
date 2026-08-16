// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_protocol.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

int main()
{
    using namespace fsim::systemc;

    ScvBackendProtocolLimits limits;
    fsim::diagnostic::Engine diagnostics;
    const auto island = make_scv_island_id(
        "application:island", limits, diagnostics);
    assert(island && !diagnostics.has_error());

    const auto message = [&](const std::uint64_t time_fs,
                             const std::uint64_t delta,
                             const ScvBackendRegion region,
                             const std::uint64_t sequence) {
        ScvBackendMessage result;
        result.header.operation = ScvBackendOperation::flush;
        result.header.direction = ScvBackendDirection::event;
        result.header.island = *island;
        result.header.sequence = ScvSequenceId { sequence };
        result.header.time_fs = time_fs;
        result.header.delta = delta;
        result.header.region = region;
        result.payload = { std::byte { static_cast<unsigned char>(sequence) } };
        return result;
    };

    std::vector messages {
        message(20U, 0U, ScvBackendRegion::evaluate, 5U),
        message(10U, 1U, ScvBackendRegion::update, 4U),
        message(10U, 1U, ScvBackendRegion::evaluate, 9U),
        message(10U, 1U, ScvBackendRegion::evaluate, 3U),
        message(10U, 0U, ScvBackendRegion::postponed, 7U)
    };
    std::ranges::sort(messages, [](const auto& left, const auto& right) {
        return scv_backend_message_precedes(left.header, right.header);
    });
    constexpr std::array expected_sequences { 7U, 3U, 9U, 4U, 5U };
    for (std::size_t index = 0; index < messages.size(); ++index) {
        assert(messages[index].header.sequence.value == expected_sequences[index]);
        const auto bytes = serialize_scv_backend_message(
            messages[index], limits, diagnostics);
        assert(bytes);
        const auto decoded = deserialize_scv_backend_message(
            *bytes, limits, diagnostics);
        assert(decoded == messages[index]);
    }
    assert(!diagnostics.has_error());
}
