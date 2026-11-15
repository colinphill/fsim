// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

namespace fsim::support {

// The built-in data types of SystemVerilog (IEEE 1800-2023 6.11, 6.12, 6.14,
// 6.16, 6.17, 6.13 `void`): the type a keyword names. A type is identified
// by this kind once the parser has read its keyword; no later stage compares
// a type's spelling with a keyword.
enum class SystemVerilogBuiltinType : std::uint8_t {
    none,
    bit,
    logic,
    reg,
    byte,
    shortint,
    int_type,
    longint,
    integer,
    time,
    shortreal,
    real,
    realtime,
    string,
    chandle,
    event,
    void_type,
};

// The built-in net types (IEEE 1800-2023 6.7): the kind a net-type keyword
// names. `interconnect` (6.6.8) is kept apart by its own flag.
enum class SystemVerilogNetKind : std::uint8_t {
    none,
    wire,
    tri,
    wand,
    triand,
    wor,
    trior,
    tri0,
    tri1,
    trireg,
    supply0,
    supply1,
    uwire,
};

// The kind a data-type keyword names, `none` for any other text. Only the
// parser's keyword recognition (IEEE 1800-2023 5.6.2: keywords are
// reserved) and the tables that read source type text call it.
[[nodiscard]] constexpr SystemVerilogBuiltinType
systemverilog_builtin_type_keyword(const std::string_view keyword) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    return keyword == "bit"       ? Kind::bit
        : keyword == "logic"     ? Kind::logic
        : keyword == "reg"       ? Kind::reg
        : keyword == "byte"      ? Kind::byte
        : keyword == "shortint"  ? Kind::shortint
        : keyword == "int"       ? Kind::int_type
        : keyword == "longint"   ? Kind::longint
        : keyword == "integer"   ? Kind::integer
        : keyword == "time"      ? Kind::time
        : keyword == "shortreal" ? Kind::shortreal
        : keyword == "real"      ? Kind::real
        : keyword == "realtime"  ? Kind::realtime
        : keyword == "string"    ? Kind::string
        : keyword == "chandle"   ? Kind::chandle
        : keyword == "event"     ? Kind::event
        : keyword == "void"      ? Kind::void_type
                                 : Kind::none;
}

// The keyword of a kind, for text the tool writes (`$typename`, VPI type
// names, diagnostics); empty for `none`.
[[nodiscard]] constexpr std::string_view systemverilog_builtin_type_name(
    const SystemVerilogBuiltinType kind) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    switch (kind) {
    case Kind::bit: return "bit";
    case Kind::logic: return "logic";
    case Kind::reg: return "reg";
    case Kind::byte: return "byte";
    case Kind::shortint: return "shortint";
    case Kind::int_type: return "int";
    case Kind::longint: return "longint";
    case Kind::integer: return "integer";
    case Kind::time: return "time";
    case Kind::shortreal: return "shortreal";
    case Kind::real: return "real";
    case Kind::realtime: return "realtime";
    case Kind::string: return "string";
    case Kind::chandle: return "chandle";
    case Kind::event: return "event";
    case Kind::void_type: return "void";
    case Kind::none: break;
    }
    return { };
}

// The kind a net-type keyword names (6.7), `none` for any other text; the
// parser's keyword recognition only.
[[nodiscard]] constexpr SystemVerilogNetKind systemverilog_net_kind_keyword(
    const std::string_view keyword) noexcept
{
    using Kind = SystemVerilogNetKind;
    return keyword == "wire"    ? Kind::wire
        : keyword == "tri"     ? Kind::tri
        : keyword == "wand"    ? Kind::wand
        : keyword == "triand"  ? Kind::triand
        : keyword == "wor"     ? Kind::wor
        : keyword == "trior"   ? Kind::trior
        : keyword == "tri0"    ? Kind::tri0
        : keyword == "tri1"    ? Kind::tri1
        : keyword == "trireg"  ? Kind::trireg
        : keyword == "supply0" ? Kind::supply0
        : keyword == "supply1" ? Kind::supply1
        : keyword == "uwire"   ? Kind::uwire
                               : Kind::none;
}

[[nodiscard]] constexpr std::string_view systemverilog_net_kind_name(
    const SystemVerilogNetKind kind) noexcept
{
    using Kind = SystemVerilogNetKind;
    switch (kind) {
    case Kind::wire: return "wire";
    case Kind::tri: return "tri";
    case Kind::wand: return "wand";
    case Kind::triand: return "triand";
    case Kind::wor: return "wor";
    case Kind::trior: return "trior";
    case Kind::tri0: return "tri0";
    case Kind::tri1: return "tri1";
    case Kind::trireg: return "trireg";
    case Kind::supply0: return "supply0";
    case Kind::supply1: return "supply1";
    case Kind::uwire: return "uwire";
    case Kind::none: break;
    }
    return { };
}

// byte, shortint, int, longint, integer, time: the integer atom types
// (6.11).
[[nodiscard]] constexpr bool systemverilog_integer_atom(
    const SystemVerilogBuiltinType kind) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    return kind == Kind::byte || kind == Kind::shortint
        || kind == Kind::int_type || kind == Kind::longint
        || kind == Kind::integer || kind == Kind::time;
}

// bit, logic, reg: the integer vector types (6.11).
[[nodiscard]] constexpr bool systemverilog_integer_vector(
    const SystemVerilogBuiltinType kind) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    return kind == Kind::bit || kind == Kind::logic || kind == Kind::reg;
}

// The integral built-in types (6.11.1).
[[nodiscard]] constexpr bool systemverilog_integral(
    const SystemVerilogBuiltinType kind) noexcept
{
    return systemverilog_integer_atom(kind)
        || systemverilog_integer_vector(kind);
}

// shortreal, real, realtime (6.12).
[[nodiscard]] constexpr bool systemverilog_real_family(
    const SystemVerilogBuiltinType kind) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    return kind == Kind::shortreal || kind == Kind::real
        || kind == Kind::realtime;
}

// The four-state integral types (logic, reg, integer, time; 6.11).
[[nodiscard]] constexpr bool systemverilog_four_state(
    const SystemVerilogBuiltinType kind) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    return kind == Kind::logic || kind == Kind::reg || kind == Kind::integer
        || kind == Kind::time;
}

// The width of an integer atom type (6.11, Table 6-8); 1 for an integer
// vector type without a range, 0 for every other kind.
[[nodiscard]] constexpr std::uint32_t systemverilog_builtin_width(
    const SystemVerilogBuiltinType kind) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    switch (kind) {
    case Kind::byte: return 8U;
    case Kind::shortint: return 16U;
    case Kind::int_type:
    case Kind::integer: return 32U;
    case Kind::longint:
    case Kind::time: return 64U;
    case Kind::bit:
    case Kind::logic:
    case Kind::reg: return 1U;
    default: break;
    }
    return 0U;
}

// Signed by default: byte, shortint, int, longint, integer (6.11.3).
[[nodiscard]] constexpr bool systemverilog_builtin_default_signed(
    const SystemVerilogBuiltinType kind) noexcept
{
    using Kind = SystemVerilogBuiltinType;
    return kind == Kind::byte || kind == Kind::shortint
        || kind == Kind::int_type || kind == Kind::longint
        || kind == Kind::integer;
}

// What the target of a cast `target'(value)` is (IEEE 1800-2023 6.24.1); the
// parser sets it with the cast, so no later stage reads the target's text.
enum class SystemVerilogCastTarget : std::uint8_t {
    none,          // not a cast
    builtin,       // a built-in data type keyword (its kind travels beside)
    signed_type,   // `signed'(...)`
    unsigned_type, // `unsigned'(...)`
    constant,      // `const'(...)`
    size,          // a constant size `N'(...)`
    named,         // a type name: typedef, class, enum or type parameter
    type_operator, // `type(...)'(...)` (6.23)
};

// A keyword of a data type's source tokens that names no built-in type
// (IEEE 1800-2023 6.8, 6.11.3); the parser marks it on the token.
enum class SystemVerilogTypeWord : std::uint8_t {
    none,
    signed_keyword,
    unsigned_keyword,
    const_keyword,
    var_keyword,
};

// A class of the built-in package `std` (IEEE 1800-2023 15.4 mailbox,
// 15.3 semaphore, 9.7 process, 8.30 weak_reference); the parser sets it
// where a type name resolves to the std declaration, so no later stage
// identifies these classes by spelling.
enum class SystemVerilogStandardClass : std::uint8_t {
    none,
    process,
    mailbox,
    semaphore,
    weak_reference,
};

// The class's identifier in package std (IEEE 1800-2023 9.7, 15.3, 15.4,
// 8.30), which names it in VPI (37.32); empty for `none`.
[[nodiscard]] constexpr std::string_view systemverilog_standard_class_name(
    const SystemVerilogStandardClass kind) noexcept
{
    switch (kind) {
    case SystemVerilogStandardClass::process:
        return "process";
    case SystemVerilogStandardClass::mailbox:
        return "mailbox";
    case SystemVerilogStandardClass::semaphore:
        return "semaphore";
    case SystemVerilogStandardClass::weak_reference:
        return "weak_reference";
    case SystemVerilogStandardClass::none:
        break;
    }
    return { };
}

}  // namespace fsim::support
