// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {

std::optional<CompiledVhdlResolutionDiagnostic>
HierarchyBuilder::register_compiled_vhdl_resolution(
    const SignalId signal,
    const semantic::vhdl::SubtypeIndication& subtype,
    const semantic::SourceSpanId declaration_source,
    const CompiledVhdlResolutionBinding& binding)
{
    resolver_by_signal_.insert_or_assign(signal, binding.designator);
    const auto source = binding.source.valid()
        ? binding.source
        : declaration_source;

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

} // namespace fsim::elaboration
