// SPDX-License-Identifier: Apache-2.0
#include "compiled_design_vhdl_typing_internal.hpp"

#include "fsim/project/project.hpp"
#include "fsim/semantic/vhdl_standard_catalog.hpp"
#include "fsim/semantic/vhdl_standard_declaration.hpp"
#include "fsim/support/vhdl_standard_enumeration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// The structure of the types of a design (gate BD build D4, IEEE 1076-2019
// 5.1-5.7, 9.3.2, 9.3.6, 16.3). A declared type is the TypeDefinition of its
// base type (subtypes and type aliases followed); a type of the compiler-
// served STD.STANDARD is its support::StandardDeclaration row. No type is
// looked up by spelling: names are interned symbols, STANDARD rows are
// enumerators, declared types are TypeIds.

namespace fsim::semantic::vhdl_names {

namespace {

using SD = support::StandardDeclaration;

// A chain of subtypes, aliases and names that is longer than this is a cycle
// (a subtype of itself, 6.3): the analysis has already refused such a design.
constexpr int chain_limit = 256;

// 16.3: the class, base type, element and index subtype of a STANDARD type or
// subtype row, keyed by enumerator. The element and index texts of
// vhdl_standard_catalog() are the same facts (STRING: CHARACTER / POSITIVE,
// BIT_VECTOR: BIT / NATURAL, BOOLEAN_VECTOR: BOOLEAN / NATURAL, INTEGER_VECTOR:
// INTEGER / NATURAL, REAL_VECTOR: REAL / NATURAL, TIME_VECTOR: TIME / NATURAL);
// they are not read at run time.
struct StandardRow {
    TypeClass type_class { TypeClass::none };
    // The base type: the row itself for a type; INTEGER for the subtypes
    // NATURAL and POSITIVE, TIME for DELAY_LENGTH (16.3).
    SD base { SD::none };
    SD element { SD::none };
    SD index { SD::none };
};

[[nodiscard]] StandardRow standard_row(const SD row) noexcept
{
    constexpr auto enumeration = TypeClass::enumeration;
    switch (row) {
    case SD::boolean:
    case SD::bit:
    case SD::character:
    case SD::severity_level:
    case SD::file_open_kind:
    case SD::file_open_status:
    case SD::file_open_state:
    case SD::file_origin_kind:
    case SD::range_direction:
        return { enumeration, row, SD::none, SD::none };
    case SD::integer:
        return { TypeClass::integer, SD::integer, SD::none, SD::none };
    case SD::natural:
    case SD::positive:
        return { TypeClass::integer, SD::integer, SD::none, SD::none };
    case SD::real:
        return { TypeClass::floating, SD::real, SD::none, SD::none };
    case SD::time:
        return { TypeClass::physical, SD::time, SD::none, SD::none };
    case SD::delay_length:
        return { TypeClass::physical, SD::time, SD::none, SD::none };
    case SD::string:
        return { TypeClass::array, SD::string, SD::character, SD::positive };
    case SD::bit_vector:
        return { TypeClass::array, SD::bit_vector, SD::bit, SD::natural };
    case SD::boolean_vector:
        return { TypeClass::array, SD::boolean_vector, SD::boolean, SD::natural };
    case SD::integer_vector:
        return { TypeClass::array, SD::integer_vector, SD::integer, SD::natural };
    case SD::real_vector:
        return { TypeClass::array, SD::real_vector, SD::real, SD::natural };
    case SD::time_vector:
        return { TypeClass::array, SD::time_vector, SD::time, SD::natural };
    default:
        return { };
    }
}

[[nodiscard]] bool valid_year(const std::uint16_t year) noexcept
{
    return year == 1987U || year == 1993U || year == 2000U || year == 2002U
        || year == 2008U || year == 2019U;
}

// The literal symbols of the enumeration types of STANDARD (16.3), interned
// once. CHARACTER (16.3): 1987 has the 128 characters of ISO 646 (control
// characters as identifiers NUL..USP and DEL, graphic characters as character
// literals); 1993 and later the 256 characters of ISO 8859-1, adding the
// identifiers C128..C159 and the graphic characters 160..255. A character
// literal's symbol is its quoted text (support::vhdl_symbol keeps the case),
// one byte for the character, as the lexer produces it.
struct StandardLiterals {
    std::unordered_map<std::uint16_t, std::unordered_set<std::uint32_t>> by_type;
    std::unordered_set<std::uint32_t> character_1987;
    std::unordered_set<std::uint32_t> character_1993;
    std::uint32_t file_open_kind_2019 { };
};

[[nodiscard]] StandardLiterals build_standard_literals()
{
    namespace detail = support::vhdl_standard_enumeration_detail;
    StandardLiterals result;
    const auto add_all = [&](const SD row, const auto& literals) {
        auto& set = result.by_type[static_cast<std::uint16_t>(row)];
        for (const auto literal : literals) {
            set.insert(support::vhdl_symbol(literal).index());
        }
    };
    add_all(SD::boolean, std::array<std::string_view, 2U> { "false", "true" });
    add_all(SD::bit, std::array<std::string_view, 2U> { "'0'", "'1'" });
    add_all(SD::severity_level, detail::severity_level);
    add_all(SD::file_open_kind, detail::file_open_kind);
    add_all(SD::file_open_status, detail::file_open_status);
    add_all(SD::range_direction, detail::range_direction);
    // 2019 16.3: FILE_ORIGIN_KIND and FILE_OPEN_STATE.
    add_all(SD::file_origin_kind,
        std::array<std::string_view, 3U> {
            "file_origin_begin", "file_origin_current", "file_origin_end" });
    add_all(SD::file_open_state,
        std::array<std::string_view, 2U> { "state_open", "state_closed" });
    // 2019 16.3: FILE_OPEN_KIND's fourth literal (position 3); the editions
    // before 2019 have three (standard_has_literal).
    result.file_open_kind_2019 = support::vhdl_symbol("read_write_mode").index();

    const auto add_character = [&](const unsigned code, const bool in_1987) {
        std::string text;
        if (code < detail::character_controls.size()) {
            text = std::string { detail::character_controls[code] };
        } else if (code == 127U) {
            text = "del";
        } else if (code >= 128U && code <= 159U) {
            text = "c" + std::to_string(code);
        } else {
            text = std::string { '\'', static_cast<char>(code), '\'' };
        }
        const auto symbol = support::vhdl_symbol(text).index();
        result.character_1993.insert(symbol);
        if (in_1987) {
            result.character_1987.insert(symbol);
        }
    };
    for (unsigned code = 0U; code < 256U; ++code) {
        add_character(code, code < 128U);
    }
    return result;
}

[[nodiscard]] const StandardLiterals& standard_literals()
{
    static const StandardLiterals literals = build_standard_literals();
    return literals;
}

[[nodiscard]] bool standard_has_literal(const SD type,
    const support::SymbolId literal, const std::uint16_t year)
{
    if (!support::available(type, year)) {
        return false;
    }
    const auto& tables = standard_literals();
    if (type == SD::character) {
        const auto& set = year == 1987U ? tables.character_1987
                                        : tables.character_1993;
        return set.contains(literal.index());
    }
    if (type == SD::file_open_kind && year >= 2019U
        && literal.index() == tables.file_open_kind_2019) {
        return true;
    }
    const auto found = tables.by_type.find(static_cast<std::uint16_t>(type));
    return found != tables.by_type.end() && found->second.contains(literal.index());
}

[[nodiscard]] bool object_form(const vhdl::DeclarationForm form) noexcept
{
    switch (form) {
    case vhdl::DeclarationForm::constant:
    case vhdl::DeclarationForm::signal:
    case vhdl::DeclarationForm::variable:
    case vhdl::DeclarationForm::file:
    case vhdl::DeclarationForm::port:
    case vhdl::DeclarationForm::generic_constant:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool numeric(const TypeClass type_class) noexcept
{
    return type_class == TypeClass::integer || type_class == TypeClass::floating
        || type_class == TypeClass::universal_integer
        || type_class == TypeClass::universal_real;
}

// The declarations and type definitions of the design, by id.
[[nodiscard]] const vhdl::TypeDefinition& definition_of(
    const Regions& regions, const TypeId id)
{
    const auto view = regions.design().find_type(id);
    if (!view || view->vhdl == nullptr) {
        throw std::logic_error { "VHDL typing: type " + std::to_string(id.value())
            + " is not a VHDL type of the design" };
    }
    return *view->vhdl;
}

[[nodiscard]] const vhdl::Declaration& declaration_of(
    const Regions& regions, const DeclarationId id)
{
    const auto view = regions.design().find_declaration(id);
    if (!view || view->vhdl == nullptr) {
        throw std::logic_error { "VHDL typing: declaration "
            + std::to_string(id.value())
            + " is not a VHDL declaration of the design" };
    }
    return *view->vhdl;
}

// Following subtypes, aliases and names to base types (6.3, 6.6.3).
class Resolver {
public:
    explicit Resolver(const Regions& regions) noexcept
        : regions_ { regions }
    {
    }

    // 5.1: a type definition's base type: a subtype or alias (not the
    // declaration of an integer or floating type, `type T is range ...`, which
    // declares a distinct type kept as a subtype of its predefined base,
    // 5.2.3.1, 5.2.5.1) denotes the base type of its subtype indication.
    [[nodiscard]] Type from_definition(const vhdl::TypeDefinition& definition,
        const int depth) const
    {
        check(depth);
        const bool follows = (definition.form == vhdl::TypeForm::subtype
                                 || definition.form == vhdl::TypeForm::alias)
            && !definition.scalar_type;
        if (!follows) {
            return Types::declared(definition.id);
        }
        return from_subtype(definition.base, depth + 1);
    }

    // 6.3: the type mark's target, else the STANDARD declaration its written
    // name denotes, else the declaration it selects; none when name analysis
    // left it undetermined.
    [[nodiscard]] Type from_subtype(const vhdl::SubtypeIndication& subtype,
        const int depth) const
    {
        check(depth);
        // The written mark D3 resolved where it is written (12.3, 12.4: a
        // use clause later in the region is not yet in force) denotes the
        // type; the HIR builder's target is found through every use clause
        // of the unit's context, whatever its position.
        // A mark whose stored result is an object prefix (selected_parts,
        // `rec.f'subtype`) denotes an element of it, not the prefix itself.
        // Where both denote the same declaration the target is taken: it
        // names the definition directly, so a chain of subtypes is not
        // walked one declaration at a time (no fixed depth, 5.2.2).
        const auto target_view = subtype.type_mark.target.valid()
            ? regions_.design().find_type(subtype.type_mark.target)
            : std::nullopt;
        const auto target_declaration = target_view && target_view->vhdl != nullptr
            ? std::optional { target_view->vhdl->declaration }
            : std::nullopt;
        const auto disagrees = [&](const vhdl::Name& mark) {
            if (!target_declaration) {
                return true;
            }
            if (mark.standard != SD::none) {
                return target_declaration->valid();
            }
            return !target_declaration->valid() || *target_declaration != *mark.selected;
        };
        if (subtype.written_type_mark && subtype.written_type_mark->analyzed
            && subtype.written_type_mark->selected_parts == 0U
            && (subtype.written_type_mark->standard != SD::none
                || subtype.written_type_mark->selected)
            && disagrees(*subtype.written_type_mark)) {
            const auto& mark = *subtype.written_type_mark;
            if (mark.selected && object_form(
                    declaration_of(regions_, *mark.selected).form)) {
                return from_object(declaration_of(regions_, *mark.selected), depth + 1);
            }
            if (const auto type = from_name(mark, depth + 1)) {
                return type;
            }
        }
        if (subtype.type_mark.target.valid()) {
            // A target outside the VHDL type definitions (an interface
            // type's placeholder, 6.5.3) is named by the written type mark.
            const auto view = regions_.design().find_type(subtype.type_mark.target);
            if (view && view->vhdl != nullptr) {
                return from_definition(*view->vhdl, depth + 1);
            }
        }
        if (!subtype.written_type_mark) {
            return { };
        }
        const auto& mark = *subtype.written_type_mark;
        if (mark.selected && object_form(
                declaration_of(regions_, *mark.selected).form)) {
            // O'SUBTYPE (16.2): the type mark is an object.
            return from_object(declaration_of(regions_, *mark.selected), depth + 1);
        }
        return from_name(mark, depth + 1);
    }

    [[nodiscard]] Type from_name(const vhdl::Name& name, const int depth) const
    {
        check(depth);
        if (name.standard != SD::none) {
            return from_standard(name.standard);
        }
        if (name.selected) {
            return from_declaration(
                declaration_of(regions_, *name.selected), depth + 1);
        }
        return { };
    }

    [[nodiscard]] static Type from_standard(const SD row) noexcept
    {
        const auto info = standard_row(row);
        return info.type_class == TypeClass::none ? Type { }
                                                  : Types::standard(info.base);
    }

    // A type or subtype declaration, or an alias of a type.
    [[nodiscard]] Type from_declaration(const vhdl::Declaration& declaration,
        const int depth) const
    {
        check(depth);
        switch (declaration.form) {
        case vhdl::DeclarationForm::type:
        case vhdl::DeclarationForm::subtype:
        case vhdl::DeclarationForm::generic_type:
            if (!declaration.declared_type) {
                return { };
            }
            if (const auto view = regions_.design().find_type(*declaration.declared_type);
                view && view->vhdl != nullptr) {
                return from_definition(*view->vhdl, depth + 1);
            }
            // An interface type (6.5.3) has no type definition: its type is
            // not known before instantiation.
            return { };
        case vhdl::DeclarationForm::alias:
            if (!declaration.alias_target) {
                return { };
            }
            return from_name(*declaration.alias_target, depth + 1);
        default:
            return { };
        }
    }

    // 6.4.2, 6.5: an object's base type; an object alias without a subtype
    // (6.6.2) has the type of the object it names.
    [[nodiscard]] Type from_object(const vhdl::Declaration& declaration,
        const int depth) const
    {
        check(depth);
        if (declaration.form == vhdl::DeclarationForm::alias) {
            if (declaration.subtype) {
                return from_subtype(*declaration.subtype, depth + 1);
            }
            if (declaration.alias_target && declaration.alias_target->selected) {
                const auto& target
                    = declaration_of(regions_, *declaration.alias_target->selected);
                if (object_form(target.form)
                    || target.form == vhdl::DeclarationForm::alias) {
                    return from_object(target, depth + 1);
                }
            }
            return { };
        }
        if (!object_form(declaration.form)) {
            return { };
        }
        if (declaration.subtype) {
            return from_subtype(*declaration.subtype, depth + 1);
        }
        if (declaration.form == vhdl::DeclarationForm::constant) {
            return of_physical_unit(declaration);
        }
        return { };
    }

private:
    static void check(const int depth)
    {
        if (depth > chain_limit) {
            throw std::logic_error { "VHDL typing: a subtype, alias or type "
                                     "mark refers to itself" };
        }
    }

    // 5.2.4.1: a physical unit is declared as a constant of its physical
    // type, with no subtype of its own: the type whose definition lists it.
    [[nodiscard]] Type of_physical_unit(const vhdl::Declaration& unit) const
    {
        for (const auto& definition : regions_.design().vhdl_hir.types()) {
            for (const auto& listed : definition.physical_units) {
                if (listed.declaration == unit.id) {
                    return Types::declared(definition.id);
                }
            }
        }
        return { };
    }

    const Regions& regions_;
};

// 5.2.2.1: whether an enumeration type has character literals.
[[nodiscard]] bool has_character_literals(const Types& types, const Type& type)
{
    switch (type.kind) {
    case Type::Kind::standard:
        return type.standard == SD::bit || type.standard == SD::character;
    case Type::Kind::declared:
        for (const auto& literal :
            definition_of(types.regions(), type.declared).enumeration_literals) {
            if (!literal.spelling.empty() && literal.spelling.front() == '\'') {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

constexpr int closely_related_limit = 64;

// 9.3.6.
[[nodiscard]] bool related(const Types& types, const Type& target,
    const Type& operand, const std::uint16_t year, const int depth)
{
    if (depth > closely_related_limit) {
        throw std::logic_error { "VHDL typing: closely related types nest "
                                 "without end" };
    }
    if (!target || !operand) {
        return false;
    }
    // A type is closely related to itself.
    if (target == operand) {
        return true;
    }
    const auto target_class = types.class_of(target);
    const auto operand_class = types.class_of(operand);
    // Abstract numeric types: any is closely related to any other.
    if (numeric(target_class) && numeric(operand_class)) {
        return true;
    }
    if (target_class == TypeClass::array && operand_class == TypeClass::array) {
        const auto dimensions = types.dimensions(target);
        if (dimensions == 0U || dimensions != types.dimensions(operand)) {
            return false;
        }
        const auto target_element = types.element(target);
        const auto operand_element = types.element(operand);
        if (year >= 2008U) {
            // 2008 and 2019 9.3.6: the element types are closely related.
            return related(types, target_element, operand_element, year, depth + 1);
        }
        // 1987-2002 (1993 7.3.5): the element types are the same and, for
        // each index position, the index types are the same or closely
        // related.
        if (!target_element || target_element != operand_element) {
            return false;
        }
        for (std::size_t dimension = 0U; dimension < dimensions; ++dimension) {
            const auto target_index = types.index(target, dimension);
            const auto operand_index = types.index(operand, dimension);
            // A dimension written as a discrete range (`array (1 to 5)`)
            // names no index type: it is the type of the range's bounds
            // (5.3.2.2), which only the expression typing core knows. An
            // index type that is not known here decides nothing (analysis
            // mode: unknown information reports nothing).
            if (!target_index || !operand_index) {
                continue;
            }
            if (!related(types, target_index, operand_index, year, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (target_class == TypeClass::record && operand_class == TypeClass::record
        && year >= 2008U) {
        // 2008 and 2019 9.3.6: each element has a matching element (9.2.3:
        // the same simple name) in the other type, with closely related types.
        const auto matched = [&](const Type& from, const Type& in,
                                 const bool from_target) {
            for (const auto& element :
                definition_of(types.regions(), from.declared).record_elements) {
                const auto other = types.record_element(
                    in, support::vhdl_symbol(element.name));
                const auto mine = types.record_element(
                    from, support::vhdl_symbol(element.name));
                if (!other
                    || !(from_target ? related(types, mine, other, year, depth + 1)
                                     : related(types, other, mine, year, depth + 1))) {
                    return false;
                }
            }
            return true;
        };
        return matched(target, operand, true) && matched(operand, target, false);
    }
    // No other types are closely related.
    return false;
}

} // namespace

Types::Types(const Regions& regions)
    : regions_ { regions }
{
}

TypeClass Types::class_of(const Type& type) const
{
    switch (type.kind) {
    case Type::Kind::none:
        return TypeClass::none;
    case Type::Kind::universal_integer:
        return TypeClass::universal_integer;
    case Type::Kind::universal_real:
        return TypeClass::universal_real;
    case Type::Kind::standard:
        return standard_row(type.standard).type_class;
    case Type::Kind::declared:
        break;
    }
    const auto& definition = definition_of(regions_, type.declared);
    switch (definition.form) {
    case vhdl::TypeForm::enumeration:
        return TypeClass::enumeration;
    case vhdl::TypeForm::array:
        return TypeClass::array;
    case vhdl::TypeForm::record:
        return TypeClass::record;
    case vhdl::TypeForm::access:
        return TypeClass::access;
    case vhdl::TypeForm::file:
        return TypeClass::file;
    case vhdl::TypeForm::protected_type:
    case vhdl::TypeForm::protected_body:
        return TypeClass::protected_type;
    case vhdl::TypeForm::physical:
        return TypeClass::physical;
    case vhdl::TypeForm::scalar:
    case vhdl::TypeForm::subtype:
    case vhdl::TypeForm::alias: {
        // A declared integer or floating type is kept as a subtype of its
        // predefined base (5.2.3.1, 5.2.5.1): its class is the base's.
        if (!definition.scalar_type && definition.form != vhdl::TypeForm::scalar) {
            // A plain subtype or alias is never a base type: from_definition
            // followed it.
            throw std::logic_error { "VHDL typing: class of a subtype "
                                     "that is not a base type" };
        }
        if (definition.scalar_type) {
            // 5.2.3.1, 5.2.5.1: the class the type definition gives (its
            // base is an anonymous type of that class, not INTEGER or REAL).
            return definition.floating_type ? TypeClass::floating : TypeClass::integer;
        }
        const auto base = Resolver { regions_ }.from_subtype(definition.base, 0);
        if (base == type) {
            throw std::logic_error { "VHDL typing: a type is its own base" };
        }
        return class_of(base);
    }
    case vhdl::TypeForm::unresolved:
        return TypeClass::none;
    }
    return TypeClass::none;
}

Type Types::of_type_declaration(const vhdl::Declaration& declaration) const
{
    return Resolver { regions_ }.from_declaration(declaration, 0);
}

Type Types::of_subtype(const vhdl::SubtypeIndication& subtype) const
{
    return Resolver { regions_ }.from_subtype(subtype, 0);
}

Type Types::of_standard(const support::StandardDeclaration declaration) const
{
    return Resolver::from_standard(declaration);
}

Type Types::of_object(const vhdl::Declaration& declaration) const
{
    return Resolver { regions_ }.from_object(declaration, 0);
}

Type Types::result_of(const vhdl::Declaration& function) const
{
    if (function.form == vhdl::DeclarationForm::alias) {
        // 6.6.3: an alias of a function has the profile its signature gives.
        if (function.alias_signature && function.alias_signature->result) {
            return of_subtype(*function.alias_signature->result);
        }
        return { };
    }
    if (!function.callable || !function.callable->function
        || !function.callable->return_type) {
        return { };
    }
    return of_subtype(*function.callable->return_type);
}

Type Types::of_literal(const vhdl::Declaration& literal) const
{
    if (literal.form != vhdl::DeclarationForm::enumeration_literal) {
        throw std::logic_error { "VHDL typing: declaration "
            + std::to_string(literal.id.value())
            + " is not an enumeration literal" };
    }
    // The builder gives a literal the subtype indication of its enumeration
    // type, whose type mark targets the type definition that lists it.
    if (!literal.subtype) {
        return { };
    }
    return of_subtype(*literal.subtype);
}

std::size_t Types::dimensions(const Type& array) const
{
    switch (array.kind) {
    case Type::Kind::standard:
        return standard_row(array.standard).type_class == TypeClass::array ? 1U : 0U;
    case Type::Kind::declared:
        if (class_of(array) != TypeClass::array) {
            return 0U;
        }
        return definition_of(regions_, array.declared).array_dimensions.size();
    default:
        return 0U;
    }
}

Type Types::element(const Type& array) const
{
    switch (array.kind) {
    case Type::Kind::standard: {
        const auto row = standard_row(array.standard);
        return row.type_class == TypeClass::array ? Resolver::from_standard(row.element)
                                                  : Type { };
    }
    case Type::Kind::declared: {
        if (class_of(array) != TypeClass::array) {
            return { };
        }
        const auto& definition = definition_of(regions_, array.declared);
        return definition.element_subtype ? of_subtype(*definition.element_subtype)
                                          : Type { };
    }
    default:
        return { };
    }
}

Type Types::index(const Type& array, const std::size_t dimension) const
{
    switch (array.kind) {
    case Type::Kind::standard: {
        const auto row = standard_row(array.standard);
        if (row.type_class != TypeClass::array || dimension != 0U) {
            return { };
        }
        return Resolver::from_standard(row.index);
    }
    case Type::Kind::declared: {
        if (class_of(array) != TypeClass::array) {
            return { };
        }
        const auto& ranges = definition_of(regions_, array.declared).array_dimensions;
        if (dimension >= ranges.size()) {
            return { };
        }
        // An index subtype name (`natural range <>`, a subtype or type name,
        // 5.3.2.1); a discrete range written in the array definition names
        // no type and is typed by its bounds (the typing core, 5.3.2.2).
        if (ranges[dimension].implied_index_subtype) {
            return { };
        }
        return Resolver { regions_ }.from_name(ranges[dimension].index_subtype, 0);
    }
    default:
        return { };
    }
}

Type Types::record_element(const Type& record, const support::SymbolId element) const
{
    if (record.kind != Type::Kind::declared || class_of(record) != TypeClass::record) {
        return { };
    }
    for (const auto& candidate : definition_of(regions_, record.declared).record_elements) {
        if (support::vhdl_symbol(candidate.name) == element) {
            return of_subtype(candidate.subtype);
        }
    }
    return { };
}

Type Types::designated(const Type& type) const
{
    if (type.kind != Type::Kind::declared) {
        return { };
    }
    const auto& definition = definition_of(regions_, type.declared);
    switch (class_of(type)) {
    case TypeClass::access:
        return definition.designated_subtype
            ? of_subtype(*definition.designated_subtype)
            : Type { };
    case TypeClass::file:
        // The builder keeps a file type's subtype in element_subtype.
        return definition.element_subtype ? of_subtype(*definition.element_subtype)
                                          : Type { };
    default:
        return { };
    }
}

bool Types::has_literal(const Type& type, const support::SymbolId literal,
    const std::uint16_t year) const
{
    if (!valid_year(year)) {
        throw std::logic_error { "VHDL typing: unknown VHDL edition "
            + std::to_string(year) };
    }
    switch (type.kind) {
    case Type::Kind::standard:
        return standard_has_literal(type.standard, literal, year);
    case Type::Kind::declared:
        if (class_of(type) != TypeClass::enumeration) {
            return false;
        }
        for (const auto& candidate :
            definition_of(regions_, type.declared).enumeration_literals) {
            if (support::vhdl_symbol(candidate.spelling) == literal) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

bool Types::closely_related(const Type& target, const Type& operand,
    const std::uint16_t year) const
{
    return related(*this, target, operand, year, 0);
}

bool Types::string_type(const Type& type) const
{
    if (dimensions(type) != 1U) {
        return false;
    }
    const auto component = element(type);
    return component && class_of(component) == TypeClass::enumeration
        && has_character_literals(*this, component);
}

std::string Types::describe(const Type& type) const
{
    switch (type.kind) {
    case Type::Kind::none:
        return "(no type)";
    case Type::Kind::universal_integer:
        return "universal_integer";
    case Type::Kind::universal_real:
        return "universal_real";
    case Type::Kind::standard:
        return vhdl_standard_catalog()[support::catalog_row(type.standard)].name;
    case Type::Kind::declared:
        return definition_of(regions_, type.declared).name;
    }
    return { };
}

std::uint16_t edition_of(const Regions& regions, const ScopeId scope)
{
    const auto* const unit = regions.unit_of(scope);
    if (unit == nullptr) {
        return 0U;
    }
    const auto standard = project::parse_vhdl_standard(unit->standard);
    return standard ? standard_year(*standard) : std::uint16_t { 0U };
}

} // namespace fsim::semantic::vhdl_names
