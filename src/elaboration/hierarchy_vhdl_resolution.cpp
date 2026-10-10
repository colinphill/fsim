// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace fsim::elaboration {
namespace {

    frontend::SourceSpan resolver_source_span(
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

std::optional<CompiledVhdlResolutionDiagnostic>
HierarchyBuilder::register_compiled_vhdl_resolution(
    const SignalId signal,
    const semantic::vhdl::SubtypeIndication& subtype,
    const semantic::SourceSpanId declaration_source,
    const CompiledVhdlResolutionBinding& binding,
    const semantic::SpecializedHirUnit& specialization)
{
    resolver_by_signal_.insert_or_assign(signal, binding.designator);
    const auto source = binding.source.valid()
        ? binding.source
        : declaration_source;

    if (binding.kind == ResolutionKind::vhdl_user_function) {
        vhdl_function_resolvers_.insert_or_assign(signal,
            PendingVhdlResolver { binding.body, source,
                std::make_shared<const semantic::SpecializedHirUnit>(
                    specialization) });
    }
    if (binding.kind) {
        const auto [existing, inserted]
            = vhdl_resolution_kinds_.emplace(
                binding.designator, *binding.kind);
        if (!inserted && existing->second != *binding.kind) {
            return CompiledVhdlResolutionDiagnostic {
                "FSIM-ELAB-VHRESOLVE-003",
                "VHDL resolution function designator '"
                    + binding.designator
                    + "' denotes conflicting visible bodies",
                source,
            };
        }
        return std::nullopt;
    }

    switch (binding.issue) {
    case CompiledVhdlResolutionIssue::missing_body:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-001",
            "VHDL resolution function '" + binding.designator
                + "' is not visible with an executable body",
            source,
        };
    case CompiledVhdlResolutionIssue::invalid_profile:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-002",
            "VHDL resolution function '" + binding.designator
                + "' must be pure with one array-of-base-type "
                  "input and a base-type result",
            source,
        };
    case CompiledVhdlResolutionIssue::ambiguous_profile:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-003",
            "VHDL resolution function '" + binding.designator
                + "' is ambiguous for subtype '"
                + subtype.type_mark.spelling + "'",
            source,
        };
    case CompiledVhdlResolutionIssue::unsupported_body:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-004",
            "bounded VHDL resolution function '"
                + binding.designator
                + "' must return one scalar OR or AND expression",
            source,
        };
    case CompiledVhdlResolutionIssue::none:
        return std::nullopt;
    }

    return std::nullopt;
}

void HierarchyBuilder::lower_compiled_vhdl_resolvers(
    const std::unordered_map<SignalId, std::vector<ProcessId>>& drivers)
{
    for (const auto& [signal, pending] : vhdl_function_resolvers_) {
        if (signal >= design_.signals_.size()
            || design_.signals_[signal].resolution
                != ResolutionKind::vhdl_user_function) {
            continue;
        }
        const auto found = drivers.find(signal);
        if (found == drivers.end() || found->second.empty()) {
            // Without drivers the resolution function is never called.
            set_resolution(signal, ResolutionKind::none);
            continue;
        }
        const SignalBindings signals;
        const ReadOnlySignalBindings read_only_signals;
        const StringObjectBindings string_objects;
        const ReadOnlyStringBindings read_only_strings;
        const ContainerObjectBindings container_objects;
        const ReadOnlyContainerBindings read_only_containers;
        Lowerer lowerer {
            design_,
            signals,
            read_only_signals,
            string_objects,
            read_only_strings,
            container_objects,
            read_only_containers,
            diagnostics_,
        };
        lowerer.set_specialized_hir_unit(pending.specialization.get());
        const auto& name = design_.signal_info_.at(signal).name;
        const auto separator = name.find_last_of('.');
        const auto hierarchy = separator == std::string::npos
            ? std::string { }
            : name.substr(0U, separator);
        auto program = lowerer.lower_hir_vhdl_resolver_process(
            signal, pending.body, found->second.size(), hierarchy);
        if (!program) {
            report("FSIM-ELAB-VHRESOLVE-004",
                "the VHDL resolution function of '" + name
                    + "' could not be lowered as a resolver program",
                resolver_source_span(*compiled_, pending.source));
            continue;
        }
        const auto process = program->process.id;
        design_.append_process_record(std::move(program->process));
        design_.signals_[signal].vhdl_resolver = VhdlSignalResolver {
            process, program->input, program->output, found->second };
    }
}

} // namespace fsim::elaboration
