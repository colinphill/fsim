// SPDX-License-Identifier: Apache-2.0

// Predefined attributes (IEEE 1076-2019 16.2; 16.2.1-16.2.5 in the older
// editions): the designator -> attribute table per edition, and the
// profile (prefix kinds, argument, result type) of each attribute.

#include "compiled_design_vhdl_typing_internal.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace fsim::semantic::vhdl_names {
namespace {

using vhdl::PredefinedAttribute;
using support::StandardDeclaration;

struct Row {
    const char* spelling; // interned once, below
    PredefinedAttribute attribute;
    std::uint16_t since;
    std::uint16_t until; // last edition defining it (0 = still defined)
};

// 1076-1987 14.1: BASE, LEFT..LEFTOF, LEFT..LENGTH, RANGE, REVERSE_RANGE,
// the signal attributes (DELAYED, STABLE, QUIET, TRANSACTION, EVENT, ACTIVE,
// LAST_EVENT, LAST_ACTIVE, LAST_VALUE) and BEHAVIOR/STRUCTURE (removed by
// 1076-1993). 1076-1993 16.2 adds ASCENDING, IMAGE, VALUE, DRIVING,
// DRIVING_VALUE, SIMPLE_NAME, INSTANCE_NAME, PATH_NAME; 1076-2008 adds
// SUBTYPE and ELEMENT; 1076-2019 adds INDEX, DESIGNATED_SUBTYPE, REFLECT
// and CONVERSE (the last for a mode view).
constexpr std::array<Row, 39> rows { {
    { "base", PredefinedAttribute::base, 1987, 0 },
    { "left", PredefinedAttribute::left, 1987, 0 },
    { "right", PredefinedAttribute::right, 1987, 0 },
    { "high", PredefinedAttribute::high, 1987, 0 },
    { "low", PredefinedAttribute::low, 1987, 0 },
    { "pos", PredefinedAttribute::pos, 1987, 0 },
    { "val", PredefinedAttribute::val, 1987, 0 },
    { "succ", PredefinedAttribute::succ, 1987, 0 },
    { "pred", PredefinedAttribute::pred, 1987, 0 },
    { "leftof", PredefinedAttribute::leftof, 1987, 0 },
    { "rightof", PredefinedAttribute::rightof, 1987, 0 },
    { "range", PredefinedAttribute::range, 1987, 0 },
    { "reverse_range", PredefinedAttribute::reverse_range, 1987, 0 },
    { "length", PredefinedAttribute::length, 1987, 0 },
    { "delayed", PredefinedAttribute::delayed, 1987, 0 },
    { "stable", PredefinedAttribute::stable, 1987, 0 },
    { "quiet", PredefinedAttribute::quiet, 1987, 0 },
    { "transaction", PredefinedAttribute::transaction, 1987, 0 },
    { "event", PredefinedAttribute::event, 1987, 0 },
    { "active", PredefinedAttribute::active, 1987, 0 },
    { "last_event", PredefinedAttribute::last_event, 1987, 0 },
    { "last_active", PredefinedAttribute::last_active, 1987, 0 },
    { "last_value", PredefinedAttribute::last_value, 1987, 0 },
    { "behavior", PredefinedAttribute::behavior, 1987, 1987 },
    { "structure", PredefinedAttribute::structure, 1987, 1987 },
    { "ascending", PredefinedAttribute::ascending, 1993, 0 },
    { "image", PredefinedAttribute::image, 1993, 0 },
    { "value", PredefinedAttribute::value, 1993, 0 },
    { "driving", PredefinedAttribute::driving, 1993, 0 },
    { "driving_value", PredefinedAttribute::driving_value, 1993, 0 },
    { "simple_name", PredefinedAttribute::simple_name, 1993, 0 },
    { "instance_name", PredefinedAttribute::instance_name, 1993, 0 },
    { "path_name", PredefinedAttribute::path_name, 1993, 0 },
    { "subtype", PredefinedAttribute::subtype, 2008, 0 },
    { "element", PredefinedAttribute::element, 2008, 0 },
    { "index", PredefinedAttribute::index, 2019, 0 },
    { "designated_subtype", PredefinedAttribute::designated_subtype, 2019, 0 },
    { "reflect", PredefinedAttribute::reflect, 2019, 0 },
    { "converse", PredefinedAttribute::converse, 2019, 0 },
} };

struct Interned {
    support::SymbolId symbol;
    const Row* row;
};

const std::vector<Interned>& interned_rows()
{
    static const std::vector<Interned> table = [] {
        std::vector<Interned> result;
        for (const Row& row : rows)
            result.push_back({ support::vhdl_symbol(row.spelling), &row });
        return result;
    }();
    return table;
}

bool defined_in(const Row& row, std::uint16_t year)
{
    return year >= row.since && (row.until == 0 || year <= row.until);
}

[[nodiscard]] bool is_array(TypeClass c) { return c == TypeClass::array; }
[[nodiscard]] bool is_discrete(TypeClass c)
{
    return c == TypeClass::enumeration || c == TypeClass::integer
        || c == TypeClass::universal_integer;
}
[[nodiscard]] bool is_scalar(TypeClass c)
{
    return is_discrete(c) || c == TypeClass::floating || c == TypeClass::physical
        || c == TypeClass::universal_real;
}
[[nodiscard]] bool is_discrete_or_physical(TypeClass c)
{
    return is_discrete(c) || c == TypeClass::physical;
}

// 5.7: a composite type is representable if the base type of each of its
// elements is a scalar type or a representable composite type.
[[nodiscard]] bool representable(const Types& types, const Type& type, const int depth)
{
    if (depth > 256) {
        throw std::logic_error { "VHDL typing: composite type nesting too deep" };
    }
    const TypeClass cls = types.class_of(type);
    if (is_scalar(cls)) {
        return true;
    }
    if (cls == TypeClass::array) {
        return representable(types, types.element(type), depth + 1);
    }
    if (cls != TypeClass::record) {
        return false;
    }
    const auto view = types.regions().design().find_type(type.declared);
    if (!view || view->vhdl == nullptr) {
        throw std::logic_error { "VHDL typing: record type without a definition" };
    }
    return std::ranges::all_of(view->vhdl->record_elements, [&](const auto& element) {
        return representable(types, types.of_subtype(element.subtype), depth + 1);
    });
}

AttributeProfile make(PredefinedAttribute attribute, Type result,
    AttributeArgument argument = AttributeArgument::none, bool optional = false)
{
    AttributeProfile profile;
    profile.attribute = attribute;
    profile.argument = argument;
    profile.argument_optional = optional;
    profile.result = std::move(result);
    return profile;
}

Type std_type(StandardDeclaration declaration)
{
    return Types::standard(declaration);
}

// Attributes whose prefix is an object or a type (16.2.2, 16.2.3, 16.2.6):
// `is_type` is a type mark, otherwise an object (or alias) of the type.
// Scalar attributes on objects are 1076-2019 shorthands (O'IMAGE is
// O'SUBTYPE'IMAGE(O), 16.2.2); 'LENGTH of a discrete or physical type or
// object is also 2019 (16.2.2).
std::optional<AttributeProfile> value_profile(const Types& types,
    PredefinedAttribute attribute, bool is_type, const Type& type,
    std::size_t dimension, std::uint16_t year)
{
    const TypeClass cls = types.class_of(type);
    const bool array = is_array(cls);
    const bool scalar = is_scalar(cls);
    // A scalar-object prefix is defined only from 1076-2019.
    const bool scalar_ok = scalar && (is_type || year >= 2019);
    const bool dim_ok = array && dimension < types.dimensions(type);
    const bool optional_dim = true;

    switch (attribute) {
    case PredefinedAttribute::left:
    case PredefinedAttribute::right:
    case PredefinedAttribute::high:
    case PredefinedAttribute::low:
        if (array) {
            if (!dim_ok)
                return std::nullopt;
            return make(attribute, types.index(type, dimension),
                AttributeArgument::dimension, optional_dim);
        }
        if (scalar_ok)
            return make(attribute, type);
        return std::nullopt;
    case PredefinedAttribute::ascending:
        if (array) {
            if (!dim_ok)
                return std::nullopt;
            return make(attribute, std_type(StandardDeclaration::boolean),
                AttributeArgument::dimension, optional_dim);
        }
        if (scalar_ok)
            return make(attribute, std_type(StandardDeclaration::boolean));
        return std::nullopt;
    case PredefinedAttribute::length:
        if (array) {
            if (!dim_ok)
                return std::nullopt;
            return make(attribute, Types::universal_integer(),
                AttributeArgument::dimension, optional_dim);
        }
        if (is_discrete_or_physical(cls) && (is_type || year >= 2019))
            return make(attribute, Types::universal_integer());
        return std::nullopt;
    case PredefinedAttribute::range:
    case PredefinedAttribute::reverse_range: {
        AttributeProfile profile;
        if (array) {
            if (!dim_ok)
                return std::nullopt;
            profile = make(attribute, types.index(type, dimension),
                AttributeArgument::dimension, optional_dim);
        } else if (scalar_ok) {
            profile = make(attribute, type);
        } else {
            return std::nullopt;
        }
        profile.range = true;
        return profile;
    }
    case PredefinedAttribute::pos:
    case PredefinedAttribute::val:
    case PredefinedAttribute::succ:
    case PredefinedAttribute::pred:
    case PredefinedAttribute::leftof:
    case PredefinedAttribute::rightof: {
        // T'POS(X) etc. take the argument; the object shorthand O'POS
        // (2019) is O'SUBTYPE'POS(O) and takes none (16.2.2).
        if (!is_discrete_or_physical(cls) || (!is_type && year < 2019))
            return std::nullopt;
        const bool call = is_type;
        const AttributeArgument argument = !call ? AttributeArgument::none
            : attribute == PredefinedAttribute::val ? AttributeArgument::any_integer
                                                    : AttributeArgument::prefix_base;
        return make(attribute,
            attribute == PredefinedAttribute::pos ? Types::universal_integer() : type,
            argument);
    }
    case PredefinedAttribute::image:
        // 16.2.2 (2019): also a representable composite type (5.7).
        if (!scalar_ok && !(year >= 2019 && !scalar && representable(types, type, 0)))
            return std::nullopt;
        return make(attribute, std_type(StandardDeclaration::string),
            is_type ? AttributeArgument::prefix_base : AttributeArgument::none);
    case PredefinedAttribute::value:
        // T'VALUE(X) exists only for a type mark (16.2.2).
        // 2019: also a representable composite type (5.7).
        if (!is_type || !(scalar || (year >= 2019 && representable(types, type, 0))))
            return std::nullopt;
        return make(attribute, type, AttributeArgument::string);
    case PredefinedAttribute::base: {
        // T'BASE: a type or subtype mark (16.2.2).
        if (!is_type || types.class_of(type) == TypeClass::none)
            return std::nullopt;
        AttributeProfile profile = make(attribute, type);
        profile.subtype = true;
        return profile;
    }
    case PredefinedAttribute::subtype: {
        // O'SUBTYPE: an object or alias (16.2.2, 2008).
        if (is_type || cls == TypeClass::none)
            return std::nullopt;
        AttributeProfile profile = make(attribute, type);
        profile.subtype = true;
        return profile;
    }
    case PredefinedAttribute::element: {
        if (!array)
            return std::nullopt;
        AttributeProfile profile = make(attribute, types.element(type));
        profile.subtype = true;
        return profile;
    }
    case PredefinedAttribute::index: {
        // A'INDEX[(N)] (2019).
        if (!array || !dim_ok)
            return std::nullopt;
        AttributeProfile profile = make(attribute, types.index(type, dimension),
            AttributeArgument::dimension, optional_dim);
        profile.subtype = true;
        return profile;
    }
    case PredefinedAttribute::designated_subtype: {
        // P'DESIGNATED_SUBTYPE: an access or file type or object (2019).
        if (cls != TypeClass::access && cls != TypeClass::file)
            return std::nullopt;
        AttributeProfile profile = make(attribute, types.designated(type));
        profile.subtype = true;
        return profile;
    }
    case PredefinedAttribute::reflect:
        // T'REFLECT / O'REFLECT (2019): the core types the mirror.
        if (cls == TypeClass::none)
            return std::nullopt;
        return make(attribute, Type { });
    default:
        return std::nullopt;
    }
}

// Signal attributes (16.2.4): the prefix is a signal of any type.
std::optional<AttributeProfile> signal_profile(const Types& types,
    PredefinedAttribute attribute, const Type& type)
{
    if (types.class_of(type) == TypeClass::none)
        return std::nullopt;
    switch (attribute) {
    case PredefinedAttribute::event:
    case PredefinedAttribute::active:
    case PredefinedAttribute::driving:
        return make(attribute, std_type(StandardDeclaration::boolean));
    case PredefinedAttribute::last_event:
    case PredefinedAttribute::last_active:
        return make(attribute, std_type(StandardDeclaration::time));
    case PredefinedAttribute::last_value:
    case PredefinedAttribute::driving_value:
        return make(attribute, type);
    case PredefinedAttribute::delayed: {
        AttributeProfile profile = make(attribute, type, AttributeArgument::time, true);
        profile.signal = true;
        return profile;
    }
    case PredefinedAttribute::stable:
    case PredefinedAttribute::quiet: {
        AttributeProfile profile = make(attribute,
            std_type(StandardDeclaration::boolean), AttributeArgument::time, true);
        profile.signal = true;
        return profile;
    }
    case PredefinedAttribute::transaction: {
        AttributeProfile profile = make(attribute, std_type(StandardDeclaration::bit));
        profile.signal = true;
        return profile;
    }
    default:
        return std::nullopt;
    }
}

} // namespace

std::optional<PredefinedAttribute> predefined_attribute(
    support::SymbolId designator, std::uint16_t year)
{
    if (year < 1987)
        throw std::logic_error("predefined_attribute: unknown VHDL edition");
    for (const Interned& entry : interned_rows())
        if (entry.symbol == designator)
            return defined_in(*entry.row, year)
                ? std::optional<PredefinedAttribute> { entry.row->attribute }
                : std::nullopt;
    return std::nullopt;
}

std::optional<AttributeProfile> attribute_profile(const Types& types,
    PredefinedAttribute attribute, AttributePrefix prefix, const Type& prefix_type,
    std::size_t dimension, std::uint16_t year)
{
    // 16.2.5: the named-entity attributes apply to any named entity,
    // including types and objects; BEHAVIOR/STRUCTURE (1076-1987 14.1) to
    // a block or architecture.
    switch (attribute) {
    case PredefinedAttribute::simple_name:
    case PredefinedAttribute::instance_name:
    case PredefinedAttribute::path_name:
        return make(attribute, std_type(StandardDeclaration::string));
    case PredefinedAttribute::behavior:
    case PredefinedAttribute::structure:
        if (prefix != AttributePrefix::named_entity)
            return std::nullopt;
        return make(attribute, std_type(StandardDeclaration::boolean));
    case PredefinedAttribute::converse:
        // M'CONVERSE (2019 6.5.2): a mode view, the core types it.
        if (prefix != AttributePrefix::named_entity)
            return std::nullopt;
        return make(attribute, Type { });
    default:
        break;
    }
    switch (prefix) {
    case AttributePrefix::named_entity:
        // A value that is no object, such as the call of the predefined
        // function T'IMAGE(X) or a literal, is a prefix appropriate for an
        // array object (1076-1993 14.1 / 1076-2019 16.1, 16.2.3: a prefix
        // is a name or a function call): the array attributes take it.
        if (types.class_of(prefix_type) == TypeClass::array) {
            switch (attribute) {
            case PredefinedAttribute::left:
            case PredefinedAttribute::right:
            case PredefinedAttribute::high:
            case PredefinedAttribute::low:
            case PredefinedAttribute::ascending:
            case PredefinedAttribute::length:
            case PredefinedAttribute::range:
            case PredefinedAttribute::reverse_range:
                return value_profile(types, attribute, false, prefix_type, dimension, year);
            default:
                break;
            }
        }
        return std::nullopt;
    case AttributePrefix::type_mark:
        return value_profile(types, attribute, true, prefix_type, dimension, year);
    case AttributePrefix::object:
        return value_profile(types, attribute, false, prefix_type, dimension, year);
    case AttributePrefix::signal:
        // A signal is an object: its own attributes first (16.2.4), then
        // the object attributes.
        if (auto profile = signal_profile(types, attribute, prefix_type))
            return profile;
        return value_profile(types, attribute, false, prefix_type, dimension, year);
    }
    throw std::logic_error("attribute_profile: invalid prefix kind");
}

} // namespace fsim::semantic::vhdl_names
