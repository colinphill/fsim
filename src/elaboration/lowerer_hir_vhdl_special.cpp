// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "fsim/frontend/parser.hpp"

namespace fsim::elaboration {
namespace {

struct SelectedVhdlName {
    std::string_view object;
    std::string_view member;
};

std::optional<SelectedVhdlName> selected_vhdl_name(
    const std::string_view spelling)
{
    const auto separator = spelling.find_last_of('.');
    if (separator == std::string_view::npos || separator == 0U
        || separator + 1U == spelling.size()) {
        return std::nullopt;
    }
    return SelectedVhdlName {
        spelling.substr(0U, separator),
        spelling.substr(separator + 1U),
    };
}

frontend::VhdlSimulatorApi vhdl_environment_type_api(
    std::string_view spelling)
{
    spelling = semantic::vhdl::without_builtin_prefix(spelling);
    return frontend::vhdl_simulator_api(spelling);
}

bool vhdl_environment_status_api(const frontend::VhdlSimulatorApi api)
{
    using Api = frontend::VhdlSimulatorApi;
    return api == Api::dir_open_status
        || api == Api::dir_create_status
        || api == Api::dir_delete_status
        || api == Api::file_delete_status;
}

bool vhdl_assert_function_api(const frontend::VhdlSimulatorApi api)
{
    using Api = frontend::VhdlSimulatorApi;
    return api == Api::is_vhdl_assert_failed
        || api == Api::get_vhdl_assert_count
        || api == Api::get_vhdl_assert_enable
        || api == Api::get_vhdl_assert_format
        || api == Api::get_vhdl_read_severity;
}

bool vhdl_assert_procedure_api(const frontend::VhdlSimulatorApi api)
{
    using Api = frontend::VhdlSimulatorApi;
    return api == Api::clear_vhdl_assert
        || api == Api::set_vhdl_assert_enable
        || api == Api::set_vhdl_assert_format
        || api == Api::set_vhdl_read_severity;
}

bool vhdl_environment_status_literal_name(const std::string_view spelling)
{
    const auto name = simple_name_of(spelling);
    return equal_ignoring_ascii_case(name, "status_ok")
        || equal_ignoring_ascii_case(name, "status_not_found")
        || equal_ignoring_ascii_case(name, "status_no_directory")
        || equal_ignoring_ascii_case(name, "status_item_exists")
        || equal_ignoring_ascii_case(name, "status_not_empty")
        || equal_ignoring_ascii_case(name, "status_no_file")
        || equal_ignoring_ascii_case(name, "status_access_denied")
        || equal_ignoring_ascii_case(name, "status_error");
}

std::optional<std::uint64_t> vhdl_environment_status_ordinal(
    const frontend::VhdlSimulatorApi api,
    const std::string_view spelling)
{
    using Api = frontend::VhdlSimulatorApi;
    const auto name = simple_name_of(spelling);
    if (equal_ignoring_ascii_case(name, "status_ok")) {
        return 0U;
    }
    if (api == Api::dir_open_status) {
        if (equal_ignoring_ascii_case(name, "status_not_found")) {
            return 1U;
        }
        if (equal_ignoring_ascii_case(name, "status_no_directory")) {
            return 2U;
        }
        if (equal_ignoring_ascii_case(name, "status_access_denied")) {
            return 3U;
        }
        if (equal_ignoring_ascii_case(name, "status_error")) {
            return 4U;
        }
        return std::nullopt;
    }
    if (api == Api::dir_create_status) {
        if (equal_ignoring_ascii_case(name, "status_item_exists")) {
            return 1U;
        }
        if (equal_ignoring_ascii_case(name, "status_access_denied")) {
            return 2U;
        }
        if (equal_ignoring_ascii_case(name, "status_error")) {
            return 3U;
        }
        return std::nullopt;
    }
    if (api == Api::dir_delete_status) {
        if (equal_ignoring_ascii_case(name, "status_no_directory")) {
            return 1U;
        }
        if (equal_ignoring_ascii_case(name, "status_not_empty")) {
            return 2U;
        }
        if (equal_ignoring_ascii_case(name, "status_access_denied")) {
            return 3U;
        }
        if (equal_ignoring_ascii_case(name, "status_error")) {
            return 4U;
        }
        return std::nullopt;
    }
    if (api == Api::file_delete_status) {
        if (equal_ignoring_ascii_case(name, "status_no_file")) {
            return 1U;
        }
        if (equal_ignoring_ascii_case(name, "status_access_denied")) {
            return 2U;
        }
        if (equal_ignoring_ascii_case(name, "status_error")) {
            return 3U;
        }
    }
    return std::nullopt;
}

bool reflection_mirror_type(const std::string_view spelling)
{
    const auto name = simple_name_of(spelling);
    constexpr auto suffix = std::string_view { "_mirror" };
    return (name.size() >= suffix.size()
            && equal_ignoring_ascii_case(
                name.substr(name.size() - suffix.size()), suffix))
        || equal_ignoring_ascii_case(name, "value_mirror")
        || equal_ignoring_ascii_case(name, "subtype_mirror");
}

bool reflection_mirror_subtype(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::SubtypeIndication& subtype)
{
    if (reflection_mirror_type(subtype.type_mark.spelling)) {
        return true;
    }
    auto type_id = subtype.type_mark.target;
    std::unordered_set<std::uint32_t> visiting;
    while (type_id.valid() && visiting.insert(type_id.value()).second) {
        const auto type = unit.find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            break;
        }
        if (reflection_mirror_type(type->vhdl->name)
            || reflection_mirror_type(
                type->vhdl->base.type_mark.spelling)) {
            return true;
        }
        type_id = type->vhdl->base.type_mark.target;
    }
    return false;
}

std::optional<VhdlReflectionApiKind> reflection_scalar_operation(
    const std::string_view name)
{
    if (equal_ignoring_ascii_case(name, "get_type_class")
        || equal_ignoring_ascii_case(name, "get_value_class")) {
        return VhdlReflectionApiKind::get_type_class;
    }
    if (equal_ignoring_ascii_case(name, "get_subtype_mirror")) {
        return VhdlReflectionApiKind::get_subtype_mirror;
    }
    if (equal_ignoring_ascii_case(name, "to_subtype_mirror")
        || equal_ignoring_ascii_case(name, "to_value_mirror")) {
        return VhdlReflectionApiKind::convert_generic;
    }
    if (equal_ignoring_ascii_case(name, "enumeration_literal")) {
        return VhdlReflectionApiKind::enumeration_literal;
    }
    if (equal_ignoring_ascii_case(name, "pos")) {
        return VhdlReflectionApiKind::pos;
    }
    if (equal_ignoring_ascii_case(name, "left")) {
        return VhdlReflectionApiKind::left;
    }
    if (equal_ignoring_ascii_case(name, "right")) {
        return VhdlReflectionApiKind::right;
    }
    if (equal_ignoring_ascii_case(name, "low")) {
        return VhdlReflectionApiKind::low;
    }
    if (equal_ignoring_ascii_case(name, "high")) {
        return VhdlReflectionApiKind::high;
    }
    if (equal_ignoring_ascii_case(name, "length")) {
        return VhdlReflectionApiKind::length;
    }
    if (equal_ignoring_ascii_case(name, "ascending")) {
        return VhdlReflectionApiKind::ascending;
    }
    if (equal_ignoring_ascii_case(name, "value")) {
        return VhdlReflectionApiKind::value;
    }
    if (equal_ignoring_ascii_case(name, "units_length")) {
        return VhdlReflectionApiKind::units_length;
    }
    if (equal_ignoring_ascii_case(name, "unit_index")) {
        return VhdlReflectionApiKind::unit_index;
    }
    if (equal_ignoring_ascii_case(name, "scale")) {
        return VhdlReflectionApiKind::scale;
    }
    if (equal_ignoring_ascii_case(name, "element_index")) {
        return VhdlReflectionApiKind::record_element_index;
    }
    if (equal_ignoring_ascii_case(name, "dimensions")) {
        return VhdlReflectionApiKind::dimensions;
    }
    if (equal_ignoring_ascii_case(name, "index_subtype")) {
        return VhdlReflectionApiKind::array_index_subtype;
    }
    if (equal_ignoring_ascii_case(name, "designated_subtype")) {
        return VhdlReflectionApiKind::designated_subtype;
    }
    if (equal_ignoring_ascii_case(name, "is_null")) {
        return VhdlReflectionApiKind::is_null;
    }
    if (equal_ignoring_ascii_case(name, "get_file_open_kind")) {
        return VhdlReflectionApiKind::file_open_kind;
    }
    return std::nullopt;
}

std::optional<VhdlReflectionApiKind> reflection_string_operation(
    const std::string_view name)
{
    if (equal_ignoring_ascii_case(name, "simple_name")) {
        return VhdlReflectionApiKind::simple_name;
    }
    if (equal_ignoring_ascii_case(name, "image")) {
        return VhdlReflectionApiKind::image;
    }
    if (equal_ignoring_ascii_case(name, "unit_name")) {
        return VhdlReflectionApiKind::unit_name;
    }
    if (equal_ignoring_ascii_case(name, "element_name")) {
        return VhdlReflectionApiKind::record_element_name;
    }
    if (equal_ignoring_ascii_case(name, "get_file_logical_name")) {
        return VhdlReflectionApiKind::file_logical_name;
    }
    return std::nullopt;
}

std::optional<semantic::DeclarationId> find_vhdl_object(
    const semantic::SpecializedHirUnit& unit,
    const semantic::ScopeId scope,
    const std::string_view name)
{
    semantic::vhdl::Name reference;
    reference.spelling = std::string { name };
    reference.canonical = reference.spelling;
    const semantic::CompiledDeclarationPredicate object
        = [](const semantic::CompiledDeclarationView& candidate) {
              if (candidate.vhdl == nullptr
                  || !candidate.vhdl->subtype) {
                  return false;
              }
              using Form = semantic::vhdl::DeclarationForm;
              const auto form = candidate.vhdl->form;
              return semantic::vhdl::is_value_object_form(form)
                  || form == Form::file || form == Form::alias;
          };
    return semantic::CompiledDesignResolver { unit }
        .resolve_vhdl(reference, scope, object)
        .unique();
}

const semantic::vhdl::Declaration* find_vhdl_declaration(
    const semantic::SpecializedHirUnit& unit,
    const semantic::DeclarationId declaration_id)
{
    const auto specialized = unit.find_declaration(declaration_id);
    if (specialized && specialized->vhdl != nullptr) {
        return specialized->vhdl;
    }
    const auto& declarations = unit.design().vhdl_hir.declarations();
    const auto stored = std::ranges::find(
        declarations, declaration_id,
        &semantic::vhdl::Declaration::id);
    return stored != declarations.end() ? &*stored : nullptr;
}

// A call actual: its formal name (empty when positional) and value; the
// same type as Lowerer::HirVhdlProtectedActual.
using ProtectedActual = std::pair<std::string_view, semantic::ExpressionId>;

// Associates call actuals, given as (formal name or empty, actual), with a
// protected method's formals by name or position and fills the rest from
// the formals' defaults.
std::optional<std::vector<semantic::ExpressionId>> bind_protected_actuals(
    const semantic::SpecializedHirUnit& unit,
    const std::span<const ProtectedActual> call,
    const semantic::vhdl::Declaration& method)
{
    if (!method.callable || call.size() > method.callable->formals.size()) {
        return std::nullopt;
    }
    const auto& formals = method.callable->formals;
    std::vector<semantic::ExpressionId> result(formals.size());
    std::size_t positional { };
    for (const auto& [name, actual] : call) {
        auto formal_index = positional;
        if (name.empty()) {
            while (formal_index < result.size()
                && result[formal_index].valid()) {
                ++formal_index;
            }
            positional = formal_index + 1U;
        } else {
            const auto found = std::ranges::find_if(
                formals, [&](const semantic::DeclarationId formal_id) {
                    const auto* formal = find_vhdl_declaration(
                        unit, formal_id);
                    return formal != nullptr
                        && equal_ignoring_ascii_case(formal->name, name);
                });
            if (found == formals.end()) {
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        }
        if (formal_index >= result.size()
            || result[formal_index].valid()) {
            return std::nullopt;
        }
        result[formal_index] = actual;
    }
    for (std::size_t index { }; index < result.size(); ++index) {
        if (result[index].valid()) {
            continue;
        }
        const auto* formal = find_vhdl_declaration(
            unit, formals[index]);
        if (formal == nullptr || !formal->initializer) {
            return std::nullopt;
        }
        result[index] = *formal->initializer;
    }
    return result;
}

const semantic::vhdl::TypeDefinition* find_protected_body(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::TypeDefinition& public_type)
{
    const semantic::vhdl::TypeDefinition* selected = nullptr;
    for (const auto& stored : unit.design().vhdl_hir.types()) {
        const auto candidate = unit.find_type(stored.id);
        if (!candidate || candidate->vhdl == nullptr
            || candidate->vhdl->form
                != semantic::vhdl::TypeForm::protected_body
            || !equal_ignoring_ascii_case(
                candidate->vhdl->name, public_type.name)) {
            continue;
        }
        if (selected != nullptr && selected->id != candidate->vhdl->id) {
            return nullptr;
        }
        selected = candidate->vhdl;
    }
    return selected;
}

const semantic::vhdl::TypeDefinition* find_protected_type(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::SubtypeIndication& subtype)
{
    if (subtype.type_mark.target.valid()) {
        const auto type = unit.find_type(subtype.type_mark.target);
        if (type && type->vhdl != nullptr
            && (type->vhdl->form
                    == semantic::vhdl::TypeForm::protected_type
                || type->vhdl->form
                    == semantic::vhdl::TypeForm::protected_body)) {
            return type->vhdl;
        }
    }
    const semantic::vhdl::TypeDefinition* selected = nullptr;
    for (const auto& stored : unit.design().vhdl_hir.types()) {
        const auto candidate = unit.find_type(stored.id);
        if (!candidate || candidate->vhdl == nullptr
            || candidate->vhdl->form
                != semantic::vhdl::TypeForm::protected_type
            || !equal_ignoring_ascii_case(
                candidate->vhdl->name, subtype.type_mark.spelling)) {
            continue;
        }
        if (selected != nullptr && selected->id != candidate->vhdl->id) {
            return nullptr;
        }
        selected = candidate->vhdl;
    }
    return selected;
}

// The protected body of a protected type (the type itself when it is one).
const semantic::vhdl::TypeDefinition* protected_body_of(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::TypeDefinition* type)
{
    if (type == nullptr
        || type->form == semantic::vhdl::TypeForm::protected_body) {
        return type;
    }
    return find_protected_body(unit, *type);
}

std::vector<ProtectedActual> protected_call_actuals(
    const semantic::vhdl::Statement& statement)
{
    std::vector<ProtectedActual> result;
    for (const auto& association : statement.procedure_arguments) {
        result.emplace_back(association.formal
                ? association.formal->text()
                : std::string_view { },
            association.actual);
    }
    return result;
}

std::vector<ProtectedActual> protected_call_actuals(
    const semantic::vhdl::Expression& expression)
{
    std::vector<ProtectedActual> result;
    for (std::size_t index { }; index < expression.operands.size(); ++index) {
        result.emplace_back(index < expression.argument_names.size()
                ? std::string_view { expression.argument_names[index] }
                : std::string_view { },
            expression.operands[index]);
    }
    return result;
}

std::optional<ContainerObjectId> find_protected_storage(
    const ContainerObjectBindings& objects,
    const std::string_view object,
    const std::string_view member)
{
    const auto requested = std::string { object } + "."
        + std::string { member };
    const auto direct = objects.find(requested);
    if (direct != objects.end()) {
        return direct->second;
    }
    const auto found = std::ranges::find_if(
        objects, [&](const auto& candidate) {
            return equal_ignoring_ascii_case(candidate.first, requested);
        });
    return found != objects.end()
        ? std::optional { found->second }
        : std::nullopt;
}

bool contains_hir_protected_call(
    const semantic::SpecializedHirUnit& unit,
    const std::span<const semantic::StatementId> statements,
    const bool wait)
{
    for (const auto statement_id : statements) {
        const auto statement = unit.find_statement(statement_id);
        if (!statement || statement->vhdl == nullptr) {
            continue;
        }
        const auto& source = *statement->vhdl;
        if ((wait
                && source.kind
                    == semantic::vhdl::StatementKind::wait_statement)
            || (!wait
                && source.kind
                    == semantic::vhdl::StatementKind::procedure_call)
            || contains_hir_protected_call(unit, source.statements, wait)
            || contains_hir_protected_call(
                unit, source.else_statements, wait)) {
            return true;
        }
        for (const auto& alternative : source.alternatives) {
            if (contains_hir_protected_call(
                    unit, alternative.statements, wait)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

class Lowerer::HirVhdlProtectedMethodScope {
public:
    // Gives a protected method fresh locals and its own scope; the
    // caller's are restored on destruction.
    HirVhdlProtectedMethodScope(
        Lowerer& lowerer, const semantic::vhdl::Declaration& method)
        : lowerer_ { lowerer }
        , registers_ { std::exchange(lowerer.hir_local_registers_, { }) }
        , strings_ { std::exchange(lowerer.hir_local_string_registers_, { }) }
        , containers_ {
            std::exchange(lowerer.hir_local_container_registers_, { })
        }
        , container_types_ {
            std::exchange(lowerer.hir_local_container_types_, { })
        }
        , scope_ { lowerer.hir_process_scope_ }
    {
        lowerer.hir_process_scope_ = method.nested_scope.value_or(method.scope);
        lowerer.active_hir_vhdl_protected_method_ = true;
    }
    HirVhdlProtectedMethodScope(const HirVhdlProtectedMethodScope&) = delete;
    HirVhdlProtectedMethodScope& operator=(
        const HirVhdlProtectedMethodScope&) = delete;
    ~HirVhdlProtectedMethodScope()
    {
        lowerer_.active_hir_vhdl_protected_method_ = false;
        lowerer_.hir_process_scope_ = scope_;
        lowerer_.hir_local_registers_ = std::move(registers_);
        lowerer_.hir_local_string_registers_ = std::move(strings_);
        lowerer_.hir_local_container_registers_ = std::move(containers_);
        lowerer_.hir_local_container_types_ = std::move(container_types_);
    }

private:
    Lowerer& lowerer_;
    decltype(Lowerer::hir_local_registers_) registers_;
    decltype(Lowerer::hir_local_string_registers_) strings_;
    decltype(Lowerer::hir_local_container_registers_) containers_;
    decltype(Lowerer::hir_local_container_types_) container_types_;
    semantic::ScopeId scope_;
};

const semantic::vhdl::TypeDefinition* Lowerer::hir_vhdl_protected_type(
    const semantic::ScopeId scope, const std::string_view object) const
{
    const auto object_id = find_vhdl_object(
        *specialized_hir_unit_, scope, object);
    const auto declaration = object_id
        ? specialized_hir_unit_->find_declaration(*object_id)
        : std::nullopt;
    const auto subtype = declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype
        ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
        : std::nullopt;
    return subtype ? find_protected_type(*specialized_hir_unit_, *subtype)
                   : nullptr;
}

std::optional<std::size_t> Lowerer::hir_vhdl_protected_formal_width(
    const semantic::DeclarationId formal_id) const
{
    const auto* formal = find_vhdl_declaration(
        *specialized_hir_unit_, formal_id);
    const auto subtype = formal != nullptr && formal->subtype
        ? hir_effective_vhdl_subtype(*formal->subtype)
        : std::nullopt;
    return subtype ? vhdl_subtype_width(*subtype) : std::nullopt;
}

std::vector<Lowerer::HirVhdlProtectedCandidate>
Lowerer::hir_vhdl_protected_candidates(
    const semantic::vhdl::TypeDefinition& body,
    const std::string_view member_name, const bool function,
    const std::span<const HirVhdlProtectedActual> call) const
{
    std::vector<HirVhdlProtectedCandidate> matches;
    for (const auto member_id : body.protected_members) {
        const auto* member = find_vhdl_declaration(
            *specialized_hir_unit_, member_id);
        if (member == nullptr
            || member->form
                != (function ? semantic::vhdl::DeclarationForm::function
                             : semantic::vhdl::DeclarationForm::procedure)
            || !member->callable
            || member->callable->function != function
            || !member->callable->defined
            || !equal_ignoring_ascii_case(member->name, member_name)) {
            continue;
        }
        const auto actuals = bind_protected_actuals(
            *specialized_hir_unit_, call, *member);
        if (!actuals) {
            continue;
        }
        bool compatible
            = member->callable->formals.size() == actuals->size();
        for (std::size_t index { };
            compatible && index < actuals->size(); ++index) {
            const auto formal_id = member->callable->formals[index];
            const auto width = hir_vhdl_protected_formal_width(formal_id);
            const auto actual_width = hir_expression_width(
                (*actuals)[index], hir_process_scope_);
            // Protected procedures support only mode-in formals.
            const auto* formal = find_vhdl_declaration(
                *specialized_hir_unit_, formal_id);
            compatible = width
                && (function
                    || formal->direction == semantic::vhdl::Direction::input)
                && (!actual_width || *actual_width == *width);
        }
        if (compatible) {
            matches.push_back({ member, *actuals });
        }
    }
    return matches;
}

std::optional<std::vector<RegisterId>>
Lowerer::lower_hir_vhdl_protected_arguments(
    const semantic::vhdl::Declaration& method,
    const std::span<const semantic::ExpressionId> actuals)
{
    std::vector<RegisterId> arguments;
    for (std::size_t index { }; index < actuals.size(); ++index) {
        const auto width = hir_vhdl_protected_formal_width(
            method.callable->formals[index]);
        const auto value = width
            ? lower_hir_expression(actuals[index], *width)
            : std::nullopt;
        if (!value) {
            return std::nullopt;
        }
        arguments.push_back(*value);
    }
    return arguments;
}

std::optional<std::vector<Lowerer::HirVhdlProtectedStorage>>
Lowerer::load_hir_vhdl_protected_storage(
    const semantic::vhdl::TypeDefinition& body,
    const std::string_view object,
    const semantic::vhdl::Declaration& method,
    const std::span<const RegisterId> arguments, const RegisterId zero,
    const frontend::SourceSpan& span)
{
    std::vector<HirVhdlProtectedStorage> storage;
    for (const auto member_id : body.protected_members) {
        const auto* member = find_vhdl_declaration(
            *specialized_hir_unit_, member_id);
        if (member == nullptr
            || member->form != semantic::vhdl::DeclarationForm::variable) {
            continue;
        }
        const auto member_subtype = member->subtype
            ? hir_effective_vhdl_subtype(*member->subtype)
            : std::nullopt;
        const auto width = member_subtype
            ? vhdl_subtype_width(*member_subtype)
            : std::nullopt;
        const auto object_storage = find_protected_storage(
            container_objects_, object, member->name);
        if (!width || !object_storage
            || *object_storage >= design_.container_object_info_.size()) {
            report(
                "FSIM-ELAB-VHPROTECTED-017",
                "protected private storage for '" + std::string { object }
                    + "." + member->name + "' is unavailable",
                span);
            return std::nullopt;
        }
        const auto container = allocate_container_register(
            design_.container_object_info_[*object_storage].type);
        const auto value = allocate_register(
            *width, vhdl_value_domain(member_subtype->domain));
        process_.operations.emplace_back(
            ReadContainerObject { container, *object_storage });
        process_.operations.emplace_back(
            ContainerRead { value, container, zero, true, false });
        hir_local_registers_.insert_or_assign(member_id.value(), value);
        storage.push_back({ *object_storage, container, value });
    }
    for (std::size_t index { }; index < arguments.size(); ++index) {
        hir_local_registers_.insert_or_assign(
            method.callable->formals[index].value(), arguments[index]);
    }
    return storage;
}

bool Lowerer::is_hir_vhdl_protected_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return false;
    }
    auto* invocation = expression->vhdl;
    if (invocation->kind == semantic::vhdl::ExpressionKind::index
        && invocation->operands.size() >= 2U) {
        const auto designator = specialized_hir_unit_->find_expression(
            invocation->operands.front());
        invocation = designator && designator->vhdl != nullptr
            ? designator->vhdl
            : nullptr;
    }
    if (invocation == nullptr
        || (invocation->kind
                != semantic::vhdl::ExpressionKind::call
            && invocation->kind
                != semantic::vhdl::ExpressionKind::name)) {
        return false;
    }
    const auto selected = selected_vhdl_name(invocation->text);
    if (!selected) {
        return false;
    }
    return hir_vhdl_protected_type(invocation->scope, selected->object)
        != nullptr;
}

bool Lowerer::is_hir_vhdl_protected_statement(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::procedure_call) {
        return false;
    }
    const auto selected = selected_vhdl_name(
        statement->vhdl->procedure.text());
    if (!selected) {
        return false;
    }
    return hir_vhdl_protected_type(statement->vhdl->scope, selected->object)
        != nullptr;
}

std::optional<RegisterId>
Lowerer::lower_hir_vhdl_protected_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    auto* invocation = expression->vhdl;
    semantic::vhdl::Expression indexed_invocation;
    if (invocation->kind == semantic::vhdl::ExpressionKind::index
        && invocation->operands.size() >= 2U) {
        const auto designator = specialized_hir_unit_->find_expression(
            invocation->operands.front());
        if (!designator || designator->vhdl == nullptr) {
            return std::nullopt;
        }
        indexed_invocation = *designator->vhdl;
        indexed_invocation.kind = semantic::vhdl::ExpressionKind::call;
        indexed_invocation.operands.assign(
            std::next(invocation->operands.begin()),
            invocation->operands.end());
        indexed_invocation.argument_names = invocation->argument_names;
        if (indexed_invocation.argument_names.size()
            == invocation->operands.size()) {
            indexed_invocation.argument_names.erase(
                indexed_invocation.argument_names.begin());
        }
        invocation = &indexed_invocation;
    }
    const auto selected = selected_vhdl_name(invocation->text);
    if (!selected) {
        return std::nullopt;
    }
    const auto* body = protected_body_of(*specialized_hir_unit_,
        hir_vhdl_protected_type(invocation->scope, selected->object));
    if (body == nullptr) {
        return std::nullopt;
    }

    const auto matches = hir_vhdl_protected_candidates(*body,
        selected->member, true, protected_call_actuals(*invocation));
    if (matches.size() != 1U) {
        report(
            matches.empty() ? "FSIM-ELAB-VHPROTECTED-012"
                            : "FSIM-ELAB-VHPROTECTED-013",
            matches.empty()
                ? "protected function call '" + expression->vhdl->text
                    + "' matches no public profile"
                : "protected function call '" + expression->vhdl->text
                    + "' is ambiguous",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    const auto& method = *matches.front().declaration;
    if (active_hir_callable_) {
        const auto& frame = hir_callable_frames_[*active_hir_callable_];
        const auto caller = specialized_hir_unit_->find_declaration(
            frame.declaration);
        if (caller && caller->vhdl != nullptr
            && caller->vhdl->callable
            && caller->vhdl->callable->function
            && caller->vhdl->callable->pure
            && method.callable && !method.callable->pure) {
            report(
                "FSIM-ELAB-VHPROTECTED-022",
                "pure VHDL function '" + caller->vhdl->name
                    + "' cannot call impure protected function '"
                    + expression->vhdl->text + "'",
                hir_source_span(expression->vhdl->source));
            return std::nullopt;
        }
    }
    const auto result_subtype = method.subtype
        ? hir_effective_vhdl_subtype(*method.subtype)
        : std::nullopt;
    const auto result_width = result_subtype
        ? vhdl_subtype_width(*result_subtype)
        : std::nullopt;
    if (!result_width || *result_width != expected_width
        || method.statements.size() != 1U) {
        report(
            !result_width || *result_width != expected_width
                ? "FSIM-ELAB-VHPROTECTED-016"
                : "FSIM-ELAB-VHPROTECTED-015",
            !result_width || *result_width != expected_width
                ? "protected function result is incompatible with its context"
                : "a bounded protected function body must contain one direct value-return statement",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    const auto return_statement = specialized_hir_unit_->find_statement(
        method.statements.front());
    if (!return_statement || return_statement->vhdl == nullptr
        || return_statement->vhdl->kind
            != semantic::vhdl::StatementKind::return_statement
        || !return_statement->vhdl->value) {
        report(
            "FSIM-ELAB-VHPROTECTED-015",
            "a bounded protected function body must contain one direct value-return statement",
            hir_source_span(method.source));
        return std::nullopt;
    }
    if (active_hir_vhdl_protected_method_) {
        report(
            "FSIM-ELAB-VHPROTECTED-014",
            "re-entry into a protected method is outside the bounded execution policy",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }

    const auto arguments = lower_hir_vhdl_protected_arguments(
        method, matches.front().actuals);
    if (!arguments) {
        return std::nullopt;
    }

    const HirVhdlProtectedMethodScope method_scope { *this, method };

    const auto zero = load_integer(0, 32U, frontend::ValueDomain::Integer);
    const auto storage = load_hir_vhdl_protected_storage(*body,
        selected->object, method, *arguments, zero,
        hir_source_span(expression->vhdl->source));
    return storage ? lower_hir_expression(
                         *return_statement->vhdl->value, *result_width)
                   : std::nullopt;
}

bool Lowerer::lower_hir_vhdl_protected_statement(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_->find_statement(statement_id);
    const auto selected = statement && statement->vhdl != nullptr
        ? selected_vhdl_name(
              statement->vhdl->procedure.text())
        : std::nullopt;
    if (!statement || statement->vhdl == nullptr || !selected) {
        return false;
    }
    const auto* body = protected_body_of(*specialized_hir_unit_,
        hir_vhdl_protected_type(statement->vhdl->scope, selected->object));
    if (body == nullptr) {
        return false;
    }

    const auto matches = hir_vhdl_protected_candidates(*body,
        selected->member, false, protected_call_actuals(*statement->vhdl));
    if (matches.size() != 1U) {
        const auto spelling = statement->vhdl->procedure.spelling;
        report(
            matches.empty() ? "FSIM-ELAB-VHPROTECTED-018"
                            : "FSIM-ELAB-VHPROTECTED-019",
            matches.empty()
                ? "protected procedure call '" + spelling
                    + "' matches no supported public input profile"
                : "protected procedure call '" + spelling
                    + "' is ambiguous",
            hir_source_span(statement->vhdl->source));
        return true;
    }
    const auto& method = *matches.front().declaration;
    if (active_hir_vhdl_protected_method_) {
        report(
            "FSIM-ELAB-VHPROTECTED-014",
            "re-entry into a protected method is outside the bounded execution policy",
            hir_source_span(statement->vhdl->source));
        return true;
    }
    if (contains_hir_protected_call(
            *specialized_hir_unit_, method.statements, true)) {
        report(
            "FSIM-ELAB-VHPROTECTED-020",
            "a protected method cannot suspend",
            hir_source_span(method.source));
        return true;
    }
    if (contains_hir_protected_call(
            *specialized_hir_unit_, method.statements, false)) {
        report(
            "FSIM-ELAB-VHPROTECTED-021",
            "nested protected procedure calls are outside the bounded non-reentrant policy",
            hir_source_span(method.source));
        return true;
    }

    const auto arguments = lower_hir_vhdl_protected_arguments(
        method, matches.front().actuals);
    if (!arguments) {
        return true;
    }

    const HirVhdlProtectedMethodScope method_scope { *this, method };

    const auto zero = load_integer(0, 32U, frontend::ValueDomain::Integer);
    const auto storage = load_hir_vhdl_protected_storage(*body,
        selected->object, method, *arguments, zero,
        hir_source_span(statement->vhdl->source));
    if (storage && lower_hir_statements(method.statements)) {
        for (const auto& member : *storage) {
            process_.operations.emplace_back(ContainerWrite {
                member.container, zero, member.value, true, false });
            process_.operations.emplace_back(WriteContainerObject {
                member.object, member.container, std::nullopt });
        }
    }
    return true;
}

bool Lowerer::hir_vhdl_time_record_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto subtype = hir_vhdl_expression_subtype(expression_id);
    if (!expression || expression->vhdl == nullptr || !subtype) {
        return false;
    }
    return vhdl_environment_time_record_subtype(
        *specialized_hir_unit_, *subtype, expression->vhdl->scope);
}

bool Lowerer::is_hir_vhdl_environment_status_literal(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    return expression && expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name
        && vhdl_environment_status_literal_name(
            expression->vhdl->text);
}

std::optional<std::uint64_t>
Lowerer::hir_vhdl_environment_status_literal(
    const semantic::ExpressionId expression_id,
    const semantic::vhdl::SubtypeIndication& context) const
{
    if (!is_hir_vhdl_environment_status_literal(expression_id)) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto status_api = [&](const auto& self,
                                const semantic::vhdl::SubtypeIndication&
                                    subtype,
                                std::unordered_set<std::uint32_t>& visiting)
        -> frontend::VhdlSimulatorApi {
        const auto direct = vhdl_environment_type_api(
            subtype.type_mark.spelling);
        if (vhdl_environment_status_api(direct)) {
            return direct;
        }
        if (!subtype.type_mark.target.valid()
            || !visiting.insert(
                    subtype.type_mark.target.value()).second) {
            return frontend::VhdlSimulatorApi::none;
        }
        const auto type = specialized_hir_unit_->find_type(
            subtype.type_mark.target);
        if (!type || type->vhdl == nullptr) {
            return frontend::VhdlSimulatorApi::none;
        }
        const auto named = vhdl_environment_type_api(type->vhdl->name);
        if (vhdl_environment_status_api(named)) {
            return named;
        }
        return self(self, type->vhdl->base, visiting);
    };
    std::unordered_set<std::uint32_t> visiting;
    const auto api = status_api(status_api, context, visiting);
    return vhdl_environment_status_api(api)
        ? vhdl_environment_status_ordinal(
              api, expression->vhdl->text)
        : std::nullopt;
}

bool Lowerer::is_hir_vhdl_environment_directory(
    const semantic::DeclarationId declaration_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype
        || declaration->vhdl->object_class
            != semantic::vhdl::ObjectClass::variable) {
        return false;
    }
    const auto matches = [](const semantic::vhdl::SubtypeIndication& subtype) {
        return vhdl_environment_type_api(subtype.type_mark.spelling)
            == frontend::VhdlSimulatorApi::directory;
    };
    if (matches(*declaration->vhdl->subtype)) {
        return true;
    }
    const auto effective = hir_effective_vhdl_subtype(
        *declaration->vhdl->subtype);
    return effective && matches(*effective);
}

ContainerType Lowerer::hir_vhdl_environment_directory_container_type() const
{
    ContainerType type;
    type.element_kind = ContainerElementKind::String;
    type.element_width = 0U;
    type.element_nominal_type = "@builtin:std.textio.line";
    return type;
}

bool Lowerer::is_hir_vhdl_environment_call_path(
    const semantic::DeclarationId declaration_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype
        || declaration->vhdl->object_class
            != semantic::vhdl::ObjectClass::variable) {
        return false;
    }
    const auto matches = [](const semantic::vhdl::SubtypeIndication& subtype) {
        using Api = frontend::VhdlSimulatorApi;
        const auto api = vhdl_environment_type_api(
            subtype.type_mark.spelling);
        return api == Api::call_path_element
            || api == Api::call_path_vector
            || api == Api::call_path_vector_ptr;
    };
    if (matches(*declaration->vhdl->subtype)) {
        return true;
    }
    const auto effective = hir_effective_vhdl_subtype(
        *declaration->vhdl->subtype);
    return effective && matches(*effective);
}

ContainerType
Lowerer::hir_vhdl_environment_call_path_container_type() const
{
    ContainerType string_member;
    string_member.element_kind = ContainerElementKind::String;
    string_member.element_width = 0U;
    string_member.fixed = true;
    string_member.dimensions = { { 0, 0 } };
    string_member.element_nominal_type = "@builtin:std.textio.line";

    ContainerType line_member;
    line_member.element_kind = ContainerElementKind::Packed;
    line_member.element_width = 64U;
    line_member.two_state = true;
    line_member.signed_elements = true;
    line_member.fixed = true;
    line_member.dimensions = { { 0, 0 } };
    line_member.element_nominal_type = "@builtin:std.standard.positive";

    ContainerType type;
    type.element_kind = ContainerElementKind::Aggregate;
    type.element_width = 0U;
    type.element_nominal_type = "@builtin:std.env.call_path_element";
    type.element_types = {
        string_member,
        string_member,
        string_member,
        line_member,
    };
    type.member_names = {
        "name",
        "file_name",
        "file_path",
        "file_line",
    };
    return type;
}

std::optional<ContainerRegisterId>
Lowerer::lower_hir_vhdl_environment_call_path_container_expression(
    const semantic::ExpressionId expression_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && (source.text == "@vhdl-dereference"
            || source.text == "@vhdl-member:all")
        && source.operands.size() == 1U) {
        return lower_hir_vhdl_environment_call_path_container_expression(
            source.operands.front());
    }
    const auto api = frontend::vhdl_simulator_api(source.text);
    if (api == frontend::VhdlSimulatorApi::get_call_path) {
        if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
            || (source.kind != semantic::vhdl::ExpressionKind::name
                && source.kind != semantic::vhdl::ExpressionKind::call)
            || !source.operands.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV GET_CALL_PATH requires VHDL-2019 and accepts no arguments",
                hir_source_span(source.source));
            return std::nullopt;
        }
        const auto type = hir_vhdl_environment_call_path_container_type();
        const auto destination = allocate_container_register(type);
        const auto span = hir_source_span(source.source);
        process_.operations.emplace_back(VhdlEnvironmentGetCallPath {
            destination,
            runtime_source_location(span),
            debug_scope_name(),
        });
        return destination;
    }
    const auto binding = hir_container_object_binding(expression_id);
    if (!binding || !binding->local
        || !is_hir_vhdl_environment_call_path(binding->declaration)) {
        return std::nullopt;
    }
    return *binding->local;
}

std::optional<Lowerer::HirVhdlCallPathMemberSelection>
Lowerer::hir_vhdl_environment_call_path_member_selection(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    auto selected = specialized_hir_unit_->find_expression(expression_id);
    if (!selected || selected->vhdl == nullptr) {
        return std::nullopt;
    }
    if (selected->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && (selected->vhdl->text == "@vhdl-dereference"
            || selected->vhdl->text == "@vhdl-member:all")
        && selected->vhdl->operands.size() == 1U) {
        selected = specialized_hir_unit_->find_expression(
            selected->vhdl->operands.front());
    }
    constexpr auto member_prefix = std::string_view { "@vhdl-member:" };
    if (!selected || selected->vhdl == nullptr
        || selected->vhdl->kind != semantic::vhdl::ExpressionKind::call
        || !selected->vhdl->text.starts_with(member_prefix)
        || selected->vhdl->operands.size() != 1U) {
        return std::nullopt;
    }
    const auto member_name = std::string_view { selected->vhdl->text }
        .substr(member_prefix.size());
    const auto member = equal_ignoring_ascii_case(member_name, "name") ? 0U
        : equal_ignoring_ascii_case(member_name, "file_name") ? 1U
        : equal_ignoring_ascii_case(member_name, "file_path") ? 2U
        : equal_ignoring_ascii_case(member_name, "file_line") ? 3U
                                                    : 4U;
    if (member >= 4U) {
        return std::nullopt;
    }
    const auto indexed = specialized_hir_unit_->find_expression(
        selected->vhdl->operands.front());
    if (!indexed || indexed->vhdl == nullptr
        || indexed->vhdl->kind != semantic::vhdl::ExpressionKind::index
        || indexed->vhdl->operands.size() != 2U) {
        return std::nullopt;
    }
    auto base = indexed->vhdl->operands.front();
    const auto base_expression = specialized_hir_unit_->find_expression(base);
    if (base_expression && base_expression->vhdl != nullptr
        && base_expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && (base_expression->vhdl->text == "@vhdl-dereference"
            || base_expression->vhdl->text == "@vhdl-member:all")
        && base_expression->vhdl->operands.size() == 1U) {
        base = base_expression->vhdl->operands.front();
    }
    const auto declaration = hir_referenced_declaration(base);
    return declaration && is_hir_vhdl_environment_call_path(*declaration)
        ? std::optional { HirVhdlCallPathMemberSelection {
              *declaration,
              indexed->vhdl->operands.back(),
              member,
          } }
        : std::nullopt;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_vhdl_environment_call_path_string_selection(
    const semantic::ExpressionId expression_id)
{
    const auto selection
        = hir_vhdl_environment_call_path_member_selection(expression_id);
    if (!selection || selection->member >= 3U) {
        return std::nullopt;
    }
    const auto local = hir_local_container_registers_.find(
        selection->call_path.value());
    if (local == hir_local_container_registers_.end()) {
        return std::nullopt;
    }
    auto index = lower_hir_expression(selection->index, 32U);
    if (!index) {
        return std::nullopt;
    }
    if (register_width(*index) != 32U) {
        *index = resize_register(*index, 32U, true);
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(ContainerStringRead {
        destination,
        local->second,
        *index,
        true,
        false,
        false,
        { selection->member },
    });
    return destination;
}

std::optional<RegisterId>
Lowerer::lower_hir_vhdl_environment_call_path_scalar_selection(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto selection
        = hir_vhdl_environment_call_path_member_selection(expression_id);
    if (!selection || selection->member != 3U) {
        return std::nullopt;
    }
    const auto local = hir_local_container_registers_.find(
        selection->call_path.value());
    if (local == hir_local_container_registers_.end()) {
        return std::nullopt;
    }
    auto index = lower_hir_expression(selection->index, 32U);
    if (!index) {
        return std::nullopt;
    }
    if (register_width(*index) != 32U) {
        *index = resize_register(*index, 32U, true);
    }
    const auto destination = allocate_register(
        64U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(ContainerAggregateRead {
        destination,
        local->second,
        *index,
        { selection->member },
        true,
        false,
    });
    return expected_width != 0U && expected_width != 64U
        ? std::optional { resize_register(
              destination, expected_width, true) }
        : std::optional { destination };
}

bool Lowerer::is_hir_vhdl_environment_call_path_assignment(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::variable_assignment
        || !statement->vhdl->target || !statement->vhdl->value) {
        return false;
    }
    const auto declaration = hir_referenced_declaration(
        *statement->vhdl->target);
    return declaration
        && is_hir_vhdl_environment_call_path(*declaration);
}

bool Lowerer::lower_hir_vhdl_environment_call_path_assignment(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || !statement->vhdl->target || !statement->vhdl->value) {
        return false;
    }
    const auto destination = hir_container_object_binding(
        *statement->vhdl->target);
    const auto source
        = lower_hir_vhdl_environment_call_path_container_expression(
            *statement->vhdl->value);
    if (!destination || !destination->local || !source) {
        report(
            "FSIM-ELAB-VHENV-006",
            "STD.ENV call-path assignment requires compatible CALL_PATH_VECTOR_PTR values",
            hir_source_span(statement->vhdl->source));
        return true;
    }
    process_.operations.emplace_back(CopyContainerRegister {
        *destination->local, *source });
    return true;
}

std::optional<Lowerer::HirVhdlDirectoryStringSelection>
Lowerer::hir_vhdl_environment_directory_string_selection(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto directory_name = [&](const semantic::ExpressionId name_id,
                                    const std::string_view suffix)
        -> std::optional<semantic::DeclarationId> {
        const auto name = specialized_hir_unit_->find_expression(name_id);
        if (!name || name->vhdl == nullptr
            || name->vhdl->kind != semantic::vhdl::ExpressionKind::name
            || name->vhdl->text.size() <= suffix.size()
            || !equal_ignoring_ascii_case(
                std::string_view { name->vhdl->text }.substr(
                    name->vhdl->text.size() - suffix.size()),
                suffix)) {
            return std::nullopt;
        }
        auto declaration = hir_referenced_declaration(name_id);
        if (!declaration
            || !is_hir_vhdl_environment_directory(*declaration)) {
            const auto base = std::string_view { name->vhdl->text }.substr(
                0U, name->vhdl->text.size() - suffix.size());
            semantic::vhdl::Name reference;
            reference.spelling = base;
            reference.canonical = base;
            reference.source = name->vhdl->source;
            declaration = hir_resolver()
                              .resolve_vhdl(reference, name->vhdl->scope)
                              .unique();
        }
        return declaration
                && is_hir_vhdl_environment_directory(*declaration)
            ? declaration : std::nullopt;
    };

    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.text == "@vhdl-dereference"
        && source.operands.size() == 1U) {
        const auto directory = directory_name(
            source.operands.front(), ".name");
        if (directory) {
            return HirVhdlDirectoryStringSelection {
                *directory, std::nullopt };
        }
    }
    if (source.kind != semantic::vhdl::ExpressionKind::call
        || source.text != "@vhdl-member:all"
        || source.operands.size() != 1U) {
        return std::nullopt;
    }
    const auto indexed = specialized_hir_unit_->find_expression(
        source.operands.front());
    if (!indexed || indexed->vhdl == nullptr
        || indexed->vhdl->kind != semantic::vhdl::ExpressionKind::index
        || indexed->vhdl->operands.size() != 2U) {
        return std::nullopt;
    }
    const auto dereference = specialized_hir_unit_->find_expression(
        indexed->vhdl->operands.front());
    if (!dereference || dereference->vhdl == nullptr
        || dereference->vhdl->kind != semantic::vhdl::ExpressionKind::call
        || dereference->vhdl->text != "@vhdl-dereference"
        || dereference->vhdl->operands.size() != 1U) {
        return std::nullopt;
    }
    const auto directory = directory_name(
        dereference->vhdl->operands.front(), ".items");
    return directory
        ? std::optional { HirVhdlDirectoryStringSelection {
              *directory, indexed->vhdl->operands.back() } }
        : std::nullopt;
}

bool Lowerer::is_hir_vhdl_environment_directory_statement(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr) {
        return false;
    }
    using Api = frontend::VhdlSimulatorApi;
    const auto api = frontend::vhdl_simulator_api(
        hir_vhdl_std_env_procedure_spelling(
            statement->vhdl->procedure, statement->vhdl->scope));
    return api == Api::dir_open || api == Api::dir_close
        || api == Api::dir_workingdir || api == Api::dir_createdir
        || api == Api::dir_deletedir || api == Api::dir_deletefile;
}

bool Lowerer::lower_hir_vhdl_environment_directory_statement(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr) {
        return false;
    }
    const auto& source = *statement->vhdl;
    const auto span = hir_source_span(source.source);
    using Api = frontend::VhdlSimulatorApi;
    const auto api = frontend::vhdl_simulator_api(
        hir_vhdl_std_env_procedure_spelling(
            source.procedure, source.scope));
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV directory procedures require VHDL-2019",
            span);
        return true;
    }

    std::vector<std::string_view> formals;
    if (api == Api::dir_open) {
        formals = { "dir", "path", "status" };
    } else if (api == Api::dir_close) {
        formals = { "dir" };
    } else if (api == Api::dir_createdir) {
        formals = source.procedure_arguments.size() == 3U
            ? std::vector<std::string_view> {
                  "path", "parents", "status" }
            : std::vector<std::string_view> { "path", "status" };
    } else if (api == Api::dir_deletedir) {
        formals = source.procedure_arguments.size() == 3U
            ? std::vector<std::string_view> {
                  "path", "recursive", "status" }
            : std::vector<std::string_view> { "path", "status" };
    } else {
        formals = { "path", "status" };
    }
    std::vector<std::optional<semantic::ExpressionId>> actuals(
        formals.size());
    std::size_t positional { };
    bool valid = source.procedure_arguments.size() == formals.size();
    for (const auto& association : source.procedure_arguments) {
        auto formal = positional;
        if (!association.formal) {
            ++positional;
        } else {
            const auto spelling = association.formal->text();
            const auto found = std::ranges::find_if(
                formals, [&](const std::string_view candidate) {
                    return equal_ignoring_ascii_case(candidate, spelling);
                });
            if (found == formals.end()) {
                valid = false;
                continue;
            }
            formal = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        }
        if (formal >= actuals.size() || actuals[formal]) {
            valid = false;
        } else {
            actuals[formal] = association.actual;
        }
    }
    valid = valid && std::ranges::all_of(
        actuals, [](const auto& actual) { return actual.has_value(); });
    if (!valid) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV directory procedure has no matching standardized association profile",
            span);
        return true;
    }

    std::optional<ContainerRegisterId> directory;
    std::optional<StringRegisterId> path;
    std::optional<RegisterId> option;
    std::optional<RegisterId> status;
    std::size_t next { };
    if (api == Api::dir_open || api == Api::dir_close) {
        const auto actual = *actuals[next++];
        const auto binding = hir_container_object_binding(actual);
        if (!binding || !binding->local
            || !is_hir_vhdl_environment_directory(
                binding->declaration)) {
            report(
                "FSIM-ELAB-VHENV-006",
                "STD.ENV directory DIR must be a writable DIRECTORY variable",
                hir_source_span(
                    specialized_hir_unit_->find_expression(actual)
                        ->vhdl->source));
            return true;
        }
        directory = *binding->local;
    }
    if (api != Api::dir_close) {
        const auto actual = *actuals[next++];
        path = lower_hir_string_expression(actual);
        if (!path) {
            report(
                "FSIM-ELAB-VHENV-006",
                "STD.ENV directory PATH must be STRING-compatible",
                hir_source_span(
                    specialized_hir_unit_->find_expression(actual)
                        ->vhdl->source));
            return true;
        }
    }
    if ((api == Api::dir_createdir || api == Api::dir_deletedir)
        && actuals.size() == 3U) {
        const auto actual = *actuals[next++];
        const auto domain = hir_expression_domain(
            actual, hir_process_scope_);
        if (domain != frontend::ValueDomain::Boolean) {
            report(
                "FSIM-ELAB-VHENV-006",
                "STD.ENV directory option must be BOOLEAN",
                hir_source_span(
                    specialized_hir_unit_->find_expression(actual)
                        ->vhdl->source));
            return true;
        }
        option = lower_hir_expression(actual, 1U);
        if (!option) {
            return true;
        }
    }
    if (api != Api::dir_close) {
        const auto actual = *actuals[next];
        const auto declaration_id = hir_referenced_declaration(actual);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        const auto binding = declaration_id
            ? hir_runtime_binding(
                  *declaration_id, hir_process_scope_, true)
            : std::nullopt;
        const auto status_kind = api == Api::dir_createdir
            ? Api::dir_create_status
            : api == Api::dir_deletedir ? Api::dir_delete_status
            : api == Api::dir_deletefile ? Api::file_delete_status
                                         : Api::dir_open_status;
        const auto status_type = [&](const auto& self,
                                     const semantic::vhdl::SubtypeIndication&
                                         subtype,
                                     std::unordered_set<std::uint32_t>&
                                         visiting) -> bool {
            if (vhdl_environment_type_api(
                    subtype.type_mark.spelling) == status_kind) {
                return true;
            }
            if (!subtype.type_mark.target.valid()
                || !visiting.insert(
                    subtype.type_mark.target.value()).second) {
                return false;
            }
            const auto type = specialized_hir_unit_->find_type(
                subtype.type_mark.target);
            return type && type->vhdl != nullptr
                && self(self, type->vhdl->base, visiting);
        };
        std::unordered_set<std::uint32_t> visiting;
        const bool matching_type = declaration
            && declaration->vhdl != nullptr
            && declaration->vhdl->object_class
                == semantic::vhdl::ObjectClass::variable
            && declaration->vhdl->subtype
            && status_type(
                status_type, *declaration->vhdl->subtype, visiting);
        if (!matching_type || !binding || !binding->local
            || binding->width != 3U) {
            report(
                "FSIM-ELAB-VHENV-006",
                "STD.ENV directory STATUS must be a writable matching status variable",
                hir_source_span(
                    specialized_hir_unit_->find_expression(actual)
                        ->vhdl->source));
            return true;
        }
        status = *binding->local;
    }
    using Kind = VhdlEnvironmentDirectoryKind;
    const auto kind = api == Api::dir_open ? Kind::open
        : api == Api::dir_close ? Kind::close
        : api == Api::dir_workingdir ? Kind::set_working_directory
        : api == Api::dir_createdir ? Kind::create_directory
        : api == Api::dir_deletedir ? Kind::delete_directory
                                     : Kind::delete_file;
    process_.operations.emplace_back(VhdlEnvironmentDirectory {
        kind, status, std::nullopt, directory, path, option });
    return true;
}

std::optional<frontend::VhdlSimulatorApi>
Lowerer::hir_vhdl_assert_expression_api(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.kind != semantic::vhdl::ExpressionKind::name
        && source.kind != semantic::vhdl::ExpressionKind::call) {
        return std::nullopt;
    }
    const auto api = frontend::vhdl_simulator_api(source.text);
    if (!vhdl_assert_function_api(api)) {
        return std::nullopt;
    }
    auto name = source.referenced_name.value_or(semantic::vhdl::Name { });
    if (name.spelling.empty()) {
        name.spelling = source.text;
        name.canonical = source.text;
        name.source = source.source;
    }
    const auto resolver = hir_resolver();
    return resolver.vhdl_standard_package_member_visible(
               name, source.scope)
        ? std::optional { api } : std::nullopt;
}

bool Lowerer::is_hir_vhdl_assert_statement(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::procedure_call) {
        return false;
    }
    const auto& source = *statement->vhdl;
    const auto api = frontend::vhdl_simulator_api(
        source.procedure.canonical.empty()
            ? source.procedure.spelling
            : source.procedure.canonical);
    if (!vhdl_assert_procedure_api(api)) {
        return false;
    }
    const auto resolver = hir_resolver();
    return resolver.vhdl_standard_package_member_visible(
        source.procedure, source.scope);
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_assert_level(
    const semantic::ExpressionId expression_id)
{
    // Keep the STD.ENV state index identical to the severity used by report
    // statements.  General enumeration lowering may encounter relocated or
    // duplicated standard-package type records before it finds the canonical
    // SEVERITY_LEVEL declaration.
    if (const auto level
        = hir_vhdl_standard_enumeration_literal(expression_id)) {
        const auto destination = allocate_register(
            2U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, PackedLogic4::from_aval_bval(
                             2U, *level, 0U) });
        return destination;
    }
    return lower_hir_expression(expression_id, 2U);
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_assert_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto api = hir_vhdl_assert_expression_api(expression_id);
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!api || !expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
        report(
            "FSIM-ELAB-VHENV-001",
            "the STD.ENV VHDL assert API requires VHDL-2019",
            span);
        return std::nullopt;
    }
    if (*api == frontend::VhdlSimulatorApi::get_vhdl_assert_format) {
        return std::nullopt;
    }

    const bool accepts_level
        = *api != frontend::VhdlSimulatorApi::get_vhdl_read_severity;
    std::optional<semantic::ExpressionId> level_actual;
    bool valid = source.operands.size() <= (accepts_level ? 1U : 0U)
        && (source.argument_names.empty()
            || source.argument_names.size() == source.operands.size());
    if (!source.operands.empty()) {
        const auto name = source.argument_names.empty()
            ? std::string_view { }
            : std::string_view { source.argument_names.front() };
        valid = valid && (name.empty() || equal_ignoring_ascii_case(name, "level"));
        level_actual = source.operands.front();
    }
    if (!valid) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV VHDL assert query has no matching standardized "
            "association profile",
            span);
        return std::nullopt;
    }

    std::optional<RegisterId> level;
    if (level_actual) {
        level = lower_hir_vhdl_assert_level(*level_actual);
        if (!level) {
            return std::nullopt;
        }
    } else if (*api == frontend::VhdlSimulatorApi::get_vhdl_assert_enable) {
        level = allocate_register(2U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            *level, PackedLogic4::from_aval_bval(2U, 0U, 0U) });
    }

    using Api = frontend::VhdlSimulatorApi;
    using Kind = VhdlAssertApiKind;
    const auto width = *api == Api::get_vhdl_assert_count ? 64U
        : *api == Api::get_vhdl_read_severity ? 2U : 1U;
    if (expected_width != 0U && expected_width != width) {
        report(
            "FSIM-ELAB-VHENV-004",
            "STD.ENV VHDL assert query result is incompatible with its "
            "context",
            span);
        return std::nullopt;
    }
    const auto kind = *api == Api::is_vhdl_assert_failed
        ? Kind::is_failed
        : *api == Api::get_vhdl_assert_count ? Kind::get_count
        : *api == Api::get_vhdl_assert_enable ? Kind::get_enable
                                             : Kind::get_read_severity;
    const auto domain = *api == Api::is_vhdl_assert_failed
            || *api == Api::get_vhdl_assert_enable
        ? frontend::ValueDomain::Boolean
        : *api == Api::get_vhdl_assert_count
        ? frontend::ValueDomain::Integer
        : frontend::ValueDomain::Bit2;
    const auto destination = allocate_register(width, domain);
    process_.operations.emplace_back(VhdlAssertApi {
        kind,
        destination,
        std::nullopt,
        level,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        runtime_source_location(span),
    });
    return destination;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_vhdl_assert_string_expression(
    const semantic::ExpressionId expression_id)
{
    const auto api = hir_vhdl_assert_expression_api(expression_id);
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!api
        || *api != frontend::VhdlSimulatorApi::get_vhdl_assert_format
        || !expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    const auto named = source.argument_names.empty()
        || (source.argument_names.size() == 1U
            && (source.argument_names.front().empty()
                || equal_ignoring_ascii_case(
                    source.argument_names.front(), "level")));
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
        || source.operands.size() != 1U || !named) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV GETVHDLASSERTFORMAT requires VHDL-2019 and one "
            "SEVERITY_LEVEL actual",
            span);
        return std::nullopt;
    }
    const auto level = lower_hir_vhdl_assert_level(
        source.operands.front());
    if (!level) {
        return std::nullopt;
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(VhdlAssertApi {
        VhdlAssertApiKind::get_format,
        std::nullopt,
        destination,
        *level,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        runtime_source_location(span),
    });
    return destination;
}

bool Lowerer::lower_hir_vhdl_assert_statement(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || !is_hir_vhdl_assert_statement(statement_id)) {
        return false;
    }
    const auto& source = *statement->vhdl;
    const auto span = hir_source_span(source.source);
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
        report(
            "FSIM-ELAB-VHENV-001",
            "the STD.ENV VHDL assert API requires VHDL-2019",
            span);
        return true;
    }
    using Api = frontend::VhdlSimulatorApi;
    const auto api = frontend::vhdl_simulator_api(
        source.procedure.canonical.empty()
            ? source.procedure.spelling
            : source.procedure.canonical);
    const auto location = runtime_source_location(span);
    const auto& associations = source.procedure_arguments;
    if (api == Api::clear_vhdl_assert) {
        if (!associations.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV CLEARVHDLASSERT does not accept arguments",
                span);
            return true;
        }
        process_.operations.emplace_back(VhdlAssertApi {
            VhdlAssertApiKind::clear,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            location,
        });
        return true;
    }

    const auto formal_name = [](const semantic::vhdl::ProcedureAssociation&
                                    association) {
        if (!association.formal) {
            return std::string_view { };
        }
        return association.formal->text();
    };
    const auto constant = [&](const std::size_t width,
                              const std::uint64_t value) {
        const auto result = allocate_register(
            width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            result, PackedLogic4::from_aval_bval(width, value, 0U) });
        return result;
    };
    if (api == Api::set_vhdl_assert_enable) {
        std::optional<semantic::ExpressionId> level_actual;
        std::optional<semantic::ExpressionId> enable_actual;
        bool valid = associations.size() <= 2U;
        for (std::size_t index { }; index < associations.size(); ++index) {
            const auto name = formal_name(associations[index]);
            if (equal_ignoring_ascii_case(name, "level")) {
                valid = valid && !level_actual;
                level_actual = associations[index].actual;
            } else if (equal_ignoring_ascii_case(name, "enable")) {
                valid = valid && !enable_actual;
                enable_actual = associations[index].actual;
            } else if (!name.empty()) {
                valid = false;
            } else if (index == 0U && associations.size() == 1U) {
                const auto domain = hir_expression_domain(
                    associations[index].actual, hir_process_scope_);
                if (domain == frontend::ValueDomain::Boolean) {
                    enable_actual = associations[index].actual;
                } else {
                    level_actual = associations[index].actual;
                }
            } else if (index == 0U) {
                level_actual = associations[index].actual;
            } else {
                enable_actual = associations[index].actual;
            }
        }
        if (!valid) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETVHDLASSERTENABLE has no matching standardized "
                "association profile",
                span);
            return true;
        }
        const auto level = level_actual
            ? lower_hir_vhdl_assert_level(*level_actual)
            : std::optional<RegisterId> { };
        const auto enable = enable_actual
            ? lower_hir_expression(*enable_actual, 1U)
            : std::optional<RegisterId> { constant(1U, 1U) };
        if ((level_actual && !level) || !enable) {
            return true;
        }
        process_.operations.emplace_back(VhdlAssertApi {
            VhdlAssertApiKind::set_enable,
            std::nullopt,
            std::nullopt,
            level,
            enable,
            std::nullopt,
            std::nullopt,
            location,
        });
        return true;
    }
    if (api == Api::set_vhdl_read_severity) {
        const auto valid = associations.size() <= 1U
            && (associations.empty()
                || formal_name(associations.front()).empty()
                || equal_ignoring_ascii_case(
                    formal_name(associations.front()), "level"));
        if (!valid) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETVHDLREADSEVERITY accepts at most one "
                "SEVERITY_LEVEL actual",
                span);
            return true;
        }
        const auto level = associations.empty()
            ? std::optional<RegisterId> { constant(2U, 3U) }
            : lower_hir_vhdl_assert_level(
                  associations.front().actual);
        if (!level) {
            return true;
        }
        process_.operations.emplace_back(VhdlAssertApi {
            VhdlAssertApiKind::set_read_severity,
            std::nullopt,
            std::nullopt,
            level,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            location,
        });
        return true;
    }

    std::array<std::optional<semantic::ExpressionId>, 3> actuals;
    std::size_t positional { };
    bool valid = associations.size() == 2U || associations.size() == 3U;
    for (const auto& association : associations) {
        const auto name = formal_name(association);
        const auto formal = name.empty() ? positional++
            : equal_ignoring_ascii_case(name, "level") ? 0U
            : equal_ignoring_ascii_case(name, "format") ? 1U
            : equal_ignoring_ascii_case(name, "valid") ? 2U : actuals.size();
        if (formal >= actuals.size() || actuals[formal]) {
            valid = false;
        } else {
            actuals[formal] = association.actual;
        }
    }
    valid = valid && actuals[0] && actuals[1]
        && (associations.size() == 2U || actuals[2]);
    if (!valid) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV SETVHDLASSERTFORMAT has no matching standardized "
            "association profile",
            span);
        return true;
    }
    const auto level = lower_hir_vhdl_assert_level(*actuals[0]);
    const auto format = lower_hir_string_expression(*actuals[1]);
    std::optional<RegisterId> valid_result;
    if (actuals[2]) {
        const auto declaration = hir_target_declaration(*actuals[2]);
        const auto binding = declaration
            ? hir_runtime_binding(*declaration, hir_process_scope_, true)
            : std::nullopt;
        if (!binding || binding->kind != HirRuntimeBindingKind::local
            || !binding->local || binding->width != 1U) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV SETVHDLASSERTFORMAT VALID must be a writable "
                "BOOLEAN variable",
                span);
            return true;
        }
        valid_result = *binding->local;
    }
    if (!level || !format) {
        return true;
    }
    process_.operations.emplace_back(VhdlAssertApi {
        VhdlAssertApiKind::set_format,
        std::nullopt,
        std::nullopt,
        *level,
        std::nullopt,
        *format,
        valid_result,
        location,
    });
    return true;
}

bool Lowerer::is_hir_vhdl_environment_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return false;
    }
    const auto& source = *expression->vhdl;
    using Api = frontend::VhdlSimulatorApi;
    const auto api = frontend::vhdl_simulator_api(source.text);
    const bool directory_function = api == Api::dir_open
        || api == Api::dir_itemexists || api == Api::dir_itemisdir
        || api == Api::dir_itemisfile || api == Api::dir_createdir
        || api == Api::dir_deletedir || api == Api::dir_deletefile
        || (api == Api::dir_workingdir && !source.operands.empty());
    if (directory_function) {
        return source.kind == semantic::vhdl::ExpressionKind::call;
    }
    if (api == Api::resolution_limit || api == Api::localtime
        || api == Api::gmtime || api == Api::epoch
        || api == Api::time_to_seconds || api == Api::seconds_to_time
        || api == Api::file_line) {
        return source.kind == semantic::vhdl::ExpressionKind::call
            || source.kind == semantic::vhdl::ExpressionKind::name;
    }
    if (source.kind != semantic::vhdl::ExpressionKind::binary
        || (source.text != "+" && source.text != "-")
        || source.operands.size() != 2U) {
        return false;
    }
    return hir_vhdl_time_record_expression(source.operands.front())
        || hir_vhdl_time_record_expression(source.operands.back());
}

std::optional<RegisterId>
Lowerer::lower_hir_vhdl_environment_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    using Api = frontend::VhdlSimulatorApi;
    const auto api = frontend::vhdl_simulator_api(source.text);
    if (vhdl_standard_ < frontend::vhdl_simulator_api_standard(api)) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV data/time functions require VHDL-2019",
            span);
        return std::nullopt;
    }
    if (api == Api::file_line) {
        if ((source.kind != semantic::vhdl::ExpressionKind::name
                && source.kind != semantic::vhdl::ExpressionKind::call)
            || !source.operands.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV FILE_LINE accepts no arguments",
                span);
            return std::nullopt;
        }
        if (expected_width != 0U && expected_width != 64U) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV FILE_LINE result requires a 64-bit integer context",
                span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4::from_aval_bval(
                64U, static_cast<std::uint64_t>(span.begin.line), 0U),
        });
        return destination;
    }
    const bool directory_function = api == Api::dir_open
        || api == Api::dir_itemexists || api == Api::dir_itemisdir
        || api == Api::dir_itemisfile || api == Api::dir_workingdir
        || api == Api::dir_createdir || api == Api::dir_deletedir
        || api == Api::dir_deletefile;
    if (directory_function) {
        std::vector<std::string_view> formals;
        if (api == Api::dir_open) {
            formals = { "dir", "path" };
        } else if (api == Api::dir_createdir) {
            formals = { "path", "parents" };
        } else if (api == Api::dir_deletedir) {
            formals = { "path", "recursive" };
        } else {
            formals = { "path" };
        }
        std::vector<std::optional<semantic::ExpressionId>> actuals(
            formals.size());
        std::size_t positional { };
        bool valid = source.kind == semantic::vhdl::ExpressionKind::call
            && source.operands.size() <= formals.size()
            && (source.argument_names.empty()
                || source.argument_names.size() == source.operands.size());
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto name = source.argument_names.empty()
                    || index >= source.argument_names.size()
                ? std::string_view { }
                : std::string_view { source.argument_names[index] };
            auto formal = positional;
            if (name.empty()) {
                ++positional;
            } else {
                const auto found = std::ranges::find_if(
                    formals, [&](const std::string_view candidate) {
                        return equal_ignoring_ascii_case(candidate, name);
                    });
                if (found == formals.end()) {
                    valid = false;
                    continue;
                }
                formal = static_cast<std::size_t>(
                    std::distance(formals.begin(), found));
            }
            if (formal >= actuals.size() || actuals[formal]) {
                valid = false;
            } else {
                actuals[formal] = source.operands[index];
            }
        }
        if (!valid || actuals.empty() || !actuals.front()
            || (api == Api::dir_open
                && (actuals.size() < 2U || !actuals[1]))) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV directory function has no matching standardized association profile",
                span);
            return std::nullopt;
        }

        std::optional<ContainerRegisterId> directory;
        std::size_t path_index { };
        if (api == Api::dir_open) {
            const auto binding = hir_container_object_binding(
                *actuals.front());
            if (!binding || !binding->local
                || !is_hir_vhdl_environment_directory(
                    binding->declaration)) {
                report(
                    "FSIM-ELAB-VHENV-006",
                    "STD.ENV DIR_OPEN DIR must be a writable DIRECTORY variable",
                    hir_source_span(
                        specialized_hir_unit_->find_expression(
                            *actuals.front())->vhdl->source));
                return std::nullopt;
            }
            directory = *binding->local;
            path_index = 1U;
        }
        const auto path = lower_hir_string_expression(
            *actuals[path_index]);
        if (!path) {
            report(
                "FSIM-ELAB-VHENV-006",
                "STD.ENV directory PATH must be STRING-compatible",
                hir_source_span(
                    specialized_hir_unit_->find_expression(
                        *actuals[path_index])->vhdl->source));
            return std::nullopt;
        }
        std::optional<RegisterId> option;
        if (actuals.size() > path_index + 1U
            && actuals[path_index + 1U]) {
            const auto domain = hir_expression_domain(
                *actuals[path_index + 1U], hir_process_scope_);
            if (domain != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-VHENV-006",
                    "STD.ENV directory option must be BOOLEAN",
                    hir_source_span(
                        specialized_hir_unit_->find_expression(
                            *actuals[path_index + 1U])->vhdl->source));
                return std::nullopt;
            }
            option = lower_hir_expression(
                *actuals[path_index + 1U], 1U);
            if (!option) {
                return std::nullopt;
            }
        }
        const bool boolean_result = api == Api::dir_itemexists
            || api == Api::dir_itemisdir || api == Api::dir_itemisfile;
        const auto result_width = boolean_result ? 1U : 3U;
        if (expected_width != 0U && expected_width != result_width) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV directory result is incompatible with its context",
                span);
            return std::nullopt;
        }
        using Kind = VhdlEnvironmentDirectoryKind;
        const auto kind = api == Api::dir_open ? Kind::open
            : api == Api::dir_itemexists ? Kind::item_exists
            : api == Api::dir_itemisdir ? Kind::item_is_directory
            : api == Api::dir_itemisfile ? Kind::item_is_file
            : api == Api::dir_workingdir ? Kind::set_working_directory
            : api == Api::dir_createdir ? Kind::create_directory
            : api == Api::dir_deletedir ? Kind::delete_directory
                                         : Kind::delete_file;
        const auto destination = allocate_register(
            result_width,
            boolean_result ? frontend::ValueDomain::Boolean
                           : frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(VhdlEnvironmentDirectory {
            kind, destination, std::nullopt, directory, path, option });
        return destination;
    }
    const auto require_width = [&](const std::size_t width) {
        if (expected_width == 0U || expected_width == width) {
            return true;
        }
        report(
            "FSIM-ELAB-VHENV-004",
            "STD.ENV data/time result width "
                + std::to_string(width)
                + " is incompatible with context width "
                + std::to_string(expected_width),
            span);
        return false;
    };
    const auto emit = [&](const VhdlEnvironmentTimeKind kind,
                          const std::size_t width,
                          const frontend::ValueDomain domain,
                          const std::optional<RegisterId> first,
                          const std::optional<RegisterId> second)
        -> std::optional<RegisterId> {
        if (!require_width(width)) {
            return std::nullopt;
        }
        const auto destination = allocate_register(width, domain);
        process_.operations.emplace_back(VhdlEnvironmentTime {
            kind, destination, first, second });
        return destination;
    };

    using Kind = VhdlEnvironmentTimeKind;
    if (api == Api::resolution_limit) {
        if (!source.operands.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV RESOLUTION_LIMIT does not accept arguments",
                span);
            return std::nullopt;
        }
        if (!require_width(64U)) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination, PackedLogic4::from_aval_bval(64U, 1U, 0U) });
        return destination;
    }
    if (api == Api::localtime || api == Api::gmtime) {
        if (source.operands.empty()) {
            return emit(
                api == Api::localtime ? Kind::current_local
                                      : Kind::current_utc,
                515U, frontend::ValueDomain::Bit2,
                std::nullopt, std::nullopt);
        }
        if (source.operands.size() != 1U) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV calendar conversion has no matching overload",
                span);
            return std::nullopt;
        }
        const auto operand = source.operands.front();
        const bool record = hir_vhdl_time_record_expression(operand);
        const auto operand_expression
            = specialized_hir_unit_->find_expression(operand);
        const auto operand_subtype = hir_vhdl_expression_subtype(operand);
        const bool real_time = (operand_expression
                && operand_expression->vhdl != nullptr
                && operand_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::real_literal)
            || (operand_subtype
                && equal_ignoring_ascii_case(
                    operand_subtype->type_mark.spelling, "real"));
        const auto argument_name = source.argument_names.empty()
            ? std::string_view { }
            : std::string_view { source.argument_names.front() };
        const auto matching_name = argument_name.empty()
            || equal_ignoring_ascii_case(
                argument_name, record ? "trec" : "timer");
        if ((!record && !real_time) || !matching_name) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV calendar conversion has no matching overload",
                span);
            return std::nullopt;
        }
        const auto value = lower_hir_expression(
            operand, record ? 515U : 64U);
        if (!value) {
            return std::nullopt;
        }
        const auto kind = api == Api::localtime
            ? record ? Kind::local_from_utc_record
                     : Kind::local_from_epoch
            : record ? Kind::utc_from_local_record
                     : Kind::utc_from_epoch;
        return emit(kind, 515U, frontend::ValueDomain::Bit2,
            *value, std::nullopt);
    }
    if (api == Api::epoch) {
        if (source.operands.empty()) {
            return emit(Kind::current_epoch, 64U,
                frontend::ValueDomain::Bit2,
                std::nullopt, std::nullopt);
        }
        if (source.operands.size() != 1U
            || !hir_vhdl_time_record_expression(
                source.operands.front())) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV EPOCH requires one TIME_RECORD argument",
                span);
            return std::nullopt;
        }
        const auto value = lower_hir_expression(
            source.operands.front(), 515U);
        return value
            ? emit(Kind::epoch_from_local, 64U,
                  frontend::ValueDomain::Bit2, *value, std::nullopt)
            : std::nullopt;
    }
    if (api == Api::time_to_seconds || api == Api::seconds_to_time) {
        if (source.operands.size() != 1U) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV time conversion requires exactly one argument",
                span);
            return std::nullopt;
        }
        const auto value = lower_hir_expression(
            source.operands.front(), 64U);
        return value
            ? emit(
                  api == Api::time_to_seconds
                      ? Kind::time_to_seconds : Kind::seconds_to_time,
                  64U,
                  api == Api::time_to_seconds
                      ? frontend::ValueDomain::Bit2
                      : frontend::ValueDomain::Integer,
                  *value, std::nullopt)
            : std::nullopt;
    }

    if (source.kind != semantic::vhdl::ExpressionKind::binary
        || source.operands.size() != 2U) {
        return std::nullopt;
    }
    const bool left_record = hir_vhdl_time_record_expression(
        source.operands.front());
    const bool right_record = hir_vhdl_time_record_expression(
        source.operands.back());
    const bool difference = source.text == "-"
        && left_record && right_record;
    if ((!left_record && !right_record)
        || (!difference && left_record == right_record)) {
        report(
            "FSIM-ELAB-VHENV-005",
            "STD.ENV TIME_RECORD arithmetic requires TIME_RECORD/REAL or TIME_RECORD/TIME_RECORD subtraction",
            span);
        return std::nullopt;
    }
    const auto first_id = difference || left_record
        ? source.operands.front() : source.operands.back();
    const auto second_id = difference || left_record
        ? source.operands.back() : source.operands.front();
    const auto first = lower_hir_expression(first_id, 515U);
    const auto second = lower_hir_expression(
        second_id, difference ? 515U : 64U);
    if (!first || !second) {
        return std::nullopt;
    }
    const auto kind = difference
        ? Kind::difference_seconds
        : source.text == "+"
        ? Kind::add_seconds
        : left_record ? Kind::subtract_seconds
                      : Kind::reverse_subtract_seconds;
    return emit(kind, difference ? 64U : 515U,
        frontend::ValueDomain::Bit2, *first, *second);
}

std::optional<StringRegisterId>
Lowerer::lower_hir_vhdl_environment_string_expression(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
        || source.operands.empty() || source.operands.size() > 2U) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV TO_STRING requires VHDL-2019, one TIME_RECORD, and an optional FRAC_DIGITS",
            span);
        return std::nullopt;
    }

    struct CallPathActual {
        enum class Kind : std::uint8_t {
            current,
            vector,
            element,
        };
        Kind kind { Kind::current };
        semantic::ExpressionId container;
        std::optional<semantic::ExpressionId> index;
    };
    const auto unwrap_call_path_access
        = [&](const auto& self, const semantic::ExpressionId candidate)
        -> semantic::ExpressionId {
        const auto value = specialized_hir_unit_->find_expression(candidate);
        if (!value || value->vhdl == nullptr
            || value->vhdl->kind != semantic::vhdl::ExpressionKind::call
            || (value->vhdl->text != "@vhdl-dereference"
                && value->vhdl->text != "@vhdl-member:all")
            || value->vhdl->operands.size() != 1U) {
            return candidate;
        }
        return self(self, value->vhdl->operands.front());
    };
    const auto call_path_actual
        = [&](const semantic::ExpressionId candidate)
        -> std::optional<CallPathActual> {
        const auto unwrapped = unwrap_call_path_access(
            unwrap_call_path_access, candidate);
        const auto value = specialized_hir_unit_->find_expression(unwrapped);
        if (!value || value->vhdl == nullptr) {
            return std::nullopt;
        }
        if (frontend::vhdl_simulator_api(value->vhdl->text)
            == frontend::VhdlSimulatorApi::get_call_path) {
            return CallPathActual {
                CallPathActual::Kind::current, unwrapped, std::nullopt };
        }
        if (value->vhdl->kind == semantic::vhdl::ExpressionKind::index
            && value->vhdl->operands.size() == 2U) {
            const auto container = value->vhdl->operands.front();
            const auto base = unwrap_call_path_access(
                unwrap_call_path_access, container);
            const auto declaration = hir_referenced_declaration(base);
            if (declaration
                && is_hir_vhdl_environment_call_path(*declaration)) {
                return CallPathActual {
                    CallPathActual::Kind::element,
                    container,
                    value->vhdl->operands.back(),
                };
            }
            return std::nullopt;
        }
        const auto declaration = hir_referenced_declaration(unwrapped);
        return declaration
                && is_hir_vhdl_environment_call_path(*declaration)
            ? std::optional { CallPathActual {
                  CallPathActual::Kind::vector,
                  candidate,
                  std::nullopt,
              } }
            : std::nullopt;
    };
    const bool named_call_path_profile = std::ranges::any_of(
        source.argument_names, [](const std::string& name) {
            return equal_ignoring_ascii_case(name, "call_path")
                || equal_ignoring_ascii_case(name, "separator");
        });
    const auto positional_call_path = call_path_actual(
        source.operands.front());
    if (named_call_path_profile || positional_call_path) {
        std::array<std::optional<semantic::ExpressionId>, 2> actuals;
        std::size_t positional { };
        bool valid { true };
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto name = index < source.argument_names.size()
                ? std::string_view { source.argument_names[index] }
                : std::string_view { };
            const auto formal = name.empty()
                ? positional++
                : equal_ignoring_ascii_case(name, "call_path") ? 0U
                : equal_ignoring_ascii_case(name, "separator") ? 1U
                                                       : actuals.size();
            if (formal >= actuals.size() || actuals[formal]) {
                valid = false;
                continue;
            }
            actuals[formal] = source.operands[index];
        }
        const auto actual = valid && actuals[0]
            ? call_path_actual(*actuals[0])
            : std::nullopt;
        if (!actual
            || (actual->kind == CallPathActual::Kind::element
                && actuals[1])) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV TO_STRING call-path overload has no matching standardized association profile",
                span);
            return std::nullopt;
        }
        if (actual->kind == CallPathActual::Kind::current) {
            const auto current = specialized_hir_unit_->find_expression(
                actual->container);
            if (!current || current->vhdl == nullptr
                || (current->vhdl->kind
                        != semantic::vhdl::ExpressionKind::name
                    && current->vhdl->kind
                        != semantic::vhdl::ExpressionKind::call)
                || !current->vhdl->operands.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV GET_CALL_PATH accepts no arguments",
                    span);
                return std::nullopt;
            }
        }
        std::optional<ContainerRegisterId> source_value;
        if (actual->kind != CallPathActual::Kind::current) {
            source_value
                = lower_hir_vhdl_environment_call_path_container_expression(
                    actual->container);
            if (!source_value) {
                return std::nullopt;
            }
        }
        std::optional<RegisterId> source_index;
        if (actual->index) {
            source_index = lower_hir_expression(*actual->index, 32U);
            if (!source_index) {
                return std::nullopt;
            }
            if (register_width(*source_index) != 32U) {
                *source_index = resize_register(*source_index, 32U, true);
            }
        }
        std::optional<StringRegisterId> separator;
        if (actuals[1]) {
            separator = lower_hir_string_expression(*actuals[1]);
        } else {
            separator = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                *separator, "\n" });
        }
        if (!separator) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV TO_STRING SEPARATOR must be STRING-compatible",
                span);
            return std::nullopt;
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(VhdlEnvironmentCallPath {
            destination,
            *separator,
            source_value,
            source_index,
            runtime_source_location(span),
            debug_scope_name(),
        });
        return destination;
    }

    std::array<std::optional<semantic::ExpressionId>, 2> actuals;
    std::size_t positional { };
    bool valid { true };
    for (std::size_t index { }; index < source.operands.size(); ++index) {
        const auto name = index < source.argument_names.size()
            ? std::string_view { source.argument_names[index] }
            : std::string_view { };
        const auto formal = name.empty()
            ? positional++
            : equal_ignoring_ascii_case(name, "trec") ? 0U
            : equal_ignoring_ascii_case(name, "frac_digits") ? 1U
                                                   : actuals.size();
        if (formal >= actuals.size() || actuals[formal]) {
            valid = false;
            continue;
        }
        actuals[formal] = source.operands[index];
    }
    if (!valid || !actuals[0]) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV TO_STRING has no matching standardized association profile",
            span);
        return std::nullopt;
    }
    const auto record = lower_hir_expression(*actuals[0], 515U);
    std::optional<RegisterId> digits;
    if (actuals[1]) {
        digits = lower_hir_expression(*actuals[1], 64U);
    } else {
        digits = allocate_register(64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            *digits, PackedLogic4::from_aval_bval(64U, 0U, 0U) });
    }
    if (!record || !digits) {
        return std::nullopt;
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(VhdlEnvironmentTimeToString {
        destination, *record, *digits });
    return destination;
}

bool Lowerer::is_hir_vhdl_reflection_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || (expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::call
            && expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::name)) {
        return false;
    }
    const auto& source = *expression->vhdl;
    if (equal_ignoring_ascii_case(source.text, "'reflect")) {
        return source.operands.size() == 1U;
    }
    const auto selected = selected_vhdl_name(source.text);
    if (!selected) {
        return false;
    }
    const auto object_id = find_vhdl_object(
        *specialized_hir_unit_, source.scope, selected->object);
    const auto object = object_id
        ? specialized_hir_unit_->find_declaration(*object_id)
        : std::nullopt;
    const auto declared_subtype = object && object->vhdl != nullptr
        ? object->vhdl->subtype
        : std::nullopt;
    const auto subtype = declared_subtype
        ? hir_effective_vhdl_subtype(*declared_subtype)
        : std::nullopt;
    const auto mirror = (declared_subtype
            && reflection_mirror_subtype(
                *specialized_hir_unit_, *declared_subtype))
        || (subtype
            && reflection_mirror_subtype(
                *specialized_hir_unit_, *subtype));
    if (!mirror) {
        return false;
    }
    return reflection_scalar_operation(selected->member).has_value()
        || equal_ignoring_ascii_case(selected->member, "to_enumeration")
        || equal_ignoring_ascii_case(selected->member, "to_integer")
        || equal_ignoring_ascii_case(selected->member, "to_floating")
        || equal_ignoring_ascii_case(selected->member, "to_physical")
        || equal_ignoring_ascii_case(selected->member, "to_record")
        || equal_ignoring_ascii_case(selected->member, "to_array")
        || equal_ignoring_ascii_case(selected->member, "to_access")
        || equal_ignoring_ascii_case(selected->member, "to_file")
        || equal_ignoring_ascii_case(selected->member, "to_protected")
        || equal_ignoring_ascii_case(selected->member, "element_subtype")
        || equal_ignoring_ascii_case(selected->member, "get");
}

bool Lowerer::is_hir_vhdl_reflection_string_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || (expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::call
            && expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::name)) {
        return false;
    }
    const auto selected = selected_vhdl_name(expression->vhdl->text);
    if (!selected || !reflection_string_operation(selected->member)) {
        return false;
    }
    const auto object_id = find_vhdl_object(
        *specialized_hir_unit_, expression->vhdl->scope,
        selected->object);
    const auto object = object_id
        ? specialized_hir_unit_->find_declaration(*object_id)
        : std::nullopt;
    const auto declared_subtype = object && object->vhdl != nullptr
        ? object->vhdl->subtype
        : std::nullopt;
    const auto subtype = declared_subtype
        ? hir_effective_vhdl_subtype(*declared_subtype)
        : std::nullopt;
    return (declared_subtype
            && reflection_mirror_subtype(
                *specialized_hir_unit_, *declared_subtype))
        || (subtype
            && reflection_mirror_subtype(
                *specialized_hir_unit_, *subtype));
}

std::optional<RegisterId>
Lowerer::lower_hir_vhdl_reflection_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (equal_ignoring_ascii_case(source.text, "'reflect")) {
        if (source.operands.size() != 1U || expected_width != 32U) {
            report(
                "FSIM-ELAB-VHREFLECT-001",
                "'reflect requires a resolved VHDL type or object and a matching reflection mirror context",
                hir_source_span(source.source));
            return std::nullopt;
        }
        const auto operand = specialized_hir_unit_->find_expression(
            source.operands.front());
        const auto reflected_object = operand && operand->vhdl != nullptr
            ? find_vhdl_object(
                  *specialized_hir_unit_,
                  operand->vhdl->scope,
                  operand->vhdl->text)
            : std::nullopt;
        auto declaration_id = reflected_object
            ? reflected_object
            : operand && operand->vhdl != nullptr
            && operand->vhdl->referenced_name
            && operand->vhdl->referenced_name->selected
            ? operand->vhdl->referenced_name->selected
            : hir_referenced_declaration(source.operands.front());
        if (!declaration_id && operand && operand->vhdl != nullptr) {
            auto scope = operand->vhdl->scope;
            const auto& model = specialized_hir_unit_->design().semantics;
            for (std::size_t depth { };
                depth <= model.scopes().size(); ++depth) {
                for (const auto& candidate : model.declarations()) {
                    if (candidate.scope != scope
                        || !equal_ignoring_ascii_case(
                            candidate.name, operand->vhdl->text)) {
                        continue;
                    }
                    const auto record
                        = specialized_hir_unit_->find_declaration(
                            candidate.id);
                    if (record && record->vhdl != nullptr
                        && (record->vhdl->form
                                == semantic::vhdl::DeclarationForm::type
                            || record->vhdl->form
                                == semantic::vhdl::DeclarationForm::subtype)) {
                        declaration_id = candidate.id;
                        break;
                    }
                }
                if (declaration_id) {
                    break;
                }
                const auto found = std::ranges::find(
                    model.scopes(), scope, &semantic::Scope::id);
                if (found == model.scopes().end() || !found->parent) {
                    break;
                }
                scope = *found->parent;
            }
        }
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        auto subtype = hir_vhdl_expression_subtype(source.operands.front());
        const semantic::vhdl::TypeDefinition* definition = nullptr;
        const bool value_mirror = subtype.has_value()
            && reflected_object.has_value();
        if (subtype && subtype->type_mark.target.valid()) {
            const auto type = specialized_hir_unit_->find_type(
                subtype->type_mark.target);
            definition = type ? type->vhdl : nullptr;
        } else if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->declared_type) {
            const auto type = specialized_hir_unit_->find_type(
                *declaration->vhdl->declared_type);
            definition = type ? type->vhdl : nullptr;
            if (definition != nullptr) {
                subtype = definition->base;
                subtype->type_mark.target = definition->id;
                subtype->type_mark.spelling = definition->name;
            }
        }
        if (!operand || !subtype) {
            report(
                "FSIM-ELAB-VHREFLECT-001",
                "'reflect requires a resolved VHDL type or object and a matching reflection mirror context",
                hir_source_span(source.source));
            return std::nullopt;
        }

        std::unordered_set<std::uint32_t> visiting;
        const auto describe = [&](const auto& self,
                                  semantic::vhdl::SubtypeIndication current,
                                  const std::uint64_t offset)
            -> std::optional<VhdlReflectionType> {
            current = hir_effective_vhdl_subtype(current).value_or(current);
            if (!current.type_mark.target.valid()) {
                VhdlReflectionType result;
                const auto type_name = simple_name_of(
                    current.type_mark.spelling);
                result.simple_name = type_name;
                result.signed_value = current.signed_value;
                result.lsb_offset = offset;
                result.packed_width = static_cast<std::uint32_t>(
                    vhdl_subtype_width(current).value_or(
                        equal_ignoring_ascii_case(type_name, "real")
                            ? 64U
                            : 0U));
                result.type_class = equal_ignoring_ascii_case(type_name, "real")
                    ? VhdlReflectionClass::floating
                    : VhdlReflectionClass::integer;
                if (!current.constraints.empty()) {
                    const auto& constraint = current.constraints.front();
                    const auto left = constraint.left_expression
                        ? specialized_hir_unit_
                              ->evaluate_integral_expression(
                                  *constraint.left_expression)
                        : constraint.left;
                    const auto right = constraint.right_expression
                        ? specialized_hir_unit_
                              ->evaluate_integral_expression(
                                  *constraint.right_expression)
                        : constraint.right;
                    if (left && right) {
                        result.ranges.push_back(VhdlReflectionRange {
                            *left, *right, !constraint.descending });
                    }
                }
                return result.packed_width != 0U
                    ? std::optional { std::move(result) }
                    : std::nullopt;
            }
            if (!visiting.insert(current.type_mark.target.value()).second) {
                return std::nullopt;
            }
            const auto view = specialized_hir_unit_->find_type(
                current.type_mark.target);
            if (!view || view->vhdl == nullptr) {
                visiting.erase(current.type_mark.target.value());
                return std::nullopt;
            }
            const auto& type = *view->vhdl;
            const bool builtin_real = equal_ignoring_ascii_case(
                    simple_name_of(type.name), "real")
                || equal_ignoring_ascii_case(
                    simple_name_of(current.type_mark.spelling),
                    "real");
            if ((type.form == semantic::vhdl::TypeForm::subtype
                    || type.form == semantic::vhdl::TypeForm::alias)
                && !builtin_real) {
                auto base = type.base;
                if (!current.constraints.empty()) {
                    base.constraints = current.constraints;
                }
                visiting.erase(current.type_mark.target.value());
                auto result = self(self, base, offset);
                if (result) {
                    result->simple_name = type.name;
                }
                return result;
            }
            VhdlReflectionType result;
            result.simple_name = type.name.empty()
                ? current.type_mark.spelling
                : type.name;
            result.signed_value = current.signed_value;
            result.lsb_offset = offset;
            if (const auto width = vhdl_subtype_width(current);
                width && *width <= std::numeric_limits<std::uint32_t>::max()) {
                result.packed_width = static_cast<std::uint32_t>(*width);
            }
            const auto range = [&](const semantic::vhdl::RangeConstraint& input)
                -> std::optional<VhdlReflectionRange> {
                const auto left = input.left_expression
                    ? specialized_hir_unit_->evaluate_integral_expression(
                          *input.left_expression)
                    : input.left;
                const auto right = input.right_expression
                    ? specialized_hir_unit_->evaluate_integral_expression(
                          *input.right_expression)
                    : input.right;
                return left && right
                    ? std::optional { VhdlReflectionRange {
                          *left, *right, !input.descending } }
                    : std::nullopt;
            };
            if (type.form == semantic::vhdl::TypeForm::enumeration) {
                result.type_class = VhdlReflectionClass::enumeration;
                for (const auto& literal : type.enumeration_literals) {
                    result.names.push_back(literal.spelling);
                }
                const auto* selected_range = !current.constraints.empty()
                    ? &current.constraints.front()
                    : type.scalar_range ? &*type.scalar_range : nullptr;
                if (selected_range) {
                    if (const auto converted = range(*selected_range)) {
                        result.ranges.push_back(*converted);
                    }
                }
            } else if (type.form
                == semantic::vhdl::TypeForm::physical) {
                result.type_class = VhdlReflectionClass::physical;
                if (result.packed_width == 0U) {
                    result.packed_width
                        = vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                        ? 64U
                        : 32U;
                }
                if (type.scalar_range) {
                    if (const auto converted = range(*type.scalar_range)) {
                        result.ranges.push_back(*converted);
                    }
                }
                std::unordered_map<std::string, std::int64_t> scales;
                for (const auto& unit : type.physical_units) {
                    std::optional<std::int64_t> scale = unit.scale_factor;
                    if (!scale && !unit.scale) {
                        scale = 1;
                    }
                    if (!scale && unit.scale) {
                        const auto scale_expression
                            = specialized_hir_unit_->find_expression(
                                *unit.scale);
                        constexpr std::string_view prefix {
                            "@vhdl-physical:"
                        };
                        if (scale_expression
                            && scale_expression->vhdl != nullptr
                            && scale_expression->vhdl->text.starts_with(prefix)
                            && scale_expression->vhdl->operands.size() == 1U) {
                            const auto referenced = std::string {
                                std::string_view {
                                    scale_expression->vhdl->text
                                }
                                    .substr(prefix.size())
                            };
                            const auto prior = scales.find(referenced);
                            const auto multiplier = specialized_hir_unit_
                                ->evaluate_integral_expression(
                                    scale_expression->vhdl->operands.front());
                            if (prior != scales.end() && multiplier
                                && *multiplier >= 0
                                && (*multiplier == 0
                                    || prior->second
                                        <= std::numeric_limits<
                                            std::int64_t>::max()
                                            / *multiplier)) {
                                scale = prior->second * *multiplier;
                            }
                        }
                    }
                    if (!scale || *scale <= 0) {
                        visiting.erase(current.type_mark.target.value());
                        return std::nullopt;
                    }
                    scales.insert_or_assign(unit.name, *scale);
                    result.names.push_back(unit.name);
                    result.scales.push_back(static_cast<std::uint64_t>(
                        *scale));
                }
            } else if (type.form == semantic::vhdl::TypeForm::record) {
                result.type_class = VhdlReflectionClass::record;
                std::uint64_t member_offset { };
                for (auto index = type.record_elements.size();
                    index != 0U; --index) {
                    const auto& member = type.record_elements[index - 1U];
                    const auto child = self(
                        self, member.subtype, member_offset);
                    if (!child) {
                        visiting.erase(current.type_mark.target.value());
                        return std::nullopt;
                    }
                    member_offset += child->packed_width;
                }
                member_offset = 0U;
                for (auto index = type.record_elements.size();
                    index != 0U; --index) {
                    const auto& member = type.record_elements[index - 1U];
                    const auto child = self(
                        self, member.subtype, member_offset);
                    result.names.insert(result.names.begin(), member.name);
                    result.children.insert(
                        result.children.begin(), *child);
                    member_offset += child->packed_width;
                }
            } else if (type.form == semantic::vhdl::TypeForm::array) {
                result.type_class = VhdlReflectionClass::array;
                for (std::size_t index { };
                    index < type.array_dimensions.size(); ++index) {
                    const auto* selected_range
                        = index < current.constraints.size()
                        ? &current.constraints[index]
                        : type.array_dimensions[index].constraint
                        ? &*type.array_dimensions[index].constraint
                        : nullptr;
                    if (selected_range) {
                        if (const auto converted = range(*selected_range)) {
                            result.ranges.push_back(*converted);
                        }
                    }
                }
                if (type.element_subtype) {
                    if (const auto child = self(
                            self, *type.element_subtype, 0U)) {
                        result.children.push_back(*child);
                    }
                }
            } else if (type.form == semantic::vhdl::TypeForm::access) {
                result.type_class = VhdlReflectionClass::access;
                result.packed_width = 32U;
                if (type.designated_subtype) {
                    if (const auto child = self(
                            self, *type.designated_subtype, 0U)) {
                        result.children.push_back(*child);
                    }
                }
            } else if (type.form == semantic::vhdl::TypeForm::file) {
                result.type_class = VhdlReflectionClass::file;
                result.packed_width = 32U;
                if (type.element_subtype) {
                    if (const auto child = self(
                            self, *type.element_subtype, 0U)) {
                        result.children.push_back(*child);
                    }
                }
            } else if (type.form
                    == semantic::vhdl::TypeForm::protected_type
                || type.form
                    == semantic::vhdl::TypeForm::protected_body) {
                result.type_class = VhdlReflectionClass::protected_type;
                result.packed_width = 0U;
            } else if (builtin_real) {
                result.type_class = VhdlReflectionClass::floating;
                result.simple_name = "real";
                result.packed_width = 64U;
            } else {
                result.type_class = VhdlReflectionClass::integer;
                if (type.scalar_range) {
                    if (const auto converted = range(*type.scalar_range)) {
                        result.ranges.push_back(*converted);
                    }
                }
            }
            visiting.erase(current.type_mark.target.value());
            return result;
        };
        auto descriptor = describe(describe, *subtype, 0U);
        if (!descriptor) {
            report(
                "FSIM-ELAB-VHREFLECT-001",
                "'reflect requires a bounded supported VHDL type",
                hir_source_span(source.source));
            return std::nullopt;
        }
        std::optional<RegisterId> value;
        if (value_mirror && descriptor->packed_width != 0U) {
            value = lower_hir_expression(
                source.operands.front(), descriptor->packed_width);
            if (!value) {
                report(
                    "FSIM-ELAB-VHREFLECT-001",
                    "a reflected VHDL value requires executable object storage",
                    hir_source_span(source.source));
                return std::nullopt;
            }
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        VhdlReflectionApi operation;
        operation.kind = value_mirror
            ? VhdlReflectionApiKind::create_value
            : VhdlReflectionApiKind::create_subtype;
        operation.destination = destination;
        operation.source = value;
        operation.result_width = 32U;
        if (value_mirror
            && descriptor->type_class == VhdlReflectionClass::access) {
            const auto access_subtype
                = hir_effective_vhdl_subtype(*subtype).value_or(*subtype);
            const auto heap = access_subtype.type_mark.target.valid()
                ? hir_vhdl_access_heaps_.find(
                      access_subtype.type_mark.target.value())
                : hir_vhdl_access_heaps_.end();
            if (heap != hir_vhdl_access_heaps_.end()) {
                operation.access_heap = heap->second.objects;
            }
        }
        operation.type = std::move(*descriptor);
        operation.source_location = runtime_source_location(
            hir_source_span(source.source));
        process_.operations.emplace_back(std::move(operation));
        return destination;
    }

    const auto selected = selected_vhdl_name(source.text);
    if (!selected || expected_width == 0U
        || expected_width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto object_id = find_vhdl_object(
        *specialized_hir_unit_, source.scope, selected->object);
    const auto object = object_id
        ? specialized_hir_unit_->find_declaration(*object_id)
        : std::nullopt;
    const auto object_subtype = object && object->vhdl != nullptr
            && object->vhdl->subtype
        ? hir_effective_vhdl_subtype(*object->vhdl->subtype)
        : std::nullopt;
    const auto declared_receiver_type
        = object && object->vhdl != nullptr && object->vhdl->subtype
        ? simple_name_of(
              object->vhdl->subtype->type_mark.spelling)
        : std::string_view { };
    const auto effective_receiver_type = object_subtype
        ? simple_name_of(object_subtype->type_mark.spelling)
        : std::string_view { };
    const auto receiver_is = [&](const std::string_view prefix) {
        const auto matches = [&](const std::string_view name) {
            return name.size() >= prefix.size()
                && equal_ignoring_ascii_case(name.substr(0U, prefix.size()), prefix);
        };
        return matches(declared_receiver_type)
            || matches(effective_receiver_type);
    };
    const auto binding = object_id
        ? hir_runtime_binding(*object_id, hir_process_scope_, true)
        : std::nullopt;
    if (!binding || !binding->local) {
        return std::nullopt;
    }
    auto kind = reflection_scalar_operation(selected->member);
    VhdlReflectionType conversion;
    if (!kind) {
        kind = VhdlReflectionApiKind::convert;
        if (equal_ignoring_ascii_case(selected->member, "to_enumeration")) {
            conversion.type_class = VhdlReflectionClass::enumeration;
        } else if (equal_ignoring_ascii_case(selected->member, "to_integer")) {
            conversion.type_class = VhdlReflectionClass::integer;
        } else if (equal_ignoring_ascii_case(selected->member, "to_floating")) {
            conversion.type_class = VhdlReflectionClass::floating;
        } else if (equal_ignoring_ascii_case(selected->member, "to_physical")) {
            conversion.type_class = VhdlReflectionClass::physical;
        } else if (equal_ignoring_ascii_case(selected->member, "to_record")) {
            conversion.type_class = VhdlReflectionClass::record;
        } else if (equal_ignoring_ascii_case(selected->member, "to_array")) {
            conversion.type_class = VhdlReflectionClass::array;
        } else if (equal_ignoring_ascii_case(selected->member, "to_access")) {
            conversion.type_class = VhdlReflectionClass::access;
        } else if (equal_ignoring_ascii_case(selected->member, "to_file")) {
            conversion.type_class = VhdlReflectionClass::file;
        } else if (equal_ignoring_ascii_case(selected->member, "to_protected")) {
            conversion.type_class = VhdlReflectionClass::protected_type;
        } else if (equal_ignoring_ascii_case(selected->member, "element_subtype")) {
            kind = receiver_is("array_")
                ? VhdlReflectionApiKind::array_element_subtype
                : VhdlReflectionApiKind::record_element_subtype;
        } else if (equal_ignoring_ascii_case(selected->member, "get")) {
            kind = receiver_is("access_")
                ? VhdlReflectionApiKind::access_get
                : VhdlReflectionApiKind::aggregate_get;
        } else {
            return std::nullopt;
        }
    }
    VhdlReflectionApi operation;
    operation.kind = *kind;
    operation.destination = allocate_register(
        expected_width,
        hir_expression_domain(expression_id, hir_process_scope_)
            .value_or(frontend::ValueDomain::Bit2));
    operation.receiver = *binding->local;
    operation.result_width = static_cast<std::uint32_t>(expected_width);
    operation.type = std::move(conversion);
    operation.source_location = runtime_source_location(
        hir_source_span(source.source));
    for (const auto operand_id : source.operands) {
        if (hir_expression_is_string(operand_id, hir_process_scope_)) {
            if (operation.string_argument) {
                return std::nullopt;
            }
            operation.string_argument = lower_hir_string_expression(
                operand_id);
        } else {
            auto argument = lower_hir_expression(operand_id, 64U);
            if (!argument) {
                return std::nullopt;
            }
            if (register_width(*argument) != 64U) {
                argument = resize_register(*argument, 64U, true);
            }
            operation.arguments.push_back(*argument);
        }
    }
    const auto destination = *operation.destination;
    process_.operations.emplace_back(std::move(operation));
    return destination;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_vhdl_reflection_string_expression(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto selected = expression && expression->vhdl != nullptr
        ? selected_vhdl_name(expression->vhdl->text)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr || !selected) {
        return std::nullopt;
    }
    const auto kind = reflection_string_operation(selected->member);
    const auto object_id = find_vhdl_object(
        *specialized_hir_unit_, expression->vhdl->scope,
        selected->object);
    const auto binding = object_id
        ? hir_runtime_binding(*object_id, hir_process_scope_, true)
        : std::nullopt;
    if (!kind || !binding || !binding->local) {
        return std::nullopt;
    }
    VhdlReflectionApi operation;
    operation.kind = *kind;
    operation.string_destination = allocate_string_register();
    operation.receiver = *binding->local;
    operation.source_location = runtime_source_location(
        hir_source_span(expression->vhdl->source));
    for (const auto operand_id : expression->vhdl->operands) {
        auto argument = lower_hir_expression(operand_id, 64U);
        if (!argument) {
            return std::nullopt;
        }
        if (register_width(*argument) != 64U) {
            argument = resize_register(*argument, 64U, true);
        }
        operation.arguments.push_back(*argument);
    }
    const auto destination = *operation.string_destination;
    process_.operations.emplace_back(std::move(operation));
    return destination;
}

} // namespace fsim::elaboration
