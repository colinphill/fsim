// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/design_artifact.hpp"

namespace fsim::app::codec_detail {

bool portable_semantics(
    const semantic::ModelRecords& records,
    diagnostic::Engine& diagnostics);
} // namespace fsim::app::codec_detail
