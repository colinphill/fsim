// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration::elaboration_detail {

enum class CompiledPortMode : std::uint8_t {
    unknown,
    input,
    output,
    inout,
    buffer,
    reference,
};

enum class CompiledActualKind : std::uint8_t {
    expression,
    type,
    default_value,
    open,
    default_box,
};

/// Non-owning dispatch over one compiled HIR port declaration. The selected
/// declaration and every returned type/range view remain owned by the
/// CompiledDesign passed to unit_ports().
struct CompiledPortView {
    semantic::Language language { semantic::Language::system_verilog };
    const semantic::sv::Declaration* systemverilog { };
    const semantic::vhdl::Declaration* vhdl { };

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] semantic::DeclarationId declaration() const noexcept;
    [[nodiscard]] CompiledPortMode mode() const noexcept;
    [[nodiscard]] semantic::SourceSpanId source() const noexcept;
    [[nodiscard]] semantic::OriginId origin() const noexcept;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    default_expression() const noexcept;
    [[nodiscard]] const semantic::sv::TypeReference*
    systemverilog_type() const noexcept;
    [[nodiscard]] const semantic::sv::PackedRange*
    systemverilog_packed_range() const noexcept;
    [[nodiscard]] const semantic::vhdl::SubtypeIndication*
    vhdl_subtype() const noexcept;
    [[nodiscard]] std::span<const semantic::vhdl::RangeConstraint>
    vhdl_constraints() const noexcept;
    [[nodiscard]] std::string_view interface_type() const noexcept;
    [[nodiscard]] std::string_view modport() const noexcept;
};

/// Non-owning dispatch over one compiled HIR parameter, generic, or port
/// association. The association and every referenced expression/type remain
/// owned by the CompiledDesign that owns the instance record.
struct CompiledActualView {
    const semantic::sv::ActualAssociation* systemverilog { };
    const semantic::vhdl::Association* vhdl { };

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::optional<std::string_view> formal() const noexcept;
    [[nodiscard]] CompiledActualKind kind() const noexcept;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    expression() const noexcept;
    [[nodiscard]] const semantic::sv::TypeReference*
    systemverilog_type() const noexcept;
    [[nodiscard]] const semantic::vhdl::SubtypeIndication*
    vhdl_subtype() const noexcept;
    [[nodiscard]] semantic::SourceSpanId source() const noexcept;
};

[[nodiscard]] std::vector<CompiledActualView>
instance_parameters(semantic::CompiledInstanceView instance);

[[nodiscard]] std::vector<CompiledActualView>
instance_ports(semantic::CompiledInstanceView instance);

std::optional<semantic::CompiledUnitView> choose_unit(
    const semantic::CompiledDesign& compiled,
    const std::string& requested);

struct TargetSpec {
    std::string language;
    std::string library;
    std::string unit;
    std::optional<std::string> architecture;
};

std::optional<TargetSpec> parse_target(std::string_view spelling);

std::optional<std::vector<CompiledPortView>> unit_ports(
    const semantic::CompiledDesign& compiled,
    semantic::CompiledUnitView unit);

std::optional<semantic::CompiledUnitView> choose_bound_unit(
    const semantic::CompiledDesign& compiled,
    const TargetSpec& target);

std::optional<semantic::CompiledUnitView> choose_top_unit(
    const semantic::CompiledDesign& compiled,
    std::string_view spelling);

struct CompiledSystemVerilogUnitMatch {
    std::optional<semantic::CompiledUnitView> unit;
    bool ambiguous { };
};

/// Resolve exactly one non-external SystemVerilog module in a logical
/// library. Keeping this lookup here gives configuration, bind, and ordinary
/// target selection one canonical module-selection policy.
[[nodiscard]] CompiledSystemVerilogUnitMatch
find_exact_systemverilog_module(
    const semantic::CompiledDesign& compiled,
    std::string_view library,
    std::string_view name);

struct UnitResolutionCandidate {
    std::optional<std::string> systemc_target;
    std::string identity;
    std::optional<semantic::CompiledUnitView> compiled_unit { };
    const semantic::sv::UdpDeclaration* compiled_udp{};
};

std::vector<UnitResolutionCandidate> resolve_unit_candidates(
    const semantic::CompiledDesign& compiled,
    std::string_view library,
    std::string_view name,
    bool include_vhdl_configurations = false,
    bool include_udp_declarations = true);

std::vector<std::string> effective_search_scope(
    std::string_view parent_library,
    std::span<const std::string> search_libraries);

bool has_logical_library(
    const semantic::CompiledDesign& compiled,
    std::span<const SystemCFactoryCandidate> systemc_candidates,
    std::span<const std::string> systemc_libraries,
    std::string_view library);

std::string format_resolution_candidates(
    std::span<const UnitResolutionCandidate> candidates);

std::optional<semantic::CompiledUnitView> choose_same_language_instance(
    const semantic::CompiledDesign& compiled,
    semantic::CompiledUnitView parent,
    std::string_view name);

std::string unit_identity(semantic::CompiledUnitView unit);

} // namespace fsim::elaboration::elaboration_detail
