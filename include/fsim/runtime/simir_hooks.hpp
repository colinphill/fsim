// SPDX-License-Identifier: Apache-2.0
#pragma once

// The observer and service callbacks an Interpreter takes, for headers that
// name them without needing the IR itself (Interpreter's member aliases name
// the same types).

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir_ids.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime::simir {

struct ExecutionPoint;
struct SourceLocation;
struct VcdControlEvent;
struct DpiSitePlan;
class DpiFrame;
enum class AssertionSeverity : std::uint8_t;

/// A mailbox or semaphore, by the handle a variable of its class holds
/// (IEEE 1800-2023 15.3, 15.4), as the VPI reads it (37.33 details 3 and
/// 4): the messages a mailbox holds, oldest first, and the processes
/// waiting on the object (a mailbox's readers, then its writers).
struct SynchronizationObject {
    bool mailbox { };
    bool string_messages { };
    std::vector<PackedLogic4> messages;
    std::vector<std::string> string_values;
    std::vector<ProcessId> waiting;
};

/// Observes a process's force or release of a signal slice when the
/// statement executes, before a VHDL update applies it (IEEE 1076-2019
/// 10.5.2.1, 21.3.2.3-4). The width is in bits from the offset.
using ForceStatementHook = std::function<void(
    ProcessId, SignalId, std::size_t, std::size_t, bool)>;
/// Observes a signal becoming active: every transaction, with or without
/// an event (IEEE 1076-2019 14.7.3.1, 21.3.2.5).
using SignalTransactionHook = std::function<void(SignalId)>;
/// Observes drivers acquiring the value of a matured transaction, with
/// or without a change: the process that holds them, the signal they
/// drive and the bits of it the transaction is on (offset and width; a
/// driver is one scalar subelement's, IEEE 1076-2019 14.7.2, 21.3.2.5).
/// Called once the update phase has updated the drivers' values;
/// composite-signal families that are updated as a whole are reported
/// through DriverChangeHook only.
using DriverTransactionHook = std::function<void(
    ProcessId, SignalId, std::uint32_t offset, std::uint32_t width)>;
/// Observes a change of the value of a watched debug local (a variable
/// of a process, watch_debug_local) by a variable assignment, the
/// association of an out or inout formal, or a deposit or force (IEEE
/// 1076-2019 21.3.2.2): the process and the local's index. Called once
/// the local holds its new value.
using DebugLocalChangeHook = std::function<void(ProcessId, std::size_t)>;
using ExecutionPointHook = std::function<void(Scheduler&, const ExecutionPoint&)>;
using OutputHook = std::function<void(
    ProcessId,
    std::string_view,
    bool,
    SimulationTick,
    std::uint64_t)>;
using ReportHook = std::function<void(
    ProcessId,
    std::string_view,
    AssertionSeverity,
    const SourceLocation&,
    SimulationTick,
    std::uint64_t)>;
using SystemCommandHook = std::function<std::int32_t(
    std::optional<std::string_view>)>;
using VcdControlHook = std::function<void(const VcdControlEvent&)>;
/// Execute one imported SystemVerilog DPI function. The identity is the
/// declaration's C linkage name; mutable actual vectors carry output,
/// inout, and ref copy-out values using the same representation as class
/// call boundaries.
using DpiFunctionCallHook = std::function<PackedLogic4(
    std::string_view,
    std::vector<PackedLogic4>&,
    std::vector<std::string>&,
    std::span<const std::string>,
    std::span<const std::uint8_t>)>;

/// The imported DPI call sites bound once. `bind` takes the identity a
/// DpiFunctionCallHook would take and returns an opaque site (the C
/// symbol, call interface and caller context resolved), or null when the
/// service does not serve the identity as a plain import call; the
/// reference interpreter binds each call operation on its first run and
/// every later run calls through the site, with no identity parsing. The
/// static kernel numbers its call sites when its image is built and
/// binds them once at load (`bind_plan`).
struct DpiSiteHooks {
    std::function<const void*(std::string_view)> bind;
    std::function<PackedLogic4(const void*, std::vector<PackedLogic4>&,
        std::vector<std::string>&)>
        call;
    /// A kernel image's call sites (DpiSitePlan): `bind_plan` binds one
    /// to the loaded C symbol once, at load, and returns the site (null
    /// when the service does not serve the identity as an import call;
    /// it throws as `bind` does for an import without an implementation).
    /// `planned` tells whether the site calls through its plan, which
    /// `call_plan` then does, reading the arguments from the frame's
    /// registers into the C argument buffer and writing the copy-out
    /// values and the result back; every other site takes `call`.
    std::function<const void*(const DpiSitePlan&)> bind_plan;
    std::function<bool(const void*)> planned;
    std::function<PackedLogic4(const void*, const DpiSitePlan&, DpiFrame&)>
        call_plan;
};

} // namespace fsim::runtime::simir
