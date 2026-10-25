// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static kernel planning (docs/simulation-engine-v4-plan.md).
// Members are SystemVerilog processes with a static shape:
//
// - combinational: one any-change static wait and a loop jump;
// - sequential: one posedge/negedge static wait at the top and a loop jump;
// - once: no sensitivity, a terminating halt.
//
// Their bodies may use only the operations the kernel evaluates. A signal is
// kernel-owned when every writer is a member and nothing outside the members
// can observe it except through ordinary reads, which make it a boundary
// output. Memories are kernel-owned when every accessor is a member.
#include "fsim/elaboration/elaborator.hpp"
#include "elaborated_design_process_access.hpp"
#include "../runtime/simir_region_graph_bindings.hpp"
#include "../runtime/simir_region_graph_program_access.hpp"
#include "../runtime/simir_static_kernel.hpp"

#include <algorithm>
#include <cstdlib>
#include <cxxabi.h>
#include <typeinfo>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <string>
#include <string_view>
#include <limits>
#include <optional>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

void collect_container_objects(
    const Operation& operation, std::vector<ContainerObjectId>& objects)
{
    visit_operation([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ReadContainerObject>
            || std::is_same_v<T, WriteContainerObject>
            || std::is_same_v<T, WriteContainerObjectElement>) {
            objects.push_back(value.object);
        } else if constexpr (std::is_same_v<T, PlaEvaluate>
            || std::is_same_v<T, WaitPla>) {
            objects.push_back(value.memory);
        }
    }, operation);
}

/// The operation's type name, for diagnostics.
[[nodiscard]] std::string operation_type_name(const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        int status = 0;
        char* name = abi::__cxa_demangle(
            typeid(T).name(), nullptr, nullptr, &status);
        std::string text = name != nullptr ? name : "?";
        std::free(name);
        if (const auto colon = text.rfind("::"); colon != std::string::npos) {
            text = text.substr(colon + 2U);
        }
        return text;
    }, operation);
}

[[nodiscard]] std::optional<InstructionIndex> branch_target_outside(
    const Operation& operation, const std::size_t count)
{
    return visit_operation([&](const auto& value)
        -> std::optional<InstructionIndex> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Jump>) {
            return value.target >= count ? std::optional { value.target }
                                         : std::nullopt;
        } else if constexpr (std::is_same_v<T, Branch>) {
            if (value.when_true >= count) {
                return value.when_true;
            }
            return value.when_false >= count ? std::optional { value.when_false }
                                             : std::nullopt;
        } else {
            return std::nullopt;
        }
    }, operation);
}

/// Whether a fork child of a program can hold an automatic callable frame
/// across a suspension. An operation that returns a behavioral thread to its
/// runner (a wait, fork, join, halt, $stop or $pause) reachable from a Call
/// target before its Return, or from a frame push before the pop of the same
/// callable, makes that callable suspending. A SystemVerilog function never
/// is (IEEE 1800-2023 13.4: no timing controls in a function); a task may
/// (13.3). The hazard is a fork child entering a suspending callable, or a
/// fork the parent does not join (join_any, join_none) while a suspending
/// callable exists: only then may two threads of the member be inside one
/// callable's registers with one of them suspended.
template <typename Operations>
[[nodiscard]] bool fork_holds_suspended_frame(
    const Operations& operations, const std::size_t count)
{
    struct Entry {
        InstructionIndex pc { };
        std::optional<std::uint32_t> frame;
        bool suspends { };
        std::vector<std::size_t> callees;
    };
    std::vector<Entry> entries;
    std::unordered_map<InstructionIndex, std::size_t> call_entry;
    std::unordered_map<std::uint32_t, std::size_t> frame_entry;
    std::vector<InstructionIndex> branches;
    bool unjoined_fork = false;
    for (std::size_t op = 0U; op < count; ++op) {
        const auto operation = operations.expanded(op);
        if (const auto* call = operation_get_if<Call>(&operation)) {
            if (call_entry.try_emplace(call->target, entries.size()).second) {
                entries.push_back({ call->target, std::nullopt, false, { } });
            }
        } else if (const auto* push
                   = operation_get_if<CallableFramePush>(&operation)) {
            if (frame_entry.try_emplace(push->identity, entries.size()).second) {
                entries.push_back({ static_cast<InstructionIndex>(op + 1U),
                    push->identity, false, { } });
            }
        } else if (const auto* fork = operation_get_if<Fork>(&operation)) {
            branches.insert(branches.end(), fork->branches.begin(),
                fork->branches.end());
            unjoined_fork = unjoined_fork
                || fork->join != runtime::simir::ForkJoinKind::all;
        }
    }
    if (entries.empty() || branches.empty()) {
        return false;
    }
    // One callable's own operations: a Call continues at its return target
    // (the callee is its own entry), a Return or the matching pop ends it.
    std::vector<std::uint32_t> seen(count, 0U);
    std::uint32_t epoch = 0U;
    const auto walk = [&](const InstructionIndex begin,
                          const std::optional<std::uint32_t> frame,
                          const bool branch, const auto& visit) {
        ++epoch;
        std::vector<InstructionIndex> work { begin };
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            if (pc >= count || seen[pc] == epoch) {
                continue;
            }
            seen[pc] = epoch;
            const auto operation = operations.expanded(pc);
            if (frame) {
                if (const auto* pop
                    = operation_get_if<CallableFramePop>(&operation);
                    pop != nullptr && pop->identity == *frame) {
                    continue;
                }
            }
            if (visit(operation)) {
                return true;
            }
            if (operation_holds<Return>(operation)
                || (branch && operation_holds<ForkEnd>(operation))) {
                continue;
            }
            if (const auto* jump = operation_get_if<Jump>(&operation)) {
                work.push_back(jump->target);
            } else if (const auto* branch_op
                       = operation_get_if<Branch>(&operation)) {
                work.push_back(branch_op->when_true);
                work.push_back(branch_op->when_false);
            } else if (const auto* call = operation_get_if<Call>(&operation)) {
                work.push_back(call->return_target);
            } else {
                work.push_back(static_cast<InstructionIndex>(pc + 1U));
            }
        }
        return false;
    };
    for (auto& entry : entries) {
        entry.suspends = walk(entry.pc, entry.frame, false,
            [&](const auto& operation) {
                if (const auto* call = operation_get_if<Call>(&operation)) {
                    entry.callees.push_back(call_entry.at(call->target));
                }
                return operation_holds<WaitFor>(operation)
                    || operation_holds<WaitOn>(operation)
                    || operation_holds<WaitForever>(operation)
                    || operation_holds<WaitSensitivity>(operation)
                    || operation_holds<Fork>(operation)
                    || operation_holds<ForkEnd>(operation)
                    || operation_holds<Halt>(operation)
                    || operation_holds<Stop>(operation)
                    || operation_holds<Pause>(operation);
            });
    }
    // A callable calling a suspending callable suspends.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto& entry : entries) {
            if (!entry.suspends
                && std::ranges::any_of(entry.callees, [&](const std::size_t callee) {
                       return entries[callee].suspends;
                   })) {
                entry.suspends = true;
                changed = true;
            }
        }
    }
    if (std::ranges::none_of(entries, &Entry::suspends)) {
        return false;
    }
    if (unjoined_fork) {
        return true;
    }
    return std::ranges::any_of(branches, [&](const InstructionIndex begin) {
        return walk(begin, std::nullopt, true, [&](const auto& operation) {
            if (const auto* call = operation_get_if<Call>(&operation)) {
                return entries[call_entry.at(call->target)].suspends;
            }
            if (const auto* push
                = operation_get_if<CallableFramePush>(&operation)) {
                return entries[frame_entry.at(push->identity)].suspends;
            }
            return false;
        });
    });
}

struct Candidate {
    ProcessId process { };
    std::uint8_t kind { };
    bool run_at_start { };
    bool vhdl { };
    /// Behavioral members: starts one delta after time zero's other
    /// processes, as the scheduler starts the same program.
    bool start_late { };
    std::uint32_t body_begin { };
    std::uint32_t body_end { };
    std::uint32_t exit_alt { std::numeric_limits<std::uint32_t>::max() };
    std::vector<Sensitivity> sensitivity;
    /// Behavioral members: signals named by their dynamic waits.
    std::vector<SignalId> waited;
};

/// Candidate::kind of a behavioral member (StaticKernelMemberKind).
constexpr std::uint8_t behavioral_kind = 3U;

} // namespace

namespace {

// The host of a kernel whose processes are all hosted (no member to replace):
// the stub alone, without the driver regions, region flags or startup role of
// the hosted process it is shaped from.
runtime::simir::Process synthetic_host(runtime::simir::Process stub)
{
    runtime::simir::Process host;
    host.name = "$static_kernel";
    host.language_standard = std::move(stub.language_standard);
    host.compatibility_profile = std::move(stub.compatibility_profile);
    host.static_sensitivity = std::move(stub.static_sensitivity);
    host.operations = std::move(stub.operations);
    host.scheduling_domain = stub.scheduling_domain;
    return host;
}

/// The program the scheduler runs for the kernel's host process: it wakes
/// on the boundary inputs and activates the kernel.
runtime::simir::Process host_stub(const runtime::simir::ProcessProgramView& host,
    std::vector<runtime::simir::Sensitivity> sensitivity)
{
    using namespace runtime::simir;
    auto stub = host.materialize();
    stub.static_sensitivity = std::move(sensitivity);
    stub.debug_locals.clear();
    stub.debug_string_locals.clear();
    stub.debug_container_locals.clear();
    stub.expression_profiles = { };
    stub.register_value_kinds = { };
    stub.static_trigger_regions = { };
    stub.container_register_types = { };
    stub.register_count = 0U;
    stub.string_register_count = 0U;
    stub.container_register_count = 0U;
    stub.initialize = true;
    std::vector<Operation> body;
    // Without boundary inputs nothing on the host wakes the kernel; it runs at
    // start and then by its own timers (behavioral delays).
    if (stub.static_sensitivity.empty()) {
        body.push_back(WaitForever { });
    } else {
        body.push_back(WaitSensitivity { });
    }
    body.push_back(Jump { 0U });
    body.push_back(Yield { });
    body.push_back(Jump { 0U });
    stub.operations = OperationList { std::move(body) };
    return stub;
}

} // namespace

StaticKernelPlan ElaboratedDesign::plan_static_kernel(
    const StaticKernelAccessPlan& access) const
{
    // The access plan replaces the whole-design coverage, specify and
    // VCD-control gates (docs/engine-v4-semantics.md S19, S21);
    // FSIM_STATIC_KERNEL_ACCESS_PLAN=0 restores them.
    const bool access_plans = [] {
        const char* text = std::getenv("FSIM_STATIC_KERNEL_ACCESS_PLAN");
        return text == nullptr || std::string_view { text } != "0";
    }();
    // A mixed kernel can be refused where a single-language one is not;
    // the majority language's kernel is planned instead.
    bool mixed_planned = false;
    const auto plan_for = [&](const bool allow_mixed) -> StaticKernelPlan {
    StaticKernelPlan plan;
    // Access plan: clients outside the SimIR processes that reach any
    // signal. Their signals stay host signals, which members write through
    // the host commit path, so the clients see the reference values,
    // callbacks and delta timing. A VCD control operation raises
    // `observe_all` below.
    bool observe_all = access.observe_all;
    // Census (FSIM_PROFILE_PHASES): the first reason each process stays on
    // the host. Opaque accesses come first: they refuse the whole design.
    // That refusal is then deferred until the processes are classified.
    const bool census = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    std::vector<std::string> rejection(census ? this->process_count() : 0U);
    std::vector<std::string> opaque(rejection.size());
    std::vector<std::uint8_t> census_valid(rejection.size(), 1U);
    std::vector<std::string> census_names(rejection.size());
    const auto reject = [&](const std::size_t index, const char* reason) {
        if (census && rejection[index].empty()) {
            rejection[index] = reason;
        }
    };
    // `hosted` marks the processes the kernel schedules and the reference
    // evaluator executes: they are kernel-owned (not counted as host), and
    // their reasons (why they are not native members) form their own rows.
    const auto take_census = [&](StaticKernelPlan& result,
                                 const std::vector<std::uint8_t>* members,
                                 const std::vector<std::uint8_t>* hosted
                                 = nullptr) {
        if (!census) {
            return;
        }
        std::map<std::string, StaticKernelHostReason> rows;
        std::map<std::string, StaticKernelHostReason> hosted_rows;
        for (std::size_t index = 0U; index < rejection.size(); ++index) {
            if (census_valid[index] == 0U) {
                continue;
            }
            ++result.processes;
            if (members != nullptr && (*members)[index] != 0U) {
                continue;
            }
            const bool is_hosted = hosted != nullptr && (*hosted)[index] != 0U;
            if (!is_hosted) {
                ++result.host_processes;
            }
            auto reason = !opaque[index].empty() ? opaque[index]
                : !rejection[index].empty()      ? rejection[index]
                : result.disabled
                    ? std::string { "design_" } + result.disabled_reason
                    : std::string { "planner_drop" };
            auto& row = (is_hosted ? hosted_rows : rows)[reason];
            if (row.count++ == 0U) {
                row.reason = std::move(reason);
                row.example = census_names[index];
            }
        }
        for (auto& [reason, row] : rows) {
            result.host_reasons.push_back(std::move(row));
        }
        for (auto& [reason, row] : hosted_rows) {
            result.hosted_reasons.push_back(std::move(row));
        }
    };
    const auto disable = [&](const char* reason) {
        StaticKernelPlan disabled;
        disabled.disabled = true;
        disabled.disabled_reason = reason;
        take_census(disabled, nullptr);
        return disabled;
    };
    // Coverage: the counters are host data bumped by CodeCoverageHit
    // operations, which the kernel does not evaluate, so the processes that
    // carry them are not members (they stay host processes with an
    // unsupported-operation reason) and the rest run as usual.
    if (code_coverage_inventory_ && !access_plans) {
        return disable("coverage");
    }
    if ((!verilog_specify_paths_.empty() || !verilog_timing_checks_.empty())
        && !access_plans) {
        return disable("specify");
    }
    if (!systemc_processes_.empty() || !systemc_objects_.empty()) {
        return disable("systemc");
    }

    const auto process_count = this->process_count();
    const auto signal_count = signals_.size();
    std::vector<ProcessProgramView> views;
    views.reserve(process_count);
    for (std::size_t index = 0U; index < process_count; ++index) {
        views.push_back(detail::ElaboratedDesignProcessAccess::process_view(
            *this, index));
        if (census) {
            census_valid[index] = views.back().valid() ? 1U : 0U;
            census_names[index] = views.back().valid()
                ? std::string { views.back().name() } : std::string { };
        }
    }
    // A design without a single valid process has nothing to schedule: it is
    // admitted trivially (docs/engine-v4-semantics.md S3), not refused. The
    // plan carries no specification; the design runs with every signal a
    // host signal, which is exact because no process reads or writes one.
    if (std::ranges::none_of(views,
            [](const auto& view) { return view.valid(); })) {
        StaticKernelPlan trivial;
        trivial.disabled_reason = "trivial_no_processes";
        return trivial;
    }
    for (const auto& view : views) {
        const auto& operations = view.operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            if (operation_holds<VcdControl>(operations[op])) {
                if (!access_plans) {
                    return disable("vcd_control");
                }
                // $dumpvars scopes are run-time values and a dump records
                // any signal's changes (IEEE 1800-2023 21.7).
                observe_all = true;
            }
        }
    }
    if (std::ranges::any_of(signals_, [](const auto& signal) {
            return signal.initial_value.width()
                > std::numeric_limits<std::uint32_t>::max();
        })) {
        return disable("signal_width");
    }
    const auto graph_owner
        = detail::ElaboratedDesignProcessAccess::region_graph(*this);
    const auto& graph = *graph_owner;
    const auto graph_signals = graph.signals();
    const auto graph_processes = graph.processes();
    const char* deferred_refusal = nullptr;
    // Publish-all mode (docs/engine-v4-semantics.md S4, S19): a process whose
    // signal accesses are statically unknown (a class method call, a class
    // property write through a handle) may read or write ANY signal. It stays
    // a non-member, every kernel-owned signal is published to the host so its
    // reads are current, and a value the host commits to an owned signal is
    // imported into the kernel (StaticKernel::import_host_write). Scope is
    // SystemVerilog: an opaque generic-domain (VHDL) process, or a VHDL kernel
    // member, keeps the whole-design refusal, because VHDL signals have driver
    // and resolution semantics (IEEE 1076-2019 14.7.2, 14.7.3) that a
    // host-computed effective value cannot simply replace in a kernel slot.
    // FSIM_STATIC_KERNEL_PUBLISH_ALL=0 restores the refusal everywhere.
    bool publish_all = false;
    if (std::ranges::any_of(graph_processes,
            &RegionProcessNode::dependencies_unknown)) {
        const char* publish_all_switch
            = std::getenv("FSIM_STATIC_KERNEL_PUBLISH_ALL");
        const bool publish_all_allowed = (publish_all_switch == nullptr
                || std::string_view { publish_all_switch } != "0")
            && std::ranges::none_of(graph_processes, [](const auto& node) {
                   return node.dependencies_unknown
                       && node.scheduling_domain
                           != ProcessSchedulingDomain::systemverilog;
               });
        // A VHDL opaque process (a VHPIDIRECT foreign subprogram call, say)
        // is harmless when every signal is a host object anyway: a foreign
        // library or an observer is attached (S19, S21). Nothing is
        // kernel-owned, so nothing needs publishing and no host-computed
        // effective value replaces a kernel slot; the process is hosted like
        // any other non-member (S25a).
        const bool all_signals_host
            = (access.foreign || observe_all)
            && (publish_all_switch == nullptr
                || std::string_view { publish_all_switch } != "0");
        if (publish_all_allowed) {
            publish_all = true;
        } else if (all_signals_host) {
            // Accepted: no refusal, no publish-all mode.
        } else {
            // Fail closed: an opaque access could read kernel-owned state.
            if (!census) {
                return disable("unknown_dependencies");
            }
            deferred_refusal = "unknown_dependencies";
        }
        if (census) {
            for (std::size_t index = 0U;
                 index < graph_processes.size() && index < process_count;
                 ++index) {
                const auto& node = graph_processes[index];
                if (!node.dependencies_unknown) {
                    continue;
                }
                const auto& operations = views[index].operations();
                opaque[index] = node.unknown_dependency_instruction
                        < operations.size()
                    ? "opaque_access:"
                        + operation_type_name(operations.expanded(
                            node.unknown_dependency_instruction))
                    : std::string { "opaque_access" };
            }
        }
    }
    // The first property that keeps a process off the kernel whatever its
    // body, or nullptr; `behavioral` applies the behavioral tier's rules.
    const auto flag_rejection = [&](const ProcessProgramView& view,
                                    const bool vhdl,
                                    const bool behavioral) -> const char* {
        const auto non_logic4 = std::ranges::any_of(view.register_value_kinds(),
            [](const ValueKind kind) { return kind != ValueKind::logic4; });
        if (view.observed()) {
            return "observed";
        }
        if (view.reactive()) {
            return "reactive";
        }
        if (view.postponed()) {
            return "postponed";
        }
        if (view.final()) {
            return "final";
        }
        if (!view.initialize()) {
            return "not_initialized";
        }
        // Package static initializers run once, before time zero, in the
        // host's startup order (IEEE 1800-2023 6.21, 26.3); the host owns
        // their startup metadata and a synchronous finish.
        if (view.startup_phase()
            == ProcessStartupPhase::package_static_initializer) {
            return "package_static_initializer";
        }
        if (view.switch_source() || view.switch_target()
            || view.switch_control() || view.switch_bidirectional()
            || view.switch_resistive()) {
            return "switch";
        }
        if (view.drive_strength() != DriveStrength { }) {
            return "drive_strength";
        }
        if (behavioral) {
            if (!view.static_trigger_regions().empty()) {
                return "static_trigger_regions";
            }
            if (vhdl) {
                // VHDL behavioral threads keep their registers in the
                // member's reference-format file (any value kind); strings,
                // containers and the 2019 report semantics stay on the host.
                if (view.string_register_count() != 0U) {
                    return "string_register";
                }
                if (view.container_register_count() != 0U) {
                    return "vhdl_container_register";
                }
                if (view.language_standard() == "2019") {
                    return "vhdl_2019";
                }
            } else if (non_logic4) {
                return "register_kind";
            }
            if (std::ranges::any_of(view.static_sensitivity(),
                    [&](const Sensitivity& entry) {
                        return entry.signal >= signal_count
                            || signals_[entry.signal].event_variable;
                    })) {
                return "event_sensitivity";
            }
            return nullptr;
        }
        if (view.string_register_count() != 0U) {
            return "string_register";
        }
        if (vhdl && view.container_register_count() != 0U) {
            return "vhdl_container_register";
        }
        if (vhdl && view.language_standard() == "2019") {
            return "vhdl_2019";
        }
        if (!vhdl && non_logic4) {
            return "register_kind";
        }
        if (view.operations().size() < 2U) {
            return "trivial";
        }
        return nullptr;
    };
    std::vector<std::uint8_t> alias_signal(signal_count, 0U);
    for (const auto& family : graph.signal_alias_families()) {
        if (family.proxy < signal_count) {
            alias_signal[family.proxy] = 1U;
        }
    }
    for (const auto& alias : container_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_signal[alias.signal] = 1U;
        }
    }
    for (const auto& alias : container_element_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_signal[alias.signal] = 1U;
        }
    }
    for (const auto& alias : container_aggregate_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_signal[alias.signal] = 1U;
        }
    }

    // 1. Classify member candidates.
    std::vector<std::optional<Candidate>> candidates(process_count);
    std::vector<std::vector<ContainerObjectId>> container_uses(process_count);
    for (std::size_t index = 0U; index < process_count; ++index) {
        const auto& view = views[index];
        if (!view.valid()) {
            continue;
        }
        const auto& operations = view.operations();
        const auto count = operations.size();
        // Stored operations (canonical or overridden) answer every question
        // below except instance fields, which only ReadContainerObject's
        // object and signal operands carry; those are expanded or remapped.
        for (std::size_t op = 0U; op < count; ++op) {
            const auto& stored = operations[op];
            if (operation_holds<ReadContainerObject>(stored)) {
                collect_container_objects(
                    operations.expanded(op), container_uses[index]);
            } else {
                collect_container_objects(stored, container_uses[index]);
            }
        }
        std::ranges::sort(container_uses[index]);
        container_uses[index].erase(std::unique(container_uses[index].begin(),
            container_uses[index].end()), container_uses[index].end());
        const bool vhdl = view.scheduling_domain()
            != ProcessSchedulingDomain::systemverilog;
        if (access.debug) {
            // Debugger plan: no comb/seq/once shape. Every process is
            // classified by the behavioral tiers below (a resumable thread
            // that can stop at any debug point) or stays hosted.
            continue;
        }
        if (const char* flag = flag_rejection(view, vhdl, false)) {
            reject(index, flag);
            continue;
        }
        Candidate member;
        member.process = static_cast<ProcessId>(index);
        member.vhdl = vhdl;
        const auto& sensitivity = view.static_sensitivity();
        std::optional<std::size_t> wait;
        bool shape_ok = true;
        for (std::size_t op = 0U; op < count; ++op) {
            if (operation_holds<WaitSensitivity>(operations[op])) {
                if (wait) {
                    shape_ok = false;
                }
                wait = op;
            }
        }
        if (!shape_ok) {
            reject(index, "shape_waits");
            continue;
        }
        const auto loop_jump = [&](const std::size_t op) {
            if (op >= count) {
                return false;
            }
            const auto* jump = operation_get_if<Jump>(&operations[op]);
            return jump != nullptr && jump->target == 0U;
        };
        // Structural operations excluded from the supported-operation check.
        std::size_t structural_a = count;
        std::size_t structural_b = count;
        if (vhdl) {
            // A VHDL process with one static wait W runs from operation 0 to
            // W at initialization; each activation resumes at W + 1 and runs
            // until it reaches W again. A process without sensitivity runs
            // once to its halt.
            if (sensitivity.empty()) {
                std::optional<std::size_t> halt;
                for (std::size_t op = 0U; op < count && shape_ok; ++op) {
                    if (const auto* value = operation_get_if<Halt>(&operations[op])) {
                        shape_ok = !halt && !value->program_exit;
                        halt = op;
                    }
                }
                if (wait || !halt || !shape_ok) {
                    reject(index, "shape_vhdl_once");
                    continue;
                }
                member.kind = 2U;
                member.body_begin = 0U;
                member.body_end = static_cast<std::uint32_t>(*halt);
                structural_a = *halt;
            } else {
                const bool any = std::ranges::all_of(sensitivity,
                    [](const Sensitivity& entry) {
                        return entry.edge == EdgeKind::any;
                    });
                const bool edges = std::ranges::all_of(sensitivity,
                    [](const Sensitivity& entry) {
                        return entry.edge == EdgeKind::posedge
                            || entry.edge == EdgeKind::negedge;
                    });
                if (!wait || *wait + 1U >= count || !(any || edges)
                    || std::ranges::any_of(sensitivity,
                        [&](const Sensitivity& entry) {
                            return entry.signal >= signal_count;
                        })) {
                    reject(index, "shape_vhdl_sensitivity");
                    continue;
                }
                member.kind = any ? 0U : 1U;
                member.run_at_start = *wait != 0U;
                member.body_begin = static_cast<std::uint32_t>(*wait + 1U);
                member.body_end = static_cast<std::uint32_t>(*wait);
                member.sensitivity = sensitivity;
                structural_a = *wait;
            }
        } else if (sensitivity.empty()) {
            const auto* halt = operation_get_if<Halt>(&operations[count - 1U]);
            if (wait || halt == nullptr || halt->program_exit) {
                reject(index, "shape_once");
                continue;
            }
            member.kind = 2U;
            member.body_begin = 0U;
            member.body_end = static_cast<std::uint32_t>(count - 1U);
            structural_a = count - 1U;
        } else {
            if (!wait) {
                reject(index, "shape_no_wait");
                continue;
            }
            const bool any = std::ranges::all_of(sensitivity,
                [](const Sensitivity& entry) {
                    return entry.edge == EdgeKind::any;
                });
            const bool edges = std::ranges::all_of(sensitivity,
                [](const Sensitivity& entry) {
                    return entry.edge == EdgeKind::posedge
                        || entry.edge == EdgeKind::negedge;
                });
            // SystemVerilog bodies end at the loop jump. VHDL subprogram
            // bodies may follow it and are reached only through Call.
            if (*wait != 0U && any && loop_jump(*wait + 1U)
                && (vhdl || *wait == count - 2U)) {
                member.kind = 0U;
                member.run_at_start = true;
                member.body_begin = 0U;
                member.body_end = static_cast<std::uint32_t>(*wait);
                member.exit_alt = static_cast<std::uint32_t>(*wait + 1U);
                structural_a = *wait;
                structural_b = *wait + 1U;
            } else if (*wait == 0U && (any || edges)) {
                std::size_t jump = 1U;
                while (jump < count && !loop_jump(jump)) {
                    ++jump;
                }
                if (jump == count || (!vhdl && jump != count - 1U)) {
                    reject(index, "shape_loop");
                    continue;
                }
                member.kind = any ? 0U : 1U;
                member.body_begin = 1U;
                member.body_end = static_cast<std::uint32_t>(jump);
                member.exit_alt = 0U;
                structural_a = 0U;
                structural_b = jump;
            } else {
                reject(index, "shape_wait");
                continue;
            }
            if (std::ranges::any_of(sensitivity, [&](const Sensitivity& entry) {
                    return entry.signal >= signal_count;
                })) {
                reject(index, "shape_sensitivity");
                continue;
            }
            member.sensitivity = sensitivity;
        }
        bool supported = true;
        std::size_t unsupported_at = count;
        for (std::size_t op = 0U; op < count && supported; ++op) {
            if (op == structural_a || op == structural_b) {
                continue;
            }
            unsupported_at = op;
            const auto& operation = operations[op];
            const bool overridden = &operation != operations.data() + op;
            supported = (vhdl ? static_kernel_vhdl_operation_supported(operation)
                              : static_kernel_operation_supported(operation))
                && !branch_target_outside(operation, count);
            if (supported) {
                visit_operation([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (requires { value.signal; }) {
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(value.signal)>,
                                          SignalId>) {
                            supported = (overridden ? value.signal
                                                    : operations.signal(value.signal))
                                < signal_count;
                        }
                    }
                    if constexpr (std::is_same_v<T, Call>) {
                        supported = value.target < count
                            && value.return_target < count;
                    }
                }, operation);
            }
        }
        if (!supported) {
            if (census && rejection[index].empty()) {
                rejection[index] = unsupported_at < count
                    ? "operation:"
                        + operation_type_name(operations.expanded(unsupported_at))
                    : std::string { "operation" };
            }
            continue;
        }
        candidates[index] = std::move(member);
        if (census) {
            rejection[index].clear();
        }
    }
    // Behavioral members: SystemVerilog testbench processes that run as
    // resumable kernel threads (plan §3.5). FSIM_STATIC_KERNEL_BEHAVIORAL=0
    // keeps them on the host.
    if (const char* behavioral = std::getenv("FSIM_STATIC_KERNEL_BEHAVIORAL");
        behavioral == nullptr || std::string_view { behavioral } != "0") {
        for (std::size_t index = 0U; index < process_count; ++index) {
            const auto& view = views[index];
            if (candidates[index] || !view.valid()
                || view.scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog) {
                continue;
            }
            // The behavioral tier is the general one: its reason replaces
            // the static tiers'.
            if (const char* flag = flag_rejection(view, false, true)) {
                if (census) {
                    rejection[index] = std::string { "behavioral_" } + flag;
                }
                continue;
            }
            const auto& operations = view.operations();
            const auto count = operations.size();
            Candidate member;
            member.process = static_cast<ProcessId>(index);
            member.kind = behavioral_kind;
            member.run_at_start = true;
            member.body_begin = 0U;
            member.body_end = static_cast<std::uint32_t>(count);
            member.sensitivity = view.static_sensitivity();
            bool supported = count != 0U;
            std::size_t failed_at = count;
            const char* failed_reason = "";
            // Container registers bound to design memories, used directly
            // (the kernel gives each such private register its own storage),
            // and saved by automatic frames.
            std::set<ContainerRegisterId> bound_containers;
            std::set<ContainerRegisterId> private_containers;
            // Whole-register copies (CopyContainerRegister): both registers
            // must be process-private arrays of one shape.
            std::vector<std::pair<ContainerRegisterId, ContainerRegisterId>>
                container_copies;
            bool has_fork = false;
            bool has_frame_push = false;
            bool has_random = false;
            for (std::size_t op = 0U; op < count && supported; ++op) {
                const auto operation = operations.expanded(op);
                failed_at = op;
                if (!static_kernel_behavioral_operation_supported(operation)) {
                    supported = false;
                    failed_reason = "operation";
                    break;
                }
                if (branch_target_outside(operation, count)) {
                    supported = false;
                    failed_reason = "branch_target";
                    break;
                }
                failed_reason = "operand";
                visit_operation([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (requires { value.signal; }) {
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(value.signal)>,
                                          SignalId>) {
                            supported = supported && value.signal < signal_count;
                        }
                    }
                    if constexpr (std::is_same_v<T, Call>) {
                        supported = supported && value.target < count
                            && value.return_target < count;
                    } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
                        bound_containers.insert(value.destination);
                    } else if constexpr (std::is_same_v<T, ContainerRead>) {
                        private_containers.insert(value.source);
                    } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                        private_containers.insert(value.target);
                    } else if constexpr (std::is_same_v<T, CopyContainerRegister>) {
                        private_containers.insert(value.destination);
                        private_containers.insert(value.source);
                        container_copies.emplace_back(
                            value.destination, value.source);
                    } else if constexpr (std::is_same_v<T, CallableFramePush>) {
                        has_frame_push = true;
                    } else if constexpr (std::is_same_v<T, RandomValue>) {
                        has_random = true;
                    } else if constexpr (std::is_same_v<T, Fork>) {
                        has_fork = true;
                        supported = supported
                            && std::ranges::all_of(value.branches,
                                [&](const InstructionIndex branch) {
                                    return branch < count;
                                });
                    } else if constexpr (std::is_same_v<T, WaitOn>) {
                        for (std::size_t entry = 0U;
                             entry < value.signals.size() && supported; ++entry) {
                            const auto signal = value.signals[entry];
                            const auto edge = value.edges.empty()
                                ? EdgeKind::any : value.edges[entry];
                            supported = signal < signal_count
                                && !signals_[signal].event_variable
                                && (edge == EdgeKind::any
                                    || ((edge == EdgeKind::posedge
                                            || edge == EdgeKind::negedge)
                                        && signals_[signal].initial_value.width()
                                            == 1U));
                            if (supported) {
                                member.waited.push_back(signal);
                            }
                        }
                    }
                }, operation);
            }
            if (supported && has_fork && has_random) {
                // A fork child is a reference process with its own identity
                // and random stream (IEEE 1800-2023 18.14); kernel threads
                // have neither.
                supported = false;
                failed_at = count;
                failed_reason = "fork_random";
            }
            if (supported && has_fork && has_frame_push
                && fork_holds_suspended_frame(operations, count)) {
                // Fork children and the activation that joins them observe
                // the automatic variables of the enclosing callable frames
                // (IEEE 1800-2023 6.21, 9.3.2), and concurrent activations of
                // one automatic task each own their arguments and locals
                // (13.4.2). The reference evaluator gives every thread its
                // own callable context (escaping contexts, suspended
                // contexts); kernel threads share one register file with a
                // single frame stack per thread, so a thread suspended inside
                // a callable would see a sibling's values on resume. Such
                // processes stay on the host until the kernel's behavioral
                // tier gains per-thread callable contexts. A callable that
                // cannot suspend (every function, 13.4, and any task without
                // a timing control) runs from frame push to pop without
                // another thread of the member running in between, so it
                // shares nothing observable; a joined fork whose children
                // enter only such callables leaves at most the parent inside
                // a suspending one, and the children share its frame as
                // 9.3.2 requires.
                supported = false;
                failed_at = count;
                failed_reason = "fork_callable_frames";
            }
            if (supported) {
                // A register both bound and written would alias the design
                // memory, which the interpreter copies. Private storage must
                // be a fixed packed array.
                const auto types = process_layout_detail::ProcessLayoutAccess::view(
                    view.container_register_types());
                for (const auto id : private_containers) {
                    if (bound_containers.contains(id)) {
                        continue;
                    }
                    const bool fixed = id < types.size() && types[id].fixed
                        && !types[id].associative
                        && (types[id].element_kind == ContainerElementKind::Packed
                            || types[id].element_kind
                                == ContainerElementKind::Scalar);
                    if (!fixed) {
                        supported = false;
                        failed_at = count;
                        failed_reason = "private_container_shape";
                        break;
                    }
                }
                // A copy keeps the reference semantics only between private
                // registers of identical type (the reference requires the
                // same type, IEEE 1800-2023 7.6). A register bound to a
                // design memory keeps the host: the kernel does not notify
                // memory readers of a whole-array write.
                for (const auto& [destination, source] : container_copies) {
                    if (!supported) {
                        break;
                    }
                    if (bound_containers.contains(destination)
                        || bound_containers.contains(source)
                        || destination >= types.size() || source >= types.size()
                        || !(types[destination] == types[source])
                        || types[destination].sparse
                        || types[destination].vhdl_shaped) {
                        supported = false;
                        failed_at = count;
                        failed_reason = "container_copy_shape";
                    }
                }
            }
            if (!supported && census) {
                rejection[index] = std::string { "behavioral_" } + failed_reason
                    + (failed_at < count
                            ? ":" + operation_type_name(
                                  operations.expanded(failed_at))
                            : std::string { });
            }
            if (supported) {
                candidates[index] = std::move(member);
                if (census) {
                    rejection[index].clear();
                }
            } else if (std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr) {
                std::cerr << "fsim-kernel-plan: behavioral rejected process="
                          << view.name() << " op=" << failed_at
                          << " reason=" << failed_reason;
                if (failed_at < count) {
                    std::cerr << " kind="
                              << operation_type_name(
                                     operations.expanded(failed_at));
                }
                std::set<std::string> unsupported;
                for (std::size_t op = 0U; op < count; ++op) {
                    const auto operation = operations.expanded(op);
                    if (!static_kernel_behavioral_operation_supported(
                            operation)) {
                        unsupported.insert(operation_type_name(operation));
                    }
                }
                for (const auto& name : unsupported) {
                    std::cerr << " unsupported=" << name;
                }
                std::cerr << '\n';
            }
        }
    }
    // VHDL behavioral members: processes that fail the static VHDL shapes
    // (wait for, wait on, wait until, several waits, waits in procedures,
    // bare wait) run as resumable kernel threads (IEEE 1076-2019 11.3,
    // 10.2). Their signal assignments are the delta-mode ones: queued in the
    // thread's round and applied in the round's update phase (14.7.3.4); a
    // wait resumes in the delta after the event or timeout (14.7.5.3). Any
    // operation outside the supported set keeps the process on the host (and,
    // through the vhdl_partial rule, the whole VHDL design).
    // FSIM_STATIC_KERNEL_VHDL_BEHAVIORAL=0 disables this tier.
    if (const char* behavioral = std::getenv("FSIM_STATIC_KERNEL_BEHAVIORAL");
        behavioral == nullptr || std::string_view { behavioral } != "0") {
        const char* vhdl_text = std::getenv("FSIM_STATIC_KERNEL_VHDL_BEHAVIORAL");
        const bool vhdl_enabled
            = vhdl_text == nullptr || std::string_view { vhdl_text } != "0";
        for (std::size_t index = 0U; vhdl_enabled && index < process_count;
             ++index) {
            const auto& view = views[index];
            if (candidates[index] || !view.valid()
                || view.scheduling_domain()
                    == ProcessSchedulingDomain::systemverilog) {
                continue;
            }
            if (const char* flag = flag_rejection(view, true, true)) {
                if (census) {
                    rejection[index] = std::string { "behavioral_" } + flag;
                }
                continue;
            }
            const auto& operations = view.operations();
            const auto count = operations.size();
            Candidate member;
            member.process = static_cast<ProcessId>(index);
            member.kind = behavioral_kind;
            member.vhdl = true;
            member.run_at_start = false;
            member.body_begin = 0U;
            member.body_end = static_cast<std::uint32_t>(count);
            const auto& sensitivity = view.static_sensitivity();
            bool supported = count != 0U;
            std::size_t failed_at = count;
            const char* failed_reason = "";
            // The implicit wait of a sensitivity list wakes on any event of
            // a whole signal (a thread has no range filter).
            for (const auto& entry : sensitivity) {
                if (entry.signal >= signal_count
                    || alias_signal[entry.signal] != 0U
                    || entry.edge != EdgeKind::any || entry.offset != 0U
                    || entry.width != 0U) {
                    supported = false;
                    failed_reason = "sensitivity";
                    break;
                }
            }
            member.sensitivity = sensitivity;
            for (std::size_t op = 0U; op < count && supported; ++op) {
                const auto operation = operations.expanded(op);
                failed_at = op;
                if (!static_kernel_vhdl_behavioral_operation_supported(
                        operation)) {
                    supported = false;
                    failed_reason = "operation";
                    break;
                }
                if (branch_target_outside(operation, count)) {
                    supported = false;
                    failed_reason = "branch_target";
                    break;
                }
                failed_reason = "operand";
                visit_operation([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (requires { value.signal; }) {
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(value.signal)>,
                                          SignalId>) {
                            supported = supported && value.signal < signal_count;
                        }
                    }
                    if constexpr (std::is_same_v<T, Call>) {
                        supported = supported && value.target < count
                            && value.return_target < count;
                    } else if constexpr (std::is_same_v<T, WaitSensitivity>) {
                        supported = supported && !sensitivity.empty();
                    } else if constexpr (std::is_same_v<T, WaitOn>) {
                        for (std::size_t entry = 0U;
                             entry < value.signals.size() && supported; ++entry) {
                            const auto signal = value.signals[entry];
                            supported = signal < signal_count
                                && alias_signal[signal] == 0U
                                && !signals_[signal].event_variable;
                            if (supported) {
                                member.waited.push_back(signal);
                            }
                        }
                    }
                }, operation);
            }
            if (!supported && census) {
                rejection[index] = std::string { "behavioral_" } + failed_reason
                    + (failed_at < count
                            ? ":" + operation_type_name(
                                  operations.expanded(failed_at))
                            : std::string { });
            }
            if (supported) {
                candidates[index] = std::move(member);
            } else if (std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr) {
                std::cerr << "fsim-kernel-plan: vhdl behavioral rejected process="
                          << view.name() << " op=" << failed_at
                          << " reason=" << failed_reason;
                if (failed_at < count) {
                    std::cerr << " kind="
                              << operation_type_name(
                                     operations.expanded(failed_at));
                }
                std::cerr << '\n';
            }
        }
    }
    if (publish_all && deferred_refusal == nullptr
        && std::ranges::any_of(candidates, [](const auto& candidate) {
               return candidate && candidate->vhdl;
           })) {
        // A VHDL member with an opaque process (see publish_all above).
        deferred_refusal = "unknown_dependencies";
        publish_all = false;
    }
    if (deferred_refusal != nullptr) {
        return disable(deferred_refusal);
    }
    // A named block or task that a disable statement of another process
    // names (IEEE 1800-2023 9.6.2) is terminated through the host scheduler's
    // process records, which a kernel member does not have: a member holding
    // such a block would keep running. The process stays on the host until
    // the kernel itself can terminate a named scope (breadth workstream C3,
    // SystemVerilog breadth: disable and process control).
    {
        std::set<std::string> disabled_scopes;
        for (const auto& view : views) {
            if (!view.valid()) {
                continue;
            }
            const auto& operations = view.operations();
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                if (!operation_holds<DisableBlock>(operations[op])) {
                    continue;
                }
                const auto expanded = operations.expanded(op);
                if (const auto* statement
                    = operation_get_if<DisableBlock>(&expanded)) {
                    disabled_scopes.insert(
                        statement->scopes.begin(), statement->scopes.end());
                }
            }
        }
        if (!disabled_scopes.empty()) {
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (!candidates[index]) {
                    continue;
                }
                const auto& operations = views[index].operations();
                std::string named;
                for (std::size_t op = 0U; op < operations.size() && named.empty();
                     ++op) {
                    const auto* point
                        = operation_get_if<DebugPoint>(&operations[op]);
                    if (point == nullptr
                        || point->kind != DebugPointKind::block_entry) {
                        continue;
                    }
                    const auto& actual = operations.debug_point(op, *point);
                    const auto& scope
                        = operations.debug_scope(actual.scope).str();
                    if (disabled_scopes.contains(scope)) {
                        named = scope;
                    }
                }
                if (!named.empty()) {
                    if (std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr) {
                        std::cerr << "fsim-kernel-plan: rejected process="
                                  << views[index].name()
                                  << " reason=disabled_scope scope=" << named
                                  << '\n';
                    }
                    candidates[index].reset();
                    reject(index, "disabled_scope");
                }
            }
        }
    }
    // A thread that the scheduler would start one delta late (a process that
    // reads a settling signal and waits only on time, or not at all) starts
    // late in the kernel too, after time zero's continuous assignments and
    // event controls have settled.
    if (std::ranges::any_of(candidates, [](const auto& candidate) {
            return candidate && candidate->kind == behavioral_kind
                && !candidate->vhdl;
        })) {
        const auto late = runtime::simir::systemverilog_late_starters(
            process_count, signal_count,
            [&](const std::size_t id) -> const ProcessProgramView& {
                return views[id];
            },
            [&](const std::size_t id) { return !views[id].valid(); },
            [&](const std::size_t id) {
                return !(candidates[id] && candidates[id]->kind == behavioral_kind
                    && !candidates[id]->vhdl);
            },
            [&](const std::size_t signal) {
                return signals_[signal].resolution != ResolutionKind::none;
            },
            [](const std::size_t) { return false; });
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (candidates[index] && candidates[index]->kind == behavioral_kind
                && !candidates[index]->vhdl) {
                candidates[index]->start_late = late[index];
            }
        }
    }
    // VHDL members follow exact delta semantics, SystemVerilog members the
    // levelized Active-region schedule. A design with both runs one mixed
    // kernel that keeps both schedules (FSIM_STATIC_KERNEL_MIXED=0 instead
    // keeps only the majority language).
    std::size_t vhdl_candidates = 0U;
    std::size_t sv_candidates = 0U;
    for (const auto& candidate : candidates) {
        if (candidate) {
            ++(candidate->vhdl ? vhdl_candidates : sv_candidates);
        }
    }
    const char* mixed_text = std::getenv("FSIM_STATIC_KERNEL_MIXED");
    const bool mixed = allow_mixed
        && (mixed_text == nullptr || std::string_view { mixed_text } != "0")
        && vhdl_candidates != 0U && sv_candidates != 0U;
    mixed_planned = mixed;
    // A design without a native member (no candidates of either language)
    // takes the language of its processes: its VHDL processes are hosted
    // (S25a) and its delta semantics are VHDL's (IEEE 1076-2019 14.7.5).
    std::size_t vhdl_processes = 0U;
    std::size_t sv_processes = 0U;
    for (const auto& view : views) {
        if (view.valid()) {
            ++(view.scheduling_domain()
                    == ProcessSchedulingDomain::systemverilog
                    ? sv_processes
                    : vhdl_processes);
        }
    }
    const bool vhdl_mode = mixed || vhdl_candidates > sv_candidates
        || (vhdl_candidates == 0U && sv_candidates == 0U
            && vhdl_processes > sv_processes);
    for (std::size_t index = 0U; index < process_count; ++index) {
        auto& candidate = candidates[index];
        if (!mixed && candidate && candidate->vhdl != vhdl_mode) {
            candidate.reset();
            reject(index, "language_mode");
        }
    }
    // A module path delays the commit of its destination, measured from the
    // most recent transition of a path source (IEEE 1800-2023 30.4, 30.5).
    // The host applies that delay when the driver of the destination writes
    // it, and finds the source transition only once the source's own update
    // has committed: the driver runs in a later delta than the update that
    // changed the source. A kernel member evaluates the whole combinational
    // cone from the source to the destination in one run, so the source
    // update is still staged when the destination is written and the host
    // sees no source transition (the delay is lost or taken from the wrong
    // path). The processes that drive a path destination therefore stay off
    // the native members, where they wake after the source commits.
    std::vector<std::uint8_t> path_destination(signal_count, 0U);
    for (const auto& path : verilog_specify_paths_) {
        for (const auto& terminal : path.destinations) {
            if (terminal.signal < signal_count) {
                path_destination[terminal.signal] = 1U;
            }
        }
    }
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        if (path_destination[signal] == 0U) {
            continue;
        }
        for (const auto& writer : graph_signals[signal].writers) {
            if (writer.process < process_count && candidates[writer.process]) {
                candidates[writer.process].reset();
                reject(writer.process, "path_destination_driver");
            }
        }
    }
    const auto vhdl_written = [&](const SignalId signal) {
        return std::ranges::all_of(graph_signals[signal].writers,
            [&](const auto& writer) {
                return writer.process < process_count
                    && candidates[writer.process]
                    && candidates[writer.process]->vhdl;
            });
    };

    // 2. Alias structure: per-element arrays (leaves plus an optional
    // aggregate proxy) and memories stored as one packed signal are owned
    // as units together with their container object.
    const auto object_count = container_objects_.size();
    std::vector<std::vector<SignalId>> element_signals(object_count);
    std::vector<std::uint8_t> element_complete(object_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        element_signals[object].assign(
            container_objects_[object].initial_value.elements.size(),
            std::numeric_limits<SignalId>::max());
    }
    for (const auto& alias : container_element_signal_aliases_) {
        if (alias.object < object_count
            && alias.ordinal < element_signals[alias.object].size()) {
            element_signals[alias.object][alias.ordinal] = alias.signal;
        }
    }
    for (std::size_t object = 0U; object < object_count; ++object) {
        element_complete[object] = !element_signals[object].empty()
            && std::ranges::none_of(element_signals[object],
                [](const SignalId signal) {
                    return signal == std::numeric_limits<SignalId>::max();
                })
            ? 1U : 0U;
    }
    std::vector<std::optional<SignalId>> packed_alias(object_count);
    std::vector<std::uint8_t> has_element_alias(object_count, 0U);
    for (const auto& alias : container_signal_aliases_) {
        if (alias.object < object_count) {
            packed_alias[alias.object] = alias.signal;
        }
    }
    for (const auto& alias : container_element_signal_aliases_) {
        if (alias.object < object_count) {
            has_element_alias[alias.object] = 1U;
        }
    }
    std::vector<const RegionSignalAliasFamilyDescriptor*> family_of_object(
        object_count, nullptr);
    std::vector<std::uint8_t> proxy_signal(signal_count, 0U);
    for (const auto& family : graph.signal_alias_families()) {
        if (family.object < object_count) {
            family_of_object[family.object] = &family;
        }
        if (family.proxy < signal_count) {
            proxy_signal[family.proxy] = 1U;
        }
    }
    // Every signal and container is a host object for a foreign library
    // (VPI/VHPI/DPI) or a trace/VCD observer.
    const bool host_signals = access.foreign || observe_all;
    std::vector<std::uint8_t> container_shape_ok(object_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        const auto& container = container_objects_[object];
        const auto& type = container.initial_value.type;
        container_shape_ok[object] = !host_signals && type.fixed
            && !type.associative
            && !container.slice_alias
            && (type.element_kind == ContainerElementKind::Packed
                || type.element_kind == ContainerElementKind::Scalar)
            && !(packed_alias[object] && has_element_alias[object])
            ? 1U : 0U;
    }
    std::vector<std::vector<ProcessId>> accessors(object_count);
    for (std::size_t index = 0U; index < process_count; ++index) {
        for (const auto object : container_uses[index]) {
            if (object < object_count) {
                accessors[object].push_back(static_cast<ProcessId>(index));
            }
        }
    }

    // Signals a non-member touches through anything but a current-value
    // read: writes, forces, history and event queries, sampled reads. None
    // of these may target kernel-owned state.
    std::vector<std::uint8_t> host_touched(signal_count, 0U);
    if (host_signals) {
        std::ranges::fill(host_touched, 1U);
    }
    // Module paths and timing checks run on the host commit path and its
    // value-change events (IEEE 1800-2023 30.4, 31): their terminals,
    // conditions, data sources, notifiers and delayed copies stay host
    // signals, and a path destination takes the host's delayed commit, so
    // the kernel must not mirror an update the host applies later.
    {
        const auto touch = [&](const SignalId signal) {
            if (signal < signal_count) {
                host_touched[signal] = 1U;
            }
        };
        const auto touch_expression = [&](const ModulePathExpression& expression) {
            for (const auto& node : expression.nodes) {
                if (node.operation == ModulePathExpressionOperator::terminal) {
                    touch(node.terminal.signal);
                }
            }
        };
        for (const auto& path : verilog_specify_paths_) {
            for (const auto& terminal : path.sources) {
                touch(terminal.signal);
            }
            for (const auto& terminal : path.destinations) {
                touch(terminal.signal);
            }
            touch_expression(path.condition_program);
            touch_expression(path.data_source_program);
        }
        for (const auto& check : verilog_timing_checks_) {
            touch(check.reference.terminal.signal);
            touch_expression(check.reference.condition);
            if (check.data) {
                touch(check.data->terminal.signal);
                touch_expression(check.data->condition);
            }
            if (check.notifier) {
                touch(*check.notifier);
            }
            if (check.delayed_reference) {
                touch(check.delayed_reference->signal);
            }
            if (check.delayed_data) {
                touch(check.delayed_data->signal);
            }
            touch_expression(check.timestamp_condition);
            touch_expression(check.timecheck_condition);
        }
    }
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (candidates[index]) {
            continue;
        }
        const auto& operations = views[index].operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            visit_operation([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (value.kind != SignalReadKind::current
                        || value.ticks != 1U || value.clock || value.gate) {
                        if (value.signal < signal_count) {
                            host_touched[value.signal] = 1U;
                        }
                    }
                } else if constexpr (requires { value.signal; }) {
                    if constexpr (std::is_same_v<
                                      std::decay_t<decltype(value.signal)>,
                                      SignalId>) {
                        if (value.signal < signal_count) {
                            host_touched[value.signal] = 1U;
                        }
                    }
                }
            }, operations.expanded(op));
        }
    }
    // The endpoints and controls of a switch or inout alias network take
    // their values from the host's network solution, which merges the
    // drivers of every joined net (IEEE 1800-2023 28.3, 23.3.3.1 and 6.6.1):
    // a kernel slot would hold only its own members' drivers. The signals
    // stay host signals; members read the solved value as an input.
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (!views[index].valid()) {
            continue;
        }
        for (const auto& signal : { views[index].switch_source(),
                 views[index].switch_target(), views[index].switch_control() }) {
            if (signal && *signal < signal_count) {
                host_touched[*signal] = 1U;
            }
        }
    }
    // A port-association program (a resolver with sources) runs on the
    // host in the cycle its sources update, so its sources stay host
    // signals (except pure routing, below). The program and the adapters it replaced (halted stubs) are
    // host programs, not VHDL processes the kernel must hold.
    std::vector<std::uint8_t> host_program(process_count, 0U);
    for (std::size_t index = 0U; index < signals_.size(); ++index) {
        // A conversion driver's program and stub run on the host too, and
        // the host writes the resolved actual.
        for (const auto& driver : signals_[index].vhdl_conversion_drivers) {
            for (const auto process : { driver.program, driver.stub }) {
                if (process < process_count) {
                    host_program[process] = 1U;
                }
            }
            for (const auto source : driver.sources) {
                if (source < signal_count) {
                    host_touched[source] = 1U;
                }
            }
            if (index < signal_count) {
                host_touched[index] = 1U;
            }
        }
    }
    // A pure-routing program (a formal fed by a bit range of one signal)
    // is kernel wiring rather than a host program: the kernel copies the
    // range into the formal in the update phase that changes the source, so
    // the formal updates in the same delta cycle (IEEE 1076-2008 6.5.7.1,
    // 14.7.3.3) and the source stays kernel-owned. A wiring whose source or
    // formal cannot be kernel-owned reverts to a host program below.
    struct Wiring {
        SignalId target { };
        SignalId source { };
        ProcessId program { };
        bool live { };
    };
    std::vector<Wiring> wirings;
    std::vector<std::int32_t> wiring_of_target(signal_count, -1);
    const auto wiring_signal_width = [&](const SignalId signal) {
        return static_cast<std::uint64_t>(
            signal < signal_count ? signals_[signal].initial_value.width()
                                  : 0U);
    };
    for (SignalId target = 0U; vhdl_mode && target < signal_count; ++target) {
        const auto& resolver = signals_[target].vhdl_resolver;
        if (!resolver || resolver->sources.size() != 1U
            || !resolver->drivers.empty() || !resolver->variants.empty()
            || resolver->guarded_kind != 0U || resolver->driving_mirror
            || !signals_[target].vhdl_conversion_drivers.empty()
            || resolver->process >= process_count
            || !views[resolver->process].valid()) {
            continue;
        }
        const auto wire = static_kernel_wiring(
            views[resolver->process].operations(), resolver->output,
            wiring_signal_width);
        const auto source = resolver->sources.front();
        if (!wire || wire->source != source || source >= signal_count
            || source == target
            || wire->width != wiring_signal_width(target)
            || signals_[source].value_kind != signals_[target].value_kind) {
            continue;
        }
        wiring_of_target[target] = static_cast<std::int32_t>(wirings.size());
        wirings.push_back({ target, source, resolver->process, true });
    }
    for (SignalId index = 0U; index < signal_count; ++index) {
        const auto& signal = signals_[index];
        if (!signal.vhdl_resolver || signal.vhdl_resolver->sources.empty()) {
            continue;
        }
        if (signal.vhdl_resolver->process < process_count) {
            host_program[signal.vhdl_resolver->process] = 1U;
        }
        if (wiring_of_target[index] >= 0) {
            continue;
        }
        for (const auto source : signal.vhdl_resolver->sources) {
            if (source < signal_count) {
                host_touched[source] = 1U;
            }
        }
    }
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (!views[index].valid() || candidates[index]) {
            continue;
        }
        const auto& operations = views[index].operations();
        if (!views[index].initialize() && operations.size() == 1U
            && operation_holds<Halt>(operations.expanded(0U))) {
            host_program[index] = 1U;
        }
    }
    // Fork children of a behavioral member run as kernel threads, so their
    // writes stay inside the kernel like the member's own (only behavioral
    // members fork).
    const auto forks_in_kernel = [&](const SignalId signal) {
        const auto& writers = graph_signals[signal].writers;
        return !writers.empty()
            && std::ranges::all_of(writers, [&](const auto& writer) {
                   return writer.process < process_count
                       && candidates[writer.process];
               });
    };
    const auto basic_ok = [&](const SignalId signal) {
        const auto& node = graph_signals[signal];
        const auto& info = signals_[signal];
        // Members make only zero-delay inertial projected writes, which
        // never leave pending transactions.
        return (!node.dynamic_fork_writers || forks_in_kernel(signal))
            && (vhdl_mode || !node.partial_projected_transactions)
            && host_touched[signal] == 0U
            && (info.value_kind == ValueKind::logic4
                || (vhdl_mode && (!mixed || vhdl_written(signal))))
            && info.systemverilog_scalar == runtime::SystemVerilogScalarKind::None
            && !info.event_variable && !info.implicit_driver
            && !info.charge_strength
            && signals_[signal].initial_value.width() != 0U
            && (info.resolution == ResolutionKind::none
                || info.resolution == ResolutionKind::sv_wire
                || (vhdl_mode && info.resolution == ResolutionKind::std_logic));
    };
    const auto writers_ok = [&](const SignalId signal) {
        const auto& node = graph_signals[signal];
        if (std::ranges::any_of(node.writers, [&](const auto& writer) {
                return writer.process >= process_count
                    || !candidates[writer.process];
            })) {
            return false;
        }
        const auto resolution = signals_[signal].resolution;
        const auto width = static_cast<std::uint32_t>(
            signals_[signal].initial_value.width());
        if (vhdl_mode && (resolution == ResolutionKind::sv_wire
                || resolution == ResolutionKind::std_logic)) {
            // Each element must have exactly one driving process, so the
            // resolution function is the identity; a resolved std_logic
            // signal must also have a driver for every element.
            std::map<ProcessId, std::vector<std::pair<std::uint32_t,
                std::uint32_t>>> per_process;
            for (const auto& writer : node.writers) {
                if (writer.width == 0U) {
                    per_process[writer.process].emplace_back(0U, width);
                } else {
                    per_process[writer.process].emplace_back(
                        writer.offset, writer.offset + writer.width);
                }
            }
            for (auto& [process, ranges] : per_process) {
                std::ranges::sort(ranges);
                // Merge this process's own ranges.
                std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
                for (const auto& range : ranges) {
                    if (!merged.empty() && range.first <= merged.back().second) {
                        merged.back().second
                            = std::max(merged.back().second, range.second);
                    } else {
                        merged.push_back(range);
                    }
                }
                ranges = std::move(merged);
            }
            std::vector<std::pair<std::uint32_t, std::uint32_t>> regions;
            for (const auto& [process, ranges] : per_process) {
                regions.insert(regions.end(), ranges.begin(), ranges.end());
            }
            std::ranges::sort(regions);
            std::uint64_t covered = 0U;
            for (std::size_t index = 0U; index < regions.size(); ++index) {
                if (index != 0U
                    && regions[index - 1U].second > regions[index].first) {
                    return false;
                }
                covered += regions[index].second - regions[index].first;
            }
            return resolution != ResolutionKind::std_logic || covered == width;
        }
        if (!vhdl_mode && resolution == ResolutionKind::none) {
            // Combinational members settle in dependency order, so which of
            // two members assigning the same bits of a variable writes last
            // is not the order the host scheduler gives them (process
            // order within a time step; IEEE 1800-2023 9.2.2.2.2 makes such
            // a variable an error for always_comb, and 4.7 leaves the
            // order of other processes unspecified). Such a variable stays
            // on the host, where the reference order applies.
            std::vector<ProcessId> combinational;
            for (const auto& writer : node.writers) {
                if (candidates[writer.process]->kind == 0U
                    && std::ranges::find(combinational, writer.process)
                        == combinational.end()) {
                    combinational.push_back(writer.process);
                }
            }
            if (combinational.size() > 1U) {
                std::vector<std::tuple<std::uint32_t, std::uint32_t, ProcessId>>
                    spans;
                for (const auto process : combinational) {
                    for (const auto& region : views[process].driver_regions()) {
                        if (region.signal == signal) {
                            const auto begin = region.whole ? 0U : region.offset;
                            spans.emplace_back(begin,
                                begin + (region.whole ? width : region.width),
                                process);
                        }
                    }
                }
                std::ranges::sort(spans);
                std::uint32_t reach = 0U;
                ProcessId reach_process = 0U;
                for (std::size_t index = 0U; index < spans.size(); ++index) {
                    const auto& [begin, end, process] = spans[index];
                    if (index != 0U && begin < reach && process != reach_process) {
                        return false;
                    }
                    if (end > reach) {
                        reach = end;
                        reach_process = process;
                    }
                }
            }
        }
        if (resolution == ResolutionKind::sv_wire) {
            // A resolved net may be kernel-owned only when its drivers cover
            // pairwise-disjoint bits, so no resolution is needed.
            std::vector<ProcessId> writers;
            for (const auto& writer : node.writers) {
                writers.push_back(writer.process);
            }
            std::ranges::sort(writers);
            writers.erase(std::unique(writers.begin(), writers.end()),
                writers.end());
            if (writers.size() > 1U) {
                std::vector<std::pair<std::uint32_t, std::uint32_t>> regions;
                for (const auto writer : writers) {
                    for (const auto& region : views[writer].driver_regions()) {
                        if (region.signal == signal) {
                            regions.emplace_back(
                                region.whole ? 0U : region.offset,
                                region.whole ? width : region.width);
                        }
                    }
                }
                std::ranges::sort(regions);
                for (std::size_t index = 1U; index < regions.size(); ++index) {
                    if (regions[index - 1U].first + regions[index - 1U].second
                        > regions[index].first) {
                        return false;
                    }
                }
            }
        }
        return true;
    };
    const auto signal_ok = [&](const SignalId signal) {
        return signal < signal_count && basic_ok(signal) && writers_ok(signal);
    };
    // Signals some candidate is edge-sensitive to, or a behavioral thread
    // waits on (the kernel gives a thread no wait on an alias proxy);
    // recomputed for each fixpoint pass (candidates only drop out between
    // passes).
    std::vector<std::uint8_t> edge_sensitive(signal_count, 0U);
    const auto mark_edge_sensitive = [&] {
        std::ranges::fill(edge_sensitive, 0U);
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (!candidates[index]) {
                continue;
            }
            const bool thread = candidates[index]->kind == behavioral_kind;
            for (const auto& entry : candidates[index]->sensitivity) {
                if ((thread || entry.edge != EdgeKind::any)
                    && entry.signal < signal_count) {
                    edge_sensitive[entry.signal] = 1U;
                }
            }
            if (thread) {
                for (const auto signal : candidates[index]->waited) {
                    if (signal < signal_count) {
                        edge_sensitive[signal] = 1U;
                    }
                }
            }
        }
    };
    const auto edge_sensitive_member = [&](const SignalId signal) {
        return signal < signal_count && edge_sensitive[signal] != 0U;
    };

    std::vector<std::uint8_t> owned(signal_count, 0U);
    std::vector<std::uint8_t> owned_container(object_count, 0U);
    std::vector<std::uint8_t> owned_family(object_count, 0U);
    std::vector<std::uint8_t> alias_role(signal_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        for (const auto signal : element_signals[object]) {
            if (signal < signal_count) {
                alias_role[signal] = 1U;
            }
        }
        if (packed_alias[object] && *packed_alias[object] < signal_count) {
            alias_role[*packed_alias[object]] = 1U;
        }
    }
    for (std::size_t signal = 0U; signal < signal_count; ++signal) {
        if (proxy_signal[signal] != 0U) {
            alias_role[signal] = 1U;
        }
    }
    for (const auto& alias : container_aggregate_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_role[alias.signal] = 1U;
        }
    }
    for (bool revalidate = true; revalidate;) {
    revalidate = false;
    for (bool changed = true; changed;) {
        changed = false;
        mark_edge_sensitive();
        std::ranges::fill(owned, 0U);
        std::ranges::fill(owned_container, 0U);
        std::ranges::fill(owned_family, 0U);
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (alias_role[signal] == 0U
                && !graph_signals[signal].writers.empty()
                && signal_ok(signal)) {
                owned[signal] = 1U;
            }
        }
        // A wired formal is kernel-owned with its source; the program is its
        // only writer.
        for (bool again = true; again;) {
            again = false;
            for (const auto& wiring : wirings) {
                if (!wiring.live || owned[wiring.target] != 0U
                    || owned[wiring.source] == 0U
                    || alias_role[wiring.target] != 0U
                    || !basic_ok(wiring.target)
                    || !std::ranges::all_of(
                        graph_signals[wiring.target].writers,
                        [&](const auto& writer) {
                            return writer.process == wiring.program;
                        })) {
                    continue;
                }
                owned[wiring.target] = 1U;
                again = true;
            }
        }
        for (std::size_t object = 0U; object < object_count; ++object) {
            if (container_shape_ok[object] == 0U || (vhdl_mode && !mixed)
                || (mixed && std::ranges::any_of(accessors[object],
                    [&](const ProcessId process) {
                        return candidates[process] && candidates[process]->vhdl;
                    }))
                || std::ranges::any_of(accessors[object],
                    [&](const ProcessId process) {
                        return !candidates[process];
                    })) {
                continue;
            }
            if (packed_alias[object]) {
                const auto signal = *packed_alias[object];
                if (signal_ok(signal)) {
                    owned[signal] = 1U;
                    owned_container[object] = 1U;
                }
                continue;
            }
            if (has_element_alias[object] == 0U) {
                // An opaque process (publish_all) may touch any container,
                // for example a fixed array passed by reference to a method.
                // Privately stored elements have no signal through which the
                // host could write back, so such a container stays on the
                // host; signal-backed containers are imported as signals.
                if (publish_all) {
                    continue;
                }
                owned_container[object] = 1U;
                continue;
            }
            if (element_complete[object] == 0U) {
                continue;
            }
            const auto* family = family_of_object[object];
            if (family != nullptr
                && (!family->complete || family->proxy >= signal_count
                    || !signal_ok(family->proxy)
                    || edge_sensitive_member(family->proxy))) {
                continue;
            }
            if (!std::ranges::all_of(element_signals[object], signal_ok)) {
                continue;
            }
            for (const auto signal : element_signals[object]) {
                owned[signal] = 1U;
            }
            owned_container[object] = 1U;
            owned_family[object] = family != nullptr ? 1U : 0U;
        }
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (candidates[index]
                && std::ranges::any_of(container_uses[index],
                    [&](const ContainerObjectId object) {
                        return object >= object_count
                            || owned_container[object] == 0U;
                    })) {
                if (candidates[index]->kind == behavioral_kind
                    && std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr) {
                    std::cerr << "fsim-kernel-plan: behavioral container process="
                              << views[index].name() << '\n';
                }
                candidates[index].reset();
                reject(index, "container_unowned");
                changed = true;
            }
        }
    }
    // A wiring needs its source and its formal kernel-owned; otherwise its
    // program runs on the host and the source stays a host signal.
    for (auto& wiring : wirings) {
        if (wiring.live
            && (owned[wiring.source] == 0U || owned[wiring.target] == 0U)) {
            wiring.live = false;
            host_touched[wiring.source] = 1U;
            wiring_of_target[wiring.target] = -1;
            revalidate = true;
        }
    }
    }

    const bool diagnose = std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr;
    if (diagnose) {
        std::map<std::string, std::size_t> reasons;
        std::map<std::string, std::string> examples;
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (graph_signals[signal].writers.empty() || owned[signal] != 0U
                || proxy_signal[signal] != 0U) {
                continue;
            }
            const auto& node = graph_signals[signal];
            std::string reason = alias_role[signal] != 0U ? "alias:" : "";
            if (!basic_ok(signal)) {
                reason += host_touched[signal] != 0U ? "host_touched" : "basic";
            } else if (std::ranges::any_of(node.writers, [&](const auto& writer) {
                           return !candidates[writer.process];
                       })) {
                reason += "host_writer";
            } else if (!writers_ok(signal)) {
                reason += "undeclared_or_resolved";
            } else {
                reason += "container_unit";
            }
            ++reasons[reason];
            examples.emplace(reason, signals_[signal].name);
        }
        for (const auto& [reason, count] : reasons) {
            std::cerr << "fsim-kernel-plan: unowned reason=" << reason
                      << " count=" << count << " example=" << examples[reason]
                      << '\n';
        }
        std::map<std::string, std::size_t> rejected;
        std::map<std::string, std::string> rejected_examples;
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (candidates[index] || !views[index].valid()) {
                continue;
            }
            const auto& operations = views[index].operations();
            std::string reason = "shape";
            std::set<std::string> unsupported;
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                const auto operation = operations.expanded(op);
                const bool vhdl_process = views[index].scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog;
                if (!(vhdl_process
                            ? static_kernel_vhdl_operation_supported(operation)
                            : static_kernel_operation_supported(operation))
                    && !operation_holds<WaitSensitivity>(operation)
                    && !operation_holds<Halt>(operation)) {
                    unsupported.insert(operation_type_name(operation));
                }
            }
            if (!unsupported.empty()) {
                reason = "ops:";
                for (const auto& name : unsupported) {
                    reason += name + ",";
                }
            }
            if (reason == "shape" && !container_uses[index].empty()) {
                reason = "container";
            }
            ++rejected[reason];
            rejected_examples.emplace(reason, views[index].name());
        }
        if (const char* dump = std::getenv("FSIM_PROFILE_KERNEL_PLAN_DUMP")) {
            std::size_t dumped = 0U;
            for (std::size_t index = 0U; index < process_count && dumped < 12U;
                 ++index) {
                if (!views[index].valid()
                    || views[index].name().find(dump) == std::string::npos) {
                    continue;
                }
                ++dumped;
                const auto& operations = views[index].operations();
                std::cerr << "fsim-kernel-plan-dump: " << views[index].name()
                          << " candidate=" << (candidates[index] ? 1 : 0)
                          << " domain="
                          << static_cast<int>(views[index].scheduling_domain())
                          << " sens=";
                for (const auto& entry : views[index].static_sensitivity()) {
                    std::cerr << entry.signal << '/'
                              << static_cast<int>(entry.edge) << ',';
                }
                std::cerr << " kinds=";
                for (const auto kind : views[index].register_value_kinds()) {
                    std::cerr << static_cast<int>(kind);
                }
                std::cerr << '\n';
                for (std::size_t op = 0U; op < operations.size(); ++op) {
                    visit_operation([&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        int status = 0;
                        char* name = abi::__cxa_demangle(
                            typeid(T).name(), nullptr, nullptr, &status);
                        std::string text = name != nullptr ? name : "?";
                        std::free(name);
                        if (const auto colon = text.rfind("::");
                            colon != std::string::npos) {
                            text = text.substr(colon + 2U);
                        }
                        std::cerr << "  " << op << ' ' << text;
                        if constexpr (requires { value.destination; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.destination)>>) {
                                std::cerr << " d=" << value.destination;
                            }
                        }
                        if constexpr (requires { value.signal; }) {
                            if constexpr (std::is_same_v<std::decay_t<
                                              decltype(value.signal)>, SignalId>) {
                                std::cerr << " sig=" << value.signal << '('
                                          << signals_[value.signal].name << ')';
                            }
                        }
                        if constexpr (requires { value.source; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.source)>>) {
                                std::cerr << " src=" << value.source;
                            }
                        }
                        if constexpr (requires { value.left; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.left)>>) {
                                std::cerr << " l=" << value.left;
                            }
                        }
                        if constexpr (requires { value.right; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.right)>>) {
                                std::cerr << " r=" << value.right;
                            }
                        }
                        if constexpr (requires { value.condition; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.condition)>>) {
                                std::cerr << " c=" << value.condition;
                            }
                        }
                        if constexpr (requires { value.target; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.target)>>) {
                                std::cerr << " t=" << value.target;
                            }
                        }
                        if constexpr (requires { value.offset; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.offset)>>) {
                                std::cerr << " off=" << value.offset;
                            }
                        }
                        if constexpr (requires { value.width; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.width)>>) {
                                std::cerr << " w=" << value.width;
                            }
                        }
                        if constexpr (requires { value.operation; }) {
                            std::cerr << " op=" << static_cast<int>(value.operation);
                        }
                        if constexpr (requires { value.lhs; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.lhs)>>) {
                                std::cerr << " lhs=" << value.lhs
                                          << " rhs=" << value.rhs;
                            }
                        }
                        if constexpr (requires { value.delay; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.delay)>>) {
                                std::cerr << " delay=" << value.delay;
                            }
                        }
                        std::cerr << '\n';
                    }, operations.expanded(op));
                }
            }
        }
        for (const auto& [reason, count] : rejected) {
            std::cerr << "fsim-kernel-plan: rejected reason=" << reason
                      << " count=" << count
                      << " example=" << rejected_examples[reason] << '\n';
        }
    }

    // Hosted VHDL processes (engine-v4-semantics S25a): in a VHDL or mixed
    // kernel every VHDL process that is not a native member is hosted, so the
    // kernel schedules it in its delta rounds and the reference evaluator
    // executes it. The delta exactness the two rules below protect then holds
    // without a closed kernel: a hosted process wakes in the delta after the
    // update that woke it (the kernel yields to the host once per round) and
    // writes through the host's update phase of its own delta, and a signal a
    // member writes and another member reads crosses the host in that phase
    // too. FSIM_STATIC_KERNEL_HOSTED_VHDL=0 restores the refusals.
    const bool hosted_vhdl_enabled = vhdl_mode
        && [] {
               const char* text = std::getenv("FSIM_STATIC_KERNEL_HOSTED_VHDL");
               return text == nullptr || std::string_view { text } != "0";
           }();
    // A postponed VHDL process is hosted like any other (S25a): the kernel
    // keeps it in its postponed region list and runs it after the last delta
    // cycle of the time step (1076-2019 14.7.5.3).
    // FSIM_STATIC_KERNEL_HOSTED_POSTPONED=0 restores the vhdl_postponed
    // refusal.
    const bool hosted_postponed_enabled = hosted_vhdl_enabled
        && [] {
               const char* text
                   = std::getenv("FSIM_STATIC_KERNEL_HOSTED_POSTPONED");
               return text == nullptr || std::string_view { text } != "0";
           }();
    if (vhdl_mode) {
        // Delta exactness holds only inside a closed VHDL kernel: every VHDL
        // process is a member, and every signal one member writes and
        // another reads is kernel-owned (a host round trip would add deltas).
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (views[index].valid() && !candidates[index]
                && host_program[index] == 0U
                && views[index].scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog) {
                // A postponed process runs in the last delta of the time
                // step (1076-2019 14.7.5.3): the kernel has no end-of-slot
                // region yet (workstream C5c, migration phase P4), so it
                // keeps the design off the kernel.
                if (hosted_vhdl_enabled
                    && (!views[index].postponed() || hosted_postponed_enabled)) {
                    continue;
                }
                if (diagnose) {
                    std::cerr << "fsim-kernel-plan: vhdl_partial process="
                              << views[index].name() << '\n';
                }
                return disable(hosted_vhdl_enabled ? "vhdl_postponed"
                                                   : "vhdl_partial");
            }
        }
        // Signals a VHDL behavioral thread waits on (its wait statements
        // and implicit sensitivity wait): a wait is a read for this rule.
        std::vector<std::uint8_t> vhdl_waited(signal_count, 0U);
        for (const auto& candidate : candidates) {
            if (candidate && candidate->vhdl
                && candidate->kind == behavioral_kind) {
                for (const auto signal : candidate->waited) {
                    vhdl_waited[signal] = 1U;
                }
                for (const auto& entry : candidate->sensitivity) {
                    vhdl_waited[entry.signal] = 1U;
                }
            }
        }
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (owned[signal] != 0U) {
                continue;
            }
            const auto& node = graph_signals[signal];
            const auto member_access = [&](const auto& accesses) {
                return std::ranges::any_of(accesses, [&](const auto& access) {
                    return access.process < process_count
                        && candidates[access.process];
                });
            };
            // SystemVerilog members exchange host signals through the host
            // as in a SystemVerilog kernel; only VHDL accesses need the
            // closed kernel.
            const auto vhdl_access = [&](const auto& accesses) {
                return std::ranges::any_of(accesses, [&](const auto& access) {
                    return access.process < process_count
                        && candidates[access.process]
                        && candidates[access.process]->vhdl;
                });
            };
            if (!hosted_vhdl_enabled && member_access(node.writers)
                && (member_access(node.readers) || vhdl_waited[signal] != 0U)
                && (!mixed || vhdl_access(node.writers)
                    || vhdl_access(node.readers)
                    || vhdl_waited[signal] != 0U)) {
                if (diagnose) {
                    const auto& info = signals_[signal];
                    std::cerr << "fsim-kernel-plan: vhdl_unowned signal="
                              << info.name << " basic=" << basic_ok(signal)
                              << " writers_ok=" << writers_ok(signal)
                              << " host_touched=" << int { host_touched[signal] }
                              << " fork=" << node.dynamic_fork_writers
                              << " partial=" << node.partial_projected_transactions
                              << " kind=" << static_cast<int>(info.value_kind)
                              << " resolution=" << static_cast<int>(info.resolution)
                              << " implicit=" << info.implicit_driver.has_value()
                              << " width=" << signals_[signal].initial_value.width()
                              << " alias=" << int { alias_role[signal] }
                              << " writers=";
                    for (const auto& writer : node.writers) {
                        std::cerr << views[writer.process].name() << ',';
                    }
                    std::cerr << '\n';
                }
                return disable("vhdl_unowned");
            }
        }
    }

    // Hosted processes (StaticKernelMemberKind::hosted, engine-v4-semantics
    // S25): every valid SystemVerilog process that is neither a native member
    // nor a host program is scheduled by the kernel and executed by the
    // reference evaluator, so the host scheduler runs no SimIR process of an
    // admitted kernel. This only widens an admitted design; every design-level
    // refusal above is unchanged. VHDL and mixed kernels (delta rounds) keep
    // their host processes (Phase 2).
    // BEGIN P4 hosted regions: the processes of the observed, reactive and
    // postponed regions, final blocks and package static initializers are
    // hosted too (S25 region model). FSIM_STATIC_KERNEL_HOSTED_REGIONS=0
    // keeps them on the host scheduler.
    std::vector<std::uint8_t> hosted(process_count, 0U);
    {
        const char* hosted_text = std::getenv("FSIM_STATIC_KERNEL_HOSTED");
        const bool hosted_enabled = !vhdl_mode
            && (hosted_text == nullptr
                || std::string_view { hosted_text } != "0");
        const char* regions_text
            = std::getenv("FSIM_STATIC_KERNEL_HOSTED_REGIONS");
        const bool regions_enabled = regions_text == nullptr
            || std::string_view { regions_text } != "0";
        for (std::size_t index = 0U; hosted_enabled && index < process_count;
             ++index) {
            const auto& view = views[index];
            if (!view.valid() || candidates[index] || host_program[index] != 0U
                || view.scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog) {
                continue;
            }
            if (!regions_enabled) {
                const char* region = view.observed()   ? "observed"
                    : view.reactive()                  ? "reactive"
                    : view.postponed()                 ? "postponed"
                    : view.final()                     ? "final"
                                                       : nullptr;
                if (region != nullptr) {
                    if (census) {
                        rejection[index]
                            = std::string { "hosted_region_" } + region;
                    }
                    continue;
                }
                if (view.startup_phase()
                    == ProcessStartupPhase::package_static_initializer) {
                    continue;
                }
            }
            hosted[index] = 1U;
        }
    }
    // END P4 hosted regions

    // Hosted VHDL processes (S25a): the VHDL processes that are neither
    // native members nor host programs. They read and write host signals
    // (host_touched above), wake through the host's signal fanout, and the
    // kernel runs them in its delta round. Host programs (resolvers and
    // conversion drivers) run inside the host's update commit
    // (run_vhdl_resolver) and are not scheduled processes, so they stay as
    // they are until the kernel owns that commit (C5b).
    for (std::size_t index = 0U; hosted_vhdl_enabled && index < process_count;
         ++index) {
        const auto& view = views[index];
        if (!view.valid() || candidates[index] || host_program[index] != 0U
            || view.scheduling_domain()
                == ProcessSchedulingDomain::systemverilog
            || (view.postponed() && !hosted_postponed_enabled)) {
            continue;
        }
        hosted[index] = 1U;
    }

    // Implicit-signal drivers (S25a): the driver process of S'DELAYED,
    // S'TRANSACTION, S'STABLE and S'QUIET is a scheduled process of the
    // SystemVerilog scheduling domain with no initial run (it is not a
    // member: it is not initialized). The VHDL kernel hosts it like a VHDL
    // process: it wakes in the delta after the source's update phase
    // (queue_next_delta: a wake from another domain runs in the next delta,
    // 1076-2019 14.7.5.3) and its assignments are the host's staged updates.
    // FSIM_STATIC_KERNEL_HOSTED_IMPLICIT=0 keeps it on the host.
    {
        const char* implicit_text
            = std::getenv("FSIM_STATIC_KERNEL_HOSTED_IMPLICIT");
        const bool implicit_enabled = hosted_vhdl_enabled
            && (implicit_text == nullptr
                || std::string_view { implicit_text } != "0");
        constexpr std::string_view implicit_suffix { ".$implicit_driver" };
        for (std::size_t index = 0U; implicit_enabled && index < process_count;
             ++index) {
            const auto& view = views[index];
            if (!view.valid() || candidates[index] || host_program[index] != 0U
                || view.scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog
                || view.initialize() || view.postponed() || view.observed()
                || view.reactive() || view.final()) {
                continue;
            }
            const std::string_view name { view.name() };
            if (name.size() > implicit_suffix.size()
                && name.ends_with(implicit_suffix)) {
                hosted[index] = 1U;
            }
        }
    }

    // 3. Members, specification, host and boundaries.
    auto spec = std::make_shared<StaticKernelRuntimeSpec>();
    std::vector<std::uint8_t> member(process_count, 0U);
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (!candidates[index]) {
            continue;
        }
        member[index] = 1U;
        auto& candidate = *candidates[index];
        StaticKernelMemberSpec runtime_member;
        runtime_member.process = candidate.process;
        runtime_member.kind = static_cast<StaticKernelMemberKind>(candidate.kind);
        runtime_member.run_at_start = candidate.run_at_start;
        runtime_member.start_late = candidate.start_late;
        runtime_member.body_begin = candidate.body_begin;
        runtime_member.body_end = candidate.body_end;
        runtime_member.exit_alt = candidate.exit_alt;
        runtime_member.vhdl = candidate.vhdl;
        runtime_member.sensitivity = std::move(candidate.sensitivity);
        spec->members.push_back(std::move(runtime_member));
    }
    // A design whose processes are all hosted still runs on the kernel: it
    // schedules them (engine-v4-semantics S25).
    if (spec->members.empty()
        && std::ranges::none_of(hosted, [](const auto flag) { return flag != 0U; })) {
        // Every valid process is a host program (port-association resolver,
        // conversion driver): run inline by the host's update commit, not a
        // scheduled process. There is nothing for the kernel to schedule, so
        // the design is admitted trivially, as one with no processes; the
        // census still counts the programs as host processes.
        const bool only_host_programs = [&] {
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (views[index].valid() && host_program[index] == 0U) {
                    return false;
                }
            }
            return true;
        }();
        if (only_host_programs) {
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (views[index].valid()) {
                    reject(index, "port_program");
                }
            }
            StaticKernelPlan trivial;
            trivial.disabled_reason = "trivial_host_programs_only";
            take_census(trivial, nullptr);
            return trivial;
        }
        return disable("no_members");
    }
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (hosted[index] != 0U) {
            StaticKernelMemberSpec hosted_member;
            hosted_member.process = static_cast<ProcessId>(index);
            hosted_member.kind = StaticKernelMemberKind::hosted;
            spec->hosted.push_back(std::move(hosted_member));
        }
    }
    // A VHDL kernel's hosted SystemVerilog-domain processes (implicit-signal
    // drivers) follow its VHDL ones: the synthetic host of a design with no
    // member is shaped from the first hosted process.
    if (vhdl_mode) {
        std::ranges::stable_partition(spec->hosted,
            [&](const StaticKernelMemberSpec& entry) {
                return views[entry.process].scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog;
            });
        // Implicit drivers alone give a VHDL kernel nothing to shape its
        // host from.
        if (spec->members.empty() && !spec->hosted.empty()
            && views[spec->hosted.front().process].scheduling_domain()
                == ProcessSchedulingDomain::systemverilog) {
            return disable("no_members");
        }
    }
    plan.hosted_processes = spec->hosted.size();
    spec->vhdl = vhdl_mode;
    spec->mixed = mixed;
    spec->import_host_writes = publish_all;
    std::vector<std::uint8_t> wired_program(process_count, 0U);
    for (const auto& wiring : wirings) {
        if (wiring.live) {
            wired_program[wiring.program] = 1U;
        }
    }
    std::vector<std::uint8_t> virtual_owned(signal_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        if (owned_container[object] == 0U) {
            continue;
        }
        StaticKernelContainerSpec container;
        container.object = static_cast<ContainerObjectId>(object);
        if (packed_alias[object]) {
            container.storage = StaticKernelContainerStorage::packed_signal;
            container.packed_signal = *packed_alias[object];
        } else if (has_element_alias[object] != 0U) {
            container.storage = StaticKernelContainerStorage::element_signals;
            container.element_signals = element_signals[object];
        }
        if (!accessors[object].empty()
            || container.storage != StaticKernelContainerStorage::elements) {
            spec->containers.push_back(std::move(container));
        }
        if (owned_family[object] != 0U) {
            const auto& family = *family_of_object[object];
            StaticKernelAliasFamily runtime_family;
            runtime_family.proxy = family.proxy;
            runtime_family.width = family.width;
            for (const auto& leaf : family.leaves) {
                runtime_family.leaves.push_back(
                    { leaf.signal, leaf.offset, leaf.width });
            }
            virtual_owned[family.proxy] = 1U;
            spec->families.push_back(std::move(runtime_family));
        }
    }
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        if (owned[signal] == 0U) {
            continue;
        }
        spec->owned_signals.push_back(signal);
        // VHDL writers publish under merged writer regions; SystemVerilog
        // members of a mixed kernel keep their declared driver regions.
        if (vhdl_mode && (!mixed || vhdl_written(signal))) {
            const auto width = static_cast<std::uint32_t>(
                signals_[signal].initial_value.width());
            std::map<ProcessId, std::vector<std::pair<std::uint32_t,
                std::uint32_t>>> per_process;
            for (const auto& writer : graph_signals[signal].writers) {
                per_process[writer.process].emplace_back(
                    writer.width == 0U ? 0U : writer.offset,
                    writer.width == 0U ? width : writer.offset + writer.width);
            }
            for (auto& [process, ranges] : per_process) {
                std::ranges::sort(ranges);
                std::pair<std::uint32_t, std::uint32_t> current = ranges.front();
                const auto emit = [&] {
                    spec->writer_regions.push_back({ signal, process,
                        current.first, current.second - current.first });
                };
                for (std::size_t index = 1U; index < ranges.size(); ++index) {
                    if (ranges[index].first <= current.second) {
                        current.second
                            = std::max(current.second, ranges[index].second);
                    } else {
                        emit();
                        current = ranges[index];
                    }
                }
                emit();
            }
        }
        if (publish_all
            || std::ranges::any_of(graph_signals[signal].readers,
                [&](const auto& reader) {
                    return reader.process >= process_count
                        || (member[reader.process] == 0U
                            && wired_program[reader.process] == 0U);
                })) {
            spec->boundary_outputs.push_back(signal);
        }
    }
    // The kernel's wiring is the only writer of a wired formal; the program
    // is its driver identity on the host.
    for (const auto& wiring : wirings) {
        if (wiring.live
            && std::ranges::none_of(spec->writer_regions,
                [&](const auto& region) {
                    return region.signal == wiring.target;
                })) {
            spec->writer_regions.push_back({ wiring.target, wiring.program,
                0U,
                static_cast<std::uint32_t>(
                    signals_[wiring.target].initial_value.width()) });
        }
    }
    std::vector<std::uint8_t> input(signal_count, 0U);
    std::vector<Sensitivity> host_sensitivity;
    const auto host_input = [&](const SignalId signal) {
        if (owned[signal] == 0U && virtual_owned[signal] == 0U
            && input[signal] == 0U) {
            input[signal] = 1U;
            ++plan.boundary_inputs;

            host_sensitivity.push_back({ signal, EdgeKind::any });
        }
    };
    for (const auto& entry : spec->members) {
        for (const auto& sensitivity : entry.sensitivity) {
            host_input(sensitivity.signal);
        }
    }
    // A behavioral thread's dynamic wait on a host signal also needs the
    // host to activate the kernel when that signal changes.
    for (const auto& candidate : candidates) {
        if (candidate && candidate->kind == behavioral_kind) {
            for (const auto signal : candidate->waited) {
                host_input(signal);
            }
        }
    }
    // A forced value hides the driver's (IEEE 1800-2023 10.6.2); a port
    // segment force reaches nets by another name, so any force in the design
    // turns the mirroring below off.
    const bool forces = std::ranges::any_of(
        std::views::iota(std::size_t { 0U }, process_count),
        [&](const std::size_t index) {
            if (!views[index].valid()) {
                return false;
            }
            const auto& operations = views[index].operations();
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                const auto& operation = operations.expanded(op);
                if (operation_get_if<ForceSignalSlice>(&operation) != nullptr
                    || operation_get_if<ReleaseSignalSlice>(&operation) != nullptr) {
                    return true;
                }
            }
            return false;
        });
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        const auto& node = graph_signals[signal];
        // A foreign library may deposit into a signal no process writes
        // between kernel rounds.
        // A force writes the signal as well (IEEE 1800-2023 10.6.2): a
        // hosted process that forces an input changes it inside the
        // activation, where only the written inputs are rescanned.
        if (!forces && !access.foreign && owned[signal] == 0U
            && virtual_owned[signal] == 0U
            && node.writers.empty()
            && std::ranges::any_of(node.readers, [&](const auto& reader) {
                   return reader.process < process_count && member[reader.process] != 0U;
               })) {
            spec->unwritten_inputs.push_back(signal);
        }
        // A blocking or continuous write is visible to the members that run
        // after it (IEEE 1800-2023 4.7, 10.3.2, 10.4.1). A host signal with
        // one driving member process takes that driver's value, so the
        // kernel can mirror its staged Active updates at once.
        // A foreign library may deposit or release a signal, and a module
        // path delays the host's commit of its destination, so the host
        // does not apply a member's update unchanged and at once.
        if (!forces && !access.foreign && path_destination[signal] == 0U
            && owned[signal] == 0U && virtual_owned[signal] == 0U
            && !node.writers.empty()
            // One driver of an unresolved variable or a plain net (no pull,
            // no charge storage) resolves to that driver's value.
            && (signals_[signal].resolution == ResolutionKind::none
                || (signals_[signal].resolution == ResolutionKind::sv_wire
                    && !signals_[signal].implicit_driver
                    && !signals_[signal].charge_strength))
            && std::ranges::all_of(node.writers, [&](const auto& writer) {
                   return writer.process == node.writers.front().process
                       && writer.process < process_count
                       && member[writer.process] != 0U;
               })
            && std::ranges::any_of(node.readers, [&](const auto& reader) {
                   return reader.process < process_count && member[reader.process] != 0U;
               })) {
            spec->sole_writer_inputs.push_back(signal);
        }
    }
    // The kernel runs as a host process whose stub replaces the first
    // member's program. A design of hosted processes only has no member to
    // replace: the stub then runs as a process of its own, appended after
    // the design's processes (plan.host == process_count; create_interpreter
    // adds it), shaped from the first hosted process.
    if (spec->members.empty() && spec->hosted.empty()) {
        return disable("no_members");
    }
    plan.host = spec->members.empty() ? static_cast<ProcessId>(process_count)
                                      : spec->members.front().process;
    plan.host_program = host_stub(
        views[spec->members.empty() ? spec->hosted.front().process : plan.host],
        std::move(host_sensitivity));
    if (spec->members.empty()) {
        plan.host_program = synthetic_host(std::move(plan.host_program));
    }
    plan.members = spec->members.size();
    plan.owned_signals = spec->owned_signals.size();
    plan.boundary_outputs = spec->boundary_outputs.size();
    plan.owned_containers = spec->containers.size();
    plan.spec = std::move(spec);
    // Commit programs (S25b): the programs of the host's update commit are
    // part of the kernel's plan. They are not scheduled processes and have
    // no wake of their own: the commit runs each at its reference position
    // inside the update phase that follows a kernel delta (IEEE 1076-2019
    // 14.7.3 for effective values, conversion functions and resolution;
    // IEEE 1800-2023 23.3.3 and 28 for port coercion and inout networks).
    // The census therefore counts them as kernel-owned rows, not as host
    // processes. Whatever the candidate classification said about their
    // operations is replaced by the program's kind.
    std::vector<std::uint8_t> kernel_owned = hosted;
    for (std::size_t index = 0U; census && index < process_count; ++index) {
        if (member[index] != 0U || host_program[index] == 0U) {
            continue;
        }
        const auto& view = views[index];
        kernel_owned[index] = 1U;
        rejection[index] = !view.valid()                          ? "commit_program"
            : view.switch_source() || view.switch_target()        ? "commit_program_switch"
            : !view.initialize() && view.operations().size() == 1U
                ? "commit_program_stub"
                : "commit_program";
    }
    take_census(plan, &member, &kernel_owned);
    return plan;
    };
    auto plan = plan_for(true);
    if (plan.disabled && mixed_planned) {
        plan = plan_for(false);
    }
    return plan;
}


namespace detail {
namespace {

constexpr std::string_view plan_magic { "fsim-static-kernel-plan-v4" };

class PlanWriter {
public:
    template <typename T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    void put(const T value)
    {
        const auto raw = static_cast<std::uint64_t>(value);
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            bytes.push_back(static_cast<char>((raw >> shift) & 0xffU));
        }
    }
    template <typename T, typename F>
    void list(const std::vector<T>& items, F&& each)
    {
        put(items.size());
        for (const auto& item : items) {
            each(item);
        }
    }
    void sensitivities(const std::vector<runtime::simir::Sensitivity>& items)
    {
        list(items, [&](const runtime::simir::Sensitivity& entry) {
            put(entry.signal);
            put(entry.edge);
            put(entry.offset);
            put(entry.width);
        });
    }
    std::string bytes;
};

class PlanReader {
public:
    explicit PlanReader(const std::string_view bytes) : bytes_ { bytes } { }

    template <typename T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    bool get(T& value)
    {
        if (bytes_.size() - at_ < 8U) {
            return false;
        }
        std::uint64_t raw { };
        for (unsigned index = 0U; index < 8U; ++index) {
            raw |= static_cast<std::uint64_t>(
                       static_cast<unsigned char>(bytes_[at_ + index]))
                << (8U * index);
        }
        at_ += 8U;
        if constexpr (std::is_same_v<T, bool>) {
            if (raw > 1U) {
                return false;
            }
            value = raw != 0U;
        } else if constexpr (std::is_enum_v<T>) {
            using U = std::underlying_type_t<T>;
            if (raw > std::numeric_limits<U>::max()) {
                return false;
            }
            value = static_cast<T>(static_cast<U>(raw));
        } else {
            if (raw > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return false;
            }
            value = static_cast<T>(raw);
        }
        return true;
    }
    /// Each item is at least eight bytes, so a count beyond the remaining
    /// bytes is corrupt (and never allocates).
    template <typename T, typename F>
    bool list(std::vector<T>& items, F&& each)
    {
        std::size_t count { };
        if (!get(count) || count > (bytes_.size() - at_) / 8U) {
            return false;
        }
        items.resize(count);
        for (auto& item : items) {
            if (!each(item)) {
                return false;
            }
        }
        return true;
    }
    bool sensitivities(std::vector<runtime::simir::Sensitivity>& items)
    {
        return list(items, [&](runtime::simir::Sensitivity& entry) {
            return get(entry.signal) && get(entry.edge) && get(entry.offset)
                && get(entry.width);
        });
    }
    [[nodiscard]] bool done() const noexcept { return at_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t at_ { };
};

} // namespace

std::string static_kernel_plan_identity()
{
    // The planner and the environment knobs that change what it plans.
    std::string identity { plan_magic };
    identity += ";" __DATE__ " " __TIME__;
    for (const char* name : { "FSIM_STATIC_KERNEL_BEHAVIORAL",
             "FSIM_STATIC_KERNEL_MIXED", "FSIM_STATIC_KERNEL_PUBLISH_ALL",
             "FSIM_STATIC_KERNEL_ACCESS_PLAN", "FSIM_STATIC_KERNEL_HOSTED",
             "FSIM_STATIC_KERNEL_HOSTED_REGIONS",
             "FSIM_STATIC_KERNEL_HOSTED_VHDL",
             "FSIM_STATIC_KERNEL_HOSTED_POSTPONED" }) {
        const char* value = std::getenv(name);
        identity += ";";
        identity += name;
        identity += "=";
        identity += value != nullptr ? value : "-";
    }
    return identity;
}

std::string serialize_static_kernel_plan(const StaticKernelPlan& plan)
{
    PlanWriter out;
    out.bytes.assign(plan_magic);
    if (plan.disabled || !plan.spec) {
        return { };
    }
    const auto& spec = *plan.spec;
    out.put(plan.host);
    out.sensitivities(plan.host_program.static_sensitivity);
    out.list(spec.members, [&](const runtime::simir::StaticKernelMemberSpec& member) {
        out.put(member.process);
        out.put(member.kind);
        out.put(member.body_begin);
        out.put(member.body_end);
        out.put(member.exit_alt);
        out.put(member.run_at_start);
        out.sensitivities(member.sensitivity);
        out.put(member.partition);
        out.put(member.vhdl);
        out.put(member.start_late);
    });
    out.list(spec.hosted, [&](const runtime::simir::StaticKernelMemberSpec& member) {
        out.put(member.process);
    });
    out.list(spec.owned_signals, [&](const auto signal) { out.put(signal); });
    out.list(spec.boundary_outputs, [&](const auto signal) { out.put(signal); });
    out.list(spec.families, [&](const runtime::simir::StaticKernelAliasFamily& family) {
        out.put(family.proxy);
        out.put(family.width);
        out.list(family.leaves, [&](const runtime::simir::StaticKernelAliasLeaf& leaf) {
            out.put(leaf.signal);
            out.put(leaf.offset);
            out.put(leaf.width);
        });
    });
    out.list(spec.containers, [&](const runtime::simir::StaticKernelContainerSpec& container) {
        out.put(container.object);
        out.put(container.storage);
        out.list(container.element_signals, [&](const auto signal) { out.put(signal); });
        out.put(container.packed_signal);
    });
    out.put(spec.vhdl);
    out.put(spec.mixed);
    out.put(spec.import_host_writes);
    out.list(spec.writer_regions, [&](const runtime::simir::StaticKernelWriterRegion& region) {
        out.put(region.signal);
        out.put(region.process);
        out.put(region.offset);
        out.put(region.width);
    });
    out.list(spec.unwritten_inputs, [&](const auto signal) { out.put(signal); });
    out.list(spec.sole_writer_inputs, [&](const auto signal) { out.put(signal); });
    out.put(plan.boundary_inputs);
    return std::move(out.bytes);
}

std::optional<StaticKernelPlan> restore_static_kernel_plan(
    const ElaboratedDesign& design, const std::string_view bytes)
{
    using namespace runtime::simir;
    if (!bytes.starts_with(plan_magic)) {
        return std::nullopt;
    }
    PlanReader in { bytes.substr(plan_magic.size()) };
    const auto processes = ElaboratedDesignProcessAccess::process_count(design);
    const auto signals = design.signals().size();
    const auto containers = design.container_objects().size();
    const auto signal_ok = [&](const SignalId signal) { return signal < signals; };
    const auto sensitivity_ok = [&](const std::vector<Sensitivity>& items) {
        return std::ranges::all_of(items, [&](const Sensitivity& entry) {
            return signal_ok(entry.signal);
        });
    };
    StaticKernelPlan plan;
    auto spec = std::make_shared<StaticKernelRuntimeSpec>();
    std::vector<Sensitivity> host_sensitivity;
    const auto signal_list = [&](std::vector<SignalId>& items) {
        return in.list(items, [&](SignalId& signal) {
            return in.get(signal) && signal_ok(signal);
        });
    };
    const bool read = in.get(plan.host) && plan.host <= processes
        && in.sensitivities(host_sensitivity) && sensitivity_ok(host_sensitivity)
        && in.list(spec->members, [&](StaticKernelMemberSpec& member) {
               return in.get(member.process) && member.process < processes
                   && in.get(member.kind)
                   && member.kind <= StaticKernelMemberKind::behavioral
                   && in.get(member.body_begin) && in.get(member.body_end)
                   && in.get(member.exit_alt) && in.get(member.run_at_start)
                   && in.sensitivities(member.sensitivity)
                   && sensitivity_ok(member.sensitivity)
                   && in.get(member.partition) && in.get(member.vhdl)
                   && in.get(member.start_late);
           })
        && in.list(spec->hosted, [&](StaticKernelMemberSpec& member) {
               member.kind = StaticKernelMemberKind::hosted;
               return in.get(member.process) && member.process < processes;
           })
        && signal_list(spec->owned_signals) && signal_list(spec->boundary_outputs)
        && in.list(spec->families, [&](StaticKernelAliasFamily& family) {
               return in.get(family.proxy) && signal_ok(family.proxy)
                   && in.get(family.width)
                   && in.list(family.leaves, [&](StaticKernelAliasLeaf& leaf) {
                          return in.get(leaf.signal) && signal_ok(leaf.signal)
                              && in.get(leaf.offset) && in.get(leaf.width);
                      });
           })
        && in.list(spec->containers, [&](StaticKernelContainerSpec& container) {
               return in.get(container.object) && container.object < containers
                   && in.get(container.storage)
                   && container.storage <= StaticKernelContainerStorage::packed_signal
                   && signal_list(container.element_signals)
                   && in.get(container.packed_signal);
           })
        && in.get(spec->vhdl) && in.get(spec->mixed)
        && in.get(spec->import_host_writes)
        && in.list(spec->writer_regions, [&](StaticKernelWriterRegion& region) {
               return in.get(region.signal) && signal_ok(region.signal)
                   && in.get(region.process) && region.process < processes
                   && in.get(region.offset) && in.get(region.width);
           })
        && signal_list(spec->unwritten_inputs)
        && signal_list(spec->sole_writer_inputs)
        && in.get(plan.boundary_inputs) && in.done();
    // The host is the first member, or a process of its own after the
    // design's processes when every process is hosted (plan_static_kernel).
    const bool synthetic_host_process = spec->members.empty();
    if (!read
        || (synthetic_host_process
                ? plan.host != processes || spec->hosted.empty()
                : plan.host == processes
                    || spec->members.front().process != plan.host)) {
        return std::nullopt;
    }
    plan.host_program = host_stub(
        ElaboratedDesignProcessAccess::process_view(design,
            synthetic_host_process ? spec->hosted.front().process : plan.host),
        std::move(host_sensitivity));
    if (synthetic_host_process) {
        plan.host_program = synthetic_host(std::move(plan.host_program));
    }
    plan.members = spec->members.size();
    plan.hosted_processes = spec->hosted.size();
    plan.owned_signals = spec->owned_signals.size();
    plan.boundary_outputs = spec->boundary_outputs.size();
    plan.owned_containers = spec->containers.size();
    plan.spec = std::move(spec);
    return plan;
}

} // namespace detail

} // namespace fsim::elaboration
