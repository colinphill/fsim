// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/systemverilog_standard_package.hpp"

#include <array>
#include <ranges>

namespace fsim::frontend {
namespace {

using Kind = SystemVerilogStandardPackageMemberKind;

// This is an independently authored semantic inventory. Keep the 2023-only
// declaration last so earlier profiles are zero-allocation prefix views.
constexpr std::array<SystemVerilogStandardPackageDeclaration, 5>
    kDeclarations { {
        { "mailbox", Kind::class_type,
            StandardRevision::SystemVerilog2005, true, false, false },
        { "process", Kind::class_type,
            StandardRevision::SystemVerilog2005, false, true, true },
        { "semaphore", Kind::class_type,
            StandardRevision::SystemVerilog2005, false, false, false },
        { "randomize", Kind::function,
            StandardRevision::SystemVerilog2005, false, false, false },
        { "weak_reference", Kind::class_type,
            StandardRevision::SystemVerilog2023, true, false, false },
    } };

struct Identity {
    StandardRevision standard;
    std::string_view revision;
    std::string_view digest;
};

constexpr std::array<Identity, 5> kIdentities { {
    { StandardRevision::SystemVerilog2005,
        "ieee-1800-2005:std:fsim-v3",
        "59f277143a00578b23751bef909878773599f46fdfbcc8d34a96b557a3f85bb9" },
    { StandardRevision::SystemVerilog2009,
        "ieee-1800-2009:std:fsim-v3",
        "0ed4f42045e54cf2951f4efe4e64b3e3dcf7377e9b93869b3e0a5911f37d6837" },
    { StandardRevision::SystemVerilog2012,
        "ieee-1800-2012:std:fsim-v3",
        "f14eb85140f132bd5b066773dd94e54f57e47742b26781da429a389867e8a898" },
    { StandardRevision::SystemVerilog2017,
        "ieee-1800-2017:std:fsim-v3",
        "f14d51f2f6484cacf2552d7f41c4edc13173fb2086418bf9a684bba3e8451361" },
    { StandardRevision::SystemVerilog2023,
        "ieee-1800-2023:std:fsim-v3",
        "dcf685c1c89945589b737b4a02fa162d23dbfeba3a2b219480f723637ef5a148" },
} };

} // namespace

std::optional<SystemVerilogStandardPackageSnapshot>
systemverilog_standard_package(const StandardRevision standard) noexcept
{
    const auto identity = std::ranges::find(
        kIdentities, standard, &Identity::standard);
    if (identity == kIdentities.end()) {
        return std::nullopt;
    }
    const auto count = standard == StandardRevision::SystemVerilog2023
        ? kDeclarations.size()
        : kDeclarations.size() - 1U;
    return SystemVerilogStandardPackageSnapshot {
        identity->revision,
        identity->digest,
        std::span { kDeclarations }.first(count),
    };
}

const SystemVerilogStandardPackageDeclaration*
find_systemverilog_standard_package_declaration(
    const StandardRevision standard, const std::string_view name) noexcept
{
    const auto package = systemverilog_standard_package(standard);
    if (!package) {
        return nullptr;
    }
    const auto found = std::ranges::find(
        package->declarations, name,
        &SystemVerilogStandardPackageDeclaration::name);
    return found == package->declarations.end() ? nullptr : &*found;
}

} // namespace fsim::frontend
