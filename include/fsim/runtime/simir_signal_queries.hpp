// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal declaration fragment included by simir.hpp after common IDs.

enum class SignalReadKind : std::uint8_t {
    current,
    sampled,
    rose,
    fell,
    stable,
    changed,
    past,
    future,
    rising,
    falling,
    steady,
    changing,
};

enum class SampledClockEdge : std::uint8_t {
    any,
    positive,
    negative,
};

struct ReadSignal {
    RegisterId destination { };
    SignalId signal { };
    SignalReadKind kind { SignalReadKind::current };
    std::uint32_t ticks { 1 };
    std::optional<SignalId> clock { };
    SampledClockEdge clock_edge { SampledClockEdge::any };
    std::optional<SignalId> gate { };
};

/// A statically selected packed subelement of a composite signal.
struct SignalSelection {
    std::uint32_t offset { };
    std::uint32_t width { };
    // An element read of a whole composite signal's 'LAST_VALUE: an event on
    // the signal updates the element's last value whenever the element is
    // assigned, changed or not (IEEE 1076-2008 16.2.4). A selected prefix
    // (`s(i)'LAST_VALUE`) counts only events on that element.
    bool composite_element { };

    friend bool operator==(const SignalSelection&, const SignalSelection&)
        = default;
};

/// True only during the delta in which the signal (or selected range) changed.
struct SignalEvent {
    RegisterId destination { };
    SignalId signal { };
    std::optional<SignalSelection> selection { };
};

/// The effective value immediately before the signal's (or selected range's)
/// most recent event.
struct SignalLastValue {
    RegisterId destination { };
    SignalId signal { };
    std::optional<SignalSelection> selection { };
};

/// Elapsed ticks since the signal's (or selected range's) latest event,
/// or TIME'HIGH if none.
struct SignalLastEvent {
    RegisterId destination { };
    SignalId signal { };
    std::optional<SignalSelection> selection { };
};

/// Capture the current global simulation tick in a 64-bit two-state value.
///
/// Unlike TimeDisplay this is an ordinary value-producing operation, so
/// language libraries can retain and compare timestamps without depending on
/// host formatting or an executor-specific side channel.
struct ReadSimulationTime {
    RegisterId destination { };
};

/// True during the delta following a transaction on the signal or selected
/// range.
struct SignalActive {
    RegisterId destination { };
    SignalId signal { };
    std::optional<SignalSelection> selection { };
};

/// Elapsed ticks since the latest transaction on the signal or selected
/// range, or TIME'HIGH if none.
struct SignalLastActive {
    RegisterId destination { };
    SignalId signal { };
    std::optional<SignalSelection> selection { };
};

/// True when the executing process owns a driver for the signal or selected
/// range.
struct SignalDriving {
    RegisterId destination { };
    SignalId signal { };
    std::optional<SignalSelection> selection { };
};

/// The executing process's current driver contribution to the signal or
/// selected range.
struct SignalDrivingValue {
    RegisterId destination { };
    SignalId signal { };
    std::optional<SignalSelection> selection { };
};
