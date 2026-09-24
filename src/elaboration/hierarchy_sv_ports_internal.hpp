// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::elaboration::hierarchy_sv_ports_detail {

enum class PortActualStatus : std::uint8_t {
    unconnected,
    ready,
    invalid_child_port,
    fatal,
};

struct PortActualDiagnostic {
    std::string code;
    std::string message;
};

struct PortActualResult {
    PortActualStatus status { PortActualStatus::ready };
    std::string actual_name;
    std::optional<PortActualDiagnostic> diagnostic;
};

[[nodiscard]] PortActualResult normalize_port_actual(
    const semantic::SpecializedHirAssociationBinding& binding,
    const semantic::sv::Declaration& formal,
    const semantic::SpecializedHirUnit& specialization,
    std::string_view child_path);

} // namespace fsim::elaboration::hierarchy_sv_ports_detail
