// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration::hierarchy_sv_generate_detail {

struct Occurrence {
    // The region is borrowed from the compiled unit; the caller must keep the
    // compiled design alive while consuming the result.
    const semantic::sv::GenerateRegion* region { };
    std::string path;
    semantic::SpecializedHirUnit specialization;
};

struct Diagnostic {
    std::string code;
    std::string message;
    semantic::SourceSpanId source;
};

struct CollectionResult {
    std::vector<Occurrence> occurrences;
    std::optional<Diagnostic> diagnostic;
};

struct ValidationResult {
    std::optional<Diagnostic> diagnostic;

    [[nodiscard]] bool valid() const noexcept
    {
        return !diagnostic.has_value();
    }
};

[[nodiscard]] CollectionResult collect_occurrences(
    const semantic::sv::Unit& unit,
    std::string_view parent_path,
    const semantic::SpecializedHirUnit& specialization);

[[nodiscard]] ValidationResult validate_generated_constant(
    const Occurrence& occurrence,
    semantic::DeclarationId declaration_id);

} // namespace fsim::elaboration::hierarchy_sv_generate_detail
