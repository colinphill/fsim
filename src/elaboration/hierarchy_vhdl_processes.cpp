// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace fsim::elaboration {
namespace {

    frontend::SourceSpan compiled_source_span(
        const semantic::CompiledDesign& compiled,
        const semantic::SourceSpanId source)
    {
        frontend::SourceSpan result;
        const auto& spans = compiled.semantics.source_spans();
        if (!source.valid() || source.value() >= spans.size()) {
            return result;
        }
        const auto& span = spans[source.value()];
        result.source_name = span.logical_name;
        result.begin = {
            static_cast<std::size_t>(span.begin.offset),
            span.begin.line,
            span.begin.column,
        };
        result.end = {
            static_cast<std::size_t>(span.end.offset),
            span.end.line,
            span.end.column,
        };
        const auto& files = compiled.semantics.source_files();
        if (span.file.valid() && span.file.value() < files.size()) {
            result.physical_source_name = files[span.file.value()].physical_name;
        }
        return result;
    }

} // namespace

bool HierarchyBuilder::lower_compiled_vhdl_generated_processes(
    std::vector<VhdlHirMaterialization>& materializations,
    const semantic::vhdl::Unit& architecture,
    StringMap& string_objects,
    ReadOnlyStringSet& read_only_strings,
    ContainerMap& container_objects,
    ReadOnlyContainerSet& read_only_containers,
    SpecializationInfo& specialization,
    std::size_t& concurrent_order)
{
    const auto append_generated_processes = [&](
                                                Lowerer& source,
                                                const semantic::vhdl::Unit& owner) {
        for (auto& generated : source.take_generated_processes()) {
            generated.language_standard = owner.standard;
            generated.compatibility_profile
                = owner.compatibility_profile;
            canonicalize_process_operations(generated);
            specialization.processes.push_back(generated.id);
            design_.processes_.push_back(std::move(generated));
        }
    };
    const auto report_unspecified_inference = [&](Lowerer& source,
                                                  const auto id) {
        const auto failure
            = source.hir_vhdl_unspecified_inference_failure(id);
        if (!failure) {
            return false;
        }
        report(
            "FSIM-ELAB-VHUNSPEC-001",
            "VHDL-2019 unspecified interface types do not infer one "
            "consistent callable profile",
            compiled_source_span(*compiled_, *failure));
        return true;
    };

    for (auto& materialization : materializations) {
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
            design_.processes_.push_back(std::move(*lowered));
            append_generated_processes(generated_lowerer, architecture);
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
            if (report_unspecified_inference(
                    generated_lowerer, statement_id)) {
                return false;
            }
            auto lowered = generated_lowerer
                               .lower_hir_concurrent_statement(
                                   statement_id,
                                   frontend::Language::Vhdl2008,
                                   materialization.path,
                                   concurrent_order++,
                                   materialization.region->kind
                                           == semantic::vhdl::GenerateKind::block
                                       ? materialization.region->condition
                                       : std::nullopt);
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated VHDL concurrent statement "
                    "could not be lowered from HIR",
                    compiled_source_span(
                        *compiled_, statement_source));
                return false;
            }
            lowered->language_standard = architecture.standard;
            lowered->compatibility_profile
                = architecture.compatibility_profile;
            canonicalize_process_operations(*lowered);
            specialization.processes.push_back(lowered->id);
            design_.processes_.push_back(std::move(*lowered));
            append_generated_processes(generated_lowerer, architecture);
        }
        for (const auto process_id :
            materialization.region->processes) {
            const auto process
                = materialization.specialization.find_process(process_id);
            const auto process_source = process
                    && process->vhdl != nullptr
                ? process->vhdl->source
                : materialization.region->source;
            if (report_unspecified_inference(
                    generated_lowerer, process_id)) {
                return false;
            }
            auto lowered = generated_lowerer.lower_hir_process(
                process_id,
                frontend::Language::Vhdl2008,
                materialization.path);
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated VHDL process could not be "
                    "lowered from HIR",
                    compiled_source_span(*compiled_, process_source));
                return false;
            }
            if (process && process->vhdl != nullptr
                && process->vhdl->name == "<process>") {
                lowered->name = materialization.path;
            }
            lowered->language_standard = architecture.standard;
            lowered->compatibility_profile
                = architecture.compatibility_profile;
            canonicalize_process_operations(*lowered);
            specialization.processes.push_back(lowered->id);
            specialization.semantic_processes.emplace_back(
                lowered->id, process_id);
            design_.processes_.push_back(std::move(*lowered));
            append_generated_processes(generated_lowerer, architecture);
        }
    }
    return true;
}

} // namespace fsim::elaboration
