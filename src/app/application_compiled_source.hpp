// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/semantic/model.hpp"

namespace fsim::app::application_detail {

/// The diagnostic location of a compiled source span: its logical name, or
/// the physical file name when it has none. Empty for an unknown span.
[[nodiscard]] diagnostic::SourceSpan compiled_source_span(
    const semantic::Model& model, semantic::SourceSpanId source);

} // namespace fsim::app::application_detail
