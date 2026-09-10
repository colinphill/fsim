// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/design_artifact.hpp"

namespace fsim::app::codec_detail {

bool portable_semantics(
    const semantic::ModelRecords& records,
    diagnostic::Engine& diagnostics);
bool valid_class_state(
    const std::vector<frontend::SystemVerilogClassSpecialization>& classes);
bool valid_systemverilog_constraint_classes(
    const std::vector<semantic::sv::ClassDeclaration>& declarations);

} // namespace fsim::app::codec_detail
