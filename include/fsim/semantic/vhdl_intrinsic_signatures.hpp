// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace fsim::semantic {

/// Parameter modes and classes of a compiler-owned subprogram's formals
/// (IEEE 1076-2019 4.2.2.1, 4.2.2.2).
enum class VhdlIntrinsicMode : std::uint8_t { in, out, inout, buffer, linkage };
enum class VhdlIntrinsicClass : std::uint8_t { constant, signal, variable, file };

/// One formal of a compiler-owned subprogram. `type` is the lower-case
/// spelling of the declared subtype indication; naming the type is the
/// resolution pass's job.
struct VhdlIntrinsicFormal {
    std::string_view name;
    std::string_view type;
    VhdlIntrinsicMode mode;
    VhdlIntrinsicClass formal_class;
    bool has_default;
};

/// The declaration of a subprogram of a package the compiler serves itself
/// (STD.STANDARD, STD.TEXTIO, STD.ENV, IEEE.STD_LOGIC_1164, IEEE.NUMERIC_BIT,
/// IEEE.NUMERIC_STD, their UNSIGNED companions, IEEE.MATH_REAL,
/// IEEE.MATH_COMPLEX and IEEE.STD_LOGIC_TEXTIO), read at build time from the
/// package declarations in third_party/ieee-1076-2019
/// (scripts/generate_intrinsic_signatures.py). The design retains none of
/// them, so these are the interpretations of such a name that the
/// overload resolution of 12.5 considers. An operator's name is its
/// designator ("+", "and"). `result` is empty for a procedure. `since` is the
/// first language revision ("1987", "1993", "2008") that declares it.
struct VhdlIntrinsicSignature {
    std::string_view library;
    std::string_view package;
    std::string_view name;
    std::string_view result;
    std::uint32_t first_formal;
    std::uint32_t formal_count;
    bool procedure;
    bool impure;
    std::string_view since;
};

/// Every generated signature, in package analysis order.
[[nodiscard]] std::span<const VhdlIntrinsicSignature>
vhdl_intrinsic_signatures() noexcept;

/// The formals of `signature`, in declaration order.
[[nodiscard]] std::span<const VhdlIntrinsicFormal> vhdl_intrinsic_formals(
    const VhdlIntrinsicSignature& signature) noexcept;

} // namespace fsim::semantic
