// SPDX-License-Identifier: Apache-2.0
// The profiles of the subprograms of a design for compile-time expression
// typing (gate BD build D4, IEEE 1076-2019 4.5, 5.x, 9.2, 16.3): the explicit
// subprograms and enumeration literals, the STD.STANDARD function rows, and
// the operations a type declaration implicitly declares. Names are compared
// as interned symbols, types by identity; a spelling is interned once, in the
// tables below.

#include "compiled_design_vhdl_typing_internal.hpp"

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/vhdl_native_identity.hpp"
#include "fsim/semantic/vhdl_standard_catalog.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::semantic::vhdl_names {

namespace {

namespace sd = support;
using Op = vhdl::PredefinedOperation;
using Designated = std::pair<sd::SymbolId, Op>;

// ---------------------------------------------------------------------
// STD.STANDARD catalog rows (16.3)
// ---------------------------------------------------------------------

struct StandardRow {
    bool function { };
    std::vector<sd::SymbolId> names;
    std::vector<sd::SymbolId> types;
    sd::SymbolId result;
};

/// The catalog parsed once: every function row's formals and result as
/// symbols, and the declaration of each catalog type or subtype row by its
/// symbol. The type names of the catalog are interned here and nowhere else.
struct StandardTable {
    std::vector<StandardRow> rows;
    std::unordered_map<std::uint32_t, sd::StandardDeclaration> type_rows;
    sd::SymbolId universal_integer = sd::vhdl_symbol("universal_integer");
    sd::SymbolId universal_real = sd::vhdl_symbol("universal_real");

    StandardTable()
    {
        const auto catalog = vhdl_standard_catalog();
        rows.resize(catalog.size());
        for (std::size_t index = 0; index < catalog.size(); ++index) {
            const auto& entry = catalog[index];
            if (entry.kind == 't') {
                type_rows.emplace(sd::vhdl_symbol(entry.name).index(),
                    static_cast<sd::StandardDeclaration>(index + 1U));
            } else if (entry.kind == 'f' || entry.kind == 'i') {
                parse(entry, rows[index]);
            }
        }
    }

    /// "NAME/CLASS/TYPE,..." (an anonymous formal has an empty NAME).
    static void parse(const VhdlStandardCatalogEntry& entry, StandardRow& row)
    {
        row.function = true;
        row.result = sd::vhdl_symbol(entry.result);
        std::string_view rest = entry.parameters;
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const auto item = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view {}
                                                   : rest.substr(comma + 1U);
            const auto first = item.find('/');
            const auto second = first == std::string_view::npos
                ? std::string_view::npos
                : item.find('/', first + 1U);
            if (second == std::string_view::npos) {
                throw std::logic_error("malformed STANDARD catalog parameter");
            }
            const auto name = item.substr(0, first);
            row.names.push_back(name.empty() ? sd::SymbolId {} : sd::vhdl_symbol(name));
            row.types.push_back(sd::vhdl_symbol(item.substr(second + 1U)));
        }
    }
};

const StandardTable& standard_table()
{
    static const StandardTable table;
    return table;
}

Type standard_type(const Types& types, const StandardTable& table, const sd::SymbolId name)
{
    if (name == table.universal_integer) {
        return Types::universal_integer();
    }
    if (name == table.universal_real) {
        return Types::universal_real();
    }
    const auto found = table.type_rows.find(name.index());
    if (found == table.type_rows.end()) {
        throw std::logic_error("STANDARD catalog names an unknown type");
    }
    const Type type = types.of_standard(found->second);
    if (!type) {
        throw std::logic_error("STANDARD catalog type has no base type");
    }
    return type;
}

// ---------------------------------------------------------------------
// Designators and formal names of implicit operations
// ---------------------------------------------------------------------

struct Symbols {
    // Operator designators (9.2; the operator text without its quotes).
    sd::SymbolId eq = sd::vhdl_symbol("=");
    sd::SymbolId ne = sd::vhdl_symbol("/=");
    sd::SymbolId lt = sd::vhdl_symbol("<");
    sd::SymbolId le = sd::vhdl_symbol("<=");
    sd::SymbolId gt = sd::vhdl_symbol(">");
    sd::SymbolId ge = sd::vhdl_symbol(">=");
    sd::SymbolId match_eq = sd::vhdl_symbol("?=");
    sd::SymbolId match_ne = sd::vhdl_symbol("?/=");
    sd::SymbolId match_lt = sd::vhdl_symbol("?<");
    sd::SymbolId match_le = sd::vhdl_symbol("?<=");
    sd::SymbolId match_gt = sd::vhdl_symbol("?>");
    sd::SymbolId match_ge = sd::vhdl_symbol("?>=");
    sd::SymbolId and_ = sd::vhdl_symbol("and");
    sd::SymbolId or_ = sd::vhdl_symbol("or");
    sd::SymbolId nand = sd::vhdl_symbol("nand");
    sd::SymbolId nor = sd::vhdl_symbol("nor");
    sd::SymbolId xor_ = sd::vhdl_symbol("xor");
    sd::SymbolId xnor = sd::vhdl_symbol("xnor");
    sd::SymbolId not_ = sd::vhdl_symbol("not");
    sd::SymbolId plus = sd::vhdl_symbol("+");
    sd::SymbolId minus = sd::vhdl_symbol("-");
    sd::SymbolId times = sd::vhdl_symbol("*");
    sd::SymbolId divide = sd::vhdl_symbol("/");
    sd::SymbolId mod = sd::vhdl_symbol("mod");
    sd::SymbolId rem = sd::vhdl_symbol("rem");
    sd::SymbolId abs_ = sd::vhdl_symbol("abs");
    sd::SymbolId power = sd::vhdl_symbol("**");
    sd::SymbolId concat = sd::vhdl_symbol("&");
    sd::SymbolId sll = sd::vhdl_symbol("sll");
    sd::SymbolId srl = sd::vhdl_symbol("srl");
    sd::SymbolId sla = sd::vhdl_symbol("sla");
    sd::SymbolId sra = sd::vhdl_symbol("sra");
    sd::SymbolId rol = sd::vhdl_symbol("rol");
    sd::SymbolId ror = sd::vhdl_symbol("ror");
    // Subprogram designators (5.2.6, 5.3.2.4, 5.4.3, 5.5.2).
    sd::SymbolId minimum = sd::vhdl_symbol("minimum");
    sd::SymbolId maximum = sd::vhdl_symbol("maximum");
    sd::SymbolId to_string = sd::vhdl_symbol("to_string");
    sd::SymbolId deallocate = sd::vhdl_symbol("deallocate");
    sd::SymbolId file_open = sd::vhdl_symbol("file_open");
    sd::SymbolId file_close = sd::vhdl_symbol("file_close");
    sd::SymbolId read = sd::vhdl_symbol("read");
    sd::SymbolId write = sd::vhdl_symbol("write");
    sd::SymbolId endfile = sd::vhdl_symbol("endfile");
    sd::SymbolId flush = sd::vhdl_symbol("flush");
    sd::SymbolId file_rewind = sd::vhdl_symbol("file_rewind");
    sd::SymbolId file_seek = sd::vhdl_symbol("file_seek");
    sd::SymbolId file_truncate = sd::vhdl_symbol("file_truncate");
    sd::SymbolId file_state = sd::vhdl_symbol("file_state");
    sd::SymbolId file_mode = sd::vhdl_symbol("file_mode");
    sd::SymbolId file_position = sd::vhdl_symbol("file_position");
    sd::SymbolId file_size = sd::vhdl_symbol("file_size");
    sd::SymbolId file_canseek = sd::vhdl_symbol("file_canseek");
    // Formal designators.
    sd::SymbolId l = sd::vhdl_symbol("l");
    sd::SymbolId r = sd::vhdl_symbol("r");
    sd::SymbolId value = sd::vhdl_symbol("value");
    sd::SymbolId f = sd::vhdl_symbol("f");
    sd::SymbolId p = sd::vhdl_symbol("p");
    sd::SymbolId external_name = sd::vhdl_symbol("external_name");
    sd::SymbolId open_kind = sd::vhdl_symbol("open_kind");
    sd::SymbolId status = sd::vhdl_symbol("status");
    sd::SymbolId length = sd::vhdl_symbol("length");
    sd::SymbolId offset = sd::vhdl_symbol("offset");
    sd::SymbolId origin = sd::vhdl_symbol("origin");
    sd::SymbolId size = sd::vhdl_symbol("size");
};

const Symbols& symbols()
{
    static const Symbols table;
    return table;
}

// ---------------------------------------------------------------------
// Implicit operations
// ---------------------------------------------------------------------

/// Collects the operations of one type declaration whose designator is the
/// wanted one.
struct Emitter {
    const Types& types;
    const vhdl::Declaration& declaration;
    sd::SymbolId wanted;
    std::uint16_t year;
    Type type;
    const Symbols& s = symbols();
    std::vector<Profile> out;

    /// Whether edition `edition` or a later one is in force (a standard year
    /// is compared as a number: 2000 and 2002 follow 1993).
    [[nodiscard]] bool since(const std::uint16_t edition) const noexcept
    {
        return year >= edition;
    }

    void add(const sd::SymbolId designator, const Op operation, std::vector<Type> parameters,
        const Type result, std::vector<sd::SymbolId> names = {},
        std::vector<bool> defaults = {})
    {
        if (!(designator == wanted)) {
            return;
        }
        names.resize(parameters.size());
        defaults.resize(parameters.size());
        Profile profile;
        profile.source = Profile::Source::implicit;
        profile.declaration = declaration.id;
        profile.operation = operation;
        profile.parameters = std::move(parameters);
        profile.formal_names = std::move(names);
        profile.defaults = std::move(defaults);
        profile.result = result;
        out.push_back(std::move(profile));
    }
};

Type std_type(const sd::StandardDeclaration row) noexcept
{
    return Types::standard(row);
}

bool scalar_class(const TypeClass c) noexcept
{
    return c == TypeClass::enumeration || c == TypeClass::integer
        || c == TypeClass::floating || c == TypeClass::physical;
}

bool discrete_class(const TypeClass c) noexcept
{
    return c == TypeClass::enumeration || c == TypeClass::integer;
}

/// Whether the type contains a file or protected subelement through array
/// elements (9.2.3: no equality). Record elements cannot be walked with the
/// Types interface; see the report.
bool limited(const Types& types, const Type& type)
{
    switch (types.class_of(type)) {
    case TypeClass::file:
    case TypeClass::protected_type:
        return true;
    case TypeClass::array:
        return limited(types, types.element(type));
    default:
        return false;
    }
}

/// A representable composite type (5.7: every element scalar or
/// representable composite).
bool representable(const Types& types, const Type& type)
{
    const auto c = types.class_of(type);
    if (scalar_class(c) || c == TypeClass::record) {
        return true;
    }
    return c == TypeClass::array && representable(types, types.element(type));
}

/// Whether a file type's element subtype is an unconstrained or partially
/// constrained array (5.5.2: READ then gets a LENGTH formal).
bool unconstrained_subtype(const Types& types, const vhdl::SubtypeIndication& subtype,
    const Type& designated, const unsigned depth = 0U)
{
    if (depth > 64U) {
        throw std::logic_error("cyclic subtype chain");
    }
    if (types.class_of(designated) != TypeClass::array) {
        return false;
    }
    if (!subtype.constraints.empty() || !subtype.element_constraints.empty()) {
        return false;
    }
    if (!subtype.type_mark.target.valid()) {
        // A STANDARD array type (STRING, BIT_VECTOR, ...) is unconstrained.
        return true;
    }
    const auto record = types.regions().design().find_type(subtype.type_mark.target);
    if (!record || record->vhdl == nullptr) {
        throw std::logic_error("file element subtype has no type definition");
    }
    const auto& definition = *record->vhdl;
    if (definition.form == vhdl::TypeForm::subtype || definition.form == vhdl::TypeForm::alias) {
        return unconstrained_subtype(types, definition.base, designated, depth + 1U);
    }
    for (const auto& dimension : definition.array_dimensions) {
        if (dimension.unconstrained) {
            return true;
        }
    }
    return false;
}

/// 5.2.6 and the numeric clauses (5.2.3.2, 5.2.4.2, 5.2.5.2, 9.2).
void scalar_operations(Emitter& e, const TypeClass c)
{
    const auto& s = e.s;
    const Type t = e.type;
    const Type integer = std_type(sd::StandardDeclaration::integer);
    const Type real = std_type(sd::StandardDeclaration::real);
    const Type boolean = std_type(sd::StandardDeclaration::boolean);

    // 9.2.3: relational operators on every scalar type.
    const Designated relational[] = { { s.eq, Op::eq }, { s.ne, Op::ne }, { s.lt, Op::lt },
        { s.le, Op::le }, { s.gt, Op::gt }, { s.ge, Op::ge } };
    for (const auto& [designator, op] : relational) {
        e.add(designator, op, { t, t }, boolean);
    }
    if (c != TypeClass::enumeration) {
        // 9.2.5, 9.2.6, 9.2.8: numeric types (integer, floating, physical).
        e.add(s.plus, Op::add, { t, t }, t);
        e.add(s.minus, Op::subtract, { t, t }, t);
        e.add(s.plus, Op::identity, { t }, t);
        e.add(s.minus, Op::negation, { t }, t);
        e.add(s.abs_, Op::absolute, { t }, t);
    }
    if (c == TypeClass::integer || c == TypeClass::floating) {
        // 9.2.7: * and / on integer and floating-point types.
        e.add(s.times, Op::multiply, { t, t }, t);
        e.add(s.divide, Op::divide, { t, t }, t);
        // 9.2.8: the exponent is the predefined type INTEGER.
        e.add(s.power, Op::power, { t, integer }, t);
    }
    if (c == TypeClass::integer) {
        e.add(s.mod, Op::modulus, { t, t }, t);
        e.add(s.rem, Op::remainder, { t, t }, t);
    }
    if (c == TypeClass::physical) {
        // 9.2.7: multiplication and division by INTEGER and REAL.
        e.add(s.times, Op::multiply, { t, integer }, t);
        e.add(s.times, Op::multiply, { t, real }, t);
        e.add(s.times, Op::multiply, { integer, t }, t);
        e.add(s.times, Op::multiply, { real, t }, t);
        e.add(s.divide, Op::divide, { t, integer }, t);
        e.add(s.divide, Op::divide, { t, real }, t);
        e.add(s.divide, Op::divide, { t, t }, Types::universal_integer());
        // 1076-2008 9.2.7: mod and rem on physical types (absent in 1987 to 2002).
        if (e.since(2008U)) {
            e.add(s.mod, Op::modulus, { t, t }, t);
            e.add(s.rem, Op::remainder, { t, t }, t);
        }
    }
    // 1076-2008 9.2.3: the matching relational operators of STD_ULOGIC
    // (BIT's are STANDARD rows; STD_LOGIC_1164 declares "??" itself);
    // STD_ULOGIC is identified natively, never by name.
    if (e.since(2008U) && c == TypeClass::enumeration && t.kind == Type::Kind::declared
        && vhdl_native_logic9(e.types.regions().design(), t.declared)) {
        const Designated matching[] = { { s.match_eq, Op::match_eq },
            { s.match_ne, Op::match_ne }, { s.match_lt, Op::match_lt },
            { s.match_le, Op::match_le }, { s.match_gt, Op::match_gt },
            { s.match_ge, Op::match_ge } };
        for (const auto& [designator, op] : matching) {
            e.add(designator, op, { t, t }, t);
        }
    }
    // 1076-2008 5.2.6: MINIMUM, MAXIMUM and TO_STRING (absent before 2008).
    if (e.since(2008U)) {
        e.add(s.minimum, Op::minimum, { t, t }, t, { s.l, s.r });
        e.add(s.maximum, Op::maximum, { t, t }, t, { s.l, s.r });
        e.add(s.to_string, Op::to_string, { t },
            std_type(sd::StandardDeclaration::string), { s.value });
    }
}

/// 5.3.2.4 and 9.2.
void array_operations(Emitter& e)
{
    const auto& s = e.s;
    const auto& types = e.types;
    const Type a = e.type;
    const Type boolean = std_type(sd::StandardDeclaration::boolean);
    const Type bit = std_type(sd::StandardDeclaration::bit);
    const Type integer = std_type(sd::StandardDeclaration::integer);
    const Type element = types.element(a);
    const auto element_class = types.class_of(element);

    // 9.2.3: = and /= unless an element is a file or protected type.
    if (!limited(types, a)) {
        e.add(s.eq, Op::eq, { a, a }, boolean);
        e.add(s.ne, Op::ne, { a, a }, boolean);
    }
    // 1076-2019 5.3.2.4: TO_STRING of a representable array type (5.7);
    // 1076-2008 5.3.2.4: of a one-dimensional array of a character type
    // (absent before 2008).
    if (e.since(2019U) ? representable(types, a) : (e.since(2008U) && types.string_type(a))) {
        e.add(s.to_string, Op::to_string, { a },
            std_type(sd::StandardDeclaration::string), { s.value });
    }
    if (types.dimensions(a) != 1U) {
        return;
    }
    // 9.2.3: ordering of discrete arrays; 1076-2019 widens it to scalar arrays.
    const bool ordered = e.since(2019U) ? scalar_class(element_class)
                                        : discrete_class(element_class);
    if (ordered) {
        const Designated ordering[] = { { s.lt, Op::lt }, { s.le, Op::le }, { s.gt, Op::gt },
            { s.ge, Op::ge } };
        for (const auto& [designator, op] : ordering) {
            e.add(designator, op, { a, a }, boolean);
        }
        // 1076-2008 5.3.2.4: MINIMUM and MAXIMUM of discrete arrays (scalar
        // arrays from 2019).
        if (e.since(2008U)) {
            e.add(s.minimum, Op::minimum, { a, a }, a, { s.l, s.r });
            e.add(s.maximum, Op::maximum, { a, a }, a, { s.l, s.r });
        }
    }
    // 1076-2008 5.3.2.4: MINIMUM (L) and MAXIMUM (L) of a one-dimensional
    // array of a scalar type.
    if (e.since(2008U) && scalar_class(element_class)) {
        e.add(s.minimum, Op::minimum, { a }, element, { s.l });
        e.add(s.maximum, Op::maximum, { a }, element, { s.l });
    }
    // 9.2.5: concatenation of a one-dimensional array.
    e.add(s.concat, Op::concatenate, { a, a }, a);
    e.add(s.concat, Op::concatenate, { a, element }, a);
    e.add(s.concat, Op::concatenate, { element, a }, a);
    e.add(s.concat, Op::concatenate, { element, element }, a);

    if (element == bit || element == boolean) {
        // 9.2.2: logical operators; XNOR from 1076-1993.
        const Designated logical[] = { { s.and_, Op::logical_and }, { s.or_, Op::logical_or },
            { s.nand, Op::logical_nand }, { s.nor, Op::logical_nor },
            { s.xor_, Op::logical_xor }, { s.xnor, Op::logical_xnor } };
        for (const auto& [designator, op] : logical) {
            if (op == Op::logical_xnor && !e.since(1993U)) {
                continue;
            }
            e.add(designator, op, { a, a }, a);
            // 1076-2008 9.2.2: array-scalar and scalar-array forms, and the
            // unary reduction operators (result: the element type).
            if (e.since(2008U)) {
                e.add(designator, op, { a, element }, a);
                e.add(designator, op, { element, a }, a);
                e.add(designator, op, { a }, element);
            }
        }
        e.add(s.not_, Op::logical_not, { a }, a);
        // 9.2.4: shift operators from 1076-1993.
        if (e.since(1993U)) {
            const Designated shifts[] = { { s.sll, Op::shift_sll }, { s.srl, Op::shift_srl },
                { s.sla, Op::shift_sla }, { s.sra, Op::shift_sra }, { s.rol, Op::shift_rol },
                { s.ror, Op::shift_ror } };
            for (const auto& [designator, op] : shifts) {
                e.add(designator, op, { a, integer }, a);
            }
        }
    }
    // 1076-2008 9.2.3: matching ?= and ?/= of one-dimensional arrays of BIT
    // or STD_ULOGIC (the latter identified natively, never by name).
    if (e.since(2008U)) {
        const bool std_ulogic = element.kind == Type::Kind::declared
            && vhdl_native_logic9(types.regions().design(), element.declared);
        if (element == bit || std_ulogic) {
            e.add(s.match_eq, Op::match_eq, { a, a }, element);
            e.add(s.match_ne, Op::match_ne, { a, a }, element);
        }
    }
}

/// 5.5.2.
void file_operations(Emitter& e)
{
    const auto& s = e.s;
    const auto& types = e.types;
    const Type ft = e.type;
    const Type boolean = std_type(sd::StandardDeclaration::boolean);
    const Type integer = std_type(sd::StandardDeclaration::integer);
    const Type string = std_type(sd::StandardDeclaration::string);
    const Type designated = types.designated(ft);
    if (ft.kind != Type::Kind::declared) {
        throw std::logic_error("file type is not a declared type");
    }
    const auto record = types.regions().design().find_type(ft.declared);
    if (!record || record->vhdl == nullptr || !record->vhdl->element_subtype) {
        throw std::logic_error("file type has no element subtype");
    }

    // 1076-1987 has no FILE_OPEN or FILE_CLOSE (a file is opened by its
    // declaration, 4.3.1.4); both are 1076-1993.
    if (e.since(1993U)) {
        const Type kind = std_type(sd::StandardDeclaration::file_open_kind);
        const Type status = std_type(sd::StandardDeclaration::file_open_status);
        e.add(s.file_open, Op::file_open, { ft, string, kind }, Type {},
            { s.f, s.external_name, s.open_kind }, { false, false, true });
        e.add(s.file_open, Op::file_open, { status, ft, string, kind }, Type {},
            { s.status, s.f, s.external_name, s.open_kind }, { false, false, false, true });
        // 1076-2019 5.5.2: the impure function form returning the status.
        if (e.since(2019U)) {
            e.add(s.file_open, Op::file_open, { ft, string, kind }, status,
                { s.f, s.external_name, s.open_kind }, { false, false, true });
        }
        e.add(s.file_close, Op::file_close, { ft }, Type {}, { s.f });
    }
    e.add(s.read, Op::read, { ft, designated }, Type {}, { s.f, s.value });
    if (unconstrained_subtype(types, *record->vhdl->element_subtype, designated)) {
        e.add(s.read, Op::read,
            { ft, designated, types.of_standard(sd::StandardDeclaration::natural) }, Type {},
            { s.f, s.value, s.length });
    }
    e.add(s.write, Op::write, { ft, designated }, Type {}, { s.f, s.value });
    e.add(s.endfile, Op::endfile, { ft }, boolean, { s.f });
    // 1076-2008 5.5.2: FLUSH.
    if (e.since(2008U)) {
        e.add(s.flush, Op::flush, { ft }, Type {}, { s.f });
    }
    // 1076-2019 5.5.2: the seek, size and state operations.
    if (e.since(2019U)) {
        const Type origin = std_type(sd::StandardDeclaration::file_origin_kind);
        e.add(s.file_rewind, Op::file_rewind, { ft }, Type {}, { s.f });
        e.add(s.file_seek, Op::file_seek, { ft, integer, origin }, Type {},
            { s.f, s.offset, s.origin }, { false, false, true });
        e.add(s.file_truncate, Op::file_truncate, { ft, integer, origin }, Type {},
            { s.f, s.size, s.origin }, { false, false, true });
        e.add(s.file_state, Op::file_state, { ft },
            std_type(sd::StandardDeclaration::file_open_state), { s.f });
        e.add(s.file_mode, Op::file_mode, { ft },
            std_type(sd::StandardDeclaration::file_open_kind), { s.f });
        e.add(s.file_position, Op::file_position, { ft, origin }, integer, { s.f, s.origin },
            { false, true });
        e.add(s.file_size, Op::file_size, { ft }, integer, { s.f });
        e.add(s.file_canseek, Op::file_canseek, { ft }, boolean, { s.f });
    }
}

} // namespace

Operations::Operations(const Types& types)
    : types_ { types }
{
}

Profile Operations::of_declaration(const vhdl::Declaration& declaration) const
{
    Profile profile;
    profile.source = Profile::Source::declaration;
    profile.declaration = declaration.id;
    if (declaration.form == vhdl::DeclarationForm::enumeration_literal) {
        // 5.2.2.1: a literal is a parameterless function of its type.
        profile.result = types_.of_literal(declaration);
        return profile;
    }
    if (!declaration.callable) {
        throw std::logic_error("declaration is no subprogram or enumeration literal");
    }
    const auto& design = types_.regions().design();
    for (const auto formal_id : declaration.callable->formals) {
        const auto view = design.find_declaration(formal_id);
        if (!view || view->vhdl == nullptr) {
            throw std::logic_error("subprogram formal has no declaration");
        }
        const auto& formal = *view->vhdl;
        profile.formals.push_back(formal_id);
        profile.parameters.push_back(types_.of_object(formal));
        // 15.4: an extended identifier keeps its spelling, a basic one is
        // case folded (Declaration::name).
        profile.formal_names.push_back(sd::is_extended_vhdl_identifier(formal.spelling)
                ? sd::vhdl_symbol(formal.spelling)
                : sd::vhdl_symbol(formal.name));
        profile.defaults.push_back(formal.initializer.has_value());
    }
    profile.result = types_.result_of(declaration);
    return profile;
}

Profile Operations::of_standard(const sd::StandardDeclaration function) const
{
    const auto& table = standard_table();
    const auto index = sd::catalog_row(function);
    if (index >= table.rows.size() || !table.rows[index].function) {
        throw std::logic_error("STANDARD declaration is no function");
    }
    const auto& row = table.rows[index];
    Profile profile;
    profile.source = Profile::Source::standard;
    profile.standard = function;
    for (const auto parameter : row.types) {
        profile.parameters.push_back(standard_type(types_, table, parameter));
    }
    profile.formal_names = row.names;
    profile.defaults.assign(row.types.size(), false);
    profile.result = standard_type(types_, table, row.result);
    return profile;
}

std::vector<Profile> Operations::implicit(const vhdl::Declaration& type_declaration,
    const sd::SymbolId designator, const std::uint16_t year) const
{
    const Type type = types_.of_type_declaration(type_declaration);
    const auto c = types_.class_of(type);
    Emitter e { types_, type_declaration, designator, year, type, symbols(), { } };
    const auto& s = e.s;
    const Type boolean = std_type(sd::StandardDeclaration::boolean);

    switch (c) {
    case TypeClass::enumeration:
    case TypeClass::integer:
    case TypeClass::floating:
    case TypeClass::physical:
        scalar_operations(e, c);
        break;
    case TypeClass::array:
        array_operations(e);
        break;
    case TypeClass::record:
        // 9.2.3: equality; 1076-2019 5.3.3.3: TO_STRING of a representable
        // record type (its elements cannot be walked through Types).
        e.add(s.eq, Op::eq, { type, type }, boolean);
        e.add(s.ne, Op::ne, { type, type }, boolean);
        if (e.since(2019U)) {
            e.add(s.to_string, Op::to_string, { type },
                std_type(sd::StandardDeclaration::string), { s.value });
        }
        break;
    case TypeClass::access:
        // 5.4.3: equality and DEALLOCATE (formal P, mode inout).
        e.add(s.eq, Op::eq, { type, type }, boolean);
        e.add(s.ne, Op::ne, { type, type }, boolean);
        e.add(s.deallocate, Op::deallocate, { type }, Type {}, { s.p });
        break;
    case TypeClass::file:
        file_operations(e);
        break;
    case TypeClass::protected_type: // 5.6: none
    case TypeClass::none:
    case TypeClass::universal_integer:
    case TypeClass::universal_real:
        break;
    }
    return std::move(e.out);
}

} // namespace fsim::semantic::vhdl_names
