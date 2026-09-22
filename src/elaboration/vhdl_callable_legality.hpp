// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design_specialization.hpp"

#include <span>
#include <string>
#include <vector>

namespace fsim::elaboration {

struct VhdlCallableLegalityFailure {
    std::string code;
    std::string message;
    semantic::SourceSpanId source;
};

[[nodiscard]] std::vector<VhdlCallableLegalityFailure>
validate_vhdl_callable_legality(
    const semantic::SpecializedHirUnit& specialization,
    std::span<const semantic::DeclarationId> declarations);

} // namespace fsim::elaboration
