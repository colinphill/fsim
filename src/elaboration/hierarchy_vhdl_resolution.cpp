// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace fsim::elaboration {
namespace {

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
                compiled_source_span(*compiled_, pending.source));
            continue;
        }
        const auto process = program->process.id;
        design_.append_process_record(std::move(program->process));
        VhdlSignalResolver resolver {
            process, program->input, program->output, found->second };
        // A guarded signal resolves its connected drivers only; a bus with
        // none calls the function with a null array (14.7.3.2).
        if (const auto guarded = vhdl_guarded_signal_kinds_.find(signal);
            guarded != vhdl_guarded_signal_kinds_.end()) {
            resolver.guarded_kind = guarded->second;
            for (std::size_t count { }; count < found->second.size();
                ++count) {
                if (count == 0U && guarded->second == 2U) {
                    resolver.variants.push_back(resolver.variants.empty()
                            ? VhdlResolverVariant { process, program->input,
                                  program->output }
                            : resolver.variants.back());
                    continue;
                }
                const auto diagnostics_before = diagnostics_.size();
                auto variant = lowerer.lower_hir_vhdl_resolver_process(
                    signal, pending.body, count, hierarchy);
                if (count == 0U && diagnostics_.size() != diagnostics_before) {
                    variant.reset();
                }
                if (!variant && count == 0U) {
                    diagnostics_.resize(diagnostics_before);
                    // A null array argument is not yet lowered; the bus
                    // then keeps its value (marked by an invalid process).
                    resolver.variants.push_back(VhdlResolverVariant {
                        std::numeric_limits<ProcessId>::max(), 0U, 0U });
                    continue;
                }
                if (!variant) {
                    break;
                }
                const auto variant_process = variant->process.id;
                design_.append_process_record(std::move(variant->process));
                resolver.variants.push_back(VhdlResolverVariant {
                    variant_process, variant->input, variant->output });
            }
        }
        design_.signals_[signal].vhdl_resolver = std::move(resolver);
    }
}

} // namespace fsim::elaboration
