// SPDX-License-Identifier: Apache-2.0
#include "application_sdf_session.hpp"
#include "application_internal.hpp"

#include "fsim/app/sdf_annotation_scope.hpp"
#include "fsim/app/sdf_cell_resolution.hpp"
#include "fsim/app/sdf_endpoint_resolution.hpp"
#include "fsim/app/sdf_mapping_validation.hpp"
#include "fsim/app/sdf_target_plan.hpp"
#include "fsim/app/sdf_precedence.hpp"
#include "fsim/app/sdf_scheduling.hpp"
#include "fsim/app/sdf_drive_timing.hpp"
#include "fsim/app/sdf_schema.hpp"
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
    diagnostic::Engine& diagnostics)
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
        if (scope_request.selection == SdfScopeSelection::Single)
            scope_request.root_aliases.push_back(input.root);
        scope_request.expected_project_identity = design_identity;
        scope_request.expected_design_identity = design_identity;
        const auto scope = bind_sdf_annotation_scope(parsed.file,
            *candidate, design_identity, design_identity, scope_request);
        if (!scope.ok()) {
            import_errors(diagnostics, scope.diagnostics);
            return std::nullopt;
        }
        const auto cells = resolve_sdf_cells(scope.scope, *candidate);
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
    auto result_design = elaboration::ElaboratedDesign::from_state(
        effective.application->design().state());
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
        built.time_resolution, built.cache_key, request, { }, diagnostics);
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
