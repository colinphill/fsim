// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <limits>
#include <tuple>

namespace fsim::runtime::simir {

void normalize_sensitivities(std::vector<Sensitivity>& sensitivities)
{
    for (auto& sensitivity : sensitivities) {
        if (sensitivity.width == 0U || sensitivity.edge != EdgeKind::any) {
            sensitivity.offset = 0U;
            sensitivity.width = 0U;
        }
    }
    std::ranges::sort(sensitivities, {}, [](const Sensitivity& sensitivity) {
        return std::tuple { sensitivity.signal, sensitivity.edge,
            sensitivity.offset, sensitivity.width };
    });
    std::size_t output { };
    for (const auto sensitivity : sensitivities) {
        if (output != 0U) {
            auto& previous = sensitivities[output - 1U];
            if (previous.signal == sensitivity.signal
                && previous.edge == sensitivity.edge) {
                if (previous.width == 0U)
                    continue;
                if (sensitivity.width == 0U) {
                    previous.offset = 0U;
                    previous.width = 0U;
                    continue;
                }
                const auto previous_end = std::uint64_t { previous.offset }
                    + previous.width;
                const auto end = std::uint64_t { sensitivity.offset }
                    + sensitivity.width;
                if (sensitivity.offset <= previous_end) {
                    const auto merged_width = std::max(previous_end, end)
                        - previous.offset;
                    if (merged_width <= std::numeric_limits<std::uint32_t>::max()) {
                        previous.width = static_cast<std::uint32_t>(merged_width);
                    } else {
                        previous.offset = 0U;
                        previous.width = 0U;
                    }
                    continue;
                }
            }
        }
        sensitivities[output++] = sensitivity;
    }
    sensitivities.resize(output);
}

bool sensitivity_range_changed(const PackedLogic4& previous,
    const PackedLogic4& current, std::uint32_t offset,
    std::uint32_t width) noexcept
{
    // Callers already established that the complete value changed. Unknown
    // dependency bounds must never suppress an otherwise observable event.
    if (width == 0U || previous.width() != current.width()
        || offset >= current.width()
        || width > current.width() - offset)
        return true;

    const auto end = std::uint64_t { offset } + width;
    if (previous.is_logic9() != current.is_logic9()) {
        for (std::uint64_t bit = offset; bit < end; ++bit) {
            if (previous.get_logic9(bit) != current.get_logic9(bit))
                return true;
        }
        return false;
    }

    const auto planes_differ = [&](std::span<const std::uint64_t> old_plane,
                                  std::span<const std::uint64_t> new_plane) {
        const auto first_word = offset / 64U;
        const auto last_word = (end - 1U) / 64U;
        if (last_word >= old_plane.size() || last_word >= new_plane.size())
            return true;
        for (auto word = std::uint64_t { first_word }; word <= last_word; ++word) {
            auto mask = std::numeric_limits<std::uint64_t>::max();
            if (word == first_word)
                mask <<= offset % 64U;
            if (word == last_word && end % 64U != 0U)
                mask &= (UINT64_C(1) << (end % 64U)) - UINT64_C(1);
            if (((old_plane[word] ^ new_plane[word]) & mask) != 0U)
                return true;
        }
        return false;
    };
    if (current.is_logic9()) {
        for (std::size_t plane = 0; plane < 4U; ++plane) {
            if (planes_differ(previous.logic9_plane_words(plane),
                    current.logic9_plane_words(plane)))
                return true;
        }
        return false;
    }
    return planes_differ(previous.aval_words(), current.aval_words())
        || planes_differ(previous.bval_words(), current.bval_words());
}

} // namespace fsim::runtime::simir
