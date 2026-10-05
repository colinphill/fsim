// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

namespace fsim::app::application_detail {

struct DirectReadPlaneBufferIdentity {
    const void* data { };
    std::size_t size { };

    friend bool operator==(
        const DirectReadPlaneBufferIdentity&,
        const DirectReadPlaneBufferIdentity&) = default;
};

struct DirectReadPlaneLayoutKey {
    // Narrow Logic4/Logic9 planes, wide Logic4/Logic9 planes, and wide offsets.
    std::array<DirectReadPlaneBufferIdentity, 11> buffers { };

    friend bool operator==(
        const DirectReadPlaneLayoutKey&,
        const DirectReadPlaneLayoutKey&) = default;
};

/// Reuses the host-owned per-resume fallback map only when the context
/// promises a stable capability epoch and all direct-plane storage identities
/// are unchanged. A zero owner or epoch means the context is dynamic.
class DirectReadMapCache {
public:
    template <typename SupportsSignal>
    [[nodiscard]] bool refresh(
        const std::span<const std::uint32_t> static_signals,
        const std::span<std::uint32_t> runtime_signals,
        const runtime::simir::DirectSignalReadCapabilityKey capability,
        const DirectReadPlaneLayoutKey& plane_layout,
        const bool plane_layout_valid,
        const std::size_t direct_signal_count,
        SupportsSignal&& supports_signal)
    {
        if (static_signals.size() != runtime_signals.size()) {
            valid_ = false;
            all_supported_ = false;
            return false;
        }

        const bool cacheable = capability.cacheable();
        if (cacheable && valid_
            && capability == capability_
            && plane_layout == plane_layout_
            && static_signals.data() == static_signals_
            && static_signals.size() == static_signal_count_
            && runtime_signals.data() == runtime_signals_
            && plane_layout_valid == plane_layout_valid_
            && direct_signal_count == direct_signal_count_) {
            return all_supported_;
        }

        valid_ = false;
        all_supported_ = plane_layout_valid;
        constexpr auto unsupported = std::numeric_limits<std::uint32_t>::max();
        for (std::size_t slot = 0U; slot < static_signals.size(); ++slot) {
            const auto signal = static_signals[slot];
            const bool supported = plane_layout_valid
                && signal < direct_signal_count
                && std::invoke(supports_signal, signal);
            runtime_signals[slot] = supported ? signal : unsupported;
            all_supported_ = all_supported_ && supported;
        }

        if (cacheable) {
            capability_ = capability;
            plane_layout_ = plane_layout;
            static_signals_ = static_signals.data();
            static_signal_count_ = static_signals.size();
            runtime_signals_ = runtime_signals.data();
            plane_layout_valid_ = plane_layout_valid;
            direct_signal_count_ = direct_signal_count;
            valid_ = true;
        }
        return all_supported_;
    }

private:
    runtime::simir::DirectSignalReadCapabilityKey capability_ { };
    DirectReadPlaneLayoutKey plane_layout_ { };
    const std::uint32_t* static_signals_ { };
    std::size_t static_signal_count_ { };
    std::uint32_t* runtime_signals_ { };
    bool plane_layout_valid_ { };
    std::size_t direct_signal_count_ { };
    bool valid_ { };
    bool all_supported_ { };
};

} // namespace fsim::app::application_detail
