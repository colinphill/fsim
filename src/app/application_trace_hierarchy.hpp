// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/trace_model.hpp"
#include "fsim/semantic/design_ir.hpp"

#include <string>

namespace fsim::app::application_detail {

struct FstTraceObject {
    std::string path;
    runtime::TraceSourceMetadata source;
};

[[nodiscard]] FstTraceObject canonical_fst_trace_object(
    const semantic::design::DesignIr& design,
    const semantic::design::Object& object);

} // namespace fsim::app::application_detail
