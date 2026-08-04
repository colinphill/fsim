// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration::elaboration_detail {

using frontend::DesignUnit;

const DesignUnit* choose_unit(
    const frontend::ParsedDesign& parsed, const std::string& requested);

struct TargetSpec {
    std::string language;
    std::string library;
    std::string unit;
    std::optional<std::string> architecture;
};

std::optional<TargetSpec> parse_target(std::string_view spelling);

const DesignUnit* find_vhdl_entity(
    const frontend::ParsedDesign& parsed, const DesignUnit& architecture);

const std::vector<frontend::SignalDeclaration>* unit_ports(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& unit);

const DesignUnit* choose_bound_unit(
    const frontend::ParsedDesign& parsed,
    const TargetSpec& target);

const DesignUnit* choose_top_unit(
    const frontend::ParsedDesign& parsed,
    std::string_view spelling);

struct UnitResolutionCandidate {
    const DesignUnit* unit{};
    std::optional<std::string> systemc_target;
    std::string identity;
    const frontend::VerilogUdpDeclaration* udp{};
};

std::vector<UnitResolutionCandidate> resolve_unit_candidates(
    const frontend::ParsedDesign& parsed,
    std::string_view library,
    std::string_view name,
    bool include_vhdl_configurations = false,
    bool include_udp_declarations = true);

std::vector<std::string> effective_search_scope(
    std::string_view parent_library,
    std::span<const std::string> search_libraries);

bool has_logical_library(
    const frontend::ParsedDesign& parsed,
    std::span<const SystemCFactoryCandidate> systemc_candidates,
    std::span<const std::string> systemc_libraries,
    std::string_view library);

std::string format_resolution_candidates(
    std::span<const UnitResolutionCandidate> candidates);

const DesignUnit* choose_same_language_instance(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& parent,
    std::string_view name);

std::string unit_identity(const DesignUnit& unit);

} // namespace fsim::elaboration::elaboration_detail
