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
        } else if constexpr (std::is_same_v<T, FileBinaryRead>) {
            if (value.target_kind == FileBinaryTargetKind::container_object) {
                objects.push_back(value.target);
            }
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

struct Candidate {
    ProcessId process { };
    std::uint8_t kind { };
    bool run_at_start { };
    bool vhdl { };
    /// Behavioral members: starts one delta after time zero's other
    /// processes, as the scheduler starts the same program.
    bool start_late { };
    /// Behavioral members: an implicit signal's driver, armed on its static
    /// sensitivity at time zero without running.
    bool start_waiting { };
    std::uint32_t body_begin { };
    std::uint32_t body_end { };
    std::uint32_t exit_alt { std::numeric_limits<std::uint32_t>::max() };
    std::vector<Sensitivity> sensitivity;
    /// Behavioral members: signals named by their dynamic waits, and those
    /// waited on for an edge.
    std::vector<SignalId> waited;
    std::vector<SignalId> edge_waited;
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

/// The process the kernel's host stub is shaped from: the first member, else
/// the first hosted process, else (a design whose only programs are commit
/// programs) the first valid process.
template <typename Valid>
ProcessId host_shape(const runtime::simir::StaticKernelRuntimeSpec& spec,
    const std::size_t process_count, Valid valid)
{
    if (!spec.members.empty()) {
        return spec.members.front().process;
    }
    if (!spec.hosted.empty()) {
        return spec.hosted.front().process;
    }
    ProcessId id = 0U;
    while (id < process_count && !valid(id)) {
        ++id;
    }
    return id;
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
    // The host process starts at time zero even when the member it replaces
    // is a final procedure (the kernel runs that one in run_finals) or a
    // package static initializer (the kernel runs it at its startup action).
    stub.final = false;
    // The kernel activates in the Active region even when the member it
    // replaces is an assertion's process or a postponed VHDL process (that
    // one resumes in its own region, StaticKernel::ready_thread_in).
    stub.observed = false;
    stub.reactive = false;
    stub.postponed = false;
    stub.startup_phase = ProcessStartupPhase::ordinary;
    stub.startup_order = 0U;
    stub.startup_package_owner_id = 0U;
    stub.startup_writable_string_object_ids.clear();
    stub.startup_writable_container_object_ids.clear();
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
    const StaticKernelAccessPlan& requested_access) const
{
    // SystemC co-simulation (engine-v4-semantics S26): the SystemC kernel
    // reads and writes HDL signals through the boundary context, outside any
    // SimIR operation, like a foreign library (S19). Every signal stays a
    // host signal, and each SystemC process runs in its process executor.
    const bool systemc = !systemc_processes_.empty() || !systemc_objects_.empty();
    auto access = requested_access;
    access.foreign = access.foreign || systemc;
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
    // Host-access objects: fixed arrays the kernel cannot hold (their
    // signals are forced, observed or otherwise host signals) that
    // SystemVerilog members reach through the host, as they reach dynamic
    // arrays (reference-format registers). A plan that finds new ones is
    // planned again with them.
    std::vector<std::uint8_t> host_access_objects(container_objects_.size(), 0U);
    bool host_access_grew = false;
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
    // Coverage: the counters are host data that CodeCoverageHit operations
    // bump; members run them through the generic step (S20). Without access
    // plans the whole design stays off the kernel.
    if (code_coverage_inventory_ && !access_plans) {
        return disable("coverage");
    }
    if ((!verilog_specify_paths_.empty() || !verilog_timing_checks_.empty())
        && !access_plans) {
        return disable("specify");
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
        // A trace selection widens to every signal for such a process.
        const bool all_signals_host
            = (access.foreign || observe_all || !access.observed.empty())
            && (publish_all_switch == nullptr
                || std::string_view { publish_all_switch } != "0");
        if (publish_all_allowed) {
            publish_all = true;
        } else if (all_signals_host) {
            // Accepted: no refusal, no publish-all mode. A trace selection
            // then observes every signal.
            observe_all = true;
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
    // A named event whose handle no EventAlias assigns keeps its own
    // synchronization object (IEEE 1800-2023 15.5.5): only its own triggers
    // are its events, which the host reports to the kernel as revisions of
    // the event variable. An aliased one stays with host waiters.
    std::optional<std::vector<std::uint8_t>> aliased_events;
    const auto fixed_event = [&](const SignalId signal) {
        if (!aliased_events) {
            aliased_events.emplace(signal_count, std::uint8_t { 0U });
            for (const auto& view : views) {
                if (!view.valid()) {
                    continue;
                }
                const auto& operations = view.operations();
                for (std::size_t op = 0U; op < operations.size(); ++op) {
                    if (!operation_holds<EventAlias>(operations[op])) {
                        continue;
                    }
                    const auto expanded = operations.expanded(op);
                    const auto* alias = operation_get_if<EventAlias>(&expanded);
                    if (alias == nullptr) {
                        continue;
                    }
                    if (alias->target < signal_count) {
                        (*aliased_events)[alias->target] = 1U;
                    }
                    if (alias->has_source && alias->source < signal_count) {
                        (*aliased_events)[alias->source] = 1U;
                    }
                }
            }
        }
        return signal < signal_count && (*aliased_events)[signal] == 0U;
    };
    // A SystemVerilog process that runs in the Observed or Reactive region:
    // its own region, or one a WaitRegion moves it to (IEEE 1800-2023 4.4.2,
    // 16.4). As a member it runs outside the kernel's rounds.
    const auto region_member = [](const ProcessProgramView& view) {
        if (view.observed() || view.reactive()) {
            return true;
        }
        const auto& operations = view.operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            if (operation_holds<WaitRegion>(operations[op])) {
                return true;
            }
        }
        return false;
    };
    // A process with assertion operations (static_kernel_assertion_operation)
    // or a region of its own.
    const auto assertion_member = [&](const ProcessProgramView& view) {
        if (region_member(view)) {
            return true;
        }
        const auto& operations = view.operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            if (static_kernel_assertion_operation(operations[op])) {
                return true;
            }
        }
        return false;
    };
    // The programs of the update commit (IEEE 1076-2019 4.6, 6.5.7.1,
    // 14.7.3): a resolution function over drivers, a guarded signal's
    // variant for fewer connected drivers, a port association's conversion
    // of its sources, and an output port's conversion driver. Not scheduled
    // processes: the update commit runs one pass of each
    // (Impl::run_vhdl_resolver, run_vhdl_conversion_drivers), which a
    // behavioral member runs in the kernel (StaticKernel::run_resolver).
    std::vector<std::uint8_t> resolution_program(process_count, 0U);
    for (const auto& signal : signals_) {
        for (const auto& driver : signal.vhdl_conversion_drivers) {
            if (driver.program < process_count) {
                resolution_program[driver.program] = 1U;
            }
        }
        const auto& resolver = signal.vhdl_resolver;
        if (!resolver) {
            continue;
        }
        if (resolver->process < process_count) {
            resolution_program[resolver->process] = 1U;
        }
        for (const auto& variant : resolver->variants) {
            if (variant.process < process_count) {
                resolution_program[variant.process] = 1U;
            }
        }
    }
    // The driver of an implicit signal (S'DELAYED, S'TRANSACTION, S'STABLE,
    // S'QUIET; IEEE 1076-2019 16.2.4): a SystemVerilog-domain process that
    // is not initialized and runs whenever its prefix changes.
    const auto implicit_signal_driver = [](const ProcessProgramView& view) {
        constexpr std::string_view suffix { ".$implicit_driver" };
        const std::string_view name { view.name() };
        return view.scheduling_domain() == ProcessSchedulingDomain::systemverilog
            && !view.initialize() && !view.postponed() && !view.observed()
            && !view.reactive() && !view.final()
            && name.size() > suffix.size() && name.ends_with(suffix);
    };
    // The first property that keeps a process off the kernel whatever its
    // body, or nullptr; `behavioral` applies the behavioral tier's rules.
    // With `all`, every such property is appended to it (the blockers
    // census).
    const auto flag_rejection = [&](const ProcessProgramView& view,
                                    const bool vhdl,
                                    const bool behavioral,
                                    std::vector<const char*>* all
                                    = nullptr) -> const char* {
        const char* first = nullptr;
        const auto hit = [&](const char* flag) {
            if (first == nullptr) {
                first = flag;
            }
            if (all != nullptr) {
                all->push_back(flag);
            }
            return all == nullptr;
        };
        const auto non_logic4 = std::ranges::any_of(view.register_value_kinds(),
            [](const ValueKind kind) { return kind != ValueKind::logic4; });
        // A SystemVerilog behavioral member runs in its process's region
        // (IEEE 1800-2023 4.4.2; StaticKernel::ready_thread_in).
        if (view.observed() && (!behavioral || vhdl) && hit("observed")) {
            return first;
        }
        if (view.reactive() && (!behavioral || vhdl) && hit("reactive")) {
            return first;
        }
        // A program's processes end the program, and with the last program
        // the simulation (IEEE 1800-2023 24.7): a SystemVerilog behavioral
        // member through the kernel (StaticKernel::complete_program_member),
        // any other through the host's process records.
        if (view.program_owner() && (!behavioral || vhdl) && hit("program")) {
            return first;
        }
        // A postponed VHDL process is a behavioral member in the postponed
        // region list (IEEE 1076-2019 11.3, 14.7.5.3).
        if (view.postponed() && (!behavioral || !vhdl) && hit("postponed")) {
            return first;
        }
        // A final procedure is a behavioral member only (its body rule is
        // static_kernel_final_operation).
        if (view.final() && (!behavioral || vhdl) && hit("final")) {
            return first;
        }
        // A final procedure does not start at time zero (run_finals). An
        // implicit signal's driver is a VHDL member (it runs, as the host
        // runs it, in the delta after its prefix changes, after the VHDL
        // processes): it waits on its static sensitivity from before time
        // zero, as the reference arms it (Interpreter::start), and runs from
        // its first operation when that wakes it: on any change of its
        // prefix, or on each of its transactions (StaticKernel::
        // wake_transaction_waiters).
        if (!view.initialize() && !view.final()
            && !(behavioral && vhdl && view.id() < process_count
                && resolution_program[view.id()] != 0U)
            && !(behavioral && vhdl && implicit_signal_driver(view)
                && !view.static_sensitivity().empty())
            && hit("not_initialized")) {
            return first;
        }
        // Package static initializers run once, before time zero, in the
        // host's startup order (IEEE 1800-2023 6.21, 26.3): a SystemVerilog
        // behavioral member runs one at its startup action (its body rule
        // is static_kernel_startup_operation).
        if (view.startup_phase()
                == ProcessStartupPhase::package_static_initializer
            && (!behavioral || vhdl)
            && hit("package_static_initializer")) {
            return first;
        }
        // A unidirectional switch (a MOS gate; IEEE 1800-2023 28.7) is a
        // SystemVerilog behavioral member: the host resolves its source's
        // strength, which its output takes (Impl::set_driver), and wakes it
        // when a driver of the source changes (StaticKernel::
        // wake_switch_reader). A bidirectional one is the host's switch
        // network.
        const bool unidirectional_switch = view.switch_source()
            && view.switch_target() && !view.switch_bidirectional();
        if ((view.switch_source() || view.switch_target()
                || view.switch_control() || view.switch_bidirectional()
                || view.switch_resistive())
            && !(behavioral && !vhdl && unidirectional_switch)
            && hit("switch")) {
            return first;
        }
        // A driver with a drive strength (IEEE 1800-2023 28.11, 28.14) is
        // a SystemVerilog behavioral member only: its targets stay host
        // signals, whose drivers the host resolves by strength.
        if (view.drive_strength() != DriveStrength { } && (!behavioral || vhdl)
            && hit("drive_strength")) {
            return first;
        }
        if (behavioral) {
            if (!view.static_trigger_regions().empty()
                && hit("static_trigger_regions")) {
                return first;
            }
            // VHDL behavioral threads keep their registers and strings in the
            // member's reference-format files (any value kind); their
            // container registers follow the container rule.
            if (!vhdl && non_logic4 && hit("register_kind")) {
                return first;
            }
            if (std::ranges::any_of(view.static_sensitivity(),
                    [&](const Sensitivity& entry) {
                        return entry.signal >= signal_count
                            || (signals_[entry.signal].event_variable
                                && (entry.edge != EdgeKind::any
                                    || !fixed_event(entry.signal)));
                    })
                && hit("event_sensitivity")) {
                return first;
            }
            return first;
        }
        if (view.string_register_count() != 0U && hit("string_register")) {
            return first;
        }
        if (vhdl && view.container_register_count() != 0U
            && hit("vhdl_container_register")) {
            return first;
        }
        if (!vhdl && non_logic4 && hit("register_kind")) {
            return first;
        }
        if (view.operations().size() < 2U && hit("trivial")) {
            return first;
        }
        return first;
    };
    // The blockers census (FSIM_PROFILE_KERNEL_BLOCKERS): every flag and
    // every unsupported operation kind of a process the behavioral tiers
    // reject, so the hosted work can be ranked by what it closes.
    const bool blockers_census = census
        && std::getenv("FSIM_PROFILE_KERNEL_BLOCKERS") != nullptr;
    const auto report_blockers = [&](const ProcessProgramView& view,
                                     const bool vhdl, const char* reason) {
        std::vector<const char*> flags;
        static_cast<void>(flag_rejection(view, vhdl, true, &flags));
        std::set<std::string> unsupported;
        const auto& operations = view.operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            const auto operation = operations.expanded(op);
            if (!(vhdl ? static_kernel_vhdl_behavioral_operation_supported(operation)
                       : static_kernel_behavioral_operation_supported(operation))) {
                unsupported.insert(operation_type_name(operation));
            }
        }
        std::cerr << "fsim-profile: static-kernel-blockers vhdl=" << vhdl
                  << " reason=" << reason << " flags=";
        for (std::size_t entry = 0U; entry < flags.size(); ++entry) {
            std::cerr << (entry == 0U ? "" : ",") << flags[entry];
        }
        std::cerr << " ops=";
        bool comma = false;
        for (const auto& name : unsupported) {
            std::cerr << (comma ? "," : "") << name;
            comma = true;
        }
        std::cerr << " process=" << view.name() << '\n';
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
        // A host-access object is reached through reference-format
        // registers, which only behavioral members hold.
        if (!vhdl && std::ranges::any_of(container_uses[index],
                [&](const ContainerObjectId object) {
                    return object < host_access_objects.size()
                        && host_access_objects[object] != 0U;
                })) {
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
        // A runtime check's halt after its failure report never runs
        // (static_kernel_halt_after_failure) unless a branch reaches it.
        std::optional<std::vector<std::uint8_t>> targeted;
        const auto unreachable_halt = [&](const std::size_t op) {
            // In 2019 mode std.env may disable failures (IEEE 1076-2019
            // 16.5.6), so the halt can run.
            if (!vhdl || op == 0U || view.language_standard() == "2019"
                || !static_kernel_halt_after_failure(
                    operations[op - 1U], operations[op])) {
                return false;
            }
            if (!targeted) {
                targeted.emplace(count, 0U);
                const auto mark = [&](const std::size_t target) {
                    if (target < count) {
                        (*targeted)[target] = 1U;
                    }
                };
                for (std::size_t at = 0U; at < count; ++at) {
                    visit_operation([&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, Jump>) {
                            mark(value.target);
                        } else if constexpr (std::is_same_v<T, Branch>) {
                            mark(value.when_true);
                            mark(value.when_false);
                        } else if constexpr (std::is_same_v<T, Call>) {
                            mark(value.target);
                            mark(value.return_target);
                        }
                    }, operations[at]);
                }
            }
            return (*targeted)[op] == 0U;
        };
        // Structural operations excluded from the supported-operation check.
        std::size_t structural_a = count;
        std::size_t structural_b = count;
        // A SystemVerilog body that calls functions runs them expanded in
        // place (static_kernel_inline_calls); its callees follow the loop
        // jump.
        std::optional<StaticKernelInlinedProgram> inlined;
        const auto inline_calls = [&](const std::size_t at_wait,
                                      const std::size_t at_jump) {
            if (vhdl || std::ranges::none_of(std::views::iota(std::size_t { 0 }, count),
                    [&](const std::size_t op) {
                        return operation_holds<Call>(operations[op]);
                    })) {
                return false;
            }
            std::vector<Operation> expanded;
            expanded.reserve(count);
            for (std::size_t op = 0U; op < count; ++op) {
                expanded.push_back(operations.expanded(op));
            }
            inlined = static_kernel_inline_calls(expanded,
                static_cast<std::uint32_t>(at_wait), static_cast<std::uint32_t>(at_jump));
            return inlined.has_value();
        };
        if (vhdl) {
            // A VHDL process with one static wait W runs from operation 0 to
            // W at initialization; each activation resumes at W + 1 and runs
            // until it reaches W again. A process without sensitivity runs
            // once to its halt.
            if (sensitivity.empty()) {
                std::optional<std::size_t> halt;
                for (std::size_t op = 0U; op < count && shape_ok; ++op) {
                    if (const auto* value = operation_get_if<Halt>(&operations[op]);
                        value != nullptr && !unreachable_halt(op)) {
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
                && (vhdl || *wait == count - 2U || inline_calls(*wait, *wait + 1U))) {
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
                if (jump == count
                    || (!vhdl && jump != count - 1U && !inline_calls(0U, jump))) {
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
        if (inlined) {
            // The expanded program replaces the stored one: its operations
            // are expanded, so their signals need no remapping.
            const auto wait_first = inlined->exit_alt == 0U;
            const auto inlined_a = wait_first ? 0U : inlined->body_end;
            const auto inlined_b = wait_first ? inlined->body_end : inlined->exit_alt;
            const auto inlined_count = inlined->operations.size();
            for (std::size_t op = 0U; op < inlined_count && supported; ++op) {
                if (op == inlined_a || op == inlined_b) {
                    continue;
                }
                const auto& operation = inlined->operations[op];
                supported = (static_kernel_operation_supported(operation)
                                || (member.kind == 1U
                                    && static_kernel_sequential_operation_supported(
                                        operation)))
                    && !branch_target_outside(operation, inlined_count);
                if (supported) {
                    visit_operation([&](const auto& value) {
                        if constexpr (requires { value.signal; }) {
                            if constexpr (std::is_same_v<
                                              std::decay_t<decltype(value.signal)>,
                                              SignalId>) {
                                supported = value.signal < signal_count;
                            }
                        }
                    }, operation);
                }
            }
            // The stored callee code is checked as expanded above.
            structural_a = structural_b = count;
            if (!supported && census && rejection[index].empty()) {
                rejection[index] = "inlined_operation";
            }
        }
        for (std::size_t op = 0U; op < count && supported && !inlined; ++op) {
            if (op == structural_a || op == structural_b) {
                continue;
            }
            unsupported_at = op;
            const auto& operation = operations[op];
            const bool overridden = &operation != operations.data() + op;
            supported = (vhdl ? static_kernel_vhdl_operation_supported(operation)
                                  || unreachable_halt(op)
                              : static_kernel_operation_supported(operation)
                                  || (member.kind == 1U
                                      && static_kernel_sequential_operation_supported(
                                          operation)))
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
    // The container registers of a behavioral member: arrays bound to design
    // memories or protected-type state (ReadContainerObject), used directly
    // (the kernel gives each private register its own storage) and saved by
    // automatic frames. Returns the reason the kernel cannot hold them, or
    // nullptr.
    // SystemVerilog members also hold registers in reference format
    // (static_kernel_dynamic_container_registers): their ReadContainerObject
    // copies the object's value and their WriteContainerObject writes it, as
    // the reference's do. An object of another shape than the arena's is a
    // host object (never kernel storage) without signal aliases: a member's
    // write through the host changes the signals its elements alias, which
    // stay host signals (host_alias_signals below), and the kernel rescans
    // its inputs after it. `dynamic_written` receives the host objects
    // written.
    const auto host_container_ok = [&](const ContainerObjectId object) {
        return object < container_objects_.size()
            && !container_objects_[object].slice_alias
            && std::ranges::none_of(graph.signal_alias_families(),
                [&](const auto& family) { return family.object == object; });
    };
    // Container objects a member writes through the host (see below), and
    // those it reads through the host.
    std::vector<std::uint8_t> host_container_object(container_objects_.size(), 0U);
    std::vector<std::uint8_t> host_read_object(container_objects_.size(), 0U);
    const auto container_rule = [&](const auto& view, const bool vhdl,
                                    std::set<ContainerObjectId>* dynamic_written
                                    = nullptr) -> const char* {
        const auto& operations = view.operations();
        const auto count = operations.size();
        const auto types = process_layout_detail::ProcessLayoutAccess::view(
            view.container_register_types());
        const auto dynamic = static_kernel_dynamic_container_registers(count,
            [&](const std::size_t op) -> const Operation& {
                return operations[op];
            },
            types, !vhdl, vhdl ? nullptr : &host_access_objects);
        const auto is_dynamic = [&](const ContainerRegisterId id) {
            return id < dynamic.size() && dynamic[id] != 0U;
        };
        // Why a reference-format register cannot reach an object: another
        // type, or signal alias structure the host access does not keep.
        const auto dynamic_object_reason = [&](const ContainerObjectId object,
                                               const ContainerRegisterId reg) {
            return object >= container_objects_.size() || reg >= types.size()
                    || !(container_objects_[object].initial_value.type == types[reg])
                ? "dynamic_container_type" : "dynamic_container_alias";
        };
        // An object of the arena's shape is kernel storage (container_uses
        // keeps it); another is a host object without signal aliases.
        const auto dynamic_object_ok = [&](const ContainerObjectId object,
                                           const ContainerRegisterId reg) {
            if (object >= container_objects_.size() || reg >= types.size()
                || !(container_objects_[object].initial_value.type == types[reg])) {
                return false;
            }
            if (static_kernel_arena_container_type(types[reg])
                && host_access_objects[object] == 0U) {
                return true;
            }
            return host_container_ok(object);
        };
        std::set<ContainerRegisterId> bound_containers;
        std::set<ContainerRegisterId> private_containers;
        // Registers an element write targets.
        std::set<ContainerRegisterId> written_containers;
        // Registers element values move through (ContainerRead destinations,
        // ContainerWrite sources).
        std::set<RegisterId> element_registers;
        // Whole-register copies (CopyContainerRegister) between arrays of
        // one shape, and whether the next operation writes the destination
        // back to the memory it is bound to.
        struct ContainerCopy {
            ContainerRegisterId destination { };
            ContainerRegisterId source { };
            std::optional<ContainerObjectId> written_back;
        };
        std::vector<ContainerCopy> container_copies;
        // Registers bound to each design memory, and whole writes of a
        // register to a memory: the kernel supports the write-back of a
        // binding to its own memory (the $readmem lowering, a protected
        // method's update of its state) and the blocking write of a private
        // array of the memory's type (a task's output array copied out, IEEE
        // 1800-2023 13.5).
        std::map<ContainerRegisterId, std::set<ContainerObjectId>> binding_objects;
        struct ObjectWrite {
            ContainerObjectId object { };
            ContainerRegisterId source { };
            bool plain { };
            bool nonblocking { };
        };
        std::vector<ObjectWrite> object_writes;
        const char* dynamic_reason = nullptr;
        for (std::size_t op = 0U; op < count && dynamic_reason == nullptr; ++op) {
            const auto operation = operations.expanded(op);
            visit_operation([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ReadContainerObject>) {
                    if (is_dynamic(value.destination)) {
                        // A VHDL member's reference-format registers are its
                        // own (access values, arrays with run-time ranges).
                        if (vhdl) {
                            dynamic_reason = "dynamic_container_object";
                        } else if (!dynamic_object_ok(value.object, value.destination)) {
                            dynamic_reason = dynamic_object_reason(
                                value.object, value.destination);
                        }
                        return;
                    }
                    bound_containers.insert(value.destination);
                    binding_objects[value.destination].insert(value.object);
                } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
                    // An element write of a host object (a dynamic array,
                    // queue or associative array of the design, IEEE
                    // 1800-2023 7.5-7.10, or a host-access array), through
                    // the host: blocking now, nonblocking from its NBA
                    // region (Impl::write_object_element).
                    if (!vhdl && value.object < container_objects_.size()
                        && (!static_kernel_arena_container_type(
                                container_objects_[value.object].initial_value.type)
                            || host_access_objects[value.object] != 0U)) {
                        if (!host_container_ok(value.object)) {
                            dynamic_reason = "dynamic_container_alias";
                        } else if (value.transaction_signal) {
                            dynamic_reason = "dynamic_container_transaction";
                        } else if (dynamic_written != nullptr) {
                            dynamic_written->insert(value.object);
                        }
                    }
                } else if constexpr (std::is_same_v<T, FileBinaryRead>) {
                    // $fread into an array register: private storage (or
                    // a binding's copy of its memory), filled whole.
                    if (value.target_kind == FileBinaryTargetKind::container_register
                        && !is_dynamic(value.target)) {
                        private_containers.insert(value.target);
                        written_containers.insert(value.target);
                    }
                } else if constexpr (std::is_same_v<T, LoadMemory>) {
                    // A reference-format register loads or writes as the
                    // reference's does.
                    if (is_dynamic(value.target)) {
                        if (vhdl) {
                            dynamic_reason = "dynamic_container_load";
                        }
                        return;
                    }
                    private_containers.insert(value.target);
                } else if constexpr (std::is_same_v<T, WriteContainerObject>) {
                    if (is_dynamic(value.source)) {
                        // A nonblocking write of a host object goes through
                        // the host's NBA region (Impl::write_object); one of
                        // a kernel array stages its elements for the kernel's.
                        const bool host = !static_kernel_arena_container_type(
                                types[value.source])
                            || (value.object < host_access_objects.size()
                                && host_access_objects[value.object] != 0U);
                        if (vhdl) {
                            dynamic_reason = "dynamic_container_object";
                        } else if (!dynamic_object_ok(value.object, value.source)) {
                            dynamic_reason = dynamic_object_reason(
                                value.object, value.source);
                        } else if (value.transaction_signal) {
                            dynamic_reason = "dynamic_container_transaction";
                        } else if (dynamic_written != nullptr && host) {
                            dynamic_written->insert(value.object);
                        }
                        return;
                    }
                    object_writes.push_back({ value.object, value.source,
                        !value.transaction_signal, value.nonblocking });
                } else if constexpr (std::is_same_v<T, ContainerRead>) {
                    if (is_dynamic(value.source)) {
                        return;
                    }
                    if (value.string_index) {
                        dynamic_reason = "container_string_index";
                        return;
                    }
                    private_containers.insert(value.source);
                    element_registers.insert(value.destination);
                } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                    if (is_dynamic(value.target)) {
                        return;
                    }
                    if (value.string_index || value.associative_default) {
                        dynamic_reason = "container_string_index";
                        return;
                    }
                    private_containers.insert(value.target);
                    written_containers.insert(value.target);
                    element_registers.insert(value.source);
                } else if constexpr (std::is_same_v<T, CopyContainerRegister>) {
                    if (is_dynamic(value.destination)) {
                        // Both are (the copy connects them), of one type.
                        if (!(value.destination < types.size()
                                && value.source < types.size()
                                && types[value.destination] == types[value.source])) {
                            dynamic_reason = "container_copy_shape";
                        }
                        return;
                    }
                    private_containers.insert(value.destination);
                    private_containers.insert(value.source);
                    std::optional<ContainerObjectId> written_back;
                    if (op + 1U < count) {
                        const auto next = operations.expanded(op + 1U);
                        if (const auto* write
                            = operation_get_if<WriteContainerObject>(&next);
                            write != nullptr
                            && write->source == value.destination
                            && !write->nonblocking
                            && !write->transaction_signal) {
                            written_back = write->object;
                        }
                    }
                    container_copies.push_back({ value.destination,
                        value.source, written_back });
                }
            }, operation);
        }
        if (dynamic_reason != nullptr) {
            return dynamic_reason;
        }
        // Private storage must be a fixed packed array. A register both
        // bound and element-written is the reference's copy of its memory
        // (IEEE 1800-2023 7.4, 7.6; IEEE 1076-2019 6.4.2.4): the kernel gives
        // it private storage that each binding copies the memory into, and
        // its write-back copies it out.
        for (const auto id : private_containers) {
            if (bound_containers.contains(id) && !written_containers.contains(id)) {
                continue;
            }
            const bool fixed = id < types.size() && types[id].fixed
                && !types[id].associative && !types[id].vhdl_shaped
                && (types[id].element_kind == ContainerElementKind::Packed
                    || types[id].element_kind == ContainerElementKind::Scalar);
            if (!fixed) {
                return "private_container_shape";
            }
        }
        for (const auto id : written_containers) {
            if (!bound_containers.contains(id)) {
                continue;
            }
            for (const auto object : binding_objects[id]) {
                if (object >= container_objects_.size()
                    || !(container_objects_[object].initial_value.type == types[id])) {
                    return "bound_container_write";
                }
            }
        }
        for (const auto& [object, source, plain, nonblocking] : object_writes) {
            // A nonblocking copy-out (`a <= b`) stages the private array's
            // elements for the NBA region; a binding aliases its memory, so
            // its write-back is blocking only.
            const auto found = binding_objects.find(source);
            const bool write_back = !nonblocking && found != binding_objects.end()
                && found->second.size() == 1U
                && *found->second.begin() == object;
            const bool copy_out = plain && found == binding_objects.end()
                && source < types.size() && object < container_objects_.size()
                && types[source] == container_objects_[object].initial_value.type
                && types[source].fixed && !types[source].sparse;
            if (!write_back && !copy_out) {
                return "container_object_write";
            }
        }
        // A copy keeps the reference semantics between registers of
        // identical type (the reference requires the same type, IEEE
        // 1800-2023 7.6); its source may be bound to a design memory (an
        // array argument copied in, 13.5). The reference copies into a
        // binding's own copy of its memory and stores it at the write-back;
        // the kernel's binding aliases the memory, so a bound destination
        // needs the write-back of the binding to its one memory as the next
        // operation (a static task's array argument, 13.5).
        for (const auto& [destination, source, written_back] : container_copies) {
            const auto bound = binding_objects.find(destination);
            const bool published = written_back
                && bound != binding_objects.end()
                && bound->second.size() == 1U
                && *bound->second.begin() == *written_back;
            if ((bound_containers.contains(destination) && !published)
                || destination >= types.size() || source >= types.size()
                || !(types[destination] == types[source])
                || types[destination].sparse
                || types[destination].vhdl_shaped) {
                return "container_copy_shape";
            }
        }
        if (vhdl) {
            // Kernel arrays hold two-plane (Logic4) elements: a VHDL array of
            // std_logic values (IEEE 1076-2019 16.7) keeps its nine-value
            // elements on the host, as do arrays with run-time ranges.
            for (const auto reg : element_registers) {
                if (process_layout_detail::ProcessLayoutAccess::copy_at_or(
                        view.register_value_kinds(), reg, ValueKind::logic4)
                    != ValueKind::logic4) {
                    return "container_value_kind";
                }
            }
            for (const auto& [id, objects] : binding_objects) {
                for (const auto object : objects) {
                    if (object >= container_objects_.size()) {
                        return "container_value_kind";
                    }
                    const auto& value = container_objects_[object].initial_value;
                    if (value.type.vhdl_shaped
                        || std::ranges::any_of(value.elements,
                            [](const runtime::PackedLogic4& element) {
                                return element.is_logic9();
                            })) {
                        return "container_value_kind";
                    }
                }
            }
            for (const auto& write : object_writes) {
                if (write.object >= container_objects_.size()
                    || container_objects_[write.object].initial_value.type.vhdl_shaped) {
                    return "container_value_kind";
                }
            }
        }
        return nullptr;
    };
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
                if (blockers_census) {
                    report_blockers(view, false, flag);
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
                    } else if constexpr (std::is_same_v<T, Fork>) {
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
                            // A vector's edge is its least significant
                            // bit's (IEEE 1800-2023 9.4.2), bit 0 of the
                            // signal's own slot or input.
                            supported = signal < signal_count
                                && (!signals_[signal].event_variable
                                    || (edge == EdgeKind::any
                                        && fixed_event(signal)))
                                && (edge == EdgeKind::any
                                    || ((edge == EdgeKind::posedge
                                            || edge == EdgeKind::negedge)
                                        && signals_[signal].initial_value.width()
                                            != 0U));
                            if (supported) {
                                member.waited.push_back(signal);
                                if (edge != EdgeKind::any) {
                                    member.edge_waited.push_back(signal);
                                }
                            }
                        }
                    }
                }, operation);
            }
            for (std::size_t op = 0U; supported && view.final() && op < count;
                 ++op) {
                if (!static_kernel_final_operation(operations.expanded(op))) {
                    supported = false;
                    failed_at = op;
                    failed_reason = "final_body";
                }
            }
            for (std::size_t op = 0U; supported
                 && view.startup_phase()
                     == ProcessStartupPhase::package_static_initializer
                 && op < count; ++op) {
                if (!static_kernel_startup_operation(operations.expanded(op))) {
                    supported = false;
                    failed_at = op;
                    failed_reason = "package_static_initializer_body";
                }
            }
            std::set<ContainerObjectId> dynamic_written;
            // Container writes that change a reference-format register or a
            // host object (the planner keeps written objects on the host):
            // none notifies a kernel reader.
            std::vector<std::uint8_t> host_container_write(count, 0U);
            if (supported) {
                if (const char* reason
                    = container_rule(view, false, &dynamic_written)) {
                    supported = false;
                    failed_at = count;
                    failed_reason = reason;
                }
            }
            if (supported) {
                // Objects reached only through reference-format registers
                // need no kernel ownership; those written stay host objects.
                const auto types = process_layout_detail::ProcessLayoutAccess::view(
                    view.container_register_types());
                const auto dynamic = static_kernel_dynamic_container_registers(
                    count,
                    [&](const std::size_t op) -> const Operation& {
                    return operations[op];
                },
                    types, true, &host_access_objects);
                const auto is_dynamic = [&](const ContainerRegisterId id) {
                    return id < dynamic.size() && dynamic[id] != 0U;
                };
                // A reference-format register bound to, or written to, a
                // host object (dynamic or host-access).
                const auto host_object = [&](const ContainerObjectId object,
                                             const ContainerRegisterId reg) {
                    return is_dynamic(reg)
                        && (!static_kernel_arena_container_type(types[reg])
                            || (object < host_access_objects.size()
                                && host_access_objects[object] != 0U));
                };
                for (std::size_t op = 0U; op < count; ++op) {
                    const auto operation = operations.expanded(op);
                    if (const auto* write
                        = operation_get_if<ContainerWrite>(&operation)) {
                        host_container_write[op] = is_dynamic(write->target);
                    } else if (const auto* object
                        = operation_get_if<WriteContainerObject>(&operation)) {
                        host_container_write[op]
                            = host_object(object->object, object->source);
                    } else if (const auto* element
                        = operation_get_if<WriteContainerObjectElement>(
                            &operation)) {
                        host_container_write[op]
                            = dynamic_written.contains(element->object);
                    }
                }
                auto& uses = container_uses[index];
                const bool rebuild = !dynamic_written.empty()
                    || std::ranges::any_of(dynamic,
                        [](const std::uint8_t value) { return value != 0U; });
                if (rebuild) {
                    uses.clear();
                }
                for (std::size_t op = 0U; rebuild && op < count; ++op) {
                    const auto operation = operations.expanded(op);
                    if (const auto* bind
                        = operation_get_if<ReadContainerObject>(&operation);
                        bind != nullptr
                        && host_object(bind->object, bind->destination)) {
                        if (bind->object < host_read_object.size()) {
                            host_read_object[bind->object] = 1U;
                        }
                        continue;
                    }
                    if (const auto* write
                        = operation_get_if<WriteContainerObject>(&operation);
                        write != nullptr
                        && host_object(write->object, write->source)) {
                        continue;
                    }
                    if (const auto* element
                        = operation_get_if<WriteContainerObjectElement>(&operation);
                        element != nullptr
                        && dynamic_written.contains(element->object)) {
                        continue;
                    }
                    collect_container_objects(operation, uses);
                }
                std::ranges::sort(uses);
                uses.erase(std::unique(uses.begin(), uses.end()), uses.end());
                for (const auto object : dynamic_written) {
                    host_container_object[object] = 1U;
                }
            }
            // A region member runs outside the kernel's rounds: it writes
            // host signals (marked below), blocking assignments committed at
            // once and updates staged from its own region; its fork
            // children run in its region (the reference's queue_active_
            // current), and a constant delay resumes it there. It neither
            // waits #0, forces, disables nor writes design memories and
            // kernel containers (host_container_write).
            // A register no ReadContainerObject binds holds the member's
            // private array (container_rule): writing its elements touches
            // no design memory.
            std::set<ContainerRegisterId> bound_registers;
            if (region_member(view)) {
                for (std::size_t op = 0U; op < count; ++op) {
                    const auto operation = operations.expanded(op);
                    if (const auto* bind
                        = operation_get_if<ReadContainerObject>(&operation)) {
                        bound_registers.insert(bind->destination);
                    }
                }
            }
            for (std::size_t op = 0U; supported && op < count
                 && region_member(view); ++op) {
                const auto operation = operations.expanded(op);
                const bool denied = visit_operation([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, WaitFor>) {
                        // A delay resumes the thread in its region at the
                        // later time (Impl::queue_at); a #0 resumes it in
                        // this time step's Inactive region instead.
                        return value.source.has_value() || value.delay == 0U;
                    }
                    if constexpr (std::is_same_v<T, ContainerWrite>) {
                        return bound_registers.contains(value.target);
                    }
                    return std::is_same_v<T, WriteAfter>
                        || std::is_same_v<T, WriteAfterSlice>
                        || std::is_same_v<T, WriteAfterDynamicSlice>
                        || std::is_same_v<T, WriteAfterDynamicPartSlice>
                        || std::is_same_v<T, WriteDelayed>
                        || std::is_same_v<T, WriteInertial>
                        || std::is_same_v<T, WriteInertialSlice>
                        || std::is_same_v<T, ForceSignalSlice>
                        || std::is_same_v<T, ReleaseSignalSlice>
                        || std::is_same_v<T, DisableBlock>
                        || std::is_same_v<T, LoadMemory>
                        || std::is_same_v<T, FileBinaryRead>
                        || std::is_same_v<T, FileReadLine>
                        || std::is_same_v<T, FileErrorStatus>
                        || std::is_same_v<T, FileScan>
                        || std::is_same_v<T, WriteContainerObject>
                        || std::is_same_v<T, WriteContainerObjectElement>;
                }, operation);
                if (denied && host_container_write[op] == 0U) {
                    supported = false;
                    failed_at = op;
                    failed_reason = "region_operation";
                }
            }
            if (!supported && census) {
                rejection[index] = std::string { "behavioral_" } + failed_reason
                    + (failed_at < count
                            ? ":" + operation_type_name(
                                  operations.expanded(failed_at))
                            : std::string { });
                if (blockers_census) {
                    report_blockers(view, member.vhdl, rejection[index].c_str());
                }
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
                || (view.scheduling_domain()
                        == ProcessSchedulingDomain::systemverilog
                    && !implicit_signal_driver(view))) {
                continue;
            }
            if (const char* flag = flag_rejection(view, true, true)) {
                if (census) {
                    rejection[index] = std::string { "behavioral_" } + flag;
                }
                if (blockers_census) {
                    report_blockers(view, true, flag);
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
            member.start_waiting = implicit_signal_driver(view);
            member.body_begin = 0U;
            member.body_end = static_cast<std::uint32_t>(count);
            const auto& sensitivity = view.static_sensitivity();
            bool supported = count != 0U;
            std::size_t failed_at = count;
            const char* failed_reason = "";
            // The implicit wait of a sensitivity list: a thread's waiter
            // filters an any-edge entry by its bit range and an edge entry
            // (a `rising_edge(clk)` process narrowed to the edge it tests)
            // by the transition of the signal's first bit.
            for (const auto& entry : sensitivity) {
                if (entry.signal >= signal_count
                    || alias_signal[entry.signal] != 0U
                    || (entry.edge != EdgeKind::any && entry.offset != 0U)) {
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
            if (supported) {
                if (const char* reason = container_rule(view, true)) {
                    supported = false;
                    failed_at = count;
                    failed_reason = reason;
                }
            }
            if (!supported && census) {
                rejection[index] = std::string { "behavioral_" } + failed_reason
                    + (failed_at < count
                            ? ":" + operation_type_name(
                                  operations.expanded(failed_at))
                            : std::string { });
                if (blockers_census) {
                    report_blockers(view, member.vhdl, rejection[index].c_str());
                }
            }
            if (supported) {
                candidates[index] = std::move(member);
                if (census) {
                    rejection[index].clear();
                }
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
    // A disable naming a block or task by hierarchical name (IEEE 1800-2023
    // 9.6.2) ends, for the first of its candidate names that a process
    // declares, that interval in every activation of the declaring
    // processes (Impl::disable_named_scope). The kernel does so for its
    // members (StaticKernel::disable_named_scope) and the host for its
    // processes: the disabling process and the declaring ones are members
    // together, or all stay on the host. So are a program's processes, whose
    // last one to end ends the program (IEEE 1800-2023 24.7; StaticKernel::
    // complete_program_member). enforce_process_groups is applied again
    // wherever a later rule drops a candidate.
    struct ProcessGroup {
        const char* reason { };
        std::vector<std::size_t> processes;
    };
    std::vector<ProcessGroup> process_groups;
    const auto enforce_process_groups = [&] {
        bool any = false;
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& [reason, group] : process_groups) {
                if (std::ranges::all_of(group, [&](const std::size_t index) {
                        return candidates[index].has_value();
                    })) {
                    continue;
                }
                for (const auto index : group) {
                    if (candidates[index]) {
                        if (std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr) {
                            std::cerr << "fsim-kernel-plan: rejected process="
                                      << views[index].name()
                                      << " reason=" << reason << '\n';
                        }
                        candidates[index].reset();
                        reject(index, reason);
                        changed = true;
                        any = true;
                    }
                }
            }
        }
        return any;
    };
    {
        std::vector<std::pair<std::size_t, std::vector<std::string>>> disables;
        for (std::size_t index = 0U; index < process_count; ++index) {
            const auto& view = views[index];
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
                    = operation_get_if<DisableBlock>(&expanded);
                    statement != nullptr && !statement->scopes.empty()) {
                    disables.emplace_back(index, statement->scopes);
                }
            }
        }
        // The processes declaring each named interval, as the reference
        // pairs block_entry and block_exit points.
        std::map<std::string, std::set<std::size_t>> declaring;
        for (std::size_t index = 0U; !disables.empty() && index < process_count;
             ++index) {
            const auto& view = views[index];
            if (!view.valid()) {
                continue;
            }
            const auto& operations = view.operations();
            std::vector<std::string> open;
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                const auto* point = operation_get_if<DebugPoint>(&operations[op]);
                if (point == nullptr
                    || (point->kind != DebugPointKind::block_entry
                        && point->kind != DebugPointKind::block_exit)) {
                    continue;
                }
                const auto& actual = operations.debug_point(op, *point);
                const auto& scope = operations.debug_scope(actual.scope).str();
                if (point->kind == DebugPointKind::block_entry) {
                    open.push_back(scope);
                    continue;
                }
                for (auto entry = open.rbegin(); entry != open.rend(); ++entry) {
                    if (*entry == scope) {
                        declaring[scope].insert(index);
                        open.erase(std::next(entry).base());
                        break;
                    }
                }
            }
        }
        for (const auto& [disabler, scopes] : disables) {
            std::vector<std::size_t> group { disabler };
            for (const auto& scope : scopes) {
                if (const auto found = declaring.find(scope);
                    found != declaring.end()) {
                    group.insert(group.end(), found->second.begin(),
                        found->second.end());
                    break;
                }
            }
            if (group.size() > 1U) {
                process_groups.push_back({ "disabled_scope", std::move(group) });
            }
        }
        std::map<std::uint32_t, std::vector<std::size_t>> programs;
        for (std::size_t index = 0U; index < process_count; ++index) {
            const auto& view = views[index];
            if (view.valid() && view.program_owner() && !view.final()) {
                programs[*view.program_owner()].push_back(index);
            }
        }
        for (auto& [owner, group] : programs) {
            if (group.size() > 1U) {
                process_groups.push_back({ "program", std::move(group) });
            }
        }
        // A process handle (IEEE 1800-2023 9.7) names a kernel thread or a
        // host process, never both kinds: the processes that use handles
        // are members together (StaticKernel::handle_record), or all stay
        // on the host.
        std::vector<std::size_t> handle_users;
        for (std::size_t index = 0U; index < process_count; ++index) {
            const auto& view = views[index];
            if (!view.valid()) {
                continue;
            }
            const auto& operations = view.operations();
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                const auto& operation = operations[op];
                if (operation_holds<ProcessSelf>(operation)
                    || operation_holds<ProcessStatusQuery>(operation)
                    || operation_holds<ProcessCompleted>(operation)
                    || operation_holds<ProcessAwait>(operation)
                    || operation_holds<ProcessKill>(operation)
                    || operation_holds<ProcessSuspend>(operation)
                    || operation_holds<ProcessResume>(operation)
                    || operation_holds<ProcessGetRandState>(operation)
                    || operation_holds<ProcessSetRandState>(operation)
                    || operation_holds<ProcessSrandom>(operation)) {
                    handle_users.push_back(index);
                    break;
                }
            }
        }
        if (!handle_users.empty()) {
            process_groups.push_back({ "process_handle", std::move(handle_users) });
        }
        // The waiters of a mailbox, semaphore or dynamic event (IEEE
        // 1800-2023 15.3-15.5) resume in the order the reference queues
        // them only when they all run in one scheduler: the processes that
        // use them are members together, or all stay on the host.
        std::vector<std::size_t> synchronization_users;
        for (std::size_t index = 0U; index < process_count; ++index) {
            const auto& view = views[index];
            if (!view.valid()) {
                continue;
            }
            for (const auto& operation : view.operations()) {
                if (operation_holds<MailboxCreate>(operation)
                    || operation_holds<MailboxPut>(operation)
                    || operation_holds<MailboxGet>(operation)
                    || operation_holds<MailboxNum>(operation)
                    || operation_holds<SemaphoreCreate>(operation)
                    || operation_holds<SemaphoreGet>(operation)
                    || operation_holds<SemaphorePut>(operation)
                    || operation_holds<DynamicEvent>(operation)) {
                    synchronization_users.push_back(index);
                    break;
                }
            }
        }
        if (!synchronization_users.empty()) {
            process_groups.push_back(
                { "synchronization", std::move(synchronization_users) });
        }
        static_cast<void>(enforce_process_groups());
    }
    // A SystemC process is neither a member nor a hosted step: its body is
    // native SystemC code that its process executor resumes (S26).
    std::vector<std::uint8_t> systemc_process(process_count, 0U);
    for (const auto& entry : systemc_processes_) {
        if (entry.process < process_count) {
            systemc_process[entry.process] = 1U;
            candidates[entry.process].reset();
            reject(entry.process, "systemc_process");
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
    // The driver of an implicit signal runs in the SystemVerilog scheduling
    // domain but belongs to the VHDL signal it observes, so it counts as
    // VHDL.
    std::size_t vhdl_processes = 0U;
    std::size_t sv_processes = 0U;
    for (const auto& view : views) {
        if (view.valid()) {
            ++(view.scheduling_domain()
                        == ProcessSchedulingDomain::systemverilog
                    && !implicit_signal_driver(view)
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
        // Assertion members run in the SystemVerilog kernel's regions only.
        if (candidate && (mixed || vhdl_mode) && !candidate->vhdl
            && assertion_member(views[index])) {
            candidate.reset();
            reject(index, "assertion_language_mode");
        }
    }
    static_cast<void>(enforce_process_groups());
    // A module path delays the commit of its destination, measured from the
    // most recent transition of a path source (IEEE 1800-2023 30.4, 30.5).
    // The host applies that delay when the driver of the destination writes
    // it, and finds the source transition only once the source's own update
    // has committed. Path terminals are host signals (host_touched below),
    // and a path source is never a sole-writer input (sole_writer_inputs):
    // a member reading a source wakes only after the host commits its
    // update, as the reference's driver does, so a member may drive a
    // destination.
    std::vector<std::uint8_t> path_destination(signal_count, 0U);
    std::vector<std::uint8_t> path_source(signal_count, 0U);
    for (const auto& path : verilog_specify_paths_) {
        for (const auto& terminal : path.destinations) {
            if (terminal.signal < signal_count) {
                path_destination[terminal.signal] = 1U;
            }
        }
        for (const auto& terminal : path.sources) {
            if (terminal.signal < signal_count) {
                path_source[terminal.signal] = 1U;
            }
        }
    }
    // The programs of live wirings (below): VHDL port routing, whose formal
    // the kernel updates in the VHDL update phase (IEEE 1076-2008 6.5.7.1).
    std::vector<std::uint8_t> wiring_program(process_count, 0U);
    const auto vhdl_written = [&](const SignalId signal) {
        return std::ranges::all_of(graph_signals[signal].writers,
            [&](const auto& writer) {
                return writer.process < process_count
                    && ((candidates[writer.process]
                            && candidates[writer.process]->vhdl)
                        || wiring_program[writer.process] != 0U);
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
    std::vector<std::uint8_t> vhdl_variable_storage(object_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        const auto& value = container_objects_[object].initial_value;
        vhdl_variable_storage[object] = !packed_alias[object]
                && has_element_alias[object] == 0U && !value.type.vhdl_shaped
                && std::ranges::none_of(value.elements,
                    [](const runtime::PackedLogic4& element) {
                        return element.is_logic9();
                    })
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
    // A member's delayed, transport or waveform signal assignment keeps its
    // target a host signal, an attribute query its prefix and a force its
    // target: the host holds the projected output waveform of the member's
    // driver, the signal's history and the force (static_kernel_host_signal).
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (!candidates[index]) {
            continue;
        }
        // The host records a signal's transactions (a thread waits on them
        // through it), and an implicit signal's driver updates its signal
        // as the reference's does, from its SystemVerilog domain (IEEE
        // 1076-2019 16.2.4).
        for (const auto& entry : candidates[index]->sensitivity) {
            if (entry.edge == EdgeKind::transaction
                && entry.signal < signal_count) {
                host_touched[entry.signal] = 1U;
            }
        }
        if (implicit_signal_driver(views[index])) {
            for (const auto& region : views[index].driver_regions()) {
                if (region.signal < signal_count) {
                    host_touched[region.signal] = 1U;
                }
            }
        }
        // A switch member's source and target: the host resolves the
        // source's strength and drives the target at it (IEEE 1800-2023
        // 28.13; Impl::set_driver).
        for (const auto endpoint : { views[index].switch_source(),
                 views[index].switch_target() }) {
            if (endpoint && *endpoint < signal_count) {
                host_touched[*endpoint] = 1U;
            }
        }
        const auto& operations = views[index].operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            const auto operation = operations.expanded(op);
            if (const auto target = static_kernel_host_signal(operation);
                target && *target < signal_count) {
                host_touched[*target] = 1U;
            }

            // A member's $monitor or $strobe: the host's monitor watches
            // its signals through the host commit path and reads them when
            // it prints (IEEE 1800-2023 21.2.2, 21.2.3).
            if (const auto* monitor = operation_get_if<MonitorInstall>(&operation)) {
                for (const auto& value : monitor->values) {
                    if (value.kind == MonitorValueKind::signal
                        && value.signal < signal_count) {
                        host_touched[value.signal] = 1U;
                    }
                }
            }
        }
    }
    // A package static initializer runs before the kernel's first
    // activation and establishes the host's baseline (IEEE 1800-2023 6.21);
    // a final procedure runs after its last round (9.2.3). Their members
    // write host signals, which the host commits at once. A member driving
    // with a strength writes host signals too: the host resolves its
    // driver by strength (28.11-28.14).
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        if (std::ranges::any_of(graph_signals[signal].writers,
                [&](const auto& writer) {
                    return writer.process < process_count
                        && candidates[writer.process]
                        && (views[writer.process].final()
                            || views[writer.process].startup_phase()
                                == ProcessStartupPhase::package_static_initializer
                            || views[writer.process].drive_strength()
                                != DriveStrength { });
                })) {
            host_touched[signal] = 1U;
        }
    }
    // A file trace's fixed selection: its signals are host signals, which
    // the trace observes through the host commit path (plan §3.6).
    for (const auto signal : access.observed) {
        if (signal < signal_count) {
            host_touched[signal] = 1U;
        }
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
    // The signals that the elements of a host object members access alias
    // (IEEE 1800-2023 7.4, 23.3.3): the host keeps the object and its
    // signals coherent, so they stay host signals; those of an object a
    // member writes change inside an activation (alias_written). A member
    // reading the object or another alias reads the host's value, which a
    // mirrored update of one alias does not change (container_aliased).
    std::vector<std::uint8_t> alias_written(signal_count, 0U);
    std::vector<std::uint8_t> container_aliased(signal_count, 0U);
    {
        const auto alias = [&](const ContainerObjectId object,
                               const SignalId signal) {
            if (object >= container_objects_.size() || signal >= signal_count
                || (host_container_object[object] == 0U
                    && host_read_object[object] == 0U)) {
                return;
            }
            host_touched[signal] = 1U;
            container_aliased[signal] = 1U;
            if (host_container_object[object] != 0U) {
                alias_written[signal] = 1U;
            }
        };
        for (const auto& entry : container_signal_aliases_) {
            alias(entry.object, entry.signal);
        }
        for (const auto& entry : container_element_signal_aliases_) {
            alias(entry.object, entry.signal);
        }
        for (const auto& entry : container_aggregate_signal_aliases_) {
            alias(entry.object, entry.signal);
        }
    }
    // Assertion members read the host's sampled state (run_sampled_read):
    // a sampled signal, its sampling clock and its gate are host signals,
    // whose values, histories and events the host keeps (IEEE 1800-2023
    // 16.5.1, 16.9.3). A region member's writes are the host's: blocking
    // ones commit at once, updates are staged from the member's region
    // (StaticKernel::write_host, Impl::capture_signal_change_origin).
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (!candidates[index] || candidates[index]->vhdl
            || candidates[index]->kind != behavioral_kind
            || !assertion_member(views[index])) {
            continue;
        }
        const bool region = region_member(views[index]);
        const auto touch = [&](const SignalId signal) {
            if (signal < signal_count) {
                host_touched[signal] = 1U;
            }
        };
        const auto& operations = views[index].operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            visit_operation([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (value.kind != SignalReadKind::current
                        || value.ticks != 1U || value.clock || value.gate) {
                        touch(value.signal);
                        if (value.clock) {
                            touch(*value.clock);
                        }
                        if (value.gate) {
                            touch(*value.gate);
                        }
                    }
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                    if (region) {
                        touch(value.signal);
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
    const auto wiring_signal_width = [&](const SignalId signal) {
        return static_cast<std::uint64_t>(
            signal < signal_count ? signals_[signal].initial_value.width()
                                  : 0U);
    };
    // An output formal associated with a static element or slice of its
    // actual drives that part (IEEE 1076-2008 6.5.7.1, 14.7.3.2): a
    // conversion driver whose program is pure routing over the formal is
    // kernel wiring when every conversion driver of the actual is, the
    // kernel copying the formal into the part in the update phase that
    // changes it. Otherwise the drivers run on the host, which writes the
    // resolved actual.
    struct DriverWiring {
        SignalId target { };
        SignalId source { };
        ProcessId stub { };
        ProcessId program { };
        bool live { };
    };
    std::vector<DriverWiring> driver_wirings;
    for (std::size_t index = 0U; index < signals_.size(); ++index) {
        const auto& drivers = signals_[index].vhdl_conversion_drivers;
        std::vector<DriverWiring> wired;
        for (const auto& driver : drivers) {
            if (!vhdl_mode || index >= signal_count
                || signals_[index].vhdl_resolver || driver.sources.size() != 1U
                || driver.program >= process_count || driver.stub >= process_count
                || !views[driver.program].valid()) {
                break;
            }
            const auto wire = static_kernel_wiring(
                views[driver.program].operations(), driver.output,
                wiring_signal_width);
            const auto source = driver.sources.front();
            const auto target_width = wiring_signal_width(
                static_cast<SignalId>(index));
            if (!wire || wire->source != source || source >= signal_count
                || source == index
                || (driver.offset
                        ? *driver.offset + std::uint64_t { wire->width }
                            > target_width
                        : wire->width != target_width)
                || signals_[source].value_kind != signals_[index].value_kind) {
                break;
            }
            wired.push_back({ static_cast<SignalId>(index), source,
                driver.stub, driver.program, true });
        }
        if (!drivers.empty() && wired.size() == drivers.size()) {
            driver_wirings.insert(driver_wirings.end(), wired.begin(),
                wired.end());
        }
        for (const auto& driver : drivers) {
            for (const auto process : { driver.program, driver.stub }) {
                if (process < process_count) {
                    host_program[process] = 1U;
                }
            }
            if (wired.size() == drivers.size()) {
                continue;
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
    for (const auto& wiring : driver_wirings) {
        wiring_program[wiring.stub] = 1U;
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
        wiring_program[resolver->process] = 1U;
    }
    // A port-association conversion (IEEE 1076-2008 6.5.7.1, 14.7.3.3): a
    // resolver with sources and no drivers whose program is a pure function
    // of its sources (static_kernel_conversion_operation). The kernel runs
    // it in the update phase that changes a source, as the host's update
    // commit does, so the formal updates in the same simulation cycle. It
    // needs its sources and its formal kernel-owned; otherwise it reverts
    // to a host program below.
    struct Conversion {
        SignalId target { };
        ProcessId program { };
        std::vector<SignalId> sources;
        bool live { };
    };
    std::vector<Conversion> conversions;
    std::vector<std::int32_t> conversion_of_target(signal_count, -1);
    for (SignalId target = 0U; vhdl_mode && !mixed && target < signal_count;
         ++target) {
        const auto& resolver = signals_[target].vhdl_resolver;
        if (!resolver || resolver->sources.empty() || !resolver->drivers.empty()
            || !resolver->variants.empty() || resolver->guarded_kind != 0U
            || resolver->driving_mirror
            || !signals_[target].vhdl_conversion_drivers.empty()
            || wiring_of_target[target] >= 0
            || resolver->process >= process_count
            || !views[resolver->process].valid()
            || std::ranges::any_of(resolver->sources, [&](const SignalId source) {
                   return source >= signal_count || source == target;
               })) {
            continue;
        }
        const auto& operations = views[resolver->process].operations();
        bool pure = operations.size() != 0U;
        for (std::size_t op = 0U; op < operations.size() && pure; ++op) {
            pure = static_kernel_conversion_operation(
                operations.expanded(op), resolver->sources);
        }
        if (!pure) {
            continue;
        }
        conversion_of_target[target] = static_cast<std::int32_t>(conversions.size());
        conversions.push_back({ target, resolver->process, resolver->sources, true });
        wiring_program[resolver->process] = 1U;
    }
    for (SignalId index = 0U; index < signal_count; ++index) {
        const auto& signal = signals_[index];
        if (!signal.vhdl_resolver || signal.vhdl_resolver->sources.empty()) {
            continue;
        }
        if (signal.vhdl_resolver->process < process_count) {
            host_program[signal.vhdl_resolver->process] = 1U;
        }
        if (wiring_of_target[index] >= 0 || conversion_of_target[index] >= 0) {
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
        // A wired actual's parts are written by its driver wirings' stubs.
        if (std::ranges::any_of(node.writers, [&](const auto& writer) {
                return writer.process >= process_count
                    || (!candidates[writer.process]
                        && wiring_program[writer.process] == 0U);
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
    // Signals some candidate is edge-sensitive to (the kernel gives an
    // alias proxy no edge; a thread's any-change wait on one waits on the
    // leaves it overlaps); recomputed for each fixpoint pass (candidates
    // only drop out between passes).
    std::vector<std::uint8_t> edge_sensitive(signal_count, 0U);
    const auto mark_edge_sensitive = [&] {
        std::ranges::fill(edge_sensitive, 0U);
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (!candidates[index]) {
                continue;
            }
            for (const auto& entry : candidates[index]->sensitivity) {
                if (entry.edge != EdgeKind::any && entry.signal < signal_count) {
                    edge_sensitive[entry.signal] = 1U;
                }
            }
            for (const auto signal : candidates[index]->edge_waited) {
                if (signal < signal_count) {
                    edge_sensitive[signal] = 1U;
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
    // A forced value hides the driver's (IEEE 1800-2023 10.6.2); a port
    // segment force reaches nets by another name, so any force in the design
    // turns the mirroring of host signals and the ownership of undriven
    // actuals off.
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
    // Signals with no source at all: no process writes them, and no port
    // association (resolver, conversion driver, wiring) drives them.
    std::vector<std::uint8_t> undriven_actual(signal_count, 0U);
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        undriven_actual[signal] = !signals_[signal].vhdl_resolver
            && signals_[signal].vhdl_conversion_drivers.empty() ? 1U : 0U;
    }
    for (const auto& wiring : wirings) {
        undriven_actual[wiring.target] = 0U;
    }
    for (const auto& wiring : driver_wirings) {
        undriven_actual[wiring.target] = 0U;
    }
    for (const auto& conversion : conversions) {
        undriven_actual[conversion.target] = 0U;
    }
    // Unowned fixed arrays that rejected a SystemVerilog member: 1 when the
    // kernel cannot hold the array, 2 when only a hosted accessor keeps it
    // off the kernel (that accessor may become a member once the arrays of
    // the first kind are host-access objects).
    std::vector<std::uint8_t> access_candidates(object_count, 0U);
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
        // An actual no process drives keeps its default value for the whole
        // run (IEEE 1076-2019 14.7.3.1): unless a force or a foreign library
        // changes it, it is a constant the kernel owns, and a wiring or
        // conversion of it converts that value once (14.7.5.2).
        if (!forces && !host_signals) {
            const auto own_undriven = [&](const SignalId source) {
                if (source < signal_count && owned[source] == 0U
                    && alias_role[source] == 0U && proxy_signal[source] == 0U
                    && graph_signals[source].writers.empty()
                    && undriven_actual[source] != 0U
                    && basic_ok(source)) {
                    owned[source] = 1U;
                }
            };
            for (const auto& wiring : wirings) {
                if (wiring.live) {
                    own_undriven(wiring.source);
                }
            }
            for (const auto& conversion : conversions) {
                if (conversion.live) {
                    std::ranges::for_each(conversion.sources, own_undriven);
                }
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
            // A converted formal likewise, with every source: plain
            // signals, which the kernel reads in its update phase.
            for (const auto& conversion : conversions) {
                if (!conversion.live || owned[conversion.target] != 0U
                    || alias_role[conversion.target] != 0U
                    || proxy_signal[conversion.target] != 0U
                    || !basic_ok(conversion.target)
                    || !std::ranges::all_of(conversion.sources,
                        [&](const SignalId source) {
                            return owned[source] != 0U && alias_role[source] == 0U
                                && proxy_signal[source] == 0U;
                        })
                    || !std::ranges::all_of(
                        graph_signals[conversion.target].writers,
                        [&](const auto& writer) {
                            return writer.process == conversion.program;
                        })) {
                    continue;
                }
                owned[conversion.target] = 1U;
                again = true;
            }
        }
        std::vector<const char*> unowned_reason(object_count, "");
        for (std::size_t object = 0U; object < object_count; ++object) {
            // A VHDL variable's storage (a shared variable, protected-type
            // state; IEEE 1076-2019 6.4.2.4, 5.6.2) is kernel storage when
            // its elements are two-plane values; containers that alias VHDL
            // signals stay on the host.
            const bool vhdl_accessed = (vhdl_mode && !mixed)
                || (mixed && std::ranges::any_of(accessors[object],
                    [&](const ProcessId process) {
                        return candidates[process] && candidates[process]->vhdl;
                    }));
            if (host_access_objects[object] != 0U) {
                unowned_reason[object] = "host_access";
                continue;
            }
            if (container_shape_ok[object] == 0U) {
                unowned_reason[object] = "shape";
                continue;
            }
            if (host_container_object[object] != 0U) {
                unowned_reason[object] = "host_object";
                continue;
            }
            if (vhdl_accessed && !vhdl_variable_storage[object]) {
                unowned_reason[object] = "vhdl_storage";
                continue;
            }
            if (std::ranges::any_of(accessors[object],
                    [&](const ProcessId process) {
                        return !candidates[process];
                    })) {
                unowned_reason[object] = "host_accessor";
                continue;
            }
            if (packed_alias[object]) {
                const auto signal = *packed_alias[object];
                if (signal_ok(signal)) {
                    owned[signal] = 1U;
                    owned_container[object] = 1U;
                } else {
                    unowned_reason[object] = "packed_alias_signal";
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
                    unowned_reason[object] = "publish_all";
                    continue;
                }
                owned_container[object] = 1U;
                continue;
            }
            if (element_complete[object] == 0U) {
                unowned_reason[object] = "elements_incomplete";
                continue;
            }
            const auto* family = family_of_object[object];
            if (family != nullptr
                && (!family->complete || family->proxy >= signal_count
                    || !signal_ok(family->proxy)
                    || edge_sensitive_member(family->proxy))) {
                unowned_reason[object] = !family->complete
                    ? "family_incomplete"
                    : family->proxy >= signal_count ? "family_proxy"
                    : !basic_ok(family->proxy) ? "family_proxy_signal"
                    : !writers_ok(family->proxy) ? "family_proxy_writers"
                    : "family_edge_sensitive";
                continue;
            }
            if (!std::ranges::all_of(element_signals[object], signal_ok)) {
                unowned_reason[object] = !std::ranges::all_of(
                        element_signals[object], basic_ok)
                    ? "element_signal" : "element_writers";
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
                if (!candidates[index]->vhdl) {
                    for (const auto object : container_uses[index]) {
                        if (object < object_count && owned_container[object] == 0U
                            && host_access_objects[object] == 0U
                            && host_container_ok(object)) {
                            const std::uint8_t kind
                                = std::string_view { unowned_reason[object] }
                                    == "host_accessor" ? 2U : 1U;
                            if (access_candidates[object] == 0U
                                || kind < access_candidates[object]) {
                                access_candidates[object] = kind;
                            }
                        }
                    }
                }
                if (std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr) {
                    for (const auto object : container_uses[index]) {
                        if (object < object_count
                            && owned_container[object] == 0U) {
                            std::cerr << "fsim-kernel-plan: container process="
                                      << views[index].name() << " object="
                                      << container_objects_[object].name
                                      << " reason=" << unowned_reason[object]
                                      << '\n';
                        }
                    }
                }
                candidates[index].reset();
                reject(index, "container_unowned");
                changed = true;
            }
        }
        changed = enforce_process_groups() || changed;
    }
    // A wiring needs its source and its formal kernel-owned; otherwise its
    // program runs on the host and the source stays a host signal. A driver
    // wiring also needs its actual owned; otherwise every conversion driver
    // of the actual runs on the host.
    // An actual's driver wirings are adjacent.
    for (std::size_t begin = 0U; begin < driver_wirings.size();) {
        auto end = begin + 1U;
        while (end < driver_wirings.size()
            && driver_wirings[end].target == driver_wirings[begin].target) {
            ++end;
        }
        const auto target = driver_wirings[begin].target;
        if (driver_wirings[begin].live
            && (owned[target] == 0U
                || std::ranges::any_of(
                    std::span { driver_wirings }.subspan(begin, end - begin),
                    [&](const DriverWiring& wiring) {
                        return owned[wiring.source] == 0U;
                    }))) {
            for (auto at = begin; at < end; ++at) {
                driver_wirings[at].live = false;
                wiring_program[driver_wirings[at].stub] = 0U;
                host_touched[driver_wirings[at].source] = 1U;
            }
            host_touched[target] = 1U;
            revalidate = true;
        }
        begin = end;
    }
    for (auto& wiring : wirings) {
        if (wiring.live
            && (owned[wiring.source] == 0U || owned[wiring.target] == 0U)) {
            wiring.live = false;
            wiring_program[wiring.program] = 0U;
            host_touched[wiring.source] = 1U;
            wiring_of_target[wiring.target] = -1;
            revalidate = true;
        }
    }
    for (auto& conversion : conversions) {
        if (conversion.live
            && (owned[conversion.target] == 0U
                || std::ranges::any_of(conversion.sources,
                    [&](const SignalId source) { return owned[source] == 0U; }))) {
            conversion.live = false;
            wiring_program[conversion.program] = 0U;
            for (const auto source : conversion.sources) {
                host_touched[source] = 1U;
            }
            host_touched[conversion.target] = 1U;
            conversion_of_target[conversion.target] = -1;
            revalidate = true;
        }
    }
    }
    // New host-access objects plan the design again (plan_access).
    {
        const bool primary = std::ranges::any_of(access_candidates,
            [](const std::uint8_t kind) { return kind == 1U; });
        for (std::size_t object = 0U; object < object_count; ++object) {
            if (access_candidates[object] == (primary ? 1U : 2U)) {
                host_access_objects[object] = 1U;
                host_access_grew = true;
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
                          << " kind="
                          << (candidates[index]
                                     ? static_cast<int>(candidates[index]->kind)
                                     : -1)
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
                || systemc_process[index] != 0U
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
            || systemc_process[index] != 0U
            || view.scheduling_domain()
                == ProcessSchedulingDomain::systemverilog
            || (view.postponed() && !hosted_postponed_enabled)) {
            continue;
        }
        hosted[index] = 1U;
    }

    // SystemVerilog-domain processes of a VHDL kernel (S25a). The driver
    // process of S'DELAYED, S'TRANSACTION, S'STABLE and S'QUIET is a
    // scheduled process of the SystemVerilog scheduling domain with no
    // initial run; the processes of a Verilog unit that the kernel cannot
    // run natively (a register of a VHDL value kind read across the language
    // boundary, for example) are SystemVerilog-domain processes too. The
    // VHDL kernel hosts the Active-region ones like VHDL processes: a wake
    // from the other domain runs in the next delta (queue_next_delta, as the
    // host does; 1076-2019 14.7.5.3), one from its own domain in the current
    // Active region, and its assignments are the host's staged updates.
    // FSIM_STATIC_KERNEL_HOSTED_IMPLICIT=0 keeps them on the host.
    {
        const char* implicit_text
            = std::getenv("FSIM_STATIC_KERNEL_HOSTED_IMPLICIT");
        const bool implicit_enabled = hosted_vhdl_enabled
            && (implicit_text == nullptr
                || std::string_view { implicit_text } != "0");
        for (std::size_t index = 0U; implicit_enabled && index < process_count;
             ++index) {
            const auto& view = views[index];
            if (!view.valid() || candidates[index] || host_program[index] != 0U
                || systemc_process[index] != 0U
                || view.scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog
                || view.postponed() || view.observed() || view.reactive()
                || view.final()
                || view.startup_phase()
                    == ProcessStartupPhase::package_static_initializer) {
                continue;
            }
            hosted[index] = 1U;
        }
    }

    // Dead-logic removal (engine-v4 plan 3.1, 3.6): a member whose only
    // effects are writes that no live process reads, and that nothing
    // outside the processes observes, changes nothing a run can see except
    // a read of a named net on demand. Such a member is lazy: never
    // scheduled, and evaluated from its current inputs when what it writes
    // is read (StaticKernel::materialize), which the eligibility rule below
    // makes exact. Live roots are every process outside the native members,
    // every member that cannot be lazy (state, effects beyond plain writes:
    // output, assertion, randomization, a runtime error check, a nonblocking,
    // delayed or waveform write), every signal the host owns or touches,
    // and every signal a client outside the processes may observe. Liveness
    // flows from a live process to what it reads and from a live signal or
    // container to its writers. Members on a cycle of activations stay live:
    // such a cycle need not settle (IEEE 1800-2023 4.7). Any client that
    // reads arbitrary state as it changes (foreign library, trace, coverage,
    // debugger, publish-all) turns the removal off.
    std::vector<std::uint8_t> lazy(process_count, 0U);
    {
        const char* dead_text = std::getenv("FSIM_STATIC_KERNEL_DEAD_LOGIC");
        bool enabled = (dead_text == nullptr || std::string_view { dead_text } != "0")
            && !access.foreign && !observe_all && access.observed.empty()
            && !access.coverage && !access.debug
            && !publish_all && graph.access_inventory_complete()
            && graph_processes.size() == process_count;
        for (std::size_t index = 0U; enabled && index < process_count; ++index) {
            enabled = !views[index].valid() || !graph_processes[index].dependencies_unknown;
        }
        std::vector<std::uint8_t> live_process(process_count, 0U);
        std::vector<std::uint8_t> live_signal(signal_count, 0U);
        std::vector<std::uint8_t> live_container(object_count, 0U);
        std::vector<ProcessId> process_work;
        std::vector<SignalId> signal_work;
        std::vector<ContainerObjectId> container_work;
        const auto mark_process = [&](const std::size_t process) {
            if (process < process_count && live_process[process] == 0U) {
                live_process[process] = 1U;
                process_work.push_back(static_cast<ProcessId>(process));
            }
        };
        const auto mark_signal = [&](const SignalId signal) {
            if (signal < signal_count && live_signal[signal] == 0U) {
                live_signal[signal] = 1U;
                signal_work.push_back(signal);
            }
        };
        const auto mark_container = [&](const ContainerObjectId object) {
            if (object < object_count && live_container[object] == 0U) {
                live_container[object] = 1U;
                container_work.push_back(object);
            }
        };
        const auto propagate = [&] {
            while (!process_work.empty() || !signal_work.empty()
                || !container_work.empty()) {
                if (!process_work.empty()) {
                    const auto process = process_work.back();
                    process_work.pop_back();
                    const auto& node = graph_processes[process];
                    for (const auto& read : node.reads) {
                        mark_signal(read.signal);
                    }
                    for (const auto& sensitivity : node.sensitivities) {
                        mark_signal(sensitivity.signal);
                    }
                    for (const auto object : container_uses[process]) {
                        mark_container(object);
                    }
                } else if (!signal_work.empty()) {
                    const auto signal = signal_work.back();
                    signal_work.pop_back();
                    for (const auto& writer : graph_signals[signal].writers) {
                        mark_process(writer.process);
                    }
                } else {
                    const auto object = container_work.back();
                    container_work.pop_back();
                    for (const auto process : accessors[object]) {
                        mark_process(process);
                    }
                }
            }
        };
        // Operations whose only effects are register values and immediate
        // signal and container writes (blocking, or a continuous
        // assignment's update): a lazy run at a read has no later region to
        // defer a nonblocking update to. Per shared body.
        std::unordered_map<const void*, bool> effect_free_body;
        const auto effect_free = [&](const ProcessProgramView& view) {
            // Operation overrides make one body's instances differ.
            const auto* const body = view.operations().has_operation_overrides()
                ? nullptr : view.operations().body_identity();
            if (body != nullptr) {
                if (const auto found = effect_free_body.find(body);
                    found != effect_free_body.end()) {
                    return found->second;
                }
            }
            const auto& operations = view.operations();
            bool free = true;
            for (std::size_t op = 0U; free && op < operations.size(); ++op) {
                free = visit_operation([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, WriteUpdate>
                        || std::is_same_v<T, WriteUpdateSlice>
                        || std::is_same_v<T, WriteUpdateDynamicSlice>
                        || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                        // A continuous assignment's update applies at once;
                        // a nonblocking one waits for the NBA region.
                        return value.domain != SignalUpdateDomain::systemverilog_nba;
                    } else if constexpr (std::is_same_v<T, Halt>) {
                        // The end of a process that runs once; not an exit.
                        return !value.program_exit;
                    } else {
                        return std::is_same_v<T, LoadConstant>
                            || std::is_same_v<T, CopyRegister>
                            || std::is_same_v<T, ConvertToTwoState>
                            || std::is_same_v<T, UnaryNot>
                            || std::is_same_v<T, LogicalNot>
                            || std::is_same_v<T, LogicalBinary>
                            || std::is_same_v<T, Reduction>
                            || std::is_same_v<T, CountOnes>
                            || std::is_same_v<T, CountBits>
                            || std::is_same_v<T, Shift>
                            || std::is_same_v<T, Extract>
                            || std::is_same_v<T, DynamicExtract>
                            || std::is_same_v<T, DynamicPartSelect>
                            || std::is_same_v<T, Concatenate>
                            || std::is_same_v<T, Binary>
                            || std::is_same_v<T, Insert>
                            || std::is_same_v<T, DynamicInsert>
                            || std::is_same_v<T, DynamicPartInsert>
                            || std::is_same_v<T, ConditionalSelect>
                            || std::is_same_v<T, ReadSignal>
                            || std::is_same_v<T, SignalEvent>
                            || std::is_same_v<T, SignalLastValue>
                            || std::is_same_v<T, SignalLastEvent>
                            || std::is_same_v<T, SignalActive>
                            || std::is_same_v<T, SignalLastActive>
                            || std::is_same_v<T, WriteBlocking>
                            || std::is_same_v<T, WriteBlockingSlice>
                            || std::is_same_v<T, WriteBlockingDynamicSlice>
                            || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                            || std::is_same_v<T, ContainerRead>
                            || std::is_same_v<T, ContainerWrite>
                            || std::is_same_v<T, ContainerElementRead>
                            || std::is_same_v<T, ContainerElementWrite>
                            || std::is_same_v<T, Jump>
                            || std::is_same_v<T, Branch>
                            || std::is_same_v<T, Call>
                            || std::is_same_v<T, Return>
                            || std::is_same_v<T, CallableFramePush>
                            || std::is_same_v<T, CallableFramePop>
                            || std::is_same_v<T, DebugPoint>
                            || std::is_same_v<T, WaitSensitivity>;
                    }
                }, operations.at(op));
            }
            if (body != nullptr) {
                effect_free_body.emplace(body, free);
            }
            return free;
        };
        // A dead member can be left unscheduled and evaluated when its
        // output is read only if its value then is the value it would have
        // had: a combinational SystemVerilog member that runs at time zero
        // (continuous assignment, always_comb) and is sensitive to every
        // kernel-owned signal it reads, with no container access and no
        // effect but its writes. Any other candidate is a root, so what it
        // reads stays computed.
        const auto lazy_ok = [&](const std::size_t index) {
            const auto& candidate = candidates[index];
            if (!candidate || candidate->vhdl || !candidate->run_at_start
                || candidate->kind != static_cast<std::uint8_t>(
                       StaticKernelMemberKind::combinational)
                || !container_uses[index].empty() || !effect_free(views[index])) {
                return false;
            }
            return std::ranges::all_of(graph_processes[index].reads,
                [&](const Sensitivity& read) {
                    return read.signal < signal_count && owned[read.signal] != 0U
                        && alias_role[read.signal] == 0U
                        && std::ranges::any_of(candidate->sensitivity,
                            [&](const Sensitivity& sensitivity) {
                                // Width 0 is the whole signal.
                                return sensitivity.signal == read.signal
                                    && sensitivity.edge == EdgeKind::any
                                    && (sensitivity.width == 0U
                                        || (read.width != 0U
                                            && sensitivity.offset <= read.offset
                                            && read.offset + read.width
                                                <= sensitivity.offset
                                                    + sensitivity.width));
                            });
                });
        };
        // Candidates whose trigger edges stay inside the dead set: lazy only
        // off a cycle of activations.
        std::vector<std::uint8_t> sensitive(signal_count, 0U);
        std::vector<std::vector<ProcessId>> triggered(signal_count);
        if (enabled) {
            for (std::size_t index = 0U; index < process_count; ++index) {
                const auto& candidate = candidates[index];
                if (!views[index].valid()) {
                    continue;
                }
                if (!lazy_ok(index)) {
                    mark_process(index);
                }
                for (const auto& sensitivity : candidate
                         ? candidate->sensitivity : std::vector<Sensitivity> { }) {
                    if (sensitivity.signal < signal_count) {
                        triggered[sensitivity.signal].push_back(
                            static_cast<ProcessId>(index));
                    }
                }
            }
            for (SignalId signal = 0U; signal < signal_count; ++signal) {
                const auto& node = graph_signals[signal];
                const auto& info = signals_[signal];
                if (owned[signal] == 0U || host_touched[signal] != 0U
                    || alias_role[signal] != 0U
                    || node.descriptor.observations != RegionObservation::none
                    || (static_cast<std::uint32_t>(node.observations)
                           & static_cast<std::uint32_t>(RegionObservation::unknown
                               | RegionObservation::coverage)) != 0U
                    || node.writers_unknown || node.descriptor.external_driver
                    || info.implicit_driver.has_value()) {
                    mark_signal(signal);
                }
            }
            for (std::size_t object = 0U; object < object_count; ++object) {
                if (packed_alias[object] || has_element_alias[object] != 0U) {
                    mark_container(static_cast<ContainerObjectId>(object));
                }
            }
            propagate();
            // Tarjan over the dead candidates' trigger edges (a write to a
            // signal another dead candidate is sensitive to); a member on a
            // cycle, a self-trigger included, becomes a root.
            for (bool cycles = true; cycles;) {
                cycles = false;
                std::vector<std::uint32_t> order(process_count, 0U);
                std::vector<std::uint32_t> low(process_count, 0U);
                std::vector<std::uint8_t> on_stack(process_count, 0U);
                std::vector<ProcessId> stack;
                std::uint32_t counter = 0U;
                const auto successors = [&](const ProcessId process) {
                    std::vector<ProcessId> next;
                    for (const auto& write : graph_processes[process].writes) {
                        if (write.signal < signal_count) {
                            for (const auto target : triggered[write.signal]) {
                                if (live_process[target] == 0U) {
                                    next.push_back(target);
                                }
                            }
                        }
                    }
                    return next;
                };
                struct Frame {
                    ProcessId process;
                    std::vector<ProcessId> next;
                    std::size_t at { };
                };
                for (ProcessId root = 0U; root < process_count; ++root) {
                    if (live_process[root] != 0U || !candidates[root]
                        || order[root] != 0U) {
                        continue;
                    }
                    std::vector<Frame> frames;
                    const auto enter = [&](const ProcessId process) {
                        order[process] = low[process] = ++counter;
                        stack.push_back(process);
                        on_stack[process] = 1U;
                        frames.push_back({ process, successors(process) });
                    };
                    enter(root);
                    while (!frames.empty()) {
                        auto& frame = frames.back();
                        if (frame.at < frame.next.size()) {
                            const auto target = frame.next[frame.at++];
                            if (target == frame.process) {
                                mark_process(target);
                                cycles = true;
                            } else if (order[target] == 0U) {
                                enter(target);
                            } else if (on_stack[target] != 0U) {
                                low[frame.process]
                                    = std::min(low[frame.process], order[target]);
                            }
                            continue;
                        }
                        const auto process = frame.process;
                        frames.pop_back();
                        if (!frames.empty()) {
                            low[frames.back().process]
                                = std::min(low[frames.back().process], low[process]);
                        }
                        if (low[process] != order[process]) {
                            continue;
                        }
                        std::vector<ProcessId> component;
                        ProcessId popped { };
                        do {
                            popped = stack.back();
                            stack.pop_back();
                            on_stack[popped] = 0U;
                            component.push_back(popped);
                        } while (popped != process);
                        if (component.size() > 1U) {
                            for (const auto member_process : component) {
                                mark_process(member_process);
                            }
                            cycles = true;
                        }
                    }
                }
                propagate();
            }
            for (std::size_t index = 0U; index < process_count; ++index) {
                lazy[index] = candidates[index] && live_process[index] == 0U ? 1U : 0U;
            }
        }
    }

    // VHDL delta collapsing, exact subset (owner 2026-10-27). A VHDL member
    // whose any-edge sensitivity covers everything it reads and whose only
    // effects are zero-delay assignments to signals it alone drives computes
    // a function of its inputs. The kernel may evaluate such a member inside
    // the update phase that changed its inputs, writing its outputs in the
    // same phase, instead of one delta later, when no reader can tell: every
    // reader of what it writes is another such member (it recomputes from
    // the settled values), or a VHDL member that reads it only as data and
    // wakes only on signals that change before the first delta of a time
    // step. Such a member runs only in that first delta, when the delta
    // schedule has settled every chain anyway. A signal changes only before
    // the first delta when every writer is a SystemVerilog thread that
    // suspends only on a positive constant delay (a clock generator): the
    // kernel runs it before the time step's first round. The kernel confirms
    // each member's purity on its compiled code and keeps the subset closed
    // under the reader rule (StaticKernel::plan_delta_collapse). Clients
    // that observe arbitrary signals turn it off.
    std::vector<std::uint8_t> collapse(process_count, 0U);
    {
        // FSIM_PROFILE_KERNEL_PLAN: why each candidate stays out.
        std::vector<const char*> why(diagnose ? process_count : 0U, nullptr);
        std::vector<ProcessId> why_reader(diagnose ? process_count : 0U,
            std::numeric_limits<ProcessId>::max());
        const char* collapse_text = std::getenv("FSIM_STATIC_KERNEL_DELTA_COLLAPSE");
        bool enabled = vhdl_mode
            && (collapse_text == nullptr || std::string_view { collapse_text } != "0")
            && !access.foreign && !observe_all && access.observed.empty()
            && !access.coverage && !access.debug
            && !publish_all && graph.access_inventory_complete()
            && graph_processes.size() == process_count;
        // An operation whose accesses are opaque could write anything.
        for (std::size_t index = 0U; enabled && index < process_count; ++index) {
            enabled = !views[index].valid() || !graph_processes[index].dependencies_unknown;
        }
        // A thread whose only suspensions are positive constant delays and
        // whose only effects are immediate or nonblocking writes.
        const auto clock_thread = [&](const ProcessId process) {
            if (process >= process_count || !candidates[process]
                || candidates[process]->kind != behavioral_kind
                || candidates[process]->vhdl || !candidates[process]->waited.empty()
                || !graph_processes[process].sensitivities.empty()
                || graph_processes[process].dependencies_unknown
                || !container_uses[process].empty()) {
                return false;
            }
            const auto& operations = views[process].operations();
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                const auto operation = operations.expanded(op);
                if (const auto* wait = operation_get_if<WaitFor>(&operation)) {
                    if (wait->source || wait->delay == 0U) {
                        return false;
                    }
                    continue;
                }
                if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
                    if (read->kind != SignalReadKind::current || read->clock
                        || read->gate) {
                        return false;
                    }
                    continue;
                }
                if (!(operation_holds<WaitForever>(operation)
                        || operation_holds<Halt>(operation)
                        || operation_holds<Jump>(operation)
                        || operation_holds<Branch>(operation)
                        || operation_holds<DebugPoint>(operation)
                        || operation_holds<LoadConstant>(operation)
                        || operation_holds<CopyRegister>(operation)
                        || operation_holds<ConvertToTwoState>(operation)
                        || operation_holds<UnaryNot>(operation)
                        || operation_holds<LogicalNot>(operation)
                        || operation_holds<LogicalBinary>(operation)
                        || operation_holds<Binary>(operation)
                        || operation_holds<Reduction>(operation)
                        || operation_holds<Shift>(operation)
                        || operation_holds<Extract>(operation)
                        || operation_holds<Concatenate>(operation)
                        || operation_holds<Insert>(operation)
                        || operation_holds<ConditionalSelect>(operation)
                        || operation_holds<WriteBlocking>(operation)
                        || operation_holds<WriteBlockingSlice>(operation)
                        || operation_holds<WriteUpdate>(operation)
                        || operation_holds<WriteUpdateSlice>(operation))) {
                    return false;
                }
            }
            return true;
        };
        const auto plain_signal = [&](const SignalId signal) {
            const auto& node = graph_signals[signal];
            return owned[signal] != 0U && alias_role[signal] == 0U
                && host_touched[signal] == 0U && !node.writes_undeclared
                && !node.writers.empty() && !node.descriptor.external_driver
                && !signals_[signal].implicit_driver.has_value();
        };
        // A host signal nothing writes keeps its initial value.
        const auto constant_input = [&](const SignalId signal) {
            const auto& node = graph_signals[signal];
            return node.writers.empty() && !node.writes_undeclared
                && host_touched[signal] == 0U && !node.descriptor.external_driver
                && (static_cast<std::uint32_t>(node.observations)
                       & static_cast<std::uint32_t>(RegionObservation::mutation
                           | RegionObservation::unknown))
                    == 0U;
        };
        // Signals that change only before a time step's first delta.
        std::vector<std::uint8_t> time_only(signal_count, 0U);
        for (SignalId signal = 0U; enabled && signal < signal_count; ++signal) {
            const auto& node = graph_signals[signal];
            time_only[signal] = (plain_signal(signal)
                    && (static_cast<std::uint32_t>(node.observations)
                           & static_cast<std::uint32_t>(RegionObservation::mutation
                               | RegionObservation::unknown))
                        == 0U
                    && std::ranges::all_of(node.writers,
                        [&](const RegionAccess& writer) {
                            // A process that runs once runs at time zero.
                            return clock_thread(writer.process)
                                || (writer.process < process_count
                                    && candidates[writer.process]
                                    && candidates[writer.process]->kind == 2U);
                        }))
                    || constant_input(signal)
                ? 1U : 0U;
        }
        // Readers that wake only on such signals.
        std::vector<std::uint8_t> first_delta(process_count, 0U);
        for (std::size_t index = 0U; enabled && index < process_count; ++index) {
            const auto& candidate = candidates[index];
            first_delta[index] = candidate && candidate->vhdl
                    && candidate->kind != behavioral_kind && lazy[index] == 0U
                    && !candidate->sensitivity.empty()
                    && std::ranges::all_of(candidate->sensitivity,
                        [&](const Sensitivity& sensitivity) {
                            return sensitivity.signal < signal_count
                                && time_only[sensitivity.signal] != 0U;
                        })
                ? 1U : 0U;
        }
        for (std::size_t index = 0U; enabled && index < process_count; ++index) {
            const auto& candidate = candidates[index];
            if (!candidate || !candidate->vhdl || !candidate->run_at_start
                || lazy[index] != 0U
                || candidate->kind != static_cast<std::uint8_t>(
                       StaticKernelMemberKind::combinational)
                || !container_uses[index].empty() || !views[index].valid()
                || graph_processes[index].dependencies_unknown
                || graph_processes[index].writes.empty()) {
                continue;
            }
            const auto& node = graph_processes[index];
            const bool covered = std::ranges::all_of(node.reads,
                [&](const Sensitivity& read) {
                    return read.signal < signal_count
                        && (owned[read.signal] != 0U || constant_input(read.signal))
                        && alias_role[read.signal] == 0U
                        && std::ranges::any_of(candidate->sensitivity,
                            [&](const Sensitivity& sensitivity) {
                                return sensitivity.signal == read.signal
                                    && sensitivity.edge == EdgeKind::any
                                    && (sensitivity.width == 0U
                                        || (read.width != 0U
                                            && sensitivity.offset <= read.offset
                                            && read.offset + read.width
                                                <= sensitivity.offset
                                                    + sensitivity.width));
                            });
                });
            const bool outputs = std::ranges::all_of(node.writes,
                [&](const Process::DriverRegion& region) {
                    if (region.signal >= signal_count || !plain_signal(region.signal)) {
                        return false;
                    }
                    const auto& written = graph_signals[region.signal];
                    return written.observations == RegionObservation::none
                        && written.descriptor.observations == RegionObservation::none
                        && std::ranges::all_of(written.writers,
                            [&](const RegionAccess& writer) {
                                return writer.process == index;
                            });
                });
            collapse[index] = covered && outputs ? 1U : 0U;
            if (diagnose && collapse[index] == 0U) {
                why[index] = "outputs";
                for (const auto& read : node.reads) {
                    if (read.signal >= signal_count
                        || (owned[read.signal] == 0U && !constant_input(read.signal))) {
                        why[index] = "reads_unowned";
                        break;
                    }
                    if (alias_role[read.signal] != 0U) {
                        why[index] = "reads_alias";
                        break;
                    }
                }
                if (!covered && std::string_view { why[index] } == "outputs") {
                    why[index] = "reads_uncovered";
                }
                if (covered) {
                    for (const auto& region : node.writes) {
                        if (region.signal >= signal_count) {
                            break;
                        }
                        const auto& written = graph_signals[region.signal];
                        why[index] = owned[region.signal] == 0U ? "outputs_unowned"
                            : alias_role[region.signal] != 0U ? "outputs_alias"
                            : host_touched[region.signal] != 0U ? "outputs_host"
                            : !plain_signal(region.signal) ? "outputs_driver"
                            : written.observations != RegionObservation::none
                                || written.descriptor.observations != RegionObservation::none
                            ? "outputs_observed"
                            : !std::ranges::all_of(written.writers,
                                  [&](const RegionAccess& writer) {
                                      return writer.process == index;
                                  })
                            ? "outputs_writers" : nullptr;
                        if (why[index] != nullptr) {
                            break;
                        }
                    }
                }
            }
        }
        // Close the subset under the reader rule; a member on a cycle of
        // collapsed members (or reading its own output) leaves it.
        std::vector<std::uint32_t> indegree(process_count, 0U);
        std::vector<ProcessId> ready;
        for (bool changed = enabled; changed;) {
            changed = false;
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (collapse[index] == 0U) {
                    continue;
                }
                const bool readers_ok = std::ranges::all_of(
                    graph_processes[index].writes,
                    [&](const Process::DriverRegion& region) {
                        return std::ranges::all_of(graph_signals[region.signal].readers,
                            [&](const RegionAccess& reader) {
                                return reader.process < process_count
                                    && reader.process != index
                                    && (collapse[reader.process] != 0U
                                        || first_delta[reader.process] != 0U);
                            });
                    });
                if (!readers_ok) {
                    collapse[index] = 0U;
                    changed = true;
                    if (diagnose) {
                        // The first reader that fails the rule.
                        for (const auto& region : graph_processes[index].writes) {
                            for (const auto& reader : graph_signals[region.signal].readers) {
                                const auto other = reader.process;
                                if (why[index] != nullptr
                                    || (other < process_count && other != index
                                        && (collapse[other] != 0U
                                            || first_delta[other] != 0U))) {
                                    continue;
                                }
                                const auto* found = other < process_count
                                        && candidates[other]
                                    ? &*candidates[other] : nullptr;
                                why_reader[index] = other;
                                why[index] = other == index ? "reader_self"
                                    : found == nullptr ? "reader_host"
                                    : found->kind == behavioral_kind
                                    ? (found->vhdl ? "reader_vhdl_thread" : "reader_sv_thread")
                                    : !found->vhdl ? "reader_sv"
                                    : why[other] != nullptr ? "reader_vhdl_rejected"
                                    : "reader_vhdl_wakes";
                            }
                        }
                    }
                }
            }
            if (changed) {
                continue;
            }
            // Kahn over writer -> collapsed reader: what remains is on or
            // behind a cycle.
            std::ranges::fill(indegree, 0U);
            const auto for_each_successor = [&](const std::size_t index,
                                                const auto& visit) {
                for (const auto& region : graph_processes[index].writes) {
                    for (const auto& reader : graph_signals[region.signal].readers) {
                        if (collapse[reader.process] != 0U) {
                            visit(reader.process);
                        }
                    }
                }
            };
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (collapse[index] != 0U) {
                    for_each_successor(index,
                        [&](const ProcessId reader) { ++indegree[reader]; });
                }
            }
            ready.clear();
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (collapse[index] != 0U && indegree[index] == 0U) {
                    ready.push_back(static_cast<ProcessId>(index));
                }
            }
            while (!ready.empty()) {
                const auto process = ready.back();
                ready.pop_back();
                for_each_successor(process, [&](const ProcessId reader) {
                    if (--indegree[reader] == 0U) {
                        ready.push_back(reader);
                    }
                });
            }
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (collapse[index] != 0U && indegree[index] != 0U) {
                    collapse[index] = 0U;
                    changed = true;
                    if (diagnose) {
                        why[index] = "cycle";
                    }
                }
            }
        }
        if (diagnose && enabled) {
            std::map<std::string, std::size_t> reasons;
            std::map<std::string, std::string> examples;
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (why[index] != nullptr) {
                    ++reasons[why[index]];
                    const auto reader = why_reader[index];
                    examples.try_emplace(why[index],
                        std::string { views[index].name() }
                            + (reader < process_count
                                    ? " <- " + std::string { views[reader].name() }
                                    : std::string { }));
                }
            }
            // The first sensitivity signal of each VHDL member that is not
            // time-only, by cause.
            std::map<std::string, std::size_t> clocks;
            std::map<std::string, std::string> clock_examples;
            for (std::size_t index = 0U; index < process_count; ++index) {
                const auto& candidate = candidates[index];
                if (!candidate || !candidate->vhdl || first_delta[index] != 0U
                    || candidate->kind == behavioral_kind) {
                    continue;
                }
                for (const auto& sensitivity : candidate->sensitivity) {
                    const auto signal = sensitivity.signal;
                    if (signal >= signal_count || time_only[signal] != 0U) {
                        continue;
                    }
                    const auto& node = graph_signals[signal];
                    std::string cause = owned[signal] == 0U ? "unowned"
                        : alias_role[signal] != 0U ? "alias"
                        : host_touched[signal] != 0U ? "host"
                        : node.writers.empty() ? "undriven"
                        : !plain_signal(signal) ? "driver"
                        : "writer";
                    if (cause == "writer") {
                        for (const auto& writer : node.writers) {
                            if (!clock_thread(writer.process)
                                && !(writer.process < process_count
                                    && candidates[writer.process]
                                    && candidates[writer.process]->kind == 2U)) {
                                const auto& found = writer.process < process_count
                                    ? candidates[writer.process] : std::nullopt;
                                cause = !found ? "writer_host"
                                    : found->kind != behavioral_kind
                                    ? (found->vhdl ? "writer_vhdl_member" : "writer_sv_member")
                                    : found->vhdl ? "writer_vhdl_thread"
                                    : "writer_sv_thread";
                                clock_examples.try_emplace(cause,
                                    views[writer.process].name());
                                break;
                            }
                        }
                    }
                    ++clocks[cause];
                    clock_examples.try_emplace(cause, views[index].name());
                    break;
                }
            }
            for (const auto& [cause, count] : clocks) {
                std::cerr << "fsim-kernel-plan: collapse-wake cause=" << cause
                          << " count=" << count << " example="
                          << clock_examples[cause] << '\n';
            }
            std::cerr << "fsim-kernel-plan: collapse admitted="
                      << std::ranges::count(collapse, 1U) << " time_only="
                      << std::ranges::count(time_only, 1U) << " first_delta="
                      << std::ranges::count(first_delta, 1U) << '\n';
            for (const auto& [reason, count] : reasons) {
                std::cerr << "fsim-kernel-plan: collapse-reject reason=" << reason
                          << " count=" << count << " example=" << examples[reason]
                          << '\n';
            }
        }
    }

    // 3. Members, specification, host and boundaries.
    auto spec = std::make_shared<StaticKernelRuntimeSpec>();
    std::vector<std::uint8_t> member(process_count, 0U);
    for (std::size_t index = 0U; index < process_count; ++index) {
        // A live wiring's or conversion's program is the kernel's own update
        // (below), never a resolver member.
        if (!candidates[index]
            || (resolution_program[index] != 0U && wiring_program[index] != 0U)) {
            continue;
        }
        member[index] = 1U;
        auto& candidate = *candidates[index];
        StaticKernelMemberSpec runtime_member;
        runtime_member.process = candidate.process;
        runtime_member.kind = static_cast<StaticKernelMemberKind>(candidate.kind);
        runtime_member.run_at_start = candidate.run_at_start;
        runtime_member.start_late = candidate.start_late;
        runtime_member.start_waiting = candidate.start_waiting;
        runtime_member.body_begin = candidate.body_begin;
        runtime_member.body_end = candidate.body_end;
        runtime_member.exit_alt = candidate.exit_alt;
        runtime_member.vhdl = candidate.vhdl;
        runtime_member.sensitivity = std::move(candidate.sensitivity);
        runtime_member.lazy = lazy[index] != 0U;
        runtime_member.collapse = collapse[index] != 0U;
        plan.lazy_members += lazy[index];
        plan.collapse_members += collapse[index];
        spec->members.push_back(std::move(runtime_member));
    }
    // Live conversions: members the kernel runs in its update phase, never
    // scheduled. The body is the program followed by the formal's write, a
    // wait and the loop jump the kernel appends (StaticKernel), so it has a
    // combinational member's shape.
    for (const auto& conversion : conversions) {
        if (!conversion.live || member[conversion.program] != 0U) {
            continue;
        }
        member[conversion.program] = 1U;
        StaticKernelMemberSpec runtime_member;
        runtime_member.process = conversion.program;
        runtime_member.kind = StaticKernelMemberKind::combinational;
        runtime_member.body_begin = 0U;
        runtime_member.body_end = static_cast<std::uint32_t>(
            views[conversion.program].operations().size() + 1U);
        runtime_member.vhdl = true;
        runtime_member.conversion_target = conversion.target;
        spec->members.push_back(std::move(runtime_member));
    }
    // Commit programs (S25b): the programs of the update commit (resolvers,
    // conversion drivers and their stubs) are not scheduled processes; the
    // census counts them as kernel-owned rows of their program kind, never
    // as host processes. Whatever the candidate classification said about
    // their operations is replaced by that kind.
    const auto census_commit_programs = [&](std::vector<std::uint8_t>& kernel_rows,
                                            const std::vector<std::uint8_t>& wired) {
        if (!census) {
            return;
        }
        // The program's role in the update commit (IEEE 1076-2008 14.7.3):
        // a conversion of one source (an input port's conversion function),
        // a resolution function over drivers, a guarded variant, or an
        // output port's conversion driver.
        std::vector<const char*> role(process_count, nullptr);
        for (std::size_t signal = 0U; signal < signals_.size(); ++signal) {
            if (const auto& resolver = signals_[signal].vhdl_resolver) {
                if (resolver->process < process_count) {
                    role[resolver->process] = resolver->driving_mirror
                        ? "commit_program:formal"
                        : !resolver->drivers.empty()
                        ? "commit_program:resolution"
                        : resolver->sources.size() == 1U
                            && resolver->variants.empty()
                            && resolver->guarded_kind == 0U
                        ? "commit_program:conversion"
                        : "commit_program:resolver";
                }
                for (const auto& variant : resolver->variants) {
                    if (variant.process < process_count) {
                        role[variant.process] = "commit_program:guarded";
                    }
                }
            }
            for (const auto& driver : signals_[signal].vhdl_conversion_drivers) {
                if (driver.program < process_count) {
                    role[driver.program] = driver.sources.size() == 1U
                        ? "commit_program:driver"
                        : "commit_program:driver_multi";
                }
            }
        }
        // A stub (a halted record that never starts: a conversion driver's
        // driver identity, or an output adapter whose routes a composer
        // program took over) executes nothing on either side; the program
        // that does its work has its own row.
        const auto stub = [&](const std::size_t index) {
            const auto& view = views[index];
            return view.valid() && !view.initialize()
                && view.static_sensitivity().empty()
                && view.operations().size() == 1U
                && operation_holds<Halt>(view.operations().expanded(0U));
        };
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (member[index] != 0U || host_program[index] == 0U) {
                continue;
            }
            if (wired[index] != 0U || stub(index)) {
                // A live wiring or conversion: the kernel drives its target
                // in its own update phase, and the host never runs it. A
                // stub runs nowhere.
                kernel_rows[index] = 0U;
                member[index] = 1U;
                continue;
            }
            const auto& view = views[index];
            kernel_rows[index] = 1U;
            rejection[index] = !view.valid() ? "commit_program"
                : view.switch_source() || view.switch_target()
                ? "commit_program_switch"
                : !view.initialize() && view.operations().size() == 1U
                ? "commit_program_stub"
                : role[index] != nullptr ? role[index]
                : "commit_program";
        }
    };
    // A design whose processes are all hosted still runs on the kernel: it
    // schedules them (engine-v4-semantics S25).
    if (spec->members.empty()
        && std::ranges::none_of(hosted, [](const auto flag) { return flag != 0U; })) {
        // Every valid process is a host program (port-association resolver,
        // conversion driver), run inline by the update commit, or a SystemC
        // process, run by the SystemC kernel: none is a scheduled HDL
        // process. The kernel still runs, with nothing to schedule: its
        // update commit runs the programs natively.
        const bool only_host_programs = [&] {
            bool any = false;
            for (std::size_t index = 0U; index < process_count; ++index) {
                if (views[index].valid() && host_program[index] == 0U
                    && systemc_process[index] == 0U) {
                    return false;
                }
                any = any || views[index].valid();
            }
            return any;
        }();
        if (!only_host_programs) {
            return disable("no_members");
        }
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
    // A live wiring's program reads its source on the host only as the
    // driver identity; the kernel copies the value.
    std::vector<std::uint8_t> wired_program(process_count, 0U);
    for (const auto& wiring : wirings) {
        if (wiring.live) {
            wired_program[wiring.program] = 1U;
        }
    }
    for (const auto& wiring : driver_wirings) {
        if (wiring.live) {
            wired_program[wiring.program] = 1U;
            wired_program[wiring.stub] = 1U;
        }
    }
    for (const auto& conversion : conversions) {
        if (conversion.live) {
            wired_program[conversion.program] = 1U;
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
    // A conversion member is its formal's only writer.
    for (const auto& conversion : conversions) {
        if (conversion.live
            && std::ranges::none_of(spec->writer_regions,
                [&](const auto& region) {
                    return region.signal == conversion.target;
                })) {
            spec->writer_regions.push_back({ conversion.target,
                conversion.program, 0U,
                static_cast<std::uint32_t>(
                    signals_[conversion.target].initial_value.width()) });
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
    // A member waiting on a host signal's transactions also needs the host
    // to activate the kernel when the signal is active without changing
    // (IEEE 1076-2019 16.2.4).
    for (const auto& entry : spec->members) {
        for (const auto& sensitivity : entry.sensitivity) {
            host_input(sensitivity.signal);
            if (sensitivity.edge == EdgeKind::transaction
                && sensitivity.signal < signal_count
                && input[sensitivity.signal] != 0U
                && std::ranges::none_of(host_sensitivity,
                    [&](const Sensitivity& present) {
                        return present.signal == sensitivity.signal
                            && present.edge == EdgeKind::transaction;
                    })) {
                host_sensitivity.push_back(
                    { sensitivity.signal, EdgeKind::transaction });
            }
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
    // A member reading a host signal's history ('EVENT, $last_event...)
    // reads the host's, which records an update when the host commits it.
    std::vector<std::uint8_t> history_read(signal_count, 0U);
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (member[index] == 0U || !views[index].valid()) {
            continue;
        }
        const auto& operations = views[index].operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            const auto& operation = operations.expanded(op);
            const auto mark = [&](const auto* query) {
                if (query != nullptr && query->signal < signal_count) {
                    history_read[query->signal] = 1U;
                }
            };
            mark(operation_get_if<SignalEvent>(&operation));
            mark(operation_get_if<SignalActive>(&operation));
            mark(operation_get_if<SignalLastValue>(&operation));
            mark(operation_get_if<SignalLastEvent>(&operation));
            mark(operation_get_if<SignalLastActive>(&operation));
            mark(operation_get_if<SignalStrengthText>(&operation));
        }
    }
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        const auto& node = graph_signals[signal];
        // A foreign library may deposit into a signal no process writes
        // between kernel rounds.
        // A force writes the signal as well (IEEE 1800-2023 10.6.2): a
        // hosted process that forces an input changes it inside the
        // activation, where only the written inputs are rescanned.
        if (!forces && !access.foreign && owned[signal] == 0U
            && virtual_owned[signal] == 0U
            && node.writers.empty() && alias_written[signal] == 0U
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
        // does not apply a member's update unchanged and at once. A mirrored
        // update would wake a member whose history query (history_read) does
        // not see it yet. Nor would it change the aliased host object
        // (container_aliased).
        if (!forces && !access.foreign && path_destination[signal] == 0U
            && container_aliased[signal] == 0U
            && path_source[signal] == 0U && history_read[signal] == 0U
            && owned[signal] == 0U && virtual_owned[signal] == 0U
            && !node.writers.empty()
            // One driver of an unresolved variable or a plain net (no pull,
            // no charge storage) resolves to that driver's value.
            && (signals_[signal].resolution == ResolutionKind::none
                || (signals_[signal].resolution == ResolutionKind::sv_wire
                    && !signals_[signal].implicit_driver
                    && !signals_[signal].charge_strength))
            && std::ranges::all_of(node.writers, [&](const auto& writer) {
                   // A driver with a drive strength resolves to a value
                   // its own may not be (a highz0 driver of 0 is z).
                   return writer.process == node.writers.front().process
                       && writer.process < process_count
                       && member[writer.process] != 0U
                       && views[writer.process].drive_strength()
                           == DriveStrength { };
               })
            && std::ranges::any_of(node.readers, [&](const auto& reader) {
                   return reader.process < process_count && member[reader.process] != 0U;
               })) {
            spec->sole_writer_inputs.push_back(signal);
        }
    }
    // The kernel runs as a host process whose stub replaces the first
    // member's program. A design of hosted processes or commit programs only
    // has no member to replace: the stub then runs as a process of its own,
    // appended after the design's processes (plan.host == process_count;
    // create_interpreter adds it), shaped by host_shape.
    plan.host = spec->members.empty() ? static_cast<ProcessId>(process_count)
                                      : spec->members.front().process;
    plan.host_program = host_stub(
        views[host_shape(*spec, process_count,
            [&](const ProcessId id) { return views[id].valid(); })],
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
    std::vector<std::uint8_t> kernel_owned = hosted;
    census_commit_programs(kernel_owned, wired_program);
    take_census(plan, &member, &kernel_owned);
    return plan;
    };
    const auto plan_access = [&](const bool allow_mixed) {
        auto planned = plan_for(allow_mixed);
        while (host_access_grew) {
            host_access_grew = false;
            planned = plan_for(allow_mixed);
        }
        return planned;
    };
    auto plan = plan_access(true);
    if (plan.disabled && mixed_planned) {
        std::ranges::fill(host_access_objects, 0U);
        plan = plan_access(false);
    }
    return plan;
}


namespace detail {
namespace {

constexpr std::string_view plan_magic { "fsim-static-kernel-plan-v8" };

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
             "FSIM_STATIC_KERNEL_HOSTED_POSTPONED",
             "FSIM_STATIC_KERNEL_DEAD_LOGIC",
             "FSIM_STATIC_KERNEL_DELTA_COLLAPSE" }) {
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
        out.put(member.start_waiting);
        out.put(member.lazy);
        out.put(member.collapse);
        out.put(member.conversion_target);
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
                   && in.get(member.start_late)
                   && in.get(member.start_waiting) && in.get(member.lazy)
                   && in.get(member.collapse)
                   && in.get(member.conversion_target)
                   && (member.conversion_target
                           == std::numeric_limits<SignalId>::max()
                       || signal_ok(member.conversion_target));
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
    // design's processes when there is no member (plan_static_kernel).
    const bool synthetic_host_process = spec->members.empty();
    if (!read
        || (synthetic_host_process
                ? plan.host != processes
                : plan.host == processes
                    || spec->members.front().process != plan.host)) {
        return std::nullopt;
    }
    const auto shape = host_shape(*spec, processes, [&](const ProcessId id) {
        return ElaboratedDesignProcessAccess::process_view(design, id).valid();
    });
    if (shape >= processes) {
        return std::nullopt;
    }
    plan.host_program = host_stub(
        ElaboratedDesignProcessAccess::process_view(design, shape),
        std::move(host_sensitivity));
    if (synthetic_host_process) {
        plan.host_program = synthetic_host(std::move(plan.host_program));
    }
    plan.members = spec->members.size();
    plan.hosted_processes = spec->hosted.size();
    plan.lazy_members = static_cast<std::size_t>(std::ranges::count_if(
        spec->members, &StaticKernelMemberSpec::lazy));
    plan.collapse_members = static_cast<std::size_t>(std::ranges::count_if(
        spec->members, &StaticKernelMemberSpec::collapse));
    plan.owned_signals = spec->owned_signals.size();
    plan.boundary_outputs = spec->boundary_outputs.size();
    plan.owned_containers = spec->containers.size();
    plan.spec = std::move(spec);
    return plan;
}

} // namespace detail

} // namespace fsim::elaboration
