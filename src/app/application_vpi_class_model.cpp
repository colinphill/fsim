// SPDX-License-Identifier: Apache-2.0
#include "application_vpi_class_model.hpp"

#include "application_vpi_operator_type.hpp"

#include <algorithm>
#include <iterator>
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


// An internal marker call (`@inside-range`, `@dist-:=`, ...) is no
// function call.
[[nodiscard]] bool marker_call(const sv::ConstraintExpression& expression)
{
    return expression.text.starts_with('@');
}

// The operator of an operation leaf (IEEE 1800-2023 37.78 vpiOpType); empty
// for a leaf that is no operation.
[[nodiscard]] std::optional<PLI_INT32> operation_type(
    const sv::ConstraintExpression& expression)
{
    using Kind = sv::ConstraintExpressionKind;
    switch (expression.kind) {
    case Kind::name:
        // A skipped foreach index variable is a null operation (37.38
        // detail 2).
        return expression.text.empty() ? std::optional { vpiNullOp }
                                       : std::nullopt;
    case Kind::concatenation:
        return vpiConcatOp;
    case Kind::replication:
        return vpiMultiConcatOp;
    case Kind::assignment_pattern:
        return vpiAssignmentPatternOp;
    case Kind::conditional:
        return vpiConditionOp;
    case Kind::inside_set:
        return vpiInsideOp;
    case Kind::unary:
    case Kind::binary: {
        // `x++` and `--x` keep the frontend's `post++` spelling.
        if (expression.text == "pre++") {
            return vpiPreIncOp;
        }
        if (expression.text == "post++") {
            return vpiPostIncOp;
        }
        if (expression.text == "pre--") {
            return vpiPreDecOp;
        }
        if (expression.text == "post--") {
            return vpiPostDecOp;
        }
        const auto found
            = operator_type(expression.text, expression.operands.size());
        return found ? found : std::optional { vpiNullOp };
    }
    default:
        return std::nullopt;
    }
}

// The member a member select `a[i].m` names (7.2): the HIR keeps it as the
// marker call `@sv-select:m` of its prefix; none for any other expression.
[[nodiscard]] std::optional<std::string_view> selected_member(
    const sv::ConstraintExpression& expression)
{
    constexpr std::string_view prefix { "@sv-select:" };
    if (expression.kind != sv::ConstraintExpressionKind::call
        || expression.operands.size() != 1U
        || !expression.text.starts_with(prefix)) {
        return std::nullopt;
    }
    return std::string_view { expression.text }.substr(prefix.size());
}

} // namespace

// The unpacked dimensions a type has, outermost first, each of which an
// index selects an element of (a fixed-size dimension, or a dynamic array,
// queue or associative array; 7.4, 7.5, 7.8, 7.9, 7.10), through the
// element types of nested containers and typedefs of arrays (6.18).
VpiClassModel::SelectShape VpiClassModel::select_shape(
    const sv::TypeReference& type) const
{
    auto unpacked = type.unpacked_dimensions.size();
    if (type.container_form && unpacked == 0U) {
        unpacked = 1U;
    }
    if (!type.container_element_types.empty()) {
        const auto inner = select_shape(type.container_element_types.front());
        return { inner.element, unpacked + inner.unpacked };
    }
    if (unpacked == 0U && type.target.target.valid()) {
        if (const auto* definition = hir_.find_type(type.target.target);
            definition != nullptr && definition->form == sv::TypeForm::alias) {
            return select_shape(definition->base);
        }
    }
    return { &type, unpacked };
}

// The shape of a leaf an index or member select applies to: a property
// (the declaration its binding names exactly), an element of one, or a
// member of a structure (7.2, 7.4); none for anything else.
std::optional<VpiClassModel::SelectShape> VpiClassModel::leaf_shape(
    const sv::ConstraintExpression& expression) const
{
    using Kind = sv::ConstraintExpressionKind;
    if (expression.kind == Kind::name) {
        const auto binding = std::ranges::find(expression.bindings,
            sv::ConstraintReferenceKind::property, &sv::ConstraintBinding::kind);
        if (binding == expression.bindings.end()) {
            return std::nullopt;
        }
        for (const auto& declaration : hir_.classes()) {
            const auto property = std::ranges::find(declaration.properties,
                binding->canonical_identity, &sv::ClassProperty::canonical_identity);
            if (property != declaration.properties.end()) {
                return select_shape(property->type);
            }
        }
        return std::nullopt;
    }
    // The result of a method call has the declared return type of the
    // method its binding names (13.4.1: a function may return an unpacked
    // array through a typedef).
    const auto method_binding = std::ranges::find(expression.bindings,
        sv::ConstraintReferenceKind::method, &sv::ConstraintBinding::kind);
    if (expression.kind == Kind::call && method_binding != expression.bindings.end()) {
        const auto binding = method_binding;
        for (const auto& declaration : hir_.classes()) {
            const auto method = std::ranges::find(declaration.methods,
                binding->canonical_identity, &sv::ClassMethod::canonical_identity);
            if (method == declaration.methods.end()) {
                continue;
            }
            const auto* const callable = hir_.find_declaration(method->declaration);
            if (callable == nullptr || !callable->callable) {
                return std::nullopt;
            }
            return select_shape(callable->callable->return_type);
        }
        return std::nullopt;
    }
    if (expression.kind == Kind::index && expression.operands.size() == 2U) {
        const auto base = leaf_shape(expression.operands.front());
        if (!base || base->unpacked == 0U) {
            return std::nullopt;
        }
        return SelectShape { base->element, base->unpacked - 1U };
    }
    if (const auto member = selected_member(expression)) {
        const auto base = leaf_shape(expression.operands.front());
        if (!base || base->unpacked != 0U || base->element == nullptr
            || !base->element->target.target.valid()) {
            return std::nullopt;
        }
        const auto* definition = hir_.find_type(base->element->target.target);
        while (definition != nullptr && definition->form == sv::TypeForm::alias
            && definition->base.target.target.valid()) {
            definition = hir_.find_type(definition->base.target.target);
        }
        if (definition == nullptr) {
            return std::nullopt;
        }
        const auto found = std::ranges::find(
            definition->members, *member, &sv::PackedMember::name);
        if (found == definition->members.end()) {
            return std::nullopt;
        }
        return select_shape(found->type);
    }
    return std::nullopt;
}

// The type of a leaf expression of a constraint (37.34, 37.38): the HIR
// keeps the expression tree, not the VPI expression objects. An index
// selects an element (a var select) while its prefix has unpacked
// dimensions left, and a bit of a packed value (a bit select) afterwards
// or when the prefix has no declared type the model knows; the result of a
// method call has the method's declared return type; a member select of a structure is a ref obj named as written
// (owner 2026-11-12).
PLI_INT32 VpiClassModel::leaf_type(const sv::ConstraintExpression& expression) const
{
    using Kind = sv::ConstraintExpressionKind;
    if (selected_member(expression)) {
        return vpiRefObj;
    }
    switch (expression.kind) {
    case Kind::integer_literal:
    case Kind::boolean_literal:
    case Kind::logic_literal:
    case Kind::string_literal:
        return vpiConstant;
    case Kind::name:
        return expression.text.empty() ? vpiOperation : vpiRefObj;
    case Kind::inside_range:
        return vpiRange;
    // A bit, element or part select (37.19).
    case Kind::index: {
        if (expression.operands.size() != 2U) {
            return vpiOperation;
        }
        const auto base = leaf_shape(expression.operands.front());
        return base && base->unpacked != 0U ? vpiVarSelect : vpiBitSelect;
    }
    case Kind::slice:
        if (expression.operands.size() != 3U) {
            return vpiOperation;
        }
        return expression.text == ":" ? vpiPartSelect : vpiIndexedPartSelect;
    // A function call (37.77): a system function when the name starts with
    // `$`.
    case Kind::call:
        if (marker_call(expression)) {
            return vpiOperation;
        }
        return expression.text.starts_with('$') ? vpiSysFuncCall : vpiFuncCall;
    default:
        return vpiOperation;
    }
}

namespace {

// vpiConstType of a literal leaf (37.59).
[[nodiscard]] PLI_INT32 constant_type(const sv::ConstraintExpression& expression)
{
    using Kind = sv::ConstraintExpressionKind;
    switch (expression.kind) {
    case Kind::string_literal:
        return vpiStringConst;
    case Kind::boolean_literal:
    case Kind::logic_literal:
        return vpiBinaryConst;
    default:
        break;
    }
    const auto quote = expression.text.find('\'');
    if (quote == std::string::npos) {
        return vpiIntConst;
    }
    auto rest = std::string_view { expression.text }.substr(quote + 1U);
    if (!rest.empty() && (rest.front() == 's' || rest.front() == 'S')) {
        rest.remove_prefix(1U);
    }
    const auto base = rest.empty() ? 'd'
                                   : static_cast<char>(std::tolower(
                                       static_cast<unsigned char>(rest.front())));
    return base == 'o' ? vpiOctConst
        : base == 'h'  ? vpiHexConst
        : base == 'd'  ? vpiDecConst
                       : vpiBinaryConst;
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
    if (const auto member = selected_member(expression)) {
        return decompile(operands.front()) + "." + std::string { *member };
    }
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
    case Kind::slice:
        if (operands.size() == 3U) {
            return decompile(operands[0]) + "[" + decompile(operands[1])
                + expression.text + decompile(operands[2]) + "]";
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

// The bit size of a numeric literal (37.59 vpiSize): the written size of a
// sized literal, 32 for an unsized integer or based literal, one for an
// unbased unsized fill (`'1`, 5.7.1).
[[nodiscard]] std::optional<std::int64_t> literal_size(
    const sv::ConstraintExpression& expression)
{
    const auto quote = expression.text.find('\'');
    if (expression.kind == sv::ConstraintExpressionKind::boolean_literal) {
        return 1;
    }
    if (expression.kind == sv::ConstraintExpressionKind::integer_literal) {
        return expression.text.find_first_of(".eE") == std::string::npos
            ? std::optional<std::int64_t> { 32 }
            : std::nullopt;
    }
    if (quote == std::string::npos) {
        return std::nullopt;
    }
    std::int64_t size { };
    bool digits = false;
    for (const auto character : expression.text.substr(0U, quote)) {
        if (character >= '0' && character <= '9') {
            size = size * 10 + (character - '0');
            digits = true;
        }
    }
    if (digits) {
        return size;
    }
    const auto rest = expression.text.substr(quote + 1U);
    const bool based = !rest.empty()
        && std::string_view { "sSbBoOdDhH" }.find(rest.front())
            != std::string_view::npos;
    return based ? 32 : 1;
}

// Whether a select index is a constant expression (37.19 vpiConstantSelect):
// a literal, a parameter, or an operation over them.
[[nodiscard]] bool constant_index(const sv::ConstraintExpression& expression)
{
    using Kind = sv::ConstraintExpressionKind;
    switch (expression.kind) {
    case Kind::integer_literal:
    case Kind::boolean_literal:
    case Kind::logic_literal:
    case Kind::string_literal:
        return true;
    case Kind::name:
        return !expression.bindings.empty()
            && expression.bindings.front().kind
            == sv::ConstraintReferenceKind::parameter;
    case Kind::unary:
    case Kind::binary:
    case Kind::conditional:
        return std::ranges::all_of(expression.operands, constant_index);
    default:
        return false;
    }
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

VpiClassModel::ParameterKey VpiClassModel::parameter_key_of(
    const std::size_t declaration, const std::size_t specialization,
    const std::size_t index) const
{
    const auto& parameter = class_at(declaration).parameters.at(index);
    // The class of a generated scope or instance has its source
    // specialization's parameters.
    return ParameterKey { identity(declaration),
        specialization == no_index
            ? std::string { }
            : std::string { sv::generated_specialization_source(
                  specializations_[specialization].specialization_identity) },
        parameter.name, parameter.type_parameter };
}

std::optional<VpiClassModel::ParameterKey> VpiClassModel::parameter_key(
    const std::uint64_t serial) const
{
    if (!owns(serial)) {
        return std::nullopt;
    }
    const auto& entry = entries_.at(serial);
    if (entry.kind == Kind::body_parameter) {
        return ParameterKey { identity(entry.declaration),
            entry.specialization == no_index
                ? std::string { }
                : std::string { sv::generated_specialization_source(
                      specializations_[entry.specialization]
                          .specialization_identity) },
            class_at(entry.declaration).properties.at(entry.member).name,
            false };
    }
    if (entry.kind != Kind::parameter) {
        return std::nullopt;
    }
    return parameter_key_of(entry.declaration, entry.specialization, entry.member);
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

std::optional<semantic::ExpressionId> VpiClassModel::lexical_actual(
    const std::size_t use, const std::size_t base, const std::size_t index) const
{
    const auto& parameters = class_at(base).parameters;
    if (use < uses_.size()) {
        const auto& actuals = *uses_[use];
        for (std::size_t at = 0U; at < actuals.size(); ++at) {
            const auto& actual = actuals[at];
            const bool match = actual.formal
                ? *actual.formal == parameters.at(index).name
                : at == index;
            if (match && actual.kind == sv::ActualKind::expression
                && actual.expression) {
                return actual.expression;
            }
        }
    }
    return parameters.at(index).default_value;
}

std::size_t VpiClassModel::use_of(
    const std::vector<sv::ActualAssociation>& actuals)
{
    const auto [found, added] = use_index_.try_emplace(&actuals, uses_.size());
    if (added) {
        uses_.push_back(&actuals);
    }
    return found->second + 1U;
}

fsim_vpi_handle_v1 VpiClassModel::item_typespec(
    const std::string& owner, const std::string& name)
{
    const auto declaration = find_declaration(owner);
    if (!declaration) {
        return 0U;
    }
    const auto& source = class_at(*declaration);
    for (const auto& property : source.properties) {
        if (property.name != name
            || property.owner_identity != source.canonical_identity) {
            continue;
        }
        const auto base = property.type.class_identity.empty()
            ? std::nullopt
            : find_declaration(property.type.class_identity);
        if (!base) {
            return 0U;
        }
        return make(Kind::typespec, *base, no_index,
            use_of(property.type.interface_parameter_actuals));
    }
    return 0U;
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

fsim_vpi_handle_v1 VpiClassModel::typespec_for_identity(
    const std::string& identity)
{
    if (const auto exact = find_declaration(identity)) {
        return typespec_of(*exact, default_specialization(*exact));
    }
    if (const auto exact = specialization_index_.find(identity);
        exact != specialization_index_.end()) {
        if (const auto owner = find_declaration(
                specializations_[exact->second].declaration_identity)) {
            return typespec_of(*owner, exact->second);
        }
    }
    return 0U;
}

const sv::TypeReference* VpiClassModel::typedef_reference(
    const Entry& entry) const
{
    const auto& source = class_at(entry.declaration);
    const auto* const alias
        = hir_.find_declaration(source.type_aliases.at(entry.member));
    if (alias == nullptr) {
        return nullptr;
    }
    const sv::TypeDefinition* record = alias->declared_type
        ? hir_.find_type(*alias->declared_type)
        : nullptr;
    return alias->type ? &*alias->type : (record ? &record->base : nullptr);
}

// A class typedef entry is a typedef naming a class: the lexical class
// typespec of the user-defined typedef (37.32 detail 1). The class's other
// typedefs are the typespecs the elaborator recorded.
PLI_INT32 VpiClassModel::typedef_type(const Entry& entry) const
{
    const auto* const reference = typedef_reference(entry);
    return reference != nullptr && !reference->class_identity.empty()
        ? vpiClassTypespec
        : vpiUndefined;
}

// The class defn a typedef of a class names; 0 for any other typedef.
fsim_vpi_handle_v1 VpiClassModel::typedef_class(const Entry& entry)
{
    const auto* const reference = typedef_reference(entry);
    if (reference == nullptr || reference->class_identity.empty()) {
        return 0U;
    }
    const auto base = find_declaration(reference->class_identity);
    return base ? defn_of(*base) : 0U;
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
    // A type parameter is an object of its own kind (37.28).
    case Kind::parameter:
        return class_at(entry.declaration).parameters.at(entry.member).type_parameter
            ? vpiTypeParameter
            : vpiParameter;
    case Kind::body_parameter:
        return vpiParameter;
    case Kind::variable:
        return hooks_.variable_type
            ? hooks_.variable_type(entry.member, identity(entry.declaration),
                  specializations_[entry.specialization].specialization_identity)
            : vpiUndefined;
    case Kind::param_assign:
        return vpiParamAssign;
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

semantic::DeclarationId VpiClassModel::method_body_declaration(
    const semantic::DeclarationId declaration) const
{
    // An extern prototype's body is its out-of-block definition, the method
    // of the same class with the same canonical identity (IEEE 1800-2023
    // 8.24).
    for (const auto& item : hir_.classes()) {
        const auto prototype = std::ranges::find(
            item.methods, declaration, &sv::ClassMethod::declaration);
        if (prototype == item.methods.end()) {
            continue;
        }
        if (!prototype->external) {
            return declaration;
        }
        const auto definition = std::ranges::find_if(item.methods,
            [&](const sv::ClassMethod& candidate) {
                return candidate.out_of_block_definition
                    && candidate.canonical_identity
                        == prototype->canonical_identity;
            });
        return definition == item.methods.end() ? declaration
                                                : definition->declaration;
    }
    return declaration;
}

std::optional<semantic::DeclarationId> VpiClassModel::method_body(
    const std::uint64_t serial) const
{
    if (!owns(serial)) {
        return std::nullopt;
    }
    const auto& entry = entries_.at(serial);
    if (entry.kind != Kind::method) {
        return std::nullopt;
    }
    return method_body_declaration(
        class_at(entry.declaration).methods.at(entry.member).declaration);
}

std::optional<sv::ClassMethodKind> VpiClassModel::method_kind(
    const std::uint64_t serial) const
{
    if (!owns(serial)) {
        return std::nullopt;
    }
    const auto& entry = entries_.at(serial);
    if (entry.kind != Kind::method) {
        return std::nullopt;
    }
    return class_at(entry.declaration).methods.at(entry.member).kind;
}

fsim_vpi_handle_v1 VpiClassModel::method_object(
    const semantic::DeclarationId declaration)
{
    const auto& classes = hir_.classes();
    for (std::size_t index = 0U; index < classes.size(); ++index) {
        const auto& methods = classes[index].methods;
        const auto found = std::ranges::find(
            methods, declaration, &sv::ClassMethod::declaration);
        if (found == methods.end()) {
            continue;
        }
        // An out-of-block definition is listed at its extern prototype.
        auto listed = found;
        if (found->out_of_block_definition) {
            listed = std::ranges::find_if(methods,
                [&](const sv::ClassMethod& candidate) {
                    return !candidate.out_of_block_definition
                        && candidate.canonical_identity
                            == found->canonical_identity;
                });
            if (listed == methods.end()) {
                return 0U;
            }
        }
        return make(Kind::method, index, no_index,
            static_cast<std::size_t>(std::distance(methods.begin(), listed)));
    }
    return 0U;
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

const sv::ConstraintExpression* VpiClassModel::leaf_expression(
    const std::uint64_t serial) const
{
    const auto& entry = entries_.at(serial);
    if (entry.kind != Kind::constraint_item) {
        return nullptr;
    }
    const auto& node = nodes_.at(entry.member);
    return item_type(node) == vpiConstant ? node.expression : nullptr;
}

std::optional<std::string> VpiClassModel::leaf_property(
    const std::uint64_t serial) const
{
    if (!owns(serial)) {
        return std::nullopt;
    }
    const auto& entry = entries_.at(serial);
    if (entry.kind != Kind::constraint_item) {
        return std::nullopt;
    }
    const auto& node = nodes_.at(entry.member);
    if (node.expression == nullptr
        || node.expression->kind != sv::ConstraintExpressionKind::name) {
        return std::nullopt;
    }
    const auto binding = std::ranges::find(node.expression->bindings,
        sv::ConstraintReferenceKind::property, &sv::ConstraintBinding::kind);
    return binding == node.expression->bindings.end()
        ? std::nullopt
        : std::optional { binding->canonical_identity };
}

std::optional<std::int64_t> VpiClassModel::property(
    const std::uint64_t serial, const PLI_INT32 property) const
{
    const auto& entry = entries_.at(serial);
    const auto& source = class_at(entry.declaration);
    switch (entry.kind) {
    // An automatic property of a specialization (37.32 detail 5).
    case Kind::variable:
        return hooks_.variable_property
            ? hooks_.variable_property(entry.member, identity(entry.declaration),
                  specializations_[entry.specialization].specialization_identity,
                  property)
            : std::nullopt;
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
    // A parameter of the class body is local (37.31 detail 7).
    case Kind::body_parameter:
        if (property == vpiLocalParam) {
            return 1;
        }
        return std::nullopt;
    // A constraint (37.34): vpiAutomatic reflects the keyword of its
    // declaration (detail 1), vpiAccessType whether it was declared out of
    // the class (detail 3). vpiVirtual is true for a pure constraint, the
    // constraint counterpart of a pure virtual method (18.5.2: an obligation
    // on every derived class); the LRM gives constraints no `virtual`
    // keyword, so no other constraint is virtual.
    case Kind::constraint: {
        const auto& constraint = source.constraints.at(entry.member);
        switch (property) {
        case vpiAutomatic:
            return constraint.static_constraint ? 0 : 1;
        case vpiVirtual:
            return constraint.pure ? 1 : 0;
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
    case Kind::param_assign:
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
    // An automatic property of a specialization has its declared name and,
    // like a non-static data member of an object, no full name (37.33
    // detail 9): it is reached through the typespec, not a scope.
    case Kind::variable:
        if (property == vpiName) {
            return hooks_.name ? std::optional { hooks_.name(entry.member) }
                               : std::nullopt;
        }
        return std::nullopt;
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
    case Kind::body_parameter: {
        const auto& parameter = source.properties.at(entry.member);
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
        // A name and a function call are named like their expression
        // (37.76, 37.77).
        if (property == vpiName) {
            const auto& expression = *nodes_.at(entry.member).expression;
            const auto kind = leaf_type(expression);
            if (kind == vpiRefObj && selected_member(expression)) {
                return decompile(expression);
            }
            if (kind == vpiRefObj || kind == vpiFuncCall
                || kind == vpiSysFuncCall) {
                return expression.text;
            }
        }
        return std::nullopt;
    case Kind::extends:
    case Kind::param_assign:
        return std::nullopt;
    }
    return std::nullopt;
}

fsim_vpi_handle_v1 VpiClassModel::relation(
    const std::uint64_t serial, const PLI_INT32 type)
{
    const auto entry = entries_.at(serial);
    switch (entry.kind) {
    // An automatic property of a specialization: its parent and scope are
    // the class typespec it was obtained from (37.33 detail 5 note), its
    // typespec the one the
    // specialization gives its declared type (37.32, 37.17).
    case Kind::variable:
        if (type == vpiParent || type == vpiScope) {
            return typespec_of(entry.declaration, entry.specialization);
        }
        if (type == vpiTypespec) {
            return hooks_.variable_typespec
                ? hooks_.variable_typespec(entry.member,
                      identity(entry.declaration),
                      specializations_[entry.specialization]
                          .specialization_identity)
                : 0U;
        }
        return 0U;
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
            // The base typespec of this class's `extends` keeps the use,
            // whose actuals are its parameter assignments (37.32 detail 3).
            if (source.base) {
                if (const auto owner
                    = find_declaration(source.base->declaration_identity)) {
                    return make(Kind::typespec, *owner, no_index,
                        use_of(source.base->actuals));
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
    // A parameter's typespec is its declared type, a type parameter's the
    // type it names (37.28), as the elaborator recorded them.
    case Kind::parameter:
        if (type == vpiTypespec) {
            const auto key = parameter_key_of(
                entry.declaration, entry.specialization, entry.member);
            return hooks_.parameter_typespec
                ? hooks_.parameter_typespec(
                      key.declaration, key.specialization, key.name)
                : 0U;
        }
        return type == vpiParent || type == vpiScope ? defn_of(entry.declaration)
                                                     : 0U;
    case Kind::body_parameter:
        if (type == vpiTypespec) {
            const auto key = parameter_key(serial);
            return key && hooks_.parameter_typespec
                ? hooks_.parameter_typespec(
                      key->declaration, key->specialization, key->name)
                : 0U;
        }
        return type == vpiParent || type == vpiScope ? defn_of(entry.declaration)
                                                     : 0U;
    case Kind::method:
        return type == vpiParent || type == vpiScope ? defn_of(entry.declaration)
                                                     : 0U;
    case Kind::class_typedef:
        if (type == vpiClassDefn) {
            return typedef_class(entry);
        }
        return type == vpiParent || type == vpiScope ? defn_of(entry.declaration)
                                                     : 0U;
    // The parameter an assignment sets and the actual it sets it to (37.32
    // vpiParamAssign; 37.28 vpiLhs, vpiRhs); a parameter left at its default
    // has no assignment.
    case Kind::param_assign: {
        // A lexical assignment's member holds the use (the deriving class,
        // plus one) above the parameter's index.
        const auto parameter = entry.member % param_assign_stride;
        const auto use = entry.member / param_assign_stride;
        if (type == vpiLhs) {
            return make(Kind::parameter, entry.declaration,
                entry.specialization, parameter);
        }
        // The Rhs of a type parameter's assignment is the typespec of the
        // type it is bound to (37.28).
        if (type == vpiRhs && entry.specialization != no_index
            && specializations_[entry.specialization]
                   .parameters.at(parameter)
                   .type_parameter) {
            const auto key = parameter_key_of(
                entry.declaration, entry.specialization, parameter);
            return hooks_.parameter_typespec
                ? hooks_.parameter_typespec(
                      key.declaration, key.specialization, key.name)
                : 0U;
        }
        if (type == vpiRhs && hooks_.expression) {
            if (entry.specialization != no_index) {
                const auto& actual = specializations_[entry.specialization]
                                         .parameters.at(parameter);
                return actual.expression
                    ? hooks_.expression(*actual.expression, hooks_.handle(serial))
                    : 0U;
            }
            const auto expression
                = use != 0U ? lexical_actual(use - 1U, entry.declaration, parameter)
                            : std::nullopt;
            return expression
                ? hooks_.expression(*expression, hooks_.handle(serial))
                : 0U;
        }
        if (type == vpiParent) {
            return entry.specialization == no_index && use != 0U
                ? make(Kind::typespec, entry.declaration, no_index, use)
                : typespec_of(entry.declaration, entry.specialization);
        }
        return 0U;
    }
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
    // The assignments of a class specialization (37.32 detail 3): one per
    // parameter, the object of a value parameter's Rhs being any object
    // with the right value, here the actual or default expression it was
    // bound to, and a type parameter's the typespec of its type; none for a
    // lexical typespec, whose Rhs is the use's explicit actual.
    case vpiParamAssign:
        if (specialization != no_index) {
            const auto& actuals = specializations_[specialization].parameters;
            for (std::size_t index = 0U; index < actuals.size(); ++index) {
                if ((actuals[index].type_parameter ? actuals[index].type.has_value()
                                                   : actuals[index].expression.has_value())
                    && index < source.parameters.size()) {
                    objects.push_back(make(
                        Kind::param_assign, declaration, specialization, index));
                }
            }
        }
        break;
    case vpiParameter:
        for (std::size_t index = 0U; index < source.parameters.size(); ++index) {
            objects.push_back(
                make(Kind::parameter, declaration, specialization, index));
        }
        // A specialization's body parameters have the values they take in
        // it (8.25), so they are objects of its own; the defn's are the
        // registry's.
        if (specialization != no_index) {
            for (std::size_t index = 0U; index < source.properties.size();
                 ++index) {
                const auto& property = source.properties[index];
                if (property.parameter
                    && property.owner_identity == source.canonical_identity) {
                    objects.push_back(make(Kind::body_parameter, declaration,
                        specialization, index));
                }
            }
            break;
        }
        [[fallthrough]];
    case vpiVariables:
    case vpiNamedEvent:
    case vpiNamedEventArray: {
        auto declared = hooks_.members(identity(declaration), type);
        if (specialization == no_index) {
            objects.insert(objects.end(), declared.begin(), declared.end());
            break;
        }
        // A specialization's properties are its own (37.32 detail 5): a
        // static one holds the specialization's value, an automatic one
        // none (detail 6).
        const auto& specialized
            = specializations_[specialization].specialization_identity;
        const auto typespec = typespec_of(declaration, specialization);
        for (const auto property : declared) {
            const auto member = hooks_.specialization_member
                ? hooks_.specialization_member(
                      property, identity(declaration), specialized, typespec)
                : fsim_vpi_handle_v1 { };
            objects.push_back(member != 0U
                    ? member
                    : make(Kind::variable, declaration, specialization,
                          static_cast<std::size_t>(property)));
        }
        break;
    }
    // In declaration order: a typedef naming a class is a lexical class
    // typespec in the defn (37.32 detail 1); every other typedef, and each
    // of a specialization, is the typespec the elaborator recorded for it
    // there (37.25).
    case vpiTypedef:
        for (std::size_t index = 0U; index < source.type_aliases.size();
             ++index) {
            const Entry alias { Kind::class_typedef, declaration, no_index,
                index };
            if (specialization == no_index
                && typedef_type(alias) == vpiClassTypespec) {
                objects.push_back(
                    make(Kind::class_typedef, declaration, no_index, index));
                continue;
            }
            const auto* const declared
                = hir_.find_declaration(source.type_aliases[index]);
            if (declared == nullptr || !hooks_.typedef_typespec) {
                continue;
            }
            if (const auto handle = hooks_.typedef_typespec(
                    identity(declaration),
                    specialization == no_index
                        ? std::string { }
                        : specializations_[specialization]
                              .specialization_identity,
                    declared->name);
                handle != 0U) {
                objects.push_back(handle);
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
    // A class typedef is a lexical class typespec (37.32 detail 1): its
    // parameter assignments are the actuals the typedef wrote, else the
    // defaults (detail 3); it supports no member relation (detail 2).
    if (entry.kind == Kind::class_typedef) {
        std::vector<fsim_vpi_handle_v1> assignments;
        const auto* const reference = typedef_reference(entry);
        const auto base = reference == nullptr
                || reference->class_identity.empty()
            ? std::nullopt
            : find_declaration(reference->class_identity);
        if (type == vpiParamAssign && base) {
            const auto use = use_of(reference->interface_parameter_actuals);
            const auto& parameters = class_at(*base).parameters;
            for (std::size_t index = 0U; index < parameters.size(); ++index) {
                if (!parameters[index].type_parameter
                    && lexical_actual(use - 1U, *base, index)) {
                    assignments.push_back(make(Kind::param_assign, *base,
                        no_index, use * param_assign_stride + index));
                }
            }
        }
        return assignments;
    }
    // The arguments `extends B(args)` passes to the base constructor, in
    // source order (IEEE 1800-2023 8.17, 37.32 vpiArgument).
    if (entry.kind == Kind::extends && type == vpiArgument) {
        std::vector<fsim_vpi_handle_v1> arguments;
        const auto& source = class_at(entry.declaration);
        if (source.base && hooks_.expression) {
            const auto self
                = extends_of(entry.declaration, entry.specialization);
            // The formals of the base constructor, from the nearest class
            // of the base chain that declares one; a named argument takes
            // its formal's place, and the arguments come in formal order
            // (8.17, 13.5.4). Defaults the call leaves out are not listed.
            std::vector<std::string> formals;
            auto base = find_declaration(source.base->declaration_identity);
            std::vector<std::size_t> followed_bases;
            while (base && formals.empty()) {
                // A class chain that returns to a class already followed
                // (a class extending itself) ends the walk.
                if (std::ranges::find(followed_bases, *base)
                    != followed_bases.end()) {
                    break;
                }
                followed_bases.push_back(*base);
                const auto& declared = class_at(*base);
                for (const auto& method : declared.methods) {
                    if (method.kind != semantic::sv::ClassMethodKind::constructor) {
                        continue;
                    }
                    const auto* const callable
                        = hir_.find_declaration(method.declaration);
                    if (callable == nullptr || !callable->callable) {
                        continue;
                    }
                    for (const auto formal : callable->callable->formals) {
                        if (const auto* const record
                            = hir_.find_declaration(formal)) {
                            formals.push_back(record->name);
                        }
                    }
                    break;
                }
                base = declared.base
                    ? find_declaration(declared.base->declaration_identity)
                    : std::nullopt;
            }
            const auto& names = source.base->constructor_argument_names;
            std::vector<std::pair<std::size_t, semantic::ExpressionId>> bound;
            for (std::size_t index = 0U;
                 index < source.base->constructor_arguments.size(); ++index) {
                std::size_t slot = index;
                if (index < names.size() && !names[index].empty()
                    && !formals.empty()) {
                    const auto found
                        = std::ranges::find(formals, names[index]);
                    if (found == formals.end()) {
                        continue;
                    }
                    slot = static_cast<std::size_t>(found - formals.begin());
                }
                bound.emplace_back(
                    slot, source.base->constructor_arguments[index]);
            }
            std::ranges::stable_sort(bound, {},
                &std::pair<std::size_t, semantic::ExpressionId>::first);
            for (const auto& [slot, id] : bound) {
                static_cast<void>(slot);
                if (const auto handle = hooks_.expression(id, self);
                    handle != 0U) {
                    arguments.push_back(handle);
                }
            }
        }
        return arguments;
    }
    if (entry.kind != Kind::typespec) {
        return std::vector<fsim_vpi_handle_v1> { };
    }
    // A lexical typespec supports none of the member relations (37.32
    // detail 2), but the parameter assignments of the use it came from
    // (detail 3): the explicit actuals of the `extends`, else the defaults.
    if (entry.specialization == no_index) {
        std::vector<fsim_vpi_handle_v1> assignments;
        if (type == vpiParamAssign && entry.member != 0U) {
            const auto& parameters = class_at(entry.declaration).parameters;
            for (std::size_t index = 0U; index < parameters.size(); ++index) {
                if (!parameters[index].type_parameter
                    && lexical_actual(entry.member - 1U, entry.declaration, index)) {
                    assignments.push_back(make(Kind::param_assign,
                        entry.declaration, no_index,
                        entry.member * param_assign_stride + index));
                }
            }
        }
        return assignments;
    }
    if (type == vpiVariables || type == vpiMethods || type == vpiParameter
        || type == vpiParamAssign || type == vpiNamedEvent || type == vpiNamedEventArray
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
        return item_type(node) == vpiOperation ? operation_type(expression)
                                               : std::nullopt;
    // A select with only constant indexes (37.19).
    case vpiConstantSelect: {
        const auto selection = item_type(node);
        if (selection != vpiBitSelect && selection != vpiVarSelect
            && selection != vpiPartSelect && selection != vpiIndexedPartSelect) {
            return std::nullopt;
        }
        return std::ranges::all_of(
                   std::span { expression.operands }.subspan(1U),
                   [](const sv::ConstraintExpression& index) {
                       return constant_index(index);
                   })
            ? 1
            : 0;
    }
    case vpiIndexedPartSelectType:
        if (item_type(node) != vpiIndexedPartSelect) {
            return std::nullopt;
        }
        return expression.text == "+:" ? vpiPosIndexed : vpiNegIndexed;
    case vpiConstType:
        return item_type(node) == vpiConstant
            ? std::optional<std::int64_t> { constant_type(expression) }
            : std::nullopt;
    case vpiSize:
        // A string literal is eight bits a character (37.59).
        if (expression.kind == sv::ConstraintExpressionKind::string_literal) {
            return static_cast<std::int64_t>(8U * expression.text.size());
        }
        if (item_type(node) == vpiConstant) {
            return literal_size(expression);
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
    const auto selection = item_type(node);
    const bool bit_or_var = selection == vpiBitSelect || selection == vpiVarSelect;
    const bool part = selection == vpiPartSelect;
    const bool indexed_part = selection == vpiIndexedPartSelect;
    switch (type) {
    // The parent of a select is the expression selected (37.19).
    case vpiParent:
        if ((bit_or_var || part || indexed_part) && !operands.empty()) {
            return child(operands.front());
        }
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
    // The index of a bit select (37.19).
    case vpiIndex:
        if (bit_or_var && operands.size() == 2U) {
            return child(operands[1]);
        }
        break;
    // The base and the width of an indexed part select (37.19).
    case vpiBaseExpr:
    case vpiWidthExpr:
        if (indexed_part) {
            return child(operands[type == vpiBaseExpr ? 1U : 2U]);
        }
        break;
    // The array a foreach constraint indexes (37.38 detail 1).
    case vpiVariables:
        if (expression.kind == CKind::foreach_constraint) {
            const auto indexed = foreach_selection(expression);
            if (indexed.array != nullptr) {
                return child(*indexed.array);
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
    // The bounds of a range leaf (37.62).
    case vpiLeftRange:
    case vpiRightRange:
        if (expression.kind == CKind::inside_range && operands.size() == 2U) {
            return child(operands[type == vpiLeftRange ? 0U : 1U]);
        }
        if (part) {
            return child(operands[type == vpiLeftRange ? 1U : 2U]);
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
    // The indexes of a var select (37.19): one for each select level.
    case vpiIndex:
        if (item_type(node) == vpiVarSelect && operands.size() == 2U) {
            leaf(operands[1]);
        }
        break;
    // The operands of an operation (37.78) and the arguments of a function
    // call (37.77), in source order.
    case vpiOperand:
        if (leaf_type(expression) == vpiOperation) {
            for (const auto& operand : operands) {
                leaf(operand);
            }
        }
        break;
    case vpiArgument:
        if (leaf_type(expression) == vpiFuncCall
            || leaf_type(expression) == vpiSysFuncCall) {
            for (const auto& operand : operands) {
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
