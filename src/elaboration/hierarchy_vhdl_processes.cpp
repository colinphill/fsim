// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace fsim::elaboration {

void HierarchyBuilder::append_generated_vhdl_processes(Lowerer& source,
    const semantic::vhdl::Unit& owner, SpecializationInfo& specialization)
{
    auto generated_processes = source.take_generated_processes();
    record_lowering_census(
        vhdl_lowerer_generated_processes_, generated_processes.size());
    for (auto& generated : generated_processes) {
        generated.language_standard = owner.standard;
        generated.compatibility_profile = owner.compatibility_profile;
        canonicalize_process_operations(generated);
        specialization.processes.push_back(generated.id);
        design_.append_process_record(std::move(generated));
    }
}

bool HierarchyBuilder::report_vhdl_unspecified_inference(
    const std::optional<semantic::SourceSpanId> failure)
{
    if (!failure) {
        return false;
    }
    report(
        "FSIM-ELAB-VHUNSPEC-001",
        "VHDL-2019 unspecified interface types do not infer one "
        "consistent callable profile",
        compiled_source_span(*compiled_, *failure));
    return true;
}

bool HierarchyBuilder::lower_compiled_vhdl_generated_processes(
    std::vector<VhdlHirMaterialization>& materializations,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture,
    StringMap& string_objects,
    ReadOnlyStringSet& read_only_strings,
    ContainerMap& container_objects,
    ReadOnlyContainerSet& read_only_containers,
    SpecializationInfo& specialization,
    std::size_t& concurrent_order,
    std::vector<std::size_t>* const deferred,
    const std::vector<std::size_t>* const only)
{
    // With `deferred`, a generated region that contains an external name is
    // collected there and lowered, through `only`, once the architecture's
    // instances exist (IEEE 1076-2008 8.7, 14.5).
    const auto external_spans = deferred != nullptr
        ? compiled_vhdl_external_name_spans(architecture)
        : std::vector<const semantic::SourceSpan*> { };
    const auto names_external = [&](const semantic::SourceSpanId source) {
        return compiled_source_encloses(source, external_spans);
    };

    // Signals a generated occurrence may name: the architecture's and the
    // generate region's.
    const auto generated_signal_declarations
        = [&](const VhdlHirMaterialization& materialization) {
              std::vector<semantic::DeclarationId> declarations;
              declarations.reserve(architecture.declarations.size()
                  + materialization.signal_declarations.size());
              declarations.insert(declarations.end(),
                  architecture.declarations.begin(),
                  architecture.declarations.end());
              declarations.insert(declarations.end(),
                  materialization.signal_declarations.begin(),
                  materialization.signal_declarations.end());
              return declarations;
          };
    for (std::size_t materialization_index { };
         materialization_index < materializations.size();
         ++materialization_index) {
        auto& materialization = materializations[materialization_index];
        if (only != nullptr
            && std::ranges::find(*only, materialization_index)
                == only->end()) {
            continue;
        }
        if (!external_spans.empty()
            && names_external(materialization.region->source)) {
            deferred->push_back(materialization_index);
            continue;
        }
        Lowerer generated_lowerer {
            design_,
            materialization.signals,
            materialization.read_only_signals,
            string_objects,
            read_only_strings,
            container_objects,
            read_only_containers,
            diagnostics_,
        };
        generated_lowerer.set_specialized_hir_unit(
            &materialization.specialization);
        if (materialization.region->kind
                == semantic::vhdl::GenerateKind::block
            && materialization.region->condition
            && !generated_lowerer.diagnose_hir_vhdl_block_guard(
                *materialization.region->condition)) {
            return false;
        }
        for (const auto& adapter :
            materialization.block_input_adapters) {
            record_lowering_census(
                vhdl_generated_input_actual_occurrences_);
            if (lower_compiled_vhdl_input_conversion(generated_lowerer,
                    materialization.specialization, adapter.expression,
                    adapter.destination, materialization.path,
                    adapter.subtype, architecture)) {
                continue;
            }
            auto lowered = generated_lowerer.lower_hir_input_actual(
                adapter.expression,
                adapter.destination,
                frontend::Language::Vhdl2008,
                materialization.path,
                concurrent_order++,
                adapter.subtype);
            if (!lowered) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled VHDL block input default could not be "
                    "lowered from HIR",
                    compiled_source_span(*compiled_, adapter.source));
                return false;
            }
            lowered->language_standard = architecture.standard;
            lowered->compatibility_profile
                = architecture.compatibility_profile;
            canonicalize_process_operations(*lowered);
            specialization.processes.push_back(lowered->id);
            design_.append_process_record(std::move(*lowered));
            append_generated_vhdl_processes(
                generated_lowerer, architecture, specialization);
        }
        for (const auto statement_id :
            materialization.region->concurrent_statements) {
            const auto statement
                = materialization.specialization.find_statement(
                    statement_id);
            const auto statement_source = statement
                    && statement->vhdl != nullptr
                ? statement->vhdl->source
                : materialization.region->source;
            if (report_vhdl_unspecified_inference(
                    generated_lowerer.hir_vhdl_unspecified_inference_failure(
                        statement_id))) {
                return false;
            }
            record_lowering_census(vhdl_generated_concurrent_occurrences_);
            if (lower_compiled_vhdl_formal_conversion(generated_lowerer,
                    statement_id, materialization.path, architecture)) {
                ++concurrent_order;
                continue;
            }
            const auto guard = materialization.region->kind
                    == semantic::vhdl::GenerateKind::block
                ? materialization.region->condition
                : std::nullopt;
            if (!guard) {
                // Instances with equal specializations repeat these
                // statements; the template cache replays them.
                auto occurrence = lower_cached_vhdl_occurrence(
                    entity, architecture, materialization.specialization,
                    generated_lowerer, statement_id, materialization.path,
                    concurrent_order++, std::nullopt,
                    materialization.generate_relative_discriminator,
                    generated_signal_declarations(materialization));
                if (!occurrence) {
                    report("FSIM-ELAB-HIR-001",
                        std::string {
                            "compiled generated VHDL concurrent statement "
                            "could not be lowered from HIR" }
                            + hir_lowering_rejection_detail(
                                generated_lowerer),
                        compiled_source_span(
                            *compiled_, statement_source));
                    return false;
                }
                if (occurrence->instance) {
                    auto& instance = *occurrence->instance;
                    canonicalize_process_operations(
                        occurrence->common, instance);
                    specialization.processes.push_back(instance.id);
                    design_.append_process_instance_record(
                        std::move(occurrence->common), std::move(instance));
                } else {
                    auto& process_instance = *occurrence->process;
                    process_instance.language_standard = architecture.standard;
                    process_instance.compatibility_profile
                        = architecture.compatibility_profile;
                    canonicalize_process_operations(process_instance);
                    specialization.processes.push_back(process_instance.id);
                    design_.append_process_record(
                        std::move(process_instance));
                }
                append_generated_vhdl_processes(
                    generated_lowerer, architecture, specialization);
                continue;
            }
            auto lowered = generated_lowerer
                               .lower_hir_concurrent_statement(
                                   statement_id,
                                   frontend::Language::Vhdl2008,
                                   materialization.path,
                                   concurrent_order++,
                                   guard);
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    std::string {
                        "compiled generated VHDL concurrent statement "
                        "could not be lowered from HIR" }
                        + hir_lowering_rejection_detail(generated_lowerer),
                    compiled_source_span(
                        *compiled_, statement_source));
                return false;
            }
            lowered->language_standard = architecture.standard;
            lowered->compatibility_profile
                = architecture.compatibility_profile;
            canonicalize_process_operations(*lowered);
            specialization.processes.push_back(lowered->id);
            design_.append_process_record(std::move(*lowered));
            append_generated_vhdl_processes(
                generated_lowerer, architecture, specialization);
        }
        for (const auto process_id :
            materialization.region->processes) {
            const auto process
                = materialization.specialization.find_process(process_id);
            const auto process_source = process
                    && process->vhdl != nullptr
                ? process->vhdl->source
                : materialization.region->source;
            if (report_vhdl_unspecified_inference(
                    generated_lowerer.hir_vhdl_unspecified_inference_failure(
                        process_id))) {
                return false;
            }
            record_lowering_census(vhdl_generated_process_lower_requests_);
            auto lowered = lower_cached_vhdl_occurrence(
                entity, architecture, materialization.specialization,
                generated_lowerer, semantic::StatementId { },
                materialization.path, concurrent_order++, process_id,
                materialization.generate_relative_discriminator,
                generated_signal_declarations(materialization));
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated VHDL process could not be "
                    "lowered from HIR",
                    compiled_source_span(*compiled_, process_source));
                return false;
            }
            const bool anonymous_process = process
                && process->vhdl != nullptr
                && process->vhdl->name == "<process>";
            if (lowered->instance) {
                auto& instance = *lowered->instance;
                if (anonymous_process) {
                    instance.name = materialization.path;
                }
                canonicalize_process_operations(
                    lowered->common, instance);
                specialization.processes.push_back(instance.id);
                specialization.semantic_processes.emplace_back(
                    instance.id, process_id);
                design_.append_process_instance_record(
                    std::move(lowered->common), std::move(instance));
            } else {
                auto& process_instance = *lowered->process;
                if (anonymous_process) {
                    process_instance.name = materialization.path;
                }
                process_instance.language_standard = architecture.standard;
                process_instance.compatibility_profile
                    = architecture.compatibility_profile;
                canonicalize_process_operations(process_instance);
                specialization.processes.push_back(process_instance.id);
                specialization.semantic_processes.emplace_back(
                    process_instance.id, process_id);
                design_.append_process_record(std::move(process_instance));
            }
            append_generated_vhdl_processes(
                generated_lowerer, architecture, specialization);
        }
    }
    return true;
}

} // namespace fsim::elaboration
