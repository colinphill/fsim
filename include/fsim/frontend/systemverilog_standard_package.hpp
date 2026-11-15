// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/token.hpp"
#include "fsim/support/systemverilog_builtin_type.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fsim::frontend {

enum class SystemVerilogStandardPackageMemberKind : std::uint8_t {
    class_type,
    function,
};

struct SystemVerilogStandardPackageDeclaration {
    std::string_view name;
    SystemVerilogStandardPackageMemberKind kind;
    StandardRevision minimum_standard;
    bool parameterized { };
    bool abstract_class { };
    bool final_in_2023 { };
    support::SystemVerilogStandardClass standard_class {
        support::SystemVerilogStandardClass::none };
};

struct SystemVerilogStandardPackageSnapshot {
    std::string_view revision;
    std::string_view declaration_identity;
    std::span<const SystemVerilogStandardPackageDeclaration> declarations;
};

/// Returns the compiler-owned standard-package view for a SystemVerilog
/// profile. Verilog and VHDL revisions deliberately have no such view.
[[nodiscard]] std::optional<SystemVerilogStandardPackageSnapshot>
systemverilog_standard_package(StandardRevision standard) noexcept;

/// The value of a constant of the std class process's `state` enum (IEEE
/// 1800-2023 9.7: FINISHED, RUNNING, WAITING, SUSPENDED, KILLED in that
/// order); nullopt for any other member name.
[[nodiscard]] std::optional<std::uint32_t>
systemverilog_process_state_value(std::string_view member) noexcept;

/// Finds one directly declared member in the selected standard-package view.
/// Nested class member selection is validated by the owning class service.
[[nodiscard]] const SystemVerilogStandardPackageDeclaration*
find_systemverilog_standard_package_declaration(
    StandardRevision standard, std::string_view name) noexcept;

} // namespace fsim::frontend
