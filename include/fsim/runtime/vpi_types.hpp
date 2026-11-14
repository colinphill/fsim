// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    // An interface or a modport of one, as the type of a virtual interface
    // (IEEE 1800-2023 37.30).
    Interface,
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
// A parameter value an interface typespec assigns: the actual of
// `virtual SBus#(16)` for the interface's parameter `name` (IEEE 1800-2023
// 37.28, 37.30).
struct SystemVerilogVpiParamAssign {
    std::string name;
    std::int64_t value { };

    friend bool operator==(const SystemVerilogVpiParamAssign&,
        const SystemVerilogVpiParamAssign&) = default;
};

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
    // An interface typespec's vpiDefName: the interface declaration's
    // identifier, or the modport's for a modport typespec, whose `element`
    // then holds the interface's typespec (its vpiParent, 37.30 details 1
    // and 2).
    std::string def_name;
    // The parameter values an interface typespec's actuals assign, in the
    // interface's parameter order (IEEE 1800-2023 37.30 vpiParamAssign); on
    // the interface level of a modport typespec's `element`.
    std::vector<SystemVerilogVpiParamAssign> param_assigns;
    // A class typespec's class: the identity of the specialization, or of
    // the declaration of a class with no parameters (IEEE 1800-2023 8.25);
    // empty for a built-in class (37.32 detail 13).
    std::string class_identity;

    friend bool operator==(
        const SystemVerilogVpiTypespec&,
        const SystemVerilogVpiTypespec&) = default;
};

// The typespec of `virtual interface_name.modport` (IEEE 1800-2023 37.30);
// `name` is the typedef's when the type was named through one.
[[nodiscard]] inline SystemVerilogVpiTypespec
make_systemverilog_vpi_interface_typespec(const std::string_view interface_name,
    const std::string_view modport, const std::string_view name,
    std::vector<SystemVerilogVpiParamAssign> param_assigns = { })
{
    SystemVerilogVpiTypespec interface;
    interface.kind = SystemVerilogVpiTypespecKind::Interface;
    interface.def_name = std::string { interface_name };
    interface.param_assigns = std::move(param_assigns);
    if (modport.empty()) {
        interface.name = std::string { name };
        return interface;
    }
    SystemVerilogVpiTypespec result;
    result.kind = SystemVerilogVpiTypespecKind::Interface;
    result.name = std::string { name };
    result.def_name = std::string { modport };
    result.element.push_back(std::move(interface));
    return result;
}

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
    // A net or net array declared `interconnect` (IEEE 1800-2023 6.6.8,
    // 37.24); `net_kind` is the kind of the net it was merged with.
    bool interconnect { };
    // The user-defined nettype a net is declared with, empty for a net of a
    // built-in kind (IEEE 1800-2023 6.6.7, 37.16 detail 11).
    std::string nettype;
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
    // The library.cell name of the configuration that controls the binding
    // of an instance, empty when none does (IEEE 1800-2023 33.7 vpiConfig).
    std::string configuration;
    // A task or function declared by a DPI import or named by a DPI export
    // (IEEE 1800-2023 37.40 details 6-10): vpiAccessType, vpiDPIPure,
    // vpiDPIContext, vpiDPICStr, vpiDPICIdentifier, and for an import the
    // vpiFuncType, vpiSize and vpiSigned of its result.
    enum class DpiAccess : std::uint8_t { None, Import, Export };
    DpiAccess dpi_access { DpiAccess::None };
    bool dpi_pure { };
    bool dpi_context { };
    // The deprecated "DPI" link string (vpiDPI) rather than "DPI-C".
    bool dpi_legacy_string { };
    std::string dpi_c_identifier;
    std::int32_t dpi_func_type { };
    std::int64_t dpi_size { };
    bool dpi_signed { };
    // Where an import's declaration is, so its formals and result variable
    // can be read from the HIR: the owning unit's id and its index in that
    // unit's DPI declarations (IEEE 1800-2023 37.41 vpiIODecl, vpiReturn).
    bool dpi_has_declaration { };
    std::uint32_t dpi_unit { };
    std::uint32_t dpi_index { };
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
    // The argument of the `default_decay_time directive where the definition
    // is (IEEE 1800-2023 E.2, vpiDefDecayTime); empty when none.
    std::string default_decay_time;
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
