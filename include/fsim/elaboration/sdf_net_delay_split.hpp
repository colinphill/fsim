// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fsim::elaboration {

struct SdfNetDelaySplitResult {
    /// The load ports whose net was split and now carry a delay.
    std::size_t split { };
    /// One reason for each annotated load that was left undelayed.
    std::vector<std::string> skipped;
    /// The processes whose reads were renamed to a copy.
    std::vector<runtime::simir::ProcessId> rewritten;
    /// One note for each INTERCONNECT whose source port drives nothing on its
    /// load's net: the delay is annotated anyway, as the delay from all
    /// sources, like a PORT delay (IEEE 1364-2005 16.2.4).
    std::vector<std::string> from_all_sources;
    /// The processes that also write a shadow signal (a dual write) or were
    /// added as copies of a port connection process; an existing process
    /// whose operation list grows cannot be grafted into a running design.
    std::size_t dual_written { };
};

/// Makes the INTERCONNECT and PORT delays of `state.sdf_net_delays` delay the
/// net in simulation (IEEE 1364-2005 16 SDF back annotation, IEEE
/// 1800-2023 38.10, 32). Port connections share one signal between the driver
/// side and the instance behind the port, so each annotated load port gets a
/// signal of its own: a copy of the net that a new process writes with the
/// delay's twelve transition delays and their pulse limits (WriteNetDelay,
/// IEEE 1364-2005 16.2.4), and the
/// processes of the load's instance read the copy instead of the net. Only
/// the load side sees the delayed value. A design without an annotated port
/// is not changed. Appends signals and processes, so existing ids stay valid,
/// and resets the signal driver inventory.
SdfNetDelaySplitResult split_sdf_net_delays(ElaboratedDesignState& state);

/// The table of a copy driver's WriteNetDelay: twelve transition delays with
/// the pulse reject and error limit of each, in the order of a module path
/// (IEEE 1800-2023 30.5.1 Table 30-2). `reject` and `error` are empty or
/// twelve long; a limit that is not given equals its delay (the error limit
/// the reject limit), and no limit exceeds its delay or the error limit
/// falls below the reject limit.
[[nodiscard]] runtime::simir::NetDelayTable sdf_net_delay_table(
    std::span<const runtime::SimulationTick> delays,
    std::span<const runtime::SimulationTick> reject,
    std::span<const runtime::SimulationTick> error);

/// The tables a running simulation of `running` takes to follow a live SDF
/// reannotation whose design is `annotated` (both built from one baseline,
/// `annotated` by split_sdf_net_delays): the table of each copy driver's
/// WriteNetDelay in `annotated`, to give the driver of the same process in
/// the running design (the later transitions use it, pending ones keep their
/// time). The running design has handed its processes to the interpreter, so
/// it is compared by its records (the delayed loads with their copy signal
/// and driver); the tables come from the processes of `annotated`. Empty when
/// neither design has an interconnect or port delay.
/// Nothing when the reannotation needs another split than the running design
/// has: a load that gains, loses or regroups its delayed copy (its signals,
/// processes or write operations differ), which a running design cannot take
/// without new processes.
[[nodiscard]] std::optional<
    std::vector<std::pair<runtime::simir::ProcessId, runtime::simir::NetDelayTable>>>
sdf_net_delay_retiming(
    const ElaboratedDesign& running, const ElaboratedDesign& annotated);

/// One more load port split in a running design: `grown` is the running
/// design with the port's net split like split_sdf_net_delays does at
/// elaboration, and `graft` what the running simulation adds to take it.
struct SdfNetDelayGraft {
    std::optional<ElaboratedDesign> grown;
    runtime::simir::NetDelayGraft graft;
    /// The copy driver of `record` in `grown` (the first run of bits).
    runtime::simir::ProcessId driver { };
};

/// Plans the split of the one load port that `record` names (a PORT record
/// of the unsplit port, with the twelve delays and pulse limits the first
/// vpi_put_delays or acc_replace_delays on it gave) in `running`, a design
/// that may already split other ports. Only the instance's own processes
/// change and their operation lists keep their length, so the plan can be
/// applied to a suspended run (Interpreter::graft_net_delay); a port that
/// needs a dual write (drivers with different delays) cannot arise from one
/// PORT record. Nothing, with `reason` set, when the split does not cover the
/// port (no process of the instance reads it, or one writes it).
/// A design that gave its processes to the interpreter
/// (ElaboratedDesign::create_interpreter &&) has none left: the split then
/// reads its `process_count` processes from `running_program` (the
/// interpreter's programs as elaborated).
[[nodiscard]] std::optional<SdfNetDelayGraft> plan_sdf_net_delay_graft(
    const ElaboratedDesign& running, SdfNetDelayInfo record,
    std::size_t process_count,
    const std::function<const runtime::simir::Process&(
        runtime::simir::ProcessId)>& running_program,
    std::string& reason);

} // namespace fsim::elaboration
