// SPDX-License-Identifier: Apache-2.0
#include "application_sdf_session.hpp"
#include "application_internal.hpp"
#include "application_sdf_vital_generics.hpp"

#include "fsim/app/sdf_annotation_scope.hpp"
#include "fsim/app/sdf_cell_resolution.hpp"
#include "fsim/app/sdf_endpoint_resolution.hpp"
#include "fsim/app/sdf_mapping_validation.hpp"
#include "fsim/app/sdf_target_plan.hpp"
#include "fsim/app/sdf_delay_modes.hpp"
#include "fsim/app/sdf_precedence.hpp"
#include "fsim/app/sdf_scheduling.hpp"
#include "fsim/app/sdf_drive_timing.hpp"
#include "fsim/app/sdf_schema.hpp"
#include "fsim/elaboration/sdf_net_delay_split.hpp"
#include "fsim/app/sdf_artifact_identity.hpp"
#include "fsim/app/sdf_portable_archive.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/compiler/object_cache.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <ranges>

namespace fsim::app {
namespace {

void import_errors(diagnostic::Engine& output,
    const std::vector<frontend::Diagnostic>& diagnostics)
{
    for (const auto& diagnostic : diagnostics)
        application_detail::import_diagnostic(output, diagnostic);
}

void append_identity_field(std::string& target,
    const std::string_view value)
{
    target += std::to_string(value.size());
    target.push_back(':');
    target.append(value);
}

bool pattern_matches(const std::string_view pattern,
    const std::string_view name)
{
    std::size_t pattern_index { };
    std::size_t name_index { };
    std::size_t star = std::string_view::npos;
    std::size_t retry { };
    while (name_index < name.size()) {
        if (pattern_index < pattern.size()
            && (pattern[pattern_index] == '?'
                || pattern[pattern_index] == name[name_index])) {
            ++pattern_index;
            ++name_index;
        } else if (pattern_index < pattern.size()
            && pattern[pattern_index] == '*') {
            star = pattern_index++;
            retry = name_index;
        } else if (star != std::string_view::npos) {
            pattern_index = star + 1U;
            name_index = ++retry;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size()
        && pattern[pattern_index] == '*')
        ++pattern_index;
    return pattern_index == pattern.size();
}

bool cell_matches(const SdfControlInput& input,
    const std::string_view instance)
{
    if (input.root != "*" && instance != input.root
        && !(instance.size() > input.root.size()
            && instance.starts_with(input.root)
            && instance[input.root.size()] == '.'))
        return false;
    const auto relative = input.root != "*"
        && instance.size() > input.root.size()
        ? instance.substr(input.root.size() + 1U)
        : instance;
    return pattern_matches(input.cell_pattern, instance)
        || pattern_matches(input.cell_pattern, relative);
}

std::optional<std::uint64_t> resolution_femtoseconds(
    const std::string_view text)
{
    const auto parsed = application_detail::magnitude_and_unit(text);
    const auto factor = parsed
        ? application_detail::unit_femtoseconds(parsed->unit)
        : std::nullopt;
    if (!parsed || !factor || parsed->magnitude == 0U
        || parsed->magnitude
            > std::numeric_limits<std::uint64_t>::max() / *factor)
        return std::nullopt;
    return parsed->magnitude * *factor;
}

elaboration::SdfNetDelayTerminal net_delay_terminal(
    const SdfResolvedEndpoint& endpoint)
{
    elaboration::SdfNetDelayTerminal result;
    result.instance = endpoint.instance_path;
    result.object = endpoint.object_path;
    result.signal = endpoint.signal;
    result.port = endpoint.object_kind == SdfEndpointObjectKind::HdlPort;
    if (endpoint.select) {
        result.select_width = static_cast<std::uint32_t>(endpoint.select->width);
        result.select_left = endpoint.select->left;
        result.select_right = endpoint.select->right;
    }
    return result;
}

// The persistent record of an INTERCONNECT, PORT or NETDELAY annotation: its
// terminals and the twelve effective transition delays (IEEE 1800-2023 37.37,
// 37.45). A NETDELAY is a PORT delay on each load it reaches (IEEE 1364-2005
// 16.2.4), so its record names the first endpoint until the loads replace it
// (net_delay_records).
std::optional<elaboration::SdfNetDelayInfo> net_delay_record(
    const SdfPlannedAnnotation& annotation,
    const SdfPrecedenceApplication& precedence)
{
    const auto terminal = net_delay_terminal;
    const auto with_role = [&](const SdfEndpointRole role) {
        return std::ranges::find(
            annotation.endpoints, role, &SdfResolvedEndpoint::role);
    };
    elaboration::SdfNetDelayInfo result;
    result.instance = annotation.target_instance_path;
    result.identity = annotation.target_identity;
    if (annotation.construct_kind == frontend::SdfConstructKind::NetDelay) {
        if (annotation.endpoints.empty())
            return std::nullopt;
        result.kind = elaboration::SdfNetDelayKind::port;
        result.source = terminal(annotation.endpoints.front());
        result.destination = result.source;
    } else if (annotation.target_kind == SdfTimingTargetKind::Interconnect) {
        const auto source = with_role(SdfEndpointRole::InterconnectSource);
        const auto destination
            = with_role(SdfEndpointRole::InterconnectDestination);
        if (source == annotation.endpoints.end()
            || destination == annotation.endpoints.end())
            return std::nullopt;
        result.kind = elaboration::SdfNetDelayKind::interconnect;
        result.source = terminal(*source);
        result.destination = terminal(*destination);
    } else {
        if (annotation.endpoints.size() != 1U)
            return std::nullopt;
        result.kind = elaboration::SdfNetDelayKind::port;
        result.source = terminal(annotation.endpoints.front());
        result.destination = result.source;
    }
    // The pulse limits of a delay that gives them (a delval of two or three
    // values): each as the twelve transitions by the rules of the delays; a
    // limit that is not given follows its delay, and an error limit its
    // reject limit (IEEE 1364-2005 14.6, 16.2.4).
    if (!annotation.reject_ticks.empty()
        && annotation.reject_ticks.size() == annotation.after_ticks.size()
        && annotation.error_ticks.size() == annotation.after_ticks.size()) {
        std::vector<std::uint64_t> reject;
        std::vector<std::uint64_t> error;
        for (std::size_t index = 0U; index < annotation.after_ticks.size();
            ++index) {
            const auto given = [&](const std::uint64_t limit,
                                   const std::uint64_t otherwise) {
                return limit != sdf_no_pulse_limit ? limit : otherwise;
            };
            reject.push_back(given(
                annotation.reject_ticks[index], annotation.after_ticks[index]));
            error.push_back(given(annotation.error_ticks[index], reject.back()));
        }
        const auto rejects = expand_sdf_transition_delays(reject);
        const auto errors = expand_sdf_transition_delays(error);
        if (rejects && errors) {
            result.reject.assign(rejects->begin(), rejects->end());
            result.error.assign(errors->begin(), errors->end());
        }
    }
    const auto effective
        = precedence.find_target(annotation.target_kind, annotation.target_identity);
    result.delays.assign(12U, 0U);
    if (effective.empty()) {
        const auto profile = expand_sdf_transition_delays(annotation.after_ticks);
        if (!profile)
            return std::nullopt;
        result.delays.assign(profile->begin(), profile->end());
        return result;
    }
    for (const auto& value : effective) {
        if (value.value_index >= result.delays.size()
            || !value.effective_delay_ticks)
            return std::nullopt;
        result.delays[value.value_index] = *value.effective_delay_ticks;
    }
    return result;
}


// An INCREMENT adds to the delay the earlier files annotated to the same load
// (IEEE 1364-2005 16.3: the constructs apply in order of occurrence, an
// INCREMENT to what is there): the delays and, when either record gives pulse
// limits, the limits, each a delay when it is not given.
void add_to_previous(elaboration::SdfNetDelayInfo& record,
    const elaboration::SdfNetDelayInfo& previous)
{
    if (record.delays.size() != 12U || previous.delays.size() != 12U)
        return;
    const auto limit = [](const std::vector<runtime::SimulationTick>& given,
                           const std::vector<runtime::SimulationTick>& otherwise,
                           const std::size_t index) {
        return given.size() == 12U ? given[index] : otherwise[index];
    };
    const bool limited = !record.reject.empty() || !previous.reject.empty();
    std::vector<runtime::SimulationTick> reject;
    std::vector<runtime::SimulationTick> error;
    for (std::size_t index = 0U; index < 12U; ++index) {
        if (limited) {
            const auto new_reject = limit(record.reject, record.delays, index);
            const auto old_reject = limit(previous.reject, previous.delays, index);
            reject.push_back(old_reject + new_reject);
            error.push_back(limit(previous.error, previous.reject.empty()
                                      ? previous.delays : previous.reject, index)
                + limit(record.error, record.reject.empty()
                          ? record.delays : record.reject, index));
        }
        record.delays[index] += previous.delays[index];
    }
    record.reject = std::move(reject);
    record.error = std::move(error);
}

// One input or inout port, or an output port, of a Verilog or SystemVerilog
// instance, by the signal its path names.
struct DeclaredPort {
    std::string instance;
    std::string path;
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
};

using DeclaredPorts
    = std::map<runtime::simir::SignalId, std::vector<DeclaredPort>>;

// The ports of the instances (IEEE 1800-2023 37.14), from the same design IR
// and HIR records the VPI port objects come from. A port that shares its
// actual's net names the net's signal; one with an adapter of its own (a
// different width) names its own, so it is not a load of the net.
DeclaredPorts declared_ports(const elaboration::ElaboratedDesign& design,
    const SdfPortSource& source)
{
    DeclaredPorts result;
    if (source.design_ir == nullptr || source.systemverilog_hir == nullptr)
        return result;
    const auto& ir = *source.design_ir;
    const auto& hir = *source.systemverilog_hir;
    for (const auto& instance : ir.instances()) {
        const auto& specialization
            = ir.specializations().at(instance.specialization.value());
        const auto unit = std::ranges::find(
            hir.units(), specialization.unit, &semantic::sv::Unit::id);
        if (unit == hir.units().end())
            continue;
        const std::string instance_path { ir.path(instance.path) };
        for (const auto declaration_id : unit->declarations) {
            const auto* const declaration = hir.find_declaration(declaration_id);
            if (declaration == nullptr
                || declaration->form != semantic::sv::DeclarationForm::port
                || !declaration->interface_type.empty())
                continue;
            DeclaredPort port;
            port.instance = instance_path;
            port.path = instance_path + "." + declaration->name;
            switch (declaration->direction) {
            case semantic::sv::Direction::input:
                port.direction = frontend::PortDirection::Input;
                break;
            case semantic::sv::Direction::output:
                port.direction = frontend::PortDirection::Output;
                break;
            case semantic::sv::Direction::inout:
                port.direction = frontend::PortDirection::Inout;
                break;
            default:
                continue;
            }
            if (const auto signal = design.find_signal(port.path))
                result[*signal].push_back(std::move(port));
        }
    }
    return result;
}

// The records of a NETDELAY (IEEE 1364-2005 16.2.4): on an input or inout
// port the delay is that port's interconnect delay from all its sources, on
// a net it is the delay of every input and inout port the net reaches; an
// output port is an error. Each record is a PORT-kind record of one load
// whose identity names the load, so that a later record of the same load
// replaces it (16.3). False with `problem` set for an output port; a net
// that reaches no load gives no record and sets `warning`.
bool net_delay_records(const SdfPlannedAnnotation& annotation,
    const elaboration::SdfNetDelayInfo& base, const DeclaredPorts& ports,
    std::vector<elaboration::SdfNetDelayInfo>& records, std::string& problem,
    std::string& warning, const bool design_has_vhdl)
{
    constexpr std::string_view vital
        = "NETDELAY is unsupported for VHDL/VITAL targets and has no effect "
          "on them (IEEE 1076.4-2000 5.2.7.1, 5.2)";
    // The delay device belongs to the module of the load port.
    const auto add = [&](elaboration::SdfNetDelayTerminal terminal,
                         std::string instance) {
        auto record = base;
        record.instance = std::move(instance);
        record.identity = annotation.target_identity + '\x1f' + terminal.object;
        if (std::ranges::any_of(records, [&](const auto& each) {
                return each.identity == record.identity;
            }))
            return;
        record.source = terminal;
        record.destination = std::move(terminal);
        records.push_back(std::move(record));
    };
    for (const auto& endpoint : annotation.endpoints) {
        if (endpoint.language == SdfScopeRootLanguage::Vhdl) {
            warning = "NETDELAY " + endpoint.object_path
                + " names a VHDL object: " + std::string { vital };
            continue;
        }
        auto terminal = net_delay_terminal(endpoint);
        const DeclaredPort* named { };
        if (const auto found = ports.find(endpoint.signal);
            found != ports.end()) {
            const auto port = std::ranges::find(
                found->second, endpoint.object_path, &DeclaredPort::path);
            if (port != found->second.end())
                named = &*port;
        }
        const auto direction = named != nullptr ? named->direction
            : endpoint.object_kind == SdfEndpointObjectKind::HdlPort
            ? endpoint.direction : frontend::PortDirection::Unknown;
        if (direction == frontend::PortDirection::Output) {
            problem = "NETDELAY " + endpoint.object_path
                + " names an output port; it annotates only input or inout "
                  "ports or nets (IEEE 1364-2005 16.2.4)";
            return false;
        }
        if (named != nullptr
            || endpoint.object_kind == SdfEndpointObjectKind::HdlPort) {
            auto instance = named != nullptr ? named->instance
                                             : annotation.target_instance_path;
            add(std::move(terminal), std::move(instance));
            continue;
        }
        bool reached { };
        if (const auto found = ports.find(endpoint.signal);
            found != ports.end()) {
            for (const auto& port : found->second) {
                if (port.direction == frontend::PortDirection::Output
                    || port.path == endpoint.object_path)
                    continue;
                auto load = terminal;
                load.instance = port.instance;
                load.object = port.path;
                load.port = true;
                add(std::move(load), port.instance);
                reached = true;
            }
        }
        if (!reached)
            warning = "NETDELAY " + endpoint.object_path
                + (design_has_vhdl
                        ? " reaches no input or inout port of a Verilog or "
                          "SystemVerilog instance, and its VHDL loads get no "
                          "delay: " + std::string { vital }
                        : std::string { " reaches no input or inout port of "
                                        "a Verilog or SystemVerilog instance" });
    }
    return true;
}
} // namespace

SdfControlRequest make_cli_sdf_request(const cli::Invocation& invocation,
    const SdfControlPhase phase,
    const project::DelayMode default_mode)
{
    SdfControlRequest request;
    request.surface = SdfControlSurface::ProjectCli;
    request.phase = phase;
    const auto mode = invocation.delay_mode.value_or(default_mode);
    request.selection = mode == project::DelayMode::minimum
        ? SdfDelaySelection::Minimum
        : mode == project::DelayMode::maximum
            ? SdfDelaySelection::Maximum : SdfDelaySelection::Typical;
    request.report_limit = invocation.sdf_report_limit.value_or(
        request.report_limit);
    for (std::size_t index = 0; index < invocation.sdf_files.size(); ++index) {
        request.inputs.push_back({
            support::path_to_utf8(invocation.sdf_files[index]),
            invocation.sdf_root.value_or("*"), invocation.sdf_cell,
            static_cast<std::uint64_t>(index), 0U });
    }
    return request;
}

std::optional<SdfSessionPlan> plan_sdf_session_inputs(
    const elaboration::ElaboratedDesign& design,
    const std::string_view time_resolution,
    const std::string_view design_identity, SdfControlRequest request,
    const std::span<const std::filesystem::path> resolved_paths,
    diagnostic::Engine& diagnostics, const SdfPortSource ports,
    const bool vital_applied)
{
    if (!resolved_paths.empty()
        && resolved_paths.size() != request.inputs.size()) {
        diagnostics.error("FSIM-SDF-SESSION-001",
            "SDF resolved file paths do not match control inputs");
        return std::nullopt;
    }
    const auto generation = request.generation == 0U
        ? 1U : request.generation;
    request.generation = 0U;
    const auto controlled = apply_sdf_control(request);
    if (!controlled.ok()) {
        import_errors(diagnostics, controlled.diagnostics);
        return std::nullopt;
    }
    const auto precision = resolution_femtoseconds(time_resolution);
    if (!precision) {
        diagnostics.error("FSIM-SDF-SESSION-001",
            "SDF annotation requires a valid design time resolution");
        return std::nullopt;
    }
    auto candidate = elaboration::ElaboratedDesign::from_state(
        design.state());
    if (!candidate) {
        diagnostics.error("FSIM-SDF-SESSION-001",
            "SDF annotation requires a valid elaborated design");
        return std::nullopt;
    }
    SdfTimingPrecedencePolicy baseline_policy;
    baseline_policy.command_selection = request.selection;
    const auto baseline_precedence = apply_sdf_precedence(
        nullptr, design, baseline_policy);
    if (!baseline_precedence.ok()) {
        import_errors(diagnostics, baseline_precedence.diagnostics);
        return std::nullopt;
    }
    const auto baseline_scheduling = apply_sdf_scheduling(
        baseline_precedence.application, design);
    if (!baseline_scheduling.ok()) {
        import_errors(diagnostics, baseline_scheduling.diagnostics);
        return std::nullopt;
    }
    const auto baseline_drive = apply_sdf_drive_timing(
        baseline_scheduling.application);
    if (!baseline_drive.ok()) {
        import_errors(diagnostics, baseline_drive.diagnostics);
        return std::nullopt;
    }
    std::vector<SdfReannotationLayer> layers;
    std::vector<std::shared_ptr<const SdfSessionPublication>> pending;
    pending.reserve(request.inputs.size());
    constexpr std::size_t maximum_body_bytes = 64U << 20U;
    std::size_t body_bytes { };
    std::vector<const SdfControlInput*> ordered;
    ordered.reserve(controlled.application->request().inputs.size());
    for (const auto& input : controlled.application->request().inputs)
        ordered.push_back(&input);
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const auto* left, const auto* right) {
            return std::tie(left->file_precedence, left->cell_precedence)
                < std::tie(right->file_precedence, right->cell_precedence);
        });
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>>
        target_precedence;
    // The interconnect and port delays the files create; a later file
    // replaces an earlier delay of the same target.
    std::vector<elaboration::SdfNetDelayInfo> net_delays;
    // The ports of the instances, found when the first NETDELAY needs them.
    DeclaredPorts declared;
    bool declared_loaded { };
    bool design_has_vhdl { };

    for (const auto* selected_input : ordered) {
        const auto& input = *selected_input;
        const auto path = resolved_paths.empty()
            ? support::path_from_utf8(input.source_identity)
            : resolved_paths[static_cast<std::size_t>(selected_input
                  - controlled.application->request().inputs.data())];
        const auto source = application_detail::read_binary_payload(
            path, maximum_body_bytes - body_bytes);
        if (!source.bytes) {
            diagnostics.error("FSIM-SDF-SESSION-001",
                "cannot read SDF input within the 64 MiB aggregate limit: "
                    + input.source_identity);
            return std::nullopt;
        }
        body_bytes += source.bytes->size();
        auto parsed = frontend::parse_sdf(
            { input.source_identity, *source.bytes });
        if (!parsed.ok()) {
            import_errors(diagnostics, parsed.diagnostics);
            return std::nullopt;
        }
        SdfAnnotationScopeRequest scope_request;
        scope_request.selection = input.root == "*"
            ? SdfScopeSelection::All : SdfScopeSelection::Single;
        // A nested instance scope (`top.dut`) annotates below its root.
        std::vector<std::string> scope_path;
        auto scope_root = input.root;
        if (scope_request.selection == SdfScopeSelection::Single
            && std::ranges::find(candidate->roots(), scope_root)
                == candidate->roots().end()) {
            std::size_t start { };
            for (auto dot = scope_root.find('.'); dot != std::string::npos;
                dot = scope_root.find('.', start)) {
                scope_path.push_back(scope_root.substr(start, dot - start));
                start = dot + 1U;
            }
            if (!scope_path.empty()) {
                scope_path.push_back(scope_root.substr(start));
                scope_root = scope_path.front();
                scope_path.erase(scope_path.begin());
            }
        }
        if (scope_request.selection == SdfScopeSelection::Single)
            scope_request.root_aliases.push_back(scope_root);
        scope_request.expected_project_identity = design_identity;
        scope_request.expected_design_identity = design_identity;
        const auto scope = bind_sdf_annotation_scope(parsed.file,
            *candidate, design_identity, design_identity, scope_request);
        if (!scope.ok()) {
            import_errors(diagnostics, scope.diagnostics);
            return std::nullopt;
        }
        const auto cells
            = resolve_sdf_cells(scope.scope, *candidate, scope_path);
        if (!cells.ok()) {
            import_errors(diagnostics, cells.diagnostics);
            return std::nullopt;
        }
        std::vector<SdfResolvedCell> selected_cells;
        selected_cells.reserve(cells.resolution->cells().size());
        std::size_t selected_count { };
        std::string selected_identity { "sdf-session-selection-v1" };
        append_identity_field(selected_identity,
            cells.resolution->semantic_identity());
        append_identity_field(selected_identity, input.root);
        append_identity_field(selected_identity, input.cell_pattern);
        for (const auto& cell : cells.resolution->cells()) {
            auto selected = cell;
            std::erase_if(selected.targets,
                [&](const SdfResolvedInstance& target) {
                    return !cell_matches(input, target.instance_path);
                });
            selected_count += selected.targets.size();
            for (const auto& target : selected.targets)
                append_identity_field(selected_identity,
                    target.instance_path);
            selected_cells.push_back(std::move(selected));
        }
        // VHDL cells are annotated through their VITAL timing generics when
        // the design is elaborated (IEEE 1076.4-2000 5.1.1); a session that
        // runs later cannot change those elaboration constants.
        const auto vital_count = remove_sdf_vital_targets(selected_cells);
        selected_count -= vital_count;
        if (vital_count != 0U && !vital_applied) {
            diagnostics.error("FSIM-SDF-VITAL-008",
                "SDF input names VHDL cells whose VITAL timing generics are "
                "annotated when the design is elaborated; give the file to "
                "`fsim elaborate --sdf`: " + input.source_identity);
            return std::nullopt;
        }
        if (selected_count == 0U && vital_count != 0U)
            continue;
        if (selected_count == 0U) {
            diagnostics.error("FSIM-SDF-SESSION-002",
                "SDF cell selector matches no resolved timing target: "
                    + input.cell_pattern);
            return std::nullopt;
        }
        auto selected_resolution = std::make_shared<const SdfCellResolution>(
            cells.resolution->scope(), std::move(selected_cells),
            support::Sha256::hex(support::Sha256::digest(selected_identity)));
        const auto endpoints = resolve_sdf_endpoints(
            std::move(selected_resolution), *candidate);
        if (!endpoints.ok()) {
            import_errors(diagnostics, endpoints.diagnostics);
            return std::nullopt;
        }
        const auto mapping = validate_sdf_mapping(
            endpoints.resolution, *candidate);
        if (!mapping.ok()) {
            import_errors(diagnostics, mapping.diagnostics);
            return std::nullopt;
        }
        SdfValuePolicy value_policy;
        value_policy.selection = request.selection;
        value_policy.design_time_unit_femtoseconds = *precision;
        value_policy.simulation_precision_femtoseconds = *precision;
        const auto plan = build_sdf_annotation_plan(
            mapping.summary, *candidate, value_policy);
        if (!plan.ok()) {
            import_errors(diagnostics, plan.diagnostics);
            return std::nullopt;
        }
        if (plan.plan->annotations().empty()) {
            diagnostics.error("FSIM-SDF-SESSION-002",
                "SDF input contains no selected timing annotations");
            return std::nullopt;
        }
        std::set<std::string> file_targets;
        for (const auto& annotation : plan.plan->annotations()) {
            auto key = std::to_string(static_cast<unsigned>(
                annotation.target_kind)) + ':' + annotation.target_identity;
            file_targets.insert(std::move(key));
        }
        for (const auto& target : file_targets) {
            const auto precedence = std::pair {
                input.file_precedence, input.cell_precedence };
            const auto [prior, inserted]
                = target_precedence.emplace(target, precedence);
            if (!inserted && prior->second == precedence) {
                diagnostics.error("FSIM-SDF-REANNOTATION-003",
                    "SDF inputs contain an equal-precedence timing target conflict: "
                        + target);
                return std::nullopt;
            }
            if (!inserted)
                prior->second = precedence;
        }
        SdfTimingPrecedencePolicy precedence_policy;
        precedence_policy.command_selection = request.selection;
        const auto precedence = apply_sdf_precedence(
            plan.plan, *candidate, precedence_policy);
        if (!precedence.ok()) {
            import_errors(diagnostics, precedence.diagnostics);
            return std::nullopt;
        }
        const auto scheduling = apply_sdf_scheduling(
            precedence.application, *candidate);
        if (!scheduling.ok()) {
            import_errors(diagnostics, scheduling.diagnostics);
            return std::nullopt;
        }
        const auto drive = apply_sdf_drive_timing(scheduling.application);
        if (!drive.ok()) {
            import_errors(diagnostics, drive.diagnostics);
            return std::nullopt;
        }
        std::map<std::string, std::set<std::string>> targets_by_root;
        for (const auto& annotation : plan.plan->annotations()) {
            if (annotation.target_kind == SdfTimingTargetKind::Interconnect
                || annotation.target_kind == SdfTimingTargetKind::Port) {
                // An interconnect or port delay that ends at an input port
                // of a VHDL instance is the tipd_ generic of that instance
                // (IEEE 1076.4-2000 5.2.7.1.3, 5.2.7.1.4): the VITAL map
                // annotated it when the design was elaborated, and the net
                // is not delayed a second time.
                const SdfResolvedEndpoint* load { };
                if (annotation.target_kind == SdfTimingTargetKind::Interconnect) {
                    const auto destination = std::ranges::find(
                        annotation.endpoints,
                        SdfEndpointRole::InterconnectDestination,
                        &SdfResolvedEndpoint::role);
                    if (destination != annotation.endpoints.end())
                        load = &*destination;
                } else if (annotation.endpoints.size() == 1U) {
                    load = &annotation.endpoints.front();
                }
                // The endpoint takes the language of the cell; a port of a
                // VHDL instance below a Verilog or SystemVerilog cell is
                // found by the instance that owns it.
                const auto vhdl_load = [&](const SdfResolvedEndpoint& endpoint) {
                    if (endpoint.language == SdfScopeRootLanguage::Vhdl)
                        return true;
                    const auto dot = endpoint.object_path.rfind('.');
                    if (dot == std::string::npos)
                        return false;
                    const std::string_view instance {
                        endpoint.object_path.data(), dot };
                    return std::ranges::any_of(design.specializations(),
                        [&](const auto& specialization) {
                            return specialization.language
                                    == frontend::Language::Vhdl2008
                                && specialization.instance == instance;
                        });
                };
                if (load != nullptr && vhdl_load(*load)
                    && annotation.construct_kind
                        != frontend::SdfConstructKind::NetDelay) {
                    if (!vital_applied) {
                        diagnostics.error("FSIM-SDF-VITAL-008",
                            "SDF input names VHDL ports whose VITAL timing "
                            "generics are annotated when the design is "
                            "elaborated; give the file to `fsim elaborate "
                            "--sdf`");
                        return std::nullopt;
                    }
                    continue;
                }
                auto record = net_delay_record(annotation, *precedence.application);
                if (!record) {
                    diagnostics.error("FSIM-SDF-SESSION-003",
                        "SDF interconnect or port delay has no complete effective timing");
                    return std::nullopt;
                }
                std::vector<elaboration::SdfNetDelayInfo> records;
                if (annotation.construct_kind
                    == frontend::SdfConstructKind::NetDelay) {
                    if (!declared_loaded) {
                        design_has_vhdl = std::ranges::any_of(
                            design.specializations(),
                            [](const auto& specialization) {
                                return specialization.language
                                    == frontend::Language::Vhdl2008;
                            });
                        declared = declared_ports(design, ports);
                        declared_loaded = true;
                    }
                    std::string problem;
                    std::string warning;
                    if (!net_delay_records(annotation, *record, declared,
                            records, problem, warning, design_has_vhdl)) {
                        diagnostics.error("FSIM-SDF-INTERCONNECT-002", problem);
                        return std::nullopt;
                    }
                    if (!warning.empty())
                        diagnostics.warning("FSIM-SDF-SESSION-004",
                            "SDF interconnect or port delay is recorded but "
                            "does not delay the net in simulation: " + warning);
                } else {
                    records.push_back(std::move(*record));
                }
                for (auto& each : records) {
                    const auto same = std::ranges::find(net_delays,
                        each.identity, &elaboration::SdfNetDelayInfo::identity);
                    // A repeated record applies at its later place (IEEE
                    // 1364-2005 16.3: in order of occurrence).
                    if (same != net_delays.end()) {
                        if (annotation.delay_mode
                            == SdfDelayApplicationMode::Increment)
                            add_to_previous(each, *same);
                        net_delays.erase(same);
                    }
                    net_delays.push_back(std::move(each));
                }
                continue;
            }
            if (annotation.target_kind != SdfTimingTargetKind::SpecifyPath
                && annotation.target_kind
                    != SdfTimingTargetKind::TimingCheck) {
                diagnostics.error("FSIM-SDF-SESSION-003",
                    "session SDF application currently requires a module path or timing-check target");
                return std::nullopt;
            }
            bool owned { };
            for (const auto& root : scope.scope->roots()) {
                if (annotation.target_instance_path == root.alias
                    || (annotation.target_instance_path.size()
                            > root.alias.size()
                        && annotation.target_instance_path.starts_with(
                            root.alias)
                        && annotation.target_instance_path[root.alias.size()]
                            == '.')) {
                    targets_by_root[root.alias].insert(
                        annotation.target_identity);
                    owned = true;
                    break;
                }
            }
            if (!owned) {
                diagnostics.error("FSIM-SDF-SESSION-003",
                    "selected SDF timing target has no owning root");
                return std::nullopt;
            }
        }
        for (auto& [root, identities] : targets_by_root) {
            SdfReannotationLayer layer;
            layer.file_precedence = input.file_precedence;
            layer.cell_precedence = input.cell_precedence;
            layer.file_identity = input.source_identity;
            layer.root = std::move(root);
            layer.cell_pattern = input.cell_pattern;
            layer.timing = drive.application;
            layer.selected_targets.assign(
                identities.begin(), identities.end());
            layers.push_back(std::move(layer));
        }
        candidate = elaboration::ElaboratedDesign::from_state(
            drive.application->scheduling()->design().state());
        if (!candidate) {
            diagnostics.error("FSIM-SDF-SESSION-001",
                "SDF application produced an invalid elaborated design");
            return std::nullopt;
        }
        auto publication = std::make_shared<SdfSessionPublication>();
        publication->file = std::move(parsed.file);
        publication->summary = mapping.summary;
        publication->source_text = std::move(*source.bytes);
        publication->selection = request.selection;
        pending.push_back(std::move(publication));
    }
    auto effective = apply_sdf_reannotation(
        baseline_drive.application, layers, generation);
    if (!effective.ok()) {
        import_errors(diagnostics, effective.diagnostics);
        return std::nullopt;
    }
    request.generation = generation;
    auto control = apply_sdf_control(request, effective.application);
    if (!control.ok()) {
        import_errors(diagnostics, control.diagnostics);
        return std::nullopt;
    }
    auto result_state = effective.application->design().state();
    // A design without an interconnect or port delay keeps none (IEEE
    // 1800-2023 37.45).
    result_state.sdf_net_delays = std::move(net_delays);
    // An interconnect or port delay delays the load's net (IEEE 1364-2005
    // 16, 1800-2023 38.10); a load that cannot be split keeps the record and
    // its VPI objects, and says so.
    const auto split = elaboration::split_sdf_net_delays(result_state);
    for (const auto& reason : split.skipped) {
        diagnostics.warning("FSIM-SDF-SESSION-004",
            "SDF interconnect or port delay is recorded but does not delay "
            "the net in simulation: " + reason);
    }
    // An INTERCONNECT whose source port is not on the load's net is
    // annotated anyway, as the delay from all sources (IEEE 1364-2005
    // 16.2.4: a warning is issued).
    for (const auto& note : split.from_all_sources) {
        diagnostics.warning("FSIM-SDF-SESSION-005",
            "SDF interconnect delay is annotated as the delay from all "
            "sources, like a PORT delay: " + note);
    }
    auto result_design = elaboration::ElaboratedDesign::from_state(
        std::move(result_state));
    if (!result_design) {
        diagnostics.error("FSIM-SDF-SESSION-001",
            "SDF reannotation produced an invalid elaborated design");
        return std::nullopt;
    }
    return SdfSessionPlan { std::move(*result_design),
        std::move(pending), std::move(control.application) };
}

bool apply_sdf_session_inputs(BuiltProject& built,
    const SdfControlRequest& request, diagnostic::Engine& diagnostics)
{
    if (request.inputs.empty())
        return true;
    auto planned = plan_sdf_session_inputs(built.design,
        built.time_resolution, built.cache_key, request, { }, diagnostics,
        { &built.design_ir, &built.systemverilog_hir },
        built.sdf_vital_applied);
    if (!planned)
        return false;
    built.design = std::move(planned->design);
    built.sdf_session_publications.insert(
        built.sdf_session_publications.end(),
        planned->publications.begin(), planned->publications.end());
    return true;
}

void apply_sdf_session_cache_identity(BuiltProject& built,
    const SdfControlApplication& control)
{
    apply_sdf_session_cache_identity(built.cache_key,
        built.specialization_cache_keys, control);
}

void apply_sdf_session_cache_identity(std::string& cache_key,
    std::vector<std::string>& specialization_cache_keys,
    const SdfControlApplication& control)
{
    const auto compose = [&](const std::string_view kind,
                             const std::string_view base) {
        compiler::CacheKeyBuilder key;
        key.add("schema", "sdf-session-native-v1");
        key.add("kind", kind);
        key.add("base", base);
        key.add("effective", control.semantic_identity());
        return key.finish();
    };
    cache_key = compose("design", cache_key);
    for (auto& key : specialization_cache_keys)
        key = compose("specialization", key);
}

bool append_sdf_session_payloads(const BuiltProject& built,
    artifact::DesignMetadata& metadata,
    std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics)
{
    for (const auto& publication : built.sdf_session_publications) {
        const SdfSchemaOptions options { "sdf-parse-options-v1",
            "sdf-normalization-options-v1", "fsim-compiler-compat-v1" };
        auto schema = encode_sdf_schema(publication->file,
            *publication->summary, publication->source_text, options);
        if (!schema.ok()) {
            import_errors(diagnostics, schema.diagnostics);
            return false;
        }
        auto decoded = decode_sdf_schema(schema.bytes,
            options.compiler_compatibility_identity,
            publication->summary->semantic_identity());
        if (!decoded.ok()) {
            import_errors(diagnostics, decoded.diagnostics);
            return false;
        }
        const auto selection = publication->selection == SdfDelaySelection::Minimum
            ? SdfDelaySelectionPolicy::Minimum
            : publication->selection == SdfDelaySelection::Maximum
                ? SdfDelaySelectionPolicy::Maximum
                : SdfDelaySelectionPolicy::Typical;
        auto identity = build_sdf_artifact_identity(*decoded.snapshot,
            *publication->summary, metadata.design_digest, selection);
        if (!identity.ok()) {
            import_errors(diagnostics, identity.diagnostics);
            return false;
        }
        auto portable = encode_sdf_portable_archive(*decoded.snapshot,
            *publication->summary, *identity.annotation, schema.bytes);
        if (!portable.ok()) {
            import_errors(diagnostics, portable.diagnostics);
            return false;
        }
        auto applied = apply_sdf_artifact_identity(metadata,
            *identity.annotation);
        if (!applied.ok()) {
            import_errors(diagnostics, applied.diagnostics);
            return false;
        }
        metadata = std::move(*applied.metadata);
        const auto index = make_sdf_design_payload(*identity.annotation,
            std::filesystem::path { "sdf" }
                / (identity.annotation->cache_key + ".bin"),
            portable.bytes);
        metadata.payloads.push_back(index);
        payloads.push_back({ index.artifact,
            std::string { reinterpret_cast<const char*>(portable.bytes.data()),
                portable.bytes.size() }, index.checksum });
        metadata.design_digest = artifact::compute_design_digest(metadata);
    }
    return true;
}

} // namespace fsim::app
