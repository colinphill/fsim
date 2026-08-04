// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal declaration fragment included by simir.hpp after common IDs.

struct ReadSignal {
  RegisterId destination{};
  SignalId signal{};
};

/// True only during the delta in which the signal most recently changed.
struct SignalEvent {
  RegisterId destination{};
  SignalId signal{};
};

/// The effective value immediately before the signal's most recent event.
struct SignalLastValue {
  RegisterId destination{};
  SignalId signal{};
};

/// Elapsed ticks since the signal's latest event, or TIME'HIGH if none.
struct SignalLastEvent {
  RegisterId destination{};
  SignalId signal{};
};

/// Capture the current global simulation tick in a 64-bit two-state value.
///
/// Unlike TimeDisplay this is an ordinary value-producing operation, so
/// language libraries can retain and compare timestamps without depending on
/// host formatting or an executor-specific side channel.
struct ReadSimulationTime {
  RegisterId destination{};
};

/// True during the delta following any committed signal transaction.
struct SignalActive {
  RegisterId destination{};
  SignalId signal{};
};

/// Elapsed ticks since the latest transaction, or TIME'HIGH if none.
struct SignalLastActive {
  RegisterId destination{};
  SignalId signal{};
};

/// True when the executing process owns a driver for the signal.
struct SignalDriving {
  RegisterId destination{};
  SignalId signal{};
};

/// The executing process's current driver contribution to the signal.
struct SignalDrivingValue {
  RegisterId destination{};
  SignalId signal{};
};
