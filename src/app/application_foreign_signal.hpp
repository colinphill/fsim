// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::app::application_detail {

/// Traps an operating-system signal for a foreign-interface callback: the
/// handler only records the signal, and the owner delivers it at its next
/// safe point (IEEE 1800-2023 38.36.3 cbSignal, IEEE 1076-2019 21.3.7.10
/// vhpiCbSigInterrupt). While any trap of a signal exists, it replaces the
/// tool's own handling of that signal; the last trap restores it.
class ForeignSignalTrap final {
public:
    explicit ForeignSignalTrap(int signal) noexcept;
    ~ForeignSignalTrap();
    ForeignSignalTrap(const ForeignSignalTrap&) = delete;
    ForeignSignalTrap& operator=(const ForeignSignalTrap&) = delete;

    /// The signal can be trapped and the handler is installed.
    [[nodiscard]] bool valid() const noexcept { return valid_; }

    /// Whether the signal has arrived since the last call.
    [[nodiscard]] bool take() noexcept;

private:
    int signal_ { };
    bool valid_ { };
    std::uint64_t seen_ { };
};

} // namespace fsim::app::application_detail
