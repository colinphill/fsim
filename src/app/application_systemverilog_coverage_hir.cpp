// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

SystemVerilogCoverageHirInventory systemverilog_coverage_hir_inventory(
    const semantic::sv::Hir& hir)
{
    SystemVerilogCoverageHirInventory inventory;
    for (const auto& unit : hir.units()) {
        for (const auto& declaration : unit.coverage) {
            inventory.declarations.push_back(&declaration);
        }
    }
    for (const auto& owner : hir.classes()) {
        for (const auto& declaration : owner.covergroups) {
            inventory.declarations.push_back(&declaration);
        }
    }
    std::ranges::sort(
        inventory.declarations, { },
        [](const semantic::sv::CovergroupDeclaration* declaration) {
            return declaration->canonical_identity;
        });
    inventory.instances = hir.covergroup_instances();
    return inventory;
}

const semantic::sv::CovergroupDeclaration*
find_systemverilog_covergroup_declaration(
    const SystemVerilogCoverageHirInventory& inventory,
    const std::string_view canonical_identity) noexcept
{
    const auto found = std::ranges::lower_bound(
        inventory.declarations, canonical_identity, { },
        [](const semantic::sv::CovergroupDeclaration* declaration) {
            return std::string_view { declaration->canonical_identity };
        });
    return found == inventory.declarations.end()
            || (*found)->canonical_identity != canonical_identity
        ? nullptr
        : *found;
}

const semantic::sv::CovergroupDeclaration*
find_systemverilog_covergroup_declaration(
    const semantic::sv::Hir& hir,
    const std::string_view canonical_identity) noexcept
{
    for (const auto& unit : hir.units()) {
        const auto found = std::ranges::find(
            unit.coverage, canonical_identity,
            &semantic::sv::CovergroupDeclaration::canonical_identity);
        if (found != unit.coverage.end()) {
            return &*found;
        }
    }
    for (const auto& owner : hir.classes()) {
        const auto found = std::ranges::find(
            owner.covergroups, canonical_identity,
            &semantic::sv::CovergroupDeclaration::canonical_identity);
        if (found != owner.covergroups.end()) {
            return &*found;
        }
    }
    return nullptr;
}

const semantic::sv::CovergroupInstance*
find_systemverilog_covergroup_instance(
    const SystemVerilogCoverageHirInventory& inventory,
    const std::string_view runtime_identity) noexcept
{
    const auto found = std::ranges::find(
        inventory.instances, runtime_identity,
        &semantic::sv::CovergroupInstance::runtime_identity);
    return found == inventory.instances.end() ? nullptr : &*found;
}

const semantic::sv::CovergroupInstance*
find_systemverilog_covergroup_instance(
    const semantic::sv::Hir& hir,
    const std::string_view runtime_identity) noexcept
{
    const auto& instances = hir.covergroup_instances();
    const auto found = std::ranges::find(
        instances, runtime_identity,
        &semantic::sv::CovergroupInstance::runtime_identity);
    return found == instances.end() ? nullptr : &*found;
}

semantic::sv::CovergroupInstance* find_systemverilog_covergroup_instance(
    semantic::sv::Hir& hir,
    const std::string_view runtime_identity) noexcept
{
    auto& instances = hir.mutable_covergroup_instances();
    const auto found = std::ranges::find(
        instances, runtime_identity,
        &semantic::sv::CovergroupInstance::runtime_identity);
    return found == instances.end() ? nullptr : &*found;
}

void synchronize_systemverilog_covergroup_runtime_state(
    semantic::sv::CovergroupInstance& destination,
    const frontend::SystemVerilogCovergroupInstance& source,
    semantic::Model& semantics)
{
    destination.bin_hits.clear();
    destination.bin_hits.reserve(source.bin_hits.size());
    for (const auto& hit : source.bin_hits) {
        destination.bin_hits.push_back({
            static_cast<std::uint64_t>(hit.coverage_declaration_index),
            static_cast<std::uint64_t>(hit.bin_declaration_index),
            hit.identity,
            hit.automatic_value,
            hit.automatic_value_bits,
            hit.automatic_unknown_bits,
            hit.automatic_width,
            hit.automatic_signed,
            hit.hit_count,
            hit.at_least,
            hit.covered,
        });
    }
    destination.transition_progress.clear();
    destination.transition_progress.reserve(
        source.transition_progress.size());
    for (const auto& progress : source.transition_progress) {
        destination.transition_progress.push_back({
            static_cast<std::uint64_t>(
                progress.coverage_declaration_index),
            static_cast<std::uint64_t>(progress.bin_declaration_index),
            static_cast<std::uint64_t>(progress.sequence_index),
            static_cast<std::uint64_t>(progress.step_index),
            progress.repetition_count,
            progress.samples_since_step,
        });
    }
    destination.previous_samples.clear();
    destination.previous_samples.reserve(source.previous_samples.size());
    for (const auto& sample : source.previous_samples) {
        destination.previous_samples.push_back({
            static_cast<std::uint64_t>(
                sample.coverage_declaration_index),
            sample.value,
            sample.unknown_mask,
            sample.width,
            sample.value_bits,
            sample.unknown_bits,
            sample.signed_value,
        });
    }
    destination.cross_bin_state.clear();
    destination.cross_bin_state.reserve(source.cross_bin_state.size());
    for (const auto& state : source.cross_bin_state) {
        destination.cross_bin_state.push_back({
            static_cast<std::uint64_t>(
                state.coverage_declaration_index),
            state.bin_declaration_index
                ? std::optional<std::uint64_t> {
                      static_cast<std::uint64_t>(
                          *state.bin_declaration_index) }
                : std::nullopt,
            state.identity,
            state.operand_bin_identities,
            state.hit_count,
            state.exclusion_count,
            state.weight,
            state.goal,
            state.at_least,
            state.covered,
            state.excluded,
        });
    }
    destination.illegal_bin_reports.clear();
    destination.illegal_bin_reports.reserve(
        source.illegal_bin_reports.size());
    for (const auto& report : source.illegal_bin_reports) {
        destination.illegal_bin_reports.push_back({
            report.bin_identity,
            report.sampled_value,
            report.sampled_unknown_mask,
            report.sampled_width,
            report.sampled_value_bits,
            report.sampled_unknown_bits,
            report.sampled_signed,
            static_cast<semantic::sv::CoverageScalarKind>(
                report.sampled_scalar_kind),
            report.sampled_scalar_bits,
            intern_semantic_span(semantics, report.span),
        });
    }
    destination.cross_inventory_initialized
        = source.cross_inventory_initialized;
}

namespace {

class CoverageProjection {
public:
    explicit CoverageProjection(const semantic::Model& semantics)
        : semantics_ { semantics }
    {
    }

    CoverageProjection(
        const semantic::sv::Hir& hir,
        const semantic::Model& semantics)
        : hir_ { &hir }
        , semantics_ { semantics }
    {
    }

    [[nodiscard]] frontend::SystemVerilogCoverageState project(
        const runtime::SystemVerilogCoverageState& runtime_state) const
    {
        frontend::SystemVerilogCoverageState state;
        const auto inventory = systemverilog_coverage_hir_inventory(*hir_);
        state.declarations.reserve(inventory.declarations.size());
        for (const auto* declaration : inventory.declarations) {
            state.declarations.push_back(
                project_declaration(*declaration));
        }
        state.instances.reserve(runtime_state.instances.size());
        for (const auto& instance : runtime_state.instances) {
            state.instances.push_back(project_instance(instance));
        }
        std::ranges::sort(
            state.instances, { },
            &frontend::SystemVerilogCovergroupInstance::runtime_identity);
        state.callback_events = runtime_state.callback_events;
        state.trace_events = runtime_state.trace_events;
        state.aliases = runtime_state.aliases;
        frontend::refresh_systemverilog_coverage_reports(state);
        return state;
    }

    [[nodiscard]] frontend::SourceSpan span(
        const semantic::SourceSpanId id) const
    {
        frontend::SourceSpan output;
        if (!id.valid()
            || id.value() >= semantics_.source_spans().size()) {
            return output;
        }
        const auto& input = semantics_.source_spans()[id.value()];
        if (!input.file.valid()
            || input.file.value()
                >= semantics_.source_files().size()) {
            return output;
        }
        const auto& file = semantics_.source_files()[input.file.value()];
        output.source_name = input.logical_name.empty()
            ? file.physical_name
            : input.logical_name;
        output.physical_source_name = file.physical_name;
        output.begin = {
            static_cast<std::size_t>(input.begin.offset),
            static_cast<std::size_t>(input.begin.line),
            static_cast<std::size_t>(input.begin.column)
        };
        output.end = {
            static_cast<std::size_t>(input.end.offset),
            static_cast<std::size_t>(input.end.line),
            static_cast<std::size_t>(input.end.column)
        };
        std::vector<std::string> expansion_stack;
        auto expansion = input.expansion;
        while (expansion && expansion->valid()
            && expansion->value()
                < semantics_.expansions().size()) {
            const auto& record = semantics_.expansions()[expansion->value()];
            expansion_stack.push_back(record.description);
            expansion = record.parent;
        }
        for (auto iterator = expansion_stack.rbegin();
             iterator != expansion_stack.rend(); ++iterator) {
            output.expansion_stack.push_back(*iterator);
        }
        return output;
    }

    [[nodiscard]] frontend::Token token(
        const semantic::sv::SourceToken& input) const
    {
        auto kind = frontend::TokenKind::EndOfFile;
        if (input.kind
            <= static_cast<std::uint16_t>(
                frontend::TokenKind::EndOfFile)) {
            kind = static_cast<frontend::TokenKind>(input.kind);
        }
        auto source_span = span(input.source);
        auto expansion_stack = source_span.expansion_stack;
        auto output = frontend::Token {
            kind,
            input.text,
            std::move(source_span),
            std::move(expansion_stack)
        };
        output.generated_text
            = static_cast<frontend::GeneratedTextKind>(input.generated_text);
        return output;
    }

    [[nodiscard]] std::vector<frontend::Token> tokens(
        const std::span<const semantic::sv::SourceToken> input) const
    {
        std::vector<frontend::Token> output;
        output.reserve(input.size());
        for (const auto& item : input) {
            output.push_back(token(item));
        }
        return output;
    }

    [[nodiscard]] frontend::Token name_token(
        const std::string& name,
        const semantic::SourceSpanId source) const
    {
        return {
            frontend::TokenKind::Identifier,
            name,
            span(source),
            { }
        };
    }

    [[nodiscard]] frontend::StandardRevision revision(
        const std::string_view standard) const noexcept
    {
        if (standard == "2005") {
            return frontend::StandardRevision::SystemVerilog2005;
        }
        if (standard == "2009") {
            return frontend::StandardRevision::SystemVerilog2009;
        }
        if (standard == "2012") {
            return frontend::StandardRevision::SystemVerilog2012;
        }
        if (standard == "2023") {
            return frontend::StandardRevision::SystemVerilog2023;
        }
        return frontend::StandardRevision::SystemVerilog2017;
    }

    [[nodiscard]] frontend::SystemVerilogCovergroupFormal project_formal(
        const semantic::sv::CovergroupFormal& input) const
    {
        frontend::SystemVerilogCovergroupFormal output;
        output.direction
            = static_cast<frontend::PortDirection>(input.direction);
        output.const_ref = input.const_reference;
        output.type_tokens = tokens(input.type_tokens);
        output.name = input.name;
        output.name_token = name_token(input.name, input.name_source);
        output.name_span = span(input.name_source);
        output.default_tokens = tokens(input.default_tokens);
        output.span = span(input.source);
        return output;
    }

    [[nodiscard]] frontend::SystemVerilogCovergroupOptionAssignment
    project_option(
        const semantic::sv::CovergroupOptionAssignment& input) const
    {
        frontend::SystemVerilogCovergroupOptionAssignment output;
        output.scope = static_cast<
            frontend::SystemVerilogCovergroupOptionScope>(input.scope);
        output.name = input.name;
        output.name_token = name_token(input.name, input.name_source);
        output.name_span = span(input.name_source);
        output.value_tokens = tokens(input.value_tokens);
        output.evaluated_value = input.evaluated_value;
        output.evaluated_real_bits = input.evaluated_real_bits;
        output.inherited = input.inherited;
        output.span = span(input.source);
        return output;
    }

    [[nodiscard]] frontend::SystemVerilogCoverageBinValue project_value(
        const semantic::sv::CoverageBinValue& input) const
    {
        frontend::SystemVerilogCoverageBinValue output;
        output.tokens = tokens(input.tokens);
        output.exact_value = input.exact_value;
        output.range_left = input.range_left;
        output.range_right = input.range_right;
        output.wildcard_value = input.wildcard_value;
        output.wildcard_mask = input.wildcard_mask;
        output.width = input.width;
        output.wildcard = input.wildcard;
        output.span = span(input.source);
        output.exact_bits = input.exact_bits;
        output.exact_unknown_bits = input.exact_unknown_bits;
        output.range_left_bits = input.range_left_bits;
        output.range_right_bits = input.range_right_bits;
        output.wildcard_value_bits = input.wildcard_value_bits;
        output.wildcard_mask_bits = input.wildcard_mask_bits;
        output.exact_signed = input.exact_signed;
        output.range_left_signed = input.range_left_signed;
        output.range_right_signed = input.range_right_signed;
        output.exact_real_bits = input.exact_real_bits;
        output.range_left_real_bits = input.range_left_real_bits;
        output.range_right_real_bits = input.range_right_real_bits;
        output.range_left_inclusive = input.range_left_inclusive;
        output.range_right_inclusive = input.range_right_inclusive;
        return output;
    }

    [[nodiscard]] frontend::SystemVerilogCoverageTransitionSequence
    project_transition(
        const semantic::sv::CoverageTransitionSequence& input) const
    {
        frontend::SystemVerilogCoverageTransitionSequence output;
        output.span = span(input.source);
        output.steps.reserve(input.steps.size());
        for (const auto& input_step : input.steps) {
            frontend::SystemVerilogCoverageTransitionStep step;
            step.span = span(input_step.source);
            step.repetition.kind = static_cast<
                frontend::SystemVerilogCoverageTransitionRepetitionKind>(
                    input_step.repetition.kind);
            step.repetition.minimum = input_step.repetition.minimum;
            step.repetition.maximum = input_step.repetition.maximum;
            step.repetition.span = span(input_step.repetition.source);
            step.values.reserve(input_step.values.size());
            for (const auto& value : input_step.values) {
                step.values.push_back(project_value(value));
            }
            output.steps.push_back(std::move(step));
        }
        output.delays.reserve(input.delays.size());
        for (const auto& input_delay : input.delays) {
            output.delays.push_back({
                input_delay.minimum,
                input_delay.maximum,
                span(input_delay.source)
            });
        }
        return output;
    }

    [[nodiscard]] frontend::SystemVerilogCoverageBin project_bin(
        const semantic::sv::CoverageBin& input) const
    {
        frontend::SystemVerilogCoverageBin output;
        output.kind = static_cast<frontend::SystemVerilogCoverageBinKind>(
            input.kind);
        output.selection
            = static_cast<frontend::SystemVerilogCoverageBinSelection>(
                input.selection);
        output.name = input.name;
        output.name_token = name_token(input.name, input.source);
        output.declaration_index
            = static_cast<std::size_t>(input.declaration_index);
        output.source_name = input.source_name;
        if (input.array_index) {
            output.array_index
                = static_cast<std::size_t>(*input.array_index);
        }
        if (input.declared_array_size) {
            output.declared_array_size
                = static_cast<std::size_t>(*input.declared_array_size);
        }
        output.wildcard = input.wildcard;
        output.values.reserve(input.values.size());
        for (const auto& value : input.values) {
            output.values.push_back(project_value(value));
        }
        output.transitions.reserve(input.transitions.size());
        for (const auto& transition : input.transitions) {
            output.transitions.push_back(project_transition(transition));
        }
        output.cross_selection_tokens = tokens(
            input.cross_selection_tokens);
        output.with_tokens = tokens(input.with_tokens);
        output.with_span = span(input.with_source);
        output.iff_tokens = tokens(input.iff_tokens);
        output.iff_span = span(input.iff_source);
        output.weight = input.weight;
        output.goal = input.goal;
        output.at_least = input.at_least;
        output.span = span(input.source);
        return output;
    }

    [[nodiscard]] frontend::SystemVerilogCoverageDeclaration project_item(
        const semantic::sv::CoverageItem& input) const
    {
        frontend::SystemVerilogCoverageDeclaration output;
        output.kind = static_cast<
            frontend::SystemVerilogCoverageDeclarationKind>(input.kind);
        output.name = input.name;
        output.name_token = name_token(input.name, input.name_source);
        output.name_span = span(input.name_source);
        output.explicit_name = input.explicit_name;
        output.declaration_index
            = static_cast<std::size_t>(input.declaration_index);
        output.origin_covergroup_identity
            = input.origin_covergroup_identity;
        output.inherited = input.inherited;
        output.sampled_scalar_kind
            = static_cast<frontend::SystemVerilogScalarKind>(
                input.sampled_scalar_kind);
        output.effective_real_interval_bits
            = input.effective_real_interval_bits;
        output.expression_tokens = tokens(input.expression_tokens);
        output.expression_span = span(input.expression_source);
        output.cross_operands.reserve(input.cross_operands.size());
        for (const auto& operand : input.cross_operands) {
            frontend::SystemVerilogCoverageCrossOperand projected;
            projected.name = operand.target.spelling;
            projected.tokens = tokens(operand.tokens);
            projected.span = span(operand.source);
            if (operand.resolved_declaration_index) {
                projected.resolved_declaration_index
                    = static_cast<std::size_t>(
                        *operand.resolved_declaration_index);
            }
            projected.implicit_coverpoint = operand.implicit_coverpoint;
            output.cross_operands.push_back(std::move(projected));
        }
        output.iff_tokens = tokens(input.iff_tokens);
        output.iff_span = span(input.iff_source);
        output.body_tokens = tokens(input.body_tokens);
        output.body_span = span(input.body_source);
        output.option_assignments.reserve(input.option_assignments.size());
        for (const auto& option : input.option_assignments) {
            output.option_assignments.push_back(project_option(option));
        }
        output.effective_weight = input.effective_weight;
        output.effective_goal = input.effective_goal;
        output.effective_at_least = input.effective_at_least;
        output.bins.reserve(input.bins.size());
        for (const auto& bin : input.bins) {
            output.bins.push_back(project_bin(bin));
        }
        output.references.reserve(input.references.size());
        for (const auto& reference : input.references) {
            output.references.push_back({
                static_cast<
                    frontend::SystemVerilogCoverageReferenceKind>(
                        reference.kind),
                reference.target.spelling,
                tokens(reference.tokens),
                span(reference.source)
            });
        }
        output.span = span(input.source);
        return output;
    }

    [[nodiscard]] frontend::SystemVerilogCovergroupDeclaration
    project_declaration(
        const semantic::sv::CovergroupDeclaration& input) const
    {
        frontend::SystemVerilogCovergroupDeclaration output;
        output.owner_kind = static_cast<
            frontend::SystemVerilogCovergroupOwnerKind>(input.owner_kind);
        output.standard_revision = revision(input.standard);
        output.name = input.name;
        output.extends_parent = input.extends_parent;
        output.resolved_base_identity = input.resolved_base_identity;
        output.owner_identity = input.owner_identity;
        output.canonical_identity = input.canonical_identity;
        output.specialization_identity = input.specialization_identity;
        output.runtime_identity_prefix = input.runtime_identity_prefix;
        output.name_token = name_token(input.name, input.name_source);
        output.name_span = span(input.name_source);
        output.header_tokens = tokens(input.header_tokens);
        output.header_span = span(input.header_source);
        output.formals.reserve(input.formals.size());
        for (const auto& formal : input.formals) {
            output.formals.push_back(project_formal(formal));
        }
        if (input.sampling) {
            frontend::SystemVerilogCovergroupSampling sampling;
            sampling.kind = static_cast<
                frontend::SystemVerilogCovergroupSamplingKind>(
                    input.sampling->kind);
            sampling.tokens = tokens(input.sampling->tokens);
            sampling.formals.reserve(input.sampling->formals.size());
            for (const auto& formal : input.sampling->formals) {
                sampling.formals.push_back(project_formal(formal));
            }
            sampling.span = span(input.sampling->source);
            output.sampling = std::move(sampling);
        }
        output.body_tokens = tokens(input.body_tokens);
        output.body_span = span(input.body_source);
        output.option_assignments.reserve(input.option_assignments.size());
        for (const auto& option : input.option_assignments) {
            output.option_assignments.push_back(project_option(option));
        }
        output.effective_instance_weight
            = input.effective_instance_weight;
        output.effective_instance_goal = input.effective_instance_goal;
        output.effective_type_weight = input.effective_type_weight;
        output.effective_type_goal = input.effective_type_goal;
        output.effective_per_instance = input.effective_per_instance;
        output.effective_merge_instances = input.effective_merge_instances;
        output.effective_cross_retain_auto_bins
            = input.effective_cross_retain_auto_bins;
        output.effective_real_interval_bits
            = input.effective_real_interval_bits;
        output.coverage_declarations.reserve(input.items.size());
        for (const auto& item : input.items) {
            output.coverage_declarations.push_back(project_item(item));
        }
        output.end_name = input.end_name;
        if (input.end_name) {
            output.end_name_token = name_token(
                *input.end_name, input.end_name_source);
        }
        output.end_name_span = span(input.end_name_source);
        output.span = span(input.source);
        return output;
    }

    [[nodiscard]] frontend::SystemVerilogCovergroupInstance project_instance(
        const semantic::sv::CovergroupInstance& input) const
    {
        frontend::SystemVerilogCovergroupInstance output;
        output.name = input.name;
        output.owner_identity = input.owner_identity;
        output.declaration_identity = input.declaration_identity;
        output.specialization_identity = input.specialization_identity;
        output.runtime_identity = input.runtime_identity;
        output.initial_option_state.reserve(input.initial_option_state.size());
        for (const auto& option : input.initial_option_state) {
            output.initial_option_state.push_back(project_option(option));
        }
        output.bin_hits.reserve(input.bin_hits.size());
        for (const auto& hit : input.bin_hits) {
            output.bin_hits.push_back({
                static_cast<std::size_t>(
                    hit.coverage_declaration_index),
                static_cast<std::size_t>(hit.bin_declaration_index),
                hit.identity,
                hit.automatic_value,
                hit.automatic_value_bits,
                hit.automatic_unknown_bits,
                hit.automatic_width,
                hit.automatic_signed,
                hit.hit_count,
                hit.at_least,
                hit.covered
            });
        }
        output.transition_progress.reserve(input.transition_progress.size());
        for (const auto& progress : input.transition_progress) {
            output.transition_progress.push_back({
                static_cast<std::size_t>(
                    progress.coverage_declaration_index),
                static_cast<std::size_t>(progress.bin_declaration_index),
                static_cast<std::size_t>(progress.sequence_index),
                static_cast<std::size_t>(progress.step_index),
                progress.repetition_count,
                progress.samples_since_step
            });
        }
        output.previous_samples.reserve(input.previous_samples.size());
        for (const auto& previous : input.previous_samples) {
            output.previous_samples.push_back({
                static_cast<std::size_t>(
                    previous.coverage_declaration_index),
                previous.value,
                previous.unknown_mask,
                previous.width,
                previous.value_bits,
                previous.unknown_bits,
                previous.signed_value
            });
        }
        output.cross_bin_state.reserve(input.cross_bin_state.size());
        for (const auto& cross : input.cross_bin_state) {
            frontend::SystemVerilogCoverageCrossBinState projected;
            projected.coverage_declaration_index
                = static_cast<std::size_t>(
                    cross.coverage_declaration_index);
            if (cross.bin_declaration_index) {
                projected.bin_declaration_index
                    = static_cast<std::size_t>(
                        *cross.bin_declaration_index);
            }
            projected.identity = cross.identity;
            projected.operand_bin_identities
                = cross.operand_bin_identities;
            projected.hit_count = cross.hit_count;
            projected.exclusion_count = cross.exclusion_count;
            projected.weight = cross.weight;
            projected.goal = cross.goal;
            projected.at_least = cross.at_least;
            projected.covered = cross.covered;
            projected.excluded = cross.excluded;
            output.cross_bin_state.push_back(std::move(projected));
        }
        output.illegal_bin_reports.reserve(input.illegal_bin_reports.size());
        for (const auto& report : input.illegal_bin_reports) {
            output.illegal_bin_reports.push_back({
                report.bin_identity,
                report.sampled_value,
                report.sampled_unknown_mask,
                report.sampled_width,
                report.sampled_value_bits,
                report.sampled_unknown_bits,
                report.sampled_signed,
                static_cast<frontend::SystemVerilogScalarKind>(
                    report.sampled_scalar_kind),
                report.sampled_scalar_bits,
                span(report.source)
            });
        }
        output.class_member_template = input.class_member_template;
        output.cross_inventory_initialized
            = input.cross_inventory_initialized;
        output.span = span(input.source);
        return output;
    }

    const semantic::sv::Hir* hir_ { };
    const semantic::Model& semantics_;
};

} // namespace

runtime::SystemVerilogCoverageState make_systemverilog_coverage_state(
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics)
{
    runtime::SystemVerilogCoverageState state;
    const auto inventory = systemverilog_coverage_hir_inventory(hir);
    state.declarations.reserve(inventory.declarations.size());
    for (const auto* declaration : inventory.declarations) {
        state.declarations.push_back({ declaration->canonical_identity });
    }
    state.instances.assign(
        inventory.instances.begin(), inventory.instances.end());
    std::ranges::sort(
        state.instances, { },
        &semantic::sv::CovergroupInstance::runtime_identity);
    refresh_systemverilog_coverage_reports(state, hir, semantics);
    return state;
}

frontend::SystemVerilogCovergroupDeclaration
project_systemverilog_covergroup_declaration(
    const semantic::sv::CovergroupDeclaration& declaration,
    const semantic::Model& semantics)
{
    return CoverageProjection { semantics }
        .project_declaration(declaration);
}

frontend::SystemVerilogCovergroupInstance
project_systemverilog_covergroup_instance(
    const semantic::sv::CovergroupInstance& instance,
    const semantic::Model& semantics)
{
    return CoverageProjection { semantics }
        .project_instance(instance);
}

frontend::SystemVerilogCoverageState project_systemverilog_coverage_state(
    const runtime::SystemVerilogCoverageState& state,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics)
{
    return CoverageProjection { hir, semantics }.project(state);
}

void refresh_systemverilog_coverage_reports(
    runtime::SystemVerilogCoverageState& state,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics)
{
    state.reports = project_systemverilog_coverage_state(
        state, hir, semantics).reports;
}

bool restore_systemverilog_coverage_state(
    runtime::SystemVerilogCoverageState& destination,
    const runtime::SystemVerilogCoverageState& source,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
    std::string& error)
{
    auto restored = make_systemverilog_coverage_state(hir, semantics);
    std::set<std::string, std::less<>> source_declarations;
    for (const auto& declaration : source.declarations) {
        if (declaration.canonical_identity.empty()
            || !source_declarations.insert(
                declaration.canonical_identity).second) {
            error = "coverage state has duplicate declaration identities";
            return false;
        }
    }
    std::set<std::string, std::less<>> expected_declarations;
    for (const auto& declaration : restored.declarations) {
        expected_declarations.insert(declaration.canonical_identity);
    }
    if (source_declarations != expected_declarations) {
        error = "coverage state describes a different declaration model";
        return false;
    }
    if (source.instances.size() != restored.instances.size()) {
        error = "coverage state describes a different instance model";
        return false;
    }
    std::set<std::string, std::less<>> source_instances;
    for (const auto& source_instance : source.instances) {
        if (source_instance.runtime_identity.empty()
            || !source_instances.insert(
                source_instance.runtime_identity).second) {
            error = "coverage state has duplicate instance identities";
            return false;
        }
        auto destination_instance = std::ranges::find(
            restored.instances, source_instance.runtime_identity,
            &semantic::sv::CovergroupInstance::runtime_identity);
        if (destination_instance == restored.instances.end()
            || destination_instance->declaration_identity
                != source_instance.declaration_identity
            || destination_instance->name != source_instance.name
            || destination_instance->owner_identity
                != source_instance.owner_identity
            || destination_instance->specialization_identity
                != source_instance.specialization_identity
            || destination_instance->class_member_template
                != source_instance.class_member_template) {
            error = "coverage state describes a different instance model";
            return false;
        }
        if (std::ranges::any_of(
                source_instance.illegal_bin_reports,
                [&](const auto& report) {
                    return !report.source.valid()
                        || report.source.value()
                            >= semantics.source_spans().size();
                })) {
            error = "coverage state contains an invalid source identity";
            return false;
        }
        destination_instance->bin_hits = source_instance.bin_hits;
        destination_instance->transition_progress
            = source_instance.transition_progress;
        destination_instance->previous_samples
            = source_instance.previous_samples;
        destination_instance->cross_bin_state
            = source_instance.cross_bin_state;
        destination_instance->illegal_bin_reports
            = source_instance.illegal_bin_reports;
        destination_instance->cross_inventory_initialized
            = source_instance.cross_inventory_initialized;
    }
    restored.callback_events = source.callback_events;
    restored.trace_events = source.trace_events;
    restored.aliases = source.aliases;
    refresh_systemverilog_coverage_reports(restored, hir, semantics);
    destination = std::move(restored);
    error.clear();
    return true;
}

bool merge_systemverilog_coverage_state(
    runtime::SystemVerilogCoverageState& destination,
    const runtime::SystemVerilogCoverageState& source,
    const semantic::sv::Hir& hir,
    semantic::Model& semantics,
    std::string& error)
{
    const auto declaration_identities = [](const auto& state) {
        std::set<std::string, std::less<>> result;
        for (const auto& declaration : state.declarations) {
            if (declaration.canonical_identity.empty()
                || !result.insert(declaration.canonical_identity).second) {
                return std::optional<decltype(result)> { };
            }
        }
        return std::optional { std::move(result) };
    };
    const auto destination_declarations
        = declaration_identities(destination);
    const auto source_declarations = declaration_identities(source);
    if (!destination_declarations || !source_declarations) {
        error = "coverage state has duplicate declaration identities";
        return false;
    }
    if (*destination_declarations != *source_declarations) {
        error = "coverage database describes a different declaration model";
        return false;
    }
    auto destination_adapter = project_systemverilog_coverage_state(
        destination, hir, semantics);
    const auto source_adapter = project_systemverilog_coverage_state(
        source, hir, semantics);
    if (!frontend::merge_systemverilog_coverage_state(
            destination_adapter, source_adapter, error)) {
        return false;
    }
    for (const auto& instance : destination_adapter.instances) {
        auto destination_instance = std::ranges::find(
            destination.instances, instance.runtime_identity,
            &semantic::sv::CovergroupInstance::runtime_identity);
        if (destination_instance == destination.instances.end()) {
            error = "coverage database merge produced an unknown instance";
            return false;
        }
        synchronize_systemverilog_covergroup_runtime_state(
            *destination_instance, instance, semantics);
    }
    destination.reports = std::move(destination_adapter.reports);
    error.clear();
    return true;
}

} // namespace fsim::app::application_detail
