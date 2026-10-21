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

// A packed integral variable port with no net type: it keeps its own
// storage and drives a resolved actual as one source (IEEE 1800-2017 23.3.3).
[[nodiscard]] bool is_simple_variable_output_port(
    const semantic::sv::Declaration& declaration,
    const semantic::sv::TypeReference& effective_type) noexcept;

} // namespace fsim::elaboration::hierarchy_sv_ports_detail
