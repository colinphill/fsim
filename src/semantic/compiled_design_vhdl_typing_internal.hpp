// SPDX-License-Identifier: Apache-2.0
#pragma once

// Interfaces between the files of compile-time VHDL expression typing (gate
// BD build D4, IEEE 1076-2019 9.3 and 12.5). Frozen for a worker round: a
// change goes through the lead.
//
//   compiled_design_vhdl_typing_types.cpp       Types (5.1-5.7 type
//                                               structure, 9.3.6 closely
//                                               related types)
//   compiled_design_vhdl_typing_operations.cpp  Operations (the parameter and
//                                               result types of explicit
//                                               subprograms, of STD.STANDARD
//                                               functions, and of the
//                                               operations a type declaration
//                                               implicitly declares, 5.x, 9.2)
//   compiled_design_vhdl_typing_attributes.cpp  predefined attributes (16.2)
//   compiled_design_vhdl_typing.cpp             type_vhdl_expressions: the
//                                               12.5 resolution of every
//                                               complete context, storing,
//                                               diagnostics
//
// The typing runs after name analysis (compiled_design_vhdl_analysis*.cpp),
// over the same units, with the same Regions and Lookup: every candidate of
// an overloaded name, operator or character literal comes from Lookup, and
// every comparison of names is one of interned symbols. A STD.STANDARD
// declaration is identified by its support::StandardDeclaration value, a
// declared type by its TypeId, never by a spelling.

#include "compiled_design_vhdl_analysis_internal.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/symbol.hpp"
#include "fsim/support/vhdl_standard_declaration.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::semantic::vhdl_names {

/// A type (5.1): always a base type, never a subtype.
using Type = vhdl::TypeIdentity;

/// The class of a type (5.2-5.7).
enum class TypeClass : std::uint8_t {
    none,
    enumeration,
    integer,
    floating,
    physical,
    array,
    record,
    access,
    file,
    protected_type,
    universal_integer,
    universal_real,
};

/// The structure of the types of a design (compiled_design_vhdl_typing_types.cpp).
/// Declared types are read from vhdl::TypeDefinition (subtypes and aliases
/// followed to the base type); the types of the compiler-served STD.STANDARD
/// from their support::StandardDeclaration rows (16.3), with the class, base
/// type, element and index of each given by a table keyed by the
/// StandardDeclaration enumerators (never by the catalog's spellings).
class Types {
public:
    explicit Types(const Regions& regions);

    [[nodiscard]] const Regions& regions() const noexcept { return regions_; }

    /// The class of `type`; none for an invalid type.
    [[nodiscard]] TypeClass class_of(const Type& type) const;

    /// The base type a type or subtype declaration (DeclarationForm::type /
    /// subtype; an alias of a type) denotes (5.1, 6.3, 6.6.3).
    [[nodiscard]] Type of_type_declaration(const vhdl::Declaration& declaration) const;
    /// The base type of a subtype indication: its type mark's
    /// TypeReference::target, else the STANDARD declaration its written type
    /// mark denotes (Name::standard), else the declaration it selects (6.3).
    [[nodiscard]] Type of_subtype(const vhdl::SubtypeIndication& subtype) const;
    /// The base type of a STANDARD type or subtype row: NATURAL and POSITIVE
    /// denote INTEGER, DELAY_LENGTH denotes TIME (16.3); none for a row that
    /// is no type or subtype.
    [[nodiscard]] Type of_standard(support::StandardDeclaration declaration) const;
    /// The type of an object (constant, signal, variable, file, port,
    /// generic constant, object alias): the base type of its subtype
    /// indication; none for a declaration with none (a loop or generate
    /// parameter, typed by its range in the typing core).
    [[nodiscard]] Type of_object(const vhdl::Declaration& declaration) const;
    /// The result type of a function declaration; none for a procedure.
    [[nodiscard]] Type result_of(const vhdl::Declaration& function) const;
    /// The type of an enumeration literal declaration (its enumeration type).
    [[nodiscard]] Type of_literal(const vhdl::Declaration& literal) const;

    /// The number of index dimensions of an array type; 0 for any other.
    [[nodiscard]] std::size_t dimensions(const Type& array) const;
    /// The element base type of an array type; none for any other.
    [[nodiscard]] Type element(const Type& array) const;
    /// The base type of index `dimension` (0-based) of an array type.
    [[nodiscard]] Type index(const Type& array, std::size_t dimension) const;
    /// The base type of the record element named `element` (its symbol);
    /// none when the record has no such element.
    [[nodiscard]] Type record_element(const Type& record, support::SymbolId element) const;
    /// The designated base type of an access or file type.
    [[nodiscard]] Type designated(const Type& type) const;
    /// Whether enumeration type `type` has the literal whose designator
    /// symbol is `literal` (an identifier, or a character literal with its
    /// quotes, e.g. the symbol of `'X'`; 5.2.2.1). STANDARD CHARACTER has
    /// every character of the edition (128 in 1987, 256 later).
    [[nodiscard]] bool has_literal(const Type& type, support::SymbolId literal,
        std::uint16_t year) const;
    /// Whether a value of `operand` may be converted to `target` in
    /// edition `year` (9.3.6): the same type, abstract numeric types,
    /// closely related array types (same dimensionality; the same element
    /// type before 2008, closely related element types from 2008).
    [[nodiscard]] bool closely_related(const Type& target, const Type& operand,
        std::uint16_t year) const;
    /// Whether `type` is a one-dimensional array type whose element type is
    /// an enumeration type with character literals (a string or bit string
    /// literal may have it, 9.3.2).
    [[nodiscard]] bool string_type(const Type& type) const;

    /// The STANDARD types.
    [[nodiscard]] static Type standard(support::StandardDeclaration type) noexcept
    {
        Type result;
        result.kind = Type::Kind::standard;
        result.standard = type;
        return result;
    }
    [[nodiscard]] static Type universal_integer() noexcept
    {
        Type result;
        result.kind = Type::Kind::universal_integer;
        return result;
    }
    [[nodiscard]] static Type universal_real() noexcept
    {
        Type result;
        result.kind = Type::Kind::universal_real;
        return result;
    }
    [[nodiscard]] static Type declared(TypeId type) noexcept
    {
        Type result;
        result.kind = Type::Kind::declared;
        result.declared = type;
        return result;
    }

    /// The type as a diagnostic names it (its declared name, e.g. INTEGER,
    /// STD_ULOGIC_VECTOR, universal_integer).
    [[nodiscard]] std::string describe(const Type& type) const;

private:
    const Regions& regions_;
};

/// One subprogram profile (4.2.2, 4.5.2): of an explicit subprogram or
/// enumeration literal (a parameterless function, 5.2.2.1), of a STANDARD
/// function row, or of an operation a type declaration implicitly declares.
struct Profile {
    enum class Source : std::uint8_t { declaration, standard, implicit };
    Source source { Source::declaration };
    /// declaration: the subprogram or enumeration literal; implicit: the
    /// type declaration that declares the operation.
    DeclarationId declaration;
    /// standard: the function row.
    support::StandardDeclaration standard { support::StandardDeclaration::none };
    /// implicit: the operation.
    vhdl::PredefinedOperation operation { vhdl::PredefinedOperation::none };
    /// The parameter base types, in order.
    std::vector<Type> parameters;
    /// The formal designators (named association, 6.5.7.1), parallel to
    /// `parameters`; invalid for an anonymous formal.
    std::vector<support::SymbolId> formal_names;
    /// declaration: the formal declarations, parallel to `parameters`.
    std::vector<DeclarationId> formals;
    /// Whether each formal has a default value (an actual may be omitted).
    std::vector<bool> defaults;
    /// The result base type; none for a procedure.
    Type result;
};

/// The profiles of the operations of a design (compiled_design_vhdl_typing_operations.cpp).
class Operations {
public:
    explicit Operations(const Types& types);

    /// The profile of an explicit subprogram declaration or of an
    /// enumeration literal declaration.
    [[nodiscard]] Profile of_declaration(const vhdl::Declaration& declaration) const;
    /// The profile of a STANDARD function row (16.3; catalog parameter and
    /// result types, universal_integer and universal_real included).
    [[nodiscard]] Profile of_standard(support::StandardDeclaration function) const;
    /// The operations designated `designator` that type declaration
    /// `type_declaration` implicitly declares in edition `year` (5.2.6,
    /// 5.3.2.4, 5.4.3, 5.5.2, 5.6, 5.7, 9.2): relational and matching
    /// operators, arithmetic, logical and reduction, shift and
    /// concatenation operators, MINIMUM, MAXIMUM, TO_STRING, DEALLOCATE,
    /// and the file operations, each with its operation identity.
    [[nodiscard]] std::vector<Profile> implicit(const vhdl::Declaration& type_declaration,
        support::SymbolId designator, std::uint16_t year) const;

    [[nodiscard]] const Types& types() const noexcept { return types_; }

private:
    const Types& types_;
};

/// What the prefix of an attribute name is (16.2.1).
enum class AttributePrefix : std::uint8_t {
    /// A type or subtype mark (T'LEFT, T'IMAGE(X), T'POS(X)).
    type_mark,
    /// An object, or the name of an array object (A'LENGTH, A'RANGE(2)).
    object,
    /// A signal (S'EVENT, S'DELAYED(T)).
    signal,
    /// Any other named entity (E'SIMPLE_NAME, E'PATH_NAME).
    named_entity,
};

/// What the argument of a function-like attribute must be (16.2).
enum class AttributeArgument : std::uint8_t {
    /// No argument.
    none,
    /// A locally static universal_integer dimension number (A'LENGTH(N)).
    dimension,
    /// A value of the prefix's base type (T'IMAGE(X), T'SUCC(X)).
    prefix_base,
    /// A value of any integer type (T'VAL(X)).
    any_integer,
    /// A STRING (T'VALUE(X)).
    string,
    /// A TIME (S'DELAYED(T), S'STABLE(T), S'QUIET(T)).
    time,
};

/// What a predefined attribute name denotes and its type (16.2).
struct AttributeProfile {
    vhdl::PredefinedAttribute attribute { };
    AttributeArgument argument { AttributeArgument::none };
    /// Whether the argument may be omitted (A'LENGTH, S'DELAYED).
    bool argument_optional { };
    /// The value's base type; none when the attribute denotes no value.
    Type result;
    /// The attribute denotes a range ('RANGE, 'REVERSE_RANGE), a subtype
    /// ('BASE, 'SUBTYPE, 'ELEMENT, 'INDEX, 'DESIGNATED_SUBTYPE), or a signal
    /// ('DELAYED, 'STABLE, 'QUIET, 'TRANSACTION).
    bool range { };
    bool subtype { };
    bool signal { };
};

/// The predefined attribute whose designator symbol is `designator` in
/// edition `year` (16.2; 'BEHAVIOR and 'STRUCTURE only in 1987, the
/// 2008/2019 additions only from their edition); none otherwise (a
/// user-defined attribute).
[[nodiscard]] std::optional<vhdl::PredefinedAttribute> predefined_attribute(
    support::SymbolId designator, std::uint16_t year);

/// The profile of `attribute` applied to a prefix of kind `prefix` whose
/// base type is `prefix_type` (none for a named entity) in edition `year`;
/// none when 16.2 does not define the attribute for such a prefix (an
/// error the caller reports). For an array prefix with a dimension
/// argument the result is that of dimension `dimension` (0-based).
[[nodiscard]] std::optional<AttributeProfile> attribute_profile(const Types& types,
    vhdl::PredefinedAttribute attribute, AttributePrefix prefix,
    const Type& prefix_type, std::size_t dimension, std::uint16_t year);

/// The edition year of the unit holding `scope` (1987, 1993, 2000, 2002,
/// 2008, 2019); 0 when unknown.
[[nodiscard]] std::uint16_t edition_of(const Regions& regions, ScopeId scope);

/// Types every expression of the VHDL units `units` (12.5): each complete
/// context once, recording on each node its type, interpretation and the
/// declaration, STANDARD row, implicit operation or attribute it denotes,
/// and the formals of each call's actuals. Reports FSIM-VHDL-TYPE-001 (no
/// interpretation fits), -002 (more than one does) and -003 (the context
/// does not determine the type of a literal, aggregate or null). Returns
/// false when it reported an error.
[[nodiscard]] bool type_vhdl_expressions(CompiledDesign& design,
    const Regions& regions, const Lookup& lookup, std::span<const UnitId> units,
    diagnostic::Engine& diagnostics);

} // namespace fsim::semantic::vhdl_names
