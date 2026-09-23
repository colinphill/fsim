// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"

#include <string>
#include <utility>
#include <vector>

namespace fsim::app::sdf_detail {

using frontend::Diagnostic;
using frontend::DiagnosticSeverity;
using frontend::SourceSpan;

inline void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
    std::string message, const SourceSpan& span)
{
    diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
        std::move(code), std::move(message), span, { } });
}

} // namespace fsim::app::sdf_detail
