// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fsim::runtime {

// The vpiType of a scope that is an instance of another language's design
// unit, such as a VHDL entity in a mixed design. It is outside the object
// types of IEEE 1800-2023 Annex K, as a tool-defined type.
inline constexpr std::int32_t fsim_vpi_foreign_instance_type = 1000;

enum class SystemVerilogVpiLanguage {
    Verilog2005 = 0,
    SystemVerilog2017 = 1,
    Verilog1995 = 2,
    Verilog2001 = 3,
    Verilog2001NoConfig = 4,
    SystemVerilog2005 = 5,
    SystemVerilog2009 = 6,
    SystemVerilog2012 = 7,
};

enum class SystemVerilogVpiValueCategory {
    None,
    Bit2,
    Logic4,
    Logic9,
    Integer2,
    Integer4,
    Real,
    ShortReal,
    Time,
    String,
    Event,
};

enum class SystemVerilogVpiNetKind {
    None,
    Wire,
    Tri,
    Wand,
    Wor,
    Tri0,
    Tri1,
    Supply0,
    Supply1,
    Uwire,
    TriReg,
    TriAnd,
    TriOr,
};

// The `unconnected_drive in effect for a module (IEEE 1800-2023 22.9).
enum class SystemVerilogVpiUnconnectedDrive {
    None,
    Pull0,
    Pull1,
};

enum class SystemVerilogVpiDirection {
    None,
    Input,
    Output,
    Inout,
};

enum class SystemVerilogVpiLifetime {
    Static,
    Automatic,
};

enum class SystemVerilogVpiDescriptorKind {
    Scalar,
    PackedArray,
    UnpackedArray,
    DynamicArray,
    Queue,
    AssociativeArray,
    Struct,
    Union,
    Enum,
    String,
    Class,
    ClassHandle,
};

struct SystemVerilogVpiRange {
    std::int64_t left { };
    std::int64_t right { };

    friend bool operator==(
        const SystemVerilogVpiRange&,
        const SystemVerilogVpiRange&) = default;
};

struct SystemVerilogVpiEnumLiteral {
    std::string name;
    PackedLogic4 value;

    friend bool operator==(
        const SystemVerilogVpiEnumLiteral&,
        const SystemVerilogVpiEnumLiteral&) = default;
};

struct SystemVerilogVpiTypeDescriptor {
    SystemVerilogVpiDescriptorKind kind {
        SystemVerilogVpiDescriptorKind::Scalar
    };
    SystemVerilogVpiValueCategory category {
        SystemVerilogVpiValueCategory::Logic4
    };
    std::uint32_t width { 1 };
    bool is_signed { };
    std::string nominal_name;
    std::vector<SystemVerilogVpiRange> ranges;
    std::optional<std::uint64_t> maximum_size;
    std::vector<SystemVerilogVpiTypeDescriptor> children;
    std::vector<std::string> member_names;
    std::vector<SystemVerilogVpiEnumLiteral> enum_literals;
    // A packed structure or union (IEEE 1800-2023 7.2.1, 7.3.1).
    bool packed { };

    friend bool operator==(
        const SystemVerilogVpiTypeDescriptor&,
        const SystemVerilogVpiTypeDescriptor&) = default;
};

// The vpiType of a typespec (IEEE 1800-2023 37.25).
enum class SystemVerilogVpiTypespecKind {
    Logic,
    Bit,
    Byte,
    ShortInt,
    Int,
    LongInt,
    Integer,
    Time,
    Real,
    ShortReal,
    String,
    Event,
    Chandle,
    Enum,
    Struct,
    Union,
    PackedArray,
    Array,
    Class,
};

// The vpiArrayType of an array typespec (IEEE 1800-2023 37.25).
enum class SystemVerilogVpiArrayKind {
    Static,
    Dynamic,
    Associative,
    Queue,
};

// The data type of a declaration, one typedef level per node (IEEE
// 1800-2023 37.25): `ranges` are this level's dimensions, leftmost first,
// and `element` (at most one) is what they repeat, or an enumeration's base
// type. `name` is the typedef's, empty for a built-in or inline type, and
// `alias` (at most one) is the type a typedef of a typedef or of a built-in
// type names (vpiTypedefAlias).
struct SystemVerilogVpiTypespec {
    SystemVerilogVpiTypespecKind kind { SystemVerilogVpiTypespecKind::Logic };
    std::string name;
    bool is_signed { };
    bool packed { };
    bool tagged { };
    SystemVerilogVpiArrayKind array_kind { SystemVerilogVpiArrayKind::Static };
    std::vector<SystemVerilogVpiRange> ranges;
    std::vector<SystemVerilogVpiTypespec> element;
    std::vector<SystemVerilogVpiTypespec> alias;
    std::vector<std::string> member_names;
    std::vector<SystemVerilogVpiTypespec> members;
    std::vector<SystemVerilogVpiEnumLiteral> enum_constants;
    // The key type of an associative array (at most one element; none for
    // the wildcard index, IEEE 1800-2023 37.25 detail 2 vpiIndexTypespec).
    std::vector<SystemVerilogVpiTypespec> index;

    friend bool operator==(
        const SystemVerilogVpiTypespec&,
        const SystemVerilogVpiTypespec&) = default;
};

struct SystemVerilogVpiDriverRange {
    std::uint32_t offset { };
    std::uint32_t width { };
    bool whole { };

    friend bool operator==(
        const SystemVerilogVpiDriverRange&,
        const SystemVerilogVpiDriverRange&) = default;
};

struct SystemVerilogVpiTypeInfo {
    SystemVerilogVpiLanguage language {
        SystemVerilogVpiLanguage::SystemVerilog2017
    };
    SystemVerilogVpiValueCategory category {
        SystemVerilogVpiValueCategory::None
    };
    SystemVerilogVpiNetKind net_kind { SystemVerilogVpiNetKind::None };
    SystemVerilogVpiDirection direction { SystemVerilogVpiDirection::None };
    SystemVerilogVpiLifetime lifetime { SystemVerilogVpiLifetime::Static };
    std::uint32_t width { };
    bool is_signed { };
    bool is_constant { };
    // A parameter whose value is a string literal (IEEE 1800-2023 37.32
    // vpiStringConst).
    bool string_literal { };
    // A `celldefine module instance (IEEE 1800-2023 37.10 vpiCellInstance).
    bool is_cell { };
    // A local parameter (IEEE 1800-2023 6.20.4, 37.28 vpiLocalParam).
    bool local_parameter { };
    std::shared_ptr<const SystemVerilogVpiTypeDescriptor> descriptor;
    // The declared data type of a variable or net stored as one packed
    // value: a structure, union or enumeration, or a packed array of them
    // (IEEE 1800-2023 37.17, 37.18, 37.26). It gives the object's VPI type
    // and members; `descriptor` stays the storage shape.
    std::shared_ptr<const SystemVerilogVpiTypeDescriptor> declared;
    // The declaration's typespec (IEEE 1800-2023 37.25 vpiTypespec).
    std::shared_ptr<const SystemVerilogVpiTypespec> typespec;
    std::optional<SystemVerilogVpiDriverRange> driver_range;
    std::optional<std::uint32_t> semantic_unit_id;
    std::optional<std::uint32_t> source_id;
    std::string semantic_unit;
    std::string source_path;
    std::uint32_t source_line { };
    std::uint32_t source_column { };
    // Where an instance's module, interface or program is defined (IEEE
    // 1800-2023 37.10 vpiDefFile, vpiDefLineNo).
    std::string definition_path;
    std::uint32_t definition_line { };
    // A gate primitive's type name, such as `and` (IEEE 1800-2023 37.7
    // vpiDefName, vpiPrimType).
    std::string definition_name;
    // An instance of a VHDL design unit in a mixed design, or a generate
    // scope of its architecture. IEEE 1800-2023 has no object type for the
    // instance, so its vpiType is `fsim_vpi_foreign_instance_type`; the
    // names of either are VHDL names.
    bool foreign_instance { };
    // An instance's `default_nettype, None for `default_nettype none, and
    // `unconnected_drive where its definition is (IEEE 1800-2023 22.8,
    // 22.9, 37.10 vpiDefNetType, vpiUnconnDrive).
    SystemVerilogVpiNetKind default_net_kind { SystemVerilogVpiNetKind::Wire };
    SystemVerilogVpiUnconnectedDrive unconnected_drive {
        SystemVerilogVpiUnconnectedDrive::None
    };
    // An instance's generate blocks named by the implicit-name rule, as
    // '.'-separated block paths (IEEE 1800-2023 27.6, 37.85 detail 2).
    std::vector<std::string> implicit_generate_scopes;
    // An instance's implicitly declared nets (IEEE 1800-2023 6.10, 37.16
    // vpiImplicitDecl).
    std::vector<std::string> implicit_nets;
    std::string standard;
    std::string compatibility_profile;
};

} // namespace fsim::runtime
