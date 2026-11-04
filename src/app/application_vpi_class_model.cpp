// SPDX-License-Identifier: Apache-2.0
#include "application_vpi_class_model.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace fsim::app::application_detail {

namespace {

namespace sv = semantic::sv;

constexpr std::size_t no_index = static_cast<std::size_t>(-1);

[[nodiscard]] std::int64_t visibility_of(const sv::ClassVisibility visibility)
{
    switch (visibility) {
    case sv::ClassVisibility::protected_access:
        return vpiProtectedVis;
    case sv::ClassVisibility::local_access:
        return vpiLocalVis;
    case sv::ClassVisibility::public_access:
        break;
    }
    return vpiPublicVis;
}

// A class is automatic unless it is declared static (IEEE 1800-2023 37.3.7,
// 37.31 detail 8).
[[nodiscard]] bool automatic(const sv::ClassLifetime lifetime)
{
    return lifetime != sv::ClassLifetime::static_lifetime;
}

struct SourcePlace {
    std::string file;
    std::uint32_t line { };
};

[[nodiscard]] std::optional<SourcePlace> place_of(
    const semantic::Model& semantics, const auto& span)
{
    if (!span.valid() || span.value() >= semantics.source_spans().size()) {
        return std::nullopt;
    }
    const auto& location = semantics.source_spans().at(span.value());
    SourcePlace found;
    found.line = location.begin.line;
    if (!location.logical_name.empty()) {
        found.file = location.logical_name;
    } else if (location.file.valid()
        && location.file.value() < semantics.source_files().size()) {
        found.file
            = semantics.source_files().at(location.file.value()).physical_name;
    }
    return found;
}

[[nodiscard]] std::string without_spaces(const std::string_view text)
{
    std::string out;
    for (const auto character : text) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) {
            out.push_back(character);
        }
    }
    return out;
}

// The leading word of a type's spelling: "logic [3:0]" gives "logic".
[[nodiscard]] std::string leading_word(const std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return { };
    }
    auto end = begin;
    while (end < text.size()
        && (std::isalnum(static_cast<unsigned char>(text[end])) != 0
            || text[end] == '_')) {
        ++end;
    }
    return std::string { text.substr(begin, end - begin) };
}

// The type of a leaf expression of a constraint (37.34, 37.38): the HIR
// keeps the expression tree, not the VPI expression objects.
[[nodiscard]] PLI_INT32 leaf_type(const sv::ConstraintExpression& expression)
{
    using Kind = sv::ConstraintExpressionKind;
    switch (expression.kind) {
    case Kind::integer_literal:
    case Kind::boolean_literal:
    case Kind::logic_literal:
    case Kind::string_literal:
        return vpiConstant;
    case Kind::name:
        // A skipped foreach index variable is a null operation (37.38
        // detail 2).
        return expression.text.empty() ? vpiOperation : vpiRefObj;
    case Kind::inside_range:
        return vpiRange;
    default:
        return vpiOperation;
    }
}

// A source-like spelling of an expression; the tree keeps operators and
// names, not the original text.
[[nodiscard]] std::string decompile(const sv::ConstraintExpression& expression)
{
    using Kind = sv::ConstraintExpressionKind;
    if (expression.operands.empty()) {
        return expression.text;
    }
    const auto& operands = expression.operands;
    switch (expression.kind) {
    case Kind::unary:
        return expression.text + decompile(operands.front());
    case Kind::binary:
        if (operands.size() == 2U) {
            return decompile(operands[0]) + " " + expression.text + " "
                + decompile(operands[1]);
        }
        break;
    case Kind::index:
        if (operands.size() == 2U) {
            return decompile(operands[0]) + "[" + decompile(operands[1]) + "]";
        }
        break;
    default:
        break;
    }
    std::string text = expression.text + "(";
    for (std::size_t index = 0U; index < operands.size(); ++index) {
        text += (index == 0U ? "" : ", ") + decompile(operands[index]);
    }
    return text + ")";
}

[[nodiscard]] bool disable_soft(const sv::ConstraintExpression& expression)
{
    return expression.kind == sv::ConstraintExpressionKind::call
        && expression.text == "@disable-soft";
}

} // namespace

VpiClassModel::VpiClassModel(const sv::Hir& hir,
    const semantic::Model& semantics,
    const std::span<const sv::ClassSpecialization> specializations,
    Hooks hooks)
    : hir_(hir)
    , semantics_(semantics)
    , specializations_(specializations)
    , hooks_(std::move(hooks))
{
    const auto& classes = hir_.classes();
    for (std::size_t index = 0U; index < classes.size(); ++index) {
        identities_.emplace(sv::class_declaration_identity(classes[index]), index);
        by_name_[classes[index].name].push_back(index);
    }
    for (std::size_t index = 0U; index < specializations_.size(); ++index) {
        const auto& specialization = specializations_[index];
        specialized_[specialization.declaration_identity].push_back(index);
        specialization_index_.emplace(
            specialization.specialization_identity, index);
    }
}

const sv::ClassDeclaration& VpiClassModel::class_at(
    const std::size_t index) const
{
    return hir_.classes().at(index);
}

std::optional<std::size_t> VpiClassModel::find_declaration(
    const std::string& identity) const
{
    const auto found = identities_.find(identity);
    return found == identities_.end() ? std::nullopt
                                      : std::optional { found->second };
}

std::string VpiClassModel::identity(const std::size_t index) const
{
    return sv::class_declaration_identity(class_at(index));
}

bool VpiClassModel::owns(const std::uint64_t serial) const noexcept
{
    return serial < entries_.size();
}

fsim_vpi_handle_v1 VpiClassModel::make(const Kind kind,
    const std::size_t declaration, const std::size_t specialization,
    const std::size_t member)
{
    const auto key = std::tuple { static_cast<std::uint8_t>(kind), declaration,
        specialization, member };
    if (const auto found = index_.find(key); found != index_.end()) {
        return hooks_.handle(found->second);
    }
    entries_.push_back(Entry { kind, declaration, specialization, member });
    index_.emplace(key, entries_.size() - 1U);
    return hooks_.handle(entries_.size() - 1U);
}

fsim_vpi_handle_v1 VpiClassModel::typespec_of(
    const std::size_t declaration, const std::size_t specialization)
{
    return make(Kind::typespec, declaration, specialization, 0U);
}

// The specialization a bare class name denotes (every parameter at its
// default), none when the class has no specialization of its own.
std::size_t VpiClassModel::default_specialization(
    const std::size_t declaration) const
{
    const auto found = specialized_.find(identity(declaration));
    if (found == specialized_.end()) {
        return no_index;
    }
    for (const auto index : found->second) {
        const auto& specialization = specializations_[index];
        if (specialization.default_specialization
            && specialization.generate_environment.empty()) {
            return index;
        }
    }
    return no_index;
}

fsim_vpi_handle_v1 VpiClassModel::extends_of(
    const std::size_t declaration, const std::size_t specialization)
{
    const auto& source = class_at(declaration);
    const bool has_base = specialization == no_index
        ? source.base.has_value()
        : specializations_[specialization].base.has_value();
    return has_base ? make(Kind::extends, declaration, specialization, 0U) : 0U;
}

fsim_vpi_handle_v1 VpiClassModel::defn_of(const std::size_t declaration) const
{
    return hooks_.defn(identity(declaration));
}

std::string VpiClassModel::typespec_name(
    const std::size_t declaration, const std::size_t specialization) const
{
    const auto& source = class_at(declaration);
    if (specialization == no_index) {
        return source.name;
    }
    const auto& specialized = specializations_[specialization];
    if (specialized.default_specialization || specialized.parameters.empty()) {
        return source.name;
    }
    // The name of a specialization is tool-dependent (37.32 detail 4):
    // the class name and its parameter actuals.
    std::string name = source.name + "#(";
    for (std::size_t index = 0U; index < specialized.parameters.size(); ++index) {
        if (index != 0U) {
            name += ", ";
        }
        // A type actual reads as it was spelled (`byte`), not as its
        // canonical identity.
        const auto& parameter = specialized.parameters[index];
        name += parameter.type_parameter && parameter.type
                && !parameter.type->target.spelling.empty()
            ? parameter.type->target.spelling
            : parameter.display_identity;
    }
    return name + ")";
}

std::string VpiClassModel::defn_full_name(const std::size_t declaration) const
{
    const auto handle = defn_of(declaration);
    return handle != 0U ? hooks_.full_name(handle) : identity(declaration);
}

std::int64_t VpiClassModel::class_type_of(const std::string_view name)
{
    if (name == "mailbox") {
        return vpiMailboxClass;
    }
    if (name == "semaphore") {
        return vpiSemaphoreClass;
    }
    if (name == "process") {
        return vpiProcessClass;
    }
    return vpiUserDefinedClass;
}

fsim_vpi_handle_v1 VpiClassModel::typespec_for_name(
    const std::string_view spelling)
{
    std::string text { spelling };
    if (const auto exact = find_declaration(text)) {
        return typespec_of(*exact, default_specialization(*exact));
    }
    if (const auto exact = specialization_index_.find(text);
        exact != specialization_index_.end()) {
        if (const auto owner = find_declaration(
                specializations_[exact->second].declaration_identity)) {
            return typespec_of(*owner, exact->second);
        }
    }
    const auto parameters = text.find('#');
    const bool parameterized = parameters != std::string::npos;
    if (parameterized) {
        text.resize(parameters);
    }
    text = without_spaces(text);
    const auto separator = text.rfind("::");
    const auto qualifier
        = separator == std::string::npos ? std::string { } : text.substr(0U, separator);
    const auto name
        = separator == std::string::npos ? text : text.substr(separator + 2U);
    const auto found = by_name_.find(name);
    if (found == by_name_.end() || found->second.empty()) {
        return 0U;
    }
    auto chosen = found->second.front();
    if (!qualifier.empty()) {
        for (const auto candidate : found->second) {
            if (identity(candidate).find(qualifier) != std::string::npos) {
                chosen = candidate;
                break;
            }
        }
    }
    if (parameterized) {
        // `Box#(4)`: the specialization named so, when there is one.
        const auto at = spelling.rfind(name);
        const auto wanted = without_spaces(
            at == std::string_view::npos ? spelling : spelling.substr(at));
        const auto owned = specialized_.find(identity(chosen));
        if (owned != specialized_.end()) {
            for (const auto index : owned->second) {
                if (without_spaces(typespec_name(chosen, index)) == wanted) {
                    return typespec_of(chosen, index);
                }
            }
        }
        return typespec_of(chosen, no_index);
    }
    return typespec_of(chosen, default_specialization(chosen));
}

PLI_INT32 VpiClassModel::typedef_type(const Entry& entry) const
{
    const auto& source = class_at(entry.declaration);
    const auto* const alias = hir_.find_declaration(source.type_aliases.at(entry.member));
    if (alias == nullptr) {
        return vpiUndefined;
    }
    const sv::TypeDefinition* record = alias->declared_type
        ? hir_.find_type(*alias->declared_type)
        : nullptr;
    const sv::TypeReference* reference
        = alias->type ? &*alias->type : (record ? &record->base : nullptr);
    auto form = sv::TypeForm::unresolved;
    if (record != nullptr) {
        form = record->form;
    } else if (reference != nullptr && reference->value_form) {
        form = *reference->value_form;
    }
    if (form == sv::TypeForm::class_handle
        || (reference != nullptr && !reference->class_identity.empty())) {
        return vpiClassTypespec;
    }
    switch (form) {
    case sv::TypeForm::enumeration:
        return vpiEnumTypespec;
    case sv::TypeForm::packed_structure:
    case sv::TypeForm::unpacked_structure:
        return vpiStructTypespec;
    case sv::TypeForm::packed_union:
    case sv::TypeForm::unpacked_union:
    case sv::TypeForm::tagged_union:
        return vpiUnionTypespec;
    case sv::TypeForm::string:
        return vpiStringTypespec;
    case sv::TypeForm::dynamic_array:
    case sv::TypeForm::queue:
    case sv::TypeForm::associative_array:
    case sv::TypeForm::static_array:
        return vpiArrayTypespec;
    case sv::TypeForm::packed_integral: {
        const auto word = reference != nullptr
            ? leading_word(reference->target.spelling)
            : std::string { };
        if (word == "int") {
            return vpiIntTypespec;
        }
        if (word == "byte") {
            return vpiByteTypespec;
        }
        if (word == "shortint") {
            return vpiShortIntTypespec;
        }
        if (word == "longint") {
            return vpiLongIntTypespec;
        }
        if (word == "integer") {
            return vpiIntegerTypespec;
        }
        if (word == "time") {
            return vpiTimeTypespec;
        }
        if (word == "bit") {
            return vpiBitTypespec;
        }
        return vpiLogicTypespec;
    }
    default:
        return vpiUndefined;
    }
}

// The class defn a typedef of a class names; 0 for any other typedef.
fsim_vpi_handle_v1 VpiClassModel::typedef_class(const Entry& entry)
{
    const auto& source = class_at(entry.declaration);
    const auto* const alias = hir_.find_declaration(source.type_aliases.at(entry.member));
    if (alias == nullptr || typedef_type(entry) != vpiClassTypespec) {
        return 0U;
    }
    const sv::TypeDefinition* record = alias->declared_type
        ? hir_.find_type(*alias->declared_type)
        : nullptr;
    const sv::TypeReference* reference
        = alias->type ? &*alias->type : (record ? &record->base : nullptr);
    if (reference == nullptr) {
        return 0U;
    }
    const auto spelling = reference->class_identity.empty()
        ? reference->target.spelling
        : reference->class_identity;
    const auto typespec = typespec_for_name(spelling);
    if (typespec == 0U) {
        return 0U;
    }
    const auto& found = entries_.at(typespec & ((std::uint64_t { 1 } << 48U) - 1U));
    return defn_of(found.declaration);
}

PLI_INT32 VpiClassModel::type(const std::uint64_t serial) const
{
    const auto& entry = entries_.at(serial);
    switch (entry.kind) {
    case Kind::typespec:
        return vpiClassTypespec;
    case Kind::extends:
        return vpiExtends;
    case Kind::method: {
        const auto& method = class_at(entry.declaration).methods.at(entry.member);
        return method.kind == sv::ClassMethodKind::task ? vpiTask : vpiFunction;
    }
    case Kind::parameter:
        return vpiParameter;
    case Kind::class_typedef:
        return typedef_type(entry);
    case Kind::constraint:
        return vpiConstraint;
    case Kind::constraint_item:
        return item_type(nodes_.at(entry.member));
    }
    return vpiUndefined;
}

std::optional<std::pair<std::string, std::string>> VpiClassModel::typespec_class(
    const std::uint64_t serial) const
{
    const auto& entry = entries_.at(serial);
    if (entry.kind != Kind::typespec) {
        return std::nullopt;
    }
    return std::pair { identity(entry.declaration),
        entry.specialization == no_index
            ? std::string { }
            : specializations_[entry.specialization].specialization_identity };
}

std::optional<std::pair<std::string, std::string>>
VpiClassModel::constraint_identity(const std::uint64_t serial) const
{
    const auto& entry = entries_.at(serial);
    if (entry.kind != Kind::constraint) {
        return std::nullopt;
    }
    const auto& constraint = class_at(entry.declaration).constraints.at(entry.member);
    return std::pair { constraint.canonical_identity, constraint.name };
}

std::optional<std::int64_t> VpiClassModel::property(
    const std::uint64_t serial, const PLI_INT32 property) const
{
    const auto& entry = entries_.at(serial);
    const auto& source = class_at(entry.declaration);
    switch (entry.kind) {
    case Kind::typespec:
        if (property == vpiClassType) {
            return vpiUserDefinedClass;
        }
        if (property == vpiAutomatic) {
            return automatic(source.lifetime) ? 1 : 0;
        }
        if (property == vpiLineNo) {
            const auto place = place_of(semantics_, source.source);
            return place ? std::optional<std::int64_t> { place->line }
                         : std::nullopt;
        }
        return std::nullopt;
    case Kind::method: {
        const auto& method = source.methods.at(entry.member);
        switch (property) {
        case vpiMethod:
            return 1;
        case vpiVirtual:
            return method.virtual_method ? 1 : 0;
        case vpiVisibility:
            return visibility_of(method.visibility);
        case vpiAutomatic:
            return automatic(method.lifetime) ? 1 : 0;
        case vpiLineNo: {
            const auto place = place_of(semantics_, method.source);
            return place ? std::optional<std::int64_t> { place->line }
                         : std::nullopt;
        }
        default:
            return std::nullopt;
        }
    }
    case Kind::parameter:
        // The parameters of the parameter port list are not local (37.31
        // detail 7).
        if (property == vpiLocalParam) {
            return 0;
        }
        return std::nullopt;
    // A constraint (37.34): vpiAutomatic reflects the keyword of its
    // declaration (detail 1), vpiAccessType whether it was declared out of
    // the class (detail 3). vpiVirtual is left undefined: the LRM does not
    // say whether a pure constraint is virtual.
    case Kind::constraint: {
        const auto& constraint = source.constraints.at(entry.member);
        switch (property) {
        case vpiAutomatic:
            return constraint.static_constraint ? 0 : 1;
        case vpiAccessType:
            return constraint.external ? vpiExternAcc : 0;
        case vpiAllocScheme:
            return vpiOtherScheme;
        case vpiIsConstraintEnabled:
            return constraint.defined && !constraint.pure
                    && !constraint.external
                ? 1
                : 0;
        case vpiLineNo: {
            const auto place = place_of(semantics_, constraint.source);
            return place ? std::optional<std::int64_t> { place->line }
                         : std::nullopt;
        }
        default:
            return std::nullopt;
        }
    }
    case Kind::constraint_item:
        return item_property(nodes_.at(entry.member), property);
    case Kind::extends:
    case Kind::class_typedef:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> VpiClassModel::string_property(
    const std::uint64_t serial, const PLI_INT32 property) const
{
    const auto& entry = entries_.at(serial);
    const auto& source = class_at(entry.declaration);
    switch (entry.kind) {
    case Kind::typespec:
        if (property == vpiName) {
            return typespec_name(entry.declaration, entry.specialization);
        }
        if (property == vpiFile) {
            const auto place = place_of(semantics_, source.source);
            return place ? std::optional { place->file } : std::nullopt;
        }
        return std::nullopt;
    case Kind::method: {
        const auto& method = source.methods.at(entry.member);
        if (property == vpiName) {
            return method.name;
        }
        if (property == vpiFullName) {
            return defn_full_name(entry.declaration) + "." + method.name;
        }
        if (property == vpiFile) {
            const auto place = place_of(semantics_, method.source);
            return place ? std::optional { place->file } : std::nullopt;
        }
        return std::nullopt;
    }
    case Kind::parameter: {
        const auto& parameter = source.parameters.at(entry.member);
        if (property == vpiName) {
            return parameter.name;
        }
        if (property == vpiFullName) {
            return defn_full_name(entry.declaration) + "." + parameter.name;
        }
        return std::nullopt;
    }
    case Kind::class_typedef: {
        const auto* const alias
            = hir_.find_declaration(source.type_aliases.at(entry.member));
        if (alias != nullptr && property == vpiName) {
            return alias->name;
        }
        return std::nullopt;
    }
    case Kind::constraint: {
        const auto& constraint = source.constraints.at(entry.member);
        if (property == vpiName) {
            return constraint.name;
        }
        if (property == vpiFullName) {
            return defn_full_name(entry.declaration) + "." + constraint.name;
        }
        if (property == vpiFile) {
            const auto place = place_of(semantics_, constraint.source);
            return place ? std::optional { place->file } : std::nullopt;
        }
        return std::nullopt;
    }
    // The tree keeps no source text, so a leaf is only decompiled.
    case Kind::constraint_item:
        if (property == vpiDecompile) {
            return decompile(*nodes_.at(entry.member).expression);
        }
        return std::nullopt;
    case Kind::extends:
        return std::nullopt;
    }
    return std::nullopt;
}

fsim_vpi_handle_v1 VpiClassModel::relation(
    const std::uint64_t serial, const PLI_INT32 type)
{
    const auto entry = entries_.at(serial);
    switch (entry.kind) {
    case Kind::typespec:
        if (type == vpiClassDefn) {
            return defn_of(entry.declaration);
        }
        if (type == vpiExtends) {
            return extends_of(entry.declaration, entry.specialization);
        }
        if (type == vpiParent || type == vpiScope) {
            return defn_of(entry.declaration);
        }
        return 0U;
    case Kind::extends: {
        // The base class typespec; a specialization's base is also a
        // specialization (37.32 detail 9).
        if (type == vpiClassTypespec) {
            const auto& source = class_at(entry.declaration);
            if (entry.specialization != no_index) {
                const auto& specialized = specializations_[entry.specialization];
                if (specialized.base) {
                    const auto found = specialization_index_.find(
                        specialized.base->specialization_identity);
                    const auto owner
                        = find_declaration(specialized.base->declaration_identity);
                    if (found != specialization_index_.end() && owner) {
                        return typespec_of(*owner, found->second);
                    }
                    if (owner) {
                        return typespec_of(*owner, no_index);
                    }
                }
                return 0U;
            }
            if (source.base) {
                if (const auto owner
                    = find_declaration(source.base->declaration_identity)) {
                    return typespec_of(*owner, no_index);
                }
            }
            return 0U;
        }
        if (type == vpiParent) {
            return entry.specialization == no_index
                ? defn_of(entry.declaration)
                : typespec_of(entry.declaration, entry.specialization);
        }
        return 0U;
    }
    case Kind::method:
    case Kind::parameter:
        return type == vpiParent || type == vpiScope ? defn_of(entry.declaration)
                                                     : 0U;
    case Kind::class_typedef:
        if (type == vpiClassDefn) {
            return typedef_class(entry);
        }
        return type == vpiParent || type == vpiScope ? defn_of(entry.declaration)
                                                     : 0U;
    case Kind::constraint:
    case Kind::constraint_item:
        return constraint_relation(entry, type);
    }
    return 0U;
}

std::vector<fsim_vpi_handle_v1> VpiClassModel::members_of(
    const std::size_t declaration, const std::size_t specialization,
    const PLI_INT32 type)
{
    std::vector<fsim_vpi_handle_v1> objects;
    const auto& source = class_at(declaration);
    switch (type) {
    // A method declared out of the class body is listed at its extern
    // prototype (37.31 detail 4).
    case vpiMethods:
        for (std::size_t index = 0U; index < source.methods.size(); ++index) {
            if (!source.methods[index].out_of_block_definition) {
                objects.push_back(
                    make(Kind::method, declaration, specialization, index));
            }
        }
        break;
    case vpiParameter:
        for (std::size_t index = 0U; index < source.parameters.size(); ++index) {
            objects.push_back(
                make(Kind::parameter, declaration, specialization, index));
        }
        [[fallthrough]];
    case vpiVariables:
    case vpiNamedEvent:
    case vpiNamedEventArray: {
        auto declared = hooks_.members(identity(declaration), type);
        objects.insert(objects.end(), declared.begin(), declared.end());
        break;
    }
    case vpiTypedef:
        if (specialization == no_index) {
            for (std::size_t index = 0U; index < source.type_aliases.size();
                 ++index) {
                objects.push_back(make(Kind::class_typedef, declaration, no_index, index));
            }
        }
        break;
    // In syntactic declaration order (37.34 detail 4).
    case vpiConstraint:
        for (std::size_t index = 0U; index < source.constraints.size(); ++index) {
            objects.push_back(
                make(Kind::constraint, declaration, specialization, index));
        }
        break;
    default:
        break;
    }
    return objects;
}

std::optional<std::vector<fsim_vpi_handle_v1>> VpiClassModel::iterate(
    const std::uint64_t serial, const PLI_INT32 type)
{
    const auto entry = entries_.at(serial);
    if (entry.kind == Kind::constraint || entry.kind == Kind::constraint_item) {
        return constraint_iterate(entry, type);
    }
    if (entry.kind != Kind::typespec) {
        return std::vector<fsim_vpi_handle_v1> { };
    }
    // A lexical typespec supports none of the member relations (37.32
    // detail 2).
    if (entry.specialization == no_index) {
        return std::vector<fsim_vpi_handle_v1> { };
    }
    if (type == vpiVariables || type == vpiMethods || type == vpiParameter
        || type == vpiNamedEvent || type == vpiNamedEventArray
        || type == vpiConstraint || type == vpiTypedef
        || type == vpiInternalScope) {
        return members_of(entry.declaration, entry.specialization, type);
    }
    return std::vector<fsim_vpi_handle_v1> { };
}

std::optional<std::int64_t> VpiClassModel::defn_property(
    const std::string& identity, const PLI_INT32 property) const
{
    const auto owner = find_declaration(identity);
    if (!owner) {
        return std::nullopt;
    }
    const auto& source = class_at(*owner);
    if (property == vpiVirtual) {
        return source.virtual_class ? 1 : 0;
    }
    if (property == vpiAutomatic) {
        return automatic(source.lifetime) ? 1 : 0;
    }
    return std::nullopt;
}

fsim_vpi_handle_v1 VpiClassModel::defn_relation(
    const std::string& identity, const PLI_INT32 type)
{
    const auto owner = find_declaration(identity);
    if (!owner || type != vpiExtends) {
        return 0U;
    }
    return extends_of(*owner, no_index);
}

std::optional<std::vector<fsim_vpi_handle_v1>> VpiClassModel::defn_iterate(
    const std::string& identity, const PLI_INT32 type)
{
    const auto owner = find_declaration(identity);
    if (!owner) {
        return std::nullopt;
    }
    std::vector<fsim_vpi_handle_v1> objects;
    switch (type) {
    // The classes derived directly from this one (37.31 detail 5).
    case vpiDerivedClasses: {
        const auto& classes = hir_.classes();
        for (std::size_t index = 0U; index < classes.size(); ++index) {
            if (classes[index].base
                && classes[index].base->declaration_identity == identity) {
                if (const auto derived = defn_of(index); derived != 0U) {
                    objects.push_back(derived);
                }
            }
        }
        return objects;
    }
    // The specializations of this class (37.32 detail 10).
    case vpiClassTypespec: {
        const auto found = specialized_.find(identity);
        if (found != specialized_.end()) {
            for (const auto index : found->second) {
                if (specializations_[index].generate_environment.empty()) {
                    objects.push_back(typespec_of(*owner, index));
                }
            }
        }
        return objects;
    }
    // The classes a class declares are its scopes.
    case vpiInternalScope:
        return hooks_.members(identity, vpiClassDefn);
    case vpiMethods:
    case vpiParameter:
    case vpiVariables:
    case vpiNamedEvent:
    case vpiNamedEventArray:
    case vpiTypedef:
    case vpiConstraint:
        return members_of(*owner, no_index, type);
    default:
        return std::nullopt;
    }
}

std::optional<std::int64_t> VpiClassModel::member_property(
    const std::string& owner, const std::string& name, const PLI_INT32 property)
{
    const auto index = find_declaration(owner);
    if (!index) {
        return std::nullopt;
    }
    for (const auto& member : class_at(*index).properties) {
        if (member.name != name) {
            continue;
        }
        switch (property) {
        case vpiVisibility:
            return visibility_of(member.visibility);
        case vpiRandType:
            return member.random_kind == sv::ClassRandomKind::rand ? vpiRand
                : member.random_kind == sv::ClassRandomKind::randc ? vpiRandC
                                                                   : vpiNotRand;
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// --- constraints (IEEE 1800-2023 37.34, 37.38) -----------------------------

fsim_vpi_handle_v1 VpiClassModel::item_of(const Entry& owner,
    const std::size_t constraint, const std::size_t parent,
    const sv::ConstraintExpression& expression, const bool soft)
{
    const auto key = std::pair { &expression, owner.specialization };
    auto found = node_index_.find(key);
    if (found == node_index_.end()) {
        nodes_.push_back(Node { &expression, owner.declaration,
            owner.specialization, constraint, parent, soft });
        found = node_index_.emplace(key, nodes_.size() - 1U).first;
    }
    return make(Kind::constraint_item, owner.declaration, owner.specialization,
        found->second);
}

void VpiClassModel::collect_items(const Entry& owner,
    const std::size_t constraint, const std::size_t parent,
    const sv::ConstraintExpression& set, const bool soft,
    std::vector<fsim_vpi_handle_v1>& out)
{
    using CKind = sv::ConstraintExpressionKind;
    switch (set.kind) {
    case CKind::constraint_block:
        for (const auto& operand : set.operands) {
            collect_items(owner, constraint, parent, operand, false, out);
        }
        return;
    case CKind::soft:
        if (set.operands.size() == 1U) {
            collect_items(owner, constraint, parent, set.operands.front(), true,
                out);
        }
        return;
    // The LRM has no object for a unique constraint.
    case CKind::unique_constraint:
        return;
    default:
        out.push_back(item_of(owner, constraint, parent, set, soft));
    }
}

PLI_INT32 VpiClassModel::item_type(const Node& node) const
{
    using CKind = sv::ConstraintExpressionKind;
    const auto& expression = *node.expression;
    switch (expression.kind) {
    case CKind::implication:
        return vpiImplication;
    case CKind::conditional_constraint:
        return expression.operands.size() == 3U ? vpiConstrIfElse : vpiConstrIf;
    case CKind::foreach_constraint:
        return vpiConstrForEach;
    case CKind::distribution:
        return vpiDistribution;
    case CKind::distribution_item:
        return vpiDistItem;
    case CKind::solve_before:
        return vpiConstraintOrdering;
    default:
        return disable_soft(expression) ? vpiSoftDisable : leaf_type(expression);
    }
}

std::optional<std::int64_t> VpiClassModel::item_property(
    const Node& node, const PLI_INT32 property) const
{
    const auto& expression = *node.expression;
    switch (property) {
    case vpiSoft:
        return node.soft ? 1 : 0;
    case vpiDistType:
        if (expression.kind == sv::ConstraintExpressionKind::distribution_item) {
            return expression.text == "@dist-:/" ? vpiDivDist : vpiEqualDist;
        }
        return std::nullopt;
    case vpiOpType:
        if (expression.kind == sv::ConstraintExpressionKind::name
            && expression.text.empty()) {
            return vpiNullOp;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

// The loop variables of a foreach constraint and the array it indexes, from
// its selection `array[i][j]` (37.38 details 1, 2).
namespace {

struct ForeachSelection {
    const sv::ConstraintExpression* array { };
    std::vector<const sv::ConstraintExpression*> variables;
};

[[nodiscard]] ForeachSelection foreach_selection(
    const sv::ConstraintExpression& expression)
{
    ForeachSelection found;
    if (expression.operands.empty()) {
        return found;
    }
    const auto* selection = &expression.operands.front();
    while (selection->kind == sv::ConstraintExpressionKind::index
        && selection->operands.size() == 2U) {
        found.variables.push_back(&selection->operands[1]);
        selection = &selection->operands[0];
    }
    found.array = selection;
    std::ranges::reverse(found.variables);
    return found;
}

} // namespace

fsim_vpi_handle_v1 VpiClassModel::constraint_relation(
    const Entry& entry, const PLI_INT32 type)
{
    using CKind = sv::ConstraintExpressionKind;
    if (entry.kind == Kind::constraint) {
        if (type == vpiParent || type == vpiScope) {
            return entry.specialization == no_index
                ? defn_of(entry.declaration)
                : typespec_of(entry.declaration, entry.specialization);
        }
        return 0U;
    }
    // A copy: new items grow `nodes_`.
    const auto node = nodes_.at(entry.member);
    const auto& expression = *node.expression;
    const auto& operands = expression.operands;
    const Entry owner { Kind::constraint, node.declaration, node.specialization,
        node.constraint };
    const auto child = [&](const sv::ConstraintExpression& operand) {
        return item_of(owner, node.constraint, entry.member, operand, false);
    };
    switch (type) {
    case vpiParent:
        return node.parent == none
            ? make(Kind::constraint, node.declaration, node.specialization,
                node.constraint)
            : make(Kind::constraint_item, node.declaration, node.specialization,
                node.parent);
    case vpiCondition:
        if ((expression.kind == CKind::implication
                || expression.kind == CKind::conditional_constraint)
            && !operands.empty()) {
            return child(operands.front());
        }
        break;
    // The array a foreach constraint indexes (37.38 detail 1).
    case vpiVariables:
        if (expression.kind == CKind::foreach_constraint) {
            const auto selection = foreach_selection(expression);
            if (selection.array != nullptr) {
                return child(*selection.array);
            }
        }
        break;
    case vpiExpr:
        if (expression.kind == CKind::distribution && !operands.empty()) {
            return child(operands.front());
        }
        break;
    case vpiValueRange:
        if (expression.kind == CKind::distribution_item && operands.size() == 2U) {
            return child(operands[0]);
        }
        break;
    case vpiWeight:
        if (expression.kind == CKind::distribution_item && operands.size() == 2U) {
            return child(operands[1]);
        }
        break;
    default:
        break;
    }
    return 0U;
}

std::vector<fsim_vpi_handle_v1> VpiClassModel::constraint_iterate(
    const Entry& entry, const PLI_INT32 type)
{
    using CKind = sv::ConstraintExpressionKind;
    std::vector<fsim_vpi_handle_v1> out;
    if (entry.kind == Kind::constraint) {
        if (type == vpiConstraintItem) {
            const auto& constraint
                = class_at(entry.declaration).constraints.at(entry.member);
            for (const auto& expression : constraint.expressions) {
                collect_items(
                    entry, entry.member, none, expression, false, out);
            }
        }
        return out;
    }
    const auto node = nodes_.at(entry.member);
    const auto& expression = *node.expression;
    const auto& operands = expression.operands;
    const Entry owner { Kind::constraint, node.declaration, node.specialization,
        node.constraint };
    const auto leaf = [&](const sv::ConstraintExpression& operand) {
        out.push_back(
            item_of(owner, node.constraint, entry.member, operand, false));
    };
    switch (type) {
    // The items of the consequent, the taken branch or the loop body (37.38
    // detail 3), and of the else branch.
    case vpiConstraintExpr:
        if (operands.size() >= 2U
            && (expression.kind == CKind::implication
                || expression.kind == CKind::conditional_constraint
                || expression.kind == CKind::foreach_constraint)) {
            collect_items(owner, node.constraint, entry.member, operands[1],
                false, out);
        }
        break;
    case vpiElseConst:
        if (expression.kind == CKind::conditional_constraint
            && operands.size() == 3U) {
            collect_items(owner, node.constraint, entry.member, operands[2],
                false, out);
        }
        break;
    // In left-to-right order (37.38 detail 2).
    case vpiLoopVars:
        if (expression.kind == CKind::foreach_constraint) {
            for (const auto* variable : foreach_selection(expression).variables) {
                leaf(*variable);
            }
        }
        break;
    case vpiVariables:
        if (expression.kind == CKind::foreach_constraint) {
            if (const auto* array = foreach_selection(expression).array) {
                leaf(*array);
            }
        }
        break;
    case vpiSolveBefore:
    case vpiSolveAfter:
        if (expression.kind == CKind::solve_before && operands.size() == 2U) {
            const auto& list = operands[type == vpiSolveBefore ? 0U : 1U];
            for (const auto& operand : list.operands) {
                leaf(operand);
            }
        }
        break;
    case vpiDistItem:
        if (expression.kind == CKind::distribution) {
            for (std::size_t index = 1U; index < operands.size(); ++index) {
                leaf(operands[index]);
            }
        }
        break;
    default:
        break;
    }
    return out;
}

} // namespace fsim::app::application_detail
